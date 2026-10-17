/* tests/test_radar.c - the radar list, the threat bits, designation and the HUD target data (src/core/radar.c, docs/re/07-ai-and-weapons.md section 12, "note 07").
 *
 * Synthetic records only: the player at the origin with the identity attitude (so a record's list coordinates are its position difference divided by 256: x lateral, z ahead),
 * the other records placed at chosen offsets. The numbers are the note's (windows, limits, thresholds, cue bits). */
#include "test.h"
#include "../src/core/radar.h"

static entities_t E;
static aircraft_t P;
static radar_t R;
#define U8192 (8192 * 256)   /* one scope pixel at the range nibble 13, in position units */

static void setup(void) {
    memset(&P, 0, sizeof P);
    P.f.cls = 0x11; P.f.w0 = 0x11C8; P.f.id = 0x1D;   /* the gun, range nibble 13 */
    P.f.mat_b[0] = P.f.mat_b[4] = P.f.mat_b[8] = 16384;
    entities_init(&E, NULL, NULL, NULL);
    entities_attach_player(&E, &P);
    E.mode = 3; E.designated = -1;
    for (int i = 1; i < ENT_RECORDS; i++) { aircraft_t *a = entities_ac(&E, i); memset(a, 0, sizeof *a); a->f.mat_b[0] = a->f.mat_b[4] = a->f.mat_b[8] = 16384; }
    radar_reset(&R);
}

/* a valid record i of class cls at the list position (px, pz) pixels of the range nibble 13 (x lateral: the scope shows minus x, z ahead) */
static aircraft_t *put(int i, int cls, int w0extra, int px, int pz) {
    aircraft_t *a = entities_ac(&E, i);
    a->f.w0 = (uint16_t)(0x1040 | w0extra); a->f.cls = (uint8_t)cls;
    a->pos[0] = -px * U8192; a->pos[2] = pz * U8192; a->pos[1] = 0;   /* list x = -px * 8192: scope x = px */
    return a;
}

static radar_in_t in(uint32_t n) {
    radar_in_t r;
    memset(&r, 0, sizeof r);
    r.e = &E; r.n = n; r.cockpit = 1; r.zoom = 0x80; r.cam_y = 0;
    return r;
}

static void walk_once(uint32_t n) { radar_in_t i = in(n); radar_pass(&R, &i); }

/* ---- 12.2 the list builder ---------------------------------------------------------------------------------------------------------------- */

TEST(the_list_holds_the_valid_records_but_the_viewed_one_in_index_order_with_the_flag_word) {
    setup();
    put(5, 0x12, 0, 0, 10); put(2, 0x14, 0, 0, 20); put(9, 0x12, 0, 0, 30);
    entities_ac(&E, 3)->f.w0 = 0x0000;   /* invalid: left out */
    radar_build(&R, &E, NULL);
    ASSERT_EQ_INT(R.nlist, 3);
    ASSERT_EQ_INT(R.list[0].flag, 0x0210); ASSERT_EQ_INT(R.list[1].flag, 0x0510); ASSERT_EQ_INT(R.list[2].flag, 0x0910);
    ASSERT_EQ_INT(R.list[0].z, 20 * 8192);
}

TEST(a_list_value_is_the_relative_position_over_256_with_x_lateral_and_z_ahead) {
    setup();
    aircraft_t *a = put(4, 0x12, 0, 0, 0);
    a->pos[0] = 256 * 1000; a->pos[1] = 256 * 77; a->pos[2] = 256 * 3000;
    radar_build(&R, &E, NULL);
    ASSERT_EQ_INT(R.list[0].x, 1000); ASSERT_EQ_INT(R.list[0].z, 3000);
}

TEST(a_distant_record_is_shifted_by_an_even_count_so_that_the_word_precision_holds) {
    setup();
    aircraft_t *a = put(4, 0x12, 0, 0, 0);
    a->pos[0] = 0x10000000;   /* 2^28: the largest component after >> 0 exceeds 0x7FFFFF; the smallest even shift that brings it to at most 0x7FFFFF is 6 */
    radar_build(&R, &E, NULL);
    ASSERT_EQ_INT(R.list[0].x, (0x10000000 >> 14) << 6);   /* ((d >> (6 + 8)) * 256 >> 8) << 6 */
}

TEST(the_rotation_words_come_from_the_viewed_records_matrix_or_the_caller) {
    setup();
    aircraft_t *a = put(4, 0x12, 0, 0, 0);
    a->pos[0] = 256 * 100; a->pos[2] = 256 * 300;
    int16_t rot[9] = { 0, 0, 256, 0, 256, 0, -256, 0, 0 };   /* a quarter turn: x = dz, z = -dx */
    radar_build(&R, &E, rot);
    ASSERT_EQ_INT(R.list[0].x, 300); ASSERT_EQ_INT(R.list[0].z, -100);
}

/* ---- 12.3 the walk ------------------------------------------------------------------------------------------------------------------------- */

TEST(the_gate_builds_on_n_mod_4_zero_and_walks_on_n_mod_4_zero_and_one_only) {
    setup();
    put(4, 0x12, 0, 3, 5);
    walk_once(0); ASSERT_EQ_INT(R.walk_done, 1); ASSERT_EQ_INT(R.nlist, 1);
    put(6, 0x12, 0, 1, 1);
    walk_once(1); ASSERT_EQ_INT(R.walk_done, 1); ASSERT_EQ_INT(R.nlist, 1);   /* the stale list is walked again: record 6 not yet in it */
    walk_once(2); ASSERT_EQ_INT(R.walk_done, 0);
    walk_once(3); ASSERT_EQ_INT(R.walk_done, 0);
    walk_once(4); ASSERT_EQ_INT(R.walk_done, 1); ASSERT_EQ_INT(R.nlist, 2);
    radar_in_t i = in(2); i.redraw = 2; radar_pass(&R, &i); ASSERT_EQ_INT(R.walk_done, 1);   /* a page redraw counter above 1 builds and walks every pass */
}

TEST(the_scope_draw_walks_twice_on_a_redraw_pass_and_once_otherwise_with_the_pause_byte_no_gate) {
    /* 07 12.1: R = 0 ends in one walk (V:4A93B +1); R of 1 or more in two (cone lines, then the plus), both on the page the display flip chose; the second raises no cue of its own */
    setup();
    put(4, 0x12, 0, 3, 5);
    walk_once(0); ASSERT_EQ_INT(R.frame, 1); ASSERT_EQ_INT(R.cur_page, 1);
    for (int r = 1; r <= 3; r++) {
        setup();
        put(4, 0x12, 0, 3, 5);
        radar_in_t i = in(0); i.redraw = (int16_t)r; radar_pass(&R, &i);
        ASSERT_EQ_INT(R.frame, 2);                         /* two walks */
        ASSERT_EQ_INT(R.cur_page, 1);                      /* one flip per pass: both walks wrote page 0 */
        ASSERT_EQ_INT(R.nblip[0], 1); ASSERT_EQ_INT(R.nblip[1], 0);
    }
    /* the threat bits are rebuilt by the second walk and the cue is raised once: a hostile aircraft in the box */
    setup();
    put(4, 0x12, 0, 3, 5);
    radar_in_t i = in(0); i.redraw = 1; radar_pass(&R, &i);
    ASSERT_EQ_INT(R.threat, 0x04); ASSERT_EQ_INT(R.threat_prev, 0x04);
    ASSERT_EQ_INT(P.f.v4ac0c, 0x0002);
    /* the mission-1 counter is stepped by each walk */
    setup();
    put(4, 0x12, 0, 3, 5); E.designated = 4;
    i = in(0); i.redraw = 1; radar_pass(&R, &i);
    ASSERT_EQ_INT(R.mission1, 0x1D);                       /* 0 -> 0x1E at the first walk, 0x1D at the second */
    /* the pause byte of a script's count is no gate of the list builder or of the walk */
    setup();
    put(4, 0x12, 0, 3, 5);
    i = in(0); i.paused = 1; radar_pass(&R, &i);
    ASSERT_EQ_INT(R.walk_done, 1); ASSERT_EQ_INT(R.nlist, 1);
}

TEST(the_erase_list_of_a_double_walk_is_the_first_walks_and_the_second_walk_redraws_over_it) {
    setup();
    put(4, 0x12, 0, 3, 5);
    walk_once(0);                                          /* page 0 now holds one blip */
    walk_once(0);                                          /* page 1 */
    radar_in_t i = in(0); i.redraw = 1; radar_pass(&R, &i);   /* page 0 twice: the erase list the frontend sees is what the first walk read: page 0's blip of two passes ago */
    ASSERT_EQ_INT(R.nerase, 1);
    ASSERT_EQ_INT(R.erase[0].x, R.blip[0][0].x);
    ASSERT_EQ_INT(R.nblip[0], 1);
}

