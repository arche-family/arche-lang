#!/bin/sh
# The phone's Linux kernel for QEMU x86-64 (docs/sprints.md S35, arche-os
# T1): mainline Linux, the version and hash below, from kernel.org,
# configured as x86_64_defconfig and kvm_guest.config with phone.config on
# top, everything built in, with the patches in patches/ (each says why
# at its top). Built once into LINUX_DIR (default build/linux), and again
# only when the version, the config or the patches change. Prints the
# bzImage's path.
#
# With aarch64 as its argument (S39), the kernel for QEMU's virt machine
# on ARM64: arm64's defconfig without its modules (they would all be
# built in) and virt.config, with phone.config and phone-aarch64.config on
# top, built by LLVM (clang, lld and LLVM's binutils), in a source tree of
# its own; prints the Image's path.
#
# Needs make, patch, gcc, flex, bison, bc, perl and libelf's headers; KBUILD_TOOLS
# names a directory where they were unpacked without installing them
# (usr/bin, usr/include, usr/lib/x86_64-linux-gnu), as on a machine
# without root.
# Usage: kernel.sh [x86_64|aarch64]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
arch=${1:-x86_64}
case $arch in
x86_64 | aarch64) ;;
*)
    echo "kernel: no $arch, only x86_64 and aarch64" >&2
    exit 2
    ;;
esac
version=6.12.116
sha256=cb3ed3dfdd47e6ade49dc933db334fd86e24cadee920da612c262b0e9b6a511b
dir=${LINUX_DIR:-$here/../../build/linux}
mkdir -p "$dir"
tarball=$dir/linux-$version.tar.xz
if [ ! -f "$tarball" ]; then
    curl -fsSL --retry 5 -o "$tarball.part" "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$version.tar.xz"
    mv "$tarball.part" "$tarball"
fi
if ! echo "$sha256  $tarball" | sha256sum -c --status; then
    echo "kernel: linux-$version.tar.xz is not the one kernel.org lists" >&2
    exit 1
fi
patches=$(ls "$here"/patches/*.patch)
patched=$(cat $patches | sha256sum | cut -c1-16)
if [ "$arch" = x86_64 ]; then
    tree=$dir
    stamp=$(cat "$here/phone.config" $patches | sha256sum | cut -c1-16)
    image=$dir/bzImage-$version-$stamp
else
    tree=$dir/aarch64
    stamp=$(cat "$here/phone.config" "$here/phone-aarch64.config" $patches | sha256sum | cut -c1-16)
    image=$dir/Image-aarch64-$version-$stamp
fi
src=$tree/linux-$version
if [ -f "$image" ]; then
    echo "$image"
    exit 0
fi
# The source as kernel.org gives it with the patches on it, unpacked
# again when they change.
if [ -d "$src" ] && [ "$(cat "$src/.arche-patches" 2>/dev/null)" != "$patched" ]; then
    rm -rf "$src"
fi
if [ ! -d "$src" ]; then
    mkdir -p "$tree"
    tar -xJf "$tarball" -C "$tree"
    for p in $patches; do
        patch -s -p1 -d "$src" < "$p"
    done
    echo "$patched" > "$src/.arche-patches"
fi
# What make is given past its targets: nothing yet (the processor's name,
# this script's argument, is no target of make's).
set --
if [ -n "${KBUILD_TOOLS:-}" ]; then
    PATH=$KBUILD_TOOLS/usr/bin:$PATH
    BISON_PKGDATADIR=$KBUILD_TOOLS/usr/share/bison
    M4=$KBUILD_TOOLS/usr/bin/m4
    export PATH BISON_PKGDATADIR M4
    set -- HOSTCFLAGS="-I$KBUILD_TOOLS/usr/include" HOSTLDFLAGS="-L$KBUILD_TOOLS/usr/lib/x86_64-linux-gnu"
fi
cd "$src"
: > "$tree/config.log"
# A step of the configuration, its output in config.log, whose end is
# shown when it fails.
configure() {
    if ! "$@" >> "$tree/config.log" 2>&1; then
        tail -20 "$tree/config.log" >&2
        exit 1
    fi
}
if [ "$arch" = x86_64 ]; then
    configure make -s x86_64_defconfig kvm_guest.config "$@"
    configure scripts/kconfig/merge_config.sh -m .config "$here/phone.config"
    configure make -s olddefconfig "$@"
    if ! make -j"$(nproc)" bzImage "$@" > "$tree/build.log" 2>&1; then
        tail -20 "$tree/build.log" >&2
        exit 1
    fi
    cp arch/x86/boot/bzImage "$image"
else
    # LLVM cross-compiles for any processor clang knows. LLVM is Linux's
    # own variable: -21 says the tools are clang-21, ld.lld-21 and so on.
    set -- "$@" ARCH=arm64 LLVM="${LLVM:-1}"
    grep -v '=m$' arch/arm64/configs/defconfig > arch/arm64/configs/arche_defconfig
    configure make -s arche_defconfig virt.config "$@"
    configure scripts/kconfig/merge_config.sh -m .config "$here/phone.config" "$here/phone-aarch64.config"
    configure make -s olddefconfig "$@"
    if ! make -j"$(nproc)" Image "$@" > "$tree/build.log" 2>&1; then
        tail -20 "$tree/build.log" >&2
        exit 1
    fi
    cp arch/arm64/boot/Image "$image"
fi
echo "$image"
