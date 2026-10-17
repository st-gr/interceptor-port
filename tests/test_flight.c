/* tests/test_flight.c - the flight model against the worked examples of docs/re/05-flight-model.md (section 16, rig output
 * of the original code), and the review focus cases of the plan. Needs the extracted tables (assets/tables/flight_*.bin);
 * skipped (exit 0) when they are absent. Examples E5 and E6 also need assets/models (the carrier and aircraft vertices). */
#include "test.h"
#include "../src/core/flight.h"
#include "../src/core/world.h"
#include "../src/core/fx.h"
#include "../src/core/game.h"
#include "../src/core/flight_int.h"
#include "flight_examples.h"
#include "player_draw.h"

/* ---- loader: files below assets/ ------------------------------------------------------ */
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

static flight_tables_t tables;
static flight_env_t env;
static world_t *world;
static int have_models;

static aircraft_t ac;
static const flight_inputs_t NOKEY;

/* The player's slots are the draw's: these tests run the step without the display lists, so the stand-in draw (player_draw.h) refreshes them after every start, attitude change and step. */
static int16_t pvtx[2][4][3];   /* model vertices 0..3 (level 0) of model 4 (F/A-18, index 0) and model 3 (F-16, index 1) */
static void t_draw(aircraft_t *a) { if (have_models) player_draw_standin(&a->f, pvtx[(a->f.cls & 0x0F) == 1 ? 0 : 1]); }
static void t_step(aircraft_t *a, const flight_inputs_t *in) { flight_step(a, in); t_draw(a); }
static void t_attitude(aircraft_t *a, uint16_t p, uint16_t h, uint16_t r) { flight_set_attitude(a, p, h, r); t_draw(a); }
static void t_init(aircraft_t *a, flight_type t, flight_start s) { flight_init(a, t, s); t_draw(a); }

/* B0 plus an airborne case (section 16 table) */
static void air_case(int32_t y, uint16_t pitch, int S, int T, uint8_t gear) {
    t_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    flight_set_position(&ac, 273272584, y, 280480032);
    t_attitude(&ac, pitch, 2240, 0);
    ac.f.w2 = 0; ac.f.b4 = 0;
    ac.f.speed_target = ac.f.speed = (int16_t)S;
    ac.f.throttle = (int8_t)T;
    ac.f.gear = gear;
    flight_sync(&ac);
}
static void runway_case(void) { t_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY); }

typedef uint8_t (*keyfn)(int step);
static uint8_t keys_none(int s) { (void)s; return 0; }
static uint8_t keys_e3(int s) { return s >= 1 && s <= 10 ? FLIGHT_KEY_STICK_BACK : 0; }
static uint8_t keys_e4(int s) { return (s >= 1 && s <= 40 ? FLIGHT_KEY_THROTTLE_UP : 0) | (s >= 120 && s <= 150 ? FLIGHT_KEY_STICK_BACK : 0); }

/* runs the rows; returns 0 or prints the first mismatch */
static int run_rows(const char *name, const ex_row_t *rows, size_t n, keyfn kf) {
    int step = 0;
    for (size_t i = 0; i < n; i++) {
        while (step < rows[i].step) {
            flight_inputs_t in = NOKEY;
            in.keys = kf(step + 1);
            t_step(&ac, &in);
            step++;
        }
        const flight_state_t *f = &ac.f;
        const ex_row_t *r = &rows[i];
        struct { const char *n; long got, want; } c[] = {
            {"x", ac.pos[0], r->x}, {"y", ac.pos[1], r->y}, {"z", ac.pos[2], r->z}, {"pitch", f->pitch, r->pitch},
            {"heading", f->heading, r->heading}, {"roll", f->roll, r->roll}, {"S", f->speed_target, r->S}, {"W", f->speed, r->W},
            {"T", f->throttle, r->T}, {"fuel", f->fuel, r->fuel}, {"W2", f->w2, r->w2}, {"B4", f->b4, r->b4} };
        for (size_t k = 0; k < sizeof c / sizeof c[0]; k++)
            if (c[k].got != c[k].want) {
                printf("  %s step %d field %s: got %ld want %ld\n", name, r->step, c[k].n, c[k].got, c[k].want);
                return 1;
            }
    }
    return 0;
}

TEST(e1_runway_idle) { runway_case(); ASSERT_EQ_INT(run_rows("E1", ex_e1, EX_E1_N, keys_none), 0); }
TEST(e2_level_flight_gear_extending) { air_case(409600, 0, 3000, 60, 0x60); ASSERT_EQ_INT(run_rows("E2", ex_e2, EX_E2_N, keys_none), 0); }
TEST(e3_pull_up_gear_extending) { air_case(409600, 0, 3000, 60, 0x60); ASSERT_EQ_INT(run_rows("E3", ex_e3, EX_E3_N, keys_e3), 0); }
TEST(e9_level_flight_gear_up) { air_case(409600, 0, 3000, 60, 0xE0); ASSERT_EQ_INT(run_rows("E9", ex_e9, EX_E9_N, keys_none), 0); }
TEST(e4_takeoff_roll) { runway_case(); ASSERT_EQ_INT(run_rows("E4", ex_e4, EX_E4_N, keys_e4), 0); }
TEST(e7_stall) { air_case(409600, 0, 1700, 0, 0); ASSERT_EQ_INT(run_rows("E7", ex_e7, EX_E7_N, keys_none), 0); }

/* Open divergence 3 of the task 5 report (note 05 17.9): the stall nose drop at pitch 5 with +0x26 = -40 stores the signed word -5. The rig
 * (tests/rig/test_flight_diff.c, case "X1" and the junk family) agrees with the port on every field, matrices included. The next step's table
 * reads (gust index 901, pitch scale index -1) are exact reads of the image spans: no table read is unanswered. */
TEST(stall_nose_drop_below_level_stores_a_negative_pitch_word_and_the_next_steps_read_the_spans) {
    air_case(409600, 5, 1000, 0, 0);
    ac.f.ground = -40;
    flight_guard_reset(&ac);
    t_step(&ac, &NOKEY);
    ASSERT_EQ_INT((int16_t)ac.f.pitch, -5);
    ASSERT_EQ_INT(ac.f.ground, -10);
    ASSERT_EQ_INT(ac.f.rate_p, 0);
    ASSERT_EQ_INT(ac.f.b20 & 4, 4);
    ASSERT_EQ_INT(ac.f.heading, 2240);
    ASSERT_EQ_INT(ac.f.roll, 0);
    ASSERT_EQ_INT(ac.pitch, 5);                       /* nose up positive in the derived field */
    t_step(&ac, &NOKEY);                         /* gust: sine index 901; coupling: pitch scale index -1 */
    t_step(&ac, &NOKEY);
    ASSERT_EQ_INT((int)flight_guard_hits(&ac, FL_COUNT_RUNTIME), 0);
    ASSERT_EQ_INT((int)flight_guard_hits(&ac, FL_COUNT_IMAGE), 0);
    flight_guard_reset(&ac);
}

/* Note 05 17.9: the words the lookups see around the tables. Sine table index 901 and on (0x43DF2), the pitch scale in front of 0x3259A. */
TEST(the_table_over_reads_answer_the_image_words_of_the_note) {
    int known;
    const uint32_t sine = FLIGHT_ADDR_SINE;
    static const int want[] = { 0, 77, 78, 79, 80, 81, 1060 };
    for (int i = 0; i < 7; i++) ASSERT_EQ_INT(flight_image_word(&tables, sine + 2 * (901 + i), &known), want[i]);
    ASSERT_EQ_INT(known, 1);
    const uint32_t ps = FLIGHT_ADDR_PITCH_SCALE;
    ASSERT_EQ_INT(flight_image_word(&tables, ps - 2, &known), 0x4E75);
    ASSERT_EQ_INT(flight_image_word(&tables, ps - 4, &known), 84);
    ASSERT_EQ_INT(flight_image_word(&tables, ps - 6, &known), 0x4469);
    ASSERT_EQ_INT(flight_image_word(&tables, ps - 8, &known), 80);
    ASSERT_EQ_INT(flight_image_word(&tables, ps, &known), 256);
    ASSERT_EQ_INT(flight_image_word(&tables, ps + 2, &known), 240);
    ASSERT_EQ_INT(flight_image_word(&tables, ps + 4, &known), 220);
    flight_image_word(&tables, FLIGHT_SPAN_B_BASE + FLIGHT_SPAN_B_SIZE, &known);
    ASSERT_EQ_INT(known, 0);                           /* beyond the span: the module answers 0 and counts */
    flight_image_word(&tables, FLIGHT_SPAN_B_BASE - 2, &known);
    ASSERT_EQ_INT(known, 0);
}

