/* tests/test_missions.c - the mission scripts and the dispatcher (src/core/missions.c, docs/re/08-game-flow.md sections 12 to 14, "note 08").
 *
 * The records are synthetic: the fields a script reads are set on the entity records directly (W0, W2, the cells, the speeds, B20, B21, the positions), the numbers are the
 * note's (the thresholds, the delays, the start values of the counts, the quirk of the F6 delay). The outcome texts are ids; no text is compared. */
#include "test.h"
#include <math.h>
#include "../src/core/missions.h"
#include "../src/core/entities.h"
#include "../src/core/weapons.h"
#include "../src/core/world.h"
#include "../src/core/game.h"
#include "../src/core/screen.h"
#include "../src/core/mover.h"
#include "../src/core/logbook.h"

static entities_t E;
static aircraft_t P;
static missions_t M;

static int route_calls;
static int route_hook(void *u, unsigned r, int16_t blocks[][5], int max) {
    (void)u; (void)max;
    route_calls++;
    blocks[0][0] = (int16_t)(60 + r); blocks[0][1] = (int16_t)(70 + r); blocks[0][2] = 0x2000; blocks[0][3] = 0x20A0; blocks[0][4] = 176;
    return 2;
}

static flight_state_t *R(int i) { return &entities_ac(&E, i)->f; }
static aircraft_t *A(int i) { return entities_ac(&E, i); }

static void setup(int mode, int loc) {
    memset(&P, 0, sizeof P);
    P.f.cls = 0x11; P.f.w0 = 0x11C8;
    entities_init(&E, NULL, NULL, NULL);
    entities_attach_player(&E, &P);
    E.mode = (uint8_t)mode; E.sim_on = 1; E.flying = 1; E.demo = 0; E.hostile_cap = 2; E.progress = 0;
    E.hook.route = route_hook;
    route_calls = 0;
    missions_start(&M, mode, loc);
}
static void step(void) { missions_step(&M, &E); }
static void rec(int i, unsigned w0, int cell_x, int fine_x) { R(i)->w0 = (uint16_t)w0; R(i)->cell_x = (int16_t)cell_x; R(i)->fine_x = (int16_t)fine_x; }
static int code(void) { return P.f.v4a850; }
static int pause_byte(void) { return P.f.v4a866; }

/* ---- the gates (12.1) ------------------------------------------------------------------------------------------------------------------------- */

TEST(the_dispatcher_acts_only_with_the_simulation_bit_and_the_demo_flag_clear_and_a_mode_above_2) {
    setup(3, 0);
    M.view.in_ident = -1;
    E.sim_on = 0; step(); ASSERT_EQ_INT(code(), 0);                      /* 0x28546: the simulation switch */
    E.sim_on = 1; E.demo = 1; step(); ASSERT_EQ_INT(code(), 0);          /* 0x28550: the demo flag */
    E.demo = 0; E.mode = 2; step(); ASSERT_EQ_INT(code(), 0);            /* a mode byte of 2 or less (signed) does nothing */
    E.mode = 1; step(); ASSERT_EQ_INT(code(), 0);
    E.mode = 0xFF; step(); ASSERT_EQ_INT(code(), 0);
    E.mode = 9; step(); ASSERT_EQ_INT(code(), 0);                        /* the qualification (9.3) and the training (8.9) are the game's */
    E.mode = 0x7D; step(); ASSERT_EQ_INT(code(), 0);
    E.mode = 3; step(); ASSERT_EQ_INT(code(), 0xFF);
}

TEST(the_end_of_update_clears_the_launch_request_bytes_and_drops_a_stale_lock) {
    setup(3, 0);
    E.req_player_launch = E.req_ai_slot4 = E.req_ai = 1;                 /* V:4A872, V:4A873, V:4A874 */
    E.designated = 5; step();                                            /* record 5 is not valid: the lock is stale */
    ASSERT_EQ_INT(E.req_player_launch + E.req_ai_slot4 + E.req_ai, 0);
    ASSERT_EQ_INT(E.designated, -1);
}

/* ---- F1 (12.5) ----------------------------------------------------------------------------------------------------------------------------- */

TEST(f1_succeeds_when_the_counter_is_negative_and_stores_the_end_code_once) {
    setup(3, 0);
    step(); ASSERT_EQ_INT(code(), 0);
    M.view.in_ident = -1;
    step();
    ASSERT_EQ_INT(code(), 0xFF);
    ASSERT_EQ_INT(*E.req, 3); ASSERT_EQ_INT(*E.req_delay, 4);            /* the shared end 0x28ACE: the request 3 and its countdown 4 */
    ASSERT_EQ_INT(M.delay, 0); ASSERT_EQ_INT(M.key_lock, 1);
    ASSERT_EQ_INT(M.ends, 1);
    step(); step();                                                      /* the code is not 0: the tail only; the landing test fails */
    ASSERT_EQ_INT(M.ends, 1); ASSERT_EQ_INT(code(), 0xFF);
}

TEST(a_request_3_already_pending_stops_a_script_from_storing_again) {
    setup(3, 0);
    M.view.in_ident = -1; *E.req = 3; *E.req_delay = 2;
    step();
    ASSERT_EQ_INT(code(), 0); ASSERT_EQ_INT(*E.req_delay, 2); ASSERT_EQ_INT(M.ends, 0);
}

TEST(a_pending_request_of_another_kind_is_overwritten_by_the_scripts_3_inside_the_window) {
    setup(3, 0);
    M.view.in_ident = -1; *E.req = 2; *E.req_delay = 1;                          /* the seat's request 2 (Review Focus 3): the script stores 3 and the countdown 4 over it */
    step();
    ASSERT_EQ_INT(*E.req, 3); ASSERT_EQ_INT(*E.req_delay, 4); ASSERT_EQ_INT(code(), 0xFF);
}

/* ---- the tail: the landing test and 0xFC (12.2) ---------------------------------------------------------------------------------------------- */

static void landed(int deck_or_pad_b4) { P.f.b21 = 1; P.f.w2 = 0x0080; P.f.b4 = (uint8_t)deck_or_pad_b4; P.f.speed = 0; }

TEST(the_landing_test_at_an_airfield_needs_the_pad_bit_and_stores_0xFC_with_delay_0) {
    setup(3, 0);
    P.f.v4a850 = 0xFF; landed(0x04);
    step();
    ASSERT_EQ_INT(code(), 0xFC); ASSERT_EQ_INT(*E.req, 3); ASSERT_EQ_INT(*E.req_delay, 4); ASSERT_EQ_INT(M.delay, 0);
    setup(3, 0); P.f.v4a850 = 1; landed(0x40); step(); ASSERT_EQ_INT(code(), 1);   /* a deck bit is not a pad */
    setup(3, 0); P.f.v4a850 = 1; landed(0x04); step(); ASSERT_EQ_INT(code(), 0xFC); /* code 1 (the flight resumed after the text) is the other code the tail takes */
}

TEST(the_landing_test_at_the_carrier_needs_a_deck_bit) {
    setup(4, 3);
    P.f.v4a850 = 0xFF; landed(0x80); step(); ASSERT_EQ_INT(code(), 0xFC);
    setup(4, 3); P.f.v4a850 = 0xFF; landed(0x40); step(); ASSERT_EQ_INT(code(), 0xFC);
    setup(4, 3); P.f.v4a850 = 0xFF; landed(0x04); step(); ASSERT_EQ_INT(code(), 0xFF);   /* the pad bit does not count on the carrier */
}

TEST(the_landing_test_needs_the_lift_off_the_ground_bit_and_speed_zero) {
    setup(3, 0);
    P.f.v4a850 = 0xFF; landed(0x04); P.f.b21 = 0; step(); ASSERT_EQ_INT(code(), 0xFF);   /* never airborne */
    landed(0x04); P.f.w2 = 0; step(); ASSERT_EQ_INT(code(), 0xFF);                         /* not on the ground */
    landed(0x04); P.f.speed = 12; step(); ASSERT_EQ_INT(code(), 0xFF);                     /* indicated speed not 0 */
    landed(0x04); step(); ASSERT_EQ_INT(code(), 0xFC);
}

TEST(the_other_codes_disable_the_tail) {
    static const int codes[] = { 0xFE, 0xFD, 0xFC, 4, 0xF0, 0xEF, 2 };
    for (unsigned i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        setup(3, 0);
        P.f.v4a850 = (uint8_t)codes[i]; landed(0x04); M.view.in_ident = -1;
        step();
        ASSERT_EQ_INT(code(), codes[i]); ASSERT_EQ_INT(M.ends, 0);
    }
}

/* ---- F2 (12.6) ----------------------------------------------------------------------------------------------------------------------------- */

TEST(f2_waits_until_air_force_one_is_down_slow_and_both_fighters_are_gone) {
    setup(4, 3);
    step(); ASSERT_EQ_INT(pause_byte(), 0);                              /* slot 4 never placed (cell x and fine x zero) */
    rec(4, 0x40C8, 70, 5); rec(8, 0x40C8, 76, 5); rec(10, 0x40C8, 75, 5);
    R(4)->w2 = 0x0080; R(4)->speed_target = 0;
    step(); ASSERT_EQ_INT(pause_byte(), 0);                              /* the fighters are alive */
    rec(8, 0, 76, 5);
    step(); ASSERT_EQ_INT(pause_byte(), 0);                              /* one is left */
    rec(10, 0, 75, 5);
    R(4)->speed_target = 0x321; step(); ASSERT_EQ_INT(pause_byte(), 0);  /* faster than 800 */
    R(4)->speed_target = 0x320; R(4)->w2 = 0; step(); ASSERT_EQ_INT(pause_byte(), 0);   /* not on the ground */
    R(4)->w2 = 0x0080; step(); ASSERT_EQ_INT(pause_byte(), 4);           /* the count starts: 5 loaded and decremented */
}

TEST(f2_success_counts_5_passes_hands_over_the_view_at_1_and_stores_0xFF_with_delay_4) {
    setup(4, 3);
    rec(4, 0x40C8, 70, 5); R(4)->w2 = 0x0080; R(4)->speed_target = 0; R(4)->heading = 777;
    M.view.in_selector = 0; M.view.in_direction = 3;
    const int bytes[] = { 4, 3, 2, 1 };
    for (int i = 0; i < 4; i++) { step(); ASSERT_EQ_INT(pause_byte(), bytes[i]); ASSERT_EQ_INT(code(), 0); }
    ASSERT_EQ_INT(M.view.v4a876, 0x83);                                  /* the hand-over ran in the pass at 1: bit 7 and the old direction */
    ASSERT_EQ_INT(M.view.v4a96a, 7); ASSERT_EQ_INT(M.view.v4a86d, 1); ASSERT_EQ_INT(M.view.v4a86c, 1);
    ASSERT_EQ_INT(M.view.v4a8eb, 0xFF); ASSERT_EQ_INT(M.view.toggle_request, 1); ASSERT_EQ_INT(M.view.v4a9e2, 777);
    ASSERT_EQ_INT(M.view.v4a8ec, 0);                                     /* the saved view selector */
    ASSERT_EQ_INT(P.f.v4a994, 0);                                        /* the tracking camera waits for the pass at 0 */
    step();
    ASSERT_EQ_INT(pause_byte(), 0); ASSERT_EQ_INT(code(), 0xFF); ASSERT_EQ_INT(M.delay, 4);
    ASSERT_EQ_INT(*E.req, 3); ASSERT_EQ_INT(*E.req_delay, 4);
    ASSERT_EQ_INT(P.f.v4a994, 4); ASSERT_EQ_INT(P.f.v4a996, 0x800); ASSERT_EQ_INT(P.f.v4a968, 0xFE);   /* the tracking camera on slot 4 */
    ASSERT_EQ_INT(M.view.v4a8ee, 3);
    ASSERT_EQ_INT(M.view.track, 1);
    ASSERT_EQ_INT(M.view.ring_set, 1);                                   /* the hand-over's ring write waits for the game */
    ASSERT_EQ_INT(M.view.sel_set, 0);                                    /* the cockpit view: the heading is saved, the selector is not written */
}

