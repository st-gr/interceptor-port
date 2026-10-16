/* tests/test_views.c - the view kinds and cameras on the extracted assets (milestone 3 Task 2 round 2; docs/re/06-cockpit-hud.md "Head views" and "View kinds, settled",
 * docs/re/03-3d-pipeline.md "Camera attitude in the head and external views" and "External camera aim", docs/re/04a-display-lists.md "The 23 camera and view request sites").
 * Runs the camera builder on staged aircraft states and the whole game (game_input, game_step, game_scene); skipped (exit 0) when the assets are absent. */
#include "test.h"
#include "../src/core/game.h"
#include "../src/core/flight.h"
#include "../src/core/world.h"

static const char *root;
static int fl_read(void *user, const char *path, uint8_t **out, size_t *len) {
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
static void fl_release(void *user, uint8_t *buf) { (void)user; free(buf); }
static game_loader_t loader = { fl_read, fl_release, NULL };
static flight_tables_t TAB;

static int16_t image_cb(void *user, uint32_t addr) { int known; return flight_image_word((const flight_tables_t *)user, addr, &known); }

static game_t *start_game(void) {
    char err[128];
    game_t *g = game_create(&loader, err, sizeof err);
    if (!g) { printf("  (game_create: %s)\n", err); return NULL; }
    game_debug_start_flight(g, 3, 0);
    return g;
}
static void press(game_t *g, int code, int value) { game_event_t ev = { GAME_EV_BUTTON, code, value }; game_input(g, &ev); }
static void kp(game_t *g, int key) { press(g, GAME_BTN_KEYPAD, key); }
static game_scene_t SC;
static const game_scene_t *scene(game_t *g) { game_scene(g, &SC); return &SC; }
static const game_scene_t *step_scene(game_t *g) { game_step(g); return scene(g); }
static unsigned request_byte(game_t *g) { return world_lists((world_t *)game_world(g))->request; }

/* A staged aircraft in the air (the engine of the test: the game's own environment is installed by game_create), far above any clamp. */
static void stage(aircraft_t *ac, uint16_t pitch, uint16_t heading, uint16_t roll) {
    flight_set_position(ac, 273272584, 1000000, 280480032);
    flight_set_attitude(ac, pitch, heading, roll);
}
static void vs_init(world_viewstate_t *vs, int level, int ring) {
    memset(vs, 0, sizeof *vs);
    vs->level = level; vs->ring = ring; vs->look = 1; vs->aim_on = 1;
    vs->image_word = image_cb; vs->image_user = &TAB;
}
static void offset(const world_t *w, const aircraft_t *ac, int level, int ring, int32_t out[3]) {
    world_viewstate_t vs; world_camera_t c;
    vs_init(&vs, level, ring);
    if (world_camera_build(w, ac, &vs, &c) != 0) { out[0] = out[1] = out[2] = 0x7FFFFFFF; return; }
    for (int i = 0; i < 3; i++) out[i] = c.pos[i] - ac->pos[i];
}

/* ---- the ring (note 06 item 4) ---------------------------------------------------------------------------------------------- */

TEST(the_ring_offset_is_the_matrix_sum_shifted_right_4_in_units_of_1024) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    const world_t *w = game_world(g);
    stage(ac, 0, 2240, 0);   /* the ground snapshot: heading 2240, matrix words 14466, 0, -7692, 0, 16384, 0, 7692, 0, 14466 */
    ASSERT_EQ_INT(ac->f.mat_b[0], 14466); ASSERT_EQ_INT(ac->f.mat_b[2], -7692); ASSERT_EQ_INT(ac->f.mat_b[8], 14466);
    int32_t o[3];
    offset(w, ac, -1, 4, o);
    ASSERT_EQ_INT(o[0], 8653); ASSERT_EQ_INT(o[1], 3072); ASSERT_EQ_INT(o[2], -16275);
    /* levels 1 to 4 (the words (0, 4, -27), (0, 6, -36), (0, 12, -72), (0, 24, -144)) */
    static const int32_t lx[4] = { 12980, 17307, 34614, 69228 }, ly[4] = { 4096, 6144, 12288, 24576 };
    for (int l = 1; l <= 4; l++) {
        offset(w, ac, l, 4, o);
        ASSERT_EQ_INT(o[0], lx[l - 1]); ASSERT_EQ_INT(o[1], ly[l - 1]);
    }
    game_destroy(g);
}

TEST(directions_0_and_4_follow_pitch_and_roll_the_other_eight_use_the_heading_matrix) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    const world_t *w = game_world(g);
    stage(ac, 26360, 2256, 0);   /* the airborne snapshot: pitch 26360, heading 2256, matrix 14439, 3930, -6671, 0, 14117, 8316, 7742, -7329, 12441 */
    static const int16_t m[9] = { 14439, 3930, -6671, 0, 14117, 8316, 7742, -7329, 12441 };
    for (int i = 0; i < 9; i++) ASSERT_EQ_INT(ac->f.mat_b[i], m[i]);
    int32_t o[3];
    offset(w, ac, -1, 4, o); ASSERT_EQ_INT(o[0], 8241); ASSERT_EQ_INT(o[1], -6709); ASSERT_EQ_INT(o[2], -15371);
    offset(w, ac, -1, 0, o); ASSERT_EQ_INT(o[0], -6768); ASSERT_EQ_INT(o[1], 12002); ASSERT_EQ_INT(o[2], 12621);
    offset(w, ac, -1, 2, o); ASSERT_EQ_INT(o[0], -16244); ASSERT_EQ_INT(o[1], 3072); ASSERT_EQ_INT(o[2], -8710);
    offset(w, ac, -1, 8, o); ASSERT_EQ_INT(o[0], 2419); ASSERT_EQ_INT(o[1], 21504); ASSERT_EQ_INT(o[2], -4513);
    /* the ejection seat's class (the high nibble 0x30) is heading only for every direction, also 0 and 4 */
    ac->f.cls = 0x34;
    int16_t r[3];
    world_camera_ring(w, 3, 4, r);
    const uint32_t a = fx_neg_angle(2256);
    const int s_ = fx_sin(a), c_ = fx_cos(a);
    offset(w, ac, -1, 4, o);
    ASSERT_EQ_INT(o[0], (c_ * r[0] + s_ * r[2]) >> 4); ASSERT_EQ_INT(o[1], (16384 * r[1]) >> 4); ASSERT_EQ_INT(o[2], (-s_ * r[0] + c_ * r[2]) >> 4);
    game_destroy(g);
}

/* ---- the aim (note 03 "External camera aim") -------------------------------------------------------------------------------- */

