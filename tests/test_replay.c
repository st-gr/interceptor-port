/* tests/test_replay.c - the demo replay (src/core/replay.c, docs/re/08-game-flow.md section 7).
 *
 * The streams of the first groups are INVENTED (known patterns, no shipped bytes): they drive the reader, the event bits, the start sequence and the game's flight. The
 * last group runs the shipped stream on the extracted assets (skipped without assets/) and compares the player record with the recorded run of 7.8.
 * Frames and steps: frame i is applied at core step i (7.8, no conversion); the screens around the flight are counted in steps as screen.h states.
 */
#include "test.h"
#include "../src/core/game.h"
#include "../src/core/screen.h"
#include "../src/core/replay.h"
#include "../src/core/flight.h"
#include "../src/core/logbook.h"
#include "../src/core/entities.h"

/* ---- A: the reader and the event bits ---------------------------------------------------------------------------------------------------------- */

static int none_read(void *user, const char *path, uint8_t **out, size_t *len) { (void)user; (void)path; (void)out; (void)len; return -1; }
static void any_rel(void *user, uint8_t *buf) { (void)user; free(buf); }
static game_loader_t NOFILES = { none_read, any_rel, NULL };

static screen_t *new_screen(void) {
    screen_t *s = (screen_t *)malloc(sizeof *s);
    char err[100] = "";
    if (screen_init(s, &NOFILES, err, sizeof err) != 0) { free(s); return NULL; }
    return s;
}

#define N 64
static uint8_t KEYS[N];
static demo_events_t EV[N];

static void fresh(void) { memset(KEYS, 0, sizeof KEYS); memset(EV, 0, sizeof EV); }

TEST(the_played_frames_end_at_the_first_run_of_three_ff_bytes) {
    uint8_t k[12] = { 1, 2, 0xFF, 0xFF, 3, 4, 0xFF, 0xFF, 0xFF, 0xFF, 5, 6 };
    ASSERT_EQ_INT(replay_played_frames(k, 12), 6);            /* two 0xFF in a row are not the marker */
    uint8_t m[5] = { 1, 2, 3, 4, 5 };
    ASSERT_EQ_INT(replay_played_frames(m, 5), 5);             /* a stream without a marker ends at its length (7.10 rule 1) */
    uint8_t e[3] = { 0xFF, 0xFF, 0xFF };
    ASSERT_EQ_INT(replay_played_frames(e, 3), 0);
    ASSERT_EQ_INT(replay_played_frames(m, 0), 0);
}

TEST(frame_i_is_the_key_byte_i_and_the_events_of_index_i_none_for_frame_0) {
    fresh();
    for (int i = 0; i < 10; i++) KEYS[i] = (uint8_t)(0x10 + i);
    KEYS[10] = KEYS[11] = KEYS[12] = 0xFF;
    EV[0].word1 = 0x0100;            /* the words of index 0 are never executed (7.4 rule 1) */
    EV[3].word1 = 0x0100;            /* G */
    screen_t *s = new_screen(); replay_t r = {0};
    ASSERT_EQ_INT(replay_set_stream(&r, s, KEYS, EV, 13), 0);
    ASSERT_EQ_INT(r.played, 10);
    replay_frame_t f;
    ASSERT_EQ_INT(replay_step(&r, &f), 0);                  /* not started: no frame */
    ASSERT_EQ_INT(f.valid, 0);
    replay_start(&r);
    for (int i = 0; i < 10; i++) {
        int more = replay_step(&r, &f);
        ASSERT_EQ_INT(f.valid, 1); ASSERT_EQ_INT(f.index, i); ASSERT_EQ_INT(f.key, 0x10 + i);
        ASSERT_EQ_INT(more, i < 9);                            /* 0 on the last played frame */
        ASSERT_EQ_INT(f.n_events, i == 3 ? 1 : 0);
        if (i == 3) { ASSERT_EQ_INT(f.ev[0].code, GAME_BTN_GEAR); ASSERT_EQ_INT(f.ev[0].value, 1); ASSERT_EQ_INT(f.ev[0].kind, GAME_EV_BUTTON); }
    }
    ASSERT_EQ_INT(replay_step(&r, &f), 0);                  /* over: no further frame, the marker is never played */
    ASSERT_EQ_INT(f.valid, 0);
    replay_destroy(&r); screen_destroy(s); free(s);
}

TEST(the_start_rewinds_to_frame_0_and_stop_ends_the_reader) {
    fresh();
    for (int i = 0; i < 6; i++) KEYS[i] = (uint8_t)(i + 1);
    screen_t *s = new_screen(); replay_t r = {0}; replay_frame_t f;
    replay_set_stream(&r, s, KEYS, EV, 6);
    replay_start(&r);
    for (int i = 0; i < 3; i++) replay_step(&r, &f);
    replay_start(&r);                                          /* the ESC restart: the pointers read the buffer starts again (7.7) */
    replay_step(&r, &f);
    ASSERT_EQ_INT(f.index, 0); ASSERT_EQ_INT(f.key, 1);
    replay_stop(&r);
    ASSERT_EQ_INT(replay_step(&r, &f), 0); ASSERT_EQ_INT(f.valid, 0);
    replay_destroy(&r); screen_destroy(s); free(s);
}

/* the executor's order (7.3): word 1 bits 8 to 15, word 1 bits 0 to 3, word 2 bits 8 to 15, word 2 bits 0 to 7 */
TEST(every_bit_set_runs_in_the_executors_order_through_the_key_events) {
    fresh();
    EV[1].word1 = 0xFFFF; EV[1].word2 = 0xFFFF;
    screen_t *s = new_screen(); replay_t r = {0}; replay_frame_t f;
    replay_set_stream(&r, s, KEYS, EV, 4);
    replay_start(&r); replay_step(&r, &f);                  /* frame 0 */
    replay_step(&r, &f);                                    /* frame 1 */
    static const int code[] = {
        GAME_BTN_GEAR, GAME_BTN_HOOK, GAME_BTN_FIRE, GAME_BTN_FIRE, GAME_BTN_SELECT_WEAPON,         /* word 1 bits 8 to 12 (13 eject: nothing) */
        GAME_BTN_RADAR_RANGE, GAME_BTN_TARGET,                                                       /* bits 14 (R) and 15 (T) */
        GAME_BTN_BRAKE, GAME_BTN_FLARE, GAME_BTN_CHAFF, GAME_BTN_ECM,                                /* word 1 bits 0 to 3 */
        GAME_BTN_TOWER, GAME_BTN_KEYPAD, GAME_BTN_FIXED_CAMERA, GAME_BTN_KEYPAD, GAME_BTN_KEYPAD, GAME_BTN_KEYPAD, GAME_BTN_KEYPAD, GAME_BTN_KEYPAD,   /* word 2 bits 8, 9, 10 (Shift and KP-ENTER), 11 to 15 */
        GAME_BTN_KEYPAD, GAME_BTN_KEYPAD, GAME_BTN_KEYPAD, GAME_BTN_KEYPAD, GAME_BTN_KEYPAD, GAME_BTN_KEYPAD,  /* word 2 bits 0 to 5 */
        GAME_BTN_KEYPAD_UP, GAME_BTN_LBRACKET };                                                     /* bits 6 and 7 */
    static const int val[] = { 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, GAME_KP_ENTER, 1, 5, 8, 9, 6, 3, 2, 1, 4, 7, 5, GAME_KP_DOT, 2, 1 };
    ASSERT_EQ_INT(f.n_events, (int)(sizeof code / sizeof *code));
    for (int i = 0; i < f.n_events; i++) {
        ASSERT_EQ_INT(f.ev[i].code, code[i]);
        ASSERT_EQ_INT(f.ev[i].value, val[i]);
    }
    ASSERT_EQ_INT(f.gap1, 0);                                  /* R, T, ECM and the fixed camera are key events now (milestone 5) */
    ASSERT_EQ_INT(f.gap2, 0);
    replay_destroy(&r); screen_destroy(s); free(s);
}

