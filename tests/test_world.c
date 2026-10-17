/* tests/test_world.c - the world module on a synthetic in-memory asset set. The byte buffers
 * are built here from the formats in tools/extract/README.md; no game data is used. */
#include "test.h"
#include "world_synth.h"
#include "../src/core/assets.h"

static int pens(const world_poly_t *p, int n, char *s) {
    for (int i = 0; i < n; i++) s[i] = (char)('0' + p[i].pen);
    s[n] = 0;
    return n;
}
static int polys_at(world_t *w, unsigned id, int level, int ex, int ey, int ez, unsigned scale, char *s) {
    world_view_t v;
    memset(&v, 0, sizeof v);
    v.eye[0] = ex; v.eye[1] = ey; v.eye[2] = ez; v.inst_scale = (uint8_t)scale;
    world_poly_t p[16];
    int n = world_model_polys(w, id, level, &v, p, 16);
    if (n < 0) { s[0] = 0; return n; }
    return pens(p, n, s);
}

/* ---- tests ------------------------------------------------------------------------------ */
TEST(loads_models_tables_palette_and_lod) {
    build_all();
    char err[160] = "";
    world_t *w = world_load(&LD, err, sizeof err);
    ASSERT_TRUE(w != NULL);
    ASSERT_EQ_INT(world_model_status(w, 1), WORLD_MODEL_OK);
    ASSERT_EQ_INT(world_model_status(w, 6), WORLD_MODEL_PARTIAL);
    ASSERT_EQ_INT(world_model_status(w, 16), WORLD_MODEL_EMPTY);
    ASSERT_EQ_INT(world_model_status(w, 77), WORLD_MODEL_ABSENT);
    ASSERT_EQ_INT(world_model_level_count(w, 5), 2);
    ASSERT_EQ_INT(world_model_vertex_count(w, 1, 0), 3);
    int32_t v[3];
    ASSERT_EQ_INT(world_model_vertex(w, 1, 0, 1, v), 0);
    ASSERT_EQ_INT(v[0], 100);
    ASSERT_EQ_INT(world_model_vertex(w, 1, 0, 3, v), -1);
    ASSERT_EQ_INT(world_palette(w)[7], 35);
    int16_t r[3];
    ASSERT_EQ_INT(world_camera_ring(w, 2, 3, r), 0);
    ASSERT_EQ_INT(r[0], 203); ASSERT_EQ_INT(r[1], -3); ASSERT_EQ_INT(r[2], 7);
    ASSERT_EQ_INT(world_camera_ring(w, 4, 0, r), -1);
    ASSERT_EQ_INT(fx_lod_shift(100), 6);    /* the table reached fx */
    int32_t pos[3];
    ASSERT_EQ_INT(world_position_entry(w, 33, pos), 0);
    ASSERT_EQ_INT(pos[0], 10 * 4194304 + 8192 * 256);
    ASSERT_EQ_INT(pos[1], 4 * 256);
    ASSERT_EQ_INT(world_position_entry(w, 5, pos), -1);
    int before = released;
    world_destroy(w);
    (void)before;
}

TEST(every_loader_buffer_is_released) {
    build_all();
    released = 0;
    world_t *w = world_load(&LD, NULL, 0);
    ASSERT_TRUE(w != NULL);
    /* index + 25 model files + terrain + positions + palette + lod + range + 4 rings = 35, plus the 15 files of the display-list tables (12 and the three of the near-ground pass) */
    ASSERT_EQ_INT(released, 51);
    world_destroy(w);
}

TEST(the_synthetic_sound_table_loads_with_absent_and_synthesised_objects) {
    sound_table_t st; char err[96] = "";
    build_all();
    ASSERT_EQ_INT(assets_load_sounds(&LD, &st, err, sizeof err), 0);
    int absent = 0, tone = 0, noise = 0, sample = 0;
    for (int i = 0; i < SOUND_OBJECTS; i++) {
        switch (st.obj[i].kind) {
        case SOUND_NONE: absent++; ASSERT_EQ_INT(st.obj[i].length, 0); break;
        case SOUND_KIND_TONE: tone++; break;
        case SOUND_KIND_NOISE: noise++; break;
        default: sample++; ASSERT_TRUE(st.obj[i].length > 0 && st.obj[i].file != SOUND_NONE); break;
        }
    }
    ASSERT_TRUE(absent > 0 && tone == 1 && noise == 2 && sample > 0);   /* the tone (object 4); the noise and the gun clone (6, 0xA) */
    for (int i = 0; i <= 0x24; i++) ASSERT_EQ_INT(st.obj[i].kind == SOUND_NONE, i == 7);   /* every object of the note's table but object 7 exists (sound_synth.h) */
}

TEST(bsp_two_node_tree_picks_child_by_viewer_side) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    char s[32];
    polys_at(w, 1, 0, 50, 0, 0, 0, s);  ASSERT_STR_EQ(s, "5");   /* eye.x >= plane: first child */
    polys_at(w, 1, 0, -50, 0, 0, 0, s); ASSERT_STR_EQ(s, "9");   /* eye.x < plane: second child */
    polys_at(w, 1, 0, 0, 0, 0, 0, s);   ASSERT_STR_EQ(s, "5");   /* equal: first child (>=) */
    world_destroy(w);
}

TEST(three_vertex_node_uses_the_cross_product_dot) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    char s[32];
    polys_at(w, 2, 0, 0, 0, 50, 0, s);  ASSERT_STR_EQ(s, "5");   /* negative: first child */
    polys_at(w, 2, 0, 0, 0, -50, 0, s); ASSERT_STR_EQ(s, "9");
    polys_at(w, 2, 0, 0, 0, 0, 0, s);   ASSERT_STR_EQ(s, "9");   /* exactly zero: second child (README) */
    world_destroy(w);
}

TEST(gating_hides_items_whose_gate_is_unsatisfied) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    char s[32];
    polys_at(w, 3, 0, 50, 0, 0, 0, s);  ASSERT_STR_EQ(s, "12");  /* near: A and the near-only item */
    polys_at(w, 3, 0, 100, 0, 0, 0, s); ASSERT_STR_EQ(s, "12");  /* "at most that far" */
    polys_at(w, 3, 0, 300, 0, 0, 0, s); ASSERT_STR_EQ(s, "13");  /* band 100 < d <= 500 */
    polys_at(w, 3, 0, 600, 0, 0, 0, s); ASSERT_STR_EQ(s, "1");   /* beyond the band: A only */
    polys_at(w, 3, 0, 50, 0, 0, 1, s);  ASSERT_STR_EQ(s, "12");  /* instance scale 1: G1 = 50, d <= 50 */
    polys_at(w, 3, 0, 60, 0, 0, 1, s);  ASSERT_STR_EQ(s, "13");  /* d > G1 = 50, G2 = 250 >= 60 */
    world_destroy(w);
}

TEST(one_sided_polygon_facing_back_colour_and_abandon) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    char s[32];
    polys_at(w, 4, 0, 0, 0, 50, 0, s);  ASSERT_STR_EQ(s, "78");  /* faces the viewer */
    polys_at(w, 4, 0, 0, 0, -50, 0, s); ASSERT_STR_EQ(s, "");    /* faces away: skipped, rest abandoned */
    polys_at(w, 7, 0, 0, 0, -50, 0, s); ASSERT_STR_EQ(s, "38");  /* back colour keeps it, rest runs */
    world_destroy(w);
}

TEST(stream_end_by_0xffff_and_the_type_b_abort_are_reported) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    world_view_t v;
    memset(&v, 0, sizeof v);
    v.eye[2] = 50;
    world_poly_t p[16];
    unsigned f = 99;
    ASSERT_EQ_INT(world_model_polys_ex(w, 4, 0, &v, p, 16, &f), 2); ASSERT_EQ_INT((int)f, 0);          /* an ordinary end: nothing reported */
    ASSERT_EQ_INT(world_model_polys_ex(w, 14, 0, &v, p, 16, &f), 2); ASSERT_EQ_INT((int)f, WORLD_WALK_END_FFFF);
    ASSERT_EQ_INT(world_model_polys_ex(w, 15, 0, &v, p, 16, &f), 2); ASSERT_EQ_INT((int)f, 0);         /* type B, facing: no abort */
    v.eye[2] = -50;                                                                                      /* the one-sided polygon faces away */
    ASSERT_EQ_INT(world_model_polys_ex(w, 4, 0, &v, p, 16, &f), 0); ASSERT_EQ_INT((int)f, 0);         /* the program is abandoned, the drawer goes on */
    ASSERT_EQ_INT(world_model_polys_ex(w, 15, 0, &v, p, 16, &f), 0); ASSERT_EQ_INT((int)f, WORLD_WALK_ABORTED);   /* type B: the whole drawer returns 0 */
    world_destroy(w);
}

TEST(first_vertex_follows_the_precision_exponent_of_the_unscaled_distance_word) {
    /* note 04a P2 and P3: model 13 has Tm 40, Tf 80, H 1, L 3 and its first vertex at the model origin, so the camera-space first vertex is the position word
     * alone: (object - camera) in position units shifted right by 8 minus the exponent (3 nearer than Tm, 2 between Tm and Tf, 0 farther than Tf), then the
     * camera matrix with its rows scaled by the zoom triple (168, 252, 128: x and depth are 0.656 and 0.5 of the world's). The object is 200 units to the side and
     * 192 ahead: near gives (6, 6) and camera (3, 3): inside the 1.5 * depth pyramid; between gives (3, 3) and (1, 1): inside; beyond Tf gives (0, 0): the depth
     * is not positive, so rule 8 rejects. */
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    game_entry_t e; game_camera_t cam; world_draw_info_t info;
    memset(&e, 0, sizeof e); memset(&cam, 0, sizeof cam);
    e.kind = GAME_ENTRY_TERRAIN; e.asset_id = 13; e.draw = 1; e.zoom = 0x80;
    e.pos.x = 200; e.pos.z = 192;
    static const struct { int dist, lod, ok; } c[] = { {10, 0, 1}, {40, 0, 1}, {41, 0, 1}, {80, 0, 1}, {81, 0, 0}, {90, 0, 0},
                                                      {5, 1, 1} /* Tm >> 1 = 20: near; the position is shifted by the LOD as well: (200>>1>>5, 192>>1>>5) = (3, 3) */ };
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        e.dist = (uint16_t)c[i].dist; e.lod = (uint8_t)c[i].lod;
        ASSERT_EQ_INT(world_draw_entry(w, &e, &cam, NULL, NULL, &info), 0);
        ASSERT_EQ_INT(info.fv_ok, c[i].ok);
    }
    world_destroy(w);
}

/* ---- the draw layer: handedness and the handler returns (note 03) ------------------------------------------------------------------------------ */
typedef struct { int n; world_out_t o[16]; } cap_t;
static void cap_sink(void *user, const world_out_t *o) { cap_t *c = (cap_t *)user; if (c->n < 16) c->o[c->n++] = *o; }
/* draws model `id` at the position offset (dx, dy, dz) from the camera at the origin, facing north (yaw 0) */
static int draw_at(world_t *w, unsigned id, int dx, int dy, int dz, cap_t *cap, world_draw_info_t *info) {
    game_entry_t e; game_camera_t cam;
    memset(&e, 0, sizeof e); memset(&cam, 0, sizeof cam);
    e.kind = GAME_ENTRY_TERRAIN; e.asset_id = (uint16_t)id; e.draw = 1; e.zoom = 0x80;
    e.pos.x = dx; e.pos.y = dy; e.pos.z = dz;
    cap->n = 0;
    return world_draw_entry(w, &e, &cam, cap_sink, cap, info);
}

TEST(a_point_east_of_a_camera_facing_north_lands_right_of_the_centre) {
    /* note 03, handedness: screen x increases with world -x for the camera's right; the original has no mirror */
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    cap_t cap; world_draw_info_t info;
    ASSERT_EQ_INT(draw_at(w, 20, -32000, 0, 160000, &cap, &info), 0);   /* east: world -x */
    ASSERT_EQ_INT(cap.n, 1);
    for (int i = 0; i < cap.o[0].n; i++) ASSERT_TRUE(cap.o[0].xy[2 * i] > 159);
    ASSERT_EQ_INT(draw_at(w, 20, 32000, 0, 160000, &cap, &info), 0);    /* west: world +x */
    ASSERT_EQ_INT(cap.n, 1);
    for (int i = 0; i < cap.o[0].n; i++) ASSERT_TRUE(cap.o[0].xy[2 * i] < 159);
    /* heading 90 degrees (clockwise) faces -x: an object there is straight ahead */
    game_entry_t e; game_camera_t cam;
    memset(&e, 0, sizeof e); memset(&cam, 0, sizeof cam);
    e.kind = GAME_ENTRY_TERRAIN; e.asset_id = 20; e.draw = 1; e.zoom = 0x80; e.pos.x = -160000; e.pos.z = 0;
    cam.yaw = 7200; cap.n = 0;
    world_draw_entry(w, &e, &cam, cap_sink, &cap, &info);
    ASSERT_EQ_INT(cap.n, 1);
    int minx = 999, maxx = -1;
    for (int i = 0; i < cap.o[0].n; i++) { int x = cap.o[0].xy[2 * i]; if (x < minx) minx = x; if (x > maxx) maxx = x; }
    ASSERT_TRUE(minx <= 159 && maxx >= 159 - 20);
    world_destroy(w);
}