TEST(an_external_view_without_the_panel_counter_leaves_the_list_and_the_flags_alone_and_with_it_walks) {
    setup();
    put(4, 0x12, 0, 3, 5);
    radar_in_t i = in(0); i.cockpit = 0; i.no_panel = 1; radar_pass(&R, &i);   /* the external block 0x113F8 runs: no scope draw */
    ASSERT_EQ_INT(R.nlist, 0); ASSERT_EQ_INT(R.walk_done, 0);
    i.no_panel = 0; radar_pass(&R, &i);                                        /* V:4A8EE non-zero: the panel block runs in the external view too (note 07 12.1) */
    ASSERT_EQ_INT(R.nlist, 1); ASSERT_EQ_INT(R.walk_done, 1);
}

TEST(the_window_is_27_either_side_16_ahead_and_21_behind_in_pixels_of_the_nibble_13) {
    setup();
    aircraft_t *in1 = put(1, 0x12, 0, 27, 16), *out1 = put(2, 0x12, 0, 28, 0), *in2 = put(3, 0x12, 0, -27, -21), *out2 = put(4, 0x12, 0, 0, 17);
    (void)in1; (void)in2;
    walk_once(0);
    ASSERT_EQ_INT(R.nblip[0], 2);
    ASSERT_TRUE(!(out1->f.w0 & EW0_RADAR) && !(out2->f.w0 & EW0_RADAR));
    ASSERT_TRUE(entities_ac(&E, 1)->f.w0 & EW0_RADAR);
    /* scope x = px + 158, y = -pz... : the point of the record at (27, ahead 16) */
    ASSERT_EQ_INT(R.blip[0][0].x, 158 + 27); ASSERT_EQ_INT(R.blip[0][0].y, 167 - 16);
    ASSERT_EQ_INT(R.list[0].flag & RADAR_DRAWN, RADAR_DRAWN);
    ASSERT_EQ_INT(R.list[1].flag & RADAR_DRAWN, 0);
}

TEST(the_scope_range_nibbles_13_11_and_9_scale_the_blip_by_4_and_16) {
    setup();
    put(4, 0x12, 0, 0, 0);
    entities_ac(&E, 4)->pos[0] = -256 * 8192 * 4; entities_ac(&E, 4)->pos[2] = 0;   /* list x = -32768 -> 4 pixels at nibble 13 */
    walk_once(0); ASSERT_EQ_INT(R.blip[0][0].x, 158 + 4);
    P.f.id = 0x1B; R.cur_page = 0; walk_once(0);   /* nibble 11: 2048 list units a pixel -> 16 pixels */
    ASSERT_EQ_INT(R.blip[0][0].x, 158 + 16);
    P.f.id = 0x19; R.cur_page = 0; walk_once(0);   /* nibble 9: 512 -> 64 pixels: outside the window, no blip (the record is still in the 13 window) */
    ASSERT_EQ_INT(R.nblip[0], 0);
    ASSERT_TRUE(entities_ac(&E, 4)->f.w0 & EW0_RADAR);
}

TEST(the_detection_box_is_0x10000_list_units_and_sets_or_clears_b20_bit_6) {
    setup();
    aircraft_t *a = put(4, 0x12, 0, 0, 0);
    a->pos[2] = 0x1000000;   /* above 0x7FFFFF: the shift 2 gives the list value 0x10000, exactly the limit: detected (nz = -8, inside the window) */
    a->f.b20 = 0;
    walk_once(0);
    ASSERT_EQ_INT(R.list[0].z, 0x10000);
    ASSERT_TRUE(a->f.b20 & 0x40);
    a->pos[2] = 0x1000400; a->f.b20 = 0x40;   /* list value 0x10004 */
    walk_once(0);
    ASSERT_TRUE(!(a->f.b20 & 0x40));
}

TEST(the_designated_blip_blinks_with_bit_0_of_the_radar_frame_counter_and_a_page_holds_ten_blips) {
    setup();
    for (int i = 1; i <= 13; i++) put(i, 0x12, 0, i - 7, 3);
    E.designated = 3;
    walk_once(0);   /* frame 1: bit 0 set: drawn */
    ASSERT_EQ_INT(R.nblip[0], 10);
    int seen = 0; for (int k = 0; k < R.nblip[0]; k++) if (R.blip[0][k].rec == 3) seen = 1;
    ASSERT_TRUE(seen);
    ASSERT_TRUE(entities_ac(&E, 13)->f.w0 & EW0_RADAR);   /* a later entry is neither stored nor drawn but its bookkeeping is done */
    ASSERT_EQ_INT(R.list[12].flag & RADAR_DRAWN, RADAR_DRAWN);
    walk_once(1);   /* frame 2: the designated blip is not drawn; page 1 */
    seen = 0; for (int k = 0; k < R.nblip[1]; k++) if (R.blip[1][k].rec == 3) seen = 1;
    ASSERT_TRUE(!seen);
    ASSERT_EQ_INT(R.nblip[1], 10);
    ASSERT_EQ_INT(R.nerase, 0);
    walk_once(4);   /* page 0 again: the erase list is what the page drew two walks ago */
    /* ELEVEN entries (12.3 step 1): a full page has no room for its terminator, so page 0's ten blips are followed in memory by the first entry of page 1 (the page walk 2 drew), which the erase
     * reads as the eleventh; and page 0's own terminator overwrote the first word of page 1 before that page's walk 2, so its erase found an empty list (nerase 0 above) */
    ASSERT_EQ_INT(R.nerase, 11);
    ASSERT_EQ_INT(R.erase[10].x, R.blip[1][0].x);
    ASSERT_EQ_INT(R.erase[10].y, R.blip[1][0].y);
    ASSERT_EQ_INT(R.erase[0].x, R.blip[0][0].x);
}

TEST(a_page_of_ten_blips_ends_its_list_on_the_first_word_of_the_other_page_and_page_1_on_the_word_past_both) {
    setup();
    for (int i = 1; i <= 12; i++) put(i, 0x12, 0, i - 6, 3);
    walk_once(0);                                         /* page 0: ten blips, the terminator is the first word of page 1 */
    ASSERT_EQ_INT(R.nblip[0], 10);
    ASSERT_EQ_INT(R.emem[20], 0xFFFF);
    ASSERT_EQ_INT(R.emem[0], (uint16_t)(R.blip[0][0].x | (R.blip[0][0].wide ? 0x8000 : 0)));
    ASSERT_EQ_INT(R.emem[1], (uint16_t)R.blip[0][0].y);
    walk_once(1);                                         /* page 1: ten blips; its first entry sits where the terminator was, its own terminator is the word at 0x53818 */
    ASSERT_EQ_INT(R.nblip[1], 10);
    ASSERT_EQ_INT(R.emem[40], 0xFFFF);
    ASSERT_EQ_INT(R.emem[20], (uint16_t)(R.blip[1][0].x | (R.blip[1][0].wide ? 0x8000 : 0)));
    game_hud_t hud; memset(&hud, 0, sizeof hud);
    radar_fill_hud(&R, &E, &hud);
    ASSERT_EQ_INT(hud.blips[3].x, R.blip[1][3].x);   /* the scene carries the page the last walk wrote */
}

TEST(a_wide_blip_is_stored_with_bit_15_and_a_short_page_ends_with_its_own_terminator_word) {
    setup();
    put(1, 0x12, 0, 3, 3); entities_ac(&E, 1)->pos[1] = 5 * 256;   /* above the camera (cam_y 0): wide */
    walk_once(0);
    ASSERT_EQ_INT(R.nblip[0], 1);
    ASSERT_TRUE(R.blip[0][0].wide);
    ASSERT_EQ_INT(R.emem[0] & 0x8000, 0x8000);
    ASSERT_EQ_INT(R.emem[2], 0xFFFF);
    walk_once(1);                                         /* page 1 starts empty after the reset */
    walk_once(4);
    ASSERT_EQ_INT(R.nerase, 1);
    ASSERT_TRUE(R.erase[0].wide);
    ASSERT_EQ_INT(R.erase[0].x, R.blip[0][0].x);
}

TEST(the_pens_follow_the_class_the_detected_bit_the_out_of_control_bit_and_the_counted_bit) {
    setup();
    put(1, 0x20, 0, 1, 1);                   /* a ship: 5 */
    put(2, 0x12, 0x0008, 2, 1);              /* not counted aircraft: 4 */
    put(3, 0x12, 0, 3, 1);                   /* hostile: 1 */
    aircraft_t *c = put(4, 0x00, 0, 4, 1);   /* a missile: 2 */
    aircraft_t *o = put(5, 0x12, 0, 5, 1); o->f.b20 = 0x02;   /* out of control: 8 */
    aircraft_t *u = put(6, 0x12, 0, 6, 1); u->pos[2] = 256 * 0x20000;   /* undetected: 8 (and 18 ahead... outside the window at nibble 13, so the pen is not observable) */
    (void)c; (void)u;
    walk_once(0);
    ASSERT_EQ_INT(R.blip[0][0].pen, 5); ASSERT_EQ_INT(R.blip[0][1].pen, 4); ASSERT_EQ_INT(R.blip[0][2].pen, 1);
    ASSERT_EQ_INT(R.blip[0][3].pen, 2); ASSERT_EQ_INT(R.blip[0][4].pen, 8);
}

