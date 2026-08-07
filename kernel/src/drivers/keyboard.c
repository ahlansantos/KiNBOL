#include <stdint.h>
#include "keyboard.h"
#include "../kernel/pit.h"
#include "../kernel/sched.h"
#include "../kernel/idt.h"
#include "../kernel/ioapic.h"
#include "../kernel/lapic.h"
#include "../kernel/dmesg.h"
extern void terminal_putchar(char c);

#define KEYBOARD_VECTOR 33
#define KEYBOARD_IRQ    1

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
static int (*completion_cb)(char *buf, int max) = 0;
void keyboard_set_cursor_cb(void (*cb)(int visible)) { cursor_cb = cb; }
void keyboard_set_completion_cb(int (*cb)(char *buf, int max)) { completion_cb = cb; }

static uint8_t key_state[256] = {0};
static void kb_track_scancode(uint8_t sc) {
    if (sc & 0x80) key_state[sc & 0x7F] = 0;
    else           key_state[sc] = 1;
}

#define KB_RING_SIZE 64
static volatile uint8_t  kb_ring[KB_RING_SIZE];
static volatile uint32_t kb_head = 0;
static volatile uint32_t kb_tail = 0;

static void keyboard_isr(void) {
    uint8_t sc = inb(0x60);
    kb_track_scancode(sc);

    uint32_t next = (kb_head + 1) % KB_RING_SIZE;
    if (next != kb_tail) {
        kb_ring[kb_head] = sc;
        kb_head = next;
    }

    lapic_eoi();
}

static int kb_ring_pop(uint8_t *out) {
    if (kb_tail == kb_head) return 0;
    *out = kb_ring[kb_tail];
    kb_tail = (kb_tail + 1) % KB_RING_SIZE;
    return 1;
}

void keyboard_init(void) {
    int guard = 0;
    while ((inb(0x64) & 1) && guard < 256) {
        if (!(inb(0x64) & 0x20)) (void)inb(0x60);
        else break;
        guard++;
    }

    irq_register(KEYBOARD_VECTOR, keyboard_isr);
    if (!ioapic_route_isa_irq(KEYBOARD_IRQ, KEYBOARD_VECTOR, lapic_get_id(), false)) {
        dmesg("[keyboard] failed to route IRQ1\n");
    }
}

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
    uint64_t blink_period = tsc_hz ? (tsc_hz / 2) : 500000000ULL;
    uint64_t last_blink   = kb_rdtsc();
    int      cursor_vis   = 1;
    int      extended     = 0;

    if (cursor_cb) cursor_cb(1);

    while (i < max - 1) {
        uint8_t sc;
        if (!kb_ring_pop(&sc)) {
            sched_yield();
            asm volatile("pause");

            uint64_t now = kb_rdtsc();
            if ((now - last_blink) >= blink_period) {
                cursor_vis = !cursor_vis;
                last_blink = now;
                if (cursor_cb) cursor_cb(cursor_vis);
            }
            continue;
        }
        if (cursor_cb) cursor_cb(0);

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

        if (sc == 0x0F) {

            if (completion_cb && i > 0) {
                buf[i] = '\0';
                int result = completion_cb(buf, max);
                if (result > 0) {

                    while (i > 0) { i--; terminal_putchar('\b'); }

                    i = result;
                    for (int j = 0; j < i; j++) {
                        terminal_putchar(buf[j]);
                    }
                }
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
    uint8_t sc;
    if (!kb_ring_pop(&sc)) return 0;
    return sc;
}
void keyboard_update(void) {
    uint8_t sc;
    while (kb_ring_pop(&sc)) { }
}

int keyboard_held(uint8_t scancode) {
    return key_state[scancode];
}