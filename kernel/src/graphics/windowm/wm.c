#include "wm.h"
#include "../api/gpipe_prim.h"
#include "../cursor.h"
#include "../../drivers/mouse.h"
#include "../../kernel/lock.h"
#include "../../mm/heap.h"
#include "../../libk/string.h"

#define WM_MAX_WINDOWS 8

struct wm_window {
    int x, y, w, h;
    char title[64];
    uint32_t *saved;
    bool owns_saved;
    bool open;
    bool dragging;
    int drag_dx, drag_dy;
    int prev_left;
    wm_draw_fn draw_content;
    wm_click_fn click_handler;
    void *user;
};

static wm_window_t *g_windows[WM_MAX_WINDOWS];

static void hide_cursor_locked(gpipe_ctx_t *ctx) {
    cursor_set_visible(false);
    cursor_update(ctx);
}
static void show_cursor_locked(gpipe_ctx_t *ctx) {
    cursor_set_visible(true);
    cursor_update(ctx);
}

bool wm_point_in(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static bool glass_mask(int lx, int ly, int rw, int rh, int radius) {
    if (lx < 0 || ly < 0 || lx >= rw || ly >= rh) return false;
    if (lx < radius && ly < radius) {
        int dx = radius - lx, dy = radius - ly;
        return dx * dx + dy * dy <= radius * radius;
    }
    if (lx >= rw - radius && ly < radius) {
        int dx = lx - (rw - radius - 1), dy = radius - ly;
        return dx * dx + dy * dy <= radius * radius;
    }
    if (lx < radius && ly >= rh - radius) {
        int dx = radius - lx, dy = ly - (rh - radius - 1);
        return dx * dx + dy * dy <= radius * radius;
    }
    if (lx >= rw - radius && ly >= rh - radius) {
        int dx = lx - (rw - radius - 1), dy = ly - (rh - radius - 1);
        return dx * dx + dy * dy <= radius * radius;
    }
    return true;
}

void wm_draw_button(gpipe_ctx_t *ctx, int x, int y, const char *label, bool danger) {
    int cx = x + WM_BTN_W / 2, cy = y + WM_BTN_H / 2;
    int r  = WM_BTN_W / 2 - 1;

    for (int row = -r; row <= r; row++) {
        for (int col = -r; col <= r; col++) {
            if (col * col + row * row > r * r) continue;
            uint32_t tint = danger ? 0x50FF5C4A : 0x30FFFFFF;
            gpipe_pixel_blend(ctx, cx + col, cy + row, tint);
        }
    }
    gpipe_circle(ctx, cx, cy, r, danger ? 0xFF7A68 : 0x8A93A6);

    int tw = gpipe_text_width(label);
    gpipe_text(ctx, cx - tw / 2, cy - 8, label, 0xFFFFFF, GPIPE_TEXT_TRANSPARENT);
}

static void save_region(wm_window_t *win, gpipe_ctx_t *ctx) {
    int rw = wm_region_w(win), rh = wm_region_h(win);
    for (int row = 0; row < rh; row++) {
        uint32_t *src = ctx->back + (uint32_t)(win->y + row) * ctx->pw + win->x;
        uint32_t *dst = win->saved + row * rw;
        for (int col = 0; col < rw; col++) dst[col] = src[col];
    }
}

static void restore_region(wm_window_t *win, gpipe_ctx_t *ctx) {
    int rw = wm_region_w(win), rh = wm_region_h(win);
    for (int row = 0; row < rh; row++) {
        uint32_t *dst = ctx->back + (uint32_t)(win->y + row) * ctx->pw + win->x;
        uint32_t *src = win->saved + row * rw;
        for (int col = 0; col < rw; col++) dst[col] = src[col];
    }
    gpipe_mark_dirty(ctx, win->x, win->y, rw, rh);
}

static void draw_chrome(wm_window_t *win, gpipe_ctx_t *ctx) {
    int rw = win->w, rh = win->h, radius = WM_CORNER_R;
    if (radius * 2 > rw) radius = rw / 2;
    if (radius * 2 > rh) radius = rh / 2;

    int sx = win->x + WM_SHADOW_OFF, sy = win->y + WM_SHADOW_OFF;
    for (int row = 0; row < rh; row++) {
        for (int col = 0; col < rw; col++) {
            if (!glass_mask(col, row, rw, rh, radius)) continue;
            gpipe_pixel_blend(ctx, sx + col, sy + row, 0x3A000000);
        }
    }

    for (int row = 0; row < rh; row++) {
        uint32_t top_boost = (uint32_t)(70 * (rh - row)) / (uint32_t)rh;
        uint32_t alpha = 0x70 + top_boost;
        if (alpha > 0xC8) alpha = 0xC8;
        uint32_t tint = (alpha << 24) | 0x0D111C;

        for (int col = 0; col < rw; col++) {
            if (!glass_mask(col, row, rw, rh, radius)) continue;
            gpipe_pixel_blend(ctx, win->x + col, win->y + row, tint);

            bool near_edge = (row < 2) || (col < 2) || (row >= rh - 2) || (col >= rw - 2);
            if (near_edge) {
                if (row < 2 || col < 2) {
                    gpipe_pixel_blend(ctx, win->x + col, win->y + row, 0x50FFFFFF);
                } else {
                    gpipe_pixel_blend(ctx, win->x + col, win->y + row, 0x30000000);
                }
            }
        }
    }

    gpipe_rect_fill(ctx, win->x + 18, win->y + WM_TITLEBAR_H - 6, rw - 36, 1, 0x1AFFFFFF);

    gpipe_text(ctx, win->x + 20, win->y + 14, win->title, WM_COL_TITLETEXT, GPIPE_TEXT_TRANSPARENT);

    wm_draw_button(ctx, win->x + win->w - WM_BTN_W * 2 - 14, win->y + 10, "-", false);
    wm_draw_button(ctx, win->x + win->w - WM_BTN_W - 8, win->y + 10, "X", true);
}

static void draw_all(wm_window_t *win, gpipe_ctx_t *ctx) {
    draw_chrome(win, ctx);
    if (win->draw_content) win->draw_content(ctx, win, win->user);
    gpipe_mark_dirty(ctx, win->x, win->y, wm_region_w(win), wm_region_h(win));
}

static void move_locked(wm_window_t *win, gpipe_ctx_t *ctx, int nx, int ny) {
    hide_cursor_locked(ctx);
    restore_region(win, ctx);
    win->x = nx;
    win->y = ny;
    save_region(win, ctx);
    draw_all(win, ctx);
    show_cursor_locked(ctx);
    gpipe_present(ctx);
}

wm_window_t *wm_create(int x, int y, int w, int h, const char *title, void *user) {
    int slot = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!g_windows[i]) { slot = i; break; }
    }
    if (slot < 0) return NULL;

    wm_window_t *win = kmalloc(sizeof(wm_window_t));
    if (!win) return NULL;

    int rw = w + WM_SHADOW_OFF, rh = h + WM_SHADOW_OFF;
    win->saved = kmalloc((size_t)rw * rh * sizeof(uint32_t));
    if (!win->saved) { kfree(win); return NULL; }
    win->owns_saved = true;

    win->x = x; win->y = y; win->w = w; win->h = h;
    strncpy(win->title, title, sizeof(win->title) - 1);
    win->title[sizeof(win->title) - 1] = 0;
    win->open = false;
    win->dragging = false;
    win->drag_dx = win->drag_dy = 0;
    win->prev_left = 0;
    win->draw_content = NULL;
    win->click_handler = NULL;
    win->user = user;

    g_windows[slot] = win;
    return win;
}

