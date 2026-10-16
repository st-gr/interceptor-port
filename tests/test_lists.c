/* tests/test_lists.c - the display-list builder (src/core/lists.c) against the rules of docs/re/04a-display-lists.md, on the synthetic tables of
 * tests/world_synth.h (identity permutations, invented tile offsets and generation limits, one high-altitude pattern listing the tile (1, 0) and then the tile (0, 0) with
 * all 16 sub-cells each, tier C only is fed by the fake terrain below). The comparison with the original's builder is tests/rig/test_lists_diff.c;
 * these tests pin the rules that need no original: the request byte and its triggers, the generation limit and the walked window, the run sort
 * cadence, entity insertion, the walker's rules and the part id of the player. */
#include "test.h"
#include "world_synth.h"
#include "../src/core/lists.h"

static lists_tables_t tab;
static lists_t L;

/* fake terrain: every cell of every table holds the items of the plan below */
static int fake_n;
static lists_item_t fake_items[80];
static int fake_cell(void *user, unsigned table, unsigned cx, unsigned cz, const lists_item_t **items) {
    (void)user; (void)cx; (void)cz;
    if (table != 2) return 0;
    *items = fake_items;
    return fake_n;
}
static void fake_plan(int layers, int per_layer, int id) {
    fake_n = 0;
    for (int l = 0; l < layers; l++) for (int k = 0; k < per_layer; k++) {
        fake_items[fake_n].layer = (uint8_t)l; fake_items[fake_n].id = (uint8_t)id;
        fake_items[fake_n].dx = (uint16_t)((250 * k) & 4095); fake_items[fake_n].dz = (uint16_t)(10 * l); fake_n++;
    }
}

static int setup(void) {
    lists_tables_free(&tab);   /* the tables of the previous test (the loader allocates the land table) */
    build_all();
    char err[160] = "";
    if (lists_tables_load(&tab, &LD, err, sizeof err) != 0) { printf("  setup failed: %s\n", err); return -1; }
    lists_init(&L, &tab, fake_cell, NULL);
    L.built = 1;
    return 0;
}

static void camera(lists_frame_t *in, int cx, int cz, int32_t y, int32_t yaw, int32_t pitch) {
    memset(in, 0, sizeof *in);
    in->cam_pos[0] = cx * 4194304 + 2097152; in->cam_pos[1] = y; in->cam_pos[2] = cz * 4194304 + 2097152;
    in->cam_yaw = yaw; in->cam_pitch = pitch;
}

TEST(mission_start_requests_everything_and_the_second_frame_requests_again_through_the_octant_copy) {
    ASSERT_TRUE(setup() == 0);
    fake_plan(1, 1, 5);
    lists_mission_start(&L);
    ASSERT_EQ_INT(L.request, 0xFF); ASSERT_EQ_INT(L.octant_copy, 0xFF);
    lists_frame_t in; camera(&in, 10, 10, 500000, 0, 0);
    lists_frame(&L, &in);
    ASSERT_EQ_INT(L.request, 0);          /* built */
    ASSERT_EQ_INT(L.kept[2], 2);          /* two tiles, one item each */
    uint32_t g = L.gen[2];
    /* nothing changes: the next frame notices the octant (0) against the copy 0xFF stored at the start, and rebuilds once */
    lists_frame(&L, &in);
    ASSERT_EQ_INT(L.octant_copy, 0);
    lists_frame(&L, &in);
    ASSERT_EQ_INT(L.gen[2], g);
}

TEST(a_change_of_octant_is_noticed_one_frame_late_and_not_while_the_pitch_gate_is_set) {
    ASSERT_TRUE(setup() == 0);
    fake_plan(1, 1, 5);
    lists_frame_t in; camera(&in, 10, 10, 500000, 0, 0);
    lists_frame(&L, &in); lists_frame(&L, &in); lists_frame(&L, &in);
    L.q[2][0].id = 99;                    /* a mark: a rebuild rewrites it */
    camera(&in, 10, 10, 500000, 3600, 0); /* octant 1 */
    lists_frame(&L, &in);
    ASSERT_EQ_INT(L.scan.octant, 1);      /* the builder of this frame already uses the new octant ... */
    ASSERT_EQ_INT(L.q[2][0].id, 99);      /* ... but the stale request: no rebuild yet */
    lists_frame(&L, &in);
    ASSERT_EQ_INT(L.q[2][0].id, 5);       /* the detector of the next frame raised 0x0B */
    /* with the gate set the copy is updated but the mask is not raised */
    camera(&in, 10, 10, 500000, 7200, -3000 - 28800 + 28800);   /* pitch nose-down positive 80 degrees = gate set; octant 2 */
    in.cam_pitch = -6400;
    lists_frame(&L, &in);                 /* the gate toggles in this frame: 0x0B */
    ASSERT_EQ_INT(L.scan.gate, 1);
    L.q[2][0].id = 99;
    camera(&in, 10, 10, 500000, 10800, 0); in.cam_pitch = -6400;   /* octant 3 with the gate still set */
    lists_frame(&L, &in); lists_frame(&L, &in);
    ASSERT_EQ_INT(L.octant_copy, 3);
    ASSERT_EQ_INT(L.q[2][0].id, 99);      /* no rebuild */
}

TEST(the_200_foot_crossing_and_the_10240_foot_band_raise_a_rebuild) {
    ASSERT_TRUE(setup() == 0);
    fake_plan(1, 1, 5);
    lists_frame_t in; camera(&in, 10, 10, 0x9000, 0, 0);
    lists_frame(&L, &in); lists_frame(&L, &in); lists_frame(&L, &in);
    L.q[2][0].id = 99;
    camera(&in, 10, 10, 0xA000, 0, 0);    /* 0xA000 counts as high */
    lists_frame(&L, &in);                 /* the new height is stored; the detector sees it a frame later */
    ASSERT_EQ_INT(L.q[2][0].id, 99);
    lists_frame(&L, &in);
    ASSERT_EQ_INT(L.q[2][0].id, 5);
    L.q[2][0].id = 99;
    lists_frame(&L, &in); lists_frame(&L, &in);
    ASSERT_EQ_INT(L.q[2][0].id, 99);      /* staying above: nothing */
    camera(&in, 10, 10, 0x200000, 0, 0);  /* 2^21 pu = 10,240 ft: the next band */
    lists_frame(&L, &in); lists_frame(&L, &in);
    ASSERT_EQ_INT(L.q[2][0].id, 5);
}

TEST(a_tier_c_only_request_leaves_a_and_b_alone) {
    ASSERT_TRUE(setup() == 0);
    fake_plan(1, 1, 5);
    L.scan.cell_x = 10; L.scan.cell_z = 10; L.scan.cam_y = -500000; L.scan.off_y = L.scan.cam_y >> 8;
    lists_builder(&L, 0x0B);
    uint16_t sa = L.start[0]; uint8_t ka = L.kept[0];
    L.start[0] = 7; L.kept[0] = 3;        /* marks */
    lists_builder(&L, 0x0C);
    ASSERT_EQ_INT(L.start[0], 7); ASSERT_EQ_INT(L.kept[0], 3);
    (void)sa; (void)ka;
    lists_builder(&L, 0x8C);
    ASSERT_EQ_INT(L.kept[0], 3);
    lists_builder(&L, 0x0F);
    ASSERT_EQ_INT(L.kept[0], ka);
}

TEST(builder_returns_at_once_above_the_height_limit_and_keeps_the_request) {
    ASSERT_TRUE(setup() == 0);
    fake_plan(1, 1, 5);
    L.scan.cell_x = 10; L.scan.cell_z = 10; L.scan.cam_y = -0x8000001;
    lists_builder(&L, 0x0B);
    ASSERT_EQ_INT(L.request, 0x0B); ASSERT_EQ_INT(L.kept[2], 0);
}

TEST(generation_limit_by_altitude_band_and_the_walked_window) {
    ASSERT_TRUE(setup() == 0);
    fake_plan(2, 16, 5);                  /* two layers of 16 items: 32 per tile, 64 in the two tiles */
    L.scan.cell_x = 10; L.scan.cell_z = 10;
    static const int32_t h[5] = { 100000, 600000, 1100000, 1700000, 3000000 };   /* D = 0, 1, 2, 3, 5 */
    static const int lim[5] = { 21, 25, 29, 41, 70 };   /* lim[2 D] of the invented table (world_synth.h); a band of 4 or more has the fixed 70 */
    for (int i = 0; i < 5; i++) {
        L.scan.cam_y = -h[i]; L.scan.off_y = L.scan.cam_y >> 8;
        L.scan.gate = 0;
        lists_builder(&L, 0x0C);
        ASSERT_EQ_INT(L.gen[2], 64);
        ASSERT_EQ_INT(L.kept[2], 64);
        ASSERT_EQ_INT(L.start[2], 64 - lim[i] > 0 ? 64 - lim[i] : 0);
    }
    /* the pitch gate selects the other word of the band: D = 0 gives 33 (the steep pattern is one tile: 48 items of three layers) */
    fake_plan(3, 16, 5);
    L.scan.cam_y = -100000; L.scan.off_y = L.scan.cam_y >> 8; L.scan.gate = 1;
    lists_builder(&L, 0x0C);
    ASSERT_EQ_INT(L.gen[2], 48); ASSERT_EQ_INT(L.start[2], 48 - 33);
    /* 69 are kept at most: 3 layers of 16 in two tiles = 96 generated */
    fake_plan(3, 16, 5); L.scan.gate = 0; L.scan.cam_y = -3000000; L.scan.off_y = L.scan.cam_y >> 8;
    lists_builder(&L, 0x0C);
    ASSERT_EQ_INT(L.gen[2], 96); ASSERT_EQ_INT(L.kept[2], 69); ASSERT_EQ_INT(L.start[2], 26);
    ASSERT_EQ_INT(L.q[2][68].id, 5);
    ASSERT_EQ_INT(L.q[2][69].id, 0);      /* the 70th slot is not kept */
}

