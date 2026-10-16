/* tests/rig/test_training_diff.c - differential test: the port's training state routines and sequencer (src/core/training.c) against the original's world update
 * 0x26AF8 in the Musashi rig, for the record of the instructor (record 4, docs/re/08-game-flow.md section 8).
 *
 * Convention (the one of test_flight_diff.c, note 05 section 1): the image at 0x10000; the variable area 0x4A6E8-0x54EBF cleared; a return instruction over the
 * first word of 0x18E54, 0x1A18C, 0x1F11E and 0x1F0E2; the case written into the records and the variables; rig_run(0x26AF8) with all registers 0. The
 * routines the sequencer calls that are not under test are stubs that log: the placement 0x2CE36 (counted) and the post routine 0x29BC8 (the code word is read
 * from D0 of the call and compared with the port's post). The input conversion 0x1DFAA,
 * which the step calls right after the state routine, is a stub too: when it is called the record of the instructor is read there, i.e. as the state routine
 * left it, before the flight step changes the attitude and clears the recentre bit; when the step does not reach it the record is read after the run.
 *
 * Per case: a random state of the instructor (the state byte from the states of 8.5, the counter, the phase flags, the attitude words and rates biased to the
 * thresholds of the note, the altitude, the speed word, the key byte, W2) and of the sequencer (mode byte 2 or 0x7D, lesson, V:4A852, V:54E42, V:4A84B, pause,
 * the viewed entity, the requests, the player's speed). The port runs training_step on the same state with no flight environment installed (so the flight step
 * is a no-op) and the two are compared on: the state byte, the key byte, the counter, the phase flags, the low byte of W2 (recentre, the request bit; not bit 0, the proximity bit, which a neighbour's step sets), +0x64, the
 * throttle, the lesson number, V:4A852, V:54E42, the count of placements, the post list and the cells of the player's pass (V:4A968).
 * Skipped (exit 0) when extracted/interceptor.bin is absent. Run from tools/m68k-rig (make test-training). Environment: TRAINING_DIFF_N (cases, default 20000),
 * TRAINING_DIFF_SEED, TRAINING_DIFF_VERBOSE (print every difference), TRAINING_DIFF_STATE (only this state byte). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/training.h"
#include "../../src/core/flight.h"

#define BASE 0x10000u
#define V_LO 0x4A6E8u
#define V_HI 0x54EC0u
#define REC 0x4B23Cu
#define REC4 (REC + 4 * 0x200u)

static uint32_t rng = 0x9E3779B9u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }
static int pct(int p) { return rnd(0, 99) < p; }

static void put8(uint32_t a, int v) { uint8_t b = (uint8_t)v; rig_poke(a, &b, 1); }
static void put16(uint32_t a, int v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 2); }
static void put32(uint32_t a, int32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 4); }
static int get8(uint32_t a) { uint8_t b; rig_peek(a, &b, 1); return b; }
static int get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return (int16_t)((b[0] << 8) | b[1]); }

/* ---- the snapshot of the instructor's record and the sequencer's variables --------------------------------------------------------------------- */
typedef struct {
    int state, keys, counter, b20, w2lo, b64, throttle, w0, turn;
    int lesson, trig, cursor;
    int placements, nposts;
    int posts[8];
} snap_t;

static int hook_calls, placements, nposts, posts_raw[8];
static snap_t hooked;
static int have_hook;

