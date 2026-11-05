// Runtime of programs built by stage0. See arche0_rt.h.

#include "arche0_rt.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
// Windows opens the standard output and error in text mode, where the C
// library writes a carriage return before every newline; what an Arche
// program writes is bytes (docs/sprints.md S19, main below). The two calls
// that change a stream back to bytes are declared here, because io.h does
// not compile under -std=c11.
int _fileno(FILE *stream);
int _setmode(int fd, int mode);
#define A0_MODE_BINARY 0x8000
#endif

// ---- exit -------------------------------------------------------------------

// §10: outputs are flushed before the process exits, however it exits.
static void flush_outputs(void) {
    fflush(stdout);
    fflush(stderr);
}

static _Noreturn void die(const char *message, size_t len, const a0_loc *loc) {
    flush_outputs();
    fputs("panic: ", stderr);
    fwrite(message, 1, len, stderr);
    fputc('\n', stderr);
    if (loc) {
        fprintf(stderr, "  at %s:%" PRIu32 ":%" PRIu32 "\n", loc->file, loc->line, loc->col);
    }
    fflush(stderr);
    exit(101);
}

void a0_panic(a0_str message, a0_loc loc) {
    die((const char *)message.ptr, (size_t)message.len, &loc);
}

void a0_trap(const char *message, a0_loc loc) {
    die(message, strlen(message), &loc);
}

void a0_trap_fit(const char *type, a0_loc loc) {
    char message[64];
    int n = snprintf(message, sizeof message, "value does not fit in %s", type);
    die(message, (size_t)n, &loc);
}

// ---- UTF-8 ------------------------------------------------------------------

static bool valid_utf8(const uint8_t *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t b = s[i];
        size_t len;
        uint32_t c;
        uint32_t min;
        if (b < 0x80) {
            i++;
            continue;
        } else if ((b & 0xE0) == 0xC0) {
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
            return false;
        }
        if (n - i < len) {
            return false;
        }
        for (size_t k = 1; k < len; k++) {
            if ((s[i + k] & 0xC0) != 0x80) {
                return false;
            }
            c = (c << 6) | (s[i + k] & 0x3F);
        }
        if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
            return false;
        }
        i += len;
    }
    return true;
}

// ---- str ----------------------------------------------------------------------

uint64_t a0_str_len(a0_str s) {
    return s.len;
}

bool a0_str_eq(a0_str s, a0_str other) {
    return s.len == other.len && (s.len == 0 || memcmp(s.ptr, other.ptr, (size_t)s.len) == 0);
}

bool a0_str_starts_with(a0_str s, a0_str prefix) {
    return s.len >= prefix.len && (prefix.len == 0 || memcmp(s.ptr, prefix.ptr, (size_t)prefix.len) == 0);
}

uint8_t a0_str_at(a0_str s, uint64_t i, a0_loc loc) {
    if (i >= s.len) {
        a0_trap("index out of range", loc);
    }
    return s.ptr[i];
}

static bool char_boundary(a0_str s, uint64_t i) {
    return i == 0 || i == s.len || (s.ptr[i] & 0xC0) != 0x80;
}

a0_str a0_str_slice(a0_str s, uint64_t a, uint64_t b, a0_loc loc) {
    if (a > b || b > s.len) {
        a0_trap("slice out of range", loc);
    }
    if (!char_boundary(s, a) || !char_boundary(s, b)) {
        a0_trap("slice is not on a character boundary", loc);
    }
    return (a0_str){s.ptr + a, b - a};
}

// ---- memory -----------------------------------------------------------------

void *a0_realloc(a0_alloc_Allocator a, void *p, uint64_t count, uint64_t size) {
    (void)a;
    if (size != 0 && count > SIZE_MAX / size) {
        die("out of memory", 13, NULL);
    }
    void *q = realloc(p, (size_t)(count * size));
    if (!q && count * size != 0) {
        die("out of memory", 13, NULL);
    }
    return q;
}

// ---- alloc.String -------------------------------------------------------------

a0_alloc_String a0_alloc_String_init(a0_alloc_Allocator a) {
    a0_alloc_String s = {0};
    s.alloc = a;
    return s;
}

static void string_reserve(a0_alloc_String *self, uint64_t extra) {
    if (self->len + extra <= self->cap) {
        return;
    }
    uint64_t cap = self->cap ? self->cap : 16;
    while (cap < self->len + extra) {
        cap *= 2;
    }
    self->data = a0_realloc(self->alloc, self->data, cap, 1);
    self->cap = cap;
}

void a0_alloc_String_push_str(a0_alloc_String *self, a0_str text) {
    if (text.len == 0) {
        return;
    }
    string_reserve(self, text.len);
    memcpy(self->data + self->len, text.ptr, (size_t)text.len);
    self->len += text.len;
}

