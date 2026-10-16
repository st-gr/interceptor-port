/* src/core/flight.c - the player's flight model, from docs/re/05-flight-model.md ("note 05").
 * Section numbers in the comments are that note's; "note 05 §17.x" cites the rule the note states for a place that was a gap. NOTE-GAP marks a place where
 * the note is still silent or ambiguous and the reading chosen is stated; the task report lists them.
 *
 * Arithmetic: words are int16_t with wrap (w16), products are 32-bit, right shifts of negative values are
 * arithmetic (checked by tests/test_fx.c for the toolchains in use). */
#include <stddef.h>
#include <string.h>
#include "flight_int.h"
#include "fx.h"

static const flight_env_t *g_env;

void flight_set_env(const flight_env_t *env) { g_env = env; }
const flight_env_t *flight_env(void) { return g_env; }

/* W0 bits (record +0x00) */
#define W0_HIT 0x8000u
#define W0_ACTIVE 0x1000u
#define W0_BRAKE 0x0800u
#define W0_DYING 0x0400u
#define W0_CRASHED 0x0200u
#define W0_VALID 0x0040u
/* W2 bits (record +0x02) */
#define W2_HOOK 0x8000u
#define W2_ENGAGED 0x4000u
#define W2_MIL 0x2000u
#define W2_SKID 0x1000u
#define W2_IDLE 0x0400u
#define W2_GROUND 0x0080u
#define W2_SETTLED 0x0040u
#define W2_RECENTRE 0x0010u
#define W2_AB 0x0008u
#define W2_GIMBAL 0x0004u
#define W2_CONTACT 0x0002u
#define W2_PROX 0x0001u

#define SPEED_KT_DIV 12

/* Angle words (+0x66, +0x68, +0x6A) are signed 16-bit like every state word (note 05 conventions: "all state words are signed 16-bit"). The
 * storage in aircraft.h is unsigned, so every rule reads them through these. A stall nose drop near level flight with a negative +0x26 stores
 * a small negative pitch word (rig: -1..-3); the rules below then take the signed branches, as the original does. */
static inline int pitch_w(const flight_state_t *f) { return (int16_t)f->pitch; }
static inline int heading_w(const flight_state_t *f) { return (int16_t)f->heading; }
static inline int roll_w(const flight_state_t *f) { return (int16_t)f->roll; }

/* ---- table lookups by address (note 05 §17.9) --------------------------------------------------------------------- */

/* The original never guards a table index: every lookup is an address computed from a state word, and every such address is inside the loaded image. The
 * module answers a lookup from the two extracted spans (flight.h) by address. The live variable block 0x4A6E8..0x4AE92 (hunk 71) is answered from the port's own
 * variable where it keeps that word (the table below), the rest of 0x49A12..0x4AE92 (display data written at run time) answers 0 and is counted: accepted
 * divergence, note 05 Q16. */
#define VAR(a, sz, field) { (a), (sz), (uint16_t)offsetof(flight_state_t, field) }
static const struct { uint32_t addr; uint8_t size; uint16_t off; } varmap[] = {
    VAR(0x4A7B2, 4, v4a7b2), VAR(0x4A830, 2, v4a830), VAR(0x4A834, 2, v4a834), VAR(0x4A83C, 1, v4a83c), VAR(0x4A83D, 1, v4a83d), VAR(0x4A844, 1, v4a844),
    VAR(0x4A847, 1, v4a847), VAR(0x4A84F, 1, v4a84f), VAR(0x4A850, 1, v4a850), VAR(0x4A857, 1, v4a857), VAR(0x4A858, 1, v4a858), VAR(0x4A866, 1, v4a866),
    VAR(0x4A867, 1, v4a867), VAR(0x4A878, 1, v4a878), VAR(0x4A87D, 1, v4a87d), VAR(0x4A890, 1, v4a890), VAR(0x4A8F6, 1, v4a8f6), VAR(0x4A8FD, 1, v4a8fd),
    VAR(0x4A8FE, 1, v4a8fe), VAR(0x4A8FF, 1, v4a8ff), VAR(0x4A903, 1, v4a903), VAR(0x4A910, 1, v4a910), VAR(0x4A928, 1, v4a928), VAR(0x4A952, 1, v4a952),
    VAR(0x4A957, 1, v4a957), VAR(0x4A95E, 1, v4a95e), VAR(0x4A963, 1, v4a963), VAR(0x4A968, 1, v4a968), VAR(0x4A982, 2, v4a982), VAR(0x4A994, 2, v4a994), VAR(0x4A996, 2, v4a996), VAR(0x4A984, 2, v4a984),
    VAR(0x4A986, 2, v4a986), VAR(0x4A98A, 2, v4a98a), VAR(0x4A9A8, 1, v4a9a8), VAR(0x4A9FE, 2, v4a9fe), VAR(0x4AA04, 2, v4aa04), VAR(0x4AA06, 2, v4aa06),
    VAR(0x4AA7A, 2, v4aa7a), VAR(0x4AB98, 2, v4ab98), VAR(0x4AC08, 4, v4ac08), VAR(0x4AC0C, 4, v4ac0c),
};

/* byte `a` of a variable the port keeps (big-endian image order); returns 0 when the port holds no variable there */
static int var_byte(const flight_state_t *f, uint32_t a, uint8_t *out) {
    for (size_t i = 0; i < sizeof varmap / sizeof varmap[0]; i++) {
        if (a < varmap[i].addr || a >= varmap[i].addr + varmap[i].size) continue;
        const unsigned char *p = (const unsigned char *)f + varmap[i].off;
        uint32_t v;
        if (varmap[i].size == 1) { uint8_t x; memcpy(&x, p, 1); v = x; }
        else if (varmap[i].size == 2) { uint16_t x; memcpy(&x, p, 2); v = x; }
        else { memcpy(&v, p, 4); }
        *out = (uint8_t)(v >> (8 * (varmap[i].size - 1 - (a - varmap[i].addr))));
        return 1;
    }
    return 0;
}

/* the five run-time text bytes inside span 2 (a text buffer the game writes; note 05 §17.9, "the span rule"): answered 0 and counted, never read in the rig scenarios */
static int is_text_byte(uint32_t a) { return (a >= 0x440C9u && a <= 0x440CCu) || a == 0x44EA1u; }

int16_t flight_image_word(const flight_tables_t *t, uint32_t addr, int *known) {
    if (known) *known = 0;
    if (is_text_byte(addr) || is_text_byte(addr + 1)) return 0;
    if (addr >= FLIGHT_SPAN_A_BASE && addr + 1 < FLIGHT_SPAN_A_BASE + FLIGHT_SPAN_A_SIZE) {
        const uint8_t *p = t->span_a + (addr - FLIGHT_SPAN_A_BASE);
        if (known) *known = 1;
        return (int16_t)(uint16_t)((p[0] << 8) | p[1]);
    }
    if (addr >= FLIGHT_SPAN_B_BASE && addr + 1 < FLIGHT_SPAN_B_BASE + FLIGHT_SPAN_B_SIZE) {
        const uint8_t *p = t->span_b + (addr - FLIGHT_SPAN_B_BASE);
        if (known) *known = 1;
        return (int16_t)(uint16_t)((p[0] << 8) | p[1]);
    }
    return 0;
}

int16_t fl_image_word(flight_state_t *f, const flight_tables_t *t, uint32_t addr) {
    int known;
    int16_t w = flight_image_word(t, addr, &known);
    if (known) return w;
    if (addr >= 0x4A6E8u && addr < 0x4AE92u) {
        uint8_t hi = 0, lo = 0;
        int nh = var_byte(f, addr, &hi), nl = var_byte(f, addr + 1, &lo);
        fl_count(f, nh && nl ? FL_COUNT_VARIABLE : FL_COUNT_RUNTIME);
        return (int16_t)(uint16_t)((hi << 8) | lo);
    }
    fl_count(f, (addr >= 0x49A12u && addr < 0x4A6E8u) || is_text_byte(addr) || is_text_byte(addr + 1) ? FL_COUNT_RUNTIME : FL_COUNT_IMAGE);
    return 0;
}

int16_t flight_lookup_word(aircraft_t *ac, const flight_tables_t *t, uint32_t addr) { return fl_image_word(&ac->f, t, addr); }

int16_t fl_sine(flight_state_t *f, const flight_tables_t *t, int i) {
    return fl_image_word(f, t, (uint32_t)((int32_t)FLIGHT_ADDR_SINE + 2 * i));
}

unsigned flight_guard_hits(const aircraft_t *ac, int kind) { return kind >= 0 && kind < FL_GUARD_KINDS ? ac->f.reads[kind] : 0; }
void flight_guard_reset(aircraft_t *ac) { memset(ac->f.reads, 0, sizeof ac->f.reads); }

/* The sine and cosine of an angle word as the matrix builders see it (0x32DB2, note 05 §17.9): the word is shifted right by 3 LOGICALLY as an unsigned 16-bit
 * number (0..8191 tenths of a degree), then folded with no range guard, so an angle word outside 0..28799 reads the sine table around its ends. */
static void sincos_word(flight_state_t *f, const flight_tables_t *t, uint16_t w, int32_t *s, int32_t *c) {
    int a = w >> 3;
    if (a < 900) { *s = fl_sine(f, t, a); *c = fl_sine(f, t, 900 - a); }
    else if (a < 1800) { *s = fl_sine(f, t, 1800 - a); *c = w16(-fl_sine(f, t, a - 900)); }
    else if (a < 2700) { *s = w16(-fl_sine(f, t, a - 1800)); *c = w16(-fl_sine(f, t, 2700 - a)); }
    else { *s = w16(-fl_sine(f, t, 3600 - a)); *c = fl_sine(f, t, a - 2700); }   /* neg.w: a word of -32768 stays -32768 */
}

