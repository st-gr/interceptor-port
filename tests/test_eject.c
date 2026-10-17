/* tests/test_eject.c - the ejection at the game level (docs/re/07-ai-and-weapons.md 6.4 and 6.5, note 05 17.17 rule 8; milestone 5, Review Focus 3, core side).
 * Runs the whole game (game_step) on the extracted assets (assets/ or argv[1]); skipped (exit 0) when the flight tables or the models are absent.
 * The nibble cycle of the player's +0x7C (10, 9, 8, 7, 1, 2, 3, 4, 5, 6 and the hold at 6), the seat in record 1 at the nibble 3, the view on the seat, the landing, the end code 0xF0 five
 * passes after request 2, and a crash during the hold that is NOT handed over. */
#include "test.h"
#include "../src/core/game.h"
#include "../src/core/entities.h"
#include "../src/core/flight.h"
#include "../src/core/screen.h"

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

#define SEA_X 273272584
#define SEA_Z 280480032

static game_t *start_game(void) {
    char err[128];
    game_t *g = game_create(&loader, err, sizeof err);
    if (!g) { printf("  (game_create: %s)\n", err); return NULL; }
    game_debug_start_flight(g, 3, 0);
    return g;
}
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
static void press(game_t *g, int code) { game_event_t ev = { GAME_EV_BUTTON, code, 1 }; game_input(g, &ev); }

/* Note 07 13.1 / 10.4 (V: weapon_check.py result40): the player with an ejection nibble that touches another aircraft is REMOVED (valid bit cleared, W2 0x1000, state 1, B20 bit 1, V:54E72 = 0, V:4A910 =
 * 0x0C) and the ejection goes on: the nibble cycle runs from the world update and the seat appears in record 1 in the sixth pass after the contact, with the seat viewed. */
TEST(the_player_with_a_nibble_that_touches_an_aircraft_is_removed_and_the_ejection_goes_on_to_the_seat) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 5; i++) game_step(g);
    stage_air(g, SEA_X, 600000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    game_step(g);
    press(g, GAME_BTN_EJECT);
    game_step(g);
    ASSERT_EQ_INT(ac->f.gear & 0x0F, 9);                                  /* the key's step set the nibble and counted it down once */
    const int32_t before[3] = { ac->pos[0], ac->pos[1], ac->pos[2] };
    flight_test_result(ac, 0x40, before);
    ASSERT_EQ_INT(ac->f.w0 & 0x0040, 0);                                  /* removed from the lists and the contact scan */
    ASSERT_EQ_INT(ac->f.w0 & 0x1000, 0x1000);                             /* the active bit is kept for the result 0x40 */
    ASSERT_EQ_INT(ac->f.w2 & 0x1000, 0x1000); ASSERT_EQ_INT(ac->f.state, 1); ASSERT_EQ_INT(ac->f.b20 & 0x02, 0x02);
    ASSERT_EQ_INT(e->trail, 0);                                           /* V:54E72 = the player's own offset */
    ASSERT_EQ_INT(ac->f.v4a910, 0x0C);
    static const int want[5] = { 8, 7, 1, 2, 3 };
    for (int k = 0; k < 5; k++) {
        game_step(g);
        ASSERT_EQ_INT(ac->f.gear & 0x0F, want[k]);
        ASSERT_EQ_INT(e->rec[1].ac.f.cls == 0x30, k == 4);                /* the seat appears with the count 3, the sixth pass after the key (the rig: the sixth after a contact in the first) */
    }
    ASSERT_EQ_INT(ac->f.v4a996, 0x0200);                                  /* the viewed record is the seat */
    game_destroy(g);
}

