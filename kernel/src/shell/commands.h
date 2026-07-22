/*
 * Shared header for every shell command: the color constants used in
 * output (COLOR_HEADER, COLOR_BODY, etc) and the prototypes for each
 * command (calc, fs, gfx, info, mem, sys).
 */
#pragma once

void cmd_help(void);
void cmd_ticks(void);
void cmd_sleep(const char *arg);
void cmd_crash(void);
void cmd_fastfetch(void);
void cmd_memtest(void);
void cmd_reboot(void);
void cmd_shutdown(void);
void cmd_anim(void);
void cmd_date(void);
void cmd_meminfo(void);
void cmd_ascii(void);
void cmd_dmesg(void);
void cmd_drawtest(void);
void cmd_pixel(int x, int y);
void cmd_line(int x0, int y0, int x1, int y1);
void cmd_rect(int x, int y, int w, int h);
void cmd_fillrect(int x, int y, int w, int h);
void cmd_circle(int x, int y, int r);
void cmd_scale(const char *arg);
void cmd_clearfb(void);
void cmd_baregl_status(void);
void cmd_vfsls(void);
void cmd_vminfo(void);
void cmd_ramls(void);
void cmd_raminfo(void);
void cmd_ramcat(const char *name);
void cmd_ramwrite(const char *name, const char *content);
void cmd_ramdel(const char *name);
void cmd_calc(const char *expr);
void cmd_hexdump(const char *addr_str, const char *len_str);
void cmd_peek(const char *addr_str);
void cmd_poke(const char *addr_str, const char *val_str);
void cmd_vfsread(const char *dev);
void cmd_vfswrite(const char *dev, const char *data);
void cmd_ps(void);
void cmd_schedtest(void);
void cmd_sleeptest(void);
void cmd_top(void);