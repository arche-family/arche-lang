// Linux on x86-64, for linux.inc: system calls through the syscall
// instruction, the switch between fibers, and the entry point.

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

enum { SYS_READ = 0, SYS_WRITE = 1, SYS_CLOSE = 3, SYS_FSTAT = 5, SYS_MMAP = 9, SYS_MPROTECT = 10, SYS_MUNMAP = 11, SYS_GETPID = 39, SYS_PIPE2 = 293, SYS_SCHED_GETAFFINITY = 204,
       SYS_EXIT = 60, SYS_WAIT4 = 61, SYS_CLOCK_GETTIME = 228, SYS_EXIT_GROUP = 231, SYS_OPENAT = 257, SYS_UNLINKAT = 263 };

enum { SYS_CLONE = 56, SYS_FUTEX = 202, SYS_EVENTFD2 = 290, SYS_PREAD64 = 17, SYS_PREADV2 = 327 };

// A thread of this process running fn on stack, which must never return:
// clone with the new stack, and in the child a call of fn (kept in r12,
// which the child has as the parent left it). The parent gets the id.
static i64 sys_thread(i64 flags, u8 *stack, void (*fn)(void)) {
    i64 r;
    register i64 r10 __asm__("r10") = 0;
    register i64 r8 __asm__("r8") = 0;
    register void (*r12)(void) __asm__("r12") = fn;
    __asm__ volatile("syscall\n"
                     "test %%rax, %%rax\n"
                     "jnz 1f\n"
                     "call *%%r12\n"
                     "mov $60, %%eax\n"
                     "xor %%edi, %%edi\n"
                     "syscall\n"
                     "1:\n"
                     : "=a"(r)
                     : "a"(SYS_CLONE), "D"(flags), "S"(stack), "d"(0), "r"(r10), "r"(r8), "r"(r12)
                     : "rcx", "r11", "memory");
    return r;
}

// A spinning thread's breath: it tells the processor so.
static void cpu_relax(void) {
    __asm__ volatile("pause" ::: "memory");
}

static i64 sys_poll(PollFd *fds, u64 n, int wait) {
    return sys3(7, (i64)fds, (i64)n, wait ? -1 : 0);
}

static i64 sys_fork(void) {
    return sys1(57, 0);
}

static i64 sys_dup2(i64 from, i64 to) {
    return sys3(33, from, to, 0);
}

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
        "    xor %rbp, %rbp\n"
        "    mov %rsp, %rdi\n"
        "    and $-16, %rsp\n"
        "    call a0rt_start\n"
        "    hlt\n");