TEST(the_bit_that_stands_for_kp5_or_the_bracket_follows_the_live_view) {
    fresh();
    EV[1].word2 = 0x0800;
    screen_t *s = new_screen(); replay_t r = {0}; replay_frame_t f;
    replay_set_stream(&r, s, KEYS, EV, 4);
    replay_start(&r); replay_step(&r, &f); replay_step(&r, &f);
    ASSERT_EQ_INT(f.n_events, 1); ASSERT_EQ_INT(f.view_dep[0], 1);
    game_event_t e = replay_event_in_view(&f, 0, 0);
    ASSERT_EQ_INT(e.code, GAME_BTN_KEYPAD); ASSERT_EQ_INT(e.value, 5);   /* the cockpit: the head-forward request */
    e = replay_event_in_view(&f, 0, 1);
    ASSERT_EQ_INT(e.code, GAME_BTN_RBRACKET); ASSERT_EQ_INT(e.value, 1); /* an outside view: one distance level closer; asked at the event's turn, so a KP-ENTER of the same frame is seen */
    replay_destroy(&r); screen_destroy(s); free(s);
}

TEST(a_set_bit_of_the_mask_00f0_in_word_1_is_not_an_event) {
    fresh();
    EV[1].word1 = 0x00F0;
    screen_t *s = new_screen(); replay_t r = {0}; replay_frame_t f;
    replay_set_stream(&r, s, KEYS, EV, 4);
    replay_start(&r); replay_step(&r, &f); replay_step(&r, &f);
    ASSERT_EQ_INT(f.n_events, 0); ASSERT_EQ_INT(f.gap1, 0);
    replay_destroy(&r); screen_destroy(s); free(s);
}

static int prev_calls, prev_mode;
static int prev_handler(void *user, uint8_t mode) { (void)user; prev_calls++; prev_mode = mode; return mode == 2; }

TEST(the_handler_takes_0x7f_and_chains_the_other_modes) {
    fresh();
    screen_t *s = new_screen(); replay_t r = {0};
    screen_set_mode_handler(s, prev_handler, NULL);
    replay_set_stream(&r, s, KEYS, EV, 4);
    ASSERT_EQ_INT(s->demo_available, 1);
    prev_calls = 0;
    ASSERT_EQ_INT(replay_mode_handler(&r, 2), 1);              /* training: the earlier handler's */
    ASSERT_EQ_INT(prev_calls, 1); ASSERT_EQ_INT(prev_mode, 2);
    ASSERT_EQ_INT(replay_mode_handler(&r, 9), 0);
    ASSERT_EQ_INT(replay_mode_handler(&r, 0x7F), 1);           /* the demo: the wait state */
    ASSERT_EQ_INT(prev_calls, 2);
    ASSERT_EQ_INT(s->state, SCR_DEMO_WAIT);
    replay_destroy(&r); screen_destroy(s); free(s);
}

TEST(without_a_stream_menu_key_1_is_ignored) {
    screen_t *s = new_screen(); replay_t r = {0};
    ASSERT_EQ_INT(replay_init(&r, s, &NOFILES, NULL, 0), 0);
    ASSERT_EQ_INT(r.demo == NULL, 1);
    ASSERT_EQ_INT(s->demo_available, 0);
    screen_destroy(s); free(s);
}

/* ---- B: the start sequence on the screen machine (7.5, 7.7) ------------------------------------------------------------------------------------- */

static void key(screen_t *s, int c) { game_event_t ev = { GAME_EV_KEY, 0, c }; screen_key(s, &ev); }
static void btn(screen_t *s, int code) { game_event_t ev = { GAME_EV_BUTTON, code, 1 }; screen_key(s, &ev); }
static void steps(screen_t *s, int n) { for (int i = 0; i < n; i++) screen_step(s); }

/* The screens to the menu, then key 1; returns with the screen in the menu state before the key. */
static void to_menu(screen_t *s) { key(s, 'x'); steps(s, 3); btn(s, GAME_BTN_RETURN); steps(s, 8); }

/* Steps until the state `st` is the state after a step, at most `max`; returns the steps taken or -1. */
static int until(screen_t *s, screen_state_t st, int max) { for (int i = 1; i <= max; i++) { screen_step(s); if (s->state == st) return i; } return -1; }
/* The number of steps the machine stays in `st` (counting the step that entered it, as the state after each step), from now. */
static int stay(screen_t *s, screen_state_t st, int max) { int n = 0; for (int i = 0; i < max && s->state == st; i++) { screen_step(s); n++; if (s->state != st) break; } return n; }

TEST(the_start_sequence_counts_the_waits_in_steps_as_the_note_converts_them) {
    fresh();
    screen_t *s = new_screen(); replay_t r = {0};
    replay_set_stream(&r, s, KEYS, EV, 4);
    to_menu(s);
    ASSERT_EQ_INT(s->state, SCR_MENU);
    key(s, '1');
    ASSERT_TRUE(until(s, SCR_DEMO_WAIT, 20) > 0);               /* the echo line, then the dispatch (7.5: 4 blanks + the echo passes) */
    ASSERT_EQ_INT(s->mode_byte, 0x7F);                         /* 0x7F from the menu key to the set-up */
    ASSERT_EQ_INT(s->demo, 1); ASSERT_EQ_INT(s->direct_restart, 0);
    int w = stay(s, SCR_DEMO_WAIT, 20);
    ASSERT_EQ_INT(w, 6);                                       /* 0x11B54: 5 passes and the transition, one step each (world-drawing passes) */
    ASSERT_EQ_INT(s->state, SCR_APPROACH);                     /* the set-up has run: the flight is placed (out_place), frozen */
    ASSERT_EQ_INT(s->mode_byte, 3);                            /* the flight runs in mode 3 (5.10) */
    ASSERT_EQ_INT(s->location, 3); ASSERT_EQ_INT(s->aircraft, 0x11);
    ASSERT_EQ_INT(s->allowance, 3);                            /* 6.3 */
    ASSERT_EQ_INT(s->out_place, 1);
    ASSERT_EQ_INT(s->direct_restart, 1);
    ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT);
    int a = stay(s, SCR_APPROACH, 40);
    ASSERT_EQ_INT(a, 17);                                      /* fade-in 7, 0x11BD0 3, 0x12D58 1, 0x12DB8 6 */
    ASSERT_EQ_INT(s->state, SCR_NOTICE);
    ASSERT_TRUE(until(s, SCR_WAIT, 80) > 0);                   /* the notice typed and held (6.2) */
    ASSERT_EQ_INT(stay(s, SCR_WAIT, 10), 4);                   /* four waiting passes, each a simulated step: they read stream indices 0 to 3 */
    ASSERT_EQ_INT(s->state, SCR_FLIGHT);
    replay_destroy(&r); screen_destroy(s); free(s);
}

