/* tests/rig/test_weapons_diff.c - differential tests of the weapons slice (src/core/weapons.c, flight.c, ai.c) against the original in the Musashi rig
 * (docs/re/07-ai-and-weapons.md sections 7 to 10 and 13).
 *
 * Parts (selected by the environment variable WEAPONS_DIFF_PART, default all):
 *   missile   a player's missile launched through the world update 0x26AF8 (V:4A872 = 1 in the first pass) from an owner (record 0, invalid so that the update does not step it) and followed pass
 *             by pass: the launch (the copy of the owner, the hard point, the stock, the life, the flags), the gate (boost, coast, expiry, the live retarget of the designated record 7), the
 *             steering with the blind period, the response and the flight step. The port runs entities_step with the weapons hooks. Every mapped word of record 1 is compared each pass.
 *   launchdec the hostile launches of the records 9, 11, 13: the decision 0x2701A (mask 0x8700, out of control, the global request V:4A874, the target tests) and the launch routine 0x275F6 (rate gate, stock)
 *             over random owners and targets, against ai_launch_decision and weapons_launch_ai.
 *   gun       the gun routine 0x172DC (called alone) against weapons_gun over random pools, requests, stores and attitudes: the pool, the references, the reservoir, the timer, the rounds, the
 *             counters and the decoys' motion over several calls.
 *   hostile   an AI missile (the launch decision, the decoys, ECM) against the player: the contact scan, the damage handler, the posts and the counters.
 *   gunhit    the gun's hit test 0x2ADDE with the hull lists of ai.bin on records with the vertex slots the draw would leave (any level the picker chooses, its part id unfiltered, slots a block lacks kept from an
 *             earlier draw). The route into the big-hull test 0x2B3F0 is covered: the queue entries carry the descriptor pointer (+2) of the record's model, so the original reads the model's block flag byte (+7 of the block named by the
 *             first word of the model's list, or the word at +4) and takes the box of 0x800 and the ship volumes for the models 0 (missile, 0x18), 8, 9, 12 (0x11) and 10 (0xFC); the model 1 (0x48) stays on the aircraft
 *             hull. The port's flag byte is the extractor's hull_flags (extractor 20). The volume test 0x2B3F0 itself is also compared directly by tests/rig/test_ship_diff.c (part bighull, make test-ships), and the slots the draw leaves by tests/rig/test_slots_diff.c (make test-slots, the original's drawer run on the
 *             record); the class-to-lists and box rules of the route are the note's reading (8.2) with the unit tests in tests/test_weapons.c.
 *   demo      the shipped demonstration played by the whole core; from DEMO_FROM the original's world update runs on the port's player and the MiG of the demo is compared every pass.
 * Convention (test_ai_traj.c): the image at 0x10000; the variable area cleared; a return over the first word of 0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2 and the sound/draw wrappers; run from tools/m68k-rig
 * (make test-weapons). Skipped (exit 0) without extracted/interceptor.bin or the extracted assets. Environment: WEAPONS_DIFF_N (cases), WEAPONS_DIFF_STEPS, WEAPONS_DIFF_SEED, WEAPONS_DIFF_VERBOSE,
 * WEAPONS_DIFF_ALL (print every step of case WEAPONS_DIFF_CASE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/flight.h"
#include "../../src/core/entities.h"
#include "../../src/core/weapons.h"
#include "../../src/core/radar.h"
#include "../../src/core/ai.h"
#include "../../src/core/world.h"
#include "../../src/core/logbook.h"

#define BASE 0x10000u
#define V_LO 0x4A6E8u
#define V_HI 0x54EC0u
#define REC 0x4B23Cu
#define RECA(i) (REC + (uint32_t)(i) * 0x200u)

static void put8(uint32_t a, int v) { uint8_t b = (uint8_t)v; rig_poke(a, &b, 1); }
static void put16(uint32_t a, int v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 2); }
static void put32(uint32_t a, int32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 4); }
static int get8(uint32_t a) { uint8_t b; rig_peek(a, &b, 1); return b; }
static int get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return (int16_t)((b[0] << 8) | b[1]); }
static int32_t get32(uint32_t a) { uint8_t b[4]; rig_peek(a, b, 4); return (int32_t)(((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]); }

static uint32_t rng = 0x9E3779B9u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }
static int pct(int p) { return rnd(0, 99) < p; }

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
static aircraft_t PLAYER;
static entities_t E;
static weapons_t W;
static radar_t RD;
static flight_tables_t tables;
static world_t *WORLD;
static flight_env_t env;
static ai_t AITAB;
static templates_t TPL;
static int have_tab, have_tpl;

static int build_env(void) {
    char err[160];
    if (flight_tables_load(&tables, &loader, err, sizeof err) != 0) { printf("test_weapons_diff: skipped (%s)\n", err); return -1; }
    world_t *w = world_load(&loader, err, sizeof err);
    if (!w) { printf("test_weapons_diff: skipped (%s)\n", err); return -1; }
    memset(&env, 0, sizeof env);
    env.tab = &tables;
    env.landmark_z = 1; env.no_rearm = 1; env.no_sound_system = 1; env.no_liftoff_hook = 1;
    env.contact = weapons_contact; env.contact_user = &W;
    env.follow = weapons_follow; env.follow_user = &W;
    flight_set_env(&env);
    WORLD = w;
    have_tab = assets_load_ai(&loader, &AITAB, err, sizeof err) == 0;
    if (!have_tab) { printf("test_weapons_diff: skipped (%s)\n", err); return -1; }
    have_tpl = assets_load_templates(&loader, &TPL, err, sizeof err) == 0;
    return 0;
}

/* ---- the records ------------------------------------------------------------------------------------------------------------------------------- */

typedef struct { const char *name; uint32_t off; int size; int32_t val; } field_t;
static field_t fl[160];
static int nfl;
static void F(const char *name, uint32_t off, int size, int32_t val) { field_t *f = &fl[nfl++]; f->name = name; f->off = off; f->size = size; f->val = val; }

static void record_fields(const entity_t *r, const aircraft_t *a) {
    const flight_state_t *f = &a->f;
    nfl = 0;
    F("W0", 0x00, 2, f->w0); F("W2", 0x02, 2, f->w2); F("B4", 0x04, 1, f->b4); F("state+05", 0x05, 1, f->state);
    F("cell_x+06", 0x06, 2, f->cell_x); F("cell_z+08", 0x08, 2, f->cell_z); F("cell+0A", 0x0A, 1, f->cell_a); F("cell+0B", 0x0B, 1, f->cell_b);
    F("fine_x+0C", 0x0C, 2, f->fine_x); F("fine_z+0E", 0x0E, 2, f->fine_z); F("alt8+10", 0x10, 4, f->alt8);
    F("x+14", 0x14, 4, a->pos[0]); F("y+18", 0x18, 4, a->pos[1]); F("z+1C", 0x1C, 4, a->pos[2]);
    F("B20", 0x20, 1, f->b20); F("B21+21", 0x21, 1, f->b21);
    F("roll_sin+22", 0x22, 2, f->roll_sin); F("roll_cos+24", 0x24, 2, f->roll_cos); F("ground+26", 0x26, 2, f->ground);
    F("cmd_pitch+28", 0x28, 1, (uint8_t)f->cmd_pitch); F("cmd_rudder+29", 0x29, 1, (uint8_t)f->cmd_rudder);
    F("cmd_roll+2A", 0x2A, 1, (uint8_t)f->cmd_roll); F("T+2B", 0x2B, 1, (uint8_t)f->throttle);
    F("repeat+39", 0x39, 1, f->repeat);
    F("vel_x+3E", 0x3E, 4, f->vel[0]); F("vel_y+42", 0x42, 4, f->vel[1]); F("vel_z+46", 0x46, 4, f->vel[2]);
    F("counter+4C", 0x4C, 2, f->counter); F("deck+4E", 0x4E, 2, f->deck);
    F("turn+50", 0x50, 2, f->turn50); F("turn+52", 0x52, 2, f->turn52); F("turn+54", 0x54, 2, f->turn54);
    F("E+56", 0x56, 2, f->rate_p); F("Y+58", 0x58, 2, f->rate_h); F("Q+5A", 0x5A, 2, f->rate_r);
    F("keys+65", 0x65, 1, f->keys);
    F("pitch+66", 0x66, 2, (int16_t)f->pitch); F("heading+68", 0x68, 2, (int16_t)f->heading); F("roll+6A", 0x6A, 2, (int16_t)f->roll);
    F("S+6C", 0x6C, 2, f->speed_target); F("W+6E", 0x6E, 2, f->speed);
    F("fuel+72", 0x72, 4, f->fuel); F("sink+76", 0x76, 2, f->sink); F("gust+78", 0x78, 2, f->gust);
    F("group+5E", 0x5E, 1, f->group); F("cls+62", 0x62, 1, f->cls); F("id+63", 0x63, 1, f->id);
    F("gear+7C", 0x7C, 1, f->gear); F("l7d+7D", 0x7D, 1, f->l7d); F("turn_limit+7E", 0x7E, 2, f->turn_limit);
    static char mn[18][16];
    for (int i = 0; i < 9; i++) { snprintf(mn[i], 16, "matA[%d]", i); F(mn[i], 0x80 + 2 * i, 2, f->mat_a[i]); }
    for (int i = 0; i < 9; i++) { snprintf(mn[9 + i], 16, "matB[%d]", i); F(mn[9 + i], 0x92 + 2 * i, 2, f->mat_b[i]); }
    F("dest_cx+2C", 0x2C, 2, r->dest_cx); F("dest_cz+2E", 0x2E, 2, r->dest_cz); F("dest_fx+30", 0x30, 2, r->dest_fx); F("dest_fz+32", 0x32, 2, r->dest_fz); F("dest_h+34", 0x34, 4, r->dest_h);
    F("target+38", 0x38, 1, r->target); F("range+4A", 0x4A, 2, (int16_t)r->range); F("flags+64", 0x64, 1, r->ai_flags); F("engage+7A", 0x7A, 1, r->engage);
    F("hits+3C", 0x3C, 1, r->hits); F("stock+5F", 0x5F, 1, r->stock); F("rounds+60", 0x60, 2, (int16_t)r->rounds);
}

static void put_fields(uint32_t base) {
    for (int i = 0; i < nfl; i++) {
        switch (fl[i].size) {
        case 1: put8(base + fl[i].off, fl[i].val); break;
        case 2: put16(base + fl[i].off, fl[i].val); break;
        default: put32(base + fl[i].off, fl[i].val); break;
        }
    }
}

static int verbose;
static int skip_cellb;
static int only_flags;   /* a removed record (the valid bit clear): only its flag words are compared; the rest of it is not observable */
static int compare(uint32_t base, const char *when, int s, int mask_w2prox) {
    int nbad = 0;
    for (int i = 0; i < nfl; i++) {
        if (only_flags && fl[i].off > 0x05) continue;
        if (skip_cellb && fl[i].off == 0x0B) continue;
        int32_t rv = fl[i].size == 1 ? get8(base + fl[i].off) & 0xFF : fl[i].size == 2 ? get16(base + fl[i].off) : get32(base + fl[i].off);
        int32_t pv = fl[i].val;
        if (fl[i].size == 1) pv &= 0xFF;
        if (fl[i].size == 2) pv = (int16_t)pv;
        if (mask_w2prox && !strcmp(fl[i].name, "W2")) { rv &= 0xFFFE; pv &= 0xFFFE; }
        if (rv != pv) {
            if (nbad == 0 || verbose) printf("  %s, step %d field %s: port %d (0x%X) rig %d (0x%X)\n", when, s, fl[i].name, pv, (unsigned)pv, rv, (unsigned)rv);
            nbad++;
            if (!verbose) return 1;
        }
    }
    return nbad != 0;
}

static void pos_to_cells(aircraft_t *a) {
    flight_state_t *f = &a->f;
    f->cell_x = (int16_t)(a->pos[0] >> 22); f->cell_z = (int16_t)(a->pos[2] >> 22);
    f->fine_x = (int16_t)((a->pos[0] & 0x3FFFFF) >> 8); f->fine_z = (int16_t)((a->pos[2] & 0x3FFFFF) >> 8);
    f->alt8 = a->pos[1] >> 8;
    f->cell_a = (uint8_t)(4 * (3 - (f->fine_z >> 12)) + (3 - (f->fine_x >> 12)));
}

static const char *image_path;

static int LBC[0x80];
static int post_codes[16], npost;
static void hook_post(void) { if (npost < 16) post_codes[npost++] = (int)(rig_get_reg(RIG_D0) & 0xFFFF); }
static void ph_count(void *u, int off, unsigned d) { (void)u; LBC[off & 0x7F] = (LBC[off & 0x7F] + (int)d) & 0xFFFF; }
static int ph_launch_player(void *u, int i) { (void)u; return weapons_launch_player(&W, i); }
static int ph_launch_ai(void *u, int i) { (void)u; return weapons_launch_ai(&W, i); }
static int ph_weapon(void *u, int i) { (void)u; return weapons_gate(&W, i); }

static void rig_reset_state(void) {
    if (rig_load_image(image_path, BASE) != 0) { printf("image load failed\n"); exit(1); }
    static const uint32_t patch[] = { 0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x1A0DC, 0x1A2F0, 0x1A042, 0x1A258 };
    for (unsigned i = 0; i < sizeof patch / sizeof *patch; i++) put16(patch[i], 0x4E75);
    rig_poke(FLIGHT_SPAN_B_BASE, tables.span_b, FLIGHT_SPAN_B_SIZE);
    static uint8_t zero[V_HI - V_LO];
    rig_poke(V_LO, zero, sizeof zero);
    rig_hooks_clear();
    rig_hook_set(0x29BC8, hook_post, 0); npost = 0;
    put32(0x3DACC, 0x3DAFA); put32(0x3DACC + 4, 0x3DD28); put32(0x3DACC + 8, 0x3DD94);   /* the hull table's pointers were relocated for the reference machine's load address: the rig's image is flat at 0x10000 */
    { static const uint8_t z[0x60]; rig_poke(0, z, sizeof z); }   /* the log record's base pointer V:1D024 reads 0 in the rig: the counters live at 0x3A to 0x48 */
}

static void rig_globals(int mode, int progress) {
    put8(0x4A95E, mode); put8(0x4A95F, progress); put8(0x4A846, 1); put8(0x4A84D, 1); put16(0x4A984, 0x0840);
    put32(0x4A7B2, 0x800000); put8(0x4A83C, 1); put8(0x4A847, 1); put8(0x4A844, 1); put8(0x4A83D, 1); put8(0x4A841, 1);
    put16(0x4AA78, 0xFFFF);
    put16(0x54E72, 0xFFFF);   /* no followed record (the port: 0xFF) */
}

/* ---- the missile ----------------------------------------------------------------------------------------------------------------------------- */

static void make_owner(int cls_ac) {
    memset(&PLAYER, 0, sizeof PLAYER);
    flight_state_t *f = &PLAYER.f;
    f->cls = (uint8_t)cls_ac; f->w0 = 0x0008; f->v4a83d = 1; f->v4a984 = 0x0840; f->v4a844 = 1; f->v4a847 = 1; f->v4a7b2 = 0x800000; f->v4a83c = 1;
    PLAYER.pos[0] = (64 + rnd(-1, 1)) * 4194304 + rnd(0, 4000000); PLAYER.pos[2] = (64 + rnd(-1, 1)) * 4194304 + rnd(0, 4000000); PLAYER.pos[1] = 0x300000 + rnd(-100000, 300000);
    pos_to_cells(&PLAYER);
    f->heading = (uint16_t)rnd(0, 28799);
    f->pitch = (uint16_t)(pct(50) ? rnd(0, 1500) : rnd(27300, 28799));
    f->roll = (uint16_t)(pct(50) ? 0 : (pct(50) ? rnd(0, 3000) : rnd(25000, 28799)));
    flight_set_attitude(&PLAYER, f->pitch, f->heading, f->roll);
    f->speed_target = f->speed = (int16_t)rnd(0x800, 0x2800);
    if (getenv("WEAPONS_DIFF_S0")) f->speed_target = f->speed = (int16_t)strtol(getenv("WEAPONS_DIFF_S0"), NULL, 0);
    if (!getenv("WEAPONS_DIFF_ZEROVEL")) { f->vel[0] = 4 * rnd(-500, 500); f->vel[1] = 4 * rnd(-125, 125); f->vel[2] = 4 * rnd(-500, 500); }
    f->group = 0; f->id = (uint8_t)((pct(50) ? 0x30 : 0x20) | 0x0D);
    f->gear = 0; f->l7d = 0x53; f->turn_limit = 0x1400;
    f->v4aa04 = f->cell_x; f->v4aa06 = f->cell_z;
}

