#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

typedef struct {
    int dx;
    int dy;
    int left;
    int right;
    int middle;
} mouse_state_t;

void mouse_init(void);
mouse_state_t mouse_get_state(void);

#endif