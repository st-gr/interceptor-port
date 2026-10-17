/* src/core/sky.c - the sky polygon, the zone flags, the blackout strain and the sea mesh pass (docs/re/04-scene-composition.md). */
#include <string.h>
#include "sky.h"
#include "assets.h"

static int16_t w16(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v & 0xFFFFu); }
static int iabs(int v) { return v < 0 ? -v : v; }

/* ---- the tiny-polygon inputs ------------------------------------------------------------------------------------------------------------------ */

static int special_frame(uint32_t frame) { unsigned f = frame & 31u; return f >= 18 && f <= 20; }

int sky_tiny_pen(sky_tiny_view_t view, int rear_age, uint32_t frame)
{
    if (view == SKY_TINY_MAP) return 3;
    if (view == SKY_TINY_COCKPIT && rear_age >= 0) {
        if (rear_age < 11) return 3;
        if (rear_age < 15) return 0;
    }
    if (view == SKY_TINY_EXTERNAL) return special_frame(frame) ? 4 : 0;
    return special_frame(frame) ? 0 : 13;
}

int sky_tiny_mask(sky_tiny_view_t view, int polygon_built, uint32_t frame)
{
    if (polygon_built) return 1;
    return (view == SKY_TINY_COCKPIT && !special_frame(frame)) ? 7 : 0xF;
}

/* ---- the signed 16-bit divide with the correction of note 03 (fx_clip) --------------------------------------------------------------------- */

/* DIVS: a 32-bit dividend by a word. When the quotient does not fit a word the instruction leaves the register unchanged: the "quotient" the code then
 * reads is the low word of the dividend and the "remainder" its high word. The correction adds 1 (quotient zero or positive) or -1 (negative) when
 * |remainder| >= floor(|divisor| / 2). The result is a word. d must not be 0.
 * NOTE-GAP: the edge cases the notes do not state and no state reaches (0 in 200,000 random rotations, note 04): d = -32768 (the original's absolute value of the
 * divisor and the half-divisor compare are not specified; the port takes |d| = 32768) and a remainder word of -32768 after an overflowed divide (|r| = 32768). */
static int32_t sdiv_fix(sky_state_t *st, int32_t num, int16_t d)
{
    int32_t q = num / d, r = num % d;
    int16_t qw, rw;
    int ar, ad;
    int32_t res;
    if (q > 32767 || q < -32768) {
        qw = w16(num); rw = w16(num >> 16);
        if (st) st->div_overflows++;
    } else {
        qw = (int16_t)q; rw = (int16_t)r;
    }
    ar = iabs(rw); ad = iabs(d);
    res = qw;
    if (ar >= ad / 2) res += res >= 0 ? 1 : -1;
    return w16(res);
}

/* ---- the sky polygon ------------------------------------------------------------------------------------------------------------------------- */

static const int16_t CORNER_X[4] = { 10240, -11008, -10240, 9728 };
static const int16_t CORNER_Z[4] = { -9216, -9728, 9216, 11008 };

typedef struct {
    sky_state_t *st;
    sky_poly_t *out;
    int16_t pt[8][2];     /* the screen point stored per slot */
    int abort;
} sk_t;

/* The plane's coordinate g (planes: 0 y = z, 1 x = z, 2 -y = z, 3 -x = z). */
static int16_t plane_g(int p, const int16_t c[3])
{
    switch (p) {
    case 0: return c[1];
    case 1: return c[0];
    case 2: return w16(-c[1]);
    default: return w16(-c[0]);
    }
}

