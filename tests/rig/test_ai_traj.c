/* tests/rig/test_ai_traj.c - the generic AI over time: one AI aircraft (record 8) chases a stationary target (record 7, a valid record the update never visits) and the port's whole
 * world update (entities_step with the flight model) is compared with the original's world update 0x26AF8 in the Musashi rig on every mapped word of the record, step by step
 * (docs/re/07-ai-and-weapons.md sections 3 and 4, note 08 8.3). The record is made by the PORT's placement (entities_spawn) and written into the rig; the player is invalid in both
 * (it is far away and not stepped). Environment: AI_TRAJ_STEPS (default 400), AI_TRAJ_CASES (default 6), AI_TRAJ_SEED, AI_TRAJ_ALL (print every step). Skipped (exit 0) without
 * extracted/interceptor.bin or the extracted assets. Run from tools/m68k-rig (make test-ai-traj). The steering normaliser is exact (100,000 vectors, note 07), so every case must agree in every step; a divergence
 * is reported with the number of steps that agreed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/flight.h"
#include "../../src/core/entities.h"
#include "rig_ai.h"
#include "../../src/core/ai.h"
#include "../../src/core/world.h"

#define BASE 0x10000u
#define V_LO 0x4A6E8u
#define V_HI 0x54EC0u
#define REC 0x4B23Cu
#define AIREC 8
#define TGT 7
#define REC8 (REC + AIREC * 0x200u)
#define RECT (REC + TGT * 0x200u)
#define REC4 REC8

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
static flight_env_t env;
static int16_t pvtx[2][4][3];   /* model vertices 0..3 (level 0) of model 4 (index 0) and model 3 (index 1): the rig's stand-in for the draw, poked into the original's record and the port's slots alike */

static int build_env(void) {
    char err[160];
    if (flight_tables_load(&tables, &loader, err, sizeof err) != 0) { printf("test_ai_traj: skipped (%s)\n", err); return -1; }
    world_t *w = world_load(&loader, err, sizeof err);
    if (!w) { printf("test_ai_traj: skipped (%s)\n", err); return -1; }
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
static void hook_speed(void) { if (get16(0x4AA6E) == AIREC * 0x200) printf("   rig speed step entry: keys %02X T %d\n", get8(REC8 + 0x65), (int8_t)get8(REC8 + 0x2B)); }
static int launches, last_agreed;
static void hook_launch(void) { launches++; }
static aircraft_t PLAYER;
static entities_t E;
static uint32_t rng = 0x9E3779B9u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }

