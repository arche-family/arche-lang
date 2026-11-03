// Runtime of programs built by stage0. See arche0_rt.h.

#include "arche0_rt.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

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