/* Reads in 0x49A12..0x4A6E8 answer 0 and are counted (accepted divergence, Q16); reads in the variable block 0x4A6E8..0x4AE92 answer the port's own variable where it
 * keeps one (counted separately) and 0 elsewhere; any other address outside the spans answers 0 and is counted as an image read. */
TEST(over_reads_beyond_the_image_spans_are_counted_not_guessed) {
    t_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    flight_guard_reset(&ac);
    ac.f.v4a7b2 = 0x12345678;
    ASSERT_EQ_INT(flight_lookup_word(&ac, &tables, 0x4A7B2), 0x1234);        /* V:4A7B2 is a long the port keeps */
    ASSERT_EQ_INT(flight_lookup_word(&ac, &tables, 0x4A7B4), 0x5678);
    ASSERT_EQ_INT((int)flight_guard_hits(&ac, FL_COUNT_VARIABLE), 2);
    ac.f.v4a984 = 0x0840;
    ASSERT_EQ_INT(flight_lookup_word(&ac, &tables, 0x4A984), 0x0840);
    ASSERT_EQ_INT((int)flight_guard_hits(&ac, FL_COUNT_RUNTIME), 0);
    ASSERT_EQ_INT(flight_lookup_word(&ac, &tables, 0x4A8B0), 0);               /* the event-echo counters: not a flight variable */
    ASSERT_EQ_INT((int)flight_guard_hits(&ac, FL_COUNT_RUNTIME), 1);
    ASSERT_EQ_INT(flight_lookup_word(&ac, &tables, 0x49A12), 0);               /* display data written at run time */
    ASSERT_EQ_INT(flight_lookup_word(&ac, &tables, 0x4A6E6), 0);
    ASSERT_EQ_INT((int)flight_guard_hits(&ac, FL_COUNT_RUNTIME), 3);
    ASSERT_EQ_INT(flight_lookup_word(&ac, &tables, 0x30000), 0);               /* image, outside the spans */
    ASSERT_EQ_INT((int)flight_guard_hits(&ac, FL_COUNT_IMAGE), 1);
    ASSERT_EQ_INT(flight_lookup_word(&ac, &tables, FLIGHT_ADDR_SINE + 1800), 16384);   /* inside a span: no count */
    ASSERT_EQ_INT((int)flight_guard_hits(&ac, FL_COUNT_RUNTIME) + (int)flight_guard_hits(&ac, FL_COUNT_IMAGE) + (int)flight_guard_hits(&ac, FL_COUNT_VARIABLE), 3 + 1 + 3);
    flight_guard_reset(&ac);
}

/* The angle words are signed: on the ground a negative pitch word counts as level for the nose settle (note 05 section 17.9: "pitch at most 0: nothing
 * happens"), so neither the settled bit (W2 0x0040) nor E = 0 results; E keeps its value. */
TEST(a_negative_pitch_word_takes_the_signed_branches) {
    air_case(1800, 0, 0, 9, 0);
    ac.f.w2 = 0x0080;
    t_attitude(&ac, (uint16_t)-5, 2240, 0);
    ac.f.rate_p = 100;
    t_step(&ac, &NOKEY);
    ASSERT_EQ_INT(ac.f.rate_p, 100);
    ASSERT_EQ_INT(ac.f.w2 & 0x0040, 0);
    flight_guard_reset(&ac);
}

/* 17.18 c: with the nibble at 6 and W2 0x0020 set, a change of the retract progress clears the bit as the nibble-6 release does (gear 0x66 -> 0x50). */
TEST(a_retract_progress_change_releases_w2_0020_at_nibble_6) {
    air_case(409600, 0, 3000, 60, 0x66);
    ac.f.w2 = 0x0020;
    t_step(&ac, &NOKEY);
    ASSERT_EQ_INT(ac.f.gear, 0x50);
    ASSERT_EQ_INT(ac.f.w2 & 0x0020, 0);
    air_case(409600, 0, 3000, 60, 0x65);   /* no nibble 6: the bit is not released by this path */
    ac.f.w2 = 0x0020;
    t_step(&ac, &NOKEY);
    ASSERT_EQ_INT(ac.f.gear, 0x50);
    ASSERT_EQ_INT(ac.f.w2 & 0x0020, 0x0020);
    flight_guard_reset(&ac);
}

/* 17.18 c: the bit is cleared whenever the nibble was 6 with the bit set, also in a step with a progress change; no other nibble touches it. */
TEST(w2_0020_is_cleared_by_nibble_6_alone_17_18_c) {
    static const struct { uint8_t gear; int cleared; } c[] = { {0x06, 1}, {0x86, 1}, {0x76, 1}, {0x16, 1}, {0x05, 0}, {0x85, 0}, {0x0A, 0}, {0x8A, 0}, {0x07, 0}, {0x87, 0}, {0x60, 0} };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        air_case(409600, 0, 3000, 60, c[i].gear);
        ac.f.w2 = 0x0020;
        t_step(&ac, &NOKEY);
        if (((ac.f.w2 & 0x0020) == 0) != c[i].cleared) { printf("  gear 0x%02X: W2 0x%04X\n", c[i].gear, ac.f.w2); FAIL("17.18 c"); }
    }
    flight_guard_reset(&ac);
}

/* 17.18 b: the idle shortcut (external view, W0 without 0x1000 and 0x0100), also for a dying record with +0x4C above 0, leaves the stored displacement alone. */
TEST(the_idle_shortcut_keeps_the_stored_displacement_17_18_b) {
    air_case(409600, 0, 3000, 60, 0);
    ac.f.v4a83d = 1;
    ac.f.w0 = 0x06C8; ac.f.counter = 16;               /* dying: the world update takes +0x4C to 15 */
    ac.f.vel[0] = 111; ac.f.vel[1] = -222; ac.f.vel[2] = 333;
    int32_t p[3] = { ac.pos[0], ac.pos[1], ac.pos[2] };
    t_step(&ac, &NOKEY);
    ASSERT_EQ_INT(ac.f.counter, 15);
    ASSERT_EQ_INT(ac.pos[0], p[0]); ASSERT_EQ_INT(ac.pos[1], p[1]); ASSERT_EQ_INT(ac.pos[2], p[2]);
    ASSERT_EQ_INT(ac.f.vel[0], 111); ASSERT_EQ_INT(ac.f.vel[1], -222); ASSERT_EQ_INT(ac.f.vel[2], 333);
    flight_guard_reset(&ac);
}

/* 17.18 d and e: the notice 0xFE is gated on V:4A994 (the entity index, 0 for the player), not on V:4A996; a pointer height of 1 to 7 fires the preset 0 (none). */
TEST(the_throttle_notices_follow_v4a994_and_a_low_pointer_fires_preset_0) {
    air_case(409600, 0, 3000, 0, 0);
    ac.f.v4a994 = 0; ac.f.v4a996 = 0x0200; ac.f.v4a968 = 0;
    flight_inputs_t in = NOKEY; in.keys = FLIGHT_KEY_THROTTLE_UP;
    t_step(&ac, &in);
    ASSERT_EQ_INT(ac.f.v4a968, 0xFE);
    air_case(409600, 0, 3000, 0, 0);
    ac.f.v4a994 = 1; ac.f.v4a996 = 0; ac.f.v4a968 = 0;
    t_step(&ac, &in);
    ASSERT_EQ_INT(ac.f.v4a968, 0);
    air_case(409600, 0, 3000, 9, 0);
    ac.f.v4a830 = 5; ac.f.v4a834 = 72; ac.f.v4a928 = 0;
    t_step(&ac, &NOKEY);
    ASSERT_EQ_INT(ac.f.v4a928, 0); ASSERT_EQ_INT(ac.f.v4a834, 5); ASSERT_EQ_INT(ac.f.throttle, 9);
    flight_guard_reset(&ac);
}

