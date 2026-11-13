#!/bin/sh
# Makes the files the audio tests decode (_media/), from signals ffmpeg
# synthesizes, with ffmpeg's encoders: flac, libmp3lame and libvorbis; and
# hand-made ones (make_flac.py, make_mp3.py) for what those encoders never
# write. ffmpeg's own decoding of each lossy file is its reference, kept
# as 16-bit FLAC (*.ref.flac), which the tests read with lib/audio/flac.
# Then it prints the line each FLAC test expects: the samples' hash as
# ffmpeg decodes them. The files are kept; a test needs neither ffmpeg nor
# Python. Run it from anywhere: sh conformance/tests/audio/make.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
m="$here/_media"
mkdir -p "$m"
ff="ffmpeg -hide_banner -loglevel error -y"
bx="-fflags +bitexact -flags:a +bitexact -map_metadata -1"

# Signals: a sweep over the whole band, a chord, white noise of a fixed
# seed, silence, and clicks, which make encoders switch to short blocks.
sweep="aevalsrc='0.5*sin(2*PI*(60*t+9970*t*t))'"
chord2="aevalsrc='0.25*sin(2*PI*261.6*t)+0.25*sin(2*PI*329.6*t)+0.25*sin(2*PI*392*t)|0.3*sin(2*PI*440*t)+0.3*sin(2*PI*554.4*t)+0.1*sin(2*PI*3520*t)'"
clicks="aevalsrc='0.8*exp(-200*mod(t,0.125))*sin(2*PI*1500*t)*lt(mod(t,0.125),0.02)+0.05*sin(2*PI*220*t)|0.7*exp(-150*mod(t+0.06,0.125))*sin(2*PI*900*t)+0.05*sin(2*PI*330*t)'"

# ---- FLAC ------------------------------------------------------------------
$ff -f lavfi -i "$sweep:s=44100:d=0.6" -c:a flac -sample_fmt s16 $bx "$m/sweep-mono-44k-16.flac"
$ff -f lavfi -i "$chord2:s=48000:d=0.4" -c:a flac -sample_fmt s32 -bits_per_raw_sample 24 \
    -compression_level 8 $bx "$m/chord-stereo-48k-24.flac"
# Silence, then noise, then a chord, at level 0 (fixed predictors only)
# with blocks of 1000 samples, whose size the header carries itself.
$ff -f lavfi -i "anullsrc=r=44100:cl=stereo:d=0.1" -f lavfi -i "anoisesrc=r=44100:a=0.3:seed=7:d=0.15" \
    -f lavfi -i "$chord2:s=44100:d=0.15" \
    -filter_complex "[1]aformat=channel_layouts=stereo[n];[0][n][2]concat=n=3:v=0:a=1" \
    -c:a flac -sample_fmt s16 -compression_level 0 -frame_size 1000 $bx "$m/mixed-stereo-44k-16.flac"
# Noise at 24 bits, whose residuals need Rice2's five-bit parameters.
$ff -f lavfi -i "anoisesrc=r=96000:a=0.9:seed=3:d=0.1" -c:a flac -sample_fmt s32 \
    -bits_per_raw_sample 24 $bx "$m/noise-mono-96k-24.flac"
python3 -I "$here/make_flac.py" "$m"

# ffmpeg's decoding of a lossy file ($2 its decoder), as 16-bit FLAC: its
# float output made 16 bits by swresample, without dither.
ref() {
    $ff -c:a "$2" -i "$m/$1" -c:a flac -sample_fmt s16 $bx "$m/$1.ref.flac"
}

# ---- MP3 -------------------------------------------------------------------
# libmp3lame at MPEG-1's three rates, MPEG-2's and MPEG-2.5's: constant and
# variable bit rates, joint stereo (M/S), plain stereo, mono, short blocks
# (clicks), big values with linbits (loud noise), a file without the Xing
# frame (so nothing is cut), one with ID3v2 and ID3v1 tags, and one by
# libshine, another encoder. make_mp3.py makes the rest (hand-*.mp3): what
# libmp3lame never writes.
mp3="-c:a libmp3lame"
$ff -f lavfi -i "$chord2:s=44100:d=0.5" $mp3 -b:a 128k $bx "$m/cbr128-joint-44k.mp3"
$ff -f lavfi -i "$clicks:s=48000:d=0.5" $mp3 -q:a 2 $bx "$m/vbr-clicks-48k.mp3"
$ff -f lavfi -i "$sweep:s=32000:d=0.5" $mp3 -b:a 64k $bx "$m/cbr64-mono-32k.mp3"
$ff -f lavfi -i "$chord2:s=44100:d=0.4" $mp3 -b:a 192k -joint_stereo 0 -write_xing 0 $bx "$m/lr-noxing-44k.mp3"
$ff -f lavfi -i "anoisesrc=r=44100:a=0.9:seed=5:d=0.3" -f lavfi -i "anoisesrc=r=44100:a=0.9:seed=6:d=0.3" \
    -filter_complex "[0][1]amerge=inputs=2" $mp3 -b:a 320k $bx "$m/noise320-stereo-44k.mp3"
