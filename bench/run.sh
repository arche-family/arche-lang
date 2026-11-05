#!/bin/sh
# The baseline of docs/sprints.md S26: how fast the compiler compiles, and
# how fast its programs run, in a debug and a release build. Prints a
# Markdown table; bench/baseline.md keeps one.
# Usage: bench/run.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$here/..
arche=$root/compiler/build/arche
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
make -s -C "$root/stage0"
make -s -C "$root/compiler"
rt=$root/compiler/build/rt.o

# The best of three runs of a command, in milliseconds.
best_ms() {
    best=""
    for _ in 1 2 3; do
        start=$(date +%s%N)
        "$@" > /dev/null
        end=$(date +%s%N)
        t=$(( (end - start) / 1000000 ))
        if [ -z "$best" ] || [ "$t" -lt "$best" ]; then
            best=$t
        fi
    done
    echo "$best"
}

echo "| What | Debug | Release |"
echo "| --- | ---: | ---: |"
# The compiler compiling itself, to LLVM IR: the front end and, in release,
# the optimizer.
cd "$root/compiler"
d=$(best_ms "$arche" build main.arche "$work/c.ll")
r=$(best_ms "$arche" --release build main.arche "$work/cr.ll")
echo "| compiler: itself to LLVM IR (ms) | $d | $r |"
lines=$(cat ./*.arche syntax/*.arche sema/*.arche lower/*.arche ir/*.arche | wc -l)
echo "| compiler: lines of Arche compiled | $lines | $lines |"
d=$(wc -c < "$work/c.ll")
r=$(wc -c < "$work/cr.ll")
echo "| compiler: LLVM IR (KB) | $((d / 1024)) | $((r / 1024)) |"
cd "$here"
"$arche" build programs.arche "$work/p.ll"
"$arche" --release build programs.arche "$work/pr.ll"
# With clang -O0 the time is the Arche optimizer's alone; with -O1, LLVM's
# passes run after it.
for level in O0 O1; do
    clang -$level -nostdlib -static -fno-pie -Wno-override-module -o "$work/p" "$work/p.ll" "$rt"
    clang -$level -nostdlib -static -fno-pie -Wno-override-module -o "$work/pr" "$work/pr.ll" "$rt"
    # Each benchmark: the best of three runs.
    for name in fib sieve map sort; do
        bd=""
        br=""
        for _ in 1 2 3; do
            t=$("$work/p" | sed -n "s/^$name \([0-9]*\) ms.*/\1/p")
            if [ -z "$bd" ] || [ "$t" -lt "$bd" ]; then bd=$t; fi
            t=$("$work/pr" | sed -n "s/^$name \([0-9]*\) ms.*/\1/p")
            if [ -z "$br" ] || [ "$t" -lt "$br" ]; then br=$t; fi
        done
        echo "| $name (ms, clang -$level) | $bd | $br |"
    done
done