TEST(a_rejected_point_abandons_its_program_and_the_hand_over_to_the_next_is_by_the_or) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    cap_t cap; world_draw_info_t info;
    ASSERT_EQ_INT(draw_at(w, 21, 0, 0, 160000, &cap, &info), 0);        /* both points inside */
    ASSERT_EQ_INT(info.polys, 2); ASSERT_EQ_INT(cap.n, 2);
    ASSERT_EQ_INT(cap.o[0].kind, WORLD_OUT_POINT);
    ASSERT_EQ_INT(draw_at(w, 21, 192000, 0, 160000, &cap, &info), 0);   /* the first point at x = 6000 > depth 5000: rejected, the second is not run */
    ASSERT_EQ_INT(info.polys, 0); ASSERT_EQ_INT(cap.n, 0);
    world_destroy(w);
}

TEST(the_filler_places_the_small_shape_sprites_at_the_right_column_of_the_box) {
    cap_t cap;
    const int16_t blk[6] = { 10, 20, 12, 22, 11, 22 };          /* h 2, w 2: the 2 by 2 block, rows miny + 1 and miny + 2, columns (maxx - 1, maxx) */
    cap.n = 0; world_test_filler(blk, 3, 179, 5, cap_sink, &cap);
    ASSERT_EQ_INT(cap.n, 1); ASSERT_EQ_INT(cap.o[0].kind, WORLD_OUT_POINT); ASSERT_EQ_INT(cap.o[0].sprite, 2);
    ASSERT_EQ_INT(cap.o[0].xy[0], 12); ASSERT_EQ_INT(cap.o[0].xy[1], 21);
    const int16_t one[6] = { 30, 40, 31, 41, 30, 41 };          /* h 1, w 1: a dot at minx and a second at maxx: the 2 by 1 sprite at x = maxx */
    cap.n = 0; world_test_filler(one, 3, 179, 5, cap_sink, &cap);
    ASSERT_EQ_INT(cap.n, 1); ASSERT_EQ_INT(cap.o[0].sprite, 1); ASSERT_EQ_INT(cap.o[0].xy[0], 31); ASSERT_EQ_INT(cap.o[0].xy[1], 41);
    const int16_t dot[6] = { 50, 60, 50, 61, 50, 60 };          /* w 0: a single dot at minx = maxx */
    cap.n = 0; world_test_filler(dot, 3, 179, 5, cap_sink, &cap);
    ASSERT_EQ_INT(cap.n, 1); ASSERT_EQ_INT(cap.o[0].sprite, 0); ASSERT_EQ_INT(cap.o[0].xy[0], 50);
    cap.n = 0; world_test_filler(blk, 3, 20, 5, cap_sink, &cap);   /* miny + 1 beyond the last row: nothing is drawn by the front (the first-row case is the raster's) */
    ASSERT_EQ_INT(cap.n, 0);
}

TEST(op_48_is_not_clipped_and_op_74_draws_only_above_240_ft) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    cap_t cap; world_draw_info_t info;
    ASSERT_EQ_INT(draw_at(w, 22, 0, 0, 160000, &cap, &info), 0);        /* op 2: the line leaves the pyramid, the clipped part counts */
    ASSERT_EQ_INT(info.polys, 1); ASSERT_EQ_INT(cap.o[0].kind, WORLD_OUT_LINE);
    ASSERT_EQ_INT(draw_at(w, 23, 0, 0, 160000, &cap, &info), 0);        /* op 48: one end outside the closed pyramid: nothing */
    ASSERT_EQ_INT(info.polys, 0); ASSERT_EQ_INT(cap.n, 0);
    world_lists(w)->scan.off_y = 0;
    ASSERT_EQ_INT(draw_at(w, 24, 0, 0, 160000, &cap, &info), 0);        /* op 74 below 240 ft: skipped */
    ASSERT_EQ_INT(info.polys, 0);
    world_lists(w)->scan.off_y = -0xD0;
    ASSERT_EQ_INT(draw_at(w, 24, 0, 0, 160000, &cap, &info), 0);
    ASSERT_EQ_INT(info.polys, 1);
    world_destroy(w);
}

TEST(op_26_abandons_in_the_common_drawer_and_counts_in_the_type_b_drawer) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    cap_t cap; world_draw_info_t info;
    ASSERT_EQ_INT(draw_at(w, 28, 0, 0, 160000, &cap, &info), 0);        /* kind 0: negative condition codes: nothing is added and op 24 does not run */
    ASSERT_EQ_INT(info.polys, 0);
    ASSERT_EQ_INT(draw_at(w, 26, 0, 8000, 160000, &cap, &info), 0);     /* type B: the word is tested, both ops count (seen from above: a polygon, not an edge-on line) */
    ASSERT_EQ_INT(info.polys, 2); ASSERT_EQ_INT(cap.n, 2);
    ASSERT_EQ_INT(cap.o[0].mask, 3); ASSERT_EQ_INT(cap.o[0].pattern, 3);   /* op 26's stipple */
    world_destroy(w);
}

TEST(the_second_vertex_section_makes_the_slots_after_the_plain_ones) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    cap_t a, b; world_draw_info_t info;
    ASSERT_EQ_INT(draw_at(w, 25, 0, 0, 160000, &a, &info), 0);
    ASSERT_EQ_INT(info.polys, 1);
    ASSERT_EQ_INT(draw_at(w, 27, 0, 0, 160000, &b, &info), 0);
    ASSERT_EQ_INT(info.polys, 1);
    ASSERT_EQ_INT(a.n, 1); ASSERT_EQ_INT(b.n, 1);
    for (int i = 0; i < 4; i++) ASSERT_EQ_INT(a.o[0].xy[i], b.o[0].xy[i]);
    world_destroy(w);
}

/* ---- the clipped line's word negation and zero difference, the op 6 ruling, the pen timing and the gear skips (note 03, e79e531) -------------------------------------------- */
static int g_nout;
static void count_sink(void *user, const world_out_t *o) { (void)user; (void)o; g_nout++; }
static int hcall(int op, const int32_t *o, int nops, unsigned pen, const fx_vec_t *v, int nv, int off_y, cap_t *cap, int *code) {
    cap->n = 0;
    return world_test_handler(op, o, nops, pen, v, nv, off_y, 1, cap_sink, cap, code);
}

TEST(the_clipped_line_negates_words_and_rejects_and_counts_a_zero_margin_difference) {
    cap_t cap; int code; memset(&cap, 0, sizeof cap);
    const int32_t o2[2] = { 0, 1 };
    /* -(-32768) is -32768: the first end is not beyond the minus plane, and the hit on it keeps the word (the rig case of tests/rig/test_draw_diff.c, seed 2) */
    fx_vec_t a[2] = { { -32768, 96, 175 }, { -7754, 2049, 8253 } };
    ASSERT_EQ_INT(hcall(2, o2, 2, 6, a, 2, 0, &cap, &code), 1);
    ASSERT_EQ_INT(cap.n, 1); ASSERT_EQ_INT(cap.o[0].xy[0], 319); ASSERT_EQ_INT(cap.o[0].xy[1], 40);
    ASSERT_EQ_INT(world_test_den_zero(), 0);
    /* E beyond XP with the margin z - x wrapping to 32,536 and F with the same margin: den = 0, the original never returns; rejected, nothing drawn, counted */
    fx_vec_t z[2] = { { 30000, 0, -3000 }, { 0, 0, 32536 } };
    ASSERT_EQ_INT(hcall(2, o2, 2, 6, z, 2, 0, &cap, &code), 0);
    ASSERT_EQ_INT(cap.n, 0); ASSERT_EQ_INT(world_test_den_zero(), 1);
    /* op 6: N below 2 draws nothing and is counted; N = 3 draws two rungs */
    fx_vec_t r[4] = { { 0, 0, 100 }, { 30, 0, 100 }, { 0, 20, 100 }, { 30, 20, 100 } };
    for (int n = -2; n < 2; n++) {
        const int32_t o6[5] = { n, 0, 1, 2, 3 };
        ASSERT_EQ_INT(hcall(6, o6, 5, 4, r, 4, 0, &cap, &code), 0);
        ASSERT_EQ_INT(cap.n, 0); ASSERT_EQ_INT(world_test_op6_small(), 1);
    }
    { const int32_t o6[5] = { 3, 0, 1, 2, 3 }; ASSERT_EQ_INT(hcall(6, o6, 5, 4, r, 4, 0, &cap, &code), 1); ASSERT_EQ_INT(cap.n, 2); ASSERT_EQ_INT(world_test_op6_small(), 0); }
    /* the counts are the original's words: op 19 with N = 300 (above the old cap of 256) draws 2 x 300 strokes, and op 11 draws the 255 segments its byte allows */
    { fx_vec_t h[4] = { { 0, 0, 100 }, { 10, 0, 100 }, { 10, 0, 100 }, { 0, 0, 100 } }; const int32_t o19[2] = { 0, 300 };
      g_nout = 0; world_test_handler(19, o19, 2, 3, h, 4, 0, 1, count_sink, NULL, &code); ASSERT_EQ_INT(g_nout, 600);
      fx_vec_t sg[600]; for (int i = 0; i < 600; i++) { sg[i].x = i & 7; sg[i].y = 1; sg[i].z = 100 + (i & 3); }
      const int32_t o11[2] = { 255, 0 };
      g_nout = 0; world_test_handler(11, o11, 2, 3, sg, 600, 0, 1, count_sink, NULL, &code); ASSERT_EQ_INT(g_nout, 255); }
}

TEST(op_71_leaves_the_pen_word_of_its_last_disc_even_when_that_disc_fails_its_projection) {
    /* Note 07 11.3 item 2: every disc of the list is handed to the disc routine (a rejected one does not end the list) and the pen word after the list is the last disc's pen. */
    cap_t cap; int code; memset(&cap, 0, sizeof cap); unsigned m, t, pen;
    int pt;
    fx_vec_t v[4];
    for (int i = 0; i < 4; i++) { v[i].x = 0; v[i].y = 0; v[i].z = 200; }
    v[1].z = -200;                         /* behind the camera: the projection fails */
    const int32_t two[7] = { 2, 0, 9, 0x0100, 6, 7, 0x0100 | 0x8000 };           /* count, then (vertex byte offset, pen, size) per disc: disc 0 drawn, disc 1 rejected */
    ASSERT_EQ_INT(hcall(71, two, 7, 5, v, 4, 0, &cap, &code), 1);               /* the OR of the results: the first disc drew */
    world_test_fill_state(&m, &pt, &t, &pen);
    ASSERT_EQ_INT((int)pen, 7);                                                  /* the pen of the rejected last disc, not the 9 of the drawn one */
    ASSERT_TRUE(cap.n >= 1);
    const int32_t one[4] = { 1, 0, 11, 0x0100 };
    hcall(71, one, 4, 5, v, 4, 0, &cap, &code); world_test_fill_state(&m, &pt, &t, &pen); ASSERT_EQ_INT((int)pen, 11);   /* a drawn last disc */
    const int32_t rejected_first[7] = { 2, 6, 3, 0x0100, 0, 4, 0x0100 };         /* the first disc rejected, the second drawn: the list went on past it */
    ASSERT_EQ_INT(hcall(71, rejected_first, 7, 5, v, 4, 0, &cap, &code), 1);
    world_test_fill_state(&m, &pt, &t, &pen); ASSERT_EQ_INT((int)pen, 4);
}

TEST(each_handler_writes_the_pen_at_the_moment_of_the_table_in_note_03) {
    cap_t cap; int code; memset(&cap, 0, sizeof cap); unsigned m, t, pen; int pt;
    fx_vec_t v[8];
    for (int i = 0; i < 8; i++) { v[i].x = 10 * i; v[i].y = 5 * i; v[i].z = 200; }
    const int32_t o2[2] = { 0, 1 }, o1[1] = { 0 };
    /* op 74: written after the height gate */
    hcall(74, o2, 2, 11, v, 8, 0, &cap, &code); world_test_fill_state(&m, &pt, &t, &pen); ASSERT_EQ_INT((int)pen, 5);
    hcall(74, o2, 2, 11, v, 8, -0xD0, &cap, &code); world_test_fill_state(&m, &pt, &t, &pen); ASSERT_EQ_INT((int)pen, 11);
    /* ops 9 and 10: before their height gate, a closed gate included */
    hcall(9, o1, 1, 12, v, 8, -0x100, &cap, &code); world_test_fill_state(&m, &pt, &t, &pen); ASSERT_EQ_INT((int)pen, 12); ASSERT_EQ_INT(cap.n, 0);
    hcall(10, o1, 1, 13, v, 8, -0x100, &cap, &code); world_test_fill_state(&m, &pt, &t, &pen); ASSERT_EQ_INT((int)pen, 13);
    /* op 23: never */
    hcall(23, o2, 2, 9, v, 8, 0, &cap, &code); world_test_fill_state(&m, &pt, &t, &pen); ASSERT_EQ_INT((int)pen, 5);
    /* op 26: not on the early-out over N + 1 slots (nothing written: the pen and the fill words stay), 7 afterwards */
    fx_vec_t b[8]; for (int i = 0; i < 8; i++) { b[i].x = i; b[i].y = i; b[i].z = -100; }
    { const int32_t o26[2] = { 3, 0 };
      ASSERT_EQ_INT(hcall(26, o26, 2, 7, b, 8, 0, &cap, &code), 0); ASSERT_EQ_INT(code, 0);
      world_test_fill_state(&m, &pt, &t, &pen); ASSERT_EQ_INT((int)pen, 5); ASSERT_EQ_INT((int)m, 0xF); ASSERT_EQ_INT(pt, -1);
      b[3].z = 100;   /* the slot after the three listed ones is not behind: no early-out */
      hcall(26, o26, 2, 7, b, 8, 0, &cap, &code); world_test_fill_state(&m, &pt, &t, &pen); ASSERT_EQ_INT((int)pen, 7); ASSERT_EQ_INT(code, 1); ASSERT_EQ_INT((int)m, 0xF); ASSERT_EQ_INT(pt, -1); }
    /* ops 27 and 30: the fill words, then the pen, then the early-out over A, B, C and A - B + C (a word sum) */
    { const int32_t o27[1] = { 0 };
      ASSERT_EQ_INT(hcall(27, o27, 1, 10, b, 8, 0, &cap, &code), 0);
      world_test_fill_state(&m, &pt, &t, &pen); ASSERT_EQ_INT((int)pen, 10); ASSERT_EQ_INT((int)m, 2); ASSERT_EQ_INT(pt, 0); ASSERT_EQ_INT((int)t, 2);
      hcall(30, o27, 1, 3, b, 8, 0, &cap, &code); world_test_fill_state(&m, &pt, &t, &pen); ASSERT_EQ_INT((int)pen, 3); ASSERT_EQ_INT((int)m, 8); ASSERT_EQ_INT(pt, 8); ASSERT_EQ_INT((int)t, 0);
      /* A, B, C behind with A - B + C not behind (z = -100 + 300 - ... : B = -300, A = C = -100 gives +100): drawn, not an early-out */
      fx_vec_t q[8]; for (int i = 0; i < 8; i++) { q[i].x = 20 * i; q[i].y = 10 * i; q[i].z = -100; }
      q[1].z = -300;
      hcall(27, o27, 1, 10, q, 8, 0, &cap, &code); ASSERT_EQ_INT(cap.n, 1); }
}

