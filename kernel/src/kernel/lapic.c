/*
 * Driver for the per-CPU Local APIC. Enables the LAPIC through the
 * IA32_APIC_BASE MSR, sends EOI after every interrupt, and, most
 * importantly, self-calibrates and arms the LAPIC's periodic timer
 * (which replaced the PIT/IRQ0 as the system's tick source). This is
 * where the TSC-based calibration work happened.
 */
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

    /* Limine's own page tables (the ones actually active right now) map
     * normal RAM via HHDM but have no reason to map an MMIO hole like
     * the LAPIC's registers - nothing needs that until this exact write.
     * Map just this one 4KB page (all LAPIC registers we use fit in the
     * first 0x400 bytes) into whatever pagemap is currently live. */
    vmm_map(vmm_current(),
            hhdm_offset + (acpi_info.lapic_phys_addr & ~0xFFFULL),
            acpi_info.lapic_phys_addr & ~0xFFFULL,
            VMM_FLAGS_KERNEL);

    /* Set the global-enable bit in IA32_APIC_BASE (bit 11). Leave the
     * base-address field alone - it should already match what ACPI
     * reported, since relocating the LAPIC is not something we do. */
    uint64_t apic_base_msr = rdmsr(0x1B);
    apic_base_msr |= (1ULL << 11);
    wrmsr(0x1B, apic_base_msr);

    wr(REG_TPR, 0); /* accept every priority */
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

    /* Divide the LAPIC timer's input clock by 16. */
    wr(REG_TIMER_DIV, 0x3);

    /* Self-calibrate: run the LAPIC timer counting down from its max
     * for a known TSC-measured window, see how far it got, derive its
     * real frequency. Pure polling on both sides - no interrupt of any
     * kind is required for this, so it can't silently "not fire". */
    wr(REG_LVT_TIMER, LVT_MASKED);

    /* t0 must be sampled BEFORE the write that arms the counter, not
     * after. REG_TIMER_INIT is an MMIO write to the LAPIC, which QEMU
     * traps/emulates with real, non-trivial latency - the countdown
     * starts the instant the write lands, so if t0 is taken afterward,
     * that write latency becomes an uncounted head start: by the time
     * REG_TIMER_CUR is read below, the counter has actually been
     * running for (window + write latency), not a clean window. That
     * inflates the elapsed/lapic_hz estimate, which inflates the
     * computed reload value, which makes the real periodic period
     * longer than requested - fewer ticks than the caller asked for.
     * Sampling t0 first folds the write's latency into the measured
     * window instead of before it, cancelling almost all of the bias. */
    /* 10ms was too short a sample: any timing jitter (QEMU's rdtsc<->
     * wall-clock mapping isn't perfectly linear over very short windows,
     * especially without hardware acceleration) is a small absolute
     * amount but a large fraction of 10ms, which is exactly why this
     * came out wrong by a different amount on every boot instead of a
     * single consistent ratio. Widening the window to 100ms doesn't
     * remove the jitter, but dilutes it to a much smaller fraction of
     * the total, which is what actually gets rid of it in practice. */
    uint64_t window_ticks = tsc_hz / 10; /* 100ms window */
    uint64_t t0 = rdtsc_();
    wr(REG_TIMER_INIT, 0xFFFFFFFF);
    while (rdtsc_() - t0 < window_ticks) asm volatile("pause");

    uint32_t elapsed = 0xFFFFFFFF - rd(REG_TIMER_CUR);

    /* If elapsed is at (or essentially at) the max possible value, the
     * counter ran all the way down to 0 before the window ended - i.e.
     * it saturated. That means this measurement is not "the LAPIC is
     * very fast", it means the window was wrong (too long) and this
     * number is garbage. Trusting it produces a reload that's off by
     * orders of magnitude instead of a normal few-percent calibration
     * error, so refuse it outright rather than arming with it. */
    if (elapsed >= 0xFFFFFFF0) {
        dmesg("[lapic] timer calibration saturated (bad window), aborting\n");
        return;
    }

    uint64_t lapic_hz = (uint64_t)elapsed * 10; /* scale 100ms window up to 1s */

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