TEST(a_blip_is_two_pixels_wide_when_the_height_is_at_least_minus_the_camera_offset) {
    setup();
    aircraft_t *a = put(4, 0x12, 0, 0, 0);
    a->f.alt8 = 100;
    radar_in_t i = in(0); i.cam_y = -100; radar_pass(&R, &i); ASSERT_EQ_INT(R.blip[0][0].wide, 1);
    i.cam_y = -101; R.cur_page = 0; radar_pass(&R, &i); ASSERT_EQ_INT(R.blip[0][0].wide, 0);
    i.cam_y = -100; i.hud_row = -1; i.view_off = 3; R.cur_page = 0; radar_pass(&R, &i);
    ASSERT_EQ_INT(R.blip[0][0].y, 167 - 1); ASSERT_EQ_INT(R.blip[0][0].x, 158 + 3);
}

TEST(a_record_on_the_ground_or_invalid_is_not_plotted_and_loses_the_radar_bit) {
    setup();
    aircraft_t *g = put(4, 0x12, 0, 0, 3); g->f.w2 = EW2_GROUND; g->f.w0 |= EW0_RADAR;
    walk_once(0);
    ASSERT_EQ_INT(R.nblip[0], 0);
    ASSERT_TRUE(!(g->f.w0 & EW0_RADAR));
}

TEST(the_mission_1_counter_counts_31_aircraft_contacts_with_a_target_designated) {
    setup();
    put(4, 0x12, 0, 0, 3);
    walk_once(0); ASSERT_EQ_INT(R.mission1, 0);   /* no target designated: not counted */
    E.designated = 4;
    walk_once(1); ASSERT_EQ_INT(R.mission1, 0x1E);
    for (int k = 2; k <= 30; k++) walk_once(1);
    ASSERT_EQ_INT(R.mission1, 1);
    walk_once(1); ASSERT_EQ_INT(R.mission1, 0xFF);   /* the 31st contact */
    ASSERT_TRUE((int8_t)R.mission1 < 0);             /* the sign the F1 script tests: the game feeds this byte to missions_view_t.in_ident (game.c missions_g; tests/golden/mission-f1.txt reaches the end code through it) */
    walk_once(1); ASSERT_EQ_INT(R.mission1, 0xFF);
}

TEST(modes_2_and_0x7d_skip_the_detection_and_the_classification) {
    setup();
    aircraft_t *a = put(4, 0x12, 0, 0, 3); a->f.b20 = 0x40;
    E.mode = 2;
    walk_once(0); ASSERT_EQ_INT(R.threat, 0); ASSERT_TRUE(a->f.b20 & 0x40);
    a->pos[2] = 256 * 0x20000 + 256 * 8192 * 3; a->f.b20 = 0;   /* far away: the bit is left as it is (clear) */
    E.mode = 0x7D; R.cur_page = 0; walk_once(0);
    ASSERT_TRUE(!(a->f.b20 & 0x40));
}

/* ---- 12.3 / 12.4 threat bits and cues ----------------------------------------------------------------------------------------------------- */

TEST(threat_bits_per_class_and_flag) {
    setup();
    put(1, 0x11, 0x0008, 1, 1);                       /* friendly aircraft: bit 1 */
    walk_once(0); ASSERT_EQ_INT(R.threat, 0x02);
    put(2, 0x12, 0, 2, 1);                            /* hostile: bit 2 */
    R.cur_page = 0; walk_once(0); ASSERT_EQ_INT(R.threat, 0x06);
    put(3, 0x15, 0, 3, 1);                            /* cruise missile: bit 3 */
    R.cur_page = 0; walk_once(0); ASSERT_EQ_INT(R.threat, 0x0E);
}

TEST(a_hostile_missile_aimed_at_the_flown_aircraft_sets_bit_4_or_5_and_a_players_missile_none) {
    setup();
    aircraft_t *m = put(5, 0x00, 0, 1, 1);
    E.rec[5].target = 0x80;   /* the player: low seven bits 0 */
    walk_once(0); ASSERT_EQ_INT(R.threat, 0x10);
    m->f.cls = 0x01; R.cur_page = 0; walk_once(0); ASSERT_EQ_INT(R.threat, 0x20);
    m->f.w0 |= EW0_NOT_COUNTED; R.cur_page = 0; walk_once(0); ASSERT_EQ_INT(R.threat, 0);
    m->f.w0 &= (uint16_t)~EW0_NOT_COUNTED; E.rec[5].target = 0x87; R.cur_page = 0; walk_once(0); ASSERT_EQ_INT(R.threat, 0);
}

TEST(wrecks_out_of_control_records_and_class_nibble_3_raise_no_threat) {
    setup();
    aircraft_t *a = put(1, 0x12, 0, 1, 1); a->f.b20 = 0x02;
    aircraft_t *b = put(2, 0x12, 0x0600, 2, 1);
    aircraft_t *c = put(3, 0x30, 0, 3, 1);
    aircraft_t *d = put(4, 0x20, 0, 4, 1);
    (void)a; (void)b; (void)c; (void)d;
    walk_once(0); ASSERT_EQ_INT(R.threat, 0);
}

static uint32_t cue_for(uint8_t old_bits, uint8_t new_bits, int mode) {
    setup();
    E.mode = (uint8_t)mode;
    R.threat_prev = old_bits; R.threat = new_bits;
    P.f.v4ac0c = 0; R.holdoff = 0;
    /* run only the dispatch through a walk with the bits fixed by records is awkward: build the records that produce new_bits */
    int slot = 1;
    if (new_bits & 0x10) { put(slot, 0x00, 0, 1, 1); E.rec[slot].target = 0x80; slot++; }
    if (new_bits & 0x20) { put(slot, 0x01, 0, 1, 1); E.rec[slot].target = 0x80; slot++; }
    if (new_bits & 0x08) { put(slot++, 0x15, 0, 1, 1); }
    if (new_bits & 0x04) { put(slot++, 0x12, 0, 1, 1); }
    if (new_bits & 0x02) { put(slot++, 0x11, 0x0008, 1, 1); }
    walk_once(0);
    if (R.threat != new_bits || R.threat_prev != new_bits) return 0xFFFFu;
    return P.f.v4ac0c;
}

TEST(the_cue_rows_of_12_4) {
    ASSERT_EQ_INT(cue_for(0, 0x10, 3), 0x0001);          /* bit 4 newly set */
    ASSERT_EQ_INT(cue_for(0x10, 0x10, 3), 0);            /* already set */
    ASSERT_EQ_INT(cue_for(0, 0x20, 3), 0x0001);
    ASSERT_EQ_INT(cue_for(0x20, 0x30, 3), 0x0001);       /* bit 4 newly set while 5 was: the bit 4 row decides */
    ASSERT_EQ_INT(cue_for(0x10, 0x30, 3), 0);            /* bit 4 held, bit 5 new: only the first matching row applies, no cue */
}

TEST(the_cue_rows_of_12_4_hostile_cruise_friendly_and_the_hold_off) {
    ASSERT_EQ_INT(cue_for(0, 0x08, 3), 0x0002); ASSERT_EQ_INT(R.holdoff, 0x18);
    ASSERT_EQ_INT(cue_for(0x08, 0x08, 3), 0);
    ASSERT_EQ_INT(cue_for(0x08, 0x0C, 3), 0x0002);       /* bit 3 held, bit 2 newly set */
    ASSERT_EQ_INT(cue_for(0x0C, 0x0C, 3), 0);
    ASSERT_EQ_INT(cue_for(0, 0x04, 3), 0x0002); ASSERT_EQ_INT(R.holdoff, 0x18);
    ASSERT_EQ_INT(cue_for(0x04, 0x04, 3), 0); ASSERT_EQ_INT(R.holdoff, 0);
    ASSERT_EQ_INT(cue_for(0, 0x02, 3), 0x0800);
    ASSERT_EQ_INT(cue_for(0x02, 0x02, 3), 0);
    ASSERT_EQ_INT(cue_for(0, 0x02, 5), 0);               /* not in mode 5 */
}

TEST(the_friendly_row_applies_when_the_hostile_bit_was_already_set) {
    ASSERT_EQ_INT(cue_for(0x04, 0x06, 3), 0x0800);
}

TEST(the_threat_bits_are_remembered_for_the_next_walk_even_when_empty) {
    setup();
    put(1, 0x12, 0, 1, 1);
    walk_once(0); ASSERT_EQ_INT(R.threat_prev, 0x04);
    entities_ac(&E, 1)->f.w0 = 0;
    radar_in_t i = in(4); radar_pass(&R, &i);
    ASSERT_EQ_INT(R.threat, 0); ASSERT_EQ_INT(R.threat_prev, 0);
}

