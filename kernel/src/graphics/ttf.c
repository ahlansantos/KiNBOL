#include "ttf.h"

#define TTF_SS 4

static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
static int16_t rd_s16(const uint8_t *p) {
    return (int16_t)rd_u16(p);
}
static uint32_t rd_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static int in_bounds(const ttf_font_t *f, uint32_t off, uint32_t len) {
    return (uint64_t)off + (uint64_t)len <= (uint64_t)f->size;
}

int ttf_init(ttf_font_t *f, const uint8_t *data, size_t size) {
    if (!f || !data || size < 12) return 0;

    for (int i = 0; i < (int)sizeof(*f); i++) ((uint8_t *)f)[i] = 0;
    f->data = data;
    f->size = size;

    uint32_t sfnt_ver = rd_u32(data);
    if (sfnt_ver != 0x00010000u && sfnt_ver != 0x4F54544Fu &&
        sfnt_ver != 0x74727565u) {
        return 0;
    }

    int num_tables = rd_u16(data + 4);
    if (!in_bounds(f, 12, (uint32_t)num_tables * 16)) return 0;

    uint32_t head_off = 0, maxp_off = 0, loca_off = 0, loca_len = 0;
    uint32_t glyf_off = 0, glyf_len = 0, cmap_off = 0;
    uint32_t hhea_off = 0, hmtx_off = 0;
    int have_head = 0, have_maxp = 0, have_loca = 0, have_glyf = 0, have_cmap = 0;

    for (int i = 0; i < num_tables; i++) {
        const uint8_t *rec = data + 12 + i * 16;
        uint32_t tag = rd_u32(rec);
        uint32_t off = rd_u32(rec + 8);
        uint32_t len = rd_u32(rec + 12);
        if (!in_bounds(f, off, len)) continue;

        switch (tag) {
            case 0x68656164u: head_off = off; have_head = 1; break; /* "head" */
            case 0x6D617870u: maxp_off = off; have_maxp = 1; break; /* "maxp" */
            case 0x6C6F6361u: loca_off = off; loca_len = len; have_loca = 1; break; /* "loca" */
            case 0x676C7966u: glyf_off = off; glyf_len = len; have_glyf = 1; break; /* "glyf" */
            case 0x636D6170u: cmap_off = off; have_cmap = 1; break; /* "cmap" */
            case 0x68686561u: hhea_off = off; break; /* "hhea" */
            case 0x686D7478u: hmtx_off = off; break; /* "hmtx" */
            default: break;
        }
    }

    if (!have_head || !have_maxp || !have_loca || !have_glyf || !have_cmap) return 0;
    if (!in_bounds(f, head_off, 54) || !in_bounds(f, maxp_off, 6)) return 0;

    f->head_off      = head_off;
    f->maxp_off       = maxp_off;
    f->loca_off       = loca_off;
    f->loca_len       = loca_len;
    f->glyf_off       = glyf_off;
    f->glyf_len       = glyf_len;
    f->cmap_off       = cmap_off;
    f->hhea_off       = hhea_off;
    f->hmtx_off       = hmtx_off;

    f->units_per_em   = rd_u16(data + head_off + 18);
    if (f->units_per_em <= 0) f->units_per_em = 1000;
    f->loca_long      = rd_s16(data + head_off + 50) != 0;
    f->num_glyphs     = rd_u16(data + maxp_off + 4);

    if (hhea_off && in_bounds(f, hhea_off, 36)) {
        f->num_hmetrics = rd_u16(data + hhea_off + 34);
    } else {
        f->num_hmetrics = 0;
    }

    /* pick a cmap subtable: prefer (3,1) BMP, then (3,10)/(0,*) full unicode,
     * then (0,3), else first table found. */
    if (!in_bounds(f, cmap_off, 4)) return 0;
    int n_sub = rd_u16(data + cmap_off + 2);
    uint32_t best_off = 0;
    int      best_score = -1;

    for (int i = 0; i < n_sub; i++) {
        uint32_t rec_off = cmap_off + 4 + (uint32_t)i * 8;
        if (!in_bounds(f, rec_off, 8)) continue;
        int plat = rd_u16(data + rec_off);
        int eid  = rd_u16(data + rec_off + 2);
        uint32_t sub_off = cmap_off + rd_u32(data + rec_off + 4);
        if (!in_bounds(f, sub_off, 4)) continue;
        int fmt = rd_u16(data + sub_off);

        int score = -1;
        if (plat == 3 && eid == 1 && fmt == 4)  score = 100;
        else if (plat == 3 && eid == 10 && fmt == 12) score = 90;
        else if (plat == 0 && fmt == 4)         score = 80;
        else if (plat == 0 && fmt == 12)        score = 75;
        else if (plat == 1 && eid == 0 && fmt == 0) score = 10;

        if (score > best_score) {
            best_score = score;
            best_off   = sub_off;
        }
    }

    if (best_score < 0) return 0;
    f->cmap_subtable_off = best_off;
    f->cmap_format        = rd_u16(data + best_off);
    return 1;
}