/* 17.18 a: span 2 starts at 0x3AE92 (the composition's lookups read 0x3AE92..0x4AE91) */
TEST(span_2_covers_the_composition_lookup_range) {
    int known;
    flight_image_word(&tables, 0x3AE92, &known); ASSERT_EQ_INT(known, 1);
    flight_image_word(&tables, 0x3AE90, &known); ASSERT_EQ_INT(known, 0);
    flight_image_word(&tables, 0x49A10, &known); ASSERT_EQ_INT(known, 1);
    ASSERT_EQ_INT(FLIGHT_SPAN_B_BASE, 0x3AE92); ASSERT_EQ_INT(FLIGHT_SPAN_B_SIZE, 60288);
}

/* The extracted quarter-wave table and the generated one of fx.c agree at all 901 entries (note 03), so the two sources are interchangeable; the
 * flight module uses the extracted one for the coupling and the gust and the composition (note 05: "the port loads these"). */
TEST(the_extracted_sine_table_equals_the_generated_one) {
    for (int i = 0; i <= 900; i++) {
        int known;
        int w = flight_image_word(&tables, FLIGHT_ADDR_SINE + 2 * (uint32_t)i, &known);
        if (!known || w != fx_sin_tenth((uint32_t)i)) { printf("  entry %d: %d vs %d\n", i, w, fx_sin_tenth((uint32_t)i)); FAIL("sine table"); }
    }
}

/* Section 1: with V:4A866 set the step is skipped except for the clearing of the proximity bit (and, rig, W0 bit 0x0002 and the gear cue). */
TEST(pause_keeps_the_state_but_clears_the_proximity_bit) {
    air_case(409600, 0, 3000, 60, 0);
    ac.f.v4a866 = 1;
    ac.f.w2 |= 0x0001; ac.f.w0 |= 0x0002; ac.f.counter = 10;
    aircraft_t before = ac;
    flight_inputs_t in = NOKEY; in.keys = FLIGHT_KEY_STICK_BACK | FLIGHT_KEY_THROTTLE_UP;
    t_step(&ac, &in);
    ASSERT_EQ_INT(ac.f.w2 & 1, 0);
    ASSERT_EQ_INT(ac.f.w0 & 2, 0);
    ASSERT_EQ_INT(ac.f.counter, 10);                  /* not decremented */
    ASSERT_EQ_INT(ac.f.keys, in.keys);                /* the key byte is stored (the key handlers run whether or not the step does) */
    ASSERT_EQ_INT(ac.pos[1], before.pos[1]);
    ASSERT_EQ_INT(ac.f.speed_target, before.f.speed_target);
    ASSERT_EQ_INT(ac.f.cmd_pitch, 0);
    ac.f.v4a866 = 0;
    t_step(&ac, &in);
    ASSERT_EQ_INT(ac.f.cmd_pitch, -1);                /* running again */
}

/* Note 04a 8a: the viewed cell V:4AA04/06 is written by the camera set-up AFTER the update. The game's environment says so (view_cell_by_camera): the step leaves the word alone, so the
 * records 1 to 15 of the same pass read the previous pass's camera cell; the rig harnesses (no frame) take the step's cockpit value. */
TEST(the_step_writes_the_viewed_cell_only_for_a_caller_without_a_frame) {
    air_case(409600, 0, 3000, 60, 0);
    ac.f.v4aa04 = 7; ac.f.v4aa06 = 9;                  /* the camera's cell of the previous pass, not the player's */
    flight_inputs_t in = NOKEY;
    env.view_cell_by_camera = 1;
    t_step(&ac, &in);
    ASSERT_EQ_INT(ac.f.v4aa04, 7); ASSERT_EQ_INT(ac.f.v4aa06, 9);
    env.view_cell_by_camera = 0;
    t_step(&ac, &in);
    ASSERT_EQ_INT(ac.f.v4aa04, ac.f.cell_x); ASSERT_EQ_INT(ac.f.v4aa06, ac.f.cell_z);
}

/* Section 12 as the rig shows it: out of control and descending, only the eject warning is considered, and not with an ejection nibble. */
TEST(direct_posts_follow_the_rig_chain) {
    struct { int ooc, nib, desc, bank; uint16_t want; } c[] = {
        { 0, 0, 0, 1, 0x9C06 }, { 0, 0, 1, 1, 0x9C06 }, { 0, 1, 1, 1, 0x9C06 }, { 1, 0, 0, 1, 0x9C06 },
        { 1, 0, 1, 0, 0xD02A }, { 1, 0, 1, 1, 0xD02A }, { 1, 1, 0, 1, 0x9C06 }, { 1, 1, 1, 0, 0 }, { 1, 1, 1, 1, 0 } };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        air_case(5000, 0, 3000, 60, 0);
        t_attitude(&ac, 0, 2240, c[i].bank ? 3000 : 0);
        ac.f.b20 = c[i].ooc ? 2 : 0;
        ac.f.gear |= c[i].nib ? 9 : 0;
        ac.f.vel[1] = c[i].desc ? -3000 : 3000;
        ASSERT_EQ_INT(flight_direct_post_code(&ac), c[i].want);
        t_step(&ac, &NOKEY);
        if (ac.f.v4ab98 != c[i].want) { printf("  case %zu: code %04x want %04x\n", i, ac.f.v4ab98, c[i].want); FAIL("direct post"); }
        if (c[i].want == 0xD02A) ASSERT_EQ_INT(ac.f.v4ac0c & 0x40, 0x40);
        if (c[i].want == 0x9C06) ASSERT_EQ_INT(ac.f.v4ac0c & 0x80, 0x80);
    }
}

/* Note 07 6.5 / sweep B (V: the roll and yaw capture and the rig: rates equal with and without B20 bit 1, roll -571 / +572, yaw +128 / -127; the conversion identical for 256 key bytes; speed frozen,
 * pitch lost): an out-of-control player (B20 bit 1) still answers the roll and the rudder keys; only the speed step and the pitch are lost. */
TEST(an_out_of_control_player_still_rolls_and_yaws_with_its_keys_and_loses_only_the_speed_and_the_pitch) {
    static const uint8_t keysets[] = { FLIGHT_KEY_ROLL_RIGHT, FLIGHT_KEY_ROLL_LEFT, FLIGHT_KEY_RUDDER_RIGHT, FLIGHT_KEY_RUDDER_LEFT, FLIGHT_KEY_ROLL_RIGHT | FLIGHT_KEY_RUDDER_LEFT, FLIGHT_KEY_STICK_BACK, FLIGHT_KEY_STICK_FORWARD };
    for (size_t k = 0; k < sizeof keysets; k++) {
        aircraft_t a, b;
        air_case(8000, 0, 3000, 60, 0);
        a = ac;
        air_case(8000, 0, 3000, 60, 0);
        b = ac;
        b.f.b20 |= 0x02;
        flight_inputs_t in = NOKEY; in.keys = keysets[k];
        for (int i = 0; i < 12; i++) { t_step(&a, &in); t_step(&b, &in); }
        ASSERT_EQ_INT(b.f.cmd_roll, a.f.cmd_roll);                      /* the conversion is the same */
        ASSERT_EQ_INT(b.f.cmd_rudder, a.f.cmd_rudder);
        ASSERT_EQ_INT(b.f.cmd_pitch, a.f.cmd_pitch);
        if (keysets[k] & (FLIGHT_KEY_ROLL_RIGHT | FLIGHT_KEY_ROLL_LEFT)) { ASSERT_TRUE(a.f.rate_r != 0); ASSERT_EQ_INT(b.f.rate_r, a.f.rate_r); }   /* the roll rate */
        if (keysets[k] & (FLIGHT_KEY_RUDDER_RIGHT | FLIGHT_KEY_RUDDER_LEFT)) { ASSERT_TRUE(a.f.rate_h != 0); ASSERT_EQ_INT(b.f.rate_h, a.f.rate_h); }   /* the yaw rate */
        ASSERT_EQ_INT(b.f.speed, 3000);                                  /* the speed step is skipped for B20 bit 1 */
    }
}