/* ---- 12.5 designation ---------------------------------------------------------------------------------------------------------------------- */

TEST(the_t_key_cycles_none_first_candidate_to_last_and_none_again) {
    setup();
    put(2, 0x12, 0, 1, 3); put(5, 0x14, 0, 2, 3); put(9, 0x16, 0, 3, 3);
    int seq[6], k = 0;
    for (int t = 0; t < 5; t++) { radar_key_target(&R); walk_once(0); seq[k++] = E.designated; }
    ASSERT_EQ_INT(seq[0], 2); ASSERT_EQ_INT(seq[1], 5); ASSERT_EQ_INT(seq[2], 9); ASSERT_EQ_INT(seq[3], -1); ASSERT_EQ_INT(seq[4], 2);
}

TEST(designation_sets_the_page_the_countdown_the_holdoff_and_the_list_position) {
    setup();
    put(2, 0x12, 0, 1, 3); put(5, 0x14, 0, 2, 3);
    P.f.v4a986 = 0x2000;
    radar_key_target(&R); walk_once(0);
    ASSERT_EQ_INT(E.designated, 2); ASSERT_EQ_INT(R.page, 1); ASSERT_EQ_INT(R.countdown, 1); ASSERT_EQ_INT((uint8_t)R.holdoff, 0xFF);
    ASSERT_EQ_INT(R.listpos, 1); ASSERT_EQ_INT(P.f.v4a986 & 0x2000, 0);
    radar_key_target(&R); walk_once(0);
    ASSERT_EQ_INT(E.designated, 5); ASSERT_EQ_INT(R.listpos, 2);
    radar_key_target(&R); walk_once(0); ASSERT_EQ_INT(E.designated, -1); ASSERT_EQ_INT(R.listpos, 0);
}

TEST(candidates_need_bits_4_and_5_and_an_aircraft_class_and_no_out_of_control) {
    setup();
    put(1, 0x00, 0, 1, 3);                                   /* a missile */
    put(2, 0x20, 0, 2, 3);                                   /* a ship */
    put(3, 0x30, 0, 3, 3);                                   /* the seat */
    aircraft_t *o = put(4, 0x12, 0, 4, 3); o->f.b20 = 0x02;  /* out of control */
    put(5, 0x12, 0, 40, 3);                                  /* outside the window: not drawn */
    put(6, 0x11, 0x0008, 6, 3);                              /* a friendly aircraft IS a candidate */
    radar_key_target(&R); walk_once(0);
    ASSERT_EQ_INT(E.designated, 6);
}

TEST(a_target_missing_from_the_list_restarts_at_the_top_and_the_request_waits_for_a_walk) {
    setup();
    put(2, 0x12, 0, 1, 3);
    E.designated = 9;   /* not in the list */
    radar_key_target(&R);
    walk_once(2);       /* no walk on n = 2: the request stays */
    ASSERT_EQ_INT(R.req_target, 1); ASSERT_EQ_INT(E.designated, 9);
    walk_once(0);
    ASSERT_EQ_INT(E.designated, 2); ASSERT_EQ_INT(R.req_target, 0);
}

/* ---- 12.6 the range ---------------------------------------------------------------------------------------------------------------------- */

static void tgt_at(int idx, int dcx, int fine_x, int dalt) {
    aircraft_t *t = entities_ac(&E, idx);
    t->f.cell_x = (int16_t)dcx; t->f.fine_x = (int16_t)fine_x; t->f.alt8 = dalt;
    E.designated = (int16_t)idx;
}

TEST(the_range_is_recomputed_every_pass_below_0x480_every_third_to_0x900_and_every_sixth_beyond) {
    setup();
    tgt_at(4, 0, 300, 0);
    E.rec[0].range = 0x7FFF;           /* far: the counter must exceed 5 */
    P.f.repeat = 0x00;
    int passes = 0;
    do { entities_update_player_range(&E); passes++; } while (E.rec[0].range == 0x7FFF && passes < 10);
    ASSERT_EQ_INT(passes, 6); ASSERT_EQ_INT(E.rec[0].range, 300);
    ASSERT_TRUE(P.f.b4 & EB4_RANGE_DONE);
    ASSERT_EQ_INT(P.f.repeat & 0xF0, 0);
    tgt_at(4, 0, 600, 0); E.rec[0].range = 0x7FFF;
    P.f.repeat = 0x00;
    E.rec[0].range = 0x500;            /* between 0x480 and 0x900: the counter must exceed 2 */
    passes = 0;
    do { entities_update_player_range(&E); passes++; } while (E.rec[0].range == 0x500 && passes < 10);
    ASSERT_EQ_INT(passes, 3); ASSERT_EQ_INT(E.rec[0].range, 600);
    E.rec[0].range = 0x100; entities_update_player_range(&E); ASSERT_EQ_INT(E.rec[0].range, 600);   /* below 0x480: every pass */
}

TEST(the_range_keeps_the_low_nibble_of_the_counter_byte_and_needs_a_designated_target) {
    setup();
    P.f.repeat = 0x05; E.designated = -1;
    entities_update_player_range(&E); ASSERT_EQ_INT(P.f.repeat, 0x05);
    tgt_at(4, 0, 100, 0); E.rec[0].range = 0;
    entities_update_player_range(&E); ASSERT_EQ_INT(P.f.repeat, 0x05);
    E.designated = 0;   /* index 0 is the player and never a target */
    P.f.repeat = 0x05; entities_update_player_range(&E); ASSERT_EQ_INT(P.f.repeat, 0x05);
}

TEST(a_component_above_0x7f00_gives_0x7fff_and_leaves_b4_bit_0_alone) {
    setup();
    tgt_at(4, 3, 0, 0);   /* 3 cells = 3 * 16384 fine units */
    E.rec[0].range = 0; P.f.b4 = 0;
    entities_update_player_range(&E);
    ASSERT_EQ_INT((uint16_t)E.rec[0].range, 0x7FFF); ASSERT_EQ_INT(P.f.b4 & 1, 0);
    tgt_at(4, 0, 0, 0x7F01);
    E.rec[0].range = 0; entities_update_player_range(&E);
    ASSERT_EQ_INT((uint16_t)E.rec[0].range, 0x7FFF);
    tgt_at(4, 0, 0x100, 0x200);
    E.rec[0].range = 0; entities_update_player_range(&E);
    { fx_vec_t v = { 0x100, 0x200, 0 }; ASSERT_EQ_INT(E.rec[0].range, fx_dist(&v)); }
}

/* ---- 12.7 the lock state, the prompt, the pipper, the range number -------------------------------------------------------------------------- */

static void hud_setup(int sel) {
    setup();
    P.f.id = (uint8_t)(sel | 0x0D);
    put(4, 0x12, 0, 1, 3);
    E.designated = 4;
}

static void framep(uint32_t n, int tx, int ty);
static void frame(uint32_t n, int tx, int ty) { radar_in_t i = in(n); i.tgt_x = (int16_t)tx; i.tgt_y = (int16_t)ty; radar_hud_pass(&R, &i); }

TEST(the_marker_is_invalid_with_no_weapon_or_no_target_point_and_starts_at_159_91) {
    hud_setup(0x20);
    frame(0, 150, 70);
    ASSERT_EQ_INT(R.mx, 156); ASSERT_EQ_INT(R.my, 88);   /* the first frame with a valid target: the first iteration initialises (159, 91) and takes no step, the other three step (0x38D8C) */
    frame(1, 150, 70);
    ASSERT_EQ_INT(R.mx, 153); ASSERT_EQ_INT(R.my, 84);   /* four iterations of at most one pixel toward (150, 70); a gap of 3 shifted right by 2 is 0 */
    P.f.id = 0x0D; frame(2, 150, 70); ASSERT_EQ_INT(R.mx, -1);
    P.f.id = 0x2D; frame(3, 0, 70); ASSERT_EQ_INT(R.mx, -1);
}

