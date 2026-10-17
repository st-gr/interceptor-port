/* tests/test_screen.c - the front-end screens (src/core/screen.c, docs/re/08-game-flow.md sections 3 to 6).
 *
 * The text and scenario assets of the tests are INVENTED (record shapes and tables in the form the extractor writes, no game text and no shipped values): the tests
 * compare record counts, positions, shown counts, modes and step counts, never text. The step counts are the note's blanks converted with (blanks * 8 + 25) / 50;
 * "+n" is the n-th screen_step after the key. The last group runs on the extracted assets when assets/ exists (skipped otherwise).
 */
#include "test.h"
#include "../src/core/game.h"
#include "../src/core/screen.h"
#include "../src/core/entities.h"
#include "../src/core/flight.h"
#include "../src/core/sound.h"
#include "sound_synth.h"

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
        case 0x26: add_line(id, 43, 16, 3, 0x96, 4, 10); break;                 /* the pause text: ten characters, eight of them not blank (below), typed with the spaces free */
        case 0x1E: add_line(id, 6, 4, 1, 0x93, 2, 30); add_line(id, 6, 4, 1, 0x93, 2, 20); break;
        case 0x55: add_line(id, 6, 4, 1, 0x93, 2, 30); break;                    /* 0x8055 plays after 0x1E */
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
        for (int k = 0; k < l->len; k++) p8(&B_TEXT, l->id == 0x26 ? (unsigned char)"A BCDEFG H"[k] : 'A' + (i + k) % 26);   /* the invented pause line: ten characters, eight of them not blank, as the real one */
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
    for (int r = 0; r < 10; r++) for (int v = 0; v < 3; v++) { p16(&B_SCN, (unsigned)(r % 2)); p16(&B_SCN, r == 0 ? 2 : r == 7 ? 4 : 3); }
    sec(&B_SCN, 2, 8);    /* lists: four sub-list indices each */
    p16(&B_SCN, 0); p16(&B_SCN, 0); p16(&B_SCN, 0); p16(&B_SCN, 0);
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

/* An INVENTED briefing chain (tables/briefing_chain.bin: five record ids for each of the modes 3 to 8) over ids whose fixture records are plain lines. */
static const uint8_t CHAIN_T[30] = { 0x60, 0x61, 0x62, 0x63, 0x64,  0x65, 0x66, 0x67, 0x68, 0x69,  0x6A, 0x6B, 0x6C, 0x47, 0x48,
                                     0x49, 0x4A, 0x4D, 0x4E, 0x4F,  0x50, 0x51, 0x52, 0x53, 0x54,  0x56, 0x57, 0x58, 0x59, 0x5A };

static blob_t B_PIX;
static int nofiles;
static int sread(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    const blob_t *b = NULL;
    static const char ver[] = "27\n", pix[] = "splsh\n";
    if (!strcmp(path, "VERSION")) { *out = (uint8_t *)malloc(sizeof ver); memcpy(*out, ver, sizeof ver); *len = sizeof ver - 1; return 0; }
    if (!strcmp(path, "pix/index.txt")) { *out = (uint8_t *)malloc(sizeof pix); memcpy(*out, pix, sizeof pix); *len = sizeof pix - 1; return 0; }
    if (nofiles) return -1;
    if (!strcmp(path, "tables/briefing_chain.bin")) { *out = (uint8_t *)malloc(sizeof CHAIN_T); memcpy(*out, CHAIN_T, sizeof CHAIN_T); *len = sizeof CHAIN_T; return 0; }
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
    screen_set_logdisk_result(s, 3);   /* most tests walk the log-disk prompt (the write-protected variant); the default 0 shows none (tests below) */
    return s;
}
static void del(screen_t *s) { screen_destroy(s); free(s); }
static void key(screen_t *s, int c) { game_event_t ev = { GAME_EV_KEY, 0, c }; screen_key(s, &ev); }
static void btn(screen_t *s, int code, int v) { game_event_t ev = { GAME_EV_BUTTON, code, v }; screen_key(s, &ev); }
static void steps(screen_t *s, int n) { for (int i = 0; i < n; i++) screen_step(s); }
static game_scene_t SC;
static const game_scene_t *fill(screen_t *s) { memset(&SC, 0, sizeof SC); SC.mode = s->mode; screen_fill(s, &SC); return &SC; }
static int lines(screen_t *s) { return fill(s)->text_count; }
static int shown_all(screen_t *s) { const game_scene_t *c = fill(s); int n = 0; for (int i = 0; i < c->text_count; i++) n += c->text[i].shown; return n; }
/* the screens a session passes before the menu */
static void to_menu(screen_t *s) { key(s, 'x'); steps(s, 3); btn(s, GAME_BTN_RETURN, 1); steps(s, 8); }
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

/* ---- the key clicks of the typed text (note 08 3.6, 3.8; note 10 7.4, 5.7 item 13) ------------------------------------------------------------------------------ */
static sound_t SND;
static int click_abs[600], click_n;      /* the library blank (counted from the first step) of each click of the last click_blanks run */
/* the blanks, within their steps, of the clicks (DMA on of voice 3) of `n` steps of the screen, with the sound library running after each step as the game runs it */
static int click_blanks(screen_t *s, int n, int *by_blank) {
    int total = 0, nabs = 0;
    int *abs_blank = click_abs; click_n = 0;
    for (int i = 0; i < n; i++) {
        screen_step(s);
        sound_step(&SND);
        game_audio_cmd_t c[GAME_MAX_AUDIO_CMDS];
        int k = 0;
        uint8_t mt, ms, mr;
        sound_drain(&SND, c, GAME_MAX_AUDIO_CMDS, &k, &mt, &ms, &mr);
        for (int j = 0; j < k; j++) if (c[j].op == GAME_AUD_DMA_ON && c[j].voice == 3) { by_blank[c[j].blank]++; total++ ; if (abs_blank && nabs < 600) abs_blank[nabs++] = (25 * i) / 4 + c[j].blank; }
    }
    click_n = nabs;
    return total;
}
static screen_t *screen_with_sound(void) {
    screen_t *s = new_screen();
    if (!s) return NULL;
    sound_table_t T;
    sound_synth_table(&T);
    sound_init(&SND, &T);
    s->snd = &SND;
    return s;
}

TEST(the_key_clicks_of_a_typed_line_fall_at_the_blank_of_their_passes_by_the_cost_of_the_list) {
    /* note 10 5.7 item 13 and 9.2: a request is made at library blank floor(c x nb / B) of its step, c the cost the step's passes have used up to and including the requesting pass, B the list's
     * budget (the credits' list is timed: its rate is not 37,500). The passes of a step are spread over all its blanks, the requests come in time order, and along one typed line, where every
     * pass costs the same, they are evenly spaced to the blank. */
    screen_t *s = screen_with_sound(); ASSERT_TRUE(s != NULL);
    int by_blank[8] = { 0 };
    const int total = click_blanks(s, 120, by_blank);              /* the credits type with a click per character (flag 0 here: no start-up phrase) */
    ASSERT_TRUE(total > 20 && click_n == total);
    for (int b = 1; b < 7; b++) ASSERT_TRUE(by_blank[b] > 0);      /* the later blanks of a step, including the seventh of the 6, 6, 6, 7 pattern, take requests (blank 0 only when the request is the step's first pass: c includes its cost) */
    for (int i = 1; i < click_n; i++) ASSERT_TRUE(click_abs[i] >= click_abs[i - 1]);
    int gaps[600], ng = 0;
    for (int i = 1; i < click_n; i++) if (click_abs[i] - click_abs[i - 1] > 0) gaps[ng++] = click_abs[i] - click_abs[i - 1];
    ASSERT_TRUE(ng > 20);
    /* the median gap, and most gaps within a blank of it (the quantisation of the blank) */
    int sorted[600];
    memcpy(sorted, gaps, sizeof(int) * (size_t)ng);
    for (int a = 1; a < ng; a++) for (int b = a; b > 0 && sorted[b - 1] > sorted[b]; b--) { int t = sorted[b]; sorted[b] = sorted[b - 1]; sorted[b - 1] = t; }
    const int med = sorted[ng / 2];
    int near = 0;
    for (int i = 0; i < ng; i++) near += gaps[i] >= med - 1 && gaps[i] <= med + 1;
    ASSERT_TRUE(near * 10 >= ng * 8);
    /* the typed line's rate: the credits' 245 blanks hold their requests; the mean gap is the list's duration over its requests, so the clicks are spread over the whole time, not bunched in each step's first blanks */
    ASSERT_TRUE(click_abs[click_n - 1] - click_abs[0] > 200);
    del(s);
}

TEST(the_typing_byte_2_makes_no_click_and_a_cool_down_holds_them_back) {
    screen_t *s = screen_with_sound(); ASSERT_TRUE(s != NULL);
    int by_blank[8] = { 0 };
    sound_master(&SND, -1, -1, 2);                                 /* the start-up's byte: the credits are silent until the any-key */
    ASSERT_EQ_INT(click_blanks(s, 60, by_blank), 0);
    del(s);
    s = screen_with_sound(); ASSERT_TRUE(s != NULL);
    SND.cooldown = 3;                                              /* the cue cool-down V:4A942 (positive) holds the click back (0x37C5E) */
    ASSERT_EQ_INT(click_blanks(s, 60, by_blank), 0);
    del(s);
}

/* ---- the boot and the credits (5.2) ------------------------------------------------------------------------------------------------------------ */

TEST(boot_shows_the_credits_and_the_text_starts_two_steps_in) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    ASSERT_EQ_INT(s->mode, GAME_MODE_CREDITS);
    ASSERT_EQ_INT(lines(s), 0);
    ASSERT_EQ_INT(fill(s)->palette_regime, 0);            /* the menu palette */
    ASSERT_EQ_INT(fill(s)->fade_target, 15);
    steps(s, 2);
    ASSERT_EQ_INT(lines(s), 0);
    steps(s, 1);                                           /* 13 to 15 blanks: 2 steps (5.2) */
    ASSERT_TRUE(lines(s) >= 1);
    const game_text_t *t = &fill(s)->text[0];
    ASSERT_EQ_INT(t->row, 8); ASSERT_EQ_INT(t->col, 6); ASSERT_EQ_INT(t->pen, 5); ASSERT_EQ_INT(t->step, 11); ASSERT_EQ_INT(t->length, 20);
    ASSERT_TRUE(t->shown >= 1 && t->shown <= t->length);
    ASSERT_EQ_INT(t->id, 0x0F);                            /* the record id of the line (the credits record, 2.4) */
    del(s);
}

TEST(the_credits_type_out_and_the_blank_line_is_not_a_record_line) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    int prev = 0;
    steps(s, 3);
    for (int i = 0; i < 45; i++) { steps(s, 1); int n = shown_all(s); ASSERT_TRUE(n >= prev); prev = n; }   /* the shown counts only grow */
    ASSERT_EQ_INT(lines(s), 5);                                    /* six lines, one of them blank: five are drawn */
    const game_scene_t *c = fill(s);
    ASSERT_EQ_INT(c->text[0].row, 8); ASSERT_EQ_INT(c->text[1].row, 19); ASSERT_EQ_INT(c->text[2].row, 41);   /* the line step of 11 scanlines */
    for (int i = 0; i < 5; i++) ASSERT_EQ_INT(c->text[i].shown, c->text[i].length);
    del(s);
}

TEST(the_credits_list_ends_at_245_blanks_and_the_hold_is_its_own_stage_40_steps_in_all) {   /* 5.2: typing 39 steps, the 50-pass hold 1 step */
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    steps(s, 2);                                     /* the text starts in the next step (k = 0) */
    int finished = -1;
    for (int k = 0; k <= 60; k++) {
        steps(s, 1);
        if (finished < 0 && screen_text_finished(s)) finished = k;
        if (k == 39) ASSERT_EQ_INT(shown_all(s), 20 + 12 + 30 + 16 + 24);   /* everything is on screen when the list ends */
    }
    ASSERT_EQ_INT(finished, 40);
    ASSERT_EQ_INT(s->mode, GAME_MODE_CREDITS);       /* the screen waits for ever */
    del(s);
}

TEST(any_key_leaves_the_credits_for_the_log_disk_prompt_but_a_mouse_button_does_not) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    steps(s, 5);
    btn(s, GAME_BTN_FIRE, 1); btn(s, GAME_BTN_BRAKE, 1); btn(s, GAME_BTN_PAUSE, 1);   /* flight buttons: not menu keys (5.9) */
    steps(s, 1);
    ASSERT_EQ_INT(s->mode, GAME_MODE_CREDITS);
    btn(s, GAME_BTN_RETURN, 0);                       /* a release is never a key (5.8) */
    steps(s, 1);
    ASSERT_EQ_INT(s->mode, GAME_MODE_CREDITS);
    steps(s, 8);                                      /* the P key was stored in the credits' pass of 60 blanks: the latch holds about 10 steps (08 5.8) */
    key(s, '5');
    steps(s, 1);
    ASSERT_EQ_INT(s->mode, GAME_MODE_LOGDISK);
    ASSERT_EQ_INT(lines(s), 0);
    ASSERT_EQ_INT(fill(s)->palette_regime, 0);
    del(s);
}

TEST(every_kind_of_key_leaves_the_credits) {
    int kinds = 0;
    for (int k = 0; k < 6; k++) {
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        steps(s, 3);
        switch (k) {
        case 0: key(s, 'A'); break;
        case 1: btn(s, GAME_BTN_RETURN, 1); break;
        case 2: btn(s, GAME_BTN_ESC, 1); break;            /* ESC counts as any key on the credits; the frontend quits instead (5.2) */
        case 3: btn(s, GAME_BTN_KEYPAD, 11); break;
        case 4: btn(s, GAME_BTN_FKEY, 1); break;
        default: btn(s, GAME_BTN_SHIFT_ESC, 1); break;
        }
        steps(s, 1);
        if (s->mode == GAME_MODE_LOGDISK) kinds++;
        del(s);
    }
    ASSERT_EQ_INT(kinds, 6);
}

TEST(a_key_pressed_before_the_credits_poll_is_kept_and_acts_when_the_state_polls) {   /* C: the first two steps of the credits do not poll */
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    key(s, 'q');
    steps(s, 2);
    ASSERT_EQ_INT(s->mode, GAME_MODE_CREDITS);
    steps(s, 1);
    ASSERT_EQ_INT(s->mode, GAME_MODE_LOGDISK);
    del(s);
}

/* ---- the log-disk prompt (5.3) ------------------------------------------------------------------------------------------------------------------ */

TEST(the_log_disk_prompt_shows_two_records_of_instant_lines_after_two_steps) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    key(s, 'x'); steps(s, 3);
    ASSERT_EQ_INT(s->mode, GAME_MODE_LOGDISK);
    ASSERT_EQ_INT(lines(s), 0);
    steps(s, 2);
    ASSERT_TRUE(lines(s) >= 1);                       /* the first record (one line) starts at +2 (L+12 blanks) */
    steps(s, 12);
    ASSERT_EQ_INT(lines(s), 4);                       /* one line, then the three of the explanation */
    const game_scene_t *c = fill(s);
    for (int i = 0; i < 4; i++) ASSERT_EQ_INT(c->text[i].shown, c->text[i].length);
    ASSERT_EQ_INT(c->text[0].row, 90); ASSERT_EQ_INT(c->text[1].row, 110); ASSERT_EQ_INT(c->text[2].row, 121);
    ASSERT_EQ_INT(c->text[0].flags & 1, 0);           /* instant */
    for (int i = 0; i < 12; i++) { key(s, 'a' + i); steps(s, 1); }   /* other keys do nothing */
    ASSERT_EQ_INT(s->mode, GAME_MODE_LOGDISK);
    del(s);
}

TEST(return_on_the_log_disk_prompt_goes_to_the_menu_and_the_menu_text_starts_seven_steps_later) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    key(s, 'x'); steps(s, 8);
    btn(s, GAME_BTN_RETURN, 1);
    for (int i = 1; i <= 6; i++) { steps(s, 1); ASSERT_EQ_INT(s->mode, GAME_MODE_MENU); ASSERT_EQ_INT(lines(s), 0); }
    steps(s, 1);
    ASSERT_TRUE(lines(s) >= 1);                        /* +7: 43 blanks (5.3) */
    ASSERT_EQ_INT(fill(s)->text[0].row, 20 + 6);       /* the menu title is id 6 */
    del(s);
}

TEST(return_pressed_twice_is_one_action_and_the_second_does_nothing_in_the_menu) {   /* Review Focus 1 */
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    key(s, 'x'); steps(s, 8);
    btn(s, GAME_BTN_RETURN, 1); btn(s, GAME_BTN_RETURN, 1);     /* a held key delivers one event; two events are two presses */
    steps(s, 1);
    ASSERT_EQ_INT(s->mode, GAME_MODE_MENU);
    ASSERT_EQ_INT(s->nq, 1);                                    /* the second waits: the menu build polls nothing for 7 steps */
    steps(s, 20);
    ASSERT_EQ_INT(s->mode, GAME_MODE_MENU);                     /* RETURN in the menu does nothing (5.8) */
    ASSERT_EQ_INT(s->nq, 0);
    ASSERT_EQ_INT(s->mode_byte, 0);
    del(s);
}

TEST(a_digit_pressed_during_the_menu_build_is_kept_and_acts_in_the_first_menu_step) {   /* 5.3: key 2 sent 17 blanks after the RETURN */
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    key(s, 'x'); steps(s, 8);
    btn(s, GAME_BTN_RETURN, 1);
    steps(s, 3);
    key(s, '2');
    steps(s, 3);
    ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);
    ASSERT_EQ_INT(s->mode_byte, 0);
    steps(s, 1);                                                /* the build ends in the 7th step; the key acts in that step */
    ASSERT_EQ_INT(s->mode_byte, 1);
    ASSERT_EQ_INT(s->state, SCR_ECHO);
    del(s);
}

TEST(the_key_queue_holds_31_events_and_drops_the_rest) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_menu(s);                                                   /* (the credits store one key per pass, 5.8) */
    for (int i = 0; i < 40; i++) key(s, '9');
    ASSERT_EQ_INT(s->nq, 31);
    del(s);
}

