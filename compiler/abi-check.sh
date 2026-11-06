#!/bin/sh
# The ABI test suite (docs/abi.md, docs/sprints.md S29): each module of
# conformance/abi, with the library's, built separately by LLVM and by the
# compiler's own backend, and linked by the compiler's own linker in each mix.
# The program must print the same whichever backend built which of its modules.
# So must its Arche executable, loaded by tests/ax-load.c.
# Usage: abi-check.sh [x86_64 | aarch64 | riscv64]. QEMU names how to run a
# program of another processor. Needs clang, and ld.lld for the loader.
set -u
here=$(cd "$(dirname "$0")" && pwd)
arch=${1:-x86_64}
ARCHE_TARGET=linux-$arch
. "$here/target.sh"
qemu=
if [ "$arch" != x86_64 ]; then
    qemu=${QEMU:-qemu-$arch}
fi
arche=$here/build/arche
source=$here/../conformance/abi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
fail() {
    echo "abi-check: $arch: $1"
    status=1
}

$CLANG -c -O2 -ffreestanding -fno-builtin -nostdlib -fno-stack-protector $RT_FLAGS -o "$work/rt.o" "$here/runtime/rt.c" || exit 1
"$arche" import "$work/rt.o" "$work/rt.ao" || exit 1

mkdir "$work/own" "$work/llvm"
"$arche" $TARGET_FLAG units --ao "$source/main.arche" "$work/own" > "$work/own.list" || exit 1
"$arche" $TARGET_FLAG units "$source/main.arche" "$work/llvm" > "$work/llvm.list" || exit 1
while read -r unit; do
    $CLANG -c -O1 $RT_FLAGS -Wno-override-module -o "${unit%.ll}.o" "$unit" || exit 1
    "$arche" import "${unit%.ll}.o" "${unit%.ll}.ao" || exit 1
done < "$work/llvm.list"
units=$(sed -e 's|.*/||' -e 's|\..*||' "$work/own.list")
count=$(echo "$units" | wc -l)
[ "$count" -ge 3 ] || fail "only $count modules: the suite crosses none"

# mix <name> <backend of the odd units> <backend of the even ones>
mix() {
    objects=
    n=0
    for unit in $units; do
        if [ $((n % 2)) -eq 0 ]; then
            objects="$objects $work/$2/$unit.ao"
        else
            objects="$objects $work/$3/$unit.ao"
        fi
        n=$((n + 1))
    done
    if ! "$arche" link "$work/$1" $objects "$work/rt.ao" 2> "$work/$1.log"; then
        fail "$1: the link failed: $(cat "$work/$1.log")"
        return
    fi
    if ! $qemu "$work/$1" > "$work/$1.out" 2> "$work/$1.err"; then
        fail "$1: the program failed: $(head -3 "$work/$1.err")"
        return
    fi
    if ! cmp -s "$work/$1.out" "$source/expected.txt"; then
        fail "$1: the output differs:"
        diff "$work/$1.out" "$source/expected.txt" | head -12
    fi
}
mix all-own own own
mix all-llvm llvm llvm
mix own-then-llvm own llvm
mix llvm-then-own llvm own

# The whole program as one object, and as an Arche executable.
"$arche" $TARGET_FLAG build "$source/main.arche" "$work/whole.ao" || exit 1
"$arche" link "$work/whole" "$work/whole.ao" "$work/rt.ao" || exit 1
$qemu "$work/whole" > "$work/whole.out" 2>&1 || fail "the whole program failed"
cmp -s "$work/whole.out" "$source/expected.txt" || fail "the whole program's output differs"
"$arche" link --ax "$work/whole.ax" "$work/whole.ao" "$work/rt.ao" || exit 1
$CLANG -O1 $RT_FLAGS -ffreestanding -fno-builtin -nostdlib -static -fno-stack-protector -fuse-ld=lld -Wl,--image-base=0x10000000 \
    -o "$work/ax-load" "$here/tests/ax-load.c" || exit 1
$qemu "$work/ax-load" "$work/whole.ax" > "$work/ax.out" 2> "$work/ax.err" || fail "the Arche executable failed: $(head -3 "$work/ax.err")"
cmp -s "$work/ax.out" "$source/expected.txt" || fail "the Arche executable's output differs"

[ "$status" -eq 0 ] && echo "abi-check: $arch: $count modules, by each backend in each mix, and an Arche executable"
exit "$status"
