#include "ahci.h"
#include "../kernel/dmesg.h"
#include "../mm/vmm.h"
#include "../fs/vfs.h"

extern uint64_t hhdm_offset;

#include "../mm/pmm.h"
#include <libk/string.h>

static hba_mem_t *abar;
static hba_port_t *active_port = NULL;
static spinlock_t ahci_lock = SPINLOCK_INIT;
static uint64_t disk_size = 0;

static uint32_t sda_read(vfs_node_t *n, uint32_t off, uint32_t len, uint8_t *buf) {
    (void)n;
    if (!active_port) return 0;
    uint64_t size = ahci_disk_size();
    if (size == 0) size = 512ULL * 1024 * 1024;
    if (off >= size) return 0;
    if (off + len > size) len = (uint32_t)(size - off);

    uint32_t sector = off / 512;
    uint32_t skip   = off % 512;
    uint32_t done   = 0;

    while (done < len) {
        void *phys = pmm_alloc_page();
        if (!phys) break;
        uint16_t *tmp = (uint16_t *)(hhdm_offset + (uint64_t)phys);
        memset(tmp, 0, 512);

        if (!ahci_read(0, sector, 1, tmp)) {
            pmm_free_page(phys);
            break;
        }

        uint32_t chunk = 512 - skip;
        if (chunk > len - done) chunk = len - done;
        for (uint32_t i = 0; i < chunk; i++) buf[done + i] = ((uint8_t *)tmp)[skip + i];

        pmm_free_page(phys);
        done += chunk;
        sector++;
        skip = 0;
    }
    return done;
}

static uint32_t sda_write(vfs_node_t *n, uint32_t off, uint32_t len, const uint8_t *buf) {
    (void)n;
    if (!active_port) return 0;

    uint32_t sector = off / 512;
    uint32_t skip   = off % 512;
    uint32_t done   = 0;

    while (done < len) {
        void *phys = pmm_alloc_page();
        if (!phys) break;
        uint8_t *tmp = (uint8_t *)(hhdm_offset + (uint64_t)phys);

        uint32_t chunk = 512 - skip;
        if (chunk > len - done) chunk = len - done;
        if (skip != 0 || chunk != 512) {
            if (!ahci_read(0, sector, 1, (uint16_t *)tmp)) { pmm_free_page(phys); break; }
        }
        for (uint32_t i = 0; i < chunk; i++) tmp[skip + i] = buf[done + i];

        if (!ahci_write(0, sector, 1, (uint16_t *)tmp)) { pmm_free_page(phys); break; }

        pmm_free_page(phys);
        done += chunk;
        sector++;
        skip = 0;
    }
    return done;
}

static vfs_node_t dev_sda = {
    .name   = "sda",
    .flags  = VFS_BLOCKDEV,
    .size   = 512ULL * 1024 * 1024,
    .read   = sda_read,
    .write  = sda_write,
};

int ahci_present(void) {
    return active_port != NULL;
}

static int check_type(hba_port_t *port) {
    uint32_t ssts = port->ssts;
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;

    if (det != HBA_PORT_DET_PRESENT || ipm != HBA_PORT_IPM_ACTIVE) return 0;

    switch (port->sig) {
        case SATA_SIG_ATAPI: return 2;
        case SATA_SIG_SEMB: return 3;
        case SATA_SIG_PM: return 4;
        default: return 1;
    }
}

