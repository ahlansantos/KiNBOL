#include "../bmp/logo.h"
#include "commands.h"
#include "../graphics/terminal.h"
#include "../graphics/api/baregl.h"
#include "../graphics/games/doomport.h"
#include "../drivers/rtc.h"
#include "../drivers/keyboard.h"
#include "../kernel/dmesg.h"
#include "../kernel/pit.h"
#include "../mm/pmm.h"
#include "../mm/heap.h"
#include "../fs/vfs.h"
#include "../fs/ramdisk.h"
#include <limine.h>

extern uint64_t hhdm_offset;
extern struct limine_framebuffer     *fbi;
extern struct limine_memmap_response *g_memmap;

#define COLOR_HEADER    0x00FFFF
#define COLOR_SUCCESS   0x00FF00
#define COLOR_ERROR     0xFF0000
#define COLOR_WARNING   0xFFAA00
#define COLOR_BODY      0xDDDDDD
#define COLOR_HIGHLIGHT 0x88CC88
#define COLOR_ACCENT    0x88AACC
#define COLOR_DIM       0x555555
#define COLOR_WHITE     0xFFFFFF
#define MAX_SLEEP_MS    3600000

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0,%1" :: "a"(val), "Nd"(port));
}

static uint32_t get_ticks(void) {
    return (uint32_t)(uptime_ms() / 10);
}

