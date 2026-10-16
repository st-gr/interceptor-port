/* tests/test_weapons.c - the gun, the missiles, the countermeasures, the contact scan and the hull test (src/core/weapons.c, docs/re/07-ai-and-weapons.md Part B, "note 07";
 * section numbers below are that note's).
 *
 * The values are the note's; the fixtures are invented and text-free: a player record, a few target records, a hull list of six planes around the origin. The differentials against
 * the original's code are tests/rig/test_weapons_diff.c (make -C tools/m68k-rig test-weapons). */
#include "test.h"
#include "ai_tables.h"
#include "sound_synth.h"
#include "../src/core/weapons.h"
#include "../src/core/logbook.h"
#include "../src/core/flight.h"
#include "../src/core/flight_int.h"
#include "../src/core/world.h"

static templates_t TPL;
static scenario_t SC;
static entities_t E;
static aircraft_t P;
static weapons_t W;
static flight_tables_t TABLES;
static flight_env_t ENV;
static unsigned LB[256];
static int POSTS[16], NPOSTS;

static void h_count(void *u, int off, unsigned d) { (void)u; LB[off & 255] += d; }
static void h_post(void *u, uint16_t code) { (void)u; if (NPOSTS < 16) POSTS[NPOSTS++] = code; }

/* the player: selector in the id's high nibble, the gear byte's low nibble 0 (up), the fire reservoir 5 */
static void setup(int sel) {
    memset(&TPL, 0, sizeof TPL);
    TPL.count = TEMPLATE_COUNT;
    memset(&SC, 0, sizeof SC);
    memset(&P, 0, sizeof P);
    P.f.cls = 0x11; P.f.w0 = 0x11C8; P.f.v4a984 = 0x0840; P.f.v4a844 = 1; P.f.v4a847 = 1;
    P.f.id = (uint8_t)(sel << 4 | 0x0B); P.f.gear = 0;
    P.f.speed = P.f.speed_target = 0x2263;
    P.pos[0] = P.pos[2] = 64 * 4194304; P.pos[1] = 0x300000;   /* airborne: a tracer does not meet the ground */
    flight_set_attitude(&P, 0, 0, 0);
    entities_init(&E, &TPL, &AI_FIXTURE, &SC);
    entities_attach_player(&E, &P);
    E.mode = 3; E.flying = 1; E.sim_on = 1;
    memset(&TABLES, 0, sizeof TABLES);
    memset(&ENV, 0, sizeof ENV);
    ENV.tab = &TABLES; ENV.landmark_z = 1; ENV.no_rearm = 1; ENV.no_sound_system = 1; ENV.no_liftoff_hook = 1;
    flight_set_env(&ENV);
    weapons_init(&W, &E);
    W.hook.count = h_count; W.hook.post = h_post;
    weapons_mission_start(&W);
    memset(LB, 0, sizeof LB);
    NPOSTS = 0;
}

/* a valid, counted aircraft record (class 0x12, group = its index) at an absolute position */
static aircraft_t *target(int idx, int32_t x, int32_t y, int32_t z) {
    aircraft_t *a = entities_ac(&E, idx);
    memset(a, 0, sizeof *a);
    a->f.cls = 0x12; a->f.w0 = 0x50C1; a->f.group = (uint8_t)idx;
    a->pos[0] = x; a->pos[1] = y; a->pos[2] = z;
    flight_set_attitude(a, 0, 0, 0);
    return a;
}

/* ---- the keys (7.1, 9.1) ------------------------------------------------------------------------------------------------------------------- */

TEST(the_fire_key_holds_the_trigger_for_the_gun_and_requests_a_launch_for_a_missile_and_nothing_with_the_gear_down) {
    setup(1);
    weapons_key_fire(&W, 1);
    ASSERT_EQ_INT(W.trigger, 1);
    weapons_key_fire(&W, 0);
    ASSERT_EQ_INT(W.trigger, 0);
    P.f.id = 0x2B;
    weapons_key_fire(&W, 1);
    ASSERT_EQ_INT(E.req_player_launch, 1);
    E.req_player_launch = 0;
    P.f.id = 0x3B;
    weapons_key_fire(&W, 1);
    ASSERT_EQ_INT(E.req_player_launch, 1);
    E.req_player_launch = 0;
    P.f.gear = 0x01;   /* any bit of the low nibble: no press has an effect */
    weapons_key_fire(&W, 1);
    ASSERT_EQ_INT(E.req_player_launch, 0);
}

TEST(the_select_key_cycles_3_2_1_0_3_and_keeps_the_low_nibble) {
    setup(3);
    const int want[5] = { 2, 1, 0, 3, 2 };
    for (int i = 0; i < 5; i++) {
        weapons_key_select(&W);
        ASSERT_EQ_INT(P.f.id >> 4, want[i]);
        ASSERT_EQ_INT(P.f.id & 15, 0x0B);
    }
}

TEST(a_flare_press_posts_0x4026_sets_the_request_and_the_30_pass_cool_down_and_the_last_flare_posts_0x4027) {
    setup(1);
    weapons_key_flare(&W);
    ASSERT_EQ_INT(W.flares, 15);
    ASSERT_EQ_INT(W.cm_req, 2);
    ASSERT_EQ_INT(P.f.v4a907, 30);
    ASSERT_EQ_INT(POSTS[0], 0x4026);
    W.flares = 1; W.cm_req = 0; NPOSTS = 0;
    weapons_key_flare(&W);
    ASSERT_EQ_INT(W.flares, 0);
    ASSERT_EQ_INT(W.cm_req, 0);
    ASSERT_EQ_INT(POSTS[0], 0x4027);
}

TEST(a_chaff_press_posts_0x4028_and_the_last_one_0x4029) {
    setup(1);
    weapons_key_chaff(&W);
    ASSERT_EQ_INT(W.chaff, 15);
    ASSERT_EQ_INT(W.cm_req, 1);
    ASSERT_EQ_INT(P.f.v4a906, 30);
    ASSERT_EQ_INT(POSTS[0], 0x4028);
    W.chaff = 1; W.cm_req = 0; NPOSTS = 0;
    weapons_key_chaff(&W);
    ASSERT_EQ_INT(POSTS[0], 0x4029);
}

TEST(the_cool_downs_count_down_once_per_unpaused_pass) {
    setup(1);
    P.f.v4a906 = 3; P.f.v4a907 = 2;
    weapons_world_start(&W, 0);
    ASSERT_EQ_INT(P.f.v4a906, 2);
    ASSERT_EQ_INT(P.f.v4a907, 1);
    weapons_world_start(&W, 1);
    ASSERT_EQ_INT(P.f.v4a906, 2);
    weapons_world_start(&W, 0); weapons_world_start(&W, 0); weapons_world_start(&W, 0);
    ASSERT_EQ_INT(P.f.v4a906, 0);
    ASSERT_EQ_INT(P.f.v4a907, 0);
}

TEST(the_ecm_key_toggles_the_switch) {
    setup(1);
    weapons_key_ecm(&W);
    ASSERT_EQ_INT(P.f.v4a96d, 1);
    weapons_key_ecm(&W);
    ASSERT_EQ_INT(P.f.v4a96d, 0);
}

/* ---- the gun (7.3 to 7.5) ----------------------------------------------------------------------------------------------------------------- */

TEST(the_gun_fires_a_burst_of_five_three_rounds_and_three_log_counts_each_then_waits_for_the_reload) {
    setup(1);
    W.trigger = 1;
    for (int i = 0; i < 5; i++) weapons_gun(&W, (uint32_t)i, 0);
    ASSERT_EQ_INT(E.rec[0].rounds, 500 - 15);
    ASSERT_EQ_INT(LB[LB_CANNON_FIRED], 15);
    ASSERT_EQ_INT(P.f.l7d >> 4, 0);                     /* the reservoir is empty */
    ASSERT_EQ_INT(W.n_shots, 5);
    weapons_gun(&W, 5, 0);                              /* the sixth call: the reservoir is empty and the timer runs */
    ASSERT_EQ_INT(E.rec[0].rounds, 485);
    ASSERT_EQ_INT(W.n_shots, 5);
}

TEST(the_reservoir_is_refilled_to_five_when_the_trigger_is_released_and_the_timer_has_run_out) {
    setup(1);
    W.trigger = 1;
    for (int i = 0; i < 5; i++) weapons_gun(&W, (uint32_t)i, 0);
    W.trigger = 0;
    for (int i = 0; i < 40; i++) weapons_gun(&W, (uint32_t)(5 + i), 0);
    ASSERT_EQ_INT(P.f.l7d >> 4, 5);
    ASSERT_EQ_INT(W.reload, 0);
    W.trigger = 1;
    weapons_gun(&W, 50, 0);
    ASSERT_EQ_INT(W.n_shots, 6);
    ASSERT_EQ_INT(E.rec[0].rounds, 500 - 18);
}

TEST(the_last_rounds_are_taken_to_zero_and_with_no_round_left_nothing_is_fired) {
    setup(1);
    E.rec[0].rounds = 2;
    W.trigger = 1;
    weapons_gun(&W, 0, 0);
    ASSERT_EQ_INT(E.rec[0].rounds, 0);
    ASSERT_EQ_INT(W.n_shots, 1);
    weapons_gun(&W, 1, 0);
    ASSERT_EQ_INT(W.n_shots, 1);
}

TEST(a_paused_pass_neither_fires_nor_ages_the_tracers) {
    setup(1);
    W.trigger = 1;
    weapons_gun(&W, 0, 0);
    const int16_t life = W.slot[0].life;
    weapons_gun(&W, 1, 1);
    ASSERT_EQ_INT(W.n_shots, 1);
    ASSERT_EQ_INT(W.slot[0].life, life);
}

TEST(a_flare_request_takes_a_pool_slot_with_the_flare_flag_and_counts_three_log_rounds_without_using_rounds) {
    setup(1);
    W.cm_req = 2;
    weapons_gun(&W, 0, 0);
    ASSERT_TRUE((W.slot[0].flags & WPF_FLARE) != 0);
    ASSERT_EQ_INT(W.flare_ref, 1);
    ASSERT_EQ_INT(W.cm_req, 0);
    ASSERT_EQ_INT(E.rec[0].rounds, 500);
    W.cm_req = 1;
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE((W.slot[2].flags & WPF_CHAFF) != 0);
    ASSERT_EQ_INT(W.chaff_ref, 3);
}

TEST(a_tracer_is_deactivated_in_the_step_after_its_life_has_run_out) {
    setup(1);
    W.trigger = 1;
    weapons_gun(&W, 0, 0);
    W.trigger = 0;
    ASSERT_TRUE((W.slot[0].flags & WPF_ACTIVE) != 0);
    for (int i = 0; i < 100 && (W.slot[0].flags & WPF_ACTIVE); i++) weapons_gun(&W, (uint32_t)(i + 1), 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_ACTIVE));
}

/* ---- the hull test (8.2a) ----------------------------------------------------------------------------------------------------------------- */

