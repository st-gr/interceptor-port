/* tests/test_ai.c - the generic AI (src/core/ai.c, docs/re/07-ai-and-weapons.md sections 3 and 4, "note 07").
 *
 * One test per state of the tables of 3.2, the steering of 3.5 and 3.6 on a record with the identity attitude (the body axes are the world axes: lateral x, vertical y, forward z),
 * the response delay by progress (3.3), the speed control (4.2), the gate's wreck rule (4.1), the threatened test and the evasion table by progress (4.5), the launch decision (4.6).
 * The values are the note's; the records are invented. tests/rig/test_ai_diff.c compares the whole against the original over random records. */
#include "test.h"
#include "ai_tables.h"
#include "../src/core/ai.h"
#include "../src/core/entities.h"
#include "../src/core/flight.h"

static entities_t E;
static aircraft_t P;
#define AI 8
#define TG 7

static flight_state_t *F(void) { return &E.rec[AI].ac.f; }
static entity_t *R(void) { return &E.rec[AI]; }

static void level_record(aircraft_t *a) {
    flight_state_t *f = &a->f;
    for (int i = 0; i < 9; i++) f->mat_a[i] = f->mat_b[i] = (i % 4 == 0) ? 16384 : 0;
    f->pitch = f->heading = f->roll = 0;
}

/* the AI record in the middle of the cell (64, 64), 16 * 1024 units up, airborne at speed 0x1D40, a destination at (dx, dy, dz) from it (fine units of 256 position units) */
static void setup(void) {
    memset(&E, 0, sizeof E);
    memset(&P, 0, sizeof P);
    entities_init(&E, NULL, &AI_FIXTURE, NULL);
    entities_attach_player(&E, &P);
    E.flying = 1; E.sim_on = 1; E.mode = 4; E.progress = 0; E.designated = -1; E.hostile_cap = 4;
    P.f.cls = 0x11; P.f.w0 = 0x11C8;
    level_record(&P);
    flight_state_t *f = F();
    f->w0 = 0x11C0; f->cls = 0x12; f->state = 0; f->counter = 40; f->speed_target = f->speed = 0x1D40; f->turn_limit = 0;
    level_record(&E.rec[AI].ac);
    E.rec[AI].ac.pos[0] = 64 * 4194304 + 8192 * 256; E.rec[AI].ac.pos[2] = 64 * 4194304 + 8192 * 256; E.rec[AI].ac.pos[1] = 0x300000;
    f->cell_x = f->cell_z = 64; f->fine_x = f->fine_z = 8192; f->alt8 = 0x3000;
    f->vel[1] = 100;
    E.rec[AI].dest_cx = 64; E.rec[AI].dest_cz = 64; E.rec[AI].dest_fx = 8192; E.rec[AI].dest_fz = 8192 + 4000; E.rec[AI].dest_h = 0x3000;
    E.rec[AI].target = 0xFF; E.rec[AI].engage = 3; E.rec[AI].range = 0x7FFF;
    f->repeat = 0x05;
    /* the target record 7 (a valid, inert record) and the player far away */
    E.rec[TG].ac.f.w0 = 0x0040; E.rec[TG].ac.f.cls = 0x12; level_record(&E.rec[TG].ac);
    P.pos[0] = 90 * 4194304; P.pos[2] = 90 * 4194304; P.pos[1] = 0x300000;
}
static void dest(int dx, int dy, int dz) {
    R()->dest_fx = (int16_t)(8192 + dx); R()->dest_fz = (int16_t)(8192 + dz); R()->dest_h = 0x3000 + dy;
}
static int rollkeys(void) { return F()->keys & 0x0C; }
static int pitchkeys(void) { return F()->keys & 0x30; }

/* ---- the steering (3.5, 3.6) ------------------------------------------------------------------------------------------------------------------------ */

TEST(state_0_aligned_with_a_destination_dead_ahead_commands_nothing_and_sets_the_hysteresis_flags) {
    setup();
    dest(0, 0, 4000);
    F()->keys = 0xFC;
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->turn_limit, 0x1400);                      /* written at the entry of the routine for every class but 0x30 */
    ASSERT_EQ_INT(R()->ai_flags & 0x4C, 0x4C);                   /* both components are 0: a tie goes to the vertical axis (bit 3), aligned with the target ahead: bits 2 and 6 */
    ASSERT_EQ_INT(R()->ai_flags & 0x10, 0x10);                   /* the epilogue */
    ASSERT_EQ_INT(F()->keys, 0xFC);                              /* nothing commanded in the aligned case */
}

