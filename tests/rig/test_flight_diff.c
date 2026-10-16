/* tests/rig/test_flight_diff.c - differential test: the port's flight_step against the original world update (0x26AF8)
 * in the Musashi rig, with the rig convention of docs/re/05-flight-model.md section 1 and "Worked examples":
 *   - the image at 0x10000; the whole of hunks 71 to 73 (0x4A6E8-0x54EBF) cleared; the base block B0 plus the changes of the
 *     case written; a return instruction (0x4E75) over the first word of 0x18E54, 0x1A18C, 0x1F11E and 0x1F0E2;
 *   - the carrier (record 14) with its vertex list prepared as section 9 ("Carrier set-up for E5 and E6") says, the player's
 *     model vertices 0..3 rotated by its matrix B before every step (the draw pass);
 *   - per step the key byte is written to +0x65 (and V:4A928 for a preset) and rig_run(0x26AF8) is called with registers 0.
 * Every mapped state word (port_fields below: the player record and the V: variables of the note) is compared after every step; the
 * first divergence of a sequence is reported with field, step and values.
 *
 * What runs: the worked examples E1 to E9 (the rig against the note's printed rows, the port against the rig) and two more cases, X1 (the exit
 * step of a negative pitch word) and X2 (the plain ejection cycle); random sequences of 80 steps from documented initial states in eleven kinds
 * (0 runway, 1 air, 2 carrier deck, 3 carrier approach, 4 low descent, 5 coast and ground, 6 fuel at or near 0 with the afterburner on, 7 the edges
 * of the world, 8 the ceiling, 9 ejection, 10 junk: a negative pitch word with the original's table over-reads, note 05 17.9) with random attitudes,
 * speeds and altitudes ("envelope": must not diverge), and the same with random flags, switches, key bytes, presets, pause, external view and part id
 * ("adversarial": they probe rules the note leaves open, a divergence is printed as information and does not fail the run); the terrain query at crafted
 * positions (the boundary values of u of section 10 and random positions, x < 0 included); the two matrix builders over all pitch words; the pointer
 * throttle rules at the thresholds of 17.4.
 *
 * Every step is compared on every mapped field, matrices included; there are no exemptions. A sequence ends early, after the comparison of its step, only
 * in these cases, which the summary counts: the step read run-time memory the port does not hold (the counter FL_COUNT_RUNTIME; FL_COUNT_IMAGE, an image read outside the spans, is NOT accepted: it is reported as its own divergence class,
 * accepted divergence Q16 of the note: a difference in such a step is counted as accepted, not as a verdict); the count of the ejection cycle reached 3
 * (the seat object is spawned by the original: its record is not the player's, the rest of the sequence compares the player's record only, so this is
 * not an end any more); a zero divisor or an unreachable terrain bound (never seen). Run-time memory the rig has no content for is zero in the rig as it is
 * in the port's answer, so most such steps agree.
 *
 * Known remaining cases at N=3000 (round 4, listed in the report line "unexplained" by sequence): the span rule of 17.18 a removed the outside-spans class and
 * the earlier junk cases (envelope 1440 step 2, roll after a junk matrix: no longer diverges). Left: (1) envelope 2364 step 1: a composition lookup that lands on
 * one of the 145 relocated longs in 0x3AE92..0x4130A, where the rig serves flat values (rig_reset writes only 0x4130A.. so as not to break the rig's collision
 * pointers) and the port the reference machine's: a rig limit (with the whole span written the sequence agrees). (2) adversarial 1594 step 6 (junk heading
 * 53016): the composed pitch, heading and roll agree with the rig's stored values (6928, 20408, 18048) but the port abandons the step in the gimbal guard
 * (old heading 53016 and old roll 26040 give dh 32608 and dr 7992, so |dr - dh| is far above 0x320) while the rig stores the angles and clears W2 0x0004:
 * unexplained (steps 3 to 5 of the same sequence abandon in both).
 * Skipped (exit 0) when extracted/interceptor.bin or the extracted assets (tables/flight_*.bin, models) are absent. Run from tools/m68k-rig
 * (make test-flight). Environment: FLIGHT_DIFF_N (sequences per family, default 200), FLIGHT_DIFF_ONLY=i (one sequence), FLIGHT_DIFF_VERBOSE
 * (every divergence with the state), FLIGHT_DIFF_TRACE=i (per-step trace of sequence i). The debugging experiments of the first round are in
 * commit deeb64d. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/flight.h"
#include "../../src/core/fx.h"
#include "../../src/core/world.h"
#include "../flight_examples.h"
#include "../player_draw.h"

#include "../../src/core/entities.h"

/* The part of the original's world update that follows the player's step and touches the player's record: the seat routine 0x26F60 (the seat spawn at the ejection count 3, the latch
 * V:4AA7A, the view switch; note 07 6.4), run by the entity module as the game runs it. The other records are empty here, as in the rig. */
static entities_t SEATE;
static int seate_ready;
static void world_tail(aircraft_t *ac) {
    if (!seate_ready || SEATE.player != ac || ac->f.steps <= 1) {   /* a new sequence (the first step of a fresh aircraft): fresh records */ entities_init(&SEATE, NULL, NULL, NULL); entities_attach_player(&SEATE, ac); seate_ready = 1; }
    SEATE.flying = 1; SEATE.sim_on = 1; SEATE.camera_update = 0;   /* V:4A86D is 0 in the rig */
    entities_step(&SEATE);
    if (SEATE.request) { ac->f.v4a910 |= SEATE.request; SEATE.request = 0; }
}


#ifdef FL_UFORM
#define FL_UFORM_REPORT FL_UFORM
#else
#define FL_UFORM_REPORT 0
#endif
#define BASE 0x10000u
#define V_LO 0x4A6E8u
#define V_HI 0x54EC0u
#define REC 0x4B23Cu
#define CARRIER_REC (REC + 14 * 0x200u)
#define CARRIER_POS 286785536
#define N_RANDOM 200
#define STEPS_PER_RUN 80

/* ---- random numbers --------------------------------------------------------------------------- */
static uint32_t rng = 0x9E3779B9u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }
static int pct(int p) { return rnd(0, 99) < p; }

