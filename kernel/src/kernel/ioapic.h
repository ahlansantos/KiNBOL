#ifndef IOAPIC_H
#define IOAPIC_H

#include <stdint.h>
#include <stdbool.h>

void ioapic_init(void);

bool ioapic_route_isa_irq(uint8_t isa_irq, uint8_t vector, uint32_t dest_lapic_id, bool masked);

void ioapic_mask_gsi(uint32_t gsi);
void ioapic_unmask_gsi(uint32_t gsi);

#endif