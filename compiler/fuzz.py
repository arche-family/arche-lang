#!/usr/bin/env python3
"""Fuzzes the parser and the checker: mutates .arche files and checks that
`arche cst` gives the input back byte for byte, and that neither `arche cst`
nor `arche check` crashes or hangs.

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
        data = mutate(rng, rng.choice(sources))
        try:
            data.decode("utf-8")
        except UnicodeDecodeError:
            continue
        cases += 1
        path = "fuzz-input.arche" if cases % 2 else os.path.join("fuzz-full", "input.arche")
        open(path, "wb").write(data)
        try:
            r = subprocess.run([arche, "cst", path], capture_output=True, timeout=5)
            ok = r.returncode == 0 and r.stdout == data
            # The checker may reject the program, but never crash (status
            # 101) or hang.
            r = subprocess.run([arche, "check", path], capture_output=True, timeout=5)
            ok = ok and r.returncode in (0, 1)
        except subprocess.TimeoutExpired:
            ok = False
        if not ok:
            failures += 1
            open("fuzz-%d.arche" % failures, "wb").write(data)
    for p in ("fuzz-input.arche", os.path.join("fuzz-full", "input.arche"), os.path.join("fuzz-full", "package.arche")):
        if os.path.exists(p):
            os.remove(p)
    os.rmdir("fuzz-full")
    print("fuzz: %d cases, %d failures" % (cases, failures))
    sys.exit(1 if failures else 0)


main()
