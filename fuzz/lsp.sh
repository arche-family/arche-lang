#!/bin/sh
# Fuzzes arche-lsp's message parser for the given seconds
# (driver/lsp_fuzz.arche); a seed repeats a run.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work=${WORK:-$here/../build/fuzz}
mkdir -p "$work"
"$here/../compiler/arche-build" "$here/../driver/lsp_fuzz.arche" "$work/lsp_fuzz"
"$work/lsp_fuzz" "${1:-60}" ${2:-}
