#include <stdint.h>
#include <stddef.h>
#include <libk/string.h>

#include "usermode.h"
#include "gdt.h"
#include "idt.h"
#include "dmesg.h"
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

    dmesg("[usertest] pid "); dmesg_int(pid); dmesg(" allocating pages\n");

    uint64_t size = (uint64_t)(user_blob_end - user_blob_start);
    void *code_phys  = pmm_alloc_page();
    void *stack_phys = pmm_alloc_page();
    if (!code_phys || !stack_phys) {
        dmesg("[usertest] pid "); dmesg_int(pid); dmesg(" out of memory\n");
        return;
    }

    uint8_t *code_dst = (uint8_t *)(hhdm_offset + (uint64_t)code_phys);
    for (uint64_t i = 0; i < size; i++) code_dst[i] = user_blob_start[i];

    vmm_map(vmm_current(), USER_CODE_VA,  (uint64_t)code_phys,  VMM_FLAGS_USER);
    vmm_map(vmm_current(), USER_STACK_VA, (uint64_t)stack_phys, VMM_FLAGS_USER);

    dmesg("[usertest] pid "); dmesg_int(pid);
    dmesg(" code_phys="); dmesg_hex((uint64_t)code_phys);
    dmesg(" stack_phys="); dmesg_hex((uint64_t)stack_phys);
    dmesg("\n");

    dmesg("[usertest] pid "); dmesg_int(pid); dmesg(" entering ring 3\n");
    enter_userspace(USER_CODE_VA, USER_STACK_VA + 0x1000 - 16);
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
        dmesg("[syscall] rejected bad user pointer\n");
        return false;
    }
    return true;
}

static bool syscall_check_user_rsp(void) {
    task_t *self = sched_current();
    if (!self) return false;
    uint64_t rsp = self->user_rsp;

    if (rsp < USER_RSP_GUARD) {
        dmesg("[syscall] rejected bad user rsp (too low)\n");
        return false;
    }
    if (!vmm_check_user_range(vmm_current(), rsp - USER_RSP_GUARD, USER_RSP_GUARD, true)) {
        dmesg("[syscall] rejected bad user rsp (unmapped)\n");
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
        dmesg("[syscall] pid "); dmesg_int(pid);
        dmesg(" killing task: corrupt user rsp\n");
        task_exit();
        return;
    }

    uint64_t num = regs[RAX];

    dmesg("[syscall] pid "); dmesg_int(pid);
    dmesg(" num="); dmesg_int((uint32_t)num); dmesg("\n");

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
            for (uint64_t i = 0; i < len && buf[i]; i++) {
                char c[2] = { buf[i], 0 };
                terminal_print(c);
            }
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
                dmesg("[syscall] rejected unterminated path\n");
                regs[RAX] = (uint64_t)-1;
                break;
            }

            vfs_node_t *node = vfs_find(path);
            if (!node) {
                regs[RAX] = (uint64_t)-1;
                break;
            }

            regs[RAX] = vfs_read(node, (uint32_t)offset, (uint32_t)len, (uint8_t *)buf_ptr);
            break;
        }
        case SYS_SLEEP: {
            uint32_t ms = (uint32_t)regs[RDI];

            uint64_t tf = terminal_lock();
            terminal_set_fg(COLOR_DIM);
            terminal_print("  [pid ");
            terminal_print_int(pid);
            terminal_print("] sleeping for ");
            terminal_print_int(ms);
            terminal_println("ms...");
            terminal_unlock(tf);

            sleep_ms(ms);

            tf = terminal_lock();
            terminal_set_fg(COLOR_SUCCESS);
            terminal_print("  [pid ");
            terminal_print_int(pid);
            terminal_println("] woke up");
            terminal_unlock(tf);

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
                    vmm_map(vmm_current(), old_page_end + (i * PAGE_SIZE), (uint64_t)phys, VMM_FLAGS_USER);
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
                vmm_map(vmm_current(), start_va + i, (uint64_t)phys, VMM_FLAGS_USER);
            }
            self->user_mmap_base += alloc_len;
            regs[RAX] = start_va;
        mmap_done:
            break;
        }
        case SYS_EXIT:
            dmesg("[syscall] task exited\n");
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
        case SYS_MPROTECT:
        case SYS_MUNMAP:
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

    dmesg("[syscall] syscall/sysret configured\n");
}