TEST(a_destination_to_the_right_presses_the_roll_toward_it_and_sets_the_bank_limit) {
    setup();
    dest(3000, 0, 3000);                                         /* 45 degrees off the nose, level */
    ai_state(&E, AI);
    ASSERT_EQ_INT(rollkeys(), 0x08);                             /* c >= 0: the table entry is positive: roll "left" key 0x08 by the note's sign rule */
    ASSERT_TRUE(R()->ai_flags & 0x01);                           /* the sign of the control axis component */
    ASSERT_TRUE(F()->turn_limit >= 0x1000 && F()->turn_limit <= 0x3000);
    dest(-3000, 0, 3000);
    ai_state(&E, AI);
    ASSERT_EQ_INT(rollkeys(), 0x04);
    ASSERT_EQ_INT(R()->ai_flags & 0x01, 0);
}

TEST(a_destination_above_the_nose_pulls_back_and_below_it_pushes_forward) {
    setup();
    dest(0, 3000, 3000);                                         /* above: the vertical axis is the control axis, c >= 0, the entry negated: stick back */
    ai_state(&E, AI);
    ASSERT_EQ_INT(pitchkeys(), 0x20);
    ASSERT_TRUE(R()->ai_flags & 0x08);                           /* the vertical axis */
    dest(0, -3000, 3000);
    F()->keys = 0; F()->rate_p = 0;
    ai_state(&E, AI);
    ASSERT_EQ_INT(pitchkeys(), 0x10);
}

TEST(a_destination_behind_raises_the_bank_limit_to_the_cap) {
    setup();
    dest(2000, 0, -3000);                                        /* behind and to the right: the target is behind, the limit is 0x600000 minus |c|, capped at 0x3000 */
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->turn_limit, 0x3000);
    ASSERT_EQ_INT(R()->ai_flags & 0x20, 0);                      /* not ahead */
}

TEST(a_scripted_record_in_state_0_gets_only_the_recentre_bit_from_the_step_and_the_state_routine_leaves_the_throttle_bits) {
    setup();
    F()->w2 = 0x0100;
    F()->keys = 0xFF;
    entities_step(&E);                                           /* the production path: the gate ran, the state routine did not (3.1) */
}

TEST(the_state_routine_does_nothing_while_the_game_is_paused_or_frozen_and_clears_the_flags_for_a_seat) {
    setup();
    dest(3000, 0, 3000);
    F()->keys = 0x77; E.paused = 1;
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->keys, 0x77); ASSERT_EQ_INT(F()->turn_limit, 0);
    E.paused = 0;
    F()->cls = 0x30; R()->ai_flags = 0xFF;
    ai_state(&E, AI);
    ASSERT_EQ_INT(R()->ai_flags, 0); ASSERT_EQ_INT(F()->turn_limit, 0);   /* class nibble 0x30 returns before +0x7E is written */
    F()->cls = 0x12; F()->w0 &= (uint16_t)~0x0080; R()->ai_flags = 0xFF;
    ai_state(&E, AI);
    ASSERT_EQ_INT(R()->ai_flags, 0); ASSERT_EQ_INT(F()->turn_limit, 0x1400);   /* AI valid clear: after +0x7E */
}

TEST(no_destination_returns_with_the_flags_cleared) {
    setup();
    R()->dest_cx = -1;
    R()->ai_flags = 0xFF;
    ai_state(&E, AI);
    ASSERT_EQ_INT(R()->ai_flags, 0);
}

/* ---- the states (3.2) ---------------------------------------------------------------------------------------------------------------------------------- */

TEST(state_6_waits_with_the_keys_as_they_stand_and_falls_into_state_1_with_counter_30) {
    setup();
    dest(3000, 0, 3000);
    F()->state = 6; F()->counter = 5; F()->keys = 0x5A;
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->state, 6); ASSERT_EQ_INT(F()->keys, 0x5A);
    F()->counter = -1;
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->state, 1); ASSERT_EQ_INT(F()->counter, 30);
    ASSERT_EQ_INT(rollkeys(), 0x08);                             /* and it runs as state 1 in the same pass */
}

