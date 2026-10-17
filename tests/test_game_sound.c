/* tests/test_game_sound.c - the game's sound wiring (note 10; src/core/game.c, screen.c, sound.c) on the SYNTHETIC world: the invented object table of tests/sound_synth.h served as tables/sounds.bin, an invented
 * carrier model with the 58 vertices the ship record needs, and flight tables of zeros (the sound does not depend on the physics). Nothing from the disk is read, so these tests run in CI: they pin Review
 * Focus 1 of the milestone-6 plan (what stops which voices), the flight start's two latches, the order of the pass and the seconds tick, the beeps of note 10 7.4 and the start-up without the music. */
#include "test.h"
#include "world_synth.h"
#include "../src/core/screen.h"
#include "../src/core/sound.h"
#include "../src/core/lists.h"
#include "../src/core/weapons.h"
#include "../src/core/training.h"

/* ---- the fixture ------------------------------------------------------------------------------------------------------------------------------------ */
static int flight_read(void *user, const char *path, uint8_t **out, size_t *len) { synth_flight = 1; return synth_flight_read(user, path, out, len); }   /* the invented flight tables of world_synth.h */
static const game_loader_t LDF = { flight_read, fake_release, NULL };

enum { ALL = 0, NO_MUSIC = 1 };
static game_t *make_game(int variant) {
    char err[300];
    build_all();
    synth_invent_carrier();
    if (variant == NO_MUSIC) {   /* a disk whose music files are missing: the objects 0xD to 0x22 do not exist (2.3) */
        buf_t *b = file_buf("tables/sounds.bin");
        for (int i = 0xD; i <= 0x22; i++) { b->d[20 * i + 0] = 0xFF; b->d[20 * i + 1] = 0xFF; b->d[20 * i + 16] = 0xFF; }
    }
    game_t *g = game_create(&LDF, err, sizeof err);
    if (!g) printf("create failed: %s\n", err);
    return g;
}
static game_t *flight_game(void) {   /* the carrier deck, the cockpit view, the engine pair running */
    game_t *g = make_game(ALL);
    if (!g) return NULL;
    game_debug_start_flight(g, 3, 0);
    return g;
}

static void press(game_t *g, int code, int value) { game_event_t ev = { GAME_EV_BUTTON, code, value }; game_input(g, &ev); }
static void key(game_t *g, int ch) { game_event_t ev = { GAME_EV_KEY, 0, ch }; game_input(g, &ev); }
static int overflowed;
static void step(game_t *g) {
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    game_step(g);
    game_scene(g, s);
    if (s->audio_overflow) overflowed = 1;
    free(s);
}
static void steps(game_t *g, int n) { while (n-- > 0) step(g); }
static sound_t *snd(game_t *g) { return (sound_t *)game_debug_sound(g); }
static int mask(const sound_t *q) { return q->v[0].dma | (q->v[1].dma << 1) | (q->v[2].dma << 2) | (q->v[3].dma << 3); }
static int scene_count(game_t *g, int op, int voice) {   /* the commands of the last step */
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    game_scene(g, s);
    int n = 0;
    for (int i = 0; i < s->audio_count; i++) n += s->audio[i].op == op && s->audio[i].voice == voice;
    free(s);
    return n;
}

TEST(the_synthetic_flight_game_starts_with_the_cockpit_engine_pair) {
    game_t *g = flight_game();
    ASSERT_TRUE(g != NULL);
    ASSERT_TRUE(game_debug_aircraft(g) != NULL);                  /* the synthetic world flies: has_flight */
    steps(g, 60);
    sound_t *q = snd(g);
    ASSERT_EQ_INT(sound_slot(q, 0), 0);
    ASSERT_EQ_INT(sound_slot(q, 1), 1);
    ASSERT_EQ_INT(q->v[0].per, 764); ASSERT_EQ_INT(q->v[1].per, 766); ASSERT_EQ_INT(q->v[0].vol, 40);   /* throttle 9 in the cockpit: 784 - 18 - 2, volume 0x28 (note 10 5.3) */
    ASSERT_EQ_INT(overflowed, 0);
    game_destroy(g);
}

/* ---- the flight start through the screens ------------------------------------------------------------------------------------------------------------ */
/* the keys of the golden "takeoff-and-circuit": any key (tick 3), the menu's 2 (30), the location 1 (70), the aircraft 1 (130); one tick is one core step */
static void screen_keys(game_t *g, int tick) {
    if (tick == 3) key(g, 120);
    else if (tick == 30) key(g, '2');
    else if (tick == 70) key(g, '1');
    else if (tick == 130) key(g, '1');
}
/* the register writes of the voices 0 and 1 of one step: the engine objects latched, the periods and volumes (without the stops) */
typedef struct { int latch0, latch1, per0, per1, vol0, vol1; } vstep_t;
static vstep_t voices01(game_t *g) {
    vstep_t r = { -1, -1, -1, -1, -1, -1 };
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    game_scene(g, s);
    for (int i = 0; i < s->audio_count; i++) {
        const game_audio_cmd_t *c = &s->audio[i];
        if (c->voice > 1) continue;
        if (c->op == GAME_AUD_LATCH) { if (c->voice == 0) r.latch0 = c->object; else r.latch1 = c->object; }
        if (c->op == GAME_AUD_PERIOD && c->value != 124) { if (c->voice == 0) r.per0 = c->value; else r.per1 = c->value; }
        if (c->op == GAME_AUD_VOLUME && c->value != 0) { if (c->voice == 0) r.vol0 = c->value; else r.vol1 = c->value; }
    }
    free(s);
    return r;
}

