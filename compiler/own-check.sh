#!/bin/sh
# The compiler's own backend and linker, where the conformance tests do not
# look (docs/sprints.md S27, S29): a panic's stack trace, a build that repeats
# and needs no other tool, what the linker refuses, and a session in gdb with
# breakpoints, stepping and local variables.
# Usage: own-check.sh. REQUIRE_GDB=1 fails where gdb is missing, as CI does.
set -u
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
fail() {
    echo "own-check: $1"
    status=1
}
cd "$here/tests/own" || exit 1
export ARCHE_BACKEND=own
"$here/arche-build" sample.arche "$work/sample" || exit 1

# The output, and how it got to the panic.
"$work/sample" > "$work/out" 2> "$work/err"
[ $? -eq 101 ] || fail "the panic did not end the program with 101"
[ "$(cat "$work/out")" = "28" ] || fail "the program printed $(cat "$work/out")"
for line in "panic: division by zero" "  at sample.arche:22:14" "  in boom (sample.arche:22)" "  in main (sample.arche:35)"; do
    grep -qxF "$line" "$work/err" || fail "the panic does not say: $line; it says: $(cat "$work/err")"
done

# A build repeats, bit for bit.
"$here/arche-build" sample.arche "$work/again" || exit 1
cmp -s "$work/sample" "$work/again" || fail "two builds differ"

# With no clang and no lld to be found, once the runtime's object is there.
CLANG=false LLD=false "$here/arche-build" sample.arche "$work/alone" || fail "the build needs a tool"
cmp -s "$work/sample" "$work/alone" || fail "the build with no tools differs"

# An ELF object still links with lld, as it did.
. "$here/target.sh"
arche=$here/build/arche
rt=$here/build/rt-$ARCHE_TARGET
if command -v "$LLD" > /dev/null 2>&1; then
    "$arche" build sample.arche "$work/elf.o" || exit 1
    "$LLD" -static -o "$work/elf" "$work/elf.o" "$rt.o" || fail "lld does not take the ELF object"
    "$work/elf" > "$work/elf.out" 2> /dev/null
    [ "$(cat "$work/elf.out")" = "28" ] || fail "the program lld linked printed $(cat "$work/elf.out")"
fi

# What the linker refuses, and how: never with a panic.
"$arche" build sample.arche "$work/sample.ao" || exit 1
link_refuses() { # what it must say, then the objects
    want=$1
    shift
    "$arche" link "$work/refused" "$@" > "$work/link.out" 2>&1
    code=$?
    [ "$code" -eq 1 ] || fail "the link ended with $code, wanted 1: $(cat "$work/link.out")"
    grep -qF "$want" "$work/link.out" || fail "the link did not say: $want; it said: $(cat "$work/link.out")"
}
link_refuses "undefined symbol" "$work/sample.ao"
link_refuses "is defined in both" "$work/sample.ao" "$work/sample.ao" "$rt.ao"
head -c 20 "$work/sample.ao" > "$work/cut.ao"
link_refuses "not an Arche object" "$work/cut.ao"
head -c 200 "$work/sample.ao" > "$work/cut.ao"
link_refuses "cut short" "$work/cut.ao"
link_refuses "not an Arche object" "$rt.o"
cp "$work/sample.ao" "$work/version.ao"
printf '\177' | dd of="$work/version.ao" bs=1 seek=12 conv=notrunc 2> /dev/null
link_refuses "another ABI version" "$work/version.ao" "$rt.ao"
cp "$work/sample.ao" "$work/version.ao"
printf '\177' | dd of="$work/version.ao" bs=1 seek=8 conv=notrunc 2> /dev/null
link_refuses "another format version" "$work/version.ao" "$rt.ao"
"$arche" --target linux-riscv64 build sample.arche "$work/other.ao" || exit 1
link_refuses "another processor" "$work/other.ao" "$rt.ao"

# Whatever an object holds: bytes changed all over its tables and names.
size=$(wc -c < "$work/sample.ao")
limit=$((size < 6000 ? size : 6000))
at=8
while [ "$at" -lt "$limit" ]; do
    for byte in '\377' '\000'; do
        cp "$work/sample.ao" "$work/bad.ao"
        printf "$byte" | dd of="$work/bad.ao" bs=1 seek="$at" conv=notrunc 2> /dev/null
        "$arche" link "$work/refused" "$work/bad.ao" "$rt.ao" > "$work/link.out" 2>&1
        code=$?
        [ "$code" -le 1 ] || fail "byte $at of an object made the linker end with $code: $(cat "$work/link.out")"
    done
    at=$((at + 13))
done
size=$(wc -c < "$rt.o")
at=4
while [ "$at" -lt "$size" ]; do
    cp "$rt.o" "$work/bad.o"
    printf '\377' | dd of="$work/bad.o" bs=1 seek="$at" conv=notrunc 2> /dev/null
    "$arche" import "$work/bad.o" "$work/bad.ao" > "$work/link.out" 2>&1
    code=$?
    [ "$code" -le 1 ] || fail "byte $at of an ELF object made import end with $code: $(cat "$work/link.out")"
    at=$((at + 397))
done

if ! command -v gdb > /dev/null 2>&1; then
    if [ "${REQUIRE_GDB:-0}" = 1 ]; then
        fail "gdb is missing"
    else
        echo "own-check: no gdb here: the session is skipped"
    fi
    [ "$status" -eq 0 ] && echo "own-check: a stack trace, a build that repeats with no tools, and what the linker refuses"
    exit "$status"
fi
cat > "$work/session" <<'SESSION'
set pagination off
set debuginfod enabled off
set confirm off
break add
run
info args
next
print sum
bt
finish
delete
break sample.arche:29
continue
step
info args
delete
break norm
continue
print p
print p.x
print label.len
next
print square
up
print total
print i
kill
SESSION
gdb -q -batch -x "$work/session" "$work/sample" > "$work/gdb" 2>&1
expect() {
    grep -qF "$1" "$work/gdb" || fail "gdb did not say: $1"
}
# A breakpoint stops after the parameters are in place, on the body's line.
expect "Breakpoint 1, add (a=0, b=0) at sample.arche:11"
expect "12	    return sum;"
expect '$1 = 0'
expect "#0  add (a=0, b=0) at sample.arche:12"
expect "in main (sys="
# Stepping goes into the call.
expect "add (a=0, b=1) at sample.arche:11"
expect "Breakpoint 3, norm (p=..., label=...) at sample.arche:16"
expect "= {x = 3, y = 4}"
expect "17	    return square;"
expect '= 25'
expect "32	    total = total + norm(p, label: \"here\");"
if [ "$status" -ne 0 ]; then
    cat "$work/gdb"
else
    echo "own-check: a stack trace, a build that repeats with no tools, what the linker refuses, and gdb's breakpoints, steps and variables"
fi
exit "$status"
