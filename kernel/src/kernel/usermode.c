#include <stdint.h>
#include <stddef.h>
#include <libk/string.h>

#include "usermode.h"
#include "gdt.h"
#include "idt.h"
#include "dmesg.h"
#include "klog.h"
#include "../graphics/terminal.h"
#include "sched.h"
#include "pit.h"
#include "../fs/vfs.h"
#include "../shell/commands/util.h"

#define SYS_READ  0
#define SYS_WRITE 1
#define SYS_OPEN  2
#define SYS_CLOSE 3
#define SYS_STAT  4
#define SYS_FSTAT 5
#define SYS_LSEEK 8
#define SYS_MMAP  9
#define SYS_MPROTECT 10
#define SYS_MUNMAP 11
#define SYS_BRK   12
#define SYS_IOCTL 16
#define SYS_YIELD 24
#define SYS_SLEEP 35
#define SYS_EXIT  60
#define SYS_ARCH_PRCTL 158
#define SYS_SET_TID_ADDRESS 218

#define USER_RSP_GUARD  16

enum { R15=0, R14, R13, R12, R11, R10, R9, R8, RBP, RDI, RSI, RDX, RCX, RBX, RAX };

extern void task_exit(void);

__attribute__((naked)) void enter_userspace(uint64_t rip, uint64_t rsp) {
    asm volatile(
        "movq $0x1B, %%rax;"
        "movq %%rax, %%ds;"
        "movq %%rax, %%es;"
        "movq %%rax, %%fs;"
        "movq %%rax, %%gs;"

        "pushq $0x1B;"
        "pushq %%rsi;"
        "pushfq;"
        "popq %%rax;"
        "orq  $0x200, %%rax;"
        "pushq %%rax;"
        "pushq $0x23;"
        "pushq %%rdi;"
        "iretq;"
        ::: "memory"
    );
}

#include "../mm/pmm.h"
#include "../mm/vmm.h"

extern uint64_t hhdm_offset;

asm(
    ".global user_blob_start\n"
    ".global user_blob_end\n"
    "user_blob_start:\n"
    "    movq $0, %rdi\n"
    "    movq $12, %rax\n"
    "    syscall\n"
    "    movq %rax, %r8\n"
    "    movq %r8, %rdi\n"
    "    addq $4096, %rdi\n"
    "    movq $12, %rax\n"
    "    syscall\n"
    "    movb $'s', 0(%r8)\n"
    "    movb $'y', 1(%r8)\n"
    "    movb $'s', 2(%r8)\n"
    "    movb $'_', 3(%r8)\n"
    "    movb $'b', 4(%r8)\n"
    "    movb $'r', 5(%r8)\n"
    "    movb $'k', 6(%r8)\n"
    "    movb $' ', 7(%r8)\n"
    "    movb $'O', 8(%r8)\n"
    "    movb $'K', 9(%r8)\n"
    "    movb $'\\n', 10(%r8)\n"
    "    movq $1, %rdi\n"
    "    movq %r8, %rsi\n"
    "    movq $11, %rdx\n"
    "    movq $1, %rax\n"
    "    syscall\n"
    "    movq $3000, %rdi\n"
    "    movq $35, %rax\n"
    "    syscall\n"
    "    movq $0, %rdi\n"
    "    movq $60, %rax\n"
    "    syscall\n"
    "user_blob_end:\n"
);
extern uint8_t user_blob_start[];
extern uint8_t user_blob_end[];

void usertest_run(void) {
    task_t *self = sched_current();
    uint32_t pid = self ? self->id : 0xFFFFFFFF;

    KLOG_D("usertest", "pid %d allocating pages", pid);

    uint64_t size = (uint64_t)(user_blob_end - user_blob_start);
    void *code_phys  = pmm_alloc_page();
    void *stack_phys = pmm_alloc_page();
    if (!code_phys || !stack_phys) {
        KLOG_E("usertest", "pid %d out of memory", pid);
        return;
    }

    uint8_t *code_dst = (uint8_t *)(hhdm_offset + (uint64_t)code_phys);
    for (uint64_t i = 0; i < size; i++) code_dst[i] = user_blob_start[i];

    vmm_map(vmm_current(), USER_CODE_VA,  (uint64_t)code_phys,  VMM_FLAGS_USER_CODE);
    vmm_map(vmm_current(), USER_STACK_VA, (uint64_t)stack_phys, VMM_FLAGS_USER_DATA);

    KLOG_D("usertest", "pid %d code_phys=%p stack_phys=%p", pid, code_phys, stack_phys);

    KLOG_I("usertest", "pid %d entering ring 3", pid);
    enter_userspace(USER_CODE_VA, USER_STACK_VA + 0x1000 - 16);
}

