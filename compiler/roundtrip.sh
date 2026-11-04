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
    # A file that is not UTF-8 has no syntax tree.
    if ! iconv -f UTF-8 -t UTF-8 "$f" > /dev/null 2>&1; then
        continue
    fi
    count=$((count + 1))
    "$arche" cst "$f" > "$out" 2>/dev/null
    if ! cmp -s "$f" "$out"; then
        echo "roundtrip: $f differs"
        failed=$((failed + 1))
    fi
done
echo "roundtrip: $count files, $failed differ"
[ "$failed" -eq 0 ]
