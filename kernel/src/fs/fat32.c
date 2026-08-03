#include "fat32.h"
#include "vfs.h"
#include "../drivers/ahci.h"
#include "../kernel/dmesg.h"
#include "../kernel/lock.h"
#include "../mm/heap.h"
#include <libk/string.h>

#define FAT32_MAX_FILES      40
#define FAT32_MAX_DEPTH      3
#define FAT32_LFN_MAX_ENTRIES 10
#define FAT32_MAX_LFN_NAME   (FAT32_LFN_MAX_ENTRIES * 13)

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
    uint8_t  seq;
    uint16_t name1[5];
    uint8_t  attr;
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];
    uint16_t fst_clus_lo;
    uint16_t name3[2];
} __attribute__((packed)) fat_lfn_entry_t;

typedef struct {
    uint32_t start_cluster;
    uint32_t dirent_sector;
    uint32_t dirent_offset;
} fat32_file_ctx_t;

typedef struct {
    int        used;
    vfs_node_t node;
} fat32_slot_t;

typedef struct {
    uint32_t lead_sig;
    uint8_t  reserved1[480];
    uint32_t struc_sig;
    uint32_t free_count;
    uint32_t next_free;
    uint8_t  reserved2[12];
    uint32_t trail_sig;
} __attribute__((packed)) fat32_fsinfo_t;

#define FSINFO_LEAD_SIG  0x41615252u
#define FSINFO_STRUC_SIG 0x61417272u
#define FSINFO_TRAIL_SIG 0xAA550000u

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

static uint32_t fsinfo_lba           = 0;
static int      fsinfo_valid         = 0;
static uint32_t fsinfo_free_count    = 0xFFFFFFFF;

static fat32_slot_t fat_files[FAT32_MAX_FILES];
static int          fat_file_active = 0;

static vfs_node_t *slot_alloc(void) {
    for (int i = 0; i < FAT32_MAX_FILES; i++) {
        if (!fat_files[i].used) {
            fat_files[i].used = 1;
            memset(&fat_files[i].node, 0, sizeof(vfs_node_t));
            fat_file_active++;
            return &fat_files[i].node;
        }
    }
    return NULL;
}

static void slot_free(vfs_node_t *n) {
    for (int i = 0; i < FAT32_MAX_FILES; i++) {
        if (fat_files[i].used && &fat_files[i].node == n) {
            fat_files[i].used = 0;
            fat_file_active--;
            return;
        }
    }
}

static int slot_full(void) {
    return fat_file_active >= FAT32_MAX_FILES;
}

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

static void fsinfo_save(void) {
    if (!fsinfo_valid) return;
    uint8_t sec[512];
    if (!ahci_read_sector(fsinfo_lba, sec)) return;

    fat32_fsinfo_t *fi = (fat32_fsinfo_t *)sec;
    if (fi->lead_sig != FSINFO_LEAD_SIG || fi->struc_sig != FSINFO_STRUC_SIG ||
        fi->trail_sig != FSINFO_TRAIL_SIG) {
        fsinfo_valid = 0;
        return;
    }
    fi->free_count = fsinfo_free_count;
    fi->next_free  = next_free_hint;
    ahci_write_sector(fsinfo_lba, sec);
}

static void fsinfo_load(uint32_t fs_info_sector) {
    fsinfo_valid = 0;
    fsinfo_free_count = 0xFFFFFFFF;
    if (fs_info_sector == 0 || fs_info_sector == 0xFFFF) return;

    uint8_t sec[512];
    if (!ahci_read_sector(fs_info_sector, sec)) return;

    fat32_fsinfo_t *fi = (fat32_fsinfo_t *)sec;
    if (fi->lead_sig != FSINFO_LEAD_SIG || fi->struc_sig != FSINFO_STRUC_SIG ||
        fi->trail_sig != FSINFO_TRAIL_SIG) {
        dmesg("[fat32] FSInfo signature invalid, falling back to full-scan allocator\n");
        return;
    }

    fsinfo_lba   = fs_info_sector;
    fsinfo_valid = 1;
    fsinfo_free_count = fi->free_count;

    if (fi->next_free != 0xFFFFFFFF && fi->next_free >= 2) next_free_hint = fi->next_free;

    dmesg("[fat32] FSInfo loaded (hint honored)\n");
}