int ttf_find_glyph_index(const ttf_font_t *f, uint32_t codepoint) {
    const uint8_t *data = f->data;
    uint32_t sub = f->cmap_subtable_off;

    if (f->cmap_format == 4) {
        if (!in_bounds(f, sub, 14)) return 0;
        int seg_x2   = rd_u16(data + sub + 6);
        int seg_count = seg_x2 / 2;
        uint32_t end_off   = sub + 14;
        uint32_t start_off = end_off + seg_x2 + 2;
        uint32_t delta_off = start_off + seg_x2;
        uint32_t range_off = delta_off + seg_x2;

        if (codepoint > 0xFFFF) return 0;
        if (!in_bounds(f, range_off, (uint32_t)seg_count * 2)) return 0;

        for (int i = 0; i < seg_count; i++) {
            uint16_t end = rd_u16(data + end_off + i * 2);
            if (codepoint > end) continue;
            uint16_t start = rd_u16(data + start_off + i * 2);
            if (codepoint < start) return 0;
            int16_t  delta = rd_s16(data + delta_off + i * 2);
            uint16_t range = rd_u16(data + range_off + i * 2);
            if (range == 0) {
                return (uint16_t)(codepoint + delta);
            }
            uint32_t addr = range_off + i * 2 + range + (codepoint - start) * 2;
            if (!in_bounds(f, addr, 2)) return 0;
            uint16_t gid = rd_u16(data + addr);
            if (gid == 0) return 0;
            return (uint16_t)(gid + delta);
        }
        return 0;
    }

    if (f->cmap_format == 12) {
        if (!in_bounds(f, sub, 16)) return 0;
        uint32_t n_groups = rd_u32(data + sub + 12);
        uint32_t base = sub + 16;
        for (uint32_t i = 0; i < n_groups; i++) {
            uint32_t g = base + i * 12;
            if (!in_bounds(f, g, 12)) break;
            uint32_t start_cp = rd_u32(data + g);
            uint32_t end_cp   = rd_u32(data + g + 4);
            uint32_t start_id = rd_u32(data + g + 8);
            if (codepoint >= start_cp && codepoint <= end_cp) {
                return (int)(start_id + (codepoint - start_cp));
            }
        }
        return 0;
    }

    if (f->cmap_format == 0) {
        if (!in_bounds(f, sub, 262) || codepoint > 255) return 0;
        return data[sub + 6 + codepoint];
    }

    return 0;
}

static uint32_t glyf_loca(const ttf_font_t *f, int glyph_index, uint32_t *out_len) {
    const uint8_t *data = f->data;
    if (glyph_index < 0 || glyph_index >= f->num_glyphs) { *out_len = 0; return 0; }

    uint32_t off1, off2;
    if (f->loca_long) {
        uint32_t rec = f->loca_off + (uint32_t)glyph_index * 4;
        if (!in_bounds(f, rec, 8)) { *out_len = 0; return 0; }
        off1 = rd_u32(data + rec);
        off2 = rd_u32(data + rec + 4);
    } else {
        uint32_t rec = f->loca_off + (uint32_t)glyph_index * 2;
        if (!in_bounds(f, rec, 4)) { *out_len = 0; return 0; }
        off1 = (uint32_t)rd_u16(data + rec) * 2;
        off2 = (uint32_t)rd_u16(data + rec + 2) * 2;
    }
    if (off2 < off1) { *out_len = 0; return 0; }
    *out_len = off2 - off1;
    return f->glyf_off + off1;
}