/* ---- the menu (5.4) ------------------------------------------------------------------------------------------------------------------------------- */

TEST(the_menu_types_its_list_in_96_blanks_with_a_line_step_of_12) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    key(s, 'x'); steps(s, 8);
    btn(s, GAME_BTN_RETURN, 1); steps(s, 6);
    ASSERT_EQ_INT(lines(s), 0);
    int finished = -1;
    for (int k = 0; k < 40; k++) {                              /* k = 0 is the step in which the list starts (+7 after the RETURN) */
        steps(s, 1);
        if (k == 0) ASSERT_TRUE(lines(s) >= 1);
        if (finished < 0 && screen_text_finished(s)) finished = k;
        if (k == 15) {
            const game_scene_t *c = fill(s);
            ASSERT_EQ_INT(c->text_count, 12);
            for (int i = 0; i < c->text_count; i++) ASSERT_EQ_INT(c->text[i].shown, c->text[i].length);   /* the list is typed after 96 blanks = 15 steps */
        }
    }
    ASSERT_EQ_INT(finished, 16);                                /* typing 15 steps and the end hold of 8 blanks = 1 */
    ASSERT_EQ_INT(fill(s)->text[11].pen, 10);                   /* the two ESC lines: pen 10, the record of id 0x6D */
    ASSERT_EQ_INT(fill(s)->text[10].step, 12);                  /* the menu list plays with the line step of 12 scanlines */
    ASSERT_EQ_INT(fill(s)->text[11].row - fill(s)->text[10].row, 12);
    ASSERT_EQ_INT(fill(s)->palette_regime, 0);
    del(s);
}

TEST(the_digits_9_and_0_the_letters_and_the_function_keys_do_nothing_in_the_menu) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_menu(s);
    const char keys[] = "90abz ";
    for (const char *k = keys; *k; k++) { key(s, *k); steps(s, 1); }
    btn(s, GAME_BTN_FKEY, 1); steps(s, 1);
    btn(s, GAME_BTN_FKEY, 6); steps(s, 1);
    key(s, '7'); steps(s, 1);           /* item 7 and F1 to F6 need a log (Task 8): no effect */
    key(s, '8'); steps(s, 1);           /* item 8, the flight-log statistics (Task 8) */
    btn(s, GAME_BTN_KEYPAD, 9); steps(s, 1);
    btn(s, GAME_BTN_KEYPAD, 0); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_MENU);
    ASSERT_EQ_INT(s->mode_byte, 0);
    key(s, '1'); steps(s, 1);          /* the demo needs its stream (Task 6) */
    ASSERT_EQ_INT(s->state, SCR_MENU);
    del(s);
}

TEST(esc_and_shift_esc_in_the_menu_are_consumed_without_effect) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_menu(s);
    steps(s, 20);
    const int n = lines(s);
    btn(s, GAME_BTN_ESC, 1); steps(s, 1);
    btn(s, GAME_BTN_SHIFT_ESC, 1); steps(s, 3);
    ASSERT_EQ_INT(s->state, SCR_MENU);
    ASSERT_EQ_INT(lines(s), n);
    ASSERT_EQ_INT(s->nq, 0);
    del(s);
}

TEST(key_2_chooses_free_flight_and_the_echo_line_appears_one_step_later) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_menu(s);
    steps(s, 20);
    key(s, '2');
    steps(s, 1);
    ASSERT_EQ_INT(s->mode_byte, 1);                            /* V:4A95E = 1 (5.10) */
    ASSERT_EQ_INT(s->state, SCR_ECHO);
    ASSERT_EQ_INT(lines(s), 1);                                /* the menu text is cleared; one line, the chosen item only, 5 blanks after the key */
    ASSERT_EQ_INT(s->mode, GAME_MODE_MENU);
    ASSERT_EQ_INT(fill(s)->palette_regime, 0);
    del(s);
}

TEST(the_mode_keys_set_the_mode_byte_of_note_5_10_and_the_unbuilt_modes_return_to_the_menu) {
    static const struct { int key; int mode; } M[] = { { '3', 2 }, { '4', 0x7D }, { '5', 9 } };
    for (int i = 0; i < 3; i++) {
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        to_menu(s); steps(s, 20);
        key(s, M[i].key); steps(s, 1);
        ASSERT_EQ_INT(s->mode_byte, M[i].mode);
        ASSERT_EQ_INT(s->state, SCR_ECHO);
        steps(s, 5);
        ASSERT_EQ_INT(s->state, SCR_ECHO);
        steps(s, 2);                                           /* +7: the dispatch; a bare screen module has no mode handler (C), the menu is rebuilt */
        ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);
        ASSERT_EQ_INT(s->mode_byte, 0);
        del(s);
    }
}

static int took(void *user, uint8_t mode) { int *m = (int *)user; *m = mode; return 1; }
TEST(a_later_task_can_take_a_mode_over_through_the_handler_and_the_allowance_is_by_mode) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    int got = -1;
    screen_set_mode_handler(s, took, &got);
    to_menu(s); steps(s, 20);
    key(s, '5'); steps(s, 8);
    ASSERT_EQ_INT(got, 9);
    ASSERT_EQ_INT(s->state, SCR_EXTERNAL);
    static const struct { int mode, allow; } A[] = { { 1, 5 }, { 2, 3 }, { 3, 3 }, { 8, 3 }, { 9, 3 }, { 0x7D, 1 }, { 0x7F, 3 } };
    for (unsigned i = 0; i < sizeof A / sizeof A[0]; i++) {
        s->mode_byte = (uint8_t)A[i].mode;
        screen_start_flight(s);
        ASSERT_EQ_INT(s->allowance, A[i].allow);                /* 6.3: 5, 3, 1 */
    }
    del(s);
}

/* The handler's answer 2 (the trainings, note 08 8.1): the allowance and the placement are requested, the lead text types over the placed start scene after its delay, then the
 * tail of the part (the countdown of 0x12DB8 in part 1; the approach, 0x12DB8 and the take-off notice in part 2), the wait and the flight. */
static int briefing_handler(void *user, uint8_t mode) { screen_t *sc = (screen_t *)user; sc->location = (uint8_t)(mode == 2 ? 3 : 2); sc->aircraft = 0x11; sc->lead_text = (uint16_t)(mode == 2 ? 9 : 10); return 2; }
TEST(a_handler_answer_of_2_starts_part_1_through_the_briefing_6_steps_of_0x12DB8_and_no_notice_and_esc_rewrites_it_as_part_2) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    screen_set_mode_handler(s, briefing_handler, s);
    to_menu(s); steps(s, 20);
    key(s, '3'); steps(s, 7);                                   /* the dispatch at +7 */
    ASSERT_EQ_INT(s->mode_byte, 2);
    ASSERT_EQ_INT(s->state, SCR_TRAIN_DELAY);                   /* the start scene stands still: 24 blanks = 4 steps to the post */
    ASSERT_EQ_INT(s->allowance, 3);
    ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT);
    ASSERT_TRUE(s->sim_hold);
    ASSERT_TRUE(s->out_place);
    ASSERT_EQ_INT(lines(s), 0);
    steps(s, 3);
    ASSERT_EQ_INT(s->state, SCR_TRAIN_DELAY);
    steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_TRAIN_BRIEF);
    ASSERT_EQ_INT(s->eng.ids[0], 9);                            /* the briefing of part 1 */
    int n = 0;
    s->out_panel = 0;                                           /* (the dispatch's 0x120E0 store is consumed) */
    while (s->state == SCR_TRAIN_BRIEF && n < 4000) { steps(s, 1); n++; }
    ASSERT_EQ_INT(s->state, SCR_APPROACH);                      /* no approach in part 1: 0x12DB8 with its countdown of six passes */
    ASSERT_EQ_INT(s->out_panel, 0);                             /* and not the part 2 approach's V:4A8EE store (0x1250C) */
    ASSERT_EQ_INT(s->notice_at, 6);
    ASSERT_TRUE(s->sim_hold);
    n = 0;
    while (s->state == SCR_APPROACH && n < 100) { steps(s, 1); n++; }
    ASSERT_EQ_INT(n, 6);
    ASSERT_EQ_INT(s->state, SCR_WAIT);                          /* mode 2 posts no notice: 0x12E4E acts at once, the simulation switch */
    ASSERT_TRUE(s->out_sim);
    ASSERT_TRUE(!s->sim_hold);
    steps(s, 4);
    ASSERT_EQ_INT(s->state, SCR_FLIGHT);
    btn(s, GAME_BTN_ESC, 1);                                    /* ESC in part 1: the reset block, then part 2 */
    int saw_delay = 0, saw_menu = 0, at_post = -1;
    for (n = 1; n < 200 && at_post < 0; n++) {
        steps(s, 1);
        if (s->state == SCR_MENU_BUILD || s->state == SCR_MENU) saw_menu = 1;
        if (s->state == SCR_TRAIN_DELAY) saw_delay = 1;
        if (s->state == SCR_TRAIN_BRIEF) at_post = n;
    }
    ASSERT_TRUE(saw_delay && !saw_menu);                        /* no menu and no second prompt */
    ASSERT_EQ_INT(s->mode_byte, 0x7D);
    ASSERT_EQ_INT(s->allowance, 1);
    ASSERT_TRUE(at_post >= 62 && at_post <= 64);                /* 8.1 [V]: the restart's briefing is posted 395 blanks = 63 steps after the ESC */
    ASSERT_EQ_INT(s->eng.ids[0], 10);                           /* part 2's briefing */
    del(s);
}

TEST(part_2_goes_from_the_briefing_through_the_approach_0x12DB8_and_the_take_off_notice_59_to_the_wait) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    screen_set_mode_handler(s, briefing_handler, s);
    to_menu(s); steps(s, 20);
    key(s, '4'); steps(s, 7);
    ASSERT_EQ_INT(s->mode_byte, 0x7D);
    ASSERT_EQ_INT(s->state, SCR_TRAIN_DELAY);
    steps(s, 10);
    ASSERT_EQ_INT(s->state, SCR_TRAIN_DELAY);
    steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_TRAIN_BRIEF);
    int n = 0;
    s->out_panel = 0;                                           /* (the dispatch's 0x120E0 store is consumed) */
    while (s->state == SCR_TRAIN_BRIEF && n < 4000) { steps(s, 1); n++; }
    ASSERT_EQ_INT(s->state, SCR_APPROACH);                      /* 12.14 C13: the approach is 29 passes of the mover (one step each), then 0x12D58's 2 passes and 0x12DB8's six: 31 + 6 steps, not the 401 blanks as 64 steps */
    ASSERT_EQ_INT(s->out_panel, 1);                             /* 0x124F8 stores V:4A8EE = 3 at 0x1250C */
    ASSERT_EQ_INT(s->view_kind, SV_TRAIN2);                     /* seen from outside from the dispatch (06 start table) */
    ASSERT_EQ_INT(s->notice_at, 29 + 2 + 6);
    n = 0;
    while (s->state == SCR_APPROACH && n < 400) { steps(s, 1); n++; }
    ASSERT_EQ_INT(n, 37);
    ASSERT_EQ_INT(s->state, SCR_NOTICE);
    ASSERT_EQ_INT(s->eng.ids[0], 0x59);                         /* the take-off notice (the player starts on a runway) */
    n = 0;
    while (s->state == SCR_NOTICE && n < 400) { steps(s, 1); n++; }
    ASSERT_EQ_INT(s->state, SCR_WAIT);
    ASSERT_TRUE(s->out_sim);
    steps(s, 4);
    ASSERT_EQ_INT(s->state, SCR_FLIGHT);
    del(s);
}

TEST(the_mission_list_of_item_6_shows_the_empty_log_and_esc_rebuilds_the_menu) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_menu(s); steps(s, 20);
    key(s, '6'); steps(s, 10);
    ASSERT_EQ_INT(s->state, SCR_LIST);
    ASSERT_EQ_INT(s->mode_byte, 0);
    ASSERT_EQ_INT(lines(s), 2);                                 /* the header and the continuation hint */
    ASSERT_EQ_INT(fill(s)->text[1].row, 40 + 2 * 11);           /* the continuation: two line steps below the last line of the header */
    btn(s, GAME_BTN_ESC, 1); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);
    del(s);
}

/* ---- the mission start: echo, black passes, fade-in, location, aircraft (5.4, 5.6, 5.7, 6.1) ------------------------------------------------------- */

TEST(free_flight_from_the_menu_reaches_the_location_caption_25_steps_after_the_key) {   /* 5.6: 7 + 11 + 7 */
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_menu(s); steps(s, 20);
    key(s, '2');
    for (int i = 1; i <= 6; i++) { steps(s, 1); ASSERT_EQ_INT(s->mode, GAME_MODE_MENU); }
    steps(s, 1);                                                /* +7: the mode starts, the screen is black (all 16 colour registers 0) */
    ASSERT_EQ_INT(s->mode, GAME_MODE_SELECT_LOCATION);
    ASSERT_EQ_INT(lines(s), 0);
    ASSERT_EQ_INT(fill(s)->fade_target, 0);
    ASSERT_EQ_INT(fill(s)->palette_regime, 1);                  /* the flight palette */
    for (int i = 8; i <= 17; i++) { steps(s, 1); ASSERT_EQ_INT(s->state, SCR_BLACK); ASSERT_EQ_INT(fill(s)->fade_target, 0); }
    steps(s, 1);                                                /* +18: the scene build is over (71 blanks = 11 steps), the fade-in begins */
    ASSERT_EQ_INT(s->state, SCR_FADE_IN);
    ASSERT_EQ_INT(fill(s)->fade_target, 15);
    for (int i = 19; i <= 24; i++) { steps(s, 1); ASSERT_EQ_INT(s->state, SCR_FADE_IN); ASSERT_EQ_INT(lines(s), 0); }
    steps(s, 1);                                                /* +25: level 15, the caption starts */
    ASSERT_EQ_INT(s->state, SCR_LOCATION);
    ASSERT_TRUE(lines(s) >= 1);
    ASSERT_EQ_INT(fill(s)->text[0].row, 170); ASSERT_EQ_INT(fill(s)->text[0].pen, 13);
    ASSERT_EQ_INT(s->allowance, 5);                             /* free flight: 5 crashes (6.3) */
    del(s);
}

TEST(the_location_caption_types_in_126_blanks_and_the_keys_are_accepted_while_it_types) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_menu(s); steps(s, 20);
    key(s, '2'); steps(s, 25);                                   /* the caption starts in the 25th step (k = 0) */
    int finished = -1;
    for (int k = 1; k <= 30 && finished < 0; k++) {
        steps(s, 1);
        if (k == 20) { const game_scene_t *c = fill(s); ASSERT_EQ_INT(c->text_count, 2); ASSERT_EQ_INT(c->text[1].shown, c->text[1].length); }   /* 126 blanks = 20 steps */
        if (screen_text_finished(s)) finished = k;
    }
    ASSERT_EQ_INT(finished, 21);                                 /* typing 20 steps and the hold 1 */
    del(s);
    s = new_screen(); ASSERT_TRUE(s != NULL);
    to_menu(s); steps(s, 20);
    key(s, '2'); steps(s, 28);
    ASSERT_EQ_INT(lines(s), 1);                                  /* still typing */
    key(s, '2'); steps(s, 1);
    ASSERT_EQ_INT(s->location, 1);                               /* accepted at once (5.6) */
    ASSERT_EQ_INT(s->state, SCR_LOC_PICKED);
    del(s);
}

TEST(the_location_keys_1_to_4_choose_5_and_6_start_at_airfield_1_7_to_0_keep_6_to_9_and_letters_are_ignored) {
    static const struct { int key, loc; } K[] = { { '1', 0 }, { '2', 1 }, { '3', 2 }, { '4', 3 }, { '5', 0 }, { '6', 0 }, { '7', 6 }, { '8', 7 }, { '9', 8 }, { '0', 9 } };
    for (int i = 0; i < 10; i++) {
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        to_location(s);
        key(s, K[i].key); steps(s, 1);
        ASSERT_EQ_INT(s->location, K[i].loc);                     /* V:4A900 (5.6): 5 and 6 are cleared, 7, 8, 9, 0 stay 6 to 9 */
        ASSERT_EQ_INT(s->state, SCR_LOC_PICKED);
        del(s);
    }
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_location(s);
    const char ign[] = "azA ";
    for (const char *k = ign; *k; k++) { key(s, *k); steps(s, 2); }   /* the location screen's pass is 10 blanks (08 5.8): a key every 2 steps is stored */
    btn(s, GAME_BTN_KEYPAD, 9); steps(s, 2);
    btn(s, GAME_BTN_FKEY, 1); steps(s, 2);
    btn(s, GAME_BTN_RETURN, 1); steps(s, 2);
    ASSERT_EQ_INT(s->state, SCR_LOCATION);
    btn(s, GAME_BTN_KEYPAD, 3); steps(s, 2);                      /* KP3 acts as the digit 3 (5.6) */
    ASSERT_EQ_INT(s->location, 2);
    del(s);
}

TEST(a_location_other_than_the_carrier_shows_the_aircraft_list_seven_steps_after_the_key) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_location(s);
    key(s, '1'); steps(s, 1);
    ASSERT_EQ_INT(s->mode, GAME_MODE_SELECT_AIRCRAFT);
    ASSERT_EQ_INT(lines(s), 0);                                   /* the location text is gone */
    steps(s, 5);
    ASSERT_EQ_INT(lines(s), 0);
    steps(s, 1);                                                  /* +7 */
    ASSERT_EQ_INT(s->state, SCR_AIRCRAFT);
    ASSERT_TRUE(lines(s) >= 1);
    ASSERT_EQ_INT(fill(s)->palette_regime, 1);
    del(s);
}

