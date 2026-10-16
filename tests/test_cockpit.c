/* tests/test_cockpit.c - the tick generator, the warning arbiter, the code word and latch, the frame counter and the raw cockpit block
 * (src/core/cockpit.c), against docs/re/06-cockpit-hud.md "Warning line" ("Ticks and blinking", "Durations of alerts", "Arbiter").
 * The warning table is invented: see build_table (durations and classes chosen for the tests, no game text; the disk's column is in the extracted assets only). */
#include "test.h"
#include "../src/core/cockpit.h"

static cockpit_t ck;
static aircraft_t ac;

/* Flag bytes of an invented warning table: class in bits 7..6 (2 alert, 1 information), duration in bits 5..0 (note 06, "Durations of alerts").
 * Only the entries these tests post carry a class (the class follows the code the rule posts: the nine relations, the posted codes) and
 * a duration chosen here (different from the disk's, spread over the same branches: a long and a short timed alert, a zero duration, timed
 * information); every other entry is a plain information entry with duration 0. The tests derive their expectations from these durations. */
static void build_table(uint8_t t[COCKPIT_WARN_ENTRIES][COCKPIT_WARN_SIZE]) {
    static const struct { int e, cls, d; } used[] = {
        { 1, 2, 0 }, { 2, 2, 7 }, { 3, 2, 0 }, { 4, 2, 0 }, { 6, 2, 3 }, { 7, 2, 0 }, { 9, 2, 0 }, { 10, 2, 0 }, { 14, 2, 5 },
        { 15, 2, 0 }, { 16, 2, 10 }, { 17, 2, 0 }, { 18, 2, 9 }, { 19, 2, 0 }, { 20, 2, 0 }, { 21, 2, 0 }, { 36, 2, 11 }, { 37, 2, 0 },
        { 5, 1, 9 }, { 13, 1, 7 }, { 31, 1, 6 }, { 33, 1, 3 }, { 34, 1, 4 }, { 35, 1, 7 } };
    memset(t, 0, COCKPIT_WARN_ENTRIES * COCKPIT_WARN_SIZE);
    for (int i = 0; i < COCKPIT_WARN_ENTRIES; i++) t[i][0] = 0x40;
    for (size_t k = 0; k < sizeof used / sizeof *used; k++) t[used[k].e][0] = (uint8_t)((used[k].cls << 6) | used[k].d);
    t[20][27] = 1;                      /* the one sound flag */
}

static uint8_t table[COCKPIT_WARN_ENTRIES][COCKPIT_WARN_SIZE];

static void fresh(int gen_seed) {
    build_table(table);
    memset(&ac, 0, sizeof ac);
    ac.f.v4a984 = 0x0840;
    cockpit_init(&ck, &table[0][0]);
    cockpit_mission_start(&ck);
    if (gen_seed) ck.gen = (uint8_t)gen_seed;
}

/* One pass: generator, the inputs, arbiter, n. */
static void pass(int32_t ev, uint8_t th) {
    ac.f.v4ac08 = ev; ck.threat = th;
    cockpit_generator(&ck);
    cockpit_arbiter(&ck, &ac, 0);
    cockpit_end_pass(&ck, 0);
}

TEST(tick_sequence_from_n0_zero) {
    fresh(0);
    static const int cnt[8] = { 5, 4, 3, 2, 1, 8, 7, 6 };
    static const int tick[8] = { 0, 0x06, 0, 0x04, 0, 0x86, 0, 0x04 };
    for (int i = 0; i < 8; i++) {
        cockpit_generator(&ck);
        ASSERT_EQ_INT(ck.gen, cnt[i]);
        ASSERT_EQ_INT(ck.tick, tick[i]);
        ck.tick = 0;                      /* the arbiter's clear */
    }
}

TEST(slow_tick_every_fourth_pass_and_fast_every_second_and_n_does_not_drive_it) {
    fresh(1);
    int slow[32], fast[32];
    for (int k = 0; k < 32; k++) {
        ck.n = (uint32_t)(k * 7);        /* the generator does not read n */
        cockpit_generator(&ck);
        slow[k] = (ck.tick & 2) != 0; fast[k] = (ck.tick & 4) != 0;
        ck.tick = 0;
    }
    for (int k = 0; k < 32; k++) {
        ASSERT_EQ_INT(slow[k], k % 4 == 0);
        ASSERT_EQ_INT(fast[k], k % 2 == 0);
    }
}

