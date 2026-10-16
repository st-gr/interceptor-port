/* tests/test_game.c */
#include "test.h"
#include "../src/core/game.h"
#include "../src/core/screen.h"
#include "../src/core/weapons.h"
#include "../src/core/entities.h"
#include "../src/core/training.h"
#include "../src/core/sound.h"

/* user: NULL = serve the current CORE_ASSETS_VERSION and a three-name pix index; else a const char *
 * holding the VERSION text to serve. */
static int serve(const char *text, uint8_t **out, size_t *len) {
    size_t n = strlen(text);
    *out = (uint8_t *)malloc(n + 1);
    memcpy(*out, text, n);
    *len = n;
    return 0;
}
static int fake_read(void *user, const char *path, uint8_t **out, size_t *len) {
    if (!strcmp(path, "VERSION")) return serve(user ? (const char *)user : "27\n", out, len);
    if (!strcmp(path, "pix/index.txt")) return serve("splsh\ninst5\nfrnt5\n", out, len);
    if (!strcmp(path, "sounds/index.txt")) return serve("a\nb\n", out, len);
    return -1;
}
static void fake_release(void *user, uint8_t *buf) { (void)user; free(buf); }

TEST(create_with_null_loader_fails_with_message) {
    char err[64] = "";
    game_t *g = game_create(NULL, err, sizeof err);
    ASSERT_TRUE(g == NULL);
    ASSERT_TRUE(strlen(err) > 0);
}

TEST(scene_before_any_step_is_the_credits_at_tick_zero) {
    game_loader_t ld = { fake_read, fake_release, NULL };
    game_t *g = game_create(&ld, NULL, 0);
    ASSERT_TRUE(g != NULL);
    game_scene_t s;
    memset(&s, 0xAA, sizeof s);
    game_scene(g, &s);
    ASSERT_EQ_INT(s.mode, GAME_MODE_CREDITS);     /* the first screen of a session (note 08 5.2) */
    ASSERT_EQ_INT(s.tick, 0);
    ASSERT_EQ_INT(s.entry_count, 0);
    ASSERT_EQ_INT(s.text_count, 0);
    ASSERT_EQ_INT(s.audio_count, 0);
    game_destroy(g);
}

TEST(step_increments_tick_and_input_is_accepted) {
    game_loader_t ld = { fake_read, fake_release, NULL };
    game_t *g = game_create(&ld, NULL, 0);
    game_event_t ev = { GAME_EV_BUTTON, GAME_BTN_ANY, 1 };
    game_input(g, &ev);
    game_step(g);
    game_step(g);
    game_scene_t s;
    game_scene(g, &s);
    ASSERT_EQ_INT(s.tick, 2);
    game_destroy(g);
}

TEST(null_context_calls_do_not_crash) {
    game_event_t ev = { GAME_EV_BUTTON, GAME_BTN_ANY, 1 };
    game_scene_t s;
    memset(&s, 0xAA, sizeof s);
    game_step(NULL);
    game_input(NULL, &ev);
    game_scene(NULL, &s);
    ASSERT_EQ_INT(s.mode, GAME_MODE_SPLASH);
    ASSERT_EQ_INT(s.tick, 0);
}

TEST(create_refuses_wrong_extractor_version) {
    char err[128] = "";
    game_loader_t ld = { fake_read, fake_release, (void *)"1\n" };
    game_t *g = game_create(&ld, err, sizeof err);
    ASSERT_TRUE(g == NULL);
    ASSERT_STR_EQ(err, "assets were made by extractor version 1, need 27");
}

TEST(create_reads_asset_indexes) {
    game_loader_t ld = { fake_read, fake_release, NULL };
    char err[128] = "";
    game_t *g = game_create(&ld, err, sizeof err);
    ASSERT_TRUE(g != NULL);
    game_scene_t s;
    game_scene(g, &s);
    ASSERT_EQ_INT(s.splash_asset, 0);
    game_destroy(g);
}

