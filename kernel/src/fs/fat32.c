#include "fat32.h"
#include "vfs.h"
#include "../drivers/ahci.h"
#include "../kernel/dmesg.h"
#include "../mm/heap.h"
#include <libk/string.h>

#define FAT32_MAX_FILES   40
#define FAT32_MAX_DEPTH   3

typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors16;
    uint8_t  media;
    uint16_t fat_size16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors32;

    uint32_t fat_size32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved0[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} __attribute__((packed)) fat_dir_entry_t;

typedef struct {
    uint32_t start_cluster;
    uint32_t dirent_sector;
    uint32_t dirent_offset;
} fat32_file_ctx_t;

static int      fat_ok               = 0;
static uint32_t bytes_per_sector     = 512;
static uint32_t sectors_per_cluster  = 1;
static uint32_t num_fats             = 2;
static uint32_t fat_size32           = 0;
static uint32_t fat_start            = 0;
static uint32_t data_start           = 0;
static uint32_t root_cluster         = 2;
static uint32_t total_clusters       = 0;
static uint32_t next_free_hint       = 2;

static vfs_node_t fat_files[FAT32_MAX_FILES];
static int        fat_file_count = 0;

static int read_sectors(uint32_t lba, uint32_t count, uint8_t *buf) {
    for (uint32_t i = 0; i < count; i++)
        if (!ahci_read_sector(lba + i, buf + i * bytes_per_sector)) return 0;
    return 1;
}

static int write_sectors(uint32_t lba, uint32_t count, const uint8_t *buf) {
    for (uint32_t i = 0; i < count; i++)
        if (!ahci_write_sector(lba + i, buf + i * bytes_per_sector)) return 0;
    return 1;
}

static inline uint32_t cluster_to_lba(uint32_t cluster) {
    return data_start + (cluster - 2) * sectors_per_cluster;
}

static uint32_t fat_get_entry(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start + (fat_offset / bytes_per_sector);
    uint32_t ent_off    = fat_offset % bytes_per_sector;

    uint8_t sec[512];
    if (!ahci_read_sector(fat_sector, sec)) return 0x0FFFFFFF;

    uint32_t v;
    memcpy(&v, &sec[ent_off], sizeof(uint32_t));
    return v & 0x0FFFFFFF;
}

static void fat_set_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start + (fat_offset / bytes_per_sector);
    uint32_t ent_off    = fat_offset % bytes_per_sector;

    uint8_t sec[512];
    if (!ahci_read_sector(fat_sector, sec)) return;

    uint32_t old;
    memcpy(&old, &sec[ent_off], sizeof(uint32_t));
    uint32_t merged = (old & 0xF0000000) | (value & 0x0FFFFFFF);
    memcpy(&sec[ent_off], &merged, sizeof(uint32_t));

    for (uint32_t f = 0; f < num_fats; f++)
        ahci_write_sector(fat_sector + f * fat_size32, sec);
}

static uint32_t fat32_alloc_cluster(void) {
    uint32_t limit = total_clusters + 2;

    for (uint32_t c = next_free_hint; c < limit; c++) {
        if (fat_get_entry(c) == 0) {
            fat_set_entry(c, 0x0FFFFFFF);
            next_free_hint = c + 1;
            return c;
        }
    }
    for (uint32_t c = 2; c < next_free_hint && c < limit; c++) {
        if (fat_get_entry(c) == 0) {
            fat_set_entry(c, 0x0FFFFFFF);
            next_free_hint = c + 1;
            return c;
        }
    }
    dmesg("[fat32] disk full, no free clusters\n");
    return 0;
}

static void update_dirent_cluster(fat32_file_ctx_t *ctx) {
    uint8_t sec[512];
    if (!ahci_read_sector(ctx->dirent_sector, sec)) return;
    fat_dir_entry_t *e = (fat_dir_entry_t *)(sec + ctx->dirent_offset);
    e->fst_clus_hi = (uint16_t)(ctx->start_cluster >> 16);
    e->fst_clus_lo = (uint16_t)(ctx->start_cluster & 0xFFFF);
    ahci_write_sector(ctx->dirent_sector, sec);
}

