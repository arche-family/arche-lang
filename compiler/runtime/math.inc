// The C library's float functions that LLVM calls for operations the
// processor has no instruction for (spec/arche.md §2.2): floor, ceil and
// trunc without SSE4.1, the remainder (frem), and fused multiply-add
// without FMA. There is no C library, so the runtimes have their own;
// rt.c and freestanding.c include this file. fmod, fmodf and trunc follow
// musl (MIT licence); fma is exact, by 128-bit integers.

typedef union {
    double f;
    u64 i;
} F64Bits;

typedef union {
    float f;
    u32 i;
} F32Bits;

double trunc(double x) {
    F64Bits u = {x};
    int e = (int)(u.i >> 52 & 0x7ff) - 0x3ff + 12;
    if (e >= 52 + 12) {
        return x;
    }
    if (e < 12) {
        e = 1;
    }
    u64 m = ~(u64)0 >> e;
    if ((u.i & m) == 0) {
        return x;
    }
    u.i &= ~m;
    return u.f;
}

float truncf(float x) {
    F32Bits u = {x};
    int e = (int)(u.i >> 23 & 0xff) - 0x7f + 9;
    if (e >= 23 + 9) {
        return x;
    }
    if (e < 9) {
        e = 1;
    }
    u32 m = ~(u32)0 >> e;
    if ((u.i & m) == 0) {
        return x;
    }
    u.i &= ~m;
    return u.f;
}

double floor(double x) {
    double t = trunc(x);
    return t > x ? t - 1.0 : t;
}

double ceil(double x) {
    double t = trunc(x);
    return t < x ? t + 1.0 : t;
}

float floorf(float x) {
    float t = truncf(x);
    return t > x ? t - 1.0f : t;
}

float ceilf(float x) {
    float t = truncf(x);
    return t < x ? t + 1.0f : t;
}

double fmod(double x, double y) {
    F64Bits ux = {x}, uy = {y};
    int ex = (int)(ux.i >> 52 & 0x7ff);
    int ey = (int)(uy.i >> 52 & 0x7ff);
    u64 sx = ux.i >> 63;
    u64 i;
    u64 uxi = ux.i;
    if (uy.i << 1 == 0 || (uy.i << 1) > 0xffe0000000000000ULL || ex == 0x7ff) {
        return (x * y) / (x * y);
    }
    if (uxi << 1 <= uy.i << 1) {
        if (uxi << 1 == uy.i << 1) {
            return 0 * x;
        }
        return x;
    }
    if (!ex) {
        for (i = uxi << 12; i >> 63 == 0; ex--, i <<= 1) {
        }
        uxi <<= -ex + 1;
    } else {
        uxi &= ~(u64)0 >> 12;
        uxi |= (u64)1 << 52;
    }
    if (!ey) {
        for (i = uy.i << 12; i >> 63 == 0; ey--, i <<= 1) {
        }
        uy.i <<= -ey + 1;
    } else {
        uy.i &= ~(u64)0 >> 12;
        uy.i |= (u64)1 << 52;
    }
    for (; ex > ey; ex--) {
        i = uxi - uy.i;
        if (i >> 63 == 0) {
            if (i == 0) {
                return 0 * x;
            }
            uxi = i;
        }
        uxi <<= 1;
    }
    i = uxi - uy.i;
    if (i >> 63 == 0) {
        if (i == 0) {
            return 0 * x;
        }
        uxi = i;
    }
    for (; uxi >> 52 == 0; uxi <<= 1, ex--) {
    }
    if (ex > 0) {
        uxi -= (u64)1 << 52;
        uxi |= (u64)ex << 52;
    } else {
        uxi >>= -ex + 1;
    }
    uxi |= sx << 63;
    ux.i = uxi;
    return ux.f;
}