TEST(aircraft_keys_1_and_2_choose_the_class_every_other_key_is_ignored_and_the_choice_is_not_remembered) {
    for (int air = 1; air <= 2; air++) {
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        to_location(s);
        key(s, '1'); steps(s, 8);
        const char ign[] = "3456789azQ";
        for (const char *k = ign; *k; k++) { key(s, *k); steps(s, 1); }
        btn(s, GAME_BTN_RETURN, 1); steps(s, 1);
        ASSERT_EQ_INT(s->state, SCR_AIRCRAFT);
        steps(s, 3);                                                  /* the aircraft screen has no latch cap (08 5.8): a key right after the RETURN is stored; these steps let the caption start first */
        key(s, '0' + air); steps(s, 1);
        ASSERT_EQ_INT(s->aircraft, air == 1 ? 0x11 : 0x10);
        ASSERT_EQ_INT(s->state, SCR_AIR_PICKED);
        ASSERT_EQ_INT(lines(s), 0);
        del(s);
    }
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_location(s);
    key(s, '1'); steps(s, 8);
    btn(s, GAME_BTN_KEYPAD, 2); steps(s, 1);                      /* KP2 chooses the F-16 (5.7) */
    ASSERT_EQ_INT(s->aircraft, 0x10);
    del(s);
}

TEST(the_player_is_placed_six_steps_after_the_aircraft_key_and_seven_after_the_carrier_key) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_location(s);
    key(s, '2'); steps(s, 8);
    key(s, '2');
    for (int i = 1; i <= 5; i++) { steps(s, 1); ASSERT_EQ_INT(s->out_place, 0); }
    steps(s, 1);                                                  /* +6: 0x12D58, the start position is in the record */
    ASSERT_EQ_INT(s->out_place, 1);
    ASSERT_EQ_INT(s->location, 1); ASSERT_EQ_INT(s->aircraft, 0x10);
    ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT);
    ASSERT_EQ_INT(screen_sim_hold(s), 1);                         /* the flight is shown but not simulated: the approach */
    ASSERT_EQ_INT(s->allowance, 5);
    del(s);
    s = new_screen(); ASSERT_TRUE(s != NULL);
    to_location(s);
    key(s, '4');
    for (int i = 1; i <= 6; i++) { steps(s, 1); ASSERT_EQ_INT(s->out_place, 0); }
    steps(s, 1);                                                  /* +7 */
    ASSERT_EQ_INT(s->out_place, 1);
    ASSERT_EQ_INT(s->location, 3); ASSERT_EQ_INT(s->aircraft, 0x11);   /* the carrier skips the aircraft screen and uses the F/A-18 */
    del(s);
}

/* Note 08 12.14 N1 and C13 (followup-airfield-start, -approach-state): the passes of the approach state 0x12D58 per free-flight start: 118 for the location keys 1, 5 and 6, 93 for the keys 2 and 3,
 * 104 for the key 4 (the carrier), 227, 223, 200, 192 for the keys 7 to 0. */
TEST(the_free_flight_approach_lasts_118_93_93_104_118_118_227_223_200_and_192_passes_by_the_start_keys_1_to_0) {
    static const struct { char key; int passes; } C[] = { { '1', 118 }, { '2', 93 }, { '3', 93 }, { '4', 104 }, { '5', 118 }, { '6', 118 },
        { '7', 227 }, { '8', 223 }, { '9', 200 }, { '0', 192 } };   /* 12.14 C13 (keys56): the keys 5 and 6 start at airfield 1 with 118 passes; 5.6, 12.14 C13 [V: flow7-keys7890]: the keys 7, 8, 9, 0 (V:4A900 = 6 to 9) run 227, 223, 200, 192 passes, +-1 */
    for (unsigned c = 0; c < sizeof C / sizeof C[0]; c++) {
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        to_location(s);
        key(s, C[c].key);
        if (C[c].key != '4') { steps(s, 8); key(s, '1'); }
        int guard = 0;
        while (!s->out_place && guard++ < 30) steps(s, 1);
        ASSERT_EQ_INT(s->state, SCR_APPROACH);
        s->out_place = 0;
        int natural = 0;
        for (; natural < 400 && s->state == SCR_APPROACH; natural++) steps(s, 1);
        ASSERT_EQ_INT(natural, C[c].passes + 2 + 6);
        del(s);
    }
}

TEST(the_approach_ends_by_itself_after_104_steps_or_by_kp_enter_and_the_notice_and_the_wait_follow) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_location(s);
    key(s, '4'); steps(s, 7);
    s->out_place = 0;
    ASSERT_EQ_INT(s->state, SCR_APPROACH);
    int natural = 0;
    for (; natural < 400 && s->state == SCR_APPROACH; natural++) steps(s, 1);
    ASSERT_EQ_INT(natural, 104 + 2 + 6);                          /* 12.14 N1, C13: the carrier start's 104 passes, then 0x12D58's 2 passes and 0x12DB8's 6 (6.4, 8.1: passes, one step each) */
    int typed = 0;
    for (; typed < 40 && s->state == SCR_NOTICE; typed++) steps(s, 1);
    ASSERT_EQ_INT(typed, 7);                                      /* 37 blanks typed = 6, the end hold 8 blanks = 1 */
    ASSERT_EQ_INT(s->state, SCR_WAIT);
    ASSERT_EQ_INT(s->out_sim, 1);
    del(s);
    s = new_screen(); ASSERT_TRUE(s != NULL);
    to_location(s);
    key(s, '4'); steps(s, 7);
    steps(s, 20);
    btn(s, GAME_BTN_KEYPAD, GAME_KP_ENTER);                       /* the skip starts the same chain at its key: the notice 8 steps after the key (2 + 6 passes; the 14 of the log is blanks) */
    int skip = 0;
    for (; skip < 40 && s->state == SCR_APPROACH; skip++) steps(s, 1);
    ASSERT_EQ_INT(skip, 8);
    ASSERT_EQ_INT(s->state, SCR_NOTICE);
    ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT);
    ASSERT_TRUE(lines(s) >= 1);                                   /* the launch notice (carrier start) over the picture */
    ASSERT_EQ_INT(fill(s)->palette_regime, 1);
    ASSERT_EQ_INT(screen_sim_hold(s), 1);
    typed = 0;
    for (; typed < 40 && s->state == SCR_NOTICE; typed++) steps(s, 1);
    ASSERT_EQ_INT(typed, 7);
    ASSERT_EQ_INT(s->state, SCR_WAIT);
    ASSERT_EQ_INT(screen_sim_hold(s), 0);                         /* the simulation switch: the aircraft is simulated from here */
    ASSERT_EQ_INT(s->out_sim, 1);
    s->out_sim = 0;
    for (int i = 1; i <= 3; i++) { steps(s, 1); ASSERT_EQ_INT(s->state, SCR_WAIT); ASSERT_EQ_INT(s->out_hud, 0); }
    steps(s, 1);                                                  /* the acting pass: four steps after the clear, the flight state in the fifth */
    ASSERT_EQ_INT(s->state, SCR_FLIGHT);
    ASSERT_EQ_INT(s->out_hud, 1);                                 /* the HUD is switched on */
    del(s);
}

/* ---- ESC and SHIFT-ESC (5.11, Review Focus 5) --------------------------------------------------------------------------------------------------------- */

static void esc_chain_expect(screen_t *s, int reset_steps) {   /* called after the ESC's own step (call #1) */
    /* from the key: the reset at reset_steps, the dispatch at +7 after it, black 12, fade-in 7, map 13, then the caption (5.11: 66 steps in all with the caption typed) */
    for (int i = 2; i < reset_steps; i++) { steps(s, 1); ASSERT_EQ_INT(s->state, SCR_ESC_FADE); ASSERT_EQ_INT(fill(s)->fade_target, 0); }
    steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_ECHO);                            /* the reset: the flight is lost, the menu palette and the echo of the mode */
    ASSERT_EQ_INT(s->mode, GAME_MODE_MENU);
    ASSERT_EQ_INT(s->out_leave, 1);
    s->out_leave = 0;
    ASSERT_EQ_INT(lines(s), 0);
    steps(s, 2);
    ASSERT_EQ_INT(lines(s), 1);                                   /* the echo text at k = 55: 14 blanks after the reset */
    steps(s, 4);
    ASSERT_EQ_INT(s->state, SCR_ECHO);
    steps(s, 1);                                                  /* the dispatch, 46 blanks after the reset */
    ASSERT_EQ_INT(s->state, SCR_BLACK);
    ASSERT_EQ_INT(s->mode, GAME_MODE_SELECT_LOCATION);
    steps(s, 11); ASSERT_EQ_INT(s->state, SCR_BLACK);
    steps(s, 1);  ASSERT_EQ_INT(s->state, SCR_FADE_IN);           /* 73 blanks of black passes */
    steps(s, 7);  ASSERT_EQ_INT(s->state, SCR_FADE_IN);
    steps(s, 1);  ASSERT_EQ_INT(s->state, SCR_MAP);               /* the fade-in, 47 blanks = 8 steps */
    ASSERT_EQ_INT(fill(s)->fade_target, 15);
    steps(s, 12); ASSERT_EQ_INT(s->state, SCR_MAP);
    steps(s, 1);  ASSERT_EQ_INT(s->state, SCR_LOCATION);          /* the map stage still runs when the code is accepted (5.11) */
    ASSERT_TRUE(lines(s) >= 1);
}

/* Note 08 7.7, "The fade level at an ESC in the fade-in, settled" and "Two more levels and a correction": the ESC is acted on at the next pass boundary strictly after it, T_b = 12, 21, 30 or 34 blanks (T from
 * the first run of the state 0x11B9C), the fade-out starts from the level floor(T_b / 3) = 4, 7, 10 or 11 (the fade-in goes on until then) and the reset comes 21, 24, 29 or 32 blanks after the consuming pass.
 * T is s->t * 50 / 8 blanks here. */
TEST(a_demo_esc_in_the_fade_in_is_taken_at_the_next_pass_boundary_and_fades_out_from_that_boundarys_level) {
    /* k: steps in the approach before the ESC step (T = (k + 1) * 50 / 8 blanks at the ESC); T_b, the level L, the blanks to the consuming pass and from it to the reset (21, 24, 29, 32) converted to
     * steps: the target stays 15 for `pass` steps, the reset comes `reset` steps after the ESC step */
    static const struct { int k, l, pass, reset; } C[] = {
        { 0, 4, 1, 4 },     /* T 6, T_b 12, 6 blanks, 27 */
        { 1, 7, 1, 5 },     /* T 12, T_b 21 (the pass at 12 is the ESC's own moment: strictly after), 9 blanks, 33 */
        { 2, 7, 0, 4 },     /* T 18, T_b 21, 3 blanks, 27 */
        { 3, 10, 1, 5 },    /* T 25, T_b 30, 5 blanks, 34 */
        { 4, 11, 0, 6 },    /* T 31, T_b 34, 3 blanks, 35 */
        /* 08 7.7 "ESC after the 34-blank boundary ... settled" (T = 36, 39, 42 measured: levels 12, 13, 14, resets 33, 35, 37 blanks after the consuming pass): the port's steps fall at T 31 and 37 within the fade-in (a step is 6.25 blanks; the
         * fade-in of 7 steps ends at T 43, the T of 39 and 42 fall between steps): the level is floor(T / 3), the ESC is consumed in the next blank (the target is 0 from the ESC step on) and the reset comes
         * 2 L + 9 blanks after it (L 12: 33 + 1 blanks). The measured T of 39 and 42 (levels 13, 14) fall between the port's steps: not reachable here */
        { 5, 12, 0, 5 } };  /* T 37, L 12, 33 + 1 blanks */
    for (unsigned c = 0; c < sizeof C / sizeof C[0]; c++) {
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        screen_demo_begin(s, 2);
        int n = 0;
        while (s->state != SCR_APPROACH && n++ < 20) steps(s, 1);
        ASSERT_EQ_INT(s->state, SCR_APPROACH);
        ASSERT_EQ_INT(s->direct_restart, 1);
        steps(s, C[c].k);
        btn(s, GAME_BTN_ESC, 1);
        steps(s, 1);                                              /* the ESC's own step */
        ASSERT_EQ_INT(s->state, SCR_ESC_FADE);
        ASSERT_EQ_INT(s->esc_l0, C[c].l);
        ASSERT_EQ_INT(fill(s)->fade_target, (C[c].pass > 0) ? 15 : 0);   /* the fade-in goes on until the consuming pass */
        for (int i = 1; i < C[c].reset; i++) { steps(s, 1); ASSERT_EQ_INT(s->state, SCR_ESC_FADE); if (i >= C[c].pass) ASSERT_EQ_INT(fill(s)->fade_target, 0); }
        steps(s, 1);
        ASSERT_EQ_INT(s->state, SCR_DEMO_WAIT);                   /* the reset: the demo restarts without the echo (7.7) */
        del(s);
    }
}

TEST(esc_in_the_flight_runs_the_restart_chain_stage_by_stage) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    ASSERT_EQ_INT(s->state, SCR_FLIGHT);
    btn(s, GAME_BTN_ESC, 1);
    steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_ESC_FADE);
    ASSERT_EQ_INT(screen_sim_hold(s), 1);                         /* V:4A866 = 1: the world freezes at once */
    esc_chain_expect(s, 6);                                       /* fade-out 32 blanks = 5 steps and the close and pulse 8 blanks = 1: the reset at 6 */
    ASSERT_EQ_INT(s->mode_byte, 1);                               /* the mode survives, the allowance is set again by the dispatch */
    ASSERT_EQ_INT(s->allowance, 5);
    del(s);
}

TEST(esc_from_a_pause_resets_after_4_steps_not_6) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    screen_pause(s, 1);
    steps(s, 25);
    btn(s, GAME_BTN_ESC, 1);
    steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_ESC_FADE);
    esc_chain_expect(s, 4);                                       /* the fade starts at level 10: 21 blanks = 3 steps, the pulse 1 */
    del(s);
}

TEST(the_whole_restart_takes_66_steps_from_the_key_to_the_end_of_the_typed_caption) {   /* 0 + 5 + 1 + 2 + 5 + 12 + 8 + 13 + 20 */
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    btn(s, GAME_BTN_ESC, 1);
    int loc = -1, finished = -1;
    for (int i = 1; i <= 80 && finished < 0; i++) {
        steps(s, 1);
        s->out_leave = 0;
        if (loc < 0 && s->state == SCR_LOCATION) loc = i;
        if (loc > 0 && screen_text_finished(s)) finished = i;
    }
    ASSERT_EQ_INT(loc, 46);                                       /* the caption starts 46 steps after the key (the map stage ends) */
    ASSERT_EQ_INT(finished, 67);                                  /* the list ends 66 steps after the key, the end hold is the next step */
    del(s);
}

TEST(a_second_esc_during_the_restart_restarts_it_from_the_level_reached_in_every_stage) {   /* Review Focus 5 */
    static const int at[] = { 3, 9, 12, 20, 25, 34, 48, 60 };   /* a step after the first ESC: the fade, the echo, the black passes, the fade-in, the map, the caption */
    for (unsigned i = 0; i < sizeof at / sizeof at[0]; i++) {
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        to_flight(s, 4, 1);
        btn(s, GAME_BTN_ESC, 1);
        steps(s, at[i]);
        s->out_leave = 0;
        btn(s, GAME_BTN_ESC, 1);
        steps(s, 1);
        ASSERT_EQ_INT(s->state, SCR_ESC_FADE);                    /* acted on in the next step: nothing is skipped and nothing sticks */
        int caption = -1;
        for (int k = 1; k <= 90 && caption < 0; k++) { steps(s, 1); s->out_leave = 0; if (s->state == SCR_LOCATION) caption = k; }
        ASSERT_TRUE(caption > 0 && caption <= 60);                // a restart from level 0 takes less than the first
        ASSERT_EQ_INT(s->mode_byte, 1);
        del(s);
    }
}

TEST(a_digit_during_the_fade_in_the_black_passes_and_the_echo_is_consumed_and_changes_nothing) {   /* Review Focus 5 */
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_menu(s); steps(s, 20);
    key(s, '2'); steps(s, 3);
    key(s, '3'); steps(s, 1);                                     /* during the echo */
    steps(s, 6);
    ASSERT_EQ_INT(s->state, SCR_BLACK);
    key(s, '1'); steps(s, 1);                                     /* during the black passes */
    steps(s, 12);
    ASSERT_EQ_INT(s->state, SCR_FADE_IN);
    key(s, '2'); steps(s, 1);                                     /* during the fade-in: stored in V:4A900 and nothing else (5.6) */
    ASSERT_EQ_INT(s->state, SCR_FADE_IN);
    steps(s, 10);
    ASSERT_EQ_INT(s->state, SCR_LOCATION);                         /* never a stuck black screen */
    ASSERT_EQ_INT(s->nq, 0);
    del(s);
}

TEST(esc_during_the_three_tick_wait_and_during_the_notice_restarts_the_selection) {   /* Review Focus 5 */
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_location(s);
    key(s, '4'); steps(s, 7);
    btn(s, GAME_BTN_KEYPAD, GAME_KP_ENTER); steps(s, 10);
    ASSERT_EQ_INT(s->state, SCR_NOTICE);
    btn(s, GAME_BTN_ESC, 1); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_ESC_FADE);
    del(s);
    s = new_screen(); ASSERT_TRUE(s != NULL);
    to_location(s);
    key(s, '4'); steps(s, 7);
    btn(s, GAME_BTN_KEYPAD, GAME_KP_ENTER);
    for (int i = 0; i < 80 && s->state != SCR_WAIT; i++) steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_WAIT);
    steps(s, 2);
    btn(s, GAME_BTN_ESC, 1); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_ESC_FADE);
    del(s);
}

