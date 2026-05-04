/* DOOM-like / DOOM port - github.com/ahlansantos/KiNBOL-doomport */
#include "doomport.h"
#include "../api/baregl.h"
#include "../../drivers/keyboard.h"
#include "../../kernel/pit.h"
#include "../../graphics/terminal.h"
#include <stdint.h>
#include <stddef.h>

typedef int32_t fixed_t;
#define FP_SHIFT   16
#define FP_ONE     (1 << FP_SHIFT)
#define INT2FP(x)  ((fixed_t)(x) << FP_SHIFT)
#define FP2INT(x)  ((x) >> FP_SHIFT)
#define FP_MUL(a,b) ((fixed_t)(((int64_t)(a) * (b)) >> FP_SHIFT))
#define FP_DIV(a,b) ((fixed_t)(((int64_t)(a) << FP_SHIFT) / (b)))

static const fixed_t sin_table[91] = {
         0,1143,2287,3430,4571,5712,6850,7987,
      9120,10251,11380,12504,13625,14742,15855,16962,
     18064,19161,20251,21336,22415,23486,24550,25607,
     26656,27696,28729,29752,30767,31772,32768,33754,
     34729,35693,36647,37590,38521,39441,40348,41243,
     42126,42995,43852,44695,45525,46341,47143,47930,
     48703,49461,50203,50931,51643,52339,53020,53684,
     54332,54963,55578,56175,56756,57319,57865,58393,
     58903,59395,59870,60326,60763,61183,61584,61966,
     62328,62672,62997,63303,63589,63856,64104,64332,
     64540,64729,64898,65047,65176,65286,65376,65446,
     65496,65527,65536
};

static fixed_t fp_sin(int deg) {
    deg = ((deg % 360) + 360) % 360;
    if (deg <= 90)  return sin_table[deg];
    if (deg <= 180) return sin_table[180 - deg];
    if (deg <= 270) return -sin_table[deg - 180];
    return -sin_table[360 - deg];
}
static fixed_t fp_cos(int deg) { return fp_sin(deg + 90); }
static fixed_t fp_abs(fixed_t x) { return x < 0 ? -x : x; }

#define MAP_W 32
#define MAP_H 32

