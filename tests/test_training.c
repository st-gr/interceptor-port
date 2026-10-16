/* tests/test_training.c - the training parts (src/core/training.c, docs/re/08-game-flow.md section 8).
 *
 * The tests use the note's values (the tables of 8.2, the thresholds of 8.5, the posts of 8.7) and invented, text-free assets; none needs the flight model: the
 * instructor's state machine is exercised against a toy plant (a few lines that turn the key byte into a roll or pitch change), and the sequencer by setting the
 * instructor's state byte to 0 ("the manoeuvre has finished", 8.4 step 5). With assets/ present a last group steps the instructor through the real flight model.
 * The posts are the warning-table entry numbers of 8.7 (no text is compared).
 */
#include "test.h"
#include "../src/core/training.h"
#include "../src/core/flight.h"
#include "../src/core/game.h"
#include "../src/core/entities.h"
#include "../src/core/screen.h"
#include "../src/core/mover.h"

/* ---- the tables of 8.2 (invented ids; the states are the first state of each manoeuvre's chain in the machine) ------------------------------ */
static const int16_t PROMPT[8] = { 0, 71, 74, 72, 70, 75, 73, 76 }, NAME[8] = { 0, 90, 95, 91, 94, 92, 96, 93 };   /* invented warning-entry numbers: the tests check the routing, not the disk's column */
static uint8_t lesson_state(int l) { return (uint8_t)(l == 0 ? 0x23 : l == 2 ? 0x27 : l == 1 ? 0x0B : 0x0B + 3 * (l - 2)); }   /* the machine's entry states: 8.5's chains */
static training_t tbl(void) {
    training_t t;
    memset(&t, 0, sizeof t);
    for (int i = 0; i < 8; i++) {
        t.a[i].prompt_id = PROMPT[i]; t.a[i].name_id = NAME[i];
        t.b[i].camera = (uint32_t)((0x0200 + 0x100 * (i % 2)) | (9 + i));   /* invented, different per row: the view 2 or 3 in byte 1, the direction 9 + i in byte 0 (row 4: view 2, direction 13) */
        t.b[i].script = 0x80000000u | lesson_state(i);
    }
    t.pitch_rate[4] = 320; t.pitch_rate[6] = 400; t.pitch_rate[7] = 480;
    t.roll_rate[7] = 720; t.roll_rate[8] = 800;
    return t;
}

static aircraft_t PLAYER;
static trainer_t T;
static void begin(int part) {
    const training_t tb = tbl();
    memset(&PLAYER, 0, sizeof PLAYER);
    PLAYER.f.v4a984 = 0x0840; PLAYER.f.v4a844 = 1; PLAYER.f.v4a847 = 1; PLAYER.f.v4a95e = (uint8_t)(part == 1 ? 2 : 0x7D);
    PLAYER.f.cls = 0x11; PLAYER.f.id = 0x0D; PLAYER.f.w0 = 0x11C8;
    training_init(&T, &tb);
    training_start(&T, part == 1 ? TRAINING_MODE_PART1 : TRAINING_MODE_PART2, &PLAYER);
}
static int last_post(void) { return T.npost ? T.posts[T.npost - 1] : -1; }

/* ---- a toy plant: what the key byte does to the instructor's attitude --------------------------------------------------------------------- */
typedef struct { int roll_step, pitch_step, pull_sign, snap_home; } plant_t;
static int wrap(int v) { v %= 28800; return v < 0 ? v + 28800 : v; }
static void plant(trainer_t *t, const plant_t *p) {
    flight_state_t *f = &t->inst.f;
    const int k = f->keys;
    int roll = (int16_t)f->roll, pitch = (int16_t)f->pitch;
    const int held = (f->w2 & 0x0010) != 0;   /* the recentre bit: the flight step centres the controls and clears the bit (note 05 17.7) */
    f->w2 &= (uint16_t)~0x0010;
    if (held) { /* the roll rate is 0 this step */ }
    else if (k & FLIGHT_KEY_ROLL_LEFT) roll += p->roll_step;
    else if (k & FLIGHT_KEY_ROLL_RIGHT) roll -= p->roll_step;
    else if (p->snap_home && roll >= 28160) roll = 0;
    if (held) f->rate_p = 0;
    else if (k & FLIGHT_KEY_STICK_BACK) { pitch += p->pull_sign * p->pitch_step; f->rate_p = -100; }
    else if (k & FLIGHT_KEY_STICK_FORWARD) { pitch -= p->pull_sign * p->pitch_step; f->rate_p = 100; }
    else f->rate_p = 0;
    f->roll = (uint16_t)wrap(roll); f->pitch = (uint16_t)wrap(pitch);
}

/* One step of the instructor as training_step does it, without the sequencer and the flight step: the counter, the state routine, the toy plant. */
static void tick(const plant_t *p) {
    flight_state_t *f = &T.inst.f;
    f->counter--;
    if (f->state) training_ai(&T);
    plant(&T, p);
}
/* Runs until the state byte is 0 (the wait is over) or n steps; fills the visited states in order (consecutive duplicates once) and returns the steps used. */
static int visited[96], nvisited, first_step[256], steps_in[256];
static int run(const plant_t *p, int maxsteps, int stop_state) {
    flight_state_t *f = &T.inst.f;
    int last = -1, n;
    nvisited = 0;
    memset(first_step, -1, sizeof first_step); memset(steps_in, 0, sizeof steps_in);
    for (n = 0; n < maxsteps; n++) {
        tick(p);
        steps_in[f->state]++;
        if (first_step[f->state] < 0) first_step[f->state] = n;
        if (f->state != last && nvisited < 96) visited[nvisited++] = last = f->state;
        if (f->state == stop_state) { n++; break; }
    }
    return n;
}
static void inst_reset(int state, int roll, int pitch, int32_t alt) {
    flight_state_t *f = &T.inst.f;
    memset(f, 0, sizeof *f);
    f->w0 = 0x11C8; f->w2 = 0x0100; f->cls = 0x10; f->state = (uint8_t)state; f->roll = (uint16_t)roll; f->pitch = (uint16_t)pitch;
    f->throttle = 0x60; f->speed = 8192;
    T.inst.pos[1] = alt;
    T.b64 = 0;
}
static int seq_is(const int *want, int n) {
    if (nvisited != n) return 0;
    for (int i = 0; i < n; i++) if (visited[i] != want[i]) return 0;
    return 1;
}

/* ---- the manoeuvres (8.5) ------------------------------------------------------------------------------------------------------------------- */

/* The copy rule of the part 2 placement (0x2CFDA): the first 0xA4 bytes of the player's record come along, +0x70 and +0x71 included; the part 1 head overwrite gives the template's 0 and 0xFF (08 8.3). */
TEST(the_part_2_placement_copies_the_players_plus_70_and_plus_71_and_part_1_writes_the_template_bytes) {
    const training_t tb = tbl();
    memset(&PLAYER, 0, sizeof PLAYER);
    PLAYER.f.cls = 0x11; PLAYER.f.w0 = 0x11C8;
    training_init(&T, &tb);
    T.src_b70 = 0x5A; T.src_b71 = 0x03;
    training_start(&T, TRAINING_MODE_PART2, &PLAYER);
    ASSERT_EQ_INT(T.b70, 0x5A); ASSERT_EQ_INT(T.b71, 0x03);
    ASSERT_EQ_INT(T.placed_new, 1);
    T.src_b70 = 0x11; T.src_b71 = 0x22;                       /* the player's bytes change; a re-placement copies the new ones, the instructor's own do not change on their own */
    training_start(&T, TRAINING_MODE_PART1, &PLAYER);         /* training_start keeps the caller's input over its clear */
    ASSERT_EQ_INT(T.src_b70, 0x11); ASSERT_EQ_INT(T.b70, 0); ASSERT_EQ_INT(T.b71, 0xFF);
    training_start(&T, TRAINING_MODE_PART2, &PLAYER);
    ASSERT_EQ_INT(T.b70, 0x11); ASSERT_EQ_INT(T.b71, 0x22);
}

TEST(the_aileron_roll_waits_15_steps_in_state_0C_then_rolls_left_and_ends_in_the_wait_state_36_steps_in_part_1) {
    begin(1);
    inst_reset(0x0B, 0, 0, 0x300000);
    const plant_t p = { 400, 0, 1, 1 };
    run(&p, 400, 0x0A);
    static const int want[] = { 0x0C, 0x0D, 0x0A };
    ASSERT_TRUE(seq_is(want, 3));
    ASSERT_EQ_INT(steps_in[0x0C], 15);                      /* "0x0C for 15 steps, then 0x0D with key 0x08" */
    ASSERT_EQ_INT(T.inst.f.counter, 36);                    /* the end routine: V:4A84B set */
    ASSERT_TRUE(T.b64 & 0x10);                              /* the epilogue ran in the roll states */
    /* in 0x0D the key byte is exactly roll left: take a fresh run and look at the first 0x0D step */
    inst_reset(0x0B, 0, 0, 0x300000);
    for (int i = 0; i < 16; i++) tick(&p);
    ASSERT_EQ_INT(T.inst.f.state, 0x0D);
    ASSERT_EQ_INT(T.inst.f.keys, 0x08);
}

TEST(the_aileron_roll_passes_through_both_phase_flags_and_a_stuck_stick_never_ends) {
    begin(1);
    inst_reset(0x0B, 0, 0, 0x300000);
    const plant_t p = { 400, 0, 1, 1 };
    int saw_f3 = 0, saw_f4_only_after_half = 0;
    for (int i = 0; i < 400 && T.inst.f.state != 0x0A; i++) {
        tick(&p);
        if (T.inst.f.b20 & 0x08) saw_f3 = 1;
        if ((T.inst.f.b20 & 0x10) && T.inst.f.roll >= 14400 - 400) saw_f4_only_after_half = 1;
    }
    ASSERT_TRUE(saw_f3);
    ASSERT_TRUE(saw_f4_only_after_half);
    inst_reset(0x0D, 0, 0, 0x300000);   /* the failing run: the plant ignores the stick */
    const plant_t stuck = { 0, 0, 1, 0 };
    run(&stuck, 600, 0x0A);
    ASSERT_EQ_INT(T.inst.f.state, 0x0D);
    ASSERT_EQ_INT(T.inst.f.keys, 0x08);
}

TEST(the_roll_command_keeps_the_throttle_bits_and_only_the_sign_matters) {
    begin(1);
    inst_reset(0x0D, 0, 0, 0x300000);
    T.inst.f.keys = 0xFF;
    T.inst.f.b20 = 0x00;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.keys, 0x08 | 0x03);
    inst_reset(0x02, 0, 0, 0x300000);   /* break turn: roll 0 < 14400, T = 7200: roll left */
    T.inst.f.keys = 0xFF;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.keys, 0x08 | 0x03);
    inst_reset(0x02, 28000, 0, 0x300000);   /* roll >= 14400: T = 21600, roll above it: roll right */
    T.inst.f.keys = 0xFF;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.keys, 0x04 | 0x03);
}

TEST(the_break_turn_banks_to_90_degrees_pulls_for_32_steps_and_ends) {
    begin(1);
    inst_reset(0x27, 0, 0, 0x300000);
    const plant_t p = { 400, 0, 1, 0 };
    /* the pull has no pitch plant here: the pitch rate stays 0, so -320 < 0 keeps the stick back */
    int n = run(&p, 400, 0x0A);
    static const int want[] = { 0x28, 2, 7, 0x0A };
    ASSERT_TRUE(seq_is(want, 4));
    ASSERT_EQ_INT(steps_in[0x28], 15);
    ASSERT_TRUE(n > 15 + 10 + 32);
    ASSERT_EQ_INT(T.inst.f.counter, 36);
    /* entering state 7 sets the counter to 32 and ANDs the key byte with 3 */
    inst_reset(0x02, 6800, 0, 0x300000);   /* E = 400 < 1200 */
    T.inst.f.keys = 0xFF;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 7);
    ASSERT_EQ_INT(T.inst.f.counter, 32);
    ASSERT_EQ_INT(T.inst.f.keys, 0x03);
}

