#include "cursor.h"
#include "../drivers/mouse.h"
#include <stddef.h>

static const char *cursor_glyph[CURSOR_H] = {
    "X           ",
    "XX          ",
    "XOX         ",
    "XOOX        ",
    "XOOOX       ",
    "XOOOOX      ",
    "XOOOOOX     ",
    "XOOOOOOX    ",
    "XOOOOOOOX   ",
    "XOOOOOOOOX  ",
    "XOOOOOOOOOX ",
    "XOOOOOXXXXX ",
    "XOOXOOX     ",
    "XOX XOOX    ",
    "XX   XOOX   ",
    "X     XOOX  ",
    "      XOOX  ",
    "       XX   ",
    "            ",
};

static uint8_t glyph_bits[CURSOR_H][CURSOR_W];
static uint32_t saved_px[CURSOR_H][CURSOR_W];
static bool saved_valid = false;
static bool cursor_visible = true;

static int cx = 0, cy = 0;
static int screen_w = 0, screen_h = 0;

static inline int iclampc(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void cursor_init(gpipe_ctx_t *ctx) {
    for (int y = 0; y < CURSOR_H; y++) {
        for (int x = 0; x < CURSOR_W; x++) {
            char c = cursor_glyph[y][x];
            glyph_bits[y][x] = (c == 'X') ? 1 : (c == 'O') ? 2 : 0;
        }
    }

    saved_valid = false;
    cursor_visible = true;

    if (ctx) {
        screen_w = (int)ctx->width;
        screen_h = (int)ctx->height;
        cx = screen_w / 2;
        cy = screen_h / 2;
    }
}

void cursor_set_visible(bool visible) {
    cursor_visible = visible;
}

static void cursor_restore(gpipe_ctx_t *ctx) {
    if (!saved_valid || !ctx || !ctx->back) return;

    for (int y = 0; y < CURSOR_H; y++) {
        uint32_t *row = ctx->back + (uint32_t)(cy + y) * ctx->pw + cx;
        for (int x = 0; x < CURSOR_W; x++)
            row[x] = saved_px[y][x];
    }

    gpipe_mark_dirty(ctx, cx, cy, CURSOR_W, CURSOR_H);
    saved_valid = false;
}

static void cursor_paint(gpipe_ctx_t *ctx, int x, int y) {
    if (!ctx || !ctx->back) return;

    for (int row = 0; row < CURSOR_H; row++) {
        uint32_t *dst = ctx->back + (uint32_t)(y + row) * ctx->pw + x;
        for (int col = 0; col < CURSOR_W; col++) {
            saved_px[row][col] = dst[col];

            switch (glyph_bits[row][col]) {
                case 1: dst[col] = 0x000000; break;
                case 2: dst[col] = 0xFFFFFF; break;
                default: break;
            }
        }
    }

    saved_valid = true;
    gpipe_mark_dirty(ctx, x, y, CURSOR_W, CURSOR_H);
}

void cursor_update(gpipe_ctx_t *ctx) {
    if (!ctx || !ctx->back) return;

    screen_w = (int)ctx->width;
    screen_h = (int)ctx->height;

    int dx, dy, left, right, middle;
    mouse_get_cursor_delta(&dx, &dy, &left, &right, &middle);
    (void)left; (void)right; (void)middle;

    if (!cursor_visible) {
        if (saved_valid) cursor_restore(ctx);
        return;
    }

    if (dx == 0 && dy == 0 && saved_valid) return;

    int nx = iclampc(cx + dx, 0, screen_w - CURSOR_W);
    int ny = iclampc(cy + dy, 0, screen_h - CURSOR_H);

    cursor_restore(ctx);
    cx = nx;
    cy = ny;
    cursor_paint(ctx, cx, cy);
}

void cursor_get_pos(int *x, int *y) {
    if (x) *x = cx;
    if (y) *y = cy;
}

void cursor_erase_for_scroll(gpipe_ctx_t *ctx) {
    
    if (saved_valid) cursor_restore(ctx);
    saved_valid = false;
}