static void put_fields(uint32_t base) {
    for (int i = 0; i < nfl; i++) {
        switch (fl[i].size) {
        case 1: put8(base + fl[i].off, fl[i].val); break;
        case 2: put16(base + fl[i].off, fl[i].val); break;
        default: put32(base + fl[i].off, fl[i].val); break;
        }
    }
}
static void entity_fields(const entity_t *r, const aircraft_t *a) {
    port_fields(a);
    F("dest_cx+2C", 0x2C, 2, r->dest_cx); F("dest_cz+2E", 0x2E, 2, r->dest_cz); F("dest_fx+30", 0x30, 2, r->dest_fx); F("dest_fz+32", 0x32, 2, r->dest_fz); F("dest_h+34", 0x34, 4, r->dest_h);
    F("target+38", 0x38, 1, r->target); F("range+4A", 0x4A, 2, (int16_t)r->range); F("flags+64", 0x64, 1, r->ai_flags); F("engage+7A", 0x7A, 1, r->engage);
}
static int compare(uint32_t base, const char *when, int s) {
    for (int i = 0; i < nfl; i++) {
        int32_t rv = fl[i].size == 1 ? get8(base + fl[i].off) & 0xFF : fl[i].size == 2 ? get16(base + fl[i].off) : get32(base + fl[i].off);
        int32_t pv = fl[i].val;
        if (fl[i].size == 1) pv &= 0xFF;
        if (fl[i].size == 2) pv = (int16_t)pv;
        if (!strcmp(fl[i].name, "W2")) { rv &= 0xFFFE; pv &= 0xFFFE; }
        if (rv != pv) {
            printf("  %s, step %d field %s: port %d rig %d (state %02X)\n", when, s, fl[i].name, pv, rv, E.rec[AIREC].ac.f.state);
            if (getenv("AI_TRAJ_ALLFIELDS")) for (int j = i + 1; j < nfl; j++) {
                int32_t rv2 = fl[j].size == 1 ? get8(base + fl[j].off) & 0xFF : fl[j].size == 2 ? get16(base + fl[j].off) : get32(base + fl[j].off), pv2 = fl[j].val;
                if (fl[j].size == 1) pv2 &= 0xFF;
                if (fl[j].size == 2) pv2 = (int16_t)pv2;
                if (rv2 != pv2) printf("      also %s: port %d rig %d\n", fl[j].name, pv2, rv2);
            }
            return 1;
        }
    }
    return 0;
}
static void write_vertices8(void) {
    const int16_t (*mv)[3] = pvtx[1];
    int mb[9];
    for (int i = 0; i < 9; i++) mb[i] = get16(REC8 + 0x92 + 2 * i) >> 6;
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 3; i++) {
            int32_t s = 0;
            for (int j = 0; j < 3; j++) s += mb[3 * i + j] * mv[k][j];
            put16(REC8 + 0xA4 + 6 * k + 2 * i, (int16_t)(s >> 8));
            E.rec[AIREC].ac.f.slot[k][i] = (int16_t)(s >> 8);   /* the port reads the record's own slots (the ground test, 05 17.11): the same words */
        }
}

/* the 16 secondary objects 0x4D23C (stride 0x20) and the kill counter V:4A963 */
static int compare_objects(int s) {
    for (int k = 0; k < ENT_OBJECTS; k++) {
        const secondary_t *o = &E.obj[k];
        const uint32_t b = 0x4D23Cu + 0x20u * (uint32_t)k;
        const int32_t pv[] = { o->w0, o->model, o->life, o->cell_x, o->cell_z, o->sub, o->fine_x, o->fine_z, o->alt8, o->pos[0], o->pos[1], o->pos[2] };
        static const struct { const char *n; uint32_t off; int sz; } W[] = { {"w0",0,2}, {"model",2,2}, {"life",4,2}, {"cell_x",6,2}, {"cell_z",8,2}, {"sub",0xA,1}, {"fine_x",0xC,2}, {"fine_z",0xE,2}, {"alt8",0x10,4}, {"x",0x14,4}, {"y",0x18,4}, {"z",0x1C,4} };
        if (!(o->w0 & 0x40) && !(get16(b) & 0x40)) continue;
        for (int i = 0; i < 12; i++) {
            int32_t rv = W[i].sz == 1 ? get8(b + W[i].off) : W[i].sz == 2 ? get16(b + W[i].off) : get32(b + W[i].off), pw = pv[i];
            if (W[i].sz == 1) { rv &= 0xFF; pw &= 0xFF; } else if (W[i].sz == 2) pw = (int16_t)pw;
            if (rv != pw) { printf("  object %d step %d field %s: port %d rig %d\n", k, s, W[i].n, pw, rv); return 1; }
        }
    }
    if (E.kills != get8(0x4A963)) { printf("  step %d kill counter V:4A963: port %d rig %d\n", s, E.kills, get8(0x4A963)); return 1; }
    return 0;
}