TEST(the_break_turn_error_is_taken_in_word_arithmetic) {
    begin(1);
    /* E >= 1200 gives a roll command, E < 1200 state 7; roll just above and below the target 7200 */
    inst_reset(0x02, 7200 - 1199, 0, 0x300000);
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 7);
    inst_reset(0x02, 7200 - 1200, 0, 0x300000);
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 2);
    inst_reset(0x02, 7200 + 1200, 0, 0x300000);
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 2);
    ASSERT_EQ_INT(T.inst.f.keys & 0xFC, 0x04);   /* above the target: roll right */
}

TEST(the_inside_loop_pulls_through_the_top_and_ends_when_the_pitch_word_passes_14400_the_second_time) {
    begin(1);
    inst_reset(0x0E, 0, 0, 0x300000);
    const plant_t p = { 0, 400, -1, 0 };   /* pull lowers the pitch word (nose up): 0 wraps to 28400, down through 14400 to 0 and wraps again */
    run(&p, 800, 0x0A);
    static const int want[] = { 0x0F, 0x10, 0x0A };
    ASSERT_TRUE(seq_is(want, 3));
    ASSERT_EQ_INT(steps_in[0x0F], 15);
    ASSERT_TRUE(T.inst.f.b20 & 0x10);
    /* the three phases of 0x10 */
    inst_reset(0x10, 100, 0, 0x300000);
    T.inst.f.rate_p = 0;
    training_ai(&T);                                    /* f3 and f4 clear, pitch <= 14400: pull, flags unchanged */
    ASSERT_EQ_INT(T.inst.f.b20 & 0x18, 0);
    ASSERT_EQ_INT(T.inst.f.keys, 0x20);
    inst_reset(0x10, 100, 14401, 0x300000);
    training_ai(&T);                                    /* pitch above 14400: f3 */
    ASSERT_EQ_INT(T.inst.f.b20 & 0x18, 0x08);
    inst_reset(0x10, 100, 14400, 0x300000);
    T.inst.f.b20 = 0x08;
    training_ai(&T);                                    /* f3 set, pitch not above 14400: f4 */
    ASSERT_EQ_INT(T.inst.f.b20 & 0x18, 0x18);
    inst_reset(0x10, 100, 14401, 0x300000);
    T.inst.f.b20 = 0x18;
    training_ai(&T);                                    /* f4 set, pitch above 14400: the manoeuvre ends */
    ASSERT_EQ_INT(T.inst.f.state, 0x0A);
}

TEST(the_inside_loop_with_a_plant_that_never_pulls_does_not_end) {
    begin(1);
    inst_reset(0x0E, 0, 0, 0x300000);
    const plant_t stuck = { 0, 0, -1, 0 };
    run(&stuck, 500, 0x0A);
    ASSERT_EQ_INT(T.inst.f.state, 0x10);
}

TEST(the_split_s_is_refused_at_or_below_5000_feet_and_otherwise_rolls_to_exactly_180_degrees_into_the_loop_pull) {
    begin(1);
    inst_reset(0x11, 0, 0, 0x100000);                  /* 0x100000: refused at once */
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x0A);
    ASSERT_EQ_INT(T.inst.f.counter, 36);
    ASSERT_TRUE(!(T.b64 & 0x10));                      /* no epilogue after the end routine */
    inst_reset(0x11, 0, 0, 0x100001);
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x12);
    ASSERT_EQ_INT(T.inst.f.counter, 15);
    inst_reset(0x11, 0, 0, 0x300000);
    const plant_t p = { 800, 400, -1, 0 };   /* 800 per step lands on 14400 exactly (18 steps) without stopping in the 13760..14399 band */
    run(&p, 600, 0x0A);
    static const int want[] = { 0x12, 0x13, 0x0F, 0x10, 0x0A };
    ASSERT_TRUE(seq_is(want, 5));
    ASSERT_EQ_INT(steps_in[0x0F], 2);                  /* state 0x0F with the counter 2: recentre while it is above 0 */
    /* roll within 172 degrees of the target stops the roll */
    inst_reset(0x13, 13760, 0, 0x300000);
    T.inst.f.b20 = 0x08;
    T.inst.f.keys = 0x0F;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.keys, 0x03);
    ASSERT_TRUE(T.inst.f.w2 & 0x0010);                 /* recentre */
    inst_reset(0x13, 13759, 0, 0x300000);
    T.inst.f.b20 = 0x08;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.keys, 0x08);
}

TEST(the_vertical_half_loop_pulls_to_the_top_then_rolls_upright_in_state_0D) {
    begin(1);
    inst_reset(0x14, 0, 0, 0x300000);
    const plant_t p = { 400, 400, -1, 1 };
    int ok = 0;
    for (int i = 0; i < 300; i++) {
        tick(&p);
        if (T.inst.f.state == 0x0D) {
            ok = 1;
            ASSERT_TRUE(T.inst.f.b20 & 0x08);          /* f3 still set: the roll of 0x0D starts in its second phase */
            break;
        }
    }
    ASSERT_TRUE(ok);
    inst_reset(0x14, 0, 0, 0x300000);
    T.inst.f.b20 = 0x18;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.b20 & 0x18, 0x10);          /* only f3 is cleared */
}

TEST(the_barrel_roll_rolls_and_pulls_together_and_ends_when_the_roll_returns_below_180_degrees) {
    begin(1);
    inst_reset(0x17, 0, 0, 0x300000);
    const plant_t p = { 400, 100, -1, 1 };
    run(&p, 400, 0x0A);
    static const int want[] = { 0x18, 0x19, 0x0A };
    ASSERT_TRUE(seq_is(want, 3));
    /* both commands in one key byte: roll left and stick back, the throttle bits kept */
    inst_reset(0x19, 100, 0, 0x300000);
    T.inst.f.keys = 0x03;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.keys, 0x08 | 0x20 | 0x03);
    ASSERT_TRUE(T.inst.f.b20 & 0x08);                  /* f3 on the first step below 14400 */
    inst_reset(0x19, 15000, 0, 0x300000);
    T.inst.f.b20 = 0x08;
    training_ai(&T);
    ASSERT_TRUE(T.inst.f.b20 & 0x10);                  /* f4 on the first step at or above it */
    inst_reset(0x19, 100, 0, 0x300000);
    T.inst.f.b20 = 0x18;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x0A);               /* f4 set and roll < 14400: ends */
}

TEST(inverted_flight_rolls_to_180_degrees_holds_pulls_and_pushes_back_into_the_half_loop_states) {
    begin(1);
    inst_reset(0x1A, 0, 0, 0x300000);
    const plant_t p = { 800, 200, 1, 0 };
    int n = 0;
    nvisited = 0;
    {
        int last = -1;
        for (; n < 600; n++) {
            tick(&p);
            if (T.inst.f.state != last) { if (nvisited < 96) visited[nvisited++] = last = T.inst.f.state; }
            if (T.inst.f.state == 0x16) break;
        }
    }
    static const int want[] = { 0x1B, 0x1C, 0x1E, 0x1F, 0x21, 0x22, 0x16 };
    ASSERT_TRUE(seq_is(want, 7));
    /* 0x1C at exactly 14400: f4 cleared, state 0x1E, counter 10 */
    inst_reset(0x1C, 14400, 0, 0x300000);
    T.inst.f.b20 = 0x18;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x1E);
    ASSERT_EQ_INT(T.inst.f.counter, 10);
    ASSERT_EQ_INT(T.inst.f.b20 & 0x10, 0);
    /* 0x1F, band A (roll 7200..21600): done when the pitch word reaches 2400; band B: done below 26400 */
    inst_reset(0x1F, 14400, 2400, 0x300000);
    T.inst.f.b20 = 0x10;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x21);
    ASSERT_EQ_INT(T.inst.f.counter, 25);
    inst_reset(0x1F, 14400, 2399, 0x300000);
    T.inst.f.b20 = 0x10;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x1F);
    inst_reset(0x1F, 100, 26399, 0x300000);
    T.inst.f.b20 = 0x10;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x21);
    /* 0x22 done: f3 set and state 0x16 */
    inst_reset(0x22, 14400, 26400, 0x300000);
    T.inst.f.b20 = 0x10;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x16);
    ASSERT_TRUE(T.inst.f.b20 & 0x08);
}

TEST(the_take_off_climbs_the_throttle_to_60_hex_waits_60_steps_pulls_to_25_degrees_and_ends_at_the_altitude) {
    begin(2);
    inst_reset(0x23, 0, 0, 0x1000);
    T.inst.f.throttle = 9;
    T.inst.f.speed = 0;
    T.inst.f.keys = 0;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.keys & 3, 1);                 /* throttle up while below 0x60 */
    T.inst.f.throttle = 0x60;
    T.inst.f.keys = 0x03;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.keys & 3, 0);                 /* both bits cleared at 0x60 */
    T.inst.f.speed = 0x83F;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x23);
    T.inst.f.speed = 0x840;                              /* about 176 knots */
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x24);
    ASSERT_EQ_INT(T.inst.f.counter, 60);
    int n = 0;                                           /* 0x24: nothing until the counter is negative */
    while (T.inst.f.state == 0x24 && n < 200) { T.inst.f.counter--; training_ai(&T); n++; }
    ASSERT_EQ_INT(n, 61);
    ASSERT_EQ_INT(T.inst.f.state, 0x25);
    ASSERT_EQ_INT(T.inst.f.b20 & 0x10, 0);
    /* 0x25: pull (rate -40) while pitch < 14400; after f4 pull while pitch >= 26800; done: key byte 0, state 0x26 */
    T.inst.f.rate_p = 0;
    T.inst.f.pitch = 0;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.keys, 0x20);
    T.inst.f.pitch = 28000;
    training_ai(&T);
    ASSERT_TRUE(T.inst.f.b20 & 0x10);
    ASSERT_EQ_INT(T.inst.f.keys, 0x20);
    T.inst.f.pitch = 26799;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x26);
    ASSERT_EQ_INT(T.inst.f.keys, 0);
    T.inst.pos[1] = 0x240000;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x26);                 /* not above yet */
    T.inst.pos[1] = 0x240001;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x0A);
    ASSERT_EQ_INT(T.inst.f.counter, 60);                 /* part 2: V:4A84B clear */
}

TEST(the_wait_state_0A_ends_when_the_counter_is_negative_and_leaves_only_the_throttle_bits) {
    begin(1);
    inst_reset(0x0A, 0, 0, 0x300000);
    T.inst.f.keys = 0xFF;
    T.inst.f.counter = 0;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x0A);
    T.inst.f.counter = -1;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0);
    ASSERT_EQ_INT(T.inst.f.keys, 0x03);
    ASSERT_TRUE(!(T.b64 & 0x10));                        /* no epilogue in the wait */
}

