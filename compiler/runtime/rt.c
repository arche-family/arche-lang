// The runtime of programs the Arche compiler builds (docs/abi.md).
//
// It calls no C library: it has its own entry point, talks to Linux through
// system calls only, and allocates from memory it maps itself. x86-64 Linux.
// Build: clang -O2 -ffreestanding -fno-builtin -nostdlib -fno-stack-protector

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;
typedef long i64;

// ---- system calls -------------------------------------------------------------

static i64 sys1(i64 n, i64 a) {
    i64 r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a) : "rcx", "r11", "memory");
    return r;
}

static i64 sys3(i64 n, i64 a, i64 b, i64 c) {
    i64 r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return r;
}

static i64 sys4(i64 n, i64 a, i64 b, i64 c, i64 d) {
    i64 r;
    register i64 r10 __asm__("r10") = d;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory");
    return r;
}

static i64 sys6(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    i64 r;
    register i64 r10 __asm__("r10") = d;
    register i64 r8 __asm__("r8") = e;
    register i64 r9 __asm__("r9") = f;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return r;
}

enum { SYS_READ = 0, SYS_WRITE = 1, SYS_CLOSE = 3, SYS_FSTAT = 5, SYS_MMAP = 9, SYS_EXIT_GROUP = 231, SYS_OPENAT = 257 };
enum { AT_FDCWD = -100, O_RDONLY = 0, O_WRONLY = 1, O_CREAT = 0100, O_TRUNC = 01000, O_CLOEXEC = 02000000 };
enum { ENOENT = 2, EACCES = 13, ENOTDIR = 20, EISDIR = 21, EPERM = 1 };

// ---- memory ---------------------------------------------------------------------

// The C compiler may turn loops into calls to these, and LLVM's memcpy and
// memset intrinsics call them.
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
// bumping a pointer. Nothing is freed: an Arche-0 program allocates for
// its whole run (spec/arche-0.md §13).
static u8 *heap_next;
static u64 heap_left;

static void *alloc_bytes(u64 n) {
    n = (n + 15) & ~(u64)15;
    if (n > heap_left) {
        u64 chunk = n > ((u64)64 << 20) ? n : ((u64)64 << 20);
        i64 p = sys6(SYS_MMAP, 0, (i64)chunk, 3 /* read, write */, 0x22 /* private, anonymous */, -1, 0);
        if (p < 0 && p > -4096) {
            a0rt_die("out of memory", 13);
        }
        heap_next = (u8 *)p;
        heap_left = chunk;
    }
    void *r = heap_next;
    heap_next += n;
    heap_left -= n;
    return r;
}

// Memory from mmap is zero, and stays so until handed out once.
void *a0rt_alloc(u64 n) {
    return alloc_bytes(n == 0 ? 1 : n);
}

void *a0rt_grow(void *old, u64 old_bytes, u64 new_bytes) {
    void *p = a0rt_alloc(new_bytes);
    if (old && old_bytes) {
        memcpy(p, old, old_bytes);
    }
    return p;
}

