// The Windows half of the runtime (rt.c): x86-64, Win32 only (kernel32 and
// ws2_32), no C library and no C runtime of Microsoft's either. The program
// owns its entry point, memory comes from VirtualAlloc, paths are UTF-16,
// and a test's child process is this program started again, since Windows
// has no fork (docs/sprints.md S19).
//
// rt.c defines what every system must give it; this file is included at its
// end, so the types and the helpers above are in scope.

typedef void *Handle;

// An object file that uses floating point refers to this, so that a C
// runtime would link its float support; here the runtime is its own.
int _fltused = 1;

// kernel32, by name: the import library gives each a thunk, so no header
// and no C library are needed. Every Win32 call returns 0 or -1 on failure
// and leaves the reason in GetLastError.
extern void *VirtualAlloc(void *address, u64 size, u32 type, u32 protect);
extern int VirtualFree(void *address, u64 size, u32 type);
extern int VirtualProtect(void *address, u64 size, u32 protect, u32 *old);
extern Handle GetStdHandle(u32 which);
extern int WriteFile(Handle file, const void *data, u32 n, u32 *written, void *overlapped);
extern int ReadFile(Handle file, void *into, u32 n, u32 *got, void *overlapped);
extern Handle CreateFileW(const u16 *path, u32 access, u32 share, void *security, u32 disposition, u32 flags, Handle from);
extern int CloseHandle(Handle it);
extern u32 GetFileAttributesW(const u16 *path);
extern int DeleteFileW(const u16 *path);
extern u32 GetLastError(void);
extern _Noreturn void ExitProcess(u32 status);
extern u16 *GetCommandLineW(void);
extern u32 GetCurrentProcessId(void);
extern int QueryPerformanceCounter(i64 *count);
extern void GetSystemInfo(void *info);
extern u32 GetTempPathW(u32 n, u16 *into);
extern u32 GetModuleFileNameW(Handle module, u16 *into, u32 n);
extern int CreateProcessW(const u16 *program, u16 *command, void *process_security, void *thread_security, int inherit,
                         u32 flags, void *environment, const u16 *directory, void *startup, void *made);
extern u32 WaitForMultipleObjects(u32 count, const Handle *handles, int all, u32 milliseconds);
extern int GetExitCodeProcess(Handle process, u32 *status);
extern int GetConsoleMode(Handle it, u32 *mode);
extern int SetConsoleOutputCP(u32 page);
// ws2_32: the netpoller waits with the sockets' own poll, and the sockets
// have to be started before any of them is made (std's Net).
extern int WSAPoll(void *entries, u32 count, int milliseconds);
extern int WSAStartup(u16 version, void *data);

enum { MEM_COMMIT = 0x1000, MEM_RESERVE = 0x2000, MEM_RELEASE = 0x8000 };
enum { PAGE_NOACCESS = 0x01, PAGE_READWRITE = 0x04 };
enum { FILE_SHARE_READ = 1, FILE_SHARE_WRITE = 2, FILE_SHARE_DELETE = 4 };
enum { CREATE_ALWAYS = 2, OPEN_EXISTING = 3 };
enum { FILE_ATTRIBUTE_NORMAL = 0x80, FILE_ATTRIBUTE_DIRECTORY = 0x10 };
enum { ERROR_FILE_NOT_FOUND = 2, ERROR_PATH_NOT_FOUND = 3, ERROR_ACCESS_DENIED = 5, ERROR_NOT_ENOUGH_MEMORY = 8,
       ERROR_INVALID_NAME = 123, ERROR_DIRECTORY = 267, ERROR_BROKEN_PIPE = 109 };
enum { STARTF_USESTDHANDLES = 0x100 };
enum { CP_UTF8 = 65001 };
// The values an int cannot hold are written as they are used.
#define GENERIC_READ ((u32)0x80000000)
#define GENERIC_WRITE ((u32)0x40000000)
#define INVALID_FILE_ATTRIBUTES ((u32)0xFFFFFFFF)
#define INFINITE ((u32)0xFFFFFFFF)
// poll's events, as rt.c passes them, and what the sockets want for them.
enum { POLLRDNORM = 0x0100, POLLWRNORM = 0x0010 };