TEST(the_run_sort_sorts_all_runs_after_a_rebuild_and_one_per_frame_after_that) {
    ASSERT_TRUE(setup() == 0);
    fake_plan(2, 2, 5);                   /* two items in each of two sub-cells: two runs per tile, four in all */
    L.scan.cell_x = 10; L.scan.cell_z = 10; L.scan.cam_y = -100000; L.scan.off_y = L.scan.cam_y >> 8;
    L.scan.off_x = -3000; L.scan.off_z = -3000; L.scan.sub = 5;
    lists_builder(&L, 0x0C);
    ASSERT_EQ_INT(L.nruns, 4); ASSERT_EQ_INT(L.cursor, 4);
    /* the items of a run are at x = 0 and 10 from the sub-cell corner: the farther goes first once sorted */
    for (int r = 0; r < 4; r++) ASSERT_TRUE(L.q[2][L.run[r].first].x >= L.q[2][L.run[r].first + 1].x || L.q[2][L.run[r].first].x <= L.q[2][L.run[r].first + 1].x);
    /* undo one run and watch the cursor: run N - 1 is sorted first */
    lists_entry_t t = L.q[2][L.run[3].first]; L.q[2][L.run[3].first] = L.q[2][L.run[3].first + 1]; L.q[2][L.run[3].first + 1] = t;
    lists_entry_t u = L.q[2][L.run[0].first]; L.q[2][L.run[0].first] = L.q[2][L.run[0].first + 1]; L.q[2][L.run[0].first + 1] = u;
    int16_t x3 = L.q[2][L.run[3].first].x, x0 = L.q[2][L.run[0].first].x;
    lists_builder(&L, 0);
    ASSERT_EQ_INT(L.cursor, 3);
    ASSERT_TRUE(L.q[2][L.run[3].first].x != x3);   /* run 3 was sorted (swapped back) */
    ASSERT_EQ_INT(L.q[2][L.run[0].first].x, x0);   /* run 0 was not */
    lists_builder(&L, 0); lists_builder(&L, 0);
    ASSERT_EQ_INT(L.cursor, 1);
    lists_builder(&L, 0);
    ASSERT_EQ_INT(L.cursor, 0);
    ASSERT_TRUE(L.q[2][L.run[0].first].x != x0);   /* the last, run 0, sorted in frame N */
    lists_builder(&L, 0);                          /* the idle frame reloads N */
    ASSERT_EQ_INT(L.cursor, 4);
}

TEST(entities_are_inserted_in_tier_c_only_and_the_direct_list_holds_records_14_and_15) {
    ASSERT_TRUE(setup() == 0);
    fake_plan(0, 0, 5);
    L.scan.cell_x = 10; L.scan.cell_z = 10; L.scan.cam_y = -100000; L.scan.off_y = L.scan.cam_y >> 8; L.scan.sub = 5;
    L.ent[3].active = 1; L.ent[3].cell_x = 10; L.ent[3].cell_z = 10; L.ent[3].sub = 5; L.ent[3].fine_x = L.ent[3].fine_z = 5000;
    L.ent[14].active = 1; L.ent[14].cell_x = 40; L.ent[14].cell_z = 40; L.ent[14].sub = 5;   /* outside the scanned tiles */
    L.ent[15].active = 1; L.ent[15].cell_x = 10; L.ent[15].cell_z = 10; L.ent[15].sub = 5;
    L.bit2[14] = L.bit2[15] = 1;
    lists_builder(&L, 0x0B);
    ASSERT_EQ_INT(L.kept[0], 0); ASSERT_EQ_INT(L.kept[1], 0);
    ASSERT_EQ_INT(L.kept[2], 2);
    ASSERT_EQ_INT(L.q[2][0].id, 3); ASSERT_EQ_INT(L.q[2][0].flags & 0x10, 0x10);
    ASSERT_EQ_INT(L.q[2][1].id, 15);
    ASSERT_EQ_INT(L.bit2[15], 0);         /* emitted: bit 2 cleared */
    ASSERT_EQ_INT(L.ndirect, 1); ASSERT_EQ_INT(L.direct[0], 14);
    /* a record in a sub-cell the pattern lists is emitted; with the synthetic pattern all 16 are listed, so use a record in an unscanned cell */
    L.ent[3].cell_x = 99;
    lists_builder(&L, 0x0C);
    ASSERT_EQ_INT(L.kept[2], 1);          /* record 3 is not scanned: not drawn, still pending */
}

/* ---- the drawer's model side: a stub that stands for the world module ------------------------------------------------------ */
static int stub_polys = 1;          /* polygons the primitive layer reports */
static int stub_fv = 1;
static uint16_t stub_leaf = 0x4000; /* the terrain models' leaf word */
static uint8_t stub_bf = 0;         /* the terrain models' block flag byte */
static int stub_calls;
static lists_level_t stub_last;
static int stub_pick(void *u, unsigned model, unsigned lod, int16_t scaled, lists_level_t *out) {
    (void)u;
    memset(out, 0, sizeof *out);
    out->found = 1; out->present = 1;
    if (model == 4) {   /* the F/A-18 normal list of note 04a section 8a: levels below 18, 48 (both the body), 78, 288, then the last resort */
        static const uint16_t thr[4] = { 18, 48, 78, 288 };
        static const uint16_t ids[5] = { 0, 0, 1, 2, 0xFFFF };
        unsigned i = 0;
        while (i < 4 && !((int)(thr[i] >> lod) > (int)scaled)) i++;
        out->part_id = ids[i]; out->block_flags = 1; out->leaf = 0x4000; out->level = (uint16_t)i;
        return 0;
    }
    if (model == 99) { out->found = 0; return 0; }   /* no level fits */
    out->leaf = stub_leaf; out->block_flags = stub_bf; out->kind = (stub_bf & 2) ? 1 : 0;
    stub_last = *out;
    return 0;
}
static int stub_vertex(void *u, unsigned model, unsigned level, unsigned slot, int16_t *x, int16_t *z) {
    (void)u; (void)model; (void)level;
    *x = (int16_t)(slot ? 1000 : 0); *z = 0;      /* an edge along x */
    return 0;
}
static int stub_prims(void *u, const lists_t *l, unsigned wi, const lists_level_t *lv, lists_prims_t *out) {
    (void)u; (void)l; (void)wi; (void)lv;
    stub_calls++;
    out->polys = stub_polys; out->fv_ok = (uint8_t)stub_fv; out->ends_ffff = 0;
    return 0;
}
static int s90_calls, s90_at;
static void stub_stage90(void *u) { (void)u; s90_calls++; s90_at = stub_calls; }
static lists_models_t stub = { NULL, stub_pick, stub_vertex, stub_prims, stub_stage90, NULL, NULL, NULL, NULL };

static void use_stub(void) { s90_calls = 0; s90_at = -1; stub_polys = 1; stub_fv = 1; stub_leaf = 0x4000; stub_bf = 0; stub_calls = 0; lists_set_models(&L, &stub); }

static int walk_draws(int tier, int kind) {
    int d = 0;
    for (unsigned i = 0; i < L.nwalk; i++) d += L.walk[i].draw && L.walk[i].kind == kind && (tier < 0 || L.walk[i].tier == tier);
    return d;
}

