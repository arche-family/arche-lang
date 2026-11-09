#!/usr/bin/env python3
"""Writes the generated conformance tests (docs/sprints.md S34) into
conformance/tests/gen/: families of tests, each test one rule's case, whose
expected output this script computes from its own model of the rule. The
same seed writes the same files; the files are kept in the repository, so
running the suite needs no Python. A development tool, as fuzz.py is.

Usage: python3 -I conformance/gen/gen.py
"""
import json
import os
import random
import shutil
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "tests", "gen")

INTS = {
    "u8": (False, 8), "u16": (False, 16), "u32": (False, 32), "u64": (False, 64),
    "i8": (True, 8), "i16": (True, 16), "i32": (True, 32), "i64": (True, 64),
}
NAMES = list(INTS)


def lo(t):
    s, w = INTS[t]
    return -(1 << (w - 1)) if s else 0


def hi(t):
    s, w = INTS[t]
    return (1 << (w - 1)) - 1 if s else (1 << w) - 1


def wrap(t, v):
    s, w = INTS[t]
    v &= (1 << w) - 1
    if s and v >= 1 << (w - 1):
        v -= 1 << w
    return v


def lit(t, v):
    """v as an expression of type t: a typed local's initializer. The most
    negative value is written as one less than the next, which a literal of
    its own size could not hold."""
    if v == lo(t) and INTS[t][0]:
        return "%d - 1" % (v + 1)
    return str(v)


