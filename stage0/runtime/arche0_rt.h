// Runtime of programs built by stage0: spec/arche-0.md §10-§11.
// Written in portable C11 so that any C compiler can build stage0's output.

#ifndef ARCHE0_RT_H
#define ARCHE0_RT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    const uint8_t *ptr;
    uint64_t len;
} a0_str;

#define A0_STR(lit, n) ((a0_str){(const uint8_t *)(lit), (n)})

typedef struct {
    const char *file;
    uint32_t line;
    uint32_t col;
} a0_loc;

#define A0_LOC(file, line, col) ((a0_loc){(file), (line), (col)})

// The result of a function returning E!void: 0, or the error's number.
typedef uint16_t a0_err;

// ---- std --------------------------------------------------------------------

typedef struct {
    FILE *f;
} a0_std_Output;

typedef struct {
    uint64_t len;
    a0_str *items;
} a0_std_Args;

typedef struct {
    char unused;
} a0_std_Fs;

typedef struct {
    char unused;
} a0_alloc_Allocator;

// ---- alloc ------------------------------------------------------------------

typedef struct {
    uint8_t *data;
    uint64_t len;
    uint64_t cap;
    a0_alloc_Allocator alloc;
} a0_alloc_String;

typedef uint32_t a0_alloc_Symbol;

// Interned texts, each copied once; a symbol is its text's index.
typedef struct {
    a0_str *texts;
    uint64_t len;
    uint64_t cap;
    uint32_t *slots; // 0: empty, else a text's index + 1
    uint64_t nslots;
    a0_alloc_Allocator alloc;
} a0_alloc_Interner;

a0_alloc_String a0_alloc_String_init(a0_alloc_Allocator a);
void a0_alloc_String_push_str(a0_alloc_String *self, a0_str text);
void a0_alloc_String_push_ascii(a0_alloc_String *self, uint8_t byte, a0_loc loc);
a0_str a0_alloc_String_view(a0_alloc_String *self);
uint64_t a0_alloc_String_len(a0_alloc_String *self);

a0_alloc_Interner a0_alloc_Interner_init(a0_alloc_Allocator a);
a0_alloc_Symbol a0_alloc_Interner_intern(a0_alloc_Interner *self, a0_str text);
a0_str a0_alloc_Interner_text(a0_alloc_Interner *self, a0_alloc_Symbol sym, a0_loc loc);

typedef struct {
    a0_std_Fs fs;
    a0_std_Output out;
    a0_std_Output err;
    a0_std_Args args;
    a0_alloc_Allocator alloc;
} a0_std_System;

// std.IoError: its errors are numbered first (types.c).
#define A0_ERR_NOT_FOUND 1
#define A0_ERR_ACCESS_DENIED 2
#define A0_ERR_IS_DIRECTORY 3
#define A0_ERR_INVALID_UTF8 4
#define A0_ERR_IO_FAILED 5

// IoError!alloc.String; every E!alloc.String uses it.
typedef struct {
    a0_err err;
    a0_alloc_String value;
} a0_res_String;

a0_res_String a0_std_Fs_read_file(a0_std_Fs *self, a0_str path, a0_alloc_Allocator a);

// alloc.List(u8): the same fields as every list stage0 generates.
typedef struct a0_bytes {
    uint8_t *data;
    uint64_t len;
    uint64_t cap;
    a0_alloc_Allocator alloc;
} a0_bytes;

// IoError!alloc.List(u8); every E!alloc.List(u8) uses it.
typedef struct {
    a0_err err;
    a0_bytes value;
} a0_res_bytes;

a0_res_bytes a0_std_Fs_read_bytes(a0_std_Fs *self, a0_str path, a0_alloc_Allocator a);
a0_err a0_std_Fs_write_file(a0_std_Fs *self, a0_str path, a0_str data);
// data: the address of a []u8, {pointer, length}.
a0_err a0_std_Fs_write_bytes(a0_std_Fs *self, a0_str path, const void *data);

