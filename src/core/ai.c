/* src/core/ai.c - the generic AI (docs/re/07-ai-and-weapons.md sections 3 and 4). See ai.h. */
#include <stddef.h>
#include <string.h>
#include "ai.h"
#include "fx.h"
#include "flight.h"
#ifdef AI_TRACE
void ai_trace(const char *fmt, ...);   /* defined by the rig harness that builds this file with -DAI_TRACE (debugging a difference) */
#define TR(...) ai_trace(__VA_ARGS__)
#else
#define TR(...) ((void)0)
#endif

/* ---- helpers --------------------------------------------------------------------------------------------------------------------------------- */

static int16_t w16(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v & 0xFFFFu); }
static int32_t add32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static int32_t sub32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
static int32_t abs32(int32_t v) { return v < 0 ? (int32_t)(0u - (uint32_t)v) : v; }

/* The rate tables of 3.6 (words at 0x31316, 0x3132C, 0x31342, 0x31358, 0x3136E: five tables of 11 words, 22 bytes apart, so a negative index of one table reads the tail of the
 * previous one) come from tables/ai.bin, the one source; without the table (synthetic assets) the AI commands nothing that depends on them. */
enum { T_NONAIR = 0, T_PITCH = 1, T_ROLL = 2, T_C14_PITCH = 3, T_C14_ROLL = 4 };
static int rate_at(const entities_t *e, int table, int idx) {
    const int flat = table * 11 + idx;
    if (!e->tab) return 0;
    if (flat == -1) return e->tab->pre_word;   /* the one non-aircraft case of the aligned pitch path: the word before the first table (note 07 3.6; ai.bin pre_word) */
    if (flat < 0 || flat >= 55) return 0;
    return e->tab->rate[flat / 11][flat % 11];
}

static entity_t *rec(entities_t *e, int i) { return &e->rec[i & 15]; }
static flight_state_t *fs(entities_t *e, int i) { return &entities_ac(e, i & 15)->f; }

/* The notices 0x1E, 0x34, 0x35, 0x3C and 0x3E store a value in V:4AA56 and call a bare return (note 07 4.8): no visible effect. */
static void post(entities_t *e, uint8_t code) { e->debug_word = code; }

/* ---- the normaliser (note 04a P4) ------------------------------------------------------------------------------------------------------------- */

void ai_normalise(int32_t x, int32_t y, int32_t z, int len, int32_t out[3]) {
    int32_t cx = w16(x), cy = w16(y), cz = w16(z);
    int32_t ax = cx < 0 ? w16(-cx) : cx, ay = cy < 0 ? w16(-cy) : cy, az = cz < 0 ? w16(-cz) : cz;
    fx_vec_t v = { ax, ay, az };
    const int32_t a = fx_dist(&v);
    if (len == 0 || (a & 0xFFFF) == 0) { out[0] = out[1] = out[2] = 0; return; }
    /* The scale: the largest power of four n whose quotient q = lq * n / A stays within 1024, where lq is the length with its low (10 - s) bits cleared for a scale 2^s below 1024
     * (for the length 256 that is the smallest power of four above A, note 04a P4). Rig: 60,000 random lengths and vectors, bit for bit. */
    const uint32_t A = (uint32_t)(a & 0xFFFF);
    uint32_t n = 4, lq;
    int s = 2;
    for (;;) {
        const int s2 = s + 2;
        const uint32_t l2 = s2 < 10 ? (uint32_t)len >> (10 - s2) << (10 - s2) : (uint32_t)len;
        if ((uint64_t)l2 * ((uint64_t)n << 2) / A > 1024) break;
        n <<= 2; s = s2;
    }
    lq = s < 10 ? (uint32_t)len >> (10 - s) << (10 - s) : (uint32_t)len;
    uint32_t q = (lq * n) / A;
    if (q > 0xFFFFu) q &= 0xFFFFu;   /* the hardware's overflow leaves the register: not reached for the lengths used here */
    const int32_t qq = (int32_t)q;
    out[0] = (int32_t)(((int64_t)cx * qq) >> s);
    out[1] = (int32_t)(((int64_t)cy * qq) >> s);
    out[2] = (int32_t)(((int64_t)cz * qq) >> s);
}

/* ---- waypoints (4.3) ------------------------------------------------------------------------------------------------------------------------- */

static int route_get(entities_t *e, unsigned r, int16_t blocks[][5], int max) {
    if (!e->hook.route) return 0;
    return e->hook.route(e->hook.user, r, blocks, max);
}

static void set_dest(entity_t *r, const int16_t w[5]) {
    r->dest_cx = w[0]; r->dest_cz = w[1]; r->dest_fx = w[2]; r->dest_fz = w[3]; r->dest_h = w[4];
}

/* ---- the steering (3.5, 3.6) ----------------------------------------------------------------------------------------------------------------- */

/* flag bits (3.7) */
#define FL_SIGN 0x01
#define FL_ROLL_HYST 0x02
#define FL_PITCH_HYST 0x04
#define FL_VERT 0x08
#define FL_EPILOGUE 0x10
#define FL_ROLL_AHEAD 0x20
#define FL_PITCH_AHEAD 0x40
#define FL_CLOSE 0x80

static int32_t dot3(const int32_t v[3], int16_t a, int16_t b, int16_t c) {
    return add32(add32((int32_t)((int64_t)v[0] * a), (int32_t)((int64_t)v[1] * b)), (int32_t)((int64_t)v[2] * c));
}

typedef struct { entities_t *e; int i; entity_t *r; flight_state_t *f; aircraft_t *a; int aircraft; int epilogue; } sctx_t;

static void keys_set(sctx_t *c, int bits) { c->f->keys = (uint8_t)((c->f->keys & 0x03) | bits); }

