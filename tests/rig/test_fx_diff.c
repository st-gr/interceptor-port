/* tests/rig/test_fx_diff.c - differential test: the port's fx_* against the original 68000
 * routines in the Musashi rig. Routine addresses and register conventions are those of
 * docs/re/03-3d-pipeline.md ("Routines", "State used by the pipeline", "Verified by").
 * Skipped (exit 0) when extracted/interceptor.bin is absent. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/fx.h"

#define BASE 0x10000u          /* note: flat-image base */
#define N_RUNS 200             /* inputs per routine */

/* routine entries (note, "Routines") */
#define A_SINCOS2 0x32CCEu
#define A_SINCOS1 0x32DB2u
#define A_MATRIX_A 0x32B52u
#define A_MATRIX_B 0x32BECu
#define A_CAM_YP 0x32A66u
#define A_CAM_YPR 0x32AB6u
#define A_ROLL_Q8 0x32A1Eu
#define A_ROLL_Q14 0x32A48u
#define A_ROWSCALE 0x32C84u
#define A_VISIBLE 0x22A7Au
#define A_CLIP 0x28B6Au
#define A_FILLER 0x34A28u
#define A_PROJECT 0x3377Cu
#define A_DIST 0x20884u
#define A_DISTKEY 0x2082Au
/* note 03, "Entry conventions for the differential tests": entry, address to patch with RTS */
#define A_ROTATE 0x22002u
#define A_ROTATE_END 0x2203Cu
#define A_CAMERA 0x22072u
#define A_CAMERA_END 0x220AEu
#define A_SORT 0x213AAu
/* state (note, "State used by the pipeline") */
#define S_ZOOM 0x4AAF6u
#define S_ROWLIMIT 0x4AA3Cu
#define S_LASTPT 0x4AA10u
#define S_CAMX 0x4AB2Au
#define S_CAMZ 0x4AB2Eu
#define S_CAMY 0x4AB30u
#define S_LOD 0x4AB70u
#define S_ALT3 0x4A973u
#define S_CLIPIN 0x5103Cu
#define S_CLIPOUT 0x5043Cu
#define S_VERTS 0x51040u
#define S_OBJMAT 0x4AC7Eu        /* object matrix, Q8 (note 03) */
#define S_CAMMAT 0x4AC90u        /* camera matrix, Q8 */
#define S_CAMOUT 0x4D43Cu        /* camera-space vertex table, used as the A3 destination */
#define S_SORT_KEYS 0x53818u     /* ordering keys */
#define S_SORT_IDENT 0x53870u    /* identity words 0..21, preloaded by the test */
#define S_SORT_OUT 0x538C8u      /* sorted index words */
#define S_MAT (0x4B23Cu + 0x80u)  /* record 0 +0x80: scratch matrix destination */

static uint32_t rng = 0x2545F491u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }

static int fails, total_fail;
static const char *cur;
static int counts[16], ncounts;
static const char *names[16];

