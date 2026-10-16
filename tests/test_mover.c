/* tests/test_mover.c - the front end's camera mover (src/core/mover.c; note 07 6.2, note 08 6.4 and 12.14 C13). The fixtures are invented: a player at an invented position with an invented attitude matrix. The per-pass camera of the
 * real starts (airfield 1, the carrier, training part 2) is checked against the captured longs by the real-asset tests of test_missions.c and test_training.c (the stage passes) and by the rig. */
#include "test.h"
#include "../src/core/mover.h"
#include <string.h>

static const int32_t POS[3] = { 0x10000000, 0x708, 0x11000000 };
static const int16_t IDENT[9] = { 16384, 0, 0, 0, 16384, 0, 0, 0, 16384 };

static mover_ctx_t ctx(int location) { mover_ctx_t c; c.pos = POS; c.mat = IDENT; c.location = location; return c; }

TEST(the_start_target_is_5760_ring_units_behind_the_player_in_its_own_frame) {
    mover_ctx_t c = ctx(0);
    int32_t t[3];
    mover_start_target(&c, t);
    ASSERT_EQ_INT(t[0], POS[0]); ASSERT_EQ_INT(t[1], POS[1]); ASSERT_EQ_INT(t[2], POS[2] - 5760 * 1024);
    static const int16_t turned[9] = { 0, 0, 16384, 0, 16384, 0, -16384, 0, 0 };   /* a quarter turn: the z axis of the player is the world x axis */
    c.mat = turned;
    mover_start_target(&c, t);
    ASSERT_EQ_INT(t[0], POS[0] - 5760 * 1024); ASSERT_EQ_INT(t[1], POS[1]); ASSERT_EQ_INT(t[2], POS[2]);
}

TEST(a_free_flight_approach_starts_at_the_selection_camera_in_stage_0_with_an_empty_trail) {
    mover_ctx_t c = ctx(0);
    mover_t m;
    mover_begin(&m, &c);
    ASSERT_EQ_INT(m.stage, 0);
    ASSERT_EQ_INT(m.cam[0], 0x10800000); ASSERT_EQ_INT(m.cam[1], 0x03000000); ASSERT_EQ_INT(m.cam[2], 0x10C00000);
    ASSERT_TRUE(m.trail[0] == 0 && m.trail[1] == 0 && m.trail[2] == 0);
}

TEST(a_mission_approach_starts_at_the_briefing_overview_in_stage_8_and_steps_in_stage_0_from_its_fourth_pass) {
    mover_ctx_t c = ctx(0);
    mover_t m;
    mover_begin_mission(&m, &c);
    ASSERT_EQ_INT(m.stage, 8);
    ASSERT_EQ_INT(m.cam[0], 0x10C00000); ASSERT_EQ_INT(m.cam[1], 0x05000000); ASSERT_EQ_INT(m.cam[2], 0x11400000);
    for (int i = 0; i < MOVER_MISSION_WAIT - 1; i++) {      /* stage 8: the camera is at its target, nothing moves */
        mover_step(&m, &c);
        ASSERT_EQ_INT(m.stage, 8);
        ASSERT_EQ_INT(m.cam[0], 0x10C00000); ASSERT_EQ_INT(m.cam[1], 0x05000000); ASSERT_EQ_INT(m.cam[2], 0x11400000);
    }
    mover_step(&m, &c);                                      /* the pass that ends the wait takes stage 0 and makes its step */
    ASSERT_EQ_INT(m.stage, 0);
    ASSERT_TRUE(m.cam[0] != 0x10C00000 || m.cam[1] != 0x05000000 || m.cam[2] != 0x11400000);
}

