#pragma once
#include <stdint.h>

#define FAT32_OK             1
#define FAT32_ERR_DISK       0
#define FAT32_ERR_NO_FS     -1
#define FAT32_ERR_NO_PARENT -2
#define FAT32_ERR_NOT_FOUND -3
#define FAT32_ERR_NOT_EMPTY -4
#define FAT32_ERR_IS_DIR    -5
#define FAT32_ERR_NOT_DIR   -6
#define FAT32_ERR_EXISTS    -7

int  fat32_detect(void);

void fat32_init(void);
int  fat32_present(void);

int  fat32_create_file(const char *path, char *out_fullname);
int  fat32_mkdir(const char *path, char *out_fullname);
int  fat32_remove(const char *path, int recursive, char *out_name);
uint32_t fat32_free_clusters(void);