TEST(the_aim_target_quadrants_and_the_precision_bands) {
    world_viewstate_t vs; vs_init(&vs, -1, 4);
    int32_t p, h;
    /* heading 0 faces +z, 90 degrees faces -x (note 03); positive pitch is down */
    world_aim_target(&vs, 0, 0, 1000, &p, &h);       ASSERT_EQ_INT(h, 0);
    world_aim_target(&vs, -5120, 0, 5120, &p, &h);   ASSERT_EQ_INT(h, 3600);
    world_aim_target(&vs, -1000, 0, 0, &p, &h);      ASSERT_EQ_INT(h, 7200);
    world_aim_target(&vs, -5120, 0, -5120, &p, &h);  ASSERT_EQ_INT(h, 10800);
    world_aim_target(&vs, 0, 0, -1000, &p, &h);      ASSERT_EQ_INT(h, 14400);
    world_aim_target(&vs, 5120, 0, -5120, &p, &h);   ASSERT_EQ_INT(h, 18000);
    world_aim_target(&vs, 1000, 0, 0, &p, &h);       ASSERT_EQ_INT(h, 21600);
    world_aim_target(&vs, 5120, 0, 5120, &p, &h);    ASSERT_EQ_INT(h, 25200);
    /* every precision band (S, T) = (8, 6), (10, 4), (12, 2), (14, 0), and the shift by 3 above 0x10000000: an exact 45 degrees (multiples of 256 << S, so the truncated words keep the ratio) */
    world_aim_target(&vs, 5120, 0, 5120, &p, &h);                ASSERT_EQ_INT(h, 25200);
    world_aim_target(&vs, 4194304, 0, 4194304, &p, &h);          ASSERT_EQ_INT(h, 25200);
    world_aim_target(&vs, 16777216, 0, 16777216, &p, &h);        ASSERT_EQ_INT(h, 25200);
    world_aim_target(&vs, 67108864, 0, 67108864, &p, &h);        ASSERT_EQ_INT(h, 25200);
    world_aim_target(&vs, 0x20000000, 0, 0x20000000, &p, &h);    ASSERT_EQ_INT(h, 25200);
    /* note 03, the rig example (-1000, 0, 1000): x' = z' = 3 (truncated from 3.9), the quotient is 333 / 256 rounded, so the index is 333, past the 261 table words (no clamp: the 512 words
     * after 0x42C00 are read by address from the trig span); the word there is 9, so the pair is (pitch 0, heading 72), not the table's 45 degrees */
    ASSERT_EQ_INT(image_cb(&TAB, 0x42C00u + 2u * 333u), 9);
    world_aim_target(&vs, -1000, 0, 1000, &p, &h);               ASSERT_EQ_INT(h, 72); ASSERT_EQ_INT(p, 0);
    world_aim_target(&vs, 0, -1000, 1000, &p, &h);   ASSERT_EQ_INT(p, 3600);    /* below the camera: 45 degrees down */
    world_aim_target(&vs, 0, 1000, 1000, &p, &h);    ASSERT_EQ_INT(p, 25200);   /* above: 28800 - 3600 */
    world_aim_target(&vs, 0, -1000, 0, &p, &h);      ASSERT_EQ_INT(p, 7200);    /* straight down: y' > H = 0, 8 x (900 - table[0]) */
}

TEST(the_step_moves_a_quarter_of_the_difference_the_short_way_round_capped_by_the_limit) {
    ASSERT_EQ_INT(world_aim_step(1000, 1400, 2000), 1100);        /* d = 400: +100 */
    ASSERT_EQ_INT(world_aim_step(1000, 9000, 2000), 3000);        /* d = 8000: a quarter is 2000 */
    ASSERT_EQ_INT(world_aim_step(1000, 9000, 560), 1560);         /* the camera modes' limit */
    ASSERT_EQ_INT(world_aim_step(5000, 3000, 2000), 4500);        /* d = -2000: -500 */
    ASSERT_EQ_INT(world_aim_step(20000, 1000, 2000), 20000 + 2000);   /* d = -19000 (below -14400): up through the wrap, (d + 28800) >> 2 = 2450 capped at 2000 */
    ASSERT_EQ_INT(world_aim_step(28000, 1000, 2000), 28450);      /* (-27000 + 28800) >> 2 = 450 */
    ASSERT_EQ_INT(world_aim_step(28700, 100, 2000), 28750);       /* d = -28600: (200) >> 2 = 50 */
    ASSERT_EQ_INT(world_aim_step(28750, 100, 2000), 28787);
    ASSERT_EQ_INT(world_aim_step(28780, 100, 2000), 10);          /* the sum 28810 reaches 28800: 28800 is subtracted */
    ASSERT_EQ_INT(world_aim_step(1000, 27000, 2000), 300);        /* d = 26000 above 14400: down through zero, (28800 - d) >> 2 = 700 */
    ASSERT_EQ_INT(world_aim_step(100, 27000, 2000), 28800 + 100 - 475);   /* d = 26900: 475 below zero, 28800 added */
    ASSERT_EQ_INT(world_aim_step(777, 777, 2000), 777);
}

TEST(the_first_external_frame_snaps_then_a_quarter_step_with_a_snap_on_a_new_key_or_without_look_at) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    const world_t *w = game_world(g);
    stage(ac, 0, 2240, 0);
    ac->pos[1] = 30472;
    world_viewstate_t vs; world_camera_t c;
    vs_init(&vs, -1, 4);
    vs.aim_pitch = 1397; vs.aim_heading = 25550;                  /* the stale pair of the emulator capture */
    ASSERT_EQ_INT(world_camera_build(w, ac, &vs, &c), 0);
    ASSERT_EQ_INT(vs.aim_pitch, 720); ASSERT_EQ_INT(vs.aim_heading, 2240);   /* the ground snapshot's first external frame (note 03) */
    ASSERT_EQ_INT(c.pitch, -720); ASSERT_EQ_INT(c.yaw, 2240); ASSERT_EQ_INT(c.roll, 0);
    /* the level keys do not snap: from the converged level -1 pair 720 each game pass moves a quarter of the remaining difference toward the snap target (level 1: 640; levels 2 to 4: 760
     * at heading 2240 on the ground snapshot), no pass count is fitted (note 03 "Levels and passes"); the step is 0 when the difference is 3 or less */
    static const int seq1[16] = { 700, 685, 674, 666, 660, 655, 652, 649, 647, 646, 645, 644, 643, 643, 643, 643 };
    static const int seq2[16] = { 730, 737, 742, 746, 749, 751, 753, 754, 755, 756, 757, 757, 757, 757, 757, 757 };
    for (int lvl = 1; lvl <= 4; lvl++) {
        vs.level = -1; vs.aim_pitch = 720; vs.aim_heading = 2240;
        vs.level = lvl;
        for (int i = 0; i < 16; i++) {
            world_camera_build(w, ac, &vs, &c);
            ASSERT_EQ_INT(vs.aim_pitch, lvl == 1 ? seq1[i] : seq2[i]);
            ASSERT_EQ_INT(vs.aim_heading, 2240);
        }
    }
    vs.level = 4;
    /* a changed ring key snaps: the pair is the target of the new vector, not a quarter step from the old pair */
    vs.ring = 2;
    world_camera_build(w, ac, &vs, &c);
    ASSERT_TRUE(vs.aim_heading > 2240 + 560 || vs.aim_heading < 2240 - 560);
    /* without the look-at flag nothing is aimed: the stored pair is the camera (the map's) */
    vs.look = 0; vs.aim_pitch = 7200; vs.aim_heading = 0;
    world_camera_build(w, ac, &vs, &c);
    ASSERT_EQ_INT(c.pitch, -7200); ASSERT_EQ_INT(c.yaw, 0);
    /* V:4A86D clear: snap every frame */
    vs.look = 1; vs.aim_on = 0;
    world_camera_build(w, ac, &vs, &c);
    const int32_t snapped = vs.aim_heading;
    vs.ring = 2; world_camera_build(w, ac, &vs, &c);
    ASSERT_EQ_INT(vs.aim_heading, snapped);
    /* a camera mode limits to 560 */
    vs.aim_on = 1; vs.mode = 1; vs.aim_pitch = 0; vs.aim_heading = 0;
    world_camera_build(w, ac, &vs, &c);
    ASSERT_TRUE(vs.aim_heading == 560 || vs.aim_heading == 28800 - 560);
    game_destroy(g);
}

/* ---- the head views (note 06 "Head views", note 03) --------------------------------------------------------------------------- */

/* The camera words of a head code as game.c computes them (the composition 0x325B8 of the record matrix with the head triple, or the record's own words for code 0). */
static void head_words(aircraft_t *ac, int code, int out[3]) {
    int t[3];
    int32_t w[3];
    if (!world_head_triple(ac->f.cls, code, (int16_t)ac->f.pitch, t)) { out[0] = ac->f.pitch; out[1] = ac->f.heading; out[2] = ac->f.roll; return; }
    flight_head_camera(ac, &TAB, t[0], t[1], t[2], w);
    out[0] = w[0]; out[1] = w[1]; out[2] = w[2];
}

