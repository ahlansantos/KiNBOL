#pragma once

#include <stddef.h>

extern void fpu_enable(void);

void fpu_make_default_state(void *out, size_t n);

int fpu_ready(void);

void fpu_report(void);