TEST(the_eject_key_runs_the_nibble_cycle_the_seat_spawns_at_3_the_view_switches_and_the_nibble_holds_at_6) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 5; i++) game_step(g);
    stage_air(g, SEA_X, 600000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    game_step(g);
    press(g, GAME_BTN_EJECT);
    static const int want[10] = { 9, 8, 7, 1, 2, 3, 4, 5, 6, 6 };
    for (int k = 0; k < 10; k++) {
        game_step(g);
        ASSERT_EQ_INT(ac->f.gear & 0x0F, want[k]);
        if (k < 5) ASSERT_EQ_INT(e->rec[1].ac.f.w0 & 0x0040, 0);        /* no seat before the count reaches 3 */
        if (k <= 1) {   /* the scene flags the pass that took the key: the EJECT lamp's timer first counts in the next pass's draw (note 07 6.5) */
            game_scene_t *q = (game_scene_t *)calloc(1, sizeof *q);
            game_scene(g, q);
            ASSERT_EQ_INT(q->hud.cp_eject_key, k == 0);
            ASSERT_EQ_INT(q->hud.cp_eject_armed, 1);
            free(q);
        }
        if (k == 5) {   /* the view switch to the seat in the cockpit view runs the seat's head set-up and the helper 0x1E7B2 (note 07 12.1 writer 4: 0x1EC94): R = 3 in that pass */
            game_scene_t *q = (game_scene_t *)calloc(1, sizeof *q);
            game_scene(g, q);
            ASSERT_EQ_INT(q->hud.cp_redraw, 3);
            ASSERT_EQ_INT(q->camera.viewed, 1);                         /* the seat's view from the spawn pass ... */
            ASSERT_EQ_INT(q->camera.hud_step, 0);                       /* ... with the cockpit window and the panel for this one picture (note 07 6.5) */
            free(q);
        }
        if (k == 6) {   /* the next picture: the window offset 800 and row 167, no panel */
            game_scene_t *q = (game_scene_t *)calloc(1, sizeof *q);
            game_scene(g, q);
            ASSERT_EQ_INT(q->camera.viewed, 1);
            ASSERT_EQ_INT(q->camera.hud_step, 50);
            free(q);
        }
    }
    ASSERT_EQ_INT(e->rec[1].ac.f.cls, 0x30);                              /* the seat spawned in the pass of the count 3 */
    ASSERT_EQ_INT(ac->f.v4aa7a, 0x0200);                                  /* the latch */
    ASSERT_EQ_INT(ac->f.v4a994, 1);                                       /* the viewed record is the seat (cockpit view) */
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    game_scene(g, s);
    ASSERT_EQ_INT(s->camera.viewed, 1);
    ASSERT_EQ_INT(s->hud.cp_eject_armed, 1);
    ASSERT_EQ_INT(s->camera.hud_step, 50);                                /* the seat's cockpit view: offset 800 */
    free(s);
    for (int k = 0; k < 20; k++) { game_step(g); ASSERT_EQ_INT(ac->f.gear & 0x0F, 6); }   /* the hold at 6 while the seat flies */
    ASSERT_TRUE(e->rec[1].ac.f.counter >= 0);                             /* the seat's counter counts up from -20 */
    ASSERT_TRUE(ac->f.w0 & 0x0040);                                       /* the aircraft flies on */
    game_destroy(g);
}

/* The seat's view switch in an external view (0x1EC14 to 0x1EC54, note 07 12.1): the zoom reset 0x1F060 (both views), the external view stays, the ring direction becomes 4 and V:4A8EE = 3 is stored
 * (0x1EC54, external branch only); the pass's panel draw counts it once, so the pass that spawns the seat ends with 2. The 32nd-pass writer and the head keys are not in play: the counter is 0 before. */
TEST(the_seats_view_switch_in_an_external_view_resets_the_zoom_and_stores_the_panel_counter) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 5; i++) game_step(g);
    stage_air(g, SEA_X, 600000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    game_event_t kp = { GAME_EV_BUTTON, GAME_BTN_KEYPAD, GAME_KP_ENTER };
    game_input(g, &kp);                                                    /* the external view */
    for (int i = 0; i < 8; i++) game_step(g);
    ASSERT_EQ_INT(game_debug_panel_counter(g), 0);                        /* the three panel passes of the switch have counted it down */
    game_debug_set_zoom(g, 0x40);                                          /* an external view's `]` is the level key, not the zoom key */
    for (int i = 0; i < 2; i++) game_step(g);
    ASSERT_EQ_INT(game_debug_zoom(g), 0x40);
    press(g, GAME_BTN_EJECT);
    int sw = -1;
    for (int k = 0; k < 45 && sw < 0; k++) {   /* the seat switches the view once its counter reaches 0x14 (06 6.4); the periodic writer acts at the passes in between, not in this one */
        const int before = game_debug_panel_counter(g);
        game_step(g);
        if (game_debug_zoom(g) == 0x80) {
            sw = k;
            ASSERT_EQ_INT(before, 0);
            ASSERT_EQ_INT(game_debug_panel_counter(g), 2);                 /* 3 stored by 0x1EC54, one count by that pass's panel draw */
        }
    }
    ASSERT_TRUE(sw >= 0);                                                  /* the zoom reset */
    game_destroy(g);
}

