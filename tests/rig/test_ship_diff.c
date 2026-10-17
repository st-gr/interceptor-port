/* tests/rig/test_ship_diff.c - differential tests of the ship volumes (src/core/flight_coll.c fl_bighull_hit and fl_ship_contact) against the original's routines in the Musashi rig
 * (docs/re/07-ai-and-weapons.md 8.2 "Models whose flag byte has bit 4" and 10.2 "Ships"; note 05 17.11).
 *
 * Parts (SHIP_DIFF_PART, default all):
 *   bighull  the gun's big-hull test 0x2B3F0 called directly: A3 -> the tracer's three position longs, V:4AA70 = the record under test, A6 = a scratch frame. The record is of class 0x20 (the carrier
 *            lists) or another class (the second ship's lists, the lists of the missiles and of template 10), with the vertex slots +0xA4.. poked (the carrier model's near level rotated by a random
 *            matrix, or random words), the fine position +0xC, +0xE, +0x10 and the unit shift +0x7D poked. The tracer is placed around the record at low and at high altitudes.
 *   ships    the contact scan 0x2B5E2 called directly for a mover (V:4AA6E) with its previous position in D2 to D4, against fl_ship_contact: a ship record after the mover (class 0x20 or another with the
 *            nibble 0x20), the mover the viewed player (its slots 0 and 3 are the vertices of the flight state), a missile that is not the viewed record (its slots are random and must not matter) or a
 *            missile that is (V:4A996 = its offset: its own slots, two passes), or the player stepping while the camera is on another record (V:4A996 non-zero: one pass, no vertex term). Compared: the returned code, the proximity flag V:4A957, B4 (the deck bits), +0x4E, W2.
 * Run from tools/m68k-rig (make test-ships). Skipped (exit 0) without extracted/interceptor.bin or the extracted assets. Environment: SHIP_DIFF_N, SHIP_DIFF_SEED, SHIP_DIFF_VERBOSE, SHIP_DIFF_PART. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/world.h"
#include "../../src/core/flight.h"
#include "../../src/core/flight_int.h"
#include "../../src/core/fx.h"

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

static uint32_t rng = 0x2545F491u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }
static int pct(int p) { return rnd(0, 99) < p; }

static const char *assets = "../../assets";
static int fs_read(void *user, const char *path, uint8_t **out, size_t *len) {
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
static void fs_release(void *user, uint8_t *buf) { (void)user; free(buf); }
static game_loader_t loader = { fs_read, fs_release, NULL };

static const char *image_path;
static world_t *WORLD;
static flight_tables_t tables;
static flight_env_t env;
static int16_t carrier_v[FLIGHT_SHIP_SLOTS][3];
static int diffs, cases, hits;
static int verbose;

static void rig_reset_state(void) {
    if (rig_load_image(image_path, BASE) != 0) { printf("image load failed\n"); exit(1); }
    static uint8_t zero[V_HI - V_LO];
    rig_poke(V_LO, zero, sizeof zero);
    rig_hooks_clear();
}

/* slots: the carrier model's near level rotated by a random matrix (60%), or random words around a hull's size */
static void make_slots(int16_t out[FLIGHT_SHIP_SLOTS][3], int16_t mat[9]) {
    fx_mat_t fm;
    fx_matrix_b(&fm, (int16_t)rnd(0, 28799), (int16_t)rnd(0, 28799), (int16_t)rnd(0, 28799));
    for (int i = 0; i < 9; i++) mat[i] = (int16_t)fm.m[i / 3][i % 3];
    const int real = pct(65);
    for (int k = 0; k < FLIGHT_SHIP_SLOTS; k++)
        for (int i = 0; i < 3; i++) {
            if (real) {
                int32_t s = 0;
                for (int j = 0; j < 3; j++) s += (int32_t)(mat[3 * i + j] >> 6) * carrier_v[k][j];
                out[k][i] = (int16_t)(s >> 8);
            } else out[k][i] = (int16_t)rnd(-500, 500);
        }
}

static void poke_slots(uint32_t rec, const int16_t s[FLIGHT_SHIP_SLOTS][3]) {
    for (int k = 0; k < FLIGHT_SHIP_SLOTS; k++) for (int a = 0; a < 3; a++) put16(rec + 0xA4 + 6 * (uint32_t)k + 2 * (uint32_t)a, s[k][a]);
}

#define SCRATCH 0x60000u
#define FRAME 0x70000u

