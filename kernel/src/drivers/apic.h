#pragma once
#include <stdint.h>

void apic_init(void);
void apic_send_eoi(void);
void apic_timer_init(uint32_t hz);
uint32_t apic_read(uint32_t reg);
void apic_write(uint32_t reg, uint32_t val);