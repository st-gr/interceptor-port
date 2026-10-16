/* tests/rig/test_sky_diff.c - the port's sky polygon, side flag, zone flags and sea mesh pass against the original routines in the Musashi rig
 * (docs/re/04-scene-composition.md, "Sky fill and ground polygon", "Stage [68]: the sea mesh pass", "Verified by"). Skipped (exit 0) when
 * extracted/interceptor.bin or the extracted assets are absent.
 *
 *   polygon  0x32E5A over random camera states: the matrix V:4AC90 (the port's fx_cam_ypr with the zoom row scale, random angles, heights, zoom),
 *            V:4AB1E, the side flag bit, the selector V:4A83D, the pitch and roll words; the polygon at V:5043C, the return word, the plane counters and
 *            slot words, and the stale candidate V:4AB7E (x, y, z) are compared after every call. The candidate is NOT reset between calls in either
 *            program: the port's sky_state_t and the rig's memory carry it from call to call, as the original does.
 *   flag     0x143F2 over random pitch and roll words (bit 1 of V:4A982).
 *   zone     0x230B8 (V:4A953) and 0x230D8 (V:4A954) over random keys, altitudes, phases and octants.
 *   mesh     0x2F19C with the polygon clipper (0x28B6A / 0x28B6C, the same routine) replaced by a logging stub: every polygon (count and camera-space
 *            triples) and their order, over random positions, heights, zooms, matrices and flags; the scaled altitude is not observable and not compared.
 * Environment: SKY_DIFF_N (states per section, default 3000), SKY_DIFF_SEED. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/fx.h"
#include "../../src/core/sky.h"
#include "../../src/core/game.h"

#define BASE 0x10000u

static uint32_t rng = 0x2545F491u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }

static void put8(uint32_t a, int v) { uint8_t b = (uint8_t)v; rig_poke(a, &b, 1); }
static void put16(uint32_t a, int v) { uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v }; rig_poke(a, b, 2); }
static void put32(uint32_t a, int32_t v) { uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; rig_poke(a, b, 4); }
static unsigned get8(uint32_t a) { uint8_t b; rig_peek(a, &b, 1); return b; }
static int get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return (int16_t)(uint16_t)((b[0] << 8) | b[1]); }

static sky_tables_t tab;
static int load_tables(void)
{
    static const struct { const char *name; void *dst; size_t n; } f[] = {
        { "../../assets/tables/sea_zone.bin", tab.zone, SKY_ZONE_BYTES }, { "../../assets/tables/sea_zone2.bin", tab.zone2, SKY_ZONE2_BYTES },
        { "../../assets/tables/mesh_scan.bin", tab.scan, MESH_SCAN_BYTES }, { "../../assets/tables/mesh_far.bin", tab.far_tab, MESH_FAR_BYTES },
        { "../../assets/tables/mesh_cells.bin", tab.cells, MESH_CELLS_BYTES } };
    for (size_t i = 0; i < sizeof f / sizeof f[0]; i++) {
        FILE *fp = fopen(f[i].name, "rb");
        if (!fp) return -1;
        size_t n = fread(f[i].dst, 1, f[i].n, fp);
        fclose(fp);
        if (n != f[i].n) return -1;
    }
    return 0;
}

static long ndiff, ncase;
static int shown;
#define REPORT(...) do { ndiff++; if (shown < 12) { shown++; printf("  DIFF: " __VA_ARGS__); printf("\n"); } } while (0)

/* ---- the polygon ------------------------------------------------------------------------------------------------------------------------------- */

static void poke_matrix(const fx_mat_t *m) { for (int i = 0; i < 9; i++) put16(0x4AC90u + 2u * (unsigned)i, m->m[i / 3][i % 3]); }