/* six planes of form A around a cube of half-size 100: the point vertex a, the normal vertex c minus vertex b */
static ai_t hull_table(void) {
    ai_t t = AI_FIXTURE;
    static const int16_t faces[6][3] = { { 0, 1, 2 }, { 0, 1, 3 }, { 0, 1, 4 }, { 5, 2, 1 }, { 5, 3, 1 }, { 5, 4, 1 } };
    memset(t.hull, 0, sizeof t.hull);
    for (int f = 0; f < 6; f++) for (int k = 0; k < 3; k++) t.hull[0][f][k] = (int16_t)(6 * faces[f][k]);
    t.hull[0][6][0] = (int16_t)0x8000;
    return t;
}
static void hull_vertices(int16_t v[17][3]) {
    memset(v, 0, 17 * 3 * sizeof(int16_t));
    v[0][0] = v[0][1] = v[0][2] = -100;
    v[5][0] = v[5][1] = v[5][2] = 100;
    v[2][0] = 200; v[3][1] = 200; v[4][2] = 200;
}

TEST(a_point_inside_all_six_planes_is_a_hit_and_one_outside_any_plane_is_not) {
    const ai_t t = hull_table();
    int16_t v[17][3];
    hull_vertices(v);
    flight_state_t rf; memset(&rf, 0, sizeof rf);
    const int32_t fine[3] = { 0, 0, 0 };
    const int32_t in[3] = { 0, 0, 0 }, edge[3] = { 100, 0, 0 }, out_x[3] = { 101, 0, 0 }, out_y[3] = { 0, -101, 0 }, out_z[3] = { 0, 0, 150 };
    ASSERT_EQ_INT(weapons_hull_inside(&t, &rf, 0, v, fine, in), 1);
    ASSERT_EQ_INT(weapons_hull_inside(&t, &rf, 0, v, fine, edge), 1);   /* on the plane: the dot product is 0, only a negative one rejects */
    ASSERT_EQ_INT(weapons_hull_inside(&t, &rf, 0, v, fine, out_x), 0);
    ASSERT_EQ_INT(weapons_hull_inside(&t, &rf, 0, v, fine, out_y), 0);
    ASSERT_EQ_INT(weapons_hull_inside(&t, &rf, 0, v, fine, out_z), 0);
}

TEST(the_hull_moves_with_the_records_fine_position_and_a_part_id_with_bit_7_is_never_a_hit) {
    const ai_t t = hull_table();
    int16_t v[17][3];
    hull_vertices(v);
    flight_state_t rf; memset(&rf, 0, sizeof rf);
    const int32_t fine[3] = { 1000, 0, 0 }, near_it[3] = { 1050, 0, 0 }, far_it[3] = { 0, 0, 0 };
    ASSERT_EQ_INT(weapons_hull_inside(&t, &rf, 0, v, fine, near_it), 1);
    ASSERT_EQ_INT(weapons_hull_inside(&t, &rf, 0, v, fine, far_it), 0);
    ASSERT_EQ_INT(weapons_hull_inside(&t, &rf, 0x80, v, fine, near_it), 0);
}

/* ---- the missile launch (7.6) and the gate (8.1) ------------------------------------------------------------------------------------------ */

TEST(a_launched_record_carries_the_owners_map_bytes_in_the_first_0xa4_bytes) {
    setup(3);
    E.rec[0].b70 = 0x4C; E.rec[0].b71 = 0x07;
    E.rec[1].b70 = 0x11; E.rec[1].b71 = 0xFF;
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);
    ASSERT_EQ_INT(E.rec[1].b70, 0x4C); ASSERT_EQ_INT(E.rec[1].b71, 0x07);   /* the copy of the owner's 0xA4 bytes (note 07 1.4, +0x71) */
}

TEST(an_aim_120_launch_takes_a_long_range_stock_gives_class_1_life_200_and_requests_0x8c) {
    setup(3);
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);
    aircraft_t *m = entities_ac(&E, 1);
    ASSERT_EQ_INT(E.rec[0].stock, 0x23);
    ASSERT_EQ_INT(m->f.cls, 1);
    ASSERT_EQ_INT(m->f.counter, 200);
    ASSERT_EQ_INT(E.rec[1].dest_cx, -1);
    ASSERT_EQ_INT(E.request, 0x8C);
    ASSERT_EQ_INT(LB[LB_AIM120_FIRED], 1);
    ASSERT_EQ_INT(E.launch_flag, 1);
    ASSERT_TRUE((m->f.w0 & 0x10C0) == 0x10C0);
    ASSERT_TRUE(!(m->f.w2 & 0x0004));
}

TEST(an_aim_9_launch_takes_a_short_range_stock_gives_class_0_life_150) {
    setup(2);
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);
    aircraft_t *m = entities_ac(&E, 1);
    ASSERT_EQ_INT(E.rec[0].stock, 0x14);
    ASSERT_EQ_INT(m->f.cls, 0);
    ASSERT_EQ_INT(m->f.counter, 150);
    ASSERT_EQ_INT(LB[0x3E] + LB[LB_AIM120_FIRED], 0);
}

TEST(a_launch_with_no_stock_of_the_selected_kind_is_refused) {
    setup(3);
    E.rec[0].stock = 0x20;
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 0);
    ASSERT_EQ_INT(E.rec[0].stock, 0x20);
    ASSERT_EQ_INT(W.n_refused, 1);
    setup(2);
    E.rec[0].stock = 0x04;
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 0);
}

TEST(the_launch_pass_already_decrements_the_life_and_boosts_the_speed_word_by_0x1e0_toward_the_designated_target) {
    setup(3);
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);
    aircraft_t *m = entities_ac(&E, 1);
    target(10, m->pos[0] + 0x40000, m->pos[1], m->pos[2]);
    E.designated = 10;
    ASSERT_EQ_INT(weapons_gate(&W, 1), 1);
    ASSERT_EQ_INT(m->f.counter, 199);
    ASSERT_EQ_INT(m->f.speed_target, 0x2263 + 0x1E0);   /* 0x2443: the demo's AIM-120 at its launch pass (8.9) */
    ASSERT_EQ_INT(E.rec[1].target, 0x8A);               /* the live retarget names the designation */
    ASSERT_EQ_INT(weapons_gate(&W, 1), 1);
    ASSERT_EQ_INT(m->f.speed_target, 0x2263 + 2 * 0x1E0);
}

TEST(without_a_designation_the_player_missile_also_steps_toward_0x4200_by_0x240_and_aims_at_no_height) {
    setup(3);
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);
    aircraft_t *m = entities_ac(&E, 1);
    E.designated = -1;
    ASSERT_EQ_INT(weapons_gate(&W, 1), 1);
    ASSERT_EQ_INT(m->f.speed_target, 0x2263 + 0x1E0 + 0x240);
    ASSERT_EQ_INT(E.rec[1].dest_h, 0);
}

TEST(the_boost_stops_at_0x4200) {
    setup(3);
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);
    aircraft_t *m = entities_ac(&E, 1);
    for (int i = 0; i < 20; i++) weapons_gate(&W, 1);
    ASSERT_EQ_INT(m->f.speed_target, 0x4200);
}

TEST(a_missile_is_freed_by_the_150th_gate_call_of_a_sidewinder_with_the_display_request_0x0c) {
    setup(2);
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);
    aircraft_t *m = entities_ac(&E, 1);
    E.request = 0;
    for (int i = 1; i < 150; i++) ASSERT_EQ_INT(weapons_gate(&W, 1), 1);
    ASSERT_TRUE((m->f.w0 & 0x40) != 0);
    ASSERT_EQ_INT(weapons_gate(&W, 1), 0);
    ASSERT_EQ_INT(m->f.w0, 0);
    ASSERT_EQ_INT(E.request, 0x0C);
}

TEST(below_a_life_of_50_the_missile_coasts_toward_0x3000_and_below_12_it_is_marked_dying) {
    setup(2);
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);
    aircraft_t *m = entities_ac(&E, 1);
    m->f.counter = 0x32;
    m->f.speed_target = 0x4200;
    ASSERT_EQ_INT(weapons_gate(&W, 1), 1);   /* the decrement makes it 49 */
    ASSERT_EQ_INT(m->f.counter, 49);
    ASSERT_TRUE(m->f.speed_target < 0x4200);
    m->f.counter = 0x0D;
    ASSERT_EQ_INT(weapons_gate(&W, 1), 1);
    ASSERT_EQ_INT(m->f.counter, 0x0C);
    ASSERT_TRUE(!(m->f.w0 & 0x0400));
    ASSERT_EQ_INT(weapons_gate(&W, 1), 1);
    ASSERT_TRUE((m->f.w0 & 0x0400) != 0);
}

TEST(a_gate_for_an_invalid_record_does_nothing_and_a_paused_world_skips_the_gate) {
    setup(2);
    ASSERT_EQ_INT(weapons_gate(&W, 1), 0);
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);
    E.paused = 1;
    ASSERT_EQ_INT(weapons_gate(&W, 1), 0);
    ASSERT_EQ_INT(entities_ac(&E, 1)->f.counter, 150);
}

/* ---- the contact scan and the damage (10.2, 10.3) ------------------------------------------------------------------------------------------ */

TEST(an_aim_120_inside_the_inner_box_of_a_counted_hostile_marks_it_crashed_and_out_of_control_and_counts_the_hit) {
    setup(3);
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);
    aircraft_t *m = entities_ac(&E, 1);
    aircraft_t *t = target(10, m->pos[0] + 0x1000, m->pos[1], m->pos[2]);
    const int32_t prev[3] = { m->pos[0], m->pos[1], m->pos[2] };
    ASSERT_EQ_INT(weapons_contact(&W, &m->f, prev, 0), 0x40);
    ASSERT_TRUE((t->f.w0 & 0x0200) != 0);
    ASSERT_TRUE((t->f.b20 & 0x02) != 0);
    ASSERT_TRUE((t->f.w2 & 0x1000) != 0);
    ASSERT_TRUE((t->f.w2 & 0x0001) != 0);   /* the proximity mark of the outer box */
    ASSERT_EQ_INT(t->f.state, 1);
    ASSERT_EQ_INT(LB[LB_AIM120_HITS], 1);
    ASSERT_EQ_INT(LB[LB_AIM120_HITS + 4], 0);
}

TEST(a_target_outside_the_outer_box_is_untouched_and_between_the_boxes_only_the_proximity_mark_is_set) {
    setup(3);
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);
    aircraft_t *m = entities_ac(&E, 1);
    const int32_t prev[3] = { m->pos[0], m->pos[1], m->pos[2] };
    aircraft_t *t = target(10, m->pos[0] + 0x20000, m->pos[1], m->pos[2]);
    ASSERT_EQ_INT(weapons_contact(&W, &m->f, prev, 0), 0);
    ASSERT_EQ_INT(t->f.w2 & 1, 0);
    t->pos[0] = m->pos[0] + 0x8000;   /* inside 0x10000, outside 0x5000 */
    ASSERT_EQ_INT(weapons_contact(&W, &m->f, prev, 0), 0);
    ASSERT_EQ_INT(t->f.w2 & 1, 1);
    ASSERT_EQ_INT(t->f.w0 & 0x0200, 0);
    ASSERT_EQ_INT(m->f.v4a957, 1);
}

