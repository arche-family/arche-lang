#!/bin/sh
# Nimadir's mini-apps on arche-wasm (docs/sprints.md S31): counter, hello and
# store run unrebuilt in a shell with no window, and runaway is stopped by
# the shell's limits: a second a frame, 512 MiB of memory, and what a frame
# may send. An app that imports what the shell does not give is refused.
# Usage: nimadir-check.sh <arche-wasm>
set -eu
here=$(cd "$(dirname "$0")" && pwd)
wasm=$1
apps=$here/tests/nimadir
failed=0

# expect <description> <status> <line that must appear> -- <arche-wasm args>...
expect() {
    what=$1
    want_status=$2
    want_line=$3
    shift 4
    set +e
    out=$("$wasm" nimadir "$@" 2>&1)
    status=$?
    set -e
    if [ "$status" -ne "$want_status" ] || ! printf '%s\n' "$out" | grep -qxF -- "$want_line"; then
        echo "FAIL $what (status $status, wanted $want_status and '$want_line'):"
        printf '%s\n' "$out" | head -20
        failed=$((failed + 1))
    else
        echo "PASS $what"
    fi
}

expect "counter draws its count" 0 "label: Count: 0" -- "$apps/counter.component.wasm" --frames 1
expect "counter counts a click" 0 "label: Count: 1" -- "$apps/counter.component.wasm" --frames 3 --click Increment
expect "hello greets" 0 "label: Hello from a *different* mini-app!" -- "$apps/hello.component.wasm" --frames 1
expect "hello opens the counter" 0 "open: counter" -- "$apps/hello.component.wasm" --frames 2 --click "Open the counter"
expect "store lists the registry" 0 "      \"3 apps\"" -- "$apps/store.component.wasm" --frames 1 --tree
expect "store filters as one types" 0 "      <a class=\"tile\" href=\"counter.component.wasm\" hidden>" -- "$apps/store.component.wasm" --frames 2 --type "#q" hel --tree
expect "runaway spinning is stopped by the frame's deadline" 3 "stopped: deadline exceeded" -- "$apps/runaway.component.wasm" --frames 2 --click "Spin forever"
expect "runaway eating memory is stopped at 512 MiB" 3 "stopped: memory limit exceeded" -- "$apps/runaway.component.wasm" --frames 2 --click "Eat memory"
expect "runaway flooding the shell is stopped" 3 "stopped: host limit exceeded" -- "$apps/runaway.component.wasm" --frames 2 --click "Flood the shell"
expect "a full store is said, not a trap" 0 "log: runaway: storage is full: 1 MiB per app" -- "$apps/runaway.component.wasm" --frames 2 --click "Overfill storage"
expect "an import the shell does not give is refused" 1 "arche-wasm: the component did not instantiate: imports what the shell does not give" -- "$apps/counter.component.wasm" --deny now-millis

if [ "$failed" -ne 0 ]; then
    echo "nimadir-check: $failed failed"
    exit 1
fi
echo "nimadir-check: all passed"