static int str_len(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

static void print_header(const char *title) {
    terminal_set_fg(COLOR_HEADER);
    terminal_println("");
    terminal_print("  ─── ");
    terminal_print(title);
    terminal_println(" ───");
    terminal_set_fg(COLOR_WHITE);
}

static void print_separator(void) {
    terminal_set_fg(COLOR_DIM);
    terminal_println("  ────────────────────────────────────────");
    terminal_set_fg(COLOR_WHITE);
}

static void print_field(const char *label, const char *value) {
    terminal_set_fg(COLOR_BODY);
    terminal_print("  ");
    terminal_print(label);
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_println(value);
}

static const char *calc_ptr;

static uint64_t calc_expr(void);

static uint64_t calc_num(void) {
    while (*calc_ptr == ' ') calc_ptr++;
    uint64_t val = 0;
    if (calc_ptr[0] == '0' && (calc_ptr[1] == 'x' || calc_ptr[1] == 'X')) {
        calc_ptr += 2;
        while ((*calc_ptr >= '0' && *calc_ptr <= '9') ||
               (*calc_ptr >= 'a' && *calc_ptr <= 'f') ||
               (*calc_ptr >= 'A' && *calc_ptr <= 'F')) {
            char c = *calc_ptr++;
            if      (c >= '0' && c <= '9') val = val * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
            else                           val = val * 16 + (c - 'A' + 10);
        }
    } else {
        while (*calc_ptr >= '0' && *calc_ptr <= '9')
            val = val * 10 + (*calc_ptr++) - '0';
    }
    return val;
}

static uint64_t calc_primary(void) {
    while (*calc_ptr == ' ') calc_ptr++;
    if (*calc_ptr == '(') {
        calc_ptr++;
        uint64_t v = calc_expr();
        if (*calc_ptr == ')') calc_ptr++;
        return v;
    }
    if (*calc_ptr == '~') { calc_ptr++; return ~calc_primary(); }
    if (*calc_ptr == '-') { calc_ptr++; return (uint64_t)(-(int64_t)calc_primary()); }
    return calc_num();
}

static uint64_t calc_mul(void) {
    uint64_t v = calc_primary();
    while (1) {
        while (*calc_ptr == ' ') calc_ptr++;
        if (*calc_ptr == '*') { calc_ptr++; v *= calc_primary(); }
        else if (*calc_ptr == '/') {
            calc_ptr++;
            uint64_t d = calc_primary();
            v = d ? v / d : 0;
        }
        else if (*calc_ptr == '%') {
            calc_ptr++;
            uint64_t d = calc_primary();
            v = d ? v % d : 0;
        }
        else break;
    }
    return v;
}

static uint64_t calc_add(void) {
    uint64_t v = calc_mul();
    while (1) {
        while (*calc_ptr == ' ') calc_ptr++;
        if (*calc_ptr == '+') { calc_ptr++; v += calc_mul(); }
        else if (*calc_ptr == '-') { calc_ptr++; v -= calc_mul(); }
        else break;
    }
    return v;
}

static uint64_t calc_shift(void) {
    uint64_t v = calc_add();
    while (1) {
        while (*calc_ptr == ' ') calc_ptr++;
        if (calc_ptr[0] == '<' && calc_ptr[1] == '<') {
            calc_ptr += 2;
            v <<= calc_add();
        }
        else if (calc_ptr[0] == '>' && calc_ptr[1] == '>') {
            calc_ptr += 2;
            v >>= calc_add();
        }
        else break;
    }
    return v;
}

static uint64_t calc_band(void) {
    uint64_t v = calc_shift();
    while (*calc_ptr == '&' && calc_ptr[1] != '&') {
        calc_ptr++;
        v &= calc_shift();
    }
    return v;
}

static uint64_t calc_bxor(void) {
    uint64_t v = calc_band();
    while (*calc_ptr == '^') { calc_ptr++; v ^= calc_band(); }
    return v;
}

static uint64_t calc_bor(void) {
    uint64_t v = calc_bxor();
    while (*calc_ptr == '|' && calc_ptr[1] != '|') {
        calc_ptr++;
        v |= calc_bxor();
    }
    return v;
}

static uint64_t calc_expr(void) {
    return calc_bor();
}

void cmd_help(void) {
    terminal_set_fg(COLOR_HEADER);
    terminal_println("\n  |-----------------------------------------|");
    terminal_println(  "  |        KiNBOL - Command Reference       |");
    terminal_println(  "  |_________________________________________|\n");

    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_println("  System Commands:");
    terminal_set_fg(COLOR_BODY);
    terminal_println("    help        - Display this reference");
    terminal_println("    clear       - Clear terminal screen");
    terminal_println("    uname       - Display system information");
    terminal_println("    echo <txt>  - Print text to terminal");
    terminal_println("    sleep <ms>  - Pause execution");
    terminal_println("    date        - Display current date/time");
    terminal_println("    ticks       - Show system uptime ticks");
    terminal_println("    crash       - Trigger kernel panic");
    terminal_println("    fastfetch   - System overview");
    terminal_println("    reboot      - Reboot system");
    terminal_println("    dmesg       - Display kernel log");

    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_println("\n  Memory & Hardware:");
    terminal_set_fg(COLOR_BODY);
    terminal_println("    meminfo     - Memory statistics");
    terminal_println("    memtest     - Memory allocation test");
    terminal_println("    vminfo      - Virtual memory info");
    terminal_println("    hexdump <a> <l> - Hex dump memory");
    terminal_println("    peek <addr> - Read memory address");
    terminal_println("    poke <a> <v> - Write memory address");

    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_println("\n  File System:");
    terminal_set_fg(COLOR_BODY);
    terminal_println("    ramls       - List ramdisk files");
    terminal_println("    ramcat <f>  - Display file contents");
    terminal_println("    ramwrite <f> <txt> - Create/write file");
    terminal_println("    ramdel <f>  - Delete file");
    terminal_println("    raminfo     - Ramdisk statistics");
    terminal_println("    vfsls       - List /dev nodes");
    terminal_println("    vfsread <dev> - Read device node");
    terminal_println("    vfswrite <dev> <txt> - Write to device");

    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_println("\n  Graphics (BareGL):");
    terminal_set_fg(COLOR_BODY);
    terminal_println("    bmpv <file> - Display BMP image");
    terminal_println("    drawtest    - Graphics test pattern");
    terminal_println("    doomport    - Launch DOOM port");
    terminal_println("    pixel <x> <y> - Draw pixel");
    terminal_println("    line <x0> <y0> <x1> <y1> - Draw line");
    terminal_println("    rect <x> <y> <w> <h> - Draw rectangle");
    terminal_println("    fillrect <x> <y> <w> <h> - Fill rect");
    terminal_println("    circle <x> <y> <r> - Draw circle");
    terminal_println("    clearfb    - Clear framebuffer");

    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_println("\n  Utilities:");
    terminal_set_fg(COLOR_BODY);
    terminal_println("    calc <expr>- Expression calculator");
    terminal_println("    ascii      - ASCII table");
    terminal_println("    memtest    - Memory diagnostics");
    terminal_println("    anim       - Animation test\n");
}

void cmd_ticks(void) {
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print("  Uptime: ");
    terminal_set_fg(COLOR_BODY);
    terminal_print_int(get_ticks());
    terminal_print(" ticks  |  ");
    terminal_print_int((uint32_t)(uptime_ms() / 1000));
    terminal_println(" seconds");
}

void cmd_sleep(const char *arg) {
    unsigned long long ms = 0;
    int valid = 0;
    for (int i = 0; arg[i] >= '0' && arg[i] <= '9'; i++) {
        ms = ms * 10 + (arg[i] - '0');
        valid = 1;
    }
    if (!valid || ms == 0) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Error: Invalid duration");
        return;
    }
    if (ms > MAX_SLEEP_MS) ms = MAX_SLEEP_MS;

    terminal_set_fg(COLOR_BODY);
    terminal_print("  Sleeping for ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int((uint32_t)ms);
    terminal_set_fg(COLOR_BODY);
    terminal_println("ms...");
    sleep_ms((uint32_t)ms);
    terminal_set_fg(COLOR_SUCCESS);
    terminal_println("  Done.");
}

void cmd_crash(void) {
    terminal_set_fg(COLOR_ERROR);
    terminal_println("  Triggering kernel panic...");
    volatile int a = 10, b = 0, c = a / b;
    (void)c;
}

void cmd_reboot(void) {
    terminal_set_fg(COLOR_WARNING);
    terminal_println("  Rebooting system...");
    outb(0x64, 0xFE);
}

void cmd_anim(void) {
    terminal_set_fg(COLOR_BODY);
    terminal_print("\n  ");
    const char spinner[] = "|/-\\";
    for (int i = 0; i < 20; i++) {
        terminal_putchar(spinner[i % 4]);
        sleep_ms(100);
        terminal_putchar('\b');
    }
    terminal_set_fg(COLOR_SUCCESS);
    terminal_println(" Done!");
}

void cmd_date(void) {
    rtc_time_t t = rtc_read();
    print_header("System Date & Time");

    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print("\n  ");
    if (t.day    < 10) terminal_putchar('0');
    terminal_print_int(t.day);
    terminal_putchar('/');
    if (t.month  < 10) terminal_putchar('0');
    terminal_print_int(t.month);
    terminal_putchar('/');
    terminal_print_int(t.year);
    terminal_print("  ─  ");
    if (t.hour   < 10) terminal_putchar('0');
    terminal_print_int(t.hour);
    terminal_putchar(':');
    if (t.minute < 10) terminal_putchar('0');
    terminal_print_int(t.minute);
    terminal_putchar(':');
    if (t.second < 10) terminal_putchar('0');
    terminal_print_int(t.second);
    terminal_println("\n");
}

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

void cmd_fastfetch(void) {
    terminal_clear();

    terminal_set_fg(0x88AACC);
    terminal_println(" ___  __    ___  ________   ________  ________  ___          ");
    terminal_println("|\\  \\|\\  \\ |\\  \\|\\   ___  \\|\\   __  \\|\\   __  \\|\\  \\         ");
    terminal_println("\\ \\  \\/  /|\\ \\  \\ \\  \\\\ \\  \\ \\  \\|\\ /\\ \\  \\|\\  \\ \\  \\        ");
    terminal_println(" \\ \\   ___  \\ \\  \\ \\  \\\\ \\  \\ \\   __  \\ \\  \\\\\\  \\ \\  \\       ");
    terminal_println("  \\ \\  \\\\ \\  \\ \\  \\ \\  \\\\ \\  \\ \\  \\|\\  \\ \\  \\\\\\  \\ \\  \\____  ");
    terminal_println("   \\ \\__\\\\ \\__\\ \\__\\ \\__\\\\ \\__\\ \\_______\\ \\_______\\ \\_______\\");
    terminal_println("    \\|__| \\|__|\\|__|\\|__| \\|__|\\|_______|\\|_______|\\|_______\\");
    terminal_println("");

    terminal_set_fg(0x88CC88);
    terminal_println("  KiNBOL 0.06.1");
    terminal_set_fg(0xAAAAAA);
    terminal_println("  this Kernel is Not Based On Linux");
    terminal_println("");

    terminal_set_fg(0x88CC88);
    terminal_println("  kernel@KiNBOL");
    terminal_set_fg(0xAAAAAA);
    terminal_println("  -----------");

    terminal_set_fg(0xDDDDDD);
    terminal_print("  OS:       ");
    terminal_set_fg(0x88CC88);
    terminal_println("KiNBOL 0.06.1");

    terminal_set_fg(0xDDDDDD);
    terminal_print("  Kernel:   ");
    terminal_set_fg(0x88CC88);
    terminal_println("x86_64 Limine UEFI");

    terminal_set_fg(0xDDDDDD);
    terminal_print("  Shell:    ");
    terminal_set_fg(0x88CC88);
    terminal_println("kinsh 1");

    terminal_set_fg(0xDDDDDD);
    terminal_print("  VFS:      ");
    terminal_set_fg(0x88CC88);
    terminal_print_int(vfs_node_count());
    terminal_println(" nodes");

    uint64_t ms = uptime_ms();
    uint32_t s = (uint32_t)(ms / 1000);
    uint32_t h = s / 3600;
    uint32_t m = (s % 3600) / 60;
    s = s % 60;

    terminal_set_fg(0xDDDDDD);
    terminal_print("  Uptime:   ");
    terminal_set_fg(0x88CC88);

    if (h) { terminal_print_int(h); terminal_print("h "); }
    if (m) { terminal_print_int(m); terminal_print("m "); }
    terminal_print_int(s);
    terminal_println("s");

    terminal_set_fg(0xDDDDDD);
    terminal_print("  Display:  ");
    terminal_set_fg(0x88CC88);
    terminal_print_int(fbi->width);
    terminal_print("x");
    terminal_print_int(fbi->height);
    terminal_print(" @ ");
    terminal_print_int(fbi->bpp);
    terminal_println("bpp");

    terminal_set_fg(0xDDDDDD);
    terminal_print("  BareGL:   ");
    terminal_set_fg(0x88CC88);
    terminal_println("0.2 (SR)");

    terminal_set_fg(0xDDDDDD);
    terminal_print("  CPU:      ");
    terminal_set_fg(0x88CC88);

    uint32_t eax, ebx, ecx, edx;
    char cpu[49] = {0};

    asm volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x80000002));
    *(uint32_t *)(cpu + 0)  = eax;
    *(uint32_t *)(cpu + 4)  = ebx;
    *(uint32_t *)(cpu + 8)  = ecx;
    *(uint32_t *)(cpu + 12) = edx;

    asm volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x80000003));
    *(uint32_t *)(cpu + 16) = eax;
    *(uint32_t *)(cpu + 20) = ebx;
    *(uint32_t *)(cpu + 24) = ecx;
    *(uint32_t *)(cpu + 28) = edx;

    asm volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x80000004));
    *(uint32_t *)(cpu + 32) = eax;
    *(uint32_t *)(cpu + 36) = ebx;
    *(uint32_t *)(cpu + 40) = ecx;
    *(uint32_t *)(cpu + 44) = edx;

    cpu[48] = 0;

    int cp = 0;
    while (cpu[cp] == ' ') cp++;
    terminal_println(&cpu[cp]);

    terminal_set_fg(0xDDDDDD);
    terminal_print("  TSC:      ");
    terminal_set_fg(0x88CC88);
    terminal_print_int((uint32_t)(tsc_hz / 1000000));
    terminal_println(" MHz");

    terminal_set_fg(0xDDDDDD);
    terminal_print("  PMM:      ");
    terminal_set_fg(0x88CC88);
    terminal_print_int((uint32_t)pmm_get_free_page_count());
    terminal_println(" pages free");

    terminal_println("");
    uint32_t cols[] = {0xCC88AA, 0xCCCC88, 0x88CC88, 0x88AACC, 0x8888CC};

    for (int r = 0; r < 5; r++) {
        terminal_print("  ");
        terminal_set_fg(cols[r]);
        for (int c = 0; c < 30; c++) terminal_print("#");
        terminal_println("");
    }

    terminal_println("");
}

