#!/bin/sh
# Nimadir's mini-apps on arche-wasm (docs/sprints.md S31): counter, hello,
# gallery and store run unrebuilt in a shell with no window, and say what
# Nimadir's own shell says to the same cases of its test suite
# (tests/nimadir/cases.txt, expected/). runaway is stopped by the shell's
# limits: a second a frame, 512 MiB of memory, and what a frame may send.
# An app that imports what the shell does not give is refused. Each runs
# in the interpreter, and again with the JIT (--jit).
# Usage: nimadir-check.sh <arche-wasm>
set -eu
here=$(cd "$(dirname "$0")" && pwd)
wasm=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
apps=$here/tests/nimadir
failed=0

# The apps where Nimadir's registry says they are, so that both shells are
# run alike from one directory.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for app in counter hello store gallery runaway; do
    mkdir -p "$work/mini-apps/$app"
    cp "$apps/$app.component.wasm" "$work/mini-apps/$app/"
done

# The same as Nimadir: each case, normalized, against what Nimadir said.
grep -v '^#' "$apps/cases.txt" | {
    while read -r name args; do
        [ -n "$name" ] || continue
        for mode in "" --jit; do
            # shellcheck disable=SC2086
            if ! (cd "$work" && "$wasm" nimadir $args $mode --registry "$apps/registry.list") > "$work/out" 2>&1; then
                echo "FAIL $name $mode: arche-wasm failed:"
                head -20 "$work/out"
                failed=$((failed + 1))
                continue
            fi
            sh "$apps/normalize.sh" < "$work/out" > "$work/got"
            if ! diff -u "$apps/expected/$name.txt" "$work/got" > "$work/diff"; then
                echo "FAIL $name $mode: not what Nimadir says:"
                head -30 "$work/diff"
                failed=$((failed + 1))
            else
                echo "PASS $name $mode: as Nimadir"
            fi
        done
    done
    echo "$failed" > "$work/failed"
}
failed=$(cat "$work/failed")

# expect <description> <status> <line that must appear> -- <arche-wasm args>...
expect() {
    what=$1
    want_status=$2
    want_line=$3
    shift 4
    app=$1
    shift
    for mode in "" --jit; do
        set +e
        out=$("$wasm" nimadir "$app" $mode "$@" 2>&1)
        status=$?
        set -e
        if [ "$status" -ne "$want_status" ] || ! printf '%s\n' "$out" | grep -qxF -- "$want_line"; then
            echo "FAIL $what $mode (status $status, wanted $want_status and '$want_line'):"
            printf '%s\n' "$out" | head -20
            failed=$((failed + 1))
        else
            echo "PASS $what $mode"
        fi
    done
}

expect "runaway spinning is stopped by the frame's deadline" 3 "stopped: deadline exceeded" -- "$apps/runaway.component.wasm" --frames 2 --script 1:0
expect "runaway eating memory is stopped at 512 MiB" 3 "stopped: memory limit exceeded" -- "$apps/runaway.component.wasm" --frames 2 --script 1:1
expect "runaway flooding the shell is stopped" 3 "stopped: host limit exceeded" -- "$apps/runaway.component.wasm" --frames 2 --script 1:2
expect "a full store is said, not a trap" 0 "   [wasm log] runaway: storage is full: 1 MiB per app" -- "$apps/runaway.component.wasm" --frames 2 --script 1:3
expect "an import the shell does not give is refused" 1 "arche-wasm: the component did not instantiate: imports what the shell does not give" -- "$apps/counter.component.wasm" --deny now-millis

if [ "$failed" -ne 0 ]; then
    echo "nimadir-check: $failed failed"
    exit 1
fi
echo "nimadir-check: all passed"