TEST(the_floor_test_sends_a_descending_record_below_speed_times_64_to_state_1_with_counter_30) {
    setup();
    F()->state = 6; F()->counter = 20; F()->vel[1] = -50;
    E.rec[AI].ac.pos[1] = 0x1D40 * 64 - 1;                        /* at or below the floor S * 64 */
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->state, 1); ASSERT_EQ_INT(F()->counter, 30);
    setup();
    F()->state = 6; F()->counter = 20; F()->vel[1] = 50;          /* climbing: the ceiling 0x800000 minus the floor, strictly above */
    E.rec[AI].ac.pos[1] = 0x800000 - 0x1D40 * 64 + 1;
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->state, 1);
    setup();
    F()->state = 6; F()->counter = 20; F()->vel[1] = 50;
    E.rec[AI].ac.pos[1] = 0x800000 - 0x1D40 * 64;                 /* exactly at the ceiling: not above */
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->state, 6);
}

TEST(state_8_follows_the_waypoint_and_an_inverted_roll_with_a_level_pitch_waits_with_counter_0x25) {
    setup();
    dest(3000, 0, 3000);
    F()->state = 8;
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->state, 8);
    ASSERT_EQ_INT(rollkeys(), 0x08);
    ASSERT_TRUE(F()->turn_limit <= 0x1800);                       /* halved in state 8 */
    F()->roll = 14400; F()->pitch = 200;
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->state, 6); ASSERT_EQ_INT(F()->counter, 0x25);
}

TEST(state_2_rolls_toward_90_degrees_and_goes_to_state_7_with_counter_32_within_15_degrees) {
    setup();
    F()->state = 2; F()->roll = 0;
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->state, 2); ASSERT_EQ_INT(rollkeys(), 0x08);   /* toward 7200 (roll < 14400) */
    F()->roll = 7200 - 1199;                                          /* within 1200 words */
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->state, 7); ASSERT_EQ_INT(F()->counter, 32);
    ASSERT_EQ_INT(F()->keys & 0xFC, 0);
}

TEST(state_7_pulls_back_with_the_pitch_rate_table_and_ends_when_the_counter_is_negative) {
    setup();
    F()->state = 7; F()->counter = 10;
    ai_state(&E, AI);
    ASSERT_EQ_INT(pitchkeys(), 0x20);                                 /* -320 below the pitch rate 0: stick back */
    F()->counter = -1;
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->state == 6 || F()->state == 1, 1);             /* the end routine: a wait */
}

TEST(states_5_and_9_hold_a_shallow_bank_with_their_tolerances) {
    setup();
    F()->state = 5; F()->roll = 0;
    ai_state(&E, AI);
    ASSERT_EQ_INT(rollkeys(), 0x08);                                   /* toward 14 degrees = 1120 words */
    F()->roll = 1120;
    ai_state(&E, AI);
    ASSERT_EQ_INT(rollkeys(), 0);                                      /* inside the 6 degree tolerance (480 words) */
    F()->state = 9; F()->roll = 640 + 320;                             /* 8 degrees with the 4 degree tolerance (320): at the edge it presses */
    ai_state(&E, AI);
    ASSERT_EQ_INT(rollkeys(), 0x04);
    F()->roll = 28800 - 640 - 320;                                     /* the other side: 28800 minus the bank */
    ai_state(&E, AI);
    ASSERT_EQ_INT(rollkeys(), 0x08);
    F()->counter = -1;
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->state, 0);
}

TEST(state_0a_waits_and_leaves_to_state_0_when_the_counter_is_negative) {
    setup();
    F()->state = 0x0A; F()->counter = 3; F()->keys = 0xA4;
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->state, 0x0A); ASSERT_EQ_INT(F()->keys, 0xA4);
    F()->counter = -1;
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->state, 0); ASSERT_EQ_INT(F()->keys, 0x00);                  /* the throttle bits are kept: 0xA4 has none */
}

/* ---- the response delay by progress (3.3) -------------------------------------------------------------------------------------------------------- */

TEST(the_end_routine_waits_75_37_18_or_4_steps_by_the_progress_when_the_record_has_a_target) {
    static const int want[4] = { 0x4B, 0x25, 0x12, 4 };
    for (int p = 0; p < 4; p++) {
        setup();
        E.progress = p;
        R()->target = 0x80; R()->ai_flags = 0x60;
        F()->state = 1; F()->counter = -1;
        ai_state(&E, AI);
        ASSERT_EQ_INT(F()->state, 6);
        ASSERT_EQ_INT(F()->counter, want[p]);
    }
    setup();   /* no target: counter 10 */
    F()->state = 1; F()->counter = -1;
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->state, 6); ASSERT_EQ_INT(F()->counter, 10);
    setup();   /* progress 3 and the flags 5 and 6 not both set: counter 10 */
    E.progress = 3; R()->target = 0x80; R()->ai_flags = 0x20;
    F()->state = 1; F()->counter = -1;
    ai_state(&E, AI);
    ASSERT_EQ_INT(F()->counter, 10);
}