// Appends n bytes to a String or List(u8): {data, len, cap}.
void a0rt_bytes_push(u64 *list, const u8 *p, u64 n) {
    if (list[1] + n > list[2]) {
        u64 cap = list[2] ? list[2] : 16;
        while (cap < list[1] + n) {
            cap *= 2;
        }
        list[0] = (u64)a0rt_grow((void *)list[0], list[1], cap);
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
    list[0] = (u64)a0rt_grow((void *)list[0], list[2] * elem_size, cap * elem_size);
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
        i64 n = sys3(SYS_WRITE, b->fd, (i64)(b->data + done), (i64)(b->len - done));
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

// ---- exit -------------------------------------------------------------------------

static _Noreturn void exit_with(int status) {
    flush_all();
    sys1(SYS_EXIT_GROUP, status);
    __builtin_unreachable();
}

static const i64 err_fd = 2;

void a0rt_die(const char *message, u64 len) {
    flush_all();
    a0rt_write(&err_fd, (const u8 *)"panic: ", 7);
    a0rt_write(&err_fd, (const u8 *)message, len);
    a0rt_write(&err_fd, (const u8 *)"\n", 1);
    exit_with(101);
}

// panic and every trap: the message, then where it happened (§10).
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
    exit_with(101);
}

// ---- std.Fs -------------------------------------------------------------------------

static u16 io_error(i64 e) {
    switch (-e) {
    case ENOENT:
    case ENOTDIR:
        return 1; // NotFound
    case EACCES:
    case EPERM:
        return 2; // AccessDenied
    case EISDIR:
        return 3; // IsDirectory
    default:
        return 5; // IoFailed
    }
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
static char *c_path(const u8 *p, u64 n) {
    for (u64 i = 0; i < n; i++) {
        if (p[i] == 0) {
            return 0;
        }
    }
    char *s = a0rt_alloc(n + 1);
    memcpy(s, p, n);
    s[n] = 0;
    return s;
}

// Reads a whole file into a List(u8) or String {data, len, cap, alloc}.
static u16 read_into(const u8 *path, u64 path_len, u64 *out) {
    char *p = c_path(path, path_len);
    if (!p) {
        return 1;
    }
    i64 fd = sys4(SYS_OPENAT, AT_FDCWD, (i64)p, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) {
        return io_error(fd);
    }
    for (;;) {
        if (out[2] - out[1] < 65536) {
            u64 cap = out[2] ? out[2] * 2 : 65536;
            out[0] = (u64)a0rt_grow((void *)out[0], out[1], cap);
            out[2] = cap;
        }
        i64 n = sys3(SYS_READ, fd, (i64)(out[0] + out[1]), (i64)(out[2] - out[1]));
        if (n < 0) {
            sys1(SYS_CLOSE, fd);
            return io_error(n);
        }
        if (n == 0) {
            break;
        }
        out[1] += (u64)n;
    }
    sys1(SYS_CLOSE, fd);
    return 0;
}

// IoError!alloc.String and IoError!alloc.List(u8): {error: u16} and the
// value at offset 8.
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
    char *p = c_path(path, path_len);
    if (!p) {
        return 1;
    }
    i64 fd = sys4(SYS_OPENAT, AT_FDCWD, (i64)p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return io_error(fd);
    }
    u64 done = 0;
    while (done < len) {
        i64 n = sys3(SYS_WRITE, fd, (i64)(data + done), (i64)(len - done));
        if (n < 0) {
            sys1(SYS_CLOSE, fd);
            return io_error(n);
        }
        done += (u64)n;
    }
    sys1(SYS_CLOSE, fd);
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
    m->slots = a0rt_alloc(nslots * 8);
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
        m->entries = a0rt_grow(m->entries, m->cap * esize, cap * esize);
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
        in->slots = a0rt_alloc(nslots * 4);
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
        in->texts = a0rt_grow(in->texts, in->cap * sizeof(Str), cap * sizeof(Str));
        in->cap = cap;
    }
    u8 *copy = a0rt_alloc(n);
    memcpy(copy, p, n);
    in->texts[in->len].ptr = copy;
    in->texts[in->len].len = n;
    in->slots[slot] = (u32)(in->len + 1);
    return (u32)in->len++;
}

// ---- entry --------------------------------------------------------------------------

// std.System: {fs, stdout, stderr, args {items, len}, allocator}.
typedef struct {
    i64 fs;
    i64 stdout_fd;
    i64 stderr_fd;
    Str *args;
    u64 nargs;
    u64 allocator;
} System;

extern u16 a0_entry(System *sys);
extern const Str a0_error_names[];

static u64 c_strlen(const char *s) {
    u64 n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

_Noreturn void a0rt_start(i64 *sp) {
    buffers[1].fd = 1;
    buffers[2].fd = 2;
    i64 argc = sp[0];
    char **argv = (char **)(sp + 1);
    static System sys;
    sys.stdout_fd = 1;
    sys.stderr_fd = 2;
    sys.args = a0rt_alloc((u64)(argc > 0 ? argc : 1) * sizeof(Str));
    for (i64 i = 0; i < argc; i++) {
        u64 n = c_strlen(argv[i]);
        if (!valid_utf8((const u8 *)argv[i], n)) {
            a0rt_die("an argument is not valid UTF-8", 30);
        }
        sys.args[i].ptr = (const u8 *)argv[i];
        sys.args[i].len = n;
    }
    sys.nargs = (u64)argc;
    u16 err = a0_entry(&sys);
    if (err) {
        flush_all();
        a0rt_write(&err_fd, (const u8 *)"error: ", 7);
        a0rt_write(&err_fd, a0_error_names[err].ptr, a0_error_names[err].len);
        a0rt_write(&err_fd, (const u8 *)"\n", 1);
        exit_with(1);
    }
    exit_with(0);
}

// The kernel starts the program here, with argc at the top of the stack.
__asm__(".text\n"
        ".global _start\n"
        "_start:\n"
        "    xor %rbp, %rbp\n"
        "    mov %rsp, %rdi\n"
        "    and $-16, %rsp\n"
        "    call a0rt_start\n"
        "    hlt\n");
