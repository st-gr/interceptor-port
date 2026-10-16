/* src/core/flight_coll.c - terrain class (note 05 section 10), the carrier deck, hull and arrestor
 * tests (section 9) and the landmark phase (section 11b). Part of the flight module (flight.h). */
#include <string.h>
#include "flight_int.h"

/* ---- section 10: terrain class (0x2F75A) --------------------------------------------------- */

static int32_t wrap32(int64_t v) { return (int32_t)(uint32_t)((uint64_t)v & 0xFFFFFFFFu); }

#ifndef FL_UFORM
#define FL_UFORM 0   /* the u normalisation of the original; 1 selects the other reading (tests only, see tests/rig/test_flight_diff.c) */
#endif

/* The height of the edge (X1, Z1)-(X2, Z2) at xc (section 10): returns 0 when the edge is skipped (a vertical edge), else 1 with *ez. `form` 0 is
 * the original's u normalisation (least m with |u| >> m <= 0x7FFF, then the signed value shifted by m); form 1 is the other reading (halve the
 * signed value while its magnitude exceeds 0x7FFF), kept for the test that tells the two apart. */
static void cnt(uint32_t *reads, int kind) { if (reads) reads[kind]++; }

static int edge_height(uint32_t *reads, int32_t X1, int32_t Z1, int32_t X2, int32_t Z2, int32_t xc, int form, int32_t *ez_out) {
    int32_t dx = X2 - X1, dz = Z2 - Z1;
    if ((dx >> 8) == 0) return 0;
    if (iabs32(dx >> 8) > 0x7FFF) {   /* halve until the magnitude of dx >> 8, shifted, fits */
        int sh = 0;
        while ((iabs32(dx >> 8) >> sh) > 0x7FFF) sh++;
        dx >>= sh; dz >>= sh;
    }
    int32_t adx = iabs32(dx), adz = iabs32(dz), num, den;
    int e;
    if (adx >= 4 * adz)      { num = wrap32((int64_t)dz * 64); den = dx >> 10; e = -16; }
    else if (adx >= 2 * adz) { num = wrap32((int64_t)dz * 64); den = dx >> 9;  e = -15; }
    else if (adx >= adz)     { num = wrap32((int64_t)dz * 64); den = dx >> 8;  e = -14; }
    else if (2 * adx >= adz) { num = wrap32((int64_t)dz * 32); den = dx >> 8;  e = -13; }
    else if (4 * adx >= adz) { num = wrap32((int64_t)dz * 16); den = dx >> 8;  e = -12; }
    else if (8 * adx >= adz) { num = wrap32((int64_t)dz * 8);  den = dx >> 8;  e = -11; }
    else if (16 * adx >= adz){ num = wrap32((int64_t)dz * 4);  den = dx >> 8;  e = -10; }
    else                     { num = dz;                       den = dx >> 8;  e = -8; }
    int16_t d16 = w16(den);
    int32_t slope;
    if (d16 == 0) {
        /* GUARD: a zero divisor (the original's DIVS would trap). The vertices are multiples of 4096 so |den| is at least 4 for every edge
         * of the extracted cell data and this is never reached with them; counted, and the slope is taken as the low word of num. */
        cnt(reads, FL_GUARD_EDGE_DEN);
        slope = w16(num);
    } else {
        int64_t q = (int64_t)num / d16;   /* 64 bits: INT32_MIN / -1 must not overflow in C */
        slope = (q >= -32768 && q <= 32767) ? (int32_t)q : w16(num);   /* overflow leaves the low word of num */
    }
    int32_t u = xc - X1;
    int m = 0;
    if (form == 0) { while ((iabs32(u) >> m) > 0x7FFF) m++; u >>= m; }
    else { while (iabs32(u) > 0x7FFF) { u >>= 1; m++; } }
    e += m;
    int32_t prod = (int32_t)w16(u) * slope;
    prod = e < 0 ? (prod >> -e) : wrap32((int64_t)prod * ((int64_t)1 << e));
    *ez_out = fl_add(Z1, prod);
    return 1;
}