static Handle invalid_handle(void) {
    return (Handle)(i64)-1;
}

// Handles 0, 1 and 2 are this program's standard input, output and error;
// every other i64 in the runtime is a Win32 handle itself. Windows gives
// out no handle below 4, so the three numbers are free to mean this.
static Handle std_handles[3];

static Handle handle_of(i64 handle) {
    if (handle >= 0 && handle <= 2) {
        return std_handles[handle];
    }
    return (Handle)handle;
}

// A failure of Win32 as one of rt.c's IO_ codes; the path tells a directory
// from a file, which Windows refuses with the same error as a locked file.
static u16 win_code(const u16 *path) {
    u32 e = GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND || e == ERROR_INVALID_NAME) {
        return IO_NOT_FOUND;
    }
    if (e == ERROR_ACCESS_DENIED) {
        if (path) {
            u32 attributes = GetFileAttributesW(path);
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                return IO_IS_DIR;
            }
        }
        return IO_ACCESS;
    }
    if (e == ERROR_DIRECTORY) {
        return IO_IS_DIR;
    }
    return IO_FAILED;
}

// ---- UTF-16 ---------------------------------------------------------------------
//
// Win32 takes paths and gives the command line as UTF-16; Arche's text is
// UTF-8 (spec/arche.md §2.1), so the runtime translates at the edge.

// The text as UTF-16, NUL-terminated, in memory the runtime keeps for it:
// the next call writes over it, so a server that opens files for as long as
// it runs does not grow. 0 when the text holds a NUL or is not valid UTF-8,
// which no path can be.
static u16 *w_scratch;
static u64 w_scratch_units;

static u16 *w_text(const u8 *p, u64 n) {
    if (!valid_utf8(p, n)) {
        return 0;
    }
    for (u64 i = 0; i < n; i++) {
        if (p[i] == 0) {
            return 0;
        }
    }
    // Two units for every byte is always enough: a byte becomes at most one
    // unit, and a four-byte character two.
    if ((n + 1) * 2 > w_scratch_units) {
        w_scratch_units = (n + 1) * 2 + 256;
        w_scratch = a0rt_alloc(w_scratch_units * sizeof(u16));
    }
    u16 *w = w_scratch;
    u64 at = 0;
    u64 i = 0;
    while (i < n) {
        u8 c = p[i];
        u32 cp;
        u64 len;
        if (c < 0x80) {
            cp = c;
            len = 1;
        } else if (c < 0xE0) {
            cp = c & 0x1F;
            len = 2;
        } else if (c < 0xF0) {
            cp = c & 0x0F;
            len = 3;
        } else {
            cp = c & 0x07;
            len = 4;
        }
        for (u64 k = 1; k < len; k++) {
            cp = (cp << 6) | (p[i + k] & 0x3F);
        }
        i += len;
        if (cp < 0x10000) {
            w[at++] = (u16)cp;
        } else {
            cp -= 0x10000;
            w[at++] = (u16)(0xD800 + (cp >> 10));
            w[at++] = (u16)(0xDC00 + (cp & 0x3FF));
        }
    }
    w[at] = 0;
    return w;
}

// How many bytes the UTF-16 text takes as UTF-8, without its NUL.
static u64 utf8_len_of(const u16 *w, u64 n) {
    u64 bytes = 0;
    for (u64 i = 0; i < n; i++) {
        u32 cp = w[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < n && w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000) {
            bytes += 4;
            i++;
        } else if (cp < 0x80) {
            bytes += 1;
        } else if (cp < 0x800) {
            bytes += 2;
        } else {
            bytes += 3;
        }
    }
    return bytes;
}

