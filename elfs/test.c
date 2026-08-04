typedef unsigned long u64;
typedef unsigned int  u32;

#define SYS_WRITE    1
#define SYS_MMAP     9
#define SYS_MPROTECT 10
#define SYS_SLEEP    35
#define SYS_EXIT     60

static inline long syscall3(long num, long a1, long a2, long a3) {
    long ret;
    register long r10 asm("r10") = 0;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall4(long num, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10 asm("r10") = a4;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static void print(const char *s) {
    u64 len = 0;
    while (s[len]) len++;
    syscall3(SYS_WRITE, 1, (long)s, (long)len);
}

static void print_hex(u64 n) {
    char buf[19];
    const char *digits = "0123456789abcdef";
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = digits[(n >> ((15 - i) * 4)) & 0xF];
    }
    buf[18] = 0;
    print(buf);
}

static void exit_now(int code) {
    syscall3(SYS_EXIT, code, 0, 0);
    while (1) { }
}

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_PRIVATE_ANON 0x22

static u64 do_mmap(u64 len, u64 prot) {
    return (u64)syscall4(SYS_MMAP, 0, (long)len, (long)prot, MAP_PRIVATE_ANON);
}

void _main(u64 argc, char **argv) {
    (void)argc; (void)argv;

    print("=== test-crashmprotect ===\n");
    print("this test is EXPECTED to kill the task with a #PF.\n");
    print("if you see 'FAIL' or the shell prompt returns cleanly\n");
    print("without a page fault log, mprotect is NOT enforcing write protection.\n\n");

    u64 addr = do_mmap(4096, PROT_READ | PROT_WRITE);
    if ((long)addr < 0) {
        print("FAIL: mmap returned error, cannot run test\n");
        exit_now(1);
    }
    print("mmap ok, addr="); print_hex(addr); print("\n");

    volatile char *p = (volatile char *)addr;
    p[0] = 'K';
    if (p[0] != 'K') {
        print("FAIL: page not actually writable before mprotect\n");
        exit_now(1);
    }
    print("PASS: page writable before mprotect\n");

    long r = syscall3(SYS_MPROTECT, (long)addr, 4096, PROT_READ);
    if (r != 0) {
        print("FAIL: mprotect(PROT_READ) returned error, cannot run test\n");
        exit_now(1);
    }
    print("PASS: mprotect(PROT_READ) returned 0\n");

    syscall3(SYS_SLEEP, 50, 0, 0);

    print("\n--- writing to read-only page now ---\n");
    p[0] = 'Z';

    /* if we reach this line, the write did NOT fault */
    print("FAIL: write to read-only page did NOT fault! mprotect is not enforced.\n");
    exit_now(1);
}

asm(
    ".global _start\n"
    "_start:\n"
    "    movq (%rsp), %rdi\n"
    "    leaq 8(%rsp), %rsi\n"
    "    andq $-16, %rsp\n"
    "    call _main\n"
    "    movq $60, %rax\n"
    "    xorq %rdi, %rdi\n"
    "    syscall\n"
);