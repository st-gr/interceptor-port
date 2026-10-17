/* tests/test_logbook.c - the flight log and the qualification (src/core/logbook.c, docs/re/08-game-flow.md sections 9 and 10).
 *
 * The values are the note's: the record layout of 10.2, the container of 10.3, the name-entry table of 10.5 (one test per row, and the two captured overrun runs),
 * the figures of 10.6 (the shipped record's screen: 4, 3, 18 0, 9 6, 1 1, 0 hours 26 minutes, 3, 0), the clock of 10.7, the success test of 9.3. The text lines
 * the tests patch are invented placeholders; no game text and no shipped record is read. The second half drives the whole game (screens, qualification, flight
 * time, round trip through the game API) on invented screen assets; the groups that need the flight model run on the extracted assets when assets/ exists. */
#include "test.h"
#include "../src/core/logbook.h"
#include "../src/core/game.h"
#include "../src/core/screen.h"
#include "../src/core/flight.h"

static void type(logbook_name_t *n, logbook_t *lb, const char *s) { for (; *s; s++) logbook_name_key(n, lb, (unsigned char)*s); }

/* ---- the record and the container (10.2, 10.3) --------------------------------------------------------------------------------------------------- */

TEST(the_record_is_78_bytes_with_big_endian_words_at_the_notes_offsets) {
    logbook_t lb;
    logbook_zero(&lb);
    ASSERT_EQ_INT(LOGBOOK_SIZE, 78);
    logbook_set16(&lb, LB_QUALIFIED, 1);
    logbook_add(&lb, LB_CRASHES, 0x0102);
    ASSERT_EQ_INT(lb.rec[0], 0); ASSERT_EQ_INT(lb.rec[1], 1);
    ASSERT_EQ_INT(lb.rec[0x46], 1); ASSERT_EQ_INT(lb.rec[0x47], 2);
    ASSERT_EQ_INT(lb.dirty, 1);                                 /* a counter sets V:4A87D */
    ASSERT_EQ_INT(LB_SECONDS, 8); ASSERT_EQ_INT(LB_COMPLETIONS, 0x12); ASSERT_EQ_INT(LB_CALLSIGN, 0x1E);
    logbook_add(&lb, LB_CRASHES, 0xFFFF);
    ASSERT_EQ_INT(logbook_get16(&lb, LB_CRASHES), 0x0101);      /* words wrap */
}

TEST(a_new_player_is_the_zero_record_and_tours_zero_selects_the_first_tour) {
    logbook_t lb;
    memset(&lb, 0xAA, sizeof lb);
    logbook_zero(&lb);
    for (int i = 0; i < LOGBOOK_SIZE; i++) ASSERT_EQ_INT(lb.rec[i], 0);
    ASSERT_EQ_INT(logbook_get16(&lb, LB_TOURS), 0);
    ASSERT_EQ_INT(lb.dirty, 0); ASSERT_EQ_INT(lb.save_req, 0); ASSERT_EQ_INT(lb.live_level, 0);
}

TEST(the_container_is_magic_version_and_the_78_bytes_and_round_trips) {
    logbook_t a, b;
    logbook_zero(&a);
    for (int i = 0; i < LOGBOOK_SIZE; i++) a.rec[i] = (uint8_t)(i * 7 + 1);
    uint8_t buf[LOGBOOK_FILE_SIZE + 4];
    ASSERT_EQ_INT(logbook_file_encode(&a, buf, sizeof buf), 83);
    ASSERT_TRUE(memcmp(buf, "ILOG", 4) == 0);
    ASSERT_EQ_INT(buf[4], 1);
    ASSERT_TRUE(memcmp(buf + 5, a.rec, 78) == 0);
    ASSERT_EQ_INT(logbook_file_encode(&a, buf, 82), 0);         /* too small a buffer: nothing written */
    const uint8_t *rec; size_t n;
    ASSERT_EQ_INT(logbook_file_unwrap(buf, 83, &rec, &n), 0);
    ASSERT_EQ_INT(n, 78);
    logbook_load_record(&b, rec, n);
    ASSERT_TRUE(memcmp(a.rec, b.rec, 78) == 0);
    ASSERT_EQ_INT(b.live_level, 0);                              /* 14.3: V:4A95F is loaded from +2 BEFORE the log is read: every run starts at progress 0 */
}

TEST(the_bad_containers_are_refused_with_their_codes) {
    uint8_t buf[100];
    const uint8_t *rec; size_t n;
    memset(buf, 0, sizeof buf);
    memcpy(buf, "ILOG", 4); buf[4] = 1;
    ASSERT_EQ_INT(logbook_file_unwrap(buf, 4, &rec, &n), LOGBOOK_ERR_SHORT);      /* shorter than 5 bytes */
    ASSERT_EQ_INT(logbook_file_unwrap(buf, 0, &rec, &n), LOGBOOK_ERR_SHORT);
    ASSERT_EQ_INT(logbook_file_unwrap(NULL, 83, &rec, &n), LOGBOOK_ERR_SHORT);
    buf[0] = 'X';
    ASSERT_EQ_INT(logbook_file_unwrap(buf, 83, &rec, &n), LOGBOOK_ERR_MAGIC);
    buf[0] = 'I'; buf[4] = 2;
    ASSERT_EQ_INT(logbook_file_unwrap(buf, 83, &rec, &n), LOGBOOK_ERR_VERSION);
    buf[4] = 0;
    ASSERT_EQ_INT(logbook_file_unwrap(buf, 83, &rec, &n), LOGBOOK_ERR_VERSION);
    buf[4] = 1;
    ASSERT_EQ_INT(logbook_file_unwrap(buf, 5, &rec, &n), 0);                      /* a bare header is a valid container with an empty record */
    ASSERT_EQ_INT(n, 0);
}

TEST(a_short_inner_record_gives_the_leading_bytes_and_a_long_one_the_first_78) {
    logbook_t lb;
    uint8_t in[120];
    for (int i = 0; i < 120; i++) in[i] = (uint8_t)(i + 1);
    logbook_load_record(&lb, in, 10);
    for (int i = 0; i < 10; i++) ASSERT_EQ_INT(lb.rec[i], i + 1);
    for (int i = 10; i < 78; i++) ASSERT_EQ_INT(lb.rec[i], 0);
    logbook_load_record(&lb, in, 120);
    for (int i = 0; i < 78; i++) ASSERT_EQ_INT(lb.rec[i], i + 1);
    logbook_load_record(&lb, in, 0);
    for (int i = 0; i < 78; i++) ASSERT_EQ_INT(lb.rec[i], 0);
}

TEST(a_save_point_clears_the_dirty_flag_and_asks_for_the_blob) {
    logbook_t lb;
    logbook_zero(&lb);
    logbook_add(&lb, LB_CRASHES, 1);
    ASSERT_EQ_INT(lb.dirty, 1);
    logbook_save_point(&lb);
    ASSERT_EQ_INT(lb.dirty, 0); ASSERT_EQ_INT(lb.save_req, 1);
}

/* ---- the success test of the qualification (9.3) ------------------------------------------------------------------------------------------------- */

TEST(the_success_test_needs_all_five_conditions_and_nothing_else) {
    const uint16_t w0 = 0x11C8, w2 = 0xC082;                    /* the player record of a stopped aircraft on the deck, hook down and engaged, wheels down */
    ASSERT_EQ_INT(logbook_qual_success(w0, w2, 0, 0, 0), 1);
    ASSERT_EQ_INT(logbook_qual_success(w0, (uint16_t)(w2 & ~0x8000u), 0, 0, 0), 0);   /* hook up (the hook-up approach of 9.3 never succeeds) */
    ASSERT_EQ_INT(logbook_qual_success(w0, (uint16_t)(w2 & ~0x4000u), 0, 0, 0), 0);   /* the corridor flag alone is not enough, nor is its absence */
    ASSERT_EQ_INT(logbook_qual_success(w0, (uint16_t)(w2 & ~0x0080u), 0, 0, 0), 0);   /* wheels up */
    ASSERT_EQ_INT(logbook_qual_success(w0, 0x6082, 0, 0, 0), 0);                      /* the bolter capture: W2 = 0x6082 with the hook up */
    ASSERT_EQ_INT(logbook_qual_success(w0, w2, 1, 0, 0), 0);                          /* still rolling */
    ASSERT_EQ_INT(logbook_qual_success(w0, w2, -1, 0, 0), 0);
    ASSERT_EQ_INT(logbook_qual_success((uint16_t)(w0 & ~0x0040u), w2, 0, 0, 0), 0);   /* the record is not active */
    ASSERT_EQ_INT(logbook_qual_success(w0, w2, 0, 1, 0), 0);                          /* a leave request 3 is pending: the success is stored once */
    ASSERT_EQ_INT(logbook_qual_success(w0, w2, 0, 0, 0xFF), 0);                       /* the mission code is already set */
    ASSERT_EQ_INT(logbook_qual_success(w0, 0xE082, 0, 0, 0), 1);                      /* the captured catch: W2 = 0xE082 (the military-power detent bit does not matter) */
    ASSERT_EQ_INT(logbook_qual_success(0x93C8, w2, 0, 0, 0), 1);                      /* only the active bit of W0 is tested (the crash bit is the crash flow's) */
}

/* ---- what the log unlocks (9.6, 5.4, 10.8) ------------------------------------------------------------------------------------------------------ */

TEST(menu_key_7_and_the_function_keys_need_the_first_word_and_the_availability_byte) {
    logbook_t lb;
    logbook_zero(&lb);
    ASSERT_EQ_INT(logbook_item7_mode(&lb), 0);                  /* an empty log: only the click */
    for (int n = 1; n <= 6; n++) ASSERT_EQ_INT(logbook_fkey_mode(&lb, n), 0);
    logbook_set16(&lb, LB_QUALIFIED, 1);
    ASSERT_EQ_INT(logbook_item7_mode(&lb), 3);                  /* byte +6 = 0: outside 3 to 8, so 3 */
    ASSERT_EQ_INT(logbook_fkey_mode(&lb, 1), 3);                /* F1 is available once a record exists */
    for (int n = 2; n <= 6; n++) ASSERT_EQ_INT(logbook_fkey_mode(&lb, n), 0);
    lb.rec[LB_LAST_MISSION] = 5;
    ASSERT_EQ_INT(logbook_item7_mode(&lb), 6);
    lb.rec[LB_LAST_MISSION] = 8;
    ASSERT_EQ_INT(logbook_item7_mode(&lb), 3);                  /* 8 + 1 = 9 is outside 3 to 8 */
    lb.rec[LB_LAST_MISSION] = 2;
    ASSERT_EQ_INT(logbook_item7_mode(&lb), 3);
    lb.rec[LB_LAST_MISSION] = 3;
    ASSERT_EQ_INT(logbook_item7_mode(&lb), 4);
    /* the shipped completions 0 0 0 1 1 1 0 0 0: F1 to F4 */
    lb.rec[LB_COMPLETIONS + 3] = lb.rec[LB_COMPLETIONS + 4] = lb.rec[LB_COMPLETIONS + 5] = 1;
    static const int want[7] = { 0, 3, 4, 5, 6, 0, 0 };
    for (int n = 1; n <= 6; n++) ASSERT_EQ_INT(logbook_fkey_mode(&lb, n), want[n]);
    ASSERT_EQ_INT(logbook_fkey_mode(&lb, 0), 0);
    ASSERT_EQ_INT(logbook_fkey_mode(&lb, 7), 0);
}

TEST(the_sublist_selector_is_the_completion_byte_of_the_mode_limited_to_3) {
    logbook_t lb;
    logbook_zero(&lb);
    lb.rec[LB_COMPLETIONS + 3] = 2; lb.rec[LB_COMPLETIONS + 4] = 9;
    ASSERT_EQ_INT(logbook_sublist_selector(&lb, 3), 2);
    ASSERT_EQ_INT(logbook_sublist_selector(&lb, 4), 3);         /* an out-of-range byte of a foreign file is used as a small counter */
    ASSERT_EQ_INT(logbook_sublist_selector(&lb, 1), 0);
    ASSERT_EQ_INT(logbook_sublist_selector(&lb, 0x7F), 0);      /* the demo rows */
}

TEST(the_completion_bookkeeping_follows_0x134a0) {
    logbook_t lb;
    logbook_zero(&lb);
    lb.live_level = 1;
    lb.rec[LB_COMPLETIONS + 5] = 2;
    logbook_mission_complete(&lb, 5);
    ASSERT_EQ_INT(logbook_get16(&lb, LB_LEVEL), 1);             /* the saved level is one completion behind the live counter */
    ASSERT_EQ_INT(lb.live_level, 2);
    ASSERT_EQ_INT(lb.rec[LB_LAST_MISSION], 5);
    ASSERT_EQ_INT(lb.rec[LB_LAST_COUNT], 2);                    /* the old completion count */
    ASSERT_EQ_INT(lb.rec[LB_COMPLETIONS + 5], 3);
    ASSERT_EQ_INT(logbook_get16(&lb, LB_COMPLETED), 1);
    ASSERT_EQ_INT(lb.dirty, 1);
    logbook_mission_complete(&lb, 5);
    ASSERT_EQ_INT(lb.rec[LB_COMPLETIONS + 5], 3);               /* saturates at 3 */
    ASSERT_EQ_INT(lb.live_level, 3);
    logbook_mission_complete(&lb, 5);
    ASSERT_EQ_INT(lb.live_level, 3);
    ASSERT_EQ_INT(logbook_get16(&lb, LB_COMPLETED), 3);
    logbook_mission_complete(&lb, 9);                           /* the qualification does not go through it */
    ASSERT_EQ_INT(logbook_get16(&lb, LB_COMPLETED), 3);
    logbook_mission_started(&lb, 4); logbook_mission_started(&lb, 2);
    ASSERT_EQ_INT(logbook_get16(&lb, LB_STARTED), 1);
}

/* ---- the clock (10.7) --------------------------------------------------------------------------------------------------------------------------- */

