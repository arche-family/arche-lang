#!/bin/sh
# A release (docs/sprints.md S34): one versioned archive with the compiler
# (built by itself, release mode), arche-driver and arche-lsp (built by it),
# the library, and the runtime: its C, and its object for the compiler's own
# linker, so that a program builds on a machine with no C compiler and no
# LLVM. An editor finds the tools in PATH or by ARCHE_HOME.
#
#   build/dist/arche-<VERSION>-<target>.tar.gz
#
# The archive is the same bytes each time from the same tree: files sorted,
# owners and times zero, gzip without a name or a time. Needs clang to make
# the release (CLANG, as bootstrap.sh). Usage: dist.sh [out dir]
set -eu
root=$(cd "$(dirname "$0")" && pwd)
. "$root/compiler/target.sh"
version=$(cat "$root/VERSION")
name=arche-$version-$ARCHE_TARGET
out=${1:-$root/build/dist}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
tree=$work/$name
mkdir -p "$tree/bin" "$tree/runtime" "$out"

make -s -C "$root/stage0"
make -s -C "$root/compiler"
rt_o=$work/rt.o
$CLANG -c -O2 -ffreestanding -fno-builtin -nostdlib -fno-stack-protector $RT_FLAGS -o "$rt_o" "$root/compiler/runtime/rt.c"
# The compiler, built by itself in release mode, as bootstrap.sh's release
# stage2 is.
(cd "$root/compiler" && ./build/arche $TARGET_FLAG --lib ../lib/ --release build main.arche "$work/arche.ll")
(cd "$work" && $CLANG -O1 $LINK_FLAGS -Wno-override-module -o "$tree/bin/arche" arche.ll "$rt_o")
# The library, beside bin/ where the compiler looks for it.
(cd "$root" && find lib -name '*.arche' | sort | while read -r f; do
    mkdir -p "$tree/$(dirname "$f")"
    cp "$f" "$tree/$f"
done)
# The driver and the language server, by that compiler.
ARCHE=$tree/bin/arche ARCHE_FLAGS=--release "$root/compiler/arche-build" "$root/driver/main.arche" "$tree/bin/arche-driver"
ARCHE=$tree/bin/arche ARCHE_FLAGS=--release "$root/compiler/arche-build" "$root/driver/lsp.arche" "$tree/bin/arche-lsp"
# The runtime.
cp "$root/compiler/runtime/rt.c" "$root/compiler/runtime/"*.inc "$tree/runtime/"
"$tree/bin/arche" import "$rt_o" "$tree/runtime/rt-$ARCHE_TARGET.ao"
cp "$root/README.md" "$root/VERSION" "$tree/"
[ -f "$root/LICENSE" ] && cp "$root/LICENSE" "$tree/"
cat > "$tree/INSTALL" <<END
Arche $version for $ARCHE_TARGET.

Put bin/ in PATH, or set ARCHE_HOME to this directory: arche-driver and
arche-lsp find the compiler, the library and the runtime from there.

  arche-driver build [dir]   builds a package's programs (build.arche)
  arche check <file>         diagnostics; arche build <file> <out.ll|out.ao>

With clang in PATH, programs are built through LLVM; without it, by the
compiler's own backend and linker, on runtime/rt-$ARCHE_TARGET.ao.
END
# The same bytes from the same tree.
(cd "$work" && tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -cf "$work/$name.tar" "$name")
gzip -n -9 -c "$work/$name.tar" > "$out/$name.tar.gz"
echo "$out/$name.tar.gz"
sha256sum "$out/$name.tar.gz"
