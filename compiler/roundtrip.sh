#!/bin/sh
# Checks that `arche cst` gives back every .arche file below the given
# directories byte for byte: the syntax tree is lossless.
# Usage: roundtrip.sh <arche> <dir>...
set -u
arche=$1
shift
out=$(mktemp)
trap 'rm -f "$out"' EXIT
count=0
failed=0
for f in $(find "$@" -name '*.arche' | sort); do
    count=$((count + 1))
    "$arche" cst "$f" > "$out" 2>/dev/null
    if ! cmp -s "$f" "$out"; then
        echo "roundtrip: $f differs"
        failed=$((failed + 1))
    fi
done
echo "roundtrip: $count files, $failed differ"
[ "$failed" -eq 0 ]
