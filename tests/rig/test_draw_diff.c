/* tests/rig/test_draw_diff.c - differential test: the port's primitive handlers of the draw layer (src/core/world.c, draw_item through world_test_handler) against the
 * original's handlers in the Musashi rig (docs/re/03-3d-pipeline.md, "Draw operations", "Hatch and stripe geometry", "The clipped line routine", "Fill state").
 *
 * Each case is a camera-space vertex table (words) and a stream of operands, as the common drawer's program interpreter hands them to a handler (A2 = the operand words, the table at
 * 0x4D43C filled, A6 a zeroed frame). The filler 0x34A28 and the line routine 0x3455E are replaced in the rig's RAM by two small 68000 stubs that append their input to a log: for
 * the filler the clipper's output list at 0x5043C, for the line routine the registers D0..D3; both log the fill state words V:4A79E, V:4A7A0, V:4A7A2 and the pen V:4AA0C at the call.
 * The port runs the same operands through world_test_handler (the clipper's output is handed to the sink as it is). Compared per handler call: the returned word, the sign of the
 * condition codes, every polygon (vertex count and the screen vertices in order) and every line (the four screen words) with the fill state it was drawn under, and after the call
 * the fill state words. A case is a sequence of 1 to 6 handlers on one object (the state is reset at its start), so the state a handler inherits is tested too.
 *
 * Environment: DRAW_DIFF_N (sequences, default 3000), DRAW_DIFF_SEED, DRAW_DIFF_VERBOSE (every difference), DRAW_DIFF_OPS (a comma list of ops to restrict the sequences to), DRAW_DIFF_FULL (full-range words in every sequence), DRAW_DIFF_EDGE (with full-range words, the coordinate -32,768 in a share of them).
 * Skipped (exit 0) when extracted/interceptor.bin is absent. Run from tools/m68k-rig (make test-draw). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/world.h"
#include "../../src/core/fx.h"

#define BASE 0x10000u
#define V_LO 0x4A6E8u
#define V_HI 0x54EC0u
#define VTAB 0x4D43Cu
#define STUB_FILL 0x600000u
#define STUB_LINE 0x600100u
#define LOGP_F 0x600FF0u
#define LOGP_L 0x600FF4u
#define LOG_F 0x601000u
#define LOG_L 0x650000u
#define OPS 0x600200u
#define OPTAB 0x23408u

static int verbose;
static uint32_t rng = 0x2545F491u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int rnd(int lo, int hi) { return lo + (int)(xr() % (uint32_t)(hi - lo + 1)); }
static int pct(int p) { return rnd(0, 99) < p; }

static void put16(uint32_t a, int v) { uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v }; rig_poke(a, b, 2); }
static void put32(uint32_t a, uint32_t v) { uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; rig_poke(a, b, 4); }
static unsigned get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return ((unsigned)b[0] << 8) | b[1]; }
static uint32_t get32(uint32_t a) { uint8_t b[4]; rig_peek(a, b, 4); return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]; }

static void poke_words(uint32_t a, const uint16_t *w, int n) { for (int i = 0; i < n; i++) put16(a + 2u * (unsigned)i, w[i]); }

static uint32_t handler[80];

static int rig_setup(void) {
    FILE *f = fopen("../../extracted/interceptor.bin", "rb");
    if (!f) return -1;
    fclose(f);
    rig_init(0xE00000);
    rig_set_max_instructions(3000000);   /* a handler is a few thousand instructions; a runaway case must not stall the run */
    rig_load_image("../../extracted/interceptor.bin", BASE);
    for (int i = 0; i < 80; i++) handler[i] = get32(OPTAB + 4u * (unsigned)i);
    static const uint16_t fill[] = {
        0x48E7, 0xC0C0, 0x41F9, 0x0005, 0x043C, 0x2279, 0x0060, 0x0FF0, 0x3018, 0x32C0,
        0x32F9, 0x0004, 0xA79E, 0x32F9, 0x0004, 0xA7A0, 0x32F9, 0x0004, 0xA7A2, 0x32F9, 0x0004, 0xAA0C,
        0x3200, 0x5341, 0x6B06, 0x22D8, 0x51C9, 0xFFFC, 0x23C9, 0x0060, 0x0FF0, 0x4CDF, 0x0303, 0x4E75 };
    static const uint16_t line[] = {
        0x2F09, 0x2279, 0x0060, 0x0FF4, 0x32C0, 0x32C1, 0x32C2, 0x32C3,
        0x32F9, 0x0004, 0xA79E, 0x32F9, 0x0004, 0xA7A0, 0x32F9, 0x0004, 0xA7A2, 0x32F9, 0x0004, 0xAA0C,
        0x23C9, 0x0060, 0x0FF4, 0x225F, 0x4E75 };
    poke_words(STUB_FILL, fill, (int)(sizeof fill / sizeof *fill));
    poke_words(STUB_LINE, line, (int)(sizeof line / sizeof *line));
    static const uint16_t j1[] = { 0x4EF9, 0x0060, 0x0000 }, j2[] = { 0x4EF9, 0x0060, 0x0100 };
    poke_words(0x34A28, j1, 3);
    poke_words(0x3455E, j2, 3);
    return 0;
}