/* Note 05 17.10 (correction C19): the terrain grid is indexed with the unsigned bytes (x >> 24) & 0xFF and (z >> 24) & 0xFF, word index col + 32 * row. A
 * negative x reads cell 256 + col of row 0 (column -1 is word 255, the last cell of row 7), never outside the extracted words. */
TEST(terrain_grid_index_uses_the_unsigned_column_byte) {
    uint8_t b4a = 0, b4b = 0;
    unsigned reads[8] = { 0 };
    int checked = 0, water = 0;
    for (int k = 0; k < 400; k++) {
        int32_t xc = (int32_t)((k * 40503u) & 0xFFFFFF), zc = (int32_t)((k * 9973u * 7u) & 0xFFFFFF);
        for (int col = 128; col < 256; col += 9) {
            /* x < 0 in column `col - 256` is the cell word col + 32 * row; the same cell reached with x >= 0 needs col <= 31, so compare with the table walk
             * of the same word through a position in the grid when it is in the first row group */
            int32_t x = ((col - 256) * 16777216) + xc, z = zc;
            flight_terrain_query(&tables, x, z, &b4a, reads);
            checked++;
            if (b4a & 2) water++;
            (void)b4b;
        }
    }
    ASSERT_TRUE(checked > 3000);
    ASSERT_TRUE(water > 0);                          /* some of the cells read are sea cells, as the rig finds (note 17.10: 1500 of 3000) */
    ASSERT_EQ_INT((int)reads[FL_GUARD_TERRAIN_DATA], 0);
    ASSERT_EQ_INT((int)reads[FL_GUARD_EDGE_DEN], 0);
    /* word 255 reached two ways: column -1 of row 0, and column 31 of row 7 */
    for (int k = 0; k < 200; k++) {
        int32_t xc = (int32_t)((k * 77003u) & 0xFFFFFF), zc = (int32_t)((k * 55001u) & 0xFFFFFF);
        uint8_t u = 0, v = 0;
        flight_terrain_query(&tables, -16777216 + xc, zc, &u, reads);
        flight_terrain_query(&tables, 31 * 16777216 + xc, 7 * 16777216 + zc, &v, reads);
        ASSERT_EQ_INT(u & 2, v & 2);
    }
}

/* The extracted terrain data is well formed: every cell the unsigned-byte index can reach with a legal position walks to its terminator inside the extracted
 * words, so the four bounds of the walk (FL_GUARD_TERRAIN_DATA) are unreachable. Legal: x >= 0 (columns 0..31) in any of the 32 rows; x < 0 (columns 128..255)
 * only with z of 3 cells or less (the position update sets z to 0 in the step in which x turns negative, and the displacement of a step is far below a cell,
 * note 05 17.10). */
TEST(terrain_walk_stays_inside_the_extracted_words_for_every_reachable_cell) {
    unsigned reads[8] = { 0 };
    uint8_t b4;
    for (int row = 0; row < 32; row++)
        for (int col = 0; col < 256; col++)
            for (int k = 0; (col < 32 || (col >= 128 && row < 3)) && k < 6; k++) {
                int32_t xc = (int32_t)(((unsigned)(col * 31 + row * 17 + k * 40009u) * 2654435761u >> 8) & 0xFFFFFF);
                int32_t zc = (int32_t)(((unsigned)(col * 13 + row * 29 + k * 30011u) * 40503u * 2654435761u >> 8) & 0xFFFFFF);
                int32_t x = (col < 128 ? col : col - 256) * 16777216 + xc;
                flight_terrain_query(&tables, x, row * 16777216 + zc, &b4, reads);
            }
    ASSERT_EQ_INT((int)reads[FL_GUARD_TERRAIN_DATA], 0);
    ASSERT_EQ_INT((int)reads[FL_GUARD_EDGE_DEN], 0);
}

/* Section 15: the pointer throttle flag is set at the start, P = Q = 8 * T (T = 9). */
TEST(start_sets_the_pointer_throttle_flag) {
    t_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    ASSERT_EQ_INT(ac.f.v4a890, 1);
    ASSERT_EQ_INT(ac.f.v4a830, 72);
    ASSERT_EQ_INT(ac.f.v4a834, 72);
}

TEST(e8_hard_touchdown) {
    air_case(20000, 2000, 3000, 60, 0);
    int step = 0;
    for (size_t i = 0; i < EX_E8_N; i++) {
        while (step < ex_e8[i].step) { t_step(&ac, &NOKEY); step++; }
        const ex8_row_t *r = &ex_e8[i];
        const flight_state_t *f = &ac.f;
        long got[] = { ac.pos[1], i == 0 ? 0 : f->vel[1], f->speed, f->w0, f->w2, f->v4a87d, f->v4a850 };
        long want[] = { r->y, r->v42, r->W, r->w0, r->w2, r->v87d, r->v850 };
        const char *nm[] = { "y", "+0x42", "W", "W0", "W2", "V:4A87D", "V:4A850" };
        for (int k = 0; k < 7; k++)
            if (got[k] != want[k]) { printf("  E8 step %d field %s: got %ld want %ld\n", r->step, nm[k], got[k], want[k]); FAIL("E8 row"); }
    }
}

/* Note 08 10.7 and 10.2: the log's clock and crash counter take their events from the writers (0x171E2 lift-off, 0x16F6A touchdown, 0x16FB0 crash), not from the
 * flag edges; the flight step raises them and never reads or clears them. */
TEST(the_lift_off_touchdown_and_crash_writers_raise_their_events) {
    runway_case();
    ASSERT_TRUE(ac.f.w2 & 0x0080);
    ASSERT_EQ_INT(ac.f.ev_liftoff | ac.f.ev_touchdown | ac.f.ev_crash, 0);
    ac.pos[1] += 100000;                                       /* well above the ground with the wheels flagged down: the lift-off */
    t_step(&ac, &NOKEY);
    ASSERT_TRUE(!(ac.f.w2 & 0x0080));
    ASSERT_EQ_INT(ac.f.ev_liftoff, 1); ASSERT_EQ_INT(ac.f.ev_touchdown, 0); ASSERT_EQ_INT(ac.f.ev_crash, 0);
    air_case(20000, 2000, 3000, 60, 0);                        /* the E8 case: a hard touchdown */
    ASSERT_EQ_INT(ac.f.ev_liftoff | ac.f.ev_touchdown | ac.f.ev_crash, 0);
    for (int i = 0; i < 30; i++) t_step(&ac, &NOKEY);
    ASSERT_TRUE(ac.f.w0 & 0x0200);
    ASSERT_EQ_INT(ac.f.ev_touchdown, 1); ASSERT_EQ_INT(ac.f.ev_crash, 1); ASSERT_EQ_INT(ac.f.ev_liftoff, 0);
}

/* ---- the carrier examples E5 and E6 (need the extracted models) ------------------------ */
#define CARRIER_POS 286785536
static int16_t cv[FLIGHT_SHIP_SLOTS][3];   /* the carrier's vertices, kept for the start tests */
static void build_env_models(void) {
    char err[128];
    world = world_load(&loader, err, sizeof err);
    if (!world) { printf("  (models unavailable: %s)\n", err); return; }
    for (int k = 0; k < FLIGHT_SHIP_SLOTS; k++) {
        int32_t v[3] = {0, 0, 0};
        if (world_model_vertex(world, 8, 0, (unsigned)k, v) != 0) { printf("  (carrier vertex %d missing)\n", k); world_destroy(world); world = NULL; return; }
        for (int i = 0; i < 3; i++) cv[k][i] = (int16_t)v[i];
    }
    env.ship.present = 1;
    env.ship.w0 = 0x404C; env.ship.cls = 0x20; env.ship.group = 0x0E; env.ship.l7d = 3;
    env.ship.pos[0] = CARRIER_POS; env.ship.pos[1] = 0; env.ship.pos[2] = CARRIER_POS;
    env.ship.fine_x = env.ship.fine_z = 6144; env.ship.alt8 = 0;
    flight_ship_build(&env.ship, (const int16_t (*)[3])cv, FLIGHT_SHIP_SLOTS, 0, 28600, 0);
    for (int m = 0; m < 2; m++)
        for (int k = 0; k < 4; k++) {
            int32_t v[3] = {0, 0, 0};
            world_model_vertex(world, m == 0 ? 4 : 3, 0, (unsigned)k, v);
            for (int i = 0; i < 3; i++) pvtx[m][k][i] = (int16_t)v[i];
        }
    have_models = 1;
}