static void random_matrix(fx_mat_t *m, int *pitch_word, int *roll_word, int zoom)
{
    uint32_t a, b, c;
    int16_t triple[3] = { 0xA8, 0xFC, (int16_t)zoom };
    switch (xr() % 4) {   /* some states on the boundaries and special attitudes */
    case 0: a = (uint32_t)(xr() % 28800); break;
    case 1: a = (uint32_t)(xr() % 3) * 7200 + (uint32_t)rnd(-3, 3) + 28800; a %= 28800; break;
    case 2: a = (uint32_t)(rnd(-300, 300) + 28800) % 28800; break;
    default: a = (uint32_t)(rnd(14000, 15000)); break;
    }
    b = (uint32_t)(xr() % 28800);
    switch (xr() % 4) {
    case 0: c = (uint32_t)(xr() % 28800); break;
    case 1: c = (uint32_t)(xr() % 5) * 7200 + (uint32_t)rnd(-3, 3) + 28800; c %= 28800; break;
    case 2: c = (uint32_t)(rnd(-400, 400) + 28800) % 28800; break;
    default: c = (uint32_t)(rnd(3000, 4000)); break;
    }
    fx_cam_ypr(m, a, b, c);
    fx_rowscale(m, triple);
    *pitch_word = (int)a; *roll_word = (int)c;
}

static void test_polygon(int n)
{
    sky_state_t st;
    long hist[8] = { 0 }, built = 0, none = 0, whole = 0;
    memset(&st, 0, sizeof st);
    shown = 0;
    for (int i = 0; i < n; i++) {
        fx_mat_t m;
        int pw, rw, zoom = (int)(0x20u << (xr() % 3)), cockpit = (int)(xr() % 3 != 0), inv = (int)(xr() & 1);
        int32_t cam_y = -(int32_t)(xr() % 8400000u);
        sky_poly_t p;
        int pn;
        random_matrix(&m, &pw, &rw, zoom);
        if (xr() % 8 == 0) cam_y = 0;
        poke_matrix(&m);
        put32(0x4AB1E, cam_y);
        put16(0x4A982, inv ? 2 : 0);
        put8(0x4A83D, cockpit ? 0 : 1);
        put16(0x4AB42, pw); put16(0x4AB4C, pw); put16(0x4AB4A, rw);
        if (xr() % 4 == 0) put16(0x4AB4C, (int)(xr() % 28800)), pw = get16(0x4AB4C);
        rig_set_reg(RIG_A0, 0);
        if (rig_run(0x32E5A) != 0) { printf("rig error at state %d\n", i); ndiff++; return; }
        ncase++;
        pn = sky_polygon(&st, (const int16_t *)m.m, cam_y, cockpit, inv,
                         cockpit ? get16(0x4AB42) & 0xFFFF : get16(0x4AB4C) & 0xFFFF, get16(0x4AB4A) & 0xFFFF, &p);
        {
            unsigned d0 = rig_get_reg(RIG_D0);
            int rn = d0 == 0 ? get16(0x5043C) : 0;
            if ((d0 == 0) != (pn > 0)) REPORT("state %d: rig D0 %u, port n %d", i, d0, pn);
            else if (pn > 0) {
                if (rn != pn) REPORT("state %d: vertex count rig %d port %d", i, rn, pn);
                else for (int k = 0; k < 2 * pn; k++)
                    if (get16(0x5043Eu + 2u * (unsigned)k) != p.xy[k]) { REPORT("state %d: vertex word %d rig %d port %d", i, k, get16(0x5043Eu + 2u * (unsigned)k), p.xy[k]); break; }
            }
            for (int k = 0; k < 4; k++) {
                if (get16(0x538F4u + 2u * (unsigned)k) != p.cnt[k]) { REPORT("state %d: counter %d rig %d port %d", i, k, get16(0x538F4u + 2u * (unsigned)k), p.cnt[k]); break; }
                if (p.cnt[k] && get16(0x538FCu + 2u * (unsigned)k) != p.slot[k]) { REPORT("state %d: slot word %d rig %d port %d", i, k, get16(0x538FCu + 2u * (unsigned)k), p.slot[k]); break; }
            }
            for (int k = 0; k < 3; k++)
                if (get16(0x4AB7Eu + 2u * (unsigned)k) != st.cand[k]) { REPORT("state %d: candidate %d rig %d port %d", i, k, get16(0x4AB7Eu + 2u * (unsigned)k), st.cand[k]); break; }
            hist[pn]++;
            if (pn) built++; else none++;
            if (p.whole) whole++;
        }
    }
    printf("polygon: %d states (built %ld, none %ld, whole window %ld; vertex counts 3:%ld 4:%ld 5:%ld), divide overflows %u, depth-0 rejects %u\n",
           n, built, none, whole, hist[3], hist[4], hist[5], st.div_overflows, st.z0_rejects);
}