TEST(op_52_and_53_select_the_gear_step_by_the_progress_in_the_record_byte) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    game_entry_t e; game_camera_t cam; world_draw_info_t info; cap_t cap;
    memset(&e, 0, sizeof e); memset(&cam, 0, sizeof cam);
    e.kind = GAME_ENTRY_ENTITY; e.id = 0; e.asset_id = 30; e.draw = 1; e.zoom = 0x80; e.pos.z = 160000;
    int xend[8];
    for (int n = 0; n < 8; n++) {
        world_lists(w)->ent[0].b7c = (uint8_t)(0x80 | (n << 4) | 5);   /* bit 7 (selected) and the low bits do not matter: (byte & 0x7F) >> 4 */
        cap.n = 0;
        ASSERT_EQ_INT(world_draw_entry(w, &e, &cam, cap_sink, &cap, &info), 0);
        if (n >= 6) { ASSERT_EQ_INT(cap.n, 1); continue; }   /* gear up: the op 52 item, the op 53 items and the gear lines are not executed; the rest of the body (its one line) is */
        ASSERT_EQ_INT(cap.n, 2);                             /* the gear line, then the body's line */
        xend[n] = cap.o[0].xy[0] - cap.o[0].xy[2];
    }
    /* step n synthesises slot 30 = S1 + S[B] - S0 with the x of S[B] = 400 (n + 1): the line from slot 30 to the origin vertex grows with n (the first end lies further to the left, camera +x maps to the left);
     * the last step never wins for the smaller n; n = 6 and 7 run no gear item at all (no fabricated slot is read) */
    for (int n = 1; n < 6; n++) ASSERT_TRUE(xend[n] < xend[n - 1]);
    ASSERT_TRUE(xend[0] < 0);
    world_destroy(w);
}

TEST(level_selection_by_threshold_and_instance_scale) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    world_view_t v;
    memset(&v, 0, sizeof v);
    v.eye[0] = 50;
    ASSERT_EQ_INT(world_pick_level(w, 5, &v), 0);
    v.eye[0] = 500;
    ASSERT_EQ_INT(world_pick_level(w, 5, &v), 1);      /* last-resort level */
    v.eye[0] = 50; v.inst_scale = 2;                    /* threshold 100 >> 2 = 25 */
    ASSERT_EQ_INT(world_pick_level(w, 5, &v), 1);
    v.eye[0] = 500; v.inst_scale = 0;
    ASSERT_EQ_INT(world_pick_level(w, 9, &v), -1);     /* beyond the only level: nothing drawn */
    ASSERT_EQ_INT(world_pick_level(w, 77, &v), -1);
    char s[8];
    polys_at(w, 5, 0, 50, 0, 0, 0, s); ASSERT_STR_EQ(s, "5");
    polys_at(w, 5, 1, 50, 0, 0, 0, s); ASSERT_STR_EQ(s, "6");
    ASSERT_EQ_INT(polys_at(w, 5, 2, 0, 0, 0, 0, s), -1);
    world_destroy(w);
}

TEST(make_view_applies_the_model_unit_scale) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    world_view_t v;
    int32_t d[3] = { 3200, -3200, 64 };
    world_make_view(w, 1, d, 0, &v);       /* L = 3: 2^(8-3) = 32 position units per model unit */
    ASSERT_EQ_INT(v.eye[0], 100); ASSERT_EQ_INT(v.eye[1], -100); ASSERT_EQ_INT(v.eye[2], 2);
    world_make_view(w, 1, d, 1, &v);       /* instance scale 1 doubles the unit */
    ASSERT_EQ_INT(v.eye[0], 50);
    world_destroy(w);
}

TEST(precision_bands_shrink_the_level_distance_beyond_tm_and_tf) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    world_view_t v;
    /* model 13: level 0 below distance 100, Tm 40, Tf 80, H 1, L 3; eye = delta >> 5 */
    int32_t e30[3] = { 30 * 32, 0, 0 }, e60[3] = { 60 * 32, 0, 0 }, e150[3] = { 150 * 32, 0, 0 }, e900[3] = { 900 * 32, 0, 0 };
    ASSERT_EQ_INT(world_make_view(w, 13, e30, 0, &v), 0); ASSERT_EQ_INT(v.band, 0);   /* nearer than Tm */
    ASSERT_EQ_INT(world_make_view(w, 13, e60, 0, &v), 0); ASSERT_EQ_INT(v.band, 1);   /* between Tm and Tf: H */
    ASSERT_EQ_INT(world_make_view(w, 13, e150, 0, &v), 0);                             /* beyond Tf: 150 >> 3 = 18 */
    ASSERT_EQ_INT(v.band, 3); ASSERT_EQ_INT(v.eye[0], 150);                            /* the geometric eye is unchanged */
    ASSERT_EQ_INT(world_make_view(w, 13, e900, 0, &v), 1);                             /* 900 >> 3 = 112 >= 100 */
    ASSERT_EQ_INT(world_make_view(w, 5, e150, 0, &v), 1);                              /* model 5 has no bands */
    world_destroy(w);
}

TEST(partial_model_loads_with_blocked_items_skipped) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    ASSERT_EQ_INT(world_model_blocked(w, 6), 1);
    char s[8];
    polys_at(w, 6, 0, 0, 0, 0, 0, s); ASSERT_STR_EQ(s, "13");
    world_destroy(w);
}

TEST(unresolved_or_undefined_slots_make_an_item_undrawable) {
    build_all();
    /* model 6 (partial): vertex states are the 4th long of each 16-byte vertex record; levels = 54 bytes (IMDL v5), header 60 */
    buf_t *b = file_buf("models/model_006.bin");
    size_t v2 = 60 + 54 + 16 * 2 + 12;   /* state of slot 2 */
    b->d[v2] = 4;                         /* unresolved */
    world_t *w = world_load(&LD, NULL, 0);
    char s[8];
    polys_at(w, 6, 0, 0, 0, 0, 0, s); ASSERT_STR_EQ(s, "");   /* every polygon uses slot 2 */
    world_destroy(w);
    build_all();
    b = file_buf("models/model_006.bin");
    b->d[v2] = 0;                         /* undefined */
    w = world_load(&LD, NULL, 0);
    polys_at(w, 6, 0, 0, 0, 0, 0, s); ASSERT_STR_EQ(s, "");
    world_destroy(w);
}

TEST(selector_skip_list_and_speculative_items_and_blocked_nodes) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    char s[8];
    polys_at(w, 10, 0, 0, 0, 0, 0, s); ASSERT_STR_EQ(s, "13");   /* item1 skipped, item3 speculative */
    polys_at(w, 11, 0, 50, 0, 0, 0, s); ASSERT_STR_EQ(s, "");     /* blocked node: not drawable */
    world_destroy(w);
}

TEST(version_1_files_are_rejected_with_a_version_message) {
    build_all();
    file_buf("models/model_001.bin")->d[4] = 1;
    char err[160] = "";
    ASSERT_TRUE(world_load(&LD, err, sizeof err) == NULL);
    ASSERT_TRUE(strstr(err, "model_001.bin") != NULL);
    ASSERT_TRUE(strstr(err, "version") != NULL);
}

TEST(op67_face_visibility_selector_skips_by_side) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    char s[8];
    polys_at(w, 12, 0, 0, 0, 50, 0, s);  ASSERT_STR_EQ(s, "45");   /* faces the viewer: entry 0 */
    polys_at(w, 12, 0, 0, 0, -50, 0, s); ASSERT_STR_EQ(s, "5");    /* faces away: entry 1 skips the next item */
    world_destroy(w);
}

TEST(unit_shift_and_unit_tables_are_checked) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    ASSERT_EQ_INT(world_model_unit_shift(w, 1, 0), 3);
    ASSERT_EQ_INT(world_model_unit_shift_h(w, 1, 0), 0);
    ASSERT_EQ_INT(world_model_unit_shift(w, 1, 1), -1);
    ASSERT_EQ_INT(world_model_unit_shift(w, 77, 0), -1);
    ASSERT_TRUE(game_world(NULL) == NULL);
    world_destroy(w);
    char err[160];
    build_all(); file_buf("tables/range.bin")->n = 63; err[0] = 0;
    ASSERT_TRUE(world_load(&LD, err, sizeof err) == NULL); ASSERT_TRUE(strstr(err, "range.bin") != NULL);
    build_all(); drop_file("tables/range.bin"); err[0] = 0;
    ASSERT_TRUE(world_load(&LD, err, sizeof err) == NULL); ASSERT_TRUE(strstr(err, "range.bin") != NULL);
    build_all(); file_buf("tables/lod.bin")->n = 239; err[0] = 0;
    ASSERT_TRUE(world_load(&LD, err, sizeof err) == NULL); ASSERT_TRUE(strstr(err, "lod.bin") != NULL);
    build_all(); put_text("terrain/positions.txt", "33 200 10 8192 8192 4\n"); err[0] = 0;
    ASSERT_TRUE(world_load(&LD, err, sizeof err) == NULL); ASSERT_TRUE(strstr(err, "positions.txt") != NULL);
}

/* Each validation of model_parse and load_terrain: corrupt one field, expect a failure naming the
 * file, and that every buffer the loader handed out was released. Offsets for model 1 (3 vertices,
 * 3 elements, 2 list entries, 2 programs, 2 items, one level): level @48, vertices @88,
 * elements @136, proglist @232, programs @240, items @264. */
static void corrupt_expect(const char *file, size_t off, uint8_t val, const char *name) {
    char err[200] = "";
    build_all();
    file_buf(file)->d[off] = val;
    reads = 0; released = 0;
    world_t *w = world_load(&LD, err, sizeof err);
    if (w) { world_destroy(w); printf("  accepted corrupt %s\n", name); test_failures++; return; }
    if (!strstr(err, name)) { printf("  error \"%s\" lacks %s\n", err, name); test_failures++; }
    if (released != reads) { printf("  %d reads but %d releases\n", reads, released); test_failures++; }
}
TEST(corrupt_files_are_rejected_and_buffers_released) {
    const char *m = "models/model_001.bin";
    corrupt_expect(m, 60 + 23, 0x7F, "model_001.bin");        /* level root (high byte) out of range */
    corrupt_expect(m, 60 + 24 + 3, 0x70, "model_001.bin");    /* level vertex slice */
    corrupt_expect(m, 162 + 4 + 3, 0x70, "model_001.bin");    /* element child */
    corrupt_expect(m, 162 + 32 + 24, 99, "model_001.bin");    /* draw element program list slice */
    corrupt_expect(m, 258, 99, "model_001.bin");              /* program list entry */
    corrupt_expect(m, 266, 99, "model_001.bin");              /* program item range */
    corrupt_expect(m, 290 + 24, 99, "model_001.bin");         /* item operands */
    corrupt_expect(m, 290 + 36, 5, "model_001.bin");          /* item skip slice (no skips in the file) */
    corrupt_expect(m, 6, 9, "model_001.bin");                 /* id does not match the index */
    const char *t = "terrain/terrain.bin";
    corrupt_expect(t, 86, 20, "terrain.bin");                 /* cell 0 z = 20: cells not in z then x order */
    corrupt_expect(t, 88, 99, "terrain.bin");                 /* cell list index */
    corrupt_expect(t, 16 + 2, 33, "terrain.bin");             /* grid not a power of two covering 2^29 */
}

TEST(missing_model_file_is_an_error_naming_the_file) {
    build_all();
    drop_file("models/model_003.bin");
    char err[160] = "";
    world_t *w = world_load(&LD, err, sizeof err);
    ASSERT_TRUE(w == NULL);
    ASSERT_TRUE(strstr(err, "model_003.bin") != NULL);
    build_all();
    file_buf("models/model_002.bin")->n -= 4;   /* truncated: size no longer matches the counts */
    err[0] = 0;
    w = world_load(&LD, err, sizeof err);
    ASSERT_TRUE(w == NULL);
    ASSERT_TRUE(strstr(err, "model_002.bin") != NULL);
}

