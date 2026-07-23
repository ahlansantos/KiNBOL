#include "lapic.h"
#include "acpi.h"
#include "pit.h"
#include "dmesg.h"
#include "../mm/vmm.h"
#include <stddef.h>

extern uint64_t hhdm_offset;

#define REG_ID         0x020
#define REG_VERSION    0x030
#define REG_TPR        0x080
#define REG_EOI        0x0B0
#define REG_SVR        0x0F0
#define REG_LVT_TIMER  0x320
#define REG_TIMER_INIT 0x380
#define REG_TIMER_CUR  0x390
#define REG_TIMER_DIV  0x3E0

#define SVR_ENABLE     0x100
#define SPURIOUS_VECTOR 0xFF

#define LVT_TIMER_PERIODIC 0x20000
#define LVT_MASKED          0x10000

static volatile uint8_t *lapic_base = NULL;

static inline uint32_t rd(uint32_t reg) {
    return *(volatile uint32_t *)(lapic_base + reg);
}
static inline void wr(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(lapic_base + reg) = val;
}

static inline uint64_t rdtsc_(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    asm volatile("wrmsr" :: "a"((uint32_t)val), "d"((uint32_t)(val >> 32)), "c"(msr));
}
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

void lapic_init(void) {
    if (!acpi_info.valid) {
        dmesg("[lapic] no ACPI/MADT info, cannot init\n");
        return;
    }

    lapic_base = (volatile uint8_t *)(hhdm_offset + acpi_info.lapic_phys_addr);

    vmm_map(vmm_current(),
            hhdm_offset + (acpi_info.lapic_phys_addr & ~0xFFFULL),
            acpi_info.lapic_phys_addr & ~0xFFFULL,
            VMM_FLAGS_KERNEL);

    uint64_t apic_base_msr = rdmsr(0x1B);
    apic_base_msr |= (1ULL << 11);
    wrmsr(0x1B, apic_base_msr);

    wr(REG_TPR, 0);
    wr(REG_SVR, SVR_ENABLE | SPURIOUS_VECTOR);

    dmesg("[lapic] enabled\n");
}

void lapic_eoi(void) {
    wr(REG_EOI, 0);
}

uint32_t lapic_get_id(void) {
    return rd(REG_ID) >> 24;
}

void lapic_timer_init(uint32_t hz, uint8_t vector) {
    if (!lapic_base || !tsc_hz) {
        dmesg("[lapic] timer init skipped, lapic/tsc not ready\n");
        return;
    }

    wr(REG_TIMER_DIV, 0x3);

    wr(REG_LVT_TIMER, LVT_MASKED);

    uint64_t window_ticks = tsc_hz / 10;
    uint64_t t0 = rdtsc_();
    wr(REG_TIMER_INIT, 0xFFFFFFFF);
    while (rdtsc_() - t0 < window_ticks) asm volatile("pause");

    uint32_t elapsed = 0xFFFFFFFF - rd(REG_TIMER_CUR);

    if (elapsed >= 0xFFFFFFF0) {
        dmesg("[lapic] timer calibration saturated (bad window), aborting\n");
        return;
    }

    uint64_t lapic_hz = (uint64_t)elapsed * 10;

    dmesg("[lapic] calib: elapsed="); dmesg_hex(elapsed);
    dmesg(" lapic_hz="); dmesg_int((uint32_t)(lapic_hz / 1000));
    dmesg(" kHz\n");

    if (lapic_hz < 1000) {
        dmesg("[lapic] timer calibration looked bogus, aborting\n");
        return;
    }

    uint32_t reload = (uint32_t)(lapic_hz / hz);

    dmesg("[lapic] reload="); dmesg_hex(reload);
    dmesg(" for hz="); dmesg_int(hz); dmesg("\n");

    wr(REG_LVT_TIMER, LVT_TIMER_PERIODIC | vector);
    wr(REG_TIMER_DIV, 0x3);
    wr(REG_TIMER_INIT, reload);

    dmesg("[lapic] periodic timer armed\n");
}