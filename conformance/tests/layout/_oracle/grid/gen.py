#!/usr/bin/env python3
"""Makes the random grid tests, conformance/tests/layout/grid_fuzz*.arche.

One case list, from seeded random trees in _tree_grid.arche's case language,
goes two ways: to cases.txt, which the oracle (src/main.rs, over the real
taffy 0.15) runs, and, with the oracle's lines as the expected output, into
the Arche test files. The same seeds give the same files.

    python3 gen.py          # build the oracle, write cases.txt and the tests
    python3 gen.py soak SEED COUNT [LEAN]
                            # COUNT more cases from another seed, not kept:
                            # the Arche side (built by compiler/arche-build,
                            # with ARCHE_FLAGS as set) against taffy; prints
                            # the cases that differ. LEAN: mixed, intrinsic,
                            # placement, repeat or abs
"""
import os
import pathlib
import random
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
TESTS = HERE.parent.parent  # conformance/tests/layout

# (file, seed, cases, what the cases lean to)
SUITES = [
    ('grid_fuzz1', 21, 60, 'mixed'),
    ('grid_fuzz2', 22, 60, 'mixed'),
    ('grid_fuzz3', 23, 60, 'intrinsic'),
    ('grid_fuzz4', 24, 60, 'placement'),
    ('grid_fuzz5', 25, 60, 'repeat'),
    ('grid_fuzz6', 26, 50, 'abs'),
]

LENGTHS = ['0', '5', '10', '12.5', '20', '33.3', '40', '50', '75.75', '100', '150.5']
PERCENTS = ['10%', '25%', '33%', '50%', '100%']
CALCS = ['calc(10+10%)', 'calc(-5+25%)', 'calc(0+50%)', 'calc(20+5%)']
NAMES = ['a', 'b', 'x', 'y', 'hd-start', 'hd-end', 'main-start', 'main-end', 'side']
AREA_NAMES = ['hd', 'main', 'side', 'ft']
ITEMS = ['normal', 'start', 'end', 'flex-start', 'flex-end', 'self-start', 'self-end', 'center', 'baseline',
         'stretch']
CONTENTS = ['normal', 'start', 'end', 'flex-start', 'flex-end', 'center', 'stretch', 'space-between',
            'space-evenly', 'space-around']


