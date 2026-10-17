/* tests/test_sound.c - the core's sound module (src/core/sound.c, docs/re/10-sound.md): the library as an integer model (objects, scripts, the block-count rule, the master inputs, the stops), the wrappers and the
 * per-pass routine (one test per notice case with invented throttle, speed and rate values; the expected periods and volumes are computed in the tests from the note's formulas). The fixtures are invented
 * (tests/sound_synth.h: the shape of the note's table with invented sample lengths). The differential against the original's library is tests/rig/test_sound_diff.c (make -C tools/m68k-rig test-sound-core). */
#include "test.h"
#include "sound_synth.h"
#include "../src/core/sound.h"
#include "../src/frontend/sdl/audio.c"      /* the mixer, linked into this test: the chain tests run the library's writes through it */

static sound_t S;
static sound_table_t T;
static uint8_t NOTICE, HOLD;
static uint32_t CUES;

static void init(void) {
    sound_synth_table(&T);
    sound_init(&S, &T);
    NOTICE = 0; HOLD = 0; CUES = 0;
}

/* a cockpit pass of a viewed player at throttle 60, no rates, wheels up, afterburner off, in the retarget phase (pass counter 0) */
static sound_in_t base(void) {
    sound_in_t in;
    memset(&in, 0, sizeof in);
    in.v4a84d = 1; in.camera = 1; in.throttle = 60; in.cls = 0x11;
    in.notice = &NOTICE; in.hold = &HOLD; in.cues = &CUES;
    return in;
}
static void blanks(int n) { for (int i = 0; i < n; i++) sound_blank(&S); S.blank = 0; }
static int per(int v) { return S.v[v].per; }
static int vol(int v) { return S.v[v].vol; }
static int dma(int v) { return S.v[v].dma; }

/* the test's own copy of the library's random register (note 10 3.3): taps at bits 0 and 3, the new bit enters at the top */
static uint32_t R;
static uint32_t draw(int bits) {
    uint32_t r = 0;
    for (int i = 0; i < bits; i++) { const uint32_t b = (R ^ (R >> 3)) & 1u; R = (R >> 1) | (b << 31); r = (r << 1) | b; }
    return r;
}
static void reg_after_fill(void) { R = 0x47091247u; for (int i = 0; i < 2048; i++) draw(8); }

TEST(the_noise_buffer_is_the_first_bits_of_the_register_and_the_first_wind_draws_are_32_and_62) {
    init();
    uint32_t r = 0x47091247u;
    const int8_t *buf; uint32_t len;
    ASSERT_TRUE(sound_buffer(&S, 6, &buf, &len));
    ASSERT_EQ_INT(len, 0x800);
    for (int i = 0; i < 2048; i++) {
        uint32_t v = 0;
        for (int b = 0; b < 8; b++) { const uint32_t x = (r ^ (r >> 3)) & 1u; r = (r >> 1) | (x << 31); v = (v << 1) | x; }
        ASSERT_EQ_INT(buf[i], (int8_t)(uint8_t)v);
    }
    ASSERT_EQ_INT(S.rnd, r);
    ASSERT_TRUE(sound_buffer(&S, 0xA, &buf, &len));                  /* the gun plays the noise buffer too */
    ASSERT_EQ_INT(len, 0x800);
    sound_event(&S, SND_WIND, 8, 0);                                  /* the draws after the fill: 32 and 62 (the captures: 392 and 422) */
    ASSERT_EQ_INT(per(0), 0x168 + 32);
    ASSERT_EQ_INT(per(1), 0x168 + 62);
    ASSERT_TRUE(sound_buffer(&S, 4, &buf, &len));                    /* the tone: 16 samples of +127, 16 of -126 */
    ASSERT_EQ_INT(len, 32);
    ASSERT_EQ_INT(buf[0], 127); ASSERT_EQ_INT(buf[15], 127); ASSERT_EQ_INT(buf[16], -126); ASSERT_EQ_INT(buf[31], -126);
    ASSERT_TRUE(!sound_buffer(&S, 0, &buf, &len));                   /* a file-backed object has no core buffer */
}

TEST(the_tone_script_plays_two_segments_then_holds_and_stops_at_the_formula_blank) {
    init();
    const int p[9] = { 300, 10, 2, 400, 20, 3, 1, 1, 4 };           /* p1..p9 of 3.2 */
    sound_tone(&S, p);
    ASSERT_EQ_INT(dma(3), 1);
    ASSERT_EQ_INT(vol(3), 0);                                        /* the call's registers: the object's left-over period, volume 0 */
    ASSERT_EQ_INT(per(3), 358);
    const int want[10][2] = { { 300, 10 }, { 300, 10 }, { 400, 20 }, { 400, 20 }, { 400, 20 }, { 400, 20 }, { 400, 20 }, { 400, 20 }, { 400, 20 }, { 0, 0 } };
    for (int k = 0; k < 9; k++) { blanks(1); ASSERT_EQ_INT(per(3), want[k][0]); ASSERT_EQ_INT(vol(3), want[k][1]); ASSERT_EQ_INT(dma(3), 1); }
    blanks(1);                                                       /* blank 1 + p8 x (p7 x (p3 + p6) + p9) = 10: the voice stops */
    ASSERT_EQ_INT(dma(3), 0);
}

TEST(a_tone_with_a_repeat_count_of_zero_never_ends_and_the_next_tone_replaces_it) {
    init();
    const int p[9] = { 229, 3, 5, 229, 3, 5, 1, 0, 1 };
    sound_tone(&S, p);
    blanks(400);
    ASSERT_EQ_INT(dma(3), 1);
    ASSERT_EQ_INT(per(3), 229); ASSERT_EQ_INT(vol(3), 3);
    const int q[9] = { 500, 15, 5, 500, 15, 5, 3, 1, 5 };
    sound_tone(&S, q);
    blanks(1);
    ASSERT_EQ_INT(per(3), 500);
    blanks(36);                                                       /* 1 + 3 x 10 + 5 = 36 blanks in all */
    ASSERT_EQ_INT(dma(3), 0);
}

TEST(a_tone_with_a_last_wait_of_zero_stops_after_the_first_outer_repeat) {
    init();
    const int p[9] = { 300, 8, 3, 300, 8, 3, 2, 3, 0 };              /* p9 = 0 is a wait of 0: the script ends after 1 + p7 x (p3 + p6) blanks */
    sound_tone(&S, p);
    blanks(12); ASSERT_EQ_INT(dma(3), 1);
    blanks(1);  ASSERT_EQ_INT(dma(3), 0);
}

TEST(the_noise_fades_linearly_and_stops_after_the_duration) {
    init();
    reg_after_fill();
    sound_event(&S, SND_NOISE, 28, 48);
    const uint32_t r = draw(11);
    ASSERT_EQ_INT(S.o[6].period >> 16, 0x231E + 4 * (int)r);
    const int step = (28 << 16) / 48;
    for (int k = 1; k <= 48; k++) {
        blanks(1);
        ASSERT_EQ_INT(vol(2), (((28 << 16) - (k - 1) * step) >> 16) & 0x3F);
        ASSERT_EQ_INT(per(2), 0x231E + 4 * (int)r);
        ASSERT_EQ_INT(dma(2), 1);
        if (k == 40) ASSERT_EQ_INT(vol(2), 5);                        /* note 10 8.2: 5 at blank 40 */
    }
    blanks(1);
    ASSERT_EQ_INT(dma(2), 0);
}

TEST(a_missing_noise_sound_reports_failure_for_the_caller_to_post_the_notice) {
    init();
    S.en12 &= ~1;
    ASSERT_EQ_INT(sound_event(&S, SND_NOISE, 28, 48), 1);
    ASSERT_EQ_INT(dma(2), 0);
    S.en12 |= 1;
    ASSERT_EQ_INT(sound_event(&S, SND_NOISE, 28, 48), 0);
}