static void part_bighull(int N) {
    for (int c = 0; c < N; c++) {
        const int r = rnd(1, 13);
        const int cls = pct(40) ? 0x20 : pct(50) ? 0x00 : pct(50) ? 0x01 : 0x12;
        const int l7d = pct(50) ? 3 : (int)rnd(0, 5);
        const int16_t fx = (int16_t)rnd(2000, 12000), fz = (int16_t)rnd(2000, 12000);
        const int alt8 = pct(60) ? (int)rnd(-50, 120) : (int)rnd(-2000, 3000);
        int16_t slots[FLIGHT_SHIP_SLOTS][3], mat[9];
        make_slots(slots, mat);
        const int near = pct(70);
        int tx = fx + (int)rnd(-700, 700), tz = fz + (int)rnd(-700, 700);
        int ty = near ? (int)rnd(-20, 70) : pct(50) ? (int)rnd(0, 3000) : (int)rnd(0, 40000);
        if (pct(50) && near) ty = alt8 + (int)rnd(-30, 30);
        if (ty < 0) ty = 0;
        const int32_t px = tx * 256 + (int)rnd(0, 255), py = ty * 256 + (int)rnd(0, 255), pz = tz * 256 + (int)rnd(0, 255);
        rig_reset_state();
        put8(RECA(r) + 0x62, cls); put8(RECA(r) + 0x7D, l7d); put16(RECA(r) + 0xC, fx); put16(RECA(r) + 0xE, fz); put32(RECA(r) + 0x10, alt8);
        poke_slots(RECA(r), slots);
        put16(0x4AA70, r);
        put32(SCRATCH, px); put32(SCRATCH + 4, py); put32(SCRATCH + 8, pz);
        for (int rr = RIG_D0; rr <= RIG_A7; rr++) if (rr != RIG_A7) rig_set_reg((rig_reg_t)rr, 0);
        rig_set_reg(RIG_A3, SCRATCH); rig_set_reg(RIG_A6, FRAME);
        if (rig_run(0x2B3F0) != 0) { printf("  bighull case %d: rig run failed (%d)\n", c, rig_last_error); diffs++; continue; }
        const int rig_hit = (int)(rig_get_reg(RIG_D7) & 0xFF);
        flight_ship_t s; memset(&s, 0, sizeof s);
        s.present = 1; s.w0 = 0x4048; s.cls = (uint8_t)cls; s.l7d = (uint8_t)l7d; s.fine_x = fx; s.fine_z = fz; s.alt8 = (int16_t)alt8;
        memcpy(s.vtx, slots, sizeof slots);
        const int32_t P[3] = { (int16_t)(px >> 8), (int32_t)((uint32_t)py >> 8), (int16_t)(pz >> 8) };
        const int port_hit = fl_bighull_hit(&env, &s, P);
        cases++; hits += rig_hit;
        if (port_hit != rig_hit) {
            diffs++;
            if (verbose || diffs <= 5) printf("  bighull case %d: class %02X l7d %d fine (%d,%d) alt8 %d tracer fine (%d,%d,%d): port %d rig %d\n", c, cls, l7d, fx, fz, alt8, P[0], P[1], P[2], port_hit, rig_hit);
        }
    }
}