TEST(the_pitch_command_compares_the_target_rate_with_the_pitch_rate) {
    begin(1);
    inst_reset(0x07, 0, 0, 0x300000);                    /* state 7 pulls at -320 */
    T.inst.f.counter = 5; T.inst.f.rate_p = -321;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.keys & 0x30, 0);              /* already pulling faster */
    T.inst.f.rate_p = -320;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.keys & 0x30, 0);              /* equal: no stick bit */
    T.inst.f.rate_p = -319;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.keys & 0x30, 0x20);
    inst_reset(0x25, 0, 0, 0x1000);                      /* pitch command(0) at the end of 0x25 pushes when the rate is negative */
    T.inst.f.b20 = 0x10;
    T.inst.f.pitch = 26799;
    T.inst.f.rate_p = -5;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x26);
}

/* ---- the sequencer (8.4) -------------------------------------------------------------------------------------------------------------------- */

TEST(part_1_starts_at_the_note_position_and_lesson_0_is_skipped_into_lesson_1) {
    begin(1);
    ASSERT_EQ_INT(T.inst.pos[0], 307757056);
    ASSERT_EQ_INT(T.inst.pos[1], 3145728);
    ASSERT_EQ_INT(T.inst.pos[2], 328728576);
    ASSERT_EQ_INT(T.inst.f.speed, 8192);
    ASSERT_EQ_INT(T.inst.f.speed_target, 8192);
    ASSERT_EQ_INT(T.inst.f.w0, 0x11C8);
    ASSERT_EQ_INT(T.inst.f.group, 4);
    ASSERT_EQ_INT(T.inst.f.cls, 0x10);
    ASSERT_EQ_INT(T.inst.f.id, 0x3D);
    ASSERT_TRUE(T.inst.f.w2 & 0x0100);
    ASSERT_EQ_INT(T.inst.f.throttle, 0);
    ASSERT_EQ_INT(T.lesson, 0);
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.lesson, 1);
    ASSERT_EQ_INT(T.inst.f.throttle, 0x60);
    ASSERT_EQ_INT(PLAYER.f.v4a968, 0xFE);               /* the centre-screen notice selector */
    ASSERT_EQ_INT(T.npost, 1);
    ASSERT_EQ_INT(T.posts[0], NAME[1]);                      /* the name of the aileron roll */
    ASSERT_EQ_INT(T.inst.f.state, 0x0C);                /* the sequencer gave 0x0B and the state routine ran in the same pass: the set-up pair falls into 0x0C */
    ASSERT_EQ_INT(T.inst.f.counter, 15);
    ASSERT_EQ_INT(T.cursor, 1);
    ASSERT_EQ_INT(T.out_ring, 13);                       /* the camera line of the table B row of the entity index 4 */
    ASSERT_EQ_INT(T.out_view, 2);
    ASSERT_TRUE(T.out_redraw);
}

TEST(part_1_runs_the_seven_lessons_in_order_with_the_names_of_8_7_and_wraps_to_lesson_1_with_a_re_placement) {
    begin(1);
    const int16_t *names = NAME + 1;
    static const int states[7] = { 0x0C, 0x28, 0x0F, 0x12, 0x15, 0x18, 0x1B };   /* the second state of each set-up pair: the pass that starts a lesson runs it */
    training_step(&T, &PLAYER);
    for (int k = 0; k < 7; k++) {
        ASSERT_EQ_INT(T.lesson, k + 1);
        ASSERT_EQ_INT(T.inst.f.state, states[k]);
        ASSERT_EQ_INT(last_post(), names[k]);
        T.inst.pos[1] = 0x300000;                       /* high enough for the split-S */
        T.inst.f.state = 0;                             /* the manoeuvre has finished */
        const int placed = T.placements;
        training_clear_outputs(&T);
        training_step(&T, &PLAYER);
        if (k < 6) ASSERT_EQ_INT(T.placements, placed);
        else { ASSERT_EQ_INT(T.placements, placed + 1); ASSERT_EQ_INT(T.inst.pos[1], 3145728); }
    }
    ASSERT_EQ_INT(T.lesson, 1);                         /* 8 wraps to 1 */
    ASSERT_EQ_INT(T.inst.f.state, 0x0C);
    ASSERT_EQ_INT(last_post(), NAME[1]);
}

TEST(the_sequencer_leaves_the_state_alone_while_a_manoeuvre_runs_and_waits_for_the_end_of_the_wait) {
    begin(1);
    training_step(&T, &PLAYER);                         /* lesson 1, state 0x0C */
    const int lesson = T.lesson;
    for (int i = 0; i < 20; i++) training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.lesson, lesson);
    ASSERT_TRUE(T.inst.f.state != 0);
    T.inst.f.state = 0x0A; T.inst.f.counter = 5;        /* the wait: the state is not 0 yet */
    training_clear_outputs(&T);
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.lesson, lesson);
    ASSERT_EQ_INT(T.npost, 0);
}

TEST(the_sequencer_does_nothing_while_paused_and_in_part_1_until_the_flight_is_running) {
    begin(1);
    T.paused = 1;
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.lesson, 0);
    T.paused = 0; T.running = 0;
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.lesson, 0);                         /* V:4A84B gates the sequencer of part 1 */
    T.running = 1;
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.lesson, 1);
    PLAYER.f.v4a866 = 1;                                /* the game's pause byte */
    T.inst.f.state = 0;
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.lesson, 1);
}

TEST(the_split_s_refusal_notice_replaces_the_name_when_the_instructor_is_at_or_below_0x100000) {
    begin(1);
    training_step(&T, &PLAYER);
    T.lesson = 3; T.inst.f.state = 0; T.cursor = 1;
    T.inst.pos[1] = 0x0F0000;
    training_clear_outputs(&T);
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.lesson, 4);
    ASSERT_EQ_INT(T.npost, 1);
    ASSERT_EQ_INT(T.posts[0], 53);
    ASSERT_EQ_INT(T.inst.f.state, 0x0A);                /* state 0x11 refuses at once: the end routine ran in the same pass */
    ASSERT_EQ_INT(T.inst.f.counter, 36);
    T.lesson = 3; T.inst.f.state = 0; T.cursor = 1;
    T.inst.pos[1] = 0x100000;                           /* the test is signed and includes the value: the notice is posted at 0x100000 itself (8.4 step 7, 8.7) */
    training_clear_outputs(&T);
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.posts[0], 53);
    T.lesson = 3; T.inst.f.state = 0; T.cursor = 1;
    T.inst.pos[1] = 0x100001;
    training_clear_outputs(&T);
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.posts[0], NAME[4]);                      /* the name of the split-S */
}

TEST(return_in_part_1_re_places_the_instructor_and_skips_to_the_next_lesson) {
    begin(1);
    training_step(&T, &PLAYER);                         /* lesson 1 */
    T.inst.f.state = 0x0D;
    T.inst.pos[0] = 1; T.inst.pos[1] = 2; T.inst.pos[2] = 3;
    T.inst.f.pitch = 4000; T.inst.f.roll = 5000;
    training_key_return(&T, &PLAYER);
    ASSERT_TRUE(PLAYER.f.w2 & 0x0800);
    training_clear_outputs(&T);
    const int placed = T.placements;
    training_step(&T, &PLAYER);
    ASSERT_TRUE(!(PLAYER.f.w2 & 0x0800));
    ASSERT_EQ_INT(T.placements, placed + 1);
    ASSERT_EQ_INT(T.lesson, 2);
    ASSERT_EQ_INT(T.posts[T.npost - 1], NAME[2]);
    ASSERT_EQ_INT(T.inst.pos[0], 307757056);            /* back at the start position, attitude 0 */
    ASSERT_EQ_INT(T.inst.pos[1], 3145728);
    ASSERT_EQ_INT(T.inst.pos[2], 328728576);
    ASSERT_EQ_INT(T.inst.f.roll, 0);
    ASSERT_EQ_INT(T.inst.f.pitch, 0);
    ASSERT_EQ_INT(T.inst.f.state, 0x28);
    ASSERT_EQ_INT(T.inst.f.throttle, 0x60);
}

/* ---- part 2: the lead, the prompts, RETURN and the trigger (8.4 steps 1 and 6, 8.6) ---------------------------------------------------------- */

TEST(part_2_places_the_lead_72_model_units_ahead_with_the_players_attitude_and_waits_for_the_player_to_move) {
    begin(2);
    ASSERT_EQ_INT(T.part, 2);
    ASSERT_TRUE(!(T.inst.f.w2 & 0x0100));
    ASSERT_EQ_INT(T.inst.f.cls, 0x10);
    ASSERT_EQ_INT(T.inst.f.group, 4);
    ASSERT_EQ_INT(T.placements, 1);
    T.inst.f.state = 0x55;
    for (int i = 0; i < 5; i++) training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.inst.f.state, 0);                   /* cleared each step while the player stands */
    ASSERT_TRUE(!(T.inst.f.w2 & 0x0100));
    ASSERT_EQ_INT(T.lesson, 0);
    PLAYER.f.speed = 1;                                 /* the indicated speed is not zero */
    training_step(&T, &PLAYER);
    ASSERT_TRUE(T.inst.f.w2 & 0x0100);
    ASSERT_EQ_INT(T.inst.f.state, 0x23);                /* lesson 0: the take-off starts in the same pass (rig) */
    ASSERT_EQ_INT(T.lesson, 0);
    ASSERT_EQ_INT(T.cursor, 1);
}

TEST(a_finished_take_off_posts_the_prompt_of_lesson_1_and_the_trigger_starts_the_lesson_after_it) {
    begin(2);
    PLAYER.f.speed = 1;
    training_step(&T, &PLAYER);
    training_step(&T, &PLAYER);                         /* state 0x23 */
    T.inst.f.state = 0;                                 /* the take-off is over */
    training_clear_outputs(&T);
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.trig, 0xFF);
    ASSERT_EQ_INT(T.npost, 1);
    ASSERT_EQ_INT(T.posts[0], PROMPT[1]);                      /* the prompt of the aileron roll */
    training_clear_outputs(&T);
    for (int i = 0; i < 5; i++) training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.npost, 0);                          /* the prompt is posted once */
    ASSERT_EQ_INT(T.lesson, 0);
    training_key_fire(&T, &PLAYER);                     /* the trigger */
    ASSERT_TRUE(T.inst.f.w2 & 0x0800);
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.lesson, 1);
    ASSERT_EQ_INT(T.trig, 0);
    ASSERT_EQ_INT(T.inst.f.state, 0x0C);
    ASSERT_EQ_INT(last_post(), NAME[1]);
    ASSERT_TRUE(!(T.inst.f.w2 & 0x0800));
}

TEST(the_fire_key_has_no_training_effect_while_a_manoeuvre_runs_or_when_the_player_ejects) {
    begin(2);
    PLAYER.f.speed = 1;
    training_step(&T, &PLAYER);
    training_key_fire(&T, &PLAYER);
    ASSERT_TRUE(!(T.inst.f.w2 & 0x0800));               /* V:4A852 = 0 */
    T.trig = 0xFF;
    PLAYER.f.gear = 0x0A;                               /* the ejection nibble */
    training_key_fire(&T, &PLAYER);
    ASSERT_TRUE(!(T.inst.f.w2 & 0x0800));
    PLAYER.f.gear = 0;
    begin(1);
    T.trig = 0xFF;
    training_key_fire(&T, &PLAYER);
    ASSERT_TRUE(!(T.inst.f.w2 & 0x0800));               /* none of the training use in part 1 */
}

TEST(return_with_a_manoeuvre_running_posts_the_not_ready_notice_and_changes_nothing_else) {
    begin(2);
    PLAYER.f.speed = 1;
    training_step(&T, &PLAYER);
    training_clear_outputs(&T);
    training_key_return(&T, &PLAYER);
    ASSERT_EQ_INT(T.npost, 1);
    ASSERT_EQ_INT(T.posts[0], 0x401F);
    ASSERT_TRUE(!(PLAYER.f.w2 & 0x0800));
    training_player_pass(&T, &PLAYER);
    ASSERT_EQ_INT(T.placements, 1);
}