int flight_terrain_edge_height(int32_t X1, int32_t Z1, int32_t X2, int32_t Z2, int32_t xc, int form, int32_t *ez) {
    return edge_height(NULL, X1, Z1, X2, Z2, xc, form, ez);
}

/* Water: ray toward +z through the polygons of the cell record. Returns 1 when the point is in the water.
 * The grid is indexed as the original does (note 05 17.10, correction C19): the column is the unsigned byte (x >> 24) & 0xFF and the row (z >> 24) & 0xFF, the
 * word index col + 32 * row. For x >= 0 the column is 0..31; a negative x gives columns 128..255, which are cells inside the extracted words (rows 4 to 7 of the
 * grid and, with z of 25 cells or more, the first words of the cell records read as grid words: unreachable, since the position update keeps z at 0 while x is
 * negative). The walk below leaves the extracted words only for a malformed table, which the extracted data is not (tests/test_flight.c walks all of it); each
 * such exit is counted as FL_GUARD_TERRAIN_DATA. */
static int terrain_water(uint32_t *reads, const flight_tables_t *t, int32_t x, int32_t z) {
    int col = (x >> 24) & 0xFF, row = (z >> 24) & 0xFF;
    int gi = col + 32 * row;
    if (gi >= FLIGHT_TERRAIN_WORDS) { cnt(reads, FL_GUARD_TERRAIN_DATA); return 0; }
    int32_t off = t->terrain[gi];
    if (off <= 0) return 0;
    int32_t wi = off / 2;
    if (wi + 2 >= FLIGHT_TERRAIN_WORDS) { cnt(reads, FL_GUARD_TERRAIN_DATA); return 0; }
    if (t->terrain[wi] < 0) return 0;
    wi += 2;   /* two header words */
    int32_t xc = x & 0xFFFFFF, zc = z & 0xFFFFFF;
    for (;;) {
        if (wi >= FLIGHT_TERRAIN_WORDS) { cnt(reads, FL_GUARD_TERRAIN_DATA); return 0; }
        int n = t->terrain[wi++];
        if (n == -1) return 0;
        if (n <= 0) { if (wi >= FLIGHT_TERRAIN_WORDS) { cnt(reads, FL_GUARD_TERRAIN_DATA); return 0; } n = t->terrain[wi++]; }
        if (n <= 0) continue;
        if (wi + 2 * n > FLIGHT_TERRAIN_WORDS) { cnt(reads, FL_GUARD_TERRAIN_DATA); return 0; }
        const int16_t *v = &t->terrain[wi];
        wi += 2 * n;
        int count = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1 == n) ? 0 : i + 1;
            int32_t X1 = (int32_t)v[2 * i] * 4096, Z1 = (int32_t)v[2 * i + 1] * 4096;
            int32_t X2 = (int32_t)v[2 * j] * 4096, Z2 = (int32_t)v[2 * j + 1] * 4096;
            int32_t ez;
            if (!edge_height(reads, X1, Z1, X2, Z2, xc, FL_UFORM, &ez)) continue;
            if (ez < zc) continue;
            if (iabs32(xc - X1) < 0x300 || iabs32(xc - X2) < 0x300) return 0;
            if (Z1 == Z2) {
                int32_t lo = X1 < X2 ? X1 : X2, hi = X1 < X2 ? X2 : X1;
                if (lo < xc && xc < hi) count++;
            } else {
                int32_t lo = Z1 < Z2 ? Z1 : Z2, hi = Z1 < Z2 ? Z2 : Z1;
                if (lo < ez && ez < hi) count++;
            }
        }
        if (count & 1) return 1;
    }
}