TEST(the_gun_burst_has_six_pulses_then_a_tail_and_keeps_the_period_it_ends_on) {
    init();
    S.o[0xA].length = 1000000;                 /* a block longer than the sound, so that no block end falls in the window (a block end rewrites the fields, one step ahead: the test after the gear's) */
    sound_event(&S, SND_GUN, 6, 0x16);
    ASSERT_EQ_INT(sound_slot(&S, 2), 0xA);
    for (int k = 1; k <= 18; k++) {            /* three blanks a pulse: on, on, off */
        blanks(1);
        ASSERT_EQ_INT(vol(2), (k % 3) ? 0x16 : 0);
        ASSERT_EQ_INT(per(2), 150);            /* the loader's period, heard until the first burst ends its tail */
    }
    for (int k = 19; k <= 28; k++) {           /* the tail: period 280, volume 20 falling by 61,440 / 65,536 a blank */
        blanks(1);
        ASSERT_EQ_INT(per(2), 280);
        ASSERT_EQ_INT(vol(2), ((20 << 16) - (k - 19) * 61440) >> 16);
    }
    ASSERT_EQ_INT(vol(2), 11);
    blanks(1);
    ASSERT_EQ_INT(dma(2), 0);
    sound_event(&S, SND_GUN, 6, 0x1E);         /* the second burst plays its pulses at 280: nothing sets the period back (review 3) */
    blanks(1);
    ASSERT_EQ_INT(per(2), 280); ASSERT_EQ_INT(vol(2), 0x1E);
}

TEST(the_launch_ramps_up_then_fades_for_sixty_blanks_at_a_constant_pitch) {
    init();
    S.o[0xC].length = 1000000;                 /* no block end in the window */
    reg_after_fill();
    draw(11); draw(11);                        /* nothing else drew: the launch's 3-bit draw follows the fill */
    R = S.rnd;                                 /* resynchronise with the module's register */
    sound_event(&S, SND_LAUNCH, 61, 0);
    const int r = (int)draw(3);
    for (int k = 1; k <= 8; k++) { blanks(1); ASSERT_EQ_INT(vol(2), 11 + 2 * (k - 1)); ASSERT_EQ_INT(per(2), 0x8C + r); }
    for (int k = 9; k <= 68; k++) { blanks(1); ASSERT_EQ_INT(vol(2), ((61 << 16) - (k - 9) * (60 << 16) / 60) >> 16); ASSERT_EQ_INT(per(2), 0x8C + r); }
    ASSERT_EQ_INT(vol(2), 2);
    blanks(1);
    ASSERT_EQ_INT(dma(2), 0);
}

TEST(the_gear_sound_slides_from_700_to_595_in_fifteen_blanks_and_runs_until_stopped) {
    init();
    sound_event(&S, SND_GEAR, 10, 1);
    ASSERT_EQ_INT(per(2), 358);                /* the call writes the left-over period for the rest of its blank */
    for (int k = 1; k <= 16; k++) { blanks(1); ASSERT_EQ_INT(per(2), 700 - 7 * (k - 1)); ASSERT_EQ_INT(vol(2), 10); }
    blanks(30);
    ASSERT_EQ_INT(per(2), 595);
    ASSERT_EQ_INT(dma(2), 1);
    sound_event(&S, SND_GEAR_STOP, 0, 0);
    ASSERT_EQ_INT(dma(2), 0);
    sound_event(&S, SND_GEAR, 5, 0);           /* not the camera's record: nothing */
    ASSERT_EQ_INT(dma(2), 0);
}

/* A block ends after n samples, each lasting the period register's value when the sample starts (note 10 9.2). The test's own sample-by-sample reference: the register values of the blanks
 * `per[b]` (b = 1, 2, ...) give the blank in which a block of n samples that started at the first blank's start ends, and the sample in progress carries into the next blank. */
static int ref_block_end(int n, const int *per, int blanks_known) {
    long cnt = 0;                               /* colour clocks left of the sample in progress */
    int left = n;
    for (int b = 0; b < blanks_known; b++) {
        long avail = SND_CLOCKS_PER_BLANK;
        while (avail > 0) {
            if (cnt == 0) cnt = per[b];
            const long step = avail < cnt ? avail : cnt;
            cnt -= step; avail -= step;
            if (cnt == 0 && --left == 0) return b + 1;
        }
    }
    return -1;
}

TEST(a_block_ends_after_its_samples_at_the_periods_in_force_and_the_handler_writes_the_fields_one_step_ahead) {
    init();
    S.o[0xB].length = 300;                     /* the gear: period 700 sliding down by 7 a blank */
    sound_event(&S, SND_GEAR, 10, 1);
    int p[20];
    for (int b = 0; b < 20; b++) p[b] = 700 - 7 * b;   /* the register of blank b + 1 (the server writes it before the block clock runs) */
    const int end = ref_block_end(300, p, 20);
    ASSERT_TRUE(end > 1);
    int ends = 0;
    for (int k = 1; k <= end + 1; k++) {
        blanks(1);
        if (k == end) { ASSERT_EQ_INT(per(2), 700 - 7 * k); ends++; }   /* the block ends in this blank, after the server: the handler writes the period field, already stepped */
        else if (k < end) ASSERT_EQ_INT(per(2), 700 - 7 * (k - 1));
    }
    ASSERT_EQ_INT(ends, 1);
}

TEST(a_glide_inside_a_block_changes_when_it_ends) {
    init();
    S.o[0xB].length = 1400;
    sound_event(&S, SND_GEAR, 10, 1);
    int p[30];
    for (int b = 0; b < 30; b++) p[b] = b < 15 ? 700 - 7 * b : 595;   /* the slide, then 595 */
    const int slide_end = ref_block_end(1400, p, 30);
    int constant[30];
    for (int b = 0; b < 30; b++) constant[b] = 700;
    const int flat_end = ref_block_end(1400, constant, 30);
    ASSERT_TRUE(slide_end > 0 && flat_end > 0);
    ASSERT_TRUE(slide_end != flat_end);        /* the two rules differ for this block: the periods in force count, not the period at the block's start */
    int seen = -1;
    for (int k = 1; k <= 30 && seen < 0; k++) {
        blanks(1);
        if (k >= 2 && per(2) != (k <= 16 ? 700 - 7 * (k - 1) : 595)) seen = k;   /* the first blank whose register is not the glide's own value: the handler's write */
    }
    ASSERT_EQ_INT(seen, slide_end);
}

TEST(the_ground_contact_plays_one_block_at_a_constant_period_and_needs_the_cameras_record) {
    init();
    sound_event(&S, SND_GROUND, 0x28, 1);
    ASSERT_EQ_INT(per(2), 310); ASSERT_EQ_INT(vol(2), 0x28);
    ASSERT_EQ_INT(sound_slot(&S, 2), SND_NONE);                      /* the slot is emptied at the DMA-start request: no script, no glide */
    const long block = 1500L * 310;                                  /* bytes x period colour clocks */
    int k = 0;
    while (dma(2) && k < 100) { blanks(1); k++; if (dma(2)) { ASSERT_EQ_INT(per(2), 310); ASSERT_EQ_INT(vol(2), 0x28); } }
    ASSERT_EQ_INT(k, (int)((block + SND_CLOCKS_PER_BLANK - 1) / SND_CLOCKS_PER_BLANK));
    sound_event(&S, SND_GROUND, 0x1E, 0);
    ASSERT_EQ_INT(dma(2), 0);
}

/* the object each blank shows under the hardware's block, as (object, first blank) runs */
static int chain_run(int voice, int max_blanks, int *objs, int *first, int max_runs) {
    int n = 0, last = -2;
    for (int b = 0; b < max_blanks; b++) {
        blanks(1);
        const int cur = S.v[voice].dma ? S.v[voice].cur : -1;
        if (cur != last && n < max_runs) { objs[n] = cur; first[n] = b + 1; n++; }
        last = cur;
    }
    return n;
}