TEST(head_views_compose_the_head_triple_with_the_aircraft_attitude) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    int w[3];
    stage(ac, 26360, 2256, 0);   /* the airborne snapshot (note 06 "Camera attitude in the head views") */
    static const struct { int code, p, h, r; } air[] = {
        { 0, 26360, 2256, 0 }, { 1, 26664, 4800, 27552 }, { 3, 0, 9456, 26360 }, { 6, 2440, 16656, 0 }, { 9, 0, 23856, 2440 },
        { 11, 26664, 28496, 1248 }, { 12, 24024, 16680, 14400 }, { 13, 4776, 2280, 0 } };
    for (unsigned k = 0; k < sizeof air / sizeof air[0]; k++) {
        head_words(ac, air[k].code, w);
        if (w[0] != air[k].p || w[1] != air[k].h || w[2] != air[k].r) printf("  code %d: (%d, %d, %d)\n", air[k].code, w[0], w[1], w[2]);
        ASSERT_EQ_INT(w[0], air[k].p); ASSERT_EQ_INT(w[1], air[k].h); ASSERT_EQ_INT(w[2], air[k].r);
    }
    stage(ac, 0, 2240, 0);       /* the ground snapshot */
    static const struct { int code, p, h; } gnd[] = {
        { 1, 0, 4504 }, { 2, 0, 7040 }, { 3, 0, 9440 }, { 4, 0, 11832 }, { 6, 0, 16640 }, { 9, 0, 23840 }, { 11, 0, 16 }, { 12, 22176, 2176 }, { 13, 6624, 2176 } };
    for (unsigned k = 0; k < sizeof gnd / sizeof gnd[0]; k++) {
        head_words(ac, gnd[k].code, w);
        ASSERT_EQ_INT(w[0], gnd[k].p); ASSERT_EQ_INT(w[1], gnd[k].h); ASSERT_EQ_INT(w[2], 0);
    }
    game_destroy(g);
}

TEST(the_head_triples_and_the_level_test_of_codes_12_and_13) {
    int t[3];
    ASSERT_EQ_INT(world_head_triple(0x11, 0, 0, t), 0);                     /* code 0 of an aircraft: no composition */
    ASSERT_EQ_INT(world_head_triple(0x11, 5, 0, t), 1); ASSERT_EQ_INT(t[0], 0); ASSERT_EQ_INT(t[1], 12000); ASSERT_EQ_INT(t[2], 0);
    ASSERT_EQ_INT(world_head_triple(0x11, 11, 0, t), 1); ASSERT_EQ_INT(t[1], 26544);
    world_head_triple(0x11, 12, 559, t);   ASSERT_EQ_INT(t[0], 0x5690);       /* p < 560: within 7 degrees of level */
    world_head_triple(0x11, 12, 560, t);   ASSERT_EQ_INT(t[0], 0x5460);
    world_head_triple(0x11, 12, 28239, t); ASSERT_EQ_INT(t[0], 0x5460);
    world_head_triple(0x11, 12, 28240, t); ASSERT_EQ_INT(t[0], 0x5690);       /* p >= 28240 */
    world_head_triple(0x11, 13, 100, t);   ASSERT_EQ_INT(t[0], 0x19F0);
    world_head_triple(0x11, 13, 26360, t); ASSERT_EQ_INT(t[0], 0x1C20);
    /* the ejection seat (class 0x30): the table 0x323DC and a twelfth row (21600, 0, 0) for code 0 */
    ASSERT_EQ_INT(world_head_triple(0x30, 0, 0, t), 1); ASSERT_EQ_INT(t[0], 21600); ASSERT_EQ_INT(t[1], 0); ASSERT_EQ_INT(t[2], 0);
    world_head_triple(0x30, 1, 0, t);  ASSERT_EQ_INT(t[0], 24000); ASSERT_EQ_INT(t[1], 7200); ASSERT_EQ_INT(t[2], 21600);
    world_head_triple(0x30, 2, 0, t);  ASSERT_EQ_INT(t[0], 26400);
    world_head_triple(0x30, 3, 0, t);  ASSERT_EQ_INT(t[0], 0);
    world_head_triple(0x30, 4, 0, t);  ASSERT_EQ_INT(t[0], 2400);
    world_head_triple(0x30, 11, 0, t); ASSERT_EQ_INT(t[0], 19200);
    world_head_triple(0x30, 12, 7000, t); ASSERT_EQ_INT(t[0], 0x3610);
    world_head_triple(0x30, 12, 8000, t); ASSERT_EQ_INT(t[0], 0x3840);
    world_head_triple(0x30, 13, 7000, t); ASSERT_EQ_INT(t[0], 0x230);
    world_head_triple(0x30, 13, 100, t);  ASSERT_EQ_INT(t[0], 0);
}

TEST(a_head_key_in_the_game_composes_the_camera_and_the_pitch_gate_reads_the_composed_pitch) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    step_scene(g);
    ASSERT_EQ_INT(world_lists((world_t *)game_world(g))->scan.gate, 0);   /* level forward view: pitch word 0 */
    kp(g, 7);                                                             /* KP7: code 12 */
    const game_scene_t *s = step_scene(g);
    ASSERT_EQ_INT(s->camera.head, 12);
    int w[3];
    head_words(ac, 12, w);                                                /* the aircraft is level: P = 0x5690 = 22160 */
    ASSERT_EQ_INT(w[0], 22176);                                           /* near level: 22176 (note 06, the ground snapshot's up view) */
    ASSERT_EQ_INT(((-s->camera.pitch) % 28800 + 28800) % 28800, w[0]);
    ASSERT_EQ_INT((int)s->camera.yaw, w[1]);
    ASSERT_EQ_INT(world_lists((world_t *)game_world(g))->scan.gate, 1);   /* 22176 / 8 = 2772 lies in 600 .. 3150: the gate is set in both up and down views */
    kp(g, 8);
    step_scene(g);
    ASSERT_EQ_INT(world_lists((world_t *)game_world(g))->scan.gate, 0);
    game_destroy(g);
}

TEST(a_head_view_is_momentary_the_hold_runs_down_by_the_timer_second_and_kp5_locks_it) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    kp(g, 6);
    ASSERT_EQ_INT(step_scene(g)->camera.head, 1);
    for (int i = 0; i < 40; i++) { game_clock_second(g); game_step(g); }            /* held: no release yet, nothing recentres */
    ASSERT_EQ_INT(scene(g)->camera.head, 1);
    press(g, GAME_BTN_KEYPAD_UP, 6);                                              /* the release sets the hold byte to 5 */
    for (int sec = 0; sec < 4; sec++) { game_clock_second(g); game_step(g); ASSERT_EQ_INT(scene(g)->camera.head, 1); }
    game_clock_second(g); game_step(g);                                           /* the 5th second: 0, and the state routine recentres in that pass or the next */
    game_step(g);
    ASSERT_EQ_INT(scene(g)->camera.head, 0);
    kp(g, 4);                                                                      /* KP5 locks the present head view */
    ASSERT_EQ_INT(step_scene(g)->camera.head, 11);
    press(g, GAME_BTN_KEYPAD_UP, 4);
    kp(g, 5);
    for (int sec = 0; sec < 12; sec++) { game_clock_second(g); game_step(g); }
    ASSERT_EQ_INT(scene(g)->camera.head, 11);
    kp(g, 9);                                                                      /* KP9: code 0 */
    ASSERT_EQ_INT(step_scene(g)->camera.head, 0);
    game_destroy(g);
}

/* ---- the view switch (note 06 item 7, 04a) ------------------------------------------------------------------------------------ */

TEST(kp_enter_is_a_request_bit_two_presses_in_one_pass_are_one_toggle) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    kp(g, GAME_KP_ENTER); kp(g, GAME_KP_ENTER);
    ASSERT_EQ_INT(scene(g)->camera.view, GAME_VIEW_COCKPIT);              /* nothing happens until the pass */
    ASSERT_EQ_INT(step_scene(g)->camera.view, GAME_VIEW_CHASE);           /* once */
    kp(g, GAME_KP_ENTER); kp(g, GAME_KP_ENTER); kp(g, GAME_KP_ENTER);
    ASSERT_EQ_INT(step_scene(g)->camera.view, GAME_VIEW_COCKPIT);
    kp(g, GAME_KP_ENTER); press(g, GAME_BTN_TOWER, 1);                    /* KP-Enter and M together: one switch, to the map */
    ASSERT_EQ_INT(step_scene(g)->camera.view, GAME_VIEW_TOWER);
    ASSERT_EQ_INT(step_scene(g)->camera.view, GAME_VIEW_TOWER);
    game_destroy(g);
}