/* The same switch in the cockpit view: the zoom reset runs in both views (0x1EC3C). The seat's own helper stores the panel counter there, so the value is not told apart from 0x1EC54 here. */
TEST(the_seats_view_switch_in_the_cockpit_view_resets_the_zoom) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 5; i++) game_step(g);
    stage_air(g, SEA_X, 600000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    for (int i = 0; i < 3; i++) game_step(g);
    press(g, GAME_BTN_RBRACKET);
    ASSERT_EQ_INT(game_debug_zoom(g), 0x40);
    press(g, GAME_BTN_EJECT);
    int hit = 0;
    for (int k = 0; k < 45 && !hit; k++) { game_step(g); if (game_debug_zoom(g) == 0x80) hit = 1; }
    ASSERT_TRUE(hit);
    game_destroy(g);
}

static int armed(game_t *g) {
    game_scene_t *q = (game_scene_t *)calloc(1, sizeof *q);
    game_scene(g, q);
    const int a = q->hud.cp_eject_armed;
    free(q);
    return a;
}

/* Note 07 6.4a: the key E is in the first chain of the key handler, which the handler skips while the low nibble of the gear byte is not 0 (0x1DB2E, note 08 11.1) [V: the emulator, SHIFT-E
 * twice in free flight: the second press at the nibble 6 changed neither the lamp V:4A863 nor the nibble nor the records]. A second press at the nibble 7, 6 or any other non-zero value is ignored:
 * no OR into the nibble, no toggle of the lamp, no parachute (the parachute comes from the rescue key in mode 6 or a nibble of 15 at the step, which a key cannot make). */
TEST(a_second_press_while_the_nibble_is_not_zero_is_ignored_at_7_and_at_6) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 5; i++) game_step(g);
    stage_air(g, SEA_X, 600000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    game_step(g);
    press(g, GAME_BTN_EJECT);
    ASSERT_EQ_INT(armed(g), 1);
    game_step(g); game_step(g); game_step(g);                              /* the nibble reads 9, 8, 7 */
    ASSERT_EQ_INT(ac->f.gear & 0x0F, 7);
    press(g, GAME_BTN_EJECT);                                              /* 7 or 10 would be 15 (the parachute): the handler does not run */
    ASSERT_EQ_INT(armed(g), 1);
    game_step(g);
    ASSERT_EQ_INT(ac->f.gear & 0x0F, 1);                                   /* the cycle goes on: 7 gives 1 */
    ASSERT_TRUE(e->rec[1].ac.f.cls != 0x31);
    ASSERT_EQ_INT(e->chute_latch, 0);
    game_destroy(g);
    /* at the hold (the nibble 6): 6 or 10 would be 14, which the step makes 13 */
    g = start_game();
    for (int i = 0; i < 5; i++) game_step(g);
    stage_air(g, SEA_X, 600000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    ac = game_debug_aircraft(g); e = (entities_t *)game_debug_entities(g);
    game_step(g);
    press(g, GAME_BTN_EJECT);
    for (int k = 0; k < 9; k++) game_step(g);                              /* 9, 8, 7, 1, 2, 3 (the seat), 4, 5, 6 */
    ASSERT_EQ_INT(ac->f.gear & 0x0F, 6);
    ASSERT_EQ_INT(e->rec[1].ac.f.cls, 0x30);
    press(g, GAME_BTN_EJECT);
    game_step(g);
    ASSERT_EQ_INT(ac->f.gear & 0x0F, 6);
    ASSERT_EQ_INT(armed(g), 1);                                            /* the lamp is not toggled back */
    ASSERT_EQ_INT(e->chute_latch, 0);
    for (int i = 1; i <= 3; i++) ASSERT_TRUE(e->rec[i].ac.f.cls != 0x31);
    game_destroy(g);
}

/* Note 07 6.4a: the same gate stops the other keys of the first chain (G here: no gear toggle, no gear sound, no beep), and the press that sets the nibble stops the keys of its own drain. */
TEST(the_keys_of_the_first_chain_are_skipped_while_the_nibble_is_not_zero_and_after_an_ejection_press_of_the_same_drain) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 5; i++) game_step(g);
    stage_air(g, SEA_X, 600000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    aircraft_t *ac = game_debug_aircraft(g);
    game_step(g);
    press(g, GAME_BTN_EJECT);
    press(g, GAME_BTN_GEAR);                                               /* the same drain, after the OR of the handler: skipped */
    game_step(g);
    ASSERT_EQ_INT(ac->f.gear & 0x80, 0x80);                                /* the gear stays up (a toggle would have cleared bit 7) */
    press(g, GAME_BTN_GEAR);
    game_step(g);
    ASSERT_EQ_INT(ac->f.gear & 0x80, 0x80);
    game_destroy(g);
}