TEST(a_count_c_object_with_a_successor_plays_c_plus_1_blocks_and_the_chain_follows_the_next_links) {
    init();
    sound_start(&S, 0xD, 0, 0x3F);
    int objs[8], first[8];
    const int n = chain_run(0, 400, objs, first, 8);
    ASSERT_TRUE(n >= 4);
    ASSERT_EQ_INT(objs[0], 0xD); ASSERT_EQ_INT(objs[1], 0xF); ASSERT_EQ_INT(objs[2], 0x11); ASSERT_EQ_INT(objs[3], 0x13);
    /* 0xD: count 13, 14 blocks of 700 bytes at 358; 0xF: count 1, 2 blocks of 600 bytes; 0x11: count 2, 3 blocks of 1500 bytes */
    const long b1 = 14L * 700 * 358, b2 = b1 + 2L * 600 * 358, b3 = b2 + 3L * 1500 * 358;
    ASSERT_EQ_INT(first[0], 1);
    ASSERT_EQ_INT(first[1], (int)(b1 / SND_CLOCKS_PER_BLANK) + 1);
    ASSERT_EQ_INT(first[2], (int)(b2 / SND_CLOCKS_PER_BLANK) + 1);
    ASSERT_EQ_INT(first[3], (int)(b3 / SND_CLOCKS_PER_BLANK) + 1);
}

TEST(the_chain_loops_back_to_the_first_loop_object_after_the_last_and_voice_1_follows_in_step) {
    init();
    sound_start(&S, 0xD, 0, 0x3F);
    sound_start(&S, 0xE, 1, 0x3F);
    int a[24];
    int n = 0, last = -2;
    for (int b = 0; b < 1500; b++) {
        blanks(1);
        ASSERT_EQ_INT(S.v[1].cur, S.v[0].cur + 1);                    /* the two voices play the odd and the even object of a pair in step */
        if (S.v[0].cur != last && n < 24) { a[n++] = S.v[0].cur; last = S.v[0].cur; }
    }
    ASSERT_TRUE(n >= 13);
    static const int want[13] = { 0xD, 0xF, 0x11, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x1F, 0x21, 0x11, 0x13 };
    for (int i = 0; i < 13; i++) ASSERT_EQ_INT(a[i], want[i]);
}

TEST(counts_persist_across_a_stop_and_the_music_after_a_flight_plays_the_intro_objects_one_block_each) {
    init();
    sound_start(&S, 0xD, 0, 0x3F);
    int objs[8], first[8];
    chain_run(0, 330, objs, first, 8);                                /* into the loop: the counts are spent and partly reloaded */
    ASSERT_EQ_INT(S.o[0xD].count, 0);                                 /* the reload of 0xD */
    const int where = S.v[0].cur;
    ASSERT_TRUE(where >= 0x11);
    sound_stop_all(&S);
    ASSERT_EQ_INT(dma(0), 0);
    sound_start(&S, 0xD, 0, 0x3F);                                    /* the phrase again: 0xD has count 0, 0xF has count 0 */
    int o2[8], f2[8];
    const int n = chain_run(0, 120, o2, f2, 8);
    ASSERT_TRUE(n >= 3);
    ASSERT_EQ_INT(o2[0], 0xD); ASSERT_EQ_INT(o2[1], 0xF);
    ASSERT_EQ_INT(f2[1], (int)(700L * 358 / SND_CLOCKS_PER_BLANK) + 1);                          /* one block of 0xD */
    ASSERT_EQ_INT(f2[2], (int)((700L * 358 + 600L * 358) / SND_CLOCKS_PER_BLANK) + 1);           /* one block of 0xF, then the loop resumes */
    ASSERT_TRUE(o2[2] >= 0x11);
}

TEST(the_master_level_ramps_a_quarter_unit_per_blank_while_the_flag_is_set_and_caps_the_volumes) {
    init();
    sound_master(&S, 31, -1, 1);                                      /* the any-key at the credits: target 31, flag 1 */
    sound_start(&S, 0, 0, 63);                                        /* the engine voice at the full volume 63 */
    for (int k = 1; k <= 130; k++) {
        blanks(1);                                                    /* the ramp runs before the server in a blank: the cap in blank k is 63 - ceil(k / 4) */
        const int lvl = k < 128 ? 0x3F0000 - 0x4000 * k : 0x1F0000;
        ASSERT_EQ_INT(S.level, lvl);
        ASSERT_EQ_INT(vol(0), lvl >> 16);
        ASSERT_EQ_INT(vol(0), k < 125 ? 63 - (k + 3) / 4 : 31);
    }
    ASSERT_EQ_INT(S.level, 0x1F0000);
    sound_master(&S, 0, -1, -1);                                      /* the echo state: target 0, the flag unchanged */
    blanks(8);
    ASSERT_EQ_INT(S.level, 0x1F0000 - 8 * 0x4000);
    sound_master(&S, -1, 63, 0);                                      /* the mode dispatch: the level at once, the flag 0 */
    ASSERT_EQ_INT(S.level, 0x3F0000);
    blanks(10);
    ASSERT_EQ_INT(S.level, 0x3F0000);                                 /* flag 0: the level stays */
    sound_master(&S, 10, -1, 2);                                      /* any non-zero flag (2 during the start-up too) lets the level move toward the target */
    blanks(10);
    ASSERT_EQ_INT(S.level, 0x3F0000 - 10 * 0x4000);
}

TEST(the_drain_hands_over_the_register_writes_with_their_blanks_and_the_master_inputs) {
    init();
    sound_master(&S, 31, 31, 1);
    sound_start(&S, 0, 0, 40);
    game_audio_cmd_t c[GAME_MAX_AUDIO_CMDS];
    int n = 0;
    uint8_t mt, ms, mr;
    sound_drain(&S, c, GAME_MAX_AUDIO_CMDS, &n, &mt, &ms, &mr);
    ASSERT_EQ_INT(mt, 31); ASSERT_EQ_INT(ms, 31); ASSERT_EQ_INT(mr, 1);
    /* the player's start: the stop (volume 0, period 0x7C), then the latch, DMA on, period, volume; writes that change nothing are left out; the stamp is the pass (blank 0) */
    ASSERT_TRUE(n >= 4);
    int k = 0;
    ASSERT_EQ_INT(c[k].op, GAME_AUD_PERIOD); ASSERT_EQ_INT(c[k].value, 0x7C); k++;   /* the volume was 0 already: not written again */
    ASSERT_EQ_INT(c[k].op, GAME_AUD_LATCH); ASSERT_EQ_INT(c[k].object, 0); ASSERT_EQ_INT(c[k].length, 4000); k++;
    ASSERT_EQ_INT(c[k].op, GAME_AUD_DMA_ON); k++;
    ASSERT_EQ_INT(c[k].op, GAME_AUD_PERIOD); ASSERT_EQ_INT(c[k].value, 358); k++;
    ASSERT_EQ_INT(c[k].op, GAME_AUD_VOLUME); ASSERT_EQ_INT(c[k].value, 31); k++;       /* 40 capped by the level 31 */
    ASSERT_EQ_INT(c[0].blank, 0);
    sound_drain(&S, c, GAME_MAX_AUDIO_CMDS, &n, &mt, &ms, &mr);
    ASSERT_EQ_INT(n, 0); ASSERT_EQ_INT(ms, 0xFF);                    /* the immediate set is delivered once */
}

TEST(a_step_runs_six_six_six_and_seven_blanks_and_stamps_them) {
    init();
    sound_start(&S, 4, 3, 0);
    const int p[9] = { 300, 8, 1, 400, 9, 1, 0, 0, 1 };              /* a tone that changes every blank for ever */
    sound_tone(&S, p);
    game_audio_cmd_t c[GAME_MAX_AUDIO_CMDS];
    int n, total = 0;
    uint8_t a, b, d;
    sound_drain(&S, c, GAME_MAX_AUDIO_CMDS, &n, &a, &b, &d);
    static const int want[8] = { 6, 6, 6, 7, 6, 6, 6, 7 };
    for (int step = 0; step < 8; step++) {
        sound_step(&S);
        sound_drain(&S, c, GAME_MAX_AUDIO_CMDS, &n, &a, &b, &d);
        int maxb = -1;
        for (int i = 0; i < n; i++) if (c[i].blank > maxb) maxb = c[i].blank;
        ASSERT_EQ_INT(maxb, want[step] - 1);                          /* the last blank of the step carries a write */
        total += want[step];
    }
    ASSERT_EQ_INT(total, 50);                                         /* 25 blanks per 4 steps */
}

