// The Linux half of the runtime (rt.c): x86-64, no C library, system calls
// by number through the syscall instruction. A call that fails returns the
// negated errno in -4095..-1.
//
// rt.c defines what every system must give it; this file is included at its
// end, so the types and the helpers above are in scope.

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

enum { SYS_READ = 0, SYS_WRITE = 1, SYS_CLOSE = 3, SYS_FSTAT = 5, SYS_MMAP = 9, SYS_MPROTECT = 10, SYS_MUNMAP = 11, SYS_GETPID = 39, SYS_FORK = 57, SYS_POLL = 7, SYS_DUP2 = 33, SYS_PIPE2 = 293, SYS_SCHED_GETAFFINITY = 204,
       SYS_EXIT = 60, SYS_WAIT4 = 61, SYS_CLOCK_GETTIME = 228, SYS_EXIT_GROUP = 231, SYS_OPENAT = 257 };
enum { AT_FDCWD = -100, O_RDONLY = 0, O_WRONLY = 1, O_CREAT = 0100, O_TRUNC = 01000, O_CLOEXEC = 02000000 };
enum { ENOENT = 2, EACCES = 13, ENOTDIR = 20, EISDIR = 21, EPERM = 1, EINTR = 4 };

// ---- memory ---------------------------------------------------------------------

static void *os_map(u64 n) {
    i64 p = sys6(SYS_MMAP, 0, (i64)n, 3 /* read, write */, 0x22 /* private, anonymous */, -1, 0);
    if (p < 0 && p > -4096) {
        return 0;
    }
    return (void *)p;
}

static void os_unmap(void *p, u64 n) {
    sys3(SYS_MUNMAP, (i64)p, (i64)n, 0);
}

static u8 *os_stack_map(u64 n) {
    i64 p = sys6(SYS_MMAP, 0, (i64)n, 3, 0x22, -1, 0);
    if (p < 0 && p > -4096) {
        return 0;
    }
    sys3(SYS_MPROTECT, p, GUARD, 0); // the stack's end faults, not overflows
    return (u8 *)p;
}

static void os_stack_unmap(u8 *p, u64 n) {
    os_unmap(p, n);
}

// ---- files and output -------------------------------------------------------------

// An errno as one of rt.c's IO_ codes.
static u16 io_code(i64 e) {
    switch (-e) {
    case ENOENT:
    case ENOTDIR:
        return IO_NOT_FOUND;
    case EACCES:
    case EPERM:
        return IO_ACCESS;
    case EISDIR:
        return IO_IS_DIR;
    default:
        return IO_FAILED;
    }
}

// A path as the kernel wants it: NUL-terminated. 0 when it holds a NUL.
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

static i64 os_open_read(const u8 *path, u64 path_len) {
    char *p = c_path(path, path_len);
    if (!p) {
        return -IO_NOT_FOUND;
    }
    i64 fd = sys4(SYS_OPENAT, AT_FDCWD, (i64)p, O_RDONLY | O_CLOEXEC, 0);
    return fd < 0 ? -(i64)io_code(fd) : fd;
}

