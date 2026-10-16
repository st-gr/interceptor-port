/* tests/rig/test_ai_diff.c - differential test: the port's generic AI (src/core/ai.c: the gate, the range and speed control, the waypoint rule, target handling, the threatened
 * test and the evasion choice, the state routine and the steering) against the original's world update 0x26AF8 in the Musashi rig, for one AI record
 * (docs/re/07-ai-and-weapons.md sections 3 and 4).
 *
 * Convention (test_training_diff.c): the image at 0x10000; the variable area 0x4A6E8-0x54EBF cleared; a return instruction over the first word of 0x18E54, 0x1A18C, 0x1F11E,
 * 0x1F0E2 and of the launches 0x275F6 and 0x275C4; the case written into the records and the variables; rig_run(0x26AF8) with all registers 0. The input conversion 0x1DFAA,
 * which the step of every record calls right after the state routine, is a hook: at its call for the AI record (the second call: the player's own step is the first) the AI record
 * is read as the gate and the state routine left it, together with the player's record as the player's step left it (the port's target and avoidance inputs). Three records are
 * valid: the player (0), a target (7, a slot the update never visits: a valid, inert record) and the AI aircraft (8). The port runs ai_gate and ai_state on the same state.
 * Skipped (exit 0) when extracted/interceptor.bin is absent. Run from tools/m68k-rig (make test-ai). Environment: AI_DIFF_N (cases, default 6000), AI_DIFF_SEED,
 * AI_DIFF_VERBOSE (print every difference), AI_DIFF_STATE (only this state byte, hex), AI_DIFF_GATE=0|1 (only the state routine, with W0 bit 0x1000 clear so the gate stops after
 * its decrement / only cases with the gate), AI_DIFF_ONLY (one case), AI_DIFF_NORM_DIRECT (the normaliser 0x29C0E called directly: D0 = length, D5 to D7 = the components, results in D5 to
 * D7; AI_DIFF_N vectors, NORMD_ALL for every length 1 to 600, otherwise the lengths the game uses), AI_DIFF_TWEAK=field:value (change one field of the case AI_DIFF_ONLY), AI_DIFF_S8 and
 * AI_DIFF_FINAL (diagnostic prints). Records the original's collision pass marked as hit before the hook (W0 0x0200) are counted, not compared. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/ai.h"
#include "../../src/core/fx.h"
#include "../../src/core/assets.h"
#include "../../src/core/entities.h"
#include "../../src/core/flight.h"

#define BASE 0x10000u
#define V_LO 0x4A6E8u
#define V_HI 0x54EC0u
#define REC 0x4B23Cu
#define AIREC 8
#define TGT 7
#define RA (REC + AIREC * 0x200u)
#define RT (REC + TGT * 0x200u)

static uint32_t rng = 0x9E3779B9u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }
static int pct(int p) { return rnd(0, 99) < p; }

static void put8(uint32_t a, int v) { uint8_t b = (uint8_t)v; rig_poke(a, &b, 1); }
static void put16(uint32_t a, int v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 2); }
static void put32(uint32_t a, int32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 4); }
static int get8(uint32_t a) { uint8_t b; rig_peek(a, &b, 1); return b; }
static int get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return (int16_t)((b[0] << 8) | b[1]); }
static int32_t get32(uint32_t a) { uint8_t b[4]; rig_peek(a, b, 4); return (int32_t)(((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]); }

/* a record as the harness writes it and the port reads it */
typedef struct {
    int w0, w2, b4, state, cell_x, cell_z, cell_a, fine_x, fine_z;
    int32_t alt8, pos[3], vel[3], dest_h;
    int b20, dcx, dcz, dfx, dfz, target, route, repeat, range, counter, rate_p, rate_h, rate_r, group, cls, id, ai_flags, keys, pitch, heading, roll, S, W, gear, engage, turn_limit, throttle;
    int mat[9];
} rec_t;

typedef struct {
    int mode, progress, trig, part1, sim, scripted_defaults;
    rec_t a, t, p;
    int mode_target;   /* 0 player (0xFF), 1 player (0x80), 2 record 7 (0x87) */
    int req_ai;
} cs_t;

static void pos_to_cells(rec_t *r) {
    r->cell_x = (int)(r->pos[0] >> 22); r->cell_z = (int)(r->pos[2] >> 22);
    r->fine_x = (int)((r->pos[0] & 0x3FFFFF) >> 8); r->fine_z = (int)((r->pos[2] & 0x3FFFFF) >> 8);
    r->alt8 = r->pos[1] >> 8;
    r->cell_a = 4 * (3 - (r->fine_z >> 12)) + (3 - (r->fine_x >> 12));
}

static void rot_matrix(int pitch, int heading, int roll, int m[9]) {
    const double k = 3.14159265358979 * 2.0 / 28800.0;
    const double cp = cos(pitch * k), sp = sin(pitch * k), ch = cos(heading * k), sh = sin(heading * k), cr = cos(roll * k), sr = sin(roll * k);
    /* rows: lateral, up, forward as columns (lateral = column 0 ...): R = Ry(heading) * Rx(pitch) * Rz(roll) */
    double R[3][3];
    R[0][0] = ch * cr + sh * sp * sr;  R[0][1] = -ch * sr + sh * sp * cr;  R[0][2] = sh * cp;
    R[1][0] = cp * sr;                 R[1][1] = cp * cr;                  R[1][2] = -sp;
    R[2][0] = -sh * cr + ch * sp * sr; R[2][1] = sh * sr + ch * sp * cr;   R[2][2] = ch * cp;
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) m[3 * r + c] = (int)lround(R[r][c] * 16384.0);
}


