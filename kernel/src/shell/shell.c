#include "shell.h"
#include "commands.h"
#include "../graphics/terminal.h"
#include "../drivers/keyboard.h"
#include "commands/util.h"
#include <stddef.h>
#include <libk/string.h>



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

        if      (!strcmp(in, "help"))      cmd_help();
        else if (!strcmp(in, "clear"))     terminal_clear();
        else if (!strcmp(in, "uname")) {
            terminal_set_fg(0x64D2FF);
            terminal_println("  KiNBOL 0.08 x86_64-uefi Limine");
            terminal_set_fg(COLOR_DIM);
        }
        else if (sh_startswith(in, "echo ")) {
            terminal_set_fg(COLOR_SUCCESS);
            terminal_print("  "); terminal_println(in + 5);
        }
        else if (!strcmp(in, "ticks"))       cmd_ticks();
        else if (sh_startswith(in, "sleep "))   cmd_sleep(in + 6);
        else if (sh_startswith(in, "crash ")) {
            const char *type = in + 6;
            while (*type == ' ') type++;
            if      (!strcmp(type, "de"))  cmd_crash_de();
            else if (!strcmp(type, "ud"))  cmd_crash_ud();
            else if (!strcmp(type, "pf"))  cmd_crash_pf();
            else if (!strcmp(type, "gp"))  cmd_crash_gp();
            else cmd_crash();
        }
        else if (!strcmp(in, "crash"))       cmd_crash();
        else if (!strcmp(in, "fastfetch"))   cmd_fastfetch();
        else if (!strcmp(in, "syscalls"))    cmd_syscalls();
        else if (!strcmp(in, "libktest")) {
            extern void libk_test(void);
            libk_test();
        }
        else if (!strcmp(in, "memtest"))     cmd_memtest();
        else if (!strcmp(in, "reboot"))      cmd_reboot();
        else if (!strcmp(in, "shutdown"))    cmd_shutdown();
        else if (!strcmp(in, "anim"))        cmd_anim();
        else if (!strcmp(in, "date"))        cmd_date();
        else if (!strcmp(in, "meminfo"))     cmd_meminfo();
        else if (!strcmp(in, "ascii"))       cmd_ascii();
        else if (!strcmp(in, "dmesg"))       cmd_dmesg();
        else if (sh_startswith(in, "dmesg ")) {
            const char *arg = in + 6;
            while (*arg == ' ') arg++;
            if (!strcmp(arg, "--clear") || !strcmp(arg, "clear"))
                cmd_dmesg_clear();
            else
                cmd_dmesg();
        }
        else if (!strcmp(in, "ps"))          cmd_ps();
        else if (!strcmp(in, "top"))         cmd_top();
        else if (!strcmp(in, "schedtest"))   cmd_schedtest();
        else if (!strcmp(in, "sleeptest"))   cmd_sleeptest();
        else if (!strcmp(in, "clearfb"))     cmd_clearfb();
        else if (!strcmp(in, "gpipe"))        cmd_gpipe("");
        else if (!strcmp(in, "usertest"))     cmd_usertest();
        else if (sh_startswith(in, "exec ")) {
            char *d = in + 5; while (*d == ' ') d++;
            if (*d) cmd_exec(d);
            else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: exec <path>"); }
        }
        else if (!strcmp(in, "mstat"))        cmd_mstat();
        else if (!strcmp(in, "about-wm"))     cmd_about();
        else if (sh_startswith(in, "gpipe "))    cmd_gpipe(in + 6);
        else if (!strcmp(in, "ls"))          cmd_vfsls();
        else if (!strcmp(in, "ls -a"))       cmd_vfsls_ex(1);
        else if (!strcmp(in, "vminfo"))      cmd_vminfo();
        else if (!strcmp(in, "ahcitest")) {
            extern void cmd_ahcitest(void);
            cmd_ahcitest();
        }
        else if (!strcmp(in, "ramls"))       cmd_ramls();
        else if (!strcmp(in, "raminfo"))     cmd_raminfo();
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
        else if (sh_startswith(in, "cat ")) {
            char *d = in + 4; while (*d == ' ') d++;
            if (*d) cmd_vfsread(d);
            else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: cat <dev>"); }
        }
        else if (sh_startswith(in, "touch ")) {
            char *d = in + 6; while (*d == ' ') d++;
            if (*d) cmd_touch(d);
            else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: touch <file>"); }
        }
        else if (sh_startswith(in, "mkdir ")) {
            char *d = in + 6; while (*d == ' ') d++;
            if (*d) cmd_mkdir(d);
            else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: mkdir <dir>"); }
        }
        else if (sh_startswith(in, "rm -r ")) {
            char *d = in + 6; while (*d == ' ') d++;
            if (*d) cmd_rm(d, 1);
            else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: rm -r <path>"); }
        }
        else if (sh_startswith(in, "rm ")) {
            char *d = in + 3; while (*d == ' ') d++;
            if (*d) cmd_rm(d, 0);
            else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: rm <path> (or rm -r <path>)"); }
        }
        else if (sh_startswith(in, "rmdir ")) {
            char *d = in + 6; while (*d == ' ') d++;
            if (*d) cmd_rmdir(d);
            else { terminal_set_fg(COLOR_ERROR); terminal_println("  Usage: rmdir <path>"); }
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
        else if (sh_startswith(in, "ls ") && strcmp(in, "ls -a")) {
            terminal_set_fg(COLOR_WARNING);
            terminal_println("  ls takes no arguments, just type 'ls' (or 'ls -a' to show hidden files)");
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