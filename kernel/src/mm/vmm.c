#include "vmm.h"
#include "pmm.h"
#include <stdint.h>
#include <stddef.h>

static inline void write_cr3(uint64_t val) {
    asm volatile("mov %0, %%cr3" :: "r"(val) : "memory");
}
static inline uint64_t read_cr3(void) {
    uint64_t val;
    asm volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}
static inline void invlpg(uint64_t virt) {
    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

extern uint64_t hhdm_offset;

static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + hhdm_offset);
}
static inline uint64_t virt_to_phys(void *virt) {
    return (uint64_t)virt - hhdm_offset;
}

static uint64_t alloc_table(void) {
    void *phys = pmm_alloc_page();
    if (!phys) return 0;
    uint64_t *tbl = (uint64_t *)phys_to_virt((uint64_t)phys);
    for (int i = 0; i < 512; i++) tbl[i] = 0;
    return (uint64_t)phys;
}
static inline uint64_t pml4_idx(uint64_t virt) { return (virt >> 39) & 0x1FF; }
static inline uint64_t pdpt_idx(uint64_t virt) { return (virt >> 30) & 0x1FF; }
static inline uint64_t pd_idx  (uint64_t virt) { return (virt >> 21) & 0x1FF; }
static inline uint64_t pt_idx  (uint64_t virt) { return (virt >> 12) & 0x1FF; }

static uint64_t *get_or_create(uint64_t *entry, uint64_t flags) {
    (void)flags;
    if (*entry & VMM_PRESENT) {
        return (uint64_t *)phys_to_virt(*entry & ~0xFFFULL);
    }
    // Não existe — aloca nova tabela
    uint64_t phys = alloc_table();
    if (!phys) return NULL;
    *entry = phys | VMM_PRESENT | VMM_WRITE | VMM_USER;
    return (uint64_t *)phys_to_virt(phys);
}

static pagemap_t kernel_pagemap = NULL;

int vmm_map(pagemap_t pm, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!pm) return -1;

    uint64_t *pml4 = (uint64_t *)phys_to_virt((uint64_t)pm);
    uint64_t *pdpt = get_or_create(&pml4[pml4_idx(virt)], flags);
    if (!pdpt) return -1;

    uint64_t *pd = get_or_create(&pdpt[pdpt_idx(virt)], flags);
    if (!pd) return -1;

    uint64_t *pt = get_or_create(&pd[pd_idx(virt)], flags);
    if (!pt) return -1;

    pt[pt_idx(virt)] = (phys & ~0xFFFULL) | (flags & 0xFFF) | (flags & VMM_NX);

    invlpg(virt);
    return 0;
}

void vmm_unmap(pagemap_t pm, uint64_t virt) {
    if (!pm) return;

    uint64_t *pml4 = (uint64_t *)phys_to_virt((uint64_t)pm);
    if (!(pml4[pml4_idx(virt)] & VMM_PRESENT)) return;

    uint64_t *pdpt = (uint64_t *)phys_to_virt(pml4[pml4_idx(virt)] & ~0xFFFULL);
    if (!(pdpt[pdpt_idx(virt)] & VMM_PRESENT)) return;

    uint64_t *pd = (uint64_t *)phys_to_virt(pdpt[pdpt_idx(virt)] & ~0xFFFULL);
    if (!(pd[pd_idx(virt)] & VMM_PRESENT)) return;

    uint64_t *pt = (uint64_t *)phys_to_virt(pd[pd_idx(virt)] & ~0xFFFULL);

    pt[pt_idx(virt)] = 0;
    invlpg(virt);
}

uint64_t vmm_virt_to_phys(pagemap_t pm, uint64_t virt) {
    if (!pm) return 0;

    uint64_t *pml4 = (uint64_t *)phys_to_virt((uint64_t)pm);
    if (!(pml4[pml4_idx(virt)] & VMM_PRESENT)) return 0;

    uint64_t *pdpt = (uint64_t *)phys_to_virt(pml4[pml4_idx(virt)] & ~0xFFFULL);
    if (!(pdpt[pdpt_idx(virt)] & VMM_PRESENT)) return 0;

    uint64_t *pd = (uint64_t *)phys_to_virt(pdpt[pdpt_idx(virt)] & ~0xFFFULL);
    if (!(pd[pd_idx(virt)] & VMM_PRESENT)) return 0;

    uint64_t *pt = (uint64_t *)phys_to_virt(pd[pd_idx(virt)] & ~0xFFFULL);
    if (!(pt[pt_idx(virt)] & VMM_PRESENT)) return 0;

    return (pt[pt_idx(virt)] & ~0xFFFULL) | (virt & 0xFFF);
}

int vmm_map_range(pagemap_t pm,
                  uint64_t virt_start, uint64_t phys_start,
                  uint64_t size, uint64_t flags) {
    uint64_t pages = (size + 0xFFF) / 0x1000;
    for (uint64_t i = 0; i < pages; i++) {
        int r = vmm_map(pm,
                        virt_start + i * 0x1000,
                        phys_start + i * 0x1000,
                        flags);
        if (r != 0) return -1;
    }
    return 0;
}