/* Airfield pads: the player is on a pad when it lies in one of the quads of a candidate entry. */
static int terrain_pad(const flight_tables_t *t, int32_t x, int32_t z) {
    for (int e = 0; e < 5; e++) {
        const int16_t *ent = t->pad_entry[e];
        if (ent[1] == -1) break;
        if (ent[1] < 0 || e >= 4) continue;
        int d = ent[3];
        if (d < 0 || d > 15) continue;
        int32_t rx = wrap32((int64_t)ent[1] * 16777216 + (int64_t)t->dir[2 * d] * 1024 + (int64_t)ent[4] * 1024);
        int32_t rz = wrap32((int64_t)ent[2] * 16777216 + (int64_t)t->dir[2 * d + 1] * 1024 + (int64_t)ent[5] * 1024);
        if (iabs32(fl_sub(rx, x)) > 0x180000 || iabs32(fl_sub(rz, z) >> 8) > 0x180000) continue;   /* the second test is weaker than it looks (note) */
        const int16_t *q = t->pad_quads[e];
        int nw = t->pad_nquad_words[e];
        for (int base = 0; base + 8 <= nw && q[base] != -1; base += 8) {
            int inside = 1;
            for (int k = 0; k < 4 && inside; k++) {
                int kn = (k + 1) & 3;
                int16_t xk = q[base + 2 * k], zk = q[base + 2 * k + 1];
                int16_t xn = q[base + 2 * kn], zn = q[base + 2 * kn + 1];
                int16_t ex = w16(xn - xk), ez = w16(-(zn - zk));
                int16_t vx = w16(fl_sub(fl_add((int32_t)xk * 256, rx), x) >> 8), vz = w16(fl_sub(fl_add((int32_t)zk * 256, rz), z) >> 8);
                if ((int32_t)ez * vx + (int32_t)ex * vz < 0) inside = 0;
            }
            if (inside) return 1;
        }
    }
    return 0;
}

int32_t flight_terrain_query(const flight_tables_t *t, int32_t x, int32_t z, uint8_t *b4, uint32_t *reads) {
    int w = terrain_water(reads, t, x, z);
    int p = terrain_pad(t, x, z);
    *b4 = (uint8_t)((*b4 & ~0x06) | (w ? 0x02 : 0) | (p ? 0x04 : 0));
    return (w ? 2 : 0) | (p ? 4 : 0);
}

int32_t fl_terrain_query(flight_state_t *f, const flight_tables_t *t, int32_t x, int32_t z, uint8_t *b4) {
    return flight_terrain_query(t, x, z, b4, f->reads);
}

/* ---- section 9: the ship lists ------------------------------------------------------------- */

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

#define MAX_GROUPS 8
#define MAX_PTRS 8
typedef struct { int ngroups; int n[MAX_GROUPS]; uint32_t ptr[MAX_GROUPS][MAX_PTRS]; } plist_t;

/* A pointer list: longs, each group closed by the word 0xFFFF, the list by a second 0xFFFF (or by the end of the bytes read). */
static void plist_parse(const uint8_t *l, int len, plist_t *out) {
    memset(out, 0, sizeof *out);
    int pos = 0, g = 0;
    while (pos + 2 <= len && g < MAX_GROUPS) {
        if (be16(l + pos) == 0xFFFF) {
            pos += 2;
            if (out->n[g] == 0) break;
            g++;
            continue;
        }
        if (pos + 4 > len) break;
        if (out->n[g] < MAX_PTRS) out->ptr[g][out->n[g]++] = be32(l + pos);
        pos += 4;
    }
    out->ngroups = (g < MAX_GROUPS && out->n[g] > 0) ? g + 1 : g;
}

typedef struct {
    const flight_tables_t *t;
    int k;                    /* 0 carrier lists, 1 other ship lists */
    const flight_ship_t *s;
    int lc;                   /* the ship's L */
} shipctx_t;

static const uint8_t *st_at(const shipctx_t *c, uint32_t addr, int need) {
    uint32_t base = c->k == 0 ? FLIGHT_CARRIER_STRUCT_BASE : FLIGHT_SHIP2_STRUCT_BASE;
    uint32_t size = c->k == 0 ? 684u : 362u;
    if (addr < base || addr - base + (uint32_t)need > size) return NULL;
    return c->t->ship_st[c->k] + (addr - base);
}

static void ship_vertex(const shipctx_t *c, int off, int16_t v[3]) {
    int slot = off / 6;
    if (off < 0 || slot >= FLIGHT_SHIP_SLOTS) { v[0] = v[1] = v[2] = 0; return; }
    v[0] = c->s->vtx[slot][0]; v[1] = c->s->vtx[slot][1]; v[2] = c->s->vtx[slot][2];
}

