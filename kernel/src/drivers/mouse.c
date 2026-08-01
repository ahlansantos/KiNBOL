#include "mouse.h"
#include "../kernel/idt.h"
#include "../kernel/ioapic.h"
#include "../kernel/lapic.h"
#include "../kernel/dmesg.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define MOUSE_VECTOR 44
#define MOUSE_IRQ    12

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    asm volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void wait_input(void) {
    int guard = 100000;
    while ((inb(PS2_STATUS) & 0x02) && guard--) { }
}
static void wait_output(void) {
    int guard = 100000;
    while (!(inb(PS2_STATUS) & 0x01) && guard--) { }
}

static void mouse_write(uint8_t val) {
    wait_input();
    outb(PS2_CMD, 0xD4);
    wait_input();
    outb(PS2_DATA, val);
}
static uint8_t mouse_read(void) {
    wait_output();
    return inb(PS2_DATA);
}

static volatile mouse_state_t state = {0, 0, 0, 0, 0};
static uint8_t packet[3];
static int packet_idx = 0;

static volatile int cur_dx = 0;
static volatile int cur_dy = 0;

static void mouse_isr(void) {
    if (!(inb(PS2_STATUS) & 0x20)) {
        lapic_eoi();
        return;
    }

    uint8_t byte = inb(PS2_DATA);
    packet[packet_idx++] = byte;

    if (packet_idx == 1 && !(packet[0] & 0x08)) {
        packet_idx = 0;
        lapic_eoi();
        return;
    }

    if (packet_idx == 3) {
        packet_idx = 0;
        uint8_t flags = packet[0];

        int dx = packet[1];
        int dy = packet[2];
        if (flags & 0x10) dx -= 256;
        if (flags & 0x20) dy -= 256;
        if (flags & 0x40 || flags & 0x80) { dx = 0; dy = 0; }

        state.dx    += dx;
        state.dy    -= dy;
        state.left   = flags & 0x01;
        state.right  = flags & 0x02;
        state.middle = flags & 0x04;

        cur_dx += dx;
        cur_dy -= dy;
    }

    lapic_eoi();
}

void mouse_init(void) {
    wait_input();
    outb(PS2_CMD, 0xA8);

    wait_input();
    outb(PS2_CMD, 0x20);
    uint8_t status = mouse_read();
    status |= 0x02;
    status &= ~0x20;

    wait_input();
    outb(PS2_CMD, 0x60);
    wait_input();
    outb(PS2_DATA, status);

    mouse_write(0xF6);
    if (mouse_read() != 0xFA) {
        dmesg("[mouse] set defaults: no ACK\n");
    }

    mouse_write(0xF4);
    if (mouse_read() != 0xFA) {
        dmesg("[mouse] enable reporting: no ACK\n");
    }

    irq_register(MOUSE_VECTOR, mouse_isr);
    if (!ioapic_route_isa_irq(MOUSE_IRQ, MOUSE_VECTOR, lapic_get_id(), false)) {
        dmesg("[mouse] failed to route IRQ12\n");
        return;
    }

    dmesg("[mouse] PS/2 mouse online\n");
}

mouse_state_t mouse_get_state(void) {
    mouse_state_t copy;
    copy.dx     = state.dx;
    copy.dy     = state.dy;
    copy.left   = state.left;
    copy.right  = state.right;
    copy.middle = state.middle;
    state.dx = 0;
    state.dy = 0;
    return copy;
}

void mouse_get_cursor_delta(int *dx, int *dy, int *left, int *right, int *middle) {
    *dx = cur_dx;
    *dy = cur_dy;
    cur_dx = 0;
    cur_dy = 0;
    *left   = state.left;
    *right  = state.right;
    *middle = state.middle;
}