void vmm_switch(pagemap_t pm) {
    if (!pm) return;
    write_cr3(virt_to_phys(pm));
}

pagemap_t vmm_current(void) {
    uint64_t cr3 = read_cr3();
    return (pagemap_t)phys_to_virt(cr3 & ~0xFFFULL);
}

pagemap_t vmm_create_pagemap(void) {
    uint64_t phys = alloc_table();
    if (!phys) return NULL;
    pagemap_t pm = (pagemap_t)phys_to_virt(phys);

    if (kernel_pagemap) {
        uint64_t *kpml4 = (uint64_t *)kernel_pagemap;
        uint64_t *npml4 = (uint64_t *)pm;

        for (int i = 256; i < 512; i++) {
            npml4[i] = kpml4[i];
        }
    }
    return pm;
}

void vmm_destroy_pagemap(pagemap_t pm) {
    if (!pm || pm == kernel_pagemap) return;

    uint64_t *pml4 = (uint64_t *)pm;

    for (int i = 0; i < 256; i++) {
        if (!(pml4[i] & VMM_PRESENT)) continue;
        uint64_t *pdpt = (uint64_t *)phys_to_virt(pml4[i] & ~0xFFFULL);

        for (int j = 0; j < 512; j++) {
            if (!(pdpt[j] & VMM_PRESENT)) continue;
            uint64_t *pd = (uint64_t *)phys_to_virt(pdpt[j] & ~0xFFFULL);

            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & VMM_PRESENT)) continue;

                pmm_free_page((void *)(pd[k] & ~0xFFFULL));
            }
            pmm_free_page((void *)(pdpt[j] & ~0xFFFULL));
        }
        pmm_free_page((void *)(pml4[i] & ~0xFFFULL));
    }

    pmm_free_page((void *)virt_to_phys(pm));
}

void vmm_init(void) {
    uint64_t phys = alloc_table();
    if (!phys) return;
    kernel_pagemap = (pagemap_t)phys_to_virt(phys);

    uint64_t hhdm_pages = 0x100000000ULL / 0x200000ULL; 
    uint64_t *pml4 = (uint64_t *)kernel_pagemap;

    for (uint64_t i = 0; i < hhdm_pages; i++) {
        uint64_t virt = hhdm_offset + i * 0x200000ULL;
        uint64_t phys_addr = i * 0x200000ULL;

        uint64_t pml4_i = (virt >> 39) & 0x1FF;
        uint64_t pdpt_i = (virt >> 30) & 0x1FF;
        uint64_t pd_i   = (virt >> 21) & 0x1FF;

        if (!(pml4[pml4_i] & VMM_PRESENT)) {
            uint64_t p = alloc_table();
            if (!p) return;
            pml4[pml4_i] = p | VMM_PRESENT | VMM_WRITE;
        }
        uint64_t *pdpt = (uint64_t *)phys_to_virt(pml4[pml4_i] & ~0xFFFULL);

        if (!(pdpt[pdpt_i] & VMM_PRESENT)) {
            uint64_t p = alloc_table();
            if (!p) return;
            pdpt[pdpt_i] = p | VMM_PRESENT | VMM_WRITE;
        }
        uint64_t *pd = (uint64_t *)phys_to_virt(pdpt[pdpt_i] & ~0xFFFULL);

        pd[pd_i] = phys_addr | VMM_PRESENT | VMM_WRITE | VMM_HUGE;
    }

    uint64_t apic_phys = 0xFEE00000ULL;
    uint64_t apic_virt = apic_phys + hhdm_offset;
    uint64_t apic_pml4_i = (apic_virt >> 39) & 0x1FF;
    uint64_t apic_pdpt_i = (apic_virt >> 30) & 0x1FF;
    uint64_t apic_pd_i   = (apic_virt >> 21) & 0x1FF;

    if (!(pml4[apic_pml4_i] & VMM_PRESENT)) {
        uint64_t p = alloc_table();
        if (!p) return;
        pml4[apic_pml4_i] = p | VMM_PRESENT | VMM_WRITE;
    }
    uint64_t *apic_pdpt = (uint64_t *)phys_to_virt(pml4[apic_pml4_i] & ~0xFFFULL);
    if (!(apic_pdpt[apic_pdpt_i] & VMM_PRESENT)) {
        uint64_t p = alloc_table();
        if (!p) return;
        apic_pdpt[apic_pdpt_i] = p | VMM_PRESENT | VMM_WRITE;
    }
    uint64_t *apic_pd = (uint64_t *)phys_to_virt(apic_pdpt[apic_pdpt_i] & ~0xFFFULL);
    apic_pd[apic_pd_i] = apic_phys | VMM_PRESENT | VMM_WRITE | VMM_HUGE;
}