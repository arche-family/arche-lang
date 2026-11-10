#!/bin/sh
# lib/font with a real font (docs/sprints.md S35): DejaVu Sans (fonts/),
# every glyph drawn without an error, and a sample of Latin and Cyrillic
# text whose image must hash as it did when it was looked at: the same
# bytes on every platform. ARCHE_TARGET builds for another processor, run
# through binfmt. The image is left as a PGM in the build directory.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
build=${WORK:-$here/../build/interop}
mkdir -p "$build"
"$here/../compiler/arche-build" "$here/font_draw.arche" "$build/font_draw"
got=$("$build/font_draw" "$here/fonts/DejaVuSans.ttf" "$build/font-sample.pgm")
want="glyphs 6253, failed 0, coverage 61353865
image 640x252, hash 4351598282857021976"
if [ "$got" != "$want" ]; then
    echo "font: got"
    echo "$got"
    echo "font: wanted"
    echo "$want"
    exit 1
fi
echo "font: every glyph of DejaVu Sans, and the sample, as before ($build/font-sample.pgm)"