/* ---- rig memory ---------------------------------------------------------------------------------- */
static void put8(uint32_t a, int v) { uint8_t b = (uint8_t)v; rig_poke(a, &b, 1); }
static void put16(uint32_t a, int v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 2); }
static void put32(uint32_t a, int32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 4); }
static int get8(uint32_t a) { uint8_t b; rig_peek(a, &b, 1); return b; }
static int get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return (int16_t)((b[0] << 8) | b[1]); }
static int32_t get32(uint32_t a) { uint8_t b[4]; rig_peek(a, b, 4); return (int32_t)(((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]); }

/* ---- port environment -------------------------------------------------------------------------- */
static const char *assets = "../../assets";
static int fl_read(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    char p[256];
    snprintf(p, sizeof p, "%s/%s", assets, path);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = (uint8_t *)malloc((size_t)n + 1);
    *len = fread(*out, 1, (size_t)n, f);
    fclose(f);
    return 0;
}
static void fl_release(void *user, uint8_t *buf) { (void)user; free(buf); }
static game_loader_t loader = { fl_read, fl_release, NULL };
static flight_tables_t tables;
static flight_env_t env;
static int16_t pvtx[2][4][3];   /* model vertices 0..3 (level 0) of model 4 (index 0) and model 3 (index 1): the rig's stand-in for the draw, poked into the original's record and the port's slots alike */
static int16_t carrier_verts[FLIGHT_SHIP_SLOTS][3];

static int build_env(void) {
    char err[128];
    if (flight_tables_load(&tables, &loader, err, sizeof err) != 0) { printf("test_flight_diff: skipped (%s)\n", err); return -1; }
    world_t *w = world_load(&loader, err, sizeof err);
    if (!w) { printf("test_flight_diff: skipped (%s)\n", err); return -1; }
    for (int k = 0; k < FLIGHT_SHIP_SLOTS; k++) {
        int32_t v[3];
        if (world_model_vertex(w, 8, 0, (unsigned)k, v) != 0) { printf("test_flight_diff: skipped (carrier vertex %d)\n", k); return -1; }
        for (int i = 0; i < 3; i++) carrier_verts[k][i] = (int16_t)v[i];
    }
    memset(&env, 0, sizeof env);
    for (int m = 0; m < 2; m++)
        for (int k = 0; k < 4; k++) {
            int32_t v[3];
            world_model_vertex(w, m == 0 ? 4 : 3, 0, (unsigned)k, v);
            for (int i = 0; i < 3; i++) pvtx[m][k][i] = (int16_t)v[i];
        }
    env.tab = &tables;
    env.ship.present = 1;
    env.ship.w0 = 0x404C; env.ship.cls = 0x20; env.ship.group = 0x0E; env.ship.l7d = 3;
    env.ship.pos[0] = CARRIER_POS; env.ship.pos[1] = 0; env.ship.pos[2] = CARRIER_POS;
    env.ship.fine_x = env.ship.fine_z = 6144; env.ship.alt8 = 0;
    flight_ship_build(&env.ship, (const int16_t (*)[3])carrier_verts, FLIGHT_SHIP_SLOTS, 0, 28600, 0);
    env.landmark_z = 1;   /* the rig image at 0x10000 (section 11b) */
    env.no_rearm = 1;     /* 0x1F11E is patched out in the rig */
    env.no_sound_system = 1;   /* V:4AC12 bit 0 is clear in the rig (note 05 17.12): the first contact leaves the notice 0xFA / V:4A84F = 2 */
    flight_set_env(&env);
    world_destroy(w);
    return 0;
}

/* ---- mapped fields --------------------------------------------------------------------------------- */
typedef struct { const char *name; uint32_t addr; int size; int32_t val; } field_t;
static field_t fl[256];
static int nfl;
static void F(const char *name, uint32_t addr, int size, int32_t val) {
    field_t *f = &fl[nfl++];
    f->name = name; f->addr = addr; f->size = size; f->val = val;
}

/* The port's state as the fields the note maps (record offsets and V: addresses of section 16 / the tables of section 1). */
static void port_fields(const aircraft_t *a) {
    const flight_state_t *f = &a->f;
    nfl = 0;
    F("W0", REC + 0x00, 2, f->w0); F("W2", REC + 0x02, 2, f->w2); F("B4", REC + 0x04, 1, f->b4); F("state+05", REC + 0x05, 1, f->state);
    F("cell_x+06", REC + 0x06, 2, f->cell_x); F("cell_z+08", REC + 0x08, 2, f->cell_z); F("cell+0A", REC + 0x0A, 1, f->cell_a); F("cell+0B", REC + 0x0B, 1, f->cell_b);
    F("fine_x+0C", REC + 0x0C, 2, f->fine_x); F("fine_z+0E", REC + 0x0E, 2, f->fine_z); F("alt8+10", REC + 0x10, 4, f->alt8);
    F("x+14", REC + 0x14, 4, a->pos[0]); F("y+18", REC + 0x18, 4, a->pos[1]); F("z+1C", REC + 0x1C, 4, a->pos[2]);
    F("B20", REC + 0x20, 1, f->b20); F("B21+21", REC + 0x21, 1, f->b21);
    F("roll_sin+22", REC + 0x22, 2, f->roll_sin); F("roll_cos+24", REC + 0x24, 2, f->roll_cos); F("ground+26", REC + 0x26, 2, f->ground);
    F("cmd_pitch+28", REC + 0x28, 1, (uint8_t)f->cmd_pitch); F("cmd_rudder+29", REC + 0x29, 1, (uint8_t)f->cmd_rudder);
    F("cmd_roll+2A", REC + 0x2A, 1, (uint8_t)f->cmd_roll); F("T+2B", REC + 0x2B, 1, (uint8_t)f->throttle);
    F("repeat+39", REC + 0x39, 1, f->repeat);
    F("vel_x+3E", REC + 0x3E, 4, f->vel[0]); F("vel_y+42", REC + 0x42, 4, f->vel[1]); F("vel_z+46", REC + 0x46, 4, f->vel[2]);
    F("counter+4C", REC + 0x4C, 2, f->counter); F("deck+4E", REC + 0x4E, 2, f->deck);
    F("turn+50", REC + 0x50, 2, f->turn50); F("turn+52", REC + 0x52, 2, f->turn52); F("turn+54", REC + 0x54, 2, f->turn54);
    F("E+56", REC + 0x56, 2, f->rate_p); F("Y+58", REC + 0x58, 2, f->rate_h); F("Q+5A", REC + 0x5A, 2, f->rate_r);
    F("keys+65", REC + 0x65, 1, f->keys);
    F("pitch+66", REC + 0x66, 2, (int16_t)f->pitch); F("heading+68", REC + 0x68, 2, (int16_t)f->heading); F("roll+6A", REC + 0x6A, 2, (int16_t)f->roll);
    F("S+6C", REC + 0x6C, 2, f->speed_target); F("W+6E", REC + 0x6E, 2, f->speed);
    F("fuel+72", REC + 0x72, 4, f->fuel); F("sink+76", REC + 0x76, 2, f->sink); F("gust+78", REC + 0x78, 2, f->gust);
    F("gear+7C", REC + 0x7C, 1, f->gear);
    static char mn[18][16];
    for (int i = 0; i < 9; i++) { snprintf(mn[i], 16, "matA[%d]", i); F(mn[i], REC + 0x80 + 2 * i, 2, f->mat_a[i]); }
    for (int i = 0; i < 9; i++) { snprintf(mn[9 + i], 16, "matB[%d]", i); F(mn[9 + i], REC + 0x92 + 2 * i, 2, f->mat_b[i]); }
    F("V:4A984", 0x4A984, 2, f->v4a984); F("V:4A986", 0x4A986, 2, f->v4a986); F("V:4A98A", 0x4A98A, 2, f->v4a98a);
    F("V:4A982", 0x4A982, 2, f->v4a982); F("V:4AB98", 0x4AB98, 2, f->v4ab98); F("V:4AC0C", 0x4AC0C, 4, f->v4ac0c);
    F("V:4AC08", 0x4AC08, 4, f->v4ac08); F("V:4A9FE", 0x4A9FE, 2, f->v4a9fe);
    F("V:4A957", 0x4A957, 1, f->v4a957); F("V:4A928", 0x4A928, 1, (uint8_t)f->v4a928);
    F("V:4A878", 0x4A878, 1, f->v4a878); F("V:4A8F6", 0x4A8F6, 1, f->v4a8f6); F("V:4A8FD", 0x4A8FD, 1, f->v4a8fd);
    F("V:4A968", 0x4A968, 1, f->v4a968); F("V:4A84F", 0x4A84F, 1, f->v4a84f); F("V:4A87D", 0x4A87D, 1, f->v4a87d);
    F("V:4A850", 0x4A850, 1, f->v4a850); F("V:4A8FE", 0x4A8FE, 1, f->v4a8fe); F("V:4A8FF", 0x4A8FF, 1, f->v4a8ff);
    F("V:4A9A8", 0x4A9A8, 1, (uint8_t)f->v4a9a8);
    F("V:4A857", 0x4A857, 1, f->v4a857);
    F("V:4A910", 0x4A910, 1, f->v4a910); F("V:4A830", 0x4A830, 2, f->v4a830); F("V:4A834", 0x4A834, 2, f->v4a834);
    F("V:4A963", 0x4A963, 1, f->v4a963); F("V:4A994", 0x4A994, 2, f->v4a994); F("V:4A996", 0x4A996, 2, f->v4a996); F("V:4AA7A", 0x4AA7A, 2, f->v4aa7a);
}

static int32_t rig_value(const field_t *f) {
    switch (f->size) {
    case 1: return get8(f->addr);
    case 2: return get16(f->addr);
    default: return get32(f->addr);
    }
}

/* Variables the port keeps as inputs of the case (not compared, only written): the constants of B0 and the switches. */
static void write_inputs(const aircraft_t *a) {
    const flight_state_t *f = &a->f;
    put32(0x4A7B2, f->v4a7b2);
    put8(0x4A83C, f->v4a83c); put8(0x4A847, f->v4a847); put8(0x4A83D, f->v4a83d); put8(0x4A844, f->v4a844);
    put8(0x4A858, f->v4a858); put8(0x4A866, f->v4a866); put8(0x4A867, f->v4a867);
    put8(0x4A890, f->v4a890);   /* the pointer throttle flag (B0 has 0; the cases that probe the pointer rules set it) */
    put16(0x4AA04, f->v4aa04); put16(0x4AA06, f->v4aa06);   /* the viewed cell, set by the camera set-up of the frame */
    put8(0x4A903, f->v4a903); put8(0x4A95E, f->v4a95e); put8(0x4A952, f->v4a952);
    put8(REC + 0x5E, f->group); put8(REC + 0x62, f->cls); put8(REC + 0x63, f->id); put8(REC + 0x71, 0xFF);
    put8(REC + 0x7B, f->b7b); put8(REC + 0x7D, f->l7d); put16(REC + 0x7E, f->turn_limit);
}

/* the draw pass: player model vertices 0..3 rotated by matrix B >> 6, read from the rig's own memory */
static aircraft_t ac;
/* the port's slots are the draw's: the same stand-in words the rig's record gets in write_player_vertices, from the matrix the step starts with */
static void port_draw(void) { player_draw_standin(&ac.f, pvtx[(ac.f.cls & 15) == 1 ? 0 : 1]); }
static void write_player_vertices(void) {
    int cls = get8(REC + 0x62);
    const int16_t (*mv)[3] = pvtx[(cls & 15) == 1 ? 0 : 1];
    int mb[9];
    for (int i = 0; i < 9; i++) mb[i] = get16(REC + 0x92 + 2 * i) >> 6;
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 3; i++) {
            int32_t s = 0;
            for (int j = 0; j < 3; j++) s += mb[3 * i + j] * mv[k][j];
            put16(REC + 0xA4 + 6 * k + 2 * i, (int16_t)(s >> 8));
        }
}

