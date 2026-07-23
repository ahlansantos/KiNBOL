#ifndef LAPIC_H
#define LAPIC_H

#include <stdint.h>

void lapic_init(void);

void lapic_eoi(void);

void lapic_timer_init(uint32_t hz, uint8_t vector);

uint32_t lapic_get_id(void);

#endif