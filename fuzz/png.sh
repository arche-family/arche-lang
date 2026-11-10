#!/bin/sh
# Fuzzes lib/image/png for the given seconds (png_fuzz.arche) from
# PngSuite's good images; a seed repeats a run.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work=${WORK:-$here/../build/fuzz}
mkdir -p "$work"
"$here/../compiler/arche-build" "$here/png_fuzz.arche" "$work/png_fuzz"
cd "$here/../interop/png/PngSuite"
"$work/png_fuzz" "${1:-60}" ${2:-} $(ls *.png | grep -v '^x')