/* ---- the cases ------------------------------------------------------------------------------------------- */
#define NSLOT 14
typedef struct { int op; int nw; uint16_t w[64]; int nop; int32_t o[64]; unsigned pen; int npoly; int nax[3]; uint16_t ax[3][16]; } call_t;   /* ax: the polygon records of op 55, placed at AUX + 0x100 k */
#define AUX 0x620000u   /* w: the original's stream words; o, pen: the extractor's operands */

static const int op_list[] = { 2, 74, 48, 13, 75, 14, 11, 12, 6, 19, 20, 32, 21, 57, 22, 23, 26, 27, 30, 17, 18, 24, 25, 9, 10, 15, 16, 3, 55, 39, 40 };
static int op_enabled[80];

/* full: words over the whole 16-bit range (the clipped line is word exact there; the polygon clipper of the original is not characterised beyond +-300 times 4 for a quotient that overflows,
 * note 03 open questions, so sequences with polygon operations keep the words within +-3000) */
static int edge;
static void gen_vertices(fx_vec_t *v, int n, int full) {
    int mode = rnd(0, 4);
    for (int i = 0; i < n; i++) {
        int m = mode == 4 ? rnd(0, 3) : mode;
        int z, x, y;
        if (m == 0) { z = rnd(40, 3000); x = rnd(-z * 5 / 4, z * 5 / 4); y = rnd(-z * 5 / 4, z * 5 / 4); }
        else if (m == 1) { int r = full ? 32767 : 3000; z = rnd(-r - (full ? 1 : 0), r); x = rnd(-r - (full ? 1 : 0), r); y = rnd(-r - (full ? 1 : 0), r); }
        else if (m == 2) {   /* near or on the boundary of the pyramid */
            z = rnd(20, 400); x = rnd(-z - 2, z + 2); y = rnd(-z - 2, z + 2);
            if (pct(40)) x = pct(50) ? z : -z;
            if (pct(40)) y = pct(50) ? z : -z;
        } else { z = pct(60) ? -rnd(1, 2000) : rnd(1, 2000); x = rnd(-2500, 2500); y = rnd(-2500, 2500); }
        if (full && edge) { if (pct(15)) x = -32768; if (pct(15)) y = -32768; if (pct(8)) z = -32768; }   /* DRAW_DIFF_EDGE: the word negation's one special value */
        v[i].x = x; v[i].y = y; v[i].z = z;
    }
}

static int is_poly_op(int op) { return !(op == 2 || op == 74 || op == 48 || op == 13 || op == 75 || op == 14 || op == 11 || op == 12 || op == 6 || op == 19 || op == 22 || op == 39 || op == 40); }
static int S6(int s) { return s * 6; }
static int cnt_pen(void) { return rnd(0, 15); }