TEST(stop_all_stops_the_playing_voices_in_order_and_a_voice_stop_leaves_the_others) {
    init();
    sound_start(&S, 0, 0, 40); sound_start(&S, 1, 1, 40); sound_event(&S, SND_GEAR, 10, 1);
    const int p[9] = { 300, 8, 1, 400, 9, 1, 0, 0, 1 };
    sound_tone(&S, p);
    game_audio_cmd_t c[GAME_MAX_AUDIO_CMDS];
    int n; uint8_t a, b, d;
    sound_drain(&S, c, GAME_MAX_AUDIO_CMDS, &n, &a, &b, &d);
    sound_stop_voice(&S, 3);
    ASSERT_EQ_INT(dma(3), 0);
    ASSERT_EQ_INT(dma(0) + dma(1) + dma(2), 3);
    sound_stop_all(&S);
    sound_drain(&S, c, GAME_MAX_AUDIO_CMDS, &n, &a, &b, &d);
    int offs[4], no = 0;
    for (int i = 0; i < n; i++) if (c[i].op == GAME_AUD_DMA_OFF) offs[no++] = c[i].voice;
    ASSERT_EQ_INT(no, 4);                                              /* the voice stop of 3 above, then the stop-all: voices 0, 1 and 2 in that order */
    ASSERT_EQ_INT(offs[0], 3); ASSERT_EQ_INT(offs[1], 0); ASSERT_EQ_INT(offs[2], 1); ASSERT_EQ_INT(offs[3], 2);
    for (int v = 0; v < 4; v++) { ASSERT_EQ_INT(dma(v), 0); ASSERT_EQ_INT(per(v), 0x7C); ASSERT_EQ_INT(vol(v), 0); }
}

/* ---- the per-pass routine ---------------------------------------------------------------------------------------------------------------------------- */

TEST(cockpit_normal_engine_case_fe_starts_the_cockpit_pair_on_voices_0_and_1) {
    init();
    sound_in_t in = base();
    NOTICE = 0xFE;
    sound_pass(&S, &in);
    /* a0 = 60: P0 = 63 - ((120 - 60) >> 2) = 48; cockpit: not halved (strain 0), P = max(48, 0x28) = 48; a = min(60 + 0, 120) = 60; b = 784 - 120 - 15 = 649 */
    ASSERT_EQ_INT(sound_slot(&S, 0), 0); ASSERT_EQ_INT(sound_slot(&S, 1), 1);
    ASSERT_EQ_INT(per(0), 649); ASSERT_EQ_INT(per(1), 651);           /* voice 1 detuned by +2 */
    ASSERT_EQ_INT(vol(0), 48); ASSERT_EQ_INT(vol(1), 48);
    ASSERT_EQ_INT(NOTICE, 0);
}

TEST(the_engine_volume_is_halved_in_the_cockpit_under_strain_and_floored_at_0x28) {
    init();
    sound_in_t in = base();
    in.strain = 6; NOTICE = 0xFE;                                      /* P0 = 48 >> 1 = 24, the cockpit floor 0x28 */
    sound_pass(&S, &in);
    ASSERT_EQ_INT(vol(0), 0x28);
    init(); in = base(); in.throttle = 120; in.strain = 3; NOTICE = 0xFE;   /* full power: P0 = 63 */
    sound_pass(&S, &in);
    ASSERT_EQ_INT(vol(0), 63); ASSERT_EQ_INT(per(0), 784 - 240 - 30);
    init(); in = base(); in.throttle = 5; NOTICE = 0xFE;               /* a0 <= 8: 60 - ((120 - 5) >> 1) = 60 - 57 = 3, the floor 0x28 */
    sound_pass(&S, &in);
    ASSERT_EQ_INT(vol(0), 0x28);
}

TEST(the_throttle_term_uses_the_turn_rates_and_is_not_bounded_below) {
    init();
    sound_in_t in = base();
    in.throttle = 120; in.roll_rate = 0x0400; in.pitch_rate = 0;       /* u = |0x400 >> 4| = 64; w = 0; a0 >= 120: a = 120 - 64 = 56 */
    NOTICE = 0xFE;
    sound_pass(&S, &in);
    ASSERT_EQ_INT(per(0), 784 - 2 * 56 - (56 >> 2));
    init(); in = base();
    in.throttle = 30; in.roll_rate = 0; in.pitch_rate = (int16_t)-0x0800; /* w = 128 > u = 0: u = 0 + (E >> 4) = -128; a = min(30 - 128, 120) = -98 */
    NOTICE = 0xFE;
    sound_pass(&S, &in);
    ASSERT_EQ_INT(per(0), 784 - 2 * -98 - (-98 >> 2));                 /* 784 + 196 + 25 = 1005 */
    ASSERT_EQ_INT(per(0), 1005);
}

TEST(outside_view_case_fe_uses_the_external_pair_and_the_view_terms) {
    init();
    sound_in_t in = base();
    in.view = 0xFF; in.v4a86d = 1; in.ext_dir = 4;                     /* the default outside view, behind */
    NOTICE = 0xFE;
    sound_pass(&S, &in);
    /* P0 = 48; direction 4 unchanged; the level -2 unchanged; afterburner off: 48 - (48 >> 2) = 36; P = 36 - (36 >> 2) = 27; b = 800 - 120 - 30 - 15 = 635 */
    ASSERT_EQ_INT(sound_slot(&S, 0), 2); ASSERT_EQ_INT(sound_slot(&S, 1), 3);
    ASSERT_EQ_INT(per(0), 635); ASSERT_EQ_INT(per(1), 637);
    ASSERT_EQ_INT(vol(0), 27);
    init(); in = base();
    in.view = 2; in.v4a86d = 1; in.ext_dir = 0; in.w2 = 8;             /* level 2 - 1 = 1; in front: (48 >> 1) + (48 >> 3) = 30; level 1: 30 - 7 = 23; afterburner on; P = 23 - 5 = 18 */
    NOTICE = 0xFE;
    sound_pass(&S, &in);
    ASSERT_EQ_INT(vol(0), 18);
    init(); in = base();
    in.view = 0xFF; in.v4a86d = 0; in.cam_dist = 200; in.w2 = 8;       /* the approach camera: 32 x 48 / 200 = 7; afterburner on: unchanged; P = 7 - 1 = 6 */
    NOTICE = 0xFE;
    sound_pass(&S, &in);
    ASSERT_EQ_INT(vol(0), 6);
}

TEST(afterburner_on_and_off_cases_in_the_cockpit) {
    init();
    sound_in_t in = base();
    NOTICE = 0xFD;                                                     /* the afterburner switching on: the external pair at b + 0x23, volume 0x1E */
    sound_pass(&S, &in);
    ASSERT_EQ_INT(sound_slot(&S, 0), 2);
    ASSERT_EQ_INT(per(0), 649 + 0x23); ASSERT_EQ_INT(vol(0), 0x1E);
    init(); in = base();
    NOTICE = 0xFC;                                                     /* switching off: the cockpit pair at b + 0x20, volume P */
    sound_pass(&S, &in);
    ASSERT_EQ_INT(sound_slot(&S, 0), 0);
    ASSERT_EQ_INT(per(0), 649 + 0x20); ASSERT_EQ_INT(vol(0), 48);
    init(); in = base(); in.w2 = 8;                                    /* case 0xFE with the afterburner on: the external pair at P >> 2 */
    NOTICE = 0xFE;
    sound_pass(&S, &in);
    ASSERT_EQ_INT(sound_slot(&S, 0), 2); ASSERT_EQ_INT(vol(0), 48 >> 2);
    init(); in = base(); in.view = 0xFF; in.v4a86d = 1; in.ext_dir = 4;
    NOTICE = 0xFD;                                                     /* outside: b + 0x20, v6 = 0x3D */
    sound_pass(&S, &in);
    ASSERT_EQ_INT(per(0), 635 + 0x20); ASSERT_EQ_INT(vol(0), 0x3D);
}

