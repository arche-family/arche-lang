#!/bin/sh
# Runs the examples in the library's doc comments as tests (arche doc
# --tests, docs/sprints.md S23): every module with an ```arche block.
# Usage: doctest.sh <arche>
set -u
arche=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
here=$(cd "$(dirname "$0")" && pwd)
lib=$here/../lib
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
echo "// A manifest: the examples are full Arche." > "$work/package.arche"
status=0
modules=0
total=0
for f in $(cd "$lib" && grep -rl '^ *///.*```arche' --include='*.arche' . | sort); do
    rel=${f#./}
    module=$(echo "${rel%.arche}" | tr / .)
    name=doc_$(echo "$module" | tr . _)
    n=$("$arche" doc --tests "$lib/$rel" "$module" "$work/$name.arche" | cut -d' ' -f1) || { status=1; continue; }
    if ARCHE="$arche" "$here/arche-build-tests" "$work/$name.arche" "$work/$name" > "$work/out" 2>&1 &&
        "$work/$name" --seed 1 > "$work/out" 2>&1; then
        modules=$((modules + 1))
        total=$((total + n))
    else
        echo "doctest: the examples of $module fail:"
        sed 's/^/    /' "$work/out"
        status=1
    fi
done
[ "$status" -eq 0 ] && echo "doctest: $total examples in $modules modules"
exit "$status"
