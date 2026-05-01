#include "rtc.h"
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0,%1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    asm volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

static uint8_t bcd2bin(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

static uint8_t rtc_reg(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static int rtc_updating(void) {
    outb(0x70, 0x0A);
    return inb(0x71) & 0x80;
}

rtc_time_t rtc_read(void) {
    while (rtc_updating());

    rtc_time_t t;
    t.second = rtc_reg(0x00);
    t.minute = rtc_reg(0x02);
    t.hour   = rtc_reg(0x04);
    t.day    = rtc_reg(0x07);
    t.month  = rtc_reg(0x08);
    uint8_t year_raw = rtc_reg(0x09);

    uint8_t regB = rtc_reg(0x0B);

    if (!(regB & 0x04)) {
        t.second = bcd2bin(t.second);
        t.minute = bcd2bin(t.minute);
        t.hour   = bcd2bin(t.hour);
        t.day    = bcd2bin(t.day);
        t.month  = bcd2bin(t.month);
        year_raw = bcd2bin(year_raw);
    }

    t.year = 2000 + year_raw;
    return t;
}