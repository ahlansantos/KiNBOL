/*
 * Public interface for the keyboard driver: line reading, key peek, held-key
 * check, and the scancode constants used by the shell for arrow key
 * navigation and escape.
 */
#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdint.h>

void    keyboard_set_cursor_cb(void (*cb)(int visible));
void    keyboard_set_completion_cb(int (*cb)(char *buf, int max));
void    keyboard_init(void);
void    keyboard_readline(char *buf, int max);
uint8_t keyboard_peek(void);
int keyboard_held(uint8_t scancode);
void keyboard_update(void);

#define KEY_ESC   0x01
#define KEY_UP    0x48
#define KEY_DOWN  0x50
#define KEY_LEFT  0x4B
#define KEY_RIGHT 0x4D
#define KEY_TAB   0x0F

#endif