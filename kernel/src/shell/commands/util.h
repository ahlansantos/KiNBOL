#pragma once

#include <stdint.h>

#define COLOR_HEADER    0x00CCFF
#define COLOR_SUCCESS   0x00FF66
#define COLOR_ERROR     0xFF2222
#define COLOR_WARNING   0xFFAA00
#define COLOR_BODY      0xBBBBBB
#define COLOR_HIGHLIGHT 0x00FFAA
#define COLOR_ACCENT    0x3399FF
#define COLOR_DIM       0x555577
#define COLOR_WHITE     0xFFFFFF
#define COLOR_PROMPT    0x00FF44
#define COLOR_CMD       0xFFDD66

uint32_t get_ticks(void);
int str_len(const char *s);

void print_header(const char *title);
void print_separator(void);
void print_field(const char *label, const char *value);

void calc_set_input(const char *expr);
uint64_t calc_expr(void);