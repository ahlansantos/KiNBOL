#pragma once

#include <stdint.h>

#define COLOR_HEADER    0x7DD3FC
#define COLOR_SUCCESS   0x34D399
#define COLOR_ERROR     0xF87171
#define COLOR_WARNING   0xFBBF24
#define COLOR_BODY      0xD4D8E2
#define COLOR_HIGHLIGHT 0x93C5FD
#define COLOR_ACCENT    0x60A5FA
#define COLOR_DIM       0x8B95A8
#define COLOR_WHITE     0xF8FAFC
#define COLOR_PROMPT    0x818CF8
#define COLOR_CMD       0xC4B5FD

uint32_t get_ticks(void);
int str_len(const char *s);

void print_header(const char *title);
void print_separator(void);
void print_field(const char *label, const char *value);

void calc_set_input(const char *expr);
uint64_t calc_expr(void);