TEST(generator_runs_on_pause_passes_and_is_not_reseeded) {
    fresh(0);
    cockpit_generator(&ck); cockpit_arbiter(&ck, &ac, 1); cockpit_end_pass(&ck, 1);   /* paused passes: the generator advances, n does not */
    cockpit_generator(&ck); cockpit_arbiter(&ck, &ac, 1); cockpit_end_pass(&ck, 1);
    ASSERT_EQ_INT(ck.n, 0);
    ASSERT_EQ_INT(ck.gen, 4);            /* 5, 4 */
    cockpit_generator(&ck);
    ASSERT_EQ_INT(ck.gen, 3);            /* free running, no reseed */
}

TEST(mission_start_clears_n_and_seeds_the_phase_for_every_n0_rule) {
    fresh(0);
    for (int i = 0; i < 20; i++) pass(0, 0);
    ASSERT_EQ_INT(ck.n, 20);
    ASSERT_EQ_INT(ck.n_draw, 19);
    cockpit_mission_start(&ck);
    ASSERT_EQ_INT(ck.n, 0);
    cockpit_generator(&ck);
    ASSERT_EQ_INT(ck.gen, 5);
    ASSERT_EQ_INT(ck.hud_on, 1);
}

/* ---- priorities ------------------------------------------------------------------------------------------ */

typedef struct { uint32_t ev; uint8_t th; uint16_t code; } rel_t;
/* The nine relations in order (note 06, item 4). */
static const rel_t REL[9] = {
    { 0x00, 0x10, 0x8010 }, { 0x00, 0x20, 0x8012 }, { 0x40, 0x00, 0xC004 }, { 0x80, 0x00, 0xC001 }, { 0x02, 0x00, 0x8002 },
    { 0x00, 0x08, 0x8024 }, { 0x00, 0x04, 0x800E }, { 0x01, 0x00, 0x0C09 }, { 0x00, 0x02, 0x400D } };

TEST(each_of_the_nine_conditions_alone_picks_its_code) {
    for (int i = 0; i < 9; i++) {
        fresh(0);
        pass((int32_t)REL[i].ev, REL[i].th);
        ASSERT_EQ_INT(ac.f.v4ab98, REL[i].code);
        ASSERT_EQ_INT(ck.last_idx, REL[i].code & 0xFF);
        ASSERT_EQ_INT(ck.dcount >= 0, 1);
    }
}

TEST(each_condition_beats_all_the_lower_ones) {
    for (int i = 0; i < 9; i++) {
        fresh(0);
        uint32_t ev = 0; uint8_t th = 0;
        for (int j = i; j < 9; j++) { ev |= REL[j].ev; th |= REL[j].th; }   /* this one and everything below it */
        pass((int32_t)ev, th);
        ASSERT_EQ_INT(ac.f.v4ab98, REL[i].code);
    }
}

TEST(a_higher_relation_arriving_replaces_the_lower_one_and_the_old_line_stays_until_a_tick) {
    fresh(8);
    pass(0x01, 0);                         /* fuel low, plain: shown at once */
    ASSERT_EQ_INT(ck.shown, 9);
    pass(0x01, 0x10);                      /* an IR missile: the alert installs; its text waits for a tick of the slow kind */
    ASSERT_EQ_INT(ac.f.v4ab98, 0x8010);
    ASSERT_EQ_INT(ck.dcount, 10);
    ASSERT_EQ_INT(ck.shown, 9);            /* the old line stays (this pass has no slow tick: counter 6 after the decrement is a fast one) */
}

TEST(all_clear_blanks_the_line_and_forgets_the_index) {
    fresh(8);
    pass(0x01, 0);
    ASSERT_EQ_INT(ac.f.v4ab98, 0x0C09);
    pass(0, 0);
    ASSERT_EQ_INT(ac.f.v4ab98, 0);
    ASSERT_EQ_INT(ck.shown, 0);
    ASSERT_EQ_INT(ck.last_idx, 0);
    ASSERT_EQ_INT(ac.f.v4a984 & 0x80, 0);
}

