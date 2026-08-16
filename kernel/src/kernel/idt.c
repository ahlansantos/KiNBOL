#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "idt.h"
#include "../graphics/terminal.h"
#include "dmesg.h"
#include "../mm/vmm.h"
#include "sched.h"

#define IDT_ENTRIES 256

typedef struct {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_mid;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idt_ptr;

__attribute__((naked)) static void isr_common(void);
__attribute__((naked)) static void isr0(void);
__attribute__((naked)) static void isr6(void);
__attribute__((naked)) static void isr8(void);
__attribute__((naked)) static void isr13(void);
__attribute__((naked)) static void isr14(void);

#define IRQ_STUB_DECL(vec) __attribute__((naked)) static void isr##vec(void);
IRQ_STUB_DECL(32) IRQ_STUB_DECL(33) IRQ_STUB_DECL(34) IRQ_STUB_DECL(35)
IRQ_STUB_DECL(36) IRQ_STUB_DECL(37) IRQ_STUB_DECL(38) IRQ_STUB_DECL(39)
IRQ_STUB_DECL(40) IRQ_STUB_DECL(41) IRQ_STUB_DECL(42) IRQ_STUB_DECL(43)
IRQ_STUB_DECL(44) IRQ_STUB_DECL(45) IRQ_STUB_DECL(46) IRQ_STUB_DECL(47)

IRQ_STUB_DECL(1)  IRQ_STUB_DECL(2)  IRQ_STUB_DECL(3)  IRQ_STUB_DECL(4)
IRQ_STUB_DECL(5)  IRQ_STUB_DECL(7)  IRQ_STUB_DECL(9)  IRQ_STUB_DECL(10)
IRQ_STUB_DECL(11) IRQ_STUB_DECL(12) IRQ_STUB_DECL(15) IRQ_STUB_DECL(16)
IRQ_STUB_DECL(17) IRQ_STUB_DECL(18) IRQ_STUB_DECL(19) IRQ_STUB_DECL(20)
IRQ_STUB_DECL(21) IRQ_STUB_DECL(22) IRQ_STUB_DECL(23) IRQ_STUB_DECL(24)
IRQ_STUB_DECL(25) IRQ_STUB_DECL(26) IRQ_STUB_DECL(27) IRQ_STUB_DECL(28)
IRQ_STUB_DECL(29) IRQ_STUB_DECL(30) IRQ_STUB_DECL(31)

typedef void (*irq_handler_t)(void);
static irq_handler_t irq_handlers[IDT_ENTRIES] = {0};

enum { R15=0, R14, R13, R12, R11, R10, R9, R8, RBP, RDI, RSI, RDX, RCX, RBX, RAX };

void exception_fatal(uint64_t exception_num, uint64_t error_code, uint64_t rip, uint64_t *regs);
extern void syscall_dispatch(uint64_t *regs);

void irq_register(int num, irq_handler_t handler) {
    if (num >= 0 && num < IDT_ENTRIES) {
        irq_handlers[num] = handler;
        dmesg("[idt] handler registered\n");
    }
}

void irq_dispatcher(uint64_t irq_num, uint64_t error_code, uint64_t rip, uint64_t *regs) {

    if (irq_num == 0x80) {
        syscall_dispatch(regs);
        return;
    }

    if (irq_num < 32) {
        if (irq_num == 14) {
            uint64_t cr2;
            asm volatile("movq %%cr2, %0" : "=r"(cr2));
            if (vmm_sync_kernel_entry(cr2)) return;
        }
        exception_fatal(irq_num, error_code, rip, regs);
        return;
    }

    if (irq_num < IDT_ENTRIES && irq_handlers[irq_num] != NULL) {
        irq_handlers[irq_num]();
        return;
    }
}

static const char *exception_name(uint64_t vec) {
    static const char *names[32] = {
        "#DE Divide Error", "#DB Debug", "NMI", "#BP Breakpoint",
        "#OF Overflow", "#BR Bound Range", "#UD Invalid Opcode", "#NM Device N/A",
        "#DF Double Fault", "Coprocessor Seg Overrun", "#TS Invalid TSS", "#NP Segment Not Present",
        "#SS Stack Fault", "#GP General Protection", "#PF Page Fault", "",
        "#MF x87 FP Error", "#AC Alignment Check", "#MC Machine Check", "#XF SIMD FP Error",
        "", "#CP Control Protection", "", "",
        "", "", "", "",
        "", "#VE Virtualization", "#SX Security", ""
    };
    return (vec < 32 && names[vec][0]) ? names[vec] : "Unknown";
}

static void reg_line(const char *name, uint64_t val) {
    terminal_print("  "); terminal_print(name); terminal_print_hex(val); terminal_println("");
}

void exception_fatal(uint64_t exception_num, uint64_t error_code, uint64_t rip, uint64_t *regs) {
    uint64_t cr2;
    asm volatile("movq %%cr2, %0" : "=r"(cr2));

    uint64_t cs_val     = regs[18];
    uint64_t rflags_val = regs[19];
    uint64_t cpl        = cs_val & 3;

    terminal_set_fg(0xFF0000);
    terminal_println("\n=== FATAL EXCEPTION ===");
    terminal_print("vector "); terminal_print_int((uint32_t)exception_num);
    terminal_print(" - "); terminal_println(exception_name(exception_num));
    terminal_print("error code: "); terminal_print_hex(error_code); terminal_println("");
    terminal_print("RIP:        "); terminal_print_hex(rip); terminal_println("");
    terminal_print("CS:         "); terminal_print_hex(cs_val); terminal_println("");
    terminal_print("RFLAGS:     "); terminal_print_hex(rflags_val); terminal_println("");
    if (exception_num == 14) { terminal_print("CR2 (fault addr): "); terminal_print_hex(cr2); terminal_println(""); }

    terminal_println("\n-- registers --");
    reg_line("RAX: ", regs[RAX]); reg_line("RBX: ", regs[RBX]);
    reg_line("RCX: ", regs[RCX]); reg_line("RDX: ", regs[RDX]);
    reg_line("RSI: ", regs[RSI]); reg_line("RDI: ", regs[RDI]);
    reg_line("RBP: ", regs[RBP]);
    reg_line("R8:  ", regs[R8]);  reg_line("R9:  ", regs[R9]);
    reg_line("R10: ", regs[R10]); reg_line("R11: ", regs[R11]);
    reg_line("R12: ", regs[R12]); reg_line("R13: ", regs[R13]);
    reg_line("R14: ", regs[R14]); reg_line("R15: ", regs[R15]);

    dmesg("[idt] exception vector=");
    dmesg_int((uint32_t)exception_num);
    dmesg(" (");
    dmesg(exception_name(exception_num));
    dmesg(") err=");
    dmesg_hex(error_code);
    dmesg(" rip=");
    dmesg_hex(rip);
    dmesg(" rax=");
    dmesg_hex(regs[RAX]);
    dmesg(" cpl=");
    dmesg_int((uint32_t)cpl);
    dmesg("\n");

    if (cpl == 3) {

        task_t *cur = sched_current();
        if (exception_num == 14 && cur && cur->user_stack_guard_va &&
            (cr2 & ~0xFFFULL) == cur->user_stack_guard_va) {
            terminal_set_fg(0xFFAA00);
            terminal_println("\nStack overflow (guard page hit) -- killing task, kernel continues.");
            dmesg("[idt] stack guard page hit, killing task\n");
        } else {
            terminal_set_fg(0xFFAA00);
            terminal_println("\nUser-mode task faulted -- killing task, kernel continues.");
            dmesg("[idt] user-mode fault, killing task\n");
        }

        extern void task_exit(void);
        task_exit();

        while (1) asm volatile("hlt");
    }

    terminal_set_fg(0xFF0000);
    terminal_println("\nSystem halted");
    dmesg("[idt] kernel-mode fault, halting system\n");

    asm volatile("cli; 1: hlt; jmp 1b");
}

void idt_set_ist(int num, uint8_t ist) {
    if (num >= 0 && num < IDT_ENTRIES) {
        idt[num].always0 = ist & 0x07;
    }
}

void idt_set_gate(int num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_mid  = (base >> 16) & 0xFFFF;
    idt[num].base_high = (base >> 32) & 0xFFFFFFFF;

    idt[num].sel     = sel;
    idt[num].always0 = 0;
    idt[num].flags   = flags;
    idt[num].reserved = 0;
}

__attribute__((naked)) static void isr_common(void) {
    asm volatile(
        "pushq %%rax; pushq %%rbx; pushq %%rcx; pushq %%rdx;"
        "pushq %%rsi; pushq %%rdi; pushq %%rbp;"
        "pushq %%r8;  pushq %%r9;  pushq %%r10; pushq %%r11;"
        "pushq %%r12; pushq %%r13; pushq %%r14; pushq %%r15;"

        "cld;"

        "movq 120(%%rsp), %%rdi;"
        "movq 128(%%rsp), %%rsi;"
        "movq 136(%%rsp), %%rdx;"
        "movq %%rsp, %%rcx;"

        /* The dispatcher/handlers are C compiled with -msse2 and may clobber
         * the interrupted task's SSE registers. Save XMM0-15 + MXCSR on the
         * ISR stack and restore them right before iretq. */
        "sub $288, %%rsp;"
        "stmxcsr 16(%%rsp);"
        "movdqu %%xmm0,  32(%%rsp); movdqu %%xmm1,  48(%%rsp);"
        "movdqu %%xmm2,  64(%%rsp); movdqu %%xmm3,  80(%%rsp);"
        "movdqu %%xmm4,  96(%%rsp); movdqu %%xmm5, 112(%%rsp);"
        "movdqu %%xmm6, 128(%%rsp); movdqu %%xmm7, 144(%%rsp);"
        "movdqu %%xmm8, 160(%%rsp); movdqu %%xmm9, 176(%%rsp);"
        "movdqu %%xmm10,192(%%rsp); movdqu %%xmm11,208(%%rsp);"
        "movdqu %%xmm12,224(%%rsp); movdqu %%xmm13,240(%%rsp);"
        "movdqu %%xmm14,256(%%rsp); movdqu %%xmm15,272(%%rsp);"

        "call irq_dispatcher;"

        "movdqu 32(%%rsp), %%xmm0;  movdqu 48(%%rsp), %%xmm1;"
        "movdqu 64(%%rsp), %%xmm2;  movdqu 80(%%rsp), %%xmm3;"
        "movdqu 96(%%rsp), %%xmm4;  movdqu 112(%%rsp), %%xmm5;"
        "movdqu 128(%%rsp),%%xmm6;  movdqu 144(%%rsp),%%xmm7;"
        "movdqu 160(%%rsp),%%xmm8;  movdqu 176(%%rsp),%%xmm9;"
        "movdqu 192(%%rsp),%%xmm10; movdqu 208(%%rsp),%%xmm11;"
        "movdqu 224(%%rsp),%%xmm12; movdqu 240(%%rsp),%%xmm13;"
        "movdqu 256(%%rsp),%%xmm14; movdqu 272(%%rsp),%%xmm15;"
        "ldmxcsr 16(%%rsp);"
        "add $288, %%rsp;"

        "popq %%r15; popq %%r14; popq %%r13; popq %%r12;"
        "popq %%r11; popq %%r10; popq %%r9;  popq %%r8;"
        "popq %%rbp; popq %%rdi; popq %%rsi;"
        "popq %%rdx; popq %%rcx; popq %%rbx; popq %%rax;"

        "addq $16, %%rsp;"
        "iretq"
        ::: "memory"
    );
}

__attribute__((naked)) static void isr0(void)  { asm volatile("pushq $0; pushq $0;  jmp isr_common"); }
__attribute__((naked)) static void isr6(void)  { asm volatile("pushq $0; pushq $6;  jmp isr_common"); }
__attribute__((naked)) static void isr8(void)  { asm volatile("pushq $8;  jmp isr_common"); }
__attribute__((naked)) static void isr13(void) { asm volatile("pushq $13; jmp isr_common"); }
__attribute__((naked)) static void isr14(void) { asm volatile("pushq $14; jmp isr_common"); }

#define IRQ_STUB(vec) \
    __attribute__((naked)) static void isr##vec(void) { \
        asm volatile("pushq $0; pushq $" #vec "; jmp isr_common"); \
    }

IRQ_STUB(32) IRQ_STUB(33) IRQ_STUB(34) IRQ_STUB(35)
IRQ_STUB(36) IRQ_STUB(37) IRQ_STUB(38) IRQ_STUB(39)
IRQ_STUB(40) IRQ_STUB(41) IRQ_STUB(42) IRQ_STUB(43)
IRQ_STUB(44) IRQ_STUB(45) IRQ_STUB(46) IRQ_STUB(47)

#define EXC_STUB_NOERR(vec) \
    __attribute__((naked)) static void isr##vec(void) { \
        asm volatile("pushq $0; pushq $" #vec "; jmp isr_common"); \
    }

#define EXC_STUB_ERR(vec) \
    __attribute__((naked)) static void isr##vec(void) { \
        asm volatile("pushq $" #vec "; jmp isr_common"); \
    }

EXC_STUB_NOERR(1)  EXC_STUB_NOERR(2)  EXC_STUB_NOERR(3)  EXC_STUB_NOERR(4)
EXC_STUB_NOERR(5)  EXC_STUB_NOERR(7)  EXC_STUB_NOERR(9)
EXC_STUB_NOERR(15) EXC_STUB_NOERR(16) EXC_STUB_NOERR(18) EXC_STUB_NOERR(19)
EXC_STUB_NOERR(20) EXC_STUB_NOERR(22) EXC_STUB_NOERR(23) EXC_STUB_NOERR(24)
EXC_STUB_NOERR(25) EXC_STUB_NOERR(26) EXC_STUB_NOERR(27) EXC_STUB_NOERR(28)
EXC_STUB_NOERR(31)

EXC_STUB_ERR(10) EXC_STUB_ERR(11) EXC_STUB_ERR(12)
EXC_STUB_ERR(17) EXC_STUB_ERR(21) EXC_STUB_ERR(29) EXC_STUB_ERR(30)

__attribute__((naked)) static void isr128(void) {
    asm volatile("pushq $0; pushq $0x80; jmp isr_common");
}
uint64_t isr128_addr(void) { return (uint64_t)isr128; }

void idt_init(void) {
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, (uint64_t)isr_common, 0x08, 0x8E);
    }

    idt_set_gate(0,  (uint64_t)isr0,  0x08, 0x8E);
    idt_set_gate(6,  (uint64_t)isr6,  0x08, 0x8E);
    idt_set_gate(8,  (uint64_t)isr8,  0x08, 0x8E);
    idt_set_gate(13, (uint64_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint64_t)isr14, 0x08, 0x8E);

    #define EXC_GATE(vec) idt_set_gate(vec, (uint64_t)isr##vec, 0x08, 0x8E);
    EXC_GATE(1)  EXC_GATE(2)  EXC_GATE(3)  EXC_GATE(4)
    EXC_GATE(5)  EXC_GATE(7)  EXC_GATE(9)  EXC_GATE(10)
    EXC_GATE(11) EXC_GATE(12) EXC_GATE(15) EXC_GATE(16)
    EXC_GATE(17) EXC_GATE(18) EXC_GATE(19) EXC_GATE(20)
    EXC_GATE(21) EXC_GATE(22) EXC_GATE(23) EXC_GATE(24)
    EXC_GATE(25) EXC_GATE(26) EXC_GATE(27) EXC_GATE(28)
    EXC_GATE(29) EXC_GATE(30) EXC_GATE(31)
    #undef EXC_GATE

    #define IRQ_GATE(vec) idt_set_gate(vec, (uint64_t)isr##vec, 0x08, 0x8E);
    IRQ_GATE(32) IRQ_GATE(33) IRQ_GATE(34) IRQ_GATE(35)
    IRQ_GATE(36) IRQ_GATE(37) IRQ_GATE(38) IRQ_GATE(39)
    IRQ_GATE(40) IRQ_GATE(41) IRQ_GATE(42) IRQ_GATE(43)
    IRQ_GATE(44) IRQ_GATE(45) IRQ_GATE(46) IRQ_GATE(47)
    #undef IRQ_GATE

    idt_set_ist(8, 1);

    idt_ptr.limit = sizeof(idt_entry_t) * IDT_ENTRIES - 1;
    idt_ptr.base  = (uint64_t)&idt;

    asm volatile("lidt %0" : : "m"(idt_ptr));

    dmesg("[idt] initialized with dispatcher\n");
}