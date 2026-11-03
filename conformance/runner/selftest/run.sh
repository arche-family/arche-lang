#!/bin/sh
# Self-test of the conformance runner: runs it over cases/ with the mock
# compiler and compares its report with the expected-*.txt files.
#
# Usage: run.sh <path to runner>

set -eu

runner=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
cd "$(dirname "$0")"
actual=$(mktemp)
trap 'rm -f "$actual"' EXIT
status=0

# check <expected file> <expected exit status> <runner arguments>...
check() {
    expected=$1
    want=$2
    shift 2
    set +e
    "$runner" --compiler ./mockc --timeout 1 "$@" > "$actual" 2>&1
    got=$?
    set -e
    if [ "$got" -ne "$want" ]; then
        echo "selftest: runner $* exited with $got, expected $want"
        status=1
    fi
    if ! diff -u "$expected" "$actual"; then
        status=1
    fi
}

check expected-stage0.txt 1 --stage0 cases
check expected-full.txt 1 cases
check expected-pass.txt 0 cases/stdout.arche cases/args.arche

if [ "$status" -eq 0 ]; then
    echo "selftest: ok"
fi
exit "$status"
