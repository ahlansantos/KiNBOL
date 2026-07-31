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

uint64_t terminal_lock(void) {
    uint64_t flags;
    asm volatile("pushfq; popq %0" : "=r"(flags) :: "memory");
    asm volatile("cli" ::: "memory");
    return flags;
}

void terminal_unlock(uint64_t flags) {
    asm volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

void terminal_init(struct limine_framebuffer *fb) {
    fbi = fb;
    pw  = fb->pitch / 4;
    gx  = 8;
    gy  = 8;
}

void terminal_set_fg(uint32_t color) {
    uint64_t f = terminal_lock();
    fg = color;
    terminal_unlock(f);
}
void terminal_set_bg(uint32_t color) {
    uint64_t f = terminal_lock();
    bg = color;
    terminal_unlock(f);
}
uint32_t terminal_get_fg(void)       { return fg;  }

void terminal_set_scale(uint32_t scale) {
    uint64_t f = terminal_lock();
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    ts = scale;
    gx = 8; gy = 8;
    terminal_unlock(f);
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

static void fb_scroll_copy(uint32_t *dst, uint32_t *src, uint32_t count) {
    uint64_t *d64 = (uint64_t *)dst;
    uint64_t *s64 = (uint64_t *)src;
    uint32_t n64 = count / 2;
    for (uint32_t i = 0; i < n64; i++) d64[i] = s64[i];
    if (count & 1) dst[count - 1] = src[count - 1];
}

static void scroll(void) {
    gpipe_ctx_t *gctx   = gpipe_default();
    uint32_t    *target = NULL;
    uint32_t     tpitch = 0;
    gpipe_get_draw_target(gctx, &target, &tpitch);

    uint32_t *fbb;
    uint32_t pitch;
    if (target) {
        fbb   = target;
        pitch = tpitch;
    } else {
        fbb   = (uint32_t *)fbi->address;
        pitch = pw;
    }

    uint32_t copy_rows = fbi->height - CELL_H;
    fb_scroll_copy(fbb, fbb + (size_t)CELL_H * pitch, copy_rows * pitch);

    for (uint32_t r = copy_rows; r < fbi->height; r++) {
        uint32_t *p = fbb + r * pitch;
        for (uint32_t c = 0; c < fbi->width; c++) p[c] = bg;
    }
    gy = fbi->height - CELL_H;

    if (target) {
        gpipe_mark_dirty(gctx, 0, 0, (int)fbi->width, (int)fbi->height);
        gpipe_flip_full(gctx);
    }
}

static void putchar_nolock(char c) {
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

void terminal_putchar(char c) {
    uint64_t f = terminal_lock();
    putchar_nolock(c);
    terminal_unlock(f);
}

void terminal_print(const char *s) {
    uint64_t f = terminal_lock();
    for (int i = 0; s[i]; i++) putchar_nolock(s[i]);
    terminal_unlock(f);
}

void terminal_println(const char *s) {
    uint64_t f = terminal_lock();
    for (int i = 0; s[i]; i++) putchar_nolock(s[i]);
    putchar_nolock('\n');
    terminal_unlock(f);
}

void terminal_ensure_newline(void) {
    uint64_t f = terminal_lock();
    if (gx != 8) putchar_nolock('\n');
    terminal_unlock(f);
}

void terminal_print_int(uint32_t n) {
    char b[11]; int i = 10; b[i--] = 0;
    do { b[i--] = '0' + n % 10; n /= 10; } while (n);
    terminal_print(&b[i + 1]);
}

void terminal_print_hex(uint64_t n) {
    char h[] = "0123456789ABCDEF";
    uint64_t f = terminal_lock();
    for (int i = 0; i < 2; i++) putchar_nolock("0x"[i]);
    for (int i = 15; i >= 0; i--)
        putchar_nolock(h[(n >> (i * 4)) & 0xF]);
    terminal_unlock(f);
}

void terminal_clear(void) {
    uint64_t lockf = terminal_lock();
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
    terminal_unlock(lockf);
}

void terminal_cursor_draw(int visible) {
    uint64_t lockf = terminal_lock();

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

    terminal_unlock(lockf);
}