TEST(the_re_position_puts_the_player_9_right_1_up_and_97_behind_the_instructor_99760_units_away) {
    begin(2);
    flight_state_t *i = &T.inst.f;
    memset(i->mat_b, 0, sizeof i->mat_b);
    i->mat_b[0] = i->mat_b[4] = i->mat_b[8] = 16384;    /* attitude 0: the identity in Q14 */
    T.inst.pos[0] = 200000000; T.inst.pos[1] = 3145728; T.inst.pos[2] = 300000000;
    i->throttle = 0x60; i->speed = 8192; i->id = 0x3D; i->group = 4; i->cls = 0x10; i->w2 |= 0x0100;
    PLAYER.f.id = 0x0D; PLAYER.f.v4a910 = 0;
    T.trig = 0xFF;
    training_key_return(&T, &PLAYER);
    ASSERT_TRUE(PLAYER.f.w2 & 0x0800);
    training_clear_outputs(&T);
    training_player_pass(&T, &PLAYER);
    ASSERT_EQ_INT(PLAYER.pos[0] - T.inst.pos[0], 9 * 1024);
    ASSERT_EQ_INT(PLAYER.pos[1] - T.inst.pos[1], 1 * 1024);
    ASSERT_EQ_INT(PLAYER.pos[2] - T.inst.pos[2], -97 * 1024);
    const double dx = 9216.0, dy = 1024.0, dz = 99328.0;
    const long long d2 = (long long)(dx * dx + dy * dy + dz * dz);
    ASSERT_TRUE(d2 > 99759LL * 99759LL + 99759LL && d2 < 99760LL * 99760LL + 99760LL);   /* 99,760 position units */
    ASSERT_TRUE(!(PLAYER.f.w2 & 0x0800));
    ASSERT_EQ_INT(PLAYER.f.cls, 0x11);                  /* the F/A-18 class whatever aircraft was chosen */
    ASSERT_EQ_INT(PLAYER.f.id, 0x0D);                   /* the id is restored */
    ASSERT_EQ_INT(PLAYER.f.group, 0);
    ASSERT_TRUE(!(PLAYER.f.w2 & 0x0100));
    ASSERT_EQ_INT(PLAYER.f.state, 0);
    ASSERT_EQ_INT(PLAYER.f.keys, 0);
    ASSERT_EQ_INT(PLAYER.f.throttle, 0x60);             /* the instructor's throttle is the player's now */
    ASSERT_EQ_INT(PLAYER.f.speed, 8192);
    ASSERT_EQ_INT(PLAYER.f.v4a910, 0xFF);
    ASSERT_EQ_INT(PLAYER.f.cell_x, (int16_t)(PLAYER.pos[0] >> 16) >> 6);   /* the cells are derived from the position */
    ASSERT_EQ_INT(T.trig, 0xFE);
    ASSERT_EQ_INT(T.lesson, 0);                         /* the first RETURN after a prompt only re-positions */
    ASSERT_EQ_INT(T.npost, 1);
    ASSERT_EQ_INT(T.posts[0], PROMPT[1]);                      /* the prompt of lesson 1 again */
}

TEST(the_re_position_offset_goes_through_the_instructors_matrix) {
    begin(2);
    flight_state_t *i = &T.inst.f;
    /* a heading of 90 degrees: the matrix words of a yaw: x' = z (the model-space z, behind, becomes x), z' = -x */
    memset(i->mat_b, 0, sizeof i->mat_b);
    i->mat_b[1] = 16384; i->mat_b[5] = 0; i->mat_b[2] = 16384; i->mat_b[4] = 16384; i->mat_b[6] = -16384;
    T.inst.pos[0] = 100000000; T.inst.pos[1] = 5000000; T.inst.pos[2] = 100000000;
    T.trig = 0xFE;
    training_key_return(&T, &PLAYER);
    training_player_pass(&T, &PLAYER);
    /* x' = (M0 * 9 + M1 * 1 + M2 * -97) >> 4 = (0 + 16384 - 97 * 16384) >> 4 */
    ASSERT_EQ_INT(PLAYER.pos[0] - T.inst.pos[0], (16384 - 97 * 16384) >> 4);
    ASSERT_EQ_INT(PLAYER.pos[1] - T.inst.pos[1], (16384 * 1) >> 4);             /* row 1: only M4 */
    ASSERT_EQ_INT(PLAYER.pos[2] - T.inst.pos[2], (-16384 * 9) >> 4);            /* row 2: M6 * 9 */
}

TEST(the_return_key_reports_whether_it_set_the_request_the_game_beeps_then_0x1e8c2) {
    begin(1);
    ASSERT_EQ_INT(training_key_return(&T, &PLAYER), 1);   /* part 1 (mode 2): the beep and the request */
    ASSERT_TRUE(PLAYER.f.w2 & 0x0800);
    begin(2);
    T.trig = 0x00;                                         /* V:4A852 not negative: the not-ready notice, no beep and no request */
    PLAYER.f.w2 &= (uint16_t)~0x0800;
    ASSERT_EQ_INT(training_key_return(&T, &PLAYER), 0);
    ASSERT_TRUE(!(PLAYER.f.w2 & 0x0800));
    T.trig = 0xFF;                                         /* negative: accepted */
    ASSERT_EQ_INT(training_key_return(&T, &PLAYER), 1);
    ASSERT_TRUE(PLAYER.f.w2 & 0x0800);
}

TEST(each_further_return_skips_one_lesson_and_the_prompt_follows_the_number) {
    begin(2);
    T.inst.f.w2 |= 0x0100;
    T.trig = 0xFF;
    training_key_return(&T, &PLAYER);
    training_clear_outputs(&T);
    training_player_pass(&T, &PLAYER);                  /* the first: re-position only */
    ASSERT_EQ_INT(T.lesson, 0);
    ASSERT_EQ_INT(T.posts[0], PROMPT[1]);
    training_key_return(&T, &PLAYER);                   /* V:4A852 = 0xFE: negative, accepted */
    ASSERT_TRUE(PLAYER.f.w2 & 0x0800);
    training_clear_outputs(&T);
    training_player_pass(&T, &PLAYER);
    ASSERT_EQ_INT(T.lesson, 1);                         /* lesson 1 skipped */
    ASSERT_EQ_INT(T.posts[0], PROMPT[2]);                      /* the prompt of the break turn */
    ASSERT_EQ_INT(T.trig, 0xFE);
    for (int k = 2; k <= 7; k++) {
        training_key_return(&T, &PLAYER);
        training_clear_outputs(&T);
        training_player_pass(&T, &PLAYER);
        ASSERT_EQ_INT(T.lesson, k);
    }
    ASSERT_EQ_INT(T.posts[0], PROMPT[1]);                      /* after lesson 7 the prompt of lesson 1 */
    training_key_return(&T, &PLAYER);
    training_player_pass(&T, &PLAYER);
    ASSERT_EQ_INT(T.lesson, 1);                         /* 8 becomes 1 */
    /* the trigger now starts lesson 2 and posts the name of the break turn */
    T.inst.f.state = 0; T.cursor = 1;
    training_key_fire(&T, &PLAYER);
    training_clear_outputs(&T);
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.lesson, 2);
    ASSERT_EQ_INT(last_post(), NAME[2]);
    ASSERT_EQ_INT(T.inst.f.state, 0x28);
}

TEST(after_lesson_7_the_next_trigger_re_places_the_lead_ahead_of_the_player_and_starts_lesson_1) {
    begin(2);
    T.inst.f.w2 |= 0x0100;
    T.lesson = 7; T.cursor = 1; T.inst.f.state = 0; T.trig = 0xFF;
    PLAYER.pos[0] = 50000000; PLAYER.pos[1] = 123456; PLAYER.pos[2] = 60000000;
    PLAYER.f.heading = 0; PLAYER.f.w0 = 0x11C8;
    T.inst.pos[0] = 0; T.inst.pos[2] = 0;
    training_key_fire(&T, &PLAYER);
    const int placed = T.placements;
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.placements, placed + 1);
    ASSERT_EQ_INT(T.lesson, 1);
    ASSERT_EQ_INT(T.inst.f.state, 0x0C);
    ASSERT_EQ_INT(T.inst.pos[1], 123456);               /* no vertical offset */
    ASSERT_EQ_INT(T.inst.f.group, 4);
    ASSERT_EQ_INT(T.inst.f.throttle, 0x60);
}

TEST(the_lead_is_placed_along_the_players_heading_with_no_vertical_offset) {
    begin(2);
    ASSERT_EQ_INT(T.inst.pos[0], PLAYER.pos[0]);        /* without an installed flight environment the sine table is absent: the offset is 0 */
    ASSERT_EQ_INT(T.inst.pos[1], PLAYER.pos[1]);
}

TEST(the_smoke_key_toggles_the_instructors_trail_bit_in_part_2_only) {
    begin(2);
    training_key_smoke(&T);
    ASSERT_TRUE(T.inst.f.w2 & 0x1000);
    training_key_smoke(&T);
    ASSERT_TRUE(!(T.inst.f.w2 & 0x1000));
    begin(1);
    training_key_smoke(&T);
    ASSERT_TRUE(!(T.inst.f.w2 & 0x1000));
}

TEST(the_end_wait_is_36_steps_in_part_1_and_60_in_part_2_at_the_instructors_clock) {
    begin(1);
    inst_reset(0x0D, 100, 0, 0x300000);
    T.inst.f.b20 = 0x18;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x0A);
    ASSERT_EQ_INT(T.inst.f.counter, 36);
    begin(2);
    inst_reset(0x0D, 100, 0, 0x300000);
    T.inst.f.b20 = 0x18;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.counter, 60);
}

TEST(the_instructor_state_machine_runs_only_for_a_scripted_active_instructor_with_the_simulation_on) {
    begin(1);
    training_step(&T, &PLAYER);                         /* lesson 1 */
    T.inst.f.state = 0x0B;
    T.inst.f.w2 &= (uint16_t)~0x0100;                   /* not scripted: no state routine */
    T.inst.f.counter = 40;
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.inst.f.state, 0x0B);
    T.inst.f.w2 |= 0x0100;
    PLAYER.f.v4a984 = 0;                                /* simulation off */
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.inst.f.state, 0x0B);
    PLAYER.f.v4a984 = 0x0840;
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.inst.f.state, 0x0C);
    ASSERT_EQ_INT(T.inst.f.counter, 15);
}

TEST(the_counter_is_decremented_once_per_step_before_the_state_routine) {
    begin(1);
    training_step(&T, &PLAYER);                         /* the sequencer sets 0x0B and the state routine runs 0x0B and 0x0C in the same pass: counter 15 */
    ASSERT_EQ_INT(T.inst.f.counter, 15);
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.inst.f.counter, 14);
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.inst.f.counter, 13);
}

/* ---- the ends of part 2 (8.9) -------------------------------------------------------------------------------------------------------------- */

TEST(part_2_ends_once_when_the_instructor_loses_its_valid_bit_and_part_1_never_does) {
    begin(2);
    ASSERT_EQ_INT(training_result(&T), TRAINING_RUNNING);
    T.inst.f.w0 &= (uint16_t)~0x0040;
    ASSERT_EQ_INT(training_result(&T), TRAINING_LOST);
    ASSERT_EQ_INT(training_result(&T), TRAINING_RUNNING);   /* reported once */
    begin(1);
    T.inst.f.w0 &= (uint16_t)~0x0040;
    ASSERT_EQ_INT(training_result(&T), TRAINING_RUNNING);
}

