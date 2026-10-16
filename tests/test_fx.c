/* tests/test_fx.c - every worked example of docs/re/03-3d-pipeline.md ("Worked examples"),
 * bit-exact, plus the edge cases the note describes. */
#include "test.h"
#include "../src/core/fx.h"

static void mat_eq(const fx_mat_t *m, const int16_t *e, const char *what) {
    for (int i = 0; i < 9; i++)
        if (m->m[i / 3][i % 3] != e[i]) {
            test_failures++;
            printf("FAIL %s element %d: got %d expected %d\n", what, i, m->m[i / 3][i % 3], e[i]);
            return;
        }
}

TEST(arithmetic_right_shift_assumed) { ASSERT_TRUE((-3 >> 1) == -2); }
TEST(sincos_examples) {   /* note: sincos2 / sincos1 tables (0.1 degree input) */
    ASSERT_EQ_INT(fx_sin_tenth(0), 0);     ASSERT_EQ_INT(fx_cos_tenth(0), 16384);
    ASSERT_EQ_INT(fx_sin_tenth(900), 16384); ASSERT_EQ_INT(fx_cos_tenth(900), 0);
    ASSERT_EQ_INT(fx_sin_tenth(300), 8192);  ASSERT_EQ_INT(fx_cos_tenth(300), 14189);
    ASSERT_EQ_INT(fx_sin_tenth(2700), -16384); ASSERT_EQ_INT(fx_cos_tenth(2700), 0);
    ASSERT_EQ_INT(fx_sin_tenth(1234), 13678);  ASSERT_EQ_INT(fx_cos_tenth(1234), -9019);
    ASSERT_EQ_INT(fx_sin_tenth(3599), -29);    ASSERT_EQ_INT(fx_cos_tenth(3599), 16384);
    ASSERT_EQ_INT(fx_sin_tenth(450), 11585);   ASSERT_EQ_INT(fx_cos_tenth(450), 11585);
    ASSERT_EQ_INT(fx_sin_tenth(1800), 0);      ASSERT_EQ_INT(fx_cos_tenth(1800), -16384);
    ASSERT_EQ_INT(fx_sin_tenth(3000), -14189); ASSERT_EQ_INT(fx_cos_tenth(3000), 8192);
}

TEST(sin_28800_unit_and_wrap) {
    ASSERT_EQ_INT(fx_sin(2400), 8192);        /* 300 tenths * 8 */
    ASSERT_EQ_INT(fx_cos(7200), 0);           /* 900 tenths */
    ASSERT_EQ_INT(fx_sin(28799), fx_sin_tenth(3599));   /* last valid angle */
    ASSERT_EQ_INT(fx_sin(28800), 0);          /* wraps (port extension beyond the original's domain) */
    ASSERT_EQ_INT(fx_cos(28800 + 7200), 0);
    ASSERT_EQ_INT(fx_neg_angle(0), 0);
    ASSERT_EQ_INT(fx_neg_angle(8000), 20800);
}

TEST(sin_table_properties) {   /* amplitude and symmetry, note "Angle unit and sine table" */
    for (uint32_t a = 0; a < 3600; a++) {
        if (fx_sin_tenth(a) != -fx_sin_tenth((a + 1800) % 3600)) { test_failures++; printf("FAIL antisymmetry %u\n", a); return; }
        if (fx_cos_tenth(a) != fx_sin_tenth((a + 900) % 3600)) { test_failures++; printf("FAIL cos %u\n", a); return; }
    }
}