/* the roll command (3.6) */
static void roll_command(sctx_t *c, int32_t t, int state) {
    flight_state_t *f = c->f;
    if (state != 0 && state != 1 && state != 8) return;
    if (state == 8 && (f->w2 & EW2_GROUND)) return;
    const int roll = (uint16_t)f->roll;
    int bits = 0;
    if ((c->r->ai_flags & (FL_ROLL_AHEAD | FL_PITCH_AHEAD)) == (FL_ROLL_AHEAD | FL_PITCH_AHEAD)) {
        int rud = 0;
        if (t > 0) { if (t > f->rate_h) rud = 0x80; }
        else if (t < 0) { if (t < f->rate_h) rud = 0x40; }
        if (rud) {
            bits = rud;
            if (roll >= 81 && roll <= 14400) bits |= 0x04;
            else if (roll >= 14401 && roll <= 28720) bits |= 0x08;
        }
    } else if (c->r->ai_flags & FL_CLOSE) {
        if (roll >= 400 && roll <= 14400) bits = 0x04;
        else if (roll >= 14401 && roll <= 28400) bits = 0x08;
        else bits = t > 0 ? 0x08 : t < 0 ? 0x04 : 0;
    } else {
        bits = t > 0 ? 0x08 : t < 0 ? 0x04 : 0;
    }
    keys_set(c, bits);
    if (!c->aircraft) f->turn54 = (int16_t)t;   /* the rate target of a non-aircraft class is also stored at +0x54 */
}

/* the bank command (3.6): returns 1 when it handled the axis, 0 when the rudder command is to be used */
static int bank_command(sctx_t *c, int32_t cc, int idx, int state, int forward_neg) {
    flight_state_t *f = c->f;
    const int roll = (uint16_t)f->roll, pitch = (uint16_t)f->pitch;
    const int bank_ok = (roll < 6000 || roll > 22800) && (pitch <= 1600 || pitch > 27200);
    if (!bank_ok) return 0;
    if ((state != 0 && state != 1 && state != 8) || (state == 8 && (f->w2 & EW2_GROUND))) return 1;   /* neither the bank limit nor the roll command is issued (rig) */
    const int tab = (state == 8 || f->cls == 0x14) ? T_C14_ROLL : T_ROLL;
    int32_t t = rate_at(c->e, tab, idx);
    if ((uint16_t)f->speed_target >= 0x1800 && f->speed_target >= 0x1800) t >>= 1;
    if (cc < 0) t = -t;
    /* the bank limit +0x7E */
    {
        int32_t m = abs32(cc);
        if (forward_neg) m = sub32(0x600000, m);
        m >>= 8;
        if (m > 0x3000) m = 0x3000;
        else if (m <= 32) m = 0x1000;
        else if (m < 0x1800) m = 0x1800;
        if (state == 8) m >>= 1;
        f->turn_limit = (int16_t)m;
    }
    if (c->r->engage == 3 || c->r->engage == 4) {   /* the level-off bands of the roll word */
        const int both = (c->r->ai_flags & (FL_ROLL_AHEAD | FL_PITCH_AHEAD)) == (FL_ROLL_AHEAD | FL_PITCH_AHEAD);
        const int r = roll;
        if (t < 0) {
            if (both && r >= 14400 && r < 28000) t = 240;
            else if (r <= 14400 || r > 26800) { /* unchanged */ }
            else if (r > 26400) t = 0;
            else t = 240;
        } else {
            if (both && r > 800 && r <= 14400) t = -240;
            else if (r >= 14400 || r < 2000) { /* unchanged */ }
            else if (r < 2400) t = 0;
            else t = -240;
        }
    }
    roll_command(c, t, state);
    return 1;
}

static void rudder_command(sctx_t *c, int32_t cc, int idx, int state) {
    flight_state_t *f = c->f;
    int32_t t = rate_at(c->e, c->aircraft ? ((state == 8 || f->cls == 0x14) ? T_C14_ROLL : T_ROLL) : T_NONAIR, idx);   /* the bank table (rig: the class 0x14 and state 8 table too) */
    if (c->aircraft && f->speed_target >= 0x1800) t >>= 1;   /* rig: the rudder target is halved at speed like the roll target */
    if (c->aircraft) { if (cc < 0) t = -t; }
    else { if (cc >= 0) t = -t; }
    int bits = 0;
    if (t > 0) { if (t > f->rate_h) bits = 0x80; }
    else if (t < 0) { if (t < f->rate_h) bits = 0x40; }
    keys_set(c, bits);
    if (!c->aircraft) { f->turn52 = (int16_t)t; f->turn50 = 0; c->epilogue = 1; }   /* RIG (tests/rig/test_weapons_diff.c): a missile's rudder command clears the pitch target */
    else c->epilogue = 0;
}

static void pitch_command(sctx_t *c, int32_t cc, int idx, int state) {
    flight_state_t *f = c->f;
    int tab = !c->aircraft ? T_NONAIR : (f->cls == 0x14 ? T_C14_PITCH : T_PITCH);
    int32_t t = rate_at(c->e, tab, idx);
    if (cc >= 0) t = -t;
    if (state == 0 || state == 1 || state == 8) {
        if (!(state == 8 && (f->w2 & EW2_GROUND))) {
            int bits = 0;
            if (t > 0) { if (t > f->rate_p) bits = 0x10; }
            else if (t < 0) { if (t < f->rate_p) bits = 0x20; }
            keys_set(c, bits);
        }
    }
    if (!c->aircraft) { f->turn50 = (int16_t)t; f->turn52 = 0; }   /* RIG: and a pitch command the rudder target */
    c->epilogue = 0;
}

