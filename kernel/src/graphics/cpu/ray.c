#include "ray.h"
#include "draw.h"
#include "../../drivers/keyboard.h"
#include "../../kernel/pit.h"
#include "../../graphics/terminal.h"
#include <stdint.h>
#include <stddef.h>

typedef int32_t  fixed_t;
#define FP_SHIFT   16
#define FP_ONE     (1 << FP_SHIFT)
#define INT2FP(x)  ((fixed_t)(x) << FP_SHIFT)
#define FP2INT(x)  ((x) >> FP_SHIFT)
#define FP_MUL(a,b) ((fixed_t)(((int64_t)(a) * (b)) >> FP_SHIFT))
#define FP_DIV(a,b) ((fixed_t)(((int64_t)(a) << FP_SHIFT) / (b)))

static const fixed_t sin_table[91] = {
         0,   1143,   2287,   3430,   4571,   5712,   6850,   7987,
      9120,  10251,  11380,  12504,  13625,  14742,  15855,  16962,
     18064,  19161,  20251,  21336,  22415,  23486,  24550,  25607,
     26656,  27696,  28729,  29752,  30767,  31772,  32768,  33754,
     34729,  35693,  36647,  37590,  38521,  39441,  40348,  41243,
     42126,  42995,  43852,  44695,  45525,  46341,  47143,  47930,
     48703,  49461,  50203,  50931,  51643,  52339,  53020,  53684,
     54332,  54963,  55578,  56175,  56756,  57319,  57865,  58393,
     58903,  59395,  59870,  60326,  60763,  61183,  61584,  61966,
     62328,  62672,  62997,  63303,  63589,  63856,  64104,  64332,
     64540,  64729,  64898,  65047,  65176,  65286,  65376,  65446,
     65496,  65527,  65536
};

static fixed_t fp_sin(int deg) {
    deg = ((deg % 360) + 360) % 360;
    if (deg <= 90)  return  sin_table[deg];
    if (deg <= 180) return  sin_table[180 - deg];
    if (deg <= 270) return -sin_table[deg - 180];
    return                 -sin_table[360 - deg];
}
static fixed_t fp_cos(int deg) { return fp_sin(deg + 90); }

static fixed_t fp_abs(fixed_t x) { return x < 0 ? -x : x; }

#define MAP_W 16
#define MAP_H 16