TEST(a_kp_enter_does_not_skip_anything_in_the_demo) {
    fresh();
    screen_t *s = new_screen(); replay_t r = {0};
    replay_set_stream(&r, s, KEYS, EV, 4);
    to_menu(s); key(s, '1');
    until(s, SCR_APPROACH, 40);
    steps(s, 2);
    game_event_t ev = { GAME_EV_BUTTON, GAME_BTN_KEYPAD, GAME_KP_ENTER }; screen_key(s, &ev);
    ASSERT_EQ_INT(stay(s, SCR_APPROACH, 40), 15);              /* 17 steps in all, not the 70 blanks of the free flight's skip */
    replay_destroy(&r); screen_destroy(s); free(s);
}

TEST(esc_in_the_echo_and_in_the_wait_restarts_the_whole_selection) {
    fresh();
    screen_t *s = new_screen(); replay_t r = {0};
    replay_set_stream(&r, s, KEYS, EV, 4);
    to_menu(s); key(s, '1');
    until(s, SCR_DEMO_WAIT, 20);
    steps(s, 2);
    btn(s, GAME_BTN_ESC);
    ASSERT_TRUE(until(s, SCR_ESC_FADE, 3) > 0);                /* acted on in the wait state (7.7: before the set-up) */
    ASSERT_TRUE(until(s, SCR_ECHO, 40) > 0);                   /* the destination is the echo line: the selection starts again */
    ASSERT_EQ_INT(s->mode_byte, 0x7F);
    ASSERT_TRUE(until(s, SCR_DEMO_WAIT, 20) > 0);
    ASSERT_EQ_INT(stay(s, SCR_DEMO_WAIT, 20), 6);
    replay_destroy(&r); screen_destroy(s); free(s);
}

TEST(esc_after_the_set_up_restarts_the_demo_directly_without_the_echo) {
    fresh();
    screen_t *s = new_screen(); replay_t r = {0};
    replay_set_stream(&r, s, KEYS, EV, 4);
    to_menu(s); key(s, '1');
    until(s, SCR_APPROACH, 40);
    steps(s, 3);
    s->out_place = 0;
    btn(s, GAME_BTN_ESC);
    ASSERT_TRUE(until(s, SCR_ESC_FADE, 3) > 0);
    int saw_echo = 0, n = 0;
    while (s->state == SCR_ESC_FADE && n < 40) { screen_step(s); n++; }
    for (int i = 0; i < 3 && s->state != SCR_DEMO_WAIT; i++) { saw_echo |= s->state == SCR_ECHO; screen_step(s); }
    ASSERT_EQ_INT(s->state, SCR_DEMO_WAIT);                    /* straight to the wait state (7.7: mode 0x7F and 0x11B54 7 blanks after the reset) */
    ASSERT_EQ_INT(saw_echo, 0);
    ASSERT_EQ_INT(s->out_leave, 1);                            /* the reset block runs on the way */
    ASSERT_EQ_INT(s->mode_byte, 0x7F);
    ASSERT_EQ_INT(stay(s, SCR_DEMO_WAIT, 20), 3);              /* 7.7: three passes from the reset pass of 0x11AE2 to the set-up pass (16 blanks), one step each */
    ASSERT_EQ_INT(s->state, SCR_APPROACH);
    ASSERT_EQ_INT(s->mode_byte, 3);
    replay_destroy(&r); screen_destroy(s); free(s);
}

TEST(shift_esc_returns_to_the_menu_from_the_demo_and_clears_it) {
    fresh();
    screen_t *s = new_screen(); replay_t r = {0};
    replay_set_stream(&r, s, KEYS, EV, 4);
    to_menu(s); key(s, '1');
    until(s, SCR_APPROACH, 40);
    steps(s, 5);
    btn(s, GAME_BTN_SHIFT_ESC);
    ASSERT_TRUE(until(s, SCR_MENU_BUILD, 3) > 0);
    ASSERT_EQ_INT(s->demo, 0); ASSERT_EQ_INT(s->direct_restart, 0); ASSERT_EQ_INT(s->mode_byte, 0);
    replay_destroy(&r); screen_destroy(s); free(s);
}

/* ---- C: the flight through the game, on the extracted assets with an invented stream ---------------------------------------------------------- */

