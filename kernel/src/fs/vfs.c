#include "vfs.h"
#include "../kernel/dmesg.h"
#include <stdint.h>
#include <stddef.h>

static vfs_node_t *nodes[VFS_MAX_NODES];
static int         node_count = 0;

static uint32_t null_read(vfs_node_t *n, uint32_t off, uint32_t len, uint8_t *buf) {
    (void)n; (void)off; (void)len; (void)buf;
    return 0; 
}
static uint32_t null_write(vfs_node_t *n, uint32_t off, uint32_t len, const uint8_t *buf) {
    (void)n; (void)off; (void)buf;
    return len;
}

static vfs_node_t dev_null = {
    .name  = "null",
    .flags = VFS_CHARDEV,
    .size  = 0,
    .read  = null_read,
    .write = null_write,
};

static uint32_t zero_read(vfs_node_t *n, uint32_t off, uint32_t len, uint8_t *buf) {
    (void)n; (void)off;
    for (uint32_t i = 0; i < len; i++) buf[i] = 0;
    return len;
}

static vfs_node_t dev_zero = {
    .name  = "zero",
    .flags = VFS_CHARDEV,
    .size  = 0,
    .read  = zero_read,
    .write = null_write,
};

static uint64_t rng_state = 0x123456789ABCDEF0ULL;

static uint8_t rng_next(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint8_t)(rng_state & 0xFF);
}

static uint32_t random_read(vfs_node_t *n, uint32_t off, uint32_t len, uint8_t *buf) {
    (void)n; (void)off;
    for (uint32_t i = 0; i < len; i++) buf[i] = rng_next();
    return len;
}

static uint32_t random_write(vfs_node_t *n, uint32_t off, uint32_t len, const uint8_t *buf) {
    (void)n; (void)off;
    for (uint32_t i = 0; i < len; i++) rng_state ^= ((uint64_t)buf[i] << ((i % 8) * 8));
    return len;
}

static vfs_node_t dev_random = {
    .name  = "random",
    .flags = VFS_CHARDEV,
    .size  = 0,
    .read  = random_read,
    .write = random_write,
};

extern void terminal_putchar(char c);

static uint32_t tty_write(vfs_node_t *n, uint32_t off, uint32_t len, const uint8_t *buf) {
    (void)n; (void)off;
    for (uint32_t i = 0; i < len; i++) terminal_putchar((char)buf[i]);
    return len;
}
static uint32_t tty_read(vfs_node_t *n, uint32_t off, uint32_t len, uint8_t *buf) {
    (void)n; (void)off; (void)len; (void)buf;
    return 0;
}

static vfs_node_t dev_tty = {
    .name  = "tty",
    .flags = VFS_CHARDEV,
    .size  = 0,
    .read  = tty_read,
    .write = tty_write,
};

static uint32_t dmesg_vfs_read(vfs_node_t *n, uint32_t off, uint32_t len, uint8_t *buf) {
    (void)n; (void)off; (void)len; (void)buf;

    return 0;
}
static uint32_t dmesg_vfs_write(vfs_node_t *n, uint32_t off, uint32_t len, const uint8_t *buf) {
    (void)n; (void)off;

    char tmp[2] = {0, 0};
    for (uint32_t i = 0; i < len; i++) {
        tmp[0] = (char)buf[i];
        dmesg(tmp);
    }
    return len;
}

static vfs_node_t dev_dmesg_node = {
    .name  = "dmesg",
    .flags = VFS_CHARDEV,
    .size  = 0,
    .read  = dmesg_vfs_read,
    .write = dmesg_vfs_write,
};

#define RAM0_SIZE (64 * 1024) 

typedef struct {
    uint8_t *data;
    uint32_t size;
} ram0_dev_t;

static ram0_dev_t ram0_dev;

static uint32_t ram0_read(vfs_node_t *n, uint32_t off, uint32_t len, uint8_t *buf) {
    ram0_dev_t *dev = (ram0_dev_t *)n->device;
    if (!dev || !dev->data) return 0;
    if (off >= dev->size) return 0;
    if (off + len > dev->size) len = dev->size - off;
    for (uint32_t i = 0; i < len; i++) buf[i] = dev->data[off + i];
    return len;
}

static uint32_t ram0_write(vfs_node_t *n, uint32_t off, uint32_t len, const uint8_t *buf) {
    ram0_dev_t *dev = (ram0_dev_t *)n->device;
    if (!dev || !dev->data) return 0;
    if (off >= dev->size) return 0;
    if (off + len > dev->size) len = dev->size - off;
    for (uint32_t i = 0; i < len; i++) dev->data[off + i] = buf[i];
    return len;
}

static vfs_node_t dev_ram0 = {
    .name   = "ram0",
    .flags  = VFS_BLOCKDEV,
    .size   = RAM0_SIZE,
    .read   = ram0_read,
    .write  = ram0_write,
    .device = &ram0_dev,
};

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}
static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

extern void *kmalloc(size_t size);

void vfs_init(void) {
    node_count = 0;
    for (int i = 0; i < VFS_MAX_NODES; i++) nodes[i] = NULL;

    ram0_dev.data = (uint8_t *)kmalloc(RAM0_SIZE);
    ram0_dev.size = RAM0_SIZE;
    if (ram0_dev.data) {
        for (uint32_t i = 0; i < RAM0_SIZE; i++) ram0_dev.data[i] = 0;
    }

    vfs_register(&dev_null);
    vfs_register(&dev_zero);
    vfs_register(&dev_random);
    vfs_register(&dev_tty);
    vfs_register(&dev_dmesg_node);
    vfs_register(&dev_ram0);

    dmesg("[vfs] initialized with 6 default nodes\n");
}

int vfs_register(vfs_node_t *node) {
    if (!node || node_count >= VFS_MAX_NODES) return -1;
    nodes[node_count++] = node;
    dmesg("[vfs] registered /dev/");
    dmesg(node->name);
    dmesg("\n");
    return 0;
}

vfs_node_t *vfs_find(const char *name) {
    const char *n = name;
    if (n[0] == '/' && n[1] == 'd' && n[2] == 'e' && n[3] == 'v' && n[4] == '/') n += 5;
    for (int i = 0; i < node_count; i++)
        if (nodes[i] && str_eq(nodes[i]->name, n)) return nodes[i];
    return NULL;
}

uint32_t vfs_read(vfs_node_t *node, uint32_t offset, uint32_t len, uint8_t *buf) {
    if (!node || !node->read) return 0;
    return node->read(node, offset, len, buf);
}

uint32_t vfs_write(vfs_node_t *node, uint32_t offset, uint32_t len, const uint8_t *buf) {
    if (!node || !node->write) return 0;
    return node->write(node, offset, len, buf);
}

void vfs_list(void (*cb)(const char *name, uint32_t flags, uint32_t size)) {
    for (int i = 0; i < node_count; i++)
        if (nodes[i]) cb(nodes[i]->name, nodes[i]->flags, nodes[i]->size);
}

int vfs_node_count(void) { return node_count; }