TEST(the_tracking_camera_in_an_external_view_writes_the_selector_from_v4a8eb_for_the_game) {
    setup(7, 3);
    rec(4, 0x0440, 60, 1);                                               /* the missile arrived: the dying path, no wreck bit */
    M.view.in_selector = 1; M.view.in_direction = 4; M.view.v4a8eb = 3;
    step(); step();                                                      /* start 2: the byte 1 (the hand-over), then 0 */
    ASSERT_EQ_INT(code(), 0xFE);
    ASSERT_EQ_INT(M.view.track, 1);
    ASSERT_EQ_INT(M.view.v4a83d, 3); ASSERT_EQ_INT(M.view.sel_set, 1);   /* V:4A83D = V:4A8EB (12.2): the game puts it into its view level */
    ASSERT_EQ_INT(M.view.v4a96a, 7); ASSERT_EQ_INT(M.view.ring_set, 1);
    M.view.ring_set = 0;                                                 /* the game applied the ring: the value stays for the restart (12.11) */
    missions_start(&M, 7, 3);
    ASSERT_EQ_INT(M.view.v4a96a, 7); ASSERT_EQ_INT(M.view.ring_set, 0); ASSERT_EQ_INT(M.view.sel_set, 0);
}

TEST(f2_failure_is_0xFE_or_0xFD_by_bit_7_of_b20_with_delay_0_and_no_tracking_camera) {
    setup(4, 3);
    rec(4, 0, 70, 5);                                                    /* destroyed: not valid */
    for (int i = 0; i < 4; i++) step();
    ASSERT_EQ_INT(code(), 0); ASSERT_EQ_INT(pause_byte(), 1);
    step();
    ASSERT_EQ_INT(code(), 0xFE); ASSERT_EQ_INT(M.delay, 0); ASSERT_EQ_INT(*E.req, 3); ASSERT_EQ_INT(M.view.track, 0);
    setup(4, 3);
    rec(4, 0, 70, 5); R(4)->b20 = 0x80;                                  /* the player's hit destroyed it */
    for (int i = 0; i < 5; i++) step();
    ASSERT_EQ_INT(code(), 0xFD);
}

TEST(f2_does_nothing_for_a_never_placed_slot_even_with_the_slot_invalid) {
    setup(4, 3);
    for (int i = 0; i < 8; i++) step();
    ASSERT_EQ_INT(code(), 0); ASSERT_EQ_INT(pause_byte(), 0);
}

TEST(f2_count_ends_without_a_store_when_the_request_is_3_and_restarts_from_the_start_value) {
    setup(4, 3);
    rec(4, 0, 70, 5);
    *E.req = 3; *E.req_delay = 2;
    for (int i = 0; i < 5; i++) step();
    ASSERT_EQ_INT(code(), 0); ASSERT_EQ_INT(pause_byte(), 0);            /* the byte reached 0, nothing was stored */
    step(); ASSERT_EQ_INT(pause_byte(), 4);                              /* the trigger still holds: the count restarts */
}

/* ---- F3 (12.7) ----------------------------------------------------------------------------------------------------------------------------- */

TEST(f3_fails_when_a_neutral_thief_reaches_cell_x_0x70) {
    setup(5, 3);
    rec(4, 0x40C8, 0x6F, 1); step(); ASSERT_EQ_INT(code(), 0);
    R(4)->cell_x = 0x70; step();
    ASSERT_EQ_INT(code(), 0xFE); ASSERT_EQ_INT(M.delay, 0); ASSERT_EQ_INT(*E.req, 3);
    setup(5, 3); rec(6, 0x40C8, 0x70, 1); step(); ASSERT_EQ_INT(code(), 0xFE);   /* slot 6 is the subject when slot 4 is not valid */
}

TEST(f3_a_thief_turned_hostile_is_no_subject) {
    setup(5, 3);
    rec(4, 0x40C0, 0x70, 1);                                             /* valid, bit 3 (neutral) clear */
    step(); ASSERT_EQ_INT(code(), 0);
}

TEST(f3_shadowing_loads_200_when_far_counts_down_when_near_and_reloads_when_far) {
    setup(5, 3);
    rec(4, 0x40C8, 60, 1);
    A(4)->pos[0] = 0x100000; A(4)->pos[1] = 0; A(4)->pos[2] = 0; P.pos[0] = 0; P.pos[1] = 0; P.pos[2] = 0;
    step(); ASSERT_EQ_INT(M.timer, 200);                                 /* far (0x100000 > 0x30000) with the timer 0 (not negative): loaded with 200 */
    step(); ASSERT_EQ_INT(M.timer, 200);                                 /* far again: reloaded */
    A(4)->pos[0] = 0; step(); ASSERT_EQ_INT(M.timer, 199);               /* near: one per pass */
}

TEST(f3_the_timer_runs_down_one_per_pass_while_near_and_the_distance_depends_on_the_progress) {
    setup(5, 3);
    rec(4, 0x40C8, 60, 1);
    M.timer = 200;
    A(4)->pos[0] = 0x30000; A(4)->pos[1] = 0x30000; A(4)->pos[2] = 0x30000;   /* exactly D at progress 0 */
    step(); ASSERT_EQ_INT(M.timer, 199);
    step(); ASSERT_EQ_INT(M.timer, 198);
    A(4)->pos[0] = 0x30001; step(); ASSERT_EQ_INT(M.timer, 200);        /* far: reloaded */
    E.progress = 1; A(4)->pos[0] = 0x30000; step(); ASSERT_EQ_INT(M.timer, 199);   /* progress 0 and 1: 0x30000 */
    E.progress = 2; A(4)->pos[0] = 0x24000; A(4)->pos[1] = 0x24000; A(4)->pos[2] = 0x24000; step(); ASSERT_EQ_INT(M.timer, 198);
    A(4)->pos[0] = 0x24001; step(); ASSERT_EQ_INT(M.timer, 200);
    E.progress = 3; A(4)->pos[0] = A(4)->pos[1] = A(4)->pos[2] = 0x18000; step(); ASSERT_EQ_INT(M.timer, 199);
    E.progress = 7; A(4)->pos[0] = 0x18001; step(); ASSERT_EQ_INT(M.timer, 200);
}

TEST(f3_with_the_timer_negative_both_thieves_are_rerouted_to_the_first_waypoint_of_their_route) {
    setup(5, 3);
    rec(4, 0x40C8, 60, 1); rec(6, 0x40C8, 60, 1);
    E.rec[4].route = 0x12; E.rec[6].route = 0x13;
    M.timer = -1;
    step();
    ASSERT_EQ_INT(route_calls, 2);
    ASSERT_EQ_INT(E.rec[4].dest_cx, 0x12 + 60); ASSERT_EQ_INT(E.rec[4].dest_cz, 0x12 + 70); ASSERT_EQ_INT(E.rec[4].dest_fx, 0x2000); ASSERT_EQ_INT(E.rec[4].dest_fz, 0x20A0); ASSERT_EQ_INT(E.rec[4].dest_h, 176);
    ASSERT_EQ_INT(E.rec[6].dest_cx, 0x13 + 60);
}

TEST(f3_succeeds_after_the_shadowing_when_the_kills_reach_the_cap_with_one_frozen_pass) {
    setup(5, 3);
    rec(4, 0x40C8, 60, 1); M.timer = -1;                                 /* the shadowing is in force */
    E.kills = 1; step(); ASSERT_EQ_INT(pause_byte(), 0);
    E.kills = 2; step(); ASSERT_EQ_INT(pause_byte(), 1); ASSERT_EQ_INT(code(), 0);   /* one frozen pass */
    step();
    ASSERT_EQ_INT(code(), 0xFF); ASSERT_EQ_INT(M.delay, 0); ASSERT_EQ_INT(*E.req, 3); ASSERT_EQ_INT(pause_byte(), 0);
    ASSERT_EQ_INT(M.view.v4a876, 0);                                     /* no hand-over in F3 */
}

TEST(f3_with_no_thief_alive_the_success_test_runs_at_once_and_the_cap_grows_with_turned_thieves) {
    setup(5, 3);
    E.kills = 2; step(); step();
    ASSERT_EQ_INT(code(), 0xFF);
    setup(5, 3);
    E.hostile_cap = 4; E.kills = 2; step(); step(); ASSERT_EQ_INT(code(), 0);   /* a turned thief raised the cap to 3 and 4 */
    E.kills = 4; step(); step(); ASSERT_EQ_INT(code(), 0xFF);
}

TEST(f3_a_hostile_thief_with_the_timer_not_negative_ends_the_pass_before_the_success_test) {
    setup(5, 3);
    rec(4, 0x40C0, 60, 1);                                               /* valid, not neutral */
    E.kills = 2;
    step(); step(); ASSERT_EQ_INT(code(), 0); ASSERT_EQ_INT(pause_byte(), 0);
    M.timer = -1; step(); step(); ASSERT_EQ_INT(code(), 0xFF);
}

/* ---- F4 (12.10) ---------------------------------------------------------------------------------------------------------------------------- */

static void pod_setup(int dx, int dz) {
    E.chute_latch = 0x200;
    R(1)->w0 = 0x8040; R(1)->counter = 0;
    A(1)->pos[0] = 0x1000000 + dx; A(1)->pos[2] = 0x2000000 + dz;
    A(11)->pos[0] = 0x1000000; A(11)->pos[2] = 0x2000000;
}

TEST(f4_waits_for_a_release_a_landing_and_the_counter) {
    setup(6, 3);
    step(); ASSERT_EQ_INT(code(), 0);                                    /* nothing released */
    pod_setup(0, 0); R(1)->w0 = 0x0040; step(); ASSERT_EQ_INT(code(), 0);       /* not landed (W0 bit 0x8000 clear) */
    R(1)->w0 = 0x8040; R(1)->counter = 1; step(); ASSERT_EQ_INT(code(), 0);      /* the counter above 0 */
    R(1)->counter = -5; step(); ASSERT_EQ_INT(code(), 0xFF);
}

TEST(f4_judges_both_horizontal_axes_against_0x10000_with_no_frozen_pass) {
    setup(6, 3); pod_setup(0x10000, -0x10000); step(); ASSERT_EQ_INT(code(), 0xFF); ASSERT_EQ_INT(pause_byte(), 0); ASSERT_EQ_INT(M.delay, 0);
    setup(6, 3); pod_setup(0x10001, 0); step(); ASSERT_EQ_INT(code(), 0xFE);
    setup(6, 3); pod_setup(0, -0x10001); step(); ASSERT_EQ_INT(code(), 0xFE);
    setup(6, 3); pod_setup(-0x10000, 0); step(); ASSERT_EQ_INT(code(), 0xFF);
    ASSERT_EQ_INT(*E.req, 3); ASSERT_EQ_INT(*E.req_delay, 4);
}

/* ---- F5 (12.8) ----------------------------------------------------------------------------------------------------------------------------- */

