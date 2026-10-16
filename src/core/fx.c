/* src/core/fx.c - see fx.h. Source of every rule: docs/re/03-3d-pipeline.md. */
#include <stdint.h>
#include "fx.h"

/* ---- helpers --------------------------------------------------------------------- */

/* Keep the low 16 bits as a signed word (note, preamble). */
static int16_t w16(int32_t v) { int32_t u = (int32_t)((uint32_t)v & 0xFFFFu); return (int16_t)(u >= 0x8000 ? u - 0x10000 : u); }
/* 32-bit wrapping add, as the 68000 does. */
static int32_t add32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static int32_t neg_w(int32_t v) { return w16(-(int32_t)w16(v)); }

/* ---- tables ---------------------------------------------------------------------- */

#define SIN_QUARTER 900    /* note, "Tables": 901 words, quarter circle, 0.1 degree steps */
#define TENTHS_PER_CIRCLE 3600   /* note, "Constants": 3600 after >> 3 */
#define ANGLE_SHIFT 3      /* note, "Angle unit and sine table": consumers shift right by 3 */
#define HYPOT_N 257        /* note, "Tables": 0x208E8, 257 words */

static int16_t sin_tab[SIN_QUARTER + 1];
static uint16_t hyp_tab[HYPOT_N];
static int tabs_ready;