static int clamp_i(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int half_up(int q) { return (q >> 1) + (q & 1); }   /* arithmetic shift right by 1, plus the bit shifted out */

/* Intersection of the segment O to C with plane p. A hit (a valid point) counts the plane, stores the slot and the screen point and returns 1. */
static int try_plane(sk_t *k, int p, const int16_t O[3], const int16_t C[3], int slot)
{
    const int own = (p == 0 || p == 2) ? 1 : 0;   /* the coordinate the plane fixes */
    int16_t fO = w16(O[2] - plane_g(p, O)), fC = w16(C[2] - plane_g(p, C));
    int16_t D = w16(fO - fC);
    int16_t r[3];
    int c;
    if (D == 0) return 0;     /* no point; the stored candidate is not changed */
    for (c = 0; c < 3; c++) {
        int16_t dc;
        if (c == own) continue;
        dc = w16(O[c] - C[c]);
        r[c] = w16(O[c] - sdiv_fix(k->st, (int32_t)dc * fO, D));
    }
    r[own] = (p == 0 || p == 1) ? r[2] : w16(-r[2]);
    k->st->cand[0] = r[0]; k->st->cand[1] = r[1]; k->st->cand[2] = r[2];
    if (r[2] < 0 || iabs(r[0]) > r[2] || iabs(r[1]) > r[2]) return 0;
    if (r[2] == 0) {          /* the point (0, 0, 0): the projection of the original branches to itself on z <= 0 */
        k->st->z0_rejects++;
        k->abort = 1;
        return 1;
    }
    {
        int qx = (r[0] * 320) / r[2], qy = (r[1] * 180) / r[2];
        int sx = clamp_i(160 + half_up(qx), 0, 319), sy = clamp_i(90 + half_up(qy), 0, 179);
        k->pt[slot][0] = (int16_t)(319 - sx);
        k->pt[slot][1] = (int16_t)(179 - sy);
    }
    k->out->cnt[p]++;
    k->out->slot[p] = (uint8_t)slot;
    return 1;
}

/* One slot of the crossing search; the table of note 04 "The crossing search". *inside is set when C is inside the pyramid (an odd slot then skips the
 * next slot). */
static void slot_decide(sk_t *k, int s, const int16_t C[3], const int16_t O[3], int *inside)
{
    const int16_t x = C[0], y = C[1], z = C[2], xo = O[0], yo = O[1], zo = O[2];
    const int16_t x1 = w16(x + 1), y1 = w16(y + 1), nx1 = w16(-x1), ny1 = w16(-y1), nxo = w16(-xo), nyo = w16(-yo);
    sky_state_t *st = k->st;
    (void)x; (void)y;
    *inside = 0;
    if (x1 >= z) {                                     /* X-high */
        if (zo <= xo) return;
        if (try_plane(k, 1, O, C, s)) return;
        if (st->cand[2] >= 0 || zo <= nxo) goto Y;
        if (try_plane(k, 3, O, C, s)) return;
        goto Y;
    } else if (nx1 >= z) {                             /* X-low */
        if (zo <= nxo) return;
        if (try_plane(k, 3, O, C, s)) return;
        if (st->cand[2] >= 0 || zo <= xo) goto Y;
        if (try_plane(k, 1, O, C, s)) return;
        goto Y;
    } else if (y1 >= z) {                              /* Y-high */
        if (zo <= yo) return;
        if (try_plane(k, 0, O, C, s)) return;
        if (st->cand[2] >= 0 || zo <= nyo) goto X2;
        if (try_plane(k, 2, O, C, s)) return;
        goto X2;
    } else if (ny1 >= z) {                             /* Y-low */
        if (zo <= nyo) return;
        if (try_plane(k, 2, O, C, s)) return;
        if (st->cand[2] >= 0 || zo <= yo) goto X2;
        if (try_plane(k, 0, O, C, s)) return;
        goto X2;
    } else {
        if (z < 0) return;
        *inside = 1;
        return;
    }
Y:
    if (y1 < 0) {
        if (ny1 < z || zo <= nyo) return;
        if (try_plane(k, 2, O, C, s)) return;
        if (st->cand[2] >= 0 || y1 < z || zo <= yo) return;
        (void)try_plane(k, 0, O, C, s);
    } else {
        if (y1 < z || zo <= yo) return;
        if (try_plane(k, 0, O, C, s)) return;
        if (st->cand[2] >= 0 || ny1 < z || zo <= nyo) return;
        (void)try_plane(k, 2, O, C, s);
    }
    return;
X2:
    if (x1 < 0) {
        if (nx1 < z || zo <= nxo) return;
        if (try_plane(k, 3, O, C, s)) return;
        if (st->cand[2] >= 0 || x1 < z || zo <= xo) return;
        (void)try_plane(k, 1, O, C, s);
    } else {
        if (x1 < z || zo <= xo) return;
        if (try_plane(k, 1, O, C, s)) return;
        if (st->cand[2] >= 0 || nx1 < z || zo <= nxo) return;
        (void)try_plane(k, 3, O, C, s);
    }
}

static int32_t corner_sum(const int16_t m[3], int32_t x, int32_t y, int32_t z)
{
    return (int32_t)m[0] * x + (int32_t)m[1] * y + (int32_t)m[2] * z;
}
/* >> 8 with round half up: the last bit shifted out is added; stored as a word. */
static int16_t round8(int32_t s) { return w16((s >> 8) + ((s >> 7) & 1)); }

int sky_polygon(sky_state_t *st, const int16_t m[9], int32_t cam_y, int cockpit, int inverted, int pitch_word, int roll_word, sky_poly_t *out)
{
    static const int16_t TL[2] = { 0, 0 }, TR[2] = { 319, 0 }, BR[2] = { 319, 179 }, BL[2] = { 0, 179 };
    sky_poly_t local;
    sk_t k;
    int16_t cc[4][3];
    int16_t gy = w16((cam_y >> 16) >> 2);
    int i, s, skip = 0, inv1, n = 0;
    const int16_t *v[SKY_MAX_VERTS];
    int c0, c1, c2, c3;
    sky_state_t dummy;

    if (!out) out = &local;
    if (!st) { memset(&dummy, 0, sizeof dummy); st = &dummy; }
    memset(out, 0, sizeof *out);
    memset(&k, 0, sizeof k);
    k.st = st; k.out = out;
    for (i = 0; i < 4; i++) {
        cc[i][0] = round8(corner_sum(m, CORNER_X[i], gy, CORNER_Z[i]));
        cc[i][1] = round8(corner_sum(m + 3, CORNER_X[i], gy, CORNER_Z[i]));
        cc[i][2] = round8(corner_sum(m + 6, CORNER_X[i], gy, CORNER_Z[i]));
    }
    for (s = 0; s < 8 && !k.abort; s++) {
        const int kk = s >> 1;
        const int16_t *C = (s & 1) ? cc[(kk + 1) & 3] : cc[kk];
        const int16_t *O = (s & 1) ? cc[kk] : cc[(kk + 1) & 3];
        int inside;
        if (skip) { skip = 0; continue; }
        slot_decide(&k, s, C, O, &inside);
        if (inside && (s & 1)) skip = 1;
    }
    if (k.abort) return 0;

    c0 = out->cnt[0] != 0; c1 = out->cnt[1] != 0; c2 = out->cnt[2] != 0; c3 = out->cnt[3] != 0;
    inv1 = cockpit ? inverted : 0;
    {
        const int16_t *P0 = k.pt[out->slot[0]], *P1 = k.pt[out->slot[1]], *P2 = k.pt[out->slot[2]], *P3 = k.pt[out->slot[3]];
        if (c1 && c3) {
            if (!inv1) { v[0] = P1; v[1] = P3; v[2] = TR; v[3] = TL; n = 4; }
            else       { v[0] = P1; v[1] = P3; v[2] = BR; v[3] = BL; n = 4; }
        } else if (c1 && c0) {
            if (!inverted) { v[0] = P1; v[1] = P0; v[2] = TL; n = 3; }
            else           { v[0] = P1; v[1] = P0; v[2] = TR; v[3] = BR; v[4] = BL; n = 5; }
        } else if (c1 && c2) {
            if (!inverted) { v[0] = P1; v[1] = P2; v[2] = BR; v[3] = TR; v[4] = TL; n = 5; }
            else           { v[0] = P1; v[1] = P2; v[2] = BL; n = 3; }
        } else if (c1) {
            n = 0;
        } else if (c0 && c3) {
            if (!inverted) { v[0] = P0; v[1] = P3; v[2] = TR; n = 3; }
            else           { v[0] = P0; v[1] = P3; v[2] = BR; v[3] = BL; v[4] = TL; n = 5; }
        } else if (c0 && c2) {
            if (roll_word >= 14400) { v[0] = P0; v[1] = P2; v[2] = BL; v[3] = TL; n = 4; }
            else                    { v[0] = P0; v[1] = P2; v[2] = BR; v[3] = TR; n = 4; }
        } else if (c0) {
            n = 0;
        } else if (!c2) {
            if (pitch_word > 14400) { v[0] = TL; v[1] = TR; v[2] = BR; v[3] = BL; n = 4; out->whole = 1; }
        } else if (c3) {
            if (!inverted) { v[0] = P2; v[1] = P3; v[2] = TR; v[3] = TL; v[4] = BL; n = 5; }
            else           { v[0] = P2; v[1] = P3; v[2] = BR; n = 3; }
        } else {
            n = 0;
        }
    }
    for (i = 0; i < n; i++) { out->xy[2 * i] = v[i][0]; out->xy[2 * i + 1] = v[i][1]; }
    out->n = (uint8_t)n;
    return n;
}

int sky_side_flag(int cockpit, int pitch_word, int roll_word)
{
    int P = pitch_word / 8, R = cockpit ? roll_word / 8 : 0, flag = 0;
    if (P >= 900 && P <= 2699) {
        if (!(R >= 900 && R <= 2699)) flag = 1;
    } else if (R >= 900 && R <= 2700) {
        flag = 1;
    }
    return flag;
}

/* ---- the zone lookup ---------------------------------------------------------------------------------------------------------------------------- */

static int tw(const uint8_t *t, size_t len, size_t at, int *v)   /* big-endian word, signed; 0 when outside */
{
    if (at + 2 > len) return 0;
    *v = (int16_t)(uint16_t)((t[at] << 8) | t[at + 1]);
    return 1;
}

/* A list of words ending with -1 at `at`, followed by one offset word per entry: the offset of `key`, or -1. */
static long list_offset(const uint8_t *t, size_t len, size_t at, int key)
{
    int v, i = 0, found = -1;
    size_t p = at;
    for (;;) {
        if (!tw(t, len, p, &v)) return -1;
        if (v < 0) break;
        if (v == key && found < 0) found = i;
        i++; p += 2;
    }
    if (found < 0) return -1;
    if (!tw(t, len, p + 2 + 2 * (size_t)found, &v)) return -1;
    return v;
}

int sky_zone_lookup(const uint8_t *tab, size_t len, int row, int col, int32_t alt, int phase, int octant)
{
    long off = list_offset(tab, len, 0, row);
    size_t rec;
    if (off < 0) return 0;
    off = list_offset(tab, len, (size_t)off, col);
    if (off < 0) return 0;
    rec = (size_t)off;
    for (;;) {
        int flag;
        int32_t bound;
        size_t q;
        int cmp = phase;
        if (!tw(tab, len, rec, &flag) || flag < 0) return 0;      /* the chain ends */
        if (rec + 6 > len) return 0;
        bound = (int32_t)(((uint32_t)tab[rec + 2] << 24) | ((uint32_t)tab[rec + 3] << 16) | ((uint32_t)tab[rec + 4] << 8) | tab[rec + 5]);
        if (flag == 0) { if (!(alt <= bound)) return 0; }
        else if (!(alt >= bound)) return 0;                        /* a failed bound ends the whole lookup */
        q = rec + 6;
        for (;;) {
            int b;
            if (q >= len) return 0;
            b = (int8_t)tab[q++];
            if (b < 0) break;                                      /* the group list ends */
            if (b == cmp) {
                int hit = 0;
                while (q < len && (int8_t)tab[q] >= 0) { if (tab[q] == octant) hit = 1; q++; }
                if (hit) return 1;
                cmp = octant;                                      /* quirk: the later groups of this record compare against the octant */
            } else {
                while (q < len && (int8_t)tab[q] >= 0) q++;
            }
            q++;                                                   /* the negative terminator of the group */
        }
        rec = (q + 1) & ~(size_t)1;                                /* the next record starts at the next even address */
    }
}

/* ---- the blackout strain -------------------------------------------------------------------------------------------------------------------------- */

void sky_strain_reset(sky_strain_t *s) { s->acc = 0; s->latch = 0; }

void sky_strain_cue(sky_strain_t *s, int e, int coupling)
{
    int v = w16(e + (e >> 3) + coupling) >> 3;
    s->latch = (uint8_t)(iabs(v) >= 78);
}

void sky_strain_second(sky_strain_t *s)
{
    if (s->latch) { if (s->acc < 15) s->acc++; }
    else if (s->acc >= 0) s->acc--;
}

int sky_fade_target(const sky_strain_t *s, int cockpit_own_aircraft)
{
    return (s->acc > 5 && cockpit_own_aircraft) ? 2 : 15;
}

/* ---- the tables ------------------------------------------------------------------------------------------------------------------------------------ */

int sky_tables_load(sky_tables_t *t, const game_loader_t *loader, char *err, size_t errlen)
{
    static const struct { const char *name; size_t off, n; } files[] = {
        { "tables/sea_zone.bin", offsetof(sky_tables_t, zone), SKY_ZONE_BYTES },
        { "tables/sea_zone2.bin", offsetof(sky_tables_t, zone2), SKY_ZONE2_BYTES },
        { "tables/mesh_scan.bin", offsetof(sky_tables_t, scan), MESH_SCAN_BYTES },
        { "tables/mesh_far.bin", offsetof(sky_tables_t, far_tab), MESH_FAR_BYTES },
        { "tables/mesh_cells.bin", offsetof(sky_tables_t, cells), MESH_CELLS_BYTES },
    };
    size_t i;
    for (i = 0; i < sizeof files / sizeof files[0]; i++) {
        if (core_assets_read_exact(loader, files[i].name, (uint8_t *)t + files[i].off, files[i].n) != 0) {
            if (err && errlen) {
                err[0] = '\0';
                core_assets_append(err, errlen, files[i].name);
                core_assets_append(err, errlen, " is missing or has the wrong size (re-run the extractor)");
            }
            return -1;
        }
    }
    return 0;
}

/* ---- the sea mesh pass ------------------------------------------------------------------------------------------------------------------------------ */

#define MB_SCAN 0x2E600u        /* the scan blob's address in the image */
#define MB_CELLS 0x47D7Cu
#define MB_FINE_GRID 0x47F40u

static int bu8(const uint8_t *b, size_t len, size_t at) { return at < len ? b[at] : 0; }
static int bs16(const uint8_t *b, size_t len, size_t at) { return at + 2 <= len ? (int16_t)(uint16_t)((b[at] << 8) | b[at + 1]) : 0; }
static int32_t bs32(const uint8_t *b, size_t len, size_t at)
{
    return at + 4 <= len ? (int32_t)(((uint32_t)b[at] << 24) | ((uint32_t)b[at + 1] << 16) | ((uint32_t)b[at + 2] << 8) | b[at + 3]) : 0;
}

typedef struct {
    const sky_tables_t *t;
    const mesh_in_t *in;
    mesh_poly_t *out;
    int max, count;
    int32_t a;           /* A' */
    int fine;            /* 0 coarse, 1 fine */
    int16_t a_, b_, c_;  /* the height terms */
} mp_t;

/* Appends the polygons of the list at `list` (offset into the cells blob) for the cell origin (ox, oz) in the pass unit and the pre-shift s. */
static void emit_list(mp_t *p, size_t list, int16_t ox, int16_t oz, int s)
{
    const uint8_t *cb = p->t->cells;
    size_t len = MESH_CELLS_BYTES;
    const int16_t *m = p->in->m;
    for (;;) {
        int n = bs16(cb, len, list);
        list += 2;
        if (n == -1) return;                         /* 0xFFFF ends the list */
        if (n <= 0) {                                /* a gate */
            int16_t g = w16((n & 0x7FFF) * 4);
            if (g > p->a) return;
            continue;
        }
        if (n > MESH_MAX_VERTS) return;              /* a table error: the rest of the cell is dropped */
        if (p->count < p->max) {
            mesh_poly_t *o = &p->out[p->count];
            int i;
            o->n = (uint8_t)n;
            for (i = 0; i < n; i++) {
                int16_t X = w16(ox + bs16(cb, len, list + 4 * (size_t)i)), Z = w16(oz + bs16(cb, len, list + 4 * (size_t)i + 2));
                int16_t cx = w16(w16(((int32_t)X * m[0] + (int32_t)Z * m[2]) >> 8) + p->a_);
                int16_t cy = w16(w16(((int32_t)X * m[3] + (int32_t)Z * m[5]) >> 8) + p->b_);
                int16_t cz = w16(w16(((int32_t)X * m[6] + (int32_t)Z * m[8]) >> 8) + p->c_);
                o->v[i][0] = w16((int32_t)((uint32_t)(int32_t)cx << s));
                o->v[i][1] = w16((int32_t)((uint32_t)(int32_t)cy << s));
                o->v[i][2] = w16((int32_t)((uint32_t)(int32_t)cz << s));
            }
            p->count++;
        }
        list += 4 * (size_t)n;
    }
}

/* The scaled-like-A' factor: x * (0x8000 / z) >> shift, z the zoom word (below 2 counts as 2). */
static int32_t zoom_q(int zoom) { int z = zoom < 2 ? 2 : zoom; return 0x8000 / z; }

static void do_cell(mp_t *p, int type, int col, int row)
{
    const mesh_in_t *in = p->in;
    const uint8_t *sc = p->t->scan;
    const int dx = (int8_t)sc[2 * type], dz = (int8_t)sc[2 * type + 1];
    const int ncell = p->fine ? 32 : 8;
    const uint32_t mask = p->fine ? 0x00FFFFFFu : 0x0FFFFFFFu;
    const int cshift = p->fine ? 24 : 28, ushift = p->fine ? 12 : 16;
    const int cc = col + dx, cr = row + dz;
    size_t gbase = p->fine ? MB_FINE_GRID - MB_CELLS : 0;
    int w;
    int32_t px = (int32_t)((uint32_t)in->pos[0] & mask), pz = (int32_t)((uint32_t)in->pos[2] & mask);
    int32_t ox32, oz32;
    int16_t ox, oz;
    int s = 0, use_alt = 0;
    size_t rec, list;
    int32_t alt_ptr;

    if (cc < 0 || cc >= ncell || cr < 0 || cr >= ncell) return;
    w = bs16(p->t->cells, MESH_CELLS_BYTES, gbase + (size_t)cr * (p->fine ? 64 : 16) + (size_t)cc * 2);
    if (w <= 0) return;                                          /* a table error: the cell is skipped */
    rec = gbase + (size_t)w;
    ox32 = (int32_t)((uint32_t)((int32_t)dx) << cshift) - px;
    oz32 = (int32_t)((uint32_t)((int32_t)dz) << cshift) - pz;
    ox = w16(ox32 >> ushift); oz = w16(oz32 >> ushift);
    if (p->fine && !in->map) {
        if (type == 4) {
            s = p->a <= 0x400 ? 2 : (p->a <= 0xC80 ? 1 : 0);
            use_alt = in->own_alt != 0;
        } else if (p->a <= 0xC80) {
            int32_t t = bs16(p->t->far_tab, MESH_FAR_BYTES, 2 * (size_t)((p->a >> 8) < 17 ? (p->a >> 8) : 17));
            int64_t lim, adx, adz;
            int32_t dxl, dzl;
            s = 1;
            if (!in->zoom_default) t = (t * zoom_q(in->zoom)) >> 8;
            lim = (int64_t)t * 65536;
            dxl = (int32_t)((uint32_t)ox32 + 0x800000u); dzl = (int32_t)((uint32_t)oz32 + 0x800000u);
            adx = dxl < 0 ? -(int64_t)dxl : dxl; adz = dzl < 0 ? -(int64_t)dzl : dzl;
            if (adx > lim) use_alt = (adx & 0xFFFF) != 0;        /* quirk: the flag keeps the distance itself and later tests its low word */
            else use_alt = adz > lim;
        }
    }
    alt_ptr = bs32(p->t->cells, MESH_CELLS_BYTES, rec);
    if (alt_ptr < 0) return;                                      /* no water in this cell */
    list = rec + 4;
    if (use_alt) list = (size_t)((uint32_t)alt_ptr - MB_CELLS);
    emit_list(p, list, ox, oz, s);
}

/* One pass over the cells of the scan list at `at` in the scan blob. */
static void do_scan(mp_t *p, size_t at, int col, int row)
{
    const uint8_t *sc = p->t->scan;
    int i;
    for (i = 0; i < 32; i++) {
        int b = bu8(sc, MESH_SCAN_BYTES, at + (size_t)i);
        if (b == 0xFF) return;
        do_cell(p, b & 0x7F, col, row);
        if (b & 0x80) return;
    }
}

static int own_cell_test(const sky_tables_t *t, int32_t a, int phase, int octant)
{
    size_t base = a > 0x160 ? 0x2F0DCu : (a > 0x10 ? 0x2F11Cu : 0x2F15Cu);
    size_t at = base - MB_SCAN + 4 * (size_t)phase;
    int i;
    if ((int8_t)t->scan[at] < 0) return 1;
    for (i = 0; i < 4; i++) if (t->scan[at + (size_t)i] == octant) return 1;
    return 0;
}

int mesh_compute(const sky_tables_t *t, const mesh_in_t *in, mesh_poly_t *out, int max, int32_t *a_scaled)
{
    mp_t p;
    int32_t a = in->alt, h;
    int phase = in->phase & 15, octant = in->octant & 7;
    int32_t px = in->pos[0], py = in->pos[1], pz = in->pos[2];

    if (!in->zoom_default && a <= 0x7FFF0)
        a = ((int32_t)(uint16_t)(a >> 4) * zoom_q(in->zoom)) >> 4;
    if (a_scaled) *a_scaled = a;
    memset(&p, 0, sizeof p);
    p.t = t; p.in = in; p.out = out; p.max = max; p.a = a;

    /* coarse pass */
    if (a > 1016) {
        size_t at;
        int qx = 3 - (int)((px >> 26) & 3), qz = 3 - (int)((pz >> 26) & 3), idx = qx + 4 * qz;
        p.fine = 0;
        /* NOTE-GAP: h = -32768 (the product -h * m is taken here as the 32-bit negation, +32768 * m; whether the original negates the word, as the draw layer
         * does in note 03, is not stated for the mesh; a height of 32768 * 65536 position units does not occur, the camera stays below 8,388,608) */
        h = (int16_t)(py >> 16);
        p.a_ = w16((-h * in->m[1]) >> 8); p.b_ = w16((-h * in->m[4]) >> 8); p.c_ = w16((-h * in->m[7]) >> 8);
        at = (a <= 0x5000 ? 0x2E732u + 4u * (size_t)idx : 0x2E632u + 16u * (size_t)idx) - MB_SCAN;
        do_scan(&p, at, (int)((uint32_t)px >> 28) + 4, (int)((uint32_t)pz >> 28) + 4);   /* the high word, unsigned: a negative position is column or row 12 and up, off the grid */
    }
    /* fine pass */
    {
        size_t at;
        p.fine = 1;
        h = (int16_t)((py + 0x1000) >> 12);
        p.a_ = w16((-h * in->m[1]) >> 8); p.b_ = w16((-h * in->m[4]) >> 8); p.c_ = w16((-h * in->m[7]) >> 8);
        if (a > 0x10000) at = 0x2E7C2u - MB_SCAN;
        else if (a > 0x9000) at = 0x2E772u - MB_SCAN;
        else if (a <= 0x3A0 && own_cell_test(t, a, phase, octant)) at = 0;   /* the fixed list {type 4} is handled below */
        else if (a <= 0x3000) at = 0x2EEDCu - MB_SCAN + (size_t)phase * 32 + (size_t)octant * 4;
        else if (a <= 0x4800) at = 0x2EBDCu - MB_SCAN + (size_t)phase * 48 + (size_t)octant * 6;
        else at = 0x2E7DCu - MB_SCAN + (size_t)phase * 64 + (size_t)octant * 8;
        if (a <= 0x3A0 && a <= 0x9000 && own_cell_test(t, a, phase, octant)) {
            do_cell(&p, 4, (int)((uint32_t)px >> 24) & 0xFF, (int)((uint32_t)pz >> 24) & 0xFF);
        } else {
            do_scan(&p, at, (int)((uint32_t)px >> 24) & 0xFF, (int)((uint32_t)pz >> 24) & 0xFF);
        }
    }
    return p.count;
}