static void read_record(snap_t *s) {
    s->state = get8(REC4 + 0x05); s->keys = get8(REC4 + 0x65); s->counter = get16(REC4 + 0x4C); s->b20 = get8(REC4 + 0x20);
    s->w2lo = get8(REC4 + 0x03); s->b64 = get8(REC4 + 0x64); s->throttle = (int8_t)get8(REC4 + 0x2B); s->w0 = get16(REC4 + 0x00) & 0xFFFF; s->turn = get16(REC4 + 0x7E) & 0xFFFF;
    s->lesson = get8(0x4A851); s->trig = get8(0x4A852); s->cursor = get16(0x54E42) & 0xFFFF;
}
/* The player's record as it stands at the first call of the input conversion (the player's own, after the re-position of its pre-steps and before its step). */
typedef struct { int32_t pos[3], alt8; int cell_x, cell_z, fine_x, fine_z, cls, id, group, w2, state, keys, throttle, speed, v4a910, v4a968; int mat_b[9]; } p0_t;
static p0_t r0;
static int seq_player_speed;   /* +0x6E of record 0 at the last call: the player's own step has run (record 0 is processed first), the sequencer read this */
static int have_r0;
static int32_t get32r(uint32_t a) { uint8_t b[4]; rig_peek(a, b, 4); return (int32_t)(((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]); }
static void read_player(p0_t *p) {
    for (int i = 0; i < 3; i++) p->pos[i] = get32r(REC + 0x14 + 4 * i);
    p->alt8 = get32r(REC + 0x10);
    p->cell_x = get16(REC + 0x06); p->cell_z = get16(REC + 0x08); p->fine_x = get16(REC + 0x0C); p->fine_z = get16(REC + 0x0E);
    p->cls = get8(REC + 0x62); p->id = get8(REC + 0x63); p->group = get8(REC + 0x5E); p->w2 = get16(REC + 0x02) & 0xFFFF; p->state = get8(REC + 0x05); p->keys = get8(REC + 0x65);
    p->throttle = (int8_t)get8(REC + 0x2B); p->speed = get16(REC + 0x6E); p->v4a910 = get8(0x4A910); p->v4a968 = get8(0x4A968);
    for (int i = 0; i < 9; i++) p->mat_b[i] = get16(REC + 0x92 + 2 * i);
}
static void hook_input_conversion(void) {
    if (!hook_calls) { read_player(&r0); have_r0 = 1; }
    read_record(&hooked); have_hook = 1; hook_calls++;
    seq_player_speed = get16(REC + 0x6E);
}
/* The placement 0x2CE36 is a stub that performs the writes the note names (8.3, 6.1) so that the rest of the pass sees the record as the original's placement leaves it:
 * part 1: the head of the record (+0x00 to +0xA3) cleared, then W0 0x11C8, the fixed position, attitude 0, the speed words 8192, group 4, class 0x10, id 0x3D, +0x64 = 0x10; part 2 (0x2CFDA): the first 0xA4 bytes of
 * record 0 over record 4, class 0x10, group 4, state and key bytes 0, W2 without 0x0100 and 0x0800 (the port's reading where the note is silent). */
static void put32w(uint32_t a, int32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 4); }
static void hook_placement(void) {
    placements++;
    if (get8(0x4A95E) == 2) {
        static const uint8_t zero_head[0xA4];
        rig_poke(REC4, zero_head, sizeof zero_head);   /* the original's placement rewrites bytes +0x00 to +0xA3 of the record (tests/rig/test_training_traj.c compares the whole result) */
        put8(REC4 + 0x64, 0x10);
        put16(REC4 + 0x00, 0x11C8);
        put32w(REC4 + 0x14, 307757056); put32w(REC4 + 0x18, 3145728); put32w(REC4 + 0x1C, 328728576);
        put16(REC4 + 0x66, 0); put16(REC4 + 0x68, 0); put16(REC4 + 0x6A, 0); put16(REC4 + 0x6C, 8192); put16(REC4 + 0x6E, 8192);
        put8(REC4 + 0x2B, 0); put8(REC4 + 0x5E, 4); put8(REC4 + 0x62, 0x10); put8(REC4 + 0x63, 0x3D);
        for (int i = 0; i < 9; i++) { put16(REC4 + 0x80 + 2 * i, (i % 4 == 0) ? 16384 : 0); put16(REC4 + 0x92 + 2 * i, (i % 4 == 0) ? 16384 : 0); }   /* the matrices of attitude 0 */
    } else {
        uint8_t buf[0xA4];
        rig_peek(REC, buf, sizeof buf);
        rig_poke(REC4, buf, sizeof buf);
        put8(REC4 + 0x62, 0x10); put8(REC4 + 0x5E, 4); put8(REC4 + 0x05, 0); put8(REC4 + 0x65, 0);
        put16(REC4 + 0x02, get16(REC4 + 0x02) & ~0x0900);
    }
}
static void hook_post(void) {
    /* the flight model's own direct posts (0x9C06 low altitude, 0xD02A) come from the player's step in the same pass: not the training's */
    { const int code = (int)(rig_get_reg(RIG_D0) & 0xFFFF); if (code == 0x9C06 || code == 0xD02A) return; }
    if (nposts < 8) {
        posts_raw[nposts] = (int)(rig_get_reg(RIG_D0) & 0xFFFF);   /* the code word of the call is in D0 only (3,319 of 3,319 posts at the last run) */
    }
    nposts++;
}

/* ---- the random case ----------------------------------------------------------------------------------------------------------------------------- */
static const uint8_t STATES[] = { 0x00, 0x0A, 0x02, 0x07, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,
                                  0x1C, 0x1E, 0x1F, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28 };
typedef struct {
    int mode, lesson, trig, cursor, running, paused, viewed, p_speed, p_req, i_req, scripted, active, sim;
    int state, counter, b20, w2lo, keys, throttle, roll, pitch, rate_p, rate_r, rate_h, speed, w0, turn;
    int32_t alt;
    int mat[9];
    int32_t p_pos[3];
} cs_t;