TEST(stage_0_steps_with_shift_13_above_0x1c00000_then_sets_the_look_at_and_steps_with_shift_11) {
    mover_ctx_t c = ctx(0);
    mover_t m;
    memset(&m, 0, sizeof m);
    m.tgt[0] = 0x10000000; m.tgt[1] = 0x1000000; m.tgt[2] = 0x10000000;
    m.cam[0] = 0x10000000; m.cam[1] = 0x1000000 + 0x1C00001; m.cam[2] = 0x10000000;   /* D = 0x1C00001: just above the threshold */
    mover_out_t o = mover_step(&m, &c);
    ASSERT_EQ_INT(o.look_on, 0);
    ASSERT_EQ_INT(m.stage, 0);
    ASSERT_EQ_INT(m.trail[1], -(0x200 << 13));                                     /* the unit vector 0x200 along the axis, shifted left by 13 */
    ASSERT_EQ_INT(m.trail[0], 0); ASSERT_EQ_INT(m.trail[2], 0);
    memset(&m, 0, sizeof m);
    m.tgt[0] = 0x10000000; m.tgt[1] = 0x1000000; m.tgt[2] = 0x10000000;
    m.cam[0] = 0x10000000; m.cam[1] = 0x1000000 + 0x1C00000; m.cam[2] = 0x10000000;   /* D = 0x1C00000: the look-at byte is set, the shift is 11 */
    o = mover_step(&m, &c);
    ASSERT_EQ_INT(o.look_on, 1);
    ASSERT_EQ_INT(m.stage, 0);
    ASSERT_EQ_INT(m.trail[1], -(0x1FF << 11));          /* the normaliser's length for this vector is 0x1FF (note 04a P4), shifted left by 11 */
}

TEST(the_step_is_averaged_with_the_previous_step_when_that_is_not_zero) {
    mover_ctx_t c = ctx(0);
    mover_t m;
    memset(&m, 0, sizeof m);
    m.tgt[0] = 0x10000000; m.tgt[1] = 0x1000000; m.tgt[2] = 0x10000000;
    m.cam[0] = 0x10000000; m.cam[1] = 0x1000000 + 0x1C00001 + 0x1000; m.cam[2] = 0x10000000;
    mover_step(&m, &c);                                  /* shift 13: -0x400000 */
    ASSERT_EQ_INT(m.trail[1], -(0x200 << 13));
    ASSERT_EQ_INT(m.cam[1], 0x1000000 + 0x1C01001 - (0x200 << 13));
    mover_step(&m, &c);                                  /* D = 0x1C01001 - 0x400000 = 0x1801001 <= 0x1C00000: shift 11, new = old + (new - old) / 2 */
    const int32_t old = -(0x200 << 13), nw = -(0x200 << 11);
    ASSERT_EQ_INT(m.trail[1], old + ((nw - old) >> 1));
}

TEST(the_blend_target_of_the_stages_0_to_2_is_the_five_halvings_of_the_camera_and_the_player) {
    mover_ctx_t c = ctx(0);
    mover_t m;
    memset(&m, 0, sizeof m);
    m.stage = 0;
    m.cam[0] = 0x10400000; m.cam[1] = 0x00800000; m.cam[2] = 0x10200000;
    m.tgt[0] = m.cam[0] + 0xD00000; m.tgt[1] = m.cam[1]; m.tgt[2] = m.cam[2];       /* D = 0xD00000: the stage advances to 1 and the target is rebuilt */
    mover_step(&m, &c);
    ASSERT_EQ_INT(m.stage, 1);
    int32_t cam[3] = { 0x10400000, 0x00800000, 0x10200000 }, a[3], b[3], cc[3], e[3], t[3];
    for (int i = 0; i < 3; i++) a[i] = (cam[i] + POS[i]) >> 1;
    b[0] = (a[0] + cam[0]) >> 1; b[1] = a[1] >> 1; b[2] = (a[2] + cam[2]) >> 1;
    for (int i = 0; i < 3; i++) { cc[i] = (b[i] + a[i]) >> 1; e[i] = (cc[i] + a[i]) >> 1; t[i] = (e[i] + cc[i]) >> 1; }
    for (int i = 0; i < 3; i++) ASSERT_EQ_INT(m.tgt[i], t[i]);
}

