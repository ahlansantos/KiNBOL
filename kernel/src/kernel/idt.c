#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "idt.h"
#include "../graphics/terminal.h"
#include "dmesg.h"

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

typedef void (*irq_handler_t)(void);
static irq_handler_t irq_handlers[IDT_ENTRIES] = {0};

enum { R15=0, R14, R13, R12, R11, R10, R9, R8, RBP, RDI, RSI, RDX, RCX, RBX, RAX };

void exception_fatal(uint64_t exception_num, uint64_t error_code, uint64_t rip, uint64_t *regs);

void irq_register(int num, irq_handler_t handler) {
    if (num >= 0 && num < IDT_ENTRIES) {
        irq_handlers[num] = handler;
        dmesg("[idt] handler registered\n");
    }
}

void irq_dispatcher(uint64_t irq_num, uint64_t error_code, uint64_t rip, uint64_t *regs) {
    if (irq_num < 32) {
        exception_fatal(irq_num, error_code, rip, regs);
        return;
    }

    if (irq_handlers[irq_num] != NULL) {
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

    terminal_set_fg(0xFF0000);
    terminal_println("\n=== FATAL EXCEPTION ===");
    terminal_print("vector "); terminal_print_int((uint32_t)exception_num);
    terminal_print(" - "); terminal_println(exception_name(exception_num));
    terminal_print("error code: "); terminal_print_hex(error_code); terminal_println("");
    terminal_print("RIP:        "); terminal_print_hex(rip); terminal_println("");
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

    terminal_println("\nSystem halted");

    dmesg("[idt] fatal exception vector=");
    dmesg_int((uint32_t)exception_num);
    dmesg(" (");
    dmesg(exception_name(exception_num));
    dmesg(") err=");
    dmesg_hex(error_code);
    dmesg(" rip=");
    dmesg_hex(rip);
    dmesg(" rax=");
    dmesg_hex(regs[RAX]);
    dmesg("\n");

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
        "call irq_dispatcher;"
        
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

void idt_init(void) {
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, (uint64_t)isr_common, 0x08, 0x8E);
    }

    idt_set_gate(0,  (uint64_t)isr0,  0x08, 0x8E);
    idt_set_gate(6,  (uint64_t)isr6,  0x08, 0x8E);
    idt_set_gate(8,  (uint64_t)isr8,  0x08, 0x8E);
    idt_set_gate(13, (uint64_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint64_t)isr14, 0x08, 0x8E);

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