static void put16(uint32_t a, int v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 2); }
static void put32(uint32_t a, int32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 4); }
static int16_t get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return (int16_t)((b[0] << 8) | b[1]); }
static int32_t get32(uint32_t a) { uint8_t b[4]; rig_peek(a, b, 4); return (int32_t)(((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]); }

static void check(int cond, const char *what, int i) {
    if (!cond) { fails++; if (fails <= 5) printf("  MISMATCH %s run %d\n", what, i); }
}
static void begin(const char *n) { cur = n; fails = 0; }
static void end(int runs) {
    printf("  %-14s %3d runs, %d mismatches\n", cur, runs, fails);
    total_fail += fails;
    names[ncounts] = cur; counts[ncounts++] = runs;
}
static int run(uint32_t addr) {
    if (rig_run(addr) != 0) return -1;
    return 0;
}
static uint32_t ang(void) { return xr() % 28800u; }   /* note: valid range 0..28799 */

static void cmp_matrix(const fx_mat_t *m, const char *what, int i) {
    for (int k = 0; k < 9; k++)
        if (get16(S_MAT + 2 * k) != m->m[k / 3][k % 3]) { check(0, what, i); return; }
}

static void d_sincos(void) {
    begin("sincos2");
    for (int i = 0; i < N_RUNS; i++) {
        uint32_t a = xr() % 3600, b = xr() % 3600;   /* tenth-degree inputs, note "Angle unit" */
        rig_set_reg(RIG_D0, a); rig_set_reg(RIG_D2, b);
        if (run(A_SINCOS2)) { check(0, "run", i); continue; }
        check((int16_t)rig_get_reg(RIG_D0) == fx_sin_tenth(a) && (int16_t)rig_get_reg(RIG_D1) == fx_cos_tenth(a) &&
              (int16_t)rig_get_reg(RIG_D2) == fx_sin_tenth(b) && (int16_t)rig_get_reg(RIG_D3) == fx_cos_tenth(b), "sincos2", i);
    }
    end(N_RUNS);
    begin("sincos1");
    for (int i = 0; i < N_RUNS; i++) {
        uint32_t a = xr() % 3600;
        rig_set_reg(RIG_D4, a);
        if (run(A_SINCOS1)) { check(0, "run", i); continue; }
        check((int16_t)rig_get_reg(RIG_D4) == fx_sin_tenth(a) && (int16_t)rig_get_reg(RIG_D5) == fx_cos_tenth(a), "sincos1", i);
    }
    end(N_RUNS);
}

/* three-angle builders: angles in D0, D2, D4, destination in A1 (note, fx_matrix_a) */
static void d_matrix(const char *name, uint32_t addr, void (*f)(fx_mat_t *, uint32_t, uint32_t, uint32_t)) {
    begin(name);
    for (int i = 0; i < N_RUNS; i++) {
        uint32_t a = ang(), b = ang(), c = ang();
        fx_mat_t m;
        rig_set_reg(RIG_D0, a); rig_set_reg(RIG_D2, b); rig_set_reg(RIG_D4, c); rig_set_reg(RIG_A1, S_MAT);
        if (run(addr)) { check(0, "run", i); continue; }
        f(&m, a, b, c);
        cmp_matrix(&m, name, i);
    }
    end(N_RUNS);
}
static void d_cam_yp(void) {
    begin("fx_cam_yp");
    for (int i = 0; i < N_RUNS; i++) {
        uint32_t a = ang(), b = ang();
        fx_mat_t m;
        rig_set_reg(RIG_D0, a); rig_set_reg(RIG_D2, b); rig_set_reg(RIG_A1, S_MAT);
        if (run(A_CAM_YP)) { check(0, "run", i); continue; }
        fx_cam_yp(&m, a, b);
        cmp_matrix(&m, "cam_yp", i);
    }
    end(N_RUNS);
}
static void d_roll(const char *name, uint32_t addr, void (*f)(fx_mat_t *, uint32_t)) {
    begin(name);
    for (int i = 0; i < N_RUNS; i++) {
        uint32_t a = ang();
        fx_mat_t m;
        rig_set_reg(RIG_D4, a); rig_set_reg(RIG_A1, S_MAT);
        if (run(addr)) { check(0, "run", i); continue; }
        f(&m, a);
        cmp_matrix(&m, name, i);
    }
    end(N_RUNS);
}
static void d_rowscale(void) {
    begin("fx_rowscale");
    for (int i = 0; i < N_RUNS; i++) {
        fx_mat_t m; int16_t sc[3];
        for (int k = 0; k < 9; k++) { m.m[k / 3][k % 3] = (int16_t)rnd(-16384, 16384); put16(S_MAT + 2 * k, m.m[k / 3][k % 3]); }
        for (int k = 0; k < 3; k++) { sc[k] = (int16_t)rnd(-512, 512); put16(S_ZOOM + 2 * k, sc[k]); }
        rig_set_reg(RIG_A1, S_MAT);
        if (run(A_ROWSCALE)) { check(0, "run", i); continue; }
        fx_rowscale(&m, sc);
        cmp_matrix(&m, "rowscale", i);
    }
    end(N_RUNS);
}

static void d_project(void) {
    begin("fx_project");
    int rejected = 0;
    for (int i = 0; i < N_RUNS; i++) {
        int32_t z = rnd(-50, 30000), x, y;   /* x, y clamped to the word range below */
        int range = z > 0 ? z + z / 8 + 2 : 100;   /* mostly inside the pyramid, some outside */
        x = rnd(-range, range); y = rnd(-range, range);
        x = x > 32767 ? 32767 : (x < -32768 ? -32768 : x);
        y = y > 32767 ? 32767 : (y < -32768 ? -32768 : y);
        put16(S_ROWLIMIT, 0x7FFF);
        rig_set_reg(RIG_D0, (uint32_t)x); rig_set_reg(RIG_D1, (uint32_t)y); rig_set_reg(RIG_D2, (uint32_t)z);
        if (run(A_PROJECT)) { check(0, "run", i); continue; }
        int16_t sx, sy; fx_vec_t v = {x, y, z};
        int ok = fx_project(&sx, &sy, &v);
        int orig_ok = get32(S_LASTPT) != -1;
        check(ok == orig_ok, "project accept", i);
        if (ok && orig_ok)
            check(get16(S_LASTPT) == sx && get16(S_LASTPT + 2) == sy &&
                  (int16_t)rig_get_reg(RIG_D0) == sx && (int16_t)rig_get_reg(RIG_D1) == sy, "project xy", i);
        if (!ok) rejected++;
    }
    printf("    (%d rejected points)\n", rejected);
    end(N_RUNS);
}

static void d_clip(void) {
    begin("fx_clip");
    static const uint8_t rts[2] = {0x4E, 0x75};
    int compared = 0, produced = 0;
    rig_poke(A_FILLER, rts, 2);   /* note "Verified by": RTS over the filler's first byte */
    for (int i = 0; i < 300; i++) {
        int n = rnd(3, 6), shift = rnd(0, 2);
        fx_vec_t in[6]; int16_t out[32];
        put16(S_CLIPIN, shift); put16(S_CLIPIN + 2, n);
        for (int k = 0; k < n; k++) {   /* note "Open questions": fuzz stayed within +-300 (times 4) */
            in[k].x = rnd(-300, 300); in[k].y = rnd(-300, 300); in[k].z = rnd(-60, 300);
            put16(S_CLIPIN + 4 + 6 * k, in[k].x); put16(S_CLIPIN + 6 + 6 * k, in[k].y); put16(S_CLIPIN + 8 + 6 * k, in[k].z);
        }
        put16(S_CLIPOUT, 0);
        if (run(A_CLIP)) continue;   /* error handler on a zero divisor: not comparable */
        int n_orig = (rig_get_reg(RIG_D0) & 0xFFFF) ? get16(S_CLIPOUT) : 0;
        int n_port = fx_clip(out, 16, in, n, shift);
        compared++;
        check(n_port == n_orig, "clip count", i);
        if (n_port == n_orig) {
            produced += n_port > 0;
            for (int k = 0; k < 2 * n_port; k++) check(out[k] == get16(S_CLIPOUT + 2 + 2 * k), "clip xy", i);
        }
    }
    printf("    (%d compared, %d with a polygon)\n", compared, produced);
    check(compared > 0 && produced > 0, "clip compared nothing", 0);
    end(compared);
}

static void d_visible(void) {
    begin("fx_visible");
    for (int i = 0; i < N_RUNS; i++) {
        fx_vec_t v[3]; uint16_t flags = (uint16_t)(rnd(0, 7) << 7);   /* k in bits 7..9, bits 10..13 zero */
        for (int k = 0; k < 3; k++) {
            v[k].x = rnd(-300, 300); v[k].y = rnd(-300, 300); v[k].z = rnd(1, 600);
            put16(S_VERTS + 6 * k, v[k].x); put16(S_VERTS + 6 * k + 2, v[k].y); put16(S_VERTS + 6 * k + 4, v[k].z);
        }
        rig_set_reg(RIG_D7, flags); rig_set_reg(RIG_A3, flags);
        if (run(A_VISIBLE)) { check(0, "run", i); continue; }
        int d7 = (int)(rig_get_reg(RIG_D7) & 1);
        check(fx_visible(&v[0], &v[1], &v[2], flags) == !d7, "visible", i);
    }
    end(N_RUNS);
}

static void d_dist(void) {
    begin("fx_dist");
    for (int i = 0; i < 400; i++) {
        int32_t d2 = rnd(0, i & 1 ? 32767 : 2000), d3 = rnd(0, i & 2 ? 32767 : 2000), d4 = rnd(0, i & 4 ? 32767 : 2000);
        rig_set_reg(RIG_D2, (uint32_t)d2); rig_set_reg(RIG_D3, (uint32_t)d3); rig_set_reg(RIG_D4, (uint32_t)d4);
        if (run(A_DIST)) { check(0, "run", i); continue; }
        fx_vec_t v = {d2, d3, d4};
        check((int32_t)(rig_get_reg(RIG_D1) & 0xFFFF) == fx_dist(&v), "dist", i);
    }
    end(400);
    begin("fx_distkey");
    for (int i = 0; i < 300; i++) {
        fx_cam_off_t st = {(int16_t)rnd(-3000, 3000), (int16_t)rnd(-3000, 3000), rnd(-200000, 200000), rnd(0, 6)};
        int32_t d2 = rnd(-6000, 6000), d3 = rnd(-100000, 100000), d4 = rnd(-6000, 6000);
        put16(S_CAMX, st.off_x); put16(S_CAMZ, st.off_z); put32(S_CAMY, st.off_y); put16(S_LOD, st.shift);
        { uint8_t z = 0; rig_poke(S_ALT3, &z, 1); }
        rig_set_reg(RIG_D2, (uint32_t)d2); rig_set_reg(RIG_D3, (uint32_t)d3); rig_set_reg(RIG_D4, (uint32_t)d4);
        if (run(A_DISTKEY)) { check(0, "run", i); continue; }
        check((int32_t)(rig_get_reg(RIG_D1) & 0xFFFF) == fx_distkey(&st, d2, d3, d4), "distkey", i);
    }
    end(300);
}


/* fx_rotate / fx_camera: patch an RTS over the first word of the instruction at the end address,
 * matrix from the fixed address, D2..D4 = x, y, z (note 03, "Entry conventions") */
static void rtsp(uint32_t a) { static const uint8_t rts[2] = {0x4E, 0x75}; rig_poke(a, rts, 2); }
static void rand_mat_vec(fx_mat_t *m, fx_vec_t *v, uint32_t mataddr, int i) {
    int lim = (i % 4 == 3) ? 32767 : 300;                 /* mostly Q8-sized, some large elements */
    int vlim = (i % 4 == 3) ? 100 : 32767;                /* large elements only with small vectors (32-bit sums) */
    for (int k = 0; k < 9; k++) { m->m[k / 3][k % 3] = (int16_t)rnd(-lim, lim); put16(mataddr + 2 * k, m->m[k / 3][k % 3]); }
    v->x = rnd(-vlim - 1, vlim); v->y = rnd(-vlim - 1, vlim); v->z = rnd(-vlim - 1, vlim);
    rig_set_reg(RIG_D2, (uint32_t)v->x); rig_set_reg(RIG_D3, (uint32_t)v->y); rig_set_reg(RIG_D4, (uint32_t)v->z);
}
static void d_rotate(void) {
    begin("fx_rotate");
    rtsp(A_ROTATE_END);
    for (int i = 0; i < N_RUNS; i++) {
        fx_mat_t m; fx_vec_t v, o;
        rand_mat_vec(&m, &v, S_OBJMAT, i);
        if (run(A_ROTATE)) { check(0, "run", i); continue; }
        fx_rotate(&o, &m, &v);
        /* outputs: x' in the low word of A4, y' in D6 and D7, z' in the low word of D4 */
        check((int16_t)rig_get_reg(RIG_A4) == o.x && (int16_t)rig_get_reg(RIG_D6) == o.y &&
              (int16_t)rig_get_reg(RIG_D7) == o.y && (int16_t)rig_get_reg(RIG_D4) == o.z, "rotate", i);
    }
    end(N_RUNS);
}
static void d_camera(void) {
    begin("fx_camera");
    rtsp(A_CAMERA_END);
    for (int i = 0; i < N_RUNS; i++) {
        fx_mat_t m; fx_vec_t v, o;
        rand_mat_vec(&m, &v, S_CAMMAT, i);
        rig_set_reg(RIG_A3, S_CAMOUT);
        put16(S_CAMOUT, 0x5A5A); put16(S_CAMOUT + 2, 0x5A5A); put16(S_CAMOUT + 4, 0x5A5A);
        if (run(A_CAMERA)) { check(0, "run", i); continue; }
        fx_camera(&o, &m, &v);
        check(get16(S_CAMOUT) == o.x && get16(S_CAMOUT + 2) == o.y && get16(S_CAMOUT + 4) == o.z &&
              rig_get_reg(RIG_A3) == S_CAMOUT + 6, "camera", i);
    }
    end(N_RUNS);
}
/* fx_sort: n in D7, keys at 0x53818, identity words at 0x53870 (the game writes them once; the
 * test preloads them), sorted indices from 0x538C8; valid outputs = keys that are not negative */
static void d_sort(void) {
    begin("fx_sort");
    for (int k = 0; k < 22; k++) put16(S_SORT_IDENT + 2 * k, k);
    int dropped = 0, ties = 0;
    for (int i = 0; i < N_RUNS; i++) {
        int n = rnd(2, 22);
        int16_t keys[22]; uint8_t idx[22];
        int range = (i & 1) ? 6 : 32767;                   /* narrow range forces ties */
        for (int k = 0; k < n; k++) {
            keys[k] = (int16_t)rnd(i % 5 == 0 ? -3 : 0, range);   /* some runs include negative keys */
            put16(S_SORT_KEYS + 2 * k, keys[k]);
        }
        rig_set_reg(RIG_D7, (uint32_t)n);
        if (run(A_SORT)) { check(0, "run", i); continue; }
        int m = fx_sort(idx, keys, n);
        int expect = 0;
        for (int k = 0; k < n; k++) { expect += keys[k] >= 0; if (keys[k] < 0) dropped++; }
        for (int k = 1; k < m; k++) ties += keys[idx[k]] == keys[idx[k - 1]];
        check(m == expect, "sort count", i);
        for (int k = 0; k < m; k++) check(get16(S_SORT_OUT + 2 * k) == idx[k], "sort order", i);
    }
    printf("    (%d negative keys dropped, %d tied neighbours)\n", dropped, ties);
    check(dropped > 0 && ties > 0, "sort inputs did not exercise drops or ties", 0);
    end(N_RUNS);
}

int main(void) {
    const char *paths[] = {"../../extracted/interceptor.bin", "extracted/interceptor.bin"};
    int loaded = 0;
    if (rig_init(1 << 21) != 0) { printf("rig_init failed\n"); return 1; }
    for (int i = 0; i < 2 && !loaded; i++) loaded = rig_load_image(paths[i], BASE) == 0;
    if (!loaded) { printf("test_fx_diff: skipped (extracted/interceptor.bin not found)\n"); rig_shutdown(); return 0; }
    printf("test_fx_diff:\n");
    d_sincos();
    d_matrix("fx_matrix_a", A_MATRIX_A, fx_matrix_a);
    d_matrix("fx_matrix_b", A_MATRIX_B, fx_matrix_b);
    d_cam_yp();
    d_matrix("fx_cam_ypr", A_CAM_YPR, fx_cam_ypr);
    d_roll("fx_roll_q8", A_ROLL_Q8, fx_roll_q8);
    d_roll("fx_roll_q14", A_ROLL_Q14, fx_roll_q14);
    d_rowscale();
    d_project();
    d_clip();
    d_visible();
    d_dist();
    d_rotate();
    d_camera();
    d_sort();
    rig_shutdown();
    printf("test_fx_diff: %d mismatches\n", total_fail);
    return total_fail ? 1 : 0;
}