TEST(the_first_external_frame_after_every_kp_enter_snaps) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    stage(ac, 0, 2240, 0); ac->pos[1] = 30472;
    kp(g, GAME_KP_ENTER);
    const game_scene_t *s = step_scene(g);
    ASSERT_EQ_INT(s->camera.view, GAME_VIEW_CHASE);
    ASSERT_EQ_INT(s->camera.pitch, -720);   /* the snap to the target of the ground snapshot's chase */
    ASSERT_EQ_INT(s->camera.yaw, 2240);
    kp(g, GAME_KP_ENTER); step_scene(g);    /* back to the cockpit: V:4A85E is cleared */
    stage(ac, 0, 7200, 0); ac->pos[1] = 30472;
    kp(g, GAME_KP_ENTER);
    s = step_scene(g);
    ASSERT_EQ_INT(s->camera.yaw, 7200);     /* a snap again, not a quarter step from 2240 */
    game_destroy(g);
}

TEST(the_keys_are_processed_while_paused_the_world_is_not_redrawn_and_the_switch_waits) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    step_scene(g);
    press(g, GAME_BTN_PAUSE, 1);
    const game_scene_t *s = step_scene(g);
    ASSERT_EQ_INT(s->mode, GAME_MODE_PAUSED); ASSERT_EQ_INT(s->flight_blank, 1);
    kp(g, 6);                                                              /* the head code changes in the key routine */
    s = step_scene(g);
    ASSERT_EQ_INT(s->camera.head, 1);
    press(g, GAME_BTN_RBRACKET, 1);                                        /* the zoom word halves */
    ASSERT_EQ_INT(scene(g)->hud.cp_zoom, 0x40);
    kp(g, GAME_KP_ENTER); press(g, GAME_BTN_TOWER, 1);                     /* KP-Enter then M while paused */
    for (int i = 0; i < 4; i++) { s = step_scene(g); ASSERT_EQ_INT(s->camera.view, GAME_VIEW_COCKPIT); ASSERT_EQ_INT(s->flight_blank, 1); }
    press(g, GAME_BTN_PAUSE, 1);
    s = scene(g); ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT); ASSERT_EQ_INT(s->flight_blank, 0);
    s = step_scene(g);
    ASSERT_EQ_INT(s->camera.view, GAME_VIEW_TOWER);                        /* one switch: the map, from the first flight pass */
    ASSERT_EQ_INT(s->sky.row_limit, 179);
    game_destroy(g);
}

TEST(the_pointer_and_the_axes_keep_summing_while_paused_and_n_keeps_its_low_bits) {
    int32_t p[2];
    for (int variant = 0; variant < 2; variant++) {
        game_t *g = start_game();
        ASSERT_TRUE(g != NULL);
        for (int i = 0; i < 5; i++) game_step(g);
        const uint32_t n = scene(g)->hud.cp_n;
        press(g, GAME_BTN_PAUSE, 1);
        if (variant) {   /* the mouse moves during the pause: the counter sums, the first flight pass takes it */
            game_event_t ev = { GAME_EV_AXIS, GAME_AXIS_POINTER_DY, -40 };
            game_input(g, &ev); game_input(g, &ev);
        }
        for (int i = 0; i < 3; i++) game_step(g);
        ASSERT_EQ_INT(scene(g)->hud.cp_n, n);
        press(g, GAME_BTN_PAUSE, 1);
        game_step(g);
        ASSERT_EQ_INT(scene(g)->hud.cp_n, n + 1);                              /* no n & 7 clear */
        p[variant] = game_debug_aircraft(g)->f.v4a830;
        game_destroy(g);
    }
    ASSERT_TRUE(p[0] != p[1]);
}

/* ---- the bracket keys by view (note 06 items 1, 2, 9) ------------------------------------------------------------------------- */

TEST(the_bracket_keys_zoom_in_the_cockpit_step_the_level_outside_and_the_altitude_in_the_map) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    static const int closer[3] = { 0x40, 0x20, 0x20 }, farther[3] = { 0x40, 0x80, 0x80 };
    for (int k = 0; k < 3; k++) { press(g, GAME_BTN_RBRACKET, 1); press(g, GAME_BTN_RBRACKET, 0); ASSERT_EQ_INT(step_scene(g)->hud.cp_zoom, closer[k]); }
    for (int k = 0; k < 3; k++) { press(g, GAME_BTN_LBRACKET, 1); press(g, GAME_BTN_LBRACKET, 0); ASSERT_EQ_INT(step_scene(g)->hud.cp_zoom, farther[k]); }
    ASSERT_EQ_INT(scene(g)->sky.zoom[2], 0x80);
    press(g, GAME_BTN_RBRACKET, 1);
    ASSERT_EQ_INT(step_scene(g)->sky.zoom[2], 0x40);                       /* the sky and the matrix take the zoom word */
    press(g, GAME_BTN_LBRACKET, 1); step_scene(g);
    /* external: the level -1, 1, 2, 3, 4 (the camera's offset from the aircraft shows it); [ farther stops at 4, ] closer stops at -1 */
    kp(g, GAME_KP_ENTER);
    const game_scene_t *s = step_scene(g);
    ASSERT_EQ_INT(s->camera.pos.z - ac->pos[2], -18 * 1024);               /* the carrier deck start: heading 0, class 0x11 ring entry 4 (0, 3, -18) */
    static const int far_z[5] = { -27 * 1024, -36 * 1024, -72 * 1024, -144 * 1024, -144 * 1024 }, near_z[5] = { -72 * 1024, -36 * 1024, -27 * 1024, -18 * 1024, -18 * 1024 };
    for (int k = 0; k < 5; k++) { press(g, GAME_BTN_LBRACKET, 1); s = step_scene(g); ASSERT_EQ_INT(s->camera.pos.z - ac->pos[2], far_z[k]); }
    for (int k = 0; k < 5; k++) { press(g, GAME_BTN_RBRACKET, 1); s = step_scene(g); ASSERT_EQ_INT(s->camera.pos.z - ac->pos[2], near_z[k]); }
    /* the map: ] lowers the altitude by 0x2000000 down to 0x1000000, [ raises it up to 0x8000000 */
    press(g, GAME_BTN_TOWER, 1);
    s = step_scene(g);
    ASSERT_EQ_INT(s->camera.view, GAME_VIEW_TOWER);
    ASSERT_EQ_INT(s->camera.pos.y, 0x3000000);
    press(g, GAME_BTN_RBRACKET, 1); ASSERT_EQ_INT(step_scene(g)->camera.pos.y, 0x1000000);
    press(g, GAME_BTN_RBRACKET, 1); ASSERT_EQ_INT(step_scene(g)->camera.pos.y, 0x1000000);
    for (int k = 0; k < 3; k++) press(g, GAME_BTN_LBRACKET, 1);
    ASSERT_EQ_INT(step_scene(g)->camera.pos.y, 0x7000000);
    press(g, GAME_BTN_LBRACKET, 1); ASSERT_EQ_INT(step_scene(g)->camera.pos.y, 0x8000000);
    press(g, GAME_BTN_LBRACKET, 1); ASSERT_EQ_INT(step_scene(g)->camera.pos.y, 0x8000000);
    ASSERT_EQ_INT(scene(g)->hud.cp_zoom, 0x80);                            /* the zoom word is the cockpit's: the map keys leave it */
    game_destroy(g);
}

