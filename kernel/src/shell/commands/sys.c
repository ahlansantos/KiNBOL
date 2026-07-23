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
#include "../../kernel/sched.h"
#include "../../mm/pmm.h"
#include "../../mm/heap.h"
#include "../../drivers/keyboard.h"

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
    terminal_println("  ps           process status");
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
    terminal_println("\n  Graphics (BareGL - deprecated)");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  ------------------------------------------");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  clearfb                 clear framebuffer");
    terminal_println("  scale <1-8>             resize terminal font");
    terminal_set_fg(COLOR_WARNING);
    terminal_println("  (pixel, line, rect, circle, drawtest are deprecated)");

    terminal_set_fg(COLOR_HEADER);
    terminal_println("\n  Utilities");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  ------------------------------------------");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  calc <expr>   calculator (hex, +-*/&|^~)");
    terminal_println("  ascii         ASCII table");
    terminal_println("  anim          animation test\n");
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

static void test_task_entry(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        dmesg("[test_task] running\n");
        sched_yield();
    }
    task_exit();
}

void cmd_schedtest(void) {
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_println("  Running Scheduler Test...");
    
    uint64_t free_pages_before = pmm_get_free_page_count();
    uint32_t heap_free_before = kmalloc_free_space();
    
    terminal_set_fg(COLOR_BODY);
    terminal_print("  Before: Pages free: ");
    terminal_print_int((uint32_t)free_pages_before);
    terminal_print(" | Heap free: ");
    terminal_print_int(heap_free_before);
    terminal_println(" bytes");

    task_t *tasks[5];
    for (int i = 0; i < 5; i++) {
        char name[8] = "test0";
        name[4] += i;
        tasks[i] = task_create(name, test_task_entry, NULL);
        if (!tasks[i]) {
            terminal_set_fg(COLOR_ERROR);
            terminal_println("  Failed to create task.");
            return;
        }
    }
    
    // Yield until all tasks finish
    for (int i = 0; i < 5; i++) {
        while (tasks[i]->state != TASK_DEAD) {
            sched_yield();
        }
    }
    
    // Yield a few more times so the idle task gets scheduled and the reaper runs
    for (int i = 0; i < 20; i++) {
        sched_yield();
    }
    
    uint64_t free_pages_after = pmm_get_free_page_count();
    uint32_t heap_free_after = kmalloc_free_space();
    
    terminal_set_fg(COLOR_BODY);
    terminal_print("  After:  Pages free: ");
    terminal_print_int((uint32_t)free_pages_after);
    terminal_print(" | Heap free: ");
    terminal_print_int(heap_free_after);
    terminal_println(" bytes");

    terminal_set_fg(COLOR_SUCCESS);
    if (free_pages_after == free_pages_before && heap_free_after == heap_free_before) {
        terminal_println("  [PASS] Task cleanup");
        terminal_println("  [PASS] No memory leak");
    } else {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  [FAIL] Memory leak detected!");
    }
}

static void sleeptest_task_A(void *arg) {
    (void)arg;
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_println("[A] before sleep");
    
    sleep_ms(1000);
    
    terminal_set_fg(COLOR_SUCCESS);
    terminal_println("[A] after sleep");
    task_exit();
}

static void sleeptest_task_B(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        terminal_set_fg(COLOR_BODY);
        terminal_println("[B] running");
        sleep_ms(200);
    }
    task_exit();
}

static void sleeptest_task_multi(void *arg) {
    uint32_t delay = (uint32_t)(uint64_t)arg;
    sleep_ms(delay);
    terminal_set_fg(COLOR_SUCCESS);
    terminal_print("Task woke up after ");
    terminal_print_int(delay);
    terminal_println("ms");
    task_exit();
}