/* ---- the gate (4.1 to 4.5) --------------------------------------------------------------------------------------------------------------------------- */

TEST(the_gate_decrements_the_counter_and_does_nothing_more_while_paused_or_before_the_flight_flag) {
    setup();
    F()->counter = 7; E.paused = 1;
    ai_gate(&E, AI);
    ASSERT_EQ_INT(F()->counter, 7);                                    /* paused: nothing */
    E.paused = 0; E.flying = 0;
    ai_gate(&E, AI);
    ASSERT_EQ_INT(F()->counter, 6);
    ASSERT_EQ_INT(R()->range, 0x7FFF);                                 /* V:4A846 clear: nothing more */
}

TEST(a_wreck_aims_at_itself_and_settles_its_speed_at_0x1200_by_steps_of_0x90) {
    setup();
    F()->b20 = 0x02; F()->speed_target = F()->speed = 0x1D40; F()->w2 = 0x2008;
    ai_gate(&E, AI);
    ASSERT_EQ_INT(R()->dest_cx, 64); ASSERT_EQ_INT(R()->dest_fx, 8192); ASSERT_EQ_INT(R()->dest_h, 0);
    ASSERT_EQ_INT(F()->speed_target, 0x1D40 - 0x90); ASSERT_EQ_INT(F()->speed, 0x1D40 - 0x90);
    ASSERT_EQ_INT(F()->w2 & 0x2008, 0);
    F()->speed_target = 0x1210;
    ai_gate(&E, AI);
    ASSERT_EQ_INT(F()->speed_target, 0x1200);                          /* stops there */
    F()->speed_target = 0x1000;
    ai_gate(&E, AI);
    ASSERT_EQ_INT(F()->speed_target, 0x1090);                          /* and rises toward it */
}

TEST(the_speed_control_presses_the_throttle_up_below_the_cruise_target_and_down_above_it) {
    setup();
    F()->speed_target = 0x900;                                         /* no target: the cruise target 0x1D40 (0x15C0 for a record that is not counted, 0x1EC0 for class 0x15) */
    ai_gate(&E, AI);
    ASSERT_EQ_INT(F()->keys & 3, 1);
    setup();
    F()->speed_target = 0x2400;
    ai_gate(&E, AI);
    ASSERT_EQ_INT(F()->keys & 3, 2);
    setup();
    F()->speed_target = 0x1D40; E.rec[AI].dest_cx = -1;
    F()->keys = 0xFF;
    ai_gate(&E, AI);
    ASSERT_EQ_INT(F()->keys, 0xFF);                                    /* no destination: no speed control */
}

TEST(the_range_routine_class_and_schedule_follow_the_notes_thresholds) {
    setup();
    dest(0, 0, 200);
    R()->range = 0x100; F()->repeat = 0x05;                             /* a range under 0x480 is recomputed on every call */
    ai_range(&E, AI);
    ASSERT_EQ_INT((uint16_t)R()->range <= 0x300, 1);
    ASSERT_EQ_INT(F()->id >> 4, 1);                                    /* up to 0x300: the range class 1 */
    dest(0, 0, 3000);
    R()->range = 0x100; F()->repeat = 0x05;
    ai_range(&E, AI);
    ASSERT_EQ_INT(F()->id >> 4, 2);                                    /* 0x300 to 0xC00: 2 */
    dest(0, 0, 4000);
    R()->range = 0x100; F()->repeat = 0x05;
    ai_range(&E, AI);
    ASSERT_EQ_INT(F()->id >> 4, 3);                                    /* beyond 0xC00 up to 0x1800: 3 */
    /* a range above 0x900 is recomputed only when the counter's byte (masked) as a signed byte exceeds 0x50: the counters 6 and 7 */
    R()->range = 0x1000; F()->repeat = 0x35;
    ai_range(&E, AI);
    ASSERT_EQ_INT(R()->range, 0x1000); ASSERT_EQ_INT(F()->repeat, 0x45);
    F()->repeat = 0x55;
    ai_range(&E, AI);
    ASSERT_TRUE(R()->range != 0x1000); ASSERT_EQ_INT(F()->repeat & 0xF0, 0);
}

