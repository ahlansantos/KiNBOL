#pragma once
#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#define PAGE_SIZE 4096

void     pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);
void    *pmm_alloc_page(void);
void    *pmm_alloc_pages_contiguous(uint64_t count);
void     pmm_free_page(void *phys);
void     pmm_free_pages(void *phys, uint64_t count);
uint64_t pmm_get_free_page_count(void);
uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_highest_phys(void);
uint64_t pmm_phys_to_virt(uint64_t phys);