/* ---- outline extraction: emits a flat point list per contour, already
 * flattened (quadratic bezier curves subdivided into line segments). ---- */

typedef struct { float x, y; } ttf_pt;

typedef struct {
    ttf_pt *pts;
    int     count;
    int     cap;
} ttf_ptbuf;

static int ptbuf_push(ttf_ptbuf *b, float x, float y, ttf_alloc_fn alloc, ttf_free_fn freefn) {
    if (b->count >= b->cap) {
        int newcap = b->cap ? b->cap * 2 : 64;
        ttf_pt *np = (ttf_pt *)alloc((size_t)newcap * sizeof(ttf_pt));
        if (!np) return 0;
        for (int i = 0; i < b->count; i++) np[i] = b->pts[i];
        if (b->pts) freefn(b->pts);
        b->pts = np;
        b->cap = newcap;
    }
    b->pts[b->count].x = x;
    b->pts[b->count].y = y;
    b->count++;
    return 1;
}

static void flatten_quad(ttf_ptbuf *b, float x0, float y0, float cx, float cy,
                          float x1, float y1, int depth,
                          ttf_alloc_fn alloc, ttf_free_fn freefn) {
    if (depth >= 6) {
        ptbuf_push(b, x1, y1, alloc, freefn);
        return;
    }
    float mx = (x0 + 2 * cx + x1) * 0.25f;
    float my = (y0 + 2 * cy + y1) * 0.25f;
    float lx = (x0 + x1) * 0.5f;
    float ly = (y0 + y1) * 0.5f;
    float dx = mx - lx, dy = my - ly;
    float flatness = dx * dx + dy * dy;

    if (flatness < 0.09f) {
        ptbuf_push(b, x1, y1, alloc, freefn);
        return;
    }

    float c1x = (x0 + cx) * 0.5f, c1y = (y0 + cy) * 0.5f;
    float c2x = (cx + x1) * 0.5f, c2y = (cy + y1) * 0.5f;
    float midx = (c1x + c2x) * 0.5f, midy = (c1y + c2y) * 0.5f;

    flatten_quad(b, x0, y0, c1x, c1y, midx, midy, depth + 1, alloc, freefn);
    flatten_quad(b, midx, midy, c2x, c2y, x1, y1, depth + 1, alloc, freefn);
}

/* Contours are stored back-to-back in one ptbuf; contour_ends[i] gives the
 * (exclusive) end index of contour i within pts. */
#define TTF_MAX_CONTOURS 64

typedef struct {
    ttf_ptbuf pts;
    int       contour_end[TTF_MAX_CONTOURS];
    int       n_contours;
} ttf_outline;

