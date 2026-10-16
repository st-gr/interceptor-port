/* tests/rig/test_training_traj.c - the instructor's trajectory: the port's training_step with the flight model (flight_step_entity) against the original's
 * world update 0x26AF8 in the Musashi rig, step by step from the start state of each part (docs/re/08-game-flow.md 8.3, 8.11: "the original's instructor").
 *
 * Same convention as test_flight_diff.c and test_training_diff.c. The rig's record 4 is NOT seeded from the port: it is made by the ORIGINAL's placement run over a cleared
 * record (part 1: 0x2CE36 with the mode byte 2; part 2: the player's record written into the rig from the port's player, then 0x2CFDA), and the port's training_start
 * result is compared with it on every mapped word before the first step (the placement check). The only writes the harness adds are the ones the original's mission start
 * makes: record 4's W2 bit 0x0100 in part 1, the lead's destination word +0x2C = -1 in part 2 (6.1), and the player's indicated speed in part 2. The re-placements of the
 * sequencer (RETURN, the wrap at lesson 7) are the original's 0x2CE36 running inside the pass. Per step the port runs training_player_pass and training_step, the rig
 * rig_run(0x26AF8) after the draw pass's vertex slots are written; every mapped word of the record is compared and the first divergence (field, step, values) printed.
 * Both parts run by default, 3,000 steps each (TRAINING_TRAJ_STEPS overrides, TRAINING_TRAJ_PART=1|2 selects one); TRAINING_TRAJ_ALL=1 prints every step's state byte, key
 * byte, pitch, roll and rates for both. Skipped (exit 0) without extracted/interceptor.bin or the extracted assets. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/flight.h"
#include "../../src/core/training.h"
#include "../../src/core/world.h"

#define BASE 0x10000u
#define V_LO 0x4A6E8u
#define V_HI 0x54EC0u
#define REC 0x4B23Cu
#define REC4 (REC + 4 * 0x200u)

static void put8(uint32_t a, int v) { uint8_t b = (uint8_t)v; rig_poke(a, &b, 1); }
static void put16(uint32_t a, int v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 2); }
static void put32(uint32_t a, int32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 4); }
static int get8(uint32_t a) { uint8_t b; rig_peek(a, &b, 1); return b; }
static int get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return (int16_t)((b[0] << 8) | b[1]); }
static int32_t get32(uint32_t a) { uint8_t b[4]; rig_peek(a, b, 4); return (int32_t)(((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]); }

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
static training_t TBL;

static int build_env(void) {
    char err[160];
    if (flight_tables_load(&tables, &loader, err, sizeof err) != 0) { printf("test_training_traj: skipped (%s)\n", err); return -1; }
    if (assets_load_training(&loader, &TBL, err, sizeof err) != 0) { printf("test_training_traj: skipped (%s)\n", err); return -1; }
    world_t *w = world_load(&loader, err, sizeof err);
    if (!w) { printf("test_training_traj: skipped (%s)\n", err); return -1; }
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
static void hook_none(void) {}
static int ai_keys, ai_w0, ai_w2, ai_calls;
static int ai_state, ai_cnt;
static void hook_conv(void) { ai_state = get8(REC4 + 0x05); ai_cnt = get16(REC4 + 0x4C); ai_keys = get8(REC4 + 0x65); ai_w0 = get16(REC4) & 0xFFFF; ai_w2 = get16(REC4 + 0x02) & 0xFFFF; ai_calls++; }
static trainer_t T;
static void write_vertices(void) {
    const int16_t (*mv)[3] = pvtx[1];
    int mb[9];
    for (int i = 0; i < 9; i++) mb[i] = get16(REC4 + 0x92 + 2 * i) >> 6;
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 3; i++) {
            int32_t s = 0;
            for (int j = 0; j < 3; j++) s += mb[3 * i + j] * mv[k][j];
            put16(REC4 + 0xA4 + 6 * k + 2 * i, (int16_t)(s >> 8));
            T.inst.f.slot[k][i] = (int16_t)(s >> 8);   /* the instructor is a record: its slots are the draw's, the same words */
        }
}

static aircraft_t PLAYER;
static trainer_t T;

