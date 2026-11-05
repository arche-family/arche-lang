#!/bin/sh
# Golden tests of `arche check`: text and JSON diagnostics, fixes included;
# and of `arche fix`: each fix/<name>.arche becomes fix/<name>.fixed, which
# checks clean.
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
work=$(mktemp -d)
trap 'rm -f "$out"; rm -rf "$work"' EXIT
for f in fix/*.arche; do
    name=${f%.arche}
    cp "$f" "$work/case.arche"
    "$arche" fix "$work/case.arche" > /dev/null 2> "$out" || { cat "$out"; status=1; }
    diff -u "$name.fixed" "$work/case.arche" || status=1
    "$arche" check "$work/case.arche" || status=1
done
[ "$status" -eq 0 ] && echo "diagnostics and fixes: ok"
exit "$status"
