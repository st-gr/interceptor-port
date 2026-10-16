/* tests/test_mapmarks.c - the map view's entity marks (src/core/mapmarks.c; docs/re/06-cockpit-hud.md "Map renderer" and "What V:4A90F and V:4A86D hold, and the paths with them clear").
 * Invented records only. The camera hangs over (0, 0) at the altitude 0x03000000: the depth is Z = 768 * 128 / 256 = 384, a record at the camera point projects to the column 159 and the row 90
 * (the call row 89 plus one); a record 100 high-word units east and north projects to X = (168 * 100) >> 8 = 65, c = 160 + 160 * 65 / 384 = 187 (column 319 - 187 = 132) and Y = (252 * 100) >> 8 = 98,
 * r = 90 + 90 * 98 / 384 = 112 (row 179 - 112 + 1 = 68). The routine keeps two bytes per record (+0x70: the x and y nibbles of the projected point, +0x71: the sector, 0xFF unplaced). */
#include "test.h"
#include "../src/core/mapmarks.h"

static mapmarks_t M;
static mapmarks_in_t I;

#define UNIT 0x10000   /* one high-word unit of a position long */

static void setup(void) {
    mapmarks_init(&M);
    memset(&I, 0, sizeof I);
    I.cam_y = 0x03000000;
    I.last_row = 179;
    I.v4a86d = 1; I.v4a90f = 1;
}

/* record i: valid (bit 0x40), at (dx, dz) high-word units from the camera, the class and the heading */
static void put(int i, uint16_t w0, uint8_t cls, int dx, int dz, int heading) {
    mapmarks_rec_t *r = &I.rec[i];
    memset(r, 0, sizeof *r);
    r->w0 = (uint16_t)(0x0040 | w0); r->cls = cls; r->x = dx * UNIT; r->z = dz * UNIT; r->heading = (int16_t)heading;
}

static void pass(uint16_t n) { I.n = n; mapmarks_pass(&M, &I); }

TEST(a_record_at_the_camera_point_projects_to_column_159_row_90_and_the_east_north_one_to_132_68) {
    setup();
    put(0, 0, 0x11, 0, 0, 0); put(1, 0, 0x12, 100, 100, 0);
    pass(1);
    ASSERT_EQ_INT(M.count, 2);
    ASSERT_TRUE(M.mark[0].rec == 0 && M.mark[0].x == 159 && M.mark[0].y == 90);
    ASSERT_TRUE(M.mark[1].rec == 1 && M.mark[1].x == 132 && M.mark[1].y == 68);
    put(2, 0, 0x12, -100, -100, 0);   /* west and south: X = (168 * -100) >> 8 = -66, c = 160 + trunc(-10560 / 384) = 160 - 27 = 133 -> column 186; Y = -99, r = 90 - 23 = 67 -> row 113 */
    pass(1);
    ASSERT_TRUE(M.mark[2].x == 186 && M.mark[2].y == 113);
}

TEST(only_records_with_bit_0x40_are_drawn_and_a_friend_on_the_ground_is_hidden) {
    setup();
    put(3, 0, 0x12, 0, 0, 0); I.rec[3].w0 &= (uint16_t)~0x0040;           /* bit 0x40 clear */
    put(4, 0x0008, 0x12, 0, 0, 0); I.rec[4].b03 = 0x80;                   /* bit 0x0008 with bit 7 of byte +3: hidden */
    put(5, 0x0008, 0x12, 0, 0, 0); I.rec[5].b03 = 0x7F;                   /* not hidden without bit 7 */
    put(6, 0x0000, 0x12, 0, 0, 0); I.rec[6].b03 = 0x80;                   /* bit 7 alone does not hide */
    pass(1);
    ASSERT_EQ_INT(M.count, 2);
    ASSERT_TRUE(M.mark[0].rec == 5 && M.mark[1].rec == 6);
}

