/* tests/rig/test_seat_diff.c - the ejection seat's, the parachute's and the pod's step (docs/re/07-ai-and-weapons.md 6.4) against the original's world update 0x26AF8 in the Musashi rig.
 * One record in slot 1 of class 0x30, 0x31 or 0x32 in a random state (flying with a counter from -20 up, landed with the hit bit, dying), the player as a valid record whose bits the seat
 * step reads (W0 0x0004, 0x0600, valid), the camera flag V:4A86D and the viewed record V:4A994/996 at random. The port runs entities_step with the flight model; compared at the end of the
 * pass: the seat's W0, counter, velocity, position, speed word and W2; the player's W2 bit 0x0020 (the release); V:4A8E2, V:4A8E4, V:4A910, V:4A968, V:4A994, V:4A996. The attitude step of
 * class nibble 3 (0x31AE0) is not in the notes and not compared. SEAT_DIFF_SPAWN=1 compares the SPAWN instead: the player's ejection cycle in the real flight model (the key nibble 2 or 15
 * before its step), the record 1 the world update fills, the latches, the viewed record. Environment: SEAT_DIFF_N (default 4000), SEAT_DIFF_SEED, SEAT_DIFF_VERBOSE, SEAT_SLOT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/flight.h"
#include "../../src/core/entities.h"
#include "rig_ai.h"
#include "../../src/core/world.h"

#define BASE 0x10000u
#define V_LO 0x4A6E8u
#define V_HI 0x54EC0u
#define REC 0x4B23Cu
static int SEAT = 1;
#define RECS (REC + (uint32_t)SEAT * 0x200u)

static void put8(uint32_t a, int v) { uint8_t b = (uint8_t)v; rig_poke(a, &b, 1); }
static void put16(uint32_t a, int v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 2); }
static void put32(uint32_t a, int32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 4); }
static int get8(uint32_t a) { uint8_t b; rig_peek(a, &b, 1); return b; }
static int get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return (int16_t)((b[0] << 8) | b[1]); }
static int32_t get32(uint32_t a) { uint8_t b[4]; rig_peek(a, b, 4); return (int32_t)(((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]); }

static const char *assets = "../../assets";
static ai_t RAI;
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
static int aim_first;   /* V:4A85E as the port's aim hook keeps it */
static int16_t trig_w(void *u, uint32_t addr) { int known; return flight_image_word((const flight_tables_t *)u, addr, &known); }
/* the aim of the attitude step (note 07 6.3a): the target pair of the velocity through the aim routine of note 03, the first call a snap */
static void aim_dir_hook(void *u, int32_t vx, int32_t vy, int32_t vz, int32_t *pitch, int32_t *heading) {
    (void)u;
    world_viewstate_t vs;
    memset(&vs, 0, sizeof vs);
    vs.image_word = trig_w; vs.image_user = &tables;
    int32_t tp = 0, th = 0;
    world_aim_target(&vs, vx, vy, vz, &tp, &th);
    if (!aim_first) { aim_first = 1; *pitch = tp; *heading = th; }
    else { *pitch = world_aim_step(*pitch, tp, WORLD_AIM_LIMIT); *heading = world_aim_step(*heading, th, WORLD_AIM_LIMIT); }
}
static flight_env_t env;
static int16_t pvtx[2][4][3];   /* model vertices 0..3 (level 0) of model 4 (index 0) and model 3 (index 1): the rig's stand-in for the draw, poked into the original's record and the port's slots alike */

static int build_env(void) {
    char err[160];
    if (flight_tables_load(&tables, &loader, err, sizeof err) != 0) { printf("test_seat_diff: skipped (%s)\n", err); return -1; }
    world_t *w = world_load(&loader, err, sizeof err);
    if (!w) { printf("test_seat_diff: skipped (%s)\n", err); return -1; }
    memset(&env, 0, sizeof env);
    for (int m = 0; m < 2; m++)
        for (int k = 0; k < 4; k++) {
            int32_t v[3];
            world_model_vertex(w, m == 0 ? 4 : 3, 0, (unsigned)k, v);
            for (int i = 0; i < 3; i++) pvtx[m][k][i] = (int16_t)v[i];
        }
    env.tab = &tables;
    env.landmark_z = 1; env.no_rearm = 1; env.no_sound_system = 1; env.no_liftoff_hook = 1;
    flight_set_env(&env);
    world_destroy(w);
    return 0;
}