// The UTF-16 text as UTF-8, into memory of the caller's; a lone surrogate,
// which is no character, goes as U+FFFD. Returns how many bytes it wrote.
static u64 to_utf8(const u16 *w, u64 n, u8 *into) {
    u64 at = 0;
    for (u64 i = 0; i < n; i++) {
        u32 cp = w[i];
        if (cp >= 0xD800 && cp < 0xDC00) {
            if (i + 1 < n && w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (w[i + 1] - 0xDC00);
                i++;
            } else {
                cp = 0xFFFD;
            }
        } else if (cp >= 0xDC00 && cp < 0xE000) {
            cp = 0xFFFD;
        }
        if (cp < 0x80) {
            into[at++] = (u8)cp;
        } else if (cp < 0x800) {
            into[at++] = (u8)(0xC0 | (cp >> 6));
            into[at++] = (u8)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            into[at++] = (u8)(0xE0 | (cp >> 12));
            into[at++] = (u8)(0x80 | ((cp >> 6) & 0x3F));
            into[at++] = (u8)(0x80 | (cp & 0x3F));
        } else {
            into[at++] = (u8)(0xF0 | (cp >> 18));
            into[at++] = (u8)(0x80 | ((cp >> 12) & 0x3F));
            into[at++] = (u8)(0x80 | ((cp >> 6) & 0x3F));
            into[at++] = (u8)(0x80 | (cp & 0x3F));
        }
    }
    return at;
}

static u64 w_len(const u16 *w) {
    u64 n = 0;
    while (w[n]) {
        n++;
    }
    return n;
}

// ---- memory ---------------------------------------------------------------------
//
// VirtualAlloc's pages are zero, as mmap's are.

