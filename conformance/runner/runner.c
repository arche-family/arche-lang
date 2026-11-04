// Conformance test runner. The test format is described in conformance/README.md.
//
// Usage: runner --compiler <command> [--stage0] [--codes <prefixes>]
//               [--timeout <seconds>] [--filter <text>] <path>...
//
// Written in C11 + POSIX because it has to work before Arche does. `arche test`
// replaces it in S23 (docs/sprints.md).

#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// ---- utilities ----------------------------------------------------------

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("runner: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(2);
}

static void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) {
        die("out of memory");
    }
    return p;
}

static char *xstrndup(const char *s, size_t n) {
    char *r = xrealloc(NULL, n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static char *xformat(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        die("format error");
    }
    char *s = xrealloc(NULL, (size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buf;

static void buf_add(Buf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        while (cap < b->len + n + 1) {
            cap *= 2;
        }
        b->data = xrealloc(b->data, cap);
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void buf_free(Buf *b) {
    free(b->data);
    *b = (Buf){0};
}

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} List;

static void list_push(List *l, char *s) {
    if (l->len == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->items = xrealloc(l->items, l->cap * sizeof *l->items);
    }
    l->items[l->len++] = s;
}

static void list_free(List *l) {
    for (size_t i = 0; i < l->len; i++) {
        free(l->items[i]);
    }
    free(l->items);
    *l = (List){0};
}

// Reads a whole file. The buffer is always NUL-terminated, even when empty.
static bool read_file(const char *path, Buf *out) {
    buf_add(out, "", 0);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    char chunk[8192];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
        buf_add(out, chunk, n);
    }
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

// Position of `needle` in `hay` at or after `from`, or -1.
static long find(const Buf *hay, size_t from, const char *needle) {
    size_t n = strlen(needle);
    for (size_t i = from; i + n <= hay->len; i++) {
        if (memcmp(hay->data + i, needle, n) == 0) {
            return (long)i;
        }
    }
    return -1;
}

// `code` appears in `text` as a whole word: E0001 does not match E00012.
static bool has_code(const Buf *text, const char *code) {
    size_t n = strlen(code);
    long at = 0;
    while ((at = find(text, (size_t)at, code)) >= 0) {
        size_t i = (size_t)at;
        bool before = i > 0 && isalnum((unsigned char)text->data[i - 1]);
        bool after = i + n < text->len && isalnum((unsigned char)text->data[i + n]);
        if (!before && !after) {
            return true;
        }
        at++;
    }
    return false;
}

static bool parse_uint(const char *s, long max, long *out) {
    if (!*s) {
        return false;
    }
    long v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        int d = *s - '0';
        if (v > (max - d) / 10) {
            return false;
        }
        v = v * 10 + d;
    }
    *out = v;
    return true;
}

// ---- test header ----------------------------------------------------------

enum { K_TEST, K_SPEC, K_STAGE0, K_EXIT, K_STDOUT, K_STDERR, K_ARGS, K_LINE, K_CODE, KEY_COUNT };

static const char *const key_names[KEY_COUNT] = {
    "test", "spec", "stage0", "exit", "stdout", "stderr", "args", "line", "code",
};

static const bool key_repeats[KEY_COUNT] = {
    [K_STDOUT] = true,
    [K_STDERR] = true,
    [K_ARGS] = true,
};

typedef enum { KIND_RUN, KIND_COMPILE_ERROR } Kind;

typedef struct {
    bool seen[KEY_COUNT];
    Kind kind;
    char *spec;
    bool stage0_skip;
    int exit_code;
    Buf stdout_text;
    List stderr_parts;
    List args;
    long line;
    char *code;
} Test;

static void test_free(Test *t) {
    free(t->spec);
    buf_free(&t->stdout_text);
    list_free(&t->stderr_parts);
    list_free(&t->args);
    free(t->code);
}

// Applies one `// key: value` line. Takes ownership of `value` when it keeps it.
static bool apply_key(Test *t, int key, char **value) {
    char *v = *value;
    switch (key) {
    case K_TEST:
        if (strcmp(v, "run") == 0) {
            t->kind = KIND_RUN;
            return true;
        }
        if (strcmp(v, "compile-error") == 0) {
            t->kind = KIND_COMPILE_ERROR;
            return true;
        }
        return false;
    case K_SPEC:
        if (!*v) {
            return false;
        }
        t->spec = v;
        *value = NULL;
        return true;
    case K_STAGE0:
        t->stage0_skip = strcmp(v, "skip") == 0;
        return t->stage0_skip;
    case K_EXIT: {
        long n;
        if (!parse_uint(v, 255, &n)) {
            return false;
        }
        t->exit_code = (int)n;
        return true;
    }
    case K_STDOUT:
        buf_add(&t->stdout_text, v, strlen(v));
        buf_add(&t->stdout_text, "\n", 1);
        return true;
    case K_STDERR:
        if (!*v) {
            return false;
        }
        list_push(&t->stderr_parts, v);
        *value = NULL;
        return true;
    case K_ARGS: {
        size_t before = t->args.len;
        const char *p = v;
        while (*p) {
            while (*p == ' ') {
                p++;
            }
            const char *start = p;
            while (*p && *p != ' ') {
                p++;
            }
            if (p > start) {
                list_push(&t->args, xstrndup(start, (size_t)(p - start)));
            }
        }
        return t->args.len > before;
    }
    case K_LINE:
        return parse_uint(v, LONG_MAX, &t->line) && t->line > 0;
    case K_CODE:
        if (!*v) {
            return false;
        }
        t->code = v;
        *value = NULL;
        return true;
    }
    return false;
}

// The header is the run of `// key: value` lines at the start of the file.
static bool parse_header(const Buf *src, Test *t, char *err, size_t errlen) {
    buf_add(&t->stdout_text, "", 0);
    size_t pos = 0;
    long lineno = 0;
    while (pos < src->len) {
        const char *line = src->data + pos;
        const char *nl = memchr(line, '\n', src->len - pos);
        size_t n = nl ? (size_t)(nl - line) : src->len - pos;
        lineno++;
        if (n < 4 || memcmp(line, "// ", 3) != 0) {
            break;
        }
        size_t k = 3;
        while (k < n && (islower((unsigned char)line[k]) || isdigit((unsigned char)line[k]) || line[k] == '-')) {
            k++;
        }
        if (k == 3 || k >= n || line[k] != ':') {
            break;
        }
        const char *name = line + 3;
        int name_len = (int)(k - 3);
        size_t v = k + 1;
        if (v < n && line[v] == ' ') {
            v++;
        }

        int key = -1;
        for (int i = 0; i < KEY_COUNT; i++) {
            if ((int)strlen(key_names[i]) == name_len && memcmp(key_names[i], name, (size_t)name_len) == 0) {
                key = i;
            }
        }
        if (key < 0) {
            snprintf(err, errlen, "line %ld: unknown key '%.*s'", lineno, name_len, name);
            return false;
        }
        if (t->seen[key] && !key_repeats[key]) {
            snprintf(err, errlen, "line %ld: duplicate '%s'", lineno, key_names[key]);
            return false;
        }
        t->seen[key] = true;

        char *value = xstrndup(line + v, n - v);
        if (!apply_key(t, key, &value)) {
            snprintf(err, errlen, "line %ld: invalid value for '%s': '%s'", lineno, key_names[key], value);
            free(value);
            return false;
        }
        free(value);
        pos += n + (nl ? 1 : 0);
    }

    if (!t->seen[K_TEST]) {
        snprintf(err, errlen, "missing 'test'");
        return false;
    }
    if (!t->seen[K_SPEC]) {
        snprintf(err, errlen, "missing 'spec'");
        return false;
    }
    static const int run_only[] = {K_EXIT, K_STDOUT, K_STDERR, K_ARGS};
    static const int error_only[] = {K_LINE, K_CODE};
    if (t->kind == KIND_RUN) {
        for (size_t i = 0; i < sizeof error_only / sizeof *error_only; i++) {
            if (t->seen[error_only[i]]) {
                snprintf(err, errlen, "'%s' is allowed only with 'test: compile-error'", key_names[error_only[i]]);
                return false;
            }
        }
    } else {
        for (size_t i = 0; i < sizeof run_only / sizeof *run_only; i++) {
            if (t->seen[run_only[i]]) {
                snprintf(err, errlen, "'%s' is allowed only with 'test: run'", key_names[run_only[i]]);
                return false;
            }
        }
        if (!t->seen[K_LINE]) {
            snprintf(err, errlen, "missing 'line'");
            return false;
        }
    }
    return true;
}

// ---- processes ----------------------------------------------------------

typedef struct {
    bool timed_out;
    bool signaled;
    int status; // exit status, or the signal number when `signaled`
} Outcome;

// Runs argv in its own process group with stdin from /dev/null and output in
// files. On timeout, or when the process exits, the whole group is killed, so
// children left behind by a compiler script do not outlive the test.
static Outcome run_process(char *const argv[], const char *cwd, const char *out_path, const char *err_path,
                           int timeout_s) {
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) {
        die("fork: %s", strerror(errno));
    }
    if (pid == 0) {
        setpgid(0, 0);
        int in = open("/dev/null", O_RDONLY);
        int out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        int err = strcmp(out_path, err_path) == 0 ? out : open(err_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (in < 0 || out < 0 || err < 0) {
            _exit(126);
        }
        dup2(in, STDIN_FILENO);
        dup2(out, STDOUT_FILENO);
        dup2(err, STDERR_FILENO);
        if (cwd && chdir(cwd) != 0) {
            fprintf(stderr, "runner: cannot enter %s: %s\n", cwd, strerror(errno));
            _exit(126);
        }
        execvp(argv[0], argv);
        fprintf(stderr, "runner: cannot run %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    setpgid(pid, pid);

    Outcome o = {0};
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        // WNOWAIT leaves the child a zombie, so its process group id stays
        // reserved until the group is killed below.
        siginfo_t info;
        memset(&info, 0, sizeof info);
        if (waitid(P_PID, (id_t)pid, &info, WEXITED | WNOHANG | WNOWAIT) == 0) {
            if (info.si_pid == pid) {
                break;
            }
        } else if (errno != EINTR) {
            die("waitid: %s", strerror(errno));
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (double)(now.tv_sec - start.tv_sec) + (double)(now.tv_nsec - start.tv_nsec) / 1e9;
        if (elapsed >= timeout_s) {
            o.timed_out = true;
            break;
        }
        struct timespec pause = {0, 5 * 1000 * 1000};
        nanosleep(&pause, NULL);
    }
    kill(-pid, SIGKILL);

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            die("waitpid: %s", strerror(errno));
        }
    }
    if (WIFEXITED(status)) {
        o.status = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        o.signaled = true;
        o.status = WTERMSIG(status);
    }
    return o;
}

static int remove_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw) {
    (void)sb;
    (void)flag;
    if (ftw->level > 0 && remove(path) != 0) {
        die("cannot remove %s: %s", path, strerror(errno));
    }
    return 0;
}

// Empties `dir`, keeping the directory itself.
static void clear_dir(const char *dir) {
    if (nftw(dir, remove_entry, 16, FTW_DEPTH | FTW_PHYS) != 0) {
        die("cannot clear %s: %s", dir, strerror(errno));
    }
}

// ---- reporting ----------------------------------------------------------

static void verdict(const char *word, const char *path, const char *fmt, ...) {
    printf("%s %s", word, path);
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        fputs(": ", stdout);
        vprintf(fmt, ap);
        va_end(ap);
    }
    putchar('\n');
}

// Prints output as indented lines, at most 20 of them.
static void show_lines(const Buf *b) {
    if (b->len == 0) {
        puts("  (no output)");
        return;
    }
    size_t pos = 0;
    int shown = 0;
    while (pos < b->len) {
        if (shown == 20) {
            puts("  | ...");
            return;
        }
        const char *line = b->data + pos;
        const char *nl = memchr(line, '\n', b->len - pos);
        size_t n = nl ? (size_t)(nl - line) : b->len - pos;
        printf("  | %.*s\n", (int)n, line);
        pos += n + (nl ? 1 : 0);
        shown++;
    }
}

// Prints `label: "text"` with C-style escapes, cut at 400 bytes.
static void show_escaped(const char *label, const Buf *b) {
    size_t limit = b->len > 400 ? 400 : b->len;
    printf("  %s \"", label);
    for (size_t i = 0; i < limit; i++) {
        unsigned char c = (unsigned char)b->data[i];
        switch (c) {
        case '\n':
            fputs("\\n", stdout);
            break;
        case '\t':
            fputs("\\t", stdout);
            break;
        case '\\':
            fputs("\\\\", stdout);
            break;
        case '"':
            fputs("\\\"", stdout);
            break;
        default:
            if (c < 0x20 || c == 0x7F) {
                printf("\\x%02X", c);
            } else {
                putchar(c);
            }
        }
    }
    putchar('"');
    if (b->len > limit) {
        printf(" (%zu more bytes)", b->len - limit);
    }
    putchar('\n');
}

// ---- running a test -------------------------------------------------------

typedef struct {
    char *compiler;
    bool stage0;
    int timeout;
    const char *filter;
    const char *codes; // comma-separated code prefixes, or NULL
} Options;

// With --codes, only compile-error tests whose code starts with one of the
// prefixes run: for a compiler that checks programs but cannot build them yet.
static bool code_selected(const Options *o, const Test *t) {
    if (!o->codes) {
        return true;
    }
    if (t->kind != KIND_COMPILE_ERROR || !t->code) {
        return false;
    }
    const char *p = o->codes;
    while (*p) {
        const char *end = strchr(p, ',');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n > 0 && strncmp(t->code, p, n) == 0) {
            return true;
        }
        p += n + (end ? 1 : 0);
    }
    return false;
}