static int no_pix_read(void *user, const char *path, uint8_t **out, size_t *len) {
    if (!strcmp(path, "pix/index.txt")) return -1;
    return fake_read(user, path, out, len);
}
static int empty_pix_read(void *user, const char *path, uint8_t **out, size_t *len) {
    if (!strcmp(path, "pix/index.txt")) return serve("\n", out, len);
    return fake_read(user, path, out, len);
}

TEST(create_fails_naming_missing_pix_index) {
    char err[128] = "";
    game_loader_t ld = { no_pix_read, fake_release, NULL };
    ASSERT_TRUE(game_create(&ld, err, sizeof err) == NULL);
    ASSERT_TRUE(strstr(err, "pix/index.txt") != NULL);
}

TEST(create_fails_on_empty_pix_index) {
    char err[128] = "";
    game_loader_t ld = { empty_pix_read, fake_release, NULL };
    ASSERT_TRUE(game_create(&ld, err, sizeof err) == NULL);
    ASSERT_TRUE(strstr(err, "pix/index.txt") != NULL);
}

/* ---- the function keys (real assets below assets/, skipped without them) ---- */
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

static void press(game_t *g, int code, int value) { game_event_t ev = { GAME_EV_BUTTON, code, value }; game_input(g, &ev); }

TEST(function_keys_store_the_preset_12n_f10_121_and_f1_again_at_12_stores_minus_1) {
    game_loader_t ld = { real_read, fake_release, NULL };
    char err[256];
    game_t *g = game_create(&ld, err, sizeof err);
    if (!g) { printf("skipped "); return; }
    game_debug_start_flight(g, 3, 0);
    aircraft_t *ac = game_debug_aircraft(g);
    if (!ac) { game_destroy(g); printf("skipped "); return; }
    ASSERT_EQ_INT(ac->f.v4a830, 72);                 /* the start: P = Q = 72 (T = 9), no throttle event was sent */
    ASSERT_EQ_INT(ac->f.v4a834, 72);
    press(g, GAME_BTN_FKEY, 3);
    game_step(g);
    ASSERT_EQ_INT(ac->f.v4a928, 36);                 /* F3: 12 x 3 */
    press(g, GAME_BTN_FKEY, 10);
    game_step(g);
    ASSERT_EQ_INT(ac->f.v4a928, 121);                /* F10 */
    ac->f.v4a928 = 12;
    press(g, GAME_BTN_FKEY, 1);
    game_step(g);
    ASSERT_EQ_INT(ac->f.v4a928, -1);                 /* F1 again while at 12 stores idle */
    press(g, GAME_BTN_FKEY, 11);                     /* outside 1..10: ignored */
    press(g, GAME_BTN_FKEY, 0);
    game_step(g);
    ASSERT_EQ_INT(ac->f.v4a928, -1);
    game_destroy(g);
}

TEST(view_and_flight_events_are_ignored_outside_flight) {
    game_loader_t ld = { fake_read, fake_release, NULL };
    game_t *g = game_create(&ld, NULL, 0);
    game_scene_t s;
    for (int c = GAME_BTN_TOWER; c <= GAME_BTN_THROTTLE_DOWN; c++) {   /* the flight buttons: not menu keys (note 08 5.9), and no flight is running */
        game_event_t ev = { GAME_EV_BUTTON, c, 1 };
        game_input(g, &ev);
    }
    for (int i = 0; i < 10; i++) game_step(g);   /* the credits' pass is 60 blanks: a key stored in it (the buttons above) holds the latch for about 10 steps (08 5.8) */
    game_set_debug_freecam(g, 1);
    game_set_debug_freecam(NULL, 1);
    game_scene(g, &s);
    ASSERT_EQ_INT(s.mode, GAME_MODE_CREDITS);
    ASSERT_EQ_INT(s.camera.view, GAME_VIEW_COCKPIT);
    ASSERT_EQ_INT(s.camera.head, 0);
    game_event_t kp = { GAME_EV_BUTTON, GAME_BTN_KEYPAD, 3 };   /* a keypad key is a key: it leaves the credits and does nothing else */
    game_input(g, &kp);
    game_step(g);
    game_scene(g, &s);
    ASSERT_EQ_INT(s.mode, GAME_MODE_MENU);                      /* the check result 0 (default): no log-disk prompt (note 08 5.3) */
    ASSERT_EQ_INT(s.camera.view, GAME_VIEW_COCKPIT);
    game_destroy(g);
}