typedef struct { const char *name; uint32_t off; int size; int32_t val; } field_t;
static field_t fl[128];
static int nfl;
static void F(const char *name, uint32_t off, int size, int32_t val) { field_t *f = &fl[nfl++]; f->name = name; f->off = off; f->size = size; f->val = val; }
static void port_fields(const aircraft_t *a) {
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
}

static const char *image_path;

static entities_t E;
static uint16_t E_chute_latch;
static aircraft_t PLAYER;
static uint32_t rng = 0x9E3779B9u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }
static int pct(int p) { return rnd(0, 99) < p; }

typedef struct {
    int cls, w0, w2, counter, nib; int32_t vel[3], pos[3];
    int p_w0, p_w2, camera_flag, viewed, req, delay, ext, roll, pitch, heading, first;
} cs_t;

static void gen(cs_t *c) {
    memset(c, 0, sizeof *c);
    c->cls = pct(70) ? 0x30 : pct(60) ? 0x31 : 0x32;
    const int kind = rnd(0, 9);   /* 0..5 flying, 6..8 landed, 9 dying */
    c->w0 = 0x10C0 | 0x0100 | 0x0002 | (kind >= 6 && kind <= 8 ? 0x8000 : 0) | (kind == 9 ? 0x0400 : 0);
    if (pct(30)) c->w0 &= ~0x0100;
    c->counter = kind <= 5 ? (pct(50) ? rnd(-20, 6) : rnd(0, 40)) : (pct(50) ? rnd(0, 6) : rnd(5, 900));
    c->vel[0] = rnd(-900, 900); c->vel[2] = rnd(-900, 900); c->vel[1] = pct(50) ? rnd(-1200, 600) : rnd(-768, -700);
    if (pct(20)) { c->vel[0] = c->vel[2] = rnd(-30, 30); }
    c->pos[0] = (64 + rnd(-1, 1)) * 4194304 + rnd(0, 4000000); c->pos[2] = (64 + rnd(-1, 1)) * 4194304 + rnd(0, 4000000);
    c->pos[1] = pct(30) ? rnd(0x1000, 0x4000) : rnd(0x100000, 0x800000);
    c->p_w0 = 0x0040 | (pct(50) ? 0x0004 : 0) | (pct(15) ? (pct(50) ? 0x0400 : 0x0200) : 0) | (pct(10) ? 0 : 0x1000) | 0x0080;
    if (pct(10)) c->p_w0 &= ~0x0040;
    c->p_w2 = pct(20) ? 0x0080 : 0;
    c->camera_flag = pct(70);
    c->viewed = pct(50) ? 0 : 1;
    c->req = pct(15) ? 2 : 0;
    c->delay = rnd(0, 5);
    c->nib = rnd(0, 6);
    c->first = pct(50);
    c->ext = pct(35);
    if (c->ext) c->p_w0 &= ~0x0400;   /* an external-view player step with the dying bit runs its expiry (the player is not stepped by this harness) */
    c->roll = pct(30) ? 0 : rnd(0, 28799); c->pitch = rnd(0, 28799); c->heading = rnd(0, 28799);
}

#define CHK(name, p, r) do { if (!bad && (long)(p) != (long)(r)) { bad = name; pv = (long)(p); rv = (long)(r); } } while (0)
static int diffs, cases;
static int verbose;