TEST(the_cruise_missile_has_the_small_boxes_0x5000_and_0x1000) {
    setup(3);
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);
    aircraft_t *m = entities_ac(&E, 1);
    const int32_t prev[3] = { m->pos[0], m->pos[1], m->pos[2] };
    aircraft_t *t = target(10, m->pos[0] + 0x3000, m->pos[1], m->pos[2]);
    t->f.cls = 0x15;
    ASSERT_EQ_INT(weapons_contact(&W, &m->f, prev, 0), 0);
    ASSERT_EQ_INT(t->f.w2 & 1, 1);
    t->pos[0] = m->pos[0] + 0x1000;
    ASSERT_EQ_INT(weapons_contact(&W, &m->f, prev, 0), 0x40);
}

TEST(a_hit_on_a_record_of_the_same_group_or_an_uncounted_pair_is_ignored) {
    setup(3);
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);
    aircraft_t *m = entities_ac(&E, 1);
    const int32_t prev[3] = { m->pos[0], m->pos[1], m->pos[2] };
    aircraft_t *t = target(10, m->pos[0], m->pos[1], m->pos[2]);
    t->f.group = m->f.group;
    ASSERT_EQ_INT(weapons_contact(&W, &m->f, prev, 0), 0);
    t->f.group = 10;
    m->f.w0 &= (uint16_t)~0x0008; t->f.w0 &= (uint16_t)~0x0008;   /* neither is the player's side: the pair is not scanned */
    ASSERT_EQ_INT(weapons_contact(&W, &m->f, prev, 0), 0);
}

TEST(a_dying_or_crashed_record_is_not_hit_again) {
    setup(3);
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);
    aircraft_t *m = entities_ac(&E, 1);
    const int32_t prev[3] = { m->pos[0], m->pos[1], m->pos[2] };
    aircraft_t *t = target(10, m->pos[0], m->pos[1], m->pos[2]);
    t->f.w0 |= 0x0400;
    ASSERT_EQ_INT(weapons_contact(&W, &m->f, prev, 0), 0);
    t->f.w0 = (uint16_t)((t->f.w0 & ~0x0400) | 0x0200);
    ASSERT_EQ_INT(weapons_contact(&W, &m->f, prev, 0), 0);
}

/* ---- the trail and the jolt (13.1, 13.3) --------------------------------------------------------------------------------------------------- */

TEST(the_jolt_row_offset_is_zero_without_a_jolt_and_the_machine_stays_off_without_a_kind) {
    setup(1);
    ASSERT_EQ_INT(weapons_jolt_pass(&W, &P.f, 0), 0);
    ASSERT_EQ_INT(W.jolt_row, 0);
}

TEST(the_gun_shot_plays_the_cannon_burst_through_the_sound_module_unless_the_map_byte_is_set) {
    static sound_t SND;
    sound_table_t st;
    setup(1);
    sound_synth_table(&st);
    sound_init(&SND, &st);
    W.sound = &SND;
    W.trigger = 1;
    weapons_gun(&W, 0, 0);
    ASSERT_EQ_INT(sound_slot(&SND, 2), 0xA);                      /* 0x1A2F0(6, 0x16): the cannon sound on voice 2 (note 10 8.2) */
    sound_blank(&SND);
    ASSERT_EQ_INT(SND.v[2].vol, 0x16);
    sound_init(&SND, &st);
    W.snd_off = 1; W.reload = 0; W.trigger = 0; weapons_gun(&W, 1, 0); W.trigger = 1; weapons_gun(&W, 2, 0);
    ASSERT_EQ_INT(sound_slot(&SND, 2), SND_NONE);                 /* the map view byte mutes it */
    W.snd_off = 0; W.ext_view = 1;
    sound_init(&SND, &st);
}


/* ---- the paused flag (one flag: the pause screen or the pause byte V:4A866 of a script's count) --------------------------------------------------- */

static wp_qentry_t QE[32];
static int QN;
static int q_hook(void *u, wp_qentry_t *out, int max) { (void)u; int n = QN < max ? QN : max; for (int i = 0; i < n; i++) out[i] = QE[i]; return n; }
static int16_t SLOT_V[WP_VSLOTS][3];
static int slots_hook(void *u, int rec, int16_t (*out)[3], int n) { (void)u; (void)rec; memcpy(out, SLOT_V, sizeof(int16_t) * 3 * (size_t)n); return 0; }

/* the record `idx` standing at the player's cell with its fine position, the tracer in slot 0 at the fine point (fx, fy, fz) of that cell, at rest */
static aircraft_t *rec_at(int idx, int cls) {
    aircraft_t *a = target(idx, P.pos[0] + 0x40000, P.pos[1], P.pos[2] + 0x40000);
    a->f.cls = (uint8_t)cls;
    flight_sync(a);
    E.rec[idx].template_id = 3;
    return a;
}
static void tracer_at(const aircraft_t *a, int dx, int dy, int dz) {
    wp_tracer_t t; memset(&t, 0, sizeof t);
    t.cell_x = a->f.cell_x; t.cell_z = a->f.cell_z;
    t.pos[0] = (a->f.fine_x + dx) * 256; t.pos[1] = (a->f.alt8 + dy) * 256; t.pos[2] = (a->f.fine_z + dz) * 256;
    t.flags = 0x401; t.life = 20; t.latch = 1; t.owner = 0;
    W.slot[0] = t;
}

TEST(the_pause_byte_of_a_script_count_freezes_tracers_and_cool_downs_like_the_pause_screen) {
    setup(1);
    aircraft_t *a = rec_at(1, 0x12);
    W.hook.queue_c = q_hook; W.hook.slots = slots_hook;
    QE[0].id = 1; QE[0].flags = 0x10; QN = 1;
    tracer_at(a, 0, 0, 0);
    wp_tracer_t before = W.slot[0];
    W.trigger = 1;
    P.f.v4a866 = 1;                                   /* the pause byte, not the pause screen */
    weapons_gun(&W, 1, 0);
    ASSERT_EQ_INT(W.n_shots, 0);                      /* the trigger is ignored */
    ASSERT_EQ_INT(W.slot[0].life, before.life);       /* no life ages */
    ASSERT_EQ_INT(W.slot[0].pos[0], before.pos[0]);   /* no move */
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));    /* and no hit, even with the tracer at the record */
    P.f.v4a906 = 3; P.f.v4a907 = 3;
    weapons_world_start(&W, 0);                       /* the cool-downs do not run under the byte either */
    ASSERT_EQ_INT(P.f.v4a906, 3); ASSERT_EQ_INT(P.f.v4a907, 3);
    P.f.v4a866 = 0;
    weapons_gun(&W, 2, 0);                            /* released: the same call tests the hit (the record's slots are all zero here, a degenerate hull every point is inside of) */
    ASSERT_TRUE((W.slot[0].flags & WPF_HIT_SET) != 0);
}

TEST(the_pause_screen_stops_a_tracer_that_would_have_hit) {
    setup(1);
    aircraft_t *a = rec_at(1, 0x12);
    ai_t t = AI_FIXTURE; (void)t;
    W.hook.queue_c = q_hook; W.hook.slots = slots_hook;
    QE[0].id = 1; QE[0].flags = 0x10; QN = 1;
    tracer_at(a, 0, 0, 0);
    W.slot[0].sub[2] = 200 * 256; W.slot[0].step[2] = 8 * 200 * 256;   /* it would be moved through the record's box */
    const int32_t z0 = W.slot[0].pos[2];
    weapons_gun(&W, 1, 1);
    ASSERT_EQ_INT(W.slot[0].pos[2], z0);
    ASSERT_EQ_INT(W.slot[0].life, 20);
}

/* ---- the hull test reads the draw's slots and the part id of the record (8.2a) ------------------------------------------------------------------------- */

static ai_t HULL;
static void hull_setup(int part, int l7d) {
    setup(1);
    HULL = hull_table();
    for (int l = 1; l < 3; l++) memcpy(HULL.hull[l], HULL.hull[0], sizeof HULL.hull[0]);   /* the lists 1 and 2 on the same six faces */
    E.tab = &HULL;
    memset(SLOT_V, 0, sizeof SLOT_V);
    int16_t v[17][3]; hull_vertices(v);
    memcpy(SLOT_V, v, sizeof v);
    aircraft_t *a = rec_at(1, 0x12);
    a->f.b7b = (uint8_t)part; a->f.l7d = (uint8_t)l7d;
    W.hook.queue_c = q_hook; W.hook.slots = slots_hook;
    QE[0].id = 1; QE[0].flags = 0x10; QN = 1;
}
static int tracer_hits_at(int dx, int dy, int dz) {
    tracer_at(entities_ac(&E, 1), dx, dy, dz);
    weapons_gun(&W, 1, 0);
    return (W.slot[0].flags & WPF_HIT_SET) != 0;
}

TEST(a_record_at_the_coarser_part_ids_0x21_and_0x32_is_hittable_through_its_slots) {
    /* the old test recomputed the vertices at level (part & 15) and needed 17 slots, so a MiG at 0x21 / 0x32 was never hit; the slots now come from the draw's store */
    hull_setup(0x00, 0);
    ASSERT_EQ_INT(tracer_hits_at(80, 0, 0), 1);       /* the cube of half-size 100 at the unit shift 0 */
    hull_setup(0x21, 3);                              /* shift 3 - 2 = 1: the cube is halved */
    ASSERT_EQ_INT(tracer_hits_at(30, 0, 0), 1);
    hull_setup(0x21, 3);
    ASSERT_EQ_INT(tracer_hits_at(80, 0, 0), 0);
    hull_setup(0x32, 3);                              /* shift max(0, 3 - 3) = 0 */
    ASSERT_EQ_INT(tracer_hits_at(80, 0, 0), 1);
    hull_setup(0xFF, 3);                              /* the cockpit block and the far level: no hit, nothing read */
    ASSERT_EQ_INT(tracer_hits_at(0, 0, 0), 0);
}

TEST(a_record_without_stored_slots_is_not_hit) {
    hull_setup(0x00, 3);
    W.hook.slots = NULL;
    ASSERT_EQ_INT(tracer_hits_at(0, 0, 0), 0);
}

/* Note 07 8.2 and 5.3 (V: followup-combat-kills.txt, V:4A963 = 1, 2, 3 in the passes 258, 283, 307): a kept airborne wreck (hit nibble 2, counted) that the gun hits again gets the crashed bit
 * again, the step converts it to dying with the counter 15, and the expiry counts the kill again; no limit. */
TEST(a_gun_hit_on_a_kept_airborne_wreck_restarts_its_dying_cycle_and_the_kill_is_counted_again) {
    hull_setup(0x00, 0);
    aircraft_t *a = entities_ac(&E, 1);
    a->f.w0 = 0x5041;                                  /* valid, counted, airborne, no dying/crashed bit: the kept wreck after its first expiry */
    a->f.speed = a->f.speed_target = 0x1200;
    flight_sync(a);
    E.rec[1].hits = 2;                                  /* the nibble stays 2 for the life of the record */
    unsigned counts_before = entities_kill_count(&E);
    for (int cycle = 1; cycle <= 3; cycle++) {
        ASSERT_EQ_INT(tracer_hits_at(30, 0, 0), 1);
        ASSERT_TRUE((a->f.w0 & 0x0200) != 0);           /* the crashed bit again */
        int dying_pass = 0, counted_pass = 0;
        for (int pass = 1; pass <= 40; pass++) {
            E.pass = (uint32_t)(cycle * 100 + pass);
            a->pos[1] = 0x300000; a->f.w2 &= (uint16_t)~0x0080u;   /* the wreck stays in the air: the fixture has no terrain, the step would ground it */
            entities_step(&E);
            if (!dying_pass && (a->f.w0 & 0x0400)) dying_pass = pass;
            if (!counted_pass && entities_kill_count(&E) > counts_before) { counted_pass = pass; counts_before = entities_kill_count(&E); }
            E.request = 0;
        }
        ASSERT_TRUE(dying_pass > 0);
        ASSERT_TRUE(counted_pass > dying_pass);          /* counted at the expiry of the new cycle */
        ASSERT_EQ_INT(entities_kill_count(&E), (unsigned)cycle);
        ASSERT_TRUE(a->f.w0 & 0x0040);                   /* kept: airborne, no hit bit */
        a->f.w0 = (uint16_t)(a->f.w0 & ~0x0600u);
    }
}

