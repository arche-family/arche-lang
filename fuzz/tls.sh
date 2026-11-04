#!/bin/sh
# Fuzzes lib/tls for the given seconds (tls_fuzz.arche); a seed repeats a run.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work=${WORK:-$here/../build/fuzz}
mkdir -p "$work"
"$here/../compiler/arche-build" "$here/tls_fuzz.arche" "$work/tls_fuzz"
"$work/tls_fuzz" "${1:-60}" ${2:-}
