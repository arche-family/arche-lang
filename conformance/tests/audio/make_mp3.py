# Writes MP3 files that libmp3lame never makes, for the tests of
# lib/audio/mp3 (make.sh runs it): intensity stereo, MPEG-1's and MPEG-2's,
# alone and with M/S; mixed blocks; CRC-protected frames; dual channel;
# scfsi; pre-emphasis; count1 table B; every Huffman table, linbits too;
# subblock gains; and count1 quadruples that run past part2_3_length.
# The spectra are noise of a fixed seed, coded as the standard says; the
# file means nothing, but every decoder must make the same sound of it.
# Huffman codes come from lib/audio/mp3.arche's tables, so a wrong code
# there shows as a difference with ffmpeg's decoding, not as a broken file.
# Integer arithmetic only, so it writes the same bytes everywhere.
# usage: python3 make_mp3.py <directory>
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, "..", "..", "..", "lib", "audio", "mp3.arche")


class Rand:
    """xorshift64*, so the files do not depend on Python's generator."""

    def __init__(self, seed):
        self.s = seed

    def next(self):
        s = self.s
        s ^= s >> 12
        s ^= (s << 25) & 0xFFFFFFFFFFFFFFFF
        s ^= s >> 27
        self.s = s
        return (s * 0x2545F4914F6CDD1D) & 0xFFFFFFFFFFFFFFFF

    def below(self, n):
        return (self.next() >> 11) % n

    def pick(self, xs):
        return xs[self.below(len(xs))]


class Bits:
    def __init__(self):
        self.bits = []

    def put(self, v, n):
        for i in range(n - 1, -1, -1):
            self.bits.append((v >> i) & 1)

    def extend(self, other):
        self.bits.extend(other.bits)

    def __len__(self):
        return len(self.bits)

    def bytes(self):
        out = bytearray()
        b = self.bits + [0] * (-len(self.bits) % 8)
        for i in range(0, len(b), 8):
            v = 0
            for k in range(8):
                v = (v << 1) | b[i + k]
            out.append(v)
        return bytes(out)


def huffman_tables():
    src = open(LIB).read()
    tables = {}
    for m in re.finditer(r"fn table(\d+)\(\) -> \[(\d+)\]u32 \{\s*return \[(.*?)\];", src, re.S):
        vals = [int(x, 16) for x in re.findall(r"0x[0-9A-F]+", m.group(3))]
        assert len(vals) == int(m.group(2))
        tables[int(m.group(1))] = [(v & 0xFFFFFF, v >> 24) for v in vals]
    return tables


TABLES = huffman_tables()
QUAD_A = list(zip([1, 5, 4, 5, 6, 5, 4, 4, 7, 3, 6, 0, 7, 2, 3, 1],
                  [1, 4, 4, 5, 4, 6, 5, 6, 4, 5, 5, 6, 5, 6, 6, 6]))
LINBITS = [0] * 16 + [1, 2, 3, 4, 6, 8, 10, 13, 4, 5, 6, 7, 8, 9, 11, 13]
BIG = {1: 1, 2: 2, 3: 2, 5: 3, 6: 3, 7: 5, 8: 5, 9: 5, 10: 7, 11: 7, 12: 7, 13: 15, 15: 15}

LONG = [
    [4, 4, 4, 4, 4, 4, 6, 6, 8, 8, 10, 12, 16, 20, 24, 28, 34, 42, 50, 54, 76, 158],
    [4, 4, 4, 4, 4, 4, 6, 6, 6, 8, 10, 12, 16, 18, 22, 28, 34, 40, 46, 54, 54, 192],
    [4, 4, 4, 4, 4, 4, 6, 6, 8, 10, 12, 16, 20, 24, 30, 38, 46, 56, 68, 84, 102, 26],
    [6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54],
    [6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 18, 22, 26, 32, 38, 46, 54, 62, 70, 76, 36],
    [6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54],
    [6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54],
    [6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54],
]
SLEN = [(0, 0), (0, 1), (0, 2), (0, 3), (3, 0), (1, 1), (1, 2), (1, 3),
        (2, 1), (2, 2), (2, 3), (3, 1), (3, 2), (3, 3), (4, 2), (4, 3)]
NSF = [
    [[6, 5, 5, 5], [9, 9, 9, 9], [6, 9, 9, 9]],
    [[6, 5, 7, 3], [9, 9, 12, 6], [6, 9, 12, 6]],
    [[11, 10, 0, 0], [18, 18, 0, 0], [15, 18, 0, 0]],
    [[7, 7, 7, 0], [12, 12, 12, 0], [6, 15, 12, 0]],
    [[6, 6, 6, 3], [12, 9, 9, 6], [6, 12, 9, 6]],
    [[8, 8, 5, 0], [15, 12, 9, 0], [6, 18, 9, 0]],
]


def crc16(bits):
    c = 0xFFFF
    for b in bits:
        top = (c >> 15) & 1
        c = (c << 1) & 0xFFFF
        if top ^ b:
            c ^= 0x8005
    return c


