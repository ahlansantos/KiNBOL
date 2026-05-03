#include "pit.h"
#include <stdint.h>

uint64_t tsc_hz   = 0;
uint64_t boot_tsc = 0;

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0,%1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t val; asm volatile("inb %1,%0" : "=a"(val) : "Nd"(port)); return val;
}
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint16_t pit_read(void) {
    outb(0x43, 0x00);
    return (uint16_t)inb(0x40) | ((uint16_t)inb(0x40) << 8);
}

void tsc_calibrate(void) {
    outb(0x43, 0x34); outb(0x40, 0xFF); outb(0x40, 0xFF);
    while (pit_read() < 60000) asm volatile("pause");
    while (pit_read() < 60000) asm volatile("pause");
    uint16_t start = pit_read(); uint64_t t0 = rdtsc();
    uint16_t target = start - 10000;
    while (pit_read() > target) asm volatile("pause");
    uint64_t cycles = rdtsc() - t0;
    tsc_hz = (cycles * 1193182ULL) / 10000ULL;
    if (tsc_hz < 100000000ULL) tsc_hz = 1000000000ULL;
    boot_tsc = rdtsc();
}

uint64_t uptime_ms(void) {
    if (!tsc_hz) return 0;
    return ((rdtsc() - boot_tsc) * 1000ULL) / tsc_hz;
}

void sleep_ms(uint32_t ms) {
    uint64_t end = rdtsc() + (tsc_hz * (uint64_t)ms) / 1000ULL;
    while (rdtsc() < end) asm volatile("pause");
}