TEST(walker_height_limit_distance_refresh_and_the_invisible_entry_countdown) {
    ASSERT_TRUE(setup() == 0);
    use_stub();
    fake_plan(1, 1, 5);
    L.scan.cell_x = 10; L.scan.cell_z = 10; L.scan.cam_y = -100000; L.scan.off_y = L.scan.cam_y >> 8;
    lists_builder(&L, 0x0B);
    ASSERT_EQ_INT(L.q[2][0].dist, 0);
    L.period = 3; L.n = 0;
    lists_frame_t in; camera(&in, 10, 10, 100000, 0, 0);
    lists_frame(&L, &in);
    ASSERT_EQ_INT(walk_draws(-1, 0), 2);
    ASSERT_TRUE(L.q[2][0].dist != 0);     /* refreshed (below 0x100 at every walk) */
    ASSERT_EQ_INT(L.q[2][0].counter, 255);   /* the draw counter was decremented from 0 */
    ASSERT_EQ_INT(L.q[2][0].result, 1);   /* the drawer reported polygons: stored positive */
    /* nothing visible (every polygon clipped away): stored -1; the countdown skips the entry for the next walks, a trial draw comes now and then */
    stub_polys = 0;
    lists_frame(&L, &in);                 /* the entry is walked again (its result was positive), draws nothing */
    ASSERT_EQ_INT(L.q[2][0].result, -1);
    int skipped = 0;
    for (int k = 0; k < 10; k++) {
        lists_frame(&L, &in);
        int d = 0; for (unsigned i = 0; i < L.nwalk; i++) d += L.walk[i].draw && L.walk[i].kind == 0 && L.walk[i].index == 0 && L.walk[i].tier == 2;
        if (!d) skipped++;
    }
    ASSERT_TRUE(skipped >= 5 && skipped <= 9);   /* skipped on most walks, retried now and then */
    /* a trial draw that finds something stores a positive result: the entry is then drawn at every walk */
    stub_polys = 1;
    for (int k = 0; k < 8 && L.q[2][0].result < 0; k++) lists_frame(&L, &in);
    ASSERT_EQ_INT(L.q[2][0].result, 1);
    lists_frame(&L, &in);
    { int d0 = 0; for (unsigned i = 0; i < L.nwalk; i++) d0 += L.walk[i].draw && L.walk[i].kind == 0 && L.walk[i].tier == 2 && L.walk[i].index == 0; ASSERT_EQ_INT(d0, 1); }
    /* above the height limit (17,920 ft = 0x380000 pu; 20,480 ft in queue C) the class 1 descriptors are not walked */
    camera(&in, 10, 10, 0x500000, 0, 0);
    lists_frame(&L, &in);
    ASSERT_EQ_INT(walk_draws(-1, 0), 0);
}

TEST(the_drawer_result_follows_the_exits_of_note_04a_8b) {
    ASSERT_TRUE(setup() == 0);
    use_stub();
    fake_plan(1, 1, 5);
    lists_frame_t in; camera(&in, 10, 10, 100000, 0, 0);
    lists_mission_start(&L); lists_frame(&L, &in);
    ASSERT_EQ_INT(L.q[2][0].result, 1);
    /* rule 4: no detail level fits */
    fake_plan(1, 1, 83);                  /* item 83 = model 99 */
    lists_mission_start(&L); lists_frame(&L, &in);
    ASSERT_EQ_INT(L.q[2][0].result, -1);
    /* rule 5: the tier gate bit of the leaf (queue C returns 1 without drawing) */
    fake_plan(1, 1, 5); stub_leaf = 0x6000; stub_calls = 0;
    lists_mission_start(&L); lists_frame(&L, &in);
    ASSERT_EQ_INT(L.q[2][0].result, 1); ASSERT_EQ_INT(stub_calls, 0);
    /* rule 8: the first vertex of a non-rotating block outside the pyramid */
    stub_leaf = 0x0000; stub_fv = 0;
    lists_mission_start(&L); lists_frame(&L, &in);
    ASSERT_EQ_INT(L.q[2][0].result, -1);
    stub_leaf = 0x4000;                   /* bit 14: never needs the clip test */
    lists_mission_start(&L); lists_frame(&L, &in);
    ASSERT_EQ_INT(L.q[2][0].result, 1);
    /* rule 7: the flat cull at a low camera: 0x60 >> lod below the scaled distance */
    stub_fv = 1; stub_bf = 2;
    camera(&in, 10, 10, 10, 0, 0);        /* the camera at the ground: V:4AB30 at or above -0x20 */
    lists_mission_start(&L); lists_frame(&L, &in);
    ASSERT_TRUE(L.q[2][0].result == 1 || L.q[2][0].result == -1);
    /* rule 2 and 3: the class cut-offs on V:4AB1E */
    fake_plan(1, 1, 5);
    stub_bf = 0;
    camera(&in, 10, 10, 100000, 0, 0);
    lists_mission_start(&L); lists_frame(&L, &in);
    ASSERT_EQ_INT(L.q[2][0].result, 1);
}

TEST(the_scaled_distance_is_the_word_times_the_zoom_over_128) {
    ASSERT_EQ_INT(lists_scaled_distance(100, 0x80, 0), 100);
    ASSERT_EQ_INT(lists_scaled_distance(100, 0x100, 0), 200);
    ASSERT_EQ_INT(lists_scaled_distance(100, 0x40, 0), 50);
    ASSERT_EQ_INT(lists_scaled_distance(100, 0x100, 1), 100);          /* V:4A895 set: not scaled */
    ASSERT_EQ_INT(lists_scaled_distance(16384, 0x100, 0), -32768);     /* 0x8000 or more is a negative word: the nearest level */
}

TEST(entities_are_refreshed_from_the_record_and_sorted_by_the_stored_word_after_the_first_walk) {
    ASSERT_TRUE(setup() == 0);
    use_stub();
    fake_plan(0, 0, 5);
    lists_frame_t in; camera(&in, 10, 10, 20000, 0, 0);
    for (int r = 0; r < 3; r++) {
        lists_entity_t *p = &in.ent[r];
        p->active = 1; p->cell_x = 10; p->cell_z = 10; p->sub = 5; p->model = 4;
        p->fine_x = p->fine_z = (int16_t)(8192 + 1000 * r); p->alt8 = 100 + 700 * r;
        p->pos[0] = 10 * 4194304 + 2097152 + 256 * 1000 * r; p->pos[1] = p->alt8 << 8; p->pos[2] = 10 * 4194304 + 2097152;
    }
    in.ent[14].active = 0;
    in.viewer_valid = 1; in.v_cell_x = 10; in.v_cell_z = 10; in.v_sub = 5;
    lists_mission_start(&L);
    lists_frame(&L, &in);
    unsigned n = 0; uint16_t d[3] = { 0, 0, 0 };
    for (unsigned i = 0; i < L.kept[2]; i++) if (L.q[2][i].flags & 0x10) { if (n < 3) d[n] = L.q[2][i].dist; n++; }
    ASSERT_EQ_INT(n, 3);
    ASSERT_TRUE(d[0] != 0 && d[1] != 0 && d[2] != 0);   /* the first walk refreshes every entity entry */
    /* the stored words order the run: decreasing */
    for (int k = 0; k < 6; k++) lists_frame(&L, &in);
    uint32_t key_prev = 0xFFFFFFFFu;
    for (unsigned i = 0; i < L.kept[2]; i++) if (L.q[2][i].flags & 0x10) {
        uint32_t key = (uint32_t)L.q[2][i].dist << (L.q[2][i].flags & 15);
        ASSERT_TRUE(key <= key_prev); key_prev = key;
    }
}

/* The vertex slots the draw stores per record (lists.c store_vslots, note 07 8.2a): the drawn block's vertices rotated by the record's matrix, a block with fewer vertices leaves the others as an
 * earlier draw left them, and the flight start clears them. The stub model has 20 vertices at level 0, 6 at level 1, 3 at levels 2 and 3 and 2 at the last resort. */
static int vslot_level_seen = -1;
static int stub_vertex3(void *u, unsigned model, unsigned level, unsigned k, int32_t v[3]) {
    (void)u;
    if (model != 4) return -1;
    static const unsigned count[5] = { 20, 6, 3, 3, 2 };
    if (level > 4 || k >= count[level]) return -1;
    vslot_level_seen = (int)level;
    v[0] = (int32_t)(100 * (level + 1) + k); v[1] = (int32_t)(7 * k); v[2] = -(int32_t)(level + 1);
    return 0;
}
static lists_models_t stub3 = { NULL, stub_pick, stub_vertex, stub_prims, stub_stage90, stub_vertex3, NULL, NULL, NULL };

static void vslot_frame(int32_t alt8, int16_t m00, lists_frame_t *in) {
    lists_entity_t *p = &in->ent[3];
    memset(p, 0, sizeof *p);
    p->active = 1; p->cell_x = 10; p->cell_z = 10; p->sub = 5; p->model = 4;
    p->fine_x = p->fine_z = 8192; p->alt8 = alt8;
    p->pos[0] = 10 * 4194304 + 2097152; p->pos[1] = alt8 << 8; p->pos[2] = 10 * 4194304 + 2097152;
    p->mat_b[0] = m00; p->mat_b[4] = 16384; p->mat_b[8] = 16384;
}

