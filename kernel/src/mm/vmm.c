#include "vmm.h"
#include "pmm.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

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

static inline uint64_t read_msr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void write_msr(uint32_t msr, uint64_t val) {
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    asm volatile("wrmsr" :: "c"(msr), "a"(lo), "d"(hi) : "memory");
}

#define IA32_PAT_MSR   0x277
#define PAT_WC         0x01ULL

#define VMM_PWT        (1ULL << 3)
#define VMM_PCD        (1ULL << 4)
#define VMM_PAT_4K     (1ULL << 7)
#define VMM_PAT_HUGE   (1ULL << 12)

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
    uint64_t phys = alloc_table();
    if (!phys) return NULL;
    *entry = phys | VMM_PRESENT | VMM_WRITE | VMM_USER;
    return (uint64_t *)phys_to_virt(phys);
}

static pagemap_t kernel_pagemap = NULL;

int vmm_map(pagemap_t pm, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!pm) return -1;

    uint64_t *pml4 = (uint64_t *)pm;
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

    uint64_t *pml4 = (uint64_t *)pm;
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

    uint64_t *pml4 = (uint64_t *)pm;
    if (!(pml4[pml4_idx(virt)] & VMM_PRESENT)) return 0;

    uint64_t *pdpt = (uint64_t *)phys_to_virt(pml4[pml4_idx(virt)] & ~0xFFFULL);
    if (!(pdpt[pdpt_idx(virt)] & VMM_PRESENT)) return 0;

    uint64_t *pd = (uint64_t *)phys_to_virt(pdpt[pdpt_idx(virt)] & ~0xFFFULL);
    if (!(pd[pd_idx(virt)] & VMM_PRESENT)) return 0;

    uint64_t *pt = (uint64_t *)phys_to_virt(pd[pd_idx(virt)] & ~0xFFFULL);
    if (!(pt[pt_idx(virt)] & VMM_PRESENT)) return 0;

    return (pt[pt_idx(virt)] & ~0xFFFULL) | (virt & 0xFFF);
}

bool vmm_check_user_range(pagemap_t pm, uint64_t virt, uint64_t len, bool need_write) {
    if (!pm || len == 0) return false;

    if (virt + len < virt) return false;

    uint64_t start = virt & ~0xFFFULL;
    uint64_t end   = (virt + len - 1) & ~0xFFFULL;

    for (uint64_t page = start; ; page += PAGE_SIZE) {
        uint64_t *pml4 = (uint64_t *)pm;
        uint64_t e1 = pml4[pml4_idx(page)];
        if (!(e1 & VMM_PRESENT) || !(e1 & VMM_USER)) return false;

        uint64_t *pdpt = (uint64_t *)phys_to_virt(e1 & ~0xFFFULL);
        uint64_t e2 = pdpt[pdpt_idx(page)];
        if (!(e2 & VMM_PRESENT) || !(e2 & VMM_USER)) return false;

        uint64_t *pd = (uint64_t *)phys_to_virt(e2 & ~0xFFFULL);
        uint64_t e3 = pd[pd_idx(page)];
        if (!(e3 & VMM_PRESENT) || !(e3 & VMM_USER)) return false;

        uint64_t *pt = (uint64_t *)phys_to_virt(e3 & ~0xFFFULL);
        uint64_t e4 = pt[pt_idx(page)];
        if (!(e4 & VMM_PRESENT) || !(e4 & VMM_USER)) return false;
        if (need_write && !(e4 & VMM_WRITE)) return false;

        if (page == end) break;
    }

    return true;
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

int vmm_sync_kernel_entry(uint64_t vaddr) {
    if (!kernel_pagemap) return 0;

    pagemap_t active = vmm_current();
    if (!active || active == kernel_pagemap) return 0;

    int idx = (int)((vaddr >> 39) & 0x1FF);
    if (idx < 256) return 0;

    uint64_t *kpml4 = (uint64_t *)kernel_pagemap;
    uint64_t *apml4 = (uint64_t *)active;

    if (!(kpml4[idx] & VMM_PRESENT)) return 0;
    if (apml4[idx] == kpml4[idx]) return 0;

    apml4[idx] = kpml4[idx];
    asm volatile("movq %0, %%cr3" :: "r"(read_cr3()) : "memory");
    return 1;
}

pagemap_t vmm_create_pagemap(void) {
    uint64_t phys = alloc_table();
    if (!phys) return NULL;
    pagemap_t pm = (pagemap_t)phys_to_virt(phys);

    pagemap_t active = vmm_current();

    if (active) {
        uint64_t *apml4 = (uint64_t *)active;
        uint64_t *npml4 = (uint64_t *)pm;

        for (int i = 256; i < 512; i++) {
            npml4[i] = apml4[i];
        }
    }
    return pm;
}

void vmm_destroy_pagemap(pagemap_t pm) {
    if (!pm || pm == vmm_current()) return;

    uint64_t *pml4 = (uint64_t *)pm;

    for (int i = 0; i < 256; i++) {
        if (!(pml4[i] & VMM_PRESENT)) continue;
        uint64_t *pdpt = (uint64_t *)phys_to_virt(pml4[i] & ~0xFFFULL);

        for (int j = 0; j < 512; j++) {
            if (!(pdpt[j] & VMM_PRESENT)) continue;
            uint64_t *pd = (uint64_t *)phys_to_virt(pdpt[j] & ~0xFFFULL);

            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & VMM_PRESENT)) continue;
                if (pd[k] & VMM_HUGE) {
                    pmm_free_page((void *)(pd[k] & ~0xFFFULL));
                    continue;
                }

                uint64_t *pt = (uint64_t *)phys_to_virt(pd[k] & ~0xFFFULL);
                for (int l = 0; l < 512; l++) {
                    if (!(pt[l] & VMM_PRESENT)) continue;
                    pmm_free_page((void *)(pt[l] & ~0xFFFULL));
                }
                pmm_free_page((void *)(pd[k] & ~0xFFFULL));
            }
            pmm_free_page((void *)(pdpt[j] & ~0xFFFULL));
        }
        pmm_free_page((void *)(pml4[i] & ~0xFFFULL));
    }

    pmm_free_page((void *)virt_to_phys(pm));
}