static int port_rebase(hba_port_t *port, int portno) {
    (void)portno;

    port->cmd &= ~0x0001;
    port->cmd &= ~0x0010;
    while (port->cmd & (0x4000 | 0x8000));

    void *clb = pmm_alloc_page();
    void *fb  = pmm_alloc_page();
    if (!clb || !fb) {
        if (clb) pmm_free_page(clb);
        if (fb)  pmm_free_page(fb);
        dmesg("[ahci] port_rebase: out of memory\n");
        return 0;
    }

    memset((void *)(hhdm_offset + (uint64_t)clb), 0, PAGE_SIZE);
    memset((void *)(hhdm_offset + (uint64_t)fb),  0, PAGE_SIZE);

    port->clb  = (uint32_t)(uint64_t)clb;
    port->clbu = 0;
    port->fb   = (uint32_t)(uint64_t)fb;
    port->fbu  = 0;

    hba_cmd_header_t *cmdheader = (hba_cmd_header_t *)(hhdm_offset + (uint64_t)clb);
    for (int i = 0; i < 32; i++) {
        void *ctba = pmm_alloc_page();
        if (!ctba) {
            dmesg("[ahci] port_rebase: out of memory building command tables\n");
            return 0;
        }
        memset((void *)(hhdm_offset + (uint64_t)ctba), 0, PAGE_SIZE);
        cmdheader[i].prdtl = 8;
        cmdheader[i].ctba  = (uint32_t)(uint64_t)ctba;
        cmdheader[i].ctbau = 0;
    }

    port->serr = (uint32_t)-1;
    port->is   = (uint32_t)-1;

    while (port->cmd & 0x8000);
    port->cmd |= 0x0010;
    port->cmd |= 0x0001;
    return 1;
}

void ahci_init(pci_device_t *dev) {
    dmesg("[ahci] ABAR (BAR5) is at "); dmesg_hex(dev->bar5); dmesg("\n");

    uint32_t abar_phys = dev->bar5 & 0xFFFFFFF0;
    if (abar_phys == 0) return;

    uint64_t abar_virt = hhdm_offset + abar_phys;
    abar = (hba_mem_t *)abar_virt;

    if (abar->cap2 & 0x1) {
        abar->bohc |= (1 << 1);
        int spin = 0;
        while ((abar->bohc & (1 << 0)) && spin < 100000) spin++;
        dmesg("[ahci] BIOS/OS handoff requested\n");
    }

    abar->ghc |= (1u << 31);
    dmesg("[ahci] AHCI enable (GHC.AE) set\n");

    uint32_t pi = abar->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            int dt = check_type(&abar->ports[i]);
            if (dt == 1) {
                dmesg("[ahci] SATA drive found on port "); dmesg_int(i); dmesg("\n");
                if (!port_rebase(&abar->ports[i], i)) {
                    dmesg("[ahci] port_rebase failed, skipping port\n");
                    continue;
                }
                active_port = &abar->ports[i];

                void *id_phys = pmm_alloc_page();
                if (id_phys) {
                    uint16_t *id = (uint16_t *)(hhdm_offset + (uint64_t)id_phys);
                    memset(id, 0, 512);
                    if (ahci_identify(id)) {
                        uint64_t lba48 = ((uint64_t)id[103] << 48) | ((uint64_t)id[102] << 32) |
                                         ((uint64_t)id[101] << 16) | (uint64_t)id[100];
                        uint64_t lba28 = ((uint64_t)id[61] << 16) | (uint64_t)id[60];
                        if (lba48) disk_size = lba48 * 512;
                        else       disk_size = lba28 * 512;
                        dev_sda.size = (uint32_t)disk_size;
                        dmesg("[ahci] disk size: "); dmesg_int((uint32_t)(disk_size / 1048576));
                        dmesg(" MB\n");
                    }
                    pmm_free_page(id_phys);
                }

                vfs_register(&dev_sda);
                dmesg("[ahci] registered /dev/sda (block device)\n");
                break;
            }
        }
    }
}