/* The steering of 3.5. `state` is the state byte (0, 1, 3, 4 or 8). */
static void steer(entities_t *e, int i, int state) {
    sctx_t c;
    c.e = e; c.i = i; c.r = rec(e, i); c.a = entities_ac(e, i); c.f = &c.a->f;
    entity_t *r = c.r;
    flight_state_t *f = c.f;
    c.aircraft = (f->cls & 0xF0) == 0x10;
    c.epilogue = 0;
    if (r->dest_cx < 0) { r->ai_flags = 0; return; }   /* no destination */
    if ((f->cls & 0xF0) == 0) { f->ground = w16(f->ground - 1); if (f->ground >= 10) return; }   /* note 07 8.3 (0x30BFE): a missile's blind period, +0x26 counts down from 20 and the steering returns while it is 10 or more */
    r->ai_flags &= 0x79;
    int32_t dcx = r->dest_cx, dcz = r->dest_cz, dfx = r->dest_fx, dfz = r->dest_fz;
    if ((int8_t)r->target >= 0) {   /* a shift count (3.5 step 2; no shipped record has it) */
        const int n = r->target & 31;
        dcx = w16(dcx << n); dcz = w16(dcz << n); dfx = w16(dfx << n) & 0x3FFF; dfz = w16(dfz << n) & 0x3FFF;
    }
    int32_t v[3];
    v[0] = add32((int32_t)((uint32_t)(int32_t)w16(dcx - f->cell_x) << 14), dfx - f->fine_x);
    v[2] = add32((int32_t)((uint32_t)(int32_t)w16(dcz - f->cell_z) << 14), dfz - f->fine_z);
    /* the target height */
    int32_t th = r->dest_h;
    if ((f->cls & 0xF0) == 0x10 && f->cls != 0x14 && f->cls != 0x15 && !(f->b20 & 0x02)) {
        const int32_t h = (int32_t)f->speed_target * (state == 8 ? 64 : 128);
        if (f->vel[1] < 0) { if (h >= c.a->pos[1] && h >= r->dest_h) th = h; }
        else {
            const int32_t g = 0x800000 - h;
            if (g <= c.a->pos[1] && g < r->dest_h) th = g;
        }
    }
    v[1] = sub32(th, f->alt8);
    {   /* normalise: halve the components until the largest is under 0x4800 */
        int guard = 0;
        for (;;) {
            const int32_t m = abs32(v[0]) > abs32(v[1]) ? (abs32(v[0]) > abs32(v[2]) ? abs32(v[0]) : abs32(v[2])) : (abs32(v[1]) > abs32(v[2]) ? abs32(v[1]) : abs32(v[2]));
            if (m < 0x4800 || guard++ > 20) break;
            v[0] >>= 2; v[1] >>= 2; v[2] >>= 2;
        }
    }
    int32_t n3[3];
    ai_normalise(v[0], v[1], v[2], 0xC0, n3);
    /* the limits */
    int32_t A, B, F, K;
    if (state == 8) { A = 0xC000; B = 0x10000; F = 0x30000; K = 0x80000; }
    else if ((f->cls & 0xF0) != 0x10) { A = 0x10000; B = 0x10000; F = 0x20000; K = 0x20000; }
    else { A = 0x30000; B = 0x40000; F = 0x80000; K = 0x80000; }
    const int32_t lat = dot3(n3, f->mat_b[0], f->mat_b[3], f->mat_b[6]);
    const int32_t vert = dot3(n3, f->mat_b[1], f->mat_b[4], f->mat_b[7]);
    const int32_t fwd = dot3(n3, f->mat_b[2], f->mat_b[5], f->mat_b[8]);
    const int use_vert = abs32(lat) <= abs32(vert);
    const int32_t cc = use_vert ? vert : lat;
    if (use_vert) r->ai_flags |= FL_VERT; else r->ai_flags &= (uint8_t)~FL_VERT;
    if (cc >= 0) r->ai_flags |= FL_SIGN; else r->ai_flags &= (uint8_t)~FL_SIGN;
    const int32_t lim = use_vert ? A : B;
    int idx;
    TR("steer st=%d lat=%d vert=%d fwd=%d cc=%d lim=%d vertaxis=%d flags=%02X n=(%d,%d,%d) mat=%d,%d,%d,%d,%d,%d,%d,%d,%d\n", state, lat, vert, fwd, cc, lim, use_vert, r->ai_flags, n3[0], n3[1], n3[2], f->mat_b[0],f->mat_b[1],f->mat_b[2],f->mat_b[3],f->mat_b[4],f->mat_b[5],f->mat_b[6],f->mat_b[7],f->mat_b[8]);
    if (cc >= -lim && cc <= lim) {   /* aligned */
        if (!c.aircraft) f->turn50 = f->turn52 = f->turn54 = 0;
        if (!use_vert) {
            r->ai_flags |= FL_ROLL_HYST | FL_ROLL_AHEAD;
            r->ai_flags ^= FL_SIGN;
            r->ai_flags |= FL_EPILOGUE;
            return;
        }
        if (fwd >= 0) {
            r->ai_flags |= FL_PITCH_HYST | FL_PITCH_AHEAD;
            r->ai_flags ^= FL_SIGN;
            r->ai_flags |= FL_EPILOGUE;
            return;
        }
        idx = cc >> 16;   /* the signed value: a negative index reads the previous table's tail */
        pitch_command(&c, cc, idx, state);
        if (c.epilogue) r->ai_flags |= FL_EPILOGUE;
        return;
    }
    const int32_t mag = abs32(cc);
    idx = (int)(mag >> 16);
    if (idx > 10) idx = 10;
    if (!use_vert) {
        if (mag < K) r->ai_flags |= FL_CLOSE;
        if (mag >= F) r->ai_flags &= (uint8_t)~FL_ROLL_AHEAD;
        else if (fwd >= 0) r->ai_flags |= FL_ROLL_AHEAD;
        else r->ai_flags &= (uint8_t)~FL_ROLL_AHEAD;
        r->ai_flags &= (uint8_t)~FL_ROLL_HYST;
        if (c.aircraft) {
            if (!bank_command(&c, cc, idx, state, fwd < 0)) rudder_command(&c, cc, idx, state);
            return;
        }
        rudder_command(&c, cc, idx, state);
        return;
    }
    if (mag >= F) r->ai_flags &= (uint8_t)~FL_PITCH_AHEAD;
    else if (fwd >= 0) r->ai_flags |= FL_PITCH_AHEAD;
    else r->ai_flags &= (uint8_t)~FL_PITCH_AHEAD;
    r->ai_flags &= (uint8_t)~FL_PITCH_HYST;
    pitch_command(&c, cc, idx, state);
}

