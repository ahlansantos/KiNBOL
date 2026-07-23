/*
 * Shell commands related to graphics, most likely small tests for the gpipe
 * primitives run straight from the terminal.
 */
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