TEST(flight_time_is_airborne_steps_over_8_with_the_bounds_of_the_note) {
    logbook_t lb;
    logbook_zero(&lb);
    ASSERT_EQ_INT(logbook_book_flight(&lb, 7), 0);              /* under a second: booked as 0 */
    ASSERT_EQ_INT(logbook_get32(&lb, LB_SECONDS), 0);
    ASSERT_EQ_INT(logbook_book_flight(&lb, 8 * 20 + 7), 20);    /* truncated */
    ASSERT_EQ_INT(logbook_get32(&lb, LB_SECONDS), 20);
    ASSERT_EQ_INT(logbook_book_flight(&lb, 8ul * 17999), 17999);
    ASSERT_EQ_INT(logbook_get32(&lb, LB_SECONDS), 20 + 17999);
    const uint32_t before = logbook_get32(&lb, LB_SECONDS);
    ASSERT_EQ_INT(logbook_book_flight(&lb, 8ul * 18000), -1);   /* five hours or more: dropped */
    ASSERT_EQ_INT(logbook_get32(&lb, LB_SECONDS), before);
    logbook_zero(&lb);
    logbook_book_flight(&lb, 8ul * 0x4203);                       /* 16899 s */
    ASSERT_EQ_INT(lb.rec[8], 0); ASSERT_EQ_INT(lb.rec[9], 0); ASSERT_EQ_INT(lb.rec[10], 0x42); ASSERT_EQ_INT(lb.rec[11], 3);
}

/* ---- the log screen's figures (10.6) ------------------------------------------------------------------------------------------------------------ */

static void filler(char *t, size_t n, char c) { memset(t, c, n); t[n] = '\0'; }

TEST(the_figures_are_patched_right_to_left_with_leading_zeros_blanked_at_the_notes_indices) {
    logbook_t lb;
    logbook_zero(&lb);
    /* the shipped record's screen: started 4, completed 3, cannon 18 and 0, AIM-120 9 and 6, AIM-9L 1 and 1, 1,609 seconds, crashes 3, missile hits 0 */
    logbook_set16(&lb, LB_STARTED, 4); logbook_set16(&lb, LB_COMPLETED, 3);
    logbook_set16(&lb, LB_CANNON_FIRED, 18); logbook_set16(&lb, LB_CANNON_HITS, 0);
    logbook_set16(&lb, LB_AIM120_FIRED, 9); logbook_set16(&lb, LB_AIM120_HITS, 6);
    logbook_set16(&lb, LB_AIM9_FIRED, 1); logbook_set16(&lb, LB_AIM9_HITS, 1);
    lb.rec[8] = 0; lb.rec[9] = 0; lb.rec[10] = 0x06; lb.rec[11] = 0x49;      /* 1609 */
    logbook_set16(&lb, LB_CRASHES, 3);
    char t[64];
    filler(t, 39, '.');
    ASSERT_EQ_INT(logbook_patch_line(&lb, 0x57, 2, t, 39), 1);
    ASSERT_TRUE(strncmp(t + 36, "  4", 3) == 0);
    ASSERT_TRUE(t[35] == '.');                                    /* nothing outside the figure */
    filler(t, 39, '.'); logbook_patch_line(&lb, 0x57, 3, t, 39);
    ASSERT_TRUE(strncmp(t + 36, "  3", 3) == 0);
    filler(t, 35, '.'); ASSERT_EQ_INT(logbook_patch_line(&lb, 0x57, 6, t, 35), 1);
    ASSERT_TRUE(strncmp(t + 22, "   18", 5) == 0);
    ASSERT_TRUE(strncmp(t + 31, "   0", 4) == 0);                 /* the last digit is always shown */
    ASSERT_TRUE(t[21] == '.' && t[27] == '.' && t[30] == '.');
    filler(t, 36, '.'); logbook_patch_line(&lb, 0x57, 7, t, 36);
    ASSERT_TRUE(strncmp(t + 22, "    9", 5) == 0); ASSERT_TRUE(strncmp(t + 31, "   6", 4) == 0);
    filler(t, 36, '.'); logbook_patch_line(&lb, 0x57, 8, t, 36);
    ASSERT_TRUE(strncmp(t + 22, "    1", 5) == 0); ASSERT_TRUE(strncmp(t + 31, "   1", 4) == 0);
    filler(t, 38, '.'); ASSERT_EQ_INT(logbook_patch_line(&lb, 0x57, 10, t, 38), 1);
    ASSERT_TRUE(strncmp(t + 22, "   0", 4) == 0);                 /* 0 hours */
    ASSERT_TRUE(strncmp(t + 32, "26", 2) == 0);                   /* 26 minutes: (1609 mod 3600) / 60 */
    filler(t, 26, '.'); ASSERT_EQ_INT(logbook_patch_line(&lb, 0x57, 11, t, 26), 1);
    ASSERT_TRUE(strncmp(t + 22, "   3", 4) == 0);
    filler(t, 26, '.'); ASSERT_EQ_INT(logbook_patch_line(&lb, 0x57, 12, t, 26), 1);
    ASSERT_TRUE(strncmp(t + 22, "   0", 4) == 0);
    for (int line = 0; line < 18; line++) {                       /* the other six lines (title, blanks, key lines) carry no figure */
        const int fig = line == 2 || line == 3 || line == 6 || line == 7 || line == 8 || line == 10 || line == 11 || line == 12;
        filler(t, 40, 'x');
        ASSERT_EQ_INT(logbook_patch_line(&lb, 0x57, line, t, 40), fig);
    }
    ASSERT_EQ_INT(logbook_patch_line(&lb, 0x58, 2, t, 40), 0);    /* another record */
}

TEST(a_field_above_its_width_shows_its_low_digits_and_hours_are_a_16_bit_quotient) {
    logbook_t lb;
    logbook_zero(&lb);
    logbook_set16(&lb, LB_AIM120_FIRED, 0x2041);                  /* 8257 */
    logbook_set16(&lb, LB_AIM120_HITS, 16963);                    /* 10^4 modulus: 6963 */
    logbook_set16(&lb, LB_AIM9_FIRED, 17477); logbook_set16(&lb, LB_AIM9_HITS, 7991);
    logbook_set16(&lb, LB_CRASHES, 8505); logbook_set16(&lb, LB_MISSILE_HITS, 9019);
    logbook_set16(&lb, LB_STARTED, 1234);                         /* 3 digits: 234 */
    char t[64];
    filler(t, 36, ' '); logbook_patch_line(&lb, 0x57, 7, t, 36);
    ASSERT_TRUE(strncmp(t + 22, " 8257", 5) == 0); ASSERT_TRUE(strncmp(t + 31, "6963", 4) == 0);
    filler(t, 36, ' '); logbook_patch_line(&lb, 0x57, 8, t, 36);
    ASSERT_TRUE(strncmp(t + 22, "17477", 5) == 0); ASSERT_TRUE(strncmp(t + 31, "7991", 4) == 0);
    filler(t, 26, ' '); logbook_patch_line(&lb, 0x57, 11, t, 26);
    ASSERT_TRUE(strncmp(t + 22, "8505", 4) == 0);
    filler(t, 26, ' '); logbook_patch_line(&lb, 0x57, 12, t, 26);
    ASSERT_TRUE(strncmp(t + 22, "9019", 4) == 0);
    filler(t, 39, ' '); logbook_patch_line(&lb, 0x57, 2, t, 39);
    ASSERT_TRUE(strncmp(t + 36, "234", 3) == 0);
    logbook_zero(&lb);
    lb.rec[8] = 0; lb.rec[9] = 1; lb.rec[10] = 0; lb.rec[11] = 0;   /* 65536 seconds = 18 h 12 min 16 s */
    filler(t, 38, ' '); logbook_patch_line(&lb, 0x57, 10, t, 38);
    ASSERT_TRUE(strncmp(t + 22, "  18", 4) == 0); ASSERT_TRUE(strncmp(t + 32, "12", 2) == 0);
    logbook_zero(&lb);                                            /* the all-zero record: every figure shows one zero */
    filler(t, 39, 'x'); logbook_patch_line(&lb, 0x57, 2, t, 39);
    ASSERT_TRUE(strncmp(t + 36, "  0", 3) == 0);
}

TEST(a_line_too_short_for_its_figure_is_left_alone) {
    logbook_t lb;
    logbook_zero(&lb);
    char t[8];
    filler(t, 5, 'x');
    ASSERT_EQ_INT(logbook_patch_line(&lb, 0x57, 6, t, 5), 1);
    ASSERT_TRUE(strcmp(t, "xxxxx") == 0);                         /* never a write past the line */
}

TEST(the_continuing_welcome_copies_the_callsign_from_text_index_8_up_to_24_or_the_first_zero) {
    logbook_t lb;
    logbook_zero(&lb);
    memcpy(lb.rec + LB_CALLSIGN, "TEST", 5);
    char t[48];
    filler(t, 39, '-');
    ASSERT_EQ_INT(logbook_patch_line(&lb, 0x02, 0, t, 39), 1);
    ASSERT_TRUE(strncmp(t, "--------TEST---", 15) == 0);
    memset(lb.rec + LB_CALLSIGN, 'Z', LOGBOOK_NAME_FIELD);       /* an unterminated name is cut at 24 */
    filler(t, 39, '-');
    logbook_patch_line(&lb, 0x02, 0, t, 39);
    ASSERT_TRUE(t[8 + 23] == 'Z' && t[8 + 24] == '-');
    logbook_zero(&lb);                                            /* an empty name changes nothing */
    filler(t, 39, '-');
    logbook_patch_line(&lb, 0x02, 0, t, 39);
    ASSERT_TRUE(strspn(t, "-") == 39);
    ASSERT_EQ_INT(logbook_patch_line(&lb, 0x02, 2, t, 39), 0);
}

/* ---- name entry (10.5), Review Focus 2 ------------------------------------------------------------------------------------------------------- */

TEST(name_entry_row_1_to_10_stores_the_callsign_and_counts_pos_and_rem) {
    logbook_t lb; logbook_name_t n;
    logbook_zero(&lb); logbook_name_begin(&n);
    ASSERT_EQ_INT(n.pos, 0); ASSERT_EQ_INT(n.rem, 20);
    for (int k = 1; k <= 10; k++) {
        ASSERT_EQ_INT(logbook_name_key(&n, &lb, 'A' + k - 1), LB_NAME_TYPED);
        ASSERT_EQ_INT(lb.rec[LB_CALLSIGN + k - 1], 'A' + k - 1);
        ASSERT_EQ_INT(n.pos, k); ASSERT_EQ_INT(n.rem, 20 - k);
        ASSERT_EQ_INT(n.shadow[k - 1], 'A' + k - 1);
    }
    ASSERT_EQ_INT(n.ncell, 10);
    ASSERT_STR_EQ(n.cell, "ABCDEFGHIJ");
}

TEST(name_entry_row_11_overwrites_rem_with_the_character_and_stores_at_0x28) {
    logbook_t lb; logbook_name_t n;
    logbook_zero(&lb); logbook_name_begin(&n);
    type(&n, &lb, "ABCDEFGHIJ");
    logbook_name_key(&n, &lb, 'K');
    ASSERT_EQ_INT(lb.rec[0x28], 'K');
    ASSERT_EQ_INT(n.pos, 11);
    ASSERT_EQ_INT(n.rem, 'K' - 1);                                /* c - 1 */
    ASSERT_EQ_INT(lb.rec[0x3E], 0);
    logbook_zero(&lb); logbook_name_begin(&n);
    type(&n, &lb, "ABCDEFGHIJ ");
    ASSERT_EQ_INT(lb.rec[0x28], ' '); ASSERT_EQ_INT(n.rem, 0x1F);   /* a SPACE as the 11th: rem 0x1F */
}

TEST(name_entry_row_12_space_overwrites_pos_and_reaches_0x3e_any_other_character_reaches_no_byte) {
    logbook_t lb; logbook_name_t n;
    logbook_zero(&lb); logbook_name_begin(&n);
    type(&n, &lb, "ABCDEFGHIJK");                                 /* rem = 0x4A */
    ASSERT_EQ_INT(logbook_name_key(&n, &lb, ' '), LB_NAME_TYPED);
    ASSERT_EQ_INT(lb.rec[0x3E], 0x20);                            /* +0x1E + c with c = 0x20 */
    ASSERT_EQ_INT(n.pos, 0x21);                                   /* c + 1 */
    ASSERT_EQ_INT(n.rem, 0x49);                                   /* rem - 1 */
    static const char others[] = "AZ09";
    for (int i = 0; others[i]; i++) {
        logbook_zero(&lb); logbook_name_begin(&n);
        type(&n, &lb, "ABCDEFGHIJK");
        uint8_t before[LOGBOOK_SIZE]; memcpy(before, lb.rec, sizeof before);
        logbook_name_key(&n, &lb, others[i]);                     /* c >= 0x30: +0x1E + c is beyond the record, not stored */
        ASSERT_TRUE(memcmp(before, lb.rec, sizeof before) == 0);
        ASSERT_EQ_INT(n.pos, others[i] + 1);
        ASSERT_EQ_INT(n.rem, 0x49);
    }
}

TEST(name_entry_rows_13_on_with_a_space_as_the_12th_store_at_0x3f_to_0x4d_and_the_28th_is_dropped) {
    logbook_t lb; logbook_name_t n;
    logbook_zero(&lb); logbook_name_begin(&n);
    type(&n, &lb, "ABCDEFGHIJK ");
    for (int k = 13; k <= 27; k++) {
        logbook_name_key(&n, &lb, 'A' + (k - 13));
        ASSERT_EQ_INT(lb.rec[0x3F + (k - 13)], 'A' + (k - 13));   /* +0x1E + pos with pos = 0x21 + (k - 13) */
        ASSERT_EQ_INT(n.pos, 0x21 + (k - 12));
        ASSERT_EQ_INT(n.rem, 0x49 - (k - 12));
    }
    ASSERT_EQ_INT(n.pos, 0x30); ASSERT_EQ_INT(n.rem, 0x3A);      /* the capture: pos 0x30 and rem 0x3A before RETURN */
    uint8_t before[LOGBOOK_SIZE]; memcpy(before, lb.rec, sizeof before);
    logbook_name_key(&n, &lb, 'Z');                               /* the 28th: +0x1E + 0x30 = 78, beyond the record */
    ASSERT_TRUE(memcmp(before, lb.rec, sizeof before) == 0);
    ASSERT_EQ_INT(n.pos, 0x31);
}