/* ---- the end routine (3.3) ------------------------------------------------------------------------------------------------------------------- */

static void end_routine(entities_t *e, int i) {
    entity_t *r = rec(e, i);
    flight_state_t *f = fs(e, i);
    if (f->w2 & EW2_SCRIPTED) {
        if (e->trig >= 0) { f->state = 0x0A; f->counter = e->training_part1 ? 36 : 60; }
        else { f->state = 6; f->counter = 10; }
        return;
    }
    const int has_target = (r->target & 0x80) && r->target != 0xFF;
    f->state = 6;
    if (has_target && (e->progress <= 2 || (r->ai_flags & 0x60) == 0x60)) {
        f->counter = e->progress <= 0 ? 0x4B : e->progress == 1 ? 0x25 : e->progress == 2 ? 0x12 : 4;
    } else {
        f->counter = 10;
    }
}

/* ---- the state routine (3.1, 3.2) ------------------------------------------------------------------------------------------------------------ */

void ai_state(entities_t *e, int i) {
    entity_t *r = rec(e, i);
    aircraft_t *a = entities_ac(e, i);
    flight_state_t *f = &a->f;
    if (e->paused || e->frozen) return;
    if ((f->cls & 0xF0) == 0x30) { r->ai_flags = 0; return; }
    f->turn_limit = 0x1400;
    if (!(f->w0 & EW0_AI)) { r->ai_flags = 0; return; }
    if (e->mode == 5 && (f->w0 & EW0_NOT_COUNTED) && f->counter <= 0 && f->state != 8) {
        int16_t run[ENT_ROUTE_BLOCKS][5];
        f->state = 8;   /* the state byte is written before the run is tested: an empty run leaves it at 8 (review of note 07 3.2) */
        if (route_get(e, r->route, run, ENT_ROUTE_BLOCKS) > 0) set_dest(r, run[0]);
    }
    const int scripted = (f->w2 & EW2_SCRIPTED) != 0, ooc = (f->b20 & 0x02) != 0;
    int epilogue = 1;
    for (int guard = 0; guard < 8; guard++) {
        const int st = f->state;
        TR("state loop st=%d counter=%d scripted=%d ooc=%d keys=%02X\n", st, f->counter, scripted, ooc, f->keys);
        if (st != 0 && st != 1 && st != 8 && !scripted && !ooc) {   /* the floor test */
            const int32_t floor = (int32_t)f->speed_target * 64;
            int fail;
            if (f->vel[1] < 0) fail = a->pos[1] <= floor;
            else fail = a->pos[1] > 0x800000 - floor;   /* strictly above */
            if (fail) {
                if (f->vel[1] < 0 && st == 6) post(e, 0x3C);
                f->state = 1; f->counter = 30;
                continue;
            }
        }
        switch (st) {
        case 0: case 1: case 3: case 4:
            if (ooc) {   /* a wreck steers only while the pitch is outside 2800 to 14400 words, else it waits in state 6 (review of note 07 3.2) */
                const int p = (uint16_t)f->pitch;
                epilogue = 0;
                if (p >= 2800 && p <= 14400) { f->state = 6; goto done; }
                steer(e, i, st);
                goto done;
            }
            epilogue = 0;
            if (f->counter < 0 && !scripted) { end_routine(e, i); continue; }   /* the new state runs on in this pass (floor test, state 6) */
            steer(e, i, st);
            goto done;
        case 6:
            epilogue = 0;
            if (ooc) goto done;   /* an out-of-control state 6 does nothing */
            if (f->counter < 0) {
                if (scripted) { if (e->trig >= 0) { f->state = 8; f->counter = 30; } goto done; }   /* a scripted record waits while the sequencer is idle (rig) */
                f->state = 1; f->counter = 30;
                continue;   /* and runs as state 1 in this pass (rig) */
            }
            goto done;
        case 8: {
            const int roll = (uint16_t)f->roll, pitch = (uint16_t)f->pitch;
            epilogue = 0;
            if (ooc) {
                if (pitch >= 2800 && pitch <= 14400) { f->state = 6; goto done; }
                steer(e, i, 8);
                goto done;
            }
            if (scripted && e->trig >= 0) goto done;   /* the lesson sequencer owns the controls (rig: AI_DIFF_S8) */
            if (scripted && f->counter < 0) { f->state = 6; f->counter = 11; goto done; }
            if (roll >= 7200 && roll <= 21600 && (pitch < 3600 || pitch > 25200)) { f->state = 6; f->counter = 0x25; continue; }   /* and state 6 runs on in this pass (floor test) */
            steer(e, i, 8);
            goto done;
        }
        case 0x0A:
            epilogue = 0;
            if (f->counter < 0) { f->state = 0; f->keys &= 0x03; }
            goto done;
        case 2: {
            const int roll = (uint16_t)f->roll;
            const int T = roll < 14400 ? 7200 : 21600;
            int16_t d = w16(roll - T);
            const int E = roll < T ? w16(-d) : d;
            const int t800 = rate_at(e, T_ROLL, 8);
            if (E >= 1200) {
                const int dd = roll < T ? t800 : -t800;
                f->keys = (uint8_t)(f->keys & 0x03);
                if (dd > 0) f->keys |= FLIGHT_KEY_ROLL_LEFT; else if (dd < 0) f->keys |= FLIGHT_KEY_ROLL_RIGHT;
                goto done;
            }
            f->state = 7; f->counter = 32; f->keys &= 0x03;
            goto done;
        }
        case 7: {
            if (f->counter < 0) { end_routine(e, i); continue; }
            const int d = -rate_at(e, T_PITCH, 4);
            f->keys = (uint8_t)(f->keys & 0x03);
            if (d >= 0) { if (d > f->rate_p) f->keys |= FLIGHT_KEY_STICK_FORWARD; }
            else if (d < f->rate_p) f->keys |= FLIGHT_KEY_STICK_BACK;
            goto done;
        }
        case 5: case 9: {
            if (f->counter < 0) { f->state = 0; f->keys &= 0x03; epilogue = 0; goto done; }
            const int bank = st == 5 ? 1120 : 640, tol = st == 5 ? 480 : 320;
            const int roll = (uint16_t)f->roll;
            const int T = roll < 14400 ? bank : 28800 - bank;
            f->keys = (uint8_t)(f->keys & 0x03);
            if (roll <= T - tol) f->keys |= FLIGHT_KEY_ROLL_LEFT;       /* the tolerance edges act (rig) */
            else if (roll >= T + tol) f->keys |= FLIGHT_KEY_ROLL_RIGHT;
            goto done;
        }
        default:
            epilogue = 0;
            goto done;
        }
    }
done:
    if (epilogue) r->ai_flags |= FL_EPILOGUE;
}