TEST(a_counted_record_with_no_target_steers_36_units_behind_the_player_and_acquisition_makes_the_player_the_target_when_near) {
    setup();
    E.progress = 0;
    P.pos[0] = 64 * 4194304 + 8192 * 256 + 100 * 1024; P.pos[2] = 64 * 4194304 + 8192 * 256 + 10 * 1024; P.pos[1] = 0x300000;
    P.f.cell_x = 64; P.f.cell_z = 64;
    R()->target = 0xFF;
    ai_gate(&E, AI);
    /* the pursuit point: (0, 0, -36) model units in the player's frame (1,024 units each): x unchanged, z 26 units short of the AI record's z */
    const int32_t px = 64 * 4194304 + 8192 * 256 + 100 * 1024, pz = 64 * 4194304 + 8192 * 256 + 10 * 1024 - 36 * 1024;
    ASSERT_EQ_INT(R()->dest_cx, (px >> 22)); ASSERT_EQ_INT(R()->dest_fx, ((px >> 8) & 0x3FFF));
    ASSERT_EQ_INT(R()->dest_fz, ((pz >> 8) & 0x3FFF));
    ASSERT_EQ_INT(R()->target, 0xFF);                                  /* the target byte 0xFF stays and the engaged bit is not set (review of 4.4) */
    ASSERT_EQ_INT(F()->w0 & 0x0001, 0);
    setup();
    R()->target = 0x80 + TG;                                           /* a target record, the player inside the box of 0x180000 on every axis: it becomes the target */
    E.rec[TG].ac.pos[0] = 80 * 4194304; E.rec[TG].ac.pos[2] = 80 * 4194304;
    P.pos[0] = 64 * 4194304 + 8192 * 256 + 100000; P.pos[2] = 64 * 4194304 + 8192 * 256; P.pos[1] = 0x300000 + 100000;
    ai_gate(&E, AI);
    ASSERT_EQ_INT(R()->target, 0x80);
    ASSERT_EQ_INT(F()->w0 & 0x0001, 0x0001);                          /* engaged */
}

/* the threatened scene: the AI record at z = 8000 units ahead of the player on the same heading, so the player's nose points at it from behind */
static void threatened_scene(int progress, int range_word, int speed_diff) {
    setup();
    E.progress = progress;
    R()->target = 0x80;
    P.pos[0] = 64 * 4194304 + 8192 * 256; P.pos[2] = 64 * 4194304 + 8192 * 256 - 1500 * 1024; P.pos[1] = 0x300000;
    P.f.cell_x = 64; P.f.cell_z = 64;
    P.f.speed_target = (int16_t)(0x1D40 + speed_diff);
    E.rec[0].range = (int16_t)range_word;                              /* the target's range word +0x4A (4.5) */
    F()->repeat = 0x10;                                                /* the test runs when the counter nibble is 1 */
    E.rec[AI].dest_cx = -1;                                            /* no destination: no speed control, so the range routine leaves the nibble alone */
}

TEST(a_record_chased_from_behind_is_threatened_and_picks_its_evasion_pair_by_progress_and_range_group) {
    /* the groups of the fixture: 0 (range up to 0x300, equal speeds), 6 (0x300 to 0xC00), 12 (0xC00 to 0x1E00), 18 (over 0x1E00) */
    for (int p = 0; p < 4; p++) {
        threatened_scene(p, 0x200, 0);
        ai_gate(&E, AI);
        ASSERT_EQ_INT(F()->b4 & 0x20, 0x20);
        ASSERT_EQ_INT(F()->counter, AI_FIXTURE.evasion[0][p][0]); ASSERT_EQ_INT(F()->state, AI_FIXTURE.evasion[0][p][1]);
        threatened_scene(p, 0x500, 0);
        ai_gate(&E, AI);
        ASSERT_EQ_INT(F()->counter, AI_FIXTURE.evasion[6][p][0]); ASSERT_EQ_INT(F()->state, AI_FIXTURE.evasion[6][p][1]);
        threatened_scene(p, 0x1000, 0);
        ai_gate(&E, AI);
        ASSERT_EQ_INT(F()->counter, AI_FIXTURE.evasion[12][p][0]); ASSERT_EQ_INT(F()->state, AI_FIXTURE.evasion[12][p][1]);
        threatened_scene(p, 0x2000, 0);                                                  /* over 0x1E00: group 18, (40, 9) for all */
        ai_gate(&E, AI);
        ASSERT_EQ_INT(F()->counter, AI_FIXTURE.evasion[18][p][0]); ASSERT_EQ_INT(F()->state, AI_FIXTURE.evasion[18][p][1]);
    }
    threatened_scene(1, 0x200, 0xC0);                                                    /* the record slower by 0xC0 or more: group 2 */
    F()->speed_target = 0x1D40; P.f.speed_target = 0x1D40 + 0xC0;
    ai_gate(&E, AI);
    ASSERT_EQ_INT(F()->counter, AI_FIXTURE.evasion[2][1][0]); ASSERT_EQ_INT(F()->state, AI_FIXTURE.evasion[2][1][1]);
}