void cmd_sleeptest(void) {
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_println("  Starting Sleeptest...");
    
    task_t *tA = task_create("sleep_A", sleeptest_task_A, NULL);
    task_t *tB = task_create("sleep_B", sleeptest_task_B, NULL);
    task_t *tm1 = task_create("multi_1", sleeptest_task_multi, (void*)(uint64_t)1000);
    task_t *tm2 = task_create("multi_2", sleeptest_task_multi, (void*)(uint64_t)2000);
    task_t *tm3 = task_create("multi_3", sleeptest_task_multi, (void*)(uint64_t)3000);
    
    if (!tA || !tB || !tm1 || !tm2 || !tm3) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Failed to create tasks.");
        return;
    }
    
    while (tA->state != TASK_DEAD || tB->state != TASK_DEAD || 
           tm1->state != TASK_DEAD || tm2->state != TASK_DEAD || tm3->state != TASK_DEAD) {
        sched_yield();
    }
    
    for (int i=0; i<10; i++) sched_yield();
    
    terminal_set_fg(COLOR_SUCCESS);
    terminal_println("  Sleeptest finished.");
}

void cmd_top(void) {
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_println("  Starting TOP... Press 'q' to exit.");
    sleep_ms(1000);
    
    while (1) {
        keyboard_update();
        if (keyboard_held(0x10)) { // Scancode 0x10 is 'Q'/'q' on PS/2 QWERTY
            break;
        }

        terminal_clear();
        terminal_set_fg(COLOR_HEADER);
        terminal_println("\n  KiNBOL TOP - Process Monitor");
        terminal_set_fg(COLOR_DIM);
        terminal_println("  --------------------------------------------------");
        terminal_set_fg(COLOR_ACCENT);
        terminal_println("  PID    STATE       WAKE (ms)    NAME");
        terminal_set_fg(COLOR_DIM);
        terminal_println("  --------------------------------------------------");
        
        task_t *head = task_get_head();
        if (head) {
            task_t *iter = head;
            do {
                terminal_set_fg(COLOR_BODY);
                terminal_print("  ");
                if (iter->id < 10) terminal_print(" ");
                terminal_print_int(iter->id);
                terminal_print("     ");
                
                if (iter->state == TASK_RUNNING) {
                    terminal_set_fg(COLOR_SUCCESS);
                    terminal_print("RUNNING    ");
                } else if (iter->state == TASK_READY) {
                    terminal_set_fg(COLOR_BODY);
                    terminal_print("READY      ");
                } else if (iter->state == TASK_BLOCKED) {
                    terminal_set_fg(COLOR_WARNING);
                    terminal_print("BLOCKED    ");
                } else if (iter->state == TASK_DEAD) {
                    terminal_set_fg(COLOR_ERROR);
                    terminal_print("DEAD       ");
                }
                
                terminal_set_fg(COLOR_DIM);
                if (iter->wake_time_ms > 0) {
                    terminal_print_int((uint32_t)iter->wake_time_ms);
                } else {
                    terminal_print("-");
                }
                
                int wake_len = 1;
                uint32_t val = (uint32_t)iter->wake_time_ms;
                if (val > 0) {
                    wake_len = 0;
                    while (val > 0) { val /= 10; wake_len++; }
                }
                for (int i = 0; i < 13 - wake_len; i++) terminal_print(" ");
                
                terminal_set_fg(COLOR_HIGHLIGHT);
                terminal_print(iter->name);
                terminal_println("");
                
                iter = iter->next;
            } while (iter && iter != head);
        }
        
        terminal_set_fg(COLOR_DIM);
        terminal_println("  --------------------------------------------------");
        terminal_set_fg(COLOR_BODY);
        terminal_print("  Uptime: ");
        terminal_print_int((uint32_t)(uptime_ms() / 1000));
        terminal_println(" s");
        
        for (int i = 0; i < 10; i++) {
            keyboard_update();
            if (keyboard_held(0x10)) goto exit_top;
            sleep_ms(100);
        }
    }
exit_top:
    terminal_clear();
}