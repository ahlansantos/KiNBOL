#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "idt.h"
#include "graphics/terminal.h"
#include "kernel/dmesg.h"

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

typedef void (*irq_handler_t)(void);
static irq_handler_t irq_handlers[IDT_ENTRIES] = {0};

void irq_register(int num, irq_handler_t handler) {
    if (num >= 0 && num < IDT_ENTRIES) {
        irq_handlers[num] = handler;
        dmesg("[idt] handler registered\n");
    }
}

void irq_dispatcher(uint64_t irq_num, uint64_t error_code) {
    (void)error_code;
    if (irq_handlers[irq_num] != NULL) {
        irq_handlers[irq_num]();
        return;
    }
    
}

void exception_fatal(uint64_t exception_num, uint64_t error_code) {
    (void)exception_num;
    (void)error_code;
    terminal_set_fg(0xFF0000);
    terminal_println("\n=== FATAL EXCEPTION ===");
    terminal_println("System halted");
    
    dmesg("[idt] fatal exception\n");
    
    asm volatile("cli; 1: hlt; jmp 1b");
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
__attribute__((naked)) static void isr8(void)  { asm volatile("pushq $0; pushq $8;  jmp isr_common"); }
__attribute__((naked)) static void isr13(void) { asm volatile("pushq $0; pushq $13; jmp isr_common"); }
__attribute__((naked)) static void isr14(void) { asm volatile("pushq $0; pushq $14; jmp isr_common"); }

void idt_init(void) {
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, (uint64_t)isr_common, 0x08, 0x8E);
    }

    idt_set_gate(0,  (uint64_t)isr0,  0x08, 0x8E);
    idt_set_gate(6,  (uint64_t)isr6,  0x08, 0x8E);
    idt_set_gate(8,  (uint64_t)isr8,  0x08, 0x8E);
    idt_set_gate(13, (uint64_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint64_t)isr14, 0x08, 0x8E);

    idt_ptr.limit = sizeof(idt_entry_t) * IDT_ENTRIES - 1;
    idt_ptr.base  = (uint64_t)&idt;

    asm volatile("lidt %0" : : "m"(idt_ptr));
    
    dmesg("[idt] initialized with dispatcher\n");
}