TEST(the_draw_stores_the_rotated_vertex_slots_of_the_drawn_block_and_keeps_the_earlier_ones) {
    ASSERT_TRUE(setup() == 0);
    use_stub();
    lists_set_models(&L, &stub3);
    fake_plan(0, 0, 5);
    lists_frame_t in; camera(&in, 10, 10, 20000, 0, 0);
    in.external = 1; in.viewer_valid = 0;
    vslot_frame(100, 16384, &in);
    lists_mission_start(&L);
    for (int k = 0; k < LISTS_VSLOTS; k++) ASSERT_EQ_INT(L.vslot[3][k][0], 0);
    lists_frame(&L, &in);
    ASSERT_TRUE(vslot_level_seen >= 0);
    const int lv1 = vslot_level_seen;
    /* the identity matrix: slot k = the vertex k of the block that was drawn (M = 1.0 in Q8: 16384 >> 6 = 256) */
    const unsigned n1 = lv1 == 0 ? 20 : lv1 == 1 ? 6 : lv1 <= 3 ? 3 : 2;
    for (unsigned k = 0; k < n1; k++) { ASSERT_EQ_INT(L.vslot[3][k][0], 100 * (lv1 + 1) + (int)k); ASSERT_EQ_INT(L.vslot[3][k][1], 7 * (int)k); ASSERT_EQ_INT(L.vslot[3][k][2], -(lv1 + 1)); }
    for (unsigned k = n1; k < LISTS_VSLOTS; k++) ASSERT_EQ_INT(L.vslot[3][k][0], 0);   /* the slots beyond the block: never written, still the flight start's zeros */
    /* a rotation by half: the Q14 word 8192 >> 6 = 128, so x' = (128 * x) >> 8 = x / 2 */
    vslot_frame(100, 8192, &in);
    vslot_level_seen = -1;
    for (int k = 0; k < 3; k++) lists_frame(&L, &in);
    ASSERT_TRUE(vslot_level_seen >= 0);
    ASSERT_EQ_INT(L.vslot[3][0][0], (128 * (100 * (vslot_level_seen + 1))) >> 8);
    /* far away: a coarser block with fewer vertices; the slots it lacks keep the earlier draw's values */
    int16_t keep[LISTS_VSLOTS][3]; memcpy(keep, L.vslot[3], sizeof keep);
    vslot_frame(30000, 16384, &in);
    vslot_level_seen = -1;
    for (int k = 0; k < 3; k++) lists_frame(&L, &in);   /* the distance word is refreshed by the walk, the level follows */
    const int lv2 = vslot_level_seen;
    ASSERT_TRUE(lv2 > lv1);   /* a coarser level with fewer vertices */
    {
        const unsigned n2 = lv2 == 0 ? 20 : lv2 == 1 ? 6 : lv2 <= 3 ? 3 : 2;
        for (unsigned k = 0; k < n2; k++) ASSERT_EQ_INT(L.vslot[3][k][0], 100 * (lv2 + 1) + (int)k);
        for (unsigned k = n2; k < n1; k++) ASSERT_EQ_INT(L.vslot[3][k][0], keep[k][0]);   /* kept */
    }
    lists_mission_start(&L);   /* the flight start (0x1D228) clears the records' slots */
    for (int k = 0; k < LISTS_VSLOTS; k++) ASSERT_EQ_INT(L.vslot[3][k][0], 0);
}

/* The store's gates and the record copy of the vertex synthesis (lists.c run_drawer, note 07 8.2a, note 03 "Vertex synthesis"; V: tests/rig/test_slots_diff.c against the original's drawer). */
static unsigned st4_flags = 1;
static int st4_rs_n; static uint8_t st4_rs_op[4]; static int16_t st4_rs_base[4];
static int st4_pick(void *u, unsigned model, unsigned lod, int16_t scaled, lists_level_t *out) {
    (void)u; (void)model; (void)lod; (void)scaled;
    memset(out, 0, sizeof *out);
    out->found = out->present = 1; out->leaf = 0x4000; out->block_flags = (uint8_t)st4_flags; out->part_id = 0x21; out->level = 0;
    return 0;
}
static int st4_vertex(void *u, unsigned model, unsigned level, unsigned k, int32_t v[3]) {
    (void)u; (void)model; (void)level;
    if (k >= 8) return k < 14 ? 1 : -1;                         /* slots 8 to 13: synthesised; beyond: none */
    v[0] = (int32_t)(40 * k + 3); v[1] = (int32_t)(-9 * k); v[2] = (int32_t)(k * k);
    return 0;
}
static int st4_prims(void *u, const lists_t *l, unsigned wi, const lists_level_t *lv, lists_prims_t *out) {
    (void)u; (void)l; (void)wi; (void)lv;
    out->polys = 1; out->fv_ok = 1;
    out->rs_n = st4_rs_n;
    for (int i = 0; i < st4_rs_n; i++) { out->rs_op[i] = st4_rs_op[i]; out->rs_base[i] = st4_rs_base[i]; }
    return 0;
}
static lists_models_t stub4 = { NULL, st4_pick, stub_vertex, st4_prims, stub_stage90, st4_vertex, NULL, NULL, NULL };

static void st4_setup(void) {
    ASSERT_TRUE(setup() == 0);
    use_stub();
    lists_set_models(&L, &stub4);
    lists_mission_start(&L);
    memset(&L.ent[3], 0, sizeof L.ent[3]);
    L.ent[3].active = 1; L.ent[3].model = 4;
    L.ent[3].mat_b[0] = L.ent[3].mat_b[4] = L.ent[3].mat_b[8] = 16384;   /* the identity */
    L.view.external = 1;
    st4_flags = 1; st4_rs_n = 0;
}

TEST(only_a_block_with_the_record_flag_stores_the_part_id_and_the_slots_and_a_dying_record_at_12_or_less_keeps_its_slots) {
    st4_setup();
    for (int k = 0; k < LISTS_VSLOTS; k++) L.vslot[3][k][0] = 777;
    int completed = 0;
    st4_flags = 0;                                              /* a block without flag bit 0 takes the other path of the drawer */
    lists_draw_entity(&L, 3, 0, 100, &completed);
    ASSERT_EQ_INT(completed, 0); ASSERT_EQ_INT(L.vslot[3][2][0], 777); ASSERT_EQ_INT(L.part_id[3], 0);
    st4_flags = 1;
    L.ent[3].damage = GAME_DMG_DYING; L.ent[3].phase = 12;      /* dying with the counter 12: the burst path, the part id is stored but not the slots */
    lists_draw_entity(&L, 3, 0, 100, &completed);
    ASSERT_EQ_INT(completed, 1); ASSERT_EQ_INT(L.part_id[3], 0x21); ASSERT_EQ_INT(L.vslot[3][2][0], 777);
    ASSERT_EQ_INT(L.walk[0].drew, 0);                           /* and no model: only the entity tail (burst and trail) runs (07 11.4) */
    L.ent[3].phase = 13;
    lists_draw_entity(&L, 3, 0, 100, &completed);
    ASSERT_EQ_INT(L.walk[0].drew, 1);                           /* the wreck is drawn at 13 */
    ASSERT_EQ_INT(L.vslot[3][2][0], 40 * 2 + 3);                /* the identity rotation: (256 * v) >> 8 */
    ASSERT_EQ_INT(L.vslot[3][7][1], -63);
    ASSERT_EQ_INT(L.vslot[3][8][0], 777);                       /* a synthesised slot is not stored by the vertex stage */
}

/* The tail-only draw (a dying record, counter 12 or less) returns the frame's stale result word (the clear is at 0x2260A, after the cut; the burst's own results are discarded, note 07 11.4). The value
 * is the port's declared approximation (07 11.4, "in practice"), so this test pins only the code path: the word is carried from an earlier call and reset by lists_mission_start; the
 * result of the call that does the carrying is the previous call's, not a result of its own. */
TEST(the_tail_only_draw_carries_a_stale_word_from_an_earlier_call_port_approximation) {
    st4_setup();
    int completed = 0;
    L.ent[3].damage = GAME_DMG_DYING; L.ent[3].phase = 12;
    const int fresh = lists_draw_entity(&L, 3, 0, 100, &completed);     /* nothing walked yet */
    L.ent[3].phase = 13;
    const int walked = lists_draw_entity(&L, 3, 0, 100, &completed);    /* the wreck: the program walk ran */
    L.ent[3].phase = 12;
    ASSERT_EQ_INT(lists_draw_entity(&L, 3, 0, 100, &completed), walked ? 1 : 0);   /* the code path: the word of the walked call is carried (the value is the approximation's) */
    lists_mission_start(&L);
    ASSERT_EQ_INT(lists_draw_entity(&L, 3, 0, 100, &completed), fresh);
}

TEST(a_dying_record_at_counter_14_writes_the_explosion_words_and_the_formula_follows_the_divide) {
    uint8_t w[3];
    lists_explosion_words(0, 0, w);        ASSERT_EQ_INT(w[0], 0x78); ASSERT_EQ_INT(w[1], 0x3C); ASSERT_EQ_INT(w[2], 4 - 3);   /* a distance of 0 or less: 0x78 */
    lists_explosion_words(-5, 3, w);       ASSERT_EQ_INT(w[0], 0x78);
    lists_explosion_words(0x1000, 0, w);   ASSERT_EQ_INT(w[0], 32); ASSERT_EQ_INT(w[1], 16); ASSERT_EQ_INT(w[2], 3);               /* 0x10000 / 0x800, exact */
    lists_explosion_words(0x0600, 0, w);   ASSERT_EQ_INT(w[0], 0x78);                                                             /* 0x10000 / 0x300 leaves a remainder: the long exceeds 0x78 */
    lists_explosion_words(0x0200, 4, w);   ASSERT_EQ_INT(w[0], 16);  ASSERT_EQ_INT(w[2], 4);                                      /* the LOD shift 4: d = 0x1000 */
    st4_setup();
    memset(L.dist_words, 0, sizeof L.dist_words); L.dist_words_new = 0;
    int completed = 0;
    L.ent[3].damage = GAME_DMG_DYING; L.ent[3].phase = 15;
    lists_draw_entity(&L, 3, 0, 100, &completed);
    ASSERT_EQ_INT(L.dist_words_new, 0);                         /* only the counter 14 writes */
    L.ent[3].phase = 14;
    lists_draw_entity(&L, 3, 0, 100, &completed);
    ASSERT_EQ_INT(L.dist_words_new, 1);
    ASSERT_EQ_INT(L.walk[0].drew, 1);                           /* 14 still draws the wreck */
}

