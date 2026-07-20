#include "acpi.h"
#include "dmesg.h"
#include <stddef.h>

/* Physical memory is reachable at hhdm_offset + phys once vmm_init() has
 * run (Limine's HHDM maps all usable + reserved RAM, which covers ACPI
 * tables since they live in memory the firmware marked reserved/ACPI). */
extern uint64_t hhdm_offset;
#define PHYS(addr) ((void *)(hhdm_offset + (uint64_t)(addr)))

acpi_info_t acpi_info = {0};

typedef struct {
    char     signature[8];
    uint8_t  checksum;
    char     oemid[6];
    uint8_t  revision;
    uint32_t rsdt_address;
    /* ACPI 2.0+ only - only valid if revision >= 2 */
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} __attribute__((packed)) rsdp_t;

typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oemid[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) sdt_header_t;

typedef struct {
    sdt_header_t hdr;
    uint32_t     lapic_address;
    uint32_t     flags;
    uint8_t      entries[];
} __attribute__((packed)) madt_t;

typedef struct { uint8_t type; uint8_t length; } __attribute__((packed)) madt_entry_hdr_t;

typedef struct {
    madt_entry_hdr_t hdr;
    uint8_t  acpi_processor_id;
    uint8_t  apic_id;
    uint32_t flags; /* bit0 = enabled */
} __attribute__((packed)) madt_lapic_t;

typedef struct {
    madt_entry_hdr_t hdr;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
} __attribute__((packed)) madt_ioapic_t;

typedef struct {
    madt_entry_hdr_t hdr;
    uint8_t  bus;
    uint8_t  source;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed)) madt_iso_t;

typedef struct {
    madt_entry_hdr_t hdr;
    uint16_t reserved;
    uint64_t lapic_address;
} __attribute__((packed)) madt_lapic_override_t;

static bool checksum_ok(const void *table, uint32_t len) {
    uint8_t sum = 0;
    const uint8_t *p = table;
    for (uint32_t i = 0; i < len; i++) sum = (uint8_t)(sum + p[i]);
    return sum == 0;
}

static sdt_header_t *find_table(sdt_header_t *root, bool is_xsdt, const char sig[4]) {
    uint32_t entry_count;
    void *entries_ptr;

    if (is_xsdt) {
        entry_count = (root->length - sizeof(sdt_header_t)) / 8;
        entries_ptr = (uint8_t *)root + sizeof(sdt_header_t);
    } else {
        entry_count = (root->length - sizeof(sdt_header_t)) / 4;
        entries_ptr = (uint8_t *)root + sizeof(sdt_header_t);
    }

    for (uint32_t i = 0; i < entry_count; i++) {
        uint64_t phys;
        if (is_xsdt) phys = ((uint64_t *)entries_ptr)[i];
        else         phys = ((uint32_t *)entries_ptr)[i];

        sdt_header_t *table = (sdt_header_t *)PHYS(phys);
        if (table->signature[0] == sig[0] && table->signature[1] == sig[1] &&
            table->signature[2] == sig[2] && table->signature[3] == sig[3]) {
            return table;
        }
    }
    return NULL;
}

static void parse_madt(madt_t *madt) {
    acpi_info.lapic_phys_addr    = madt->lapic_address;
    acpi_info.legacy_pic_present = (madt->flags & 1) != 0;

    uint8_t *p   = madt->entries;
    uint8_t *end = (uint8_t *)madt + madt->hdr.length;

    while (p < end) {
        madt_entry_hdr_t *eh = (madt_entry_hdr_t *)p;
        if (eh->length == 0) break; /* malformed table guard */

        switch (eh->type) {
            case 0: { /* Processor Local APIC */
                madt_lapic_t *e = (madt_lapic_t *)p;
                if ((e->flags & 1) && acpi_info.bsp_lapic_id == 0) {
                    /* First enabled CPU we see; good enough as BSP id
                     * pre-SMP. (Real SMP bring-up should instead read
                     * the LAPIC ID register directly on the BSP.) */
                    acpi_info.bsp_lapic_id = e->apic_id;
                }
                break;
            }
            case 1: { /* I/O APIC */
                madt_ioapic_t *e = (madt_ioapic_t *)p;
                if (acpi_info.ioapic_count < ACPI_MAX_IOAPICS) {
                    acpi_ioapic_t *io = &acpi_info.ioapics[acpi_info.ioapic_count++];
                    io->id        = e->ioapic_id;
                    io->phys_addr = e->ioapic_addr;
                    io->gsi_base  = e->gsi_base;
                    dmesg("[acpi] IOAPIC found\n");
                }
                break;
            }
            case 2: { /* Interrupt Source Override */
                madt_iso_t *e = (madt_iso_t *)p;
                if (acpi_info.iso_count < ACPI_MAX_ISOS) {
                    acpi_iso_t *iso = &acpi_info.isos[acpi_info.iso_count++];
                    iso->bus_irq = e->source;
                    iso->gsi     = e->gsi;
                    iso->flags   = e->flags;
                    dmesg("[acpi] ISO found\n");
                }
                break;
            }
            case 5: { /* Local APIC Address Override (64-bit) */
                madt_lapic_override_t *e = (madt_lapic_override_t *)p;
                acpi_info.lapic_phys_addr = e->lapic_address;
                break;
            }
            default:
                break;
        }

        p += eh->length;
    }
}