TEST(the_pass_skips_a_record_4_whose_valid_bit_is_clear_8_9) {
    begin(1);
    training_step(&T, &PLAYER);                         /* lesson 1 starts */
    ASSERT_EQ_INT(T.lesson, 1);
    T.inst.f.w0 &= (uint16_t)~0x0040;
    const int cnt = T.inst.f.counter;
    T.inst.f.state = 0; T.cursor = 1;                   /* a finished manoeuvre: the sequencer would advance */
    training_clear_outputs(&T);
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.lesson, 1);                         /* no sequencer */
    ASSERT_EQ_INT(T.inst.f.counter, cnt);               /* no decrement of +0x4C, no state routine */
    ASSERT_EQ_INT(T.npost, 0);
    begin(2);
    PLAYER.f.speed = 100;
    T.inst.f.w0 &= (uint16_t)~0x0040;
    training_step(&T, &PLAYER);
    ASSERT_TRUE(!(T.inst.f.w2 & 0x0100));               /* part 2: the lead is not started either */
    ASSERT_EQ_INT(training_result(&T), TRAINING_LOST);
}

TEST(the_mode_bytes_map_to_the_parts) {
    begin(1);
    ASSERT_EQ_INT(T.part, 1);
    training_start(&T, TRAINING_MODE_PART2, &PLAYER);
    ASSERT_EQ_INT(T.part, 2);
    training_start(&T, TRAINING_MODE_PART1, &PLAYER);
    ASSERT_EQ_INT(T.part, 1);
    training_start(&T, 9, &PLAYER);
    ASSERT_EQ_INT(T.part, 0);
    training_step(&T, &PLAYER);                         /* inactive: nothing happens */
    ASSERT_EQ_INT(T.lesson, 0);
}

/* ---- rules the differential test found (tests/rig/test_training_diff.c) --------------------------------------------------------------------- */

TEST(a_state_change_falls_into_the_next_state_in_the_same_step) {
    begin(1);
    inst_reset(0x24, 0, 0, 0x1000);                      /* 0x24 -> 0x25: the pull of 0x25 is issued in the step that ends the wait */
    T.inst.f.counter = -1;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x25);
    ASSERT_EQ_INT(T.inst.f.keys, 0x20);
    inst_reset(0x25, 0, 26799, 0x300000);                /* 0x25 -> 0x26: the altitude test of 0x26 follows at once: above 0x240000 the end routine */
    T.inst.f.b20 = 0x10;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x0A);
    inst_reset(0x22, 14400, 100, 0x300000);              /* 0x22 done -> 0x16 -> 0x0D, all in one step */
    T.inst.f.b20 = 0x10;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x0D);
    ASSERT_TRUE(T.inst.f.b20 & 0x08);
    inst_reset(0x1F, 100, 100, 0x300000);                /* 0x1F done -> 0x21 with the counter 25: recentre */
    T.inst.f.b20 = 0x10;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x21);
    ASSERT_EQ_INT(T.inst.f.counter, 25);
    ASSERT_TRUE(T.inst.f.w2 & 0x0010);
}

TEST(the_take_off_throttle_up_keeps_the_other_throttle_bit) {
    begin(2);
    inst_reset(0x23, 0, 0, 0x1000);
    T.inst.f.throttle = 9; T.inst.f.keys = 0xE6;
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.keys, 0xE7);
}

TEST(the_barrel_roll_ends_on_the_second_flag_whatever_the_first) {
    begin(1);
    inst_reset(0x19, 100, 0, 0x300000);
    T.inst.f.b20 = 0x10;                                 /* f4 set, f3 clear */
    training_ai(&T);
    ASSERT_EQ_INT(T.inst.f.state, 0x0A);
}

TEST(a_re_position_enters_the_advance_past_its_own_re_placement_so_lesson_7_places_once) {
    begin(1);
    training_step(&T, &PLAYER);
    T.lesson = 7; T.inst.f.state = 0x0D;
    training_key_return(&T, &PLAYER);
    const int placed = T.placements;
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.placements, placed + 1);
    ASSERT_EQ_INT(T.lesson, 1);
}

TEST(the_step_clears_the_proximity_bit_of_the_instructor) {
    begin(1);
    training_step(&T, &PLAYER);
    T.inst.f.w2 |= 0x0001;
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.inst.f.w2 & 0x0001, 0);
}

TEST(an_unscripted_instructor_takes_state_6_when_its_counter_runs_out_and_has_its_key_byte_cut_to_0xC3_and_never_levels) {
    begin(2);
    T.inst.f.state = 0; T.inst.f.keys = 0xFF; T.inst.f.counter = 5;
    training_step(&T, &PLAYER);                          /* the player stands: the lead is not scripted */
    ASSERT_EQ_INT(T.inst.f.keys, 0xC3);                  /* 8.3: the conversion cuts the key byte of a record that is neither scripted nor has +0x64 bit 4 */
    ASSERT_EQ_INT(T.inst.f.state, 0);
    T.inst.f.counter = 0;                                /* the step decrements it to -1 */
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.inst.f.state, 6);
    ASSERT_EQ_INT(T.inst.f.counter, 10);
    /* state 6 of an unscripted record: no levelling and no W0 bit 0x2000 (8.3: the levelling is reached only by a scripted record or with +0x64 bit 4) */
    T.inst.f.roll = 5000; T.inst.f.keys = 0; T.inst.f.w0 = 0x11C8; T.inst.f.counter = 0;
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.inst.f.state, 6);
    ASSERT_EQ_INT(T.inst.f.keys, 0);
    ASSERT_TRUE(!(T.inst.f.w0 & 0x2000));
    T.b64 = 0x10;                                        /* the epilogue flag: the unscripted record is levelled all the same */
    T.inst.f.w0 = 0x11C8; T.inst.f.counter = 0;
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.inst.f.keys, FLIGHT_KEY_ROLL_RIGHT); /* roll 5000: above the dead band, below 180 degrees */
    ASSERT_TRUE(T.inst.f.w0 & 0x2000);
}

TEST(the_wait_state_levels_the_wings_with_a_roll_command_from_the_roll_the_pass_began_with) {
    begin(1);
    static const struct { int roll, bits; } W[] = { { 0, 0 }, { 280, 0 }, { 281, 4 }, { 14400, 4 }, { 14401, 8 }, { 28519, 8 }, { 28520, 0 }, { 28799, 0 } };
    for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) {
        inst_reset(0x0A, W[i].roll, 0, 0x300000);
        T.inst.f.counter = 30; T.inst.f.keys = 0xFF; T.inst.f.w2 = 0x0100;
        T.lesson = 3; T.cursor = 1;                         /* the sequencer sees a running manoeuvre */
        training_step(&T, &PLAYER);
        ASSERT_EQ_INT(T.inst.f.keys, W[i].bits | 0x03);   /* bits 7 to 2 replaced, the throttle bits kept */
        ASSERT_TRUE(T.inst.f.w0 & 0x2000);                /* the wait leaves W0 bit 0x2000 set */
    }
    inst_reset(0x0A, 1000, 0, 0x300000);                   /* an inactive record is not stepped but is levelled all the same */
    T.inst.f.w0 = 0x00C8; T.inst.f.counter = 30; T.inst.f.keys = 0; T.inst.f.w2 = 0x0100; T.lesson = 3; T.cursor = 1;
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.inst.f.keys, 4);
}

TEST(the_wait_state_6_levels_the_wings_as_state_0A_does_note_8_3_text_NOTE_GAP) {
    begin(1);
    static const struct { int roll, bits; } W[] = { { 0, 0 }, { 280, 0 }, { 281, 4 }, { 14401, 8 }, { 28520, 0 } };
    for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) {
        inst_reset(6, W[i].roll, 0, 0x300000);
        T.inst.f.counter = 30; T.inst.f.keys = 0xFF; T.inst.f.w2 = 0x0100;
        T.lesson = 3; T.cursor = 1;
        training_step(&T, &PLAYER);
        ASSERT_EQ_INT(T.inst.f.state, 6);
        ASSERT_EQ_INT(T.inst.f.keys, W[i].bits | 0x03);
        ASSERT_TRUE(T.inst.f.w0 & 0x2000);
    }
}

TEST(a_freshly_placed_record_loses_the_placement_bit_in_its_first_pass) {
    begin(1);
    ASSERT_TRUE(T.inst.f.w0 & 0x0100);
    training_step(&T, &PLAYER);
    ASSERT_TRUE(!(T.inst.f.w0 & 0x0100));
}

/* ---- the whole game on the extracted assets (skipped without assets/) -------------------------------------------------------------------------- */

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
static void real_release(void *user, uint8_t *buf) { (void)user; free(buf); }
static game_loader_t RLD = { real_read, real_release, NULL };
static int have_real(void) {
    uint8_t *b; size_t n;
    if (real_read(NULL, "tables/training.bin", &b, &n) != 0) return 0;
    free(b);
    if (real_read(NULL, "tables/flight_stick_player.bin", &b, &n) != 0) return 0;
    free(b);
    if (real_read(NULL, "tables/text.bin", &b, &n) != 0) return 0;
    free(b);
    if (real_read(NULL, "models/index.txt", &b, &n) != 0) return 0;
    free(b);
    return 1;
}
static game_t *G;
static game_scene_t *SC;
static void gkey(int c) { game_event_t ev = { GAME_EV_KEY, 0, c }; game_input(G, &ev); }
static void gbtn(int c, int v) { game_event_t ev = { GAME_EV_BUTTON, c, v }; game_input(G, &ev); }
static void gaxis(int c, int v) { game_event_t ev = { GAME_EV_AXIS, c, v }; game_input(G, &ev); }
static void gsteps(int n) { for (int i = 0; i < n; i++) game_step(G); game_scene(G, SC); }
static trainer_t *tr(void) { return (trainer_t *)game_debug_trainer(G); }
/* the screens: any key, RETURN, the menu item, then until the three-tick wait is over and the flight runs (the briefing types out first) */
static int real_start(int item) {
    char err[200];
    G = game_create(&RLD, err, sizeof err);
    if (!G) { printf("(game_create: %s) ", err); return 0; }
    SC = (game_scene_t *)calloc(1, sizeof *SC);
    gbtn(GAME_BTN_ANY, 1); gsteps(4);
    gbtn(GAME_BTN_RETURN, 1); gsteps(60);
    gkey(item);
    for (int n = 0; n < 3000; n++) {   /* the briefing types out, the three-tick wait runs, then the instructor takes its first step */
        gsteps(1);
        if (tr()->part && tr()->inst.f.steps >= 1) break;
    }
    return tr()->part != 0;
}
static void real_end(void) { game_destroy(G); free(SC); G = NULL; SC = NULL; }

/* The stage lists of note 8.1 from the menu key (step 0) in steps: the echo 7, the post 4 (part 1: blank 24) or 11 (part 2: blank 67) later, the briefing typed 59 or 95 steps
 * and held 1, then part 1 the six passes of 0x12DB8 to its first instructor step (the first pass of 0x12EDA); part 2 the approach (64 + 4 steps), 0x12DB8 (6), the take-off notice
 * 0x59 (7 steps with its hold) and the first simulated step. The text ids are the scene's first text record (game_text_t.id). */
