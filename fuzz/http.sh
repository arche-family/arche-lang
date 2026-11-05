#!/bin/sh
# Fuzzes lib/http for the given seconds (http_fuzz.arche); a seed repeats a run.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work=${WORK:-$here/../build/fuzz}
mkdir -p "$work"
"$here/../compiler/arche-build" "$here/http_fuzz.arche" "$work/http_fuzz"
"$work/http_fuzz" "${1:-60}" ${2:-}
