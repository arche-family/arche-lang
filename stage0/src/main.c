// stage0: translates Arche-0 (spec/arche-0.md) to C11.
//
// Usage: arche0 <input.arche> -o <output.c>
//
// The input is the root module and its directory is the package root (§10).
// Every module the program imports is loaded from there and the whole
// program becomes one C file.

#include "arche0.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void) {
    fprintf(stderr, "usage: arche0 <input.arche> -o <output.c>\n");
    return 2;
}

static SourceFile *read_source(const char *path, const char *display) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    StrBuf b = {0};
    sb_add(&b, "", 0);
    char chunk[65536];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
        sb_add(&b, chunk, n);
    }
    if (ferror(f)) {
        fatal("cannot read %s", path);
    }
    fclose(f);
    SourceFile *src = NEW(SourceFile);
    src->path = path;
    src->display = display;
    src->text = b.data;
    src->len = b.len;
    return src;
}

static bool is_identifier(const char *s) {
    if (!*s || (strcmp(s, "_") == 0)) {
        return false;
    }
    for (const char *c = s; *c; c++) {
        bool letter = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '_';
        if (!letter && !(c != s && *c >= '0' && *c <= '9')) {
            return false;
        }
    }
    return true;
}

static const char *join(const Vec *segments, const char *sep) {
    StrBuf b = {0};
    sb_add(&b, "", 0);
    for (size_t i = 0; i < segments->len; i++) {
        if (i > 0) {
            sb_puts(&b, sep);
        }
        sb_puts(&b, segments->items[i]);
    }
    return b.data;
}

static bool is_library(const char *first) {
    return strcmp(first, "std") == 0 || strcmp(first, "alloc") == 0 || strcmp(first, "core") == 0;
}

// Loads every module reachable from the root, breadth first, each once.
static void load_imports(Program *p, const char *root_dir) {
    for (size_t i = 0; i < p->modules.len; i++) {
        Module *m = p->modules.items[i];
        for (size_t j = 0; j < m->imports.len; j++) {
            Import *imp = m->imports.items[j];
            if (is_library(imp->path.items[0])) {
                continue; // the checker handles the library roots
            }
            const char *name = join(&imp->path, ".");
            bool loaded = false;
            for (size_t k = 0; k < p->modules.len && !loaded; k++) {
                loaded = strcmp(((Module *)p->modules.items[k])->name, name) == 0;
            }
            if (loaded) {
                continue;
            }
            const char *rel = arena_printf("%s.arche", join(&imp->path, "/"));
            const char *path = root_dir ? arena_printf("%s/%s", root_dir, rel) : rel;
            SourceFile *src = read_source(path, rel);
            if (!src) {
                fatal_at(imp->loc, "cannot find module '%s': %s: %s", name, path, strerror(errno));
            }
            vec_push(&p->modules, parse_module(src, name));
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 4 || strcmp(argv[2], "-o") != 0) {
        return usage();
    }
    const char *path = argv[1];
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    const char *root_dir = slash ? arena_strndup(path, (size_t)(slash - path)) : NULL;
    SourceFile *src = read_source(path, base);
    if (!src) {
        fatal("cannot open %s: %s", path, strerror(errno));
    }

    // The root module's path is its file name without the extension, when
    // that is a valid module path; otherwise nothing can import it.
    const char *dot = strrchr(base, '.');
    const char *stem = dot && strcmp(dot, ".arche") == 0 ? arena_strndup(base, (size_t)(dot - base)) : base;
    Program program = {0};
    program.root = parse_module(src, is_identifier(stem) ? stem : "");
    vec_push(&program.modules, program.root);
    load_imports(&program, root_dir);

    check_program(&program);
    StrBuf c = {0};
    emit_program(&program, &c);

    const char *out_path = argv[3];
    FILE *out = fopen(out_path, "wb");
    if (!out || fwrite(c.data, 1, c.len, out) != c.len || fclose(out) != 0) {
        fatal("cannot write %s: %s", out_path, strerror(errno));
    }
    return 0;
}
