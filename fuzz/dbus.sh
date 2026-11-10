#!/bin/sh
# Fuzzes lib/dbus for the given seconds (dbus_fuzz.arche); a seed repeats a run.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work=${WORK:-$here/../build/fuzz}
mkdir -p "$work"
"$here/../compiler/arche-build" "$here/dbus_fuzz.arche" "$work/dbus_fuzz"
"$work/dbus_fuzz" "${1:-60}" ${2:-}