TEST(matrix_a_examples) {
    static const uint32_t ang[3][3] = {{8000, 4000, 12000}, {14400, 2400, 0}, {28000, 100, 7000}};
    static const int16_t e[3][9] = {
        {-2941, 1423, -16055, 15969, 2463, -2706, 2180, -16135, -1829},
        {14189, 0, 8192, 0, -16384, 0, 8192, 0, -14189},
        {654, -16119, 2857, 16366, 704, 218, -337, 2845, 16131}};
    for (int i = 0; i < 3; i++) { fx_mat_t m; fx_matrix_a(&m, ang[i][0], ang[i][1], ang[i][2]); mat_eq(&m, e[i], "matrix_a"); }
}
TEST(matrix_b_examples) {
    static const uint32_t ang[3][3] = {{8000, 4000, 12000}, {14400, 2400, 0}, {28000, 100, 7000}};
    static const int16_t e[3][9] = {
        {-15300, 5440, -2180, -1423, 2463, 16135, 5685, 15257, -1829},
        {14189, 0, -8192, 0, -16384, 0, -8192, 0, -14189},
        {774, -16361, 337, 16119, 704, -2845, 2828, 467, 16131}};
    for (int i = 0; i < 3; i++) { fx_mat_t m; fx_matrix_b(&m, ang[i][0], ang[i][1], ang[i][2]); mat_eq(&m, e[i], "matrix_b"); }
}
TEST(matrix_identity_and_single_axis) {   /* note: all zero = identity; A alone is X rotation */
    fx_mat_t m;
    static const int16_t id[9] = {16384, 0, 0, 0, 16384, 0, 0, 0, 16384};
    fx_matrix_a(&m, 0, 0, 0); mat_eq(&m, id, "a identity");
    fx_matrix_b(&m, 0, 0, 0); mat_eq(&m, id, "b identity");
    fx_matrix_a(&m, 2400, 0, 0);   /* A alone: m4 = cA, m5 = sA, m7 = -sA, m8 = cA */
    ASSERT_EQ_INT(m.m[1][1], fx_cos(2400)); ASSERT_EQ_INT(m.m[1][2], fx_sin(2400));
    ASSERT_EQ_INT(m.m[2][1], -fx_sin(2400)); ASSERT_EQ_INT(m.m[2][2], fx_cos(2400));
}
TEST(matrix_b_is_near_transpose_of_a) {   /* note: within about 63/16384 for negated angles */
    uint32_t seed = 12345;
    for (int n = 0; n < 200; n++) {
        uint32_t a[3];
        for (int i = 0; i < 3; i++) { seed = seed * 1664525u + 1013904223u; a[i] = (seed >> 8) % 28800; }
        fx_mat_t ma, mb;
        fx_matrix_a(&ma, a[0], a[1], a[2]);
        fx_matrix_b(&mb, fx_neg_angle(a[0]), fx_neg_angle(a[1]), fx_neg_angle(a[2]));
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
            int d = ma.m[c][r] - mb.m[r][c];
            if (d < 0) d = -d;
            if (d > 80) { test_failures++; printf("FAIL near-transpose %d: %d\n", n, d); return; }
        }
    }
}

TEST(cam_yp_examples) {
    static const uint32_t ang[3][2] = {{8000, 4000}, {14400, 2400}, {28000, 100}};
    static const int16_t e[3][9] = {
        {164, 0, 196, -193, -45, 162, 35, -253, -29},
        {221, 0, 128, 0, -256, 0, 128, 0, -222},
        {255, 0, 5, 1, 252, -45, -5, 44, 252}};
    for (int i = 0; i < 3; i++) { fx_mat_t m; fx_cam_yp(&m, ang[i][0], ang[i][1]); mat_eq(&m, e[i], "cam_yp"); }
}
TEST(cam_ypr_examples) {
    static const uint32_t ang[3][3] = {{8000, 4000, 12000}, {14400, 2400, 0}, {28000, 100, 7000}};
    static const int16_t e[3][9] = {
        {-46, 23, -251, 249, 38, -43, 35, -252, -29},
        {221, 0, 128, 0, -256, 0, 128, 0, -222},
        {10, -251, 44, 255, 11, 3, -5, 45, 252}};
    for (int i = 0; i < 3; i++) { fx_mat_t m; fx_cam_ypr(&m, ang[i][0], ang[i][1], ang[i][2]); mat_eq(&m, e[i], "cam_ypr"); }
}
TEST(roll_examples) {
    static const int16_t q8_12000[9] = {-222, 0, 128, 0, 256, 0, -128, 0, -222};
    static const int16_t q14_12000[9] = {-14189, 0, 8192, 0, 16384, 0, -8192, 0, -14189};
    static const int16_t q8_5000[9] = {118, 0, 227, 0, 256, 0, -227, 0, 118};
    static const int16_t q14_5000[9] = {7565, 0, 14533, 0, 16384, 0, -14533, 0, 7565};
    fx_mat_t m;
    fx_roll_q8(&m, 12000);  mat_eq(&m, q8_12000, "roll_q8 12000");
    fx_roll_q14(&m, 12000); mat_eq(&m, q14_12000, "roll_q14 12000");
    fx_roll_q8(&m, 5000);   mat_eq(&m, q8_5000, "roll_q8 5000");
    fx_roll_q14(&m, 5000);  mat_eq(&m, q14_5000, "roll_q14 5000");
}

