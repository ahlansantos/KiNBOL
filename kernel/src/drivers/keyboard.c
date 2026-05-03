#include <stdint.h>
#include "keyboard.h"
extern void terminal_putchar(char c);

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline uint64_t kb_rdtsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void (*cursor_cb)(int visible) = 0;
void keyboard_set_cursor_cb(void (*cb)(int visible)) { cursor_cb = cb; }

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

#define HIST_MAX   16
#define HIST_LEN  256

static char history[HIST_MAX][HIST_LEN];
static int  hist_count = 0;
static int  hist_head  = 0;

static void hist_add(const char *buf) {
    if (buf[0] == '\0') return;
    int slot = hist_head % HIST_MAX;
    int i = 0;
    while (buf[i] && i < HIST_LEN - 1) { history[slot][i] = buf[i]; i++; }
    history[slot][i] = '\0';
    hist_head++;
    if (hist_count < HIST_MAX) hist_count++;
}

void keyboard_readline(char *buf, int max) {
    int i = 0, shift = 0, caps = 0;
    int hist_pos = hist_head;
    uint64_t blink_period = 500000000ULL;
    uint64_t last_blink   = kb_rdtsc();
    int      cursor_vis   = 1;
    int      extended     = 0;

    if (cursor_cb) cursor_cb(1);

    while (i < max - 1) {
        if (!(inb(0x64) & 1)) {
            uint64_t now = kb_rdtsc();
            if (cursor_cb && (now - last_blink) >= blink_period) {
                cursor_vis = !cursor_vis;
                cursor_cb(cursor_vis);
                last_blink = now;
            }
            asm volatile("pause");
            continue;
        }
        if (cursor_cb) cursor_cb(0);
        uint8_t sc = inb(0x60);

        if (sc == 0xE0) { extended = 1; goto next; }

        if (extended) {
            extended = 0;
            if (sc == 0x48) {
                int target = hist_pos - 1;
                if (target < hist_head - hist_count) goto next;
                hist_pos = target;
                int slot = hist_pos % HIST_MAX;
                while (i > 0) { i--; terminal_putchar('\b'); }
                int j = 0;
                while (history[slot][j] && j < max - 1) {
                    buf[j] = history[slot][j];
                    terminal_putchar(buf[j]);
                    j++;
                }
                i = j;
                goto next;
            }
            if (sc == 0x50) {
                int target = hist_pos + 1;
                if (target > hist_head) goto next;
                hist_pos = target;
                while (i > 0) { i--; terminal_putchar('\b'); }
                if (hist_pos == hist_head) {
                    i = 0;
                    goto next;
                }
                int slot = hist_pos % HIST_MAX;
                int j = 0;
                while (history[slot][j] && j < max - 1) {
                    buf[j] = history[slot][j];
                    terminal_putchar(buf[j]);
                    j++;
                }
                i = j;
                goto next;
            }
            goto next;
        }

        if (sc == 0x2A || sc == 0x36) { shift = 1; goto next; }
        if (sc == 0xAA || sc == 0xB6) { shift = 0; goto next; }
        if (sc == 0x3A) { caps = !caps; goto next; }
        if (sc & 0x80) goto next;

        {
            char c = (shift ^ caps) ? up[sc] : lo[sc];
            if (!c) goto next;
            if (c == '\n') {
                terminal_putchar('\n');
                break;
            }
            if (c == '\b') {
                if (i > 0) { i--; terminal_putchar('\b'); }
                goto next;
            }
            buf[i++] = c;
            terminal_putchar(c);
        }

    next:
        if (cursor_cb) {
            cursor_vis = 1;
            cursor_cb(1);
            last_blink = kb_rdtsc();
        }
    }

    buf[i] = '\0';
    hist_add(buf);
    if (cursor_cb) cursor_cb(0);
}
uint8_t keyboard_peek(void) {
    if (!(inb(0x64) & 1)) return 0;
    return inb(0x60);
}