void a0_std_Output_write(a0_std_Output *self, a0_str text);
void a0_std_Output_write_byte(a0_std_Output *self, uint8_t byte);
void a0_std_Output_write_u64(a0_std_Output *self, uint64_t n);
void a0_std_Output_write_i64(a0_std_Output *self, int64_t n);

uint64_t a0_std_Args_len(a0_std_Args *self);
a0_str a0_std_Args_get(a0_std_Args *self, uint64_t i, a0_loc loc);

// ---- panics and traps -------------------------------------------------------

_Noreturn void a0_panic(a0_str message, a0_loc loc);
_Noreturn void a0_trap(const char *message, a0_loc loc);

_Noreturn void a0_trap_fit(const char *type, a0_loc loc);

// ---- str ----------------------------------------------------------------------

uint64_t a0_str_len(a0_str s);
bool a0_str_eq(a0_str s, a0_str other);
bool a0_str_starts_with(a0_str s, a0_str prefix);
uint8_t a0_str_at(a0_str s, uint64_t i, a0_loc loc);
a0_str a0_str_slice(a0_str s, uint64_t a, uint64_t b, a0_loc loc); // traps off a character boundary

// ---- indexing and memory (§7.7, §11) ------------------------------------------

static inline void a0_check_index(uint64_t i, uint64_t len, a0_loc loc) {
    if (i >= len) a0_trap("index out of range", loc);
}

static inline void a0_check_slice(uint64_t a, uint64_t b, uint64_t len, a0_loc loc) {
    if (a > b || b > len) a0_trap("slice out of range", loc);
}

// Resizes p to count elements of size bytes; traps when memory runs out.
void *a0_realloc(a0_alloc_Allocator a, void *p, uint64_t count, uint64_t size);

// ---- hashing Map keys ---------------------------------------------------------

static inline uint64_t a0_hash_word(uint64_t x) {
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}

static inline uint64_t a0_hash_mix(uint64_t h, uint64_t x) {
    return a0_hash_word(h ^ (x + UINT64_C(0x9e3779b97f4a7c15) + (h << 6) + (h >> 2)));
}

// ---- integers (§7.2) ------------------------------------------------------------
//
// Checked operators trap on overflow, division by zero and shifts at least as
// wide as the type. Narrow types compute in 64 bits and check the range; the
// 64-bit types check before they compute. Wrapping operators work on unsigned
// bits, and a0_from_bits_* turns bits back into a signed value with
// arithmetic only, so nothing depends on how C converts out-of-range values.

#define A0_OVERFLOW(loc) a0_trap("integer overflow", loc)
#define A0_DIV_ZERO(loc) a0_trap("division by zero", loc)
#define A0_SHIFT(loc) a0_trap("shift amount too large", loc)

#define A0_FROM_BITS(S, T, UT, MIN, MAX)                                                                               \
    static inline T a0_from_bits_##S(UT u) {                                                                           \
        return u <= (UT)MAX ? (T)u : (T)((T)(u - (UT)MAX - 1u) + MIN);                                                 \
    }