TEST(a_target_that_is_not_pointing_at_the_record_does_not_threaten_it) {
    threatened_scene(0, 0x200, 0);
    P.f.mat_b[2] = 0; P.f.mat_b[8] = 0; P.f.mat_b[0] = 16384; P.f.mat_b[6] = 16384;    /* the player flies sideways */
    F()->b4 = 0x20;
    ai_gate(&E, AI);
    ASSERT_EQ_INT(F()->b4 & 0x20, 0);
    ASSERT_EQ_INT(F()->state, 0);
}

TEST(a_not_counted_state_8_record_that_is_evaded_becomes_counted_and_raises_the_cap) {
    threatened_scene(1, 0x200, 0);
    F()->w0 |= 0x0008; F()->state = 8; E.mode = 5; E.launch_flag = 1;
    E.hostile_cap = 2; E.hostile_count = 1;
    ai_gate(&E, AI);
    ASSERT_EQ_INT(F()->w0 & 0x0008, 0);
    ASSERT_EQ_INT(E.hostile_cap, 3); ASSERT_EQ_INT(E.hostile_count, 2);
}

static const int16_t RUN3[3][5] = { { 64, 64, 8192, 8292, 0x3000 }, { 65, 64, 100, 8192, 0x3100 }, { 66, 64, 300, 8192, 0x3200 } };
static int route3(void *u, unsigned r, int16_t blocks[][5], int max) {
    (void)u;
    if (r != 2 || max < 3) return 0;
    memcpy(blocks, RUN3, sizeof RUN3);
    return 3;
}

static void place_at(int cx, int fx, int cz, int fz, int h) {
    flight_state_t *f = F();
    f->cell_x = (int16_t)cx; f->cell_z = (int16_t)cz; f->fine_x = (int16_t)fx; f->fine_z = (int16_t)fz; f->alt8 = h;
    E.rec[AI].ac.pos[0] = cx * 4194304 + fx * 256; E.rec[AI].ac.pos[2] = cz * 4194304 + fz * 256; E.rec[AI].ac.pos[1] = h * 256;
}

TEST(the_waypoint_rule_advances_to_the_next_block_and_the_last_one_clears_ai_valid_and_crashes_a_cruise_missile) {
    setup();
    E.hook.route = route3;
    F()->state = 8; R()->route = 2;
    R()->dest_cx = RUN3[0][0]; R()->dest_cz = RUN3[0][1]; R()->dest_fx = RUN3[0][2]; R()->dest_fz = RUN3[0][3]; R()->dest_h = RUN3[0][4];
    place_at(64, 8192, 64, 8292, 0x3000);                               /* at the first waypoint */
    R()->range = 0x100;
    ai_gate(&E, AI);                                                    /* the destination is the block 0: the next block becomes the destination */
    ASSERT_EQ_INT(R()->dest_cx, 65); ASSERT_EQ_INT(R()->dest_fx, 100); ASSERT_EQ_INT(R()->dest_h, 0x3100);
    ASSERT_EQ_INT((uint16_t)R()->range, 0x7FFF);                        /* so that the range routine recomputes */
    R()->dest_cx = 66; R()->dest_cz = 64; R()->dest_fx = 300; R()->dest_fz = 8192; R()->dest_h = 0x3200; R()->range = 0x100;   /* the last block */
    place_at(66, 300, 64, 8192, 0x3200);
    ai_gate(&E, AI);
    ASSERT_EQ_INT(F()->w0 & 0x0080, 0);                                 /* AI valid cleared */
    ASSERT_EQ_INT(F()->w0 & 0x0200, 0);                                 /* an aircraft just flies on */
    F()->w0 |= 0x0080; F()->cls = 0x15; R()->range = 0x100;
    ai_gate(&E, AI);
    ASSERT_EQ_INT(F()->w0 & 0x0200, 0x0200);                            /* the cruise missile ends its flight by crashing */
    F()->w0 |= 0x0080; F()->cls = 0x12; R()->dest_cx = 66; R()->dest_fx = 301; place_at(66, 301, 64, 8192, 0x3200); R()->range = 0x100;
    ai_gate(&E, AI);
    ASSERT_EQ_INT(E.debug_word, 0x35);                                  /* a destination that is not in the run: the debug word, no effect */
}