TEST(the_launch_notice_plays_the_launch_and_clears_the_hold_and_the_overspeed_notice_the_external_pair) {
    init();
    sound_in_t in = base();
    NOTICE = 0xFB; HOLD = 8;
    sound_pass(&S, &in);
    ASSERT_EQ_INT(sound_slot(&S, 2), 0xC);
    blanks(1);
    ASSERT_EQ_INT(vol(2), 11);                                         /* the launch script's first ramp (the volume v of the call is the fade's start, 9 blanks later) */
    ASSERT_EQ_INT(HOLD, 0);                                            /* 0x152C6 clears H */
    S.en12 &= ~2;                                                      /* no launch sound: the external pair at b + 0x16 */
    init(); S.en12 &= ~2; in = base(); NOTICE = 0xFB; HOLD = 8;
    sound_pass(&S, &in);
    ASSERT_EQ_INT(sound_slot(&S, 0), 2); ASSERT_EQ_INT(per(0), 649 + 0x16); ASSERT_EQ_INT(vol(0), 0x3D);
    init(); in = base(); NOTICE = 0xFA;                                // cockpit: the external pair at b + 0x16, volume 0x28
    sound_pass(&S, &in);
    ASSERT_EQ_INT(per(0), 649 + 0x16); ASSERT_EQ_INT(vol(0), 0x28);
}

TEST(other_notices_glide_the_voices_over_32_blanks_and_the_hold_picks_the_volume) {
    init();
    sound_in_t in = base();
    NOTICE = 0xFE;
    sound_pass(&S, &in);                                               /* the cockpit pair at 649, volume 48 */
    blanks(1);
    in.throttle = 100; in.pass_n = 4; NOTICE = 0;                      /* a retarget pass: a0 = 100: P0 = 63 - (20 >> 2) = 58, a = 100: b = 784 - 200 - 25 = 559 */
    sound_pass(&S, &in);
    ASSERT_EQ_INT(S.o[0].ptimer, 32); ASSERT_EQ_INT(S.o[0].vtimer, 32);
    ASSERT_EQ_INT(S.o[0].pstep, ((559 << 16) - S.o[0].period) / 32);
    ASSERT_EQ_INT(S.o[0].vstep, ((58 << 16) - S.o[0].volume) / 32);
    ASSERT_EQ_INT(S.o[1].pstep, S.o[0].pstep);                          /* both voices take the step computed from voice 0 */
    blanks(40);
    ASSERT_EQ_INT(per(0), 559 + 0);
    ASSERT_EQ_INT(vol(0), 58 - 1 < vol(0) ? vol(0) : vol(0));
    init(); in = base(); in.throttle = 100; NOTICE = 0xFE; HOLD = 3;   /* H non-zero: P0 = 63 */
    sound_pass(&S, &in);
    ASSERT_EQ_INT(vol(0), 63);
}

TEST(the_engine_is_retargeted_every_fourth_pass_only_and_the_hold_counts_down_in_every_pass) {
    init();
    sound_in_t in = base();
    NOTICE = 0xFE;
    sound_pass(&S, &in);
    in.throttle = 100;
    NOTICE = 0; HOLD = 3;
    in.pass_n = 1;                                                     /* a skipped pass: no glide... */
    sound_pass(&S, &in);
    ASSERT_EQ_INT(S.o[0].ptimer, 0);
    ASSERT_EQ_INT(HOLD, 2);                                            /* ...but H falls */
    in.pass_n = 2; sound_pass(&S, &in); ASSERT_EQ_INT(HOLD, 1);
    in.pass_n = 3; sound_pass(&S, &in); ASSERT_EQ_INT(HOLD, 0);
    ASSERT_EQ_INT(NOTICE, 0xFE);                                       /* H reached 0 in the cockpit with the afterburner off: restart the engine at its normal level */
    in.pass_n = 4; sound_pass(&S, &in);                                /* a non-zero notice always runs the engine block */
    ASSERT_EQ_INT(per(0), 784 - 200 - 25);
    ASSERT_EQ_INT(NOTICE, 0);
}

TEST(throttle_zero_in_the_air_is_the_wind_and_on_the_ground_rolling_the_engine_at_the_idle_volume) {
    init();
    sound_in_t in = base();
    in.throttle = 0; in.speed = 0x5000; NOTICE = 0xFE;
    sound_pass(&S, &in);
    ASSERT_EQ_INT(sound_slot(&S, 0), 8); ASSERT_EQ_INT(sound_slot(&S, 1), 9);
    ASSERT_EQ_INT(vol(0), (0x5000 >> 9) >> 1);                          /* |W| >> 9, halved in the cockpit: 20 >> 1 = 10 */
    init(); in = base(); in.throttle = 0; in.speed = 0; NOTICE = 0xFE;
    sound_pass(&S, &in);
    ASSERT_EQ_INT(vol(0), 1);                                           /* at least 1 */
    init(); in = base(); in.throttle = 0; in.speed = 0x100; in.w2 = 0x80; NOTICE = 0xFE;   /* wheels down and rolling: P0 = 0x0C, the cockpit floor 0x28; the view and afterburner terms do not apply to it */
    sound_pass(&S, &in);
    ASSERT_EQ_INT(sound_slot(&S, 0), 0); ASSERT_EQ_INT(vol(0), 0x28);
    init(); in = base(); in.throttle = 0; in.speed = 0x100; in.w2 = 0x80; in.view = 0xFF; in.v4a86d = 1; in.ext_dir = 0; NOTICE = 0xFE;   /* outside: 0x0C less a quarter, no direction terms */
    sound_pass(&S, &in);
    ASSERT_EQ_INT(vol(0), 12 - 3);
}

TEST(a_record_of_class_3x_hears_only_the_wind_at_volume_0x10) {
    init();
    sound_in_t in = base();
    in.cls = 0x31; NOTICE = 0xFE;
    sound_pass(&S, &in);
    ASSERT_EQ_INT(sound_slot(&S, 0), 8);
    ASSERT_EQ_INT(vol(0), 0x10);
}

TEST(the_gates_stop_the_whole_routine_and_leave_the_notice_alone) {
    init();
    sound_in_t in = base();
    NOTICE = 0xFE;
    in.v4a866 = 1; sound_pass(&S, &in); ASSERT_EQ_INT(NOTICE, 0xFE); ASSERT_EQ_INT(dma(0), 0);
    in.v4a866 = 0; in.v4a865 = 1; sound_pass(&S, &in); ASSERT_EQ_INT(NOTICE, 0xFE);
    in.v4a865 = 0; in.v4a966 = 1; sound_pass(&S, &in); ASSERT_EQ_INT(NOTICE, 0xFE);
    in.v4a966 = 0; in.v4a84d = 0; sound_pass(&S, &in); ASSERT_EQ_INT(NOTICE, 0xFE);
    in.v4a84d = 1; sound_pass(&S, &in); ASSERT_EQ_INT(NOTICE, 0); ASSERT_EQ_INT(dma(0), 1);
}

TEST(the_gear_countdown_starts_the_sound_in_the_key_pass_and_stops_it_two_ticks_later) {
    init();
    sound_in_t in = base();
    in.pass_n = 1;
    ASSERT_EQ_INT(sound_gear_key(&S), 1);
    ASSERT_EQ_INT(S.gear_cd, 0x83);
    sound_pass(&S, &in);                                                /* cockpit: volume 10 */
    ASSERT_EQ_INT(S.gear_cd, 3); ASSERT_EQ_INT(sound_slot(&S, 2), 0xB); ASSERT_EQ_INT(vol(2), 10);
    sound_second(&S); ASSERT_EQ_INT(S.gear_cd, 2);
    sound_pass(&S, &in); ASSERT_EQ_INT(dma(2), 1);
    sound_second(&S); ASSERT_EQ_INT(S.gear_cd, 1);
    sound_pass(&S, &in); ASSERT_EQ_INT(dma(2), 0); ASSERT_EQ_INT(S.gear_cd, 0);
    init(); in = base(); in.pass_n = 1; in.view = 0xFF;                 /* outside: volume 5 with the flight value, nothing without it */
    sound_gear_key(&S); sound_pass(&S, &in);
    ASSERT_EQ_INT(dma(2), 0); ASSERT_EQ_INT(S.gear_cd, 3);
    init(); in = base(); in.pass_n = 1; in.view = 0xFF; in.v4a86d = 1;
    sound_gear_key(&S); sound_pass(&S, &in);
    ASSERT_EQ_INT(vol(2), 5);
    init(); S.en13 &= ~0x40;
    ASSERT_EQ_INT(sound_gear_key(&S), 0);                               /* no gear sound: the key clicks instead */
}