/* ---- the side flag and the zone flags -------------------------------------------------------------------------------------------------------------- */

static void test_flag(int n)
{
    long set = 0;
    shown = 0;
    for (int i = 0; i < n; i++) {
        int pw, rw, cockpit = (int)(xr() & 1);
        switch (xr() % 3) {
        case 0: pw = (int)(xr() % 28800); rw = (int)(xr() % 28800); break;
        default: {   /* the boundaries: tenths of a degree 899..901, 2699..2701 */
            static const int edges[] = { 899, 900, 901, 2698, 2699, 2700, 2701, 0, 3599 };
            pw = edges[xr() % 9] * 8 + (int)(xr() % 8); rw = edges[xr() % 9] * 8 + (int)(xr() % 8);
        }
        }
        put16(0x4AB42, pw); put16(0x4AB4C, pw); put16(0x4AB4A, rw);
        put8(0x4A83D, cockpit ? 0 : 1);
        put16(0x4A982, 0);
        if (rig_run(0x143F2) != 0) { printf("rig error (flag)\n"); ndiff++; return; }
        ncase++;
        {
            int rf = (int)((get16(0x4A982) >> 1) & 1), pf = sky_side_flag(cockpit, pw, rw);
            if (rf != pf) REPORT("flag: pitch %d roll %d cockpit %d rig %d port %d", pw, rw, cockpit, rf, pf);
            set += pf;
        }
    }
    printf("flag: %d states (flag set in %ld)\n", n, set);
}

static void test_zone(int n)
{
    long set1 = 0, set2 = 0;
    shown = 0;
    for (int i = 0; i < n; i++) {
        int row, col, phase = (int)(xr() % 16), oct = (int)(xr() % 8);
        int32_t alt;
        uint32_t pick = xr() % 4;
        if (pick == 0) { row = rnd(10, 21); col = rnd(14, 22); }
        else if (pick == 1) { row = 17; col = 16; }
        else { static const int rows[] = { 12, 13, 14, 15, 16, 17, 18, 19 }; row = rows[xr() % 8]; col = rnd(16, 20); }
        switch (xr() % 4) { case 0: alt = rnd(0, 300); break; case 1: alt = rnd(2300, 2700); break; case 2: alt = rnd(0, 6000); break; default: alt = rnd(0, 70000); }
        put16(0x4AA00, col); put16(0x4AA02, row);
        put32(0x4AB30, -alt);
        put8(0x4A908, phase); put8(0x4A90C, oct);
        put8(0x4A953, 0xEE); put8(0x4A954, 0xEE);
        if (rig_run(0x230B8) != 0 || rig_run(0x230D8) != 0) { printf("rig error (zone)\n"); ndiff++; return; }
        ncase++;
        {
            int r1 = (int)(get8(0x4A953) != 0), r2 = (int)(get8(0x4A954) != 0);
            int p1 = sky_zone_lookup(tab.zone, SKY_ZONE_BYTES, row, col, alt, phase, oct), p2 = sky_zone_lookup(tab.zone2, SKY_ZONE2_BYTES, row, col, alt, phase, oct);
            if (r1 != p1) REPORT("zone 1: row %d col %d alt %d phase %d oct %d rig %d port %d", row, col, alt, phase, oct, r1, p1);
            if (r2 != p2) REPORT("zone 2: row %d col %d alt %d phase %d oct %d rig %d port %d", row, col, alt, phase, oct, r2, p2);
            set1 += p1; set2 += p2;
        }
    }
    printf("zone: %d lookups (flag 1 set %ld, flag 2 set %ld)\n", n, set1, set2);
}