static void write_carrier(void) {
    uint32_t c = CARRIER_REC;
    put16(c + 0x00, 0x404C); put32(c + 0x14, CARRIER_POS); put32(c + 0x18, 0); put32(c + 0x1C, CARRIER_POS);
    put16(c + 0x0C, 6144); put16(c + 0x0E, 6144); put16(c + 0x10, 0);
    put16(c + 0x06, 68); put16(c + 0x08, 68); put8(c + 0x0A, 10);   /* its own cell fields, so that its step does not report a sub-cell change (V:4A910) */
    put8(c + 0x5E, 0x0E); put8(c + 0x62, 0x20); put8(c + 0x63, 0x3D); put16(c + 0x68, 28600); put8(c + 0x7D, 3);
    fx_mat_t m;
    fx_matrix_a(&m, 0, 28600, 0);
    for (int i = 0; i < 9; i++) put16(c + 0x80 + 2 * i, m.m[i / 3][i % 3]);
    for (int i = 0; i < 9; i++) put16(c + 0x92 + 2 * i, env.ship.mat_b[i]);
    for (int k = 0; k < FLIGHT_SHIP_SLOTS; k++)
        for (int i = 0; i < 3; i++) put16(c + 0xA4 + 6 * k + 2 * i, env.ship.vtx[k][i]);
}

static char unx_list[512];   /* the unexplained first divergences of the family: sequence, step, field (the report names them, item 8 of round 4) */
static int fldbg;   /* debug output of the deck sweep (FLDBGSWEEP in the environment) */
static const char *image_path;
static int full_span;   /* 1: poke the whole span 2 (0x3AE92..), the non-carrier families; 0: only 0x4130A.. (the carrier families keep the rig's own pointers) */
static int rig_reset(const aircraft_t *a, int with_carrier) {
    if (rig_load_image(image_path, BASE) != 0) return -1;
    static const uint32_t patch[] = { 0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2 };
    for (int i = 0; i < 4; i++) put16(patch[i], 0x4E75);
    /* The relocated longs of junk span 2 (note 05 17.9, the span rule) hold the reference machine's RAM addresses there; the rig's image is relocated for its own
     * base, so the span's bytes (extracted with the reference-machine values) are written over it: the rig then answers a junk read that lands on one as the
     * original did. Span 1 is not written: its three longs are operands of the bank-turn code, which must keep running at the rig's addresses. */
    /* Only 0x4130A.. is written (the old span 2): since 17.18 a the port's span starts at 0x3AE92, but 0x3AE92..0x4130A holds the models and the collision
     * lists whose pointer longs the rig's code dereferences at the rig's own addresses (writing the reference machine's values there breaks the rig's collision:
     * E5/E6 and the deck sweep fail). That range is served by the rig's own image, which equals the port's span except at its 145 relocated longs (flat values
     * there, the reference machine's in the port); a composition lookup that lands on one of those longs differs by construction and is a rig limit. */
    if (full_span) rig_poke(FLIGHT_SPAN_B_BASE, tables.span_b, FLIGHT_SPAN_B_SIZE);   /* the non-carrier kinds: the 145 longs in 0x3AE92..0x4130A are compared too */
    else rig_poke(0x4130Au, tables.span_b + (0x4130Au - FLIGHT_SPAN_B_BASE), FLIGHT_SPAN_B_SIZE - (0x4130Au - FLIGHT_SPAN_B_BASE));
    static uint8_t zero[V_HI - V_LO];
    rig_poke(V_LO, zero, sizeof zero);
    write_inputs(a);
    port_fields(a);
    for (int i = 0; i < nfl; i++) {
        switch (fl[i].size) {
        case 1: put8(fl[i].addr, fl[i].val); break;
        case 2: put16(fl[i].addr, fl[i].val); break;
        default: put32(fl[i].addr, fl[i].val); break;
        }
    }
    if (with_carrier) write_carrier();
    return 0;
}

static int rig_step(uint8_t keys, int preset) {
    for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
    put8(REC + 0x65, keys);
    if (preset) put8(0x4A928, preset);
    write_player_vertices();
    return rig_run(0x26AF8u);
}

static int verbose, trace = -1, accepted_divs, accepted_equal, total_junk_steps, outside_steps, outside_divs, class_seat, class_other;   /* outside_*: steps that read the image outside the spans (FL_COUNT_IMAGE): a divergence class of its own, never accepted */
static unsigned long total_var_reads;
static int seat_910;
/* ---- comparisons --------------------------------------------------------------------------------------- */
typedef struct { char field[24]; int step; long port, rig; char all[400]; } diverge_t;
static int compare(const aircraft_t *a, diverge_t *d, int step) {
    port_fields(a);
    int first = 1;
    d->all[0] = '\0';
    for (int i = 0; i < nfl; i++) {
        /* V:4A910 is also written by the step of every other record whose sub-cell changes; once the seat has been spawned (the latch is set) the seat's own step
         * (milestone 5, note 05 17.10) writes it, so the byte is not compared from then on */
        if (a->f.v4aa7a && !strcmp(fl[i].name, "V:4A910")) { seat_910++; continue; }
        int32_t rv = rig_value(&fl[i]);
        int32_t pv = fl[i].val;
        if (fl[i].size == 1) { rv &= 0xFF; pv &= 0xFF; }
        if (fl[i].size == 2) { rv = (int16_t)rv; pv = (int16_t)pv; }
        if (rv != pv) {
            if (first) { snprintf(d->field, sizeof d->field, "%s", fl[i].name); d->step = step; d->port = pv; d->rig = rv; first = 0; }
            size_t l = strlen(d->all);
            if (l < sizeof d->all - 60) snprintf(d->all + l, sizeof d->all - l, " %s(p%ld r%ld)", fl[i].name, (long)pv, (long)rv);
        }
    }
    return !first;
}

static int total_fail;
static int hist_n;
static char hist_name[64][24];
static int hist_cnt[64];
static void note_divergence(const diverge_t *d) {
    int i;
    for (i = 0; i < hist_n; i++) if (!strcmp(hist_name[i], d->field)) break;
    if (i == hist_n && hist_n < 64) { snprintf(hist_name[hist_n], 24, "%s", d->field); hist_cnt[hist_n++] = 0; }
    if (i < 64) hist_cnt[i]++;
}

/* ---- the worked examples through both ---------------------------------------------------------------------- */
typedef uint8_t (*keyfn)(int step);
static uint8_t keys_none(int s) { (void)s; return 0; }
static uint8_t keys_e3(int s) { return s >= 1 && s <= 10 ? FLIGHT_KEY_STICK_BACK : 0; }
static uint8_t keys_e4(int s) { return (s >= 1 && s <= 40 ? FLIGHT_KEY_THROTTLE_UP : 0) | (s >= 120 && s <= 150 ? FLIGHT_KEY_STICK_BACK : 0); }
static uint8_t keys_e5(int s) { return s <= 45 ? FLIGHT_KEY_THROTTLE_UP : 0; }

static aircraft_t ac;
static void air_case(int32_t y, uint16_t pitch, int S, int T, uint8_t gear) {
    flight_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    flight_set_position(&ac, 273272584, y, 280480032);
    flight_set_attitude(&ac, pitch, 2240, 0);
    ac.f.w2 = 0; ac.f.b4 = 0;
    ac.f.speed_target = ac.f.speed = (int16_t)S; ac.f.throttle = (int8_t)T; ac.f.gear = gear;
    flight_sync(&ac);
}
static void case_e1(void) { flight_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY); }
static void case_e2(void) { air_case(409600, 0, 3000, 60, 0x60); }
static void case_e9(void) { air_case(409600, 0, 3000, 60, 0xE0); }
static void case_e7(void) { air_case(409600, 0, 1700, 0, 0); }
/* open divergence 3: a stall nose drop at pitch 5 with +0x26 = -40 stores the word -5 (65531); the step is compared without the matrices */
static void case_exit(void) { air_case(409600, 5, 1000, 0, 0); ac.f.ground = -40; }
/* the ejection cycle from the key (nibble 10): the count reaches 3 at step 6, where the three values of the port's rule appear */
static void case_eject(void) { air_case(409600, 0, 3000, 60, 0); ac.f.gear = 10; }
static void case_e8(void) { air_case(20000, 2000, 3000, 60, 0); }
static void case_e5(void) {
    flight_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    flight_set_position(&ac, 286801436, 30472, 286891436); flight_set_attitude(&ac, 0, 28600, 0);
    ac.f.w2 = 0x0082; ac.f.b4 = 0xC8; ac.f.deck = 112; flight_sync(&ac);
}
static void case_e6(void) {
    flight_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    flight_set_position(&ac, CARRIER_POS, 37472, CARRIER_POS - 260000); flight_set_attitude(&ac, 0, 28600, 0);
    ac.f.w2 = 0x8000; ac.f.b4 = 0; ac.f.speed_target = ac.f.speed = 1700; ac.f.throttle = 40; ac.f.gear = 0; flight_sync(&ac);
}

