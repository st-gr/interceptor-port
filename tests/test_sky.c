/* tests/test_sky.c - the sky polygon, the side flag, the blackout strain, the zone flags and the sea mesh pass against the worked examples of
 * docs/re/04-scene-composition.md ("Worked examples", "Stage [68]: the sea mesh pass"). The zone and mesh examples need the extracted tables
 * (assets/tables/mesh_*.bin, sea_zone*.bin) and are skipped without them; the differential test against the original is tests/rig/test_sky_diff.c. */
#include <stdio.h>
#include "test.h"
#include "../src/core/sky.h"

typedef struct { int16_t m[9]; int32_t cam_y; int pitch, roll; int n; int16_t xy[10]; int cnt[4]; } ex_t;

static const ex_t EX[] = {
    /* (pitch, roll) = (0, 0): sky rows 1 to 89 in every column */
    { { 107, 0, 128, 0, 252, 0, -98, 0, 82 }, -566501, 0, 0, 4, { 0, 89, 319, 89, 319, 0, 0, 0 }, { 0, 1, 0, 1 } },
    /* (0, +1600) */
    { { 101, -58, 120, 55, 236, 65, -98, 0, 82 }, -566501, 0, 1600, 4, { 0, 40, 319, 138, 319, 0, 0, 0 }, { 0, 1, 0, 1 } },
    /* (0, 14400): inverted, side flag 1 */
    { { -109, 0, -130, 0, -252, 0, -98, 0, 82 }, -566501, 0, 14400, 4, { 0, 89, 319, 89, 319, 179, 0, 179 }, { 0, 1, 0, 1 } },
    /* (0, +3600) */
    { { 76, -119, 90, 114, 178, 135, -98, 0, 82 }, -566501, 0, 3600, 4, { 52, 0, 265, 179, 319, 179, 319, 0 }, { 1, 0, 1, 0 } },
};

TEST(worked_examples_of_the_sky_polygon) {
    for (unsigned e = 0; e < sizeof EX / sizeof EX[0]; e++) {
        sky_state_t st = { { 0, 0, 0 }, 0, 0 };
        sky_poly_t p;
        const int flag = sky_side_flag(1, EX[e].pitch, EX[e].roll);
        int n = sky_polygon(&st, EX[e].m, EX[e].cam_y, 1, flag, EX[e].pitch, EX[e].roll, &p);
        ASSERT_EQ_INT(n, EX[e].n);
        for (int k = 0; k < 2 * n; k++) { if (p.xy[k] != EX[e].xy[k]) { printf("  example %u word %d: got %d expected %d\n", e, k, p.xy[k], EX[e].xy[k]); FAIL("vertex"); } }
        for (int k = 0; k < 4; k++) ASSERT_EQ_INT(p.cnt[k], EX[e].cnt[k]);
        ASSERT_EQ_INT(st.div_overflows, 0);
    }
}

TEST(side_flag_rule) {
    ASSERT_EQ_INT(sky_side_flag(1, 0, 14400), 1);       /* roll 1800 tenths */
    ASSERT_EQ_INT(sky_side_flag(1, 0, 3600), 0);
    ASSERT_EQ_INT(sky_side_flag(1, 14400, 0), 1);       /* pitch past the vertical, roll level */
    ASSERT_EQ_INT(sky_side_flag(1, 14400, 14400), 0);   /* both: upright again */
    ASSERT_EQ_INT(sky_side_flag(1, 0, 8 * 2700), 1);    /* roll 2700 sets the flag for pitch outside 900..2699 */
    ASSERT_EQ_INT(sky_side_flag(1, 8 * 2700, 8 * 2700), 1);   /* and in both: R = 2700 is not in 900..2699 */
    ASSERT_EQ_INT(sky_side_flag(0, 0, 14400), 0);       /* outside the cockpit the roll is 0 */
    ASSERT_EQ_INT(sky_side_flag(0, 14400, 14400), 1);
    ASSERT_EQ_INT(sky_side_flag(1, 8 * 899 + 7, 0), 0);
    ASSERT_EQ_INT(sky_side_flag(1, 8 * 900, 0), 1);
}

TEST(whole_window_above_pitch_14400_and_none_below) {
    /* looking straight up: every corner behind or beside the camera... the matrix of pitch 90 degrees up (word 21600) with heading 0 */
    sky_state_t st = { { 0, 0, 0 }, 0, 0 };
    sky_poly_t p;
    const int16_t up[9] = { 168, 0, 0, 0, 0, 252, 0, -128, 0 };   /* the camera looks along +y: the ground corners are outside every plane */
    int n = sky_polygon(&st, up, -566501, 1, 0, 21600, 0, &p);
    ASSERT_EQ_INT(n, 4);
    ASSERT_EQ_INT(p.whole, 1);
    n = sky_polygon(&st, up, -566501, 1, 0, 14400, 0, &p);   /* the pitch word must be greater than 14400: at 14400 and below, none */
    ASSERT_EQ_INT(n, 0);
    n = sky_polygon(&st, up, -566501, 1, 0, 14401, 0, &p);
    ASSERT_EQ_INT(n, 4);
}

