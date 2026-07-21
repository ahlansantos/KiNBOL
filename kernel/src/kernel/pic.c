/*
 * Driver for the legacy 8259 PIC (the old interrupt controller). All it
 * does now is remap the vectors and then permanently disable the chip
 * through the IMCR, since KiNBOL moved over to APIC. Kept around mostly
 * as history and as a fallback reference.
 */
#include "pic.h"
#include "dmesg.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0,%1" :: "a"(val), "Nd"(port));
}
static inline void io_wait(void) { outb(0x80, 0); }

void pic_disable(bool imcr_switch_to_apic) {
    /* Remap first (even though we're about to mask everything): if this
     * board's firmware/BIOS left the PIC unmapped or mapped over the
     * exception range, a leftover in-flight or spurious IRQ could still
     * land on a vector 0-31 for one instruction's worth of race window
     * and get misread as a CPU exception. Costs nothing to be safe. */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    /* Mask every legacy IRQ permanently - the IOAPIC owns them now. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    if (imcr_switch_to_apic) {
        /* IMCR: bit0=0 routes NMI/INTR through the 8259 to the CPU,
         * bit0=1 routes through the APIC instead. Only present/needed on
         * boards with a real dual-8259 (MADT PCAT_COMPAT flag). Writing
         * this on a board that doesn't implement it is a no-op; NOT
         * writing it on a board that does and defaults to PIC mode is
         * exactly what silently ate every IRQ0 tick before. */
        outb(0x22, 0x70);
        outb(0x23, 0x01);
        dmesg("[pic] disabled, IMCR switched to APIC mode\n");
    } else {
        dmesg("[pic] disabled\n");
    }
}