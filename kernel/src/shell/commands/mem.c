<<<<<<< HEAD
/*
 * Memory shell command: reports PMM/heap state, free pages, usage, and so on.
 */
=======
>>>>>>> origin/x86_64-uefi
#include "../commands.h"
#include "util.h"
#include "../../graphics/terminal.h"
#include "../../mm/pmm.h"
#include "../../mm/heap.h"
#include <limine.h>

extern uint64_t hhdm_offset;
extern struct limine_memmap_response *g_memmap;

void cmd_memtest(void) {
    print_header("Memory Diagnostics");

    terminal_set_fg(COLOR_BODY);
    terminal_print("  Free pages: ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int((uint32_t)pmm_get_free_page_count());
    terminal_println("");

    terminal_set_fg(COLOR_BODY);
    terminal_print("  Allocating 1 page... ");
    void *phys = pmm_alloc_page();
    if (phys) {
        terminal_set_fg(COLOR_SUCCESS);
        terminal_println("OK");
        terminal_set_fg(COLOR_BODY);
        terminal_print("  Physical address: ");
        terminal_set_fg(COLOR_ACCENT);
        terminal_print_hex((uint64_t)phys);
        terminal_println("");

        volatile uint32_t *virt = (uint32_t *)pmm_phys_to_virt((uint64_t)phys);
        terminal_set_fg(COLOR_BODY);
        terminal_print("  Writing pattern 0xDEADBEEF... ");
        *virt = 0xDEADBEEF;
        if (*virt == 0xDEADBEEF) {
            terminal_set_fg(COLOR_SUCCESS);
            terminal_println("OK");
        } else {
            terminal_set_fg(COLOR_ERROR);
            terminal_println("FAILED");
        }
        pmm_free_page(phys);
        terminal_set_fg(COLOR_SUCCESS);
        terminal_println("  Page freed.");
    } else {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("FAILED - Out of memory");
    }

    terminal_println("");
    terminal_set_fg(COLOR_BODY);
    terminal_print("  kmalloc(64)... ");
    void *p = kmalloc(64);
    if (p) {
        terminal_set_fg(COLOR_SUCCESS);
        terminal_println("OK");
        terminal_set_fg(COLOR_BODY);
        terminal_print("  Virtual address: ");
        terminal_set_fg(COLOR_ACCENT);
        terminal_print_hex((uint64_t)p);
        terminal_println("");

        volatile uint64_t *q = (uint64_t *)p;
        *q = 0x123456789ABCDEF0ULL;
        terminal_set_fg(COLOR_BODY);
        terminal_print("  Verifying... ");
        if (*q == 0x123456789ABCDEF0ULL) {
            terminal_set_fg(COLOR_SUCCESS);
            terminal_println("OK");
        } else {
            terminal_set_fg(COLOR_ERROR);
            terminal_println("FAILED");
        }
        kfree(p);
        terminal_set_fg(COLOR_SUCCESS);
        terminal_println("  Freed.");
    } else {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("FAILED");
    }

    terminal_println("");
    terminal_set_fg(COLOR_BODY);
    terminal_print("  kcalloc(16, 4)... ");
    uint32_t *arr = (uint32_t *)kcalloc(16, sizeof(uint32_t));
    if (arr) {
        terminal_set_fg(COLOR_SUCCESS);
        terminal_println("OK");
        terminal_set_fg(COLOR_BODY);
        terminal_print("  Zero check... ");
        int ok = 1;
        for (int i = 0; i < 16; i++) {
            if (arr[i] != 0) { ok = 0; break; }
        }
        if (ok) {
            terminal_set_fg(COLOR_SUCCESS);
            terminal_println("OK");
        } else {
            terminal_set_fg(COLOR_ERROR);
            terminal_println("FAILED");
        }
        kfree(arr);
    }
    terminal_println("");
}

void cmd_meminfo(void) {
    print_header("Memory Information");

    terminal_set_fg(COLOR_BODY);
    terminal_print("  PMM Free Pages:     ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int((uint32_t)pmm_get_free_page_count());
    terminal_println("");

    terminal_set_fg(COLOR_BODY);
    terminal_print("  PMM Free Memory:    ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int((uint32_t)(pmm_get_free_page_count() * 4));
    terminal_println(" KB");

    heap_stats_t hs = heap_get_stats();
    terminal_set_fg(COLOR_BODY);
    terminal_println("");
    terminal_println("  Heap Statistics:");
    terminal_print("    Pages allocated:  ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(hs.pages_allocated);
    terminal_println("");

    terminal_set_fg(COLOR_BODY);
    terminal_print("    Total bytes:      ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(hs.total_bytes);
    terminal_println("");

    terminal_set_fg(COLOR_BODY);
    terminal_print("    Used bytes:       ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(hs.used_bytes);
    terminal_println("");

    terminal_set_fg(COLOR_BODY);
    terminal_print("    Free bytes:       ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(hs.free_bytes);
    terminal_println("");

    terminal_set_fg(COLOR_BODY);
    terminal_print("    Blocks used:      ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(hs.used_blocks);
    terminal_println("");

    terminal_set_fg(COLOR_BODY);
    terminal_print("    Blocks free:      ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(hs.free_blocks);
    terminal_println("");

    if (g_memmap) {
        terminal_set_fg(COLOR_BODY);
        terminal_println("");
        terminal_println("  Memory Map:");
        static const char *types[] = {
            "Usable", "Reserved", "ACPI Reclaimable", "ACPI NVS",
            "Bad Memory", "Bootloader Reclaimable", "Bootloader",
            "Kernel/Modules", "Framebuffer"
        };
        for (uint64_t i = 0; i < g_memmap->entry_count; i++) {
            struct limine_memmap_entry *e = g_memmap->entries[i];
            uint8_t t = (uint8_t)(e->type > 8 ? 8 : e->type);
            terminal_set_fg((e->type == 0) ? COLOR_HIGHLIGHT : COLOR_BODY);
            terminal_print("    ");
            terminal_print_hex(e->base);
            terminal_print(" + ");
            terminal_print_hex(e->length);
            terminal_print("  [");
            terminal_print(types[t]);
            terminal_println("]");
        }
    }
    terminal_println("");
}

void cmd_vminfo(void) {
    print_header("Virtual Memory Information");

    terminal_set_fg(COLOR_BODY);
    terminal_print("  HHDM Offset: ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_hex(hhdm_offset);
    terminal_println("");

    terminal_set_fg(COLOR_BODY);
    terminal_print("  CR3 Register: ");
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_hex(cr3);
    terminal_println("");

    terminal_set_fg(COLOR_DIM);
    terminal_println("  Note: vmm_virt_to_phys only works in own pagemaps.");
    terminal_println("");
}

void cmd_hexdump(const char *addr_str, const char *len_str) {
    calc_set_input(addr_str);
    uint64_t addr = calc_expr();
    calc_set_input(len_str);
    uint64_t len  = calc_expr();
    if (len == 0 || len > 4096) len = 256;
    if (addr < 0x100000000ULL) addr += hhdm_offset;

    terminal_set_fg(COLOR_HEADER);
    terminal_print("\n  hexdump ");
    terminal_print_hex(addr);
    terminal_set_fg(COLOR_BODY);
    terminal_print(" (");
    terminal_print_int((uint32_t)len);
    terminal_println(" bytes)");

    uint8_t *p = (uint8_t *)(uintptr_t)addr;
    for (uint64_t row = 0; row < len; row += 16) {
        terminal_set_fg(COLOR_ACCENT);
        terminal_print("  ");
        terminal_print_hex(addr + row);
        terminal_print("  ");

        terminal_set_fg(COLOR_BODY);
        for (int col = 0; col < 16; col++) {
            if (row + col < len) {
                uint8_t b = p[row + col];
                char h[] = "0123456789ABCDEF";
                terminal_putchar(h[b >> 4]);
                terminal_putchar(h[b & 0xF]);
                terminal_putchar(' ');
            } else {
                terminal_print("   ");
            }
            if (col == 7) terminal_putchar(' ');
        }

        terminal_print(" |");
        terminal_set_fg(COLOR_HIGHLIGHT);
        for (int col = 0; col < 16 && row + col < len; col++) {
            char c = (char)p[row + col];
            terminal_putchar((c >= 32 && c < 127) ? c : '.');
        }
        terminal_set_fg(COLOR_BODY);
        terminal_println("|");
    }
    terminal_println("");
}

void cmd_peek(const char *addr_str) {
    calc_set_input(addr_str);
    uint64_t addr = calc_expr();
    if (addr < 0x100000000ULL) addr += hhdm_offset;
    volatile uint64_t *p = (volatile uint64_t *)(uintptr_t)addr;

    terminal_set_fg(COLOR_HEADER);
    terminal_print("\n  peek ");
    terminal_print_hex(addr);
    terminal_print("  =  ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_hex(*p);
    terminal_println("\n");
}

void cmd_poke(const char *addr_str, const char *val_str) {
    calc_set_input(addr_str);
    uint64_t addr = calc_expr();
    calc_set_input(val_str);
    uint64_t val  = calc_expr();
    if (addr < 0x100000000ULL) addr += hhdm_offset;
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)addr;
    *p = (uint32_t)val;

    terminal_set_fg(COLOR_WARNING);
    terminal_print("\n  poke ");
    terminal_print_hex(addr);
    terminal_print(" <- ");
    terminal_print_hex(val);
    terminal_println("\n");
}