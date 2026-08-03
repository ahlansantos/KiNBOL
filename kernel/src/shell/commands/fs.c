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

static int is_noisy_dev(const char *name, uint32_t flags) {
    if (flags & VFS_CHARDEV) return 1;
    if ((flags & VFS_BLOCKDEV) && !strcmp(name, "ram0")) return 1;
    return 0;
}

static int path_depth(const char *name) {
    int d = 0;
    for (int i = 0; name[i]; i++) if (name[i] == '/') d++;
    return d;
}

static const char *path_leaf(const char *name) {
    const char *leaf = name;
    for (int i = 0; name[i]; i++) if (name[i] == '/') leaf = name + i + 1;
    return leaf;
}

static int name_lt(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca < cb;
        a++; b++;
    }
    return *a == 0 && *b != 0;
}

#define LS_MAX_ENTRIES 64

typedef struct { char name[VFS_NAME_MAX]; int is_dir; } ls_entry_t;

static ls_entry_t ls_entries[LS_MAX_ENTRIES];
static int        ls_entry_count = 0;
static int        ls_show_hidden = 0;

static void vfsls_collect_cb(const char *name, uint32_t flags, uint32_t size) {
    (void)size;

    if (flags & VFS_BLOCKDEV) {
        if (is_noisy_dev(name, flags)) return;
        if (ls_entry_count < LS_MAX_ENTRIES) {
            strncpy(ls_entries[ls_entry_count].name, name, VFS_NAME_MAX - 1);
            ls_entries[ls_entry_count].name[VFS_NAME_MAX - 1] = 0;
            ls_entries[ls_entry_count].is_dir = -1;
            ls_entry_count++;
        }
        return;
    }
    if (flags & VFS_CHARDEV) return;

    if (!ls_show_hidden && is_macos_junk(path_leaf(name))) return;
    if (ls_entry_count >= LS_MAX_ENTRIES) return;

    strncpy(ls_entries[ls_entry_count].name, name, VFS_NAME_MAX - 1);
    ls_entries[ls_entry_count].name[VFS_NAME_MAX - 1] = 0;
    ls_entries[ls_entry_count].is_dir = (flags & VFS_DIRECTORY) ? 1 : 0;
    ls_entry_count++;
}

static int ls_entry_before(const ls_entry_t *a, const ls_entry_t *b) {
    if (a->is_dir == -1 || b->is_dir == -1) return a->is_dir == -1;
    return name_lt(a->name, b->name);
}

static void ls_sort(void) {
    for (int i = 1; i < ls_entry_count; i++) {
        ls_entry_t key = ls_entries[i];
        int j = i - 1;
        while (j >= 0 && ls_entry_before(&key, &ls_entries[j])) {
            ls_entries[j + 1] = ls_entries[j];
            j--;
        }
        ls_entries[j + 1] = key;
    }
}

void cmd_vfsls_ex(int show_hidden) {
    ls_show_hidden = show_hidden;
    ls_entry_count = 0;

    vfs_list(vfsls_collect_cb);
    ls_sort();

    if (ls_entry_count == 0) {
        terminal_set_fg(COLOR_DIM);
        terminal_println("  (empty)");
        terminal_println("");
        return;
    }

    for (int i = 0; i < ls_entry_count; i++) {
        ls_entry_t *e = &ls_entries[i];

        if (e->is_dir == -1) {
            terminal_set_fg(COLOR_HIGHLIGHT);
            terminal_print("  ");
            terminal_println(e->name);
            continue;
        }

        int depth = path_depth(e->name);
        terminal_print("  ");
        for (int d = 0; d < depth; d++) terminal_print("  ");
        if (depth == 0) terminal_print("sda/");

        terminal_set_fg(e->is_dir ? COLOR_WARNING : COLOR_BODY);
        terminal_print(path_leaf(e->name));
        if (e->is_dir) terminal_putchar('/');
        terminal_println("");
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
    char real[VFS_NAME_MAX];
    int r = fat32_create_file(name, real);
    if (r == FAT32_OK) {
        terminal_set_fg(COLOR_SUCCESS);
        terminal_print("  Created ");
        terminal_println(real);
    } else if (r == FAT32_ERR_EXISTS) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Already exists");
    } else if (r == FAT32_ERR_NO_PARENT) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Parent directory not found (check 'ls' for the exact name)");
    } else if (r == FAT32_ERR_NO_FS) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  /dev/sda has no FAT32 filesystem mounted");
    } else {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Failed to create file (disk full or too many VFS nodes)");
    }
}

void cmd_mkdir(const char *name) {
    if (!fat32_present()) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  /dev/sda has no FAT32 filesystem mounted");
        return;
    }
    char real[VFS_NAME_MAX];
    int r = fat32_mkdir(name, real);
    if (r == FAT32_OK) {
        terminal_set_fg(COLOR_SUCCESS);
        terminal_print("  Created directory ");
        terminal_println(real);
        terminal_set_fg(COLOR_DIM);
        terminal_println("  (run 'ls' again to see files inside it)");
    } else if (r == FAT32_ERR_EXISTS) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Already exists");
    } else if (r == FAT32_ERR_NO_PARENT) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Parent directory not found (check 'ls' for the exact name)");
    } else if (r == FAT32_ERR_NO_FS) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  /dev/sda has no FAT32 filesystem mounted");
    } else {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  Failed to create directory (disk full or too many VFS nodes)");
    }
}

static void print_remove_error(int r) {
    terminal_set_fg(COLOR_ERROR);
    if (r == FAT32_ERR_NOT_FOUND) terminal_println("  Not found");
    else if (r == FAT32_ERR_NOT_EMPTY) terminal_println("  Directory not empty (use 'rm -r <path>')");
    else if (r == FAT32_ERR_NO_FS) terminal_println("  /dev/sda has no FAT32 filesystem mounted");
    else terminal_println("  Failed to remove");
}

void cmd_rm(const char *name, int recursive) {
    if (!fat32_present()) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  /dev/sda has no FAT32 filesystem mounted");
        return;
    }
    vfs_node_t *n = vfs_find(name);
    if (n && (n->flags & VFS_DIRECTORY) && !recursive) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  That's a directory (use 'rmdir' or 'rm -r <path>')");
        return;
    }
    char real[VFS_NAME_MAX];
    int r = fat32_remove(name, recursive, real);
    if (r == FAT32_OK) {
        terminal_set_fg(COLOR_SUCCESS);
        terminal_print("  Removed ");
        terminal_println(real);
    } else {
        print_remove_error(r);
    }
}

void cmd_rmdir(const char *name) {
    if (!fat32_present()) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  /dev/sda has no FAT32 filesystem mounted");
        return;
    }
    vfs_node_t *n = vfs_find(name);
    if (n && !(n->flags & VFS_DIRECTORY)) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  That's a file (use 'rm')");
        return;
    }
    char real[VFS_NAME_MAX];
    int r = fat32_remove(name, 0, real);
    if (r == FAT32_OK) {
        terminal_set_fg(COLOR_SUCCESS);
        terminal_print("  Removed directory ");
        terminal_println(real);
    } else {
        print_remove_error(r);
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