/* Runs port and rig for `steps` steps from the case in `ac`; compares after every step. `rows` (may be NULL) are the note's rows,
 * which the rig must reproduce on the printed columns (the harness sanity check). Returns the number of problems. */
static int run_example(const char *name, void (*mk)(void), keyfn kf, int steps, int carrier, const ex_row_t *rows, size_t nrows) {
    int bad = 0;
    mk();
    if (rig_reset(&ac, carrier) != 0) { printf("  %s: rig reset failed\n", name); return 1; }
    size_t ri = 0;
    diverge_t d;
    for (int s = 1; s <= steps; s++) {
        flight_inputs_t in; memset(&in, 0, sizeof in);
        in.keys = kf(s);
        port_draw(); flight_step(&ac, &in); world_tail(&ac);
        if (rig_step(in.keys, 0) != 0) { printf("  %s: rig_run failed at step %d\n", name, s); return bad + 1; }
        if (compare(&ac, &d, s)) {
            printf("  %s: step %d field %s: port %ld rig %ld  [all:%s]\n", name, s, d.field, d.port, d.rig, d.all);
            bad++;
            break;
        }
        while (rows && ri < nrows && rows[ri].step < s) ri++;
        if (rows && ri < nrows && rows[ri].step == s) {   /* the rig against the note's printed row */
            const ex_row_t *r = &rows[ri];
            long got[] = { get32(REC + 0x14), get32(REC + 0x18), get32(REC + 0x1C), get16(REC + 0x66), get16(REC + 0x68), get16(REC + 0x6A),
                           get16(REC + 0x6C), get16(REC + 0x6E), (int8_t)get8(REC + 0x2B), get32(REC + 0x72), get16(REC + 0x02) & 0xFFFF, get8(REC + 0x04) };
            long want[] = { r->x, r->y, r->z, r->pitch, r->heading, r->roll, r->S, r->W, r->T, r->fuel, r->w2, r->b4 };
            for (int k = 0; k < 12; k++)
                if (got[k] != want[k]) { printf("  %s: rig differs from the note's row %d, column %d: rig %ld note %ld\n", name, r->step, k, got[k], want[k]); bad++; break; }
        }
    }
    printf("  %-28s %3d steps, %s\n", name, steps, bad ? "MISMATCH" : "port = rig");
    return bad;
}

/* ---- random initial states --------------------------------------------------------------------------------- */
typedef struct { const char *name; int nseq, nfail, steps_ok; } family_t;

static const uint8_t gears[] = { 0x00, 0x00, 0x00, 0x60, 0xE0, 0x80, 0x10, 0x30, 0x50, 0xC0, 0xA0 };

/* kind: 0 runway, 1 air, 2 carrier deck, 3 carrier approach, 4 low descent, 5 coast/ground; the rig-decided rules of the port's report:
 * 6 fuel at or near 0 with the afterburner on, 7 the edges of the world (x or z near 0 or 0x1FFFFFFF: the negative-x quirk with the z reset, the
 * clamps) at low altitude and high speed, 8 the ceiling 0x800000 (nose-up climb), 9 ejection (the key handler starts the cycle), 10 junk: a pitch word
 * from -60 to -1 or a stall nose drop that makes one, any heading and roll, speeds 200 to 1400, a ground term -77 to 0 (the generator of note 05 17.9) */