static uint64_t isqrt64(uint64_t n) {
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* sin(i * 0.1 degree) * 16384 rounded to nearest, integer-only (Q30 Taylor series).
 * The note states the table equals this formula at all 901 entries. */
static int16_t gen_sin(int i) {
    const int64_t pi_q30 = 3373259426LL;                 /* pi * 2^30, rounded */
    int64_t x = ((int64_t)i * pi_q30) / 1800;            /* i * 0.1 deg in radians, Q30 */
    int64_t x2 = (x * x) >> 30, term = x, sum = x;
    for (int k = 1; k <= 7; k++) {                       /* x - x^3/3! + x^5/5! ... */
        term = (term * x2) >> 30;
        term /= (int64_t)(2 * k) * (2 * k + 1);
        sum += (k & 1) ? -term : term;
    }
    return (int16_t)((sum * 16384 + ((int64_t)1 << 29)) >> 30);
}

static void init_tables(void) {
    if (tabs_ready) return;
    for (int i = 0; i <= SIN_QUARTER; i++) sin_tab[i] = gen_sin(i);
    /* note, "Tables": T[i] = 16384 * sqrt(1 + (i/256)^2), rounded = round(sqrt(2^28 + 4096 i^2)) */
    for (int i = 0; i < HYPOT_N; i++) {
        uint64_t n = ((uint64_t)1 << 28) + 4096ull * (uint64_t)i * (uint64_t)i;
        uint64_t r = isqrt64(n);
        if (n - r * r > r) r++;                          /* round to nearest */
        hyp_tab[i] = (uint16_t)r;
    }
    tabs_ready = 1;
}

/* ---- sine and cosine -------------------------------------------------------------- */

/* Fold per note "Angle unit and sine table": <=900 entry a; <=1800 entry 1800-a;
 * <=2700 minus entry a-1800; else minus entry 3600-a. */
static int16_t sin_fold(uint32_t a) {
    init_tables();
    a %= TENTHS_PER_CIRCLE;
    if (a <= 900) return sin_tab[a];
    if (a <= 1800) return sin_tab[1800 - a];
    if (a <= 2700) return (int16_t)-sin_tab[a - 1800];
    return (int16_t)-sin_tab[3600 - a];
}
int16_t fx_sin_tenth(uint32_t a) { return sin_fold(a); }
int16_t fx_cos_tenth(uint32_t a) { return sin_fold((a % TENTHS_PER_CIRCLE) + 900); }  /* note: sine of a + 900 */
int16_t fx_sin(uint32_t angle) { return fx_sin_tenth(angle >> ANGLE_SHIFT); }
int16_t fx_cos(uint32_t angle) { return fx_cos_tenth(angle >> ANGLE_SHIFT); }
uint32_t fx_neg_angle(uint32_t angle) { return angle ? 28800u - angle : 0; }   /* note: "28800 - angle (zero stays zero)" */

/* p x q: (p * q) >> 14 as a 32-bit product, result used as a word (note, "Object matrices"). */
static int32_t mx(int32_t p, int32_t q) { return w16((p * q) >> 14); }

/* ---- object matrices (Q14) ---------------------------------------------------------- */

void fx_matrix_a(fx_mat_t *out, uint32_t a, uint32_t b, uint32_t c) {
    int32_t sA = fx_sin(a), cA = fx_cos(a), sB = fx_sin(b), cB = fx_cos(b);
    int32_t sC = fx_sin(c), cC = fx_cos(c);
    int32_t sBsA = mx(sB, sA), cBsA = mx(cB, sA);
    out->m[0][0] = w16(((sBsA * sC) + cB * cC) >> 14);
    out->m[0][1] = w16(-mx(cA, sC));
    out->m[0][2] = w16((sB * cC - cBsA * sC) >> 14);
    out->m[1][0] = w16((cB * sC - sBsA * cC) >> 14);
    out->m[1][1] = (int16_t)mx(cA, cC);
    out->m[1][2] = w16((cBsA * cC + sB * sC) >> 14);
    out->m[2][0] = w16(-mx(sB, cA));
    out->m[2][1] = w16(-sA);
    out->m[2][2] = (int16_t)mx(cB, cA);
}

void fx_matrix_b(fx_mat_t *out, uint32_t a, uint32_t b, uint32_t c) {
    int32_t sA = fx_sin(a), cA = fx_cos(a), sB = fx_sin(b), cB = fx_cos(b);
    int32_t sC = fx_sin(c), cC = fx_cos(c);
    int32_t sCsA = mx(sC, sA), cCsA = mx(cC, sA);
    out->m[0][0] = w16((cC * cB - sCsA * sB) >> 14);
    out->m[0][1] = w16(-w16((sC * cB + cCsA * sB) >> 14));
    out->m[0][2] = (int16_t)mx(cA, sB);
    out->m[1][0] = (int16_t)mx(sC, cA);
    out->m[1][1] = (int16_t)mx(cC, cA);
    out->m[1][2] = (int16_t)sA;                          /* Q14, not shifted */
    out->m[2][0] = w16(-w16((cC * sB + sCsA * cB) >> 14));
    out->m[2][1] = w16((sC * sB - cCsA * cB) >> 14);
    out->m[2][2] = (int16_t)mx(cA, cB);
}

/* ---- camera matrices (Q8) ----------------------------------------------------------- */

void fx_cam_yp(fx_mat_t *out, uint32_t a, uint32_t b) {
    int32_t sA = fx_sin(a), cA = fx_cos(a), sB = fx_sin(b), cB = fx_cos(b);
    out->m[0][0] = w16(cB >> 6);
    out->m[0][1] = 0;
    out->m[0][2] = w16(sB >> 6);
    out->m[1][0] = w16(-((sB * sA) >> 20));
    out->m[1][1] = w16(cA >> 6);
    out->m[1][2] = w16((cB * sA) >> 20);
    out->m[2][0] = w16(-((sB * cA) >> 20));
    out->m[2][1] = w16((-sA) >> 6);
    out->m[2][2] = w16((cB * cA) >> 20);
}

void fx_cam_ypr(fx_mat_t *out, uint32_t a, uint32_t b, uint32_t c) {
    int32_t sA = fx_sin(a), cA = fx_cos(a), sB = fx_sin(b), cB = fx_cos(b);
    int32_t sC = fx_sin(c), cC = fx_cos(c);
    int32_t sBsA = (sB * sA) >> 14, cBsA = (cB * sA) >> 14;
    out->m[0][0] = w16((cB * cC + sBsA * sC) >> 20);
    out->m[0][1] = w16(-((cA * sC) >> 20));
    out->m[0][2] = w16((sB * cC - cBsA * sC) >> 20);
    out->m[1][0] = w16((cB * sC - sBsA * cC) >> 20);
    out->m[1][1] = w16((cA * cC) >> 20);
    out->m[1][2] = w16((sB * sC + cBsA * cC) >> 20);
    out->m[2][0] = w16(-((sB * cA) >> 20));
    out->m[2][1] = w16(-(sA >> 6));
    out->m[2][2] = w16((cB * cA) >> 20);
}

static void roll_common(fx_mat_t *out, int32_t s, int32_t c, int32_t one) {
    int32_t i, j;
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) out->m[i][j] = 0;
    out->m[0][0] = w16(c); out->m[0][2] = w16(s);
    out->m[1][1] = w16(one);
    out->m[2][0] = w16(-s); out->m[2][2] = w16(c);
}
void fx_roll_q8(fx_mat_t *out, uint32_t angle) {
    roll_common(out, fx_sin(angle) >> 6, fx_cos(angle) >> 6, 256);       /* note: m4 = 256 */
}
void fx_roll_q14(fx_mat_t *out, uint32_t angle) {
    roll_common(out, fx_sin(angle), fx_cos(angle), 16384);               /* note: m4 = 16384 */
}

