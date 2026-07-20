#include "../commands.h"
#include "util.h"
#include "../../graphics/terminal.h"
#include "../../fs/vfs.h"
#include "../../fs/ramdisk.h"

static void vfsls_cb(const char *name, uint32_t flags, uint32_t size) {
    terminal_set_fg((flags & VFS_BLOCKDEV) ? COLOR_WARNING : COLOR_HIGHLIGHT);
    terminal_print("  /dev/");
    terminal_print(name);
    if (size) {
        terminal_set_fg(COLOR_BODY);
        terminal_print("  (");
        terminal_print_int(size);
        terminal_print(" bytes)");
    }
    terminal_println("");
}

void cmd_vfsls(void) {
    print_header("/dev Nodes");
    vfs_list(vfsls_cb);
    terminal_println("");
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