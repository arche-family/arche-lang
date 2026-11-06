// A loader of Arche executables (docs/abi.md), to run one on Linux: what Arche
// OS's kernel will do, on a system that exists today. It maps the segments,
// gives the program a stack as Linux would, and jumps to the entry.
// Usage: ax-load <program.ax> [args]. Built with no C library, for any target.
typedef unsigned long u64;
typedef unsigned int u32;
typedef unsigned char u8;

enum { READ = 4, WRITE = 2, EXEC = 1, HEADER = 40, SEGMENT = 40, STACK = 8 * 1024 * 1024 };

#if defined(__x86_64__)
enum { SYS_READ = 0, SYS_MMAP = 9, SYS_MPROTECT = 10, SYS_LSEEK = 8, SYS_OPENAT = 257, SYS_EXIT = 60, SYS_WRITE = 1 };
static long sys6(long n, long a, long b, long c, long d, long e, long f) {
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return r;
}
__asm__(".text\n.global _start\n_start:\n  mov %rsp, %rdi\n  and $-16, %rsp\n  call load\n");
static void enter(u64 *sp, u64 entry) {
    __asm__ volatile("mov %0, %%rsp\n xor %%ebp, %%ebp\n jmp *%1\n" : : "r"(sp), "r"(entry));
    __builtin_unreachable();
}
#elif defined(__aarch64__)
enum { SYS_READ = 63, SYS_MMAP = 222, SYS_MPROTECT = 226, SYS_LSEEK = 62, SYS_OPENAT = 56, SYS_EXIT = 93, SYS_WRITE = 64 };
static long sys6(long n, long a, long b, long c, long d, long e, long f) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return x0;
}
__asm__(".text\n.global _start\n_start:\n  mov x0, sp\n  bl load\n");
static void enter(u64 *sp, u64 entry) {
    __asm__ volatile("mov sp, %0\n mov x29, #0\n br %1\n" : : "r"(sp), "r"(entry));
    __builtin_unreachable();
}
#elif defined(__riscv)
enum { SYS_READ = 63, SYS_MMAP = 222, SYS_MPROTECT = 226, SYS_LSEEK = 62, SYS_OPENAT = 56, SYS_EXIT = 93, SYS_WRITE = 64 };
static long sys6(long n, long a, long b, long c, long d, long e, long f) {
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    register long a3 __asm__("a3") = d;
    register long a4 __asm__("a4") = e;
    register long a5 __asm__("a5") = f;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5) : "memory");
    return a0;
}
__asm__(".text\n.global _start\n_start:\n  mv a0, sp\n  call load\n");
static void enter(u64 *sp, u64 entry) {
    __asm__ volatile("mv sp, %0\n li s0, 0\n jr %1\n" : : "r"(sp), "r"(entry));
    __builtin_unreachable();
}
#endif

static void die(const char *text) {
    u64 n = 0;
    while (text[n]) {
        n++;
    }
    sys6(SYS_WRITE, 2, (long)text, (long)n, 0, 0, 0);
    sys6(SYS_EXIT, 127, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

static u64 get(const u8 *b, int size) {
    u64 v = 0;
    for (int i = 0; i < size; i++) {
        v |= (u64)b[i] << (i * 8);
    }
    return v;
}

static void read_all(long fd, u8 *into, u64 size) {
    while (size > 0) {
        long n = sys6(SYS_READ, fd, (long)into, (long)size, 0, 0, 0);
        if (n <= 0) {
            die("ax-load: the file is cut short\n");
        }
        into += n;
        size -= (u64)n;
    }
}

__attribute__((used)) void load(u64 *start) {
    u64 argc = start[0];
    char **argv = (char **)(start + 1);
    if (argc < 2) {
        die("usage: ax-load <program.ax> [args]\n");
    }
    long fd = sys6(SYS_OPENAT, -100, (long)argv[1], 0, 0, 0, 0);
    if (fd < 0) {
        die("ax-load: cannot open the file\n");
    }
    static u8 table[4096];
    read_all(fd, table, HEADER);
    const char *magic = "ARCHEAX\n";
    for (int i = 0; i < 8; i++) {
        if (table[i] != (u8)magic[i]) {
            die("ax-load: not an Arche executable\n");
        }
    }
    u64 count = get(table + 20, 4);
    u64 entry = get(table + 24, 8);
    u64 page = get(table + 32, 8);
    if (get(table + 8, 4) != 1 || count > 8 || page == 0) {
        die("ax-load: not an Arche executable of this format\n");
    }
    read_all(fd, table + HEADER, count * SEGMENT);
    for (u64 s = 0; s < count; s++) {
        const u8 *e = table + HEADER + s * SEGMENT;
        u64 flags = get(e, 4);
        u64 address = get(e + 8, 8);
        u64 offset = get(e + 16, 8);
        u64 file_size = get(e + 24, 8);
        u64 memory_size = get(e + 32, 8);
        if (memory_size == 0) {
            continue;
        }
        u64 size = (memory_size + page - 1) / page * page;
        long at = sys6(SYS_MMAP, (long)address, (long)size, READ >> 2 | WRITE >> 0, 0x32, -1, 0);
        if (at != (long)address) {
            die("ax-load: cannot map a segment\n");
        }
        sys6(SYS_LSEEK, fd, (long)offset, 0, 0, 0, 0);
        read_all(fd, (u8 *)address, file_size);
        long protection = 0;
        if (flags & READ) {
            protection |= 1;
        }
        if (flags & WRITE) {
            protection |= 2;
        }
        if (flags & EXEC) {
            protection |= 4;
        }
        sys6(SYS_MPROTECT, (long)address, (long)size, protection, 0, 0, 0);
    }
    // argc, argv without this program's name, the environment, and the
    // auxiliary vector, on a new stack.
    u64 *from = start + 2;
    u64 n = 0;
    while (from[n] != 0) {
        n++;
    }
    n++;
    while (from[n] != 0) {
        n++;
    }
    n++;
    while (from[n] != 0) {
        n += 2;
    }
    n += 2;
    long stack = sys6(SYS_MMAP, 0, STACK, 3, 0x22, -1, 0);
    if (stack < 0 && stack > -4096) {
        die("ax-load: no memory for a stack\n");
    }
    u64 *top = (u64 *)(((u64)stack + STACK - (n + 2) * 8) & ~(u64)15);
    top[0] = argc - 1;
    for (u64 i = 0; i < n; i++) {
        top[i + 1] = from[i];
    }
    enter(top, entry);
}
