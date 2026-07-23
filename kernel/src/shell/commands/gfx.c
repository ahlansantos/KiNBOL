/*
 * Shell commands related to graphics, most likely small tests for the BareGL
 * primitives run straight from the terminal.
 */
#include "../commands.h"
#include "util.h"
#include "../../graphics/terminal.h"
#include "../../graphics/api/baregl.h"
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
    bare_clear(bare_rgb(0, 0, 0));
    bare_flip();
    terminal_clear();
    terminal_set_fg(COLOR_HIGHLIGHT);
}