void a0_alloc_String_push_ascii(a0_alloc_String *self, uint8_t byte, a0_loc loc) {
    if (byte > 0x7F) {
        a0_trap("push_ascii takes ASCII, a byte up to 0x7F", loc);
    }
    string_reserve(self, 1);
    self->data[self->len++] = byte;
}

a0_str a0_alloc_String_view(a0_alloc_String *self) {
    return (a0_str){self->data, self->len};
}

uint64_t a0_alloc_String_len(a0_alloc_String *self) {
    return self->len;
}

// ---- alloc.Interner -----------------------------------------------------------

static uint64_t hash_text(a0_str s) {
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (uint64_t i = 0; i < s.len; i++) {
        h = (h ^ s.ptr[i]) * UINT64_C(0x100000001b3);
    }
    return h;
}

a0_alloc_Interner a0_alloc_Interner_init(a0_alloc_Allocator a) {
    a0_alloc_Interner in = {0};
    in.alloc = a;
    return in;
}

// The slot of text: where it is, or the empty slot where it would go.
static uint64_t interner_slot(a0_alloc_Interner *self, a0_str text) {
    uint64_t mask = self->nslots - 1;
    uint64_t i = hash_text(text) & mask;
    for (;;) {
        uint32_t s = self->slots[i];
        if (s == 0 || a0_str_eq(self->texts[s - 1], text)) {
            return i;
        }
        i = (i + 1) & mask;
    }
}

a0_alloc_Symbol a0_alloc_Interner_intern(a0_alloc_Interner *self, a0_str text) {
    if ((self->len + 1) * 2 > self->nslots) {
        uint64_t nslots = self->nslots ? self->nslots * 2 : 64;
        self->slots = a0_realloc(self->alloc, self->slots, nslots, sizeof *self->slots);
        self->nslots = nslots;
        for (uint64_t i = 0; i < nslots; i++) {
            self->slots[i] = 0;
        }
        for (uint64_t i = 0; i < self->len; i++) {
            self->slots[interner_slot(self, self->texts[i])] = (uint32_t)(i + 1);
        }
    }
    uint64_t slot = interner_slot(self, text);
    if (self->slots[slot] != 0) {
        return self->slots[slot] - 1;
    }
    if (self->len == UINT32_MAX - 1) {
        die("too many interned texts", 23, NULL);
    }
    if (self->len == self->cap) {
        self->cap = self->cap ? self->cap * 2 : 64;
        self->texts = a0_realloc(self->alloc, self->texts, self->cap, sizeof *self->texts);
    }
    uint8_t *copy = a0_realloc(self->alloc, NULL, text.len ? text.len : 1, 1);
    if (text.len > 0) {
        memcpy(copy, text.ptr, (size_t)text.len);
    }
    self->texts[self->len] = (a0_str){copy, text.len};
    self->slots[slot] = (uint32_t)(self->len + 1);
    return (a0_alloc_Symbol)self->len++;
}

a0_str a0_alloc_Interner_text(a0_alloc_Interner *self, a0_alloc_Symbol sym, a0_loc loc) {
    if (sym >= self->len) {
        a0_trap("this symbol is not from this interner", loc);
    }
    return self->texts[sym];
}

// ---- std.Fs -------------------------------------------------------------------

#if defined(_WIN32)
// Windows refuses to open a directory as a file with the same error it
// uses for a file one may not read, so the two are told apart by the
// attributes. The call is declared here to keep windows.h out of a file
// that every C compiler has to build.
unsigned long GetFileAttributesA(const char *path);

static int is_directory(const char *path) {
    unsigned long attributes = GetFileAttributesA(path);
    return attributes != 0xFFFFFFFFul && (attributes & 0x10u) != 0;
}
#else
static int is_directory(const char *path) {
    (void)path;
    return 0;
}
#endif

static a0_err io_error(int e) {
    switch (e) {
    case ENOENT:
    case ENOTDIR:
        return A0_ERR_NOT_FOUND;
    case EACCES:
    case EPERM:
        return A0_ERR_ACCESS_DENIED;
    case EISDIR:
        return A0_ERR_IS_DIRECTORY;
    default:
        return A0_ERR_IO_FAILED;
    }
}

// A NUL-terminated copy of a path, or NULL when the path holds a NUL byte.
static char *c_path(a0_str path) {
    if (path.len > 0 && memchr(path.ptr, 0, (size_t)path.len)) {
        return NULL;
    }
    char *p = malloc((size_t)path.len + 1);
    if (!p) {
        die("out of memory", 13, NULL);
    }
    memcpy(p, path.ptr, (size_t)path.len);
    p[path.len] = '\0';
    return p;
}

