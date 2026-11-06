#!/bin/sh
# Builds the freestanding examples with the Arche compiler and runs them in
# QEMU (docs/sprints.md S16): each writes over its serial port, then panics
# on purpose, and its panic handler reports it and powers the machine off.
# The serial output and QEMU's exit status must be the expected ones.
# Needs clang, ld.lld and qemu-system-x86_64 / -riscv64 / -aarch64.
# Usage: freestanding/run.sh [x86_64] [riscv64] [aarch64]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
arche=${ARCHE:-$root/compiler/build/arche}
clang=${CLANG:-clang}
lld=${LLD:-ld.lld}
work=${WORK:-$root/build/freestanding}
targets=${*:-x86_64 riscv64 aarch64}
failed=0

for arch in $targets; do
    first=''
    out="$work/$arch"
    mkdir -p "$out"
    case $arch in
        x86_64)
            triple=x86_64-unknown-none-elf
            flags="-mno-red-zone -fno-pic"
            qemu="qemu-system-x86_64 -kernel $out/kernel.elf -device isa-debug-exit,iobase=0xf4,iosize=0x04"
            arch_name=x86-64
            status=3 ;;   # isa-debug-exit: 1 * 2 + 1
        riscv64)
            triple=riscv64-unknown-elf
            flags="-march=rv64gc -mabi=lp64d -mcmodel=medany -mno-relax"
            qemu="qemu-system-riscv64 -machine virt -bios none -kernel $out/kernel.elf"
            arch_name="RISC-V 64"
            status=1 ;;   # the SiFive test device: 0x3333 | 1 << 16
        aarch64)
            triple=aarch64-unknown-none-elf
            # With the MMU off all memory is device memory, where an unaligned access faults.
            flags="-mstrict-align -mno-outline-atomics -fno-pic"
            qemu="qemu-system-aarch64 -machine virt -cpu cortex-a57 -semihosting -nic none -kernel $out/kernel.elf"
            arch_name=ARM64
            first='exception: class 60 came and went\n'   # brk, in its table
            status=1 ;;   # semihosting's exit, with the status it was told
        *)
            echo "freestanding: unknown target $arch" >&2
            exit 2 ;;
    esac
    "$arche" --target "freestanding-$arch" --panic-handler "$arch.halt" build "$here/$arch.arche" "$out/kernel.ll"
    # shellcheck disable=SC2086
    "$clang" --target=$triple $flags -O1 -ffreestanding -Wno-override-module -c -o "$out/kernel.o" "$out/kernel.ll"
    # shellcheck disable=SC2086
    "$clang" --target=$triple $flags -O2 -ffreestanding -fno-builtin -nostdlib -fno-stack-protector \
        -Wall -Wextra -Werror -c -o "$out/rt.o" "$root/compiler/runtime/freestanding.c"
    "$lld" -T "$here/$arch.ld" -o "$out/kernel.elf" "$out/kernel.o" "$out/rt.o"
    set +e
    # shellcheck disable=SC2086
    timeout 60 $qemu ${QEMU_FLAGS:-} -display none -serial stdio -monitor none -no-reboot > "$out/serial.txt" 2> "$out/qemu.txt"
    got=$?
    set -e
    printf "$first" > "$out/expected.txt"
    printf 'Arche, freestanding on %s\ncore and alloc: the squares of 1..10 add up to 385\npanic: index out of range\n  at kernel.arche:55:23\n' \
        "$arch_name" >> "$out/expected.txt"
    tr -d '\r' < "$out/serial.txt" > "$out/serial.clean"
    if cmp -s "$out/expected.txt" "$out/serial.clean" && [ "$got" -eq "$status" ]; then
        echo "freestanding: $arch runs and writes over its serial port"
    else
        echo "freestanding: $arch failed (QEMU status $got, expected $status); serial output:"
        cat "$out/serial.clean"
        cat "$out/qemu.txt"
        failed=1
    fi
done
# The rules of a freestanding program, each with its code.
expect_error() {
    if "$arche" --target freestanding-x86_64 $2 check "$here/errors/$1" 2> "$work/error.txt"; then
        echo "freestanding: errors/$1 was accepted"
        failed=1
    elif ! grep -q "$3" "$work/error.txt"; then
        echo "freestanding: errors/$1 does not report $3:"
        cat "$work/error.txt"
        failed=1
    fi
}
mkdir -p "$work"
expect_error no_std.arche "--panic-handler no_std.halt" E0308
expect_error bad_handler.arche "--panic-handler bad_handler.halt" E0928
expect_error bad_handler.arche "" E0928
[ $failed -eq 0 ] && echo "freestanding: the errors are reported"
exit $failed