static int run_missile(int cn, int steps, int all, int ecm) {
    make_owner(pct(85) ? 0x11 : 0x10);
    entities_init(&E, have_tpl ? &TPL : NULL, &AITAB, NULL);
    entities_attach_player(&E, &PLAYER);
    weapons_init(&W, &E);
    W.hook.count = ph_count; memset(LBC, 0, sizeof LBC);
    radar_reset(&RD);
    E.hook.user = NULL; E.hook.launch_player = ph_launch_player; E.hook.launch_ai = ph_launch_ai; E.hook.weapon = ph_weapon;
    E.mode = 4; E.progress = rnd(0, 3); E.flying = 1; E.sim_on = 1; E.hostile_cap = 4;
    PLAYER.f.v4a96d = (uint8_t)ecm;
    E.rec[0].stock = (uint8_t)(0x20 | rnd(1, 4) | (pct(30) ? 0x10 : 0)); E.rec[0].rounds = 500; E.rec[0].target = 0xFF;
    /* the designated target: record 7, a valid inert record the update never visits */
    aircraft_t *tg = &E.rec[7].ac;
    tg->f.w0 = pct(70) ? 0x1040 : pct(50) ? 0x1048 : 0x0040; tg->f.cls = pct(80) ? 0x12 : pct(50) ? 0x15 : 0x13;
    const int where = rnd(0, 3);   /* the target ahead of the owner at a distance (in passes of the missile's travel), off to the side, behind */
    {
        const int dist = where == 0 ? rnd(100000, 600000) : where == 1 ? rnd(1500000, 6000000) : where == 2 ? rnd(300000, 2000000) : rnd(3000000, 20000000);
        int32_t dir[3] = { PLAYER.f.mat_b[2], PLAYER.f.mat_b[5], PLAYER.f.mat_b[8] };
        for (int k = 0; k < 3; k++) tg->pos[k] = PLAYER.pos[k] + (int32_t)(((int64_t)dir[k] * dist) >> 14);
        if (where == 2) { tg->pos[0] += rnd(-1500000, 1500000); tg->pos[2] += rnd(-1500000, 1500000); tg->pos[1] += rnd(-300000, 300000); }
        if (tg->pos[1] < 0x40000) tg->pos[1] = 0x40000;
    }
    pos_to_cells(tg);
    for (int i = 0; i < 9; i++) tg->f.mat_a[i] = tg->f.mat_b[i] = (i % 4 == 0) ? 16384 : 0;
    tg->f.speed_target = tg->f.speed = (int16_t)rnd(0, 3000);
    tg->f.group = 7;
    E.designated = pct(80) ? 7 : -1;
    const int redesig = pct(20) ? rnd(20, 100) : 0;   /* re-designate (drop) at this pass */

    rig_reset_state();
    rig_globals(4, E.progress);
    put8(0x4A96D, ecm);
    put16(0x4AA04, PLAYER.f.cell_x); put16(0x4AA06, PLAYER.f.cell_z);
    PLAYER.f.v4aa04 = PLAYER.f.cell_x; PLAYER.f.v4aa06 = PLAYER.f.cell_z;
    put16(0x4AA78, E.designated < 0 ? 0xFFFF : E.designated * 0x200);
    record_fields(&E.rec[0], &PLAYER); put_fields(RECA(0));
    record_fields(&E.rec[7], tg); put_fields(RECA(7));
    int bad = 0, s;
    for (s = 1; s <= steps && !bad; s++) {
        for (int k = RIG_D0; k <= RIG_A6; k++) rig_set_reg((rig_reg_t)k, 0);
        if (s == 1) { E.req_player_launch = 1; put8(0x4A872, 1); }
        if (redesig && s == redesig) { E.designated = -1; put16(0x4AA78, 0xFFFF); }
        flight_set_env(&env);
        E.pass = (uint32_t)s; put16(0x4A992, s);
        PLAYER.f.v4a992 = (uint32_t)s;
        { flight_inputs_t in; memset(&in, 0, sizeof in); flight_step(&PLAYER, &in); }   /* the update steps the owner too (record 0), whatever its W0; entities_step writes the range of the designated target (12.6) */
        entities_step(&E);
        if (rig_run(0x26AF8u) != 0) { printf("rig_run failed at step %d\n", s); return -1; }
        record_fields(&E.rec[1], &E.rec[1].ac);
        if (all) printf("step %3d W0 %04X/%04X cnt %d/%d S %d/%d rates %d,%d,%d / %d,%d,%d tgt %d,%d,%d / %d,%d,%d g26 %d/%d\n", s, E.rec[1].ac.f.w0, get16(RECA(1)) & 0xFFFF, E.rec[1].ac.f.counter, get16(RECA(1) + 0x4C), E.rec[1].ac.f.speed_target, get16(RECA(1) + 0x6C),
                        E.rec[1].ac.f.rate_p, E.rec[1].ac.f.rate_h, E.rec[1].ac.f.rate_r, get16(RECA(1) + 0x56), get16(RECA(1) + 0x58), get16(RECA(1) + 0x5A),
                        E.rec[1].ac.f.turn50, E.rec[1].ac.f.turn52, E.rec[1].ac.f.turn54, get16(RECA(1) + 0x50), get16(RECA(1) + 0x52), get16(RECA(1) + 0x54), E.rec[1].ac.f.ground, get16(RECA(1) + 0x26));
        if (getenv("WEAPONS_DIFF_LOGDBG") && s == 1) { printf("      log base long %08X bytes:", (unsigned)get32(0x1D024)); for (int q = 0; q < 16; q++) printf(" %02X", get8(0x1D024 + q)); printf("\n"); for (int q = 0x38; q < 0x4A; q++) printf(" %02X", get8(0x1D024 + q)); printf("\n  low memory:"); for (int q = 0x30; q < 0x50; q++) printf(" %02X", get8(q)); printf("\n"); }
        if (getenv("WEAPONS_DIFF_VELDBG") && s == 1)
            printf("      owner vel %d,%d,%d S0 %d nose %d,%d,%d | rig vel %d,%d,%d port vel %d,%d,%d | W0 %04X\n", PLAYER.f.vel[0], PLAYER.f.vel[1], PLAYER.f.vel[2], PLAYER.f.speed, PLAYER.f.mat_b[2], PLAYER.f.mat_b[5], PLAYER.f.mat_b[8],
                   get32(RECA(1) + 0x3E), get32(RECA(1) + 0x42), get32(RECA(1) + 0x46), E.rec[1].ac.f.vel[0], E.rec[1].ac.f.vel[1], E.rec[1].ac.f.vel[2], get16(RECA(1)) & 0xFFFF);
        if (getenv("WEAPONS_DIFF_HP") && s == 1) {
            const int32_t o_rig[3] = { get32(RECA(1) + 0x14) - PLAYER.pos[0], get32(RECA(1) + 0x18) - PLAYER.pos[1], get32(RECA(1) + 0x1C) - PLAYER.pos[2] };
            const int32_t o_port[3] = { E.rec[1].ac.pos[0] - PLAYER.pos[0], E.rec[1].ac.pos[1] - PLAYER.pos[1], E.rec[1].ac.pos[2] - PLAYER.pos[2] };
            printf("      hardpoint: rig %d,%d,%d port %d,%d,%d | stock now %02X | matB", o_rig[0], o_rig[1], o_rig[2], o_port[0], o_port[1], o_port[2], E.rec[0].stock);
            for (int i = 0; i < 9; i++) printf(" %d", PLAYER.f.mat_b[i]);
            printf("\n      owner after in rig: pitch %d head %d roll %d (before %d %d %d) matB", get16(RECA(0) + 0x66), get16(RECA(0) + 0x68), get16(RECA(0) + 0x6A), (int16_t)PLAYER.f.pitch, (int16_t)PLAYER.f.heading, (int16_t)PLAYER.f.roll);
            for (int i = 0; i < 9; i++) printf(" %d", get16(RECA(0) + 0x92 + 2 * i));
            printf(" | W0 %04X S %d W %d\n", get16(RECA(0)) & 0xFFFF, get16(RECA(0) + 0x6C), get16(RECA(0) + 0x6E));
        }
        if (all) printf("      pitch %d heading %d roll %d | rig %d %d %d\n", E.rec[1].ac.f.pitch, E.rec[1].ac.f.heading, E.rec[1].ac.f.roll, get16(RECA(1) + 0x66) & 0xFFFF, get16(RECA(1) + 0x68) & 0xFFFF, get16(RECA(1) + 0x6A) & 0xFFFF);
        if (all) printf("      flags %02X/%02X keys %02X/%02X dest %d,%d,%d,%d,%d / %d,%d,%d,%d,%d target %02X/%02X b20 %02X/%02X w2 %04X/%04X range %d/%d state %d/%d\n", E.rec[1].ai_flags, get8(RECA(1) + 0x64), E.rec[1].ac.f.keys, get8(RECA(1) + 0x65),
                       E.rec[1].dest_cx, E.rec[1].dest_cz, E.rec[1].dest_fx, E.rec[1].dest_fz, E.rec[1].dest_h, get16(RECA(1) + 0x2C), get16(RECA(1) + 0x2E), get16(RECA(1) + 0x30), get16(RECA(1) + 0x32), get32(RECA(1) + 0x34),
                       E.rec[1].target, get8(RECA(1) + 0x38), E.rec[1].ac.f.b20, get8(RECA(1) + 0x20), E.rec[1].ac.f.w2, get16(RECA(1) + 2) & 0xFFFF, (int16_t)E.rec[1].range, get16(RECA(1) + 0x4A), E.rec[1].ac.f.state, get8(RECA(1) + 5));
        if (all) printf("      vel port %d,%d,%d rig %d,%d,%d | S %d W %d\n", E.rec[1].ac.f.vel[0], E.rec[1].ac.f.vel[1], E.rec[1].ac.f.vel[2], get32(RECA(1) + 0x3E), get32(RECA(1) + 0x42), get32(RECA(1) + 0x46), get16(RECA(1) + 0x6C), get16(RECA(1) + 0x6E));
        only_flags = !(get16(RECA(1)) & 0x40) && !(E.rec[1].ac.f.w0 & 0x40);
        if (compare(RECA(1), "missile", s, 1)) bad = 1;
        only_flags = 0;
        if (!bad) {   /* the target's marks (10.3), the followed record V:54E72 and the log counters (13.5) */
            record_fields(&E.rec[7], &E.rec[7].ac);
            if (compare(RECA(7), "target", s, 1)) bad = 1;
            if (!bad && E.trail != (get16(0x54E72) < 0 ? 0xFF : (get16(0x54E72) >> 9))) { printf("  step %d trail record: port %d rig %04X\n", s, E.trail, get16(0x54E72) & 0xFFFF); bad = 1; }
            for (int off = 0x3A; off <= 0x48 && !bad; off += 2) if (LBC[off] != (get16(off) & 0xFFFF)) { printf("  step %d log counter +0x%02X: port %d rig %d (owner id %02X, missile class %d, port counters 3E %d 42 %d rig 3E %d 42 %d)\n", s, off, LBC[off], get16(off) & 0xFFFF, PLAYER.f.id, E.rec[1].ac.f.cls, LBC[0x3E], LBC[0x42], get16(0x3E), get16(0x42)); bad = 1; }
        }
        if (!bad && get8(0x4A910) != E.request) { /* the display request: informational only */ }
        E.request = 0;
        put8(0x4A910, 0);
        if (!(E.rec[1].ac.f.w0 & 0x40) && !(get16(RECA(1)) & 0x40) && s > 3) { s++; break; }
    }
    printf("test_weapons_diff: missile case %d (%s, where %d, ecm %d, stock %02X): %d of %d steps agree%s\n", cn, PLAYER.f.cls == 0x10 ? "F-16" : "F/A-18", where, ecm, E.rec[0].stock, bad ? s - 1 : s - 1, steps, bad ? " (diverged)" : "");
    return bad ? 1 : 0;
}

/* ---- a hostile missile at the player: the AI launch (record 5 from record 4 on the script's request), the destination update with the decoy branch, the lost mark, the ECM divisor,
 * the contact scan of the player's own step and the damage handler (10.2 to 10.4) ----------------------------------------------------------------------------------------------- */

#include <math.h>
static void set_pool_slot(int k, const wp_tracer_t *s) {   /* the tracer pool 0x4AD2A, slot k (64 bytes) */
    const uint32_t b = 0x4AD2Au + 64u * (uint32_t)k;
    for (int i = 0; i < 3; i++) { put32(b + 4 * i, s->pos[i]); put32(b + 0x0C + 4 * i, s->step[i]); put32(b + 0x18 + 4 * i, s->sub[i]); }
    put16(b + 0x24, s->latch); put16(b + 0x26, s->flags); put16(b + 0x28, s->life); put16(b + 0x2E, s->owner); put16(b + 0x30, s->cell_x); put16(b + 0x32, s->cell_z);
}

