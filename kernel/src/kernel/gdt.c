#include <stdint.h>
#include <stddef.h>

#include "gdt.h"
#include "dmesg.h"

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t length;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  flags1;
    uint8_t  flags2;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed)) tss_desc_t;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed)) tss_t;

typedef struct {
    gdt_entry_t null_entry;
    gdt_entry_t kcode;
    gdt_entry_t kdata;
    gdt_entry_t ucode;
    gdt_entry_t udata;
    tss_desc_t  tss;
} __attribute__((packed)) gdt_table_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

static gdt_table_t gdt_table;
static gdt_ptr_t   gdt_ptr;
static tss_t        tss;

#define IST1_STACK_SIZE 8192
static uint8_t ist1_stack[IST1_STACK_SIZE] __attribute__((aligned(16)));

static void gdt_set_entry(gdt_entry_t *e, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t gran_flags) {
    e->base_low    = base & 0xFFFF;
    e->base_mid    = (base >> 16) & 0xFF;
    e->base_high   = (base >> 24) & 0xFF;
    e->limit_low   = limit & 0xFFFF;
    e->granularity = ((limit >> 16) & 0x0F) | (gran_flags & 0xF0);
    e->access      = access;
}

static void tss_set_descriptor(tss_desc_t *d, uint64_t base, uint32_t limit) {
    d->length     = limit & 0xFFFF;
    d->base_low   = base & 0xFFFF;
    d->base_mid   = (base >> 16) & 0xFF;
    d->flags1     = 0x89;
    d->flags2     = (uint8_t)((limit >> 16) & 0x0F);
    d->base_high  = (base >> 24) & 0xFF;
    d->base_upper = (uint32_t)(base >> 32);
    d->reserved   = 0;
}

__attribute__((naked)) static void gdt_flush(void *ptr) {
    asm volatile(
        "lgdt (%%rdi);"
        "mov $0x10, %%ax;"
        "mov %%ax, %%ds;"
        "mov %%ax, %%es;"
        "mov %%ax, %%fs;"
        "mov %%ax, %%gs;"
        "mov %%ax, %%ss;"
        "pushq $0x08;"
        "leaq 1f(%%rip), %%rax;"
        "pushq %%rax;"
        "lretq;"
        "1: ret;"
        ::: "memory"
    );
}

__attribute__((naked)) static void tss_flush(uint16_t sel) {
    asm volatile(
        "mov %%di, %%ax;"
        "ltr %%ax;"
        "ret;"
        ::: "memory"
    );
}

void tss_set_rsp0(uint64_t stack_top) {
    tss.rsp0 = stack_top;
}

void tss_set_ist(int ist_index, uint64_t stack_top) {
    switch (ist_index) {
        case 1: tss.ist1 = stack_top; break;
        case 2: tss.ist2 = stack_top; break;
        case 3: tss.ist3 = stack_top; break;
        case 4: tss.ist4 = stack_top; break;
        case 5: tss.ist5 = stack_top; break;
        case 6: tss.ist6 = stack_top; break;
        case 7: tss.ist7 = stack_top; break;
        default: break;
    }
}

void gdt_init(void) {
    for (size_t i = 0; i < sizeof(gdt_table); i++) ((uint8_t *)&gdt_table)[i] = 0;
    for (size_t i = 0; i < sizeof(tss); i++)       ((uint8_t *)&tss)[i] = 0;

    gdt_set_entry(&gdt_table.kcode, 0, 0xFFFFF, 0x9A, 0xA0);
    gdt_set_entry(&gdt_table.kdata, 0, 0xFFFFF, 0x92, 0xA0);
    gdt_set_entry(&gdt_table.ucode, 0, 0xFFFFF, 0xFA, 0xA0);
    gdt_set_entry(&gdt_table.udata, 0, 0xFFFFF, 0xF2, 0xA0);

    tss.iopb_offset = sizeof(tss_t);
    tss_set_ist(1, (uint64_t)(ist1_stack + IST1_STACK_SIZE));

    tss_set_descriptor(&gdt_table.tss, (uint64_t)&tss, sizeof(tss_t) - 1);

    gdt_ptr.limit = sizeof(gdt_table) - 1;
    gdt_ptr.base  = (uint64_t)&gdt_table;

    gdt_flush(&gdt_ptr);
    tss_flush(GDT_SEL_TSS);

    dmesg("[gdt] GDT + TSS loaded, IST1 stack ready\n");
}