static const char *root = "assets";
static uint8_t SYN[4 + DEMO_MAX_FRAMES * 5];
static size_t SYN_LEN;
static int use_syn;
static int fread_file(const char *path, uint8_t **out, size_t *len) {
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
static int real_read(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    if (use_syn && !strcmp(path, "tables/demo.bin")) { *out = (uint8_t *)malloc(SYN_LEN + 1); memcpy(*out, SYN, SYN_LEN); *len = SYN_LEN; return 0; }
    return fread_file(path, out, len);
}
static game_loader_t RLD = { real_read, any_rel, NULL };
static int have_real(void) {
    uint8_t *b; size_t n;
    if (fread_file("tables/flight_stick_player.bin", &b, &n) != 0) return 0;
    free(b);
    if (fread_file("tables/demo.bin", &b, &n) != 0) return 0;
    free(b);
    return fread_file("tables/text.bin", &b, &n) == 0 ? (free(b), 1) : 0;
}

/* The invented demo file: 2060 frames, the marker at `marker` (the key bytes 0xFF, 3 of them). */
static void build_syn(int marker) {
    const int n = 2060;
    memset(SYN, 0, sizeof SYN);
    SYN[0] = n & 255; SYN[1] = n >> 8;
    uint8_t *k = SYN + 4;
    demo_events_t *ev = NULL; (void)ev;
    uint8_t *e = SYN + 4 + n;
    static const uint8_t pat[8] = { 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x14, 0x28 };   /* roll, stick, rudder bits only: the throttle bits get the detent rules */
    for (int i = 0; i < n; i++) k[i] = (uint8_t)(i >= marker && i < marker + 4 ? 0xFF : pat[i % 8]);
    SYN_LEN = 4 + (size_t)n * 5;
    (void)e;
}
static void set_ev(int i, unsigned w1, unsigned w2) {
    uint8_t *e = SYN + 4 + 2060 + 4 * i;
    e[0] = w1 & 255; e[1] = w1 >> 8; e[2] = w2 & 255; e[3] = w2 >> 8;
}

static void gkey(game_t *g, int c) { game_event_t ev = { GAME_EV_KEY, 0, c }; game_input(g, &ev); }
static void gbtn(game_t *g, int code, int v) { game_event_t ev = { GAME_EV_BUTTON, code, v }; game_input(g, &ev); }
static game_scene_t GS;
static const game_scene_t *gscene(game_t *g) { game_scene(g, &GS); return &GS; }

static game_t *start_to_menu(void) {
    char err[200] = "";
    game_t *g = game_create(&RLD, err, sizeof err);
    if (!g) { printf("  (game_create: %s)\n", err); return NULL; }
    game_random_seed(g, 9);   /* seed 9 draws the selector's k = 0 and the offsets (-2, -1): the original's most common demo start (variant 0, (-2, +-1); 8 of 13 captured starts at exactly 1,172, 07 2.4, review N2); seed 1 would draw variant 1 (the measured 1,170 case, 1 of 13) */
    gkey(g, 'x'); for (int i = 0; i < 14; i++) game_step(g);   /* the log-disk check passes (result 0, the default): any key on the credits goes to the menu, whose list starts 7 steps later (5.3) */
    return g;
}
/* menu key 1, then steps until the demo has consumed `frames` frames (the step that applied frame frames - 1 has run); returns the steps taken, -1 on a timeout */
static int run_to(game_t *g, int frames, int max) {
    for (int i = 1; i <= max; i++) { game_step(g); if (game_demo_step(g) >= frames) return i; }
    return -1;
}
static game_t *start_demo(void) {
    game_t *g = start_to_menu();
    if (!g) return NULL;
    gkey(g, '1');
    return g;
}

TEST(the_mode_sequence_of_the_demo_is_menu_then_the_flight_in_mode_3) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 1; build_syn(2046);
    game_t *g = start_to_menu(); ASSERT_TRUE(g != NULL);
    ASSERT_EQ_INT(gscene(g)->mode, GAME_MODE_MENU);
    gkey(g, '1');
    int prev = GAME_MODE_MENU, changes = 0, flight_at = -1;
    for (int i = 0; i < 100 && game_demo_step(g) < 1; i++) {
        game_step(g);
        int m = gscene(g)->mode;
        if (m != prev) { changes++; if (m == GAME_MODE_FLIGHT) flight_at = i + 1; prev = m; }
    }
    ASSERT_EQ_INT(changes, 1);                                 /* the menu screens are one mode (echo and wait), then the flight */
    ASSERT_EQ_INT(prev, GAME_MODE_FLIGHT);
    ASSERT_TRUE(flight_at > 6 && flight_at < 20);
    aircraft_t *ac = game_debug_aircraft(g);
    ASSERT_EQ_INT(ac->f.v4a95e, 3);                            /* the mode byte of the flight (5.10) */
    ASSERT_EQ_INT(game_crash_allowance(g), 3);                 /* 6.3 */
    game_destroy(g);
}

TEST(the_first_stream_frame_is_applied_44_steps_after_the_menu_key_on_the_real_text) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 0;                           /* the shipped stream and text: the notice's length is the real one; tests/frontend/test_demo.py counts from this number */
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    int n = -1;
    for (int i = 1; i <= 120 && n < 0; i++) { game_step(g); if (game_demo_step(g) >= 1) n = i; }
    ASSERT_EQ_INT(n, 44);                  /* 12.14 N5: the notice of the two lines is 348 passes = 13 steps typed + 1 hold = 14 (the earlier 7 + 1 undercounted): 7 + 6 + 17 + 14 = 44. Earlier reading: the key's own step is 1: the echo to the dispatch 7 steps (5.4: 43 to 45 blanks), the wait 0x11B54 6 (7.5: 5 passes and the transition), the fade-in, 0x11BD0, 0x12D58 and 0x12DB8 17 (7 + 3 + 1 + 6), the notice of the two lines 0x58 and 0x5A 7 + its hold 1 (7.5: about 290 passes = 7.7 steps = 8): 7 + 6 + 17 + 8 = 38; the first frame is read in the step that ends the notice (the simulation switch of 0x12E4E, 6.2)  */
    game_destroy(g);
}

TEST(the_demo_carrier_and_the_player_start_follow_the_heading_28600) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 1; build_syn(2046);
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(run_to(g, 1, 200) > 0);
    aircraft_t *ac = game_debug_aircraft(g);
    ASSERT_EQ_INT(ac->f.heading, 28600);                       /* the attitude copied from the carrier record (7.5) */
    ASSERT_EQ_INT(ac->f.pitch, 0); ASSERT_EQ_INT(ac->f.roll, 0);
    ASSERT_EQ_INT(ac->pos[0], 286785536 + 15900);              /* the carrier plus ((11 M0 + 104 M2) >> 4, (11 M6 + 104 M8) >> 4) */
    ASSERT_EQ_INT(ac->pos[2], 286785536 + 105900);
    ASSERT_EQ_INT(ac->pos[1], 30472);
    game_destroy(g);
}

TEST(the_key_byte_of_frame_i_drives_step_i_and_the_live_controls_are_ignored) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 1; build_syn(2046);
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    ASSERT_TRUE(run_to(g, 1, 200) > 0);
    static const uint8_t pat[8] = { 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x14, 0x28 };
    for (int i = 0; i < 24; i++) {
        ASSERT_EQ_INT(game_demo_step(g), i + 1);
        ASSERT_EQ_INT(ac->f.keys, pat[i % 8]);                 /* the byte of the frame just applied */
        game_event_t ax = { GAME_EV_AXIS, GAME_AXIS_STICK_X, 32000 }; game_input(g, &ax);
        gbtn(g, GAME_BTN_THROTTLE_UP, 1); gbtn(g, GAME_BTN_BRAKE, 1); gbtn(g, GAME_BTN_GEAR, 1); gbtn(g, GAME_BTN_FKEY, 3);
        gkey(g, '2');
        game_step(g);
    }
    ASSERT_EQ_INT(ac->f.v4a903, 3);                            /* the replay state */
    ASSERT_EQ_INT(ac->f.w0 & 0x0800, 0);                       /* the live brake key (a toggle) did nothing: the brake bit is still clear */
    game_destroy(g);
}

TEST(the_events_of_index_i_run_before_step_i_in_the_note_order_and_index_0_never) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 1; build_syn(2046);
    set_ev(0, 0, 0x0200);                  /* index 0: never executed */
    set_ev(6, 0, 0x0300);                  /* index 6: M (bit 8) and KP-ENTER (bit 9): M first, then the toggle: the chase view, not the map (7.3 order) */
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(run_to(g, 1, 200) > 0);
    ASSERT_EQ_INT(gscene(g)->camera.view, GAME_VIEW_COCKPIT);   /* frame 0 ran with the cockpit view: the words of index 0 were not executed */
    for (int i = 1; i < 6; i++) { game_step(g); ASSERT_EQ_INT(game_demo_step(g), i + 1); ASSERT_EQ_INT(gscene(g)->camera.view, GAME_VIEW_COCKPIT); }
    game_step(g);                          /* step 6 applies the events of index 6 first */
    ASSERT_EQ_INT(game_demo_step(g), 7);
    ASSERT_EQ_INT(gscene(g)->camera.view, GAME_VIEW_CHASE);
    game_destroy(g);
}