void fx_rowscale(fx_mat_t *m, const int16_t scale[3]) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) m->m[r][c] = w16(((int32_t)m->m[r][c] * scale[r]) >> 8);
}

void fx_mat_q14_to_q8(fx_mat_t *out, const fx_mat_t *in) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) out->m[r][c] = (int16_t)(in->m[r][c] >> 6);
}

/* ---- vector transforms --------------------------------------------------------------- */

void fx_rotate(fx_vec_t *out, const fx_mat_t *m, const fx_vec_t *v) {
    int32_t x = w16(v->x), y = w16(v->y), z = w16(v->z), o[3];
    for (int i = 0; i < 3; i++)
        o[i] = w16(add32(add32(m->m[i][0] * x, m->m[i][1] * y), m->m[i][2] * z) >> 8);
    out->x = o[0]; out->y = o[1]; out->z = o[2];
}
void fx_camera(fx_vec_t *out, const fx_mat_t *cam, const fx_vec_t *v) { fx_rotate(out, cam, v); }

/* ---- projection -------------------------------------------------------------------------- */

#define FOCAL_X 160     /* note, "Constants": horizontal focal constant */
#define FOCAL_Y 90      /* vertical focal constant */
#define VIEW_W 320      /* viewport columns 0..319 */
#define VIEW_H 180      /* rows 0..179 */

static int32_t clampi(int32_t v, int32_t hi) { return v < 0 ? 0 : (v > hi ? hi : v); }

int fx_project_rows(int16_t *sx, int16_t *sy, const fx_vec_t *v, int16_t row_limit) {
    int32_t x = w16(v->x), y = w16(v->y), z = w16(v->z);
    *sx = 0; *sy = 0;
    /* note, "Projection" step 1: reject if x >= z, y >= z, -x >= z or -y >= z */
    if (x >= z || y >= z || -x >= z || -y >= z) return 0;
    int32_t px = clampi(x * FOCAL_X / z + 160, VIEW_W - 1);   /* step 2, centre 160 */
    int32_t py = clampi(y * FOCAL_Y / z + 90, VIEW_H - 1);    /* centre 90 */
    int32_t rx = 319 - px, ry = 180 - py;                      /* step 3: mirror */
    if (ry > row_limit) return 0;
    *sx = (int16_t)rx; *sy = (int16_t)ry;
    return 1;
}
int fx_project(int16_t *sx, int16_t *sy, const fx_vec_t *v) {
    return fx_project_rows(sx, sy, v, 0x7FFF);   /* note: examples set 0x4AA3C to 0x7FFF */
}

/* ---- polygon clip ------------------------------------------------------------------------ */

typedef struct { int16_t x, y, z; } cv_t;
#define CLIP_BUF (FX_CLIP_MAX_IN * 16 + 8)   /* a stage emits at most 2n (n inside + n edge crossings); 4 stages: 16n, +8 slack */

/* g for stage s: y, -y, x, -x (note, "Clipping" table). */
static int16_t clip_g(int s, const cv_t *p) {
    switch (s) {
    case 0: return p->y;
    case 1: return (int16_t)neg_w(p->y);
    case 2: return p->x;
    default: return (int16_t)neg_w(p->x);
    }
}
static int clip_inside(int s, const cv_t *p) { return clip_g(s, p) <= p->z; }   /* g <= z */