#include "../loader/elf.h"
#include "../mm/heap.h"

static uint64_t build_initial_user_stack(uint64_t stack_va, void *stack_phys,
                                          const char *argv0, elf_load_result_t *res) {
    uint8_t *page = (uint8_t *)(hhdm_offset + (uint64_t)stack_phys);
    uint64_t off = PAGE_SIZE;

    size_t argv0_len = strlen(argv0) + 1;
    off -= argv0_len;
    memcpy(page + off, argv0, argv0_len);
    uint64_t argv0_va = stack_va + off;

    off &= ~0xFULL;
    off -= 128;
    uint64_t block_va = stack_va + off;

    uint64_t *w = (uint64_t *)(page + off);
    w[0]  = 1;
    w[1]  = argv0_va;
    w[2]  = 0;
    w[3]  = 0;
    w[4]  = AT_PHDR;  w[5]  = res->phdr_vaddr;
    w[6]  = AT_PHENT; w[7]  = res->phent;
    w[8]  = AT_PHNUM; w[9]  = res->phnum;
    w[10] = AT_ENTRY; w[11] = res->entry;
    w[12] = AT_BASE;  w[13] = res->load_bias;
    w[14] = AT_NULL;  w[15] = 0;

    return block_va;
}

void exec_run(void *arg) {
    char *path = (char *)arg;
    task_t *self = sched_current();
    uint32_t pid = self ? self->id : 0xFFFFFFFF;

    KLOG_I("exec", "pid %d loading %s", pid, path);

    elf_load_result_t res;
    if (elf_load(path, vmm_current(), &res) != 0) {
        KLOG_E("exec", "pid %d elf_load failed", pid);
        kfree(path);
        task_exit();
        return;
    }

    uint64_t stack_va = res.highest_vaddr + PAGE_SIZE;

    void *stack_phys = pmm_alloc_page();
    if (!stack_phys) {
        KLOG_E("exec", "pid %d out of memory for stack", pid);
        kfree(path);
        task_exit();
        return;
    }
    memset((void *)(hhdm_offset + (uint64_t)stack_phys), 0, PAGE_SIZE);

    uint64_t user_rsp = build_initial_user_stack(stack_va, stack_phys, path, &res);

    vmm_map(vmm_current(), stack_va, (uint64_t)stack_phys, VMM_FLAGS_USER_DATA);

    if (self) {
        self->user_brk = res.highest_vaddr;
        self->user_mmap_base = USER_MMAP_START;
    }

    kfree(path);

    KLOG_I("exec", "pid %d entering ring 3 at %p rsp=%p", pid, (void *)res.entry, (void *)user_rsp);

    enter_userspace(res.entry, user_rsp);
}

static void exec_task_entry(void *arg) {
    exec_run(arg);
}

task_t *exec_launch(const char *path) {
    size_t len = 0;
    while (path[len] && len < 255) len++;

    char *path_copy = (char *)kmalloc(len + 1);
    if (!path_copy) return NULL;
    for (size_t i = 0; i < len; i++) path_copy[i] = path[i];
    path_copy[len] = 0;

    task_t *t = task_create_user("exec", exec_task_entry, path_copy);
    if (!t) kfree(path_copy);
    return t;
}

static void usertest_task_entry(void *arg) {
    (void)arg;
    usertest_run();
}

task_t *usertest_launch(void) {
    return task_create_user("usertest", usertest_task_entry, NULL);
}

static bool syscall_check_user_ptr(uint64_t ptr, uint64_t len, bool need_write) {
    if (len == 0) return false;
    if (!vmm_check_user_range(vmm_current(), ptr, len, need_write)) {
        KLOG_W("syscall", "rejected bad user pointer");
        return false;
    }
    return true;
}