TEST(only_m_saves_the_map_altitude_and_m_or_kp_enter_in_the_map_return_to_the_cockpit) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    press(g, GAME_BTN_TOWER, 1); step_scene(g);
    press(g, GAME_BTN_RBRACKET, 1);
    ASSERT_EQ_INT(step_scene(g)->camera.pos.y, 0x1000000);
    kp(g, GAME_KP_ENTER);                                                  /* KP-Enter: back to the cockpit, the altitude is not saved */
    ASSERT_EQ_INT(step_scene(g)->camera.view, GAME_VIEW_COCKPIT);
    press(g, GAME_BTN_TOWER, 1);
    ASSERT_EQ_INT(step_scene(g)->camera.pos.y, 0x3000000);
    press(g, GAME_BTN_RBRACKET, 1); step_scene(g);
    press(g, GAME_BTN_TOWER, 1);                                           /* M in the map saves 0x1000000 and returns */
    ASSERT_EQ_INT(step_scene(g)->camera.view, GAME_VIEW_COCKPIT);
    press(g, GAME_BTN_TOWER, 1);
    ASSERT_EQ_INT(step_scene(g)->camera.pos.y, 0x1000000);
    /* M in an external view: the map at once (no request bit), the level stays; M again returns to the cockpit, never to the external view */
    press(g, GAME_BTN_TOWER, 1); step_scene(g);
    kp(g, GAME_KP_ENTER); step_scene(g);
    kp(g, GAME_KP_ENTER); step_scene(g);                                   /* cockpit */
    kp(g, GAME_KP_ENTER); step_scene(g); step_scene(g);                    /* external */
    ASSERT_EQ_INT(scene(g)->camera.view, GAME_VIEW_CHASE);
    ASSERT_EQ_INT(request_byte(g), 0);
    press(g, GAME_BTN_TOWER, 1);
    ASSERT_EQ_INT(request_byte(g), 0);                                      /* M in an external view requests nothing */
    ASSERT_EQ_INT(scene(g)->camera.view, GAME_VIEW_TOWER);                  /* and shows the map at once */
    press(g, GAME_BTN_TOWER, 1);
    ASSERT_EQ_INT(step_scene(g)->camera.view, GAME_VIEW_COCKPIT);
    game_destroy(g);
}

TEST(the_map_camera_follows_the_aircraft_every_frame) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    press(g, GAME_BTN_TOWER, 1);
    const game_scene_t *s = step_scene(g);
    ASSERT_EQ_INT(s->camera.view, GAME_VIEW_TOWER);
    ASSERT_EQ_INT(s->camera.pos.x, ac->pos[0]); ASSERT_EQ_INT(s->camera.pos.z, ac->pos[2]);
    flight_set_position(ac, 273272584 + 5000, 1000000, 280480032 - 7000);
    s = step_scene(g);
    ASSERT_TRUE(s->camera.pos.x == ac->pos[0] && s->camera.pos.z == ac->pos[2]);
    ASSERT_TRUE(s->camera.pos.x - 273272584 > 4000);
    ASSERT_EQ_INT(s->camera.pitch, -WORLD_MAP_PITCH);
    ASSERT_EQ_INT(s->sky.sky_n, 0);                                        /* looking straight down: no sky polygon in the map */
    ASSERT_EQ_INT(s->sky.cockpit, 0);
    game_destroy(g);
}

/* ---- the list requests (04a, the 23 sites) ------------------------------------------------------------------------------------ */

TEST(the_requests_follow_the_original_sites) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    step_scene(g); step_scene(g);
    ASSERT_EQ_INT(request_byte(g), 0);
    static const int head_keys[7] = { 8, 9, 2, 4, 6, 7, 1 };
    for (int k = 0; k < 7; k++) { kp(g, head_keys[k]); ASSERT_EQ_INT(request_byte(g), 0xFF); step_scene(g); step_scene(g); ASSERT_EQ_INT(request_byte(g), 0); }
    kp(g, 5);  ASSERT_EQ_INT(request_byte(g), 0);                          /* KP5 only stores the hold byte */
    kp(g, 0);  ASSERT_EQ_INT(request_byte(g), 0);
    kp(g, 3);  ASSERT_EQ_INT(request_byte(g), 0xFF); step_scene(g); step_scene(g);   /* KP3 and KP-dot in the cockpit */
    kp(g, GAME_KP_DOT); ASSERT_EQ_INT(request_byte(g), 0xFF); step_scene(g); step_scene(g);
    press(g, GAME_BTN_RBRACKET, 1); ASSERT_EQ_INT(request_byte(g), 0xFF); step_scene(g); step_scene(g);   /* the zoom keys, also at the stop */
    press(g, GAME_BTN_LBRACKET, 1); press(g, GAME_BTN_LBRACKET, 1); step_scene(g); step_scene(g);
    press(g, GAME_BTN_LBRACKET, 1); ASSERT_EQ_INT(request_byte(g), 0xFF); step_scene(g); step_scene(g);
    kp(g, GAME_KP_ENTER);  ASSERT_EQ_INT(request_byte(g), 0);              /* KP-Enter and M store no request themselves */
    press(g, GAME_BTN_TOWER, 1); ASSERT_EQ_INT(request_byte(g), 0);
    game_step(g); ASSERT_EQ_INT(scene(g)->camera.view, GAME_VIEW_TOWER);  /* the view switch (0x142B4) requests; the builder consumed it in the same frame */
    kp(g, 6);  ASSERT_EQ_INT(request_byte(g), 0xFF); step_scene(g); step_scene(g);   /* the ring keys, also in the map */
    press(g, GAME_BTN_RBRACKET, 1); ASSERT_EQ_INT(request_byte(g), 0xFF); step_scene(g); step_scene(g);   /* the altitude keys */
    press(g, GAME_BTN_TOWER, 1); step_scene(g);
    kp(g, GAME_KP_ENTER); step_scene(g); step_scene(g); step_scene(g);
    kp(g, 2);  ASSERT_EQ_INT(request_byte(g), 0xFF); step_scene(g); step_scene(g);
    press(g, GAME_BTN_LBRACKET, 1); ASSERT_EQ_INT(request_byte(g), 0xFF); step_scene(g); step_scene(g);   /* the level keys */
    press(g, GAME_BTN_LBRACKET, 1); ASSERT_EQ_INT(request_byte(g), 0xFF);
    game_destroy(g);
}

TEST(the_external_camera_longs_are_the_negated_camera_position_with_no_eye_offset) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    kp(g, GAME_KP_ENTER);
    const game_scene_t *s = step_scene(g);
    const lists_scan_t *sc = &world_lists((world_t *)game_world(g))->scan;
    ASSERT_EQ_INT(s->camera.view, GAME_VIEW_CHASE);
    ASSERT_EQ_INT(sc->cam_x, -(s->camera.pos.x & 0x3FFFFF));              /* V:4AB1A, V:4AB1E, V:4AB22 */
    ASSERT_EQ_INT(sc->cam_y, -s->camera.pos.y);
    ASSERT_EQ_INT(sc->cam_z, -(s->camera.pos.z & 0x3FFFFF));
    ASSERT_EQ_INT(sc->off_x, (int16_t)(sc->cam_x >> 8));                  /* V:4AB2A, V:4AB2C, V:4AB2E and V:4AB30: those longs >> 8 */
    ASSERT_EQ_INT(sc->off_z, (int16_t)(sc->cam_z >> 8));
    ASSERT_EQ_INT(sc->off_y, sc->cam_y >> 8);
    press(g, GAME_BTN_TOWER, 1);                                           /* the map: the same rule, x and z the aircraft's */
    s = step_scene(g);
    ASSERT_EQ_INT(sc->cam_y, -0x3000000);
    ASSERT_EQ_INT(sc->cam_x, -(s->camera.pos.x & 0x3FFFFF));
    game_destroy(g);
}

/* Note 05 17.14, "The roll word V:4AB4A outside the cockpit": the end of the external branch copies the player's record 0 words (+0x66, +0x68, +0x6A) to the camera longs, so in every external view
 * (the map included) the roll word is the player's roll, not the camera's; the cockpit's is the viewed record's (the camera's). */
TEST(the_roll_word_in_an_external_view_is_the_players_record_0_roll) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    flight_set_position(ac, 273272584, 1000000, 280480032);
    flight_set_attitude(ac, 0, 5000, 5000);
    kp(g, GAME_KP_ENTER);
    const game_scene_t *s = step_scene(g);
    ASSERT_EQ_INT(s->camera.view, GAME_VIEW_CHASE);
    ASSERT_TRUE(ac->f.roll != 0);
    ASSERT_EQ_INT(s->sky.roll_word, ac->f.roll);
    press(g, GAME_BTN_TOWER, 1);
    s = step_scene(g);
    ASSERT_EQ_INT(s->sky.roll_word, ac->f.roll);
    game_destroy(g);
}