TEST(crash_and_start_accessors_are_null_safe_and_idle_without_a_flight) {
    game_loader_t ld = { fake_read, fake_release, NULL };
    game_t *g = game_create(&ld, NULL, 0);
    int hb = 99;
    ASSERT_EQ_INT(game_crash_state(NULL, &hb), GAME_CRASH_NONE);
    ASSERT_EQ_INT(game_crash_count(NULL), 0);
    ASSERT_TRUE(game_debug_aircraft(NULL) == NULL);
    ASSERT_TRUE(game_debug_aircraft(g) == NULL);   /* no flight data in the splash-only asset set */
    game_set_start(g, 1);
    game_set_start(NULL, 1);
    ASSERT_EQ_INT(game_crash_state(g, &hb), GAME_CRASH_NONE);
    ASSERT_EQ_INT(hb, 0);
    ASSERT_EQ_INT(game_crash_count(g), 0);
    game_destroy(g);
}

/* ---- the replenish key W (PORT FEATURE, game.h): the original's dead rearm body 0x1ECA4 (note 08 11.6) ---- */
static game_t *flight_game(void) {
    game_loader_t ld = { real_read, fake_release, NULL };
    char err[256];
    game_t *g = game_create(&ld, err, sizeof err);
    if (!g) return NULL;
    game_debug_start_flight(g, 3, 0);
    if (!game_debug_aircraft(g) || !game_debug_weapons(g)) { game_destroy(g); return NULL; }
    for (int i = 0; i < 4; i++) game_step(g);
    return g;
}
static void run_down(game_t *g) {   /* low stores, a weapon record in use, another message on the line */
    aircraft_t *ac = game_debug_aircraft(g);
    weapons_t *w = game_debug_weapons(g);
    entities_t *e = game_debug_entities(g);
    ac->f.fuel = 1000;
    e->rec[0].stock = 0x01; e->rec[0].rounds = 7;
    w->flares = 3; w->chaff = 2;
    e->rec[1].ac.f.w0 = 0x10CA;
    ac->f.v4ab98 = 0x4023;
}
static int is_full(game_t *g) {
    aircraft_t *ac = game_debug_aircraft(g);
    weapons_t *w = game_debug_weapons(g);
    entities_t *e = game_debug_entities(g);
    return ac->f.fuel == 0x61A800 && e->rec[0].stock == 0x24 && e->rec[0].rounds == 500 && w->flares == 16 && w->chaff == 16 &&
           e->rec[1].ac.f.w0 == 0 && e->rec[2].ac.f.w0 == 0 && e->rec[3].ac.f.w0 == 0;
}
static int is_run_down(game_t *g) {
    aircraft_t *ac = game_debug_aircraft(g);
    weapons_t *w = game_debug_weapons(g);
    entities_t *e = game_debug_entities(g);
    return ac->f.fuel == 1000 && e->rec[0].stock == 0x01 && e->rec[0].rounds == 7 && w->flares == 3 && w->chaff == 2 && e->rec[1].ac.f.w0 == 0x10CA;
}