class Gen:
    def __init__(self, seed, lean):
        self.r = random.Random(seed)
        self.lean = lean

    def chance(self, p):
        return self.r.random() < p

    def pick(self, xs):
        return self.r.choice(xs)

    def length(self):
        return self.pick(LENGTHS)

    def lp(self):
        x = self.r.random()
        if x < 0.6:
            return self.length()
        if x < 0.85:
            return self.pick(PERCENTS)
        return self.pick(CALCS)

    def lpa(self):
        return 'auto' if self.chance(0.15) else self.lp()

    def dimension(self):
        x = self.r.random()
        if x < 0.5:
            return self.length()
        if x < 0.65:
            return self.pick(PERCENTS)
        if x < 0.72:
            return self.pick(CALCS)
        return self.pick(['auto', 'min-content', 'max-content', 'fit-content', 'stretch',
                          'fit-content(40)', 'fit-content(25%)'])

    def min_track(self):
        x = self.r.random()
        if x < 0.35:
            return self.pick(['10', '20', '25', '40', '50', '0'])
        if x < 0.5:
            return self.pick(PERCENTS)
        if x < 0.55:
            return self.pick(CALCS)
        return self.pick(['auto', 'min-content', 'max-content'])

    def max_track(self):
        x = self.r.random()
        if x < 0.3:
            return self.pick(['10', '20', '30', '40', '60', '100'])
        if x < 0.4:
            return self.pick(PERCENTS)
        if x < 0.45:
            return self.pick(CALCS)
        if x < 0.65:
            return self.pick(['1fr', '2fr', '0.5fr', '0fr', '3fr'])
        if x < 0.75:
            return 'fit-content(%s)' % self.pick(['20', '50', '25%', 'calc(5+10%)'])
        return self.pick(['auto', 'min-content', 'max-content'])

    def fixed_track(self):
        x = self.r.random()
        if x < 0.6:
            return self.pick(['10', '20', '25', '30', '40', '50', '60'])
        if x < 0.8:
            return self.pick(PERCENTS)
        if x < 0.9:
            return self.pick(CALCS)
        return 'minmax(%s,%s)' % (self.pick(['10', '20', '25%']), self.pick(['40', '1fr', 'auto', '50%']))

    def track(self):
        x = self.r.random()
        intrinsic = 0.5 if self.lean == 'intrinsic' else 0.25
        if x < 0.3:
            return self.pick(['10', '20', '25', '30', '40', '50', '60', '75.5', '100'])
        if x < 0.4:
            return self.pick(PERCENTS + CALCS)
        if x < 0.4 + intrinsic:
            return self.pick(['auto', 'min-content', 'max-content', 'fit-content(30)', 'fit-content(50%)',
                              'fit-content(calc(10+20%))'])
        if x < 0.85:
            return self.pick(['1fr', '2fr', '0.5fr', '1.5fr'])
        return 'minmax(%s,%s)' % (self.min_track(), self.max_track())

    def group(self):
        n = self.pick([0, 1, 1, 1, 2])
        return '[' + '.'.join(self.r.sample(NAMES, n)) + ']'

    def template(self):
        """A template and the names it gives lines."""
        parts = []
        named = self.chance(0.35 if self.lean != 'placement' else 0.6)
        used = []
        n = self.r.randint(1, 4)
        auto_done = False
        rep = 0.45 if self.lean == 'repeat' else 0.2
        for i in range(n):
            if named and self.chance(0.6):
                g = self.group()
                parts.append(g)
                used += g[1:-1].split('.') if g != '[]' else []
            if self.chance(rep):
                # A second auto repeat and a count of 0 are invalid: rare.
                auto = (not auto_done or self.chance(0.05)) and self.chance(0.5)
                if auto:
                    auto_done = True
                    count = self.pick(['auto-fill', 'auto-fit'])
                    tracks = [self.fixed_track() if self.chance(0.85) else self.track()
                              for _ in range(self.r.randint(1, 2))]
                else:
                    count = str(self.r.randint(1, 3) if self.chance(0.97) else 0)
                    tracks = [self.track() for _ in range(self.r.randint(1, 3))]
                inner = [count]
                if named and self.chance(0.5):
                    for t in tracks:
                        g = self.group()
                        used += g[1:-1].split('.') if g != '[]' else []
                        inner += [g, t]
                    inner.append(self.group())
                else:
                    inner += tracks
                parts.append('repeat(%s)' % ','.join(inner))
            else:
                parts.append(self.track())
        if named and self.chance(0.5):
            g = self.group()
            parts.append(g)
            used += g[1:-1].split('.') if g != '[]' else []
        return ','.join(parts), [u for u in used if u]

    def areas(self):
        rows = self.r.randint(1, 3)
        cols = self.r.randint(1, 3)
        out = [str(rows), str(cols)]
        names = []
        for name in self.r.sample(AREA_NAMES, self.r.randint(1, 3)):
            rs = self.r.randint(1, rows)
            re_ = self.r.randint(rs + 1, rows + 1)
            cs = self.r.randint(1, cols)
            ce = self.r.randint(cs + 1, cols + 1)
            out.append('%s:%d:%d:%d:%d' % (name, rs, re_, cs, ce))
            names.append(name)
        return ','.join(out), names

    def line_index(self):
        return str(self.pick([1, 1, 2, 2, 3, 4, 5, -1, -1, -2, -3, -5, 0]))

    def placement(self, names):
        x = self.r.random()
        if x < 0.3:
            return 'auto'
        if x < 0.55:
            return self.line_index()
        if x < 0.7:
            return 'span-%d' % self.r.randint(1, 3)
        name = self.pick(names) if names and self.chance(0.85) else self.pick(NAMES + AREA_NAMES)
        if x < 0.9:
            return '%s:%d' % (name, self.pick([1, 1, 1, 2, -1, 0, 3]))
        return 'span-%s:%d' % (name, self.pick([1, 1, 2, 3]))

    def line(self, names):
        return '%s/%s' % (self.placement(names), self.placement(names))

    def align(self, values):
        v = self.pick(values)
        if v not in ('normal', 'stretch', 'baseline') and self.chance(0.2):
            v = self.pick(['safe-', 'unsafe-']) + v
        return v

    def box_props(self, props, item):
        """Size, spacing and other box styles."""
        p = 0.25 if item else 0.3
        if self.chance(p):
            props.append('w=' + self.dimension())
        if self.chance(p):
            props.append('h=' + self.dimension())
        if self.chance(0.1):
            props.append('minw=' + self.lpa())
        if self.chance(0.1):
            props.append('minh=' + self.lpa())
        if self.chance(0.1):
            props.append('maxw=' + self.lpa())
        if self.chance(0.1):
            props.append('maxh=' + self.lpa())
        if self.chance(0.12):
            props.append('ar=' + self.pick(['0.5', '1', '1.5', '2']))
        if self.chance(0.15):
            if self.chance(0.5):
                props.append('m=' + self.pick(['5', '-5', 'auto', '10%', '3']))
            else:
                props.append('m=%s,%s,%s,%s' % tuple(self.pick(['auto', '0', '5', '-5', '5%']) for _ in range(4)))
        if self.chance(0.15):
            props.append('p=' + self.pick(['3', '10', '5%', '2,4,6,8', 'calc(1+2%)']))
        if self.chance(0.12):
            props.append('b=' + self.pick(['1', '2', '1.5,2.5,3,4']))
        if self.chance(0.06):
            props.append('box=content')
        if self.chance(0.08):
            props.append('ox=' + self.pick(['hidden', 'scroll', 'clip']))
        if self.chance(0.08):
            props.append('oy=' + self.pick(['hidden', 'scroll', 'clip']))
        if self.chance(0.05):
            props.append('sw=' + self.pick(['10', '15']))
        if self.chance(0.08):
            props.append('dirn=rtl')
        if self.chance(0.04):
            props.append('contain=' + self.pick(['layout', 'paint', 'content']))

    def item_props(self, props, names):
        place = 0.65 if self.lean == 'placement' else 0.35
        if self.chance(place):
            props.append('col=' + self.line(names))
        if self.chance(place):
            props.append('row=' + self.line(names))
        if self.chance(0.2):
            props.append('as=' + self.align(ITEMS))
        if self.chance(0.2):
            props.append('js=' + self.align(ITEMS))
        absolute = 0.3 if self.lean == 'abs' else 0.07
        if self.chance(absolute):
            props.append('pos=' + self.pick(['absolute', 'absolute', 'absolute', 'fixed']))
            for key in ['il', 'ir', 'it', 'ib']:
                if self.chance(0.35):
                    props.append('%s=%s' % (key, self.lpa()))
        elif self.chance(0.06):
            props.append('pos=relative')
            props.append('il=' + self.lpa())
            props.append('it=' + self.lpa())
        if self.chance(0.04):
            props.append('display=none')
        if self.chance(0.04):
            props.append(self.pick(['replaced=1', 'creplaced=1']))

    def leaf(self, names):
        props = []
        self.box_props(props, True)
        self.item_props(props, names)
        x = self.r.random()
        if x < 0.45:
            props.append('fixed=%s,%s' % (self.pick(['10', '20', '25', '40', '50', '5']),
                                          self.pick(['10', '20', '15', '30', '35'])))
        elif x < 0.85:
            props.append('text=%s,%s' % (self.pick(['40', '75', '120', '250', '400']), self.pick(['10', '12.5'])))
        return '(leaf %s)' % ' '.join(props) if props else '(leaf)'

    def grid(self, depth, outer_names):
        props = []
        names = []
        if self.chance(0.85):
            t, used = self.template()
            props.append('cols=' + t)
            names += used
        if self.chance(0.7):
            t, used = self.template()
            props.append('rows=' + t)
            names += used
        if self.chance(0.3):
            props.append('acols=' + ','.join(self.track() for _ in range(self.r.randint(1, 3))))
        if self.chance(0.35):
            props.append('arows=' + ','.join(self.track() for _ in range(self.r.randint(1, 3))))
        if self.chance(0.4):
            props.append('flow=' + self.pick(['row', 'column', 'row-dense', 'column-dense']))
        if self.chance(0.2 if self.lean != 'placement' else 0.5):
            a, area_names = self.areas()
            props.append('areas=' + a)
            names += area_names + [n + '-start' for n in area_names] + [n + '-end' for n in area_names]
        if self.chance(0.4):
            props.append('gap=' + (self.lp() if self.chance(0.6) else '%s,%s' % (self.lp(), self.lp())))
        if self.chance(0.3):
            props.append('ai=' + self.align(ITEMS))
        if self.chance(0.3):
            props.append('ji=' + self.align(ITEMS))
        if self.chance(0.35):
            props.append('ac=' + self.align(CONTENTS))
        if self.chance(0.35):
            props.append('jc=' + self.align(CONTENTS))
        self.box_props(props, depth > 0)
        if depth > 0:
            self.item_props(props, outer_names)
        children = []
        n = self.r.randint(1, (10 if self.lean == 'placement' else 7) if depth == 0 else 4)
        for _ in range(n):
            if depth < 2 and self.chance(0.15 if depth == 0 else 0.08):
                children.append(self.grid(depth + 1, names))
            else:
                children.append(self.leaf(names))
        return '(grid %s %s)' % (' '.join(props), ' '.join(children))

    def case(self):
        head = []
        sp = self.pick(['800,600', '800,600', '300,200', '123.5,77', 'max,max', 'min,max', '400,max', 'max,250'])
        head.append('avail=' + sp)
        x = self.r.random()
        if x < 0.62:
            pass
        elif x < 0.85 or self.lean == 'abs':
            head.append('mode=direct')
        else:
            head.append(self.pick(['mode=size', 'mode=sizew', 'mode=sizeh']))
        if 'mode=' in ' '.join(head) and self.chance(0.25):
            head.append('known=%s,%s' % (self.pick(['none', '200', '75.5']), self.pick(['none', '150', '40'])))
        if 'mode=' in ' '.join(head) and self.chance(0.2):
            head.append('sizing=content')
        if self.chance(0.2):
            head.append('round')
        return ' '.join(head) + ' ' + self.grid(0, [])


