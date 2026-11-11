#!/bin/sh
# Builds a Nimadir mini-app written in Arche (docs/sprints.md S38): the
# package in <app-dir> is copied aside, the bindings of Nimadir's world are
# generated next to it (arche-wasm bindgen), it is built for
# wasm32-component, and its program is made a component of the world.
# Nothing is written into <app-dir>. template/ is an app to start from.
# Usage: nimadir-app.sh <app-dir> <out.component.wasm>
# ARCHE names the compiler, ARCHE_WASM arche-wasm, WIT the world's WIT
# (Nimadir's, by default) and WORLD the world (mini-app).
set -eu
if [ $# -ne 2 ]; then
    echo "usage: nimadir-app.sh <app-dir> <out.component.wasm>" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
arche=${ARCHE:-$here/../compiler/build/arche}
wasm=${ARCHE_WASM:-$here/build/arche-wasm}
wit=${WIT:-$here/tests/nimadir/world.wit}
world=${WORLD:-mini-app}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp -R "$1"/. "$work/"
"$wasm" bindgen "$wit" "$world" "$work"
"$arche" --target wasm32-component build "$work/main.arche" "$work/app.core.wasm"
"$wasm" component "$work/app.core.wasm" "$wit" "$world" "$2"