TEST(replenish_key_runs_the_one_rearm_body_of_the_landing_on_the_deck) {
    game_t *g = flight_game();
    if (!g) { printf("skipped "); return; }
    aircraft_t *ac = game_debug_aircraft(g);
    run_down(g);
    press(g, GAME_BTN_REPLENISH, 1);
    ASSERT_TRUE(is_full(g));
    ASSERT_EQ_INT(ac->f.v4ab98, 0x4005);                 /* entry 5, "refuelled and rearmed" */
    ASSERT_EQ_INT(ac->f.v4a984 & 1, 1);                  /* the post latch */
    ASSERT_EQ_INT(ac->f.fuel, 0x61A800);                 /* the fuel */
    ac->f.v4a984 &= (uint16_t)~1; ac->f.fuel = 1000;
    press(g, GAME_BTN_REPLENISH, 1);                     /* 0x1F11E posts and latches unconditionally, as the landing's call does: no repeat guard (note 05 17.13's guard is for the direct posts of the step) */
    ASSERT_EQ_INT(ac->f.v4a984 & 1, 1);
    ASSERT_EQ_INT(ac->f.v4ab98, 0x4005); ASSERT_EQ_INT(ac->f.fuel, 0x61A800);
    press(g, GAME_BTN_REPLENISH, 0);                     /* a release does nothing */
    game_destroy(g);
}

TEST(replenish_key_works_in_the_air_and_leaves_the_log_counters_alone) {
    game_t *g = flight_game();
    if (!g) { printf("skipped "); return; }
    aircraft_t *ac = game_debug_aircraft(g);
    game_scene_t before, after;
    ac->pos[1] += 0x400000;                              /* well above the deck */
    run_down(g);
    game_scene(g, &before);
    press(g, GAME_BTN_REPLENISH, 1);
    ASSERT_TRUE(is_full(g));
    game_scene(g, &after);
    ASSERT_EQ_INT(after.mode, before.mode);
    ASSERT_EQ_INT(after.log_dirty, before.log_dirty);
    game_destroy(g);
}

TEST(replenish_key_acts_in_a_pause_and_in_the_esc_countdown_as_the_other_control_keys_do) {
    game_t *g = flight_game();
    if (!g) { printf("skipped "); return; }
    press(g, GAME_BTN_PAUSE, 1);
    game_step(g);
    game_scene_t s;
    game_scene(g, &s);
    ASSERT_EQ_INT(s.mode, GAME_MODE_PAUSED);
    run_down(g);
    press(g, GAME_BTN_REPLENISH, 1);                     /* the key routine runs in every pass, paused or not (note 06 item 8, note 08 11.1 phase F) */
    ASSERT_TRUE(is_full(g));
    press(g, GAME_BTN_PAUSE, 1);
    game_step(g);
    run_down(g);
    press(g, GAME_BTN_ESC, 1);                           /* the ESC countdown: the flight keys still act (game.c flight_event) */
    game_step(g);
    press(g, GAME_BTN_REPLENISH, 1);
    ASSERT_TRUE(is_full(g));
    game_destroy(g);
}

TEST(replenish_key_is_ignored_in_training_part_1_and_with_the_ejection_nibble_set) {
    game_t *g = flight_game();
    if (!g) { printf("skipped "); return; }
    aircraft_t *ac = game_debug_aircraft(g);
    run_down(g);
    ac->f.gear = 0x0A;                                   /* the ejection cycle (V:4B2B8 low nibble): the control chain is skipped (note 08 11.1) */
    press(g, GAME_BTN_REPLENISH, 1);
    ASSERT_TRUE(is_run_down(g));
    ac->f.gear = 0;
    struct trainer *tr = game_debug_trainer(g);
    ASSERT_TRUE(tr != NULL);
    tr->part = 1;                                        /* part 1 drops the flight controls (note 08 8.6, 8.8) */
    press(g, GAME_BTN_REPLENISH, 1);
    ASSERT_TRUE(is_run_down(g));
    tr->part = 2;                                        /* part 2 (mode 0x7D) does not skip the control chain */
    press(g, GAME_BTN_REPLENISH, 1);
    ASSERT_TRUE(is_full(g));
    game_destroy(g);
}