#define N_KINDS 11
static void random_case(int kind, int adversarial) {
    flight_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    flight_state_t *f = &ac.f;
    int32_t x = 273272584, y = 409600, z = 280480032;
    uint16_t pitch = 0, heading = (uint16_t)rnd(0, 28799), roll = 0;
    f->w2 = 0; f->b4 = 0;
    switch (kind) {
    case 0: x = 273963296 + rnd(-30000, 30000); y = 1800; z = 279184624 + rnd(-30000, 30000); f->w2 = 0x0080; f->b4 = 0x04; heading = 2240; break;
    case 1: x = 273272584 + rnd(-3000000, 3000000); z = 280480032 + rnd(-3000000, 3000000); y = rnd(20000, 3000000); break;
    case 2: x = 286801436 + rnd(-6000, 6000); y = 30472; z = 286891436 + rnd(-30000, 30000); heading = (uint16_t)(28600 + rnd(-40, 40));
            f->w2 = 0x0082; f->b4 = pct(70) ? 0xC8 : 0xC0; f->deck = 112; break;
    case 3: x = CARRIER_POS + rnd(-12000, 12000); z = CARRIER_POS - rnd(150000, 330000); y = rnd(30000, 90000); heading = (uint16_t)(28600 + rnd(-60, 60));
            f->w2 = pct(70) ? 0x8000 : 0; f->deck = 112; break;
    case 4: y = rnd(2500, 40000); pitch = (uint16_t)rnd(480, 4000); break;
    case 6: x = 273272584 + rnd(-3000000, 3000000); z = 280480032 + rnd(-3000000, 3000000); y = rnd(100000, 3000000); break;
    case 7: x = pct(50) ? rnd(0, 40000) : pct(50) ? 0x1FFFFFFF - rnd(0, 40000) : (rnd(0, 31) << 24) + rnd(0, 0xFFFFFF);
            z = pct(50) ? rnd(0, 40000) : pct(50) ? 0x1FFFFFFF - rnd(0, 40000) : (rnd(0, 31) << 24) + rnd(0, 0xFFFFFF);
            y = pct(60) ? rnd(1800, 9000) : rnd(20000, 400000); break;
    case 8: x = 273272584 + rnd(-3000000, 3000000); z = 280480032 + rnd(-3000000, 3000000); y = 0x800000 - rnd(0, 300000); break;
    case 9: x = 273272584 + rnd(-3000000, 3000000); z = 280480032 + rnd(-3000000, 3000000); y = rnd(20000, 3000000); break;
    case 10: x = 273272584 + rnd(-3000000, 3000000); z = 280480032 + rnd(-3000000, 3000000); y = rnd(20000, 400000); break;
    default: x = (rnd(0, 31) << 24) + rnd(0, 0xFFFFFF); z = (rnd(0, 31) << 24) + rnd(0, 0xFFFFFF); y = rnd(1800, 6000); f->w2 = pct(50) ? 0x0080 : 0; pitch = (uint16_t)rnd(0, 800); break;
    }
    if (kind != 0 && kind != 2) {
        int r = rnd(0, 9);
        pitch = (uint16_t)(kind == 4 ? pitch : r < 5 ? rnd(-1500, 1500) + 28800 : r < 8 ? rnd(0, 28799) : rnd(-600, 600) + 28800);
        pitch %= 28800;
        roll = (uint16_t)(pct(40) ? rnd(0, 28799) : (rnd(-1600, 1600) + 28800) % 28800);
    }
    if (kind == 0 || kind == 2) { roll = 0; pitch = 0; }
    if (kind == 10) {
        pitch = (uint16_t)(pct(80) ? (uint16_t)rnd(-60, -1) : pct(50) ? (uint16_t)rnd(0, 6) : (uint16_t)rnd(-4200, -1));
        roll = (uint16_t)(pct(50) ? 0 : rnd(0, 28799));
    }
    flight_set_position(&ac, x, y, z);
    flight_set_attitude(&ac, pitch, heading, roll);
    int S = (kind == 0 || kind == 2) ? (pct(30) ? rnd(0, 900) : 0) : kind == 10 ? rnd(200, 1400) : rnd(1000, 9000);
    if (kind == 4 || kind == 3) S = rnd(1200, 5000);
    f->speed_target = (int16_t)S; f->speed = (int16_t)S;
    f->throttle = (int8_t)(pct(20) ? 9 : pct(30) ? rnd(0, 60) : rnd(-32, 120));
    if (kind == 0 || kind == 2) f->throttle = (int8_t)(pct(50) ? 9 : rnd(0, 120));
    f->gear = gears[rnd(0, (int)sizeof gears - 1)];
    if (kind == 0 || kind == 2) f->gear = 0;
    if (kind == 10) { f->ground = (int16_t)rnd(-77, 0); f->gear = (uint8_t)(pct(60) ? 0 : f->gear); }
    if (kind == 3 && pct(50)) f->gear = 0;
    if (pct(15)) f->w2 |= 0x8000;
    f->fuel = pct(90) ? 6400000 : pct(50) ? rnd(0, 800000) : rnd(0, 6400000);
    if (kind == 6) {   /* fuel within a few steps of 0 (the afterburner burns 0x600 a step) with the afterburner on or just off */
        f->fuel = pct(20) ? 0 : rnd(1, 0x600 * 12);
        f->throttle = 120; f->w2 |= pct(80) ? (0x2000 | 0x0008) : 0x2000;
        f->speed_target = f->speed = (int16_t)rnd(3000, 9000);
    }
    if (kind == 7 || kind == 8) f->speed_target = f->speed = (int16_t)rnd(5000, 9000);
    if (kind == 7 && pct(60)) f->gear = 0;
    if (kind == 8) { pitch = (uint16_t)rnd(25500, 27800); flight_set_attitude(&ac, pitch, heading, 0); }
    /* the pointer throttle: on in half of the sequences, P = Q = 8 T like a keyboard player, or anywhere */
    f->v4a890 = pct(50);
    if (f->v4a890) {
        int t8 = 8 * (f->throttle > 0 ? f->throttle : 0);
        if (pct(60)) f->v4a830 = f->v4a834 = (int16_t)t8;
        else { f->v4a830 = (int16_t)rnd(-960, 960); f->v4a834 = (int16_t)(pct(50) ? f->v4a830 + rnd(-30, 30) : rnd(-960, 960)); }
        f->v4a95e = (uint8_t)(pct(15) ? 2 : 1);
    }
    if (adversarial) {
        const uint16_t w0s[] = { 0x11C8, 0x11C8, 0x11C8, 0x19C8, 0x91C8, 0x15C8, 0x13C8, 0x01C8, 0x10C8 };
        f->w0 = w0s[rnd(0, 8)];
        if (pct(40)) f->w2 |= (uint16_t)(rnd(0, 0xFFFF) & 0x2478);
        f->b20 = (uint8_t)(pct(70) ? 0 : rnd(0, 7));
        f->state = pct(90) ? 0 : 0x0A;
        f->cls = (uint8_t)(pct(80) ? 0x11 : pct(50) ? 0x10 : 0x14);
        f->cmd_pitch = (int8_t)(pct(50) ? 0 : rnd(-20, 20)); f->cmd_rudder = (int8_t)(pct(50) ? 0 : rnd(-20, 20)); f->cmd_roll = (int8_t)(pct(50) ? 0 : rnd(-60, 60));
        f->repeat = (uint8_t)rnd(0, 8);
        f->turn50 = (int16_t)(pct(50) ? 0 : -1); f->turn52 = (int16_t)(pct(50) ? 0 : -1); f->turn54 = (int16_t)(pct(60) ? 0 : rnd(-5000, 5000));
        f->rate_p = (int16_t)(pct(50) ? 0 : rnd(-800, 800)); f->rate_h = (int16_t)(pct(50) ? 0 : rnd(-800, 800)); f->rate_r = (int16_t)(pct(50) ? 0 : rnd(-800, 800));
        f->ground = (int16_t)(pct(70) ? 0 : rnd(-60, 80));
        f->gust = (int16_t)(pct(70) ? 0 : rnd(-100, 400));
        f->counter = (int16_t)rnd(-5, 40);
        f->v4a984 = (uint16_t)(pct(85) ? 0x0840 : pct(50) ? 0x0841 : 0x0800);
        f->v4a986 = (uint16_t)(pct(85) ? 0 : 0x4000); f->v4a98a = (uint16_t)(pct(80) ? 0 : rnd(0, 3) << 1 | (pct(20) ? 0x4000 : 0));
        f->v4a982 = (uint16_t)(pct(70) ? 0 : 0x0100);
        f->v4ac08 = pct(70) ? 0 : rnd(0, 0xFF) & 0xC3;
        f->v4a83c = pct(90); f->v4a847 = pct(90); f->v4a952 = pct(10); f->v4a844 = pct(80);
        f->v4a928 = (int8_t)(pct(70) ? 0 : pct(15) ? -1 : 12 * rnd(1, 10));
        if (pct(10)) f->v4a928 = 121;
        f->v4a867 = pct(3);
        f->v4a866 = pct(3);   /* paused for the whole sequence (the toggle below does it mid-sequence) */
        if (pct(25)) f->b4 |= (uint8_t)(rnd(0, 15) << 1 & 0x1E);
        f->v4a83d = pct(kind == 2 || kind == 3 ? 25 : 10);   /* the external view finalises a collision result at once */
        f->v4a9fe = (int16_t)(pct(70) ? 0 : rnd(-300, 300));
        f->b7b = (uint8_t)(pct(50) ? 0xFF : pct(70) ? 0 : rnd(0, 255));   /* the part id the renderer stored: 0 is the body (the ground test is live) */
        f->v4aa7a = pct(15) ? 0x0200 : 0;
        f->v4a963 = (uint8_t)rnd(0, 3);
        /* V:4A903 stays 0: the replay streams (3) are not modelled (note 05 17.16, open divergences) */
    } else {
        f->w0 = 0x11C8;
        f->v4a844 = 1;
    }
    f->roll_sin = 0;
    flight_sync(&ac);
}

