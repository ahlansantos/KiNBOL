#include "fpu.h"
#include "dmesg.h"
#include <stdint.h>

#define FPU_STATE_SIZE 512

static int g_fpu_ready = 0;

int fpu_ready(void) {
    return g_fpu_ready;
}

void fpu_make_default_state(void *out, size_t n) {
    if (!out || n < FPU_STATE_SIZE) return;

    static uint8_t tpl[FPU_STATE_SIZE] __attribute__((aligned(16)));
    static int     ready = 0;

    if (!ready) {
        uint32_t mxcsr = 0x1F80u;
        asm volatile(
            "fninit\n\t"
            "ldmxcsr (%0)\n\t"
            "fxsave64 (%1)\n\t"
            :
            : "r"(&mxcsr), "r"(tpl)
            : "memory");
        ready = 1;
    }

    for (size_t i = 0; i < FPU_STATE_SIZE; i++) {
        ((uint8_t *)out)[i] = tpl[i];
    }

    g_fpu_ready = 1;
}

void fpu_report(void) {
    uint32_t a, b, c, d;
    a = b = c = d = 0;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));

    dmesg("[cpu] FPU/SSE enabled: x87");
    if (d & (1u << 23)) dmesg(" MMX");
    if (d & (1u << 25)) dmesg(" SSE");
    if (d & (1u << 26)) dmesg(" SSE2");
    if (c & (1u << 26)) dmesg(" XSAVE");
    dmesg("\n");

    dmesg("[cpu] FPU state saved/restored per task (FXSAVE64/FXRSTOR64)\n");
}