/* ---- the range routine and the speed control (4.2) ------------------------------------------------------------------------------------------ */

void ai_range(entities_t *e, int i) {
    entity_t *r = rec(e, i);
    aircraft_t *a = entities_ac(e, i);
    flight_state_t *f = &a->f;
    if (r->dest_cx < 0) return;
    /* the schedule (rig: AI_DIFF_SWEEP_RANGE): the counter in the high nibble of +0x39 is raised by 1, and the test compares the masked byte as a SIGNED byte against 0x20 (range
     * under 0x900) or 0x50 (further): the counters 3 to 7 and 6 to 7, not 8 and up (those bytes are negative) */
    f->repeat = (uint8_t)(f->repeat + 0x10);
    const int8_t hi = (int8_t)(f->repeat & 0xF0);
    const uint16_t old = (uint16_t)r->range;
    const int again = old < 0x480 ? 1 : (old < 0x900 ? hi > 0x20 : hi > 0x50);
    if (!again) return;
    f->repeat &= 0x0F;
    int32_t hx = add32((int32_t)((uint32_t)(int32_t)w16(r->dest_cx - f->cell_x) << 14), r->dest_fx - f->fine_x);
    int32_t hz = add32((int32_t)((uint32_t)(int32_t)w16(r->dest_cz - f->cell_z) << 14), r->dest_fz - f->fine_z);
    int32_t hy = sub32(r->dest_h, f->alt8);
    hx = abs32(hx); hz = abs32(hz); hy = abs32(hy);
    int range;
    if (hx > 0x7F00 || hy > 0x7F00 || hz > 0x7F00) range = 0x7FFF;   /* a component out of range: 0x7FFF, and +0x04 bit 0 stays as it was (rig) */
    else {
        fx_vec_t v = { hx, hy, hz };
        range = fx_dist(&v);
        if (range > 0x7FFF) range = 0x7FFF;
        f->b4 |= EB4_RANGE_DONE;
    }
    r->range = (int16_t)range;
    const int is_player = i == 0;
    if (range > 0x36C0) {
        if (!is_player) f->id = (uint8_t)((f->id & 0x0F) | 0xF0);
        return;
    }
    if (range > 0x1E00) { if (r->engage == 4) r->engage = 3; }
    else if (r->engage == 3) r->engage = 4;
    if (is_player) return;
    int cls;
    if (range > 0x1800) { r->range = 0x7FFF; cls = 0xF; }
    else if (range > 0x300) cls = range <= 0xC00 ? 2 : 3;
    else cls = 1;
    f->id = (uint8_t)((f->id & 0x0F) | (cls << 4));
}

/* 4.2: the speed control: writes the two low bits of the key byte */
static void speed_control(entities_t *e, int i) {
    entity_t *r = rec(e, i);
    flight_state_t *f = fs(e, i);
    ai_range(e, i);
    const int32_t S = f->speed_target;
    int32_t D = 0;
    int step;   /* the step to continue at: 3, 4, 5, 6 or -1 for "done" */
    int skip_floor = 0;
    if (r->target == 0xFF) {
        if (f->cls == 0x14) {
            if (f->w2 & EW2_GROUND) {
                if (S < 0x100) { f->throttle = 0; f->keys &= (uint8_t)~0x03; return; }
                D = S / 2; skip_floor = 1; step = -1;
            } else {
                if (f->alt8 > 0x6C0) { D = 0x1680; step = 6; }
                else { f->gear &= 0x7F; D = 0x600; step = 6; }
            }
        } else step = 4;
    } else {
        int t = r->target & 0x7F;
        flight_state_t *tf = fs(e, t);
        const int32_t v = tf->speed_target;
        const int range = (uint16_t)r->range;
        if (f->b4 & EB4_THREATENED) {
            if (range <= 0x240) step = 3;
            else if (range >= 0x600) step = 5;
            else step = 6;
        } else if (f->state == 0 || f->state == 1 || (f->w2 & EW2_SCRIPTED)) {
            if (range < 0x180) step = 5;
            else {
                const int both = (r->ai_flags & 0x60) == 0x60;
                if (!both) step = range <= 0x300 ? 5 : 6;
                else step = 3;
            }
        } else {
            if (range <= 0x360) step = 6;
            else if (f->state == 6) step = 3;
            else step = 5;
        }
        D = v;
        if (step == 3) {
            if (range >= 0x300) step = 4;
            else { D = v * 2; step = D <= 0x1BC0 ? 6 : 4; }   /* more than 0x1BC0 falls on to the cruise target */
        }
        if (step == 5) { D = v - (v / 8 < 0x420 ? v / 8 : 0x420); step = 6; }
    }
    if (step == 4) { D = f->cls == 0x15 ? 0x1EC0 : (f->w0 & EW0_NOT_COUNTED) ? 0x15C0 : 0x1D40; step = 6; }
    if (step == 6 && !skip_floor) { if (D < 0x900) D = 0x900; }
    int32_t R = (D + S) >> 1;   /* averaged with the current speed; the throttle bits compare it with the unmoved S (rig: AI_DIFF) */
    f->keys &= (uint8_t)~0x03;
    if (R > S) f->keys |= 0x01; else if (R < S) f->keys |= 0x02;
}

