/* tests/test_crash.c - takeoff, landing and crash flow at the game level (milestone 3 Task 6; docs/re/05-flight-model.md sections 9, 11, 14,
 * 15, 17.1, 17.3, 17.12, 17.14, 17.17). Runs the whole game (game_step) on the extracted assets (assets/ or argv[1]); skipped (exit 0) when the
 * flight tables or the models are absent. Timing: one game_step is one pass at FLIGHT_STEP_HZ = 8, that is 6.25 blanks (game.c head). Cockpit
 * crash: impact pass N, hand-over pass N+1 (its own step), the hold of 13 frozen steps (80 blanks), restart pass A, B, five waits C1-C5, and C6,
 * the first simulated step (the eighth pass counted from A). */
#include "test.h"
#include "../src/core/game.h"
#include "../src/core/flight.h"
#include "../src/core/radar.h"

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

/* the carrier of free flight, record 14: (295174144, 0, 274202624), heading 0, for both start kinds (17.17 rule 5; the world's entry 33); the deck
 * start is +11264 / +106496 from it (captured: player (295185408, 30472, 274309120)). */
#define CARRIER_X 295174144
#define CARRIER_Z 274202624
#define DECK_X 295185408
#define DECK_Y 30472
#define DECK_Z 274309120
/* runway start block 0 */
#define RWY_X 273963296
#define RWY_Y 1800
#define RWY_Z 279184624

static game_t *start_game(int runway) {
    char err[128];
    game_t *g = game_create(&loader, err, sizeof err);
    if (!g) { printf("  (game_create: %s)\n", err); return NULL; }
    game_debug_start_flight(g, runway ? 0 : 3, 0);   /* the flight at once: the screens' own start is tested in test_screen.c */
    return g;
}
static void axis(game_t *g, int code, int v) { game_event_t ev = { GAME_EV_AXIS, code, v }; game_input(g, &ev); }
static uint32_t scene_n(const game_t *g) { game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s); game_scene(g, s); uint32_t n = s->hud.cp_n; free(s); return n; }
static int scene_dma_ons(const game_t *g) { game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s); game_scene(g, s); int n = 0; for (int i = 0; i < s->audio_count; i++) n += s->audio[i].op == GAME_AUD_DMA_ON; free(s); return n; }
static int scene_crashed(const game_t *g) { game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s); game_scene(g, s); int c = s->hud.cp_crashed; free(s); return c; }

/* Stages an airborne state as test_flight's air_case does (the game's own environment stays installed). */
static void stage_air(game_t *g, int32_t x, int32_t y, int32_t z, uint16_t pitch, uint16_t heading, int S, int T, uint8_t gear) {
    aircraft_t *ac = game_debug_aircraft(g);
    flight_init(ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    flight_set_position(ac, x, y, z);
    flight_set_attitude(ac, pitch, heading, 0);
    ac->f.w2 = 0; ac->f.b4 = 0;
    ac->f.speed_target = ac->f.speed = (int16_t)S;
    ac->f.throttle = (int8_t)T;
    ac->f.gear = gear;
    flight_sync(ac);
}
/* open sea one cell short of the airfield, and the runway cell: the hard touchdown of E8 (v_y above 0x240 per step) */
#define SEA_X 273272584
#define SEA_Z 280480032

/* Steps until W0 bit 0x0200 is seen after a game_step; returns the index of that step (the impact step N) or -1. */
static int step_to_impact(game_t *g, int max) {
    aircraft_t *ac = game_debug_aircraft(g);
    for (int i = 0; i < max; i++) {
        game_step(g);
        if (ac->f.w0 & 0x0200) return i;
    }
    return -1;
}

/* ---- takeoff and landing ------------------------------------------------------------------------------------------------ */

/* The game starts on the deck at the position of 17.14 (+11264, +106496 from the carrier at attitude 0), and idle with the stick fully back
 * nothing happens (Review Focus 1 at the game level). */
TEST(deck_idle_stays_put_in_the_game) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    ASSERT_EQ_INT(ac->pos[0], DECK_X);
    ASSERT_EQ_INT(ac->pos[1], DECK_Y);
    ASSERT_EQ_INT(ac->pos[2], DECK_Z);
    ASSERT_EQ_INT(ac->f.heading, 0);
    axis(g, GAME_AXIS_STICK_Y, 32767);                 /* stick fully back */
    axis(g, GAME_AXIS_THROTTLE, -32768);               /* lever at idle */
    for (int i = 0; i < 300; i++) {
        game_step(g);
        ASSERT_TRUE(ac->f.speed >= 0);
        ASSERT_EQ_INT(ac->pos[1], DECK_Y);
    }
    ASSERT_EQ_INT(ac->pos[0], DECK_X);
    ASSERT_EQ_INT(ac->pos[2], DECK_Z);
    ASSERT_EQ_INT(ac->f.speed, 0);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_NONE);
    ASSERT_EQ_INT(game_crash_count(g), 0);
    ASSERT_EQ_INT(ac->on_carrier, 1);
    game_destroy(g);
}

/* Full throttle on the deck: the catapult fires and the aircraft leaves the deck at flying speed. */
TEST(catapult_launch_reaches_flying_speed) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    axis(g, GAME_AXIS_THROTTLE, 32767);
    int off = -1;
    for (int i = 0; i < 400 && off < 0; i++) {
        game_step(g);
        if (!(ac->f.w2 & 0x0080)) off = i;             /* wheels off the deck */
    }
    ASSERT_TRUE(off > 0);
    for (int i = 0; i < 40; i++) game_step(g);
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    game_scene(g, s);
    ASSERT_TRUE(s->hud.airspeed_kt >= 150);            /* above the stall speed S = 1700 (141 kt) with the engine at full power */
    ASSERT_TRUE(ac->pos[1] > 10000);                   /* airborne and clear of the sea (the wheels-down aircraft sinks slowly without a pull) */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_NONE);
    free(s);
    game_destroy(g);
}

/* Approach with the hook down and the wire engaged: the aircraft stops on the deck (E6 at the game level, carrier at attitude 0). */
TEST(landing_in_the_wire_zone_with_the_hook_down_stops_the_aircraft) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    stage_air(g, CARRIER_X, 37472, CARRIER_Z - 260000, 0, 0, 1700, 40, 0);
    ac->f.w2 = 0x8000;                                 /* hook down */
    flight_sync(ac);
    int engaged = -1, stopped = -1;
    for (int i = 0; i < 400 && stopped < 0; i++) {
        game_step(g);
        if (engaged < 0 && (ac->f.w2 & 0x4000)) engaged = i;
        if (engaged >= 0 && ac->f.speed == 0) stopped = i;
        ASSERT_EQ_INT(ac->f.w0 & 0x0200, 0);           /* no crash on the way */
    }
    ASSERT_TRUE(engaged >= 0);
    ASSERT_TRUE(stopped > engaged);
    ASSERT_EQ_INT(ac->pos[1], DECK_Y);                 /* on the deck */
    ASSERT_TRUE(ac->f.b4 & 0xC0);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_NONE);
    game_destroy(g);
}