TEST(all_clear_does_not_blank_while_bit_2_of_the_state_word_is_set) {
    fresh(8);
    pass(0x01, 0);
    ac.f.v4a984 |= 4;
    pass(0, 0);
    ASSERT_EQ_INT(ac.f.v4ab98, 0x0C09);
}

TEST(a_bit_the_list_does_not_know_leaves_the_code_word_alone) {
    fresh(8);
    pass(0x01, 0);
    pass(0x01 | 0x0400, 0);                // still fuel low
    ASSERT_EQ_INT(ac.f.v4ab98, 0x0C09);
    pass(0x0400, 0);                       // only an unknown bit: the code stays, the line stays
    ASSERT_EQ_INT(ac.f.v4ab98, 0x0C09);
    ASSERT_EQ_INT(ck.shown, 9);
    ASSERT_TRUE(ac.f.v4a984 & 0x80);       // bit 7 is set whenever one of the words is nonzero
}

TEST(a_latched_post_is_kept_over_the_list_and_replaces_an_arbiter_alert_at_once) {
    fresh(8);
    pass(0, 0x10);                         // alert 0x8010 from the arbiter
    ASSERT_EQ_INT(ac.f.v4ab98, 0x8010);
    cockpit_post(&ck, &ac, 0x4005);        // refuelled and rearmed
    ASSERT_EQ_INT(ac.f.v4a984 & 1, 1);
    pass(0, 0x10);                         // the condition holds: the post is not overridden
    ASSERT_EQ_INT(ac.f.v4ab98, 0x4005);
    ASSERT_EQ_INT(ck.shown, 5);            // timed information shows at once
    for (int i = 0; i < 4; i++) pass(0, 0x20);   // another, higher condition: still not overridden
    ASSERT_EQ_INT(ac.f.v4ab98, 0x4005);
}

TEST(a_posted_blank_code_unlatches) {
    fresh(8);
    cockpit_post(&ck, &ac, 0xD00A);
    pass(0, 0);
    ASSERT_EQ_INT(ac.f.v4a984 & 1, 1);
    cockpit_post(&ck, &ac, 0x4800);
    ASSERT_EQ_INT(ac.f.v4a984 & 1, 0);
}

/* ---- durations ---------------------------------------------------------------------------------------------- */

static int shown_passes_posted(uint16_t code, int phase) {
    fresh(phase);
    int count = 0;
    for (int k = 0; k < 90; k++) {
        if (k == 2) cockpit_post(&ck, &ac, code);
        cockpit_generator(&ck);
        cockpit_arbiter(&ck, &ac, 0);
        cockpit_end_pass(&ck, 0);
        if (k >= 2 && ck.shown == (code & 0xFF) && ac.f.v4ab98 != 0) count++;
    }
    return count;
}

TEST(timed_information_expiry_ranges_4d_to_4d_plus_3_for_d_7_and_d_0) {
    int lo8 = 99, hi8 = 0, lo0 = 99, hi0 = 0, seen8 = 0, seen0 = 0;
    for (int ph = 1; ph <= 8; ph++) {
        int a = shown_passes_posted(0x400D, ph), b = shown_passes_posted(0x401D, ph);
        if (a < lo8) lo8 = a;
        if (a > hi8) hi8 = a;
        if (b < lo0) lo0 = b;
        if (b > hi0) hi0 = b;
        seen8 |= 1 << (a - 28 < 0 || a - 28 > 7 ? 7 : a - 28);
        seen0 |= 1 << (b < 0 || b > 7 ? 7 : b);
    }
    ASSERT_EQ_INT(lo8, 28); ASSERT_EQ_INT(hi8, 31);   /* 4 D .. 4 D + 3 with D = 7 */
    ASSERT_EQ_INT(lo0, 0);  ASSERT_EQ_INT(hi0, 3);
    ASSERT_EQ_INT(seen8, 0x0F);            /* all four values occur */
    ASSERT_EQ_INT(seen0, 0x0F);
}

