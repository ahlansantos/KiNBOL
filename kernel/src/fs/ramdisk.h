#pragma once
#include <stdint.h>

#define MAX_RAMFILES 64

typedef struct {
    char    name[64];
    uint8_t *data;
    uint32_t size;
} ramfile_t;

extern ramfile_t ramfiles[MAX_RAMFILES];
extern int       ramfile_count;

void ramdisk_init(void);
int  ramdisk_find(const char *name);
int  ramdisk_create(const char *name, const uint8_t *data, uint32_t size);
int  ramdisk_delete(const char *name);