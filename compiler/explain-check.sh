#!/bin/sh
# Every error code has an example in `arche explain`, and checking that
# example reports the code (docs/sprints.md S23). Also: explain.arche is
# what gen-explain.py makes from docs/error-codes.md and the tests.
# Usage: explain-check.sh <arche>
set -u
arche=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
if python3 -I "$here/gen-explain.py" "$work/explain.arche" > /dev/null; then
    if ! cmp -s "$work/explain.arche" "$here/explain.arche"; then
        echo "explain: compiler/explain.arche is out of date; run python3 -I compiler/gen-explain.py"
        status=1
    fi
fi
count=0
for code in $(sed -n 's/^| `\(E[0-9]\{4\}\)` | .*|$/\1/p' "$here/../docs/error-codes.md"); do
    if grep -q "^| \`$code\` | .*Not reported yet" "$here/../docs/error-codes.md"; then
        continue
    fi
    case_dir="$work/$code"
    mkdir -p "$case_dir"
    file=$("$arche" explain --example-file "$code")
    [ -n "$file" ] || file=case.arche
    "$arche" explain --example "$code" > "$case_dir/$file"
    if [ ! -s "$case_dir/$file" ]; then
        echo "explain: $code has no example"
        status=1
        continue
    fi
    "$arche" explain "$code" | grep -q "in a package" && echo "// A manifest." > "$case_dir/package.arche"
    "$arche" explain --example-manifest "$code" > "$case_dir/manifest"
    [ -s "$case_dir/manifest" ] && cp "$case_dir/manifest" "$case_dir/package.arche"
    options=$("$arche" explain "$code" | sed -n 's/.*`arche \(.*\) check` reports.*/\1/p')
    # shellcheck disable=SC2086
    if "$arche" $options check "$case_dir/$file" > /dev/null 2> "$case_dir/out"; then
        echo "explain: the example of $code checks clean"
        status=1
    elif ! grep -q "error\[$code\]" "$case_dir/out"; then
        echo "explain: the example of $code does not report it:"
        sed 's/^/    /' "$case_dir/out"
        status=1
    fi
    count=$((count + 1))
done
[ "$status" -eq 0 ] && echo "explain: $count codes, each example reports its code"
exit "$status"
