#!/usr/bin/env python3
"""Fuzzes the parser: mutates .arche files and checks that `arche cst`
never crashes or hangs and always gives the input back byte for byte.

Usage: python3 -I compiler/fuzz.py <arche> <seconds> [seed]
Failing inputs are kept as fuzz-<n>.arche in the current directory.
A development tool: the bootstrap does not need Python.
"""
import glob
import os
import random
import subprocess
import sys
import time

TOKENS = [b"{", b"}", b"(", b")", b"[", b"]", b";", b",", b".", b"..", b"=", b"|", b"match ", b"fn ", b'"', b"'",
          b"'\\", b".{", b"some(", b"x:", b"&mut ", b"0x", b"\n", b"type T = enum {", b"pub ", b"struct {", b"=>"]


def mutate(rng, b):
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
    path = "fuzz-input.arche"
    cases = failures = 0
    end = time.time() + seconds
    while time.time() < end:
        data = mutate(rng, rng.choice(sources))
        try:
            data.decode("utf-8")
        except UnicodeDecodeError:
            continue
        cases += 1
        open(path, "wb").write(data)
        try:
            r = subprocess.run([arche, "cst", path], capture_output=True, timeout=5)
            ok = r.returncode == 0 and r.stdout == data
        except subprocess.TimeoutExpired:
            ok = False
        if not ok:
            failures += 1
            open("fuzz-%d.arche" % failures, "wb").write(data)
    os.remove(path)
    print("fuzz: %d cases, %d failures" % (cases, failures))
    sys.exit(1 if failures else 0)


main()