/* The slew's loop (0x38D8C, tests/rig/test_radar_diff.c part slew): the early return of the box point, the script's count, the jitter and the invalid marker's y. */
TEST(the_slew_returns_after_one_step_when_the_box_point_is_not_positive_and_the_script_count_freezes_the_marker) {
    hud_setup(0x20);
    R.mx = 100; R.my = 80; R.tpx = 5; R.tpy = 6;
    { radar_in_t i = in(0); i.tgt_x = 150; i.tgt_y = 70; i.hud_row = -70; radar_slew(&R, &i); }   /* y + V:4A990 = 0: the first iteration moves the marker once and returns over the copy */
    ASSERT_EQ_INT(R.mx, 101); ASSERT_EQ_INT(R.my, 79);
    ASSERT_EQ_INT(R.tpx, 5); ASSERT_EQ_INT(R.tpy, 6);
    { radar_in_t i = in(0); i.tgt_x = 150; i.tgt_y = 70; radar_slew(&R, &i); }   /* four iterations, then the copy */
    ASSERT_EQ_INT(R.mx, 105); ASSERT_EQ_INT(R.my, 75);
    ASSERT_EQ_INT(R.tpx, 150); ASSERT_EQ_INT(R.tpy, 70);
    { radar_in_t i = in(0); i.tgt_x = 150; i.tgt_y = 70; i.paused = 1; radar_slew(&R, &i); }   /* V:4A866: the box is drawn and the point copied, the marker stays */
    ASSERT_EQ_INT(R.mx, 105); ASSERT_EQ_INT(R.my, 75);
    R.tpx = 0;
    { radar_in_t i = in(0); i.tgt_x = 151; i.tgt_y = 71; i.paused = 1; radar_slew(&R, &i); }
    ASSERT_EQ_INT(R.tpx, 151);
    /* the jitter pair is added to the box point only when record 0's +0x56 is non-zero: x 1 + the pair x -1 is 0, the box point is not positive */
    P.f.rate_p = 7; entities_ac(&E, 0)->pos[0] = 0x1C;   /* index 7: (-1, -1) */
    R.tpx = 0;
    { radar_in_t i = in(0); i.tgt_x = 1; i.tgt_y = 50; i.paused = 1; radar_slew(&R, &i); }
    ASSERT_EQ_INT(R.tpx, 0);
    P.f.rate_p = 0;
    { radar_in_t i = in(0); i.tgt_x = 1; i.tgt_y = 50; i.paused = 1; radar_slew(&R, &i); }
    ASSERT_EQ_INT(R.tpx, 1);
    /* an invalid marker keeps the old x in y (D2 of the original), the second iteration then the -1 */
    R.mx = 77; R.my = 66;
    { radar_in_t i = in(0); i.tgt_x = 0; i.tgt_y = 50; i.view_off = 0; radar_slew(&R, &i); }   /* x <= 0: the box point is not positive either, so one iteration only */
    ASSERT_EQ_INT(R.mx, -1); ASSERT_EQ_INT(R.my, 77);
}


static void framep(uint32_t n, int tx, int ty) { R.px = 100; R.py = 50; frame(n, tx, ty); }   /* the pipper is projected at the end of every sight call: set a visible one first */

/* the lock state's rows are tested on the function directly: the marker (mx, my), the previous target point (tpx, tpy), the range and the closing delta are set by hand */
#define BOX 0x4000
#define NEAR_ 0x0200

static void lock_setup(int sel, int range, int closing) {
    hud_setup(sel);
    E.rec[0].range = (int16_t)range;
    R.closing = (int16_t)closing;
    R.mx = 159; R.my = 91; R.tpx = 159; R.tpy = 91;   /* in the box */
    P.f.v4ac08 = 0; P.f.v4ac0c = 0; R.prompt = 0;
}
static void lock(uint32_t n) { radar_in_t i = in(n); radar_lock_state(&R, &i); }

TEST(lock_row_1_no_target_gun_or_invalid_marker_clears_the_bits_and_cues_once) {
    lock_setup(0x20, 0x100, 0);
    P.f.v4ac08 = BOX | NEAR_; E.designated = -1;
    lock(0); ASSERT_EQ_INT(P.f.v4ac08 & (BOX | NEAR_), 0); ASSERT_EQ_INT(P.f.v4ac0c, 0x08);
    P.f.v4ac0c = 0; lock(0); ASSERT_EQ_INT(P.f.v4ac0c, 0);   /* nothing was set: no cue */
    lock_setup(0x10, 0x100, 0); P.f.v4ac08 = BOX; lock(0); ASSERT_EQ_INT(P.f.v4ac08 & BOX, 0);
    lock_setup(0x20, 0x100, 0); R.mx = -1; P.f.v4ac08 = NEAR_; R.prompt = 1; lock(0); ASSERT_EQ_INT(R.prompt, 1);   /* V:4A96C unchanged in row 1 */
}

TEST(lock_row_2_marker_outside_the_box_x_97_221_y_47_133_clears_the_prompt) {
    lock_setup(0x20, 0x100, 0);
    R.mx = 96; P.f.v4ac08 = BOX; R.prompt = 2; lock(0);
    ASSERT_EQ_INT(R.prompt, 0); ASSERT_EQ_INT(P.f.v4ac08 & BOX, 0); ASSERT_EQ_INT(P.f.v4ac0c, 0x08);
    lock_setup(0x20, 0x100, 0); R.mx = 221; R.my = 133; R.tpx = 221; R.tpy = 133; lock(0); ASSERT_EQ_INT(P.f.v4ac08 & BOX, BOX);   /* the corners are inside */
}

TEST(lock_row_3_marker_more_than_5_pixels_from_the_target_point_loses_the_lock) {
    lock_setup(0x20, 0x100, 0);
    R.tpx = 165; P.f.v4ac08 = BOX; R.prompt = 1; lock(0);
    ASSERT_EQ_INT(P.f.v4ac08 & (BOX | NEAR_), NEAR_); ASSERT_EQ_INT(P.f.v4ac0c, 0x100); ASSERT_EQ_INT(R.prompt, 0);
    P.f.v4ac0c = 0; lock(1); ASSERT_EQ_INT(P.f.v4ac0c, 0);                 /* 0x0200 already set: silent ... */
    lock(9); ASSERT_EQ_INT(P.f.v4ac0c, 0x100);                              /* ... except on passes with (n & 0x1F) = 9 */
    lock_setup(0x20, 0x100, 0); R.tpx = 164; lock(0); ASSERT_EQ_INT(P.f.v4ac08 & BOX, BOX);   /* 5 pixels is still in the box */
}

TEST(lock_row_4_in_the_box_with_another_selection_sets_the_box_bit_only) {
    lock_setup(0x40, 0x100, 0);
    P.f.v4ac08 = NEAR_; R.prompt = 2; lock(0);
    ASSERT_EQ_INT(P.f.v4ac08 & (BOX | NEAR_), BOX); ASSERT_EQ_INT(R.prompt, 2); ASSERT_EQ_INT(P.f.v4ac0c, 0);
}

TEST(lock_row_5_range_at_or_above_the_limit_0x2700_long_0x1800_short) {
    lock_setup(0x30, 0x2700, 0); lock(0);
    ASSERT_EQ_INT(P.f.v4ac08 & (BOX | NEAR_), BOX); ASSERT_EQ_INT(R.prompt, 0); ASSERT_EQ_INT(P.f.v4ac0c, 0);
    lock(9); ASSERT_EQ_INT(P.f.v4ac08 & (BOX | NEAR_), NEAR_); ASSERT_EQ_INT(P.f.v4ac0c, 0x100);   /* the periodic lock-lost change */
    lock_setup(0x30, 0x26FF, 0); lock(0); ASSERT_EQ_INT(R.prompt, 1);
    lock_setup(0x20, 0x1800, 0); lock(0); ASSERT_EQ_INT(R.prompt, 0);
    lock_setup(0x20, 0x17FF, 0); lock(0); ASSERT_EQ_INT(R.prompt, 1);
}

TEST(lock_rows_6_and_7_in_range_prompt_state_1_or_2_by_the_closing_delta_limit_minus_675) {
    lock_setup(0x20, 0x100, -675); P.f.v4ac08 = NEAR_; lock(0); ASSERT_EQ_INT(P.f.v4ac08 & NEAR_, 0);   /* rig: rows 6 and 7 clear 0x0200 too */
    ASSERT_EQ_INT(R.prompt, 1); ASSERT_EQ_INT(P.f.v4ac0c, 0x04); ASSERT_EQ_INT(P.f.v4ac08 & BOX, BOX);
    P.f.v4ac0c = 0; lock(0); ASSERT_EQ_INT(P.f.v4ac0c, 0);       /* already 1: no new cue */
    lock(9); ASSERT_EQ_INT(P.f.v4ac0c, 0x04);                    /* the periodic re-cue */
    lock_setup(0x20, 0x100, -676); lock(0);
    ASSERT_EQ_INT(R.prompt, 2); ASSERT_EQ_INT(P.f.v4ac0c, 0x10);
    P.f.v4ac0c = 0; lock(0); ASSERT_EQ_INT(P.f.v4ac0c, 0);
}