static void carrier_case(void) {   /* E5 */
    t_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    flight_set_position(&ac, 286801436, 30472, 286891436);
    t_attitude(&ac, 0, 28600, 0);
    ac.f.w2 = 0x0082; ac.f.b4 = 0xC8; ac.f.deck = 112;
    flight_sync(&ac);
}

TEST(e5_catapult_launch) {
    if (!have_models) { printf("skipped "); return; }
    carrier_case();
    int step = 0;
    for (size_t i = 0; i < EX_E5_N; i++) {
        const ex5_row_t *r = &ex_e5[i];
        while (step < r->step) { flight_inputs_t in = NOKEY; in.keys = step + 1 <= 45 ? FLIGHT_KEY_THROTTLE_UP : 0; t_step(&ac, &in); step++; }
        const flight_state_t *f = &ac.f;
        long got[] = { ac.pos[0], ac.pos[1], ac.pos[2], f->speed_target, f->speed, f->throttle, f->w2, f->b4, f->deck };
        long want[] = { r->x, r->y, r->z, r->S, r->W, r->T, r->w2, r->b4, r->deck };
        const char *nm[] = { "x", "y", "z", "S", "W", "T", "W2", "B4", "+0x4E" };
        for (int k = 0; k < 9; k++)
            if (got[k] != want[k]) { printf("  E5 step %d field %s: got %ld want %ld\n", r->step, nm[k], got[k], want[k]); FAIL("E5 row"); }
    }
}

TEST(e6_arrestor_catch) {
    if (!have_models) { printf("skipped "); return; }
    t_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    flight_set_position(&ac, CARRIER_POS, 37472, CARRIER_POS - 260000);
    t_attitude(&ac, 0, 28600, 0);
    ac.f.w2 = 0x8000; ac.f.b4 = 0; ac.f.speed_target = ac.f.speed = 1700; ac.f.throttle = 40; ac.f.gear = 0;
    flight_sync(&ac);
    int step = 0;
    for (size_t i = 0; i < EX_E6_N; i++) {
        const ex6_row_t *r = &ex_e6[i];
        while (step < r->step) { t_step(&ac, &NOKEY); step++; }
        const flight_state_t *f = &ac.f;
        long got[] = { ac.pos[0] - CARRIER_POS, ac.pos[1], ac.pos[2] - CARRIER_POS, f->speed_target, f->speed, f->w2, f->b4 };
        long want[] = { r->dx, r->y, r->dz, r->S, r->W, r->w2, r->b4 };
        const char *nm[] = { "dx", "y", "dz", "S", "W", "W2", "B4" };
        for (int k = 0; k < 7; k++)
            if (got[k] != want[k]) { printf("  E6 step %d field %s: got %ld want %ld\n", r->step, nm[k], got[k], want[k]); FAIL("E6 row"); }
    }
}


/* ---- review focus 1 and 3, HUD figures, the landmark z constant ------------------------------- */

/* Review focus 1: on the deck with the throttle at idle and the stick held fully back nothing happens: no sinking, no negative airspeed. */
TEST(deck_idle_with_stick_back_nothing_happens) {
    if (!have_models) { printf("skipped "); return; }
    t_init(&ac, FLIGHT_FA18, FLIGHT_START_CARRIER);
    const int32_t x0 = ac.pos[0], y0 = ac.pos[1], z0 = ac.pos[2];
    ASSERT_EQ_INT(y0, 30472);   /* section 9: (112 << 8) + 0x708 */
    for (int i = 0; i < 300; i++) {
        flight_inputs_t in = NOKEY;
        in.keys = FLIGHT_KEY_STICK_BACK;
        t_step(&ac, &in);
        ASSERT_TRUE(ac.f.speed >= 0);
        ASSERT_TRUE(ac.f.speed_target >= 0);
        ASSERT_EQ_INT(ac.pos[1], y0);
    }
    ASSERT_EQ_INT(ac.pos[0], x0);
    ASSERT_EQ_INT(ac.pos[2], z0);
    ASSERT_EQ_INT(ac.f.speed, 0);
    ASSERT_EQ_INT(ac.f.pitch, 0);
    ASSERT_EQ_INT(ac.f.w2 & 0x0080, 0x0080);   /* still on its wheels */
    ASSERT_EQ_INT(ac.f.b4 & 0x08, 0x08);       /* catapult still armed */
}
TEST(runway_idle_with_stick_back_nothing_happens) {
    runway_case();
    const int32_t x0 = ac.pos[0], z0 = ac.pos[2];
    for (int i = 0; i < 300; i++) {
        flight_inputs_t in = NOKEY;
        in.keys = FLIGHT_KEY_STICK_BACK;
        t_step(&ac, &in);
        ASSERT_TRUE(ac.f.speed >= 0);
        ASSERT_EQ_INT(ac.pos[1], 1800);
    }
    ASSERT_EQ_INT(ac.pos[0], x0);
    ASSERT_EQ_INT(ac.pos[2], z0);
    ASSERT_EQ_INT(ac.f.pitch, 0);
}

static int angles_in_domain(const flight_state_t *f) { return f->pitch < 28800 && f->heading < 28800 && f->roll < 28800; }

/* Review focus 3: roll through 360 degrees, a loop and a vertical climb: the angle words stay in 0..28799 and the matrices follow them. */
TEST(roll_through_360_wraps_in_the_angle_unit) {
    air_case(409600, 0, 3000, 80, 0xE0);
    int wraps = 0;
    uint16_t prev = ac.f.roll;
    for (int i = 0; i < 400; i++) {
        flight_inputs_t in = NOKEY;
        in.keys = FLIGHT_KEY_ROLL_LEFT;
        t_step(&ac, &in);
        ASSERT_TRUE(angles_in_domain(&ac.f));
        if (ac.f.roll < prev && prev - ac.f.roll > 14400) wraps++;
        prev = ac.f.roll;
        fx_mat_t m;
        fx_matrix_a(&m, ac.f.pitch, ac.f.heading, ac.f.roll);
        for (int k = 0; k < 9; k++) ASSERT_EQ_INT(ac.f.mat_a[k], m.m[k / 3][k % 3]);
        game_hud_t h;
        flight_hud(&ac, &h);
        ASSERT_TRUE(h.roll_deg >= -180 && h.roll_deg <= 180);
        ASSERT_TRUE(h.heading_deg >= 0 && h.heading_deg < 360);
        ASSERT_EQ_INT(h.heading_deg, (ac.f.heading >> 3) / 10);
    }
    ASSERT_TRUE(wraps >= 1);   /* at least one full turn of the roll angle in 400 steps */
}
TEST(loop_and_vertical_climb_keep_the_angles_in_domain) {
    air_case(409600, 0, 4000, 120, 0xE0);
    int steepest = 0;
    for (int i = 0; i < 300; i++) {
        flight_inputs_t in = NOKEY;
        in.keys = FLIGHT_KEY_STICK_BACK;
        t_step(&ac, &in);
        if (!angles_in_domain(&ac.f)) break;   /* a stall nose drop below level is the one way out: see the stall step in flight.c */
        game_hud_t h;
        flight_hud(&ac, &h);
        ASSERT_TRUE(h.pitch_deg >= -180 && h.pitch_deg <= 180);
        int p = h.pitch_deg < 0 ? -h.pitch_deg : h.pitch_deg;
        if (p > steepest) steepest = p;
    }
    ASSERT_TRUE(steepest > 60);   /* the stick back for 300 steps pulls through the vertical */
}

