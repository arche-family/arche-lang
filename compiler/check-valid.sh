#!/bin/sh
# The checker accepts every valid program: each `test: run` conformance test,
# and the compiler's own source.
# Usage: check-valid.sh <arche> <conformance tests dir>
set -u
arche=$1
tests=$2
count=0
failed=0
for f in $(grep -rl '^// test: run' "$tests" --include='*.arche' | grep -v '/_' | sort); do
    count=$((count + 1))
    if ! "$arche" check "$f" > /dev/null 2>&1; then
        echo "check-valid: $f is rejected:"
        "$arche" check "$f" 2>&1 | head -5
        failed=$((failed + 1))
    fi
done
here=$(cd "$(dirname "$0")" && pwd)
count=$((count + 1))
if ! (cd "$here" && "$arche" check main.arche > /dev/null 2>&1); then
    echo "check-valid: the compiler's own source is rejected:"
    (cd "$here" && "$arche" check main.arche 2>&1 | head -5)
    failed=$((failed + 1))
fi
echo "check-valid: $count programs, $failed rejected"
[ "$failed" -eq 0 ]
