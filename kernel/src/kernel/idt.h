#ifndef IDT_H
#define IDT_H

#include <stdint.h>

void idt_init(void);
void idt_set_gate(int num, uint64_t base, uint16_t sel, uint8_t flags);
void idt_set_ist(int num, uint8_t ist);

typedef void (*irq_handler_t)(void);
void irq_register(int num, irq_handler_t handler);

#endif