TEST(the_gear_and_brake_events_reach_the_flight_through_the_ordinary_handlers) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 1; build_syn(2046);
    set_ev(10, 0x0100, 0);                 /* G */
    set_ev(20, 0x0001, 0);                 /* the brake */
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    ASSERT_TRUE(run_to(g, 10, 300) > 0);   /* frame 9 applied */
    int gear0 = ac->f.gear;
    ac->f.w2 = (uint16_t)(ac->f.w2 & ~0x0080);   /* the gear key acts in the air only (flight.c): lift the wheel contact flag for the test */
    game_step(g);                          /* frame 10: the gear toggle */
    ASSERT_EQ_INT((ac->f.gear ^ gear0) & 0x80, 0x80);
    ASSERT_TRUE(run_to(g, 20, 40) > 0);
    int w1 = ac->f.w0;
    game_step(g);                          /* frame 20: the brake toggle (W0 bit 0x0800) */
    ASSERT_TRUE(((ac->f.w0 ^ w1) & 0x0800) != 0);
    game_destroy(g);
}

TEST(p_pauses_the_demo_and_the_stream_stands_still) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 1; build_syn(2046);
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(run_to(g, 5, 300) > 0);
    gbtn(g, GAME_BTN_PAUSE, 1);
    for (int i = 0; i < 6; i++) { game_step(g); ASSERT_EQ_INT(game_demo_step(g), 5); }
    ASSERT_EQ_INT(gscene(g)->mode, GAME_MODE_PAUSED);
    gbtn(g, GAME_BTN_PAUSE, 1);
    game_step(g);
    ASSERT_EQ_INT(game_demo_step(g), 6);
    game_destroy(g);
}

TEST(the_end_of_the_stream_runs_the_esc_flow_and_the_demo_restarts_at_frame_0) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 1; build_syn(30);            /* 30 frames, indices 0 to 29 */
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(run_to(g, 29, 300) > 0);
    game_step(g);                          /* the step that reads index 29 completes normally (7.4 rule 5) */
    ASSERT_EQ_INT(game_demo_step(g), 30);
    ASSERT_EQ_INT(gscene(g)->mode, GAME_MODE_FLIGHT);
    int left = -1;
    for (int i = 1; i <= 30 && left < 0; i++) { game_step(g); if (game_demo_step(g) < 0) left = i; }
    ASSERT_EQ_INT(left, 6);                /* 5.11 stages from the request: the fade-out from level 15, 32 blanks = 5 steps, the close and gate pulse 8 blanks = 1: the reset in step 6 */
    ASSERT_EQ_INT(gscene(g)->mode, GAME_MODE_MENU);
    int back = -1;
    for (int i = 1; i <= 80 && back < 0; i++) { game_step(g); if (game_demo_step(g) >= 0) back = i; }
    ASSERT_TRUE(back > 0);                 /* directly: no echo (7.7), the wait and the set-up */
    ASSERT_EQ_INT(game_demo_step(g), 0);
    ASSERT_TRUE(run_to(g, 1, 100) > 0);
    ASSERT_EQ_INT(game_debug_aircraft(g)->f.keys, 0x04);       /* the key byte of frame 0 again */
    ASSERT_EQ_INT(game_debug_aircraft(g)->f.heading, 28600);
    game_destroy(g);
}

TEST(esc_in_the_demo_restarts_it_at_frame_0_and_the_stream_is_not_continued) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 1; build_syn(2046);
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(run_to(g, 12, 300) > 0);
    gbtn(g, GAME_BTN_ESC, 1);
    int left = -1;
    for (int i = 1; i <= 30 && left < 0; i++) { game_step(g); if (game_demo_step(g) < 0) left = i; }
    ASSERT_EQ_INT(left, 6);                                    /* 5.11 (as above): 5 + 1 steps to the reset */
    ASSERT_TRUE(run_to(g, 1, 300) > 0);
    ASSERT_EQ_INT(game_demo_step(g), 1);                       /* frame 0 again */
    game_destroy(g);
}

TEST(esc_before_the_first_step_restarts_at_frame_0) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 1; build_syn(2046);
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 100 && game_demo_step(g) < 0; i++) game_step(g);
    ASSERT_EQ_INT(game_demo_step(g), 0);                       /* the flight is placed, no step has read the stream (the approach and the notice) */
    gbtn(g, GAME_BTN_ESC, 1);
    for (int i = 0; i < 30 && game_demo_step(g) >= 0; i++) game_step(g);
    ASSERT_EQ_INT(game_demo_step(g), -1);
    ASSERT_TRUE(run_to(g, 1, 300) > 0);
    ASSERT_EQ_INT(game_demo_step(g), 1);
    game_destroy(g);
}

TEST(shift_esc_leaves_the_demo_for_the_menu_and_a_second_start_works) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 1; build_syn(2046);
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(run_to(g, 8, 300) > 0);
    gbtn(g, GAME_BTN_SHIFT_ESC, 1);
    for (int i = 0; i < 60; i++) game_step(g);
    ASSERT_EQ_INT(game_demo_step(g), -1);
    ASSERT_EQ_INT(gscene(g)->mode, GAME_MODE_MENU);
    ASSERT_EQ_INT(game_debug_aircraft(g)->f.v4a903, 0);
    gkey(g, '1');
    ASSERT_TRUE(run_to(g, 3, 300) > 0);
    ASSERT_EQ_INT(game_demo_step(g), 3);
    game_destroy(g);
}

/* ---- the demo and the log (7.6, 10.7), the ESC level in the fade-in (7.7) ------------------------------------------------------------------------ */

static unsigned log_word(game_t *g, int off) { uint8_t b[GAME_LOG_FILE_SIZE]; game_log_export(g, b, sizeof b); return ((unsigned)b[5 + off] << 8) | b[5 + off + 1]; }

TEST(the_demo_books_no_flight_time_and_has_no_crash_to_count) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 1; build_syn(2046);
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(run_to(g, 3, 300) > 0);
    aircraft_t *ac = game_debug_aircraft(g);
    ac->pos[1] += 40000;                                       /* above the deck with the wheels flagged down: the lift-off event */
    for (int i = 0; i < 8 * 12; i++) { ac->pos[1] = 5000000; game_step(g); }   /* 12 s in the air */
    ASSERT_EQ_INT(ac->f.v4a95e, 3);                            /* the demo flies with the mode byte 3: the mode byte alone would book (game.c books for every mode above 0) */
    gbtn(g, GAME_BTN_SHIFT_ESC, 1);
    for (int i = 0; i < 60; i++) game_step(g);
    ASSERT_EQ_INT(gscene(g)->mode, GAME_MODE_MENU);
    ASSERT_EQ_INT(log_word(g, LB_SECONDS), 0); ASSERT_EQ_INT(log_word(g, LB_SECONDS + 2), 0);   /* 7.6: the mission-seconds counter does not add the demo's flight */
    ASSERT_EQ_INT(log_word(g, LB_CRASHES), 0); ASSERT_EQ_INT(log_word(g, LB_HANDOVERS), 0);
    game_destroy(g);
}