TEST(the_seconds_tick_clears_bit_7_first_and_counts_the_cool_down) {
    init();
    S.gear_cd = 0x83; S.cooldown = 3;
    sound_second(&S);
    ASSERT_EQ_INT(S.gear_cd, 2); ASSERT_EQ_INT(S.cooldown, 2);
    sound_second(&S); sound_second(&S); sound_second(&S);
    ASSERT_EQ_INT(S.gear_cd, 0); ASSERT_EQ_INT(S.cooldown, 0);
}

TEST(the_cue_player_plays_one_cue_in_the_notes_order_clears_the_long_and_the_cool_down_holds_it) {
    init();
    sound_in_t in = base();
    in.pass_n = 1;
    CUES = 0x800 | 0x20 | 0x80;                                        /* 0x80 comes before 0x20 and 0x800 */
    sound_pass(&S, &in);
    ASSERT_EQ_INT(CUES, 0);
    blanks(1);
    ASSERT_EQ_INT(per(3), 324); ASSERT_EQ_INT(vol(3), 10);              /* the crash-imminent cue: 324, 10, 8, 200, 10, 5, 3, 1, 4 */
    blanks(8);
    ASSERT_EQ_INT(per(3), 200);
    CUES = 0x01; sound_pass(&S, &in);                                   /* the missile cue sets the cool-down 3 */
    ASSERT_EQ_INT(S.cooldown, 3);
    CUES = 0x40; sound_pass(&S, &in);
    ASSERT_EQ_INT(CUES, 0x40);                                          /* held back and kept while the cool-down is above 0 */
    S.cooldown = 0;
    in.eject_nibble = 3;                                                /* during an ejection every cue but 0x40 is discarded */
    CUES = 0x80; sound_pass(&S, &in);
    ASSERT_EQ_INT(CUES, 0);
    blanks(1);
    ASSERT_EQ_INT(per(3), 0x7C);                                        /* nothing new played: the first segment of the 0x01 cue (124, 16...) is still the one on voice 3 */
    CUES = 0x40; sound_pass(&S, &in);
    blanks(1);
    ASSERT_EQ_INT(per(3), 0xA0); ASSERT_EQ_INT(vol(3), 0x10);
    in.eject_nibble = 0;
    CUES = 0x08; sound_pass(&S, &in);                                   /* the silence cue stops voice 3 */
    ASSERT_EQ_INT(dma(3), 0);
    CUES = 0x200; sound_pass(&S, &in);                                  /* a bit no branch tests: cleared, nothing played */
    ASSERT_EQ_INT(CUES, 0); ASSERT_EQ_INT(dma(3), 0);
}

TEST(the_bang_countdown_fires_when_it_reaches_zero_or_below_halved_in_the_cockpit) {
    init();
    sound_in_t in = base();
    in.pass_n = 1;
    sound_bang_request(&S, 0x3F, 0x78, 0xFF);                           /* the ejection seat: 0xFF is -1, the next pass makes it -2: it plays at once */
    sound_pass(&S, &in);
    ASSERT_EQ_INT(sound_slot(&S, 2), 6);
    ASSERT_EQ_INT(S.bang_cd, 0);
    sound_stop_all(&S);
    blanks(1);
    sound_pass(&S, &in);
    ASSERT_EQ_INT(sound_slot(&S, 2), SND_NONE);                         /* cleared: no second bang */
    init(); in = base(); in.pass_n = 1;
    sound_bang_request(&S, 0x3F, 0x78, 0xFF);
    sound_pass(&S, &in);
    blanks(1);
    ASSERT_EQ_INT(vol(2), 0x1F);                                        /* (0x3F >> 1, 0x78): 31 in the cockpit */
    init(); in = base(); in.pass_n = 1; in.view = 0xFF;
    sound_bang_request(&S, 0x3F, 0x78, 0xFF);
    sound_pass(&S, &in);
    blanks(1);
    ASSERT_EQ_INT(vol(2), 0x3F);                                        /* the full amplitude outside */
    init(); in = base(); in.pass_n = 1; in.view = 0xFF;
    sound_bang_request(&S, 60, 120, 1);                                 /* a dying record's explosion: (60, 120), countdown 1 */
    sound_pass(&S, &in);
    blanks(1);
    ASSERT_EQ_INT(vol(2), 60);
    init(); in = base(); in.pass_n = 1;
    sound_bang_request(&S, 60, 120, 3);                                 /* 3: two passes pass without a sound */
    sound_pass(&S, &in); ASSERT_EQ_INT(sound_slot(&S, 2), SND_NONE);
    sound_pass(&S, &in); ASSERT_EQ_INT(sound_slot(&S, 2), SND_NONE);
    sound_pass(&S, &in); ASSERT_EQ_INT(sound_slot(&S, 2), 6);
}

TEST(a_failed_bang_posts_the_overspeed_notice_and_the_hold_which_the_same_pass_already_sees) {
    init();
    S.en12 &= ~1;
    sound_in_t in = base();
    in.pass_n = 1;                                                      /* not a retarget pass: the engine block does not run, the notice stays */
    sound_bang_request(&S, 60, 120, 1);
    sound_pass(&S, &in);
    ASSERT_EQ_INT(NOTICE, 0xFA);
    ASSERT_EQ_INT(HOLD, 1);                                             /* 2, counted down in the same pass */
    in.pass_n = 2;
    sound_pass(&S, &in);                                                /* the next pass plays the case 0xFA */
    ASSERT_EQ_INT(per(0), 649 + 0x16);
}

TEST(a_view_switch_a_map_a_pause_and_the_crash_stop_the_voices_the_note_lists) {
    /* the stop table of 4.1 and 7.3 as calls on the module: a view change stops voice 3, M and P and the crash hand-over stop all four */
    init();
    sound_in_t in = base();
    NOTICE = 0xFE;
    sound_pass(&S, &in);
    sound_event(&S, SND_GEAR, 10, 1);
    const int p[9] = { 229, 3, 5, 229, 3, 5, 1, 0, 1 };
    sound_tone(&S, p);
    sound_stop_voice(&S, 3);
    ASSERT_EQ_INT(dma(3), 0); ASSERT_EQ_INT(dma(0), 1); ASSERT_EQ_INT(dma(1), 1); ASSERT_EQ_INT(dma(2), 1);
    sound_tone(&S, p);
    sound_stop_all(&S);
    for (int v = 0; v < 4; v++) ASSERT_EQ_INT(dma(v), 0);
}


/* ---- the library and the mixer together (note 10 4.2, 9.2): the register-write stream of the core played by audio.c --------------------------------------------------------------------------------- */
#define CH_RATE 44100
static int8_t *CH_BUF[SND_OBJECTS];
static int ch_val(int o) { return 3 * o + 1; }                      /* the value of every byte of object o's buffer: the output says which object plays */