/* ---- the crash flow, cockpit view -------------------------------------------------------------------------------------------- */

/* The cockpit crash of 17.17: impact step N; the hand-over in the dispatcher of pass N+1 (before its step) clears W0 bit 0x0200 and the pass runs
 * its single step and counts n; the hold follows at once (no extra pass): 13 frozen steps with no flight step, generator, arbiter or n; pass A
 * restarts and also steps (bit 6 clear: nothing moves), counts n and rebuilds the lists; B; C1-C5; C6 sets bit 6 and is the first simulated step. */
#define HOLD_STEPS 13
static void crash_timeline(game_t *g, int32_t x, int32_t z) {
    aircraft_t *ac = game_debug_aircraft(g);
    const int N = step_to_impact(g, 60);
    ASSERT_TRUE(N >= 0);
    ASSERT_EQ_INT(ac->pos[1], 1800);                   /* the floor of the gear-down aircraft */
    ASSERT_TRUE(ac->f.w0 & 0x0200);
    ASSERT_EQ_INT(ac->f.v4a87d, 1);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_NONE);   /* the hand-over has not run yet: it runs in the next pass */
    ASSERT_EQ_INT(game_crash_count(g), 0);
    ASSERT_EQ_INT(game_crash_allowance(g), 5);         /* free flight (17.17 rule 7) */
    const int32_t ix = ac->pos[0], iz = ac->pos[2];
    ASSERT_TRUE(ix != x || iz != z);
    const uint32_t steps_i = ac->f.steps;
    const uint32_t n_i = scene_n(g);
    game_step(g);                                      /* pass N+1: hand-over, then the step */
    ASSERT_EQ_INT(ac->f.w0 & 0x0200, 0);               /* cleared before this pass's step saw it */
    ASSERT_EQ_INT(ac->f.v4a952, 1);                    /* immune */
    ASSERT_EQ_INT(ac->f.vel[1], 0);
    ASSERT_EQ_INT(ac->f.v4a984 & 0x40, 0);             /* the simulation stops */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_HOLD);   /* the hold begins at the end of this pass: no pass N+2 */
    ASSERT_EQ_INT(game_crash_count(g), 1);
    ASSERT_EQ_INT(game_crash_allowance(g), 5);
    ASSERT_EQ_INT(ac->f.steps, steps_i + 1);           /* the hand-over pass stepped once */
    ASSERT_EQ_INT(scene_n(g), n_i + 1);                /* and counted */
    const uint32_t steps_f = ac->f.steps;
    const uint32_t n_f = scene_n(g);
    ASSERT_EQ_INT(scene_crashed(g), 1);                /* the external strip is not drawn after the hand-over */
    int hb;
    game_crash_state(g, &hb);
    ASSERT_EQ_INT(hb, 80);
    /* the hold: 13 frozen passes at the impact position, no flight step, n still, the last frame stays */
    for (int k = 1; k <= HOLD_STEPS; k++) {
        game_step(g);
        ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_HOLD);
        ASSERT_EQ_INT(ac->pos[0], ix);
        ASSERT_EQ_INT(ac->pos[2], iz);
        ASSERT_EQ_INT(ac->pos[1], 1800);
        ASSERT_EQ_INT(ac->f.steps, steps_f);
        ASSERT_EQ_INT(scene_n(g), n_f);
        ASSERT_EQ_INT(scene_crashed(g), 1);
        if (k == 12) {
            game_crash_state(g, &hb);
            ASSERT_EQ_INT(hb, 80 - (12 * 50) / 8);     /* 12 steps of 6.25 blanks = 75 blanks gone, 5 left */
        }
    }
    /* pass A: the dispatcher restarts, the pass steps with bit 6 clear and counts n */
    game_step(g);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME);
    ASSERT_EQ_INT(ac->f.steps, steps_f + 1);
    ASSERT_EQ_INT(scene_n(g), n_f + 1);                /* n counts through the restart: not cleared (section 15) */
    ASSERT_EQ_INT(game_crash_allowance(g), 4);         /* decremented in the wait state after 0x1D538 */
    ASSERT_EQ_INT(game_flash(g), 1);                   /* the hold has just ended: one orange frame precedes this one (17.17 (xii)) */
    ASSERT_EQ_INT(ac->pos[0], DECK_X);
    ASSERT_EQ_INT(ac->pos[1], DECK_Y);
    ASSERT_EQ_INT(ac->pos[2], DECK_Z);
    ASSERT_EQ_INT(ac->f.v4a952, 0);                    /* 0x1D538 clears it */
    ASSERT_EQ_INT(ac->f.v4a984 & 0x40, 0);             /* the simulation switch stays clear until C6 */
    ASSERT_EQ_INT(scene_crashed(g), 1);
    ASSERT_EQ_INT(ac->f.fuel, 0x61A800);               /* refilled by the start (rule 4) */
    /* B and C1-C5: seven passes without simulation in all (A included), each one a full pass that counts n */
    for (int k = 1; k <= 6; k++) {
        game_step(g);
        ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME);
        ASSERT_EQ_INT(ac->f.steps, steps_f + 1 + (uint32_t)k);
        ASSERT_EQ_INT(scene_n(g), n_f + 1 + (uint32_t)k);
        ASSERT_EQ_INT(ac->f.v4a984 & 0x40, 0);
        ASSERT_EQ_INT(ac->pos[0], DECK_X);
        ASSERT_EQ_INT(ac->pos[1], DECK_Y);
        ASSERT_EQ_INT(ac->pos[2], DECK_Z);
        ASSERT_EQ_INT(ac->f.speed, 0);
        ASSERT_EQ_INT(scene_crashed(g), 1);
        if (k == 1) ASSERT_EQ_INT(ac->f.w2 & 0x0002, 0x0002);   /* B: W2 bit 1 */
    }
    game_step(g);                                      /* C6: bit 6 set before the world update: the eighth pass counted from A, the first simulated step */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_NONE);
    ASSERT_EQ_INT(ac->f.v4a984 & 0x40, 0x40);
    ASSERT_EQ_INT(ac->f.v4a968, 0);                    /* C6 posts 0xFE and the sound routine of the same pass takes it (note 10 5.1): the engine is restarted in this step */
    ASSERT_TRUE(scene_dma_ons(g) >= 2);                /* the engine pair (or the wind) is started on voices 0 and 1 */
    ASSERT_EQ_INT(ac->f.steps, steps_f + 8);
    ASSERT_EQ_INT(scene_n(g), n_f + 8);
    ASSERT_EQ_INT(scene_crashed(g), 0);
    ASSERT_EQ_INT(ac->f.v4a952, 0);
    ASSERT_EQ_INT(game_crash_count(g), 1);
    ASSERT_EQ_INT(ac->f.v4a87d, 1);                    /* the statistic is kept */
}