TEST(the_cockpit_flight_start_latches_the_external_pair_at_776_and_volume_12_then_the_cockpit_pair_at_764_and_volume_40) {
    game_t *g = make_game(ALL);
    ASSERT_TRUE(g != NULL);
    int first = -1, second = -1;
    vstep_t a = { 0 }, b = { 0 };
    int wind0 = 0, wind1 = 0;
    for (int tick = 1; tick <= 400 && second < 0; tick++) {
        screen_keys(g, tick);
        step(g);
        const vstep_t v = voices01(g);
        if (first < 0 && (v.latch0 == 2 || v.latch0 == 0)) {
            first = tick; a = v;
            wind0 = snd(g)->o[8].period >> 16; wind1 = snd(g)->o[9].period >> 16;   /* the wind objects keep the periods of their start: no glide moved them */
        } else if (first >= 0 && v.latch0 >= 0) { second = tick; b = v; }
    }
    ASSERT_TRUE(first > 0);
    ASSERT_EQ_INT(a.latch0, 2); ASSERT_EQ_INT(a.latch1, 3);                    /* the pending notice 0xFE is played by the first ungated pass with the approach's outside view (note 10 5.7 item 10) */
    ASSERT_EQ_INT(a.per0, 776); ASSERT_EQ_INT(a.per1, 778);                    /* 800 - 18 - 4 - 2 */
    ASSERT_EQ_INT(a.vol0, 12); ASSERT_EQ_INT(a.vol1, 12);                      /* x = 36, direction 0: 22, level 1: 20, no afterburner: 15, outside: 12 */
    ASSERT_EQ_INT(second, first + 1);                                          /* the pass after the approach's switch to the cockpit view */
    ASSERT_EQ_INT(b.latch0, 0); ASSERT_EQ_INT(b.latch1, 1);
    ASSERT_EQ_INT(b.per0, 764); ASSERT_EQ_INT(b.per1, 766);
    ASSERT_EQ_INT(b.vol0, 40); ASSERT_EQ_INT(b.vol1, 40);
    ASSERT_TRUE(wind0 >= 0x168 && wind0 < 0x168 + 64 && wind1 >= 0x168 && wind1 < 0x168 + 64);   /* the wind was never glided toward the engine's period */
    ASSERT_EQ_INT(overflowed, 0);
    game_destroy(g);
}

TEST(the_acting_pass_of_the_wait_state_beeps_with_volume_2_for_nine_blanks) {
    game_t *g = make_game(ALL);
    ASSERT_TRUE(g != NULL);
    int found = 0;
    for (int tick = 1; tick <= 600 && !found; tick++) {
        screen_keys(g, tick);
        const screen_state_t before = game_debug_screen(g)->state;
        step(g);
        if (before == SCR_WAIT && game_debug_screen(g)->state == SCR_FLIGHT) {   /* 0x12EDA: the HUD goes on and the start sound 0x12EEC sounds */
            found = 1;
            const sound_t *q = snd(g);
            ASSERT_EQ_INT(q->script[SOUND_SCRIPT_TONE][1][1], 1);     /* p7: one inner repeat */
            ASSERT_EQ_INT(q->script[SOUND_SCRIPT_TONE][0][1], 2);     /* p8 = n = 2 outer repeats */
            ASSERT_EQ_INT(q->script[SOUND_SCRIPT_TONE][3][1], 2 << 16);   /* the volume v = 2 (the entry 0x37C44) */
            ASSERT_EQ_INT(sound_slot(q, 3), 4);
        }
    }
    ASSERT_TRUE(found);
    game_destroy(g);
}

/* ---- the order of the pass and the seconds tick (note 10 5.1 item 7) ---------------------------------------------------------------------------------- */
TEST(a_gear_key_is_seen_by_the_sound_routine_of_its_pass_before_the_seconds_tick_can_clear_its_bit) {
    game_t *g = flight_game();
    ASSERT_TRUE(g != NULL);
    steps(g, 6);
    sound_t *q = snd(g);
    ASSERT_TRUE(sound_slot(q, 2) == SND_NONE);
    for (int sec = 0; sec < 8; sec++) {   /* the key always meets a tick in its own pass, as often as it comes: the sound starts every time */
        game_clock_second(g);
        press(g, GAME_BTN_GEAR, 1);
        step(g);
        ASSERT_EQ_INT(sound_slot(q, 2), 0xB);                    /* 0x14B1C: the gear sound at volume 10 in the cockpit */
        ASSERT_EQ_INT(q->gear_cd, 2);                            /* 0x83 -> the routine clears bit 7 (3) -> the tick of the same pass counts it down (2) */
        sound_stop_all(q);
        q->gear_cd = 0;
        steps(g, 2);
    }
    ASSERT_EQ_INT(overflowed, 0);
    game_destroy(g);
}

TEST(the_cue_cool_down_counts_in_the_pass_after_the_cue_player_set_it) {
    game_t *g = flight_game();
    ASSERT_TRUE(g != NULL);
    steps(g, 6);
    sound_t *q = snd(g);
    game_debug_aircraft(g)->f.v4ac0c |= 0x01;                    /* a hostile missile locked: the cue 0x01 sets the cool-down 3 */
    game_clock_second(g);
    step(g);
    ASSERT_EQ_INT(q->cooldown, 2);                               /* the player set 3, the tick of the same pass (last) made it 2 */
    ASSERT_EQ_INT(overflowed, 0);
    game_destroy(g);
}

