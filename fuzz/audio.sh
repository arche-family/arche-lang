#!/bin/sh
# Fuzzes lib/audio for the given seconds (audio_fuzz.arche) from the FLAC,
# Ogg Vorbis and MP3 files of conformance/tests/audio; a seed repeats a
# run.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work=${WORK:-$here/../build/fuzz}
mkdir -p "$work"
"$here/../compiler/arche-build" "$here/audio_fuzz.arche" "$work/audio_fuzz"
cd "$here/../conformance/tests/audio/_media"
"$work/audio_fuzz" "${1:-60}" ${2:-} $(ls *.flac *.ogg *.mp3 | grep -v '\.ref\.flac$')