TEST(replenish_key_is_ignored_in_the_demo_and_on_the_screens) {
    game_loader_t ld = { real_read, fake_release, NULL };
    char err[256];
    game_t *g = game_create(&ld, err, sizeof err);
    if (!g) { printf("skipped "); return; }
    game_scene_t s;
    for (int i = 0; i < 300; i++) {                      /* the credits take a key (W is just a key there) once their text is up */
        game_scene(g, &s);
        if (s.mode == GAME_MODE_MENU) break;
        if (s.text_count) press(g, i % 2 ? GAME_BTN_REPLENISH : GAME_BTN_RETURN, 1);
        game_step(g);
    }
    game_scene(g, &s);
    ASSERT_EQ_INT(s.mode, GAME_MODE_MENU);
    game_event_t k = { GAME_EV_KEY, 1, '1' };           /* the menu's demo row */
    game_input(g, &k);
    for (int i = 0; i < 400 && game_demo_step(g) < 3; i++) game_step(g);
    if (game_demo_step(g) < 3 || !game_debug_weapons(g)) { game_destroy(g); printf("skipped (no demo) "); return; }
    run_down(g);
    press(g, GAME_BTN_REPLENISH, 1);
    ASSERT_TRUE(is_run_down(g));                         /* the replay's live-key rule: only the system keys act */
    game_destroy(g);
}


/* ---- the counters at the first flight pass of each mode (note 07 12.1 [V: combat-panel-first-*.txt], note 06 for V:4A90F; real assets, skipped without them) ---- */

typedef struct { int ok, panel, redraw, v4a90f; } first_pass_t;

static game_t *menu_game(void) {   /* the boot: the credits, the log-disk result 0 (a usable disk), the menu */
    game_loader_t ld = { real_read, fake_release, NULL };
    char err[256];
    game_t *g = game_create(&ld, err, sizeof err);
    if (!g) return NULL;
    game_set_logdisk_result(g, 0);
    game_scene_t s;
    for (int i = 0; i < 3; i++) game_step(g);
    game_event_t k = { GAME_EV_KEY, 1, 'x' };
    game_input(g, &k);
    for (int i = 0; i < 300; i++) { game_scene(g, &s); if (s.mode == GAME_MODE_MENU) break; game_step(g); }
    for (int i = 0; i < 6; i++) game_step(g);
    return g;
}

/* Presses the keys (30 steps apart) and steps until the simulation switch of the screen module (the flight's first pass): the counters are those the pass reads, sampled at the end of the
 * last step before it. */
static first_pass_t run_to_first_pass(game_t *g, const char *keys) {
    first_pass_t r = { 0, -1, -1, 0 };
    for (const char *c = keys; *c; c++) { game_event_t k = { GAME_EV_KEY, 1, *c }; game_input(g, &k); for (int i = 0; i < 30 && c[1]; i++) game_step(g); }
    int seen = 0, hold_before = 0;
    for (int i = 0; i < 3000; i++) {
        game_step(g);
        const int h = screen_sim_hold(game_debug_screen(g));
        if (h) seen = 1;
        if (seen && hold_before && !h) { r.ok = 1; return r; }
        r.panel = game_debug_panel_counter(g); r.redraw = game_debug_redraw_counter(g); r.v4a90f = game_debug_v4a90f(g);
        hold_before = h;
    }
    return r;
}

static void check_first_pass(const char *keys, int panel, int redraw, int *skipped) {
    game_t *g = menu_game();
    if (!g) { *skipped = 1; return; }
    first_pass_t r = run_to_first_pass(g, keys);
    if (!r.ok) { game_destroy(g); *skipped = 1; return; }
    ASSERT_EQ_INT(r.panel, panel); ASSERT_EQ_INT(r.redraw, redraw);
    game_destroy(g);
}