TEST(hud_figures_follow_the_note) {
    air_case(409600, 0, 4231, 120, 0xE0);
    game_hud_t h;
    ac.f.fuel = 0x61A800;
    flight_hud(&ac, &h);
    ASSERT_EQ_INT(h.airspeed_kt, 352);     /* 4231 / 12 truncated (note 06 ran the routine) */
    ac.f.speed = 4271;
    flight_hud(&ac, &h);
    ASSERT_EQ_INT(h.airspeed_kt, 355);
    ac.f.speed = -4271;
    flight_hud(&ac, &h);
    ASSERT_EQ_INT(h.airspeed_kt, 355);     /* |W| */
    flight_set_position(&ac, ac.pos[0], 1800, ac.pos[2]);
    flight_hud(&ac, &h);
    ASSERT_EQ_INT(h.altitude_ft, 5);       /* 5 * (1800 >> 10) */
    flight_set_position(&ac, ac.pos[0], 30472, ac.pos[2]);
    flight_hud(&ac, &h);
    ASSERT_EQ_INT(h.altitude_ft, 145);     /* the carrier deck, demo-deck.ppm */
    t_attitude(&ac, 0, 2240, 0);
    flight_hud(&ac, &h);
    ASSERT_EQ_INT(h.heading_deg, 28);      /* 2240 >> 3 / 10, the runway frame */
    ac.f.throttle = 9;
    flight_hud(&ac, &h);
    ASSERT_EQ_INT(h.throttle_pct, 7);
    ac.f.throttle = 120;
    flight_hud(&ac, &h);
    ASSERT_EQ_INT(h.throttle_pct, 100);
    ASSERT_EQ_INT(h.fuel_pct, 100);
    ac.f.fuel = 0x61A800 / 2;
    flight_hud(&ac, &h);
    ASSERT_EQ_INT(h.fuel_pct, 50);
    ASSERT_EQ_INT(FLIGHT_STEP_HZ, 8);      /* the controller's ruling (note 01b round 3, note 05 section 14) */
}

TEST(panel_digits_are_caches_refreshed_on_the_cadence_of_note_06) {
    air_case(409600, 0, 3000, 60, 0xE0);
    int32_t last_alt = flight_panel_altitude(&ac);
    int changes = 0;
    for (int i = 0; i < 40; i++) {
        t_step(&ac, &NOKEY);
        int32_t a = flight_panel_altitude(&ac);
        if (a != last_alt) { changes++; ASSERT_EQ_INT(ac.f.steps & 3, 0); }
        last_alt = a;
    }
    ASSERT_TRUE(changes >= 1 && changes <= 10);   /* at most once per four steps */
}

/* The landmark z quirk (section 11b): the same fine position is inside a volume for z = 1 (the rig image) and outside for z = 48 (the reference machine). */
TEST(landmark_z_constant_shifts_the_volume_by_its_value) {
    ASSERT_EQ_INT(FLIGHT_LANDMARK_Z, 48);
    int hits[2];
    int16_t saved_vtx[2][4][3]; memcpy(saved_vtx, pvtx, sizeof saved_vtx);
    static const int zs[2] = { 1, FLIGHT_LANDMARK_Z };
    for (int run = 0; run < 2; run++) {
        flight_env_t e = env;
        e.landmark_z = zs[run];
        memset(pvtx, 0, sizeof pvtx);
        e.n_landmarks = 1;
        memset(&e.landmark[0], 0, sizeof e.landmark[0]);
        flight_set_env(&e);
        air_case(60000, 0, 0, 0, 0xE0);          /* at rest: the fine position stays put */
        flight_set_position(&ac, 273272584, 60000, 280480032);
        ac.f.w2 = 0x0002;                       /* the contact flag of the previous step */
        flight_lm_slot_t *sl = &e.landmark[0];
        sl->cell_x = (uint8_t)ac.f.cell_x; sl->cell_z = (uint8_t)ac.f.cell_z;
        sl->box[0] = 0x7000; sl->box[1] = (int16_t)(ac.f.fine_x - 500); sl->box[2] = (int16_t)(ac.f.fine_x + 500);
        sl->box[3] = (int16_t)(ac.f.fine_z - 500); sl->box[4] = (int16_t)(ac.f.fine_z + 500);
        sl->nnodes = 1;
        flight_lm_node_t *nd = &sl->node[0];
        memcpy(nd->box, sl->box, sizeof nd->box);
        nd->nplanes = 1;
        nd->ref[0][0] = 0; nd->ref[0][1] = 0; nd->ref[0][2] = (int16_t)(ac.f.fine_z + 10);   /* inside while z-position < fine_z + 10 */
        nd->nrm[0][0] = 0; nd->nrm[0][1] = 0; nd->nrm[0][2] = 1;
        /* the player's own point is (fine_x, alt8, fine_z + z); the plane is on its far side for z = 48 */
        uint16_t w0 = ac.f.w0;
        t_step(&ac, &NOKEY);
        hits[run] = (ac.f.w0 & 0x8000) != 0;
        (void)w0;
    }
    flight_set_env(&env);
    memcpy(pvtx, saved_vtx, sizeof pvtx);
    ASSERT_EQ_INT(hits[0], 1);
    ASSERT_EQ_INT(hits[1], 0);
}

/* flight_restart (note 05 17.17 rule 4, C32, C43): the lines of 0x1D538 are written, everything else is kept (markers). From W2 = 0xFFFF the common lines leave
 * 0x4EF7 (bits 0xB108 cleared, 0x0080 set; 0x0002 was set already); the carrier start does not touch W2 again. */
TEST(flight_restart_writes_the_list_of_17_17_rule_4_and_keeps_the_rest) {
    if (!have_models) { printf("skipped "); return; }
    flight_set_env(&env);
    t_init(&ac, FLIGHT_FA18, FLIGHT_START_CARRIER);
    flight_state_t *f = &ac.f;
    f->w2 = 0xFFFF; f->w0 = 0xFFFF; f->b4 = 0xFF; f->v4a830 = f->v4a834 = 500; f->throttle = 40; f->id = 3;
    f->v4a957 = 1; f->v4a84f = 2; f->v4a8fe = 1; f->v4a986 = 0x0100; f->b7b = 0x45; f->cmd_pitch = 5; f->v4a994 = 6; f->v4a996 = 7;
    f->fuel = 0x300000; f->v4a963 = 7; f->v4a903 = 3; f->v4a9fe = 5; f->v4a968 = 4; f->v4a850 = 3; f->v4a83c = 0; f->v4a847 = 0; f->v4a95e = 0;
    f->speed = 3000; f->vel[1] = -500; f->v4a928 = 77; f->v4ab98 = 0x9C06; f->v4aa7a = 0x0200; f->v4a83d = 1; f->v4a982 = 0x0301; f->v4a98a = 0x1234;
    f->v4a984 = 0x0800; f->pitch = 5000; f->roll = 6000; f->heading = 100; f->rate_h = 33; f->l7d = 0x53;
    flight_restart(&ac, FLIGHT_START_CARRIER);
    ASSERT_EQ_INT(f->w0, 0x11C8);
    ASSERT_EQ_INT(f->w2, 0x4EF7);
    ASSERT_EQ_INT(f->b4, 0xFD);                         /* (0xFF & 0x35) | 0xC8 */
    ASSERT_EQ_INT(f->pitch, 0); ASSERT_EQ_INT(f->roll, 0); ASSERT_EQ_INT(f->heading, 28600);   /* the carrier's attitude (the demo carrier here) */
    ASSERT_EQ_INT(f->v4a830, 72); ASSERT_EQ_INT(f->v4a834, 72); ASSERT_EQ_INT(f->v4a890, 1);
    ASSERT_EQ_INT(f->throttle, 9); ASSERT_EQ_INT(f->id, 0x0D); ASSERT_EQ_INT(f->l7d, 0x03);
    ASSERT_EQ_INT(f->speed, 0); ASSERT_EQ_INT(f->vel[1], 0);
    ASSERT_EQ_INT(f->fuel, 0x61A800);                   /* refilled */
    ASSERT_EQ_INT(f->v4a928, 0); ASSERT_EQ_INT(f->v4ab98, 0); ASSERT_EQ_INT(f->v4aa7a, 0);
    ASSERT_EQ_INT(f->v4a968, 0xFE); ASSERT_EQ_INT(f->v4a850, 4);
    /* kept */
    ASSERT_EQ_INT(f->v4a83d, 1);                        /* the view is not reset */
    ASSERT_EQ_INT(f->v4a984, 0x0800);                   /* bit 6 is the resume state's, not set here */
    ASSERT_EQ_INT(f->v4a957, 1); ASSERT_EQ_INT(f->v4a84f, 2); ASSERT_EQ_INT(f->v4a8fe, 1);
    ASSERT_EQ_INT(f->v4a986, 0x0100); ASSERT_EQ_INT(f->b7b, 0x45); ASSERT_EQ_INT(f->cmd_pitch, 5);
    ASSERT_EQ_INT(f->v4a994, 6); ASSERT_EQ_INT(f->v4a996, 7); ASSERT_EQ_INT(f->rate_h, 33);
    ASSERT_EQ_INT(f->v4a963, 7); ASSERT_EQ_INT(f->v4a903, 3);
    ASSERT_EQ_INT(f->v4a9fe, 5);
    ASSERT_EQ_INT(f->v4a982, 0x0301); ASSERT_EQ_INT(f->v4a98a, 0x1234);
    ASSERT_EQ_INT(f->v4a83c, 0); ASSERT_EQ_INT(f->v4a847, 0); ASSERT_EQ_INT(f->v4a95e, 0);
    /* a runway restart from a nose-down, rolled attitude is level; V:4A850 stays 0 when it was 0 */
    f->pitch = 5000; f->roll = 6000; f->v4a850 = 0; f->w2 = 0x0000;
    flight_restart(&ac, FLIGHT_START_RUNWAY);
    ASSERT_EQ_INT(f->pitch, 0); ASSERT_EQ_INT(f->roll, 0); ASSERT_EQ_INT(f->heading, 28 * 80);
    ASSERT_EQ_INT(f->v4a850, 0); ASSERT_EQ_INT(f->w2, 0x0080);   /* W2 0x0002 is the resume state's (game.c pass B), not the restart's */
}

