#!/bin/sh
# Fuzzes lib/font for the given seconds (font_fuzz.arche) from DejaVu Sans;
# a seed repeats a run.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work=${WORK:-$here/../build/fuzz}
mkdir -p "$work"
"$here/../compiler/arche-build" "$here/font_fuzz.arche" "$work/font_fuzz"
"$work/font_fuzz" "$here/../interop/fonts/DejaVuSans.ttf" "${1:-60}" ${2:-}
