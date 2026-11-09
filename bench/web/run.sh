#!/usr/bin/env bash
# The web benchmark (docs/sprints.md S33): throughput and latency of the
# web application (examples/webapp) and of a WASM handler (arche-wasm
# serve), one worker each, in the clear, measured by load.arche: 16
# keep-alive connections of 2000 requests. The numbers are compared with
# baseline.txt, the line of this machine's kind (BENCH_MACHINE: local, or
# ci under CI): a run fails when its throughput falls under a third of the
# baseline's, or its p99 grows past four times it, which only a regression
# does, not a busy machine. --record writes this run as the baseline.
# Usage: run.sh [--record]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../..
machine=${BENCH_MACHINE:-local}
if [ -n "${CI:-}" ]; then
    machine=${BENCH_MACHINE:-ci}
fi
make -s -C "$root/examples/webapp" > /dev/null
make -s -C "$root/wasm" > /dev/null
mkdir -p "$here/build"
"$root/compiler/arche-build" "$here/load.arche" "$here/build/load"
work=$(mktemp -d)
cleanup() {
    jobs -p | xargs -r kill 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT
wat2wasm=${WAT2WASM:-$root/wasm/build/spec/wabt/usr/bin/wat2wasm}
"$wat2wasm" "$root/wasm/tests/handler/handler.wat" -o "$work/handler.wasm"

port=$((20000 + RANDOM % 20000))
"$root/examples/webapp/build/webapp" "$port" "$root/examples/webapp/public" --plain 1 > "$work/a" 2>&1 &
"$root/wasm/build/arche-wasm" serve "$work/handler.wasm" "$((port + 1))" --plain 1 > "$work/b" 2>&1 &
for _ in $(seq 100); do
    if grep -q ready "$work/a" 2>/dev/null && grep -q ready "$work/b" 2>/dev/null; then
        break
    fi
    sleep 0.05
done

# scenario name, port, path
results=$work/results
measure() {
    # A short warm-up, then the run.
    "$here/build/load" "$2" "$3" 4 200 > /dev/null
    set -- "$1" $("$here/build/load" "$2" "$3" 16 2000)
    echo "$machine $1 $5 $9" >> "$results"
    printf '%-22s %8s requests/s  p50 %6s us  p99 %6s us\n' "$1" "$5" "$7" "$9"
}
measure webapp-json "$port" /api/health
measure webapp-file "$port" /index.html
measure wasm-handler "$((port + 1))" /hello

if [ "${1:-}" = "--record" ]; then
    { grep -v "^$machine " "$here/baseline.txt" 2>/dev/null || true; cat "$results"; } | sort > "$work/new"
    cp "$work/new" "$here/baseline.txt"
    echo "recorded as the baseline of '$machine'"
    exit 0
fi
failed=0
while read -r m name per p99; do
    base=$(grep "^$m $name " "$here/baseline.txt" 2>/dev/null || true)
    if [ -z "$base" ]; then
        echo "no baseline for $m $name"
        continue
    fi
    set -- $base
    if [ "$per" -lt $(( $3 / 3 )) ]; then
        echo "REGRESSION $name: $per requests/s, the baseline $3"
        failed=1
    fi
    if [ "$p99" -gt $(( $4 * 4 )) ]; then
        echo "REGRESSION $name: p99 $p99 us, the baseline $4"
        failed=1
    fi
done < "$results"
[ "$failed" -eq 0 ] && echo "web benchmark: within the baseline of '$machine'"
exit "$failed"
