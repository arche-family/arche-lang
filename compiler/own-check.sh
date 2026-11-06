#!/bin/sh
# The compiler's own backend, where the conformance tests do not look
# (docs/sprints.md S27): a panic's stack trace, a build that repeats, and a
# session in gdb with breakpoints, stepping and local variables.
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

if ! command -v gdb > /dev/null 2>&1; then
    if [ "${REQUIRE_GDB:-0}" = 1 ]; then
        fail "gdb is missing"
    else
        echo "own-check: no gdb here: the session is skipped"
    fi
    [ "$status" -eq 0 ] && echo "own-check: a stack trace, and a build that repeats"
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
    echo "own-check: a stack trace, a build that repeats, and gdb's breakpoints, steps and variables"
fi
exit "$status"
