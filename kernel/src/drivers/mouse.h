/*
 * Header for the mouse driver. Defines mouse_state_t (position plus button
 * flags) and the init/isr/draw functions. Also marked WONT WORK.
 */
// WONT WORK
#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int x, y;
    bool left, right, middle;
} mouse_state_t;

extern mouse_state_t mouse;

void mouse_init(void);
void mouse_isr(void);      /* registrar no IRQ12 (vetor 44) */
void mouse_draw(void);     /* chamar após cada IRQ para atualizar cursor */

#endif