static const uint8_t map[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,1,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,1},
    {1,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,1},
    {1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

#define MAP_TEXTURE_NONE    0
#define MAP_TEXTURE_STONE   1
#define MAP_TEXTURE_WOOD    2
#define MAP_TEXTURE_METAL   3
#define MAP_TEXTURE_BRICK   4
#define MAP_TEXTURE_MARBLE  5
#define MAP_TEXTURE_MOSS    6

static const uint8_t map_textures[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,4,4,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,0,0,0,0,0,1},
    {1,0,0,4,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,4,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,6,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,5,0,0,0,0,0,0,0,0,0,0,0,6,0,0,1},
    {1,0,0,0,0,0,5,5,0,0,0,0,0,0,0,0,5,0,0,0,0,0,4,4,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,2,0,0,0,0,0,0,0,0,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,6,0,0,1},
    {1,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,5,5,0,0,0,0,0,0,0,0,6,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,5,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,5,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,3,3,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,3,0,0,0,0,0,1},
    {1,0,0,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,3,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

static int map_solid(int x, int y) {
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return 1;
    return map[y][x];
}

static uint8_t map_get_texture(int x, int y) {
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return MAP_TEXTURE_STONE;
    return map_textures[y][x];
}

static fixed_t cam_x;
static fixed_t cam_y;
static int cam_angle;

static fixed_t bob_phase_x = 0;
static fixed_t bob_phase_y = 0;
static fixed_t bob_amount = 0;
static int is_moving = 0;

#define MOVE_SPEED  INT2FP(1) / 8
#define TURN_SPEED  2
#define FOV         60

static uint32_t get_texture_color(uint8_t texture_type, int tex_x, int tex_y, fixed_t dist) {
    int d = FP2INT(dist);
    int shade = 255 - d * 8;
    if (shade < 25) shade = 25;
    if (shade > 255) shade = 255;
    
    int pattern = (tex_x * 7 + tex_y * 13) & 15;
    int noise = ((tex_x * 1103515245 + tex_y * 12345) >> 16) & 15;
    int checker = ((tex_x >> 4) ^ (tex_y >> 4)) & 1;
    
    switch (texture_type) {
        case MAP_TEXTURE_STONE: {
            int base = 128 + noise * 4;
            int r = (base * shade) >> 8;
            int g = (base * shade) >> 8;
            int b = (base * shade) >> 8;
            
            if (pattern < 3) {
                r = (r * 3) >> 2;
                g = (g * 3) >> 2;
                b = (b * 3) >> 2;
            } else if (pattern > 12) {
                r = r + ((255 - r) >> 2);
                g = g + ((255 - g) >> 2);
                b = b + ((255 - b) >> 2);
            }
            return bare_rgb(r, g, b);
        }
        
        case MAP_TEXTURE_WOOD: {
            int grain = (tex_y * 3 + tex_x) & 31;
            int base_r = 139;
            int base_g = 90;
            int base_b = 43;
            
            if (grain < 4 || (grain > 12 && grain < 16)) {
                base_r -= 20;
                base_g -= 15;
                base_b -= 10;
            } else {
                base_r += 15;
                base_g += 10;
                base_b += 5;
            }
            
            int r = (base_r * shade) >> 8;
            int g = (base_g * shade) >> 8;
            int b = (base_b * shade) >> 8;
            return bare_rgb(r, g, b);
        }
        
        case MAP_TEXTURE_METAL: {
            int base = 160 + noise * 3;
            int highlight = (pattern == 7 || pattern == 8) ? 40 : 0;
            int r = ((base + highlight) * shade) >> 8;
            int g = ((base + highlight) * shade) >> 8;
            int b = ((base + highlight + 20) * shade) >> 8;
            
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            return bare_rgb(r, g, b);
        }
        
        case MAP_TEXTURE_BRICK: {
            int brick_h = tex_y & 31;
            int brick_w = tex_x & 63;
            int mortar = (brick_h < 2 || brick_w < 2) ? 1 : 0;
            
            if (mortar) {
                int gray = (100 * shade) >> 8;
                return bare_rgb(gray, gray, gray);
            } else {
                int r = ((180 + noise * 2) * shade) >> 8;
                int g = ((70 + noise) * shade) >> 8;
                int b = ((50 + noise) * shade) >> 8;
                return bare_rgb(r, g, b);
            }
        }
        
        case MAP_TEXTURE_MARBLE: {
            int vein = ((tex_x * 3 + tex_y * 7) & 63);
            int base = 220;
            
            if (vein < 3 || (vein > 25 && vein < 28) || vein > 55) {
                base = 200;
            }
            
            int r = ((base + noise) * shade) >> 8;
            int g = ((base + noise) * shade) >> 8;
            int b = ((base + noise + 10) * shade) >> 8;
            return bare_rgb(r, g, b);
        }
        
        case MAP_TEXTURE_MOSS: {
            int base_g = 80 + noise * 3;
            int base_r = 40 + noise * 2;
            
            if (checker) {
                base_g += 20;
                base_r += 10;
            }
            
            int r = (base_r * shade) >> 8;
            int g = (base_g * shade) >> 8;
            int b = ((30 + noise) * shade) >> 8;
            return bare_rgb(r, g, b);
        }
        
        default:
            return bare_rgb(shade, shade, shade);
    }
}

static fixed_t cast_ray(int ray_angle, uint8_t *texture_type, fixed_t *wall_x, int *hit_side_out) {
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
    int hit_side = 0;

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
            dist = t_max_x;
            t_max_x += delta_x;
            mx += step_x;
            hit_side = 0;
        } else {
            dist = t_max_y;
            t_max_y += delta_y;
            my += step_y;
            hit_side = 1;
        }
        if (map_solid(mx, my)) {
            *texture_type = map_get_texture(mx, my);
            
            if (hit_side == 0) {
                fixed_t wall_hit_y = py + FP_MUL(dist, ry);
                *wall_x = wall_hit_y & (FP_ONE - 1);
            } else {
                fixed_t wall_hit_x = px + FP_MUL(dist, rx);
                *wall_x = wall_hit_x & (FP_ONE - 1);
            }
            *hit_side_out = hit_side;
            break;
        }
    }

    int diff = ray_angle - cam_angle;
    diff = ((diff + 180 + 360) % 360) - 180;
    fixed_t cos_diff = fp_cos(diff < 0 ? -diff : diff);
    if (cos_diff > 100)
        dist = FP_MUL(dist, cos_diff);

    return dist > 0 ? dist : FP_ONE / 4;
}

