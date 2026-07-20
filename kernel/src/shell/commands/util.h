#pragma once

#include <stdint.h>

#define COLOR_HEADER    0x00FFFF
#define COLOR_SUCCESS   0x00FF00
#define COLOR_ERROR     0xFF0000
#define COLOR_WARNING   0xFFAA00
#define COLOR_BODY      0xDDDDDD
#define COLOR_HIGHLIGHT 0x88CC88
#define COLOR_ACCENT    0x88AACC
#define COLOR_DIM       0x555555
#define COLOR_WHITE     0xFFFFFF

uint32_t get_ticks(void);
int str_len(const char *s);

void print_header(const char *title);
void print_separator(void);
void print_field(const char *label, const char *value);

void calc_set_input(const char *expr);
uint64_t calc_expr(void);