TEST(the_marker_shifts_are_4_and_2_and_2_and_1_in_the_lock_box_with_steps_clamped_to_1_or_2) {
    hud_setup(0x20);
    R.mx = 159; R.my = 91;
    radar_in_t i = in(0); i.tgt_x = 100; i.tgt_y = 91;
    radar_slew(&R, &i);   /* gap 59 -> >> 4 = 3 -> clamped to 1, four sub-steps */
    ASSERT_EQ_INT(R.mx, 155);
    P.f.v4ac08 = BOX; R.mx = 159; i.tgt_x = 100;
    radar_slew(&R, &i);   /* in the box: >> 2 = 14 -> clamped to 2: 4 * 2 */
    ASSERT_EQ_INT(R.mx, 151);
    R.mx = 150; R.my = 91; i.tgt_x = 151; i.tgt_y = 91; P.f.v4ac08 = 0;
    radar_slew(&R, &i);   /* the marker is LEFT of the target by 1: floor(-1 >> 2) = -1 moves it right, then the gap is 0 */
    ASSERT_EQ_INT(R.mx, 151);
    R.mx = 152; radar_slew(&R, &i);   /* right of the target by 1: 1 >> 2 = 0: does not move */
    ASSERT_EQ_INT(R.mx, 152);
    ASSERT_EQ_INT(R.tpx, 151);        /* the target point is copied at the end of the slew */
}

TEST(a_slew_whose_box_returns_early_copies_nothing_and_the_last_copied_point_stays) {
    hud_setup(0x20);
    radar_in_t i = in(0); i.tgt_x = 150; i.tgt_y = 70;
    radar_slew(&R, &i);
    ASSERT_EQ_INT(R.tpx, 150); ASSERT_EQ_INT(R.tpy, 70);
    i.tgt_x = -1; i.tgt_y = -1;                                 /* not drawn: the box point is at or below 0, the routine returns at 0x38EFE without the copy (note 07 12.7) */
    radar_slew(&R, &i);
    ASSERT_EQ_INT(R.tpx, 150); ASSERT_EQ_INT(R.tpy, 70);        /* the last copied point, not -1 */
    i.tgt_x = 120; i.tgt_y = 0;                                 /* y at 0: the second early return */
    radar_slew(&R, &i);
    ASSERT_EQ_INT(R.tpx, 150);
    i.tgt_x = 120; i.tgt_y = 66;
    radar_slew(&R, &i);
    ASSERT_EQ_INT(R.tpx, 120); ASSERT_EQ_INT(R.tpy, 66);
}

TEST(the_lock_state_and_the_slew_read_the_selector_of_the_viewed_record_not_record_0) {
    hud_setup(0x20);                                            /* the player holds the missile selection */
    entities_ac(&E, 3)->f.id = 0x10; entities_ac(&E, 3)->f.w0 = 0x1040;   /* the viewed record (V:4A996 = record 3's offset) holds the gun */
    P.f.v4a996 = 3 * 0x200; P.f.v4a994 = 3;
    E.rec[0].range = 0x100;
    R.mx = 159; R.my = 91; R.tpx = 159; R.tpy = 91;
    radar_in_t i = in(0); i.tgt_x = 159; i.tgt_y = 91;
    radar_lock_state(&R, &i);
    ASSERT_EQ_INT(R.hud.marker, 0);                             /* the gun: no marker, no lock state (it would be 2 for the missile) */
    radar_slew(&R, &i);
    ASSERT_EQ_INT(R.mx, 159);
    P.f.v4a996 = 0; P.f.v4a994 = 0;
    radar_lock_state(&R, &i);
    ASSERT_EQ_INT(R.hud.marker, 2);                             /* record 0 viewed again: the missile selection */
}

/* the gun branch of 0x385F4 */
TEST(the_fire_prompt_with_the_gun_needs_the_pipper_within_8_pixels_and_a_range_of_at_most_0x900) {
    hud_setup(0x10);
    E.rec[0].range = 0x900; R.px = 100; R.py = 50;
    R.tpx = 108; R.tpy = 58;   /* the previous frame target point */
    frame(0, 108, 58);
    ASSERT_EQ_INT(R.prompt, 1); ASSERT_EQ_INT(P.f.v4ac0c & 0x04, 0x04); ASSERT_EQ_INT(R.hud.prompt, 1);
}

TEST(the_fire_prompt_stays_without_a_new_cue_and_ends_with_cue_8) {
    hud_setup(0x10);
    E.rec[0].range = 0x900;
    R.prompt = 1; R.px = 100; R.py = 50; R.tpx = 108; R.tpy = 58;
    P.f.v4ac0c = 0; frame(1, 108, 58); ASSERT_EQ_INT(P.f.v4ac0c, 0);
    R.px = 100; R.py = 50; E.rec[0].range = 0x901; frame(2, 108, 58);
    ASSERT_EQ_INT(R.prompt, 0); ASSERT_EQ_INT(P.f.v4ac0c & 0x08, 0x08);
}

TEST(with_the_hud_off_the_sight_0x385F4_does_not_run_no_prompt_state_cue_pipper_history_circle_dot_number_or_prompt) {
    /* Note 07 12.7: the HUD draw 0x37D78 returns before 0x385F4 while V:4A859 is zero; the lock state 0x38884 ran before that test. */
    hud_setup(0x10);
    E.rec[0].range = 0x900; R.px = 100; R.py = 50; R.tpx = 108; R.tpy = 58;
    R.hist[0][0] = 111; R.hist[1][0] = 222;
    radar_in_t i = in(0); i.tgt_x = 108; i.tgt_y = 58; i.hud_off = 1;
    radar_hud_pass(&R, &i);
    ASSERT_EQ_INT(R.prompt, 0); ASSERT_EQ_INT(P.f.v4ac0c, 0);                          /* no state change, no cue 4 */
    ASSERT_EQ_INT(R.hud.prompt, 0); ASSERT_EQ_INT(R.hud.circle, 0); ASSERT_EQ_INT(R.hud.pip_pixel, 0);
    ASSERT_EQ_INT(R.hud.dot, -1); ASSERT_EQ_INT(R.hud.number, 0);
    ASSERT_EQ_INT(R.hist[0][0], 111); ASSERT_EQ_INT(R.hist[1][0], 222);               /* the pipper history does not step */
    ASSERT_EQ_INT(R.px, 100); ASSERT_EQ_INT(R.py, 50);
    R.prompt = 1; P.f.v4ac0c = 0;                                                      /* a stale prompt state of the gun: neither cleared (no cue 8) nor drawn */
    E.rec[0].range = 0x901;
    radar_hud_pass(&R, &i);
    ASSERT_EQ_INT(R.prompt, 1); ASSERT_EQ_INT(P.f.v4ac0c, 0); ASSERT_EQ_INT(R.hud.prompt, 0);
    i.hud_off = 0; radar_hud_pass(&R, &i);                                             /* HUD on again: the sight runs, the stale state ends with the cue 8 */
    ASSERT_EQ_INT(R.prompt, 0); ASSERT_EQ_INT(P.f.v4ac0c & 0x08, 0x08); ASSERT_EQ_INT(R.hud.circle, 1);
    /* a missile: the lock state runs with the HUD off (its prompt is drawn by 0x38884, before the HUD test) while the sight's circle does not */
    lock_setup(0x20, 0x100, -100);
    i = in(0); i.hud_off = 1; i.tgt_x = 159; i.tgt_y = 91;
    radar_hud_pass(&R, &i);
    ASSERT_EQ_INT(R.prompt, 1); ASSERT_EQ_INT(R.hud.prompt, 1); ASSERT_EQ_INT(R.hud.circle, 0);
}

TEST(with_the_gun_the_circle_and_the_pipper_are_drawn_and_with_a_target_the_dot_and_the_number) {
    hud_setup(0x10);
    E.rec[0].range = 18000; R.px = 100; R.py = 50;
    frame(0, 120, 70);
    ASSERT_EQ_INT(R.hud.circle, 1); ASSERT_EQ_INT(R.hud.circle_x, 100);
    ASSERT_EQ_INT(R.hud.dot, 18000 * 76 / 36000); ASSERT_EQ_INT(R.hud.number, 1);
    E.designated = -1; frame(1, -1, -1);
    ASSERT_EQ_INT(R.hud.circle, 1); ASSERT_EQ_INT(R.hud.dot, -1); ASSERT_EQ_INT(R.hud.number, 0);
    E.designated = 4; E.rec[0].range = 30000; framep(2, 120, 70); ASSERT_EQ_INT(R.hud.dot, 63);
}

TEST(with_a_missile_the_circle_needs_a_target_and_is_centred_on_159_91_while_the_target_point_is_off) {
    hud_setup(0x20);
    E.rec[0].range = 100; R.px = 10; R.py = 20;
    frame(0, -1, -1);   /* the target point is not positive */
    ASSERT_EQ_INT(R.hud.circle, 1); ASSERT_EQ_INT(R.hud.circle_x, 159); ASSERT_EQ_INT(R.hud.circle_y, 91);
    E.designated = -1; frame(1, -1, -1); ASSERT_EQ_INT(R.hud.circle, 0);
    hud_setup(0x20); { radar_in_t i = in(0); i.zoom = 0x40; radar_hud_pass(&R, &i); ASSERT_EQ_INT(R.hud.circle, 0); }   /* only at the zoom factor 0x80 */
}

