#pragma once
#include <stdint.h>
#include <limine.h>

void terminal_init(struct limine_framebuffer *fb);

uint64_t terminal_lock(void);
void terminal_unlock(uint64_t flags);

void terminal_set_fg(uint32_t color);
void terminal_set_bg(uint32_t color);
uint32_t terminal_get_fg(void);

void terminal_set_scale(uint32_t scale);
uint32_t terminal_get_scale(void);

void terminal_putchar(char c);
void terminal_print(const char *s);
void terminal_println(const char *s);
void terminal_ensure_newline(void);
void terminal_print_int(uint32_t n);
void terminal_print_hex(uint64_t n);
void terminal_clear(void);

void terminal_cursor_draw(int visible);