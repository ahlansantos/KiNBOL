/*
 * Driver for the I/O APIC. Reads and writes the redirection table
 * (IOREDTBL) to decide, for every external IRQ pin (keyboard, PIT, etc),
 * which vector it should fire, which LAPIC it should be delivered to,
 * and its polarity/trigger mode. This replaces the old PIC remapping.
 */
#include "ioapic.h"
#include "acpi.h"
#include "dmesg.h"
#include "../mm/vmm.h"
#include <stddef.h>

extern uint64_t hhdm_offset;

#define IOREGSEL 0x00
#define IOWIN    0x10

#define IOAPICID  0x00
#define IOAPICVER 0x01
#define IOREDTBL  0x10 /* + 2*pin gives low dword, +1 gives high dword */

static inline volatile uint32_t *regsel(acpi_ioapic_t *io) {
    return (volatile uint32_t *)(hhdm_offset + io->phys_addr + IOREGSEL);
}
static inline volatile uint32_t *win(acpi_ioapic_t *io) {
    return (volatile uint32_t *)(hhdm_offset + io->phys_addr + IOWIN);
}

static uint32_t ioapic_read(acpi_ioapic_t *io, uint8_t reg) {
    *regsel(io) = reg;
    return *win(io);
}
static void ioapic_write(acpi_ioapic_t *io, uint8_t reg, uint32_t val) {
    *regsel(io) = reg;
    *win(io) = val;
}

static void set_redirection(acpi_ioapic_t *io, uint32_t pin, uint8_t vector,
                             uint32_t dest_lapic_id, bool active_low,
                             bool level_triggered, bool masked) {
    uint32_t low = vector;                 /* delivery mode fixed=0, dest mode physical=0 */
    if (active_low)      low |= (1 << 13);
    if (level_triggered) low |= (1 << 15);
    if (masked)           low |= (1 << 16);

    uint32_t high = dest_lapic_id << 24;

    ioapic_write(io, IOREDTBL + 2 * pin + 1, high); /* write high dword first */
    ioapic_write(io, IOREDTBL + 2 * pin,     low);
}

void ioapic_init(void) {
    if (!acpi_info.valid) {
        dmesg("[ioapic] no ACPI/MADT info, cannot init\n");
        return;
    }

    /* Mask every pin on every IOAPIC to start from a known-clean state;
     * individual drivers unmask what they need, same convention as the
     * old pic_remap(). */
    for (int i = 0; i < acpi_info.ioapic_count; i++) {
        acpi_ioapic_t *io = &acpi_info.ioapics[i];

        /* Same story as the LAPIC: this MMIO page is not mapped by
         * Limine's own tables, and it's a different physical page per
         * IOAPIC, so each one needs its own vmm_map() call. IOREGSEL
         * and IOWIN both sit within the first 0x20 bytes, one page
         * covers everything. */
        vmm_map(vmm_current(),
                hhdm_offset + (io->phys_addr & ~0xFFFULL),
                io->phys_addr & ~0xFFFULL,
                VMM_FLAGS_KERNEL);

        uint32_t ver_reg = ioapic_read(io, IOAPICVER);
        uint32_t max_entries = ((ver_reg >> 16) & 0xFF) + 1;

        for (uint32_t pin = 0; pin < max_entries; pin++) {
            set_redirection(io, pin, 0, 0, false, false, true);
        }
    }

    dmesg("[ioapic] all IOAPICs initialized, all pins masked\n");
}

bool ioapic_route_isa_irq(uint8_t isa_irq, uint8_t vector, uint32_t dest_lapic_id, bool masked) {
    uint16_t flags;
    uint32_t gsi = acpi_isa_irq_to_gsi(isa_irq, &flags);

    acpi_ioapic_t *io;
    uint32_t pin;
    if (!acpi_gsi_to_ioapic(gsi, &io, &pin)) {
        dmesg("[ioapic] ISA IRQ has no owning IOAPIC\n");
        return false;
    }

    uint16_t polarity = flags & 0x3;
    uint16_t trigger  = (flags >> 2) & 0x3;
    /* 00 = "conforms to bus spec", which for ISA means active-high/edge */
    bool active_low      = (polarity == 0x3);
    bool level_triggered = (trigger  == 0x3);

    set_redirection(io, pin, vector, dest_lapic_id, active_low, level_triggered, masked);
    dmesg("[ioapic] ISA IRQ routed\n");
    return true;
}

void ioapic_mask_gsi(uint32_t gsi) {
    acpi_ioapic_t *io; uint32_t pin;
    if (!acpi_gsi_to_ioapic(gsi, &io, &pin)) return;
    uint32_t low = ioapic_read(io, IOREDTBL + 2 * pin);
    low |= (1 << 16);
    ioapic_write(io, IOREDTBL + 2 * pin, low);
}

void ioapic_unmask_gsi(uint32_t gsi) {
    acpi_ioapic_t *io; uint32_t pin;
    if (!acpi_gsi_to_ioapic(gsi, &io, &pin)) return;
    uint32_t low = ioapic_read(io, IOREDTBL + 2 * pin);
    low &= ~(1u << 16);
    ioapic_write(io, IOREDTBL + 2 * pin, low);
}