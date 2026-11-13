# Writes FLAC files that ffmpeg's encoder never makes, for the tests of
# lib/audio/flac (make.sh runs it): depths of 8, 12, 20 and 32 bits, a
# variable block size with every kind of block size and rate code, the
# three stereo decorrelations (32-bit sides of 33 bits too), LPC of many
# orders and precisions, escaped Rice partitions, wasted bits, constant
# subframes, metadata blocks of every kind to pass over, and an ID3v2 tag
# in front. Its signals are integer arithmetic only, so it writes the same
# bytes everywhere. usage: python3 make_flac.py <directory>
import os
import sys


class Bits:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, v, n):
        for i in range(n - 1, -1, -1):
            self.acc = (self.acc << 1) | ((v >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = 0
                self.n = 0

    def signed(self, v, n):
        self.put(v & ((1 << n) - 1), n)

    def unary(self, q):
        for _ in range(q):
            self.put(0, 1)
        self.put(1, 1)

    def align(self):
        while self.n != 0:
            self.put(0, 1)


def crc8(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def crc16(data):
    c = 0
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x8005) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def utf8_number(v):
    if v < 0x80:
        return bytes([v])
    n = 2
    while v >= 1 << (5 * n + 1):
        n += 1
    out = []
    for _ in range(n - 1):
        out.append(0x80 | (v & 0x3F))
        v >>= 6
    lead = (0xFF00 >> n) & 0xFF
    out.append(lead | v)
    return bytes(reversed(out))


def rice_cost(vals, k):
    return sum(((((v << 1) ^ (v >> 63)) & ((1 << 64) - 1)) >> k) + 1 + k for v in vals)


def zig(v):
    return (v << 1) if v >= 0 else ((-v) << 1) - 1


def residual(w, res, order, n, porder, method, escape_parts):
    w.put(method, 2)
    w.put(porder, 4)
    pbits = 4 + method
    esc = (1 << pbits) - 1
    parts = 1 << porder
    per = n >> porder
    i = 0
    for p in range(parts):
        cnt = per - order if p == 0 else per
        vals = res[i:i + cnt]
        i += cnt
        if p in escape_parts:
            w.put(esc, pbits)
            need = 0
            for v in vals:
                while not (-(1 << (need - 1)) <= v < (1 << (need - 1)) if need > 0 else v == 0):
                    need += 1
            w.put(need, 5)
            for v in vals:
                if need:
                    w.signed(v, need)
            continue
        best = min(range(esc), key=lambda k: sum((zig(v) >> k) + 1 + k for v in vals))
        w.put(best, pbits)
        for v in vals:
            z = zig(v)
            w.unary(z >> best)
            w.put(z & ((1 << best) - 1), best)


def subframe(w, x, bps, kind, wasted=0, porder=0, method=0, escape_parts=(), coefs=None, shift=0, precision=0):
    n = len(x)
    w.put(0, 1)
    if kind == "constant":
        w.put(0, 6)
    elif kind == "verbatim":
        w.put(1, 6)
    elif kind.startswith("fixed"):
        w.put(8 + int(kind[5:]), 6)
    else:
        w.put(31 + len(coefs), 6)
    if wasted:
        w.put(1, 1)
        w.unary(wasted - 1)
        x = [v >> wasted for v in x]
    else:
        w.put(0, 1)
    b = bps - wasted
    if kind == "constant":
        w.signed(x[0], b)
        return
    if kind == "verbatim":
        for v in x:
            w.signed(v, b)
        return
    if kind.startswith("fixed"):
        order = int(kind[5:])
        for v in x[:order]:
            w.signed(v, b)
        res = []
        for i in range(order, n):
            p = [0, x[i - 1], 2 * x[i - 1] - x[i - 2],
                 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3] if order >= 3 else 0,
                 4 * x[i - 1] - 6 * x[i - 2] + 4 * x[i - 3] - x[i - 4] if order >= 4 else 0][order]
            res.append(x[i] - p)
        residual(w, res, order, n, porder, method, escape_parts)
        return
    order = len(coefs)
    for v in x[:order]:
        w.signed(v, b)
    w.put(precision - 1, 4)
    w.signed(shift, 5)
    for c in coefs:
        w.signed(c, precision)
    res = []
    for i in range(order, n):
        s = sum(coefs[j] * x[i - 1 - j] for j in range(order))
        res.append(x[i] - (s >> shift))
    residual(w, res, order, n, porder, method, escape_parts)


def frame(number, variable, size, size_code, rate_code, rate_extra, assignment, depth_code, subs):
    w = Bits()
    w.put(0xFFF8 | variable, 16)
    w.put(size_code, 4)
    w.put(rate_code, 4)
    w.put(assignment, 4)
    w.put(depth_code, 3)
    w.put(0, 1)
    for b in utf8_number(number):
        w.put(b, 8)
    if size_code == 6:
        w.put(size - 1, 8)
    elif size_code == 7:
        w.put(size - 1, 16)
    if rate_code == 12:
        w.put(rate_extra, 8)
    elif rate_code in (13, 14):
        w.put(rate_extra, 16)
    w.put(crc8(w.out), 8)
    for s in subs:
        s(w)
    w.align()
    w.put(crc16(w.out), 16)
    return bytes(w.out)


def streaminfo(min_block, max_block, rate, channels, bps, total, last=False):
    w = Bits()
    w.put(min_block, 16)
    w.put(max_block, 16)
    w.put(0, 24)
    w.put(0, 24)
    w.put(rate, 20)
    w.put(channels - 1, 3)
    w.put(bps - 1, 5)
    w.put(total, 36)
    w.put(0, 128)
    return block(0, bytes(w.out), last)


def block(kind, body, last=False):
    return bytes([(0x80 if last else 0) | kind]) + len(body).to_bytes(3, "big") + body


class Noise:
    def __init__(self, seed):
        self.s = seed

    def next(self, bits):
        self.s = (self.s * 6364136223846793005 + 1442695040888963407) & ((1 << 64) - 1)
        return (self.s >> (64 - bits)) - (1 << (bits - 1))


def tri(i, period, amp):
    p = i % period
    v = 4 * p * amp // period
    if p < period // 4:
        return v
    if p < 3 * period // 4:
        return 2 * amp - v
    return v - 4 * amp


def signal(n, bps, period, seed, start=0):
    amp = (1 << (bps - 1)) * 6 // 10
    rnd = Noise(seed)
    return [max(-(1 << (bps - 1)), min((1 << (bps - 1)) - 1, tri(start + i, period, amp) + rnd.next(max(2, bps * 2 // 3 - 4))))
            for i in range(n)]


def stereo_subs(left, right, bps, assignment, plan):
    if assignment == 8:
        chans = [(left, bps), ([l - r for l, r in zip(left, right)], bps + 1)]
    elif assignment == 9:
        chans = [([l - r for l, r in zip(left, right)], bps + 1), (right, bps)]
    elif assignment == 10:
        chans = [([(l + r) >> 1 for l, r in zip(left, right)], bps), ([l - r for l, r in zip(left, right)], bps + 1)]
    else:
        chans = [(left, bps), (right, bps)]
    return [(lambda w, c=c, d=d, k=k: subframe(w, c, d, **k)) for (c, d), k in zip(chans, plan)]


def eight_bit_stereo():
    bps, rate = 8, 32000
    out = b"fLaC" + streaminfo(16, 4096, rate, 2, bps, 0)
    out += block(4, b"\x08\x00\x00\x00arche-oc\x00\x00\x00\x00")
    out += block(1, bytes(100))
    out += block(2, b"arch" + bytes(20))
    out += block(3, bytes(18 * 2), last=True)
    frames = []
    sizes = [(4096, 12), (1152, 3), (192, 1), (200, 6), (16, 6), (2000, 7)]
    plans = [
        (8, [dict(kind="fixed2", porder=3), dict(kind="fixed1", porder=2)]),
        (9, [dict(kind="verbatim"), dict(kind="fixed3", porder=1)]),
        (10, [dict(kind="lpc", coefs=[3, -2], shift=1, precision=3, porder=2),
              dict(kind="lpc", coefs=[100, -50, 10, 3, -1, 0, 1, -1], shift=6, precision=8, porder=0)]),
        (1, [dict(kind="fixed4", porder=0), dict(kind="constant")]),
        (10, [dict(kind="verbatim"), dict(kind="fixed0", porder=1)]),
        (8, [dict(kind="lpc", coefs=[1] * 12, shift=3, precision=2, porder=4), dict(kind="fixed2", porder=4)]),
    ]
    at = 0
    for f, ((size, code), (assignment, plan)) in enumerate(zip(sizes, plans)):
        left = signal(size, bps, 37, 11 + f, at)
        right = signal(size, bps, 53, 29 + f, at)
        if assignment == 1:
            right = [right[0]] * size
        frames.append(frame(at, 1, size, code, 0, 0, assignment, 1, stereo_subs(left, right, bps, assignment, plan)))
        at += size
    return out + b"".join(frames)


def twelve_bit_variable():
    bps, rate = 12, 22050
    out = b"ID3\x03\x00\x00\x00\x00\x00\x0aTIT2\x00\x00\x00\x00\x00\x00"
    out += b"fLaC" + streaminfo(16, 32768, rate, 1, bps, 0)
    out += block(4, b"\x05\x00\x00\x00arche\x00\x00\x00\x00", last=True)
    codes = [(192, 1), (576, 2), (1152, 3), (2304, 4), (4608, 5), (17, 6), (40000 // 4, 7),
             (256, 8), (512, 9), (1024, 10), (2048, 11), (4096, 12), (8192, 13), (16384, 14), (32768, 15)]
    rates = [(0, 0), (6, 0), (13, 22050), (14, 2205)]
    kinds = ["fixed2", "lpc", "verbatim", "constant", "fixed0", "fixed1", "fixed3", "fixed4"]
    at = 0
    frames = []
    for f, (size, code) in enumerate(codes):
        x = signal(size, bps, 101 + 7 * f, 5 + f, at)
        kind = kinds[f % len(kinds)]
        if kind == "constant":
            x = [x[0]] * size
        args = dict(kind=kind, porder=min(f % 6, 15))
        if kind == "lpc":
            order = 1 + (f * 11) % 32
            shift = 4 + f % 8
            args["coefs"] = ([2 << shift, -(1 << shift)] + [((j * 7919 + f) % 7) - 3 for j in range(order)])[:order]
            if order == 1:
                args["coefs"] = [1 << shift]
            args["shift"] = shift
            args["precision"] = 15
        while size % (1 << args.get("porder", 0)) or (size >> args.get("porder", 0)) < len(args.get("coefs", [])) + 4:
            args["porder"] -= 1
        rc, extra = rates[f % len(rates)]
        frames.append(frame(at, 1, size, code, rc, extra, 0, 0,
                            [lambda w, x=x, args=args: subframe(w, x, bps, **args)]))
        at += size
    return out + b"".join(frames)


def twenty_bit_escaped():
    bps, rate = 20, 44100
    out = b"fLaC" + streaminfo(1024, 1024, rate, 3, bps, 0, last=True)
    frames = []
    for f in range(4):
        size = 1024
        chans = []
        for c in range(3):
            x = signal(size, bps, 64 + 13 * c, 100 + 3 * f + c, f * size)
            if c == 1:
                # Wasted bits: every sample a multiple of 2^5.
                x = [(v >> 5) << 5 for v in x]
                args = dict(kind="fixed2", wasted=5, porder=4, escape_parts=(1, 5, 15))
            elif c == 2 and f == 2:
                x = [0] * size
                args = dict(kind="fixed1", porder=3, escape_parts=(0, 1, 2, 3, 4, 5, 6, 7))
            elif c == 2:
                args = dict(kind="lpc", coefs=[2048, -1024, 512, -256], shift=10, precision=13,
                            porder=8, method=1, escape_parts=(0, 200, 255))
            else:
                args = dict(kind="fixed3", porder=2, method=1, escape_parts=(2,))
            chans.append(lambda w, x=x, args=args: subframe(w, x, bps, **args))
        frames.append(frame(f, 0, size, 10, 9, 0, 2, 5, chans))
    return out + b"".join(frames)


def thirty_two_bit_sides():
    bps, rate = 32, 48000
    out = b"fLaC" + streaminfo(16, 4608, rate, 2, bps, 0, last=True)
    frames = []
    at = 0
    for f, assignment in enumerate([8, 9, 10, 1]):
        size = 576
        left = signal(size, bps, 97, 40 + f, at)
        right = [-v - 1 if f % 2 else v // 3 for v in signal(size, bps, 41, 50 + f, at)]
        plan = [dict(kind="verbatim"), dict(kind="fixed1", porder=2, method=1)]
        if f == 2:
            plan = [dict(kind="fixed2", porder=1, method=1), dict(kind="verbatim")]
        frames.append(frame(f, 0, size, 2, 10, 0, assignment, 7, stereo_subs(left, right, bps, assignment, plan)))
        at += size
    return out + b"".join(frames)


def main():
    d = sys.argv[1]
    for name, data in [("odd-8bit-stereo", eight_bit_stereo()), ("odd-12bit-variable", twelve_bit_variable()),
                       ("odd-20bit-escaped", twenty_bit_escaped()), ("odd-32bit-sides", thirty_two_bit_sides())]:
        with open(os.path.join(d, name + ".flac"), "wb") as f:
            f.write(data)


main()