TEST(the_captured_overrun_a_run_gives_the_callsign_and_the_counters_the_note_lists) {
    logbook_t lb; logbook_name_t n;
    logbook_zero(&lb); logbook_name_begin(&n);
    type(&n, &lb, "ABCDEFGHIJK ABCDEFGHIJKLMNO");
    ASSERT_STR_EQ((const char *)lb.rec + LB_CALLSIGN, "ABCDEFGHIJK");   /* the first 11 characters, then the zero of the record */
    ASSERT_EQ_INT(lb.rec[0x3E], 0x20);
    ASSERT_TRUE(memcmp(lb.rec + 0x3F, "ABCDEFGHIJKLMNO", 15) == 0);
    /* the log screen then reads the words 0x2041 0x4243 0x4445 0x4647 0x4849 0x4A4B as the counters */
    ASSERT_EQ_INT(logbook_get16(&lb, LB_AIM120_FIRED), 0x2041);
    ASSERT_EQ_INT(logbook_get16(&lb, LB_AIM120_HITS), 0x4243);
    ASSERT_EQ_INT(logbook_get16(&lb, LB_AIM9_FIRED), 0x4445);
    ASSERT_EQ_INT(logbook_get16(&lb, LB_AIM9_HITS), 0x4647);
    ASSERT_EQ_INT(logbook_get16(&lb, LB_CRASHES), 0x4849);
    ASSERT_EQ_INT(logbook_get16(&lb, LB_MISSILE_HITS), 0x4A4B);
    ASSERT_EQ_INT(n.pos, 0x30); ASSERT_EQ_INT(n.rem, 0x3A);
    char t[40];
    filler(t, 36, ' '); logbook_patch_line(&lb, 0x57, 7, t, 36);  /* and the screen shows 8257 and 6963 (16963 mod 10^4) */
    ASSERT_TRUE(strncmp(t + 22, " 8257", 5) == 0); ASSERT_TRUE(strncmp(t + 31, "6963", 4) == 0);
}

TEST(a_letter_as_the_12th_character_changes_nothing_beyond_the_11_stored_the_25_letter_run) {
    logbook_t lb; logbook_name_t n;
    logbook_zero(&lb); logbook_name_begin(&n);
    type(&n, &lb, "ABCDEFGHIJKLMNOPQRSTUVWXY");                   /* 25 letters */
    ASSERT_STR_EQ((const char *)lb.rec + LB_CALLSIGN, "ABCDEFGHIJK");
    for (int i = 0x29; i < LOGBOOK_SIZE; i++) ASSERT_EQ_INT(lb.rec[i], 0);
    ASSERT_EQ_INT(n.ncell, 25);                                   /* all 25 are drawn */
    ASSERT_EQ_INT(strlen(n.cell), 25);
}

TEST(the_automatic_end_comes_after_11_plus_the_11th_character_code_the_43_and_86_character_runs) {
    logbook_t lb; logbook_name_t n;
    logbook_zero(&lb); logbook_name_begin(&n);
    type(&n, &lb, "ABCDEFGHIJ ");                                 /* a SPACE as the 11th: rem 0x1F */
    int count = 11, r = LB_NAME_TYPED;
    while (r != LB_NAME_END && count < 200) { r = logbook_name_key(&n, &lb, 'A' + count % 26); count++; }
    ASSERT_EQ_INT(count, 43);
    ASSERT_EQ_INT(n.rem, 0xFF);                                   /* the capture: rem read 0xFF after 43 characters */
    for (int i = 0x2A; i < LOGBOOK_SIZE; i++) ASSERT_EQ_INT(lb.rec[i], 0);   /* nothing beyond the 11th reached the record */
    logbook_zero(&lb); logbook_name_begin(&n);
    type(&n, &lb, "ABCDEFGHIJK");                                 /* a K as the 11th: 11 + 0x4B = 86 */
    count = 11; r = LB_NAME_TYPED;
    while (r != LB_NAME_END && count < 200) { r = logbook_name_key(&n, &lb, 'A'); count++; }
    ASSERT_EQ_INT(count, 86);
    logbook_zero(&lb); logbook_name_begin(&n);
    r = LB_NAME_TYPED;                                            /* 12th a SPACE: the pos jump does not change rem's count down */
    type(&n, &lb, "ABCDEFGHIJK ");
    count = 12;
    while (r != LB_NAME_END && count < 200) { r = logbook_name_key(&n, &lb, 'B'); count++; }
    ASSERT_EQ_INT(count, 11 + 0x4B);
}

TEST(backspace_erases_downward_clears_the_bytes_and_does_nothing_at_an_empty_field) {
    logbook_t lb; logbook_name_t n;
    logbook_zero(&lb); logbook_name_begin(&n);
    ASSERT_EQ_INT(logbook_name_key(&n, &lb, 8), LB_NAME_IGNORED);   /* three presses at an empty field */
    logbook_name_key(&n, &lb, 8); logbook_name_key(&n, &lb, 8);
    ASSERT_EQ_INT(n.pos, 0); ASSERT_EQ_INT(n.rem, 20); ASSERT_EQ_INT(n.ncell, 0);
    for (int i = 0; i < LOGBOOK_SIZE; i++) ASSERT_EQ_INT(lb.rec[i], 0);
    type(&n, &lb, "ABCD");
    for (int k = 4; k >= 1; k--) {
        ASSERT_EQ_INT(logbook_name_key(&n, &lb, 8), LB_NAME_ERASED);
        ASSERT_EQ_INT(n.pos, k - 1); ASSERT_EQ_INT(n.rem, 20 - (k - 1));
        ASSERT_EQ_INT(lb.rec[LB_CALLSIGN + k - 1], 0);
        ASSERT_EQ_INT(n.ncell, k - 1);
    }
    ASSERT_EQ_INT(logbook_name_key(&n, &lb, 8), LB_NAME_IGNORED);
    /* after the overrun the bytes are cleared downwards as pos falls */
    logbook_zero(&lb); logbook_name_begin(&n);
    type(&n, &lb, "ABCDEFGHIJK ABC");
    ASSERT_EQ_INT(n.pos, 0x24);
    logbook_name_key(&n, &lb, 8);                                 /* pos 0x23: clears +0x1E + 0x23 - 0 -> the byte of the 15th character (+0x41) */
    ASSERT_EQ_INT(lb.rec[0x41], 0); ASSERT_EQ_INT(lb.rec[0x40], 'B');
    ASSERT_EQ_INT(n.pos, 0x23);
    for (int i = 0; i < 4; i++) logbook_name_key(&n, &lb, 8);     /* down to pos 0x1F: +0x3E.. */
    ASSERT_EQ_INT(n.pos, 0x1F);
    ASSERT_EQ_INT(lb.rec[0x3E], 0);                               /* the SPACE at +0x3E went with the fourth press */
    ASSERT_EQ_INT(lb.rec[0x3D], 0);
}

TEST(name_entry_accepts_letters_digits_and_space_upper_cased_and_ignores_every_other_key) {
    logbook_t lb; logbook_name_t n;
    logbook_zero(&lb); logbook_name_begin(&n);
    ASSERT_EQ_INT(logbook_name_key(&n, &lb, 'q'), LB_NAME_TYPED);   /* there is no lower case */
    ASSERT_EQ_INT(logbook_name_key(&n, &lb, '7'), LB_NAME_TYPED);
    ASSERT_EQ_INT(logbook_name_key(&n, &lb, ' '), LB_NAME_TYPED);
    static const int bad[] = { 0, 1, 9, 13, 27, 127, 128, 200, 255, '-', '=', '.', ',', '@', '[', '`', '{', '~', '!', 256, 0x4141, -1, -65 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) ASSERT_EQ_INT(logbook_name_key(&n, &lb, bad[i]), LB_NAME_IGNORED);
    ASSERT_EQ_INT(n.pos, 3);
    ASSERT_TRUE(memcmp(lb.rec + LB_CALLSIGN, "Q7 ", 4) == 0);
    ASSERT_STR_EQ(n.cell, "Q7 ");
}

TEST(only_41_cells_are_kept_for_the_screen_and_the_record_is_never_written_past_byte_78) {
    logbook_t lb; logbook_name_t n;
    logbook_zero(&lb); logbook_name_begin(&n);
    type(&n, &lb, "ABCDEFGHIJK ");
    for (int i = 0; i < 31; i++) logbook_name_key(&n, &lb, 'A' + i % 26);
    ASSERT_EQ_INT(n.ncell, 43);
    ASSERT_EQ_INT(strlen(n.cell), 41);                            /* at most 41 cells are drawn, no wrap (deviation 3) */
    logbook_zero(&lb); logbook_name_begin(&n);
    for (int pass = 0; pass < 3; pass++) { type(&n, &lb, "ABCDEFGHIJK "); type(&n, &lb, "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ"); logbook_name_begin(&n); }
    ASSERT_EQ_INT(lb.dirty, 0); ASSERT_EQ_INT(lb.save_req, 0); ASSERT_EQ_INT(lb.live_level, 0);
}

/* ---- the invented assets ------------------------------------------------------------------------------------------------------------------------ */
typedef struct { uint8_t d[65536]; size_t n; } blob_t;
static blob_t B_TEXT, B_IDS, B_SCN;
static void p8(blob_t *b, unsigned v) { b->d[b->n++] = (uint8_t)v; }
static void p16(blob_t *b, unsigned v) { p8(b, v & 255); p8(b, v >> 8); }

typedef struct { int id, line, row, col, flags, colour, delay, len; } tl_t;
static tl_t TL[400];
static int ntl;
static int id_first[128], id_count[128];
static void add_line(int id, int row, int col, int flags, int colour, int delay, int len) {
    tl_t *l = &TL[ntl];
    l->id = id; l->row = row; l->col = col; l->flags = flags; l->colour = colour; l->delay = delay; l->len = len;
    l->line = id_count[id]++;
    if (l->line == 0) id_first[id] = ntl;
    ntl++;
}

static void build_text(void) {
    ntl = 0;
    memset(id_first, 0, sizeof id_first); memset(id_count, 0, sizeof id_count);
    /* records must be stored in order of their first id: build each id in order */
    for (int id = 1; id <= 110; id++) {
        switch (id) {
        case 0x0F: {   /* the credits: six lines, one blank, typed, spaces skipped */
            static const int len[6] = { 20, 12, 0, 30, 16, 24 };
            for (int k = 0; k < 6; k++) add_line(id, 8, 6, 3, 0x53, 2, len[k]);
            break;
        }
        case 0x46: add_line(id, 90, 6, 0, 0x23, 3, 30); break;                 /* the log-disk lines: instant */
        case 0x5E: for (int k = 0; k < 3; k++) add_line(id, 110, 6, 0, 0x23, 3, 28); break;
        case 0x6D: add_line(id, 170, 4, 1, 0xA3, 2, 28); add_line(id, 170, 4, 1, 0xA3, 2, 28); break;
        case 0x40: add_line(id, 40, 4, 1, 0x53, 2, 24); break;
        case 0x6E: add_line(id, 120, 4, 1, 0x53, 2, 24); break;
        case 7: add_line(id, 170, 2, 1, 0xD3, 2, 36); add_line(id, 170, 2, 1, 0xD3, 2, 30); break;   /* the location caption */
        case 0x4B: for (int k = 0; k < 3; k++) add_line(id, 60, 8, 1, 0x93, 2, 20); break;          /* the aircraft list */
        case 0x62: case 0x63:   /* the game over: a line of 23, a blank, a line of 38 characters (note 08 5.12) */
            add_line(id, 90, 6, 1, 0x13, 3, 23); add_line(id, 90, 6, 1, 0x13, 3, 0); add_line(id, 90, 6, 3, 0x18, 3, 38); break;
        case 0x26: add_line(id, 43, 16, 1, 0x96, 4, 10); break;                 /* the pause text */
        case 0x1E: add_line(id, 6, 4, 1, 0x93, 2, 30); add_line(id, 6, 4, 1, 0x93, 2, 20); break;
        case 0x55: add_line(id, 6, 4, 1, 0x93, 2, 30); break;                    /* 0x8055 plays after 0x1E */
        case 1: add_line(id, 16, 4, 1, 0x26, 0, 35); add_line(id, 16, 4, 1, 0x26, 0, 0); add_line(id, 16, 4, 1, 0x26, 6, 20); break;   /* the first-tour welcome */
        case 2: add_line(id, 36, 4, 3, 0x26, 0, 39); add_line(id, 36, 4, 3, 0x26, 0, 0); add_line(id, 36, 4, 3, 0x26, 5, 32); break;      /* the continuing welcome */
        case 3: add_line(id, 130, 4, 3, 0x56, 0, 35); add_line(id, 130, 4, 3, 0x56, 0, 0); add_line(id, 130, 4, 3, 0x26, 8, 31); break;  /* the callsign prompt */
        case 0x49: add_line(id, 6, 2, 1, 0x96, 3, 26); add_line(id, 6, 2, 1, 0x96, 3, 0); add_line(id, 6, 2, 1, 0x96, 3, 32); break;   /* the qualification briefing */
        case 0x57: {   /* the log page: 18 lines of the lengths of note 08 2.4 */
            static const int len[18] = { 30, 0, 39, 39, 0, 35, 36, 36, 36, 0, 38, 26, 26, 0, 0, 24, 40, 21 };
            for (int k = 0; k < 18; k++) add_line(id, 0, 2, 3, 0x42, 1, len[k]);
            break;
        }
        case 16: case 17: case 18: case 19: case 21: case 22: case 25: break;    /* empty records and the ids that name no record */
        default: add_line(id, 20 + (id % 100), 4, 1, 0x93, 2, 10); break;
        }
    }
    B_TEXT.n = 0;
    p16(&B_TEXT, ntl);
    for (int i = 0; i < ntl; i++) {
        const tl_t *l = &TL[i];
        p16(&B_TEXT, l->id); p8(&B_TEXT, l->line); p8(&B_TEXT, l->row); p8(&B_TEXT, l->col); p8(&B_TEXT, l->flags);
        p8(&B_TEXT, l->colour); p8(&B_TEXT, l->delay); p16(&B_TEXT, l->len);
        for (int k = 0; k < l->len; k++) p8(&B_TEXT, 'A' + (i + k) % 26);
    }
    B_IDS.n = 0;
    p16(&B_IDS, 110);
    for (int id = 1; id <= 110; id++) { p16(&B_IDS, id); p16(&B_IDS, id_count[id] ? id_first[id] : 0); p16(&B_IDS, id_count[id]); }
}