TEST(missing_terrain_or_table_files_are_errors) {
    char err[160];
    build_all(); drop_file("terrain/terrain.bin"); err[0] = 0;
    ASSERT_TRUE(world_load(&LD, err, sizeof err) == NULL);
    ASSERT_TRUE(strstr(err, "terrain.bin") != NULL);
    build_all(); drop_file("tables/camera_ring_14.bin"); err[0] = 0;
    ASSERT_TRUE(world_load(&LD, err, sizeof err) == NULL);
    ASSERT_TRUE(strstr(err, "camera_ring_14.bin") != NULL);
    build_all(); file_buf("terrain/terrain.bin")->d[5] = 9; err[0] = 0;   /* bad version */
    ASSERT_TRUE(world_load(&LD, err, sizeof err) == NULL);
}

TEST(cell_lookup_is_bounds_checked) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    const world_item_t *it; unsigned n, ci;
    ASSERT_EQ_INT(world_cell_items(w, 2, 10, 10, &it, &n, &ci), 1);
    ASSERT_EQ_INT(n, 2); ASSERT_EQ_INT(ci, 1); ASSERT_EQ_INT(it[0].dx, 2048);
    ASSERT_EQ_INT(world_cell_items(w, 2, 128, 10, &it, &n, &ci), 0);     /* beyond the grid */
    ASSERT_EQ_INT(world_cell_items(w, 2, 10, 65535, &it, &n, &ci), 0);
    ASSERT_EQ_INT(world_cell_items(w, 0, 32, 0, &it, &n, &ci), 0);       /* A and B grids are 32 wide */
    ASSERT_EQ_INT(world_cell_items(w, 3, 0, 0, &it, &n, &ci), 0);        /* no such table */
    ASSERT_EQ_INT(world_cell_items(w, 2, 9, 10, &it, &n, &ci), 0);       /* empty cell */
    world_destroy(w);
}

/* The camera at the centre of cell (10, 10) of the synthetic terrain, the carrier (entity 14) in the same cell, one builder frame. The synthetic
 * high-altitude pattern lists the tile (1, 0) and then the tile (0, 0), each with all 16 sub-cells in order (tests/world_synth.h). */
static void lists_frame_at(world_t *w, int32_t x, int32_t z, int32_t y, int carrier_cell_x, int carrier_cell_z) {
    world_camera_t c; memset(&c, 0, sizeof c);
    c.pos[0] = x; c.pos[1] = y; c.pos[2] = z;
    world_set_camera(w, &c);
    lists_frame_t in; memset(&in, 0, sizeof in);
    lists_entity_t *e = &in.ent[14];
    e->active = 1; e->cell_x = (uint8_t)carrier_cell_x; e->cell_z = (uint8_t)carrier_cell_z;
    e->fine_x = 8192; e->fine_z = 8192; e->sub = 5; e->model = WORLD_CARRIER_ID;
    e->pos[0] = (carrier_cell_x << 22) + (8192 << 8); e->pos[2] = (carrier_cell_z << 22) + (8192 << 8);
    world_lists_frame(w, &in);
}

TEST(builder_emits_tiles_and_sub_cells_in_pattern_order_with_the_note_formulas) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    lists_frame_at(w, 10 * 4194304 + 2097152, 10 * 4194304 + 2097152, 0, 10, 10);
    const lists_t *l = world_lists(w);
    ASSERT_EQ_INT(l->kept[0], 0); ASSERT_EQ_INT(l->kept[1], 0);     /* tables A and B are empty */
    ASSERT_EQ_INT(l->kept[2], 4);
    const lists_entry_t *q = l->q[2];
    /* tile (1, 0) = cell (11, 10): its item (id 1, layer 0, offsets 0, 0): X = 16384 + 4096 * 3, Z = 4096 * 3; the key has 16 | slot 0 and the tile label (11, 10) */
    ASSERT_EQ_INT(q[0].id, 1); ASSERT_EQ_INT(q[0].flags, 0); ASSERT_EQ_INT(q[0].x, 16384 + 12288); ASSERT_EQ_INT(q[0].z, 12288);
    ASSERT_TRUE(q[0].key == 0x00100B0Au);
    /* tile (0, 0) = cell (10, 10): the layer-0 item at (2048, 2048), then the layer-1 item (id 20) at (100, 100) of the sub-cell 1 */
    ASSERT_EQ_INT(q[1].id, 1); ASSERT_EQ_INT(q[1].x, 12288 + 2048); ASSERT_EQ_INT(q[1].z, 12288 + 2048); ASSERT_TRUE(q[1].key == 0x00100A0Au);
    ASSERT_EQ_INT(q[2].id, 20); ASSERT_EQ_INT(q[2].x, 8192 + 100); ASSERT_EQ_INT(q[2].z, 12288 + 100); ASSERT_TRUE(q[2].key == 0x00110A0Au);
    /* the carrier (entity 14, sub-cell 5 of its cell) is inserted into its slot: flags 0x10, tile offset plus sub-cell corner only */
    ASSERT_EQ_INT(q[3].id, 14); ASSERT_EQ_INT(q[3].flags, 0x10); ASSERT_EQ_INT(q[3].x, 8192); ASSERT_EQ_INT(q[3].z, 8192); ASSERT_TRUE(q[3].key == 0x8E000000u);
    ASSERT_EQ_INT(l->ndirect, 0);   /* emitted, so not in the direct list */
    game_scene_t s; memset(&s, 0, sizeof s);
    world_fill_scene(w, &s);
    ASSERT_EQ_INT(s.entry_count, 4);   /* queue C (the synthetic point tables hold no speck: stage [90] adds nothing) */
    ASSERT_EQ_INT(s.entries[0].kind, GAME_ENTRY_TERRAIN); ASSERT_EQ_INT(s.entries[0].asset_id, 17);   /* item id 1 = object id 17 */
    ASSERT_TRUE(s.entries[0].pos.x == 10 * 4194304 + (28672 << 8));   /* the viewer's cell corner plus the word */
    ASSERT_EQ_INT(s.entries[3].kind, GAME_ENTRY_ENTITY); ASSERT_EQ_INT(s.entries[3].id, 14); ASSERT_EQ_INT(s.entries[3].asset_id, WORLD_CARRIER_ID);
    world_destroy(w);
}

TEST(a_carrier_outside_the_scanned_tiles_goes_to_the_direct_list_and_the_scene_draws_it_first) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    lists_frame_at(w, 10 * 4194304 + 2097152, 10 * 4194304 + 2097152, 0, 70, 65);   /* the carrier far away */
    const lists_t *l = world_lists(w);
    ASSERT_EQ_INT(l->kept[2], 3);
    ASSERT_EQ_INT(l->ndirect, 1); ASSERT_EQ_INT(l->direct[0], 14);
    game_scene_t s; memset(&s, 0, sizeof s);
    world_fill_scene(w, &s);
    ASSERT_EQ_INT(s.entry_count, 4);     /* direct carrier, three terrain entries */
    ASSERT_EQ_INT(s.entries[0].kind, GAME_ENTRY_ENTITY); ASSERT_EQ_INT(s.entries[0].tier, GAME_TIER_DIRECT);
    ASSERT_EQ_INT(s.entries[1].kind, GAME_ENTRY_TERRAIN);
    world_destroy(w);
}

/* ---- the near-ground pass in the scene, the fill block it leaves and the polygon buffer residue (stage [90] and op 9, docs/re/04-scene-composition.md and 03-3d-pipeline.md) ---------------------------- */
TEST(the_scene_carries_the_specks_of_the_near_ground_pass_between_queue_b_and_the_entities) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    lists_tables_t *t = (lists_tables_t *)world_lists_tables(w);
    t->ng[0].pt[0][0] = 0; t->ng[0].pt[0][1] = 1000; t->ng[0].pt[0][2] = 1;    /* a 1 by 1 sprite straight ahead: X = 0, Z = 1000 << 3 */
    t->ng[0].pt[1][0] = 0; t->ng[0].pt[1][1] = 1000; t->ng[0].pt[1][2] = 2;    /* and the 2 by 1 sprite at the same point */
    lists_frame_at(w, 10 * 4194304 + 2097152, 10 * 4194304 + 2097152, 0, 70, 65);   /* height 0 (table A), the carrier far away: the direct list */
    game_scene_t s; memset(&s, 0, sizeof s);
    world_fill_scene(w, &s);
    ASSERT_EQ_INT(s.entry_count, 6);                       /* two specks, the direct carrier, three terrain entries of queue C */
    ASSERT_EQ_INT(s.entries[0].kind, GAME_ENTRY_SPECK); ASSERT_EQ_INT(s.entries[0].variant, 0);
    ASSERT_EQ_INT(s.entries[1].kind, GAME_ENTRY_SPECK); ASSERT_EQ_INT(s.entries[1].variant, 1);
    ASSERT_EQ_INT(s.entries[0].sv[0], 159); ASSERT_EQ_INT(s.entries[0].sv[1], 89);   /* y' = 0 (height 0): sy = 90, the row 179 - 90 */
    ASSERT_EQ_INT(s.entries[2].kind, GAME_ENTRY_ENTITY); ASSERT_EQ_INT(s.entries[2].tier, GAME_TIER_DIRECT);
    world_set_window_last_row(w, 88);                      /* a sprite whose row is below the last 3D row is dropped */
    lists_frame_at(w, 10 * 4194304 + 2097152, 10 * 4194304 + 2097152, 0, 70, 65);
    memset(&s, 0, sizeof s); world_fill_scene(w, &s);
    for (unsigned i = 0; i < s.entry_count; i++) ASSERT_TRUE(s.entries[i].kind != GAME_ENTRY_SPECK);
    world_destroy(w);
}

TEST(the_first_object_after_the_pass_finds_the_fill_block_and_a_type_b_polygon_takes_the_alternate_blit) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    cap_t cap; int code; memset(&cap, 0, sizeof cap);
    /* the walk: the pass stores (4, 0, 0, 0xFFFF) and the colour 3 before the first object after it, which therefore finds them at its entry (entry fields fill_toggle, fill_alt); the plain drawer's entry
     * clears them for the objects after that one */
    lists_frame_at(w, 10 * 4194304 + 2097152, 10 * 4194304 + 2097152, 100 * 256, 70, 65);   /* 100 u: inside the limit */
    game_scene_t s; memset(&s, 0, sizeof s);
    world_fill_scene(w, &s);
    ASSERT_TRUE(s.entry_count >= 2);
    ASSERT_EQ_INT(s.entries[0].fill_alt, 0xFFFF); ASSERT_EQ_INT(s.entries[0].fill_toggle, 0);
    ASSERT_EQ_INT(s.entries[1].fill_alt, 0);
    lists_frame_at(w, 10 * 4194304 + 2097152, 10 * 4194304 + 2097152, 3000 * 256, 70, 65);   /* above the limit: the pass is not drawn but its stores are made */
    memset(&s, 0, sizeof s); world_fill_scene(w, &s);
    ASSERT_TRUE(s.entry_count >= 1);
    ASSERT_EQ_INT(s.entries[0].fill_alt, 0xFFFF);
    /* a type B object's first polygon (op 26: mask 3, pattern 3) drawn while the word is set: the alternate blit; after the plain drawer's entry or a block write it is not */
    fx_vec_t v[14]; for (int i = 0; i < 14; i++) { v[i].x = 20 * i; v[i].y = 10 * i; v[i].z = 400; }
    const int32_t o19[2] = { 0, 2 }, o26[2] = { 4, 0 };
    world_test_handler(19, o19, 2, 9, v, 14, 0, 1, cap_sink, &cap, &code);
    ASSERT_EQ_INT((int)world_test_fill_alt(), 0);
    world_test_set_fill_alt(0xFFFF);                                                         /* what the pass leaves, with no plain drawer's entry since */
    cap.n = 0; world_test_handler(26, o26, 2, 7, v, 14, 0, 0, cap_sink, &cap, &code);
    ASSERT_EQ_INT(cap.n, 1); ASSERT_EQ_INT(cap.o[0].alt, 1); ASSERT_EQ_INT(cap.o[0].mask, 3); ASSERT_EQ_INT(cap.o[0].pattern, 3);
    ASSERT_EQ_INT((int)world_test_fill_alt(), 0xFFFF);                                      /* op 26 restores mask and pattern only: the word stays */
    cap.n = 0; world_test_handler(26, o26, 2, 7, v, 14, 0, 0, cap_sink, &cap, &code);
    ASSERT_EQ_INT(cap.o[0].alt, 1);
    for (int k = 0; k < 4; k++) {   /* the block writers (17 to 23, 27 and 30) store V:4A7A4 = 0; the stale word is op 26's alone */
        static const int wr[4] = { 30, 27, 20, 17 };
        const int32_t ow[2] = { 0, 1 }; int32_t o17[4] = { 0, 1, 2, 3 };
        world_test_set_fill_alt(0xFFFF);
        world_test_handler(wr[k], wr[k] == 17 ? o17 : ow, wr[k] == 17 ? 4 : (wr[k] == 20 ? 2 : 1), 3, v, 14, 0, 0, cap_sink, &cap, &code);
        ASSERT_EQ_INT((int)world_test_fill_alt(), 0);
    }
    world_test_set_fill_alt(0xFFFF);
    world_test_handler(19, o19, 2, 9, v, 14, 0, 0, cap_sink, &cap, &code);                  /* op 19 writes the whole block */
    ASSERT_EQ_INT((int)world_test_fill_alt(), 0);
    world_test_set_fill_alt(0xFFFF);
    cap.n = 0; world_test_handler(26, o26, 2, 7, v, 14, 0, 1, cap_sink, &cap, &code);       /* the plain drawer's entry (reset) clears it before the polygon */
    ASSERT_EQ_INT(cap.n, 1); ASSERT_EQ_INT(cap.o[0].alt, 0);
    world_destroy(w);
}