TEST(a_point_outside_the_frustum_or_below_the_window_is_rejected_and_stores_nothing) {
    setup();
    put(1, 0, 0x12, 400, 0, 0);   /* X = (168 * 400) >> 8 = 262 < 384: inside */
    put(2, 0, 0x12, 600, 0, 0);   /* X = 393 >= 384: outside */
    put(3, 0, 0x12, -600, 0, 0);  /* -X >= Z */
    put(4, 0, 0x12, 0, 400, 0);   /* Y = (252 * 400) >> 8 = 393 >= 384: outside */
    put(5, 0, 0x12, 0, -400, 0);
    pass(1);
    ASSERT_EQ_INT(M.count, 1);
    ASSERT_EQ_INT(M.mark[0].rec, 1);
    ASSERT_EQ_INT(M.b71[2], 0xFF); ASSERT_EQ_INT(M.b71[4], 0xFF);
    setup();
    put(1, 0, 0x12, 0, -90, 0);   /* Y = -89, r = 90 - 20 = 70 -> row 110 */
    I.last_row = 109;
    pass(1);
    ASSERT_EQ_INT(M.count, 0);    /* the projector rejects a row beyond V:4AA3C */
    I.last_row = 110;
    pass(1);
    ASSERT_EQ_INT(M.count, 1);
    ASSERT_EQ_INT(M.mark[0].y, 110);
}

TEST(the_shape_is_the_heading_sector_floor_of_the_heading_plus_900_over_1800_modulo_16) {
    static const int hd[][2] = { {0, 0}, {899, 0}, {900, 1}, {1799, 1}, {2699, 1}, {2700, 2}, {14400, 8}, {26999, 15}, {27000, 15}, {28799, 0}, {27899, 15}, {27900, 0}, {-100, 0}, {-32768, 0} };
    for (unsigned k = 0; k < sizeof hd / sizeof hd[0]; k++) {
        setup();
        put(1, 0, 0x12, 0, 0, hd[k][0]);
        pass(1);
        ASSERT_EQ_INT(M.count, 1);
        ASSERT_EQ_INT(M.mark[0].shape, hd[k][1]);
        ASSERT_EQ_INT(M.b71[1], hd[k][1]);
    }
}

TEST(the_direct_path_draws_the_true_position_on_odd_frames_only_and_stores_the_sector_and_nibbles_on_every_frame) {   /* V:4A86D set, V:4A90F = 1: the flight value */
    setup();
    put(1, 0, 0x12, 100, 100, 1800);
    pass(2);
    ASSERT_EQ_INT(M.count, 0);                                           /* even n: bit 0 of V:4A993 clear */
    ASSERT_EQ_INT(M.b71[1], 1); ASSERT_EQ_INT(M.b70[1], (132 & 15) << 4 | (68 & 15));
    I.rec[1].heading = 6000; I.rec[1].x = 101 * UNIT;
    pass(3);
    ASSERT_EQ_INT(M.count, 1);
    ASSERT_EQ_INT(M.mark[0].shape, 3);                                   /* the sector 1 + (6000 - 900) / 1800 = 3, refreshed this frame */
    ASSERT_EQ_INT(M.b71[1], 3);
    ASSERT_EQ_INT(M.mark[0].x, 319 - (160 + (160 * ((168 * 101) >> 8)) / 384));   /* the true position, not the stored one */
}

TEST(with_v4a86d_clear_and_v4a90f_negative_nothing_is_drawn_not_even_the_ship) {   /* the demo set-up state */
    setup();
    I.v4a86d = 0; I.v4a90f = -1;
    put(0, 0, 0x11, 0, 0, 0); put(1, 0, 0x12, 10, 10, 0); put(2, 0x0008, 0x20, 20, 20, 0);
    for (uint16_t n = 0; n < 4; n++) { pass(n); ASSERT_EQ_INT(M.count, 0); }
    ASSERT_EQ_INT(M.b71[1], 0xFF);                                       /* and nothing is stored */
    I.v4a86d = 1;                                                        /* the direct path does not look at V:4A90F's sign for an entity ... */
    pass(1);
    ASSERT_EQ_INT(M.count, 2);                                           /* records 0 and 1 */
    ASSERT_TRUE(M.mark[0].rec == 0 && M.mark[1].rec == 1);
    ASSERT_EQ_INT(M.mark[1].shape, 0);                                   /* ... and the ship routine refuses a negative V:4A90F whatever V:4A86D is */
}