static void stage_walk(int item, int *post, int *gone, int *notice, int *notice_gone, int *first_inst) {
    char err[200];
    G = game_create(&RLD, err, sizeof err);
    SC = (game_scene_t *)calloc(1, sizeof *SC);
    gbtn(GAME_BTN_ANY, 1); gsteps(4);
    gbtn(GAME_BTN_RETURN, 1); gsteps(60);
    gkey(item);
    const int brief = item == '3' ? 9 : 10;
    *post = *gone = *notice = *notice_gone = *first_inst = -1;
    for (int n = 1; n <= 400 && *first_inst < 0; n++) {
        gsteps(1);
        const int id = SC->text_count ? SC->text[0].id : 0;
        if (id == brief && *post < 0) *post = n;
        if (*post >= 0 && id != brief && *gone < 0) *gone = n;
        if (id == 0x59 && *notice < 0) *notice = n;
        if (*notice >= 0 && id != 0x59 && *notice_gone < 0) *notice_gone = n;
        if (tr()->part && tr()->inst.f.steps >= 1) *first_inst = n;
    }
    real_end();
}

TEST(real_assets_the_stage_lists_of_8_1_from_the_menu_key_to_the_first_instructor_step) {
    if (!have_real()) { printf("skipped "); return; }
    int post, gone, notice, notice_gone, first;
    stage_walk('3', &post, &gone, &notice, &notice_gone, &first);
    ASSERT_EQ_INT(post, 11);                 /* 7 (echo) + 4 (24 blanks) */
    ASSERT_EQ_INT(gone - post, 60);          /* 59 typed + 1 hold */
    ASSERT_EQ_INT(notice, -1);               /* mode 2 types no notice */
    ASSERT_EQ_INT(first, gone + 6);          /* 0x12DB8: six passes, then 0x12E4E acts at once and the first pass of 0x12EDA steps the lead */
    stage_walk('4', &post, &gone, &notice, &notice_gone, &first);
    ASSERT_EQ_INT(post, 18);                 /* 7 + 11 (67 blanks) */
    ASSERT_EQ_INT(gone - post, 96);          /* 95 typed + 1 hold */
    ASSERT_EQ_INT(notice - gone, 37);        /* 12.14 C13: the mover's 29 passes, 0x12D58's 2 passes (31 in all), 0x12DB8's six passes */
    ASSERT_EQ_INT(notice_gone - notice, 7);  /* 36 blanks typed and the hold */
    ASSERT_EQ_INT(first, notice_gone);       /* then 0x12E4E: the simulation is on in the step that follows */
}

/* 06 start table, 08 12.14 C13 [V: flow7-training-2]: training part 1 shows the lead from outside with the HUD step 50 (window row 167, external view) from the dispatch through the briefing, the countdown,
 * the wait and the first flight pass; the milestone-6 port showed step 0. */
TEST(real_assets_training_part_1_keeps_the_hud_step_50_in_the_external_view_through_the_first_flight_pass) {
    if (!have_real()) { printf("skipped "); return; }
    char err[200];
    G = game_create(&RLD, err, sizeof err);
    ASSERT_TRUE(G != NULL);
    SC = (game_scene_t *)calloc(1, sizeof *SC);
    gbtn(GAME_BTN_ANY, 1); gsteps(4);
    gbtn(GAME_BTN_RETURN, 1); gsteps(60);
    gkey('3');
    int seen = 0, bad = 0, flight_passes = 0;
    for (int n = 0; n < 3000 && flight_passes < 3; n++) {
        gsteps(1);
        const screen_t *s = (const screen_t *)game_debug_screen(G);
        if (s->state == SCR_TRAIN_DELAY || s->state == SCR_TRAIN_BRIEF || s->state == SCR_APPROACH || s->state == SCR_WAIT || s->state == SCR_FLIGHT) {
            seen++;
            if (SC->camera.hud_step != 50 || SC->sky.row_limit != 167 || SC->camera.view == GAME_VIEW_COCKPIT) bad++;
        }
        if (s->state == SCR_FLIGHT) flight_passes++;
    }
    ASSERT_TRUE(seen > 70);
    ASSERT_EQ_INT(bad, 0);               /* every picture from the dispatch to the third flight pass: step 50, row 167, an external view */
    ASSERT_EQ_INT(flight_passes, 3);
    real_end();
}

/* 06 start table, 08 12.14 C13 [V: flow7-training-7D]: part 2 is seen from its fixed camera point (step 50, row 167, external view) from the dispatch through the briefing and the approach; the mover flies it
 * from stage 3 for 29 passes (stage 4 at pass 1, 5 at 15, 6 at 25, camera mode 0 at 29) and the cockpit (step 0, row 144, view 0) follows at pass 30 of the 31. */
TEST(real_assets_training_part_2_is_seen_from_outside_through_the_approach_and_the_cockpit_comes_at_pass_30_of_31) {
    if (!have_real()) { printf("skipped "); return; }
    char err[200];
    G = game_create(&RLD, err, sizeof err);
    ASSERT_TRUE(G != NULL);
    SC = (game_scene_t *)calloc(1, sizeof *SC);
    gbtn(GAME_BTN_ANY, 1); gsteps(4);
    gbtn(GAME_BTN_RETURN, 1); gsteps(60);
    gkey('4');
    int bad_before = 0, seen_before = 0, p = -1, stage4 = -1, stage5 = -1, stage6 = -1, mode0 = -1, cockpit = -1, passes = 0;
    for (int n = 0; n < 3000; n++) {
        gsteps(1);
        const screen_t *s = (const screen_t *)game_debug_screen(G);
        if (s->state == SCR_APPROACH) {
            p++; passes++;
            const int stage = game_debug_mover(G)->stage;
            if (stage >= 4 && stage4 < 0) stage4 = p;
            if (stage >= 5 && stage5 < 0) stage5 = p;
            if (stage >= 6 && stage6 < 0) stage6 = p;
            if (game_debug_camera_mode(G) == 0 && mode0 < 0) mode0 = p;
            if (SC->camera.hud_step == 0 && cockpit < 0) cockpit = p;
            if (cockpit < 0 && (SC->camera.hud_step != 50 || SC->sky.row_limit != 167 || SC->camera.view == GAME_VIEW_COCKPIT)) bad_before++;
        } else if (s->state == SCR_TRAIN_DELAY || s->state == SCR_TRAIN_BRIEF) {
            seen_before++;
            if (SC->camera.hud_step != 50 || SC->sky.row_limit != 167 || SC->camera.view == GAME_VIEW_COCKPIT) bad_before++;
        } else if (s->state == SCR_NOTICE) break;
    }
    ASSERT_TRUE(seen_before > 100);
    ASSERT_EQ_INT(bad_before, 0);
    ASSERT_TRUE(stage4 >= 0 && stage4 <= 2);       /* the log: stage 4 at 1 (a sample is +-1 pass) */
    ASSERT_TRUE(stage5 >= 14 && stage5 <= 16);     /* 15 */
    ASSERT_TRUE(stage6 >= 24 && stage6 <= 26);     /* 25 */
    ASSERT_TRUE(mode0 >= 28 && mode0 <= 30);       /* 29 */
    ASSERT_EQ_INT(cockpit, 30);                    /* the cockpit pass: step 0 */
    ASSERT_EQ_INT(passes, 31 + 6);                 /* 29 + 2 passes of 0x12D58 and the six of 0x12DB8 */
    real_end();
}

/* 08 6.1 item 6 and 8.3: the player of part 1 stands on airfield 1's runway (the reset block placed it at start block 0) while V:4A900 is 3 (the dispatch stored it afterwards): the fixed
 * camera of Shift+KP-Enter (0x1E3EE, game.c fixed_camera) and the landing test read V:4A900, so they read the carrier's entry; the player is where a runway start at block 0 puts it. */
TEST(real_assets_training_part_1_keeps_v4a900_at_3_while_the_player_stands_at_start_block_0) {
    if (!have_real()) { printf("skipped "); return; }
    ASSERT_TRUE(real_start('3'));
    ASSERT_EQ_INT(((const screen_t *)game_debug_screen(G))->location, 3);
    aircraft_t *pl = game_debug_aircraft(G);
    int32_t ppos[3] = { pl->pos[0], pl->pos[1], pl->pos[2] };
    real_end();
    char err[200];
    G = game_create(&RLD, err, sizeof err);
    ASSERT_TRUE(G != NULL);
    SC = (game_scene_t *)calloc(1, sizeof *SC);
    game_debug_start_flight(G, 0, 0);                                    /* a runway start at block 0 */
    pl = game_debug_aircraft(G);
    ASSERT_EQ_INT(pl->pos[0], ppos[0]); ASSERT_EQ_INT(pl->pos[2], ppos[2]);
    real_end();
}

/* The lead (record 4, the F-16 model 3) is in the scene at the position it was placed at, in every stage of the part 1 start, the briefing and the six passes of 0x12DB8 included
 * (note 08 8.1: the start scene shows the lead over the bay). */
TEST(real_assets_a_lost_lead_in_part_1_hands_the_view_back_to_the_dormant_player) {   /* 8.9 */
    if (!have_real()) { printf("skipped "); return; }
    ASSERT_TRUE(real_start('3'));
    gsteps(3);
    ASSERT_EQ_INT(SC->hud.cp_y, tr()->inst.pos[1]);
    aircraft_t *pl = game_debug_aircraft(G);
    tr()->inst.f.w0 &= (uint16_t)~0x0040;
    gsteps(3);
    ASSERT_EQ_INT(SC->hud.cp_y, pl->pos[1]);                       /* the strip reads the player ... */
    ASSERT_EQ_INT(SC->hud.cp_heading80, pl->f.heading);
    const int32_t px = pl->pos[0];
    ASSERT_TRUE(SC->camera.pos.x > px - 2000000 && SC->camera.pos.x < px + 2000000);   /* ... and the camera is around the player, not 3 million units away at the lead */
    for (int i = 0; i < SC->entry_count; i++) ASSERT_TRUE(!(SC->entries[i].kind == GAME_ENTRY_ENTITY && SC->entries[i].id == 4));   /* and the lead is not drawn */
    real_end();
}

TEST(real_assets_the_part_1_lead_is_in_the_scene_at_its_placed_position_during_the_briefing_and_0x12DB8) {
    if (!have_real()) { printf("skipped "); return; }
    char err[200];
    G = game_create(&RLD, err, sizeof err);
    SC = (game_scene_t *)calloc(1, sizeof *SC);
    gbtn(GAME_BTN_ANY, 1); gsteps(4);
    gbtn(GAME_BTN_RETURN, 1); gsteps(60);
    gkey('3');
    int checked = 0;
    for (int n = 1; n <= 76; n++) {   /* up to the step before the first instructor step (77) */
        gsteps(1);
        if (n < 12) continue;                                      /* the start scene is built at the dispatch (step 7) and the post is at 11 */
        int found = 0;
        for (int i = 0; i < SC->entry_count; i++)
            if (SC->entries[i].kind == GAME_ENTRY_ENTITY && SC->entries[i].id == 4) {
                found = 1;
                ASSERT_EQ_INT(SC->entries[i].pos.x, 307757056); ASSERT_EQ_INT(SC->entries[i].pos.z, 328728576);
            }
        ASSERT_TRUE(found);
        checked++;
    }
    ASSERT_TRUE(checked > 60);
    real_end();
}