/* builds the stream words and the extractor operands of op `op` */
static void gen_call(call_t *c, int op) {
    memset(c, 0, sizeof *c);
    c->op = op;
    int *nw = &c->nw, *no = &c->nop;
    unsigned pen = (unsigned)cnt_pen();
    c->pen = pen;
#define W(x) (c->w[(*nw)++] = (uint16_t)(x))
#define O(x) (c->o[(*no)++] = (x))
    switch (op) {
    case 2: case 74: case 48: { int a = rnd(0, 5), b = rnd(0, 5); W(S6(a)); W(S6(b)); W(pen); O(a); O(b); break; }
    case 13: case 75: case 14: {
        int np = rnd(1, 3), vref = rnd(0, NSLOT - 2);
        W(pen);
        if (op == 14) { W(S6(vref)); O(vref); }
        if (op == 75) O((int32_t)pen);   /* raw: the pen word, then byte offsets with bit 15 on the last */
        for (int i = 0; i < np; i++) {
            int a = rnd(0, 5), b = rnd(0, 5), last = i == np - 1;
            W(S6(a)); W(S6(b) | (last ? 0x8000 : 0));
            if (op == 75) { O(S6(a)); O(S6(b) | (last ? 0x8000 : 0)); } else { O(a); O(b); }
        }
        break;
    }
    case 11: case 12: {
        int cnt = rnd(0, 3), v = rnd(0, 4), vref = rnd(0, NSLOT - 2);
        W((cnt << 8) | pen);
        if (op == 12) { W(S6(vref)); O(cnt); O(vref); O(v); } else { O(cnt); O(v); }
        W(S6(v));
        break;
    }
    case 6: { int n = rnd(2, 6);   /* N is 2 or more: below 2 the original overwrites 1.5 MB above 0x4FE3C or divides by zero (undefined; no shipped item, note 03), and the port ruling draws nothing */ W(n); O(n); for (int i = 0; i < 4; i++) { int s = rnd(0, 7); W(S6(s)); O(s); } W(pen); break; }
    case 19: case 32: { int v = rnd(0, 6), n = rnd(-1, 4); W(pen); W(S6(v)); W(n); O(v); O(n); break; }
    case 20: case 23: { int v = rnd(0, 6), n = rnd(-1, 4); W(S6(v)); W(n); O(v); O(n); if (op == 20) c->pen = 13; break; }
    case 21: case 57: case 22: {
        int v = rnd(0, 5), n1 = rnd(1, 4);
        if (op != 21) W(pen);
        W(S6(v)); W(n1); O(v); O(n1);
        for (int i = 0; i < n1; i++) { int n2 = rnd(-1, 4); W(n2); O(n2); }
        if (op == 21) c->pen = 13;
        break;
    }
    case 26: { int n = rnd(3, 6), v = rnd(0, 6); W(n); W(S6(v)); O(n); O(v); c->pen = 7; break; }
    case 27: case 30: { int v = rnd(0, 7); W(pen); W(S6(v)); O(v); break; }
    case 9: case 10: { int v = rnd(0, NSLOT - 4); W(pen); W(S6(v)); O(v); break; }   /* the height gate: V:4AB30 is drawn on both sides of -0x80 */
    case 15: { int a = rnd(0, 6), b = rnd(0, 7), cc = rnd(0, 7), e = rnd(0, 7); W(pen); W(S6(a)); W(S6(b)); W(S6(cc)); W(S6(e)); O(a); O(b); O(cc); O(e); break; }
    case 16: { int a = rnd(0, 7), b = rnd(0, 6), cc = rnd(0, 7); W(pen); W(S6(a)); W(S6(b)); W(S6(cc)); O(a); O(b); O(cc); break; }
    case 17: case 18: { int nq = rnd(1, 2); for (int i = 0; i < nq * 4; i++) { int s = rnd(0, 7); W(S6(s)); O(s); } W(0x8000); c->pen = 12; break; }
    case 39: case 40: { unsigned p2 = (unsigned)cnt_pen(), p3 = (unsigned)cnt_pen(); W(pen); W(p2); O((int32_t)pen); O((int32_t)p2); if (op == 39) { W(p3); O((int32_t)p3); } break; }   /* the pens (the rig's object position and V:4AB2A are zero: the first pen); slot 1 decides the closed-pyramid exit */
    case 3: { int n = rnd(3, 6); O(n); for (int i = 0; i < n; i++) { int s = rnd(0, 9); W(S6(s) | (i == n - 1 ? 0x8000 : 0)); O(s); } c->pen = (unsigned)cnt_pen(); W(c->pen); break; }   /* the W word follows the slots: pen bits only, so the face test applies */
    case 55: {   /* the polygon lists of the entity models: the base long, the offsets (bit 15 ends), each offset a polygon record (slots with the last bit, then W) */
        int np = rnd(1, 3);
        W(AUX >> 16); W(AUX & 0xFFFF);
        O(np); c->npoly = np;
        for (int k = 0; k < np; k++) W(0x100 * k);
        W(0x8000);
        for (int k = 0; k < np; k++) {
            int n = rnd(3, 5), wp = rnd(0, 15);
            O(wp); O(0); O(0); O(n);
            for (int i = 0; i < n; i++) { int s = rnd(0, 9); c->ax[k][c->nax[k]++] = (uint16_t)(S6(s) | (i == n - 1 ? 0x8000 : 0)); O(s); }
            c->ax[k][c->nax[k]++] = (uint16_t)wp;
        }
        c->pen = 0;
        break;
    }
    case 24: { int n = rnd(3, 5); W(n); O(n); for (int i = 0; i < n; i++) { int s = rnd(0, 7); W(S6(s)); O(s); } W(pen); break; }
    case 25: { int n = rnd(4, 5); W(n); W(pen); O(n); break; }   /* the shipped counts are 5 and 7; below 4 the original's copy loop wraps and fills the memory after the polygon buffer (probe: count 3 writes 40 vertices), outside the data */
    }
#undef W
#undef O
}