/* rdiv: truncating 16-bit signed divide with the note's rounding fix-up. */
static int32_t rdiv(int32_t num, int32_t d) {
    int32_t q = num / d, r = num % d;
    int32_t ar = r < 0 ? -r : r, ad = d < 0 ? -d : d;
    if (ar >= ad / 2) q += (q >= 0) ? 1 : -1;   /* note: zero quotient is corrected upwards */
    return q;
}

/* Intersection of edge P to Q; returns 0 on D == 0 (error handler in the original). */
static int clip_cross(int s, const cv_t *p, const cv_t *q, cv_t *o) {
    int16_t fP = w16((int32_t)p->z - clip_g(s, p));
    int16_t fQ = w16((int32_t)q->z - clip_g(s, q));
    int16_t d = w16((int32_t)fP - fQ);
    if (d == 0) return 0;
    int16_t dx = w16((int32_t)q->x - p->x), dy = w16((int32_t)q->y - p->y);
    o->x = w16(p->x + rdiv((int32_t)dx * fP, d));
    o->y = w16(p->y + rdiv((int32_t)dy * fP, d));
    o->z = clip_g(s, o);   /* new z = g of the new point */
    return 1;
}

int fx_clip(int16_t *out_xy, int n_out_max, const fx_vec_t *in, int n_in, int shift) {
    cv_t buf[2][CLIP_BUF];
    int cur = 0, n = n_in, i, s;
    if (shift < 0) shift = 0;
    if (shift > 16) shift = 16;   /* the 16-bit truncation makes any larger shift equal to 16 (and avoids UB at >= 32) */
    if (n_in < 3 || n_in > FX_CLIP_MAX_IN) return 0;
    for (i = 0; i < n; i++) {   /* note: sign-extend, shift left by S, truncate to 16 bits */
        buf[0][i].x = w16((int32_t)((uint32_t)(int32_t)w16(in[i].x) << shift));
        buf[0][i].y = w16((int32_t)((uint32_t)(int32_t)w16(in[i].y) << shift));
        buf[0][i].z = w16((int32_t)((uint32_t)(int32_t)w16(in[i].z) << shift));
    }
    for (s = 0; s < 4; s++) {
        const cv_t *src = buf[cur];
        cv_t *dst = buf[cur ^ 1];
        int m = 0;
        if (n > 0) {   /* a stage whose input is empty is skipped */
            if (clip_inside(s, &src[0])) dst[m++] = src[0];
            for (i = 1; i < n; i++) {
                int pin = clip_inside(s, &src[i - 1]), qin = clip_inside(s, &src[i]);
                if (pin != qin) { if (!clip_cross(s, &src[i - 1], &src[i], &dst[m++])) return 0; }
                if (qin) dst[m++] = src[i];
            }
            /* closing edge computed from the first vertex towards the last (note quirk) */
            if (clip_inside(s, &src[0]) != clip_inside(s, &src[n - 1])) {
                if (!clip_cross(s, &src[0], &src[n - 1], &dst[m++])) return 0;
            }
        }
        cur ^= 1; n = m;
    }
    if (n < 3 || n > n_out_max) return 0;     /* rejection applied once, to the final list */
    for (i = 0; i < n; i++) {
        const cv_t *p = &buf[cur][i];
        if (p->z <= 0) return 0;               /* final depth not positive rejects */
        int32_t px = clampi((int32_t)p->x * FOCAL_X / p->z + 160, VIEW_W - 1);
        int32_t py = clampi((int32_t)p->y * FOCAL_Y / p->z + 90, VIEW_H - 1);
        out_xy[2 * i] = (int16_t)(319 - px);
        out_xy[2 * i + 1] = (int16_t)(179 - py);   /* polygon path: 179 - sy' */
    }
    return n;
}

/* ---- back-face test --------------------------------------------------------------------------- */