/* ---- the spawn (0x26F60, 0x27522, 0x2758E): the player's ejection cycle in the real flight model, the record 1 the world update fills ---------------------------------------------- */
static void put_fields(uint32_t base) {
    for (int i = 0; i < nfl; i++) {
        switch (fl[i].size) {
        case 1: put8(base + fl[i].off, fl[i].val); break;
        case 2: put16(base + fl[i].off, fl[i].val); break;
        default: put32(base + fl[i].off, fl[i].val); break;
        }
    }
}
static void write_vertices_player(void) {
    const int16_t (*mv)[3] = pvtx[0];
    int mb[9];
    for (int i = 0; i < 9; i++) mb[i] = get16(REC + 0x92 + 2 * i) >> 6;
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 3; i++) {
            int32_t sum = 0;
            for (int j = 0; j < 3; j++) sum += mb[3 * i + j] * mv[k][j];
            put16(REC + 0xA4 + 6 * k + 2 * i, (int16_t)(sum >> 8));
            PLAYER.f.vtx[k][i] = (int16_t)(sum >> 8);   /* the port's slots are the draw's: the same words */
        }
}

static int run_spawn(int N) {
    int d = 0, seats = 0, chutes = 0, dying_player = 0;
    for (int n = 0; n < N; n++) {
        const int chute = pct(35);
        memset(&PLAYER, 0, sizeof PLAYER);
        flight_init(&PLAYER, FLIGHT_FA18, FLIGHT_START_RUNWAY);
        flight_set_position(&PLAYER, (64 + rnd(-1, 1)) * 4194304 + rnd(0, 4000000), pct(15) ? rnd(0x6000, 0x9000) : rnd(0x100000, 0x600000), (64 + rnd(-1, 1)) * 4194304 + rnd(0, 4000000));
        flight_set_attitude(&PLAYER, (uint16_t)(pct(50) ? rnd(0, 1500) : 28800 - rnd(0, 1500)), (uint16_t)rnd(0, 28799), (uint16_t)(pct(50) ? rnd(0, 3000) : 28800 - rnd(0, 3000)));
        PLAYER.f.w2 = 0; PLAYER.f.b4 = 0;
        PLAYER.f.speed_target = PLAYER.f.speed = (int16_t)rnd(1800, 4500);
        PLAYER.f.throttle = (int8_t)rnd(20, 100);
        PLAYER.f.gear = (uint8_t)(0xE0 | (chute ? 15 : 2));
        PLAYER.f.v4a95e = 1; PLAYER.f.v4a984 = 0x0840; PLAYER.f.v4a844 = 1; PLAYER.f.v4a847 = 1; PLAYER.f.v4a7b2 = 0x800000; PLAYER.f.v4a83c = 1; PLAYER.f.v4a890 = 0;
        PLAYER.f.v4aa04 = PLAYER.f.cell_x; PLAYER.f.v4aa06 = PLAYER.f.cell_z;
        const int ext = pct(25);
        PLAYER.f.v4a83d = (uint8_t)ext;
        const int rescue_blocked = pct(10);   /* the latch already set: nothing spawns */
        if (rescue_blocked) PLAYER.f.v4aa7a = 0x200;
        flight_sync(&PLAYER);
        if (rig_load_image(image_path, BASE) != 0) return -1;
        static const uint32_t patch[] = { 0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x275F6, 0x275C4 };
        for (int i = 0; i < 6; i++) put16(patch[i], 0x4E75);
        rig_poke(FLIGHT_SPAN_B_BASE, tables.span_b, FLIGHT_SPAN_B_SIZE);
        static uint8_t zero[V_HI - V_LO];
        rig_poke(V_LO, zero, sizeof zero);
        rig_hooks_clear();
        put8(0x4A95E, 1); put8(0x4A846, 1); put8(0x4A84D, 1); put16(0x4A984, 0x0840);
        put32(0x4A7B2, 0x800000); put8(0x4A83C, 1); put8(0x4A847, 1); put8(0x4A844, 1); put8(0x4A83D, ext);
        put8(0x4A86D, 1); put16(0x4AA78, 0xFFFF);
        put16(0x4AA04, PLAYER.f.cell_x); put16(0x4AA06, PLAYER.f.cell_z);
        put16(0x4AA7A, PLAYER.f.v4aa7a);
        port_fields(&PLAYER);
        put_fields(REC);
        put8(REC + 0x71, 0xFF);
        for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
        write_vertices_player();
        if (rig_run(0x26AF8u) != 0) continue;
        flight_inputs_t in; memset(&in, 0, sizeof in);
        flight_set_env(&env);
        entities_init(&E, NULL, &RAI, NULL);
        entities_attach_player(&E, &PLAYER);
        E.flying = 1; E.sim_on = 1; E.camera_update = 1;
        flight_step(&PLAYER, &in);
        entities_step(&E);
        const flight_state_t *f = &E.rec[SEAT].ac.f;
        const char *bad = NULL; long pv = 0, rv = 0;
        const uint32_t S0 = RECS;
        CHK("seat W0", f->w0 & 0xFEFFu, get16(S0) & 0xFEFF);
        CHK("W2", f->w2, get16(S0 + 2) & 0xFFFF);
        CHK("cls", f->cls, get8(S0 + 0x62));
        CHK("counter", f->counter, get16(S0 + 0x4C));
        CHK("+26", f->ground, get16(S0 + 0x26));
        CHK("vel x", f->vel[0], get32(S0 + 0x3E)); CHK("vel y", f->vel[1], get32(S0 + 0x42)); CHK("vel z", f->vel[2], get32(S0 + 0x46));
        CHK("pos x", E.rec[SEAT].ac.pos[0], get32(S0 + 0x14)); CHK("pos y", E.rec[SEAT].ac.pos[1], get32(S0 + 0x18)); CHK("pos z", E.rec[SEAT].ac.pos[2], get32(S0 + 0x1C));
        CHK("alt8", f->alt8, get32(S0 + 0x10)); CHK("cell x", f->cell_x, get16(S0 + 6)); CHK("cell z", f->cell_z, get16(S0 + 8));
        CHK("speed W", f->speed, get16(S0 + 0x6E));
        CHK("dest cx", E.rec[SEAT].dest_cx, get16(S0 + 0x2C));
        CHK("latch 4AA7A", PLAYER.f.v4aa7a, get16(0x4AA7A)); CHK("latch 4A97A", E.chute_latch, get16(0x4A97A));
        if (E.rec[SEAT].ac.f.w0 & 0x0040) CHK("V:4A910 (bits 0x0C)", (E.request | PLAYER.f.v4a910) & 0x0C, get8(0x4A910) & 0x0C);   /* the rest of the byte is the display request: 0x8C of the spawn, 0x0C of a dying copy, 0xFF of a cell change (not compared) */
        CHK("V:4A994", PLAYER.f.v4a994, get16(0x4A994)); CHK("V:4A996", PLAYER.f.v4a996, get16(0x4A996));
        CHK("V:4A968", PLAYER.f.v4a968, get8(0x4A968));
        CHK("player gear", PLAYER.f.gear, get8(REC + 0x7C));
        if ((f->w0 & 0x0040) && (f->cls & 0xF0) == 0x30) { if (f->cls == 0x31) chutes++; else seats++; }
        if (PLAYER.f.w0 & 0x0400) dying_player++;
        if (getenv("SEAT_DIFF_410") && (f->w0 & 0x0040) && (f->cls & 0xF0) == 0x30) printf("R410 chute=%d ext=%d rig=%02X port_req=%02X player410=%02X\n", f->cls == 0x31, ext, get8(0x4A910), E.request, PLAYER.f.v4a910);
        if (bad) {
            d++;
            if (verbose || d <= 10) printf("  spawn case %d (%s, ext %d, blocked %d): %s: port %ld (0x%lX) rig %ld (0x%lX)   [player w0 port %04X rig %04X, y %d, gear port %02X rig %02X]\n", n, chute ? "parachute" : "seat", ext, rescue_blocked, bad, pv, pv & 0xFFFFFFFFl, rv, rv & 0xFFFFFFFFl, PLAYER.f.w0, get16(REC) & 0xFFFF, PLAYER.pos[1], PLAYER.f.gear, get8(REC + 0x7C));
        }
    }
    printf("test_seat_diff spawn: %d cases, %d differences; %d seats, %d parachutes spawned\n", N, d, seats, chutes);
    (void)dying_player;
    return d != 0;
}

