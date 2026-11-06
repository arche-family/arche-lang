// Linux on ARM64, for linux.inc: system calls through svc, with the number in
// x8; the numbers of the generic table, which has no poll, fork or dup2.

static i64 sys6(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    register i64 x8 __asm__("x8") = n;
    register i64 x0 __asm__("x0") = a;
    register i64 x1 __asm__("x1") = b;
    register i64 x2 __asm__("x2") = c;
    register i64 x3 __asm__("x3") = d;
    register i64 x4 __asm__("x4") = e;
    register i64 x5 __asm__("x5") = f;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory", "cc");
    return x0;
}

static i64 sys1(i64 n, i64 a) {
    return sys6(n, a, 0, 0, 0, 0, 0);
}

static i64 sys3(i64 n, i64 a, i64 b, i64 c) {
    return sys6(n, a, b, c, 0, 0, 0);
}

static i64 sys4(i64 n, i64 a, i64 b, i64 c, i64 d) {
    return sys6(n, a, b, c, d, 0, 0);
}

enum { SYS_READ = 63, SYS_WRITE = 64, SYS_CLOSE = 57, SYS_FSTAT = 80, SYS_MMAP = 222, SYS_MPROTECT = 226, SYS_MUNMAP = 215, SYS_GETPID = 172, SYS_PIPE2 = 59, SYS_SCHED_GETAFFINITY = 123,
       SYS_EXIT = 93, SYS_WAIT4 = 260, SYS_CLOCK_GETTIME = 113, SYS_EXIT_GROUP = 94, SYS_OPENAT = 56 };

static i64 sys_poll(PollFd *fds, u64 n) {
    return sys6(73, (i64)fds, (i64)n, 0, 0, 0, 0);
}

static i64 sys_fork(void) {
    return sys6(220, 17, 0, 0, 0, 0, 0); // clone, SIGCHLD
}

static i64 sys_dup2(i64 from, i64 to) {
    return sys3(24, from, to, 0);
}

void a0rt_ctx_switch(u64 *save_sp, u64 load_sp);
__asm__(".text\n"
        ".global a0rt_ctx_switch\n"
        "a0rt_ctx_switch:\n"
        "    sub sp, sp, #160\n"
        "    stp x19, x20, [sp, #0]\n"
        "    stp x21, x22, [sp, #16]\n"
        "    stp x23, x24, [sp, #32]\n"
        "    stp x25, x26, [sp, #48]\n"
        "    stp x27, x28, [sp, #64]\n"
        "    stp x29, x30, [sp, #80]\n"
        "    stp d8, d9, [sp, #96]\n"
        "    stp d10, d11, [sp, #112]\n"
        "    stp d12, d13, [sp, #128]\n"
        "    stp d14, d15, [sp, #144]\n"
        "    mov x9, sp\n"
        "    str x9, [x0]\n"
        "    mov sp, x1\n"
        "    ldp x19, x20, [sp, #0]\n"
        "    ldp x21, x22, [sp, #16]\n"
        "    ldp x23, x24, [sp, #32]\n"
        "    ldp x25, x26, [sp, #48]\n"
        "    ldp x27, x28, [sp, #64]\n"
        "    ldp x29, x30, [sp, #80]\n"
        "    ldp d8, d9, [sp, #96]\n"
        "    ldp d10, d11, [sp, #112]\n"
        "    ldp d12, d13, [sp, #128]\n"
        "    ldp d14, d15, [sp, #144]\n"
        "    add sp, sp, #160\n"
        "    ret\n");

// A new fiber's stack, as a0rt_ctx_switch leaves it: all its registers, zero,
// but x30, where it returns to (rt.c's fiber_entry).
static u64 os_fiber_frame(void) {
    return 20;
}

static u64 os_fiber_saved(void) {
    return 11;
}

// A frame: its parent's frame pointer, then where it returns to.
static u64 frame_return(const u64 *frame) {
    return frame[1];
}

static const u64 *frame_parent(const u64 *frame) {
    return (const u64 *)frame[0];
}

// The kernel starts the program here, with argc at the top of the stack.
__asm__(".text\n"
        ".global _start\n"
        "_start:\n"
        "    mov x29, #0\n"
        "    mov x30, #0\n"
        "    mov x0, sp\n"
        "    and x9, x0, #-16\n"
        "    mov sp, x9\n"
        "    bl a0rt_start\n"
        "    brk #1\n");