static void part_ships(int N) {
    for (int c = 0; c < N; c++) {
        const int variant = (int)rnd(0, 3);   /* 0 the viewed player, 1 a missile not viewed, 2 a missile that is viewed, 3 the player stepping while another record is viewed (a seat or a missile with the tracking camera: V:4A996 names it) */
        const int player = variant == 0 || variant == 3;
        const int m = player ? 0 : (int)rnd(1, 4);
        const int viewed_rec = variant == 3 ? (int)rnd(1, 4) : 0;   /* the record the camera is on in variant 3 */
        const int s = (int)rnd(m + 1, 15) == viewed_rec && variant == 3 ? 5 + (int)rnd(0, 9) : (int)rnd(m + 1, 15);
        const int ship_cls = pct(55) ? 0x20 : (int)rnd(0x21, 0x2F);
        int16_t sslots[FLIGHT_SHIP_SLOTS][3], smat[9], mslots[FLIGHT_SHIP_SLOTS][3], mmat[9];
        make_slots(sslots, smat);
        make_slots(mslots, mmat);
        const int mcls = player ? 0x11 : 0x00;
        const int ml7d = pct(60) ? 3 : (int)rnd(0, 5), sl7d = pct(70) ? 3 : (int)rnd(0, 5);
        const int32_t cellx = 70 * 4194304, cellz = 70 * 4194304;
        const int16_t sfx = (int16_t)rnd(3000, 11000), sfz = (int16_t)rnd(3000, 11000);
        const int32_t spos[3] = { cellx + sfx * 256, 0, cellz + sfz * 256 };
        const int16_t salt = (int16_t)(pct(50) ? 0 : (int)rnd(-30, 60));
        const int32_t prev[3] = { spos[0] + (int)rnd(-700, 700) * 256, (pct(70) ? (int)rnd(-10, 90) : (int)rnd(-400, 4000)) * 256 + (int)rnd(0, 255), spos[2] + (int)rnd(-700, 700) * 256 };
        const int w2 = (int)(pct(50) ? 0x0080 : 0) | (pct(30) ? 0x8000 : 0) | (pct(30) ? 0x4000 : 0), b4 = (int)rnd(0, 255);
        const int malt = (int)rnd(-30, 200);
        rig_reset_state();
        /* the mover */
        put16(RECA(m), 0x0048); put16(RECA(m) + 2, w2); put8(RECA(m) + 4, b4); put8(RECA(m) + 0x5E, 0); put8(RECA(m) + 0x62, mcls); put8(RECA(m) + 0x7D, ml7d);
        put32(RECA(m) + 0x10, malt);
        for (int i = 0; i < 9; i++) put16(RECA(m) + 0x92 + 2 * (uint32_t)i, mmat[i]);
        poke_slots(RECA(m), mslots);
        /* the ship */
        put16(RECA(s), 0x4048); put8(RECA(s) + 0x5E, 0x0E); put8(RECA(s) + 0x62, ship_cls); put8(RECA(s) + 0x7D, sl7d);
        put32(RECA(s) + 0x14, spos[0]); put32(RECA(s) + 0x18, spos[1]); put32(RECA(s) + 0x1C, spos[2]);
        put16(RECA(s) + 0xC, sfx); put16(RECA(s) + 0xE, sfz); put32(RECA(s) + 0x10, salt);
        for (int i = 0; i < 9; i++) put16(RECA(s) + 0x92 + 2 * (uint32_t)i, smat[i]);
        poke_slots(RECA(s), sslots);
        put16(0x4AA6E, m * 0x200); put16(0x4A996, variant == 2 ? m * 0x200 : variant == 3 ? viewed_rec * 0x200 : 0);
        for (int rr = RIG_D0; rr <= RIG_A7; rr++) if (rr != RIG_A7) rig_set_reg((rig_reg_t)rr, 0);
        rig_set_reg(RIG_D2, (uint32_t)prev[0]); rig_set_reg(RIG_D3, (uint32_t)prev[1]); rig_set_reg(RIG_D4, (uint32_t)prev[2]);
        rig_set_reg(RIG_A6, FRAME);
        if (rig_run(0x2B5E2) != 0) { printf("  ships case %d: rig run failed (%d)\n", c, rig_last_error); diffs++; continue; }
        const int rig_r = (int)(rig_get_reg(RIG_D0) & 0xFFFF);
        /* the port */
        flight_state_t f; memset(&f, 0, sizeof f);
        f.w0 = 0x0048; f.w2 = (uint16_t)w2; f.b4 = (uint8_t)b4; f.group = 0; f.cls = (uint8_t)mcls; f.l7d = (uint8_t)ml7d; f.alt8 = malt;
        memcpy(f.mat_b, mmat, sizeof mmat);
        if (player) {   /* the player: the vertex slots 0 and 3 are the flight state's; in variant 3 the camera is elsewhere (V:4A996 non-zero): one test at the bare previous position */
            f.v4a996 = (uint16_t)(viewed_rec * 0x200);
            memcpy(f.vtx[0], mslots[0], sizeof f.vtx[0]); memcpy(f.vtx[3], mslots[3], sizeof f.vtx[3]);
        } else {
            f.entity = 1; f.self_off = (uint16_t)(m * 0x200); f.view_off = (uint16_t)(variant == 2 ? m * 0x200 : 0);
            memcpy(f.slot, mslots, sizeof f.slot);
        }
        flight_ship_t sh; memset(&sh, 0, sizeof sh);
        sh.present = 1; sh.w0 = 0x4048; sh.cls = (uint8_t)ship_cls; sh.group = 0x0E; sh.l7d = (uint8_t)sl7d;
        memcpy(sh.pos, spos, sizeof spos); sh.fine_x = sfx; sh.fine_z = sfz; sh.alt8 = salt;
        memcpy(sh.mat_b, smat, sizeof smat); memcpy(sh.vtx, sslots, sizeof sslots);
        f.b4 &= 0x3F;   /* the scan clears the deck bits at its entry (fl_collide does the same before the ship tests) */
        const int port_r = fl_ship_contact(&f, &env, &sh, prev);
        cases++; hits += rig_r != 0;
        const char *bad = NULL; int pv = 0, rv = 0;
        /* 0x2B5E2 is the whole collision routine: after the ships it runs the landmarks and the ground test (0x10); only the ship result 0x20 and the ship tests' own stores are compared */
        if ((port_r == 0x20) != (rig_r == 0x20)) { bad = "result"; pv = port_r; rv = rig_r; }
        else if (f.v4a957 != get8(0x4A957)) { bad = "proximity V:4A957"; pv = f.v4a957; rv = get8(0x4A957); }
        else if ((f.b4 & 0xC0) != (get8(RECA(m) + 4) & 0xC0)) { bad = "B4 deck bits"; pv = f.b4; rv = get8(RECA(m) + 4); }
        else if ((int16_t)f.deck != get16(RECA(m) + 0x4E)) { bad = "+0x4E"; pv = (int16_t)f.deck; rv = get16(RECA(m) + 0x4E); }
        else if ((f.w2 & 0xC000) != (get16(RECA(m) + 2) & 0xC000)) { bad = "W2 0xC000"; pv = f.w2; rv = get16(RECA(m) + 2) & 0xFFFF; }
        if (bad) {
            diffs++;
            if (verbose || diffs <= 5) printf("  ships case %d: variant %d mover %d ship %d class %02X: %s port %X rig %X (prev %d,%d,%d)\n", c, variant, m, s, ship_cls, bad, pv, rv, prev[0], prev[1], prev[2]);
        }
    }
}