void cmd_ascii(void) {
    print_header("ASCII Table");

    terminal_set_fg(COLOR_DIM);
    terminal_println("  ───┬─────────────────────────────────────");
    terminal_println("  Dec│ Hex  Char");
    terminal_println("  ───┼─────────────────────────────────────");

    for (int i = 0; i < 128; i++) {
        terminal_set_fg(COLOR_BODY);
        terminal_print("  ");
        if (i < 100) terminal_putchar(' ');
        if (i < 10)  terminal_putchar(' ');
        terminal_print_int(i);
        terminal_print(" │ ");

        terminal_set_fg(COLOR_ACCENT);
        char hex[] = "0123456789ABCDEF";
        terminal_putchar(hex[i >> 4]);
        terminal_putchar(hex[i & 0xF]);
        terminal_print("   ");

        terminal_set_fg((i < 32 || i == 127) ? COLOR_DIM : COLOR_HIGHLIGHT);
        if (i < 32 || i == 127) {
            terminal_println(".");
        } else {
            terminal_putchar((char)i);
            terminal_println("");
        }
    }
    terminal_println("");
}

void cmd_dmesg(void) {
    print_header("Kernel Log");
    terminal_set_fg(COLOR_HIGHLIGHT);
    dmesg_foreach(terminal_putchar);
    terminal_set_fg(COLOR_BODY);
    terminal_print("\n  ─── ");
    terminal_print_int(dmesg_len());
    terminal_println(" bytes in buffer ───\n");
}

