#include "apic.h"
#include "../mm/pmm.h"
#include <stdint.h>
#include <stddef.h>

#define IA32_APIC_BASE_MSR  0x1B
#define APIC_SPURIOUS       0x0F0
#define APIC_EOI            0x0B0
#define APIC_LVT_TIMER      0x320
#define APIC_TIMER_INIT     0x380
#define APIC_TIMER_CURR     0x390
#define APIC_TIMER_DIV      0x3E0
#define APIC_TIMER_PERIODIC 0x20000

extern uint64_t hhdm_offset;
extern void sleep_ms(uint32_t ms);
extern void serial_print(const char *s);
extern void serial_hex(uint64_t n);

static volatile uint32_t *apic_base = NULL;

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t val) {
    asm volatile("wrmsr" :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

uint32_t apic_read(uint32_t reg)            { return apic_base[reg / 4]; }
void     apic_write(uint32_t reg, uint32_t val) { apic_base[reg / 4] = val; }
void     apic_send_eoi(void)                { apic_write(APIC_EOI, 0); }

void apic_init(void) {
    serial_print("[apic] init\n");
    uint64_t msr  = rdmsr(IA32_APIC_BASE_MSR);
    uint64_t phys = msr & 0xFFFFF000ULL;
    uint64_t virt = phys + hhdm_offset;

    serial_print("[apic] phys="); serial_hex(phys); serial_print("\n");
    serial_print("[apic] virt="); serial_hex(virt); serial_print("\n");

    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    cr3 &= ~0xFFFULL;
    serial_print("[apic] cr3="); serial_hex(cr3); serial_print("\n");

    uint64_t *pml4  = (uint64_t *)(cr3 + hhdm_offset);
    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i   = (virt >> 21) & 0x1FF;
    uint64_t pt_i   = (virt >> 12) & 0x1FF;

    serial_print("[apic] pml4_i="); serial_hex(pml4_i); serial_print("\n");
    serial_print("[apic] pml4e=");  serial_hex(pml4[pml4_i]); serial_print("\n");

    if (!(pml4[pml4_i] & 1)) { serial_print("[apic] no pml4 entry\n"); return; }

    uint64_t *pdpt = (uint64_t *)((pml4[pml4_i] & ~0xFFFULL) + hhdm_offset);
    serial_print("[apic] pdpte="); serial_hex(pdpt[pdpt_i]); serial_print("\n");

    uint64_t *pd = NULL;
    if (!(pdpt[pdpt_i] & 1)) {
        serial_print("[apic] creating pdpt entry\n");
        void *new_pd_phys = pmm_alloc_page();
        if (!new_pd_phys) { serial_print("[apic] OOM\n"); return; }
        uint64_t *new_pd = (uint64_t *)((uint64_t)new_pd_phys + hhdm_offset);
        for (int i = 0; i < 512; i++) new_pd[i] = 0;
        pdpt[pdpt_i] = (uint64_t)new_pd_phys | (1<<0) | (1<<1);
        pd = new_pd;
    } else {
        pd = (uint64_t *)((pdpt[pdpt_i] & ~0xFFFULL) + hhdm_offset);
    }
    serial_print("[apic] pde="); serial_hex(pd[pd_i]); serial_print("\n");

    if (pd[pd_i] & (1 << 7)) {
        serial_print("[apic] huge page - already mapped\n");
        apic_base = (volatile uint32_t *)virt;
    } else if (pd[pd_i] & 1) {
        uint64_t *pt = (uint64_t *)((pd[pd_i] & ~0xFFFULL) + hhdm_offset);
        serial_print("[apic] pte="); serial_hex(pt[pt_i]); serial_print("\n");
        pt[pt_i] = phys | (1<<0) | (1<<1) | (1<<4);
        asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
        apic_base = (volatile uint32_t *)virt;
    } else {
        serial_print("[apic] creating pt\n");
        void *new_pt_phys = pmm_alloc_page();
        if (!new_pt_phys) { serial_print("[apic] OOM pt\n"); return; }
        uint64_t *new_pt = (uint64_t *)((uint64_t)new_pt_phys + hhdm_offset);
        for (int i = 0; i < 512; i++) new_pt[i] = 0;
        pd[pd_i] = (uint64_t)new_pt_phys | (1<<0) | (1<<1);
        new_pt[pt_i] = phys | (1<<0) | (1<<1) | (1<<4);
        asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
        apic_base = (volatile uint32_t *)virt;
    }

    serial_print("[apic] apic_base="); serial_hex((uint64_t)apic_base); serial_print("\n");

    wrmsr(IA32_APIC_BASE_MSR, msr | (1 << 11));
    apic_write(APIC_SPURIOUS, apic_read(APIC_SPURIOUS) | (1 << 8) | 0xFF);
    serial_print("[apic] init done\n");

        wrmsr(IA32_APIC_BASE_MSR, msr | (1 << 11));
    
    // Aguarda estabilização
    for(volatile int i = 0; i < 1000; i++) asm volatile("pause");
    
    // Testa escrita/leitura no APIC
    uint32_t test_val = apic_read(APIC_SPURIOUS);
    serial_print("[apic] spurious vector: "); serial_hex(test_val); serial_print("\n");
    
    // Ativa APIC (bit 8 = enable, bits 0-7 = spurious vector)
    apic_write(APIC_SPURIOUS, (test_val & ~0xFF) | (1 << 8) | 0xFF);
    
    serial_print("[apic] init done\n");
}

void apic_timer_init(uint32_t hz) {
    // Mascara o timer (desativa)
    apic_write(APIC_LVT_TIMER, 0x10000);  // Bit 16 = 1 (masked)
    serial_print("[apic] timer masked (disabled)\n");
}