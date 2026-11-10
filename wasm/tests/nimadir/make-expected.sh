#!/bin/sh
# Writes expected/<name>.txt: what Nimadir's own shell says to each case of
# cases.txt, normalized. Needs a Nimadir checkout whose shell is built
# (cargo build -p host), named by NIMADIR.
# Usage: NIMADIR=~/nimadir make-expected.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
: "${NIMADIR:?name a Nimadir checkout}"
grep -v '^#' "$here/cases.txt" | while read -r name args; do
    [ -n "$name" ] || continue
    home=$(mktemp -d)
    # shellcheck disable=SC2086
    (cd "$NIMADIR" && NIMADIR_HOME=$home NIMADIR_HOSTED_REGISTRY="" ./target/debug/nimadir-shell $args) \
        | sh "$here/normalize.sh" > "$here/expected/$name.txt"
    rm -rf "$home"
done
