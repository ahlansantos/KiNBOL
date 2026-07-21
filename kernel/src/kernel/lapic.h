<<<<<<< HEAD
/*
 * Header for the LAPIC: init, EOI, get_id, and periodic timer init (frequency
 * plus the interrupt vector to fire).
 */
=======
>>>>>>> origin/x86_64-uefi
#ifndef LAPIC_H
#define LAPIC_H

#include <stdint.h>

/* Enables the LAPIC (MSR + spurious vector), must run after acpi_init(). */
void lapic_init(void);

/* Must be called at the end of every LAPIC-sourced interrupt handler
 * (timer, and anything IOAPIC-routed to this CPU) instead of the old
 * pic_send_eoi(). */
void lapic_eoi(void);

/* Self-calibrates against the already-known TSC frequency (tsc_hz from
 * pit.c) and arms a periodic timer interrupt at hz on IDT vector `vector`.
 * No PIT interrupt involved anywhere in this path. */
void lapic_timer_init(uint32_t hz, uint8_t vector);

uint32_t lapic_get_id(void);

#endif