static audio_t *chain_mixer(void) {
    audio_t *a = audio_create(CH_RATE, 2);
    if (!a) return NULL;
    audio_set_filter(a, 0);
    audio_set_clock(a, AUDIO_PORT_CLOCK);                           /* the port's clock: a blank is 882 frames, the grid of the steps */
    for (int o = 0; o < SND_OBJECTS; o++) {
        free(CH_BUF[o]); CH_BUF[o] = NULL;
        if (T.obj[o].kind != SOUND_KIND_SAMPLE) continue;
        const uint32_t n = T.obj[o].offset + T.obj[o].length;
        CH_BUF[o] = (int8_t *)malloc(n);
        memset(CH_BUF[o], ch_val(o), n);
        audio_set_sample(a, o, CH_BUF[o], n);
    }
    return a;
}
/* the steps of the core, each handed to the mixer as the game does (drain, then apply) */
static void chain_steps(audio_t *a, int n) {
    static game_audio_cmd_t c[GAME_MAX_AUDIO_CMDS];
    for (int i = 0; i < n; i++) {
        int k = 0;
        uint8_t mt, ms, mr;
        sound_step(&S);
        sound_drain(&S, c, GAME_MAX_AUDIO_CMDS, &k, &mt, &ms, &mr);
        audio_apply(a, c, k);
    }
}
/* the first output frame that shows what begins at colour clock `clk` of the mixer's time (the sample is chosen at the frame's start) */
static long ch_frame(long long clk) { return (long)((clk * CH_RATE + AUDIO_PORT_CLOCK - 1) / AUDIO_PORT_CLOCK); }

TEST(the_music_chain_plays_every_block_of_every_object_the_expected_number_of_times_through_the_mixer) {
    /* the invented table's music, voice 0 (docs/re/10-sound.md 4.2): an object with count c followed by another plays c + 1 blocks; 0xD 14 blocks of 700 samples, 0xF 2 of 600, then the loop 0x11 .. 0x21
     * (3, 1, 3, 1, 3, 1, 3, 1 blocks of 1,500 samples and 4 of 2,500) and back to 0x11. A block lasts length x 358 colour clocks. The expectation is written out from those numbers alone. */
    init();
    audio_t *a = chain_mixer();
    ASSERT_TRUE(a != NULL);
    sound_event(&S, SND_PHRASE, 0, 0x32);
    static const struct { int obj, blocks, len; } plan[] = {
        { 0xD, 14, 700 }, { 0xF, 2, 600 },
        { 0x11, 3, 1500 }, { 0x13, 1, 1500 }, { 0x15, 3, 1500 }, { 0x17, 1, 1500 }, { 0x19, 3, 1500 }, { 0x1B, 1, 1500 }, { 0x1D, 3, 1500 }, { 0x1F, 1, 1500 }, { 0x21, 4, 2500 },
        { 0x11, 3, 1500 }, { 0x13, 1, 1500 }, { 0x15, 3, 1500 }, { 0x17, 1, 1500 }, { 0x19, 3, 1500 }, { 0x1B, 1, 1500 }, { 0x1D, 3, 1500 }, { 0x1F, 1, 1500 }, { 0x21, 4, 2500 },
    };
    long long end = 0;
    const int np = (int)(sizeof plan / sizeof plan[0]);
    for (int i = 0; i < np; i++) end += (long long)plan[i].blocks * plan[i].len * 358;
    const long nframes = ch_frame(end) - 2000;                      /* stay clear of the end of the last block */
    const int nsteps = (int)(end / 71051 / 6) + 4;
    chain_steps(a, nsteps);                                         /* the core runs ahead: all its writes are queued, the mixer plays them in time */
    int16_t *out = (int16_t *)malloc((size_t)nframes * 2 * sizeof(int16_t));
    ASSERT_TRUE(out != NULL);
    audio_render(a, out, (int)nframes);
    long long clk = 0;
    long bad = 0, first_bad = -1;
    for (int i = 0; i < np; i++) {
        for (int b = 0; b < plan[i].blocks; b++) {
            const long f0 = ch_frame(clk), f1 = ch_frame(clk + (long long)plan[i].len * 358);
            for (long f = f0; f < f1 && f < nframes; f++) {
                const int want = ch_val(plan[i].obj) * 63 * 2;
                const int want_r = ch_val(plan[i].obj + 1) * 63 * 2;       /* voice 1 plays the odd object of the pair */
                if (out[2 * f] != want || out[2 * f + 1] != want_r) { if (!bad++) first_bad = f; }
            }
            clk += (long long)plan[i].len * 358;
        }
    }
    if (bad) printf("  %ld frames differ, the first at %ld (object plan index at that frame: left %d)\n", bad, first_bad, out[2 * first_bad]);
    ASSERT_EQ_INT(bad, 0);
    free(out);
    audio_destroy(a);
}

TEST(the_ground_contacts_only_block_plays_to_its_end_and_is_silent_after_it_through_the_mixer) {
    /* object 5, count 1: one block of 1,500 samples at the wrapper's constant period 310 (465,000 colour clocks, 6.54 blanks) with the DMA off in the blank after: the audible block is whole, nothing before or after */
    init();
    audio_t *a = chain_mixer();
    ASSERT_TRUE(a != NULL);
    sound_event(&S, SND_GROUND, 0x30, 1);
    chain_steps(a, 6);
    const long nframes = 12000;
    int16_t *out = (int16_t *)malloc((size_t)nframes * 2 * sizeof(int16_t));
    audio_render(a, out, (int)nframes);
    const long end = ch_frame(1500LL * 310);
    long loud = 0, last = -1;
    for (long f = 0; f < nframes; f++) if (out[2 * f + 1] != 0) { loud++; last = f; }
    ASSERT_EQ_INT(last, end - 1);                                   /* the whole block, to its last sample */
    ASSERT_EQ_INT(loud, end);                                       /* from the first frame without a gap */
    free(out);
    audio_destroy(a);
}


TEST(the_writes_of_a_block_end_carry_the_clock_of_the_end_inside_the_blank) {
    /* docs/re/10-sound.md 4.2 and 9.2: the first object 0xD plays 14 blocks of 700 x 358 = 250,600 colour clocks. The load of the 15th block, the latch of 0xF, is made by the interrupt at the end of the
     * 13th block (a load is one block ahead of the sound): at colour clock 13 x 250,600 = 3,257,800 from the start, blank 45 clock 60,505 (45 x 71,051 = 3,197,295). The writes of the server and of the
     * wrappers carry clock 0. */
    init();
    sound_event(&S, SND_PHRASE, 0, 0x32);
    long long at = -1;
    int wrapper_clocks = 0;
    for (int step = 0; step < 12 && at < 0; step++) {
        game_audio_cmd_t c[GAME_MAX_AUDIO_CMDS];
        int k = 0;
        uint8_t mt, ms, mr;
        sound_step(&S);
        sound_drain(&S, c, GAME_MAX_AUDIO_CMDS, &k, &mt, &ms, &mr);
        for (int i = 0; i < k; i++) {
            if (step == 0 && c[i].blank == 0) wrapper_clocks += c[i].clock;
            if (c[i].op == GAME_AUD_LATCH && c[i].voice == 0 && c[i].object == 0xF) at = (long long)((25 * step) / 4 + c[i].blank) * 71051 + c[i].clock;
        }
    }
    ASSERT_EQ_INT(wrapper_clocks, 0);
    ASSERT_TRUE(at == 13LL * 250600);
}


/* the register writes of the library so far (the drain), for the fallback tests */
static int drained(game_audio_cmd_t *c) {
    int k = 0;
    uint8_t mt, ms, mr;
    sound_drain(&S, c, GAME_MAX_AUDIO_CMDS, &k, &mt, &ms, &mr);
    return k;
}
static int has_write(const game_audio_cmd_t *c, int k, int op, int voice, int object, int value) {
    for (int i = 0; i < k; i++) if (c[i].op == op && c[i].voice == voice && (op != GAME_AUD_LATCH || c[i].object == object) && (op != GAME_AUD_VOLUME || c[i].value == value)) return 1;
    return 0;
}