#define A0_SIGNED_COMMON(S, T, UT, MIN, MAX, BITS)                                                                     \
    static inline T a0_neg_##S(T a, a0_loc loc) {                                                                      \
        if (a == MIN) A0_OVERFLOW(loc);                                                                                \
        return (T)-a;                                                                                                  \
    }                                                                                                                  \
    static inline T a0_div_##S(T a, T b, a0_loc loc) {                                                                 \
        if (b == 0) A0_DIV_ZERO(loc);                                                                                  \
        if (a == MIN && b == -1) A0_OVERFLOW(loc);                                                                     \
        return (T)(a / b);                                                                                             \
    }                                                                                                                  \
    static inline T a0_rem_##S(T a, T b, a0_loc loc) {                                                                 \
        if (b == 0) A0_DIV_ZERO(loc);                                                                                  \
        if (b == -1) return 0;                                                                                         \
        return (T)(a % b);                                                                                             \
    }                                                                                                                  \
    static inline T a0_wadd_##S(T a, T b) {                                                                            \
        return a0_from_bits_##S((UT)((uint64_t)a + (uint64_t)b));                                                      \
    }                                                                                                                  \
    static inline T a0_wsub_##S(T a, T b) {                                                                            \
        return a0_from_bits_##S((UT)((uint64_t)a - (uint64_t)b));                                                      \
    }                                                                                                                  \
    static inline T a0_wmul_##S(T a, T b) {                                                                            \
        return a0_from_bits_##S((UT)((uint64_t)a * (uint64_t)b));                                                      \
    }                                                                                                                  \
    static inline T a0_shl_##S(T a, uint64_t n, a0_loc loc) {                                                          \
        if (n >= BITS) A0_SHIFT(loc);                                                                                  \
        return a0_from_bits_##S((UT)((uint64_t)(UT)a << n));                                                           \
    }                                                                                                                  \
    static inline T a0_shr_##S(T a, uint64_t n, a0_loc loc) {                                                          \
        if (n >= BITS) A0_SHIFT(loc);                                                                                  \
        return a >= 0 ? (T)(a >> n) : (T)~(~a >> n);                                                                   \
    }

#define A0_SIGNED_NARROW(S, T, UT, MIN, MAX, BITS)                                                                     \
    A0_FROM_BITS(S, T, UT, MIN, MAX)                                                                                   \
    A0_SIGNED_COMMON(S, T, UT, MIN, MAX, BITS)                                                                         \
    static inline T a0_add_##S(T a, T b, a0_loc loc) {                                                                 \
        int64_t r = (int64_t)a + b;                                                                                    \
        if (r < MIN || r > MAX) A0_OVERFLOW(loc);                                                                      \
        return (T)r;                                                                                                   \
    }                                                                                                                  \
    static inline T a0_sub_##S(T a, T b, a0_loc loc) {                                                                 \
        int64_t r = (int64_t)a - b;                                                                                    \
        if (r < MIN || r > MAX) A0_OVERFLOW(loc);                                                                      \
        return (T)r;                                                                                                   \
    }                                                                                                                  \
    static inline T a0_mul_##S(T a, T b, a0_loc loc) {                                                                 \
        int64_t r = (int64_t)a * b;                                                                                    \
        if (r < MIN || r > MAX) A0_OVERFLOW(loc);                                                                      \
        return (T)r;                                                                                                   \
    }

A0_SIGNED_NARROW(i8, int8_t, uint8_t, INT8_MIN, INT8_MAX, 8)
A0_SIGNED_NARROW(i16, int16_t, uint16_t, INT16_MIN, INT16_MAX, 16)
A0_SIGNED_NARROW(i32, int32_t, uint32_t, INT32_MIN, INT32_MAX, 32)
A0_FROM_BITS(i64, int64_t, uint64_t, INT64_MIN, INT64_MAX)
A0_SIGNED_COMMON(i64, int64_t, uint64_t, INT64_MIN, INT64_MAX, 64)

static inline int64_t a0_add_i64(int64_t a, int64_t b, a0_loc loc) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) A0_OVERFLOW(loc);
    return a + b;
}

static inline int64_t a0_sub_i64(int64_t a, int64_t b, a0_loc loc) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) A0_OVERFLOW(loc);
    return a - b;
}

static inline int64_t a0_mul_i64(int64_t a, int64_t b, a0_loc loc) {
    if (a > 0 ? (b > 0 ? a > INT64_MAX / b : b < INT64_MIN / a) : (b > 0 ? a < INT64_MIN / b : a != 0 && b < INT64_MAX / a)) {
        A0_OVERFLOW(loc);
    }
    return a * b;
}