static int biased_angle(void) {
    static const int pts[] = { 0, 800, 1200, 2400, 7200, 13760, 14400, 21600, 26400, 26800, 28160, 28799 };
    int v = pts[rnd(0, 11)];
    if (pct(60)) v += rnd(-2, 2);
    if (pct(15)) v = rnd(0, 28799);
    if (v < 0) v += 28800;
    return v % 28800;
}
static int ai_only, sweep_value;
static void gen(cs_t *c, int only_state) {
    memset(c, 0, sizeof *c);
    c->mode = pct(50) ? 2 : 0x7D;
    c->lesson = pct(10) ? rnd(0, 9) : rnd(0, 7);
    c->trig = pct(60) ? 0 : pct(50) ? 0xFF : 0xFE;
    c->cursor = pct(50);
    c->running = pct(80);
    c->paused = 0;   /* with the pause byte set the pass only clears W2 bit 0x0001; the port does not run (unit test) */
    c->viewed = pct(50) ? 4 : 0;
    c->p_speed = pct(30) ? 0 : rnd(1, 3000);
    c->p_req = pct(20);
    c->i_req = pct(20);
    c->scripted = pct(92);
    c->active = pct(95);
    c->sim = 1;   /* with V:4A984 bit 6 clear the pass clears W0 bit 0x0100 and runs no state routine; the game never steps the instructor then */
    c->state = only_state >= 0 ? only_state : STATES[rnd(0, (int)sizeof STATES - 1)];
    { static const int cn[] = { -2, -1, 0, 1, 2, 5, 14, 15, 32, 36, 60 }; c->counter = pct(70) ? cn[rnd(0, 10)] : rnd(-5, 70); }
    c->b20 = (pct(50) ? 0x08 : 0) | (pct(50) ? 0x10 : 0) | (pct(30) ? (int)(xr() & 0xE5) : 0);   /* bit 1 (out of control) is never set on the instructor (8.5): the pass does other things with it */
    c->w2lo = pct(30) ? (int)(xr() & 0xFF) : 0;
    c->keys = pct(50) ? 0 : (int)(xr() & 0xFF);
    { static const int th[] = { 0, 9, 0x5F, 0x60, 0x61, -1 }; c->throttle = pct(70) ? th[rnd(0, 5)] : rnd(-32, 120); }
    c->roll = biased_angle();
    c->pitch = biased_angle();
    c->rate_r = pct(50) ? 0 : pct(50) ? rnd(-300, 300) : rnd(-1500, 1500);
    c->rate_h = pct(70) ? 0 : rnd(-300, 300);
    if ((c->state == 0x0A || c->state == 6 || c->state == 8) && pct(70)) {   /* the wait states level the aircraft: small angles on both sides of 0 */
        c->roll = (rnd(-1600, 1600) + 28800) % 28800; c->pitch = (rnd(-1600, 1600) + 28800) % 28800;
    }
    { static const int rt[] = { 0, -40, -41, -39, -320, -321, -319, -400, -401, -399, -480, -481, -479, 320, 321, 319 }; c->rate_p = pct(60) ? rt[rnd(0, 15)] : rnd(-700, 700); }
    { static const int sp[] = { 0, 0x83F, 0x840, 0x841, 8192 }; c->speed = pct(60) ? sp[rnd(0, 4)] : rnd(-100, 5000); }
    c->w0 = 0x11C8 | (pct(40) ? 0x2000 : 0);
    /* the turn limit +0x7E the record carries into the pass (07 3.1: the state routine writes 0x1400 at its entry on every call, whatever the word held; the steering of the generic AI leaves other values) */
    c->turn = pct(40) ? 0x1400 : pct(30) ? 0 : pct(50) ? 0x0E00 : rnd(0, 0x3000);
    for (int i = 0; i < 9; i++) c->mat[i] = pct(40) ? ((i % 4 == 0) ? 16384 : 0) : rnd(-16384, 16384);
    {   /* the re-positioned player must not meet the lead (the rig's world update would let the two collide: flight model, not training): a junk matrix whose offset is short becomes the identity */
        static const int w[3] = { 9, 1, -97 };
        long mag = 0;
        for (int r = 0; r < 3; r++) { long sum = 0; for (int k = 0; k < 3; k++) sum += (long)c->mat[3 * r + k] * w[k]; sum >>= 4; mag += sum < 0 ? -sum : sum; }
        if (mag < 90000) for (int i = 0; i < 9; i++) c->mat[i] = (i % 4 == 0) ? 16384 : 0;
    }
    c->p_pos[0] = rnd(0, 0x1FFFFFFF); c->p_pos[2] = rnd(0, 0x1FFFFFFF); c->p_pos[1] = 0x708;
    if (!getenv("TRAINING_DIFF_WILD")) {   /* the states the game can reach: V:4A852 stays 0 and the instructor is scripted in part 1; V:4A84B is set in part 1 only; the requests exist only while a trigger is awaited */
        if (c->mode == 2) { c->trig = 0; c->scripted = 1; c->viewed = 4; }
        else { c->running = 0; c->viewed = 0; if (c->trig == 0) c->p_req = c->i_req = 0; }
        if (c->p_req && c->i_req) c->i_req = 0;   /* RETURN and the trigger in one pass: the player's own step runs between the re-position and the re-placement in the rig and changes the record the stub copies (+0x20), which the harness does not run */
        if (!c->scripted) {   /* an unscripted instructor runs the original's generic AI, which does nothing in the rig (note 08 8.3, "The unscripted lead"): only its real situation, the start of part 2 (lesson 0, no request, state 0) is compared */
            c->mode = 0x7D; c->running = 0; c->viewed = 0; c->lesson = 0; c->trig = 0; c->cursor = 0; c->p_req = c->i_req = 0; c->state = 0; c->w2lo &= ~0x0800;
        }
    }
    if (ai_only) {   /* the sequencer inert: the manoeuvre is running (cursor set, state not 0), no request, nothing paused */
        c->mode = 2; c->running = 1; c->paused = 0; c->cursor = 1; c->p_req = c->i_req = 0; c->scripted = c->active = c->sim = 1; c->trig = 0;
        if (c->state == 0) c->state = 0x0D;
        c->w2lo &= ~0x0800;
    }
    { static const int32_t al[] = { 0x100000, 0x0FFFFF, 0x100001, 0x240000, 0x240001, 0x23FFFF, 0x300000, 0x1000 }; c->alt = pct(60) ? al[rnd(0, 7)] : rnd(0, 0x500000); }
}