TEST(rowscale_examples) {
    static const int16_t base[9] = {100, -150, 300, 50, 500, 250, 700, 450, 900};
    static const int16_t s1[3] = {256, 256, 256}, s2[3] = {128, 256, 512}, s3[3] = {300, -200, 256};
    static const int16_t e2[9] = {50, -75, 150, 50, 500, 250, 1400, 900, 1800};
    static const int16_t e3[9] = {117, -176, 351, -40, -391, -196, 700, 450, 900};
    fx_mat_t m;
    memcpy(m.m, base, sizeof base); fx_rowscale(&m, s1); mat_eq(&m, base, "rowscale 1");
    memcpy(m.m, base, sizeof base); fx_rowscale(&m, s2); mat_eq(&m, e2, "rowscale 2");
    memcpy(m.m, base, sizeof base); fx_rowscale(&m, s3); mat_eq(&m, e3, "rowscale 3");
}

TEST(rotate_examples) {
    static const int16_t mm[9] = {181, -181, 0, 181, 181, 0, 0, 0, 256};
    static const int32_t in[3][3] = {{100, 200, 300}, {-1000, 500, 2000}, {32, -17, 9}};
    static const int32_t ex[3][3] = {{-71, 212, 300}, {-1061, -354, 2000}, {34, 10, 9}};
    fx_mat_t m; memcpy(m.m, mm, sizeof mm);
    for (int i = 0; i < 3; i++) {
        fx_vec_t v = {in[i][0], in[i][1], in[i][2]}, o;
        fx_rotate(&o, &m, &v);
        ASSERT_EQ_INT(o.x, ex[i][0]); ASSERT_EQ_INT(o.y, ex[i][1]); ASSERT_EQ_INT(o.z, ex[i][2]);
    }
}
TEST(q14_to_q8) {
    fx_mat_t a, b;
    fx_matrix_a(&a, 8000, 4000, 12000);
    fx_mat_q14_to_q8(&b, &a);
    ASSERT_EQ_INT(b.m[0][0], -2941 >> 6); ASSERT_EQ_INT(b.m[2][2], -1829 >> 6);
}
TEST(camera_examples) {
    static const int16_t mm[9] = {200, 0, -100, 0, 256, 0, 100, 0, 200};
    static const int32_t in[3][3] = {{10, 20, 300}, {-500, 40, 900}, {7, -3, 1}};
    static const int32_t ex[3][3] = {{-110, 20, 238}, {-743, 40, 507}, {5, -3, 3}};
    fx_mat_t m; memcpy(m.m, mm, sizeof mm);
    for (int i = 0; i < 3; i++) {
        fx_vec_t v = {in[i][0], in[i][1], in[i][2]}, o;
        fx_camera(&o, &m, &v);
        ASSERT_EQ_INT(o.x, ex[i][0]); ASSERT_EQ_INT(o.y, ex[i][1]); ASSERT_EQ_INT(o.z, ex[i][2]);
    }
}

TEST(project_examples) {
    static const int32_t t[][5] = {   /* x y z -> sx sy, or sx = -1 for reject */
        {0, 0, 100, 159, 90}, {50, -30, 100, 79, 117}, {-99, 89, 100, 317, 10},
        {1000, 500, 3000, 106, 75}, {-7, 3, 13, 245, 70}, {20000, -9000, 30000, 53, 117},
        {100, 0, 100, -1, 0}, {5, 5, 0, -1, 0}};
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        fx_vec_t v = {t[i][0], t[i][1], t[i][2]};
        int16_t sx, sy;
        int ok = fx_project(&sx, &sy, &v);
        if (t[i][3] < 0) { ASSERT_EQ_INT(ok, 0); }
        else { ASSERT_EQ_INT(ok, 1); ASSERT_EQ_INT(sx, t[i][3]); ASSERT_EQ_INT(sy, t[i][4]); }
    }
}
TEST(project_edges) {   /* behind the eye, row limit, each rejecting plane */
    int16_t sx, sy;
    fx_vec_t behind = {0, 0, -100}, left = {-100, 0, 100}, up = {0, 100, 100}, dn = {0, -100, 100};
    ASSERT_EQ_INT(fx_project(&sx, &sy, &behind), 0);
    ASSERT_EQ_INT(fx_project(&sx, &sy, &left), 0);
    ASSERT_EQ_INT(fx_project(&sx, &sy, &up), 0);
    ASSERT_EQ_INT(fx_project(&sx, &sy, &dn), 0);
    fx_vec_t c = {0, 0, 100};
    ASSERT_EQ_INT(fx_project_rows(&sx, &sy, &c, 89), 0);    /* row 90 > limit 89 */
    ASSERT_EQ_INT(fx_project_rows(&sx, &sy, &c, 90), 1);
}