/* ---- the two sides ------------------------------------------------------------------------------------------ */
typedef struct { int n; int16_t xy[64]; unsigned mask, pat, tog, pen; } poly_rec_t;
typedef struct { int16_t xy[4]; unsigned mask, pat, tog, pen; } line_rec_t;
typedef struct { int word, ccneg, hung, fail, dz, pshift, pcount; int np, nl; poly_rec_t p[40]; line_rec_t l[40]; unsigned fmask, ftog, fpen; int fpat; } result_t;

static result_t R, P;

static void rig_case(const call_t *c, const fx_vec_t *v, int off_y_unused) {
    (void)off_y_unused;
    for (int i = 0; i < NSLOT; i++) { put16(VTAB + 6u * (unsigned)i, (unsigned)v[i].x & 0xFFFF); put16(VTAB + 6u * (unsigned)i + 2, (unsigned)v[i].y & 0xFFFF); put16(VTAB + 6u * (unsigned)i + 4, (unsigned)v[i].z & 0xFFFF); }
    poke_words(OPS, c->w, c->nw);
    for (int k = 0; k < c->npoly; k++) poke_words(AUX + 0x100u * (unsigned)k, c->ax[k], c->nax[k]);
    put32(LOGP_F, LOG_F); put32(LOGP_L, LOG_L);
    for (int r = 0; r < 16; r++) rig_set_reg((rig_reg_t)r, 0);
    rig_set_reg(RIG_A2, OPS);
    rig_set_reg(RIG_A6, 0x70000);
    memset(&R, 0, sizeof R);
    if (rig_run(handler[c->op]) != 0) { if (verbose) printf("    rig_run(op %d, %x) failed: error %d\n", c->op, handler[c->op], rig_last_error); R.word = 0x7777; R.ccneg = 7; R.hung = rig_last_error == RIG_ERR_TIMEOUT; R.fail = !R.hung; return; }
    R.word = (int16_t)(rig_get_reg(RIG_D0) & 0xFFFF);
    uint32_t sr = rig_get_reg(RIG_SR);
    R.ccneg = (int)(((sr >> 3) ^ (sr >> 1)) & 1);
    uint32_t pf = get32(LOGP_F), pl = get32(LOGP_L);
    for (uint32_t a = LOG_F; a < pf && R.np < 40;) {
        poly_rec_t *p = &R.p[R.np++];
        p->n = (int)get16(a); a += 2;
        p->mask = get16(a); p->pat = get16(a + 2); p->tog = get16(a + 4); p->pen = get16(a + 6); a += 8;
        for (int i = 0; i < p->n && i < 32; i++) { p->xy[2 * i] = (int16_t)get16(a); p->xy[2 * i + 1] = (int16_t)get16(a + 2); a += 4; }
    }
    for (uint32_t a = LOG_L; a < pl && R.nl < 40; a += 16) {
        line_rec_t *l = &R.l[R.nl++];
        for (int i = 0; i < 4; i++) l->xy[i] = (int16_t)get16(a + 2u * (unsigned)i);
        l->mask = get16(a + 8); l->pat = get16(a + 10); l->tog = get16(a + 12); l->pen = get16(a + 14);
    }
    R.pshift = (int16_t)get16(0x5103C); R.pcount = (int16_t)get16(0x5103E);
    R.fpen = get16(0x4AA0C) & 15;
    R.fmask = get16(0x4A79E); R.fpat = (int16_t)get16(0x4A7A0); R.ftog = get16(0x4A7A2);
}

