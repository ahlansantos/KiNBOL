#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdint.h>

void    keyboard_set_cursor_cb(void (*cb)(int visible));
void    keyboard_readline(char *buf, int max);
uint8_t keyboard_peek(void);

#define KEY_ESC   0x01
#define KEY_UP    0x48
#define KEY_DOWN  0x50
#define KEY_LEFT  0x4B
#define KEY_RIGHT 0x4D

#endif