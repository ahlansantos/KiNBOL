#include "pmm.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../drivers/serial.h"

static uint64_t hhdm_off      = 0;
static uint8_t *bitmap        = NULL;
static uint64_t total_pages   = 0;
static uint64_t free_pages    = 0;
static uint64_t highest_phys  = 0;

static inline void bm_set(uint64_t page)   { bitmap[page / 8] |=  (1u << (page % 8)); }
static inline void bm_clear(uint64_t page) { bitmap[page / 8] &= ~(1u << (page % 8)); }
static inline int  bm_get(uint64_t page)   { return (bitmap[page / 8] >> (page % 8)) & 1; }

uint64_t pmm_phys_to_virt(uint64_t phys) {
    return phys + hhdm_off;
}

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset) {
    hhdm_off = hhdm_offset;

    serial_print("=== memmap ===\n");
    uint64_t highest = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        serial_print("  base="); serial_hex(e->base);
        serial_print(" len=");   serial_hex(e->length);
        serial_print(" type=");  serial_hex(e->type);
        serial_print("\n");
        uint64_t end = e->base + e->length;
        if (end > highest_phys) highest_phys = end;
        if (e->type == LIMINE_MEMMAP_USABLE) {
            if (end > highest) highest = end;
        }
    }
    serial_print("  highest_usable="); serial_hex(highest); serial_print("\n");

    total_pages = highest / PAGE_SIZE;
    uint64_t bitmap_bytes = (total_pages + 7) / 8;

    serial_print("  total_pages="); serial_hex(total_pages);
    serial_print(" bitmap_bytes="); serial_hex(bitmap_bytes);
    serial_print("\n");

    bitmap = NULL;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE
                && e->base >= 0x100000
                && e->length >= bitmap_bytes) {
            bitmap = (uint8_t *)(e->base + hhdm_off);
            serial_print("  bitmap -> phys="); serial_hex(e->base);
            serial_print(" bytes="); serial_hex(bitmap_bytes);
            serial_print("\n");
            break;
        }
    }

    if (!bitmap) {
        serial_print("  ERROR: bitmap NULL, nenhuma regiao adequada!\n");
        return;
    }

    for (uint64_t i = 0; i < total_pages; i++)
        bm_set(i);

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        uint64_t start = e->base / PAGE_SIZE;
        uint64_t count = e->length / PAGE_SIZE;
        for (uint64_t p = 0; p < count; p++) {
            bm_clear(start + p);
            free_pages++;
        }
    }

    uint64_t bm_phys  = (uint64_t)bitmap - hhdm_off;
    uint64_t bm_pages = (bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t p = 0; p < bm_pages; p++) {
        uint64_t pg = bm_phys / PAGE_SIZE + p;
        if (!bm_get(pg)) {
            bm_set(pg);
            free_pages--;
        }
    }

    serial_print("  free_pages="); serial_hex(free_pages); serial_print("\n");
}

void *pmm_alloc_page(void) {
    uint64_t start = 0x100000 / PAGE_SIZE;
    for (uint64_t i = start; i < total_pages; i++) {
        if (!bm_get(i)) {
            bm_set(i);
            free_pages--;
            return (void *)(i * PAGE_SIZE);
        }
    }
    return NULL;
}

void pmm_free_page(void *phys) {
    uint64_t page = (uint64_t)phys / PAGE_SIZE;
    if (page < total_pages && bm_get(page)) {
        bm_clear(page);
        free_pages++;
    }
}

uint64_t pmm_get_free_page_count(void) {
    return free_pages;
}

uint64_t pmm_get_total_pages(void) {
    return total_pages;
}

uint64_t pmm_get_highest_phys(void) {
    return highest_phys;
}

void *pmm_alloc_pages_contiguous(uint64_t count) {
    if (count == 0) return NULL;

    uint64_t start = 0x100000 / PAGE_SIZE;
    for (uint64_t i = start; i <= total_pages - count; i++) {
        bool free = true;
        for (uint64_t j = 0; j < count; j++) {
            if (bm_get(i + j)) { free = false; break; }
        }
        if (free) {
            for (uint64_t j = 0; j < count; j++) bm_set(i + j);
            free_pages -= count;
            return (void *)(i * PAGE_SIZE);
        }
    }
    return NULL;
}

void pmm_free_pages(void *phys, uint64_t count) {
    uint64_t page = (uint64_t)phys / PAGE_SIZE;
    for (uint64_t j = 0; j < count; j++) {
        uint64_t pg = page + j;
        if (pg < total_pages && bm_get(pg)) {
            bm_clear(pg);
            free_pages++;
        }
    }
}