typedef enum { PASS, FAIL, SKIP } Result;

static char *dir_of(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        return xstrndup(".", 1);
    }
    if (slash == path) {
        return xstrndup("/", 1);
    }
    return xstrndup(path, (size_t)(slash - path));
}

static Result check_compile_error(const char *path, const Test *t, const Options *o, const Buf *diag) {
    char *needle = xformat("%s:%ld:", path, t->line);
    bool at_line = find(diag, 0, needle) >= 0;
    free(needle);
    if (!at_line) {
        verdict("FAIL", path, "diagnostics do not point at %s:%ld", path, t->line);
        show_lines(diag);
        return FAIL;
    }
    if (t->code && !o->stage0 && !has_code(diag, t->code)) {
        verdict("FAIL", path, "diagnostics do not mention %s", t->code);
        show_lines(diag);
        return FAIL;
    }
    verdict("PASS", path, NULL);
    return PASS;
}

static Result check_run(const char *path, const Test *t, const Options *o, const char *tmp, const char *exe) {
    char **argv = xrealloc(NULL, (t->args.len + 2) * sizeof *argv);
    argv[0] = (char *)exe;
    for (size_t i = 0; i < t->args.len; i++) {
        argv[i + 1] = t->args.items[i];
    }
    argv[t->args.len + 1] = NULL;
    char *cwd = dir_of(path);
    char *out_path = xformat("%s/stdout", tmp);
    char *err_path = xformat("%s/stderr", tmp);
    Outcome r = run_process(argv, cwd, out_path, err_path, o->timeout);
    Buf out = {0};
    Buf err = {0};
    read_file(out_path, &out);
    read_file(err_path, &err);
    free(argv);
    free(cwd);
    free(out_path);
    free(err_path);

    Result result = FAIL;
    if (r.timed_out) {
        verdict("FAIL", path, "timed out after %d s", o->timeout);
    } else if (r.signaled) {
        verdict("FAIL", path, "killed by signal %d", r.status);
        show_lines(&err);
    } else if (r.status != t->exit_code) {
        verdict("FAIL", path, "exit status %d, expected %d", r.status, t->exit_code);
        show_lines(&err);
    } else if (out.len != t->stdout_text.len || memcmp(out.data, t->stdout_text.data, out.len) != 0) {
        verdict("FAIL", path, "stdout differs");
        show_escaped("expected:", &t->stdout_text);
        show_escaped("actual:  ", &out);
    } else {
        size_t from = 0;
        const char *missing = NULL;
        for (size_t i = 0; i < t->stderr_parts.len && !missing; i++) {
            long at = find(&err, from, t->stderr_parts.items[i]);
            if (at < 0) {
                missing = t->stderr_parts.items[i];
            } else {
                from = (size_t)at + strlen(t->stderr_parts.items[i]);
            }
        }
        if (missing) {
            verdict("FAIL", path, "stderr does not contain \"%s\"", missing);
            show_lines(&err);
        } else {
            verdict("PASS", path, NULL);
            result = PASS;
        }
    }
    buf_free(&out);
    buf_free(&err);
    return result;
}