TEST(stage_3_ends_at_0x8000_with_the_offset_target_6_4_3_for_the_starts_1_and_2_and_minus_6_4_minus_9_for_the_others) {
    for (int loc = 0; loc <= 3; loc++) {
        mover_ctx_t c = ctx(loc);
        mover_t m;
        memset(&m, 0, sizeof m);
        m.stage = 3;
        memcpy(m.tgt, POS, sizeof m.tgt);
        m.cam[0] = POS[0] + 0x8000; m.cam[1] = POS[1]; m.cam[2] = POS[2];
        mover_step(&m, &c);
        ASSERT_EQ_INT(m.stage, 4);
        const int near = loc == 1 || loc == 2;
        ASSERT_EQ_INT(m.tgt[0], POS[0] + (near ? 6 : -6) * 1024);
        ASSERT_EQ_INT(m.tgt[1], POS[1] + 4 * 1024);
        ASSERT_EQ_INT(m.tgt[2], POS[2] + (near ? 3 : -9) * 1024);
        m.stage = 4;                                      /* stage 4 ends at 0x2800: the target 6 or -6, 1, 6 */
        m.cam[0] = m.tgt[0] + 0x2800; m.cam[1] = m.tgt[1]; m.cam[2] = m.tgt[2];
        mover_step(&m, &c);
        ASSERT_EQ_INT(m.stage, 5);
        ASSERT_EQ_INT(m.tgt[0], POS[0] + (near ? 6 : -6) * 1024);
        ASSERT_EQ_INT(m.tgt[1], POS[1] + 1 * 1024);
        ASSERT_EQ_INT(m.tgt[2], POS[2] + 6 * 1024);
    }
}

TEST(the_arrival_within_0x240_sets_stage_6_and_the_cockpit_request_comes_four_passes_later) {
    mover_ctx_t c = ctx(0);
    mover_t m;
    memset(&m, 0, sizeof m);
    m.stage = 5;
    memcpy(m.tgt, POS, sizeof m.tgt);
    m.cam[0] = POS[0] + 0x240; m.cam[1] = POS[1]; m.cam[2] = POS[2];
    mover_out_t o = mover_step(&m, &c);
    ASSERT_TRUE(o.arrived && o.look_off);
    ASSERT_EQ_INT(m.stage, 6);
    ASSERT_EQ_INT(o.cockpit, 0);
    const int32_t cam0 = m.cam[0];
    for (int i = 1; i <= 3; i++) { o = mover_step(&m, &c); ASSERT_EQ_INT(o.cockpit, 0); }
    o = mover_step(&m, &c);
    ASSERT_EQ_INT(o.cockpit, 1);                          /* V:4A95D: 5, 4 in the arrival pass, then 3, 2, 1, 0 */
    ASSERT_EQ_INT(m.cam[0], cam0);                        /* the stage 6 does not move the camera */
}

int main(void) {
    RUN(the_start_target_is_5760_ring_units_behind_the_player_in_its_own_frame);
    RUN(a_free_flight_approach_starts_at_the_selection_camera_in_stage_0_with_an_empty_trail);
    RUN(a_mission_approach_starts_at_the_briefing_overview_in_stage_8_and_steps_in_stage_0_from_its_fourth_pass);
    RUN(stage_0_steps_with_shift_13_above_0x1c00000_then_sets_the_look_at_and_steps_with_shift_11);
    RUN(the_step_is_averaged_with_the_previous_step_when_that_is_not_zero);
    RUN(the_blend_target_of_the_stages_0_to_2_is_the_five_halvings_of_the_camera_and_the_player);
    RUN(stage_3_ends_at_0x8000_with_the_offset_target_6_4_3_for_the_starts_1_and_2_and_minus_6_4_minus_9_for_the_others);
    RUN(the_arrival_within_0x240_sets_stage_6_and_the_cockpit_request_comes_four_passes_later);
    TEST_MAIN_END();
}
