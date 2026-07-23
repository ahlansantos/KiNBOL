#include "serial.h"
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0,%1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    asm volatile("inb %1,%0" : "=a"(val) : "Nd"(port));
    return val;
}

void serial_init(void) {
    outb(0x3F8+1, 0x00);
    outb(0x3F8+3, 0x80);
    outb(0x3F8+0, 0x03);
    outb(0x3F8+1, 0x00);
    outb(0x3F8+3, 0x03);
    outb(0x3F8+2, 0xC7);
    outb(0x3F8+4, 0x0B);
}

void serial_putc(char c) {
    while (!(inb(0x3F8+5) & 0x20));
    outb(0x3F8, c);
}

void serial_print(const char *s) {
    for (int i = 0; s[i]; i++) serial_putc(s[i]);
}

void serial_hex(uint64_t n) {
    char h[] = "0123456789ABCDEF";
    serial_print("0x");
    for (int i = 15; i >= 0; i--)
        serial_putc(h[(n >> (i * 4)) & 0xF]);
}