TEST(a_crash_in_the_demo_is_counted_in_the_log_the_counters_are_not_gated_by_v4a848_7_6) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 1; build_syn(2046);
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(run_to(g, 3, 300) > 0);
    aircraft_t *ac = game_debug_aircraft(g);
    ac->pos[1] = 30472 + 60000; ac->f.vel[1] = -40000; ac->f.w2 &= (uint16_t)~0x0080u;   /* a hard touchdown (as test_logbook's crash test) */
    for (int i = 0; i < 40; i++) game_step(g);
    ASSERT_EQ_INT(game_crash_count(g), 1);                     /* the flight did crash and the hand-over ran */
    ASSERT_EQ_INT(log_word(g, LB_CRASHES), 1);                 /* 7.6, 10.7: the crash writers (+0x46 at 0x16FB0 and 0x2A8C6, +0x10 at 0x12F44) carry no demo test ... */
    ASSERT_EQ_INT(log_word(g, LB_HANDOVERS), 1);
    ASSERT_EQ_INT(log_word(g, LB_SECONDS), 0);                 /* ... only the mission clock does */
    ASSERT_EQ_INT(gscene(g)->log_dirty, 0);                    /* the counters stay in the buffer: no save point of 10.8 is passed */
    game_destroy(g);
}

TEST(an_esc_in_the_fade_in_after_the_set_up_fades_out_from_the_level_reached_and_later_from_15) {   /* 7.7: measured from level 7 for an ESC at blank 6 */
    screen_t *s = new_screen(); replay_t r = {0}; uint8_t k[8] = { 0 };
    ASSERT_EQ_INT(replay_set_stream(&r, s, k, NULL, 8), 0);
    game_event_t esc = { GAME_EV_BUTTON, GAME_BTN_ESC, 1 };
    int levels[12];
    for (int at = 0; at < 10; at++) {
        screen_destroy(s); free(s); s = new_screen();
        replay_set_stream(&r, s, k, NULL, 8);
        screen_demo_begin(s, 6);
        while (!(s->state == SCR_APPROACH && s->direct_restart)) screen_step(s);
        for (int i = 0; i < at; i++) screen_step(s);
        screen_key(s, &esc); screen_step(s);
        ASSERT_EQ_INT(s->state, SCR_ESC_FADE);
        levels[at] = s->esc_l0;
    }
    ASSERT_TRUE(levels[1] >= 5 && levels[1] <= 8);             /* an ESC one step (6 blanks) after the set-up: 7 measured, 6 estimated */
    for (int i = 1; i < 10; i++) ASSERT_TRUE(levels[i] >= levels[i - 1]);   /* the fade-in only rises */
    ASSERT_EQ_INT(levels[8], 15); ASSERT_EQ_INT(levels[9], 15);   /* after the 7 steps of the fade-in */
    ASSERT_EQ_INT(s->v4a848, 1);                               /* the set-up stored V:4A848 = 1 (7.5) ... */
    screen_start_flight(s);
    ASSERT_EQ_INT(s->v4a848, 0);                               /* ... and the next mission start clears it */
    replay_destroy(&r); screen_destroy(s); free(s);
}

TEST(free_flight_after_a_demo_has_its_own_carrier) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 1; build_syn(2046);
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(run_to(g, 2, 300) > 0);
    gbtn(g, GAME_BTN_SHIFT_ESC, 1);
    for (int i = 0; i < 60; i++) game_step(g);
    gkey(g, '2'); for (int i = 0; i < 30; i++) game_step(g);
    gkey(g, '4');                                              /* the carrier deck */
    for (int i = 0; i < 400 && gscene(g)->mode != GAME_MODE_FLIGHT; i++) game_step(g);
    ASSERT_EQ_INT(gscene(g)->mode, GAME_MODE_FLIGHT);
    ASSERT_EQ_INT(game_demo_step(g), -1);
    aircraft_t *ac = game_debug_aircraft(g);
    ASSERT_EQ_INT(ac->f.heading, 0);                           /* free flight: heading 0 at (295174144, 274202624) + (11264, 106496) */
    ASSERT_EQ_INT(ac->pos[0], 295174144 + 11264);
    ASSERT_EQ_INT(ac->pos[2], 274202624 + 106496);
    game_destroy(g);
}

/* ---- D: the recorded run of 7.8 (the shipped stream, the extracted assets) -------------------------------------------------------------------- */

/* The 21 checkpoint rows of 7.8: the player record's position words +0x14, +0x18, +0x1C and attitude words +0x66, +0x68, +0x6A (the engine's units; facts of the original's
 * run, compared exactly). `n` is the stream step of the row (the key pointer index): the row equals the record after n frames have been applied (n steps run); the
 * landmark constant is the note's z = 48 (FLIGHT_LANDMARK_Z, set by game.c). */
typedef struct { int n; long x, y, z; int pitch, heading, roll; unsigned w0, w2; } cp_t;
static const cp_t CP[21] = {
    { 100, 286803488, 30472, 286938520, 0, 28600, 0, 0x11C8, 0x2082 }, { 200, 286728088, 303812, 288386112, 27768, 416, 104, 0x11C8, 0x2008 },
    { 300, 288694664, 565896, 290717428, 232, 21616, 144, 0x11C8, 0x2008 }, { 400, 291843216, 623864, 293039628, 28712, 27232, 28728, 0x11C8, 0x2008 },
    { 500, 291699332, 816360, 297044292, 28336, 4632, 26880, 0x11C8, 0x2008 }, { 600, 288671752, 1020056, 295390912, 536, 13968, 28064, 0x11C8, 0x0000 },
    { 700, 289393932, 882160, 292559192, 344, 16120, 28672, 0x11C8, 0x2000 }, { 800, 288123012, 832700, 291302452, 88, 6536, 1440, 0x11C8, 0x2008 },
    { 900, 284196556, 709908, 292948196, 16, 5496, 88, 0x11C8, 0x2008 }, { 1000, 280366464, 180592, 294293540, 1088, 5840, 680, 0x11C8, 0x0000 },
    { 1100, 277735004, 125196, 295107140, 28728, 7760, 27640, 0x11C8, 0x0000 }, { 1200, 275255080, 166960, 294074068, 32, 9624, 1136, 0x11C8, 0x2000 },
    { 1300, 272608976, 72780, 294097104, 28584, 3328, 1456, 0x11C8, 0x2002 }, { 1400, 270510800, 200660, 295324752, 28624, 8008, 27624, 0x11C8, 0x2000 },
    { 1500, 268101628, 421744, 294243588, 28656, 8360, 28208, 0x11C8, 0x2000 }, { 1600, 266823708, 546576, 292214228, 28784, 15032, 27304, 0x11C8, 0x2000 },
    { 1700, 267236564, 630280, 289745140, 88, 13912, 304, 0x11C8, 0x0000 }, { 1800, 266570160, 319728, 288191812, 1384, 11256, 560, 0x11C8, 0x0000 },
    { 1900, 265673852, 25048, 287369064, 376, 10616, 376, 0x11C8, 0x0000 }, { 2040, 265206172, 1800, 286967284, 0, 10432, 0, 0x19C8, 0x0080 },
    { 2044, 265206172, 1800, 286967284, 0, 10432, 0, 0x19C8, 0x0080 } };