static void update_dirent_size(fat32_file_ctx_t *ctx, uint32_t size) {
    uint8_t sec[512];
    if (!ahci_read_sector(ctx->dirent_sector, sec)) return;
    fat_dir_entry_t *e = (fat_dir_entry_t *)(sec + ctx->dirent_offset);
    e->file_size = size;
    ahci_write_sector(ctx->dirent_sector, sec);
}

static uint32_t fat32_file_read(vfs_node_t *n, uint32_t off, uint32_t len, uint8_t *buf) {
    fat32_file_ctx_t *ctx = (fat32_file_ctx_t *)n->device;
    if (!ctx || !ctx->start_cluster) return 0;
    if (off >= n->size) return 0;
    if (off + len > n->size) len = n->size - off;

    uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
    uint32_t cluster = ctx->start_cluster;
    uint32_t skip = off;
    uint32_t done = 0;

    while (skip >= cluster_size) {
        cluster = fat_get_entry(cluster);
        if (cluster >= 0x0FFFFFF8) return done;
        skip -= cluster_size;
    }

    uint8_t *cbuf = (uint8_t *)kmalloc(cluster_size);
    if (!cbuf) return 0;

    while (done < len) {
        if (!read_sectors(cluster_to_lba(cluster), sectors_per_cluster, cbuf)) break;
        uint32_t chunk = cluster_size - skip;
        if (chunk > len - done) chunk = len - done;
        memcpy(buf + done, cbuf + skip, chunk);
        done += chunk;
        skip = 0;
        cluster = fat_get_entry(cluster);
        if (cluster >= 0x0FFFFFF8) break;
    }

    kfree(cbuf);
    return done;
}

static uint32_t fat32_file_write(vfs_node_t *n, uint32_t off, uint32_t len, const uint8_t *buf) {
    fat32_file_ctx_t *ctx = (fat32_file_ctx_t *)n->device;
    if (!ctx) return 0;

    uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;

    if (ctx->start_cluster == 0) {
        uint32_t c = fat32_alloc_cluster();
        if (!c) return 0;
        ctx->start_cluster = c;
        update_dirent_cluster(ctx);
    }

    uint32_t cluster = ctx->start_cluster;
    uint32_t skip = off;
    while (skip >= cluster_size) {
        uint32_t next = fat_get_entry(cluster);
        if (next >= 0x0FFFFFF8) {
            uint32_t newc = fat32_alloc_cluster();
            if (!newc) return 0;
            fat_set_entry(cluster, newc);
            next = newc;
        }
        cluster = next;
        skip -= cluster_size;
    }

    uint8_t *cbuf = (uint8_t *)kmalloc(cluster_size);
    if (!cbuf) return 0;

    uint32_t done = 0;
    while (done < len) {
        uint32_t chunk = cluster_size - skip;
        if (chunk > len - done) chunk = len - done;

        if (skip != 0 || chunk != cluster_size) {
            if (!read_sectors(cluster_to_lba(cluster), sectors_per_cluster, cbuf))
                memset(cbuf, 0, cluster_size);
        }
        memcpy(cbuf + skip, buf + done, chunk);
        if (!write_sectors(cluster_to_lba(cluster), sectors_per_cluster, cbuf)) break;

        done += chunk;
        skip = 0;

        if (done < len) {
            uint32_t next = fat_get_entry(cluster);
            if (next >= 0x0FFFFFF8) {
                uint32_t newc = fat32_alloc_cluster();
                if (!newc) break;
                fat_set_entry(cluster, newc);
                next = newc;
            }
            cluster = next;
        }
    }
    kfree(cbuf);

    if (off + done > n->size) {
        n->size = off + done;
        update_dirent_size(ctx, n->size);
    }
    return done;
}

