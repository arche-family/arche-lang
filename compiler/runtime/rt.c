// The runtime of programs the Arche compiler builds (docs/abi.md).
//
// It calls no C library: it has its own entry point, talks to the system
// through its system calls alone, and allocates from memory it maps itself.
// x86-64 Linux and Windows (docs/sprints.md S19): everything that differs
// between them is in linux.inc and windows.inc, included at the end of this
// file, and reached only through the os_ functions below.
// Build: clang -O2 -ffreestanding -fno-builtin -nostdlib -fno-stack-protector

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
// Windows counts a long as four bytes and Linux as eight; long long is
// eight on both.
typedef unsigned long long u64;
typedef long long i64;

// ---- the system ---------------------------------------------------------------
//
// What a system gives the runtime. A handle is a file, a socket, or one of
// 0, 1 and 2, this program's standard input, output and error. An operation
// on a file answers with what it did, or with a negated code below, so that
// a failure reads the same on every system; std turns those into IoError
// (lib/std.arche, docs/abi.md).

enum { IO_NOT_FOUND = 1, IO_ACCESS = 2, IO_IS_DIR = 3, IO_INVALID_UTF8 = 4, IO_FAILED = 5 };

typedef struct Fiber Fiber;
typedef struct Run Run;
typedef struct Kept Kept;
typedef struct System System;

// memory: pages that are zero until written
static void *os_map(u64 n);
static void os_unmap(void *p, u64 n);
static u8 *os_stack_map(u64 n);        // a task's stack; its lowest page faults
static void os_stack_unmap(u8 *p, u64 n);
// files and output
static i64 os_open_read(const u8 *path, u64 path_len);
static i64 os_open_write(const u8 *path, u64 path_len);
static i64 os_read(i64 handle, u8 *p, u64 n);
static i64 os_write(i64 handle, const u8 *p, u64 n);
static void os_close(i64 handle);
static i64 os_fs_dir(void);            // std.Fs.dir, as this system names the current directory
// the process
_Noreturn static void os_exit(int status);
static u64 os_seed(void);              // the clock and this process, for a test run's seed
static u64 os_processors(void);
// tasks (spec/arche.md §9)
void a0rt_ctx_switch(u64 *save_rsp, u64 load_rsp);
static void os_stack_switch(const Fiber *next);
static u64 os_fiber_frame(void);   // how many words of a new fiber's stack its frame takes
static u64 os_fiber_saved(void);   // which of them a0rt_ctx_switch returns through
static void os_poll(const i64 *handles, const short *events, u8 *ready, u64 n);
// the test runner: a child process for each test
static void os_test_child(System *sys);
// a panic: how it got there, when the program knows (the own backend's table)
static void os_stack_trace(void);
static int stack_trace_off;
static void os_start_test(System *sys, u64 i, u64 seed, Run *r);
static void os_collect(Run *runs, u64 count);

// ---- memory ---------------------------------------------------------------------

// The C compiler may turn loops into calls to these, and LLVM's memcpy and
// memset intrinsics call them.
#include "math.inc"