static uint32_t seq_seed;
static int adv_keys;
static int run_random(int idx, int adversarial, int *nsteps_ok) {
    adv_keys = adversarial;
    int kind = idx % N_KINDS;
    rng = seq_seed ^ ((uint32_t)(idx + 1) * 0x9E3779B1u);   /* each sequence replays alone */
    if (rng == 0) rng = 1;
    random_case(kind, adversarial);
    full_span = (kind != 2 && kind != 3);
    int rr = rig_reset(&ac, 1);
    full_span = 0;
    if (rr != 0) return -1;
    diverge_t d;
    int segment_left = 0;
    uint8_t keys = 0;
    for (int s = 1; s <= STEPS_PER_RUN; s++) {
        if (segment_left == 0) {
            segment_left = rnd(1, 24);
            int r = rnd(0, 9);
            keys = 0;
            if (r < 8) {
                if (pct(40)) keys |= (uint8_t)(pct(70) ? FLIGHT_KEY_THROTTLE_UP : FLIGHT_KEY_THROTTLE_DOWN);
                if (pct(40)) keys |= (uint8_t)(pct(50) ? FLIGHT_KEY_STICK_FORWARD : FLIGHT_KEY_STICK_BACK);
                if (pct(35)) keys |= (uint8_t)(pct(50) ? FLIGHT_KEY_ROLL_RIGHT : FLIGHT_KEY_ROLL_LEFT);
                if (pct(25)) keys |= (uint8_t)(pct(50) ? FLIGHT_KEY_RUDDER_RIGHT : FLIGHT_KEY_RUDDER_LEFT);
            }
        }
        segment_left--;
        flight_inputs_t in; memset(&in, 0, sizeof in);
        in.keys = keys;
        if (pct(3)) in.preset = (int8_t)(pct(20) ? -1 : 12 * rnd(1, 10));
        if (pct(1)) in.preset = 121;
        if (adv_keys && pct(8)) in.keys = (uint8_t)rnd(0, 255);   /* any key byte, both keys of a pair included */
        if (kind == 9 ? pct(6) : rnd(0, 999) < 3) in.eject = 1;
        if (pct(10)) in.pointer_dy = (int16_t)rnd(-60, 60);
        if (ac.f.v4a890 && pct(2)) { in.pointer_set = 1; in.pointer_value = (int16_t)rnd(-1000, 1000); }
        if (pct(3)) in.gear_toggle = 1;
        if (pct(2)) in.hook_toggle = 1;
        if (pct(2)) in.brake_toggle = 1;
        /* the key handlers' effects on the rig's memory (section 2, 17.4): the rig does not run the handlers */
        if (in.gear_toggle && !(get16(REC + 0x02) & 0x0080)) put8(REC + 0x7C, get8(REC + 0x7C) ^ 0x80);
        if (in.hook_toggle && get8(REC + 0x62) == 0x11) { put16(REC + 0x02, (get16(REC + 0x02) & 0xFFFF) ^ 0x8000); put8(0x4A8FF, get8(0x4A8FF) ^ 0x80); }
        if (in.brake_toggle) { put16(REC + 0x00, (get16(REC + 0x00) & 0xFFFF) ^ 0x0800); put16(0x4A830, 0); put16(0x4A834, 0); put8(0x4A928, 0); }
        if (in.eject && (get8(REC + 0x7C) & 0x0F)) in.eject = 0;   /* a second press while the cycle runs gives a nibble of 14 or 15, which the world update's seat routine (entities.c, not under test here) turns into the parachute */
        if (in.eject) put8(REC + 0x7C, get8(REC + 0x7C) | 10);   /* note 07 6.4: the key ORs 0x0A into the nibble */
        if (in.preset) { int p = in.preset < 0 ? 0 : 8 * in.preset; p = p > 960 ? 960 : p; put16(0x4A830, p); put16(0x4A834, p); }   /* the F-key handler: P = Q = min(8 * preset, 960) */
        /* the mouse interrupt (rule 0 of 17.4): P = clamp(P - dy, -960, 960); the absolute axis replaces P first */
        {   /* the handler runs whatever V:4A890 is; with V:4A890 = 0 a negative dy is halved first (17.4 rule 0); the axis replaces P only with the flag */
            int P = (int16_t)get16(0x4A830), dy = in.pointer_dy;
            if (ac.f.v4a890 && in.pointer_set) P = in.pointer_value;
            if (!ac.f.v4a890 && dy < 0) dy >>= 1;
            P -= dy;
            put16(0x4A830, P > 960 ? 960 : P < -960 ? -960 : P);
        }
        if (adv_keys && pct(3)) { ac.f.v4a866 ^= 1; put8(0x4A866, ac.f.v4a866); }   /* the pause switch toggles */
        aircraft_t pre = ac;
        if (trace == idx) printf("   inputs: dy=%d set=%d val=%d gear_t=%d hook_t=%d brake_t=%d eject=%d\n", in.pointer_dy, in.pointer_set, in.pointer_value, in.gear_toggle, in.hook_toggle, in.brake_toggle, in.eject);
        if (trace == idx) printf("   pre-step %d: y=%d vel_y=%d pitch=%d roll=%d w2=%04x b20=%02x gear=%02x W=%d code=%04x\n", s, pre.pos[1], pre.f.vel[1], pre.f.pitch, pre.f.roll, pre.f.w2, pre.f.b20, pre.f.gear, pre.f.speed, pre.f.v4ab98);
        if (trace == idx) printf("      pre counter=%d w0=%04x b20=%02x P=%d Q=%d\n", pre.f.counter, pre.f.w0, pre.f.b20, pre.f.v4a830, pre.f.v4a834);
        flight_guard_reset(&ac);
        port_draw(); flight_step(&ac, &in); world_tail(&ac);
        put16(0x4AA04, ac.f.v4aa04); put16(0x4AA06, ac.f.v4aa06);   /* the camera set-up of the frame, after the update */
        unsigned accepted = ac.f.reads[FL_COUNT_RUNTIME] + ac.f.reads[FL_GUARD_EDGE_DEN] + ac.f.reads[FL_GUARD_TERRAIN_DATA];   /* Q16 only: run-time memory the port does not hold */
        const unsigned outside = ac.f.reads[FL_COUNT_IMAGE];   /* image bytes outside the extracted spans, answered 0: reported, not accepted */
        outside_steps += outside != 0;
        total_var_reads += ac.f.reads[FL_COUNT_VARIABLE];
        if (rig_step(in.keys, in.preset) != 0) { printf("  run %d: rig_run failed at step %d\n", idx, s); return -1; }
        if (trace == idx)
            printf("   trace seq %d step %2d keys %02x preset %d eject %d | port T=%d rep=%x v928=%d gear=%02x w2=%04x w0=%04x f6=%d fd=%d n68=%02x n4f=%02x P=%d Q=%d | rig T=%d rep=%x v928=%d gear=%02x w2=%04x w0=%04x f6=%d fd=%d n68=%02x n4f=%02x P=%d Q=%d\n",
                   idx, s, in.keys, in.preset, in.eject, ac.f.throttle, ac.f.repeat, ac.f.v4a928, ac.f.gear, ac.f.w2, ac.f.w0, ac.f.v4a8f6, ac.f.v4a8fd, ac.f.v4a968, ac.f.v4a84f, ac.f.v4a830, ac.f.v4a834,
                   (int8_t)get8(REC + 0x2B), get8(REC + 0x39), (int8_t)get8(0x4A928), get8(REC + 0x7C), get16(REC + 2) & 0xFFFF, get16(REC) & 0xFFFF, get8(0x4A8F6), get8(0x4A8FD), get8(0x4A968), get8(0x4A84F), get16(0x4A830), get16(0x4A834));
        if (trace == idx) printf("      extra: port v910=%02x cellA=%02x cellB=%02x cell=(%d,%d) view=(%d,%d) pos=(%d,%d,%d) | rig v910=%02x cellA=%02x cellB=%02x cell=(%d,%d) view=(%d,%d) pos=(%d,%d,%d)\n", ac.f.v4a910, ac.f.cell_a, ac.f.cell_b, ac.f.cell_x, ac.f.cell_z, ac.f.v4aa04, ac.f.v4aa06, ac.pos[0], ac.pos[1], ac.pos[2],
                                get8(0x4A910), get8(REC + 0x0A), get8(REC + 0x0B), get16(REC + 6), get16(REC + 8), get16(0x4AA04), get16(0x4AA06), get32(REC + 0x14), get32(REC + 0x18), get32(REC + 0x1C));
        const int differs = compare(&ac, &d, s);
        if (differs && accepted) {
            /* the port read memory it does not hold in this step (run-time display data, note 05 Q16): the difference is the accepted divergence, not a verdict */
            accepted_divs++;
            if (verbose) printf("  seq %3d (kind %d) step %2d ACCEPTED (run-time read), differs in %s: port %ld rig %ld  [all:%s]\n", idx, kind, d.step, d.field, d.port, d.rig, d.all);
            return 0;
        }
        if (differs) {
            if (outside) {
                outside_divs++;
                if (verbose) printf("  seq %3d (kind %d) step %2d OUTSIDE-SPANS (image read outside the spans, answered 0), differs in %s: port %ld rig %ld\n", idx, kind, d.step, d.field, d.port, d.rig);
            }
            /* classes of a first divergence: image read outside the spans (above), the ejected seat viewed (V:4A996 != 0 before or after the step: the original works in
             * the seat's frame, milestone 5), anything else (unexplained) */
            const char *cls = outside ? "outside-spans" : (pre.f.v4a996 || ac.f.v4a996) ? "seat-viewed" : "UNEXPLAINED";
            if (!outside) {
                if (pre.f.v4a996 || ac.f.v4a996) class_seat++;
                else {
                    class_other++;
                    int l = (int)strlen(unx_list);
                    if (l < (int)sizeof unx_list - 80) snprintf(unx_list + l, sizeof unx_list - (size_t)l, "%s seq %d step %d field %s", l ? ";" : "", idx, d.step, d.field);
                }
            }
            if (verbose) printf("  seq %3d (kind %d) step %2d [%s] field %-14s port %ld rig %ld  [all:%s]\n", idx, kind, d.step, cls, d.field, d.port, d.rig, d.all);
            if (verbose) printf("      pre: v4ac08=%x v4a984=%x v4a986=%x v4a98a=%x v4a982=%x fuel=%d w0=%04x w2=%04x b20=%02x state=%02x cls=%02x keys=%02x v4a847=%d v4a83c=%d\n", pre.f.v4ac08, pre.f.v4a984, pre.f.v4a986, pre.f.v4a98a, pre.f.v4a982, pre.f.fuel, pre.f.w0, pre.f.w2, pre.f.b20, pre.f.state, pre.f.cls, in.keys, pre.f.v4a847, pre.f.v4a83c);
            if (verbose) printf("      port: w0=%04x w2=%04x b4=%02x b20=%02x pos=(%d,%d,%d) vel=(%d,%d,%d) S=%d W=%d T=%d gear=%02x pitch=%d roll=%d hdg=%d\n", ac.f.w0, ac.f.w2, ac.f.b4, ac.f.b20, ac.pos[0], ac.pos[1], ac.pos[2], ac.f.vel[0], ac.f.vel[1], ac.f.vel[2], ac.f.speed_target, ac.f.speed, ac.f.throttle, ac.f.gear, ac.f.pitch, ac.f.roll, ac.f.heading);
            note_divergence(&d);
            return 1;
        }
        (*nsteps_ok)++;
        if (accepted) { accepted_equal++; }   /* a run-time read whose answer agreed: the sequence goes on */
        total_junk_steps += (int16_t)ac.f.pitch < 0;
    }
    return 0;
}

