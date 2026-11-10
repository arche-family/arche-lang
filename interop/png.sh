#!/bin/sh
# lib/image/png against PngSuite (docs/sprints.md S35; png/PngSuite, by
# Willem van Schaik): every image decodes to the pixels in
# png/expected.txt, and each of the 14 broken ones (x*.png) is refused.
# The expected pixels were checked against Pillow; where they differ,
# tbbn0g04's transparent gray, Pillow compares the 4-bit tRNS value with
# the gray scaled to 8 bits, and expected.txt has what the PNG spec says.
# ARCHE_TARGET builds for another processor, run through binfmt.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
build=${WORK:-$here/../build/interop}
mkdir -p "$build"
"$here/../compiler/arche-build" "$here/png_check.arche" "$build/png_check"
cd "$here/png/PngSuite"
"$build/png_check" *.png > "$build/png.txt"
if ! diff -u "$here/png/expected.txt" "$build/png.txt"; then
    echo "png: PngSuite did not decode as expected"
    exit 1
fi
refused=$(grep -c ' refused' "$build/png.txt")
broken=$(ls x*.png | wc -l)
if [ "$refused" -ne "$broken" ] || grep ' refused' "$build/png.txt" | grep -v '^x' > /dev/null; then
    echo "png: refused $refused images, but $broken are broken"
    exit 1
fi
echo "png: PngSuite's $(ls *.png | wc -l) images: the good ones as expected, the $broken broken ones refused"