uint32_t fat32_free_clusters(void) {
    return fsinfo_valid ? fsinfo_free_count : 0xFFFFFFFF;
}

static uint32_t fat32_alloc_cluster(void) {
    uint32_t limit = total_clusters + 2;
    uint32_t found = 0;

    for (uint32_t c = next_free_hint; c < limit; c++) {
        if (fat_get_entry(c) == 0) { found = c; break; }
    }
    if (!found) {
        for (uint32_t c = 2; c < next_free_hint && c < limit; c++) {
            if (fat_get_entry(c) == 0) { found = c; break; }
        }
    }
    if (!found) {
        dmesg("[fat32] disk full, no free clusters\n");
        return 0;
    }

    fat_set_entry(found, 0x0FFFFFFF);
    next_free_hint = found + 1;
    if (fsinfo_valid && fsinfo_free_count != 0xFFFFFFFF && fsinfo_free_count > 0)
        fsinfo_free_count--;
    fsinfo_save();
    return found;
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

static void base_upper(const char *name, char *out, int maxlen) {
    int i = 0, o = 0;
    while (name[i] && name[i] != '.' && o < maxlen) {
        if (name[i] != ' ') out[o++] = to_upper_c(name[i]);
        i++;
    }
    out[o] = 0;
}

static int needs_lfn(const char *name) {
    int i = 0, dot = -1;
    for (i = 0; name[i]; i++) if (name[i] == '.') dot = i;
    int len = i;
    if (len == 0 || len > 12) return 1;
    int baselen = (dot < 0) ? len : dot;
    int extlen  = (dot < 0) ? 0   : len - dot - 1;
    if (baselen > 8 || baselen == 0 || extlen > 3) return 1;
    for (i = 0; name[i]; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') return 1;
        if (c == ' ') return 1;
        if (i != dot && c == '.') return 1;
    }
    return 0;
}

static int write_dir_entry(uint32_t lba, uint32_t offset, const char *name8, const char *ext3,
                            uint8_t attr, uint32_t cluster, uint32_t size);

static int find_free_dir_slot(uint32_t dir_cluster, uint32_t *out_lba, uint32_t *out_offset) {
    uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
    uint8_t *cbuf = (uint8_t *)kmalloc(cluster_size);
    if (!cbuf) return 0;

    uint32_t cluster = dir_cluster;
    uint32_t last_cluster = dir_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        if (!read_sectors(lba, sectors_per_cluster, cbuf)) { kfree(cbuf); return 0; }

        for (uint32_t i = 0; i < cluster_size; i += 32) {
            fat_dir_entry_t *e = (fat_dir_entry_t *)(cbuf + i);
            if (e->name[0] == 0x00 || (uint8_t)e->name[0] == 0xE5) {
                *out_lba = lba + (i / bytes_per_sector);
                *out_offset = i % bytes_per_sector;
                kfree(cbuf);
                return 1;
            }
        }
        last_cluster = cluster;
        cluster = fat_get_entry(cluster);
    }

    uint32_t newc = fat32_alloc_cluster();
    if (!newc) { kfree(cbuf); return 0; }
    fat_set_entry(last_cluster, newc);
    fat_set_entry(newc, 0x0FFFFFFF);

    memset(cbuf, 0, cluster_size);
    if (!write_sectors(cluster_to_lba(newc), sectors_per_cluster, cbuf)) { kfree(cbuf); return 0; }

    *out_lba = cluster_to_lba(newc);
    *out_offset = 0;
    kfree(cbuf);
    return 1;
}