/* note 05 17.9 gimbal guard: 16-bit subtract, true signed compare, 16-bit negate when smaller; a wrapped (>= 32768) result stays negative */
TEST(gimbal_guard_differences_wrap_as_words) {
    ASSERT_EQ_INT(gg_diff(20408, -12520), -32608);   /* 1594 step 6: dh wraps negative, not negated again */
    ASSERT_EQ_INT(gg_diff(7992, 0x3840), 6408);      /* dr 7992 flipped */
    ASSERT_EQ_INT(gg_diff(6408, -32608), -26520);    /* the last test D wraps negative: below 0x320, the angles are stored */
    ASSERT_EQ_INT(gg_diff(100, 300), 200); ASSERT_EQ_INT(gg_diff(300, 100), 200); ASSERT_EQ_INT(gg_diff(-5, 5), 10);
    ASSERT_EQ_INT(gg_diff(-20000, 20000), -25536);   /* -40000 wraps to 25536, first operand smaller: negated */
    ASSERT_EQ_INT(gg_diff(0, -32768), -32768);
}

/* 17.17 (xi): the vertex slots are the DRAW's. flight_restart leaves them as the last draw wrote them (the crash attitude); the step reads those and refreshes
 * them from its own final attitude AFTER it, as the pass's draw does. */
TEST(the_vertex_slots_are_refreshed_after_the_step_and_the_restart_leaves_them) {
    if (!have_models) { printf("skipped "); return; }
    flight_set_env(&env);
    t_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    t_attitude(&ac, 2000, 1000, 500);          /* rotates the slots (the draw) */
    int16_t crash[4][3];
    memcpy(crash, ac.f.vtx, sizeof crash);
    int differs = 0;
    t_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    for (int k = 0; k < 4; k++) for (int i = 0; i < 3; i++) differs |= crash[k][i] != ac.f.vtx[k][i];
    ASSERT_TRUE(differs);                               /* the level attitude's slots differ from the tilted ones */
    t_attitude(&ac, 2000, 1000, 500);
    flight_restart(&ac, FLIGHT_START_RUNWAY);           /* level again, but the slots are the last draw's */
    ASSERT_EQ_INT(memcmp(ac.f.vtx, crash, sizeof crash), 0);
    flight_inputs_t in; memset(&in, 0, sizeof in);
    t_step(&ac, &in);                              /* the step reads the old slots; its draw then refreshes them */
    ASSERT_TRUE(memcmp(ac.f.vtx, crash, sizeof crash) != 0);
}

/* 17.18 f and 17.14: the carrier start writes +0x0A and +0x0B from the position (demo carrier at cell (68, 68): 10 and 15), the runway start writes +0x0B = n and
 * leaves +0x0A; in free flight the carrier is record 14 at (295174144, 274202624), heading 0, and the player starts at +11264 / +106496 from it. */
