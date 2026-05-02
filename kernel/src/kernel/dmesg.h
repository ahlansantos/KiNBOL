#pragma once
#include <stdint.h>

#define DMESG_BUF_SIZE 4096  

void dmesg_init(void);

void dmesg(const char *msg);

void dmesg_int(uint32_t n);

void dmesg_hex(uint64_t n);

void dmesg_foreach(void (*cb)(char c));

uint32_t dmesg_len(void);

void dmesg_clear(void);