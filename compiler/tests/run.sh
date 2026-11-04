#!/bin/sh
# Golden tests of `arche check`: text and JSON diagnostics, fixes included.
# Usage: run.sh <arche>
set -u
arche=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
cd "$(dirname "$0")"
status=0
out=$(mktemp)
trap 'rm -f "$out"' EXIT
for f in *.arche; do
    name=${f%.arche}
    "$arche" check "$f" 2> "$out" > /dev/null
    diff -u "$name.txt.expected" "$out" || status=1
    "$arche" check --json "$f" > "$out" 2> /dev/null
    diff -u "$name.json.expected" "$out" || status=1
done
[ "$status" -eq 0 ] && echo "diagnostics: ok"
exit "$status"
