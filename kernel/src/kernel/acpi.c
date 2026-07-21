/*
 * ACPI table parser. Finds the RSDP handed over by the bootloader, follows
 * it to the RSDT or XSDT, locates the MADT (Multiple APIC Description
 * Table), and pulls out of it the LAPIC address, the list of IOAPICs,
 * and the Interrupt Source Overrides (ISA IRQ -> GSI remapping with
 * custom polarity/trigger). Also knows how to find the FADT and perform
 * an ACPI poweroff (the _S5_ method) by writing directly to the PM1_CNT
 * register.
 */
#include "acpi.h"
#include "dmesg.h"
#include <stddef.h>

extern uint64_t hhdm_offset;
#define PHYS(addr) ((void *)(hhdm_offset + (uint64_t)(addr)))

acpi_info_t acpi_info = {0};

typedef struct {
    char     signature[8];
    uint8_t  checksum;
    char     oemid[6];
    uint8_t  revision;
    uint32_t rsdt_address;
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
    uint32_t flags;
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

typedef struct {
    sdt_header_t hdr;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  _pad0;
    uint8_t  preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
} __attribute__((packed)) fadt_t;

static fadt_t *g_fadt = NULL;

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
        if (eh->length == 0) break;

        switch (eh->type) {
            case 0: {
                madt_lapic_t *e = (madt_lapic_t *)p;
                if ((e->flags & 1) && acpi_info.bsp_lapic_id == 0)
                    acpi_info.bsp_lapic_id = e->apic_id;
                break;
            }
            case 1: {
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
            case 2: {
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
            case 5: {
                madt_lapic_override_t *e = (madt_lapic_override_t *)p;
                acpi_info.lapic_phys_addr = e->lapic_address;
                break;
            }
            default: break;
        }
        p += eh->length;
    }
}

void acpi_init(void *rsdp_ptr) {
    if (!rsdp_ptr) {
        dmesg("[acpi] no RSDP from bootloader\n");
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
        root    = (sdt_header_t *)PHYS(rsdp->xsdt_address);
        is_xsdt = true;
    } else {
        root = (sdt_header_t *)PHYS(rsdp->rsdt_address);
    }

    if (!checksum_ok(root, root->length)) {
        dmesg("[acpi] root SDT checksum failed\n");
        return;
    }

    sdt_header_t *madt_hdr = find_table(root, is_xsdt, "APIC");
    if (!madt_hdr || !checksum_ok(madt_hdr, madt_hdr->length)) {
        dmesg("[acpi] no valid MADT\n");
        return;
    }
    parse_madt((madt_t *)madt_hdr);

    if (acpi_info.ioapic_count == 0 || acpi_info.lapic_phys_addr == 0) {
        dmesg("[acpi] MADT present but no usable LAPIC/IOAPIC\n");
        return;
    }

    sdt_header_t *fadt_hdr = find_table(root, is_xsdt, "FACP");
    if (fadt_hdr && checksum_ok(fadt_hdr, fadt_hdr->length))
        g_fadt = (fadt_t *)fadt_hdr;

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
    if (out_flags) *out_flags = 0;
    return isa_irq;
}

bool acpi_gsi_to_ioapic(uint32_t gsi, acpi_ioapic_t **out_ioapic, uint32_t *out_pin) {
    for (int i = 0; i < acpi_info.ioapic_count; i++) {
        acpi_ioapic_t *io = &acpi_info.ioapics[i];
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

static inline void outw_p(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" :: "a"(val), "Nd"(port));
}

static int find_s5(uint8_t *data, uint32_t len, uint8_t *typa, uint8_t *typb) {
    for (uint32_t i = 0; i + 7 < len; i++) {
        if (data[i]   != 0x08)  continue;
        if (data[i+1] != '_' || data[i+2] != 'S' ||
            data[i+3] != '5' || data[i+4] != '_') continue;

        uint32_t j = i + 5;
        if (j >= len) break;
        if (data[j] == 0x5C) j++; 
        if (data[j] != 0x12) continue; 
        j++;
        if (j >= len) break;

        uint8_t pl = data[j++];
        j += (pl >> 6);
        j++;
        if (j + 2 >= len) break;

        if (data[j] == 0x0A) j++;
        *typa = data[j++];
        if (data[j] == 0x0A) j++;
        *typb = data[j];
        return 1;
    }
    return 0;
}

void acpi_poweroff(void) {
    if (!acpi_info.valid || !g_fadt) {
        dmesg("[acpi] poweroff: no FADT cached\n");
        return;
    }

    sdt_header_t *dsdt = (sdt_header_t *)PHYS((uint64_t)g_fadt->dsdt);
    uint8_t *body      = (uint8_t *)dsdt + sizeof(sdt_header_t);
    uint32_t body_len  = dsdt->length - (uint32_t)sizeof(sdt_header_t);

    uint8_t slp_typa = 0, slp_typb = 0;
    if (!find_s5(body, body_len, &slp_typa, &slp_typb)) {
        dmesg("[acpi] poweroff: _S5_ not found\n");
        return;
    }

    uint16_t pm1a = (uint16_t)g_fadt->pm1a_cnt_blk;
    uint16_t pm1b = (uint16_t)g_fadt->pm1b_cnt_blk;
    uint16_t va   = (uint16_t)((slp_typa << 10) | (1 << 13));
    uint16_t vb   = (uint16_t)((slp_typb << 10) | (1 << 13));

    dmesg("[acpi] writing S5 to PM1_CNT\n");
    asm volatile("cli");
    outw_p(pm1a, va);
    if (pm1b) outw_p(pm1b, vb);

    for (;;) asm volatile("hlt");
}