static void gen_record(rec_t *r, int is_ai) {
    memset(r, 0, sizeof *r);
    static const int cls_tab[] = { 0x10, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x10, 0x12, 0x13 };
    r->cls = is_ai ? cls_tab[rnd(0, 9)] : (pct(70) ? 0x12 : 0x11);
    r->id = rnd(0, 255);
    r->group = is_ai ? AIREC : TGT;
    r->w0 = 0x11C8 | (pct(30) ? 0x2000 : 0);
    r->pos[0] = (62 + rnd(0, 6)) * 4194304 + rnd(0, 4194303);
    r->pos[2] = (62 + rnd(0, 6)) * 4194304 + rnd(0, 4194303);
    r->pos[1] = pct(20) ? rnd(0x100000, 0x240000) : rnd(0x10000, 0x600000);
    pos_to_cells(r);
    r->pitch = pct(60) ? rnd(0, 28799) : pct(50) ? rnd(-1600, 1600) + 28800 : rnd(0, 1600);
    r->pitch %= 28800;
    r->heading = rnd(0, 28799);
    r->roll = pct(50) ? rnd(0, 28799) : (rnd(-5000, 5000) + 28800) % 28800;
    rot_matrix((int16_t)(r->pitch >= 14400 ? r->pitch - 28800 : r->pitch), r->heading, (int16_t)(r->roll >= 14400 ? r->roll - 28800 : r->roll), r->mat);
    { static const int sp[] = { 0x900, 0x1200, 0x1680, 0x1800, 0x1D40, 0x1EC0, 0x600, 0x100, 0x1BC0 }; r->S = pct(60) ? sp[rnd(0, 8)] + rnd(-3, 3) : rnd(0, 8000); }
    r->W = pct(70) ? r->S : rnd(0, 8000);
    r->vel[1] = pct(50) ? -rnd(1, 3000) : rnd(0, 3000);
    r->vel[0] = rnd(-3000, 3000); r->vel[2] = rnd(-3000, 3000);
    r->rate_p = rnd(-700, 700); r->rate_h = rnd(-300, 300); r->rate_r = rnd(-900, 900);
    r->counter = pct(60) ? rnd(-3, 80) : rnd(-2, 2);
    r->engage = pct(70) ? rnd(3, 4) : 5;
    r->gear = pct(50) ? 0xE0 : 0;
    r->turn_limit = 0x1400;
    r->throttle = rnd(-32, 120);
    r->repeat = pct(50) ? 0x10 | rnd(0, 15) : rnd(0, 255);
    r->range = pct(30) ? 0x7FFF : pct(50) ? rnd(0, 0x480) : rnd(0, 0x3000);
    r->dest_h = rnd(0, 0x3000);
    r->route = rnd(0, 3);
    r->keys = rnd(0, 255);
    r->ai_flags = pct(50) ? 0x10 : rnd(0, 255);
    r->dcx = r->cell_x + rnd(-3, 3); r->dcz = r->cell_z + rnd(-3, 3);
    r->dfx = rnd(0, 16383); r->dfz = rnd(0, 16383);
    if (pct(15)) { r->dcx = r->cell_x; r->dcz = r->cell_z; r->dfx = r->fine_x + rnd(-300, 300); r->dfz = r->fine_z + rnd(-300, 300); r->dest_h = r->alt8 + rnd(-200, 200); }
    r->target = 0xFF;
}

static void gen(cs_t *c, int only_state, int gate_mode) {
    static const uint8_t STATES[] = { 0, 1, 3, 4, 6, 8, 2, 5, 7, 9, 0x0A, 0, 1, 6, 8 };
    memset(c, 0, sizeof *c);
    c->mode = pct(20) ? 5 : rnd(3, 8);
    c->progress = rnd(0, 4);
    c->trig = pct(90) ? 0 : 0xFF;
    c->part1 = pct(50);
    gen_record(&c->a, 1);
    gen_record(&c->t, 0);
    gen_record(&c->p, 0);
    c->p.cls = 0x11; c->p.w0 = 0x11C8; c->p.vel[0] = c->p.vel[1] = c->p.vel[2] = 0; c->p.S = c->p.W = 0; c->p.counter = 0;
    c->p.pitch = c->p.roll = c->p.heading = 0;
    for (int i = 0; i < 9; i++) c->p.mat[i] = (i % 4 == 0) ? 16384 : 0;
    /* the player and the target near the AI record: cells within a few of it */
    c->p.pos[0] = c->a.pos[0] + rnd(-6000000, 6000000); c->p.pos[2] = c->a.pos[2] + rnd(-6000000, 6000000); c->p.pos[1] = c->a.pos[1] + rnd(-300000, 300000);
    if (pct(30)) { c->p.pos[0] = c->a.pos[0] + rnd(-400000, 400000); c->p.pos[2] = c->a.pos[2] + rnd(-400000, 400000); c->p.pos[1] = c->a.pos[1] + rnd(-100000, 100000); }
    if (c->p.pos[1] < 0x10000) c->p.pos[1] = 0x10000;
    c->t.pos[0] = c->a.pos[0] + rnd(-5000000, 5000000); c->t.pos[2] = c->a.pos[2] + rnd(-5000000, 5000000); c->t.pos[1] = c->a.pos[1] + rnd(-200000, 200000);
    if (pct(30)) { c->t.pos[0] = c->a.pos[0] + rnd(-300000, 300000); c->t.pos[2] = c->a.pos[2] + rnd(-300000, 300000); c->t.pos[1] = c->a.pos[1] + rnd(-60000, 60000); }
    if (c->t.pos[1] < 0x10000) c->t.pos[1] = 0x10000;
    pos_to_cells(&c->p); pos_to_cells(&c->t);
    c->mode_target = rnd(0, 2);
    c->a.target = c->mode_target == 0 ? 0xFF : c->mode_target == 1 ? 0x80 : 0x80 + TGT;
    if (pct(10)) c->a.target = 0xFF;
    c->a.state = only_state >= 0 ? only_state : STATES[rnd(0, (int)sizeof STATES - 1)];
    c->a.w2 = (pct(15) ? 0x0100 : 0) | (pct(15) ? 0x0080 : 0) | (pct(15) ? 0x0008 : 0);
    if (pct(20)) c->a.w0 |= 0x0008;   /* not counted */
    if (pct(20)) c->a.b20 = 0x02;     /* out of control */
    if (pct(30)) c->a.b4 = pct(50) ? 0x20 : 0;
    if (!pct(55)) c->a.w0 &= ~0x1000;
    if (gate_mode == 1) c->a.w0 |= 0x1000; else if (gate_mode == 0) c->a.w0 &= ~0x1000;
    /* The waypoint runs (the table 0x2DCE8) are not supplied to the port by this harness: the cases that read them (the mode 5 shortcut of 3.2, the waypoint rule of 4.3) are left out. */
    if (c->mode == 5 && (c->a.w0 & 0x0008) && c->a.counter <= 1 && c->a.state != 8) c->a.counter = rnd(2, 40);
    if (c->a.state == 8 && !(c->a.w2 & 0x0100) && (uint16_t)c->a.range <= 0x480) c->a.range = rnd(0x481, 0x3000);
    c->scripted_defaults = 0;
    c->sim = 1;
    c->req_ai = pct(8);
}

