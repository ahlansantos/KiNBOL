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

#define SYS_WRITE 0
#define SYS_EXIT  1
#define SYS_READ  2
#define SYS_SLEEP 3
#define SYS_YIELD 4

/* Index of the saved user RSP inside the iretq frame that isr_common
 * builds on the kernel stack. Layout (each 8-byte slot, counted from
 * `regs` == rsp right after the 15 GPR pushes in isr_common):
 *   [0..14]  R15..RAX  (see enum below)
 *   [15]     vector
 *   [16]     error_code
 *   [17]     RIP    (pushed by CPU)
 *   [18]     CS     (pushed by CPU)
 *   [19]     RFLAGS (pushed by CPU)
 *   [20]     RSP    (pushed by CPU on a ring3->ring0 transition)
 *   [21]     SS     (pushed by CPU)
 */
#define IRETQ_FRAME_RSP 20

/* Bytes below the saved user RSP that must be mapped + writable for it
 * to be accepted. Just needs to be nonzero so a forged RSP (NULL, a
 * kernel address, unmapped garbage) gets caught here instead of being
 * handed straight back to `iretq`. */
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
    "    movq $13, %rdx\n"
    "    xorq %rax, %rax\n"
    "    int $0x80\n"
    "    movq $1, %rax\n"
    "    int $0x80\n"
    "1:\n"
    "    .ascii \"user mode ok\\n\"\n"
    "user_blob_end:\n"
);
extern uint8_t user_blob_start[];
extern uint8_t user_blob_end[];

#define USER_CODE_VA  0x400000ULL
#define USER_STACK_VA 0x401000ULL

void usertest_run(void) {
    uint64_t size = (uint64_t)(user_blob_end - user_blob_start);
    void *code_phys  = pmm_alloc_page();
    void *stack_phys = pmm_alloc_page();
    if (!code_phys || !stack_phys) { dmesg("[usertest] out of memory\n"); return; }

    uint8_t *code_dst = (uint8_t *)(hhdm_offset + (uint64_t)code_phys);
    for (uint64_t i = 0; i < size; i++) code_dst[i] = user_blob_start[i];

    vmm_map(vmm_current(), USER_CODE_VA,  (uint64_t)code_phys,  VMM_FLAGS_USER);
    vmm_map(vmm_current(), USER_STACK_VA, (uint64_t)stack_phys, VMM_FLAGS_USER);

    dmesg("[usertest] entering ring 3\n");
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

/* Validate the RSP the task had in ring3 at the moment it trapped in.
 * The CPU already saved it safely on the *kernel* stack as part of the
 * iretq frame, so a forged value can't corrupt kernel state directly -
 * but if we don't check it here, isr_common's `iretq` will happily load
 * it back into %rsp and hand control to ring3 with a broken stack
 * (NULL, a kernel address, or plain unmapped memory), which is exactly
 * the "corrupts the iretq return" case. Reject it in the same style as
 * syscall_check_user_ptr() so the task gets killed cleanly instead. */
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
    /* Every syscall entry is a ring3->ring0 transition, so regs[IRETQ_FRAME_RSP]
     * always holds the RSP the task will be handed back on the way out. Check
     * it up front, before we even look at which syscall was requested - a task
     * that forges RSP doesn't get to run SYS_WRITE first. */
    if (!syscall_check_user_rsp(regs)) {
        dmesg("[syscall] killing task: corrupt user rsp would break iretq\n");
        task_exit();
        return;
    }

    uint64_t num = regs[RAX];

    switch (num) {
        case SYS_WRITE: {
            uint64_t uptr = regs[RSI];
            uint64_t len  = regs[RDX];

            if (!syscall_check_user_ptr(uptr, len, false)) {
                regs[RAX] = (uint64_t)-1;
                break;
            }

            const char *buf = (const char *)uptr;
            for (uint64_t i = 0; i < len && buf[i]; i++) {
                char c[2] = { buf[i], 0 };
                terminal_print(c);
            }
            regs[RAX] = len;
            break;
        }
        case SYS_READ: {
            /* RDI = path ptr (NUL-terminated, <= VFS_NAME_MAX-1 bytes)
             * RSI = dest buffer ptr (user, writable)
             * RDX = len
             * R10 = offset into the file */
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
            /* RDI = milliseconds. Blocks the calling task via the existing
             * TASK_BLOCKED/wake_time_ms mechanism (see pit.c:sleep_ms). */
            uint32_t ms = (uint32_t)regs[RDI];
            sleep_ms(ms);
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