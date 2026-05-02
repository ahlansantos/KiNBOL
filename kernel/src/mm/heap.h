#pragma once
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096

void *kmalloc(size_t size);
void *kcalloc(size_t num, size_t size); 
void *krealloc(void *ptr, size_t size); 
void  kfree(void *ptr);
void  kfree_sized(void *ptr, size_t size);

typedef struct {
    uint32_t total_blocks;
    uint32_t used_blocks;
    uint32_t free_blocks;
    uint32_t total_bytes;
    uint32_t used_bytes;
    uint32_t free_bytes;
    uint32_t pages_allocated;
} heap_stats_t;

heap_stats_t heap_get_stats(void);
uint32_t     kmalloc_total_allocated(void);
uint32_t     kmalloc_free_space(void);
void         kmalloc_dump_stats(void);