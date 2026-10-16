/* tests/rig/test_nearground_diff.c - differential test: the port's near-ground pass (stage [90], lists_nearground, docs/re/04-scene-composition.md "Stage [90]: the near-ground pass")
 * against the original routine 0x2C0EC in the Musashi rig.
 *
 * The three callees that draw, the point sprites 0x340D4 and 0x340EA and the polygon filler 0x34A28, are replaced by logging hooks; the pass runs on random camera states written to the words it
 * reads (V:4AB2A, V:4AB2E, V:4AB30, the pitch band V:4A923, the camera matrix V:4AC90 and the last row V:4AA3C). Compared per state: the call sequence (kind, coordinates, order) with the port's
 * specks (a sprite is a speck with its pixel in D0 and D1; a triangle is the three vertices of the filler's list, the port dropping the ones whose bounding box starts below the last row, which the
 * filler rejects, and naming the case by the box), the fill block V:4A79E.. and the colour word V:4AA0C after the pass (4, 0, 0, 0xFFFF and 3, also above the height limit) and the flag V:4A85A (0).
 *
 * Environment: NG_DIFF_N (states, default 4000), NG_DIFF_SEED, NG_DIFF_VERBOSE. Skipped (exit 0) when extracted/interceptor.bin or the extracted assets (../../assets) are absent. Run from tools/m68k-rig
 * (make test-nearground). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/lists.h"
#include "../../src/core/fx.h"
#include "../../src/core/world.h"

#define BASE 0x10000u
#define PASS 0x2C0ECu
#define SPR1 0x340D4u
#define SPR2 0x340EAu
#define FILL 0x34A28u

static uint32_t rng = 0x9E3779B9u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int rnd(int lo, int hi) { return lo + (int)(xr() % (uint32_t)(hi - lo + 1)); }
static int pct(int p) { return rnd(0, 99) < p; }

static void put8(uint32_t a, int v) { uint8_t b = (uint8_t)v; rig_poke(a, &b, 1); }
static void put16(uint32_t a, int v) { uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v }; rig_poke(a, b, 2); }
static void put32(uint32_t a, int32_t v) { uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; rig_poke(a, b, 4); }
static unsigned get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return ((unsigned)b[0] << 8) | b[1]; }

static const char *root = "../../assets";
static int fs_read(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    char p[256];
    snprintf(p, sizeof p, "%s/%s", root, path);
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

typedef struct { int kind; int16_t v[6]; } ev_t;   /* kind 0: 1x1 sprite, 1: 2x1 sprite, 3: filler triangle */
static ev_t log_[512];
static int nlog;

static void hook_nop(void) {}
static void hook_spr1(void) { if (nlog < 512) { ev_t *e = &log_[nlog++]; e->kind = 0; e->v[0] = (int16_t)rig_get_reg(RIG_D0); e->v[1] = (int16_t)rig_get_reg(RIG_D1); } }
static void hook_spr2(void) { if (nlog < 512) { ev_t *e = &log_[nlog++]; e->kind = 1; e->v[0] = (int16_t)rig_get_reg(RIG_D0); e->v[1] = (int16_t)rig_get_reg(RIG_D1); } }
static void hook_fill(void) {
    if (nlog >= 512) return;
    ev_t *e = &log_[nlog++];
    e->kind = 3;
    if (get16(0x5043C) != 3) e->kind = 4;   /* anything but a triangle: reported */
    for (int i = 0; i < 6; i++) e->v[i] = (int16_t)get16(0x5043E + 2u * (unsigned)i);
}

static lists_tables_t tab;
static lists_t L;
static world_t *world;

