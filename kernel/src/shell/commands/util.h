/*
 * Header for shell utilities: tick count, simple string length, and the
 * standardized header print used by several commands.
 */
#pragma once

#include <stdint.h>

/* Terminal color palette — brighter, more saturated */
#define COLOR_HEADER    0x00FFFF   /* cyan  — section titles       */
#define COLOR_SUCCESS   0x44FF88   /* green — OK / done            */
#define COLOR_ERROR     0xFF4444   /* red   — errors / panics      */
#define COLOR_WARNING   0xFFCC00   /* amber — warnings             */
#define COLOR_BODY      0xCCCCCC   /* light grey — normal text     */
#define COLOR_HIGHLIGHT 0x55FFAA   /* mint  — values / numbers     */
#define COLOR_ACCENT    0x66BBFF   /* sky blue — labels / fields   */
#define COLOR_DIM       0x666688   /* muted purple-grey — borders  */
#define COLOR_WHITE     0xFFFFFF
#define COLOR_PROMPT    0x55FF55   /* bright green — shell prompt  */
#define COLOR_CMD       0xFFFFAA   /* yellow — command text        */

uint32_t get_ticks(void);
int str_len(const char *s);

void print_header(const char *title);
void print_separator(void);
void print_field(const char *label, const char *value);

void calc_set_input(const char *expr);
uint64_t calc_expr(void);