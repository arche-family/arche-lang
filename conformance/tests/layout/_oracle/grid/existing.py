#!/usr/bin/env python3
"""Checks the hand-written grid tests against taffy.

Translates every case function of conformance/tests/layout/grid_*.arche
(except the generated grid_fuzz*) into Rust, statement by statement, as
src/existing.rs; builds the oracle; runs it; and compares each file's lines
with the file's `// stdout:` lines.

    python3 existing.py            # translate, build, compare
"""
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
TESTS = HERE.parent.parent  # conformance/tests/layout


def close_brace(s, i):
    """The index of the `}` matching the `{` at s[i]."""
    depth = 0
    for j in range(i, len(s)):
        if s[j] == '{':
            depth += 1
        elif s[j] == '}':
            depth -= 1
            if depth == 0:
                return j
    raise ValueError(s)


def unwrap(s, head, field):
    """`HEAD { FIELD: X }` -> `HEAD(X)`, for nested X."""
    while True:
        at = s.find(head + ' { ' + field + ': ')
        if at < 0:
            return s
        brace = at + len(head) + 1
        end = close_brace(s, brace)
        inner = s[brace + 2 + len(field) + 2:end].rstrip()
        s = s[:at] + head + '(' + inner + ')' + s[end + 1:]


def expr(s):
    s = re.sub(r't\.names\.intern\(("[^"]*")\)', r'\1.to_string()', s)
    s = re.sub(r'sty\.Calc \{ px: ([^,]+), percent: ([^ ]+) \}', r't.calc(\1, \2)', s)
    s = re.sub(r'sgrid\.GridPlacement\.NamedLine \{ name: ([^,]+), index: (-?\d+) \}',
               r'GridPlacement::NamedLine(\1, \2)', s)
    s = re.sub(r'sgrid\.GridPlacement\.NamedSpan \{ name: ([^,]+), span: (\d+) \}',
               r'GridPlacement::NamedSpan(\1, \2)', s)
    s = unwrap(s, 'sgrid.GridTemplateComponent.Single', 'track')
    s = unwrap(s, 'sgrid.GridTemplateComponent.Repeat', 'repetition')
    s = re.sub(r'sgrid\.RepetitionCount\.Count \{ count: (\d+) \}', r'RepetitionCount::Count(\1)', s)
    s = re.sub(r'geo\.(Size|Rect|Line|Point)\([^)]*\) \{', r'\1 {', s)
    s = re.sub(r'(?:sty|sgrid)\.([A-Z]\w*) \{', r'\1 {', s)
    s = s.replace('sty.AvailableSpace.definite(', 'AvailableSpace::Definite(')
    s = s.replace('sty.Contain.layout()', 'Contain::LAYOUT')
    s = s.replace('sty.Contain.paint()', 'Contain::PAINT')
    s = s.replace('sty.Contain.content()', 'Contain::CONTENT')
    s = re.sub(r'(?:sty|sgrid)\.([A-Z]\w*)\.(\w+)', r'\1::\2', s)
    s = re.sub(r'\bsome\(', 'Some(', s)
    s = s.replace('_tree_grid.Content.', 'Content::')
    return s


def statement(line):
    line = line.strip()
    if line == 'var t = _tree_grid.TestTree.init(a);':
        return 'let mut t = TestTree::new();'
    m = re.fullmatch(r'var (\w+) = sty\.Style\.default\(a\);', line)
    if m:
        return 'let mut %s = Style::default();' % m.group(1)
    m = re.fullmatch(r'var (\w+) = alloc\.List\(.*\)\.init\(a\);', line)
    if m:
        return 'let mut %s = Vec::new();' % m.group(1)
    m = re.fullmatch(r't\.add_child\(parent: (\w+), child: (\w+)\);', line)
    if m:
        return 't.add_child(%s, %s);' % m.groups()
    line = line.replace('compute.compute_root_layout(', 'compute_root_layout(')
    line = line.replace('_show.layout_line(', 'layout_line(')
    line = line.replace('_tree_grid.counts_line(', 'counts_line(')
    return expr(line)


def translate(path):
    """The file's case functions in Rust and the order main calls them."""
    text = path.read_text()
    fns = []
    for m in re.finditer(r'^fn (case_\w+)\(out: &mut std\.Output, a: alloc\.Allocator\) \{\n(.*?)^\}', text, re.S | re.M):
        body = [statement(l) for l in m.group(2).splitlines() if l.strip()]
        fns.append((m.group(1), body))
    main = re.search(r'^fn main\(.*?^\}', text, re.S | re.M).group(0)
    calls = re.findall(r'(case_\w+)\(&mut sys\.stdout, a\);', main)
    return fns, calls


def expected(path):
    return [l[len('// stdout: '):] for l in path.read_text().splitlines() if l.startswith('// stdout: ')]


def main():
    files = sorted(p for p in TESTS.glob('grid_*.arche') if not p.name.startswith('grid_fuzz'))
    rs = ['//! Made by existing.py from conformance/tests/layout/grid_*.arche: do not edit.',
          '#![allow(unused_mut, unused_variables, clippy::all)]',
          '']
    run_all = ['pub fn run_all(out: &mut String) {']
    for path in files:
        mod = path.stem
        fns, calls = translate(path)
        rs.append('mod %s {' % mod)
        rs.append('    use super::super::*;')
        for name, body in fns:
            rs.append('    pub fn %s(out: &mut String) {' % name)
            rs.extend('        ' + l for l in body)
            rs.append('    }')
        rs.append('}')
        run_all.append('    out.push_str("== %s\\n");' % mod)
        run_all.extend('    %s::%s(out);' % (mod, c) for c in calls)
    run_all.append('}')
    (HERE / 'src' / 'existing.rs').write_text('\n'.join(rs + run_all) + '\n')
    subprocess.run(['cargo', 'build', '--offline', '--release', '--quiet'], cwd=HERE, check=True)
    got = subprocess.run([str(HERE / 'target' / 'release' / 'grid-oracle'), 'existing'],
                         capture_output=True, text=True, check=True).stdout
    per_file = {}
    current = None
    for line in got.splitlines():
        if line.startswith('== '):
            current = line[3:]
            per_file[current] = []
        else:
            per_file[current].append(line)
    bad = 0
    total = 0
    for path in files:
        want = expected(path)
        have = per_file[path.stem]
        total += len(want)
        if want != have:
            bad += 1
            print('%s: differs from taffy' % path.name)
            for i in range(max(len(want), len(have))):
                w = want[i] if i < len(want) else '<none>'
                h = have[i] if i < len(have) else '<none>'
                if w != h:
                    print('  test:  ' + w)
                    print('  taffy: ' + h)
        else:
            print('%s: %d lines, same as taffy' % (path.name, len(want)))
    print('%d files, %d lines, %d differ' % (len(files), total, bad))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
