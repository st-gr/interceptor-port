/* src/frontend/sdl/effects.c - the combat effects (effects.h; docs/re/07-ai-and-weapons.md sections 7.7, 11, 13). */
#include "effects.h"
#include "../../core/fx.h"
#include <stdlib.h>
#include <string.h>

#define PEN_TRACER 9
#define PEN_CHAFF 13
#define MASK_ALL 15

static int16_t w16(int64_t v) { return (int16_t)(uint16_t)((uint64_t)v & 0xFFFFu); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int iabs(int v) { return v < 0 ? -v : v; }

/* The world wraps at 2^29 on x and z (note 05); a difference of two positions is taken in the signed range. */
static int32_t wrap29(int64_t d) {
    d &= 0x1FFFFFFF;
    if (d >= 0x10000000) d -= 0x20000000;
    return (int32_t)d;
}

void effects_begin(effects_t *e, raster_t *r, const game_scene_t *s, const effects_tables_t *tab) {
    e->r = r;
    e->cam = &s->camera;
    e->tab = tab && tab->loaded ? tab : NULL;
    e->row_limit = r->row_limit;
    world_camera_matrix_z(&s->camera, s->hud.cp_zoom ? s->hud.cp_zoom : 0x80, &e->m);
}

/* a difference of two x or z positions as the original forms it (note 07 7.7): the cell difference times 16384 plus the position within its cell shifted right by 8, minus the camera's word: the
 * difference of the two shifted positions (not the shifted difference), wrapped at the world's 2^21 words. */
static int32_t word_diff(int32_t a, int32_t b) {
    int32_t d = ((a >> 8) - (b >> 8)) & 0x1FFFFF;
    if (d >= 0x100000) d -= 0x200000;
    return d;
}

/* the position difference in words (the position shifted right by 8, minus the camera's); -1 when a component does not fit a word */
static int delta_words(const effects_t *e, const int32_t pos[3], fx_vec_t *w) {
    const int32_t dx = word_diff(pos[0], e->cam->pos.x), dy = (int32_t)(((int64_t)pos[1] - e->cam->pos.y) >> 8), dz = word_diff(pos[2], e->cam->pos.z);
    if (dx < -32768 || dx > 32767 || dy < -32768 || dy > 32767 || dz < -32768 || dz > 32767) return -1;
    w->x = dx; w->y = dy; w->z = dz;
    return 0;
}

int effects_view(const effects_t *e, const int32_t pos[3], fx_vec_t *out) {
    fx_vec_t w;
    if (delta_words(e, pos, &w) != 0) return 0;
    fx_camera(out, &e->m, &w);
    return 1;
}

/* ---- the tracer pool (7.7) ------------------------------------------------------------------------------------------------------------- */

static void tracer_pixel(effects_t *e, const int32_t pos[3], int pen) {
    fx_vec_t v;
    int16_t sx, sy;
    if (!effects_view(e, pos, &v)) return;
    if (fx_project_rows(&sx, &sy, &v, (int16_t)e->row_limit)) raster_point_sprite(e->r, sx, sy, 0, MASK_ALL, (unsigned)pen);
}

/* 0x3385C, the line of the star: not clipped; drawn only when both end points are inside the closed pyramid (|x| <= z, |y| <= z; z positive), projected with the line routine's own divides
 * (sx' = 160 x / z + 160 clamped to 0..319, sy' = 90 y / z + 90 clamped to 0..179, the screen point (319 - sx', 179 - sy')); the same arithmetic as the object drawer's op 48. */
static int inside_closed(const fx_vec_t *p) {
    const int x = (int16_t)p->x, y = (int16_t)p->y, z = (int16_t)p->z;
    if (z <= 0) return 0;
    return iabs(x) <= z && iabs(y) <= z;
}
static int line_div(int32_t prod, int den) {
    int32_t q = prod / den;
    return (q >= -32768 && q <= 32767) ? (int)q : (int)(int16_t)(prod & 0xFFFF);
}
static void line_end(const fx_vec_t *p, int *ox, int *oy) {
    int sx = line_div((int32_t)(int16_t)p->x * 160, (int16_t)p->z) + 160, sy = line_div((int32_t)(int16_t)p->y * 90, (int16_t)p->z) + 90;
    sx = clampi(sx, 0, 319); sy = clampi(sy, 0, 179);
    *ox = 319 - sx; *oy = 179 - sy;
}


static int star(effects_t *e, const int32_t pos[3]) {
    fx_vec_t c;
    int drawn = 0;
    if (!e->tab) return 0;   /* the star is data: tables/star.bin */
    if (delta_words(e, pos, &c) != 0) return 0;
    for (int k = 0; k < 3; k++) {
        fx_vec_t a, b, ta, tb;
        a.x = w16(c.x + e->tab->star[k][0][0]); a.y = w16(c.y + e->tab->star[k][0][1]); a.z = w16(c.z + e->tab->star[k][0][2]);
        b.x = w16(c.x + e->tab->star[k][1][0]); b.y = w16(c.y + e->tab->star[k][1][1]); b.z = w16(c.z + e->tab->star[k][1][2]);
        fx_camera(&ta, &e->m, &a); fx_camera(&tb, &e->m, &b);
        if (!inside_closed(&ta) || !inside_closed(&tb)) continue;
        int x0, y0, x1, y1;
        line_end(&ta, &x0, &y0); line_end(&tb, &x1, &y1);
        raster_line_planes(e->r, x0, y0, x1, y1, MASK_ALL, PEN_TRACER);
        drawn++;
    }
    return drawn;
}

/* ---- the burst routine 0x31766 (11.4) -------------------------------------------------------------------------------------------------- */

/* the screen point of a camera-space point, or 0 when the fragment is dropped (z <= 0, |x| > z or |y| > z): x = 0x13F - clamp(160 + 160 x / z, 0..0x13F), y = 0xB3 - clamp(90 + 90 y / z, 0..0xB4) */
static int burst_point(const fx_vec_t *v, int *sx, int *sy) {
    const int x = (int16_t)v->x, y = (int16_t)v->y, z = (int16_t)v->z;
    if (z <= 0 || iabs(x) > z || iabs(y) > z) return 0;
    *sx = 0x13F - clampi(160 + 160 * x / z, 0, 0x13F);
    *sy = 0xB3 - clampi(90 + 90 * y / z, 0, 0xB4);
    return 1;
}

/* One layer of triangles (or, with `lines`, of lines): n fragments, j from n - 1 down to 0. centre: the burst centre as words, camera-relative (before the shift s); cx/cy/cz are added after the shift (kind 3's layer c). */
static int burst_layer(effects_t *e, const fx_vec_t *centre, int s, int age, const int8_t (*off)[3], int n, const uint8_t *pens, int pen_forced, const int16_t (*add), int lines) {
    int drawn = 0;
    for (int j = n - 1; j >= 0; j--) {
        fx_vec_t fc;
        fc.x = w16((centre->x >> s) + (w16((int64_t)off[j][0] * age) >> s) + (add ? add[0] : 0));
        fc.y = w16((centre->y >> s) + (w16((int64_t)off[j][1] * age) >> s) + (add ? add[1] : 0));
        fc.z = w16((centre->z >> s) + (w16((int64_t)off[j][2] * age) >> s) + (add ? add[2] : 0));
        const int shape = (e->tab->cycle[(j + age) & 31] >> 2) & 7;   /* word (j + age) of the cycle table 0x31722 names the shape by its byte offset in the pointer table (4 k) */
        int16_t xy[6];
        int ok = 1;
        for (int k = 0; k < 3 && ok; k++) {
            fx_vec_t v, t;
            v.x = w16((fc.x + e->tab->shape[shape][k][0]) >> s);   /* the centre is shifted twice: the original's LOD rule */
            v.y = w16((fc.y + e->tab->shape[shape][k][1]) >> s);
            v.z = w16((fc.z + e->tab->shape[shape][k][2]) >> s);
            fx_camera(&t, &e->m, &v);
            int sx, sy;
            if (!burst_point(&t, &sx, &sy)) ok = 0; else { xy[2 * k] = (int16_t)sx; xy[2 * k + 1] = (int16_t)sy; }
        }
        if (!ok) continue;
        const unsigned pen = (unsigned)(pen_forced >= 0 ? pen_forced : pens[j]) & 15u;   /* the pen of the layer's table at the fragment (the chaff forces pen 13) */
        if (lines) raster_line_planes(e->r, xy[0], xy[1], xy[2], xy[3], MASK_ALL, pen);   /* kind 0 (chaff): the line from the first to the second vertex (0x31A78, line routine 0x3455E) */
        else raster_fill_polygon_planes(e->r, xy, 3, MASK_ALL, pen, -1, 0);
        drawn++;
    }
    return drawn;
}

/* kind 3, layer a: nine discs at the fragment centres with the radius (0x2800 * a / 6 + 0x2800) / distance (a word division; a sum past 0x7FFF is negative and draws nothing) */
static int burst_discs(effects_t *e, const fx_vec_t *centre, int s, int age) {
    int drawn = 0;
    for (int j = 8; j >= 0; j--) {
        fx_vec_t fc, t;
        fc.x = w16((centre->x >> s) + (w16((int64_t)e->tab->disc[j][0] * age) >> s));
        fc.y = w16((centre->y >> s) + (w16((int64_t)e->tab->disc[j][1] * age) >> s));
        fc.z = w16((centre->z >> s) + (w16((int64_t)e->tab->disc[j][2] * age) >> s));
        fx_camera(&t, &e->m, &fc);
        int sx, sy;
        if (!burst_point(&t, &sx, &sy)) continue;
        fx_vec_t a = { iabs((int16_t)t.x), iabs((int16_t)t.y), iabs((int16_t)t.z) };
        const int dist = (int16_t)fx_dist(&a);
        const int num = w16(0x2800 * age / 6 + 0x2800);
        int r = dist <= 0 ? 127 : num / dist;
        if (num < 0) continue;
        if (r > 127) r = 127;
        if (r < 1) continue;   /* the disc routine 0x33CAC returns at once for a radius below 1 (note 07 11.4, kind 3); the 2 by 1 sprite of a zero radius is op 62's rule, not this routine's */
        raster_disc(e->r, sx, sy, r, MASK_ALL, (unsigned)e->tab->pen[2][j] & 15u);   /* the disc pens of the table 0x316FD (13 and 10 alternating in the shipped image) */
        drawn++;
    }
    return drawn;
}

/* The burst (11.4) at pos with the kind (0 chaff: one layer of twelve lines in pen 13; 1 air or land: pen 9; 2 water: pen 2; 3 the cruise missile), the age and the entry's LOD shift s. Nothing is drawn
 * when |y + V:4AB30| exceeds 0x8000 (the height difference as the long the drawer holds). */
static int burst(effects_t *e, const int32_t pos[3], int kind, int age, int s) {
    if (!e->tab) return 0;
    fx_vec_t c;
    if (delta_words(e, pos, &c) != 0) return 0;
    const int32_t dy = (int32_t)(((int64_t)pos[1] - e->cam->pos.y) >> 8);
    if (iabs(dy) > 0x8000) return 0;
    if (age < 0) age = 0;
    if (s < 0) s = 0;
    switch (kind) {
    case 0: return burst_layer(e, &c, s, age, e->tab->off[0], 12, e->tab->pen[0], PEN_CHAFF, NULL, 1);   /* chaff (11.4, kind 0): the table of kind 1, the pen forced to 13, twelve lines */
    case 1: return burst_layer(e, &c, s, age, e->tab->off[0], 12, e->tab->pen[0], -1, NULL, 0);
    case 2: return burst_layer(e, &c, s, age, e->tab->off[0], 12, e->tab->pen[1], -1, NULL, 0);
    default: {
        static const int16_t move[3] = { 0x14, 0x0A, 0x14 };
        int n = burst_discs(e, &c, s, age);
        n += burst_layer(e, &c, s, age > 1 ? age - 1 : 0, e->tab->off[2], 12, e->tab->pen[3], -1, NULL, 0);
        n += burst_layer(e, &c, s, age > 2 ? age - 2 : 0, e->tab->off[1], 12, e->tab->pen[4], -1, move, 0);
        return n;
    }
    }
}

unsigned effects_pool_position(const game_scene_t *s) {
    const unsigned n = s->entry_count < GAME_MAX_ENTRIES ? s->entry_count : GAME_MAX_ENTRIES;
    if (!s->gun_first) return n;
    for (unsigned i = 0; i < n; i++) {
        const game_entry_t *e = &s->entries[i];
        if (e->kind == GAME_ENTRY_SPECK || e->kind == GAME_ENTRY_SPECK_TRI || e->kind == GAME_ENTRY_SPECK_FILL) continue;   /* the near-ground pass precedes the entity pass */
        if (e->kind == GAME_ENTRY_ENTITY || e->kind == GAME_ENTRY_SECONDARY || e->tier == GAME_TIER_DIRECT || e->tier == GAME_TIER_C) return i;
    }
    return n;
}

void effects_pool(effects_t *e, const game_scene_t *s) {
    for (int i = 0; i < s->effect_count && i < GAME_MAX_EFFECTS; i++) {
        const game_effect_t *f = &s->effects[i];
        const int32_t p[3] = { f->pos.x, f->pos.y, f->pos.z };
        const int life = f->phase;
        switch (f->kind) {
        case GAME_FX_SHELL:   /* the slot flags are in model: 0x0002 stopped (a tracer that hit something): the star for its two remaining passes */
            if (f->model & 0x1002) star(e, p); else tracer_pixel(e, p, PEN_TRACER);
            break;
        case GAME_FX_FLARE: star(e, p); break;
        case GAME_FX_CHAFF:
            if (f->model & 0x0002) star(e, p);
            else if (life > 0x3A) tracer_pixel(e, p, PEN_CHAFF);   /* 0x3140C stores the pen word 13 and jumps into the tracer's pixel code after its own pen store: the pixel is pen 13 (note 07 7.7) */
            else { int a = 0x3A - life; burst(e, p, 0, a > 12 ? 12 : a, 0); }
            break;
        default: break;
        }
    }
}

/* ---- the trail (13.2) ------------------------------------------------------------------------------------------------------------------- */

typedef struct { int x, y, z, r, s; } tdisc_t;

static int trail_disc(const effects_t *e, const int32_t pos[3], tdisc_t *d) {
    int32_t df[3] = { wrap29((int64_t)pos[0] - e->cam->pos.x), (int32_t)((int64_t)pos[1] - e->cam->pos.y), wrap29((int64_t)pos[2] - e->cam->pos.z) };
    int32_t big = 0;
    for (int i = 0; i < 3; i++) { const int32_t a = df[i] < 0 ? -df[i] : df[i]; if (a > big) big = a; }
    const int s = big <= 0x4000 ? 0 : big <= 0x40000 ? 4 : 8;
    fx_vec_t sv, ab, rot;
    sv.x = w16(df[0] >> s); sv.y = w16(df[1] >> s); sv.z = w16(df[2] >> s);
    ab.x = iabs((int16_t)sv.x); ab.y = iabs((int16_t)sv.y); ab.z = iabs((int16_t)sv.z);
    const int dist = (int16_t)fx_dist(&ab);
    fx_camera(&rot, &e->m, &sv);
    d->x = (int16_t)rot.x; d->y = (int16_t)rot.y; d->z = (int16_t)rot.z; d->s = s;
    const int div = dist >> (8 - s);
    d->r = div <= 0 ? 0x7F : 0x9C0 / div;
    if (d->r > 0x7F) d->r = 0x7F;
    return 1;
}

static int trail_draw(effects_t *e, const tdisc_t *d, int r, int pen) {
    fx_vec_t v = { d->x, d->y, d->z };
    int16_t sx, sy;
    if (!fx_project_rows(&sx, &sy, &v, (int16_t)e->row_limit)) return 0;
    raster_disc(e->r, sx, sy, r > 0 ? r : 0, MASK_ALL, (unsigned)pen);
    return 1;
}

int effects_trail(effects_t *e, const game_trail_t *t) {
    if (t->length == 0 || t->count == 0) return 0;
    const int cnt = t->count < 6 ? t->count : 6, full = t->count >= 6;
    int n = t->length - (full ? 0 : 1);
    if (n < 0) n = 0;
    /* the direction (13.2 step 1): the sign of (the record's velocity longs . (its position - the camera's)), each factor shifted right by 8 when the speed word is 0x100 or more and truncated to
     * words before the product. Negative = moving toward the camera. The sum of the three word products is 32-bit and wraps (note 07 13.2: muls.w three times, add.l twice) [V]: the sign is that of the wrapped sum. */
    int toward = 0;
    {
        uint32_t dot = 0;
        const int sh = t->speed >= 0x100 ? 8 : 0;
        for (int a = 0; a < 3; a++) {
            const int32_t rel = a == 1 ? (int32_t)((int64_t)t->pos[1] - e->cam->pos.y) : wrap29((int64_t)t->pos[a] - (a == 0 ? e->cam->pos.x : e->cam->pos.z));
            const int16_t vw = (int16_t)(uint16_t)((uint32_t)(t->vel[a] >> sh) & 0xFFFF), rw = (int16_t)(uint16_t)((uint32_t)(rel >> sh) & 0xFFFF);
            dot += (uint32_t)((int32_t)vw * (int32_t)rw);
        }
        toward = (int32_t)dot < 0;
    }
    int slots[8], ns = 0;
    if (toward) {
        const int start = full ? t->index : 0;
        for (int k = 0; k <= n && ns < 8; k++) slots[ns++] = (start + k) % cnt;
    } else {
        int start = (t->index - t->count - (full ? 0 : 1) + t->length) % cnt;
        if (start < 0) start += cnt;
        for (int k = 0; k <= n && ns < 8; k++) { int sl = (start - k) % cnt; if (sl < 0) sl += cnt; slots[ns++] = sl; }
    }
    int drawn = 0;
    tdisc_t prev_d = { 0, 0, 0, 0, 0 };
    for (int k = 0; k < ns; k++) {
        const int slot = slots[k];
        tdisc_t d;
        const int32_t p[3] = { t->ring[slot][0], t->ring[slot][1], t->ring[slot][2] };
        trail_disc(e, p, &d);
        const int pen = (slot & 1) ? (t->ooc ? 3 : 8) : (t->ooc ? 12 : 10);
        if (k > 0 && t->length > 1) {   /* three discs at 1/4, 1/2 and 3/4 from the previous, in x, y, z and radius */
            const int sh = (prev_d.s - d.s) & 63;
            const int px = sh >= 16 ? 0 : (int16_t)(prev_d.x << sh), py = sh >= 16 ? 0 : (int16_t)(prev_d.y << sh), pz = sh >= 16 ? 0 : (int16_t)(prev_d.z << sh);
            for (int q = 1; q <= 3; q++) {
                tdisc_t m;
                m.x = px + (d.x - px) * q / 4; m.y = py + (d.y - py) * q / 4; m.z = pz + (d.z - pz) * q / 4;
                m.r = prev_d.r + (d.r - prev_d.r) * q / 4; m.s = d.s;
                drawn += trail_draw(e, &m, m.r, pen);
            }
        }
        int r = d.r;
        if (k == ns - 1) r -= r >> 3;   /* the last disc drawn has its radius reduced by 1/8 */
        drawn += trail_draw(e, &d, r, pen);
        prev_d = d;
    }
    return drawn;
}

int effects_tail(effects_t *e, const game_scene_t *s, int rec, int lod) {
    int n = 0;
    for (int i = 0; i < s->effect_count && i < GAME_MAX_EFFECTS; i++) {
        const game_effect_t *f = &s->effects[i];
        if (f->kind != GAME_FX_BURST || f->rec != rec) continue;
        const int32_t p[3] = { f->pos.x, f->pos.y, f->pos.z };
        n += burst(e, p, (int)f->model, (int)f->age, lod);
    }
    if (s->camera.trail == rec) n += effects_trail(e, &s->trail);
    return n;
}

/* ---- the jolt (13.3) -------------------------------------------------------------------------------------------------------------------- */

int effects_window_row(const game_scene_t *s, int base) {
    if (s->camera.view != GAME_VIEW_COCKPIT) return base;
    const int hs = s->camera.hud_step < 0 ? -s->camera.hud_step : s->camera.hud_step;
    if (hs > 14) return base;   /* |V:4AA3E| above 14: the full-width views cancel the jolt */
    return base + s->jolt_row;
}

/* ---- the tables ------------------------------------------------------------------------------------------------------------------------- */

int effects_load_tables(const game_loader_t *loader, effects_tables_t *t) {
    burst_tables_t b;
    star_t st;
    memset(t, 0, sizeof *t);
    if (assets_load_burst(loader, &b, NULL, 0) != 0) return -1;
    if (assets_load_star(loader, &st, NULL, 0) != 0) return -1;
    memcpy(t->star, st.pt, sizeof t->star);
    memcpy(t->shape, b.shape, sizeof t->shape);
    memcpy(t->off, b.off, sizeof t->off);
    memcpy(t->disc, b.disc, sizeof t->disc);
    memcpy(t->pen, b.pen, sizeof t->pen);
    memcpy(t->cycle, b.cycle, sizeof t->cycle);
    t->loaded = 1;
    return 0;
}