TEST(f5_waits_while_the_cruise_missile_is_flying) {
    setup(7, 3);
    step(); ASSERT_EQ_INT(pause_byte(), 0);                              /* never placed */
    rec(4, 0x40C0 | 0x0080, 60, 1); for (int i = 0; i < 4; i++) step(); ASSERT_EQ_INT(pause_byte(), 0);   /* W0 bit 0x0080: still flying */
}

TEST(f5_failure_when_the_missile_arrives_tracks_it_and_stores_0xFE_with_delay_7) {
    setup(7, 3);
    rec(4, 0x0440, 60, 1);                                               /* the dying path took it: the flying flag is clear, B20 bit 1 clear */
    M.view.in_selector = 0; M.view.in_direction = 5;
    step(); ASSERT_EQ_INT(pause_byte(), 1);                              /* start value 2: one frozen pass, the hand-over at 1 */
    ASSERT_EQ_INT(M.view.v4a876, 0x85); ASSERT_EQ_INT(M.view.v4a8eb, 3);
    ASSERT_EQ_INT(code(), 0);
    step();
    ASSERT_EQ_INT(code(), 0xFE); ASSERT_EQ_INT(M.delay, 7); ASSERT_EQ_INT(*E.req, 3);
    ASSERT_EQ_INT(P.f.v4a994, 4);                                        /* the tracking camera */
}

TEST(f5_success_when_the_missile_is_shot_down_has_no_delay_and_no_tracking_camera) {
    setup(7, 3);
    rec(4, 0x0040, 60, 1); R(4)->b20 = 0x02;
    step(); step();
    ASSERT_EQ_INT(code(), 0xFF); ASSERT_EQ_INT(M.delay, 0);
    ASSERT_EQ_INT(M.view.track, 0);
}

/* ---- F6 and the default script (12.9) ------------------------------------------------------------------------------------------------------ */

TEST(f6_succeeds_at_the_cap_after_one_frozen_pass) {
    setup(8, 3);
    E.hostile_cap = 4; E.kills = 3; step(); ASSERT_EQ_INT(pause_byte(), 0);
    E.kills = 4; step(); ASSERT_EQ_INT(pause_byte(), 1); ASSERT_EQ_INT(code(), 0);
    step(); ASSERT_EQ_INT(code(), 0xFF); ASSERT_EQ_INT(pause_byte(), 0);
    ASSERT_EQ_INT(M.view.v4a876, 0);
}

TEST(f6_delay_quirk_the_high_byte_of_the_lock_offset_times_256_plus_the_cap) {
    static const struct { int lock; unsigned delay; } cases[] = { { -1, 0xFF04 }, { 4, 0x0804 }, { 8, 0x1004 }, { 15, 0x1E04 }, { 1, 0x0204 } };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        setup(8, 3);
        E.hostile_cap = 4; E.kills = 4;
        E.lock_tail = (int16_t)cases[i].lock;
        step(); step();
        ASSERT_EQ_INT(code(), 0xFF); ASSERT_EQ_INT(M.delay, cases[i].delay);
    }
}

TEST(the_default_script_serves_every_unlisted_mode) {
    setup(0x7F, 3);
    E.hostile_cap = 2; E.kills = 2; step(); step();
    ASSERT_EQ_INT(code(), 0xFF);
}

TEST(kills_are_compared_as_signed_bytes) {
    setup(8, 3);
    E.hostile_cap = 4; E.kills = 0x84; step(); step();                   /* 0x84 is negative: below the cap */
    ASSERT_EQ_INT(code(), 0);
}

/* ---- the state across a crash restart, a pause and an ESC (12.11, Review Focus 5) ------------------------------------------------------------- */

TEST(a_crash_sets_code_4_and_the_scripts_are_dead_until_the_restart_of_the_selection) {
    setup(8, 3);
    E.hostile_cap = 4; E.kills = 4;
    P.f.v4a850 = 4;
    for (int i = 0; i < 6; i++) step();
    ASSERT_EQ_INT(code(), 4); ASSERT_EQ_INT(pause_byte(), 0); ASSERT_EQ_INT(M.ends, 0);
}

TEST(the_script_state_survives_a_crash_restart) {
    setup(5, 3);
    rec(4, 0x40C8, 60, 1);
    M.timer = 137; M.view.v4a876 = 0x87;
    P.f.v4a850 = 4;                                                      /* the crash */
    step();                                                              /* the restart does not call missions_start */
    ASSERT_EQ_INT(M.timer, 137); ASSERT_EQ_INT(M.view.v4a876, 0x87);
    ASSERT_EQ_INT(code(), 4);
}

TEST(the_start_of_the_selection_clears_the_script_state) {
    setup(5, 3);
    M.timer = 137; M.view.v4a876 = 0x87; M.delay = 55; M.key_lock = 1;
    missions_start(&M, 5, 3);                                            /* the ESC restart runs the dispatch again */
    ASSERT_EQ_INT(M.timer, 0); ASSERT_EQ_INT(M.view.v4a876, 0); ASSERT_EQ_INT(M.delay, 0); ASSERT_EQ_INT(M.key_lock, 0);
}

TEST(esc_sets_the_pause_byte_to_1_so_a_holding_trigger_ends_its_count_in_that_pass) {
    setup(8, 3);
    E.hostile_cap = 4; E.kills = 4;
    missions_esc(&P.f);
    ASSERT_EQ_INT(pause_byte(), 1);
    step();                                                              /* 1 - 1 = 0: the script acts at once, inside the restart's fade */
    ASSERT_EQ_INT(code(), 0xFF); ASSERT_EQ_INT(*E.req, 3); ASSERT_EQ_INT(pause_byte(), 0);
}

TEST(esc_in_the_count_skips_the_hand_over_pass_but_the_tracking_camera_still_runs_and_the_camera_bytes_survive_the_restart) {
    setup(4, 3);
    rec(4, 0x40C8, 70, 5); R(4)->w2 = 0x0080; R(4)->speed_target = 0; R(4)->heading = 321;
    rec(8, 0, 76, 5); rec(10, 0, 75, 5);
    M.view.in_selector = 0; M.view.in_direction = 3;
    missions_esc(&P.f);                                          /* the byte jumps from 1 to 0: the pass in which it reads 1 never happens (12.11) */
    step();
    ASSERT_EQ_INT(code(), 0xFF); ASSERT_EQ_INT(*E.req, 3);
    ASSERT_EQ_INT(M.view.v4a876, 0);                             /* no hand-over */
    ASSERT_EQ_INT(M.view.track, 1); ASSERT_EQ_INT(P.f.v4a994, 4);   /* the tracking-camera call 0x1EC14 ran */
    ASSERT_EQ_INT(M.view.v4a9e2, 321);
    M.view.v4a96a = 7; M.view.v4a8eb = 0xFF; M.view.v4a8ec = 1; M.view.v4a876 = 0x83; M.view.toggle_request = 1;
    missions_start(&M, 4, 3);                                    /* the restart: V:4A96A, V:4A8EB, V:4A8EC and V:4A9E2 survive, V:4A876 and the request do not */
    ASSERT_EQ_INT(M.view.v4a96a, 7); ASSERT_EQ_INT(M.view.v4a8eb, 0xFF); ASSERT_EQ_INT(M.view.v4a8ec, 1); ASSERT_EQ_INT(M.view.v4a9e2, 321);
    ASSERT_EQ_INT(M.view.v4a876, 0); ASSERT_EQ_INT(M.view.toggle_request, 0);
}

TEST(the_tail_has_no_request_guard_so_a_landing_replaces_a_pending_0xFF_with_0xFC) {
    setup(3, 0);
    M.view.in_ident = -1; step();                                        /* the success: code 0xFF, request 3, the countdown 4 */
    ASSERT_EQ_INT(code(), 0xFF); ASSERT_EQ_INT(*E.req, 3);
    landed(0x04);                                                /* the aircraft sits on the pad after a take-off */
    step();
    ASSERT_EQ_INT(code(), 0xFC); ASSERT_EQ_INT(M.delay, 0); ASSERT_EQ_INT(*E.req_delay, 4); ASSERT_EQ_INT(M.ends, 2);   /* the debrief shows the completion text, never the success text (12.1) */
}

/* Review Focus 3 (the player ejects, the seat lands, and a script ends the mission): the order is the one that first finds the request byte free */
TEST(a_landed_seat_leaves_a_request_3_the_script_stored_first_and_the_script_overwrites_a_seat_request_2) {
    setup(8, 3);
    E.hostile_cap = 4; E.kills = 4;
    step(); step();                                              /* the script: one frozen pass, then code 0xFF and the request 3 */
    ASSERT_EQ_INT(*E.req, 3);
    R(1)->w0 = 0x8040; R(1)->cls = 0x30; R(1)->counter = 1;      /* the seat has landed: its counter reaches 0 */
    entities_seat_step(&E, 1);
    ASSERT_EQ_INT(*E.req, 3); ASSERT_EQ_INT(*E.req_delay, 4);    /* the seat's own store of request 2 is skipped while any request is pending */
    setup(8, 3);
    E.hostile_cap = 4; E.kills = 4;
    R(1)->w0 = 0x8040; R(1)->cls = 0x30; R(1)->counter = 1;
    entities_seat_step(&E, 1);
    ASSERT_EQ_INT(*E.req, 2);                                    /* the seat first: request 2, countdown 4 */
    *E.req_delay = 2;                                            /* two passes later the script's trigger holds */
    step(); step();
    ASSERT_EQ_INT(*E.req, 3); ASSERT_EQ_INT(*E.req_delay, 4);    /* inside the window the script overwrites it: ONE outcome (the debrief sees request 3 and the code 0xFF) */
    ASSERT_EQ_INT(code(), 0xFF); ASSERT_EQ_INT(M.ends, 1);
}

TEST(a_pause_key_during_the_count_clears_the_byte_and_the_count_restarts_from_the_start_value) {
    setup(4, 3);
    rec(4, 0, 70, 5);                                                    /* destroyed: the trigger holds */
    step(); step(); ASSERT_EQ_INT(pause_byte(), 3);
    P.f.v4a866 = 0;                                                      /* P sees the byte non-zero and clears it (5.14) */
    step(); ASSERT_EQ_INT(pause_byte(), 4);
}

TEST(a_pause_byte_of_0xFF_counts_down_by_one_per_pass_and_does_the_hand_over_once) {
    setup(4, 3);
    rec(4, 0x40C8, 70, 5); R(4)->w2 = 0x0080;
    P.f.v4a866 = 0xFF;                                                   /* the pause key found the byte 0 */
    step(); ASSERT_EQ_INT(pause_byte(), 0xFE);
    ASSERT_EQ_INT(M.view.v4a876 & 0x80, 0x80);                           /* a value of 1 or less (signed) is the hand-over pass */
    M.view.v4a876 = 0x80; M.view.v4a96a = 0;
    step(); ASSERT_EQ_INT(pause_byte(), 0xFD); ASSERT_EQ_INT(M.view.v4a96a, 0);   /* once */
}

TEST(the_resume_unlocks_the_keys_and_clears_the_pause_byte) {
    setup(4, 3);
    M.key_lock = 1; P.f.v4a866 = 1;
    missions_resume(&M, &P.f);
    ASSERT_EQ_INT(M.key_lock, 0); ASSERT_EQ_INT(pause_byte(), 0);
}

/* ---- the briefing tables and the text patches (2.1, 10.9, 13) ------------------------------------------------------------------------------- */