static int hostile_refusals;
static int run_hostile(int cn, int steps, int all) {
    memset(&PLAYER, 0, sizeof PLAYER);
    flight_state_t *pf = &PLAYER.f;
    pf->cls = 0x11; pf->w0 = 0x11C8; pf->v4a83d = 0; pf->v4a984 = 0x0840; pf->v4a844 = 1; pf->v4a847 = 1; pf->v4a7b2 = 0x800000; pf->v4a83c = 1;
    PLAYER.pos[0] = (64 + rnd(-1, 1)) * 4194304 + rnd(0, 4000000); PLAYER.pos[2] = (64 + rnd(-1, 1)) * 4194304 + rnd(0, 4000000); PLAYER.pos[1] = 0x300000 + rnd(-100000, 300000);
    pos_to_cells(&PLAYER);
    pf->heading = (uint16_t)rnd(0, 28799);
    flight_set_attitude(&PLAYER, 0, pf->heading, 0);
    pf->speed_target = pf->speed = (int16_t)rnd(0x1200, 0x2200);
    pf->group = 0; pf->id = 0x1D; pf->gear = 0xE0; pf->l7d = 0x53; pf->turn_limit = 0x1400; pf->throttle = 70;
    pf->v4aa04 = pf->cell_x; pf->v4aa06 = pf->cell_z; pf->fuel = 0x61A800;
    const int ecm = pct(30);
    pf->v4a96d = (uint8_t)ecm;
    entities_init(&E, have_tpl ? &TPL : NULL, &AITAB, NULL);
    entities_attach_player(&E, &PLAYER);
    weapons_init(&W, &E);
    W.hook.count = ph_count; memset(LBC, 0, sizeof LBC);
    radar_reset(&RD);
    E.hook.user = NULL; E.hook.launch_player = ph_launch_player; E.hook.launch_ai = ph_launch_ai; E.hook.weapon = ph_weapon;
    E.mode = 4; E.progress = rnd(0, 3); E.flying = 1; E.sim_on = 1; E.hostile_cap = 4;
    E.rec[0].stock = 0x24; E.rec[0].rounds = 500; E.rec[0].target = 0xFF;
    /* the hostile in record 4: behind the player, aimed at it */
    aircraft_t *h = &E.rec[4].ac;
    const int32_t dist = rnd(300000, 2600000);
    const double yaw = pf->heading * 3.14159265358979 * 2.0 / 28800.0;
    h->pos[0] = PLAYER.pos[0] + (int32_t)(sin(yaw) * dist) + rnd(-80000, 80000);
    h->pos[2] = PLAYER.pos[2] - (int32_t)(cos(yaw) * dist) + rnd(-80000, 80000);
    h->pos[1] = PLAYER.pos[1] + rnd(-60000, 60000);
    pos_to_cells(h);
    h->f.w0 = 0x0040; h->f.cls = 0x12; h->f.group = 4; h->f.gear = 0xE0; h->f.l7d = 0x53; h->f.turn_limit = 0x1400; h->f.v4aa04 = h->f.cell_x; h->f.v4aa06 = h->f.cell_z;
    {
        const double dx = PLAYER.pos[0] - h->pos[0], dz = PLAYER.pos[2] - h->pos[2];
        const double hd = atan2(-dx, dz) * 28800.0 / (3.14159265358979 * 2.0);
        const int hw = (int)((hd < 0 ? hd + 28800.0 : hd) + 0.5) % 28800;
        const double dy = PLAYER.pos[1] - h->pos[1], dh = sqrt(dx * dx + dz * dz);
        const int pw = (int)(atan2(-dy, dh) * 28800.0 / (3.14159265358979 * 2.0));
        flight_set_attitude(h, (uint16_t)(pw < 0 ? pw + 28800 : pw), (uint16_t)hw, 0);
    }
    h->f.speed_target = h->f.speed = (int16_t)rnd(0x900, 0x2000);
    h->f.id = (uint8_t)((pct(50) ? 0x30 : 0x20) | 0x0D);
    E.rec[4].stock = 0x44; E.rec[4].target = pct(85) ? 0x80 : 0x87; E.rec[4].engage = 5; E.rec[4].range = 0x7FFF; E.rec[4].dest_cx = -1;
    E.designated = -1;
    /* an optional decoy in the pool, near the player's track */
    wp_tracer_t decoy; memset(&decoy, 0, sizeof decoy);
    const int use_decoy = pct(50), dkind = pct(50) ? WPF_FLARE : WPF_CHAFF;
    if (use_decoy) {
        const int32_t px = PLAYER.pos[0] + rnd(-50000, 50000), pz = PLAYER.pos[2] + rnd(-50000, 50000);
        decoy.cell_x = (int16_t)(px >> 22); decoy.cell_z = (int16_t)(pz >> 22);
        decoy.pos[0] = px & 0x3FFFFF; decoy.pos[2] = pz & 0x3FFFFF; decoy.pos[1] = PLAYER.pos[1] + rnd(-40000, 40000);
        decoy.flags = (uint16_t)(0x401 | dkind); decoy.life = 100; decoy.latch = 1;
    }

    rig_reset_state();
    rig_globals(4, E.progress);
    put8(0x4A83D, 0); put8(0x4A96D, ecm); put8(0x4A903, 0);
    put16(0x4AA04, PLAYER.f.cell_x); put16(0x4AA06, PLAYER.f.cell_z);
    if (use_decoy) {
        W.slot[2] = decoy;
        if (dkind == WPF_FLARE) W.flare_ref = 3; else W.chaff_ref = 3;
        set_pool_slot(2, &decoy);
        put8(dkind == WPF_FLARE ? 0x4A944 : 0x4A945, 3);
    }
    record_fields(&E.rec[0], &PLAYER); put_fields(RECA(0));
    record_fields(&E.rec[4], h); put_fields(RECA(4));
    put8(RECA(0) + 0x71, 0);
    /* the launch pass varies (the frame counter starts anywhere, the request repeats for eight passes while the record is empty), so the AI rate gate (7.6: the counter AND the mask must be 0) refuses on
     * most passes and the refusals are compared; the flare and chaff cool-downs and the immunity byte start anywhere, so the damage handler's gates (10.4) run */
    const int launch_s = pct(40) ? 1 : (int)rnd(2, 12);
    const uint32_t pass0 = pct(30) ? 0u : (uint32_t)rnd(0, 255);
    { const int c6 = pct(35) ? (int)rnd(0, 35) : 0, c7 = pct(35) ? (int)rnd(0, 35) : 0, imm = pct(8); PLAYER.f.v4a906 = (uint8_t)c6; PLAYER.f.v4a907 = (uint8_t)c7; PLAYER.f.v4a952 = (uint8_t)imm; put8(0x4A906, c6); put8(0x4A907, c7); put8(0x4A952, imm); }
    int ever = 0, refused = 0;
    int bad = 0, s, hit_pass = 0;
    for (s = 1; s <= steps && !bad; s++) {
        for (int k = RIG_D0; k <= RIG_A6; k++) rig_set_reg((rig_reg_t)k, 0);
        if (s >= launch_s && s < launch_s + 8 && !ever) { E.req_ai_slot4 = 1; put8(0x4A873, 1); }
        flight_set_env(&env);
        E.pass = pass0 + (uint32_t)(s - 1); put16(0x4A992, pass0 + (uint32_t)(s - 1));
        PLAYER.f.v4a992 = pass0 + (uint32_t)(s - 1);
        weapons_world_start(&W, 0);
        const uint16_t prev98 = PLAYER.f.v4ab98;
        npost = 0;
        entities_head_a(&E); entities_head_b(&E); entities_prestep(&E);   /* the head of the update (4.8) and the player's range pre-step before the player's step: W2 bit 0 is cleared before the player's scan marks the records near it */
        { flight_inputs_t in; memset(&in, 0, sizeof in); flight_step(&PLAYER, &in); }
        entities_step(&E);
        /* the cool-downs are the world update's: the rig decrements V:4A906/7 itself */
        if (rig_run(0x26AF8u) != 0) { printf("rig_run failed at step %d\n", s); return -1; }
        for (int i = 1; i < ENT_RECORDS; i++) { flight_state_t *f = &entities_ac(&E, i)->f; f->ev_lost = 0; f->ev_jolt = f->ev_liftoff = f->ev_touchdown = f->ev_crash = 0; }
        if (E.request) E.request = 0;
        put8(0x4A910, 0);
        record_fields(&E.rec[5], &E.rec[5].ac);
        only_flags = !(get16(RECA(5)) & 0x40) && !(E.rec[5].ac.f.w0 & 0x40);
        if (compare(RECA(5), "hostile missile", s, 1)) bad = 1;
        only_flags = 0;
        if (!bad) {
            record_fields(&E.rec[0], &PLAYER);
            if (compare(RECA(0), "player", s, 1)) bad = 1;
        }
        if (!bad) {
            const struct { const char *n; uint32_t a; int sz; int v; } G[] = {
                { "V:4A878 jolt", 0x4A878, 1, PLAYER.f.v4a878 }, { "V:4A984", 0x4A984, 2, PLAYER.f.v4a984 }, { "V:4A957", 0x4A957, 1, 0 },
            };
            for (unsigned g = 0; g < sizeof G / sizeof *G && !bad; g++) {
                if (G[g].a == 0x4A957) continue;
                int rv = G[g].sz == 1 ? get8(G[g].a) : get16(G[g].a) & 0xFFFF;
                int pv = G[g].sz == 1 ? (G[g].v & 0xFF) : (G[g].v & 0xFFFF);
                if (G[g].a == 0x4A984) { rv &= ~1; pv &= ~1; }   /* the post routine is a logger in the rig: the latch bit is its work */
                if (rv != pv) { printf("  step %d %s: port %04X rig %04X\n", s, G[g].n, pv, rv); bad = 1; }
            }
            for (int off = 0x3A; off <= 0x48 && !bad; off += 2) if (LBC[off] != (get16(off) & 0xFFFF)) { printf("  step %d log counter +0x%02X: port %d rig %d\n", s, off, LBC[off], get16(off) & 0xFFFF); bad = 1; }
            if (!bad) {   /* the code the pass posted (the post routine is a logger in the rig) */
                const int rv = npost ? post_codes[npost - 1] : 0, pv = PLAYER.f.v4ab98 != prev98 ? PLAYER.f.v4ab98 : 0;
                if (rv != pv) { printf("  step %d post: port %04X rig %04X (%d posts)\n", s, pv, rv, npost); bad = 1; }
            }
        }
        if (!hit_pass && (PLAYER.f.w0 & 0x8000)) hit_pass = s;
        if (all) printf("step %3d missile W0 %04X/%04X cnt %d/%d target %02X dest %d,%d lost %d/%d | player W0 %04X/%04X B20 %02X/%02X post %04X/%04X\n", s, E.rec[5].ac.f.w0, get16(RECA(5)) & 0xFFFF, E.rec[5].ac.f.counter, get16(RECA(5) + 0x4C), E.rec[5].target,
                        E.rec[5].dest_cx, E.rec[5].dest_cz, (E.rec[5].ac.f.b20 >> 5) & 1, (get8(RECA(5) + 0x20) >> 5) & 1, PLAYER.f.w0, get16(RECA(0)) & 0xFFFF, PLAYER.f.b20, get8(RECA(0) + 0x20), PLAYER.f.v4ab98, get16(0x4AB98) & 0xFFFF);
        if (E.rec[5].ac.f.w0 & 0x40) ever = 1;
        else if (!ever && s >= launch_s && s < launch_s + 8) refused++;
        if (!(E.rec[5].ac.f.w0 & 0x40) && !(get16(RECA(5)) & 0x40) && ((ever && s > 3) || s >= launch_s + 9)) { s++; break; }
        (void)cn;
    }
    hostile_refusals += refused;
    printf("test_weapons_diff: hostile case %d (decoy %s, ecm %d, id %02X, launch pass %d, counter %u, %d refused): %d of %d steps agree%s%s\n", cn, use_decoy ? (dkind == WPF_FLARE ? "flare" : "chaff") : "none", ecm, h->f.id, launch_s, pass0, refused, bad ? s - 1 : s - 1, steps,
           hit_pass ? " (the player was hit)" : "", bad ? " (diverged)" : "");
    return bad ? 1 : 0;
}

/* ---- the gun routine 0x172DC alone (7.3 to 7.5, 9.2, 9.3): random pools, requests, stores and attitudes, several calls in a row ---------------------------------------------- */

static int compare_pool(int call, int cn) {
    for (int k = 0; k < WP_SLOTS; k += 2) {
        const wp_tracer_t *s = &W.slot[k];
        const uint32_t b = 0x4AD2Au + 64u * (uint32_t)k;
        struct { const char *n; uint32_t off; int sz; int32_t v; } F2[] = {
            { "flags", 0x26, 2, s->flags }, { "life", 0x28, 2, s->life }, { "x", 0, 4, s->pos[0] }, { "y", 4, 4, s->pos[1] }, { "z", 8, 4, s->pos[2] }, { "step x", 0x0C, 4, s->step[0] }, { "step y", 0x10, 4, s->step[1] }, { "step z", 0x14, 4, s->step[2] },
            { "sub x", 0x18, 4, s->sub[0] }, { "sub y", 0x1C, 4, s->sub[1] }, { "sub z", 0x20, 4, s->sub[2] }, { "latch", 0x24, 2, s->latch },
            { "owner", 0x2E, 2, s->owner }, { "cell x", 0x30, 2, s->cell_x }, { "cell z", 0x32, 2, s->cell_z } };
        for (unsigned i = 0; i < sizeof F2 / sizeof *F2; i++) {
            const int32_t rv = F2[i].sz == 4 ? get32(b + F2[i].off) : get16(b + F2[i].off);
            int32_t pv = F2[i].v;
            if (F2[i].sz == 2) pv = (int16_t)pv;
            /* an inactive slot keeps stale contents the original's creation overwrites: only the life word and the flags of such a slot are compared */
            if (!(s->flags & WPF_ACTIVE) && F2[i].off != 0x26 && F2[i].off != 0x28) continue;
            if (rv != pv) { printf("  gun case %d call %d slot %d %s: port %d (0x%X) rig %d (0x%X)\n", cn, call, k, F2[i].n, pv, (unsigned)pv, rv, (unsigned)rv); return 1; }
        }
    }
    return 0;
}

