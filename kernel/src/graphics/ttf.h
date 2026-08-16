#ifndef TTF_H
#define TTF_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t         size;

    uint32_t glyf_off, glyf_len;
    uint32_t loca_off, loca_len;
    uint32_t cmap_off;
    uint32_t hmtx_off;
    uint32_t hhea_off;
    uint32_t head_off;
    uint32_t maxp_off;

    int loca_long;
    int num_glyphs;
    int units_per_em;
    int num_hmetrics;

    uint32_t cmap_subtable_off;
    int      cmap_format;
} ttf_font_t;

typedef struct {
    uint8_t *pixels;
    int      w, h;
    int      xoff, yoff;
    int      advance_px;
} ttf_bitmap_t;

typedef void *(*ttf_alloc_fn)(size_t size);
typedef void  (*ttf_free_fn)(void *ptr);

int ttf_init(ttf_font_t *f, const uint8_t *data, size_t size);

int ttf_find_glyph_index(const ttf_font_t *f, uint32_t codepoint);

float ttf_scale_for_pixel_height(const ttf_font_t *f, float pixel_height);

int ttf_rasterize_glyph(const ttf_font_t *f, int glyph_index, float scale,
                         ttf_alloc_fn alloc, ttf_free_fn freefn,
                         ttf_bitmap_t *out);

int ttf_rasterize_codepoint(const ttf_font_t *f, uint32_t codepoint, float pixel_height,
                             ttf_alloc_fn alloc, ttf_free_fn freefn,
                             ttf_bitmap_t *out);

#endif