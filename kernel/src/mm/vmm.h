#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define VMM_PRESENT    (1ULL << 0)
#define VMM_WRITE      (1ULL << 1)
#define VMM_USER       (1ULL << 2)
#define VMM_HUGE       (1ULL << 7)
#define VMM_NX         (1ULL << 63)

#define VMM_FLAGS_KERNEL  (VMM_PRESENT | VMM_WRITE)
#define VMM_FLAGS_USER    (VMM_PRESENT | VMM_WRITE | VMM_USER)
#define VMM_FLAGS_USER_DATA (VMM_PRESENT | VMM_WRITE | VMM_USER | VMM_NX)
#define VMM_FLAGS_USER_CODE (VMM_PRESENT | VMM_USER)
#define VMM_FLAGS_RODATA  (VMM_PRESENT | VMM_NX)
#define VMM_FLAGS_CODE    (VMM_PRESENT)

#define VMM_KERNEL_BASE   0xFFFFFFFF80000000ULL
#define VMM_HHDM_BASE     0xFFFF800000000000ULL

typedef uint64_t *pagemap_t;

void vmm_init(void);

pagemap_t vmm_create_pagemap(void);

void vmm_destroy_pagemap(pagemap_t pm);

void vmm_switch(pagemap_t pm);

pagemap_t vmm_current(void);

int vmm_sync_kernel_entry(uint64_t vaddr);

int vmm_map(pagemap_t pm, uint64_t virt, uint64_t phys, uint64_t flags);

void vmm_unmap(pagemap_t pm, uint64_t virt);

int vmm_protect(pagemap_t pm, uint64_t virt, uint64_t len, uint64_t flags);

void vmm_unmap_range(pagemap_t pm, uint64_t virt, uint64_t len, bool free_phys);

uint64_t vmm_virt_to_phys(pagemap_t pm, uint64_t virt);

int vmm_map_range(pagemap_t pm,
                  uint64_t virt_start, uint64_t phys_start,
                  uint64_t size, uint64_t flags);

bool vmm_check_user_range(pagemap_t pm, uint64_t virt, uint64_t len, bool need_write);

void vmm_enable_writecombine_pat(void);

bool vmm_mark_range_writecombine(uint64_t virt, uint64_t size);

void cpu_security_init(void);
void smap_stac(void);
void smap_clac(void);