TEST(timed_information_is_blank_after_expiry_and_stays_blank_while_the_choice_holds) {
    fresh(8);
    for (int k = 0; k < 60; k++) pass(0, 0x02);       // friendly nearby: 0x400D, D = 7 (entry 13)
    ASSERT_EQ_INT(ac.f.v4ab98, 0);
    ASSERT_EQ_INT(ck.shown, 0);
    ASSERT_EQ_INT(ck.last_idx, 13);                   // the old index is kept: nothing reloads
    pass(0, 0);
    ASSERT_EQ_INT(ck.last_idx, 0);                    // all clear forgets it
    pass(0, 0x02);
    ASSERT_EQ_INT(ac.f.v4ab98, 0x400D);               // a later return restarts the message
    ASSERT_EQ_INT(ck.shown, 13);
}

TEST(alert_d7_fuel_critical_blinks_seven_cycles_then_is_steady) {
    fresh(8);
    int codes_8002 = 0, starts = 0, prev = -1;
    uint16_t last = 0;
    for (int k = 0; k < 90; k++) {
        pass(k >= 2 ? 0x02 : 0, 0);
        if (ac.f.v4ab98 == 0x8002) codes_8002++;
        if (ck.shown == 2 && prev != 2 && k >= 1 && k <= 58) starts++;   /* the eighth text phase is the expiry itself */
        prev = ck.shown; last = ac.f.v4ab98;
    }
    ASSERT_EQ_INT(codes_8002, 57);        // the code is held for 8 D + 1 passes (D = 7 here; the rule is verified in the original's harness)
    ASSERT_EQ_INT(starts, 7);
    ASSERT_EQ_INT(last, 0x0002);          // the bare index: steady plain text although the condition holds
    ASSERT_EQ_INT(ck.shown, 2);
    ASSERT_EQ_INT(ac.f.v4a984 & 1, 0);
}

TEST(alert_d10_text_four_passes_partner_four_passes_and_counter_loaded_at_install) {
    fresh(8);
    int seq[40];
    for (int k = 0; k < 40; k++) {
        pass(0, k >= 2 ? 0x10 : 0);
        seq[k] = ck.shown;
        if (k == 2) ASSERT_EQ_INT(ck.dcount, 10);
    }
    for (int k = 3; k < 7; k++) ASSERT_EQ_INT(seq[k], 16);
    for (int k = 7; k < 11; k++) ASSERT_EQ_INT(seq[k], 17);
    for (int k = 11; k < 15; k++) ASSERT_EQ_INT(seq[k], 16);
    ASSERT_EQ_INT(ck.snd_b, 0);
}

TEST(timed_alert_alternates_with_the_blank_entry_not_the_partner) {
    fresh(8);
    int seen_blank = 0, seen_text = 0, other = 0;
    for (int k = 0; k < 40; k++) {
        pass(k >= 2 ? 0x80 : 0, 0);        // stall: 0xC001, entry 1
        if (k > 3) { if (ck.shown == 0) seen_blank++; else if (ck.shown == 1) seen_text++; else other++; }
    }
    ASSERT_TRUE(seen_blank > 5 && seen_text > 5);
    ASSERT_EQ_INT(other, 0);
}

TEST(posted_fast_alert_blinks_two_on_two_off_and_never_expires) {
    fresh(8);
    int on = 0;
    for (int k = 0; k < 40; k++) {
        if (k == 2) cockpit_post(&ck, &ac, 0xD00A);
        cockpit_generator(&ck); cockpit_arbiter(&ck, &ac, 0); cockpit_end_pass(&ck, 0);
        if (k >= 2) ASSERT_EQ_INT(ac.f.v4ab98, 0xD00A);
        if (k >= 4 && k < 36) { if (ck.shown == 10) on++; else ASSERT_EQ_INT(ck.shown, 0); }
    }
    ASSERT_EQ_INT(on, 16);                // half of 32 passes
}

