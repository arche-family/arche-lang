#!/bin/sh
# What the compiler makes of its own source, for docs/sprints.md S34's check
# that two machines build the same bytes: stage1 (built from stage0's C by
# this machine's C compiler) builds compiler/main.arche to LLVM IR in
# release mode and to an object by its own backend; their SHA-256s are
# written out. CI runs it on two systems and compares.
set -eu
root=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
make -s -C "$root/stage0"
make -s -C "$root/compiler"
cd "$root/compiler"
./build/arche --lib ../lib/ --release build main.arche "$work/arche.ll"
./build/arche --lib ../lib/ build main.arche "$work/arche.ao"
cd "$work"
sha256sum arche.ll arche.ao
