#include "../commands.h"
#include "util.h"
#include "../../graphics/api/gpipe.h"
#include "../../graphics/api/gpipe_prim.h"
#include "../../graphics/cursor.h"
#include "../../drivers/keyboard.h"
#include "../../drivers/mouse.h"
#include "../../fs/vfs.h"
#include "../../mm/pmm.h"
#include "../../kernel/pit.h"
#include "../../kernel/sched.h"
#include "../../kernel/lock.h"
#include <limine.h>
#include <stdbool.h>

extern struct limine_framebuffer *fbi;
extern uint64_t tsc_hz;

#define WIN_W 460
#define WIN_H 300
#define TITLEBAR_H 22
#define BTN_W 20
#define BTN_H 16
#define SHADOW_OFF 8
#define REGION_W (WIN_W + SHADOW_OFF)
#define REGION_H (WIN_H + SHADOW_OFF)

#define COL_BORDER    0x0A246A
#define COL_BORDER_HI 0x4C7BD9
#define COL_TITLEBAR  0x1A3A7A
#define COL_TITLEBAR2 0x0A2050
#define COL_TITLETEXT 0xFFFFFF
#define COL_FACE      0xEFEFEF
#define COL_TEXT      0x1A1A1A
#define COL_LABEL     0x5A5A5A
#define COL_ACCENT    0x3D7BFF
#define COL_BTNFACE   0xE4E4E4
#define COL_BTNHOVER  0xD64C4C
#define COL_BTNBORDER 0xB0B0B0
#define COL_SHADOW    0x000000

static uint32_t saved_win[REGION_H][REGION_W];
static int win_x, win_y;
static bool about_running = false;

static void get_cpu_name(char *out) {
    uint32_t eax, ebx, ecx, edx;

    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000002));
    *(uint32_t *)(out + 0)  = eax;
    *(uint32_t *)(out + 4)  = ebx;
    *(uint32_t *)(out + 8)  = ecx;
    *(uint32_t *)(out + 12) = edx;

    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000003));
    *(uint32_t *)(out + 16) = eax;
    *(uint32_t *)(out + 20) = ebx;
    *(uint32_t *)(out + 24) = ecx;
    *(uint32_t *)(out + 28) = edx;

    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000004));
    *(uint32_t *)(out + 32) = eax;
    *(uint32_t *)(out + 36) = ebx;
    *(uint32_t *)(out + 40) = ecx;
    *(uint32_t *)(out + 44) = edx;

    out[48] = 0;
    int i = 0;
    while (out[i] == ' ') i++;
    if (i > 0) {
        int j = 0;
        while (out[i]) out[j++] = out[i++];
        out[j] = 0;
    }
}

static void uint_to_str(uint32_t n, char *out) {
    char tmp[12];
    int i = 0;
    if (n == 0) { out[0] = '0'; out[1] = 0; return; }
    while (n) { tmp[i++] = '0' + (n % 10); n /= 10; }
    int j = 0;
    while (i) out[j++] = tmp[--i];
    out[j] = 0;
}

static void str_cat(char *dst, const char *src) {
    while (*dst) dst++;
    while (*src) *dst++ = *src++;
    *dst = 0;
}

static void draw_shadow(gpipe_ctx_t *ctx) {
    int sx = win_x + 8, sy = win_y + 8;
    for (int row = 0; row < WIN_H; row++) {
        for (int col = 0; col < WIN_W; col++) {
            gpipe_pixel_blend(ctx, sx + col, sy + row, 0x28000000);
        }
    }
}

static void draw_button(gpipe_ctx_t *ctx, int x, int y, const char *label, bool danger) {
    uint32_t face = danger ? COL_BTNHOVER : COL_BTNFACE;
    uint32_t txt  = danger ? 0xFFFFFF : COL_TEXT;
    gpipe_rect_fill(ctx, x, y, BTN_W, BTN_H, face);
    gpipe_rect(ctx, x, y, BTN_W, BTN_H, COL_BTNBORDER);
    gpipe_line(ctx, x, y, x + BTN_W - 1, y, 0xFFFFFF);
    gpipe_line(ctx, x, y, x, y + BTN_H - 1, 0xFFFFFF);
    int tw = gpipe_text_width(label);
    gpipe_text(ctx, x + (BTN_W - tw) / 2, y + (BTN_H - 16) / 2, label, txt, GPIPE_TEXT_TRANSPARENT);
}

