#include "terminal.h"
#include "../graphics/font.h"
#include <stdint.h>

static struct limine_framebuffer *fbi = 0;
static uint32_t pw   = 0;
static uint32_t gx   = 8;
static uint32_t gy   = 8;
static uint32_t fg   = 0xFFFFFF;
static uint32_t bg   = 0x00111122;

void terminal_init(struct limine_framebuffer *fb) {
    fbi = fb;
    pw  = fb->pitch / 4;
    gx  = 8;
    gy  = 8;
}

void terminal_set_fg(uint32_t color) { fg = color; }
void terminal_set_bg(uint32_t color) { bg = color; }
uint32_t terminal_get_fg(void)       { return fg;  }

static void px(uint32_t x, uint32_t y, uint32_t c) {
    if (x < fbi->width && y < fbi->height)
        ((volatile uint32_t *)fbi->address)[y * pw + x] = c;
}

static void chr(uint32_t x, uint32_t y, char c, uint32_t cf, uint32_t cb) {
    const uint8_t *g = font[(unsigned char)c];
    for (int r = 0; r < 16; r++)
        for (int co = 0; co < 8; co++)
            px(x + co, y + r, (g[r] & (1 << (7 - co))) ? cf : cb);
}

static void scroll(void) {
    volatile uint32_t *fbb = fbi->address;
    for (uint32_t r = 0; r < fbi->height - 16; r++) {
        uint32_t *d = (uint32_t *)(fbb + r * pw);
        uint32_t *s = (uint32_t *)(fbb + (r + 16) * pw);
        for (uint32_t c = 0; c < fbi->width; c++) d[c] = s[c];
    }
    for (uint32_t r = fbi->height - 16; r < fbi->height; r++) {
        uint32_t *p = (uint32_t *)(fbb + r * pw);
        for (uint32_t c = 0; c < fbi->width; c++) p[c] = bg;
    }
    gy = fbi->height - 16;
}

void terminal_putchar(char c) {
    if (c == '\n') {
        gx = 8; gy += 16;
        if (gy >= fbi->height - 16) scroll();
        return;
    }
    if (c == '\b') {
        if (gx >= 16) { gx -= 8; chr(gx, gy, ' ', fg, bg); }
        return;
    }
    chr(gx, gy, c, fg, bg);
    gx += 8;
    if (gx >= fbi->width - 8) {
        gx = 8; gy += 16;
        if (gy >= fbi->height - 16) scroll();
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
    for (uint32_t y = 0; y < fbi->height; y++)
        for (uint32_t x = 0; x < fbi->width; x++)
            px(x, y, bg);
    gx = 8; gy = 8;
}

void terminal_cursor_draw(int visible) {
    uint32_t color = visible ? fg : bg;
    for (int co = 0; co < 8; co++) {
        px(gx + co, gy + 11, color);
        px(gx + co, gy + 12, color);
    }
}