static int ahci_cmd(uint8_t command, uint8_t count, uint16_t *buf) {
    if (!active_port) return 0;
    spin_acquire(&ahci_lock);
    hba_port_t *port = active_port;

    port->is = (uint32_t)-1;

    int spin = 0;
    int slot = 0;
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & (1 << i)) == 0) {
            slot = i;
            break;
        }
    }

    hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)(hhdm_offset + port->clb);
    cmdheader += slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t)/sizeof(uint32_t);
    cmdheader->w = 0;
    cmdheader->prdtl = 1;

    hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t*)(hhdm_offset + cmdheader->ctba);
    memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t));

    uint64_t buf_phys = (uint64_t)buf - hhdm_offset;
    cmdtbl->prdt_entry[0].dba = (uint32_t)buf_phys;
    cmdtbl->prdt_entry[0].dbau = (uint32_t)(buf_phys >> 32);
    cmdtbl->prdt_entry[0].dbc = 511;
    cmdtbl->prdt_entry[0].i = 1;

    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t*)(&cmdtbl->cfis);
    cmdfis->fis_type = 0x27;
    cmdfis->c = 1;
    cmdfis->command = command;
    cmdfis->device = 1 << 6;
    cmdfis->countl = count;
    cmdfis->counth = 0;

    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) spin++;
    if (spin == 1000000) { spin_release(&ahci_lock); return 0; }

    port->ci = 1 << slot;

    while (1) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) { spin_release(&ahci_lock); return 0; }
    }

    int ok = !(port->is & (1 << 30));
    spin_release(&ahci_lock);
    return ok;
}

int ahci_identify(uint16_t *buf) {
    return ahci_cmd(0xEC, 0, buf);
}

int ahci_read_sector(uint32_t lba, uint8_t *buf) {
    if (!active_port) return 0;
    void *phys = pmm_alloc_page();
    if (!phys) return 0;
    uint16_t *tmp = (uint16_t *)(hhdm_offset + (uint64_t)phys);
    memset(tmp, 0, 512);
    int ok = ahci_read(0, lba, 1, tmp);
    if (ok) {
        for (int i = 0; i < 512; i++) buf[i] = ((uint8_t *)tmp)[i];
    }
    pmm_free_page(phys);
    return ok;
}

uint64_t ahci_disk_size(void) {
    return disk_size;
}

int ahci_read(uint64_t starth, uint32_t startl, uint32_t count, uint16_t *buf) {
    if (!active_port) return 0;
    spin_acquire(&ahci_lock);
    hba_port_t *port = active_port;

    port->is = (uint32_t)-1;

    int spin = 0;
    int slot = 0;
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & (1 << i)) == 0) {
            slot = i;
            break;
        }
    }

    hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)(hhdm_offset + port->clb);
    cmdheader += slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t)/sizeof(uint32_t);
    cmdheader->w = 0;
    cmdheader->prdtl = (uint16_t)((count-1)>>4) + 1;

    hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t*)(hhdm_offset + cmdheader->ctba);
    memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t) + (cmdheader->prdtl-1)*sizeof(hba_prdt_entry_t));

    uint64_t buf_phys = (uint64_t)buf - hhdm_offset;

    int i;
    for (i = 0; i < cmdheader->prdtl - 1; i++) {
        cmdtbl->prdt_entry[i].dba = (uint32_t)buf_phys;
        cmdtbl->prdt_entry[i].dbau = (uint32_t)(buf_phys >> 32);
        cmdtbl->prdt_entry[i].dbc = 8192 - 1;
        cmdtbl->prdt_entry[i].i = 1;
        buf_phys += 8192;
        count -= 16;
    }
    cmdtbl->prdt_entry[i].dba = (uint32_t)buf_phys;
    cmdtbl->prdt_entry[i].dbau = (uint32_t)(buf_phys >> 32);
    cmdtbl->prdt_entry[i].dbc = (count << 9) - 1;
    cmdtbl->prdt_entry[i].i = 1;

    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t*)(&cmdtbl->cfis);
    cmdfis->fis_type = 0x27;
    cmdfis->c = 1;
    cmdfis->command = 0x24;

    cmdfis->lba0 = (uint8_t)startl;
    cmdfis->lba1 = (uint8_t)(startl >> 8);
    cmdfis->lba2 = (uint8_t)(startl >> 16);
    cmdfis->device = 1 << 6;
    cmdfis->lba3 = (uint8_t)(startl >> 24);
    cmdfis->lba4 = (uint8_t)starth;
    cmdfis->lba5 = (uint8_t)(starth >> 8);

    cmdfis->command = 0x24;
    cmdfis->featurel = 0;
    cmdfis->featureh = 0;
    cmdfis->countl = (uint8_t)count;
    cmdfis->counth = (uint8_t)(count >> 8);

    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) spin++;
    if (spin == 1000000) { spin_release(&ahci_lock); return 0; }

    port->ci = 1 << slot;

    while (1) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) { spin_release(&ahci_lock); return 0; }
    }

    int ok = !(port->is & (1 << 30));
    spin_release(&ahci_lock);
    return ok;
}