static const uint8_t map[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,1,1,0,0,0,0,0,1,0,0,0,0,1},
    {1,0,0,1,0,0,0,0,0,0,1,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,1,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

static int map_solid(int x, int y) {
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return 1;
    return map[y][x];
}

static fixed_t cam_x;
static fixed_t cam_y;
static int     cam_angle;

#define MOVE_SPEED  INT2FP(1) / 10
#define TURN_SPEED  3
#define FOV         60

static uint32_t wall_color(fixed_t dist) {
    int d = FP2INT(dist);
    if (d <  2) return draw_rgb(220, 220, 220);
    if (d <  4) return draw_rgb(180, 180, 180);
    if (d <  6) return draw_rgb(140, 140, 140);
    if (d <  9) return draw_rgb(100, 100, 100);
    return              draw_rgb( 60,  60,  60);
}

static fixed_t cast_ray(int ray_angle) {
    ray_angle = ((ray_angle % 360) + 360) % 360;

    fixed_t rx = fp_cos(ray_angle);
    fixed_t ry = fp_sin(ray_angle);

    fixed_t px = cam_x;
    fixed_t py = cam_y;

    int mx = FP2INT(px);
    int my = FP2INT(py);

    int step_x = rx > 0 ? 1 : -1;
    int step_y = ry > 0 ? 1 : -1;

    fixed_t delta_x, delta_y;
    fixed_t t_max_x, t_max_y;

    if (fp_abs(rx) < 10) {
        delta_x = INT2FP(1000);
        t_max_x = INT2FP(1000);
    } else {
        delta_x = fp_abs(FP_DIV(FP_ONE, rx));
        if (rx > 0)
            t_max_x = FP_MUL(INT2FP(mx + 1) - px, FP_DIV(FP_ONE, rx));
        else
            t_max_x = FP_MUL(px - INT2FP(mx), FP_DIV(FP_ONE, -rx));
    }

    if (fp_abs(ry) < 10) {
        delta_y = INT2FP(1000);
        t_max_y = INT2FP(1000);
    } else {
        delta_y = fp_abs(FP_DIV(FP_ONE, ry));
        if (ry > 0)
            t_max_y = FP_MUL(INT2FP(my + 1) - py, FP_DIV(FP_ONE, ry));
        else
            t_max_y = FP_MUL(py - INT2FP(my), FP_DIV(FP_ONE, -ry));
    }

    fixed_t dist = 0;
    for (int step = 0; step < 64; step++) {
        if (t_max_x < t_max_y) {
            dist   = t_max_x;
            t_max_x += delta_x;
            mx     += step_x;
        } else {
            dist   = t_max_y;
            t_max_y += delta_y;
            my     += step_y;
        }
        if (map_solid(mx, my)) break;
    }

    int diff = ray_angle - cam_angle;
    diff = ((diff + 180 + 360) % 360) - 180;
    fixed_t cos_diff = fp_cos(diff < 0 ? -diff : diff);
    if (cos_diff > 100)
        dist = FP_MUL(dist, cos_diff);

    return dist > 0 ? dist : FP_ONE / 4;
}

static void ray_render(void) {
    int sw = draw_width();
    int sh = draw_height();

    
    draw_rect_fill(0,      0,    sw, sh / 2, draw_rgb(40,  40,  80));
    draw_rect_fill(0, sh / 2,    sw, sh / 2, draw_rgb(60,  40,  20));

    for (int col = 0; col < sw; col++) {
        int ray_angle = cam_angle + (col * FOV / sw) - FOV / 2;

        fixed_t dist = cast_ray(ray_angle);
        if (dist < FP_ONE / 4) dist = FP_ONE / 4;

        int wall_h = FP2INT(FP_DIV(INT2FP(sh), dist));
        if (wall_h > sh) wall_h = sh;

        int y0 = (sh - wall_h) / 2;
        int y1 = y0 + wall_h;

        uint32_t color = wall_color(dist);
        draw_line(col, y0, col, y1, color);
    }

    int ms = 4;
    for (int my = 0; my < MAP_H; my++) {
        for (int mx2 = 0; mx2 < MAP_W; mx2++) {
            uint32_t c = map[my][mx2] ? draw_rgb(200,200,200) : draw_rgb(30,30,30);
            draw_rect_fill(mx2 * ms, my * ms, ms - 1, ms - 1, c);
        }
    }
    int px = FP2INT(cam_x) * ms + ms / 2;
    int py = FP2INT(cam_y) * ms + ms / 2;
    draw_rect_fill(px - 1, py - 1, 3, 3, draw_rgb(255, 100, 0));
}

void ray_run(void) {
    cam_x     = INT2FP(8);
    cam_y     = INT2FP(8);
    cam_angle = 0;

    while (1) {
        keyboard_update();

        if (keyboard_held(0x01)) {
            terminal_clear();
            break;
        }

        if (keyboard_held(0x48)) {
            fixed_t dx = FP_MUL(fp_cos(cam_angle), MOVE_SPEED);
            fixed_t dy = FP_MUL(fp_sin(cam_angle), MOVE_SPEED);
            fixed_t nx = cam_x + dx;
            fixed_t ny = cam_y + dy;
            if (!map_solid(FP2INT(nx), FP2INT(cam_y))) cam_x = nx;
            if (!map_solid(FP2INT(cam_x), FP2INT(ny))) cam_y = ny;
        }
        if (keyboard_held(0x50)) {
            fixed_t dx = FP_MUL(fp_cos(cam_angle), MOVE_SPEED);
            fixed_t dy = FP_MUL(fp_sin(cam_angle), MOVE_SPEED);
            fixed_t nx = cam_x - dx;
            fixed_t ny = cam_y - dy;
            if (!map_solid(FP2INT(nx), FP2INT(cam_y))) cam_x = nx;
            if (!map_solid(FP2INT(cam_x), FP2INT(ny))) cam_y = ny;
        }
        if (keyboard_held(0x4B)) cam_angle = (cam_angle - TURN_SPEED + 360) % 360;
        if (keyboard_held(0x4D)) cam_angle = (cam_angle + TURN_SPEED) % 360;

        ray_render();
        draw_flip();
        sleep_ms(16);
    }
}