#include "../commands.h"
#include "util.h"
#include "../../graphics/terminal.h"
#include "../../fs/vfs.h"
#include "../../mm/pmm.h"
#include "../../kernel/pit.h"
#include <limine.h>

extern struct limine_framebuffer *fbi;
extern uint64_t tsc_hz;

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