TEST(in_the_game_the_step_leaves_the_viewed_cell_to_the_camera_set_up_of_the_pass) {
    /* note 04a 8a (review M3): the records 1 to 15 read the camera's cell of the PREVIOUS pass; the player's step does not write V:4AA04/06 */
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    ASSERT_TRUE(flight_env() && flight_env()->view_cell_by_camera);
    flight_set_position(ac, 273272584, 1000000, 280480032);
    press(g, GAME_BTN_TOWER, 1);
    step_scene(g);
    ac->f.v4aa04 = 7; ac->f.v4aa06 = 9;                 /* the camera's cell of the pass before */
    flight_inputs_t in; memset(&in, 0, sizeof in);
    flight_step(ac, &in);                               /* the player's step alone: it does not touch the word */
    ASSERT_EQ_INT(ac->f.v4aa04, 7); ASSERT_EQ_INT(ac->f.v4aa06, 9);
    game_destroy(g);
}

/* ---- round 3: the aim target point, the snap flag, the hold byte, the zoom reset, the HUD step, the pause keys ----------------------------------------------------- */

TEST(the_aim_target_point_is_the_offset_rotated_by_the_full_matrix_and_shifted_right_4) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    const world_t *w = game_world(g);
    stage(ac, 26360, 2256, 0);   /* the airborne snapshot: matrix 14439, 3930, -6671, 0, 14117, 8316, 7742, -7329, 12441 */
    ac->pos[1] = 30472;
    static const struct { uint8_t cls; int mode; int vz; } cases[] = { { 0x11, 0, 1 }, { 0x11, 6, 5 }, { 0x30, 0, -4 }, { 0x31, 0, -4 }, { 0x3F, 6, -4 }, { 0x14, 6, 5 } };
    for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        ac->f.cls = cases[k].cls;
        world_viewstate_t vs; world_camera_t c;
        vs_init(&vs, -1, 4);
        vs.mode = cases[k].mode; vs.look = -1;   /* snap, so the pair is the target itself */
        ASSERT_EQ_INT(world_camera_build(w, ac, &vs, &c), 0);
        int32_t t[3];
        for (int i = 0; i < 3; i++) t[i] = ac->pos[i] + ((ac->f.mat_b[3 * i + 2] * cases[k].vz) >> 4);   /* the offset (0, 0, vz) through the +0x92 matrix, the ring unit */
        int32_t p, h;
        world_aim_target(&vs, t[0] - c.pos[0], t[1] - c.pos[1], t[2] - c.pos[2], &p, &h);
        ASSERT_EQ_INT(vs.aim_pitch, p); ASSERT_EQ_INT(vs.aim_heading, h);
    }
    game_destroy(g);
}

TEST(every_snap_path_sets_the_first_frame_flag_and_the_modes_limit_does_not) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    const world_t *w = game_world(g);
    stage(ac, 0, 2240, 0); ac->pos[1] = 30472;
    world_viewstate_t vs; world_camera_t c;
    vs_init(&vs, -1, 4);
    vs.look = -1;                                          /* the signed look-at byte 0xFF: snap on every frame, V:4A85E = 1 */
    vs.mode = 5; world_camera_build(w, ac, &vs, &c); ASSERT_EQ_INT(vs.first, 1); ASSERT_EQ_INT(vs.aim_pitch, 720);
    vs_init(&vs, -1, 4); vs.mode = 5;                      /* the limit 560 with V:4A85E clear: no snap, the flag stays 0 */
    vs.aim_pitch = 0; vs.aim_heading = 0;
    world_camera_build(w, ac, &vs, &c); ASSERT_EQ_INT(vs.first, 0); ASSERT_EQ_INT(vs.aim_heading, 560);
    vs_init(&vs, -1, 4); vs.aim_on = 0;                    /* V:4A86D clear snaps and sets the flag */
    world_camera_build(w, ac, &vs, &c); ASSERT_EQ_INT(vs.first, 1);
    vs_init(&vs, -1, 4); vs.first = 1; vs.key_word = 0x7777;   /* a changed key word snaps (flag already set) */
    world_camera_build(w, ac, &vs, &c); ASSERT_EQ_INT(vs.aim_pitch, 720);
    vs_init(&vs, -1, 4);                                   /* mode 0, flag clear: the first-frame snap */
    world_camera_build(w, ac, &vs, &c); ASSERT_EQ_INT(vs.first, 1);
    world_camera_build(w, ac, &vs, &c);                    /* the next frame is a limited step, the flag stays */
    ASSERT_EQ_INT(vs.first, 1); ASSERT_EQ_INT(vs.aim_pitch, 720);
    game_destroy(g);
}

TEST(a_paused_second_does_not_count_the_hold_byte_falls_in_drawn_frames_only_and_zoom_resets_on_the_recentre) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    kp(g, 6);
    ASSERT_EQ_INT(step_scene(g)->camera.head, 1);
    press(g, GAME_BTN_RBRACKET, 1); press(g, GAME_BTN_RBRACKET, 0);                 /* zoom 0x40, hold 5 */
    ASSERT_EQ_INT(step_scene(g)->hud.cp_zoom, 0x40);
    press(g, GAME_BTN_PAUSE, 1);
    for (int i = 0; i < 10; i++) { game_clock_second(g); game_step(g); }            /* ten seconds in a pause: nothing counts */
    press(g, GAME_BTN_PAUSE, 1);
    game_step(g);                                                                   /* the jump of the seconds counts as one change: 5 -> 4 */
    for (int sec = 0; sec < 3; sec++) { ASSERT_EQ_INT(scene(g)->camera.head, 1); game_clock_second(g); game_step(g); }   /* 3, 2, 1 */
    ASSERT_EQ_INT(scene(g)->camera.head, 1);
    ASSERT_EQ_INT(scene(g)->hud.cp_zoom, 0x40);
    game_clock_second(g); game_step(g);                                             /* 0 */
    ASSERT_EQ_INT(scene(g)->camera.head, 1);
    game_step(g);                                                                   /* the state routine recentres before any decrement */
    ASSERT_EQ_INT(scene(g)->camera.head, 0);
    ASSERT_EQ_INT(scene(g)->hud.cp_zoom, 0x80);                                     /* the KP8 path resets the zoom */
    game_destroy(g);
}

TEST(head_keys_and_view_switches_reset_the_zoom_word_to_0x80) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    press(g, GAME_BTN_RBRACKET, 1); ASSERT_EQ_INT(step_scene(g)->hud.cp_zoom, 0x40);
    kp(g, 6); ASSERT_EQ_INT(step_scene(g)->hud.cp_zoom, 0x80);
    press(g, GAME_BTN_RBRACKET, 1); press(g, GAME_BTN_RBRACKET, 1); ASSERT_EQ_INT(step_scene(g)->hud.cp_zoom, 0x20);
    kp(g, 5); ASSERT_EQ_INT(step_scene(g)->hud.cp_zoom, 0x20);                       /* KP5 is not a head key */
    kp(g, GAME_KP_ENTER); ASSERT_EQ_INT(step_scene(g)->hud.cp_zoom, 0x80);           /* the view switch */
    kp(g, GAME_KP_ENTER); step_scene(g);
    press(g, GAME_BTN_RBRACKET, 1); ASSERT_EQ_INT(step_scene(g)->hud.cp_zoom, 0x40);
    press(g, GAME_BTN_TOWER, 1); ASSERT_EQ_INT(step_scene(g)->hud.cp_zoom, 0x80);    /* M in the cockpit: the switch */
    game_destroy(g);
}