TEST(op_9_reads_the_depth_words_of_the_polygon_buffer_left_by_earlier_polygons) {
    build_all();
    cap_t cap; int code; memset(&cap, 0, sizeof cap);
    fx_vec_t v[14];
    for (int i = 0; i < 14; i++) { v[i].x = 4 * i; v[i].y = 2 * i; v[i].z = 300; }
    v[0].z = 100; v[1].z = 300; v[2].z = 100;    /* the quad A, B, C, A - B + C: the fourth depth is 100 - 300 + 100 = -100, the others are in front */
    const int32_t o9[1] = { 0 }, o26[2] = { 6, 3 };
    ASSERT_EQ_INT(hcall(9, o9, 1, 5, v, 14, 0, &cap, &code), 1);    /* a first op 9: the buffer is zero and a zero depth is not negative: the quad reaches the filler */
    ASSERT_EQ_INT(cap.n, 1);
    int16_t pb[7]; world_test_pbuf(pb, 0);
    ASSERT_EQ_INT(pb[0], 100); ASSERT_EQ_INT(pb[1], 300); ASSERT_EQ_INT(pb[2], 100); ASSERT_EQ_INT(pb[3], -100); ASSERT_EQ_INT(pb[4], 0);
    /* op 26 with count 6 from slot 3 writes seven vertices (slots 3 to 9), so the positions 4, 5 and 6 hold the depths of slots 7, 8 and 9 */
    for (int i = 3; i <= 9; i++) v[i].z = 250 + i;
    cap.n = 0; world_test_handler(26, o26, 2, 7, v, 14, 0, 1, cap_sink, &cap, &code);   /* (the reset clears the buffer first) */
    world_test_pbuf(pb, 0);
    for (int i = 0; i < 7; i++) ASSERT_EQ_INT(pb[i], 253 + i);
    /* the residue decides op 9: all three words negative drops the quad (return 0, nothing to the filler), one positive word does not */
    v[7].z = -50; v[8].z = -60; v[9].z = -70;
    cap.n = 0; world_test_handler(26, o26, 2, 7, v, 14, 0, 1, cap_sink, &cap, &code);
    cap.n = 0; ASSERT_EQ_INT(world_test_handler(9, o9, 1, 5, v, 14, 0, 0, cap_sink, &cap, &code), 0); ASSERT_EQ_INT(cap.n, 0); ASSERT_EQ_INT(code, 0);
    v[8].z = 60;
    cap.n = 0; world_test_handler(26, o26, 2, 7, v, 14, 0, 1, cap_sink, &cap, &code);
    cap.n = 0; ASSERT_EQ_INT(world_test_handler(9, o9, 1, 5, v, 14, 0, 0, cap_sink, &cap, &code), 1); ASSERT_EQ_INT(cap.n, 1);
    /* the buffer is not cleared between objects: a later op 9 with reset 0 (a following object) sees the same residue, and op 10 does not read it */
    v[8].z = -60;
    cap.n = 0; world_test_handler(26, o26, 2, 7, v, 14, 0, 1, cap_sink, &cap, &code);
    cap.n = 0; world_test_handler(10, o9, 1, 5, v, 14, 0, 0, cap_sink, &cap, &code); ASSERT_EQ_INT(cap.n, 1);
    cap.n = 0; ASSERT_EQ_INT(world_test_handler(9, o9, 1, 5, v, 14, 0, 0, cap_sink, &cap, &code), 0);
    world_destroy(NULL);
}

TEST(op_3_stores_the_vertices_before_its_exits_and_the_count_between_the_all_behind_and_the_face_tests) {
    build_all();
    cap_t cap; int code, sh, cnt; memset(&cap, 0, sizeof cap);
    fx_vec_t v[14];
    for (int i = 0; i < 14; i++) { v[i].x = 10 * i; v[i].y = 5 * i; v[i].z = 300 + i; }
    const int32_t o24[5] = { 4, 0, 1, 2, 3 };
    world_test_handler(24, o24, 5, 4, v, 14, 0, 1, cap_sink, &cap, &code);        /* a polygon of 4 vertices: count 4 */
    world_test_pwords(&sh, &cnt); ASSERT_EQ_INT(cnt, 4);
    const int32_t o3[4] = { 3, 0, 1, 2 };
    for (int i = 0; i < 14; i++) v[i].z = -200 - i;                                /* every depth behind: -1, the vertices are in the buffer, the old count stays */
    ASSERT_EQ_INT(world_test_handler(3, o3, 4, 5, v, 14, 0, 0, cap_sink, &cap, &code), -1);
    int16_t pb[7]; world_test_pbuf(pb, 0);
    ASSERT_EQ_INT(pb[0], -200); ASSERT_EQ_INT(pb[2], -202);
    world_test_pwords(&sh, &cnt); ASSERT_EQ_INT(sh, 0); ASSERT_EQ_INT(cnt, 4);
    for (int i = 0; i < 14; i++) { v[i].z = 300; v[i].x = (i & 1) ? 40 : -40; v[i].y = i == 2 ? -40 : 40; }   /* a face turned away (W = 5: the face test applies): -1 with the new count */
    v[0].x = 40; v[1].x = -40; v[2].x = 0;
    int w = world_test_handler(3, o3, 4, 5, v, 14, 0, 0, cap_sink, &cap, &code);
    world_test_pwords(&sh, &cnt); ASSERT_EQ_INT(cnt, 3);
    (void)w;
}

/* note 03 "Op 9 reads past its quad" (the sea mesh as writer, V mesh_check.py): the pass clears the shift word at entry, also with no polygon, and every polygon stores its count word and n vertices; the residue is the sequence of all of them */
TEST(the_sea_mesh_pass_writes_every_polygon_into_the_polygon_buffer_in_order) {
    build_all();
    int sh, cnt; int16_t pb[7];
    fx_vec_t a[6], b[3];
    for (int i = 0; i < 6; i++) { a[i].x = i; a[i].y = i; a[i].z = 100 + i; }
    for (int i = 0; i < 3; i++) { b[i].x = i; b[i].y = i; b[i].z = 500 + i; }
    world_test_pbuf((int16_t[7]){ 9, 9, 9, 9, 9, 9, 9 }, 1);
    world_mesh_pbuf_begin(NULL);
    world_test_pwords(&sh, &cnt); ASSERT_EQ_INT(sh, 0);
    world_mesh_pbuf_polygon(NULL, a, 6);
    world_mesh_pbuf_polygon(NULL, b, 3);
    world_test_pbuf(pb, 0);
    world_test_pwords(&sh, &cnt); ASSERT_EQ_INT(cnt, 3);
    ASSERT_EQ_INT(pb[0], 500); ASSERT_EQ_INT(pb[2], 502);
    ASSERT_EQ_INT(pb[3], 103); ASSERT_EQ_INT(pb[5], 105); ASSERT_EQ_INT(pb[6], 9);   /* the positions from n on keep the earlier polygons' words */
}

/* ---- the trailing data slot (note 07 8.10 rule 5): the draw's vertex loop stores b8 + 1 slots when b8 = b9, the last one the rotated data triple of the level; b8 slots when b8 > b9 ---------------- */
static void put_slot_model(int id, int b8, int b9, int16_t d0, int16_t d1, int16_t d2) {
    model_b m;
    mb_init(&m, id);
    mb_vertex(&m, 10, 20, 30); mb_vertex(&m, 40, 50, 60); mb_vertex(&m, 70, 80, 90);
    { int o[2] = { 0, 1 }; mb_item(&m, 2, 1, 6, 0, 2, o); }
    { int p = mb_prog(&m, 0, 1); mb_draw(&m, p); mb_level(&m, 0, 2, 1, 0); }
    m.lv[0].bflags = 1; m.lv[0].b8 = (uint8_t)b8; m.lv[0].b9 = (uint8_t)b9;
    m.lv[0].data[0] = d0; m.lv[0].data[1] = d1; m.lv[0].data[2] = d2;
    put_model(&m);
    buf_t *ix = file_buf("models/index.txt");
    char line[48]; const int n = snprintf(line, sizeof line, "%d\tmodel_%03d.bin\tok\n", id, id);
    memcpy(ix->d + ix->n, line, (size_t)n); ix->n += (size_t)n;
}

TEST(the_vertex_loop_stores_b8_plus_1_slots_the_last_the_data_triple_when_b8_equals_b9) {
    build_all();
    put_slot_model(40, 3, 3, 111, -222, 333);
    world_t *w = world_load(&LD, NULL, 0);
    lists_t *l = world_lists(w);
    lists_set_models(l, world_lists_models(w));
    lists_mission_start(l);
    memset(&l->ent[3], 0, sizeof l->ent[3]);
    l->ent[3].active = 1; l->ent[3].model = 40;
    l->ent[3].mat_b[0] = l->ent[3].mat_b[4] = l->ent[3].mat_b[8] = 16384;   /* the identity */
    l->view.external = 1;
    for (int k = 0; k < LISTS_VSLOTS; k++) l->vslot[3][k][0] = l->vslot[3][k][1] = l->vslot[3][k][2] = 777;
    int completed = 0;
    lists_draw_entity(l, 3, 0, 100, &completed);
    ASSERT_EQ_INT(completed, 1);
    ASSERT_EQ_INT(l->vslot[3][0][0], 10); ASSERT_EQ_INT(l->vslot[3][2][2], 90);
    ASSERT_EQ_INT(l->vslot[3][3][0], 111); ASSERT_EQ_INT(l->vslot[3][3][1], -222); ASSERT_EQ_INT(l->vslot[3][3][2], 333);   /* slot b8: the rotated data triple */
    ASSERT_EQ_INT(l->vslot[3][4][0], 777);                                                                                  /* slot b8 + 1 is not stored */
    world_destroy(w);
}

TEST(the_vertex_loop_stores_b8_slots_when_b8_exceeds_b9_and_no_data_triple) {
    build_all();
    put_slot_model(41, 3, 2, 111, -222, 333);
    world_t *w = world_load(&LD, NULL, 0);
    lists_t *l = world_lists(w);
    lists_set_models(l, world_lists_models(w));
    lists_mission_start(l);
    memset(&l->ent[3], 0, sizeof l->ent[3]);
    l->ent[3].active = 1; l->ent[3].model = 41;
    l->ent[3].mat_b[0] = l->ent[3].mat_b[4] = l->ent[3].mat_b[8] = 16384;
    l->view.external = 1;
    for (int k = 0; k < LISTS_VSLOTS; k++) l->vslot[3][k][0] = 777;
    int completed = 0;
    lists_draw_entity(l, 3, 0, 100, &completed);
    ASSERT_EQ_INT(completed, 1);
    ASSERT_EQ_INT(l->vslot[3][2][0], 70);
    ASSERT_EQ_INT(l->vslot[3][3][0], 777);
    world_destroy(w);
}

/* ---- the effect objects (note 07 11.1 to 11.3): the secondary object reaches the draw through the walker, models 33 to 35, ops 70, 71 and 62 -------------------------------------------------------- */
/* An invented stand-in for model 33: the shape of the starburst program with the numbers of a test (vertices 12 to 17 a column above the origin, one variant of the four): op 70 (2 bytes), the empty item
 * (op 0, last, 2 bytes), the op 71 list of six discs (38 bytes), an ascending run of six op 62 items (8 bytes each) and a descending run of six. */
static void put_starburst_model(int id) {
    model_b m;
    mb_init(&m, id);
    for (int i = 0; i < 12; i++) mb_vertex(&m, 0, 0, 0);
    for (int k = 0; k < 6; k++) mb_vertex(&m, 0, 6 * (k + 1), 0);
    uint32_t addr = 0x100;
    int first = m.ni;
    { int i = mb_item(&m, 70, 0, 0, 0, 0, NULL); m.it[i].address = addr; m.it[i].length = 2; addr += 2;
      m.it[i].skip_first = (uint32_t)m.nsk; m.it[i].skip_count = 1; m.sk[m.nsk++] = 2; }
    { int i = mb_item(&m, 0, 1, 0, 0, 0, NULL); m.it[i].address = addr; m.it[i].length = 2; addr += 2; }
    { int o[19]; o[0] = 6; for (int k = 0; k < 6; k++) { o[1 + 3 * k] = 6 * (12 + k); o[2 + 3 * k] = (k & 1) ? 10 : 8; o[3 + 3 * k] = 0x1D00 - 0x100 * k; }
      int i = mb_item(&m, 71, 1, 0, 0, 19, o); m.it[i].address = addr; m.it[i].length = 0x26; addr += 0x26; }
    for (int run = 0; run < 2; run++)
        for (int k = 0; k < 6; k++) {
            const int vert = run ? 5 - k : k;
            int o[3] = { 6 * (12 + vert), run ? 3 : 4, 0x1D00 - 0x100 * vert };
            int i = mb_item(&m, 62, k == 5, 0, 0, 3, o); m.it[i].address = addr; m.it[i].length = 8; addr += 8;
        }
    { int p = mb_prog(&m, first, m.ni - first); mb_draw(&m, p); mb_level(&m, 0, 2, 1, 0); }
    put_model(&m);
    buf_t *ix = file_buf("models/index.txt");
    char line[48]; const int n = snprintf(line, sizeof line, "%d\tmodel_%03d.bin\tok\n", id, id);
    memcpy(ix->d + ix->n, line, (size_t)n); ix->n += (size_t)n;
}