static void sec(blob_t *b, unsigned count, unsigned rec) { p16(b, count); p16(b, rec); }
static void entry(blob_t *b, unsigned w0, unsigned w1, unsigned w2, unsigned w3, unsigned w4) { p16(b, w0); p16(b, w1); p16(b, w2); p16(b, w3); p16(b, w4); }
static void build_scenario(void) {
    B_SCN.n = 0;
    sec(&B_SCN, 5, 16);   /* start blocks: four with invented numbers; the fifth, like the original's, names an inactive carrier record */
    {   /* cell_x, cell_z, n, ox, oz, fx, fz, degrees */
        static const int blk[3][8] = { { 16, 16, 2, 100, 200, 300, -400, 90 }, { 15, 17, 3, -50, 60, 70, 80, 180 }, { 14, 15, 4, 11, 12, 13, -14, 270 } };
        for (int b = 0; b < 3; b++) for (int k = 0; k < 8; k++) p16(&B_SCN, (unsigned)blk[b][k] & 0xFFFF);
        p16(&B_SCN, 0x800E); for (int k = 1; k < 8; k++) p16(&B_SCN, 0);
        p16(&B_SCN, 0x800F); for (int k = 1; k < 8; k++) p16(&B_SCN, 0);
    }
    sec(&B_SCN, 0, 14);   /* regions */
    sec(&B_SCN, 0, 10);   /* region entries */
    sec(&B_SCN, 10, 12);  /* placement rows: row r has the list r % 2 in all three variants; the caps differ per row */
    for (int r = 0; r < 10; r++) for (int v = 0; v < 3; v++) { p16(&B_SCN, (unsigned)(r % 2)); p16(&B_SCN, r == 0 ? 2 : r == 7 ? 4 : r == 4 ? 5 : 3); }
    sec(&B_SCN, 2, 8);    /* lists: four sub-list indices each */
    p16(&B_SCN, 0); p16(&B_SCN, 1); p16(&B_SCN, 0); p16(&B_SCN, 0);   /* list 0: selector 1 chooses the single-carrier sub-list */
    p16(&B_SCN, 1); p16(&B_SCN, 1); p16(&B_SCN, 1); p16(&B_SCN, 1);
    sec(&B_SCN, 2, 4);    /* sub-lists: first entry, count */
    p16(&B_SCN, 0); p16(&B_SCN, 7); p16(&B_SCN, 7); p16(&B_SCN, 1);
    sec(&B_SCN, 8, 10);   /* sub-list entries: template offset (20 x id), class, slot, position reference, flags */
    entry(&B_SCN, 140, 0x20, 14, 52, 0x8000);   /* a carrier, not counted */
    entry(&B_SCN, 60, 0x10, 4, 38, 0x0000);     /* an aircraft, counted */
    entry(&B_SCN, 60, 0x10, 6, 74, 0x0000);     /* an aircraft, counted */
    entry(&B_SCN, 120, 0x12, 8, 0, 0x0000);     /* an aircraft, counted: the cap (2 in row 0) is reached before it */
    entry(&B_SCN, 60, 0x10, 10, 0, 0x0005);     /* progress threshold 5: not placed with progress 0 */
    entry(&B_SCN, 60, 0x15, 11, 0, 0x0200);     /* mode filter 2: placed in mode 2 only */
    entry(&B_SCN, 60, 0x15, 12, 0, 0x0000);     /* a missile (class 0x15, not an aircraft): never counted */
    entry(&B_SCN, 140, 0x20, 14, 52, 0x8000);   /* list 1, sub-list 1: a single carrier */
    sec(&B_SCN, 0, 0);
}

static blob_t B_PIX;
static int nofiles;
static int sread(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    const blob_t *b = NULL;
    static const char ver[] = "27\n", pix[] = "splsh\n";
    if (!strcmp(path, "VERSION")) { *out = (uint8_t *)malloc(sizeof ver); memcpy(*out, ver, sizeof ver); *len = sizeof ver - 1; return 0; }
    if (!strcmp(path, "pix/index.txt")) { *out = (uint8_t *)malloc(sizeof pix); memcpy(*out, pix, sizeof pix); *len = sizeof pix - 1; return 0; }
    if (nofiles) return -1;
    if (!strcmp(path, "tables/text.bin")) b = &B_TEXT;
    else if (!strcmp(path, "tables/text_ids.bin")) b = &B_IDS;
    else if (!strcmp(path, "tables/scenario.bin")) b = &B_SCN;
    if (!b) return -1;
    *out = (uint8_t *)malloc(b->n + 1);
    memcpy(*out, b->d, b->n);
    *len = b->n;
    return 0;
}
static void srel(void *user, uint8_t *buf) { (void)user; free(buf); }
static game_loader_t SLD = { sread, srel, NULL };
static void build_assets(void) { build_text(); build_scenario(); (void)B_PIX; }

/* ---- helpers --------------------------------------------------------------------------------------------------------------------------------- */
static screen_t *new_screen(void) {
    build_assets();
    screen_t *s = (screen_t *)malloc(sizeof *s);
    char err[200] = "";
    if (screen_init(s, &SLD, err, sizeof err) != 0) { printf("  (screen_init: %s)\n", err); free(s); return NULL; }
    return s;
}
static void del(screen_t *s) { screen_destroy(s); free(s); }
static void key(screen_t *s, int c) { game_event_t ev = { GAME_EV_KEY, 0, c }; screen_key(s, &ev); }
static void btn(screen_t *s, int code, int v) { game_event_t ev = { GAME_EV_BUTTON, code, v }; screen_key(s, &ev); }
static void steps(screen_t *s, int n) { for (int i = 0; i < n; i++) screen_step(s); }
static game_scene_t SC;
static const game_scene_t *fill(screen_t *s) { memset(&SC, 0, sizeof SC); SC.mode = s->mode; screen_fill(s, &SC); return &SC; }
static int lines(screen_t *s) { return fill(s)->text_count; }
/* the screens a session passes before the menu */
static void to_menu(screen_t *s) { key(s, 'x'); steps(s, 11); }   /* the credits key leads to the menu in 7 steps (no log-disk prompt by default) */
static void to_location(screen_t *s) { to_menu(s); key(s, '2'); steps(s, 26); }
/* the chain to a running flight: ... location digit, aircraft digit, then the approach skip and the notice */
static void to_flight(screen_t *s, int loc, int air) {
    to_location(s);
    key(s, '0' + loc);
    if (loc != 4) { steps(s, 8); key(s, '0' + air); }
    for (int i = 0; i < 400 && s->state != SCR_FLIGHT; i++) {
        steps(s, 1);
        if (s->state == SCR_APPROACH) btn(s, GAME_BTN_KEYPAD, GAME_KP_ENTER);
    }
}


static void g_step(game_t *g, int n) { for (int i = 0; i < n; i++) game_step(g); }
static void g_key(game_t *g, int c) { game_event_t ev = { GAME_EV_KEY, 0, c }; game_input(g, &ev); }
static void g_btn(game_t *g, int code, int v) { game_event_t ev = { GAME_EV_BUTTON, code, v }; game_input(g, &ev); }
static game_scene_t GS;
static const game_scene_t *g_scene(game_t *g) { game_scene(g, &GS); return &GS; }
static const char *real_root = "assets";
static int real_read(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    char p[256];
    snprintf(p, sizeof p, "%s/%s", real_root, path);
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
static game_loader_t RLD = { real_read, srel, NULL };
static int have_real(void) { uint8_t *b; size_t n; if (real_read(NULL, "tables/text.bin", &b, &n) != 0) return 0; free(b); return real_read(NULL, "models/index.txt", &b, &n) == 0 ? (free(b), 1) : 0; }

/* ======================================================================================================================================================
 * The screens of the log on the invented assets (screen_t with a logbook): welcome, callsign entry, log page, menu keys, qualification, debrief.
 * ================================================================================================================================================== */
static logbook_t LB;
static screen_t *log_screen(int welcome) {
    screen_t *s = new_screen();
    if (!s) return NULL;
    logbook_zero(&LB);
    s->log = &LB;
    s->log_welcome = (uint8_t)welcome;
    return s;
}
static void to_logdisk(screen_t *s) { key(s, 'x'); steps(s, 3); }
/* the text line of the scene at a scanline (NULL when none) */
static const game_text_t *row_line(screen_t *s, int row) {
    const game_scene_t *c = fill(s);
    for (int i = 0; i < c->text_count; i++) if (c->text[i].row == row) return &c->text[i];
    return NULL;
}
static const game_text_t *last_row_line(screen_t *s, int row) {   /* the typed callsign is drawn after the blank line of the callsign record, at the same position */
    const game_scene_t *c = fill(s);
    const game_text_t *r = NULL;
    for (int i = 0; i < c->text_count; i++) if (c->text[i].row == row) r = &c->text[i];
    return r;
}
static int run_to(screen_t *s, screen_state_t st, int max) { for (int i = 0; i < max; i++) { if (s->state == st) return i; steps(s, 1); } return s->state == st ? max : -1; }

TEST(without_a_log_import_the_log_disk_screen_goes_straight_to_the_menu) {
    screen_t *s = log_screen(0);
    ASSERT_TRUE(s != NULL);
    to_logdisk(s); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);
    del(s);
}

TEST(the_typed_key_ring_before_the_callsign_entry_holds_10_presses_and_drops_the_rest) {   /* 5.8, 10.5: the ring of 10; the screen queue keeps 31 elsewhere */
    screen_t *s = log_screen(1);
    ASSERT_TRUE(s != NULL);
    to_logdisk(s); steps(s, 15);
    ASSERT_EQ_INT(s->state, SCR_WELCOME);
    ASSERT_EQ_INT(s->name_open, 0);
    for (int i = 0; i < 14; i++) key(s, (int)('A' + i));
    ASSERT_EQ_INT(s->nq, 10);
    del(s);
}

TEST(the_credits_store_one_key_per_pass_and_the_second_press_of_a_pass_is_lost) {   /* 5.8 [V: followup-callsign-ring-slow] */
    screen_t *s = log_screen(1);
    ASSERT_TRUE(s != NULL);
    key(s, 'x'); key(s, 'y'); key(s, 'z');
    ASSERT_EQ_INT(s->nq, 1);
    steps(s, 1);
    del(s);
}

TEST(the_first_tour_welcome_holds_the_keys_until_the_callsign_entry_opens_and_return_saves) {
    screen_t *s = log_screen(1);
    ASSERT_TRUE(s != NULL);
    to_logdisk(s); steps(s, 15);
    ASSERT_EQ_INT(s->state, SCR_WELCOME);
    ASSERT_EQ_INT(s->mode, GAME_MODE_WELCOME);
    ASSERT_EQ_INT(s->welcome_first, 1);                          /* tours 0 */
    ASSERT_EQ_INT(fill(s)->palette_regime, 0);                   /* the menu palette, text only */
    key(s, 't'); key(s, 'E'); key(s, 'S'); key(s, 'T');
    steps(s, 3);
    ASSERT_EQ_INT(s->name_open, 0);                              /* the ring keeps them until the input mode starts (10.5) ... */
    ASSERT_EQ_INT(s->nq, 4);
    ASSERT_EQ_INT(logbook_get16(&LB, LB_TOURS), 0);
    int open = -1;
    for (int i = 1; i <= 600 && open < 0; i++) { steps(s, 1); if (s->name_open) open = i; }
    ASSERT_TRUE(open > 3);
    ASSERT_EQ_INT(logbook_get16(&LB, LB_TOURS), 1);              /* tours becomes 1 as soon as the prompt is shown (10.4) */
    steps(s, 2);
    ASSERT_EQ_INT(s->nq, 0);
    ASSERT_STR_EQ(s->name.cell, "");                             /* ... and drops them when it opens: the ring and the typed text are cleared, nothing is handed on (5.8 [V: followup-callsign-ring]) */
    key(s, 't'); key(s, 'E'); key(s, 'S'); key(s, 'T'); steps(s, 3);
    ASSERT_STR_EQ(s->name.cell, "TEST");                         /* upper case, there is no lower case */
    const game_text_t *t = last_row_line(s, 130);
    ASSERT_TRUE(t != NULL);
    ASSERT_EQ_INT(t->col, 4);                                    /* the entry line starts at x = 32 */
    ASSERT_EQ_INT(t->shown, 4);
    ASSERT_STR_EQ(t->text, "TEST");
    ASSERT_EQ_INT(LB.save_req, 0);
    btn(s, GAME_BTN_RETURN, 1); steps(s, 1);
    ASSERT_EQ_INT(LB.save_req, 1);                               /* save point 1 of 10.8 */
    ASSERT_EQ_INT(LB.dirty, 0);
    ASSERT_STR_EQ((const char *)LB.rec + LB_CALLSIGN, "TEST");
    ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);
    del(s);
}

TEST(an_empty_callsign_is_accepted_and_every_other_key_and_the_esc_requests_do_nothing_in_the_entry) {
    screen_t *s = log_screen(1);
    ASSERT_TRUE(s != NULL);
    to_logdisk(s);
    for (int i = 0; i < 600 && !s->name_open; i++) steps(s, 1);
    ASSERT_EQ_INT(s->name_open, 1);
    key(s, 200); key(s, 0x20AC); key(s, '-'); key(s, '.'); key(s, 27); key(s, 8); key(s, 8);   /* a non-ASCII key, signs, ESC, BACKSPACE at an empty field */
    btn(s, GAME_BTN_KEYPAD, 5); btn(s, GAME_BTN_FKEY, 1); btn(s, GAME_BTN_ESC, 1); btn(s, GAME_BTN_SHIFT_ESC, 1); btn(s, GAME_BTN_ANY, 1);
    steps(s, 4);
    ASSERT_EQ_INT(s->state, SCR_WELCOME);                        /* ESC is stored as a request that nothing consumes during the entry (10.5) */
    ASSERT_EQ_INT(s->name.pos, 0); ASSERT_EQ_INT(s->name.ncell, 0);
    for (int i = 0; i < LOGBOOK_SIZE; i++) if (i != LB_TOURS + 1) ASSERT_EQ_INT(LB.rec[i], 0);
    btn(s, GAME_BTN_RETURN, 1); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);                     /* an empty field is accepted */
    ASSERT_EQ_INT(LB.save_req, 1);
    ASSERT_EQ_INT(LB.rec[LB_CALLSIGN], 0);
    del(s);
}