/* The point P (words) against the planes of one volume: every plane must have (P - V0) . n < 0. */
static int volume_hit(const shipctx_t *c, const uint32_t *planes, int nplanes, const int16_t P[3]) {
    if (nplanes <= 0) return 0;
    for (int i = 0; i < nplanes; i++) {
        const uint8_t *r = st_at(c, planes[i], 8);
        if (!r) return 0;
        int16_t a[3], b[3], d[3];
        ship_vertex(c, (int16_t)be16(r + 2), d);
        ship_vertex(c, (int16_t)be16(r + 4), a);
        ship_vertex(c, (int16_t)be16(r + 6) & 0xFFF, b);
        int16_t ea[3], eb[3];
        for (int k = 0; k < 3; k++) { ea[k] = w16(a[k] - d[k]); eb[k] = w16(b[k] - d[k]); }
        int sh = 7 + c->lc;
        int16_t n[3];
        n[0] = w16(wrap32((int64_t)ea[1] * eb[2] - (int64_t)ea[2] * eb[1]) >> sh);
        n[1] = w16(wrap32((int64_t)ea[2] * eb[0] - (int64_t)ea[0] * eb[2]) >> sh);
        n[2] = w16(wrap32((int64_t)ea[0] * eb[1] - (int64_t)ea[1] * eb[0]) >> sh);
        int16_t v0[3] = { w16((d[0] >> c->lc) + c->s->fine_x), w16((d[1] >> c->lc) + c->s->alt8), w16((d[2] >> c->lc) + c->s->fine_z) };
        int32_t dot = wrap32((int64_t)w16(P[0] - v0[0]) * n[0] + (int64_t)w16(P[1] - v0[1]) * n[1] + (int64_t)w16(P[2] - v0[2]) * n[2]);
        if (dot >= 0) return 0;
    }
    return 1;
}

/* y word of the vertex named by word 1 of the first structure of a group, >> L */
static int group_y(const shipctx_t *c, const plist_t *pl, int g, int *ok) {
    const uint8_t *s = st_at(c, pl->ptr[g][0], 4);
    *ok = s != NULL;
    if (!s) return 0;
    int16_t v[3];
    ship_vertex(c, (int16_t)be16(s + 2), v);
    return v[1] >> c->lc;
}

/* Deck polygons of S0 (note 05 17.11, "The deck polygon list"): the array starts at its first word, which is at the same time the byte offset of the
 * reference vertex and the first vertex offset of the first polygon; a polygon is a run of vertex byte offsets whose last word has bit 15 set; the list ends at
 * a word with bit 15 set where a polygon would start (0xFFFF; vertex offset 0 is a valid vertex, so 0 does not end it). The limit (the next structure of the
 * lists or the end of the block) is a bound for a malformed table only. */
static int deck_contains(const shipctx_t *c, uint32_t s0, const int16_t P[3], uint32_t limit) {
    uint32_t a = s0;   /* note 05 17.11: the first word is both the reference vertex offset and the first vertex of the first polygon */
    for (;;) {
        const uint8_t *p = st_at(c, a, 2);
        if (!p || a + 2 > limit) return 0;
        uint16_t w = be16(p);
        if (w == 0xFFFF) return 0;
        int16_t vx[16], vz[16];
        int n = 0, last = 0;
        while (!last) {
            p = st_at(c, a, 2);
            if (!p || a + 2 > limit || n >= 16) return 0;
            uint16_t o = be16(p);
            last = (o & 0x8000) != 0;
            int16_t v[3];
            ship_vertex(c, o & 0x7FFF, v);
            vx[n] = v[0]; vz[n] = v[2]; n++;
            a += 2;
        }
        int inside = n >= 3;
        for (int i = 0; i < n && inside; i++) {
            int j = (i + 1 == n) ? 0 : i + 1;
            int16_t pax = w16(P[0] - w16((vx[i] >> c->lc) + c->s->fine_x));
            int16_t paz = w16(P[2] - w16((vz[i] >> c->lc) + c->s->fine_z));
            int16_t nx = w16(-(vz[j] - vz[i])), nz = w16(vx[j] - vx[i]);
            if ((int32_t)pax * nx + (int32_t)paz * nz >= 0) inside = 0;
        }
        if (inside) return 1;
    }
}