void acpi_init(void *rsdp_ptr) {
    if (!rsdp_ptr) {
        dmesg("[acpi] no RSDP from bootloader - cannot use APIC\n");
        return;
    }

    rsdp_t *rsdp = (rsdp_t *)rsdp_ptr;

    if (rsdp->signature[0] != 'R' || rsdp->signature[1] != 'S' ||
        rsdp->signature[2] != 'D' || rsdp->signature[3] != ' ') {
        dmesg("[acpi] bad RSDP signature\n");
        return;
    }

    sdt_header_t *root;
    bool is_xsdt = false;

    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        root = (sdt_header_t *)PHYS(rsdp->xsdt_address);
        is_xsdt = true;
    } else {
        root = (sdt_header_t *)PHYS(rsdp->rsdt_address);
    }

    if (!checksum_ok(root, root->length)) {
        dmesg("[acpi] root SDT checksum failed\n");
        return;
    }

    sdt_header_t *madt_hdr = find_table(root, is_xsdt, "APIC");
    if (!madt_hdr) {
        dmesg("[acpi] no MADT present\n");
        return;
    }
    if (!checksum_ok(madt_hdr, madt_hdr->length)) {
        dmesg("[acpi] MADT checksum failed\n");
        return;
    }

    parse_madt((madt_t *)madt_hdr);

    if (acpi_info.ioapic_count == 0 || acpi_info.lapic_phys_addr == 0) {
        dmesg("[acpi] MADT present but no usable LAPIC/IOAPIC entries\n");
        return;
    }

    acpi_info.valid = true;
    dmesg("[acpi] MADT parsed OK\n");
}

uint32_t acpi_isa_irq_to_gsi(uint8_t isa_irq, uint16_t *out_flags) {
    for (int i = 0; i < acpi_info.iso_count; i++) {
        if (acpi_info.isos[i].bus_irq == isa_irq) {
            if (out_flags) *out_flags = acpi_info.isos[i].flags;
            return acpi_info.isos[i].gsi;
        }
    }
    if (out_flags) *out_flags = 0; /* conforms to bus (active-high, edge) */
    return isa_irq; /* identity mapped, the common case */
}

bool acpi_gsi_to_ioapic(uint32_t gsi, acpi_ioapic_t **out_ioapic, uint32_t *out_pin) {
    for (int i = 0; i < acpi_info.ioapic_count; i++) {
        acpi_ioapic_t *io = &acpi_info.ioapics[i];
        /* Each IOAPIC covers a contiguous GSI range starting at gsi_base;
         * without a redirection-table-size field handy we assume 24
         * inputs (the near-universal case) unless another IOAPIC's base
         * says otherwise. Good enough for single-IOAPIC desktop/laptop
         * boards and QEMU; a multi-IOAPIC server board would need the
         * MAX REDIRECTION ENTRY register read to be fully correct. */
        uint32_t span = 24;
        for (int j = 0; j < acpi_info.ioapic_count; j++) {
            if (j != i && acpi_info.ioapics[j].gsi_base > io->gsi_base) {
                uint32_t d = acpi_info.ioapics[j].gsi_base - io->gsi_base;
                if (d < span) span = d;
            }
        }
        if (gsi >= io->gsi_base && gsi < io->gsi_base + span) {
            *out_ioapic = io;
            *out_pin    = gsi - io->gsi_base;
            return true;
        }
    }
    return false;
}