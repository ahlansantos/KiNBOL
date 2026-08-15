#pragma once

#include <stdint.h>

#define COLOR_HEADER    0x64D2FF
#define COLOR_SUCCESS   0x30D158
#define COLOR_ERROR     0xFF453A
#define COLOR_WARNING   0xFF9F0A
#define COLOR_BODY      0xD1D6E0
#define COLOR_HIGHLIGHT 0x64D2FF
#define COLOR_ACCENT    0x0A84FF
#define COLOR_DIM       0x6E7A93
#define COLOR_WHITE     0xF5F7FA
#define COLOR_PROMPT    0x5E9EFF
#define COLOR_CMD       0xBFA7FF

uint32_t get_ticks(void);
int str_len(const char *s);

void print_header(const char *title);
void print_separator(void);
void print_field(const char *label, const char *value);

void calc_set_input(const char *expr);
uint64_t calc_expr(void);