float fmodf(float x, float y) {
    F32Bits ux = {x}, uy = {y};
    int ex = (int)(ux.i >> 23 & 0xff);
    int ey = (int)(uy.i >> 23 & 0xff);
    u32 sx = ux.i & 0x80000000;
    u32 i;
    u32 uxi = ux.i;
    if (uy.i << 1 == 0 || (uy.i << 1) > 0xff000000 || ex == 0xff) {
        return (x * y) / (x * y);
    }
    if (uxi << 1 <= uy.i << 1) {
        if (uxi << 1 == uy.i << 1) {
            return 0 * x;
        }
        return x;
    }
    if (!ex) {
        for (i = uxi << 9; i >> 31 == 0; ex--, i <<= 1) {
        }
        uxi <<= -ex + 1;
    } else {
        uxi &= ~(u32)0 >> 9;
        uxi |= (u32)1 << 23;
    }
    if (!ey) {
        for (i = uy.i << 9; i >> 31 == 0; ey--, i <<= 1) {
        }
        uy.i <<= -ey + 1;
    } else {
        uy.i &= ~(u32)0 >> 9;
        uy.i |= (u32)1 << 23;
    }
    for (; ex > ey; ex--) {
        i = uxi - uy.i;
        if (i >> 31 == 0) {
            if (i == 0) {
                return 0 * x;
            }
            uxi = i;
        }
        uxi <<= 1;
    }
    i = uxi - uy.i;
    if (i >> 31 == 0) {
        if (i == 0) {
            return 0 * x;
        }
        uxi = i;
    }
    for (; uxi >> 23 == 0; uxi <<= 1, ex--) {
    }
    if (ex > 0) {
        uxi -= (u32)1 << 23;
        uxi |= (u32)ex << 23;
    } else {
        uxi >>= -ex + 1;
    }
    uxi |= sx;
    ux.i = uxi;
    return ux.f;
}

// ---- fma: x * y + z with one rounding --------------------------------------------

typedef unsigned __int128 u128;

static int msb128(u128 v) {
    u64 hi = (u64)(v >> 64);
    if (hi) {
        return 127 - __builtin_clzll(hi);
    }
    return 63 - __builtin_clzll((u64)v);
}

static u128 shift_sticky(u128 v, int d) {
    if (d <= 0) {
        return v;
    }
    if (d >= 128) {
        return v != 0;
    }
    u128 lost = v & (((u128)1 << d) - 1);
    return (v >> d) | (lost != 0);
}

// The exact value (-1)^s * (mx 2^ex * my 2^ey) + (-1)^t * mz 2^ez, rounded
// to nearest even with prec bits and the least exponent emin: the mantissa
// and its exponent. A zero result has m == 0.
static void fma_exact(u64 mx, int ex, u64 my, int ey, int s, u64 mz, int ez, int t, int prec, int emin, u64 *m_out,
                      int *e_out, int *sign_out) {
    u128 p = (u128)mx * my;
    int ep = ex + ey;
    int shift_p = 125 - msb128(p);
    p <<= shift_p;
    ep -= shift_p;
    u128 z = (u128)mz;
    int shift_z = 125 - msb128(z);
    z <<= shift_z;
    ez -= shift_z;
    // The smaller is shifted to the larger's exponent; shifts of up to 20
    // are exact, and longer ones leave no cancellation to fear.
    int er;
    if (ep >= ez) {
        z = shift_sticky(z, ep - ez);
        er = ep;
    } else {
        p = shift_sticky(p, ez - ep);
        er = ez;
    }
    u128 r;
    int sign;
    if (s == t) {
        r = p + z;
        sign = s;
    } else if (p >= z) {
        r = p - z;
        sign = s;
    } else {
        r = z - p;
        sign = t;
    }
    *sign_out = sign;
    if (r == 0) {
        *m_out = 0;
        *e_out = 0;
        *sign_out = 0;
        return;
    }
    int k = msb128(r);
    int sh = k - (prec - 1);
    int e = er + sh;
    if (e < emin) {
        sh += emin - e;
        e = emin;
    }
    u64 m;
    if (sh <= 0) {
        m = (u64)(r << -sh);
    } else if (sh > 128) {
        m = 0;
    } else {
        u128 kept = sh == 128 ? 0 : r >> sh;
        int guard = (int)((r >> (sh - 1)) & 1);
        int sticky = sh >= 2 ? (r & (((u128)1 << (sh - 1)) - 1)) != 0 : 0;
        m = (u64)kept;
        if (guard && (sticky || (m & 1))) {
            m++;
        }
        if (m == (u64)1 << prec) {
            m >>= 1;
            e++;
        }
    }
    *m_out = m;
    *e_out = e;
}