/* ---- the landmark branch of the tracer test (8.2 "Landmarks", V: weapon_check.py landmark, 12,600 cases, 0 differences) ---------------------------------------------------- */

/* the landmark slot k of the environment: the tracer's own cell, an outer box and `nn` chain elements on the same box; element n has one plane whose normal is (nx, 0, 0) through the origin (inside when nx * X < 0) */
static void lm_slot(int k, int cell_x, int cell_z, int nn, const int nx[]) {
    flight_lm_slot_t *sl = &ENV.landmark[k];
    memset(sl, 0, sizeof *sl);
    sl->cell_x = (uint8_t)cell_x; sl->cell_z = (uint8_t)cell_z; sl->ordinal = (uint8_t)k;
    const int16_t box[5] = { 200, -1000, 1000, -1000, 1000 };
    memcpy(sl->box, box, sizeof box);
    sl->nnodes = (uint8_t)nn;
    for (int n = 0; n < nn; n++) {
        memcpy(sl->node[n].box, box, sizeof box);
        sl->node[n].nplanes = 1;
        sl->node[n].nrm[0][0] = (int16_t)nx[n];
    }
    if (ENV.n_landmarks < k + 1) ENV.n_landmarks = k + 1;
}
/* slot 0 of the pool: fine position (x, 20, z) of cell (cx, cz), the sub-step vector (sx, 0, 0), the step eight times it, the latch clear (the creation pass) */
static void lm_tracer(int cx, int cz, int x, int z, int sx) {
    wp_tracer_t t; memset(&t, 0, sizeof t);
    t.cell_x = (int16_t)cx; t.cell_z = (int16_t)cz;
    t.pos[0] = x * 256; t.pos[1] = 20 * 256; t.pos[2] = z * 256;
    t.sub[0] = sx * 256; t.step[0] = 8 * sx * 256;
    t.flags = 0x401; t.life = 20; t.latch = 0; t.owner = 0;
    W.slot[0] = t;
}
static void lm_queue(int n) { for (int i = 0; i < n; i++) { QE[i].id = 0; QE[i].flags = 0; } QN = n; W.hook.queue_c = q_hook; }

TEST(the_walk_counts_eleven_landmark_ordinals_0_to_10) {
    setup(1);
    ENV.n_landmarks = 0; ENV.landmark_z = 1;
    const int inside[1] = { 1 };
    lm_queue(11);                                           /* the walk goes backwards: queue entry 0 has the ordinal 10 */
    lm_slot(10, 0, 0, 1, inside);                           /* the only volume is the eleventh slot */
    lm_tracer(0, 0, -50, 0, 0);
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);             /* a hit at the ordinal 10 (a count of ten skipped it) */
}

TEST(a_landmark_slot_is_found_by_its_ordinal_when_a_slot_before_it_has_a_negative_pointer) {
    setup(1);
    ENV.n_landmarks = 0; ENV.landmark_z = 1;
    const int inside[1] = { 1 };
    lm_slot(0, 0, 0, 1, inside);                            /* the only listed slot ... */
    ENV.landmark[0].ordinal = 4;                            /* ... is the ordinal 4: the ordinals 0 to 3 had a negative pointer (skipped, numbers used) */
    ENV.n_landmarks = 1;
    lm_queue(3);                                            /* the walk reaches the ordinals 0, 1, 2 only */
    lm_tracer(0, 0, -50, 0, 0);
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));
    lm_queue(5);                                            /* the ordinal 4 is the first queue entry */
    lm_tracer(0, 0, -50, 0, 0);
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);
}

TEST(the_z_offset_of_a_landmark_test_carries_the_high_word_of_the_table_pointer_divided_by_4) {
    setup(1);
    ENV.n_landmarks = 0;
    const int inside[1] = { 1 };
    lm_queue(1);
    lm_slot(0, 0, 0, 1, inside);
    ENV.landmark[0].box[3] = 40; ENV.landmark[0].box[4] = 60;   /* z strictly between 40 and 60 */
    ENV.landmark[0].node[0].box[3] = 40; ENV.landmark[0].node[0].box[4] = 60;
    ENV.landmark_z = 48;                                    /* the reference machine (0xC3.... program: 48) */
    lm_tracer(0, 0, -50, 0, 0);
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);             /* z 0 + 48 is inside */
    ENV.landmark_z = 1;                                     /* the rig image */
    lm_tracer(0, 0, -50, 0, 0);
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));          /* 0 + 1 is not */
    ENV.landmark_z = 1;
}

TEST(a_tracer_in_the_next_cell_is_tested_with_the_cell_difference_times_16384_added) {
    setup(1);
    ENV.n_landmarks = 0; ENV.landmark_z = 1;
    const int inside[1] = { 1 };
    lm_queue(1);
    lm_slot(0, 5, 5, 1, inside);
    ENV.landmark[0].box[1] = 15000; ENV.landmark[0].box[2] = 17000; ENV.landmark[0].node[0].box[1] = 15000; ENV.landmark[0].node[0].box[2] = 17000;
    ENV.landmark[0].node[0].nrm[0][0] = 1; ENV.landmark[0].node[0].nrm[0][1] = 0;
    /* inside when X < 0 is false here (X = 16384 - 50): use the plane through the point (16000) instead */
    ENV.landmark[0].node[0].ref[0][0] = 16384;
    lm_tracer(6, 5, -50, 0, 0);                             /* one cell east: X = -50 + 16384 */
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);
    lm_tracer(7, 5, -50, 0, 0);                             /* two cells away: skipped */
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));
}

TEST(a_miss_at_the_last_chain_element_ends_the_call_without_the_motion_step) {
    setup(1);
    ENV.n_landmarks = 0; ENV.landmark_z = 1;
    const int outside[1] = { 1 };                           /* inside when X < 0: the tracer is at X = +10 */
    lm_queue(1);
    lm_slot(0, 0, 0, 1, outside);
    lm_tracer(0, 0, 10, 0, 3);
    const int32_t x0 = W.slot[0].pos[0], st1 = W.slot[0].step[1];
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));
    ASSERT_EQ_INT(W.slot[0].pos[0], x0 + 8 * 3 * 256);      /* the 8 sub-steps stay: one whole step */
    ASSERT_EQ_INT(W.slot[0].step[1], st1);                  /* no gravity */
    ASSERT_EQ_INT(W.slot[0].latch, 0);                      /* no first-pass latch */
}

TEST(a_failing_element_backs_the_tracer_up_by_the_whole_step_and_the_next_element_is_tested_from_the_start) {
    setup(1);
    ENV.n_landmarks = 0; ENV.landmark_z = 1;
    const int planes[2] = { 1, -1 };                        /* element 0 inside for X < 0, element 1 for X > 0 */
    lm_queue(1);
    lm_slot(0, 0, 0, 2, planes);
    lm_tracer(0, 0, 10, 0, 3);
    const int32_t x0 = W.slot[0].pos[0];
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);
    ASSERT_EQ_INT(W.slot[0].pos[0], x0 + 3 * 256);          /* the whole step undone, then the first sub-step of the element that hit */
}

TEST(the_plane_test_of_a_later_chain_element_keeps_the_cell_offsets_and_the_z_artefact_only_its_box_compares_drop_them) {
    /* review M1, 07 8.2 step 4: element 0 passes its box (z + 48 between 40 and 60) and fails every plane; the tracer is backed up and element 1 is boxed with BARE coordinates (z in -10..10)
     * but its plane is tested at z + 48: inside when the plane normal is (0, 0, -1) through z = 40 (a point at z = 48 is on the inside), outside if the 48 were dropped */
    setup(1);
    ENV.n_landmarks = 0; ENV.landmark_z = 48;
    const int nx[2] = { 1, 0 };
    lm_queue(1);
    lm_slot(0, 0, 0, 2, nx);
    ENV.landmark[0].box[3] = 40; ENV.landmark[0].box[4] = 60;
    ENV.landmark[0].node[0].box[3] = 40; ENV.landmark[0].node[0].box[4] = 60;
    ENV.landmark[0].node[1].box[3] = -10; ENV.landmark[0].node[1].box[4] = 10;
    ENV.landmark[0].node[1].nrm[0][0] = 0; ENV.landmark[0].node[1].nrm[0][2] = -1; ENV.landmark[0].node[1].ref[0][2] = 40;
    lm_tracer(0, 0, 10, 0, 3);
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);
    ENV.landmark_z = 1;
}

TEST(the_x_and_z_box_compares_of_a_landmark_are_long_compares_that_do_not_wrap_to_words) {
    /* review S9: z 16368 + the offset 16384 + 48 = 32800 is outside a box ceiling of 32767 (the word wrap made it -32768, inside a box that reaches below) */
    setup(1);
    ENV.n_landmarks = 0; ENV.landmark_z = 48;
    const int inside[1] = { 1 };
    lm_queue(1);
    lm_slot(0, 0, 0, 1, inside);
    ENV.landmark[0].box[3] = -32760; ENV.landmark[0].box[4] = 32767; ENV.landmark[0].node[0].box[3] = -32760; ENV.landmark[0].node[0].box[4] = 32767;
    lm_tracer(0, 1, -50, 16368, 0);                         /* the tracer is one cell on: the z offset is 16384 + 48 */
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));          /* 32800: not below 32767 (wrapped to a word it would be -32736, inside) */
    ENV.landmark_z = 1;
}

TEST(a_plane_count_of_0_tests_one_plane) {
    setup(1);
    ENV.n_landmarks = 0; ENV.landmark_z = 1;
    const int inside[1] = { 1 };
    lm_queue(1);
    lm_slot(0, 0, 0, 1, inside);
    ENV.landmark[0].node[0].nplanes = 0;                    /* the plane at index 0 is still formed: inside when X < 0 */
    lm_tracer(0, 0, -50, 0, 0);
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);
    lm_tracer(0, 0, 50, 0, 0);
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));
}