static i64 os_open_write(const u8 *path, u64 path_len) {
    char *p = c_path(path, path_len);
    if (!p) {
        return -IO_NOT_FOUND;
    }
    i64 fd = sys4(SYS_OPENAT, AT_FDCWD, (i64)p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    return fd < 0 ? -(i64)io_code(fd) : fd;
}

static i64 os_read(i64 handle, u8 *p, u64 n) {
    i64 got = sys3(SYS_READ, handle, (i64)p, (i64)n);
    return got < 0 ? -(i64)io_code(got) : got;
}

static i64 os_write(i64 handle, const u8 *p, u64 n) {
    i64 done = sys3(SYS_WRITE, handle, (i64)p, (i64)n);
    return done < 0 ? -(i64)io_code(done) : done;
}

static void os_close(i64 handle) {
    sys1(SYS_CLOSE, handle);
}

// std.Fs works on paths from the current directory (std.Fs.dir).
static i64 os_fs_dir(void) {
    return AT_FDCWD;
}

// ---- the process ------------------------------------------------------------------

_Noreturn static void os_exit(int status) {
    sys1(SYS_EXIT_GROUP, status);
    __builtin_unreachable();
}

static u64 os_seed(void) {
    i64 ts[2] = {0, 0};
    sys3(SYS_CLOCK_GETTIME, 1, (i64)ts, 0);
    return (u64)ts[0] * 1000000000UL + (u64)ts[1] + (u64)sys1(SYS_GETPID, 0);
}

static u64 os_processors(void) {
    u64 mask[16];
    for (int i = 0; i < 16; i++) {
        mask[i] = 0;
    }
    i64 n = sys3(SYS_SCHED_GETAFFINITY, 0, sizeof(mask), (i64)mask);
    u64 count = 0;
    for (i64 i = 0; i < n / 8 && i < 16; i++) {
        count += (u64)__builtin_popcountll(mask[i]);
    }
    return count ? count : 1;
}

// ---- tasks ------------------------------------------------------------------------

void a0rt_ctx_switch(u64 *save_rsp, u64 load_rsp);
__asm__(".text\n"
        ".global a0rt_ctx_switch\n"
        "a0rt_ctx_switch:\n"
        "    push %rbp\n"
        "    push %rbx\n"
        "    push %r12\n"
        "    push %r13\n"
        "    push %r14\n"
        "    push %r15\n"
        "    mov %rsp, (%rdi)\n"
        "    mov %rsi, %rsp\n"
        "    pop %r15\n"
        "    pop %r14\n"
        "    pop %r13\n"
        "    pop %r12\n"
        "    pop %rbx\n"
        "    pop %rbp\n"
        "    ret\n");

// A fiber's stack needs no telling: the kernel keeps no bounds of its own.
static void os_stack_switch(const Fiber *next) {
    (void)next;
}

// How a0rt_ctx_switch leaves a new fiber's stack: the six registers it
// pops, then where it returns to (rt.c's fiber_entry) and that frame's
// return address, which is never used. The frame keeps the stack pointer a
// multiple of sixteen at a call, as the ABI wants.
static u64 os_fiber_frame(void) {
    return 8;
}

static u64 os_fiber_saved(void) {
    return 6;
}

typedef struct PollFd {
    int fd;
    short events;
    short revents;
} PollFd;

static PollFd *poll_entries;
static u64 poll_entries_cap;

static void os_poll(const i64 *fds, const short *events, u8 *ready, u64 n) {
    if (n > poll_entries_cap) {
        u64 cap = poll_entries_cap ? poll_entries_cap : 16;
        while (cap < n) {
            cap *= 2;
        }
        poll_entries = a0rt_alloc(cap * sizeof(PollFd));
        poll_entries_cap = cap;
    }
    for (u64 i = 0; i < n; i++) {
        poll_entries[i].fd = (int)fds[i];
        poll_entries[i].events = events[i];
        poll_entries[i].revents = 0;
    }
    i64 r;
    do {
        r = sys3(SYS_POLL, (i64)poll_entries, (i64)n, -1);
    } while (r == -EINTR);
    for (u64 i = 0; i < n; i++) {
        // An error or a hang-up wakes the fiber too: its next call says so.
        ready[i] = poll_entries[i].revents != 0 || r < 0;
    }
}

// ---- the test runner --------------------------------------------------------------
//
// Each test runs in a child process, which fork gives for nothing: the
// child is this program, with the tests' table, and runs one of them.

// Nothing to do: a child here is forked, not started afresh.
static void os_test_child(System *sys) {
    (void)sys;
}

static void os_start_test(System *sys, u64 i, u64 seed, Run *r) {
    int out[2], err[2];
    if (sys3(SYS_PIPE2, (i64)out, 0x80000, 0) < 0 || sys3(SYS_PIPE2, (i64)err, 0x80000, 0) < 0) {
        a0rt_die("cannot make a pipe for a test", 29);
    }
    flush_all();
    i64 pid = sys1(SYS_FORK, 0);
    if (pid == 0) {
        sys3(SYS_DUP2, out[1], 1, 0);
        sys3(SYS_DUP2, err[1], 2, 0);
        run_test(sys, i, seed);
        os_exit(0);
    }
    sys1(SYS_CLOSE, out[1]);
    sys1(SYS_CLOSE, err[1]);
    r->child = pid;
    r->out = out[0];
    r->err = err[0];
    r->state = 1;
}

// Reads what the running children wrote; a child whose pipes are both
// closed has ended, and is waited for.
static void os_collect(Run *runs, u64 count) {
    PollFd fds[128];
    Run *who[128];
    i64 *which[128];
    u64 n = 0;
    for (u64 i = 0; i < count && n + 2 <= 128; i++) {
        Run *r = &runs[i];
        if (r->state != 1) {
            continue;
        }
        i64 *ends[2] = {&r->out, &r->err};
        for (int k = 0; k < 2; k++) {
            if (*ends[k] >= 0) {
                fds[n].fd = (int)*ends[k];
                fds[n].events = 1;
                fds[n].revents = 0;
                who[n] = r;
                which[n] = ends[k];
                n++;
            }
        }
    }
    if (n > 0) {
        i64 p;
        do {
            p = sys3(SYS_POLL, (i64)fds, (i64)n, -1);
        } while (p == -EINTR);
        u8 buf[4096];
        for (u64 j = 0; j < n; j++) {
            if (fds[j].revents == 0) {
                continue;
            }
            i64 got = sys3(SYS_READ, *which[j], (i64)buf, sizeof(buf));
            if (got > 0) {
                keep(which[j] == &who[j]->out ? &who[j]->stdout_kept : &who[j]->stderr_kept, buf, (u64)got);
            } else if (got != -EINTR) {
                sys1(SYS_CLOSE, *which[j]);
                *which[j] = -1;
            }
        }
    }
    for (u64 i = 0; i < count; i++) {
        Run *r = &runs[i];
        if (r->state == 1 && r->out < 0 && r->err < 0) {
            int status = 0;
            sys4(SYS_WAIT4, r->child, (i64)&status, 0, 0);
            r->status = status;
            r->state = 2;
        }
    }
}

// ---- entry ------------------------------------------------------------------------

// The kernel starts the program here, with argc at the top of the stack.
// ---- a panic's stack trace -----------------------------------------------------------
//
// A program from the compiler's own backend has a table of its calls, by the
// address each returns to, and keeps frame pointers. Others have no table, and
// these names are then 0.
typedef struct {
    u32 pc;
    u32 line;
    u32 function;
} SourceCall;

typedef struct {
    const u8 *name;
    u64 name_len;
    const u8 *file;
    u64 file_len;
} SourceFunction;

extern const SourceCall a0_source_calls[] __attribute__((weak));
extern const u64 a0_source_call_count __attribute__((weak));
extern const SourceFunction a0_source_functions[] __attribute__((weak));

static const SourceCall *source_call_at(u64 pc) {
    u64 lo = 0;
    u64 hi = a0_source_call_count;
    while (lo < hi) {
        u64 mid = (lo + hi) / 2;
        if (a0_source_calls[mid].pc == pc) {
            return &a0_source_calls[mid];
        }
        if (a0_source_calls[mid].pc < pc) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return 0;
}

static void os_stack_trace(void) {
    if (!a0_source_calls || stack_trace_off) {
        return;
    }
    const u64 *frame = __builtin_frame_address(0);
    for (int depth = 0; frame && depth < 64; depth++) {
        const SourceCall *call = source_call_at(frame[1]);
        const SourceFunction *f = call ? &a0_source_functions[call->function] : 0;
        // The runtime's own frames are not in the table, nor is a0_entry in a file.
        if (f && f->file_len > 0) {
            a0rt_write(&err_fd, (const u8 *)"  in ", 5);
            a0rt_write(&err_fd, f->name, f->name_len);
            a0rt_write(&err_fd, (const u8 *)" (", 2);
            a0rt_write(&err_fd, f->file, f->file_len);
            a0rt_write_byte(&err_fd, ':');
            a0rt_write_u64(&err_fd, call->line);
            a0rt_write(&err_fd, (const u8 *)")\n", 2);
        }
        // A frame's parent is above it; 0 at the bottom of a stack.
        const u64 *parent = (const u64 *)frame[0];
        if (parent <= frame || ((u64)parent & 7) != 0) {
            break;
        }
        frame = parent;
    }
}

_Noreturn void a0rt_start(i64 *sp) {
    i64 argc = sp[0];
    char **argv = (char **)(sp + 1);
    Str *args = a0rt_alloc((u64)(argc > 0 ? argc : 1) * sizeof(Str));
    for (i64 i = 0; i < argc; i++) {
        args[i].ptr = (const u8 *)argv[i];
        args[i].len = c_strlen(argv[i]);
    }
    run_program(args, (u64)argc, argv + argc + 1);
}

__asm__(".text\n"
        ".global _start\n"
        "_start:\n"
        "    xor %rbp, %rbp\n"
        "    mov %rsp, %rdi\n"
        "    and $-16, %rsp\n"
        "    call a0rt_start\n"
        "    hlt\n");
