#!/bin/sh
# Fuzzes lib/image/jpeg for the given seconds (jpeg_fuzz.arche) from the
# JPEG files of conformance/tests/image/_jpeg; a seed repeats a run.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work=${WORK:-$here/../build/fuzz}
mkdir -p "$work"
"$here/../compiler/arche-build" "$here/jpeg_fuzz.arche" "$work/jpeg_fuzz"
cd "$here/../conformance/tests/image/_jpeg"
"$work/jpeg_fuzz" "${1:-60}" ${2:-} *.jpg