static int find_free_dir_slots(uint32_t dir_cluster, int needed,
                                uint32_t *out_lba, uint32_t *out_offset) {
    if (needed > FAT32_LFN_MAX_ENTRIES + 1) return 0;
    uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
    uint8_t *cbuf = (uint8_t *)kmalloc(cluster_size);
    if (!cbuf) return 0;

    uint32_t run_lba[FAT32_LFN_MAX_ENTRIES + 1];
    uint32_t run_off[FAT32_LFN_MAX_ENTRIES + 1];
    int run = 0;

    uint32_t cluster = dir_cluster;
    uint32_t last_cluster = dir_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        if (!read_sectors(lba, sectors_per_cluster, cbuf)) { kfree(cbuf); return 0; }

        for (uint32_t i = 0; i < cluster_size; i += 32) {
            fat_dir_entry_t *e = (fat_dir_entry_t *)(cbuf + i);
            int is_free = (e->name[0] == 0x00 || (uint8_t)e->name[0] == 0xE5);
            if (is_free) {
                run_lba[run] = lba + (i / bytes_per_sector);
                run_off[run] = i % bytes_per_sector;
                run++;
                if (run == needed) {
                    memcpy(out_lba, run_lba, needed * sizeof(uint32_t));
                    memcpy(out_offset, run_off, needed * sizeof(uint32_t));
                    kfree(cbuf);
                    return 1;
                }
            } else {
                run = 0;
            }
        }
        last_cluster = cluster;
        cluster = fat_get_entry(cluster);
    }

    while (run < needed) {
        uint32_t newc = fat32_alloc_cluster();
        if (!newc) { kfree(cbuf); return 0; }
        fat_set_entry(last_cluster, newc);
        fat_set_entry(newc, 0x0FFFFFFF);
        memset(cbuf, 0, cluster_size);
        if (!write_sectors(cluster_to_lba(newc), sectors_per_cluster, cbuf)) { kfree(cbuf); return 0; }
        last_cluster = newc;

        uint32_t lba = cluster_to_lba(newc);
        for (uint32_t i = 0; i < cluster_size && run < needed; i += 32) {
            run_lba[run] = lba + (i / bytes_per_sector);
            run_off[run] = i % bytes_per_sector;
            run++;
        }
    }

    memcpy(out_lba, run_lba, needed * sizeof(uint32_t));
    memcpy(out_offset, run_off, needed * sizeof(uint32_t));
    kfree(cbuf);
    return 1;
}

static int short_name_exists(uint32_t dir_cluster, const char *name8, const char *ext3) {
    uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
    uint8_t *cbuf = (uint8_t *)kmalloc(cluster_size);
    if (!cbuf) return 0;

    int found = 0;
    uint32_t cluster = dir_cluster;
    while (!found && cluster >= 2 && cluster < 0x0FFFFFF8) {
        if (!read_sectors(cluster_to_lba(cluster), sectors_per_cluster, cbuf)) break;
        for (uint32_t i = 0; i < cluster_size; i += 32) {
            fat_dir_entry_t *e = (fat_dir_entry_t *)(cbuf + i);
            if (e->name[0] == 0x00) break;
            if ((uint8_t)e->name[0] == 0xE5) continue;
            if ((e->attr & 0x0F) == 0x0F) continue;
            if (!memcmp(e->name, name8, 8) && !memcmp(e->ext, ext3, 3)) { found = 1; break; }
        }
        cluster = fat_get_entry(cluster);
    }
    kfree(cbuf);
    return found;
}

static void int_to_dec(int v, char *out) {
    char tmp[8]; int t = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v > 0 && t < 8) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    int o = 0;
    while (t > 0) out[o++] = tmp[--t];
    out[o] = 0;
}

static void gen_short_name(uint32_t dir_cluster, const char *leaf, char *name8, char *ext3) {
    to_short_name(leaf, name8, ext3);
    if (!short_name_exists(dir_cluster, name8, ext3)) return;

    char origbase[64];
    base_upper(leaf, origbase, 63);
    int baselen = 0;
    while (origbase[baselen]) baselen++;

    for (int n = 1; n <= 999; n++) {
        char tag[8];
        int_to_dec(n, tag);
        int taglen = 0;
        while (tag[taglen]) taglen++;

        int keep = 8 - 1 - taglen;
        if (keep < 1) keep = 1;
        if (keep > baselen) keep = baselen;

        char cand[9];
        memset(cand, ' ', 8);
        for (int i = 0; i < keep; i++) cand[i] = origbase[i];
        cand[keep] = '~';
        for (int i = 0; i < taglen; i++) cand[keep + 1 + i] = tag[i];

        if (!short_name_exists(dir_cluster, cand, ext3)) {
            memcpy(name8, cand, 8);
            return;
        }
    }
}

static uint8_t lfn_checksum(const char *name8, const char *ext3) {
    uint8_t sum = 0;
    for (int i = 0; i < 8; i++) sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + (uint8_t)name8[i]);
    for (int i = 0; i < 3; i++) sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + (uint8_t)ext3[i]);
    return sum;
}

