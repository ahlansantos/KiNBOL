#include "heap.h"
#include "pmm.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct block {
    size_t        size;  
    bool          used;
    uint32_t      magic;  
    struct block *next;
    struct block *prev;   
} block_t;

#define HEAP_MAGIC      0xDEADC0DE
#define HEAP_FREED      0xFEEFEEFE
#define HEAP_MIN_SPLIT  (sizeof(block_t) + 8)

static block_t *free_list      = NULL;
static uint32_t g_pages        = 0;
static uint32_t g_used_bytes   = 0;
static uint32_t g_used_blocks  = 0;

static void heap_memset(void *dst, uint8_t val, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = val;
}
static void heap_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static block_t *heap_new_page(void) {
    void *phys = pmm_alloc_page();
    if (!phys) return NULL;
    g_pages++;

    block_t *blk = (block_t *)pmm_phys_to_virt((uint64_t)phys);
    blk->size  = PAGE_SIZE - sizeof(block_t);
    blk->used  = false;
    blk->magic = HEAP_MAGIC;
    blk->next  = NULL;
    blk->prev  = NULL;
    return blk;
}

static void heap_split(block_t *blk, size_t size) {
    if (blk->size < size + HEAP_MIN_SPLIT) return;

    block_t *split  = (block_t *)((uint8_t *)blk + sizeof(block_t) + size);
    split->size     = blk->size - size - sizeof(block_t);
    split->used     = false;
    split->magic    = HEAP_MAGIC;
    split->next     = blk->next;
    split->prev     = blk;

    if (blk->next) blk->next->prev = split;
    blk->next = split;
    blk->size = size;
}

static void heap_coalesce(block_t *blk) {
    while (blk->next && !blk->next->used) {
        block_t *next = blk->next;
        blk->size += sizeof(block_t) + next->size;
        blk->next  = next->next;
        if (next->next) next->next->prev = blk;
        next->magic = HEAP_FREED;
    }
    if (blk->prev && !blk->prev->used) {
        block_t *prev = blk->prev;
        prev->size += sizeof(block_t) + blk->size;
        prev->next  = blk->next;
        if (blk->next) blk->next->prev = prev;
        blk->magic = HEAP_FREED;
    }
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + 7) & ~(size_t)7;

    if (!free_list) {
        free_list = heap_new_page();
        if (!free_list) return NULL;
    }

    block_t *curr = free_list;
    while (curr) {
        if (!curr->used && curr->size >= size) {
            heap_split(curr, size);
            curr->used = true;
            g_used_bytes  += curr->size;
            g_used_blocks++;
            return (void *)((uint8_t *)curr + sizeof(block_t));
        }
        curr = curr->next;
    }

    block_t *new_blk = heap_new_page();
    if (!new_blk) return NULL;

    block_t *tail = free_list;
    while (tail->next) tail = tail->next;
    tail->next    = new_blk;
    new_blk->prev = tail;

    heap_coalesce(new_blk);

    block_t *cur2 = free_list;
    while (cur2) {
        if (!cur2->used && cur2->size >= size) {
            heap_split(cur2, size);
            cur2->used = true;
            g_used_bytes  += cur2->size;
            g_used_blocks++;
            return (void *)((uint8_t *)cur2 + sizeof(block_t));
        }
        cur2 = cur2->next;
    }
    return NULL;
}

void *kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    if (num != 0 && total / num != size) return NULL; 
    void *ptr = kmalloc(total);
    if (ptr) heap_memset(ptr, 0, total);
    return ptr;
}


void *krealloc(void *ptr, size_t size) {
    if (!ptr)    return kmalloc(size);
    if (size == 0) { kfree(ptr); return NULL; }

    block_t *blk = (block_t *)((uint8_t *)ptr - sizeof(block_t));
    if (blk->magic != HEAP_MAGIC) return NULL;

    if (blk->size >= size) return ptr;

    void *new_ptr = kmalloc(size);
    if (!new_ptr) return NULL;
    heap_memcpy(new_ptr, ptr, blk->size);
    kfree(ptr);
    return new_ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;

    block_t *blk = (block_t *)((uint8_t *)ptr - sizeof(block_t));
    if (blk->magic != HEAP_MAGIC) return;

    if (!blk->used) return;

    blk->used = false;
    if (g_used_bytes  >= blk->size) g_used_bytes  -= blk->size;
    if (g_used_blocks > 0)          g_used_blocks--;

    heap_coalesce(blk);
}

void kfree_sized(void *ptr, size_t size) {
    (void)size;
    kfree(ptr);
}

heap_stats_t heap_get_stats(void) {
    heap_stats_t s = {0};
    s.pages_allocated = g_pages;
    s.total_bytes     = g_pages * (PAGE_SIZE - sizeof(block_t));
    s.used_bytes      = g_used_bytes;
    s.free_bytes      = s.total_bytes > s.used_bytes ? s.total_bytes - s.used_bytes : 0;

    block_t *curr = free_list;
    while (curr) {
        s.total_blocks++;
        if (curr->used) s.used_blocks++;
        else            s.free_blocks++;
        curr = curr->next;
    }
    return s;
}

uint32_t kmalloc_total_allocated(void) { return g_used_bytes; }
uint32_t kmalloc_free_space(void) {
    heap_stats_t s = heap_get_stats();
    return s.free_bytes;
}

extern void dmesg(const char *msg);
extern void dmesg_int(uint32_t n);

void kmalloc_dump_stats(void) {
    heap_stats_t s = heap_get_stats();
    dmesg("[heap] pages=");   dmesg_int(s.pages_allocated);
    dmesg(" used_blk=");      dmesg_int(s.used_blocks);
    dmesg(" free_blk=");      dmesg_int(s.free_blocks);
    dmesg(" used_bytes=");    dmesg_int(s.used_bytes);
    dmesg(" free_bytes=");    dmesg_int(s.free_bytes);
    dmesg("\n");
}