/* the arrestor corridor tests, a, b, c = the ship's slots */
static int corridor_ok(const shipctx_t *c, const flight_state_t *f, const int16_t P[3], int sa, int sb, int sc) {
    int16_t a[3], b[3], cc[3];
    ship_vertex(c, sa * 6, a); ship_vertex(c, sb * 6, b); ship_vertex(c, sc * 6, cc);
    int16_t q[3] = { w16(a[0] + ((a[0] - cc[0]) >> 1)), 0, w16(a[2] + ((a[2] - cc[2]) >> 1)) };
    #define UX(v) w16(((v)[0] >> c->lc) + c->s->fine_x - P[0])
    #define UZ(v) w16(((v)[2] >> c->lc) + c->s->fine_z - P[2])
    #define T(ex, ez, ux, uz) ((int32_t)(ez) * (ux) + (int32_t)(ex) * (uz))
    int16_t eac_x = w16(a[0] - cc[0]), eac_z = w16(-(a[2] - cc[2]));
    int16_t ebc_x = w16(b[0] - cc[0]), ebc_z = w16(-(b[2] - cc[2]));
    int ok = 1;
    ok = ok && T(eac_x, eac_z, UX(cc), UZ(cc)) >= 0;
    ok = ok && T(eac_x, eac_z, UX(b), UZ(b)) < 0;
    ok = ok && T(ebc_x, ebc_z, UX(cc), UZ(cc)) < 0;
    ok = ok && T(ebc_x, ebc_z, UX(q), UZ(q)) >= 0;
    #undef UX
    #undef UZ
    #undef T
    /* the nose direction agrees with the ship's: +0x96 and +0xA2 are matrix words 2 and 8 */
    int32_t dir = wrap32((int64_t)f->mat_b[2] * c->s->mat_b[2] + (int64_t)f->mat_b[8] * c->s->mat_b[8]);
    ok = ok && dir >= 0xE000000;
    return ok;
}

/* ---- section 9: the deck, hull and arrestor tests for one ship ----------------------------- */

static const int16_t zero3[3] = { 0, 0, 0 };
/* Vertex slot k (0 to 3) of the mover: the record's own +0xA4 + 6k as its last draw left them. The player's are vtx (written by the game's copy of the draw store, game.c; tests/player_draw.h stands in for the draw in the tests); a non-player record's are slot
 * (copied from the display lists by game.c, or poked by a rig): the ship test, the landmark phase and the ground test of 0x2B5E2 all read the mover's record (note 05 17.11, 11b;
 * note 07 10.2). */
static const int16_t *mslot(const flight_state_t *f, int k) { return f->entity ? f->slot[k] : f->vtx[k]; }