TEST(real_assets_menu_item_3_starts_part_1_with_the_instructor_at_the_note_position_and_the_camera_on_it) {
    if (!have_real()) { printf("skipped "); return; }
    ASSERT_TRUE(real_start('3'));
    ASSERT_EQ_INT(tr()->part, 1);
    ASSERT_TRUE(tr()->viewed_inst);
    ASSERT_EQ_INT(SC->mode, GAME_MODE_FLIGHT);
    ASSERT_EQ_INT(game_crash_allowance(G), 3);                     /* 8.1: the allowance of part 1 */
    ASSERT_EQ_INT(SC->camera.view, GAME_VIEW_EXTERNAL);            /* the external view 1 at direction 3, on record 4 */
    ASSERT_EQ_INT(SC->camera.ring, 3);
    ASSERT_EQ_INT(tr()->inst.f.cls, 0x10);
    ASSERT_EQ_INT(tr()->inst.f.id, 0x3D);
    gsteps(3);
    ASSERT_EQ_INT(SC->hud.cp_y, tr()->inst.pos[1]);                /* 8.3, 8.7: the external strip (HDG, KTS, FT) shows the LEAD's data (V:4A996 = record 4) */
    ASSERT_EQ_INT(SC->hud.cp_speed_w, tr()->inst.f.speed);
    ASSERT_EQ_INT(SC->hud.cp_heading80, tr()->inst.f.heading);
    ASSERT_TRUE(SC->hud.cp_y != game_debug_aircraft(G)->pos[1]);   /* and not the dormant player's */
    ASSERT_EQ_INT(SC->hud.altitude_ft, 5 * (tr()->inst.pos[1] >> 10));
    ASSERT_EQ_INT(tr()->inst.f.gear, 0xE0);                        /* 8.3: the placement writes the gear byte 0xE0: the instructor flies, and is drawn, with the gear UP */
    ASSERT_EQ_INT(tr()->inst.gear, 0);
    ASSERT_EQ_INT(SC->hud.cp_msg_ext, 1);                          /* 8.7: the message line is drawn in the external view when the mode byte is 2 ... */
    ASSERT_EQ_INT(SC->hud.cp_msg_entry, 23);                       /* ... and it shows the name of lesson 1 (entry 23, the aileron roll) */
    ASSERT_TRUE(tr()->inst.f.w2 & 0x0100);                         /* 8.3 (corrected): mode 2's flag bit 0x0100 is on RECORD 4 (0x4BA3E), not on the player ... */
    ASSERT_TRUE(!(game_debug_aircraft(G)->f.w2 & 0x0100));         /* ... whose own W2 stays 0x0080 in part 1 */
    real_end();
}

TEST(real_assets_part_1_lesson_starts_are_the_originals_exact_steps_0_93_196_384_541_690_780_962) {
    if (!have_real()) { printf("skipped "); return; }
    ASSERT_TRUE(real_start('3'));
    /* the instructor is rig-exact from the original's own placement (tests/rig/test_training_traj.c: 3000 steps, every mapped word): the lesson starts are pinned from the first lesson 1 */
    static const int want[8] = { 0, 93, 196, 384, 541, 690, 780, 962 };   /* lesson 1 .. 7, then lesson 1 again: note 8.12's exact steps (the rig's 0x26AF8 from the part 1 start; the emulator's 50-blank samples read 0, 93, 200, 390, 545, 691, 784, 962) */
    int base = -1, seen = 0, last = -1;
    for (int n = 0; n < 2500 && seen < 8; n++) {
        if (tr()->lesson != last && tr()->lesson != 0) {
            last = tr()->lesson;
            if (base < 0) base = n;
            ASSERT_EQ_INT(last, seen % 7 + 1);
            ASSERT_EQ_INT(n - base, want[seen]);
            seen++;
        }
        gsteps(1);
    }
    ASSERT_EQ_INT(seen, 8);
    real_end();
}

TEST(real_assets_part_1_ignores_the_flight_controls_and_return_skips_a_lesson) {
    if (!have_real()) { printf("skipped "); return; }
    ASSERT_TRUE(real_start('3'));
    const int lesson = tr()->lesson;
    aircraft_t *pl = game_debug_aircraft(G);
    gbtn(GAME_BTN_FKEY, 5); gbtn(GAME_BTN_THROTTLE_UP, 1); gaxis(GAME_AXIS_STICK_Y, 20000); gaxis(GAME_AXIS_THROTTLE, 30000);
    gsteps(3);
    ASSERT_EQ_INT(pl->f.v4a928, 0);                              /* the throttle presets cannot be used */
    ASSERT_EQ_INT(pl->f.keys, 0);
    gbtn(GAME_BTN_SELECT_WEAPON, 1);                             /* RETURN in flight */
    gsteps(1);
    ASSERT_EQ_INT(tr()->lesson, lesson % 7 + 1);
    ASSERT_EQ_INT(tr()->inst.pos[0], 307757056 + 0);             /* back at the start (no motion in the placed pass) */
    real_end();
}

TEST(real_assets_part_2_the_lead_takes_off_and_waits_with_the_first_prompt_return_repositions_and_the_trigger_starts_a_lesson) {
    if (!have_real()) { printf("skipped "); return; }
    ASSERT_TRUE(real_start('4'));
    ASSERT_EQ_INT(tr()->part, 2);
    ASSERT_EQ_INT(game_crash_allowance(G), 1);                   /* 8.9: part 2 allows one crash */
    ASSERT_EQ_INT(SC->camera.view, GAME_VIEW_COCKPIT);
    ASSERT_EQ_INT(SC->hud.cp_msg_ext, 0);                        /* part 2: its prompts are shown in the cockpit view, the external views hide the line */
    aircraft_t *pl = game_debug_aircraft(G);
    gsteps(20);
    ASSERT_TRUE(!(tr()->inst.f.w2 & 0x0100));                    /* waits while the player stands */
    gbtn(GAME_BTN_FKEY, 8);                                      /* the briefing asks for 80 percent: the player starts to roll and the lead takes off */
    int n = 0;
    while (!(tr()->inst.f.w2 & 0x0100) && n < 400) { gsteps(1); n++; }
    ASSERT_EQ_INT(tr()->inst.f.state, 0x23);
    n = 0;
    int jolt = 0;
    while (tr()->trig != 0xFF && n < 4000) { gsteps(1); n++; jolt += SC->jolt; }
    ASSERT_TRUE(jolt >= 1);                                      /* the lead's lift-off raised the jolt event (8.3: the kind is stored for any record; the player's own roll may add one) */
    ASSERT_EQ_INT(tr()->trig, 0xFF);                             /* the take-off is over: the prompt of the aileron roll */
    ASSERT_EQ_INT(SC->hud.cp_msg_entry, 46);
    ASSERT_TRUE(tr()->inst.pos[1] > 0x240000);
    /* RETURN: the player is put behind the lead (9, 1, -97 model units, about 99,760 units away) */
    gbtn(GAME_BTN_SELECT_WEAPON, 1);
    gsteps(1);
    ASSERT_EQ_INT(tr()->trig, 0xFE);
    {
        const double dx = (double)pl->pos[0] - tr()->inst.pos[0], dy = (double)pl->pos[1] - tr()->inst.pos[1], dz = (double)pl->pos[2] - tr()->inst.pos[2];
        const double d2 = dx * dx + dy * dy + dz * dz;
        ASSERT_TRUE(d2 > 99000.0 * 99000.0 && d2 < 101500.0 * 101500.0);   /* the lead has moved on one step: about the 99,760 of the formula */
    }
    ASSERT_EQ_INT(pl->f.cls, 0x11);
    gbtn(GAME_BTN_FIRE, 1);                                      /* the trigger */
    gsteps(1);
    ASSERT_EQ_INT(tr()->lesson, 1);
    ASSERT_EQ_INT(tr()->trig, 0);
    real_end();
}

TEST(real_assets_esc_in_part_1_goes_on_to_part_2_and_shift_esc_returns_to_the_menu) {
    if (!have_real()) { printf("skipped "); return; }
    ASSERT_TRUE(real_start('3'));
    gbtn(GAME_BTN_ESC, 1);
    int n = 0;
    while (n < 3000 && !(tr()->part == 2 && SC->mode == GAME_MODE_FLIGHT && tr()->placements)) { gsteps(1); n++; }
    ASSERT_EQ_INT(tr()->part, 2);
    ASSERT_EQ_INT(game_crash_allowance(G), 1);
    gbtn(GAME_BTN_SHIFT_ESC, 1);
    gsteps(60);
    ASSERT_EQ_INT(SC->mode, GAME_MODE_MENU);
    real_end();
}

TEST(real_assets_a_crash_in_part_2_ends_the_flight_with_the_game_over_text_the_allowance_is_1) {
    if (!have_real()) { printf("skipped "); return; }
    ASSERT_TRUE(real_start('4'));
    ASSERT_EQ_INT(game_crash_allowance(G), 1);
    aircraft_t *ac = game_debug_aircraft(G);
    /* staged as test_crash.c does: a dive into the open sea one cell short of the airfield (collisions are on in training, note 08 8.10) */
    flight_init(ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    ac->f.v4a95e = TRAINING_MODE_PART2;
    flight_set_position(ac, 273272584, 20000, 280480032);
    flight_set_attitude(ac, 2000, 2240, 0);
    ac->f.w2 = 0; ac->f.b4 = 0; ac->f.speed_target = ac->f.speed = 3000; ac->f.throttle = 60;
    flight_sync(ac);
    int over = 0;
    for (int n = 0; n < 400 && !over; n++) { gsteps(1); over = SC->mode == GAME_MODE_GAMEOVER; }
    ASSERT_TRUE(over);
    ASSERT_EQ_INT(SC->text_id, 0x63);                           /* the F/A-18's game-over text, the first crash of part 2 */
    ASSERT_EQ_INT(game_crash_allowance(G), 0);
    int menu = 0;
    for (int n = 0; n < 200 && !menu; n++) { gsteps(1); menu = SC->mode == GAME_MODE_MENU; }
    ASSERT_TRUE(menu);
    real_end();
}

TEST(real_assets_return_during_a_pause_in_part_2_repositions_the_player_and_nothing_else_steps) {
    if (!have_real()) { printf("skipped "); return; }
    ASSERT_TRUE(real_start('4'));
    aircraft_t *pl = game_debug_aircraft(G);
    gsteps(20);
    gbtn(GAME_BTN_FKEY, 8);
    int n = 0;
    while (tr()->trig != 0xFF && n < 4500) { gsteps(1); n++; }
    ASSERT_EQ_INT(tr()->trig, 0xFF);                             /* the first prompt is up: RETURN is accepted */
    gbtn(GAME_BTN_PAUSE, 1);
    gsteps(3);
    ASSERT_EQ_INT(SC->mode, GAME_MODE_PAUSED);
    const uint32_t isteps = tr()->inst.f.steps, psteps = pl->f.steps;
    const int32_t ipos = tr()->inst.pos[2];
    gbtn(GAME_BTN_SELECT_WEAPON, 1);                             /* 0x270A0 acts in a paused pass (8.6) */
    gsteps(3);
    ASSERT_EQ_INT(tr()->trig, 0xFE);                             /* re-positioned */
    ASSERT_EQ_INT(pl->f.cls, 0x11);
    ASSERT_EQ_INT(tr()->inst.f.steps, isteps);                   /* the pause stops the lead ... */
    ASSERT_EQ_INT(pl->f.steps, psteps);                          /* ... and the player's step */
    ASSERT_EQ_INT(tr()->inst.pos[2], ipos);
    {
        const double dx = (double)pl->pos[0] - tr()->inst.pos[0], dy = (double)pl->pos[1] - tr()->inst.pos[1], dz = (double)pl->pos[2] - tr()->inst.pos[2];
        const double d2 = dx * dx + dy * dy + dz * dz;
        ASSERT_TRUE(d2 > 99000.0 * 99000.0 && d2 < 100500.0 * 100500.0);   /* the formula's 99,760 units behind the lead that stood still */
    }
    gbtn(GAME_BTN_PAUSE, 1);
    gsteps(3);
    ASSERT_EQ_INT(SC->mode, GAME_MODE_FLIGHT);
    ASSERT_TRUE(tr()->inst.f.steps > isteps);                    /* and it flies on after the pause */
    real_end();
}

TEST(the_advance_never_touches_the_view_selector_in_part_2_and_sets_it_to_1_in_part_1) {   /* note 08 8.4 step 7 */
    begin(2);
    T.lesson = 3; T.cursor = 1; T.inst.f.state = 0; T.inst.f.w2 |= 0x0100 | 0x0800;
    training_clear_outputs(&T);
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.lesson, 4);
    ASSERT_EQ_INT(T.out_view, -1);                         /* part 2: V:4A83D untouched (the camera direction is only set when the lead is the viewed record) */
    begin(1);
    training_step(&T, &PLAYER);
    training_clear_outputs(&T);
    T.lesson = 3; T.cursor = 1; T.inst.f.state = 0;
    training_step(&T, &PLAYER);
    ASSERT_EQ_INT(T.out_view, 2);                          /* part 1: byte 1 of the table B camera word of row 4 */
    ASSERT_EQ_INT(T.out_ring, 13);
}

