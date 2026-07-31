#include <stdint.h>
#include <stddef.h>

#include "usermode.h"
#include "gdt.h"
#include "idt.h"
#include "dmesg.h"
#include "../graphics/terminal.h"
#include "sched.h"
#include "pit.h"
#include "../fs/vfs.h"
#include "../shell/commands/util.h"

#define SYS_WRITE 0
#define SYS_EXIT  1
#define SYS_READ  2
#define SYS_SLEEP 3
#define SYS_YIELD 4

#define IRETQ_FRAME_RSP 20
#define USER_RSP_GUARD  16

enum { R15=0, R14, R13, R12, R11, R10, R9, R8, RBP, RDI, RSI, RDX, RCX, RBX, RAX };

extern void task_exit(void);

__attribute__((naked)) void enter_userspace(uint64_t rip, uint64_t rsp) {
    asm volatile(
        "movq $0x23, %%rax;"
        "movq %%rax, %%ds;"
        "movq %%rax, %%es;"
        "movq %%rax, %%fs;"
        "movq %%rax, %%gs;"

        "pushq $0x23;"
        "pushq %%rsi;"
        "pushfq;"
        "popq %%rax;"
        "orq  $0x200, %%rax;"
        "pushq %%rax;"
        "pushq $0x1B;"
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
    "    leaq 1f(%rip), %rsi\n"
    "    movq $(2f - 1f), %rdx\n"
    "    xorq %rax, %rax\n"
    "    int $0x80\n"
    "    movq $3000, %rdi\n"
    "    movq $3, %rax\n"
    "    int $0x80\n"
    "    movq $1, %rax\n"
    "    int $0x80\n"
    "1:\n"
    "    .ascii \"  user mode ok \\n\"\n"
    "2:\n"
    "user_blob_end:\n"
);
extern uint8_t user_blob_start[];
extern uint8_t user_blob_end[];

#define USER_CODE_VA  0x400000ULL
#define USER_STACK_VA 0x401000ULL

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

static bool syscall_check_user_rsp(uint64_t *regs) {
    uint64_t rsp = regs[IRETQ_FRAME_RSP];

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

void syscall_dispatch(uint64_t *regs) {
    task_t *self = sched_current();
    uint32_t pid = self ? self->id : 0xFFFFFFFF;

    if (!syscall_check_user_rsp(regs)) {
        dmesg("[syscall] pid "); dmesg_int(pid);
        dmesg(" killing task: corrupt user rsp would break iretq\n");
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
        case SYS_EXIT:
            dmesg("[syscall] task exited\n");
            task_exit();
            break;
        default:
            regs[RAX] = (uint64_t)-1;
            break;
    }
}

extern uint64_t isr128_addr(void);

void syscall_init(void) {
    idt_set_gate(0x80, isr128_addr(), 0x08, 0xEE);
    dmesg("[syscall] int 0x80 gate installed\n");
}