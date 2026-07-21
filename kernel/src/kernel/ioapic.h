/*
 * Header for the IOAPIC: init (masks every pin), routing an ISA IRQ to the
 * right vector, and masking/unmasking a specific GSI.
 */
#ifndef IOAPIC_H
#define IOAPIC_H

#include <stdint.h>
#include <stdbool.h>

void ioapic_init(void);

/* Routes legacy ISA IRQ `isa_irq` (0-15, e.g. 1 = keyboard) to IDT
 * vector `vector`, delivered to the CPU whose LAPIC id is `dest_lapic_id`.
 * Honors any ACPI Interrupt Source Override (polarity/trigger/GSI remap)
 * automatically. Returns false if no IOAPIC covers that IRQ.
 * `masked = false` to unmask immediately. */
bool ioapic_route_isa_irq(uint8_t isa_irq, uint8_t vector, uint32_t dest_lapic_id, bool masked);

void ioapic_mask_gsi(uint32_t gsi);
void ioapic_unmask_gsi(uint32_t gsi);

#endif