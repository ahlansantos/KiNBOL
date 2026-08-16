#ifndef FONT_TTF_H
#define FONT_TTF_H

#include <stdint.h>
#include <stdbool.h>

#define FONT_TTF_BASE_CELL_W 8
#define FONT_TTF_BASE_CELL_H 16
#define FONT_TTF_BASELINE_ROW 12
#define FONT_TTF_BAKE_HEIGHT 12.0f

void font_ttf_init(void);
bool font_ttf_ready(void);

/* Terminal cache — rebaked when scale changes via font_ttf_set_scale(). */
void     font_ttf_set_scale(uint32_t scale);
uint32_t font_ttf_get_scale(void);
uint32_t font_ttf_cell_w(void);
uint32_t font_ttf_cell_h(void);
const uint8_t *font_ttf_glyph_row(unsigned char c, int row);

/* UI cache — rebaked via font_ttf_set_ui_scale() (gpipe_text, wm, about). */
void     font_ttf_set_ui_scale(uint32_t scale);
uint32_t font_ttf_get_ui_scale(void);
uint32_t font_ttf_ui_cell_w(void);
uint32_t font_ttf_ui_cell_h(void);
const uint8_t *font_ttf_ui_glyph_row(unsigned char c, int row);

/* On-the-fly rasterize for arbitrary float scale (gpipe_text_scaled). */
int font_ttf_rasterize_char(unsigned char c, float pixel_height,
                            uint8_t **out_pixels, int *out_w, int *out_h,
                            int *out_xoff, int *out_yoff);

#endif
