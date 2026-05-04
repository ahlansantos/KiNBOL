/* BareGL (SR) Headers 0.2 */
#pragma once
#include <stdint.h>
#include <limine.h>

void bare_init(struct limine_framebuffer *fb);

void bare_pixel(int x, int y, uint32_t color);
void bare_rect(int x, int y, int w, int h, uint32_t color);
void bare_rect_fill(int x, int y, int w, int h, uint32_t color);
void bare_line(int x0, int y0, int x1, int y1, uint32_t color);
void bare_circle(int cx, int cy, int r, uint32_t color);
void bare_circle_fill(int cx, int cy, int r, uint32_t color);
void bare_clear(uint32_t color);
void bare_sync_from_fb(void);
void bare_flip(void);

int bare_bmp_draw(int x, int y, const uint8_t *bmp_data, uint32_t bmp_size);

uint32_t bare_rgb(uint8_t r, uint8_t g, uint8_t b);
int      bare_width(void);
int      bare_height(void);