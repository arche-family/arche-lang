#!/usr/bin/env python3
"""Generates compiler/explain.arche, what `arche explain` prints: each code's
meaning from docs/error-codes.md, and an example that gets it.

An example is compiler/explain/<code>.arche when there is one, else the
shortest compile-error test that names the code (conformance/tests and
conformance/testrunner), without its header. In an example of its own, a
first line `// arche <options> check` gives the options to check it with,
a line `// package` says it is full Arche (a package.arche beside it), a
line `// manifest: <text>` gives that package.arche, and a line
`// as package.arche` says the example is the manifest itself, and a line
`// dependency <name>: <text>` gives a vendored dependency, <text> its
package.arche.
E0101's example ends with the byte 0xFF, which arche explain adds.

Usage: python3 -I compiler/gen-explain.py [output]   (default: compiler/explain.arche)
A development tool: the bootstrap does not need Python.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = re.compile(r"^// (test|spec|line|code|stage0|wasm|exit|args|stdout|stderr):")


def codes():
    text = open(os.path.join(ROOT, "docs/error-codes.md"), encoding="utf-8", newline="").read()
    return re.findall(r"^\| `(E\d{4})` \| (.*) \|\s*$", text, re.M)


def candidates(code):
    """Test files that name the code, shortest first."""
    found = []
    for pattern in ("conformance/tests/**/*.arche", "conformance/testrunner/*.arche"):
        for path in glob.glob(os.path.join(ROOT, pattern), recursive=True):
            try:
                text = open(path, encoding="utf-8", newline="").read()
            except UnicodeDecodeError:
                continue
            if "// test: compile-error" not in text:
                continue
            named = re.findall(r"^// code: (E\d{4})", text, re.M)
            if named != [code]:
                continue
            body = "\n".join(l for l in text.split("\n") if not HEADER.match(l)).strip("\n") + "\n"
            # An example stands alone: it imports no module of its test directory.
            local = False
            for m in re.findall(r"^import ([a-z_][a-z0-9_.]*)", body, re.M):
                first = m.split(".")[0]
                if first not in ("std", "alloc", "core", "crypto", "tls", "http"):
                    local = True
            if local:
                continue
            full = os.path.exists(os.path.join(os.path.dirname(path), "package.arche"))
            found.append((len(body), path, body, full))
    found.sort()
    return found


def literal(text):
    out = []
    for ch in text:
        o = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\r":
            out.append("\\r")
        elif o < 0x20 or o == 0x7F:
            out.append("\\x%02X" % o)
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "compiler/explain.arche")
    meaning, example, full, options, manifests, files, dependencies = [], [], [], [], [], [], []
    for code, text in codes():
        n = int(code[1:])
        meaning.append((n, text))
        own = os.path.join(ROOT, "compiler/explain", code + ".arche")
        if os.path.exists(own):
            body = open(own, encoding="utf-8", newline="").read()
            first = body.split("\n", 1)[0]
            m = re.match(r"^// arche (.*) check$", first)
            if m:
                options.append((n, m.group(1)))
                body = body.split("\n", 1)[1]
            is_full = False
            while True:
                first = body.split("\n", 1)[0]
                if first == "// package":
                    is_full = True
                elif first.startswith("// manifest: "):
                    is_full = True
                    manifests.append((n, first[len("// manifest: "):] + "\n"))
                elif first == "// as package.arche":
                    files.append((n, "package.arche"))
                elif first.startswith("// dependency "):
                    name, text = first[len("// dependency "):].split(": ", 1)
                    dependencies.append((n, name + "\n" + text + "\n"))
                else:
                    break
                body = body.split("\n", 1)[1]
            example.append((n, body))
            full.append((n, is_full))
            continue
        if "Not reported yet" in text:
            continue
        c = candidates(code)
        if c:
            example.append((n, c[0][2]))
            full.append((n, c[0][3]))
    lines = [
        "/// What `arche explain <code>` prints: each error code's meaning, from",
        "/// docs/error-codes.md, and an example that gets the code. Generated by",
        "/// gen-explain.py from the error codes and the conformance tests; do not",
        "/// edit it, run that.",
        "",
        "/// The meaning of a code, or \"\" when there is no such code.",
        "pub fn meaning(code: u16) -> str {",
    ]
    for n, text in meaning:
        lines.append("    if code == %d {" % n)
        lines.append("        return %s;" % literal(text))
        lines.append("    }")
    lines += ["    return \"\";", "}", "", "/// A program that gets the code, or \"\" when there is none yet.",
              "pub fn example(code: u16) -> str {"]
    for n, body in example:
        lines.append("    if code == %d {" % n)
        lines.append("        return %s;" % literal(body))
        lines.append("    }")
    lines += ["    return \"\";", "}", "", "/// Whether the example is full Arche: it needs a package.arche beside it.",
              "pub fn example_is_full(code: u16) -> bool {"]
    for n, f in full:
        if f:
            lines.append("    if code == %d {" % n)
            lines.append("        return true;")
            lines.append("    }")
    lines += ["    return false;", "}", "", "/// The options to check the example with, before the command.",
              "pub fn example_options(code: u16) -> str {"]
    for n, o in options:
        lines.append("    if code == %d {" % n)
        lines.append("        return %s;" % literal(o))
        lines.append("    }")
    lines += ["    return \"\";", "}", "", "/// The package.arche the example needs, or \"\" for one that only says",
              "/// the package is full Arche.", "pub fn example_manifest(code: u16) -> str {"]
    for n, m in manifests:
        lines.append("    if code == %d {" % n)
        lines.append("        return %s;" % literal(m))
        lines.append("    }")
    lines += ["    return \"\";", "}", "", "/// The name the example's file must have, or \"\" for any.",
              "pub fn example_file(code: u16) -> str {"]
    for n, f in files:
        lines.append("    if code == %d {" % n)
        lines.append("        return %s;" % literal(f))
        lines.append("    }")
    lines += ["    return \"\";", "}", "", "/// A dependency the example needs vendored: its name, a line, then its",
              "/// package.arche; \"\" for none.", "pub fn example_dependency(code: u16) -> str {"]
    for n, d in dependencies:
        lines.append("    if code == %d {" % n)
        lines.append("        return %s;" % literal(d))
        lines.append("    }")
    lines += ["    return \"\";", "}", ""]
    open(out_path, "w", encoding="utf-8", newline="").write("\n".join(lines))
    missing = [c for c, t in codes() if "Not reported yet" not in t and not any(n == int(c[1:]) for n, _ in example)]
    print("explain: %d codes, %d examples%s" % (len(meaning), len(example),
          ("; no example for " + " ".join(missing)) if missing else ""))


main()