TEST(sea_crash_holds_then_restarts_on_the_deck) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 5; i++) game_step(g);          /* n is not zero at the crash */
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    crash_timeline(g, DECK_X, DECK_Z);
    game_destroy(g);
}

/* The same on land: the runway cell (the terrain query answers land). */
TEST(terrain_crash_holds_and_restarts_the_same_way) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    stage_air(g, RWY_X + 40000, 20000, RWY_Z + 40000, 2000, 2240, 3000, 60, 0);
    crash_timeline(g, DECK_X, DECK_Z);
    game_destroy(g);
}

/* The restart of a runway start returns to the start block (17.14, block 0); the view is not touched (17.17 rule 11). */
TEST(runway_start_restarts_on_the_runway) {
    game_t *g = start_game(1);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    ASSERT_EQ_INT(ac->pos[0], RWY_X);
    ASSERT_EQ_INT(ac->pos[1], RWY_Y);
    ASSERT_EQ_INT(ac->pos[2], RWY_Z);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    for (int i = 0; i < 1 + HOLD_STEPS + 8; i++) game_step(g);   /* hand-over pass, hold, A to C6 */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_NONE);
    ASSERT_EQ_INT(game_crash_count(g), 1);
    ASSERT_EQ_INT(ac->pos[0], RWY_X);
    ASSERT_EQ_INT(ac->pos[1], RWY_Y);
    ASSERT_EQ_INT(ac->pos[2], RWY_Z);
    ASSERT_EQ_INT(ac->f.heading, 2240);
    game_destroy(g);
}

/* What the restart writes and what survives it, at the game level (17.17 rule 4; flight_restart is the module's half). Staged: marker values in
 * the kept fields, a crash, the hold, then A to C6. */
TEST(the_crash_restart_clears_the_f1_counter_and_the_ecm_switch_as_the_player_start_does) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 3; i++) game_step(g);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    aircraft_t *ac = game_debug_aircraft(g);
    radar_t *rd = (radar_t *)game_debug_radar(g);
    ASSERT_TRUE(rd != NULL);
    rd->mission1 = 0x15; ac->f.v4a96d = 1;             /* the F1 counter part way, the ECM switch on */
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    ASSERT_EQ_INT(ac->f.v4a96d, 1);                    /* the crash itself leaves both */
    int n = 0;
    while (game_crash_state(g, NULL) != GAME_CRASH_RESUME && n++ < 40) game_step(g);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME);   /* pass A just ran the restart */
    ASSERT_EQ_INT(rd->mission1, 0);
    ASSERT_EQ_INT(ac->f.v4a96d, 0);
}

TEST(restart_writes_and_keeps_the_fields_of_17_17_rule_4) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 3; i++) game_step(g);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    aircraft_t *ac = game_debug_aircraft(g);
    flight_state_t *f = &ac->f;
    f->fuel = 0x300000; f->v4a963 = 7; f->v4a982 = 0x0301; f->v4a98a = 0x1234; f->v4a903 = 3; f->v4a9fe = 5; f->v4a84f = 2;
    f->w2 |= 0xC455;                                   /* hook down, hook engaged, idle detent 0x0400, 0x0040, 0x0010, 0x0004, 0x0001 */
    f->v4a8fe = 1; f->v4a994 = 0; f->v4a996 = 0; f->v4a850 = 3; f->v4a986 = 0x0100; f->v4a83c = 0; f->v4a847 = 0; f->v4a95e = 0;
    f->v4a928 = 77; f->v4aa7a = 0x0200; f->v4ab98 = 0x4002; f->v4a830 = 500;
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    f->w2 |= 0x0040;                                   /* markers in the surviving bits, set at the impact */
    const flight_state_t k = *f;
    int n = 0;
    while (game_crash_state(g, NULL) != GAME_CRASH_RESUME && n++ < 40) { game_step(g); f->v4a84f = k.v4a84f; f->v4a8fe = 1; }
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME);   /* pass A just ran */
    /* written (rule 4 table) */
    ASSERT_EQ_INT(f->cell_a, 10);                      /* the carrier restart's +0x0A/+0x0B (17.18 f): 10 and 9 */
    ASSERT_EQ_INT(f->cell_b, 9);
    ASSERT_EQ_INT(f->fuel, 0x61A800);                  /* refilled */
    ASSERT_EQ_INT(f->v4a928, 0);                       /* the throttle preset is cleared */
    ASSERT_EQ_INT(f->v4aa7a, 0);                       /* the ejection latch is reset */
    ASSERT_EQ_INT(f->v4a830, 72);                      /* P = Q = 72 */
    ASSERT_EQ_INT(f->v4a834, 72);
    ASSERT_EQ_INT(f->v4a890, 1);
    ASSERT_EQ_INT(f->id, 0x0D);
    ASSERT_EQ_INT(f->w0 & 0x0200, 0);
    ASSERT_EQ_INT(f->w0, 0x91C8);                      /* (xi): A's ground test read the crash attitude's slots: 0x8000 is set after A (B clears it) */
    ASSERT_EQ_INT(f->w2 & (0xB108u & ~0x0080u), 0);    /* W2: hook down, 0x2000, 0x1000, 0x0100, 0x0008 cleared */
    ASSERT_EQ_INT(f->w2 & 0x0080, 0x0080);             /* and the ground bit set */
    ASSERT_TRUE(k.w2 & 0x0040);                         /* every other bit survives the start (bit 0x0040 as the crash left it; the collision step */
    ASSERT_EQ_INT(f->w2 & 0x0040, k.w2 & 0x0040);       /* may clear the engaged bit 0x4000 and the detent 0x0400 itself once the hook is up) */
    ASSERT_EQ_INT(f->b4 & ~0xFD, 0);                   /* B4 & 0x35, then the carrier start's 0xC8 */
    ASSERT_EQ_INT(f->pitch, 0);                        /* level: the carrier record's attitude (0, 0, 0) */
    ASSERT_EQ_INT(f->heading, 0);
    ASSERT_EQ_INT(f->roll, 0);
    ASSERT_EQ_INT(f->speed, 0);
    ASSERT_EQ_INT(f->vel[0] | f->vel[1] | f->vel[2], 0);
    /* kept */
    ASSERT_EQ_INT(f->v4a963, 7);
    ASSERT_EQ_INT(f->v4a98a & 0xFFFC, 0x1234);
    ASSERT_EQ_INT(f->v4a903, 3);
    ASSERT_EQ_INT(f->v4a9fe, 5);
    ASSERT_EQ_INT(f->v4a8fe, 1);
    ASSERT_EQ_INT(f->v4a994, 0);
    ASSERT_EQ_INT(f->v4a996, 0);
    ASSERT_EQ_INT(f->v4a850 >= 3, 1);
    ASSERT_EQ_INT(f->v4a83c, 0);
    ASSERT_EQ_INT(f->v4a847, 0);
    ASSERT_EQ_INT(f->v4a95e, 0);
    ASSERT_EQ_INT(f->v4a982 & 0x100, 0);               /* bit 8 cleared by the hand-over, set again only by the next lift-off */
    ASSERT_EQ_INT(f->v4a982 & 0x0201, 0x0201);         /* the other bits kept */
    ASSERT_EQ_INT(f->v4a984 & 0x40, 0);                /* only C6 sets the switch */
    ASSERT_EQ_INT(f->v4a984 & 0x0800, 0x0800);         /* bit 0x0800 is set at mission start and never cleared */
    /* through B and C1-C6 */
    for (int i = 0; i < 7; i++) game_step(g);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_NONE);
    ASSERT_EQ_INT(f->v4a984 & 0x40, 0x40);
    ASSERT_EQ_INT(f->w2 & 0x0002, 0x0002);             /* B */
    ASSERT_EQ_INT(f->v4a968, 0);                       /* C6 posted 0xFE and the pass's sound routine took it */
    ASSERT_EQ_INT(f->v4a963, 7);
    game_destroy(g);
}