/* ---- V:4ABFA and the fixed camera (note 10 5.3, 5.7 item 9) ------------------------------------------------------------------------------------------------ */
TEST(the_cockpit_view_stores_0x7fff_in_v4abfa_and_the_fixed_camera_fades_the_engine_with_the_viewed_records_distance) {
    game_t *g = flight_game();
    ASSERT_TRUE(g != NULL);
    steps(g, 30);
    lists_t *l = world_lists((world_t *)game_debug_world(g));
    ASSERT_EQ_INT(l->v4abfa, 0x7FFF);                            /* the cockpit walk: 0x1FE9C */
    press(g, GAME_BTN_KEYPAD, GAME_KP_ENTER);                    /* the outside view */
    steps(g, 6);
    press(g, GAME_BTN_FIXED_CAMERA, 1);                          /* Shift+KP-Enter: the fixed camera clears V:4A86D (0x1E48E) */
    steps(g, 6);
    const int dist = (int16_t)l->v4abfa;
    ASSERT_TRUE(dist > 0 && dist != 0x7FFF);                     /* the refreshed distance word of the viewed record */
    flight_state_t *f = &game_debug_aircraft(g)->f;
    ASSERT_EQ_INT(f->v4a83d != 0, 1);
    f->v4a968 = 0xFE;                                            /* the engine block replays the external pair */
    step(g);
    sound_t *q = snd(g);
    int x = 63 - (120 - f->throttle) / 4;                        /* 5.3 step 2, a0 above 8 */
    x = 32 * x / dist;                                           /* step 4 with V:4A86D clear and V:4ABFA non-zero */
    if (x > 0x54) x = 0x54;
    if (!(f->w2 & 8)) x -= x >> 2;                               /* afterburner off */
    if (x < 1) x = 1;
    const int p = x - (x >> 2);                                  /* outside: P = P0 less a quarter */
    ASSERT_EQ_INT(sound_slot(q, 0), 2);
    ASSERT_EQ_INT(q->v[0].vol, p);
    ASSERT_EQ_INT(overflowed, 0);
    game_destroy(g);
}

/* ---- the beeps of note 10 7.4 --------------------------------------------------------------------------------------------------------------------------- */
TEST(the_weapon_cycle_return_beeps_first_and_always_also_with_an_ejection_nibble_set) {
    for (int nibble = 0; nibble <= 5; nibble += 5) {
        game_t *g = flight_game();
        ASSERT_TRUE(g != NULL);
        steps(g, 6);
        sound_t *q = snd(g);
        aircraft_t *ac = game_debug_aircraft(g);
        ac->f.gear = (uint8_t)((ac->f.gear & 0xF0) | nibble);
        ac->f.v4ac0c = 0;
        sound_stop_voice(q, 3);
        press(g, GAME_BTN_RETURN, 1);
        step(g);
        ASSERT_EQ_INT(sound_slot(q, 3), 4);                      /* 0x1E8E6: 0x37C4A first, whether or not the cycle changes the weapon */
        ASSERT_EQ_INT(q->script[SOUND_SCRIPT_TONE][3][1], 4 << 16);   /* the switch beep: v = 4 (the typing flag is 0), n = 2 */
        ASSERT_EQ_INT(q->script[SOUND_SCRIPT_TONE][0][1], 2);
        game_destroy(g);
    }
    ASSERT_EQ_INT(overflowed, 0);
}

TEST(the_beep_is_silent_in_an_outside_view) {
    game_t *g = flight_game();
    ASSERT_TRUE(g != NULL);
    steps(g, 6);
    press(g, GAME_BTN_KEYPAD, GAME_KP_ENTER);
    steps(g, 8);
    sound_t *q = snd(g);
    sound_stop_voice(q, 3);
    press(g, GAME_BTN_RETURN, 1);
    step(g);
    ASSERT_EQ_INT(sound_slot(q, 3), SND_NONE);                   /* 0x37C4A: V:4A83D non-zero */
    game_destroy(g);
}

TEST(the_switch_beeps_of_every_key_are_silent_in_the_map_view_through_the_view_byte_and_sound_in_the_cockpit) {
    const int keys[][2] = { { GAME_BTN_ECM, 0 }, { GAME_BTN_RADAR_RANGE, 0 }, { GAME_BTN_TARGET, 0 }, { GAME_BTN_HOOK, 0 }, { GAME_BTN_BRAKE, 0 }, { GAME_BTN_RETURN, 0 } };
    for (int map = 0; map <= 1; map++) {
        for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
            game_t *g = flight_game();
            ASSERT_TRUE(g != NULL);
            steps(g, 6);
            if (map) { press(g, GAME_BTN_TOWER, 1); steps(g, 12); }   /* M: the map view (note 10 7.4: V:4A83D is non-zero there) */
            ASSERT_TRUE(game_debug_view_level(g) != 0 || !map);
            sound_t *q = snd(g);
            game_debug_aircraft(g)->f.v4ac0c = 0;
            sound_stop_voice(q, 3);
            press(g, keys[i][0], 1);
            step(g);
            ASSERT_EQ_INT(sound_slot(q, 3) == 4, !map);
            game_destroy(g);
        }
    }
    ASSERT_EQ_INT(overflowed, 0);
}

/* ---- the start-up (note 10 7.1) ---------------------------------------------------------------------------------------------------------------------------- */
TEST(the_start_up_with_the_music_plays_the_first_object_of_the_chain_with_the_typing_byte_2) {
    game_t *g = make_game(ALL);
    ASSERT_TRUE(g != NULL);
    const sound_t *q = snd(g);
    ASSERT_EQ_INT(sound_flag(q), 2);
    ASSERT_EQ_INT(q->v[0].cur, 0xD); ASSERT_EQ_INT(q->v[1].cur, 0xE);   /* 0x19CE6(0x32): the first objects of the music chain, at volume 63 */
    ASSERT_EQ_INT(q->v[0].vol, 63);
    game_destroy(g);
}

