// Linux on RISC-V 64, for linux.inc: system calls through ecall, with the
// number in a7; the numbers of the generic table, which has no poll, fork or
// dup2.

static i64 sys6(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    register i64 a7 __asm__("a7") = n;
    register i64 a0 __asm__("a0") = a;
    register i64 a1 __asm__("a1") = b;
    register i64 a2 __asm__("a2") = c;
    register i64 a3 __asm__("a3") = d;
    register i64 a4 __asm__("a4") = e;
    register i64 a5 __asm__("a5") = f;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5) : "memory");
    return a0;
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
        "    addi sp, sp, -208\n"
        "    sd ra, 0(sp)\n"
        "    sd s0, 8(sp)\n"
        "    sd s1, 16(sp)\n"
        "    sd s2, 24(sp)\n"
        "    sd s3, 32(sp)\n"
        "    sd s4, 40(sp)\n"
        "    sd s5, 48(sp)\n"
        "    sd s6, 56(sp)\n"
        "    sd s7, 64(sp)\n"
        "    sd s8, 72(sp)\n"
        "    sd s9, 80(sp)\n"
        "    sd s10, 88(sp)\n"
        "    sd s11, 96(sp)\n"
        "    fsd fs0, 104(sp)\n"
        "    fsd fs1, 112(sp)\n"
        "    fsd fs2, 120(sp)\n"
        "    fsd fs3, 128(sp)\n"
        "    fsd fs4, 136(sp)\n"
        "    fsd fs5, 144(sp)\n"
        "    fsd fs6, 152(sp)\n"
        "    fsd fs7, 160(sp)\n"
        "    fsd fs8, 168(sp)\n"
        "    fsd fs9, 176(sp)\n"
        "    fsd fs10, 184(sp)\n"
        "    fsd fs11, 192(sp)\n"
        "    sd sp, 0(a0)\n"
        "    mv sp, a1\n"
        "    ld ra, 0(sp)\n"
        "    ld s0, 8(sp)\n"
        "    ld s1, 16(sp)\n"
        "    ld s2, 24(sp)\n"
        "    ld s3, 32(sp)\n"
        "    ld s4, 40(sp)\n"
        "    ld s5, 48(sp)\n"
        "    ld s6, 56(sp)\n"
        "    ld s7, 64(sp)\n"
        "    ld s8, 72(sp)\n"
        "    ld s9, 80(sp)\n"
        "    ld s10, 88(sp)\n"
        "    ld s11, 96(sp)\n"
        "    fld fs0, 104(sp)\n"
        "    fld fs1, 112(sp)\n"
        "    fld fs2, 120(sp)\n"
        "    fld fs3, 128(sp)\n"
        "    fld fs4, 136(sp)\n"
        "    fld fs5, 144(sp)\n"
        "    fld fs6, 152(sp)\n"
        "    fld fs7, 160(sp)\n"
        "    fld fs8, 168(sp)\n"
        "    fld fs9, 176(sp)\n"
        "    fld fs10, 184(sp)\n"
        "    fld fs11, 192(sp)\n"
        "    addi sp, sp, 208\n"
        "    ret\n");

// A new fiber's stack, as a0rt_ctx_switch leaves it: all its registers, zero,
// but ra, where it returns to (rt.c's fiber_entry).
static u64 os_fiber_frame(void) {
    return 26;
}

static u64 os_fiber_saved(void) {
    return 0;
}

// A frame: the frame pointer is above its two saved registers.
static u64 frame_return(const u64 *frame) {
    return frame[-1];
}

static const u64 *frame_parent(const u64 *frame) {
    return (const u64 *)frame[-2];
}

// The kernel starts the program here, with argc at the top of the stack.
__asm__(".text\n"
        ".global _start\n"
        "_start:\n"
        "    li s0, 0\n"
        "    li ra, 0\n"
        "    mv a0, sp\n"
        "    andi sp, sp, -16\n"
        "    call a0rt_start\n"
        "    ebreak\n");