typedef struct { long x, y, z; int pitch, heading, roll; unsigned w0, w2; } rec_t;
static rec_t REC[2100];

/* Plays the shipped demo from the menu key and records the player record after every applied frame (index = frames applied). Returns the frames recorded. */
static int play_recorded(game_t *g) {
    aircraft_t *ac = game_debug_aircraft(g);
    int last = 0;
    for (int i = 0; i < 4000; i++) {
        game_step(g);
        const int d = game_demo_step(g);
        if (d < 0) { if (last > 0) break; continue; }
        if (getenv("DEMO_DESIGNATE") && d >= atoi(getenv("DEMO_DESIGNATE"))) ((entities_t *)game_debug_entities(g))->designated = 10;   /* diagnostic: the demo's T key (stream step 263) is Task 6's */
        if (getenv("DEMO_EARLY") && i < 400) { const entities_t *e = (const entities_t *)game_debug_entities(g); const entity_t *m = &e->rec[10]; printf("E10 rep %02X keys %02X i %d d %d pass %u w0 %04X st %02X S %d pos %d %d vel %d %d cnt %d\n", m->ac.f.repeat, m->ac.f.keys, i, d, e->pass, m->ac.f.w0, m->ac.f.state, m->ac.f.speed_target, m->ac.pos[0]>>8, m->ac.pos[2]>>8, m->ac.f.vel[0], m->ac.f.vel[2], m->ac.f.counter); }
        if (d > last && d < 2100) {
            rec_t *r = &REC[d];
            r->x = ac->pos[0]; r->y = ac->pos[1]; r->z = ac->pos[2];
            r->pitch = ac->f.pitch; r->heading = ac->f.heading; r->roll = ac->f.roll; r->w0 = ac->f.w0; r->w2 = ac->f.w2;
            last = d;
            if (getenv("DEMO_TRACE10")) {   /* diagnostic: the MiG (record 10) at each recorded step: pass counter, state, position, velocity, target, range, counter */
                const entities_t *e = (const entities_t *)game_debug_entities(g);
                const entity_t *m = &e->rec[10];
                if (d == 50) { const flight_state_t *cf = &e->rec[14].ac.f; printf("C14 w0 %04X cls %02X pos %d %d %d matB %d %d %d %d %d %d %d %d %d range %d\n", cf->w0, cf->cls, e->rec[14].ac.pos[0], e->rec[14].ac.pos[1], e->rec[14].ac.pos[2], cf->mat_b[0], cf->mat_b[1], cf->mat_b[2], cf->mat_b[3], cf->mat_b[4], cf->mat_b[5], cf->mat_b[6], cf->mat_b[7], cf->mat_b[8], (int)(uint16_t)e->rec[14].range); }
                printf("T10 d %d pass %u w0 %04X st %02X pos %d %d %d vel %d %d %d tgt %02X range %04X cnt %d keys %02X flags %02X id %02X b4 %02X rep %02X dest %d %d %d %d %d S %d pitch %d head %d roll %d T %d K %u | player pos %d %d %d\n", d, e->pass, m->ac.f.w0, m->ac.f.state, m->ac.pos[0], m->ac.pos[1], m->ac.pos[2], m->ac.f.vel[0], m->ac.f.vel[1], m->ac.f.vel[2], m->target, (uint16_t)m->range, m->ac.f.counter, m->ac.f.keys, m->ai_flags, m->ac.f.id, m->ac.f.b4, m->ac.f.repeat, m->dest_cx, m->dest_cz, m->dest_fx, m->dest_fz, m->dest_h, m->ac.f.speed_target, (int16_t)m->ac.f.pitch, (int16_t)m->ac.f.heading, (int16_t)m->ac.f.roll, m->ac.f.throttle, entities_kill_count(e), ac->pos[0], ac->pos[1], ac->pos[2]);
            }
        }
        if (d >= 2046) break;
    }
    return last;
}

TEST(the_landmark_constant_of_the_replay_is_the_notes_z_48) {
    ASSERT_EQ_INT(FLIGHT_LANDMARK_Z, 48);                      /* game.c installs it in the environment (fenv.landmark_z) of every flight, the demo's included */
}

TEST(the_recorded_run_matches_the_21_checkpoint_rows_exactly) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 0;
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    ASSERT_EQ_INT(play_recorded(g), 2046);                     /* frames 0 to 2045 are played (7.4 rule 4) */
    int bad = 0;
    for (int i = 0; i < 21; i++) {
        const cp_t *c = &CP[i];
        const rec_t *r = &REC[c->n + 1];   /* the state after step N: N + 1 steps done (7.8) */
        const int ok = r->x == c->x && r->y == c->y && r->z == c->z && r->pitch == c->pitch && r->heading == c->heading && r->roll == c->roll &&
                         r->w0 == c->w0 && r->w2 == c->w2;   /* the flag words W0 and W2 of the table too */
        if (!ok) {
            printf("\n  row %d differs: pos (%ld, %ld, %ld) att (%d, %d, %d) W0 %04x W2 %04x expected (%ld, %ld, %ld) (%d, %d, %d) %04x %04x", c->n, r->x, r->y, r->z, r->pitch,
                   r->heading, r->roll, r->w0, r->w2, c->x, c->y, c->z, c->pitch, c->heading, c->roll, c->w0, c->w2);
            bad++;
        }
    }
    ASSERT_EQ_INT(bad, 0);
    ASSERT_EQ_INT(REC[2046].w0 & 0x0200, 0);                   /* no crash: W0 bit 0x0200 is never set (7.8) */
    game_destroy(g);
}

TEST(the_demo_places_its_row_0x7f_hostiles_the_two_mig_29_entries_of_the_slot_10_with_the_first_placed) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 0;
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    ASSERT_EQ_INT(play_recorded(g), 2046);
    const entities_t *e = (const entities_t *)game_debug_entities(g);
    /* the demo's hostile (note 07 2.4, the tenth row of the table 0x2DEDA) is placed and later shot down: see the_demos_aim_120_hits_the_mig_at_pass_0x27c_and_the_kill_counts_at_0x28b */
    ASSERT_TRUE(e->rec[14].ac.f.w0 & 0x0040);                 /* the carrier */
    game_destroy(g);
}

