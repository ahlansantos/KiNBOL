#include "heap.h"
#include "pmm.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <libk/string.h>

typedef struct block {
    size_t        size;
    bool          used;
    uint32_t      magic;
    struct block *next;
    struct block *prev;
} block_t;

#define HEAP_MAGIC      0xDEADC0DE
#define HEAP_FREED      0xFEEFEEFE
#define HEAP_MIN_SPLIT  (sizeof(block_t) + 16)

static block_t *free_list      = NULL;
static uint32_t g_pages        = 0;
static uint32_t g_used_bytes   = 0;
static uint32_t g_used_blocks  = 0;

typedef struct {
    bool     in_use;
    void    *virt_base;
    void    *phys_base;
    uint32_t npages;
} heap_group_t;

#define HEAP_MAX_GROUPS 1024
static heap_group_t g_groups[HEAP_MAX_GROUPS];

static void heap_register_group(void *virt, void *phys, uint32_t npages) {
    for (int i = 0; i < HEAP_MAX_GROUPS; i++) {
        if (!g_groups[i].in_use) {
            g_groups[i].in_use    = true;
            g_groups[i].virt_base = virt;
            g_groups[i].phys_base = phys;
            g_groups[i].npages    = npages;
            return;
        }
    }
}

static void heap_try_reclaim(block_t *blk) {
    uint8_t *blk_start = (uint8_t *)blk;
    uint8_t *blk_end   = blk_start + sizeof(block_t) + blk->size;

    uint64_t covered = 0;
    int      idx[HEAP_MAX_GROUPS];
    int      nidx = 0;

    for (int i = 0; i < HEAP_MAX_GROUPS; i++) {
        if (!g_groups[i].in_use) continue;
        uint8_t *g_start = (uint8_t *)g_groups[i].virt_base;
        uint8_t *g_end   = g_start + (uint64_t)g_groups[i].npages * PAGE_SIZE;
        if (g_start >= blk_start && g_end <= blk_end) {
            idx[nidx++] = i;
            covered += (uint64_t)g_groups[i].npages * PAGE_SIZE;
        }
    }

    if (nidx == 0 || covered != (uint64_t)(blk_end - blk_start)) {
        return;
    }

    if (blk->prev) blk->prev->next = blk->next;
    if (blk->next) blk->next->prev = blk->prev;
    if (free_list == blk) free_list = blk->next;
    blk->magic = HEAP_FREED;

    for (int i = 0; i < nidx; i++) {
        heap_group_t *g = &g_groups[idx[i]];
        pmm_free_pages(g->phys_base, g->npages);
        g_pages -= g->npages;
        g->in_use = false;
    }
}

static block_t *heap_new_block(size_t min_size) {
    uint64_t need = (uint64_t)min_size + sizeof(block_t);
    uint64_t pages = (need + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages < 1) pages = 1;

    void *phys;
    if (pages == 1) {
        phys = pmm_alloc_page();
    } else {
        phys = pmm_alloc_pages_contiguous(pages);
    }
    if (!phys) return NULL;
    g_pages += (uint32_t)pages;

    block_t *blk = (block_t *)pmm_phys_to_virt((uint64_t)phys);
    blk->size  = (size_t)(pages * PAGE_SIZE - sizeof(block_t));
    blk->used  = false;
    blk->magic = HEAP_MAGIC;
    blk->next  = NULL;
    blk->prev  = NULL;

    heap_register_group(blk, phys, (uint32_t)pages);
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

static bool heap_addr_adjacent(block_t *a, block_t *b) {
    return (uint8_t *)a + sizeof(block_t) + a->size == (uint8_t *)b;
}

static block_t *heap_coalesce(block_t *blk) {
    while (blk->next && !blk->next->used && heap_addr_adjacent(blk, blk->next)) {
        block_t *next = blk->next;
        blk->size += sizeof(block_t) + next->size;
        blk->next  = next->next;
        if (next->next) next->next->prev = blk;
        next->magic = HEAP_FREED;
    }
    while (blk->prev && !blk->prev->used && heap_addr_adjacent(blk->prev, blk)) {
        block_t *prev = blk->prev;
        prev->size += sizeof(block_t) + blk->size;
        prev->next  = blk->next;
        if (blk->next) blk->next->prev = prev;
        blk->magic = HEAP_FREED;
        blk = prev;
    }
    return blk;
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    size = (size + 15) & ~(size_t)15;

    if (!free_list) {
        free_list = heap_new_block(size);
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

    block_t *new_blk = heap_new_block(size);
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
    if (ptr) memset(ptr, 0, total);
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
    memcpy(new_ptr, ptr, blk->size);
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

    block_t *merged = heap_coalesce(blk);
    heap_try_reclaim(merged);
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