def value(rng, top, sparse):
    """A quantized value up to top: mostly small, sometimes the largest;
    sparse ones mostly zero, as the high lines of music are."""
    r = rng.below(10)
    if r < 3 or (sparse and rng.below(4) != 0):
        return 0
    if r < 8:
        return 1 + rng.below(min(top, 3))
    return rng.below(top + 1)


def pair(out, rng, table, sparse):
    """One pair of big values in table (0 to 31) onto out."""
    if table >= 16:
        lin = LINBITS[table]
        codes = TABLES[16 if table < 24 else 24]
        xs = []
        for _ in range(2):
            v = value(rng, 15, sparse)
            if v == 15:
                v += rng.below(1 << min(lin, 6))
            xs.append(v)
        cx, cy = min(xs[0], 15), min(xs[1], 15)
        code, n = codes[cx * 16 + cy]
        out.put(code, n)
        for v in xs:
            if v >= 15:
                out.put(v - 15, lin)
            if v:
                out.put(rng.below(2), 1)
        return
    top = BIG[table]
    x, y = value(rng, top, sparse), value(rng, top, sparse)
    code, n = TABLES[table][x * (top + 1) + y]
    out.put(code, n)
    for v in (x, y):
        if v:
            out.put(rng.below(2), 1)


def quad(out, rng, table_b):
    q = rng.below(16) if rng.below(3) == 0 else rng.pick([0, 1, 2, 4, 8])
    if table_b:
        out.put(15 - q, 4)
    else:
        code, n = QUAD_A[q]
        out.put(code, n)
    for k in range(4):
        if q & (8 >> k):
            out.put(rng.below(2), 1)


class Granule:
    pass