static int ship_scan(flight_state_t *f, const flight_env_t *env, const flight_ship_t *s, const int32_t prev[3]) {
    shipctx_t c = { env->tab, (s->cls == 0x20) ? 0 : 1, s, s->l7d & 15 };
    plist_t l1, l2;
    plist_parse(env->tab->ship_l1[c.k], c.k == 0 ? 124 : 32, &l1);
    plist_parse(env->tab->ship_l2[c.k], 44, &l2);
    if (l1.ngroups < 1 || l1.n[0] < 1) return 0;
    int lp = f->l7d & 15;
    /* Note 07 10.2 (this reconciles note 05 17.11): a mover that is not the viewed record gets ONE test at its bare previous position, no vertex term and no second pass; the viewed mover (the player in
     * play, or the seat or a missile while the camera is on it) adds its OWN vertex slot 0 shifted by its unit shift on pass 0 and its own slot 3 on pass 1. The pass count is a local counter of the
     * scan (started at 0 for every ship hit), not a field of any record. */
    const int viewed = f->entity ? f->view_off == f->self_off : f->v4a996 == 0;
    for (int pass = 0; pass < (viewed ? 2 : 1); pass++) {
        const int16_t *vv = zero3;
        if (viewed) vv = mslot(f, pass == 0 ? 0 : 3);
        int16_t P[3] = { w16(((prev[0] & 0x3FFFFF) >> 8) + (vv[0] >> lp)),
                         w16((prev[1] >> 8) + (vv[1] >> lp)),
                         w16(((prev[2] & 0x3FFFFF) >> 8) + (vv[2] >> lp)) };
        const uint8_t *s0p = st_at(&c, l1.ptr[0][0], 2);
        if (!s0p) return 0;
        int refoff = (int16_t)be16(s0p);
        int16_t rv[3];
        ship_vertex(&c, refoff, rv);
        int refy = rv[1] >> c.lc;
        if (P[1] < refy) {   /* below deck level: the hull volumes */
            if (volume_hit(&c, &l1.ptr[0][1], l1.n[0] - 1, P)) return 0x20;
            for (int g = 1; g < l1.ngroups; g++)
                if (volume_hit(&c, &l1.ptr[g][0], l1.n[g], P)) return 0x20;
            return 0;   /* note 05 17.11: no hit ends the scan of this entity (no polygon test, no deck bit, no second list, no pass 1) */
        }
        /* the limit of S0: the lowest structure address above it among the list pointers */
        uint32_t base = c.k == 0 ? FLIGHT_CARRIER_STRUCT_BASE : FLIGHT_SHIP2_STRUCT_BASE;
        uint32_t limit = base + (c.k == 0 ? 684u : 362u);
        for (int g = 0; g < l1.ngroups; g++)
            for (int i = 0; i < l1.n[g]; i++)
                if (l1.ptr[g][i] > l1.ptr[0][0] && l1.ptr[g][i] < limit) limit = l1.ptr[g][i];
        if (!deck_contains(&c, l1.ptr[0][0], P, limit)) {
            continue;
        }
        f->b4 |= pass == 0 ? 0x40 : 0x80;
        f->deck = (int16_t)refy;
        if (pass == 1) {   /* the arrestor corridor (note 05 17.11): with the wheels up the tests count as failed; a failed test clears W2 0x4000
                            * unless the hook is down (0x8000) and the bit was already set; pass 0 does not touch the bit */
            int ok = 0;
            if (f->w2 & 0x0080) {
                int sa = c.k == 0 ? 54 : 44;
                ok = corridor_ok(&c, f, P, sa, sa + 1, sa + 2);
            }
            if (ok) f->w2 |= 0x4000;
            else if (!((f->w2 & 0x8000) && (f->w2 & 0x4000))) f->w2 &= (uint16_t)~0x4000;
        }
        if (l2.ngroups >= 1 && l2.n[0] >= 1) {   /* note 05 17.11: the second list is tested in both passes, after the deck bit and the corridor */
            int okk;
            int y1 = group_y(&c, &l2, 0, &okk);
            int yp = (int16_t)f->alt8;
            if (okk && yp < y1) {
                if (volume_hit(&c, &l2.ptr[0][1], l2.n[0] - 1, P)) return 0x20;
            } else if (l2.ngroups >= 2) {
                int y2 = group_y(&c, &l2, 1, &okk) + 7;
                if (okk && yp < y2 && volume_hit(&c, &l2.ptr[1][1], l2.n[1] - 1, P)) return 0x20;
            }
        }
    }
    return 0;
}

/* The big-hull test 0x2B3F0 of the gun (note 07 8.2, "Models whose flag byte has bit 4"; V: tests/rig/test_ship_diff.c, part bighull): the tracer point P (x and z the words within the tracer's cell, y the LONG
 * height, all in fine units) against the volumes of the record `s` (the ship lists by its class byte: 0x20 the carrier's, any other the second ship's), read from the record's vertex slots. The tracer's
 * ABSOLUTE height is compared with the RECORD-RELATIVE height of the reference vertex of the first list, as words with the reference shifted right by the unit shift: a lower tracer is tested against
 * the volumes of the first list (the structures after its reference, then each later group as one volume). Otherwise the second list decides, with the references as they stand (not shifted) against the
 * LONG height: the first group's volume when its reference is at or above the tracer, else the second group's when ITS reference is. Inside = every plane's dot product negative; a zero normal (a slot
 * that holds nothing) is outside. Returns 1 for a hit. */
