#!/bin/sh
# Differential test: every `test: run` conformance program, built by stage0
# and by the Arche compiler, gives the same stdout, stderr and exit status.
# Programs marked `stage0: skip` are full Arche, which stage0 does not build.
# Usage: differential.sh <conformance tests dir>
set -u
here=$(cd "$(dirname "$0")" && pwd)
tests=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
count=0
failed=0
for f in $(grep -rl '^// test: run' "$tests" --include='*.arche' | grep -v '/_' | sort); do
    if grep -q '^// stage0: skip' "$f"; then
        continue
    fi
    count=$((count + 1))
    dir=$(dirname "$f")
    args=$(sed -n 's#^// args: ##p' "$f" | tr '\n' ' ')
    if ! "$here/../stage0/arche0-build" "$f" "$tmp/a" > "$tmp/log" 2>&1 ||
        ! "$here/arche-build" "$f" "$tmp/b" >> "$tmp/log" 2>&1; then
        echo "differential: $f does not build:"
        head -5 "$tmp/log"
        failed=$((failed + 1))
        continue
    fi
    (cd "$dir" && "$tmp/a" $args > "$tmp/a.out" 2> "$tmp/a.err"; echo "exit $?" >> "$tmp/a.out")
    (cd "$dir" && "$tmp/b" $args > "$tmp/b.out" 2> "$tmp/b.err"; echo "exit $?" >> "$tmp/b.out")
    if ! cmp -s "$tmp/a.out" "$tmp/b.out" || ! cmp -s "$tmp/a.err" "$tmp/b.err"; then
        echo "differential: $f differs between stage0 and the Arche compiler"
        diff "$tmp/a.out" "$tmp/b.out" | head -5
        diff "$tmp/a.err" "$tmp/b.err" | head -5
        failed=$((failed + 1))
    fi
done
echo "differential: $count programs, $failed differ"
[ "$failed" -eq 0 ]