TEST(a_disk_without_the_music_starts_the_mission_tone_pair_at_volume_15_and_leaves_the_typing_byte_0) {
    game_t *g = make_game(NO_MUSIC);
    ASSERT_TRUE(g != NULL);
    const sound_t *q = snd(g);
    ASSERT_EQ_INT(q->en13 & 0x80, 0);
    ASSERT_EQ_INT(sound_flag(q), 0);                             /* 0x19D7A leaves the typing byte alone */
    ASSERT_EQ_INT(q->v[0].cur, 0x23); ASSERT_EQ_INT(q->v[1].cur, 0x24);   /* not 0x19CE6(0x32): the text201 pair */
    ASSERT_EQ_INT(q->v[0].vol, 15); ASSERT_EQ_INT(q->v[1].vol, 15);
    ASSERT_EQ_INT(q->v[0].per, 179); ASSERT_EQ_INT(q->v[1].per, 181);
    steps(g, 30);
    ASSERT_EQ_INT(sound_flag(q), 0);                             /* nothing set the typing byte to 2: the typed text of the screens clicks (tests/test_screen.c) */
    ASSERT_EQ_INT(q->v[0].cur, 0x23);                            /* and the pair plays on */
    ASSERT_EQ_INT(overflowed, 0);
    game_destroy(g);
}

/* ---- Review Focus 1 (milestone 6 plan): what stops which voices, with the engine pair, the gear sound and a cue tone playing (note 10 4.1, 7.3) ------------------------------------ */
/* a cockpit flight with the engine pair on voices 0 and 1, the gear sound on voice 2 and the alert tone (the cue 0x04, for ever) on voice 3 */
static game_t *sounding_game(void) {
    game_t *g = flight_game();
    if (!g) return NULL;
    steps(g, 10);
    press(g, GAME_BTN_GEAR, 1);
    game_debug_aircraft(g)->f.v4ac0c |= 0x04;
    steps(g, 3);
    return g;
}

TEST(a_view_switch_stops_voice_3_only_and_the_notice_restarts_the_engine_with_the_external_pair) {
    game_t *g = sounding_game();
    ASSERT_TRUE(g != NULL);
    sound_t *q = snd(g);
    ASSERT_EQ_INT(mask(q), 15);                                  /* the engine pair, the gear sound and the tone all play */
    ASSERT_EQ_INT(sound_slot(q, 0), 0);                          /* the cockpit pair */
    press(g, GAME_BTN_KEYPAD, GAME_KP_ENTER);
    step(g);
    ASSERT_TRUE(scene_count(g, GAME_AUD_DMA_OFF, 3) > 0);        /* 0x14282: voice 3 stopped by the view setter */
    ASSERT_EQ_INT(mask(q), 7);                                   /* voices 0, 1 and 2 are not touched by the switch itself */
    ASSERT_EQ_INT(sound_slot(q, 0), 2); ASSERT_EQ_INT(sound_slot(q, 1), 3);   /* the notice 0xFE restarts the engine in the same pass: the external pair */
    ASSERT_EQ_INT(overflowed, 0);
    game_destroy(g);
}

TEST(m_stops_all_four_voices_and_the_gate_keeps_the_routine_out_until_the_map_is_left_and_voice_3_stays_off) {
    game_t *g = sounding_game();
    ASSERT_TRUE(g != NULL);
    sound_t *q = snd(g);
    press(g, GAME_BTN_TOWER, 1);
    step(g);
    ASSERT_EQ_INT(mask(q), 0);                                   /* 0x1ECE2: all four voices stop when the map goes on */
    for (int v = 0; v < 4; v++) ASSERT_TRUE(scene_count(g, GAME_AUD_DMA_OFF, v) > 0);
    for (int i = 0; i < 12; i++) { step(g); ASSERT_EQ_INT(mask(q), 0); }   /* V:4A865 keeps the per-pass routine (the engine, the cue player) out */
    press(g, GAME_BTN_TOWER, 1);
    steps(g, 3);
    ASSERT_TRUE(mask(q) & 3);                                    /* the map left: the notice restarts the engine */
    ASSERT_EQ_INT(mask(q) & 8, 0);                               /* M off stops voice 3 and the tone does not come back */
    ASSERT_EQ_INT(overflowed, 0);
    game_destroy(g);
}

TEST(a_pause_stops_all_four_voices_and_the_unpause_restarts_the_engine_but_not_the_tone) {
    game_t *g = sounding_game();
    ASSERT_TRUE(g != NULL);
    sound_t *q = snd(g);
    press(g, GAME_BTN_PAUSE, 1);
    step(g);
    ASSERT_EQ_INT(mask(q), 0);                                   /* 0x1EDC0 */
    for (int i = 0; i < 6; i++) { step(g); ASSERT_EQ_INT(mask(q), 0); }
    press(g, GAME_BTN_PAUSE, 1);
    steps(g, 3);
    ASSERT_TRUE(mask(q) & 3);                                    /* the notice 0xFE: the next pass restarts the engine from the throttle */
    ASSERT_EQ_INT(mask(q) & 8, 0);                               /* the tone does not come back */
    ASSERT_EQ_INT(overflowed, 0);
    game_destroy(g);
}