TEST(the_hud_step_is_the_table_byte_and_50_only_for_the_equal_row_codes) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    static const int up[9] = { -7, -14, -21, 50, 50, 50, 50, 50, 50 };      /* KP6 from code 0: codes 1 to 9 (4 from 3 and 9 from 8 are equal-row 50s; 5 from 4 is 167 -> 179) */
    for (int k = 0; k < 9; k++) { kp(g, 6); ASSERT_EQ_INT(step_scene(g)->camera.head, k + 1); ASSERT_EQ_INT(scene(g)->camera.hud_step, up[k]); }
    kp(g, 6); ASSERT_EQ_INT(step_scene(g)->camera.hud_step, 14);               /* code 10: row 144, always the table byte */
    kp(g, 6); ASSERT_EQ_INT(step_scene(g)->camera.hud_step, 7);                /* code 11 */
    kp(g, 8); ASSERT_EQ_INT(step_scene(g)->camera.hud_step, 0);                /* code 0 */
    for (int k = 0; k < 5; k++) { kp(g, 6); step_scene(g); }                   /* code 5 (row 179) */
    ASSERT_EQ_INT(scene(g)->camera.head, 5);
    kp(g, 4); ASSERT_EQ_INT(step_scene(g)->camera.head, 4);
    ASSERT_EQ_INT(scene(g)->camera.hud_step, -50);                             /* code 4 from row 179: its table byte -50 */
    kp(g, 4); ASSERT_EQ_INT(step_scene(g)->camera.hud_step, 50);               /* code 3 from code 4: the equal row */
    kp(g, 4); ASSERT_EQ_INT(step_scene(g)->camera.hud_step, -14);              /* code 2 */
    kp(g, GAME_KP_ENTER); ASSERT_EQ_INT(step_scene(g)->camera.hud_step, 50);   /* an external view: 50 */
    kp(g, GAME_KP_ENTER); ASSERT_EQ_INT(step_scene(g)->camera.hud_step, 0);    /* the cockpit: 0 */
    game_destroy(g);
}

TEST(the_keys_through_a_pause_include_h_and_esc_starts_the_esc_flow_inside_the_pause) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    step_scene(g);
    ASSERT_EQ_INT(scene(g)->hud.cp_hud_on, 1);
    press(g, GAME_BTN_PAUSE, 1); step_scene(g);
    press(g, GAME_BTN_HUD_TOGGLE, 1);
    ASSERT_EQ_INT(step_scene(g)->hud.cp_hud_on, 0);                            /* H acts in a pause */
    press(g, GAME_BTN_ESC, 1);
    ASSERT_EQ_INT(scene(g)->mode, GAME_MODE_PAUSED);                           /* the pause byte stays nonzero: the pause is not left at once (capture views-m3-esc-pause) */
    /* from a pause the reset is 4 steps from the key (fade-out 21 blanks = 3, close and pulse = 1): the world is frozen and not redrawn, the last picture fades out */
    for (int i = 0; i < 3; i++) { const game_scene_t *s = step_scene(g); ASSERT_EQ_INT(s->mode, GAME_MODE_PAUSED); ASSERT_EQ_INT(s->flight_blank, 1); ASSERT_EQ_INT(s->fade_target, 0); }
    ASSERT_EQ_INT(step_scene(g)->mode, GAME_MODE_MENU);                         /* the reset of note 08 5.11: the echo screen of the mode (the menu palette), 4 steps from a pause */
    ASSERT_EQ_INT(scene(g)->palette_regime, 0);
    game_destroy(g);
}

TEST(p_during_the_esc_countdown_of_a_pause_only_clears_the_pause_and_the_flight_is_left_on_time) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    step_scene(g);
    press(g, GAME_BTN_PAUSE, 1); step_scene(g);
    press(g, GAME_BTN_ESC, 1);
    ASSERT_EQ_INT(step_scene(g)->mode, GAME_MODE_PAUSED);
    press(g, GAME_BTN_PAUSE, 1);                                               /* 10 blanks (about 1 step) after the key: the pause byte is cleared, nothing else */
    ASSERT_EQ_INT(scene(g)->mode, GAME_MODE_FLIGHT);
    press(g, GAME_BTN_PAUSE, 1);                                               /* a second P: open case, the countdown is neither paused nor restarted */
    ASSERT_EQ_INT(scene(g)->mode, GAME_MODE_FLIGHT);
    for (int i = 0; i < 2; i++) { const game_scene_t *s = step_scene(g); ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT); ASSERT_EQ_INT(s->flight_blank, 1); }
    ASSERT_EQ_INT(step_scene(g)->mode, GAME_MODE_MENU);                        /* the reset 4 steps after the ESC from a pause (note 08 5.11), the echo screen follows (5.11) */
    game_destroy(g);
}

TEST(the_map_altitude_keys_clamp_and_store_the_request_at_the_stop) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    press(g, GAME_BTN_TOWER, 1); step_scene(g);
    static const int32_t up[5] = { 0x5000000, 0x7000000, 0x8000000, 0x8000000, 0x8000000 }, down[6] = { 0x6000000, 0x4000000, 0x2000000, 0x1000000, 0x1000000, 0x1000000 };
    for (int k = 0; k < 5; k++) { press(g, GAME_BTN_LBRACKET, 1); ASSERT_EQ_INT(request_byte(g), 0xFF); ASSERT_EQ_INT(step_scene(g)->camera.pos.y, up[k]); }
    for (int k = 0; k < 6; k++) { press(g, GAME_BTN_RBRACKET, 1); ASSERT_EQ_INT(request_byte(g), 0xFF); ASSERT_EQ_INT(step_scene(g)->camera.pos.y, down[k]); }
    game_destroy(g);
}

/* ---- the writers of the redraw counter R (V:4A8EF; note 07 12.1) ---------------------------------------------------------------------- */

static unsigned rr(game_t *g) { game_step(g); return scene(g)->hud.cp_redraw; }

TEST(r_is_3_from_the_debug_starts_jolt_counts_down_and_is_not_reloaded_by_a_pass_with_no_writer) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    ASSERT_EQ_INT(rr(g), 3);                               /* (the carrier start's first passes run the jolt, whose end reloads R: the run of 3s ends by itself) */
    for (int i = 0; i < 10; i++) game_step(g);
    ASSERT_EQ_INT(scene(g)->hud.cp_redraw, 0);
    ASSERT_EQ_INT(rr(g), 0); ASSERT_EQ_INT(rr(g), 0);
    game_destroy(g);
}

TEST(the_head_keys_run_the_helper_unless_the_equal_row_test_takes_its_50) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 12; i++) game_step(g);
    kp(g, 6);                                             /* head code 1 (row 144): the tail ends in 0x1E7B2 */
    ASSERT_EQ_INT(rr(g), 3);
    for (int i = 0; i < 4; i++) game_step(g);
    kp(g, 2);                                             /* code 6 (row 179, the present row 144): the table byte, 0x1E7B2 */
    ASSERT_EQ_INT(rr(g), 3);
    for (int i = 0; i < 4; i++) game_step(g);
    ASSERT_EQ_INT(scene(g)->camera.head, 6);
    kp(g, 4);                                             /* code 5 (row 179 again): the equal-row test takes 50 and returns before 0x1E7B2 */
    ASSERT_EQ_INT(rr(g), 0);
    ASSERT_EQ_INT(scene(g)->camera.head, 5);
    ASSERT_EQ_INT(scene(g)->camera.hud_step, 50);
    kp(g, 6); kp(g, 6);                                    /* code 6 and 7 (rows 179 and 179): no writer either */
    ASSERT_EQ_INT(rr(g), 0);
    kp(g, 8);                                              /* code 0 (row 144): 0x1E7B2 */
    ASSERT_EQ_INT(rr(g), 3);
    game_destroy(g);
}

TEST(the_view_switch_and_the_m_key_run_the_helper_and_the_external_view_counts_down_while_the_panel_counter_lasts) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 9; i++) game_step(g);              /* (the carrier start's jolt reloads R in the first passes; the periodic writer of the 32nd pass comes later) */
    kp(g, GAME_KP_ENTER);                                  /* the view switch 0x142BC */
    ASSERT_EQ_INT(rr(g), 3);
    ASSERT_TRUE(scene(g)->camera.view != GAME_VIEW_COCKPIT);
    ASSERT_EQ_INT(scene(g)->hud.panel_block, 1);           /* the scene says the panel block ran in this external-view pass (the frontend draws the panel from it) */
    ASSERT_EQ_INT(rr(g), 2); ASSERT_EQ_INT(rr(g), 1);      /* the panel block runs in the external view while V:4A8EE (3 after the helper) lasts: R counts down in those passes (note 07 12.1) */
    ASSERT_EQ_INT(rr(g), 0);                               /* then the external block 0x113F8 runs: no count */
    ASSERT_EQ_INT(scene(g)->hud.panel_block, 0);
    kp(g, GAME_KP_ENTER);                                  /* back to the cockpit: the helper again, then the count down */
    ASSERT_EQ_INT(rr(g), 3); ASSERT_EQ_INT(rr(g), 2);
    game_destroy(g);
}