/* ---- the launch decision (4.6) ------------------------------------------------------------------------------------------------------------------------ */

TEST(the_launch_decision_needs_a_request_or_a_fair_shot_and_never_for_a_hit_dying_or_crashed_owner) {
    setup(); F()->w0 &= (uint16_t)~0x0100;   /* the owner has had its first step */
    E.req_ai = 1;
    ASSERT_EQ_INT(ai_launch_decision(&E, AI), 1);
    ASSERT_EQ_INT(E.req_ai, 0);                                         /* the request is cleared on a yes */
    E.req_ai = 1; F()->w0 |= 0x0400;
    ASSERT_EQ_INT(ai_launch_decision(&E, AI), 0);                       /* dying */
    F()->w0 &= (uint16_t)~0x0400; F()->w0 |= 0x8000;
    ASSERT_EQ_INT(ai_launch_decision(&E, AI), 0);                       /* hit */
    F()->w0 &= (uint16_t)~0x8000; F()->w0 |= 0x0100;                    /* the keep-moving bit is in the mask 0x8700 (a record's first step clears it, note 07 4.6) */
    ASSERT_EQ_INT(ai_launch_decision(&E, AI), 0);
    F()->w0 &= (uint16_t)~0x0100;
    ASSERT_EQ_INT(ai_launch_decision(&E, AI), 1);
    E.req_ai = 1; F()->b20 |= 0x02;                                     /* an out-of-control owner does not fire on the request (record 5's script request V:4A873 is another path) */
    ASSERT_EQ_INT(ai_launch_decision(&E, AI), 0); ASSERT_EQ_INT(E.req_ai, 1);
    F()->b20 &= (uint8_t)~0x02;
    ASSERT_EQ_INT(ai_launch_decision(&E, AI + 2), 1);              /* the request is global: the first record that asks takes it, whatever its owner (an empty record here) */
    ASSERT_EQ_INT(E.req_ai, 0);
    E.req_ai = 1;
    /* no request: the target valid, not out of control and not engaged; the flags 5 and 6; the owner engaged; the range class 2 or 3 */
    setup(); F()->w0 &= (uint16_t)~0x0100;   /* the owner has had its first step */
    R()->target = 0x80 + TG; E.rec[TG].ac.f.w0 = 0x0040;
    R()->ai_flags = 0x60; F()->w0 |= 0x0001; F()->id = 0x2D;
    ASSERT_EQ_INT(ai_launch_decision(&E, AI), 1);
    R()->target = 0xFF;                                                 /* the record 0x7F lies outside the array: no valid target */
    ASSERT_EQ_INT(ai_launch_decision(&E, AI), 0);
    R()->target = 0x80 + TG;
    F()->id = 0x1D;
    ASSERT_EQ_INT(ai_launch_decision(&E, AI), 0);                       /* range class 1 */
    F()->id = 0x3D; R()->ai_flags = 0x20;
    ASSERT_EQ_INT(ai_launch_decision(&E, AI), 0);                       /* flags 5 and 6 not both set */
    R()->ai_flags = 0x60; E.rec[TG].ac.f.w0 |= 0x0001;
    ASSERT_EQ_INT(ai_launch_decision(&E, AI), 0);                       /* the target is engaged */
}

TEST(the_normaliser_scales_a_vector_to_the_length_with_the_notes_approximation) {
    int32_t o[3];
    ai_normalise(0, 0, 4000, 0xC0, o);
    ASSERT_TRUE(o[2] >= 188 && o[2] <= 196); ASSERT_EQ_INT(o[0], 0); ASSERT_EQ_INT(o[1], 0);
    ai_normalise(0, 0, 0, 0xC0, o);
    ASSERT_EQ_INT(o[0] | o[1] | o[2], 0);                                /* an approximate length of 0 gives the zero vector */
    ai_normalise(3000, -4000, 0, 0xC0, o);
    ASSERT_TRUE(o[0] > 0 && o[1] < 0);
}


/* ---- the extracted tables against the constants of the note ---------------------------------------------------------------------------------------- */

