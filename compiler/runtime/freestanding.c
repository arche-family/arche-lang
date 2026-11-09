// The runtime of a freestanding Arche program (spec/arche.md §10.3): no
// operating system, no C library. It gives the compiler's code what rt.c
// gives it on Linux, except std: memory primitives, the regions' arenas,
// and traps, which call the program's panic handler. Built per target with
// clang --target=... -ffreestanding -nostdlib.

// long long, not long: a UEFI application is built for x86_64-windows,
// where long is 32 bits (LLP64); long long is 64 everywhere.
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long i64;

typedef struct {
    const u8 *ptr;
    u64 len;
} Str;

// ---- what LLVM may call --------------------------------------------------------

// A program built without the FPU, such as a kernel built with -mno-sse
// (target_features, spec/arche.md §10.2), has no floats, so no float
// functions to call: on x86-64 a double cannot even be returned then.
#if !defined(__x86_64__) || defined(__SSE2__)
#include "math.inc"
#endif

void *memcpy(void *dst, const void *src, u64 n) {
    u8 *d = dst;
    const u8 *s = src;
    for (u64 i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, u64 n) {
    u8 *d = dst;
    const u8 *s = src;
    if (d < s) {
        for (u64 i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (u64 i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

void *memset(void *dst, int c, u64 n) {
    u8 *d = dst;
    for (u64 i = 0; i < n; i++) {
        d[i] = (u8)c;
    }
    return dst;
}

int memcmp(const void *a, const void *b, u64 n) {
    const u8 *x = a;
    const u8 *y = b;
    for (u64 i = 0; i < n; i++) {
        if (x[i] != y[i]) {
            return x[i] < y[i] ? -1 : 1;
        }
    }
    return 0;
}

void a0rt_memmove(u8 *dst, const u8 *src, u64 n) {
    memmove(dst, src, n);
}

// ---- panics ----------------------------------------------------------------------

// The program's panic handler: the function --panic-handler names, which
// the compiler gives this symbol. It never returns.
_Noreturn void a0_panic(const Str *message, const Str *file, u32 line, u32 column);

_Noreturn void a0rt_trap(const u8 *message, u64 len, const u8 *file, u64 file_len, u32 line, u32 col) {
    Str m = {message, len};
    Str f = {file, file_len};
    a0_panic(&m, &f, line, col);
}

_Noreturn static void die(const char *message) {
    u64 n = 0;
    while (message[n]) {
        n++;
    }
    Str m = {(const u8 *)message, n};
    Str f = {(const u8 *)"<runtime>", 9};
    a0_panic(&m, &f, 0, 0);
}

// Error return traces need a place to print; a freestanding program has
// none, so it keeps none.
void a0rt_trace_start(const u8 *file, u64 file_len, u32 line, u32 col) {
    (void)file, (void)file_len, (void)line, (void)col;
}

void a0rt_trace_push(const u8 *file, u64 file_len, u32 line, u32 col) {
    (void)file, (void)file_len, (void)line, (void)col;
}

// ---- str -------------------------------------------------------------------------

u8 a0rt_str_boundary(const u8 *p, u64 n, u64 i) {
    return i == 0 || i >= n || (p[i] & 0xC0) != 0x80;
}

u8 a0rt_str_eq(const u8 *a, u64 an, const u8 *b, u64 bn) {
    return an == bn && memcmp(a, b, an) == 0;
}

u8 a0rt_str_starts_with(const u8 *a, u64 an, const u8 *b, u64 bn) {
    return an >= bn && memcmp(a, b, bn) == 0;
}

// ---- regions (spec/arche.md §7) --------------------------------------------------
//
// A freestanding program has no allocator but its regions. They nest, so
// one static heap serves them as a stack: a region's memory is everything
// above its mark. When an outer region allocates while an inner one is
// open, the inner one's end cannot drop below that memory; it is pinned,
// and its memory goes when the outer region ends.

#ifndef A0_HEAP_SIZE
#define A0_HEAP_SIZE ((u64)4 << 20)
#endif

enum { MAX_REGIONS = 64 };

static u8 heap[A0_HEAP_SIZE] __attribute__((aligned(16)));
static u64 top;
static u64 marks[MAX_REGIONS];
static u8 pinned[MAX_REGIONS];
static u32 depth;

// Memory starts zero (.bss) and is zeroed again as regions hand it out.
void *a0rt_alloc_in(u64 handle, u64 n) {
    if (handle == 0 || handle > depth) {
        die("no allocator: a freestanding program allocates in a region");
    }
    for (u32 i = (u32)handle; i < depth; i++) {
        pinned[i] = 1; // regions inside this one end above its memory
    }
    n = (n + 15) & ~(u64)15;
    if (n == 0) {
        n = 16;
    }
    if (n > A0_HEAP_SIZE - top) {
        die("out of memory");
    }
    void *p = heap + top;
    top += n;
    memset(p, 0, n);
    return p;
}

void *a0rt_alloc(u64 n) {
    return a0rt_alloc_in(depth, n);
}

u64 a0rt_region_begin(void) {
    if (depth == MAX_REGIONS) {
        die("regions nest too deep");
    }
    marks[depth] = top;
    pinned[depth] = 0;
    depth++;
    return depth;
}

void a0rt_region_end(u64 handle) {
    u32 i = (u32)handle - 1;
    if (!pinned[i]) {
        top = marks[i];
    }
    depth = i;
}