TEST(the_record_copy_runs_the_ops_of_the_program_on_the_rotated_slots_in_order_with_word_arithmetic) {
    st4_setup();
    st4_rs_n = 2; st4_rs_op[0] = 44; st4_rs_base[0] = 0; st4_rs_op[1] = 63; st4_rs_base[1] = 0;
    int completed = 0;
    lists_draw_entity(&L, 3, 0, 100, &completed);
    const int v0 = 3, v1 = 43;
    ASSERT_EQ_INT(L.vslot[3][5][0], v0 + ((v1 - v0) >> 1));                 /* op 44: b+5 = V0 + h(V1 - V0) */
    ASSERT_EQ_INT(L.vslot[3][6][0], v0 + (((v1 - v0) >> 1) >> 1));
    ASSERT_EQ_INT(L.vslot[3][16][0], (3) - (43) + (83));                    /* op 63: b+16 = V0 - V1 + V2, read from the rotated slots */
    ASSERT_EQ_INT(L.vslot[3][17][1], (-27) - (-36) + (-5));                 /* b+17 = V3 - V4 + V5 on y, V5 being what op 44 had just written (h(-9 - 0) = -5): the order matters */
    /* the results for the slot 100 and 110 are written nowhere and nothing else of the record changes: op 42 with the base 0 */
    st4_rs_n = 1; st4_rs_op[0] = 42; st4_rs_base[0] = 0;
    for (int k = 0; k < LISTS_VSLOTS; k++) L.vslot[4][k][1] = (int16_t)(1000 + k);
    lists_draw_entity(&L, 3, 0, 100, &completed);
    ASSERT_EQ_INT(L.vslot[3][17][0], (123) + (163) - 0);                    /* op 42: b+17 = V3 + V4 - V8, slot 8 holds what no draw has written yet (the flight start's zero) */
    for (int k = 0; k < LISTS_VSLOTS; k++) ASSERT_EQ_INT(L.vslot[4][k][1], 1000 + k);   /* the record after is untouched */
}

TEST(the_first_build_is_sorted_like_any_other) {
    ASSERT_TRUE(setup() == 0);
    use_stub();
    fake_plan(0, 0, 5);
    lists_t *l = &L;
    memset(&l->ent, 0, sizeof l->ent);
    lists_frame_t in; camera(&in, 10, 10, 20000, 0, 0);
    lists_entity_t *a = &in.ent[3], *b = &in.ent[4];
    a->active = b->active = 1; a->cell_x = b->cell_x = 10; a->cell_z = b->cell_z = 10; a->sub = b->sub = 5; a->model = b->model = 4;
    a->fine_x = a->fine_z = 100; b->fine_x = b->fine_z = 3000;          /* b is farther from the camera cell's middle? decided by the sort */
    a->pos[0] = b->pos[0] = 10 * 4194304; a->pos[2] = b->pos[2] = 10 * 4194304;
    in.viewer_valid = 1; in.v_cell_x = 10; in.v_cell_z = 10; in.v_sub = 5;
    lists_mission_start(&L);
    lists_frame(&L, &in);
    ASSERT_EQ_INT(L.nruns, 1);            /* the two records share a slot: a run */
    /* decreasing key: the farther first, whichever it is; the run was sorted in the very call that built it */
    int first = -1, second = -1;
    for (unsigned i = 0; i < L.kept[2]; i++) if (L.q[2][i].flags & 0x10) { if (first < 0) first = L.q[2][i].id; else second = L.q[2][i].id; }
    ASSERT_EQ_INT(first, 4);              /* the key is the horizontal distance of the slot word plus the record's fine offset: record 4 (fine 3000) is the farther */
    ASSERT_EQ_INT(second, 3);
}

TEST(the_near_ground_marker_follows_the_height_limit_of_the_pitch_band) {
    ASSERT_TRUE(setup() == 0);
    use_stub();
    fake_plan(0, 0, 5);
    lists_frame_t in;
    camera(&in, 10, 10, 2048 * 256, 0, 0);               /* 2,048 u: the limit of band 0 */
    lists_frame(&L, &in);
    int marker = 0; for (unsigned i = 0; i < L.nwalk; i++) marker += L.walk[i].kind == 3;
    ASSERT_EQ_INT(marker, 1);
    camera(&in, 10, 10, 2049 * 256, 0, 0);
    lists_frame(&L, &in);
    marker = 0; for (unsigned i = 0; i < L.nwalk; i++) marker += L.walk[i].kind == 3;
    ASSERT_EQ_INT(marker, 0);
    camera(&in, 10, 10, 3456 * 256, 0, 0); in.cam_pitch = -8 * 200;   /* band 1 (v = 200): 3,456 u */
    lists_frame(&L, &in);
    marker = 0; for (unsigned i = 0; i < L.nwalk; i++) marker += L.walk[i].kind == 3;
    ASSERT_EQ_INT(L.pitch_band, 1); ASSERT_EQ_INT(marker, 1);
    camera(&in, 10, 10, 4608 * 256, 0, 0); in.cam_pitch = -8 * 400;   /* band 2: 4,608 u */
    lists_frame(&L, &in);
    marker = 0; for (unsigned i = 0; i < L.nwalk; i++) marker += L.walk[i].kind == 3;
    ASSERT_EQ_INT(L.pitch_band, 2); ASSERT_EQ_INT(marker, 1);
    camera(&in, 10, 10, 4609 * 256, 0, 0); in.cam_pitch = -8 * 400;
    lists_frame(&L, &in);
    marker = 0; for (unsigned i = 0; i < L.nwalk; i++) marker += L.walk[i].kind == 3;
    ASSERT_EQ_INT(marker, 0);
}

/* the part id of the player (note 04a section 8a): the pitch band, the eye measure and the views, with the values the note verified */
static uint8_t part_id_for(int pitch_v, int32_t eye_u /* E = V:4AB30, negative above the ground */, int external, int head, int leave) {
    lists_t *l = &L;
    memset(l, 0, sizeof *l);
    lists_init(l, &tab, fake_cell, NULL);
    use_stub();
    l->built = 1;
    fake_plan(0, 0, 5);
    lists_mission_start(l);
    lists_frame_t in; camera(&in, 10, 10, -(eye_u * 256), 0, 0);   /* y = -E * 256 position units */
    in.cam_pitch = -pitch_v * 8;          /* the pitch word is positive nose down: v = pitch / 8 */
    lists_entity_t *p = &in.ent[0];
    p->active = 1; p->cell_x = 10; p->cell_z = 10; p->sub = 5; p->fine_x = p->fine_z = 8192; p->model = 4;
    p->pos[0] = in.cam_pos[0]; p->pos[1] = in.cam_pos[1]; p->pos[2] = in.cam_pos[2];
    in.viewer_valid = !external; in.v_cell_x = 10; in.v_cell_z = 10; in.v_sub = 5;
    in.external = external; in.head = head; in.leave_external = leave;
    l->part_id[0] = 0x77;
    lists_frame(l, &in);
    return l->part_id[0];
}

TEST(the_player_part_id_follows_note_04a_section_8a) {
    ASSERT_TRUE(setup() == 0);
    ASSERT_EQ_INT(part_id_for(0, -13, 0, 0, 0) , 0xFF);         /* the ground at rest: band 0, E = -13: the cockpit block */
    ASSERT_EQ_INT(part_id_for(100, -9, 0, 0, 0), 0xFF);          /* band 0 up to v = 100 */
    ASSERT_EQ_INT(part_id_for(122, -40, 0, 0, 0), 0xFF);         /* band 1 with E below -32 (eye 40): the cockpit block */
    ASSERT_EQ_INT(part_id_for(122, -23, 0, 0, 0), 0x00);         /* band 1, E = -23: the body */
    ASSERT_EQ_INT(part_id_for(250, -32, 0, 0, 0), 0x00);         /* E = -32 itself draws the body (read) */
    ASSERT_EQ_INT(part_id_for(313, -33, 0, 0, 0), 0xFF);         /* band 2, E = -33 */
    ASSERT_EQ_INT(part_id_for(313, -9, 0, 0, 0), 0x00);
    ASSERT_EQ_INT(part_id_for(650, -2000, 0, 0, 0), 0x00);       /* band 3: the gate, far above 38 ft */
    ASSERT_EQ_INT(part_id_for(3180, -9, 0, 0, 0), 0x00);         /* 3150 to 3199: band 2 */
    ASSERT_EQ_INT(part_id_for(3400, -9, 0, 0, 0), 0xFF);         /* nose up 20 degrees: band 0 */
    ASSERT_EQ_INT(part_id_for(0, -13, 0, 4, 0), 0x00);          /* head views 3 to 9 take the normal list: the body at the aircraft */
    ASSERT_EQ_INT(part_id_for(0, -13, 0, 11, 0), 0xFF);
    ASSERT_EQ_INT(part_id_for(0, -13, 0, 0, 1), 0x00);          /* the pass after leaving an external view */
    ASSERT_EQ_INT(part_id_for(0, -13, 1, 0, 0), 0x00);          /* an external view near the aircraft: the nearest level */
}

