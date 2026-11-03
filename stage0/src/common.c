// Arena, growable buffers, UTF-8 decoding and diagnostics.

#include "arche0.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Noreturn void out_of_memory(void) {
    fputs("arche0: out of memory\n", stderr);
    exit(3);
}

#define ARENA_CHUNK ((size_t)1 << 20)

static char *arena_next;
static size_t arena_left;

void *arena_alloc(size_t size) {
    size = (size + 15) & ~(size_t)15;
    if (size > arena_left) {
        size_t chunk = size > ARENA_CHUNK ? size : ARENA_CHUNK;
        arena_next = calloc(1, chunk);
        if (!arena_next) {
            out_of_memory();
        }
        arena_left = chunk;
    }
    void *p = arena_next;
    arena_next += size;
    arena_left -= size;
    return p;
}

char *arena_strndup(const char *s, size_t n) {
    char *r = arena_alloc(n + 1);
    memcpy(r, s, n);
    return r;
}

char *arena_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *s = arena_alloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

void sb_add(StrBuf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + n + 1) {
            cap *= 2;
        }
        b->data = realloc(b->data, cap);
        if (!b->data) {
            out_of_memory();
        }
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void sb_puts(StrBuf *b, const char *s) {
    sb_add(b, s, strlen(s));
}

void sb_printf(StrBuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char small[256];
    char *s = (size_t)n < sizeof small ? small : malloc((size_t)n + 1);
    if (!s) {
        out_of_memory();
    }
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    sb_add(b, s, (size_t)n);
    if (s != small) {
        free(s);
    }
}

void vec_push(Vec *v, void *item) {
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->items = realloc(v->items, v->cap * sizeof *v->items);
        if (!v->items) {
            out_of_memory();
        }
    }
    v->items[v->len++] = item;
}

size_t utf8_decode(const uint8_t *s, size_t n, uint32_t *cp) {
    if (n == 0) {
        return 0;
    }
    uint8_t b = s[0];
    if (b < 0x80) {
        *cp = b;
        return 1;
    }
    size_t len;
    uint32_t c;
    uint32_t min;
    if ((b & 0xE0) == 0xC0) {
        len = 2;
        c = b & 0x1F;
        min = 0x80;
    } else if ((b & 0xF0) == 0xE0) {
        len = 3;
        c = b & 0x0F;
        min = 0x800;
    } else if ((b & 0xF8) == 0xF0) {
        len = 4;
        c = b & 0x07;
        min = 0x10000;
    } else {
        return 0;
    }
    if (n < len) {
        return 0;
    }
    for (size_t i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            return 0;
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
        return 0;
    }
    *cp = c;
    return len;
}

void fatal_at(Loc loc, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%u:%u: error: ", loc.file->path, (unsigned)loc.line, (unsigned)loc.col);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

void fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("arche0: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}