static bool point_in(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void hide_cursor_locked(gpipe_ctx_t *ctx) {
    cursor_set_visible(false);
    cursor_update(ctx);
}

static void show_cursor_locked(gpipe_ctx_t *ctx) {
    cursor_set_visible(true);
    cursor_update(ctx);
}

static void save_window_region(gpipe_ctx_t *ctx) {
    for (int row = 0; row < REGION_H; row++) {
        uint32_t *src = ctx->back + (uint32_t)(win_y + row) * ctx->pw + win_x;
        for (int col = 0; col < REGION_W; col++)
            saved_win[row][col] = src[col];
    }
}

static void restore_window_region(gpipe_ctx_t *ctx) {
    for (int row = 0; row < REGION_H; row++) {
        uint32_t *dst = ctx->back + (uint32_t)(win_y + row) * ctx->pw + win_x;
        for (int col = 0; col < REGION_W; col++)
            dst[col] = saved_win[row][col];
    }
    gpipe_mark_dirty(ctx, win_x, win_y, REGION_W, REGION_H);
}

static void draw_window(gpipe_ctx_t *ctx) {
    draw_shadow(ctx);

    gpipe_rect_fill(ctx, win_x, win_y, WIN_W, WIN_H, COL_FACE);
    gpipe_rect(ctx, win_x, win_y, WIN_W, WIN_H, COL_BORDER);
    gpipe_rect(ctx, win_x + 1, win_y + 1, WIN_W - 2, WIN_H - 2, COL_BORDER_HI);

    for (int row = 0; row < TITLEBAR_H; row++) {
        uint32_t c = row < TITLEBAR_H / 2 ? COL_TITLEBAR : COL_TITLEBAR2;
        gpipe_line(ctx, win_x + 2, win_y + 2 + row, win_x + WIN_W - 3, win_y + 2 + row, c);
    }
    gpipe_text(ctx, win_x + 8, win_y + 4, "About KiNBOL", COL_TITLETEXT, GPIPE_TEXT_TRANSPARENT);
    gpipe_rect_fill(ctx, win_x + 1, win_y + 1 + TITLEBAR_H, WIN_W - 2, 3, COL_ACCENT);

    draw_button(ctx, win_x + WIN_W - BTN_W * 2 - 8, win_y + 3, "-", false);
    draw_button(ctx, win_x + WIN_W - BTN_W - 4, win_y + 3, "X", true);

    int ty = win_y + TITLEBAR_H + 18;
    int lx = win_x + 20;

    gpipe_text(ctx, lx, ty, "KiNBOL 0.08.2", COL_TEXT, GPIPE_TEXT_TRANSPARENT);
    ty += 22;
    gpipe_text(ctx, lx, ty, "this Kernel is Not Based On Linux", COL_LABEL, GPIPE_TEXT_TRANSPARENT);
    ty += 28;

    char line[96];
    char num[12];

    line[0] = 0;
    str_cat(line, "Kernel:   x86_64 Limine UEFI");
    gpipe_text(ctx, lx, ty, line, COL_TEXT, GPIPE_TEXT_TRANSPARENT); ty += 18;

    char cpu[49];
    get_cpu_name(cpu);
    line[0] = 0;
    str_cat(line, "CPU:      ");
    str_cat(line, cpu);
    gpipe_text(ctx, lx, ty, line, COL_TEXT, GPIPE_TEXT_TRANSPARENT); ty += 18;

    line[0] = 0;
    str_cat(line, "TSC:      ");
    uint_to_str((uint32_t)(tsc_hz / 1000000), num);
    str_cat(line, num);
    str_cat(line, " MHz");
    gpipe_text(ctx, lx, ty, line, COL_TEXT, GPIPE_TEXT_TRANSPARENT); ty += 18;

    line[0] = 0;
    str_cat(line, "Display:  ");
    uint_to_str(fbi->width, num);
    str_cat(line, num);
    str_cat(line, "x");
    uint_to_str(fbi->height, num);
    str_cat(line, num);
    str_cat(line, " @ ");
    uint_to_str(fbi->bpp, num);
    str_cat(line, num);
    str_cat(line, "bpp");
    gpipe_text(ctx, lx, ty, line, COL_TEXT, GPIPE_TEXT_TRANSPARENT); ty += 18;

    line[0] = 0;
    str_cat(line, "VFS:      ");
    uint_to_str((uint32_t)vfs_node_count(), num);
    str_cat(line, num);
    str_cat(line, " nodes");
    gpipe_text(ctx, lx, ty, line, COL_TEXT, GPIPE_TEXT_TRANSPARENT); ty += 18;

    line[0] = 0;
    str_cat(line, "PMM:      ");
    uint_to_str((uint32_t)pmm_get_free_page_count(), num);
    str_cat(line, num);
    str_cat(line, " pages free");
    gpipe_text(ctx, lx, ty, line, COL_TEXT, GPIPE_TEXT_TRANSPARENT); ty += 18;

    uint64_t ms = uptime_ms();
    uint32_t s = (uint32_t)(ms / 1000);
    uint32_t h = s / 3600;
    uint32_t m = (s % 3600) / 60;
    s = s % 60;

    line[0] = 0;
    str_cat(line, "Uptime:   ");
    if (h) { uint_to_str(h, num); str_cat(line, num); str_cat(line, "h "); }
    if (m) { uint_to_str(m, num); str_cat(line, num); str_cat(line, "m "); }
    uint_to_str(s, num); str_cat(line, num); str_cat(line, "s");
    gpipe_text(ctx, lx, ty, line, COL_TEXT, GPIPE_TEXT_TRANSPARENT); ty += 28;

    gpipe_text(ctx, lx, ty, "kernel@KiNBOL - drag titlebar to move, ESC/X to close", COL_LABEL, GPIPE_TEXT_TRANSPARENT);

    gpipe_mark_dirty(ctx, win_x, win_y, REGION_W, REGION_H);
}

static void about_task(void *arg) {
    (void)arg;

    gpipe_ctx_t *ctx = gpipe_default();
    if (!ctx || !ctx->back) { about_running = false; return; }

    win_x = ((int)ctx->width - WIN_W) / 2;
    win_y = ((int)ctx->height - WIN_H) / 2;
    if (win_x < 0) win_x = 0;
    if (win_y < 0) win_y = 0;
    if (win_x > (int)ctx->width  - REGION_W) win_x = (int)ctx->width  - REGION_W;
    if (win_y > (int)ctx->height - REGION_H) win_y = (int)ctx->height - REGION_H;

    bkl_acquire();
    hide_cursor_locked(ctx);
    save_window_region(ctx);
    draw_window(ctx);
    show_cursor_locked(ctx);
    gpipe_present(ctx);
    bkl_release();

    int prev_left = 0;
    bool dragging = false;
    int drag_dx = 0, drag_dy = 0;

    for (;;) {
        keyboard_update();
        if (keyboard_held(KEY_ESC)) break;

        mouse_state_t ms = mouse_get_state();
        int mx, my;
        cursor_get_pos(&mx, &my);

        int close_x = win_x + WIN_W - BTN_W - 4;
        int min_x   = win_x + WIN_W - BTN_W * 2 - 8;
        int btn_y   = win_y + 3;

        bool click_edge = ms.left && !prev_left;

        if (click_edge) {
            if (point_in(mx, my, close_x, btn_y, BTN_W, BTN_H)) break;
            if (point_in(mx, my, min_x, btn_y, BTN_W, BTN_H)) {
            } else if (point_in(mx, my, win_x + 1, win_y + 1, WIN_W - 2, TITLEBAR_H)) {
                dragging = true;
                drag_dx = mx - win_x;
                drag_dy = my - win_y;
            }
        }
        if (!ms.left) dragging = false;
        prev_left = ms.left;

        if (dragging) {
            int nx = mx - drag_dx;
            int ny = my - drag_dy;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            if (nx > (int)ctx->width  - REGION_W) nx = (int)ctx->width  - REGION_W;
            if (ny > (int)ctx->height - REGION_H) ny = (int)ctx->height - REGION_H;

            if (nx != win_x || ny != win_y) {
                bkl_acquire();
                hide_cursor_locked(ctx);
                restore_window_region(ctx);
                win_x = nx;
                win_y = ny;
                save_window_region(ctx);
                draw_window(ctx);
                show_cursor_locked(ctx);
                gpipe_present(ctx);
                bkl_release();
            }
        }

        sched_yield();
    }

    bkl_acquire();
    hide_cursor_locked(ctx);
    restore_window_region(ctx);
    show_cursor_locked(ctx);
    gpipe_present(ctx);
    bkl_release();

    about_running = false;
}

void cmd_about(void) {
    if (about_running) return;
    about_running = true;
    task_create("[about]", about_task, NULL);
}