TEST(tiny_polygon_colour_by_view_frame_and_rear_age) {
    for (uint32_t f = 0; f < 128; f++) {
        int sp = (f & 31) >= 18 && (f & 31) <= 20;
        ASSERT_EQ_INT(sky_tiny_pen(SKY_TINY_COCKPIT, -1, f), sp ? 0 : 13);
        ASSERT_EQ_INT(sky_tiny_pen(SKY_TINY_EXTERNAL, -1, f), sp ? 4 : 0);
        ASSERT_EQ_INT(sky_tiny_pen(SKY_TINY_MAP, -1, f), 3);
    }
    for (int a = 0; a < 11; a++) ASSERT_EQ_INT(sky_tiny_pen(SKY_TINY_COCKPIT, a, 100), 3);   /* the rear view's first 11 frames */
    ASSERT_EQ_INT(sky_tiny_pen(SKY_TINY_COCKPIT, 12, 106), 0);                                /* the sampled frames 266 to 268 */
    ASSERT_EQ_INT(sky_tiny_pen(SKY_TINY_COCKPIT, 20, 10), 13);                                /* then as the front view */
    ASSERT_EQ_INT(sky_tiny_pen(SKY_TINY_COCKPIT, 20, 18), 0);
    ASSERT_EQ_INT(sky_tiny_mask(SKY_TINY_COCKPIT, 1, 5), 1);
    ASSERT_EQ_INT(sky_tiny_mask(SKY_TINY_EXTERNAL, 1, 5), 1);
    ASSERT_EQ_INT(sky_tiny_mask(SKY_TINY_COCKPIT, 0, 5), 7);
    ASSERT_EQ_INT(sky_tiny_mask(SKY_TINY_COCKPIT, 0, 19), 15);
    ASSERT_EQ_INT(sky_tiny_mask(SKY_TINY_MAP, 0, 5), 15);
}

TEST(strain_cue_accumulator_and_target) {
    sky_strain_t s;
    sky_strain_reset(&s);
    ASSERT_EQ_INT(s.acc, 0);
    sky_strain_cue(&s, -461, -102);          /* the flown trigger of note 04: (-461 - 58 - 102) >> 3 = -78 */
    ASSERT_EQ_INT(s.latch, 1);
    sky_strain_cue(&s, -461, -96);           /* (-615) >> 3 = -77 */
    ASSERT_EQ_INT(s.latch, 0);
    sky_strain_cue(&s, -461, -102);
    for (int i = 0; i < 20; i++) sky_strain_second(&s);
    ASSERT_EQ_INT(s.acc, 15);                /* up to 15 */
    ASSERT_EQ_INT(sky_fade_target(&s, 1), 2);
    ASSERT_EQ_INT(sky_fade_target(&s, 0), 15);
    sky_strain_cue(&s, 0, 0);
    for (int i = 0; i < 10; i++) sky_strain_second(&s);
    ASSERT_EQ_INT(s.acc, 5);
    ASSERT_EQ_INT(sky_fade_target(&s, 1), 15);   /* 5 or less: back to 15 */
    for (int i = 0; i < 20; i++) sky_strain_second(&s);
    ASSERT_EQ_INT(s.acc, -1);                /* idle */
}

/* ---- the extracted tables ---------------------------------------------------------------------------------------------------------------------------- */

static sky_tables_t tab;
static int have_tables;
static int read_file(const char *path, void *dst, size_t n) {
    FILE *f = fopen(path, "rb");
    size_t got;
    if (!f) return -1;
    got = fread(dst, 1, n, f);
    fclose(f);
    return got == n ? 0 : -1;
}
static void load(void) {
    have_tables = read_file("assets/tables/sea_zone.bin", tab.zone, SKY_ZONE_BYTES) == 0 && read_file("assets/tables/sea_zone2.bin", tab.zone2, SKY_ZONE2_BYTES) == 0 &&
                  read_file("assets/tables/mesh_scan.bin", tab.scan, MESH_SCAN_BYTES) == 0 && read_file("assets/tables/mesh_far.bin", tab.far_tab, MESH_FAR_BYTES) == 0 &&
                  read_file("assets/tables/mesh_cells.bin", tab.cells, MESH_CELLS_BYTES) == 0;
}

TEST(zone_lookup_at_the_carrier) {
    if (!have_tables) { printf("(skipped: no assets) "); return; }
    /* the player staged in the carrier's zone (column 17, row 16; phase 9): at A = 249 the flag is 1 in all eight octants, at A = 2359 only octant 5 */
    for (int o = 0; o < 8; o++) ASSERT_EQ_INT(sky_zone_lookup(tab.zone, SKY_ZONE_BYTES, 16, 17, 249, 9, o), 1);
    for (int o = 0; o < 8; o++) ASSERT_EQ_INT(sky_zone_lookup(tab.zone, SKY_ZONE_BYTES, 16, 17, 2359, 9, o), o == 5);
    ASSERT_EQ_INT(sky_zone_lookup(tab.zone, SKY_ZONE_BYTES, 0, 0, 249, 9, 5), 0);   /* a key not in the table */
}

