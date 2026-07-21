<<<<<<< HEAD
/*
 * Header for the ACPI parser: the acpi_info_t struct holding everything
 * pulled out of the MADT (LAPIC address, list of IOAPICs, interrupt
 * source overrides), plus the public functions (init, poweroff, and the
 * ISA IRQ -> GSI -> IOAPIC/pin lookups).
 */
=======
>>>>>>> origin/x86_64-uefi
#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>
#include <stdbool.h>

#define ACPI_MAX_IOAPICS 8
#define ACPI_MAX_ISOS    16

typedef struct {
    uint8_t  id;
    uint32_t phys_addr;
    uint32_t gsi_base;
} acpi_ioapic_t;

typedef struct {
    uint8_t  bus_irq;
    uint32_t gsi;
    uint16_t flags;
} acpi_iso_t;

typedef struct {
    bool     valid;
    bool     legacy_pic_present;
    uint64_t lapic_phys_addr;
    uint32_t bsp_lapic_id;

    acpi_ioapic_t ioapics[ACPI_MAX_IOAPICS];
    int           ioapic_count;

    acpi_iso_t    isos[ACPI_MAX_ISOS];
    int           iso_count;
} acpi_info_t;

extern acpi_info_t acpi_info;

void acpi_init(void *rsdp_ptr);
uint32_t acpi_isa_irq_to_gsi(uint8_t isa_irq, uint16_t *out_flags);
bool acpi_gsi_to_ioapic(uint32_t gsi, acpi_ioapic_t **out_ioapic, uint32_t *out_pin);

void acpi_poweroff(void);

#endif