/* One builder frame with the camera at the centre of cell (10, 10) and the effect object 0 (the id word 0x11) in the same cell, `ahead` position units in front, with the life word `life`. */
static void effect_frame(world_t *w, int32_t ahead, int life) {
    world_camera_t c; memset(&c, 0, sizeof c);
    c.pos[0] = 10 * 4194304 + 2097152; c.pos[2] = 10 * 4194304 + 2097152;
    world_set_camera(w, &c);
    lists_frame_t in; memset(&in, 0, sizeof in);
    lists_entity_t *o = &in.sec[0];
    o->active = 1; o->cell_x = 10; o->cell_z = 10; o->fine_x = 8192; o->fine_z = (int16_t)(8192 + (ahead >> 8)); o->sub = 5; o->model = 0x11; o->phase = (int16_t)life;
    o->pos[0] = (10 << 22) + (8192 << 8); o->pos[2] = (10 << 22) + ((8192 << 8) + ahead);
    lists_entity_t *e = &in.ent[14];
    e->active = 1; e->cell_x = 70; e->cell_z = 65; e->fine_x = 8192; e->fine_z = 8192; e->sub = 5; e->model = WORLD_CARRIER_ID;
    world_lists_frame(w, &in);
}

static int effect_entry(const game_scene_t *s) {
    for (unsigned i = 0; i < s->entry_count; i++) if (s->entries[i].kind == GAME_ENTRY_SECONDARY) return (int)i;
    return -1;
}

TEST(a_secondary_object_is_walked_by_its_descriptor_and_drawn_with_the_model_of_its_id_word_plus_16) {
    build_all();
    put_starburst_model(33);
    world_t *w = world_load(&LD, NULL, 0);
    effect_frame(w, 6000, 0x40);
    game_scene_t s; memset(&s, 0, sizeof s);
    world_fill_scene(w, &s);
    const int ix = effect_entry(&s);
    ASSERT_TRUE(ix >= 0);
    const game_entry_t *e = &s.entries[ix];
    ASSERT_EQ_INT(e->asset_id, 33);                 /* the id word 0x11 names the descriptor; the model is that plus 16 (not 17) */
    ASSERT_EQ_INT(e->model_id, 0x11);
    ASSERT_EQ_INT(e->draw, 1);                      /* the walker ran the draw routine: the E1 exit belongs to the entity routine only */
    ASSERT_EQ_INT(e->phase, 0x40);
    /* the draw: life 0x40 is the steady fire, variant 0: the list of six discs, then the program ends */
    cap_t cap; world_draw_info_t info; memset(&cap, 0, sizeof cap); memset(&info, 0, sizeof info);
    game_camera_t cam; memset(&cam, 0, sizeof cam);
    cam.pos.x = 10 * 4194304 + 2097152; cam.pos.z = 10 * 4194304 + 2097152;
    ASSERT_EQ_INT(world_draw_entry(w, e, &cam, cap_sink, &cap, &info), 0);
    ASSERT_EQ_INT(cap.n, 6);
    for (int k = 0; k < 6; k++) { ASSERT_EQ_INT(cap.o[k].kind, WORLD_OUT_DISC); ASSERT_EQ_INT(cap.o[k].pen, (k & 1) ? 10 : 8); }
    world_destroy(w);
}

TEST(op_70_skips_by_the_life_word_a_negative_word_draws_nothing_and_the_runs_start_where_the_table_says) {
    build_all();
    put_starburst_model(33);
    world_t *w = world_load(&LD, NULL, 0);
    static const struct { int life, discs; } T[] = {
        { -1, 0 }, { -5, 0 },     /* a negative life word skips nothing: the empty item ends the program */
        { 0x40, 6 }, { 0x48, 6 }, { 0x70, 6 },   /* the steady fire: the list of six (variant (t & 7) >> 1: only variant 0 exists in this model, so the words with t & 7 below 2 land on it) */
        { 0x78, 4 }, { 0x79, 4 },  /* above 0x74: the descending run from disc m = (0x78 - 0x74) >> 1 = 2: four discs */
        { 0x00, 1 }, { 0x01, 1 }, /* below 0x0C: the ascending run from m = (0x0B - t) >> 1 = 5: the top disc only */
        { 0x08, 5 }, { 0x09, 5 },  /* m = 1: five discs */
        { 0x80, 0 } };            /* note 07 11.3 (settled: the observed picture): nothing is drawn for 0x80 and above */
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        effect_frame(w, 6000, T[i].life);
        game_scene_t s; memset(&s, 0, sizeof s);
        world_fill_scene(w, &s);
        const int ix = effect_entry(&s);
        ASSERT_TRUE(ix >= 0);
        cap_t cap; world_draw_info_t info; memset(&cap, 0, sizeof cap); memset(&info, 0, sizeof info);
        game_camera_t cam; memset(&cam, 0, sizeof cam);
        cam.pos.x = 10 * 4194304 + 2097152; cam.pos.z = 10 * 4194304 + 2097152;
        world_draw_entry(w, &s.entries[ix], &cam, cap_sink, &cap, &info);
        if (cap.n != T[i].discs) { fprintf(stderr, "life %d: %d discs, expected %d\n", T[i].life, cap.n, T[i].discs); }
        ASSERT_EQ_INT(cap.n, T[i].discs);
    }
    world_destroy(w);
}

/* ---- the cone handlers (note 07 11.5): the phase chooses the detail list of the pod (models 2 and 13) and the parachute (model 11) ---------------------------------------------------------------- */
static void put_cone_model(int id) {
    model_b m;
    static const int tri[3] = { 0, 1, 2 };
    mb_init(&m, id);
    mb_vertex(&m, 0, 0, 0); mb_vertex(&m, 100, 0, 0); mb_vertex(&m, 0, 100, 0);
    mb_poly(&m, 1 | 8, 0x8005, 5, 3, tri); { int p = mb_prog(&m, 0, 1); mb_draw(&m, p); }
    for (int i = 0; i < 6; i++) mb_level(&m, i == 0 ? 12 : 0, 2, 1, 0);   /* the own list (two levels) and the four phase lists */
    m.nn = 2; m.phase1 = 3;
    put_model(&m);   /* the ids 2 and 11 are listed by build_all: the file is replaced */
}

TEST(the_cone_handlers_start_the_detail_list_by_the_phase_and_the_parachutes_phase_0_reads_the_low_nibble_of_the_gear_byte) {
    build_all();
    put_cone_model(2); put_cone_model(11);
    world_t *w = world_load(&LD, NULL, 0);
    ASSERT_TRUE(w != NULL);
    static const struct { int model, phase, gear, list, level; } T[] = {
        { 2, 9, 0, 0, 0 }, { 2, 5, 0, 0, 0 }, { 2, 4, 0, 1, 2 }, { 2, 3, 0, 1, 2 }, { 2, 2, 0, 2, 3 }, { 2, 1, 0, 2, 3 }, { 2, 0, 0, 3, 4 }, { 2, 0, 5, 3, 4 }, { 2, -1, 0, 4, 5 },
        { 11, 7, 0, 0, 0 }, { 11, 3, 0, 1, 2 }, { 11, 1, 0, 2, 3 }, { 11, 0, 5, 3, 4 }, { 11, 0, 0, 4, 5 }, { 11, -3, 5, 4, 5 } };
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        world_camera_t c; memset(&c, 0, sizeof c);
        c.pos[0] = 10 * 4194304 + 2097152; c.pos[2] = 10 * 4194304 + 2097152;
        world_set_camera(w, &c);
        lists_frame_t in; memset(&in, 0, sizeof in);
        lists_entity_t *o = &in.ent[3];
        o->active = 1; o->cell_x = 10; o->cell_z = 10; o->fine_x = 8192; o->fine_z = 8192 + 16; o->sub = 5; o->model = (uint16_t)T[i].model; o->phase = (int16_t)T[i].phase; o->b7c = (uint8_t)T[i].gear;
        o->pos[0] = (10 << 22) + (8192 << 8); o->pos[2] = (10 << 22) + ((8192 + 16) << 8);
        lists_entity_t *e = &in.ent[14];
        e->active = 1; e->cell_x = 70; e->cell_z = 65; e->fine_x = 8192; e->fine_z = 8192; e->sub = 5; e->model = WORLD_CARRIER_ID;
        world_lists_frame(w, &in);
        game_scene_t s; memset(&s, 0, sizeof s);
        world_fill_scene(w, &s);
        int ix = -1;
        for (unsigned k = 0; k < s.entry_count; k++) if (s.entries[k].kind == GAME_ENTRY_ENTITY && s.entries[k].id == 3) ix = (int)k;
        ASSERT_TRUE(ix >= 0);
        ASSERT_EQ_INT(s.entries[ix].cone_list, T[i].list);
        cap_t cap; world_draw_info_t info; memset(&cap, 0, sizeof cap); memset(&info, 0, sizeof info);
        game_camera_t cam; memset(&cam, 0, sizeof cam);
        cam.pos.x = 10 * 4194304 + 2097152; cam.pos.z = 10 * 4194304 + 2097152;
        world_draw_entry(w, &s.entries[ix], &cam, cap_sink, &cap, &info);
        ASSERT_EQ_INT(info.level, T[i].level);
    }
    world_destroy(w);
}

TEST(tile_cells_wrap_at_the_edge_of_the_grid_and_labels_do_not) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    /* the camera in cell (127, 0): the tile (1, 0) wraps to cell (0, 0) (a value above the limit becomes 0); its label keeps 127 + 1 = 128 in the low byte */
    lists_frame_at(w, 127 * 4194304 + 2097152, 2097152, 0, 1, 1);
    const lists_t *l = world_lists(w);
    ASSERT_EQ_INT(l->kept[2], 1);
    ASSERT_EQ_INT(l->q[2][0].id, 1);
    ASSERT_TRUE(l->q[2][0].key == 0x00108000u);
    ASSERT_EQ_INT(l->q[2][0].x, 28672);
    /* the scene places it across the wrap, continuous with the camera */
    game_scene_t s; memset(&s, 0, sizeof s);
    world_fill_scene(w, &s);
    ASSERT_TRUE(s.entries[s.entry_count - 1].pos.x == 127 * 4194304 + (28672 << 8));
    /* far from everything: nothing scanned has terrain */
    lists_frame_at(w, 100 * 4194304, 100 * 4194304, 0, 1, 1);
    ASSERT_EQ_INT(world_lists(w)->kept[2], 0);
    world_destroy(w);
}

TEST(scene_through_game_create_input_step_scene) {
    build_all();
    char err[160] = "";
    game_t *g = game_create(&LD, err, sizeof err);
    ASSERT_TRUE(g != NULL);
    game_set_debug_freecam(g, 1);
    game_scene_t s;
    game_scene(g, &s);
    ASSERT_EQ_INT(s.mode, GAME_MODE_CREDITS);
    game_event_t ev = { GAME_EV_BUTTON, GAME_BTN_RETURN, 1 };
    game_debug_start_flight(g, 3, 0);
    game_scene(g, &s);
    ASSERT_EQ_INT(s.mode, GAME_MODE_FLIGHT);
    /* starts over the carrier: x, z at the carrier, altitude deck height + FREECAM_START_ALT */
    ASSERT_TRUE(s.camera.pos.x == 10 * 4194304 + 2097152);
    ASSERT_TRUE(s.camera.pos.z == 10 * 4194304 + 2097152 - FREECAM_START_BACK);   /* behind the carrier */
    ASSERT_EQ_INT((int)s.camera.pos.y, 1024 + FREECAM_START_ALT);
    ASSERT_EQ_INT(s.hud.heading_deg, 0);
    ASSERT_EQ_INT(s.hud.altitude_ft, 5 * ((1024 + FREECAM_START_ALT) >> 10));
    /* idle throttle: stepping does not move */
    game_step(g);
    game_scene_t s2; game_scene(g, &s2);
    ASSERT_TRUE(s2.camera.pos.z == s.camera.pos.z);
    ASSERT_EQ_INT(s2.tick, 1);
    /* full throttle moves ahead (+z at heading 0) */
    ev.kind = GAME_EV_AXIS; ev.code = GAME_AXIS_THROTTLE; ev.value = 32767; game_input(g, &ev);
    game_step(g); game_scene(g, &s2);
    ASSERT_TRUE(s2.camera.pos.z > s.camera.pos.z);
    ASSERT_TRUE(s2.camera.pos.x == s.camera.pos.x);
    /* stick right turns right: heading grows; stick back pitches up */
    ev.code = GAME_AXIS_STICK_X; ev.value = 32767; game_input(g, &ev);
    ev.code = GAME_AXIS_STICK_Y; ev.value = 32767; game_input(g, &ev);
    for (int i = 0; i < 10; i++) game_step(g);
    game_scene(g, &s2);
    ASSERT_TRUE(s2.hud.heading_deg > 20 && s2.hud.heading_deg < 40);   /* 10 steps * 240 / 80 = 30 degrees */
    ASSERT_TRUE(s2.hud.pitch_deg > 15 && s2.hud.pitch_deg < 25);       /* 10 * 160 / 80 = 20 degrees */
    ASSERT_TRUE(s2.camera.pitch > 1400 && s2.camera.pitch < 1800);     /* 20 degrees = 1600 units of 28800 per circle */
    ASSERT_TRUE(s2.camera.pos.y > s.camera.pos.y);
    game_destroy(g);
}

/* ---- views, the aircraft and the free camera (milestone 3, Task 2) ------------------------- */

static game_t *start_flight(game_scene_t *s) {
    build_all();
    game_t *g = game_create(&LD, NULL, 0);
    game_debug_start_flight(g, 3, 0);
    game_scene(g, s);
    return g;
}
static void press(game_t *g, int code, int value) {
    game_event_t ev = { GAME_EV_BUTTON, code, value };
    game_input(g, &ev);
}
static void kp(game_t *g, int key) { press(g, GAME_BTN_KEYPAD, key); }