TEST(the_recorded_demo_never_crashes_and_ends_on_the_ground_at_rest) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 0;
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    ASSERT_EQ_INT(play_recorded(g), 2046);
    for (int d = 1; d <= 2046; d++) ASSERT_EQ_INT(REC[d].w0 & 0x0200, 0);
    ASSERT_EQ_INT(game_crash_allowance(g), 3);                 /* the allowance stays 3 (7.8) */
    ASSERT_EQ_INT(game_crash_count(g), 0);
    ASSERT_EQ_INT(REC[2041].y, 1800); ASSERT_EQ_INT(REC[2041].w2 & 0x0080, 0x0080);   /* the aircraft stands on the runway, brake on, speed zero */
    ASSERT_EQ_INT(REC[2041].w0 & 0x0800, 0x0800);
    game_destroy(g);
}

/* The demo's AIM-120 (note 07 8.9, note 08 7.6): the fire event of the recorded keys (frames 599 to 602) launches record 1 at the MiG the demo designated (the stock 0x24 becomes 0x23, the
 * log's +0x3E counts one, the record is class 1 and takes its first boost 0x1E0 in the launch pass, speed word 0x2443); it hits the MiG in the placed record 10 in pass 0x27C and the kill is
 * counted fifteen passes later, in pass 0x28B, as the capture of the original shows. */
TEST(the_demos_aim_120_hits_the_mig_at_pass_0x27c_and_the_kill_counts_at_0x28b) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 0;
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    entities_t *e = (entities_t *)game_debug_entities(g);
    unsigned hit = 0, kill = 0;
    int placed = 0;
    for (int i = 0; i < 4000; i++) {
        game_step(g);
        const int d = game_demo_step(g);
        if (d < 0) continue;
        if (d == 100) { placed = (e->rec[10].ac.f.w0 & 0x0040) && (e->rec[10].ac.f.cls & 0xF0) == 0x10; }
        if (!hit && (e->rec[10].ac.f.w0 & 0x0400)) hit = e->pass;      /* the dying bit: the hit of the missile (original: 54C1 from pass 0x27C) */
        if (!kill && entities_kill_count(e) == 1) kill = e->pass;      /* V:4A963 */
        if (d >= 720) break;
    }
    ASSERT_TRUE(placed);
    ASSERT_EQ_INT(hit, 0x27C);   /* captured per pass (combat-hit-demolong.txt, combat-demo-mig*.txt): record 10 agrees word for word with the port from pass 500 to 698 */
    ASSERT_EQ_INT(kill, 0x28B);
    game_destroy(g);
}

TEST(the_demos_fire_event_launches_the_aim_120_at_the_designated_mig) {
    if (!have_real()) { printf("skipped "); return; }
    use_syn = 0;
    game_t *g = start_demo(); ASSERT_TRUE(g != NULL);
    entities_t *e = (entities_t *)game_debug_entities(g);
    int launch = -1;
    for (int i = 0; i < 4000 && launch < 0; i++) {
        game_step(g);
        const int d = game_demo_step(g);
        if (d >= 0 && (e->rec[1].ac.f.w0 & 0x40)) {
            launch = d;
            uint8_t log[GAME_LOG_FILE_SIZE];
            game_log_export(g, log, sizeof log);
            ASSERT_EQ_INT(e->rec[0].stock, 0x23);
            ASSERT_EQ_INT(log[5 + 0x3E] * 256 + log[5 + 0x3F], 1);
            ASSERT_EQ_INT(e->rec[1].ac.f.cls, 1);
            ASSERT_EQ_INT(e->designated, 10);
            ASSERT_EQ_INT(e->rec[1].target, 0x8A);
            ASSERT_EQ_INT(e->rec[1].ac.f.counter, 199);
            ASSERT_EQ_INT(e->rec[1].ac.f.speed_target, 0x2443);
        }
        if (d >= 700) break;
    }
    ASSERT_TRUE(launch >= 599 && launch <= 602);
    game_destroy(g);
}

int main(void) {
    RUN(the_played_frames_end_at_the_first_run_of_three_ff_bytes);
    RUN(frame_i_is_the_key_byte_i_and_the_events_of_index_i_none_for_frame_0);
    RUN(the_start_rewinds_to_frame_0_and_stop_ends_the_reader);
    RUN(every_bit_set_runs_in_the_executors_order_through_the_key_events);
    RUN(the_bit_that_stands_for_kp5_or_the_bracket_follows_the_live_view);
    RUN(a_set_bit_of_the_mask_00f0_in_word_1_is_not_an_event);
    RUN(the_handler_takes_0x7f_and_chains_the_other_modes);
    RUN(without_a_stream_menu_key_1_is_ignored);
    RUN(the_start_sequence_counts_the_waits_in_steps_as_the_note_converts_them);
    RUN(a_kp_enter_does_not_skip_anything_in_the_demo);
    RUN(esc_in_the_echo_and_in_the_wait_restarts_the_whole_selection);
    RUN(esc_after_the_set_up_restarts_the_demo_directly_without_the_echo);
    RUN(shift_esc_returns_to_the_menu_from_the_demo_and_clears_it);
    RUN(the_mode_sequence_of_the_demo_is_menu_then_the_flight_in_mode_3);
    RUN(the_first_stream_frame_is_applied_44_steps_after_the_menu_key_on_the_real_text);
    RUN(the_demo_carrier_and_the_player_start_follow_the_heading_28600);
    RUN(the_key_byte_of_frame_i_drives_step_i_and_the_live_controls_are_ignored);
    RUN(the_events_of_index_i_run_before_step_i_in_the_note_order_and_index_0_never);
    RUN(the_gear_and_brake_events_reach_the_flight_through_the_ordinary_handlers);
    RUN(p_pauses_the_demo_and_the_stream_stands_still);
    RUN(the_end_of_the_stream_runs_the_esc_flow_and_the_demo_restarts_at_frame_0);
    RUN(esc_in_the_demo_restarts_it_at_frame_0_and_the_stream_is_not_continued);
    RUN(esc_before_the_first_step_restarts_at_frame_0);
    RUN(shift_esc_leaves_the_demo_for_the_menu_and_a_second_start_works);
    RUN(the_demo_books_no_flight_time_and_has_no_crash_to_count);
    RUN(a_crash_in_the_demo_is_counted_in_the_log_the_counters_are_not_gated_by_v4a848_7_6);
    RUN(an_esc_in_the_fade_in_after_the_set_up_fades_out_from_the_level_reached_and_later_from_15);
    RUN(free_flight_after_a_demo_has_its_own_carrier);
    RUN(the_landmark_constant_of_the_replay_is_the_notes_z_48);
    RUN(the_recorded_run_matches_the_21_checkpoint_rows_exactly);
    RUN(the_demo_places_its_row_0x7f_hostiles_the_two_mig_29_entries_of_the_slot_10_with_the_first_placed);
    RUN(the_recorded_demo_never_crashes_and_ends_on_the_ground_at_rest);
    RUN(the_demos_fire_event_launches_the_aim_120_at_the_designated_mig);
    RUN(the_demos_aim_120_hits_the_mig_at_pass_0x27c_and_the_kill_counts_at_0x28b);
    TEST_MAIN_END();
}