static int run_gun(int cn, int calls) {
    memset(&PLAYER, 0, sizeof PLAYER);
    flight_state_t *f = &PLAYER.f;
    f->cls = pct(80) ? 0x11 : 0x10; f->w0 = 0x11C8; f->v4a83d = (uint8_t)pct(30); f->v4a984 = 0x0840; f->v4a844 = 1; f->v4a847 = 1; f->v4a83c = 1;
    PLAYER.pos[0] = (62 + rnd(0, 6)) * 4194304 + rnd(0, 4194303); PLAYER.pos[2] = (62 + rnd(0, 6)) * 4194304 + rnd(0, 4194303); PLAYER.pos[1] = pct(15) ? rnd(300, 4000) : 0x300000 + rnd(-100000, 600000);
    pos_to_cells(&PLAYER);
    f->heading = (uint16_t)rnd(0, 28799);
    f->pitch = (uint16_t)(pct(50) ? rnd(0, 3000) : rnd(25000, 28799));
    f->roll = (uint16_t)(pct(40) ? 0 : rnd(0, 28799));
    flight_set_attitude(&PLAYER, f->pitch, f->heading, f->roll);
    f->speed_target = f->speed = (int16_t)rnd(0, 0x3000);
    f->vel[0] = 4 * rnd(-2000, 2000); f->vel[1] = 4 * rnd(-500, 500); f->vel[2] = 4 * rnd(-2000, 2000);
    f->l7d = (uint8_t)((rnd(0, 5) << 4) | 3);
    f->id = 0x1D;
    entities_init(&E, have_tpl ? &TPL : NULL, &AITAB, NULL);
    entities_attach_player(&E, &PLAYER);
    weapons_init(&W, &E);
    W.hook.count = ph_count; memset(LBC, 0, sizeof LBC);
    E.rec[0].rounds = (uint16_t)(pct(60) ? 500 : pct(50) ? rnd(0, 12) : rnd(0, 500));
    W.reload = (int8_t)(pct(50) ? 0 : rnd(-2, 22));
    W.trigger = (uint8_t)pct(60);
    W.cm_req = (uint8_t)(pct(60) ? 0 : rnd(1, 2));
    W.ext_view = f->v4a83d; W.snd_off = 0; W.gun_sound = 1;
    const int paused = pct(10);
    f->v4a866 = (uint8_t)paused;
    /* a pool with a few active slots */
    for (int k = 0; k < WP_SLOTS; k += 2) {
        wp_tracer_t *s = &W.slot[k];
        if (!pct(35)) continue;
        const int kind = rnd(0, 2);
        s->flags = (uint16_t)(0x401 | (kind == 1 ? WPF_FLARE : kind == 2 ? WPF_CHAFF : 0));
        if (pct(15)) s->flags = (uint16_t)((s->flags & ~0x400) | 0x2);   /* stopped */
        s->life = (int16_t)rnd(1, 30);
        s->cell_x = f->cell_x; s->cell_z = f->cell_z;
        s->pos[0] = rnd(0, 0x3FFFFF); s->pos[2] = rnd(0, 0x3FFFFF); s->pos[1] = pct(15) ? rnd(-3000, 6000) : PLAYER.pos[1] + rnd(-100000, 100000);
        for (int a = 0; a < 3; a++) { s->step[a] = rnd(-60000, 60000); s->sub[a] = s->step[a] / 8; }
        s->latch = (uint16_t)rnd(0, 1);
        if (pct(20)) { s->cell_x = (int16_t)rnd(0, 127); s->pos[0] = pct(50) ? rnd(0x3F0000, 0x3FFFFF) : rnd(0, 0x10000); s->step[0] = pct(50) ? 60000 : -60000; }
        if (kind == 1 && pct(50)) W.flare_ref = (uint8_t)(k + 1);
        if (kind == 2 && pct(50)) W.chaff_ref = (uint8_t)(k + 1);
    }

    rig_reset_state();
    rig_globals(4, 0);
    { static const uint32_t patch[] = { 0x31384, 0x3140C, 0x31478 }; for (unsigned i = 0; i < 3; i++) put16(patch[i], 0x4E75); }   /* the pixel drawers */
    put8(0x4A83D, f->v4a83d); put8(0x4A866, paused); put8(0x4AC13, 0x20 | 0x04);
    put16(0x4A97E, W.trigger ? 8 : 0); put8(0x4A943, W.cm_req); put8(0x4A944, W.flare_ref); put8(0x4A945, W.chaff_ref);
    record_fields(&E.rec[0], &PLAYER); put_fields(RECA(0));
    put8(RECA(0) + 0x3B, W.reload); put16(RECA(0) + 0x60, (int16_t)E.rec[0].rounds);
    for (int k = 0; k < WP_SLOTS; k += 2) set_pool_slot(k, &W.slot[k]);
    if (getenv("WEAPONS_DIFF_GUNDBG") && atoi(getenv("WEAPONS_DIFF_GUNDBG")) == cn) {
        printf("  gun case %d initial: trigger %d cm %d reload %d res %d rounds %d paused %d ext %d refs %d %d\n", cn, W.trigger, W.cm_req, W.reload, PLAYER.f.l7d >> 4, E.rec[0].rounds, paused, f->v4a83d, W.flare_ref, W.chaff_ref);
        for (int k = 0; k < WP_SLOTS; k += 2) printf("    slot %d flags %04X life %d latch %d pos y %d step %d,%d,%d\n", k, W.slot[k].flags, W.slot[k].life, W.slot[k].latch, W.slot[k].pos[1], W.slot[k].step[0], W.slot[k].step[1], W.slot[k].step[2]);
    }
    int bad = 0;
    for (int c = 1; c <= calls && !bad; c++) {
        const uint32_t n = (uint32_t)rnd(0, 255);
        put16(0x4A992, n); PLAYER.f.v4a992 = n;
        for (int k = RIG_D0; k <= RIG_A6; k++) rig_set_reg((rig_reg_t)k, 0);
        if (getenv("WEAPONS_DIFF_GUNREG")) { const uint32_t rg = (uint32_t)strtoul(getenv("WEAPONS_DIFF_GUNREG"), NULL, 0); rig_set_reg(RIG_A0, RECA(0)); if (rg & 1) rig_set_reg(RIG_A1, RECA(0)); if (rg & 2) rig_set_reg(RIG_A5, RECA(0)); if (rg & 4) rig_set_reg(RIG_A4, RECA(0)); if (rg & 8) rig_set_reg(RIG_A6, RECA(0)); }
        weapons_gun(&W, n, paused);
        if (rig_run(0x172DCu) != 0) { printf("rig_run failed in call %d\n", c); return -1; }
        if (getenv("WEAPONS_DIFF_GUNDBG") && atoi(getenv("WEAPONS_DIFF_GUNDBG")) == cn) {
            printf("  after call %d: reload port %d rig %d, rounds port %d rig %d, res port %d rig %d, cm port %d rig %d\n", c, W.reload, (int8_t)get8(RECA(0) + 0x3B), (int16_t)E.rec[0].rounds, get16(RECA(0) + 0x60), PLAYER.f.l7d >> 4, get8(RECA(0) + 0x7D) >> 4, W.cm_req, get8(0x4A943));
            for (int k = 0; k < WP_SLOTS; k += 2) printf("    slot %d flags port %04X rig %04X life port %d rig %d\n", k, W.slot[k].flags, get16(0x4AD2A + 64 * k + 0x26) & 0xFFFF, W.slot[k].life, get16(0x4AD2A + 64 * k + 0x28));
        }
        if (compare_pool(c, cn)) bad = 1;
        const struct { const char *n; int rv, pv; } S2[] = {
            { "reload E+0x3B", (int8_t)get8(RECA(0) + 0x3B), W.reload }, { "l7d E+0x7D", get8(RECA(0) + 0x7D), PLAYER.f.l7d }, { "rounds E+0x60", get16(RECA(0) + 0x60), (int16_t)E.rec[0].rounds },
            { "flare ref", get8(0x4A944), W.flare_ref }, { "chaff ref", get8(0x4A945), W.chaff_ref }, { "request V:4A943", get8(0x4A943), W.cm_req }, { "cannon counter +0x3A", get16(0x3A) & 0xFFFF, LBC[0x3A] },
            { "log dirty V:4A87D", get8(0x4A87D), PLAYER.f.v4a87d },
        };
        for (unsigned i = 0; i < sizeof S2 / sizeof *S2 && !bad; i++) if (S2[i].rv != S2[i].pv) { printf("  gun case %d call %d %s: port %d rig %d\n", cn, c, S2[i].n, S2[i].pv, S2[i].rv); bad = 1; }
        /* the next call: new requests */
        if (pct(30)) { W.trigger = (uint8_t)pct(60); put16(0x4A97E, W.trigger ? 8 : 0); }
        if (pct(25) && !W.cm_req) { W.cm_req = (uint8_t)rnd(1, 2); put8(0x4A943, W.cm_req); }
    }
    return bad ? 1 : 0;
}
static int gunhit_hits, rig_hull_calls, rig_big_calls;
static int probe_force, probe_off[3], last_rig_hit, last_port_hit;
/* ---- the gun's hit test 0x2ADDE against queue C (8.2, 8.2a): one active tracer near up to seven entity entries with real model vertices, the hull lists of the extracted ai.bin against the
 * original's table, the box, the sub-steps, the third hit, the secondary entries, the cell and valid tests, the owner skip ---------------------------------------------------------------- */

#define QC_BASE 0x150000u
static wp_qentry_t QC[48];
static int NQC;
static int hk_queue_c(void *u, wp_qentry_t *out, int max) { (void)u; int n = NQC < max ? NQC : max; for (int i = 0; i < n; i++) out[i] = QC[i]; return n; }
/* The record's vertex slots as the draw leaves them (lists.c store_vslots): the earlier contents, then slot k = (M . v_k) >> 8 of the drawn block's vertex k, M = the record's matrix >> 6. */
static int16_t SLOTSTORE[ENT_RECORDS][WP_VSLOTS][3];
static int hk_slots(void *u, int rec, int16_t (*out)[3], int n) { (void)u; memcpy(out, SLOTSTORE[rec], sizeof(int16_t) * 3 * (size_t)n); return 0; }
static int hk_ship(void *u, int rec, flight_ship_t *out) {
    (void)u;
    const aircraft_t *a = &E.rec[rec].ac;
    memset(out, 0, sizeof *out);
    out->present = 1; out->w0 = a->f.w0; out->cls = a->f.cls; out->group = a->f.group; out->l7d = a->f.l7d;
    for (int k = 0; k < 3; k++) out->pos[k] = a->pos[k];
    out->fine_x = a->f.fine_x; out->fine_z = a->f.fine_z; out->alt8 = (int16_t)a->f.alt8;
    memcpy(out->mat_b, a->f.mat_b, sizeof out->mat_b);
    memcpy(out->vtx, SLOTSTORE[rec], sizeof out->vtx);
    return 0;
}

static int hk_model_flags(void *u, unsigned model) {   /* the model's hull flag byte (+7 of the block 0x2ADDE names) as game.c wp_model_flags gives it: bit 4 marks a big hull (note 07 8.2) */
    (void)u;
    return world_model_block_flags(WORLD, (uint16_t)model);
}

static void store_block(const flight_state_t *f, unsigned model, int level, int16_t out[WP_VSLOTS][3]) {
    int mb[9];
    for (int i = 0; i < 9; i++) mb[i] = f->mat_b[i] >> 6;
    for (int k = 0; k < WP_VSLOTS; k++) {
        int32_t v[3];
        if (world_model_vertex(WORLD, model, level, (unsigned)k, v) != 0) continue;
        for (int i = 0; i < 3; i++) {
            int32_t s2 = 0;
            for (int j = 0; j < 3; j++) s2 += mb[3 * i + j] * v[j];
            out[k][i] = (int16_t)(s2 >> 8);
        }
    }
}


/* ---- landmark volumes in the queue (07 8.2 "Landmarks" and "Landmark and entity entries in one queue"): the original's pointer table 0x5283C, the volume records 0x52868 + 256 k (cells at +2/+3, the top box at +4, the chain
 * from +0x0E: five box words, the link long at +0x0A of the element, the reference points 6 bytes apart from +0x0E) and the model data the pointer names (element at +0x0A: the link to the next element's data at +0x0A,
 * the plane count word at +0x0E, the normals 12 bytes apart from +0x16). The pointers' high word is 6, so the z artefact is 1 as in env.landmark_z. ---------------------------------------------------------------- */

#define LM_PTRS 0x5283Cu
#define LM_RECS 0x52868u
#define LM_MODEL 0x60000u
typedef struct {
    int listed;
    int cx, cz;
    int16_t box[5];
    int nn;
    struct { int16_t box[5]; int count, npw; int16_t ref[6][3], nrm[6][3]; } el[4];
} lmvol_t;
static lmvol_t LMV[11];
static int LM_ENTRIES, lm_cases, lm_stopped, lm_oob_cases, lm_oob_skipped;