/* ---- the gate (4.1 to 4.5) ------------------------------------------------------------------------------------------------------------------- */

static void to_world_point(const aircraft_t *t, int lx, int ly, int lz, int32_t p[3]) {
    for (int k = 0; k < 3; k++) {
        int32_t sum = 0;
        sum = add32(sum, (int32_t)t->f.mat_b[3 * k + 0] * lx);
        sum = add32(sum, (int32_t)t->f.mat_b[3 * k + 1] * ly);
        sum = add32(sum, (int32_t)t->f.mat_b[3 * k + 2] * lz);
        p[k] = add32(t->pos[k], sum >> 4);
    }
}

static void set_dest_from_point(entity_t *r, const int32_t p[3]) {
    r->dest_cx = (int16_t)(w16(p[0] >> 16) >> 6);
    r->dest_cz = (int16_t)(w16(p[2] >> 16) >> 6);
    r->dest_fx = (int16_t)((p[0] >> 8) & 0x3FFF);
    r->dest_fz = (int16_t)((p[2] >> 8) & 0x3FFF);
    r->dest_h = p[1] >> 8;
}

/* the lost-target rule 0x28156 (4.4): the mission-start entry whose slot is the record's own, in the sub-list, then in the region its tag names */
static void lost_target(entities_t *e, int i) {
    entity_t *r = rec(e, i);
    flight_state_t *f = fs(e, i);
    const scenario_t *sc = e->scn;
    int found = 0;
    uint16_t w[5] = {0};
    if (sc) {
        for (int k = 0; k < e->sub_count && !found; k++) {
            const scenario_entry_t *en = &sc->sub_entry[e->sub_first + k];
            if ((en->w[2] & 0x7F) == (unsigned)i) { memcpy(w, en->w, sizeof w); found = 1; }
        }
        if (!found && r->tag != 0xFF && (int8_t)r->tag > 0 && (int)r->tag - 1 < sc->region_count) {
            const scenario_region_t *rg = &sc->region[r->tag - 1];
            for (int k = 0; k < rg->count && !found; k++) {
                const scenario_entry_t *en = &sc->region_entry[rg->first + k];
                if ((en->w[2] & 0x7F) == (unsigned)i) { memcpy(w, en->w, sizeof w); found = 1; }
            }
        }
    }
    if (found && e->hook.position_block) {
        int16_t b[5];
        if (e->hook.position_block(e->hook.user, w[3] / 2u, b) == 0) {
            set_dest(r, b);
            r->target = 0xFF;
            f->w0 &= (uint16_t)~EW0_ENGAGED;
            return;
        }
    }
    if (!found && (int8_t)r->tag <= 0) post(e, 0x34);
}

