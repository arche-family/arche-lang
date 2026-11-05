# What to build for, and how to link it, for every script that builds an
# Arche program: the compiler's own scripts here and bootstrap.sh. The
# target is the system this runs on, unless ARCHE_TARGET names another
# (docs/sprints.md S19). Sourced, not run.
#
# It sets CLANG, ARCHE_TARGET, TARGET_FLAG (for the compiler), RT_FLAGS (to
# compile the runtime), LINK_FLAGS (to link a program with it) and GC_FLAGS
# (to drop what nothing reaches, for a build of one object per module).

: "${CLANG:=clang}"
if [ -z "${ARCHE_TARGET:-}" ]; then
    case $(uname -s) in
    MINGW* | MSYS* | CYGWIN*) ARCHE_TARGET=windows-x86_64 ;;
    *) ARCHE_TARGET=linux-x86_64 ;;
    esac
fi

case $ARCHE_TARGET in
linux-x86_64)
    TARGET_FLAG=
    RT_FLAGS="-fno-pie"
    LINK_FLAGS="-nostdlib -static -fno-pie"
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