static Result run_test(const char *path, const Options *o, const char *tmp) {
    Buf src = {0};
    Test t = {0};
    Buf diag = {0};
    char *exe = xformat("%s/prog", tmp);
    char *diag_path = xformat("%s/compile", tmp);
    char err[256];
    Result result = FAIL;

    if (!read_file(path, &src)) {
        verdict("FAIL", path, "cannot read: %s", strerror(errno));
        goto done;
    }
    if (!parse_header(&src, &t, err, sizeof err)) {
        verdict("FAIL", path, "bad header: %s", err);
        goto done;
    }
    if ((o->stage0 && t.stage0_skip) || !code_selected(o, &t)) {
        verdict("SKIP", path, NULL);
        result = SKIP;
        goto done;
    }

    char *argv[] = {o->compiler, (char *)path, exe, NULL};
    Outcome c = run_process(argv, NULL, diag_path, diag_path, o->timeout);
    read_file(diag_path, &diag);
    if (c.timed_out) {
        verdict("FAIL", path, "compiler timed out after %d s", o->timeout);
    } else if (c.signaled) {
        verdict("FAIL", path, "compiler killed by signal %d", c.status);
        show_lines(&diag);
    } else if (t.kind == KIND_COMPILE_ERROR) {
        if (c.status == 0) {
            verdict("FAIL", path, "compiled, but a compile error was expected");
        } else {
            result = check_compile_error(path, &t, o, &diag);
        }
    } else if (c.status != 0) {
        verdict("FAIL", path, "compile failed with status %d", c.status);
        show_lines(&diag);
    } else {
        result = check_run(path, &t, o, tmp, exe);
    }

done:
    clear_dir(tmp);
    buf_free(&src);
    buf_free(&diag);
    test_free(&t);
    free(exe);
    free(diag_path);
    return result;
}