TEST(objects_the_frontend_could_not_build_clear_their_enable_bits_and_the_wrappers_fall_back_as_note_10_2_5_says) {
    uint8_t present[SND_OBJECTS];
    game_audio_cmd_t c[GAME_MAX_AUDIO_CMDS];
    /* textcpt missing (objects 0 and 1): bit 0 clear, the cockpit pair is the external pair at a quarter of the volume */
    init();
    memset(present, 1, sizeof present);
    present[0] = present[1] = 0;
    sound_objects_available(&S, present);
    ASSERT_TRUE(!(S.en13 & 1));
    ASSERT_TRUE(S.en13 & 2);
    drained(c);
    sound_event(&S, SND_ENGINE_COCKPIT, 764, 40);
    int k = drained(c);
    ASSERT_TRUE(has_write(c, k, GAME_AUD_LATCH, 0, 2, 0) && has_write(c, k, GAME_AUD_LATCH, 1, 3, 0));
    ASSERT_TRUE(has_write(c, k, GAME_AUD_VOLUME, 0, 0, 10) && has_write(c, k, GAME_AUD_VOLUME, 1, 0, 10));
    ASSERT_TRUE(has_write(c, k, GAME_AUD_PERIOD, 0, 0, 764) && has_write(c, k, GAME_AUD_PERIOD, 1, 0, 766));
    /* the same call with the files present plays the cockpit pair itself at the full volume */
    init();
    sound_event(&S, SND_ENGINE_COCKPIT, 764, 40);
    k = drained(c);
    ASSERT_TRUE(has_write(c, k, GAME_AUD_LATCH, 0, 0, 0) && has_write(c, k, GAME_AUD_VOLUME, 0, 0, 40));
    /* textwnd missing (8, 9): the wind stops voices 0 and 1 */
    init();
    sound_event(&S, SND_ENGINE_EXT, 800, 20);
    drained(c);
    ASSERT_TRUE(S.v[0].slot != SND_NONE);
    memset(present, 1, sizeof present);
    present[8] = present[9] = 0;
    sound_objects_available(&S, present);
    ASSERT_TRUE(!(S.en13 & 0x10));
    sound_event(&S, SND_ENGINE_EXT, 800, 20);
    drained(c);
    sound_event(&S, SND_WIND, 8, 0);
    k = drained(c);
    ASSERT_TRUE(S.v[0].slot == SND_NONE && S.v[1].slot == SND_NONE);
    ASSERT_TRUE(has_write(c, k, GAME_AUD_DMA_OFF, 0, 0, 0) && has_write(c, k, GAME_AUD_DMA_OFF, 1, 0, 0));
    /* a music file missing: bit 7 clear, the start-up phrase is the text201 pair at the caller's volume */
    init();
    memset(present, 1, sizeof present);
    present[0x1B] = 0;
    sound_objects_available(&S, present);
    ASSERT_TRUE(!(S.en13 & 0x80));
    sound_event(&S, SND_PHRASE, 0, 0x32);
    k = drained(c);
    ASSERT_TRUE(has_write(c, k, GAME_AUD_LATCH, 0, 0x23, 0) && has_write(c, k, GAME_AUD_LATCH, 1, 0x24, 0));
    ASSERT_TRUE(!has_write(c, k, GAME_AUD_LATCH, 0, 0xD, 0));
    /* the noise buffer missing: the wrapper fails (the caller posts 0xFA) and nothing plays; the gun clone needs it too */
    init();
    memset(present, 1, sizeof present);
    present[6] = 0;
    sound_objects_available(&S, present);
    ASSERT_TRUE(!(S.en12 & 1) && !(S.en13 & 0x20));
    ASSERT_EQ_INT(sound_event(&S, SND_NOISE, 0x1C, 0x30), 1);
    /* the gear sound missing: the gear key clicks instead */
    init();
    memset(present, 1, sizeof present);
    present[0xB] = 0;
    sound_objects_available(&S, present);
    ASSERT_EQ_INT(sound_gear_key(&S), 0);
    /* nothing missing: the table is as sound_init left it */
    init();
    memset(present, 1, sizeof present);
    const uint8_t en12 = S.en12, en13 = S.en13;
    sound_objects_available(&S, present);
    ASSERT_TRUE(S.en12 == en12 && S.en13 == en13);
}

int main(void) {
    printf("test_sound\n");
    RUN(the_noise_buffer_is_the_first_bits_of_the_register_and_the_first_wind_draws_are_32_and_62);
    RUN(the_tone_script_plays_two_segments_then_holds_and_stops_at_the_formula_blank);
    RUN(a_tone_with_a_repeat_count_of_zero_never_ends_and_the_next_tone_replaces_it);
    RUN(a_tone_with_a_last_wait_of_zero_stops_after_the_first_outer_repeat);
    RUN(the_noise_fades_linearly_and_stops_after_the_duration);
    RUN(a_missing_noise_sound_reports_failure_for_the_caller_to_post_the_notice);
    RUN(the_gun_burst_has_six_pulses_then_a_tail_and_keeps_the_period_it_ends_on);
    RUN(the_launch_ramps_up_then_fades_for_sixty_blanks_at_a_constant_pitch);
    RUN(the_gear_sound_slides_from_700_to_595_in_fifteen_blanks_and_runs_until_stopped);
    RUN(a_block_ends_after_its_samples_at_the_periods_in_force_and_the_handler_writes_the_fields_one_step_ahead);
    RUN(a_glide_inside_a_block_changes_when_it_ends);
    RUN(the_ground_contact_plays_one_block_at_a_constant_period_and_needs_the_cameras_record);
    RUN(a_count_c_object_with_a_successor_plays_c_plus_1_blocks_and_the_chain_follows_the_next_links);
    RUN(the_chain_loops_back_to_the_first_loop_object_after_the_last_and_voice_1_follows_in_step);
    RUN(counts_persist_across_a_stop_and_the_music_after_a_flight_plays_the_intro_objects_one_block_each);
    RUN(the_master_level_ramps_a_quarter_unit_per_blank_while_the_flag_is_set_and_caps_the_volumes);
    RUN(the_drain_hands_over_the_register_writes_with_their_blanks_and_the_master_inputs);
    RUN(a_step_runs_six_six_six_and_seven_blanks_and_stamps_them);
    RUN(stop_all_stops_the_playing_voices_in_order_and_a_voice_stop_leaves_the_others);
    RUN(cockpit_normal_engine_case_fe_starts_the_cockpit_pair_on_voices_0_and_1);
    RUN(the_engine_volume_is_halved_in_the_cockpit_under_strain_and_floored_at_0x28);
    RUN(the_throttle_term_uses_the_turn_rates_and_is_not_bounded_below);
    RUN(outside_view_case_fe_uses_the_external_pair_and_the_view_terms);
    RUN(afterburner_on_and_off_cases_in_the_cockpit);
    RUN(the_launch_notice_plays_the_launch_and_clears_the_hold_and_the_overspeed_notice_the_external_pair);
    RUN(other_notices_glide_the_voices_over_32_blanks_and_the_hold_picks_the_volume);
    RUN(the_engine_is_retargeted_every_fourth_pass_only_and_the_hold_counts_down_in_every_pass);
    RUN(throttle_zero_in_the_air_is_the_wind_and_on_the_ground_rolling_the_engine_at_the_idle_volume);
    RUN(a_record_of_class_3x_hears_only_the_wind_at_volume_0x10);
    RUN(the_gates_stop_the_whole_routine_and_leave_the_notice_alone);
    RUN(the_gear_countdown_starts_the_sound_in_the_key_pass_and_stops_it_two_ticks_later);
    RUN(the_seconds_tick_clears_bit_7_first_and_counts_the_cool_down);
    RUN(the_cue_player_plays_one_cue_in_the_notes_order_clears_the_long_and_the_cool_down_holds_it);
    RUN(the_bang_countdown_fires_when_it_reaches_zero_or_below_halved_in_the_cockpit);
    RUN(a_failed_bang_posts_the_overspeed_notice_and_the_hold_which_the_same_pass_already_sees);
    RUN(a_view_switch_a_map_a_pause_and_the_crash_stop_the_voices_the_note_lists);
    RUN(the_writes_of_a_block_end_carry_the_clock_of_the_end_inside_the_blank);
    RUN(objects_the_frontend_could_not_build_clear_their_enable_bits_and_the_wrappers_fall_back_as_note_10_2_5_says);
    RUN(the_music_chain_plays_every_block_of_every_object_the_expected_number_of_times_through_the_mixer);
    RUN(the_ground_contacts_only_block_plays_to_its_end_and_is_silent_after_it_through_the_mixer);
    TEST_MAIN_END();
}
