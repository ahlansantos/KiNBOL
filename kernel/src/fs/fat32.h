#pragma once
#include <stdint.h>

int  fat32_detect(void);

void fat32_init(void);
int  fat32_present(void);