a0_res_String a0_std_Fs_read_file(a0_std_Fs *self, a0_str path, a0_alloc_Allocator a) {
    (void)self;
    a0_res_String r = {0};
    r.value = a0_alloc_String_init(a);
    char *p = c_path(path);
    if (!p) {
        r.err = A0_ERR_NOT_FOUND;
        return r;
    }
    errno = 0;
    FILE *f = fopen(p, "rb");
    if (!f) {
        r.err = is_directory(p) ? A0_ERR_IS_DIRECTORY : io_error(errno);
        free(p);
        return r;
    }
    free(p);
    uint8_t chunk[65536];
    size_t n;
    errno = 0;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
        a0_alloc_String_push_str(&r.value, (a0_str){chunk, n});
    }
    if (ferror(f)) {
        r.err = io_error(errno);
    } else if (!valid_utf8(r.value.data, (size_t)r.value.len)) {
        r.err = A0_ERR_INVALID_UTF8;
    }
    fclose(f);
    return r;
}

a0_res_bytes a0_std_Fs_read_bytes(a0_std_Fs *self, a0_str path, a0_alloc_Allocator a) {
    (void)self;
    a0_res_bytes r = {0};
    r.value.alloc = a;
    char *p = c_path(path);
    if (!p) {
        r.err = A0_ERR_NOT_FOUND;
        return r;
    }
    errno = 0;
    FILE *f = fopen(p, "rb");
    if (!f) {
        r.err = is_directory(p) ? A0_ERR_IS_DIRECTORY : io_error(errno);
        free(p);
        return r;
    }
    free(p);
    uint8_t chunk[65536];
    size_t n;
    errno = 0;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
        if (r.value.len + n > r.value.cap) {
            uint64_t cap = r.value.cap ? r.value.cap : 4096;
            while (cap < r.value.len + n) {
                cap *= 2;
            }
            r.value.data = a0_realloc(a, r.value.data, cap, 1);
            r.value.cap = cap;
        }
        memcpy(r.value.data + r.value.len, chunk, n);
        r.value.len += n;
    }
    if (ferror(f)) {
        r.err = io_error(errno);
    }
    fclose(f);
    return r;
}

a0_err a0_std_Fs_write_file(a0_std_Fs *self, a0_str path, a0_str data) {
    (void)self;
    char *p = c_path(path);
    if (!p) {
        return A0_ERR_NOT_FOUND;
    }
    errno = 0;
    FILE *f = fopen(p, "wb");
    if (!f) {
        a0_err e = is_directory(p) ? A0_ERR_IS_DIRECTORY : io_error(errno);
        free(p);
        return e;
    }
    free(p);
    bool ok = data.len == 0 || fwrite(data.ptr, 1, (size_t)data.len, f) == data.len;
    int e = errno;
    if (fclose(f) != 0 || !ok) {
        return io_error(ok ? errno : e);
    }
    return 0;
}

// ---- std.Output -------------------------------------------------------------

void a0_std_Output_write(a0_std_Output *self, a0_str text) {
    fwrite(text.ptr, 1, (size_t)text.len, self->f);
}

void a0_std_Output_write_byte(a0_std_Output *self, uint8_t byte) {
    fputc(byte, self->f);
}

void a0_std_Output_write_u64(a0_std_Output *self, uint64_t n) {
    fprintf(self->f, "%" PRIu64, n);
}

void a0_std_Output_write_i64(a0_std_Output *self, int64_t n) {
    fprintf(self->f, "%" PRId64, n);
}

// ---- std.Args -----------------------------------------------------------------

uint64_t a0_std_Args_len(a0_std_Args *self) {
    return self->len;
}

a0_str a0_std_Args_get(a0_std_Args *self, uint64_t i, a0_loc loc) {
    if (i >= self->len) {
        a0_trap("argument index out of range", loc);
    }
    return self->items[i];
}

// ---- entry ------------------------------------------------------------------

int main(int argc, char **argv) {
    static a0_std_System sys;
#if defined(_WIN32)
    // The bytes a program writes are its own, with no carriage return
    // added to them.
    _setmode(_fileno(stdout), A0_MODE_BINARY);
    _setmode(_fileno(stderr), A0_MODE_BINARY);
#endif
    sys.out.f = stdout;
    sys.err.f = stderr;

    a0_str *items = malloc((size_t)(argc > 0 ? argc : 1) * sizeof *items);
    if (!items) {
        die("out of memory", 13, NULL);
    }
    for (int i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]);
        if (!valid_utf8((const uint8_t *)argv[i], len)) {
            char message[64];
            int n = snprintf(message, sizeof message, "argument %d is not valid UTF-8", i);
            die(message, (size_t)n, NULL);
        }
        items[i] = (a0_str){(const uint8_t *)argv[i], len};
    }
    sys.args.len = (uint64_t)argc;
    sys.args.items = items;

    a0_err err = a0_entry(&sys);
    flush_outputs();
    if (err != 0) {
        fprintf(stderr, "error: %s\n", a0_error_names[err]);
        return 1;
    }
    return 0;
}