TEST(the_walk_ends_after_the_ordinal_10_and_after_the_sixth_entity_entry) {
    /* review M2, 07 8.2: the entries are walked from the end of the queue; after the eleventh landmark entry (ordinal 10) or the sixth entity entry nothing more is examined */
    hull_setup(0x00, 0);
    for (int i = 1; i <= 11; i++) { QE[i].id = 0; QE[i].flags = 0; }
    QE[0].id = 1; QE[0].flags = 0x10; QN = 12;              /* the entity that the degenerate hull always hits comes after the eleven landmark entries */
    tracer_at(entities_ac(&E, 1), 0, 0, 0);
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));
    QN = 11;                                                /* ten landmark entries and then the entity: reached */
    tracer_at(entities_ac(&E, 1), 0, 0, 0);
    weapons_gun(&W, 2, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);
    /* six entity entries (the owner's index: skipped, but counted), then a landmark volume that the tracer is inside of */
    const int inside[1] = { 1 };
    hull_setup(0x00, 0);
    ENV.n_landmarks = 0; ENV.landmark_z = 1;
    lm_slot(0, 0, 0, 1, inside);
    for (int i = 1; i <= 6; i++) { QE[i].id = 0; QE[i].flags = 0x10; }
    QE[0].id = 0; QE[0].flags = 0; QN = 7;
    lm_tracer(0, 0, -50, 0, 0);
    weapons_gun(&W, 3, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));          /* the walk ended at the sixth entity entry */
    QN = 6; QE[0].id = 0; QE[0].flags = 0;
    lm_tracer(0, 0, -50, 0, 0);
    weapons_gun(&W, 4, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);             /* five entity entries and the landmark */
}

/* ---- landmark and entity entries in one queue (07 8.2 "Landmark and entity entries in one queue": the ordinal shares a register with the entity sub-step counter) ---------------------------------- */

/* the landmark slot k wide around the cell of the record `a` (one plane with the normal (-1, 0, 0) through the origin when inside_x: inside at a positive fine x) */
static void lm_slot_at(int k, const aircraft_t *a, int ordinal, int inside_x) {
    const int nx[1] = { inside_x ? -1 : 1 };
    lm_slot(k, a->f.cell_x & 255, a->f.cell_z & 255, 1, nx);
    flight_lm_slot_t *sl = &ENV.landmark[k];
    sl->ordinal = (uint8_t)ordinal;
    const int16_t wide[5] = { 32000, -32000, 32000, -32000, 32000 };
    memcpy(sl->box, wide, sizeof wide); memcpy(sl->node[0].box, wide, sizeof wide);
}
/* the queue as the walk meets it (the first character first; the array is walked from its end): 'L' a landmark entry, 'E' an entity entry of the record 1, 'H' one of the record 2 */
static void mixed_queue(const char *walk_order) {
    const int n = (int)strlen(walk_order);
    for (int i = 0; i < n; i++) {
        const char c = walk_order[n - 1 - i];
        QE[i].id = c == 'L' ? 0 : c == 'E' ? 1 : 2; QE[i].flags = (uint8_t)(c == 'L' ? 0 : 0x10);
    }
    QN = n; W.hook.queue_c = q_hook;
}

TEST(the_landmark_ordinals_restart_at_0_after_an_entity_entry_whose_box_was_entered) {
    /* 07 8.2 (1): record 1 has the part id 0xFF (its box is entered, the hull test never hits); the eight sub-steps leave the shared register at -1. Slot 0 is the volume of the ordinal 2. */
    hull_setup(0xFF, 0);
    aircraft_t *a = entities_ac(&E, 1);
    ENV.n_landmarks = 0; ENV.landmark_z = 1;
    lm_slot_at(0, a, 2, 1);
    mixed_queue("LLELL");                                   /* ordinals 0 and 1, the entity, then 0 and 1 again (a counter that went on would reach 2 at the fourth entry) */
    tracer_at(a, 50, 0, 0);
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));
    ASSERT_EQ_INT(W.dbg_inbox, 1);
    a->f.w0 = (uint16_t)(a->f.w0 & ~0x0040u);               /* an entity entry that fails an earlier test (the valid bit) leaves the register alone: the fourth entry is the ordinal 2 */
    tracer_at(a, 50, 0, 0);
    weapons_gun(&W, 2, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);
    ASSERT_EQ_INT(W.dbg_inbox, 0);
}

TEST(a_queue_is_walked_past_eleven_landmark_entries_only_through_the_resets) {
    /* 07 8.2 (1), (2): record 1 (part id 0xFF) is entered without a hit, record 2 (the degenerate hull) is always hit. Six landmark entries, record 1, six more, then record 2: the register restarts at -1,
     * so the twelve landmark entries do not end the walk */
    hull_setup(0xFF, 0);
    aircraft_t *b = rec_at(2, 0x12);
    b->f.b7b = 0; b->f.l7d = 0;
    aircraft_t *a = entities_ac(&E, 1);
    ENV.n_landmarks = 0; ENV.landmark_z = 1;
    mixed_queue("LLLLLLELLLLLLH");
    tracer_at(a, 50, 0, 0);
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);
    mixed_queue("LLLLLLLLLLLLH");                           /* without the entity entry the walk ends at the eleventh landmark entry (the low word 10) */
    tracer_at(a, 50, 0, 0);
    weapons_gun(&W, 2, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));
    a->f.w0 = (uint16_t)(a->f.w0 & ~0x0040u);               /* record 1 fails the valid test: no reset, no hit */
    mixed_queue("LLLLLLELLLLLLH");
    tracer_at(a, 50, 0, 0);
    weapons_gun(&W, 3, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));
}

/* slot 0 (ordinal 0) and slot 1 (ordinal 3): the chain of slot 0 has two elements, the first passes its box and misses its plane (the tracer is outside), the whole step is undone and the second fails its box with a
 * negative link, so the call goes on. The undo loads the step vector into D4-D6 (07 8.2 (1): the register then holds step y): the walk's next ordinal is the low word of step y plus one. The plane's z difference is
 * `zdiff`, which the walk does NOT go on with. */
static void leftover_setup(int zdiff, const aircraft_t *a) {
    ENV.n_landmarks = 0; ENV.landmark_z = 1;
    lm_slot_at(0, a, 0, 0);                                 /* the normal +1: outside at the tracer's positive fine x */
    flight_lm_slot_t *s0 = &ENV.landmark[0];
    s0->nnodes = 2;
    memcpy(s0->node[1].box, s0->node[0].box, sizeof s0->node[0].box);
    s0->node[1].box[1] = 32001; s0->node[1].box[2] = 32002;  /* the second element's x range holds no tracer */
    s0->node[1].nplanes = 1;
    s0->node[0].ref[0][2] = (int16_t)(a->f.fine_z + 1 - zdiff);   /* the z difference of the plane test: the point's z (fine z plus the landmark offset 1) minus this */
    lm_slot_at(1, a, 3, 1);
}
/* the tracer at the record with the step vector's y component `sy` (the gun's own motion step is not run on a chain that ends in the undo) */
static void leftover_tracer(const aircraft_t *a, int32_t sy) {
    tracer_at(a, 50, 0, 0);
    W.slot[0].step[1] = sy;
}

TEST(a_chains_undone_step_leaves_step_y_so_the_next_ordinal_is_its_low_word_plus_one) {
    /* 07 8.2 (1): step y 2 gives the ordinal 3: the volume of slot 1 (ordinal 3) is reached by the second landmark entry; without the leftover it would be the ordinal 1. The z difference of the plane (7) is not the register */
    hull_setup(0xFF, 0);
    aircraft_t *a = entities_ac(&E, 1);
    leftover_setup(7, a);
    mixed_queue("LL");
    leftover_tracer(a, 2);
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);
    leftover_setup(2, a);                                   /* the z difference 2 would give the ordinal 3 as well; step y 5 gives the ordinal 6: no slot, no hit */
    mixed_queue("LL");
    leftover_tracer(a, 5);
    weapons_gun(&W, 2, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));
    leftover_setup(7, a);                                   /* only the low word counts: 0x30002 gives the ordinal 3 too */
    mixed_queue("LL");
    leftover_tracer(a, 0x30002);
    weapons_gun(&W, 3, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);
    leftover_setup(7, a);                                   /* the low word is a signed word: -1 gives the ordinal 0 (slot 0 again: a plane miss) */
    mixed_queue("LL");
    leftover_tracer(a, -1);
    weapons_gun(&W, 4, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));
}

TEST(an_ordinal_that_aliases_a_real_entry_tests_that_entry_and_any_other_outside_ordinal_skips) {
    /* 07 8.2 "An ordinal outside 0 to 10", port rule (C): k = j - 16384 and k = j - 32768 (j >= 1) read the real pointer and record of entry j: the volume of slot 1 (ordinal 3) is tested in full. The
     * leftover (low word of step y) is k - 1. Invented table; every other outside ordinal skips and the walk goes on (no hit). */
    hull_setup(0xFF, 0);
    aircraft_t *a = entities_ac(&E, 1);
    const int32_t hits[] = { 3 - 16384 - 1, 3 - 32768 - 1 };
    for (int i = 0; i < 2; i++) {
        leftover_setup(7, a);
        mixed_queue("LL");
        leftover_tracer(a, hits[i]);
        weapons_gun(&W, 1 + i, 0);
        ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);
    }
    const int32_t misses[] = { 2 - 16384 - 1, 5 - 32768 - 1, 3 - 16000 - 1, 3 - 16384 - 1 + 256, -2, -32767 };   /* no such slot, not an alias, off by 256, plain negatives */
    for (int i = 0; i < 6; i++) {
        leftover_setup(7, a);
        mixed_queue("LL");
        leftover_tracer(a, misses[i]);
        weapons_gun(&W, 10 + i, 0);
        ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));
    }
}

TEST(a_leftover_of_10_or_more_ends_the_walk_and_a_lower_one_goes_on_to_the_next_entry) {
    /* 07 8.2 (2): the walk ENDS when the register's low word is 10 or more; the entity record 1 (the degenerate hull is always hit, part id 0) follows the chain */
    hull_setup(0x00, 0);
    aircraft_t *a = entities_ac(&E, 1);
    leftover_setup(0, a);
    mixed_queue("LE");
    leftover_tracer(a, 10);
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));          /* ended: the entity is not examined */
    ASSERT_EQ_INT(W.dbg_inbox, 0);
    leftover_setup(0, a);
    mixed_queue("LE");
    leftover_tracer(a, 9);
    weapons_gun(&W, 2, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);
    ASSERT_EQ_INT(W.dbg_inbox, 1);
    leftover_setup(0, a);                                   /* a negative word is below 10: the walk goes on (the next landmark entry reads an ordinal outside the table: skipped unless it aliases an entry, 07 8.2) */
    mixed_queue("LE");
    leftover_tracer(a, -20000);
    weapons_gun(&W, 3, 0);
    ASSERT_TRUE(W.slot[0].flags & WPF_HIT_SET);
    ASSERT_EQ_INT(W.dbg_lm_oob_n, 0);                       /* (the entity follows at once: no landmark entry met the ordinal) */
    mixed_queue("LLE");
    leftover_setup(0, a);
    leftover_tracer(a, -20000);
    weapons_gun(&W, 4, 0);
    ASSERT_EQ_INT(W.dbg_lm_oob_n, 1);                       /* the ordinal -19999: outside the table, reported to the rig test */
    ASSERT_EQ_INT(W.dbg_lm_oob[0], -19999);
}

/* ---- the big-hull test 0x2B3F0 (8.2) ----------------------------------------------------------------------------------------------------------------- */

static int model_flags_missile(void *u, unsigned model) { (void)u; return model == 0 ? 0x18 : model == 1 ? 0x48 : 0x03; }