static void port_sink(void *user, const world_out_t *o) {
    (void)user;
    if (o->kind == WORLD_OUT_POLY && P.np < 40) {
        poly_rec_t *p = &P.p[P.np++];
        p->n = o->n; memcpy(p->xy, o->xy, (size_t)o->n * 4);
        p->mask = o->mask; p->pat = (unsigned)(uint16_t)o->pattern; p->tog = o->toggle; p->pen = o->pen;
    } else if (o->kind == WORLD_OUT_LINE && P.nl < 40) {
        line_rec_t *l = &P.l[P.nl++];
        memcpy(l->xy, o->xy, 8);
        l->mask = o->mask; l->pat = 0xFFFF; l->tog = 0; l->pen = o->pen;   /* the port folds a pattern line into the pen bits */
    }
}

static void port_case(const call_t *c, const fx_vec_t *v, int off_y, int reset) {
    memset(&P, 0, sizeof P);
    int code = 0;
    P.word = world_test_handler(c->op, c->o, c->nop, c->pen, v, NSLOT, off_y, reset, port_sink, NULL, &code);
    P.ccneg = code != 0;
    P.dz = world_test_den_zero();
    world_test_pwords(&P.pshift, &P.pcount);
    unsigned m, t, pn; int pt;
    world_test_fill_state(&m, &pt, &t, &pn);
    P.fmask = m; P.fpat = pt; P.ftog = t; P.fpen = pn;
}

static long ncase, ndiff, nsame_draws, nhang_match;

static long op_n[80], op_d[80];

static void show_call(const call_t *c, const fx_vec_t *v, int off_y) {
    printf("      op %d words:", c->op);
    for (int i = 0; i < c->nw; i++) printf(" %04x", c->w[i]);
    printf("  off_y %d\n      slots:", off_y);
    for (int i = 0; i < 14; i++) printf(" (%d,%d,%d)", v[i].x, v[i].y, v[i].z);
    printf("\n");
}

/* effective pen of a rig line: the pattern byte's bits when the pattern word is not negative */
static unsigned rig_line_pen(const line_rec_t *l) { return (l->pat & 0x8000) ? (l->pen & 15) : (l->pat & 15); }