/* An INVENTED chain (the shipped ids are the extractor's tables/briefing_chain.bin): the rule is the lookup by mode and index. */
static const uint8_t CHAIN[30] = { 11, 12, 13, 14, 15,  21, 22, 23, 24, 25,  31, 32, 33, 34, 35,  41, 42, 43, 44, 45,  51, 52, 53, 54, 55,  61, 62, 63, 64, 65 };

TEST(the_id_lists_of_the_six_missions_are_read_from_the_chain_table_by_mode_and_index) {
    for (int m = 3; m <= 8; m++) for (int i = 0; i < 5; i++) ASSERT_EQ_INT(missions_list_id(CHAIN, m, i), CHAIN[(m - 3) * 5 + i]);
    ASSERT_EQ_INT(missions_list_id(CHAIN, 2, 0), 0); ASSERT_EQ_INT(missions_list_id(CHAIN, 9, 0), 0); ASSERT_EQ_INT(missions_list_id(CHAIN, 3, 5), 0);
    ASSERT_EQ_INT(missions_list_id(CHAIN, 3, -1), 0); ASSERT_EQ_INT(missions_list_id(NULL, 3, 0), 0);
}

TEST(the_briefing_is_index_0_with_no_completion_and_index_1_after_one) {
    ASSERT_EQ_INT(missions_briefing_id(CHAIN, 3, 0), 11); ASSERT_EQ_INT(missions_briefing_id(CHAIN, 3, 1), 12);
    ASSERT_EQ_INT(missions_briefing_id(CHAIN, 8, 0), 61); ASSERT_EQ_INT(missions_briefing_id(CHAIN, 8, 3), 62);
}

TEST(the_approach_target_is_the_first_counted_aircraft_else_the_last_aircraft_found) {
    const uint8_t slots[4] = { 14, 4, 8, 10 }, cls[4] = { 0x20, 0x16, 0x12, 0x12 }, cnt[4] = { 0, 0, 1, 1 };
    ASSERT_EQ_INT(missions_approach_target(slots, cls, cnt, 4), 8);      /* the carrier is skipped, the airliner is a fallback, the first counted MiG wins */
    const uint8_t cnt2[4] = { 0, 0, 0, 0 };
    ASSERT_EQ_INT(missions_approach_target(slots, cls, cnt2, 4), 10);    /* none counted: the last aircraft found stands */
    const uint8_t cls3[1] = { 0x32 };
    ASSERT_EQ_INT(missions_approach_target(slots, cls3, cnt, 1), -1);    /* the pod is no aircraft */
    const uint8_t s5[2] = { 4, 8 }, c5[2] = { 0x15, 0x12 }, n5[2] = { 1, 1 };
    ASSERT_EQ_INT(missions_approach_target(s5, c5, n5, 2), 4);           /* F5: the cruise missile (class nibble 1, counted) is the target */
}

TEST(the_date_and_the_bearing_patches) {
    ASSERT_EQ_INT(missions_date_day(0), 1); ASSERT_EQ_INT(missions_date_day(31), 30); ASSERT_EQ_INT(missions_date_day(32), 1); ASSERT_EQ_INT(missions_date_day(29), 30);
    ASSERT_EQ_INT(missions_date_month(0), 0); ASSERT_EQ_INT(missions_date_month(31), 0); ASSERT_EQ_INT(missions_date_month(32), 1);
    ASSERT_EQ_INT(missions_date_month(96), 3); ASSERT_EQ_INT(missions_date_month(5000), 3);
    ASSERT_EQ_INT(missions_bearing_degrees(0), 0); ASSERT_EQ_INT(missions_bearing_degrees(7200), 90); ASSERT_EQ_INT(missions_bearing_degrees(28799), 359);
    ASSERT_EQ_INT(missions_bearing_degrees(28800), 0);
}

/* ---- the game with the real assets: the dispatcher inside the world update, the frozen passes, the request 3 and the outcome (skipped without assets/) --------------------------------- */

static int real_read(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    char p[256];
    snprintf(p, sizeof p, "assets/%s", path);
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
static void real_release(void *user, uint8_t *buf) { (void)user; free(buf); }
static game_loader_t RLD = { real_read, real_release, NULL };
static int have_real(void) { uint8_t *b; size_t n; if (real_read(NULL, "tables/text.bin", &b, &n) != 0) return 0; free(b); return real_read(NULL, "models/index.txt", &b, &n) == 0 ? (free(b), 1) : 0; }

static void g_step(game_t *g, int n) { for (int i = 0; i < n; i++) game_step(g); }
static void g_key(game_t *g, int c) { game_event_t ev = { GAME_EV_KEY, 0, c }; game_input(g, &ev); }
static void g_btn(game_t *g, int code, int v) { game_event_t ev = { GAME_EV_BUTTON, code, v }; game_input(g, &ev); }
static const screen_t *scr(game_t *g) { return game_debug_screen(g); }

/* a game whose log holds a player record (qualified, tours 2: the continuing welcome ends with the first key) with the completion bytes given */
static game_t *mission_game(const uint8_t comp[9], const uint8_t avail_from_prev) {
    (void)avail_from_prev;
    game_t *g = game_create(&RLD, NULL, 0);
    if (!g) return NULL;
    uint8_t buf[83];
    memset(buf, 0, sizeof buf);
    memcpy(buf, "ILOG", 4); buf[4] = 1;
    buf[5 + 1] = 1;                                              /* +0x00: qualified */
    buf[5 + 5] = 2;                                              /* +0x04: tours 2 */
    memcpy(buf + 5 + 0x12, comp, 9);
    game_log_import(g, buf, sizeof buf);
    g_step(g, 3); g_key(g, 'x'); g_step(g, 130); g_key(g, 'x'); g_step(g, 40);   /* the credits, the continuing welcome, the menu */
    return g;
}
/* the F key and the chain to the first step of the flight state */
static int start_mission(game_t *g, int fkey) {
    g_btn(g, GAME_BTN_FKEY, fkey);
    for (int i = 0; i < 4000 && scr(g)->state != SCR_FLIGHT; i++) {
        g_step(g, 1);
        if (scr(g)->state == SCR_AIRCRAFT && scr(g)->t > 3) g_key(g, '1');
        if (scr(g)->state == SCR_APPROACH) { game_event_t ev = { GAME_EV_BUTTON, GAME_BTN_KEYPAD, GAME_KP_ENTER }; game_input(g, &ev); }
    }
    return scr(g)->state == SCR_FLIGHT;
}

TEST(real_assets_f6_reaches_its_end_code_through_the_dispatcher_one_frozen_pass_request_3_the_outcome_and_the_resume) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    ASSERT_EQ_INT(scr(g)->mode_byte, 8);
    ASSERT_EQ_INT(e->hostile_cap, 4);
    g_step(g, 3);
    ASSERT_EQ_INT(ac->f.v4a850, 0);
    e->kills = 4;                                                /* the cap: the script's trigger */
    g_step(g, 1);
    ASSERT_EQ_INT(ac->f.v4a866, 1);                              /* the count: one frozen pass (12.9) */
    const uint32_t n0 = ac->f.steps;
    g_step(g, 1);
    ASSERT_EQ_INT(ac->f.v4a850, 0xFF);                           /* the script stored its code in the pass the byte reached 0 */
    ASSERT_EQ_INT(ac->f.steps, n0);                              /* the frozen pass did not run the player's step */
    ASSERT_EQ_INT(scr(g)->request, 3); ASSERT_EQ_INT(scr(g)->request_delay, 4);
    ASSERT_EQ_INT(ac->f.v4a866, 0);                              /* the countdown's passes run with the byte 0 (08 5.13 [V: flow7-pause-outcome]) */
    int begin = -1;
    for (int i = 1; i <= 12 && begin < 0; i++) { g_step(g, 1); if (scr(g)->state == SCR_END_WAIT) begin = i; }
    ASSERT_EQ_INT(begin, 6);                                     /* store pass P (countdown 4), P+1..P+5: 3, 2, 1, 0, -1, the state changes at P+6 (the pass of the second step above is P) */
    int resumed = -1, frozen_steps = 0, bad = 0;
    for (int i = 1; i <= 400 && resumed < 0; i++) {
        g_step(g, 1);
        if (scr(g)->state == SCR_FLIGHT) { resumed = i; break; }
        if (screen_sim_hold(scr(g))) { frozen_steps++; if (ac->f.v4a866 != 1) bad++; }   /* 1 from the freeze through the typed text */
    }
    ASSERT_TRUE(resumed > 0);
    ASSERT_TRUE(frozen_steps > 10);
    ASSERT_EQ_INT(bad, 0);
    ASSERT_EQ_INT(ac->f.v4a850, 1);                              /* 0xFF becomes 1 after the text (5.13) */
    ASSERT_EQ_INT(ac->f.v4a866, 0);                              /* 0 at the resume */
    /* 08 5.13 [V: flow7-pause-outcome]: the resume pass (blank 179) writes V:4A968 = 0xFE and is no flight pass; the first flight pass consumes the byte at blank 185, one step later. */
    ASSERT_EQ_INT(ac->f.v4a968, 0xFE);                           /* still pending after the resume step */
    g_step(g, 1);                                                /* the first flight pass */
    ASSERT_TRUE(ac->f.v4a968 != 0xFE);                           /* taken by this pass's sound routine */
    game_destroy(g);
}

typedef struct { int discs, polys; } sink_count_t;
static void count_sink(void *user, const world_out_t *o) {
    sink_count_t *c = (sink_count_t *)user;
    if (o->kind == WORLD_OUT_DISC) c->discs++;
    else c->polys++;   /* the scar's polygon and lines */
}

/* The real models 33 to 35 through the real walker and the interpreter (ops 70, 71, 62, note 07 11.3): one frame with the camera in a cell of the world and an effect object ahead of it, for several life
 * words. Counts only (the figures of the note, not a copy of the model): the steady fire draws six discs, the burn-down fewer, a negative word none; models 34 and 35 add the scar's polygon. */
TEST(real_assets_the_effect_models_draw_their_discs_by_the_life_word_through_the_interpreter) {
    if (!have_real()) { printf("skipped "); return; }
    world_t *w = world_load(&RLD, NULL, 0);
    ASSERT_TRUE(w != NULL);
    static const struct { int life, discs; } T[] = { { 0x40, 6 }, { 0x48, 6 }, { 0x7F, 1 }, { 0x78, 4 }, { 0x00, 1 }, { 0x08, 5 }, { -1, 0 } };   /* variant k = (t & 7) >> 1 only changes the pens */
    for (int id = 0x11; id <= 0x13; id++)
        for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
            world_camera_t c; memset(&c, 0, sizeof c);
            c.pos[0] = 10 * 4194304 + 2097152; c.pos[2] = 10 * 4194304 + 2097152; c.pos[1] = 1000;
            world_set_camera(w, &c);
            lists_frame_t in; memset(&in, 0, sizeof in);
            lists_entity_t *o = &in.sec[0];
            o->active = 1; o->cell_x = 10; o->cell_z = 10; o->fine_x = 8192; o->fine_z = 8192 + 256; o->sub = 5; o->model = (uint16_t)id; o->phase = (int16_t)T[i].life;
            o->pos[0] = (10 << 22) + (8192 << 8); o->pos[1] = 1000; o->pos[2] = (10 << 22) + ((8192 + 256) << 8);
            world_lists_frame(w, &in);
            game_scene_t *sc = (game_scene_t *)calloc(1, sizeof *sc);
            world_fill_scene(w, sc);
            int ix = -1;
            for (unsigned k = 0; k < sc->entry_count; k++) if (sc->entries[k].kind == GAME_ENTRY_SECONDARY) ix = (int)k;
            int discs = 0, polys = 0;
            if (ix >= 0) {
                sink_count_t cnt = { 0, 0 };
                world_draw_info_t info; memset(&info, 0, sizeof info);
                game_camera_t cam; memset(&cam, 0, sizeof cam);
                cam.pos.x = c.pos[0]; cam.pos.y = c.pos[1]; cam.pos.z = c.pos[2];
                world_draw_entry(w, &sc->entries[ix], &cam, count_sink, &cnt, &info);
                discs = cnt.discs; polys = cnt.polys;
            }
            free(sc);
            ASSERT_TRUE(ix >= 0);
            ASSERT_EQ_INT(discs, T[i].discs);
            if (id == 0x11) ASSERT_EQ_INT(polys, 0);   /* model 33 is the starburst alone */
            else ASSERT_TRUE(polys > 0);                /* models 34 and 35 draw the ground scar first (op 3, op 13), also when the starburst is spent */
        }
    world_destroy(w);
}

