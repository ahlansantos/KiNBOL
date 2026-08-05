#include "rand.h"
#include "pit.h"
#include "klog.h"

static uint64_t g_state;
static int g_seeded = 0;

static inline uint64_t rdtsc64(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static int cpu_has_rdseed(void) {
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    return (b >> 18) & 1;
}

static uint64_t hw_entropy(void) {
    if (!cpu_has_rdseed()) return 0;
    uint64_t val;
    uint8_t ok;
    asm volatile(
        "rdseed %0;"
        "setc %1;"
        : "=r"(val), "=qm"(ok)
    );
    if (ok) return val;
    return 0;
}

void krand_init(void) {
    uint64_t seed = rdtsc64();
    seed ^= (uint64_t)pit_get_ticks() << 17;
    seed ^= hw_entropy();
    seed ^= 0x9E3779B97F4A7C15ULL; 
    if (seed == 0) seed = 0xD1B54A32D192ED03ULL;
    g_state = seed;
    g_seeded = 1;
    KLOG_I("rand", "PRNG seeded for ASLR");
}

uint64_t krand_u64(void) {
    if (!g_seeded) krand_init();
    
    uint64_t x = g_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

uint64_t krand_below(uint64_t bound) {
    if (bound == 0) return 0;
    return krand_u64() % bound;
}

uint64_t krand_page_aligned_below(uint64_t bound) {
    if (bound < 0x1000) return 0;
    uint64_t pages = bound / 0x1000;
    return krand_below(pages) * 0x1000ULL;
}