// ---- main ---------------------------------------------------------------

static bool ends_with(const char *s, const char *suffix) {
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

// Adds `path` if it is a file, or every *.arche file below it if it is a
// directory, leaving out names that start with '_' or '.'.
static void collect(const char *path, List *files) {
    struct stat st;
    if (stat(path, &st) != 0) {
        die("cannot open %s: %s", path, strerror(errno));
    }
    if (!S_ISDIR(st.st_mode)) {
        list_push(files, xstrndup(path, strlen(path)));
        return;
    }
    DIR *d = opendir(path);
    if (!d) {
        die("cannot open %s: %s", path, strerror(errno));
    }
    bool slash = ends_with(path, "/");
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        // "." and "..", and support files: modules that tests import.
        if (e->d_name[0] == '.' || e->d_name[0] == '_') {
            continue;
        }
        char *child = slash ? xformat("%s%s", path, e->d_name) : xformat("%s/%s", path, e->d_name);
        struct stat cs;
        if (stat(child, &cs) != 0) {
            die("cannot open %s: %s", child, strerror(errno));
        }
        if (S_ISDIR(cs.st_mode)) {
            collect(child, files);
            free(child);
        } else if (S_ISREG(cs.st_mode) && ends_with(e->d_name, ".arche")) {
            list_push(files, child);
        } else {
            free(child);
        }
    }
    closedir(d);
}

