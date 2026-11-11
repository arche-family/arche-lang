#!/bin/sh
# Writes each app's expected.txt (app-check.sh): what Nimadir's own shell
# says to the options of the app's `headless` file, in Tashkent's time zone
# and English, normalized as app-check.sh normalizes what arche-wasm says.
# Needs a Nimadir checkout whose shell is built (cargo build -p host),
# named by NIMADIR.
# Usage: NIMADIR=~/nimadir app-expected.sh <app-dir>...
set -eu
if [ $# -eq 0 ]; then
    echo "usage: NIMADIR=<checkout> app-expected.sh <app-dir>..." >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
: "${NIMADIR:?name a Nimadir checkout}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for dir in "$@"; do
    dir=$(cd "$dir" && pwd)
    name=$(basename "$dir")
    [ -f "$dir/headless" ] || { echo "app-expected: $name has no headless file" >&2; exit 1; }
    sh "$here/nimadir-app.sh" "$dir" "$work/$name.wasm"
    home=$(mktemp -d)
    # shellcheck disable=SC2046
    (cd "$NIMADIR" && TZ=Asia/Tashkent LANG=en_US.UTF-8 NIMADIR_HOME=$home NIMADIR_HOSTED_REGISTRY="" \
        ./target/debug/nimadir-shell "$work/$name.wasm" $(cat "$dir/headless")) > "$work/out"
    rm -rf "$home"
    if [ -f "$dir/normalize.sed" ]; then
        sed -f "$dir/normalize.sed" "$work/out" | sh "$here/tests/nimadir/normalize.sh" > "$dir/expected.txt"
    else
        sh "$here/tests/nimadir/normalize.sh" < "$work/out" > "$dir/expected.txt"
    fi
    echo "app-expected: $name"
done