TEST(the_entry_clips_at_the_records_length_and_the_overrun_reaches_the_counters_as_the_table_says) {
    screen_t *s = log_screen(1);
    ASSERT_TRUE(s != NULL);
    to_logdisk(s);
    for (int i = 0; i < 600 && !s->name_open; i++) steps(s, 1);
    const char *typed = "abcdefghijk abcdefghijklmno";           /* 11 letters, a SPACE, 15 letters (the capture overrun-a) */
    for (const char *p = typed; *p; p++) key(s, *p);
    steps(s, 2);
    ASSERT_EQ_INT(s->name.pos, 0x30); ASSERT_EQ_INT(s->name.rem, 0x3A);
    ASSERT_STR_EQ((const char *)LB.rec + LB_CALLSIGN, "ABCDEFGHIJK");
    ASSERT_EQ_INT(logbook_get16(&LB, LB_AIM120_FIRED), 0x2041);
    ASSERT_EQ_INT(s->name.ncell, 27);
    const game_text_t *t = last_row_line(s, 130);
    ASSERT_TRUE(t != NULL && t->shown == 27);
    btn(s, GAME_BTN_RETURN, 1); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);
    ASSERT_EQ_INT(logbook_get16(&LB, LB_MISSILE_HITS), 0x4A4B);  /* saved with the overwritten counters, as the original does */
    del(s);
}

TEST(the_automatic_end_at_a_negative_count_saves_and_leaves_like_return_instead_of_locking) {
    screen_t *s = log_screen(1);
    ASSERT_TRUE(s != NULL);
    to_logdisk(s);
    for (int i = 0; i < 600 && !s->name_open; i++) steps(s, 1);
    const char *typed = "ABCDEFGHIJ ";                           /* a SPACE as the 11th: the end comes at the 43rd character */
    for (const char *p = typed; *p; p++) key(s, *p);
    steps(s, 1);
    for (int i = 0; i < 31; i++) { key(s, 'A' + i % 26); if (i % 10 == 9) steps(s, 1); }   /* characters 12 to 42 (the queue holds 31 events) */
    steps(s, 3);
    ASSERT_EQ_INT(s->state, SCR_WELCOME);
    ASSERT_EQ_INT(LB.save_req, 0);
    key(s, 'Z'); steps(s, 1);                                    /* the 43rd */
    ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);
    ASSERT_EQ_INT(LB.save_req, 1);
    ASSERT_EQ_INT(LB.rec[LB_CALLSIGN + 10], ' ');
    del(s);
}

TEST(the_continuing_welcome_starts_19_steps_after_the_credits_key_types_over_105_blanks_patches_the_callsign_and_counts_the_tour_without_saving) {   /* 10.4 */
    screen_t *s = log_screen(1);
    ASSERT_TRUE(s != NULL);
    logbook_set16(&LB, LB_TOURS, 2);
    memcpy(LB.rec + LB_CALLSIGN, "ACE", 4);
    key(s, 'x');                                                 /* the credits key, taken once the credits text has started (2 steps) */
    while (s->state != SCR_WELCOME) steps(s, 1);                 /* the step that took the key is step 0 */
    ASSERT_EQ_INT(s->welcome_first, 0);
    ASSERT_EQ_INT(logbook_get16(&LB, LB_TOURS), 3);              /* plus one, both kinds of welcome */
    ASSERT_EQ_INT(s->mode, GAME_MODE_CREDITS);                   /* the credits state's own wait (87 blanks = 14 steps) */
    steps(s, 13);
    ASSERT_EQ_INT(s->mode, GAME_MODE_WELCOME);
    ASSERT_EQ_INT(lines(s), 0);
    int start = 14;                                              /* the steps counted since the key's step */
    while (lines(s) == 0 && start < 40) { steps(s, 1); start++; }
    ASSERT_TRUE(start >= 18 && start <= 20);                     /* the text starts 116 blanks = 19 steps after the key */
    int menu = -1, typed = -1;
    for (int i = 1; i <= 900 && menu < 0; i++) {
        steps(s, 1);
        const game_text_t *t = row_line(s, 36);
        if (t && t->shown == t->length && t->length == 39) { if (typed < 0) typed = i; ASSERT_TRUE(strncmp(t->text + 8, "ACE", 3) == 0); }   /* index 8, the callsign */
        if (s->state == SCR_MENU_BUILD) menu = i;
    }
    ASSERT_TRUE(typed > 0);
    ASSERT_TRUE(menu - 0 >= 14 && menu <= 25);                   /* 105 blanks typed = 17 steps, then the hold: the menu build follows */
    steps(s, 30);
    ASSERT_EQ_INT(s->state, SCR_MENU);
    ASSERT_EQ_INT(LB.save_req, 0); ASSERT_EQ_INT(LB.dirty, 0);   /* not saved by the welcome itself */
    del(s);
}

TEST(the_first_key_during_the_continuing_welcome_ends_it_at_once_and_is_consumed_never_a_menu_choice) {   /* 10.4 [V] */
    screen_t *s = log_screen(1);
    ASSERT_TRUE(s != NULL);
    logbook_set16(&LB, LB_TOURS, 2);
    key(s, 'x'); while (s->state != SCR_WELCOME) steps(s, 1);
    steps(s, 25);                                                /* the text is typing */
    ASSERT_EQ_INT(s->state, SCR_WELCOME);
    key(s, '3'); steps(s, 2);
    ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);                     /* ended at once */
    steps(s, 40);
    ASSERT_EQ_INT(s->state, SCR_MENU);
    ASSERT_EQ_INT(s->mode_byte, 0);                              /* the key chose nothing: the menu was not built when it was handled */
    ASSERT_EQ_INT(s->nq, 0);                                     /* and it is not left in the queue */
    del(s);
}

/* A dirty stack under screen_key: it left the character of a button event uninitialised, and the callsign entry reads it (10.5: the keypad, the F-keys and ANY change nothing). */
static void dirty_stack(void) { volatile char junk[4096]; for (int i = 0; i < 4096; i++) junk[i] = 'A'; (void)junk[7]; }

TEST(the_buttons_never_type_into_the_callsign_whatever_the_stack_held) {
    screen_t *s = log_screen(1);
    ASSERT_TRUE(s != NULL);
    to_logdisk(s);
    for (int i = 0; i < 600 && !s->name_open; i++) steps(s, 1);
    ASSERT_EQ_INT(s->name_open, 1);
    const int codes[] = { GAME_BTN_KEYPAD, GAME_BTN_KEYPAD, GAME_BTN_KEYPAD, GAME_BTN_KEYPAD, GAME_BTN_FKEY, GAME_BTN_ANY, GAME_BTN_PAUSE };
    const int vals[] = { 5, 9, 0, GAME_KP_ENTER, 1, 1, 1 };
    for (size_t i = 0; i < sizeof codes / sizeof *codes; i++) { dirty_stack(); btn(s, codes[i], vals[i]); steps(s, 1); }
    ASSERT_EQ_INT(s->state, SCR_WELCOME);
    ASSERT_EQ_INT(s->name.pos, 0); ASSERT_EQ_INT(s->name.ncell, 0);
    ASSERT_EQ_INT(LB.rec[LB_CALLSIGN], 0);
    key(s, 'q'); steps(s, 1);
    ASSERT_STR_EQ(s->name.cell, "Q");                            /* a real key still types */
    del(s);
}

TEST(a_prompt_of_the_log_disk_check_with_an_imported_log_goes_to_the_menu_with_no_log) {   /* 10.3: the state is still not 0 at the second check */
    for (int r = 1; r <= 3; r++) {
        screen_t *s = log_screen(1);
        ASSERT_TRUE(s != NULL);
        logbook_set16(&LB, LB_TOURS, 3);                         /* a used record: the welcome would be the continuing one */
        LB.rec[LB_LAST_MISSION] = 4;
        screen_set_logdisk_result(s, r);
        to_logdisk(s); steps(s, 4);
        ASSERT_EQ_INT(s->state, SCR_LOGDISK);                    /* the prompt */
        btn(s, GAME_BTN_RETURN, 1); steps(s, 1);
        ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);                 /* the menu, not the welcome */
        ASSERT_EQ_INT(logbook_get16(&LB, LB_TOURS), 3);          /* the welcome did not count the tour */
        steps(s, 10);
        ASSERT_EQ_INT(s->state, SCR_MENU);
        key(s, '8'); steps(s, 3);
        ASSERT_EQ_INT(s->state, SCR_MENU);                       /* no log: the log screen and the next-mission key do nothing */
        key(s, '7'); steps(s, 3);
        ASSERT_EQ_INT(s->state, SCR_MENU);
        ASSERT_EQ_INT(LB.save_req, 0);                           /* no saving */
        del(s);
    }
}

TEST(the_log_page_is_18_lines_with_the_figures_patched_into_the_typed_text) {
    screen_t *s = log_screen(0);
    ASSERT_TRUE(s != NULL);
    logbook_set16(&LB, LB_STARTED, 4); logbook_set16(&LB, LB_COMPLETED, 3);
    logbook_set16(&LB, LB_CANNON_FIRED, 18); logbook_set16(&LB, LB_AIM120_FIRED, 9); logbook_set16(&LB, LB_AIM120_HITS, 6);
    logbook_set16(&LB, LB_AIM9_FIRED, 1); logbook_set16(&LB, LB_AIM9_HITS, 1); logbook_set16(&LB, LB_CRASHES, 3);
    LB.rec[10] = 0x06; LB.rec[11] = 0x49;
    to_menu(s); steps(s, 20);
    key(s, '8'); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_LOGSCREEN);
    ASSERT_EQ_INT(s->mode, GAME_MODE_LOG);
    ASSERT_EQ_INT(s->mode_byte, 0);                              /* the pseudo-mode 0xFE is cleared at once */
    ASSERT_EQ_INT(fill(s)->palette_regime, 0);
    steps(s, 3);
    for (int i = 0; i < 400 && !screen_text_finished(s); i++) steps(s, 1);
    ASSERT_TRUE(screen_text_finished(s));
    const game_scene_t *c = fill(s);
    ASSERT_EQ_INT(c->text_count, 13);                            /* 18 lines, five of them blank */
    for (int i = 0; i < c->text_count; i++) ASSERT_EQ_INT(c->text[i].shown, c->text[i].length);
    const game_text_t *t;
    ASSERT_TRUE((t = row_line(s, 22)) != NULL && strncmp(t->text + 36, "  4", 3) == 0);     /* line 2: missions started */
    ASSERT_TRUE((t = row_line(s, 33)) != NULL && strncmp(t->text + 36, "  3", 3) == 0);     /* line 3: completed */
    ASSERT_TRUE((t = row_line(s, 66)) != NULL && strncmp(t->text + 22, "   18", 5) == 0 && strncmp(t->text + 31, "   0", 4) == 0);   /* line 6: the cannon */
    ASSERT_TRUE((t = row_line(s, 77)) != NULL && strncmp(t->text + 22, "    9", 5) == 0 && strncmp(t->text + 31, "   6", 4) == 0);
    ASSERT_TRUE((t = row_line(s, 88)) != NULL && strncmp(t->text + 22, "    1", 5) == 0 && strncmp(t->text + 31, "   1", 4) == 0);
    ASSERT_TRUE((t = row_line(s, 110)) != NULL && strncmp(t->text + 22, "   0", 4) == 0 && strncmp(t->text + 32, "26", 2) == 0);   /* line 10: 0 hours 26 minutes */
    ASSERT_TRUE((t = row_line(s, 121)) != NULL && strncmp(t->text + 22, "   3", 4) == 0);   /* line 11: the crashes */
    ASSERT_TRUE((t = row_line(s, 132)) != NULL && strncmp(t->text + 22, "   0", 4) == 0);   /* line 12: hits from missiles */
    ASSERT_TRUE(row_line(s, 11) == NULL && row_line(s, 44) == NULL && row_line(s, 99) == NULL);   /* the blank lines */
    ASSERT_TRUE((t = row_line(s, 187)) != NULL && t->length == 21);                            /* line 17: the last key line */
    del(s);
}

TEST(update_writes_only_when_the_dirty_flag_is_set_reset_zeroes_without_writing_and_esc_returns) {
    screen_t *s = log_screen(0);
    ASSERT_TRUE(s != NULL);
    logbook_set16(&LB, LB_TOURS, 5); logbook_set16(&LB, LB_QUALIFIED, 1); logbook_set16(&LB, LB_CRASHES, 7);
    to_menu(s); steps(s, 20);
    key(s, '8'); steps(s, 3);
    key(s, '1'); steps(s, 1);                                    /* UPDATE with the dirty flag clear: nothing */
    ASSERT_EQ_INT(LB.save_req, 0);
    ASSERT_EQ_INT(s->state, SCR_LOGSCREEN);
    logbook_add(&LB, LB_CRASHES, 1);                             /* a counter: the dirty flag */
    key(s, '1'); steps(s, 1);
    ASSERT_EQ_INT(LB.save_req, 1); ASSERT_EQ_INT(LB.dirty, 0);   /* save point 4 of 10.8 */
    ASSERT_EQ_INT(s->state, SCR_LOGSCREEN);                      /* tours 5: no welcome */
    LB.save_req = 0;
    key(s, GAME_KEY_SHIFT2); steps(s, 1);                        /* RESET */
    for (int i = 0; i < LOGBOOK_SIZE; i++) ASSERT_EQ_INT(LB.rec[i], 0);
    ASSERT_EQ_INT(LB.dirty, 1); ASSERT_EQ_INT(LB.save_req, 0);   /* marked dirty, no disk write */
    steps(s, 1);
    for (int i = 0; i < 400 && !screen_text_finished(s); i++) steps(s, 1);
    const game_text_t *t = row_line(s, 121);                     /* the page was posted again with the zeros */
    ASSERT_TRUE(t != NULL && strncmp(t->text + 22, "   0", 4) == 0);
    btn(s, GAME_BTN_ESC, 1); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);
    del(s);
}