TEST(the_first_pass_of_a_flight_has_no_writer_of_r_and_the_paused_pass_counts_nothing_down) {
    char err[128];
    game_t *g = game_create(&loader, err, sizeof err);
    ASSERT_TRUE(g != NULL);
    game_debug_start_flight(g, 0, 0);
    for (int i = 0; i < 12; i++) game_step(g);             /* (the jolt's end reloads R in the first passes, as the carrier start's does; no flight-start writer is left) */
    ASSERT_EQ_INT(rr(g), 0);                               /* V:4A8EF holds what the passes left: 0 */
    ASSERT_EQ_INT(rr(g), 0);
    kp(g, 6);                                              /* a head key runs the helper: R = 3 */
    ASSERT_EQ_INT(rr(g), 3);
    press(g, GAME_BTN_PAUSE, 1);                           /* the pause screen: the gate V:4A84D = 0, the frame routine skips 0x1114A to 0x114BE */
    ASSERT_EQ_INT(scene(g)->mode, GAME_MODE_PAUSED);
    for (int i = 0; i < 6; i++) game_step(g);
    ASSERT_EQ_INT(scene(g)->hud.cp_redraw, 3);             /* no decrement, no periodic writer, no map call in the paused passes */
    press(g, GAME_BTN_PAUSE, 1);
    ASSERT_EQ_INT(rr(g), 2);                               /* the first pass after the resume goes on counting (the resume restore is skipped: V:4A994 is 0) */
    game_destroy(g);
}

TEST(the_frame_routine_sets_r_every_32nd_pass_while_the_camera_mode_is_0) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    int hits = 0;
    for (int i = 0; i < 100; i++) {
        game_step(g);
        const uint32_t n = scene(g)->hud.cp_n;           /* the value the draws of the pass saw */
        const unsigned r = scene(g)->hud.cp_redraw;
        if (i >= 4 && (n & 0x1F) == 0x11) { ASSERT_EQ_INT(r, 3); hits++; }   /* the pass after the one that saw n & 0x1F = 0x10 */
        if (i >= 4 && (n & 0x1F) == 0x12) ASSERT_EQ_INT(r, 2);
        if (i >= 4 && (n & 0x1F) == 0x10) ASSERT_EQ_INT(r, 0);
    }
    ASSERT_TRUE(hits >= 2);
    game_destroy(g);
}

TEST(the_map_view_publishes_the_marks_the_map_routine_drew_in_the_pass_and_none_elsewhere) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 12; i++) game_step(g);
    ASSERT_EQ_INT(scene(g)->hud.mark_count, 0);                       /* the cockpit view: the map routine does not run */
    press(g, GAME_BTN_TOWER, 1);
    game_step(g); game_step(g);
    const game_scene_t *s = scene(g);
    ASSERT_EQ_INT(s->camera.view, GAME_VIEW_TOWER);
    int ships = 0;
    for (int i = 0; i < s->hud.mark_count; i++) if (s->hud.marks[i].shape == 16) { ships++; ASSERT_EQ_INT(s->hud.marks[i].pen, 3); ASSERT_EQ_INT(s->hud.marks[i].rec, 14); }
    ASSERT_EQ_INT(ships, 1);                                          /* the carrier's ship shape: V:4A90F is 1 in flight, the ship needs no V:4A993 bit */
    game_step(g);                                                     /* ... and on the next frame too (odd and even) */
    s = scene(g);
    ships = 0;
    for (int i = 0; i < s->hud.mark_count; i++) ships += s->hud.marks[i].shape == 16;
    ASSERT_EQ_INT(ships, 1);
    press(g, GAME_BTN_TOWER, 1);                                      /* M in the map returns */
    game_step(g); game_step(g);
    ASSERT_EQ_INT(scene(g)->hud.mark_count, 0);
    game_destroy(g);
}

int main(int argc, char **argv) {
    root = argc > 1 ? argv[1] : "assets";
    char err[128];
    if (flight_tables_load(&TAB, &loader, err, sizeof err) != 0) { printf("test_views: skipped (%s)\n", err); return 0; }
    printf("test_views:\n");
    RUN(r_is_3_from_the_debug_starts_jolt_counts_down_and_is_not_reloaded_by_a_pass_with_no_writer);
    RUN(the_head_keys_run_the_helper_unless_the_equal_row_test_takes_its_50);
    RUN(the_view_switch_and_the_m_key_run_the_helper_and_the_external_view_counts_down_while_the_panel_counter_lasts);
    RUN(the_first_pass_of_a_flight_has_no_writer_of_r_and_the_paused_pass_counts_nothing_down);
    RUN(the_frame_routine_sets_r_every_32nd_pass_while_the_camera_mode_is_0);
    RUN(the_map_view_publishes_the_marks_the_map_routine_drew_in_the_pass_and_none_elsewhere);
    RUN(the_ring_offset_is_the_matrix_sum_shifted_right_4_in_units_of_1024);
    RUN(directions_0_and_4_follow_pitch_and_roll_the_other_eight_use_the_heading_matrix);
    RUN(the_aim_target_quadrants_and_the_precision_bands);
    RUN(the_step_moves_a_quarter_of_the_difference_the_short_way_round_capped_by_the_limit);
    RUN(the_first_external_frame_snaps_then_a_quarter_step_with_a_snap_on_a_new_key_or_without_look_at);
    RUN(head_views_compose_the_head_triple_with_the_aircraft_attitude);
    RUN(the_head_triples_and_the_level_test_of_codes_12_and_13);
    RUN(a_head_key_in_the_game_composes_the_camera_and_the_pitch_gate_reads_the_composed_pitch);
    RUN(a_head_view_is_momentary_the_hold_runs_down_by_the_timer_second_and_kp5_locks_it);
    RUN(kp_enter_is_a_request_bit_two_presses_in_one_pass_are_one_toggle);
    RUN(the_first_external_frame_after_every_kp_enter_snaps);
    RUN(the_keys_are_processed_while_paused_the_world_is_not_redrawn_and_the_switch_waits);
    RUN(the_pointer_and_the_axes_keep_summing_while_paused_and_n_keeps_its_low_bits);
    RUN(the_bracket_keys_zoom_in_the_cockpit_step_the_level_outside_and_the_altitude_in_the_map);
    RUN(only_m_saves_the_map_altitude_and_m_or_kp_enter_in_the_map_return_to_the_cockpit);
    RUN(the_map_camera_follows_the_aircraft_every_frame);
    RUN(the_requests_follow_the_original_sites);
    RUN(the_external_camera_longs_are_the_negated_camera_position_with_no_eye_offset);
    RUN(the_roll_word_in_an_external_view_is_the_players_record_0_roll);
    RUN(in_the_game_the_step_leaves_the_viewed_cell_to_the_camera_set_up_of_the_pass);
    RUN(the_aim_target_point_is_the_offset_rotated_by_the_full_matrix_and_shifted_right_4);
    RUN(every_snap_path_sets_the_first_frame_flag_and_the_modes_limit_does_not);
    RUN(a_paused_second_does_not_count_the_hold_byte_falls_in_drawn_frames_only_and_zoom_resets_on_the_recentre);
    RUN(head_keys_and_view_switches_reset_the_zoom_word_to_0x80);
    RUN(the_hud_step_is_the_table_byte_and_50_only_for_the_equal_row_codes);
    RUN(the_keys_through_a_pause_include_h_and_esc_starts_the_esc_flow_inside_the_pause);
    RUN(p_during_the_esc_countdown_of_a_pause_only_clears_the_pause_and_the_flight_is_left_on_time);
    RUN(the_map_altitude_keys_clamp_and_store_the_request_at_the_stop);
    TEST_MAIN_END();
}
