/*
 * Header for the legacy PIC driver: remap and disable.
 */
#ifndef PIC_H
#define PIC_H

#include <stdint.h>
#include <stdbool.h>

/* Legacy 8259 PIC is no longer used for interrupt delivery (LAPIC/IOAPIC
 * replace it - see lapic.c/ioapic.c). This still remaps it to 0x20-0x2F
 * first (so a spurious/stray legacy IRQ can never collide with a CPU
 * exception vector) and then fully masks + disables it, optionally
 * flipping the IMCR to APIC-routing mode if the MADT says a dual-8259
 * setup is actually present on this board. Call once, before lapic_init()
 * / ioapic_init(). */
void pic_disable(bool imcr_switch_to_apic);

#endif