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
#include "../../kernel/fpu.h"

#define MAX_SLEEP_MS 3600000

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0,%1" :: "a"(val), "Nd"(port));
}

#include "../../kernel/usermode.h"

void cmd_usertest(void) {
    task_t *t = usertest_launch();
    if (!t) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Failed to create usertest task.");
        return;
    }
    terminal_set_fg(COLOR_ACCENT);
    terminal_print("  launched usertest, pid ");
    terminal_print_int(t->id);
    terminal_println("");

    while (t->state != TASK_DEAD) {
        sched_yield();
    }
}

void cmd_exec(const char *path) {
    task_t *t = exec_launch(path);
    if (!t) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Failed to create exec task.");
        return;
    }
    terminal_set_fg(COLOR_ACCENT);
    terminal_print("  exec ");
    terminal_print(path);
    terminal_print(", pid ");
    terminal_print_int(t->id);
    terminal_println("");

    while (t->state != TASK_DEAD) {
        sched_yield();
    }
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
    terminal_println("  dmesg --clear  clear kernel log");
    terminal_println("  ps           process status");
    terminal_println("  reboot       reboot system");
    terminal_println("  shutdown     power off (ACPI S5)");
    terminal_println("  crash        trigger kernel panic");
    terminal_println("  fastfetch    system overview");
    terminal_println("  syscalls     list linux abi syscalls");
    terminal_println("  libktest     run libk string tests");

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
    terminal_println("  ls                     list devices and disk files (tree view)");
    terminal_println("  ls -a                  also show hidden/macOS junk files");
    terminal_println("  cat <name>             read file/device");
    terminal_println("  vfswrite <name> <txt>  write file/device");
    terminal_println("  touch <path>           create empty file (any existing dir, e.g. pasta1/x.txt)");
    terminal_println("  mkdir <path>           create directory (any existing dir, e.g. pasta1/sub)");
    terminal_println("  rm <path>              remove a file");
    terminal_println("  rm -r <path>           remove a file or directory recursively");
    terminal_println("  rmdir <path>           remove an empty directory");

    terminal_set_fg(COLOR_HEADER);
    terminal_println("\n  Graphics (GPipe)");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  ------------------------------------------");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  clearfb                 clear framebuffer");
    terminal_println("  gpipe                   show gpipe banner/version");
    terminal_println("  gpipe clearfb           clear framebuffer via gpipe");
    terminal_println("  gpipe drawtest          draw rect/circle/line test");
    terminal_println("  scale <1-8>             resize terminal font");

    terminal_set_fg(COLOR_HEADER);
    terminal_println("\n  Ring 3 / Userspace");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  ------------------------------------------");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  usertest        run built-in ring 3 test blob (syscall demo)");
    terminal_println("  exec <path>     load and run a real ELF64 binary (e.g. exec sda/test.elf)");
    terminal_set_fg(COLOR_HEADER);
    terminal_println("\n  Utilities");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  ------------------------------------------");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  calc <expr>   calculator (hex, +-*/&|^~)");
    terminal_println("  ascii         ASCII table");
    terminal_println("  about-wm      about the initial WM / GPipe (GUI popup)");
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
    terminal_set_fg(COLOR_HEADER);
    terminal_println("\n  Crash Test Suite");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  --------------------------------------------------");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  Usage: crash <type>");
    terminal_println("");
    terminal_set_fg(COLOR_ACCENT);
    terminal_println("  Types:");
    terminal_set_fg(COLOR_BODY);
    terminal_println("    de   Divide Error (#DE)        - integer division by zero");
    terminal_println("    ud   Invalid Opcode (#UD)      - execute ud2 instruction");
    terminal_println("    pf   Page Fault (#PF)          - dereference NULL pointer");
    terminal_println("    gp   General Protection (#GP)  - execute privileged instruction in ring 3");
    terminal_set_fg(COLOR_WARNING);
    terminal_println("\n  WARNING: These will crash the kernel and halt the system.");
    terminal_println("");
}

