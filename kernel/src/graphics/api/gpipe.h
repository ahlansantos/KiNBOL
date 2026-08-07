#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#define GPIPE_VERSION "1.2"

typedef struct gpipe_rect {
    int x, y, w, h;
} gpipe_rect_t;

typedef struct gpipe_ctx {
    struct limine_framebuffer *fb;
    uint32_t *back;
    uint32_t  pw;
    uint32_t  width;
    uint32_t  height;
    size_t    back_size;
    uint64_t  back_pages;
    void     *back_phys;

    gpipe_rect_t dirty;
} gpipe_ctx_t;

gpipe_ctx_t *gpipe_init(struct limine_framebuffer *fb);
void         gpipe_shutdown(gpipe_ctx_t *ctx);

gpipe_ctx_t *gpipe_default(void);
void         gpipe_set_default(gpipe_ctx_t *ctx);

void gpipe_sync_from_fb(gpipe_ctx_t *ctx);

void gpipe_flip(gpipe_ctx_t *ctx);
void gpipe_flip_full(gpipe_ctx_t *ctx);

void gpipe_present(gpipe_ctx_t *ctx);

void gpipe_mark_dirty(gpipe_ctx_t *ctx, int x, int y, int w, int h);

void gpipe_get_draw_target(gpipe_ctx_t *ctx, uint32_t **out_ptr, uint32_t *out_pitch);

int gpipe_width(gpipe_ctx_t *ctx);
int gpipe_height(gpipe_ctx_t *ctx);

uint32_t gpipe_rgb(uint8_t r, uint8_t g, uint8_t b);
uint32_t gpipe_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);