TEST(a_cockpit_crash_stops_all_four_voices_and_plays_the_crash_noise_an_outside_crash_does_neither) {
    game_t *g = sounding_game();
    ASSERT_TRUE(g != NULL);
    sound_t *q = snd(g);
    aircraft_t *ac = game_debug_aircraft(g);
    ac->f.w0 |= 0x0200;                                          /* the world update raises the crash bit; the hand-over of the next pass */
    step(g);
    ASSERT_EQ_INT(mask(q), 4);                                   /* 0x115F6 then 0x1A0DC(0x3F, 0x78): only the noise on voice 2 plays */
    ASSERT_EQ_INT(sound_slot(q, 2), 6);
    step(g);
    ASSERT_TRUE(q->v[2].vol >= 50 && q->v[2].vol <= 63);          /* the crash noise: 63 at the start of its 120-blank fade, in the cockpit view */
    game_destroy(g);
    g = sounding_game();
    ASSERT_TRUE(g != NULL);
    q = snd(g);
    ac = game_debug_aircraft(g);
    press(g, GAME_BTN_KEYPAD, GAME_KP_ENTER);                    /* an outside view: the hand-over stops nothing and plays nothing */
    steps(g, 2);
    const int before = mask(q);
    const int slot2 = sound_slot(q, 2);
    ac->f.w0 |= 0x0200;
    step(g);
    ASSERT_EQ_INT(mask(q) & 7, before & 7);
    ASSERT_EQ_INT(sound_slot(q, 2), slot2);
    ASSERT_EQ_INT(overflowed, 0);
    game_destroy(g);
}

/* the voice-3 volumes written over a run of the ejection from the given view: the seat tone (200, volume 14, note 10 8.3) and the cue 0x40 (volume 16) are told apart by it */
static int eject_run(int outside, int *bang_amp) {
    game_t *g = flight_game();
    if (!g) return -1;
    steps(g, 10);
    if (outside) { press(g, GAME_BTN_KEYPAD, GAME_KP_ENTER); steps(g, 8); }
    press(g, GAME_BTN_EJECT, 1);
    int seat_tone = 0, seat_vol = 0, seat_per = 0, bang = 0;      /* the seat tone (note 10 8.3): period 200, volume 14, on voice 3 */
    sound_t *q = snd(g);
    for (int i = 0; i < 400 && !bang; i++) {
        step(g);
        game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
        game_scene(g, s);
        for (int k = 0; k < s->audio_count; k++) {
            if (s->audio[k].op == GAME_AUD_VOLUME && s->audio[k].voice == 3 && s->audio[k].value == 14) seat_vol = 1;
            if (s->audio[k].op == GAME_AUD_PERIOD && s->audio[k].voice == 3 && s->audio[k].value == 200) seat_per = 1;
        }
        seat_tone = seat_vol && seat_per;
        free(s);
        if (q->v[2].dma && q->v[2].cur == 6) bang = 1;
    }
    *bang_amp = bang ? q->script[SOUND_SCRIPT_NOISE][0][1] >> 16 : -1;
    game_destroy(g);
    return seat_tone;
}
TEST(the_ejection_bang_is_31_in_the_cockpit_view_and_63_outside_and_the_seat_tone_plays_in_the_cockpit_only) {
    int amp_in = 0, amp_out = 0;
    const int tone_in = eject_run(0, &amp_in);
    const int tone_out = eject_run(1, &amp_out);
    ASSERT_EQ_INT(amp_in, 31);                                   /* 0x1A0DC(V:4A922 >> 1, V:4A921): the seat's 0x3F halved (note 10 8.2) */
    ASSERT_EQ_INT(amp_out, 63);                                  /* 0x1A0DC(V:4A922, V:4A921) */
    ASSERT_EQ_INT(tone_in, 1);                                   /* 0x26FFE at the ejection nibble 9 with the cockpit view: object 4, period 200, volume 14 */
    ASSERT_EQ_INT(tone_out, 0);                                  /* not in an outside view */
    ASSERT_EQ_INT(overflowed, 0);
}

/* The ejection with the gear sound (voice 2) and the alert tone (voice 3) playing: the starts of the seat tone and of the bang stop the voice first (note 10 4.1: the player stops the voice, then
 * loads it), voices 0 and 1 are not touched, and the stops come in the order of the starts: the tone's voice at the nibble 9 step, the bang's voice 3 steps later. */
typedef struct { int step, k, op, voice; } cmdpos_t;
static int find_cmd(const game_scene_t *s, int op, int voice) {
    for (int k = 0; k < s->audio_count; k++) if (s->audio[k].op == op && s->audio[k].voice == voice) return k;
    return -1;
}
TEST(the_ejection_with_the_gear_sound_and_a_cue_playing_stops_voice_3_at_the_seat_tone_then_voice_2_at_the_bang_and_never_voices_0_and_1) {
    game_t *g = flight_game();
    ASSERT_TRUE(g != NULL);
    steps(g, 10);
    game_debug_aircraft(g)->f.v4ac0c |= 0x04;                    /* the alert tone, for ever */
    steps(g, 3);
    ASSERT_EQ_INT(mask(snd(g)) & 8, 8);
    press(g, GAME_BTN_EJECT, 1);
    int off3_step = -1, off2_step = -1, tone_step = -1, bang_step = -1, off01 = 0;
    for (int i = 0; i < 40 && bang_step < 0; i++) {
        if (i == 1) sound_gear_key(snd(g));                  /* the gear sound plays when the seat fires (started on the sound module, as the gear key does) */
        game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
        game_step(g);
        game_scene(g, s);
        for (int k = 0; k < s->audio_count; k++) {
            const game_audio_cmd_t *c = &s->audio[k];
            if (c->op == GAME_AUD_DMA_OFF && c->voice < 2) off01++;
            if (c->op == GAME_AUD_PERIOD && c->voice == 3 && c->value == 200 && tone_step < 0) {   /* the seat tone: period 200 */
                tone_step = i;
                const int off = find_cmd(s, GAME_AUD_DMA_OFF, 3);
                ASSERT_TRUE(off >= 0 && off < k);                 /* the alert is stopped (period 0x7C, DMA off) before the seat tone's period is written */
                ASSERT_EQ_INT(s->audio[off - 1].op, GAME_AUD_PERIOD); ASSERT_EQ_INT(s->audio[off - 1].value, 0x7C);
                off3_step = i;
            }
            if (c->op == GAME_AUD_LATCH && c->voice == 2 && c->object == 6 && bang_step < 0) {     /* the bang: object 6 on voice 2 */
                bang_step = i;
                const int off = find_cmd(s, GAME_AUD_DMA_OFF, 2);
                ASSERT_TRUE(off >= 0 && off < k);                  /* the gear sound is stopped before the bang is loaded */
                off2_step = i;
            }
        }
        free(s);
    }
    ASSERT_TRUE(tone_step >= 0 && bang_step >= 0);
    ASSERT_TRUE(off3_step < off2_step);                          /* voice 3's stop comes first */
    ASSERT_EQ_INT(off01, 0);                                     /* the engine pair is never stopped by an ejection */
    ASSERT_EQ_INT(overflowed, 0);
    game_destroy(g);
}