static const char *image_path;
static int rig_reset(void) {
    if (rig_load_image(image_path, BASE) != 0) return -1;
    static const uint32_t patch[] = { 0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2 };
    for (int i = 0; i < 4; i++) put16(patch[i], 0x4E75);
    static uint8_t zero[V_HI - V_LO];
    rig_poke(V_LO, zero, sizeof zero);
    rig_hooks_clear();
    rig_hook_set(0x1DFAA, hook_input_conversion, 0);
    rig_hook_set(0x2CE36, hook_placement, 0);
    rig_hook_set(0x29BC8, hook_post, 0);
    return 0;
}

static void rig_write(const cs_t *c) {
    put8(0x4A95E, c->mode); put8(0x4A851, c->lesson); put8(0x4A852, c->trig); put16(0x54E42, c->cursor); put8(0x4A84B, c->running); put8(0x4A866, c->paused);
    put16(0x4A994, c->viewed); put16(0x4A996, c->viewed ? 0x800 : 0);
    put8(0x4A84D, 1); put8(0x4A846, 1);
    put16(0x4A984, c->sim ? 0x0840 : 0x0800);
    put32(0x4A7B2, 0x800000);
    put8(0x4A83C, 1); put8(0x4A847, 1); put8(0x4A844, 1); put8(0x4A83D, 1);
    /* the player: record 0, an active F/A-18 on the ground */
    put16(REC + 0x00, 0x11C8); put16(REC + 0x02, c->p_req ? 0x0880 : 0x0080); put8(REC + 0x62, 0x11); put16(REC + 0x6E, c->p_speed); put8(REC + 0x63, 0x0D);
    put32(REC + 0x18, 0x708);
    /* the instructor: record 4 */
    put16(REC4 + 0x00, c->active ? c->w0 : (c->w0 & ~0x1000));
    put16(REC4 + 0x02, (c->scripted ? 0x0100 : 0) | (c->i_req ? 0x0800 : 0) | c->w2lo);
    put8(REC4 + 0x05, c->state); put8(REC4 + 0x20, c->b20); put8(REC4 + 0x2B, c->throttle); put16(REC4 + 0x4C, c->counter);
    put16(REC4 + 0x56, c->rate_p); put16(REC4 + 0x5A, c->rate_r); put16(REC4 + 0x58, c->rate_h); put8(REC4 + 0x5E, 4); put8(REC4 + 0x62, 0x10); put8(REC4 + 0x63, 0x3D); put8(REC4 + 0x65, c->keys);
    put16(REC4 + 0x66, c->pitch); put16(REC4 + 0x68, 0); put16(REC4 + 0x6A, c->roll); put16(REC4 + 0x6C, 8192); put16(REC4 + 0x6E, c->speed);
    put32(REC4 + 0x14, 307757056); put32(REC4 + 0x18, c->alt); put32(REC4 + 0x1C, 328728576);
    put32(REC4 + 0x72, 0x61A800); put16(REC4 + 0x7E, c->turn);
    put16(REC4 + 0x2C, c->mode == 2 ? 0 : 0xFFFF); if (c->mode == 2) put8(REC4 + 0x38, 0xFF);   /* the placements' destination and target words (8.3, 6.1) */
    /* the matrices: identity, or in the cases with a re-position random Q14 words (the offset (+9, +1, -97) goes through them) */
    for (int i = 0; i < 9; i++) { put16(REC4 + 0x80 + 2 * i, (i % 4 == 0) ? 16384 : 0); put16(REC4 + 0x92 + 2 * i, c->mat[i]); }
    put32(REC + 0x14, c->p_pos[0]); put32(REC + 0x1C, c->p_pos[2]);
}