double fma(double x, double y, double z) {
    F64Bits ux = {x}, uy = {y}, uz = {z};
    int fx = (int)(ux.i >> 52 & 0x7ff), fy = (int)(uy.i >> 52 & 0x7ff), fz = (int)(uz.i >> 52 & 0x7ff);
    // An infinite or NaN z with finite factors: the exact product is finite.
    if (fz == 0x7ff && fx != 0x7ff && fy != 0x7ff) {
        return z;
    }
    // Not finite, or a zero factor: no rounding of the product to fear.
    if (fx == 0x7ff || fy == 0x7ff || fz == 0x7ff || x == 0 || y == 0) {
        return x * y + z;
    }
    if (z == 0) {
        return x * y;
    }
    u64 frac = ((u64)1 << 52) - 1;
    u64 mx = fx ? (ux.i & frac) | ((u64)1 << 52) : ux.i & frac;
    u64 my = fy ? (uy.i & frac) | ((u64)1 << 52) : uy.i & frac;
    u64 mz = fz ? (uz.i & frac) | ((u64)1 << 52) : uz.i & frac;
    int ex = (fx ? fx : 1) - 1075, ey = (fy ? fy : 1) - 1075, ez = (fz ? fz : 1) - 1075;
    int s = (int)((ux.i ^ uy.i) >> 63), t = (int)(uz.i >> 63);
    u64 m;
    int e, sign;
    fma_exact(mx, ex, my, ey, s, mz, ez, t, 53, -1074, &m, &e, &sign);
    F64Bits r;
    if (m == 0) {
        r.i = (u64)sign << 63;
    } else if (e > 971) {
        r.i = ((u64)sign << 63) | 0x7ff0000000000000ULL;
    } else if (m >= (u64)1 << 52) {
        r.i = ((u64)sign << 63) | ((u64)(e + 1075) << 52) | (m & frac);
    } else {
        r.i = ((u64)sign << 63) | m;
    }
    return r.f;
}

float fmaf(float x, float y, float z) {
    F32Bits ux = {x}, uy = {y}, uz = {z};
    int fx = (int)(ux.i >> 23 & 0xff), fy = (int)(uy.i >> 23 & 0xff), fz = (int)(uz.i >> 23 & 0xff);
    if (fz == 0xff && fx != 0xff && fy != 0xff) {
        return z;
    }
    if (fx == 0xff || fy == 0xff || fz == 0xff || x == 0 || y == 0) {
        return x * y + z;
    }
    if (z == 0) {
        return x * y;
    }
    u32 frac = ((u32)1 << 23) - 1;
    u64 mx = fx ? (ux.i & frac) | ((u32)1 << 23) : ux.i & frac;
    u64 my = fy ? (uy.i & frac) | ((u32)1 << 23) : uy.i & frac;
    u64 mz = fz ? (uz.i & frac) | ((u32)1 << 23) : uz.i & frac;
    int ex = (fx ? fx : 1) - 150, ey = (fy ? fy : 1) - 150, ez = (fz ? fz : 1) - 150;
    int s = (int)((ux.i ^ uy.i) >> 31), t = (int)(uz.i >> 31);
    u64 m;
    int e, sign;
    fma_exact(mx, ex, my, ey, s, mz, ez, t, 24, -149, &m, &e, &sign);
    F32Bits r;
    if (m == 0) {
        r.i = (u32)sign << 31;
    } else if (e > 104) {
        r.i = ((u32)sign << 31) | 0x7f800000;
    } else if (m >= (u64)1 << 23) {
        r.i = ((u32)sign << 31) | ((u32)(e + 150) << 23) | ((u32)m & frac);
    } else {
        r.i = ((u32)sign << 31) | (u32)m;
    }
    return r.f;
}