static int extract_simple_glyph(const ttf_font_t *f, uint32_t off, uint32_t len,
                                 float dx_shift, float dy_shift,
                                 float a, float b, float c, float d,
                                 ttf_outline *out,
                                 ttf_alloc_fn alloc, ttf_free_fn freefn) {
    const uint8_t *data = f->data;
    if (!in_bounds(f, off, len) || len < 10) return 1; /* empty glyph, ok */

    int n_contours = rd_s16(data + off);
    if (n_contours < 0) return -1; /* composite handled elsewhere */
    if (n_contours == 0 || n_contours > TTF_MAX_CONTOURS) return 1;

    uint32_t p = off + 10;
    if (!in_bounds(f, p, (uint32_t)n_contours * 2)) return 0;

    int end_pts[TTF_MAX_CONTOURS];
    for (int i = 0; i < n_contours; i++) end_pts[i] = rd_u16(data + p + i * 2);
    int n_points = end_pts[n_contours - 1] + 1;
    p += (uint32_t)n_contours * 2;

    if (!in_bounds(f, p, 2)) return 0;
    int ins_len = rd_u16(data + p);
    p += 2 + (uint32_t)ins_len;

    /* flags */
    uint8_t *flags = (uint8_t *)alloc((size_t)n_points);
    if (!flags) return 0;
    int fi = 0;
    while (fi < n_points) {
        if (!in_bounds(f, p, 1)) { freefn(flags); return 0; }
        uint8_t fl = data[p++];
        flags[fi++] = fl;
        if (fl & 0x08) {
            if (!in_bounds(f, p, 1)) { freefn(flags); return 0; }
            int repeat = data[p++];
            for (int r = 0; r < repeat && fi < n_points; r++) flags[fi++] = fl;
        }
    }

    /* x coordinates */
    int *xs = (int *)alloc((size_t)n_points * sizeof(int));
    int *ys = (int *)alloc((size_t)n_points * sizeof(int));
    if (!xs || !ys) {
        freefn(flags); if (xs) freefn(xs); if (ys) freefn(ys);
        return 0;
    }

    int x = 0;
    for (int i = 0; i < n_points; i++) {
        uint8_t fl = flags[i];
        if (fl & 0x02) {
            if (!in_bounds(f, p, 1)) { freefn(flags); freefn(xs); freefn(ys); return 0; }
            int dxv = data[p++];
            x += (fl & 0x10) ? dxv : -dxv;
        } else if (!(fl & 0x10)) {
            if (!in_bounds(f, p, 2)) { freefn(flags); freefn(xs); freefn(ys); return 0; }
            x += rd_s16(data + p);
            p += 2;
        }
        xs[i] = x;
    }
    int y = 0;
    for (int i = 0; i < n_points; i++) {
        uint8_t fl = flags[i];
        if (fl & 0x04) {
            if (!in_bounds(f, p, 1)) { freefn(flags); freefn(xs); freefn(ys); return 0; }
            int dyv = data[p++];
            y += (fl & 0x20) ? dyv : -dyv;
        } else if (!(fl & 0x20)) {
            if (!in_bounds(f, p, 2)) { freefn(flags); freefn(xs); freefn(ys); return 0; }
            y += rd_s16(data + p);
            p += 2;
        }
        ys[i] = y;
    }

    int start = 0;
    for (int ci = 0; ci < n_contours; ci++) {
        int end = end_pts[ci];
        int npc = end - start + 1;
        if (npc > 0) {
            /* find an on-curve start point (or synthesize a midpoint if all off-curve) */
            int start_idx = -1;
            for (int i = 0; i < npc; i++) {
                if (flags[start + i] & 0x01) { start_idx = i; break; }
            }
            float sx, sy;
            int   base_i;
            if (start_idx >= 0) {
                base_i = start_idx;
                sx = (float)xs[start + base_i] * a + (float)ys[start + base_i] * c + dx_shift;
                sy = (float)xs[start + base_i] * b + (float)ys[start + base_i] * d + dy_shift;
            } else {
                base_i = 0;
                float x0 = (float)xs[start] * a + (float)ys[start] * c + dx_shift;
                float y0 = (float)xs[start] * b + (float)ys[start] * d + dy_shift;
                float x1 = (float)xs[start + 1 % npc] * a + (float)ys[start + 1 % npc] * c + dx_shift;
                float y1 = (float)xs[start + 1 % npc] * b + (float)ys[start + 1 % npc] * d + dy_shift;
                sx = (x0 + x1) * 0.5f;
                sy = (y0 + y1) * 0.5f;
            }

            if (!ptbuf_push(&out->pts, sx, sy, alloc, freefn)) {
                freefn(flags); freefn(xs); freefn(ys); return 0;
            }

            float cx = sx, cy = sy;
            int   have_ctrl = 0;
            float ctrlx = 0, ctrly = 0;
            float curx = sx, cury = sy;

            for (int k = 1; k <= npc; k++) {
                int idx = start + (base_i + k) % npc;
                int on_curve = flags[idx] & 0x01;
                float px = (float)xs[idx] * a + (float)ys[idx] * c + dx_shift;
                float py = (float)xs[idx] * b + (float)ys[idx] * d + dy_shift;

                if (on_curve) {
                    if (have_ctrl) {
                        flatten_quad(&out->pts, curx, cury, ctrlx, ctrly, px, py, 0, alloc, freefn);
                        have_ctrl = 0;
                    } else {
                        if (!ptbuf_push(&out->pts, px, py, alloc, freefn)) {
                            freefn(flags); freefn(xs); freefn(ys); return 0;
                        }
                    }
                    curx = px; cury = py;
                } else {
                    if (have_ctrl) {
                        float mx = (ctrlx + px) * 0.5f;
                        float my = (ctrly + py) * 0.5f;
                        flatten_quad(&out->pts, curx, cury, ctrlx, ctrly, mx, my, 0, alloc, freefn);
                        curx = mx; cury = my;
                    }
                    ctrlx = px; ctrly = py;
                    have_ctrl = 1;
                }
            }
            if (have_ctrl) {
                flatten_quad(&out->pts, curx, cury, ctrlx, ctrly, sx, sy, 0, alloc, freefn);
            }
            (void)cx; (void)cy;
        }
        if (out->n_contours < TTF_MAX_CONTOURS) {
            out->contour_end[out->n_contours++] = out->pts.count;
        }
        start = end + 1;
    }

    freefn(flags); freefn(xs); freefn(ys);
    return 1;
}

