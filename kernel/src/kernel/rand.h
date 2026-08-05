#pragma once
#include <stdint.h>

void krand_init(void);

uint64_t krand_u64(void);

uint64_t krand_below(uint64_t bound);

uint64_t krand_page_aligned_below(uint64_t bound);