/* ---- the mesh pass ----------------------------------------------------------------------------------------------------------------------------------- */

typedef struct { int n; int16_t v[MESH_MAX_VERTS][3]; } rig_poly_t;
static rig_poly_t rlog[200];
static int nrlog;
static void clipper_stub(void)
{
    int shift = get16(0x5103C), cnt = get16(0x5103E);
    (void)shift;
    if (nrlog < 200) {
        rlog[nrlog].n = cnt;
        for (int k = 0; k < cnt && k < MESH_MAX_VERTS; k++)
            for (int c = 0; c < 3; c++) rlog[nrlog].v[k][c] = (int16_t)get16(0x51040u + 6u * (unsigned)k + 2u * (unsigned)c);
    }
    nrlog++;
}

static void test_mesh(int n, int sweep)
{
    long polys = 0, empty = 0, withalt = 0;
    mesh_poly_t out[MESH_MAX_POLYS];
    shown = 0;
    rig_hook_set(0x28B6A, clipper_stub, 0);
    rig_hook_set(0x28B6C, clipper_stub, 0);
    for (int i = 0; i < n + sweep; i++) {
        mesh_in_t in;
        fx_mat_t m;
        int pw, rw, zoom_pick = (int)(xr() % 10), zoom;
        static const int zooms[] = { 0x80, 0x80, 0x80, 0x40, 0x20, 0x10, 1, 2, 0x60, 0x33 };
        int ext = (int)(xr() % 3 == 0), map = (int)(xr() % 8 == 0);
        int32_t x, y, z;
        zoom = zooms[zoom_pick];
        random_matrix(&m, &pw, &rw, zoom == 0x80 ? 0x80 : zoom);
        /* positions over and beyond the map; round values hit the far-test quirk; the carrier's coast cells are over-sampled */
        switch (xr() % 4) {
        case 0: x = rnd(0, (1 << 29) - 1); z = rnd(0, (1 << 29) - 1); break;
        case 1: x = (int32_t)(xr() % (1u << 29)) & ~0xFFFF; z = (int32_t)(xr() % (1u << 29)) & ~0xFFFF; break;
        case 2: x = 16 * (1 << 24) + rnd(0, 3 * (1 << 24)); z = 17 * (1 << 24) + rnd(-(1 << 24), 2 * (1 << 24)); break;   /* around cell (16, 17) */
        default: x = rnd(-(1 << 29), 1 << 30); z = rnd(-(1 << 29), 1 << 30); break;
        }
        switch (xr() % 5) { case 0: y = rnd(200, 6000); break; case 1: y = rnd(6000, 400000); break; case 2: y = rnd(400000, 8400000); break; case 3: y = rnd(1000, 60000); break; default: y = xr() % 9 ? rnd(0, 3000) : rnd(8400000, 520000000); }
        in.alt = (y + 1280 + (int32_t)(xr() % 3 ? 0 : 5000)) >> 8;   /* A: the camera height in units of 256 (eye offset included) */
        if (i >= n) {   /* the sweep: every scaled altitude from 1000 up in steps of one at the default zoom (the coarse records' gates compare A' with 4 x a 15-bit word) */
            in.alt = 1000 + (i - n) / 3; zoom = 0x80; map = 0;
            x = rnd(0, (1 << 29) - 1); z = rnd(0, (1 << 29) - 1);
        }
        in.pos[0] = x; in.pos[1] = y; in.pos[2] = z;
        for (int k = 0; k < 9; k++) in.m[k] = m.m[k / 3][k % 3];
        in.zoom = zoom; in.zoom_default = zoom == 0x80; in.map = map; in.own_alt = (int)(xr() & 1);
        in.phase = (int)(xr() % 16); in.octant = (int)(xr() % 8);
        /* rig state */
        put32(ext || map ? 0x4ACF6 : 0x4B23C + 0x14, x); put32(ext || map ? 0x4ACFA : 0x4B23C + 0x18, y); put32(ext || map ? 0x4ACFE : 0x4B23C + 0x1C, z);
        put32(0x4B23C + 0x14, x); put32(0x4B23C + 0x18, y); put32(0x4B23C + 0x1C, z);
        put16(0x4A996, 0);
        put8(0x4A83D, ext ? 1 : 0);
        poke_matrix(&m);
        put32(0x4AB30, -in.alt);
        put16(0x4AAFA, zoom);
        put8(0x4A895, in.zoom_default ? 0x80 : 0); put8(0x4A894, 0);
        put8(0x4A865, map ? 1 : 0);
        put8(0x4A954, in.own_alt ? 1 : 0); put8(0x4A868, 0);
        put8(0x4A908, in.phase); put8(0x4A90C, in.octant);
        put16(0x5103C, 0x7777);
        nrlog = 0;
        if (rig_run(0x2F19C) != 0) { printf("rig error (mesh) state %d\n", i); ndiff++; break; }
        ncase++;
        {
            int pn = mesh_compute(&tab, &in, out, MESH_MAX_POLYS, NULL), bad = 0;
            if (nrlog != pn) { REPORT("mesh %d: polygon count rig %d port %d (pos %d %d %d alt %d zoom %x map %d ext %d phase %d oct %d)", i, nrlog, pn, x, y, z, in.alt, zoom, map, ext, in.phase, in.octant); bad = 1; }
            else for (int k = 0; k < pn && !bad; k++) {
                if (rlog[k].n != out[k].n) { REPORT("mesh %d poly %d: vertex count rig %d port %d", i, k, rlog[k].n, out[k].n); bad = 1; }
                else if (memcmp(rlog[k].v, out[k].v, (size_t)out[k].n * 6)) { REPORT("mesh %d poly %d: vertices differ", i, k); bad = 1; }
            }
            if (pn == 0) empty++;
            polys += pn;
            (void)withalt;
        }
    }
    rig_hooks_clear();
    printf("mesh: %d states, %ld polygons compared (%ld states drew nothing)\n", n, polys, empty);
}

int main(void)
{
    int n = getenv("SKY_DIFF_N") ? atoi(getenv("SKY_DIFF_N")) : 3000;
    FILE *f = fopen("../../extracted/interceptor.bin", "rb");
    if (!f) { printf("test_sky_diff: extracted/interceptor.bin absent, skipped\n"); return 0; }
    fclose(f);
    if (load_tables() != 0) { printf("test_sky_diff: assets/tables/sea_zone.bin etc. absent (extractor version 14), skipped\n"); return 0; }
    if (getenv("SKY_DIFF_SEED")) rng = (uint32_t)strtoul(getenv("SKY_DIFF_SEED"), NULL, 10) * 2654435761u + 1;
    rig_init(0xE00000);
    rig_set_max_instructions(3000000);
    rig_load_image("../../extracted/interceptor.bin", BASE);
    for (int r = 0; r < 16; r++) rig_set_reg(r, 0);
    test_polygon(n);
    test_flag(n);
    test_zone(n);
    test_mesh(n, 3 * 22000);
    printf("sky: %ld cases, %ld differences\n", ncase, ndiff);
    rig_shutdown();
    return ndiff ? 1 : 0;
}
