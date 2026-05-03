#pragma once
#include <stdint.h>

extern uint64_t tsc_hz;
extern uint64_t boot_tsc;

void     tsc_calibrate(void);
uint64_t uptime_ms(void);
void     sleep_ms(uint32_t ms);