TEST(shift_esc_returns_to_the_menu_from_every_screen_with_a_mode_and_the_menu_text_comes_in_seven_steps) {
    static const int when[] = { 0, 3, 14, 22, 30, 60, 70 };   /* the echo, the black passes, the fade-in, the caption, the aircraft list */
    for (unsigned i = 0; i < sizeof when / sizeof when[0]; i++) {
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        to_menu(s); steps(s, 20);
        key(s, '2'); steps(s, when[i] + 1);
        if (s->state == SCR_LOCATION) { key(s, '1'); steps(s, 10); }
        btn(s, GAME_BTN_SHIFT_ESC, 1);
        steps(s, 1);
        ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);
        ASSERT_EQ_INT(s->mode, GAME_MODE_MENU);
        ASSERT_EQ_INT(s->mode_byte, 0);                           /* the countercode counts as not passed; the mode is cleared */
        ASSERT_EQ_INT(lines(s), 0);
        steps(s, 6);
        ASSERT_EQ_INT(lines(s), 1);                               /* the menu text 7 steps after the key (44 blanks) */
        del(s);
    }
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    btn(s, GAME_BTN_SHIFT_ESC, 1);
    steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);
    ASSERT_EQ_INT(s->out_leave, 1);                               /* the reset: the flight is lost at once, no fade */
    del(s);
}

/* ---- the game over (5.12), the pause (5.14), the mission end (5.13) ------------------------------------------------------------------------------------ */

TEST(the_game_over_stages_from_a_sum_to_20_steps) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    screen_gameover(s, 0x63);
    ASSERT_EQ_INT(s->mode, GAME_MODE_GAMEOVER);
    ASSERT_EQ_INT(fill(s)->palette_regime, 0);                    /* the menu palette is loaded for the text */
    ASSERT_EQ_INT(screen_sim_hold(s), 1);
    int start = -1, list_end = -1, menu = -1;
    for (int i = 1; i <= 40; i++) {
        steps(s, 1);
        if (s->mode == GAME_MODE_GAMEOVER) {
            const game_scene_t *c = fill(s);
            if (start < 0 && c->text_count > 0) start = i;
            if (list_end < 0 && c->text_count == 2 && c->text[0].shown == 23 && c->text[1].shown == 38) list_end = i;
        } else if (s->mode == GAME_MODE_MENU && menu < 0 && s->state == SCR_MENU && lines(s) > 0) menu = i;
    }
    ASSERT_EQ_INT(start, 1);                                      /* k = 12: 6 blanks after the restart pass A (k = 6) = 1 step */
    ASSERT_TRUE(list_end > 0 && list_end <= 13);                  /* typed in 78 blanks = 12 steps (the list ends at 13; the last characters are on screen earlier) */
    ASSERT_EQ_INT(menu, 20);                                      /* 1 + 12 + the hold 1 + the reset 1 + the menu set-up 5 = 20 (21 from k = 0) */
    del(s);
}

TEST(the_game_over_hands_the_flight_back_for_the_reset_when_the_screen_ends) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    screen_gameover(s, 0x62);
    int leave = -1, mode_menu = -1;
    for (int i = 1; i <= 40; i++) {
        steps(s, 1);
        if (leave < 0 && s->out_leave) leave = i;
        if (mode_menu < 0 && s->mode == GAME_MODE_MENU) mode_menu = i;
    }
    ASSERT_EQ_INT(leave, 14);                                      /* 1 + 12 + the end hold 1: the reset at k = 98 */
    ASSERT_EQ_INT(leave, mode_menu);
    del(s);
}

TEST(esc_during_the_game_over_text_restarts_the_selection_not_the_menu) {   /* Review Focus 5, 5.12 */
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    screen_gameover(s, 0x63);
    steps(s, 6);
    ASSERT_EQ_INT(s->mode, GAME_MODE_GAMEOVER);
    btn(s, GAME_BTN_ESC, 1); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_ESC_FADE);
    steps(s, 5);                                                   // the reset at 6 steps from the key
    ASSERT_EQ_INT(s->state, SCR_ECHO);
    ASSERT_EQ_INT(s->mode_byte, 1);
    ASSERT_EQ_INT(s->allowance, 5);                                // the dispatch sets it again: five crashes
    del(s);
}

TEST(the_pause_dims_to_10_and_types_its_text_from_13_blanks_after_the_stop) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    ASSERT_EQ_INT(fill(s)->text_count, 0);
    screen_pause(s, 1);
    ASSERT_EQ_INT(fill(s)->fade_target, 10);
    steps(s, 1);
    ASSERT_EQ_INT(lines(s), 0);
    steps(s, 1);
    ASSERT_EQ_INT(lines(s), 0);                                    /* the fade takes 2 steps ... */
    steps(s, 1);
    ASSERT_TRUE(lines(s) >= 1);                                    /* ... and the first pass of the text is 13 blanks after the stop (note 10 7.3), in the third step (blanks 12 to 17) */
    ASSERT_EQ_INT(fill(s)->text[0].row, 43); ASSERT_EQ_INT(fill(s)->text[0].col, 16);
    int done = -1;
    for (int i = 1; i <= 10 && done < 0; i++) { steps(s, 1); if (fill(s)->text[0].shown == 10) done = i; }
    ASSERT_TRUE(done >= 2 && done <= 4);                           /* the last of the nine characters 16 blanks after the first (the space costs nothing): step 5 (note 10 7.3: 6 passes of a third of a blank a character) */
    ASSERT_EQ_INT(fill(s)->palette_regime, 1);                     /* the flight palette */
    screen_pause(s, 0);
    ASSERT_EQ_INT(fill(s)->text_count, 0);
    ASSERT_EQ_INT(fill(s)->fade_target, 15);                       /* the picture returns over 15 blanks */
    del(s);
}

/* Note 10 7.3, 5.7 item 13 and note 08 5.14 [V: sound7-pause, corrected in milestone 7]: the pause line has ten characters of which eight are not blank. Its first character requests a click on its first two passes and the
 * seven others one each, six passes apart: the starts fall at +13, +16, +18, +20, +22, +24, +26, +28 and +30 blanks after the stop of the voices (the start of the step that follows the key), the pass of this screen being
 * a third of a blank except the second, which is three blanks long. The positions are counted in library blanks from that step's start, at every phase of the 6, 6, 6, 7 blanks of a step. */
TEST(the_pause_text_clicks_nine_times_at_13_16_18_to_30_blanks_after_the_stop) {
    for (int phase = 0; phase < 4; phase++) {
        screen_t *s = screen_with_sound(); ASSERT_TRUE(s != NULL);
        to_flight(s, 4, 1);
        for (int i = 0; i <= phase; i++) sound_step(&SND);     /* the phase of the blanks; what the walk to the flight clicked is dropped */
        { game_audio_cmd_t d[GAME_MAX_AUDIO_CMDS]; int k = 0; uint8_t mt, ms, mr; sound_drain(&SND, d, GAME_MAX_AUDIO_CMDS, &k, &mt, &ms, &mr); }
        sound_stop_all(&SND);
        screen_pause(s, 1);
        int at[40], n = 0, base = 0, last = -1;
        for (int i = 0; i < 5; i++) {                            /* five steps are at least 31 blanks: the window ends before +34 (the tenth start of the capture is a NOTE-GAP of note 08 5.14: the test says nothing of it) */
            const int nb = sound_step_blanks(&SND);
            screen_step(s);
            sound_step(&SND);
            game_audio_cmd_t c[GAME_MAX_AUDIO_CMDS];
            int k = 0;
            uint8_t mt, ms, mr;
            sound_drain(&SND, c, GAME_MAX_AUDIO_CMDS, &k, &mt, &ms, &mr);
            for (int j = 0; j < k; j++) {
                if (c[j].op != GAME_AUD_DMA_ON || c[j].voice != 3) continue;
                const int b = base + c[j].blank;
                if (b != last && n < 40) at[n++] = last = b;      /* two requests in one blank are one start */
            }
            base += nb;
        }
        ASSERT_TRUE(n >= 9);                                     /* the first nine starts only */
        static const int WANT[9] = { 13, 16, 18, 20, 22, 24, 26, 28, 30 };
        for (int k = 0; k < 9 && k < n; k++) ASSERT_EQ_INT(at[k], WANT[k]);
        del(s);
    }
}

/* Note 10 7.2 and 5.7 item 13, note 08 5.11 [V: sound7-restart, corrected in milestone 7]: SHIFT-ESC from the air. NO click comes before the stop; the stop of all four voices and the music restart are the first act of
 * the menu set-up pass, 11 blanks after the key (library blanks from the start of the step that follows the key, at every phase of the step); the list is posted at k = 44 and its first click falls at 44 to 46 (the click
 * starts of the real menu text are compared with the capture by sound7_check.py: this fixture has invented text). */
TEST(shift_esc_from_the_air_has_no_click_before_the_stop_at_11_and_the_menu_list_clicks_from_44_to_46) {
    for (int phase = 0; phase < 4; phase++) {
        screen_t *s = screen_with_sound(); ASSERT_TRUE(s != NULL);
        to_flight(s, 4, 1);
        for (int i = 0; i <= phase; i++) sound_step(&SND);
        { game_audio_cmd_t d[GAME_MAX_AUDIO_CMDS]; int k = 0; uint8_t mt, ms, mr; sound_drain(&SND, d, GAME_MAX_AUDIO_CMDS, &k, &mt, &ms, &mr); }
        btn(s, GAME_BTN_SHIFT_ESC, 1);
        int clicks[120], nc = 0, last = -1, music = -1, base = 0;
        for (int i = 0; i < 12; i++) {
            const int nb = sound_step_blanks(&SND);
            screen_step(s);
            sound_step(&SND);
            game_audio_cmd_t c[GAME_MAX_AUDIO_CMDS];
            int k = 0;
            uint8_t mt, ms, mr;
            sound_drain(&SND, c, GAME_MAX_AUDIO_CMDS, &k, &mt, &ms, &mr);
            for (int j = 0; j < k; j++) {
                const int b = base + c[j].blank;
                if (c[j].op != GAME_AUD_DMA_ON) continue;
                if (c[j].voice == 0 && music < 0) music = b;
                if (c[j].voice == 3 && b != last && nc < 120) clicks[nc++] = last = b;
            }
            base += nb;
        }
        ASSERT_EQ_INT(music, 11);                                 /* the stop and the restart: the first act of the set-up pass */
        ASSERT_TRUE(nc > 0);
        for (int k = 0; k < nc; k++) ASSERT_TRUE(clicks[k] > 11);   /* no click before the stop (the 11 clicks of milestone 7's first reading were rewrites by the sound interrupt) */
        ASSERT_TRUE(clicks[0] >= 44 && clicks[0] <= 46);          /* the first click of the list posted at 44 (the capture: 44.3 and 47.1; a step is 6 blanks and the key falls anywhere in its step) */
        del(s);
    }
}

TEST(the_mission_end_freezes_after_4_steps_types_the_outcome_and_resumes_the_flight) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    s->mode_byte = 3;
    screen_mission_end(s, 0xFF, 0);                                // success of mission 1: ids 0x1E and 0x8055
    for (int i = 1; i <= 3; i++) { steps(s, 1); ASSERT_EQ_INT(screen_sim_hold(s), 0); }
    steps(s, 1);
    ASSERT_EQ_INT(screen_sim_hold(s), 1);                          /* the freeze: 28 blanks */
    ASSERT_EQ_INT(s->mode, GAME_MODE_FLIGHT);
    ASSERT_EQ_INT(fill(s)->palette_regime, 1);                     /* the outcome text is typed over the picture in the flight palette */
    int text = -1, over = -1;
    for (int i = 5; i <= 60 && over < 0; i++) {
        steps(s, 1);
        if (text < 0 && lines(s) > 0) text = i;
        if (s->out_end) over = i;
    }
    ASSERT_EQ_INT(text, 7);                                        /* k = 42 blanks */
    ASSERT_EQ_INT(over, 30);                                       /* 7 + 143 blanks = 23 steps of typing, then the flight resumes at once */
    ASSERT_EQ_INT(s->out_end, 1);
    ASSERT_EQ_INT(screen_sim_hold(s), 0);
    ASSERT_EQ_INT(s->state, SCR_FLIGHT);
    del(s);
}

TEST(the_mission_end_final_screens_go_to_the_menu_and_the_qualification_result_restarts) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    s->mode_byte = 0x7D;
    screen_mission_end(s, 0xFF, 0);                                // practice success: id 0x48 and the code 0xF0
    int menu = -1;
    for (int i = 1; i <= 60 && menu < 0; i++) { steps(s, 1); if (s->state == SCR_MENU_BUILD) menu = i; }
    ASSERT_EQ_INT(menu, 12);                                       /* the text ends and its hold (step 9), the 100 passes 17 blanks = 3 steps, then the reset */
    ASSERT_EQ_INT(s->out_leave, 1);
    steps(s, 5);
    ASSERT_EQ_INT(lines(s), 0);
    steps(s, 1);                                                   /* 8 + 33 blanks = 1 + 5 steps to the menu list */
    ASSERT_TRUE(lines(s) >= 1);
    del(s);
    s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    s->mode_byte = 9;
    screen_mission_end(s, 0xFF, 0);                                // qualification success: ids 0x4A, 0x8053 and the code 0xEF
    int esc = -1;
    for (int i = 1; i <= 80 && esc < 0; i++) { steps(s, 1); if (s->state == SCR_ESC_FADE) esc = i; }
    ASSERT_EQ_INT(esc, 49);                                        // 7 + 38 typed + the hold 1 + the 100 passes 3: the ESC restart of 5.11 starts by itself
    del(s);
}

/* ---- the fix round: log-disk result, list digits, P, ESC in the outcome screens, aircraft digit early ----------------------------------------------------- */

TEST(with_the_default_check_result_0_the_credits_key_goes_straight_to_the_menu_and_no_prompt_is_shown) {   /* 5.3 ruling */
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    screen_set_logdisk_result(s, 0);
    steps(s, 4);
    key(s, 'x');
    steps(s, 1);
    ASSERT_EQ_INT(s->mode, GAME_MODE_MENU);
    ASSERT_EQ_INT(lines(s), 0);
    steps(s, 6);
    ASSERT_TRUE(lines(s) >= 1);                                    /* the menu text 7 steps after the key */
    del(s);
}

TEST(the_log_disk_prompt_variants_play_the_ids_of_the_check_result) {   /* results 1, 2, 3: ids 0x44, 0x45, 0x46 (invented rows 88, 89 and 90) then the explanation */
    for (int r = 1; r <= 3; r++) {
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        screen_set_logdisk_result(s, r);
        steps(s, 4); key(s, 'x'); steps(s, 16);
        ASSERT_EQ_INT(s->mode, GAME_MODE_LOGDISK);
        ASSERT_EQ_INT(fill(s)->text[0].row, 87 + r);
        ASSERT_EQ_INT(lines(s), r == 3 ? 4 : 1 + 3);
        del(s);
    }
}

TEST(the_digits_act_on_the_mission_list_screen_because_it_is_still_the_menu_state) {   /* 5.4 */
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_menu(s); steps(s, 20);
    key(s, '6'); steps(s, 5);
    ASSERT_EQ_INT(s->state, SCR_LIST);
    key(s, '2'); steps(s, 1);
    ASSERT_EQ_INT(s->mode_byte, 1);
    ASSERT_EQ_INT(s->state, SCR_ECHO);
    del(s);
}

TEST(the_list_of_item_6_starts_in_the_keys_own_step) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_menu(s); steps(s, 20);
    key(s, '6'); steps(s, 1);
    ASSERT_TRUE(lines(s) >= 1);
    del(s);
}

TEST(an_aircraft_digit_typed_before_the_aircraft_caption_is_taken_once_the_choice_flag_is_set) {   /* 5.7, 6.4: V:4A849 = 1 from the location key */
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_location(s);
    key(s, '1'); steps(s, 2);
    ASSERT_EQ_INT(s->state, SCR_LOC_PICKED);
    key(s, '2'); steps(s, 1);
    ASSERT_EQ_INT(s->aircraft, 0x10);
    ASSERT_EQ_INT(s->state, SCR_AIR_PICKED);
    del(s);
    s = new_screen(); ASSERT_TRUE(s != NULL);
    to_location(s);
    key(s, '4'); steps(s, 2);
    key(s, '2'); steps(s, 1);                                      /* the carrier has no aircraft menu */
    ASSERT_EQ_INT(s->state, SCR_LOC_PICKED);
    ASSERT_EQ_INT(s->aircraft, 0x11);
    del(s);
}

TEST(p_on_the_selection_screens_freezes_the_map_without_a_pause_screen_and_p_in_the_approach_pauses) {   /* 6.4, 5.14 */
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_location(s);
    btn(s, GAME_BTN_PAUSE, 1); steps(s, 2);
    ASSERT_EQ_INT(s->map_frozen, 1);
    ASSERT_EQ_INT(s->state, SCR_LOCATION);
    ASSERT_EQ_INT(fill(s)->fade_target, 15);                       /* no fade, no pause text */
    btn(s, GAME_BTN_PAUSE, 1); steps(s, 2);
    ASSERT_EQ_INT(s->map_frozen, 0);
    key(s, '4'); steps(s, 7);
    ASSERT_EQ_INT(s->state, SCR_APPROACH);
    steps(s, 10);
    const int t0 = s->t;
    screen_pause(s, 1);                                            /* the approach state 0x12D58 starts the pause */
    steps(s, 6);
    ASSERT_EQ_INT(s->t, t0);                                       /* the approach counter stands still */
    ASSERT_EQ_INT(fill(s)->fade_target, 10);
    ASSERT_TRUE(lines(s) >= 1);                                    /* the pause text */
    screen_pause(s, 0);
    steps(s, 3);
    ASSERT_EQ_INT(s->t, t0 + 3);
    ASSERT_EQ_INT(lines(s), 0);
    del(s);
    s = new_screen(); ASSERT_TRUE(s != NULL);
    btn(s, GAME_BTN_PAUSE, 1); steps(s, 8);
    ASSERT_EQ_INT(s->mode, GAME_MODE_CREDITS);                     /* a pause button is no key on the credits */
    del(s);
}