void ai_gate(entities_t *e, int i) {
    entity_t *r = rec(e, i);
    aircraft_t *a = entities_ac(e, i);
    flight_state_t *f = &a->f;
    if (e->paused) return;
    f->counter = w16(f->counter - 1);
    if (!(f->w0 & EW0_ACTIVE) || !e->flying) return;
    e->gate_seen |= 1u << i;
    if (f->b20 & 0x02) {   /* a wreck: the destination is the record itself, the speed settles at 0x1200 */
        r->dest_cx = f->cell_x; r->dest_cz = f->cell_z; r->dest_fx = f->fine_x; r->dest_fz = f->fine_z; r->dest_h = 0;
        int32_t S = f->speed_target;
        if (S < 0x1200) { S += 0x90; if (S > 0x1200) S = 0x1200; }
        else if (S > 0x1200) { S -= 0x90; if (S < 0x1200) S = 0x1200; }
        f->speed_target = (int16_t)S; f->speed = (int16_t)S;
        f->w2 &= (uint16_t)~(0x2000u | 0x0008u);
        return;
    }
    if (!(f->w2 & EW2_SCRIPTED) && r->dest_cx >= 0) speed_control(e, i);
    /* the waypoint rule (4.3) */
    if (!(f->w2 & EW2_SCRIPTED) && f->state == 8 && (uint16_t)r->range <= 0x480) {
        int16_t run[ENT_ROUTE_BLOCKS][5];
        const int n = route_get(e, r->route, run, ENT_ROUTE_BLOCKS);
        int at = -1;
        for (int k = 0; k < n; k++)
            if (run[k][0] == r->dest_cx && run[k][1] == r->dest_cz && run[k][2] == r->dest_fx && run[k][3] == r->dest_fz) { at = k; break; }
        if (at < 0) post(e, 0x35);
        else if (at + 1 < n) { set_dest(r, run[at + 1]); r->range = 0x7FFF; }
        else { f->w0 &= (uint16_t)~EW0_AI; if (f->cls == 0x15) f->w0 |= EW0_CRASHED; }
    }
    /* a record that is not counted ends here, except in state 8, which skips the target handling and goes straight to the threatened test (rig) */
    if ((f->w0 & EW0_NOT_COUNTED) && f->state != 8) { e->launch_flag = 0; return; }
    /* target handling (4.4) */
    if (f->state != 8) {
        int t = r->target == 0xFF ? 0 : (r->target & 0x7F);
        flight_state_t *tf = fs(e, t);
        if (!(tf->w0 & EW0_VALID)) { lost_target(e, i); e->launch_flag = 0; return; }
        if (r->target != 0xFF) {   /* with +0x38 = 0xFF neither the engaged bit nor the 5 -> 3 change happens (review of note 07 4.4) */
            if (r->engage == 5) r->engage = 3;
            f->w0 |= EW0_ENGAGED;
        }
        const int t_look = t;   /* the record the lookup found: step 3 below builds its point from it, whatever step 2 does to +0x38 (RIG: tests/rig/test_weapons_diff.c, the demo's MiG) */
        if (t != 0 && f->state != 8) {
            const aircraft_t *pa = entities_ac(e, 0);
            const int32_t box = e->progress <= 1 ? 0x180000 : e->progress == 2 ? 0x240000 : 0x300000;
            if (abs32(sub32(pa->pos[0], a->pos[0])) <= box && abs32(sub32(pa->pos[1], a->pos[1])) <= box && abs32(sub32(pa->pos[2], a->pos[2])) <= box) {
                r->target = 0x80; t = 0; tf = fs(e, 0);
            }
        }
        {   /* the avoidance or pursuit point */
            const aircraft_t *ta = entities_ac(e, t_look);
            int32_t p[3];
            if (f->w2 & EW2_PROX) {
                const int up = (r->ai_flags & 0x02) != 0;
                if (r->ai_flags & 0x01) to_world_point(ta, 0, up ? 96 : -96, -48, p);
                else to_world_point(ta, up ? 168 : -96, 0, -48, p);
            } else to_world_point(ta, 0, 0, -36, p);
            set_dest_from_point(r, p);
        }
    }
    /* the threatened test (4.5) */
    if ((f->repeat >> 4) == 1) {
        /* The target record of the test. A state 8 record skips the target lookup, so the register still holds what the world update left in it (note 07 4.5): the previous gate record when
         * it directly precedes (a launch-decision visit at the records 9, 11, 13 in between leaves the player). Other states use the record named by +0x38 (0xFF: the player). */
        int t;
        if (f->state == 8) {
            int p = -1;
            for (int k = i - 1; k >= 1; k--) if (e->gate_seen & (1u << k)) { p = k; break; }
            int v = -1;
            for (int k = 9; k < i; k += 2) v = k;
            t = (p >= 0 && p >= v) ? p : 0;
        } else t = r->target == 0xFF ? 0 : (r->target & 0x7F);
        if (t >= ENT_RECORDS) t = -1;   /* NOTE-GAP: a target index of 16 or more reads the secondary objects and the variable area in the original (not reachable in the shipped data) */
        if (t < 0) { f->b4 &= (uint8_t)~EB4_THREATENED; goto threat_done; }
        const aircraft_t *ta = entities_ac(e, t);
        const flight_state_t *tf = &ta->f;
        int32_t d[3];
        for (int k = 0; k < 3; k++) d[k] = sub32(ta->pos[k], a->pos[k]);
        int32_t m = abs32(d[0]) > abs32(d[1]) ? abs32(d[0]) : abs32(d[1]);
        if (abs32(d[2]) > m) m = abs32(d[2]);
        int sh = 0;
        while ((m >> sh) >= 0x4800) sh += 2;
        for (int k = 0; k < 3; k++) d[k] >>= sh;
        int th = 0;
        if ((uint16_t)e->rec[t].range <= 0x3000) {   /* the target's own range word +0x4A, in units of 256 position units (note 07 4.5; rig: 100,000 state 8 records, 0 differences) */
            int32_t nv[3];
            ai_normalise(d[0], d[1], d[2], 0xC0, nv);
            const int32_t fwd_t = dot3(nv, tf->mat_b[2], tf->mat_b[5], tf->mat_b[8]);
            const int64_t par = (int64_t)tf->mat_b[2] * f->mat_b[2] + (int64_t)tf->mat_b[5] * f->mat_b[5] + (int64_t)tf->mat_b[8] * f->mat_b[8];
            /* the target's nose points back at the record (dot <= -0x2C0000) and the two forward axes are nearly parallel (dot >= 0xD000000) */
            th = fwd_t <= -0x2C0000 && par >= 0xD000000LL;
            TR("  fwd_t=%d par=%lld nv=%d,%d,%d\n", fwd_t, (long long)par, nv[0], nv[1], nv[2]);
            TR("  MATS tb=%d,%d,%d,%d,%d,%d,%d,%d,%d ta=%d,%d,%d,%d,%d,%d,%d,%d,%d fb=%d,%d,%d,%d,%d,%d,%d,%d,%d fa=%d,%d,%d,%d,%d,%d,%d,%d,%d\n", tf->mat_b[0], tf->mat_b[1], tf->mat_b[2], tf->mat_b[3], tf->mat_b[4], tf->mat_b[5], tf->mat_b[6], tf->mat_b[7], tf->mat_b[8], tf->mat_a[0], tf->mat_a[1], tf->mat_a[2], tf->mat_a[3], tf->mat_a[4], tf->mat_a[5], tf->mat_a[6], tf->mat_a[7], tf->mat_a[8], f->mat_b[0], f->mat_b[1], f->mat_b[2], f->mat_b[3], f->mat_b[4], f->mat_b[5], f->mat_b[6], f->mat_b[7], f->mat_b[8], f->mat_a[0], f->mat_a[1], f->mat_a[2], f->mat_a[3], f->mat_a[4], f->mat_a[5], f->mat_a[6], f->mat_a[7], f->mat_a[8]);
        }
        TR("threat test: th=%d target=%02X t=%d d=%d,%d,%d sh=%d S=%d TS=%d eng=%d range=%d flags=%02X rep=%02X cnt=%d w2=%04X w0=%04X cls=%02X st=%02X b4=%02X pitch=%d roll=%d tpitch=%d troll=%d\n", th, r->target, t, d[0], d[1], d[2], sh, f->speed_target, tf->speed_target, r->engage, (int)(uint16_t)r->range, r->ai_flags, f->repeat, f->counter, f->w2, f->w0, f->cls, f->state, f->b4, (int16_t)f->pitch, (int16_t)f->roll, (int16_t)tf->pitch, (int16_t)tf->roll);
        if (th) f->b4 |= EB4_THREATENED; else f->b4 &= (uint8_t)~EB4_THREATENED;
        if (th && f->state != 6 && (f->state != 8 || (e->mode == 5 && e->launch_flag))) {
            const entity_t *tr = &e->rec[t];
            const int range = (uint16_t)tr->range;
            const int32_t dv = abs32(sub32(f->speed_target, tf->speed_target));
            const int slower = f->speed_target < tf->speed_target;
            int g;
            if (range > 0x1E00) g = 18;
            else {
                g = range <= 0x300 ? 0 : range <= 0xC00 ? 6 : 12;
                if (dv >= 0xC0) g += slower ? 2 : 4;
                if (g >= 6 && e->designated == i && e->launch_flag) g += 1;
            }
            const int pr = e->progress < 0 ? 0 : e->progress > 3 ? 3 : e->progress;
            if (e->tab) {   /* the evasion table 0x30284 of tables/ai.bin: 19 groups of four (counter, state) pairs, one per progress value (4.5) */
                f->counter = e->tab->evasion[g][pr][0]; f->state = (uint8_t)e->tab->evasion[g][pr][1];
            }
            if (f->w0 & EW0_NOT_COUNTED) { f->w0 &= (uint16_t)~EW0_NOT_COUNTED; e->hostile_cap++; e->hostile_count++; }
        }
    }
threat_done:
    e->launch_flag = 0;
}