int main(void) {
    FILE *f = fopen("../../extracted/interceptor.bin", "rb");
    if (!f) { printf("near-ground pass: skipped (extracted/interceptor.bin is absent)\n"); return 0; }
    fclose(f);
    char err[200] = "";
    if (lists_tables_load(&tab, &loader, err, sizeof err) != 0) { printf("near-ground pass: skipped (%s)\n", err); return 0; }
    world = world_load(&loader, err, sizeof err);
    if (!world) { printf("near-ground pass: skipped (world: %s)\n", err); return 0; }
    const int n = getenv("NG_DIFF_N") ? atoi(getenv("NG_DIFF_N")) : 4000;
    if (getenv("NG_DIFF_SEED")) rng = (uint32_t)atoi(getenv("NG_DIFF_SEED")) * 2654435761u | 1u;
    const int verbose = getenv("NG_DIFF_VERBOSE") != NULL;
    rig_init(0xE00000);
    rig_set_max_instructions(3000000);
    rig_load_image("../../extracted/interceptor.bin", BASE);
    rig_hook_set(SPR1, hook_spr1, 0); rig_hook_set(SPR2, hook_spr2, 0); rig_hook_set(FILL, hook_fill, 0);
    L.tab = &tab;
    long ndiff = 0, nact = 0, nspr1 = 0, nspr2 = 0, ntri = 0, nfill = 0, nblock = 0;
    for (int it = 0; it < n; it++) {
        /* a random camera: heights over all four bands' limits, offsets over the wrap, a random attitude matrix scaled by the zoom triple */
        const int band = rnd(0, 3), lim = band == 0 ? 2048 : (band == 1 ? 3456 : 4608);
        int height = pct(55) ? rnd(0, 700) : rnd(0, lim + 400);
        if (pct(5)) height = lim + (pct(50) ? 0 : 1);
        const int offx = rnd(-16384, 16383), offz = rnd(-16384, 16383);
        const int rows[3] = { 144, 167, 179 };
        const int row = rows[rnd(0, 2)];
        fx_mat_t m;
        fx_cam_ypr(&m, (uint32_t)rnd(0, 28799), (uint32_t)rnd(0, 28799), pct(50) ? 0u : (uint32_t)rnd(0, 28799));
        const int16_t triple[3] = { 0xA8, 0xFC, (int16_t)(pct(60) ? 0x80 : (pct(50) ? 0x40 : 0x20)) };
        fx_rowscale(&m, triple);
#define SET_STATE() do { \
            put16(0x4AB2A, offx); put16(0x4AB2E, offz); put32(0x4AB30, -height); put8(0x4A923, band); put16(0x4AA3C, row); \
            for (int i_ = 0; i_ < 9; i_++) put16(0x4AC90 + 2u * (unsigned)i_, m.m[i_ / 3][i_ % 3]); \
            put16(0x4A79E, 0x000F); put16(0x4A7A0, 0xFFFF); put16(0x4A7A2, 7); put16(0x4A7A4, 0); put16(0x4AA0C, 5); put16(0x4A85A, 0); } while (0)
        SET_STATE();
        nlog = 0;
        for (int r = 0; r < 16; r++) rig_set_reg((rig_reg_t)r, 0);
        char why[200] = "";
        if (rig_run(PASS) != 0) { snprintf(why, sizeof why, "rig_run failed (error %d)", rig_last_error); goto done; }
        /* the port */
        L.scan.off_x = (int16_t)offx; L.scan.off_z = (int16_t)offz; L.scan.off_y = -height; L.pitch_band = (uint8_t)band;
        lists_speck_t sp[LISTS_MAX_SPECKS];
        int16_t mw[9];
        for (int i = 0; i < 9; i++) mw[i] = m.m[i / 3][i % 3];
        const int ns = lists_nearground(&L, mw, row, sp, LISTS_MAX_SPECKS);
        /* The filler's own path, from a second run of the original pass in which only the plotters, the line routine and the edge routine are replaced by do-nothing stubs and the pc landmarks are traced: after a
         * triangle's entry of the filler (0x34A28) a hit of the edge routine 0x3508A means the general fill, a hit of the line routine 0x3455E the line case; neither is a dot or block case (sprites, not
         * traced: the 2 by 1 plotter calls the 1 by 1 one) or a triangle the filler rejected (a box with its top row below the last row, note 04 "Filling the polygon": miny > L). */
        static uint32_t tr[8192];
        size_t nt;
        {
            static const uint32_t lm[3] = { FILL, 0x3455E, 0x3508A };
            rig_hooks_clear();
            rig_hook_set(0x3455E, hook_nop, 0); rig_hook_set(0x3508A, hook_nop, 0); rig_hook_set(0x3414E, hook_nop, 0); rig_hook_set(SPR1, hook_nop, 0); rig_hook_set(SPR2, hook_nop, 0);
            SET_STATE();   /* the pass may leave words of its own behind: the second run starts from the same state */
            rig_watch_set(lm, 3); rig_trace_clear();
            for (int r = 0; r < 16; r++) rig_set_reg((rig_reg_t)r, 0);
            if (rig_run(PASS) != 0) { snprintf(why, sizeof why, "second rig_run failed (error %d)", rig_last_error); goto done; }
            nt = rig_trace_get(tr, 8192);
            rig_watch_set(NULL, 0);
            rig_hooks_clear();
            rig_hook_set(SPR1, hook_spr1, 0); rig_hook_set(SPR2, hook_spr2, 0); rig_hook_set(FILL, hook_fill, 0);
            if (nt > 8192) { snprintf(why, sizeof why, "trace overflow"); goto done; }
        }
        int exp_kind[512], k = 0, port_i = 0;
        ev_t rig_ev[512];
        {
            size_t t = 0;
            for (int i = 0; i < nlog; i++) {
                ev_t *e = &log_[i];
                if (e->kind == 4) { snprintf(why, sizeof why, "filler called with a count other than 3"); goto done; }
                if (e->kind != 3) { exp_kind[k] = e->kind; rig_ev[k++] = *e; port_i++; continue; }
                if (t >= nt || tr[t] != FILL) { snprintf(why, sizeof why, "trace: no filler entry at event %d", i); goto done; }
                t++;
                int edge = 0, line = 0;
                for (; t < nt && tr[t] != FILL; t++) { if (tr[t] == 0x3508A) edge = 1; if (tr[t] == 0x3455E) line = 1; }
                int top = e->v[1]; for (int j = 1; j < 3; j++) if (e->v[2 * j + 1] < top) top = e->v[2 * j + 1];
                if (edge) exp_kind[k] = LISTS_SPECK_FILL;
                else if (line) exp_kind[k] = LISTS_SPECK_TRI;
                else if (top > row) continue;                         /* nothing drawn: the filler rejected it, the port has no event for it */
                else exp_kind[k] = LISTS_SPECK_TRI;                   /* a dot or block case */
                rig_ev[k++] = *e;
                (void)port_i;
            }
        }
        if (k != ns) { snprintf(why, sizeof why, "rig %d events, port %d", k, ns); goto done; }
        for (int i = 0; i < ns; i++) {
            const int isspr = sp[i].kind <= LISTS_SPECK_2X1;
            if (sp[i].kind != exp_kind[i] || memcmp(sp[i].xy, rig_ev[i].v, isspr ? 4 : 12)) {
                snprintf(why, sizeof why, "event %d: rig kind %d (%d,%d,%d,%d,%d,%d), port kind %d (%d,%d,%d,%d,%d,%d)", i, exp_kind[i], rig_ev[i].v[0], rig_ev[i].v[1], rig_ev[i].v[2], rig_ev[i].v[3], rig_ev[i].v[4], rig_ev[i].v[5],
                         sp[i].kind, sp[i].xy[0], sp[i].xy[1], sp[i].xy[2], sp[i].xy[3], sp[i].xy[4], sp[i].xy[5]);
                goto done;
            }
            if (sp[i].kind == LISTS_SPECK_1X1) nspr1++; else if (sp[i].kind == LISTS_SPECK_2X1) nspr2++; else if (sp[i].kind == LISTS_SPECK_TRI) ntri++; else nfill++;
        }
        if (ns || height <= lim) nact++;
        /* the block the pass leaves, also above the height limit (the port's hook stores the same words in world.c) */
        {   /* the port's hook against the rig's words, the full pen word included */
            const lists_models_t *lm_ = world_lists_models(world);
            unsigned pm, pt; int pp; unsigned ppen;
            lm_->stage90(lm_->user);
            world_test_fill_state(&pm, &pp, &pt, &ppen);
            if (pm != get16(0x4A79E) || (unsigned)(uint16_t)pp != get16(0x4A7A0) || pt != get16(0x4A7A2) || world_test_fill_alt() != get16(0x4A7A4) || ppen != get16(0x4AA0C)) {
                snprintf(why, sizeof why, "port block %x %x %x %x pen %x, rig %04x %04x %04x %04x pen %04x", pm, (unsigned)(uint16_t)pp, pt, world_test_fill_alt(), ppen, get16(0x4A79E), get16(0x4A7A0), get16(0x4A7A2), get16(0x4A7A4), get16(0x4AA0C));
                goto done;
            }
        }
        if (get16(0x4A79E) != 4 || get16(0x4A7A0) != 0 || get16(0x4A7A2) != 0 || get16(0x4A7A4) != 0xFFFF || get16(0x4AA0C) != 3 || get16(0x4A85A) != 0) {
            snprintf(why, sizeof why, "block after: %04x %04x %04x %04x pen %04x flag %04x", get16(0x4A79E), get16(0x4A7A0), get16(0x4A7A2), get16(0x4A7A4), get16(0x4AA0C), get16(0x4A85A));
            goto done;
        }
        nblock++;
done:
        if (why[0]) {
            ndiff++;
            if (verbose || ndiff <= 5) printf("  DIFF state %d (height %d band %d off %d %d row %d): %s\n", it, height, band, offx, offz, row, why);
        }
    }
    printf("near-ground pass: %d states, %ld differences; %ld active, %ld 1x1 sprites, %ld 2x1 sprites, %ld small-shape triangles, %ld general-fill triangles compared, block after the pass right in %ld states\n",
           n, ndiff, nact, nspr1, nspr2, ntri, nfill, nblock);
    rig_shutdown();
    return ndiff ? 1 : 0;
}
