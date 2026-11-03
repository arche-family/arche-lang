// stage0: translates Arche-0 (spec/arche-0.md) to C11.
//
// Usage: arche0 <input.arche> -o <output.c>
//
// The input is the root module; its directory is the package root.

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
        fatal("cannot open %s: %s", path, strerror(errno));
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

int main(int argc, char **argv) {
    if (argc != 4 || strcmp(argv[2], "-o") != 0) {
        return usage();
    }
    const char *path = argv[1];
    const char *slash = strrchr(path, '/');
    SourceFile *src = read_source(path, slash ? slash + 1 : path);

    Module *root = parse_module(src, "main");
    check_program(root);
    StrBuf c = {0};
    emit_program(root, &c);

    const char *out_path = argv[3];
    FILE *out = fopen(out_path, "wb");
    if (!out || fwrite(c.data, 1, c.len, out) != c.len || fclose(out) != 0) {
        fatal("cannot write %s: %s", out_path, strerror(errno));
    }
    return 0;
}
