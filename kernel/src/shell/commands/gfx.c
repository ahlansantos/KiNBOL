#include "../commands.h"
#include "util.h"
#include "../../graphics/terminal.h"
#include "../../graphics/api/gpipe.h"
#include "../../graphics/api/gpipe_prim.h"
#include <limine.h>

extern struct limine_framebuffer *fbi;

void cmd_scale(const char *arg) {
    while (*arg == ' ') arg++;
    if (*arg < '1' || *arg > '8' || arg[1] != '\0') {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Usage: scale <1-8>  (1 = normal size)");
        return;
    }
    uint32_t s = (uint32_t)(*arg - '0');
    terminal_set_scale(s);
    terminal_clear();
    terminal_set_fg(COLOR_SUCCESS);
    terminal_print("  Terminal scale set to ");
    terminal_print_int(s);
    terminal_print("x");
    terminal_println("");
    terminal_set_fg(COLOR_BODY);
}

void cmd_clearfb(void) {
    gpipe_ctx_t *ctx = gpipe_default();
    gpipe_clear(ctx, gpipe_rgb(0, 0, 0));
    gpipe_flip_full(ctx);
    terminal_clear();
    terminal_set_fg(COLOR_HIGHLIGHT);
}

static int gfx_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void gpipe_cmd_banner(void) {
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_println("  GPipe - The son of BareGL");
    terminal_set_fg(COLOR_DIM);
    terminal_print("  gpipe ");
    terminal_print(GPIPE_VERSION);
    terminal_println("");
    terminal_set_fg(COLOR_BODY);
    terminal_println("  Commands:");
    terminal_println("    gpipe clearfb    clear the framebuffer via gpipe");
    terminal_println("    gpipe drawtest    draw a rect, circle and line, partial flip");
}

static void gpipe_cmd_drawtest(void) {
    gpipe_ctx_t *ctx = gpipe_default();
    if (!ctx) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  gpipe: no default context (gpipe_init not called?)");
        return;
    }

    gpipe_sync_from_fb(ctx);
    ctx->dirty = (gpipe_rect_t){0, 0, 0, 0};

    int w = gpipe_width(ctx), h = gpipe_height(ctx);
    int ox = w > 480 ? w - 480 : 0;
    int oy = h > 300 ? h - 300 : 0;

    gpipe_rect_fill(ctx, ox + 40, oy + 40, 120, 80, gpipe_rgb(200, 40, 40));
    gpipe_circle_fill(ctx, ox + 260, oy + 90, 50, gpipe_rgb(40, 190, 90));
    gpipe_line(ctx, ox + 40, oy + 200, ox + 300, oy + 240, gpipe_rgb(60, 140, 255));
    gpipe_rect(ctx, ox + 340, oy + 40, 100, 100, gpipe_rgb(255, 220, 60));

    gpipe_rect_t d = ctx->dirty;
    gpipe_flip(ctx);

    terminal_set_fg(COLOR_SUCCESS);
    terminal_println("  gpipe drawtest: rect, circle, line, outline drawn");
    terminal_set_fg(COLOR_DIM);
    terminal_print("  dirty rect flipped: x=");
    terminal_print_int(d.x);
    terminal_print(" y=");
    terminal_print_int(d.y);
    terminal_print(" w=");
    terminal_print_int(d.w);
    terminal_print(" h=");
    terminal_print_int(d.h);
    terminal_println("");
    terminal_set_fg(COLOR_BODY);
}

void cmd_gpipe(const char *arg) {
    while (*arg == ' ') arg++;

    if (!*arg) {
        gpipe_cmd_banner();
    } else if (gfx_streq(arg, "clearfb")) {
        cmd_clearfb();
    } else if (gfx_streq(arg, "drawtest")) {
        gpipe_cmd_drawtest();
    } else {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Usage: gpipe [clearfb|drawtest]");
        terminal_set_fg(COLOR_BODY);
    }
}