static int clip_case(int shift, const int32_t (*v)[3], int n, int16_t *out) {
    fx_vec_t in[8];
    for (int i = 0; i < n; i++) { in[i].x = v[i][0]; in[i].y = v[i][1]; in[i].z = v[i][2]; }
    return fx_clip(out, 16, in, n, shift);
}
static void clip_expect(int shift, const int32_t (*v)[3], int n, const int16_t *e, int ne) {
    int16_t out[32];
    int got = clip_case(shift, v, n, out);
    ASSERT_EQ_INT(got, ne);
    for (int i = 0; i < 2 * ne; i++) ASSERT_EQ_INT(out[i], e[i]);
}
TEST(clip_case1) {
    static const int32_t v[][3] = {{-50, -20, 100}, {60, -30, 120}, {0, 70, 90}};
    static const int16_t e[] = {239, 107, 79, 111, 159, 19};
    clip_expect(0, v, 3, e, 3);
}
TEST(clip_case2_right_edge) {
    static const int32_t v[][3] = {{-50, 0, 100}, {150, 0, 100}, {0, 50, 100}};
    static const int16_t e[] = {239, 89, 0, 89, 0, 74, 159, 44};
    clip_expect(0, v, 3, e, 4);
}
TEST(clip_case3_all_four_planes) {
    static const int32_t v[][3] = {{-200, -30, 100}, {200, -30, 100}, {200, 60, 100}, {-200, 60, 100}};
    static const int16_t e[] = {319, 116, 0, 116, 0, 35, 319, 35};
    clip_expect(0, v, 4, e, 4);
}
TEST(clip_case4_shift) {
    static const int32_t v[][3] = {{-10, -10, 20}, {10, -10, 20}, {10, 10, 20}, {-10, 10, 20}};
    static const int16_t e[] = {239, 134, 79, 134, 79, 44, 239, 44};
    clip_expect(2, v, 4, e, 4);
}
TEST(clip_case5_point_behind_eye) {
    static const int32_t v[][3] = {{-10, -10, -50}, {10, -10, 100}, {0, 20, 100}};
    static const int16_t e[] = {191, 179, 143, 98, 159, 71, 319, 77, 319, 179};
    clip_expect(0, v, 3, e, 5);
}
TEST(clip_case6_closing_edge_vertical_slab) {
    static const int32_t v[][3] = {{20, -120, 100}, {20, 120, 100}, {-20, 120, 100}, {-20, -120, 100}};
    static const int16_t e[] = {127, 179, 127, 0, 191, 0, 191, 179};
    clip_expect(0, v, 4, e, 4);
}
TEST(clip_rejects) {
    int16_t out[32];
    static const int32_t all_behind[][3] = {{0, 0, -10}, {10, 0, -10}, {0, 10, -10}};
    ASSERT_EQ_INT(clip_case(0, all_behind, 3, out), 0);
    static const int32_t off_right[][3] = {{500, 0, 100}, {600, 0, 100}, {500, 50, 100}};
    ASSERT_EQ_INT(clip_case(0, off_right, 3, out), 0);
    fx_vec_t in[3] = {{-50, -20, 100}, {60, -30, 120}, {0, 70, 90}};
    ASSERT_EQ_INT(fx_clip(out, 2, in, 3, 0), 0);   /* output buffer too small */
    ASSERT_EQ_INT(fx_clip(out, 8, in, 2, 0), 0);   /* fewer than three vertices */
}

TEST(visible_examples) {   /* note "Three-vertex visibility": returned value is D7; fx_visible = !D7 */
    fx_vec_t a = {0, 0, 100}, b = {50, 0, 100}, c = {0, 50, 100};
    ASSERT_EQ_INT(fx_visible(&a, &b, &c, 0), 0);          /* D7 = 1 */
    ASSERT_EQ_INT(fx_visible(&a, &c, &b, 0), 1);          /* D7 = 0 */
    fx_vec_t d = {-30, 10, 200}, e = {40, 10, 150}, f = {0, -60, 180};
    ASSERT_EQ_INT(fx_visible(&d, &e, &f, 0x100), 1);      /* D7 = 0 */
    fx_vec_t g = {10, 10, 100}, h = {20, 10, 100}, i = {10, 20, 100};
    ASSERT_EQ_INT(fx_visible(&g, &h, &i, 0x80), 0);       /* D7 = 1 */
}