int main(void) {
    const char *paths[] = {"../../extracted/interceptor.bin", "extracted/interceptor.bin"};
    char err[160];
    if (rig_init(1 << 21) != 0) { printf("rig_init failed\n"); return 1; }
    for (int i = 0; i < 2 && !image_path; i++) if (rig_load_image(paths[i], BASE) == 0) image_path = paths[i];
    if (!image_path) { printf("test_ship_diff: skipped (extracted/interceptor.bin not found)\n"); rig_shutdown(); return 0; }
    { FILE *t = fopen("../../assets/VERSION", "r"); if (!t) assets = "assets"; else fclose(t); }
    WORLD = world_load(&loader, err, sizeof err);
    if (!WORLD || flight_tables_load(&tables, &loader, err, sizeof err) != 0) { printf("test_ship_diff: skipped (%s)\n", err); rig_shutdown(); return 0; }
    for (int k = 0; k < FLIGHT_SHIP_SLOTS; k++) { int32_t v[3]; if (world_model_vertex(WORLD, 8, 0, (unsigned)k, v) != 0) { printf("test_ship_diff: skipped (model 8)\n"); rig_shutdown(); return 0; } for (int i = 0; i < 3; i++) carrier_v[k][i] = (int16_t)v[i]; }
    memset(&env, 0, sizeof env);
    env.tab = &tables; env.landmark_z = 1; env.no_rearm = 1; env.no_sound_system = 1; env.no_liftoff_hook = 1;
    flight_set_env(&env);
    const int N = getenv("SHIP_DIFF_N") ? atoi(getenv("SHIP_DIFF_N")) : 1500;
    if (getenv("SHIP_DIFF_SEED")) rng = (uint32_t)atoi(getenv("SHIP_DIFF_SEED")) * 2654435761u + 1;
    verbose = getenv("SHIP_DIFF_VERBOSE") != NULL;
    const char *only = getenv("SHIP_DIFF_PART");
    if (!only || !strcmp(only, "bighull")) { int d0 = diffs, c0 = cases, h0 = hits; part_bighull(N); printf("bighull: %d cases, %d hits, %d differences\n", cases - c0, hits - h0, diffs - d0); }
    if (!only || !strcmp(only, "ships")) { int d0 = diffs, c0 = cases, h0 = hits; part_ships(N); printf("ships:   %d cases, %d hits, %d differences\n", cases - c0, hits - h0, diffs - d0); }
    printf("test_ship_diff: %d cases, %d differences\n", cases, diffs);
    rig_shutdown();
    return diffs ? 1 : 0;
}