int fl_bighull_hit(const flight_env_t *env, const flight_ship_t *s, const int32_t P[3]) {
    if (!env->tab) return 0;
    shipctx_t c = { env->tab, (s->cls == 0x20) ? 0 : 1, s, s->l7d & 15 };
    plist_t l1, l2;
    plist_parse(env->tab->ship_l1[c.k], c.k == 0 ? 124 : 32, &l1);
    plist_parse(env->tab->ship_l2[c.k], 44, &l2);
    if (l1.ngroups < 1 || l1.n[0] < 1) return 0;
    const int16_t W[3] = { w16(P[0]), w16(P[1]), w16(P[2]) };
    const uint8_t *s0p = st_at(&c, l1.ptr[0][0], 4);
    if (!s0p) return 0;
    int16_t rv[3];
    ship_vertex(&c, (int16_t)be16(s0p + 2), rv);   /* the word at +2 of the first structure (the deck test of the contact scan reads its word +0) */
    if (W[1] < (rv[1] >> c.lc)) {
        if (volume_hit(&c, &l1.ptr[0][1], l1.n[0] - 1, W)) return 1;
        for (int g = 1; g < l1.ngroups; g++)
            if (volume_hit(&c, &l1.ptr[g][0], l1.n[g], W)) return 1;
        return 0;
    }
    if (l2.ngroups >= 1 && l2.n[0] >= 1) {
        const uint8_t *r1 = st_at(&c, l2.ptr[0][0], 4);
        int16_t v1[3];
        if (!r1) return 0;
        ship_vertex(&c, (int16_t)be16(r1 + 2), v1);
        if (v1[1] >= P[1]) return volume_hit(&c, &l2.ptr[0][1], l2.n[0] - 1, W);
        if (l2.ngroups >= 2) {
            const uint8_t *r2 = st_at(&c, l2.ptr[1][0], 4);
            int16_t v2[3];
            if (!r2) return 0;
            ship_vertex(&c, (int16_t)be16(r2 + 2), v2);
            if (v2[1] < P[1]) return 0;
            return volume_hit(&c, &l2.ptr[1][1], l2.n[1] - 1, W);
        }
    }
    return 0;
}

/* ---- section 11b: landmarks ----------------------------------------------------------------- */

static int lm_box(const int16_t box[5], int X, int Y, int Z) {
    return Y <= box[0] && X > box[1] && X < box[2] && Z > box[3] && Z < box[4];
}

static int landmark_phase(flight_state_t *f, const flight_env_t *env) {
    if ((f->cls & 0xF0) != 0x10 || f->alt8 > 0x7FFF) return 0;
    int lp = f->l7d & 15;
    for (int s = 0; s < env->n_landmarks && s < FLIGHT_LM_SLOTS; s++) {
        const flight_lm_slot_t *sl = &env->landmark[s];
        int dx = (int8_t)(uint8_t)((uint8_t)f->cell_x - sl->cell_x);
        int dz = (int8_t)(uint8_t)((uint8_t)f->cell_z - sl->cell_z);
        if (iabs32(dx) > 1 || iabs32(dz) > 1) continue;
        int16_t X = w16(f->fine_x + dx * 16384);
        int16_t Z = w16(f->fine_z + dz * 16384 + env->landmark_z);
        int16_t Y = (int16_t)f->alt8;
        if (!lm_box(sl->box, X, Y, Z)) continue;
        f->v4a957 = 1;
        for (int n = 0; n < sl->nnodes && n < FLIGHT_LM_NODES; n++) {
            const flight_lm_node_t *nd = &sl->node[n];
            if (!lm_box(nd->box, X, Y, Z)) continue;
            if (!(f->w2 & 0x0002)) return 0;
            for (int v = 0; v < 3; v++) {
                int16_t px = w16(X + (mslot(f, v)[0] >> lp));
                int16_t py = w16(Y + (mslot(f, v)[1] >> lp));
                int16_t pz = w16(Z + (mslot(f, v)[2] >> lp));
                int inside = nd->nplanes > 0;
                for (int p = 0; p < nd->nplanes && p < FLIGHT_LM_PLANES && inside; p++) {
                    int32_t dot = wrap32((int64_t)w16(px - nd->ref[p][0]) * nd->nrm[p][0] +
                                         (int64_t)w16(py - nd->ref[p][1]) * nd->nrm[p][1] +
                                         (int64_t)w16(pz - nd->ref[p][2]) * nd->nrm[p][2]);
                    if (dot >= 0) inside = 0;
                }
                if (inside) return 0x20;
            }
            return 0;   /* all three points outside: the phase ends */
        }
    }
    return 0;
}