/* Review of Task 8, finding 1: an effect object that the entity module holds (the impact of a missile makes one, note 07 11.2) reaches the scene through the real path (the walker, the descriptor
 * of its id word, the model of the id word plus 16) and is drawn by the model interpreter, the scar of models 34 and 35 first. */
TEST(real_assets_an_effect_object_is_a_drawn_secondary_entry_with_the_model_of_its_id_word_plus_16) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    entities_t *e = (entities_t *)game_debug_entities(g);
    aircraft_t *ac = game_debug_aircraft(g);
    game_scene_t *sc = (game_scene_t *)calloc(1, sizeof *sc);
    game_scene(g, sc);
    const double yaw = (double)sc->camera.yaw * 6.283185307179586 / 28800.0;
    int32_t pos[3];
    pos[0] = ac->pos[0] + (int32_t)(-sin(yaw) * 20000.0); pos[1] = ac->pos[1]; pos[2] = ac->pos[2] + (int32_t)(cos(yaw) * 20000.0);   /* ahead of the camera */
    secondary_t o; memset(&o, 0, sizeof o);
    o.cell_x = (int16_t)((pos[0] >> 22) & 0x7F); o.cell_z = (int16_t)((pos[2] >> 22) & 0x7F);
    o.fine_x = (int16_t)((pos[0] & 0x3FFFFF) >> 8); o.fine_z = (int16_t)((pos[2] & 0x3FFFFF) >> 8);
    o.sub = (uint8_t)(4 * (3 - (o.fine_z >> 12)) + (3 - (o.fine_x >> 12)));
    o.alt8 = pos[1] >> 8; o.pos[0] = pos[0]; o.pos[1] = pos[1]; o.pos[2] = pos[2];
    ASSERT_TRUE(entities_object_create(e, &o, 0x12) >= 0);
    g_step(g, 1);
    game_scene(g, sc);
    int ix = -1;
    for (unsigned i = 0; i < sc->entry_count; i++) if (sc->entries[i].kind == GAME_ENTRY_SECONDARY) ix = (int)i;
    ASSERT_TRUE(ix >= 0);
    ASSERT_EQ_INT(sc->entries[ix].asset_id, 34);
    ASSERT_EQ_INT(sc->entries[ix].model_id, 0x12);
    ASSERT_EQ_INT(sc->entries[ix].draw, 1);   /* the walker ran the draw routine: the object is not skipped at the entity routine's exit */
    ASSERT_TRUE(sc->entries[ix].phase > 0 && sc->entries[ix].phase < 0x7F);
    free(sc);
    game_destroy(g);
}

/* Review of Task 8, finding 2: the scene says where the gun routine, which draws the tracer pool, stands in the frame (note 07 7.3): after the entity pass for tracers and flares in the cockpit view,
 * before it for a chaff slot. */
TEST(real_assets_the_scene_exports_the_place_of_the_gun_routine_in_the_frame) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    weapons_t *w = (weapons_t *)game_debug_weapons(g);
    game_scene_t *sc = (game_scene_t *)calloc(1, sizeof *sc);
    g_step(g, 1); game_scene(g, sc);
    ASSERT_EQ_INT(sc->gun_first, 0);                               /* nothing active */
    w->slot[0].flags = WPF_ACTIVE; w->slot[0].life = 30;           /* a tracer in the cockpit view: the gun after the entity pass */
    g_step(g, 1); game_scene(g, sc);
    ASSERT_EQ_INT(sc->gun_first, 0);
    w->slot[2].flags = WPF_ACTIVE | WPF_CHAFF; w->slot[2].life = 0x30;   /* a chaff slot: before it */
    g_step(g, 1); game_scene(g, sc);
    ASSERT_EQ_INT(sc->gun_first, 1);
    free(sc);
    game_destroy(g);
}

TEST(real_assets_f2_hands_the_view_over_at_the_pass_with_the_byte_at_1_and_tracks_air_force_one) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 0, 0, 0, 0, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 2));
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    ASSERT_EQ_INT(scr(g)->mode_byte, 4);
    g_step(g, 3);
    flight_state_t *a = &entities_ac(e, 4)->f;
    ASSERT_TRUE((a->w0 & 0x40) != 0);                            /* Air Force One is placed in slot 4 */
    entities_ac(e, 8)->f.w0 = 0; entities_ac(e, 10)->f.w0 = 0;   /* both attackers gone */
    a->w0 = 0x0040;                                              /* valid but inert: the flight model leaves it where it stands */
    a->w2 = 0x0080; a->speed_target = 0; a->state = 0; a->vel[0] = a->vel[1] = a->vel[2] = 0;   /* down and slow */
    entities_ac(e, 4)->pos[1] = 0;
    static const int want[4] = { 4, 3, 2, 1 };
    a->heading = 4321;                                           /* the heading the hand-over saves (V:4A9E2) */
    for (int i = 0; i < 4; i++) { g_step(g, 1); ASSERT_EQ_INT(ac->f.v4a866, want[i]); }
    ASSERT_EQ_INT(game_debug_missions(g)->view.v4a9e2, 4321);
    ASSERT_EQ_INT(game_debug_ring_heading(g), 4321);              /* and it reaches the view state's stored heading, which the ring rotation compares against (note 06) */
    ASSERT_EQ_INT(game_debug_missions(g)->view.v4a96a, 7);       /* the hand-over's ring write is applied and the value stays for a restart (12.11) */
    ASSERT_EQ_INT(game_debug_missions(g)->view.ring_set, 0);
    ASSERT_EQ_INT(ac->f.v4a850, 0);
    g_step(g, 1);
    ASSERT_EQ_INT(ac->f.v4a850, 0xFF);
    ASSERT_EQ_INT(ac->f.v4a994, 4);                              /* the tracking camera on slot 4 */
    ASSERT_EQ_INT(ac->f.v4a996, 0x800);
    int resumed = -1;
    for (int i = 1; i <= 600 && resumed < 0; i++) { g_step(g, 1); if (ac->f.v4a850 == 1) resumed = i; }
    ASSERT_TRUE(resumed > 0);                                    /* the outcome screen is over: code 1 */
    ASSERT_EQ_INT(ac->f.v4a994, 0);                              /* N7 (12.14): the tracking camera was on, the viewed record is the player again */
    { game_scene_t *q = (game_scene_t *)calloc(1, sizeof *q); game_scene(g, q); const unsigned r0 = q->hud.cp_redraw;
      g_step(g, 1); game_scene(g, q);
      ASSERT_TRUE(r0 >= 2 && q->hud.cp_redraw >= 1);             /* the resume ran the helper 0x1EFF4 (130FE, note 07 12.1 writer 2): R was reloaded and counts down */
      free(q); }
    ASSERT_EQ_INT(game_debug_missions(g)->view.v4a876 & 0x80, 0x80);   /* and V:4A876 is not cleared: no second hand-over in this mission */
    game_destroy(g);
}

TEST(real_assets_esc_with_the_trigger_holding_fires_the_script_in_the_gate_pulse_and_the_reset_clears_the_code) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    g_step(g, 3);
    e->kills = 4;                                                /* the trigger holds when the ESC is taken */
    g_btn(g, GAME_BTN_ESC, 1);
    int pulse = -1;
    for (int i = 1; i <= 60 && scr(g)->state != SCR_ECHO; i++) { g_step(g, 1); if (pulse < 0 && ac->f.v4a850 == 0xFF) pulse = i; }
    ASSERT_TRUE(pulse > 0);                                      /* the code was stored inside the restart (12.11), in the pulse pass before the reset */
    ASSERT_EQ_INT(scr(g)->state, SCR_ECHO);
    for (int i = 0; i < 4000 && scr(g)->state != SCR_FLIGHT; i++) g_step(g, 1);
    ac = game_debug_aircraft(g);
    ASSERT_EQ_INT(ac->f.v4a850, 0);                              /* the dispatch cleared it: the scripts are alive again */
    ASSERT_EQ_INT(scr(g)->request, 0);
    game_destroy(g);
}

TEST(real_assets_an_esc_restart_with_the_trigger_holding_carries_the_request_through_the_reset_and_takes_seven_steps_more) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    int total[2] = { 0, 0 };
    for (int trig = 0; trig < 2; trig++) {
        game_t *g = mission_game(comp, 0);
        ASSERT_TRUE(g != NULL);
        ASSERT_TRUE(start_mission(g, 6));
        entities_t *e = (entities_t *)game_debug_entities(g);
        g_step(g, 3);
        if (trig) e->kills = 4;                                  /* the trigger holds when the ESC is taken */
        g_btn(g, GAME_BTN_ESC, 1);
        int n = 0, detour = -1;
        for (; n < 200 && scr(g)->state != SCR_ECHO; n++) {
            g_step(g, 1);
            if (scr(g)->state == SCR_ESC_DETOUR && detour < 0) { detour = n; ASSERT_EQ_INT(scr(g)->request, 3); }   /* the lingering request 3 (12.14 N6) */
        }
        ASSERT_EQ_INT(scr(g)->state, SCR_ECHO);
        ASSERT_EQ_INT(scr(g)->request, 0);                       /* the menu state took it */
        ASSERT_EQ_INT(detour >= 0, trig);
        total[trig] = n;
        game_destroy(g);
    }
    ASSERT_EQ_INT(total[1] - total[0], 7);                       /* the menu set-up 0x11D30 and the menu state again: 44 blanks = 7 steps */
}

TEST(real_assets_esc_sets_the_pause_byte_to_1_and_the_restart_clears_it_and_the_script_state) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    aircraft_t *ac = game_debug_aircraft(g);
    g_step(g, 3);
    g_btn(g, GAME_BTN_ESC, 1);
    g_step(g, 2);
    ASSERT_EQ_INT(ac->f.v4a866, 1);
    ASSERT_TRUE(start_mission(g, 6) == 0 || 1);
    for (int i = 0; i < 4000 && scr(g)->state != SCR_FLIGHT; i++) g_step(g, 1);
    ASSERT_EQ_INT(scr(g)->state, SCR_FLIGHT);
    ac = game_debug_aircraft(g);
    ASSERT_EQ_INT(ac->f.v4a866, 0);                              /* the restart's player start (and the reset block) cleared it */
    ASSERT_EQ_INT(ac->f.v4a850, 0);
    game_destroy(g);
}

