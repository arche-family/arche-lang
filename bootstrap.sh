#!/bin/sh
# The bootstrap (docs/plan.md): stage0, written in C11, builds stage1, the
# Arche compiler; stage1 builds stage2 and stage2 builds stage3, both from the
# same source. stage2 and stage3 must be bit-identical.
# Needs a C compiler (CC, default cc) and clang for LLVM IR.
# Usage: ./bootstrap.sh [work dir]
set -eu
root=$(cd "$(dirname "$0")" && pwd)
work=${1:-$root/build/bootstrap}
rm -rf "$work"
mkdir -p "$work/stage2" "$work/stage3"
clang=${CLANG:-clang}
link="$clang -O1 -nostdlib -static -fno-pie -Wno-override-module"

echo "bootstrap: stage0 and stage1"
make -s -C "$root/stage0"
make -s -C "$root/compiler"
stage1=$root/compiler/build/arche
rt=$root/compiler/build/rt.o
$clang -c -O2 -ffreestanding -fno-builtin -nostdlib -fno-stack-protector -fno-pie -o "$rt" "$root/compiler/runtime/rt.c"

# Each stage builds in its own directory under one file name, so that no
# name or path differs between the binaries.
echo "bootstrap: stage2, built by stage1"
(cd "$root/compiler" && "$stage1" build main.arche "$work/stage2/arche.ll")
(cd "$work/stage2" && $link -o arche arche.ll "$rt")
echo "bootstrap: stage3, built by stage2"
(cd "$root/compiler" && "$work/stage2/arche" build main.arche "$work/stage3/arche.ll")
(cd "$work/stage3" && $link -o arche arche.ll "$rt")

if cmp -s "$work/stage2/arche" "$work/stage3/arche"; then
    echo "bootstrap: stage2 and stage3 are bit-identical"
    sha256sum "$work/stage3/arche"
else
    echo "bootstrap: stage2 and stage3 differ"
    exit 1
fi
