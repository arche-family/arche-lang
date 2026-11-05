#!/bin/sh
# The build driver's acceptance test (docs/sprints.md S24): a build that
# takes objects from the cache gives the same bytes as a clean one.
#
#   1. a clean build of example/;
#   2. one module changed, and built again: only it is compiled;
#   3. the change undone and built again, all from the cache: the program
#      is byte for byte the one of step 1;
#   4. a clean build in another directory: the same bytes again.
# Usage: check.sh <arche-driver>
set -eu
driver=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
fail() {
    echo "driver: $1"
    status=1
}

cp -r "$here/example" "$work/a"
"$driver" build "$work/a" > "$work/out1"
grep -q "0 from the cache" "$work/out1" || fail "a clean build took something from the cache"
cp "$work/a/build/greeter" "$work/clean"
[ "$("$work/clean")" = "hello, world: 55" ] || fail "the program does not print what it should"

sed -i 's/world/there/' "$work/a/words.arche"
"$driver" build "$work/a" > "$work/out2"
modules=$(sed -n 's/^greeter: \([0-9]*\) modules, \([0-9]*\) from the cache$/\1 \2/p' "$work/out2")
set -- $modules
[ "$2" -eq $(($1 - 1)) ] || fail "after one module changed, $2 of $1 came from the cache"
[ "$("$work/a/build/greeter")" = "hello, there: 55" ] || fail "the changed program does not print what it should"

sed -i 's/there/world/' "$work/a/words.arche"
"$driver" build "$work/a" > "$work/out3"
modules=$(sed -n 's/^greeter: \([0-9]*\) modules, \([0-9]*\) from the cache$/\1 \2/p' "$work/out3")
set -- $modules
[ "$1" -eq "$2" ] || fail "after the change was undone, $2 of $1 came from the cache"
cmp -s "$work/clean" "$work/a/build/greeter" || fail "the build from the cache differs from the clean build"

cp -r "$here/example" "$work/b"
"$driver" build "$work/b" > /dev/null
cmp -s "$work/clean" "$work/b/build/greeter" || fail "two clean builds differ"

out=$("$driver" run "$work/b" greeter -- ignored 2>&1 | tail -1)
[ "$out" = "hello, world: 55" ] || fail "arche-driver run printed '$out'"

[ "$status" -eq 0 ] && echo "driver: clean and cached builds are bit-identical; one change compiles one module"
exit "$status"
