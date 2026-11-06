#!/bin/sh
# WebAssembly, where the conformance tests do not look (docs/sprints.md S30):
# the ABI suite's program, a build that repeats, the compiler on WebAssembly
# building itself bit for bit and a Linux program, and what the target refuses.
# Usage: wasm-check.sh. WASMTIME names the runtime; it needs no other tool.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
wasmtime=${WASMTIME:-wasmtime}
arche=$here/build/arche
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
fail() {
    echo "wasm-check: $1"
    status=1
}

"$arche" --target wasm32-wasi build "$root/conformance/abi/main.arche" "$work/abi.wasm" || exit 1
$wasmtime run "$work/abi.wasm" > "$work/abi.out" 2> "$work/abi.err" || fail "the ABI suite's program failed: $(head -5 "$work/abi.err")"
cmp -s "$work/abi.out" "$root/conformance/abi/expected.txt" || {
    fail "the ABI suite's output differs:"
    diff "$work/abi.out" "$root/conformance/abi/expected.txt" | head -12
}

"$arche" --target wasm32-wasi build "$root/conformance/abi/main.arche" "$work/again.wasm" || exit 1
cmp -s "$work/abi.wasm" "$work/again.wasm" || fail "two builds differ"

# Paths the compiler is given are under these, as it would be on any host: the
# repository at /, where it finds the library from its own name, and its
# source as the current directory.
stage2=$work/stage2
stage3=$work/stage3
mkdir "$stage2" "$stage3"
(cd "$here" && "$arche" --target wasm32-wasi build main.arche "$stage2/arche.wasm") || exit 1
guest() {
    (cd "$here" && $wasmtime run --dir=.::. "--dir=$root::/" "--dir=$work::/out" --argv0 /compiler/build/arche "$stage2/arche.wasm" "$@")
}
guest --target wasm32-wasi build main.arche /out/stage3/arche.wasm || fail "the compiler on WebAssembly did not build itself"
if cmp -s "$stage2/arche.wasm" "$stage3/arche.wasm"; then
    echo "wasm-check: on WebAssembly, stage2 and stage3 are bit-identical"
    sha256sum "$stage3/arche.wasm"
else
    fail "on WebAssembly, stage2 and stage3 differ"
fi

# A compiler on WebAssembly builds for the others, and its object links.
rt=$here/build/rt-linux-x86_64
if [ -f "$rt.ao" ] && [ "$(uname -m)" = x86_64 ]; then
    cp "$rt.ao" "$work/rt.ao"
    cat > "$work/hello.arche" <<'HELLO'
import std;

fn main(sys: &mut std.System) -> !void {
    sys.stdout.write("built on WebAssembly\n");
}
HELLO
    guest build /out/hello.arche /out/hello.ao || fail "the compiler on WebAssembly did not build a Linux object"
    guest link /out/hello /out/hello.ao /out/rt.ao || fail "the compiler on WebAssembly did not link"
    chmod +x "$work/hello"
    [ "$("$work/hello")" = "built on WebAssembly" ] || fail "the Linux program it built does not run"
fi

# What the target cannot have, said by name, not as a broken module.
refuses() { # what it must say, the file
    "$arche" --target wasm32-wasi build "$2" "$work/refused.wasm" > "$work/refused.out" 2>&1
    [ $? -eq 1 ] || fail "$2 was built"
    grep -qF "$1" "$work/refused.out" || fail "it did not say: $1; it said: $(cat "$work/refused.out")"
}
cat > "$work/package.arche" <<'MANIFEST'
// A manifest: full Arche.
MANIFEST
cat > "$work/with-asm.arche" <<'SOURCE'
import std;

fn main(sys: &mut std.System) -> !void {
    unsafe {
        // SAFETY: no instruction would do anything.
        asm("nop");
    }
}
SOURCE
refuses "asm: a WebAssembly module has none" "$work/with-asm.arche"
"$arche" --target wasm32-wasi --release build "$root/conformance/abi/main.arche" "$work/refused.wasm" > "$work/refused.out" 2>&1
[ $? -eq 1 ] || fail "a release build was made"
grep -qF "release builds are not for WebAssembly" "$work/refused.out" || fail "the release build did not say why: $(cat "$work/refused.out")"

[ "$status" -eq 0 ] && echo "wasm-check: the ABI suite's program, a build that repeats, the compiler building itself and a Linux program"
exit "$status"
