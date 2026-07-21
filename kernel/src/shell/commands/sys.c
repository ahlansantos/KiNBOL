/*
 * System-level shell commands, most likely reboot, poweroff, and uptime,
 * wired straight into the corresponding kernel functions.
 */
#include "../commands.h"
#include "util.h"
#include "../../graphics/terminal.h"
#include "../../drivers/rtc.h"
#include "../../kernel/dmesg.h"
#include "../../kernel/pit.h"
#include "../../kernel/acpi.h"

#define MAX_SLEEP_MS 3600000

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0,%1" :: "a"(val), "Nd"(port));
}

void cmd_help(void) {
    terminal_set_fg(COLOR_ACCENT);
    terminal_println("\n  +-------------------------------------------+");
    terminal_println(  "  |        KiNBOL  -  Command Reference      |");
    terminal_println(  "  +-------------------------------------------+\n");

    terminal_set_fg(COLOR_HEADER);
    terminal_println("  System");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  ------------------------------------------");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  help         display this reference");
    terminal_println("  clear        clear terminal screen");
    terminal_println("  uname        system information");
    terminal_println("  echo <txt>   print text");
    terminal_println("  sleep <ms>   pause execution");
    terminal_println("  date         current date / time");
    terminal_println("  ticks        uptime ticks");
    terminal_println("  dmesg        kernel log");
    terminal_println("  reboot       reboot system");
    terminal_println("  shutdown     power off (ACPI S5)");
    terminal_println("  crash        trigger kernel panic");
    terminal_println("  fastfetch    system overview");

    terminal_set_fg(COLOR_HEADER);
    terminal_println("\n  Memory & Hardware");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  ------------------------------------------");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  meminfo           memory statistics");
    terminal_println("  memtest           allocation test");
    terminal_println("  vminfo            virtual memory info");
    terminal_println("  hexdump <a> <l>   hex dump memory");
    terminal_println("  peek <addr>       read memory");
    terminal_println("  poke <a> <v>      write memory");

    terminal_set_fg(COLOR_HEADER);
    terminal_println("\n  File System");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  ------------------------------------------");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  ramls                  list ramdisk");
    terminal_println("  ramcat <f>             show file");
    terminal_println("  ramwrite <f> <txt>     write file");
    terminal_println("  ramdel <f>             delete file");
    terminal_println("  raminfo                ramdisk stats");
    terminal_println("  vfsls                  list /dev nodes");
    terminal_println("  vfsread <dev>          read device");
    terminal_println("  vfswrite <dev> <txt>   write device");

    terminal_set_fg(COLOR_HEADER);
    terminal_println("\n  Graphics (BareGL)");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  ------------------------------------------");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  drawtest                graphics test");
    terminal_println("  pixel <x> <y>           draw pixel");
    terminal_println("  line <x0> <y0> <x1> <y1>");
    terminal_println("  rect <x> <y> <w> <h>");
    terminal_println("  fillrect <x> <y> <w> <h>");
    terminal_println("  circle <x> <y> <r>");
    terminal_println("  clearfb                 clear framebuffer");

    terminal_set_fg(COLOR_HEADER);
    terminal_println("\n  Utilities");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  ------------------------------------------");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  calc <expr>   calculator (hex, +-*/&|^~)");
    terminal_println("  ascii         ASCII table");
    terminal_println("  anim          animation test\n");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  Tip: press Tab to auto-complete commands.\n");
}

void cmd_ticks(void) {
    terminal_set_fg(COLOR_ACCENT);
    terminal_print("  Uptime  ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(get_ticks());
    terminal_set_fg(COLOR_BODY);
    terminal_print(" ticks  /  ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int((uint32_t)(uptime_ms() / 1000));
    terminal_set_fg(COLOR_BODY);
    terminal_print(" s  /  IRQ0: ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int((uint32_t)pit_get_ticks());
    terminal_println("");
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
        terminal_println("  Error: usage: sleep <ms>");
        return;
    }
    if (ms > MAX_SLEEP_MS) ms = MAX_SLEEP_MS;
    terminal_set_fg(COLOR_BODY);
    terminal_print("  Sleeping ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int((uint32_t)ms);
    terminal_set_fg(COLOR_BODY);
    terminal_println(" ms...");
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
    terminal_println("  Rebooting...");
    outb(0x64, 0xFE);
}

void cmd_shutdown(void) {
    terminal_set_fg(COLOR_WARNING);
    terminal_println("  Powering off via ACPI S5...");
    acpi_poweroff();
    terminal_set_fg(COLOR_ERROR);
    terminal_println("  ACPI poweroff failed.");
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
    print_header("Date & Time");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print("\n  ");
    if (t.day    < 10) terminal_putchar('0'); terminal_print_int(t.day);
    terminal_putchar('/');
    if (t.month  < 10) terminal_putchar('0'); terminal_print_int(t.month);
    terminal_putchar('/');
    terminal_print_int(t.year);
    terminal_print("  -  ");
    if (t.hour   < 10) terminal_putchar('0'); terminal_print_int(t.hour);
    terminal_putchar(':');
    if (t.minute < 10) terminal_putchar('0'); terminal_print_int(t.minute);
    terminal_putchar(':');
    if (t.second < 10) terminal_putchar('0'); terminal_print_int(t.second);
    terminal_println("\n");
}

void cmd_ascii(void) {
    print_header("ASCII Table");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  -----+--------------------------------");
    terminal_set_fg(COLOR_ACCENT);
    terminal_println("   Dec |  Hex   Char");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  -----+--------------------------------");
    for (int i = 0; i < 128; i++) {
        terminal_set_fg(COLOR_BODY);
        terminal_print("  ");
        if (i < 100) terminal_putchar(' ');
        if (i < 10)  terminal_putchar(' ');
        terminal_print_int(i);
        terminal_set_fg(COLOR_DIM);
        terminal_print("  |  ");
        terminal_set_fg(COLOR_ACCENT);
        char hex[] = "0123456789ABCDEF";
        terminal_putchar(hex[i >> 4]);
        terminal_putchar(hex[i & 0xF]);
        terminal_print("    ");
        terminal_set_fg((i < 32 || i == 127) ? COLOR_DIM : COLOR_HIGHLIGHT);
        if (i < 32 || i == 127) terminal_println(".");
        else { terminal_putchar((char)i); terminal_println(""); }
    }
    terminal_println("");
}

void cmd_dmesg(void) {
    print_header("Kernel Log");
    terminal_set_fg(COLOR_HIGHLIGHT);
    dmesg_foreach(terminal_putchar);
    terminal_set_fg(COLOR_DIM);
    terminal_print("\n  --- ");
    terminal_print_int(dmesg_len());
    terminal_println(" bytes ---\n");
}