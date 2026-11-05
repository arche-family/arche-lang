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

# The same with the library's alloc and std (lib/alloc.arche,
# lib/std.arche) instead of the compiler's built-in ones (docs/sprints.md
# S15, S17): stage2 and stage3 are then compilers whose containers and
# capabilities are Arche code. The stages live three levels below the
# repository, where they find lib/.
mkdir -p "$work/lib/stage2" "$work/lib/stage3"
echo "bootstrap: stage2 on lib/alloc and lib/std, built by stage1"
(cd "$root/compiler" && "$stage1" --lib-std build main.arche "$work/lib/stage2/arche.ll")
(cd "$work/lib/stage2" && $link -o arche arche.ll "$rt")
echo "bootstrap: stage3 on lib/alloc and lib/std, built by stage2"
(cd "$root/compiler" && "$work/lib/stage2/arche" --lib-std build main.arche "$work/lib/stage3/arche.ll")
(cd "$work/lib/stage3" && $link -o arche arche.ll "$rt")
if cmp -s "$work/lib/stage2/arche" "$work/lib/stage3/arche"; then
    echo "bootstrap: on lib/alloc and lib/std too, stage2 and stage3 are bit-identical"
    sha256sum "$work/lib/stage3/arche"
else
    echo "bootstrap: on lib/alloc and lib/std, stage2 and stage3 differ"
    exit 1
fi

# A release build (docs/sprints.md S26): the compiler's IR optimized by its
# own passes (compiler/ir/opt.arche). stage1 builds a release stage2, which
# builds a release stage3; they must be bit-identical too, and the release
# stage2 must build the debug stage2 byte for byte, which shows that the
# optimized compiler computes what the plain one does.
mkdir -p "$work/release/stage2" "$work/release/stage3" "$work/release/check"
echo "bootstrap: release stage2, built by stage1"
(cd "$root/compiler" && "$stage1" --release build main.arche "$work/release/stage2/arche.ll")
(cd "$work/release/stage2" && $link -o arche arche.ll "$rt")
echo "bootstrap: release stage3, built by the release stage2"
(cd "$root/compiler" && "$work/release/stage2/arche" --release build main.arche "$work/release/stage3/arche.ll")
(cd "$work/release/stage3" && $link -o arche arche.ll "$rt")
if cmp -s "$work/release/stage2/arche" "$work/release/stage3/arche"; then
    echo "bootstrap: in release mode too, stage2 and stage3 are bit-identical"
    sha256sum "$work/release/stage3/arche"
else
    echo "bootstrap: in release mode, stage2 and stage3 differ"
    exit 1
fi
(cd "$root/compiler" && "$work/release/stage2/arche" build main.arche "$work/release/check/arche.ll")
if cmp -s "$work/release/check/arche.ll" "$work/stage3/arche.ll"; then
    echo "bootstrap: the release compiler builds what the debug one does"
else
    echo "bootstrap: the release compiler builds something else than the debug one"
    exit 1
fi
