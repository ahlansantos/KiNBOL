#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <libk/string.h>

#include "elf.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../fs/vfs.h"
#include "../kernel/dmesg.h"
#include "../kernel/rand.h"

extern uint64_t hhdm_offset;

#define ELF_LOAD_BASE_PIE 0x555555554000ULL
#define ELF_MAX_PHDRS 32

#define ELF_ASLR_RANGE (256ULL * 1024 * 1024)

static bool elf_validate_header(elf64_ehdr_t *eh) {
    if (eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3) {
        dmesg("[elf] bad magic\n");
        return false;
    }
    if (eh->e_ident[4] != ELFCLASS64) {
        dmesg("[elf] not 64-bit\n");
        return false;
    }
    if (eh->e_ident[5] != ELFDATA2LSB) {
        dmesg("[elf] not little-endian\n");
        return false;
    }
    if (eh->e_machine != EM_X86_64) {
        dmesg("[elf] wrong machine type\n");
        return false;
    }
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) {
        dmesg("[elf] unsupported e_type\n");
        return false;
    }
    if (eh->e_phnum == 0 || eh->e_phnum > ELF_MAX_PHDRS) {
        dmesg("[elf] bad phnum\n");
        return false;
    }
    if (eh->e_phentsize != sizeof(elf64_phdr_t)) {
        dmesg("[elf] bad phentsize\n");
        return false;
    }
    return true;
}

static uint64_t elf_segment_flags(uint32_t p_flags) {
    uint64_t flags = VMM_PRESENT | VMM_USER;
    if (p_flags & PF_W) flags |= VMM_WRITE;
    if (!(p_flags & PF_X)) flags |= VMM_NX;
    return flags;
}

static int elf_map_segment(pagemap_t pm, vfs_node_t *node, elf64_phdr_t *ph, uint64_t load_bias) {
    if (ph->p_memsz == 0) return 0;
    if (ph->p_filesz > ph->p_memsz) {
        dmesg("[elf] filesz > memsz, rejecting\n");
        return -1;
    }

    uint64_t vaddr_start = load_bias + ph->p_vaddr;
    uint64_t vaddr_end   = vaddr_start + ph->p_memsz;
    uint64_t page_start  = vaddr_start & ~(PAGE_SIZE - 1);
    uint64_t page_end    = (vaddr_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    uint64_t flags = elf_segment_flags(ph->p_flags);

    uint64_t file_off  = ph->p_offset;
    uint64_t file_left  = ph->p_filesz;
    uint64_t seg_off    = vaddr_start - page_start;

    for (uint64_t va = page_start; va < page_end; va += PAGE_SIZE) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            dmesg("[elf] out of memory mapping segment\n");
            return -1;
        }
        uint8_t *dst = (uint8_t *)(hhdm_offset + (uint64_t)phys);
        memset(dst, 0, PAGE_SIZE);

        uint64_t copy_start_in_page = (va == page_start) ? seg_off : 0;
        uint64_t space_in_page = PAGE_SIZE - copy_start_in_page;
        uint64_t to_copy = file_left < space_in_page ? file_left : space_in_page;

        if (to_copy > 0) {
            uint32_t got = vfs_read(node, (uint32_t)file_off,
                                     (uint32_t)to_copy, dst + copy_start_in_page);
            if (got != to_copy) {
                dmesg("[elf] short read on segment data\n");
                pmm_free_page(phys);
                return -1;
            }
            file_off  += to_copy;
            file_left -= to_copy;
        }

        if (vmm_map(pm, va, (uint64_t)phys, flags) != 0) {
            dmesg("[elf] vmm_map failed for segment page\n");
            pmm_free_page(phys);
            return -1;
        }
    }

    return 0;
}

int elf_load(const char *path, pagemap_t pm, elf_load_result_t *out) {
    vfs_node_t *node = vfs_find(path);
    if (!node) {
        dmesg("[elf] file not found: "); dmesg(path); dmesg("\n");
        return -1;
    }

    elf64_ehdr_t eh;
    if (vfs_read(node, 0, sizeof(eh), (uint8_t *)&eh) != sizeof(eh)) {
        dmesg("[elf] short read on ehdr\n");
        return -1;
    }
    if (!elf_validate_header(&eh)) return -1;

    elf64_phdr_t phdrs[ELF_MAX_PHDRS];
    uint32_t ph_bytes = (uint32_t)eh.e_phnum * sizeof(elf64_phdr_t);
    if (vfs_read(node, (uint32_t)eh.e_phoff, ph_bytes, (uint8_t *)phdrs) != ph_bytes) {
        dmesg("[elf] short read on phdrs\n");
        return -1;
    }

    uint64_t load_bias = 0;
    if (eh.e_type == ET_DYN) {
        load_bias = ELF_LOAD_BASE_PIE + krand_page_aligned_below(ELF_ASLR_RANGE);
    }

    uint64_t highest_vaddr = 0;
    uint64_t phdr_vaddr = 0;

    for (uint16_t i = 0; i < eh.e_phnum; i++) {
        elf64_phdr_t *ph = &phdrs[i];

        if (ph->p_type == PT_LOAD) {
            if (elf_map_segment(pm, node, ph, load_bias) != 0) return -1;
            uint64_t end = load_bias + ph->p_vaddr + ph->p_memsz;
            if (end > highest_vaddr) highest_vaddr = end;
        } else if (ph->p_type == PT_PHDR) {
            phdr_vaddr = load_bias + ph->p_vaddr;
        } else if (ph->p_type == PT_INTERP) {
            dmesg("[elf] PT_INTERP present, dynamic linking not supported yet\n");
            return -1;
        }
    }

    if (phdr_vaddr == 0) {
        phdr_vaddr = load_bias + eh.e_phoff;
    }

    out->entry         = load_bias + eh.e_entry;
    out->phdr_vaddr     = phdr_vaddr;
    out->phent          = eh.e_phentsize;
    out->phnum          = eh.e_phnum;
    out->load_bias       = load_bias;
    out->highest_vaddr  = (highest_vaddr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    out->is_pie         = (eh.e_type == ET_DYN);

    dmesg("[elf] loaded "); dmesg(path);
    dmesg(" entry="); dmesg_hex(out->entry);
    dmesg(" highest_vaddr="); dmesg_hex(out->highest_vaddr);
    dmesg("\n");

    return 0;
}