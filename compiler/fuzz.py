#!/usr/bin/env python3
"""Fuzzes the parser, the checker (types, memory, the compile-time
interpreter), lowering and the optimizer: mutates .arche files and checks
that `arche cst` gives the input back byte for byte, that neither `arche
cst` nor `arche check` crashes or hangs, and that a program the checker
accepts builds, in debug or release, without crashing. A case in five is a
program of its own instead, whose constant runs a random recursion at
compile time, with traps and limits for the interpreter to meet.

Usage: python3 -I compiler/fuzz.py <arche> <seconds> [seed]
Failing inputs are kept as fuzz-<n>.arche in the current directory.
A development tool: the bootstrap does not need Python.
"""
import glob
import os
import random
import re
import subprocess
import sys
import time

TOKENS = [b"{", b"}", b"(", b")", b"[", b"]", b";", b",", b".", b"..", b"=", b"|", b"match ", b"fn ", b'"', b"'",
          b"'\\", b".{", b"some(", b"x:", b"&mut ", b"0x", b"\n", b"type T = enum {", b"pub ", b"struct {", b"=>"]


WORD = re.compile(rb"[A-Za-z_][A-Za-z0-9_]*|[0-9]+")


def mutate_meaning(rng, b):
    """Changes what a program means while its syntax mostly stays valid:
    swaps names and numbers, or drops a line, so the checker runs."""
    words = list(WORD.finditer(b))
    if not words:
        return b
    for _ in range(rng.randint(1, 3)):
        if rng.random() < 0.25:
            lines = b.split(b"\n")
            del lines[rng.randrange(len(lines))]
            b = b"\n".join(lines)
            words = list(WORD.finditer(b))
            if not words:
                return b
            continue
        target = rng.choice(words)
        source = rng.choice(words).group()
        b = b[:target.start()] + source + b[target.end():]
        words = list(WORD.finditer(b))
    return b


def mutate(rng, b):
    if rng.random() < 0.5:
        return mutate_meaning(rng, b)
    b = bytearray(b)
    for _ in range(rng.randint(1, 6)):
        i = rng.randrange(len(b) + 1)
        op = rng.random()
        if op < 0.3 and b:
            del b[i:i + rng.randint(1, 12)]
        elif op < 0.6:
            b[i:i] = rng.choice(TOKENS)
        else:
            j = rng.randrange(len(b) + 1)
            b[i:i] = b[j:j + rng.randint(1, 30)]
    return bytes(b)


def comptime_program(rng):
    """A constant computed at compile time by a recursion of random shape:
    it may overflow, divide by zero, recurse too deep or run long, which the
    interpreter must report, never crash on."""
    op = rng.choice(["+", "-", "*", "+%", "-%", "*%", "/", "%", "^", "&", "|"])
    a, b = rng.randint(0, 9), rng.randint(0, 300)
    d = rng.choice([1, 1, 2, 3, 7])
    k = rng.randint(0, 50)
    n = rng.choice([rng.randint(0, 100), rng.randint(0, 3000)])
    ty = rng.choice(["u8", "u32", "u64", "i32", "i64"])
    cond = rng.choice(["n < %d" % k, "n == %d" % k, "n <= %d" % k])
    return ("import std;\n\n"
            "fn f(n: %s) -> %s {\n"
            "    if %s {\n"
            "        return n * %d + %d;\n"
            "    }\n"
            "    return f(n - %d) %s (n %% %d + 1);\n"
            "}\n\n"
            "const X: %s = f(%d);\n\n"
            "fn main(sys: &mut std.System) -> !void {\n"
            "    sys.stdout.write_u64(X as%% u64);\n"
            "}\n" % (ty, ty, cond, a, b, d, op, rng.randint(1, 9), ty, n)).encode()


def main():
    arche, seconds = sys.argv[1], float(sys.argv[2])
    rng = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sources = []
    for f in sorted(glob.glob(os.path.join(root, "**", "*.arche"), recursive=True)):
        data = open(f, "rb").read()
        try:
            data.decode("utf-8")
            sources.append(data)
        except UnicodeDecodeError:
            pass
    # Half of the inputs are checked as full Arche, beside a manifest, so
    # generics and the compile-time interpreter are fuzzed too.
    os.makedirs("fuzz-full", exist_ok=True)
    open(os.path.join("fuzz-full", "package.arche"), "w").write("// manifest\n")
    cases = failures = 0
    end = time.time() + seconds
    while time.time() < end:
        generated = rng.random() < 0.2
        if generated:
            data = comptime_program(rng)
        else:
            data = mutate(rng, rng.choice(sources))
        try:
            data.decode("utf-8")
        except UnicodeDecodeError:
            continue
        cases += 1
        # A constant that calls a function is full Arche: beside a manifest.
        full = generated or cases % 2 == 0
        path = os.path.join("fuzz-full", "input.arche") if full else "fuzz-input.arche"
        open(path, "wb").write(data)
        try:
            r = subprocess.run([arche, "cst", path], capture_output=True, timeout=5)
            ok = r.returncode == 0 and r.stdout == data
            # The checker may reject the program, but never crash (status
            # 101) or hang.
            r = subprocess.run([arche, "check", path], capture_output=True, timeout=5)
            ok = ok and r.returncode in (0, 1)
            if ok and r.returncode == 0:
                # Accepted: it lowers and, in release, is optimized; neither
                # may crash.
                flags = ["--release"] if rng.random() < 0.5 else []
                r = subprocess.run([arche] + flags + ["build", path, "fuzz-out.ll"], capture_output=True, timeout=10)
                ok = r.returncode in (0, 1)
        except subprocess.TimeoutExpired:
            ok = False
        if not ok:
            failures += 1
            open("fuzz-%d.arche" % failures, "wb").write(data)
    for p in ("fuzz-input.arche", "fuzz-out.ll", os.path.join("fuzz-full", "input.arche"), os.path.join("fuzz-full", "package.arche")):
        if os.path.exists(p):
            os.remove(p)
    os.rmdir("fuzz-full")
    print("fuzz: %d cases, %d failures" % (cases, failures))
    sys.exit(1 if failures else 0)


main()