static void write_rec(uint32_t base, const rec_t *r) {
    put16(base + 0x00, r->w0); put16(base + 0x02, r->w2); put8(base + 0x04, r->b4); put8(base + 0x05, r->state);
    put16(base + 0x06, r->cell_x); put16(base + 0x08, r->cell_z); put8(base + 0x0A, r->cell_a); put8(base + 0x0B, 0);
    put16(base + 0x0C, r->fine_x); put16(base + 0x0E, r->fine_z); put32(base + 0x10, r->alt8);
    put32(base + 0x14, r->pos[0]); put32(base + 0x18, r->pos[1]); put32(base + 0x1C, r->pos[2]);
    put8(base + 0x20, r->b20);
    put16(base + 0x2C, r->dcx); put16(base + 0x2E, r->dcz); put16(base + 0x30, r->dfx); put16(base + 0x32, r->dfz); put32(base + 0x34, r->dest_h);
    put8(base + 0x38, r->target); put8(base + 0x39, r->repeat); put8(base + 0x3A, r->route);
    put8(base + 0x2B, r->throttle);
    put32(base + 0x3E, r->vel[0]); put32(base + 0x42, r->vel[1]); put32(base + 0x46, r->vel[2]);
    put16(base + 0x4A, r->range); put16(base + 0x4C, r->counter);
    put16(base + 0x56, r->rate_p); put16(base + 0x58, r->rate_h); put16(base + 0x5A, r->rate_r);
    put8(base + 0x5E, r->group); put8(base + 0x62, r->cls); put8(base + 0x63, r->id); put8(base + 0x64, r->ai_flags); put8(base + 0x65, r->keys);
    put16(base + 0x66, r->pitch); put16(base + 0x68, r->heading); put16(base + 0x6A, r->roll); put16(base + 0x6C, r->S); put16(base + 0x6E, r->W);
    put32(base + 0x72, 0x61A800);
    put8(base + 0x7A, r->engage); put8(base + 0x7C, r->gear); put8(base + 0x7D, 3); put16(base + 0x7E, r->turn_limit);
    for (int i = 0; i < 9; i++) { put16(base + 0x80 + 2 * i, r->mat[i]); put16(base + 0x92 + 2 * i, r->mat[i]); }
}

static void read_rec(uint32_t base, rec_t *r) {
    memset(r, 0, sizeof *r);
    r->w0 = get16(base) & 0xFFFF; r->w2 = get16(base + 2) & 0xFFFF; r->b4 = get8(base + 4); r->state = get8(base + 5);
    r->cell_x = get16(base + 6); r->cell_z = get16(base + 8); r->cell_a = get8(base + 0x0A);
    r->fine_x = get16(base + 0x0C); r->fine_z = get16(base + 0x0E); r->alt8 = get32(base + 0x10);
    for (int i = 0; i < 3; i++) { r->pos[i] = get32(base + 0x14 + 4 * i); r->vel[i] = get32(base + 0x3E + 4 * i); }
    r->b20 = get8(base + 0x20); r->throttle = (int8_t)get8(base + 0x2B);
    r->dcx = get16(base + 0x2C); r->dcz = get16(base + 0x2E); r->dfx = get16(base + 0x30); r->dfz = get16(base + 0x32); r->dest_h = get32(base + 0x34);
    r->target = get8(base + 0x38); r->repeat = get8(base + 0x39); r->route = get8(base + 0x3A); r->range = get16(base + 0x4A) & 0xFFFF; r->counter = get16(base + 0x4C);
    r->rate_p = get16(base + 0x56); r->rate_h = get16(base + 0x58); r->rate_r = get16(base + 0x5A);
    r->group = get8(base + 0x5E); r->cls = get8(base + 0x62); r->id = get8(base + 0x63); r->ai_flags = get8(base + 0x64); r->keys = get8(base + 0x65);
    r->pitch = get16(base + 0x66) & 0xFFFF; r->heading = get16(base + 0x68) & 0xFFFF; r->roll = get16(base + 0x6A) & 0xFFFF; r->S = get16(base + 0x6C); r->W = get16(base + 0x6E);
    r->engage = get8(base + 0x7A); r->gear = get8(base + 0x7C); r->turn_limit = get16(base + 0x7E);
    for (int i = 0; i < 9; i++) r->mat[i] = get16(base + 0x92 + 2 * i);
}

static int hook_calls, have_hook;
static rec_t hooked_a, hooked_p;
static int h_t50, h_t52, h_t54;
static int launch_calls;
static uint32_t launch_who[8];
static void hook_launch(void) { if (launch_calls < 8) launch_who[launch_calls] = (uint32_t)get32(0x4AA6E); launch_calls++; }
static void hook_input_conversion(void) {
    hook_calls++;
    if (hook_calls == 2) {
        read_rec(RA, &hooked_a);
        read_rec(REC, &hooked_p);
        h_t50 = get16(RA + 0x50); h_t52 = get16(RA + 0x52); h_t54 = get16(RA + 0x54);
        have_hook = 1;
    }
}

