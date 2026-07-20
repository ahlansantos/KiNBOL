#include "pit.h"
#include "dmesg.h"
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
    /* 10000 counts (~8.4ms) had the same problem the LAPIC self-cal
     * window did: short enough that ordinary jitter is a big fraction
     * of the total, so tsc_hz can come out noticeably too high on some
     * boots. Since window_ticks in lapic_timer_init is derived directly
     * from tsc_hz, an inflated tsc_hz there makes that loop run far
     * longer than the 100ms it thinks it's timing - long enough for the
     * LAPIC down-counter to hit zero and saturate, which is exactly the
     * ~1000x-too-slow tick rate just observed. 40000 counts (~33.5ms)
     * is still safely within one PIT countdown pass (start is <60000
     * after the guard loops above) but cuts jitter's relative share by
     * ~4x. */
    uint16_t start = pit_read(); uint64_t t0 = rdtsc();
    uint16_t target = start - 40000;
    while (pit_read() > target) asm volatile("pause");
    uint64_t cycles = rdtsc() - t0;
    tsc_hz = (cycles * 1193182ULL) / 40000ULL;
    dmesg("[pit] measured tsc_hz=");
    dmesg_int((uint32_t)(tsc_hz / 1000000ULL));
    dmesg(" MHz\n");
    if (tsc_hz < 100000000ULL) {
        dmesg("[pit] measurement looked bogus, falling back to 1GHz assumption\n");
        tsc_hz = 1000000000ULL;
    }
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

static volatile uint64_t system_ticks = 0;

void pit_tick(void) {
    if (system_ticks == 0) dmesg("[timer] first tick fired!\n");
    system_ticks++;
}

uint64_t pit_get_ticks(void) {
    return system_ticks;
}