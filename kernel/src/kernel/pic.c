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

    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    if (imcr_switch_to_apic) {

        outb(0x22, 0x70);
        outb(0x23, 0x01);
        dmesg("[pic] disabled, IMCR switched to APIC mode\n");
    } else {
        dmesg("[pic] disabled\n");
    }
}