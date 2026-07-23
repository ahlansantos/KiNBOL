#include "gpipe_prim.h"
#include <stddef.h>

static inline int iabs(int x) { return x < 0 ? -x : x; }

static inline int iclamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static bool clip_rect(gpipe_ctx_t *ctx, int x, int y, int w, int h,
                       int *ox, int *oy, int *ow, int *oh) {
    if (!ctx || w <= 0 || h <= 0) return false;

    int x0 = iclamp(x, 0, (int)ctx->width);
    int y0 = iclamp(y, 0, (int)ctx->height);
    int x1 = iclamp(x + w, 0, (int)ctx->width);
    int y1 = iclamp(y + h, 0, (int)ctx->height);

    if (x1 <= x0 || y1 <= y0) return false;

    *ox = x0; *oy = y0; *ow = x1 - x0; *oh = y1 - y0;
    return true;
}

void gpipe_pixel(gpipe_ctx_t *ctx, int x, int y, uint32_t color) {
    if (!ctx || !ctx->back) return;
    if ((unsigned)x >= ctx->width || (unsigned)y >= ctx->height) return;

    ctx->back[(uint32_t)y * ctx->pw + (uint32_t)x] = color;
    gpipe_mark_dirty(ctx, x, y, 1, 1);
}

void gpipe_pixel_blend(gpipe_ctx_t *ctx, int x, int y, uint32_t src) {
    if (!ctx || !ctx->back) return;
    if ((unsigned)x >= ctx->width || (unsigned)y >= ctx->height) return;

    uint32_t *dstp = &ctx->back[(uint32_t)y * ctx->pw + (uint32_t)x];
    uint32_t dst = *dstp;

    uint8_t sa = src >> 24;
    uint8_t sr = (src >> 16) & 0xFF;
    uint8_t sg = (src >> 8) & 0xFF;
    uint8_t sb = src & 0xFF;

    uint8_t dr = (dst >> 16) & 0xFF;
    uint8_t dg = (dst >> 8) & 0xFF;
    uint8_t db = dst & 0xFF;

    uint8_t r = (uint8_t)((sr * sa + dr * (255 - sa)) / 255);
    uint8_t g = (uint8_t)((sg * sa + dg * (255 - sa)) / 255);
    uint8_t b = (uint8_t)((sb * sa + db * (255 - sa)) / 255);

    *dstp = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    gpipe_mark_dirty(ctx, x, y, 1, 1);
}

void gpipe_clear(gpipe_ctx_t *ctx, uint32_t color) {
    if (!ctx || !ctx->back) return;

    for (uint32_t y = 0; y < ctx->height; y++)
        for (uint32_t x = 0; x < ctx->pw; x++)
            ctx->back[y * ctx->pw + x] = color;

    gpipe_mark_dirty(ctx, 0, 0, (int)ctx->width, (int)ctx->height);
}

void gpipe_rect(gpipe_ctx_t *ctx, int x, int y, int w, int h, uint32_t color) {
    if (!ctx || w <= 0 || h <= 0) return;
    for (int i = x; i < x + w; i++) {
        gpipe_pixel(ctx, i, y, color);
        gpipe_pixel(ctx, i, y + h - 1, color);
    }
    for (int i = y; i < y + h; i++) {
        gpipe_pixel(ctx, x, i, color);
        gpipe_pixel(ctx, x + w - 1, i, color);
    }
}

void gpipe_rect_fill(gpipe_ctx_t *ctx, int x, int y, int w, int h, uint32_t color) {
    int cx, cy, cw, ch;
    if (!clip_rect(ctx, x, y, w, h, &cx, &cy, &cw, &ch)) return;

    for (int row = cy; row < cy + ch; row++) {
        uint32_t *ptr = &ctx->back[(uint32_t)row * ctx->pw + (uint32_t)cx];
        for (int col = 0; col < cw; col++)
            ptr[col] = color;
    }

    gpipe_mark_dirty(ctx, cx, cy, cw, ch);
}