TEST(the_pipper_is_the_projection_of_the_aim_point_two_calls_old) {
    hud_setup(0x10);
    P.pos[0] = 1000; P.pos[1] = 5000; P.pos[2] = 7000;
    E.designated = -1;
    for (int k = 0; k < 3; k++) frame((uint32_t)k, -1, -1);
    /* the aim point: position + (0x600 * second column + 0x4000 * third column of the matrix) >> 6 */
    ASSERT_EQ_INT(R.hist[0][1], 5000 + ((0x600 * 16384) >> 6));
    ASSERT_EQ_INT(R.hist[0][2], 7000 + ((0x4000 * 16384) >> 6));
}

TEST(a_stopped_aircraft_sees_the_nose_point_and_a_move_shows_two_calls_later) {
    hud_setup(0x10);
    P.pos[0] = 1000; P.pos[1] = 5000; P.pos[2] = 7000;
    E.designated = -1;
    for (int k = 0; k < 3; k++) frame((uint32_t)k, -1, -1);
    fx_mat_t id; memset(&id, 0, sizeof id); id.m[0][0] = id.m[1][1] = id.m[2][2] = 0x100;
    fx_vec_t v, c; int16_t sx, sy;
    v.x = (R.hist[1][0] - P.pos[0]) >> 8; v.y = (R.hist[1][1] - P.pos[1]) >> 8; v.z = (R.hist[1][2] - P.pos[2]) >> 8;
    fx_rotate(&c, &id, &v); fx_project(&sx, &sy, &c);
    ASSERT_EQ_INT(R.px, sx); ASSERT_EQ_INT(R.py, sy);
    int32_t newest = R.hist[0][1];
    P.pos[1] += 100000;
    frame(3, -1, -1);
    ASSERT_EQ_INT(R.hist[1][1], newest);                        /* the history shifted once */
    ASSERT_EQ_INT(R.hist[0][1], newest + 100000);
}

/* the range number */
TEST(the_range_number_is_a_sign_and_four_digits_with_a_space_for_closing) {
    hud_setup(0x10);
    E.rec[0].range = 990; R.last_range = 1000; P.f.b4 = EB4_RANGE_DONE;
    framep(0, 150, 70);
    ASSERT_STR_EQ(R.hud.number_text, " 0010"); ASSERT_EQ_INT(R.closing, -10); ASSERT_EQ_INT(R.last_range, 990);
    ASSERT_EQ_INT(P.f.b4 & EB4_RANGE_DONE, 0);
    framep(1, 150, 70); ASSERT_STR_EQ(R.hud.number_text, " 0010");   /* no update: the stored argument */
    P.f.b4 = EB4_RANGE_DONE; framep(2, 150, 70); ASSERT_STR_EQ(R.hud.number_text, "-0000");   /* a steady range */
    E.rec[0].range = 990 + 12345; P.f.b4 = EB4_RANGE_DONE; framep(3, 150, 70);
    ASSERT_STR_EQ(R.hud.number_text, "-2345");   /* the magnitude modulo 10,000 */
    E.rec[0].range = 0x7F01; framep(4, 150, 70); ASSERT_EQ_INT(R.hud.number, 0);   /* only for a range of at most 0x7F00 */
}

TEST(a_paused_pass_does_not_consume_the_range_update) {
    hud_setup(0x10);
    E.rec[0].range = 500; R.last_range = 400; P.f.b4 = EB4_RANGE_DONE; R.closing = 7;
    radar_in_t i = in(0); i.paused = 1; i.tgt_x = 150; i.tgt_y = 70; radar_hud_pass(&R, &i);
    ASSERT_EQ_INT(P.f.b4 & EB4_RANGE_DONE, EB4_RANGE_DONE);
}

/* 12.8 the message line */
TEST(the_target_data_pages_altitude_heading_speed_with_the_notes_formulas) {
    hud_setup(0x10);
    aircraft_t *t = entities_ac(&E, 4);
    t->pos[1] = 1024 * 2500; t->f.heading = 14400; t->f.speed = -1200; t->f.b20 = 0x40;
    E.designated = -1; radar_key_target(&R); walk_once(0);
    ASSERT_EQ_INT(R.page, 1);
    frame(0, -1, -1);
    ASSERT_EQ_INT(R.line.mode, 2); ASSERT_EQ_INT(R.line.page, 1); ASSERT_STR_EQ(R.line.digits, "12500"); ASSERT_EQ_INT(R.line.col, 18);
    ASSERT_EQ_INT(R.line.cls, 0x12); ASSERT_EQ_INT(R.line.colour, 2); ASSERT_EQ_INT(R.line.detected, 1);
    radar_key_page(&R); ASSERT_EQ_INT(R.page, 0x8002); ASSERT_EQ_INT(R.countdown, 5); ASSERT_EQ_INT(R.line_redraw, 2);
    frame(1, -1, -1);   /* the rebuild bit is consumed by the next call (rig): the new page is built at once, the counter stays */
    ASSERT_EQ_INT(R.line.page, 2); ASSERT_STR_EQ(R.line.digits, "180"); ASSERT_EQ_INT(R.line.col, 19); ASSERT_EQ_INT(R.page, 2); ASSERT_EQ_INT(R.line_redraw, 2);
    radar_key_page(&R); frame(2, -1, -1);
    ASSERT_EQ_INT(R.line.page, 3); ASSERT_STR_EQ(R.line.digits, " 100"); ASSERT_EQ_INT(R.line.col, 18);
    t->f.w0 |= EW0_HIT; radar_key_page(&R); R.page = 0x8003; frame(3, -1, -1);
    ASSERT_STR_EQ(R.line.digits, "   0");   /* a hit target reads 0 */
}

TEST(the_y_page_word_wraps_above_3_to_1_and_climbs_while_the_rebuild_bit_is_pending) {
    setup();
    R.page = 3; radar_key_page(&R); ASSERT_EQ_INT(R.page, 0x8001);
    radar_key_page(&R); ASSERT_EQ_INT(R.page, 0x8002);   /* bit 15 still set: a signed word, no wrap */
    radar_key_page(&R); radar_key_page(&R); radar_key_page(&R); ASSERT_EQ_INT(R.page, 0x8005);
}

TEST(the_pages_rotate_by_themselves_through_the_clock_countdown) {
    hud_setup(0x10);
    E.designated = -1; radar_key_target(&R); walk_once(0);
    frame(0, -1, -1); ASSERT_EQ_INT(R.line.page, 1);
    frame(1, -1, -1); frame(2, -1, -1);   /* the redraw counter */
    radar_clock_tick(&R); ASSERT_EQ_INT(R.countdown, 0);
    radar_clock_tick(&R); ASSERT_EQ_INT(R.countdown, -1);
    radar_clock_tick(&R); ASSERT_EQ_INT(R.countdown, -1);
    frame(3, -1, -1); ASSERT_EQ_INT(R.line.page, 2); ASSERT_EQ_INT(R.countdown, 1);
}

TEST(an_undetected_target_has_colour_0_and_the_no_signal_name) {
    hud_setup(0x10);
    entities_ac(&E, 4)->f.b20 = 0;
    R.page = 1; R.countdown = -1; R.holdoff = -1; E.designated = 4;
    frame(0, -1, -1);
    ASSERT_EQ_INT(R.line.detected, 0); ASSERT_EQ_INT(R.line.colour, 0);
}

TEST(the_holdoff_delays_the_target_data_24_passes_and_a_latched_message_owns_the_line) {
    hud_setup(0x10);
    R.page = 1; R.countdown = 1; R.holdoff = 0x18;
    int k = 0;
    for (; k < 40; k++) { frame((uint32_t)k, -1, -1); if (R.line.mode == 2) break; }
    ASSERT_EQ_INT(k, 24);
    hud_setup(0x10); R.page = 1; R.countdown = 1; R.holdoff = -1; P.f.v4a984 = 1;
    frame(0, -1, -1); ASSERT_EQ_INT(R.line.mode, 1); ASSERT_EQ_INT(R.page, 0xFFFF);   /* the branch of the posted message: a positive page word is consumed (rig) */
}

TEST(without_a_target_the_page_word_is_consumed_and_the_blank_message_is_drawn) {
    setup();
    R.page = 2; frame(0, -1, -1);
    ASSERT_EQ_INT(R.page, 0xFFFF); ASSERT_EQ_INT(R.line.mode, 1);
    frame(1, -1, -1); ASSERT_EQ_INT(R.page, 0); ASSERT_EQ_INT(R.line.mode, 1);
    frame(2, -1, -1); ASSERT_EQ_INT(R.line.mode, 0);
}

