/*
 * Header for shell utilities: tick count, simple string length, and the
 * standardized header print used by several commands.
 */
#pragma once

#include <stdint.h>

/* Terminal color palette — deep, high-contrast, no pastels */
#define COLOR_HEADER    0x00CCFF   /* deep cyan — section titles       */
#define COLOR_SUCCESS   0x00FF66   /* emerald green — OK / done        */
#define COLOR_ERROR     0xFF2222   /* bright red — errors / panics     */
#define COLOR_WARNING   0xFFAA00   /* deep amber — warnings            */
#define COLOR_BODY      0xBBBBBB   /* light grey — normal text         */
#define COLOR_HIGHLIGHT 0x00FFAA   /* teal green — values / numbers    */
#define COLOR_ACCENT    0x3399FF   /* deep blue — labels / fields      */
#define COLOR_DIM       0x555577   /* muted indigo — borders           */
#define COLOR_WHITE     0xFFFFFF
#define COLOR_PROMPT    0x00FF44   /* deep green — shell prompt        */
#define COLOR_CMD       0xFFDD66   /* gold — command text              */

uint32_t get_ticks(void);
int str_len(const char *s);

void print_header(const char *title);
void print_separator(void);
void print_field(const char *label, const char *value);

void calc_set_input(const char *expr);
uint64_t calc_expr(void);