TEST(update_after_a_reset_runs_the_first_tour_welcome_again) {
    screen_t *s = log_screen(1);
    ASSERT_TRUE(s != NULL);
    logbook_set16(&LB, LB_TOURS, 2);
    to_logdisk(s);
    ASSERT_TRUE(run_to(s, SCR_MENU_BUILD, 900) >= 0);
    steps(s, 30);
    key(s, '8'); steps(s, 3);
    key(s, GAME_KEY_SHIFT2); steps(s, 1);
    key(s, '1'); steps(s, 1);                                    /* UPDATE: the zero record is written, tours <= 1: the welcome runs again at once */
    ASSERT_EQ_INT(LB.save_req, 1);
    ASSERT_EQ_INT(s->state, SCR_WELCOME);
    ASSERT_EQ_INT(s->welcome_first, 1);
    for (int i = 0; i < 600 && !s->name_open; i++) steps(s, 1);
    ASSERT_EQ_INT(logbook_get16(&LB, LB_TOURS), 1);
    del(s);
}

TEST(the_menu_keys_7_8_and_f1_to_f6_follow_the_log) {
    screen_t *s = log_screen(0);
    ASSERT_TRUE(s != NULL);
    to_menu(s); steps(s, 20);
    key(s, '7'); steps(s, 2);                                    /* an empty log: only the click */
    ASSERT_EQ_INT(s->state, SCR_MENU);
    for (int f = 1; f <= 6; f++) { btn(s, GAME_BTN_FKEY, f); steps(s, 1); ASSERT_EQ_INT(s->state, SCR_MENU); }
    btn(s, GAME_BTN_FKEY, 7); steps(s, 1); ASSERT_EQ_INT(s->state, SCR_MENU);
    logbook_set16(&LB, LB_QUALIFIED, 1);
    key(s, '7'); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_ECHO); ASSERT_EQ_INT(s->mode_byte, 3);        /* byte +6 = 0: the first mission */
    ASSERT_EQ_INT(lines(s), 1);                                               /* the echo line of the item (id 0x6B) */
    steps(s, 12); ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);                    /* no mission module yet: the menu */
    steps(s, 30);
    LB.rec[LB_LAST_MISSION] = 5;
    key(s, '7'); steps(s, 1);
    ASSERT_EQ_INT(s->mode_byte, 6);
    steps(s, 12); steps(s, 30);
    btn(s, GAME_BTN_FKEY, 1); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_ECHO); ASSERT_EQ_INT(s->mode_byte, 3);
    ASSERT_EQ_INT(lines(s), 0);                                               /* a function-key mission shows no echo line */
    steps(s, 12); steps(s, 30);
    btn(s, GAME_BTN_FKEY, 2); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_MENU);                                        /* F2 needs the byte of mission 4's predecessor */
    LB.rec[LB_COMPLETIONS + 3] = 1;
    btn(s, GAME_BTN_FKEY, 2); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_ECHO); ASSERT_EQ_INT(s->mode_byte, 4);
    steps(s, 12); steps(s, 30);
    key(s, '8'); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_LOGSCREEN);
    del(s);
}

TEST(the_mission_list_of_item_6_adds_a_line_for_each_available_mission) {
    screen_t *s = log_screen(0);
    ASSERT_TRUE(s != NULL);
    LB.rec[LB_COMPLETIONS + 3] = LB.rec[LB_COMPLETIONS + 4] = LB.rec[LB_COMPLETIONS + 5] = 1;   /* the shipped record: F1 to F4 */
    to_menu(s); steps(s, 20);
    key(s, '6'); steps(s, 10);
    ASSERT_EQ_INT(s->state, SCR_LIST);
    ASSERT_EQ_INT(lines(s), 5);                                  /* the header, the lines of F2 to F4 and the hint */
    del(s);
}

TEST(the_unlock_byte_and_the_progress_choose_the_sublist_and_the_entries_of_the_placement) {
    screen_t *s = log_screen(0);
    ASSERT_TRUE(s != NULL);
    s->mode_byte = 5;                                            /* row 4: the list whose sub-list 1 is a single carrier, the cap is 5 */
    ASSERT_EQ_INT(screen_start_flight(s), 5);                    /* the carrier, three aircraft and the entry past the threshold test */
    LB.live_level = 5;                                           /* V:4A95F: the entry with the progress threshold 5 is now placed */
    ASSERT_EQ_INT(screen_start_flight(s), 6);
    LB.live_level = 0;
    LB.rec[LB_COMPLETIONS + 5] = 1;                              /* V:1D024 + 0x12 + mode: the sub-list 1 */
    ASSERT_EQ_INT(screen_start_flight(s), 1);
    LB.rec[LB_COMPLETIONS + 5] = 9;                              /* a foreign file's out-of-range byte is limited to 3 where it indexes */
    ASSERT_EQ_INT(screen_start_flight(s), 5);
    LB.rec[LB_COMPLETIONS + 5] = 1;
    s->log = NULL;                                               /* no log: selector 0, progress 0 */
    ASSERT_EQ_INT(screen_start_flight(s), 5);
    del(s);
}

static int qual_handler(void *user, uint8_t mode) { return mode == 9 ? screen_qualification_start((screen_t *)user) : 0; }

TEST(the_qualification_shows_the_briefing_once_places_the_carrier_start_and_ends_in_the_flight) {
    screen_t *s = log_screen(0);
    ASSERT_TRUE(s != NULL);
    screen_set_mode_handler(s, qual_handler, s);
    to_menu(s); steps(s, 20);
    key(s, '5'); steps(s, 7);
    ASSERT_EQ_INT(s->mode_byte, 9);
    ASSERT_EQ_INT(s->state, SCR_BLACK);                          /* the dispatch ran at +7 */
    ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT);
    ASSERT_EQ_INT(s->out_place, 1);                              /* the player is placed (game.c) */
    ASSERT_EQ_INT(s->location, 3); ASSERT_EQ_INT(s->aircraft, 0x11);
    ASSERT_EQ_INT(s->allowance, 3);
    ASSERT_EQ_INT(fill(s)->fade_target, 0);
    s->out_place = 0;
    int brief = run_to(s, SCR_BRIEF, 60);
    ASSERT_EQ_INT(brief, 16);                                    /* 9.2 (milestone 7) [V: flow7-qual-dispatch]: the black stage of 6 steps (a scene build of 40 blanks), the fade-in of 7 and the briefing 3 steps after its end: 23 steps from the key with the echo's 7 */
    ASSERT_EQ_INT(fill(s)->fade_target, 15);
    steps(s, 6);
    ASSERT_TRUE(lines(s) > 0);                                   /* the briefing types out over the deck picture */
    ASSERT_EQ_INT(row_line(s, 6) != NULL, 1);
    key(s, 'x'); steps(s, 1);                                    /* any key leaves it */
    ASSERT_EQ_INT(s->state, SCR_APPROACH);
    ASSERT_TRUE(run_to(s, SCR_FLIGHT, 80) > 0);
    ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT);
    del(s);
}

TEST(the_qualification_success_writes_the_log_at_the_debrief_and_the_restart_skips_the_briefing) {
    screen_t *s = log_screen(0);
    ASSERT_TRUE(s != NULL);
    screen_set_mode_handler(s, qual_handler, s);
    to_menu(s); steps(s, 20);
    key(s, '5'); steps(s, 7 + 16 + 6);
    key(s, 'x');
    ASSERT_TRUE(run_to(s, SCR_FLIGHT, 80) > 0);
    s->out_place = 0;
    ASSERT_EQ_INT(logbook_get16(&LB, LB_QUALIFIED), 0);
    screen_mission_end(s, 0xFF, 0);                              /* the game's success test fired (game.c) */
    steps(s, 6);
    ASSERT_EQ_INT(logbook_get16(&LB, LB_QUALIFIED), 0);          /* the debrief state has not run yet */
    ASSERT_EQ_INT(LB.save_req, 0);
    steps(s, 2);
    ASSERT_EQ_INT(logbook_get16(&LB, LB_QUALIFIED), 1);          /* 0x131F4 sets the first word and calls the log writer */
    ASSERT_EQ_INT(LB.save_req, 1);
    ASSERT_EQ_INT(LB.dirty, 0);
    ASSERT_EQ_INT(logbook_get16(&LB, LB_COMPLETED), 0);          /* the qualification does not count as a mission completed */
    int brief_again = 0, restarted = 0;
    for (int i = 0; i < 700 && s->state != SCR_FLIGHT; i++) {
        steps(s, 1);
        if (s->state == SCR_ESC_FADE) restarted = 1;
        if (s->state == SCR_BRIEF) brief_again = 1;
        s->out_place = 0; s->out_leave = 0;
        if (restarted && s->state == SCR_APPROACH) break;
    }
    ASSERT_EQ_INT(restarted, 1);
    ASSERT_EQ_INT(brief_again, 0);                               /* V:4A8E3 = 1 after the restart: no briefing */
    ASSERT_EQ_INT(s->state, SCR_APPROACH);
    del(s);
}

/* 9.2 (milestone 7, Task 2) [V: flow7-qual-restart, blanks from the ESC]: the qualification's ESC restart has the black stage of 6 steps (a scene build of 40 blanks, not the 73 of a free-flight restart), the fade-in of 7,
 * the map stage of 6 (0x127C8 with the prompt skipped), the verdict state's 3 and then the approach and the countdown, the notice and the wait: the flight state 53 steps after the ESC (334 blanks; the stage rule gives 51 to 53). */
TEST(the_qualification_esc_restart_runs_black_6_fade_in_7_map_6_verdict_3_and_reaches_the_flight_53_steps_after_the_esc) {
    screen_t *s = log_screen(0);
    ASSERT_TRUE(s != NULL);
    screen_set_mode_handler(s, qual_handler, s);
    to_menu(s); steps(s, 20);
    key(s, '5'); steps(s, 7 + 16 + 6);
    key(s, 'x');
    ASSERT_TRUE(run_to(s, SCR_FLIGHT, 80) > 0);
    s->out_place = 0;
    btn(s, GAME_BTN_ESC, 1);
    int first[40]; for (int i = 0; i < 40; i++) first[i] = -1;
    int n = 0, last = -1;
    int left = 0;
    for (; n < 200 && !(left && s->state == SCR_FLIGHT); n++) {
        steps(s, 1);
        s->out_place = 0; s->out_leave = 0;
        if (s->state != SCR_FLIGHT) left = 1;
        if ((int)s->state != last) { if (first[s->state] < 0) first[s->state] = n + 1; last = (int)s->state; }
    }
    ASSERT_EQ_INT(s->state, SCR_FLIGHT);
    ASSERT_EQ_INT(first[SCR_FADE_IN] - first[SCR_BLACK], 6);        /* the black stage: a scene build of 40 blanks */
    ASSERT_EQ_INT(first[SCR_MAP] - first[SCR_FADE_IN], 7);          /* the fade-in of 44 blanks */
    ASSERT_EQ_INT(first[SCR_QUAL_VERDICT] - first[SCR_MAP], 6);     /* the map stage */
    ASSERT_EQ_INT(first[SCR_APPROACH] - first[SCR_QUAL_VERDICT], 3);   /* the verdict state to the approach */
    ASSERT_EQ_INT(first[SCR_BLACK], 13);                            /* the 13 steps to the scene state, as for the free-flight restart (5.11) */
    ASSERT_EQ_INT(first[SCR_NOTICE] - first[SCR_APPROACH], 7);      /* the approach and the countdown, one pass each */
    ASSERT_EQ_INT(n, 53);                                           /* the flight state: 334 blanks, 51 to 53 steps by the stage rule */
    del(s);
}

TEST(a_bolter_a_ditch_and_a_wrong_mode_write_nothing_to_the_log_and_a_mission_completion_runs_the_bookkeeping) {
    screen_t *s = log_screen(0);
    ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    s->mode_byte = 4;
    screen_mission_end(s, 0xFF, 0);                              /* a mission success text: the log is not touched (the completion code is 0xFC) */
    steps(s, 12);
    ASSERT_EQ_INT(logbook_get16(&LB, LB_QUALIFIED), 0); ASSERT_EQ_INT(LB.save_req, 0);
    del(s);
    s = log_screen(0);
    to_flight(s, 4, 1);
    s->mode_byte = 4;
    LB.live_level = 1;
    screen_mission_end(s, 0xFC, 0);
    steps(s, 12);
    ASSERT_EQ_INT(logbook_get16(&LB, LB_COMPLETED), 1);
    ASSERT_EQ_INT(LB.rec[LB_COMPLETIONS + 4], 1); ASSERT_EQ_INT(LB.rec[LB_LAST_MISSION], 4);
    ASSERT_EQ_INT(LB.save_req, 1);
    ASSERT_EQ_INT(logbook_get16(&LB, LB_QUALIFIED), 0);
    del(s);
}

/* ---- the game API: the container, the bad blobs, scene.log_dirty ------------------------------------------------------------------------------------ */

static game_t *new_game(void) {
    build_assets();
    char err[200] = "";
    game_t *g = game_create(&SLD, err, sizeof err);
    if (!g) printf("  (game_create: %s)\n", err);
    return g;
}
static void blob_of(uint8_t *out, const uint8_t *rec, size_t n) { memcpy(out, "ILOG", 4); out[4] = 1; memcpy(out + 5, rec, n); }

TEST(export_then_import_round_trips_through_the_game_and_a_new_player_exports_the_zero_record) {
    game_t *g = new_game();
    ASSERT_TRUE(g != NULL);
    uint8_t buf[GAME_LOG_FILE_SIZE], rec[78], again[GAME_LOG_FILE_SIZE];
    ASSERT_EQ_INT(game_log_export(g, buf, sizeof buf), 83);
    ASSERT_TRUE(memcmp(buf, "ILOG", 4) == 0 && buf[4] == 1);
    for (int i = 5; i < 83; i++) ASSERT_EQ_INT(buf[i], 0);       /* the zero record of a new player */
    for (int i = 0; i < 78; i++) rec[i] = (uint8_t)(i * 3 + 7);
    blob_of(buf, rec, 78);
    ASSERT_EQ_INT(game_log_import(g, buf, 83), GAME_LOG_OK);
    ASSERT_EQ_INT(game_log_export(g, again, sizeof again), 83);
    ASSERT_TRUE(memcmp(buf, again, 83) == 0);
    ASSERT_EQ_INT(game_log_export(g, again, 82), -1);            /* a buffer that is too small: nothing */
    ASSERT_EQ_INT(game_log_export(NULL, again, sizeof again), -1);
    game_destroy(g);
}