def trunc_div(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def trunc_rem(a, b):
    return a - b * trunc_div(a, b)


files = []


def write(rel, text):
    path = os.path.join(OUT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)
    files.append(rel)


def header(kind, spec, stdout=(), extra=()):
    lines = ["// test: %s" % kind, "// spec: %s" % spec]
    lines += list(extra)
    lines += ["// stdout: %s" % s for s in stdout]
    return "\n".join(lines) + "\n"


def write_error(rel, spec, code, body, target, erasable=False):
    """A compile-error test whose diagnostic is on the line of body that
    holds target, counted after its header's lines. An erasable check
    (spec/arche-0.md §12) is not stage0's."""
    extra = ["// code: %s" % code]
    if erasable:
        extra.append("// stage0: skip")
    lines = body.split("\n")
    at = next(i for i, l in enumerate(lines) if target in l)
    write(rel, header("compile-error", spec, (), ["// line: %d" % (at + 4 + len(extra))] + extra) + body)


def show(t, expr):
    """A statement that writes expr, of type t, on a line of its own."""
    if INTS[t][0]:
        return "    sys.stdout.write_i64(%s as i64);\n    sys.stdout.write(\"\\n\");\n" % expr
    return "    sys.stdout.write_u64(%s as u64);\n    sys.stdout.write(\"\\n\");\n" % expr


def edge_values(t, rng):
    vals = {0, 1, 2, 3, 7, hi(t), hi(t) - 1, lo(t), (hi(t) + 1) // 2, rng.randint(lo(t), hi(t)), rng.randint(lo(t), hi(t))}
    if INTS[t][0]:
        vals |= {-1, -2, -7, lo(t) + 1}
    return sorted(v for v in vals if lo(t) <= v <= hi(t))


# ---- integer arithmetic -----------------------------------------------------------------

OPS = {
    "add": "+", "sub": "-", "mul": "*", "div": "/", "rem": "%", "add-wrap": "+%", "sub-wrap": "-%",
    "mul-wrap": "*%", "and": "&", "or": "|", "xor": "^", "shl": "<<", "shr": ">>",
}


def model(op, t, a, b):
    """The result, or None when Arche traps on it."""
    s, w = INTS[t]
    if op in ("+", "-", "*"):
        r = a + b if op == "+" else a - b if op == "-" else a * b
        return r if lo(t) <= r <= hi(t) else None
    if op in ("+%", "-%", "*%"):
        return wrap(t, a + b if op == "+%" else a - b if op == "-%" else a * b)
    if op == "/":
        if b == 0:
            return None
        r = trunc_div(a, b)
        return r if lo(t) <= r <= hi(t) else None
    if op == "%":
        if b == 0:
            return None
        return trunc_rem(a, b)
    if op in ("&", "|", "^"):
        x, y = a & ((1 << w) - 1), b & ((1 << w) - 1)
        return wrap(t, x & y if op == "&" else x | y if op == "|" else x ^ y)
    if op == "<<":
        return None if b >= w else wrap(t, (a & ((1 << w) - 1)) << b)
    if op == ">>":
        return None if b >= w else a >> b
    raise ValueError(op)


def int_ops(rng):
    for t in NAMES:
        for name, op in OPS.items():
            shift = op in ("<<", ">>")
            pairs = []
            vals = edge_values(t, rng)
            for _ in range(400):
                a = rng.choice(vals)
                b = rng.randint(0, INTS[t][1] - 1) if shift else rng.choice(vals)
                if model(op, t, a, b) is not None and (a, b) not in pairs:
                    pairs.append((a, b))
                if len(pairs) == 8:
                    break
            body = ""
            out = []
            bt = "u32" if shift else t
            for i, (a, b) in enumerate(pairs):
                body += "    let a%d: %s = %s;\n    let b%d: %s = %s;\n" % (i, t, lit(t, a), i, bt, lit(bt, b) if not shift else b)
                body += show(t, "(a%d %s b%d)" % (i, op, i))
                out.append(str(model(op, t, a, b)))
            write("ints/%s-%s.arche" % (t, name),
                  header("run", "7.2", out) + "import std;\n\n" +
                  "// %s %s on edge values; the expected results are gen.py's.\n" % (t, op) +
                  "fn main(sys: &mut std.System) -> !void {\n" + body + "}\n")


def int_traps(rng):
    cases = []
    for t in NAMES:
        s, w = INTS[t]
        cases.append((t, "add-overflow", "+", hi(t), 1, "integer overflow", True))
        cases.append((t, "sub-overflow", "-", lo(t), 1, "integer overflow", True))
        cases.append((t, "mul-overflow", "*", hi(t), 2, "integer overflow", True))
        cases.append((t, "div-zero", "/", 7, 0, "division by zero", False))
        cases.append((t, "rem-zero", "%", 7, 0, "division by zero", False))
        cases.append((t, "shl-width", "<<", 1, w, "shift amount too large", False))
        cases.append((t, "shr-width", ">>", 1, w + rng.randint(0, 3), "shift amount too large", False))
        if s:
            cases.append((t, "div-min", "/", lo(t), -1, "integer overflow", True))
    for t, name, op, a, b, why, overflow in cases:
        shift = op in ("<<", ">>")
        bt = "u32" if shift else t
        extra = ["// exit: 101", "// stderr: panic: %s" % why]
        if overflow:
            # stage0 may leave overflow unchecked (spec/arche-0.md §12).
            extra.append("// stage0: skip")
        body = ("fn pass(x: %s) -> %s {\n    return x;\n}\n\n" % (t, t) +
                "fn main(sys: &mut std.System) -> !void {\n" +
                "    let a: %s = pass(%s);\n" % (t, lit(t, a)) +
                "    let b: %s = %s;\n" % (bt, lit(bt, b)) +
                "    let c = a %s b;\n" % op + show(t, "c") + "}\n")
        write("int-traps/%s-%s.arche" % (t, name), header("run", "7.2", (), extra) + "import std;\n\n" + body)


def casts(rng):
    for f in NAMES:
        for t in NAMES:
            if f == t:
                continue
            # as: values both types hold.
            both = [v for v in edge_values(f, rng) + edge_values(t, rng) if max(lo(f), lo(t)) <= v <= min(hi(f), hi(t))]
            vals = sorted(set(both))[:8]
            body = ""
            for i, v in enumerate(vals):
                body += "    let x%d: %s = %s;\n" % (i, f, lit(f, v)) + show(t, "(x%d as %s)" % (i, t))
            write("casts/%s-as-%s.arche" % (f, t), header("run", "7.4", [str(v) for v in vals]) + "import std;\n\n" +
                  "fn main(sys: &mut std.System) -> !void {\n" + body + "}\n")
            # as%: any value, its low bits.
            vals = edge_values(f, rng)[:8]
            body = ""
            for i, v in enumerate(vals):
                body += "    let x%d: %s = %s;\n" % (i, f, lit(f, v)) + show(t, "(x%d as%% %s)" % (i, t))
            write("casts/%s-wrap-%s.arche" % (f, t), header("run", "7.4", [str(wrap(t, v)) for v in vals]) + "import std;\n\n" +
                  "fn main(sys: &mut std.System) -> !void {\n" + body + "}\n")
            # as that cannot hold: a trap.
            outside = [v for v in edge_values(f, rng) if not (lo(t) <= v <= hi(t))]
            if outside:
                v = outside[0]
                write("cast-traps/%s-as-%s.arche" % (f, t),
                      header("run", "7.4", (), ["// exit: 101", "// stderr: panic: value does not fit in %s" % t]) +
                      "import std;\n\nfn pass(x: %s) -> %s {\n    return x;\n}\n\n" % (f, f) +
                      "fn main(sys: &mut std.System) -> !void {\n    let x: %s = pass(%s);\n" % (f, lit(f, v)) +
                      show(t, "(x as %s)" % t) + "}\n")


def comparisons(rng):
    for t in NAMES:
        vals = edge_values(t, rng)
        body = ""
        out = []
        for i in range(12):
            a, b = rng.choice(vals), rng.choice(vals)
            body += "    let a%d: %s = %s;\n    let b%d: %s = %s;\n" % (i, t, lit(t, a), i, t, lit(t, b))
            for op, f in (("==", a == b), ("!=", a != b), ("<", a < b), ("<=", a <= b), (">", a > b), (">=", a >= b)):
                body += "    if a%d %s b%d { sys.stdout.write(\"1\"); } else { sys.stdout.write(\"0\"); }\n" % (i, op, i)
                out.append("1" if f else "0")
            body += "    sys.stdout.write(\"\\n\");\n"
        lines = ["".join(out[k:k + 6]) for k in range(0, len(out), 6)]
        write("compare/%s.arche" % t, header("run", "7.3", lines) + "import std;\n\n" +
              "fn main(sys: &mut std.System) -> !void {\n" + body + "}\n")


# ---- Arche-0's library: List, Map, String -----------------------------------------------

def lists(rng, n):
    for k in range(n):
        model_ = []
        body = "    var xs = alloc.List(u64).init(sys.alloc);\n"
        out = []
        for _ in range(rng.randint(10, 40)):
            r = rng.random()
            if r < 0.55 or not model_:
                v = rng.randint(0, 10 ** 6)
                body += "    xs.push(%d);\n" % v
                model_.append(v)
            elif r < 0.7:
                body += "    match xs.pop() {\n        some(v) => { sys.stdout.write_u64(v); }\n        none => { sys.stdout.write(\"none\"); }\n    }\n    sys.stdout.write(\"\\n\");\n"
                out.append(str(model_.pop()))
            elif r < 0.85:
                i = rng.randrange(len(model_))
                body += "    sys.stdout.write_u64(xs.items()[%d]);\n    sys.stdout.write(\"\\n\");\n" % i
                out.append(str(model_[i]))
            else:
                body += "    sys.stdout.write_u64(xs.len() as u64);\n    sys.stdout.write(\"\\n\");\n"
                out.append(str(len(model_)))
        body += "    var total: u64 = 0;\n    var i: usize = 0;\n    while i < xs.len() : (i += 1) {\n        total = total +% xs.items()[i];\n    }\n    sys.stdout.write_u64(total);\n    sys.stdout.write(\"\\n\");\n"
        out.append(str(sum(model_) % (1 << 64)))
        write("library/list-%03d.arche" % k, header("run", "11", out) + "import alloc;\nimport std;\n\n" +
              "fn main(sys: &mut std.System) -> !void {\n" + body + "}\n")


def maps(rng, n):
    for k in range(n):
        m = {}
        body = "    var m = alloc.Map(u64, u64).init(sys.alloc);\n"
        out = []
        keys = [rng.randint(0, 50) for _ in range(12)]
        for _ in range(rng.randint(15, 50)):
            r = rng.random()
            key = rng.choice(keys)
            if r < 0.45:
                v = rng.randint(0, 10 ** 6)
                body += "    m.put(key: %d, value: %d);\n" % (key, v)
                m[key] = v
            elif r < 0.65:
                body += "    match m.remove(%d) {\n        some(v) => { sys.stdout.write_u64(v); }\n        none => { sys.stdout.write(\"none\"); }\n    }\n    sys.stdout.write(\"\\n\");\n" % key
                out.append(str(m.pop(key)) if key in m else "none")
            elif r < 0.9:
                body += "    match m.get(%d) {\n        some(v) => { sys.stdout.write_u64(v); }\n        none => { sys.stdout.write(\"none\"); }\n    }\n    sys.stdout.write(\"\\n\");\n" % key
                out.append(str(m[key]) if key in m else "none")
            else:
                body += "    sys.stdout.write_u64(m.len() as u64);\n    sys.stdout.write(\"\\n\");\n"
                out.append(str(len(m)))
        write("library/map-%03d.arche" % k, header("run", "11", out) + "import alloc;\nimport std;\n\n" +
              "fn main(sys: &mut std.System) -> !void {\n" + body + "}\n")


WORDS = ["arche", "tosh", "kent", "x", "", "data", "a b", "zz", "longer word", "0", "end"]


def strings(rng, n):
    for k in range(n):
        s = ""
        body = "    var s = alloc.String.init(sys.alloc);\n"
        out = []
        for _ in range(rng.randint(8, 25)):
            r = rng.random()
            if r < 0.4:
                w = rng.choice(WORDS)
                body += "    s.push_str(\"%s\");\n" % w
                s += w
            elif r < 0.55:
                c = rng.choice("abcxyz-_.")
                body += "    s.push_ascii('%s');\n" % c
                s += c
            elif r < 0.7:
                body += "    sys.stdout.write_u64(s.len() as u64);\n    sys.stdout.write(\"\\n\");\n"
                out.append(str(len(s)))
            elif r < 0.85:
                w = rng.choice(WORDS + [s[:rng.randint(0, len(s))]])
                body += "    if s.view().starts_with(\"%s\") { sys.stdout.write(\"yes\\n\"); } else { sys.stdout.write(\"no\\n\"); }\n" % w
                out.append("yes" if s.startswith(w) else "no")
            else:
                n2 = rng.randint(0, 9999)
                body += "    s.push_u64(%d);\n" % n2
                s += str(n2)
        body += "    sys.stdout.write(\"[\");\n    sys.stdout.write(s.view());\n    sys.stdout.write(\"]\\n\");\n"
        out.append("[%s]" % s)
        write("full/string/string-%03d.arche" % k, header("run", "11", out, ["// stage0: skip"]) + "import alloc;\nimport std;\n\n" +
              "fn main(sys: &mut std.System) -> !void {\n" + body + "}\n")


# ---- negative tests ------------------------------------------------------------------------

def mismatches(rng):
    types = NAMES + ["bool"]
    for t in types:
        for u in types:
            if t == u:
                continue
            init = "true" if u == "bool" else "7"
            write_error("errors/let-%s-from-%s.arche" % (t, u), "5.6", "E0401",
                        "import std;\n\nfn main(sys: &mut std.System) -> !void {\n" +
                        "    let y: %s = %s;\n    // A %s is not a %s, and nothing converts it unasked.\n    let x: %s = y;\n}\n" % (u, init, u, t, t),
                        "let x:")
    for t in NAMES:
        for u in NAMES:
            if t == u:
                continue
            write_error("errors/mixed-%s-%s.arche" % (t, u), "7.3", "E0402",
                        "import std;\n\nfn main(sys: &mut std.System) -> !void {\n" +
                        "    let a: %s = 1;\n    let b: %s = 2;\n    // Both operands of + have one type.\n    let c = a + b;\n}\n" % (t, u),
                        "let c =")


def labels(rng):
    for t in NAMES + ["bool", "str"]:
        for n in (2, 3):
            params = ", ".join("p%d: %s" % (i, t) for i in range(n))
            arg = "true" if t == "bool" else '"x"' if t == "str" else "1"
            args = ", ".join([arg] * n)
            write_error("errors/labels-%s-%d.arche" % (t, n), "7.5", "E0409",
                        "import std;\n\nfn f(%s) {\n}\n\n" % params +
                        "fn main(sys: &mut std.System) -> !void {\n    // %d parameters of one type: their arguments are labeled.\n" % n +
                        "    f(%s);\n}\n" % args,
                        "    f(", erasable=True)


SHADOW = [
    ("param", "fn f(x: u32) {\n    let x: u32 = 2;\n}\n", "let x: u32 = 2"),
    ("nested", "fn f() {\n    let x: u32 = 1;\n    if true {\n        let x: u32 = 2;\n    }\n}\n", "let x: u32 = 2"),
    ("function", "fn x() {\n}\n\nfn f() {\n    let x: u32 = 2;\n}\n", "let x: u32 = 2"),
    ("const", "const x: u32 = 1;\n\nfn f() {\n    let x: u32 = 2;\n}\n", "let x: u32 = 2"),
    ("loop", "fn f() {\n    var i: u32 = 0;\n    while i < 3 {\n        let i: u32 = 9;\n    }\n}\n", "let i: u32 = 9"),
]


def shadowing(rng):
    for name, code, target in SHADOW:
        write_error("errors/shadow-%s.arche" % name, "6.2", "E0305",
                    "import std;\n\n" + code + "\nfn main(sys: &mut std.System) -> !void {\n}\n", target, erasable=True)


# ---- full Arche's library: json, deflate, kv, web ------------------------------------------

FULL_MANIFEST = "// A manifest: the tests here are full Arche (spec/arche.md §1.1).\n"


def json_value(rng, depth):
    r = rng.random()
    if depth > 3 or r < 0.3:
        return rng.choice([None, True, False, rng.randint(-1000, 10 ** 9), rng.choice(["1.5", "-0.25", "3e8"]), rng.choice(WORDS + ["q\"uote", "back\\slash", "line\nbreak"])])
    if r < 0.65:
        return [json_value(rng, depth + 1) for _ in range(rng.randint(0, 4))]
    keys = rng.sample(["a", "b", "name", "id", "z", "key", "list"], rng.randint(0, 4))
    return {k: json_value(rng, depth + 1) for k in keys}


def as_text(v):
    if isinstance(v, str) and v in ("1.5", "-0.25", "3e8"):
        return v
    if isinstance(v, list):
        return "[" + ",".join(as_text(x) for x in v) + "]"
    if isinstance(v, dict):
        return "{" + ",".join(json.dumps(k) + ":" + as_text(x) for k, x in v.items()) + "}"
    return json.dumps(v)


def arche_string(s):
    return s.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n")


def jsons(rng, n):
    for k in range(n):
        v = json_value(rng, 0)
        compact = as_text(v)
        # Written with spaces and newlines, read back, written compact.
        spaced = compact.replace(",", ", ").replace(":", ": ") if '"' not in compact else compact.replace(",\"", ", \"")
        write("full/json/json-%03d.arche" % k, header("run", "std.json", [compact], ["// stage0: skip"]) +
              "import alloc;\nimport std;\nimport std.json;\n\nfn main(sys: &mut std.System) -> !void {\n" +
              "    let v = try json.parse(\"%s\", sys.alloc);\n" % arche_string(spaced) +
              "    var out = alloc.String.init(sys.alloc);\n    json.write(&v, out: &mut out);\n" +
              "    sys.stdout.write(out.view());\n    sys.stdout.write(\"\\n\");\n}\n")


def deflates(rng, n):
    for k in range(n):
        size = rng.choice([0, 1, 17, 300, 4000, 70000])
        seed = rng.randint(1, 2 ** 31)
        alphabet = rng.choice([2, 4, 26, 256])
        data = bytearray()
        x = seed
        for _ in range(size):
            x ^= (x << 13) & 0xFFFFFFFF
            x ^= x >> 17
            x ^= (x << 5) & 0xFFFFFFFF
            data.append(x % alphabet)
        crc = zlib.crc32(bytes(data))
        write("full/deflate/deflate-%03d.arche" % k,
              header("run", "core.deflate", [str(crc), "round trip %d" % size], ["// stage0: skip"]) +
              "import alloc;\nimport core.deflate;\nimport std;\n\nfn main(sys: &mut std.System) -> !void {\n" +
              "    var data = alloc.List(u8).init(sys.alloc);\n    var x: u32 = %d;\n" % seed +
              "    var i: u32 = 0;\n    while i < %d : (i += 1) {\n" % size +
              "        x = x ^ (x << 13);\n        x = x ^ (x >> 17);\n        x = x ^ (x << 5);\n" +
              "        data.push((x %% %d) as u8);\n    }\n" % alphabet +
              "    sys.stdout.write_u64(deflate.crc32(0, data: data.items()) as u64);\n    sys.stdout.write(\"\\n\");\n" +
              "    var packed = alloc.List(u8).init(sys.alloc);\n    deflate.gzip(data.items(), out: &mut packed, sys.alloc);\n" +
              "    var back = alloc.List(u8).init(sys.alloc);\n    try deflate.gunzip(packed.items(), out: &mut back, limit: 1 << 20);\n" +
              "    var same = back.len() == data.len();\n    var k: usize = 0;\n" +
              "    while same and k < back.len() : (k += 1) {\n        same = back.items()[k] == data.items()[k];\n    }\n" +
              "    if same {\n        sys.stdout.write(\"round trip \");\n        sys.stdout.write_u64(back.len() as u64);\n        sys.stdout.write(\"\\n\");\n    }\n}\n")


def routers(rng, n):
    segs = ["users", "files", "api", "v1", "notes", "a", "b"]
    for k in range(n):
        patterns = []
        for _ in range(rng.randint(2, 6)):
            parts = []
            for j in range(rng.randint(1, 3)):
                r = rng.random()
                parts.append(":p%d" % j if r < 0.3 else rng.choice(segs))
            if rng.random() < 0.2:
                parts.append("*rest")
            patterns.append((rng.choice(["GET", "POST"]), "/" + "/".join(parts)))
        body = "    var r = web.Router.init(sys.alloc);\n"
        for m, p in patterns:
            body += "    _ = r.add(method: \"%s\", pattern: \"%s\");\n" % (m, p)
        body += "    var params = web.Params.init(sys.alloc);\n"
        out = []
        for _ in range(6):
            m, p = rng.choice(patterns)
            path = "/" + "/".join(
                (rng.choice(["7", "x", "hello"]) if s.startswith(":") else "deep/er" if s.startswith("*") else s)
                for s in p[1:].split("/"))
            if rng.random() < 0.3:
                path = "/" + rng.choice(segs) + "/" + rng.choice(segs)
            method = rng.choice(["GET", "POST", "PUT"])
            body += ("    var q%d = http.Request.init(sys.alloc);\n    q%d.method.push_str(\"%s\");\n    q%d.target.push_str(\"%s\");\n" % (len(out), len(out), method, len(out), path) +
                     "    show(sys, found: r.find(&q%d, params: &mut params), params: &params);\n" % len(out))
            out.append(route_model(patterns, method, path))
        write("full/web/router-%03d.arche" % k, header("run", "web", out, ["// stage0: skip"]) +
              "import http;\nimport std;\nimport web;\n\n" +
              "fn show(sys: &mut std.System, found: web.Found, params: &web.Params) {\n" +
              "    match found {\n        .Route { route } => {\n            sys.stdout.write_u64(route as u64);\n" +
              "            var i: usize = 0;\n            while i < params.len() : (i += 1) {\n" +
              "                sys.stdout.write(\" \");\n                sys.stdout.write(params.names_at(i));\n" +
              "                sys.stdout.write(\"=\");\n                sys.stdout.write(params.value(i));\n            }\n        }\n" +
              "        .WrongMethod => { sys.stdout.write(\"405\"); }\n        .NotFound => { sys.stdout.write(\"404\"); }\n    }\n" +
              "    sys.stdout.write(\"\\n\");\n}\n\nfn main(sys: &mut std.System) -> !void {\n" + body + "}\n")


def match_model(pattern, path):
    ps = pattern[1:].split("/")
    qs = path[1:].split("/")
    params = []
    for i, p in enumerate(ps):
        if p.startswith("*"):
            # The rest after a '/', which must be there; it may be empty.
            if i >= len(qs):
                return None
            params.append((p[1:], "/".join(qs[i:])))
            return params
        if i >= len(qs):
            return None
        if p.startswith(":"):
            if qs[i] == "":
                return None
            params.append((p[1:], qs[i]))
        elif p != qs[i]:
            return None
    return params if len(qs) == len(ps) else None


def route_model(patterns, method, path):
    wrong = False
    for i, (m, p) in enumerate(patterns):
        got = match_model(p, path)
        if got is None:
            continue
        if m == method:
            return " ".join([str(i)] + ["%s=%s" % kv for kv in got])
        wrong = True
    return "405" if wrong else "404"


def kvs(rng, n):
    for k in range(n):
        model_ = {}
        body = ("    let tmp = try sys.fs.temp_dir(&sys.env, sys.alloc);\n    var path = alloc.String.from(tmp.view(), sys.alloc);\n" +
                "    path.push_str(\"/arche-gen-kv-%03d-\");\n    path.push_u64(sys.proc.pid() as u64);\n" % k +
                "    var wal = alloc.String.from(path.view(), sys.alloc);\n    wal.push_str(\"-wal\");\n" +
                "    _ = sys.fs.remove(path.view());\n    _ = sys.fs.remove(wal.view());\n" +
                "    var db = try kv.open(&sys.fs, path.view(), sys.alloc);\n    var value = alloc.List(u8).init(sys.alloc);\n")
        out = []
        keys = ["k%02d" % rng.randint(0, 40) for _ in range(15)]
        for step in range(rng.randint(20, 60)):
            r = rng.random()
            key = rng.choice(keys)
            if r < 0.45:
                v = "v%d" % rng.randint(0, 10 ** 6)
                body += "    try db.put(key: \"%s\".bytes(), value: \"%s\".bytes());\n" % (key, v)
                model_[key] = v
            elif r < 0.6:
                body += "    _ = try db.delete(\"%s\".bytes());\n" % key
                model_.pop(key, None)
            elif r < 0.85:
                body += ("    if try db.get(\"%s\".bytes(), value: &mut value) {\n        print_bytes(sys, value.items());\n    } else {\n        sys.stdout.write(\"none\\n\");\n    }\n" % key)
                out.append(model_.get(key, "none"))
            elif r < 0.93:
                body += "    try db.commit();\n"
            else:
                body += "    try db.close();\n    db = try kv.open(&sys.fs, path.view(), sys.alloc);\n"
        body += ("    var key = alloc.List(u8).init(sys.alloc);\n    var at = alloc.List(u8).init(sys.alloc);\n    var first = true;\n" +
                 "    while try db.next(at.items(), inclusive: first, key: &mut key, value: &mut value) {\n        print_bytes(sys, key.items());\n" +
                 "        at.clear();\n        for b in key.items() {\n            at.push(b.*);\n        }\n        first = false;\n    }\n" +
                 "    try db.close();\n    _ = sys.fs.remove(path.view());\n    _ = sys.fs.remove(wal.view());\n")
        out += sorted(model_)
        write("full/kv/kv-%03d.arche" % k, header("run", "kv", out, ["// stage0: skip", "// wasm: skip"]) +
              "import alloc;\nimport kv;\nimport std;\n\nfn print_bytes(sys: &mut std.System, b: []u8) {\n" +
              "    var s = alloc.String.init(sys.alloc);\n    _ = s.push_utf8(b);\n    sys.stdout.write(s.view());\n    sys.stdout.write(\"\\n\");\n}\n\n" +
              "fn main(sys: &mut std.System) -> !void {\n" + body + "}\n")


def main():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    rng = random.Random(2026)
    int_ops(rng)
    int_traps(rng)
    casts(rng)
    comparisons(rng)
    lists(rng, 250)
    maps(rng, 250)
    os.makedirs(os.path.join(OUT, "full", "string"), exist_ok=True)
    with open(os.path.join(OUT, "full", "string", "package.arche"), "w") as f:
        f.write(FULL_MANIFEST)
    strings(rng, 100)
    mismatches(rng)
    labels(rng)
    shadowing(rng)
    for d in ("json", "deflate", "web", "kv"):
        os.makedirs(os.path.join(OUT, "full", d), exist_ok=True)
        with open(os.path.join(OUT, "full", d, "package.arche"), "w") as f:
            f.write(FULL_MANIFEST)
    jsons(rng, 200)
    deflates(rng, 60)
    routers(rng, 150)
    kvs(rng, 80)
    print("gen: %d tests in %s" % (len(files), OUT))


main()