TEST(the_starts_write_the_cell_bytes_as_17_18_f_and_follow_the_free_flight_carrier) {
    if (!have_models) { printf("skipped "); return; }
    flight_set_env(&env);
    t_init(&ac, FLIGHT_FA18, FLIGHT_START_CARRIER);
    ASSERT_EQ_INT(ac.f.cell_x, 68); ASSERT_EQ_INT(ac.f.cell_z, 68);
    ASSERT_EQ_INT(ac.f.cell_b, 15);
    ASSERT_EQ_INT(ac.f.cell_a, (4 * (3 - (ac.f.fine_z >> 12)) + (3 - (ac.f.fine_x >> 12))) & 0xFF);
    ASSERT_EQ_INT(ac.f.cell_a, 10);
    t_init(&ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    ASSERT_EQ_INT(ac.f.cell_b, 6);
    ASSERT_EQ_INT(ac.f.cell_a, 0);                      /* left alone (the fresh record's 0) */
    ac.f.cell_a = 9;
    flight_restart(&ac, FLIGHT_START_RUNWAY);
    ASSERT_EQ_INT(ac.f.cell_a, 9); ASSERT_EQ_INT(ac.f.cell_b, 6);
    /* the free-flight carrier */
    flight_env_t fenv = env;
    fenv.ship.pos[0] = 295174144; fenv.ship.pos[2] = 274202624;
    flight_ship_build(&fenv.ship, (const int16_t (*)[3])cv, FLIGHT_SHIP_SLOTS, 0, 0, 0);
    flight_set_env(&fenv);
    t_init(&ac, FLIGHT_FA18, FLIGHT_START_CARRIER);
    ASSERT_EQ_INT(ac.pos[0], 295174144 + 11264); ASSERT_EQ_INT(ac.pos[2], 274202624 + 106496); ASSERT_EQ_INT(ac.pos[1], 30472);
    ASSERT_EQ_INT(ac.f.cell_x, 70); ASSERT_EQ_INT(ac.f.cell_z, 65);
    ASSERT_EQ_INT(ac.f.cell_b, 9);
    ASSERT_EQ_INT(ac.f.heading, 0);
    flight_set_env(&env);
}

/* Carrier set-up for the game: the whole game steps the aircraft (flight mode of game.c). */
TEST(game_flight_mode_launches_from_the_catapult) {
    if (!have_models) { printf("skipped "); return; }
    char err[128];
    game_t *g = game_create(&loader, err, sizeof err);
    ASSERT_TRUE(g != NULL);
    game_event_t ev = { GAME_EV_BUTTON, GAME_BTN_RETURN, 1 };
    game_debug_start_flight(g, 3, 0);
    ev.kind = GAME_EV_AXIS; ev.code = GAME_AXIS_THROTTLE; ev.value = 32767;   /* full lever */
    game_input(g, &ev);
    game_scene_t sc;
    for (int i = 0; i < 90; i++) game_step(g);
    game_scene(g, &sc);
    ASSERT_EQ_INT(sc.mode, GAME_MODE_FLIGHT);
    ASSERT_TRUE(sc.hud.airspeed_kt > 20);          /* the catapult released at throttle 108 */
    ASSERT_TRUE(sc.hud.throttle_pct == 100);
    ASSERT_TRUE(sc.hud.altitude_ft > 0);
    game_destroy(g);
    flight_set_env(&env);
}

/* The core owns n, the arbiter and the H key (cockpit.h); the scene carries the raw cockpit block. */
/* Note 05 17.11 / 11b / 17.12: the ground test of a non-player aircraft reads the record's OWN slots 0 to 2 (slot), never the player-model vertices (vtx). */
TEST(the_ground_test_of_a_non_player_aircraft_reads_the_records_own_slots) {
    flight_env_t env; memset(&env, 0, sizeof env);
    flight_state_t f; memset(&f, 0, sizeof f);
    const int32_t prev[3] = { 0, 0, 0 };
    f.cls = 0x10; f.b7b = 0; f.l7d = 3; f.alt8 = 10; f.entity = 1;
    f.vtx[1][1] = -4000;                                   /* the player model's vertex: ignored by a record */
    ASSERT_EQ_INT(fl_collide(&f, &env, prev), 0);
    f.slot[2][1] = -100;                                   /* -100 >> 3 + 10 < 0 */
    ASSERT_EQ_INT(fl_collide(&f, &env, prev), 0x10);
    f.slot[2][1] = 0; f.slot[3][1] = -4000;               /* slot 3 is not a ground vertex */
    ASSERT_EQ_INT(fl_collide(&f, &env, prev), 0);
    f.entity = 0;                                          /* the player: vtx */
    ASSERT_EQ_INT(fl_collide(&f, &env, prev), 0x10);
}

TEST(game_publishes_the_cockpit_block_and_the_core_owns_n) {
    if (!have_models) { printf("skipped "); return; }
    char err[128];
    game_t *g = game_create(&loader, err, sizeof err);
    ASSERT_TRUE(g != NULL);
    game_event_t ev = { GAME_EV_BUTTON, GAME_BTN_RETURN, 1 };
    game_debug_start_flight(g, 3, 0);
    game_scene_t sc;
    game_scene(g, &sc);
    ASSERT_EQ_INT(sc.hud.cp_valid, 1);
    ASSERT_EQ_INT(sc.hud.cp_n, 0);
    ASSERT_EQ_INT(sc.hud.cp_hud_on, 1);
    ASSERT_EQ_INT(sc.hud.cp_class, 0x11);
    ASSERT_EQ_INT(sc.hud.cp_sel, 0x0D);
    for (int i = 0; i < 5; i++) game_step(g);
    game_scene(g, &sc);
    ASSERT_EQ_INT(sc.hud.cp_n, 4);                  /* n starts at 0 on the first flight pass, not at the splash frame count */
    ev.code = GAME_BTN_HUD_TOGGLE;
    game_input(g, &ev);
    game_scene(g, &sc);
    ASSERT_EQ_INT(sc.hud.cp_hud_on, 0);
    ev.code = GAME_BTN_PAUSE;
    game_input(g, &ev);
    for (int i = 0; i < 3; i++) game_step(g);
    game_scene(g, &sc);
    ASSERT_EQ_INT(sc.hud.cp_n, 4);                  /* the published n is the last drawn pass; paused passes do not advance it */
    game_input(g, &ev);
    game_step(g);
    game_scene(g, &sc);
    ASSERT_EQ_INT(sc.hud.cp_n, 5);                  /* no handler clears the low three bits of n on a pause (note 06 "View kinds, settled" item 8; 0x1EDF8 is unreachable): n 5 stands through the pause and the next pass draws it */
    game_destroy(g);
    flight_set_env(&env);
}

/* Assets extracted without the segment table carry the marker tables/flight_spans_flat.txt: the core refuses them with a clear error. */
static int flat_read(void *user, const char *path, uint8_t **out, size_t *len) {
    if (strcmp(path, "tables/flight_spans_flat.txt") == 0) { *out = (uint8_t *)malloc(1); *len = 0; return 0; }
    return fl_read(user, path, out, len);
}
TEST(assets_with_flat_spans_are_refused_with_a_clear_error) {
    game_loader_t flat = { flat_read, fl_release, NULL };
    flight_tables_t tt;
    char err[256] = "";
    ASSERT_EQ_INT(flight_tables_load(&tt, &flat, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "segment table") != NULL);
}

int main(int argc, char **argv) {
    root = argc > 1 ? argv[1] : "assets";
    char err[128] = "";
    if (flight_tables_load(&tables, &loader, err, sizeof err) != 0) {
        printf("test_flight: skipped (%s)\n", err);
        return 0;
    }
    memset(&env, 0, sizeof env);
    env.tab = &tables;
    env.landmark_z = FLIGHT_LANDMARK_Z;
    flight_set_env(&env);
    printf("test_flight:\n");
    RUN(e1_runway_idle);
    RUN(e2_level_flight_gear_extending);
    RUN(e3_pull_up_gear_extending);
    RUN(e9_level_flight_gear_up);
    RUN(e4_takeoff_roll);
    RUN(e7_stall);
    RUN(the_extracted_sine_table_equals_the_generated_one);
    RUN(pause_keeps_the_state_but_clears_the_proximity_bit);
    RUN(the_step_writes_the_viewed_cell_only_for_a_caller_without_a_frame);
    RUN(direct_posts_follow_the_rig_chain);
    RUN(an_out_of_control_player_still_rolls_and_yaws_with_its_keys_and_loses_only_the_speed_and_the_pitch);
    RUN(terrain_grid_index_uses_the_unsigned_column_byte);
    RUN(terrain_walk_stays_inside_the_extracted_words_for_every_reachable_cell);
    RUN(the_table_over_reads_answer_the_image_words_of_the_note);
    RUN(over_reads_beyond_the_image_spans_are_counted_not_guessed);
    RUN(start_sets_the_pointer_throttle_flag);
    RUN(e8_hard_touchdown);
    RUN(the_lift_off_touchdown_and_crash_writers_raise_their_events);
    RUN(stall_nose_drop_below_level_stores_a_negative_pitch_word_and_the_next_steps_read_the_spans);
    RUN(a_negative_pitch_word_takes_the_signed_branches);
    RUN(a_retract_progress_change_releases_w2_0020_at_nibble_6);
    RUN(w2_0020_is_cleared_by_nibble_6_alone_17_18_c);
    RUN(the_idle_shortcut_keeps_the_stored_displacement_17_18_b);
    RUN(the_throttle_notices_follow_v4a994_and_a_low_pointer_fires_preset_0);
    RUN(span_2_covers_the_composition_lookup_range);
    RUN(assets_with_flat_spans_are_refused_with_a_clear_error);
    build_env_models();
    RUN(e5_catapult_launch);
    RUN(e6_arrestor_catch);
    RUN(deck_idle_with_stick_back_nothing_happens);
    RUN(runway_idle_with_stick_back_nothing_happens);
    RUN(roll_through_360_wraps_in_the_angle_unit);
    RUN(loop_and_vertical_climb_keep_the_angles_in_domain);
    RUN(hud_figures_follow_the_note);
    RUN(panel_digits_are_caches_refreshed_on_the_cadence_of_note_06);
    RUN(landmark_z_constant_shifts_the_volume_by_its_value);
    RUN(flight_restart_writes_the_list_of_17_17_rule_4_and_keeps_the_rest);
    RUN(gimbal_guard_differences_wrap_as_words);
    RUN(the_vertex_slots_are_refreshed_after_the_step_and_the_restart_leaves_them);
    RUN(the_starts_write_the_cell_bytes_as_17_18_f_and_follow_the_free_flight_carrier);
    RUN(game_flight_mode_launches_from_the_catapult);
    RUN(the_ground_test_of_a_non_player_aircraft_reads_the_records_own_slots);
    RUN(game_publishes_the_cockpit_block_and_the_core_owns_n);
    if (world) { world_destroy(world); world = NULL; }   /* the E5/E6 models; leakcheck found them live at exit */
    TEST_MAIN_END();
}