static void put_fields(uint32_t base) {
    for (int i = 0; i < nfl; i++) {
        switch (fl[i].size) {
        case 1: put8(base + fl[i].off, fl[i].val); break;
        case 2: put16(base + fl[i].off, fl[i].val); break;
        default: put32(base + fl[i].off, fl[i].val); break;
        }
    }
}
/* Compares the port's record 4 with the rig's on every mapped word; prints the first difference (and, with TRAINING_TRAJ_ALL, the others); returns 1 on a difference. */
static int compare(const char *when, int s) {
    for (int i = 0; i < nfl; i++) {
        int32_t rv = fl[i].size == 1 ? get8(REC4 + fl[i].off) & 0xFF : fl[i].size == 2 ? get16(REC4 + fl[i].off) : get32(REC4 + fl[i].off);
        int32_t pv = fl[i].val;
        if (fl[i].size == 1) pv &= 0xFF;
        if (fl[i].size == 2) pv = (int16_t)pv;
        if (!strcmp(fl[i].name, "W2")) { rv &= 0xFFFE; pv &= 0xFFFE; }
        if (rv != pv) {
            printf("test_training_traj: %s, step %d field %s: port %d rig %d (lesson %d state %02X)\n", when, s, fl[i].name, pv, rv, T.lesson, T.inst.f.state);
            if (getenv("TRAINING_TRAJ_ALL")) {
                for (int j = i + 1; j < nfl; j++) {
                    int32_t r2 = fl[j].size == 1 ? get8(REC4 + fl[j].off) & 0xFF : fl[j].size == 2 ? get16(REC4 + fl[j].off) : get32(REC4 + fl[j].off);
                    int32_t p2 = fl[j].val; if (fl[j].size == 1) p2 &= 0xFF; if (fl[j].size == 2) p2 = (int16_t)p2;
                    if (r2 != p2) printf("      also %s: port %d rig %d\n", fl[j].name, p2, r2);
                }
            }
            return 1;
        }
    }
    return 0;
}