TEST(second_zone_table_own_cell_flag) {
    if (!have_tables) { printf("(skipped: no assets) "); return; }
    /* row 17, column 16 (V:4AA02, V:4AA00), altitude up to 2,560: phase 4 octants 3 to 6, phase 12 all */
    ASSERT_EQ_INT(sky_zone_lookup(tab.zone2, SKY_ZONE2_BYTES, 17, 16, 100, 4, 3), 1);
    ASSERT_EQ_INT(sky_zone_lookup(tab.zone2, SKY_ZONE2_BYTES, 17, 16, 100, 4, 2), 0);
    ASSERT_EQ_INT(sky_zone_lookup(tab.zone2, SKY_ZONE2_BYTES, 17, 16, 100, 12, 0), 1);
    ASSERT_EQ_INT(sky_zone_lookup(tab.zone2, SKY_ZONE2_BYTES, 17, 16, 3000, 12, 0), 0);   /* above the bound */
    ASSERT_EQ_INT(sky_zone_lookup(tab.zone2, SKY_ZONE2_BYTES, 16, 17, 100, 12, 0), 0);
}

static mesh_in_t vec_in(int32_t x) {
    mesh_in_t in;
    static const int16_t m[9] = { 63, 0, 154, 0, 252, 0, -118, 0, 48 };
    for (int i = 0; i < 9; i++) in.m[i] = m[i];
    in.pos[0] = x; in.pos[1] = 600000; in.pos[2] = 274202624;
    in.alt = 2359; in.zoom = 0x80; in.zoom_default = 1; in.map = 0; in.own_alt = 0; in.phase = 9; in.octant = 1;
    return in;
}

TEST(mesh_worked_vectors) {
    static mesh_poly_t out[MESH_MAX_POLYS];
    static const int a[] = { 14, 7, 12, 7, 4, 9, 9, 9 }, b[] = { 14, 7, 12, 7, 4, 5, 4 };
    mesh_in_t in = vec_in(295174144);
    int n;
    if (!have_tables) { printf("(skipped: no assets) "); return; }
    n = mesh_compute(&tab, &in, out, MESH_MAX_POLYS, NULL);
    ASSERT_EQ_INT(n, 8);
    for (int i = 0; i < n; i++) ASSERT_EQ_INT(out[i].n, a[i]);
    ASSERT_EQ_INT(out[0].v[0][0], 3535); ASSERT_EQ_INT(out[0].v[0][1], -9); ASSERT_EQ_INT(out[0].v[0][2], -1019);
    ASSERT_EQ_INT(out[4].v[0][0], 4270); ASSERT_EQ_INT(out[4].v[0][1], -290); ASSERT_EQ_INT(out[4].v[0][2], -596);
    in = vec_in(295174145);   /* the low word of x is not zero any more: the far test fires on the cell (-1, 0) and its alternate list is used */
    n = mesh_compute(&tab, &in, out, MESH_MAX_POLYS, NULL);
    ASSERT_EQ_INT(n, 7);
    for (int i = 0; i < n; i++) ASSERT_EQ_INT(out[i].n, b[i]);
}

TEST(mesh_scaled_altitude) {
    static mesh_poly_t out[MESH_MAX_POLYS];
    mesh_in_t in = vec_in(295174144);
    int32_t a;
    if (!have_tables) { printf("(skipped: no assets) "); return; }
    in.alt = 2359; mesh_compute(&tab, &in, out, MESH_MAX_POLYS, &a);
    ASSERT_EQ_INT(a, 2359);                       /* the default zoom: A' = A */
    in.zoom = 0x40; in.zoom_default = 0; in.alt = 2048; mesh_compute(&tab, &in, out, MESH_MAX_POLYS, &a);
    ASSERT_EQ_INT(a, 4096);                       /* at 0x40 about 2 A (the low four bits of A are lost) */
    in.zoom = 0x20; in.alt = 2048; mesh_compute(&tab, &in, out, MESH_MAX_POLYS, &a);
    ASSERT_EQ_INT(a, 8192);
}

int main(void) {
    load();
    RUN(worked_examples_of_the_sky_polygon);
    RUN(side_flag_rule);
    RUN(whole_window_above_pitch_14400_and_none_below);
    RUN(tiny_polygon_colour_by_view_frame_and_rear_age);
    RUN(strain_cue_accumulator_and_target);
    RUN(zone_lookup_at_the_carrier);
    RUN(second_zone_table_own_cell_flag);
    RUN(mesh_worked_vectors);
    RUN(mesh_scaled_altitude);
    TEST_MAIN_END();
}