/* The queue of the hold (17.17 rule 6): the frame routine does not run, so the view, flight and pause keys wait and pass A delivers them, in
 * arrival order, before its dispatcher. The frame is frozen (line, n, view); the restart still completes; the view key acts in A (the view is
 * external from then on, the restart does not reset it); the pause makes the resume passes run without n. */
TEST(hold_queues_the_keys_and_pass_a_applies_them) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    for (int i = 0; i < 6; i++) game_step(g);          /* n is not zero */
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    game_step(g);                                      /* hand-over pass: the hold begins */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_HOLD);
    game_scene_t *a = (game_scene_t *)calloc(1, sizeof *a), *b = (game_scene_t *)calloc(1, sizeof *b);
    game_scene(g, a);
    ASSERT_TRUE(a->hud.cp_n != 0);
    const uint32_t steps = ac->f.steps;
    game_event_t kp = { GAME_EV_BUTTON, GAME_BTN_KEYPAD, GAME_KP_ENTER };
    game_event_t gear = { GAME_EV_BUTTON, GAME_BTN_GEAR, 1 };
    for (int k = 0; k < HOLD_STEPS; k++) {
        if (k == 3) game_input(g, &kp);                /* the external view key, once (make and break are one toggle) */
        if (k == 5) game_input(g, &gear);
        game_step(g);
        ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_HOLD);   /* a view toggle never cancels, shortens or lengthens a hold */
        game_scene(g, b);
        ASSERT_EQ_INT(b->mode, GAME_MODE_FLIGHT);
        ASSERT_EQ_INT(b->hud.cp_n, a->hud.cp_n);
        ASSERT_EQ_INT(b->camera.view, GAME_VIEW_COCKPIT);          /* queued, not applied */
        ASSERT_EQ_INT(ac->f.steps, steps);
    }
    ASSERT_EQ_INT(b->hud.cp_msg_code, a->hud.cp_msg_code);     /* the message line is frozen as it was */
    ASSERT_EQ_INT(b->hud.cp_msg_entry, a->hud.cp_msg_entry);
    ASSERT_EQ_INT(b->hud.cp_msg_phase, a->hud.cp_msg_phase);
    ASSERT_EQ_INT(ac->f.gear & 0x80, 0);               /* the gear key did not act yet */
    game_step(g);                                      /* pass A */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME);   /* the restart happened anyway */
    game_scene(g, b);
    ASSERT_EQ_INT(b->camera.view, GAME_VIEW_CHASE);    /* the queued toggle was applied in A and the restart did not reset the view */
    ASSERT_EQ_INT(b->hud.cp_n, a->hud.cp_n + 1);
    ASSERT_EQ_INT(ac->pos[0], DECK_X);
    ASSERT_EQ_INT(ac->pos[1], DECK_Y);
    for (int i = 0; i < 7; i++) game_step(g);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_NONE);
    game_scene(g, b);
    ASSERT_EQ_INT(b->camera.view, GAME_VIEW_CHASE);
    free(a); free(b);
    game_destroy(g);
}

/* A pause queued in the hold takes effect from pass A on: the restart completes, the resume states run on (the pause does not stop the state routines),
 * n stands still, and after C6 the flight state is paused: nothing is simulated until the pause key is pressed again. */
TEST(a_pause_queued_in_the_hold_lets_the_resume_states_run_without_n) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    for (int i = 0; i < 6; i++) game_step(g);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    game_step(g);
    const uint32_t n0 = scene_n(g), steps = ac->f.steps;
    game_event_t pause = { GAME_EV_BUTTON, GAME_BTN_PAUSE, 1 };
    game_input(g, &pause);
    for (int k = 0; k < HOLD_STEPS; k++) { game_step(g); ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_HOLD); }
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    game_scene(g, s);
    ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT);          /* still queued */
    game_step(g);                                      /* A: delivers the pause and restarts */
    game_scene(g, s);
    ASSERT_EQ_INT(s->mode, GAME_MODE_PAUSED);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME);
    ASSERT_EQ_INT(ac->pos[0], DECK_X);                 /* restarted */
    for (int i = 0; i < 6; i++) { game_step(g); ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME); }   /* B, C1-C5 */
    game_step(g);                                      /* C6 under the pause */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_NONE);
    ASSERT_EQ_INT(ac->f.v4a984 & 0x40, 0x40);
    ASSERT_EQ_INT(scene_n(g), n0);                     /* n stood still through the whole restart */
    ASSERT_EQ_INT(ac->f.steps, steps);                 /* and nothing was simulated */
    for (int i = 0; i < 5; i++) game_step(g);
    ASSERT_EQ_INT(scene_n(g), n0);
    game_input(g, &pause);                             /* resume */
    game_step(g);
    game_scene(g, s);
    ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT);
    ASSERT_EQ_INT(scene_n(g), n0 + 1);                   /* no handler clears n & 7 on a pause (note 06 "View kinds, settled" item 8): the first pass counts on from n */
    free(s);
    game_destroy(g);
}

/* A view key polled in the hand-over pass is applied after its dispatcher, so the hand-over still takes the cockpit branch and the hold is
 * unchanged (17.17 rule 1b, capture crash-keys-view-before-handover): the key arrives before the pass N+1 step here. */
TEST(a_view_key_in_the_hand_over_pass_does_not_cancel_the_hold) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    game_event_t kp = { GAME_EV_BUTTON, GAME_BTN_KEYPAD, GAME_KP_ENTER };
    game_input(g, &kp);                                /* polled at the start of N+1 */
    game_step(g);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_HOLD);
    ASSERT_EQ_INT(ac->f.v4a952, 1);                    /* the cockpit branch */
    for (int k = 0; k < HOLD_STEPS; k++) { game_step(g); ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_HOLD); }
    game_step(g);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME);
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    game_scene(g, s);
    ASSERT_EQ_INT(s->camera.view, GAME_VIEW_CHASE);    /* the view is external from the key on and stays so */
    free(s);
    game_destroy(g);
}

