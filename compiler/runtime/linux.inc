// The Linux half of the runtime (rt.c): no C library, system calls by number.
// A call that fails returns the negated errno in -4095..-1. What differs by
// processor is in linux-<processor>.inc: the numbers, how a call is made, the
// switch between fibers and the entry point.
//
// rt.c defines what every system must give it; this file is included at its
// end, so the types and the helpers above are in scope.

typedef struct PollFd {
    int fd;
    short events;
    short revents;
} PollFd;

#if defined(__x86_64__)
#include "linux-x86_64.inc"
#elif defined(__aarch64__)
#include "linux-aarch64.inc"
#elif defined(__riscv)
#include "linux-riscv64.inc"
#else
#error "no system calls for this processor"
#endif

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

// ---- files off the scheduler's thread ----------------------------------------------
// A file's system calls wait for the disk, and a regular file is always
// "ready" to poll, so a task that reads one would stop every other task.
// While tasks run, the runtime gives such a call to a thread of its own,
// which makes the call and nothing else (it touches no runtime state and
// allocates nothing), and the task waits for an eventfd in the poller as it
// waits for a socket. With no task running, a call is made where it is.
// One call is out at a time; a task that finds the thread busy waits its
// turn. A file in the page cache answers in microseconds, so each side spins
// a little before it sleeps: the thread for the next call, and the task for
// the answer when no other task is ready to run; only a call that takes
// longer costs a futex and the poller.

enum { FILE_IDLE = 0, FILE_SUBMITTED = 1, FILE_DONE = 2 };
enum { THREAD_SPINS = 20000, TASK_SPINS = 4000 };
enum { CLONE_THREAD_FLAGS = 0x100 | 0x200 | 0x400 | 0x800 | 0x10000 | 0x40000 };
enum { FUTEX_WAIT_PRIVATE = 128, FUTEX_WAKE_PRIVATE = 129, IO_STACK = 65536 };

static struct {
    u32 state;
    i64 n, a, b, c, d, e, f;
    i64 result;
} file_job;
static u32 file_sleeping;   // the thread waits on the futex
static u32 file_waiting;    // the task waits in the poller
static i64 file_event = -1;
static int file_claimed;
static int file_thread; // 0 not tried, 1 running, -1 could not start

static void file_thread_main(void) {
    for (;;) {
        u32 s = __atomic_load_n(&file_job.state, __ATOMIC_ACQUIRE);
        for (u32 i = 0; s != FILE_SUBMITTED && i < THREAD_SPINS; i++) {
            cpu_relax();
            s = __atomic_load_n(&file_job.state, __ATOMIC_ACQUIRE);
        }
        if (s != FILE_SUBMITTED) {
            // Asleep, said first, then the state seen again: a task that
            // submits after this wakes the futex.
            __atomic_store_n(&file_sleeping, 1, __ATOMIC_SEQ_CST);
            s = __atomic_load_n(&file_job.state, __ATOMIC_SEQ_CST);
            if (s != FILE_SUBMITTED) {
                sys6(SYS_FUTEX, (i64)&file_job.state, FUTEX_WAIT_PRIVATE, s, 0, 0, 0);
            }
            __atomic_store_n(&file_sleeping, 0, __ATOMIC_SEQ_CST);
            continue;
        }
        file_job.result = sys6(file_job.n, file_job.a, file_job.b, file_job.c, file_job.d, file_job.e, file_job.f);
        __atomic_store_n(&file_job.state, FILE_DONE, __ATOMIC_SEQ_CST);
        if (__atomic_load_n(&file_waiting, __ATOMIC_SEQ_CST)) {
            u64 one = 1;
            sys3(SYS_WRITE, file_event, (i64)&one, 8);
        }
    }
}

