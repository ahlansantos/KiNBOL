#include "ramdisk.h"
#include "../mm/heap.h"
#include <stdint.h>

ramfile_t ramfiles[MAX_RAMFILES];
int       ramfile_count = 0;

static int rd_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}
static char *rd_strcpy(char *dst, const char *src) {
    char *d = dst; while ((*d++ = *src++)); return dst;
}
static void *rd_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = dst; const uint8_t *s = src; while (n--) *d++ = *s++; return dst;
}

void ramdisk_init(void) {
    for (int i = 0; i < MAX_RAMFILES; i++) {
        ramfiles[i].name[0] = 0;
        ramfiles[i].data    = 0;
        ramfiles[i].size    = 0;
    }
    ramfile_count = 0;
}

int ramdisk_find(const char *name) {
    for (int i = 0; i < MAX_RAMFILES; i++)
        if (ramfiles[i].name[0] && rd_strcmp(ramfiles[i].name, name) == 0)
            return i;
    return -1;
}

int ramdisk_create(const char *name, const uint8_t *data, uint32_t size) {
    int idx = ramdisk_find(name);
    if (idx >= 0) {
        kfree(ramfiles[idx].data);
    } else {
        idx = -1;
        for (int i = 0; i < MAX_RAMFILES; i++)
            if (!ramfiles[i].name[0]) { idx = i; break; }
        if (idx < 0) return -1;
        rd_strcpy(ramfiles[idx].name, name);
        ramfile_count++;
    }
    ramfiles[idx].data = kmalloc(size + 1);
    if (!ramfiles[idx].data) { ramfiles[idx].name[0] = 0; return -1; }
    rd_memcpy(ramfiles[idx].data, data, size);
    ramfiles[idx].data[size] = 0;
    ramfiles[idx].size = size;
    return 0;
}

int ramdisk_delete(const char *name) {
    int idx = ramdisk_find(name);
    if (idx < 0) return -1;
    kfree(ramfiles[idx].data);
    ramfiles[idx].name[0] = 0;
    ramfiles[idx].data    = 0;
    ramfiles[idx].size    = 0;
    ramfile_count--;
    return 0;
}