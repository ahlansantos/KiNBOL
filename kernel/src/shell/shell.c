/*
 * The KiNBOL interactive shell. Reads a line at a time through
 * keyboard_readline, does simple parsing into a command plus arguments,
 * and dispatches to the registered command table (calc, fs, gfx, info,
 * mem, sys, util).
 */
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

static int parse_int(const char **p) {
    int val = 0, neg = 0;
    while (**p == ' ') (*p)++;
    if (**p == '-') { neg = 1; (*p)++; }
    else if (**p == '+') (*p)++;
    while (**p >= '0' && **p <= '9')
        val = val * 10 + (*(*p)++ - '0');
    return neg ? -val : val;
}

static const char *CMD_LIST[] = {
    "help", "clear", "uname", "echo ", "sleep ", "date", "ticks",
    "crash", "fastfetch", "memtest", "reboot", "shutdown", "anim",
    "dmesg", "meminfo", "vminfo",
    "hexdump ", "peek ", "poke ",
    "ramls", "ramcat ", "ramwrite ", "ramdel ", "raminfo",
    "vfsls", "vfsread ", "vfswrite ",
    "drawtest", "clearfb", "baregl status",
    "pixel ", "line ", "rect ", "fillrect ", "circle ",
    "calc ", "ascii",
    NULL
};

static int tab_complete(char *buf, int len) {
    if (len == 0) return -1;

    const char *matches[64];
    int         nmatch = 0;

    for (int i = 0; CMD_LIST[i]; i++) {
        if (sh_startswith(CMD_LIST[i], buf)) {  
            int ok = 1;
            for (int j = 0; j < len; j++) {
                if (CMD_LIST[i][j] != buf[j]) { ok = 0; break; }
            }
            if (ok && nmatch < 64) matches[nmatch++] = CMD_LIST[i];
        }
    }

    if (nmatch == 0) return -1; 

    if (nmatch == 1) {
        int i = 0;
        while (matches[0][i]) { buf[i] = matches[0][i]; i++; }
        buf[i] = '\0';
        return i;
    }

    int common = len;
    while (1) {
        char c = matches[0][common];
        if (!c) break;
        int same = 1;
        for (int i = 1; i < nmatch; i++)
            if (matches[i][common] != c) { same = 0; break; }
        if (!same) break;
        common++;
    }

    terminal_println("");
    terminal_set_fg(COLOR_DIM);
    for (int i = 0; i < nmatch; i++) {
        terminal_print("  ");
        terminal_println(matches[i]);
    }

    terminal_set_fg(COLOR_PROMPT);
    terminal_print("[kernel@KiNBOL] ~ $ ");
    terminal_set_fg(COLOR_CMD);
    for (int i = 0; i < common; i++) { buf[i] = matches[0][i]; terminal_putchar(buf[i]); }
    buf[common] = '\0';
    return common;
}


static void shell_readline(char *buf, int max) {
    extern uint8_t inb_kb(void);  

    static const char lo[] = {
        0,0,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
        'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
        'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
        'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
    };
    static const char up[] = {
        0,0,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
        'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
        'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
        'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
    };

    int len = 0, shift = 0, extended = 0;
    buf[0] = '\0';

    while (len < max - 1) {
        uint8_t status;
        do { asm volatile("inb $0x64, %0" : "=a"(status)); } while (!(status & 1));
        uint8_t sc;
        asm volatile("inb $0x60, %0" : "=a"(sc));

        if (sc == 0xE0) { extended = 1; continue; }
        if (extended)   { extended = 0; continue; } 

        if (sc & 0x80) {
            uint8_t rel = sc & 0x7F;
            if (rel == 0x2A || rel == 0x36) shift = 0;
            continue;
        }

        if (sc == 0x2A || sc == 0x36) { shift = 1; continue; }
        if (sc == 0x3A) { /* caps lock - ignore */ continue; }

        if (sc == 0x0F) { 
            int newlen = tab_complete(buf, len);
            if (newlen > len) {
                terminal_set_fg(COLOR_CMD);
                for (int i = len; i < newlen; i++) terminal_putchar(buf[i]);
                len = newlen;
            }
            continue;
        }

        if (sc == 0x1C) {
            buf[len] = '\0';
            terminal_println("");
            return;
        }

        if (sc == 0x0E) {
            if (len > 0) {
                len--;
                buf[len] = '\0';
                terminal_putchar('\b');
            }
            continue;
        }

        if (sc < sizeof(lo)) {
            char c = shift ? up[sc] : lo[sc];
            if (c && c != '\t' && c != '\b') {
                buf[len++] = c;
                buf[len]   = '\0';
                terminal_set_fg(COLOR_CMD);
                terminal_putchar(c);
            }
        }
    }
    buf[len] = '\0';
    terminal_println("");
}

