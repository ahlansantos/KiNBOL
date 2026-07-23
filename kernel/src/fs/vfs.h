#pragma once
#include <stdint.h>
#include <stddef.h>

#define VFS_FILE      0x01
#define VFS_DIRECTORY 0x02
#define VFS_CHARDEV   0x04
#define VFS_BLOCKDEV  0x08
#define VFS_PIPE      0x10
#define VFS_SYMLINK   0x20
#define VFS_MOUNTPT   0x40

#define VFS_MAX_NODES 64
#define VFS_NAME_MAX  64

typedef struct vfs_node {
    char     name[VFS_NAME_MAX];
    uint32_t flags;
    uint32_t size;
    void    *device;

    uint32_t (*read) (struct vfs_node *node, uint32_t offset,
                      uint32_t len, uint8_t *buf);
    uint32_t (*write)(struct vfs_node *node, uint32_t offset,
                      uint32_t len, const uint8_t *buf);
    void     (*open) (struct vfs_node *node);
    void     (*close)(struct vfs_node *node);
} vfs_node_t;

void vfs_init(void);

int vfs_register(vfs_node_t *node);

vfs_node_t *vfs_find(const char *name);

uint32_t vfs_read (vfs_node_t *node, uint32_t offset, uint32_t len, uint8_t *buf);
uint32_t vfs_write(vfs_node_t *node, uint32_t offset, uint32_t len, const uint8_t *buf);

void vfs_list(void (*cb)(const char *name, uint32_t flags, uint32_t size));

int vfs_node_count(void);