TEST(aircraft_starts_at_the_carrier_block_and_the_cockpit_camera_is_the_aircraft) {
    game_scene_t s;
    game_t *g = start_flight(&s);
    ASSERT_EQ_INT(s.mode, GAME_MODE_FLIGHT);
    ASSERT_EQ_INT(s.camera.view, GAME_VIEW_COCKPIT);
    ASSERT_EQ_INT(s.camera.head, 0);
    ASSERT_TRUE(s.camera.pos.x == 10 * 4194304 + 8192 * 256);   /* position block entry 33 */
    ASSERT_TRUE(s.camera.pos.z == 10 * 4194304 + 8192 * 256);
    ASSERT_EQ_INT((int)s.camera.pos.y, 4 * 256);
    ASSERT_EQ_INT(s.camera.yaw, 0);
    game_step(g); game_scene(g, &s);   /* the aircraft sits still until the flight module exists */
    ASSERT_TRUE(s.camera.pos.z == 10 * 4194304 + 8192 * 256);
    game_destroy(g);
}

TEST(hud_is_zero_until_a_flight_module_writes_it) {
    game_scene_t s;
    game_t *g = start_flight(&s);
    game_step(g); game_scene(g, &s);
    game_hud_t zero; memset(&zero, 0, sizeof zero);
    ASSERT_TRUE(memcmp(&s.hud, &zero, sizeof zero) == 0);
    game_destroy(g);
}

static void step_scene(game_t *g, game_scene_t *s) { game_step(g); game_scene(g, s); }

TEST(view_events_switch_the_view_kind_and_the_head) {
    game_scene_t s;
    game_t *g = start_flight(&s);
    kp(g, 6);          /* KP6: 0 -> 1 */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.head, 1);
    ASSERT_EQ_INT(s.camera.yaw, 2256);                 /* the table 0x3239A (28.2 degrees); without flight tables the words are the record's plus the triple's yaw (C, missing-asset fallback) */
    kp(g, 4);           /* KP4: back to 0, then 11 */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.head, 0);
    kp(g, 4);
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.head, 11);
    kp(g, 2);           /* KP2 */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.head, 6);
    ASSERT_EQ_INT(s.camera.yaw, 14400);
    kp(g, 7);         /* KP7 */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.head, 12);
    kp(g, 4);          /* KP4 from 12 gives 0 */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.head, 0);
    kp(g, 1);          /* KP1 */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.head, 13);
    kp(g, 6);          /* KP6 from 13 gives 0 */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.head, 0);
    kp(g, 6); kp(g, 8);          /* KP8 */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.head, 0);
    kp(g, 6); kp(g, 9);          /* KP9 gives code 0, not a second forward view */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.head, 0);
    kp(g, GAME_KP_ENTER);                /* KPENTER: a request, applied by the next pass: external, chase (ring 4) */
    game_scene(g, &s); ASSERT_EQ_INT(s.camera.view, GAME_VIEW_COCKPIT);
    step_scene(g, &s);
    ASSERT_EQ_INT(s.camera.view, GAME_VIEW_CHASE);
    ASSERT_EQ_INT(s.camera.ring, 4);
    ASSERT_EQ_INT(s.camera.head, 0);
    kp(g, 6);          /* outside, KP6 picks ring 2, not a head turn */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.head, 0); ASSERT_EQ_INT(s.camera.ring, 2);
    kp(g, 8);                  /* KP8 = ring 0 */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.view, GAME_VIEW_EXTERNAL); ASSERT_EQ_INT(s.camera.ring, 0);
    kp(g, GAME_KP_DOT);                 /* KPDOT = ring 9 */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.ring, 9);
    kp(g, 5);                  /* KP5 = ring 8 */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.ring, 8);
    kp(g, 0);                  /* KP0 is not a view key */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.ring, 8);
    kp(g, GAME_KP_ENTER);                /* toggles back */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.view, GAME_VIEW_COCKPIT);
    kp(g, 3);                  /* KP3 and KP-dot store the direction in the cockpit too; KP-Enter then sets 4 again */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.view, GAME_VIEW_COCKPIT); ASSERT_EQ_INT(s.camera.ring, 4);
    kp(g, GAME_KP_ENTER); step_scene(g, &s); ASSERT_EQ_INT(s.camera.ring, 4);
    game_destroy(g);
}

/* a bare view state for the camera builder: the external view at level -1, direction d */
static void vs_ext(world_viewstate_t *vs, int ring) {
    memset(vs, 0, sizeof *vs);
    vs->level = -1; vs->ring = ring; vs->look = 1; vs->aim_on = 1;
}

TEST(chase_camera_sits_behind_the_aircraft_by_the_ring_offset_matrix_shifted_right_4) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    int16_t r[3];
    ASSERT_EQ_INT(world_camera_ring(w, 0, 4, r), 0);   /* synthetic: (4, -4, -15): behind */
    ASSERT_TRUE(r[2] < 0);
    aircraft_t ac; memset(&ac, 0, sizeof ac);
    ac.pos[0] = 1000000; ac.pos[1] = 50000; ac.pos[2] = 2000000;
    world_viewstate_t vs; world_camera_t c;
    /* a record without matrices takes the heading-only matrix (C, missing-asset fallback); yaw 0: (c, 0, s) = (16384, 0, 0): out = w x 16384 >> 4, one table unit is 1024 position units */
    vs_ext(&vs, 4);
    ASSERT_EQ_INT(world_camera_build(w, &ac, &vs, &c), 0);
    ASSERT_TRUE(c.pos[0] == ac.pos[0] + r[0] * 1024);
    ASSERT_TRUE(c.pos[1] == ac.pos[1] + r[1] * 1024);
    ASSERT_TRUE(c.pos[2] == ac.pos[2] + r[2] * 1024);
    /* yaw 90 degrees (7200, clockwise): ahead is -x, the left is +z */
    ac.yaw = 7200; vs_ext(&vs, 4);
    ASSERT_EQ_INT(world_camera_build(w, &ac, &vs, &c), 0);
    ASSERT_TRUE(c.pos[0] == ac.pos[0] - r[2] * 1024);
    ASSERT_TRUE(c.pos[2] == ac.pos[2] + r[0] * 1024);
    /* the class byte selects the table (0x11, 0x14, the high nibble 0x30; every other class the default) */
    ac.yaw = 0;
    int16_t r2[3], r1[3], r3[3];
    world_camera_ring(w, 2, 4, r2); world_camera_ring(w, 1, 4, r1); world_camera_ring(w, 3, 4, r3);
    ac.f.cls = 0x14; vs_ext(&vs, 4); world_camera_build(w, &ac, &vs, &c);
    ASSERT_TRUE(c.pos[0] == ac.pos[0] + r2[0] * 1024);
    ac.f.cls = 0x11; vs_ext(&vs, 4); world_camera_build(w, &ac, &vs, &c);
    ASSERT_TRUE(c.pos[0] == ac.pos[0] + r1[0] * 1024);
    ac.f.cls = 0x34; vs_ext(&vs, 4); world_camera_build(w, &ac, &vs, &c);
    ASSERT_TRUE(c.pos[0] == ac.pos[0] + r3[0] * 1024);
    ac.f.cls = 0x10; vs_ext(&vs, 4); world_camera_build(w, &ac, &vs, &c);
    ASSERT_TRUE(c.pos[0] == ac.pos[0] + r[0] * 1024);
    vs_ext(&vs, 10);
    ASSERT_EQ_INT(world_camera_build(w, &ac, &vs, &c), -1);   /* bad ring index */
    vs_ext(&vs, 4); vs.level = 5;
    ASSERT_EQ_INT(world_camera_build(w, &ac, &vs, &c), -1);   /* bad level */
    world_destroy(w);
}

TEST(the_level_scales_each_ring_word_and_the_height_clamp_is_deck_plus_7_times_256) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    int16_t r[3];
    world_camera_ring(w, 0, 4, r);   /* (4, -4, -15) */
    aircraft_t ac; memset(&ac, 0, sizeof ac);
    ac.pos[0] = 1000000; ac.pos[1] = 5000000; ac.pos[2] = 2000000;
    world_viewstate_t vs; world_camera_t c;
    static const int lvl[5] = { -1, 1, 2, 3, 4 };
    for (int k = 0; k < 5; k++) {
        vs_ext(&vs, 4); vs.level = lvl[k];
        world_camera_build(w, &ac, &vs, &c);
        int sx[5][3];
        for (int j = 0; j < 3; j++) {
            sx[0][j] = r[j]; sx[1][j] = r[j] + (r[j] >> 1); sx[2][j] = r[j] * 2; sx[3][j] = r[j] * 4; sx[4][j] = r[j] * 8;
        }
        ASSERT_TRUE(c.pos[0] == ac.pos[0] + sx[k][0] * 1024);
        ASSERT_TRUE(c.pos[1] == ac.pos[1] + sx[k][1] * 1024);
        ASSERT_TRUE(c.pos[2] == ac.pos[2] + sx[k][2] * 1024);
    }
    /* level 1 is per word: -4 x 1.5 = -4 + (-4 >> 1) = -6, -15 -> -15 + (-8) = -23 */
    vs_ext(&vs, 4); vs.level = 1; world_camera_build(w, &ac, &vs, &c);
    ASSERT_TRUE(c.pos[2] == ac.pos[2] - 23 * 1024);
    /* the clamp: a camera below (deck + 7) x 256 is lifted to it; the deck word counts only with bit 6 or 7 of +0x04 */
    ac.pos[1] = 100; vs_ext(&vs, 4); world_camera_build(w, &ac, &vs, &c);
    ASSERT_EQ_INT(c.pos[1], 7 * 256);
    ac.f.deck = 112; vs_ext(&vs, 4); world_camera_build(w, &ac, &vs, &c);
    ASSERT_EQ_INT(c.pos[1], 7 * 256);                    /* b4 has no deck bit: the deck word is ignored */
    ac.f.b4 = 0x40; vs_ext(&vs, 4); world_camera_build(w, &ac, &vs, &c);
    ASSERT_EQ_INT(c.pos[1], (112 + 7) * 256);
    world_destroy(w);
}

TEST(the_ring_turns_by_4_when_the_heading_jumps_by_7200_to_21600) {
    build_all();
    world_t *w = world_load(&LD, NULL, 0);
    aircraft_t ac; memset(&ac, 0, sizeof ac);
    ac.pos[1] = 5000000;
    world_viewstate_t vs; world_camera_t c;
    vs_ext(&vs, 2); vs.heading = 0; ac.yaw = 7199;
    world_camera_build(w, &ac, &vs, &c); ASSERT_EQ_INT(vs.ring, 2);      /* 7199: no */
    vs_ext(&vs, 2); vs.heading = 0; ac.yaw = 7200;
    world_camera_build(w, &ac, &vs, &c); ASSERT_EQ_INT(vs.ring, 6);      /* 7200 inclusive; the heading is stored */
    ASSERT_EQ_INT(vs.heading, 7200);
    vs_ext(&vs, 6); vs.heading = 28000; ac.yaw = 100;                    /* not wrapped round the circle: 27900 */
    world_camera_build(w, &ac, &vs, &c); ASSERT_EQ_INT(vs.ring, 6);
    vs_ext(&vs, 5); vs.heading = 0; ac.yaw = 21600;
    world_camera_build(w, &ac, &vs, &c); ASSERT_EQ_INT(vs.ring, 1);      /* 21600 inclusive */
    for (int d = 0; d < 10; d++) {                                       /* 0, 4, 8, 9 stay */
        vs_ext(&vs, d); vs.heading = 0; ac.yaw = 14400;
        world_camera_build(w, &ac, &vs, &c);
        ASSERT_EQ_INT(vs.ring, (d == 0 || d == 4 || d >= 8) ? d : (d + 4) % 8);
    }
    world_destroy(w);
}

TEST(object_attitude_matrix_and_eye_follow_note_03) {
    fx_mat_t r;
    world_object_matrix(&r, 0, 0, 0);
    ASSERT_EQ_INT(r.m[0][0], 256); ASSERT_EQ_INT(r.m[1][1], 256); ASSERT_EQ_INT(r.m[2][2], 256);
    ASSERT_EQ_INT(r.m[0][1], 0); ASSERT_EQ_INT(r.m[2][0], 0);
    /* yaw 90 degrees (clockwise): the model's forward (z) points along world -x (note 03: heading 90 faces -x) */
    world_object_matrix(&r, 0, 0, 7200);
    fx_vec_t v = { 0, 0, 1000 }, o;
    fx_rotate(&o, &r, &v);
    ASSERT_EQ_INT(o.x, -1000); ASSERT_EQ_INT(o.z, 0); ASSERT_EQ_INT(o.y, 0);
    /* a camera ahead of that object along its heading (world -x) sees it straight on: the eye in model axes lies on +z */
    int32_t delta[3] = { -5000, 0, 0 }, eye[3];   /* camera minus object */
    world_object_eye(&r, delta, eye);
    ASSERT_EQ_INT(eye[2], 5000); ASSERT_EQ_INT(eye[0], 0);
    /* the transposed use inverts the rotation: eye of the rotated vector gives back the vector */
    fx_vec_t back;
    int32_t ro[3] = { o.x, o.y, o.z }, e2[3];
    world_object_eye(&r, ro, e2);
    back.x = e2[0]; back.z = e2[2];
    ASSERT_EQ_INT(back.x, 0); ASSERT_EQ_INT(back.z, 1000);
}