/* With collisions disabled (training) the crash is only cleared and the notice 0x4021 posted: no hold, no restart. Result 0x10 (the three-vertex ground
 * test) is not gated by V:4A844 and recurs while the nose is below ground (17.12), so W0 bit 0x0200 is set again by the step. */
TEST(collisions_off_clears_the_crash_and_posts_the_notice) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    ac->f.v4a844 = 0;
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    game_step(g);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_NONE);   /* no hold */
    ASSERT_EQ_INT(game_crash_count(g), 0);
    ASSERT_EQ_INT(ac->f.v4ab98, 0x4021);
    ASSERT_EQ_INT(ac->f.v4a984 & 0x40, 0x40);          /* the simulation was never stopped */
    ASSERT_EQ_INT(ac->f.w0 & 0x0200, 0x0200);         /* set again by this step (17.12) */
    ASSERT_EQ_INT(ac->f.v4a952, 0);
    game_destroy(g);
}

/* ---- the external view: no hold; the dying state runs until the expiry (17.3) ---------------------------------------------------- */

TEST(external_view_crash_has_no_hold_and_restarts_in_pass_n_plus_16_and_stays_external) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    game_event_t kp = { GAME_EV_BUTTON, GAME_BTN_KEYPAD, GAME_KP_ENTER };
    game_input(g, &kp);                                /* the external view */
    aircraft_t *ac = game_debug_aircraft(g);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    const int N = step_to_impact(g, 60);
    ASSERT_TRUE(N >= 0);
    ASSERT_EQ_INT(ac->f.counter, 15);                  /* finalised at once in the external view */
    ASSERT_TRUE(ac->f.w0 & 0x0400);
    const int32_t ix = ac->pos[0], iz = ac->pos[2];
    game_step(g);                                      /* N+1: the hand-over */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_DYING);
    ASSERT_EQ_INT(game_crash_count(g), 1);
    ASSERT_EQ_INT(ac->f.v4a952, 0);                    /* the cockpit-only immunity is not set */
    const uint32_t n1 = scene_n(g);
    for (int k = 2; k <= 15; k++) {                    /* N+2 .. N+15: no hold, the steps and n run */
        game_step(g);
        ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_DYING);
        ASSERT_EQ_INT(ac->pos[0], ix);
        ASSERT_EQ_INT(ac->pos[2], iz);
        ASSERT_EQ_INT(scene_n(g), n1 + (uint32_t)(k - 1));
    }
    ASSERT_EQ_INT(ac->f.w0 & 0x0400, 0);               /* the expiry in step N+15 */
    game_step(g);                                      /* N+16: pass A, the dispatcher restarts, the step runs on the new record */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME);
    ASSERT_EQ_INT(ac->pos[0], DECK_X);
    ASSERT_EQ_INT(ac->pos[1], DECK_Y);
    ASSERT_EQ_INT(ac->pos[2], DECK_Z);
    ASSERT_EQ_INT(scene_n(g), n1 + 15);                /* n kept and still counting */
    ASSERT_TRUE(ac->f.w0 & 0x0040);                    /* valid again */
    for (int i = 0; i < 6; i++) { game_step(g); ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME); ASSERT_EQ_INT(ac->f.v4a984 & 0x40, 0); }
    game_step(g);                                      /* C6: the eighth pass counted from A */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_NONE);
    ASSERT_EQ_INT(ac->f.v4a984 & 0x40, 0x40);
    ASSERT_EQ_INT(scene_n(g), n1 + 22);
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    game_scene(g, s);
    ASSERT_EQ_INT(s->camera.view, GAME_VIEW_CHASE);    /* 0x1D538 does not reset the view: the external restart stays external (17.17 rule 10) */
    free(s);
    game_destroy(g);
}

/* Crash in an external view, then KPENTER back to the cockpit while dying: 0x138D8 tests V:4A83D first, the cockpit view waits for V:4A951 (0 here),
 * so the restart is made at once; the 17.3 expiry would never come (it runs only in the external view). */
TEST(dying_then_back_to_the_cockpit_restarts_at_once) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    game_event_t kp = { GAME_EV_BUTTON, GAME_BTN_KEYPAD, GAME_KP_ENTER };
    game_input(g, &kp);
    aircraft_t *ac = game_debug_aircraft(g);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    game_step(g);                                      /* N+1 */
    game_step(g);                                      /* N+2: dying */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_DYING);
    ASSERT_TRUE(ac->f.w0 & 0x0400);
    game_input(g, &kp);                                /* back to the cockpit: polled in the next pass, applied after its dispatcher */
    game_step(g);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_DYING);   /* that dispatcher still saw the external view */
    game_step(g);                                      /* the next pass restarts at once (17.17 rule 1a) */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME);
    ASSERT_EQ_INT(ac->pos[0], DECK_X);
    ASSERT_EQ_INT(ac->pos[1], DECK_Y);
    ASSERT_EQ_INT(ac->f.w0 & 0x0400, 0);
    for (int i = 0; i < 40; i++) game_step(g);         /* the resume passes, then flying on: no second hand-over, nothing waiting */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_NONE);
    ASSERT_EQ_INT(game_crash_count(g), 1);
    game_destroy(g);
}

/* The converse (17.17 rule 7): a cockpit crash, the view switched to external after the hand-over, during the hold: the toggle is queued and applied in
 * pass A after... before the dispatcher's restart, which happens anyway; the hold is unchanged and the view is external from A on. */
TEST(cockpit_crash_with_a_switch_to_external_during_the_hold_changes_nothing) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    game_step(g);                                      /* N+1: hand-over, the hold begins */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_HOLD);
    game_event_t kp = { GAME_EV_BUTTON, GAME_BTN_KEYPAD, GAME_KP_ENTER };
    game_input(g, &kp);
    const uint32_t steps = ac->f.steps;
    for (int k = 0; k < HOLD_STEPS; k++) {
        game_step(g);
        ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_HOLD);   /* the hold is neither cancelled nor shortened */
        ASSERT_EQ_INT(ac->f.steps, steps);
    }
    game_step(g);                                      /* A */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME);
    ASSERT_EQ_INT(ac->pos[0], DECK_X);
    ASSERT_EQ_INT(ac->pos[1], DECK_Y);
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    game_scene(g, s);
    ASSERT_EQ_INT(s->camera.view, GAME_VIEW_CHASE);
    free(s);
    game_destroy(g);
}

/* ---- the crash allowance and the game-over path (17.17 rule 7) ------------------------------------------------------------------- */

/* Crashes the staged aircraft once and runs the whole sequence to the end of C6 (or the game-over screen). */
static void crash_once(game_t *g) {
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    for (int i = 0; i < 1 + HOLD_STEPS + 8; i++) {
        game_step(g);
        game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
        game_scene(g, s);
        int over = s->mode == GAME_MODE_GAMEOVER;
        free(s);
        if (over) break;
    }
}

