#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../api/gpipe.h"

#define WM_TITLEBAR_H  46
#define WM_BTN_W       26
#define WM_BTN_H       26
#define WM_SHADOW_OFF  10
#define WM_CORNER_R    10

#define WM_COL_BORDER    0x0A246A
#define WM_COL_BORDER_HI 0x4C7BD9
#define WM_COL_TITLEBAR  0x1A3A7A
#define WM_COL_TITLEBAR2 0x0A2050
#define WM_COL_TITLETEXT 0xFFFFFF
#define WM_COL_FACE      0xEFEFEF
#define WM_COL_TEXT      0xF2F4F8
#define WM_COL_LABEL     0xAEB6C4
#define WM_COL_ACCENT    0x0A84FF
#define WM_COL_BTNFACE   0xE4E4E4
#define WM_COL_BTNHOVER  0xD64C4C
#define WM_COL_BTNBORDER 0xB0B0B0

typedef struct wm_window wm_window_t;

typedef void (*wm_draw_fn)(gpipe_ctx_t *ctx, wm_window_t *win, void *user);
typedef void (*wm_click_fn)(wm_window_t *win, void *user, int mx, int my);

wm_window_t *wm_create(int x, int y, int w, int h, const char *title, void *user);
wm_window_t *wm_create_static(int x, int y, int w, int h, const char *title, void *user, uint32_t *saved_buf);
void wm_destroy(wm_window_t *win);

void wm_set_draw_content(wm_window_t *win, wm_draw_fn fn);
void wm_set_click_handler(wm_window_t *win, wm_click_fn fn);

void wm_open(wm_window_t *win, gpipe_ctx_t *ctx);
void wm_close(wm_window_t *win, gpipe_ctx_t *ctx);
void wm_redraw(wm_window_t *win, gpipe_ctx_t *ctx);

void wm_get_pos(wm_window_t *win, int *x, int *y);
int  wm_width(wm_window_t *win);
int  wm_height(wm_window_t *win);
int  wm_region_w(wm_window_t *win);
int  wm_region_h(wm_window_t *win);

bool wm_step(wm_window_t *win, gpipe_ctx_t *ctx, bool esc_pressed, bool *min_clicked);

void wm_draw_button(gpipe_ctx_t *ctx, int x, int y, const char *label, bool danger);
bool wm_point_in(int px, int py, int x, int y, int w, int h);

void wm_erase_all_for_scroll(gpipe_ctx_t *ctx);
void wm_repaint_all_after_scroll(gpipe_ctx_t *ctx);

bool wm_any_open_over(int x, int y, int w, int h);
void wm_repaint_over(gpipe_ctx_t *ctx, int x, int y, int w, int h);