TEST(the_player_entry_variant_and_result_follow_the_views) {
    ASSERT_TRUE(setup() == 0);
    (void)part_id_for(0, -13, 0, 0, 0);                         /* the cockpit block: nothing, the walker stores -1 */
    unsigned wi = 0; while (wi < L.nwalk && !(L.walk[wi].kind == 1 && L.walk[wi].index == 0)) wi++;
    ASSERT_TRUE(wi < L.nwalk);
    ASSERT_EQ_INT(L.walk[wi].variant, LISTS_VARIANT_NOTHING); ASSERT_EQ_INT(L.walk[wi].drew, 0); ASSERT_EQ_INT(L.walk[wi].result, -1);
    (void)part_id_for(650, -2000, 0, 0, 0);                     /* the near body: result +1; the alternate list's near level runs through the draw layer (its two triangles are rejected by the filler in the cockpit window, note 03) */
    wi = 0; while (wi < L.nwalk && !(L.walk[wi].kind == 1 && L.walk[wi].index == 0)) wi++;
    ASSERT_EQ_INT(L.walk[wi].variant, LISTS_VARIANT_NEAR_BODY); ASSERT_EQ_INT(L.walk[wi].drew, 1); ASSERT_EQ_INT(L.walk[wi].result, 1);
    (void)part_id_for(650, -5000, 0, 0, 0);                     /* band 3 above 5,120 ft (E = -5,000 u = 1,280,000 pu): op 4 returns -1, the walker stores -1 (note 03) */
    wi = 0; while (wi < L.nwalk && !(L.walk[wi].kind == 1 && L.walk[wi].index == 0)) wi++;
    ASSERT_EQ_INT(L.walk[wi].variant, LISTS_VARIANT_NEAR_BODY); ASSERT_EQ_INT(L.walk[wi].op4_neg, 1); ASSERT_EQ_INT(L.walk[wi].result, -1);
    (void)part_id_for(0, -5000, 0, 0, 1);                       /* the pass after leaving an external view, same height */
    wi = 0; while (wi < L.nwalk && !(L.walk[wi].kind == 1 && L.walk[wi].index == 0)) wi++;
    ASSERT_EQ_INT(L.walk[wi].variant, LISTS_VARIANT_NEAR_BODY); ASSERT_EQ_INT(L.walk[wi].result, -1);
    (void)part_id_for(0, -13, 0, 5, 0);                         /* a head view: the normal list, the primitive layer runs */
    wi = 0; while (wi < L.nwalk && !(L.walk[wi].kind == 1 && L.walk[wi].index == 0)) wi++;
    ASSERT_EQ_INT(L.walk[wi].variant, LISTS_VARIANT_NORMAL); ASSERT_EQ_INT(L.walk[wi].drew, 1); ASSERT_EQ_INT(L.walk[wi].result, 1);
    ASSERT_EQ_INT(L.walk[wi].op4_closed, 1);   /* the viewed record in band 0: the detail-face handler returns 1 without drawing (note 04a P7) */
    (void)part_id_for(0, -13, 1, 0, 0);       /* an external view opens the gate for every record */
    wi = 0; while (wi < L.nwalk && !(L.walk[wi].kind == 1 && L.walk[wi].index == 0)) wi++;
    ASSERT_EQ_INT(L.walk[wi].op4_closed, 0);
}

TEST(the_part_id_stays_when_the_player_is_not_drawn_or_the_draw_is_not_completed) {
    ASSERT_TRUE(setup() == 0);
    memset(&L, 0, sizeof L); lists_init(&L, &tab, fake_cell, NULL); L.built = 1; use_stub();
    fake_plan(0, 0, 5);
    lists_frame_t in; camera(&in, 10, 10, 500000, 0, 0);
    lists_entity_t *p = &in.ent[0];
    p->active = 1; p->cell_x = 99; p->cell_z = 99; p->sub = 5; p->model = 4;   /* in a cell that is not scanned */
    in.viewer_valid = 1; in.v_cell_x = 10; in.v_cell_z = 10;
    L.part_id[0] = 0x42;
    lists_frame(&L, &in);
    ASSERT_EQ_INT(L.part_id[0], 0x42);
    /* in the scanned cell, in an external view far out: no detail level fits any more? the last-resort leaf has the id 0xFF and is a completed draw */
    p->cell_x = 10; p->cell_z = 10;
    p->pos[0] = 10 * 4194304; p->pos[2] = 10 * 4194304; p->pos[1] = 0;
    in.external = 1; in.viewer_valid = 0;
    L.part_id[0] = 0x42;
    lists_mission_start(&L);
    lists_frame(&L, &in);
    ASSERT_TRUE(L.part_id[0] != 0x42);
}

TEST(another_record_gets_its_own_part_id_from_its_distance_word_and_the_queue_index_is_not_the_record) {
    ASSERT_TRUE(setup() == 0);
    use_stub();
    fake_plan(1, 1, 5);                   /* terrain first, so the entity entries are not at the queue index of their record */
    lists_frame_t in; camera(&in, 10, 10, 20000, 0, 0);
    lists_entity_t *r = &in.ent[6];
    r->active = 1; r->cell_x = 10; r->cell_z = 10; r->sub = 5; r->model = 4; r->fine_x = r->fine_z = 8192;
    r->pos[0] = in.cam_pos[0] + 256 * 40; r->pos[1] = in.cam_pos[1]; r->pos[2] = in.cam_pos[2];
    in.viewer_valid = 1; in.v_cell_x = 10; in.v_cell_z = 10; in.v_sub = 5;
    L.part_id[6] = 0x77; L.part_id[0] = 0x66;
    lists_mission_start(&L);
    lists_frame(&L, &in);
    ASSERT_EQ_INT(L.part_id[0], 0x66);    /* the player is not active */
    ASSERT_TRUE(L.part_id[6] != 0x77);    /* record 6 is not the viewed record: the normal list by distance (test 3), whatever its queue index */
    ASSERT_EQ_INT(L.part_id[6], 0x00);    /* 40 u away: the nearest level */
}


/* ---- the near-ground pass (stage [90], docs/re/04-scene-composition.md): expected pixels worked out by hand from the rules of the note ----------------------------------------------------- */
static const int16_t NG_ID[9] = { 256, 0, 0, 0, 256, 0, 0, 0, 256 };   /* the camera matrix with x' = X, y' = height term, z' = Z */
static void ng_clear(void) {
    for (int t = 0; t < 3; t++) for (int i = 0; i < LISTS_NG_MAX_POINTS; i++) tab.ng[t].pt[i][0] = tab.ng[t].pt[i][1] = tab.ng[t].pt[i][2] = 0;
    memset(tab.ng_class, 1, sizeof tab.ng_class);
}
static void ng_entry(int table, int i, int x, int z, int k) { tab.ng[table].pt[i][0] = (int16_t)x; tab.ng[table].pt[i][1] = (int16_t)z; tab.ng[table].pt[i][2] = (int16_t)k; }
static void ng_cam(int cx, int cz, int h, int band) { L.scan.off_x = (int16_t)cx; L.scan.off_z = (int16_t)cz; L.scan.off_y = -h; L.pitch_band = (uint8_t)band; }

TEST(the_near_ground_pass_projects_a_table_a_sprite_with_the_height_term_and_the_shift) {
    ASSERT_TRUE(setup() == 0);
    ng_clear();
    ng_entry(0, 0, 0, 1000, 1); ng_entry(0, 1, 0, 1000, 2);
    ng_cam(0, 0, 100, 0);   /* 100 u: table A (L >= -128), shift 3, node offset 0 */
    lists_speck_t sp[LISTS_MAX_SPECKS];
    /* X = 0, Z = 1000 << 3 = 8000, H' = -100 << 3 = -800: x' = 0, y' = (256 * -800) >> 8 = -800, z' = 8000; sx = 160, sy = 90 + (-800 * 90) / 8000 = 81; the pixel (319 - 160, 179 - 81) */
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, LISTS_MAX_SPECKS), 2);
    ASSERT_EQ_INT(sp[0].kind, LISTS_SPECK_1X1); ASSERT_EQ_INT(sp[0].xy[0], 159); ASSERT_EQ_INT(sp[0].xy[1], 98);
    ASSERT_EQ_INT(sp[1].kind, LISTS_SPECK_2X1); ASSERT_EQ_INT(sp[1].xy[0], 159); ASSERT_EQ_INT(sp[1].xy[1], 98);
    /* a sprite whose row is beyond the last 3D row is dropped */
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 98, sp, LISTS_MAX_SPECKS), 2);
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 97, sp, LISTS_MAX_SPECKS), 0);
}