static int diffs, cases, total_posts;
static int verbose;
static void report(const cs_t *c, const char *what, long port, long rig, int idx) {
    diffs++;
    if (verbose || diffs <= 12)
        printf("  case %d: %s: port %ld, rig %ld  (mode %02X lesson %d trig %02X cursor %d run %d pause %d view %d pspeed %d preq %d ireq %d scr %d act %d sim %d "
               "state %02X cnt %d b20 %02X w2lo %02X keys %02X thr %d roll %d pitch %d E %d W %d alt %X)\n",
               idx, what, port, rig, c->mode, c->lesson, c->trig, c->cursor, c->running, c->paused, c->viewed, c->p_speed, c->p_req, c->i_req, c->scripted, c->active, c->sim,
               c->state, c->counter, c->b20, c->w2lo, c->keys, c->throttle, c->roll, c->pitch, c->rate_p, c->speed, c->alt);
}

static aircraft_t PLAYER;
static trainer_t T;

static void port_run(const cs_t *c) {
    training_t tb;
    memset(&tb, 0, sizeof tb);
    static const int16_t prompt[8] = { 0, 46, 49, 48, 50, 51, 47, 52 }, name[8] = { 0, 23, 26, 25, 27, 43, 24, 28 };
    static const uint8_t st[8] = { 0x23, 0x0B, 0x27, 0x0E, 0x11, 0x14, 0x17, 0x1A };
    for (int i = 0; i < 8; i++) { tb.a[i].prompt_id = prompt[i]; tb.a[i].name_id = name[i]; tb.b[i].camera = 0x0103; tb.b[i].script = 0x80000000u | st[i]; }
    tb.pitch_rate[4] = 320; tb.pitch_rate[6] = 400; tb.pitch_rate[7] = 480; tb.roll_rate[7] = 720; tb.roll_rate[8] = 800;
    memset(&PLAYER, 0, sizeof PLAYER);
    PLAYER.f.cls = 0x11; PLAYER.f.id = 0x0D; PLAYER.f.w0 = 0x11C8; PLAYER.f.w2 = (uint16_t)(c->p_req ? 0x0880 : 0x0080); PLAYER.f.speed = (int16_t)seq_player_speed;
    PLAYER.f.v4a984 = (uint16_t)(c->sim ? 0x0840 : 0x0800); PLAYER.f.v4a844 = 1; PLAYER.f.v4a847 = 1; PLAYER.f.v4a95e = (uint8_t)c->mode; PLAYER.f.v4a7b2 = 0x800000;
    training_init(&T, &tb);
    training_start(&T, c->mode, &PLAYER);
    flight_state_t *f = &T.inst.f;
    f->w0 = (uint16_t)(c->active ? c->w0 : (c->w0 & ~0x1000));
    f->w2 = (uint16_t)((c->scripted ? 0x0100 : 0) | (c->i_req ? 0x0800 : 0) | c->w2lo);
    f->state = (uint8_t)c->state; f->b20 = (uint8_t)c->b20; f->throttle = (int8_t)c->throttle; f->counter = (int16_t)c->counter; f->rate_p = (int16_t)c->rate_p; f->rate_r = (int16_t)c->rate_r; f->rate_h = (int16_t)c->rate_h;
    f->turn_limit = (int16_t)c->turn;
    f->keys = (uint8_t)c->keys; f->pitch = (uint16_t)c->pitch; f->roll = (uint16_t)c->roll; f->speed = (int16_t)c->speed; f->speed_target = 8192;
    T.inst.pos[0] = 307757056; T.inst.pos[1] = c->alt; T.inst.pos[2] = 328728576;
    T.lesson = (uint8_t)c->lesson; T.trig = (uint8_t)c->trig; T.cursor = (uint16_t)c->cursor; T.running = (uint8_t)c->running; T.paused = (uint8_t)c->paused;
    T.viewed_inst = c->viewed != 0;
    T.placements = 1; T.b64 = 0;
    for (int i = 0; i < 9; i++) f->mat_b[i] = (int16_t)c->mat[i];
    PLAYER.pos[0] = c->p_pos[0]; PLAYER.pos[1] = c->p_pos[1]; PLAYER.pos[2] = c->p_pos[2];
    training_clear_outputs(&T);
    /* the caller's pause byte is V:4A866 = c->paused: the port's module reads t->paused */
    training_player_pass(&T, &PLAYER);   /* 0x270A0 in the player's pre-steps, then record 4's pass */
    training_step(&T, &PLAYER);
}