/* Pass A's world update (17.17 (xi), rule 2 staged record): the ground test reads the vertex slots of the LAST DRAW before the hold (the crash attitude), so
 * after A W0 reads 0x91C8 and +0x0A/+0x0B read 06/0F; B's 0x1D538 gives 0x11C8, 06/06 and W2 0x0082, and the slots A's draw refreshed give no result
 * again. Both views (the external restart is pass N+16). */
static void staged_record(int external) {
    game_t *g = start_game(1);                         /* the runway restart of the captures (a carrier restart refreshes nothing: its record already holds the cell) */
    ASSERT_TRUE(g != NULL);
    if (external) { game_event_t kp = { GAME_EV_BUTTON, GAME_BTN_KEYPAD, GAME_KP_ENTER }; game_input(g, &kp); }
    aircraft_t *ac = game_debug_aircraft(g);
    flight_state_t *f = &ac->f;
    for (int i = 0; i < 3; i++) game_step(g);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    int n = 0;
    while (game_crash_state(g, NULL) != GAME_CRASH_RESUME && n++ < 60) game_step(g);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME);   /* pass A just ran */
    ASSERT_EQ_INT(f->w0, 0x91C8);                      /* 0x8000 from result 0x10 (+0x7B >= 0), 0x0200 clear */
    ASSERT_EQ_INT(f->cell_a, 0x06);                    /* +0x0A: the cell refresh */
    ASSERT_EQ_INT(f->cell_b, 0x0F);                    /* +0x0B: the neighbour rule */
    ASSERT_EQ_INT(f->w2 & 0x00FF, 0x0080);             /* A wrote 0x0080 (bit 0x0002 is B's) */
    game_step(g);                                      /* B */
    ASSERT_EQ_INT(f->w0, 0x11C8);                      /* B's 0x1D538 restores it; A's draw replaced the slots: no result from here on */
    ASSERT_EQ_INT(f->cell_a, 0x06);
    ASSERT_EQ_INT(f->cell_b, 0x06);
    ASSERT_EQ_INT(f->w2 & 0xFF, 0x80);                 /* B's start wrote 0x0082; the same pass's no-result tail returns it to 0x0080 (V:4A957 = 0 on the runway; the carrier keeps 0x0082) */
    for (int i = 0; i < 6; i++) { game_step(g); ASSERT_EQ_INT(f->w0 & 0x8000, 0); }   /* C1-C5 and the first simulated step */
    game_destroy(g);
}
TEST(pass_a_ground_test_reads_the_crash_slots_in_the_cockpit_view) { staged_record(0); }
TEST(pass_a_ground_test_reads_the_crash_slots_in_the_external_view) { staged_record(1); }

/* (xiv) A paused restart leaves the record as 0x1D538 wrote it: the player step of A does not run, so W0 stays 0x11C8 and +0x0A/+0x0B stay 05/06. */
TEST(a_paused_restart_leaves_the_record_as_the_start_wrote_it) {
    game_t *g = start_game(1);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    flight_state_t *f = &ac->f;
    for (int i = 0; i < 3; i++) game_step(g);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    game_step(g);
    game_event_t pause = { GAME_EV_BUTTON, GAME_BTN_PAUSE, 1 };
    game_input(g, &pause);
    for (int k = 0; k < HOLD_STEPS; k++) game_step(g);
    const uint8_t crash_cell = f->cell_a;              /* the crash cell's sub-index (+0x0A), which 0x1D538 does not write on a runway start */
    game_step(g);                                      /* A, paused */
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME);
    ASSERT_EQ_INT(game_flash(g), 1);
    ASSERT_EQ_INT(f->w0, 0x11C8);
    ASSERT_EQ_INT(f->cell_a, crash_cell);              /* the refresh did not run (the capture: 05, the crash cell of its run) */
    ASSERT_TRUE(crash_cell != 0x06);                   /* and it would have changed it: the runway cell's sub-index is 06 */
    ASSERT_EQ_INT(f->cell_b, 0x06);
    for (int i = 0; i < 7; i++) game_step(g);          /* B and C1-C6 under the pause: nothing runs the player step */
    ASSERT_EQ_INT(f->w0, 0x11C8);
    ASSERT_EQ_INT(game_flash(g), 0);                   /* one frame only */
    game_destroy(g);
}

/* (xii) The orange frame is one step's flag at the end of the hold, nothing before or after. */
TEST(the_flash_is_set_only_in_the_step_where_the_hold_ends) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    int flashes = 0, where = -1;
    for (int i = 0; i < 1 + HOLD_STEPS + 12; i++) {
        game_step(g);
        game_scene(g, s);
        if (s->flash_frames) { flashes += s->flash_frames; where = i; }
    }
    ASSERT_EQ_INT(flashes, 1);
    ASSERT_EQ_INT(where, 1 + HOLD_STEPS);              /* the step of pass A */
    free(s);
    game_destroy(g);
}

TEST(the_fifth_crash_ends_the_flight_with_the_game_over_text) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    for (int c = 1; c <= 4; c++) {
        crash_once(g);
        ASSERT_EQ_INT(game_crash_count(g), c);
        ASSERT_EQ_INT(game_crash_allowance(g), 5 - c);
        ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_NONE);
        game_scene(g, s);
        ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT);
    }
    /* the fifth: pass A restarts as usual, the allowance reaches 0 and the game-over state follows */
    aircraft_t *ac = game_debug_aircraft(g);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    for (int i = 0; i < 1 + HOLD_STEPS; i++) { game_step(g); game_scene(g, s); ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT); }
    const uint32_t steps_before = ac->f.steps;   /* n has no accessor in the game-over mode (no hud in its scene): the step count is the witness that no pass ran */
    game_step(g);                                      /* pass A: the record is rewritten, nothing else runs (17.17 (xiii)) */
    game_scene(g, s);
    ASSERT_EQ_INT(s->mode, GAME_MODE_GAMEOVER);
    ASSERT_EQ_INT(s->text_id, 0x63);                   /* the F/A-18's text (0x62 for the F-16, V:4A901 = 0x10) */
    ASSERT_EQ_INT(s->flash_frames, 1);                 /* the hold ended: the orange frame still precedes */
    ASSERT_EQ_INT(game_crash_allowance(g), 0);
    ASSERT_EQ_INT(game_crash_count(g), 5);
    ASSERT_EQ_INT(ac->pos[0], DECK_X);                 /* A rewrote the record */
    ASSERT_EQ_INT(ac->f.w0, 0x11C8);                   /* and ran no world update: no 0x8000, no cell refresh, +0x0A/+0x0B as 0x1D538 left them */
    ASSERT_EQ_INT(ac->f.steps, steps_before);
    /* note 08 5.12, stages from A (k = 6): the clear screen and the wait 6 blanks = 1 step, the text typed in 78 blanks = 12 steps, the end hold 8 blanks = 1, the reset 1, the
     * menu set-up 5: the menu text 20 steps after A */
    game_scene(g, s);
    ASSERT_EQ_INT(s->text_count, 0);                   /* A's own step: the clear screen with the gate closed */
    ASSERT_EQ_INT(s->palette_regime, 0);               /* the menu palette */
    for (int i = 1; i < 14; i++) {
        game_step(g); game_scene(g, s);
        ASSERT_EQ_INT(s->mode, GAME_MODE_GAMEOVER); ASSERT_EQ_INT(s->flash_frames, 0); ASSERT_EQ_INT(ac->f.steps, steps_before);
        if (i == 1 && s->text_count == 0) FAIL("the game-over text starts one step after A");
        ASSERT_EQ_INT(s->text_id, 0x63);
    }
    ASSERT_TRUE(s->text_count >= 1);                   /* the text is on screen until the reset clears it */
    game_step(g);                                      /* A + 14: the end hold is over, 0x11A70 resets: n cleared, the screen is left for the menu set-up */
    game_scene(g, s);
    ASSERT_EQ_INT(s->mode, GAME_MODE_MENU);
    ASSERT_EQ_INT(s->text_count, 0);
    ASSERT_EQ_INT(scene_n(g), 0);
    for (int i = 0; i < 5; i++) { game_step(g); game_scene(g, s); ASSERT_EQ_INT(s->mode, GAME_MODE_MENU); ASSERT_EQ_INT(s->text_count, 0); }
    game_step(g); game_scene(g, s);                    /* A + 20: the menu list starts */
    ASSERT_TRUE(s->text_count >= 1);
    free(s);
    game_destroy(g);
}