void cmd_crash_de(void) {
    terminal_set_fg(COLOR_ERROR);
    terminal_println("  Triggering Divide Error (#DE)...");

    asm volatile("xorl %%eax, %%eax; divl %%eax" : : : "eax", "edx");
    terminal_set_fg(COLOR_WARNING);
    terminal_println("  #DE did not fire (unexpected)");
}

void cmd_crash_ud(void) {
    terminal_set_fg(COLOR_ERROR);
    terminal_println("  Triggering Invalid Opcode (#UD)...");
    asm volatile("ud2");
    terminal_set_fg(COLOR_WARNING);
    terminal_println("  #UD did not fire (unexpected)");
}

void cmd_crash_pf(void) {
    terminal_set_fg(COLOR_ERROR);
    terminal_println("  Triggering Page Fault (#PF)...");
    volatile uint64_t *null = (volatile uint64_t *)0;
    (void)*null;
    terminal_set_fg(COLOR_WARNING);
    terminal_println("  #PF did not fire (unexpected)");
}

void cmd_crash_gp(void) {
    terminal_set_fg(COLOR_ERROR);
    terminal_println("  Triggering General Protection (#GP)...");

    asm volatile("movl $0xDEAD, %%ecx; xorl %%eax, %%eax; xorl %%edx, %%edx; wrmsr"
                 : : : "eax", "ecx", "edx");
    terminal_set_fg(COLOR_WARNING);
    terminal_println("  #GP did not fire (unexpected)");
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

void cmd_dmesg_clear(void) {
    dmesg_clear();
    terminal_set_fg(COLOR_SUCCESS);
    terminal_println("  Kernel log cleared.");
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

    for (int i = 0; i < 5; i++) {
        while (tasks[i]->state != TASK_DEAD) {
            sched_yield();
        }
    }

    for (int i = 0; i < 20; i++) {
        sched_yield();
    }
    task_reaper();

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
    uint64_t f = terminal_lock();
    terminal_set_fg_nolock(COLOR_SUCCESS);
    terminal_print_nolock("Task woke up after ");
    terminal_print_int_nolock(delay);
    terminal_println_nolock("ms");
    terminal_unlock(f);
    task_exit();
}

static void fpu_test_task_a(void *arg) {
    (void)arg;
    double x = 1.0;
    for (int i = 0; i < 5; i++) {
        x = x * 1.5 + 0.25;
        uint64_t f = terminal_lock();
        terminal_set_fg_nolock(COLOR_BODY);
        terminal_print_nolock("[A] fpu val = ");
        terminal_print_int_nolock((int)(x * 1000));
        terminal_println_nolock("");
        terminal_unlock(f);
        sleep_ms(50);
    }
    uint64_t f = terminal_lock();
    terminal_set_fg_nolock(COLOR_SUCCESS);
    terminal_println_nolock("[A] fpu test done");
    terminal_unlock(f);
    task_exit();
}

static void fpu_test_task_b(void *arg) {
    (void)arg;
    double y = 100.0;
    for (int i = 0; i < 5; i++) {
        y = y / 3.0 - 1.0;
        int val = (int)(y * 1000);
        uint64_t f = terminal_lock();
        terminal_set_fg_nolock(COLOR_HIGHLIGHT);
        terminal_print_nolock("[B] fpu val = ");
        if (val < 0) {
            terminal_print_nolock("-");
            val = -val;
        }
        terminal_print_int_nolock((uint32_t)val);
        terminal_println_nolock("");
        terminal_unlock(f);
        sleep_ms(50);
    }
    uint64_t f = terminal_lock();
    terminal_set_fg_nolock(COLOR_SUCCESS);
    terminal_println_nolock("[B] fpu test done");
    terminal_unlock(f);
    task_exit();
}

void cmd_fpu(void) {
    dmesg("[fpu] Starting FPU isolation test...\n");

    // Spawn two tasks that will each corrupt the FPU state differently
    task_t *t1 = task_create("fpu_test_A", fpu_test_task_a, NULL);
    task_t *t2 = task_create("fpu_test_B", fpu_test_task_b, NULL);

    if (!t1 || !t2) {
        dmesg("[fpu] Failed to create tasks.\n");
        if (t1) task_exit();
        if (t2) task_exit();
        return;
    }

    // Wait for both tasks to finish
    while (t1->state != TASK_DEAD || t2->state != TASK_DEAD) {
        sched_yield();
    }

    // Reaper cleans up
    task_reaper();

    dmesg("[fpu] Test finished.\n");
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
        if (keyboard_held(0x10)) {
            break;
        }

        terminal_clear();
        terminal_set_fg(COLOR_HEADER);
        terminal_println("\n  KiNBOL TOP - Process Monitor");
        terminal_set_fg(COLOR_DIM);
        terminal_println("  --------------------------------------------------");
        terminal_set_fg(COLOR_ACCENT);
        terminal_println("  PID    STATE       WAKE-IN      NAME");
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
                uint64_t now = uptime_ms();
                uint32_t remaining = 0;
                bool has_wake = (iter->state == TASK_BLOCKED && iter->wake_time_ms > 0);
                if (has_wake) {
                    remaining = (uint32_t)(iter->wake_time_ms > now ? iter->wake_time_ms - now : 0);
                    terminal_print_int(remaining);
                    terminal_print("ms");
                } else {
                    terminal_print("-");
                }

                int wake_len = 1;
                if (has_wake) {
                    uint32_t val = remaining;
                    wake_len = 2;
                    if (val == 0) wake_len = 3;
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

        for (int i = 0; i < 5; i++) {
            keyboard_update();
            if (keyboard_held(0x10)) goto exit_top;
            sleep_ms(60);
        }
    }
exit_top:
    terminal_clear();
}

void cmd_syscalls(void) {
    terminal_set_fg(COLOR_ACCENT);
    terminal_println("\n  KiNBOL System Calls (Linux x86_64 ABI)");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  ------------------------------------------");
    terminal_set_fg(COLOR_BODY);
    
    terminal_println("  %rax | Name       | Status      | Description");
    terminal_println("  -------------------------------------------------------------");
    
    terminal_set_fg(COLOR_SUCCESS); terminal_print("     0 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_READ   | Implemented | read(fd, buf, len) via fd table");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("     1 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_WRITE  | Implemented | write to terminal");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("     2 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_OPEN   | Implemented | open file, returns fd");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("     3 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_CLOSE  | Implemented | close fd");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("     4 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_STAT   | Implemented | file stats (best-effort layout)");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("     5 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_FSTAT  | Implemented | fd stats (best-effort layout)");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("     8 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_LSEEK  | Implemented | seek file (SET/CUR/END)");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("     9 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_MMAP   | Implemented | map memory (anonymous, ASLR'd base)");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("    10 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_MPROTECT| Implemented | protect memory");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("    11 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_MUNMAP | Implemented | unmap memory");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("    12 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_BRK    | Implemented | change heap size");
    terminal_set_fg(COLOR_WARNING); terminal_print("    16 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_IOCTL  | Stub (ENOTTY)| device control");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("    24 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_YIELD  | Implemented | sched_yield");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("    35 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_SLEEP  | Implemented | nanosleep");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("    60 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_EXIT   | Implemented | exit current task");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("   158 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_ARCH_PRCTL| Basic    | set TLS (FS.base)");
    terminal_set_fg(COLOR_SUCCESS); terminal_print("   218 "); terminal_set_fg(COLOR_BODY); terminal_println("| SYS_SET_TID_ADDRESS| Stub| set tid address");
    
    terminal_println("");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  Registers:");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  - %rax : Syscall number & Return value");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  - %rdi, %rsi, %rdx, %r10, %r8, %r9 : Arguments");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  - %rcx, %r11 : Clobbered by syscall instruction");
    terminal_println("");
}