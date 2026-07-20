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

/* Interrupt Source Override: legacy ISA IRQ -> different GSI/polarity/trigger */
typedef struct {
    uint8_t  bus_irq;
    uint32_t gsi;
    uint16_t flags; /* MPS INTI flags: bits0-1 polarity, bits2-3 trigger mode */
} acpi_iso_t;

typedef struct {
    bool     valid;
    bool     legacy_pic_present; /* MADT flags bit0 (PCAT_COMPAT) */
    uint64_t lapic_phys_addr;
    uint32_t bsp_lapic_id;

    acpi_ioapic_t ioapics[ACPI_MAX_IOAPICS];
    int           ioapic_count;

    acpi_iso_t    isos[ACPI_MAX_ISOS];
    int           iso_count;
} acpi_info_t;

extern acpi_info_t acpi_info;

/* rsdp_ptr comes straight from Limine's rsdp_request.response->address */
void acpi_init(void *rsdp_ptr);

/* Look up the redirection target (GSI + flags) for a legacy ISA IRQ,
 * honoring any Interrupt Source Override. Returns the IRQ number itself
 * (identity mapped) if no override exists. */
uint32_t acpi_isa_irq_to_gsi(uint8_t isa_irq, uint16_t *out_flags);

/* Which IOAPIC owns a given GSI, and the pin index within it. Returns
 * false if no IOAPIC covers that GSI. */
bool acpi_gsi_to_ioapic(uint32_t gsi, acpi_ioapic_t **out_ioapic, uint32_t *out_pin);

#endif