/* Note 05 17.17 rule 7 and note 08 5.12 (followup-pause-gameover, 3 runs): the game-over steps do not depend on the pause. P pressed in the hold is queued and polled by pass A; the game-over states do not
 * read the pause byte, so the text, the end hold, the reset and the menu list come at the same steps as unpaused (20 from A) and no pause screen opens. */
TEST(the_game_over_steps_do_not_depend_on_a_pause_queued_in_the_hold) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    for (int c = 1; c <= 4; c++) crash_once(g);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    for (int i = 0; i < 1 + HOLD_STEPS; i++) {
        game_step(g);
        if (i == 5) { game_event_t p = { GAME_EV_BUTTON, GAME_BTN_PAUSE, 1 }; game_input(g, &p); }
    }
    game_step(g);                                      /* pass A */
    game_scene(g, s);
    ASSERT_EQ_INT(s->mode, GAME_MODE_GAMEOVER);
    ASSERT_EQ_INT(s->text_id, 0x63);
    for (int i = 1; i < 14; i++) {
        game_step(g); game_scene(g, s);
        ASSERT_EQ_INT(s->mode, GAME_MODE_GAMEOVER);
        if (i == 1 && s->text_count == 0) FAIL("the game-over text starts one step after A, paused or not");
    }
    ASSERT_TRUE(s->text_count >= 1);
    game_step(g); game_scene(g, s);                    /* A + 14 */
    ASSERT_EQ_INT(s->mode, GAME_MODE_MENU);
    for (int i = 0; i < 5; i++) { game_step(g); game_scene(g, s); ASSERT_EQ_INT(s->mode, GAME_MODE_MENU); ASSERT_EQ_INT(s->text_count, 0); }
    game_step(g); game_scene(g, s);                    /* A + 20 */
    ASSERT_TRUE(s->text_count >= 1);
    free(s);
    game_destroy(g);
}

TEST(the_game_over_text_id_is_0x62_for_the_f16) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    for (int c = 1; c <= 4; c++) crash_once(g);
    stage_air(g, SEA_X, 20000, SEA_Z, 2000, 2240, 3000, 60, 0);
    game_debug_aircraft(g)->f.cls = 0x10;              /* V:4A901 = 0x10: the F-16 (the restart writes +0x62 from it) */
    ASSERT_TRUE(step_to_impact(g, 60) >= 0);
    game_debug_aircraft(g)->f.cls = 0x10;
    for (int i = 0; i < 2 + HOLD_STEPS; i++) game_step(g);
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    game_scene(g, s);
    ASSERT_EQ_INT(s->mode, GAME_MODE_GAMEOVER);
    ASSERT_EQ_INT(s->text_id, 0x62);
    free(s);
    game_destroy(g);
}

/* ESC leaves the flight (17.17 rule 9): V:4A84D = 0 at once (nothing is simulated), n is cleared 39 blanks (6 steps) later and the mission screen
 * follows (note 08 5.11: the echo screen of the mode); RETURN does not (it is the weapon key in free flight: the frontend sends SELECT_WEAPON, which is ignored here). */
TEST(esc_ends_the_flight_and_return_is_not_a_reposition) {   /* the reset 6 steps after the key (note 08 5.11) */
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    for (int i = 0; i < 5; i++) game_step(g);
    game_event_t ret = { GAME_EV_BUTTON, GAME_BTN_RETURN, 1 }, sel = { GAME_EV_BUTTON, GAME_BTN_SELECT_WEAPON, 1 };
    game_input(g, &ret); game_input(g, &sel);
    game_step(g);
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    game_scene(g, s);
    ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT);
    ASSERT_EQ_INT(ac->pos[0], DECK_X);
    const uint32_t n = scene_n(g), steps = ac->f.steps;
    game_event_t esc = { GAME_EV_BUTTON, GAME_BTN_ESC, 1 };
    game_input(g, &esc);
    for (int i = 0; i < 5; i++) {   /* the reset is 6 steps from the key: the fade-out 32 blanks = 5, the close and pulse 8 blanks = 1 (note 08 5.11) */
        game_step(g); game_scene(g, s);
        ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT);
        ASSERT_EQ_INT(s->flight_blank, 1);             /* V:4A84D = 0: the world is not redrawn; the last picture stays and fades out with the target 0 */
        ASSERT_EQ_INT(s->fade_target, 0);              /* the picture fades out (V:4A959 = 0) */
        ASSERT_EQ_INT(ac->f.steps, steps);
        ASSERT_EQ_INT(scene_n(g), n);
    }
    game_step(g);
    game_scene(g, s);
    ASSERT_EQ_INT(s->mode, GAME_MODE_MENU);            /* 41 blanks from the key: the reset block runs, the echo screen of the mode follows (note 08 5.11) */
    ASSERT_EQ_INT(scene_n(g), 0);
    ASSERT_EQ_INT(s->palette_regime, 0);
    free(s);
    game_destroy(g);
}

/* ---- the throttle: the lever axis and the mouse reach the pointer throttle (17.4) ----------------------------------------------- */