TEST(a_short_long_wrong_magic_or_wrong_version_blob_is_refused_and_the_log_stays_empty) {   /* Review Focus 3 */
    game_t *g = new_game();
    ASSERT_TRUE(g != NULL);
    uint8_t rec[78], buf[100], out[GAME_LOG_FILE_SIZE];
    memset(rec, 0x55, sizeof rec);
    blob_of(buf, rec, 78);
    ASSERT_EQ_INT(game_log_import(g, buf, 83), GAME_LOG_OK);     /* a good log first, so that a refusal is seen to clear it */
    ASSERT_EQ_INT(game_log_import(g, buf, 4), GAME_LOG_BAD_SHORT);
    game_log_export(g, out, sizeof out);
    for (int i = 5; i < 83; i++) ASSERT_EQ_INT(out[i], 0);
    ASSERT_EQ_INT(game_log_import(g, buf, 83), GAME_LOG_OK);
    ASSERT_EQ_INT(game_log_import(g, buf, 0), GAME_LOG_BAD_SHORT);   /* an empty file */
    game_log_export(g, out, sizeof out);
    for (int i = 5; i < 83; i++) ASSERT_EQ_INT(out[i], 0);
    blob_of(buf, rec, 78); buf[0] = 'X';
    game_log_import(g, buf, 83);                                 /* a good log, then a wrong magic */
    ASSERT_EQ_INT(game_log_import(g, buf, 83), GAME_LOG_BAD_MAGIC);
    game_log_export(g, out, sizeof out);
    for (int i = 5; i < 83; i++) ASSERT_EQ_INT(out[i], 0);
    blob_of(buf, rec, 78); buf[4] = 2;
    ASSERT_EQ_INT(game_log_import(g, buf, 83), GAME_LOG_BAD_VERSION);
    game_log_export(g, out, sizeof out);
    for (int i = 5; i < 83; i++) ASSERT_EQ_INT(out[i], 0);
    blob_of(buf, rec, 78);
    buf[83] = 9; buf[99] = 9;
    ASSERT_EQ_INT(game_log_import(g, buf, 100), GAME_LOG_OK);    /* a longer container: the first 78 record bytes */
    game_log_export(g, out, sizeof out);
    ASSERT_TRUE(memcmp(out + 5, rec, 78) == 0);
    blob_of(buf, rec, 78);
    ASSERT_EQ_INT(game_log_import(g, buf, 5 + 10), GAME_LOG_OK); /* a shorter one: the leading bytes, zeros after them */
    game_log_export(g, out, sizeof out);
    ASSERT_TRUE(memcmp(out + 5, rec, 10) == 0);
    for (int i = 15; i < 83; i++) ASSERT_EQ_INT(out[i], 0);
    ASSERT_EQ_INT(game_log_import(g, NULL, 0), GAME_LOG_OK);     /* no file: the zero record, not an error */
    game_log_export(g, out, sizeof out);
    for (int i = 5; i < 83; i++) ASSERT_EQ_INT(out[i], 0);
    ASSERT_EQ_INT(game_log_import(NULL, buf, 83), GAME_LOG_BAD_SHORT);
    game_destroy(g);
}

TEST(a_new_player_through_the_game_saves_at_return_and_the_scene_reports_it_until_the_frontend_acks) {
    game_t *g = new_game();
    ASSERT_TRUE(g != NULL);
    ASSERT_EQ_INT(game_log_import(g, NULL, 0), GAME_LOG_OK);
    g_step(g, 3);
    ASSERT_EQ_INT(g_scene(g)->log_dirty, 0);
    g_key(g, 'a'); g_step(g, 16);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_WELCOME);
    g_step(g, 600);                                              /* the entry opens (the presses made before it would be dropped, 5.8) */
    g_key(g, 'p'); g_key(g, 'i'); g_key(g, 'l'); g_key(g, 'o'); g_key(g, 't');
    g_step(g, 3);
    ASSERT_EQ_INT(g_scene(g)->log_dirty, 0);
    int found = 0;
    for (int i = 0; i < GS.text_count; i++) if (GS.text[i].row == 130 && !strcmp(GS.text[i].text, "PILOT")) found = 1;
    ASSERT_EQ_INT(found, 1);
    g_btn(g, GAME_BTN_RETURN, 1); g_step(g, 1);
    ASSERT_EQ_INT(g_scene(g)->log_dirty, 1);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_MENU);
    uint8_t buf[GAME_LOG_FILE_SIZE];
    ASSERT_EQ_INT(game_log_export(g, buf, sizeof buf), 83);
    ASSERT_TRUE(memcmp(buf + 5 + LB_CALLSIGN, "PILOT", 6) == 0);
    ASSERT_EQ_INT(buf[5 + LB_TOURS + 1], 1);
    g_step(g, 3);
    ASSERT_EQ_INT(g_scene(g)->log_dirty, 1);                     /* still pending: the frontend has not taken it */
    game_log_ack(g);
    ASSERT_EQ_INT(g_scene(g)->log_dirty, 0);
    game_destroy(g);
}

/* ---- the qualification and the flight clock on the extracted assets ---------------------------------------------------------------------------------- */

static int g_text_row(const game_scene_t *c, int row) { for (int i = 0; i < c->text_count; i++) if (c->text[i].row == row && c->text[i].shown > 0) return 1; return 0; }

/* The menu to the running flight of the qualification: key 5, the briefing (left by a key), the launch notice and the wait. Returns the number of steps the briefing was up. */
static game_t *real_qual(int *brief_steps) {
    char err[200] = "";
    game_t *g = game_create(&RLD, err, sizeof err);
    if (!g) { printf("  (game_create: %s)\n", err); return NULL; }
    g_step(g, 3); g_key(g, 'x'); g_step(g, 8); g_btn(g, GAME_BTN_RETURN, 1); g_step(g, 8);
    g_key(g, '5');
    int brief = 0;
    aircraft_t *ac = game_debug_aircraft(g);
    for (int i = 0; i < 700; i++) {
        g_step(g, 1);
        const game_scene_t *c = g_scene(g);
        if (c->mode == GAME_MODE_FLIGHT && g_text_row(c, 6) && c->fade_target == 15 && ac && ac->f.steps == 0) {   /* the briefing's first line sits at scanline 6 */
            brief++;
            if (brief == 12) g_key(g, 'x');
        }
        if (ac && ac->f.steps > 6) break;
    }
    if (brief_steps) *brief_steps = brief;
    return g;
}

TEST(real_assets_the_qualification_start_places_the_player_on_the_catapult_with_the_briefing_first) {
    if (!have_real()) { printf("skipped "); return; }
    int brief = 0;
    game_t *g = real_qual(&brief);
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(brief >= 12);                                    /* the briefing was up until the key */
    aircraft_t *ac = game_debug_aircraft(g);
    ASSERT_EQ_INT(ac->pos[0], 295185408); ASSERT_EQ_INT(ac->pos[1], 30472); ASSERT_EQ_INT(ac->pos[2], 274309120);   /* note 08 9.2 step 2 */
    ASSERT_EQ_INT(ac->f.w2 & 0x00FF, 0x82);                      /* W2 = 0x0082 on the catapult (the bit 0x80: wheels down) */
    ASSERT_EQ_INT(ac->f.cls, 0x11);
    ASSERT_EQ_INT(ac->f.v4a95e, 9);
    ASSERT_EQ_INT(game_crash_allowance(g), 3);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_FLIGHT);
    game_destroy(g);
}

static unsigned blob_qualified(game_t *g) { uint8_t b[GAME_LOG_FILE_SIZE]; game_log_export(g, b, sizeof b); return ((unsigned)b[5 + LB_QUALIFIED] << 8) | b[5 + LB_QUALIFIED + 1]; }
static unsigned blob_word(game_t *g, int off) { uint8_t b[GAME_LOG_FILE_SIZE]; game_log_export(g, b, sizeof b); return ((unsigned)b[5 + off] << 8) | b[5 + off + 1]; }

/* The captured stop on the deck: the tail point in the corridor, 75,220 units astern of the carrier's origin, the aircraft stopped (note 08 9.3 table). */
static void stage_stop(aircraft_t *ac, unsigned w2_bits) {
    flight_set_position(ac, 295185408, 30472, 274127404);
    ac->f.w2 = (uint16_t)(0x0082u | w2_bits);
    ac->f.speed = ac->f.speed_target = 0;
}

TEST(real_assets_a_stopped_arrested_landing_ends_the_flight_with_the_debrief_the_log_and_the_restart) {
    if (!have_real()) { printf("skipped "); return; }
    int brief = 0;
    game_t *g = real_qual(&brief);
    ASSERT_TRUE(g != NULL && brief > 0);
    aircraft_t *ac = game_debug_aircraft(g);
    g_step(g, 4);
    ASSERT_EQ_INT(blob_qualified(g), 0);
    stage_stop(ac, 0xC000u);                                     /* hook down and engaged: the corridor latch of note 05 17.11 */
    int saved = -1;
    for (int i = 1; i <= 30 && saved < 0; i++) {
        g_step(g, 1);
        if (g_scene(g)->log_dirty) saved = i;
        else ASSERT_EQ_INT(blob_qualified(g), 0);
    }
    ASSERT_EQ_INT(saved, 8);                                     /* the success in the step of the stop, the debrief state 7 steps later (42 blanks, 5.13) */
    ASSERT_EQ_INT(blob_qualified(g), 1);                         /* the first word of the record */
    ASSERT_EQ_INT(blob_word(g, LB_COMPLETED), 0);                /* the qualification does not count as a mission */
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_FLIGHT);
    game_log_ack(g);
    /* the outcome text (238 blanks = 38 steps), the hold, then the restart of 5.11 by itself: the player is on the catapult again, without the briefing */
    int again = 0, brief_again = 0;
    for (int i = 0; i < 900 && !again; i++) {
        g_step(g, 1);
        const game_scene_t *c = g_scene(g);
        if (c->mode == GAME_MODE_FLIGHT && g_text_row(c, 6) && c->fade_target == 15 && ac->f.steps == 0) brief_again = 1;
        if (c->mode == GAME_MODE_FLIGHT && ac->f.steps > 6 && i > 60) again = 1;
    }
    ASSERT_EQ_INT(again, 1);
    ASSERT_EQ_INT(brief_again, 0);
    ASSERT_EQ_INT(ac->pos[0], 295185408); ASSERT_EQ_INT(ac->pos[1], 30472); ASSERT_EQ_INT(ac->pos[2], 274309120);
    ASSERT_EQ_INT(game_crash_allowance(g), 3);
    ASSERT_EQ_INT(g_scene(g)->log_dirty, 0);                     /* one save only */
    game_destroy(g);
}

TEST(real_assets_a_stop_without_the_wire_a_bolter_or_a_ditching_writes_nothing) {
    if (!have_real()) { printf("skipped "); return; }
    game_t *g = real_qual(NULL);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    g_step(g, 4);
    stage_stop(ac, 0x4000u);                                     /* stopped on the deck with the hook up: the corridor bit alone does nothing */
    for (int i = 0; i < 60; i++) { g_step(g, 1); ASSERT_EQ_INT(g_scene(g)->log_dirty, 0); }
    ASSERT_EQ_INT(blob_qualified(g), 0);
    stage_stop(ac, 0x0000u);                                     /* the bolter's rollout: the deck, no wire, the hook up */
    for (int i = 0; i < 60; i++) { g_step(g, 1); ASSERT_EQ_INT(g_scene(g)->log_dirty, 0); }
    flight_set_position(ac, 295185408 + 3000000, 750, 274309120 + 3000000);   /* the sea, far from the deck: stopped, hook down, wheels down (a ditching) */
    ac->f.w2 = 0x8082u; ac->f.speed = ac->f.speed_target = 0;
    for (int i = 0; i < 60; i++) { g_step(g, 1); ASSERT_EQ_INT(g_scene(g)->log_dirty, 0); }
    ASSERT_EQ_INT(blob_qualified(g), 0);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_FLIGHT);           /* no consequence: the flight goes on */
    game_destroy(g);
}

TEST(real_assets_a_crash_counts_at_0x46_and_at_the_handover_word_and_closes_the_airborne_interval) {
    if (!have_real()) { printf("skipped "); return; }
    game_t *g = real_qual(NULL);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    g_step(g, 4);
    ASSERT_EQ_INT(blob_word(g, LB_CRASHES), 0);
    ac->pos[1] = 30472 + 60000; ac->f.vel[1] = -40000;           /* a hard touchdown: the vertical speed above 0x240 with the wheels flagged down */
    ac->f.w2 &= (uint16_t)~0x0080u;                              /* airborne */
    int n = 0;
    for (int i = 0; i < 40 && n == 0; i++) { g_step(g, 1); n = (int)blob_word(g, LB_CRASHES); }
    ASSERT_EQ_INT(n, 1);
    g_step(g, 3);
    ASSERT_EQ_INT(blob_word(g, LB_HANDOVERS), 1);                /* the hand-over of the flight state (collisions on) */
    ASSERT_EQ_INT(game_crash_count(g), 1);
    ASSERT_EQ_INT(blob_word(g, LB_CRASHES), 1);                  /* one crash, one count */
    game_destroy(g);
}

TEST(real_assets_the_flight_clock_counts_the_airborne_steps_only) {
    if (!have_real()) { printf("skipped "); return; }
    game_t *g = real_qual(NULL);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    uint8_t buf[GAME_LOG_FILE_SIZE];
    g_step(g, 40);                                               /* on the deck: ground time, not counted */
    ac->pos[1] += 40000;                                         /* the aircraft is well above the deck with the wheels flagged down: the next step is a lift-off */
    g_step(g, 1);
    ASSERT_TRUE((ac->f.w2 & 0x0080u) == 0);
    for (int i = 0; i < 8 * 20; i++) { ac->pos[1] = 5000000; g_step(g, 1); }   /* 20 seconds in the air (held up: the staged aircraft would sink) */
    g_btn(g, GAME_BTN_PAUSE, 1); g_step(g, 8 * 30); g_btn(g, GAME_BTN_PAUSE, 1);   /* a pause of 30 s is not counted */
    g_btn(g, GAME_BTN_SHIFT_ESC, 1); g_step(g, 20);              /* the flight is left */
    game_log_export(g, buf, sizeof buf);
    const unsigned secs = ((unsigned)buf[5 + 8] << 24) | ((unsigned)buf[5 + 9] << 16) | ((unsigned)buf[5 + 10] << 8) | buf[5 + 11];
    ASSERT_EQ_INT((int)secs, 20);                                /* the lift-off step and the 160 held steps: 161 airborne steps = 20 s (10.7) */
    game_destroy(g);
}