TEST(the_ship_is_drawn_in_pen_3_on_every_frame_with_no_store_unless_v4a90f_is_negative) {
    for (int d = 0; d < 2; d++) {
        setup();
        I.v4a86d = (uint8_t)d; I.v4a90f = d ? 1 : 0;
        put(2, 0x0008, 0x20, 100, 100, 5000);
        for (uint16_t n = 0; n < 4; n++) {
            pass(n);
            ASSERT_EQ_INT(M.count, 1);
            ASSERT_TRUE(M.mark[0].rec == 2 && M.mark[0].shape == MAPMARKS_SHIP && M.mark[0].pen == 3 && M.mark[0].x == 132 && M.mark[0].y == 68);
            ASSERT_EQ_INT(M.b71[2], 0xFF);
        }
    }
    setup();
    I.v4a86d = 1; I.v4a90f = -1;
    put(2, 0, 0x2F, 0, 0, 0);
    pass(1);
    ASSERT_EQ_INT(M.count, 0);
    put(0, 0, 0x20, 0, 0, 0);          /* the player is never the ship: record 0 goes to the generic path whatever its class */
    I.v4a90f = 1;
    pass(1);
    ASSERT_TRUE(M.count == 2 && M.mark[0].rec == 0 && M.mark[0].shape != MAPMARKS_SHIP && M.mark[0].pen == 13 && M.mark[1].rec == 2 && M.mark[1].shape == MAPMARKS_SHIP);
}

TEST(with_v4a86d_clear_and_v4a90f_zero_the_stored_mark_is_drawn_on_every_frame_and_the_store_refreshes_only_while_the_sector_is_unset) {
    setup();
    I.v4a86d = 0; I.v4a90f = 0;
    put(1, 0, 0x12, 100, 100, 1800);                                     /* sector 1 */
    pass(1);
    ASSERT_EQ_INT(M.count, 0);                                           /* +0x71 is 0xFF: the first frame takes the direct path, which stores and draws nothing with V:4A86D clear */
    ASSERT_EQ_INT(M.b71[1], 1); ASSERT_EQ_INT(M.b70[1], 0x44);          /* column 132 and row 68: nibbles 4 and 4 */
    I.rec[1].heading = 9000;                                             /* the heading changes: the stored sector stays */
    pass(2);
    ASSERT_EQ_INT(M.count, 1);
    ASSERT_TRUE(M.mark[0].shape == 1 && M.mark[0].x == 132 && M.mark[0].y == 68 && M.mark[0].pen == 1);
    pass(3);
    ASSERT_EQ_INT(M.count, 1);                                           /* odd or even: every frame */
    ASSERT_EQ_INT(M.b71[1], 1);
    /* a drift within -8 .. 7 pixels is pulled back to the stored place; beyond it the wrapped difference puts the mark a full 16 pixels away */
    I.rec[1].x = 106 * UNIT;                                               /* X = (168 * 106) >> 8 = 69, c = 160 + 160 * 69 / 384 = 188 -> column 131: a drift of one pixel */
    pass(4);
    ASSERT_EQ_INT(M.mark[0].x, 132);
    I.rec[1].x = 120 * UNIT;                                              /* X = 78, c = 160 + 32 = 192 -> column 127: a drift of 5 */
    pass(5);
    ASSERT_EQ_INT(M.mark[0].x, 132);
    I.rec[1].x = 140 * UNIT;                                              /* X = 91, c = 160 + 37 = 197 -> column 122: a drift of 10; nibble 10 against the stored 4, the difference 6: drawn at 122 - 6 = 116 */
    pass(6);
    ASSERT_EQ_INT(M.mark[0].x, 116);                                      /* beyond the drift of 8 the mark has jumped by 16 pixels */
    I.rec[1].x = 160 * UNIT;                                              /* X = 105, c = 160 + 43 = 203 -> column 116: the difference 0 */
    pass(7);
    ASSERT_EQ_INT(M.mark[0].x, 116);
    I.rec[1].x = 150 * UNIT;                                              /* X = 98, c = 160 + 40 = 200 -> column 119: nibble 7, the difference 3: drawn at 119 - 3 = 116 */
    pass(8);
    ASSERT_EQ_INT(M.mark[0].x, 116);
    /* the stored sector is read from the byte; making it negative again takes the direct path once more (a new placement) */
    M.b71[1] = 0xFF;
    pass(9);
    ASSERT_EQ_INT(M.count, 0);
    ASSERT_EQ_INT(M.b71[1], 3 + 2);                                       /* the sector of the heading 9000: 1 + (9000 - 900) / 1800 = 5 */
}