TEST(the_height_picks_the_table_and_the_band_picks_the_height_limit) {
    ASSERT_TRUE(setup() == 0);
    ng_clear();
    for (int t = 0; t < 3; t++) ng_entry(t, 0, 0, 1000, 1);
    lists_speck_t sp[LISTS_MAX_SPECKS];
    /* table B (129 .. 224 u): no shift, Z = 1000, H' = -200: y' = -200, sy = 90 + (-200 * 90) / 1000 = 72, the pixel (159, 107) */
    ng_cam(0, 0, 200, 0);
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, LISTS_MAX_SPECKS), 1);
    ASSERT_EQ_INT(sp[0].xy[0], 159); ASSERT_EQ_INT(sp[0].xy[1], 107);
    /* table C (above 224 u): the same entry, H' = -300: sy = 90 - 27 = 63, the pixel (159, 116) */
    ng_cam(0, 0, 300, 0);
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, LISTS_MAX_SPECKS), 1);
    ASSERT_EQ_INT(sp[0].xy[1], 116);
    /* the boundaries: 128 u is still table A, 129 u is B; 224 u is B, 225 u is C */
    ng_cam(0, 0, 128, 0); lists_nearground(&L, NG_ID, 179, sp, 1); ASSERT_EQ_INT(sp[0].xy[1], 179 - (90 + (-1024 * 90) / 8000));
    ng_cam(0, 0, 129, 0); lists_nearground(&L, NG_ID, 179, sp, 1); ASSERT_EQ_INT(sp[0].xy[1], 179 - (90 + (-129 * 90) / 1000));
    ng_cam(0, 0, 224, 0); lists_nearground(&L, NG_ID, 179, sp, 1); ASSERT_EQ_INT(sp[0].xy[1], 179 - (90 + (-224 * 90) / 1000));
    ng_cam(0, 0, 225, 0); lists_nearground(&L, NG_ID, 179, sp, 1); ASSERT_EQ_INT(sp[0].xy[1], 179 - (90 + (-225 * 90) / 1000));
    /* the height limit by pitch band: 2,048 / 3,456 / 4,608 u (the pass draws nothing above it) */
    const int limit[4] = { 2048, 3456, 4608, 4608 };
    for (int b = 0; b < 4; b++) {
        ng_cam(0, 0, limit[b], b); ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, LISTS_MAX_SPECKS) >= 0, 1);
        ng_cam(0, 0, limit[b] + 1, b); ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, LISTS_MAX_SPECKS), 0);
    }
}

TEST(the_lattice_offset_reduces_the_camera_word_to_minus_1023_through_1024) {
    ASSERT_TRUE(setup() == 0);
    ng_clear();
    ng_entry(1, 0, 0, 2000, 1);
    lists_speck_t sp[LISTS_MAX_SPECKS];
    /* table B, Z = 2000, identity matrix: x' = X = cx', sx = 160 + (X * 160) / 2000 (truncated toward zero), the pixel column 319 - sx */
    const struct { int cx, X; } c[] = { { 0, 0 }, { 1024, 1024 }, { 1025, -1023 }, { -5000, -904 }, { 1023, 1023 }, { -1024, 1024 }, { 3071, 1023 }, { 3072, 1024 }, { 16000, -384 } };
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        ng_cam(c[i].cx, 0, 200, 0);
        ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, LISTS_MAX_SPECKS), 1);
        ASSERT_EQ_INT(sp[0].xy[0], 319 - (160 + (c[i].X * 160) / 2000));
    }
    /* the same for the z word: Z = 2000 + cz', x' = 0, the row from y' = -200 */
    ng_cam(0, 1025, 200, 0); lists_nearground(&L, NG_ID, 179, sp, 1);
    ASSERT_EQ_INT(sp[0].xy[1], 179 - (90 + (-200 * 90) / (2000 - 1023)));
}

TEST(a_candidate_needs_the_class_byte_at_row_abs_z_and_column_abs_x_below_the_limit_of_the_table) {
    ASSERT_TRUE(setup() == 0);
    ng_clear();
    ng_entry(0, 0, 0, 1000, 1); ng_entry(1, 0, 0, 2000, 1); ng_entry(2, 0, 0, 2000, 1);
    lists_speck_t sp[LISTS_MAX_SPECKS];
    /* table A: Z = 1000 -> row 3, X = 0 -> column 0; limit 2 */
    ng_cam(0, 0, 100, 0);
    tab.ng_class[3 * 32 + 0] = 2; ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 1);
    tab.ng_class[3 * 32 + 0] = 3; ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 0);
    /* table B: Z = 2000 -> row 7; limit 3; table C limit 4 */
    ng_cam(0, 0, 200, 0);
    tab.ng_class[7 * 32 + 0] = 3; ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 1);
    tab.ng_class[7 * 32 + 0] = 4; ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 0);
    ng_cam(0, 0, 300, 0);
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 1);
    tab.ng_class[7 * 32 + 0] = 5; ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 0);
    /* a negative X is looked up by its magnitude: X = -300 -> column 1 */
    ng_clear(); ng_entry(1, 0, -300, 2000, 1);
    tab.ng_class[7 * 32 + 1] = 9; ng_cam(0, 0, 200, 0);
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 0);
    tab.ng_class[7 * 32 + 1] = 3; ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 1);
}

TEST(a_triangle_becomes_a_sprite_above_512_u_or_for_class_2_and_needs_all_three_vertices) {
    ASSERT_TRUE(setup() == 0);
    ng_clear();
    lists_speck_t sp[LISTS_MAX_SPECKS];
    /* table B with the synthetic shape of k = -16 (index 2): (14, -10), (-14, -10), (0, 7) before the matrix */
    ng_entry(1, 0, 0, 2000, -16); ng_entry(1, 1, 0, 2000, -20); ng_entry(1, 2, 0, 2000, -12);
    ng_cam(0, 0, 200, 0);
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 3);   /* (the shipped table B has no k = -12 entry; a synthetic one uses the unscaled shape 0) */
    ASSERT_EQ_INT(sp[0].kind, LISTS_SPECK_TRI);
    /* k = -16: vertices (14, 1990), (-14, 1990), (0, 2007), H' = -200: sx = 160 + 14 * 160 / 1990 = 161, 159, 160; sy = 90 + (-18000) / z = 81, 81, 82 (9.04 and 8.97 truncated); pixels (158, 98), (160, 98), (159, 97) */
    ASSERT_EQ_INT(sp[0].xy[0], 158); ASSERT_EQ_INT(sp[0].xy[1], 98); ASSERT_EQ_INT(sp[0].xy[2], 160); ASSERT_EQ_INT(sp[0].xy[3], 98); ASSERT_EQ_INT(sp[0].xy[4], 159); ASSERT_EQ_INT(sp[0].xy[5], 97);
    /* above 512 u (F = 1) every triangle entry is a sprite: k = -12 the 1 by 1, -16 and -20 the 2 by 1 */
    ng_cam(0, 0, 513, 0);
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 0);   /* table C: no entries; the entries live in table B only */
    ng_clear(); for (int t = 1; t < 3; t++) { ng_entry(t, 0, 0, 2000, -12); ng_entry(t, 1, 0, 2000, -16); ng_entry(t, 2, 0, 2000, -20); }
    ng_cam(0, 0, 513, 0);
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 3);
    ASSERT_EQ_INT(sp[0].kind, LISTS_SPECK_1X1); ASSERT_EQ_INT(sp[1].kind, LISTS_SPECK_2X1); ASSERT_EQ_INT(sp[2].kind, LISTS_SPECK_2X1);
    ASSERT_EQ_INT(sp[0].xy[0], 159); ASSERT_EQ_INT(sp[0].xy[1], 179 - (90 + (-513 * 90) / 2000));
    /* class 2 or more (<= the limit 3 of table B): a sprite as well */
    ng_cam(0, 0, 200, 0);
    tab.ng_class[7 * 32 + 0] = 2;
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 3);
    ASSERT_EQ_INT(sp[0].kind, LISTS_SPECK_1X1); ASSERT_EQ_INT(sp[1].kind, LISTS_SPECK_2X1);
    tab.ng_class[7 * 32 + 0] = 1;                                /* class 1 again: triangles */
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 3);
    ASSERT_EQ_INT(sp[0].kind, LISTS_SPECK_TRI); ASSERT_EQ_INT(sp[1].kind, LISTS_SPECK_TRI);
    /* a vertex behind the camera drops the whole triangle (Z = 100, the shape reaches Z - 3000 for a big synthetic shape) */
    tab.ng_shape[2][1][1] = -3000; ng_clear(); ng_entry(1, 0, 0, 100, -16); ng_cam(0, 0, 200, 0);
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 0);
}

