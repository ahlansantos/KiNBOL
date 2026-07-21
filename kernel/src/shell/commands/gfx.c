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

void cmd_drawtest(void) {
    bare_sync_from_fb();
    bare_rect_fill(100, 100, 200, 150, bare_rgb(255, 0, 0));
    bare_circle(400, 300, 80, bare_rgb(0, 255, 100));
    bare_line(0, 0, 640, 480, bare_rgb(0, 150, 255));
    bare_flip();
}

void cmd_pixel(int x, int y) {
    bare_sync_from_fb();
    bare_pixel(x, y, bare_rgb(255, 255, 255));
    bare_flip();
}

void cmd_line(int x0, int y0, int x1, int y1) {
    bare_sync_from_fb();
    bare_line(x0, y0, x1, y1, bare_rgb(0, 255, 0));
    bare_flip();
}

void cmd_rect(int x, int y, int w, int h) {
    bare_sync_from_fb();
    bare_rect(x, y, w, h, bare_rgb(255, 0, 0));
    bare_flip();
}

void cmd_fillrect(int x, int y, int w, int h) {
    bare_sync_from_fb();
    bare_rect_fill(x, y, w, h, bare_rgb(0, 0, 255));
    bare_flip();
}

void cmd_circle(int x, int y, int r) {
    bare_sync_from_fb();
    bare_circle(x, y, r, bare_rgb(255, 255, 0));
    bare_flip();
}

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

void cmd_baregl_status(void) {
    print_header("BareGL Status");

    print_field("  Version:      ", "0.2.1 (SR)");
    print_field("  Status:       ", "Active");
    print_field("  Backend:      ", "Software (SW)");
    print_field("  Double Buffer: ", "Yes");

    terminal_set_fg(COLOR_BODY);
    terminal_print("  Resolution:   ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(bare_width());
    terminal_print("x");
    terminal_print_int(bare_height());
    terminal_println("");

    terminal_set_fg(COLOR_BODY);
    terminal_print("  Pitch:        ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(fbi->pitch);
    terminal_println("");

    terminal_set_fg(COLOR_BODY);
    terminal_print("  BPP:          ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(fbi->bpp);
    terminal_println("");

    print_separator();
    terminal_set_fg(COLOR_BODY);
    terminal_println("  BareGL - Minimal graphics library for KiNBOL");
    terminal_println("  Software rasterizer with double buffering");
    terminal_println("  Supports: pixel, line, rect, fill, circle");
    terminal_println("");
}