static uint32_t cap_regs[16]; static uint8_t cap_mem[0x10000], cap_stk[0x1000]; static int cap_have;
static void probe_norm(void) {
    static int n;
    if (!cap_have) {
        for (int r = 0; r < 16; r++) cap_regs[r] = rig_get_reg((rig_reg_t)(RIG_D0 + r));
        rig_peek(0x4A000, cap_mem, sizeof cap_mem);
        rig_peek(0x1FF000, cap_stk, sizeof cap_stk);
        cap_have = 1;
    }
    if (n++ > 3) return;
    printf("29C0E:");
    for (int r = RIG_D0; r <= RIG_A6; r++) printf(" %X", rig_get_reg((rig_reg_t)r));
    uint32_t sp = rig_get_reg(RIG_A7);
    printf(" | sp=%X:", sp);
    for (int k = 0; k < 6; k++) printf(" %X", get32(sp + 4 * k));
    printf("\n");
    printf("  a6 frame:"); for (int k = -16; k < 4; k++) printf(" %X", (unsigned)get16(rig_get_reg(RIG_A6) + 2 * k)); printf("\n");
    printf("  a0:"); for (int k = 0; k < 8; k++) printf(" %X", (unsigned)get16(rig_get_reg(RIG_A0) + 2 * k)); printf("\n");
    printf("  stack words:"); for (int k = 0; k < 24; k++) printf(" %X", (unsigned)get16(sp + 2 * k)); printf("\n");
}
static int hitmarks;
#ifdef AI_TRACE
#include <stdarg.h>
void ai_trace(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
#endif
static const char *image_path;
static int rig_reset(void) {
    if (rig_load_image(image_path, BASE) != 0) return -1;
    static const uint32_t patch[] = { 0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x275C4 };
    for (int i = 0; i < 5; i++) put16(patch[i], 0x4E75);
    static uint8_t zero[V_HI - V_LO];
    rig_poke(V_LO, zero, sizeof zero);
    rig_hooks_clear();
    rig_hook_set(0x1DFAA, hook_input_conversion, 0);
    rig_hook_set(0x275F6, hook_launch, 0);   /* the AI launch of the missile record: counted, not performed */
    return 0;
}

static void rig_write(const cs_t *c) {
    put8(0x4A95E, c->mode); put8(0x4A95F, c->progress); put8(0x4A852, c->trig); put8(0x4A84B, c->part1);
    put8(0x4A84D, 1); put8(0x4A846, 1); put16(0x4A984, 0x0840); put32(0x4A7B2, 0x800000);
    put8(0x4A83C, 1); put8(0x4A847, 1); put8(0x4A844, 1); put8(0x4A83D, 1);
    put16(0x4AA78, 0xFFFF);
    put8(0x4A874, c->req_ai);
    write_rec(REC, &c->p);
    put16(REC + 0x00, 0x11C8); put16(REC + 0x02, 0x0080); put8(REC + 0x63, 0x0D);
    write_rec(RT, &c->t);
    write_rec(RA, &c->a);
}

/* ---- the port side --------------------------------------------------------------------------------------------------------------------------- */
static entities_t E;
static aircraft_t PLAYER;
static ai_t AITAB;
static int have_tab;
static size_t ai_len;
static int ai_read(void *user, const char *path, uint8_t **out, size_t *len) { (void)path; *out = (uint8_t *)user; *len = ai_len; return 0; }
static void ai_release(void *user, uint8_t *buf) { (void)user; (void)buf; }

static void load_port(aircraft_t *a, entity_t *en, const rec_t *r) {
    flight_state_t *f = &a->f;
    memset(f, 0, offsetof(flight_state_t, vtx));
    f->w0 = (uint16_t)r->w0; f->w2 = (uint16_t)r->w2; f->b4 = (uint8_t)r->b4; f->state = (uint8_t)r->state;
    f->cell_x = (int16_t)r->cell_x; f->cell_z = (int16_t)r->cell_z; f->cell_a = (uint8_t)r->cell_a; f->fine_x = (int16_t)r->fine_x; f->fine_z = (int16_t)r->fine_z; f->alt8 = r->alt8;
    for (int i = 0; i < 3; i++) { a->pos[i] = r->pos[i]; f->vel[i] = r->vel[i]; }
    f->b20 = (uint8_t)r->b20; f->throttle = (int8_t)r->throttle;
    en->dest_cx = (int16_t)r->dcx; en->dest_cz = (int16_t)r->dcz; en->dest_fx = (int16_t)r->dfx; en->dest_fz = (int16_t)r->dfz; en->dest_h = r->dest_h;
    en->target = (uint8_t)r->target; f->repeat = (uint8_t)r->repeat; en->route = (uint8_t)r->route; en->range = (int16_t)r->range; f->counter = (int16_t)r->counter;
    f->rate_p = (int16_t)r->rate_p; f->rate_h = (int16_t)r->rate_h; f->rate_r = (int16_t)r->rate_r;
    f->group = (uint8_t)r->group; f->cls = (uint8_t)r->cls; f->id = (uint8_t)r->id; en->ai_flags = (uint8_t)r->ai_flags; f->keys = (uint8_t)r->keys;
    f->pitch = (uint16_t)r->pitch; f->heading = (uint16_t)r->heading; f->roll = (uint16_t)r->roll; f->speed_target = (int16_t)r->S; f->speed = (int16_t)r->W;
    en->engage = (uint8_t)r->engage; f->gear = (uint8_t)r->gear; f->turn_limit = (int16_t)r->turn_limit; f->l7d = 3;
    for (int i = 0; i < 9; i++) f->mat_a[i] = f->mat_b[i] = (int16_t)r->mat[i];
}

static int diffs, cases, skipped, compared_gate, compared_state, launched;
static int verbose;
static const flight_state_t *dbg_f;
static const entity_t *dbg_e;
static const rec_t *dbg_r;
static void report(int idx, const cs_t *c, const char *what, long port, long rig) {
    diffs++;
    if ((verbose || diffs <= 10) && dbg_f)
        printf("  case %d rig  end: state %02X counter %d keys %02X flags %02X turn_limit %04X dest(%d,%d,%d,%d,%d) range %04X rep %02X S %d b4 %02X target %02X eng %d id %02X w0 %04X w2 %04X\n", idx, dbg_r->state, dbg_r->counter, dbg_r->keys, dbg_r->ai_flags, (uint16_t)dbg_r->turn_limit, dbg_r->dcx, dbg_r->dcz, dbg_r->dfx, dbg_r->dfz, dbg_r->dest_h, (uint16_t)dbg_r->range, dbg_r->repeat, dbg_r->S, dbg_r->b4, dbg_r->target, dbg_r->engage, dbg_r->id, dbg_r->w0, dbg_r->w2);
    if ((verbose || diffs <= 10) && dbg_f)
        printf("  case %d port end: state %02X counter %d keys %02X flags %02X turn_limit %04X dest(%d,%d,%d,%d,%d) range %04X rep %02X S %d b4 %02X target %02X eng %d\n", idx, dbg_f->state, dbg_f->counter, dbg_f->keys, dbg_e->ai_flags, (uint16_t)dbg_f->turn_limit, dbg_e->dest_cx, dbg_e->dest_cz, dbg_e->dest_fx, dbg_e->dest_fz, dbg_e->dest_h, (uint16_t)dbg_e->range, dbg_f->repeat, dbg_f->speed_target, dbg_f->b4, dbg_e->target, dbg_e->engage);
    if (verbose || diffs <= 10)
        printf("  case %d: %s: port %ld (0x%lX), rig %ld (0x%lX)   [mode %d prog %d trig %02X part1 %d | state %02X cls %02X w0 %04X w2 %04X b4 %02X b20 %02X cnt %d S %d W %d tgt %02X "
               "range %04X rep %02X flags %02X eng %d gear %02X keys %02X dest(%d,%d,%d,%d,%d) cell(%d,%d,%d,%d) alt8 %d vel_y %d roll %d pitch %d]\n",
               idx, what, port, (unsigned long)port & 0xFFFFFFFFul, rig, (unsigned long)rig & 0xFFFFFFFFul, c->mode, c->progress, c->trig, c->part1, c->a.state, c->a.cls, c->a.w0, c->a.w2, c->a.b4, c->a.b20,
               c->a.counter, c->a.S, c->a.W, c->a.target, c->a.range, c->a.repeat, c->a.ai_flags, c->a.engage, c->a.gear, c->a.keys, c->a.dcx, c->a.dcz, c->a.dfx, c->a.dfz, c->a.dest_h,
               c->a.cell_x, c->a.cell_z, c->a.fine_x, c->a.fine_z, c->a.alt8, c->a.vel[1], c->a.roll, c->a.pitch);
    if (verbose || diffs <= 10) printf("      target S %d  player S %d  prog %d\n", c->t.S, c->p.S, c->progress);
}

static void port_run(const cs_t *c) {
    memset(&E, 0, sizeof E);
    entities_init(&E, NULL, &AITAB, NULL);
    entities_attach_player(&E, &PLAYER);
    E.mode = (uint8_t)c->mode; E.progress = c->progress; E.trig = (int8_t)c->trig; E.training_part1 = (uint8_t)c->part1;
    E.flying = 1; E.sim_on = 1; E.designated = -1;
    load_port(&PLAYER, &E.rec[0], &hooked_p);
    load_port(&E.rec[TGT].ac, &E.rec[TGT], &c->t);
    load_port(&E.rec[AIREC].ac, &E.rec[AIREC], &c->a);
    E.hostile_cap = 4;
    ai_gate(&E, AIREC);
    flight_state_t *f = &E.rec[AIREC].ac.f;
    if ((f->w2 & EW2_SCRIPTED) && f->state == 0) f->w2 |= EW2_RECENTRE;
    else ai_state(&E, AIREC);
}

#define CHK(name, pv, rv) do { if (!bad && (long)(pv) != (long)(rv)) { bad = name; pv_ = (long)(pv); rv_ = (long)(rv); } } while (0)

int main(int argc, char **argv) {
    const char *paths[] = {"../../extracted/interceptor.bin", "extracted/interceptor.bin"};
    (void)argc; (void)argv;
    image_path = NULL;
    if (rig_init(1 << 21) != 0) { printf("rig_init failed\n"); return 1; }
    for (int i = 0; i < 2 && !image_path; i++) if (rig_load_image(paths[i], BASE) == 0) image_path = paths[i];
    if (!image_path) { printf("test_ai_diff: skipped (extracted/interceptor.bin not found)\n"); rig_shutdown(); return 0; }
    {   /* the port's tables: the extracted tables/ai.bin when present (../../assets), else the harness skips */
        FILE *f = fopen("../../assets/tables/ai.bin", "rb");
        if (f) {
            static uint8_t *bufp;
            fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
            bufp = (uint8_t *)malloc((size_t)n + 1);
            if (fread(bufp, 1, (size_t)n, f) == (size_t)n) {
                game_loader_t ld = { ai_read, ai_release, bufp };
                ai_len = (size_t)n;
                have_tab = assets_load_ai(&ld, &AITAB, NULL, 0) == 0;
            }
            fclose(f);
        }
    }
    if (!have_tab) { printf("test_ai_diff: skipped (assets/tables/ai.bin not found)\n"); rig_shutdown(); return 0; }
    const int N = getenv("AI_DIFF_N") ? atoi(getenv("AI_DIFF_N")) : 6000;
    if (getenv("AI_DIFF_SEED")) rng = (uint32_t)atoi(getenv("AI_DIFF_SEED")) * 2654435761u + 1;
    verbose = getenv("AI_DIFF_VERBOSE") != NULL;
    const int only_state = getenv("AI_DIFF_STATE") ? (int)strtol(getenv("AI_DIFF_STATE"), NULL, 16) : -1;
    const int gate_mode = getenv("AI_DIFF_GATE") ? atoi(getenv("AI_DIFF_GATE")) : -1;
    if (getenv("AI_DIFF_PROBE_CALL")) {   /* print the registers and stack at the entry of the normaliser 0x29C0E in a steering run */
        cs_t c; gen(&c, 1, 1);
        c.a.w0 = 0x11C8; c.a.w2 = 0; c.a.b20 = 0; c.a.state = 1; c.a.target = 0xFF; c.a.cls = 0x10; c.a.counter = 50; c.a.range = 0x7FFF; c.a.repeat = 0x05; c.a.engage = 5; c.a.ai_flags = 0x10;
        rig_reset(); rig_write(&c); hook_calls = 0; have_hook = 0;
        rig_hook_set(0x29C0E, probe_norm, 0);
        for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
        rig_run(0x26AF8u);
        rig_hooks_clear();
        rig_poke(0x4A000, cap_mem, sizeof cap_mem); rig_poke(0x1FF000, cap_stk, sizeof cap_stk);
        for (int r = 0; r < 16; r++) rig_set_reg((rig_reg_t)(RIG_D0 + r), cap_regs[r]);
        rig_run(0x29C0E);
        for (int r = 0; r < 16; r++) if (rig_get_reg((rig_reg_t)(RIG_D0 + r)) != cap_regs[r]) printf("reg %d: %X -> %X\n", r, cap_regs[r], rig_get_reg((rig_reg_t)(RIG_D0 + r)));
        static uint8_t m2[0x10000]; rig_peek(0x4A000, m2, sizeof m2);
        for (unsigned k = 0; k < sizeof m2; k++) if (m2[k] != cap_mem[k]) printf("mem %X: %02X -> %02X\n", 0x4A000 + k, cap_mem[k], m2[k]);
        static uint8_t s2[0x1000]; rig_peek(0x1FF000, s2, sizeof s2);
        for (unsigned k = 0; k < sizeof s2; k++) if (s2[k] != cap_stk[k]) printf("stk %X: %02X -> %02X\n", 0x1FF000 + k, cap_stk[k], s2[k]);
        return 0;
    }
    if (getenv("AI_DIFF_NORM_DIRECT")) {   /* the normaliser 0x29C0E called directly: D0 = length, D5..D7 = the components (sign-extended words), results in D5..D7 */
        int bad = 0;
        rig_reset();
        for (int n = 0; n < N; n++) {
            int len = getenv("NORMD_ALL") ? (pct(70) ? 0xC0 : rnd(1, 600)) : pct(85) ? 0xC0 : pct(50) ? 0x100 : 0x3C0;
            int sc = pct(30) ? 300 : pct(50) ? 5000 : 18000;
            int32_t v[3] = { rnd(-sc, sc), rnd(-sc, sc), rnd(-sc, sc) };
            if (pct(15)) v[1] = v[2] = rnd(-20, 20);
            for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
            rig_set_reg(RIG_D0, (uint32_t)len); rig_set_reg(RIG_D5, (uint32_t)(int32_t)(int16_t)v[0]); rig_set_reg(RIG_D6, (uint32_t)(int32_t)(int16_t)v[1]); rig_set_reg(RIG_D7, (uint32_t)(int32_t)(int16_t)v[2]);
            rig_hooks_clear();
            if (rig_run(0x29C0E) != 0) { printf("run failed\n"); return 1; }
            int32_t rr[3] = { (int16_t)rig_get_reg(RIG_D5), (int16_t)rig_get_reg(RIG_D6), (int16_t)rig_get_reg(RIG_D7) }, pp[3];
            ai_normalise(v[0], v[1], v[2], len, pp);
            if (getenv("NORMD_LIST")) printf("TUP len %d A %d s %d q %d v %d %d %d out %d %d %d\n", len, (int)rig_get_reg(RIG_D1), (int)rig_get_reg(RIG_D2), (int)(rig_get_reg(RIG_D0) & 0xFFFF), v[0], v[1], v[2], rr[0], rr[1], rr[2]);
            if (rr[0] != (int16_t)pp[0] || rr[1] != (int16_t)pp[1] || rr[2] != (int16_t)pp[2]) {
                if (bad++ < 12) printf("NORMD len %d v(%d,%d,%d): port (%d,%d,%d) rig (%d,%d,%d) D0 %X D1 %X D2 %X D3 %X D4 %X (port A %d)\n", len, v[0], v[1], v[2], pp[0], pp[1], pp[2], rr[0], rr[1], rr[2], rig_get_reg(RIG_D0), rig_get_reg(RIG_D1), rig_get_reg(RIG_D2), rig_get_reg(RIG_D3), rig_get_reg(RIG_D4), (int)fx_dist(&(fx_vec_t){ abs(v[0]), abs(v[1]), abs(v[2]) }));
            }
        }
        printf("norm direct: %d cases, %d differences\n", N, bad);
        return bad != 0;
    }
    if (getenv("AI_DIFF_SWEEP_NORM")) {   /* the normalised vector of the steering, read back through the bank limit (identity matrix: limit = 64 * n0) */
        for (int n = 0; n < N; n++) {
            cs_t c; gen(&c, 1, 1);
            int X = rnd(300, 18000), Y = rnd(-X / 2, X / 2), Z = rnd(0, X / 2);
            if (pct(20)) { Y = rnd(-X, X) / 4; Z = rnd(0, X) / 10; }
            c.a.w0 = 0x11C8; c.a.w2 = 0; c.a.b20 = 0; c.a.state = 1; c.a.target = 0xFF; c.a.cls = 0x10; c.a.counter = 50; c.a.range = 0x7FFF; c.a.repeat = 0x05; c.a.engage = 5; c.a.ai_flags = 0x10;
            c.a.pos[0] = 64 * 4194304; c.a.pos[2] = 64 * 4194304; c.a.pos[1] = 0x300000; pos_to_cells(&c.a);
            c.a.fine_x = 0; c.a.fine_z = 0; c.a.alt8 = 0x3000; c.a.pos[0] = 64 * 4194304; c.a.pos[2] = 64 * 4194304; c.a.pos[1] = 0x300000;
            c.a.dcx = c.a.cell_x; c.a.dcz = c.a.cell_z; c.a.dfx = X; c.a.dfz = Z < 0 ? 0 : Z; c.a.dest_h = c.a.alt8 + Y;
            if (Z < 0) { c.a.dcz = c.a.cell_z - 1; c.a.dfz = 16384 + Z; }
            c.a.pitch = c.a.roll = c.a.heading = 0; c.a.S = 0x1800; c.a.W = 0x1800; c.a.vel[1] = 0; c.a.vel[0] = c.a.vel[2] = 0;
            for (int i = 0; i < 9; i++) c.a.mat[i] = (i % 4 == 0) ? 16384 : 0;
            c.mode = 4;
            rig_reset(); rig_write(&c); hook_calls = 0; have_hook = 0;
            for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
            rig_run(0x26AF8u);
            printf("NORM %d %d %d lim %d\n", X, Y, Z, (uint16_t)hooked_a.turn_limit);
        }
        return 0;
    }
    if (getenv("AI_DIFF_SWEEP_END")) {   /* the end routine: state 1 with a negative counter, over the ground bit, target, progress and the flags 5 and 6 */
        for (int gnd = 0; gnd < 2; gnd++) for (int tg = 0; tg < 3; tg++) for (int pr = 0; pr < 5; pr++) for (int fl = 0; fl < 4; fl++) for (int act = 0; act < 2; act++) {
            cs_t c; gen(&c, 1, act);
            c.a.w0 = act ? 0x11C8 : 0x01C8; c.a.w2 = gnd ? 0x80 : 0; c.a.b20 = 0; c.a.state = 1; c.a.counter = 0; c.a.cls = 0x12;
            c.a.target = tg == 0 ? 0xFF : tg == 1 ? 0x80 : 0x87; c.mode = 4; c.progress = pr; c.a.ai_flags = 0x10 | (fl & 1 ? 0x20 : 0) | (fl & 2 ? 0x40 : 0);
            c.a.range = 0x7FFF; c.a.repeat = 0x05;
            rig_reset(); rig_write(&c); hook_calls = 0; have_hook = 0;
            for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
            rig_run(0x26AF8u);
            printf("END gnd=%d tgt=%02X prog=%d flags56=%d act=%d -> state %02X counter %d\n", gnd, c.a.target, pr, fl, act, hooked_a.state, hooked_a.counter);
        }
        return 0;
    }
    if (getenv("AI_DIFF_SWEEP_RANGE")) {   /* when does the range routine recompute: old range x nibble (state 0, no target, class 0x10) */
        static const int olds[] = { 0, 0x100, 0x47F, 0x480, 0x481, 0x8FF, 0x900, 0x901, 0x1000, 0x1800, 0x1E00, 0x3000, 0x36C0, 0x7FFF };
        for (unsigned oi = 0; oi < sizeof olds / sizeof *olds; oi++) {
            printf("old range %04X:", olds[oi]);
            for (int nib = 0; nib < 16; nib++) {
                cs_t c; gen(&c, 0, 1);
                c.a.w0 = 0x11C8; c.a.w2 = 0; c.a.b20 = 0; c.a.state = 0; c.a.target = 0xFF; c.a.cls = 0x10; c.a.range = olds[oi]; c.a.repeat = (nib << 4) | 5; c.a.counter = 20;
                c.a.dcx = c.a.cell_x + 1; c.a.dcz = c.a.cell_z; c.a.dfx = c.a.fine_x; c.a.dfz = c.a.fine_z; c.a.dest_h = c.a.alt8;
                rig_reset(); rig_write(&c); hook_calls = 0; have_hook = 0;
                for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
                rig_run(0x26AF8u);
                printf(" %d:%s", nib, (hooked_a.repeat >> 4) == nib + 1 ? "-" : "R");
            }
            printf("\n");
        }
        return 0;
    }
    for (int n = 0; n < N; n++) {
        cs_t c;
        gen(&c, only_state, gate_mode);
        if (getenv("AI_DIFF_ONLY") && n != atoi(getenv("AI_DIFF_ONLY"))) continue;
        if (getenv("AI_DIFF_TWEAK")) {   /* experiments on one case: AI_DIFF_TWEAK=gnd0|tgt:XX|cnt:N|w2:XXXX|range:XXXX|prog:N */
            const char *t = getenv("AI_DIFF_TWEAK");
            if (!strcmp(t, "gnd0")) c.a.w2 &= (uint16_t)~0x80;
            else if (!strncmp(t, "tgt:", 4)) c.a.target = (uint8_t)strtol(t + 4, NULL, 16);
            else if (!strncmp(t, "cnt:", 4)) c.a.counter = (int16_t)atoi(t + 4);
            else if (!strncmp(t, "w2:", 3)) c.a.w2 = (uint16_t)strtol(t + 3, NULL, 16);
            else if (!strncmp(t, "range:", 6)) c.a.range = (uint16_t)strtol(t + 6, NULL, 16);
            else if (!strncmp(t, "prog:", 5)) c.progress = atoi(t + 5);
            else if (!strncmp(t, "pitch:", 6)) c.a.pitch = atoi(t + 6);
            else if (!strncmp(t, "roll:", 5)) c.a.roll = atoi(t + 5);
            else if (!strncmp(t, "pw0:", 4)) c.p.w0 = (int)strtol(t + 4, NULL, 16);
            else if (!strncmp(t, "pw2:", 4)) c.p.w2 = (int)strtol(t + 4, NULL, 16);
            else if (!strncmp(t, "pcls:", 5)) c.p.cls = (int)strtol(t + 5, NULL, 16);
            else if (!strncmp(t, "pstate:", 7)) c.p.state = (int)strtol(t + 7, NULL, 16);
            else if (!strncmp(t, "pS:", 3)) c.p.S = c.p.W = atoi(t + 3);
            else if (!strncmp(t, "pb4:", 4)) c.p.b4 = (int)strtol(t + 4, NULL, 16);
            else if (!strncmp(t, "pb20:", 5)) c.p.b20 = (int)strtol(t + 5, NULL, 16);
            else if (!strncmp(t, "w0:", 3)) c.a.w0 = (uint16_t)strtol(t + 3, NULL, 16);
            else if (!strncmp(t, "mode:", 5)) c.mode = atoi(t + 5);
            else if (!strncmp(t, "rep:", 4)) c.a.repeat = (uint8_t)strtol(t + 4, NULL, 16);
            else if (!strncmp(t, "eng:", 4)) c.a.engage = atoi(t + 4);
            else if (!strncmp(t, "b4:", 3)) c.a.b4 = (uint8_t)strtol(t + 3, NULL, 16);
            else if (!strncmp(t, "alt8:", 5)) c.a.alt8 = atoi(t + 5);
            else if (!strncmp(t, "vy:", 3)) c.a.vel[1] = atoi(t + 3);
            else if (!strncmp(t, "S:", 2)) c.a.S = c.a.W = atoi(t + 2);
            else if (!strncmp(t, "cls:", 4)) c.a.cls = (uint8_t)strtol(t + 4, NULL, 16);
            else if (!strncmp(t, "flags:", 6)) c.a.ai_flags = (uint8_t)strtol(t + 6, NULL, 16);
        }
        if (rig_reset() != 0) { printf("rig_reset failed\n"); return 1; }
        rig_write(&c);
        hook_calls = 0; have_hook = 0; launch_calls = 0;
        for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
        if (rig_run(0x26AF8u) != 0 || !have_hook) { skipped++; continue; }
        if ((hooked_a.w0 & 0x0200) && !(c.a.w0 & 0x0200)) { hitmarks++; continue; }   /* the collision pass of the original marked the record hit before the hook (W0 0x0200, B20 0x02, W2 0x1000, state 1): not the AI's */
        cases++;
        if (getenv("AI_DIFF_FINAL")) printf("FINAL case %d: b20 in %02X snapshot %02X final %02X; w0 in %04X snapshot %04X final %04X; launches %d by %X\n", n, c.a.b20, hooked_a.b20, get8(RA + 0x20), c.a.w0, hooked_a.w0 & 0xFFFF, get16(RA) & 0xFFFF, launch_calls, launch_who[0]);
        port_run(&c);
        const flight_state_t *f = &E.rec[AIREC].ac.f;
        const entity_t *en = &E.rec[AIREC];
        const rec_t *rs = &hooked_a;
        if (getenv("AI_DIFF_S8") && c.a.state == 8)
            printf("S8 mode=%d prog=%d trig=%d part1=%d tgt=%02X cls=%02X w0=%04X w2=%04X b4=%02X scr=%d ooc=%d gnd=%d cnt=%d range=%04X roll=%d pitch=%d act=%d eng=%d | steered=%d state->%02X cnt->%d rep=%02X tgt=%02X b4 %02X->%02X\n", c.mode, c.progress, c.trig, c.part1, c.a.target, c.a.cls, c.a.w0, c.a.w2, c.a.b4, (c.a.w2 >> 8) & 1, (c.a.b20 >> 1) & 1, (c.a.w2 >> 7) & 1, c.a.counter, c.a.range, c.a.roll, c.a.pitch,
                   (c.a.w0 >> 12) & 1, c.a.engage, rs->turn_limit != 0x1400 || rs->ai_flags != c.a.ai_flags, rs->state, rs->counter, c.a.repeat, c.a.target, c.a.b4, rs->b4);
        if (getenv("AI_DIFF_B4") && (c.a.w0 & 0x1000))
            printf("B4 rc=%d b4=%d st=%02X->%02X tgt=%02X cls=%02X w0=%04X w2=%04X cnt=%d range=%04X->%04X eng=%d scr=%d id=%02X->%02X\n", ((c.a.repeat + 0x10) & 0xF0) != (rs->repeat & 0xF0) ? 1 : 0, rs->b4 & 1, c.a.state, rs->state, c.a.target, c.a.cls, c.a.w0, c.a.w2, c.a.counter,
                   c.a.range, rs->range, c.a.engage, (c.a.w2 >> 8) & 1, c.a.id, rs->id);
        dbg_f = f; dbg_e = en; dbg_r = rs;
        const char *bad = NULL; long pv_ = 0, rv_ = 0;
        CHK("state", f->state, rs->state);
        CHK("counter", f->counter, rs->counter);
        CHK("keys", f->keys, rs->keys);
        CHK("ai_flags", en->ai_flags, rs->ai_flags);
        CHK("turn_limit", (uint16_t)f->turn_limit, (uint16_t)rs->turn_limit);
        CHK("turn50", f->turn50, h_t50); CHK("turn52", f->turn52, h_t52); CHK("turn54", f->turn54, h_t54);
        CHK("w0", f->w0, rs->w0);
        CHK("w2", f->w2 & 0xFFFE, rs->w2 & 0xFFFE);
        CHK("b4", f->b4, rs->b4);
        CHK("dest_cx", en->dest_cx, rs->dcx); CHK("dest_cz", en->dest_cz, rs->dcz); CHK("dest_fx", en->dest_fx, rs->dfx); CHK("dest_fz", en->dest_fz, rs->dfz); CHK("dest_h", en->dest_h, rs->dest_h);
        CHK("target", en->target, rs->target);
        CHK("repeat", f->repeat, rs->repeat);
        CHK("range", (uint16_t)en->range, rs->range);
        CHK("S", f->speed_target, rs->S); CHK("W", f->speed, rs->W);
        CHK("engage", en->engage, rs->engage);
        CHK("id", f->id, rs->id);
        CHK("gear", f->gear, rs->gear);
        CHK("throttle", f->throttle, rs->throttle);
        if (!bad) {   /* the launch decision for the record below the missile record 9 (4.6) */
            E.req_ai = (uint8_t)c.req_ai;
            E.rec[AIREC].ac.f.w0 &= (uint16_t)~0x0100;   /* the owner's own step (it precedes the missile record's turn) has cleared the keep-moving bit */
            /* the records 9, 11, 13 ask in this order about the owners 8, 10, 12 (empty here); the global request goes to the first that asks */
            int yes = ai_launch_decision(&E, AIREC);
            if (!yes && E.req_ai) yes = ai_launch_decision(&E, AIREC + 2);
            if (yes != (launch_calls > 0)) { bad = "launch decision"; pv_ = yes; rv_ = launch_calls; }
            launched += launch_calls > 0;
        }
        if (bad) report(n, &c, bad, pv_, rv_);
        if (c.a.w0 & 0x1000) compared_gate++; else compared_state++;
    }
    printf("test_ai_diff: %d cases (%d compared, %d skipped, %d hit by the collision pass), %d differences; %d with the gate, %d state routine only, %d launch decisions yes; tables: %s\n", N, cases, skipped, hitmarks, diffs, compared_gate, compared_state, launched, "extracted ai.bin");
    rig_shutdown();
    return diffs ? 1 : 0;
}
