#include "../commands.h"
#include "util.h"
#include "../../graphics/terminal.h"
#include "../../fs/vfs.h"
#include "../../fs/ramdisk.h"

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
static int ls_dev_count, ls_file_count, ls_hidden_count;

static void vfsls_devs_cb(const char *name, uint32_t flags, uint32_t size) {
    if (flags & VFS_FILE) return;
    terminal_set_fg(COLOR_DIM);
    terminal_print("  [device]  ");
    terminal_set_fg((flags & VFS_BLOCKDEV) ? COLOR_WARNING : COLOR_HIGHLIGHT);
    terminal_print(name);
    if (size) {
        terminal_set_fg(COLOR_BODY);
        terminal_print("  (");
        terminal_print_int(size);
        terminal_print(" bytes)");
    }
    terminal_println("");
    ls_dev_count++;
}

static void vfsls_files_cb(const char *name, uint32_t flags, uint32_t size) {
    if (!(flags & VFS_FILE)) return;
    if (!ls_show_hidden && is_macos_junk(name)) { ls_hidden_count++; return; }
    terminal_set_fg(COLOR_DIM);
    terminal_print("  [file]    ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print(name);
    terminal_set_fg(COLOR_BODY);
    terminal_print("  (");
    terminal_print_int(size);
    terminal_print(" bytes)");
    terminal_println("");
    ls_file_count++;
}

void cmd_vfsls_ex(int show_hidden) {
    ls_show_hidden = show_hidden;
    ls_dev_count = ls_file_count = ls_hidden_count = 0;

    print_header("Devices");
    vfs_list(vfsls_devs_cb);
    terminal_println("");

    print_header("Disk Files");
    vfs_list(vfsls_files_cb);
    if (ls_file_count == 0) {
        terminal_set_fg(COLOR_DIM);
        terminal_println("  (none)");
    }
    if (!show_hidden && ls_hidden_count) {
        terminal_set_fg(COLOR_DIM);
        terminal_print("  (+");
        terminal_print_int(ls_hidden_count);
        terminal_println(" macOS metadata files hidden, use 'ls -a')");
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