static bool syscall_check_user_rsp(void) {
    task_t *self = sched_current();
    if (!self) return false;
    uint64_t rsp = self->user_rsp;

    if (rsp < USER_RSP_GUARD) {
        KLOG_W("syscall", "rejected bad user rsp (too low)");
        return false;
    }
    if (!vmm_check_user_range(vmm_current(), rsp - USER_RSP_GUARD, USER_RSP_GUARD, true)) {
        KLOG_W("syscall", "rejected bad user rsp (unmapped)");
        return false;
    }
    return true;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = val & 0xFFFFFFFF;
    uint32_t high = val >> 32;
    asm volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

#define MSR_EFER  0xC0000080
#define MSR_STAR  0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_FMASK 0xC0000084

void syscall_dispatch(uint64_t *regs) {
    task_t *self = sched_current();
    uint32_t pid = self ? self->id : 0xFFFFFFFF;

    if (!syscall_check_user_rsp()) {
        KLOG_E("syscall", "pid %d killing task: corrupt user rsp", pid);
        task_exit();
        return;
    }

    uint64_t num = regs[RAX];

    KLOG_T("syscall", "pid %d num=%d", pid, (uint32_t)num);

    switch (num) {
        case SYS_WRITE: {
            uint64_t uptr = regs[RSI];
            uint64_t len  = regs[RDX];

            if (!syscall_check_user_ptr(uptr, len, false)) {
                regs[RAX] = (uint64_t)-1;
                break;
            }

            const char *buf = (const char *)uptr;
            asm volatile("cli");
            terminal_ensure_newline();
            smap_stac();
            for (uint64_t i = 0; i < len && buf[i]; i++) {
                char c[2] = { buf[i], 0 };
                terminal_print(c);
            }
            smap_clac();
            terminal_ensure_newline();
            asm volatile("sti");
            regs[RAX] = len;
            break;
        }
        case SYS_READ: {
            uint64_t path_ptr = regs[RDI];
            uint64_t buf_ptr  = regs[RSI];
            uint64_t len      = regs[RDX];
            uint64_t offset   = regs[R10];

            if (!syscall_check_user_ptr(path_ptr, VFS_NAME_MAX, false) ||
                !syscall_check_user_ptr(buf_ptr, len, true)) {
                regs[RAX] = (uint64_t)-1;
                break;
            }

            const char *path = (const char *)path_ptr;
            uint64_t plen = 0;
            while (plen < VFS_NAME_MAX && path[plen]) plen++;
            if (plen == VFS_NAME_MAX) {
                KLOG_W("syscall", "rejected unterminated path");
                regs[RAX] = (uint64_t)-1;
                break;
            }

            vfs_node_t *node = vfs_find(path);
            if (!node) {
                regs[RAX] = (uint64_t)-1;
                break;
            }

            smap_stac();
            regs[RAX] = vfs_read(node, (uint32_t)offset, (uint32_t)len, (uint8_t *)buf_ptr);
            smap_clac();
            break;
        }
        case SYS_SLEEP: {
            uint32_t ms = (uint32_t)regs[RDI];

            KLOG_D("sched", "pid %d sleeping for %ums", pid, ms);
            sleep_ms(ms);
            KLOG_D("sched", "pid %d woke up", pid);

            regs[RAX] = 0;
            break;
        }
        case SYS_YIELD:
            sched_yield();
            regs[RAX] = 0;
            break;
        case SYS_BRK: {
            uint64_t addr = regs[RDI];
            if (addr == 0 || addr == self->user_brk) {
                regs[RAX] = self->user_brk;
                break;
            }
            if (addr < USER_HEAP_START) {
                regs[RAX] = self->user_brk;
                break;
            }

            uint64_t old_page_end = (self->user_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t new_page_end = (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

            if (new_page_end > old_page_end) {
                uint64_t pages_needed = (new_page_end - old_page_end) / PAGE_SIZE;
                for (uint64_t i = 0; i < pages_needed; i++) {
                    void *phys = pmm_alloc_page();
                    if (!phys) {
                        regs[RAX] = self->user_brk;
                        goto brk_done;
                    }
                    uint8_t *v = (uint8_t *)(hhdm_offset + (uint64_t)phys);
                    memset(v, 0, PAGE_SIZE);
                    vmm_map(vmm_current(), old_page_end + (i * PAGE_SIZE), (uint64_t)phys, VMM_FLAGS_USER_DATA);
                }
            }
        brk_done:
            self->user_brk = addr;
            regs[RAX] = self->user_brk;
            break;
        }
        case SYS_MMAP: {
            uint64_t len   = regs[RSI];
            uint32_t flags = (uint32_t)regs[R10];

            if ((flags & 0x22) != 0x22) {
                regs[RAX] = (uint64_t)-22;
                break;
            }
            if (len == 0) {
                regs[RAX] = (uint64_t)-22;
                break;
            }

            uint64_t alloc_len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

            if (self->user_mmap_base + alloc_len >= USER_STACK_TOP) {
                regs[RAX] = (uint64_t)-12;
                break;
            }

            uint64_t start_va = self->user_mmap_base;
            for (uint64_t i = 0; i < alloc_len; i += PAGE_SIZE) {
                void *phys = pmm_alloc_page();
                if (!phys) {
                    regs[RAX] = (uint64_t)-12;
                    goto mmap_done;
                }
                uint8_t *v = (uint8_t *)(hhdm_offset + (uint64_t)phys);
                memset(v, 0, PAGE_SIZE);
                vmm_map(vmm_current(), start_va + i, (uint64_t)phys, VMM_FLAGS_USER_DATA);
            }
            self->user_mmap_base += alloc_len;
            regs[RAX] = start_va;
        mmap_done:
            break;
        }
        case SYS_MPROTECT: {
            uint64_t addr = regs[RDI];
            uint64_t len  = regs[RSI];
            uint32_t prot = (uint32_t)regs[RDX];

            if ((addr & 0xFFF) != 0) {
                regs[RAX] = (uint64_t)-22;
                break;
            }
            if (len == 0) {
                regs[RAX] = 0;
                break;
            }
            if (!vmm_check_user_range(vmm_current(), addr, len, false)) {
                regs[RAX] = (uint64_t)-12;
                break;
            }

            uint64_t flags = VMM_PRESENT | VMM_USER;
            if (prot & 0x2) flags |= VMM_WRITE;
            if (!(prot & 0x4)) flags |= VMM_NX;

            if (vmm_protect(vmm_current(), addr, len, flags) != 0) {
                regs[RAX] = (uint64_t)-12;
                break;
            }

            KLOG_D("syscall", "pid %d mprotect addr=%p len=%u prot=%u", pid, (void *)addr, (uint32_t)len, prot);
            regs[RAX] = 0;
            break;
        }
        case SYS_MUNMAP: {
            uint64_t addr = regs[RDI];
            uint64_t len  = regs[RSI];

            if ((addr & 0xFFF) != 0 || len == 0) {
                regs[RAX] = (uint64_t)-22;
                break;
            }

            vmm_unmap_range(vmm_current(), addr, len, true);
            KLOG_D("syscall", "pid %d munmap addr=%p len=%u", pid, (void *)addr, (uint32_t)len);
            regs[RAX] = 0;
            break;
        }
        case SYS_EXIT:
            KLOG_I("syscall", "pid %d task exited", pid);
            task_exit();
            break;
        case SYS_ARCH_PRCTL: {
            uint32_t code = (uint32_t)regs[RDI];
            uint64_t addr = regs[RSI];
            if (code == 0x1002) {
                wrmsr(0xC0000100, addr);
                regs[RAX] = 0;
            } else {
                regs[RAX] = (uint64_t)-22;
            }
            break;
        }
        case SYS_SET_TID_ADDRESS: {
            regs[RAX] = self ? self->id : 1;
            break;
        }
        case SYS_OPEN:
        case SYS_CLOSE:
        case SYS_STAT:
        case SYS_FSTAT:
        case SYS_LSEEK:
        case SYS_IOCTL:
            regs[RAX] = (uint64_t)-38;
            break;
        default:
            regs[RAX] = (uint64_t)-38;
            break;
    }
}

void syscall_enter(uint64_t *regs) {
    syscall_dispatch(regs);
}

extern void syscall_entry(void);

void syscall_init(void) {
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1);

    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(MSR_STAR, star);

    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_FMASK, 0x200);

    KLOG_I("syscall", "syscall/sysret configured");
}