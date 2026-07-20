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

void exception_fatal(uint64_t exception_num, uint64_t error_code, uint64_t rip);

void irq_register(int num, irq_handler_t handler) {
    if (num >= 0 && num < IDT_ENTRIES) {
        irq_handlers[num] = handler;
        dmesg("[idt] handler registered\n");
    }
}

void irq_dispatcher(uint64_t irq_num, uint64_t error_code, uint64_t rip) {
    /* Vectors 0-31 are CPU exceptions, not maskable IRQs. If one of these
     * fires and nothing is registered for it, falling through and doing
     * nothing means we iretq straight back into the SAME faulting
     * instruction (faults don't advance RIP) -> infinite silent re-fault
     * loop, CPU pegged forever, no message, everything else (PIC/PIT
     * ticks included) stops dead. Route them to the real panic handler
     * instead. */
    if (irq_num < 32) {
        exception_fatal(irq_num, error_code, rip);
        return;
    }

    if (irq_handlers[irq_num] != NULL) {
        irq_handlers[irq_num]();
        return;
    }
}

void exception_fatal(uint64_t exception_num, uint64_t error_code, uint64_t rip) {
    uint64_t cr2;
    asm volatile("movq %%cr2, %0" : "=r"(cr2));

    terminal_set_fg(0xFF0000);
    terminal_println("\n=== FATAL EXCEPTION ===");
    terminal_print("vector: "); terminal_print_int((uint32_t)exception_num); terminal_println("");
    terminal_print("error code: "); terminal_print_hex(error_code); terminal_println("");
    terminal_print("RIP: "); terminal_print_hex(rip); terminal_println("");
    if (exception_num == 14) { terminal_print("CR2 (fault addr): "); terminal_print_hex(cr2); terminal_println(""); }
    terminal_println("System halted");

    dmesg("[idt] fatal exception vector=");
    dmesg_int((uint32_t)exception_num);
    dmesg(" err=");
    dmesg_hex(error_code);
    dmesg(" rip=");
    dmesg_hex(rip);
    dmesg("\n");

    asm volatile("cli; 1: hlt; jmp 1b");
}

/* NOTE: in a real 64-bit IDT gate, the byte we call "always0" here is
 * actually the IST field (bits 0-2 select which TSS IST stack to use;
 * 0 means "don't switch stacks, keep using whatever RSP already is"). */
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

/* isr0 (#DE) and isr6 (#UD) get NO error code from the CPU, so we push a
 * dummy $0 ourselves to keep the stack layout uniform for isr_common.
 * isr8 (#DF), isr13 (#GP), isr14 (#PF) DO get a real error code pushed
 * by the CPU automatically -- pushing another dummy $0 on top of that
 * shifts every later field on the stack by one slot, so isr_common ends
 * up reading the real error code where it expects RIP (and the dummy 0
 * where it expects the error code). Only push the vector for these. */
__attribute__((naked)) static void isr0(void)  { asm volatile("pushq $0; pushq $0;  jmp isr_common"); }
__attribute__((naked)) static void isr6(void)  { asm volatile("pushq $0; pushq $6;  jmp isr_common"); }
__attribute__((naked)) static void isr8(void)  { asm volatile("pushq $8;  jmp isr_common"); }
__attribute__((naked)) static void isr13(void) { asm volatile("pushq $13; jmp isr_common"); }
__attribute__((naked)) static void isr14(void) { asm volatile("pushq $14; jmp isr_common"); }

/* IRQ0-15 land on vectors 32-47 (after the PIC remap). CPU pushes no
 * error code for these, so we push a dummy 0 ourselves to keep the
 * same stack layout isr_common expects. */
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

    /* Double Fault always runs on IST1: if the fault happened because the
     * kernel stack itself is trashed, handling it on the same (broken)
     * stack just turns it into a silent Triple Fault / reset instead of
     * a message on screen. Requires gdt_init() to have run already. */
    idt_set_ist(8, 1);

    idt_ptr.limit = sizeof(idt_entry_t) * IDT_ENTRIES - 1;
    idt_ptr.base  = (uint64_t)&idt;

    asm volatile("lidt %0" : : "m"(idt_ptr));
    
    dmesg("[idt] initialized with dispatcher\n");
}