TEST(the_landed_seat_ends_the_flight_with_the_code_0xf0_once) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 5; i++) game_step(g);
    stage_air(g, SEA_X, 60000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    game_step(g);
    press(g, GAME_BTN_EJECT);
    (void)e;
    int landed = -1, request = -1, ended = -1;
    for (int k = 0; k < 3000 && ended < 0; k++) {
        game_step(g);
        if (landed < 0 && (e->rec[1].ac.f.w0 & 0x8000)) landed = k;
        if (request < 0 && game_debug_screen(g)->request == 2) request = k;
        if (ac->f.v4a850 == 0xF0) ended = k;
    }
    ASSERT_TRUE(landed > 0);
    ASSERT_EQ_INT(request - landed, 5);                                   /* the counter 4, 3, 2, 1, 0: the request in the 5th pass after the landing pass */
    ASSERT_EQ_INT(ended - request, 6);                                    /* the end code six passes after the request (note 07 6.5: pass 1340 to 1346), 11 passes after the landing pass */
    ASSERT_EQ_INT(ended - landed, 11);
    ASSERT_EQ_INT(game_debug_screen(g)->request, 0);                      /* consumed once */
    game_destroy(g);
}

/* Note 07 6.4 and 6.5 (A8; V: followup-combat-crash-after-eject.txt), note 05 17.17 rule 8 as corrected: the aircraft that reaches the floor with the seat in flight has the nibble cleared in the crash pass and the
 * hand-over runs as for any crash: end code 4, the hold of 40, the allowance down by one, the restart that frees the seat and returns the view to the player; no request 2, no end code 0xF0. */
TEST(an_aircraft_that_crashes_during_the_hold_clears_the_nibble_and_is_handed_over_as_for_any_crash_and_the_restart_frees_the_seat) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 5; i++) game_step(g);
    stage_air(g, SEA_X, 600000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    press(g, GAME_BTN_EJECT);
    for (int k = 0; k < 12; k++) game_step(g);
    ASSERT_EQ_INT(ac->f.gear & 0x0F, 6);                                   /* the hold, the seat flying */
    ASSERT_EQ_INT(e->rec[1].ac.f.cls, 0x30);
    const int allowance0 = game_crash_allowance(g);
    flight_set_position(ac, SEA_X, 20000, SEA_Z);                          /* the aircraft dives into the water */
    flight_set_attitude(ac, 2000, 2240, 0);
    ac->f.speed_target = ac->f.speed = 3000;
    int crashed = 0, hold = 0, restart = 0;
    for (int k = 0; k < 400 && !restart; k++) {
        game_step(g);
        if (!crashed && game_crash_count(g) == 1) {
            crashed = 1;
            ASSERT_EQ_INT(ac->f.gear & 0x0F, 0);                           /* the crash pass cleared the nibble */
            ASSERT_EQ_INT(ac->f.v4a850, 4);
        }
        if (crashed && game_crash_state(g, NULL) == GAME_CRASH_HOLD) hold = 1;
        else if (hold) restart = 1;                                        /* the hold is over: the restart pass */
        if (crashed && !restart) ASSERT_TRUE(e->rec[1].ac.f.w0 & 0x0040);   /* the seat keeps flying during the hold */
    }
    ASSERT_TRUE(crashed && hold && restart);
    ASSERT_EQ_INT(game_crash_allowance(g), allowance0 - 1);
    ASSERT_TRUE(!(e->rec[1].ac.f.w0 & 0x0040));                            /* the restart frees the seat */
    ASSERT_EQ_INT(ac->f.v4a994, 0);                                        /* and the viewed record is the player */
    ASSERT_EQ_INT(game_debug_screen(g)->request, 0);                       /* no request 2 */
    ASSERT_TRUE(ac->f.v4a850 != 0xF0);
    game_destroy(g);
}