static void ray_render(void) {
    int sw = bare_width();
    int sh = bare_height();

    bare_clear(bare_rgb(0,0,0));

    fixed_t bob_x = FP_MUL(fp_sin(bob_phase_x), bob_amount);
    fixed_t bob_y = FP_MUL(fp_sin(bob_phase_y), bob_amount);
    int bob_xi = FP2INT(bob_x);
    int bob_yi = FP2INT(bob_y);

    for (int y = 0; y < sh; y++) {
        uint32_t color;

        if (y < sh/2) {
            int t = y * 255 / (sh/2);
            color = bare_rgb(50 + t/4, 50 + t/4, 80 + t/2);
        } else {
            int t = (y - sh/2) * 255 / (sh/2);
            color = bare_rgb(60 - t/4, 40 - t/6, 20);
        }

        for (int x = 0; x < sw; x++) {
            bare_pixel(x, y, color);
        }
    }

    for (int col = 0; col < sw; col++) {
        int ray_angle = cam_angle + (col * FOV / sw) - FOV / 2;

        uint8_t texture_type;
        fixed_t wall_x;
        int hit_side;
        fixed_t dist = cast_ray(ray_angle, &texture_type, &wall_x, &hit_side);
        if (dist < FP_ONE / 4) dist = FP_ONE / 4;

        int wall_h = FP2INT(FP_DIV(INT2FP(sh), dist));
        if (wall_h > sh * 2) wall_h = sh * 2;

        int y0 = (sh - wall_h) / 2 + bob_yi;
        int y1 = y0 + wall_h;

        int tex_x = FP2INT(FP_MUL(wall_x, INT2FP(64)));
        if (tex_x >= 64) tex_x = 63;
        if (tex_x < 0) tex_x = 0;

        int draw_x_base = col + bob_xi;

        for (int y = y0; y < y1; y++) {
            if (y < 0 || y >= sh) continue;
            
            int tex_y = ((y - y0) * 64) / wall_h;
            if (tex_y >= 64) tex_y = 63;
            if (tex_y < 0) tex_y = 0;
            
            uint32_t color = get_texture_color(texture_type, tex_x, tex_y, dist);
            
            if (hit_side == 1) {
                int r = (color >> 16) & 0xFF;
                int g = (color >> 8) & 0xFF;
                int b = color & 0xFF;
                r = (r * 3) >> 2;
                g = (g * 3) >> 2;
                b = (b * 3) >> 2;
                color = bare_rgb(r, g, b);
            }
            
            bare_pixel(draw_x_base, y, color);
            bare_pixel(draw_x_base + 1, y, color);
        }
    }

    int ms = 4;
    for (int my = 0; my < MAP_H; my++) {
        for (int mx2 = 0; mx2 < MAP_W; mx2++) {
            uint32_t c;
            if (map[my][mx2]) {
                switch (map_textures[my][mx2]) {
                    case MAP_TEXTURE_STONE: c = bare_rgb(180, 180, 180); break;
                    case MAP_TEXTURE_WOOD:  c = bare_rgb(139, 90, 43); break;
                    case MAP_TEXTURE_METAL: c = bare_rgb(180, 190, 200); break;
                    case MAP_TEXTURE_BRICK: c = bare_rgb(180, 70, 50); break;
                    case MAP_TEXTURE_MARBLE: c = bare_rgb(230, 230, 240); break;
                    case MAP_TEXTURE_MOSS:  c = bare_rgb(70, 120, 40); break;
                    default:               c = bare_rgb(200, 200, 200); break;
                }
            } else {
                c = bare_rgb(25, 25, 25);
            }
            bare_rect_fill(mx2 * ms, my * ms, ms - 1, ms - 1, c);
        }
    }

    int px = FP2INT(cam_x) * ms + ms / 2;
    int py = FP2INT(cam_y) * ms + ms / 2;
    bare_rect_fill(px - 2, py - 2, 5, 5, bare_rgb(255, 100, 0));
}

void doomport_run(void) {
    cam_x = INT2FP(16);
    cam_y = INT2FP(8);
    cam_angle = 0;

    while (1) {
        keyboard_update();

        if (keyboard_held(0x01)) {
            terminal_clear();
            break;
        }

        is_moving = 0;

        if (keyboard_held(0x48)) {
            fixed_t dx = FP_MUL(fp_cos(cam_angle), MOVE_SPEED);
            fixed_t dy = FP_MUL(fp_sin(cam_angle), MOVE_SPEED);
            fixed_t nx = cam_x + dx;
            fixed_t ny = cam_y + dy;

            if (!map_solid(FP2INT(nx), FP2INT(cam_y))) cam_x = nx;
            if (!map_solid(FP2INT(cam_x), FP2INT(ny))) cam_y = ny;

            is_moving = 1;
        }

        if (keyboard_held(0x50)) {
            fixed_t dx = FP_MUL(fp_cos(cam_angle), MOVE_SPEED);
            fixed_t dy = FP_MUL(fp_sin(cam_angle), MOVE_SPEED);
            fixed_t nx = cam_x - dx;
            fixed_t ny = cam_y - dy;

            if (!map_solid(FP2INT(nx), FP2INT(cam_y))) cam_x = nx;
            if (!map_solid(FP2INT(cam_x), FP2INT(ny))) cam_y = ny;

            is_moving = 1;
        }

        if (is_moving) {
            bob_phase_x = (bob_phase_x + INT2FP(8)) % INT2FP(360);
            bob_phase_y = (bob_phase_y + INT2FP(12)) % INT2FP(360);
            if (bob_amount < INT2FP(2))
                bob_amount += FP_ONE / 4;
        } else {
            bob_phase_x = 0;
            bob_phase_y = 0;
            if (bob_amount > 0)
                bob_amount -= FP_ONE / 8;
            else
                bob_amount = 0;
        }

        if (keyboard_held(0x4B)) cam_angle = (cam_angle - TURN_SPEED + 360) % 360;
        if (keyboard_held(0x4D)) cam_angle = (cam_angle + TURN_SPEED) % 360;

        ray_render();
        bare_flip();
        sleep_ms(16);
    }
}