static void vfsls_cb(const char *name, uint32_t flags, uint32_t size) {
    terminal_set_fg((flags & VFS_BLOCKDEV) ? COLOR_WARNING : COLOR_HIGHLIGHT);
    terminal_print("  /dev/");
    terminal_print(name);
    if (size) {
        terminal_set_fg(COLOR_BODY);
        terminal_print("  (");
        terminal_print_int(size);
        terminal_print(" bytes)");
    }
    terminal_println("");
}

void cmd_vfsls(void) {
    print_header("/dev Nodes");
    vfs_list(vfsls_cb);
    terminal_println("");
}

void cmd_vfsread(const char *dev) {
    vfs_node_t *node = vfs_find(dev);
    if (!node) {
        terminal_set_fg(COLOR_ERROR);
        terminal_print("  Not found: ");
        terminal_println(dev);
        return;
    }
    uint8_t buf[256];
    uint32_t n = vfs_read(node, 0, 255, buf);
    buf[n] = 0;

    terminal_set_fg(COLOR_BODY);
    terminal_print("\n  Read ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(n);
    terminal_set_fg(COLOR_BODY);
    terminal_println(" bytes:");
    terminal_print("  ");

    for (uint32_t i = 0; i < n; i++) {
        char c = (char)buf[i];
        terminal_set_fg(COLOR_HIGHLIGHT);
        terminal_putchar((c >= 32 && c < 127) ? c : '.');
    }
    terminal_println("\n");
}

void cmd_vfswrite(const char *dev, const char *data) {
    vfs_node_t *node = vfs_find(dev);
    if (!node) {
        terminal_set_fg(COLOR_ERROR);
        terminal_print("  Not found: ");
        terminal_println(dev);
        return;
    }
    uint32_t len = (uint32_t)str_len(data);
    uint32_t n = vfs_write(node, 0, len, (const uint8_t *)data);
    terminal_set_fg(COLOR_SUCCESS);
    terminal_print("\n  Wrote ");
    terminal_print_int(n);
    terminal_println(" bytes\n");
}

void cmd_ramls(void) {
    print_header("Ramdisk Contents");
    if (!ramfile_count) {
        terminal_set_fg(COLOR_DIM);
        terminal_println("  (empty)");
    } else {
        for (int i = 0; i < MAX_RAMFILES; i++) {
            if (ramfiles[i].name[0]) {
                terminal_set_fg(COLOR_HIGHLIGHT);
                terminal_print("  ");
                terminal_print(ramfiles[i].name);
                terminal_set_fg(COLOR_BODY);
                terminal_print("  (");
                terminal_print_int(ramfiles[i].size);
                terminal_println(" bytes)");
            }
        }
    }
    terminal_println("");
}

void cmd_raminfo(void) {
    print_header("Ramdisk Statistics");
    terminal_set_fg(COLOR_BODY);
    terminal_print("  Files:      ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(ramfile_count);
    terminal_println("");

    terminal_set_fg(COLOR_BODY);
    terminal_print("  Free slots: ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(MAX_RAMFILES - ramfile_count);
    terminal_println("");
    terminal_println("");
}

void cmd_ramcat(const char *name) {
    int idx = ramdisk_find(name);
    if (idx < 0) {
        terminal_set_fg(COLOR_ERROR);
        terminal_print("  Not found: ");
        terminal_println(name);
        return;
    }
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print("  ");
    for (uint32_t i = 0; i < ramfiles[idx].size; i++) {
        terminal_putchar((char)ramfiles[idx].data[i]);
    }
    terminal_println("");
}

void cmd_ramwrite(const char *name, const char *content) {
    int r = ramdisk_create(name, (const uint8_t *)content, (uint32_t)str_len(content));
    if (r == 0) {
        terminal_set_fg(COLOR_SUCCESS);
        terminal_print("  Written: ");
        terminal_println(name);
    } else {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Error: Could not write file.");
    }
}

void cmd_ramdel(const char *name) {
    int r = ramdisk_delete(name);
    if (r == 0) {
        terminal_set_fg(COLOR_SUCCESS);
        terminal_print("  Deleted: ");
        terminal_println(name);
    } else {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Error: File not found.");
    }
}

void cmd_calc(const char *expr) {
    calc_ptr = expr;
    uint64_t result = calc_expr();

    terminal_set_fg(COLOR_HEADER);
    terminal_print("\n  = ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_hex(result);
    terminal_set_fg(COLOR_BODY);
    terminal_print("  (dec: ");
    terminal_print_int((uint32_t)result);
    terminal_println(")\n");
}

void cmd_hexdump(const char *addr_str, const char *len_str) {
    calc_ptr = addr_str;
    uint64_t addr = calc_expr();
    calc_ptr = len_str;
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
    calc_ptr = addr_str;
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
    calc_ptr = addr_str;
    uint64_t addr = calc_expr();
    calc_ptr = val_str;
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

void cmd_drawtest(void) {
    bare_sync_from_fb();
    bare_rect_fill(100, 100, 200, 150, bare_rgb(255, 0, 0));
    bare_circle(400, 300, 80, bare_rgb(0, 255, 100));
    bare_line(0, 0, 640, 480, bare_rgb(0, 150, 255));
    bare_flip();
}

void cmd_ray(void) {
    doomport_run();
}

void cmd_pixel(int x, int y) {
    bare_sync_from_fb();
    bare_pixel(x, y, bare_rgb(255, 255, 255));
    bare_flip();
}

void cmd_line(int x0, int y0, int x1, int y1) {
    bare_sync_from_fb();
    bare_line(x0, y0, x1, y1, bare_rgb(0, 255, 0));
    bare_flip();
}

void cmd_rect(int x, int y, int w, int h) {
    bare_sync_from_fb();
    bare_rect(x, y, w, h, bare_rgb(255, 0, 0));
    bare_flip();
}

void cmd_fillrect(int x, int y, int w, int h) {
    bare_sync_from_fb();
    bare_rect_fill(x, y, w, h, bare_rgb(0, 0, 255));
    bare_flip();
}

void cmd_circle(int x, int y, int r) {
    bare_sync_from_fb();
    bare_circle(x, y, r, bare_rgb(255, 255, 0));
    bare_flip();
}

void cmd_clearfb(void) {
    bare_clear(bare_rgb(0, 0, 0));
    bare_flip();
    terminal_clear();
    terminal_set_fg(COLOR_HIGHLIGHT);
}

void cmd_baregl_status(void) {
    print_header("BareGL Status");

    print_field("  Version:      ", "0.2 (SR)");
    print_field("  Status:       ", "Active");
    print_field("  Backend:      ", "Software Rasterizer (SR)");
    print_field("  Double Buffer: ", "Yes");

    terminal_set_fg(COLOR_BODY);
    terminal_print("  Resolution:   ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(bare_width());
    terminal_print("x");
    terminal_print_int(bare_height());
    terminal_println("");

    terminal_set_fg(COLOR_BODY);
    terminal_print("  Pitch:        ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(fbi->pitch);
    terminal_println("");

    terminal_set_fg(COLOR_BODY);
    terminal_print("  BPP:          ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(fbi->bpp);
    terminal_println("");

    print_separator();
    terminal_set_fg(COLOR_BODY);
    terminal_println("  BareGL - Minimal graphics library for KiNBOL");
    terminal_println("  Software rasterizer with double buffering");
    terminal_println("  Supports: pixel, line, rect, fill, circle");
    terminal_println("");
}

void cmd_bmp(const char *name) {
    int idx = ramdisk_find(name);
    if (idx < 0) {
        terminal_set_fg(COLOR_ERROR);
        terminal_print("  Not found: ");
        terminal_println(name);
        return;
    }
    bare_sync_from_fb();
    int r = bare_bmp_draw(0, 0, ramfiles[idx].data, ramfiles[idx].size);
    if (r != 0) {
        terminal_set_fg(COLOR_ERROR);
        terminal_print("  BMP error: ");
        terminal_print_int(r);
        terminal_println("");
        return;
    }
    bare_flip();
    terminal_set_fg(COLOR_SUCCESS);
    terminal_println("  BMP drawn successfully.");
}

void cmd_logo(void) {
    bare_sync_from_fb();

    int32_t img_h = *(int32_t*)(logo_bmp + 22);
    int32_t img_w = *(int32_t*)(logo_bmp + 18);

    int x = (int)fbi->width - img_w;
    if (x < 0) x = 0;

    int y = 16;

    int r = bare_bmp_draw(x, y, logo_bmp, logo_bmp_size);
    if (r != 0) {
        terminal_set_fg(COLOR_ERROR);
        terminal_print("  BMP error: ");
        terminal_print_int(r);
        terminal_println("");
        return;
    }

    bare_flip();
    terminal_set_fg(COLOR_HIGHLIGHT);
}