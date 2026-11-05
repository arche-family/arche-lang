#!/usr/bin/env python3
"""The grammar's keywords are exactly the lexer's (docs/sprints.md S36), and its
primitive types are the spec's. Usage: check-grammar.py [repository]"""
import json
import os
import re
import sys


def words_of(pattern):
    m = re.fullmatch(r"\\b\(([a-z0-9_|]+)\)\\b", pattern)
    if m is None:
        sys.exit("check-grammar: not a list of words: " + pattern)
    return m.group(1).split("|")


def named_in(root, name, heading):
    """The names in backticks in a section of a spec."""
    with open(os.path.join(root, "spec", name), encoding="utf-8") as f:
        spec = f.read()
    start = spec.index(heading)
    return set(re.findall(r"`([a-z][a-z0-9]*)`", spec[start:spec.index("\n### ", start + 1)]))


def compare(what, grammar, source):
    status = 0
    for word in sorted(source - grammar):
        print("check-grammar: %s %r is missing from the grammar" % (what, word))
        status = 1
    for word in sorted(grammar - source):
        print("check-grammar: the grammar has %r, which is no %s" % (word, what))
        status = 1
    return status


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    with open(os.path.join(root, "editors", "arche.tmLanguage.json"), encoding="utf-8") as f:
        grammar = json.load(f)
    with open(os.path.join(root, "compiler", "syntax", "token.arche"), encoding="utf-8") as f:
        lexer = set(re.findall(r'word\.eq\("([a-z_]+)"\)', f.read()))
    if len(lexer) < 40:
        sys.exit("check-grammar: the lexer's keywords were not found")

    listed = []
    for pattern in grammar["repository"]["keywords"]["patterns"]:
        listed += words_of(pattern["match"])
    if len(listed) != len(set(listed)):
        sys.exit("check-grammar: a keyword is listed twice")
    status = compare("keyword", set(listed), lexer)

    named = named_in(root, "arche-0.md", "### 5.1") | named_in(root, "arche.md", "### 2.1")
    primitive = set(words_of(grammar["repository"]["primitive-types"]["match"]))
    for word in sorted(primitive - named):
        print("check-grammar: the grammar has the type %r, which the specs do not name" % word)
        status = 1
    if status == 0:
        print("check-grammar: %d keywords, %d primitive types" % (len(lexer), len(primitive)))
    sys.exit(status)


main()