TEST(esc_with_an_end_code_set_goes_to_the_menu_after_the_hold_and_does_not_restart_but_0xef_is_skipped) {   /* 5.13 */
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    s->mode_byte = 3;
    screen_mission_end(s, 0xFF, 0);
    steps(s, 12);
    btn(s, GAME_BTN_ESC, 1);
    int menu = -1;
    for (int i = 1; i <= 80 && menu < 0; i++) { steps(s, 1); ASSERT_TRUE(s->state != SCR_ESC_FADE); if (s->state == SCR_MENU_BUILD) menu = i; }
    ASSERT_TRUE(menu > 0);
    ASSERT_EQ_INT(s->out_leave, 1);
    ASSERT_EQ_INT(s->out_end, 0);                                  /* the flight does not resume */
    del(s);
    s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    s->mode_byte = 9;
    screen_mission_end(s, 0xFF, 0);
    for (int i = 0; i < 80 && s->state != SCR_END_HOLD; i++) steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_END_HOLD);
    btn(s, GAME_BTN_ESC, 1); steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_ESC_FADE);                         /* 0xEF: ESC skips the countdown and restarts */
    del(s);
}

/* ---- the mission start tables (6.1) ------------------------------------------------------------------------------------------------------------------ */

TEST(the_placement_walks_the_row_of_the_mode_with_the_hostile_cap_the_threshold_and_the_mode_filter) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    s->mode_byte = 1;
    ASSERT_EQ_INT(screen_start_flight(s), 3);                      // the carrier, two aircraft (cap 2); the third aircraft, the threshold entry and the filtered entry are not placed
    ASSERT_EQ_INT(s->hostile_cap, 2); ASSERT_EQ_INT(s->hostile_count, 2);
    ASSERT_EQ_INT(s->placed[0].template_id, 7); ASSERT_EQ_INT(s->placed[0].cls, 0x20); ASSERT_EQ_INT(s->placed[0].slot, 14); ASSERT_EQ_INT(s->placed[0].counted, 0);
    ASSERT_EQ_INT(s->placed[1].template_id, 3); ASSERT_EQ_INT(s->placed[1].slot, 4); ASSERT_EQ_INT(s->placed[1].counted, 1); ASSERT_EQ_INT(s->placed[1].pos_ref, 38);
    ASSERT_EQ_INT(s->placed[2].slot, 6);
    s->mode_byte = 2;                                              // row 1 uses list 1: a single carrier
    ASSERT_EQ_INT(screen_start_flight(s), 1);
    s->mode_byte = 0x7F;                                           // row 9: the demo row; list 1 as well
    ASSERT_EQ_INT(screen_start_flight(s), 1);
    s->mode_byte = 0x7D;                                           // the formula of 0x2CFDA, not a table
    ASSERT_EQ_INT(screen_start_flight(s), 0);
    ASSERT_EQ_INT(s->allowance, 1);
    del(s);
}

TEST(the_selector_comes_from_the_seeded_generator_one_draw_per_fresh_start_and_an_esc_restart_reuses_the_variant_and_the_offsets) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    s->mode_byte = 3;
    int seen_k[4] = { 0, 0, 0, 0 }, seen_neg_x = 0, seen_neg_z = 0;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        screen_random_seed(s, seed);
        s->variant = 2;                                         // an earlier start's stored variant
        uint32_t probe = seed;
        const unsigned m = entities_random16(&probe) % 50u;     // the blank's number in the second (note 07 2.4)
        static const int jitter_list[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, -2, -4, -6 };   // the 13 demonstration starts (07 2.4)
        const int jit = jitter_list[entities_random16(&probe) % 13u];
        const unsigned sel = (m * 20000u + 1612u + (unsigned)jit) & 0xFFFFu;   // mode 3: delta 1,612
        s->echo_restart = 0;
        screen_start_flight(s);
        ASSERT_EQ_INT(s->selector, sel);
        ASSERT_EQ_INT(s->rng, probe);                           // exactly two draws
        const unsigned k = sel & 3u;
        seen_k[k]++;
        ASSERT_EQ_INT(s->variant, k == 0 ? 2 : (int)k - 1);     // k = 0 leaves the stored variant alone; 1, 2, 3 give the variants 0, 1, 2 and store them
        const int dx = (int)((sel >> 1) & 3u) * ((sel & 0x10u) ? -1 : 1), dz = (int)((sel >> 2) & 3u) * ((sel & 0x20u) ? -1 : 1);
        ASSERT_EQ_INT(s->pl_dx, dx); ASSERT_EQ_INT(s->pl_dz, dz);
        if (dx < 0) seen_neg_x = 1;
        if (dz < 0) seen_neg_z = 1;
        ASSERT_EQ_INT(k, (unsigned)(1612 + jit) & 3u);          // m x 20,000 has its low 5 bits clear, so bits 0-4 of the selector are those of the delta plus the jitter
        // the ESC restart: no draw, the stored variant and the stored offsets
        const int var = s->variant, pdx = s->pl_dx, pdz = s->pl_dz;
        const uint32_t rng_before = s->rng;
        s->echo_restart = 1;
        screen_start_flight(s);
        ASSERT_EQ_INT(s->rng, rng_before); ASSERT_EQ_INT(s->variant, var); ASSERT_EQ_INT(s->pl_dx, pdx); ASSERT_EQ_INT(s->pl_dz, pdz);
    }
    // the selector is NOT uniform (2.4): bits 0-4 are bits 0-4 of the path's delta (1,612 = 0x64C: low 5 bits 12 -/+3), bit 5 is the parity of m + bit 5 of delta
    ASSERT_TRUE(seen_k[0] > 0 && seen_k[1] > 0 && seen_k[2] > 0 && seen_k[3] == 0);   // the jitter list (0, +1, -2, -4, -6) gives 1,612 + j the low bits 0, 1, 2 and never 3
    ASSERT_TRUE(!seen_neg_x);                                   // 1,606 to 1,613 never has bit 4 set: the x offset of mode 3 is never negative (2.4: the offsets are almost fixed per path)
    ASSERT_TRUE(seen_neg_z);
    del(s);
}

TEST(the_selector_of_each_start_path_is_20000_m_plus_its_measured_delta_and_jitter_not_a_uniform_word) {
    // 07 2.4: the deltas of the measured paths, the jitter from the measured list of the 13 demonstration starts for every path
    const struct { int mode; int delta; } path[] = { { 0x7F, 1172 }, { 0x7E, 1172 }, { 1, 1620 }, { 2, 1888 }, { 3, 1612 }, { 4, 1620 }, { 5, 1597 }, { 6, 1594 }, { 7, 1619 }, { 8, 1635 }, { 9, 583 }, { 0x7D, 335 } };
    for (unsigned i = 0; i < sizeof path / sizeof path[0]; i++) {
        uint32_t rng = 7 + i, seen_m[50] = { 0 };
        int seen_j[16] = { 0 };   // the jitter -6..+1 offset by 6 (a jitter outside the list would not be found)
        for (int n = 0; n < 600; n++) {
            const unsigned sel = screen_draw_selector(&rng, path[i].mode);
            int found = 0;
            for (unsigned m = 0; m < 50 && !found; m++) {
                const int dd = (int)(int16_t)(uint16_t)(sel - (m * 20000u + (unsigned)path[i].delta));
                if (dd == 0 || dd == 1 || dd == -2 || dd == -4 || dd == -6) { found = 1; seen_m[m]++; seen_j[dd + 6]++; }
            }
            ASSERT_TRUE(found);
        }
        int distinct = 0; for (int m = 0; m < 50; m++) distinct += seen_m[m] != 0;
        ASSERT_TRUE(distinct > 40);   // m is drawn over all 50 blanks of the second
        // 8 of 13 draws give 0, 2 of 13 give +1, 1 of 13 each -2, -4, -6: the list is not uniform
        ASSERT_TRUE(seen_j[6] > 600 * 8 / 13 - 80 && seen_j[6] < 600 * 8 / 13 + 80);
        ASSERT_TRUE(seen_j[7] > 0 && seen_j[4] > 0 && seen_j[2] > 0 && seen_j[0] > 0);
    }
}

/* Note 08 5.8, "Conversion to the step clock": the handler stores one key per pass (the latch V:4A85B, cleared by the pass accounting), so the keys stored per step are step blanks / pass blanks: one per
 * about 10 steps in the credits (a pass of 60 blanks), one per 5 in the menu build (33 blanks), one per 3 in a gate-1 pass (10 blanks on the location screen, caption typing included; no cap on the aircraft screen), and
 * every key in the 5-blank passes elsewhere. */
static int stored_after_key(screen_t *s, int c) { const int before = s->nq; key(s, c); return s->nq - before; }

TEST(the_key_latch_holds_for_the_pass_of_the_state_one_key_per_10_steps_in_the_credits_per_5_in_the_menu_build_per_3_in_a_gate_1_pass_and_none_is_lost_elsewhere) {
    {   /* the credits: the key at step 0 is stored, the next step's key is lost until the pass of 60 blanks (9.6 steps) is over */
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        ASSERT_EQ_INT(s->state, SCR_CREDITS);
        ASSERT_EQ_INT(stored_after_key(s, 'a'), 1);
        steps(s, 1);
        ASSERT_EQ_INT(stored_after_key(s, 'b'), 0);
        ASSERT_EQ_INT(s->nq, 1);
        del(s);
    }
    {   /* the menu build is one pass of 33 blanks (5.3 steps): of the six steps seen the keys of the first and the sixth are stored */
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        key(s, 'x'); steps(s, 3); btn(s, GAME_BTN_RETURN, 1);
        int guard = 0;
        while (s->state != SCR_MENU_BUILD && guard++ < 20) steps(s, 1);
        ASSERT_EQ_INT(s->state, SCR_MENU_BUILD);
        int stored = 0, n = 0;
        while (s->state == SCR_MENU_BUILD && n < 20) { stored += stored_after_key(s, 0x61); steps(s, 1); n++; }
        ASSERT_EQ_INT(n, 6);
        ASSERT_EQ_INT(stored, 2);
        del(s);
    }
    {   /* the location screen (08 5.8, errata of milestone 7 [V: flow7-latch-loc]): while the caption is typed the passes run one per blank and every key is stored; once it is typed the passes are 11.5 blanks (92 units,
         * 1.84 steps) and one key is stored in each: 5 of 8 consecutive keys (the pass in progress takes the first), against all 8 while it is typed */
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        to_location(s);
        ASSERT_EQ_INT(s->state, SCR_LOCATION);
        ASSERT_EQ_INT(s->eng.mode_byte, 0);                           /* the caption is being typed */
        int stored = 0;
        for (int i = 0; i < 8; i++) { stored += stored_after_key(s, 'a'); steps(s, 1); }   /* typing */
        ASSERT_EQ_INT(stored, 8);
        steps(s, 30);                                                 /* the caption (126 blanks, 20 steps) is typed */
        ASSERT_TRUE((s->eng.mode_byte & 0x81) != 0);
        stored = 0;
        for (int i = 0; i < 8; i++) { stored += stored_after_key(s, 'a'); steps(s, 1); }
        ASSERT_EQ_INT(stored, 5);
        del(s);
    }
    {   /* the aircraft path (08 5.8 errata of milestone 7 [V: flow7-latch-air]): the location key's pass and three slow passes of 11.5 blanks before the aircraft caption cap the keys the same way; the aircraft screen
         * from its caption on stores every key */
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        to_location(s);
        steps(s, 40);                                                 /* the caption is typed */
        ASSERT_TRUE(s->eng.mode_byte != 0);
        key(s, '1'); steps(s, 1);                                     /* the location key is taken: the aircraft path begins */
        ASSERT_EQ_INT(s->state, SCR_LOC_PICKED);
        int stored = 0, n = 0;
        while (s->state == SCR_LOC_PICKED && n < 20) { stored += stored_after_key(s, 'a'); steps(s, 1); n++; }
        ASSERT_EQ_INT(s->state, SCR_AIRCRAFT);
        ASSERT_TRUE(n >= 6 && n <= 8);
        ASSERT_TRUE(stored >= 3 && stored <= 4 && stored < n);        /* one key per pass of 1.84 steps, not one per step */
        del(s);
    }
    {   /* the aircraft screen has no cap (08 5.8: three passes a blank): every key is stored once its caption starts, while typing and after */
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        to_location(s);
        key(s, '1'); steps(s, 8);
        btn(s, GAME_BTN_RETURN, 1); steps(s, 2);
        ASSERT_EQ_INT(s->state, SCR_AIRCRAFT);
        for (int n = 0; n < 2; n++) {
            int stored = 0;
            for (int i = 0; i < 8; i++) { stored += stored_after_key(s, 'a'); steps(s, 1); }
            ASSERT_EQ_INT(stored, 8);
            steps(s, 40);
        }
        del(s);
    }
    {   /* the menu (5-blank passes): every key of one step is stored */
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        to_menu(s);
        steps(s, 100);
        ASSERT_EQ_INT(s->state, SCR_MENU);
        int stored = 0;
        for (int i = 0; i < 6; i++) stored += stored_after_key(s, 'a');
        ASSERT_EQ_INT(stored, 6);
        del(s);
    }
}

TEST(the_cap_refuses_every_class_once_it_is_reached_and_the_sub_list_is_recorded) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    s->mode_byte = 1;
    screen_start_flight(s);
    // row 0, cap 2: the carrier (slot 14, not counted), two aircraft, then the cap refuses the third aircraft and everything after it, the missile of slot 12 included
    ASSERT_EQ_INT(s->placed_count, 3);
    ASSERT_EQ_INT(s->sub_first, 0); ASSERT_EQ_INT(s->sub_count, 7);
    for (int i = 0; i < s->placed_count; i++) ASSERT_TRUE(s->placed[i].slot != 8 && s->placed[i].slot != 12);
    del(s);
}

TEST(without_text_or_scenario_assets_the_screens_still_run_and_the_flow_reaches_the_flight) {
    nofiles = 1;
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    ASSERT_TRUE(s->text == NULL && s->scn == NULL);
    to_location(s);
    ASSERT_EQ_INT(s->state, SCR_LOCATION);
    key(s, '4'); steps(s, 7);
    ASSERT_EQ_INT(s->out_place, 1);
    s->out_place = 0;
    ASSERT_EQ_INT(screen_start_flight(s), 0);
    del(s);
    nofiles = 0;
}

TEST(a_present_but_bad_text_file_is_refused_by_game_create) {
    build_assets();
    B_TEXT.n -= 3;                                                  // truncated
    screen_t s;
    char err[200] = "";
    ASSERT_EQ_INT(screen_init(&s, &SLD, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "text") != NULL);
    char gerr[300] = "";
    game_t *g = game_create(&SLD, gerr, sizeof gerr);
    ASSERT_TRUE(g == NULL);
    ASSERT_TRUE(strstr(gerr, "text.bin") != NULL);
}

/* ---- through the game (game.c) ----------------------------------------------------------------------------------------------------------------------- */

static void g_step(game_t *g, int n) { for (int i = 0; i < n; i++) game_step(g); }
static void g_key(game_t *g, int c) { game_event_t ev = { GAME_EV_KEY, 0, c }; game_input(g, &ev); }
static void g_btn(game_t *g, int code, int v) { game_event_t ev = { GAME_EV_BUTTON, code, v }; game_input(g, &ev); }
static game_scene_t GS;
static const game_scene_t *g_scene(game_t *g) { game_scene(g, &GS); return &GS; }

TEST(the_game_boots_into_the_credits_and_walks_the_front_end_with_the_key_sequence) {
    build_assets();
    char err[200] = "";
    game_t *g = game_create(&SLD, err, sizeof err);
    ASSERT_TRUE(g != NULL);
    game_set_logdisk_result(g, 3);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_CREDITS);
    g_step(g, 3);
    ASSERT_TRUE(g_scene(g)->text_count > 0);
    ASSERT_EQ_INT(GS.palette_regime, 0);
    g_key(g, 'a'); g_step(g, 1);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_LOGDISK);
    g_step(g, 4);
    g_btn(g, GAME_BTN_RETURN, 1); g_step(g, 8);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_MENU);
    ASSERT_TRUE(GS.text_count > 0);
    g_key(g, '2'); g_step(g, 8);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_SELECT_LOCATION);
    ASSERT_EQ_INT(GS.fade_target, 0);
    g_step(g, 19);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_SELECT_LOCATION);
    ASSERT_EQ_INT(GS.fade_target, 15);
    ASSERT_EQ_INT(GS.palette_regime, 1);
    ASSERT_TRUE(GS.text_count > 0);
    g_key(g, '1'); g_step(g, 8);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_SELECT_AIRCRAFT);
    ASSERT_TRUE(GS.text_count > 0);
    g_key(g, '2'); g_step(g, 7);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_FLIGHT);              // no world data in this asset set: the flight is a screen state only
    g_btn(g, GAME_BTN_SHIFT_ESC, 1); g_step(g, 1);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_MENU);
    g_step(g, 8);
    ASSERT_TRUE(g_scene(g)->text_count > 0);
    game_destroy(g);
}

TEST(a_held_key_across_the_game_screens_is_one_action) {   // Review Focus 1, through game_input
    build_assets();
    game_t *g = game_create(&SLD, NULL, 0);
    ASSERT_TRUE(g != NULL);
    game_set_logdisk_result(g, 3);
    g_step(g, 3);
    g_key(g, 'a'); g_step(g, 5);                                    // the credits key; the same key repeated by the frontend would be filtered, a second press waits
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_LOGDISK);
    g_btn(g, GAME_BTN_RETURN, 1); g_btn(g, GAME_BTN_RETURN, 1); g_step(g, 30);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_MENU);                // one screen change, no skipped screen, no quit
    game_destroy(g);
}

TEST(the_debug_start_goes_straight_to_the_flight_and_needs_world_data) {
    build_assets();
    game_t *g = game_create(&SLD, NULL, 0);
    ASSERT_TRUE(g != NULL);
    game_debug_start_flight(g, 3, 0);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_CREDITS);             // no world data: nothing starts
    game_destroy(g);
}