TEST(a_pause_turns_the_partner_tick_into_a_text_tick) {
    fresh(8);
    for (int k = 0; k < 10; k++) pass(k >= 2 ? 0x02 : 0, 0);
    for (int k = 10; k < 30; k++) {
        ac.f.v4ac08 = 0x02;
        cockpit_generator(&ck); cockpit_arbiter(&ck, &ac, 1); cockpit_end_pass(&ck, 1);
        if (k >= 12) ASSERT_EQ_INT(ck.shown, 2);   // steady text during the pause
    }
    ASSERT_EQ_INT(ck.n, 10);              // n did not advance
}

TEST(a_pause_keeps_n_and_its_low_three_bits) {
    /* note 06 "View kinds, settled" item 8: no handler clears n & 7 on a pause (the code at 0x1EDF8 is unreachable); n stands still and keeps its low bits */
    fresh(8);
    for (int k = 0; k < 13; k++) pass(0, 0);
    ASSERT_EQ_INT(ck.n, 13);
    for (int k = 0; k < 3; k++) { cockpit_generator(&ck); cockpit_arbiter(&ck, &ac, 1); cockpit_end_pass(&ck, 1); }
    ASSERT_EQ_INT(ck.n, 13);               // the paused passes leave it alone
    pass(0, 0);
    ASSERT_EQ_INT(ck.n, 14);
    ASSERT_EQ_INT(ck.n_draw, 13);
}

TEST(the_zoom_reset_puts_the_word_back_to_0x80) {
    cockpit_t ck; aircraft_t ac; game_hud_t h;
    memset(&ac, 0, sizeof ac);
    cockpit_init(&ck, NULL); cockpit_mission_start(&ck);
    cockpit_zoom_key(&ck, 1); cockpit_zoom_key(&ck, 1);
    cockpit_fill_hud(&ck, &ac, &h); ASSERT_EQ_INT(h.cp_zoom, 0x20);
    cockpit_zoom_reset(&ck);   /* 0x1F060: every head key and every view switch */
    cockpit_fill_hud(&ck, &ac, &h); ASSERT_EQ_INT(h.cp_zoom, 0x80);
}

TEST(the_zoom_keys_halve_to_0x20_and_double_to_0x80) {
    /* note 06 item 9: ] ] ] gives 0x40, 0x20, 0x20 and [ [ [ back 0x40, 0x80, 0x80; the HUD's magnification word follows */
    fresh(8);
    game_hud_t h;
    cockpit_fill_hud(&ck, &ac, &h); ASSERT_EQ_INT(h.cp_zoom, 0x80);
    static const int closer[3] = { 0x40, 0x20, 0x20 }, farther[3] = { 0x40, 0x80, 0x80 };
    for (int k = 0; k < 3; k++) { cockpit_zoom_key(&ck, 1); cockpit_fill_hud(&ck, &ac, &h); ASSERT_EQ_INT(h.cp_zoom, closer[k]); }
    for (int k = 0; k < 3; k++) { cockpit_zoom_key(&ck, 0); cockpit_fill_hud(&ck, &ac, &h); ASSERT_EQ_INT(h.cp_zoom, farther[k]); }
    cockpit_zoom_key(&ck, 1); cockpit_mission_start(&ck);
    cockpit_fill_hud(&ck, &ac, &h); ASSERT_EQ_INT(h.cp_zoom, 0x80);   /* a new mission plants 0x80 */
}

TEST(the_pass_counter_advances_on_paused_passes_and_publishes_the_pause_byte) {
    /* the draw schedule precedes the pause test: a paused pass runs the draws again (note 06 "Draw schedule"), n stands still */
    fresh(8);
    game_hud_t h;
    pass(0, 0); pass(0, 0);
    cockpit_fill_hud(&ck, &ac, &h);
    uint32_t p0 = h.cp_pass, n0 = h.cp_n;
    ASSERT_EQ_INT(h.cp_paused, 0);
    for (int k = 0; k < 3; k++) {
        cockpit_generator(&ck); cockpit_arbiter(&ck, &ac, 1); cockpit_end_pass(&ck, 1);
        cockpit_fill_hud(&ck, &ac, &h);
        ASSERT_EQ_INT(h.cp_pass, p0 + 1 + (uint32_t)k);
        ASSERT_EQ_INT(h.cp_paused, 1);
        ASSERT_EQ_INT(h.cp_n, n0);
    }
    pass(0, 0);
    cockpit_fill_hud(&ck, &ac, &h);
    ASSERT_EQ_INT(h.cp_pass, p0 + 4);
    ASSERT_EQ_INT(h.cp_paused, 0);
    ASSERT_EQ_INT(h.cp_n, n0 + 1);
    cockpit_mission_start(&ck);             /* the counter is never reset: a new mission is told apart from a repeat of the old scene */
    cockpit_fill_hud(&ck, &ac, &h);
    ASSERT_EQ_INT(h.cp_pass, p0 + 4);
}