/* The pod release (note 10 8.2, 07 13.4): SHIFT-F in mode 6 with a free record among 1 to 3 sets the nibble and calls 0x1A0DC(0x1C, 0x30): the noise object 6 on voice 2, started at volume 28. */
static game_t *pod_game(void) {
    game_t *g = flight_game();
    if (!g) return NULL;
    steps(g, 5);
    return g;
}
TEST(a_pod_release_starts_the_noise_object_6_on_voice_2_with_volume_28_in_mode_6_only) {
    game_t *g = pod_game();
    ASSERT_TRUE(g != NULL);
    sound_t *q = snd(g);
    press(g, GAME_BTN_RESCUE, 1);
    step(g);
    ASSERT_EQ_INT(q->v[2].dma, 0);                               /* not in the other modes: the key does nothing (0x1EE4E tests the mode byte 6) */
    game_destroy(g);
    g = pod_game();
    ASSERT_TRUE(g != NULL);
    q = snd(g);
    ((screen_t *)game_debug_screen(g))->mode_byte = 6;           /* the mission F4 (a test hook: the debug start is the free flight) */
    press(g, GAME_BTN_RESCUE, 1);
    ASSERT_EQ_INT(game_debug_aircraft(g)->f.gear & 0x0F, 0x0F);   /* the ejection nibble, set by the key before the noise */
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    game_step(g);
    game_scene(g, s);
    ASSERT_TRUE(find_cmd(s, GAME_AUD_LATCH, 2) >= 0);
    int vol = -1;
    for (int k = 0; k < s->audio_count; k++) if (s->audio[k].op == GAME_AUD_VOLUME && s->audio[k].voice == 2) { vol = s->audio[k].value; break; }
    ASSERT_EQ_INT(vol, 28);
    ASSERT_EQ_INT(sound_slot(q, 2), 6);
    free(s);
    press(g, GAME_BTN_RESCUE, 1);                                /* a second press: the nibble is not 0 and the latch is set, nothing sounds (10 8.2) */
    s = (game_scene_t *)calloc(1, sizeof *s);
    game_step(g);
    game_scene(g, s);
    ASSERT_EQ_INT(find_cmd(s, GAME_AUD_LATCH, 2), -1);
    free(s);
    game_destroy(g);
}

/* Note 10 7.4a: the click of T and J reads the view byte V:4A83D when the key is handled (the poll drains all pending key events in one pass, so several keys share a pass). The weapons module's
 * copy of the byte is refreshed by the gun pass only; the test plants a stale copy and the click must follow the live byte. M and KP-ENTER store nothing in the byte in their handlers (the state
 * routine of the pass does), so a T of the same drain still finds the cockpit. */
TEST(t_clicks_by_the_view_byte_of_the_moment_not_by_the_gun_pass_copy_of_the_pass_before) {
    game_t *g = flight_game();
    ASSERT_TRUE(g != NULL);
    steps(g, 6);
    sound_t *q = snd(g);
    game_debug_weapons(g)->ext_view = 1;                         /* a stale copy: the cockpit, but the pass before saw an outside view */
    sound_stop_voice(q, 3);
    press(g, GAME_BTN_TARGET, 1);
    ASSERT_EQ_INT(sound_slot(q, 3), 4);                          /* V:4A83D is 0 now: the click sounds */
    game_destroy(g);
    g = flight_game();
    ASSERT_TRUE(g != NULL);
    steps(g, 6);
    q = snd(g);
    press(g, GAME_BTN_KEYPAD, GAME_KP_ENTER);
    steps(g, 8);
    game_debug_weapons(g)->ext_view = 0;                         /* a stale copy: an outside view now, but the pass before saw the cockpit */
    sound_stop_voice(q, 3);
    press(g, GAME_BTN_TARGET, 1);
    step(g);
    ASSERT_EQ_INT(sound_slot(q, 3), SND_NONE);                   /* V:4A83D is non-zero: silent */
    game_destroy(g);
    g = flight_game();
    ASSERT_TRUE(g != NULL);
    steps(g, 6);
    q = snd(g);
    sound_stop_voice(q, 3);
    press(g, GAME_BTN_TOWER, 1);                                 /* M: the request only, V:4A83D stays 0 until the state routine */
    press(g, GAME_BTN_TARGET, 1);
    ASSERT_EQ_INT(sound_slot(q, 3), 4);                          /* the click is started in the handler (the pass then stops the voices for the map, 10 4.1) */
    game_destroy(g);
    g = flight_game();
    ASSERT_TRUE(g != NULL);
    steps(g, 6);
    q = snd(g);
    press(g, GAME_BTN_KEYPAD, GAME_KP_ENTER);
    steps(g, 8);
    game_debug_weapons(g)->ext_view = 0;
    sound_stop_voice(q, 3);
    press(g, GAME_BTN_ECM, 1);                                   /* J takes the same live byte through the weapons module */
    step(g);
    ASSERT_EQ_INT(sound_slot(q, 3), SND_NONE);
    game_destroy(g);
    ASSERT_EQ_INT(overflowed, 0);
}