static void format_name(const char *name8, const char *ext3, char *out) {
    int o = 0;
    for (int i = 0; i < 8 && name8[i] && name8[i] != ' '; i++) out[o++] = name8[i];
    if (ext3[0] && ext3[0] != ' ') {
        out[o++] = '.';
        for (int i = 0; i < 3 && ext3[i] && ext3[i] != ' '; i++) out[o++] = ext3[i];
    }
    out[o] = 0;
}

static void build_full_name(char *out, const char *prefix, const char *leaf) {
    int o = 0;
    for (int i = 0; prefix[i] && o < VFS_NAME_MAX - 1; i++) out[o++] = prefix[i];
    for (int i = 0; leaf[i] && o < VFS_NAME_MAX - 1; i++)   out[o++] = leaf[i];
    out[o] = 0;
}

static char to_upper_c(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

static void to_short_name(const char *name, char *name8, char *ext3) {
    memset(name8, ' ', 8);
    memset(ext3, ' ', 3);

    int i = 0, ni = 0;
    while (name[i] && name[i] != '.' && ni < 8) {
        name8[ni++] = to_upper_c(name[i]);
        i++;
    }
    while (name[i] && name[i] != '.') i++;
    if (name[i] == '.') {
        i++;
        int ei = 0;
        while (name[i] && ei < 3) {
            ext3[ei++] = to_upper_c(name[i]);
            i++;
        }
    }
}

static int find_free_root_slot(uint32_t *out_lba, uint32_t *out_offset, uint32_t *out_cluster) {
    uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
    uint8_t *cbuf = (uint8_t *)kmalloc(cluster_size);
    if (!cbuf) return 0;

    uint32_t cluster = root_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        if (!read_sectors(lba, sectors_per_cluster, cbuf)) { kfree(cbuf); return 0; }

        for (uint32_t i = 0; i < cluster_size; i += 32) {
            fat_dir_entry_t *e = (fat_dir_entry_t *)(cbuf + i);
            if (e->name[0] == 0x00 || (uint8_t)e->name[0] == 0xE5) {
                *out_lba = lba + (i / bytes_per_sector);
                *out_offset = i % bytes_per_sector;
                *out_cluster = cluster;
                kfree(cbuf);
                return 1;
            }
        }
        cluster = fat_get_entry(cluster);
    }
    kfree(cbuf);
    return 0;
}

static int write_dir_entry(uint32_t lba, uint32_t offset, const char *name8, const char *ext3,
                            uint8_t attr, uint32_t cluster, uint32_t size) {
    uint8_t sec[512];
    if (!ahci_read_sector(lba, sec)) return 0;

    fat_dir_entry_t *e = (fat_dir_entry_t *)(sec + offset);
    memset(e, 0, sizeof(fat_dir_entry_t));
    memcpy(e->name, name8, 8);
    memcpy(e->ext, ext3, 3);
    e->attr = attr;
    e->fst_clus_hi = (uint16_t)(cluster >> 16);
    e->fst_clus_lo = (uint16_t)(cluster & 0xFFFF);
    e->file_size = size;

    return ahci_write_sector(lba, sec);
}

int fat32_create_file(const char *name) {
    if (!fat_ok) return 0;
    if (fat_file_count >= FAT32_MAX_FILES) return 0;

    uint32_t lba, offset, dcluster;
    if (!find_free_root_slot(&lba, &offset, &dcluster)) return 0;

    char name8[8], ext3[3];
    to_short_name(name, name8, ext3);

    if (!write_dir_entry(lba, offset, name8, ext3, 0x20, 0, 0)) return 0;

    fat32_file_ctx_t *ctx = (fat32_file_ctx_t *)kmalloc(sizeof(fat32_file_ctx_t));
    if (!ctx) return 0;
    ctx->start_cluster = 0;
    ctx->dirent_sector = lba;
    ctx->dirent_offset = offset;

    vfs_node_t *n = &fat_files[fat_file_count++];
    memset(n, 0, sizeof(vfs_node_t));
    char sname[13];
    format_name(name8, ext3, sname);
    strncpy(n->name, sname, VFS_NAME_MAX - 1);
    n->name[VFS_NAME_MAX - 1] = 0;
    n->flags  = VFS_FILE;
    n->size   = 0;
    n->device = ctx;
    n->read   = fat32_file_read;
    n->write  = fat32_file_write;
    vfs_register(n);

    return 1;
}

