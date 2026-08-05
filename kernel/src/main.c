#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/rtc.h"
#include "drivers/pci.h"
#include "graphics/terminal.h"
#include "graphics/api/gpipe.h"
#include "graphics/api/gpipe_prim.h"
#include "graphics/font.h"
#include "kernel/gdt.h"
#include "kernel/idt.h"
#include "kernel/usermode.h"
#include "kernel/pic.h"
#include "kernel/acpi.h"
#include "kernel/lapic.h"
#include "kernel/ioapic.h"
#include "kernel/dmesg.h"
#include "kernel/pit.h"
#include "kernel/lock.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "fs/vfs.h"
#include "fs/ramdisk.h"
#include "fs/fat32.h"
#include "fs/procfs.h"
#include "kernel/sched.h"
#include "kernel/rand.h"
#include "shell/shell.h"

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

uint64_t hhdm_offset = 0;
struct limine_framebuffer     *fbi      = 0;
struct limine_memmap_response *g_memmap = 0;

static uint64_t total_ram = 0;

static void hcf(void) { for (;;) asm("hlt"); }

static void print_banner(void) {
    terminal_clear();

    terminal_set_fg(0x88AACC);
    terminal_println(" ___  __    ___  ________   ________  ________  ___          ");
    terminal_println("|\\  \\|\\  \\ |\\  \\|\\   ___  \\|\\   __  \\|\\   __  \\|\\  \\         ");
    terminal_println("\\ \\  \\/  /|\\ \\  \\ \\  \\\\ \\  \\ \\  \\|\\ /\\ \\  \\|\\  \\ \\  \\        ");
    terminal_println(" \\ \\   ___  \\ \\  \\ \\  \\\\ \\  \\ \\   __  \\ \\  \\\\\\  \\ \\  \\       ");
    terminal_println("  \\ \\  \\\\ \\  \\ \\  \\ \\  \\\\ \\  \\ \\  \\|\\  \\ \\  \\\\\\  \\ \\  \\____  ");
    terminal_println("   \\ \\__\\\\ \\__\\ \\__\\ \\__\\\\ \\__\\ \\_______\\ \\_______\\ \\_______\\");
    terminal_println("    \\|__| \\|__|\\|__|\\|__| \\|__|\\|_______|\\|_______|\\|_______\\");
    terminal_println("");

    terminal_set_fg(0x88CC88);
    terminal_println("  KiNBOL 0.08");
    terminal_set_fg(0xAAAAAA);
    terminal_println("  this Kernel is Not Based On Linux");
    terminal_println("");

    terminal_set_fg(0xDDDDDD); terminal_print("  Framebuffer: ");
    terminal_set_fg(0x88CC88);
    terminal_print_int(fbi->width); terminal_print("x"); terminal_print_int(fbi->height); terminal_println("");

    terminal_set_fg(0xDDDDDD); terminal_print("  RAM:         ");
    terminal_set_fg(0x88CC88);
    if (total_ram >= 1073741824) {
        uint64_t whole = total_ram / 1073741824;
        uint64_t tenths = (total_ram % 1073741824) * 10 / 1073741824;
        terminal_print_int((uint32_t)whole); terminal_print(".");
        terminal_print_int((uint32_t)tenths); terminal_println(" GB");
    } else {
        terminal_print_int((uint32_t)(total_ram / 1048576)); terminal_println(" MB");
    }

    terminal_set_fg(0xDDDDDD); terminal_print("  PMM:         ");
    terminal_set_fg(0x88CC88);
    terminal_print_int((uint32_t)pmm_get_free_page_count()); terminal_println(" pages free");

    terminal_set_fg(0xDDDDDD); terminal_print("  VFS:         ");
    terminal_set_fg(0x88CC88);
    terminal_print_int(vfs_node_count()); terminal_println(" nodes");

    terminal_set_fg(0xDDDDDD); terminal_print("  GPipe:       ");
    terminal_set_fg(0x88CC88);
    terminal_println(GPIPE_VERSION);

    terminal_println("");
    terminal_set_fg(0xAAAAAA); terminal_println("  Type 'help' for available commands."); terminal_println("");
}

#define TIMER_VECTOR    32
#define KEYBOARD_VECTOR 33

static void timer_isr(void) {
    pit_tick();
    lapic_eoi();

    if (bkl == 0) {
        task_t *task = sched_current();
        if (task && task->name[0] != '[') {
            sched_schedule();
        }
    }
}

void kmain(void) {

    dmesg_init();

    gdt_init();

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) hcf();
    if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1) hcf();
    if (!memmap_request.response || !hhdm_request.response) hcf();

    fbi      = framebuffer_request.response->framebuffers[0];
    g_memmap = memmap_request.response;

    serial_init();
    dmesg("[pre-boot] serial init\n");
    terminal_init(fbi);
    dmesg("[pre-boot] terminal init\n");
    dmesg("[boot] FreeARS Base boot init, KiNBOL 0.08 starting\n");

    hhdm_offset = hhdm_request.response->offset;
    for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry *e = memmap_request.response->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE) total_ram += e->length;
    }

    serial_print("HHDM: "); serial_hex(hhdm_offset); serial_print("\n");

    dmesg("[pmm] initializing\n");
    pmm_init(memmap_request.response, hhdm_offset);
    dmesg("[pmm] OK\n");

    vmm_init();
    dmesg("[vmm] OK\n");

    cpu_security_init();

    vmm_enable_writecombine_pat();
    uint64_t fb_size = fbi->pitch * fbi->height;
    if (vmm_mark_range_writecombine((uint64_t)fbi->address, fb_size)) {
        dmesg("[vmm] framebuffer marked write-combining\n");
    } else {
        dmesg("[vmm] WARNING: could not mark framebuffer write-combining (huge page or unmapped)\n");
    }

    if (!gpipe_init(fbi))
        dmesg("[pre-boot] gpipe init FAILED (heap/pmm not ready or fbi null)\n");
    else
        dmesg("[pre-boot] gpipe init\n");

    tsc_calibrate();
    idt_init();
    krand_init();
    syscall_init();

    acpi_init(rsdp_request.response ? rsdp_request.response->address : NULL);

    if (acpi_info.valid) {

        pic_disable(acpi_info.legacy_pic_present);

        lapic_init();
        ioapic_init();

        irq_register(TIMER_VECTOR, timer_isr);

        lapic_timer_init(100, TIMER_VECTOR);

        mouse_init();

        dmesg("[boot] LAPIC/IOAPIC timer online\n");
    } else {
        pic_disable(false);
        dmesg("[boot] no ACPI/MADT - running without a periodic timer\n");
    }

    ramdisk_init();
    vfs_init();
    procfs_init();

    pci_init();

    if (fat32_detect()) {
        dmesg("[boot] FAT32 BPB detected\n");
        fat32_init();
    } else {
        dmesg("[boot] no FAT32 BPB found, /dev/sda left unmounted\n");
    }

    sched_init();

    keyboard_init();

    asm volatile("sti");
    uint64_t rflags;
    asm volatile("pushfq; popq %0" : "=r"(rflags));
    serial_print("RFLAGS=");
    serial_hex(rflags);
    serial_print("\n");
    keyboard_set_cursor_cb(terminal_cursor_draw);

    terminal_clear();
    print_banner();

    shell_run();
    hcf();
}