TEST(dist_examples) {
    static const int32_t t[][4] = {{3, 4, 0, 5}, {100, 100, 100, 172}, {1000, 0, 0, 1000},
                                   {300, 400, 1200, 1298}, {1, 2, 3, 3}, {0, 0, 0, 0}};
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        fx_vec_t v = {t[i][0], t[i][1], t[i][2]};
        ASSERT_EQ_INT(fx_dist(&v), t[i][3]);
    }
}
TEST(hypot_table_ends) {   /* note: 257 words, 16384 .. 23170, via fx_dist (lo == hi gives T[256]) */
    fx_vec_t v = {1000, 1000, 0};
    ASSERT_EQ_INT(fx_dist(&v), 1414);
    fx_vec_t w = {1000, 0, 0};
    ASSERT_EQ_INT(fx_dist(&w), 1000);
}
TEST(dist_cap) {
    fx_vec_t v = {32767, 32767, 32767};
    ASSERT_EQ_INT(fx_dist(&v), 0x7FFF);
}
TEST(distkey_examples) {
    fx_cam_off_t st = {-20, 40, -3000, 2};
    static const int32_t t[][4] = {{100, 200, 300, 38}, {-4000, 6000, 100, 412}, {0, 0, 0, 46}, {50, -70, 90, 51}};
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
        ASSERT_EQ_INT(fx_distkey(&st, t[i][0], t[i][1], t[i][2]), t[i][3]);
    fx_cam_off_t far = {0, 0, 0x7FFF0, 0};
    ASSERT_EQ_INT(fx_distkey(&far, 0, 0, 0), 0x7FFF);   /* middle component limit 0x7FFF0 */
    far.off_y = 0x7FFEF;
    ASSERT_EQ_INT(fx_distkey(&far, 0, 0, 0), 0x7FFE);
}

TEST(distkey_narrow_d3) {   /* d3 is a word: 0x10000 + 200 behaves like 200 */
    fx_cam_off_t st = {-20, 40, -3000, 2};
    ASSERT_EQ_INT(fx_distkey(&st, 100, 0x10000 + 200, 300), fx_distkey(&st, 100, 200, 300));
}
TEST(sort_examples) {
    uint8_t o[22];
    static const int16_t k1[] = {5, 40, 12, 40, 7}, k2[] = {300, 200, 100, 400, 50, 10}, k3[] = {7, 7, 7, 7};
    static const uint8_t e1[] = {1, 3, 2, 4, 0}, e2[] = {3, 0, 1, 2, 4, 5}, e3[] = {0, 1, 2, 3};
    ASSERT_EQ_INT(fx_sort(o, k1, 5), 5); ASSERT_TRUE(memcmp(o, e1, 5) == 0);
    ASSERT_EQ_INT(fx_sort(o, k2, 6), 6); ASSERT_TRUE(memcmp(o, e2, 6) == 0);
    ASSERT_EQ_INT(fx_sort(o, k3, 4), 4); ASSERT_TRUE(memcmp(o, e3, 4) == 0);
    static const int16_t k4[] = {3, -1, 9};
    ASSERT_EQ_INT(fx_sort(o, k4, 3), 2); ASSERT_EQ_INT(o[0], 2); ASSERT_EQ_INT(o[1], 0);
}

TEST(lod_table_hook) {
    ASSERT_EQ_INT(fx_lod_shift(100), 0);   /* works without a table */
    uint8_t t[240];
    for (int i = 0; i < 240; i++) t[i] = (uint8_t)(i / 16);
    fx_set_lod_table(t, 240);
    ASSERT_EQ_INT(fx_lod_shift(0), 0); ASSERT_EQ_INT(fx_lod_shift(239), 14);
    ASSERT_EQ_INT(fx_lod_shift(5000), 14);
}

int main(void) {
    RUN(arithmetic_right_shift_assumed); RUN(sincos_examples); RUN(sin_28800_unit_and_wrap); RUN(sin_table_properties);
    RUN(matrix_a_examples); RUN(matrix_b_examples); RUN(matrix_identity_and_single_axis);
    RUN(matrix_b_is_near_transpose_of_a);
    RUN(cam_yp_examples); RUN(cam_ypr_examples); RUN(roll_examples); RUN(rowscale_examples);
    RUN(rotate_examples); RUN(q14_to_q8); RUN(camera_examples);
    RUN(project_examples); RUN(project_edges);
    RUN(clip_case1); RUN(clip_case2_right_edge); RUN(clip_case3_all_four_planes); RUN(clip_case4_shift);
    RUN(clip_case5_point_behind_eye); RUN(clip_case6_closing_edge_vertical_slab); RUN(clip_rejects);
    RUN(visible_examples); RUN(dist_examples); RUN(hypot_table_ends); RUN(dist_cap); RUN(distkey_examples);
    RUN(distkey_narrow_d3); RUN(sort_examples); RUN(lod_table_hook);
    TEST_MAIN_END();
}
