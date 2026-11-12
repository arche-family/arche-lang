"""Writes the JPEG files of conformance/tests/image/jpeg.arche into the
directory given (make.sh calls it), and prints the line the test expects
for each: its name, its size, what its header says, and the FNV-1a hash of
its pixels as Pillow decodes them (libjpeg-turbo: jpeg_idct_islow, fancy
upsampling, its YCbCr tables), as RGBA.

The images are synthesized: gradients, discs of saturated colour, lines one
pixel wide and seeded noise, so that every block has edges and texture.
Pillow writes most files; ffmpeg's MJPEG encoder one frame; write_jpeg
below, a small baseline encoder, the samplings neither of them writes (4:4:0,
4:1:1, factors of 3 and 4) and restart intervals of any length.
"""
import io
import math
import os
import random
import subprocess
import sys

from PIL import Image, ImageDraw

out = sys.argv[1]


def synth(w, h, seed):
    """A test picture of w x h pixels, the same for the same seed."""
    g = random.Random(seed)
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = (x * 255 // max(w - 1, 1), y * 255 // max(h - 1, 1), ((x + y) * 6) % 256)
    d = ImageDraw.Draw(im)
    for _ in range(4):
        cx, cy, r = g.randrange(w), g.randrange(h), g.randrange(3, max(4, min(w, h) // 2))
        colour = (g.choice([0, 255, g.randrange(256)]), g.choice([0, 255, g.randrange(256)]), g.choice([0, 255, g.randrange(256)]))
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=colour)
    for _ in range(3):
        d.line([g.randrange(w), g.randrange(h), g.randrange(w), g.randrange(h)], fill=(255, 255, 255), width=1)
    for y in range(h // 2, h):
        for x in range(w // 2):
            r, gg, b = px[x, y]
            n = g.randrange(-24, 25)
            px[x, y] = (min(255, max(0, r + n)), min(255, max(0, gg - n)), min(255, max(0, b + n)))
    return im


def fnv(data):
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def save(name, im, **opts):
    im.save(os.path.join(out, name), "JPEG", **opts)


def write(name, data):
    with open(os.path.join(out, name), "wb") as f:
        f.write(data)


def segments(b):
    """The markers of a JPEG up to its first scan: (marker, start, end)."""
    i = 2
    found = []
    while i < len(b):
        m = b[i + 1]
        n = b[i + 2] * 256 + b[i + 3]
        found.append((m, i, i + 2 + n))
        if m == 0xDA:
            break
        i += 2 + n
    return found


# ---- a small baseline encoder ------------------------------------------------

ZIGZAG = [
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
]
LUMA_Q = [
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99,
]
CHROMA_Q = [17, 18, 24, 47] + [99] * 4 + [18, 21, 26, 66] + [99] * 4 + [24, 26, 56] + [99] * 5 + [47, 66] + [99] * 38
DC_LUMA = ([0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0], list(range(12)))
DC_CHROMA = ([0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0], list(range(12)))
AC_LUMA = ([0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D], [
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
    0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA,
])
AC_CHROMA = ([0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77], [
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
    0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
    0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5,
    0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA,
])
COS = [[math.cos((2 * x + 1) * u * math.pi / 16) for x in range(8)] for u in range(8)]


def codes(spec):
    counts, symbols = spec
    table = {}
    code = 0
    k = 0
    for length in range(1, 17):
        for _ in range(counts[length - 1]):
            table[symbols[k]] = (code, length)
            code += 1
            k += 1
        code <<= 1
    return table


class Bits:
    def __init__(self):
        self.data = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, value, n):
        self.acc = (self.acc << n) | (value & ((1 << n) - 1))
        self.n += n
        while self.n >= 8:
            self.n -= 8
            b = (self.acc >> self.n) & 255
            self.data.append(b)
            if b == 255:
                self.data.append(0)
        self.acc &= (1 << self.n) - 1

    def flush(self):
        if self.n:
            self.put(0x7F, 8 - self.n)


def fdct(block):
    """The DCT of 8x8 samples less 128, in floating point."""
    res = [0.0] * 64
    for v in range(8):
        for u in range(8):
            s = 0.0
            for y in range(8):
                cy = COS[v][y]
                row = y * 8
                for x in range(8):
                    s += block[row + x] * COS[u][x] * cy
            cu = 1 / math.sqrt(2) if u == 0 else 1
            cv = 1 / math.sqrt(2) if v == 0 else 1
            res[v * 8 + u] = s * cu * cv / 4
    return res


def write_jpeg(im, factors, restart=0, quality=75, grey=False):
    """A baseline JPEG of im, its components sampled at factors ((h, v)
    each), every restart MCUs a restart marker, written as libjpeg would
    but for the floating-point DCT and the plain downsampling (a mean)."""
    w, h = im.size
    planes = [im.convert("L")] if grey else list(im.convert("YCbCr").split())
    hmax = max(f[0] for f in factors)
    vmax = max(f[1] for f in factors)
    scale = 5000 // quality if quality < 50 else 200 - 2 * quality
    qs = [[min(255, max(1, (q * scale + 50) // 100)) for q in t] for t in (LUMA_Q, CHROMA_Q)]
    huff = [(codes(DC_LUMA), codes(AC_LUMA)), (codes(DC_CHROMA), codes(AC_CHROMA))]
    mcux = -(-w // (8 * hmax))
    mcuy = -(-h // (8 * vmax))
    comps = []
    for i, (fh, fv) in enumerate(factors):
        cw = -(-w * fh // hmax)
        ch = -(-h * fv // vmax)
        src = planes[i].load()
        sx, sy = hmax // fh, vmax // fv
        bw, bh = mcux * fh * 8, mcuy * fv * 8
        samples = []
        for y in range(bh):
            for x in range(bw):
                cx, cy = min(x, cw - 1), min(y, ch - 1)
                total = 0
                for j in range(sy):
                    for i2 in range(sx):
                        total += src[min(cx * sx + i2, w - 1), min(cy * sy + j, h - 1)]
                samples.append((total + sx * sy // 2) // (sx * sy))
        comps.append((fh, fv, bw, samples, min(i, 1)))
    body = bytearray()
    body += b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
    for t in range(1 if grey else 2):
        body += bytes([0xFF, 0xDB, 0, 67, t]) + bytes(qs[t][ZIGZAG[k]] for k in range(64))
    n = len(factors)
    body += bytes([0xFF, 0xC0, 0, 8 + 3 * n, 8, h >> 8, h & 255, w >> 8, w & 255, n])
    for i, (fh, fv) in enumerate(factors):
        body += bytes([i + 1, (fh << 4) | fv, min(i, 1)])
    for t in range(1 if grey else 2):
        for cls, spec in ((0, (DC_LUMA, DC_CHROMA)[t]), (1, (AC_LUMA, AC_CHROMA)[t])):
            counts, symbols = spec
            body += bytes([0xFF, 0xC4, 0, 19 + len(symbols), (cls << 4) | t]) + bytes(counts) + bytes(symbols)
    if restart:
        body += bytes([0xFF, 0xDD, 0, 4, restart >> 8, restart & 255])
    body += bytes([0xFF, 0xDA, 0, 6 + 2 * n, n])
    for i in range(n):
        t = min(i, 1)
        body += bytes([i + 1, (t << 4) | t])
    body += bytes([0, 63, 0])
    bits = Bits()
    preds = [0] * n
    count = 0
    marker = 0
    for my in range(mcuy):
        for mx in range(mcux):
            if restart and count == restart:
                bits.flush()
                bits.data += bytes([0xFF, 0xD0 + marker])
                marker = (marker + 1) % 8
                count = 0
                preds = [0] * n
            count += 1
            for ci, (fh, fv, bw, samples, t) in enumerate(comps):
                dc, ac = huff[t]
                for j in range(fv):
                    for i in range(fh):
                        x0, y0 = (mx * fh + i) * 8, (my * fv + j) * 8
                        blk = [samples[(y0 + y) * bw + x0 + x] - 128 for y in range(8) for x in range(8)]
                        coefs = fdct(blk)
                        q = [int(round(coefs[k] / qs[t][k])) for k in range(64)]
                        diff = q[0] - preds[ci]
                        preds[ci] = q[0]
                        size = abs(diff).bit_length()
                        bits.put(*dc[size])
                        bits.put(diff if diff >= 0 else diff - 1, size)
                        run = 0
                        for k in range(1, 64):
                            v = q[ZIGZAG[k]]
                            if v == 0:
                                run += 1
                                continue
                            while run > 15:
                                bits.put(*ac[0xF0])
                                run -= 16
                            size = abs(v).bit_length()
                            bits.put(*ac[(run << 4) | size])
                            bits.put(v if v >= 0 else v - 1, size)
                            run = 0
                        if run:
                            bits.put(*ac[0])
    bits.flush()
    return bytes(body) + bytes(bits.data) + b"\xff\xd9"


# ---- the files ---------------------------------------------------------------

files = []


def made(name, info=""):
    files.append((name, info))


save("q75-420.jpg", synth(64, 48, 1), quality=75)
made("q75-420.jpg")
save("q75-420-odd.jpg", synth(35, 27, 2), quality=75)
made("q75-420-odd.jpg")
save("q50-422-odd.jpg", synth(45, 31, 3), quality=50, subsampling=1)
made("q50-422-odd.jpg")
save("q90-444-odd.jpg", synth(37, 29, 4), quality=90, subsampling=0)
made("q90-444-odd.jpg")
save("q100-444.jpg", synth(24, 16, 5), quality=100, subsampling=0)
made("q100-444.jpg")
save("q5-420.jpg", synth(40, 24, 6), quality=5)
made("q5-420.jpg")
save("grey.jpg", synth(41, 23, 7).convert("L"), quality=80)
made("grey.jpg")
save("tiny-1x1.jpg", synth(1, 1, 8), quality=75)
made("tiny-1x1.jpg")
save("tiny-3x3.jpg", synth(3, 3, 9), quality=75)
made("tiny-3x3.jpg")
save("tiny-4x9-422.jpg", synth(4, 9, 10), quality=75, subsampling=1)
made("tiny-4x9-422.jpg")
save("optimized.jpg", synth(48, 32, 11), quality=85, optimize=True)
made("optimized.jpg")
save("restart-blocks.jpg", synth(56, 40, 12), quality=75, restart_marker_blocks=3)
made("restart-blocks.jpg")
save("restart-rows.jpg", synth(40, 40, 13), quality=75, subsampling=0, restart_marker_rows=1)
made("restart-rows.jpg")
save("rgb-adobe.jpg", synth(32, 24, 14), quality=90, subsampling=0, keep_rgb=True)
made("rgb-adobe.jpg")
save("sof1-16bit.jpg", synth(32, 24, 15), qtables=[[300 + k for k in range(64)], [2 + k for k in range(64)]])
made("sof1-16bit.jpg")
save("comment-icc.jpg", synth(24, 24, 16), quality=70, comment=b"a comment", icc_profile=b"\x00" * 300)
made("comment-icc.jpg")
ex = Image.Exif()
ex[0x0112] = 6
ex[0x010F] = "Arche"
save("exif-6.jpg", synth(30, 20, 17), quality=75, exif=ex.tobytes())
made("exif-6.jpg", "o6")
# Exif, big-endian, orientation 8: one IFD entry, SHORT 8.
save("exif-8-mm.jpg", synth(20, 30, 18), quality=75,
     exif=b"Exif\x00\x00MM\x00\x2a\x00\x00\x00\x08\x00\x01\x01\x12\x00\x03\x00\x00\x00\x01\x00\x08\x00\x00\x00\x00\x00\x00")
made("exif-8-mm.jpg", "o8")
save("progressive-420.jpg", synth(51, 37, 19), quality=75, progressive=True)
made("progressive-420.jpg", "p")
save("progressive-444.jpg", synth(33, 25, 20), quality=90, subsampling=0, progressive=True)
made("progressive-444.jpg", "p")
save("progressive-422-restart.jpg", synth(48, 32, 21), quality=60, subsampling=1, progressive=True, restart_marker_blocks=2)
made("progressive-422-restart.jpg", "p")
save("progressive-grey.jpg", synth(29, 35, 22).convert("L"), quality=75, progressive=True)
made("progressive-grey.jpg", "p")
save("progressive-optimized.jpg", synth(40, 30, 23), quality=80, progressive=True, optimize=True)
made("progressive-optimized.jpg", "p")

# SOF1 with 8-bit tables: the baseline file, its frame marker changed.
b = bytearray(open(os.path.join(out, "q75-420.jpg"), "rb").read())
for m, s, e in segments(b):
    if m == 0xC0:
        b[s + 1] = 0xC1
write("sof1.jpg", b)
made("sof1.jpg")

# ffmpeg's MJPEG, a frame as a camera's AVI holds it (4:2:2, standard
# tables), and the same frame without its Huffman tables, as Motion-JPEG
# leaves them out (AVI1).
subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                "testsrc2=s=48x32:r=1:d=1", "-frames:v", "1", "-c:v", "mjpeg", "-pix_fmt", "yuvj422p",
                "-huffman", "default", "-q:v", "4", "-flags", "+bitexact", "-fflags", "+bitexact",
                "-f", "mjpeg", os.path.join(out, "ffmpeg-422.jpg")], check=True)
made("ffmpeg-422.jpg")
b = open(os.path.join(out, "ffmpeg-422.jpg"), "rb").read()
cut = bytearray()
last = 0
for m, s, e in segments(b):
    if m == 0xC4:
        cut += b[last:s]
        last = e
cut += b[last:]
write("mjpeg-no-dht.jpg", cut)
made("mjpeg-no-dht.jpg")

# What neither writes: 4:4:0, 4:1:1, factors of 3 and 4, a grey image
# sampled 2x2, restart intervals of one MCU and of five.
write("w-440.jpg", write_jpeg(synth(37, 29, 24), [(1, 2), (1, 1), (1, 1)]))
made("w-440.jpg")
write("w-411.jpg", write_jpeg(synth(45, 21, 25), [(4, 1), (1, 1), (1, 1)]))
made("w-411.jpg")
write("w-310.jpg", write_jpeg(synth(31, 19, 26), [(3, 1), (1, 1), (3, 1)]))
made("w-310.jpg")
write("w-grey-22.jpg", write_jpeg(synth(27, 18, 27), [(2, 2)], grey=True))
made("w-grey-22.jpg")
write("w-420-restart-1.jpg", write_jpeg(synth(40, 24, 28), [(2, 2), (1, 1), (1, 1)], restart=1))
made("w-420-restart-1.jpg")
write("w-444-restart-5.jpg", write_jpeg(synth(43, 17, 29), [(1, 1), (1, 1), (1, 1)], restart=5, quality=95))
made("w-444-restart-5.jpg")

print("# jpeg.arche")
for name, info in files:
    im = Image.open(os.path.join(out, name))
    im.load()
    rgba = im.convert("RGBA")
    extra = (" " + info) if info else ""
    print(f"// stdout: {name} {im.size[0]}x{im.size[1]}{extra} {fnv(rgba.tobytes())}")


# ---- what jpeg_encode.arche expects ------------------------------------------

def pattern(w, h):
    """jpeg_encode.arche's picture: red and green ramps across and down with
    a little texture, blue falling off from the middle, and a red disc with
    a sharp edge there."""
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            d2 = abs(x - w // 2) ** 2 + abs(y - h // 2) ** 2
            if 16 * d2 < h * h:
                px[x, y] = (230, 40, 60)
            else:
                px[x, y] = (x * 255 // w, min(255, y * 255 // h + (x * 7 + y * 13) % 9),
                            255 - min(255, d2 * 255 // ((w * w + h * h) // 4 + 1)))
    return im


def psnr(a, b):
    """PSNR over R, G and B, in hundredths of a dB, rounded."""
    sse = sum((p - q) ** 2 for p, q in zip(a.tobytes(), b.tobytes()))
    n = len(a.tobytes())
    return int(10 * math.log10(255 * 255 * n / sse) * 100 + 0.5)


print("# jpeg_encode.arche")
names = {0: "4:4:4", 1: "4:2:2", 2: "4:2:0"}
for w, h, q, sub in [(64, 48, 75, 2), (37, 29, 90, 0), (45, 31, 50, 1), (35, 27, 100, 2), (1, 1, 1, 2),
                     (17, 9, 20, 2), (320, 180, 85, 2), (640, 360, 75, 2), (640, 360, 75, 0)]:
    im = pattern(w, h)
    f = io.BytesIO()
    im.save(f, "JPEG", quality=q, subsampling=sub)
    data = f.getvalue()
    back = Image.open(io.BytesIO(data)).convert("RGB")
    p = psnr(im, back)
    print(f"// stdout: {w}x{h} q{q} {names[sub]}: {len(data)} bytes, hash {fnv(data)}, PSNR {p // 100}.{p % 100:02d} dB")

print("# jpeg_damaged.arche")
b = open(os.path.join(out, "q75-420.jpg"), "rb").read()[:900] + b"\xff\xd9"
im = Image.open(io.BytesIO(b))
im.load()
print(f"// stdout: cut in the scan: 64x48 {fnv(im.convert('RGBA').tobytes())}")
