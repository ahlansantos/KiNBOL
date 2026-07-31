#include "shell.h"
#include "commands.h"
#include "../graphics/terminal.h"
#include "../drivers/keyboard.h"
#include "commands/util.h"
#include <stddef.h>

static int sh_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

static int sh_startswith(const char *s, const char *p) {
    while (*p) if (*s++ != *p++) return 0;
    return 1;
}

void shell_run(void) {
    char in[256];

    while (1) {
        terminal_set_fg(COLOR_PROMPT);
        terminal_print("[kernel@KiNBOL] ~ $ ");
        terminal_set_fg(COLOR_CMD);

        keyboard_readline(in, 256);

        terminal_set_fg(COLOR_BODY);

        if      (!sh_strcmp(in, "help"))      cmd_help();
        else if (!sh_strcmp(in, "clear"))     terminal_clear();
        else if (!sh_strcmp(in, "uname")) {
            terminal_set_fg(0x88CC88);
            terminal_println("  KiNBOL 0.08 x86_64-uefi Limine");
            terminal_set_fg(0xAAAAAA);
        }
        else if (sh_startswith(in, "echo ")) {
            terminal_set_fg(COLOR_SUCCESS);
            terminal_print("  "); terminal_println(in + 5);
        }
        else if (!sh_strcmp(in, "ticks"))       cmd_ticks();
        else if (sh_startswith(in, "sleep "))   cmd_sleep(in + 6);
        else if (sh_startswith(in, "crash ")) {
            const char *type = in + 6;
            while (*type == ' ') type++;
            if      (!sh_strcmp(type, "de"))  cmd_crash_de();
            else if (!sh_strcmp(type, "ud"))  cmd_crash_ud();
            else if (!sh_strcmp(type, "pf"))  cmd_crash_pf();
            else if (!sh_strcmp(type, "gp"))  cmd_crash_gp();
            else cmd_crash();
        }
        else if (!sh_strcmp(in, "crash"))       cmd_crash();
        else if (!sh_strcmp(in, "fastfetch"))   cmd_fastfetch();
        else if (!sh_strcmp(in, "syscalls"))    cmd_syscalls();
        else if (!sh_strcmp(in, "memtest"))     cmd_memtest();
        else if (!sh_strcmp(in, "reboot"))      cmd_reboot();
        else if (!sh_strcmp(in, "shutdown"))    cmd_shutdown();
        else if (!sh_strcmp(in, "anim"))        cmd_anim();
        else if (!sh_strcmp(in, "date"))        cmd_date();
        else if (!sh_strcmp(in, "meminfo"))     cmd_meminfo();
        else if (!sh_strcmp(in, "ascii"))       cmd_ascii();
        else if (!sh_strcmp(in, "dmesg"))       cmd_dmesg();
        else if (sh_startswith(in, "dmesg ")) {
            const char *arg = in + 6;
            while (*arg == ' ') arg++;
            if (!sh_strcmp(arg, "--clear") || !sh_strcmp(arg, "clear"))
                cmd_dmesg_clear();
            else
                cmd_dmesg();
        }
        else if (!sh_strcmp(in, "ps"))          cmd_ps();
        else if (!sh_strcmp(in, "top"))         cmd_top();
        else if (!sh_strcmp(in, "schedtest"))   cmd_schedtest();
        else if (!sh_strcmp(in, "sleeptest"))   cmd_sleeptest();
        else if (!sh_strcmp(in, "clearfb"))     cmd_clearfb();
        else if (!sh_strcmp(in, "gpipe"))        cmd_gpipe("");
        else if (!sh_strcmp(in, "usertest"))     cmd_usertest();
        else if (!sh_strcmp(in, "mstat"))        cmd_mstat();
        else if (sh_startswith(in, "gpipe "))    cmd_gpipe(in + 6);
        else if (!sh_strcmp(in, "vfsls"))       cmd_vfsls();
        else if (!sh_strcmp(in, "vminfo"))      cmd_vminfo();
        else if (!sh_strcmp(in, "ramls"))       cmd_ramls();
        else if (!sh_strcmp(in, "raminfo"))     cmd_raminfo();
        else if (sh_startswith(in, "calc ")) {
            if (in[5]) cmd_calc(in + 5);
            else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: calc <expr>"); }
        }
        else if (sh_startswith(in, "hexdump ")) {
            char *p = in + 8; while (*p == ' ') p++;
            char *q = p; while (*q && *q != ' ') q++;
            if (*q) { *q = 0; cmd_hexdump(p, q + 1); }
            else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: hexdump <addr> <len>"); }
        }
        else if (sh_startswith(in, "peek ")) {
            if (in[5]) cmd_peek(in + 5);
            else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: peek <addr>"); }
        }
        else if (sh_startswith(in, "poke ")) {
            char *p = in + 5; while (*p == ' ') p++;
            char *q = p; while (*q && *q != ' ') q++;
            if (*q) { *q = 0; cmd_poke(p, q + 1); }
            else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: poke <addr> <val>"); }
        }
        else if (sh_startswith(in, "vfsread ")) {
            char *d = in + 8; while (*d == ' ') d++;
            if (*d) cmd_vfsread(d);
            else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: vfsread <dev>"); }
        }
        else if (sh_startswith(in, "vfswrite ")) {
            char *p = in + 9; while (*p == ' ') p++;
            char *q = p; while (*q && *q != ' ') q++;
            if (*q) { *q = 0; cmd_vfswrite(p, q + 1); }
            else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: vfswrite <dev> <data>"); }
        }
        else if (sh_startswith(in, "ramcat "))  { char *n = in+7;  if (*n) cmd_ramcat(n);  else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: ramcat <file>"); } }
        else if (sh_startswith(in, "ramwrite ")) {
            char *p = in + 9; char *q = p;
            while (*q && *q != ' ') q++;
            if (*q) { *q = 0; cmd_ramwrite(p, q+1); }
            else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: ramwrite <file> <content>"); }
        }
        else if (sh_startswith(in, "ramdel "))  { char *n = in+7;  if (*n) cmd_ramdel(n);  else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: ramdel <file>"); } }
        else if (sh_startswith(in, "scale ")) {
            cmd_scale(in + 6);
        }
        else if (in[0]) {
            terminal_set_fg(COLOR_ERROR);
            terminal_print("  command not found: ");
            terminal_println(in);
            terminal_set_fg(COLOR_DIM);
            terminal_println("  Try 'help'.");
        }
    }
}