/* 32-bit products and sums wrap as the 68000's do (the junk words of §17.9 can make them overflow) */
static int32_t mu(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static int32_t ad(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static int32_t sb(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
static int32_t mx14(int32_t p, int32_t q) { return w16(mu(p, q) >> 14); }

/* Matrix A (0x32B52): the formulas of fx_matrix_a over the address-mapped sine; angle words a, b, c (pitch, heading, roll as stored). */
static void matrix_a(flight_state_t *f, const flight_tables_t *t, uint16_t a, uint16_t b, uint16_t c, int16_t out[9]) {
    int32_t sA, cA, sB, cB, sC, cC;
    sincos_word(f, t, a, &sA, &cA); sincos_word(f, t, b, &sB, &cB); sincos_word(f, t, c, &sC, &cC);
    int32_t sBsA = mx14(sB, sA), cBsA = mx14(cB, sA);
    out[0] = w16(ad(mu(sBsA, sC), mu(cB, cC)) >> 14);
    out[1] = w16(-mx14(cA, sC));
    out[2] = w16(sb(mu(sB, cC), mu(cBsA, sC)) >> 14);
    out[3] = w16(sb(mu(cB, sC), mu(sBsA, cC)) >> 14);
    out[4] = (int16_t)mx14(cA, cC);
    out[5] = w16(ad(mu(cBsA, cC), mu(sB, sC)) >> 14);
    out[6] = w16(-mx14(sB, cA));
    out[7] = w16(-sA);
    out[8] = (int16_t)mx14(cB, cA);
}

/* Matrix B (0x32BEC): the same for the second Euler order; the caller passes the angles as 28800 - angle (0 stays 0), see neg_word. */
static void matrix_b(flight_state_t *f, const flight_tables_t *t, uint16_t a, uint16_t b, uint16_t c, int16_t out[9]) {
    int32_t sA, cA, sB, cB, sC, cC;
    sincos_word(f, t, a, &sA, &cA); sincos_word(f, t, b, &sB, &cB); sincos_word(f, t, c, &sC, &cC);
    int32_t sCsA = mx14(sC, sA), cCsA = mx14(cC, sA);
    out[0] = w16(sb(mu(cC, cB), mu(sCsA, sB)) >> 14);
    out[1] = w16(-w16(ad(mu(sC, cB), mu(cCsA, sB)) >> 14));
    out[2] = (int16_t)mx14(cA, sB);
    out[3] = (int16_t)mx14(sC, cA);
    out[4] = (int16_t)mx14(cC, cA);
    out[5] = (int16_t)sA;
    out[6] = w16(-w16(ad(mu(cC, sB), mu(sCsA, cB)) >> 14));
    out[7] = w16(sb(mu(sC, sB), mu(cCsA, cB)) >> 14);
    out[8] = (int16_t)mx14(cA, cB);
}

void flight_matrix_a(aircraft_t *ac, const flight_tables_t *t, uint16_t a, uint16_t b, uint16_t c, int16_t out[9]) { matrix_a(&ac->f, t, a, b, c, out); }
void flight_matrix_b(aircraft_t *ac, const flight_tables_t *t, uint16_t a, uint16_t b, uint16_t c, int16_t out[9]) { matrix_b(&ac->f, t, a, b, c, out); }

static uint16_t neg_word(uint16_t w) { return w ? (uint16_t)(28800u - w) : 0; }   /* 28800 - angle in 16 bits, 0 stays 0 */

/* ---- word helpers ---------------------------------------------------------------------------- */

static int16_t relax(int16_t x, int r) { return w16(x - ((x - r) >> 3)); }   /* "Relax(x, r)", conventions */
static int16_t slew(int16_t v, int th, int k) { return iabs32(v) > th ? w16(v - (v >> k)) : 0; }   /* "Slew(v, th, k)" */
static int16_t decay(int16_t x, int k) {   /* section 5, decay(x, k) */
    if (x < -15) return w16(x + ((-x) >> k));
    if (x > 15) return w16(x - (x >> k));
    /* note 05 §17.7: x from -15 to -1 gets +1, from 0 to 15 gets -1, so 0 becomes -1 */
    return x < 0 ? (int16_t)(x + 1) : (int16_t)(x - 1);
}

/* section 7, "Position": the cell fields (word +0x06, +0x08, byte +0x0A, words +0x0C, +0x0E, long +0x10). Note 05 §17.10: when the sub-cell byte +0x0A changes,
 * V:4A910 is set to 0xFF and, when the record's cell is within 2 of the viewed cell (V:4AA04, V:4AA06; signed word differences), +0x0B is rewritten: always 0x0F.
 * READING (rig): the test uses the cell words as they stood BEFORE this refresh (a step that moves the record across a cell border across the world's wrap, x below 0
 * with z reset to 0, still rewrites when the old cell was the viewed one). */
static void update_cells(aircraft_t *ac) {
    flight_state_t *f = &ac->f;
    const int near_view = iabs32(w16(f->cell_x - f->v4aa04)) <= 2 && iabs32(w16(f->cell_z - f->v4aa06)) <= 2;
    f->cell_x = (int16_t)(ac->pos[0] >> 16) >> 6; f->cell_z = (int16_t)(ac->pos[2] >> 16) >> 6;
    f->fine_x = w16((ac->pos[0] & 0x3FFFFF) >> 8);
    f->fine_z = w16((ac->pos[2] & 0x3FFFFF) >> 8);
    f->alt8 = ac->pos[1] >> 8;
    uint8_t sub = (uint8_t)(4 * (3 - (f->fine_z >> 12)) + (3 - (f->fine_x >> 12)));
    if (sub != f->cell_a) {
        f->v4a910 = 0xFF;
        if (near_view) f->cell_b = 0x0F;
    }
    f->cell_a = sub;
}

/* ---- section 6, step 8: the matrices ------------------------------------------------------- */

static void build_matrices(flight_state_t *f, const flight_tables_t *t) {
    matrix_a(f, t, f->pitch, f->heading, f->roll, f->mat_a);
    matrix_b(f, t, neg_word(f->pitch), neg_word(f->heading), neg_word(f->roll), f->mat_b);
}

void flight_ship_build(flight_ship_t *ship, const int16_t verts[][3], int nverts,
                       uint16_t pitch, uint16_t heading, uint16_t roll) {
    fx_mat_t m;
    fx_matrix_b(&m, fx_neg_angle(pitch), fx_neg_angle(heading), fx_neg_angle(roll));
    for (int i = 0; i < 9; i++) ship->mat_b[i] = m.m[i / 3][i % 3];
    ship->pitch = pitch; ship->heading = heading; ship->roll = roll;
    memset(ship->vtx, 0, sizeof ship->vtx);
    for (int k = 0; k < nverts && k < FLIGHT_SHIP_SLOTS; k++)
        for (int i = 0; i < 3; i++) {
            int32_t s = 0;
            for (int j = 0; j < 3; j++) s += (int32_t)(ship->mat_b[3 * i + j] >> 6) * verts[k][j];
            ship->vtx[k][i] = w16(s >> 8);
        }
}

/* ---- section 2: inputs ------------------------------------------------------------------------ */

static int step_cmd(int c, int pos, int neg, int lim, int stepsz) {
    /* held key: +step (to lim); sign flip jumps to +-step; no key: 0 at once.
     * note 05 §17.4: with both keys of a pair held the negative key wins (stick back, rudder left, roll left). */
    if (neg) { int n = c > 0 ? -stepsz : c - stepsz; return n < -lim ? -lim : n; }
    if (pos) { int n = c < 0 ? stepsz : c + stepsz; return n > lim ? lim : n; }
    return 0;
}

static void input_convert(flight_state_t *f) {
    uint8_t k = f->keys;
    if (f->gear & 0x0F) {
        /* note 05 §17.4: with an ejection nibble the whole conversion (commands, preset handling, recorder) returns at once */
        return;
    } else if ((f->b4 & 0x10) && f->counter >= 0) {
        f->cmd_pitch = f->cmd_rudder = f->cmd_roll = 0;
    } else {
        if (f->b4 & 0x10) f->b4 &= (uint8_t)~0x10;   /* note 05 §17.4: the lock bit is dropped once +0x4C is negative */
        f->cmd_pitch = (int8_t)step_cmd(f->cmd_pitch, k & FLIGHT_KEY_STICK_FORWARD, k & FLIGHT_KEY_STICK_BACK, 20, 1);
        f->cmd_rudder = (int8_t)step_cmd(f->cmd_rudder, k & FLIGHT_KEY_RUDDER_RIGHT, k & FLIGHT_KEY_RUDDER_LEFT, 20, 1);
        f->cmd_roll = (int8_t)step_cmd(f->cmd_roll, k & FLIGHT_KEY_ROLL_RIGHT, k & FLIGHT_KEY_ROLL_LEFT, 60, 3);
    }
    /* preset handling (live mode); a preset of 0 means none, a negative one counts as target 0 */
    if (f->v4a903 == 0 && f->v4a928 != 0) {
        int p = f->v4a928 < 0 ? 0 : f->v4a928;
        int t = f->throttle, r = f->repeat & 15;
        int d = (int8_t)(p - t);   /* the difference is a byte */
        /* note 05 §17.4: the sign of D comes from the signed comparison of P with T, the thresholds 8 and -8 are tested on the wrapped byte:
         * P = 121 and T = -28 give D = -107 as a byte, which is "greater" but not "> 8" (the key bits are cleared). */
        if (p == t) {
            /* the note: nothing changes only for T = 120 with the detent 0x2000 set and the afterburner off (rig: without the detent the bits are cleared) */
            if (t < 120 || (f->w2 & W2_AB) || !(f->w2 & W2_MIL)) { f->keys &= (uint8_t)~3; f->v4a928 = 0; }
        } else if (p > t) {
            f->keys = (uint8_t)((f->keys & ~3) | ((d > 8 || r < 2) ? 1 : 0));
        } else {
            f->keys = (uint8_t)((f->keys & ~3) | ((d < -8 || r < 2) ? 2 : 0));
        }
    }
}

/* ---- sections 3 and 4: throttle, fuel, speed ------------------------------------------------- */

static int16_t drag_g(const flight_state_t *f, int32_t y) {   /* 0x16A32, "Drag term G" */
    if (f->speed_target < 0x800) return 0;
    int p = pitch_w(f) >= 14400 ? pitch_w(f) - 28800 : pitch_w(f);
    int b = roll_w(f);
    if (b >= 14400) b = 28800 - b;
    if (b >= 7200) b = 14400 - b;
    int a = (int)((f->v4a7b2 - y) >> ((f->w2 & W2_AB) ? 11 : 13));
    return w16(2 * iabs32(f->rate_p) + (b >> 3) - (p >> 1) + a);
}

/* The pointer throttle, rules 1 and 2 of note 05 17.4 (rule 0, the mouse, is applied by flight_step from the inputs). Rule 1, follow: runs when the low two bits of
 * the key byte are non-zero at ENTRY of the throttle routine 0x15ED4 (the byte as the input conversion left it, the bits the preset handling set included; the
 * routine's own clearing of the bits in the detents comes after the test and does not undo the follow: 17.18 e): if P is 960 or more or |P - Q| is below 16, then
 * Q = U = 8 max(T, 0) and, when P is not negative, P = U. Rule 2, fire, every step unless the display mode V:4A95E is 2: if P is 960 or more or |P - Q| is 17 or more:
 * P negative gives P = 0 and the preset -1; otherwise with the afterburner on the preset is P >> 3 only when P is below 864, with it off nothing happens when P is 0,
 * else the notice 0xFE (T = 0 and the viewed entity, 17.18 d) and the preset P >> 3; then Q = P. A height of 1 to 7 gives the preset 0, which is "no preset" (the
 * preset handler returns at once for a zero byte; 17.4, 17.18 e): it is stored as 0, not turned into -1. */
static void pointer_rules(flight_state_t *f, int key_held) {
    int P = f->v4a830, Q = f->v4a834;
    const int dist = iabs32(P - Q);   /* READING (rig): the distance for rule 2 is the one before rule 1 moved Q (a negative P with a throttle key held keeps P and moves Q) */
    if (key_held) {
        int U = (f->throttle > 0 ? f->throttle : 0) * 8;
        if (P >= 960 || dist < 16) { Q = U; if (P >= 0) P = U; }
    }
    if (f->v4a95e != 2 && (P >= 960 || dist >= 17)) {
        if (P < 0) { P = 0; f->v4a928 = -1; }
        else if (f->w2 & W2_AB) { if (P < 864) f->v4a928 = (int8_t)(P >> 3); }
        else if (P != 0) {
            if (f->throttle == 0 && f->v4a994 == 0) f->v4a968 = 0xFE;   /* 17.18 d: V:4A994 = the stepped entity's index (0), not V:4A996 */
            f->v4a928 = (int8_t)(P >> 3);
        }
        Q = P;
    }
    f->v4a830 = (int16_t)P; f->v4a834 = (int16_t)Q;
}

static void speed_step(flight_state_t *f, const flight_tables_t *t, int32_t y) {
    /* section 3 */
    int16_t S = f->speed_target;
    const int16_t Sold = S;
    const int16_t Wold = f->speed;
    if (f->fuel == 0) {
        f->throttle = 0; f->w2 &= (uint16_t)~W2_AB;
        if (f->v4a890) f->v4a830 = f->v4a834 = 0;   /* note 05 §17.5: the pointer P and Q are reset to 0 (pointer on) */
    }
    int thr = f->keys & 3;
    if (f->fuel == 0) {
        /* note 05 §17.5: with no fuel the throttle key logic (the repeat counter, the notice 0xFE) and the pointer rules 1 and 2 are skipped */
    } else if (thr == 0) {
        f->repeat = (uint8_t)((f->repeat & 0xF0) | 1);
    } else {
        int R = f->repeat & 15, n = R + 1;
        if (n >= 9) { R = 8; n = 8; } else R = n;
        f->repeat = (uint8_t)((f->repeat & 0xF0) | R);
        int step = n >> 1;
        /* 17.18 d: the notices 0xFE, 0xFD and 0xFC are raised only when the viewed entity index V:4A994 equals the index of the entity being stepped (0 for the
         * player); V:4A996 is not tested. After the seat spawn in the cockpit view V:4A994 is 1 and the player's notices stop. */
        const int viewed = f->v4a994 == 0;
        if (step > 0 && f->throttle == 0 && viewed) f->v4a968 = 0xFE;
        /* note 05 §17.4: both throttle bits (3): T does not change, the repeat counter and the notice above do */
        if (thr == 3) {
            /* neither */
        } else if (thr == 1) {
            f->w2 &= (uint16_t)~W2_IDLE;
            int T = f->throttle + step;
            if (T >= 120) {
                T = 120;
                if (f->w2 & W2_MIL) {
                    if (!(f->w2 & W2_AB)) { f->w2 |= W2_AB; f->v4a8f6 = 2; if (viewed) { f->v4a968 = 0xFD; f->v4a84f = 6; } }
                } else {
                    f->w2 |= W2_MIL; f->keys &= (uint8_t)~3; f->v4a928 = 0;
                }
            }
            f->throttle = (int8_t)T;
        } else {
            f->w2 &= (uint16_t)~W2_MIL;
            int T = f->throttle;
            if (f->w2 & W2_AB) {
                f->w2 &= (uint16_t)~W2_AB; T -= 1; f->v4a8f6 = 2; if (viewed) f->v4a968 = 0xFC;
                /* note 05 §17.5: V:4A84F is not touched by the afterburner-off notice */
            } else if (T >= 0) {
                int tt = T - 4;
                if (tt >= 0) T = tt;
                else if (f->b4 & 0x08) T = 0;
                else if (!(f->w2 & W2_IDLE)) { f->keys &= (uint8_t)~3; T = 0; f->w2 |= W2_IDLE; }
                else T = tt;
            } else {
                T -= step;
            }
            if (T < -32) T = -32;
            f->throttle = (int8_t)T;
        }
    }
    if (f->fuel != 0 && f->v4a890) pointer_rules(f, thr != 0);   /* 17.4, 17.18 e: the key bits at entry of the throttle logic */
    /* section 4 */
    const int ab = (f->w2 & W2_AB) != 0, ground = (f->w2 & W2_GROUND) != 0, brake = (f->w0 & W0_BRAKE) != 0;
    const int geardown = (f->gear & 0x70) == 0;
    const int16_t C = w16(256 * f->throttle);
    const int16_t G = drag_g(f, y);
    const int gate = (f->v4a984 & 0x40) && !(f->v4a986 & 0x4000);
    if (gate) {
        if ((C == 0 && !ab) || f->fuel == 0) {   /* idle rules */
            if (ground) {
                f->ground = relax(f->ground, 0);
                if (S > 15) S = w16(S - 15); else if (S < -15) S = w16(S + 15); else S = 0;
            } else if (S < 0) {
                int as = -S;
                if (as > 0x90) as -= 0x52; else if (as > 0x3C) as -= 0x3C; else as = 0;
                S = w16(-as);
            } else {
                int s0 = S;
                if (S > 0x1D4C) S = w16(S - 0x52);
                if (s0 < 0x5DC) { if (S > 0x1E) S = w16(S - 0x1E); }
                else if (S > 0x3C) S = w16(S - 0x3C);
                else S = 0;
            }
        } else if (ab) {   /* afterburner rules */
            f->fuel -= 0x600;
            int vmax = w16(0x3A98 - G);
            if (vmax > 0x4650) vmax = 0x4650;
            if (geardown) vmax = w16(vmax - (vmax >> 2));
            if (brake) vmax = w16(vmax - (vmax >> 2));
            if (S < vmax) {
                if (S < 0) S = w16(S + 0x78);
                else if (!ground) S = w16(S + (S > 0x1D4C ? 0x2D : 0x36));
                else { f->ground = relax(f->ground, -37); S = w16(S - f->ground); }
            } else if (S > vmax + 0x3C) {
                S = w16(S - 0x3C);
            }
        } else {   /* normal rules */
            f->fuel -= iabs32(C) >> 9;
            if (C >= 0 && !ground) {
                int a = (C >> 1) - (C >> 3);
                int vt = a - (a >> 2);
                if (f->b20 & 0x01) vt >>= 2;
                if (vt > 0x1FEF) vt = 0x1FEF;
                vt = w16(vt - G);
                if (brake) vt = w16(vt - (vt >> 2));
                if (geardown) vt = w16(vt - (vt >> 2));
                if (S < vt) {
                    if (S < 0) S = w16(S + 0x78);
                    else S = w16(S + (vt - S < 0x150 ? 5 : 0x2A));
                    /* note 05 §17.6: the clamp to Vt is reached from both rise branches, so it also applies after the negative-S rise */
                    if (S > vt) S = (int16_t)vt;
                } else if (S > vt + 0x3C) {
                    if (S > 0x1D4C) S = w16(S - 0x52);
                    else if (S - vt < 0x1E0) S = w16(S - 7);
                    else S = w16(S - 0x3C);
                }
            } else if (C >= 0) {   /* ground */
                int vt = C >> 3;
                vt = vt - (vt >> 2);
                if (!(f->b4 & 0x08) && f->throttle < 12) vt = 0;
                if (!(f->b4 & 0x44)) vt >>= 1;
                else if (f->b20 & 0x01) vt = 0;
                int r = 0;
                if (S < vt) r = S < 0 ? -0x78 : -(((vt - S) >> 8) + ((f->b4 & 0x08) ? 0x42 : 0x16));
                else if (S > vt + 15) r = 15;
                f->ground = relax(f->ground, r);
                if (r != 0) S = w16(S - f->ground);
            } else if (!ground) {   /* air, reverse */
                if (S > 0) S = w16(S - 0x57);
                else {
                    int vt = iabs32(C) - (iabs32(C) >> 2);
                    if (-S > vt) S = w16(S + 0x2A);
                    else if (-S < vt - 0x2A) S = w16(S - 0x2A);
                }
            } else {   /* ground, reverse */
                int r = 0;
                if (S > 0) r = 0x3C;
                else {
                    int q = iabs32(C) >> 4;
                    int vt = q - (q >> 2);
                    if (!(f->b4 & 0x04)) vt >>= 1;
                    if (-S > vt) r = -22;
                    else if (-S < vt - 22) r = 22;
                }
                f->ground = relax(f->ground, r);
                if (r != 0) S = w16(S - f->ground);
            }
        }
    }
    /* after the rules: overspeed (the new S) */
    /* signed comparison (rig: small negative S raises nothing) */
    if (S >= 0x20D0) {
        if (!(f->v4a98a & 0x4000)) { f->v4a98a |= 0x4000; f->v4a878 = 3; f->v4a968 = 0xFA; f->v4a84f = 2; }
    } else {
        f->v4a98a &= (uint16_t)~0x4000;
    }
    /* final selection of S: the old S unless it is the plain case */
    if ((f->w2 & (W2_HOOK | W2_ENGAGED)) == (W2_HOOK | W2_ENGAGED)) S = slew(Sold, 7, 3);
    else if (ground && brake && Sold > 0x40) S = slew(Sold, 15, 4);
    else if (ground && (f->gear & 0x80)) S = slew(Sold, 31, 5);
    else if (f->b20 & 0x01) S = slew(Sold, 3, 2);
    else if (f->b4 & 0x08) { if (iabs32(f->throttle) < 0x6C) S = slew(Sold, 7, 3); }
    f->speed_target = S;
    /* gust and sink (0x2AB64) */
    if (!f->v4a83c) {
        f->sink = 0; f->gust = 0;
    } else {
        int c = t->gust[(iabs32(S) >> 7) > 30 ? 30 : (iabs32(S) >> 7)];
        int p = pitch_w(f) >> 3;
        int k;
        /* the sine index is not guarded: p down to -4096 reads the table up to word 4996 (note 05 §17.9) */
        if (p <= 1800) k = 256 - (fl_sine(f, t, 900 - p) >> 6);
        else k = (fl_sine(f, t, p - 2700) >> 6) - 256;
        int target = w16((c * k) >> 3);
        if (!(f->gear & 0x80)) target = w16(target - (target >> 2));
        if (f->w2 & W2_GROUND) {
            f->gust = 0;
        } else {
            int g = f->gust;
            if (g > target) {
                int d = g - target;
                if (d > 255) g = g - (d >> 8);
                else if (g == 0) g = 0;
                else if (g < 0) g = 0;
                else g = g - 1;
            } else {
                g = g - ((g - target) >> 5);
            }
            f->gust = w16(g);
        }
        int wi = iabs32(Wold) >> 7;
        f->sink = t->sink[wi > 30 ? 30 : wi];
    }
    f->speed = w16(S + f->gust);
}

/* ---- section 5: control surfaces (0x1557C) ----------------------------------------------------- */

static void boost(int16_t *x, int16_t S) {
    int s = S >> 7;
    if (*x >= 0x500) *x = w16(*x + s);
    else if (*x <= -0x500) *x = w16(*x - s);
}

static int target_rate(const int16_t *tab, int cmd) {
    int a = iabs32(cmd);
    return cmd < 0 ? -tab[a] : tab[a];
}

static void pitch_rate(flight_state_t *f, int a, int ground) {
    if (!ground && f->ground != 0 && a <= 0) return;
    if (f->b20 & 0x04) a >>= 1;
    f->rate_p = w16(f->rate_p - ((f->rate_p - a) >> 2));
    boost(&f->rate_p, f->speed_target);
}

static int16_t yawlike(const flight_state_t *f, int a) {
    int ap = (a >> 1) + (a >> 3);
    if (f->b20 & 0x04) ap >>= 1;
    return w16(ap);
}

static void yaw_rate(flight_state_t *f, int a) {
    int ap = yawlike(f, a);
    f->rate_h = w16(f->rate_h - ((f->rate_h - ap) >> 2));
    boost(&f->rate_h, f->speed_target);
}

static void roll_rate(flight_state_t *f, int a) {
    int ap = yawlike(f, a);
    int m = f->cls == 0x14 ? 2 : 1;
    f->rate_r = w16(f->rate_r - ((f->rate_r - ap) >> m));
    boost(&f->rate_r, f->speed_target);
}

static void nose_settle(flight_state_t *f) {
    if (pitch_w(f) <= 0) return;   /* note 05 §17.9: pitch at most 0 (a negative word counts as level): nothing happens */
    if (pitch_w(f) < 0x190) {
        if (!(f->w2 & W2_SETTLED) && (f->w0 & 0x1600) == 0x1000 && !(f->b4 & 0x02)) f->ev_ground = 0x0F;   /* 0x15C44 (note 10 8.2): the nose settles for the first time, the active bit only, not on the catapult */
        f->w2 |= W2_SETTLED; f->rate_p = 0; return;
    }
    int c = pitch_w(f) < 0x3840 ? -0x40 : 0x40;
    int m = f->speed_target >= 0x6C0 ? 1 : 0;
    f->rate_p = w16(f->rate_p - ((f->rate_p - c) >> m));
}

static void roll_reset(flight_state_t *f) {
    f->rate_r = f->roll == 0 ? 0 : roll_w(f) > 0x3840 ? 0x20 : -0x20;
}

static void surfaces(flight_state_t *f, const flight_tables_t *t) {
    f->w2 &= (uint16_t)~W2_SETTLED;
    const int ground = (f->w2 & W2_GROUND) != 0;
    const int16_t *tab = (f->entity && !(f->w2 & 0x0100)) ? t->stick_ai : t->stick_player;   /* note 05 section 5: 0x42654 for the other entities, unless their W2 bit 0x0100 is set (the instructor, scripted records) */
    int pc = f->cmd_pitch, rc = f->cmd_rudder, qc = f->cmd_roll;
    if (f->b4 & 0x08) { rc = 0; qc = 0; }
    /* warning thresholds (flight simulation is on here), note 05 §17.5: the state of the fuel names one flag X (0x40 exhausted, 0x02 critical, 0x01 low,
     * none above the thresholds); when X is already set nothing changes, otherwise the flags 0x43 are replaced by X. The side effects of exhaustion (afterburner
     * cut, V:4A8F6 = 2) are made only in the step in which the flag is newly set. */
    {
        int x;
        if (f->fuel <= 0) { f->fuel = 0; x = 0x40; }
        else if (f->fuel < 0x70800) x = 0x02;
        else if (f->fuel < 0xBB800) x = 0x01;
        else x = 0;
        if (x == 0) f->v4ac08 &= ~0x43;
        else if (!(f->v4ac08 & x)) {
            f->v4ac08 = (f->v4ac08 & ~0x43) | x;
            /* the side effects of exhaustion are made when the flag is newly set (rig: with 0x40 already set they are not) */
            if (x == 0x40) { f->w2 &= (uint16_t)~W2_AB; f->v4a8f6 = 2; }
        }
    }
    /* pitch */
    if (pc != 0) {
        int a = target_rate(tab, pc);
        if (!ground) pitch_rate(f, a, 0);
        else if ((f->b20 & 0x01) || f->speed_target <= 0x300 || (f->pitch != 0 && pitch_w(f) <= 0x6D60)) {
            nose_settle(f); roll_reset(f);
        } else if (pc < 0) {
            pitch_rate(f, -tab[iabs32(pc) >> 1], 1);
        } else {
            nose_settle(f); roll_reset(f);
        }
    } else if (ground) {
        nose_settle(f);
    } else if (f->rate_p != 0 && f->ground == 0) {
        f->rate_p = decay(f->rate_p, f->entity ? 1 : 2);   /* rig (tests/rig/test_training_traj.c): a non-player record's pitch rate halves, the player's loses a quarter */
    }
    /* rudder */
    if (rc != 0) {
        if (!(f->b4 & 0x02) && f->speed != 0) yaw_rate(f, target_rate(tab, rc));
        else f->rate_h = decay(f->rate_h, 1);
    } else if (!(qc != 0 && ground)) {
        if (f->rate_h != 0) f->rate_h = decay(f->rate_h, 1);
    }
    /* roll */
    if (qc != 0) {
        int ql = qc;
        if (ground) ql = qc > 10 ? 10 : qc < -10 ? -10 : qc;
        int a = target_rate(tab, ql);
        if (!ground) {
            roll_rate(f, -a);
        } else if (!(f->b4 & 0x02) && f->speed != 0) {
            yaw_rate(f, a);
            /* note 05 §17.7: roll-rate when (the roll is below 0x50 or above 0x7030) and W > 0x360, signed compares */
            if ((roll_w(f) < 0x50 || roll_w(f) > 0x7030) && f->speed > 0x360) roll_rate(f, -a);
            else if (roll_w(f) < 0x3840) { if (ql >= 0) roll_rate(f, -a); else f->rate_r = 0; }
            else { if (ql <= 0) roll_rate(f, -a); else f->rate_r = 0; }
        } else {
            f->rate_h = decay(f->rate_h, 1);
            f->rate_r = decay(f->rate_r, 1);
        }
    } else {
        if (ground) roll_reset(f);
        if (f->rate_r != 0) f->rate_r = decay(f->rate_r, 1);
    }
    /* last: the stall warning */
    if (f->fuel == 0) {
        f->v4ac08 &= ~0x80;   /* note 05 §17.7: the stall block runs with fuel not 0 only; in every other case it clears the stall flag */
    } else {
        if ((f->b20 & 0x04) && !(f->w2 & W2_GROUND)) {
            if (!(f->v4ac08 & 0x80)) { f->v4ac08 |= 0x80; f->v4ac0c |= 0x20; }
            if (f->v4a9a8 < 0) f->v4a9a8 = 2;
        } else {
            f->v4ac08 &= ~0x80;
        }
    }
}

/* ---- section 6: attitude --------------------------------------------------------------------- */

/* the quotient of the composition: the divisor d is shared by all quotients of one composition */
static int16_t quot(int32_t v, int16_t *d) {
    int32_t num, div, q, rem;
    int round = 0;
    if (*d <= 0x147) {
        *d = w16(*d * 64);
        num = v;
    } else {
        num = v >> 6;
        round = 1;
    }
    div = *d;
    if (div == 0) { q = 0x10000; rem = 0; }
    else { q = (int32_t)((int64_t)num / div); rem = (int32_t)((int64_t)num % div); }   /* 64 bits: INT32_MIN / -1 must not overflow in C */
    if (q < -32768 || q > 32767) { q = w16(num); rem = w16(num >> 16); }
    if (round && iabs32(rem) >= (iabs32(div) >> 1)) q = q >= 0 ? q + 1 : q - 1;
    return w16(q);
}

/* the inverse-sine tables, 0x42E92 + byte offset (note 05 §17.9: the heading and roll paths use the 16-bit quotient as an index with no range guard) */
static int16_t asin_word(flight_state_t *f, const flight_tables_t *t, int32_t byte_off) {
    return fl_image_word(f, t, (uint32_t)((int32_t)FLIGHT_ADDR_ASIN1 + byte_off));
}
static int16_t abs16(int16_t v) { return v < 0 ? w16(-v) : v; }   /* neg.w: -32768 stays -32768 */
/* Note 05 17.18 a (0x325B8): a quotient q (16 bits) is looked up at the byte offset = the signed 16-bit value of 2|q| from 0x42E92 (|q| = 32768 gives 0). For |q| of
 * 16384 and more the offset is negative and the read lands in 0x3AE92..0x42E91, which the extracted span 2 covers (0x3AE92..0x49A12); the reads above 0x49A12 are
 * the run-time block (Q16). The sign test that picks the branch is the TRUE sign of q (the doubling overflows into the word's sign for |q| >= 16384). */
static int16_t asin_off(flight_state_t *f, const flight_tables_t *t, int32_t q) { return asin_word(f, t, w16(2 * (q < 0 ? -q : q))); }
static int16_t asin_off_value(int32_t q) { return w16(2 * (q < 0 ? -q : q)); }

/* 0x325B8: the composition of the attitude matrix with three small rotations; out = pitch, heading, roll words (angle units, wrapped to words) */
static void compose(flight_state_t *f, const flight_tables_t *t, int pr, int yr, int qr, int32_t *npitch, int32_t *nheading, int32_t *nroll) {
    int16_t bm[9];
    matrix_a(f, t, (uint16_t)(pr < 0 ? pr + 28800 : pr), (uint16_t)(yr < 0 ? yr + 28800 : yr), (uint16_t)(qr < 0 ? qr + 28800 : qr), bm);
    int32_t P[9];
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++)
            P[3 * j + i] = ad(ad(mu(f->mat_a[i], bm[3 * j]), mu(f->mat_a[3 + i], bm[3 * j + 1])), mu(f->mat_a[6 + i], bm[3 * j + 2]));
    /* pitch */
    int32_t s = sb(0, P[7]);
    uint32_t as = s < 0 ? 0u - (uint32_t)s : (uint32_t)s;
    int tbl, idx;   /* table start in words from 0x42E92, signed index */
    if (as < 0xE210000u) {
        int hw = s >> 16;
        idx = (hw >> 4) + ((hw >> 3) & 1);
        tbl = 0;
    } else if (as < 0xFF60000u) {
        int j = (int)((as >> 16) + ((as >> 15) & 1)) - 0xDDB;
        if (j > 0x225) j = 0x225;   /* note 05 §17.9: the pitch path clamps its table index */
        idx = s < 0 ? -j : j;
        tbl = 259;
    } else {
        int j = (int)((as >> 12) + ((as >> 11) & 1)) - 0xFF08;
        if (j > 0xF8) j = 0xF8;
        idx = s < 0 ? -j : j;
        tbl = 259 + 552;
    }
    int pt, kk;
    if (idx >= 0) { pt = asin_word(f, t, 2 * (tbl + idx)); kk = 900 - pt; }
    else { int tv = asin_word(f, t, 2 * (tbl - idx)); pt = 3600 - tv; kk = 900 - tv; }
    int16_t cs = fl_sine(f, t, kk);
    if (cs < 0x8F) { pt = idx >= 0 ? 910 : 2690; cs = -286; }
    int16_t d = cs;
    /* heading, then roll (the divisor d is shared) */
    int32_t a_[2];
    for (int w = 0; w < 2; w++) {
        int32_t pa = w == 0 ? P[6] : P[1], pb = w == 0 ? P[8] : P[4];
        int16_t x = quot(sb(0, pa), &d);
        int ang;   /* a 16-bit word throughout (a looked-up word can be large with the span rule: 900 - 0x8008 must wrap before the comparisons) */
        if (asin_off_value(x) <= 0x180) ang = asin_off(f, t, x);   /* signed 16-bit offset at most 0x180 (also a negative one): the first lookup */
        else { int16_t x2 = quot(pb, &d); ang = w16(900 - asin_off(f, t, x2)); }
        if (x < 0) ang = w16(3600 - ang);   /* the true sign of 2q */
        if (pa >= 0 && pb >= 0) { }
        else if (pa >= 0 && pb < 0) ang = w16((ang > 1800 ? 5400 : 1800) - ang);
        else if (pa < 0 && pb < 0) ang = w16(1800 - ang);
        a_[w] = abs16((int16_t)ang);
    }
    *npitch = w16(pt * 8); *nheading = w16(a_[0] * 8); *nroll = w16(a_[1] * 8);
}

static int32_t recentre(int32_t r) {   /* step 4: roll recentre */
    int32_t target;
    if (r < 0x1C20) target = 0;
    else if (r < 0x5460) target = 0x3840;
    else target = 0x7080;
    int32_t dist = iabs32(target - r);
    int32_t step = dist == 1 ? 1 : (dist >> 1);
    if (step > 0x50) step = 0x50;
    if (dist == 0) return target == 0x7080 ? 0 : target;
    int32_t n = r < target ? r + step : r - step;
    if (n >= 0x7080) n = 0;
    return n;
}

/* Sine and cosine of an angle in tenths of a degree by the folding of 0x32DB2 (note 05, "Tables in the image") over the address-mapped sine table: the rule has no
 * range guard, so an index outside 0..900 reads the words around the table (§17.9). The coupling's angle is a signed value (the roll word >> 3, arithmetic). */
static void fl_sincos(flight_state_t *f, const flight_tables_t *t, int a, int *s, int *c) {
    if (a < 900) { *s = fl_sine(f, t, a); *c = fl_sine(f, t, 900 - a); }
    else if (a < 1800) { *s = fl_sine(f, t, 1800 - a); *c = w16(-fl_sine(f, t, a - 900)); }
    else if (a < 2700) { *s = w16(-fl_sine(f, t, a - 1800)); *c = w16(-fl_sine(f, t, 2700 - a)); }
    else { *s = w16(-fl_sine(f, t, 3600 - a)); *c = fl_sine(f, t, a - 2700); }
}

/* Note 07 8.3 (0x31D12), the response of the non-aircraft classes (the missiles): the targets +0x50, +0x52, +0x54 become the rates +0x56, +0x58, +0x5A. The divisor D is 10 with the lost
 * mark (B20 bit 5), else 5 when the missile is jammed (aimed at the player with the ECM on: the gate sets `jam`), else the ramp +0x26 limited to 15; none when +0x26 is 0 or below. The
 * low pass is new = ((old + q) / 2 + q) / 2 in 16-bit words with arithmetic shifts, q the quotient (divs.w: signed, truncating). */
static void missile_response(flight_state_t *f) {
    int D;
    if (f->b20 & 0x20) D = 10;
    else if (f->jam) D = 5;
    else D = f->ground > 15 ? 15 : f->ground;
    const int tgt[3] = { f->lvl_on ? 0 : (int16_t)f->turn50, f->lvl_on ? 0 : (int16_t)f->turn52, f->lvl_on ? (int)f->lvl_roll : (int16_t)f->turn54 };   /* RIG: the levelling replaces all three targets */
    int16_t *rate[3] = { &f->rate_p, &f->rate_h, &f->rate_r };
    for (int i = 0; i < 3; i++) {
        const int q = (D > 0 && !(i == 2 && f->lvl_on)) ? tgt[i] / D : tgt[i];   /* RIG: the levelling target is not divided */
        const int a = w16(*rate[i] + q) >> 1;
        *rate[i] = w16((w16(a + q)) >> 1);
    }
}

/* The bank-turn coupling 0x32426 (note 05 17.8): updates +0x22, +0x24, +0x50, +0x52, +0x54 and V:4A9FE and adds its contribution to the pitch and yaw rates *pr and *yr. */
static void coupling(flight_state_t *f, const flight_tables_t *t, int *ppr, int *pyr) {
    int pr = *ppr, yr = *pyr;
    {
        int r = roll_w(f) >> 3;
        int a, state;
        int neg = 0;
        if (r < 900) { a = r; state = 0; }
        else if (r < 1800) { a = 1800 - r; state = 1; neg = 1; }
        else if (r < 2700) { a = 3600 - r; state = 1; neg = 1; }
        else { a = r - 1800; state = 0; }
        int s, c;
        fl_sincos(f, t, a, &s, &c);
        if (neg) c = -c;
        f->roll_sin = w16(s); f->roll_cos = w16(c);
        if (state == 0) { if (f->turn50 != 0) f->turn54 = w16(-f->turn54); f->turn50 = 0; }
        else { if (f->turn50 == 0) f->turn54 = w16(-f->turn54); f->turn50 = -1; }
        int m = iabs32(s);
        if (c < 0) m = -m;
        int d = f->turn54 - m;
        int stp = d == 0 ? 0 : iabs32(d) > 7 ? (d >> 3) : (d < 0 ? -1 : 1);
        f->turn54 = w16(f->turn54 - stp);
        int mag = iabs32(f->turn54);
        if ((f->turn54 < 0 && c < 0) || (f->turn54 >= 0 && c > 0)) f->turn52 = f->turn50;
        else if ((c > 256 || c < -256) && ((f->turn50 < 0) == (f->turn52 < 0))) { s = -s; c = -c; }
        /* note 05 §17.8: on the wrong side, with c beyond +-256, and +0x50 and +0x52 equal, both sine and cosine are negated; otherwise nothing */
        if (mag > f->turn_limit) mag = f->turn_limit;
        mag = mag - (mag >> 2);
        int32_t d3 = (((-(int32_t)s * 128) >> 13) * mag) >> 13;
        int32_t d5 = (((-(int32_t)c * 128) >> 13) * mag) >> 13;
        int pitch_f = pitch_w(f) >= 14400 ? 28800 - pitch_w(f) : pitch_w(f);
        if (pitch_f <= 3600) {
            /* the scale index is the pitch fold >> 8 (arithmetic): a negative pitch word reads the words in front of the table (note 05 §17.9) */
            int sc = fl_image_word(f, t, (uint32_t)((int32_t)FLIGHT_ADDR_PITCH_SCALE + 2 * (pitch_f >> 8)));
            d3 = (d3 * sc) >> 8;
            d5 = (d5 * sc) >> 8;
            pr = w16(pr + d3);
            yr = w16(yr + d5);
            f->v4a9fe = w16(d3);
        } else {
            f->v4a9fe = 0;   /* note 05 §17.8: with the pitch fold beyond 3600 the contributions are 0 and V:4A9FE = 0 */
        }
    }
    *ppr = pr; *pyr = yr;
}

static void attitude_step(aircraft_t *ac, const flight_tables_t *t) {
    flight_state_t *f = &ac->f;
    const int missile = f->entity && (f->cls & 0xF0) == 0;
    if ((f->v4a984 & 0x40) && !missile) surfaces(f, t);   /* note 05 §17.7: 0x1557C is not called without V:4A984 bit 6, the flags V:4AC08 stay as they are */
    int pr = f->v4a867 ? 0 : f->rate_p, yr = f->v4a867 ? 0 : f->rate_h, qr = f->v4a867 ? 0 : f->rate_r;
    /* 2. bank-turn coupling (0x32426). Note 05 17.8: with V:4A867 set the routine is not called (+0x22, +0x24, +0x50, +0x52, +0x54 and V:4A9FE keep their values).
     * The routine is entered from 0x31AE0 only with W0 bit 0x8000 clear, so its 0x8000 branch (V:4A9FE = 0) cannot be reached: C11 withdrawn, 17.18 g. */
    if (missile) {
        /* RIG (tests/rig/test_weapons_diff.c): the bank-turn coupling does not run for a missile (its +0x22, +0x24, +0x50, +0x52, +0x54 are the owner's copy and the steering's); the
         * response 0x31D12 turns the targets into the rates (note 07 8.3) */
        if (f->v4a984 & 0x40) missile_response(f);
        pr = f->v4a867 ? 0 : f->rate_p; yr = f->v4a867 ? 0 : f->rate_h; qr = f->v4a867 ? 0 : f->rate_r;
    } else if ((f->v4a847 || f->entity) && !f->v4a867) {   /* note 08 8.3: a non-player record is coupled whenever the block runs (neither V:4A866 nor V:4A867), the player only with V:4A847 as well */
        coupling(f, t, &pr, &yr);
    }
    /* 3. composition */
    int32_t np, nh, nr;
    compose(f, t, pr, yr, qr, &np, &nh, &nr);
    const int32_t old_pitch = pitch_w(f), old_heading = heading_w(f), old_roll = roll_w(f);
    /* 4. roll recentre */
    if (f->w2 & W2_RECENTRE) { f->w2 &= (uint16_t)~W2_RECENTRE; nr = recentre(old_roll); }
    /* 5. pitch override */
    int override = 0;
    if (f->state == 0x0A || (f->w2 & W2_SETTLED)) {
        /* note 05 §17.8 (order 5): the drift starts from the stored pitch (the composed pitch is discarded); the wreck state's step 0x50 wins over 0xA0 */
        int amount = f->state == 0x0A ? 0x50 : 0xA0;
        np = old_pitch;
        if (np == 0) { }
        else if (np < 0x3840) { np -= amount; if (np < 0) np = 0; }
        else { np += amount; if (np >= 0x7080) np = 0; }
        override = 1;
    }
    /* 6. stall: skipped while the pitch override of step 5 applies (note 05 17.8, rig: the ground term and the flag stay untouched) */
    if (f->v4a83c && !override) {
        int nose = 0;
        if (f->b20 & 0x02) nose = 1;
        else if (!(f->w2 & W2_GROUND) && (f->cls & 0xF0) == 0x10) {
            int p2 = (int)(old_pitch >> 1);   /* the stored pitch word, as for the nose drop below */
            if (p2 < 0x1C20) p2 = (p2 < 0xE10 ? p2 : 0xE10) + 0x3840;
            uint32_t u = (((uint32_t)(0x4650 - p2) & 0xFFFFu) * 108u) >> 8;
            uint32_t sn = (uint32_t)(uint16_t)(f->roll_sin >> 8);
            int16_t Th = w16((int32_t)(u + ((sn * u) >> 6)));
            if (Th >= f->speed) {
                f->b20 |= 0x04;
                if ((f->rate_p == 0 && f->rate_h == 0 && f->rate_r == 0) || (Th >> 1) >= f->speed) nose = 1;
            } else {
                f->ground = 0; f->b20 &= (uint8_t)~0x04;
            }
        }
        if (nose) {
            const int override_before = override;
            f->rate_p = 0;
            f->ground = w16(f->ground + ((0x50 - f->ground) >> 2));
            /* note 05 §17.8 (order 6): the nose drop adds the new +0x26 to the STORED pitch (worked example E7: pitch 101 at step 6); the composed pitch is discarded */
            override = 1;   /* note 05 §17.8: a nose drop is also an override for the gimbal guard: the angles are stored without it */
            np = override_before ? np + f->ground : old_pitch + f->ground;
            /* note 05 §17.9: a negative result (+0x26 negative near level) is kept at or below 0x12C0; the matrices are then built from the angle word
             * outside 0..28799 with the table reads of matrix_a and matrix_b (address-mapped, no range guard) */
            if (np >= 0x7080) np = 0;
            else if (np >= 0x3840) { }
            else if (np <= 0x12C0) { }
            else np -= 0x50;
        }
    }
    /* 7. gimbal guard */
    int store = 1;
    if (!override && old_pitch > 0x1770 && old_pitch < 0x5910) {
        /* note 05 §6 item 7 / §17.9 (rig-verified): every difference is a 16-bit subtraction, a branch on the TRUE signed comparison of the operands, then a
         * 16-bit negate when the first is smaller; read back as a signed word, so a true difference of 32768 or more wraps negative and is not negated again */
        int dh = gg_diff(nh, old_heading), dr = gg_diff(nr, old_roll);
        int hf = 0, rf = 0;
        if (dh > 0x4B0 && dh < 0x6BD0) { hf = 1; dh = gg_diff(dh, 0x3840); }
        if (dr > 0x4B0 && dr < 0x6BD0) { rf = 1; dr = gg_diff(dr, 0x3840); }
        if (!rf) store = !hf;
        else store = gg_diff(dr, dh) < 0x320;
        if (!store) {
            if (!(f->w2 & W2_GIMBAL)) {
                f->w2 |= W2_GIMBAL;
                /* note 05 §17.7: the kick is the word at 0x425E4 (0x90) in the direction of the rate's own sign, zero counting as positive */
                if (f->cmd_pitch != 0) f->rate_p = w16(f->rate_p + (f->rate_p < 0 ? -0x90 : 0x90));
            } else {
                f->rate_h = w16(f->rate_h + (f->rate_h < 0 ? -0x90 : 0x90));
            }
        }
    }
    if (store) {
        f->w2 &= (uint16_t)~W2_GIMBAL;
        f->pitch = (uint16_t)np; f->heading = (uint16_t)nh; f->roll = (uint16_t)nr;
        build_matrices(f, t);
    }
}

/* ---- section 12: the warnings the state posts (step item d) ------------------------------------ */

static int32_t mul32(int32_t a, int32_t b) { return (int32_t)(uint32_t)((uint64_t)((int64_t)a * b) & 0xFFFFFFFFu); }

static int post(flight_state_t *f, uint16_t code) {
    /* note 05 §17.13: the post routine 0x29BC8 accepts every post (it stores the code and sets V:4A984 bit 0); the caller makes a post only when V:4AB98
     * differs from its code, whatever the latch was. A refused post does nothing. */
    if (f->v4ab98 == code) return 0;
    f->v4ab98 = code;
    f->v4a984 |= 1;
    return 1;
}

/* The direct post the state asks for in this step (section 12), before the acceptance test: 0xD02A (eject warning), 0x9C06 (crash imminent) or 0.
 * Note 05 §17.13 (order): when out of control (B20 bit 0x02) and descending (+0x42 < 0) only the eject warning is considered (made when there is no ejection
 * nibble and |+0x42| * 64 >= y; with an ejection nibble nothing is posted, not even the bank warning); otherwise the crash-imminent clauses: a descent that reaches
 * the ground within 16 steps at a nose-down attitude, or a bank beyond 15 degrees in the air at y <= 0x1800. The proximity post 0xD00A (clause 3) needs W2 bit 0
 * set, which the world update clears before every player step and only the later records' steps set: it cannot occur in the player's step. */
static uint16_t direct_post_code(const flight_state_t *f, int32_t y) {
    int32_t dy = f->vel[1];
    if ((f->b20 & 0x02) && dy < 0)
        return (!(f->gear & 0x0F) && mul32(-dy, 64) >= y) ? 0xD02A : 0;
    if (f->w2 & W2_GROUND) return 0;   /* note 05 §17.13: neither crash-imminent clause posts with the wheels down */
    if (pitch_w(f) >= 0x1E0 && pitch_w(f) <= 0x3840 && dy < 0 && mul32(-dy, 16) >= y) return 0x9C06;
    if (roll_w(f) > 0x4B0 && roll_w(f) < 0x6BD0 && y <= 0x1800) return 0x9C06;
    return 0;
}

uint16_t flight_direct_post_code(const aircraft_t *ac) { return direct_post_code(&ac->f, ac->pos[1]); }

static int state_warnings(flight_state_t *f, int32_t y) {
    uint16_t code = direct_post_code(f, y);
    if (code && post(f, code)) {
        if (code == 0xD02A) f->v4ac0c |= 0x40;
        else { f->v4ac0c |= 0x80; return 1; }   /* the crash-imminent post skips the speed step of this step */
    }
    return 0;
}

/* ---- section 7: motion (0x16B0E) ---------------------------------------------------------------- */

/* the target velocity of 0x29C18 with the length of 0x20884; comp[] is in the routine's sign convention */
static void target_velocity(const flight_state_t *f, const flight_tables_t *t, int32_t comp[3]) {
    comp[0] = comp[1] = comp[2] = 0;
    int sp = w16(-f->speed);
    if (sp == 0) return;
    int nx = f->mat_b[2], ny = f->mat_b[5], nz = f->mat_b[8];
    uint32_t ax = (uint32_t)iabs32(nx), ay = (uint32_t)iabs32(ny);
    uint32_t a = ax > ay ? ax : ay, b = ax > ay ? ay : ax;
    uint32_t i = (b == 0 || a == 0) ? 0 : (256u * b) / a;
    if (i > 256) i = 256;
    uint32_t h = a * (uint32_t)(uint16_t)t->len[i];
    uint32_t w = (uint32_t)iabs32(nz) * 16384u;
    uint32_t mx = h > w ? h : w, mn = h > w ? w : h;
    uint32_t A = (mx >> 14) & 0xFFFFu;
    uint32_t j = A == 0 ? 0 : (mn / A) >> 6;
    if (j > 256) j = 256;
    uint32_t len = ((uint32_t)(uint16_t)t->len[j] * A) >> 14;
    if (len > 0x7FFF) len = 0x7FFF;
    if (len == 0) return;
    uint32_t asp = (uint32_t)iabs32(sp), m;
    int e;
    if (asp <= len) {
        int k = 1;
        m = asp * 4;
        while (m <= len) { m *= 4; k++; }
        e = 8 + 2 * k;
    } else {
        int jj = 1;
        while (jj < 4 && (asp >> (2 * jj)) > len) jj++;
        m = 4 * (asp >> (2 * jj));
        e = 10 - 2 * jj;
    }
    uint32_t q = (256u * m) / len;
    if (q > 0xFFFFu) q = (256u * m) & 0xFFFFu;
    int n[3] = { nx, ny, nz };
    for (int i2 = 0; i2 < 3; i2++) {
        int32_t v = (int32_t)(((int64_t)n[i2] * (int64_t)q) >> e);
        comp[i2] = sp < 0 ? -v : v;
    }
}

/* The rearm routine 0x1F11E, the one body (the landing's call and the port's W key use it): it posts the code 0x4005 and sets the latch unconditionally (no test of the code already on the line: the
 * "not again while V:4AB98 holds the code" of note 05 17.13 belongs to the three direct posts of the flight step), and sets the fuel to 0x61A800; the stores (+0x5F, +0x60, V:4A8FB..., the flare and chaff
 * stores and the records 1 to 3, note 07 7.1, 9.7) are the game's weapons_rearm, which the step cannot reach: ev_rearm asks for it. */
void flight_rearm(flight_state_t *f) {
    f->v4ab98 = 0x4005; f->v4a984 |= 1;
    f->fuel = 0x61A800;
    f->ev_rearm = 1;
}

static void landing_complete(flight_state_t *f, const flight_env_t *env) {   /* 0x1F11E */
    if (env->no_rearm) return;   /* the rig patches the whole routine out (section 1, rig convention) */
    flight_rearm(f);
}

/* The motion step: velocity, floor, touchdown and take-off. pos is the position BEFORE the displacement. Returns nothing; updates
 * f->vel and the altitude in ac->pos[1]. */
/* The entity ground contact is note 07 4.12 (rig tests/rig/test_ai_traj.c AI_TRAJ_DIVE: 900 of 900 dives agree); the x drift of the ground rows is settled there (the aircraft takes its x and z velocity at once on the floor, the missile never; airborne the speed words differ by 4 because the speed step reads the height, so x and z differ by 4 to 20 units: note 07 4.12). */
static void motion_step(aircraft_t *ac, const flight_env_t *env) {
    flight_state_t *f = &ac->f;
    const flight_tables_t *t = env->tab;
    int32_t comp[3];
    target_velocity(f, t, comp);
    int32_t v[3];
    for (int ax = 0; ax < 3; ax++) {
        v[ax] = -f->vel[ax] / 4;
        if (ax == 1 || !(f->w2 & W2_GROUND)) {
            int32_t d = comp[ax] - v[ax], ad = iabs32(d);
            int s = ad > 4 ? 2 : ad >= 3 ? 1 : 0;
            v[ax] += d >> s;
        } else {
            v[ax] = comp[ax];
        }
        if (ax == 1 && !(f->v4a857 & 1)) v[ax] += f->sink;
        f->vel[ax] = -4 * v[ax];
    }
    const int32_t vy = v[1];
    int32_t y = fl_add(ac->pos[1], f->vel[1]);
    const int notdown = (f->gear & 0x70) != 0;
    int32_t c = notdown ? 0x2EE : (f->cls == 0x14 ? 0xA8C : 0x708);
    int32_t F;
    if (f->b4 & 0xC0) { F = fl_add(fl_shl(f->deck, 8), c); if (F < c) F = c; }
    else F = (f->cls & 0xF0) == 0 ? -0x200 : c;
    if (y > F) {
        if (f->w2 & W2_GROUND) {   /* lift-off */
            f->w2 &= (uint16_t)~W2_GROUND;
            if (f->entity) {
                /* note 07 4.12: the ground bit and B4 bit 3 (catapult armed) are cleared, the jolt kind 4 is requested and the hook 0x1F0E2 runs instead of the player's flight-clock bookkeeping */
                f->b4 &= (uint8_t)~0x08;
                f->ev_jolt = 4;
                if (!env->no_liftoff_hook) f->gear |= 0x80;   /* 0x1F0E2 (note 07 4.7): the gear-up command for a record with the wheels off the ground */
            } else {
                if (f->b4 & 0x08) f->v4a878 = 4;
                f->ev_liftoff = 1;   /* 0x171E2: the lift-off event of the log's clock (note 08 10.7) */
                f->ev_jolt = 1;      /* 0x171D4 */
                f->b4 &= (uint8_t)~0x0A;
                f->b21 |= 1;
                f->w2 &= (uint16_t)~W2_SKID;
                f->v4a98a &= (uint16_t)~2;
                f->v4a982 |= 0x100;
            }
        } else if (y > f->v4a7b2) {
            y = f->v4a7b2;
        }
    } else {
        f->b20 &= (uint8_t)~0x04;
        if ((f->cls & 0xF0) != 0) y = F;
        int queried = 0;
        if (!(f->b4 & 0xC0) && (!(f->w2 & W2_GROUND) || (f->counter & 7) == 0)) {
            fl_terrain_query(f, t, ac->pos[0], ac->pos[2], &f->b4);
            queried = 1;
        }
        /* note 05 §17.10: the gear is lost over water (and the skid set) only in a step in which the terrain query ran */
        if (queried && (f->b4 & 0x02) && !notdown) { f->gear |= 0x80; if ((f->cls & 0xF0) != 0) f->b20 |= 0x01; }   /* RIG (test_weapons_diff.c): a missile loses the gear byte's bit but gets no skid bit */
        if (f->entity) {
            /* note 07 4.12: the touchdown block (landing statistics, gear collapse, hard-landing tests) is skipped for a non-player record on EVERY step with the height at or below the floor:
             * 0x16F30 jumps to 0x17180, which sets W2 bit 0x0080 and, when the indicated speed +0x6E is below 0x3C0, clears W2 bit 0x1000 (the skid or smoke-trail bit). The active bit is untouched. */
            f->w2 |= W2_GROUND;
            if (f->speed < 0x3C0) f->w2 &= (uint16_t)~0x1000;
        } else if (f->w0 & W0_CRASHED) {
            f->vel[1] = 0;
        } else if (!(f->w2 & W2_GROUND)) {   /* touchdown */
            f->w2 |= W2_GROUND; if ((f->cls & 0xF0) != 0) f->ground = 0; f->v4a857 = 0; f->v4a878 = 1;   /* RIG (test_weapons_diff.c): a missile's +0x26 counter goes on */
            f->ev_touchdown = 1;   /* 0x16F6A: the touchdown event of the log's clock (note 08 10.7) */
            if (vy > 0x180) f->ev_jolt = 1;   /* the touchdown jolt (note 08 8.10, note 06): vertical speed above 0x180 */
            if ((f->cls & 0xF0) == 0) {   /* RIG (test_weapons_diff.c): a missile's touchdown sets the ground bit and nothing else */
            } else if (vy >= 0x240) {
                /* note 05 §17.10: a hard touchdown crashes only with V:4A952 = 0; with it set it does neither the crash nor S = W */
                if (f->v4a952 == 0) { f->w0 |= W0_CRASHED; f->v4a87d = 1; f->v4a850 = 4; f->ev_crash = 1; }   /* 0x16FB0: the crash counter's event (note 08 10.2) */
            } else if (f->speed != 0) {
                f->speed_target = f->speed; f->v4a968 = 0xFE;
                if (((f->b4 & 0x02) || notdown) && f->speed >= 0x3C0) {
                    f->w2 |= W2_SKID;   /* 0x17022: the skid or crash trail, and the camera follow V:54E72 (0x17024; the ring is weapons.c's, note 07 13.1) */
                    if (env->follow) env->follow(env->follow_user, f);
                }
                f->ev_ground = vy > 0x180 ? 0x28 : 0x1E;   /* 0x1703A, 0x1704A (note 10 8.2): the touchdown's contact sound by the descent per step */
            }
            if ((f->cls & 0xF0) != 0) f->vel[1] = 0;   /* RIG (tests/rig/test_weapons_diff.c): a missile keeps its vertical velocity, the ground result backs it up along it */
        } else {   /* rolling */
            if (f->speed != 0) {
                if (f->v4a98a & 2) { f->v4a98a &= (uint16_t)~2; f->v4a878 = 3; f->v4ac0c |= 0x200; }
            } else {
                if (!(f->v4a98a & 2)) {
                    f->v4a878 = 4;
                    if (f->v4a982 & 0x100) {
                        f->v4a982 &= (uint16_t)~0x100;
                        if ((f->b4 & 0x04) || (f->w2 & (W2_HOOK | W2_ENGAGED)) == (W2_HOOK | W2_ENGAGED)) landing_complete(f, env);
                    }
                    f->v4a98a |= 2;
                }
                if (f->fuel == 0) f->v4a986 |= 0x4000;
            }
            if (!f->entity) f->vel[1] = 0;   /* rig (tests/rig/test_training_traj.c, part 2): a non-player record keeps its vertical velocity words while it rolls */
        }
        if (f->speed < 0x3C0) f->w2 &= (uint16_t)~W2_SKID;
    }
    ac->pos[1] = y;
}

/* ---- section 8: gear and hook counters ---------------------------------------------------------- */

/* Returns 1 in the step in which the ejection count reaches 3 (the seat spawn of the world update follows the whole player step: the event ev_seat, entities.c). */
static int gear_counters(flight_state_t *f) {
    int count3 = 0;
    uint8_t g = f->gear;
    int prog = (g >> 4) & 7;
    int np = prog;
    if ((g & 0x80) && prog < 6) np = prog + 1;
    else if (!(g & 0x80) && prog > 0) np = prog - 1;
    /* note 05 §17.10: the sound request V:4A8FD = 3 on every change of the retract progress, rising or falling */
    if (np != prog) f->v4a8fd = 3;
    if (np != prog) {
        f->gear = (uint8_t)((g & 0x80) | (np << 4));   /* stored with the low nibble cleared */
        /* Note 05 17.18 c: the low nibble L is processed first and the bit W2 0x0020 is cleared whenever L was 6 with the bit set, in a step in which the
         * progress changes as well (rig: envelope 421/2775, adversarial 483/1230/1395/1986/2408/2462/2538/2550 of the run of 3000 at round 3); no other
         * nibble touches the bit. The progress change then stores the byte with the nibble 0. */
        if ((g & 15) == 6) f->w2 &= (uint16_t)~0x0020;   /* a clear of a clear bit changes nothing */
    } else {
        int n = g & 15;
        if (n != 0) {
            if (n < 6) {
                n++;
                if (n == 3) count3 = 1;
            }
            else if (n == 6) { if (f->w2 & 0x0020) { n = 0; f->w2 &= (uint16_t)~0x0020; } }
            else if (n > 7) n--;
            else n = 1;
        }
        f->gear = (uint8_t)((g & 0xF0) | n);
    }
    /* the hook lamp V:4A8FF counts 0..5 per step toward its target (bit 7 = lit) */
    {
        int cnt = f->v4a8ff & 0x7F;
        if (f->v4a8ff & 0x80) { if (cnt < 5) cnt++; } else if (cnt > 0) cnt--;
        f->v4a8ff = (uint8_t)((f->v4a8ff & 0x80) | cnt);
    }
    return count3;
}

/* ---- one step ------------------------------------------------------------------------------------ */

/* The handler of a collision result for the player (note 05 §17.12, "The result handler for the player, in order"). r is 0x10 (ground test), 0x20 (hull or landmark) or 0x40 (an aircraft
 * contact: the damage handler of note 07 10.4, or with an ejection nibble the marks and the removal of 13.1). */
static void handle_result_entity(aircraft_t *ac, int r, const int32_t before[3]);
static void remove_record(flight_state_t *f);

/* Note 07 10.4, the player's damage handler for the aircraft contact (result 0x40) without an ejection nibble. The parity of the frame counter V:4A992 chooses the outcome; the
 * flare and chaff cool-downs V:4A907 / V:4A906 (set to 30 by the keys, decreased once per unpaused pass) protect the player from the engine failure; the posts go through the post routine
 * 0x29BC8, which accepts repeats here; the jolt kind 1 is stored in V:4A878 (the jolt machine of weapons.c consumes it). */
static void player_damage(flight_state_t *f) {
    if (!f->v4a844) return;                    /* collisions off: nothing happens, not even the hit bit */
    f->w0 |= W0_HIT;
    if (f->speed <= 0x3C0) return;             /* an indicated speed of 0x3C0 or less: no damage, no post */
    if (f->v4a952) return;                     /* immune (the crash hold) */
    if (!(f->v4a992 & 1)) {
        if (f->v4a907 > 0 || f->v4a906 > 0) return;   /* a decoy released in the last 29 passes: nothing happens */
        f->b20 |= 0x02;                        /* out of control */
        f->v4a984 |= 0x0100;                   /* the damage bit (its only reader is the collisions-off post, note 05 17.17) */
        f->v4ab98 = 0x9014; f->v4a984 |= 1;    /* the engine-failure alert */
        f->v4a878 = 1;
    } else {
        f->v4ab98 = (f->v4a992 & 2) ? 0xD00C : 0xD00B; f->v4a984 |= 1;
        f->v4a878 = 1;
    }
}

static void handle_result(aircraft_t *ac, const flight_env_t *env, int r, const int32_t before[3]) {
    flight_state_t *f = &ac->f;
    const int nibble = (f->gear & 0x0F) != 0, ext = f->v4a83d != 0;
    int dying = 0;
    if (f->entity) { handle_result_entity(ac, r, before); return; }
    /* 1. R = 0x20: E, Y and Q are zeroed; for class not 0 the displacement is undone (x and z, and y when W2 bit 0x0080 is clear). All results: the cell words are refreshed */
    if (r == 0x20) {
        f->rate_p = f->rate_h = f->rate_r = 0;
        if ((f->cls & 0xF0) != 0) {
            ac->pos[0] = before[0]; ac->pos[2] = before[2];
            if (!(f->w2 & W2_GROUND)) ac->pos[1] = before[1];
        }
    }
    update_cells(ac);
    if (r == 0x40 && !nibble) { player_damage(f); return; }   /* note 07 10.4 */
    if (r == 0x40) {   /* the player with an ejection nibble: 0x2A7F0 goes to 0x2A8F2 for any result, which for 0x40 reaches the writer 0x2A954 and the removal 0x2A9C0 with the player as the record (note 07 13.1, 10.4;
                        * V: weapon_check.py result40): B20 bit 1, V:54E72 = 0, W2 0x1000, state 1, the valid bit cleared, V:4A910 = 0x0C. The ejection goes on (the nibble cycle runs from the world update). */
        f->b20 |= 0x02;
        f->w2 |= 0x1000;
        f->state = 1;
        if (g_env && g_env->follow) g_env->follow(g_env->follow_user, f);
        remove_record(f);
        return;
    }
    if (nibble) {
        /* 2. an ejection nibble: go to the dying path; no hit bit, no crash bits, no V:4A87D / V:4A850, no notice, whatever W, V:4A952, V:4A844 and the view */
        dying = 1;
    } else {
        if (r == 0x10) {
            f->w2 |= W2_GROUND;   /* 3. the ground test: no V:4A844 test, no first-contact effect */
        } else {
            if (!f->v4a844) return;   /* 4. collisions off: the step ends (not even the no-result tail) */
            /* first contact at W <= 0x3C0: the bump effect 0x1A0DC; only without a sound system (the rig) does it leave the notice 0xFA / V:4A84F = 2 */
            if (r == 0x20 && !(f->w0 & W0_HIT) && f->speed <= 0x3C0) {
                if (env->no_sound_system) { f->v4a968 = 0xFA; f->v4a84f = 2; } else f->ev_bump = 1;   /* the bump 0x1A0DC(0x1C, 0x30) is the game's (note 10 8.2) */
            }
        }
        f->w0 |= W0_HIT;                       /* 5. every contact sets 0x8000 */
        if (f->speed <= 0x3C0) return;         /*    W at most 0x3C0 (signed): the step ends */
        if (f->v4a952) return;                 /*    immune: nothing more */
        f->w0 |= W0_CRASHED;                   /* 6. R = 0x20 or 0x10: the crash bits (the statistics counter is not a flight variable) */
        f->v4a87d = 1; f->v4a850 = 4; f->ev_crash = 1;   /* 0x2A8C6: the crash counter's event (note 08 10.2) */
        if (!ext) return;                      /*    cockpit view: the step ends here (no finalisation) */
        dying = 1;                             /*    external view: on to 7 */
    }
    /* The dying path of a non-player record (note 05 item 17, `0x2A2C4`-`0x2A2E2`; note 07 5): this block is the finalisation for every record. An aircraft (class
     * nibble 1) makes no explosion object and no back-up; the class-0 records' object, the every-32nd-frame routine and the wreck are entities.c (note 07 5). */
    if (dying) {   /* 7. the dying path: B20 bit 0x02, W0 bit 0x1000 cleared; the player (class nibble 1): 0x0400, +0x4C = 15, 0x0200 */
        f->b20 |= 0x02;
        f->w0 &= (uint16_t)~W0_ACTIVE;
        if ((f->cls & 0xF0) == 0x10) { f->w0 |= W0_DYING | W0_CRASHED; f->counter = 15; }
    }
}

/* The removal 0x2A9C0 (note 07 10.3, 13.1): the valid bit is cleared and the display request V:4A910 is 0x0C; the step returns without the trail writer. */
static void remove_record(flight_state_t *f) {
    f->w0 &= (uint16_t)~W0_VALID;
    f->v4a910 = 0x0C;
}

/* Note 07 5.1, the result handler of a non-player record (0x2A8E6): the hit bit, out of control, the active bit cleared and the dying start (15 steps, the crashed bit cleared again). An
 * aircraft gets no object and no back-up; a record of class nibble 0 makes an explosion object (0x20, model 0x11) or, for 0x10 over land, a ground object after the impact back-up. It does not set
 * W2 bit 0x0080 and makes no speed test (the player's branch does). */
static void handle_result_entity(aircraft_t *ac, int r, const int32_t before[3]) {
    flight_state_t *f = &ac->f;
    if (r == 0x40) {   /* the aircraft contact, by class (note 07 13.1, 10.4; V: weapon_check.py result40 and nonplayer) */
        if (f->w0 & W0_DYING) return;                                      /* a dying record ends the handler for ANY counter (0x2A8F2 to 0x2A912; a counter above 15 first stores the debug word 0x3A, which nothing reads and the port does not keep) */
        if ((f->cls & 0xF0) == 0x30 && !(f->w0 & W0_CRASHED)) return;     /* the seat and the parachute go on only with the crashed bit; otherwise the step ends without any change (0x2A916 to 0x2A92A) */
        f->b20 |= 0x02;                                                    /* out of control; the active bit 0x1000 is kept for the result 0x40 */
        if ((f->cls & 0xF0) == 0) { remove_record(f); return; }            /* class nibble 0 (a missile): removed at once, none of the marks (0x2A940 to 0x2A9C0) */
        if (f->cls == 0x15) {                                              /* the cruise missile: the dying start 0x2A99A (dying bit, counter 15), not removed, no marks */
            f->w0 |= W0_DYING;
            f->counter = 15;
            f->w0 &= (uint16_t)~W0_CRASHED;
            return;
        }
        /* every other class (the aircraft): the writer 0x2A954 - V:54E72 = the record's own offset, W2 0x1000, state 1 - and the removal 0x2A9C0, which returns without the trail writer */
        f->w2 |= 0x1000;
        f->state = 1;
        if (g_env && g_env->follow) g_env->follow(g_env->follow_user, f);
        remove_record(f);
        return;
    }
    if (r == 0x20) {
        f->rate_p = f->rate_h = f->rate_r = 0;
        if ((f->cls & 0xF0) != 0) {
            ac->pos[0] = before[0]; ac->pos[2] = before[2];
            if (!(f->w2 & W2_GROUND)) ac->pos[1] = before[1];
        }
    }
    update_cells(ac);
    if (!f->v4a844) return;
    f->w0 |= W0_HIT;
    if (f->w0 & W0_DYING) return;                                          /* 0x2A8F2: a dying record ends the handler, whatever its counter (above 15 the debug word 0x3A is stored first: not kept) */
    if ((f->cls & 0xF0) == 0x30 && !(f->w0 & W0_CRASHED)) return;         /* the seat classes go on only with the crashed bit (0x2A916 to 0x2A92A) */
    f->b20 |= 0x02;
    f->w0 &= (uint16_t)~W0_ACTIVE;
    if ((f->cls & 0xF0) == 0) {   /* only a missile passes the object code (0x2A970); an aircraft jumps straight to the dying start (0x2A94E to 0x2A99A) */
        int make = 1;
        if (r == 0x10) {
            /* the impact back-up (0x2AA5E): the vertical displacement shifted right by 8 is the divisor (zero: the back-up is skipped, the object is still made); the record's LONG position
             * words are moved back along the displacement to y = 0 (the cell and fine words keep the position update's values) */
            const int16_t dy16 = (int16_t)(f->vel[1] >> 8);
            if (dy16 != 0) {
                const int16_t h = (int16_t)(-(ac->pos[1] >> 8));
                for (int k = 0; k < 3; k += 2) {
                    const int32_t num = fl_shl(f->vel[k], 4);   /* RIG (test_weapons_diff.c): the full long word, the divs.w overflow leaves the dividend's low word */
                    int32_t qq = num / dy16;
                    if (qq > 32767 || qq < -32768) qq = (int16_t)num;
                    ac->pos[k] = fl_add(ac->pos[k], ((int32_t)(int16_t)qq * h) >> 4);
                }
                ac->pos[1] = 0;
            }
            if (g_env && g_env->tab) fl_terrain_query(f, g_env->tab, ac->pos[0], ac->pos[2], &f->b4);   /* the over-land test 0x2F75A writes B4 bit 1 itself: water makes no object */
            make = !(f->b4 & 0x02);
        }
        if (make) {
            f->ev_object = r == 0x20 ? 0x11 : 0x12;
            for (int k = 0; k < 3; k++) f->ev_obj_pos[k] = ac->pos[k];
            f->ev_obj_cell[0] = f->cell_x; f->ev_obj_cell[1] = f->cell_z; f->ev_obj_fine[0] = f->fine_x; f->ev_obj_fine[1] = f->fine_z;
            f->ev_obj_sub = f->cell_a; f->ev_obj_nb = f->cell_b; f->ev_obj_alt8 = f->alt8;
        }
    }
    /* The ground (0x10) and hull or landmark (0x20) results write neither V:54E72 nor W2 0x1000 (note 07 13.1: the writer at 0x2A954 needs a result of 0x40 or more, above); the gun's third hit and the
     * contact scan write the followed record themselves. */
    f->w0 |= W0_DYING;
    f->counter = 15;
    f->w0 &= (uint16_t)~W0_CRASHED;
}

void flight_test_result_entity(aircraft_t *ac, int r, const int32_t before[3]) { handle_result_entity(ac, r, before); }
void flight_test_result(aircraft_t *ac, int r, const int32_t before[3]) { if (g_env) handle_result(ac, g_env, r, before); }

static void step_player(aircraft_t *ac, const flight_env_t *env, int no_motion) {
    flight_state_t *f = &ac->f;
    const flight_tables_t *t = env->tab;
    const int ext = f->v4a83d != 0;
    /* Note 05 §17.3, the dying expiry (the entry of the entity step; the external view only, the cockpit view goes straight on): with W0 bit 0x0400 set,
     * after the world update decreased +0x4C: while +0x4C > 0 W0 bit 0x2000 is cleared and the step goes on; at 0 or less W0 loses 0x0400, 0x0040 (valid),
     * 0x4000 (radar) and 0x2000, +0x4C = 20, V:4A963 counts when the class is an aircraft (high nibble 1) and W0 bit 0x0008 and W2 bit 0x0080 are clear,
     * and the rest of the step is skipped. */
    if (ext && (f->w0 & W0_DYING)) {
        if (f->counter > 0) {
            f->w0 &= (uint16_t)~0x2000;
        } else if (f->entity) {
            /* Note 07 section 5.3, the expiry of a non-player record, in this order: a cruise missile or a class nibble 0 record is freed (no count); an aircraft that is counted
             * (W0 bit 0x0008 clear) and not on the ground is counted in V:4A963; a record on the ground is freed; a record with the hit bit is freed; any other (an airborne
             * wreck) is kept, the 0x2000 bit cleared, and the step goes on with the wreck falling. Freeing: valid, radar and 0x2000 cleared, +0x4C = 20. */
            f->w0 &= (uint16_t)~W0_DYING;
            const int free_now = f->cls == 0x15 || (f->cls & 0xF0) == 0;
            if (!free_now && (f->cls & 0xF0) == 0x10 && !(f->w0 & 0x0008) && !(f->w2 & W2_GROUND)) f->v4a963++;
            if (free_now || (f->w2 & W2_GROUND) || (f->w0 & W0_HIT)) {
                f->w0 &= (uint16_t)~(0x0040 | 0x4000 | 0x2000);
                f->counter = 20;
                return;
            }
            f->w0 &= (uint16_t)~0x2000;
        } else {
            if ((f->cls & 0xF0) == 0x10 && !(f->w0 & 0x0008) && !(f->w2 & W2_GROUND)) f->v4a963++;
            f->w0 &= (uint16_t)~(W0_DYING | 0x0040 | 0x4000 | 0x2000);
            f->counter = 20;
            return;
        }
    }
    /* Note 05 §17.2 (which parts of the step run): in the external view without W0 bits 0x1000 and 0x0100 the input conversion, the warnings, the speed, attitude and
     * motion steps are all skipped (D = 0); only the cell fields are refreshed from the unchanged position. In every other combination the input conversion
     * and the warnings run, the speed step needs 0x1000, the attitude and motion steps run, and the position update (with the cell fields) needs 0x1000. */
    const int idle = ext && !(f->w0 & 0x1100);
    if (idle) {
        /* 17.18 b: the stored displacement words (+0x3E, +0x42, +0x46) are left as they were, also for a dying record (+0x4C > 0); D = 0 is only the value of the
         * position update and of the collision's previous position (the record is not written) */
        update_cells(ac);
    } else {
        int skip_speed = 0;
        if (f->v4a984 & 0x40) {
            input_convert(f);
            skip_speed = f->entity ? 0 : state_warnings(f, ac->pos[1]);   /* the direct posts are the player's only (note 05 17.13): a record's speed step is never skipped by one */
            if (!skip_speed && !(f->b20 & 0x02) && (f->cls & 0xF0) == 0x10 && (f->w0 & W0_ACTIVE)) speed_step(f, t, ac->pos[1]);
        }
        if (!(f->w0 & W0_HIT)) attitude_step(ac, t);
        if (!(f->v4a984 & 0x40)) {
            f->vel[0] = f->vel[1] = f->vel[2] = 0;
        } else if (!no_motion) {
            motion_step(ac, env);   /* updates y (and the velocity); x and z below */
        }
        /* h. position and cell bookkeeping */
        if ((f->w0 & W0_ACTIVE) && !no_motion) {
            int32_t x = (int32_t)((uint32_t)ac->pos[0] + (uint32_t)f->vel[0]);
            if (x < 0) { ac->pos[2] = 0; }
            else if (x > 0x1FFFFFFF) x = 0x1FFFFFFF;
            ac->pos[0] = x;
            int32_t z = (int32_t)((uint32_t)ac->pos[2] + (uint32_t)f->vel[2]);
            if (z < 0) z = 0; else if (z > 0x1FFFFFFF) z = 0x1FFFFFFF;
            ac->pos[2] = z;
            if (f->cls == 0x15) ac->pos[1] = 0xB000;   /* the cruise missile's height is forced after every position update (0x2A5B2, note 07 4.12): 176 units above the sea from its second step */
            update_cells(ac);
        } else if ((f->w0 & W0_ACTIVE) && no_motion) {
            update_cells(ac);   /* rig (tests/rig/test_training_traj.c): a placed record's first pass moves nothing but refreshes the cell bookkeeping (+0x0A, +0x0B) */
        }
    }
    const int count3 = gear_counters(f);
    /* the displacement the collision routine undoes: position minus D, whatever the position update did (note 05 §17.2) */
    int32_t before[3];
    for (int k = 0; k < 3; k++) before[k] = idle ? ac->pos[k] : (int32_t)((uint32_t)ac->pos[k] - (uint32_t)f->vel[k]);
    if (!(f->w0 & W0_DYING)) {
        int r = fl_collide(f, env, before);
        if (r) {
            handle_result(ac, env, r, before);
        } else if ((f->b20 & 0x02) && (f->w2 & W2_GROUND)) {
            f->w0 &= (uint16_t)~W0_ACTIVE;
            f->w0 |= W0_DYING | W0_CRASHED;
            f->counter = 15;
            if (f->entity) f->w0 &= (uint16_t)~W0_CRASHED;   /* 0x2A99A: the crashed bit is cleared again for every record but the player (note 07 5.1) */
        } else {
            f->w0 &= (uint16_t)~W0_HIT;
            if (f->w0 & W0_CRASHED) { f->w0 |= W0_DYING; f->counter = 15; if (f->entity) f->w0 &= (uint16_t)~W0_CRASHED; }
            else {
                /* note 07 8.4, 0x2AA28: a missile (class nibble 0) that was inside another record's outer box in the last step (W2 bit 1) and is not now takes the lost mark (B20 bit 5,
                 * never cleared) and clears the fire-prompt state V:4A96C (ev_lost: the game); the proximity flag of the scan is then copied into W2 bit 1 */
                if (!f->v4a957 && (f->w2 & W2_CONTACT) && (f->cls & 0xF0) == 0) { f->b20 |= 0x20; f->ev_lost = 1; }
                f->w2 = (uint16_t)((f->w2 & ~W2_CONTACT) | (f->v4a957 ? W2_CONTACT : 0));
            }
        }
        f->v4a957 = 0;   /* note 05 §17.11: V:4A957 is a transient within the step; the later entity steps clear it */
    }
    if (count3) f->ev_seat = 1;   /* the count reached 3: the world update's seat routine (0x26F60, entities.c) spawns the seat if the latch V:4AA7A is clear */
}

void flight_sync(aircraft_t *ac) {
    flight_state_t *f = &ac->f;
    ac->pitch = -(pitch_w(f) >= 14400 ? pitch_w(f) - 28800 : pitch_w(f));
    ac->yaw = f->heading;
    ac->roll = roll_w(f) >= 14400 ? roll_w(f) - 28800 : roll_w(f);
    ac->speed = f->speed;
    ac->throttle = f->throttle;
    ac->gear = (f->gear & 0x70) == 0;
    ac->hook = (f->w2 & W2_HOOK) != 0;
    ac->on_ground = (f->w2 & W2_GROUND) != 0;
    ac->on_carrier = (f->b4 & 0xC0) != 0;
}

/* 0x2835A: the gear-down warning lamp (note 06, GEAR lamp) with the HUD beep 0x2837E (n = 4, note 10 7.4) on its rising edge */
static void gear_cue(flight_state_t *f) {
    if (f->speed_target >= 0x1200 && (f->gear & 0x70) == 0) { if (!f->v4a8fe) f->ev_beep = 4; f->v4a8fe = 1; }
    else f->v4a8fe = 0;
}

void flight_step(aircraft_t *ac, const flight_inputs_t *in) {
    const flight_env_t *env = g_env;
    if (!ac || !in || !env || !env->tab) return;
    flight_state_t *f = &ac->f;
    /* events (section 2): the key handlers run before the frame's step, paused or not */
    if (in->gear_toggle && !(f->w2 & W2_GROUND)) f->gear ^= 0x80;
    if (in->hook_toggle && f->cls == 0x11) { f->w2 ^= W2_HOOK; f->v4a8ff ^= 0x80; }
    if (in->brake_toggle) { f->w0 ^= W0_BRAKE; f->v4a830 = f->v4a834 = 0; f->v4a928 = 0; }   /* the brake key also clears the pointer throttle (0x1E32E, §17.4) */
    if (in->eject) f->gear = (uint8_t)(f->gear | 10);   /* note 07 6.4: the key ORs 0x0A into the low nibble (the key handler runs only at the nibble 0: game.c chain_open, note 07 6.4a) */
    f->keys = in->keys;
    if (in->preset) {   /* the F-key handler 0x1EA64 (§17.4): the preset and P = Q = min(8 * preset, 960) */
        f->v4a928 = in->preset;
        int p = in->preset < 0 ? 0 : 8 * in->preset;
        f->v4a830 = f->v4a834 = (int16_t)(p > 960 ? 960 : p);
    }
    /* The pointer throttle's rule 0, the mouse (note 05 §17.4): P = clamp(P - dy, -960, 960) once per vertical blank, an interrupt that runs between steps. The
     * port applies the absolute axis (PORT CHOICE, flight.h) and the summed counter change of the step's blanks (CADENCE, flight.h) here, after the key handlers (a brake or F-key event of the same step acts first) and before the rules. */
    {
        int P = f->v4a830;
        if (f->v4a890 && in->pointer_set) P = in->pointer_value;
        /* note 05 §17.4 rule 0: the handler 0x192DE runs whatever V:4A890 is; with V:4A890 = 0 a negative dy is halved first (arithmetic shift; never in play).
         * The original halves PER BLANK (each negative dy of a blank); the port halves the step's sum: a CADENCE PORT CHOICE (flight.h), like the summed clamp. The
         * follow and fire rules need V:4A890 = 1. */
        int dy = in->pointer_dy;
        if (!f->v4a890 && dy < 0) dy >>= 1;
        P -= dy;
        f->v4a830 = (int16_t)(P > 960 ? 960 : P < -960 ? -960 : P);
    }
    /* section 1 / note 05 §17.2: with V:4A866 set the player step returns at once; the world update still clears W2 bit 0x0001 (proximity) and the player's W0 bit
     * 0x0002 and runs the gear-down cue 0x2835A (and the turn-rate cue, a sound only); +0x4C, the timers and the cool-downs are not decremented. */
    if (f->v4a866) {
        f->w2 &= (uint16_t)~W2_PROX;
        f->w0 &= (uint16_t)~0x0002;
        gear_cue(f);
        return;
    }
    /* section 1, item 3: the pre-steps of the world update */
    f->w2 &= (uint16_t)~W2_PROX;
    f->counter = w16(f->counter - 1);
    f->w0 &= (uint16_t)~0x0002;
    /* 0x2835A: the gear-down warning lamp (note 06, GEAR lamp) */
    gear_cue(f);
    step_player(ac, env, 0);
    f->steps++;
    /* The viewed cell V:4AA04 / V:4AA06 is written by the camera set-up of the frame, AFTER the update (note 04a 8a, "The viewed cell in an external view is the camera's": the record's cell in
     * the cockpit view, bits 22 to 28 of the masked camera longs in an external view, with the sub-cell and quadrant words from bits 20 to 23), so every record of the update, the player
     * included, reads the value of the PREVIOUS pass. The game (env->view_cell_by_camera) writes it from entities_update_view_cell after the pass; this line is the cockpit view's value for a
     * caller without a frame (the rig harnesses). */
    if (!env->view_cell_by_camera) { f->v4aa04 = f->cell_x; f->v4aa06 = f->cell_z; }
    /* panel caches (note 06 "Draw schedule"): digits refresh when (n & 3) = 0 (altitude) and 2 (speed) */
    if ((f->steps & 3) == 0) f->panel_alt = 5 * (ac->pos[1] >> 10);
    if ((f->steps & 3) == 2) f->panel_speed = (int16_t)(iabs32(f->speed) / SPEED_KT_DIV);
    flight_sync(ac);
}

/* A second aircraft of class 0x10 or 0x11 (the training instructor, record 4: note 08 8.3). It is stepped by the player's rules, as the original does (the stick
 * response table of the player for a record whose W2 bit 0x0100 is set, the coupling always on for a non-player aircraft, note 08 8.3), with the key byte its state
 * routine wrote; the player-only parts of flight_step are not run: the key-handler events, the pointer throttle (the instance's V:4A890 must be 0, its P and Q 0),
 * the pause early return, the decrement of +0x4C and the clearing of W0 bit 0x0002 and W2 bit 0x0001 (the caller's: the original decrements +0x4C in 0x278F6 BEFORE
 * the state routine runs, so the caller does it and then calls this), the gear-down lamp and the viewed-cell word. The global switches the step reads (V:4A83D, V:4A984,
 * V:4A844, V:4A847, V:4A866, V:4A867, V:4A7B2, V:4A95E, V:4A903, V:4A952) are fields of the instance's flight state: the caller copies them from the player's. */
void flight_step_entity(aircraft_t *ac, uint8_t keys, int no_motion) {
    const flight_env_t *env = g_env;
    if (!ac || !env || !env->tab) return;
    flight_state_t *f = &ac->f;
    f->keys = keys;
    f->entity = 1;
    if ((f->cls & 0xF0) == 0x30) {
        /* the ejection seat, the parachute and the pod (note 07 6.3a): the seat routine of the world update wrote the velocity and the entity module ran the attitude step; the flight step
         * moves the record. Height first: when height + vy is not above 0 the hit bit is set, the height becomes 0 and the rest of the position update and the cell bookkeeping are
         * skipped, as they are for every later step of a landed record. Otherwise x and z get vx and vz with the common clamp (a negative x is not clamped and clears z first), and
         * the common cell bookkeeping runs. There is no collision test against the ground. */
        if (!no_motion && !(f->w0 & W0_HIT)) {
            const int32_t y = fl_add(ac->pos[1], f->vel[1]);
            if (y <= 0) { ac->pos[1] = 0; f->w0 |= W0_HIT; }
            else {
                ac->pos[1] = y;
                int32_t x = fl_add(ac->pos[0], f->vel[0]);
                if (x < 0) ac->pos[2] = 0;
                else if (x > 0x1FFFFFFF) x = 0x1FFFFFFF;
                ac->pos[0] = x;
                int32_t z = fl_add(ac->pos[2], f->vel[2]);
                if (z < 0) z = 0; else if (z > 0x1FFFFFFF) z = 0x1FFFFFFF;
                ac->pos[2] = z;
                update_cells(ac);
            }
        } else if (no_motion) update_cells(ac);   /* the fresh pass of a spawned record moves nothing but refreshes the cell bookkeeping */
        f->entity = 0;
        f->steps++;
        flight_sync(ac);
        return;
    }
    step_player(ac, env, no_motion);
    f->entity = 0;   /* the record's slots are the draw's (game.c copies them into f->slot): no rotation here */
    f->steps++;
    flight_sync(ac);
}

/* The sine and cosine (Q14) of an angle word as the matrix builders read the table (28,800 units per circle). */
void flight_sincos_word(aircraft_t *ac, uint16_t w, int32_t *s, int32_t *c) {
    const flight_env_t *env = g_env;
    if (!ac || !env || !env->tab) { if (s) *s = 0; if (c) *c = 0; return; }
    int32_t ss, cc;
    sincos_word(&ac->f, env->tab, w, &ss, &cc);
    if (s) *s = ss;
    if (c) *c = cc;
}

/* ---- sections 9, 15 and 17.14: start of a flight ------------------------------------------------------ */

/* The start blocks (note 05 §17.14, table at 0x47ADE, entries of eight words: cell x, cell z, n, ox, oz, fx, fz, degrees). The runway start of block 0 is
 * x = cell_x * 2^24 + dirx(n) * 1024 + ox * 1024 + fx * 16 (z likewise), y = 0x708, heading = degrees * 80, where (dirx, dirz) is the word pair at 0x206F2 + 4 n
 * (flight_tables_t.dir). Without the tables the constants below are the same sum for block 0 (the position of the worked example E1). */
#define START_RUNWAY_CELL 16
#define START_RUNWAY_N 6
#define START_RUNWAY_OX 1184
#define START_RUNWAY_OZ 2528
#define START_RUNWAY_FX 7570
#define START_RUNWAY_FZ (-14257)
#define START_RUNWAY_DEGREES 28
#define START_RUNWAY_X0 273963296   /* the block-0 sum with dir(6) = (4096, 8192), checked by tests/test_flight.c against the table */
#define START_RUNWAY_Z0 279184624
#define DECK_HEIGHT 112   /* D: the long at +2 of the model header named by the pointer at +0x10 of the carrier's descriptor (header 0x3E1A0), §17.14 */

/* The part of the start that both the first start and the restart make (§15, §17.14): the position, attitude and the fields that go with them. */
static void start_placement(aircraft_t *ac, flight_start start) {
    flight_state_t *f = &ac->f;
    const flight_env_t *env = g_env;
    /* §17.14: a carrier start needs an active ship record; otherwise the start is made as block 0 */
    const flight_ship_t *ship = (env && env->ship.present && (env->ship.w0 & 0x40)) ? &env->ship : NULL;
    int carrier = 0, start_n = START_RUNWAY_N;
    if (start == FLIGHT_START_CARRIER && ship) {
        /* the record's position plus ((11 * M0 + 104 * M2) >> 4, (11 * M6 + 104 * M8) >> 4), M the nine words of its matrix B (+0x92) */
        ac->pos[0] = ship->pos[0] + ((11 * ship->mat_b[0] + 104 * ship->mat_b[2]) >> 4);
        ac->pos[2] = ship->pos[2] + ((11 * ship->mat_b[6] + 104 * ship->mat_b[8]) >> 4);
        ac->pos[1] = ((int32_t)DECK_HEIGHT << 8) + 0x708;
        f->b4 = (uint8_t)((f->b4 & 0x35) | 0xC8);   /* W2 is not touched here: the common lines of the start write it (17.17 rule 4, C43) */
        f->deck = DECK_HEIGHT;
        f->pitch = ship->pitch; f->heading = ship->heading; f->roll = ship->roll;
        carrier = 1;
    } else {
        int32_t x = START_RUNWAY_X0, z = START_RUNWAY_Z0;
        int cell = START_RUNWAY_CELL, cell_z = START_RUNWAY_CELL, n = START_RUNWAY_N, ox = START_RUNWAY_OX, oz = START_RUNWAY_OZ, fx = START_RUNWAY_FX, fz = START_RUNWAY_FZ, deg = START_RUNWAY_DEGREES;
        if (g_env && g_env->block.present) {   /* the block the location key chose: the same sum over its own words */
            const flight_block_t *b = &g_env->block;
            cell = b->cell_x; cell_z = b->cell_z; n = b->n; ox = b->ox; oz = b->oz; fx = b->fx; fz = b->fz; deg = b->degrees;
        }
        if (g_env && g_env->block.present && g_env->block.direct) { x = g_env->block.x; z = g_env->block.z; }
        else if (env && env->tab && n >= 0 && 2 * n + 1 < 32) {
            x = cell * 16777216 + env->tab->dir[2 * n] * 1024 + ox * 1024 + fx * 16;
            z = cell_z * 16777216 + env->tab->dir[2 * n + 1] * 1024 + oz * 1024 + fz * 16;
        }
        ac->pos[0] = x; ac->pos[1] = 0x708; ac->pos[2] = z;
        f->heading = (uint16_t)(deg * 80);
        f->pitch = f->roll = 0;
        f->b4 |= 0x04;
        start_n = n;
    }
    if (g_env && g_env->tab) build_matrices(f, g_env->tab);
    const uint8_t keep_a = f->cell_a;
    update_cells(ac);
    if (carrier) {
        /* 17.18 f: the carrier start writes +0x0A and +0x0B from the new position: +0x0A the step's sub-cell formula (update_cells has just set it), +0x0B the same
         * formula over the low two bits of the cell words (demo carrier at cell (68, 68): 10 and 15; the free-flight carrier's cell (70, 65): 9) */
        f->cell_b = (uint8_t)(4 * (3 - (f->cell_z & 3)) + (3 - (f->cell_x & 3)));
    } else {
        f->cell_a = keep_a;           /* the runway start writes +0x0B = n (the direction index) and leaves +0x0A as it was */
        f->cell_b = (uint8_t)start_n;
    }
    f->v4aa04 = f->cell_x; f->v4aa06 = f->cell_z;   /* §17.14: the start also sets the viewed cell */
    f->v4a910 = 0;
    flight_sync(ac);
}

void flight_init(aircraft_t *ac, flight_type type, flight_start start) {
    memset(ac, 0, sizeof *ac);
    flight_state_t *f = &ac->f;
    f->w0 = 0x11C8;
    f->cls = type == FLIGHT_FA18 ? 0x11 : 0x10;
    f->id = 0x0D;
    f->b7b = 0xFF;
    f->l7d = 0x53;
    f->turn_limit = 0x1400;
    f->fuel = 0x61A800;
    f->throttle = 9;
    f->repeat = 1;
    f->v4a7b2 = 0x800000;
    f->v4a984 = 0x0840;
    f->v4a890 = 1;   /* section 15: the pointer throttle flag is set at every start */
    f->v4a830 = f->v4a834 = 72;   /* section 15: P = Q = 8 * T with T = 9 */
    f->v4a83c = f->v4a847 = 1;
    f->v4a844 = 1;
    f->v4a95e = 1;
    f->w2 = W2_GROUND;
    f->v4aa7a = 0;   /* only the start clears the ejection latch (§17.10) */
    start_placement(ac, start);
}

/* The restart of the crash flow: the write list of 0x1D538 (note 05 17.17 rule 4; C32, C43), the common lines of every start; everything else is kept.
 * Written: W0 = 0x11C8; W2 bits 0xB108 cleared and 0x0080 set (the others survive: the hook engaged, the idle detent, ...); B4 & 0x35 (the carrier start then
 * ORs 0xC8); B20 bits 0 to 2 and +0x21 bit 0 cleared; the position and cell fields; the attitude (pitch 0 and roll 0 with the block's heading for a runway start,
 * the carrier's three words for a carrier start) and both matrices; the motion words (+0x3E, +0x42, +0x46, +0x50, +0x54, +0x56, +0x5A, +0x6C, +0x6E, +0x78, +0x65
 * cleared; T = 9; +0x7C = 0; +0x7E = 0x1400; +0x63 = 0x0D; the high nibble of +0x7D cleared; V:4AC08 and V:4AC0C cleared); THE FUEL IS REFILLED (+0x72);
 * V:4A928 (the throttle preset), V:4A858, V:4A8FF = 0; the pointer throttle P = Q = 72 with V:4A890 = 1; V:4A910 is the caller's request (game.c); V:4A952 = 0;
 * V:4A850 = 4 when it was non-zero; V:4AB98 (the message line) and V:4AA7A (the ejection latch) = 0; V:4A968 = 0xFE. The records 1 to 3 (the seat and the shot
 * records) are the world's and are cleared by the caller. NOT written (they survive, rule 4): +0x7B, V:4A963, V:4A982, V:4A98A, V:4A903, V:4A957, V:4A9FE, V:4A84F,
 * V:4A8FE, V:4A994/996, the mission switches, the commands +0x28..+0x2A, +0x39, +0x4C, +0x4E, +0x22..+0x26, E,Y,Q's middle word +0x58, and the VIEW SELECTOR V:4A83D
 * (the restart is 0x1D538, not 0x1D4E2: rule 11). V:4A984 bit 6 is set again only by the resume state 0x139C2: this function does not set it (game.c does);
 * V:4A951 (the hold counter) is the caller's too. W2 bit 0x0002 is the resume state's write (0x13980):
 * game.c pass B sets it, not this function. */
void flight_restart(aircraft_t *ac, flight_start start) {
    flight_state_t *f = &ac->f;
    f->w0 = 0x11C8;
    f->w2 = (uint16_t)((f->w2 & ~0xB108u) | W2_GROUND);
    f->b4 &= 0x35;
    f->b20 &= (uint8_t)~0x07;
    f->b21 &= (uint8_t)~0x01;
    f->gear = 0;
    f->v4a8ff = 0; f->v4a928 = 0; f->v4a858 = 0;
    f->v4a890 = 1;
    f->v4a830 = f->v4a834 = 72;
    f->throttle = 9;
    f->turn_limit = 0x1400;
    f->id = 0x0D;
    f->l7d &= 0x0F;
    f->speed_target = 0; f->speed = 0; f->gust = 0;
    f->vel[0] = f->vel[1] = f->vel[2] = 0;
    f->rate_p = f->rate_r = 0;
    f->turn50 = f->turn54 = 0;
    f->keys = 0;
    f->v4ac08 = 0; f->v4ac0c = 0;
    f->v4a952 = 0;
    if (f->v4a850) f->v4a850 = 4;
    f->fuel = 0x61A800;
    f->v4ab98 = 0;
    f->v4aa7a = 0;
    f->v4a968 = 0xFE;
    start_placement(ac, start);   /* the vertex slots are the draw's (game.c copies the draw store into them): the start does not touch them (17.17 (xi)) */
}

void flight_set_position(aircraft_t *ac, int32_t x, int32_t y, int32_t z) {
    ac->pos[0] = x; ac->pos[1] = y; ac->pos[2] = z;
    update_cells(ac);
    ac->f.v4aa04 = ac->f.cell_x; ac->f.v4aa06 = ac->f.cell_z;   /* the camera follows */
    flight_sync(ac);
}

void flight_set_attitude(aircraft_t *ac, uint16_t pitch, uint16_t heading, uint16_t roll) {
    ac->f.pitch = pitch; ac->f.heading = heading; ac->f.roll = roll;
    if (g_env && g_env->tab) build_matrices(&ac->f, g_env->tab);
    flight_sync(ac);
}

/* The camera attitude of a head view (note 03 "Camera attitude in the head and external views", note 06 "Head views"): the composition 0x325B8 with the record matrix
 * at +0x80 (mat_a) as input A and the head triple (pitch, yaw, roll) as the three angles; out = the camera pitch, heading and roll words (28800 units). The record's
 * state is only read (the lookup counters aside). */
void flight_head_camera(aircraft_t *ac, const flight_tables_t *t, int pitch, int yaw, int roll, int32_t out[3]) {
    compose(&ac->f, t, pitch, yaw, roll, &out[0], &out[1], &out[2]);
}

/* ---- HUD ---------------------------------------------------------------------------------------------- */

int32_t flight_panel_altitude(const aircraft_t *ac) { return ac->f.panel_alt; }
int32_t flight_panel_speed(const aircraft_t *ac) { return ac->f.panel_speed; }

void flight_hud(const aircraft_t *ac, game_hud_t *hud) {
    const flight_state_t *f = &ac->f;
    hud->airspeed_kt = (f->w0 & W0_HIT) ? 0 : iabs32(f->speed) / SPEED_KT_DIV;
    hud->altitude_ft = 5 * (ac->pos[1] >> 10);
    hud->heading_deg = (int32_t)((f->heading >> 3) / 10);
    hud->throttle_pct = iabs32(f->throttle) * 256 / 307;
    hud->fuel_pct = (int32_t)(((int64_t)(f->fuel < 0 ? 0 : f->fuel >> 8) * 100) / 25000);
    hud->gear_down = ac->gear;
    hud->hook_down = ac->hook;
    hud->pitch_deg = ac->pitch / 80;
    hud->roll_deg = ac->roll / 80;
    hud->warning[0] = '\0';   /* the warning line belongs to the core cockpit module's arbiter (cockpit.h), fed by V:4AB98 and V:4AC0C */
}