int fx_visible(const fx_vec_t *a, const fx_vec_t *b, const fx_vec_t *c, uint16_t flags) {
    int k = (flags >> 7) & 7;   /* note: bits 7 to 9 of the flags word */
    int32_t v0x = w16(a->x), v0y = w16(a->y), v0z = w16(a->z);
    int32_t e1x = w16((uint32_t)w16((int32_t)w16(b->x) - v0x) << k);
    int32_t e1y = w16((uint32_t)w16((int32_t)w16(b->y) - v0y) << k);
    int32_t e1z = w16((uint32_t)w16((int32_t)w16(b->z) - v0z) << k);
    int32_t e2x = w16((uint32_t)w16((int32_t)w16(c->x) - v0x) << k);
    int32_t e2y = w16((uint32_t)w16((int32_t)w16(c->y) - v0y) << k);
    int32_t e2z = w16((uint32_t)w16((int32_t)w16(c->z) - v0z) << k);
    int32_t nx = w16((e1y * e2z - e1z * e2y) >> 8);
    int32_t ny = w16((e1z * e2x - e1x * e2z) >> 8);
    int32_t nz = w16((e1x * e2y - e1y * e2x) >> 8);
    int32_t dot = add32(add32(nx * v0x, ny * v0y), nz * v0z);
    return dot < 0;   /* note: D7 = 1 (cull) when dot >= 0; faces viewer = D7 0 */
}

/* ---- distance ------------------------------------------------------------------------------------- */

int32_t fx_dist(const fx_vec_t *v) {
    init_tables();
    uint32_t d2 = (uint32_t)v->x, d3 = (uint32_t)v->y, d4 = (uint32_t)v->z;
    uint32_t hi = d2 > d3 ? d2 : d3, lo = d2 > d3 ? d3 : d2;
    uint32_t t = lo == 0 ? hyp_tab[0] : hyp_tab[(lo * 256u) / hi];   /* note: T[(lo*256)/hi] */
    uint32_t h = hi * t;                                              /* Q14 */
    uint32_t e = d4 << 14;
    uint32_t big = e > h ? e : h, small = e > h ? h : e;
    big >>= 14;
    uint32_t q = big ? small / big : 0;
    uint32_t r = (big * hyp_tab[(q >> 6) > 256 ? 256 : (q >> 6)]) >> 14;
    return (int32_t)(r > 0x7FFF ? 0x7FFF : r);                       /* note: cap 0x7FFF */
}

int32_t fx_distkey(const fx_cam_off_t *st, int32_t d2, int32_t d3, int32_t d4) {
    int s = st->shift;
    int32_t d1 = st->off_x >> s, d6 = st->off_y >> s, d7 = st->off_z >> s;
    int32_t b0 = w16(d3);   /* D3 is a word (sign-extended) when byte 0x4A973 is zero; found by the rig, see report */
    int32_t a = w16(add32(w16(d2), d1)), b = add32(b0, d6), c = w16(add32(w16(d4), d7));
    if (a < 0) a = neg_w(a);
    if (b < 0) b = (b == INT32_MIN) ? INT32_MAX : -b;   /* guard overflow; result is capped below anyway */
    if (c < 0) c = neg_w(c);
    if (b >= 0x7FFF0) return 0x7FFF;   /* note: middle component limit */
    fx_vec_t v = { a >> 4, b >> 4, c >> 4 };
    return fx_dist(&v);
}

/* ---- ordering ------------------------------------------------------------------------------------------ */

int fx_sort(uint8_t *out, const int16_t *keys, int n) {
    int m = 0, i, j;
    for (i = 0; i < n; i++) {
        if (keys[i] < 0) continue;                       /* note: negative keys are dropped */
        for (j = m; j > 0 && keys[out[j - 1]] < keys[i]; j--) out[j] = out[j - 1];
        out[j] = (uint8_t)i;                              /* descending, ties keep ascending index */
        m++;
    }
    return m;
}

/* ---- level of detail ------------------------------------------------------------------------------------------ */

#define LOD_N 240   /* note, "Tables": 240 bytes at 0x20E4A */
static uint8_t lod_tab[LOD_N];
static size_t lod_len;
void fx_set_lod_table(const uint8_t *t, size_t n) {
    lod_len = n > LOD_N ? LOD_N : n;
    for (size_t i = 0; i < lod_len; i++) lod_tab[i] = t[i];
}
uint8_t fx_lod_shift(uint32_t coarse) {
    if (!lod_len) return 0;   /* no table loaded: shift 0 = full detail */
    if (coarse > 239) coarse = 239;                       /* note: capped at 239 */
    return coarse < lod_len ? lod_tab[coarse] : lod_tab[lod_len - 1];
}