def oracle_lines(cases, path):
    """The oracle's lines for (name, spec) cases, written to path, by name;
    the cases taffy panics on left out."""
    path.write_text(''.join('%s\t%s\n' % c for c in cases))
    got = subprocess.run([str(HERE / 'target' / 'release' / 'grid-oracle'), 'cases', str(path)],
                         capture_output=True, text=True, check=True).stdout
    lines = {}
    current = None
    for line in got.splitlines():
        if line.startswith('== '):
            current = line[3:]
            lines[current] = []
        else:
            lines[current].append(line)
    return lines


def arche_source(cases):
    out = ['// test: run', '// spec: layout', '// stage0: skip', 'import std;', 'import _tree_grid;', '',
           'fn main(sys: &mut std.System) -> !void {', '    let a = sys.alloc;']
    for n, s in cases:
        out.append('    _tree_grid.run(&mut sys.stdout, name: "%s", spec: "%s", a);' % (n, s))
    out.append('}')
    return '\n'.join(out) + '\n'


def soak(seed, count, lean):
    subprocess.run(['cargo', 'build', '--offline', '--release', '--quiet'], cwd=HERE, check=True)
    g = Gen(seed, lean)
    cases = [('S%d' % i, g.case()) for i in range(count)]
    lines = oracle_lines(cases, HERE / 'target' / 'soak-cases.txt')
    cases = [c for c in cases if c[0] in lines]
    src = TESTS / 'grid_soak_tmp.arche'
    exe = HERE / 'target' / 'soak'
    src.write_text(arche_source(cases))
    try:
        subprocess.run([str(TESTS.parent.parent.parent / 'compiler' / 'arche-build'), src.name, str(exe)],
                       cwd=TESTS, check=True)
    finally:
        src.unlink()
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    have = {}
    for line in run.stdout.splitlines():
        name = line.split(' ')[0].split('.')[0].rstrip('r')
        have.setdefault(name, []).append(line)
    bad = 0
    for n, s in cases:
        if have.get(n, []) != lines[n]:
            bad += 1
            print('DIFF %s\t%s' % (n, s))
            want = lines[n]
            got = have.get(n, [])
            for i in range(max(len(want), len(got))):
                w = want[i] if i < len(want) else '<none>'
                h = got[i] if i < len(got) else '<none>'
                if w != h:
                    print('  taffy: ' + w)
                    print('  arche: ' + h)
                    break
    if run.returncode != 0:
        print('the Arche side exited with %d' % run.returncode)
    print('%d cases, %d differ' % (len(cases), bad))


