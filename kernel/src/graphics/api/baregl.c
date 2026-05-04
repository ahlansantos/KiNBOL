/* BareGL (SR) 0.2! */
#include "baregl.h"
#include "../../mm/heap.h"
#include <stdint.h>
#include <stddef.h>

#define MAX_FB_SIZE (1280 * 800 * 4)

static struct limine_framebuffer *d_fb = NULL;
static uint8_t back_storage[MAX_FB_SIZE];
static uint32_t *d_back = NULL;
static uint32_t d_pw = 0;

void bare_sync_from_fb(void) {
    if (!d_fb || !d_back) return;
    volatile uint32_t *fb = d_fb->address;
    for (uint32_t y = 0; y < d_fb->height; y++) {
        for (uint32_t x = 0; x < d_fb->width; x++) {
            d_back[y * d_pw + x] = fb[y * d_pw + x];
        }
    }
}

static inline void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static inline int abs(int x) {
    return x < 0 ? -x : x;
}

void bare_init(struct limine_framebuffer *fb) {
    d_fb = fb;
    d_pw = fb->pitch / 4;
    d_back = (uint32_t *)back_storage;
}

int bare_width(void)  { return d_fb ? (int)d_fb->width  : 0; }
int bare_height(void) { return d_fb ? (int)d_fb->height : 0; }

uint32_t bare_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

uint32_t bare_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8) | b;
}

void bare_pixel(int x, int y, uint32_t color) {
    if (!d_fb || !d_back) return;
    if ((unsigned)x >= d_fb->width || (unsigned)y >= d_fb->height) return;
    d_back[y * d_pw + x] = color;
}

void bare_pixel_blend(int x, int y, uint32_t src) {
    if (!d_fb || !d_back) return;
    if ((unsigned)x >= d_fb->width || (unsigned)y >= d_fb->height) return;

    uint32_t *dstp = &d_back[y * d_pw + x];
    uint32_t dst = *dstp;

    uint8_t sa = src >> 24;
    uint8_t sr = (src >> 16) & 0xFF;
    uint8_t sg = (src >> 8) & 0xFF;
    uint8_t sb = src & 0xFF;

    uint8_t dr = (dst >> 16) & 0xFF;
    uint8_t dg = (dst >> 8) & 0xFF;
    uint8_t db = dst & 0xFF;

    uint8_t r = (sr * sa + dr * (255 - sa)) / 255;
    uint8_t g = (sg * sa + dg * (255 - sa)) / 255;
    uint8_t b = (sb * sa + db * (255 - sa)) / 255;

    *dstp = (r << 16) | (g << 8) | b;
}

void bare_flip(void) {
    if (!d_fb || !d_back) return;

    volatile uint32_t *fb = d_fb->address;

    for (uint32_t y = 0; y < d_fb->height; y++) {
        memcpy(
            (void*)(fb + y * d_pw),
            (void*)(d_back + y * d_pw),
            d_fb->pitch
        );
    }
}

void bare_clear(uint32_t color) {
    if (!d_back) return;

    for (uint32_t y = 0; y < d_fb->height; y++) {
        for (uint32_t x = 0; x < d_pw; x++) {
            d_back[y * d_pw + x] = color;
        }
    }
}

void bare_rect(int x, int y, int w, int h, uint32_t color) {
    if (x >= bare_width() || y >= bare_height()) return;
    if (x + w < 0 || y + h < 0) return;

    for (int i = x; i < x + w; i++) {
        bare_pixel(i, y, color);
        bare_pixel(i, y + h - 1, color);
    }
    for (int i = y; i < y + h; i++) {
        bare_pixel(x, i, color);
        bare_pixel(x + w - 1, i, color);
    }
}

void bare_rect_fill(int x, int y, int w, int h, uint32_t color) {
    if (x >= bare_width() || y >= bare_height()) return;
    if (x + w < 0 || y + h < 0) return;

    for (int row = y; row < y + h; row++) {
        uint32_t *ptr = &d_back[row * d_pw + x];
        for (int col = 0; col < w; col++) {
            ptr[col] = color;
        }
    }
}

void bare_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        bare_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void bare_circle(int cx, int cy, int r, uint32_t color) {
    int x = r, y = 0;
    int err = 0;

    while (x >= y) {
        bare_pixel(cx + x, cy + y, color);
        bare_pixel(cx + y, cy + x, color);
        bare_pixel(cx - y, cy + x, color);
        bare_pixel(cx - x, cy + y, color);
        bare_pixel(cx - x, cy - y, color);
        bare_pixel(cx - y, cy - x, color);
        bare_pixel(cx + y, cy - x, color);
        bare_pixel(cx + x, cy - y, color);

        y++;
        if (err <= 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void bare_circle_fill(int cx, int cy, int r, uint32_t color) {
    int x = r, y = 0;
    int err = 0;

    while (x >= y) {
        for (int i = cx - x; i <= cx + x; i++) {
            bare_pixel(i, cy + y, color);
            bare_pixel(i, cy - y, color);
        }
        for (int i = cx - y; i <= cx + y; i++) {
            bare_pixel(i, cy + x, color);
            bare_pixel(i, cy - x, color);
        }

        y++;
        if (err <= 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

int bare_bmp_draw(int x, int y, const uint8_t *bmp_data, uint32_t bmp_size) {
    if (!d_fb || !d_back) return -1;
    if (bmp_size < 54) return -2;
    if (bmp_data[0] != 'B' || bmp_data[1] != 'M') return -3;

    int32_t img_w = *(int32_t*)(bmp_data + 18);
    int32_t img_h = *(int32_t*)(bmp_data + 22);
    uint16_t bpp  = *(uint16_t*)(bmp_data + 28);
    uint32_t off  = *(uint32_t*)(bmp_data + 10);
    int32_t row_sz = (img_w * 3 + 3) & ~3;

    if (bpp != 24) return -4;
    if (off + (uint32_t)(row_sz * img_h) > bmp_size) return -5;

    for (int32_t cy = 0; cy < img_h; cy++) {
        if (y + (img_h - 1 - cy) >= (int32_t)d_fb->height || y + (img_h - 1 - cy) < 0) continue;
        const uint8_t *row = bmp_data + off + cy * row_sz;
        for (int32_t cx = 0; cx < img_w; cx++) {
            if (x + cx >= (int32_t)d_fb->width || x + cx < 0) continue;
            uint8_t b = row[cx * 3];
            uint8_t g = row[cx * 3 + 1];
            uint8_t r = row[cx * 3 + 2];
            d_back[(y + (img_h - 1 - cy)) * d_pw + (x + cx)] = bare_rgb(r, g, b);
        }
    }
    return 0;
}