static int write_raw_entry(uint32_t lba, uint32_t offset, const uint8_t *entry32) {
    uint8_t sec[512];
    if (!ahci_read_sector(lba, sec)) return 0;
    memcpy(sec + offset, entry32, 32);
    return ahci_write_sector(lba, sec);
}

static void lfn_fill_chunk(uint8_t *entry, const char *leaf, int namelen, int chunk_index) {
    static const int off[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    int base = chunk_index * 13;
    int ended = 0;
    for (int k = 0; k < 13; k++) {
        int pos = base + k;
        uint16_t ch;
        if (!ended) {
            if (pos < namelen) { ch = (uint8_t)leaf[pos]; }
            else { ch = 0x0000; ended = 1; }
        } else {
            ch = 0xFFFF;
        }
        entry[off[k]]     = (uint8_t)(ch & 0xFF);
        entry[off[k] + 1] = (uint8_t)(ch >> 8);
    }
}

static int write_lfn_and_short(uint32_t dir_cluster, const char *leaf,
                                const char *name8, const char *ext3,
                                uint8_t attr, uint32_t cluster, uint32_t size,
                                uint32_t *out_lba, uint32_t *out_offset) {
    int namelen = 0;
    while (leaf[namelen] && namelen < FAT32_MAX_LFN_NAME) namelen++;

    int nentries = (namelen + 12) / 13;
    if (nentries < 1) nentries = 1;
    if (nentries > FAT32_LFN_MAX_ENTRIES) return 0;

    uint32_t lbas[FAT32_LFN_MAX_ENTRIES + 1];
    uint32_t offs[FAT32_LFN_MAX_ENTRIES + 1];
    if (!find_free_dir_slots(dir_cluster, nentries + 1, lbas, offs)) return 0;

    uint8_t sum = lfn_checksum(name8, ext3);

    for (int idx = 0; idx < nentries; idx++) {
        int seq = nentries - idx;
        int chunk_index = seq - 1;

        uint8_t entry[32];
        memset(entry, 0xFF, 32);
        entry[0]  = (uint8_t)(seq | (idx == 0 ? 0x40 : 0));
        entry[11] = 0x0F;
        entry[12] = 0x00;
        entry[13] = sum;
        entry[26] = 0x00;
        entry[27] = 0x00;
        lfn_fill_chunk(entry, leaf, namelen, chunk_index);

        if (!write_raw_entry(lbas[idx], offs[idx], entry)) return 0;
    }

    *out_lba    = lbas[nentries];
    *out_offset = offs[nentries];
    return write_dir_entry(*out_lba, *out_offset, name8, ext3, attr, cluster, size);
}

static const char *strip_dev_prefix(const char *path) {
    const char *n = path;
    if (n[0] == '/' && n[1] == 'd' && n[2] == 'e' && n[3] == 'v' && n[4] == '/') n += 5;
    if (n[0] == 's' && n[1] == 'd' && n[2] == 'a' && n[3] == '/') n += 4;
    return n;
}

static int resolve_parent(const char *path, uint32_t *out_dir_cluster,
                           char *out_parent_name, const char **out_leaf) {
    int last_slash = -1;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/') last_slash = i;

    if (last_slash < 0) {
        *out_dir_cluster = root_cluster;
        out_parent_name[0] = 0;
        *out_leaf = path;
        return 1;
    }

    int plen = last_slash;
    if (plen >= VFS_NAME_MAX) plen = VFS_NAME_MAX - 1;
    memcpy(out_parent_name, path, plen);
    out_parent_name[plen] = 0;
    *out_leaf = path + last_slash + 1;

    vfs_node_t *pnode = vfs_find(out_parent_name);
    if (!pnode || !(pnode->flags & VFS_DIRECTORY) || !pnode->device) {
        dmesg("[fat32] parent directory not found: "); dmesg(out_parent_name); dmesg("\n");
        return 0;
    }
    *out_dir_cluster = ((fat32_file_ctx_t *)pnode->device)->start_cluster;
    return 1;
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

static void join_full_name(char *full, const char *parent_name, const char *leaf) {
    int o = 0;
    if (parent_name[0]) {
        for (int i = 0; parent_name[i] && o < VFS_NAME_MAX - 1; i++) full[o++] = parent_name[i];
        if (o < VFS_NAME_MAX - 1) full[o++] = '/';
    }
    for (int i = 0; leaf[i] && o < VFS_NAME_MAX - 1; i++) full[o++] = leaf[i];
    full[o] = 0;
}

int fat32_create_file(const char *path, char *out_fullname) {
    if (!fat_ok) return FAT32_ERR_NO_FS;

    fs_lock_acquire();
    if (slot_full()) { fs_lock_release(); return FAT32_ERR_DISK; }

    path = strip_dev_prefix(path);

    uint32_t dir_cluster;
    char parent_name[VFS_NAME_MAX];
    const char *leaf;
    if (!resolve_parent(path, &dir_cluster, parent_name, &leaf)) {
        fs_lock_release();
        return FAT32_ERR_NO_PARENT;
    }

    char name8[8], ext3[3];
    int lfn = needs_lfn(leaf);
    if (lfn) gen_short_name(dir_cluster, leaf, name8, ext3);
    else     to_short_name(leaf, name8, ext3);

    char probe[VFS_NAME_MAX];
    join_full_name(probe, parent_name, leaf);
    if (vfs_find(probe)) { fs_lock_release(); return FAT32_ERR_EXISTS; }

    uint32_t lba, offset;
    int ok = lfn
        ? write_lfn_and_short(dir_cluster, leaf, name8, ext3, 0x20, 0, 0, &lba, &offset)
        : (find_free_dir_slot(dir_cluster, &lba, &offset) &&
           write_dir_entry(lba, offset, name8, ext3, 0x20, 0, 0));
    if (!ok) { fs_lock_release(); return FAT32_ERR_DISK; }

    fat32_file_ctx_t *ctx = (fat32_file_ctx_t *)kmalloc(sizeof(fat32_file_ctx_t));
    if (!ctx) { fs_lock_release(); return FAT32_ERR_DISK; }
    ctx->start_cluster = 0;
    ctx->dirent_sector = lba;
    ctx->dirent_offset = offset;

    vfs_node_t *n = slot_alloc();
    if (!n) { kfree(ctx); fs_lock_release(); return FAT32_ERR_DISK; }
    join_full_name(n->name, parent_name, leaf);
    n->flags  = VFS_FILE;
    n->size   = 0;
    n->device = ctx;
    n->read   = fat32_file_read;
    n->write  = fat32_file_write;
    vfs_register(n);

    if (out_fullname) strncpy(out_fullname, n->name, VFS_NAME_MAX);

    fs_lock_release();
    return FAT32_OK;
}

int fat32_mkdir(const char *path, char *out_fullname) {
    if (!fat_ok) return FAT32_ERR_NO_FS;

    fs_lock_acquire();
    if (slot_full()) { fs_lock_release(); return FAT32_ERR_DISK; }

    path = strip_dev_prefix(path);

    uint32_t dir_cluster;
    char parent_name[VFS_NAME_MAX];
    const char *leaf;
    if (!resolve_parent(path, &dir_cluster, parent_name, &leaf)) {
        fs_lock_release();
        return FAT32_ERR_NO_PARENT;
    }

    char probe[VFS_NAME_MAX];
    join_full_name(probe, parent_name, leaf);
    if (vfs_find(probe)) { fs_lock_release(); return FAT32_ERR_EXISTS; }

    uint32_t newc = fat32_alloc_cluster();
    if (!newc) { fs_lock_release(); return FAT32_ERR_DISK; }
    fat_set_entry(newc, 0x0FFFFFFF);

    char name8[8], ext3[3];
    int lfn = needs_lfn(leaf);
    if (lfn) gen_short_name(dir_cluster, leaf, name8, ext3);
    else     to_short_name(leaf, name8, ext3);

    uint32_t lba, offset;
    int ok = lfn
        ? write_lfn_and_short(dir_cluster, leaf, name8, ext3, 0x10, newc, 0, &lba, &offset)
        : (find_free_dir_slot(dir_cluster, &lba, &offset) &&
           write_dir_entry(lba, offset, name8, ext3, 0x10, newc, 0));
    if (!ok) { fs_lock_release(); return FAT32_ERR_DISK; }

    uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
    uint8_t *cbuf = (uint8_t *)kmalloc(cluster_size);
    if (!cbuf) { fs_lock_release(); return FAT32_ERR_DISK; }
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
    dotdot->fst_clus_hi = (uint16_t)(dir_cluster >> 16);
    dotdot->fst_clus_lo = (uint16_t)(dir_cluster & 0xFFFF);

    write_sectors(cluster_to_lba(newc), sectors_per_cluster, cbuf);
    kfree(cbuf);

    fat32_file_ctx_t *ctx = (fat32_file_ctx_t *)kmalloc(sizeof(fat32_file_ctx_t));
    if (!ctx) { fs_lock_release(); return FAT32_ERR_DISK; }
    ctx->start_cluster = newc;
    ctx->dirent_sector = lba;
    ctx->dirent_offset = offset;

    vfs_node_t *dn = slot_alloc();
    if (!dn) { kfree(ctx); fs_lock_release(); return FAT32_ERR_DISK; }
    join_full_name(dn->name, parent_name, leaf);
    dn->flags  = VFS_DIRECTORY;
    dn->size   = 0;
    dn->device = ctx;
    vfs_register(dn);

    if (out_fullname) strncpy(out_fullname, dn->name, VFS_NAME_MAX);

    fs_lock_release();
    return FAT32_OK;
}

static void fat32_scan_dir(uint32_t dir_cluster, const char *prefix, int depth) {
    if (depth > FAT32_MAX_DEPTH) return;
    if (slot_full()) return;

    uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
    uint8_t *cbuf = (uint8_t *)kmalloc(cluster_size);
    if (!cbuf) return;

    char    lfn_name[VFS_NAME_MAX];
    int     lfn_have = 0;
    int     lfn_next_seq = 0;
    uint8_t lfn_sum = 0;
    static const int lfn_off[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

    uint32_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        if (!read_sectors(lba, sectors_per_cluster, cbuf)) break;

        for (uint32_t i = 0; i < cluster_size; i += 32) {
            fat_dir_entry_t *e = (fat_dir_entry_t *)(cbuf + i);

            if (e->name[0] == 0x00) { kfree(cbuf); return; }
            if ((uint8_t)e->name[0] == 0xE5) { lfn_have = 0; continue; }

            if ((e->attr & 0x0F) == 0x0F) {
                uint8_t *raw = (uint8_t *)e;
                uint8_t seqb = raw[0];
                uint8_t sum  = raw[13];
                int first = (seqb & 0x40) != 0;
                int seq   = seqb & 0x1F;

                if (first) {
                    lfn_have = 1;
                    lfn_sum  = sum;
                    lfn_next_seq = seq;
                    memset(lfn_name, 0, sizeof(lfn_name));
                }
                if (!lfn_have || seq != lfn_next_seq || sum != lfn_sum ||
                    seq < 1 || seq > FAT32_LFN_MAX_ENTRIES) {
                    lfn_have = 0;
                    continue;
                }

                int base = (seq - 1) * 13;
                for (int k = 0; k < 13; k++) {
                    int pos = base + k;
                    if (pos >= VFS_NAME_MAX - 1) break;
                    uint16_t ch = (uint16_t)raw[lfn_off[k]] | ((uint16_t)raw[lfn_off[k] + 1] << 8);
                    if (ch == 0x0000) break;
                    lfn_name[pos] = (char)(ch & 0xFF);
                }
                lfn_next_seq = seq - 1;
                continue;
            }

            if (e->attr & 0x08) { lfn_have = 0; continue; }
            if (e->name[0] == '.') { lfn_have = 0; continue; }

            char sname[13];
            format_name(e->name, e->ext, sname);
            if (!sname[0]) { lfn_have = 0; continue; }

            const char *use_name = sname;
            if (lfn_have && lfn_next_seq == 0 && lfn_checksum(e->name, e->ext) == lfn_sum) {
                use_name = lfn_name;
            }
            lfn_have = 0;

            char fullname[VFS_NAME_MAX];
            build_full_name(fullname, prefix, use_name);

            uint32_t start_cluster = ((uint32_t)e->fst_clus_hi << 16) | e->fst_clus_lo;

            if (e->attr & 0x10) {
                if (!slot_full()) {
                    fat32_file_ctx_t *dctx = (fat32_file_ctx_t *)kmalloc(sizeof(fat32_file_ctx_t));
                    if (dctx) {
                        dctx->start_cluster = start_cluster;
                        dctx->dirent_sector = lba + (i / bytes_per_sector);
                        dctx->dirent_offset = i % bytes_per_sector;

                        vfs_node_t *dn = slot_alloc();
                        if (dn) {
                            strncpy(dn->name, fullname, VFS_NAME_MAX - 1);
                            dn->name[VFS_NAME_MAX - 1] = 0;
                            dn->flags  = VFS_DIRECTORY;
                            dn->size   = 0;
                            dn->device = dctx;
                            vfs_register(dn);
                        } else {
                            kfree(dctx);
                        }
                    }
                }

                if (start_cluster >= 2 && !slot_full()) {
                    char subprefix[VFS_NAME_MAX];
                    build_full_name(subprefix, fullname, "/");
                    fat32_scan_dir(start_cluster, subprefix, depth + 1);
                }
                continue;
            }

            if (slot_full()) continue;

            fat32_file_ctx_t *ctx = (fat32_file_ctx_t *)kmalloc(sizeof(fat32_file_ctx_t));
            if (!ctx) continue;
            ctx->start_cluster = start_cluster;
            ctx->dirent_sector = lba + (i / bytes_per_sector);
            ctx->dirent_offset = i % bytes_per_sector;

            vfs_node_t *n = slot_alloc();
            if (!n) { kfree(ctx); continue; }
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

static void free_chain(uint32_t start_cluster) {
    uint32_t cluster = start_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t next = fat_get_entry(cluster);
        fat_set_entry(cluster, 0);
        if (fsinfo_valid && fsinfo_free_count != 0xFFFFFFFF) fsinfo_free_count++;
        cluster = next;
    }
    fsinfo_save();
}

static uint32_t lba_to_cluster(uint32_t lba) {
    if (lba < data_start || sectors_per_cluster == 0) return 0;
    return (lba - data_start) / sectors_per_cluster + 2;
}

static int mark_removed_with_lfn(uint32_t start_cluster, uint32_t target_lba, uint32_t target_offset) {
    uint8_t tsec[512];
    if (!ahci_read_sector(target_lba, tsec)) return 0;
    fat_dir_entry_t *te = (fat_dir_entry_t *)(tsec + target_offset);
    uint8_t sum = lfn_checksum(te->name, te->ext);

    uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
    uint8_t *cbuf = (uint8_t *)kmalloc(cluster_size);
    if (!cbuf) return 0;

    uint32_t run_lba[FAT32_LFN_MAX_ENTRIES];
    uint32_t run_off[FAT32_LFN_MAX_ENTRIES];
    int run = 0;
    int expect_seq = 0;

    uint32_t cluster = start_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        if (!read_sectors(lba, sectors_per_cluster, cbuf)) { kfree(cbuf); return 0; }

        for (uint32_t i = 0; i < cluster_size; i += 32) {
            fat_dir_entry_t *e = (fat_dir_entry_t *)(cbuf + i);
            uint32_t elba = lba + (i / bytes_per_sector);
            uint32_t eoff = i % bytes_per_sector;

            if (e->name[0] == 0x00) { kfree(cbuf); return 1; }

            if ((uint8_t)e->name[0] != 0xE5 && (e->attr & 0x0F) == 0x0F) {
                uint8_t *raw = (uint8_t *)e;
                uint8_t seqb = raw[0];
                uint8_t esum = raw[13];
                int first = (seqb & 0x40) != 0;
                int seq   = seqb & 0x1F;
                if (first) { run = 0; expect_seq = seq; }
                if (run < FAT32_LFN_MAX_ENTRIES && seq == expect_seq && esum == sum) {
                    run_lba[run] = elba; run_off[run] = eoff; run++;
                    expect_seq = seq - 1;
                } else {
                    run = 0;
                }
                continue;
            }

            if (elba == target_lba && eoff == target_offset) {
                uint8_t sec[512];
                if (ahci_read_sector(elba, sec)) {
                    sec[eoff] = 0xE5;
                    ahci_write_sector(elba, sec);
                }
                if (run > 0 && expect_seq == 0) {
                    for (int r = 0; r < run; r++) {
                        uint8_t rsec[512];
                        if (ahci_read_sector(run_lba[r], rsec)) {
                            rsec[run_off[r]] = 0xE5;
                            ahci_write_sector(run_lba[r], rsec);
                        }
                    }
                }
                kfree(cbuf);
                return 1;
            }

            run = 0;
        }

        cluster = fat_get_entry(cluster);
    }

    kfree(cbuf);
    return 0;
}

static int dir_is_empty(uint32_t dir_cluster) {
    uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
    uint8_t *cbuf = (uint8_t *)kmalloc(cluster_size);
    if (!cbuf) return 0;

    int empty = 1;
    uint32_t cluster = dir_cluster;
    while (empty && cluster >= 2 && cluster < 0x0FFFFFF8) {
        if (!read_sectors(cluster_to_lba(cluster), sectors_per_cluster, cbuf)) break;
        for (uint32_t i = 0; i < cluster_size; i += 32) {
            fat_dir_entry_t *e = (fat_dir_entry_t *)(cbuf + i);
            if (e->name[0] == 0x00) break;
            if ((uint8_t)e->name[0] == 0xE5) continue;
            if ((e->attr & 0x0F) == 0x0F) continue;
            if (e->attr & 0x08) continue;
            if (e->name[0] == '.') continue;
            empty = 0;
            break;
        }
        cluster = fat_get_entry(cluster);
    }
    kfree(cbuf);
    return empty;
}

static void remove_single_node(vfs_node_t *node) {
    fat32_file_ctx_t *ctx = (fat32_file_ctx_t *)node->device;
    if (ctx) {
        if (ctx->start_cluster >= 2) free_chain(ctx->start_cluster);
        mark_removed_with_lfn(lba_to_cluster(ctx->dirent_sector), ctx->dirent_sector, ctx->dirent_offset);
        kfree(ctx);
    }
    vfs_unregister(node);
    slot_free(node);
}

static int path_depth_slashes(const char *name) {
    int d = 0;
    for (int i = 0; name[i]; i++) if (name[i] == '/') d++;
    return d;
}

static int is_path_or_child(const char *name, const char *path, int pathlen) {
    if (strncmp(name, path, pathlen)) return 0;
    return name[pathlen] == '/';
}

int fat32_remove(const char *path, int recursive, char *out_name) {
    if (!fat_ok) return FAT32_ERR_NO_FS;

    fs_lock_acquire();

    path = strip_dev_prefix(path);
    vfs_node_t *node = vfs_find(path);
    if (!node || !(node->flags & (VFS_FILE | VFS_DIRECTORY))) {
        fs_lock_release();
        return FAT32_ERR_NOT_FOUND;
    }

    if (out_name) strncpy(out_name, node->name, VFS_NAME_MAX);

    if (node->flags & VFS_DIRECTORY) {
        fat32_file_ctx_t *dctx = (fat32_file_ctx_t *)node->device;

        if (!recursive) {
            if (!dctx || !dir_is_empty(dctx->start_cluster)) {
                fs_lock_release();
                return FAT32_ERR_NOT_EMPTY;
            }
            remove_single_node(node);
            fs_lock_release();
            return FAT32_OK;
        }

        int pathlen = 0;
        while (path[pathlen]) pathlen++;

        for (;;) {
            int deepest_idx = -1;
            int deepest_depth = -1;
            for (int i = 0; i < FAT32_MAX_FILES; i++) {
                if (!fat_files[i].used) continue;
                vfs_node_t *cand = &fat_files[i].node;
                if (cand == node) continue;
                if (!is_path_or_child(cand->name, path, pathlen)) continue;
                int depth = path_depth_slashes(cand->name);
                if (depth > deepest_depth) { deepest_depth = depth; deepest_idx = i; }
            }
            if (deepest_idx < 0) break;
            remove_single_node(&fat_files[deepest_idx].node);
        }

        remove_single_node(node);
        fs_lock_release();
        return FAT32_OK;
    }

    remove_single_node(node);
    fs_lock_release();
    return FAT32_OK;
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

    fsinfo_load(bpb->fs_info);

    for (int i = 0; i < FAT32_MAX_FILES; i++) fat_files[i].used = 0;
    fat_file_active = 0;
    fat32_scan_dir(root_cluster, "", 0);

    fat_ok = 1;
    dmesg("[fat32] mounted, "); dmesg_int(fat_file_active); dmesg(" files\n");
}

int fat32_present(void) { return fat_ok; }