def main():
    if len(sys.argv) > 1 and sys.argv[1] == 'soak':
        soak(int(sys.argv[2]), int(sys.argv[3]), sys.argv[4] if len(sys.argv) > 4 else 'mixed')
        return
    subprocess.run(['cargo', 'build', '--offline', '--release', '--quiet'], cwd=HERE, check=True)
    cases = []  # (file, name, spec)
    number = 1
    for file, seed, count, lean in SUITES:
        g = Gen(seed, lean)
        for _ in range(count):
            cases.append((file, 'G%d' % number, g.case()))
            number += 1
    lines = oracle_lines([(n, s) for _, n, s in cases], HERE / 'cases.txt')
    for file, seed, count, lean in SUITES:
        mine = [(n, s) for f, n, s in cases if f == file and n in lines]
        dropped = [n for f, n, s in cases if f == file and n not in lines]
        out = ['// test: run', '// spec: layout', '// stage0: skip']
        for n, _ in mine:
            out += ['// stdout: ' + l for l in lines[n]]
        out += [
            '// Random grid trees (seed %d, leaning to %s cases): templates with' % (seed, lean),
            '// fixed, fr, auto, min-content, max-content, minmax and fit-content',
            '// tracks, repeat counts, auto-fill and auto-fit, implicit tracks, every',
            '// auto-flow, line, negative, span, named and area placements, gaps,',
            '// every alignment value, percentages and calc, min, max and aspect',
            '// ratio, nested grids and out-of-flow children, so that the port meets',
            '// the combinations no hand-written case thought of. The expected lines',
            '// are taffy 0.15\'s own, from the same cases in Rust: _oracle/grid',
            '// (gen.py made this file).',
            'import std;',
            'import _tree_grid;',
            '',
            'fn main(sys: &mut std.System) -> !void {',
            '    let a = sys.alloc;',
        ]
        for n, s in mine:
            out.append('    _tree_grid.run(&mut sys.stdout, name: "%s", spec: "%s", a);' % (n, s))
        out.append('}')
        (TESTS / (file + '.arche')).write_text('\n'.join(out) + '\n')
        print('%s.arche: %d cases%s' % (file, len(mine), ', taffy panics on ' + ' '.join(dropped) if dropped else ''))


if __name__ == '__main__':
    main()