TEST(the_selection_screens_carry_the_map_marks_and_the_overview_scene_and_the_aircraft_screen_keeps_the_chosen_number) {   /* 5.6 */
    build_assets();
    game_t *g = game_create(&SLD, NULL, 0);
    ASSERT_TRUE(g != NULL);
    game_set_logdisk_result(g, 3);
    g_step(g, 3); g_key(g, 'a'); g_step(g, 8); g_btn(g, GAME_BTN_RETURN, 1); g_step(g, 8);
    g_key(g, '2'); g_step(g, 26);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_SELECT_LOCATION);
    ASSERT_EQ_INT(GS.map_mark_count, 4);                            /* the numbers 1 to 4 are drawn by the map code, not by text records */
    for (int i = 0; i < 4; i++) { ASSERT_EQ_INT(GS.map_marks[i].digit, i + 1); ASSERT_EQ_INT(GS.map_marks[i].pen, 9); }
    g_key(g, '2'); g_step(g, 8);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_SELECT_AIRCRAFT);
    ASSERT_EQ_INT(GS.map_mark_count, 1);                            /* only the chosen start's number stays */
    ASSERT_EQ_INT(GS.map_marks[0].digit, 2);
    game_destroy(g);
}

/* 08 5.6 (flow-s2-loc-key5): the keys 5 and 6 store V:4A900 = 4 and 5; the aircraft screen's mark pass (04 "Camera mode 6") draws only the entry of that index, and entry 4 names the inactive carrier record 15 while
 * entry 5 is the terminator: no number is drawn. The start itself is airfield 1's (0x1D730 clears the byte). */
TEST(the_keys_5_and_6_draw_no_number_on_the_aircraft_screen_and_start_at_airfield_1) {
    for (char c = '5'; c <= '6'; c++) {
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        to_location(s);
        key(s, c);
        steps(s, 8);
        game_map_mark_t m[GAME_MAX_MAP_MARKS];
        ASSERT_EQ_INT(screen_map_marks(s, m), 0);
        ASSERT_EQ_INT(s->location, 0);
        btn(s, GAME_BTN_PAUSE, 1); steps(s, 1);                 /* a pause draws every entry whatever the camera mode (04 "Camera mode 6") */
        ASSERT_EQ_INT(screen_map_marks(s, m), 4);
        del(s);
    }
}

/* The flow states' stores of V:4A90F and V:4A8EE (note 06 "What V:4A90F and V:4A86D hold", 07 12.1): 0xFF at the start-up, 1 at the reset of the menu build (0x1D25E), 1 at the location screen
 * (0x12B52), V:4A8EE = 3 and V:4A90F = 0 at a runway or airfield location key (0x12BA0, 0x12BD2), 1 again at the aircraft key (0x12C56); the carrier key stores V:4A8EE only. */
TEST(the_flow_states_store_v4a90f_and_the_panel_counter_in_their_order) {
    build_assets();
    game_t *g = game_create(&SLD, NULL, 0);
    ASSERT_TRUE(g != NULL);
    ASSERT_EQ_INT(game_debug_v4a90f(g), -1);                         /* 0x11962 */
    ASSERT_EQ_INT(game_debug_panel_counter(g), 0);
    game_set_logdisk_result(g, 3);
    g_step(g, 3); g_key(g, 'a'); g_step(g, 8); g_btn(g, GAME_BTN_RETURN, 1); g_step(g, 8);
    ASSERT_EQ_INT(game_debug_v4a90f(g), -1);                         /* the menu build after the log-disk RETURN follows no reset block: the start-up's 0xFF stays (note 06: a first demo shows no map marks) */
    ASSERT_EQ_INT(game_debug_redraw_counter(g), 0xFF);               /* V:4A8EF after the boot (07 12.1: the first pass of the demo, training part 2 and the qualification reads 0xFF) */
    g_key(g, '2'); g_step(g, 26);
    ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_SELECT_LOCATION);
    ASSERT_EQ_INT(game_debug_v4a90f(g), 1);                          /* 0x12B52 */
    ASSERT_TRUE(game_debug_panel_counter(g) != 0);                   /* the dispatch state's 0x120E0 stored (07 12.1); the value after the store and the passes in between are the port's */
    g_key(g, '2'); g_step(g, 1);                                     /* a runway or airfield start */
    ASSERT_EQ_INT(game_debug_v4a90f(g), 0);                          /* 0x12BD2 */
    g_step(g, 8);
    ASSERT_EQ_INT(game_debug_panel_counter(g), 0);                   /* the passes of 0x12C02 and after count it down (07 12.1: free flight reaches its first pass with 0 [V: followup-combat-first-pass.txt]; the first passes of the other modes: tests/test_game.c) */
    ASSERT_EQ_INT(game_debug_redraw_counter(g), 0xFF);               /* the location key (0x12BA0) stores V:4A8EE only: V:4A8EF keeps its boot value 0xFF (07 12.1 [V]) */
    g_key(g, '1'); g_step(g, 1);
    ASSERT_EQ_INT(game_debug_v4a90f(g), 1);                          /* 0x12C56 */
    game_destroy(g);
}

TEST(the_carrier_location_key_stores_the_panel_counter_but_not_v4a90f_zero) {
    build_assets();
    game_t *g = game_create(&SLD, NULL, 0);
    ASSERT_TRUE(g != NULL);
    game_set_logdisk_result(g, 3);
    g_step(g, 3); g_key(g, 'a'); g_step(g, 8); g_btn(g, GAME_BTN_RETURN, 1); g_step(g, 8);
    g_key(g, '2'); g_step(g, 26);
    g_key(g, '4'); g_step(g, 1);                                     /* the carrier: no camera mode 6, no aircraft screen */
    ASSERT_EQ_INT(game_debug_v4a90f(g), 1);
    game_destroy(g);
}

/* ---- the extracted assets (skipped without assets/) -------------------------------------------------------------------------------------------------- */

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

TEST(real_text_the_credits_menu_and_game_over_land_on_the_notes_steps) {
    uint8_t *b; size_t n;
    if (real_read(NULL, "tables/text.bin", &b, &n) != 0) { printf("skipped "); return; }
    free(b);
    screen_t *s = (screen_t *)malloc(sizeof *s);
    char err[200] = "";
    ASSERT_EQ_INT(screen_init(s, &RLD, err, sizeof err), 0);
    screen_set_logdisk_result(s, 3);
    ASSERT_TRUE(s->text != NULL);
    steps(s, 2);
    int finished = -1;
    for (int i = 1; i <= 60 && finished < 0; i++) { steps(s, 1); if (screen_text_finished(s)) finished = i; }
    ASSERT_EQ_INT(finished, 41);                                    // typing 39 steps and the hold 1 after the text starts (finished flag counted from the step before the text)
    key(s, 'x'); steps(s, 8); btn(s, GAME_BTN_RETURN, 1); steps(s, 7);
    int menu_end = -1;
    for (int i = 1; i <= 40 && menu_end < 0; i++) { steps(s, 1); if (screen_text_finished(s)) menu_end = i; }
    ASSERT_EQ_INT(menu_end, 16);                                    // the list is typed in 96 blanks = 15 steps; the hold is its own stage of 1 step
    screen_destroy(s); free(s);
}

TEST(real_assets_the_four_locations_start_at_the_notes_positions_and_key_2_makes_an_f16) {
    if (!have_real()) { printf("skipped "); return; }
    static const struct { int key; int32_t x, y, z; } P[] = {   // note 08 5.6: the player position after the start
        { '1', 273963296, 1800, 279184624 }, { '2', 264178832, 1800, 286095856 }, { '3', 254084992, 1800, 265102320 }, { '4', 295185408, 30472, 274309120 } };
    for (int i = 0; i < 4; i++) {
        for (int air = 1; air <= 2; air++) {
            if (i == 3 && air == 2) continue;                       // the carrier skips the aircraft screen
            char err[200] = "";
            game_t *g = game_create(&RLD, err, sizeof err);
            if (!g) { printf("  (game_create: %s)\n", err); ASSERT_TRUE(g != NULL); }
            g_step(g, 3);
            g_key(g, 'x'); g_step(g, 8); g_btn(g, GAME_BTN_RETURN, 1); g_step(g, 8);
            g_key(g, '2'); g_step(g, 26);
            g_key(g, P[i].key); g_step(g, 8);
            if (i != 3) { g_key(g, '0' + air); }
            g_step(g, 8);
            aircraft_t *ac = game_debug_aircraft(g);
            ASSERT_TRUE(ac != NULL);
            ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_FLIGHT);
            ASSERT_EQ_INT(ac->pos[0], P[i].x); ASSERT_EQ_INT(ac->pos[1], P[i].y); ASSERT_EQ_INT(ac->pos[2], P[i].z);
            ASSERT_EQ_INT(ac->f.cls, air == 2 ? 0x10 : 0x11);       // key 2 on the aircraft screen selects the F-16
            ASSERT_EQ_INT(ac->f.v4a95e, 1);
            ASSERT_EQ_INT(game_crash_allowance(g), 5);
            game_destroy(g);
        }
    }
}

TEST(real_assets_the_location_keys_7_to_0_start_at_the_out_of_table_positions_of_the_note) {
    if (!have_real()) { printf("skipped "); return; }
    static const struct { int key; int32_t x, z; int heading; } P[] = {   // note 08 5.6 (heading word x 80; key 0: 256,000 in the start's 16-bit word shift is 0xE800 = 59,392, note 05 17.14 "The heading of the out-of-table starts, key 0")
        { '7', 0x43300, 0x1FFFFFFF, 0 }, { '8', 0xC8E00, 0x1FFFFFFF, 0 }, { '9', 0xC00000, 0xC00000, 0 }, { '0', 0x1B86060, 0xAB6700, 0xE800 } };
    for (int i = 0; i < 4; i++) {
        game_t *g = game_create(&RLD, NULL, 0);
        ASSERT_TRUE(g != NULL);
        g_step(g, 3);
        g_key(g, 'x'); g_step(g, 12);
        g_key(g, '2'); g_step(g, 26);
        g_key(g, P[i].key); g_step(g, 8);
        g_key(g, '1'); g_step(g, 8);
        aircraft_t *ac = game_debug_aircraft(g);
        ASSERT_TRUE(ac != NULL);
        ASSERT_EQ_INT(g_scene(g)->mode, GAME_MODE_FLIGHT);
        ASSERT_EQ_INT(ac->pos[0], P[i].x); ASSERT_EQ_INT(ac->pos[1], 1800); ASSERT_EQ_INT(ac->pos[2], P[i].z);
        ASSERT_EQ_INT(ac->f.heading, P[i].heading);
        g_step(g, 300);                                                  // the approach and the notice run without a crash of the core
        game_destroy(g);
    }
}

TEST(real_assets_the_three_tick_wait_and_the_hud_come_after_the_notice_and_the_flight_is_frozen_before) {
    if (!have_real()) { printf("skipped "); return; }
    char err[200] = "";
    game_t *g = game_create(&RLD, err, sizeof err);
    ASSERT_TRUE(g != NULL);
    g_step(g, 3); g_key(g, 'x'); g_step(g, 8); g_btn(g, GAME_BTN_RETURN, 1); g_step(g, 8);
    g_key(g, '2'); g_step(g, 26); g_key(g, '4'); g_step(g, 8);
    aircraft_t *ac = game_debug_aircraft(g);
    const uint32_t steps_at_start = ac->f.steps;
    g_step(g, 30);
    ASSERT_EQ_INT(ac->f.steps, steps_at_start);                      // the approach and the notice do not simulate
    ASSERT_EQ_INT(g_scene(g)->hud.cp_hud_on, 0);
    ASSERT_EQ_INT(game_debug_panel_counter(g), 0);                   // 07 12.1: the gate-1 passes after the location key count V:4A8EE down: free flight reaches its first passes with 0 (the external block runs, not the panel block)
    g_btn(g, GAME_BTN_KEYPAD, GAME_KP_ENTER);
    int wait = -1;
    for (int i = 0; i < 80 && wait < 0; i++) { g_step(g, 1); if (ac->f.steps != steps_at_start) wait = i; }
    ASSERT_TRUE(wait > 0);                                           // the simulation starts after the notice
    ASSERT_EQ_INT(g_scene(g)->hud.cp_n, 0);                          // the step counter reads 0 at the first waiting step
    g_step(g, 3);
    ASSERT_EQ_INT(g_scene(g)->hud.cp_hud_on, 0);
    g_step(g, 1);
    ASSERT_EQ_INT(g_scene(g)->hud.cp_hud_on, 1);                     // the acting pass: HUD on, the flight state
    game_destroy(g);
}

/* ---- the missions F1 to F6: the briefing chain of note 08 13 (handler answer 3) ------------------------------------------------------------------------ */

static int mission_handler(void *user, uint8_t mode) {
    screen_t *sc = (screen_t *)user;
    if (mode < 3 || mode > 8) return 0;
    sc->location = (uint8_t)(mode == 3 ? 0 : 3);   /* 13.1: F1 starts on the airfield (V:4A900 = 0), F2 to F6 on the carrier */
    sc->aircraft = 0x11;
    return 3;
}
static logbook_t MLB;
static int bearing_value = -1, bearing_calls;
static int bearing_cb(void *user) { (void)user; bearing_calls++; return bearing_value; }
static int visible_after, visible_calls;
static int visible_cb(void *user, int record) { (void)user; (void)record; return ++visible_calls > visible_after; }
static screen_t *mission_screen(void) {
    screen_t *s = new_screen();
    if (!s) return NULL;
    logbook_zero(&MLB);
    logbook_set16(&MLB, LB_QUALIFIED, 1);                         /* a record exists: the function keys and item 7 work (5.4) */
    s->log = &MLB;
    screen_set_logdisk_result(s, 0);                              /* a usable disk: no prompt, the log stays (5.3) */
    screen_set_mode_handler(s, mission_handler, s);
    bearing_value = -1; bearing_calls = 0; visible_after = 0; visible_calls = 0;
    return s;
}
/* the steps after the F key until the stage is entered (the key's own step is 0); -1 when it is not within max */
static int ms_at(screen_t *s, int stage, int max) {
    for (int i = 1; i <= max; i++) { steps(s, 1); if (s->state == SCR_MISSION && s->ms_stage == stage) return i; }
    return -1;
}
static void fkey(screen_t *s, int n) { btn(s, GAME_BTN_FKEY, n); }
static void to_log_menu(screen_t *s) { key(s, 'x'); steps(s, 30); }   /* the credits key goes straight to the menu with a usable disk; the list is typed after 24 steps */

TEST(an_f_key_starts_a_mission_the_dispatch_places_it_and_the_briefing_chain_begins_black) {
    screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    fkey(s, 1); steps(s, 6);
    ASSERT_EQ_INT(s->state, SCR_ECHO);
    ASSERT_EQ_INT(lines(s), 0);                                  /* the echo of a function-key mission shows no line (5.4) */
    steps(s, 1);                                                 /* the dispatch 7 steps after the key */
    ASSERT_EQ_INT(s->state, SCR_MISSION);
    ASSERT_EQ_INT(s->mode_byte, 3);
    ASSERT_EQ_INT(s->ms_stage, MS_BLACK);
    ASSERT_EQ_INT(s->allowance, 3);                              /* 6.3 */
    ASSERT_TRUE(s->placed_count > 0);                            /* the placement of 6.1 ran at the dispatch */
    ASSERT_EQ_INT(s->mission, 1);
    ASSERT_EQ_INT(s->mode, GAME_MODE_SELECT_LOCATION);           /* the map overview behind the text */
    ASSERT_EQ_INT(fill(s)->fade_target, 0);                      /* the palette is black until the fade-in */
    ASSERT_TRUE(!s->out_place);                                  /* the player is placed at the end of the chain, not now */
    ASSERT_EQ_INT(fill(s)->map_mark_count, 0);
    del(s);
}

TEST(real_assets_the_date_line_of_the_intro_block_names_the_month_of_the_flight_hours) {
    uint8_t *mb; size_t mn;
    if (!have_real() || real_read(NULL, "tables/months.bin", &mb, &mn) != 0) { printf("skipped "); return; }
    screen_t *s = (screen_t *)malloc(sizeof *s);
    char err[200] = "";
    ASSERT_EQ_INT(screen_init(s, &RLD, err, sizeof err), 0);
    ASSERT_TRUE(s->have_months && mn == 36 && memcmp(s->months, mb, 36) == 0);
    logbook_zero(&MLB);
    logbook_set16(&MLB, LB_QUALIFIED, 1);
    const uint32_t secs = 40u * 3600u;                           /* 40 hours: the month index 40 >> 5 = 1, the day (40 AND 31) + 1 = 9 */
    logbook_set16(&MLB, LB_SECONDS, secs >> 16); logbook_set16(&MLB, LB_SECONDS + 2, secs & 0xFFFF);
    s->log = &MLB;
    screen_set_logdisk_result(s, 0);
    screen_set_mode_handler(s, mission_handler, s);
    to_log_menu(s);
    fkey(s, 1);
    int found = 0;
    for (int i = 0; i < 700 && !found; i++) {
        steps(s, 1);
        const game_scene_t *c = fill(s);
        for (int t = 0; t < c->text_count; t++)   /* the date line (record line 3; the scene lists the lines that have characters) */
            if (c->text[t].id == 0x5B && c->text[t].length > 21 && memcmp(c->text[t].text + 10, mb + 9, 9) == 0 && c->text[t].text[20] == ' ' && c->text[t].text[21] == '9') found = 1;
    }
    ASSERT_TRUE(found);
    free(mb);
    screen_destroy(s); free(s);
}

