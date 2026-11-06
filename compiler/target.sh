# What to build for, and how to link it, for every script that builds an
# Arche program: the compiler's own scripts here and bootstrap.sh. The
# target is the system this runs on, unless ARCHE_TARGET names another
# (docs/sprints.md S19). Sourced, not run.
#
# ARCHE_BACKEND=own builds through the compiler's own backend (x64/, a64/,
# rv64/) and lld (LLD names it), with no LLVM between them, for Linux.
#
# It sets CLANG, ARCHE_TARGET, TARGET_FLAG (for the compiler), RT_FLAGS (to
# compile the runtime), LINK_FLAGS (to link a program with it) and GC_FLAGS
# (to drop what nothing reaches, for a build of one object per module).

: "${CLANG:=clang}"
: "${LLD:=ld.lld}"
: "${ARCHE_BACKEND:=llvm}"
if [ -z "${ARCHE_TARGET:-}" ]; then
    case $(uname -s) in
    MINGW* | MSYS* | CYGWIN*) ARCHE_TARGET=windows-x86_64 ;;
    *) ARCHE_TARGET=linux-x86_64 ;;
    esac
fi

case $ARCHE_TARGET in
linux-x86_64)
    TARGET_FLAG=
    # With frame pointers, so that a panic's stack trace walks through it.
    RT_FLAGS="-fno-pie -fno-omit-frame-pointer"
    LINK_FLAGS="-nostdlib -static -fno-pie"
    GC_FLAGS="-Wl,--gc-sections"
    ;;
linux-aarch64)
    TARGET_FLAG="--target linux-aarch64"
    RT_FLAGS="--target=aarch64-linux-gnu -fno-pie -fno-omit-frame-pointer -mno-outline-atomics"
    LINK_FLAGS="--target=aarch64-linux-gnu -mno-outline-atomics -fuse-ld=lld -nostdlib -static -fno-pie"
    GC_FLAGS="-Wl,--gc-sections"
    ;;
linux-riscv64)
    TARGET_FLAG="--target linux-riscv64"
    RT_FLAGS="--target=riscv64-linux-gnu -march=rv64gc -mabi=lp64d -mno-relax -fno-pie -fno-omit-frame-pointer"
    LINK_FLAGS="--target=riscv64-linux-gnu -march=rv64gc -mabi=lp64d -mno-relax -fuse-ld=lld -nostdlib -static -fno-pie"
    GC_FLAGS="-Wl,--gc-sections"
    ;;
windows-x86_64)
    TARGET_FLAG="--target windows-x86_64"
    RT_FLAGS="--target=x86_64-pc-windows-msvc"
    # No C library and no C runtime: the runtime owns the entry point, and
    # Win32 comes from the import libraries the system already has, which
    # clang finds itself. Brepro leaves out the time the link happened, so
    # that the same input gives the same bytes (bootstrap.sh). The linker's
    # options are written with a dash, which the shells of Windows leave
    # alone; a leading slash they would read as a path.
    LINK_FLAGS="--target=x86_64-pc-windows-msvc -fuse-ld=lld -nostdlib -Wl,-entry:a0rt_entry -Wl,-subsystem:console -Wl,-Brepro -lkernel32 -lws2_32 -ladvapi32"
    GC_FLAGS="-Wl,-opt:ref"
    ;;
*)
    echo "target.sh: unknown target: $ARCHE_TARGET" >&2
    exit 2
    ;;
esac

# Whether the runtime's object is older than anything it is made of. Sourced
# from a script that set here.
rt_stale() {
    [ -f "$1" ] || return 0
    for part in "$here"/runtime/*; do
        [ "$part" -nt "$1" ] && return 0
    done
    return 1
}

# ARCHE_QEMU runs a program of another processor where the system will not by
# itself: the program becomes a script that starts it through QEMU.
wrap_for_qemu() {
    if [ -n "${ARCHE_QEMU:-}" ]; then
        mv "$1" "$1.elf"
        printf '#!/bin/sh\nexec %s "%s.elf" "$@"\n' "$ARCHE_QEMU" "$1" > "$1"
        chmod +x "$1"
    fi
}