TEST(real_assets_the_lead_is_record_4_of_the_entity_array_and_its_pass_goes_through_the_world_update_order) {   /* note 07 1.2: slot 4, the instructor's pass replaces the generic gate */
    if (!have_real()) { printf("skipped "); return; }
    ASSERT_TRUE(real_start('4'));
    gsteps(10);
    entities_t *e = (entities_t *)game_debug_entities(G);
    ASSERT_TRUE(e != NULL);
    ASSERT_TRUE(entities_ac(e, 4) == &tr()->inst);               /* the storage stays the trainer's: one aircraft, two views of it */
    ASSERT_TRUE(entities_ac(e, 0) == game_debug_aircraft(G));
    const uint32_t before = tr()->inst.f.steps;
    gsteps(5);
    ASSERT_EQ_INT(tr()->inst.f.steps, before + 5);               /* record 4 is stepped once per pass by the entity update */
    for (int i = 1; i < 16; i++) if (i != 4 && i != 14) ASSERT_EQ_INT(e->rec[i].ac.f.w0 & 0x0040, 0);   /* nothing else was placed in a training */
    real_end();
}

TEST(real_assets_losing_the_lead_in_part_2_raises_the_end_request_once_the_freeze_is_14_blanks_2_steps_and_the_pass_skips_record_4_for_good) {
    if (!have_real()) { printf("skipped "); return; }
    ASSERT_TRUE(real_start('4'));
    gsteps(10);                                                  /* the wait before the flight is over */
    aircraft_t *ac = game_debug_aircraft(G);
    tr()->inst.f.w0 &= (uint16_t)~0x0040;                       /* the lead is lost (8.9) */
    const uint32_t inst_steps = tr()->inst.f.steps;
    int32_t pos[3];
    for (int i = 0; i < 3; i++) pos[i] = tr()->inst.pos[i];
    int longest = 0, gap = 0, runs = 0;
    for (int n = 0; n < 200; n++) {
        const uint32_t before = ac->f.steps;
        gsteps(1);
        if (ac->f.steps == before) { gap++; if (gap == 1) runs++; if (gap > longest) longest = gap; } else gap = 0;
    }
    ASSERT_EQ_INT(runs, 1);                                      /* one freeze */
    ASSERT_EQ_INT(longest, 3);                                   /* 8.9: 0x131F4 to the resume is 14 blanks = 2 steps, and the resume pass is no flight pass (5.13 [V: flow7-pause-outcome]: the first flight pass is 6 blanks later = 1 step) */
    ASSERT_EQ_INT(tr()->lost_reported, 1);                       /* the end request once */
    ASSERT_EQ_INT(tr()->inst.f.steps, inst_steps);               /* the pass skips record 4: no step ... */
    for (int i = 0; i < 3; i++) ASSERT_EQ_INT(tr()->inst.pos[i], pos[i]);   /* ... no motion */
    ASSERT_EQ_INT(tr()->lesson, 0);                              /* ... and no sequencer */
    ASSERT_TRUE(ac->f.steps > 100);                              /* the flight went on after code 2 */
    real_end();
}

int main(void) {
    printf("training\n");
    RUN(the_part_2_placement_copies_the_players_plus_70_and_plus_71_and_part_1_writes_the_template_bytes);
    RUN(the_aileron_roll_waits_15_steps_in_state_0C_then_rolls_left_and_ends_in_the_wait_state_36_steps_in_part_1);
    RUN(the_aileron_roll_passes_through_both_phase_flags_and_a_stuck_stick_never_ends);
    RUN(the_roll_command_keeps_the_throttle_bits_and_only_the_sign_matters);
    RUN(the_break_turn_banks_to_90_degrees_pulls_for_32_steps_and_ends);
    RUN(the_break_turn_error_is_taken_in_word_arithmetic);
    RUN(the_inside_loop_pulls_through_the_top_and_ends_when_the_pitch_word_passes_14400_the_second_time);
    RUN(the_inside_loop_with_a_plant_that_never_pulls_does_not_end);
    RUN(the_split_s_is_refused_at_or_below_5000_feet_and_otherwise_rolls_to_exactly_180_degrees_into_the_loop_pull);
    RUN(the_vertical_half_loop_pulls_to_the_top_then_rolls_upright_in_state_0D);
    RUN(the_barrel_roll_rolls_and_pulls_together_and_ends_when_the_roll_returns_below_180_degrees);
    RUN(inverted_flight_rolls_to_180_degrees_holds_pulls_and_pushes_back_into_the_half_loop_states);
    RUN(the_take_off_climbs_the_throttle_to_60_hex_waits_60_steps_pulls_to_25_degrees_and_ends_at_the_altitude);
    RUN(the_wait_state_0A_ends_when_the_counter_is_negative_and_leaves_only_the_throttle_bits);
    RUN(the_pitch_command_compares_the_target_rate_with_the_pitch_rate);
    RUN(part_1_starts_at_the_note_position_and_lesson_0_is_skipped_into_lesson_1);
    RUN(part_1_runs_the_seven_lessons_in_order_with_the_names_of_8_7_and_wraps_to_lesson_1_with_a_re_placement);
    RUN(the_sequencer_leaves_the_state_alone_while_a_manoeuvre_runs_and_waits_for_the_end_of_the_wait);
    RUN(the_sequencer_does_nothing_while_paused_and_in_part_1_until_the_flight_is_running);
    RUN(the_split_s_refusal_notice_replaces_the_name_when_the_instructor_is_at_or_below_0x100000);
    RUN(return_in_part_1_re_places_the_instructor_and_skips_to_the_next_lesson);
    RUN(part_2_places_the_lead_72_model_units_ahead_with_the_players_attitude_and_waits_for_the_player_to_move);
    RUN(a_finished_take_off_posts_the_prompt_of_lesson_1_and_the_trigger_starts_the_lesson_after_it);
    RUN(the_fire_key_has_no_training_effect_while_a_manoeuvre_runs_or_when_the_player_ejects);
    RUN(return_with_a_manoeuvre_running_posts_the_not_ready_notice_and_changes_nothing_else);
    RUN(the_re_position_puts_the_player_9_right_1_up_and_97_behind_the_instructor_99760_units_away);
    RUN(the_re_position_offset_goes_through_the_instructors_matrix);
    RUN(the_return_key_reports_whether_it_set_the_request_the_game_beeps_then_0x1e8c2);
    RUN(each_further_return_skips_one_lesson_and_the_prompt_follows_the_number);
    RUN(after_lesson_7_the_next_trigger_re_places_the_lead_ahead_of_the_player_and_starts_lesson_1);
    RUN(the_lead_is_placed_along_the_players_heading_with_no_vertical_offset);
    RUN(the_smoke_key_toggles_the_instructors_trail_bit_in_part_2_only);
    RUN(the_end_wait_is_36_steps_in_part_1_and_60_in_part_2_at_the_instructors_clock);
    RUN(the_instructor_state_machine_runs_only_for_a_scripted_active_instructor_with_the_simulation_on);
    RUN(the_counter_is_decremented_once_per_step_before_the_state_routine);
    RUN(part_2_ends_once_when_the_instructor_loses_its_valid_bit_and_part_1_never_does);
    RUN(the_pass_skips_a_record_4_whose_valid_bit_is_clear_8_9);
    RUN(the_mode_bytes_map_to_the_parts);
    RUN(a_state_change_falls_into_the_next_state_in_the_same_step);
    RUN(the_take_off_throttle_up_keeps_the_other_throttle_bit);
    RUN(the_barrel_roll_ends_on_the_second_flag_whatever_the_first);
    RUN(a_re_position_enters_the_advance_past_its_own_re_placement_so_lesson_7_places_once);
    RUN(the_step_clears_the_proximity_bit_of_the_instructor);
    RUN(an_unscripted_instructor_takes_state_6_when_its_counter_runs_out_and_has_its_key_byte_cut_to_0xC3_and_never_levels);
    RUN(the_wait_state_levels_the_wings_with_a_roll_command_from_the_roll_the_pass_began_with);
    RUN(the_wait_state_6_levels_the_wings_as_state_0A_does_note_8_3_text_NOTE_GAP);
    RUN(a_freshly_placed_record_loses_the_placement_bit_in_its_first_pass);
    RUN(real_assets_the_stage_lists_of_8_1_from_the_menu_key_to_the_first_instructor_step);
    RUN(real_assets_training_part_1_keeps_the_hud_step_50_in_the_external_view_through_the_first_flight_pass);
    RUN(real_assets_training_part_2_is_seen_from_outside_through_the_approach_and_the_cockpit_comes_at_pass_30_of_31);
    RUN(real_assets_training_part_1_keeps_v4a900_at_3_while_the_player_stands_at_start_block_0);
    RUN(real_assets_a_lost_lead_in_part_1_hands_the_view_back_to_the_dormant_player);
    RUN(real_assets_the_part_1_lead_is_in_the_scene_at_its_placed_position_during_the_briefing_and_0x12DB8);
    RUN(real_assets_menu_item_3_starts_part_1_with_the_instructor_at_the_note_position_and_the_camera_on_it);
    RUN(real_assets_part_1_lesson_starts_are_the_originals_exact_steps_0_93_196_384_541_690_780_962);
    RUN(real_assets_part_1_ignores_the_flight_controls_and_return_skips_a_lesson);
    RUN(real_assets_part_2_the_lead_takes_off_and_waits_with_the_first_prompt_return_repositions_and_the_trigger_starts_a_lesson);
    RUN(real_assets_esc_in_part_1_goes_on_to_part_2_and_shift_esc_returns_to_the_menu);
    RUN(real_assets_a_crash_in_part_2_ends_the_flight_with_the_game_over_text_the_allowance_is_1);
    RUN(real_assets_return_during_a_pause_in_part_2_repositions_the_player_and_nothing_else_steps);
    RUN(the_advance_never_touches_the_view_selector_in_part_2_and_sets_it_to_1_in_part_1);
    RUN(real_assets_the_lead_is_record_4_of_the_entity_array_and_its_pass_goes_through_the_world_update_order);
    RUN(real_assets_losing_the_lead_in_part_2_raises_the_end_request_once_the_freeze_is_14_blanks_2_steps_and_the_pass_skips_record_4_for_good);
    TEST_MAIN_END();
}