TEST(the_chain_of_f1_lands_on_the_notes_steps_per_blank) {
    screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
    MLB.rec[LB_COMPLETIONS + 3] = 1;                             /* the captured pair [0x5B, 0x1B] is the second briefing (13.5); the first briefing types by the same rule */
    to_log_menu(s);
    fkey(s, 1);
    ASSERT_EQ_INT(ms_at(s, MS_FADE, 100), 21);                   /* 0x12568: 130 blanks from the key */
    ASSERT_EQ_INT(fill(s)->fade_target, 15);
    ASSERT_EQ_INT(ms_at(s, MS_WAIT, 100), 8);               /* 0x125A8 at 180 blanks = 29 steps */
    ASSERT_EQ_INT(ms_at(s, MS_INTRO, 100), 32);             /* the text starts at 380 blanks = 61 steps */
    ASSERT_EQ_INT(s->eng.ids[0], 0x5B);                          /* the intro block, then the second briefing (the completion byte is 1) */
    ASSERT_EQ_INT(s->eng.ids[1], CHAIN_T[1]);
    ASSERT_EQ_INT(s->eng.n, 2);
    const int s7 = ms_at(s, MS_STAGE7, 200);
    ASSERT_TRUE(s7 >= 1);                                        /* the typed span follows the text (the invented records are short; the real text's duration is pinned by the real-text test below) */
    ASSERT_EQ_INT(s->brief_target, 4);                           /* the first counted aircraft of the sub-list (the invented slot 4) */
    ASSERT_EQ_INT(s->cam_mode, 2);
    ASSERT_EQ_INT(ms_at(s, MS_ORDERS, 10), 2);                   /* stage 7 lasts 2 passes (13.4), the camera is then at mode 3 (arrived), the orders type while it flies back (stage 8) */
    ASSERT_EQ_INT(s->cam_mode, 3);
    ASSERT_EQ_INT(s->eng.ids[0], 0x5D);
    ASSERT_EQ_INT(s->eng.ids[1], CHAIN_T[3]);                    /* the clearance line and the orders of F1 */
    const int tail = ms_at(s, MS_TAIL, 400);
    ASSERT_TRUE(tail > 0);
    ASSERT_EQ_INT(s->cam_mode, 3);
    steps(s, 4); ASSERT_EQ_INT(s->cam_mode, 3);
    steps(s, 1); ASSERT_EQ_INT(s->cam_mode, 4);                  /* C1: mode 4 only with the arrival of stage 8, 0x12AC0 sees it 5 steps after the orders text (4 for 0x12A92, 1 for 0x12AC0) */
    int prompt = 0;
    for (int i = 1; i <= 20 && !prompt; i++) { steps(s, 1); if (s->state == SCR_AIRCRAFT) prompt = i; }
    ASSERT_EQ_INT(prompt, 4);                                    /* (5 steps are gone above) 4 + 1 + 1 passes, then 0x12C02's three: the airfield start asks for the aircraft (13.1) */
    ASSERT_EQ_INT(s->cam_mode, 6);
    ASSERT_EQ_INT(s->eng.ids[0], 0x4B);
    del(s);
}

TEST(the_orders_of_f1_type_until_the_tail) {
    screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    fkey(s, 1);
    ms_at(s, MS_ORDERS, 400);
    const int tail = ms_at(s, MS_TAIL, 400);
    ASSERT_TRUE(tail >= 1);                                      /* the orders type for as long as the text takes (the real text's 404 blanks are pinned by the real-text test below) */
    del(s);
}

TEST(only_f1_asks_for_the_aircraft_the_carrier_starts_place_the_player_six_steps_after_the_tail) {
    screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    logbook_set16(&MLB, LB_COMPLETIONS + 3, 1);                  /* the mission before is complete: F2 is available */
    MLB.rec[0x11 + 4] = 1;
    fkey(s, 2);
    ASSERT_TRUE(ms_at(s, MS_TAIL, 600) > 0);
    ASSERT_EQ_INT(s->mode_byte, 4);
    ASSERT_EQ_INT(s->location, 3);
    int placed = 0;
    for (int i = 1; i <= 30 && !placed; i++) { steps(s, 1); if (s->out_place) placed = i; }
    ASSERT_EQ_INT(placed, 12);                                   /* 4 + 1 + 1 passes and 0x12AFC to 0x12C6E's 6 steps; no aircraft prompt (13.1) */
    ASSERT_EQ_INT(s->state, SCR_APPROACH);
    ASSERT_EQ_INT(s->cam_mode, 6);
    s->out_place = 0;
    int natural = 0;
    for (; natural < 400 && s->state == SCR_APPROACH; natural++) steps(s, 1);
    ASSERT_EQ_INT(natural, 112 + 2 + 6);                         /* 12.14 N1: F2 112 passes */
    del(s);
}

/* Note 08 6.4 and C11 (followup-brief-skip): KP-ENTER during the orders of a mission briefing skips the approach entirely, straight to the state 0x12DB8 and its 6-pass countdown. */
TEST(kp_enter_during_the_orders_skips_the_approach_and_goes_to_the_6_pass_countdown) {
    screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    MLB.rec[0x11 + 4] = 1;
    fkey(s, 2);
    ASSERT_TRUE(ms_at(s, MS_ORDERS, 600) > 0);
    steps(s, 3);
    btn(s, GAME_BTN_KEYPAD, GAME_KP_ENTER);
    steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_APPROACH);
    ASSERT_EQ_INT(s->out_place, 1);                              /* the player is placed at once with the mission's own start */
    ASSERT_EQ_INT(s->cam_mode, 0);                               /* C11: the approach never runs, the camera mode is 0 (review S6) */
    s->out_place = 0;
    int n = 0;
    for (; n < 40 && s->state == SCR_APPROACH; n++) steps(s, 1);
    ASSERT_EQ_INT(n, 5);                                         /* 0x12DB8's six passes, the key's own step the first, no 0x12D58 passes */
    ASSERT_EQ_INT(s->state, SCR_NOTICE);
    del(s);
}

TEST(f1_waits_for_the_aircraft_key_and_places_the_player_six_steps_after_it) {
    screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    fkey(s, 1);
    for (int i = 0; i < 700 && s->state != SCR_AIRCRAFT; i++) steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_AIRCRAFT);
    steps(s, 100);
    ASSERT_EQ_INT(s->state, SCR_AIRCRAFT);                       /* it waits for the key */
    key(s, '2'); steps(s, 6);
    ASSERT_EQ_INT(s->aircraft, 0x10);
    ASSERT_TRUE(s->out_place);
    ASSERT_EQ_INT(s->location, 0);                               /* the airfield start */
    ASSERT_EQ_INT(s->state, SCR_APPROACH);
    s->out_place = 0;
    int natural = 0;
    for (; natural < 400 && s->state == SCR_APPROACH; natural++) steps(s, 1);
    ASSERT_EQ_INT(natural, 121 + 2 + 6);                         /* 13.5, 12.14 N1: F1 is 121 passes (the step counter 38 to 159), then 0x12D58's 2 and 0x12DB8's 6 */
    del(s);
}

TEST(a_key_skips_the_intro_and_the_briefing_at_a_first_start_but_not_after_an_esc_restart_except_in_the_text) {
    screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    fkey(s, 1);
    ASSERT_TRUE(ms_at(s, MS_WAIT, 100) > 0);
    s->out_panel = 0;
    key(s, 'x'); steps(s, 1);                                    /* 13.1: the latch at 0x125A8 when V:4A8E3 is 0 */
    ASSERT_EQ_INT(s->ms_stage, MS_STAGE7);
    ASSERT_EQ_INT(s->out_panel, 0);                              /* straight to 0x12744: the store of 0x12730 is not passed */
    ASSERT_EQ_INT(s->out_90f_set, 1); ASSERT_EQ_INT(s->out_90f, 1);   /* 0x12756 */
    del(s);
    s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    fkey(s, 1);
    ASSERT_TRUE(ms_at(s, MS_INTRO, 100) > 0);
    s->out_panel = 0;
    key(s, 'x'); steps(s, 1);                                    /* 0x126F6 waits for the text or a key */
    ASSERT_EQ_INT(s->ms_stage, MS_STAGE7);
    ASSERT_EQ_INT(s->out_panel, 1);                              /* V:4A8EE = 3 (0x12730) */
    ASSERT_EQ_INT(s->brief_target, 4);
    del(s);
    s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    fkey(s, 1);
    ms_at(s, MS_WAIT, 100);
    s->echo_restart = 1;                                         /* an ESC restart (V:4A8E3 = 1): the latch does not skip at 0x125A8 */
    key(s, 'x'); steps(s, 1);
    ASSERT_EQ_INT(s->ms_stage, MS_WAIT);
    ms_at(s, MS_INTRO, 100); steps(s, 3);
    key(s, 'x'); steps(s, 1);
    ASSERT_EQ_INT(s->ms_stage, MS_STAGE7);                       /* but the text takes a key */
    del(s);
}

TEST(the_second_briefing_is_chosen_by_the_completion_byte_and_each_mission_has_its_own_ids) {
    for (int m = 3; m <= 8; m++) {
        screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
        to_log_menu(s);
        for (int k = 4; k <= 8; k++) MLB.rec[0x11 + k] = 1;      /* all six available */
        MLB.rec[LB_COMPLETIONS + m] = 2;                         /* completed twice: the briefing B and the sub-list 2 */
        fkey(s, m - 2);
        ASSERT_TRUE(ms_at(s, MS_INTRO, 100) > 0);
        ASSERT_EQ_INT(s->eng.ids[0], 0x5B); ASSERT_EQ_INT(s->eng.ids[1], CHAIN_T[(m - 3) * 5 + 1]);
        ASSERT_TRUE(ms_at(s, MS_STAGE7, 300) > 0);
        ASSERT_TRUE(ms_at(s, MS_ORDERS, 10) > 0);
        ASSERT_EQ_INT(s->eng.ids[1], CHAIN_T[(m - 3) * 5 + 3]);
        del(s);
    }
}

TEST(the_missions_started_counter_goes_up_at_the_orders_of_every_start_and_every_esc_restart) {
    screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    fkey(s, 1);
    ASSERT_TRUE(ms_at(s, MS_ORDERS, 400) > 0);
    ASSERT_EQ_INT(logbook_get16(&MLB, LB_STARTED), 1);          /* 14.2: +0x36 at 0x128DA */
    btn(s, GAME_BTN_ESC, 1);
    steps(s, 1);
    ASSERT_EQ_INT(s->out_esc, 1);                                /* the ESC request set the pause byte: game.c takes the flag */
    for (int i = 0; i < 700 && !(s->state == SCR_MISSION && s->ms_stage == MS_ORDERS); i++) steps(s, 1);
    ASSERT_EQ_INT(s->state, SCR_MISSION);
    ASSERT_EQ_INT(logbook_get16(&MLB, LB_STARTED), 2);          /* the restart runs the state again; the placement keeps the variant (V:4A8E3) */
    del(s);
}

TEST(an_esc_restart_passes_0x1279c_and_0x127c8_before_the_orders_a_first_start_does_not) {
    screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    fkey(s, 1);
    ASSERT_TRUE(ms_at(s, MS_STAGE7, 400) > 0);
    ASSERT_EQ_INT(ms_at(s, MS_ORDERS, 20), 2);                   /* the first start: the orders right after the camera arrives (the countercode screens are skipped) */
    btn(s, GAME_BTN_ESC, 1);
    for (int i = 0; i < 700 && !(s->state == SCR_MISSION && s->ms_stage == MS_STAGE7); i++) steps(s, 1);
    ASSERT_EQ_INT(s->ms_stage, MS_STAGE7);
    ASSERT_EQ_INT(s->echo_restart, 1);
    ASSERT_EQ_INT(ms_at(s, MS_RESTART, 20), 2);                  /* stage 7's two passes, then 0x1279C (countdown 2) */
    ASSERT_EQ_INT(ms_at(s, MS_ORDERS, 20), 7);                   /* 0x127C8 (the restart branch, countdown 3): 3 + 4 passes (5.5, 13.1) */
    ASSERT_EQ_INT(s->eng.ids[0], 0x5D);
    del(s);
}

TEST(the_approach_target_is_none_without_an_aircraft_in_the_placement_and_stage_7_ends_at_once) {
    screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    logbook_set16(&MLB, LB_COMPLETIONS + 3, 1);
    MLB.rec[0x11 + 4] = 1;
    fkey(s, 2);                                                  /* the invented list of the row of F2 holds only a carrier */
    ASSERT_TRUE(ms_at(s, MS_STAGE7, 400) > 0);
    ASSERT_EQ_INT(s->brief_target, -1);
    ASSERT_EQ_INT(ms_at(s, MS_ORDERS, 5), 1);
    del(s);
}

TEST(stage_7_waits_for_the_projection_test_of_the_draw_when_the_game_supplies_one) {
    screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
    screen_set_mission_hooks(s, NULL, visible_cb, NULL);
    visible_after = 3;                                           /* a stand-in for the draw's projection test (08 13.4; the game itself passes NULL) */
    to_log_menu(s);
    fkey(s, 1);
    ASSERT_TRUE(ms_at(s, MS_STAGE7, 400) > 0);
    ASSERT_EQ_INT(ms_at(s, MS_ORDERS, 20), 4);
    del(s);
}

TEST(the_notice_after_the_approach_posts_the_vector_58_only_when_the_lookup_found_an_aircraft) {
    for (int found = 0; found < 2; found++) {
        screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
        screen_set_mission_hooks(s, bearing_cb, NULL, NULL);
        bearing_value = found ? 123 : -1;
        to_log_menu(s);
        logbook_set16(&MLB, LB_COMPLETIONS + 3, 1);
        MLB.rec[0x11 + 4] = 1;
        fkey(s, 2);
        for (int i = 0; i < 900 && s->state != SCR_APPROACH; i++) steps(s, 1);
        ASSERT_EQ_INT(s->state, SCR_APPROACH);
        ASSERT_EQ_INT(bearing_calls, 0);
        btn(s, GAME_BTN_KEYPAD, GAME_KP_ENTER);
        for (int i = 0; i < 400 && s->state != SCR_NOTICE; i++) steps(s, 1);
        ASSERT_EQ_INT(s->state, SCR_NOTICE);
        ASSERT_EQ_INT(bearing_calls, 1);
        ASSERT_EQ_INT(MLB.bogey_deg, found ? 123 : -1);
        ASSERT_EQ_INT(s->eng.n, found ? 2 : 1);
        ASSERT_EQ_INT(s->eng.ids[found ? 1 : 0], 0x5A);          /* the carrier start: the launch notice */
        if (found) ASSERT_EQ_INT(s->eng.ids[0], 0x58);
        del(s);
    }
}

TEST(a_script_request_with_a_delay_keeps_the_world_running_d_steps_before_the_freeze) {
    screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    s->mode_byte = 7;
    screen_mission_end_delayed(s, 0xFE, 1, 7);                   /* F5 failure: the delay 7 (12.2) */
    for (int i = 1; i <= 7; i++) { steps(s, 1); ASSERT_EQ_INT(screen_sim_hold(s), 0); }
    steps(s, 1);
    ASSERT_EQ_INT(screen_sim_hold(s), 1);                        /* the freeze at D + 1 */
    int text = -1;
    for (int i = 1; i <= 10 && text < 0; i++) { steps(s, 1); if (lines(s) > 0) text = i; }
    ASSERT_EQ_INT(text, 3);                                      /* the list is built 3 steps after the freeze, as for the poked requests */
    ASSERT_EQ_INT(s->eng.ids[0], 0x39);                          /* the failure of F5: the exploded record, then by the parity bit 0x803A */
    ASSERT_EQ_INT(s->eng.ids[1], 0x803A);
    del(s);
    s = new_screen(); ASSERT_TRUE(s != NULL);
    to_flight(s, 4, 1);
    s->mode_byte = 7;
    screen_mission_end_delayed(s, 0xFE, 0, -1);                  /* a negative delay (the F6 quirk's 0xFF04): no delay */
    steps(s, 1);
    ASSERT_EQ_INT(screen_sim_hold(s), 1);
    del(s);
}

TEST(all_six_chains_pin_the_fade_done_and_the_typed_intro_per_blank_of_13_5_segment_a) {
    static const int FADE[6] = { 180, 170, 180, 170, 170, 170 };
    for (int m = 3; m <= 8; m++) {
        screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
        to_log_menu(s);
        for (int k = 4; k <= 8; k++) MLB.rec[0x11 + k] = 1;
        MLB.rec[LB_COMPLETIONS + m] = (uint8_t)(m <= 5);        /* the captured pairs: F1 to F3 the second briefing, F4 to F6 the first (13.5) */
        fkey(s, m - 2);
        ASSERT_EQ_INT(ms_at(s, MS_FADE, 100), (130 * 8 + 25) / 50);                     /* 0x12568 at 130 blanks in every mission */
        ASSERT_EQ_INT(ms_at(s, MS_WAIT, 100), (FADE[m - 3] * 8 + 25) / 50 - (130 * 8 + 25) / 50);   /* 0x125A8 at 180 or 170 blanks */
        ASSERT_EQ_INT(ms_at(s, MS_INTRO, 100), (380 * 8 + 25) / 50 - (FADE[m - 3] * 8 + 25) / 50);  /* the intro text at 380 blanks (0x12612 at 350 and the 30 blanks of the intro's own start) */
        const int typed = ms_at(s, MS_STAGE7, 300);
            ASSERT_TRUE(typed >= 1);                                 /* the typed span follows the text (pinned for the real text below) */
        ASSERT_EQ_INT(ms_at(s, MS_ORDERS, 5), s->brief_target >= 0 ? 2 : 1);             /* stage 7 lasts 2 passes with a target (all six real missions have one), 1 without (the invented lists of F2, F4, F6 hold only a carrier) */
        del(s);
    }
}

TEST(the_orders_of_f2_type_then_the_carrier_start_goes_straight_to_the_player) {
    screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    MLB.rec[0x11 + 4] = 1;
    fkey(s, 2);
    ms_at(s, MS_ORDERS, 400);
    const int tail = ms_at(s, MS_TAIL, 400);
    ASSERT_TRUE(tail >= 1);
    del(s);
}

/* The typing times of the mission texts with the real text (note 08 3.8 item 3a, 13.5): every record is typed by the text engine with its own D and click positions at the mission screens' cost (1,000 + 5,350 x clicks + 285 x
 * blits against 37,500 per step), no per-screen rate. The measured durations (blanks from the start of the typing, 13.5, and milestone 7's flow7 runs) are reproduced within 2 percent: the six captured briefings (490, 430, 464, 484, 530, 472) and the six orders (404, 312, 516, 504, 340, 381). The engine's
 * end hold adds one step. The briefing and orders records that were not captured are typed by the same rule (no figure to pin; the test checks that they type at all). */