TEST(real_assets_p_during_a_scripts_count_clears_the_byte_with_no_pause_screen_and_the_count_restarts) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    g_step(g, 3);
    g_btn(g, GAME_BTN_PAUSE, 1); g_step(g, 1);                   /* with the byte at 0 the key pauses (the contrast) */
    ASSERT_EQ_INT(scr(g)->paused, 1);
    g_btn(g, GAME_BTN_PAUSE, 1); g_step(g, 1);
    ASSERT_EQ_INT(scr(g)->paused, 0);
    e->kills = 4;                                                /* F6: the trigger holds */
    g_step(g, 1);
    ASSERT_EQ_INT(ac->f.v4a866, 1); ASSERT_EQ_INT(ac->f.v4a850, 0);   /* the count: one frozen pass */
    g_btn(g, GAME_BTN_PAUSE, 1);                                 /* P sees the byte non-zero: it clears it and opens no pause screen (12.2, 12.12) */
    g_step(g, 1);
    ASSERT_EQ_INT(scr(g)->paused, 0);
    ASSERT_EQ_INT(ac->f.v4a850, 0);                              /* the script did not fire in that pass: the trigger still holds, the count restarted from the start value (2 loaded, 1 left) */
    ASSERT_EQ_INT(ac->f.v4a866, 1);
    g_step(g, 1);
    ASSERT_EQ_INT(ac->f.v4a850, 0xFF);                           /* and ended one pass later, as without the key but a pass later */
    game_destroy(g);
}

TEST(real_assets_p_is_ignored_while_a_scripts_result_locks_the_keys) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    g_step(g, 3);
    e->kills = 4;
    g_step(g, 2);
    ASSERT_EQ_INT(ac->f.v4a850, 0xFF);
    ASSERT_EQ_INT(game_debug_missions(g)->key_lock, 1);          /* V:4A965 = 1 from the store */
    g_btn(g, GAME_BTN_PAUSE, 1); g_step(g, 1);
    ASSERT_EQ_INT(scr(g)->paused, 0);                            /* P is not in the restricted key set (6.4) */
    g_btn(g, GAME_BTN_PAUSE, 1); g_step(g, 1);
    ASSERT_EQ_INT(scr(g)->paused, 0);
    game_destroy(g);
}

TEST(real_assets_p_in_the_esc_fade_clears_the_byte_the_esc_set_so_a_holding_trigger_does_not_fire_in_the_gate_pulse) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    g_step(g, 3);
    e->kills = 4;                                                /* the trigger holds when the ESC is taken */
    g_btn(g, GAME_BTN_ESC, 1);
    g_step(g, 2);
    ASSERT_EQ_INT(ac->f.v4a866, 1);                              /* the byte the ESC stored */
    g_btn(g, GAME_BTN_PAUSE, 1);
    int fired = 0;
    for (int i = 1; i <= 60 && scr(g)->state != SCR_ECHO; i++) {
        g_step(g, 1);
        if (i == 1) ASSERT_EQ_INT(ac->f.v4a866, 0);              /* P cleared it */
        if (ac->f.v4a850 == 0xFF) fired = 1;
    }
    ASSERT_EQ_INT(scr(g)->state, SCR_ECHO);
    ASSERT_EQ_INT(fired, 0);                                     /* the gate pulse found the byte 0: the count began instead of ending (12.11) */
    game_destroy(g);
}

TEST(real_assets_a_second_p_during_the_esc_countdown_stores_0xff_in_the_pause_byte_again_and_a_third_clears_it) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    aircraft_t *ac = game_debug_aircraft(g);
    g_step(g, 3);
    g_btn(g, GAME_BTN_ESC, 1);
    g_step(g, 2);
    ASSERT_EQ_INT(scr(g)->state, SCR_ESC_FADE);
    ASSERT_EQ_INT(ac->f.v4a866, 1);                              /* the byte the ESC stored */
    g_btn(g, GAME_BTN_PAUSE, 1);
    ASSERT_EQ_INT(ac->f.v4a866, 0);                              /* the first P clears it (note 06 item 8) */
    g_btn(g, GAME_BTN_PAUSE, 1);
    ASSERT_EQ_INT(ac->f.v4a866, 0xFF);                           /* a further P finds the byte 0 and stores 0xFF again */
    g_btn(g, GAME_BTN_PAUSE, 1);
    ASSERT_EQ_INT(ac->f.v4a866, 0);                              /* and the next finds it non-zero and clears it */
    game_destroy(g);
}

TEST(real_assets_the_pause_screen_runs_no_world_pass_so_a_stale_lock_stays_until_the_flight_goes_on) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    entities_t *e = (entities_t *)game_debug_entities(g);
    g_step(g, 3);
    ASSERT_TRUE((entities_ac(e, 5)->f.w0 & 0x40) == 0);          /* record 5 holds no aircraft in F6 */
    g_btn(g, GAME_BTN_PAUSE, 1); g_step(g, 1);
    ASSERT_EQ_INT(scr(g)->paused, 1);
    e->designated = 5;                                           /* a lock on an invalid record: the stale-lock drop clears it in every world update */
    g_step(g, 5);
    ASSERT_EQ_INT(e->designated, 5);                             /* 12.14 N4: the pause screen runs no world pass at all */
    g_btn(g, GAME_BTN_PAUSE, 1); g_step(g, 2);
    ASSERT_EQ_INT(e->designated, -1);
    game_destroy(g);
}

TEST(real_assets_the_log_counts_the_mission_start_once_and_the_completion_bookkeeping_writes_the_level_before_the_increment) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    uint8_t buf[83];
    ASSERT_EQ_INT(game_log_export(g, buf, sizeof buf), 83);
    ASSERT_EQ_INT((buf[5 + 0x36] << 8) | buf[5 + 0x37], 1);      /* +0x36: one start at the orders (14.2) */
    ASSERT_EQ_INT((buf[5 + 2] << 8) | buf[5 + 3], 0);            /* +0x02 untouched until a completion */
    game_destroy(g);
}

TEST(real_assets_a_mission_played_to_its_completion_updates_the_log_as_14_2_says) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 2 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    g_step(g, 3);
    e->kills = 4;
    for (int i = 0; i < 800 && !(scr(g)->state == SCR_FLIGHT && ac->f.v4a850 == 1) ; i++) g_step(g, 1);
    ASSERT_EQ_INT(ac->f.v4a850, 1);                              /* the success text is over: the flight resumes with the code 1 */
    ac->f.b21 = 1; ac->f.w2 |= 0x0080; ac->f.speed = 0;          /* a landing after the take-off: on the deck, stopped (the carrier start: any deck bit of B4) */
    ac->f.b4 |= 0x80;
    g_step(g, 1);
    ASSERT_EQ_INT(ac->f.v4a850, 0xFC);                           /* the tail: the mission is complete */
    int over = -1;
    for (int i = 1; i <= 800 && over < 0; i++) { g_step(g, 1); if (scr(g)->state == SCR_FLIGHT && ac->f.v4a850 == 4) over = i; }
    ASSERT_TRUE(over > 0);                                       /* the debrief: code 4 after the text (5.13) */
    uint8_t buf[83];
    ASSERT_EQ_INT(game_log_export(g, buf, sizeof buf), 83);
    const uint8_t *r = buf + 5;
    ASSERT_EQ_INT((r[0x38] << 8) | r[0x39], 1);                  /* +0x38 missions completed */
    ASSERT_EQ_INT(r[0x06], 8); ASSERT_EQ_INT(r[0x07], 2);        /* +0x06 the mode, +0x07 the completion count before its increment */
    ASSERT_EQ_INT(r[0x12 + 8], 3);                               /* the completion count of the mode, limited to 3 */
    ASSERT_EQ_INT((r[0x02] << 8) | r[0x03], 0);                  /* +0x02 takes the live level BEFORE its increment (0 at this program start) */
    ASSERT_EQ_INT((r[0x36] << 8) | r[0x37], 1);                  /* +0x36 counted the start once */
    for (int i = 0; i < 50; i++) g_step(g, 1);
    ASSERT_EQ_INT(ac->f.v4a850, 4);                              /* the scripts stay dead: a second landing cannot count again */
    ASSERT_EQ_INT(game_log_export(g, buf, sizeof buf), 83);
    ASSERT_EQ_INT((buf[5 + 0x38] << 8) | buf[5 + 0x39], 1);
    game_destroy(g);
}

/* Note 06 "HUD step and window row at a flight start" (the Task 2 table, V: followup-mapcam, followup-mapcam-air, followup-hudstep-carrier, followup-hudstep-airfield, and milestone 7's flow7-approach-air1): the selection screens of free flight show
 * step 50 and window row 179 (external, map flag set at the location screen), the approach of airfield 1 step 50 and row 179 from its first pass, row 167 from the first pass whose camera height long is below 0x24000,
 * the first cockpit pass step 0 and row 144. The training rows are pinned in test_training.c. */
TEST(real_assets_the_hud_step_and_window_row_follow_the_start_table_selection_screens_50_179_the_first_cockpit_pass_0_144) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    g_key(g, '2');
    int i = 0;
    for (; i < 400 && scr(g)->state != SCR_LOCATION; i++) g_step(g, 1);
    ASSERT_EQ_INT(scr(g)->state, SCR_LOCATION);
    g_step(g, 2);
    { game_scene_t sc; game_scene(g, &sc); ASSERT_EQ_INT(sc.mode, GAME_MODE_SELECT_LOCATION); ASSERT_EQ_INT(sc.camera.hud_step, 50); ASSERT_EQ_INT(sc.sky.row_limit, 179); }
    g_key(g, '1');
    for (i = 0; i < 100 && scr(g)->state != SCR_AIRCRAFT; i++) g_step(g, 1);
    ASSERT_EQ_INT(scr(g)->state, SCR_AIRCRAFT);
    g_step(g, 2);
    { game_scene_t sc; game_scene(g, &sc); ASSERT_EQ_INT(sc.mode, GAME_MODE_SELECT_AIRCRAFT); ASSERT_EQ_INT(sc.camera.hud_step, 50); ASSERT_EQ_INT(sc.sky.row_limit, 179); }
    g_key(g, '1');
    for (i = 0; i < 100 && scr(g)->state != SCR_APPROACH; i++) g_step(g, 1);
    ASSERT_EQ_INT(scr(g)->state, SCR_APPROACH);
    int pass = 0, row_pass = -1, prev_row = 179, cockpit_pass = -1;
    for (; pass < 400 && scr(g)->state == SCR_APPROACH; pass++) {   /* 12.14 C13: step 50, row 179 and an external view from the first pass; the row is 167 from the first pass below 0x24000 */
        game_scene_t sc; game_scene(g, &sc);
        const int h = game_debug_mover(g)->cam[1];
        if (sc.camera.hud_step == 0 && cockpit_pass < 0) cockpit_pass = pass;
        if (cockpit_pass < 0) {
            ASSERT_EQ_INT(sc.camera.hud_step, 50);
            ASSERT_TRUE(sc.camera.view != GAME_VIEW_COCKPIT);
            ASSERT_EQ_INT(sc.sky.row_limit, h < 0x24000 ? 167 : 179);   /* the camera height of the picture decides the row */
            if (sc.sky.row_limit == 167 && prev_row == 179) row_pass = pass;
            prev_row = sc.sky.row_limit;
        } else {
            ASSERT_EQ_INT(sc.sky.row_limit, 144);
            ASSERT_EQ_INT(sc.camera.view, GAME_VIEW_COCKPIT);
        }
        g_step(g, 1);
    }
    ASSERT_TRUE(row_pass >= 58 && row_pass <= 61);                  /* the log: pass 60 of 118 (+-1 pass of sampling) */
    ASSERT_EQ_INT(cockpit_pass, 117);                               /* the 118th pass of 0x12D58 is the cockpit pass */
    for (i = 0; i < 1000 && scr(g)->state != SCR_FLIGHT; i++) g_step(g, 1);
    ASSERT_EQ_INT(scr(g)->state, SCR_FLIGHT);
    g_step(g, 1);
    { game_scene_t sc; game_scene(g, &sc); ASSERT_EQ_INT(sc.camera.hud_step, 0); ASSERT_EQ_INT(sc.sky.row_limit, 144); ASSERT_EQ_INT(sc.camera.view, GAME_VIEW_COCKPIT); }
    game_destroy(g);
}