TEST(the_pull_back_difference_is_wrapped_into_minus_8_to_plus_8) {
    setup();
    I.v4a86d = 0; I.v4a90f = 0;
    put(1, 0, 0x12, 0, 0, 0);                                            /* column 159 (nibble 15), row 90 (nibble 10) */
    M.b71[1] = 7;
    /* the first row of the table is the check of equal nibbles; the others set b70 by hand */
    M.b70[1] = (uint8_t)((15 << 4) | 10); pass(0); ASSERT_TRUE(M.mark[0].x == 159 && M.mark[0].y == 90);
    M.b70[1] = (uint8_t)((12 << 4) | 10); pass(0); ASSERT_EQ_INT(M.mark[0].x, 156);
    M.b70[1] = (uint8_t)((7 << 4) | 10);  pass(0); ASSERT_EQ_INT(M.mark[0].x, 167);   /* 15 - 7 = 8 wraps to -8 */
    M.b70[1] = (uint8_t)((8 << 4) | 10);  pass(0); ASSERT_EQ_INT(M.mark[0].x, 152);   /* 7 */
    M.b70[1] = (uint8_t)((0 << 4) | 10);  pass(0); ASSERT_EQ_INT(M.mark[0].x, 160);   /* 15 wraps to -1 */
    M.b70[1] = (uint8_t)((15 << 4) | 15); pass(0); ASSERT_EQ_INT(M.mark[0].y, 95);    /* 10 - 15 = -5 stays: drawn 5 lower */
    M.b70[1] = (uint8_t)((15 << 4) | 2);  pass(0); ASSERT_EQ_INT(M.mark[0].y, 98);    /* 10 - 2 = 8 wraps to -8: drawn 8 lower */
    M.b70[1] = (uint8_t)((15 << 4) | 3);  pass(0); ASSERT_EQ_INT(M.mark[0].y, 83);    /* 7 */
    M.b70[1] = (uint8_t)((15 << 4) | 11); pass(0); ASSERT_EQ_INT(M.mark[0].y, 91);    /* -1 */
    /* the other end: a difference of -8 becomes +8 (0x300F4 to 0x3010E): the record at (100, 100) projects to the column 132 (nibble 4) and the row 68 (nibble 4) */
    put(1, 0, 0x12, 100, 100, 0); M.b71[1] = 7;
    M.b70[1] = (uint8_t)((12 << 4) | 4);  pass(0); ASSERT_EQ_INT(M.mark[0].x, 124);   /* 4 - 12 = -8 wraps to +8: the mark is pulled back 8 columns */
    M.b70[1] = (uint8_t)((4 << 4) | 12);  pass(0); ASSERT_EQ_INT(M.mark[0].y, 60);    /* the same on the rows: 68 - 8 */
}

TEST(with_a_positive_v4a90f_the_frames_with_n_and_m_zero_refresh_the_store_and_draw_nothing_the_others_draw_the_stored_mark) {
    setup();
    I.v4a86d = 0; I.v4a90f = 2;                                           /* the mask 2: n = 0, 1, 4, 5 refresh; n = 2, 3, 6, 7 draw */
    put(1, 0, 0x12, 100, 100, 1800);
    M.b71[1] = 9; M.b70[1] = 0x00;                                        /* a stale stored mark: sector 9 */
    pass(0);
    ASSERT_EQ_INT(M.count, 0);
    ASSERT_TRUE(M.b71[1] == 1 && M.b70[1] == 0x44);                      /* refreshed: sector 1 of the heading 1800, nibbles of (132, 68) */
    I.rec[1].heading = 12000;                                             /* sector 7 */
    pass(1);
    ASSERT_EQ_INT(M.count, 0); ASSERT_EQ_INT(M.b71[1], 7);               /* refreshed again */
    I.rec[1].heading = 0;
    pass(2);
    ASSERT_EQ_INT(M.count, 1); ASSERT_TRUE(M.mark[0].shape == 7 && M.mark[0].x == 132 && M.mark[0].y == 68);   /* the stored mark: nothing refreshed */
    pass(3);
    ASSERT_EQ_INT(M.count, 1); ASSERT_EQ_INT(M.mark[0].shape, 7);
    pass(4);
    ASSERT_EQ_INT(M.count, 0); ASSERT_EQ_INT(M.b71[1], 0);               /* n & 2 = 0 again: refreshed with the heading 0 */
    /* the word n is tested whole against the sign-extended mask: a mask byte of 0x41 tests bits 0 and 6 */
    I.v4a90f = 0x41;
    pass(0x40);                                                           /* n & 0x41 = 0x40: the stored mark is drawn */
    ASSERT_EQ_INT(M.count, 1);
    pass(0x100);                                                          /* n & 0x41 = 0: refresh */
    ASSERT_EQ_INT(M.count, 0);
}

