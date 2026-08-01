#include "cursor.h"
#include "api/gpipe.h"
#include "../drivers/mouse.h"

#define CURSOR_W 12
#define CURSOR_H 19

static const char cursor_glyph[CURSOR_H][CURSOR_W + 1] = {
    "#...........",
    "##..........",
    "#o#.........",
    "#oo#........",
    "#ooo#.......",
    "#oooo#......",
    "#ooooo#.....",
    "#oooooo#....",
    "#ooooooo#...",
    "#oooooooo#..",
    "#ooooo#####.",
    "#oooo#......",
    "#ooo#.......",
    "#oo#........",
    "#o#.........",
    "##..........",
    "#...........",
    "............",
    "............",
};

#define CURSOR_FILL   0x00FFFFFF
#define CURSOR_BORDER 0x00000000

static uint32_t under[CURSOR_W * CURSOR_H];
static int have_saved = 0;
static int cur_x = 0, cur_y = 0;
static int last_left = 0, last_right = 0, last_middle = 0;

static inline int iclamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline uint32_t back_get(gpipe_ctx_t *ctx, int x, int y) {
    uint32_t *ptr; uint32_t pitch;
    gpipe_get_draw_target(ctx, &ptr, &pitch);
    return ptr[(uint32_t)y * pitch + (uint32_t)x];
}
static inline void back_set(gpipe_ctx_t *ctx, int x, int y, uint32_t color) {
    uint32_t *ptr; uint32_t pitch;
    gpipe_get_draw_target(ctx, &ptr, &pitch);
    ptr[(uint32_t)y * pitch + (uint32_t)x] = color;
}

static void restore_under(gpipe_ctx_t *ctx) {
    if (!have_saved) return;
    int w = gpipe_width(ctx), h = gpipe_height(ctx);
    for (int row = 0; row < CURSOR_H; row++) {
        int py = cur_y + row;
        if (py < 0 || py >= h) continue;
        for (int col = 0; col < CURSOR_W; col++) {
            int px = cur_x + col;
            if (px < 0 || px >= w) continue;
            back_set(ctx, px, py, under[row * CURSOR_W + col]);
        }
    }
    gpipe_mark_dirty(ctx, cur_x, cur_y, CURSOR_W, CURSOR_H);
    have_saved = 0;
}

static void save_and_draw(gpipe_ctx_t *ctx) {
    int w = gpipe_width(ctx), h = gpipe_height(ctx);
    for (int row = 0; row < CURSOR_H; row++) {
        int py = cur_y + row;
        for (int col = 0; col < CURSOR_W; col++) {
            int px = cur_x + col;
            uint32_t bg = 0;
            if (px >= 0 && px < w && py >= 0 && py < h)
                bg = back_get(ctx, px, py);
            under[row * CURSOR_W + col] = bg;

            char c = cursor_glyph[row][col];
            if (c == '.') continue;
            if (px < 0 || px >= w || py < 0 || py >= h) continue;
            back_set(ctx, px, py, (c == 'o') ? CURSOR_FILL : CURSOR_BORDER);
        }
    }
    have_saved = 1;
    gpipe_mark_dirty(ctx, cur_x, cur_y, CURSOR_W, CURSOR_H);
}

void cursor_init(void) {
    gpipe_ctx_t *ctx = gpipe_default();
    if (!ctx) return;

    cur_x = gpipe_width(ctx) / 2;
    cur_y = gpipe_height(ctx) / 2;

    save_and_draw(ctx);
    gpipe_flip(ctx);
}

void cursor_force_redraw(void) {
    have_saved = 0;
    gpipe_ctx_t *ctx = gpipe_default();
    if (!ctx) return;
    save_and_draw(ctx);
    gpipe_flip(ctx);
}

void cursor_update(void) {
    gpipe_ctx_t *ctx = gpipe_default();
    if (!ctx) return;

    int dx, dy, left, right, middle;
    mouse_get_cursor_delta(&dx, &dy, &left, &right, &middle);

    if (dx == 0 && dy == 0 &&
        left == last_left && right == last_right && middle == last_middle) {
        return;
    }
    last_left = left; last_right = right; last_middle = middle;

    int w = gpipe_width(ctx), h = gpipe_height(ctx);
    int nx = iclamp(cur_x + dx, 0, w - 1);
    int ny = iclamp(cur_y + dy, 0, h - 1);

    restore_under(ctx);
    cur_x = nx;
    cur_y = ny;
    save_and_draw(ctx);

    gpipe_flip(ctx);
}