/* Note 07 6.5 (V: followup-combat-batch-c/e.txt part seatshot): a seat that only carries the crashed bit is never converted and flies on; with the dying bit it is freed in the next pass, the
 * viewed record returns to the player in that pass, the player's W2 gets the release bit, and the nibble goes 5, 6 and is cleared to 0 two passes later with the release bit cleared in the same pass;
 * no request and no end code. */
TEST(a_seat_with_only_the_crashed_bit_flies_on_and_one_with_the_dying_bit_is_freed_and_releases_the_player) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 5; i++) game_step(g);
    stage_air(g, SEA_X, 600000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    game_step(g);
    press(g, GAME_BTN_EJECT);
    for (int k = 0; k < 14; k++) game_step(g);
    ASSERT_EQ_INT(e->rec[1].ac.f.cls, 0x30);
    ASSERT_EQ_INT(ac->f.v4a994, 1);
    e->rec[1].ac.f.w0 |= 0x0200;                                          /* the crashed bit alone */
    for (int k = 0; k < 30; k++) {
        game_step(g);
        ASSERT_TRUE(e->rec[1].ac.f.w0 & 0x0040);                          /* still valid */
        ASSERT_TRUE(!(e->rec[1].ac.f.w0 & 0x0400));                       /* never converted to dying */
        ASSERT_TRUE(e->rec[1].ac.f.w0 & 0x0200);                          /* and the crashed bit stays */
        ASSERT_EQ_INT(ac->f.v4a994, 1);
    }
    e->rec[1].ac.f.w0 = (uint16_t)((e->rec[1].ac.f.w0 & ~0x0200u) | 0x0400);   /* the dying bit */
    game_step(g);
    ASSERT_TRUE(!(e->rec[1].ac.f.w0 & 0x0040));                           /* freed in the next pass */
    ASSERT_EQ_INT(ac->f.v4a994, 0);                                       /* the viewed record returns to the player in the same pass */
    ASSERT_TRUE(ac->f.w2 & 0x0020);                                       /* the release */
    ASSERT_EQ_INT(ac->f.gear & 0x0F, 6);
    game_step(g);
    game_step(g);
    ASSERT_EQ_INT(ac->f.gear & 0x0F, 0);                                  /* cleared to 0 ... */
    ASSERT_TRUE(!(ac->f.w2 & 0x0020));                                    /* ... with the release bit cleared in the same pass */
    ASSERT_EQ_INT(game_debug_screen(g)->request, 0);
    ASSERT_EQ_INT(ac->f.v4a850, 0);
    game_destroy(g);
    /* the same with the nibble still counting (the capture): the nibble goes on 5, 6 and is cleared to 0 one pass later (the release is acted on at 6 only, note 07 6.4) */
    g = start_game();
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 5; i++) game_step(g);
    stage_air(g, SEA_X, 600000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    ac = game_debug_aircraft(g);
    e = (entities_t *)game_debug_entities(g);
    game_step(g);
    press(g, GAME_BTN_EJECT);
    for (int k = 0; k < 7; k++) game_step(g);
    ASSERT_EQ_INT(ac->f.gear & 0x0F, 4);
    e->rec[1].ac.f.w0 |= 0x0400;
    static const int want[3] = { 5, 6, 0 };
    for (int k = 0; k < 3; k++) { game_step(g); ASSERT_EQ_INT(ac->f.gear & 0x0F, want[k]); }
    ASSERT_TRUE(!(e->rec[1].ac.f.w0 & 0x0040));
    ASSERT_EQ_INT(ac->f.v4a994, 0);
    game_destroy(g);
}