TEST(the_fixed_camera_in_flight_is_v4a86d_clear_with_v4a90f_1_the_stored_mark_on_odd_frames_only) {
    setup();
    I.v4a86d = 0; I.v4a90f = 1;
    put(1, 0, 0x12, 100, 100, 1800);
    pass(0); ASSERT_EQ_INT(M.count, 0);                                   /* even: refresh */
    pass(1); ASSERT_EQ_INT(M.count, 1);
    ASSERT_TRUE(M.mark[0].shape == 1 && M.mark[0].x == 132 && M.mark[0].y == 68);
    pass(2); ASSERT_EQ_INT(M.count, 0);
    pass(3); ASSERT_EQ_INT(M.count, 1);
}

TEST(the_pens_the_player_13_a_friend_13_the_rest_1_and_in_mode_3_the_records_without_bit_6_of_byte_0x20_pen_3) {
    setup();
    put(0, 0, 0x11, 0, 0, 0);
    put(1, 0x0008, 0x12, 10, 10, 0);                                      /* bit 0x0008: friendly */
    put(2, 0x0000, 0x12, 20, 20, 0);
    pass(1);
    ASSERT_TRUE(M.mark[0].pen == 13 && M.mark[1].pen == 13 && M.mark[2].pen == 1);
    I.mode = 3;
    pass(1);
    ASSERT_EQ_INT(M.mark[0].pen, 13);                                     /* the loop sends entity 0 straight to pen 13: the mode 3 test comes after it */
    ASSERT_EQ_INT(M.mark[1].pen, 3); ASSERT_EQ_INT(M.mark[2].pen, 3);     /* bit 6 of byte +0x20 clear */
    I.rec[1].b20 = 0x40; I.rec[2].b20 = 0x40;
    pass(1);
    ASSERT_EQ_INT(M.mark[1].pen, 13); ASSERT_EQ_INT(M.mark[2].pen, 1);    /* detected: the ordinary pens */
    I.mode = 2;
    I.rec[1].b20 = 0; I.rec[2].b20 = 0;
    pass(1);
    ASSERT_EQ_INT(M.mark[1].pen, 13); ASSERT_EQ_INT(M.mark[2].pen, 1);    /* another mode: the ordinary pens whatever the byte */
}

TEST(a_freed_record_keeps_its_stored_bytes_and_the_routine_skips_it) {
    setup();
    put(1, 0, 0x12, 0, 0, 1800);
    pass(1);
    ASSERT_EQ_INT(M.b71[1], 1);
    I.rec[1].w0 &= (uint16_t)~0x0040;
    pass(1);
    ASSERT_EQ_INT(M.b71[1], 1);                                           /* freed: the routine skips it and the record keeps its bytes (note 07 1.4: only the placement, a copy, the rearm and the start write them) */
    ASSERT_EQ_INT(M.b71[2], 0xFF);                                        /* never placed */
}

int main(void) {
    RUN(a_record_at_the_camera_point_projects_to_column_159_row_90_and_the_east_north_one_to_132_68);
    RUN(only_records_with_bit_0x40_are_drawn_and_a_friend_on_the_ground_is_hidden);
    RUN(a_point_outside_the_frustum_or_below_the_window_is_rejected_and_stores_nothing);
    RUN(the_shape_is_the_heading_sector_floor_of_the_heading_plus_900_over_1800_modulo_16);
    RUN(the_direct_path_draws_the_true_position_on_odd_frames_only_and_stores_the_sector_and_nibbles_on_every_frame);
    RUN(with_v4a86d_clear_and_v4a90f_negative_nothing_is_drawn_not_even_the_ship);
    RUN(the_ship_is_drawn_in_pen_3_on_every_frame_with_no_store_unless_v4a90f_is_negative);
    RUN(with_v4a86d_clear_and_v4a90f_zero_the_stored_mark_is_drawn_on_every_frame_and_the_store_refreshes_only_while_the_sector_is_unset);
    RUN(the_pull_back_difference_is_wrapped_into_minus_8_to_plus_8);
    RUN(with_a_positive_v4a90f_the_frames_with_n_and_m_zero_refresh_the_store_and_draw_nothing_the_others_draw_the_stored_mark);
    RUN(the_fixed_camera_in_flight_is_v4a86d_clear_with_v4a90f_1_the_stored_mark_on_odd_frames_only);
    RUN(the_pens_the_player_13_a_friend_13_the_rest_1_and_in_mode_3_the_records_without_bit_6_of_byte_0x20_pen_3);
    RUN(a_freed_record_keeps_its_stored_bytes_and_the_routine_skips_it);
    TEST_MAIN_END();
}