TEST(a_change_of_entry_does_not_reset_the_text_flag) {
    /* Entry 2 (slow) is in its text phase; the condition switches to entry 16: the first slow tick of the new code shows the partner. */
    fresh(8);
    int k = 0;
    for (; k < 6; k++) pass(k >= 2 ? 0x02 : 0, 0);     // gen: 7 6 5 4 3 2; the slow tick at k = 3 showed the text
    ASSERT_EQ_INT(ck.shown, 2);
    ASSERT_EQ_INT(ck.text_shown, 1);
    for (; k < 7; k++) pass(0, 0x10);
    ASSERT_EQ_INT(ck.shown, 2);            /* the old line stays until the next slow tick */
    pass(0, 0x10); k++;
    /* the first slow tick after the switch is k = 7 (counter 8): the old flag is set, so the partner 17 is shown, not the text 16 */
    ASSERT_EQ_INT(ck.shown, 17);
    for (; k < 12; k++) pass(0, 0x10);
    ASSERT_EQ_INT(ck.shown, 16);           /* the slow tick four passes later shows the text */
}

TEST(sound_request_comes_from_the_installed_entry) {
    fresh(8);
    cockpit_post(&ck, &ac, 0x8014);       /* entry 20 carries the sound flag */
    pass(0, 0);
    ASSERT_EQ_INT(ck.snd_b, 1);
    cockpit_post(&ck, &ac, 0x8010);
    pass(0, 0);
    ASSERT_EQ_INT(ck.snd_b, 0);
}

/* ---- the published block ------------------------------------------------------------------------------------ */

TEST(fill_hud_copies_the_words_of_the_aircraft_and_the_state) {
    fresh(8);
    ac.f.cls = 0x11; ac.f.id = 0x0D; ac.f.b20 = 4; ac.f.gear = 0x80; ac.f.w2 = 0x0108; ac.f.w0 = 0x1848;
    ac.f.throttle = -32; ac.f.speed = 4726; ac.f.speed_target = 0x1300; ac.f.heading = 2240; ac.f.rate_p = 14;
    ac.f.fuel = 6399880; ac.f.v4a9fe = -73; ac.f.v4a982 = 2; ac.f.v4a8fe = 1;
    ac.pos[0] = 11; ac.pos[1] = 706604; ac.pos[2] = 33;
    for (int i = 0; i < 5; i++) pass(0, 0x04);
    game_hud_t h; memset(&h, 0, sizeof h);
    cockpit_fill_hud(&ck, &ac, &h);
    ASSERT_EQ_INT(h.cp_valid, 1);
    ASSERT_EQ_INT(h.cp_class, 0x11); ASSERT_EQ_INT(h.cp_sel, 0x0D);
    ASSERT_EQ_INT(h.cp_b03, 0x08); ASSERT_EQ_INT(h.cp_b20, 4); ASSERT_EQ_INT(h.cp_b7c, 0x80);
    ASSERT_EQ_INT(h.cp_throttle, 32); ASSERT_EQ_INT(h.cp_w00, 0x1848);
    ASSERT_EQ_INT(h.cp_speed_w, 4726); ASSERT_EQ_INT(h.cp_heading80, 2240); ASSERT_EQ_INT(h.cp_pitch_rate, 14);
    ASSERT_EQ_INT(h.cp_y, 706604); ASSERT_EQ_INT(h.cp_lon, 11); ASSERT_EQ_INT(h.cp_lat, 33);
    ASSERT_EQ_INT(h.cp_fuel, 6399880); ASSERT_EQ_INT(h.cp_gcorr, -73); ASSERT_EQ_INT(h.cp_gsign, 1);
    ASSERT_EQ_INT(h.cp_gear_lamp, 1); ASSERT_EQ_INT(h.cp_crashed, 0); ASSERT_EQ_INT(h.cp_threat, 4);
    ASSERT_EQ_INT(h.cp_n, 4);               /* five passes: the last saw n = 4 */
    ASSERT_EQ_INT(h.cp_hud_on, 1);
    ASSERT_EQ_INT(h.cp_msg_code, 0x800E);   /* hostile in range: entry 14 */
    ASSERT_EQ_INT(h.cp_stock, 0x24); ASSERT_EQ_INT(h.cp_rounds, 500);
    ac.f.v4a984 &= (uint16_t)~0x40;
    cockpit_fill_hud(&ck, &ac, &h);
    ASSERT_EQ_INT(h.cp_crashed, 1);
    cockpit_toggle_hud(&ck);
    cockpit_fill_hud(&ck, &ac, &h);
    ASSERT_EQ_INT(h.cp_hud_on, 0);
}