#define A0_UNSIGNED_COMMON(S, T, BITS)                                                                                 \
    static inline T a0_sub_##S(T a, T b, a0_loc loc) {                                                                 \
        if (a < b) A0_OVERFLOW(loc);                                                                                   \
        return (T)(a - b);                                                                                             \
    }                                                                                                                  \
    static inline T a0_div_##S(T a, T b, a0_loc loc) {                                                                 \
        if (b == 0) A0_DIV_ZERO(loc);                                                                                  \
        return (T)(a / b);                                                                                             \
    }                                                                                                                  \
    static inline T a0_rem_##S(T a, T b, a0_loc loc) {                                                                 \
        if (b == 0) A0_DIV_ZERO(loc);                                                                                  \
        return (T)(a % b);                                                                                             \
    }                                                                                                                  \
    static inline T a0_wadd_##S(T a, T b) {                                                                            \
        return (T)((uint64_t)a + b);                                                                                   \
    }                                                                                                                  \
    static inline T a0_wsub_##S(T a, T b) {                                                                            \
        return (T)((uint64_t)a - b);                                                                                   \
    }                                                                                                                  \
    static inline T a0_wmul_##S(T a, T b) {                                                                            \
        return (T)((uint64_t)a * b);                                                                                   \
    }                                                                                                                  \
    static inline T a0_shl_##S(T a, uint64_t n, a0_loc loc) {                                                          \
        if (n >= BITS) A0_SHIFT(loc);                                                                                  \
        return (T)((uint64_t)a << n);                                                                                  \
    }                                                                                                                  \
    static inline T a0_shr_##S(T a, uint64_t n, a0_loc loc) {                                                          \
        if (n >= BITS) A0_SHIFT(loc);                                                                                  \
        return (T)(a >> n);                                                                                            \
    }

#define A0_UNSIGNED_NARROW(S, T, MAX, BITS)                                                                            \
    A0_UNSIGNED_COMMON(S, T, BITS)                                                                                     \
    static inline T a0_add_##S(T a, T b, a0_loc loc) {                                                                 \
        uint64_t r = (uint64_t)a + b;                                                                                  \
        if (r > MAX) A0_OVERFLOW(loc);                                                                                 \
        return (T)r;                                                                                                   \
    }                                                                                                                  \
    static inline T a0_mul_##S(T a, T b, a0_loc loc) {                                                                 \
        uint64_t r = (uint64_t)a * b;                                                                                  \
        if (r > MAX) A0_OVERFLOW(loc);                                                                                 \
        return (T)r;                                                                                                   \
    }

A0_UNSIGNED_NARROW(u8, uint8_t, UINT8_MAX, 8)
A0_UNSIGNED_NARROW(u16, uint16_t, UINT16_MAX, 16)
A0_UNSIGNED_NARROW(u32, uint32_t, UINT32_MAX, 32)
A0_UNSIGNED_COMMON(u64, uint64_t, 64)

static inline uint64_t a0_add_u64(uint64_t a, uint64_t b, a0_loc loc) {
    if (a + b < a) A0_OVERFLOW(loc);
    return a + b;
}

static inline uint64_t a0_mul_u64(uint64_t a, uint64_t b, a0_loc loc) {
    if (b != 0 && a > UINT64_MAX / b) A0_OVERFLOW(loc);
    return a * b;
}

// `as`: the value must fit in the target type, given by its range.
static inline int64_t a0_fit_ss(int64_t v, int64_t min, int64_t max, const char *type, a0_loc loc) {
    if (v < min || v > max) a0_trap_fit(type, loc);
    return v;
}

static inline uint64_t a0_fit_su(int64_t v, uint64_t max, const char *type, a0_loc loc) {
    if (v < 0 || (uint64_t)v > max) a0_trap_fit(type, loc);
    return (uint64_t)v;
}

static inline int64_t a0_fit_us(uint64_t v, int64_t max, const char *type, a0_loc loc) {
    if (v > (uint64_t)max) a0_trap_fit(type, loc);
    return (int64_t)v;
}

static inline uint64_t a0_fit_uu(uint64_t v, uint64_t max, const char *type, a0_loc loc) {
    if (v > max) a0_trap_fit(type, loc);
    return v;
}

// ---- provided by the generated program --------------------------------------

a0_err a0_entry(a0_std_System *sys);
extern const char *const a0_error_names[];

#endif
