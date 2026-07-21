/*
 * Header for the GDT: gdt_init and tss_set_ist, which sets up the
 * alternate IST stack used for critical interrupts like the double
 * fault.
 */
#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define GDT_SEL_KCODE 0x08
#define GDT_SEL_KDATA 0x10
#define GDT_SEL_UCODE 0x18
#define GDT_SEL_UDATA 0x20
#define GDT_SEL_TSS   0x28

void gdt_init(void);

void tss_set_ist(int ist_index, uint64_t stack_top);

#endif