/* Note 07 6.4a: the press of E sets the nibble, but while the gear byte's retract progress is changing the step stores the byte with the nibble 0 (note 05 17.18 c): the lamp toggles, the cycle does not start. */
TEST(an_ejection_press_during_the_gear_travel_toggles_the_lamp_and_the_progress_change_clears_the_nibble) {
    game_t *g = flight_game();
    ASSERT_TRUE(g != NULL);
    steps(g, 6);
    aircraft_t *ac = game_debug_aircraft(g);
    ac->f.gear = 0xA0;                                           /* the gear command set, the progress 2 of 6: it changes every pass */
    press(g, GAME_BTN_EJECT, 1);
    step(g);
    ASSERT_EQ_INT(ac->f.gear & 0x0F, 0);
    ASSERT_EQ_INT(ac->f.gear & 0xF0, 0xB0);
    game_destroy(g);
}

/* ---- the beeps of the trainings (note 10 7.4: 0x1E8C2 the RETURN, 0x1ED88 the S key) on the invented world: the invented training table (synth_training_table in world_synth.h) lets the menu's item 3 start
 * part 1 here, so the test runs in CI. The same test with the extracted assets is tests/test_game.c (return_in_training_part_1_beeps_and_the_s_key_does_not). ---- */
static game_t *synthetic_training_part_1(void) {
    char err[300];
    build_all();
    synth_invent_carrier();
    synth_mission_tables();
    synth_training_table();
    synth_flight = 1;
    synth_missions = 1;
    game_t *g = game_create(&LDF, err, sizeof err);
    if (!g) { printf("create failed: %s\n", err); return NULL; }
    game_set_logdisk_result(g, 0);
    for (int tick = 1; tick <= 3000 && !(game_debug_screen(g)->state == SCR_FLIGHT && game_debug_trainer(g)->part == 1); tick++) {
        if (tick == 3) key(g, 120);
        else if (tick == 30) key(g, '3');
        step(g);
    }
    if (!(game_debug_screen(g)->state == SCR_FLIGHT && game_debug_trainer(g)->part == 1)) { game_destroy(g); return NULL; }
    steps(g, 20);
    return g;
}
TEST(return_in_training_part_1_beeps_and_the_s_key_does_not_on_the_invented_world) {
    game_t *g = synthetic_training_part_1();
    ASSERT_TRUE(g != NULL);
    sound_t *q = snd(g);
    press(g, GAME_BTN_KEYPAD, GAME_KP_ENTER);                     /* part 1 flies the external chase view, in which the beep is silent (0x37C4A tests V:4A83D): the cockpit */
    steps(g, 12);
    sound_stop_voice(q, 3);
    press(g, GAME_BTN_SMOKE, 1);                                  /* 0x1ED88 acts in mode 0x7D only */
    step(g);
    ASSERT_EQ_INT(sound_slot(q, 3), SND_NONE);
    press(g, GAME_BTN_RETURN, 1);                                 /* 0x1E8C2: mode 2 beeps, then sets the request */
    step(g);
    ASSERT_EQ_INT(sound_slot(q, 3), 4);
    game_destroy(g);
}

/* Note 10 5.1 item 2 and 5.7 item 12 [V: sound7-seat]: the gear wrapper's camera test (V:4AA6E = V:4A996) is true for every camera in the sound pass, the ejection seat's included: the gear sound plays
 * with the camera on the seat (the cockpit-view rule: volume 10, period 700 falling by 7 to 595). */
TEST(the_gear_sound_plays_with_the_camera_on_the_ejection_seat) {
    game_t *g = flight_game();
    ASSERT_TRUE(g != NULL);
    steps(g, 10);
    press(g, GAME_BTN_EJECT, 1);
    int seat = 0;
    for (int i = 0; i < 40 && !seat; i++) { step(g); seat = entities_viewed(game_debug_entities(g)) != 0; }
    ASSERT_TRUE(seat);
    sound_t *q = snd(g);
    sound_stop_voice(q, 2);
    q->gear_cd = 0x83;                                           /* the countdown with its start bit, as the gear key leaves it */
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    game_step(g);
    game_scene(g, s);
    int vol = -1, per = -1;
    for (int k = 0; k < s->audio_count; k++) {
        if (s->audio[k].voice != 2) continue;
        if (s->audio[k].op == GAME_AUD_VOLUME) vol = s->audio[k].value;   /* the last volume of the voice in the step */
        if (s->audio[k].op == GAME_AUD_PERIOD && per < 0 && vol == 10) per = s->audio[k].value;   /* the first period after the volume: the glide's start */
    }
    free(s);
    ASSERT_EQ_INT(sound_slot(q, 2), 0xB);
    ASSERT_EQ_INT(vol, 10);
    ASSERT_EQ_INT(per, 700);
    game_destroy(g);
}