TEST(the_strip_is_blank_for_one_pass_after_an_entity_switch) {
    fresh(8);
    pass(0, 0);
    game_hud_t h;
    cockpit_fill_hud(&ck, &ac, &h); ASSERT_EQ_INT(h.cp_strip_off, 0);
    cockpit_entity_switch(&ck);
    pass(0, 0);
    cockpit_fill_hud(&ck, &ac, &h); ASSERT_EQ_INT(h.cp_strip_off, 1);
    pass(0, 0);
    cockpit_fill_hud(&ck, &ac, &h); ASSERT_EQ_INT(h.cp_strip_off, 0);
}

int main(void) {
    printf("test_cockpit:\n");
    RUN(tick_sequence_from_n0_zero);
    RUN(slow_tick_every_fourth_pass_and_fast_every_second_and_n_does_not_drive_it);
    RUN(generator_runs_on_pause_passes_and_is_not_reseeded);
    RUN(mission_start_clears_n_and_seeds_the_phase_for_every_n0_rule);
    RUN(each_of_the_nine_conditions_alone_picks_its_code);
    RUN(each_condition_beats_all_the_lower_ones);
    RUN(a_higher_relation_arriving_replaces_the_lower_one_and_the_old_line_stays_until_a_tick);
    RUN(all_clear_blanks_the_line_and_forgets_the_index);
    RUN(all_clear_does_not_blank_while_bit_2_of_the_state_word_is_set);
    RUN(a_bit_the_list_does_not_know_leaves_the_code_word_alone);
    RUN(a_latched_post_is_kept_over_the_list_and_replaces_an_arbiter_alert_at_once);
    RUN(a_posted_blank_code_unlatches);
    RUN(timed_information_expiry_ranges_4d_to_4d_plus_3_for_d_7_and_d_0);
    RUN(timed_information_is_blank_after_expiry_and_stays_blank_while_the_choice_holds);
    RUN(alert_d7_fuel_critical_blinks_seven_cycles_then_is_steady);
    RUN(alert_d10_text_four_passes_partner_four_passes_and_counter_loaded_at_install);
    RUN(timed_alert_alternates_with_the_blank_entry_not_the_partner);
    RUN(posted_fast_alert_blinks_two_on_two_off_and_never_expires);
    RUN(a_pause_turns_the_partner_tick_into_a_text_tick);
    RUN(a_pause_keeps_n_and_its_low_three_bits);
    RUN(the_zoom_keys_halve_to_0x20_and_double_to_0x80);
    RUN(the_zoom_reset_puts_the_word_back_to_0x80);
    RUN(the_pass_counter_advances_on_paused_passes_and_publishes_the_pause_byte);
    RUN(a_change_of_entry_does_not_reset_the_text_flag);
    RUN(sound_request_comes_from_the_installed_entry);
    RUN(fill_hud_copies_the_words_of_the_aircraft_and_the_state);
    RUN(the_strip_is_blank_for_one_pass_after_an_entity_switch);
    TEST_MAIN_END();
}
