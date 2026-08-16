#include "font_ttf.h"
#include "ttf.h"
#include "../mm/heap.h"

extern const uint8_t font_ttf_data[];
extern const uint8_t font_ttf_data_end[];

#define BASELINE_ROW FONT_TTF_BASELINE_ROW

static ttf_font_t g_font;
static bool       g_ready = false;

static uint8_t *ui_glyph_cache = NULL;
static uint32_t ui_scale = 1;
static uint32_t ui_cell_w = FONT_TTF_BASE_CELL_W;
static uint32_t ui_cell_h = FONT_TTF_BASE_CELL_H;

static uint8_t *term_glyph_cache = NULL;
static uint32_t term_scale = 1;
static uint32_t term_cell_w = FONT_TTF_BASE_CELL_W;
static uint32_t term_cell_h = FONT_TTF_BASE_CELL_H;

static void clear_cache(uint8_t *cache, int cell_w, int cell_h) {
    for (int i = 0; i < 256; i++)
        for (int r = 0; r < cell_h; r++)
            for (int c = 0; c < cell_w; c++)
                cache[i * cell_h * cell_w + r * cell_w + c] = 0;
}

static void bake_glyph(uint8_t *cache, int cell_w, int cell_h,
                       unsigned char ch, float pixel_height) {
    ttf_bitmap_t bmp;
    if (!ttf_rasterize_codepoint(&g_font, (uint32_t)ch, pixel_height,
                                  kmalloc, kfree, &bmp)) {
        return;
    }
    if (!bmp.pixels) return;

    int baseline = (BASELINE_ROW * cell_h) / FONT_TTF_BASE_CELL_H;

    for (int r = 0; r < bmp.h; r++) {
        int dst_r = baseline + bmp.yoff + r;
        if (dst_r < 0 || dst_r >= cell_h) continue;
        for (int cc = 0; cc < bmp.w; cc++) {
            int dst_c = bmp.xoff + cc;
            if (dst_c < 0 || dst_c >= cell_w) continue;
            cache[ch * cell_h * cell_w + dst_r * cell_w + dst_c] =
                bmp.pixels[r * bmp.w + cc];
        }
    }
    kfree(bmp.pixels);
}

static void bake_cache(uint8_t *cache, int cell_w, int cell_h, uint32_t scale) {
    clear_cache(cache, cell_w, cell_h);
    float pixel_height = FONT_TTF_BAKE_HEIGHT * (float)scale;
    for (int c = 32; c < 127; c++)
        bake_glyph(cache, cell_w, cell_h, (unsigned char)c, pixel_height);
}

static void free_ui_cache(void) {
    if (ui_glyph_cache) {
        kfree(ui_glyph_cache);
        ui_glyph_cache = NULL;
    }
}

static int alloc_ui_cache(uint32_t scale) {
    free_ui_cache();
    ui_cell_w = FONT_TTF_BASE_CELL_W * scale;
    ui_cell_h = FONT_TTF_BASE_CELL_H * scale;
    size_t bytes = (size_t)256 * ui_cell_h * ui_cell_w;
    ui_glyph_cache = kmalloc(bytes);
    if (!ui_glyph_cache) return 0;
    bake_cache(ui_glyph_cache, (int)ui_cell_w, (int)ui_cell_h, scale);
    return 1;
}

static void free_term_cache(void) {
    if (term_glyph_cache) {
        kfree(term_glyph_cache);
        term_glyph_cache = NULL;
    }
}

static int alloc_term_cache(uint32_t scale) {
    free_term_cache();
    term_cell_w = FONT_TTF_BASE_CELL_W * scale;
    term_cell_h = FONT_TTF_BASE_CELL_H * scale;
    size_t bytes = (size_t)256 * term_cell_h * term_cell_w;
    term_glyph_cache = kmalloc(bytes);
    if (!term_glyph_cache) return 0;
    bake_cache(term_glyph_cache, (int)term_cell_w, (int)term_cell_h, scale);
    return 1;
}

void font_ttf_init(void) {
    size_t sz = (size_t)(font_ttf_data_end - font_ttf_data);
    if (!ttf_init(&g_font, font_ttf_data, sz)) {
        g_ready = false;
        return;
    }

    ui_scale = 1;
    if (!alloc_ui_cache(1)) {
        g_ready = false;
        return;
    }

    term_scale = 1;
    if (!alloc_term_cache(1)) {
        g_ready = false;
        return;
    }

    g_ready = true;
}

bool font_ttf_ready(void) { return g_ready; }

void font_ttf_set_scale(uint32_t scale) {
    if (!g_ready) return;
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    if (scale == term_scale) return;
    term_scale = scale;
    alloc_term_cache(scale);
}

uint32_t font_ttf_get_scale(void) { return term_scale; }

uint32_t font_ttf_cell_w(void)  { return term_cell_w; }
uint32_t font_ttf_cell_h(void)  { return term_cell_h; }

const uint8_t *font_ttf_glyph_row(unsigned char c, int row) {
    static uint8_t empty[FONT_TTF_BASE_CELL_W * 8];
    if (!term_glyph_cache || row < 0 || row >= (int)term_cell_h)
        return empty;
    return term_glyph_cache + c * term_cell_h * term_cell_w + row * term_cell_w;
}

void font_ttf_set_ui_scale(uint32_t scale) {
    if (!g_ready) return;
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    if (scale == ui_scale) return;
    ui_scale = scale;
    alloc_ui_cache(scale);
}

uint32_t font_ttf_get_ui_scale(void) { return ui_scale; }

uint32_t font_ttf_ui_cell_w(void)  { return ui_cell_w; }
uint32_t font_ttf_ui_cell_h(void)  { return ui_cell_h; }

const uint8_t *font_ttf_ui_glyph_row(unsigned char c, int row) {
    static uint8_t empty[FONT_TTF_BASE_CELL_W * 8];
    if (!ui_glyph_cache || row < 0 || row >= (int)ui_cell_h)
        return empty;
    return ui_glyph_cache + c * ui_cell_h * ui_cell_w + row * ui_cell_w;
}

int font_ttf_rasterize_char(unsigned char c, float pixel_height,
                            uint8_t **out_pixels, int *out_w, int *out_h,
                            int *out_xoff, int *out_yoff) {
    if (!g_ready) return 0;
    ttf_bitmap_t bmp;
    if (!ttf_rasterize_codepoint(&g_font, (uint32_t)c, pixel_height,
                                  kmalloc, kfree, &bmp))
        return 0;
    *out_pixels = bmp.pixels;
    *out_w = bmp.w;
    *out_h = bmp.h;
    *out_xoff = bmp.xoff;
    *out_yoff = bmp.yoff;
    return 1;
}
