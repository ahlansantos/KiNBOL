/*
 * Header for the serial driver: init, putc, string print, and hex print.
 */
#pragma once
#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
void serial_print(const char *s);
void serial_hex(uint64_t n);