static void *os_map(u64 n) {
    return VirtualAlloc(0, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

static void os_unmap(void *p, u64 n) {
    (void)n; // a whole reservation is freed at once, by its address alone
    VirtualFree(p, 0, MEM_RELEASE);
}

static u8 *os_stack_map(u64 n) {
    u8 *p = VirtualAlloc(0, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!p) {
        return 0;
    }
    u32 old = 0;
    VirtualProtect(p, GUARD, PAGE_NOACCESS, &old); // the stack's end faults
    return p;
}

static void os_stack_unmap(u8 *p, u64 n) {
    os_unmap(p, n);
}

// ---- files and output -------------------------------------------------------------

static i64 os_open_read(const u8 *path, u64 path_len) {
    u16 *w = w_text(path, path_len);
    if (!w) {
        return -IO_NOT_FOUND;
    }
    Handle h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, 0);
    if (h == invalid_handle()) {
        return -(i64)win_code(w);
    }
    return (i64)h;
}

static i64 os_open_write(const u8 *path, u64 path_len) {
    u16 *w = w_text(path, path_len);
    if (!w) {
        return -IO_NOT_FOUND;
    }
    Handle h = CreateFileW(w, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == invalid_handle()) {
        return -(i64)win_code(w);
    }
    return (i64)h;
}

// On Windows a program is known by its name, not its mode.
static i64 os_open_program(const u8 *path, u64 path_len) {
    return os_open_write(path, path_len);
}

static i64 os_read(i64 handle, u8 *p, u64 n) {
    u32 got = 0;
    u32 want = n > 0x40000000 ? 0x40000000 : (u32)n;
    if (!ReadFile(handle_of(handle), p, want, &got, 0)) {
        // A pipe whose other end is gone has simply ended.
        if (GetLastError() == ERROR_BROKEN_PIPE) {
            return 0;
        }
        return -(i64)win_code(0);
    }
    return got;
}

static i64 os_write(i64 handle, const u8 *p, u64 n) {
    u32 done = 0;
    u32 want = n > 0x40000000 ? 0x40000000 : (u32)n;
    if (!WriteFile(handle_of(handle), p, want, &done, 0)) {
        return -(i64)win_code(0);
    }
    return done;
}

static void os_close(i64 handle) {
    CloseHandle(handle_of(handle));
}

// std.Fs works on paths from the current directory, which Win32 keeps for
// the process: there is no handle to it (lib/std-windows.arche).
static i64 os_fs_dir(void) {
    return 0;
}

// ---- the process ------------------------------------------------------------------

_Noreturn static void os_exit(int status) {
    ExitProcess((u32)status);
}

static u64 os_seed(void) {
    i64 count = 0;
    QueryPerformanceCounter(&count);
    return (u64)count + GetCurrentProcessId();
}

static u64 os_processors(void) {
    u8 info[64];
    for (u64 i = 0; i < sizeof(info); i++) {
        info[i] = 0;
    }
    GetSystemInfo(info);
    u32 count = 0;
    memcpy(&count, info + 32, 4); // SYSTEM_INFO.dwNumberOfProcessors
    return count ? count : 1;
}

// ---- tasks ------------------------------------------------------------------------
//
// Windows keeps a thread's stack bounds in the thread's own block (the TEB),
// and the code the compiler makes probes a large frame against them
// (__chkstk below), so a switch to a fiber's stack tells the system about
// it. rdi and rsi are the callee's to keep here, unlike on Linux, and so
// are xmm6 to xmm15.

void a0rt_ctx_switch(u64 *save_rsp, u64 load_rsp);
__asm__(".text\n"
        ".global a0rt_ctx_switch\n"
        "a0rt_ctx_switch:\n"
        "    push %rbp\n"
        "    push %rbx\n"
        "    push %rdi\n"
        "    push %rsi\n"
        "    push %r12\n"
        "    push %r13\n"
        "    push %r14\n"
        "    push %r15\n"
        "    sub $160, %rsp\n"
        "    movups %xmm6, 0(%rsp)\n"
        "    movups %xmm7, 16(%rsp)\n"
        "    movups %xmm8, 32(%rsp)\n"
        "    movups %xmm9, 48(%rsp)\n"
        "    movups %xmm10, 64(%rsp)\n"
        "    movups %xmm11, 80(%rsp)\n"
        "    movups %xmm12, 96(%rsp)\n"
        "    movups %xmm13, 112(%rsp)\n"
        "    movups %xmm14, 128(%rsp)\n"
        "    movups %xmm15, 144(%rsp)\n"
        "    mov %rsp, (%rcx)\n"
        "    mov %rdx, %rsp\n"
        "    movups 0(%rsp), %xmm6\n"
        "    movups 16(%rsp), %xmm7\n"
        "    movups 32(%rsp), %xmm8\n"
        "    movups 48(%rsp), %xmm9\n"
        "    movups 64(%rsp), %xmm10\n"
        "    movups 80(%rsp), %xmm11\n"
        "    movups 96(%rsp), %xmm12\n"
        "    movups 112(%rsp), %xmm13\n"
        "    movups 128(%rsp), %xmm14\n"
        "    movups 144(%rsp), %xmm15\n"
        "    add $160, %rsp\n"
        "    pop %r15\n"
        "    pop %r14\n"
        "    pop %r13\n"
        "    pop %r12\n"
        "    pop %rsi\n"
        "    pop %rdi\n"
        "    pop %rbx\n"
        "    pop %rbp\n"
        "    ret\n");

// What the compiler calls before a frame larger than a page, to touch the
// pages it will use, lowest last, so that the main thread's stack grows in
// order. Microsoft's own reads the stack bounds from the TEB; this one
// walks from the stack pointer, so it is right on a fiber's stack too. It
// takes the frame's size in rax, which it keeps, and may use r10 and r11.
__asm__(".text\n"
        ".global __chkstk\n"
        "__chkstk:\n"
        "    lea 8(%rsp), %r11\n"
        "    sub %rax, %r11\n"        // the lowest address the frame reaches
        "    jb 2f\n"                 // a size that wraps: nothing to touch
        "    and $-4096, %r11\n"
        "    mov %rsp, %r10\n"
        "    and $-4096, %r10\n"
        "1:  cmp %r11, %r10\n"
        "    jbe 2f\n"
        "    sub $4096, %r10\n"
        "    orb $0, (%r10)\n"
        "    jmp 1b\n"
        "2:  ret\n");

// The thread block's own fields: where its stack ends and where it begins.
enum { TEB_STACK_BASE = 0x08, TEB_STACK_LIMIT = 0x10 };

static u64 teb_get(u64 at) {
    u64 v;
    __asm__ volatile("mov %%gs:(%1), %0" : "=r"(v) : "r"(at));
    return v;
}

static void teb_set(u64 at, u64 v) {
    __asm__ volatile("mov %0, %%gs:(%1)" : : "r"(v), "r"(at) : "memory");
}

static u64 main_stack_base, main_stack_limit;

static void os_stack_switch(const Fiber *next) {
    if (next->stack == 0) {
        // The thread's own stack, as Windows made it.
        teb_set(TEB_STACK_BASE, main_stack_base);
        teb_set(TEB_STACK_LIMIT, main_stack_limit);
    } else {
        teb_set(TEB_STACK_BASE, (u64)(next->stack + TASK_STACK + GUARD));
        teb_set(TEB_STACK_LIMIT, (u64)(next->stack + GUARD));
    }
}

// How a0rt_ctx_switch leaves a new fiber's stack: ten vector registers
// (two words each), eight general ones, then where it returns to (rt.c's
// fiber_entry) and that frame's return address, which is never used. The
// frame keeps the stack pointer a multiple of sixteen at a call, as the
// ABI wants.
static u64 os_fiber_frame(void) {
    return 30;
}

static u64 os_fiber_saved(void) {
    return 28;
}

// WSAPOLLFD: a socket, what to wait for, and what happened.
typedef struct {
    u64 socket;
    short events;
    short revents;
} PollEntry;

static PollEntry *poll_entries;
static u64 poll_entries_cap;

static void os_poll(const i64 *fds, const short *events, u8 *ready, u64 n) {
    if (n > poll_entries_cap) {
        u64 cap = poll_entries_cap ? poll_entries_cap : 16;
        while (cap < n) {
            cap *= 2;
        }
        poll_entries = a0rt_alloc(cap * sizeof(PollEntry));
        poll_entries_cap = cap;
    }
    for (u64 i = 0; i < n; i++) {
        poll_entries[i].socket = (u64)fds[i];
        // poll's POLLIN and POLLOUT, as the sockets want them.
        poll_entries[i].events = (short)(((events[i] & 1) ? POLLRDNORM : 0) | ((events[i] & 4) ? POLLWRNORM : 0));
        poll_entries[i].revents = 0;
    }
    int r = WSAPoll(poll_entries, (u32)n, -1);
    for (u64 i = 0; i < n; i++) {
        // An error or a hang-up wakes the fiber too: its next call says so.
        ready[i] = poll_entries[i].revents != 0 || r < 0;
    }
}

// ---- the test runner --------------------------------------------------------------
//
// Windows has no fork, so a test runs in this program started again, with
// the test's number and the run's seed as arguments. The child's output
// goes to two files, which the parent reads once the child has ended: a
// test's output is shown after its result line anyway (rt.c).

static const char TEST_ARG[] = "--a0-test";

// A build that is not a test build defines no table of tests. COFF has no
// name that may stay undefined, so the linker is told to read these two
// instead, and rt.c's test_count then finds no tests.
const u64 a0rt_no_test_count = 0;
const TestEntry a0rt_no_tests[1] = {{0, 0, 0, 0}};
#pragma comment(linker, "/alternatename:a0_test_count=a0rt_no_test_count")
#pragma comment(linker, "/alternatename:a0_tests=a0rt_no_tests")

// <temp>\arche-test-<pid>-<test>-<1 or 2>, where the child writes.
static u16 *test_file(u32 pid, u64 test, int which) {
    u16 *w = a0rt_alloc(1024 * sizeof(u16));
    u32 n = GetTempPathW(900, w);
    u64 at = n;
    const char *name = "arche-test-";
    for (u64 i = 0; name[i]; i++) {
        w[at++] = (u16)name[i];
    }
    u64 numbers[3] = {pid, test, (u64)which};
    for (int k = 0; k < 3; k++) {
        if (k > 0) {
            w[at++] = '-';
        }
        u64 v = numbers[k];
        u16 digits[20];
        int d = 0;
        do {
            digits[d++] = (u16)('0' + v % 10);
            v /= 10;
        } while (v);
        while (d > 0) {
            w[at++] = digits[--d];
        }
    }
    w[at] = 0;
    return w;
}

// Runs one test and ends, when this process was started for one.
static void os_test_child(System *sys) {
    for (u64 i = 1; i + 2 < sys->nargs; i++) {
        Str a = sys->args[i];
        if (a.len == sizeof(TEST_ARG) - 1 && memcmp(a.ptr, TEST_ARG, a.len) == 0) {
            u64 which = 0, seed = 0;
            if (!parse_u64(&sys->args[i + 1], &which) || !parse_u64(&sys->args[i + 2], &seed)) {
                a0rt_die("a test child was given no test to run", 37);
            }
            u64 count = test_count();
            if (which >= count) {
                a0rt_die("a test child was given a test that is not there", 47);
            }
            run_test(sys, which, seed);
            flush_all();
            os_exit(0);
        }
    }
}

static void os_start_test(System *sys, u64 i, u64 seed, Run *r) {
    u16 *exe = a0rt_alloc(1024 * sizeof(u16));
    u32 exe_len = GetModuleFileNameW(0, exe, 1000);
    if (exe_len == 0) {
        a0rt_die("cannot find this program's own file", 35);
    }
    // "<this program>" --a0-test <number> <seed>
    u16 *command = a0rt_alloc(1200 * sizeof(u16));
    u64 at = 0;
    command[at++] = '"';
    for (u32 k = 0; k < exe_len; k++) {
        command[at++] = exe[k];
    }
    command[at++] = '"';
    command[at++] = ' ';
    for (u64 k = 0; TEST_ARG[k]; k++) {
        command[at++] = (u16)TEST_ARG[k];
    }
    u64 numbers[2] = {i, seed};
    for (int k = 0; k < 2; k++) {
        command[at++] = ' ';
        u64 v = numbers[k];
        u16 digits[20];
        int d = 0;
        do {
            digits[d++] = (u16)('0' + v % 10);
            v /= 10;
        } while (v);
        while (d > 0) {
            command[at++] = digits[--d];
        }
    }
    command[at] = 0;

    // The files the child writes to are the child's to inherit.
    u8 security[24];
    for (u64 k = 0; k < sizeof(security); k++) {
        security[k] = 0;
    }
    u32 length = sizeof(security);
    int inherit = 1;
    memcpy(security, &length, 4);
    memcpy(security + 16, &inherit, 4);
    u32 pid = GetCurrentProcessId();
    Handle out = CreateFileW(test_file(pid, i, 1), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, security,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    Handle err = CreateFileW(test_file(pid, i, 2), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, security,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (out == invalid_handle() || err == invalid_handle()) {
        a0rt_die("cannot make a file for a test's output", 38);
    }

    u8 startup[104];
    for (u64 k = 0; k < sizeof(startup); k++) {
        startup[k] = 0;
    }
    u32 cb = sizeof(startup);
    u32 flags = STARTF_USESTDHANDLES;
    Handle in = std_handles[0];
    memcpy(startup, &cb, 4);
    memcpy(startup + 60, &flags, 4);
    memcpy(startup + 80, &in, 8);
    memcpy(startup + 88, &out, 8);
    memcpy(startup + 96, &err, 8);
    u8 made[24];
    for (u64 k = 0; k < sizeof(made); k++) {
        made[k] = 0;
    }
    flush_all();
    if (!CreateProcessW(exe, command, 0, 0, 1, 0, 0, 0, startup, made)) {
        a0rt_die("cannot start a child process for a test", 39);
    }
    Handle process;
    memcpy(&process, made, 8);
    Handle thread;
    memcpy(&thread, made + 8, 8);
    CloseHandle(thread);
    // The parent's own ends are of no use: the child has its copies.
    CloseHandle(out);
    CloseHandle(err);
    (void)sys;
    r->child = (i64)process;
    r->out = -1;
    r->err = -1;
    r->state = 1;
}

// Reads a file the child wrote, and removes it.
static void take_output(const u16 *path, Kept *k) {
    Handle h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, 0);
    if (h != invalid_handle()) {
        u8 buf[4096];
        for (;;) {
            u32 got = 0;
            if (!ReadFile(h, buf, sizeof(buf), &got, 0) || got == 0) {
                break;
            }
            keep(k, buf, got);
        }
        CloseHandle(h);
    }
    DeleteFileW(path);
}

// Waits until a child ends, and takes what it wrote.
static void os_collect(Run *runs, u64 count) {
    Handle handles[64];
    u64 which[64];
    u32 n = 0;
    for (u64 i = 0; i < count && n < 64; i++) {
        if (runs[i].state == 1) {
            handles[n] = (Handle)runs[i].child;
            which[n] = i;
            n++;
        }
    }
    if (n == 0) {
        return;
    }
    u32 w = WaitForMultipleObjects(n, handles, 0, INFINITE);
    if (w >= n) {
        a0rt_die("cannot wait for a test's child process", 38);
    }
    Run *r = &runs[which[w]];
    u32 status = 0;
    GetExitCodeProcess(handles[w], &status);
    CloseHandle(handles[w]);
    u32 pid = GetCurrentProcessId();
    take_output(test_file(pid, which[w], 1), &r->stdout_kept);
    take_output(test_file(pid, which[w], 2), &r->stderr_kept);
    r->status = (int)status;
    r->state = 2;
}

// Windows's builds go through LLVM, whose code keeps no frame pointers.
static void os_stack_trace(void) {
}

// ---- entry ------------------------------------------------------------------------
//
// Windows starts the program here, with no arguments: the command line is
// one text, which the program splits the way every other program on the
// system does.

// The arguments in the command line, counted with into 0 and written into
// into[k] when it is given. Outside quotes, spaces and tabs separate them;
// a quote begins and ends a quoted part; 2n backslashes before a quote are
// n backslashes and the quote counts, 2n+1 are n backslashes and a quote
// of its own. std's Proc writes a command line by these rules too
// (lib/std-windows.arche).
static u64 split_command(u16 *line, u16 **into) {
    u64 count = 0;
    u64 i = 0;
    while (line[i]) {
        while (line[i] == ' ' || line[i] == '\t') {
            i++;
        }
        if (!line[i]) {
            break;
        }
        u16 *arg = into ? into[count] : 0;
        u64 at = 0;
        int quoted = 0;
        while (line[i] && (quoted || (line[i] != ' ' && line[i] != '\t'))) {
            u64 slashes = 0;
            while (line[i] == '\\') {
                slashes++;
                i++;
            }
            if (line[i] == '"') {
                for (u64 k = 0; k < slashes / 2; k++) {
                    if (arg) {
                        arg[at] = '\\';
                    }
                    at++;
                }
                if (slashes % 2 == 1) {
                    if (arg) {
                        arg[at] = '"';
                    }
                    at++;
                } else {
                    quoted = !quoted;
                }
                i++;
            } else {
                for (u64 k = 0; k < slashes; k++) {
                    if (arg) {
                        arg[at] = '\\';
                    }
                    at++;
                }
                if (line[i]) {
                    if (arg) {
                        arg[at] = line[i];
                    }
                    at++;
                    i++;
                }
            }
        }
        if (arg) {
            arg[at] = 0;
        }
        count++;
    }
    return count;
}

_Noreturn void a0rt_entry(void) {
    std_handles[0] = GetStdHandle((u32)-10);
    std_handles[1] = GetStdHandle((u32)-11);
    std_handles[2] = GetStdHandle((u32)-12);
    // A console shows UTF-8 only when it is told to; a pipe takes the bytes
    // as they are.
    u32 mode = 0;
    if (GetConsoleMode(std_handles[1], &mode)) {
        SetConsoleOutputCP(CP_UTF8);
    }
    main_stack_base = teb_get(TEB_STACK_BASE);
    main_stack_limit = teb_get(TEB_STACK_LIMIT);
    // The sockets are of no use until they are started, and a program
    // cannot be asked to do it: std makes sockets, the runtime polls them.
    u8 sockets[512];
    for (u64 i = 0; i < sizeof(sockets); i++) {
        sockets[i] = 0;
    }
    WSAStartup(0x0202, sockets);

    u16 *line = GetCommandLineW();
    u64 line_len = w_len(line);
    u64 argc = split_command(line, 0);
    // Each argument fits in the command line, NUL included.
    u16 **wide = a0rt_alloc((argc ? argc : 1) * sizeof(u16 *));
    for (u64 i = 0; i < argc; i++) {
        wide[i] = a0rt_alloc((line_len + 1) * sizeof(u16));
    }
    split_command(line, wide);
    Str *args = a0rt_alloc((argc ? argc : 1) * sizeof(Str));
    for (u64 i = 0; i < argc; i++) {
        u64 n = w_len(wide[i]);
        u64 bytes = utf8_len_of(wide[i], n);
        u8 *text = a0rt_alloc(bytes + 1);
        to_utf8(wide[i], n, text);
        text[bytes] = 0;
        args[i].ptr = text;
        args[i].len = bytes;
    }
    run_program(args, argc, 0);
}