static void check_look(const char *keys, int want_negative, int *skipped) {
    game_t *g = menu_game();
    if (!g) { *skipped = 1; return; }
    first_pass_t r = run_to_first_pass(g, keys);
    if (!r.ok) { game_destroy(g); *skipped = 1; return; }
    if (want_negative) ASSERT_TRUE(game_debug_look(g) < 0); else ASSERT_TRUE(game_debug_look(g) >= 0);
    game_destroy(g);
}
TEST(the_training_starts_store_0xff_in_the_look_byte_v4a86c_and_the_other_modes_do_not) {
    int sk = 0;
    check_look("3", 1, &sk); check_look("4", 1, &sk); check_look("5", 0, &sk);
    if (sk) printf("skipped ");
}

TEST(the_demo_reaches_its_first_flight_pass_with_v4a8ee_0_and_v4a8ef_0xff) { int sk = 0; check_first_pass("1", 0, 0xFF, &sk); if (sk) printf("skipped "); }
TEST(training_part_1_reaches_its_first_flight_pass_with_both_counters_counted_down_to_0) { int sk = 0; check_first_pass("3", 0, 0, &sk); if (sk) printf("skipped "); }
TEST(training_part_2_reaches_its_first_flight_pass_with_v4a8ee_0_and_v4a8ef_0xff) { int sk = 0; check_first_pass("4", 0, 0xFF, &sk); if (sk) printf("skipped "); }
TEST(the_qualification_reaches_its_first_flight_pass_with_v4a8ee_0_and_v4a8ef_0xff) { int sk = 0; check_first_pass("5", 0, 0xFF, &sk); if (sk) printf("skipped "); }
TEST(free_flight_reaches_its_first_flight_pass_with_v4a8ee_0_and_v4a8ef_0xff) { int sk = 0; check_first_pass("221", 0, 0xFF, &sk); if (sk) printf("skipped "); }

TEST(a_first_demo_after_the_boot_holds_v4a90f_0xff_and_one_after_a_reset_block_holds_1) {
    game_t *g = menu_game();
    if (!g) { printf("skipped "); return; }
    ASSERT_EQ_INT(game_debug_v4a90f(g), -1);                      /* the start-up routine's 0xFF: the credits, the log-disk RETURN and the menu build are no reset */
    first_pass_t r = run_to_first_pass(g, "1");
    if (!r.ok) { game_destroy(g); printf("skipped (no demo) "); return; }
    ASSERT_EQ_INT(r.v4a90f, -1);                                  /* a first demo: no map marks */
    ASSERT_EQ_INT(game_debug_v4a90f(g), -1);
    game_event_t esc = { GAME_EV_BUTTON, GAME_BTN_SHIFT_ESC, 1 };
    game_input(g, &esc);
    game_scene_t s;
    for (int i = 0; i < 300; i++) { game_step(g); game_scene(g, &s); if (s.mode == GAME_MODE_MENU && i > 100) break; }
    ASSERT_EQ_INT(game_debug_v4a90f(g), 1);                       /* the reset block of the return to the menu (0x1D25E) */
    r = run_to_first_pass(g, "1");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ_INT(r.v4a90f, 1);                                   /* a demo after a reset block shows the map marks */
    game_destroy(g);
}

/* Note 08 5.14 "P on the other screens and the passes at 0xFF" (followup-pause-screens, -pause-notice): (3) P in the take-off notice sets the pause byte and opens no pause screen; the notice and the wait run on and
 * the pause screen opens at the first flight pass; a second P before that clears the byte and the flight starts normally. (1) P in flight (byte 0) is a paused step at once. */