TEST(throttle_axis_and_mouse_drive_the_pointer) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    ASSERT_EQ_INT(ac->f.v4a830, 72);
    axis(g, GAME_AXIS_THROTTLE, 32767);                /* full lever: P = 960 at the next step, then the throttle rules act on it */
    game_step(g);
    ASSERT_TRUE(ac->f.v4a830 == 960 || ac->f.v4a928 == 120 || ac->f.throttle > 9);
    for (int i = 0; i < 30; i++) game_step(g);
    ASSERT_TRUE(ac->f.throttle > 60);                  /* the throttle follows the pointer */
    axis(g, GAME_AXIS_THROTTLE, -32768);               /* idle lever: P = 0 */
    for (int i = 0; i < 60; i++) game_step(g);
    ASSERT_TRUE(ac->f.throttle <= 9);
    game_destroy(g);
    /* the mouse: pushing it away (negative counter change) raises P, and the throttle follows */
    g = start_game(0);
    ASSERT_TRUE(g != NULL);
    ac = game_debug_aircraft(g);
    axis(g, GAME_AXIS_POINTER_DY, -300);
    axis(g, GAME_AXIS_POINTER_DY, -100);               /* summed: P = 72 + 400 */
    game_step(g);
    ASSERT_TRUE(ac->f.v4a830 >= 400);
    ASSERT_TRUE(ac->f.v4a928 > 0 || ac->f.throttle > 9);
    for (int i = 0; i < 40; i++) game_step(g);
    ASSERT_TRUE(ac->f.throttle > 30);
    game_destroy(g);
}

/* ---- Review Focus 2: nothing continues underground ------------------------------------------------------------------------------ */

TEST(flying_into_the_ground_never_continues_underground) {
    const struct { uint16_t pitch; uint8_t gear; int S; int32_t y; int32_t x, z; } cases[] = {
        { 2000, 0x00, 3000, 20000, SEA_X, SEA_Z },     /* steep dive, gear down, sea */
        { 400,  0x00, 2400, 60000, SEA_X, SEA_Z },     /* shallow dive */
        { 2000, 0x80, 3000, 20000, SEA_X, SEA_Z },     /* gear up */
        { 1000, 0xE0, 4000, 30000, SEA_X, SEA_Z },
        { 2000, 0x00, 3000, 20000, RWY_X + 40000, RWY_Z + 40000 },   /* land */
        { 0,    0x00, 1500, 4000,  SEA_X, SEA_Z },     /* level, too slow: sinks to the floor */
    };
    int crash_cases = 0, restart_cases = 0;
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        game_t *g = start_game(0);
        ASSERT_TRUE(g != NULL);
        aircraft_t *ac = game_debug_aircraft(g);
        stage_air(g, cases[c].x, cases[c].y, cases[c].z, cases[c].pitch, 2240, cases[c].S, 60, cases[c].gear);
        int crashes = 0, restarts = 0, prev = GAME_CRASH_NONE;
        for (int i = 0; i < 300; i++) {
            game_step(g);
            const int st = game_crash_state(g, NULL);
            if (prev != GAME_CRASH_NONE && st == GAME_CRASH_NONE) restarts++;   /* a real restart: a crash state left for flight */
            prev = st;
            /* never below sea level, nor below the lowest floor of the step (750: the aircraft whose gear is up or was lost over water rests on
             * its belly, 1800 with the wheels down; section 7), whether it flies, has crashed or restarted */
            ASSERT_TRUE(ac->pos[1] >= 750);
        }
        crashes = game_crash_count(g);
        if (crashes) crash_cases++;
        if (restarts) restart_cases++;
        ASSERT_TRUE(restarts >= crashes - 1 && restarts <= crashes);   /* every crash but possibly the last (still in its wait) restarted */
        if (cases[c].pitch >= 1000 && cases[c].y <= 30000) ASSERT_TRUE(restarts >= 1);   /* the dives end in a crash and a restart */
        game_destroy(g);
    }
    ASSERT_TRUE(crash_cases >= 4);
    ASSERT_TRUE(restart_cases >= 4);
}

TEST(a_soft_touchdown_rests_on_the_floor_without_a_crash) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    stage_air(g, SEA_X, 4000, SEA_Z, 0, 2240, 2400, 40, 0);
    int touched = 0;
    for (int i = 0; i < 200; i++) {
        game_step(g);
        ASSERT_TRUE(ac->pos[1] >= 750);               /* the runway ends in the sea: the gear is lost over water and the belly floor is 750 */
        ASSERT_EQ_INT(game_crash_count(g), 0);
        if (ac->pos[1] <= 1800 && ac->f.speed > 0) touched = 1;   /* on the floor and still moving: a touchdown happened */
    }
    ASSERT_TRUE(touched);
    ASSERT_TRUE(ac->pos[1] <= 1800);
    game_destroy(g);
}

int main(int argc, char **argv) {
    root = argc > 1 ? argv[1] : "assets";
    char err[128] = "";
    flight_tables_t *t = (flight_tables_t *)malloc(sizeof *t);
    int rc = flight_tables_load(t, &loader, err, sizeof err);
    free(t);
    uint8_t *probe = NULL; size_t pl = 0;
    if (rc != 0 || fl_read(NULL, "models/index.txt", &probe, &pl) != 0) {
        printf("test_crash: skipped (%s)\n", rc != 0 ? err : "no models");
        return 0;
    }
    free(probe);
    printf("test_crash:\n");
    RUN(deck_idle_stays_put_in_the_game);
    RUN(catapult_launch_reaches_flying_speed);
    RUN(landing_in_the_wire_zone_with_the_hook_down_stops_the_aircraft);
    RUN(sea_crash_holds_then_restarts_on_the_deck);
    RUN(terrain_crash_holds_and_restarts_the_same_way);
    RUN(runway_start_restarts_on_the_runway);
    RUN(the_crash_restart_clears_the_f1_counter_and_the_ecm_switch_as_the_player_start_does);
    RUN(restart_writes_and_keeps_the_fields_of_17_17_rule_4);
    RUN(hold_queues_the_keys_and_pass_a_applies_them);
    RUN(collisions_off_clears_the_crash_and_posts_the_notice);
    RUN(external_view_crash_has_no_hold_and_restarts_in_pass_n_plus_16_and_stays_external);
    RUN(dying_then_back_to_the_cockpit_restarts_at_once);
    RUN(cockpit_crash_with_a_switch_to_external_during_the_hold_changes_nothing);
    RUN(pass_a_ground_test_reads_the_crash_slots_in_the_cockpit_view);
    RUN(pass_a_ground_test_reads_the_crash_slots_in_the_external_view);
    RUN(a_paused_restart_leaves_the_record_as_the_start_wrote_it);
    RUN(the_flash_is_set_only_in_the_step_where_the_hold_ends);
    RUN(the_fifth_crash_ends_the_flight_with_the_game_over_text);
    RUN(the_game_over_steps_do_not_depend_on_a_pause_queued_in_the_hold);
    RUN(the_game_over_text_id_is_0x62_for_the_f16);
    RUN(esc_ends_the_flight_and_return_is_not_a_reposition);
    RUN(a_view_key_in_the_hand_over_pass_does_not_cancel_the_hold);
    RUN(a_pause_queued_in_the_hold_lets_the_resume_states_run_without_n);
    RUN(throttle_axis_and_mouse_drive_the_pointer);
    RUN(flying_into_the_ground_never_continues_underground);
    RUN(a_soft_touchdown_rests_on_the_floor_without_a_crash);
    TEST_MAIN_END();
}