$ff -f lavfi -i "$chord2:s=22050:d=0.6" $mp3 -b:a 48k $bx -metadata title="Tagged" \
    -metadata artist="Arche" -id3v2_version 3 -write_id3v1 1 "$m/id3-stereo-22k.mp3"
$ff -f lavfi -i "$clicks:s=24000:d=0.5" $mp3 -b:a 64k $bx "$m/clicks-stereo-24k.mp3"
$ff -f lavfi -i "$sweep:s=16000:d=0.5" $mp3 -q:a 4 $bx "$m/vbr-mono-16k.mp3"
$ff -f lavfi -i "$sweep:s=8000:d=0.6" $mp3 -b:a 16k $bx "$m/mpeg25-mono-8k.mp3"
$ff -f lavfi -i "$chord2:s=11025:d=0.6" $mp3 -q:a 6 $bx "$m/mpeg25-stereo-11k.mp3"
$ff -f lavfi -i "$chord2:s=44100:d=0.4" -c:a libshine -b:a 128k $bx "$m/shine-stereo-44k.mp3"
# A stereo pair that is nearly mono, so the encoder takes M/S.
near="aevalsrc='0.4*sin(2*PI*330*t)+0.2*sin(2*PI*(500*t+3000*t*t))|0.4*sin(2*PI*330*t)+0.18*sin(2*PI*(500*t+3000*t*t))'"
$ff -f lavfi -i "$near:s=44100:d=0.5" $mp3 -b:a 96k $bx "$m/ms-joint-44k.mp3"
python3 -I "$here/make_mp3.py" "$m"
for f in cbr128-joint-44k vbr-clicks-48k cbr64-mono-32k lr-noxing-44k noise320-stereo-44k \
    id3-stereo-22k clicks-stereo-24k vbr-mono-16k mpeg25-mono-8k mpeg25-stereo-11k shine-stereo-44k \
    ms-joint-44k hand-joint-48k hand-dual-32k hand-joint-24k hand-joint-11k hand-mono-44k; do
    ref "$f.mp3" mp3float
done

# ---- Ogg Vorbis ------------------------------------------------------------
# libvorbis at qualities -1 to 8: stereo, mono, short blocks (clicks),
# noise, and six channels, which the decoder puts in WAV's order; and one
# by ffmpeg's own encoder. ffmpeg's decoder departs from the
# specification on that one (its right channel differs from libvorbis's by
# up to 2000 units), so its reference is libvorbis's decoding, the
# specification's own decoder.
vorbis="-c:a libvorbis"
$ff -f lavfi -i "$chord2:s=44100:d=1" $vorbis -q:a 2 $bx "$m/q2-stereo-44k.ogg"
$ff -f lavfi -i "$sweep:s=48000:d=0.6" $vorbis -q:a 8 $bx "$m/q8-mono-48k.ogg"
$ff -f lavfi -i "$clicks:s=22050:d=0.8" $vorbis -q:a -1 $bx "$m/clicks-q-1-stereo-22k.ogg"
$ff -f lavfi -i "anoisesrc=r=44100:a=0.5:seed=1:d=0.4" -f lavfi -i "anoisesrc=r=44100:a=0.5:seed=2:d=0.4" \
    -filter_complex "[0][1]amerge=inputs=2" $vorbis -q:a 5 $bx "$m/noise-q5-stereo-44k.ogg"
$ff -f lavfi -i "$chord2:s=44100:d=0.5" -c:a vorbis -strict experimental $bx "$m/native-stereo-44k.ogg"
$ff -f lavfi -i "aevalsrc='0.3*sin(2*PI*(100*t+2000*t*t))|0.3*sin(2*PI*330*t)|0.2*sin(2*PI*440*t)|0.3*sin(2*PI*55*t)|0.2*sin(2*PI*(3000*t-1000*t*t))|0.2*sin(2*PI*660*t):c=5.1:s=44100:d=0.3'" \
    $vorbis -q:a 0 $bx "$m/sweep-q0-6ch-44k.ogg"
for f in q2-stereo-44k q8-mono-48k clicks-q-1-stereo-22k noise-q5-stereo-44k sweep-q0-6ch-44k; do
    ref "$f.ogg" vorbis
done
ref native-stereo-44k.ogg libvorbis

# ---- the FLAC tests' hashes ------------------------------------------------

# FNV-1a of each sample as 32 bits, as ffmpeg decodes it.
for f in sweep-mono-44k-16 chord-stereo-48k-24 mixed-stereo-44k-16 noise-mono-96k-24 \
    odd-8bit-stereo odd-12bit-variable odd-20bit-escaped odd-32bit-sides; do
    bits=$(ffprobe -v error -show_entries stream=bits_per_raw_sample -of csv=p=0 "$m/$f.flac")
    $ff -i "$m/$f.flac" -f s32le -c:a pcm_s32le - | python3 -I -c '
import sys, struct
bits = int(sys.argv[1])
data = sys.stdin.buffer.read()
h = 0xcbf29ce484222325
for (v,) in struct.iter_unpack("<i", data):
    h = ((h ^ ((v >> (32 - bits)) & 0xFFFFFFFF)) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
print(sys.argv[2], len(data) // 4, h)
' "$bits" "$f.flac"
done
