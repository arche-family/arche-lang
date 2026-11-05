#!/bin/sh
# Self-test of the conformance runner: runs it over cases/ with the mock
# compiler and compares its report with the expected-*.txt files.
#
# Usage: run.sh <path to runner>

set -eu

runner=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
cd "$(dirname "$0")"
# The mock compiler is a shell script, and so is each program it writes;
# Windows starts neither on its own, so this self-test runs where they do
# (docs/sprints.md S19). On Windows the runner is checked by the
# conformance suite itself, which it builds with the real compiler.
case $(uname -s) in
MINGW* | MSYS* | CYGWIN*)
    echo "selftest: skipped: the mock compiler and its programs are shell scripts"
    exit 0
    ;;
esac
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

# A compiler's path may hold a space.
spaced=$(mktemp -d)
mkdir "$spaced/with space"
cp mockc "$spaced/with space/mockc"
set +e
"$runner" --compiler "$spaced/with space/mockc" --timeout 1 cases/stdout.arche cases/args.arche > "$actual" 2>&1
got=$?
set -e
if [ "$got" -ne 0 ]; then
    echo "selftest: a compiler at a path with a space: the runner exited with $got"
    status=1
fi
diff -u expected-pass.txt "$actual" || status=1
rm -rf "$spaced"

if [ "$status" -eq 0 ]; then
    echo "selftest: ok"
fi
exit "$status"