/* the keys */
TEST(the_r_key_cycles_the_range_nibble_9_d_b_9_and_keeps_the_weapon_nibble) {
    setup();
    P.f.id = 0x29; radar_key_range(&E); ASSERT_EQ_INT(P.f.id, 0x2D);
    radar_key_range(&E); ASSERT_EQ_INT(P.f.id, 0x2B);
    radar_key_range(&E); ASSERT_EQ_INT(P.f.id, 0x29);
    /* note 07 12.3 (V: the rig, 0x1DEF4, and 39 emulator presses): 9 -> 0xD, 0xB -> 9, anything else (0xD included) -> 0xB */
    for (int n = 0; n < 16; n++) {
        P.f.id = (uint8_t)(0x20 | n); radar_key_range(&E);
        ASSERT_EQ_INT(P.f.id, 0x20 | (n == 9 ? 0x0D : n == 0x0B ? 0x09 : 0x0B));
    }
}

/* note 07 12.3: the key changes the VIEWED record's +0x63 (the record at V:4A996), the player's own byte stays when the seat is viewed */
TEST(the_r_key_changes_the_viewed_records_byte_not_the_players) {
    setup();
    P.f.id = 0x29;
    aircraft_t *seat = entities_ac(&E, 1);
    seat->f.id = 0x19;
    P.f.v4a994 = 1; P.f.v4a996 = 0x200;
    radar_key_range(&E);
    ASSERT_EQ_INT(seat->f.id, 0x1D);
    ASSERT_EQ_INT(P.f.id, 0x29);
    P.f.v4a994 = 0; P.f.v4a996 = 0;
    radar_key_range(&E);
    ASSERT_EQ_INT(P.f.id, 0x2D);
}

TEST(the_scene_block_is_filled_from_the_state) {
    hud_setup(0x20);
    E.designated = -1; radar_key_target(&R); walk_once(0);
    game_hud_t h; memset(&h, 0, sizeof h);
    E.rec[0].range = 1234;
    radar_fill_hud(&R, &E, &h);
    ASSERT_EQ_INT(h.cp_designated, 1); ASSERT_EQ_INT(h.cp_target_rec, 4); ASSERT_EQ_INT(h.cp_range, 1234); ASSERT_EQ_INT(h.cp_target_cls, 0x12);
    ASSERT_EQ_INT(h.cp_threat, R.threat_prev); ASSERT_EQ_INT(h.blip_count, 1); ASSERT_EQ_INT(h.radar_count, 1); ASSERT_EQ_INT(h.radar[0].flags, 2);
}

int main(void) {
    RUN(the_list_holds_the_valid_records_but_the_viewed_one_in_index_order_with_the_flag_word);
    RUN(a_list_value_is_the_relative_position_over_256_with_x_lateral_and_z_ahead);
    RUN(a_distant_record_is_shifted_by_an_even_count_so_that_the_word_precision_holds);
    RUN(the_rotation_words_come_from_the_viewed_records_matrix_or_the_caller);
    RUN(the_gate_builds_on_n_mod_4_zero_and_walks_on_n_mod_4_zero_and_one_only);
    RUN(the_scope_draw_walks_twice_on_a_redraw_pass_and_once_otherwise_with_the_pause_byte_no_gate);
    RUN(the_erase_list_of_a_double_walk_is_the_first_walks_and_the_second_walk_redraws_over_it);
    RUN(an_external_view_without_the_panel_counter_leaves_the_list_and_the_flags_alone_and_with_it_walks);
    RUN(the_window_is_27_either_side_16_ahead_and_21_behind_in_pixels_of_the_nibble_13);
    RUN(the_scope_range_nibbles_13_11_and_9_scale_the_blip_by_4_and_16);
    RUN(the_detection_box_is_0x10000_list_units_and_sets_or_clears_b20_bit_6);
    RUN(the_designated_blip_blinks_with_bit_0_of_the_radar_frame_counter_and_a_page_holds_ten_blips);
    RUN(a_page_of_ten_blips_ends_its_list_on_the_first_word_of_the_other_page_and_page_1_on_the_word_past_both);
    RUN(a_wide_blip_is_stored_with_bit_15_and_a_short_page_ends_with_its_own_terminator_word);
    RUN(the_pens_follow_the_class_the_detected_bit_the_out_of_control_bit_and_the_counted_bit);
    RUN(a_blip_is_two_pixels_wide_when_the_height_is_at_least_minus_the_camera_offset);
    RUN(a_record_on_the_ground_or_invalid_is_not_plotted_and_loses_the_radar_bit);
    RUN(the_mission_1_counter_counts_31_aircraft_contacts_with_a_target_designated);
    RUN(modes_2_and_0x7d_skip_the_detection_and_the_classification);
    RUN(threat_bits_per_class_and_flag);
    RUN(a_hostile_missile_aimed_at_the_flown_aircraft_sets_bit_4_or_5_and_a_players_missile_none);
    RUN(wrecks_out_of_control_records_and_class_nibble_3_raise_no_threat);
    RUN(the_cue_rows_of_12_4);
    RUN(the_cue_rows_of_12_4_hostile_cruise_friendly_and_the_hold_off);
    RUN(the_friendly_row_applies_when_the_hostile_bit_was_already_set);
    RUN(the_threat_bits_are_remembered_for_the_next_walk_even_when_empty);
    RUN(the_t_key_cycles_none_first_candidate_to_last_and_none_again);
    RUN(designation_sets_the_page_the_countdown_the_holdoff_and_the_list_position);
    RUN(candidates_need_bits_4_and_5_and_an_aircraft_class_and_no_out_of_control);
    RUN(a_target_missing_from_the_list_restarts_at_the_top_and_the_request_waits_for_a_walk);
    RUN(the_range_is_recomputed_every_pass_below_0x480_every_third_to_0x900_and_every_sixth_beyond);
    RUN(the_range_keeps_the_low_nibble_of_the_counter_byte_and_needs_a_designated_target);
    RUN(a_component_above_0x7f00_gives_0x7fff_and_leaves_b4_bit_0_alone);
    RUN(the_marker_is_invalid_with_no_weapon_or_no_target_point_and_starts_at_159_91);
    RUN(lock_row_1_no_target_gun_or_invalid_marker_clears_the_bits_and_cues_once);
    RUN(lock_row_2_marker_outside_the_box_x_97_221_y_47_133_clears_the_prompt);
    RUN(lock_row_3_marker_more_than_5_pixels_from_the_target_point_loses_the_lock);
    RUN(lock_row_4_in_the_box_with_another_selection_sets_the_box_bit_only);
    RUN(lock_row_5_range_at_or_above_the_limit_0x2700_long_0x1800_short);
    RUN(lock_rows_6_and_7_in_range_prompt_state_1_or_2_by_the_closing_delta_limit_minus_675);
    RUN(the_marker_shifts_are_4_and_2_and_2_and_1_in_the_lock_box_with_steps_clamped_to_1_or_2);
    RUN(the_slew_returns_after_one_step_when_the_box_point_is_not_positive_and_the_script_count_freezes_the_marker);
    RUN(a_slew_whose_box_returns_early_copies_nothing_and_the_last_copied_point_stays);
    RUN(the_lock_state_and_the_slew_read_the_selector_of_the_viewed_record_not_record_0);
    RUN(the_fire_prompt_with_the_gun_needs_the_pipper_within_8_pixels_and_a_range_of_at_most_0x900);
    RUN(the_fire_prompt_stays_without_a_new_cue_and_ends_with_cue_8);
    RUN(with_the_hud_off_the_sight_0x385F4_does_not_run_no_prompt_state_cue_pipper_history_circle_dot_number_or_prompt);
    RUN(with_the_gun_the_circle_and_the_pipper_are_drawn_and_with_a_target_the_dot_and_the_number);
    RUN(with_a_missile_the_circle_needs_a_target_and_is_centred_on_159_91_while_the_target_point_is_off);
    RUN(the_pipper_is_the_projection_of_the_aim_point_two_calls_old);
    RUN(a_stopped_aircraft_sees_the_nose_point_and_a_move_shows_two_calls_later);
    RUN(the_range_number_is_a_sign_and_four_digits_with_a_space_for_closing);
    RUN(a_paused_pass_does_not_consume_the_range_update);
    RUN(the_target_data_pages_altitude_heading_speed_with_the_notes_formulas);
    RUN(the_y_page_word_wraps_above_3_to_1_and_climbs_while_the_rebuild_bit_is_pending);
    RUN(the_pages_rotate_by_themselves_through_the_clock_countdown);
    RUN(an_undetected_target_has_colour_0_and_the_no_signal_name);
    RUN(the_holdoff_delays_the_target_data_24_passes_and_a_latched_message_owns_the_line);
    RUN(without_a_target_the_page_word_is_consumed_and_the_blank_message_is_drawn);
    RUN(the_r_key_cycles_the_range_nibble_9_d_b_9_and_keeps_the_weapon_nibble);
    RUN(the_r_key_changes_the_viewed_records_byte_not_the_players);
    RUN(the_scene_block_is_filled_from_the_state);
    TEST_MAIN_END();
}