def granule(rng, lsf, rate, kind, quiet_above, right_is, first, gr):
    """A granule's side information (g) and its main data: scale factors,
    then Huffman codes. kind: 0, 1, 3 (long), 2 (short) or 'm' (mixed).
    quiet_above: the lines from there up are zero (for intensity stereo)."""
    g = Granule()
    g.data = Bits()
    g.block = 2 if kind == "m" else kind
    g.mixed = kind == "m"
    g.global_gain = 155 + rng.below(20)
    g.scfsi = 0
    g.preflag = 0
    if not lsf:
        g.compress = rng.below(16)
        s1, s2 = SLEN[g.compress]
        if g.block == 2:
            n1 = 17 if g.mixed else 18
            for i in range(n1):
                g.data.put(rng.below(1 << s1), s1)
            for i in range(18):
                g.data.put(rng.below(1 << s2), s2)
        else:
            if gr == 1 and first.block != 2:
                g.scfsi = rng.below(16)
            for k, n in enumerate([6, 5, 5, 5]):
                if g.scfsi & (8 >> k):
                    continue
                s = s1 if k < 2 else s2
                for i in range(n):
                    g.data.put(rng.below(1 << s), s)
            g.preflag = rng.below(2)
    else:
        kindex = 0 if g.block != 2 else (2 if g.mixed else 1)
        if right_is:
            sfc = rng.below(256)
            g.compress = sfc * 2 + rng.below(2)
            if sfc < 180:
                slen, row = [sfc // 36, (sfc % 36) // 6, sfc % 6, 0], 3
            elif sfc < 244:
                v = sfc - 180
                slen, row = [v // 16, (v % 16) // 4, v % 4, 0], 4
            else:
                v = sfc - 244
                slen, row = [v // 3, v % 3, 0, 0], 5
        else:
            sfc = rng.below(512)
            g.compress = sfc
            if sfc < 400:
                slen, row = [sfc // 80, (sfc // 16) % 5, (sfc // 4) % 4, sfc % 4], 0
            elif sfc < 500:
                v = sfc - 400
                slen, row = [v // 20, (v // 4) % 5, v % 4, 0], 1
            else:
                v = sfc - 500
                slen, row = [v // 3, v % 3, 0, 0], 2
        for k in range(4):
            for i in range(NSF[row][kindex][k]):
                g.data.put(rng.below(1 << slen[k]), slen[k])
    part2 = len(g.data)

    # Regions and tables.
    lines = min(576, quiet_above)
    g.big_values = rng.below(lines // 2 + 1)
    usable = [1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15] + list(range(16, 32))
    g.tables = [rng.pick(usable), rng.pick(usable), rng.pick(usable)]
    starts = [0]
    for s in LONG[rate]:
        starts.append(starts[-1] + s)
    if g.block != 0:
        g.subblock_gain = [rng.below(8) for _ in range(3)]
        if g.block == 2 or rate <= 2:
            r0 = 36
        else:
            r0 = 54
        r1 = 576
        g.tables[2] = 0
    else:
        g.r0c = rng.below(16)
        g.r1c = rng.below(8)
        r0 = starts[g.r0c + 1]
        r1 = starts[min(g.r0c + g.r1c + 2, 22)]
    bv2 = g.big_values * 2
    r0, r1 = min(r0, bv2), min(r1, bv2)
    s = 0
    for part, stop in enumerate([r0, r1, bv2]):
        while s < stop:
            pair(g.data, rng, g.tables[part], part > 0)
            s += 2
    g.count1_b = rng.below(2)
    room = (min(lines, 572) - s) // 4 if lines > s else 0
    quads = rng.below(room + 1) if room > 0 else 0
    for _ in range(quads):
        quad(g.data, rng, g.count1_b)
    # Sometimes a few bits that begin a quadruple the granule does not
    # finish: a decoder reads past part2_3_length and drops it.
    if quads > 0 and s + 4 * quads <= 568 and rng.below(3) == 0:
        if g.count1_b:
            g.data.put(rng.below(8), 3)
        else:
            g.data.put(1, 2)
    g.part23 = len(g.data)
    g.scalefac_scale = rng.below(2)
    return g


def side_granule(out, g, lsf):
    out.put(g.part23, 12)
    out.put(g.big_values, 9)
    out.put(g.global_gain, 8)
    out.put(g.compress, 9 if lsf else 4)
    if g.block != 0:
        out.put(1, 1)
        out.put(g.block, 2)
        out.put(1 if g.mixed else 0, 1)
        out.put(g.tables[0], 5)
        out.put(g.tables[1], 5)
        for v in g.subblock_gain:
            out.put(v, 3)
    else:
        out.put(0, 1)
        for t in g.tables:
            out.put(t, 5)
        out.put(g.r0c, 4)
        out.put(g.r1c, 3)
    if not lsf:
        out.put(g.preflag, 1)
    out.put(g.scalefac_scale, 1)
    out.put(g.count1_b, 1)


def stream(path, seed, version, sr, kbps_index, mode, crc, frames, kinds):
    """frames frames of version (3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5) at
    sampling rate code sr, each of a random kind of block from kinds."""
    rng = Rand(seed)
    lsf = version != 3
    rate = sr + (0 if version == 3 else 3 if version == 2 else 6)
    hz = [44100, 48000, 32000][sr] // (1 if version == 3 else 2 if version == 2 else 4)
    kbps = ([0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160] if lsf else
            [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320])[kbps_index]
    nch = 1 if mode == 3 else 2
    out = bytearray()
    for f in range(frames):
        size = (72000 if lsf else 144000) * kbps // hz
        ext = rng.below(4) if mode == 1 else 0
        header = Bits()
        header.put(0x7FF, 11)
        header.put(version, 2)
        header.put(1, 2)
        header.put(0 if crc else 1, 1)
        header.put(kbps_index, 4)
        header.put(sr, 2)
        header.put(0, 1)
        header.put(0, 1)
        header.put(mode, 2)
        header.put(ext, 2)
        header.put(0, 4)
        grs = []
        for gr in range(1 if lsf else 2):
            row = []
            kind = rng.pick(kinds)
            for ch in range(nch):
                # The right channel of intensity stereo is quiet above a
                # point, where the left's sound is carried to it.
                quiet = 576
                if ch == 1 and ext & 1:
                    quiet = rng.pick([0, 12, 36, 72, 144, 300])
                first = grs[0][ch] if gr == 1 else None
                k = kind if mode == 1 else rng.pick(kinds)
                row.append(granule(rng, lsf, rate, k, quiet, ch == 1 and ext & 1 and lsf, first, gr))
            grs.append(row)
        side = Bits()
        side.put(0, 8 if lsf else 9)
        side.put(0, (1 if nch == 1 else 2) if lsf else (5 if nch == 1 else 3))
        if not lsf:
            for ch in range(nch):
                side.put(grs[1][ch].scfsi, 4)
        for row in grs:
            for g in row:
                side_granule(side, g, lsf)
        main = Bits()
        for row in grs:
            for g in row:
                main.extend(g.data)
        frame = header.bytes()
        if crc:
            c = crc16(header.bits[16:] + side.bits)
            frame += bytes([c >> 8, c & 0xFF])
        frame += side.bytes() + main.bytes()
        assert len(frame) <= size, (path, f, len(frame), size)
        frame += bytes(size - len(frame))
        out += frame
    with open(path, "wb") as fh:
        fh.write(out)


def main():
    d = sys.argv[1]
    every = [0, 0, 1, 2, 3, "m"]
    # MPEG-1, 48 kHz, joint stereo of every mode extension, CRCs.
    stream(os.path.join(d, "hand-joint-48k.mp3"), 11, 3, 1, 14, 1, True, 24, every)
    # MPEG-1, 32 kHz, dual channel: each channel its own blocks.
    stream(os.path.join(d, "hand-dual-32k.mp3"), 12, 3, 2, 14, 2, False, 12, every)
    # MPEG-2, 24 kHz, joint stereo: MPEG-2's intensity stereo.
    stream(os.path.join(d, "hand-joint-24k.mp3"), 13, 2, 1, 14, 1, False, 24, every)
    # MPEG-2.5, 11.025 kHz, joint stereo.
    stream(os.path.join(d, "hand-joint-11k.mp3"), 14, 0, 0, 14, 1, True, 16, every)
    # MPEG-1, 44.1 kHz, mono.
    stream(os.path.join(d, "hand-mono-44k.mp3"), 15, 3, 0, 14, 3, False, 12, every)


main()
