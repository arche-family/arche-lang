#!/bin/sh
# Builds and runs the constant-time check of lib/crypto (main.arche).
# x86-64 only: it reads the time stamp counter.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work=${WORK:-$here/../build/ctcheck}
mkdir -p "$work"
"$here/../compiler/arche-build" "$here/main.arche" "$work/ctcheck"
"$work/ctcheck"