void *memcpy(void *dst, const void *src, u64 n) {
    u8 *d = dst;
    const u8 *s = src;
    for (u64 i = 0; i < n; i++) {
        d[i] = s[i];
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

_Noreturn void a0rt_die(const char *message, u64 len);

// Memory comes in chunks mapped from the kernel and is handed out by
// bumping a pointer. The program's allocator, handle 0, frees nothing: an
// Arche-0 program allocates for its whole run (spec/arche-0.md §13). A
// region (spec/arche.md §7) is an arena with chunks of its own, its handle
// the arena's address, and all of them are unmapped when it ends.
static u8 *heap_next;
static u64 heap_left;

static void *map_pages(u64 n) {
    void *p = os_map(n);
    if (!p) {
        a0rt_die("out of memory", 13);
    }
    return p;
}

static void *alloc_bytes(u64 n) {
    n = (n + 15) & ~(u64)15;
    if (n > heap_left) {
        u64 chunk = n > ((u64)64 << 20) ? n : ((u64)64 << 20);
        heap_next = map_pages(chunk);
        heap_left = chunk;
    }
    void *r = heap_next;
    heap_next += n;
    heap_left -= n;
    return r;
}

typedef struct Chunk {
    struct Chunk *prev;
    u64 size;
} Chunk;

typedef struct Arena {
    u8 *next;
    u64 left;
    Chunk *last;
    struct Arena *free_next;
} Arena;

static Arena *free_arenas;

static void *arena_alloc(Arena *a, u64 n) {
    n = (n + 15) & ~(u64)15;
    if (n > a->left) {
        u64 size = (n + sizeof(Chunk) + 4095) & ~(u64)4095;
        if (size < 65536) {
            size = 65536;
        }
        Chunk *c = map_pages(size);
        c->prev = a->last;
        c->size = size;
        a->last = c;
        a->next = (u8 *)c + sizeof(Chunk);
        a->left = size - sizeof(Chunk);
    }
    void *r = a->next;
    a->next += n;
    a->left -= n;
    return r;
}

// Memory from mmap is zero, and stays so until handed out once.
static void *alloc_in(u64 allocator, u64 n) {
    if (n == 0) {
        n = 1;
    }
    if (allocator == 0) {
        return alloc_bytes(n);
    }
    return arena_alloc((Arena *)allocator, n);
}

void *a0rt_alloc(u64 n) {
    return alloc_in(0, n);
}

// core.mem.alloc (lib/core/mem.arche): zeroed memory from an allocator.
void *a0rt_alloc_in(u64 allocator, u64 n) {
    return alloc_in(allocator, n);
}

// core.mem.copy: n bytes, which may overlap.
void a0rt_memmove(u8 *dst, const u8 *src, u64 n) {
    if (dst < src) {
        for (u64 i = 0; i < n; i++) {
            dst[i] = src[i];
        }
    } else {
        for (u64 i = n; i > 0; i--) {
            dst[i - 1] = src[i - 1];
        }
    }
}

static void *grow_in(u64 allocator, void *old, u64 old_bytes, u64 new_bytes) {
    void *p = alloc_in(allocator, new_bytes);
    if (old && old_bytes) {
        memcpy(p, old, old_bytes);
    }
    return p;
}

u64 a0rt_region_begin(void) {
    Arena *a = free_arenas;
    if (a) {
        free_arenas = a->free_next;
    } else {
        a = alloc_bytes(sizeof(Arena));
    }
    a->next = 0;
    a->left = 0;
    a->last = 0;
    a->free_next = 0;
    return (u64)a;
}

void a0rt_region_end(u64 handle) {
    Arena *a = (Arena *)handle;
    Chunk *c = a->last;
    while (c) {
        Chunk *prev = c->prev;
        os_unmap(c, c->size);
        c = prev;
    }
    a->free_next = free_arenas;
    free_arenas = a;
}

// Appends n bytes to a String or List(u8): {data, len, cap}.
void a0rt_bytes_push(u64 *list, const u8 *p, u64 n) {
    if (list[1] + n > list[2]) {
        u64 cap = list[2] ? list[2] : 16;
        while (cap < list[1] + n) {
            cap *= 2;
        }
        list[0] = (u64)grow_in(list[3], (void *)list[0], list[1], cap);
        list[2] = cap;
    }
    memcpy((u8 *)list[0] + list[1], p, n);
    list[1] += n;
}

// Whether offset i of a str falls on a character boundary (§7.7).
u8 a0rt_str_boundary(const u8 *p, u64 n, u64 i) {
    return i == 0 || i >= n || (p[i] & 0xC0) != 0x80;
}

u8 a0rt_str_eq(const u8 *a, u64 an, const u8 *b, u64 bn) {
    return an == bn && memcmp(a, b, an) == 0;
}

u8 a0rt_str_starts_with(const u8 *a, u64 an, const u8 *b, u64 bn) {
    return an >= bn && memcmp(a, b, bn) == 0;
}

// Makes room for one more element of a List or String: {data, len, cap}.
void a0rt_list_reserve(u64 *list, u64 elem_size) {
    if (list[1] < list[2]) {
        return;
    }
    u64 cap = list[2] ? list[2] * 2 : 8;
    list[0] = (u64)grow_in(list[3], (void *)list[0], list[2] * elem_size, cap * elem_size);
    list[2] = cap;
}

// ---- output -----------------------------------------------------------------------

typedef struct {
    i64 fd;
    u64 len;
    u8 data[8192];
} Buffer;

static Buffer buffers[3];

static void flush(Buffer *b) {
    u64 done = 0;
    while (done < b->len) {
        i64 n = os_write(b->fd, b->data + done, b->len - done);
        if (n <= 0) {
            break;
        }
        done += (u64)n;
    }
    b->len = 0;
}

static void flush_all(void) {
    flush(&buffers[1]);
    flush(&buffers[2]);
}

// std.Output is {fd}.
static Buffer *buffer_of(const i64 *output) {
    return &buffers[*output == 2 ? 2 : 1];
}

void a0rt_write(const i64 *output, const u8 *p, u64 n) {
    Buffer *b = buffer_of(output);
    for (u64 i = 0; i < n; i++) {
        if (b->len == sizeof b->data) {
            flush(b);
        }
        b->data[b->len++] = p[i];
    }
}

void a0rt_write_byte(const i64 *output, u8 c) {
    a0rt_write(output, &c, 1);
}

void a0rt_write_u64(const i64 *output, u64 v) {
    u8 digits[20];
    int n = 0;
    do {
        digits[19 - n++] = (u8)('0' + v % 10);
        v /= 10;
    } while (v);
    a0rt_write(output, digits + 20 - n, (u64)n);
}

void a0rt_write_i64(const i64 *output, i64 v) {
    if (v < 0) {
        a0rt_write_byte(output, '-');
        a0rt_write_u64(output, (u64)0 - (u64)v);
    } else {
        a0rt_write_u64(output, (u64)v);
    }
}

// ---- floats (spec/arche.md §2.4) ------------------------------------------------
//
// A float is written as the shortest decimal that reads back to the same
// value (Burger and Dybvig's free-format algorithm), with exact integer
// arithmetic, so the output is the same on every target.

enum { BIG_LIMBS = 44 };

typedef struct {
    u32 limb[BIG_LIMBS];
    int len;
} Big;

static void big_set(Big *x, u64 v) {
    x->len = 0;
    while (v) {
        x->limb[x->len++] = (u32)v;
        v >>= 32;
    }
}

static void big_mul_small(Big *x, u32 m) {
    u64 carry = 0;
    for (int i = 0; i < x->len; i++) {
        u64 v = (u64)x->limb[i] * m + carry;
        x->limb[i] = (u32)v;
        carry = v >> 32;
    }
    if (carry) {
        x->limb[x->len++] = (u32)carry;
    }
}

static void big_shl(Big *x, int k) {
    int words = k / 32, bits = k % 32;
    if (x->len == 0) {
        return;
    }
    for (int i = x->len - 1; i >= 0; i--) {
        x->limb[i + words] = x->limb[i];
    }
    for (int i = 0; i < words; i++) {
        x->limb[i] = 0;
    }
    x->len += words;
    if (bits) {
        u32 carry = 0;
        for (int i = words; i < x->len; i++) {
            u32 v = x->limb[i];
            x->limb[i] = (v << bits) | carry;
            carry = v >> (32 - bits);
        }
        if (carry) {
            x->limb[x->len++] = carry;
        }
    }
}

static int big_cmp(const Big *a, const Big *b) {
    if (a->len != b->len) {
        return a->len < b->len ? -1 : 1;
    }
    for (int i = a->len - 1; i >= 0; i--) {
        if (a->limb[i] != b->limb[i]) {
            return a->limb[i] < b->limb[i] ? -1 : 1;
        }
    }
    return 0;
}

static void big_add(Big *out, const Big *a, const Big *b) {
    u64 carry = 0;
    int n = a->len > b->len ? a->len : b->len;
    for (int i = 0; i < n; i++) {
        u64 v = carry;
        if (i < a->len) {
            v += a->limb[i];
        }
        if (i < b->len) {
            v += b->limb[i];
        }
        out->limb[i] = (u32)v;
        carry = v >> 32;
    }
    out->len = n;
    if (carry) {
        out->limb[out->len++] = (u32)carry;
    }
}

// a -= b, for a >= b.
static void big_sub(Big *a, const Big *b) {
    u64 borrow = 0;
    for (int i = 0; i < a->len; i++) {
        u64 bv = (i < b->len ? b->limb[i] : 0) + borrow;
        if ((u64)a->limb[i] >= bv) {
            a->limb[i] = (u32)((u64)a->limb[i] - bv);
            borrow = 0;
        } else {
            a->limb[i] = (u32)(((u64)1 << 32) + a->limb[i] - bv);
            borrow = 1;
        }
    }
    while (a->len > 0 && a->limb[a->len - 1] == 0) {
        a->len--;
    }
}

// The shortest digits of f * 2^e for a format of p bits whose smallest
// exponent is min_e; returns their count, and *k with value 0.ddd * 10^k.
static int shortest(u64 f, int e, int p, int min_e, u8 *digits, int *k_out) {
    Big r, s, mp, mm, t;
    int even = (f & 1) == 0;
    u64 hidden = (u64)1 << (p - 1);
    if (e >= 0) {
        big_set(&r, f);
        big_set(&mp, 1);
        big_shl(&mp, e);
        big_set(&mm, 1);
        big_shl(&mm, e);
        if (f != hidden) {
            big_shl(&r, e + 1);
            big_set(&s, 2);
        } else {
            big_shl(&r, e + 2);
            big_set(&s, 4);
            big_shl(&mp, 1);
        }
    } else {
        big_set(&r, f);
        big_set(&mp, 1);
        big_set(&mm, 1);
        big_set(&s, 1);
        if (e == min_e || f != hidden) {
            big_shl(&r, 1);
            big_shl(&s, 1 - e);
        } else {
            big_shl(&r, 2);
            big_shl(&s, 2 - e);
            big_set(&mp, 2);
        }
    }
    // k = ceil(log10 v), estimated low from the bit length, then fixed.
    int bits = 0;
    for (u64 x = f; x; x >>= 1) {
        bits++;
    }
    int lg = e + bits - 1;
    int k = lg >= 0 ? (lg * 1233) >> 12 : -((-lg * 1233 + 4095) >> 12);
    if (k >= 0) {
        for (int i = 0; i < k; i++) {
            big_mul_small(&s, 10);
        }
    } else {
        for (int i = 0; i < -k; i++) {
            big_mul_small(&r, 10);
            big_mul_small(&mp, 10);
            big_mul_small(&mm, 10);
        }
    }
    for (;;) {
        big_add(&t, &r, &mp);
        int c = big_cmp(&t, &s);
        if (even ? c >= 0 : c > 0) {
            big_mul_small(&s, 10);
            k++;
        } else {
            break;
        }
    }
    int n = 0;
    for (;;) {
        big_mul_small(&r, 10);
        big_mul_small(&mp, 10);
        big_mul_small(&mm, 10);
        int d = 0;
        while (big_cmp(&r, &s) >= 0) {
            big_sub(&r, &s);
            d++;
        }
        int c1 = big_cmp(&r, &mm);
        int low = even ? c1 <= 0 : c1 < 0;
        big_add(&t, &r, &mp);
        int c2 = big_cmp(&t, &s);
        int high = even ? c2 >= 0 : c2 > 0;
        if (!low && !high) {
            digits[n++] = (u8)d;
            continue;
        }
        if (low && high) {
            big_add(&t, &r, &r);
            int c3 = big_cmp(&t, &s);
            if (c3 > 0 || (c3 == 0 && (d & 1))) {
                d++;
            }
        } else if (high) {
            d++;
        }
        digits[n++] = (u8)d;
        break;
    }
    *k_out = k;
    return n;
}

static void write_float(const i64 *output, int negative, u64 f, int e, int p, int min_e, int special) {
    u8 out[64];
    int len = 0;
    if (special == 2) {
        a0rt_write(output, (const u8 *)"nan", 3);
        return;
    }
    if (negative) {
        out[len++] = '-';
    }
    if (special == 1) {
        out[len++] = 'i';
        out[len++] = 'n';
        out[len++] = 'f';
        a0rt_write(output, out, (u64)len);
        return;
    }
    if (f == 0) {
        out[len++] = '0';
        out[len++] = '.';
        out[len++] = '0';
        a0rt_write(output, out, (u64)len);
        return;
    }
    u8 d[32];
    int k;
    int n = shortest(f, e, p, min_e, d, &k);
    if (k > 21 || k < -5) {
        out[len++] = (u8)('0' + d[0]);
        if (n > 1) {
            out[len++] = '.';
            for (int i = 1; i < n; i++) {
                out[len++] = (u8)('0' + d[i]);
            }
        }
        out[len++] = 'e';
        int x = k - 1;
        if (x < 0) {
            out[len++] = '-';
            x = -x;
        }
        u8 ex[8];
        int m = 0;
        do {
            ex[m++] = (u8)('0' + x % 10);
            x /= 10;
        } while (x);
        while (m) {
            out[len++] = ex[--m];
        }
    } else if (k <= 0) {
        out[len++] = '0';
        out[len++] = '.';
        for (int i = 0; i < -k; i++) {
            out[len++] = '0';
        }
        for (int i = 0; i < n; i++) {
            out[len++] = (u8)('0' + d[i]);
        }
    } else if (k >= n) {
        for (int i = 0; i < n; i++) {
            out[len++] = (u8)('0' + d[i]);
        }
        for (int i = n; i < k; i++) {
            out[len++] = '0';
        }
        out[len++] = '.';
        out[len++] = '0';
    } else {
        for (int i = 0; i < n; i++) {
            if (i == k) {
                out[len++] = '.';
            }
            out[len++] = (u8)('0' + d[i]);
        }
    }
    a0rt_write(output, out, (u64)len);
}

void a0rt_write_f64(const i64 *output, double x) {
    u64 b;
    memcpy(&b, &x, 8);
    int negative = (int)(b >> 63);
    int be = (int)((b >> 52) & 0x7FF);
    u64 m = b & (((u64)1 << 52) - 1);
    if (be == 0x7FF) {
        write_float(output, negative, 0, 0, 53, -1074, m ? 2 : 1);
        return;
    }
    if (be == 0) {
        write_float(output, negative, m, -1074, 53, -1074, 0);
    } else {
        write_float(output, negative, m | ((u64)1 << 52), be - 1075, 53, -1074, 0);
    }
}

void a0rt_write_f32(const i64 *output, float x) {
    u32 b;
    memcpy(&b, &x, 4);
    int negative = (int)(b >> 31);
    int be = (int)((b >> 23) & 0xFF);
    u64 m = b & 0x7FFFFF;
    if (be == 0xFF) {
        write_float(output, negative, 0, 0, 24, -149, m ? 2 : 1);
        return;
    }
    if (be == 0) {
        write_float(output, negative, m, -149, 24, -149, 0);
    } else {
        write_float(output, negative, m | ((u64)1 << 23), be - 150, 24, -149, 0);
    }
}

// ---- error return traces (spec/arche.md §8.3) ---------------------------------------
//
// Where an error was made and each `try` that passed it up. Only failing
// paths record, so success costs nothing.

enum { TRACE_SIZE = 32 };

typedef struct {
    const u8 *file;
    u64 file_len;
    u32 line, col;
} TraceEntry;

static TraceEntry trace[TRACE_SIZE];
static u32 trace_len;

void a0rt_trace_push(const u8 *file, u64 file_len, u32 line, u32 col) {
    if (trace_len < TRACE_SIZE) {
        trace[trace_len].file = file;
        trace[trace_len].file_len = file_len;
        trace[trace_len].line = line;
        trace[trace_len].col = col;
        trace_len++;
    }
}

void a0rt_trace_start(const u8 *file, u64 file_len, u32 line, u32 col) {
    trace_len = 0;
    a0rt_trace_push(file, file_len, line, col);
}

// ---- exit -------------------------------------------------------------------------

static _Noreturn void exit_with(int status) {
    flush_all();
    os_exit(status);
}

static const i64 err_fd = 2;
static const i64 out_fd = 1;

void a0rt_die(const char *message, u64 len) {
    flush_all();
    a0rt_write(&err_fd, (const u8 *)"panic: ", 7);
    a0rt_write(&err_fd, (const u8 *)message, len);
    a0rt_write(&err_fd, (const u8 *)"\n", 1);
    os_stack_trace();
    exit_with(101);
}

// panic and every trap: the message, then where it happened (§10).
// ---- tasks (spec/arche.md §9, lib/std/task.arche) ---------------------------------
//
// Tasks are fibers: each has a stack of its own, and a0rt_ctx_switch moves
// between them. They run on one thread, and switch only where a task waits:
// for a scope's tasks, a channel, a lock, a socket, or by yielding. Which
// ready task runs next comes from a generator with a seed, so a run repeats
// with the same seed: arche test gives each test its own, and other programs
// a fixed one. When no task is ready and some wait for sockets, poll waits
// for the kernel to make one ready (std's sockets do not block).

enum { READY, RUNNING, WAITING, DONE, WAITING_IO };
enum { TASK_STACK = 256 * 1024, GUARD = 4096 };

struct Fiber {
    u64 rsp;
    u8 *stack;      // 0 for the thread's own: the fiber main runs on
    u16 (*fn)(void *env);
    void *env;
    struct TaskScope *scope;
    const void *waiting_on;
    int state;
    i64 io_fd;      // the socket it waits for: a descriptor, or a handle
    short io_events;
};

typedef struct TaskScope {
    u32 pending;
    u32 failed;      // 1: a task returned an error; 2: one panicked
} TaskScope;

static Fiber main_fiber = {0, 0, 0, 0, 0, 0, RUNNING, -1, 0};
static Fiber *current_fiber = &main_fiber;
static Fiber **fibers;
static u64 fiber_count, fiber_cap;
static u64 sched_state = 0x853C49E6748FEA9BUL;

// The scheduler's seed: arche test sets it for each test.
void a0rt_sched_seed(u64 seed) {
    sched_state = seed ? seed : 0x853C49E6748FEA9BUL;
}

static u64 sched_next(void) {
    u64 x = sched_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    sched_state = x;
    return x * 0x2545F4914F6CDD1DUL;
}

static void switch_to(Fiber *next) {
    Fiber *prev = current_fiber;
    if (next == prev) {
        prev->state = RUNNING;
        return;
    }
    current_fiber = next;
    next->state = RUNNING;
    os_stack_switch(next);
    a0rt_ctx_switch(&prev->rsp, next->rsp);
}

// The sockets the waiting fibers wait for, as os_poll takes them.
static i64 *poll_handles;
static short *poll_events;
static u8 *poll_ready;
static Fiber **poll_fibers;
static u64 poll_cap;

static void poll_add(Fiber *f, u64 *n) {
    if (f->state != WAITING_IO) {
        return;
    }
    if (*n == poll_cap) {
        u64 cap = poll_cap ? poll_cap * 2 : 16;
        i64 *handles = a0rt_alloc(cap * sizeof(i64));
        short *events = a0rt_alloc(cap * sizeof(short));
        u8 *ready = a0rt_alloc(cap);
        Fiber **who = a0rt_alloc(cap * sizeof(Fiber *));
        for (u64 i = 0; i < *n; i++) {
            handles[i] = poll_handles[i];
            events[i] = poll_events[i];
            who[i] = poll_fibers[i];
        }
        poll_handles = handles;
        poll_events = events;
        poll_ready = ready;
        poll_fibers = who;
        poll_cap = cap;
    }
    poll_handles[*n] = f->io_fd;
    poll_events[*n] = f->io_events;
    poll_ready[*n] = 0;
    poll_fibers[*n] = f;
    (*n)++;
}

// Blocks until a socket some fiber waits for is ready, and makes that
// fiber ready. With none waiting for one, every task waits for another.
static void poll_io(void) {
    u64 n = 0;
    poll_add(&main_fiber, &n);
    for (u64 i = 0; i < fiber_count; i++) {
        poll_add(fibers[i], &n);
    }
    if (n == 0) {
        a0rt_die("deadlock: every task is waiting", 31);
    }
    os_poll(poll_handles, poll_events, poll_ready, n);
    for (u64 i = 0; i < n; i++) {
        if (poll_ready[i]) {
            poll_fibers[i]->state = READY;
        }
    }
}

// The next fiber to run, chosen among the ready ones; when none is ready,
// one waiting for a socket, once the socket is.
static Fiber *pick(void) {
    u64 ready = 0;
    while (ready == 0) {
        ready = main_fiber.state == READY ? 1 : 0;
        for (u64 i = 0; i < fiber_count; i++) {
            ready += fibers[i]->state == READY;
        }
        if (ready == 0) {
            poll_io();
        }
    }
    u64 k = sched_next() % ready;
    if (main_fiber.state == READY) {
        if (k == 0) {
            return &main_fiber;
        }
        k--;
    }
    for (u64 i = 0; i < fiber_count; i++) {
        if (fibers[i]->state == READY) {
            if (k == 0) {
                return fibers[i];
            }
            k--;
        }
    }
    return &main_fiber;
}

// Waits until fd is ready for events (poll's POLLIN, POLLOUT).
void a0rt_io_wait(i64 fd, u32 events) {
    current_fiber->state = WAITING_IO;
    current_fiber->io_fd = fd;
    current_fiber->io_events = (short)events;
    switch_to(pick());
}

// Lets other ready tasks run first.
void a0rt_task_yield(void) {
    current_fiber->state = READY;
    switch_to(pick());
}

// Waits until a0rt_task_wake(key); the caller checks its condition again.
void a0rt_task_wait(const void *key) {
    current_fiber->state = WAITING;
    current_fiber->waiting_on = key;
    switch_to(pick());
}

void a0rt_task_wake(const void *key) {
    if (main_fiber.state == WAITING && main_fiber.waiting_on == key) {
        main_fiber.state = READY;
    }
    for (u64 i = 0; i < fiber_count; i++) {
        if (fibers[i]->state == WAITING && fibers[i]->waiting_on == key) {
            fibers[i]->state = READY;
        }
    }
}

// Memory for a task's closure: it lives as long as the program.
void *a0rt_task_memory(u64 n) {
    return a0rt_alloc(n);
}

void *a0rt_scope_new(void) {
    TaskScope *s = a0rt_alloc(sizeof(TaskScope));
    return s;
}

_Noreturn static void task_finish(u32 failure) {
    Fiber *f = current_fiber;
    f->state = DONE;
    if (failure > f->scope->failed) {
        f->scope->failed = failure;
    }
    f->scope->pending--;
    if (f->scope->pending == 0) {
        a0rt_task_wake(f->scope);
    }
    switch_to(pick());
    __builtin_unreachable();
}

static void fiber_entry(void) {
    Fiber *f = current_fiber;
    u16 err = f->fn(f->env);
    task_finish(err ? 1 : 0);
}

// Fibers that finished, kept with their stacks for the next spawn: a
// server's scope may outlive many of its tasks.
enum { SPARE_MAX = 64 };
static Fiber *spare[SPARE_MAX];
static u64 spare_count;

// Moves finished fibers out of the list. The running fiber is not one of
// them, so no stack in use is touched.
static void reap(void) {
    u64 kept = 0;
    for (u64 i = 0; i < fiber_count; i++) {
        Fiber *f = fibers[i];
        if (f->state == DONE && f != current_fiber) {
            if (spare_count < SPARE_MAX) {
                spare[spare_count++] = f;
            } else {
                os_stack_unmap(f->stack, TASK_STACK + GUARD);
            }
        } else {
            fibers[kept++] = f;
        }
    }
    fiber_count = kept;
}

void a0rt_spawn(void *scope, void *fn, void *env) {
    TaskScope *s = scope;
    reap();
    Fiber *f;
    if (spare_count > 0) {
        f = spare[--spare_count];
    } else {
        f = a0rt_alloc(sizeof(Fiber));
        u8 *p = os_stack_map(TASK_STACK + GUARD);
        if (!p) {
            a0rt_die("out of memory", 13);
        }
        f->stack = p;
    }
    // The frame a0rt_ctx_switch returns through, as this system's one
    // leaves it: the registers it restores, all zero for a fiber that has
    // not run, then where it returns to and that frame's own return
    // address, which nothing uses.
    u64 words = os_fiber_frame();
    u64 *top = (u64 *)(f->stack + TASK_STACK + GUARD);
    u64 *frame = top - words;
    for (u64 i = 0; i < words; i++) {
        frame[i] = 0;
    }
    frame[os_fiber_saved()] = (u64)fiber_entry;
    f->rsp = (u64)frame;
    f->fn = (u16 (*)(void *))fn;
    f->env = env;
    f->scope = s;
    f->state = READY;
    f->waiting_on = 0;
    f->io_fd = -1;
    s->pending++;
    if (fiber_count == fiber_cap) {
        u64 cap = fiber_cap ? fiber_cap * 2 : 16;
        Fiber **grown = a0rt_alloc(cap * sizeof(Fiber *));
        for (u64 i = 0; i < fiber_count; i++) {
            grown[i] = fibers[i];
        }
        fibers = grown;
        fiber_cap = cap;
    }
    fibers[fiber_count++] = f;
}

// Waits for the scope's tasks, then frees their stacks: 0, or 1 when one
// returned an error, 2 when one panicked.
u32 a0rt_scope_wait(void *scope) {
    TaskScope *s = scope;
    while (s->pending > 0) {
        a0rt_task_wait(s);
    }
    u64 kept = 0;
    for (u64 i = 0; i < fiber_count; i++) {
        Fiber *f = fibers[i];
        if (f->scope == s) {
            os_stack_unmap(f->stack, TASK_STACK + GUARD);
        } else {
            fibers[kept++] = f;
        }
    }
    fiber_count = kept;
    return s->failed;
}

_Noreturn void a0rt_trap(const u8 *message, u64 len, const u8 *file, u64 file_len, u32 line, u32 col) {
    flush_all();
    a0rt_write(&err_fd, (const u8 *)"panic: ", 7);
    a0rt_write(&err_fd, message, len);
    a0rt_write(&err_fd, (const u8 *)"\n  at ", 6);
    a0rt_write(&err_fd, file, file_len);
    a0rt_write_byte(&err_fd, ':');
    a0rt_write_u64(&err_fd, line);
    a0rt_write_byte(&err_fd, ':');
    a0rt_write_u64(&err_fd, col);
    a0rt_write_byte(&err_fd, '\n');
    os_stack_trace();
    if (current_fiber != &main_fiber) {
        // A task's panic ends the task; its scope reports it (spec §9).
        flush_all();
        task_finish(2);
    }
    exit_with(101);
}

// ---- std.Fs -------------------------------------------------------------------------

// What an os_ operation answered with, as the IoError's number.
static u16 io_error(i64 e) {
    return (u16)(-e);
}

static int valid_utf8(const u8 *s, u64 n) {
    u64 i = 0;
    while (i < n) {
        u8 b = s[i];
        u64 len;
        u32 c;
        u32 min;
        if (b < 0x80) {
            i++;
            continue;
        } else if ((b & 0xE0) == 0xC0) {
            len = 2, c = b & 0x1F, min = 0x80;
        } else if ((b & 0xF0) == 0xE0) {
            len = 3, c = b & 0x0F, min = 0x800;
        } else if ((b & 0xF8) == 0xF0) {
            len = 4, c = b & 0x07, min = 0x10000;
        } else {
            return 0;
        }
        if (n - i < len) {
            return 0;
        }
        for (u64 k = 1; k < len; k++) {
            if ((s[i + k] & 0xC0) != 0x80) {
                return 0;
            }
            c = (c << 6) | (s[i + k] & 0x3F);
        }
        if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
            return 0;
        }
        i += len;
    }
    return 1;
}

// A NUL-terminated copy of a path; NULL when it holds a NUL byte.
// Reads a whole file into a List(u8) or String {data, len, cap, alloc}.
static u16 read_into(const u8 *path, u64 path_len, u64 *out) {
    i64 fd = os_open_read(path, path_len);
    if (fd < 0) {
        return io_error(fd);
    }
    for (;;) {
        if (out[2] - out[1] < 65536) {
            u64 cap = out[2] ? out[2] * 2 : 65536;
            out[0] = (u64)grow_in(out[3], (void *)out[0], out[1], cap);
            out[2] = cap;
        }
        i64 n = os_read(fd, (u8 *)(out[0] + out[1]), out[2] - out[1]);
        if (n < 0) {
            os_close(fd);
            return io_error(n);
        }
        if (n == 0) {
            break;
        }
        out[1] += (u64)n;
    }
    os_close(fd);
    return 0;
}

// IoError!alloc.String and IoError!alloc.List(u8): {error: u16} and the
// value at offset 8.
// lib/std.arche's Fs: reads a file into the String or List(u8) at out,
// {data, len, cap, allocator}; checks UTF-8 when utf8 is set. 0 or the
// IoError's number.
u16 a0rt_fs_read(const u8 *path, u64 path_len, u64 *out, u8 utf8) {
    u16 e = read_into(path, path_len, out);
    if (e == 0 && utf8 && !valid_utf8((const u8 *)out[0], out[1])) {
        e = 4; // InvalidUtf8
    }
    return e;
}

// lib/std.arche's Output and Proc: write out what is buffered, and leave.
void a0rt_flush_all(void) {
    flush_all();
}

_Noreturn void a0rt_exit(u8 status) {
    exit_with(status);
}

void a0rt_read_file(const void *fs, const u8 *path, u64 path_len, u64 allocator, u8 *result) {
    (void)fs;
    u64 *value = (u64 *)(result + 8);
    value[3] = allocator;
    u16 e = read_into(path, path_len, value);
    if (e == 0 && !valid_utf8((const u8 *)value[0], value[1])) {
        e = 4; // InvalidUtf8
    }
    *(u16 *)result = e;
}

void a0rt_read_bytes(const void *fs, const u8 *path, u64 path_len, u64 allocator, u8 *result) {
    (void)fs;
    u64 *value = (u64 *)(result + 8);
    value[3] = allocator;
    *(u16 *)result = read_into(path, path_len, value);
}

u16 a0rt_write_file(const void *fs, const u8 *path, u64 path_len, const u8 *data, u64 len) {
    (void)fs;
    i64 fd = os_open_write(path, path_len);
    if (fd < 0) {
        return io_error(fd);
    }
    u64 done = 0;
    while (done < len) {
        i64 n = os_write(fd, data + done, len - done);
        if (n < 0) {
            os_close(fd);
            return io_error(n);
        }
        done += (u64)n;
    }
    os_close(fd);
    return 0;
}

// ---- alloc.Map ---------------------------------------------------------------------
//
// {entries, len, cap, slots, nslots, live, allocator}. An entry is the key,
// the value, and a live flag, each rounded up to 8 bytes. Entries stay in
// insertion order; the slots index them by hash with linear probing: 0 is
// empty, ~0 a removed entry, n the entry n - 1. A put keeps at least half of
// the slots empty, so every probe ends. Keys are compared and hashed byte by
// byte, which works because values are made from zeroed memory (docs/abi.md).

typedef struct {
    u8 *entries;
    u64 len;
    u64 cap;
    u64 *slots;
    u64 nslots;
    u64 live;
    u64 allocator;
} Map;

static u64 round8(u64 n) {
    return (n + 7) & ~(u64)7;
}

static u64 hash_bytes(const u8 *p, u64 n) {
    u64 h = 0xcbf29ce484222325UL;
    for (u64 i = 0; i < n; i++) {
        h = (h ^ p[i]) * 0x100000001b3UL;
    }
    return h ^ (h >> 29);
}

static u64 *find_slot(Map *m, const u8 *key, u64 ksize, u64 esize, int *found) {
    u64 mask = m->nslots - 1;
    u64 i = hash_bytes(key, ksize) & mask;
    u64 *free_slot = 0;
    for (;;) {
        u64 *s = &m->slots[i];
        if (*s == 0) {
            *found = 0;
            return free_slot ? free_slot : s;
        }
        if (*s == ~(u64)0) {
            if (!free_slot) {
                free_slot = s;
            }
        } else if (memcmp(m->entries + (*s - 1) * esize, key, ksize) == 0) {
            *found = 1;
            return s;
        }
        i = (i + 1) & mask;
    }
}

static void rebuild(Map *m, u64 ksize, u64 esize, u64 nslots) {
    u64 live = 0;
    for (u64 i = 0; i < m->len; i++) {
        u8 *e = m->entries + i * esize;
        if (*(u64 *)(e + esize - 8)) {
            if (live != i) {
                memcpy(m->entries + live * esize, e, esize);
            }
            live++;
        }
    }
    m->len = live;
    m->slots = alloc_in(m->allocator, nslots * 8);
    m->nslots = nslots;
    for (u64 i = 0; i < live; i++) {
        int found;
        *find_slot(m, m->entries + i * esize, ksize, esize, &found) = i + 1;
    }
}

void a0rt_map_put(Map *m, const u8 *key, u64 ksize, const u8 *value, u64 vsize) {
    u64 esize = round8(ksize) + round8(vsize) + 8;
    if ((m->len + 1) * 2 > m->nslots) {
        u64 nslots = 16;
        while (nslots < (m->live + 1) * 4) {
            nslots *= 2;
        }
        rebuild(m, ksize, esize, nslots);
    }
    int found;
    u64 *s = find_slot(m, key, ksize, esize, &found);
    if (found) {
        memcpy(m->entries + (*s - 1) * esize + round8(ksize), value, vsize);
        return;
    }
    if (m->len == m->cap) {
        u64 cap = m->cap ? m->cap * 2 : 8;
        m->entries = grow_in(m->allocator, m->entries, m->cap * esize, cap * esize);
        m->cap = cap;
    }
    u8 *e = m->entries + m->len * esize;
    memset(e, 0, esize);
    memcpy(e, key, ksize);
    memcpy(e + round8(ksize), value, vsize);
    *(u64 *)(e + esize - 8) = 1;
    *s = ++m->len;
    m->live++;
}

// The value stored under key, or NULL.
u8 *a0rt_map_get(Map *m, const u8 *key, u64 ksize, u64 vsize) {
    if (m->nslots == 0) {
        return 0;
    }
    u64 esize = round8(ksize) + round8(vsize) + 8;
    int found;
    u64 *s = find_slot(m, key, ksize, esize, &found);
    return found ? m->entries + (*s - 1) * esize + round8(ksize) : 0;
}

// Copies the value out to `out` and removes it; false when there is none.
u8 a0rt_map_remove(Map *m, const u8 *key, u64 ksize, u64 vsize, u8 *out) {
    if (m->nslots == 0) {
        return 0;
    }
    u64 esize = round8(ksize) + round8(vsize) + 8;
    int found;
    u64 *s = find_slot(m, key, ksize, esize, &found);
    if (!found) {
        return 0;
    }
    u8 *e = m->entries + (*s - 1) * esize;
    memcpy(out, e + round8(ksize), vsize);
    *(u64 *)(e + esize - 8) = 0;
    *s = ~(u64)0;
    m->live--;
    return 1;
}

// ---- alloc.Interner ---------------------------------------------------------------
//
// {texts, len, cap, slots, nslots, allocator}: texts is an array of str
// {ptr, len}, each copied once; a Symbol is a text's index.

typedef struct {
    const u8 *ptr;
    u64 len;
} Str;

typedef struct {
    Str *texts;
    u64 len;
    u64 cap;
    u32 *slots;
    u64 nslots;
    u64 allocator;
} Interner;

static u64 interner_slot(Interner *in, const u8 *p, u64 n) {
    u64 mask = in->nslots - 1;
    u64 i = hash_bytes(p, n) & mask;
    for (;;) {
        u32 s = in->slots[i];
        if (s == 0 || (in->texts[s - 1].len == n && memcmp(in->texts[s - 1].ptr, p, n) == 0)) {
            return i;
        }
        i = (i + 1) & mask;
    }
}

u32 a0rt_intern(Interner *in, const u8 *p, u64 n) {
    if ((in->len + 1) * 2 > in->nslots) {
        u64 nslots = in->nslots ? in->nslots * 2 : 64;
        in->slots = alloc_in(in->allocator, nslots * 4);
        in->nslots = nslots;
        for (u64 i = 0; i < in->len; i++) {
            in->slots[interner_slot(in, in->texts[i].ptr, in->texts[i].len)] = (u32)(i + 1);
        }
    }
    u64 slot = interner_slot(in, p, n);
    if (in->slots[slot]) {
        return in->slots[slot] - 1;
    }
    if (in->len == in->cap) {
        u64 cap = in->cap ? in->cap * 2 : 64;
        in->texts = grow_in(in->allocator, in->texts, in->cap * sizeof(Str), cap * sizeof(Str));
        in->cap = cap;
    }
    u8 *copy = alloc_in(in->allocator, n);
    memcpy(copy, p, n);
    in->texts[in->len].ptr = copy;
    in->texts[in->len].len = n;
    in->slots[slot] = (u32)(in->len + 1);
    return (u32)in->len++;
}

// ---- entry --------------------------------------------------------------------------

// std.System: {fs, stdout, stderr, args {items, len}, allocator}, and
// for lib/std.arche the environment after them (docs/abi.md). Windows
// gives no array of the environment, and leaves envp 0: its std asks the
// system for a name at a time (lib/std-windows.arche).
struct System {
    i64 fs;
    i64 stdout_fd;
    i64 stderr_fd;
    Str *args;
    u64 nargs;
    u64 allocator;
    char **envp;
};

extern u16 a0_entry(System *sys);
extern const Str a0_error_names[];

static u64 c_strlen(const char *s) {
    u64 n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

// Builds std.System from what the system gave the program, calls its main,
// and ends the program: every entry point (linux.inc, windows.inc) comes
// here with the arguments as UTF-8 text.
_Noreturn static void run_program(Str *args, u64 nargs, char **envp) {
    buffers[1].fd = 1;
    buffers[2].fd = 2;
    static System sys;
    // Fs works on paths from the current directory (std.Fs.dir).
    sys.fs = os_fs_dir();
    sys.stdout_fd = 1;
    sys.stderr_fd = 2;
    for (u64 i = 0; i < nargs; i++) {
        if (!valid_utf8(args[i].ptr, args[i].len)) {
            a0rt_die("an argument is not valid UTF-8", 30);
        }
    }
    sys.args = args;
    sys.nargs = nargs;
    sys.envp = envp;
    u16 err = a0_entry(&sys);
    if (err) {
        flush_all();
        a0rt_write(&err_fd, (const u8 *)"error: ", 7);
        a0rt_write(&err_fd, a0_error_names[err].ptr, a0_error_names[err].len);
        a0rt_write(&err_fd, (const u8 *)"\n", 1);
        for (u32 i = 0; i < trace_len; i++) {
            a0rt_write(&err_fd, (const u8 *)"  at ", 5);
            a0rt_write(&err_fd, trace[i].file, trace[i].file_len);
            a0rt_write_byte(&err_fd, ':');
            a0rt_write_u64(&err_fd, trace[i].line);
            a0rt_write_byte(&err_fd, ':');
            a0rt_write_u64(&err_fd, trace[i].col);
            a0rt_write_byte(&err_fd, '\n');
        }
        exit_with(1);
    }
    exit_with(0);
}

// ---- the test runner (arche test, lib/std/testing.arche) ---------------------------
//
// A test build has a table of its tests. Each runs in a child process, so a
// panic fails that test and the others still run. As many run at once as
// the machine has processors (--jobs n to choose); each one's output is
// kept and shown after its result line, in the tests' order, so a run reads
// the same however the children interleave. The seed is always printed;
// --seed repeats a run, --filter runs the tests whose names contain a text,
// and --json writes the report as JSON, for editors.

typedef struct {
    const u8 *name;
    u64 name_len;
    void *function;
    u64 takes_test;
} TestEntry;

// A build that is not a test build defines neither table. On Linux the
// names are weak, and are then 0; on Windows the linker points them at the
// empty tables windows.inc gives it (/alternatename).
#if defined(_WIN32)
extern const TestEntry a0_tests[];
extern const u64 a0_test_count;
#else
extern const TestEntry a0_tests[] __attribute__((weak));
extern const u64 a0_test_count __attribute__((weak));
#endif

// How many tests this build has: none when it is not a test build.
static u64 test_count(void) {
#if defined(_WIN32)
    return a0_test_count;
#else
    return &a0_test_count ? a0_test_count : 0;
#endif
}

static int contains(const u8 *hay, u64 n, const u8 *needle, u64 m) {
    if (m == 0) {
        return 1;
    }
    for (u64 i = 0; i + m <= n; i++) {
        if (memcmp(hay + i, needle, m) == 0) {
            return 1;
        }
    }
    return 0;
}

static int parse_u64(const Str *s, u64 *out) {
    u64 v = 0;
    if (s->len == 0) {
        return 0;
    }
    for (u64 i = 0; i < s->len; i++) {
        u8 c = s->ptr[i];
        if (c < '0' || c > '9' || v > (~(u64)0 - (c - '0')) / 10) {
            return 0;
        }
        v = v * 10 + (u64)(c - '0');
    }
    *out = v;
    return 1;
}

// splitmix64: each test's seed from the run's seed and the test's number.
static u64 mix(u64 x) {
    x += 0x9E3779B97F4A7C15UL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9UL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBUL;
    return x ^ (x >> 31);
}

static void say(const char *text) {
    a0rt_write(&out_fd, (const u8 *)text, c_strlen(text));
}

// What a test child wrote, kept until its result is shown.
struct Kept {
    u8 *data;
    u64 len, cap;
};

static void keep(Kept *k, const u8 *p, u64 n) {
    if (k->len + n > k->cap) {
        u64 cap = k->cap ? k->cap * 2 : 1024;
        while (cap < k->len + n) {
            cap *= 2;
        }
        u8 *grown = a0rt_alloc(cap);
        memcpy(grown, k->data, k->len);
        k->data = grown;
        k->cap = cap;
    }
    memcpy(k->data + k->len, p, n);
    k->len += n;
}

struct Run {
    i64 child;       // the child process: a process id, or a handle
    i64 out, err;    // where its output comes from, while it has not ended
    int status;
    int state;       // 0 not started, 1 running, 2 done, 3 shown
    Kept stdout_kept, stderr_kept;
};

// Runs the test numbered i with the run's seed, in the child process the
// system made for it (os_start_test).
static void run_test(System *sys, u64 i, u64 seed) {
    // A test says where it failed; what a run reports is the same for every backend.
    stack_trace_off = 1;
    const TestEntry *t = &a0_tests[i];
    // std.testing.Test: {seed, system}.
    u64 test[2] = {mix(seed ^ mix(i)), (u64)sys};
    a0rt_sched_seed(mix(test[0]));
    if (t->takes_test) {
        ((void (*)(u64 *))t->function)(test);
    } else {
        ((void (*)(void))t->function)();
    }
}

// Text as a JSON string; a byte that is not part of valid UTF-8 goes as U+FFFD.
static void say_json_string(const u8 *p, u64 n) {
    static const char hex[] = "0123456789abcdef";
    say("\"");
    u64 i = 0;
    while (i < n) {
        u8 c = p[i];
        if (c == '"' || c == '\\') {
            a0rt_write_byte(&out_fd, '\\');
            a0rt_write_byte(&out_fd, c);
            i++;
        } else if (c == '\n') {
            say("\\n");
            i++;
        } else if (c < 0x20 || c == 0x7f) {
            say("\\u00");
            a0rt_write_byte(&out_fd, (u8)hex[c >> 4]);
            a0rt_write_byte(&out_fd, (u8)hex[c & 15]);
            i++;
        } else if (c < 0x80) {
            a0rt_write_byte(&out_fd, c);
            i++;
        } else {
            u64 len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
            if (i + len <= n && valid_utf8(p + i, len)) {
                a0rt_write(&out_fd, p + i, len);
                i += len;
            } else {
                say("\\ufffd");
                i++;
            }
        }
    }
    say("\"");
}

u16 a0rt_run_tests(System *sys) {
    // On a system without fork, a child is this program started again for
    // one test: it runs that test here and never returns (windows.inc).
    os_test_child(sys);
    Str filter = {(const u8 *)"", 0};
    u64 seed = 0;
    int seeded = 0;
    int json = 0;
    u64 jobs = 0;
    for (u64 i = 1; i < sys->nargs; i++) {
        Str a = sys->args[i];
        if (i + 1 < sys->nargs && a.len == 8 && memcmp(a.ptr, "--filter", 8) == 0) {
            filter = sys->args[++i];
        } else if (i + 1 < sys->nargs && a.len == 6 && memcmp(a.ptr, "--seed", 6) == 0 && parse_u64(&sys->args[i + 1], &seed)) {
            seeded = 1;
            i++;
        } else if (i + 1 < sys->nargs && a.len == 6 && memcmp(a.ptr, "--jobs", 6) == 0 && parse_u64(&sys->args[i + 1], &jobs) && jobs > 0) {
            i++;
        } else if (a.len == 6 && memcmp(a.ptr, "--json", 6) == 0) {
            json = 1;
        } else {
            say("usage: <test binary> [--filter <text>] [--seed <number>] [--jobs <number>] [--json]\n");
            exit_with(2);
        }
    }
    if (jobs == 0) {
        jobs = os_processors();
    }
    // A child is waited for among the running ones, which some systems
    // count no higher than this.
    if (jobs > 64) {
        jobs = 64;
    }
    if (!seeded) {
        seed = mix(os_seed());
    }
    if (json) {
        say("{\"seed\": ");
        a0rt_write_u64(&out_fd, seed);
        say(", \"tests\": [");
    } else {
        say("seed: ");
        a0rt_write_u64(&out_fd, seed);
        say("\n");
    }
    flush_all();
    u64 count = test_count();
    Run *runs = a0rt_alloc((count ? count : 1) * sizeof(Run));
    u64 passed = 0, failed = 0, skipped = 0;
    u64 next = 0, shown = 0, running = 0;
    while (shown < count) {
        // Start tests while there is room.
        while (next < count && running < jobs) {
            const TestEntry *t = &a0_tests[next];
            if (!contains(t->name, t->name_len, filter.ptr, filter.len)) {
                runs[next].state = 3;
                skipped++;
            } else {
                os_start_test(sys, next, seed, &runs[next]);
                running++;
            }
            next++;
        }
        // Show the results that are next in order.
        while (shown < count && runs[shown].state >= 2) {
            Run *r = &runs[shown];
            if (r->state == 2) {
                const TestEntry *t = &a0_tests[shown];
                if (r->status == 0) {
                    passed++;
                } else {
                    failed++;
                }
                if (json) {
                    say(passed + failed > 1 ? ",\n  {\"name\": " : "\n  {\"name\": ");
                    say_json_string(t->name, t->name_len);
                    say(r->status == 0 ? ", \"ok\": true, \"stdout\": " : ", \"ok\": false, \"stdout\": ");
                    say_json_string(r->stdout_kept.data, r->stdout_kept.len);
                    say(", \"stderr\": ");
                    say_json_string(r->stderr_kept.data, r->stderr_kept.len);
                    say("}");
                } else {
                    say("test ");
                    a0rt_write(&out_fd, t->name, t->name_len);
                    say(r->status == 0 ? " ... ok\n" : " ... FAILED\n");
                    a0rt_write(&out_fd, r->stdout_kept.data, r->stdout_kept.len);
                    flush_all();
                    a0rt_write(&err_fd, r->stderr_kept.data, r->stderr_kept.len);
                }
                flush_all();
                r->state = 3;
                running--;
            }
            shown++;
        }
        if (shown < count && running > 0) {
            os_collect(runs, next);
        }
    }
    if (json) {
        say(passed + failed > 0 ? "\n], \"passed\": " : "], \"passed\": ");
        a0rt_write_u64(&out_fd, passed);
        say(", \"failed\": ");
        a0rt_write_u64(&out_fd, failed);
        say(", \"filtered\": ");
        a0rt_write_u64(&out_fd, skipped);
        say("}\n");
    } else {
        a0rt_write_u64(&out_fd, passed);
        say(" passed, ");
        a0rt_write_u64(&out_fd, failed);
        say(" failed, ");
        a0rt_write_u64(&out_fd, skipped);
        say(" filtered out\n");
    }
    if (failed) {
        if (!json) {
            say("rerun with --seed ");
            a0rt_write_u64(&out_fd, seed);
            say("\n");
        }
        exit_with(1);
    }
    return 0;
}

// ---- the system -----------------------------------------------------------------
//
// The half of the runtime that one system alone has: memory, files, the
// test runner's children, the switch between fibers, and the entry point
// the system starts the program at, which ends in run_program above.
#if defined(_WIN32)
#include "windows.inc"
#else
#include "linux.inc"
#endif