int main(int argc, char **argv) {
    const char *paths[] = {"../../extracted/interceptor.bin", "extracted/interceptor.bin"};
    (void)argc; (void)argv;
    if (rig_init(1 << 21) != 0) { printf("rig_init failed\n"); return 1; }
    for (int i = 0; i < 2 && !image_path; i++) if (rig_load_image(paths[i], BASE) == 0) image_path = paths[i];
    if (!image_path) { printf("test_seat_diff: skipped (extracted/interceptor.bin not found)\n"); rig_shutdown(); return 0; }
    {
        FILE *t = fopen("../../assets/VERSION", "r");
        if (!t) assets = "assets"; else fclose(t);
    }
    if (rig_ai_load(&RAI) != 0) { printf("%s: skipped (assets/tables/ai.bin not found)\n", __FILE__); rig_shutdown(); return 0; }
    if (build_env() != 0) { rig_shutdown(); return 0; }
    if (getenv("SEAT_SLOT")) SEAT = atoi(getenv("SEAT_SLOT"));
    const int N = getenv("SEAT_DIFF_N") ? atoi(getenv("SEAT_DIFF_N")) : 4000;
    if (getenv("SEAT_DIFF_SEED")) rng = (uint32_t)atoi(getenv("SEAT_DIFF_SEED")) * 2654435761u + 1;
    verbose = getenv("SEAT_DIFF_VERBOSE") != NULL;
    if (getenv("SEAT_DIFF_SPAWN")) { const int r = run_spawn(N); rig_shutdown(); return r; }
    int landed = 0, requests = 0, switches = 0, released = 0;
    for (int n = 0; n < N; n++) {
        cs_t c;
        gen(&c);
        if (rig_load_image(image_path, BASE) != 0) return 1;
        static const uint32_t patch[] = { 0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x275F6, 0x275C4 };
        for (int i = 0; i < 6; i++) put16(patch[i], 0x4E75);
        rig_poke(FLIGHT_SPAN_B_BASE, tables.span_b, FLIGHT_SPAN_B_SIZE);
        static uint8_t zero[V_HI - V_LO];
        rig_poke(V_LO, zero, sizeof zero);
        rig_hooks_clear();
        put8(0x4A95E, 1); put8(0x4A846, 1); put8(0x4A84D, 1); put16(0x4A984, 0x0840);
        put32(0x4A7B2, 0x800000); put8(0x4A83C, 1); put8(0x4A847, 1); put8(0x4A844, 1); put8(0x4A83D, c.ext); put8(0x4A85E, c.first);
        put8(0x4A86D, c.camera_flag); put16(0x4A994, c.viewed ? SEAT : 0); put16(0x4A996, c.viewed ? 0x200 * SEAT : 0);
        put16(0x4AA7A, 0x200 * SEAT); put16(0x4A97A, 0x200 * SEAT);   /* the latches the spawn sets */
        put8(0x4A8E2, c.req); put8(0x4A8E4, c.delay);
        put16(0x4AA78, 0xFFFF);
        put16(0x4AA04, 64); put16(0x4AA06, 64);
        /* the player: a parked aircraft with the chosen bits */
        put16(REC + 0x00, c.p_w0); put16(REC + 0x02, c.p_w2); put8(REC + 0x62, 0x11); put8(REC + 0x63, 0x0D); put8(REC + 0x7C, c.nib);
        for (int i = 0; i < 9; i++) { put16(REC + 0x80 + 2 * i, (i % 4 == 0) ? 16384 : 0); put16(REC + 0x92 + 2 * i, (i % 4 == 0) ? 16384 : 0); }
        put32(REC + 0x14, 64 * 4194304 + 1000000); put32(REC + 0x18, 0x300000); put32(REC + 0x1C, 64 * 4194304 + 1000000);
        /* the seat record */
        put16(RECS + 0x00, c.w0); put8(RECS + 0x62, c.cls); put16(RECS + 0x4C, c.counter);
        for (int i = 0; i < 3; i++) { put32(RECS + 0x14 + 4 * i, c.pos[i]); put32(RECS + 0x3E + 4 * i, c.vel[i]); }
        put32(RECS + 0x10, c.pos[1] >> 8);
        put16(RECS + 0x06, c.pos[0] >> 22); put16(RECS + 0x08, c.pos[2] >> 22);
        put16(RECS + 0x0C, (c.pos[0] & 0x3FFFFF) >> 8); put16(RECS + 0x0E, (c.pos[2] & 0x3FFFFF) >> 8);
        put16(RECS + 0x2C, 0xFFFF);
        put16(RECS + 0x66, c.pitch); put16(RECS + 0x68, c.heading); put16(RECS + 0x6A, c.roll);
        for (int i = 0; i < 9; i++) { put16(RECS + 0x80 + 2 * i, (i % 4 == 0) ? 16384 : 0); put16(RECS + 0x92 + 2 * i, (i % 4 == 0) ? 16384 : 0); }
        for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
        if (rig_run(0x26AF8u) != 0) { continue; }
        cases++;
        /* the port: the player as the rig left it, the seat as written */
        memset(&PLAYER, 0, sizeof PLAYER);
        PLAYER.f.cls = 0x11; PLAYER.f.w0 = (uint16_t)get16(REC) ; PLAYER.f.w2 = (uint16_t)(c.p_w2);
        PLAYER.f.v4a984 = 0x0840; PLAYER.f.v4a844 = 1; PLAYER.f.v4a847 = 1; PLAYER.f.v4a7b2 = 0x800000; PLAYER.f.v4a83c = 1;
        PLAYER.f.v4aa7a = (uint16_t)(0x200 * SEAT); E_chute_latch = (uint16_t)(0x200 * SEAT);
        PLAYER.f.v4a994 = (uint16_t)(c.viewed ? SEAT : 0); PLAYER.f.v4a996 = (uint16_t)(c.viewed ? 0x200 * SEAT : 0);
        PLAYER.f.w0 = (uint16_t)c.p_w0; PLAYER.f.gear = (uint8_t)c.nib;
        PLAYER.pos[0] = 64 * 4194304 + 1000000; PLAYER.pos[1] = 0x300000; PLAYER.pos[2] = 64 * 4194304 + 1000000;
        for (int i = 0; i < 9; i++) PLAYER.f.mat_a[i] = PLAYER.f.mat_b[i] = (i % 4 == 0) ? 16384 : 0;
        entities_init(&E, NULL, &RAI, NULL);
        entities_attach_player(&E, &PLAYER);
        PLAYER.f.v4a83d = (uint8_t)c.ext; PLAYER.f.v4aa04 = 64; PLAYER.f.v4aa06 = 64;   /* V:4AA04/06 as the rig has them */
        E.hook.aim_dir = aim_dir_hook; aim_first = c.first;
        E.flying = 1; E.sim_on = 1; E.camera_update = (uint8_t)c.camera_flag;
        E.end_request = (uint8_t)c.req; E.end_delay = (uint8_t)c.delay;
        aircraft_t *s = &E.rec[SEAT].ac;
        s->f.w0 = (uint16_t)c.w0; s->f.cls = (uint8_t)c.cls; s->f.counter = (int16_t)c.counter;
        for (int i = 0; i < 3; i++) { s->pos[i] = c.pos[i]; s->f.vel[i] = c.vel[i]; }
        s->f.alt8 = c.pos[1] >> 8; s->f.cell_x = (int16_t)(c.pos[0] >> 22); s->f.cell_z = (int16_t)(c.pos[2] >> 22);
        s->f.fine_x = (int16_t)((c.pos[0] & 0x3FFFFF) >> 8); s->f.fine_z = (int16_t)((c.pos[2] & 0x3FFFFF) >> 8);
        E.rec[SEAT].dest_cx = -1;
        s->f.pitch = (uint16_t)c.pitch; s->f.heading = (uint16_t)c.heading; s->f.roll = (uint16_t)c.roll;
        for (int i = 0; i < 9; i++) s->f.mat_a[i] = s->f.mat_b[i] = (i % 4 == 0) ? 16384 : 0;
        flight_set_env(&env);
        entities_step(&E);
        const flight_state_t *f = &s->f;
        const char *bad = NULL; long pv = 0, rv = 0;
        CHK("seat W0", f->w0 & 0xFEFFu, get16(RECS) & 0xFEFF);   /* the fresh bit: the rig's own first pass */
        CHK("counter", f->counter, get16(RECS + 0x4C));
        CHK("vel x", f->vel[0], get32(RECS + 0x3E)); CHK("vel y", f->vel[1], get32(RECS + 0x42)); CHK("vel z", f->vel[2], get32(RECS + 0x46));
        CHK("speed +6E", f->speed, get16(RECS + 0x6E));
        CHK("player W2 release", PLAYER.f.w2 & 0x0020, get16(REC + 2) & 0x0020);
        CHK("V:4A8E2", E.end_request, get8(0x4A8E2)); CHK("V:4A8E4", E.end_delay, get8(0x4A8E4));
        CHK("V:4A994", PLAYER.f.v4a994, get16(0x4A994)); CHK("V:4A996", PLAYER.f.v4a996, get16(0x4A996));
        CHK("pitch", (int16_t)f->pitch, get16(RECS + 0x66)); CHK("heading", (int16_t)f->heading, get16(RECS + 0x68)); CHK("roll", (int16_t)f->roll, get16(RECS + 0x6A));
        for (int q = 0; q < 9; q++) { char nm[16]; snprintf(nm, sizeof nm, "matB[%d]", q); CHK(nm, f->mat_b[q], get16(RECS + 0x92 + 2 * q)); }
        CHK("alt8", f->alt8, get32(RECS + 0x10)); CHK("fine x", f->fine_x, get16(RECS + 0xC)); CHK("fine z", f->fine_z, get16(RECS + 0xE)); CHK("cell +0A", f->cell_a, get8(RECS + 0xA)); CHK("cell +0B", f->cell_b, get8(RECS + 0xB));
        CHK("V:4A85E", aim_first, get8(0x4A85E)); CHK("V:4A83D", PLAYER.f.v4a83d, get8(0x4A83D));
        CHK("pos y", s->pos[1], get32(RECS + 0x18)); CHK("pos x", s->pos[0], get32(RECS + 0x14)); CHK("pos z", s->pos[2], get32(RECS + 0x1C));
        if (getenv("SEAT_DIFF_ROLL") && c.counter > 0 && c.roll != 0 && !(c.w0 & 0x8000)) printf("ROLL ext %d seatw0 %04X p_w0 %04X p_w2 %04X nib %d cls %02X counter %d camflag %d viewed %d req %d delay %d y %d vx %d vy %d vz %d pitch %d roll %d -> rig %d port %d\n", c.ext, c.w0, c.p_w0, c.p_w2, c.nib, c.cls, c.counter, c.camera_flag, c.viewed, c.req, c.delay, c.pos[1], c.vel[0], c.vel[1], c.vel[2], c.pitch, c.roll, get16(RECS + 0x6A) & 0xFFFF, (uint16_t)f->roll);
        if (get16(RECS) & 0x8000) landed++;
        if (get8(0x4A8E2) == 2 && c.req == 0) requests++;
        if (get16(0x4A994) != (c.viewed ? SEAT : 0)) switches++;
        if (get16(REC + 2) & 0x0020) released++;
        if (bad) {
            diffs++;
            if (verbose || diffs <= 10)
                printf("  case %d: %s: port %ld rig %ld  [cls %02X w0 %04X counter %d vel (%d,%d,%d) y %d | player w0 %04X w2 %04X nib %d camflag %d viewed %d req %d delay %d | ext %d roll %d pitch %d heading %d]\n", n, bad, pv, rv, c.cls, c.w0, c.counter,
                       c.vel[0], c.vel[1], c.vel[2], c.pos[1], c.p_w0, c.p_w2, c.nib, c.camera_flag, c.viewed, c.req, c.delay, c.ext, c.roll, c.pitch, c.heading);
        }
    }
    printf("test_seat_diff: %d cases (%d run in the rig and compared), %d differences; %d landed, %d made request 2, %d switched the view, %d released the player\n", N, cases, diffs, landed, requests, switches, released);
    rig_shutdown();
    return diffs != 0;
}