static game_t *to_state(const char *keys, screen_state_t st) {
    game_t *g = menu_game();
    if (!g) return NULL;
    for (const char *c = keys; *c; c++) { game_event_t k = { GAME_EV_KEY, 1, *c }; game_input(g, &k); for (int i = 0; i < 30 && c[1]; i++) game_step(g); }
    for (int i = 0; i < 3000 && game_debug_screen(g)->state != st; i++) game_step(g);
    if (game_debug_screen(g)->state != st) { game_destroy(g); return NULL; }
    return g;
}
static int mode_of(game_t *g) { game_scene_t s; game_scene(g, &s); return s.mode; }
static void press_p(game_t *g) { game_event_t p = { GAME_EV_BUTTON, GAME_BTN_PAUSE, 1 }; game_input(g, &p); }

TEST(p_in_the_notice_sets_the_byte_only_and_the_pause_screen_opens_at_the_first_flight_pass) {
    game_t *g = to_state("221", SCR_NOTICE);
    if (!g) { printf("skipped "); return; }
    press_p(g); game_step(g);
    ASSERT_EQ_INT(game_debug_aircraft(g)->f.v4a866, 0xFF);        /* the byte is set ... */
    ASSERT_EQ_INT(mode_of(g), GAME_MODE_FLIGHT);                  /* ... and no pause screen opens in the notice */
    for (int i = 0; i < 3000 && mode_of(g) != GAME_MODE_PAUSED; i++) { ASSERT_TRUE(game_debug_screen(g)->state != SCR_FLIGHT); game_step(g); }
    ASSERT_EQ_INT(mode_of(g), GAME_MODE_PAUSED);                  /* the first flight pass takes the byte */
    ASSERT_EQ_INT(game_debug_screen(g)->state, SCR_FLIGHT);
    ASSERT_EQ_INT(game_debug_aircraft(g)->f.steps, 0);            /* nothing was simulated meanwhile: the byte stops the player step */
    game_destroy(g);
}

TEST(a_second_p_before_the_flight_state_clears_the_byte_and_the_flight_starts_normally) {
    game_t *g = to_state("221", SCR_NOTICE);
    if (!g) { printf("skipped "); return; }
    press_p(g); game_step(g);
    press_p(g); game_step(g);
    ASSERT_EQ_INT(game_debug_aircraft(g)->f.v4a866, 0);
    for (int i = 0; i < 3000 && game_debug_screen(g)->state != SCR_FLIGHT; i++) game_step(g);
    for (int i = 0; i < 4; i++) { game_step(g); ASSERT_EQ_INT(mode_of(g), GAME_MODE_FLIGHT); }
    ASSERT_TRUE(game_debug_aircraft(g)->f.steps > 0);
    game_destroy(g);
}

TEST(p_in_flight_with_the_byte_at_0_enters_the_pause_in_the_step_it_arrives_a_paused_update) {
    game_t *g = to_state("221", SCR_FLIGHT);
    if (!g) { printf("skipped "); return; }
    for (int i = 0; i < 4; i++) game_step(g);
    const uint32_t before = game_debug_aircraft(g)->f.steps;
    press_p(g); game_step(g);
    ASSERT_EQ_INT(mode_of(g), GAME_MODE_PAUSED);
    ASSERT_EQ_INT(game_debug_aircraft(g)->f.steps, before);       /* that step did not simulate */
    game_destroy(g);
}

/* ---- the beeps of the trainings (note 10 7.4: 0x1E8C2 the RETURN, 0x1ED88 the S key); real assets, skipped without them. The same test on the invented world (the synthetic flight tables, the invented training
 * table) is tests/test_game_sound.c (return_in_training_part_1_beeps_and_the_s_key_does_not_on_the_invented_world) and runs in CI, as the rest of the sound wiring does. ---- */