static int extract_glyph_outline(const ttf_font_t *f, int glyph_index,
                                  float dx_shift, float dy_shift,
                                  float a, float b, float c, float d,
                                  ttf_outline *out, int depth,
                                  ttf_alloc_fn alloc, ttf_free_fn freefn) {
    if (depth > 6) return 1;
    uint32_t len;
    uint32_t off = glyf_loca(f, glyph_index, &len);
    if (len == 0) return 1;

    int n_contours = rd_s16(f->data + off);
    if (n_contours >= 0) {
        return extract_simple_glyph(f, off, len, dx_shift, dy_shift, a, b, c, d,
                                     out, alloc, freefn);
    }

    /* composite glyph */
    const uint8_t *data = f->data;
    uint32_t p = off + 10;
    for (;;) {
        if (!in_bounds(f, p, 4)) break;
        uint16_t flags = rd_u16(data + p);
        uint16_t comp_gi = rd_u16(data + p + 2);
        p += 4;

        int arg1, arg2;
        if (flags & 0x0001) { /* ARG_1_AND_2_ARE_WORDS */
            if (!in_bounds(f, p, 4)) break;
            arg1 = rd_s16(data + p);
            arg2 = rd_s16(data + p + 2);
            p += 4;
        } else {
            if (!in_bounds(f, p, 2)) break;
            arg1 = (int8_t)data[p];
            arg2 = (int8_t)data[p + 1];
            p += 2;
        }

        float ca = 1, cb = 0, cc = 0, cd = 1;
        if (flags & 0x0008) { /* WE_HAVE_A_SCALE */
            if (!in_bounds(f, p, 2)) break;
            ca = cd = (float)rd_s16(data + p) / 16384.0f;
            p += 2;
        } else if (flags & 0x0040) { /* X_AND_Y_SCALE */
            if (!in_bounds(f, p, 4)) break;
            ca = (float)rd_s16(data + p) / 16384.0f;
            cd = (float)rd_s16(data + p + 2) / 16384.0f;
            p += 4;
        } else if (flags & 0x0080) { /* 2x2 */
            if (!in_bounds(f, p, 8)) break;
            ca = (float)rd_s16(data + p) / 16384.0f;
            cb = (float)rd_s16(data + p + 2) / 16384.0f;
            cc = (float)rd_s16(data + p + 4) / 16384.0f;
            cd = (float)rd_s16(data + p + 6) / 16384.0f;
            p += 8;
        }

        float comp_dx = 0, comp_dy = 0;
        if (flags & 0x0002) { /* ARGS_ARE_XY_VALUES */
            comp_dx = (float)arg1;
            comp_dy = (float)arg2;
        }

        /* combine parent transform (a,b,c,d,dx_shift,dy_shift) with this
         * component's transform (ca,cb,cc,cd,comp_dx,comp_dy) */
        float na = ca * a + cb * c;
        float nb = ca * b + cb * d;
        float nc = cc * a + cd * c;
        float nd = cc * b + cd * d;
        float ndx = comp_dx * a + comp_dy * c + dx_shift;
        float ndy = comp_dx * b + comp_dy * d + dy_shift;

        extract_glyph_outline(f, comp_gi, ndx, ndy, na, nb, nc, nd, out,
                               depth + 1, alloc, freefn);

        if (!(flags & 0x0020)) break; /* !MORE_COMPONENTS */
    }
    return 1;
}