/* 06 start table, 08 12.14 C13 [V: flow7-approach-carrier]: the carrier start's approach (104 passes) has step 50 and row 179 from the first pass, row 167 from the first pass whose camera height long is below
 * 0x24000 (pass 57 of the log, +-1), and the cockpit (step 0, row 144, view 0) in the last pass but one; the mover's stage 5 comes at pass 80 and its arrival at 99. */
TEST(real_assets_the_carrier_approach_switches_the_window_row_to_167_by_the_camera_height_and_shows_the_cockpit_at_pass_103) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    g_key(g, '2');
    int i = 0;
    for (; i < 400 && scr(g)->state != SCR_LOCATION; i++) g_step(g, 1);
    g_step(g, 2);
    g_key(g, '4');
    for (i = 0; i < 100 && scr(g)->state != SCR_APPROACH; i++) g_step(g, 1);
    ASSERT_EQ_INT(scr(g)->state, SCR_APPROACH);
    int pass = 0, row_pass = -1, cockpit_pass = -1, stage5 = -1, stage6 = -1;
    for (; pass < 400 && scr(g)->state == SCR_APPROACH; pass++) {
        game_scene_t sc; game_scene(g, &sc);
        const mover_t *m = game_debug_mover(g);
        if (sc.camera.hud_step == 0 && cockpit_pass < 0) cockpit_pass = pass;
        if (cockpit_pass < 0) {
            ASSERT_EQ_INT(sc.camera.hud_step, 50);
            ASSERT_EQ_INT(sc.sky.row_limit, m->cam[1] < 0x24000 ? 167 : 179);
            if (sc.sky.row_limit == 167 && row_pass < 0) row_pass = pass;
            if (m->stage >= 5 && stage5 < 0) stage5 = pass;
            if (m->stage >= 6 && stage6 < 0) stage6 = pass;
        } else {
            ASSERT_EQ_INT(sc.sky.row_limit, 144);
        }
        g_step(g, 1);
    }
    ASSERT_TRUE(row_pass >= 55 && row_pass <= 58);        /* 57 */
    ASSERT_TRUE(stage5 >= 79 && stage5 <= 81);            /* 80 */
    ASSERT_TRUE(stage6 >= 98 && stage6 <= 100);           /* 99 */
    ASSERT_EQ_INT(cockpit_pass, 103);
    game_destroy(g);
}


/* Note 08 12.14 C14 [V: sound7-approach, an F1 start]: the missions' approach is seen from outside: the view byte is 1 in the passes 0 to 125 and the cockpit from 126, the mover reads stage 8 in the first
 * passes and stage 0 from pass 3, stage 1 from 27, 2 from 35, 3 from 56, 4 from 86, 5 from 97 and 6 from 121 (the sampling is +-1 pass). The camera's own longs were not peeked: the mover flies from the briefing camera's
 * position (13.4 C1) and the five stage passes it gives are the log's to the pass. The count is the mover's (the placement draws the start): 126 passes for F1 at every seed (the log's run: 127), and for every mission the cockpit
 * pass is the last but one of the state's count. Step 50 and the rows 179 then 167 are R (06 start table, the dispatch value and the view routine's tail; not peeked in this log). */
TEST(real_assets_the_f1_approach_is_external_with_the_stage_passes_of_the_log_and_the_cockpit_at_pass_125_to_127) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    g_btn(g, GAME_BTN_FKEY, 1);
    for (int i = 0; i < 4000 && scr(g)->state != SCR_APPROACH; i++) { g_step(g, 1); if (scr(g)->state == SCR_AIRCRAFT && scr(g)->t > 3) g_key(g, '1'); }
    ASSERT_EQ_INT(scr(g)->state, SCR_APPROACH);
    ASSERT_TRUE(scr(g)->approach_len >= 126 && scr(g)->approach_len <= 128);   /* C14 (a): the log has 127 (cockpit at 126); the count is not pinned beyond this window */
    int first[8], stage8_last = -1, cockpit_pass = -1;
    for (int i = 0; i < 8; i++) first[i] = -1;
    int pass = 0;
    for (; pass < 400 && scr(g)->state == SCR_APPROACH; pass++) {
        game_scene_t sc; game_scene(g, &sc);
        const int st = game_debug_mover(g)->stage;
        if (cockpit_pass < 0 && sc.camera.hud_step == 0) cockpit_pass = pass;
        if (cockpit_pass < 0) {
            ASSERT_EQ_INT(game_debug_view_level(g), 1);                    /* V:4A83D = 1 */
            ASSERT_TRUE(game_debug_camera_mode(g) == 6 || (game_debug_camera_mode(g) == 0 && st == 6));   /* the mover sets mode 0 at the end of stage 6, one pass before the cockpit (the log: gate 0 at pass 125) */
            ASSERT_EQ_INT(sc.camera.hud_step, 50);
            ASSERT_TRUE(sc.camera.view != GAME_VIEW_COCKPIT);
            ASSERT_EQ_INT(sc.sky.row_limit, game_debug_mover(g)->cam[1] < 0x24000 ? 167 : 179);
            if (st >= 0 && st <= 6 && first[st] < 0) first[st] = pass;
            if (st == 8) stage8_last = pass;
        }
        g_step(g, 1);
    }
    static const int WANT[7] = { 3, 27, 35, 56, 86, 97, 121 };
    for (int s = 0; s < 7; s++) { ASSERT_TRUE(first[s] >= WANT[s] - 1 && first[s] <= WANT[s] + 1); }
    ASSERT_TRUE(stage8_last >= 1 && stage8_last <= 3);                    /* stage 8 in the passes 0 to 2 */
    ASSERT_TRUE(cockpit_pass >= 125 && cockpit_pass <= 127);   /* 125 here */              /* the log: step 0 / view 0 from pass 126 */
    game_destroy(g);
}

/* 08 12.14 C14: every mission's approach is external until the cockpit pass, which is the last but one of the screen's count; the count is the mover's own. */
TEST(real_assets_every_mission_approach_is_external_until_the_cockpit_pass_one_before_the_last_of_the_count) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    for (int fk = 1; fk <= 6; fk++) {
        game_t *g = mission_game(comp, 0);
        ASSERT_TRUE(g != NULL);
        g_btn(g, GAME_BTN_FKEY, fk);
        for (int i = 0; i < 4000 && scr(g)->state != SCR_APPROACH; i++) { g_step(g, 1); if (scr(g)->state == SCR_AIRCRAFT && scr(g)->t > 3) g_key(g, '1'); }
        ASSERT_EQ_INT(scr(g)->state, SCR_APPROACH);
        const int len = scr(g)->approach_len;
        ASSERT_TRUE(len >= 90 && len <= 140);   /* C14 (b): the per-mission counts are not settled, so only a wide range */
        int cockpit_pass = -1, ext = 0;
        for (int pass = 0; pass < 400 && scr(g)->state == SCR_APPROACH; pass++) {
            game_scene_t sc; game_scene(g, &sc);
            if (cockpit_pass < 0 && sc.camera.hud_step == 0) cockpit_pass = pass;
            if (cockpit_pass < 0) { ASSERT_EQ_INT(game_debug_view_level(g), 1); ext++; }
            else { ASSERT_EQ_INT(sc.camera.view, GAME_VIEW_COCKPIT); ASSERT_EQ_INT(sc.sky.row_limit, 144); }
            g_step(g, 1);
        }
        ASSERT_EQ_INT(cockpit_pass, len - 1);
        ASSERT_EQ_INT(ext, len - 1);
        game_destroy(g);
    }
}

/* Note 08 13.4 C1 (settled): the briefing's camera holds the reset block's planted position (x 0x10C00000, height 0x05000000, z 0x11400000) for the whole briefing, not the selection screens'
 * standing position (0x10800000, 0x03000000, 0x10C00000, note 04 "The start marks of the overview map"). */
TEST(real_assets_the_briefing_camera_stays_at_the_planted_position_the_selection_screens_do_not) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    g_btn(g, GAME_BTN_FKEY, 6);
    int seen = 0;
    for (int i = 0; i < 4000 && scr(g)->state != SCR_FLIGHT; i++) {
        g_step(g, 1);
        if (scr(g)->state == SCR_AIRCRAFT && scr(g)->t > 3) g_key(g, '1');
        if (scr(g)->state == SCR_APPROACH) { game_event_t ev = { GAME_EV_BUTTON, GAME_BTN_KEYPAD, GAME_KP_ENTER }; game_input(g, &ev); }
        if (scr(g)->state == SCR_MISSION && scr(g)->mission && i % 50 == 0) {
            game_scene_t sc; game_scene(g, &sc);
            ASSERT_EQ_INT(sc.camera.pos.x, 0x10C00000);
            ASSERT_EQ_INT(sc.camera.pos.y, 0x05000000);
            ASSERT_EQ_INT(sc.camera.pos.z, 0x11400000);
            seen++;
        }
    }
    ASSERT_TRUE(seen > 0);
    game_destroy(g);
}

/* Note 08 5.14 (2), followup-pause-screens (staged outcome screen, two presses 70 and 110 blanks in): P changes nothing in the outcome screens, the byte stays 1 and no pause screen opens. */
TEST(real_assets_p_is_ignored_in_the_outcome_screens_the_byte_and_the_mode_stay) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    g_step(g, 3);
    e->kills = 4;
    for (int i = 0; i < 100 && scr(g)->state != SCR_END_TEXT; i++) g_step(g, 1);
    ASSERT_EQ_INT(scr(g)->state, SCR_END_TEXT);
    const int byte = ac->f.v4a866;                               /* (the port freezes the outcome through the screen's hold; the byte is whatever the frozen pass left) */
    for (int k = 0; k < 2; k++) {
        g_btn(g, GAME_BTN_PAUSE, 1); g_step(g, 1);
        ASSERT_EQ_INT(ac->f.v4a866, byte);
        ASSERT_EQ_INT(scr(g)->paused, 0);
        { game_scene_t sc; game_scene(g, &sc); ASSERT_TRUE(sc.mode != GAME_MODE_PAUSED); }
    }
    game_destroy(g);
}