static int ai_read(void *user, const char *path, uint8_t **out, size_t *len) {
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
static void ai_release(void *user, uint8_t *buf) { (void)user; free(buf); }

TEST(real_assets_the_extracted_tables_have_the_shapes_the_decisions_need) {
    static game_loader_t ld = { ai_read, ai_release, NULL };
    static ai_t tab;
    char err[160];
    if (assets_load_ai(&ld, &tab, err, sizeof err) != 0) { printf("skipped "); return; }
    for (int i = 0; i < AI_RATES; i++)
        for (int k = 1; k < AI_RATE_ENTRIES; k++) ASSERT_TRUE(tab.rate[i][k] >= tab.rate[i][k - 1] && tab.rate[i][k] > 0);   /* the rates rise with the angle index */
    for (int g = 0; g < AI_EVASION_GROUPS; g++)
        for (int p = 0; p < 4; p++) { ASSERT_TRUE(tab.evasion[g][p][0] > 0); ASSERT_TRUE(tab.evasion[g][p][1] >= 0 && tab.evasion[g][p][1] < 16); }
    /* the steering and the evasion gate run on the real tables over a grid of bearings and ranges without leaving the key set */
    for (int dz = -4000; dz <= 4000; dz += 500)
        for (int dx = -4000; dx <= 4000; dx += 700)
            for (int dy = -2000; dy <= 2000; dy += 1000) {
                setup();
                E.tab = &tab;
                dest(dx, dy, dz);
                F()->keys = 0;
                ai_state(&E, AI);
                ASSERT_TRUE(F()->turn_limit >= 0);
            }
    static const int ranges[4] = { 0x200, 0x500, 0x1000, 0x2000 };
    for (int rg = 0; rg < 4; rg++)
        for (int p = 0; p < 4; p++) {
            threatened_scene(p, ranges[rg], 0);
            E.tab = &tab;
            ai_gate(&E, AI);
            ASSERT_TRUE(F()->counter >= 0);
        }
}

int main(void) {
    RUN(state_0_aligned_with_a_destination_dead_ahead_commands_nothing_and_sets_the_hysteresis_flags);
    RUN(a_destination_to_the_right_presses_the_roll_toward_it_and_sets_the_bank_limit);
    RUN(a_destination_above_the_nose_pulls_back_and_below_it_pushes_forward);
    RUN(a_destination_behind_raises_the_bank_limit_to_the_cap);
    RUN(a_scripted_record_in_state_0_gets_only_the_recentre_bit_from_the_step_and_the_state_routine_leaves_the_throttle_bits);
    RUN(the_state_routine_does_nothing_while_the_game_is_paused_or_frozen_and_clears_the_flags_for_a_seat);
    RUN(no_destination_returns_with_the_flags_cleared);
    RUN(state_6_waits_with_the_keys_as_they_stand_and_falls_into_state_1_with_counter_30);
    RUN(the_floor_test_sends_a_descending_record_below_speed_times_64_to_state_1_with_counter_30);
    RUN(state_8_follows_the_waypoint_and_an_inverted_roll_with_a_level_pitch_waits_with_counter_0x25);
    RUN(state_2_rolls_toward_90_degrees_and_goes_to_state_7_with_counter_32_within_15_degrees);
    RUN(state_7_pulls_back_with_the_pitch_rate_table_and_ends_when_the_counter_is_negative);
    RUN(states_5_and_9_hold_a_shallow_bank_with_their_tolerances);
    RUN(state_0a_waits_and_leaves_to_state_0_when_the_counter_is_negative);
    RUN(the_end_routine_waits_75_37_18_or_4_steps_by_the_progress_when_the_record_has_a_target);
    RUN(the_gate_decrements_the_counter_and_does_nothing_more_while_paused_or_before_the_flight_flag);
    RUN(a_wreck_aims_at_itself_and_settles_its_speed_at_0x1200_by_steps_of_0x90);
    RUN(the_speed_control_presses_the_throttle_up_below_the_cruise_target_and_down_above_it);
    RUN(the_range_routine_class_and_schedule_follow_the_notes_thresholds);
    RUN(a_counted_record_with_no_target_steers_36_units_behind_the_player_and_acquisition_makes_the_player_the_target_when_near);
    RUN(a_record_chased_from_behind_is_threatened_and_picks_its_evasion_pair_by_progress_and_range_group);
    RUN(a_target_that_is_not_pointing_at_the_record_does_not_threaten_it);
    RUN(a_not_counted_state_8_record_that_is_evaded_becomes_counted_and_raises_the_cap);
    RUN(the_waypoint_rule_advances_to_the_next_block_and_the_last_one_clears_ai_valid_and_crashes_a_cruise_missile);
    RUN(the_launch_decision_needs_a_request_or_a_fair_shot_and_never_for_a_hit_dying_or_crashed_owner);
    RUN(the_normaliser_scales_a_vector_to_the_length_with_the_notes_approximation);
    RUN(real_assets_the_extracted_tables_have_the_shapes_the_decisions_need);
    TEST_MAIN_END();
}