/* One part: the rig's record 4 from the original's placement, the placement check, then `steps` passes. Returns the number of differing parts (0 or 1), -1 on a rig failure. */
static int run_part(int part, int steps) {
    const int all = getenv("TRAINING_TRAJ_ALL") != NULL;
    memset(&PLAYER, 0, sizeof PLAYER);
    flight_init(&PLAYER, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    PLAYER.f.v4a95e = part == 1 ? TRAINING_MODE_PART1 : TRAINING_MODE_PART2;

    if (rig_load_image(image_path, BASE) != 0) return -1;
    static const uint32_t patch[] = { 0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2 };
    for (int i = 0; i < 4; i++) put16(patch[i], 0x4E75);
    rig_poke(FLIGHT_SPAN_B_BASE, tables.span_b, FLIGHT_SPAN_B_SIZE);
    static uint8_t zero[V_HI - V_LO];
    rig_poke(V_LO, zero, sizeof zero);   /* the variables and the records: record 4 is cleared */
    rig_hooks_clear();
    rig_hook_set(0x29BC8, hook_none, 0);   /* the post routine */
    if (getenv("TRAINING_TRAJ_HOOK")) rig_hook_set(0x1DFAA, hook_conv, 0);
    /* the variables */
    put8(0x4A95E, PLAYER.f.v4a95e); put8(0x4A84B, part == 1); put8(0x4A846, 1); put8(0x4A84D, 1); put16(0x4A984, 0x0840);
    put32(0x4A7B2, 0x800000); put8(0x4A83C, 1); put8(0x4A847, 1); put8(0x4A844, 1); put8(0x4A83D, 1);
    put16(0x4A994, part == 1 ? 4 : 0);
    put8(0x4AA6C, 4);
    put16(0x4AA04, PLAYER.f.v4aa04); put16(0x4AA06, PLAYER.f.v4aa06);   /* the viewed cell: the camera's, here the player's start cell */

    /* the original's placement over the cleared record 4 */
    if (part == 2) {   /* 0x2CFDA copies the player's record: the port's player, written into the rig's record 0 */
        port_fields(&PLAYER);
        put_fields(REC);
        put8(REC + 0x71, 0xFF);
    }
    for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
    if (rig_run(part == 1 ? 0x2CE36u : 0x2CFDAu) != 0) { printf("rig_run (placement) failed\n"); return -1; }
    if (part == 2) put16(REC + 0x00, 0);               /* the player is not stepped in the rig (W0 = 0): its record only fed the placement */
    if (part == 1) put16(REC4 + 0x02, 0x0100);        /* the mission start of mode 2 sets the scripted flag (8.3) */
    else put16(REC4 + 0x2C, 0xFFFF);                   /* the formula placement leaves the destination -1 (6.1) */

    training_init(&T, &TBL);
    training_start(&T, PLAYER.f.v4a95e, &PLAYER);
    T.lesson = 0;
    port_fields(&T.inst);
    int bad = compare("placement", 0);
    if (bad && getenv("TRAINING_TRAJ_ALL") && part == 2) printf("   player pos %d %d %d head %d; rig lead %d %d %d head %d; port lead %d %d %d\n", PLAYER.pos[0], PLAYER.pos[1], PLAYER.pos[2], PLAYER.f.heading,
        get32(REC4 + 0x14), get32(REC4 + 0x18), get32(REC4 + 0x1C), get16(REC4 + 0x68), T.inst.pos[0], T.inst.pos[1], T.inst.pos[2]);
    if (part == 2) {   /* the player: not stepped in the rig (W0 = 0); its indicated speed starts the lead */
        PLAYER.f.speed = 100;
        put16(REC + 0x6E, 100);
        if (getenv("TRAINING_TRAJ_PLAYER_ACTIVE")) { put16(REC + 0x00, 0x11C8); put16(REC + 0x02, 0x0080); put8(REC + 0x62, 0x11); }
    }

    const int cont = getenv("TRAINING_TRAJ_CONT") != NULL;
    for (int s = 1; s <= steps && (!bad || cont); s++) {
        for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
        if (part == 2 && getenv("TRAINING_TRAJ_START_AT")) { const int sp = s >= atoi(getenv("TRAINING_TRAJ_START_AT")) ? 100 : 0; PLAYER.f.speed = (int16_t)sp; put16(REC + 0x6E, sp); }   /* the player moves at this step */
        write_vertices();
        training_player_pass(&T, &PLAYER);
        training_step(&T, &PLAYER);
        training_clear_outputs(&T);
        if (rig_run(0x26AF8u) != 0) { printf("rig_run failed at step %d\n", s); return -1; }
        port_fields(&T.inst);
        if (getenv("TRAINING_TRAJ_HOOK") && s <= 3) printf("   hook at step %d: calls %d state %02X cnt %d keys %02X w0 %04X w2 %04X (end of pass state %02X)\n", s, ai_calls, ai_state, ai_cnt, ai_keys, ai_w0, ai_w2, get8(REC4 + 5));
        if (all)
            printf("step %3d  T %d/%d w0 %04X/%04X cnt %d/%d z %d/%d y %d/%d port: state %02X keys %02X pitch %6d roll %6d E %5d Q %5d W %5d | rig: state %02X keys %02X pitch %6d roll %6d E %5d Q %5d W %5d\n",
                   s, T.inst.f.throttle, (int8_t)get8(REC4 + 0x2B), T.inst.f.w0, get16(REC4) & 0xFFFF, T.inst.f.counter, get16(REC4 + 0x4C), T.inst.pos[2], get32(REC4 + 0x1C), T.inst.pos[1], get32(REC4 + 0x18), T.inst.f.state, T.inst.f.keys, (int16_t)T.inst.f.pitch, (int16_t)T.inst.f.roll, T.inst.f.rate_p, T.inst.f.rate_r, T.inst.f.speed,
                   get8(REC4 + 0x05), get8(REC4 + 0x65), get16(REC4 + 0x66), get16(REC4 + 0x6A), get16(REC4 + 0x56), get16(REC4 + 0x5A), get16(REC4 + 0x6E));
        if (compare("trajectory", s)) bad = 1;
    }
    if (!bad) printf("test_training_traj: placement and %d steps of the part %d instructor, port = rig on every mapped word (lesson %d at the end)\n", steps, part, T.lesson);
    return bad;
}

int main(int argc, char **argv) {
    const char *paths[] = {"../../extracted/interceptor.bin", "extracted/interceptor.bin"};
    (void)argc; (void)argv;
    if (rig_init(1 << 21) != 0) { printf("rig_init failed\n"); return 1; }
    for (int i = 0; i < 2 && !image_path; i++) if (rig_load_image(paths[i], BASE) == 0) image_path = paths[i];
    if (!image_path) { printf("test_training_traj: skipped (extracted/interceptor.bin not found)\n"); rig_shutdown(); return 0; }
    {
        FILE *t = fopen("../../assets/VERSION", "r");
        if (!t) assets = "assets"; else fclose(t);
    }
    if (build_env() != 0) { rig_shutdown(); return 0; }
    const int steps = getenv("TRAINING_TRAJ_STEPS") ? atoi(getenv("TRAINING_TRAJ_STEPS")) : 3000;
    const int only = getenv("TRAINING_TRAJ_PART") ? atoi(getenv("TRAINING_TRAJ_PART")) : 0;
    int bad = 0;
    for (int part = 1; part <= 2; part++) {
        if (only && only != part) continue;
        const int r = run_part(part, steps);
        if (r < 0) { rig_shutdown(); return 1; }
        bad += r;
    }
    rig_shutdown();
    return bad != 0;
}
