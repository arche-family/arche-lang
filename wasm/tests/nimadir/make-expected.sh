#!/bin/sh
# Writes expected/<name>.txt: what Nimadir's own shell says to each case of
# cases.txt, normalized. Needs a Nimadir checkout whose shell is built
# (cargo build -p host), named by NIMADIR. A case's arguments are read as
# the shell reads them, quotes and all.
# Usage: NIMADIR=~/nimadir make-expected.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
: "${NIMADIR:?name a Nimadir checkout}"
grep -v '^#' "$here/cases.txt" | while read -r name args; do
    [ -n "$name" ] || continue
    home=$(mktemp -d)
    eval "set -- $args"
    (cd "$NIMADIR" && NIMADIR_HOME=$home NIMADIR_HOSTED_REGISTRY="" ./target/debug/nimadir-shell "$@") \
        | sh "$here/normalize.sh" > "$here/expected/$name.txt"
    rm -rf "$home"
done