int fat32_mkdir(const char *name) {
    if (!fat_ok) return 0;

    uint32_t lba, offset, dcluster;
    if (!find_free_root_slot(&lba, &offset, &dcluster)) return 0;

    uint32_t newc = fat32_alloc_cluster();
    if (!newc) return 0;
    fat_set_entry(newc, 0x0FFFFFFF);

    char name8[8], ext3[3];
    to_short_name(name, name8, ext3);
    if (!write_dir_entry(lba, offset, name8, ext3, 0x10, newc, 0)) return 0;

    uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
    uint8_t *cbuf = (uint8_t *)kmalloc(cluster_size);
    if (!cbuf) return 1;
    memset(cbuf, 0, cluster_size);

    fat_dir_entry_t *dot = (fat_dir_entry_t *)cbuf;
    memset(dot->name, ' ', 8); dot->name[0] = '.';
    memset(dot->ext, ' ', 3);
    dot->attr = 0x10;
    dot->fst_clus_hi = (uint16_t)(newc >> 16);
    dot->fst_clus_lo = (uint16_t)(newc & 0xFFFF);

    fat_dir_entry_t *dotdot = (fat_dir_entry_t *)(cbuf + 32);
    memset(dotdot->name, ' ', 8); dotdot->name[0] = '.'; dotdot->name[1] = '.';
    memset(dotdot->ext, ' ', 3);
    dotdot->attr = 0x10;
    dotdot->fst_clus_hi = (uint16_t)(root_cluster >> 16);
    dotdot->fst_clus_lo = (uint16_t)(root_cluster & 0xFFFF);

    write_sectors(cluster_to_lba(newc), sectors_per_cluster, cbuf);
    kfree(cbuf);

    if (fat_file_count < FAT32_MAX_FILES) {
        vfs_node_t *dn = &fat_files[fat_file_count++];
        memset(dn, 0, sizeof(vfs_node_t));
        char sname[13];
        format_name(name8, ext3, sname);
        strncpy(dn->name, sname, VFS_NAME_MAX - 1);
        dn->name[VFS_NAME_MAX - 1] = 0;
        dn->flags = VFS_DIRECTORY;
        dn->size  = 0;
        vfs_register(dn);
    }

    return 1;
}

