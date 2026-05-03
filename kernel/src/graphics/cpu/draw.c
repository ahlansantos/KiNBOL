#include "draw.h"
#include <stdint.h>
#include <stddef.h>

static struct limine_framebuffer *d_fb = NULL;
static uint32_t d_pw = 0;

void draw_init(struct limine_framebuffer *fb) {
    d_fb = fb;
    d_pw = fb->pitch / 4;
}

int draw_width(void)  { return d_fb ? (int)d_fb->width  : 0; }
int draw_height(void) { return d_fb ? (int)d_fb->height : 0; }

uint32_t draw_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void draw_pixel(int x, int y, uint32_t color) {
    if (!d_fb) return;
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= d_fb->width || (uint32_t)y >= d_fb->height) return;
    ((volatile uint32_t *)d_fb->address)[y * d_pw + x] = color;
}

void draw_clear(uint32_t color) {
    if (!d_fb) return;
    volatile uint32_t *fb = d_fb->address;
    for (uint32_t y = 0; y < d_fb->height; y++)
        for (uint32_t x = 0; x < d_fb->width; x++)
            fb[y * d_pw + x] = color;
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int i = x; i < x + w; i++) {
        draw_pixel(i, y,         color);
        draw_pixel(i, y + h - 1, color);
    }
    for (int i = y; i < y + h; i++) {
        draw_pixel(x,         i, color);
        draw_pixel(x + w - 1, i, color);
    }
}

void draw_rect_fill(int x, int y, int w, int h, uint32_t color) {
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            draw_pixel(col, row, color);
}

void draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx =  (x1 - x0 < 0 ? -(x1 - x0) : x1 - x0);
    int dy = -(y1 - y0 < 0 ? -(y1 - y0) : y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void draw_circle(int cx, int cy, int r, uint32_t color) {
    int x = r, y = 0;
    int err = 0;
    while (x >= y) {
        draw_pixel(cx + x, cy + y, color);
        draw_pixel(cx + y, cy + x, color);
        draw_pixel(cx - y, cy + x, color);
        draw_pixel(cx - x, cy + y, color);
        draw_pixel(cx - x, cy - y, color);
        draw_pixel(cx - y, cy - x, color);
        draw_pixel(cx + y, cy - x, color);
        draw_pixel(cx + x, cy - y, color);
        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void draw_circle_fill(int cx, int cy, int r, uint32_t color) {
    int x = r, y = 0;
    int err = 0;
    while (x >= y) {
        for (int i = cx - x; i <= cx + x; i++) {
            draw_pixel(i, cy + y, color);
            draw_pixel(i, cy - y, color);
        }
        for (int i = cx - y; i <= cx + y; i++) {
            draw_pixel(i, cy + x, color);
            draw_pixel(i, cy - x, color);
        }
        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}