void vmm_enable_writecombine_pat(void) {
    uint64_t pat = read_msr(IA32_PAT_MSR);
    pat &= ~(0xFFULL << 8);
    pat |= (PAT_WC << 8);
    write_msr(IA32_PAT_MSR, pat);
}

bool vmm_mark_range_writecombine(uint64_t virt, uint64_t size) {
    pagemap_t pm = vmm_current();
    if (!pm || size == 0) return false;

    uint64_t start = virt & ~0xFFFULL;
    uint64_t end   = (virt + size - 1) & ~0xFFFULL;
    uint64_t *pml4 = (uint64_t *)pm;

    for (uint64_t page = start; page <= end; ) {
        uint64_t e1 = pml4[pml4_idx(page)];
        if (!(e1 & VMM_PRESENT)) return false;
        uint64_t *pdpt = (uint64_t *)phys_to_virt(e1 & ~0xFFFULL);

        uint64_t e2 = pdpt[pdpt_idx(page)];
        if (!(e2 & VMM_PRESENT)) return false;

        if (e2 & VMM_HUGE) {
            return false;
        }

        uint64_t *pd = (uint64_t *)phys_to_virt(e2 & ~0xFFFULL);
        uint64_t e3 = pd[pd_idx(page)];
        if (!(e3 & VMM_PRESENT)) return false;

        if (e3 & VMM_HUGE) {
            uint64_t new_e3 = (e3 & ~(VMM_PCD | VMM_PAT_HUGE)) | VMM_PWT;
            pd[pd_idx(page)] = new_e3;
            invlpg(page);
            page = (page & ~0x1FFFFFULL) + 0x200000ULL;
            continue;
        }

        uint64_t *pt = (uint64_t *)phys_to_virt(e3 & ~0xFFFULL);
        uint64_t e4 = pt[pt_idx(page)];
        if (!(e4 & VMM_PRESENT)) return false;

        uint64_t new_e4 = (e4 & ~(VMM_PCD | VMM_PAT_4K)) | VMM_PWT;
        pt[pt_idx(page)] = new_e4;
        invlpg(page);
        page += PAGE_SIZE;
    }

    return true;
}

void vmm_init(void) {
    kernel_pagemap = vmm_create_pagemap();
    if (!kernel_pagemap) return;

    uint64_t hhdm_bytes = pmm_get_highest_phys();
    hhdm_bytes = (hhdm_bytes + 0x1FFFFF) & ~0x1FFFFFULL;
    if (hhdm_bytes < 0x100000000ULL) hhdm_bytes = 0x100000000ULL;
    uint64_t hhdm_pages = hhdm_bytes / 0x200000ULL;

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

    vmm_switch(kernel_pagemap);
}