void shell_run(void) {
    char in[256];

    while (1) {
        terminal_set_fg(COLOR_PROMPT);
        terminal_print("[kernel@KiNBOL] ~ $ ");
        terminal_set_fg(COLOR_CMD);

        shell_readline(in, 256);

        terminal_set_fg(COLOR_BODY);

        if      (!sh_strcmp(in, "help"))      cmd_help();
        else if (!sh_strcmp(in, "clear"))     terminal_clear();
        else if (!sh_strcmp(in, "uname")) {
            terminal_set_fg(COLOR_SUCCESS);
            terminal_println("  KiNBOL 0.07 x86_64-uefi Limine");
        }
        else if (sh_startswith(in, "echo ")) {
            terminal_set_fg(COLOR_SUCCESS);
            terminal_print("  "); terminal_println(in + 5);
        }
        else if (!sh_strcmp(in, "ticks"))       cmd_ticks();
        else if (sh_startswith(in, "sleep "))   cmd_sleep(in + 6);
        else if (!sh_strcmp(in, "crash"))       cmd_crash();
        else if (!sh_strcmp(in, "fastfetch"))   cmd_fastfetch();
        else if (!sh_strcmp(in, "memtest"))     cmd_memtest();
        else if (!sh_strcmp(in, "reboot"))      cmd_reboot();
        else if (!sh_strcmp(in, "shutdown"))    cmd_shutdown();
        else if (!sh_strcmp(in, "anim"))        cmd_anim();
        else if (!sh_strcmp(in, "date"))        cmd_date();
        else if (!sh_strcmp(in, "meminfo"))     cmd_meminfo();
        else if (!sh_strcmp(in, "ascii"))       cmd_ascii();
        else if (!sh_strcmp(in, "dmesg"))       cmd_dmesg();
        else if (!sh_strcmp(in, "drawtest"))    cmd_drawtest();
        else if (!sh_strcmp(in, "clearfb"))     cmd_clearfb();
        else if (!sh_strcmp(in, "vfsls"))       cmd_vfsls();
        else if (!sh_strcmp(in, "vminfo"))      cmd_vminfo();
        else if (!sh_strcmp(in, "ramls"))       cmd_ramls();
        else if (!sh_strcmp(in, "raminfo"))     cmd_raminfo();
        else if (!sh_strcmp(in, "baregl status")) cmd_baregl_status();
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
        else if (sh_startswith(in, "pixel ")) {
            const char *p = in + 6;
            int x = parse_int(&p), y = parse_int(&p);
            cmd_pixel(x, y);
        }
        else if (sh_startswith(in, "line ")) {
            const char *p = in + 5;
            int x0=parse_int(&p),y0=parse_int(&p),x1=parse_int(&p),y1=parse_int(&p);
            cmd_line(x0, y0, x1, y1);
        }
        else if (sh_startswith(in, "rect ")) {
            const char *p = in + 5;
            int x=parse_int(&p),y=parse_int(&p),w=parse_int(&p),h=parse_int(&p);
            cmd_rect(x, y, w, h);
        }
        else if (sh_startswith(in, "fillrect ")) {
            const char *p = in + 9;
            int x=parse_int(&p),y=parse_int(&p),w=parse_int(&p),h=parse_int(&p);
            cmd_fillrect(x, y, w, h);
        }
        else if (sh_startswith(in, "circle ")) {
            const char *p = in + 7;
            int x=parse_int(&p),y=parse_int(&p),r=parse_int(&p);
            cmd_circle(x, y, r);
        }
        else if (in[0]) {
            terminal_set_fg(COLOR_ERROR);
            terminal_print("  command not found: ");
            terminal_println(in);
            terminal_set_fg(COLOR_DIM);
            terminal_println("  Try 'help' or press Tab.");
        }
    }
}