static int file_thread_ready(void) {
    if (file_thread == 0) {
        file_thread = -1;
        // EFD_NONBLOCK | EFD_CLOEXEC
        i64 fd = sys3(SYS_EVENTFD2, 0, 0x800 | 0x80000, 0);
        u8 *stack = os_map(IO_STACK);
        if (fd >= 0 && stack) {
            file_event = fd;
            if (sys_thread(CLONE_THREAD_FLAGS, stack + IO_STACK, file_thread_main) > 0) {
                file_thread = 1;
            }
        }
    }
    return file_thread == 1;
}

// lib/std/linux.arche's file_syscall: system call n, made on the file
// thread while tasks run, and here otherwise. A read first asks for what
// the page cache has without waiting (preadv2 with RWF_NOWAIT), and an open
// for a path the kernel has in its cache (openat2 with RESOLVE_CACHED): only
// a call that would wait for the disk goes to the thread.
enum { RWF_NOWAIT = 8, RESOLVE_CACHED = 0x20, SYS_OPENAT2 = 437, EAGAIN = 11, EOPNOTSUPP = 95, ENOSYS = 38, EINVAL = 22 };

i64 a0rt_file_syscall(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    if (fiber_count == 0 || !file_thread_ready()) {
        return sys6(n, a, b, c, d, e, f);
    }
    if (n == SYS_OPENAT) {
        // struct open_how: flags, mode, resolve.
        u64 how[3] = {(u64)c, (u64)d, RESOLVE_CACHED};
        i64 r = sys4(SYS_OPENAT2, a, b, (i64)how, sizeof(how));
        if (r != -EAGAIN && r != -EOPNOTSUPP && r != -ENOSYS && r != -EINVAL) {
            return r;
        }
    }
    if (n == SYS_READ || n == SYS_PREAD64) {
        i64 iov[2] = {b, c};
        // An offset of -1 is the file's own, as read uses.
        i64 r = sys6(SYS_PREADV2, a, (i64)iov, 1, n == SYS_READ ? -1 : d, 0, RWF_NOWAIT);
        if (r != -EAGAIN && r != -EOPNOTSUPP && r != -ENOSYS && r != -EINVAL) {
            return r;
        }
    }
    while (file_claimed) {
        a0rt_task_wait(&file_job);
    }
    file_claimed = 1;
    file_job.n = n;
    file_job.a = a;
    file_job.b = b;
    file_job.c = c;
    file_job.d = d;
    file_job.e = e;
    file_job.f = f;
    __atomic_store_n(&file_job.state, FILE_SUBMITTED, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&file_sleeping, __ATOMIC_SEQ_CST)) {
        sys6(SYS_FUTEX, (i64)&file_job.state, FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
    }
    int others = main_fiber.state == READY;
    for (u64 i = 0; i < fiber_count; i++) {
        others |= fibers[i]->state == READY;
    }
    for (u32 i = 0; !others && i < TASK_SPINS && __atomic_load_n(&file_job.state, __ATOMIC_ACQUIRE) != FILE_DONE; i++) {
        cpu_relax();
    }
    while (__atomic_load_n(&file_job.state, __ATOMIC_SEQ_CST) != FILE_DONE) {
        // Waiting, said first, then the state seen again: a thread that
        // finishes after this writes the eventfd.
        __atomic_store_n(&file_waiting, 1, __ATOMIC_SEQ_CST);
        if (__atomic_load_n(&file_job.state, __ATOMIC_SEQ_CST) == FILE_DONE) {
            break;
        }
        a0rt_io_wait(file_event, 1 /* POLLIN */);
        u64 count;
        sys3(SYS_READ, file_event, (i64)&count, 8);
    }
    __atomic_store_n(&file_waiting, 0, __ATOMIC_SEQ_CST);
    i64 r = file_job.result;
    __atomic_store_n(&file_job.state, FILE_IDLE, __ATOMIC_RELEASE);
    file_claimed = 0;
    a0rt_task_wake(&file_job);
    return r;
}

