#pragma once
#include <stdint.h>

int  fat32_detect(void);

void fat32_init(void);
int  fat32_present(void);
int  fat32_create_file(const char *name);
int  fat32_mkdir(const char *name);