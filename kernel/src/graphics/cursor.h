#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "api/gpipe.h"

#define CURSOR_W 12
#define CURSOR_H 19

void cursor_init(gpipe_ctx_t *ctx);
void cursor_set_visible(bool visible);

void cursor_update(gpipe_ctx_t *ctx);
void cursor_get_pos(int *x, int *y);