static game_t *training_part_1(void) {
    game_t *g = menu_game();
    if (!g) return NULL;
    game_event_t k = { GAME_EV_KEY, 1, '3' };
    game_input(g, &k);
    for (int i = 0; i < 3000 && !(game_debug_screen(g)->state == SCR_FLIGHT && game_debug_trainer(g)->part == 1); i++) game_step(g);
    if (!(game_debug_screen(g)->state == SCR_FLIGHT && game_debug_trainer(g)->part == 1)) { game_destroy(g); return NULL; }
    for (int i = 0; i < 20; i++) game_step(g);
    return g;
}
TEST(return_in_training_part_1_beeps_and_the_s_key_does_not) {
    game_t *g = training_part_1();
    if (!g) { printf("skipped "); return; }
    sound_t *q = (sound_t *)game_debug_sound(g);
    press(g, GAME_BTN_KEYPAD, GAME_KP_ENTER);                     /* part 1 flies the external chase view, in which the beep is silent (0x37C4A tests V:4A83D): the cockpit */
    for (int i = 0; i < 12; i++) game_step(g);
    sound_stop_voice(q, 3);
    press(g, GAME_BTN_SMOKE, 1);                                  /* 0x1ED88 acts in mode 0x7D only */
    game_step(g);
    ASSERT_EQ_INT(sound_slot(q, 3), SND_NONE);
    press(g, GAME_BTN_RETURN, 1);                                 /* 0x1E8C2: mode 2 beeps, then sets the request */
    game_step(g);
    ASSERT_EQ_INT(sound_slot(q, 3), 4);
    game_destroy(g);
}

int main(void) {
    RUN(create_with_null_loader_fails_with_message);
    RUN(scene_before_any_step_is_the_credits_at_tick_zero);
    RUN(step_increments_tick_and_input_is_accepted);
    RUN(null_context_calls_do_not_crash);
    RUN(create_refuses_wrong_extractor_version);
    RUN(create_reads_asset_indexes);
    RUN(create_fails_naming_missing_pix_index);
    RUN(create_fails_on_empty_pix_index);
    RUN(function_keys_store_the_preset_12n_f10_121_and_f1_again_at_12_stores_minus_1);
    RUN(view_and_flight_events_are_ignored_outside_flight);
    RUN(crash_and_start_accessors_are_null_safe_and_idle_without_a_flight);
    RUN(replenish_key_runs_the_one_rearm_body_of_the_landing_on_the_deck);
    RUN(replenish_key_works_in_the_air_and_leaves_the_log_counters_alone);
    RUN(replenish_key_acts_in_a_pause_and_in_the_esc_countdown_as_the_other_control_keys_do);
    RUN(replenish_key_is_ignored_in_training_part_1_and_with_the_ejection_nibble_set);
    RUN(replenish_key_is_ignored_in_the_demo_and_on_the_screens);
    RUN(the_training_starts_store_0xff_in_the_look_byte_v4a86c_and_the_other_modes_do_not);
    RUN(the_demo_reaches_its_first_flight_pass_with_v4a8ee_0_and_v4a8ef_0xff);
    RUN(training_part_1_reaches_its_first_flight_pass_with_both_counters_counted_down_to_0);
    RUN(training_part_2_reaches_its_first_flight_pass_with_v4a8ee_0_and_v4a8ef_0xff);
    RUN(the_qualification_reaches_its_first_flight_pass_with_v4a8ee_0_and_v4a8ef_0xff);
    RUN(free_flight_reaches_its_first_flight_pass_with_v4a8ee_0_and_v4a8ef_0xff);
    RUN(p_in_the_notice_sets_the_byte_only_and_the_pause_screen_opens_at_the_first_flight_pass);
    RUN(a_second_p_before_the_flight_state_clears_the_byte_and_the_flight_starts_normally);
    RUN(p_in_flight_with_the_byte_at_0_enters_the_pause_in_the_step_it_arrives_a_paused_update);
    RUN(a_first_demo_after_the_boot_holds_v4a90f_0xff_and_one_after_a_reset_block_holds_1);
    RUN(return_in_training_part_1_beeps_and_the_s_key_does_not);
    TEST_MAIN_END();
}
