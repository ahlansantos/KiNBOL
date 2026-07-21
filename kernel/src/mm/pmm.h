<<<<<<< HEAD
/*
 * Header for the PMM: init from the Limine memmap, allocate/free a physical
 * page, and convert physical to virtual through the HHDM.
 */
=======
>>>>>>> origin/x86_64-uefi
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#define PAGE_SIZE 4096

void     pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);
void    *pmm_alloc_page(void);
void     pmm_free_page(void *phys);
uint64_t pmm_get_free_page_count(void);
uint64_t pmm_phys_to_virt(uint64_t phys);