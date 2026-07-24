#include "gpipe.h"
#include "../../mm/heap.h"
#include "../../mm/pmm.h"
#include <stddef.h>
#include <stdlib.h>

static gpipe_ctx_t *g_default_ctx = NULL;

static inline void *gmemcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static inline int iclamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

gpipe_ctx_t *gpipe_default(void) {
    return g_default_ctx;
}

void gpipe_set_default(gpipe_ctx_t *ctx) {
    g_default_ctx = ctx;
}

gpipe_ctx_t *gpipe_init(struct limine_framebuffer *fb) {
    if (!fb) return NULL;

    gpipe_ctx_t *ctx = kmalloc(sizeof(gpipe_ctx_t));
    if (!ctx) return NULL;

    ctx->fb = fb;
    ctx->pw = fb->pitch / 4;
    ctx->width = fb->width;
    ctx->height = fb->height;
    ctx->back_size = (size_t)fb->pitch * fb->height;
    ctx->back_pages = (ctx->back_size + PAGE_SIZE - 1) / PAGE_SIZE;
    ctx->dirty = (gpipe_rect_t){0, 0, 0, 0};

    void *phys = pmm_alloc_pages_contiguous(ctx->back_pages);
    if (!phys) {
        kfree(ctx);
        return NULL;
    }
    ctx->back_phys = phys;
    ctx->back = (uint32_t *)pmm_phys_to_virt((uint64_t)phys);

    for (size_t i = 0; i < ctx->back_size / 4; i++)
        ctx->back[i] = 0;

    if (!g_default_ctx)
        g_default_ctx = ctx;

    return ctx;
}

void gpipe_shutdown(gpipe_ctx_t *ctx) {
    if (!ctx) return;

    if (ctx->back) {
        pmm_free_pages(ctx->back_phys, ctx->back_pages);
        ctx->back = NULL;
        ctx->back_phys = NULL;
    }

    if (g_default_ctx == ctx)
        g_default_ctx = NULL;

    kfree(ctx);
}

int gpipe_width(gpipe_ctx_t *ctx)  { return ctx ? (int)ctx->width  : 0; }
int gpipe_height(gpipe_ctx_t *ctx) { return ctx ? (int)ctx->height : 0; }

uint32_t gpipe_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

uint32_t gpipe_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void gpipe_sync_from_fb(gpipe_ctx_t *ctx) {
    if (!ctx || !ctx->fb || !ctx->back) return;

    volatile uint32_t *fb = ctx->fb->address;
    for (uint32_t y = 0; y < ctx->height; y++)
        for (uint32_t x = 0; x < ctx->width; x++)
            ctx->back[y * ctx->pw + x] = fb[y * ctx->pw + x];

    gpipe_mark_dirty(ctx, 0, 0, (int)ctx->width, (int)ctx->height);
}

void gpipe_mark_dirty(gpipe_ctx_t *ctx, int x, int y, int w, int h) {
    if (!ctx || w <= 0 || h <= 0) return;

    int x0 = iclamp(x, 0, (int)ctx->width);
    int y0 = iclamp(y, 0, (int)ctx->height);
    int x1 = iclamp(x + w, 0, (int)ctx->width);
    int y1 = iclamp(y + h, 0, (int)ctx->height);
    if (x1 <= x0 || y1 <= y0) return;

    if (ctx->dirty.w == 0 || ctx->dirty.h == 0) {
        ctx->dirty.x = x0;
        ctx->dirty.y = y0;
        ctx->dirty.w = x1 - x0;
        ctx->dirty.h = y1 - y0;
        return;
    }

    int dx0 = ctx->dirty.x;
    int dy0 = ctx->dirty.y;
    int dx1 = ctx->dirty.x + ctx->dirty.w;
    int dy1 = ctx->dirty.y + ctx->dirty.h;

    if (x0 < dx0) dx0 = x0;
    if (y0 < dy0) dy0 = y0;
    if (x1 > dx1) dx1 = x1;
    if (y1 > dy1) dy1 = y1;

    ctx->dirty.x = dx0;
    ctx->dirty.y = dy0;
    ctx->dirty.w = dx1 - dx0;
    ctx->dirty.h = dy1 - dy0;
}