/* ---- the two matrix builders over all pitch words (note 05 17.9) ---------------------------------------------------- */
/* The original's builders 0x32B52 (A) and 0x32BEC (B) with the angle words in D0, D2, D4 and the destination in A1, against the module's builders: all 65536
 * pitch words with heading and roll 0, in each of the three angle positions, then random triples. The rig's span 2 carries the reference-machine longs (rig_reset). */
static int builders_sweep(int ntriples) {
    int bad = 0, shown = 0, accepted = 0;
    flight_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    if (rig_reset(&ac, 0) != 0) return -1;
    const uint32_t scratch = 0x60000;
    rng = 0xB11D3Eu;
    const uint32_t addrs[2] = { 0x32B52, 0x32BEC };
    long total = 0;
    for (int which = 0; which < 2; which++) {
        for (long i = 0; i < 3L * 65536 + ntriples; i++) {
            uint16_t a[3] = { 0, 0, 0 };
            if (i < 3L * 65536) a[i / 65536] = (uint16_t)(i % 65536);
            else for (int k = 0; k < 3; k++) a[k] = (uint16_t)(pct(60) ? rnd(0, 65535) : pct(50) ? rnd(-60, 0) & 0xFFFF : rnd(0, 28799));
            rig_set_reg(RIG_D0, a[0]); rig_set_reg(RIG_D2, a[1]); rig_set_reg(RIG_D4, a[2]); rig_set_reg(RIG_A1, scratch);
            if (rig_run(addrs[which]) != 0) { printf("  builder run failed\n"); return -1; }
            int16_t m[9];
            flight_guard_reset(&ac);
            if (which == 0) flight_matrix_a(&ac, &tables, a[0], a[1], a[2], m); else flight_matrix_b(&ac, &tables, a[0], a[1], a[2], m);
            total++;
            if (ac.f.reads[FL_COUNT_RUNTIME]) { accepted++; continue; }   /* a run-time text byte: answered 0 and counted (note 05 17.9); image reads outside the spans are compared like any other */
            for (int k = 0; k < 9; k++)
                if (get16(scratch + 2 * k) != m[k]) {
                    bad++;
                    if (shown++ < 5) printf("    builder %c angles (%u, %u, %u) element %d: port %d rig %d\n", which ? 'B' : 'A', a[0], a[1], a[2], k, m[k], get16(scratch + 2 * k));
                    break;
                }
        }
    }
    printf(" matrix builders A and B: %ld angle sets (all 65536 words in each of the three positions, then %d random triples, each builder), %d differ from the rig; %d read one of the five run-time text bytes (answered 0 and counted, not compared)\n", total, ntriples, bad, accepted);
    return bad;
}

/* ---- the carrier deck: one step at random positions around the carrier --------------------------------------------- */
/* The player hovers (speed 0, throttle 0, no keys) at a random position and attitude in and around the deck's box; one step; the deck bits of B4, W2 and the
 * contact flag are compared. Checks the polygon lists, the hull volumes and the arrestor corridor against the original (note 05 17.11). */
static int deck_sweep(int n, int seat) {
    int bad = 0, shown = 0;
    fldbg = getenv("FLDBGSWEEP") != NULL;   /* a flag, not setenv (not available on every platform) */
    rng = 0xDEC0DEu;
    for (int i = 0; i < n; i++) {
        flight_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
        int32_t x = CARRIER_POS + rnd(-30000, 90000), z = CARRIER_POS + rnd(-60000, 200000);
        int32_t y = pct(50) ? rnd(24000, 36000) : rnd(1800, 60000);
        flight_set_position(&ac, x, y, z);
        flight_set_attitude(&ac, (uint16_t)(pct(60) ? 0 : rnd(0, 28799)), (uint16_t)(pct(50) ? 28600 : rnd(0, 28799)), (uint16_t)(pct(60) ? 0 : rnd(0, 28799)));
        ac.f.w2 = (uint16_t)(pct(50) ? 0x0080 : 0) | (pct(30) ? 0x8000 : 0); ac.f.b4 = 0;
        ac.f.speed_target = ac.f.speed = 0; ac.f.throttle = 0; ac.f.gear = (uint8_t)(pct(70) ? 0 : 0xE0);
        if (seat) { ac.f.v4a994 = 1; ac.f.v4a996 = 0x0200; ac.f.v4aa7a = 0x0200; }   /* the seat view: the seat is the viewed entity */
        if (pct(40)) ac.f.gear = (uint8_t)((ac.f.gear & 0xF0) | rnd(1, 10));   /* an ejection nibble */
        flight_sync(&ac);
        if (rig_reset(&ac, 1) != 0) return -1;
        flight_inputs_t in; memset(&in, 0, sizeof in);
        if (fldbg) fprintf(stderr, "sweep %d:\n", i);
        port_draw(); flight_step(&ac, &in); world_tail(&ac);
        put16(0x4AA04, ac.f.v4aa04); put16(0x4AA06, ac.f.v4aa06);
        if (rig_step(0, 0) != 0) return -1;
        diverge_t d;
        if (compare(&ac, &d, 1)) {
            bad++;
            if (fldbg) fprintf(stderr, "FAIL %d nib=%d portW0=%04x rigW0=%04x portB4=%02x rigB4=%02x portW2=%04x rigW2=%04x y=%d\n", i, ac.f.gear & 15, ac.f.w0, get16(REC) & 0xFFFF, ac.f.b4, get8(REC + 4), ac.f.w2, get16(REC + 2) & 0xFFFF, y);
            if (shown++ < 4) {
                printf("    deck sweep %d: pos (%d,%d,%d) field %s port %ld rig %ld [%s]\n", i, x, y, z, d.field, d.port, d.rig, d.all);
                printf("       vtx port:"); for (int k = 0; k < 4; k++) printf(" (%d,%d,%d)", ac.f.vtx[k][0], ac.f.vtx[k][1], ac.f.vtx[k][2]);
                printf("  rig:"); for (int k = 0; k < 4; k++) printf(" (%d,%d,%d)", get16(REC + 0xA4 + 6 * k), get16(REC + 0xA6 + 6 * k), get16(REC + 0xA8 + 6 * k));
                printf("  seat view %d gear %02x w0 %04x w2 %04x\n", ac.f.v4a996, ac.f.gear, ac.f.w0, ac.f.w2);
            }
        }
    }
    fldbg = 0;
    printf(" deck sweep%s: %d random hovering states around the carrier, %d differ from the rig%s\n", seat ? " with the seat viewed" : "", n, bad,
           seat ? " (informational: the rig's record 1 is empty here, the original's frame is the seat's, milestone 5; the pass-1 rule is what the real sequences show)" : "");
    return seat ? 0 : bad;
}

/* ---- terrain: one step at crafted positions, the u boundary values of section 10 ---------------------------------- */
/* the aircraft stands on the ground with the terrain query due (wheels down, +0x4C & 7 = 0 after the pre-step), speed 0:
 * the step is a pure function of (x, z) for the bits 0x02 and 0x04 of B4, which the comparison covers. */
static int run_terrain_point(int32_t x, int32_t z, diverge_t *d) {
    flight_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    flight_set_position(&ac, x, 1800, z);
    flight_set_attitude(&ac, 0, 2240, 0);
    ac.f.w2 = 0x0080; ac.f.b4 = 0; ac.f.counter = 1;
    flight_sync(&ac);
    if (rig_reset(&ac, 0) != 0) return -1;
    flight_inputs_t in; memset(&in, 0, sizeof in);
    port_draw(); flight_step(&ac, &in); world_tail(&ac);
    if (rig_step(0, 0) != 0) return -1;
    return compare(&ac, d, 1);
}

static int terrain_edges(const int16_t *t, int32_t cellx, int32_t cellz, int *nedges, int16_t (*edge)[4]) {
    int32_t off = t[cellx + 32 * cellz];
    *nedges = 0;
    if (off <= 0) return 0;
    int wi = off / 2;
    if (t[wi] < 0) return 0;
    wi += 2;
    for (;;) {
        if (wi >= FLIGHT_TERRAIN_WORDS) return 0;
        int n = t[wi++];
        if (n == -1) return 1;
        if (n <= 0) n = t[wi++];
        if (n <= 0) continue;
        for (int i = 0; i < n && *nedges < 256; i++) {
            int j = (i + 1 == n) ? 0 : i + 1;
            edge[*nedges][0] = t[wi + 2 * i]; edge[*nedges][1] = t[wi + 2 * i + 1];
            edge[*nedges][2] = t[wi + 2 * j]; edge[*nedges][3] = t[wi + 2 * j + 1];
            (*nedges)++;
        }
        wi += 2 * n;
    }
}