// The runtime's own file calls go the same way.
static i64 file_sys4(i64 n, i64 a, i64 b, i64 c, i64 d) {
    return a0rt_file_syscall(n, a, b, c, d, 0, 0);
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
// A path with a zero after it, in buf, which holds PATH_MAX bytes: one with
// a zero inside, or one too long for the kernel, is none. On the stack, not
// the heap: the program's allocator frees nothing, and a server opens files
// for as long as it runs.
enum { PATH_MAX = 4096 };

static char *c_path(const u8 *p, u64 n, char *buf) {
    if (n >= PATH_MAX) {
        return 0;
    }
    for (u64 i = 0; i < n; i++) {
        if (p[i] == 0) {
            return 0;
        }
    }
    memcpy(buf, p, n);
    buf[n] = 0;
    return buf;
}

static i64 os_open_read(const u8 *path, u64 path_len) {
    char buf[PATH_MAX];
    char *p = c_path(path, path_len, buf);
    if (!p) {
        return -IO_NOT_FOUND;
    }
    i64 fd = file_sys4(SYS_OPENAT, AT_FDCWD, (i64)p, O_RDONLY | O_CLOEXEC, 0);
    return fd < 0 ? -(i64)io_code(fd) : fd;
}

static i64 os_open_write(const u8 *path, u64 path_len) {
    char buf[PATH_MAX];
    char *p = c_path(path, path_len, buf);
    if (!p) {
        return -IO_NOT_FOUND;
    }
    i64 fd = file_sys4(SYS_OPENAT, AT_FDCWD, (i64)p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    return fd < 0 ? -(i64)io_code(fd) : fd;
}

static i64 os_open_program(const u8 *path, u64 path_len) {
    char buf[PATH_MAX];
    char *p = c_path(path, path_len, buf);
    if (!p) {
        return -IO_NOT_FOUND;
    }
    // A new file: what was there kept its own mode, and may be running.
    sys3(SYS_UNLINKAT, AT_FDCWD, (i64)p, 0);
    i64 fd = sys4(SYS_OPENAT, AT_FDCWD, (i64)p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0755);
    return fd < 0 ? -(i64)io_code(fd) : fd;
}

static i64 os_write(i64 handle, const u8 *p, u64 n) {
    i64 done = sys3(SYS_WRITE, handle, (i64)p, (i64)n);
    return done < 0 ? -(i64)io_code(done) : done;
}

static void os_close(i64 handle) {
    sys1(SYS_CLOSE, handle);
}

static i64 os_file_read(i64 handle, u8 *p, u64 n) {
    i64 got = a0rt_file_syscall(SYS_READ, handle, (i64)p, (i64)n, 0, 0, 0);
    return got < 0 ? -(i64)io_code(got) : got;
}

static i64 os_file_write(i64 handle, const u8 *p, u64 n) {
    i64 done = a0rt_file_syscall(SYS_WRITE, handle, (i64)p, (i64)n, 0, 0, 0);
    return done < 0 ? -(i64)io_code(done) : done;
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
        // No popcount: not every processor has one, and no library is here.
        for (u64 bits = mask[i]; bits; bits &= bits - 1) {
            count++;
        }
    }
    return count ? count : 1;
}

// ---- tasks ------------------------------------------------------------------------

// A fiber's stack needs no telling: the kernel keeps no bounds of its own.
static void os_stack_switch(const Fiber *next) {
    (void)next;
}

static PollFd *poll_entries;
static u64 poll_entries_cap;

static void os_poll(const i64 *fds, const short *events, u8 *ready, u64 n, int wait) {
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
        r = sys_poll(poll_entries, n, wait);
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
    i64 pid = sys_fork();
    if (pid == 0) {
        sys_dup2(out[1], 1);
        sys_dup2(err[1], 2);
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
            p = sys_poll(fds, n, 1);
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
        const SourceCall *call = source_call_at(frame_return(frame));
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
        const u64 *parent = frame_parent(frame);
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