static void fat32_scan_dir(uint32_t dir_cluster, const char *prefix, int depth) {
    if (depth > FAT32_MAX_DEPTH) return;

    uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
    uint8_t *cbuf = (uint8_t *)kmalloc(cluster_size);
    if (!cbuf) return;

    uint32_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        if (!read_sectors(lba, sectors_per_cluster, cbuf)) break;

        for (uint32_t i = 0; i < cluster_size; i += 32) {
            fat_dir_entry_t *e = (fat_dir_entry_t *)(cbuf + i);

            if (e->name[0] == 0x00) { kfree(cbuf); return; }
            if ((uint8_t)e->name[0] == 0xE5) continue;
            if ((e->attr & 0x0F) == 0x0F) continue;
            if (e->attr & 0x08) continue;
            if (e->name[0] == '.') continue;

            char sname[13];
            format_name(e->name, e->ext, sname);
            if (!sname[0]) continue;

            char fullname[VFS_NAME_MAX];
            build_full_name(fullname, prefix, sname);

            uint32_t start_cluster = ((uint32_t)e->fst_clus_hi << 16) | e->fst_clus_lo;

            if (e->attr & 0x10) {
                if (fat_file_count < FAT32_MAX_FILES) {
                    vfs_node_t *dn = &fat_files[fat_file_count++];
                    memset(dn, 0, sizeof(vfs_node_t));
                    strncpy(dn->name, fullname, VFS_NAME_MAX - 1);
                    dn->name[VFS_NAME_MAX - 1] = 0;
                    dn->flags = VFS_DIRECTORY;
                    dn->size  = 0;
                    vfs_register(dn);
                }

                if (start_cluster >= 2 && fat_file_count < FAT32_MAX_FILES) {
                    char subprefix[VFS_NAME_MAX];
                    build_full_name(subprefix, fullname, "/");
                    fat32_scan_dir(start_cluster, subprefix, depth + 1);
                }
                continue;
            }

            if (fat_file_count >= FAT32_MAX_FILES) continue;

            fat32_file_ctx_t *ctx = (fat32_file_ctx_t *)kmalloc(sizeof(fat32_file_ctx_t));
            if (!ctx) continue;
            ctx->start_cluster = start_cluster;
            ctx->dirent_sector = lba + (i / bytes_per_sector);
            ctx->dirent_offset = i % bytes_per_sector;

            vfs_node_t *n = &fat_files[fat_file_count++];
            memset(n, 0, sizeof(vfs_node_t));
            strncpy(n->name, fullname, VFS_NAME_MAX - 1);
            n->name[VFS_NAME_MAX - 1] = 0;
            n->flags  = VFS_FILE;
            n->size   = e->file_size;
            n->device = ctx;
            n->read   = fat32_file_read;
            n->write  = fat32_file_write;
            vfs_register(n);
        }

        cluster = fat_get_entry(cluster);
    }

    kfree(cbuf);
}

int fat32_detect(void) {
    if (!ahci_present()) return 0;

    uint8_t bpb_buf[512];
    if (!ahci_read_sector(0, bpb_buf)) return 0;

    fat32_bpb_t *bpb = (fat32_bpb_t *)bpb_buf;
    return (bpb->bytes_per_sector == 512 &&
            bpb->root_entries == 0 &&
            bpb->fat_size16 == 0 &&
            bpb->fat_size32 != 0);
}

void fat32_init(void) {
    if (!ahci_present()) return;

    uint8_t bpb_buf[512];
    if (!ahci_read_sector(0, bpb_buf)) return;

    fat32_bpb_t *bpb = (fat32_bpb_t *)bpb_buf;
    if (bpb->bytes_per_sector != 512) {
        dmesg("[fat32] unsupported sector size\n");
        return;
    }
    if (bpb->fat_size32 == 0 || bpb->sectors_per_cluster == 0) {
        dmesg("[fat32] invalid BPB (not FAT32?)\n");
        return;
    }

    bytes_per_sector    = bpb->bytes_per_sector;
    sectors_per_cluster = bpb->sectors_per_cluster;
    num_fats            = bpb->num_fats ? bpb->num_fats : 2;
    fat_size32           = bpb->fat_size32;
    root_cluster         = bpb->root_cluster ? bpb->root_cluster : 2;
    fat_start             = bpb->reserved_sectors;
    data_start            = fat_start + num_fats * fat_size32;

    uint32_t total_sectors = bpb->total_sectors32 ? bpb->total_sectors32 : bpb->total_sectors16;
    uint32_t data_sectors  = (total_sectors > data_start) ? (total_sectors - data_start) : 0;
    total_clusters         = sectors_per_cluster ? (data_sectors / sectors_per_cluster) : 0;
    next_free_hint          = 2;

    fat_file_count = 0;
    fat32_scan_dir(root_cluster, "", 0);

    fat_ok = 1;
    dmesg("[fat32] mounted, "); dmesg_int(fat_file_count); dmesg(" files\n");
}

int fat32_present(void) { return fat_ok; }