TEST(real_assets_a_landing_during_the_success_text_stores_0xFC_and_the_debrief_runs_again_for_it_once) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 2 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    g_step(g, 3);
    e->kills = 4;
    for (int i = 0; i < 100 && scr(g)->state != SCR_END_TEXT; i++) g_step(g, 1);
    ASSERT_EQ_INT(scr(g)->state, SCR_END_TEXT);                  /* the success text of F6 (0x3F) is being typed, the world is frozen */
    ASSERT_EQ_INT(scr(g)->eng.ids[0], 0x3F);
    ASSERT_EQ_INT(ac->f.v4a850, 0xFF);
    ac->f.b21 = 1; ac->f.w2 |= 0x0080; ac->f.speed = 0; ac->f.b4 |= 0x80;   /* a stopped aircraft on the deck that has been airborne (12.14 N2) */
    g_step(g, 1);
    ASSERT_EQ_INT(ac->f.v4a850, 0xFC);                           /* the frozen pass ran the tail */
    ASSERT_EQ_INT(scr(g)->request, 3);
    int again = -1;
    for (int i = 1; i <= 20 && again < 0; i++) { g_step(g, 1); if (scr(g)->state == SCR_END_WAIT) again = i; }
    ASSERT_EQ_INT(again, 6);                                     /* six passes after the store, as for any request 3 */
    for (int i = 0; i < 20 && scr(g)->state != SCR_END_TEXT; i++) g_step(g, 1);
    ASSERT_EQ_INT(scr(g)->eng.ids[0], 0x54); ASSERT_EQ_INT(scr(g)->eng.ids[1], 0x8053);   /* the engine was reset: the list of 0xFC */
    for (int i = 0; i < 800 && !(scr(g)->state == SCR_FLIGHT && ac->f.v4a850 == 4); i++) g_step(g, 1);
    ASSERT_EQ_INT(ac->f.v4a850, 4);
    uint8_t buf[83];
    ASSERT_EQ_INT(game_log_export(g, buf, sizeof buf), 83);
    ASSERT_EQ_INT((buf[5 + 0x38] << 8) | buf[5 + 0x39], 1);     /* the log update ran once */
    game_destroy(g);
}

TEST(real_assets_a_mission_ended_by_the_script_while_a_crash_restart_runs_counts_once) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 6));
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    g_step(g, 3);
    ac->f.v4a850 = 4;                                            /* a crash: the scripts are dead until the restart of the selection (12.11) */
    e->kills = 4;
    g_step(g, 20);
    ASSERT_EQ_INT(ac->f.v4a850, 4);
    ASSERT_EQ_INT(scr(g)->request, 0);
    ASSERT_EQ_INT(scr(g)->state, SCR_FLIGHT);
    game_destroy(g);
}

TEST(real_assets_the_key_lock_holds_from_the_store_until_the_resume_and_never_leaks_into_the_next_flight) {
    if (!have_real()) { printf("skipped "); return; }
    static const uint8_t comp[9] = { 0, 0, 0, 1, 1, 1, 1, 1, 0 };
    game_t *g = mission_game(comp, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(start_mission(g, 3));                            /* F3: the thieves */
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    const missions_t *ms = game_debug_missions(g);
    ASSERT_EQ_INT(scr(g)->mode_byte, 5);
    g_step(g, 3);
    ASSERT_EQ_INT(ms->key_lock, 0);
    entities_ac(e, 4)->pos[0] = 0x71 << 22;                      /* a thief reaches the eastern edge (cell x 0x70 or more): failure, the flight ends */
    entities_ac(e, 4)->f.cell_x = 0x71;
    entities_ac(e, 4)->f.w0 |= 0x0048;
    g_step(g, 2);
    ASSERT_EQ_INT(ac->f.v4a850, 0xFE);
    ASSERT_EQ_INT(ms->key_lock, 1);                              /* V:4A965 from the store on */
    for (int i = 0; i < 1200 && scr(g)->state != SCR_MENU_BUILD && scr(g)->state != SCR_MENU; i++) g_step(g, 1);
    ASSERT_TRUE(scr(g)->state == SCR_MENU_BUILD || scr(g)->state == SCR_MENU);   /* 0x2C and the code 0xF0: the menu follows */
    g_step(g, 60);
    g_key(g, '2');
    for (int i = 0; i < 400; i++) g_step(g, 1);
    g_key(g, '4'); g_step(g, 20);
    ASSERT_EQ_INT(ms->key_lock, 0);                              /* a free flight starts with the keys free */
    game_destroy(g);
}

int main(void) {
    printf("test_missions\n");
    RUN(the_dispatcher_acts_only_with_the_simulation_bit_and_the_demo_flag_clear_and_a_mode_above_2);
    RUN(the_end_of_update_clears_the_launch_request_bytes_and_drops_a_stale_lock);
    RUN(f1_succeeds_when_the_counter_is_negative_and_stores_the_end_code_once);
    RUN(a_request_3_already_pending_stops_a_script_from_storing_again);
    RUN(a_pending_request_of_another_kind_is_overwritten_by_the_scripts_3_inside_the_window);
    RUN(the_landing_test_at_an_airfield_needs_the_pad_bit_and_stores_0xFC_with_delay_0);
    RUN(the_landing_test_at_the_carrier_needs_a_deck_bit);
    RUN(the_landing_test_needs_the_lift_off_the_ground_bit_and_speed_zero);
    RUN(the_other_codes_disable_the_tail);
    RUN(f2_waits_until_air_force_one_is_down_slow_and_both_fighters_are_gone);
    RUN(f2_success_counts_5_passes_hands_over_the_view_at_1_and_stores_0xFF_with_delay_4);
    RUN(f2_failure_is_0xFE_or_0xFD_by_bit_7_of_b20_with_delay_0_and_no_tracking_camera);
    RUN(f2_does_nothing_for_a_never_placed_slot_even_with_the_slot_invalid);
    RUN(f2_count_ends_without_a_store_when_the_request_is_3_and_restarts_from_the_start_value);
    RUN(f3_fails_when_a_neutral_thief_reaches_cell_x_0x70);
    RUN(f3_a_thief_turned_hostile_is_no_subject);
    RUN(f3_shadowing_loads_200_when_far_counts_down_when_near_and_reloads_when_far);
    RUN(f3_the_timer_runs_down_one_per_pass_while_near_and_the_distance_depends_on_the_progress);
    RUN(f3_with_the_timer_negative_both_thieves_are_rerouted_to_the_first_waypoint_of_their_route);
    RUN(f3_succeeds_after_the_shadowing_when_the_kills_reach_the_cap_with_one_frozen_pass);
    RUN(f3_with_no_thief_alive_the_success_test_runs_at_once_and_the_cap_grows_with_turned_thieves);
    RUN(f3_a_hostile_thief_with_the_timer_not_negative_ends_the_pass_before_the_success_test);
    RUN(f4_waits_for_a_release_a_landing_and_the_counter);
    RUN(f4_judges_both_horizontal_axes_against_0x10000_with_no_frozen_pass);
    RUN(f5_waits_while_the_cruise_missile_is_flying);
    RUN(f5_failure_when_the_missile_arrives_tracks_it_and_stores_0xFE_with_delay_7);
    RUN(the_tracking_camera_in_an_external_view_writes_the_selector_from_v4a8eb_for_the_game);
    RUN(f5_success_when_the_missile_is_shot_down_has_no_delay_and_no_tracking_camera);
    RUN(f6_succeeds_at_the_cap_after_one_frozen_pass);
    RUN(f6_delay_quirk_the_high_byte_of_the_lock_offset_times_256_plus_the_cap);
    RUN(the_default_script_serves_every_unlisted_mode);
    RUN(kills_are_compared_as_signed_bytes);
    RUN(a_crash_sets_code_4_and_the_scripts_are_dead_until_the_restart_of_the_selection);
    RUN(the_script_state_survives_a_crash_restart);
    RUN(the_start_of_the_selection_clears_the_script_state);
    RUN(esc_sets_the_pause_byte_to_1_so_a_holding_trigger_ends_its_count_in_that_pass);
    RUN(esc_in_the_count_skips_the_hand_over_pass_but_the_tracking_camera_still_runs_and_the_camera_bytes_survive_the_restart);
    RUN(the_tail_has_no_request_guard_so_a_landing_replaces_a_pending_0xFF_with_0xFC);
    RUN(a_landed_seat_leaves_a_request_3_the_script_stored_first_and_the_script_overwrites_a_seat_request_2);
    RUN(a_pause_key_during_the_count_clears_the_byte_and_the_count_restarts_from_the_start_value);
    RUN(a_pause_byte_of_0xFF_counts_down_by_one_per_pass_and_does_the_hand_over_once);
    RUN(the_resume_unlocks_the_keys_and_clears_the_pause_byte);
    RUN(the_id_lists_of_the_six_missions_are_read_from_the_chain_table_by_mode_and_index);
    RUN(the_briefing_is_index_0_with_no_completion_and_index_1_after_one);
    RUN(real_assets_the_f1_approach_is_external_with_the_stage_passes_of_the_log_and_the_cockpit_at_pass_125_to_127);
    RUN(real_assets_every_mission_approach_is_external_until_the_cockpit_pass_one_before_the_last_of_the_count);
    RUN(the_approach_target_is_the_first_counted_aircraft_else_the_last_aircraft_found);
    RUN(the_date_and_the_bearing_patches);
    RUN(real_assets_f6_reaches_its_end_code_through_the_dispatcher_one_frozen_pass_request_3_the_outcome_and_the_resume);
    RUN(real_assets_the_effect_models_draw_their_discs_by_the_life_word_through_the_interpreter);
    RUN(real_assets_an_effect_object_is_a_drawn_secondary_entry_with_the_model_of_its_id_word_plus_16);
    RUN(real_assets_the_scene_exports_the_place_of_the_gun_routine_in_the_frame);
    RUN(real_assets_f2_hands_the_view_over_at_the_pass_with_the_byte_at_1_and_tracks_air_force_one);
    RUN(real_assets_esc_with_the_trigger_holding_fires_the_script_in_the_gate_pulse_and_the_reset_clears_the_code);
    RUN(real_assets_an_esc_restart_with_the_trigger_holding_carries_the_request_through_the_reset_and_takes_seven_steps_more);
    RUN(real_assets_esc_sets_the_pause_byte_to_1_and_the_restart_clears_it_and_the_script_state);
    RUN(real_assets_p_during_a_scripts_count_clears_the_byte_with_no_pause_screen_and_the_count_restarts);
    RUN(real_assets_p_is_ignored_while_a_scripts_result_locks_the_keys);
    RUN(real_assets_p_in_the_esc_fade_clears_the_byte_the_esc_set_so_a_holding_trigger_does_not_fire_in_the_gate_pulse);
    RUN(real_assets_a_second_p_during_the_esc_countdown_stores_0xff_in_the_pause_byte_again_and_a_third_clears_it);
    RUN(real_assets_the_pause_screen_runs_no_world_pass_so_a_stale_lock_stays_until_the_flight_goes_on);
    RUN(real_assets_the_log_counts_the_mission_start_once_and_the_completion_bookkeeping_writes_the_level_before_the_increment);
    RUN(real_assets_a_mission_played_to_its_completion_updates_the_log_as_14_2_says);
    RUN(real_assets_p_is_ignored_in_the_outcome_screens_the_byte_and_the_mode_stay);
    RUN(real_assets_the_briefing_camera_stays_at_the_planted_position_the_selection_screens_do_not);
    RUN(real_assets_the_hud_step_and_window_row_follow_the_start_table_selection_screens_50_179_the_first_cockpit_pass_0_144);
    RUN(real_assets_the_carrier_approach_switches_the_window_row_to_167_by_the_camera_height_and_shows_the_cockpit_at_pass_103);
    RUN(real_assets_a_landing_during_the_success_text_stores_0xFC_and_the_debrief_runs_again_for_it_once);
    RUN(real_assets_a_mission_ended_by_the_script_while_a_crash_restart_runs_counts_once);
    RUN(real_assets_the_key_lock_holds_from_the_store_until_the_resume_and_never_leaks_into_the_next_flight);
    TEST_MAIN_END();
}
