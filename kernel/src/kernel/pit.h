#pragma once
#include <stdint.h>

extern uint64_t tsc_hz;
extern uint64_t boot_tsc;

/* Pure port-polling calibration against PIT channel 0 - no interrupts
 * involved, so this works before IDT/LAPIC/IOAPIC exist and can't be
 * broken by interrupt-routing issues. */
void     tsc_calibrate(void);
uint64_t uptime_ms(void);
void     sleep_ms(uint32_t ms);

/* Generic system tick counter. Whatever timer interrupt is actually
 * armed (LAPIC timer, see lapic_timer_init()) should call pit_tick()
 * from its ISR; pit_get_ticks() just reports the count. Named "pit_"
 * for shell/sys.c compatibility - it's no longer PIT-sourced. */
void     pit_tick(void);
uint64_t pit_get_ticks(void);