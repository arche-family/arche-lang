#!/bin/sh
# The bootstrap on another processor (docs/sprints.md S28): stage1, on this
# machine, builds the compiler for the target; QEMU's user mode runs that
# stage2, which builds stage3 from the same source. stage2 and stage3 must be
# bit-identical, through LLVM and by the compiler's own backend, in a debug
# build and in a release build (S41). Needs clang and qemu-user.
# Usage: ./cross-bootstrap.sh <aarch64 | riscv64> [work dir], anywhere.
set -eu
root=$(cd "$(dirname "$0")" && pwd)
arch=$1
ARCHE_TARGET=linux-$arch
here="$root/compiler"
. "$root/compiler/target.sh"
work=${2:-$root/build/cross/$arch}
rm -rf "$work"
qemu=${QEMU:-qemu-$arch}
clang=$CLANG

make -s -C "$root/stage0"
make -s -C "$root/compiler"
stage1=$root/compiler/build/arche
rt=$work/rt.o
mkdir -p "$work/llvm/stage2" "$work/llvm/stage3"
$clang -c -O2 -ffreestanding -fno-builtin -nostdlib -fno-stack-protector $RT_FLAGS -o "$rt" "$root/compiler/runtime/rt.c"
link="$clang -O1 $LINK_FLAGS -Wno-override-module"

echo "cross-bootstrap: $arch: stage2, built by stage1 through LLVM"
(cd "$root/compiler" && "$stage1" $TARGET_FLAG --lib ../lib/ build main.arche "$work/llvm/stage2/arche.ll")
(cd "$work/llvm/stage2" && $link -o arche arche.ll "$rt")
echo "cross-bootstrap: $arch: stage3, built by stage2 under QEMU"
(cd "$root/compiler" && $qemu "$work/llvm/stage2/arche" $TARGET_FLAG --lib ../lib/ build main.arche "$work/llvm/stage3/arche.ll")
(cd "$work/llvm/stage3" && $link -o arche arche.ll "$rt")
if cmp -s "$work/llvm/stage2/arche" "$work/llvm/stage3/arche"; then
    echo "cross-bootstrap: $arch: through LLVM, stage2 and stage3 are bit-identical"
    sha256sum "$work/llvm/stage3/arche"
else
    echo "cross-bootstrap: $arch: through LLVM, stage2 and stage3 differ"
    exit 1
fi

"$stage1" import "$rt" "$work/rt.ao"
# By the own backend: a directory, then the flags of the build.
own_chain() {
    mkdir -p "$work/$1/stage2" "$work/$1/stage3"
    echo "cross-bootstrap: $arch: stage2 ($1), built by stage1's own backend and linker"
    (cd "$root/compiler" && "$stage1" $TARGET_FLAG $2 --lib ../lib/ build main.arche "$work/$1/stage2/arche.ao")
    "$stage1" link "$work/$1/stage2/arche" "$work/$1/stage2/arche.ao" "$work/rt.ao"
    echo "cross-bootstrap: $arch: stage3 ($1), built by stage2 under QEMU"
    (cd "$root/compiler" && $qemu "$work/$1/stage2/arche" $TARGET_FLAG $2 --lib ../lib/ build main.arche "$work/$1/stage3/arche.ao")
    $qemu "$work/$1/stage2/arche" link "$work/$1/stage3/arche" "$work/$1/stage3/arche.ao" "$work/rt.ao"
    if cmp -s "$work/$1/stage2/arche" "$work/$1/stage3/arche"; then
        echo "cross-bootstrap: $arch: by its own backend ($1), stage2 and stage3 are bit-identical"
        sha256sum "$work/$1/stage3/arche"
    else
        echo "cross-bootstrap: $arch: by its own backend ($1), stage2 and stage3 differ"
        exit 1
    fi
}
own_chain own ""
own_chain own-release --release
