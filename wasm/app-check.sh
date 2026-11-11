#!/bin/sh
# Checks Nimadir mini-apps written in Arche (docs/sprints.md S38), each a
# package with a `headless` file of arche-wasm's options (the taps, the
# frames) and the `expected.txt` that Nimadir's own shell said to them
# (app-expected.sh writes it). Each app is built by nimadir-app.sh and run
# headless by arche-wasm, by the interpreter and by the JIT, in Tashkent's
# time zone and English, as Nimadir's shell ran it; what it says is held to
# expected.txt, both normalized alike: by the app's normalize.sed, when it
# has one (what changes from run to run, as a clock's time), then by
# tests/nimadir/normalize.sh. An app's own repository runs it in its CI;
# arche-os runs it for the phone's apps.
# Usage: app-check.sh <app-dir>...
# ARCHE names the compiler and ARCHE_WASM arche-wasm, as nimadir-app.sh.
set -eu
if [ $# -eq 0 ]; then
    echo "usage: app-check.sh <app-dir>..." >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
if [ -z "${ARCHE_WASM:-}" ]; then
    make -s -C "$here" > /dev/null
fi
wasm=${ARCHE_WASM:-$here/build/arche-wasm}
wasm=$(cd "$(dirname "$wasm")" && pwd)/$(basename "$wasm")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
failed=0
passed=0
for dir in "$@"; do
    name=$(basename "$dir")
    if [ ! -f "$dir/headless" ] || [ ! -f "$dir/expected.txt" ]; then
        echo "FAIL $name has no headless test (headless, expected.txt)"
        failed=$((failed + 1))
        continue
    fi
    if ! sh "$here/nimadir-app.sh" "$dir" "$work/$name.wasm" > "$work/err" 2>&1; then
        echo "FAIL $name does not build:"
        head -20 "$work/err"
        failed=$((failed + 1))
        continue
    fi
    for mode in "" --jit; do
        # In a copy of the app's directory, fresh for each run: a folder
        # the app is given (--share) is as the repository has it, whatever
        # a run before wrote there.
        rm -rf "$work/cwd"
        cp -R "$dir" "$work/cwd"
        # shellcheck disable=SC2046,SC2086
        (cd "$work/cwd" && "$wasm" nimadir "$work/$name.wasm" $(cat headless) --locale en-US --time-zone Asia/Tashkent --utc-offset 300 $mode) > "$work/out" 2>&1 || true
        if [ -f "$dir/normalize.sed" ]; then
            sed -f "$dir/normalize.sed" "$work/out" | sh "$here/tests/nimadir/normalize.sh" > "$work/got"
        else
            sh "$here/tests/nimadir/normalize.sh" < "$work/out" > "$work/got"
        fi
        if diff -u "$dir/expected.txt" "$work/got" > "$work/diff"; then
            passed=$((passed + 1))
        else
            how="the interpreter"
            [ -z "$mode" ] || how="the JIT"
            echo "FAIL $name, by $how: not what Nimadir says:"
            head -30 "$work/diff"
            failed=$((failed + 1))
        fi
    done
done
if [ "$failed" -ne 0 ]; then
    exit 1
fi
echo "app-check: $passed runs, each as Nimadir's shell runs it"
