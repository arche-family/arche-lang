#!/bin/sh
# Golden tests of `arche check`: text and JSON diagnostics, fixes included;
# of `arche fix`: each fix/<name>.arche becomes fix/<name>.fixed, which
# checks clean; and of `arche fmt`: fmt/<name>.arche becomes
# fmt/<name>.formatted, which stays as it is, and so do the compiler's and
# the library's sources; and of `arche query`: the queries in
# query/<name>.cases give query/<name>.expected; of `arche complete`, and
# of unsaved text (--overlay); and of `arche doc`:
# doc/<name>.arche gives doc/<name>.md; of `arche fix --edition`; and of
# a release build's proofs (release/).
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
    ../../interop/*.arche ../../fuzz/*.arche ../../ctcheck/*.arche ../../freestanding/*.arche ../../conformance/runner/*.arche ../../driver/*.arche; do
    [ -f "$f" ] || continue
    "$arche" fmt --check "$f" || status=1
done
for f in query/*.arche; do
    name=${f%.arche}
    while read -r first what at; do
        if [ -z "$at" ]; then
            at=$what
            what=$first
            first=
        fi
        echo "\$ arche query ${first:+$first }$what $f $at"
        # shellcheck disable=SC2086
        "$arche" query $first "$what" "$f" "$at" 2>&1
    done < "$name.cases" > "$out"
    diff -u "$name.expected" "$out" || status=1
done
for f in doc/*.arche; do
    name=${f%.arche}
    "$arche" doc "$f" "$(basename "$name")" > "$out" || status=1
    diff -u "$name.md" "$out" || status=1
done
# arche complete: each line of complete/sample.cases takes the place of the
# line // HERE of complete/sample.arche, with | where the cursor is.
mkdir -p "$work/complete"
cp complete/*.arche "$work/complete/"
here=$(grep -n '// HERE' complete/sample.arche | cut -d: -f1)
while IFS= read -r case_line; do
    column=$(printf '%s' "$case_line" | awk '{ print index($0, "|") }')
    text=$(printf '%s' "$case_line" | sed 's/|//')
    awk -v n="$here" -v t="$text" 'NR == n { print t; next } { print }' complete/sample.arche > "$work/complete/sample.arche"
    printf '$ %s\n' "$case_line"
    (cd "$work/complete" && "$arche" complete sample.arche "$here:$column") 2>&1
done < complete/sample.cases > "$out"
diff -u complete/sample.expected "$out" || status=1
# The unsaved text of a file and of a module it imports (--overlay), and a
# module checked on its own, from the package's root (--package, --no-main).
mkdir -p "$work/overlay/sub"
printf '// A manifest.\n' > "$work/overlay/package.arche"
printf 'import sub.part;\n\npub fn ten() -> u32 {\n    return part.five() + part.five();\n}\n' > "$work/overlay/lib.arche"
printf 'pub fn five() -> u32 {\n    return 5;\n}\n' > "$work/overlay/sub/part.arche"
printf 'pub fn five() -> u32 {\n    return "five";\n}\n' > "$work/overlay/part.unsaved"
"$arche" --package "$work/overlay" --no-main check "$work/overlay/lib.arche" || status=1
"$arche" --package "$work/overlay" --no-main check "$work/overlay/sub/part.arche" || status=1
if "$arche" --package "$work/overlay" --no-main --overlay "$work/overlay/sub/part.arche" "$work/overlay/part.unsaved" check "$work/overlay/lib.arche" 2> "$out"; then
    echo "overlay: the unsaved text of an imported module was not read"
    status=1
fi
grep -q 'sub/part.arche:2:12: error\[E0401\]' "$out" || { echo "overlay: not reported at the module:"; cat "$out"; status=1; }
if "$arche" --package "$work/overlay" check "$work/overlay/lib.arche" 2> "$out"; then
    echo "--no-main: without it, a module with no main must be refused"
    status=1
fi
# A manifest and a build file are checked as what they are.
printf 'import std.build;\n\nconst PACKAGE: build.Package = build.Package {\n    colour: "blue",\n};\n' > "$work/overlay/package.arche"
if "$arche" check --json "$work/overlay/package.arche" > "$out" 2> /dev/null; then
    echo "manifest: a bad one was accepted"
    status=1
fi
grep -q '"code": "E0802"' "$out" || { echo "manifest: not E0802:"; cat "$out"; status=1; }
# Names in a manifest name directories too: none leads out of them.
for field in 'name: "../evil", version: "1.0.0"' 'name: "good", version: "../../evil"' \
    'name: "good", version: "1.0.0", dependencies: [build.dep("../evil", "1.0.0")]' \
    'name: "good", version: "1.0.0", dependencies: [build.dep("ok", "1.0.0/../..")]' \
    'name: "good", version: "1.0.0", dependencies: [build.dep("", "1.0.0")]'; do
    printf 'import std.build;\n\nconst PACKAGE: build.Package = build.Package {\n    %s,\n};\n' "$field" > "$work/overlay/package.arche"
    if "$arche" check --json "$work/overlay/package.arche" > "$out" 2> /dev/null; then
        echo "manifest: accepted $field"
        status=1
    fi
    grep -q '"code": "E0802"' "$out" || { echo "manifest: not E0802 for $field:"; cat "$out"; status=1; }
done
printf 'import std.build;\n\nconst PACKAGE: build.Package = build.Package {\n    name: "good_1",\n    version: "1.0.0-rc.1+build.5",\n    dependencies: [build.dep("other", "0.2.0")],\n};\n' > "$work/overlay/package.arche"
"$arche" check "$work/overlay/package.arche" || { echo "manifest: a good one was refused"; status=1; }
# arche fix --edition: edition/before becomes edition/after, which builds.
mkdir -p "$work/edition"
cp edition/before/* "$work/edition/"
"$arche" fix --edition 2027 "$work/edition/main.arche" > /dev/null || status=1
diff -r edition/after "$work/edition" || status=1
# A release build proves release/contracts.arche's contracts and checks
# from its constants: its main keeps no trap, and calls nothing it inlined.
"$arche" --release build release/contracts.arche "$work/contracts.ll" || status=1
main=$(awk '/define internal i16 @a0_9contracts_4main/,/^}/' "$work/contracts.ll")
if [ -z "$main" ] || printf '%s\n' "$main" | grep -q "a0rt_trap\|@a0_9contracts_4half"; then
    echo "release: the contracts of release/contracts.arche are not proven away"
    status=1
fi
[ "$status" -eq 0 ] && echo "diagnostics, fixes, layout, queries, docs, editions and release: ok"
exit "$status"