static int cur_k;
static long st26[2][2][2][2];   /* op 26: [all depths behind][some depth behind][rig word][rig cc negative] */
static int compare(const call_t *c, const fx_vec_t *v, int off_y, char *why, size_t wl) {
    if (c->op == 26) {
        int n = c->w[0], base = c->w[1] / 6, allb = 1, anyb = 0;
        for (int i = 0; i <= n && base + i < NSLOT; i++) { if (v[base + i].z < 0) anyb = 1; else allb = 0; }   /* N + 1 slots (the quirk of op 26) */
        st26[allb][anyb][R.word & 1][R.ccneg & 1]++;
        if (allb && getenv("DRAW_DIFF_STATS")) { printf("  B n %d base %d cc %d k %d v:", n, base, R.ccneg, cur_k); for (int i = 0; i < NSLOT; i++) printf(" (%d,%d,%d)", v[i].x, v[i].y, v[i].z); printf("\n"); }
    }
    /* The zero margin difference of the clipped line (note 03, "The zero difference"): the original stays in a one-instruction loop, the rig run stops at its instruction cap (the harness
     * word 0x7777); the port ruling rejects the segment and counts it. A hang must be matched by a count, and a count by a hang; nothing else is compared (the state after the cap is
     * not the original's, the sequence ends). */
    /* ops 39 and 40 at full range: the original's clipper can run away on a wrapped word (note 03, \"The slot-1 test of ops 39 and 40\": it overran its buffers); the rig then stops at its instruction cap with the
     * memory spoilt, so such a call is not compared and the rig is built afresh for the next sequence */
    if (R.hung && (c->op == 39 || c->op == 40)) { rig_shutdown(); rig_setup(); return 2; }
    if (R.fail && (c->op == 39 || c->op == 40)) { if (P.word == -1) { snprintf(why, wl, "slot 1 exit: port rejected, rig ran on (past the exit, then error %d: the harness sets no object)", rig_last_error); return 1; } return 0; }   /* past the slot-1 exit the handler reads an object the harness does not set */
    if (R.fail) { snprintf(why, wl, "rig_run failed (error %d, not the instruction cap)", rig_last_error); return 1; }
    if (R.hung || P.dz) {
        if (R.hung && P.dz) { nhang_match++; return 2; }
        if (R.hung) { snprintf(why, wl, "rig hung (instruction cap), port counted no zero margin difference"); return 1; }
        snprintf(why, wl, "port counted %d zero margin differences, rig did not hang", P.dz); return 1;
    }
    if (c->op == 39 || c->op == 40) {   /* only the slot-1 closed-pyramid exit (word -1, codes negative): the pen choice reads the object position, the scene offsets and the LOD, which this harness does not set, and the polygon clipper is not characterised at full range */
        if ((R.word == -1) != (P.word == -1) || (R.word == -1 && R.ccneg != P.ccneg)) { snprintf(why, wl, "slot 1 exit: rig word %d cc %d, port word %d cc %d", R.word, R.ccneg, P.word, P.ccneg); return 1; }
        return 0;
    }
    if ((R.word & 0xFFFF) != (P.word & 0xFFFF)) { snprintf(why, wl, "word rig %d port %d (polygons rig %d port %d, lines rig %d port %d)", R.word, P.word, R.np, P.np, R.nl, P.nl); return 1; }
    if (R.ccneg != P.ccneg) { snprintf(why, wl, "cc rig %d port %d (word %d)", R.ccneg, P.ccneg, R.word); return 1; }
    if (R.np != P.np) { snprintf(why, wl, "polygons rig %d port %d", R.np, P.np); return 1; }
    if (R.nl != P.nl) { snprintf(why, wl, "lines rig %d port %d", R.nl, P.nl); return 1; }
    for (int i = 0; i < R.np; i++) {
        const poly_rec_t *a = &R.p[i], *b = &P.p[i];
        if (a->n != b->n || memcmp(a->xy, b->xy, (size_t)a->n * 4)) { snprintf(why, wl, "polygon %d: vertices differ (rig n %d port n %d)", i, a->n, b->n); return 1; }
        if ((a->mask & 15) != b->mask || a->pat != b->pat || a->tog != b->tog || (a->pen & 15) != b->pen) {
            snprintf(why, wl, "polygon %d: state rig mask %x pat %04x tog %x pen %u, port mask %x pat %04x tog %x pen %u", i, a->mask, a->pat, a->tog, a->pen & 15, b->mask, b->pat, b->tog, b->pen); return 1;
        }
    }
    for (int i = 0; i < R.nl; i++) {
        const line_rec_t *a = &R.l[i], *b = &P.l[i];
        if (memcmp(a->xy, b->xy, 8)) { snprintf(why, wl, "line %d: rig (%d,%d)-(%d,%d) port (%d,%d)-(%d,%d)", i, a->xy[0], a->xy[1], a->xy[2], a->xy[3], b->xy[0], b->xy[1], b->xy[2], b->xy[3]); return 1; }
        if ((a->mask & 15) != b->mask || rig_line_pen(a) != b->pen) { snprintf(why, wl, "line %d: state rig mask %x pen %u, port mask %x pen %u", i, a->mask & 15, rig_line_pen(a), b->mask, b->pen); return 1; }
    }
    if ((R.fmask & 0xFFFF) != P.fmask || (R.fpat & 0xFFFF) != (P.fpat & 0xFFFF) || (R.ftog & 0xFFFF) != P.ftog) {
        snprintf(why, wl, "fill state after: rig mask %04x pat %04x tog %04x, port mask %04x pat %04x tog %04x", R.fmask, R.fpat & 0xFFFF, R.ftog, P.fmask, P.fpat & 0xFFFF, P.ftog); return 1;
    }
    if ((c->op == 3 || c->op == 55 || c->op == 24 || c->op == 25 || c->op == 26 || c->op == 9 || c->op == 10 || c->op == 15 || c->op == 16 || c->op == 27 || c->op == 30) && (R.pshift != P.pshift || R.pcount != P.pcount)) { snprintf(why, wl, "polygon buffer shift/count after: rig %d %d, port %d %d", R.pshift, R.pcount, P.pshift, P.pcount); return 1; }
    if (R.fpen != P.fpen) { snprintf(why, wl, "pen V:4AA0C after: rig %u port %u", R.fpen, P.fpen); return 1; }
    (void)c; (void)v; (void)off_y;
    nsame_draws += R.np + R.nl;
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (rig_setup() != 0) { printf("draw handlers: skipped (extracted/interceptor.bin is absent)\n"); return 0; }
    int n = getenv("DRAW_DIFF_N") ? atoi(getenv("DRAW_DIFF_N")) : 3000;
    if (getenv("DRAW_DIFF_SEED")) rng = (uint32_t)atoi(getenv("DRAW_DIFF_SEED")) * 2654435761u | 1u;
    verbose = getenv("DRAW_DIFF_VERBOSE") != NULL;
    edge = getenv("DRAW_DIFF_EDGE") != NULL;
    int nlist = (int)(sizeof op_list / sizeof *op_list);
    if (getenv("DRAW_DIFF_OPS")) {
        const char *s = getenv("DRAW_DIFF_OPS");
        while (*s) { op_enabled[atoi(s)] = 1; while (*s && *s != ',') s++; if (*s) s++; }
    } else for (int i = 0; i < nlist; i++) op_enabled[op_list[i]] = op_list[i] != 39 && op_list[i] != 40;   /* ops 39 and 40 only by DRAW_DIFF_OPS: their pen state is not the original's here, so a following op would differ */
    static uint8_t zero[V_HI - V_LO];
    int shown = 0;
    for (int it = 0; it < n; it++) {
        fx_vec_t v[NSLOT];
        int full = getenv("DRAW_DIFF_FULL") ? 1 : pct(30);   /* full-range words: only the line operations are compared in such a sequence */
        if (full) { int any = 0; for (int i = 0; i < nlist; i++) if (op_enabled[op_list[i]] && !is_poly_op(op_list[i])) any = 1; if (!any) full = 0; }
        gen_vertices(v, NSLOT, full);
        int off_y = pct(50) ? rnd(-0x300, -0xC1) : rnd(-0xC0, 0x100);
        int len = rnd(1, 6);
        rig_poke(V_LO, zero, sizeof zero);
        put16(0x4A79E, 0x000F); put16(0x4A7A0, 0xFFFF); put16(0x4A7A2, 0); put16(0x4AA0C, 5);   /* the pen starts at the sentinel 5 on both sides */
        put32(0x4AB30, (uint32_t)off_y);
        put16(0x4AA3C, 179);
        static call_t hist[8];
        for (int k = 0; k < len; k++) {
            int op;
            do op = op_list[rnd(0, nlist - 1)]; while (!op_enabled[op] || (full && is_poly_op(op)));
            /* ops 9, 10, 15 and 16 may follow other handlers: op 9's all-behind test reads the depth words at the buffer positions 4 to 6, residue of the polygons built before it (note 03, "Op 9 reads past
             * its quad"); the port keeps the polygon buffer as persistent state (world.c, pbuf) and the harness zeroes it at the start of a sequence like the rig's memory */
            call_t c;
            gen_call(&c, op);
            rig_case(&c, v, off_y);
            port_case(&c, v, off_y, k == 0);
            cur_k = k; hist[k] = c;
            char why[300];
            ncase++; op_n[op]++;
            int cmp = compare(&c, v, off_y, why, sizeof why);
            if (cmp == 2) break;   /* a matched hang: the sequence ends */
            if (cmp) {
                ndiff++; op_d[op]++;
                if (verbose || shown < 8) { shown++; printf("  DIFF seq %d call %d op %d: %s\n", it, k, op, why); if (verbose || shown <= 4) { for (int q = 0; q < k; q++) { printf("      before:"); show_call(&hist[q], v, off_y); } show_call(&c, v, off_y); } }
                break;   /* the sequence's state has diverged */
            }
        }
    }
    printf("draw handlers: %ld handler calls in %d sequences, %ld differences; %ld primitives compared (polygons and lines, with their fill state)\n", ncase, n, ndiff, nsame_draws);
    printf("  %ld zero margin differences: the rig hung and the port rejected and counted the segment\n", nhang_match);
    printf("  by op (differing/calls):");
    for (int i = 0; i < 80; i++) if (op_n[i]) printf(" %d(%ld/%ld)", i, op_d[i], op_n[i]);
    printf("\n");
    if (getenv("DRAW_DIFF_STATS")) for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) for (int w = 0; w < 2; w++) for (int cc = 0; cc < 2; cc++)
        if (st26[a][b][w][cc]) printf("  op 26 stats: all behind %d, some behind %d, rig word %d, rig cc negative %d: %ld\n", a, b, w, cc, st26[a][b][w][cc]);
    rig_shutdown();
    return ndiff ? 1 : 0;
}
