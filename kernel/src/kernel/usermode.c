#include <stdint.h>
#include <stddef.h>

#include "usermode.h"
#include "gdt.h"
#include "idt.h"
#include "dmesg.h"
#include "../graphics/terminal.h"
#include "sched.h"

#define SYS_WRITE 0
#define SYS_EXIT  1

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
    return task_create("usertest", usertest_task_entry, NULL);
}


void syscall_dispatch(uint64_t *regs) {
    uint64_t num = regs[RAX];

    switch (num) {
        case SYS_WRITE: {
            const char *buf = (const char *)regs[RSI];
            uint64_t len = regs[RDX];
            for (uint64_t i = 0; i < len && buf[i]; i++) {
                char c[2] = { buf[i], 0 };
                terminal_print(c);
            }
            regs[RAX] = len;
            break;
        }
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