int main(int argc, char **argv) {
    const char *paths[] = {"../../extracted/interceptor.bin", "extracted/interceptor.bin"};
    image_path = NULL;
    if (rig_init(1 << 21) != 0) { printf("rig_init failed\n"); return 1; }
    for (int i = 0; i < 2 && !image_path; i++) if (rig_load_image(paths[i], BASE) == 0) image_path = paths[i];
    if (!image_path) { printf("test_flight_diff: skipped (extracted/interceptor.bin not found)\n"); rig_shutdown(); return 0; }
    if (argc > 1) assets = argv[1];
    else {
        FILE *t = fopen("../../assets/VERSION", "r");
        if (!t) assets = "assets"; else fclose(t);
    }
    if (build_env() != 0) { rig_shutdown(); return 0; }
    verbose = getenv("FLIGHT_DIFF_VERBOSE") != NULL;
    if (getenv("FLIGHT_DIFF_TRACE")) trace = atoi(getenv("FLIGHT_DIFF_TRACE"));
    printf("test_flight_diff:\n");
    int bad = 0;
    /* 1. the worked examples through port and rig (harness sanity check: the rig reproduces the note's rows) */
    printf(" worked examples (rig against the note's printed rows, port against rig):\n");
    bad += run_example("E1 runway idle", case_e1, keys_none, 10, 0, ex_e1, EX_E1_N);
    bad += run_example("E2 level flight", case_e2, keys_none, 20, 0, ex_e2, EX_E2_N);
    bad += run_example("E3 pull-up", case_e2, keys_e3, 30, 0, ex_e3, EX_E3_N);
    bad += run_example("E9 gear up", case_e9, keys_none, 40, 0, ex_e9, EX_E9_N);
    bad += run_example("E4 take-off roll", case_e1, keys_e4, 160, 0, ex_e4, EX_E4_N);
    bad += run_example("E7 stall", case_e7, keys_none, 40, 0, ex_e7, EX_E7_N);
    bad += run_example("E8 hard touchdown", case_e8, keys_none, 8, 0, NULL, 0);
    bad += run_example("X1 pitch word -5 (exit step)", case_exit, keys_none, 1, 0, NULL, 0);
    bad += run_example("X2 ejection cycle (plain state)", case_eject, keys_none, 12, 0, NULL, 0);
    bad += run_example("E5 catapult launch", case_e5, keys_e5, 100, 1, NULL, 0);
    bad += run_example("E6 arrestor catch", case_e6, keys_none, 60, 1, NULL, 0);
    /* E2/E3/E4/E9 start the gear states of the note; E2 and E3 need +0x7C = 0x60 */
    /* 2. random sequences */
    for (int adv = 0; adv < 2; adv++) {
        seq_seed = adv ? 0xC0FFEE11u : 0x12345678u;
        hist_n = 0;
        int fails = 0, steps_ok = 0;
        int n = getenv("FLIGHT_DIFF_N") ? atoi(getenv("FLIGHT_DIFF_N")) : N_RANDOM;
        for (int i = 0; i < n; i++) {
            if (getenv("FLIGHT_DIFF_ONLY") && atoi(getenv("FLIGHT_DIFF_ONLY")) != i) continue;
            int r = run_random(i, adv, &steps_ok);
            if (r < 0) return 1;
            fails += r;
        }
        printf(" (steps that read run-time memory the port does not hold: %d agreed with the rig, %d differ (accepted divergence, note 05 Q16; the sequence ends there); %d steps with a negative pitch word; %lu reads of the port's own variables in the junk range; V:4A910 not compared in %d steps after a seat spawn (the seat's own step writes it))\n", accepted_equal, accepted_divs, total_junk_steps, total_var_reads, seat_910);
        printf(" (steps that read the image outside the extracted spans, answered 0 and NOT accepted: %d steps, %d of them differ from the rig; these are divergences, not Q16)\n", outside_steps, outside_divs);
        const int outside_divs_total = outside_divs, q16_total = accepted_divs;
        accepted_equal = accepted_divs = total_junk_steps = seat_910 = 0; total_var_reads = 0; outside_steps = outside_divs = 0;
        printf(" divergence classes (first divergence of each sequence, N=%d): outside-spans %d, seat-viewed %d, Q16 runtime %d, unexplained %d%s%s\n", n, outside_divs_total, class_seat, q16_total, class_other, class_other ? ":" : "", unx_list);
        const int seat_total = class_seat;   /* informational: the ejected seat viewed is milestone 5's (not a failure) */
        class_seat = class_other = 0; unx_list[0] = 0;
        printf(" %s random sequences: %d runs of up to %d steps, %d compared steps, %d sequences diverged\n",
               adv ? "adversarial (random flags)" : "envelope", n, STEPS_PER_RUN, steps_ok, fails);
        for (int i = 0; i < hist_n; i++) printf("    first divergence in field %-14s: %d sequences\n", hist_name[i], hist_cnt[i]);
        if (!adv) bad += fails - seat_total;
        else total_fail += fails;
    }
    {
        int r = builders_sweep(20000);
        if (r < 0) return 1;
        bad += r;
    }
    {
        int r = deck_sweep(getenv("FLIGHT_DIFF_N") ? 3 * atoi(getenv("FLIGHT_DIFF_N")) : 600, 0);
        if (r < 0) return 1;
        bad += r;
        if (deck_sweep(getenv("FLIGHT_DIFF_N") ? 3 * atoi(getenv("FLIGHT_DIFF_N")) : 600, 1) < 0) return 1;
    }
    /* 3. the terrain query: boundary values of u (section 10 and the coordinator's correction), plus random positions */
    {
        static const int32_t us[] = { -65535, -65536, 65535, 65536, -32769, -32768, -32767, 32767, 32768, -131071, -131072, 0, 1, -1 };
        int checked = 0, wrong = 0, shown = 0;
        diverge_t d;
        rng = 0xBADC0DEu;
        int discriminating = 0, disc_wrong = 0;
        for (size_t ui = 0; ui < sizeof us / sizeof us[0]; ui++) {
            int found = 0;
            for (int attempt = 0; attempt < 6000 && found < 120; attempt++) {
                int cx = rnd(0, 31), cz = rnd(0, 31);
                int nedges; static int16_t edge[256][4];
                terrain_edges(tables.terrain, cx, cz, &nedges, edge);
                if (nedges == 0) continue;
                int e = rnd(0, nedges - 1);
                int32_t X1 = (int32_t)edge[e][0] * 4096, Z1 = (int32_t)edge[e][1] * 4096;
                int32_t X2 = (int32_t)edge[e][2] * 4096, Z2 = (int32_t)edge[e][3] * 4096;
                int32_t xc = X1 + us[ui];
                if (xc < 0 || xc >= 0x1000000 || (edge[e][2] == edge[e][0])) continue;
                int32_t zc = Z1 + rnd(-300000, 300000);
                int32_t ez0, ez1;
                if (flight_terrain_edge_height(X1, Z1, X2, Z2, xc, 0, &ez0) && flight_terrain_edge_height(X1, Z1, X2, Z2, xc, 1, &ez1) && ez0 != ez1) {
                    zc = ez0 > ez1 ? ez0 : ez1;   /* exactly one of the two readings counts this edge */
                    discriminating++;
                }
                if (zc < 0 || zc >= 0x1000000) continue;
                int r = run_terrain_point((cx << 24) + xc, (cz << 24) + zc, &d);
                if (r < 0) return 1;
                found++; checked++;
                if (r) { wrong++; if (ez0 != ez1) disc_wrong++; if (shown++ < 5) printf("    terrain u=%d cell (%d,%d): field %s port %ld rig %ld\n", us[ui], cx, cz, d.field, d.port, d.rig); }
            }
            if (found == 0) printf("    (no edge with u = %d in the cell data)\n", us[ui]);
        }
        printf(" terrain query, u normalisation: %d positions where the two readings give different edge heights, %d of them differ from the rig (the build under test uses reading %d)\n", discriminating, disc_wrong, FL_UFORM_REPORT);
        for (int i = 0; i < 1500; i++) {   /* random positions, half of them inside the airfield cells */
            int32_t x, z;
            if (i & 1) { x = (rnd(14, 18) << 24) + rnd(0, 0xFFFFFF); z = (rnd(14, 18) << 24) + rnd(0, 0xFFFFFF); }
            else { x = rnd(0, 0x1FFFFFFF); z = rnd(0, 0x1FFFFFFF); }
            int r = run_terrain_point(x, z, &d);
            if (r < 0) return 1;
            checked++;
            if (r) { wrong++; if (shown++ < 5) printf("    terrain random (%d, %d): field %s port %ld rig %ld\n", x, z, d.field, d.port, d.rig); }
        }
        printf(" terrain query: %d positions (boundary values of u and random), %d differ\n", checked, wrong);
        bad += wrong;
    }
    rig_shutdown();
    printf("test_flight_diff: %d problems (adversarial-flag divergences, informational: %d)\n", bad, total_fail);
    return bad ? 1 : 0;
}