void gpipe_line(gpipe_ctx_t *ctx, int x0, int y0, int x1, int y1, uint32_t color) {
    if (!ctx) return;

    int dx = iabs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -iabs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        gpipe_pixel(ctx, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gpipe_circle(gpipe_ctx_t *ctx, int cx, int cy, int r, uint32_t color) {
    if (!ctx || r < 0) return;

    int x = r, y = 0;
    int err = 0;

    while (x >= y) {
        gpipe_pixel(ctx, cx + x, cy + y, color);
        gpipe_pixel(ctx, cx + y, cy + x, color);
        gpipe_pixel(ctx, cx - y, cy + x, color);
        gpipe_pixel(ctx, cx - x, cy + y, color);
        gpipe_pixel(ctx, cx - x, cy - y, color);
        gpipe_pixel(ctx, cx - y, cy - x, color);
        gpipe_pixel(ctx, cx + y, cy - x, color);
        gpipe_pixel(ctx, cx + x, cy - y, color);

        y++;
        if (err <= 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void gpipe_circle_fill(gpipe_ctx_t *ctx, int cx, int cy, int r, uint32_t color) {
    if (!ctx || r < 0) return;

    int x = r, y = 0;
    int err = 0;

    while (x >= y) {
        gpipe_rect_fill(ctx, cx - x, cy + y, 2 * x + 1, 1, color);
        gpipe_rect_fill(ctx, cx - x, cy - y, 2 * x + 1, 1, color);
        gpipe_rect_fill(ctx, cx - y, cy + x, 2 * y + 1, 1, color);
        gpipe_rect_fill(ctx, cx - y, cy - x, 2 * y + 1, 1, color);

        y++;
        if (err <= 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

int gpipe_bmp_draw(gpipe_ctx_t *ctx, int x, int y, const uint8_t *bmp_data, uint32_t bmp_size) {
    if (!ctx || !ctx->back) return GPIPE_BMP_ERR_NO_CTX;
    if (!bmp_data || bmp_size < 54) return GPIPE_BMP_ERR_TOO_SMALL;
    if (bmp_data[0] != 'B' || bmp_data[1] != 'M') return GPIPE_BMP_ERR_BAD_MAGIC;

    int32_t img_w    = *(const int32_t *)(bmp_data + 18);
    int32_t img_h_raw = *(const int32_t *)(bmp_data + 22);
    uint16_t bpp     = *(const uint16_t *)(bmp_data + 28);
    uint32_t off     = *(const uint32_t *)(bmp_data + 10);

    if (bpp != 24) return GPIPE_BMP_ERR_BAD_BPP;
    if (img_w <= 0) return GPIPE_BMP_ERR_BAD_MAGIC;

    bool top_down = img_h_raw < 0;
    int32_t img_h = top_down ? -img_h_raw : img_h_raw;
    if (img_h <= 0) return GPIPE_BMP_ERR_BAD_MAGIC;

    int32_t row_sz = (img_w * 3 + 3) & ~3;

    uint64_t needed = (uint64_t)off + (uint64_t)row_sz * (uint64_t)img_h;
    if (needed > bmp_size) return GPIPE_BMP_ERR_OVERFLOW;

    for (int32_t row = 0; row < img_h; row++) {
        int32_t dst_row = top_down ? row : (img_h - 1 - row);
        int32_t py = y + dst_row;
        if (py < 0 || py >= (int32_t)ctx->height) continue;

        const uint8_t *src_row = bmp_data + off + (uint64_t)row * row_sz;

        for (int32_t cx = 0; cx < img_w; cx++) {
            int32_t px = x + cx;
            if (px < 0 || px >= (int32_t)ctx->width) continue;

            uint8_t b = src_row[cx * 3];
            uint8_t g = src_row[cx * 3 + 1];
            uint8_t r = src_row[cx * 3 + 2];
            ctx->back[(uint32_t)py * ctx->pw + (uint32_t)px] = gpipe_rgb(r, g, b);
        }
    }

    gpipe_mark_dirty(ctx, x, y, img_w, img_h);
    return GPIPE_BMP_OK;
}