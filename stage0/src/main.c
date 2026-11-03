// stage0: translates Arche-0 (spec/arche-0.md) to C11.
//
// Usage: arche0 <input.arche> -o <output.c>
//
// The translator is written in sprints S1-S4 (docs/sprints.md). Until then it
// rejects every program.

#include <stdio.h>
#include <string.h>

static int usage(void) {
    fprintf(stderr, "usage: arche0 <input.arche> -o <output.c>\n");
    return 2;
}

int main(int argc, char **argv) {
    if (argc != 4 || strcmp(argv[2], "-o") != 0) {
        return usage();
    }
    fprintf(stderr, "%s: error: stage0 cannot compile programs yet\n", argv[1]);
    return 1;
}
