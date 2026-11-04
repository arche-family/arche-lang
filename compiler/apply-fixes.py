#!/usr/bin/env python3
"""Applies the fixes of `arche check --json` to the files they are in.

Usage: python3 -I compiler/apply-fixes.py <arche> <root file>
A stand-in for `arche fix` (sprint S23).
"""
import json
import subprocess
import sys


def offset(text, line, column):
    lines = text.split("\n")
    return sum(len(l) + 1 for l in lines[:line - 1]) + column - 1


def main():
    arche, root = sys.argv[1], sys.argv[2]
    r = subprocess.run([arche, "check", "--json", root], capture_output=True, text=True)
    edits = {}
    for d in json.loads(r.stdout):
        f = d.get("fix")
        if f:
            edits.setdefault(d["file"], []).append(f)
    total = 0
    for path, fixes in edits.items():
        text = open(path, encoding="utf-8").read()
        spans = []
        for f in fixes:
            start = offset(text, f["line"], f["column"])
            end = offset(text, f["end_line"], f["end_column"])
            spans.append((start, end, f["replacement"]))
        for start, end, rep in sorted(set(spans), reverse=True):
            text = text[:start] + rep + text[end:]
            total += 1
        open(path, "w", encoding="utf-8").write(text)
    print("applied %d fixes in %d files" % (total, len(edits)))


main()