wm_window_t *wm_create_static(int x, int y, int w, int h, const char *title, void *user, uint32_t *saved_buf) {
    int slot = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!g_windows[i]) { slot = i; break; }
    }
    if (slot < 0) return NULL;

    wm_window_t *win = kmalloc(sizeof(wm_window_t));
    if (!win) return NULL;

    win->saved = saved_buf;
    win->owns_saved = false;

    win->x = x; win->y = y; win->w = w; win->h = h;
    strncpy(win->title, title, sizeof(win->title) - 1);
    win->title[sizeof(win->title) - 1] = 0;
    win->open = false;
    win->dragging = false;
    win->drag_dx = win->drag_dy = 0;
    win->prev_left = 0;
    win->draw_content = NULL;
    win->click_handler = NULL;
    win->user = user;

    g_windows[slot] = win;
    return win;
}

void wm_destroy(wm_window_t *win) {
    if (!win) return;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (g_windows[i] == win) { g_windows[i] = NULL; break; }
    }
    if (win->owns_saved) kfree(win->saved);
    kfree(win);
}

void wm_set_draw_content(wm_window_t *win, wm_draw_fn fn) { win->draw_content = fn; }
void wm_set_click_handler(wm_window_t *win, wm_click_fn fn) { win->click_handler = fn; }