TEST(real_assets_the_debrief_save_carries_the_flight_time_booked_when_the_request_3_is_taken) {
    if (!have_real()) { printf("skipped "); return; }
    game_t *g = real_qual(NULL);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    g_step(g, 4);
    const uint8_t deck_b4 = ac->f.b4;                            /* the lift-off clears the carrier bits of B4; the stop below restores them */
    ac->pos[1] += 40000;
    g_step(g, 1);                                                /* the lift-off */
    for (int i = 0; i < 8 * 10; i++) { ac->pos[1] = 5000000; g_step(g, 1); }   /* 10 s in the air */
    ASSERT_EQ_INT(blob_word(g, LB_SECONDS + 2), 0);              /* nothing is booked before the end */
    ac->f.b4 = deck_b4;
    stage_stop(ac, 0xC000u);                                     /* the stop with the hook engaged (no touchdown event: the flight stays airborne in the clock's sense) */
    int saved = -1;
    for (int i = 1; i <= 30 && saved < 0; i++) { g_step(g, 1); if (g_scene(g)->log_dirty) saved = i; }
    ASSERT_EQ_INT(saved, 8);
    ASSERT_EQ_INT(blob_qualified(g), 1);
    ASSERT_EQ_INT(blob_word(g, LB_SECONDS), 0);
    ASSERT_EQ_INT(blob_word(g, LB_SECONDS + 2), 10);             /* 81 held steps + the 4 to the dispatcher = 10 s, in the very blob the debrief saved */
    game_destroy(g);
}


/* ---- the mission texts (10.9, 13.2) and the mission counters (14.2, 14.3) ---------------------------------------------------------------------- */

TEST(the_date_line_of_the_intro_block_gets_the_day_and_the_month_from_the_hours_of_flight) {
    logbook_t lb;
    logbook_zero(&lb);
    static const char months[37] = "AAAAAAAAABBBBBBBBBCCCCCCCCCDDDDDDDDD";   /* invented names, 9 characters each */
    lb.months = months;
    char t[40];
    filler(t, 28, '.');
    logbook_set16(&lb, LB_SECONDS, 0); logbook_set16(&lb, LB_SECONDS + 2, 0);
    ASSERT_EQ_INT(logbook_patch_line(&lb, 0x5B, 3, t, 28), 1);
    ASSERT_TRUE(strncmp(t + 10, "AAAAAAAAA", 9) == 0);                  /* nine characters ending at index 18 */
    ASSERT_TRUE(t[9] == '.' && t[19] == '.');
    ASSERT_TRUE(strncmp(t + 20, " 1", 2) == 0);                         /* hours 0: day 1, leading zero blanked */
    ASSERT_TRUE(t[22] == '.');
    const uint32_t secs = 37u * 3600u + 59u * 60u;                       /* 37 hours: the month index 1, the day (37 AND 31) + 1 = 6 */
    logbook_set16(&lb, LB_SECONDS, secs >> 16); logbook_set16(&lb, LB_SECONDS + 2, secs & 0xFFFF);
    filler(t, 28, '.'); logbook_patch_line(&lb, 0x5B, 3, t, 28);
    ASSERT_TRUE(strncmp(t + 10, "BBBBBBBBB", 9) == 0); ASSERT_TRUE(strncmp(t + 20, " 6", 2) == 0);
    const uint32_t s2 = 31u * 3600u;                                     /* 31 hours: day 32 limited to 30 */
    logbook_set16(&lb, LB_SECONDS, s2 >> 16); logbook_set16(&lb, LB_SECONDS + 2, s2 & 0xFFFF);
    filler(t, 28, '.'); logbook_patch_line(&lb, 0x5B, 3, t, 28);
    ASSERT_TRUE(strncmp(t + 20, "30", 2) == 0);
    const uint32_t s3 = 200u * 3600u;                                    /* 200 hours: the clamp keeps the last name; (200 AND 31) + 1 = 9 */
    logbook_set16(&lb, LB_SECONDS, s3 >> 16); logbook_set16(&lb, LB_SECONDS + 2, s3 & 0xFFFF);
    filler(t, 28, '.'); logbook_patch_line(&lb, 0x5B, 3, t, 28);
    ASSERT_TRUE(strncmp(t + 10, "DDDDDDDDD", 9) == 0); ASSERT_TRUE(strncmp(t + 20, " 9", 2) == 0);
    ASSERT_EQ_INT(logbook_patch_line(&lb, 0x5B, 0, t, 28), 0);          /* only the date line */
}

TEST(without_a_month_table_the_day_alone_is_patched) {
    logbook_t lb;
    logbook_zero(&lb);
    char t[40];
    filler(t, 28, '.');
    ASSERT_EQ_INT(logbook_patch_line(&lb, 0x5B, 3, t, 28), 1);
    ASSERT_TRUE(strncmp(t + 10, "..........", 10) == 0);               /* no month table set: the fallback blanks (months.bin carries the names since extractor 16; the test above pins the table) */
    ASSERT_TRUE(strncmp(t + 20, " 1", 2) == 0);
}

TEST(the_bearing_of_the_vector_notice_is_three_digits_with_leading_zeros_at_indices_13_to_15) {
    logbook_t lb;
    logbook_zero(&lb);
    char t[40];
    filler(t, 26, '.');
    lb.bogey_deg = 7;
    ASSERT_EQ_INT(logbook_patch_line(&lb, 0x58, 0, t, 26), 1);
    ASSERT_TRUE(strncmp(t + 13, "007", 3) == 0); ASSERT_TRUE(t[12] == '.' && t[16] == '.');
    lb.bogey_deg = 359; logbook_patch_line(&lb, 0x58, 0, t, 26); ASSERT_TRUE(strncmp(t + 13, "359", 3) == 0);
    lb.bogey_deg = 0; logbook_patch_line(&lb, 0x58, 0, t, 26); ASSERT_TRUE(strncmp(t + 13, "000", 3) == 0);
    ASSERT_EQ_INT(logbook_patch_line(&lb, 0x58, 1, t, 26), 0);
}

TEST(the_progress_counter_is_zero_at_every_program_start_and_the_level_lags_one_completion) {
    logbook_t lb;
    uint8_t rec[78];
    memset(rec, 0, sizeof rec);
    rec[2] = 0; rec[3] = 2;                                              /* the shipped record holds level 2 */
    logbook_load_record(&lb, rec, 78);
    ASSERT_EQ_INT(lb.live_level, 0);                                     /* 14.3: V:4A95F reads 0 in every captured session */
    logbook_mission_complete(&lb, 8);                                    /* +0x02 takes the live level (0) BEFORE its increment */
    ASSERT_EQ_INT(logbook_get16(&lb, LB_LEVEL), 0); ASSERT_EQ_INT(lb.live_level, 1);
    logbook_mission_complete(&lb, 8);
    ASSERT_EQ_INT(logbook_get16(&lb, LB_LEVEL), 1); ASSERT_EQ_INT(lb.live_level, 2);
    ASSERT_EQ_INT(lb.rec[LB_LAST_COUNT], 1);                             /* the completion count of the mode before its increment */
    ASSERT_EQ_INT(lb.rec[LB_COMPLETIONS + 8], 2);
}

TEST(the_missions_started_counter_counts_modes_3_to_8_only) {
    logbook_t lb;
    logbook_zero(&lb);
    for (int m = 0; m <= 9; m++) logbook_mission_started(&lb, m);
    ASSERT_EQ_INT(logbook_get16(&lb, LB_STARTED), 6);
}

int main(void) {
    RUN(the_record_is_78_bytes_with_big_endian_words_at_the_notes_offsets);
    RUN(a_new_player_is_the_zero_record_and_tours_zero_selects_the_first_tour);
    RUN(the_container_is_magic_version_and_the_78_bytes_and_round_trips);
    RUN(the_bad_containers_are_refused_with_their_codes);
    RUN(a_short_inner_record_gives_the_leading_bytes_and_a_long_one_the_first_78);
    RUN(a_save_point_clears_the_dirty_flag_and_asks_for_the_blob);
    RUN(the_success_test_needs_all_five_conditions_and_nothing_else);
    RUN(menu_key_7_and_the_function_keys_need_the_first_word_and_the_availability_byte);
    RUN(the_sublist_selector_is_the_completion_byte_of_the_mode_limited_to_3);
    RUN(the_completion_bookkeeping_follows_0x134a0);
    RUN(flight_time_is_airborne_steps_over_8_with_the_bounds_of_the_note);
    RUN(the_figures_are_patched_right_to_left_with_leading_zeros_blanked_at_the_notes_indices);
    RUN(a_field_above_its_width_shows_its_low_digits_and_hours_are_a_16_bit_quotient);
    RUN(a_line_too_short_for_its_figure_is_left_alone);
    RUN(the_continuing_welcome_copies_the_callsign_from_text_index_8_up_to_24_or_the_first_zero);
    RUN(name_entry_row_1_to_10_stores_the_callsign_and_counts_pos_and_rem);
    RUN(name_entry_row_11_overwrites_rem_with_the_character_and_stores_at_0x28);
    RUN(name_entry_row_12_space_overwrites_pos_and_reaches_0x3e_any_other_character_reaches_no_byte);
    RUN(name_entry_rows_13_on_with_a_space_as_the_12th_store_at_0x3f_to_0x4d_and_the_28th_is_dropped);
    RUN(the_captured_overrun_a_run_gives_the_callsign_and_the_counters_the_note_lists);
    RUN(a_letter_as_the_12th_character_changes_nothing_beyond_the_11_stored_the_25_letter_run);
    RUN(the_automatic_end_comes_after_11_plus_the_11th_character_code_the_43_and_86_character_runs);
    RUN(backspace_erases_downward_clears_the_bytes_and_does_nothing_at_an_empty_field);
    RUN(name_entry_accepts_letters_digits_and_space_upper_cased_and_ignores_every_other_key);
    RUN(only_41_cells_are_kept_for_the_screen_and_the_record_is_never_written_past_byte_78);
    RUN(without_a_log_import_the_log_disk_screen_goes_straight_to_the_menu);
    RUN(the_typed_key_ring_before_the_callsign_entry_holds_10_presses_and_drops_the_rest);
    RUN(the_credits_store_one_key_per_pass_and_the_second_press_of_a_pass_is_lost);
    RUN(the_first_tour_welcome_holds_the_keys_until_the_callsign_entry_opens_and_return_saves);
    RUN(an_empty_callsign_is_accepted_and_every_other_key_and_the_esc_requests_do_nothing_in_the_entry);
    RUN(the_entry_clips_at_the_records_length_and_the_overrun_reaches_the_counters_as_the_table_says);
    RUN(the_automatic_end_at_a_negative_count_saves_and_leaves_like_return_instead_of_locking);
    RUN(the_continuing_welcome_starts_19_steps_after_the_credits_key_types_over_105_blanks_patches_the_callsign_and_counts_the_tour_without_saving);
    RUN(the_first_key_during_the_continuing_welcome_ends_it_at_once_and_is_consumed_never_a_menu_choice);
    RUN(the_buttons_never_type_into_the_callsign_whatever_the_stack_held);
    RUN(a_prompt_of_the_log_disk_check_with_an_imported_log_goes_to_the_menu_with_no_log);
    RUN(the_log_page_is_18_lines_with_the_figures_patched_into_the_typed_text);
    RUN(update_writes_only_when_the_dirty_flag_is_set_reset_zeroes_without_writing_and_esc_returns);
    RUN(update_after_a_reset_runs_the_first_tour_welcome_again);
    RUN(the_menu_keys_7_8_and_f1_to_f6_follow_the_log);
    RUN(the_mission_list_of_item_6_adds_a_line_for_each_available_mission);
    RUN(the_unlock_byte_and_the_progress_choose_the_sublist_and_the_entries_of_the_placement);
    RUN(the_qualification_shows_the_briefing_once_places_the_carrier_start_and_ends_in_the_flight);
    RUN(the_qualification_success_writes_the_log_at_the_debrief_and_the_restart_skips_the_briefing);
    RUN(the_qualification_esc_restart_runs_black_6_fade_in_7_map_6_verdict_3_and_reaches_the_flight_53_steps_after_the_esc);
    RUN(a_bolter_a_ditch_and_a_wrong_mode_write_nothing_to_the_log_and_a_mission_completion_runs_the_bookkeeping);
    RUN(export_then_import_round_trips_through_the_game_and_a_new_player_exports_the_zero_record);
    RUN(a_short_long_wrong_magic_or_wrong_version_blob_is_refused_and_the_log_stays_empty);
    RUN(a_new_player_through_the_game_saves_at_return_and_the_scene_reports_it_until_the_frontend_acks);
    RUN(real_assets_the_qualification_start_places_the_player_on_the_catapult_with_the_briefing_first);
    RUN(real_assets_a_stopped_arrested_landing_ends_the_flight_with_the_debrief_the_log_and_the_restart);
    RUN(real_assets_a_stop_without_the_wire_a_bolter_or_a_ditching_writes_nothing);
    RUN(real_assets_a_crash_counts_at_0x46_and_at_the_handover_word_and_closes_the_airborne_interval);
    RUN(real_assets_the_flight_clock_counts_the_airborne_steps_only);
    RUN(real_assets_the_debrief_save_carries_the_flight_time_booked_when_the_request_3_is_taken);
    RUN(the_date_line_of_the_intro_block_gets_the_day_and_the_month_from_the_hours_of_flight);
    RUN(without_a_month_table_the_day_alone_is_patched);
    RUN(the_bearing_of_the_vector_notice_is_three_digits_with_leading_zeros_at_indices_13_to_15);
    RUN(the_progress_counter_is_zero_at_every_program_start_and_the_level_lags_one_completion);
    RUN(the_missions_started_counter_counts_modes_3_to_8_only);
    TEST_MAIN_END();
}