/* Note 10 7.3 [V: sound7-pause-frames]: the engine pair returns 4 blanks after the pause byte clears (the first flight pass after the key), with no click: the DMA of voice 0 and voice 1 comes on at blank 4 of the
 * first step after the unpause key. */
TEST(the_engine_returns_4_blanks_after_the_unpause_key) {
    game_t *g = sounding_game();
    ASSERT_TRUE(g != NULL);
    press(g, GAME_BTN_PAUSE, 1);
    steps(g, 8);
    press(g, GAME_BTN_PAUSE, 1);
    int first[2] = { -1, -1 }, clicks = 0;
    for (int i = 0; i < 2; i++) {
        game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
        game_step(g);
        game_scene(g, s);
        for (int k = 0; k < s->audio_count; k++) {
            if (s->audio[k].op == GAME_AUD_DMA_ON && s->audio[k].voice < 2 && i == 0 && first[s->audio[k].voice] < 0) first[s->audio[k].voice] = s->audio[k].blank;
            if (s->audio[k].op == GAME_AUD_LATCH && s->audio[k].voice == 3) clicks++;
        }
        free(s);
    }
    ASSERT_EQ_INT(first[0], 4);
    ASSERT_EQ_INT(first[1], 4);
    ASSERT_EQ_INT(clicks, 0);
    game_destroy(g);
}

/* Note 06 (head views: "The hold timer does not run in a pause" [V: views-m3-fix S4b]) and note 04 (the blackout accumulator): the per-second counters tied to V:4ABAA are in the clock routine, inside the
 * drawn frame's limiter, which a pause does not reach: the accumulator does not move in a pause, however long it lasts. */
TEST(the_blackout_accumulator_does_not_count_seconds_in_a_pause) {
    game_t *g = flight_game();
    ASSERT_TRUE(g != NULL);
    aircraft_t *ac = game_debug_aircraft(g);
    steps(g, 10);
    for (int i = 0; i < 2 * 8; i++) { ac->f.rate_p = -461; ac->f.v4a9fe = -102; step(g); }
    const int running = game_debug_strain_acc(g);
    ASSERT_TRUE(running >= 0);                                   /* two seconds with the latch set raise it from -1 */
    press(g, GAME_BTN_PAUSE, 1);
    step(g);
    for (int i = 0; i < 6 * 8; i++) { ac->f.rate_p = -461; ac->f.v4a9fe = -102; step(g); }
    ASSERT_EQ_INT(game_debug_strain_acc(g), running);            /* six seconds paused: unchanged */
    game_destroy(g);
}

int main(void) {
    RUN(t_clicks_by_the_view_byte_of_the_moment_not_by_the_gun_pass_copy_of_the_pass_before);
    RUN(an_ejection_press_during_the_gear_travel_toggles_the_lamp_and_the_progress_change_clears_the_nibble);
    (void)drop_file;
    RUN(the_synthetic_flight_game_starts_with_the_cockpit_engine_pair);
    RUN(the_cockpit_flight_start_latches_the_external_pair_at_776_and_volume_12_then_the_cockpit_pair_at_764_and_volume_40);
    RUN(the_acting_pass_of_the_wait_state_beeps_with_volume_2_for_nine_blanks);
    RUN(a_gear_key_is_seen_by_the_sound_routine_of_its_pass_before_the_seconds_tick_can_clear_its_bit);
    RUN(the_cue_cool_down_counts_in_the_pass_after_the_cue_player_set_it);
    RUN(the_cockpit_view_stores_0x7fff_in_v4abfa_and_the_fixed_camera_fades_the_engine_with_the_viewed_records_distance);
    RUN(the_weapon_cycle_return_beeps_first_and_always_also_with_an_ejection_nibble_set);
    RUN(the_beep_is_silent_in_an_outside_view);
    RUN(the_switch_beeps_of_every_key_are_silent_in_the_map_view_through_the_view_byte_and_sound_in_the_cockpit);
    RUN(a_view_switch_stops_voice_3_only_and_the_notice_restarts_the_engine_with_the_external_pair);
    RUN(m_stops_all_four_voices_and_the_gate_keeps_the_routine_out_until_the_map_is_left_and_voice_3_stays_off);
    RUN(a_pause_stops_all_four_voices_and_the_unpause_restarts_the_engine_but_not_the_tone);
    RUN(a_cockpit_crash_stops_all_four_voices_and_plays_the_crash_noise_an_outside_crash_does_neither);
    RUN(the_ejection_bang_is_31_in_the_cockpit_view_and_63_outside_and_the_seat_tone_plays_in_the_cockpit_only);
    RUN(the_start_up_with_the_music_plays_the_first_object_of_the_chain_with_the_typing_byte_2);
    RUN(a_disk_without_the_music_starts_the_mission_tone_pair_at_volume_15_and_leaves_the_typing_byte_0);
    RUN(the_ejection_with_the_gear_sound_and_a_cue_playing_stops_voice_3_at_the_seat_tone_then_voice_2_at_the_bang_and_never_voices_0_and_1);
    RUN(a_pod_release_starts_the_noise_object_6_on_voice_2_with_volume_28_in_mode_6_only);
    RUN(return_in_training_part_1_beeps_and_the_s_key_does_not_on_the_invented_world);
    RUN(the_gear_sound_plays_with_the_camera_on_the_ejection_seat);
    RUN(the_engine_returns_4_blanks_after_the_unpause_key);
    RUN(the_blackout_accumulator_does_not_count_seconds_in_a_pause);
    TEST_MAIN_END();
}