void wm_open(wm_window_t *win, gpipe_ctx_t *ctx) {
    hide_cursor_locked(ctx);
    save_region(win, ctx);
    draw_all(win, ctx);
    show_cursor_locked(ctx);
    gpipe_present(ctx);
    win->open = true;
}

void wm_close(wm_window_t *win, gpipe_ctx_t *ctx) {
    hide_cursor_locked(ctx);
    restore_region(win, ctx);
    show_cursor_locked(ctx);
    gpipe_present(ctx);
    win->open = false;
}

void wm_redraw(wm_window_t *win, gpipe_ctx_t *ctx) {
    hide_cursor_locked(ctx);
    restore_region(win, ctx);
    save_region(win, ctx);
    draw_all(win, ctx);
    show_cursor_locked(ctx);
    gpipe_present(ctx);
}

void wm_get_pos(wm_window_t *win, int *x, int *y) {
    if (x) *x = win->x;
    if (y) *y = win->y;
}
int wm_width(wm_window_t *win)  { return win->w; }
int wm_height(wm_window_t *win) { return win->h; }
int wm_region_w(wm_window_t *win) { return win->w + WM_SHADOW_OFF; }
int wm_region_h(wm_window_t *win) { return win->h + WM_SHADOW_OFF; }

bool wm_step(wm_window_t *win, gpipe_ctx_t *ctx, bool esc_pressed, bool *min_clicked) {
    if (min_clicked) *min_clicked = false;
    if (esc_pressed) return true;

    mouse_state_t ms = mouse_get_state();
    int mx, my;
    cursor_get_pos(&mx, &my);

    int close_x = win->x + win->w - WM_BTN_W - 4;
    int min_x   = win->x + win->w - WM_BTN_W * 2 - 8;
    int btn_y   = win->y + 3;

    bool click_edge = ms.left && !win->prev_left;

    if (click_edge) {
        if (wm_point_in(mx, my, close_x, btn_y, WM_BTN_W, WM_BTN_H)) {
            win->prev_left = ms.left;
            return true;
        }
        if (wm_point_in(mx, my, min_x, btn_y, WM_BTN_W, WM_BTN_H)) {
            if (min_clicked) *min_clicked = true;
        } else if (wm_point_in(mx, my, win->x + 1, win->y + 1, win->w - 2, WM_TITLEBAR_H)) {
            win->dragging = true;
            win->drag_dx = mx - win->x;
            win->drag_dy = my - win->y;
        } else if (win->click_handler) {
            win->click_handler(win, win->user, mx, my);
        }
    }
    if (!ms.left) win->dragging = false;
    win->prev_left = ms.left;

    if (win->dragging) {
        int rw = wm_region_w(win), rh = wm_region_h(win);
        int nx = mx - win->drag_dx;
        int ny = my - win->drag_dy;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx > (int)ctx->width  - rw) nx = (int)ctx->width  - rw;
        if (ny > (int)ctx->height - rh) ny = (int)ctx->height - rh;

        if (nx != win->x || ny != win->y) {
            bkl_acquire();
            move_locked(win, ctx, nx, ny);
            bkl_release();
        }
    }

    return false;
}

void wm_erase_all_for_scroll(gpipe_ctx_t *ctx) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t *win = g_windows[i];
        if (win && win->open) restore_region(win, ctx);
    }
}

void wm_repaint_all_after_scroll(gpipe_ctx_t *ctx) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t *win = g_windows[i];
        if (win && win->open) {
            save_region(win, ctx);
            draw_all(win, ctx);
        }
    }
}

static bool rects_overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

bool wm_any_open_over(int x, int y, int w, int h) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t *win = g_windows[i];
        if (win && win->open) {
            int rw = wm_region_w(win), rh = wm_region_h(win);
            if (rects_overlap(x, y, w, h, win->x, win->y, rw, rh)) return true;
        }
    }
    return false;
}

void wm_repaint_over(gpipe_ctx_t *ctx, int x, int y, int w, int h) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t *win = g_windows[i];
        if (win && win->open) {
            int rw = wm_region_w(win), rh = wm_region_h(win);
            if (rects_overlap(x, y, w, h, win->x, win->y, rw, rh)) {
                draw_all(win, ctx);
            }
        }
    }
}