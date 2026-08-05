#pragma once

typedef unsigned long u64;
typedef long           i64;

#define SYS_READ      0
#define SYS_WRITE     1
#define SYS_OPEN      2
#define SYS_CLOSE     3
#define SYS_STAT      4
#define SYS_FSTAT     5
#define SYS_LSEEK     8
#define SYS_MMAP      9
#define SYS_MPROTECT  10
#define SYS_MUNMAP    11
#define SYS_BRK       12
#define SYS_IOCTL     16
#define SYS_YIELD     24
#define SYS_SLEEP     35
#define SYS_EXIT      60
#define SYS_ARCH_PRCTL 158
#define SYS_SET_TID_ADDRESS 218

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4
#define MAP_PRIVATE_ANON 0x22

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

static inline i64 kin_syscall0(i64 num) {
    i64 ret;
    asm volatile("syscall" : "=a"(ret) : "a"(num) : "rcx", "r11", "memory");
    return ret;
}

static inline i64 kin_syscall1(i64 num, i64 a1) {
    i64 ret;
    asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline i64 kin_syscall3(i64 num, i64 a1, i64 a2, i64 a3) {
    i64 ret;
    asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

static inline i64 kin_syscall4(i64 num, i64 a1, i64 a2, i64 a3, i64 a4) {
    i64 ret;
    register i64 r10 asm("r10") = a4;
    asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}

static inline i64 kin_open(const char *path)              { return kin_syscall3(SYS_OPEN, (i64)path, 0, 0); }
static inline i64 kin_close(i64 fd)                        { return kin_syscall1(SYS_CLOSE, fd); }
static inline i64 kin_read(i64 fd, void *buf, u64 len)      { return kin_syscall3(SYS_READ, fd, (i64)buf, (i64)len); }
static inline i64 kin_write(i64 fd, const void *buf, u64 len){ return kin_syscall3(SYS_WRITE, fd, (i64)buf, (i64)len); }
static inline i64 kin_lseek(i64 fd, i64 offset, i64 whence) { return kin_syscall3(SYS_LSEEK, fd, offset, whence); }
static inline i64 kin_stat(const char *path, void *statbuf) { return kin_syscall3(SYS_STAT, (i64)path, (i64)statbuf, 0); }
static inline i64 kin_fstat(i64 fd, void *statbuf)          { return kin_syscall3(SYS_FSTAT, fd, (i64)statbuf, 0); }
static inline i64 kin_mmap(u64 len, i64 prot)                { return kin_syscall4(SYS_MMAP, 0, (i64)len, prot, MAP_PRIVATE_ANON); }
static inline i64 kin_mprotect(void *addr, u64 len, i64 prot){ return kin_syscall3(SYS_MPROTECT, (i64)addr, (i64)len, prot); }
static inline i64 kin_munmap(void *addr, u64 len)            { return kin_syscall3(SYS_MUNMAP, (i64)addr, (i64)len, 0); }
static inline i64 kin_brk(void *addr)                         { return kin_syscall1(SYS_BRK, (i64)addr); }
static inline i64 kin_sleep_ms(u64 ms)                        { return kin_syscall1(SYS_SLEEP, (i64)ms); }
static inline i64 kin_yield(void)                              { return kin_syscall0(SYS_YIELD); }
static inline void kin_exit(int code) {
    kin_syscall1(SYS_EXIT, code);
    while (1) { }
}

static inline u64 kin_strlen(const char *s) {
    u64 n = 0;
    while (s[n]) n++;
    return n;
}

static inline void kin_print(const char *s) {
    kin_write(1, s, kin_strlen(s));
}

static inline void kin_print_int(i64 n) {
    char buf[24];
    int i = 0;
    int neg = n < 0;
    u64 v = neg ? (u64)(-n) : (u64)n;
    if (v == 0) buf[i++] = '0';
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    if (neg) buf[i++] = '-';
    char out[24];
    for (int j = 0; j < i; j++) out[j] = buf[i - 1 - j];
    out[i] = 0;
    kin_print(out);
}

static inline void kin_print_hex(u64 n) {
    char buf[19];
    const char *digits = "0123456789abcdef";
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) buf[2 + i] = digits[(n >> ((15 - i) * 4)) & 0xF];
    buf[18] = 0;
    kin_print(buf);
}

#define KIN_MAIN_TRAMPOLINE \
asm( \
    ".global _start\n" \
    "_start:\n" \
    "    movq (%rsp), %rdi\n" \
    "    leaq 8(%rsp), %rsi\n" \
    "    andq $-16, %rsp\n" \
    "    call _main\n" \
    "    movq $60, %rax\n" \
    "    xorq %rdi, %rdi\n" \
    "    syscall\n" \
);