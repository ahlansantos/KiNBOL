#include "../commands.h"
#include "util.h"
#include "../../graphics/api/gpipe.h"
#include "../../graphics/api/gpipe_prim.h"
#include "../../graphics/windowm/wm.h"
#include "../../drivers/keyboard.h"
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

static bool about_running = false;
static uint32_t about_saved_buf[(WIN_W + WM_SHADOW_OFF) * (WIN_H + WM_SHADOW_OFF)];

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

static void draw_about_content(gpipe_ctx_t *ctx, wm_window_t *win, void *user) {
    (void)user;
    int win_x, win_y;
    wm_get_pos(win, &win_x, &win_y);

    int ty = win_y + WM_TITLEBAR_H + 14;
    int lx = win_x + 20;

    gpipe_text(ctx, lx, ty, "KiNBOL 0.08.2", WM_COL_TEXT, GPIPE_TEXT_TRANSPARENT);
    ty += 22;
    gpipe_text(ctx, lx, ty, "this Kernel is Not Based On Linux", WM_COL_LABEL, GPIPE_TEXT_TRANSPARENT);
    ty += 26;

    char line[96];
    char num[12];

    line[0] = 0;
    str_cat(line, "Kernel:   x86_64 Limine UEFI");
    gpipe_text(ctx, lx, ty, line, WM_COL_TEXT, GPIPE_TEXT_TRANSPARENT); ty += 18;

    char cpu[49];
    get_cpu_name(cpu);
    line[0] = 0;
    str_cat(line, "CPU:      ");
    str_cat(line, cpu);
    gpipe_text(ctx, lx, ty, line, WM_COL_TEXT, GPIPE_TEXT_TRANSPARENT); ty += 18;

    line[0] = 0;
    str_cat(line, "TSC:      ");
    uint_to_str((uint32_t)(tsc_hz / 1000000), num);
    str_cat(line, num);
    str_cat(line, " MHz");
    gpipe_text(ctx, lx, ty, line, WM_COL_TEXT, GPIPE_TEXT_TRANSPARENT); ty += 18;

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
    gpipe_text(ctx, lx, ty, line, WM_COL_TEXT, GPIPE_TEXT_TRANSPARENT); ty += 18;

    line[0] = 0;
    str_cat(line, "VFS:      ");
    uint_to_str((uint32_t)vfs_node_count(), num);
    str_cat(line, num);
    str_cat(line, " nodes");
    gpipe_text(ctx, lx, ty, line, WM_COL_TEXT, GPIPE_TEXT_TRANSPARENT); ty += 18;

    line[0] = 0;
    str_cat(line, "PMM:      ");
    uint_to_str((uint32_t)pmm_get_free_page_count(), num);
    str_cat(line, num);
    str_cat(line, " pages free");
    gpipe_text(ctx, lx, ty, line, WM_COL_TEXT, GPIPE_TEXT_TRANSPARENT); ty += 18;

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
    gpipe_text(ctx, lx, ty, line, WM_COL_TEXT, GPIPE_TEXT_TRANSPARENT); ty += 28;

    gpipe_text(ctx, lx, ty, "kernel@KiNBOL - drag titlebar to move, ESC/X to close", WM_COL_LABEL, GPIPE_TEXT_TRANSPARENT);
}

static void about_task(void *arg) {
    (void)arg;

    gpipe_ctx_t *ctx = gpipe_default();
    if (!ctx || !ctx->back) { about_running = false; return; }

    int win_x = ((int)ctx->width  - WIN_W) / 2;
    int win_y = ((int)ctx->height - WIN_H) / 2;
    if (win_x < 0) win_x = 0;
    if (win_y < 0) win_y = 0;
    if (win_x > (int)ctx->width  - (WIN_W + WM_SHADOW_OFF)) win_x = (int)ctx->width  - (WIN_W + WM_SHADOW_OFF);
    if (win_y > (int)ctx->height - (WIN_H + WM_SHADOW_OFF)) win_y = (int)ctx->height - (WIN_H + WM_SHADOW_OFF);

    wm_window_t *win = wm_create_static(win_x, win_y, WIN_W, WIN_H, "About KiNBOL", NULL, about_saved_buf);
    if (!win) { about_running = false; return; }
    wm_set_draw_content(win, draw_about_content);

    bkl_acquire();
    wm_open(win, ctx);
    bkl_release();

    uint64_t last_tick = uptime_ms();

    for (;;) {
        bool esc = keyboard_held(KEY_ESC);
        if (wm_step(win, ctx, esc, NULL)) break;

        uint64_t now = uptime_ms();
        if (now - last_tick >= 1000) {
            last_tick = now;
            bkl_acquire();
            wm_redraw(win, ctx);
            bkl_release();
        }

        sched_yield();
    }

    bkl_acquire();
    wm_close(win, ctx);
    bkl_release();

    wm_destroy(win);
    about_running = false;
}

void cmd_about(void) {
    if (about_running) return;
    about_running = true;
    task_create("[about]", about_task, NULL);
}