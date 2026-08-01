#include "../commands.h"
#include "util.h"
#include "../../graphics/terminal.h"
#include "../../fs/vfs.h"
#include "../../fs/ramdisk.h"
#include "../../fs/fat32.h"
#include <libk/string.h>

static int str_contains(const char *hay, const char *needle) {
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (needle[j] && hay[i + j] == needle[j]) j++;
        if (!needle[j]) return 1;
    }
    return 0;
}

static int is_macos_junk(const char *name) {
    if (name[0] == '_' && name[1] == 'T') return 0;
    if (name[0] == '_') return 1;
    if (str_contains(name, "FSEVEN"))  return 1;
    if (str_contains(name, "SPOTLI"))  return 1;
    if (str_contains(name, "TRASHE"))  return 1;
    if (str_contains(name, "DS_STOR")) return 1;
    if (str_contains(name, "EASTEREGG")) return 1;
    return 0;
}

static int ls_show_hidden = 0;
static int ls_col = 0;
static int ls_total = 0;

#define LS_COL_WIDTH 16
#define LS_COLS      5

static int is_noisy_dev(const char *name, uint32_t flags) {
    if (flags & VFS_CHARDEV) return 1;
    if ((flags & VFS_BLOCKDEV) && !strcmp(name, "ram0")) return 1;
    return 0;
}

static void ls_print(const char *label, int is_dir) {
    int len = str_len(label);
    terminal_print(label);
    if (is_dir) { terminal_putchar('/'); len++; }
    for (int i = len; i < LS_COL_WIDTH; i++) terminal_putchar(' ');

    ls_col++;
    ls_total++;
    if (ls_col >= LS_COLS) { terminal_println(""); ls_col = 0; }
}

static void vfsls_all_cb(const char *name, uint32_t flags, uint32_t size) {
    (void)size;

    if (flags & VFS_BLOCKDEV) {
        if (is_noisy_dev(name, flags)) return;
        terminal_set_fg(COLOR_HIGHLIGHT);
        ls_print(name, 0);
        return;
    }
    if (flags & VFS_CHARDEV) return;

    if (!ls_show_hidden && is_macos_junk(name)) return;

    char full[80];
    int o = 0;
    full[o++] = 's'; full[o++] = 'd'; full[o++] = 'a'; full[o++] = '/';
    for (int i = 0; name[i] && o < 78; i++) full[o++] = name[i];
    full[o] = 0;

    terminal_set_fg((flags & VFS_DIRECTORY) ? COLOR_WARNING : COLOR_BODY);
    ls_print(full, (flags & VFS_DIRECTORY) ? 1 : 0);
}

void cmd_vfsls_ex(int show_hidden) {
    ls_show_hidden = show_hidden;
    ls_col = 0;
    ls_total = 0;

    vfs_list(vfsls_all_cb);
    if (ls_col != 0) terminal_println("");
    if (ls_total == 0) {
        terminal_set_fg(COLOR_DIM);
        terminal_println("  (empty)");
    }
    terminal_println("");
}

void cmd_vfsls(void) {
    cmd_vfsls_ex(0);
}

void cmd_vfsread(const char *dev) {
    vfs_node_t *node = vfs_find(dev);
    if (!node) {
        terminal_set_fg(COLOR_ERROR);
        terminal_print("  Not found: ");
        terminal_println(dev);
        return;
    }
    uint8_t buf[256];
    uint32_t n = vfs_read(node, 0, 255, buf);
    buf[n] = 0;

    terminal_set_fg(COLOR_BODY);
    terminal_print("\n  Read ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(n);
    terminal_set_fg(COLOR_BODY);
    terminal_println(" bytes:");
    terminal_print("  ");

    for (uint32_t i = 0; i < n; i++) {
        char c = (char)buf[i];
        terminal_set_fg(COLOR_HIGHLIGHT);
        terminal_putchar((c >= 32 && c < 127) ? c : '.');
    }
    terminal_println("\n");
}

void cmd_vfswrite(const char *dev, const char *data) {
    vfs_node_t *node = vfs_find(dev);
    if (!node) {
        terminal_set_fg(COLOR_ERROR);
        terminal_print("  Not found: ");
        terminal_println(dev);
        return;
    }
    uint32_t len = (uint32_t)str_len(data);
    uint32_t n = vfs_write(node, 0, len, (const uint8_t *)data);
    terminal_set_fg(COLOR_SUCCESS);
    terminal_print("\n  Wrote ");
    terminal_print_int(n);
    terminal_println(" bytes\n");
}

void cmd_touch(const char *name) {
    if (!fat32_present()) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  /dev/sda has no FAT32 filesystem mounted");
        return;
    }
    if (vfs_find(name)) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  File already exists");
        return;
    }
    if (fat32_create_file(name)) {
        terminal_set_fg(COLOR_SUCCESS);
        terminal_print("  Created ");
        terminal_println(name);
    } else {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Failed to create file (disk full or root dir full)");
    }
}

void cmd_mkdir(const char *name) {
    if (!fat32_present()) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  /dev/sda has no FAT32 filesystem mounted");
        return;
    }
    if (fat32_mkdir(name)) {
        terminal_set_fg(COLOR_SUCCESS);
        terminal_print("  Created directory ");
        terminal_println(name);
        terminal_set_fg(COLOR_DIM);
        terminal_println("  (run 'ls' again to see files inside it)");
    } else {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Failed to create directory (disk full or root dir full)");
    }
}

void cmd_ramls(void) {
    print_header("Ramdisk Contents");
    if (!ramfile_count) {
        terminal_set_fg(COLOR_DIM);
        terminal_println("  (empty)");
    } else {
        for (int i = 0; i < MAX_RAMFILES; i++) {
            if (ramfiles[i].name[0]) {
                terminal_set_fg(COLOR_HIGHLIGHT);
                terminal_print("  ");
                terminal_print(ramfiles[i].name);
                terminal_set_fg(COLOR_BODY);
                terminal_print("  (");
                terminal_print_int(ramfiles[i].size);
                terminal_println(" bytes)");
            }
        }
    }
    terminal_println("");
}

void cmd_raminfo(void) {
    print_header("Ramdisk Statistics");
    terminal_set_fg(COLOR_BODY);
    terminal_print("  Files:      ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(ramfile_count);
    terminal_println("");

    terminal_set_fg(COLOR_BODY);
    terminal_print("  Free slots: ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_int(MAX_RAMFILES - ramfile_count);
    terminal_println("");
    terminal_println("");
}

void cmd_ramcat(const char *name) {
    int idx = ramdisk_find(name);
    if (idx < 0) {
        terminal_set_fg(COLOR_ERROR);
        terminal_print("  Not found: ");
        terminal_println(name);
        return;
    }
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print("  ");
    for (uint32_t i = 0; i < ramfiles[idx].size; i++) {
        terminal_putchar((char)ramfiles[idx].data[i]);
    }
    terminal_println("");
}

void cmd_ramwrite(const char *name, const char *content) {
    int r = ramdisk_create(name, (const uint8_t *)content, (uint32_t)str_len(content));
    if (r == 0) {
        terminal_set_fg(COLOR_SUCCESS);
        terminal_print("  Written: ");
        terminal_println(name);
    } else {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Error: Could not write file.");
    }
}

void cmd_ramdel(const char *name) {
    int r = ramdisk_delete(name);
    if (r == 0) {
        terminal_set_fg(COLOR_SUCCESS);
        terminal_print("  Deleted: ");
        terminal_println(name);
    } else {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Error: File not found.");
    }
}