TEST(a_tracer_in_the_box_of_a_missile_in_flight_is_not_stopped) {
    /* the models 0 and 1 have the flag bit 4: the box is 0x800 and the test is 0x2B3F0, which answers 0 for a record in the air; the fallback that stopped a tracer near the player's own
     * missile is gone (note 07 8.2, "Models whose flag byte has bit 4") */
    setup(1);
    W.hook.model_flags = model_flags_missile;
    W.hook.queue_c = q_hook; W.hook.slots = slots_hook;
    aircraft_t *m = rec_at(1, 0x00);
    E.rec[1].template_id = 0;
    QE[0].id = 1; QE[0].flags = 0x10; QN = 1;
    tracer_at(m, 0, 0, 0);
    weapons_gun(&W, 1, 0);
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));
    ASSERT_EQ_INT(W.n_hits, 0);
    ASSERT_TRUE(W.dbg_inbox == 1);                     /* the record was in the 0x800 box (and a plain aircraft's 0x140 box would not have held a tracer 0x400 away) */
    tracer_at(m, 0x400, 0, 0);
    weapons_gun(&W, 2, 0);
    ASSERT_EQ_INT(W.dbg_inbox, 1);                     /* 0x400 du off: outside 0x140 but inside 0x800 */
    ASSERT_TRUE(!(W.slot[0].flags & WPF_HIT_SET));
}

/* the carrier's volumes against a tracer point, on the extracted tables (skipped without assets/) */
static const char *ROOT = "assets";
static int rd_file(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    char p[256];
    snprintf(p, sizeof p, "%s/%s", ROOT, path);
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
static void rd_rel(void *user, uint8_t *buf) { (void)user; free(buf); }
static game_loader_t RLD = { rd_file, rd_rel, NULL };

TEST(the_big_hull_volumes_hit_a_tracer_inside_the_carrier_below_its_reference_height_and_nothing_above_it) {
    flight_tables_t tab;
    char err[160] = "";
    if (flight_tables_load(&tab, &RLD, err, sizeof err) != 0) { printf("skipped "); return; }
    world_t *w = world_load(&RLD, err, sizeof err);
    if (!w) { printf("skipped "); return; }
    int16_t cv[FLIGHT_SHIP_SLOTS][3];
    for (int k = 0; k < FLIGHT_SHIP_SLOTS; k++) { int32_t v[3]; ASSERT_EQ_INT(world_model_vertex(w, 8, 0, (unsigned)k, v), 0); for (int i = 0; i < 3; i++) cv[k][i] = (int16_t)v[i]; }
    flight_ship_t ship; memset(&ship, 0, sizeof ship);
    ship.present = 1; ship.w0 = 0x404C; ship.cls = 0x20; ship.group = 0x0E; ship.l7d = 3;
    ship.pos[0] = 70 * 4194304 + 2097152; ship.pos[2] = 70 * 4194304 + 2097152;
    ship.fine_x = (int16_t)((ship.pos[0] & 0x3FFFFF) >> 8); ship.fine_z = (int16_t)((ship.pos[2] & 0x3FFFFF) >> 8);
    flight_ship_build(&ship, (const int16_t (*)[3])cv, FLIGHT_SHIP_SLOTS, 0, 0, 0);
    flight_env_t env; memset(&env, 0, sizeof env);
    env.tab = &tab;
    int low = 0, high = 0;
    for (int dx = -600; dx <= 600; dx += 24)
        for (int dz = -600; dz <= 600; dz += 24)
            for (int y = 0; y <= 40; y += 4) {
                const int32_t P[3] = { (int16_t)(ship.fine_x + dx), y, (int16_t)(ship.fine_z + dz) };
                low += fl_bighull_hit(&env, &ship, P);
            }
    for (int dx = -600; dx <= 600; dx += 60)
        for (int dz = -600; dz <= 600; dz += 60) {
            const int32_t P[3] = { (int16_t)(ship.fine_x + dx), 20000, (int16_t)(ship.fine_z + dz) };
            high += fl_bighull_hit(&env, &ship, P);
        }
    ASSERT_TRUE(low > 0);                              /* a tracer low inside the hull volumes is a hit */
    ASSERT_EQ_INT(high, 0);                            /* a tracer thousands of du above the reference heights is never tested against the volumes */
    world_destroy(w);
   
}

/* Note 07 10.2 (this reconciles note 05 17.11): a mover that is not the viewed record is tested ONCE at its bare previous position whatever its own vertex slots hold; the viewed mover adds its own
 * slot 0 on pass 0 and its own slot 3 on pass 1 (the player: the rotated model vertices of the flight state). */
TEST(a_ship_test_uses_the_movers_vertex_slots_only_when_the_mover_is_the_viewed_record) {
    flight_tables_t tab;
    char err[160] = "";
    if (flight_tables_load(&tab, &RLD, err, sizeof err) != 0) { printf("skipped "); return; }
    world_t *w = world_load(&RLD, err, sizeof err);
    if (!w) { printf("skipped "); return; }
    int16_t cv[FLIGHT_SHIP_SLOTS][3];
    for (int k = 0; k < FLIGHT_SHIP_SLOTS; k++) { int32_t v[3]; ASSERT_EQ_INT(world_model_vertex(w, 8, 0, (unsigned)k, v), 0); for (int i = 0; i < 3; i++) cv[k][i] = (int16_t)v[i]; }
    flight_ship_t ship; memset(&ship, 0, sizeof ship);
    ship.present = 1; ship.w0 = 0x404C; ship.cls = 0x20; ship.group = 0x0E; ship.l7d = 3;
    ship.pos[0] = 70 * 4194304 + 2097152; ship.pos[2] = 70 * 4194304 + 2097152;
    ship.fine_x = (int16_t)((ship.pos[0] & 0x3FFFFF) >> 8); ship.fine_z = (int16_t)((ship.pos[2] & 0x3FFFFF) >> 8);
    flight_ship_build(&ship, (const int16_t (*)[3])cv, FLIGHT_SHIP_SLOTS, 0, 0, 0);
    flight_env_t env; memset(&env, 0, sizeof env);
    env.tab = &tab;
    int differ_viewed = 0, differ_unviewed = 0, hits = 0;
    for (int dx = -600; dx <= 600; dx += 40)
        for (int dz = -600; dz <= 600; dz += 40)
            for (int y = 0; y <= 60; y += 6) {
                const int32_t prev[3] = { ship.pos[0] + dx * 256, y * 256, ship.pos[2] + dz * 256 };
                int r[4];
                for (int variant = 0; variant < 4; variant++) {
                    flight_state_t f; memset(&f, 0, sizeof f);
                    f.w0 = 0x1048; f.group = 0; f.l7d = 3; f.entity = 1;
                    f.self_off = 0x200; f.view_off = (variant & 1) ? 0x200 : 0;          /* variant bit 0: the mover is the viewed record */
                    if (variant & 2) { f.slot[0][1] = -300; f.slot[3][1] = 400; f.slot[0][0] = 50; }   /* variant bit 1: slots that hold something */
                    r[variant] = fl_ship_contact(&f, &env, &ship, prev);
                }
                hits += r[0] != 0;
                ASSERT_EQ_INT(r[2], r[0]);                  /* not viewed: the slots are never read */
                differ_unviewed += r[2] != r[0];
                differ_viewed += r[3] != r[1];
            }
    ASSERT_TRUE(hits > 0);
    ASSERT_EQ_INT(differ_unviewed, 0);
    ASSERT_TRUE(differ_viewed > 0);                         /* the viewed mover's own slots change the result */
    world_destroy(w);
}

/* ---- the damage handler of the player (10.4): the table of gates and outcomes ---------------------------------------------------------------------- */

typedef struct { const char *name; int coll, speed, immune, n, c6, c7; int hit, ooc, dmg, post, jolt; } dmg_row_t;

TEST(the_damage_handler_follows_the_speed_gate_the_immunity_the_cool_downs_and_the_parity_of_the_frame_counter) {
    static const dmg_row_t rows[] = {
        { "collisions off",           0, 0x800, 0, 0, 0, 0, 0, 0, 0, 0,      0 },
        { "speed at the gate",        1, 0x3C0, 0, 0, 0, 0, 1, 0, 0, 0,      0 },
        { "immune",                   1, 0x800, 1, 0, 0, 0, 1, 0, 0, 0,      0 },
        { "even, engine failure",     1, 0x3C1, 0, 0, 0, 0, 1, 1, 1, 0x9014, 1 },
        { "even, chaff cool-down",    1, 0x800, 0, 2, 5, 0, 1, 0, 0, 0,      0 },
        { "even, flare cool-down",    1, 0x800, 0, 4, 0, 1, 1, 0, 0, 0,      0 },
        { "odd, hydraulic",           1, 0x800, 0, 1, 0, 0, 1, 0, 0, 0xD00B, 1 },
        { "odd bit 1, structure",     1, 0x800, 0, 3, 0, 0, 1, 0, 0, 0xD00C, 1 },
        { "odd ignores the cool-down", 1, 0x800, 0, 5, 9, 9, 1, 0, 0, 0xD00B, 1 },
    };
    for (unsigned i = 0; i < sizeof rows / sizeof *rows; i++) {
        const dmg_row_t *r = &rows[i];
        setup(1);
        P.f.w0 = 0x11C8; P.f.v4a844 = (uint8_t)r->coll; P.f.speed = (int16_t)r->speed; P.f.v4a952 = (uint8_t)r->immune;
        P.f.v4a992 = (uint32_t)r->n; P.f.v4a906 = (uint8_t)r->c6; P.f.v4a907 = (uint8_t)r->c7;
        P.f.v4ab98 = 0; P.f.v4a878 = 0; P.f.b20 = 0; P.f.v4a984 = 0x0840;
        const int32_t before[3] = { P.pos[0], P.pos[1], P.pos[2] };
        flight_test_result(&P, 0x40, before);
        if (((P.f.w0 & 0x8000) != 0) != r->hit || ((P.f.b20 & 0x02) != 0) != r->ooc || ((P.f.v4a984 & 0x0100) != 0) != r->dmg || P.f.v4ab98 != r->post || P.f.v4a878 != r->jolt) {
            printf("  row \"%s\": hit %d ooc %d dmg %d post %04X jolt %d\n", r->name, (P.f.w0 & 0x8000) != 0, (P.f.b20 & 2) != 0, (P.f.v4a984 & 0x100) != 0, P.f.v4ab98, P.f.v4a878);
            ASSERT_TRUE(0);
        }
    }
}

/* ---- the stores, the keys and the followed record ------------------------------------------------------------------------------------------------------ */

TEST(the_rearm_writes_only_the_listed_stores_and_zeroes_the_records_1_to_3) {
    setup(1);
    E.rec[0].stock = 0x00; E.rec[0].rounds = 3;
    W.flares = 2; W.chaff = 1; W.cm_req = 2; W.flare_ref = 3; W.chaff_ref = 5;
    W.reload = 7; P.f.l7d = 0x13; P.f.v4a906 = 9; P.f.v4a907 = 8;
    for (int i = 1; i <= 3; i++) { entities_ac(&E, i)->f.w0 = 0x10C0; entities_ac(&E, i)->f.counter = 100; E.rec[i].target = 0x80; E.rec[i].template_id = 1; E.rec[i].stock = 0x44; E.rec[i].b70 = 0x12; E.rec[i].b71 = 0x05; }
    weapons_rearm(&W);
    ASSERT_EQ_INT(E.rec[0].stock, 0x24); ASSERT_EQ_INT(E.rec[0].rounds, 500);
    ASSERT_EQ_INT(W.flares, 16); ASSERT_EQ_INT(W.chaff, 16);
    ASSERT_EQ_INT(W.cm_req, 0); ASSERT_EQ_INT(W.flare_ref, 0); ASSERT_EQ_INT(W.chaff_ref, 0);
    ASSERT_EQ_INT(W.reload, 7); ASSERT_EQ_INT(P.f.l7d, 0x13);           /* not listed: the reservoir and the timer stay */
    ASSERT_EQ_INT(P.f.v4a906, 9); ASSERT_EQ_INT(P.f.v4a907, 8);        /* the cool-downs are not touched (9.7) */
    for (int i = 1; i <= 3; i++) { entity_t z; memset(&z, 0, sizeof z); ASSERT_EQ_INT(memcmp(&E.rec[i], &z, sizeof z), 0); }
}

TEST(the_select_key_reports_whether_it_acted_so_the_caller_clears_the_fire_prompt_state) {
    setup(1);
    ASSERT_EQ_INT(weapons_key_select(&W), 1);
    P.f.gear = 0x01;
    ASSERT_EQ_INT(weapons_key_select(&W), 0);
}

TEST(the_followed_record_is_the_training_lead_at_the_flight_start_and_follows_the_record_that_is_named) {
    setup(1);
    ASSERT_EQ_INT(E.trail, 4);
    weapons_follow(&W, &entities_ac(&E, 9)->f);
    ASSERT_EQ_INT(E.trail, 9);
    weapons_follow(&W, &P.f);
    ASSERT_EQ_INT(E.trail, 0);
    weapons_mission_start(&W);
    ASSERT_EQ_INT(E.trail, 4);
}

TEST(the_ecm_divisor_applies_to_every_missile_class_but_1) {
    setup(2);
    P.f.v4a96d = 1;
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);               /* a Sidewinder, class 0, target byte 0x80 + 0 after the retarget */
    E.rec[1].target = 0x80;
    weapons_gate(&W, 1);
    ASSERT_EQ_INT(entities_ac(&E, 1)->f.jam, 1);
    setup(3);
    P.f.v4a96d = 1;
    ASSERT_EQ_INT(weapons_launch_player(&W, 1), 1);               /* an AMRAAM, class 1 */
    E.rec[1].target = 0x80;
    weapons_gate(&W, 1);
    ASSERT_EQ_INT(entities_ac(&E, 1)->f.jam, 0);
}

