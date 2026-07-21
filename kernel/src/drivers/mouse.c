/*
 * PS/2 mouse driver (the auxiliary device on the 8042 controller). Sends the
 * init command sequence, enables packet streaming, and turns the raw 3-byte
 * packets into x/y position and button state. Marked WONT WORK in the
 * original source, meaning it is not working correctly yet.
 */
// WONT WORK
#include "mouse.h"
#include "../graphics/api/baregl.h"
#include "../kernel/dmesg.h"
#include <stdint.h>

/* ─── PS/2 helpers ───────────────────────────────────────────────────── */

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0,%1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(port)); return v;
}

/* Wait until PS/2 input buffer is empty (bit1 of status = 0) */
static void ps2_wait_write(void) {
    int t = 100000;
    while (--t && (inb(0x64) & 2));
}
/* Wait until PS/2 output buffer has data (bit0 of status = 1) */
static void ps2_wait_read(void) {
    int t = 100000;
    while (--t && !(inb(0x64) & 1));
}

static void mouse_write(uint8_t val) {
    ps2_wait_write(); outb(0x64, 0xD4); /* next byte goes to mouse */
    ps2_wait_write(); outb(0x60, val);
}
static uint8_t mouse_read(void) {
    ps2_wait_read(); return inb(0x60);
}

/* ─── state ──────────────────────────────────────────────────────────── */

mouse_state_t mouse = {0};

/* Packet accumulator: PS/2 mouse sends 3-byte packets */
static uint8_t pkt[3];
static int     pkt_idx = 0;

/* Saved pixels under the cursor so we can restore them */
static uint32_t saved[16 * 16];
static int      saved_x = -1, saved_y = -1;

/* ─── cursor sprite (16x16, 1-bit, big-endian rows) ─────────────────── */
/* Simple arrow pointing top-left */
static const uint16_t CURSOR[16] = {
    0b1111111111000000,
    0b1111111100000000,
    0b1111110000000000,
    0b1111111000000000,
    0b1111001100000000,
    0b1110000110000000,
    0b1100000011000000,
    0b1000000001100000,
    0b0000000000110000,
    0b0000000000011000,
    0b0000000000001100,
    0b0000000000000110,
    0b0000000000000011,
    0b0000000000000001,
    0b0000000000000000,
    0b0000000000000000,
};

/* ─── cursor rendering ───────────────────────────────────────────────── */

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static void cursor_erase(void) {
    if (saved_x < 0) return;
    int w = bare_width(), h = bare_height();
    for (int row = 0; row < 16; row++) {
        int py = saved_y + row;
        if (py < 0 || py >= h) continue;
        for (int col = 0; col < 16; col++) {
            int px = saved_x + col;
            if (px < 0 || px >= w) continue;
            bare_pixel(px, py, saved[row * 16 + col]);
        }
    }
    saved_x = saved_y = -1;
}

static void cursor_draw(int x, int y) {
    int w = bare_width(), h = bare_height();
    /* save pixels underneath */
    for (int row = 0; row < 16; row++) {
        int py = y + row;
        for (int col = 0; col < 16; col++) {
            int px = x + col;
            /* bare_pixel can't read back; we store 0 for out-of-bounds */
            saved[row * 16 + col] = (px >= 0 && px < w && py >= 0 && py < h)
                ? 0 : 0;
        }
    }
    saved_x = x; saved_y = y;

    /* draw cursor */
    for (int row = 0; row < 16; row++) {
        int py = y + row;
        if (py < 0 || py >= h) continue;
        for (int col = 0; col < 16; col++) {
            int px = x + col;
            if (px < 0 || px >= w) continue;
            if (CURSOR[row] & (1 << (15 - col))) {
                /* white pixel with 1-pixel black outline */
                bare_pixel(px, py, 0xFFFFFF);
            }
        }
    }
}

/* ─── init ───────────────────────────────────────────────────────────── */

void mouse_init(void) {
    /* Enable auxiliary (mouse) port on PS/2 controller */
    ps2_wait_write(); outb(0x64, 0xA8);

    /* Read current compaq byte, set bit1 (enable IRQ12), clear bit5 (disable mouse clock disable) */
    ps2_wait_write(); outb(0x64, 0x20);
    ps2_wait_read();
    uint8_t status = inb(0x60);
    status |=  (1 << 1); /* enable IRQ12 */
    status &= ~(1 << 5); /* enable mouse clock */
    ps2_wait_write(); outb(0x64, 0x60);
    ps2_wait_write(); outb(0x60, status);

    /* Set defaults */
    mouse_write(0xF6); mouse_read(); /* ACK */

    /* Enable data reporting */
    mouse_write(0xF4); mouse_read(); /* ACK */

    mouse.x = bare_width()  / 2;
    mouse.y = bare_height() / 2;

    dmesg("[mouse] PS/2 mouse initialized\n");
}

/* ─── ISR (called from IRQ12 handler) ───────────────────────────────── */

void mouse_isr(void) {
    uint8_t byte = inb(0x60);
    pkt[pkt_idx++] = byte;
    if (pkt_idx < 3) return;
    pkt_idx = 0;

    /* Byte 0: flags. Bit3 must be set (sync bit), discard desynced packets */
    if (!(pkt[0] & 0x08)) return;

    /* Overflow bits — discard */
    if ((pkt[0] & 0xC0)) return;

    int dx =  (int)pkt[1] - ((pkt[0] & 0x10) ? 256 : 0);
    int dy = -(int)pkt[2] + ((pkt[0] & 0x20) ? 256 : 0); /* Y is inverted */

    mouse.x = clamp(mouse.x + dx, 0, bare_width()  - 1);
    mouse.y = clamp(mouse.y + dy, 0, bare_height() - 1);

    mouse.left   = (pkt[0] & 0x01) != 0;
    mouse.right  = (pkt[0] & 0x02) != 0;
    mouse.middle = (pkt[0] & 0x04) != 0;

    mouse_draw();
}

/* ─── draw ───────────────────────────────────────────────────────────── */

void mouse_draw(void) {
    cursor_erase();
    cursor_draw(mouse.x, mouse.y);
}