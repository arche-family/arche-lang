#!/bin/sh
# The phone's Linux kernel for QEMU x86-64 (docs/sprints.md S35, arche-os
# T1): mainline Linux, the version and hash below, from kernel.org,
# configured as x86_64_defconfig and kvm_guest.config with phone.config on
# top, everything built in. Built once into LINUX_DIR (default
# build/linux), and again only when the version or the config changes.
# Prints the bzImage's path.
#
# Needs make, gcc, flex, bison, bc, perl and libelf's headers; KBUILD_TOOLS
# names a directory where they were unpacked without installing them
# (usr/bin, usr/include, usr/lib/x86_64-linux-gnu), as on a machine
# without root.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
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
src=$dir/linux-$version
stamp=$(cat "$here/phone.config" | sha256sum | cut -c1-16)
image=$dir/bzImage-$version-$stamp
if [ -f "$image" ]; then
    echo "$image"
    exit 0
fi
if [ ! -d "$src" ]; then
    tar -xJf "$tarball" -C "$dir"
fi
if [ -n "${KBUILD_TOOLS:-}" ]; then
    PATH=$KBUILD_TOOLS/usr/bin:$PATH
    BISON_PKGDATADIR=$KBUILD_TOOLS/usr/share/bison
    M4=$KBUILD_TOOLS/usr/bin/m4
    export PATH BISON_PKGDATADIR M4
    set -- HOSTCFLAGS="-I$KBUILD_TOOLS/usr/include" HOSTLDFLAGS="-L$KBUILD_TOOLS/usr/lib/x86_64-linux-gnu"
fi
cd "$src"
make -s x86_64_defconfig kvm_guest.config "$@" > "$dir/config.log" 2>&1
scripts/kconfig/merge_config.sh -m .config "$here/phone.config" >> "$dir/config.log" 2>&1
make -s olddefconfig "$@" >> "$dir/config.log" 2>&1
if ! make -j"$(nproc)" bzImage "$@" > "$dir/build.log" 2>&1; then
    tail -20 "$dir/build.log" >&2
    exit 1
fi
cp arch/x86/boot/bzImage "$image"
echo "$image"