/* ---- the ground test (note 05 17.12, "Result 0x10 and the part id +0x7B") ------------------------------------ */

/* The last test of the routine (0x2C08C), live for an aircraft class only. +0x7B is the part id the renderer stored for the entity (flight_state_t.b7b, written by
 * the caller from the draw pass: 0 is the aircraft body, 0xFF the cockpit block). With a non-negative id whose low nibble is 0, vertices 0, 1 and 2 are tested in
 * that order: the rotated vertex y word shifted right by L plus the long +0x10 (y >> 8); the first negative sum returns 0x10. With a negative id or a non-zero
 * nibble only +0x10 itself is tested (negative means below y = 0). */
static int ground_test(const flight_state_t *f) {
    if ((f->cls & 0xF0) == 0) return f->alt8 < 0 ? 0x10 : 0;   /* RIG (tests/rig/test_weapons_diff.c): a missile (class nibble 0) strikes the ground when its height is negative */
    if ((f->cls & 0xF0) != 0x10) return 0;
    if (f->b7b < 0x80 && (f->b7b & 0x0F) == 0) {
        const int lp = f->l7d & 15;
        for (int v = 0; v < 3; v++)
            if ((int32_t)(mslot(f, v)[1] >> lp) + f->alt8 < 0) return 0x10;
    }
    return f->alt8 < 0 ? 0x10 : 0;
}

/* ---- the collision routine ------------------------------------------------------------------ */

/* One ship record against the mover (note 07 10.2 ship row, note 05 17.11): the conditions of the contact scan, the box test (half-size 0xA0000 on every axis, the bound inclusive)
 * against the mover's previous position, the proximity flag, then the deck and hull tests. Returns 0x20 on a hull hit. The carrier of the environment (record 14) goes through here
 * from fl_collide, any other ship record (class nibble 0x20 in the records 1 to 13 and 15) from the contact scan of weapons.c. */
int fl_ship_contact(flight_state_t *f, const flight_env_t *env, const flight_ship_t *s, const int32_t prev[3]) {
    if (!(s->present && env->tab && (s->w0 & 0x40) && !(s->w0 & 0x600) && s->group != f->group && ((s->w0 | f->w0) & 0x08) && (s->cls & 0xF0) == 0x20)) return 0;
    if (!(iabs32(fl_sub(prev[0], s->pos[0])) <= 0xA0000 && iabs32(fl_sub(prev[1], s->pos[1])) <= 0xA0000 && iabs32(fl_sub(prev[2], s->pos[2])) <= 0xA0000)) return 0;
    f->v4a957 = 1;
    return ship_scan(f, env, s, prev);
}

int fl_collide(flight_state_t *f, const flight_env_t *env, const int32_t prev[3]) {
    f->b4 &= (uint8_t)~0xC0;   /* both deck bits are recomputed every step (note 05 record table, +0x04) */
    if (env->contact) {   /* note 07 10.2: the records after the mover, in index order, before the carrier (record 14) */
        const int r0 = env->contact(env->contact_user, f, prev, 0);
        if (r0) return r0;
    }
    {
        const int r = fl_ship_contact(f, env, &env->ship, prev);
        if (r) return r;
    }
    if (env->contact) {   /* the record 15, the second ship, after the carrier */
        const int r1 = env->contact(env->contact_user, f, prev, 1);
        if (r1) return r1;
    }
    int r = landmark_phase(f, env);
    return r ? r : ground_test(f);
}