int ahci_write(uint64_t starth, uint32_t startl, uint32_t count, const uint16_t *buf) {
    if (!active_port) return 0;
    spin_acquire(&ahci_lock);
    hba_port_t *port = active_port;

    port->is = (uint32_t)-1;

    int spin = 0;
    int slot = 0;
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & (1 << i)) == 0) { slot = i; break; }
    }

    hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)(hhdm_offset + port->clb);
    cmdheader += slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t)/sizeof(uint32_t);
    cmdheader->w = 1;
    cmdheader->prdtl = 1;

    hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t*)(hhdm_offset + cmdheader->ctba);
    memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t));

    uint64_t buf_phys = (uint64_t)buf - hhdm_offset;
    cmdtbl->prdt_entry[0].dba  = (uint32_t)buf_phys;
    cmdtbl->prdt_entry[0].dbau = (uint32_t)(buf_phys >> 32);
    cmdtbl->prdt_entry[0].dbc  = (count << 9) - 1;
    cmdtbl->prdt_entry[0].i    = 1;

    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t*)(&cmdtbl->cfis);
    cmdfis->fis_type = 0x27;
    cmdfis->c = 1;
    cmdfis->command = 0x35;

    cmdfis->lba0 = (uint8_t)startl;
    cmdfis->lba1 = (uint8_t)(startl >> 8);
    cmdfis->lba2 = (uint8_t)(startl >> 16);
    cmdfis->device = 1 << 6;
    cmdfis->lba3 = (uint8_t)(startl >> 24);
    cmdfis->lba4 = (uint8_t)starth;
    cmdfis->lba5 = (uint8_t)(starth >> 8);

    cmdfis->featurel = 0;
    cmdfis->featureh = 0;
    cmdfis->countl = (uint8_t)count;
    cmdfis->counth = (uint8_t)(count >> 8);

    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) spin++;
    if (spin == 1000000) { spin_release(&ahci_lock); return 0; }

    port->ci = 1 << slot;

    while (1) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) { spin_release(&ahci_lock); return 0; }
    }

    int ok = !(port->is & (1 << 30));
    spin_release(&ahci_lock);
    return ok;
}

int ahci_write_sector(uint32_t lba, const uint8_t *buf) {
    if (!active_port) return 0;
    void *phys = pmm_alloc_page();
    if (!phys) return 0;
    uint16_t *tmp = (uint16_t *)(hhdm_offset + (uint64_t)phys);
    memcpy(tmp, buf, 512);
    int ok = ahci_write(0, lba, 1, tmp);
    pmm_free_page(phys);
    return ok;
}

extern void terminal_print(const char *msg);
extern void terminal_print_hex(uint64_t val);

void cmd_ahcitest(void) {
    if (!active_port) {
        terminal_print("  [ahcitest] no active port found.\n");
        dmesg("[ahcitest] no active port found.\n");
        return;
    }

    void *phys = pmm_alloc_page();
    if (!phys) return;

    uint16_t *buf = (uint16_t*)(hhdm_offset + (uint64_t)phys);
    memset(buf, 0, 512);

    terminal_print("  [ahcitest] reading sector 0...\n");
    dmesg("[ahcitest] reading sector 0...\n");
    int ok = ahci_read(0, 0, 1, buf);

    if (ok) {
        terminal_print("  [ahcitest] OK. Data: \n  ");
        dmesg("[ahcitest] OK. Data: \n  ");
        for(int i=0; i<8; i++) {
            terminal_print_hex(buf[i]); terminal_print(" ");
            dmesg_hex(buf[i]); dmesg(" ");
        }
        terminal_print("\n");
        dmesg("\n");
    } else {
        terminal_print("  [ahcitest] READ FAILED.\n");
        dmesg("[ahcitest] READ FAILED.\n");
    }
}