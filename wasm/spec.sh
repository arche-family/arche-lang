#!/bin/sh
# The WebAssembly spec's own tests (docs/sprints.md S31): test/core of
# WebAssembly/spec at the wg-2.0 tag, SIMD left out, made into JSON and
# binary modules by wabt's wast2json, and run by arche-wasm: in the
# interpreter, then with the JIT.
# Needs git and curl the first time; what they fetch stays in build/spec.
# Usage: spec.sh <arche-wasm> [script name]...
set -eu
here=$(cd "$(dirname "$0")" && pwd)
wasm=$1
shift
work=$here/build/spec
tests=$work/spec/test/core
if [ ! -d "$tests" ]; then
    mkdir -p "$work"
    git clone -q --depth 1 --branch wg-2.0 --filter=blob:none --sparse \
        https://github.com/WebAssembly/spec.git "$work/spec"
    git -C "$work/spec" sparse-checkout set test/core
fi
# wabt 1.0.36: 1.0.34, Ubuntu 24.04's, cannot read comments.wast and if.wast.
wast2json=$work/wabt/usr/bin/wast2json
if [ ! -x "$wast2json" ]; then
    mkdir -p "$work/wabt"
    deb=wabt_1.0.36+dfsg+~cs1.0.36-2ubuntu1_amd64.deb
    curl -fsSL --retry 5 -o "$work/$deb" "http://archive.ubuntu.com/ubuntu/pool/universe/w/wabt/$deb"
    dpkg-deb -x "$work/$deb" "$work/wabt"
fi
json=$work/json
if [ ! -d "$json" ]; then
    mkdir -p "$json.tmp"
    for f in "$tests"/*.wast; do
        name=$(basename "$f" .wast)
        mkdir -p "$json.tmp/$name"
        "$wast2json" "$f" -o "$json.tmp/$name/$name.json"
    done
    mv "$json.tmp" "$json"
fi
if [ $# -gt 0 ]; then
    set -- $(for name in "$@"; do echo "$json/$name/$name.json"; done)
else
    set -- "$json"/*/*.json
fi
"$wasm" spec "$@"
# With the JIT, which must have run: without a code region from the system,
# arche-wasm runs everything in the interpreter and says nothing of machine
# code.
status=0
out=$("$wasm" spec --jit "$@") || status=$?
printf '%s\n' "$out"
[ "$status" -eq 0 ] || exit "$status"
case $out in
*"machine code ran"*) ;;
*)
    echo "spec.sh: the JIT did not run" >&2
    exit 1
    ;;
esac