TEST(scene_carries_the_sky_block_with_the_scaled_matrix_and_the_side_flag) {
    /* note 04, Task 8 obligations: the nine matrix words after the zoom row scale, the height long, the angle words, the window row, the zoom triple, the side flag
     * and the fade target are exported every flight frame */
    game_scene_t s0, s;
    game_t *g = start_flight(&s0);
    ASSERT_EQ_INT(s0.sky.valid, 0);               /* nothing before the first step */
    game_step(g);
    game_scene(g, &s);
    ASSERT_EQ_INT(s.sky.valid, 1);
    ASSERT_EQ_INT(s.sky.cockpit, s.camera.view == GAME_VIEW_COCKPIT);   /* the selector V:4A83D follows the view */
    ASSERT_EQ_INT(s.sky.zoom[0], 0xA8); ASSERT_EQ_INT(s.sky.zoom[1], 0xFC); ASSERT_EQ_INT(s.sky.zoom[2], 0x80);
    ASSERT_EQ_INT(s.sky.row_limit, s.camera.view == GAME_VIEW_COCKPIT ? 144 : 167);
    ASSERT_EQ_INT(s.sky.fade_target, 15);
    ASSERT_EQ_INT(s.sky.cam_y, -s.camera.pos.y);
    {   /* the row norms of the scaled matrix are the zoom triple (the unscaled rows have norm 256) */
        double n0 = 0, n1 = 0, n2 = 0;
        for (int i = 0; i < 3; i++) { n0 += (double)s.sky.matrix[i] * s.sky.matrix[i]; n1 += (double)s.sky.matrix[3 + i] * s.sky.matrix[3 + i]; n2 += (double)s.sky.matrix[6 + i] * s.sky.matrix[6 + i]; }
        ASSERT_TRUE(n0 > 160.0 * 160 && n0 < 172.0 * 172 && n1 > 248.0 * 248 && n1 < 256.0 * 256 && n2 > 124.0 * 124 && n2 < 132.0 * 132);
    }
    ASSERT_TRUE(s.sky.sky_n == 0 || s.sky.sky_n >= 3);
    game_destroy(g);
}

TEST(chase_camera_through_the_game) {
    game_scene_t s, s0;
    game_t *g = start_flight(&s0);
    kp(g, GAME_KP_ENTER);
    step_scene(g, &s);
    int16_t r[3];
    world_t *w = world_load(&LD, NULL, 0);
    world_camera_ring(w, 0, 4, r);
    world_destroy(w);
    ASSERT_TRUE(s.camera.pos.x == s0.camera.pos.x + r[0] * 1024);
    ASSERT_TRUE(s.camera.pos.z == s0.camera.pos.z + r[2] * 1024);
    game_destroy(g);
}

TEST(the_map_view_hangs_over_the_aircraft_at_the_saved_altitude_and_m_returns_to_the_cockpit) {
    game_scene_t s, s0;
    game_t *g = start_flight(&s0);
    press(g, GAME_BTN_TOWER, 1);                         /* M in the cockpit: a request; the view changes in the next pass */
    game_scene(g, &s); ASSERT_EQ_INT(s.camera.view, GAME_VIEW_COCKPIT);
    step_scene(g, &s);
    ASSERT_EQ_INT(s.camera.view, GAME_VIEW_TOWER);
    ASSERT_TRUE(s.camera.pos.x == s0.camera.pos.x);      /* x and z the aircraft's, every frame */
    ASSERT_TRUE(s.camera.pos.z == s0.camera.pos.z);
    ASSERT_TRUE(s.camera.pos.y == 0x3000000);            /* V:4A71C, planted at the mission start */
    ASSERT_EQ_INT(s.camera.pitch, -7200);                /* looking straight down (pitch word 7200), north up, no roll */
    ASSERT_EQ_INT(s.camera.yaw, 0);
    ASSERT_EQ_INT(s.camera.roll, 0);
    ASSERT_EQ_INT(s.sky.row_limit, 179);
    press(g, GAME_BTN_TOWER, 1);                         /* M in the map: back to the cockpit, never to an earlier external view */
    step_scene(g, &s); ASSERT_EQ_INT(s.camera.view, GAME_VIEW_COCKPIT);
    game_destroy(g);
}

TEST(pause_freezes_the_world_and_resumes) {
    game_scene_t s;
    game_t *g = start_flight(&s);
    press(g, GAME_BTN_PAUSE, 1);
    game_scene(g, &s); ASSERT_EQ_INT(s.mode, GAME_MODE_PAUSED);
    ASSERT_EQ_INT(s.flight_blank, 1);                   /* the pause keeps V:4A84D at 0: the last picture stays, dimmed */
    game_scene_t p0 = s, p1;
    game_step(g); game_step(g); game_scene(g, &p1);
    ASSERT_EQ_INT(p1.tick, p0.tick);
    ASSERT_TRUE(p1.camera.pos.x == p0.camera.pos.x && p1.camera.pos.y == p0.camera.pos.y && p1.camera.pos.z == p0.camera.pos.z);
    press(g, GAME_BTN_PAUSE, 1);
    game_scene(g, &s); ASSERT_EQ_INT(s.mode, GAME_MODE_FLIGHT);
    press(g, GAME_BTN_BRAKE, 1); press(g, GAME_BTN_THROTTLE_UP, 1); press(g, GAME_BTN_THROTTLE_DOWN, 1);   /* accepted */
    game_destroy(g);
}

TEST(freecam_still_works_when_enabled) {
    build_all();
    game_t *g = game_create(&LD, NULL, 0);
    game_set_debug_freecam(g, 1);
    game_event_t ev = { GAME_EV_BUTTON, GAME_BTN_RETURN, 1 };
    game_debug_start_flight(g, 3, 0);
    game_scene_t s, s2;
    game_scene(g, &s);
    ASSERT_TRUE(s.camera.pos.z == 10 * 4194304 + 2097152 - FREECAM_START_BACK);
    ASSERT_TRUE(s.hud.altitude_ft != 0);   /* the free camera still fills the hud */
    ev.kind = GAME_EV_AXIS; ev.code = GAME_AXIS_THROTTLE; ev.value = 32767; game_input(g, &ev);
    game_step(g); game_scene(g, &s2);
    ASSERT_TRUE(s2.camera.pos.z > s.camera.pos.z);
    kp(g, GAME_KP_ENTER);   /* the view kind is reported (after the pass that applies the request), the camera stays the free camera */
    game_step(g); game_scene(g, &s);
    ASSERT_EQ_INT(s.camera.view, GAME_VIEW_CHASE);
    ASSERT_TRUE(s.camera.pos.z > s2.camera.pos.z);
    game_destroy(g);
}

TEST(the_debug_flight_start_without_world_data_does_nothing) {
    build_all();
    drop_file("models/index.txt");
    game_t *g = game_create(&LD, NULL, 0);
    ASSERT_TRUE(g != NULL);
    game_debug_start_flight(g, 3, 0);
    game_scene_t s; game_scene(g, &s);
    ASSERT_EQ_INT(s.mode, GAME_MODE_CREDITS);
    game_destroy(g);
}

TEST(game_create_fails_naming_a_missing_model_file) {
    build_all();
    drop_file("models/model_004.bin");
    char err[160] = "";
    game_t *g = game_create(&LD, err, sizeof err);
    ASSERT_TRUE(g == NULL);
    ASSERT_TRUE(strstr(err, "model_004.bin") != NULL);
}

/* Flight tables that exist but are refused (assets extracted without the segment table carry the marker file) make game_create fail with the message; tables
 * that are absent (every other test here) leave the static aircraft. */
static int marker_read(void *user, const char *path, uint8_t **out, size_t *len) {
    if (!strcmp(path, "tables/flight_spans_flat.txt")) { *out = (uint8_t *)malloc(1); *len = 0; return 0; }
    return fake_read(user, path, out, len);
}
TEST(game_create_reports_refused_flight_tables) {
    build_all();
    game_loader_t ld = { marker_read, fake_release, NULL };
    char err[300] = "";
    game_t *g = game_create(&ld, err, sizeof err);
    ASSERT_TRUE(g == NULL);
    ASSERT_TRUE(strstr(err, "segment table") != NULL);
    build_all();
    char err2[300] = "";
    g = game_create(&LD, err2, sizeof err2);   /* no flight tables at all: the milestone-2 static aircraft */
    ASSERT_TRUE(g != NULL);
    game_destroy(g);
}

int main(void) {
    printf("test_world:\n");
    RUN(loads_models_tables_palette_and_lod);
    RUN(every_loader_buffer_is_released);
    RUN(the_synthetic_sound_table_loads_with_absent_and_synthesised_objects);
    RUN(bsp_two_node_tree_picks_child_by_viewer_side);
    RUN(three_vertex_node_uses_the_cross_product_dot);
    RUN(gating_hides_items_whose_gate_is_unsatisfied);
    RUN(one_sided_polygon_facing_back_colour_and_abandon);
    RUN(stream_end_by_0xffff_and_the_type_b_abort_are_reported);
    RUN(first_vertex_follows_the_precision_exponent_of_the_unscaled_distance_word);
    RUN(a_point_east_of_a_camera_facing_north_lands_right_of_the_centre);
    RUN(a_rejected_point_abandons_its_program_and_the_hand_over_to_the_next_is_by_the_or);
    RUN(the_filler_places_the_small_shape_sprites_at_the_right_column_of_the_box);
    RUN(op_48_is_not_clipped_and_op_74_draws_only_above_240_ft);
    RUN(op_26_abandons_in_the_common_drawer_and_counts_in_the_type_b_drawer);
    RUN(the_clipped_line_negates_words_and_rejects_and_counts_a_zero_margin_difference);
    RUN(op_71_leaves_the_pen_word_of_its_last_disc_even_when_that_disc_fails_its_projection);
    RUN(each_handler_writes_the_pen_at_the_moment_of_the_table_in_note_03);
    RUN(op_52_and_53_select_the_gear_step_by_the_progress_in_the_record_byte);
    RUN(the_scene_carries_the_specks_of_the_near_ground_pass_between_queue_b_and_the_entities);
    RUN(the_first_object_after_the_pass_finds_the_fill_block_and_a_type_b_polygon_takes_the_alternate_blit);
    RUN(op_9_reads_the_depth_words_of_the_polygon_buffer_left_by_earlier_polygons);
    RUN(the_sea_mesh_pass_writes_every_polygon_into_the_polygon_buffer_in_order);
    RUN(op_3_stores_the_vertices_before_its_exits_and_the_count_between_the_all_behind_and_the_face_tests);
    RUN(the_second_vertex_section_makes_the_slots_after_the_plain_ones);
    RUN(level_selection_by_threshold_and_instance_scale);
    RUN(make_view_applies_the_model_unit_scale);
    RUN(precision_bands_shrink_the_level_distance_beyond_tm_and_tf);
    RUN(partial_model_loads_with_blocked_items_skipped);
    RUN(unresolved_or_undefined_slots_make_an_item_undrawable);
    RUN(selector_skip_list_and_speculative_items_and_blocked_nodes);
    RUN(version_1_files_are_rejected_with_a_version_message);
    RUN(op67_face_visibility_selector_skips_by_side);
    RUN(unit_shift_and_unit_tables_are_checked);
    RUN(corrupt_files_are_rejected_and_buffers_released);
    RUN(missing_model_file_is_an_error_naming_the_file);
    RUN(missing_terrain_or_table_files_are_errors);
    RUN(cell_lookup_is_bounds_checked);
    RUN(builder_emits_tiles_and_sub_cells_in_pattern_order_with_the_note_formulas);
    RUN(a_carrier_outside_the_scanned_tiles_goes_to_the_direct_list_and_the_scene_draws_it_first);
    RUN(the_vertex_loop_stores_b8_plus_1_slots_the_last_the_data_triple_when_b8_equals_b9);
    RUN(the_vertex_loop_stores_b8_slots_when_b8_exceeds_b9_and_no_data_triple);
    RUN(a_secondary_object_is_walked_by_its_descriptor_and_drawn_with_the_model_of_its_id_word_plus_16);
    RUN(op_70_skips_by_the_life_word_a_negative_word_draws_nothing_and_the_runs_start_where_the_table_says);
    RUN(the_cone_handlers_start_the_detail_list_by_the_phase_and_the_parachutes_phase_0_reads_the_low_nibble_of_the_gear_byte);
    RUN(tile_cells_wrap_at_the_edge_of_the_grid_and_labels_do_not);
    RUN(scene_through_game_create_input_step_scene);
    RUN(aircraft_starts_at_the_carrier_block_and_the_cockpit_camera_is_the_aircraft);
    RUN(hud_is_zero_until_a_flight_module_writes_it);
    RUN(view_events_switch_the_view_kind_and_the_head);
    RUN(chase_camera_sits_behind_the_aircraft_by_the_ring_offset_matrix_shifted_right_4);
    RUN(the_level_scales_each_ring_word_and_the_height_clamp_is_deck_plus_7_times_256);
    RUN(the_ring_turns_by_4_when_the_heading_jumps_by_7200_to_21600);
    RUN(object_attitude_matrix_and_eye_follow_note_03);
    RUN(scene_carries_the_sky_block_with_the_scaled_matrix_and_the_side_flag);
    RUN(chase_camera_through_the_game);
    RUN(the_map_view_hangs_over_the_aircraft_at_the_saved_altitude_and_m_returns_to_the_cockpit);
    RUN(pause_freezes_the_world_and_resumes);
    RUN(freecam_still_works_when_enabled);
    RUN(the_debug_flight_start_without_world_data_does_nothing);
    RUN(game_create_fails_naming_a_missing_model_file);
    RUN(game_create_reports_refused_flight_tables);
    TEST_MAIN_END();
}