#ifdef AI_TRACE
#include <stdarg.h>
void ai_trace(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
#endif

/* Diagnostic: the key byte of the AI record at each entry of the speed step 0x15ED4 in the next world update, found by running the update with a growing instruction limit (the state is
 * restored from a snapshot each time; the update itself is then run normally). */
static uint8_t snap_mem[V_HI - V_LO];
static int hits_at(uint32_t n) {
    static const uint32_t w[1] = { 0x15ED4 };
    rig_poke(V_LO, snap_mem, sizeof snap_mem);
    for (int k = 0; k < 16; k++) rig_set_reg((rig_reg_t)(RIG_D0 + k), 0);
    rig_watch_set(w, 1); rig_trace_clear();
    rig_set_max_instructions(n);
    rig_run(0x26AF8u);
    uint32_t tr[4]; size_t c = rig_trace_get(tr, 4);
    rig_watch_set(NULL, 0);
    return (int)c;
}
static void probe_speed_entries(void) {
    rig_peek(V_LO, snap_mem, sizeof snap_mem);
    for (int hit = 1; hit <= 16; hit++) {
        uint32_t lo = 1, hi = 400000;
        if (hits_at(hi) < hit) break;
        while (lo < hi) { uint32_t mid = (lo + hi) / 2; if (hits_at(mid) >= hit) hi = mid; else lo = mid + 1; }
        hits_at(lo);
        printf("   speed step entry %d: record %04X keys %02X T %d repeat %02X (instruction %u)\n", hit, get16(0x4AA6E) & 0xFFFF, get8(REC8 + 0x65), (int8_t)get8(REC8 + 0x2B), get8(REC8 + 0x39), lo);
        if (getenv("AI_TRAJ_ENTRY_SCAN") && get16(0x4AA6E) == AIREC * 0x200) {   /* where the throttle byte changes after the entry: the pc at each change */
            int last = (int8_t)get8(REC8 + 0x2B);
            for (uint32_t n = lo; n < lo + 60000; n += 16) {
                static const uint32_t none[1] = { 0 };
                (void)none;
                rig_poke(V_LO, snap_mem, sizeof snap_mem);
                for (int k = 0; k < 16; k++) rig_set_reg((rig_reg_t)(RIG_D0 + k), 0);
                rig_set_max_instructions(n);
                rig_run(0x26AF8u);
                const int t = (int8_t)get8(REC8 + 0x2B);
                if (t != last) { printf("      T %d -> %d by instruction %u (pc %X, keys %02X)\n", last, t, n, rig_get_reg(RIG_PC), get8(REC8 + 0x65)); last = t; }
            }
        }
    }
    rig_poke(V_LO, snap_mem, sizeof snap_mem);
    rig_set_max_instructions(50000000);
}

static int blk(void *u, unsigned n, int16_t w[5]) { (void)u; (void)n; w[0] = 64; w[1] = 64; w[2] = 8192; w[3] = 8192; w[4] = 3000; return 0; }

/* one case: the AI record from the port's placement, written into the rig, then `steps` passes */
static int run_case(int cn, int steps, int all) {
    memset(&PLAYER, 0, sizeof PLAYER);
    PLAYER.f.cls = 0x11; PLAYER.f.w0 = 0; PLAYER.f.v4a984 = 0x0840; PLAYER.f.v4a844 = 1; PLAYER.f.v4a847 = 1; PLAYER.f.v4a7b2 = 0x800000; PLAYER.f.v4a83c = 1;
    PLAYER.pos[0] = 3 * 4194304; PLAYER.pos[2] = 3 * 4194304; PLAYER.pos[1] = 0x300000;
    entities_init(&E, NULL, &RAI, NULL);
    entities_attach_player(&E, &PLAYER);
    E.hook.position_block = blk;
    E.mode = 4; E.progress = rnd(0, 3); E.flying = 1; E.sim_on = 1; E.hostile_cap = 4;
    /* the target record 7 */
    aircraft_t *tg = &E.rec[TGT].ac;
    tg->f.w0 = 0x0040; tg->f.cls = 0x12; tg->f.cell_a = 0;
    tg->pos[0] = (64 + rnd(-1, 1)) * 4194304 + rnd(0, 4000000); tg->pos[2] = (64 + rnd(-1, 1)) * 4194304 + rnd(0, 4000000); tg->pos[1] = 0x300000 + rnd(-100000, 200000);
    tg->f.cell_x = (int16_t)(tg->pos[0] >> 22); tg->f.cell_z = (int16_t)(tg->pos[2] >> 22); tg->f.fine_x = (int16_t)((tg->pos[0] & 0x3FFFFF) >> 8); tg->f.fine_z = (int16_t)((tg->pos[2] & 0x3FFFFF) >> 8);
    tg->f.alt8 = tg->pos[1] >> 8;
    for (int i = 0; i < 9; i++) tg->f.mat_a[i] = tg->f.mat_b[i] = (i % 4 == 0) ? 16384 : 0;
    if (getenv("AI_TRAJ_ROT")) {   /* the target turned like the demo's carrier (heading 28600): its matrices have off-diagonal words, which an identity target cannot test */
        static const int16_t mb[9] = { 16368, 0, 715, 0, 16384, 0, -715, 0, 16368 }, ma[9] = { 16368, 0, -715, 0, 16384, 0, 715, 0, 16368 };
        for (int i = 0; i < 9; i++) { tg->f.mat_b[i] = mb[i]; tg->f.mat_a[i] = ma[i]; }
    }
    tg->f.speed_target = tg->f.speed = (int16_t)rnd(0, 6000);
    /* the AI record: a placement with the leader 7 */
    uint16_t w[5] = { 20 * 6, 0x12, 8, 0, 0 };
    w[3] = 0;
    if (entities_spawn(&E, w, -2, 0, 0) != 1) { printf("spawn failed\n"); return -1; }
    entity_t *r = &E.rec[AIREC];
    aircraft_t *a = &r->ac;
    a->pos[0] = (64 + rnd(-1, 1)) * 4194304 + rnd(0, 4000000); a->pos[2] = (64 + rnd(-1, 1)) * 4194304 + rnd(0, 4000000); a->pos[1] = 0x300000 + rnd(-100000, 300000);
    a->f.cell_x = (int16_t)(a->pos[0] >> 22); a->f.cell_z = (int16_t)(a->pos[2] >> 22); a->f.fine_x = (int16_t)((a->pos[0] & 0x3FFFFF) >> 8); a->f.fine_z = (int16_t)((a->pos[2] & 0x3FFFFF) >> 8);
    a->f.alt8 = a->pos[1] >> 8;
    a->f.cell_a = (uint8_t)(4 * (3 - (a->f.fine_z >> 12)) + (3 - (a->f.fine_x >> 12)));
    a->f.heading = (uint16_t)rnd(0, 28799);
    flight_set_attitude(a, 0, a->f.heading, 0);
    a->f.speed_target = a->f.speed = 0x2000;
    r->target = 0x80 + TGT;
    if (getenv("AI_TRAJ_DIVE")) {   /* a dive into the ground: a low record, nose down or up, not flown by the AI (no AI-valid bit): the ground test, the dying path, the expiry, the ground object */
        a->pos[1] = rnd(3000, 200000);
        a->pos[0] = (100 + rnd(-2, 2)) * 4194304 + rnd(0, 4000000); a->pos[2] = (100 + rnd(-2, 2)) * 4194304 + rnd(0, 4000000);   /* away from the landmarks and the carrier */
        a->f.cell_x = (int16_t)(a->pos[0] >> 22); a->f.cell_z = (int16_t)(a->pos[2] >> 22); a->f.fine_x = (int16_t)((a->pos[0] & 0x3FFFFF) >> 8); a->f.fine_z = (int16_t)((a->pos[2] & 0x3FFFFF) >> 8);
        a->f.cell_a = (uint8_t)(4 * (3 - (a->f.fine_z >> 12)) + (3 - (a->f.fine_x >> 12)));
        if (rnd(0, 3) == 0) a->pos[1] = rnd(300, 4000);
        const int pitch = rnd(0, 1) ? rnd(300, 6000) : rnd(22800, 28500);
        a->f.speed_target = a->f.speed = (int16_t)rnd(500, 6000);
        flight_set_attitude(a, (uint16_t)pitch, a->f.heading, 0);
        a->f.alt8 = a->pos[1] >> 8;
        a->f.w0 &= (uint16_t)~0x0080;
        if (rnd(0, 4) == 0) a->f.cls = 0x15;   /* the cruise missile: its height is forced to 0xB000 after every position update (note 07 4.12) */
        if (rnd(0, 5) == 0) a->f.w0 |= 0x0008;
    }
    r->dest_cx = 64; r->dest_cz = 64; r->dest_fx = 8192; r->dest_fz = 8192; r->dest_h = a->f.alt8;
    r->range = 0x7FFF;
    flight_sync(a);

    if (rig_load_image(image_path, BASE) != 0) return -1;
    static const uint32_t patch[] = { 0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x275C4 };
    for (int i = 0; i < 5; i++) put16(patch[i], 0x4E75);
    rig_poke(FLIGHT_SPAN_B_BASE, tables.span_b, FLIGHT_SPAN_B_SIZE);
    static uint8_t zero[V_HI - V_LO];
    rig_poke(V_LO, zero, sizeof zero);
    rig_hooks_clear();
    if (getenv("AI_TRAJ_KEYHOOK")) rig_hook_set(0x15ED4, hook_speed, 0);
    rig_hook_set(0x275F6, hook_launch, getenv("AI_TRAJ_LAUNCH_D0") ? (uint32_t)atoi(getenv("AI_TRAJ_LAUNCH_D0")) : 0);   /* the AI launch: counted, not performed */
    if (getenv("AI_TRAJ_DENSE")) { static uint32_t wd[128]; const uint32_t st = (uint32_t)strtoul(getenv("AI_TRAJ_DENSE"), NULL, 16); for (int q = 0; q < 128; q++) wd[q] = st + 2u * q; rig_watch_set(wd, 128); }
    if (getenv("AI_TRAJ_WATCH")) { static const uint32_t wa[] = { 0x30A7E, 0x309BC, 0x309C6, 0x309DE, 0x30BE8, 0x2D53C, 0x278F6, 0x281E2, 0x283E0 }; rig_watch_set(wa, 9); }
    put8(0x4A95E, 4); put8(0x4A95F, E.progress); put8(0x4A846, 1); put8(0x4A84D, 1); put16(0x4A984, 0x0840);
    put32(0x4A7B2, 0x800000); put8(0x4A83C, 1); put8(0x4A847, 1); put8(0x4A844, 1); put8(0x4A83D, 1);
    put16(0x4AA78, 0xFFFF);
    put16(0x4AA04, 64); put16(0x4AA06, 64);
    PLAYER.f.v4aa04 = PLAYER.f.v4aa06 = 64;
    entity_fields(&E.rec[TGT], tg); put_fields(RECT);
    entity_fields(r, a); put_fields(REC8);
    put8(REC8 + 0x71, 0xFF);
    if (all) printf("   setup: cls %02X w0 %04X w2 %04X b4 %02X y %d pitch %d speed %d gear %02X\n", a->f.cls, a->f.w0, a->f.w2, a->f.b4, a->pos[1], (int16_t)a->f.pitch, a->f.speed, a->f.gear);
    int bad = compare(REC8, "placement", 0);
    int prev_flags = 0;
    int32_t pp[3] = {0}, pv[3] = {0}, pa = 0; int pw0 = 0;
    int s;
    for (s = 1; s <= steps && !bad; s++) {
        for (int k = RIG_D0; k <= RIG_A6; k++) rig_set_reg((rig_reg_t)k, 0);
        write_vertices8();
        flight_set_env(&env);
        entities_step(&E);
        E.pass = 0;
        if (getenv("AI_TRAJ_ENTRY_AT") && s == atoi(getenv("AI_TRAJ_ENTRY_AT"))) probe_speed_entries();
        rig_trace_clear();
        if (rig_run(0x26AF8u) != 0) { printf("rig_run failed at step %d\n", s); return -1; }
        entity_fields(r, &r->ac);
        if ((getenv("AI_TRAJ_WATCH") || getenv("AI_TRAJ_DENSE")) && s >= 206) { static uint32_t tr[4096]; size_t n = rig_trace_get(tr, 4096); printf("   step %d trace (%zu):", s, n); for (size_t q = 0; q < n && q < 4096; q++) printf(" %X", tr[q]); printf("\n"); }
        if (getenv("AI_TRAJ_DEC") && s >= 200) printf("   step %d port decision %d flags %02X id %02X w0 %04X | rig flags %02X id %02X w0 %04X\n", s, ai_launch_decision(&E, AIREC), r->ai_flags, r->ac.f.id, r->ac.f.w0, get8(REC8 + 0x64), get8(REC8 + 0x63), get16(REC8) & 0xFFFF);
        if (getenv("AI_TRAJ_REC9") && s >= 204) printf("   rec9: w0 %04X cls %02X state %02X cnt %d | rec1 %04X rec5 %04X rec7 %04X\n", get16(REC + 9 * 0x200) & 0xFFFF, get8(REC + 9 * 0x200 + 0x62), get8(REC + 9 * 0x200 + 5), get16(REC + 9 * 0x200 + 0x4C), get16(REC + 0x200) & 0xFFFF, get16(REC + 5 * 0x200) & 0xFFFF, get16(REC + 7 * 0x200) & 0xFFFF);
        if (getenv("AI_TRAJ_ALT") && s >= 200) printf("   alt %d vel_y %d S %d floor %d ceiling-floor %d speedW %d\n", get32(REC8 + 0x18), get32(REC8 + 0x42), get16(REC8 + 0x6C), get16(REC8 + 0x6C) * 64, 0x800000 - get16(REC8 + 0x6C) * 64, get16(REC8 + 0x6E));
        if (all) printf("   v4ab98 %04X/%04X\n", r->ac.f.v4ab98, get16(0x4AB98) & 0xFFFF);
        if (all) printf("   T %d/%d repeat %02X/%02X fuel %d/%d W %d/%d S %d/%d\n", r->ac.f.throttle, (int8_t)get8(REC8 + 0x2B), r->ac.f.repeat, get8(REC8 + 0x39), r->ac.f.fuel, get32(REC8 + 0x72), r->ac.f.speed, get16(REC8 + 0x6E), r->ac.f.speed_target, get16(REC8 + 0x6C));
        if (all) printf("   y %d/%d vel_y %d/%d alt8 %d/%d\n", r->ac.pos[1], get32(REC8 + 0x18), r->ac.f.vel[1], get32(REC8 + 0x42), r->ac.f.alt8, get32(REC8 + 0x10));
        if (all) printf("step %3d w0 %04X/%04X w2 %04X/%04X state %02X/%02X keys %02X/%02X cnt %d/%d limit %04X/%04X flags %02X/%02X roll %d/%d Q %d/%d\n", s, r->ac.f.w0, get16(REC8) & 0xFFFF, r->ac.f.w2, get16(REC8 + 2) & 0xFFFF, r->ac.f.state, get8(REC8 + 5), r->ac.f.keys, get8(REC8 + 0x65), r->ac.f.counter, get16(REC8 + 0x4C), (uint16_t)r->ac.f.turn_limit, get16(REC8 + 0x7E) & 0xFFFF, r->ai_flags, get8(REC8 + 0x64), (int16_t)r->ac.f.roll, get16(REC8 + 0x6A), r->ac.f.rate_r, get16(REC8 + 0x5A));
        if (getenv("AI_TRAJ_THLOG")) printf("STEPEND %d port_b4 %02X rig_b4 %02X\n", s, r->ac.f.b4, get8(REC8 + 4));
        if (getenv("AI_TRAJ_RIGLOG")) { printf("RL %d %d state %02X keys %02X flags %02X w0 %04X cnt %d roll %d\n", cn, s, get8(REC8 + 5), get8(REC8 + 0x65), get8(REC8 + 0x64), get16(REC8) & 0xFFFF, get16(REC8 + 0x4C), get16(REC8 + 0x6A)); continue; }
        if (compare(REC8, "trajectory", s)) {
            bad = 1;
            printf("      PREV pos %d %d %d vel %d %d %d alt8 %d w0 %04X | RIG now pos %d %d %d vel %d %d %d w0 %04X\n", pp[0], pp[1], pp[2], pv[0], pv[1], pv[2], pa, pw0, get32(REC8 + 0x14), get32(REC8 + 0x18), get32(REC8 + 0x1C), get32(REC8 + 0x3E), get32(REC8 + 0x42), get32(REC8 + 0x46), get16(REC8) & 0xFFFF);
            printf("      W port %d S %d (rig W %d)\n", r->ac.f.speed, r->ac.f.speed_target, get16(REC8 + 0x6E));
            printf("      before: prev flags %02X; now port flags %02X id %02X range %04X eng %d b4 %02X w0 %04X | rig state %02X cnt %d flags %02X range %04X b4 %02X\n", prev_flags, r->ai_flags, r->ac.f.id, (uint16_t)r->range, r->engage, r->ac.f.b4, r->ac.f.w0, get8(REC8 + 5), get16(REC8 + 0x4C), get8(REC8 + 0x64), get16(REC8 + 0x4A) & 0xFFFF, get8(REC8 + 4));
        }
        if (!bad && (getenv("AI_TRAJ_DIVE") || getenv("AI_TRAJ_OBJECTS")) && compare_objects(s)) bad = 1;
        prev_flags = r->ai_flags;
        for (int k = 0; k < 3; k++) { pp[k] = r->ac.pos[k]; pv[k] = r->ac.f.vel[k]; } pa = r->ac.f.alt8; pw0 = r->ac.f.w0;
    }
    last_agreed = bad ? s - 1 : steps;
    printf("test_ai_traj: case %d (progress %d): %d of %d steps agree%s; %d launch calls\n", cn, E.progress, last_agreed, steps, bad ? " (diverged)" : "", launches);
    return bad ? 1 : 0;
}

int main(int argc, char **argv) {
    const char *paths[] = {"../../extracted/interceptor.bin", "extracted/interceptor.bin"};
    (void)argc; (void)argv;
    if (rig_init(1 << 21) != 0) { printf("rig_init failed\n"); return 1; }
    for (int i = 0; i < 2 && !image_path; i++) if (rig_load_image(paths[i], BASE) == 0) image_path = paths[i];
    if (!image_path) { printf("test_ai_traj: skipped (extracted/interceptor.bin not found)\n"); rig_shutdown(); return 0; }
    {
        FILE *t = fopen("../../assets/VERSION", "r");
        if (!t) assets = "assets"; else fclose(t);
    }
    if (rig_ai_load(&RAI) != 0) { printf("%s: skipped (assets/tables/ai.bin not found)\n", __FILE__); rig_shutdown(); return 0; }
    if (build_env() != 0) { rig_shutdown(); return 0; }
    const int steps = getenv("AI_TRAJ_STEPS") ? atoi(getenv("AI_TRAJ_STEPS")) : 400;
    const int cases = getenv("AI_TRAJ_CASES") ? atoi(getenv("AI_TRAJ_CASES")) : 6;
    if (getenv("AI_TRAJ_SEED")) rng = (uint32_t)atoi(getenv("AI_TRAJ_SEED")) * 2654435761u + 1;
    int bad = 0, full = 0, min_agreed = steps;
    for (int c = 0; c < cases; c++) {
        const int r = run_case(c, steps, getenv("AI_TRAJ_ALL") != NULL && (!getenv("AI_TRAJ_CASE") || atoi(getenv("AI_TRAJ_CASE")) == c));
        if (r < 0) { rig_shutdown(); return 1; }
        bad += r;
        if (!r) full++;
        if (last_agreed < min_agreed) min_agreed = last_agreed;
    }
    printf("test_ai_traj: %d of %d cases agree in all %d steps; the shortest agreement is %d steps\n", full, cases, steps, min_agreed);
    rig_shutdown();
    return bad != 0;   /* every case must agree in every step */
}