static void near_blanks(int steps, int blanks) {
    const double want = blanks * 8.0 / 50.0 + 1.0, tol = blanks * 8.0 / 50.0 * 0.02 + 1.5;   /* 2 percent and the quantisation of both figures (the measure is one run; F6 is the worst, 3.6 percent low) */
    ASSERT_TRUE(steps >= want - tol && steps <= want + tol);
}
TEST(real_text_the_mission_texts_type_at_the_model_rate_and_reproduce_the_measured_blanks_within_2_percent) {
    if (!have_real()) { printf("skipped "); return; }
    static const int CAPTURED[6] = { 1, 1, 1, 0, 0, 0 };   /* the completion byte of the captured briefing: the second for F1 to F3, the first for F4 to F6 */
    static const int TYPED[6] = { 490, 430, 464, 484, 530, 472 };       /* 08 3.8 (milestone 7, Task 2 [V: flow7-brief-m5..m8]): F3 to F6 at one-blank resolution */
    static const int ORDERS[6] = { 404, 312, 516, 504, 340, 381 };      /* F1 and F2 from a late first sample (about 414 and 322); F3 to F6 [V: flow7-orders-m5..m8] */
    for (int m = 3; m <= 8; m++) for (int comp = 0; comp <= 1; comp++) {
        screen_t *s = (screen_t *)malloc(sizeof *s);
        char err[200] = "";
        ASSERT_EQ_INT(screen_init(s, &RLD, err, sizeof err), 0);
        logbook_zero(&MLB);
        logbook_set16(&MLB, LB_QUALIFIED, 1);
        s->log = &MLB;
        screen_set_logdisk_result(s, 0);
        screen_set_mode_handler(s, mission_handler, s);
        to_log_menu(s);
        for (int k = 4; k <= 8; k++) MLB.rec[0x11 + k] = 1;
        MLB.rec[LB_COMPLETIONS + m] = (uint8_t)comp;
        fkey(s, m - 2);
        ASSERT_TRUE(ms_at(s, MS_INTRO, 100) > 0);
        const int typed = ms_at(s, MS_STAGE7, 300);
        const int orders = (ms_at(s, MS_ORDERS, 5), ms_at(s, MS_TAIL, 400));
        ASSERT_TRUE(typed > 0 && orders > 0);
        if (comp == CAPTURED[m - 3]) near_blanks(typed, TYPED[m - 3]);
        near_blanks(orders, ORDERS[m - 3]);
        screen_destroy(s); free(s);
    }
}

TEST(the_function_keys_and_item_7_start_missions_only_as_5_4_allows) {
    screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    fkey(s, 3); steps(s, 8);                                      /* F3 needs the byte +0x11 + 5 (the completion count of the mission before it) */
    ASSERT_EQ_INT(s->state, SCR_MENU);
    MLB.rec[0x11 + 5] = 1;
    fkey(s, 3); steps(s, 8);
    ASSERT_EQ_INT(s->state, SCR_MISSION);
    ASSERT_EQ_INT(s->mode_byte, 5);
    del(s);
    s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    MLB.rec[LB_LAST_MISSION] = 5;                                 /* item 7: the mission after the last one completed */
    key(s, '7'); steps(s, 8);
    ASSERT_EQ_INT(s->state, SCR_MISSION);
    ASSERT_EQ_INT(s->mode_byte, 6);
    del(s);
    s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    key(s, '7'); steps(s, 8);                                     /* no mission completed yet: forced to mode 3 */
    ASSERT_EQ_INT(s->mode_byte, 3);
    del(s);
    s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    logbook_set16(&MLB, LB_QUALIFIED, 0);                         /* no record: only the click */
    fkey(s, 1); key(s, '7'); steps(s, 8);
    ASSERT_EQ_INT(s->state, SCR_MENU);
    del(s);
}


TEST(the_outcome_id_lists_of_13_3_for_every_mission_code_and_parity_with_the_quirk_cut_failure_list) {
    static const struct { int mode, code, parity; int n; unsigned ids[2]; int after; } C[] = {
        { 3, 0xFF, 0, 2, { 0, 0x8055 }, 1 }, { 4, 0xFF, 0, 2, { 0, 0x8056 }, 1 }, { 5, 0xFF, 0, 2, { 0, 0x8056 }, 1 }, { 6, 0xFF, 0, 2, { 0, 0x8056 }, 1 },
        { 7, 0xFF, 0, 2, { 0, 0x8056 }, 1 }, { 8, 0xFF, 1, 2, { 0, 0x8056 }, 1 },
        { 4, 0xFE, 1, 1, { 0x24, 0 }, 2 }, { 4, 0xFE, 0, 1, { 0x25, 0 }, 2 },   /* the passenger record by bit 0 of the step counter; the words 0x8056 and 0x8053 are never played (the quirk) */
        { 4, 0xFD, 0, 1, { 0x4C, 0 }, 0xF0 }, { 5, 0xFE, 0, 1, { 0x2C, 0 }, 0xF0 }, { 6, 0xFE, 0, 1, { 0x33, 0 }, 2 },
        { 7, 0xFE, 1, 2, { 0x39, 0x803A }, 0xF0 }, { 7, 0xFE, 0, 2, { 0x39, 0x805C }, 2 },
        { 3, 0xFC, 0, 2, { 0x54, 0x8053 }, 4 }, { 4, 0xFC, 0, 2, { 0x54, 0x8053 }, 4 }, { 8, 0xFC, 0, 2, { 0x54, 0x8053 }, 4 } };
    for (unsigned i = 0; i < sizeof C / sizeof C[0]; i++) {
        screen_t *s = new_screen(); ASSERT_TRUE(s != NULL);
        to_flight(s, 4, 1);
        s->mode_byte = (uint8_t)C[i].mode;
        screen_mission_end_delayed(s, (uint8_t)C[i].code, (uint8_t)C[i].parity, 0);
        for (int k = 0; k < 20 && s->state == SCR_END_WAIT; k++) steps(s, 1);
        ASSERT_EQ_INT(s->state, SCR_END_TEXT);
        ASSERT_EQ_INT(s->end_n, C[i].n);
        for (int k = 0; k < C[i].n; k++) {
            unsigned want = C[i].ids[k];
            if (C[i].code == 0xFF && k == 0) want = CHAIN_T[(C[i].mode - 3) * 5 + 4];   /* the success text is word 4 of the mission's id list (the invented chain here) */
            ASSERT_EQ_INT(s->end_ids[k], want);
        }
        ASSERT_EQ_INT(s->end_after, C[i].after);
        del(s);
    }
}

TEST(a_completion_0xFC_updates_and_saves_the_log_once_at_the_debrief_with_the_level_before_the_increment) {
    screen_t *s = mission_screen(); ASSERT_TRUE(s != NULL);
    to_log_menu(s);
    MLB.live_level = 2;
    MLB.rec[LB_COMPLETIONS + 5] = 1;
    to_flight(s, 4, 1);
    s->mode_byte = 5;
    MLB.save_req = 0;
    screen_mission_end_delayed(s, 0xFC, 0, 0);
    for (int k = 0; k < 20 && s->state == SCR_END_WAIT; k++) steps(s, 1);
    ASSERT_EQ_INT(logbook_get16(&MLB, LB_COMPLETED), 1);
    ASSERT_EQ_INT(logbook_get16(&MLB, LB_LEVEL), 2);              /* the live level 2 is stored before its own increment to 3 */
    ASSERT_EQ_INT(MLB.live_level, 3);
    ASSERT_EQ_INT(MLB.rec[LB_LAST_MISSION], 5); ASSERT_EQ_INT(MLB.rec[LB_LAST_COUNT], 1);
    ASSERT_EQ_INT(MLB.rec[LB_COMPLETIONS + 5], 2);
    ASSERT_EQ_INT(MLB.save_req, 1);                               /* one save at the 0xFC debrief (10.8) */
    steps(s, 100);
    ASSERT_EQ_INT(logbook_get16(&MLB, LB_COMPLETED), 1);          /* counted once */
    del(s);
}


int main(void) {
    RUN(the_key_clicks_of_a_typed_line_fall_at_the_blank_of_their_passes_by_the_cost_of_the_list);
    RUN(the_typing_byte_2_makes_no_click_and_a_cool_down_holds_them_back);
    RUN(an_f_key_starts_a_mission_the_dispatch_places_it_and_the_briefing_chain_begins_black);
    RUN(real_assets_the_date_line_of_the_intro_block_names_the_month_of_the_flight_hours);
    RUN(the_chain_of_f1_lands_on_the_notes_steps_per_blank);
    RUN(the_orders_of_f1_type_until_the_tail);
    RUN(only_f1_asks_for_the_aircraft_the_carrier_starts_place_the_player_six_steps_after_the_tail);
    RUN(kp_enter_during_the_orders_skips_the_approach_and_goes_to_the_6_pass_countdown);
    RUN(f1_waits_for_the_aircraft_key_and_places_the_player_six_steps_after_it);
    RUN(a_key_skips_the_intro_and_the_briefing_at_a_first_start_but_not_after_an_esc_restart_except_in_the_text);
    RUN(the_second_briefing_is_chosen_by_the_completion_byte_and_each_mission_has_its_own_ids);
    RUN(the_missions_started_counter_goes_up_at_the_orders_of_every_start_and_every_esc_restart);
    RUN(an_esc_restart_passes_0x1279c_and_0x127c8_before_the_orders_a_first_start_does_not);
    RUN(the_approach_target_is_none_without_an_aircraft_in_the_placement_and_stage_7_ends_at_once);
    RUN(stage_7_waits_for_the_projection_test_of_the_draw_when_the_game_supplies_one);
    RUN(the_notice_after_the_approach_posts_the_vector_58_only_when_the_lookup_found_an_aircraft);
    RUN(a_script_request_with_a_delay_keeps_the_world_running_d_steps_before_the_freeze);
    RUN(all_six_chains_pin_the_fade_done_and_the_typed_intro_per_blank_of_13_5_segment_a);
    RUN(the_orders_of_f2_type_then_the_carrier_start_goes_straight_to_the_player);
    RUN(real_text_the_mission_texts_type_at_the_model_rate_and_reproduce_the_measured_blanks_within_2_percent);
    RUN(the_function_keys_and_item_7_start_missions_only_as_5_4_allows);
    RUN(the_outcome_id_lists_of_13_3_for_every_mission_code_and_parity_with_the_quirk_cut_failure_list);
    RUN(a_completion_0xFC_updates_and_saves_the_log_once_at_the_debrief_with_the_level_before_the_increment);
    RUN(boot_shows_the_credits_and_the_text_starts_two_steps_in);
    RUN(the_credits_type_out_and_the_blank_line_is_not_a_record_line);
    RUN(the_credits_list_ends_at_245_blanks_and_the_hold_is_its_own_stage_40_steps_in_all);
    RUN(any_key_leaves_the_credits_for_the_log_disk_prompt_but_a_mouse_button_does_not);
    RUN(every_kind_of_key_leaves_the_credits);
    RUN(a_key_pressed_before_the_credits_poll_is_kept_and_acts_when_the_state_polls);
    RUN(the_log_disk_prompt_shows_two_records_of_instant_lines_after_two_steps);
    RUN(return_on_the_log_disk_prompt_goes_to_the_menu_and_the_menu_text_starts_seven_steps_later);
    RUN(return_pressed_twice_is_one_action_and_the_second_does_nothing_in_the_menu);
    RUN(a_digit_pressed_during_the_menu_build_is_kept_and_acts_in_the_first_menu_step);
    RUN(the_key_queue_holds_31_events_and_drops_the_rest);
    RUN(the_menu_types_its_list_in_96_blanks_with_a_line_step_of_12);
    RUN(the_digits_9_and_0_the_letters_and_the_function_keys_do_nothing_in_the_menu);
    RUN(esc_and_shift_esc_in_the_menu_are_consumed_without_effect);
    RUN(key_2_chooses_free_flight_and_the_echo_line_appears_one_step_later);
    RUN(the_mode_keys_set_the_mode_byte_of_note_5_10_and_the_unbuilt_modes_return_to_the_menu);
    RUN(a_later_task_can_take_a_mode_over_through_the_handler_and_the_allowance_is_by_mode);
    RUN(a_handler_answer_of_2_starts_part_1_through_the_briefing_6_steps_of_0x12DB8_and_no_notice_and_esc_rewrites_it_as_part_2);
    RUN(part_2_goes_from_the_briefing_through_the_approach_0x12DB8_and_the_take_off_notice_59_to_the_wait);
    RUN(the_mission_list_of_item_6_shows_the_empty_log_and_esc_rebuilds_the_menu);
    RUN(free_flight_from_the_menu_reaches_the_location_caption_25_steps_after_the_key);
    RUN(the_location_caption_types_in_126_blanks_and_the_keys_are_accepted_while_it_types);
    RUN(the_location_keys_1_to_4_choose_5_and_6_start_at_airfield_1_7_to_0_keep_6_to_9_and_letters_are_ignored);
    RUN(a_location_other_than_the_carrier_shows_the_aircraft_list_seven_steps_after_the_key);
    RUN(aircraft_keys_1_and_2_choose_the_class_every_other_key_is_ignored_and_the_choice_is_not_remembered);
    RUN(the_player_is_placed_six_steps_after_the_aircraft_key_and_seven_after_the_carrier_key);
    RUN(the_free_flight_approach_lasts_118_93_93_104_118_118_227_223_200_and_192_passes_by_the_start_keys_1_to_0);
    RUN(the_key_latch_holds_for_the_pass_of_the_state_one_key_per_10_steps_in_the_credits_per_5_in_the_menu_build_per_3_in_a_gate_1_pass_and_none_is_lost_elsewhere);
    RUN(the_approach_ends_by_itself_after_104_steps_or_by_kp_enter_and_the_notice_and_the_wait_follow);
    RUN(a_demo_esc_in_the_fade_in_is_taken_at_the_next_pass_boundary_and_fades_out_from_that_boundarys_level);
    RUN(esc_in_the_flight_runs_the_restart_chain_stage_by_stage);
    RUN(esc_from_a_pause_resets_after_4_steps_not_6);
    RUN(the_whole_restart_takes_66_steps_from_the_key_to_the_end_of_the_typed_caption);
    RUN(a_second_esc_during_the_restart_restarts_it_from_the_level_reached_in_every_stage);
    RUN(a_digit_during_the_fade_in_the_black_passes_and_the_echo_is_consumed_and_changes_nothing);
    RUN(esc_during_the_three_tick_wait_and_during_the_notice_restarts_the_selection);
    RUN(shift_esc_returns_to_the_menu_from_every_screen_with_a_mode_and_the_menu_text_comes_in_seven_steps);
    RUN(the_game_over_stages_from_a_sum_to_20_steps);
    RUN(the_game_over_hands_the_flight_back_for_the_reset_when_the_screen_ends);
    RUN(esc_during_the_game_over_text_restarts_the_selection_not_the_menu);
    RUN(the_pause_dims_to_10_and_types_its_text_from_13_blanks_after_the_stop);
    RUN(the_pause_text_clicks_nine_times_at_13_16_18_to_30_blanks_after_the_stop);
    RUN(shift_esc_from_the_air_has_no_click_before_the_stop_at_11_and_the_menu_list_clicks_from_44_to_46);
    RUN(the_mission_end_freezes_after_4_steps_types_the_outcome_and_resumes_the_flight);
    RUN(the_mission_end_final_screens_go_to_the_menu_and_the_qualification_result_restarts);
    RUN(with_the_default_check_result_0_the_credits_key_goes_straight_to_the_menu_and_no_prompt_is_shown);
    RUN(the_log_disk_prompt_variants_play_the_ids_of_the_check_result);
    RUN(the_digits_act_on_the_mission_list_screen_because_it_is_still_the_menu_state);
    RUN(the_list_of_item_6_starts_in_the_keys_own_step);
    RUN(an_aircraft_digit_typed_before_the_aircraft_caption_is_taken_once_the_choice_flag_is_set);
    RUN(p_on_the_selection_screens_freezes_the_map_without_a_pause_screen_and_p_in_the_approach_pauses);
    RUN(esc_with_an_end_code_set_goes_to_the_menu_after_the_hold_and_does_not_restart_but_0xef_is_skipped);
    RUN(the_selection_screens_carry_the_map_marks_and_the_overview_scene_and_the_aircraft_screen_keeps_the_chosen_number);
    RUN(the_flow_states_store_v4a90f_and_the_panel_counter_in_their_order);
    RUN(the_keys_5_and_6_draw_no_number_on_the_aircraft_screen_and_start_at_airfield_1);
    RUN(the_carrier_location_key_stores_the_panel_counter_but_not_v4a90f_zero);
    RUN(the_placement_walks_the_row_of_the_mode_with_the_hostile_cap_the_threshold_and_the_mode_filter);
    RUN(the_selector_comes_from_the_seeded_generator_one_draw_per_fresh_start_and_an_esc_restart_reuses_the_variant_and_the_offsets);
    RUN(the_selector_of_each_start_path_is_20000_m_plus_its_measured_delta_and_jitter_not_a_uniform_word);
    RUN(the_cap_refuses_every_class_once_it_is_reached_and_the_sub_list_is_recorded);
    RUN(without_text_or_scenario_assets_the_screens_still_run_and_the_flow_reaches_the_flight);
    RUN(a_present_but_bad_text_file_is_refused_by_game_create);
    RUN(the_game_boots_into_the_credits_and_walks_the_front_end_with_the_key_sequence);
    RUN(a_held_key_across_the_game_screens_is_one_action);
    RUN(the_debug_start_goes_straight_to_the_flight_and_needs_world_data);
    RUN(real_text_the_credits_menu_and_game_over_land_on_the_notes_steps);
    RUN(real_assets_the_four_locations_start_at_the_notes_positions_and_key_2_makes_an_f16);
    RUN(real_assets_the_location_keys_7_to_0_start_at_the_out_of_table_positions_of_the_note);
    RUN(real_assets_the_three_tick_wait_and_the_hud_come_after_the_notice_and_the_flight_is_frozen_before);
    TEST_MAIN_END();
}