/* ---- the ship rows of the contact scan (10.2) --------------------------------------------------------------------------------------------------------- */

static int ship_calls;
static int ship_hook(void *u, int rec, flight_ship_t *out) { (void)u; (void)rec; ship_calls++; memset(out, 0, sizeof *out); return 0; }

TEST(a_ship_in_any_record_gets_the_proximity_flag_at_the_big_box_and_goes_to_the_deck_and_hull_tests) {
    setup(2);
    aircraft_t *mover = target(2, P.pos[0], P.pos[1], P.pos[2]);
    mover->f.w0 |= 0x0008; mover->f.cls = 0x12;
    aircraft_t *ship = target(6, P.pos[0] + 0x90000, P.pos[1], P.pos[2]);   /* inside 0xA0000, outside the old inner box 0 */
    ship->f.cls = 0x20; ship->f.group = 6;
    W.hook.ship = ship_hook;
    const int32_t prev[3] = { P.pos[0], P.pos[1], P.pos[2] };
    mover->f.v4a957 = 0;
    ASSERT_EQ_INT(weapons_contact(&W, &mover->f, prev, 0), 0);
    ASSERT_EQ_INT(mover->f.v4a957, 1);
    ASSERT_EQ_INT(ship->f.w2 & EW2_PROX, EW2_PROX);
    ASSERT_EQ_INT(ship_calls > 0, 1);                                  /* the deck and hull tests were asked for this record (here: none hits) */
    ship->f.w2 = 0; mover->f.v4a957 = 0;
    ship->pos[0] = P.pos[0] + 0xB0000;                                 /* outside the box: nothing */
    ASSERT_EQ_INT(weapons_contact(&W, &mover->f, prev, 0), 0);
    ASSERT_EQ_INT(mover->f.v4a957, 0);
    ship->pos[0] = P.pos[0];                                           /* at distance zero the old stub (inner 0) called it a hit; it is not */
    ASSERT_EQ_INT(weapons_contact(&W, &mover->f, prev, 0), 0);
}

/* ---- Review Focus 1 through the world update (8.5) ---------------------------------------------------------------------------------------------------- */

/* The world update with the weapon hooks: a launch request in the first pass, then one entities_step per pass; returns the pass (1 = the launch pass) in which record 1 is no longer valid, 0 if it never is
 * within `max` passes. kill_target_after: the pass after which the hostile's record is freed (0: never). */
static void install_hooks(void) {
    E.hook.user = &W;
    E.hook.launch_player = weapons_launch_player; E.hook.launch_ai = weapons_launch_ai; E.hook.weapon = weapons_gate;
    ENV.contact = weapons_contact; ENV.contact_user = &W;
    ENV.follow = weapons_follow; ENV.follow_user = &W;
}

static int missile_freed_pass(int max, int kill_target_after, int *impact_pass) {
    int freed = 0;
    if (impact_pass) *impact_pass = 0;
    E.req_player_launch = 1;
    for (int pass = 1; pass <= max && !freed; pass++) {
        E.pass = (uint32_t)pass;
        entities_step(&E);
        if (kill_target_after && pass == kill_target_after) entities_ac(&E, 10)->f.w0 = 0;
        flight_state_t *m = &entities_ac(&E, 1)->f;
        if (impact_pass && !*impact_pass && (m->w0 & 0x8000)) *impact_pass = pass;
        if (!(m->w0 & 0x40)) freed = pass;
        E.request = 0;
    }
    return freed;
}

TEST(with_no_target_a_sidewinder_is_freed_in_pass_150_through_the_world_update) {
    setup(2);
    install_hooks();
    E.designated = -1;
    ASSERT_EQ_INT(missile_freed_pass(200, 0, NULL), 150);
    ASSERT_EQ_INT(entities_ac(&E, 1)->f.w0, 0);
}

TEST(a_target_out_of_reach_leaves_the_sidewinder_to_run_out_of_life_in_pass_150) {
    setup(2);
    install_hooks();
    target(10, P.pos[0], P.pos[1], P.pos[2] + 1500 * 0x2263);
    E.designated = 10;
    ASSERT_EQ_INT(missile_freed_pass(200, 0, NULL), 150);
}


