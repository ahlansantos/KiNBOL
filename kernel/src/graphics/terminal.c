#include "terminal.h"
#include "../graphics/font.h"
#include "api/gpipe.h"
#include <stdint.h>

static struct limine_framebuffer *fbi = 0;
static uint32_t pw   = 0;
static uint32_t gx   = 8;
static uint32_t gy   = 8;
static uint32_t fg   = 0xFFFFFF;
static uint32_t bg   = 0x101418;
static uint32_t ts   = 1;

#define CELL_W (8u  * ts)
#define CELL_H (16u * ts)

void terminal_init(struct limine_framebuffer *fb) {
    fbi = fb;
    pw  = fb->pitch / 4;
    gx  = 8;
    gy  = 8;
}

void terminal_set_fg(uint32_t color) { fg = color; }
void terminal_set_bg(uint32_t color) { bg = color; }
uint32_t terminal_get_fg(void)       { return fg;  }

void terminal_set_scale(uint32_t scale) {
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    ts = scale;
    gx = 8; gy = 8;
}
uint32_t terminal_get_scale(void) { return ts; }

static void chr(uint32_t x, uint32_t y, char c, uint32_t cf, uint32_t cb) {
    if (x + CELL_W > fbi->width || y + CELL_H > fbi->height) return;

    gpipe_ctx_t *gctx   = gpipe_default();
    uint32_t    *target = NULL;
    uint32_t     tpitch = 0;
    gpipe_get_draw_target(gctx, &target, &tpitch);

    volatile uint32_t *fb;
    uint32_t pitch;
    if (target) {
        fb    = target;
        pitch = tpitch;
    } else {
        fb    = (volatile uint32_t *)fbi->address;
        pitch = pw;
    }

    const uint8_t *g = font[(unsigned char)c];

    for (int r = 0; r < 16; r++) {
        uint8_t row_bits = g[r];
        for (uint32_t dy = 0; dy < ts; dy++) {
            uint32_t line_offset = (y + r * ts + dy) * pitch + x;
            for (int co = 0; co < 8; co++) {
                uint32_t col = (row_bits & (1 << (7 - co))) ? cf : cb;
                for (uint32_t dx = 0; dx < ts; dx++) {
                    fb[line_offset + co * ts + dx] = col;
                }
            }
        }
    }

    if (target) {
        gpipe_mark_dirty(gctx, (int)x, (int)y, (int)CELL_W, (int)CELL_H);
        gpipe_flip(gctx);
    }
}

static void scroll(void) {
    gpipe_ctx_t *gctx   = gpipe_default();
    uint32_t    *target = NULL;
    uint32_t     tpitch = 0;
    gpipe_get_draw_target(gctx, &target, &tpitch);

    volatile uint32_t *fbb;
    uint32_t pitch;
    if (target) {
        fbb   = target;
        pitch = tpitch;
    } else {
        fbb   = fbi->address;
        pitch = pw;
    }

    for (uint32_t r = 0; r < fbi->height - CELL_H; r++) {
        uint32_t *d = (uint32_t *)(fbb + r * pitch);
        uint32_t *s = (uint32_t *)(fbb + (r + CELL_H) * pitch);
        for (uint32_t c = 0; c < fbi->width; c++) d[c] = s[c];
    }
    for (uint32_t r = fbi->height - CELL_H; r < fbi->height; r++) {
        uint32_t *p = (uint32_t *)(fbb + r * pitch);
        for (uint32_t c = 0; c < fbi->width; c++) p[c] = bg;
    }
    gy = fbi->height - CELL_H;

    if (target) {
        gpipe_mark_dirty(gctx, 0, 0, (int)fbi->width, (int)fbi->height);
        gpipe_flip_full(gctx);
    }
}

void terminal_putchar(char c) {
    if (c == '\n') {
        gx = 8; gy += CELL_H;
        if (gy >= fbi->height - CELL_H) scroll();
        return;
    }
    if (c == '\b') {
        if (gx >= 8 + CELL_W) { gx -= CELL_W; chr(gx, gy, ' ', fg, bg); }
        return;
    }
    chr(gx, gy, c, fg, bg);
    gx += CELL_W;
    if (gx >= fbi->width - CELL_W) {
        gx = 8; gy += CELL_H;
        if (gy >= fbi->height - CELL_H) scroll();
    }
}

void terminal_print(const char *s) {
    for (int i = 0; s[i]; i++) terminal_putchar(s[i]);
}

void terminal_println(const char *s) {
    terminal_print(s);
    terminal_putchar('\n');
}

void terminal_print_int(uint32_t n) {
    char b[11]; int i = 10; b[i--] = 0;
    do { b[i--] = '0' + n % 10; n /= 10; } while (n);
    terminal_print(&b[i + 1]);
}

void terminal_print_hex(uint64_t n) {
    char h[] = "0123456789ABCDEF";
    terminal_print("0x");
    for (int i = 15; i >= 0; i--)
        terminal_putchar(h[(n >> (i * 4)) & 0xF]);
}

void terminal_clear(void) {
    gpipe_ctx_t *gctx   = gpipe_default();
    uint32_t    *target = NULL;
    uint32_t     tpitch = 0;
    gpipe_get_draw_target(gctx, &target, &tpitch);

    volatile uint32_t *fb;
    uint32_t pitch;
    if (target) {
        fb    = target;
        pitch = tpitch;
    } else {
        fb    = (volatile uint32_t *)fbi->address;
        pitch = pw;
    }

    uint32_t total_pixels = pitch * fbi->height;
    for (uint32_t i = 0; i < total_pixels; i++) {
        fb[i] = bg;
    }
    gx = 8;
    gy = 8;

    if (target) {
        gpipe_mark_dirty(gctx, 0, 0, (int)fbi->width, (int)fbi->height);
        gpipe_flip_full(gctx);
    }
}

void terminal_cursor_draw(int visible) {
    gpipe_ctx_t *gctx   = gpipe_default();
    uint32_t    *target = NULL;
    uint32_t     tpitch = 0;
    gpipe_get_draw_target(gctx, &target, &tpitch);

    volatile uint32_t *fb;
    uint32_t pitch;
    if (target) {
        fb    = target;
        pitch = tpitch;
    } else {
        fb    = (volatile uint32_t *)fbi->address;
        pitch = pw;
    }

    uint32_t color = visible ? fg : bg;
    uint32_t row_start = 11 * ts;
    for (uint32_t row = row_start; row < row_start + 2 * ts; row++) {
        uint32_t line_offset = (gy + row) * pitch + gx;
        for (uint32_t co = 0; co < CELL_W; co++) {
            fb[line_offset + co] = color;
        }
    }

    if (target) {
        gpipe_mark_dirty(gctx, (int)gx, (int)(gy + row_start), (int)CELL_W, (int)(2 * ts));
        gpipe_flip(gctx);
    }
}