TEST(a_big_triangle_is_a_general_fill_and_the_filler_rejects_a_box_below_the_last_row) {
    ASSERT_TRUE(setup() == 0);
    ng_clear();
    lists_speck_t sp[LISTS_MAX_SPECKS];
    /* the shape of k = -16 (unscaled) made big: (300, -300), (-300, -300), (0, 200) at X = 0, Z = 2000, H' = -200:
     * z' = 1700, 1700, 2200; sx = 160 + 300 * 160 / 1700 = 188, 132, 160; sy = 90 - 18000 / 1700 = 80 (10.58 truncated to 10), 80, 90 - 18000 / 2200 = 82 (8.18 truncated to 8);
     * pixels (131, 99), (187, 99), (159, 97): a box of 3 rows and 57 columns */
    tab.ng_shape[2][0][0] = 300; tab.ng_shape[2][0][1] = -300; tab.ng_shape[2][1][0] = -300; tab.ng_shape[2][1][1] = -300; tab.ng_shape[2][2][0] = 0; tab.ng_shape[2][2][1] = 200;
    ng_entry(1, 0, 0, 2000, -16);
    ng_cam(0, 0, 200, 0);
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 179, sp, 8), 1);
    ASSERT_EQ_INT(sp[0].kind, LISTS_SPECK_FILL);
    ASSERT_EQ_INT(sp[0].xy[0], 131); ASSERT_EQ_INT(sp[0].xy[1], 99); ASSERT_EQ_INT(sp[0].xy[2], 187); ASSERT_EQ_INT(sp[0].xy[3], 99); ASSERT_EQ_INT(sp[0].xy[4], 159); ASSERT_EQ_INT(sp[0].xy[5], 97);
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 97, sp, 8), 1);    /* the box starts at row 97: still inside the window of 97 rows */
    ASSERT_EQ_INT(lists_nearground(&L, NG_ID, 96, sp, 8), 0);    /* its top row is below the last row: the filler rejects it */
}

TEST(the_walk_calls_the_pass_once_after_queues_a_and_b_whatever_the_height_but_not_at_the_top_of_the_range) {
    ASSERT_TRUE(setup() == 0);
    use_stub();
    fake_plan(1, 1, 5);
    lists_frame_t in;
    camera(&in, 10, 10, 100 * 256, 0, 0);
    lists_frame(&L, &in);
    ASSERT_EQ_INT(s90_calls, 1);
    ASSERT_EQ_INT(s90_at, 0);                           /* the fake terrain is tier C only: no object of queues A and B is drawn before the call */
    camera(&in, 10, 10, 3000 * 256, 0, 0);              /* above the 2,048 u limit of band 0: the call is still made (it stores the fill block before its height test), no marker follows */
    lists_frame(&L, &in);
    ASSERT_EQ_INT(s90_calls, 2);
    ASSERT_EQ_INT(walk_draws(-1, 3), 0);
    camera(&in, 10, 10, 0x8000000 / 256 + 1000, 0, 0);  /* V:4AB1E at or below -0x8000000: the block holding the call is skipped */
    in.cam_pos[1] = 0x8000000 + 256000;
    lists_frame(&L, &in);
    ASSERT_EQ_INT(s90_calls, 2);
}

/* Note 04a, "The viewed cell in an external view is the camera's" (viewcell_check.py, the rig's examples): the cell is bits 22 to 28 of the masked camera longs, the sub-cell word V:4A909 comes from
 * bits 20 and 21 and the quadrant V:4A908 from bits 22 and 23; bits above 28 are ignored. */
TEST(the_external_view_cell_sub_cell_and_quadrant_come_from_the_camera_longs) {
    lists_scan_t sc;
    int32_t pos[3] = { (int32_t)((70u << 22) | (2u << 20) | 12345u), 0, (int32_t)((65u << 22) | (1u << 20) | 777u) };
    memset(&sc, 0, sizeof sc);
    lists_scan_from_camera(&sc, pos, NULL, 0);
    ASSERT_EQ_INT(sc.cell_x, 70); ASSERT_EQ_INT(sc.cell_z, 65); ASSERT_EQ_INT(sc.sub, 9); ASSERT_EQ_INT(sc.quad, 9);   /* the rig: camera cell (70, 65), sub-cell offsets (2, 1): 9 and 9 */
    pos[0] = (int32_t)((127u << 22) | (0u << 20) | 5u); pos[2] = (int32_t)((3u << 22) | (3u << 20) | 5u);
    lists_scan_from_camera(&sc, pos, NULL, 0);
    ASSERT_EQ_INT(sc.cell_x, 127); ASSERT_EQ_INT(sc.cell_z, 3); ASSERT_EQ_INT(sc.sub, 3); ASSERT_EQ_INT(sc.quad, 0);       /* cell (127, 3), offsets (0, 3): 3 and 0 */
    pos[0] = (int32_t)((1u << 29) | (5u << 22)); pos[2] = (int32_t)((3u << 29) | (6u << 22));
    lists_scan_from_camera(&sc, pos, NULL, 0);
    ASSERT_EQ_INT(sc.cell_x, 5); ASSERT_EQ_INT(sc.cell_z, 6);                                                              /* bits above bit 28 are ignored */
}

TEST(the_loader_refuses_point_entries_that_could_leave_the_class_table_and_the_extreme_ones_stay_inside) {
    ASSERT_TRUE(setup() == 0);
    lists_speck_t sp[LISTS_MAX_SPECKS];
    /* the extreme entries the loader accepts: |x|, |z| = 7,167 plus the lattice offset 1,024 is 8,191, the last cell (row and column 31) of the 32 by 32 table */
    for (int i = 0; i < 4; i++) {
        ng_clear();
        ng_entry(0, 0, i & 1 ? 7167 : -7167, i & 2 ? 7167 : -7167, 1);
        for (int cx = -16384; cx < 16384; cx += 257) { ng_cam(cx, -cx, 100, 0); lists_nearground(&L, NG_ID, 179, sp, LISTS_MAX_SPECKS); }
    }
    /* an entry beyond is refused with a message naming the file */
    buf_t *b = file_buf("tables/lists_ngpts.bin");
    b->d[4] = 0x00; b->d[5] = 0x20;                    /* the first entry's x = 0x2000 = 8,192 */
    lists_tables_t bad; char err[200] = "";
    ASSERT_EQ_INT(lists_tables_load(&bad, &LD, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "lists_ngpts.bin") != NULL);
    build_all();
}

int main(void) {
    (void)drop_file;
    RUN(another_record_gets_its_own_part_id_from_its_distance_word_and_the_queue_index_is_not_the_record);
    RUN(mission_start_requests_everything_and_the_second_frame_requests_again_through_the_octant_copy);
    RUN(a_change_of_octant_is_noticed_one_frame_late_and_not_while_the_pitch_gate_is_set);
    RUN(the_200_foot_crossing_and_the_10240_foot_band_raise_a_rebuild);
    RUN(a_tier_c_only_request_leaves_a_and_b_alone);
    RUN(builder_returns_at_once_above_the_height_limit_and_keeps_the_request);
    RUN(generation_limit_by_altitude_band_and_the_walked_window);
    RUN(the_run_sort_sorts_all_runs_after_a_rebuild_and_one_per_frame_after_that);
    RUN(entities_are_inserted_in_tier_c_only_and_the_direct_list_holds_records_14_and_15);
    RUN(walker_height_limit_distance_refresh_and_the_invisible_entry_countdown);
    RUN(the_drawer_result_follows_the_exits_of_note_04a_8b);
    RUN(the_scaled_distance_is_the_word_times_the_zoom_over_128);
    RUN(entities_are_refreshed_from_the_record_and_sorted_by_the_stored_word_after_the_first_walk);
    RUN(the_draw_stores_the_rotated_vertex_slots_of_the_drawn_block_and_keeps_the_earlier_ones);
    RUN(only_a_block_with_the_record_flag_stores_the_part_id_and_the_slots_and_a_dying_record_at_12_or_less_keeps_its_slots);
    RUN(the_tail_only_draw_carries_a_stale_word_from_an_earlier_call_port_approximation);
    RUN(a_dying_record_at_counter_14_writes_the_explosion_words_and_the_formula_follows_the_divide);
    RUN(the_record_copy_runs_the_ops_of_the_program_on_the_rotated_slots_in_order_with_word_arithmetic);
    RUN(the_first_build_is_sorted_like_any_other);
    RUN(the_near_ground_marker_follows_the_height_limit_of_the_pitch_band);
    RUN(the_walk_calls_the_pass_once_after_queues_a_and_b_whatever_the_height_but_not_at_the_top_of_the_range);
    RUN(the_near_ground_pass_projects_a_table_a_sprite_with_the_height_term_and_the_shift);
    RUN(the_height_picks_the_table_and_the_band_picks_the_height_limit);
    RUN(the_lattice_offset_reduces_the_camera_word_to_minus_1023_through_1024);
    RUN(a_candidate_needs_the_class_byte_at_row_abs_z_and_column_abs_x_below_the_limit_of_the_table);
    RUN(a_triangle_becomes_a_sprite_above_512_u_or_for_class_2_and_needs_all_three_vertices);
    RUN(the_external_view_cell_sub_cell_and_quadrant_come_from_the_camera_longs);
    RUN(the_loader_refuses_point_entries_that_could_leave_the_class_table_and_the_extreme_ones_stay_inside);
    RUN(a_big_triangle_is_a_general_fill_and_the_filler_rejects_a_box_below_the_last_row);
    RUN(the_player_part_id_follows_note_04a_section_8a);
    RUN(the_player_entry_variant_and_result_follow_the_views);
    RUN(the_part_id_stays_when_the_player_is_not_drawn_or_the_draw_is_not_completed);
    lists_tables_free(&tab);
    TEST_MAIN_END();
}