/* Note 07 6.5 (V: followup-combat-eject-frames-ext-long.txt): in an external view the spawn does not switch the view; the viewed record stays the player for 39 passes after the spawn (the seat's
 * counter -19 up) and at the counter 0x14 the view switches: V:4A83D := V:4A8EB (0xFF whatever the level was: 2 here), the viewed record becomes the seat, the ring direction 4. */
TEST(in_an_external_view_the_seat_switches_the_view_at_its_counter_0x14_and_the_level_returns_to_0xff) {
    game_t *g = start_game();
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 5; i++) game_step(g);
    stage_air(g, SEA_X, 600000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    aircraft_t *ac = game_debug_aircraft(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    game_event_t kp = { GAME_EV_BUTTON, GAME_BTN_KEYPAD, GAME_KP_ENTER };
    game_input(g, &kp);
    for (int i = 0; i < 6; i++) game_step(g);
    press(g, GAME_BTN_LBRACKET); press(g, GAME_BTN_LBRACKET);              /* the level keys: -1, 1, 2 */
    game_step(g);
    ASSERT_EQ_INT(game_debug_view_level(g), 2);
    press(g, GAME_BTN_EJECT);
    int spawn = -1, sw = -1;
    for (int k = 0; k < 80 && sw < 0; k++) {
        game_step(g);
        if (spawn < 0 && (e->rec[1].ac.f.w0 & 0x0040)) spawn = k;
        if (spawn >= 0 && ac->f.v4a994 == 1) sw = k;
        else if (spawn >= 0) { ASSERT_EQ_INT(game_debug_view_level(g), 2); ASSERT_EQ_INT(ac->f.v4a994, 0); }   /* the player stays viewed, the level stays */
    }
    ASSERT_TRUE(spawn >= 0 && sw >= 0);
    ASSERT_EQ_INT(sw - spawn, 39);                                        /* the counter -19 at the spawn pass reaches 0x14 (20) 39 passes later */
    ASSERT_EQ_INT(e->rec[1].ac.f.counter, 0x14);
    ASSERT_EQ_INT(game_debug_view_level(g), -1);                          /* V:4A8EB */
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
        printf("test_eject: skipped (%s)\n", rc != 0 ? err : "no models");
        return 0;
    }
    free(probe);
    printf("test_eject:\n");
    RUN(the_player_with_a_nibble_that_touches_an_aircraft_is_removed_and_the_ejection_goes_on_to_the_seat);
    RUN(the_eject_key_runs_the_nibble_cycle_the_seat_spawns_at_3_the_view_switches_and_the_nibble_holds_at_6);
    RUN(the_seats_view_switch_in_an_external_view_resets_the_zoom_and_stores_the_panel_counter);
    RUN(the_seats_view_switch_in_the_cockpit_view_resets_the_zoom);
    RUN(a_second_press_while_the_nibble_is_not_zero_is_ignored_at_7_and_at_6);
    RUN(the_keys_of_the_first_chain_are_skipped_while_the_nibble_is_not_zero_and_after_an_ejection_press_of_the_same_drain);
    RUN(the_landed_seat_ends_the_flight_with_the_code_0xf0_once);
    RUN(an_aircraft_that_crashes_during_the_hold_clears_the_nibble_and_is_handed_over_as_for_any_crash_and_the_restart_frees_the_seat);
    RUN(a_seat_with_only_the_crashed_bit_flies_on_and_one_with_the_dying_bit_is_freed_and_releases_the_player);
    RUN(in_an_external_view_the_seat_switches_the_view_at_its_counter_0x14_and_the_level_returns_to_0xff);
    TEST_MAIN_END();
}
