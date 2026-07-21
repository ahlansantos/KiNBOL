<<<<<<< HEAD
/*
 * Linux-dmesg-style kernel log buffer. Stores kernel messages in a buffer
 * and mirrors them to the serial port in real time, so the log can still
 * be read even if the screen never comes up or freezes.
 */
=======
>>>>>>> origin/x86_64-uefi
#include "dmesg.h"
#include "../drivers/serial.h"
#include <stdint.h>

static char     buf[DMESG_BUF_SIZE];
static uint32_t head    = 0;
static uint32_t count   = 0;
static uint32_t seq     = 0;

void dmesg_init(void) {
    for (int i = 0; i < DMESG_BUF_SIZE; i++) buf[i] = 0;
    head = 0; count = 0; seq = 0;
}

static void dmesg_putc(char c) {
    buf[head] = c;
    head = (head + 1) % DMESG_BUF_SIZE;
    if (count < DMESG_BUF_SIZE) count++;
    serial_putc(c); /* live mirror - readable in qemu.log even after a halt/reset */
}

void dmesg(const char *msg) {
    if (!msg) return;
    for (int i = 0; msg[i]; i++) dmesg_putc(msg[i]);
    seq++;
}

void dmesg_int(uint32_t n) {
    char tmp[11]; int i = 10; tmp[i--] = 0;
    if (n == 0) { dmesg_putc('0'); return; }
    while (n) { tmp[i--] = '0' + (n % 10); n /= 10; }
    dmesg(&tmp[i + 1]);
}

void dmesg_hex(uint64_t n) {
    const char *h = "0123456789ABCDEF";
    dmesg_putc('0'); dmesg_putc('x');
    for (int i = 15; i >= 0; i--) dmesg_putc(h[(n >> (i * 4)) & 0xF]);
}

void dmesg_foreach(void (*cb)(char c)) {
    if (count == 0) return;
    uint32_t start = (count < DMESG_BUF_SIZE) ? 0 : head;
    uint32_t len   = (count < DMESG_BUF_SIZE) ? count : DMESG_BUF_SIZE;
    for (uint32_t i = 0; i < len; i++)
        cb(buf[(start + i) % DMESG_BUF_SIZE]);
}

uint32_t dmesg_len(void)  { return count; }
void     dmesg_clear(void){ head = 0; count = 0; }