static flight_tables_t RT;
static ai_t RAI;   /* the extracted ai.bin: the calibrated scenes below run the missile's guidance on the real rate tables */
static int have_rt;
static int real_tables(void) {
    char err[160] = "";
    if (!have_rt) have_rt = (flight_tables_load(&RT, &RLD, err, sizeof err) == 0 && assets_load_ai(&RLD, &RAI, err, sizeof err) == 0) ? 1 : -1;
    return have_rt > 0;
}
static void real_setup(int sel, int32_t y, int S) {
    setup(sel);
    ENV.tab = &RT;
    E.tab = &RAI;
    ENV.landmark_z = 1;
    flight_set_env(&ENV);
    flight_init(&P, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    flight_set_position(&P, 273272584, y, 280480032);
    flight_set_attitude(&P, 0, 2240, 0);
    P.f.w2 = 0; P.f.b4 = 0;
    P.f.speed_target = P.f.speed = (int16_t)S;
    P.f.throttle = 60; P.f.gear = 0;
    P.f.id = (uint8_t)(sel << 4 | 0x0B);
    P.f.w0 = 0x11C8; P.f.v4a984 = 0x0840; P.f.v4a844 = 1;
    flight_sync(&P);
    flight_inputs_t in; memset(&in, 0, sizeof in);
    for (int i = 0; i < 6; i++) flight_step(&P, &in);
    P.f.speed_target = P.f.speed = (int16_t)S;
    install_hooks();
}
/* The fixture's scene is not the note's rig scene (the player cruises level at speed word 0x2263 on the E2 attitude, the hostile is inert 0x1200 with the active bit): the altitude of the dive and
 * the distance of the hit are CALIBRATED so that the passes are the note's (8.5): impact 40 / freed 55, removed 35. CALIBRATED: the numbers 40 and 35 (the test names say so); THE NOTE'S: freed = impact + 15, kill = hit + 15 and the structure the note states (the dying count 15 after
 * the impact, the kill counted 15 passes after the hit, the marks of 10.3). */
TEST(in_the_fixture_scene_calibrated_to_an_impact_in_pass_40_a_missile_whose_target_died_is_freed_fifteen_passes_after_the_impact) {
    if (!real_tables()) { printf("skipped "); return; }
    real_setup(2, 1240000, 0x2263);
    E.designated = 10;
    aircraft_t *m = target(10, P.pos[0] + 30 * P.f.vel[0], P.pos[1] + 30 * P.f.vel[1], P.pos[2] + 30 * P.f.vel[2]);
    m->f.speed = m->f.speed_target = 0x1200; m->f.w0 = 0x5041;
    flight_sync(m);
    int impact = 0;
    const int freed = missile_freed_pass(260, 13, &impact);   /* the hostile's record is freed after pass 13 */
    ASSERT_EQ_INT(E.designated, -1);                          /* the stale-lock drop */
    ASSERT_EQ_INT(impact, 40);                                /* CALIBRATED (the dive altitude sets it): W0 bit 0x8000 in pass 40 */
    ASSERT_EQ_INT(freed, impact + 15);                        /* THE NOTE'S RELATION (8.5): the dying count of 15 follows the impact (55 for the calibrated 40) */
    ASSERT_EQ_INT(entities_ac(&E, 1)->f.w0, 0);
}

TEST(in_the_fixture_scene_calibrated_to_a_hit_in_pass_35_the_missile_is_removed_the_hostile_marked_and_the_kill_counted_fifteen_passes_later) {
    if (!real_tables()) { printf("skipped "); return; }
    real_setup(2, 1240000, 0x2263);
    E.designated = 10;
    aircraft_t *m = target(10, P.pos[0] + 52 * P.f.vel[0], P.pos[1] + 52 * P.f.vel[1], P.pos[2] + 52 * P.f.vel[2]);
    m->f.speed = m->f.speed_target = 0x1200; m->f.w0 = 0x5041;
    flight_sync(m);
    int hit = 0, kill = 0;
    E.req_player_launch = 1;
    for (int pass = 1; pass <= 80; pass++) {
        E.pass = (uint32_t)pass;
        const unsigned k0 = E.kills + P.f.v4a963;
        entities_step(&E);
        if (!hit && (entities_ac(&E, 10)->f.w0 & 0x0600)) hit = pass;
        if (!kill && (E.kills + P.f.v4a963) > k0) kill = pass;
        E.request = 0;
    }
    ASSERT_EQ_INT(hit, 35);                                    /* CALIBRATED (the distance of the hostile sets it) */
    ASSERT_EQ_INT(kill, hit + 15);                             /* THE NOTE'S RELATION (8.5): the dying count of 15, the kill counted at the expiry (50 for the calibrated 35) */
    const flight_state_t *h = &entities_ac(&E, 10)->f;
    ASSERT_TRUE((h->b20 & 0x02) != 0);                         /* out of control */
    ASSERT_EQ_INT(h->state, 1);
    ASSERT_EQ_INT(E.trail, 10);                                /* V:54E72 */
    ASSERT_EQ_INT(LB[LB_AIM9_HITS], 1);
    ASSERT_EQ_INT(LB[LB_AIM9_FIRED], 1);
    ASSERT_TRUE(!(entities_ac(&E, 1)->f.w0 & 0x40));           /* the missile is gone in the hit pass */
    ASSERT_EQ_INT(E.designated, -1);
}

/* ---- the draw scan 0x2AD18 (12.5; V: gunorder_check.py on combat7-gunorder.txt, and test_weapons_diff.c part gunorder, 2,000 random pools, 0 differences) --------------------------------------------- */

TEST(the_draw_scan_looks_at_the_highest_active_slot_only_and_a_plain_slot_in_the_cockpit_gives_0) {
    setup(3);
    const int32_t cam[3] = { 64 * 4194304 + 400, 0x300000, 64 * 4194304 - 5000 };
    ASSERT_EQ_INT(weapons_scan(&W, 1, cam), 0);                            /* no active slot */
    W.slot[6].flags = 0x2401; W.slot[6].cell_x = 64; W.slot[6].cell_z = 64; W.slot[6].pos[1] = 0x300000; W.slot[6].pos[2] = 0x3FFFFF - 100;
    W.slot[6].pos[0] = 3000;                                                   /* beyond the owner (the player, at the cell's origin) from this camera */
    ASSERT_EQ_INT(weapons_scan(&W, 0, cam), 1);                            /* a chaff slot goes on to the distance test in the cockpit view too */
    W.slot[10].flags = 0x0401; W.slot[10].cell_x = 64; W.slot[10].cell_z = 64;
    ASSERT_EQ_INT(weapons_scan(&W, 0, cam), 0);                            /* the higher slot is a plain tracer: the cockpit view gives 0 and the chaff slot below is not looked at */
    W.slot[10].flags = 0x0400;                                                 /* inactive: bit 0 clear */
    ASSERT_EQ_INT(weapons_scan(&W, 0, cam), 1);
    W.slot[19].flags = 0x0001;
    ASSERT_EQ_INT(weapons_scan(&W, 0, cam), 0);                            /* slot 19 is the first looked at */
}

TEST(the_draw_scan_answers_1_when_the_owner_is_not_farther_from_the_camera_than_the_slot) {
    setup(3);
    W.slot[2].flags = 0x0001; W.slot[2].owner = 0; W.slot[2].cell_x = 64; W.slot[2].cell_z = 64;
    W.slot[2].pos[0] = 0; W.slot[2].pos[1] = 0x300000; W.slot[2].pos[2] = 0;   /* the cell's origin: the player's position too */
    const int32_t cam_near[3] = { 64 * 4194304, 0x300000, 64 * 4194304 };
    ASSERT_EQ_INT(weapons_scan(&W, 1, cam_near), 1);                       /* equal distances: the owner's is not greater */
    W.slot[2].pos[0] = 200 * 256;
    ASSERT_EQ_INT(weapons_scan(&W, 1, cam_near), 1);                       /* the tracer is farther */
    W.slot[2].pos[0] = 0; P.pos[0] += 200 * 256;
    ASSERT_EQ_INT(weapons_scan(&W, 1, cam_near), 0);                       /* the owner is farther than the tracer */
    P.pos[0] -= 200 * 256;
}

int main(void) {
    RUN(the_fire_key_holds_the_trigger_for_the_gun_and_requests_a_launch_for_a_missile_and_nothing_with_the_gear_down);
    RUN(the_select_key_cycles_3_2_1_0_3_and_keeps_the_low_nibble);
    RUN(a_flare_press_posts_0x4026_sets_the_request_and_the_30_pass_cool_down_and_the_last_flare_posts_0x4027);
    RUN(a_chaff_press_posts_0x4028_and_the_last_one_0x4029);
    RUN(the_cool_downs_count_down_once_per_unpaused_pass);
    RUN(the_ecm_key_toggles_the_switch);
    RUN(the_gun_fires_a_burst_of_five_three_rounds_and_three_log_counts_each_then_waits_for_the_reload);
    RUN(the_reservoir_is_refilled_to_five_when_the_trigger_is_released_and_the_timer_has_run_out);
    RUN(the_last_rounds_are_taken_to_zero_and_with_no_round_left_nothing_is_fired);
    RUN(a_paused_pass_neither_fires_nor_ages_the_tracers);
    RUN(a_flare_request_takes_a_pool_slot_with_the_flare_flag_and_counts_three_log_rounds_without_using_rounds);
    RUN(a_tracer_is_deactivated_in_the_step_after_its_life_has_run_out);
    RUN(a_point_inside_all_six_planes_is_a_hit_and_one_outside_any_plane_is_not);
    RUN(the_hull_moves_with_the_records_fine_position_and_a_part_id_with_bit_7_is_never_a_hit);
    RUN(a_launched_record_carries_the_owners_map_bytes_in_the_first_0xa4_bytes);
    RUN(an_aim_120_launch_takes_a_long_range_stock_gives_class_1_life_200_and_requests_0x8c);
    RUN(an_aim_9_launch_takes_a_short_range_stock_gives_class_0_life_150);
    RUN(a_launch_with_no_stock_of_the_selected_kind_is_refused);
    RUN(the_launch_pass_already_decrements_the_life_and_boosts_the_speed_word_by_0x1e0_toward_the_designated_target);
    RUN(without_a_designation_the_player_missile_also_steps_toward_0x4200_by_0x240_and_aims_at_no_height);
    RUN(the_boost_stops_at_0x4200);
    RUN(a_missile_is_freed_by_the_150th_gate_call_of_a_sidewinder_with_the_display_request_0x0c);
    RUN(below_a_life_of_50_the_missile_coasts_toward_0x3000_and_below_12_it_is_marked_dying);
    RUN(a_gate_for_an_invalid_record_does_nothing_and_a_paused_world_skips_the_gate);
    RUN(an_aim_120_inside_the_inner_box_of_a_counted_hostile_marks_it_crashed_and_out_of_control_and_counts_the_hit);
    RUN(a_target_outside_the_outer_box_is_untouched_and_between_the_boxes_only_the_proximity_mark_is_set);
    RUN(the_cruise_missile_has_the_small_boxes_0x5000_and_0x1000);
    RUN(a_hit_on_a_record_of_the_same_group_or_an_uncounted_pair_is_ignored);
    RUN(a_dying_or_crashed_record_is_not_hit_again);
    RUN(the_jolt_row_offset_is_zero_without_a_jolt_and_the_machine_stays_off_without_a_kind);
    RUN(the_gun_shot_plays_the_cannon_burst_through_the_sound_module_unless_the_map_byte_is_set);
    RUN(the_pause_byte_of_a_script_count_freezes_tracers_and_cool_downs_like_the_pause_screen);
    RUN(the_pause_screen_stops_a_tracer_that_would_have_hit);
    RUN(a_record_at_the_coarser_part_ids_0x21_and_0x32_is_hittable_through_its_slots);
    RUN(a_record_without_stored_slots_is_not_hit);
    RUN(the_walk_counts_eleven_landmark_ordinals_0_to_10);
    RUN(a_landmark_slot_is_found_by_its_ordinal_when_a_slot_before_it_has_a_negative_pointer);
    RUN(the_z_offset_of_a_landmark_test_carries_the_high_word_of_the_table_pointer_divided_by_4);
    RUN(a_tracer_in_the_next_cell_is_tested_with_the_cell_difference_times_16384_added);
    RUN(a_miss_at_the_last_chain_element_ends_the_call_without_the_motion_step);
    RUN(a_failing_element_backs_the_tracer_up_by_the_whole_step_and_the_next_element_is_tested_from_the_start);
    RUN(the_plane_test_of_a_later_chain_element_keeps_the_cell_offsets_and_the_z_artefact_only_its_box_compares_drop_them);
    RUN(the_x_and_z_box_compares_of_a_landmark_are_long_compares_that_do_not_wrap_to_words);
    RUN(a_plane_count_of_0_tests_one_plane);
    RUN(the_walk_ends_after_the_ordinal_10_and_after_the_sixth_entity_entry);
    RUN(the_landmark_ordinals_restart_at_0_after_an_entity_entry_whose_box_was_entered);
    RUN(a_queue_is_walked_past_eleven_landmark_entries_only_through_the_resets);
    RUN(a_chains_undone_step_leaves_step_y_so_the_next_ordinal_is_its_low_word_plus_one);
    RUN(an_ordinal_that_aliases_a_real_entry_tests_that_entry_and_any_other_outside_ordinal_skips);
    RUN(a_leftover_of_10_or_more_ends_the_walk_and_a_lower_one_goes_on_to_the_next_entry);
    RUN(a_gun_hit_on_a_kept_airborne_wreck_restarts_its_dying_cycle_and_the_kill_is_counted_again);
    RUN(a_tracer_in_the_box_of_a_missile_in_flight_is_not_stopped);
    RUN(the_big_hull_volumes_hit_a_tracer_inside_the_carrier_below_its_reference_height_and_nothing_above_it);
    RUN(a_ship_test_uses_the_movers_vertex_slots_only_when_the_mover_is_the_viewed_record);
    RUN(the_damage_handler_follows_the_speed_gate_the_immunity_the_cool_downs_and_the_parity_of_the_frame_counter);
    RUN(the_rearm_writes_only_the_listed_stores_and_zeroes_the_records_1_to_3);
    RUN(the_select_key_reports_whether_it_acted_so_the_caller_clears_the_fire_prompt_state);
    RUN(the_followed_record_is_the_training_lead_at_the_flight_start_and_follows_the_record_that_is_named);
    RUN(the_ecm_divisor_applies_to_every_missile_class_but_1);
    RUN(the_draw_scan_looks_at_the_highest_active_slot_only_and_a_plain_slot_in_the_cockpit_gives_0);
    RUN(the_draw_scan_answers_1_when_the_owner_is_not_farther_from_the_camera_than_the_slot);
    RUN(a_ship_in_any_record_gets_the_proximity_flag_at_the_big_box_and_goes_to_the_deck_and_hull_tests);
    RUN(with_no_target_a_sidewinder_is_freed_in_pass_150_through_the_world_update);
    RUN(a_target_out_of_reach_leaves_the_sidewinder_to_run_out_of_life_in_pass_150);
    RUN(in_the_fixture_scene_calibrated_to_an_impact_in_pass_40_a_missile_whose_target_died_is_freed_fifteen_passes_after_the_impact);
    RUN(in_the_fixture_scene_calibrated_to_a_hit_in_pass_35_the_missile_is_removed_the_hostile_marked_and_the_kill_counted_fifteen_passes_later);
    TEST_MAIN_END();
}
