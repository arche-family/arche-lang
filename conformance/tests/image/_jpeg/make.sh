#!/bin/sh
# Makes the JPEG files conformance/tests/image/jpeg.arche decodes, here in
# _jpeg/, with Pillow (libjpeg-turbo), ffmpeg's MJPEG encoder and make.py's
# own small encoder, from synthesized pictures; then prints the stdout lines
# the test expects, the pixels' hashes as Pillow decodes them. The files are
# kept; the test needs neither Python nor ffmpeg.
# Run it from anywhere: sh conformance/tests/image/_jpeg/make.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
python3 -I "$here/make.py" "$here"