/* ---- the per-record check 0x2D53C (3.4) ------------------------------------------------------------------------------------------------------ */

void ai_region_check(entities_t *e, int i) {
    entity_t *r = rec(e, i);
    flight_state_t *f = fs(e, i);
    if ((f->cls & 0xF0) != 0x10 || f->state == 8 || (int8_t)r->tag < 0) return;
    if (r->tag == 0) { post(e, 0x1E); return; }
    const scenario_t *sc = e->scn;
    const int ri = r->tag - 1;
    if (!sc || ri >= sc->region_count) return;
    const scenario_region_t *rg = &sc->region[ri];
    const int cx = f->cell_x, cz = f->cell_z;
    const int inside = cx >= (int16_t)rg->box[0] && cx <= (int16_t)rg->box[1] && cz >= (int16_t)rg->box[2] && cz <= (int16_t)rg->box[3];
    if (inside) return;
    for (int k = 0; k < rg->count; k++) {
        const scenario_entry_t *en = &sc->region_entry[rg->first + k];
        if ((en->w[2] & 0x7F) != (unsigned)i) continue;
        int16_t b[5];
        if (e->hook.position_block && e->hook.position_block(e->hook.user, en->w[3] / 2u, b) == 0) set_dest(r, b);
        r->target = 0xFF;
        if (r->engage == 3 || r->engage == 4) r->engage = 5;
        f->w0 &= (uint16_t)~EW0_ENGAGED;
        return;
    }
}

/* ---- the launch decision 0x2701A (4.6) ------------------------------------------------------------------------------------------------------- */

int ai_launch_decision(entities_t *e, int owner) {
    flight_state_t *f = fs(e, owner);
    entity_t *r = rec(e, owner);
    /* the W0 mask 0x8700: hit, dying, crashed, and the keep-moving bit, which a record's first step clears (note 07 4.6) */
    if (f->w0 & 0x8700) return 0;
    /* the order of the tests (note 07 4.6, verified by launch_order_check.py and by test_ai_diff over the whole world update): the W0 mask, out of control, then the global request V:4A874,
     * which the first of the records 9, 11, 13 that asks consumes whatever its owner is (the answer yes is given before the owner's validity or stock is looked at) */
    if (f->b20 & 0x02) return 0;
    if (e->req_ai) { e->req_ai = 0; return 1; }
    if (!(f->w0 & EW0_VALID)) return 0;
    if (r->target & 0x80) {   /* the target tests only when the target byte has bit 7 set (note 07 4.6): a byte with bit 7 clear skips them */
        /* the byte 0xFF names the record 0x7F, which lies outside the entity array (the original reads whatever memory is there; the rig reads zeros: not valid): no valid target. An index
         * of 16 or more would read the secondary objects and then the variable area: NOTE-GAP, treated the same (not reachable in the shipped data). */
        const int t = r->target & 0x7F;
        if (r->target == 0xFF || t >= ENT_RECORDS) return 0;
        const flight_state_t *tf = fs(e, t);
        if (!(tf->w0 & EW0_VALID) || (tf->b20 & 0x02) || (tf->w0 & EW0_ENGAGED)) return 0;
    }
    if ((r->ai_flags & 0x60) != 0x60) return 0;
    if (!(f->w0 & EW0_ENGAGED)) return 0;
    const int rc = f->id >> 4;
    return rc == 2 || rc == 3;
}
