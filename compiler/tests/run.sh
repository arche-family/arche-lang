#!/bin/sh
# Golden tests of `arche check`: text and JSON diagnostics, fixes included;
# of `arche fix`: each fix/<name>.arche becomes fix/<name>.fixed, which
# checks clean; and of `arche fmt`: fmt/<name>.arche becomes
# fmt/<name>.formatted, which stays as it is, and so do the compiler's and
# the library's sources; and of `arche query`: the queries in
# query/<name>.cases give query/<name>.expected; and of `arche doc`:
# doc/<name>.arche gives doc/<name>.md.
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
for f in fmt/*.arche; do
    name=${f%.arche}
    cp "$f" "$work/case.arche"
    "$arche" fmt "$work/case.arche" || status=1
    diff -u "$name.formatted" "$work/case.arche" || status=1
    "$arche" fmt --check "$work/case.arche" || status=1
done
for f in ../*.arche ../syntax/*.arche ../sema/*.arche ../lower/*.arche ../ir/*.arche ../../lib/*.arche ../../lib/*/*.arche ../../lib/*/*/*.arche \
    ../../interop/*.arche ../../fuzz/*.arche ../../ctcheck/*.arche ../../freestanding/*.arche ../../conformance/runner/*.arche; do
    [ -f "$f" ] || continue
    "$arche" fmt --check "$f" || status=1
done
for f in query/*.arche; do
    name=${f%.arche}
    while read -r what at; do
        echo "\$ arche query $what $f $at"
        "$arche" query "$what" "$f" "$at" 2>&1
    done < "$name.cases" > "$out"
    diff -u "$name.expected" "$out" || status=1
done
for f in doc/*.arche; do
    name=${f%.arche}
    "$arche" doc "$f" "$(basename "$name")" > "$out" || status=1
    diff -u "$name.md" "$out" || status=1
done
[ "$status" -eq 0 ] && echo "diagnostics, fixes, layout, queries and docs: ok"
exit "$status"