static void gpipe_flip_rect(gpipe_ctx_t *ctx, int x, int y, int w, int h) {
    volatile uint32_t *fb = ctx->fb->address;

    for (int row = y; row < y + h; row++) {
        gmemcpy(
            (void *)(fb + (uint32_t)row * ctx->pw + x),
            (void *)(ctx->back + (uint32_t)row * ctx->pw + x),
            (size_t)w * 4
        );
    }
}

void gpipe_flip(gpipe_ctx_t *ctx) {
    if (!ctx || !ctx->fb || !ctx->back) return;
    if (ctx->dirty.w <= 0 || ctx->dirty.h <= 0) return;

    gpipe_flip_rect(ctx, ctx->dirty.x, ctx->dirty.y, ctx->dirty.w, ctx->dirty.h);
    ctx->dirty = (gpipe_rect_t){0, 0, 0, 0};
}

void gpipe_flip_full(gpipe_ctx_t *ctx) {
    if (!ctx || !ctx->fb || !ctx->back) return;
    gpipe_flip_rect(ctx, 0, 0, (int)ctx->width, (int)ctx->height);
    ctx->dirty = (gpipe_rect_t){0, 0, 0, 0};
}

static void gpipe_set_pixel(gpipe_ctx_t *ctx, int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)ctx->width || y < 0 || y >= (int)ctx->height) return;
    ctx->back[y * ctx->pw + x] = color;
}

void gpipe_rect(gpipe_ctx_t *ctx, int x, int y, int w, int h, uint32_t color) {
    if (!ctx || w <= 0 || h <= 0) return;
    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            gpipe_set_pixel(ctx, col, row, color);
        }
    }
    gpipe_mark_dirty(ctx, x, y, w, h);
}

void gpipe_circle(gpipe_ctx_t *ctx, int cx, int cy, int r, uint32_t color) {
    if (!ctx || r <= 0) return;
    int x = 0, y = r, d = 3 - 2 * r;
    while (x <= y) {
        gpipe_set_pixel(ctx, cx + x, cy + y, color);
        gpipe_set_pixel(ctx, cx - x, cy + y, color);
        gpipe_set_pixel(ctx, cx + x, cy - y, color);
        gpipe_set_pixel(ctx, cx - x, cy - y, color);
        gpipe_set_pixel(ctx, cx + y, cy + x, color);
        gpipe_set_pixel(ctx, cx - y, cy + x, color);
        gpipe_set_pixel(ctx, cx + y, cy - x, color);
        gpipe_set_pixel(ctx, cx - y, cy - x, color);
        if (d < 0) {
            d = d + 4 * x + 6;
        } else {
            d = d + 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
    gpipe_mark_dirty(ctx, cx - r, cy - r, r * 2, r * 2);
}

void gpipe_line(gpipe_ctx_t *ctx, int x0, int y0, int x1, int y1, uint32_t color) {
    if (!ctx) return;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2;
    while (1) {
        gpipe_set_pixel(ctx, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
    gpipe_mark_dirty(ctx, x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, abs(x1 - x0) + 1, abs(y1 - y0) + 1);
}

void gpipe_text(gpipe_ctx_t *ctx, int x, int y, const char *text, uint32_t color) {
    if (!ctx || !text) return;
    extern const unsigned char font[256][16];
    int start_x = x;
    for (int i = 0; text[i]; i++) {
        if (text[i] == '\n') {
            x = start_x;
            y += 16;
            continue;
        }
        if (text[i] == '\b') {
            x -= 8;
            continue;
        }
        const unsigned char *glyph = font[(unsigned char)text[i]];
        for (int row = 0; row < 16; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (1 << (7 - col))) {
                    gpipe_set_pixel(ctx, x + col, y + row, color);
                }
            }
        }
        x += 8;
    }
    gpipe_mark_dirty(ctx, start_x, y, x - start_x, 16);
}
