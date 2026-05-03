#pragma once

void cmd_help(void);
void cmd_ticks(void);
void cmd_sleep(const char *arg);
void cmd_crash(void);
void cmd_fastfetch(void);
void cmd_memtest(void);
void cmd_reboot(void);
void cmd_anim(void);
void cmd_date(void);
void cmd_meminfo(void);
void cmd_ascii(void);
void cmd_dmesg(void);
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