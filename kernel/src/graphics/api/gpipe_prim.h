#pragma once
#include <stdint.h>
#include "gpipe.h"

typedef enum {
    GPIPE_BMP_OK = 0,
    GPIPE_BMP_ERR_NO_CTX     = -1,
    GPIPE_BMP_ERR_TOO_SMALL  = -2,
    GPIPE_BMP_ERR_BAD_MAGIC  = -3,
    GPIPE_BMP_ERR_BAD_BPP    = -4,
    GPIPE_BMP_ERR_OVERFLOW   = -5,
} gpipe_bmp_status_t;

void gpipe_clear(gpipe_ctx_t *ctx, uint32_t color);

void gpipe_pixel(gpipe_ctx_t *ctx, int x, int y, uint32_t color);
void gpipe_pixel_blend(gpipe_ctx_t *ctx, int x, int y, uint32_t src_argb);

void gpipe_rect(gpipe_ctx_t *ctx, int x, int y, int w, int h, uint32_t color);
void gpipe_rect_fill(gpipe_ctx_t *ctx, int x, int y, int w, int h, uint32_t color);

void gpipe_line(gpipe_ctx_t *ctx, int x0, int y0, int x1, int y1, uint32_t color);

void gpipe_circle(gpipe_ctx_t *ctx, int cx, int cy, int r, uint32_t color);
void gpipe_circle_fill(gpipe_ctx_t *ctx, int cx, int cy, int r, uint32_t color);

int gpipe_bmp_draw(gpipe_ctx_t *ctx, int x, int y, const uint8_t *bmp_data, uint32_t bmp_size);

void gpipe_text(gpipe_ctx_t *ctx, int x, int y, const char *s, uint32_t fg, uint32_t bg);
int  gpipe_text_width(const char *s);

void gpipe_text_scaled(gpipe_ctx_t *ctx, int x, int y, const char *s,
                       uint32_t fg, uint32_t bg, float scale);
int  gpipe_text_scaled_width(const char *s, float scale);

#define GPIPE_TEXT_TRANSPARENT 0xFF000001