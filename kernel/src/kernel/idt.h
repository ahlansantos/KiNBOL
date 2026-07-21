<<<<<<< HEAD
/*
 * Header for the IDT (Interrupt Descriptor Table): idt_init, idt_set_gate
 * (points a vector at an assembly stub), and irq_register, used by
 * drivers to hook in their own high-level handlers.
 */
=======
>>>>>>> origin/x86_64-uefi
#ifndef IDT_H
#define IDT_H

#include <stdint.h>

void idt_init(void);
void idt_set_gate(int num, uint64_t base, uint16_t sel, uint8_t flags);
void idt_set_ist(int num, uint8_t ist);

typedef void (*irq_handler_t)(void);
void irq_register(int num, irq_handler_t handler);

#endif