/* ---- scanline rasterizer: nonzero winding fill via supersampling ---- */

typedef struct { float x0, y0, x1, y1; int dir; } ttf_edge;

static int build_edges(const ttf_outline *o, ttf_edge *edges, int max_edges) {
    int n = 0;
    int start = 0;
    for (int c = 0; c < o->n_contours; c++) {
        int end = o->contour_end[c];
        for (int i = start; i < end; i++) {
            ttf_pt p0 = o->pts.pts[i];
            ttf_pt p1 = o->pts.pts[(i + 1 < end) ? i + 1 : start];
            if (p0.y == p1.y) continue;
            if (n >= max_edges) return n;
            if (p0.y < p1.y) {
                edges[n].x0 = p0.x; edges[n].y0 = p0.y;
                edges[n].x1 = p1.x; edges[n].y1 = p1.y;
                edges[n].dir = 1;
            } else {
                edges[n].x0 = p1.x; edges[n].y0 = p1.y;
                edges[n].x1 = p0.x; edges[n].y1 = p0.y;
                edges[n].dir = -1;
            }
            n++;
        }
        start = end;
    }
    return n;
}

int ttf_rasterize_glyph(const ttf_font_t *f, int glyph_index, float scale,
                         ttf_alloc_fn alloc, ttf_free_fn freefn,
                         ttf_bitmap_t *out) {
    if (!f || !out) return 0;
    out->pixels = 0; out->w = out->h = 0; out->xoff = out->yoff = 0; out->advance_px = 0;

    uint32_t len;
    uint32_t goff = glyf_loca(f, glyph_index, &len);

    int advance = 0;
    if (f->hmtx_off && f->num_hmetrics > 0) {
        int idx = glyph_index < f->num_hmetrics ? glyph_index : f->num_hmetrics - 1;
        uint32_t rec = f->hmtx_off + (uint32_t)idx * 4;
        if (in_bounds(f, rec, 2)) {
            advance = (int)(rd_u16(f->data + rec) * scale + 0.5f);
        }
    }
    out->advance_px = advance;

    if (len == 0) return 1; /* space or missing glyph: empty bitmap, valid advance */

    int xmin = rd_s16(f->data + goff + 2);
    int ymin = rd_s16(f->data + goff + 4);
    int xmax = rd_s16(f->data + goff + 6);
    int ymax = rd_s16(f->data + goff + 8);

    ttf_outline outline;
    outline.pts.pts = 0; outline.pts.count = 0; outline.pts.cap = 0;
    outline.n_contours = 0;

    /* shift so glyph-space (0,0) maps to (0, ymax) scaled, i.e. render into
     * a tight box; y flipped so row 0 is the top of the glyph. */
    float shift_x = -(float)xmin * scale;
    float shift_y = (float)ymax * scale;

    int ok = extract_glyph_outline(f, glyph_index, shift_x, shift_y,
                                    scale, 0.0f, 0.0f, -scale,
                                    &outline, 0, alloc, freefn);
    if (!ok || outline.pts.count == 0) {
        if (outline.pts.pts) freefn(outline.pts.pts);
        return 1;
    }

    int w = (int)((xmax - xmin) * scale) + 2;
    int h = (int)((ymax - ymin) * scale) + 2;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > 512) w = 512;
    if (h > 512) h = 512;

    uint8_t *bmp = (uint8_t *)alloc((size_t)w * (size_t)h);
    if (!bmp) { freefn(outline.pts.pts); return 0; }
    for (int i = 0; i < w * h; i++) bmp[i] = 0;

    ttf_edge *edges = (ttf_edge *)alloc(sizeof(ttf_edge) * (size_t)(outline.pts.count + 4));
    if (!edges) { freefn(bmp); freefn(outline.pts.pts); return 0; }
    int n_edges = build_edges(&outline, edges, outline.pts.count + 4);

    int ss_w = w * TTF_SS;
    uint32_t *acc = (uint32_t *)alloc((size_t)w * sizeof(uint32_t));
    int *xs_buf = (int *)alloc(sizeof(int) * (size_t)(n_edges + 1));
    int *dir_buf = (int *)alloc(sizeof(int) * (size_t)(n_edges + 1));
    if (!acc || !xs_buf || !dir_buf) {
        if (acc) freefn(acc);
        if (xs_buf) freefn(xs_buf);
        if (dir_buf) freefn(dir_buf);
        freefn(edges); freefn(bmp); freefn(outline.pts.pts);
        return 0;
    }

    for (int row = 0; row < h; row++) {
        for (int i = 0; i < w; i++) acc[i] = 0;

        for (int sy = 0; sy < TTF_SS; sy++) {
            float scan_y = (float)row + ((float)sy + 0.5f) / (float)TTF_SS;

            int n_hits = 0;
            for (int e = 0; e < n_edges; e++) {
                ttf_edge *ed = &edges[e];
                if (scan_y < ed->y0 || scan_y >= ed->y1) continue;
                float t = (scan_y - ed->y0) / (ed->y1 - ed->y0);
                float x = ed->x0 + t * (ed->x1 - ed->x0);
                xs_buf[n_hits] = (int)(x * (float)TTF_SS + 0.5f);
                dir_buf[n_hits] = ed->dir;
                n_hits++;
            }

            /* insertion sort by x (n_hits is small) */
            for (int i = 1; i < n_hits; i++) {
                int xv = xs_buf[i], dv = dir_buf[i];
                int j = i - 1;
                while (j >= 0 && xs_buf[j] > xv) {
                    xs_buf[j + 1] = xs_buf[j];
                    dir_buf[j + 1] = dir_buf[j];
                    j--;
                }
                xs_buf[j + 1] = xv;
                dir_buf[j + 1] = dv;
            }

            int winding = 0;
            int span_start = 0;
            for (int i = 0; i < n_hits; i++) {
                int was_inside = winding != 0;
                winding += dir_buf[i];
                int now_inside = winding != 0;
                if (!was_inside && now_inside) {
                    span_start = xs_buf[i];
                } else if (was_inside && !now_inside) {
                    int span_end = xs_buf[i];
                    if (span_start < 0) span_start = 0;
                    if (span_end > ss_w) span_end = ss_w;
                    for (int sx = span_start; sx < span_end; sx++) {
                        acc[sx / TTF_SS]++;
                    }
                }
            }
        }

        for (int i = 0; i < w; i++) {
            uint32_t v = acc[i] * 255 / (TTF_SS * TTF_SS);
            bmp[row * w + i] = (uint8_t)(v > 255 ? 255 : v);
        }
    }

    freefn(acc); freefn(xs_buf); freefn(dir_buf);
    freefn(edges);
    freefn(outline.pts.pts);

    out->pixels = bmp;
    out->w = w;
    out->h = h;
    out->xoff = (int)(xmin * scale);
    out->yoff = -(int)(ymax * scale);
    return 1;
}

float ttf_scale_for_pixel_height(const ttf_font_t *f, float pixel_height) {
    if (f->units_per_em <= 0) return 1.0f;
    return pixel_height / (float)f->units_per_em;
}

int ttf_rasterize_codepoint(const ttf_font_t *f, uint32_t codepoint, float pixel_height,
                             ttf_alloc_fn alloc, ttf_free_fn freefn,
                             ttf_bitmap_t *out) {
    int gi = ttf_find_glyph_index(f, codepoint);
    float scale = ttf_scale_for_pixel_height(f, pixel_height);
    return ttf_rasterize_glyph(f, gi, scale, alloc, freefn, out);
}