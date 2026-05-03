#pragma once
#include <stdint.h>
#include <limine.h>

void draw_init(struct limine_framebuffer *fb);

void draw_pixel(int x, int y, uint32_t color);
void draw_rect(int x, int y, int w, int h, uint32_t color);
void draw_rect_fill(int x, int y, int w, int h, uint32_t color);
void draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void draw_circle(int cx, int cy, int r, uint32_t color);
void draw_circle_fill(int cx, int cy, int r, uint32_t color);
void draw_clear(uint32_t color);

/* Utilitários */
uint32_t draw_rgb(uint8_t r, uint8_t g, uint8_t b);
int      draw_width(void);
int      draw_height(void);