static int16_t sat16(int v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

static void lm_build(const wp_tracer_t *t) {
    memset(LMV, 0, sizeof LMV);
    env.n_landmarks = 0;
    if (!LM_ENTRIES) return;
    const int Xt = (int)(t->pos[0] >> 8), Yt = (int)(t->pos[1] >> 8), Zt = (int)(t->pos[2] >> 8);
    const int fr_lo = pct(50) ? 1 : 20;   /* some cases keep the margins small so that the z difference lands on the ordinals 0 to 9 */
    for (int k = 0; k < 11; k++) {
        lmvol_t *v = &LMV[k];
        v->listed = pct(75);
        if (!v->listed) continue;
        const int dx = pct(80) ? 0 : rnd(-2, 2), dz = pct(80) ? 0 : rnd(-2, 2);
        v->cx = (t->cell_x - dx) & 255; v->cz = (t->cell_z - dz) & 255;
        const int offx = dx * 16384, offz = dz * 16384 + 1;
        const int cxo = Xt + offx, czo = Zt + offz;
        v->box[0] = sat16(Yt + rnd(-30, 400));
        v->box[1] = sat16(cxo - rnd(5, 700)); v->box[2] = sat16(cxo + rnd(5, 700));
        v->box[3] = sat16(czo - rnd(5, 700)); v->box[4] = sat16(czo + rnd(5, 700));
        v->nn = rnd(1, 4);
        for (int m = 0; m < v->nn; m++) {
            const int off = (m == 0 || pct(60));
            const int cxm = off ? cxo : Xt, czm = off ? czo : Zt;
            v->el[m].box[0] = sat16(Yt + rnd(-30, 400));
            v->el[m].box[1] = sat16(cxm - rnd(5, 500)); v->el[m].box[2] = sat16(cxm + rnd(5, 500));
            v->el[m].box[3] = sat16(czm - rnd(5, 500)); v->el[m].box[4] = sat16(czm + rnd(5, 500));
            v->el[m].count = pct(10) ? 0 : rnd(1, 5);
            v->el[m].npw = v->el[m].count > 0 ? v->el[m].count : 1;   /* a count of 0 still forms the first plane (the loop tests the count after the first dot product) */
            const int friendly = pct(55);
            const int P[3] = { Xt + offx, Yt, Zt + offz };
            for (int p = 0; p < v->el[m].npw; p++)
                for (int c = 0; c < 3; c++) {
                    const int n = pct(10) ? 0 : rnd(-6, 6);
                    v->el[m].nrm[p][c] = (int16_t)n;
                    const int margin = c == 2 ? rnd(fr_lo, 250) : rnd(20, 250);
                    v->el[m].ref[p][c] = sat16(friendly && n != 0 ? P[c] + (n > 0 ? margin : -margin) : P[c] + rnd(-60, 60));
                }
        }
    }
    int j = 0;
    for (int k = 0; k < 11; k++) {
        const lmvol_t *v = &LMV[k];
        if (!v->listed) continue;
        flight_lm_slot_t *sl = &env.landmark[j++];
        memset(sl, 0, sizeof *sl);
        sl->cell_x = (uint8_t)v->cx; sl->cell_z = (uint8_t)v->cz; sl->ordinal = (uint8_t)k;
        memcpy(sl->box, v->box, sizeof sl->box);
        sl->nnodes = (uint8_t)v->nn;
        for (int m = 0; m < v->nn; m++) {
            memcpy(sl->node[m].box, v->el[m].box, sizeof sl->node[m].box);
            sl->node[m].nplanes = (uint8_t)v->el[m].count;
            for (int p = 0; p < v->el[m].npw; p++) for (int c = 0; c < 3; c++) { sl->node[m].ref[p][c] = v->el[m].ref[p][c]; sl->node[m].nrm[p][c] = v->el[m].nrm[p][c]; }
        }
    }
    env.n_landmarks = j;
}

static void lm_poke(void) {
    if (!LM_ENTRIES) return;
    for (int k = 0; k < 11; k++) {
        const lmvol_t *v = &LMV[k];
        const uint32_t rec = LM_RECS + 0x100u * (uint32_t)k, mod = LM_MODEL + 0x400u * (uint32_t)k;
        put32(LM_PTRS + 4u * (uint32_t)k, v->listed ? (int32_t)mod : -1);
        if (!v->listed) continue;
        put8(rec + 2, v->cx); put8(rec + 3, v->cz);
        for (int i = 0; i < 5; i++) put16(rec + 4 + 2u * (uint32_t)i, v->box[i]);
        for (int m = 0; m < v->nn; m++) {
            const uint32_t e = rec + 0x0Eu + 0x38u * (uint32_t)m, me = mod + 0x0Au + 0x60u * (uint32_t)m;
            for (int i = 0; i < 5; i++) put16(e + 2u * (uint32_t)i, v->el[m].box[i]);
            put32(e + 0x0A, m + 1 < v->nn ? (int32_t)(rec + 0x0Eu + 0x38u * (uint32_t)(m + 1)) : -1);
            put32(me + 0x0A, m + 1 < v->nn ? (int32_t)(mod + 0x0Au + 0x60u * (uint32_t)(m + 1)) : -1);
            put16(me + 0x0E, v->el[m].count);
            for (int p = 0; p < v->el[m].npw; p++)
                for (int c = 0; c < 3; c++) {
                    put16(e + 0x0E + 6u * (uint32_t)p + 2u * (uint32_t)c, v->el[m].ref[p][c]);
                    put16(me + 0x16 + 12u * (uint32_t)p + 2u * (uint32_t)c, v->el[m].nrm[p][c]);
                }
        }
    }
}

/* An ordinal the port met outside 0 to 10 (the NOTE-GAP of weapons.c; the exact aliases of real entries are not among them): the original reads the pointer table at that word index; the test makes that entry a negative pointer (the port's "skip"), and a case whose entry
 * would fall in the state hunk or on the table itself is dropped. Returns 1 when the case is to be dropped. */
static int lm_oob(void) {
    int drop = 0;
    for (int i = 0; i < W.dbg_lm_oob_n; i++) {
        const int k = W.dbg_lm_oob[i];
        if ((k + 16384 >= 0 && k + 16384 <= 10) || (k + 32768 >= 1 && k + 32768 <= 10)) continue;   /* 07 8.2 (C): an exact alias of a real entry, tested by the port as that entry */
        const uint32_t addr = (uint32_t)((int32_t)LM_PTRS + (int16_t)(k * 4));
        if ((addr + 4 > 0x4A6E8u && addr < 0x54EC0u) || (addr + 4 > LM_PTRS && addr < LM_PTRS + 44)) drop = 1;
        else put32(addr, -1);
    }
    return drop;
}

static void lm_dump(const wp_tracer_t *t) {
    printf("   tracer cell %d,%d pos %d,%d,%d (fine %d,%d,%d) sub %d,%d,%d\n", t->cell_x, t->cell_z, t->pos[0], t->pos[1], t->pos[2], t->pos[0] >> 8, t->pos[1] >> 8, t->pos[2] >> 8, t->sub[0], t->sub[1], t->sub[2]);
    for (int k = 0; k < 11; k++) {
        const lmvol_t *v = &LMV[k];
        if (!v->listed) continue;
        printf("   vol %d cell %d,%d top %d %d %d %d %d nn %d\n", k, v->cx, v->cz, v->box[0], v->box[1], v->box[2], v->box[3], v->box[4], v->nn);
        for (int m = 0; m < v->nn; m++) {
            printf("     el %d box %d %d %d %d %d count %d:", m, v->el[m].box[0], v->el[m].box[1], v->el[m].box[2], v->el[m].box[3], v->el[m].box[4], v->el[m].count);
            for (int p = 0; p < v->el[m].npw; p++) printf(" ref(%d,%d,%d) n(%d,%d,%d)", v->el[m].ref[p][0], v->el[m].ref[p][1], v->el[m].ref[p][2], v->el[m].nrm[p][0], v->el[m].nrm[p][1], v->el[m].nrm[p][2]);
            printf("\n");
        }
    }
}

static int run_gunhit(int cn) {
    memset(&PLAYER, 0, sizeof PLAYER);
    flight_state_t *pf = &PLAYER.f;
    pf->cls = 0x11; pf->w0 = 0x11C8; pf->v4a984 = 0x0840; pf->v4a844 = 1; pf->v4a847 = 1; pf->v4a83c = 1;
    PLAYER.pos[0] = 64 * 4194304 + rnd(0, 4194303); PLAYER.pos[2] = 64 * 4194304 + rnd(0, 4194303); PLAYER.pos[1] = 0x300000 + rnd(-100000, 100000);
    pos_to_cells(&PLAYER);
    flight_set_attitude(&PLAYER, 0, (uint16_t)rnd(0, 28799), 0);
    pf->l7d = 0x53; pf->id = 0x1D; pf->v4aa04 = pf->cell_x; pf->v4aa06 = pf->cell_z;
    entities_init(&E, have_tpl ? &TPL : NULL, &AITAB, NULL);
    entities_attach_player(&E, &PLAYER);
    weapons_init(&W, &E);
    W.hook.count = ph_count; W.hook.queue_c = hk_queue_c; W.hook.slots = hk_slots; W.hook.ship = hk_ship; W.hook.model_flags = hk_model_flags;  memset(LBC, 0, sizeof LBC);
    E.rec[0].rounds = 500;
    E.designated = -1;
    static const int models[] = { 3, 4, 5, 6, 7, 14, 15, 3, 8, 9, 12, 8, 0, 1, 10, 10 };   /* the aircraft models (block flag bit 4 clear) and the big hulls 8, 9, 12 (bit 4 set: the route by the model flag byte, the 0x800 box, then 0x2B3F0) */
    NQC = 0;
    int target = -1;
    const int probe = getenv("WEAPONS_DIFF_HULLPROBE") != NULL || (cn & 1);   /* odd cases: one record and a point the port calls inside the hull (boundary cases included) */
    const int nent = probe ? 1 : rnd(1, 8);
    int ents[8], drawn_level[8];
    for (int i = 0; i < nent; i++) {
        const int idx = 1 + (i % 15);
        ents[i] = idx;
        aircraft_t *a = &E.rec[idx].ac;
        flight_state_t *f = &a->f;
        const int model = (getenv("WEAPONS_DIFF_MODEL") ? atoi(getenv("WEAPONS_DIFF_MODEL")) : models[rnd(0, 15)]);
        f->w0 = (uint16_t)(pct(85) ? 0x1040 : pct(50) ? 0x0040 : 0x1000);
        if (pct(10)) f->w0 |= 0x0200;
        f->cls = (uint8_t)(pct(70) ? 0x12 : pct(50) ? 0x15 : 0x11); f->group = (uint8_t)idx;
        if (model == 8 || model == 9 || model == 12) f->cls = (uint8_t)(pct(55) ? 0x20 : pct(50) ? 0x21 : 0x12);   /* a ship record, another ship nibble or an aircraft class on a big-hull model */
        if (model == 0 || model == 1 || model == 10) f->cls = (uint8_t)(pct(70) ? 0x00 : pct(50) ? 0x01 : 0x12);   /* the missile models (0x18 big, 0x48 not) and template 10 (0xFC big) */
        a->pos[0] = PLAYER.pos[0] + rnd(-300000, 300000); a->pos[2] = PLAYER.pos[2] + rnd(-300000, 300000); a->pos[1] = PLAYER.pos[1] + rnd(-100000, 100000);
        if (pct(70)) { a->pos[0] = PLAYER.pos[0] + rnd(-30000, 30000); a->pos[2] = PLAYER.pos[2] + rnd(-30000, 30000); a->pos[1] = PLAYER.pos[1] + rnd(-30000, 30000); }
        pos_to_cells(a);
        flight_set_attitude(a, (uint16_t)rnd(0, 28799), (uint16_t)rnd(0, 28799), (uint16_t)rnd(0, 28799));
        {   /* the part id and the vertex slots the draw would leave: the level the picker chooses (any reachable level), its part id unfiltered; the slots the block lacks keep an earlier draw's (level 0, another attitude) */
            lists_level_t lv;
            int got = 0;
            for (int tries = 0; tries < 40 && !got; tries++) got = world_pick_level_word(WORLD, (unsigned)model, (unsigned)rnd(0, 9), (int16_t)rnd(-1, 600), &lv) == 0 && lv.found && lv.present;
            if (!got) { lv.level = 0; lv.part_id = 0; }
            f->b7b = (uint8_t)lv.part_id;
            flight_state_t old = *f;
            flight_set_attitude(&E.rec[idx].ac, (uint16_t)rnd(0, 28799), (uint16_t)rnd(0, 28799), (uint16_t)rnd(0, 28799));   /* (below the attitude is set again) */
            memset(SLOTSTORE[idx], 0, sizeof SLOTSTORE[idx]);
            store_block(f, (unsigned)model, 0, SLOTSTORE[idx]);
            *f = old;
            drawn_level[i] = lv.level;
        }
        const int sh = world_model_unit_shift(WORLD, (uint16_t)model, 0);
        f->l7d = (uint8_t)((sh < 0 ? 3 : sh) & 15);
        E.rec[idx].template_id = (uint16_t)model;
        E.rec[idx].hits = (uint8_t)(pct(40) ? rnd(0, 2) : 0);
        store_block(f, (unsigned)model, drawn_level[i], SLOTSTORE[idx]);   /* this draw's block over the earlier contents */
        QC[NQC].id = (uint8_t)idx; QC[NQC].flags = (uint8_t)(0x10 | rnd(0, 3)); NQC++;
        if (!probe && pct(25) && NQC < 15) { QC[NQC].id = (uint8_t)rnd(0, 15); QC[NQC].flags = 0x40; NQC++; }   /* a secondary object between them */
        if (target < 0 || pct(40)) target = i;
    }
    /* landmark entries between the entity entries (07 8.2: one queue, one shared register): flag bits 4 and 6 clear, at random places */
    LM_ENTRIES = 0;
    if (!probe && pct(60)) {
        const int nl = rnd(1, 14);
        for (int i = 0; i < nl && NQC < 46; i++) {
            const int at = rnd(0, NQC);
            for (int q = NQC; q > at; q--) QC[q] = QC[q - 1];
            QC[at].id = 0; QC[at].flags = (uint8_t)rnd(0, 3);
            NQC++; LM_ENTRIES++;
        }
    }
    if (pct(10) && NQC > 1) { QC[0].id = 0; }   /* the owner (record 0) in the queue */
    /* the tracer: near the target record, moving a few fine units per sub-step */
    wp_tracer_t t; memset(&t, 0, sizeof t);
    {
        const aircraft_t *a = &E.rec[ents[target]].ac;
        const int spread = pct(50) ? 25 : pct(60) ? 60 : 400;
        t.cell_x = a->f.cell_x; t.cell_z = a->f.cell_z;
        if (pct(10)) t.cell_x = (int16_t)(t.cell_x + 1);
        const int fx = a->f.fine_x + rnd(-spread, spread), fz = a->f.fine_z + rnd(-spread, spread);
        t.pos[0] = (int32_t)(((uint32_t)(fx & 0x3FFF) << 8) | (uint32_t)rnd(0, 255)); t.pos[2] = (int32_t)(((uint32_t)(fz & 0x3FFF) << 8) | (uint32_t)rnd(0, 255));
        t.pos[1] = (a->f.alt8 + rnd(-spread, spread)) * 256 + rnd(0, 255);
        for (int k2 = 0; k2 < 3; k2++) { t.sub[k2] = rnd(-5000, 5000); t.step[k2] = t.sub[k2] * 8 + (pct(20) ? rnd(-300, 300) : 0); }
        if (probe) {   /* a stationary point near the centroid of the slots: the verdict is the hull test's alone */
            int32_t cc[3] = { 0, 0, 0 };
            for (int k3 = 0; k3 < 17; k3++) for (int i3 = 0; i3 < 3; i3++) cc[i3] += SLOTSTORE[ents[target]][k3][i3];
            int shh = (a->f.l7d & 15) - (a->f.b7b >> 4); if (shh < 0) shh = 0;
            const int32_t fp[3] = { a->f.fine_x, a->f.alt8, a->f.fine_z };
            int32_t Tp[3];
            for (int i3 = 0; i3 < 3; i3++) Tp[i3] = fp[i3] + ((cc[i3] / 17) >> shh) + (probe_force ? probe_off[i3] : rnd(-90, 90));
            if (!probe_force && (int8_t)a->f.b7b >= 0) {   /* look for a point the port's hull test calls inside, then step off it by a few units: boundary cases are the interesting ones */
                for (int tries = 0; tries < 400; tries++) {
                    int32_t Tq[3];
                    for (int i3 = 0; i3 < 3; i3++) Tq[i3] = fp[i3] + ((cc[i3] / 17) >> shh) + rnd(-120, 120);
                    if (weapons_hull_inside(&AITAB, &a->f, a->f.b7b, SLOTSTORE[ents[target]], fp, Tq)) { for (int i3 = 0; i3 < 3; i3++) Tp[i3] = Tq[i3] + (pct(30) ? rnd(-3, 3) : 0); break; }
                }
            }
            t.pos[0] = (int32_t)(((uint32_t)(Tp[0] & 0x3FFF) << 8)); t.pos[1] = Tp[1] * 256; t.pos[2] = (int32_t)(((uint32_t)(Tp[2] & 0x3FFF) << 8));
            for (int k3 = 0; k3 < 3; k3++) t.sub[k3] = t.step[k3] = 0;
        }
        if (LM_ENTRIES && pct(45)) t.step[1] = (int32_t)((uint32_t)rnd(-3, 3) << 16) + rnd(-3, 11);   /* the undo of a failed chain element leaves step y in the ordinal register (07 8.2): its low word plus one is the next ordinal; here often 0 to 10 */
        t.flags = 0x401; t.life = 20; t.latch = 1; t.owner = 0;
    }
    W.slot[0] = t;
    wp_tracer_t t0 = t;
    lm_build(&t);

    rig_reset_state();
    rig_globals(4, 0);
    { static const uint32_t patch[] = { 0x31384, 0x3140C, 0x31478 }; for (unsigned i = 0; i < 3; i++) put16(patch[i], 0x4E75); }
    put8(0x4A83D, 0); put8(0x4AC13, 0x20 | 0x04); put16(0x4A97E, 0);
    put16(0x4AA5E, WP_TRACER_BOX);
    record_fields(&E.rec[0], &PLAYER); put_fields(RECA(0));
    put8(RECA(0) + 0x3B, 0); put16(RECA(0) + 0x60, 500);
    for (int i = 0; i < nent; i++) {
        const int idx = ents[i];
        record_fields(&E.rec[idx], &E.rec[idx].ac); put_fields(RECA(idx));
        for (int k2 = 0; k2 < WP_VSLOTS; k2++) for (int a2 = 0; a2 < 3; a2++) put16(RECA(idx) + 0xA4 + 6 * k2 + 2 * a2, SLOTSTORE[idx][k2][a2]);
        put8(RECA(idx) + 0x7B, E.rec[idx].ac.f.b7b);
        if (have_tpl) for (int k2 = 0; k2 < TEMPLATE_LONGS; k2++) put32(0x2600Cu + 20u * (uint32_t)idx + 4u * (uint32_t)k2, (int32_t)TPL.rec[E.rec[idx].template_id][k2]);
    }
    for (int i = 0; i < NQC; i++) { put16(QC_BASE + 24u * (uint32_t)i, (QC[i].id << 8) | QC[i].flags); if (QC[i].flags & 0x10) put32(QC_BASE + 24u * (uint32_t)i + 2, (int32_t)(0x25ECCu + 20u * E.rec[QC[i].id & 15].template_id)); }   /* +2: the descriptor pointer, whose second long is the model: the block flag byte of the route is read through it */
    put32(0x4AA7E, QC_BASE + 24u * (uint32_t)(NQC - 1)); put8(0x4A916, NQC);
    lm_poke();
    if (getenv("WEAPONS_DIFF_D5LOG")) {   /* debug: log D5 (the ordinal register) at each loop-end test 0x2B000 */
        uint16_t code[] = { 0x23CF, 0x0007, 0x0300, 0x2E79, 0x0007, 0x0304, 0x2F05, 0x23CF, 0x0007, 0x0304, 0x2E79, 0x0007, 0x0300, 0, 0, 0, 0, 0x4EF9, 0x0002, 0xB008 };
        /* the four words the jump replaces (slots 13 to 16) are read from the loaded image before it is patched: the tree holds no original code */
        for (unsigned i = 0; i < 4; i++) code[13 + i] = (uint16_t)get16(0x2B000u + 2 * i);
        for (unsigned i = 0; i < sizeof code / 2; i++) put16(0x70000u + 2 * i, code[i]);
        put32(0x70304, 0x70400);
        put16(0x2B000, 0x4EF9); put32(0x2B002, 0x70000);
    }
    set_pool_slot(0, &W.slot[0]);
    for (int k = RIG_D0; k <= RIG_A6; k++) rig_set_reg((rig_reg_t)k, 0);
    put16(0x4A992, 5); PLAYER.f.v4a992 = 5;
    weapons_gun(&W, 5, 0);
    if (LM_ENTRIES) { lm_cases++; if (W.dbg_lm_oob_n) lm_oob_cases++; if (lm_oob()) { lm_oob_skipped++; return 0; } }
    { static const uint32_t wa[] = { 0x2B4BA, 0x2B3F0, 0x2B1E2, 0x2B366, 0x2B38E, 0x2B392, 0x2B1D2, 0x2AF4A, 0x2B198 }; rig_watch_set(wa, getenv("WEAPONS_DIFF_LMTRACE") ? 9 : 2); rig_trace_clear(); }
    if (rig_run(0x172DCu) != 0) { printf("rig_run failed\n"); return -1; }
    if (getenv("WEAPONS_DIFF_D5LOG")) { const uint32_t pt = (uint32_t)get32(0x70304); printf("  case %d D5 log:", cn); for (uint32_t a = 0x70400; a > pt; a -= 4) printf(" %08X", (unsigned)get32(a - 4)); printf("\n"); }
    if (getenv("WEAPONS_DIFF_LMTRACE")) { static uint32_t tt[1024]; size_t nn = rig_trace_get(tt, 1024); printf("  case %d rig trace:", cn); for (size_t q = 0; q < nn; q++) printf(" %X", tt[q]); printf("\n"); }
    { static uint32_t tr[1024]; size_t nt = rig_trace_get(tr, 1024); rig_hull_calls = 0; rig_big_calls = 0; for (size_t q = 0; q < nt && q < 1024; q++) { if (tr[q] == 0x2B4BA) rig_hull_calls++; else rig_big_calls++; } rig_watch_set(NULL, 0); }
    if (getenv("WEAPONS_DIFF_HITDBG")) {
        const int idx = ents[target];
        printf("  rig record %d: +7B %02X +7D %02X W0 %04X cells %d,%d fine %d,%d alt8 %d V:4AA70 %04X V:4AA5E %04X tracer flags %04X\n    vtx:", idx, get8(RECA(idx) + 0x7B), get8(RECA(idx) + 0x7D), get16(RECA(idx)) & 0xFFFF, get16(RECA(idx) + 6), get16(RECA(idx) + 8), get16(RECA(idx) + 0xC), get16(RECA(idx) + 0xE), get32(RECA(idx) + 0x10), get16(0x4AA70) & 0xFFFF, get16(0x4AA5E) & 0xFFFF, get16(0x4AD2A + 0x26) & 0xFFFF);
        for (int q = 0; q < 17; q++) printf(" (%d,%d,%d)", get16(RECA(idx) + 0xA4 + 6 * q), get16(RECA(idx) + 0xA4 + 6 * q + 2), get16(RECA(idx) + 0xA4 + 6 * q + 4));
        printf("\n    tracer pos %d,%d,%d\n", get32(0x4AD2A), get32(0x4AD2A + 4), get32(0x4AD2A + 8));
    }
    if (getenv("WEAPONS_DIFF_TABLE") && W.dbg_inbox == 1) {
        for (int i = 0; i < nent; i++) {
            const int idx = ents[i];
            const flight_state_t *f2 = &E.rec[idx].ac.f;
            const int dx2 = (int16_t)(f2->fine_x - (int16_t)(t0.pos[0] >> 8)), dz2 = (int16_t)(f2->fine_z - (int16_t)(t0.pos[2] >> 8)), dy2 = f2->alt8 - (t0.pos[1] >> 8);
            if (f2->cell_x == t0.cell_x && f2->cell_z == t0.cell_z && (f2->w0 & 0x40) && abs(dx2) < 320 && abs(dz2) < 320 && abs(dy2) < 320)
                printf("  TABLE rig_calls %d record %d model %d part %02X cls %02X w0 %04X hits %02X cellmatch %d i %d of %d d %d %d %d\n", rig_hull_calls, idx, E.rec[idx].template_id, f2->b7b, f2->cls, f2->w0, E.rec[idx].hits, 1, i, nent, dx2, dy2, dz2);
        }
    }
    int bad = 0;
    if (compare_pool(1, cn)) bad = 1;
    for (int i = 0; i < nent && !bad; i++) {
        const int idx = ents[i];
        record_fields(&E.rec[idx], &E.rec[idx].ac);
        if (compare(RECA(idx), "queue record", idx, 1)) bad = 1;
    }
    if (!bad && E.trail != (get16(0x54E72) < 0 ? 0xFF : (get16(0x54E72) >> 9))) { printf("  gunhit case %d trail record: port %d rig %04X\n", cn, E.trail, get16(0x54E72) & 0xFFFF); bad = 1; }
    for (int off = 0x3A; off <= 0x48 && !bad; off += 2) if (LBC[off] != (get16(off) & 0xFFFF)) { printf("  gunhit case %d log counter +0x%02X: port %d rig %d\n", cn, off, LBC[off], get16(off) & 0xFFFF); bad = 1; }
    if (bad && getenv("WEAPONS_DIFF_HITDBG")) printf("  tracer before pos %d,%d,%d step %d,%d,%d sub %d,%d,%d | after port %d,%d,%d rig %d,%d,%d latch %d inbox %u noise %d,%d,%d rig hull-test calls %d (big %d)\n", t0.pos[0], t0.pos[1], t0.pos[2], t0.step[0], t0.step[1], t0.step[2], t0.sub[0], t0.sub[1], t0.sub[2], W.slot[0].pos[0], W.slot[0].pos[1], W.slot[0].pos[2], get32(0x4AD2A), get32(0x4AD2A + 4), get32(0x4AD2A + 8), t0.latch, W.dbg_inbox, t0.sub[0] * 8 - t0.step[0], t0.sub[1] * 8 - t0.step[1], t0.sub[2] * 8 - t0.step[2], rig_hull_calls, rig_big_calls);
    if (bad && getenv("WEAPONS_DIFF_LMDBG") && LM_ENTRIES) lm_dump(&t0);
    if (bad && getenv("WEAPONS_DIFF_ENTS")) { printf("   rig calls: hull %d big %d; entities:", rig_hull_calls, rig_big_calls); for (int i = 0; i < nent; i++) printf(" [rec %d model %d cls %02X w0 %04X]", ents[i], E.rec[ents[i]].template_id, E.rec[ents[i]].ac.f.cls, E.rec[ents[i]].ac.f.w0); printf(" queue:"); for (int i = 0; i < NQC; i++) printf(" %02X/%02X", QC[i].id, QC[i].flags); printf("\n"); }
    if (bad) printf("  gunhit case %d failed (entities %d, target record %d, model %d part %02X l7d %02X cls %02X w0 %04X)\n", cn, nent, ents[target], E.rec[ents[target]].template_id, E.rec[ents[target]].ac.f.b7b, E.rec[ents[target]].ac.f.l7d, E.rec[ents[target]].ac.f.cls, E.rec[ents[target]].ac.f.w0);
    gunhit_hits += (W.slot[0].flags & 0x10) != 0;
    if (LM_ENTRIES && (W.slot[0].flags & 0x10)) lm_stopped++;
    last_port_hit = (W.slot[0].flags & 0x10) != 0; last_rig_hit = (get16(0x4AD2A + 0x26) & 0x10) != 0;
    return bad ? 1 : 0;
}

/* ---- the landmark ordinals with the whole machine state (07 8.2 (b) and (c); tools/m68k-rig/tests/lists_model/landmark_state_check.py writes the cases) ----------------------------------------------------
 * The rig holds the state of a free flight (extracted/captures/state-flight.bin: the state hunks at their flat addresses, the machine's RAM at its real addresses), the gun routine runs on a single
 * landmark entry with the ordinal register loaded so that the walk enters at the ordinal k (a trampoline at LS_TRAMP replaces the register's initial load; the three words it displaces are read from the loaded
 * image), and the port runs the same case with dbg_reg0 = k and the real volumes of the dump as its landmark slots. Kind 1: a real ordinal 0 to 10 or an exact alias of one (k = j - 16384, j - 32768): the port
 * must equal the original. Kind 0: an ordinal outside 0 to 10 whose junk reads the original treats exactly like a skipped entry (the census run found it so): the port must equal the original. Kind 2: an ordinal
 * whose junk reads the original does NOT treat as a skip (a hit, a tracer moved by the eight sub-steps, a call that does not return): counted, not compared (the port has not the memory; note 07 8.2). */

#define LS_TRAMP 0xD00000u
typedef struct { int k, kind, cell_x, cell_z, pos[3]; uint32_t pf, rf, ptr; uint8_t rec[256]; } ls_case_t;
static ls_case_t *LSC;
static int nLSC;
static uint8_t *LS_STATE;
static size_t LS_STATE_LEN;
static const char *LS_STATE_PATHS[] = { "../../extracted/captures/state-flight.bin", "extracted/captures/state-flight.bin" };
static const char *LS_CASE_PATHS[] = { "../../extracted/captures/landmark-state-cases.bin", "extracted/captures/landmark-state-cases.bin" };
static uint32_t ls_be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static int ls_slurp(const char **paths, uint8_t **out, size_t *len) {
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(paths[i], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        const long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        *out = (uint8_t *)malloc((size_t)n + 1);
        *len = fread(*out, 1, (size_t)n, f);
        fclose(f);
        return 0;
    }
    return -1;
}

static int ls_load(void) {
    uint8_t *cb; size_t cl;
    if (ls_slurp(LS_STATE_PATHS, &LS_STATE, &LS_STATE_LEN) != 0 || ls_slurp(LS_CASE_PATHS, &cb, &cl) != 0) return -1;
    if (LS_STATE_LEN < 12 || memcmp(LS_STATE, "ST07", 4) || cl < 12 || memcmp(cb, "LMC2", 4)) return -1;
    const uint8_t *p = cb + 4;
    const int nv = (int)ls_be32(p); p += 4;
    memset(env.landmark, 0, sizeof env.landmark);
    env.n_landmarks = 0;
    for (int v = 0; v < nv; v++) {
        int h[9];
        for (int i = 0; i < 9; i++) { h[i] = (int)ls_be32(p); p += 4; }
        flight_lm_slot_t *sl = &env.landmark[env.n_landmarks++];
        sl->ordinal = (uint8_t)h[0]; sl->cell_x = (uint8_t)h[1]; sl->cell_z = (uint8_t)h[2];
        for (int i = 0; i < 5; i++) sl->box[i] = (int16_t)h[3 + i];
        sl->nnodes = (uint8_t)h[8];
        for (int n = 0; n < h[8]; n++) {
            int nh[7];
            for (int i = 0; i < 7; i++) { nh[i] = (int)ls_be32(p); p += 4; }
            for (int i = 0; i < 5; i++) sl->node[n].box[i] = (int16_t)nh[i];
            sl->node[n].nplanes = (uint8_t)nh[5];
            for (int q = 0; q < nh[6]; q++) {
                for (int c = 0; c < 3; c++) { sl->node[n].ref[q][c] = (int16_t)ls_be32(p); p += 4; }
                for (int c = 0; c < 3; c++) { sl->node[n].nrm[q][c] = (int16_t)ls_be32(p); p += 4; }
            }
        }
    }
    nLSC = (int)ls_be32(p); p += 4;
    LSC = (ls_case_t *)calloc((size_t)nLSC, sizeof *LSC);
    for (int i = 0; i < nLSC; i++) {
        ls_case_t *c = &LSC[i];
        c->k = (int)ls_be32(p); c->kind = (int)ls_be32(p + 4); c->cell_x = (int)ls_be32(p + 8); c->cell_z = (int)ls_be32(p + 12);
        c->pos[0] = (int)ls_be32(p + 16); c->pos[1] = (int)ls_be32(p + 20); c->pos[2] = (int)ls_be32(p + 24);
        c->pf = ls_be32(p + 28); c->rf = ls_be32(p + 32); c->ptr = ls_be32(p + 36);
        memcpy(c->rec, p + 40, 256);
        p += 296;
    }
    free(cb);
    env.landmark_z = 48;   /* the pointers of the dump are real addresses 0xC3....: the high word divided by 4 is 48 (05 17.11) */
    return 0;
}

static void ls_overlay(void) {
    const uint8_t *p = LS_STATE + 8;
    const uint32_t tl = ls_be32(p); p += 4 + tl;
    const int n = (int)ls_be32(LS_STATE + 4);
    for (int i = 0; i < n; i++) {
        const int kind = p[0]; const uint32_t a = ls_be32(p + 1), l = ls_be32(p + 5);
        p += 9;
        if (kind == 1 && a == 0) { uint8_t keep[0x120]; rig_peek(0, keep, sizeof keep); rig_poke(0, p, l); rig_poke(0, keep, 8); rig_poke(0x100, keep + 0x100, 0x20); }
        else rig_poke(a, p, l);
        p += l;
    }
}

static int ls_count[3], ls_diff[3], ls_hits;
static int run_lmstate(int ci) {
    const ls_case_t *c = &LSC[ci];
    memset(&PLAYER, 0, sizeof PLAYER);
    flight_state_t *pf = &PLAYER.f;
    pf->cls = 0x11; pf->w0 = 0x11C8; pf->v4a984 = 0x0840; pf->v4a844 = 1; pf->v4a847 = 1; pf->v4a83c = 1;
    entities_init(&E, have_tpl ? &TPL : NULL, &AITAB, NULL);
    entities_attach_player(&E, &PLAYER);
    weapons_init(&W, &E);
    W.hook.count = ph_count; W.hook.queue_c = hk_queue_c; W.hook.slots = hk_slots; W.hook.ship = hk_ship; W.hook.model_flags = hk_model_flags; memset(LBC, 0, sizeof LBC);
    E.rec[0].rounds = 500;
    E.designated = -1;
    NQC = 1; QC[0].id = 0; QC[0].flags = 0;
    wp_tracer_t t; memset(&t, 0, sizeof t);
    t.cell_x = (int16_t)c->cell_x; t.cell_z = (int16_t)c->cell_z;
    for (int a = 0; a < 3; a++) { t.pos[a] = c->pos[a]; t.sub[a] = a == 0 ? 300 : a == 1 ? -200 : 150; t.step[a] = t.sub[a] * 8; }
    t.flags = 0x401; t.life = 20; t.latch = 1; t.owner = 0;
    W.slot[0] = t;
    W.dbg_reg0 = c->k;
    rig_reset_state();
    ls_overlay();
    { static const uint32_t patch[] = { 0x31384, 0x3140C, 0x31478 }; for (unsigned i = 0; i < 3; i++) put16(patch[i], 0x4E75); }
    put16(0x4A97E, 0);
    { static const uint8_t z[1280]; rig_poke(0x4AD2A, z, sizeof z); }
    rig_poke(c->rf, c->rec, 256);
    put32(c->pf, (int32_t)c->ptr);
    put16(QC_BASE, 0);
    put32(0x4AA7E, QC_BASE); put8(0x4A916, 1);
    set_pool_slot(0, &W.slot[0]);
    {   /* the register's initial load replaced: D5 = 0xFFFF0000 | (k - 1); the six bytes after the load are re-executed from the trampoline */
        const uint32_t d5 = 0xFFFF0000u | (uint32_t)((c->k - 1) & 0xFFFF);
        uint8_t tw[6];
        rig_peek(0x2AE5Cu, tw, 6);
        put16(LS_TRAMP, 0x2A3C); put32(LS_TRAMP + 2, (int32_t)d5);
        rig_poke(LS_TRAMP + 6, tw, 6);
        put16(LS_TRAMP + 12, 0x4EF9); put32(LS_TRAMP + 14, 0x2AE62);
        put16(0x2AE5A, 0x4EF9); put32(0x2AE5C, (int32_t)LS_TRAMP);
    }
    put16(0x4A992, 5); PLAYER.f.v4a992 = 5;
    for (int k = RIG_D0; k <= RIG_A6; k++) rig_set_reg((rig_reg_t)k, 0);
    weapons_gun(&W, 5, 0);
    rig_set_max_instructions(400000);
    const int ok = rig_run(0x172DCu) == 0;
    rig_set_max_instructions(50000000);
    ls_count[c->kind]++;
    if (!ok) { if (c->kind != 2) printf("  lmstate case %d k=%d: the rig did not return\n", ci, c->k); return c->kind == 2 ? 0 : 1; }
    if (c->kind == 2) return 0;
    if (c->kind == 1 && (get16(0x4AD2A + 0x26) & 0x10)) ls_hits++;
    const int bad = compare_pool(1, ci);
    if (bad) { ls_diff[c->kind]++; printf("  lmstate case %d k=%d kind %d failed\n", ci, c->k, c->kind); }
    return bad;
}

/* ---- a landmark slot made from the real table through the node with a plane count of 0 (07 8.2 (4), S10; extractor 26) ---------------------------------------------------------------------------------
 * The footprint of the object record 34 (lists_footprint through world_footprint_slot: one node with a count of 0 and the one plane that follows the count, now carried by the table) is put in the
 * original's memory in the layout of the volume records (lm_poke) with the real plane and count; the real boxes of that footprint are empty (their z limits are reversed: no tracer is ever inside), so the
 * boxes are widened around the tracer (the plane is the table's). The original and the port run the gun routine on a tracer at random places around the box. */
static int lmnode_hits, lmnode_cases;
static int run_lmnode(int cn) {
    flight_lm_slot_t sl;
    if (world_footprint_slot(WORLD, 34, 0, 0, &sl) != 0 || sl.nnodes != 1 || sl.node[0].nplanes != 0) return -1;
    memset(&PLAYER, 0, sizeof PLAYER);
    flight_state_t *pf = &PLAYER.f;
    pf->cls = 0x11; pf->w0 = 0x11C8; pf->v4a984 = 0x0840; pf->v4a844 = 1; pf->v4a847 = 1; pf->v4a83c = 1;
    PLAYER.pos[0] = 64 * 4194304 + rnd(0, 4194303); PLAYER.pos[2] = 64 * 4194304 + rnd(0, 4194303); PLAYER.pos[1] = 0x300000 + rnd(-100000, 100000);
    pos_to_cells(&PLAYER);
    flight_set_attitude(&PLAYER, 0, (uint16_t)rnd(0, 28799), 0);
    pf->l7d = 0x53; pf->id = 0x1D; pf->v4aa04 = pf->cell_x; pf->v4aa06 = pf->cell_z;
    entities_init(&E, have_tpl ? &TPL : NULL, &AITAB, NULL);
    entities_attach_player(&E, &PLAYER);
    weapons_init(&W, &E);
    W.hook.count = ph_count; W.hook.queue_c = hk_queue_c; W.hook.slots = hk_slots; W.hook.ship = hk_ship; W.hook.model_flags = hk_model_flags; memset(LBC, 0, sizeof LBC);
    E.rec[0].rounds = 500;
    E.designated = -1;
    NQC = 1; QC[0].id = 0; QC[0].flags = 0;
    wp_tracer_t t; memset(&t, 0, sizeof t);
    const int dzc = pct(50);   /* a tracer in the next cell in z: the z offset of 16384 puts the point near the plane's reference (z 32256) */
    t.cell_x = 64; t.cell_z = (int16_t)(64 + dzc);
    int X = rnd(300, 16000), Y = rnd(-20, 1200), Z = rnd(300, 16000);
    t.pos[0] = X * 256 + rnd(0, 255); t.pos[1] = Y * 256 + rnd(0, 255); t.pos[2] = Z * 256 + rnd(0, 255);
    for (int a = 0; a < 3; a++) { t.sub[a] = rnd(-4000, 4000); t.step[a] = t.sub[a] * 8; }
    t.flags = 0x401; t.life = 20; t.latch = 1; t.owner = 0;
    W.slot[0] = t;
    /* the volume: the table's node with its plane and count, the boxes widened around the tracer (the z limits carry the pointer artefact of 1) */
    memset(LMV, 0, sizeof LMV);
    lmvol_t *v = &LMV[0];
    v->listed = 1; v->cx = t.cell_x; v->cz = 64; v->nn = 1;
    const int sp = rnd(300, 6000);
    v->box[0] = 30000; v->box[1] = (int16_t)(X - sp); v->box[2] = (int16_t)(X + sp); v->box[3] = (int16_t)(Z + 1 + 16384 * dzc - sp); v->box[4] = (int16_t)(Z + 1 + 16384 * dzc + sp);
    v->el[0].box[0] = 30000; v->el[0].box[1] = v->box[1]; v->el[0].box[2] = v->box[2]; v->el[0].box[3] = v->box[3]; v->el[0].box[4] = v->box[4];
    v->el[0].count = sl.node[0].nplanes; v->el[0].npw = 1;
    for (int c = 0; c < 3; c++) { v->el[0].ref[0][c] = sl.node[0].ref[0][c]; v->el[0].nrm[0][c] = sl.node[0].nrm[0][c]; }
    LM_ENTRIES = 1;
    env.n_landmarks = 1;
    memset(&env.landmark[0], 0, sizeof env.landmark[0]);
    env.landmark[0].cell_x = (uint8_t)v->cx; env.landmark[0].cell_z = (uint8_t)v->cz; env.landmark[0].ordinal = 0;
    memcpy(env.landmark[0].box, v->box, sizeof v->box);
    env.landmark[0].nnodes = 1;
    memcpy(env.landmark[0].node[0].box, v->el[0].box, sizeof v->el[0].box);
    env.landmark[0].node[0].nplanes = sl.node[0].nplanes;
    memcpy(env.landmark[0].node[0].ref, sl.node[0].ref, sizeof sl.node[0].ref);
    memcpy(env.landmark[0].node[0].nrm, sl.node[0].nrm, sizeof sl.node[0].nrm);
    if (getenv("WEAPONS_DIFF_MUTATE")) { memset(env.landmark[0].node[0].ref, 0, sizeof env.landmark[0].node[0].ref); memset(env.landmark[0].node[0].nrm, 0, sizeof env.landmark[0].node[0].nrm); }   /* mutation check: the port as before extractor 26 (zeros for the plane) fails */

    rig_reset_state();
    rig_globals(4, 0);
    { static const uint32_t patch[] = { 0x31384, 0x3140C, 0x31478 }; for (unsigned i = 0; i < 3; i++) put16(patch[i], 0x4E75); }
    put8(0x4A83D, 0); put8(0x4AC13, 0x20 | 0x04); put16(0x4A97E, 0);
    put16(0x4AA5E, WP_TRACER_BOX);
    record_fields(&E.rec[0], &PLAYER); put_fields(RECA(0));
    put8(RECA(0) + 0x3B, 0); put16(RECA(0) + 0x60, 500);
    put16(QC_BASE, 0); put32(0x4AA7E, QC_BASE); put8(0x4A916, 1);
    lm_poke();
    set_pool_slot(0, &W.slot[0]);
    for (int k = RIG_D0; k <= RIG_A6; k++) rig_set_reg((rig_reg_t)k, 0);
    put16(0x4A992, 5); PLAYER.f.v4a992 = 5;
    weapons_gun(&W, 5, 0);
    if (rig_run(0x172DCu) != 0) { printf("rig_run failed\n"); return -1; }
    lmnode_cases++;
    lmnode_hits += (get16(0x4AD2A + 0x26) & 0x10) != 0;
    const int bad = compare_pool(1, cn);
    if (bad) printf("  lmnode case %d failed (tracer %d,%d,%d box +-%d)\n", cn, X, Y, Z, sp);
    return bad;
}

/* ---- the draw scan 0x2AD18 (07 7.3, 12.5): the highest-index active slot decides alone, a chaff slot or an external view goes to the distance test ----------------------------------------------- */

static int gunorder_ones, gunorder_cases, gunorder_chaff;
static int run_gunorder(int cn) {
    memset(&PLAYER, 0, sizeof PLAYER);
    entities_init(&E, have_tpl ? &TPL : NULL, &AITAB, NULL);
    entities_attach_player(&E, &PLAYER);
    weapons_init(&W, &E);
    const int ext = pct(60);
    int32_t base[3] = { 64 * 4194304 + rnd(0, 4194303), 0x300000 + rnd(-100000, 100000), 64 * 4194304 + rnd(0, 4194303) };
    const int spread = pct(50) ? 30000 : pct(50) ? 400000 : 6000000;
    int32_t cam[3];
    for (int i = 0; i < 3; i++) cam[i] = base[i] + rnd(-spread, spread);
    const int nrec = rnd(1, 3);
    for (int r = 0; r < nrec; r++) {
        aircraft_t *a = r == 0 ? &PLAYER : &E.rec[r].ac;
        for (int i = 0; i < 3; i++) a->pos[i] = base[i] + rnd(-spread, spread);
        if (pct(10)) a->pos[1] = rnd(-2000000000, 2000000000);
    }
    const int nact = pct(15) ? 0 : rnd(1, 4);
    memset(W.slot, 0, sizeof W.slot);
    for (int i = 0; i < 20; i++) {
        wp_tracer_t *t = &W.slot[i];
        t->flags = (uint16_t)(pct(30) ? 0x0400 : 0);                     /* inactive slots with other bits and stale contents */
        t->pos[0] = rnd(0, 0x3FFFFF); t->pos[1] = base[1] + rnd(-spread, spread); t->pos[2] = rnd(0, 0x3FFFFF);
        t->cell_x = (int16_t)(64 + rnd(-1, 1)); t->cell_z = (int16_t)(64 + rnd(-1, 1));
        t->owner = (uint8_t)rnd(0, nrec - 1);
        t->life = (int16_t)rnd(-5, 30);
    }
    for (int j = 0; j < nact; j++) {
        wp_tracer_t *t = &W.slot[rnd(0, 19)];
        t->flags = (uint16_t)(0x0001 | (pct(20) ? 0x2000 : 0) | (pct(30) ? 0x0002 : 0) | (pct(30) ? 0x0400 : 0));
    }
    rig_reset_state();
    rig_globals(4, 0);
    put8(0x4A83D, ext ? 0xFF : 0);
    for (int i = 0; i < 3; i++) put32(0x4ACF6 + 4 * (uint32_t)i, cam[i]);
    for (int r = 0; r < nrec; r++) for (int i = 0; i < 3; i++) put32(RECA(r) + 0x14 + 4u * (uint32_t)i, (r == 0 ? &PLAYER : &E.rec[r].ac)->pos[i]);
    for (int i = 0; i < 20; i++) set_pool_slot(i, &W.slot[i]);
    for (int k = RIG_D0; k <= RIG_A6; k++) rig_set_reg((rig_reg_t)k, 0);
    {   /* a per-axis distance of two cells or more makes a word of the distance routine negative: the port does not hold the table that routine then reads (weapons.c scan_abs8): not drawn here */
        for (int i = 19; i >= 0; i--) {
            if (!(W.slot[i].flags & 1)) continue;
            if (!(W.slot[i].flags & 0x2000) && !ext) break;
            const wp_tracer_t *t = &W.slot[i];
            const int own = t->owner;
            const aircraft_t *oa = own == 0 ? &PLAYER : &E.rec[own].ac;
            const int64_t d[6] = { (int64_t)oa->pos[0] - cam[0], (int64_t)oa->pos[1] - cam[1], (int64_t)oa->pos[2] - cam[2],
                                   (int64_t)t->pos[0] + ((int64_t)t->cell_x << 22) - cam[0], (int64_t)t->pos[1] - cam[1], (int64_t)t->pos[2] + ((int64_t)t->cell_z << 22) - cam[2] };
            for (int q = 0; q < 6; q++) { const int64_t a = d[q] < 0 ? -d[q] : d[q]; if (a >= (1LL << 31) || (a >> 8) >= 0x8000) return 0; }
            break;
        }
    }
    const uint32_t rv = rig_call(0x2AD18u, NULL, 0);
    if (rig_last_error != RIG_OK) { printf("  gunorder case %d: rig error %d\n", cn, rig_last_error); return -1; }
    const int port = weapons_scan(&W, ext, cam);
    gunorder_cases++; gunorder_ones += port;
    for (int i = 0; i < 20; i++) if ((W.slot[i].flags & 0x2001) == 0x2001) { gunorder_chaff++; break; }
    if ((int)(rv & 0xFF) != port) {
        printf("  gunorder case %d: port %d rig %u (view %d, active slots", cn, port, (unsigned)(rv & 0xFF), ext);
        for (int i = 0; i < 20; i++) if (W.slot[i].flags & 1) printf(" %d(%04X owner %d)", i, W.slot[i].flags, W.slot[i].owner);
        printf(")\n");
        return 1;
    }
    return 0;
}

/* ---- the demo's AI over time: the whole core plays the demonstration (menu key 1) on the real assets; from a frame on, the original's world update is run in the rig on the port's player
 * (written after each port step), and the AI records the port holds at that frame, compared word by word every pass (the MiG of the demo that the AMRAAM chases) -------------------------- */

#include "../../src/core/game.h"
#include "../../src/core/screen.h"
static void gkey(game_t *g, int c) { game_event_t ev = { GAME_EV_KEY, 0, c }; game_input(g, &ev); }

static int run_demo_ai(int from, int to, int verbose_all) {
    char err[200] = "";
    game_t *g = game_create(&loader, err, sizeof err);
    if (!g) { printf("  (game_create: %s)\n", err); return -1; }
    gkey(g, 'x');
    for (int i = 0; i < 14; i++) game_step(g);
    gkey(g, '1');
    entities_t *pe = (entities_t *)game_debug_entities(g);
    aircraft_t *pl = game_debug_aircraft(g);
    int seen = 0, bad = 0, first_bad = 0;
    int prev_des = -1, prev_launch = 0;
    for (int i = 0; i < 6000 && !bad; i++) {
        const int des_before = pe->designated, launch_before = pe->launch_flag;
        game_step(g);
        const int d = game_demo_step(g);
        if (d < 0) continue;
        if (d == from) {   /* copy the AI records, the carrier and the globals into the rig */
            rig_reset_state();
            for (int k = 0; k < ENT_RECORDS; k++) {
                if (k == 0) continue;
                record_fields(&pe->rec[k], entities_ac(pe, k)); put_fields(RECA(k));
                put8(RECA(k) + 0x71, 0xFF);
                put8(RECA(k) + 0x7B, entities_ac(pe, k)->f.b7b);
                if (have_tpl) for (int q = 0; q < TEMPLATE_LONGS; q++) put32(0x2600Cu + 20u * (uint32_t)k + 4u * (uint32_t)q, (int32_t)pe->desc[k][q]);
            }
            seen = 1;
            printf("  demo AI: rig starts at frame %d; mode %02X progress %d hostile cap %d count %d\n", d, pe->mode, pe->progress, pe->hostile_cap, pe->hostile_count);
        } else if (seen && d > from && d <= to) {
            /* the rig runs the pass on the player's post-step state; the AI's inputs are the values the port's pass started with */
            rig_hooks_clear();
            rig_hook_set(0x29BC8, hook_post, 0);
            rig_hook_set(0x275F6, NULL, 0);
            put8(0x4A95E, pe->mode); put8(0x4A95F, pe->progress); put8(0x4A846, 1); put8(0x4A84D, 1); put16(0x4A984, 0x0840); put32(0x4A7B2, 0x800000);
            put8(0x4A83C, 1); put8(0x4A847, 1); put8(0x4A844, 1); put8(0x4A83D, 1); put8(0x4A841, 1);
            put8(0x4A962, pe->hostile_cap); put8(0x4A961, pe->hostile_count);
            put16(0x4AA78, des_before < 0 ? 0xFFFF : des_before * 0x200); put8(0x4A86F, launch_before);
            put16(0x4AA04, pl->f.cell_x); put16(0x4AA06, pl->f.cell_z);
            put16(0x4A992, pe->pass);
            {   /* the player: the port's record after its step, idle for the rig (W0 without 0x1000 and 0x0100) */
                aircraft_t tmp = *pl;
                tmp.f.w0 &= (uint16_t)~0x1100;
                record_fields(&pe->rec[0], &tmp); put_fields(RECA(0));
                put8(RECA(0) + 0x71, 0);
            }
            for (int k = RIG_D0; k <= RIG_A6; k++) rig_set_reg((rig_reg_t)k, 0);
            if (rig_run(0x26AF8u) != 0) { printf("rig_run failed at frame %d\n", d); return -1; }
            put8(0x4A910, 0);
            record_fields(&pe->rec[10], &pe->rec[10].ac);
            if (getenv("DEMO_DBG") && d >= atoi(getenv("DEMO_DBG"))) printf("  frame %d: player pos %d,%d,%d cells %d,%d fine %d,%d | MiG port dest %d,%d,%d,%d,%d rig dest %d,%d,%d,%d,%d | MiG state %d/%d range %d/%d target %02X/%02X b4 %02X/%02X w2 %04X/%04X\n", d, pl->pos[0], pl->pos[1], pl->pos[2], pl->f.cell_x, pl->f.cell_z, pl->f.fine_x, pl->f.fine_z,
                   pe->rec[10].dest_cx, pe->rec[10].dest_cz, pe->rec[10].dest_fx, pe->rec[10].dest_fz, pe->rec[10].dest_h, get16(RECA(10) + 0x2C), get16(RECA(10) + 0x2E), get16(RECA(10) + 0x30), get16(RECA(10) + 0x32), get32(RECA(10) + 0x34),
                   pe->rec[10].ac.f.state, get8(RECA(10) + 5), (int16_t)pe->rec[10].range, get16(RECA(10) + 0x4A), pe->rec[10].target, get8(RECA(10) + 0x38), pe->rec[10].ac.f.b4, get8(RECA(10) + 4), pe->rec[10].ac.f.w2, get16(RECA(10) + 2) & 0xFFFF);
            if (verbose_all) {
                const aircraft_t *mg = &pe->rec[10].ac;
                long long par = (long long)pl->f.mat_b[2] * mg->f.mat_b[2] + (long long)pl->f.mat_b[5] * mg->f.mat_b[5] + (long long)pl->f.mat_b[8] * mg->f.mat_b[8];
                double dd[3], nn = 0; for (int k = 0; k < 3; k++) { dd[k] = (double)(pl->pos[k] - mg->pos[k]); nn += dd[k] * dd[k]; }
                nn = sqrt(nn); double cs = (dd[0] * pl->f.mat_b[2] + dd[1] * pl->f.mat_b[5] + dd[2] * pl->f.mat_b[8]) / nn / 16384.0;
                printf("  frame %d: cos(player fwd, MiG->player) %.3f need <= %.3f\n", d, cs, -0x2C0000 / (192.0 * 16384.0));
                printf("  frame %d: par %lld (need %d) player fwd %d,%d,%d MiG fwd %d,%d,%d S %d/%d\n", d, par, 0xD000000, pl->f.mat_b[2], pl->f.mat_b[5], pl->f.mat_b[8], mg->f.mat_b[2], mg->f.mat_b[5], mg->f.mat_b[8], pl->f.speed_target, mg->f.speed_target);
            }
            if (verbose_all) printf("  frame %d: B4 %02X t39 %02X pr %d range %d tgt %02X\n", d, get8(RECA(10)+4), get8(RECA(10)+0x39), (int16_t)get16(RECA(0)+0x4A), (int16_t)get16(RECA(10)+0x4A), get8(RECA(10)+0x38));
            if (verbose_all) printf("  frame %d: MiG state %d/%d counter %d/%d W0 %04X/%04X flags %02X/%02X\n", d, pe->rec[10].ac.f.state, get8(RECA(10) + 5), pe->rec[10].ac.f.counter, get16(RECA(10) + 0x4C), pe->rec[10].ac.f.w0, get16(RECA(10)) & 0xFFFF, pe->rec[10].ai_flags, get8(RECA(10) + 0x64));
            skip_cellb = 1;
            put8(RECA(10) + 0x20, (uint8_t)((get8(RECA(10) + 0x20) & ~0x40) | (pe->rec[10].ac.f.b20 & 0x40)));   /* the radar list builder (the detected mark, B20 bit 6) is not part of the rig run */
            if (compare(RECA(10), "demo MiG", d, 1) && !getenv("DEMO_CONT")) { bad = 1; first_bad = d; }
            skip_cellb = 0;
        }
        (void)prev_des; (void)prev_launch;
        if (d >= to) break;
    }
    printf("test_weapons_diff: demo AI from frame %d: %s%d\n", from, bad ? "first difference at frame " : "no difference up to frame ", bad ? first_bad : to);
    game_destroy(g);
    return bad ? 1 : 0;
}

/* ---- the hostile launches of the records 9, 11 and 13: the decision 0x2701A and the launch routine 0x275F6 (note 07 4.6, 7.6; review item 22) ----------------------------------------------------
 * Random owners in the records 8, 10 and 12 and random records for their targets (the W0 word with the mask 0x8700 bits, the valid bit, the engaged bit, B20 bit 1, the target byte of every
 * kind including bit 7 set, the AI flags, the range class, the stock), a random global request V:4A874, the progress, the ECM switch and the pass counter. In record order 9, 11, 13, as the
 * record loop asks, when the missile record is not valid: the original's decision (0x2701A with A2 = the owner) against ai_launch_decision (the answer and the request byte after: the first record
 * that asks consumes the global request whatever its owner is), and after a yes the original's launch routine (0x275F6, A1 = the missile record, A2 = the owner: the rate gate by progress and
 * the ECM switch against V:4A992, the stock) against weapons_launch_ai: whether the record was filled (its valid bit) and the owner's stock after. */
static int launchdec_yes, launchdec_filled, launchdec_gate_refused, launchdec_stock_refused, launchdec_req_taken;
static int run_launchdec(int cn) {
    rig_reset_state();
    memset(&PLAYER, 0, sizeof PLAYER);
    PLAYER.f.cls = 0x11; PLAYER.f.w0 = 0x11C8; PLAYER.f.l7d = 0x53;
    entities_init(&E, have_tpl ? &TPL : NULL, &AITAB, NULL);
    entities_attach_player(&E, &PLAYER);
    weapons_init(&W, &E);
    E.hook.user = NULL; E.hook.launch_player = ph_launch_player; E.hook.launch_ai = ph_launch_ai; E.hook.weapon = ph_weapon;
    const int progress = (int)rnd(0, 4), ecm = pct(40);
    const uint32_t pass = (uint32_t)rnd(0, 255);
    const int req = pct(35);
    E.mode = 4; E.progress = progress; E.pass = pass; E.req_ai = (uint8_t)req;
    PLAYER.f.v4a96d = (uint8_t)ecm; PLAYER.f.v4a996 = 0xFFFF;
    put16(RECA(0), 0x11C8); put8(0x4A95E, 4); put8(0x4A95F, progress); put8(0x4A841, 1); put8(0x4A96D, ecm); put16(0x4A992, (int)pass); put8(0x4A874, req); put16(0x4A996, 0xFFFF);
    /* every record 1 to 15 gets random decision inputs; the owners 8, 10, 12 are made likely to say yes so that the later tests are reached */
    for (int i = 1; i < ENT_RECORDS; i++) {
        flight_state_t *f = &entities_ac(&E, i)->f;
        entity_t *r = &E.rec[i];
        const int owner = i == 8 || i == 10 || i == 12;
        uint16_t w0 = (uint16_t)xr();
        w0 &= (uint16_t)~0x0060;   /* keep the active and valid bits under control */
        if (owner) { w0 = (uint16_t)((w0 & ~0x8700) | (pct(80) ? 0 : (0x0100 << rnd(0, 3)) & 0x8700)); if (pct(85)) w0 |= 0x0040; if (pct(75)) w0 |= 0x0001; }
        else if (i == 9 || i == 11 || i == 13) { w0 = (uint16_t)(pct(80) ? 0 : 0x0040); }   /* the missile records: mostly free */
        else { if (pct(70)) w0 |= 0x0040; if (pct(70)) w0 &= (uint16_t)~0x0001; w0 &= (uint16_t)~0x8700; }
        f->w0 = w0;
        f->b20 = (uint8_t)((pct(85) ? 0 : 0x02) | (xr() & 0xFC));
        f->id = (uint8_t)(owner && pct(80) ? ((pct(50) ? 0x20 : 0x30) | rnd(0, 15)) : (uint8_t)xr());
        f->cls = 0x12;
        r->ai_flags = (uint8_t)(owner && pct(80) ? (0x60 | (xr() & 0x9F)) : (uint8_t)xr());
        r->target = (uint8_t)(owner ? (pct(60) ? (0x80 | rnd(0, 15)) : pct(15) ? 0xFF : (xr() & 0x7F)) : (uint8_t)xr());
        r->stock = (uint8_t)(pct(20) ? 0 : (xr() & 0xFF));
        put16(RECA(i), w0); put8(RECA(i) + 0x20, f->b20); put8(RECA(i) + 0x63, f->id); put8(RECA(i) + 0x64, r->ai_flags); put8(RECA(i) + 0x38, r->target); put8(RECA(i) + 0x5F, r->stock); put8(RECA(i) + 0x62, 0x12);
    }
    for (int k = 9; k <= 13; k += 2) {
        if (entities_ac(&E, k)->f.w0 & 0x0040) continue;   /* the record loop asks only for a record that is not valid */
        const int owner = k - 1;
        for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
        rig_set_reg(RIG_A2, RECA(owner));
        if (rig_run(0x2701A) != 0) { printf("  launchdec: rig run failed (%d)\n", rig_last_error); return -1; }
        const int rig_yes = rig_get_reg(RIG_D0) != 0;
        const int rig_req = get8(0x4A874);
        const int had_req = E.req_ai;
        const int yes = ai_launch_decision(&E, owner);
        if (yes != rig_yes || E.req_ai != rig_req) {
            printf("  launchdec case %d record %d: decision port %d request %d, rig %d request %d (owner w0 %04X b20 %02X target %02X flags %02X id %02X)\n", cn, k, yes, E.req_ai, rig_yes, rig_req,
                   entities_ac(&E, owner)->f.w0, entities_ac(&E, owner)->f.b20, E.rec[owner].target, E.rec[owner].ai_flags, entities_ac(&E, owner)->f.id);
            return 1;
        }
        if (!yes) continue;
        launchdec_yes++;
        if (had_req && !rig_req) launchdec_req_taken++;
        const int stock_before = E.rec[owner].stock;
        for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
        rig_set_reg(RIG_A1, RECA(k)); rig_set_reg(RIG_A2, RECA(owner));
        if (rig_run(0x275F6) != 0) { printf("  launchdec: launch rig run failed (%d)\n", rig_last_error); return -1; }
        const int rig_filled = (get16(RECA(k)) & 0x0040) != 0, rig_stock = get8(RECA(owner) + 0x5F);
        entities_ac(&E, k)->f.w0 = 0;
        const int filled = weapons_launch_ai(&W, k) && (entities_ac(&E, k)->f.w0 & 0x0040);
        if (filled != rig_filled || E.rec[owner].stock != rig_stock) {
            printf("  launchdec case %d record %d: launch port filled %d stock %02X, rig filled %d stock %02X (before %02X, progress %d ecm %d pass %u)\n", cn, k, filled, E.rec[owner].stock, rig_filled, rig_stock, stock_before, progress, ecm, pass);
            return 1;
        }
        if (filled) launchdec_filled++;
        else if (((stock_before >> ((entities_ac(&E, owner)->f.id >> 4) == 3 ? 0 : 4)) & 0x0F) != 0) launchdec_gate_refused++;
        else launchdec_stock_refused++;
    }
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    const char *paths[] = {"../../extracted/interceptor.bin", "extracted/interceptor.bin"};
    const int ls_part = getenv("WEAPONS_DIFF_PART") && !strcmp(getenv("WEAPONS_DIFF_PART"), "lmstate");
    if (rig_init(ls_part ? 0xE00000u : (1u << 21)) != 0) { printf("rig_init failed\n"); return 1; }
    for (int i = 0; i < 2 && !image_path; i++) if (rig_load_image(paths[i], BASE) == 0) image_path = paths[i];
    if (!image_path) { printf("test_weapons_diff: skipped (extracted/interceptor.bin not found)\n"); rig_shutdown(); return 0; }
    { FILE *t = fopen("../../assets/VERSION", "r"); if (!t) assets = "assets"; else fclose(t); }
    if (build_env() != 0) { rig_shutdown(); return 0; }
    const int n = getenv("WEAPONS_DIFF_N") ? atoi(getenv("WEAPONS_DIFF_N")) : 40;
    const int steps = getenv("WEAPONS_DIFF_STEPS") ? atoi(getenv("WEAPONS_DIFF_STEPS")) : 210;
    if (getenv("WEAPONS_DIFF_SEED")) rng = (uint32_t)atoi(getenv("WEAPONS_DIFF_SEED")) * 2654435761u + 1;
    verbose = getenv("WEAPONS_DIFF_VERBOSE") != NULL;
    const char *part = getenv("WEAPONS_DIFF_PART");
    if (getenv("WEAPONS_DIFF_MODELS")) for (int m = 1; m < 40; m++) { const int h = world_model_unit_shift_h(WORLD, (uint16_t)m, 0), l = world_model_unit_shift(WORLD, (uint16_t)m, 0); if (h >= 0) printf("model %d: H %d L %d levels %d vcount0 %d\n", m, h, l, world_model_level_count(WORLD, (unsigned)m), world_model_vertex_count(WORLD, (unsigned)m, 0)); }
    if (getenv("WEAPONS_DIFF_DUMPHULL")) for (int l = 0; l < AI_HULL_LISTS; l++) { printf("hull list %d:", l); for (int fc = 0; fc < AI_HULL_TRIPLES; fc++) printf(" (%04X %04X %04X)", (uint16_t)AITAB.hull[l][fc][0], (uint16_t)AITAB.hull[l][fc][1], (uint16_t)AITAB.hull[l][fc][2]); printf("\n"); }
    int bad = 0, total = 0;
    if (!part || !strcmp(part, "missile")) {
        for (int c = 0; c < n; c++) {
            const int r = run_missile(c, steps, getenv("WEAPONS_DIFF_ALL") && (!getenv("WEAPONS_DIFF_CASE") || atoi(getenv("WEAPONS_DIFF_CASE")) == c), pct(25));
            if (r < 0) { rig_shutdown(); return 1; }
            bad += r; total++;
        }
    }
    if (!part || !strcmp(part, "hostile")) {
        for (int c = 0; c < n; c++) {
            const int r = run_hostile(c, steps, getenv("WEAPONS_DIFF_ALL") && (!getenv("WEAPONS_DIFF_CASE") || atoi(getenv("WEAPONS_DIFF_CASE")) == c));
            if (r < 0) { rig_shutdown(); return 1; }
            bad += r; total++;
        }
        printf("test_weapons_diff: hostile part, %d launch refusals by the AI rate gate compared\n", hostile_refusals);
    }
    if (!part || !strcmp(part, "launchdec")) {
        int lb = 0, lt = 0;
        for (int c = 0; c < n * 50; c++) {
            const int r = run_launchdec(c);
            if (r < 0) { rig_shutdown(); return 1; }
            lb += r; lt++;
        }
        printf("test_weapons_diff: hostile launches of the records 9, 11, 13: %d cases, %d decisions yes (%d took the global request), %d launches filled, %d refused by the rate gate, %d by the stock, %d differ\n",
               lt, launchdec_yes, launchdec_req_taken, launchdec_filled, launchdec_gate_refused, launchdec_stock_refused, lb);
        bad += lb; total += lt;
    }
    if (!part || !strcmp(part, "gun")) {
        int gb = 0, gt = 0;
        for (int c = 0; c < n * 20; c++) {
            const int r = run_gun(c, 6);
            if (r < 0) { rig_shutdown(); return 1; }
            gb += r; gt++;
        }
        printf("test_weapons_diff: gun routine, %d sequences of 6 calls, %d differ\n", gt, gb);
        bad += gb; total += gt;
    }
    if (!part || !strcmp(part, "gunhit")) {
        int gb = 0, gt = 0;
        for (int c = 0; c < n * 20; c++) {
            const int r = run_gunhit(c);
            if (r < 0) { rig_shutdown(); return 1; }
            gb += r; gt++;
        }
        printf("test_weapons_diff: gun hit test, %d cases (%d tracers stopped by a hit), %d differ\n", gt, gunhit_hits, gb);
        printf("test_weapons_diff:   of them %d with landmark entries queued among the entities (%d stopped, %d met an ordinal outside 0 to 10, %d of those dropped for the pointer entry's address)\n", lm_cases, lm_stopped, lm_oob_cases, lm_oob_skipped);
        bad += gb; total += gt;
    }
    if (!part || !strcmp(part, "lmnode")) {
        int gb = 0, gt = 0;
        for (int c = 0; c < n * 10; c++) {
            const int r = run_lmnode(c);
            if (r < 0) { printf("test_weapons_diff: lmnode skipped (the table has no footprint with a plane count of 0 at record 34: extractor 26?)\n"); gt = 0; break; }
            gb += r; gt++;
        }
        if (gt) printf("test_weapons_diff: landmark slot from the real table through the node with a plane count of 0 (record 34), %d cases (%d tracers stopped by a hit), %d differ\n", gt, lmnode_hits, gb);
        bad += gb; total += gt;
    }
    if (part && !strcmp(part, "lmstate")) {
        if (ls_load() != 0) { printf("test_weapons_diff: lmstate skipped (extracted/captures/state-flight.bin and landmark-state-cases.bin: tools/m68k-rig/tests/lists_model/landmark_state_check.py)\n"); }
        else {
            int lb = 0;
            for (int c = 0; c < nLSC; c++) lb += run_lmstate(c);
            printf("test_weapons_diff: landmark ordinals with the whole state: %d real or alias cases (%d stopped by a hit, %d differ), %d junk-read cases equal to a skip in the original (%d differ), %d junk-read cases that are not a skip (not compared; the port skips them)\n",
                   ls_count[1], ls_hits, ls_diff[1], ls_count[0], ls_diff[0], ls_count[2]);
            bad += lb; total += ls_count[0] + ls_count[1];
        }
    }
    if (!part || !strcmp(part, "gunorder")) {
        int gb = 0, gt = 0;
        for (int c = 0; c < n * 50; c++) {
            const int r = run_gunorder(c);
            if (r < 0) { rig_shutdown(); return 1; }
            gb += r; gt++;
        }
        printf("test_weapons_diff: draw scan 0x2AD18, %d cases (%d answer gun first, %d with an active chaff slot), %d differ\n", gt, gunorder_ones, gunorder_chaff, gb);
        bad += gb; total += gt;
    }
    if (part && !strcmp(part, "demo")) { const int from = getenv("DEMO_FROM") ? atoi(getenv("DEMO_FROM")) : 300, to = getenv("DEMO_TO") ? atoi(getenv("DEMO_TO")) : 700; const int r = run_demo_ai(from, to, getenv("DEMO_ALL") != NULL); rig_shutdown(); return r != 0; }
    printf("test_weapons_diff: %d of %d cases differ\n", bad, total);
    rig_shutdown();
    return bad != 0;
}