int main(int argc, char **argv) {
    const char *paths[] = {"../../extracted/interceptor.bin", "extracted/interceptor.bin"};
    (void)argc; (void)argv;
    image_path = NULL;
    if (rig_init(1 << 21) != 0) { printf("rig_init failed\n"); return 1; }
    for (int i = 0; i < 2 && !image_path; i++) if (rig_load_image(paths[i], BASE) == 0) image_path = paths[i];
    if (!image_path) { printf("test_training_diff: skipped (extracted/interceptor.bin not found)\n"); rig_shutdown(); return 0; }
    const int N = getenv("TRAINING_DIFF_N") ? atoi(getenv("TRAINING_DIFF_N")) : 20000;
    if (getenv("TRAINING_DIFF_SEED")) rng = (uint32_t)atoi(getenv("TRAINING_DIFF_SEED")) * 2654435761u + 1;
    verbose = getenv("TRAINING_DIFF_VERBOSE") != NULL;
    ai_only = getenv("TRAINING_DIFF_AI") != NULL;
    const int only = getenv("TRAINING_DIFF_STATE") ? (int)strtol(getenv("TRAINING_DIFF_STATE"), NULL, 16) : -1;
    int with_repos = 0, with_post = 0, with_place = 0, state_changes = 0, ai_cases = 0, skipped = 0;
    for (int n = 0; n < N; n++) {
        cs_t c;
        gen(&c, only);
        if (getenv("TRAINING_DIFF_FIXED")) {   /* the instructor at step 56 of the part 1 cycle, as the trajectory test leaves it (state 0x0A just entered, roll 304, roll rate 252) */
            memset(&c, 0, sizeof c);
            c.mode = 2; c.lesson = 1; c.cursor = 1; c.running = 1; c.viewed = 4; c.scripted = 1; c.active = 1; c.sim = 1;
            c.state = 0x0A; c.counter = 36; c.b20 = 0x18; c.throttle = 0x60; c.roll = 304; c.pitch = 0; c.rate_p = 0; c.rate_r = 252; c.speed = 4846; c.w0 = 0x30C8;
            c.alt = 3145656;
            if (getenv("F_ROLL")) c.roll = atoi(getenv("F_ROLL"));
            if (getenv("F_PITCH")) c.pitch = atoi(getenv("F_PITCH"));
            if (getenv("F_RATER")) c.rate_r = atoi(getenv("F_RATER"));
            if (getenv("F_RATEP")) c.rate_p = atoi(getenv("F_RATEP"));
            if (getenv("F_W0")) c.w0 = (int)strtol(getenv("F_W0"), NULL, 16);
            if (getenv("F_STATE")) c.state = (int)strtol(getenv("F_STATE"), NULL, 16);
            if (getenv("F_COUNTER")) c.counter = atoi(getenv("F_COUNTER"));
            if (getenv("F_SPEED")) c.speed = atoi(getenv("F_SPEED"));
            if (getenv("F_B20")) c.b20 = (int)strtol(getenv("F_B20"), NULL, 16);
            if (getenv("F_ALT")) c.alt = atoi(getenv("F_ALT"));
            if (getenv("F_LESSON")) c.lesson = atoi(getenv("F_LESSON"));
            if (getenv("F_CURSOR")) c.cursor = atoi(getenv("F_CURSOR"));
            if (getenv("F_KEYS")) c.keys = (int)strtol(getenv("F_KEYS"), NULL, 16);
            if (getenv("F_MODE")) { c.mode = (int)strtol(getenv("F_MODE"), NULL, 16); if (c.mode == 0x7D) { c.running = 0; c.viewed = 0; } }
            if (getenv("F_TRIG")) c.trig = (int)strtol(getenv("F_TRIG"), NULL, 16);
            if (getenv("F_PART2")) {   /* the start of part 2: unscripted lead, lesson 0 */
                c.mode = 0x7D; c.running = 0; c.viewed = 0; c.lesson = 0; c.cursor = 0; c.scripted = 0; c.state = 0; c.counter = -1; c.b20 = 0;
                c.p_speed = getenv("F_PSPEED") ? atoi(getenv("F_PSPEED")) : 100;
                if (getenv("F_COUNTER")) c.counter = atoi(getenv("F_COUNTER"));
                if (getenv("F_SCRIPTED")) c.scripted = atoi(getenv("F_SCRIPTED"));
            }
            if (getenv("F_SWEEP")) {   /* NAME:from  -> the field takes from + n in case n */
                char nm[16]; int from = 0;
                if (sscanf(getenv("F_SWEEP"), "%15[A-Z]:%d", nm, &from) == 2) {
                    const int v = from + n;
                    if (!strcmp(nm, "ROLL")) c.roll = (v % 28800 + 28800) % 28800;
                    else if (!strcmp(nm, "PITCH")) c.pitch = (v % 28800 + 28800) % 28800;
                    else if (!strcmp(nm, "RATER")) c.rate_r = v;
                    else if (!strcmp(nm, "RATEP")) c.rate_p = v;
                    else if (!strcmp(nm, "COUNTER")) c.counter = v;
                    else if (!strcmp(nm, "SPEED")) c.speed = v;
                    else if (!strcmp(nm, "THR")) c.throttle = v;
                    else if (!strcmp(nm, "ALT")) c.alt = v;
                    else if (!strcmp(nm, "KEYS")) c.keys = v & 0xFF;
                    sweep_value = v;
                }
            }
            for (int i = 0; i < 9; i++) c.mat[i] = (i % 4 == 0) ? 16384 : 0;
            c.p_pos[1] = 0x708;
        }
        /* the cases the port deliberately does not cover: a positive script pointer is not in the tables; lesson 8 and 9 do not occur in the original (the advance wraps 8) */
        if (getenv("TRAINING_DIFF_ONLY") && n != atoi(getenv("TRAINING_DIFF_ONLY"))) continue;
        if (c.lesson > 7) c.lesson = 7;
        if (rig_reset() != 0) { printf("rig_reset failed\n"); return 1; }
        rig_write(&c);
        hook_calls = 0; have_hook = 0; have_r0 = 0; seq_player_speed = c.p_speed; placements = 0; nposts = 0;
        for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
        if (rig_run(0x26AF8u) != 0) { skipped++; continue; }
        snap_t rs;
        if (have_hook) rs = hooked; else read_record(&rs);
        const int end_wait = get8(REC4 + 0x05) == 0x0A || get8(REC4 + 0x05) == 6 || !c.scripted;   /* the wait states 0x0A and 6 level the wings; an unscripted record's key byte is cut to 0xC3 by the conversion (8.3) */
        if (end_wait) rs.keys = get8(REC4 + 0x65);   /* the wait state levels the wings after the input conversion: the key byte at the end of the pass */
        if (getenv("TRAINING_DIFF_FIXED")) {
            static int last_keys = -1;
            const int ek = get8(REC4 + 0x65);
            if (!getenv("F_SWEEP") || ek != last_keys) printf("  end of pass (sweep value %d): keys %02X state %02X w0 %04X roll %d cmd_roll %d\n", sweep_value, ek, get8(REC4 + 0x05), get16(REC4) & 0xFFFF, get16(REC4 + 0x6A), (int8_t)get8(REC4 + 0x2A));
            last_keys = ek;
        }
        rs.turn = get16(REC4 + 0x7E) & 0xFFFF;   /* the turn limit is compared at the end of the pass (the first pass of a placed record writes it in the step, not in the state routine) */
        port_run(&c);
        cases++;
        snap_t ps;
        const flight_state_t *f = &T.inst.f;
        ps.state = f->state; ps.keys = f->keys; ps.counter = f->counter; ps.b20 = f->b20; ps.w2lo = f->w2 & 0xFF; ps.b64 = T.b64; ps.throttle = f->throttle; ps.w0 = f->w0; ps.turn = (uint16_t)f->turn_limit;
        ps.lesson = T.lesson; ps.trig = T.trig; ps.cursor = T.cursor;
        const int ppl = T.placements - 1;
        if (getenv("TRAINING_DIFF_ALL"))
            printf("ALL scr %d trig %02X mode %02X state %02X cnt %d b20 %02X w2in %02X thr %d keys %02X W %d E %d roll %d pitch %d | rig(hook keys %02X) state %02X keys %02X cnt %d b20 %02X w2 %02X thr %d | port state %02X keys %02X cnt %d b20 %02X w2 %02X thr %d\n", c.scripted, c.trig, c.mode, c.state, c.counter, c.b20, c.w2lo, c.throttle, c.keys, c.speed, c.rate_p, c.roll, c.pitch, hooked.keys, rs.state, rs.keys, rs.counter, rs.b20, rs.w2lo, rs.throttle, ps.state, ps.keys, ps.counter, ps.b20, ps.w2lo, ps.throttle);
        if (rs.state != ps.state) report(&c, "state", ps.state, rs.state, n);
        else if (rs.keys != ps.keys && !(end_wait && ((rs.keys ^ ps.keys) & ~0x03) == 0)) report(&c, "keys", ps.keys, rs.keys, n);   /* in the wait the throttle bits are also cut by the flight step's detents, which this run does not have */
        else if ((int16_t)rs.counter != (int16_t)ps.counter) report(&c, "counter", ps.counter, rs.counter, n);
        else if (rs.b20 != ps.b20) report(&c, "b20", ps.b20, rs.b20, n);
        else if ((rs.w2lo ^ ps.w2lo) & 0xFE) report(&c, "w2lo", ps.w2lo, rs.w2lo, n);
        else if (c.scripted && rs.b64 != ps.b64) report(&c, "b64", ps.b64, rs.b64, n);
        else if (rs.throttle != ps.throttle) report(&c, "throttle", ps.throttle, rs.throttle, n);
        else if (rs.turn != ps.turn) report(&c, "turn limit +0x7E", ps.turn, rs.turn, n);   /* the state routine writes 0x1400 on every call (07 3.1) */
        else if ((rs.w0 ^ ps.w0) & ~0x2100) report(&c, "w0", ps.w0, rs.w0, n);   /* bit 0x0100 is cleared and 0x2000 set by the pass after the point the snapshot is taken */
        else if (rs.lesson != ps.lesson) report(&c, "lesson", ps.lesson, rs.lesson, n);
        else if (rs.trig != ps.trig) report(&c, "trig", ps.trig, rs.trig, n);
        else if (rs.cursor != ps.cursor) report(&c, "cursor", ps.cursor, rs.cursor, n);
        else if (placements != ppl) report(&c, "placements", ppl, placements, n);
        else if (nposts != T.npost) {
            report(&c, "post count", T.npost, nposts, n);
            if (verbose) { printf("    rig posts:"); for (int k = 0; k < nposts && k < 8; k++) printf(" %04X", posts_raw[k]); printf("   port posts:"); for (int k = 0; k < T.npost; k++) printf(" %04X", T.posts[k]); printf("\n"); }
        }
        else {
            int bad = 0;
            for (int k = 0; k < T.npost && k < 8; k++) {
                if (posts_raw[k] != T.posts[k]) { report(&c, "post code", T.posts[k], posts_raw[k], n); bad = 1; break; }
                total_posts++;
            }
            (void)bad;
        }
        if (have_r0 && c.mode == 0x7D && c.p_req && c.trig >= 0x80) with_repos++;
        if (have_r0 && c.mode == 0x7D) {   /* (part 1 consumes the request in the instructor's pass, after the snapshot) the player's record after its pre-steps: the re-position of 8.6 */
            const flight_state_t *p = &PLAYER.f;
            const char *bad = NULL; long pv = 0, rv = 0;
#define CMP(name, pval, rval) do { if (!bad && (long)(pval) != (long)(rval)) { bad = name; pv = (long)(pval); rv = (long)(rval); } } while (0)
            CMP("player x", PLAYER.pos[0], r0.pos[0]); CMP("player y", PLAYER.pos[1], r0.pos[1]); CMP("player z", PLAYER.pos[2], r0.pos[2]);
            CMP("player alt8", p->alt8, r0.alt8); CMP("player cell_x", p->cell_x, r0.cell_x); CMP("player cell_z", p->cell_z, r0.cell_z);
            CMP("player fine_x", p->fine_x, r0.fine_x); CMP("player fine_z", p->fine_z, r0.fine_z);
            CMP("player cls", p->cls, r0.cls); CMP("player id", p->id, r0.id); CMP("player group", p->group, r0.group);
            CMP("player w2", (int16_t)(p->w2 & 0xFFFE), (int16_t)(r0.w2 & 0xFFFE));   /* bit 0 is the proximity bit: set by the neighbour's step (the player stands 97 units behind the lead) */ CMP("player state", p->state, r0.state); CMP("player keys", p->keys, r0.keys);
            CMP("player throttle", p->throttle, r0.throttle); if (c.p_req && c.trig >= 0x80) CMP("player speed", p->speed, r0.speed); CMP("player v4a910", p->v4a910, r0.v4a910);
            for (int i = 0; i < 9; i++) CMP("player mat_b", p->mat_b[i], r0.mat_b[i]);
#undef CMP
            if (bad) report(&c, bad, pv, rv, n);
        }
        if (nposts) with_post++;
        if (placements) with_place++;
        if (rs.state != c.state) state_changes++;
        if (have_hook) ai_cases++;
    }
    printf("test_training_diff: %d cases (%d run in the rig and compared, %d skipped), %d differences; %d with a re-position of the player, %d with a post, %d with a placement, %d changing the state byte, %d reaching the state routine\n",
           N, cases, skipped, diffs, with_repos, with_post, with_place, state_changes, ai_cases);
    printf("  %d post codes compared (the code word is in D0)\n", total_posts);
    rig_shutdown();
    return diffs ? 1 : 0;
}