static int compare_paths(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int usage(void) {
    fputs("usage: runner --compiler <command> [--stage0] [--codes <prefixes>] [--timeout <seconds>] [--filter <text>]"
          " <path>...\n",
          stderr);
    return 2;
}

int main(int argc, char **argv) {
    Options o = {.timeout = 10};
    List files = {0};
    int path_args = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool has_value = i + 1 < argc;
        if (strcmp(a, "--compiler") == 0 && has_value) {
            o.compiler = argv[++i];
        } else if (strcmp(a, "--timeout") == 0 && has_value) {
            long v;
            if (!parse_uint(argv[++i], 3600, &v) || v == 0) {
                die("invalid --timeout: %s", argv[i]);
            }
            o.timeout = (int)v;
        } else if (strcmp(a, "--filter") == 0 && has_value) {
            o.filter = argv[++i];
        } else if (strcmp(a, "--codes") == 0 && has_value) {
            o.codes = argv[++i];
        } else if (strcmp(a, "--stage0") == 0) {
            o.stage0 = true;
        } else if (a[0] == '-') {
            return usage();
        } else {
            collect(a, &files);
            path_args++;
        }
    }
    if (!o.compiler || path_args == 0) {
        return usage();
    }
    qsort(files.items, files.len, sizeof *files.items, compare_paths);

    const char *base = getenv("TMPDIR");
    char *tmpl = xformat("%s/arche-runner-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(tmpl)) {
        die("cannot create a temporary directory: %s", strerror(errno));
    }
    char *tmp = realpath(tmpl, NULL);
    if (!tmp) {
        die("realpath %s: %s", tmpl, strerror(errno));
    }

    size_t passed = 0;
    size_t failed = 0;
    size_t skipped = 0;
    for (size_t i = 0; i < files.len; i++) {
        if (i > 0 && strcmp(files.items[i], files.items[i - 1]) == 0) {
            continue;
        }
        if (o.filter && !strstr(files.items[i], o.filter)) {
            continue;
        }
        switch (run_test(files.items[i], &o, tmp)) {
        case PASS:
            passed++;
            break;
        case FAIL:
            failed++;
            break;
        case SKIP:
            skipped++;
            break;
        }
    }
    printf("%zu passed, %zu failed, %zu skipped\n", passed, failed, skipped);

    if (rmdir(tmp) != 0) {
        die("cannot remove %s: %s", tmp, strerror(errno));
    }
    free(tmp);
    free(tmpl);
    list_free(&files);
    return failed > 0 ? 1 : 0;
}
