#include <stdint.h>
#include <stddef.h>
#include <libk/string.h>

#include "vfs.h"
#include "../kernel/sched.h"
#include "../kernel/klog.h"

#define PROC_BUF_MAX 512

static void ksnprintf_uptime(char *out, size_t cap, uint64_t whole, uint64_t frac);
static int kstrcpy_at(char *dst, const char *src);
static int kuitoa_at(char *dst, uint32_t v);
extern void *kmalloc(size_t size);

static uint32_t proc_copy_str(const char *src, uint32_t offset, uint32_t len, uint8_t *buf) {
    uint32_t total = (uint32_t)strlen(src);
    if (offset >= total) return 0;
    uint32_t avail = total - offset;
    uint32_t n = (len < avail) ? len : avail;
    memcpy(buf, src + offset, n);
    return n;
}

static uint32_t proc_read_version(vfs_node_t *node, uint32_t offset, uint32_t len, uint8_t *buf) {
    (void)node;
    static const char *s = "KiNBOL version 0.08 (syscall-abi: linux-x86_64) #1\n";
    return proc_copy_str(s, offset, len, buf);
}

static uint32_t proc_read_cpuinfo(vfs_node_t *node, uint32_t offset, uint32_t len, uint8_t *buf) {
    (void)node;
    
    static const char *s =
        "processor\t: 0\n"
        "vendor_id\t: unknown\n"
        "cpu family\t: 6\n"
        "model name\t: KiNBOL virtual CPU\n"
        "flags\t\t: fpu sse sse2 tsc syscall lm nx\n"
        "bogomips\t: 0.00\n\n";
    return proc_copy_str(s, offset, len, buf);
}

static uint32_t proc_read_uptime(vfs_node_t *node, uint32_t offset, uint32_t len, uint8_t *buf) {
    (void)node;
    extern uint64_t uptime_ms(void);
    char tmp[64];
    uint64_t ms = uptime_ms();
    uint64_t whole = ms / 1000;
    uint64_t frac = (ms % 1000) / 10;
    ksnprintf_uptime(tmp, sizeof(tmp), whole, frac);
    return proc_copy_str(tmp, offset, len, buf);
}

static void ksnprintf_uptime(char *out, size_t cap, uint64_t whole, uint64_t frac) {
    size_t i = 0;
    char tmp[24];
    int n = 0;
    if (whole == 0) tmp[n++] = '0';
    while (whole > 0 && (size_t)n < sizeof(tmp)) { tmp[n++] = '0' + (whole % 10); whole /= 10; }
    while (n > 0 && i + 1 < cap) out[i++] = tmp[--n];
    if (i + 1 < cap) out[i++] = '.';
    if (i + 1 < cap) out[i++] = '0' + (frac / 10);
    if (i + 1 < cap) out[i++] = '0' + (frac % 10);
    if (i + 1 < cap) out[i++] = ' ';
    if (i + 1 < cap) out[i++] = '0';
    if (i + 1 < cap) out[i++] = '\n';
    out[i < cap ? i : cap - 1] = 0;
}

static uint32_t proc_read_self_status(vfs_node_t *node, uint32_t offset, uint32_t len, uint8_t *buf) {
    (void)node;
    task_t *self = sched_current();
    char tmp[PROC_BUF_MAX];
    int n = 0;

    const char *name = self ? self->name : "?";
    uint32_t pid = self ? self->id : 0;

    n += kstrcpy_at(tmp + n, "Name:\t");
    n += kstrcpy_at(tmp + n, name);
    n += kstrcpy_at(tmp + n, "\nPid:\t");
    n += kuitoa_at(tmp + n, pid);
    n += kstrcpy_at(tmp + n, "\nState:\tR (running)\n");
    tmp[n] = 0;

    return proc_copy_str(tmp, offset, len, buf);
}

static int kstrcpy_at(char *dst, const char *src) {
    int n = 0;
    while (src[n]) { dst[n] = src[n]; n++; }
    return n;
}

static int kuitoa_at(char *dst, uint32_t v) {
    char tmp[12];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    int len = n;
    for (int i = 0; i < len; i++) dst[i] = tmp[len - 1 - i];
    return len;
}

static uint32_t proc_read_self_exe(vfs_node_t *node, uint32_t offset, uint32_t len, uint8_t *buf) {
    (void)node;
    
    static const char *s = "/unknown\n";
    return proc_copy_str(s, offset, len, buf);
}

static void proc_register(const char *name, uint32_t size, uint32_t (*read)(vfs_node_t *, uint32_t, uint32_t, uint8_t *)) {
    vfs_node_t *n = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!n) return;
    memset(n, 0, sizeof(*n));
    kstrcpy_at(n->name, name);
    n->flags = VFS_FILE;
    n->size = size;
    n->read = read;
    vfs_register(n);
}

void procfs_init(void) {
    proc_register("proc/version", PROC_BUF_MAX, proc_read_version);
    proc_register("proc/cpuinfo", PROC_BUF_MAX, proc_read_cpuinfo);
    proc_register("proc/uptime", PROC_BUF_MAX, proc_read_uptime);
    proc_register("proc/self/status", PROC_BUF_MAX, proc_read_self_status);
    proc_register("proc/self/exe", PROC_BUF_MAX, proc_read_self_exe);
    KLOG_I("procfs", "minimal /proc mounted (version, cpuinfo, uptime, self/status, self/exe)");
}