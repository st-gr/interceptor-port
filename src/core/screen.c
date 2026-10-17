/* src/core/screen.c - the front-end screens of note 08: see screen.h. Every number below is a section of docs/re/08-game-flow.md; the tags C and
 * NOTE-GAP are the note's. No game text lives here: the text records come from the assets and are copied into the scene. */
#include <stdlib.h>
#include <string.h>
#include "screen.h"
#include "entities.h"
#include "flight.h"
#include "missions.h"

/* blanks (50 Hz) to steps (note 05 17.17, note 08 5.1) */
#define BLANKS(b) (((b) * FLIGHT_STEP_HZ + 25) / 50)

/* The text engine's pacing. Note 08 3.8 gives a pass cost model (a pass costs 1,000 units plus 1,500 per click request plus 285 per glyph blit, 37,500 units per
 * step at 6 passes per blank) and says where the measured duration of a screen in blanks is the authority: for those lists the machine fits the units per step
 * so that the end of the list (V:4A898 = 1) falls at the measured number of blanks converted to steps (eng_list_timed: a dry run of the engine counts the units
 * of the list). Lists without a measurement run at the model's 37,500 units per step. After the list the end hold runs at the idle rate. */
#define BUDGET_STEP 37500L
#define BUDGET_HOLD 50000L   /* the 50-pass end hold is one stage of 8 blanks = 1 step (3.8 contract), not 50 passes at the idle rate of 37.5 per step */
#define PASS_COST 1000L
#define CLICK_COST 1500L
#define BLIT_COST 285L

/* The measured typing times of the lists, blanks from the start of the list to the end of the list (note 08 5.2 to 5.14). */
#define BLANKS_CREDITS 245          /* 5.2 */
#define BLANKS_LOGDISK 32           /* 5.3: L+12 to L+44 (results 2 and 3) */
#define BLANKS_LOGDISK_1 37         /* 5.3: result 1, L+12 to L+49 */
#define BLANKS_MENU 96              /* 5.4: the list is typed 96 blanks after the menu state first runs */
#define BLANKS_GAMEOVER 78          /* 5.12 */
#define BLANKS_LOCATION 126         /* 5.6 */
#define BLANKS_NOTICE 37            /* 6.2 */
#define BLANKS_NOTICE_DEMO 84       /* 12.14 N5: the two-record list [0x58, 0x5A] is 348 passes: typing 84 blanks = 13 steps, then the hold 1 step = 14 (the demo's earlier 7 undercounted) */
#define BLANKS_END_F1 143           /* 5.13: mission 1 success (ids 0x1E and 0x8055) */
#define BLANKS_END_M4_FAIL 180      /* 5.13: mission 2 failure (id 0x25) */
#define BLANKS_END_M9 238           /* 5.13: the qualification success (ids 0x4A and 0x8053) */

/* The durations of the flow in steps, each a conversion of the note's blanks; where the note gives cumulative blanks from one key the stages are the differences of
 * the cumulative steps, so that the totals are the note's (5.6: 26 steps to the caption; 5.11: 66 steps to the end of the typed location caption; 5.12: 22). */
#define STEPS_CREDITS_TEXT 2        /* 5.2: 13 to 15 blanks until the text starts */
#define STEPS_LOGDISK_TEXT 2        /* 5.3: the first record starts at L+12 */
#define STEPS_MENU_BUILD 7          /* 5.3, 5.4: RETURN or SHIFT-ESC to the first menu text: 43 and 44 blanks */
#define STEPS_MENU_BUILD_GAMEOVER 6 /* 5.12: the reset (8 blanks = 1) and the menu set-up (33 blanks = 5) after the end hold: 6 steps; the stages sum to 20 steps from A */
#define STEPS_LIST_TEXT 1           /* 5.4: key 6, the list is built at L+5 and back in the menu state from L+9: 5 to 9 blanks = 1 step (the list starts in the key's own step) */
#define STEPS_ECHO_TEXT_KEY 1       /* 5.4: the echo list starts 5 blanks after the key (in echo_enter) */
#define STEPS_ECHO_TEXT_RESTART 2   /* 5.11: the echo text at k = 55 against the reset at k = 41: 14 blanks */
#define STEPS_ECHO_TOTAL 7          /* 5.4: 43 to 45 blanks from the key; 5.11: 46 blanks from the reset */
#define STEPS_BLACK_FIRST 11        /* 5.6: the scene build, 71 blanks (45 to 116) = 11 steps */
#define STEPS_BLACK_RESTART 12      /* 5.11: the black passes, 73 blanks (87 to 160) = 12 steps */
#define STEPS_FADE_IN_FIRST 7       /* 5.6: the fade-in 116 to 160 = 44 blanks = 7 steps (7 + 11 + 7 = 25 steps from the key to the caption) */
#define STEPS_FADE_IN_RESTART 8     /* 5.11: the fade-in 160 to 207 = 47 blanks = 8 steps */
#define PAUSE_SLOW_EXTRA 16000L     /* 5.14: the pause screen's second pass is 3 blanks long: 16,000 units more than the pass cost (see pause_text) */
#define SHIFT_MENU_DELAY 30         /* FITTED PORT CHOICE (C), not a measured value: the best of a sweep of the delay against the 30 click starts of the sound7-restart capture (Task 5b report: 17 of 30 equal at 30, the maximum), because the port's menu list types its title first and the capture plays the instant lines first; the open question is the order of the real list (note 08 5.4, 5.11). 5.11: SHIFT-ESC from the air: the list is posted at k = 44 (the set-up pass of 33 blanks) and its clicks begin at k = 44 to 46; 30/25 of a step (7.2 blanks) after the build state's step puts the first click at k = 45 and agrees with 17 of the 30 click starts of sound7-restart (tools/vamiga-ref/sound7_check.py restart) */
#define STEPS_QUAL_BLACK 6          /* 9.2 (milestone 7) [V: flow7-qual-dispatch, flow7-qual-restart]: the qualification's scene build is 40 blanks, first start and ESC restart alike */
#define STEPS_QUAL_FADE_IN 7        /* 9.2: the fade-in of 44 blanks, both starts */
#define STEPS_QUAL_MAP 6            /* 9.2: the ESC restart's map stage (0x127C8 with the prompt skipped, k = 174 to 212) */
#define STEPS_QUAL_VERDICT 3        /* 9.2: RETURN to the briefing text 21 blanks (the first start); the verdict to the approach (the restart) */
#define STEPS_MAP 13                /* 5.11: the map stage k = 205 to 286, 33 to 46 steps (77 blanks in 5.6, 12 steps: the cumulative figure is used) */
#define STEPS_LOC_TO_AIRCRAFT 7     /* 5.6: the aircraft caption starts 41 to 48 blanks after the location key */
#define STEPS_LOC_TO_START 7        /* 5.6: the carrier start, 42 to 48 blanks */
#define STEPS_AIR_TO_START 6        /* 5.7: 35 to 40 blanks after the aircraft key */
#define STEPS_DEMO_WAIT 6           /* 7.5: the state 0x11B54 with the countdown 4: 5 passes and the transition pass, one step each (world-drawing passes) */
#define STEPS_DEMO_RESTART_WAIT 3   /* 7.7: three passes from the reset pass of 0x11AE2 to the set-up pass of 0x11B54 (16 blanks), one step each: 3 steps, not 4 */
#define STEPS_DEMO_TO_NOTICE 17     /* 7.5: the fade-in 7 steps, 0x11BD0 3 steps, 0x12D58 1 step and 0x12DB8 6 steps (one pass each) before the notice */
#define STEPS_DEMO_FADE 7           /* 7.5: the fade-in 0 to 15 at 3 blanks a level = 45 blanks = 7 steps, the first steps of the demo's SCR_APPROACH */
#define STEPS_APPROACH_TO_NOTICE (2 + 6)   /* 6.4, 8.1 (one conversion rule: the gate-on states count passes, one step each, in every mode): 0x12D58 2 passes, 0x12DB8 6 passes, then the notice (the notice 6, its hold 1 and the wait 4 follow) */
#define STEPS_SKIP_TO_NOTICE STEPS_APPROACH_TO_NOTICE   /* 8.1: a KP-ENTER skip is the same 2 + 6 passes (the 14 of the log is blanks) */
#define STEPS_QUAL_TO_NOTICE 7      /* 9.2 (C): the qualification's chain after the briefing has the states 0x12D58 (1 pass) and 0x12DB8 (6 passes) of the demo's derivation (7.5), one pass per step: 1 + 6 = 7; the 12 blanks of camera-mode-6 cockpit detection of the KP-ENTER chain do not exist in mode 9 */
#define STEPS_TRAIN1_POST BLANKS(24)   /* 8.1: part 1: the briefing record is posted at blank 24 from the countercode answer. (C): the port skips the prompt and counts the blanks from the dispatch instead of the answer */
#define STEPS_TRAIN2_POST BLANKS(67)   /* 8.1: part 2: at blank 67 from the answer (C, same conversion) */
#define STEPS_TRAIN2_RESTART_POST BLANKS(395)   /* 8.1 [V]: after an ESC restart the part 2 briefing is posted 395 blanks (about 63 steps) after the ESC, counted from the ESC: the reset, the echo line 0x68, the scene at 87, the fade at 172, the skipped prompt set-up at 220, 315 */
#define STEPS_TRAIN1_TYPED 59          /* 8.1: the record 9 types from blank 24 to 659: about 2,230 passes = 59 steps at 37.5 passes per step (the end hold, 1 step, follows) */
#define STEPS_TRAIN2_TYPED 95          /* 8.1: the record 10, blank 67 to 883: about 3,556 passes = 95 steps */
#define STEPS_DB8 6                    /* 8.1: 0x12DB8: the countdown 5 runs six world-drawing passes, one step each (part 1: blank 668 to 705, 37 blanks) */
#define STEPS_TRAIN2_APPROACH 29            /* 12.14 C13 [V: flow7-training-7D]: the camera approach of mode 6 runs the mover from stage 3 for 29 passes (the counter 17 to 46: stage 6 at 25, camera mode 0 at 29); a state that runs the world counts one pass per step (3.8 item 4), so not the 401 blanks converted to 64 steps */
#define STEPS_TRAIN2_TO_DB8 2               /* 12.14: part 2: 0x12D58's 2 passes after the mover (the cockpit view at 30, 0x12DB8 at 31: 31 passes in all); 0x12DB8's six passes follow */
#define STEPS_WAIT 4                /* 6.2: four steps after the step that clears the counter, the flight state in the fifth */
#define STEPS_PAUSE_TEXT BLANKS(11) /* 5.14: the fade 15 to 10 takes 11 blanks, then the text starts */
#define STEPS_END_FREEZE 4          /* 5.13: the dispatcher takes the request 4 flight passes later; by exception 4 of the 3.8 contract they count one per step */
#define STEPS_LOST_FREEZE BLANKS(14) /* 8.9: a blank outcome record (the lost lead of part 2): the freeze (pause byte 1 at 0x131F4, blank 34) to the resumed flight (blank 48) is 14 blanks = 2 steps */
#define STEPS_END_LIST BLANKS(42)   /* 5.13: the list is built and starts at k = 42 */
#define STEPS_END_HOLD_PASSES BLANKS(17)   /* 5.13: the 100-pass countdown of the final screens */
#define STEPS_GAMEOVER_TEXT 1       /* 5.12: the text starts at k = 12 (A is k = 6): 6 blanks after A */
/* The briefing chain of the missions (note 08 13.1, 13.5; the figures are blanks from the F key converted by (blanks x 8 + 25) / 50 as cumulative spans, less the 7 steps from the key to the
 * dispatch that the echo screen already counts). */
#define STEPS_MS_FADE_START (BLANKS(130) - STEPS_ECHO_TOTAL)    /* 0x12568: the fade-in starts at 130 blanks (21 steps from the key) */
#define STEPS_MS_INTRO_STATE (BLANKS(350) - STEPS_ECHO_TOTAL)   /* 0x12612: the intro countdown ends at 350 blanks (56 steps) */
#define STEPS_MS_INTRO_TEXT 5       /* the text of the intro block and the briefing starts at 380 blanks, 5 steps after 0x12612 */
#define STEPS_MS_STAGE7 2           /* 13.4: the camera approach's stage 7 lasts two passes (the draw's projection test fires in the pass after the target choice) */
#define STEPS_ESC_DETOUR 7          /* 12.14 N6: the menu set-up 0x11D30 (36 blanks) and the menu state again (8): 44 blanks = 7 steps added to an ESC restart that carries a request 3 */
#define STEPS_MS_RESTART 7          /* 5.5, 13.1 (C): an ESC restart (V:4A8E3 set) passes 0x1279C (countdown 2) and 0x127C8 (the restart branch stores countdown 3 and goes to 0x128DA): 3 + 4 world-running passes, one step each, before the orders */
#define STEPS_MS_TAIL 6             /* 13.5: 0x12A92's four passes, 0x12AC0 (camera mode 4 seen) and 0x12AFC, one each */
#define STEPS_MS_CARRIER_START 6    /* 13.5: the step counter reads 29 at 0x12AC0 and 35 at 0x12D58 (F1 and F2 alike): six passes (not the blank conversion of 40 blanks) from the end of the orders' tail to the player start */
#define STEPS_MS_PROMPT 3           /* 13.5 (F1): 0x12AFC at 494 to 0x12C02 at 542 blanks: the airfield start's aircraft prompt */
#define BLANKS_MS_PROMPT 40         /* 13.5 (F1): the aircraft prompt types in 40 blanks and holds 14 */
#define BLANKS_NOTICE_MISSION BLANKS_NOTICE_DEMO   /* 12.14 N5: the same two-record list in the missions: 13 steps typed + 1 hold = 14 */

/* Key types of the queue. */
enum { SK_ESC = 1, SK_SHIFT_ESC, SK_RETURN, SK_DIGIT, SK_KPENTER, SK_PAUSE, SK_OTHER, SK_FKEY };

/* Text ids the machine plays (note 08 2.1, 5.2 to 5.14). */
#define ID_CREDITS 0x0F
#define ID_LOGDISK_RESULT1 0x44     /* the check results 1, 2, 3 show the ids 0x44, 0x45, 0x46 (5.3) */
#define ID_LOGDISK_HELP 0x5E
#define ID_MENU_TITLE 6
#define ID_MENU_HEADER 0x64
#define ID_MENU_ITEM1 0x65          /* ids 0x65 to 0x6C are the eight items, and the echo lines of the modes */
#define ID_MENU_ESC 0x6D
#define ID_LIST_HEADER 0x40
#define ID_LIST_HINT (0x8000 | 0x6E)
#define ID_LOCATION 7
#define ID_AIRCRAFT 0x4B
#define ID_NOTICE_TAKEOFF 0x59
#define ID_NOTICE_LAUNCH 0x5A
#define ID_MS_INTRO 0x5B
#define ID_MS_ORDERS_HEAD 0x5D
#define ID_NOTICE_VECTOR 0x58       /* 7.5: the outcome lookup of mode 3 posts the vector notice before the launch notice (its bearing patch is done: 13.2) */
#define ID_PAUSE 0x26
/* Task 8 (note 08 sections 9 and 10) */
#define ID_WELCOME_FIRST 1
#define ID_WELCOME_AGAIN 2
#define ID_CALLSIGN 3
#define ID_BRIEFING_QUAL 0x49
#define ID_LOG_PAGE 0x57
#define ID_MISSION_F2 0x4D          /* ids 0x4D to 0x51: the F2 to F6 lines of the mission list (2.4) */
#define STEPS_WELCOME_TEXT 2        /* 5.3 (C): the welcome entered from the log-disk prompt's RETURN or from UPDATE has no capture of its own and borrows the log-disk prompt's start (L+12 blanks) */
#define STEPS_WELCOME_FROM_CREDITS BLANKS(116)   /* 10.4 [V]: the welcome text starts 116 blanks after the credits key (about 19 steps) ... */
#define STEPS_WELCOME_CREDITS_WAIT BLANKS(87)    /* ... of which 87 blanks are the credits state's own wait (the credits stay on screen) */
#define BLANKS_WELCOME_AGAIN 105     /* 10.4 [V]: the continuing welcome (id 2) is typed over 105 blanks */

/* ---------------------------------------------------------------------------------------------------------------------------------------------
 * The text engine (note 08 3.4), one call per pass. It drives the lines[] the scene shows.
 * ------------------------------------------------------------------------------------------------------------------------------------------ */

static void eng_pass(screen_t *s, int *clicks, int *blits);

static void eng_clear(screen_t *s) {
    s->eng.nlines = 0;
    s->eng.n = s->eng.cur = 0;
    s->eng.running = 0;
    s->eng.mode_byte = 0xFF;     /* the reset block: V:4A898 = 0xFF (5.11) */
    s->eng.hold = 0;
    s->eng.between = 0;
    s->eng.line_delay = 0;
    s->eng.step = 11;
    s->eng.budget = 0;
    s->eng.per_step = BUDGET_STEP;
    s->eng.idle_step = BUDGET_HOLD;
    s->eng.click_cost = CLICK_COST;
    s->eng.pass_cost = PASS_COST; s->eng.blit_cost = BLIT_COST; s->eng.exact = 0;
    s->eng.passes = 0;
}

/* Starts a list: ids[] with bit 15 (continuation) and bit 14 allowed, ended by the count. The text already on screen stays (pixels are not erased by a list). */
static void eng_list(screen_t *s, const uint16_t *ids, int n, long per_step) {
    if (n > SCREEN_LIST_IDS) n = SCREEN_LIST_IDS;
    for (int i = 0; i < n; i++) s->eng.ids[i] = ids[i];
    s->eng.n = n;
    s->eng.cur = 0;
    s->eng.running = 0;
    s->eng.between = 0;
    s->eng.line_delay = 0;
    s->eng.budget = 0;
    s->eng.per_step = per_step;
    s->eng.click_cost = CLICK_COST;
    s->eng.pass_cost = PASS_COST; s->eng.blit_cost = BLIT_COST; s->eng.exact = 0;
    s->eng.mode_byte = 0;
    s->eng.hold = 0;
    if (n == 0) { s->eng.mode_byte = 1; s->eng.hold = 50; }   /* nothing to play: the end hold alone (so that a state waiting for the flag does not wait for ever) */
}
/* A list whose typing time was measured (blanks from the start of the list to its end): a dry run of the engine counts the units up to the pass that ends the
 * list and the rate per step is set so that this pass falls blanks*8/50 steps after the start (note 08 3.8, "may stretch the budget per screen"). */
static void eng_list_timed_steps(screen_t *s, const uint16_t *ids, int n, int steps);
static void eng_list_timed(screen_t *s, const uint16_t *ids, int n, int blanks) {
    if (blanks <= 0) { eng_list(s, ids, n, BUDGET_STEP); return; }
    eng_list_timed_steps(s, ids, n, BLANKS(blanks));
}
/* The same with the duration given in steps (note 08 8.1 counts the briefings in steps by the pass rules: text-only states 37.5 passes per step). */
static void eng_list_timed_steps(screen_t *s, const uint16_t *ids, int n, int steps) {
    eng_list(s, ids, n, BUDGET_STEP);
    if (steps <= 0 || !s->text || n == 0) return;
    screen_t *t2 = (screen_t *)malloc(sizeof *t2);
    if (!t2) return;
    memset(t2, 0, sizeof *t2);
    t2->text = s->text;
    t2->eng = s->eng;
    long units = 0, before = 0;
    for (int i = 0; i < 400000; i++) {
        int c, b;
        before = units;
        eng_pass(t2, &c, &b);
        units += PASS_COST + CLICK_COST * c + BLIT_COST * b;
        if (t2->eng.cur >= t2->eng.n) break;   /* the pass that played the last record's end: the list is over */
    }
    free(t2);
    long rate = steps > 0 ? before / steps : BUDGET_STEP;
    if (rate < PASS_COST) rate = PASS_COST;
    s->eng.per_step = rate;
}
/* A mission text (the intro block and briefing, the orders): the same engine and the same budget of 37,500 units per step, with the mission screens' click cost (note 08 3.8 item 3a, C): each record is typed with its own
 * D and click positions, no per-screen rate. */
static void eng_list_mission(screen_t *s, const uint16_t *ids, int n) {
    eng_list(s, ids, n, BUDGET_STEP);
    s->eng.click_cost = MS_CLICK_COST;
}
static void eng_list1(screen_t *s, uint16_t id) { eng_list(s, &id, 1, BUDGET_STEP); }
static void eng_list1_timed(screen_t *s, uint16_t id, int blanks) { eng_list_timed(s, &id, 1, blanks); }

/* The line at index `line` of the current record begins (rule 7 / rule 2): flags, pen and delay, position, and the line joins the scene. */
static void eng_line_begin(screen_t *s) {
    const text_t *T = s->text;
    const text_line_t *L = &T->line[s->eng.rec->first_line + s->eng.line];
    s->eng.flags = L->flags;
    s->eng.pen = (uint8_t)(L->colour >> 4);
    s->eng.D = (uint8_t)(L->colour & 15);
    if (s->eng.line == 0 && s->eng.bit14) s->eng.D = 2;   /* rule 2: bit 14 of the id overrides the first line's delay */
    if (s->eng.line > 0) { s->eng.row = s->eng.last_row + s->eng.step; }
    s->eng.cell = 0; s->eng.cd = 0; s->eng.pending = 0; s->eng.lp = 0;
    s->eng.slot = -1;
    if (L->length > 0 && s->eng.row < 200 && s->eng.nlines < GAME_MAX_TEXT) {
        game_text_t *g = &s->eng.lines[s->eng.nlines];
        g->id = s->eng.rec->id;
        g->row = (uint8_t)s->eng.row;
        g->col = (uint8_t)s->eng.col;
        g->pen = s->eng.pen;
        g->flags = L->flags;
        g->step = (uint8_t)s->eng.step;
        g->shown = 0;
        g->length = L->length;
        memcpy(g->text, T->text + L->text, L->length);
        g->text[L->length] = '\0';
        if (s->log && s->have_months) s->log->months = s->months;   /* the date line's month names (10.9) */
        if (s->log) logbook_patch_line(s->log, s->eng.rec->id, s->eng.line, g->text, L->length);   /* Task 8 hook: the runtime-patched records of 2.4 (10.6) */
        s->eng.slot = s->eng.nlines++;
    }
    s->eng.last_row = s->eng.row;
    s->eng.last_col = s->eng.col;
}

/* Rule 2: a record begins. Returns 1 when a record with lines was started (the first line is handled in this same pass), 0 when the id names no record
 * (an empty record: it ends at once, the next one begins on the next pass). */
static int eng_record_begin(screen_t *s) {
    const uint16_t id = s->eng.ids[s->eng.cur];
    const unsigned rid = id & 0x3FFFu;
    const text_t *T = s->text;
    if (T && rid >= 1 && rid <= (unsigned)T->id_count && T->id[rid - 1].line_count > 0) {
        const text_id_t *rec = &T->id[rid - 1];
        const text_line_t *first = &T->line[rec->first_line];
        s->eng.rec = rec;
        s->eng.bit14 = (id & 0x4000u) != 0;
        s->eng.line = 0;
        if (id & 0x8000u) {   /* the continuation: two line steps below the last line of the previous record, in its column (2.1) */
            s->eng.row = s->eng.last_row + 2 * s->eng.step;
            s->eng.col = s->eng.last_col;
        } else {
            s->eng.row = first->row;
            s->eng.col = first->col;
        }
        s->eng.hold = 0; s->eng.line_delay = 0; s->eng.between = 0; s->eng.last = 0;
        s->eng.mode_byte = 0;
        s->eng.running = 1;
        s->eng.first_pass = 1;
        eng_line_begin(s);
        return 1;
    }
    s->eng.cur++;
    if (s->eng.cur >= s->eng.n) { s->eng.mode_byte = 1; s->eng.hold = 50; }
    return 0;
}

static void eng_line_end(screen_t *s) {
    const text_line_t *L = &s->text->line[s->eng.rec->first_line + s->eng.line];
    s->eng.line_delay = 4 * (int)L->delay;
    s->eng.between = 1;
    s->eng.last = s->eng.line + 1 >= s->eng.rec->line_count;
}

/* One pass of the engine. *clicks and *blits feed the cost model of 3.8. */
static void eng_pass(screen_t *s, int *clicks, int *blits) {
    *clicks = 0; *blits = 0;
    if (s->eng.cur >= s->eng.n) {   /* rule 1: no text */
        s->eng.step = 11;
        if (s->eng.hold > 0 && --s->eng.hold == 0) s->eng.mode_byte |= 0x80;
        s->eng.running = 0;
        return;
    }
    if (!s->eng.running) {
        if (!eng_record_begin(s)) return;
    } else s->eng.first_pass = 0;
    if (s->eng.between) {   /* rule 3 */
        if (s->eng.line_delay > 0 && --s->eng.line_delay > 0) return;
        s->eng.between = 0;
        if (s->eng.last) {   /* rule 8 */
            s->eng.running = 0;
            s->eng.cur++;
            if (s->eng.cur >= s->eng.n) { s->eng.mode_byte = 1; s->eng.hold = 50; }
            return;
        }
        s->eng.line++;
        eng_line_begin(s);
    }
    const text_line_t *L = &s->text->line[s->eng.rec->first_line + s->eng.line];
    game_text_t *gl = s->eng.slot >= 0 ? &s->eng.lines[s->eng.slot] : NULL;
    const uint8_t *txt = gl ? (const uint8_t *)gl->text : s->text->text + L->text;   /* the engine reads the record as the game patched it (Task 8 hook) */
    if (!(s->eng.flags & 1)) {   /* rule 4: an instant line is drawn twice, the click after the first */
        if (s->eng.lp == 0) {
            if (gl) gl->shown = L->length;
            *blits += L->length;
            *clicks += 1;
            s->eng.lp = 1;
        } else {
            *blits += L->length;
            eng_line_end(s);
        }
        return;
    }
    /* rule 5: a typed line, one character per D passes, each drawn twice; the FIRST character of a typed record is drawn on passes 0, 1 and 2 with click requests on
     * passes 0 and 1 (the review of note 08), which is why the following characters start one pass later than on a later line */
    if (s->eng.cd > 0) s->eng.cd--;
    if (s->eng.pending) {
        *blits += 1;
        if (s->eng.pending == 2) *clicks += 1;   /* the second pass of the record's first character requests the click again */
        if (--s->eng.pending == 0) s->eng.cell++;
        return;
    }
    if (s->eng.cd > 0) return;
    if (s->eng.flags & 2) while (txt[s->eng.cell] == ' ') s->eng.cell++;   /* spaces cost nothing */
    const uint8_t c = txt[s->eng.cell];
    if (c == 0) { eng_line_end(s); return; }
    if (c & 0x80) { eng_line_end(s); s->eng.last = 1; return; }   /* a text byte with bit 7 set ends the RECORD (the review of note 08); the extractor refuses such bytes */
    const int first = s->eng.first_pass && s->eng.line == 0 && s->eng.cell == 0;
    if (gl) gl->shown = (uint16_t)(s->eng.cell + 1);
    *blits += 1;
    if (c != ' ') *clicks += 1;
    s->eng.pending = first ? 2 : 1;
    s->eng.cd = s->eng.D + (first ? 1 : 0);
}

static void eng_run(screen_t *s) {
    const long step_cost = s->eng.cur >= s->eng.n ? s->eng.idle_step : s->eng.exact && s->snd ? 6000L * sound_step_blanks(s->snd) : s->eng.per_step;
    s->eng.budget += step_cost;   /* the typing rate while there is text to play, the idle rate for the end hold */
    while (s->eng.budget > 0) {
        int clicks, blits;
        const int active = s->eng.cur < s->eng.n;
        eng_pass(s, &clicks, &blits);
        s->eng.passes++;
        s->eng.budget -= s->eng.pass_cost + s->eng.click_cost * clicks + s->eng.blit_cost * blits + (s->eng.exact && s->eng.passes == 2 ? PAUSE_SLOW_EXTRA : 0);   /* the pause screen's second pass is 3 blanks long (08 5.14) */
        if (s->snd && sound_flag(s->snd) < 2) {   /* the key click 0x37C2E (volume 2) for each request, unless the typing-sound byte V:4A88F is 2 or more; the cool-down V:4A942 is the wrapper's test (note 08 3.6, note 10 7.4) */
            /* each request is a tone call of its own pass, made at the library blank floor(c x nb / B) of the step (note 10 5.7 item 13 and 9.2): c is the cost the step's passes have used up to and including this
             * pass, nb the step's blanks (6 or 7) and B the budget of the list in force, step_cost (37,500 by default, the measured rate of a stretched list), so a request cannot be later than the step's last
             * blank. Requests closer than the click's four blanks cut the earlier one; the library runs its blanks up to that one before the call. The cost of a pass is spent at its end (the request is
             * the end of the pass's work, the typed character having been drawn). */
            if (clicks > 0) {
                const long c = step_cost - s->eng.budget;
                const int nb = sound_step_blanks(s->snd);
                long b = step_cost > 0 ? c * nb / step_cost : 0;
                sound_run_to(s->snd, b < 0 ? 0 : b > nb - 1 ? nb - 1 : (int)b);
            }
            for (int c = 0; c < clicks; c++) sound_event(s->snd, SND_CLICK, 2, 0);
        }
        if (active && s->eng.cur >= s->eng.n) { s->eng.budget = 0; break; }   /* the list is over: the end hold counts its 50 passes at the idle rate from the next step */
    }
}

int screen_text_finished(const screen_t *s) { return (s->eng.mode_byte & 0x80) != 0; }

/* ---------------------------------------------------------------------------------------------------------------------------------------------
 * The states.
 * ------------------------------------------------------------------------------------------------------------------------------------------ */

/* A state entered by the timers starts at t = 0 and the end of the step counts it to 1; one entered by a key (delivered inside the step, before the count) or by a call
 * from game.c (after the step) starts at t = 1. Either way "+n steps" fires in the n-th screen_step after the key, or the n-th after the step that entered it. */
static void enter(screen_t *s, screen_state_t st) { s->state = st; s->t = s->in_timers ? 0 : 1; }

/* Where the text records cover the scene: palette regime and the mode a state shows. */
static void show(screen_t *s, game_mode_t mode, int regime) { s->mode = mode; s->regime = (uint8_t)regime; }

/* The flow states' stores of V:4A90F and V:4A8EE (screen.h out_90f, out_panel). */
static void s90f(screen_t *s, int v) { s->out_90f = (int8_t)v; s->out_90f_set = 1; }
static void span(screen_t *s) { s->out_panel = 1; }
static void sredraw(screen_t *s) { s->out_redraw = 1; }

/* The main-menu build 0x11D30 (note 10 6.2, 7.2): the target 0x1F0000; the level at once when the music is loaded and the typing-sound byte was 0 (back from a flight); the menu music 0x19CE6(0x0F) (it starts only
 * with the byte at 0); the byte 1: the clicks are on and the ramp runs. */
static void snd_menu_build(screen_t *s) {
    sound_t *q = s->snd;
    if (!q) return;
    sound_master(q, 31, -1, -1);
    if ((q->en13 & 0x80) && sound_flag(q) == 0) sound_master(q, -1, 31, -1);
    sound_event(q, SND_PHRASE, 0, 0x0F);
    sound_master(q, -1, -1, 1);
}
static void menu_build_in(screen_t *s, int steps, int reset) {   /* RETURN on the log-disk prompt, SHIFT-ESC, the end of a game over, a sub-screen left (5.3, 5.4, 5.11, 5.12) */
    s->menu_steps = steps;
    if (s->shift_defer && s->snd) s->shift_blank = 0;   /* SHIFT-ESC from the air: the sound waits for its blank (shift_sound) */
    else { s->shift_defer = 0; snd_menu_build(s); }
    eng_clear(s);
    s->mode_byte = 0;
    s->paused = 0;
    s->demo = 0; s->direct_restart = 0;
    s->mission = 0; s->cam_mode = 0xFF;
    if (reset) s90f(s, 1);   /* the reset 0x1D1BE (state 0x11A70: SHIFT-ESC, a flight's end): 0x1D25E stores V:4A90F = 1. The menu build entered from the log-disk RETURN, the credits, the welcome or a left sub-screen (0x11E04) follows no reset: the start-up's 0xFF stays (a first demo shows no map marks, note 06) */
    show(s, GAME_MODE_MENU, 0);
    enter(s, SCR_MENU_BUILD);
}
static void menu_enter_in(screen_t *s, int steps) { menu_build_in(s, steps, 1); }
static void menu_enter(screen_t *s) { menu_enter_in(s, STEPS_MENU_BUILD); }
static void menu_enter_fresh(screen_t *s) { menu_build_in(s, STEPS_MENU_BUILD, 0); }   /* no reset block before it */

static void leave_flight(screen_t *s) { s->out_leave = 1; s->sim_hold = 0; s->paused = 0; s->end_code = 0; }

static uint16_t echo_id(uint8_t mode) {
    switch (mode) {
    case 0x7F: return ID_MENU_ITEM1;
    case 1: return ID_MENU_ITEM1 + 1;
    case 2: return ID_MENU_ITEM1 + 2;
    case 0x7D: return ID_MENU_ITEM1 + 3;
    case 9: return ID_MENU_ITEM1 + 4;
    default: return 0x6B;   /* a mission started with item 7 (5.4) */
    }
}

/* The echo screen: the line of the chosen item, then the dispatch. */
static void echo_enter(screen_t *s, int restart) {
    if (s->snd && s->mode_byte > 0) sound_master(s->snd, 0, -1, -1);   /* 0x11E50: the echo state of a mode key sets the master target to 0 (the ramp, if the byte is 1, fades the music; note 10 6.2) */
    eng_clear(s);
    s->echo_restart = (uint8_t)restart;
    show(s, GAME_MODE_MENU, 0);
    enter(s, SCR_ECHO);
    if (!restart && !s->echo_fkey) eng_list1(s, echo_id(s->mode_byte));   /* 5 blanks after the key: the line is on the screen after the key's own step (a key-entered state cannot fire at +1 in its timers) */
}

/* The mission start (6.1, 6.3): the allowance by mode and the scenario placement. */
void screen_random_seed(screen_t *s, uint32_t seed) { s->rng = seed; }

/* The selector V:4ABB0 (note 07 2.4, "The distribution of the selector" and "The other paths, measured"): the microseconds of the system time are 20,000 x m (the blank's number in the second, 0..49) plus
 * the delta of the start path plus the clock's jitter; the selector is the low word. Two draws of the seeded generator: m, then the jitter. Deltas [V: measured, one start each except the demonstration, 07 2.4]:
 * the demonstration 1,172 [V, 13 starts] (and the demo's other row 0x7E, not measured: "it uses the demonstration's delta", C); free flight (mode 1) 1,620; the flight school part 1 (mode 2) 1,888 and part 2
 * (mode 0x7D) 335; the missions 3, 4, 5, 6, 7, 8: 1,612, 1,620, 1,597, 1,594, 1,619, 1,635; the qualification (mode 9) 583. The jitter list is the 13 demonstration starts [V]
 * (delta - 1,172 of 0 eight times, +1 twice, -2, -4 and -6 once each); drawing from it on every path is the note's rule for the port (C, 07 2.4: "the other paths have one start each, so their spread is unmeasured"). A mode that places nothing
 * (07 2.4: "other modes place nothing") gets the demonstration's delta (C): the selector is not used there. */
unsigned screen_draw_selector(uint32_t *rng, int mode) {
    static const int8_t jitter_list[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, -2, -4, -6 };
    const unsigned m = entities_random16(rng) % 50u;
    const int jitter = jitter_list[entities_random16(rng) % 13u];
    int delta;
    switch (mode) {
    case 1: delta = 1620; break;
    case 2: delta = 1888; break;
    case 3: delta = 1612; break;
    case 4: delta = 1620; break;
    case 5: delta = 1597; break;
    case 6: delta = 1594; break;
    case 7: delta = 1619; break;
    case 8: delta = 1635; break;
    case 9: delta = 583; break;
    case 0x7D: delta = 335; break;
    default: delta = 1172; break;   /* 0x7E, 0x7F (the demonstration) and the modes that place nothing */
    }
    return (unsigned)((int)(m * 20000u) + delta + jitter) & 0xFFFFu;
}

static int start_flight(screen_t *s, int fixed, unsigned fixed_sel);
int screen_start_flight(screen_t *s) { return start_flight(s, 0, 0); }
/* The same start with the selector word V:4ABB0 given instead of drawn (the rigs poke it into the original; a restart ignores it as the original does). */
int screen_start_flight_selector(screen_t *s, unsigned selector) { return start_flight(s, 1, selector & 0xFFFFu); }

static int start_flight(screen_t *s, int fixed, unsigned fixed_sel) {
    const int mode = s->mode_byte;
    s->view_kind = SV_COCKPIT;
    s->v4a848 = 0;   /* the mission start clears V:4A848 (the demo's set-up sets it again after its own placement, 7.5) */
    s->allowance = mode == 1 ? 5 : mode == 0x7D ? 1 : 3;   /* 6.3 */
    s->placed_count = 0; s->hostile_count = 0; s->hostile_cap = 0; s->sub_first = s->sub_count = 0;
    /* The selector (note 07 2.4): the original reads the system timer at every mission start; the port draws one 16-bit word from the seeded generator. A restart (the ESC
     * restart, V:4A8E3) uses the stored variant and the stored offsets. k = 0 gives variant 0 and leaves the stored variant alone. */
    unsigned var;
    if (!s->echo_restart) {
        const unsigned sel = fixed ? fixed_sel : screen_draw_selector(&s->rng, mode);
        const unsigned k = sel & 3u;
        s->selector = (uint16_t)sel;
        var = k == 0 ? 0 : k - 1;
        if (k != 0) s->variant = (uint8_t)var;
        s->pl_dx = (int)((sel >> 1) & 3u); if (sel & 0x10u) s->pl_dx = -s->pl_dx;
        s->pl_dz = (int)((sel >> 2) & 3u); if (sel & 0x20u) s->pl_dz = -s->pl_dz;
    } else {
        var = s->variant;
    }
    const scenario_t *sc = s->scn;
    const int rowi = (mode >= 1 && mode <= 9) ? mode - 1 : (mode == 0x7E || mode == 0x7F) ? 9 : -1;   /* the 0-based row of table 0x2DEDA (6.1); 0x7D is the formula of 0x2CFDA */
    if (!sc || rowi < 0 || rowi >= sc->row_count) return 0;
    if (var >= SCENARIO_VARIANTS) var = 0;
    const scenario_row_t *r = &sc->row[rowi];
    s->hostile_cap = (uint8_t)r->count[var];
    const scenario_list_t *l = &sc->list[r->list[var]];
    const unsigned sel = s->log ? logbook_sublist_selector(s->log, (mode >= 1 && mode <= 9) ? mode : 0) : 0;   /* the unlock byte V:1D024 + 0x12 + mode of the player record (10.2, 10.3 rule 3); no log or the demo rows give 0 */
    const scenario_sublist_t *sl = &sc->sublist[l->sub[sel & 3]];
    s->sub_first = sl->first; s->sub_count = sl->count;
    const unsigned progress = s->log ? s->log->live_level : 0;   /* V:4A95F, the pilot's progress counter (10.2) */
    unsigned taken = 0;   /* the slots placed so far: a valid record in the slot skips the entry (note 07 2.1 step 2) */
    for (unsigned k = 0; k < sl->count && s->placed_count < SCREEN_MAX_PLACED; k++) {
        const scenario_entry_t *e = &sc->sub_entry[sl->first + k];
        const unsigned flags = e->w[4];
        if (k > 0 && (flags & 0x7Fu) > progress) continue;           /* the first entry is placed without the threshold test */
        const unsigned mf = (flags >> 8) & 15u;
        if (mf && mf != (unsigned)mode) continue;                     /* bits 8 to 11: the mode filter */
        const unsigned slot = e->w[2] & 0x7Fu;
        if (slot < 32 && (taken & (1u << slot))) continue;            /* a valid record in the slot: the entry is skipped */
        if ((int8_t)s->hostile_cap <= (int8_t)s->hostile_count) continue;   /* the cap is tested for every entry whatever its class (note 07 2.1 step 4) */
        const int counted = ((e->w[1] >> 4) & 15u) == 1 && !(flags & 0x8000u);   /* aircraft class, "not counted" bit clear */
        screen_placed_t *p = &s->placed[s->placed_count++];
        p->template_id = (uint16_t)(e->w[0] / 20u);
        p->cls = (uint8_t)(e->w[1] & 0xFFu);
        p->slot = (uint8_t)slot;
        p->pos_ref = e->w[3];
        p->flags = (uint16_t)flags;
        p->counted = (uint8_t)counted;
        for (int i = 0; i < 5; i++) p->w[i] = e->w[i];
        if (slot < 32) taken |= 1u << slot;
        if (counted) s->hostile_count++;
    }
    return s->placed_count;
}

/* The mode dispatch 0x1201E (6.1): runs when the echo countdown is out. */
/* The mode dispatch 0x1201E (note 08 6.1, note 10 6.2, 7.2): the typing-sound byte 0, voices 0 and 1 stopped, the master level at 0x3F0000 at once, then the mission tone pair (modes 3 to 8) or the wind (volume 8; the external
 * engine pair at period 0x300 without the wind sound). */
static void snd_dispatch(screen_t *s) {
    sound_t *q = s->snd;
    if (!q) return;
    sound_master(q, -1, 63, 0);
    sound_stop_voice(q, 0); sound_stop_voice(q, 1);
    if (s->mode_byte >= 3 && s->mode_byte <= 8) sound_event(q, SND_PHRASE, 1, 0x28);
    else if (q->en13 & 0x10) sound_event(q, SND_WIND, 8, 0);
    else sound_event(q, SND_ENGINE_EXT, 0x300, 8);
}
static void dispatch(screen_t *s) {
    snd_dispatch(s);
    span(s);   /* 0x120E0 is a store of the dispatch state 0x1201E: V:4A8EE = 3 (07 12.1); gate-1 passes in external views follow it before the flight (cockpit.c) */
    if (s->mode_byte == 1) {   /* free flight: the allowance and the placement, the palette is cleared to black, then the scene build and the fade-in */
        screen_start_flight(s);
        eng_clear(s);
        show(s, GAME_MODE_SELECT_LOCATION, 1);
        enter(s, SCR_BLACK);
        return;
    }
    /* The demo (0x7F), the trainings (2, 0x7D), the qualification (9) and the missions (3 to 8) are the later tasks' (6, 7, 8, milestone 5): their handler takes the
     * screen over and calls screen_start_flight itself. The game installs a handler for every mode; a bare screen module (C, the unit tests) returns to the menu as the reset of SHIFT-ESC does. */
    const int taken = s->mode_handler ? s->mode_handler(s->mode_user, s->mode_byte) : 0;
    if (taken == 3) {   /* the missions F1 to F6 (note 08 13.1): the allowance and the placement are done at the dispatch (6.1), the briefing chain follows; the player is placed at its end */
        screen_start_flight(s);
        eng_clear(s);
        s->mission = 1; s->brief_target = -1; s->cam_mode = 0xFF;
        show(s, GAME_MODE_SELECT_LOCATION, 1);   /* the map overview behind the briefing text (the camera never leaves it in a mission briefing: note 08 13.4, C1 settled) */
        enter(s, SCR_MISSION);
        s->ms_stage = MS_BLACK; s->ms_t = 0;
        return;
    }
    if (taken == 2) {   /* the briefing flow (the trainings): the flight is placed, the briefing types over it, the three-tick wait follows (6.2) */
        screen_start_flight(s);
        s->out_place = 1;
        s->view_kind = s->mode_byte == 2 ? SV_TRAIN1 : SV_TRAIN2;   /* 06 start table, 12.14: the trainings are seen from outside from the dispatch */
        s->sim_hold = 1;
        show(s, GAME_MODE_FLIGHT, 1);
        eng_clear(s);
        s->train_restart = s->echo_restart;   /* an ESC restart (V:4A8E3 set): the briefing waits for the restart's own stages, counted from the ESC */
        enter(s, SCR_TRAIN_DELAY);   /* 8.1: the briefing is typed over the start scene (the gate is 0: one picture), after 24 or 67 blanks */
        return;
    }
    if (taken) {
        eng_clear(s);
        if (s->state == SCR_ECHO) enter(s, SCR_EXTERNAL);   /* a handler that took the screen to a state of its own (screen_demo_begin) keeps it */
        return;
    }
    menu_enter(s);
}

static void location_enter(screen_t *s) {
    eng_clear(s);
    show(s, GAME_MODE_SELECT_LOCATION, 1);
    s90f(s, 0); s90f(s, 1);   /* 0x12AC0 sees the camera at mode 4 (0x12AE2: 0), 0x12AFC stores the camera mode 5 and V:4A90F = 1 (0x12B52) */
    enter(s, SCR_LOCATION);
    eng_list1_timed(s, ID_LOCATION, BLANKS_LOCATION);
}

/* Estimated fade level V:4A958 (the core owns the target, the frontend the level: this is only for the time an ESC takes). */
static int est_level(const screen_t *s) {
    const int blanks = s->t * 50 / FLIGHT_STEP_HZ;
    switch (s->state) {
    case SCR_ESC_FADE: { const int b = blanks - (s->esc_pass ? s->esc_pass - 1 : 0) * 50 / FLIGHT_STEP_HZ; int l = (int)s->esc_l0 - (b > 3 ? (b - 3) / 2 : 0); return l < 0 ? 0 : l; }
    case SCR_FADE_IN: { int l = blanks / 3; return l > 15 ? 15 : l; }
    case SCR_ECHO: return s->echo_restart ? 0 : 15;   /* the first echo screen comes straight from the menu: an ESC fades out from level 15 (5.4) */
    case SCR_APPROACH:
        return s->paused ? 10 : 15;
    case SCR_BLACK: case SCR_MENU: case SCR_MENU_BUILD: case SCR_LIST: case SCR_DEMO_WAIT: case SCR_ESC_DETOUR: return 0;
    case SCR_MISSION:
        if (s->ms_stage == MS_BLACK) return 0;
        if (s->ms_stage == MS_FADE) { const int l = ((s->t - s->ms_t) * 50 / FLIGHT_STEP_HZ) / 3; return l > 15 ? 15 : l; }
        return 15;
    default: return s->paused ? 10 : 15;
    }
}

/* ESC (request 1, 5.11): the restart of the selection. The flight stays on screen while the level falls (28 blanks from 15, 20 from 10, one per 2 blanks, the first
 * step after 3), the gate pulse follows (8 blanks), then the reset. A second ESC restarts it from the level reached (Review Focus 5). */
static void esc_begin(screen_t *s) {
    s->request = 0;                 /* the dispatcher takes the request (V:4A8E2 = 1 or 0xFF) and clears it in the same pass */
    s->restart_clock = 0;   /* the steps since the ESC (8.1: the training briefing after a restart is timed from it) */
    int l = est_level(s);
    s->esc_pass = 0;
    /* 7.7, "The fade level at an ESC in the fade-in, settled" and "Two more levels and a correction" (followup-demo-esc, followup-combat-batch-g part demo-esc; 6 runs): T is the blanks since the state 0x11B9C
     * first ran. The ESC is acted on at the next pass boundary strictly after it, the boundaries being T_b = 12, 21, 30 and 34 (the passes of the fade-in; the one at 3 does not exist: ESCs at 1 and 6 were
     * consumed at 12; after 30 the next came at about 34, not 39). The fade-in level is floor(T_b / 3): 4, 7, 10, 11. The fade-out starts from it (the target is set to 0 at the consuming pass, the fade-in
     * goes on until then) and the reset (pause byte 0) comes 21, 24, 29 and 32 blanks after the consuming pass for those four levels: the measured table. Levels 1 and 13 are not reachable (no pass
     * boundary falls there). After the boundary at 34 and before the fade-in ends (08 7.7, "ESC after the 34-blank boundary ... settled", T = 36, 39, 42 measured) the passes come every blank: the ESC is
     * consumed in the next blank at the level floor(T / 3) (12, 13, 14) and the reset comes 2 L + 9 blanks after the consuming pass (33, 35, 37); the generic chain starts at level 15 only (T of 45 or more). */
    static const struct { int tb, level, after; } DEMO_ESC[4] = { { 12, 4, 21 }, { 21, 7, 24 }, { 30, 10, 29 }, { 34, 11, 32 } };
    if (s->state == SCR_APPROACH && s->demo && s->direct_restart && s->t < STEPS_DEMO_FADE) {
        const int t_esc = s->t * 50 / FLIGHT_STEP_HZ;
        int row = 0;
        while (row < 4 && DEMO_ESC[row].tb <= t_esc) row++;
        if (row < 4 || t_esc / 3 < 15) {
            const int tb = row < 4 ? DEMO_ESC[row].tb : t_esc + 1;   /* past the boundary 34: consumed in the next blank (08 7.7) */
            l = row < 4 ? DEMO_ESC[row].level : t_esc / 3;
            const int after = row < 4 ? DEMO_ESC[row].after : 2 * l + 9;
            s->esc_pass = 1 + BLANKS(tb - t_esc);   /* t counts from the ESC step (t = 1) */
            s->esc_l0 = (uint8_t)l;
            s->esc_r = 1 + BLANKS(tb - t_esc + after);
            s->sim_hold = 1;
            s->out_esc = 1;
            enter(s, SCR_ESC_FADE);
            return;
        }
    }
    /* 5.11 stages (3.8 contract): key to request 0, the fade-out 2 L + 1 blanks (32 from 15: 5 steps; 21 from a pause at 10: 3), the close and pulse 8 blanks = 1: the reset
     * at 6 steps (from a pause 4). At level 0 (a second ESC in the echo or the black passes) the chain still takes 21 blanks to the reset (3 steps). The fade-out
     * is 28 to 32 blanks in the captures (note 08 5.11 gives 28; the spread is carried in the follow-ups). */
    s->esc_l0 = (uint8_t)l;
    s->esc_r = l == 0 ? BLANKS(21) : BLANKS(2 * l + 1) + 1;
    s->sim_hold = 1;                /* V:4A866 = 1: the world freezes at once */
    s->out_esc = 1;                 /* 5.11: the request sets the pause byte V:4A866 to 1 (game.c writes it into the player's state; 12.11) */
    enter(s, SCR_ESC_FADE);
}

static int esc_ok(const screen_t *s) {
    if (s->mode_byte == 0) return 0;
    switch (s->state) {
    case SCR_ECHO: case SCR_BLACK: case SCR_FADE_IN: case SCR_LOCATION: case SCR_LOC_PICKED: case SCR_AIRCRAFT: case SCR_AIR_PICKED:
    case SCR_APPROACH: case SCR_NOTICE: case SCR_WAIT: case SCR_FLIGHT: case SCR_ESC_FADE: case SCR_GAMEOVER: case SCR_BRIEF: case SCR_TRAIN_DELAY: case SCR_TRAIN_BRIEF:
    case SCR_END_WAIT: case SCR_END_TEXT: case SCR_END_HOLD: case SCR_MAP: case SCR_QUAL_VERDICT: case SCR_EXTERNAL: case SCR_DEMO_WAIT: case SCR_ESC_DETOUR: case SCR_MISSION:
        return 1;
    default:
        return 0;
    }
}

/* ESC and SHIFT-ESC in the menu are consumed without effect (5.4); in the credits any key leaves (5.2). */
static void to_menu_from_flow(screen_t *s) {
    const int air = s->mode == GAME_MODE_FLIGHT || s->mode == GAME_MODE_GAMEOVER || s->sim_hold;
    if (air) leave_flight(s);
    s->shift_defer = (uint8_t)air;
    s->shift_menu = (uint8_t)air;   /* with or without a sound module: the text and its clicks are one thing */
    menu_enter(s);
}

/* SHIFT-ESC from the air (note 10 7.2, 5.7 item 13, note 08 5.11 [V: sound7-restart, the breakpoint runs at the click routine]): there is NO click before the stop (the eleven "starts" of the first frames of the trace are the sound
 * interrupt rewriting the same period and volume into the voices 0 and 1). The stop of the four voices and the music restart are the first act of the menu set-up pass, 11 blanks after the key (k = 11, counted from the start of
 * the step that follows the key); the list is posted at k = 44 (the end of the 33-blank pass: the menu build's 7 steps), its 19 instant lines click once a blank from k = 46 to 65 and the typed characters follow from k = 70. */
static void shift_sound(screen_t *s) {
    if (!s->shift_defer) return;
    if (!s->snd) { s->shift_defer = 0; return; }
    const int nb = sound_step_blanks(s->snd);
    for (int b = 0; b < nb; b++) {
        if (s->shift_blank + b == 11) {
            sound_run_to(s->snd, b);
            snd_menu_build(s);
            s->shift_defer = 0;
            return;
        }
    }
    s->shift_blank += nb;
}

/* The passes of the approach state 0x12D58 (one per step, 12.14 N1 and C13 [V + rig]): free flight by the start, 118 for location key 1 (airfield 1, V:4A900 = 0), 93 for
 * the keys 2 and 3 (airfields 2 and 3), 104 for the key 4 (the carrier); the missions F1 to F6 have their own table (121, 112, 110, 99, 99, 99). The count depends on the entry camera and the start position
 * (the mover flies from the selection camera to the stage 0 target at the start).
 * The keys 5 and 6 [V, 08 12.14 C13, `followup-combat-batch-g.txt` part keys56]: both start at airfield 1 (the start routine clears the out-of-table index, 0x1D730, so V:4A900 reads 0 from the approach on)
 * and run 118 passes like key 1 (the step counter 35 to 153 and 34 to 152). The keys 7, 8, 9 and 0 (out-of-table positions, V:4A900 = 6 to 9) run
 * 227, 223, 200 and 192 passes [V: flow7-keys7890, +-1 pass; the entry counter 34/33/33/33]. */
/* The approach state's end and the sound routine (note 10 5.7 item 10; note 08 6.2, 6.4). The routine is gated by V:4A966 while the approach camera runs (silent, its notice 0xFE pending); from the state 0x12D58 that has
 * seen the cockpit, and through the six passes of 0x12DB8's countdown, the gate is open (V:4A84D = 1) and the routine runs: `tail` of the last visible steps of the state. After a natural end the first of them
 * still has the approach's outside view (`ext`); the typed notice that follows has the gate closed. */
static void set_notice_at(screen_t *s, int at, int tail, int ext) { s->notice_at = at; s->snd_tail = (uint8_t)tail; s->snd_ext = (uint8_t)ext; }

int screen_sound_pass(const screen_t *s) {
    if (!s || s->state != SCR_APPROACH || s->paused || !s->snd_tail) return 0;
    const int j = s->t - (s->notice_at - s->snd_tail);   /* the step's number in the tail (the first visible step after screen_step has t = 1) */
    if (j < 1 || j > s->snd_tail) return 0;
    if (!s->snd_ext) return 3;
    return j == 1 ? 1 : j == 2 ? 2 : 3;
}

static int approach_passes(const screen_t *s) {
    static const uint8_t MS[6] = { 121, 112, 110, 99, 99, 99 };   /* the screen alone (rigs, screen tests): one run of the original each, with the placements of those runs; a game with a flight replaces the count by its mover's (screen_set_approach_len, 08 12.14 C14) */
    if (s->mission && s->mode_byte >= 3 && s->mode_byte <= 8) return MS[s->mode_byte - 3];
    switch (s->location) { case 1: case 2: return 93; case 3: return 104; case 6: return 227; case 7: return 223; case 8: return 200; case 9: return 192; default: return 118; }   /* location 0 is airfield 1 and the keys 5 and 6 (cleared byte): 118; the keys 7, 8, 9, 0 keep V:4A900 = 6 to 9 (08 5.6, 12.14 C13 [V: flow7-keys7890]) */
}

static void place_start(screen_t *s) {
    s->out_place = 1;
    s->sim_hold = 1;
    if (s->mode_byte >= 3 && s->mode_byte <= 8 && s->mission && !s->demo) s->view_kind = SV_MISSION;   /* 08 12.14 C14 [V: sound7-approach]: the missions' approach is seen from outside too (the view byte 1 in passes 0 to 125) */
    if (s->mode_byte == 1 && !s->demo) s->view_kind = SV_FREE;   /* 12.14 C13: the free-flight approach is seen from the mover's camera */
    show(s, GAME_MODE_FLIGHT, 1);
    eng_clear(s);
    set_notice_at(s, approach_passes(s) + STEPS_APPROACH_TO_NOTICE, STEPS_APPROACH_TO_NOTICE, 1);
    s->approach_len = approach_passes(s);
    if (s->mode_byte >= 3 && s->mode_byte <= 8) s->out_wind = 1;   /* 0x12C6E/0x12C90: the state that starts the approach plays the wind or the engine call (note 10 7.2, 5.7 item 10); the demo, the KP-ENTER skip of the orders and the other modes do not pass it */
    enter(s, SCR_APPROACH);
}

/* KP-ENTER during the orders of a mission briefing (note 08 6.4 "KP-ENTER skips the approach" and C11 [V: followup-brief-skip]): the key is accepted (V:4A965 = 2 from the countercode answer through the
 * orders), the cockpit is seen and the start is made at once with the defaults (airfield 1 and the F/A-18 for F1, the mission's own start otherwise); the approach is skipped entirely and the state
 * 0x12DB8 follows within a step, with its 6-pass countdown. */
static void place_start_skipping_the_approach(screen_t *s) {
    if (s->mode_byte == 3) { s->location = 0; s->aircraft = 0x11; }
    s->cam_mode = 0;   /* 08 C11 (13.x, followup-brief-skip): the approach state never runs after the skip, so the camera mode is 0, not 6 (V:4A966 selects the key set and the mark pass, 6.4) */
    s->out_place = 1;
    s->sim_hold = 1;
    show(s, GAME_MODE_FLIGHT, 1);
    eng_clear(s);
    set_notice_at(s, STEPS_DB8, STEPS_DB8, 0);
    enter(s, SCR_APPROACH);
}


/* ---------------------------------------------------------------------------------------------------------------------------------------------
 * Task 8 (note 08 sections 9 and 10): the flight-log screen, the welcome and the callsign entry, the qualification's start and the log hooks.
 * ------------------------------------------------------------------------------------------------------------------------------------------ */

static void logscreen_enter(screen_t *s) {   /* menu key 8, the pseudo-mode 0xFE (10.6): the figures are patched into the lines as they begin */
    eng_clear(s);
    s->mode_byte = 0;
    show(s, GAME_MODE_LOG, 0);
    enter(s, SCR_LOGSCREEN);
}

/* 0x13622 to 0x13888 (10.4): tours 0 is the first-tour welcome (ids 1 and 3, the callsign entry opens when they are played, tours becomes 1 at once), any larger
 * value the continuing-tour welcome (id 2 with the callsign patched in, tours plus one, not saved by the welcome itself). */
static void welcome_enter(screen_t *s, int from_credits) {
    s->welcome_text_at = from_credits ? STEPS_WELCOME_FROM_CREDITS : STEPS_WELCOME_TEXT;
    s->welcome_credits = (uint8_t)from_credits;
    if (!from_credits) eng_clear(s);   /* from the credits the credits text stays until their own wait is over */
    s->mode_byte = 0;
    s->name_open = 0;
    s->welcome_first = logbook_get16(s->log, LB_TOURS) == 0;
    if (!s->welcome_first) logbook_set16(s->log, LB_TOURS, logbook_get16(s->log, LB_TOURS) + 1);
    if (!from_credits) show(s, GAME_MODE_WELCOME, 0);
    enter(s, SCR_WELCOME);
}

static void name_done(screen_t *s) {   /* RETURN, or the automatic end (deviation 2 of 10.5): the log is saved (10.8), the menu follows */
    logbook_save_point(s->log);
    s->name_open = 0;
    menu_enter_fresh(s);
}

/* One key of the callsign entry (10.5): RETURN ends it, letters, digits, SPACE and BACKSPACE go to the table; every other key, ESC included, does nothing. */
static int welcome_key(screen_t *s, screen_keyev_t k) {
    if (!s->welcome_first) { menu_enter_fresh(s); return 1; }   /* 10.4 [V]: the first key during the continuing welcome ends it at once and is consumed by it: it never reaches the menu as a choice */
    if (!s->name_open) return 0;
    if (k.type == SK_RETURN) { name_done(s); return 1; }
    if (k.ch == 8 || k.ch >= ' ') {
        if (logbook_name_key(&s->name, s->log, k.ch) == LB_NAME_END) { name_done(s); return 1; }
    }
    return 0;
}

/* UPDATE (10.6, key 1): the log is written when the dirty flag is set; the tour count at most 1 (after a RESET) runs the welcome again at once (10.4). */
static int log_update(screen_t *s) {
    if (!s->log->dirty) return 0;
    logbook_save_point(s->log);
    if (s->log_welcome && logbook_get16(s->log, LB_TOURS) <= 1) { welcome_enter(s, 0); return 1; }
    return 0;
}
/* RESET (10.6, SHIFT 2): the record becomes zero, dirty, no write; the screen is cleared and the page posted again. */
static void log_reset(screen_t *s) {
    logbook_reset(s->log);
    eng_clear(s);
    eng_list1(s, ID_LOG_PAGE);
}

/* The debrief builder 0x131F4 and the completion branch (9.3, 10.8): the qualification sets the first word and saves; a mission completion (code 0xFC) runs the
 * bookkeeping 0x134A0 and saves. */
static void log_outcome(screen_t *s) {
    if (!s->log) return;
    if (s->end_code == 0xFF && s->end_mode == 9) { logbook_set16(s->log, LB_QUALIFIED, 1); logbook_save_point(s->log); }
    else if (s->end_code == 0xFC) { logbook_mission_complete(s->log, s->end_mode); logbook_save_point(s->log); }
}

/* The qualification (9.2): after the briefing the states 0x12D58 and 0x12DB8 run (7 steps, STEPS_QUAL_TO_NOTICE), then the launch notice. */
static void approach_to_notice(screen_t *s) {
    eng_clear(s);
    set_notice_at(s, STEPS_QUAL_TO_NOTICE, STEPS_QUAL_TO_NOTICE, 0);
    enter(s, SCR_APPROACH);
}
static void brief_enter(screen_t *s) {
    eng_clear(s);
    enter(s, SCR_BRIEF);
    eng_list1(s, ID_BRIEFING_QUAL);
}
static void qual_after_fade(screen_t *s) {   /* the briefing only when V:4A8E3 is 0, that is not after an ESC restart (9.2 step 3); 9.2 (milestone 7): the first start posts it 3 steps after the fade-in, the restart runs its map stage of 6 and the verdict state's 3 first */
    enter(s, s->echo_restart ? SCR_MAP : SCR_QUAL_VERDICT);
}

int screen_qualification_start(screen_t *s) {
    s->location = 3; s->aircraft = 0x11;                          /* 9.2 step 2: the carrier start and the F/A-18, the selections are skipped */
    screen_start_flight(s);                                       /* the allowance 3 and the placement */
    eng_clear(s);
    s->out_place = 1;                                             /* the player is placed on the catapult and the deck picture is built (frozen) */
    s->sim_hold = 1;
    show(s, GAME_MODE_FLIGHT, 1);
    enter(s, SCR_BLACK);                                          /* then the black stage of 6 steps, the fade-in of 7 and the verdict state's 3 (9.2, milestone 7 [V: flow7-qual-dispatch]) */
    return 1;
}

/* A function key in the menu (5.4): F1 to F6 start the missions 3 to 8 when the log allows it. */
static int fkey_key(screen_t *s, int n) {
    if (!s->log) return 0;
    const int m = logbook_fkey_mode(s->log, n);
    if (!m) return 0;                                             /* the click of a refused key is note 10's */
    s->mode_byte = (uint8_t)m;
    s->echo_fkey = 1;                                             /* the echo of a function-key mission shows no line */
    echo_enter(s, 0);
    return 1;
}

/* A digit or a keypad digit 1 to 8 in the menu (5.4). */
static int menu_key(screen_t *s, int d) {
    s->echo_fkey = 0;
    switch (d) {
    case 1:
        if (!s->demo_available) return 0;
        s->mode_byte = 0x7F; break;
    case 2: s->mode_byte = 1; break;
    case 3: s->mode_byte = 2; break;
    case 4: s->mode_byte = 0x7D; break;
    case 5: s->mode_byte = 9; break;
    case 6: {   /* the list of selectable missions: the empty log shows the header and the hint only */
        eng_clear(s);
        s->mode_byte = 0;
        show(s, GAME_MODE_MENU, 0);
        enter(s, SCR_LIST);
        {   /* L+5 to L+9 blanks: 1 step, the list starts in the key's own step; 5.4: one line for each mission F2 to F6 the record marks as available (record + 0x11 + mission) */
            uint16_t ids[SCREEN_LIST_IDS] = { ID_LIST_HEADER };
            int n = 1;
            if (s->log)
                for (int m = 4; m <= 8; m++) if (s->log->rec[0x11 + m]) ids[n++] = (uint16_t)(ID_MISSION_F2 + m - 4);
            ids[n++] = ID_LIST_HINT;
            eng_list(s, ids, n, BUDGET_STEP);
        }
        return 1;
    }
    case 7: {   /* the next mission; needs the first word of the log (5.4); a refused key only clicks */
        if (!s->log) return 0;
        const int m = logbook_item7_mode(s->log);
        if (!m) return 0;
        s->mode_byte = (uint8_t)m;
        break;
    }
    case 8:     /* the flight-log statistics (10.6) */
        if (!s->log) return 0;
        logscreen_enter(s);
        return 1;
    default: return 0;   /* 9 and 0 do nothing */
    }
    echo_enter(s, 0);
    return 1;
}


/* ---------------------------------------------------------------------------------------------------------------------------------------------
 * The briefing chain of the missions F1 to F6 (note 08 13.1, 13.4, 13.5): the intro block and the briefing, the approach target, the orders, the tail to the player start. The countercode
 * screens are skipped (5.5). `t` is the state's clock; ms_t the clock value at which the stage began (a stage begun in the timers or by a key is dt = 1 in the next step, one begun in
 * post() the same).
 * ------------------------------------------------------------------------------------------------------------------------------------------ */

static void ms_goto(screen_t *s, int stage, int from_post) { s->ms_stage = (uint8_t)stage; s->ms_t = s->t - (from_post ? 1 : 0); }

static void ms_intro_text(screen_t *s) {
    const unsigned comp = s->log ? s->log->rec[LB_COMPLETIONS + s->mode_byte] : 0;   /* 13.1: briefing 0 when the completion byte is zero, else 1 */
    const uint16_t ids[2] = { ID_MS_INTRO, (uint16_t)missions_briefing_id(s->chain, s->mode_byte, comp) };
    eng_list_mission(s, ids, 2);
}

/* 0x12744: the approach target of 13.4 and the camera mode 2; the stage 7 follows. */
static void ms_to_stage7(screen_t *s, int from_post) {
    uint8_t sl[SCREEN_MAX_PLACED], cl[SCREEN_MAX_PLACED], cn[SCREEN_MAX_PLACED];
    for (int i = 0; i < s->placed_count; i++) { sl[i] = s->placed[i].slot; cl[i] = s->placed[i].cls; cn[i] = s->placed[i].counted; }
    s->brief_target = (int8_t)missions_approach_target(sl, cl, cn, s->placed_count);
    s->cam_mode = 2;
    eng_clear(s);
    s90f(s, 1);   /* 0x12744 on its countdown: V:4A90F = 1 (0x12756) */
    ms_goto(s, MS_STAGE7, from_post);
}

/* 0x128DA: the verdict state: the missions-started counter, the clearance line and the orders. The camera is at mode 3 (arrived at the hostile) and flies back to the map overview (stage 8): mode 4 comes only with the arrival (13.4, C1). */
static void ms_orders(screen_t *s) {
    if (s->log) logbook_mission_started(s->log, s->mode_byte);   /* +0x36: every pass of the mission start that reaches the state, ESC restarts included (14.2) */
    eng_clear(s);
    const uint16_t ids[2] = { ID_MS_ORDERS_HEAD, (uint16_t)missions_list_id(s->chain, s->mode_byte, 3) };
    eng_list_mission(s, ids, 2);
    ms_goto(s, MS_ORDERS, 0);
}

static int ms_key(screen_t *s, screen_keyev_t k) {
    if (k.type == SK_PAUSE) return 0;
    if (k.type == SK_KPENTER && (s->ms_stage == MS_STAGE7 || s->ms_stage == MS_RESTART || s->ms_stage == MS_ORDERS || (s->ms_stage == MS_TAIL && s->t - s->ms_t < STEPS_MS_TAIL))) {
        place_start_skipping_the_approach(s);   /* C11: KP-ENTER during the orders skips the approach too */
        return 1;
    }
    /* 0x125A8 and 0x12612: a key skips the intro and the briefing when V:4A8E3 is 0 (a first start); 0x126F6 (the text) takes a key whatever V:4A8E3 is (13.1) */
    if (s->ms_stage == MS_INTRO) span(s);
    if ((s->ms_stage == MS_WAIT && !s->echo_restart) || s->ms_stage == MS_INTRO) { ms_to_stage7(s, 0); return 1; }
    return 0;
}

static void mission_timers(screen_t *s) {
    static const uint16_t FADE_DONE[6] = { 180, 170, 180, 170, 170, 170 };   /* 13.5 segment A: 0x125A8 in blanks from the F key */
    const int t = s->t, dt = t - s->ms_t, m = s->mode_byte;
    if (m < 3 || m > 8) return;
    switch (s->ms_stage) {
    case MS_BLACK:
        if (t == STEPS_MS_FADE_START) ms_goto(s, MS_FADE, 0);   /* 0x12568: the fade-in from black */
        break;
    case MS_FADE:
        if (t == BLANKS(FADE_DONE[m - 3]) - STEPS_ECHO_TOTAL) { s90f(s, 1); ms_goto(s, MS_WAIT, 0); }   /* 0x12568: the fade reached its target, V:4A90F = 1 (0x1258E); then 0x125A8's pause of 9 passes and 0x12612 */
        break;
    case MS_WAIT:
        if (t == STEPS_MS_INTRO_STATE) s90f(s, 0);   /* 0x125A8 ran its countdown out: V:4A90F = 0 (0x125F8), then 0x12612 */
        if (t == STEPS_MS_INTRO_STATE + STEPS_MS_INTRO_TEXT) { eng_clear(s); ms_intro_text(s); ms_goto(s, MS_INTRO, 0); }
        break;
    case MS_STAGE7:
        if (dt >= 1) {   /* the draw of the pass after the target choice sets V:4A967 when the target is inside the window (13.4): the observed two passes without a camera */
            const int vis = s->brief_target < 0 || !s->visible_fn || s->visible_fn(s->ms_user, s->brief_target);
            if (vis && (s->brief_target < 0 || dt >= STEPS_MS_STAGE7)) {
                s->cam_mode = 3;
                s90f(s, 0);   /* 0x1279C: the camera mode is 3: V:4A90F = 0 (0x127AE) */
                if (s->echo_restart) { eng_clear(s); ms_goto(s, MS_RESTART, 0); }   /* 5.5: the accepted code is not asked again, but 0x1279C and 0x127C8 still run */
                else ms_orders(s);
            }
        }
        break;
    case MS_RESTART:
        if (dt == STEPS_MS_RESTART) ms_orders(s);
        break;
    case MS_TAIL:
        if (dt == STEPS_MS_TAIL - 1) { s->cam_mode = 4; s90f(s, 0); }   /* 0x12AC0 (0x12AE2): V:4A90F = 0; 0x12AC0 sees the camera at mode 4 (stage 8 has brought it back to the map, 13.4), 5 steps after the orders text ends */
        if (m == 3) {
            if (dt == STEPS_MS_TAIL + STEPS_MS_PROMPT) {   /* 0x12C02: the airfield start asks for the aircraft (5.7); the carrier starts go straight to the player start */
                eng_clear(s);
                s->cam_mode = 6;
                show(s, GAME_MODE_SELECT_AIRCRAFT, 1);
                enter(s, SCR_AIRCRAFT);
                eng_list1_timed(s, ID_AIRCRAFT, BLANKS_MS_PROMPT);
            }
        } else if (dt == STEPS_MS_TAIL + STEPS_MS_CARRIER_START) {
            s->cam_mode = 6;
            place_start(s);
        }
        break;
    default:
        break;
    }
}

void screen_set_mission_hooks(screen_t *s, int (*bearing)(void *user), int (*visible)(void *user, int record), void *user) {
    if (!s) return;
    s->bearing_fn = bearing; s->visible_fn = visible; s->ms_user = user;
}

static int location_key(screen_t *s, int d) {
    /* 5.6: all ten digits are accepted. V:4A900 = key - 1 (key 0: 9); keys 5 and 6 clear it (airfield 1); keys 7, 8, 9 and 0 keep 6, 7, 8 and 9 and start at the out-of-table
     * positions of the note (game.c select_start_block). */
    const int idx = d == 0 ? 9 : d - 1;
    s->location = (uint8_t)(idx == 4 || idx == 5 ? 0 : idx);
    /* V:4A900 as the key stores it (08 5.6, 08 12.14 settled: the handler stores key - 1 at once, before its own tests, so keys 5 and 6 store 4 and 5; only the player start 0x1D730 clears the byte, so the approach
     * reads 0). The mark pass of the aircraft screen draws only the entry whose index is V:4A900 (04 "Camera mode 6"): the entries 4 and 5 are the inactive carrier record 15 and the terminator, so no number
     * is drawn. */
    s->loc_key = (uint8_t)idx;
    eng_clear(s);
    if (s->location == 3) { s->aircraft = 0x11; show(s, GAME_MODE_SELECT_LOCATION, 1); }
    else show(s, GAME_MODE_SELECT_AIRCRAFT, 1);
    span(s);   /* 0x12BA0 stores V:4A8EE = 3 only; V:4A8EF keeps its boot value 0xFF, which the panel block passes do not count down (negative): free flight reaches its first pass with 0 and 0xFF (07 12.1 [V: followup-combat-first-pass.txt]) */
    if (s->location != 3) s90f(s, 0);   /* 0x12BD2: the runway and airfield starts (camera mode 6); the carrier start goes straight on */
    enter(s, SCR_LOC_PICKED);
    return 1;
}

static int aircraft_key(screen_t *s, int d) {
    if (d != 1 && d != 2) return 0;
    s->aircraft = (uint8_t)(d == 1 ? 0x11 : 0x10);
    s90f(s, 1);   /* 0x12C56 */
    eng_clear(s);
    show(s, GAME_MODE_SELECT_AIRCRAFT, 1);
    enter(s, SCR_AIR_PICKED);
    return 1;
}

static int ms_key(screen_t *s, screen_keyev_t k);

/* Delivers one key to the state. Returns 1 when the state changed (the remaining queue waits for the next step). */
static int handle_key(screen_t *s, screen_keyev_t k) {
    if (k.type == SK_SHIFT_ESC) {
        if (s->state == SCR_CREDITS) { /* any key */ }
        else if (esc_ok(s)) { to_menu_from_flow(s); return 1; }
        else return 0;
    }
    if (k.type == SK_ESC && s->state != SCR_CREDITS) {
        if (s->state == SCR_END_WAIT || s->state == SCR_END_TEXT || s->state == SCR_END_HOLD) {   /* 5.13: with an end code set ESC does not restart; the final-screen state takes it: the menu after the hold and the 100 passes; 0xEF skips its countdown */
            if (s->state == SCR_END_HOLD && s->end_after == 0xEF) { esc_begin(s); return 1; }
            if (s->end_after == 0xEF) s->end_skip = 1;
            else { s->end_after = 0xF0; s->end_menu = 1; }
            return 0;
        }
        if (esc_ok(s)) { esc_begin(s); return 1; }
        if (s->state == SCR_LIST || s->state == SCR_LOGSCREEN) { menu_enter_fresh(s); return 1; }   /* a sub-screen of items 6 and 8: ESC rebuilds the menu (5.4) */
        return 0;
    }
    switch (s->state) {
    case SCR_CREDITS:   /* 5.2: every key goes on (the frontend filters SHIFT, ALT and the Amiga keys); a pause button is no key */
        if (k.type == SK_PAUSE) return 0;
        if (s->snd) sound_master(s->snd, 31, -1, 1);   /* 0x135D0: the any-key sets the target 0x1F0000 and the typing-sound byte 1 (note 10 6.2) */
        if (s->logdisk_result == 0) { if (s->log && s->log_welcome) welcome_enter(s, 1); else menu_enter_fresh(s); return 1; }   /* 5.3 ruling: a usable disk shows no prompt; the welcome of 10.4 follows it when a log was loaded */
        eng_clear(s);
        show(s, GAME_MODE_LOGDISK, 0);
        enter(s, SCR_LOGDISK);
        return 1;
    case SCR_LIST:   /* the list is still the menu state 0x11E04: the digits act here (5.4) */
        return k.type == SK_DIGIT ? menu_key(s, k.val) : 0;
    case SCR_LOC_PICKED:   /* V:4A849 = 1 from the location key: an aircraft digit typed before the caption is taken (5.7, 6.4) */
        if (s->location != 3 && k.type == SK_DIGIT) return aircraft_key(s, k.val);
        return 0;
    case SCR_LOGDISK:
        if (k.type == SK_RETURN) {
            if (s->logdisk_result != 0) { s->log = NULL; s->log_welcome = 0; menu_enter_fresh(s); return 1; }   /* 10.3: the disk state is still not 0 at the second check: the menu without a log (no welcome, no callsign, no saving; the keys 7, 8 and F1 to F6 have no record) */
            if (s->log && s->log_welcome) welcome_enter(s, 0); else menu_enter_fresh(s);
            return 1;
        }
        return 0;
    case SCR_MENU:
        if (k.type == SK_FKEY) return fkey_key(s, k.val);
        return k.type == SK_DIGIT ? menu_key(s, k.val) : 0;
    case SCR_WELCOME:
        return welcome_key(s, k);
    case SCR_LOGSCREEN:
        if (!s->log) return 0;
        if (k.type == SK_DIGIT && k.val == 1) return log_update(s);
        if (k.type == SK_OTHER && k.ch == GAME_KEY_SHIFT2) log_reset(s);
        return 0;
    case SCR_BRIEF:      /* 9.2 step 3: the briefing waits for its text to finish or for any key (the latch V:4A85B) */
        approach_to_notice(s);
        return 1;
    case SCR_LOCATION:
        if (k.type == SK_PAUSE) { s->map_frozen ^= 1; return 0; }   /* P on a selection screen freezes the map mover: no pause screen, no fade (6.4) */
        return k.type == SK_DIGIT ? location_key(s, k.val) : 0;
    case SCR_AIRCRAFT:
        if (k.type == SK_PAUSE) { s->map_frozen ^= 1; return 0; }
        return k.type == SK_DIGIT ? aircraft_key(s, k.val) : 0;
    case SCR_MISSION:
        return ms_key(s, k);
    case SCR_APPROACH:   /* camera mode 6: KP-ENTER skips the approach at any time (6.4); the digits and the other keys do nothing (the aircraft menu flag is clear) */
        if (k.type == SK_KPENTER && !s->demo && s->t + STEPS_SKIP_TO_NOTICE - 1 < s->notice_at) { set_notice_at(s, s->t + STEPS_SKIP_TO_NOTICE - 1, STEPS_SKIP_TO_NOTICE - 1, 0); s->approach_len = s->t; return 0; }   /* the key's own step is the first of the 8 */
        return 0;
    default:
        return 0;
    }
}

static int poll_ok(const screen_t *s) {
    switch (s->state) {
    case SCR_CREDITS: return s->t >= STEPS_CREDITS_TEXT;
    case SCR_LOGDISK: return s->t >= STEPS_LOGDISK_TEXT;
    case SCR_MENU_BUILD: return 0;
    case SCR_WELCOME: return s->welcome_first ? s->name_open : s->t >= s->welcome_text_at;   /* the first-tour keys wait in the queue until the entry opens; the continuing welcome takes the first key once its text has started (10.4) */
    default: return 1;
    }
}

/* The pass length in 1/8 blank (a step is 50) of a state whose pass is longer than a step (08 5.1 table, 5.8): the credits' disk-check pass 60 blanks (one key per about 10 steps), the menu build's one pass
 * 33 blanks (one per 5 steps), the location screen's gate-1 passes once its caption is typed (engine byte nonzero): 11.5 blanks (92 units, one key per 1.84 steps), and the three slow gate-1 passes of the aircraft
 * path between the location key and the aircraft caption (SCR_LOC_PICKED, 11 to 12 blanks, 11.5 on average) [V: flow7-latch-loc, flow7-latch-air, 08 5.8 errata of milestone 7]. While the location caption is typed
 * the passes run one per blank (shorter than a step) and every key is stored; the aircraft screen from its caption on runs 2 to 3 passes a blank and is not capped either.
 * 0: the passes are shorter than a step, no cap (the menu, the prompts, the aircraft screen, the approach, notice and flight: one pass per step). */
static int key_pass_units(const screen_t *s) {
    switch (s->state) {
    case SCR_CREDITS: return 60 * 8;
    case SCR_MENU_BUILD: return 33 * 8;
    case SCR_LOCATION: return s->eng.mode_byte ? 92 : 0;
    case SCR_LOC_PICKED: return 92;
    default: return 0;
    }
}

/* The latch V:4A85B: 1 when a key was stored in the pass that is running. Returns 1 when the key is lost (the latch is set), else sets the latch. The pass runs from the state's entry (or the last clear) and ends
 * every pass length; a state without a long pass does not latch (the pass accounting clears the latch after every step). */
static int key_latch_taken(screen_t *s) {
    if (!key_pass_units(s)) return 0;
    if (s->key_phase_state != (uint8_t)(s->state + 1)) { s->key_phase_state = (uint8_t)(s->state + 1); s->key_phase = 0; s->key_latch = 0; }
    if (s->key_latch) return 1;
    s->key_latch = 1;
    return 0;
}

/* The pass accounting at the end of a step: the latch is cleared when the pass of the state ends (every pass length), at once for the states without a long pass. */
static void key_latch_tick(screen_t *s) {
    const int pu = key_pass_units(s);
    if (!pu) { s->key_latch = 0; s->key_phase = 0; s->key_phase_state = 0; return; }
    if (s->key_phase_state != (uint8_t)(s->state + 1)) { s->key_phase_state = (uint8_t)(s->state + 1); s->key_phase = 0; s->key_latch = 0; }
    s->key_phase = (uint16_t)(s->key_phase + 50);
    if (s->key_phase >= pu) { s->key_phase = (uint16_t)(s->key_phase - pu); s->key_latch = 0; }
}

void screen_key(screen_t *s, const game_event_t *ev) {
    if (!s || !ev || s->nq >= SCREEN_QUEUE) return;   /* the queue holds 31 events and drops the later ones (5.8) */
    screen_keyev_t k;
    k.type = 0; k.val = 0; k.ch = 0;   /* ch is read by the callsign entry for every key type: a button carries none (10.5: keypad, F-keys and ANY change nothing) */
    if (ev->kind == GAME_EV_KEY) {
        const int c = ev->value;
        if (c == 27) k.type = SK_ESC;
        else if (c == 13 || c == 10) k.type = SK_RETURN;
        else if (c >= '0' && c <= '9') { k.type = SK_DIGIT; k.val = (uint8_t)(c - '0'); }
        else k.type = SK_OTHER;
        k.ch = (uint8_t)(c >= 0 && c < 256 ? c : 0);
    } else if (ev->kind == GAME_EV_BUTTON) {
        if (ev->value == 0) return;   /* a release never counts as a key (5.8) */
        switch (ev->code) {
        case GAME_BTN_ESC: k.type = SK_ESC; break;
        case GAME_BTN_SHIFT_ESC: k.type = SK_SHIFT_ESC; break;
        case GAME_BTN_RETURN: k.type = SK_RETURN; break;
        case GAME_BTN_ANY: k.type = SK_OTHER; break;
        case GAME_BTN_FKEY: k.type = SK_FKEY; k.val = (uint8_t)(ev->value >= 1 && ev->value <= 10 ? ev->value : 0); break;
        case GAME_BTN_PAUSE: k.type = SK_PAUSE; break;
        case GAME_BTN_KEYPAD:
            if (ev->value >= 1 && ev->value <= 8) { k.type = SK_DIGIT; k.val = (uint8_t)ev->value; }   /* KP1 to KP8 act as the digits 1 to 8 (5.4); KP0 and KP9 do not */
            else if (ev->value == GAME_KP_ENTER) k.type = SK_KPENTER;
            else k.type = SK_OTHER;
            break;
        default: return;   /* mouse, joystick and the flight buttons are not menu keys (5.9) */
        }
    } else return;
    /* The handler stores one key per pass (the latch V:4A85B, note 08 5.8 [V: followup-callsign-ring-slow]: of five letters sent while the credits' long pass ran only the first was stored; the latch is set by
     * the tail that stores a key in every state and cleared by the pass accounting). Conversion to the step clock (08 5.8): the keys stored per step are step blanks / pass blanks, so only the states with a
     * pass longer than a step cap them, for the length of that pass (key_pass_units); the 5-blank passes elsewhere give at least one key per step and are not capped. */
    if (key_latch_taken(s)) return;
    /* Presses that wait for the callsign entry sit in the typed-key ring, which holds 10 (5.8, 10.5 [R]; presses beyond 10 pending are dropped); the 31 of the
     * keyboard queue above bound everything else. The ring is cleared when the entry opens (post): the pending presses are dropped, not handed on. */
    if (s->state == SCR_WELCOME && s->welcome_first && !s->name_open && s->nq >= SCREEN_KEY_RING) return;
    s->q[s->nq++] = k;
}

static void deliver(screen_t *s) {
    while (s->nq > 0 && poll_ok(s)) {
        const screen_keyev_t k = s->q[0];
        memmove(s->q, s->q + 1, (size_t)(s->nq - 1) * sizeof s->q[0]);
        s->nq--;
        if (handle_key(s, k)) break;   /* one action per press: what is left waits for the new state (5.8) */
    }
}

/* ---- mission end (5.13) ------------------------------------------------------------------------------------------------------------------ */

/* Builds the id list of an outcome code. Returns the number of ids and the code that follows the text in *after (1, 2, 4, 0xF0 or 0xEF). */
static int end_list(const screen_t *s, uint16_t *ids, uint8_t *after) {
    const int mode = s->end_mode;
    int n = 0;
    *after = 1;
    switch (s->end_code) {
    case 0xFF:
        if (mode >= 3 && mode <= 8) { ids[n++] = (uint16_t)missions_list_id(s->chain, mode, 4);   /* word 4 of the mission id list (2.1): tables/briefing_chain.bin */ ids[n++] = mode == 3 ? 0x8055 : 0x8056; }
        else if (mode == 9) { ids[n++] = 0x4A; ids[n++] = 0x8053; *after = 0xEF; }
        else if (mode == 0x7D) { ids[n++] = 0x48; *after = 0xF0; }
        else ids[n++] = 0x41;
        break;
    case 0xFE:
        *after = 2;
        if (mode == 4) ids[n++] = s->end_counter ? 0x24 : 0x25;
        else if (mode == 5) { ids[n++] = 0x2C; *after = 0xF0; }
        else if (mode == 6) ids[n++] = 0x33;
        else if (mode == 7) { ids[n++] = 0x39; ids[n++] = s->end_counter ? 0x803A : 0x805C; *after = s->end_counter ? 0xF0 : 2; }   /* 12.12: the parity bit set ends the flight (0xF0, the menu); clear: 0x805C and the flight resumes with the code 2 */
        else ids[n++] = 0x42;
        /* the words 0x8056 and 0x8053 are appended one word too far in the original and never played (5.13): the list ends at the first zero */
        break;
    case 0xFD: ids[n++] = 0x4C; *after = 0xF0; break;
    case 0xFC: ids[n++] = 0x54; ids[n++] = 0x8053; *after = 4; break;
    default: break;
    }
    return n;
}

static void end_begin(screen_t *s, uint8_t code, uint8_t counter_bit0, int freeze_at) {
    s->end_freeze_at = freeze_at;
    s->end_list_at = freeze_at + (STEPS_END_LIST - STEPS_END_FREEZE);   /* the list is built three steps after the freeze (0x131F4, 5.13) */
    s->request = 0;                 /* the scripts' and the seat's request (2, 3) is consumed by the dispatcher that starts the end flow */
    s->end_code = code;
    s->end_menu = s->end_skip = 0;
    s->end_counter = counter_bit0;
    s->end_blank = code == 0xFE && !(s->mode_byte >= 4 && s->mode_byte <= 7);   /* 5.13: the failure of the other modes is the blank record 0x42 */
    s->end_mode = s->mode_byte == 0x7F ? 3 : s->mode_byte;   /* a running demo has mode 3 (5.10) */
    enter(s, SCR_END_WAIT);
}

void screen_mission_end(screen_t *s, uint8_t code, uint8_t counter_bit0) {
    if (!s || s->state != SCR_FLIGHT) return;
    end_begin(s, code, counter_bit0, STEPS_END_FREEZE);
}

int screen_in_outcome(const screen_t *s) { return s && (s->state == SCR_END_WAIT || s->state == SCR_END_TEXT || s->state == SCR_END_HOLD); }

void screen_mission_end_delayed(screen_t *s, uint8_t code, uint8_t counter_bit0, int delay) {
    /* 12.14 N2: a second request 3 arrives while the first outcome runs (a landing stored 0xFC): the dispatcher stores 0x131C8 again and 0x131F4 rebuilds the list for the new code; the engine is reset
     * there, so a text still being typed is cut */
    if (!s || (s->state != SCR_FLIGHT && !screen_in_outcome(s))) return;
    end_begin(s, code, counter_bit0, (delay > 0 ? delay : 0) + 1);   /* 12.2: the flight goes on for D passes (steps) before 0x131C8 freezes the world */
}

/* ---- the pause (5.14), the game over (5.12) ---------------------------------------------------------------------------------------------- */

void screen_pause(screen_t *s, int on) {
    if (!s) return;
    if (on && !s->paused && (s->state == SCR_FLIGHT || s->state == SCR_WAIT || s->state == SCR_APPROACH)) { s->paused = 1; s->pause_t = 1; s->pause_blank = 0; }
    else if (!on && s->paused) {
        s->paused = 0;
        s->eng.nlines = 0;   /* the picture returns over the text */
        eng_clear(s);
    }
}

void screen_gameover(screen_t *s, uint16_t text_id) {
    if (!s) return;
    eng_clear(s);
    s->paused = 0;
    s->sim_hold = 1;
    s->end_ids_text = text_id;
    show(s, GAME_MODE_GAMEOVER, 0);
    enter(s, SCR_GAMEOVER);
}

/* ---- the step ------------------------------------------------------------------------------------------------------------------------------ */

/* The pause text's clicks (note 10 7.3, 5.7 item 13, note 08 5.14 [V: sound7-pause]): nine starts at +13, +16, +18, +20, ... +30 blanks after the stop of the voices (the model of 3.6 and 3.4 rule 5: the line has eight non-blank
 * characters; the first is drawn on three passes and requests a click on the first two, the others one each, six passes apart). The pass of this screen is a third of a blank (2,000 units of 6,000 a blank, 6 passes a
 * character) and costs the same whatever it draws, except the second, which is 3 blanks long (milestone 7: the two requests of the first character are 3 blanks apart, +13 and +16; what makes that pass long is not traced);
 * the steps' budgets are their blanks x 6,000 (eng.exact), so a request has its blank exactly. The first pass of the list starts 78,000 units, 13 blanks, after the stop. The text of 08 5.14 is typed from the same
 * moment (the glyphs come with their requests). NOTE-GAP (note 08 5.14, note 10 7.3): the capture has a tenth start at +34, one pass-pair after the line's last character; its cause is not settled and the port makes none. */
#define PAUSE_PASS_UNITS 2000L
#define PAUSE_FIRST_PASS_START 78000L
static void pause_text(screen_t *s) {
    eng_list1(s, ID_PAUSE);
    s->eng.pass_cost = PAUSE_PASS_UNITS; s->eng.blit_cost = 0; s->eng.click_cost = 0; s->eng.exact = 1;
    s->eng.passes = 0;   /* the second pass of the list is the long one */
    s->eng.budget = -(PAUSE_FIRST_PASS_START - 6000L * s->pause_blank);   /* the step this runs in starts pause_blank blanks after the stop */
}

static void pause_timer(screen_t *s) {
    if (s->paused && s->pause_t == STEPS_PAUSE_TEXT && s->state != SCR_ESC_FADE) pause_text(s);   /* 5.14: the text starts when the fade to level 10 ends; its passes are placed from the stop */
}

static void timers(screen_t *s) {
    const int t = s->t;
    pause_timer(s);
    switch (s->state) {
    case SCR_CREDITS:
        if (t == STEPS_CREDITS_TEXT) eng_list1_timed(s, ID_CREDITS, BLANKS_CREDITS);
        break;
    case SCR_LOGDISK:
        if (t == STEPS_LOGDISK_TEXT) {
            const uint16_t ids[2] = { (uint16_t)(ID_LOGDISK_RESULT1 + s->logdisk_result - 1), ID_LOGDISK_HELP };
            eng_list_timed(s, ids, 2, s->logdisk_result == 1 ? BLANKS_LOGDISK_1 : BLANKS_LOGDISK);
        }
        break;
    case SCR_MENU_BUILD:
        if (t == s->menu_steps) {
            const uint16_t ids[11] = { ID_MENU_TITLE, ID_MENU_HEADER, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, ID_MENU_ESC };
            eng_clear(s);
            enter(s, SCR_MENU);
            eng_list_timed(s, ids, 11, BLANKS_MENU);
            if (s->shift_menu) { s->eng.budget = -(s->eng.per_step * SHIFT_MENU_DELAY / 25); s->shift_menu = 0; }   /* SHIFT-ESC from the air: the list is posted at k = 44 and its first click falls at k = 46 (note 08 5.11) */
            s->eng.step = 12;   /* the menu state sets the line step 0x1E0: 12 scanlines (5.4) */
        }
        break;
    case SCR_LOGSCREEN:
        if (t == 2 && s->eng.n == 0) eng_list1(s, ID_LOG_PAGE);   /* 10.6: the log state 0x11F86 at L+10 blanks */
        break;
    case SCR_WELCOME:
        if (s->welcome_credits && t == STEPS_WELCOME_CREDITS_WAIT) { eng_clear(s); show(s, GAME_MODE_WELCOME, 0); }   /* the credits state's wait is over: the set-up clears the screen */
        if (t == s->welcome_text_at) {
            if (s->welcome_first) { const uint16_t ids[2] = { ID_WELCOME_FIRST, ID_CALLSIGN }; eng_list(s, ids, 2, BUDGET_STEP); }
            else eng_list1_timed(s, ID_WELCOME_AGAIN, BLANKS_WELCOME_AGAIN);
        }
        break;
    case SCR_ECHO:
        if (s->echo_restart && !s->echo_fkey && t == STEPS_ECHO_TEXT_RESTART) eng_list1(s, echo_id(s->mode_byte));
        if (t == STEPS_ECHO_TOTAL) dispatch(s);
        break;
    case SCR_BLACK:
        if (t == (s->mode_byte == 9 ? STEPS_QUAL_BLACK : s->echo_restart ? STEPS_BLACK_RESTART : STEPS_BLACK_FIRST)) enter(s, SCR_FADE_IN);
        break;
    case SCR_FADE_IN:
        if (t == (s->mode_byte == 9 ? STEPS_QUAL_FADE_IN : s->echo_restart ? STEPS_FADE_IN_RESTART : STEPS_FADE_IN_FIRST)) {
            if (s->mode_byte == 9 && s->mode == GAME_MODE_FLIGHT) qual_after_fade(s);
            else if (s->echo_restart) enter(s, SCR_MAP);   /* 5.11: the accepted code skips the prompt but the map stage still runs */
            else location_enter(s);
        }
        break;
    case SCR_MAP:
        if (s->mode_byte == 9) { if (t == STEPS_QUAL_MAP) enter(s, SCR_QUAL_VERDICT); }   /* the qualification's restart: the map stage, then the verdict state */
        else if (t == STEPS_MAP) location_enter(s);
        break;
    case SCR_QUAL_VERDICT:
        if (t == STEPS_QUAL_VERDICT) { if (s->echo_restart) approach_to_notice(s); else brief_enter(s); }
        break;
    case SCR_LOC_PICKED:
        if (t == STEPS_LOC_TO_AIRCRAFT && s->location != 3) {
            eng_clear(s);
            enter(s, SCR_AIRCRAFT);
            eng_list1(s, ID_AIRCRAFT);
        } else if (t == STEPS_LOC_TO_START && s->location == 3) place_start(s);
        break;
    case SCR_AIR_PICKED:
        if (t == STEPS_AIR_TO_START) place_start(s);
        break;
    case SCR_TRAIN_DELAY:
        if (s->train_restart ? s->restart_clock >= STEPS_TRAIN2_RESTART_POST : t == (s->mode_byte == 2 ? STEPS_TRAIN1_POST : STEPS_TRAIN2_POST)) { enter(s, SCR_TRAIN_BRIEF); eng_list_timed_steps(s, &s->lead_text, 1, s->mode_byte == 2 ? STEPS_TRAIN1_TYPED : STEPS_TRAIN2_TYPED); }
        break;
    case SCR_APPROACH:
        if (t == s->notice_at && s->mode_byte == 2 && !s->demo) {   /* 6.2: the mode 2 sets the engine byte to 0xFF in 0x12DB8: no notice, 0x12E4E acts at once */
            eng_clear(s);
            s->sim_hold = 0;
            s->out_sim = 1;
            enter(s, SCR_WAIT);
            break;
        }
        if (t == s->notice_at && s->mission) {   /* 0x12DB8 (13.2): the outcome lookup 0x29534 posts the vector notice 0x58 when an aircraft of the sub-list is valid, then the launch or take-off notice */
            uint16_t ids[2]; int n = 0;
            const int deg = s->bearing_fn ? s->bearing_fn(s->ms_user) : -1;
            if (s->log) s->log->bogey_deg = (int16_t)deg;
            if (deg >= 0) ids[n++] = ID_NOTICE_VECTOR;
            ids[n++] = s->location == 3 ? ID_NOTICE_LAUNCH : ID_NOTICE_TAKEOFF;
            enter(s, SCR_NOTICE);
            eng_list_timed(s, ids, n, n == 2 ? BLANKS_NOTICE_MISSION : BLANKS_NOTICE);
            break;
        }
        if (t == s->notice_at) {
            enter(s, SCR_NOTICE);
            if (s->demo) {   /* 7.5 (0x12DB8): the ids 0x58 and 0x5A are posted; the engine byte goes negative after about 290 passes (3.8: the pass count is the rule, not the blanks of the run). The outcome lookup runs in the demo too
                              * (its mode byte is 3, 5.10): the bearing is patched into 0x58 when the lookup finds an aircraft (13.2) */
                if (s->log) s->log->bogey_deg = (int16_t)(s->bearing_fn ? s->bearing_fn(s->ms_user) : -1);
                const uint16_t ids[2] = { ID_NOTICE_VECTOR, ID_NOTICE_LAUNCH };
                eng_list_timed(s, ids, 2, BLANKS_NOTICE_DEMO);
            } else eng_list1_timed(s, s->location == 3 ? ID_NOTICE_LAUNCH : ID_NOTICE_TAKEOFF, BLANKS_NOTICE);
        }
        break;
    case SCR_MISSION:
        mission_timers(s);
        break;
    case SCR_DEMO_WAIT:   /* 7.5: the set-up 0x11BF4 */
        if (t == s->demo_wait) {
            screen_start_flight(s);   /* the allowance and the placement of the 0x7F row, then the mode byte 3 (0x11C32) */
            s->mode_byte = 3; s->location = 3; s->aircraft = 0x11; s->direct_restart = 1;
            s->v4a848 = 1;            /* 0x11BF4 stores V:4A848 = 1: no clock, no booking, no crash counters until the next mission start (7.6, 10.7) */
            s->out_place = 1; s->sim_hold = 1;
            show(s, GAME_MODE_FLIGHT, 1);
            eng_clear(s);
            set_notice_at(s, STEPS_DEMO_TO_NOTICE, 1 + STEPS_DB8, 0);
            enter(s, SCR_APPROACH);
        }
        break;
    case SCR_WAIT:
        if (t == STEPS_WAIT) { s->out_hud = 1; enter(s, SCR_FLIGHT); }
        break;
    case SCR_ESC_FADE:
        if (t == s->esc_r - 1) s->out_pulse = 1;   /* the gate pulse: one flight pass at level 0 before the reset (5.11, 12.11) */
        if (t == s->esc_r) {   /* the reset block runs: the flight is lost, the echo of the mode follows */
            const int was_flight = s->mode == GAME_MODE_FLIGHT || s->mode == GAME_MODE_GAMEOVER;
            if (was_flight) leave_flight(s);
            s90f(s, 1);   /* the reset block runs 0x1D1BE too (0x1D25E) */
            if (s->snd) sound_stop_all(s->snd);   /* 0x11AE2: the four sound channels stop (0x115F6) */
            /* 0x11AF8: the ESC flow rewrites training part 1 as part 2 (8.1); part 2 restarts itself. 7.7: after the demo's set-up the restart skips the echo. */
            if (s->mode_byte == 2) s->mode_byte = 0x7D;
            if (s->direct_restart) { s->direct_restart = 0; s->mode_byte = 0x7F; screen_demo_begin(s, STEPS_DEMO_RESTART_WAIT); }
            else if (s->request == 3) {   /* 12.14 N6: the script's request 3 of the gate pulse survives the reset: the menu state takes it and the menu set-up runs: +44 blanks = 7 steps */
                eng_clear(s);
                show(s, GAME_MODE_MENU, 0);
                enter(s, SCR_ESC_DETOUR);
            } else echo_enter(s, 1);
        }
        break;
    case SCR_ESC_DETOUR:
        if (t == 1) { s->request = 0; s->request_delay = 0; }   /* 0x11E04 consumes the request */
        if (t == STEPS_ESC_DETOUR) echo_enter(s, 1);
        break;
    case SCR_GAMEOVER:
        if (t == STEPS_GAMEOVER_TEXT) eng_list1_timed(s, s->end_ids_text, BLANKS_GAMEOVER);
        break;
    case SCR_END_WAIT:
        if (t == s->end_freeze_at) s->out_book = 1;
        if (t == (s->end_blank ? s->end_list_at - STEPS_LOST_FREEZE : s->end_freeze_at)) s->sim_hold = 1;   /* a blank outcome record freezes the world only for its 14 blanks (8.9) */   /* 5.13, 10.7: the dispatcher takes the request 3 (0x11748 books the time, whatever the outcome code) */
        if (t == s->end_list_at) {
            uint16_t ids[8] = { 0 }; uint8_t after = 1;
            int blanks = 0;
            s->end_n = end_list(s, ids, &after);
            memcpy(s->end_ids, ids, sizeof ids);
            s->end_after = s->end_menu ? 0xF0 : after;
            if (s->end_code == 0xFF && s->end_mode == 3) blanks = BLANKS_END_F1;
            else if (s->end_code == 0xFE && s->end_mode == 4) blanks = BLANKS_END_M4_FAIL;
            else if (s->end_code == 0xFF && s->end_mode == 9) blanks = BLANKS_END_M9;
            log_outcome(s);   /* Task 8 hook: the debrief writes the log */
            eng_clear(s);
            enter(s, SCR_END_TEXT);
            eng_list_timed(s, ids, s->end_n, blanks);
        }
        break;
    case SCR_END_HOLD:
        if (t == STEPS_END_HOLD_PASSES) {
            if (s->end_after == 0xF0) { leave_flight(s); menu_enter_in(s, STEPS_MENU_BUILD_GAMEOVER); }   /* 8 + 33 blanks = 1 + 5 (5.13) */
            else esc_begin(s);   /* 0xEF: the ESC restart of 5.11 starts by itself */
        }
        break;
    default:
        break;
    }
}

void screen_demo_begin(screen_t *s, int wait_steps) {
    if (!s) return;
    s->demo = 1; s->demo_wait = wait_steps;
    s->sim_hold = 1;
    eng_clear(s);
    show(s, GAME_MODE_MENU, 0);
    enter(s, SCR_DEMO_WAIT);
}

/* The places of the numbers 1 to 4 (top-left pixel of the 3x5 glyph) and of their dots on the overview map. Note 04, "The start marks of the overview map": the mark pass 0x2FAB6 projects the
 * start table 0x47ADE (note 05 17.14) by the overview camera (0x10800000, 0x03000000, 0x10C00000; matrix 168, 0, 0, 0, 0, 252, 0, -128, 0; row 179, step 50) to the screen point (319 - px, 180 - py):
 * the dot (pen 2) there, the glyph (pen 9) at ((x and not 3) + 8, y). These are the four results for the airfields 1 to 3 and the carrier (verified pixel for pixel against the emulator frame and
 * the rig, mapcam_check.py), kept as a table because the start table's positions are not carried by this module; the index is V:4A900.
 * The rule's two conditions (04 "The start marks of the overview map", review S15): the carrier's mark exists only while record 14 is active, and a pause draws every entry whatever the camera mode. In free
 * flight the placement row always puts the carrier in slot 14, so the first condition is always met at the selection screens (the table needs no derivation); the second is applied in screen_map_marks
 * (P on the aircraft screen, `map_frozen`). */
static const int16_t MARK_XY[4][4] = { { 176, 96, 170, 96 }, { 216, 73, 211, 73 }, { 260, 146, 253, 146 }, { 88, 114, 83, 114 } };   /* x, y of the glyph, x, y of the marker dot */

int screen_map_marks(const screen_t *s, game_map_mark_t out[GAME_MAX_MAP_MARKS]) {
    int n = 0;
    if (!s || s->mission) return 0;   /* the missions have no location screen: the numbers of the starts are the location prompt's */
    if (s->state == SCR_LOCATION) {
        for (int i = 0; i < 4; i++) { out[n].x = MARK_XY[i][0]; out[n].y = MARK_XY[i][1]; out[n].digit = (uint8_t)(i + 1); out[n].pen = 9; out[n].dot_x = MARK_XY[i][2]; out[n].dot_y = MARK_XY[i][3]; n++; }
    } else if (s->state == SCR_LOC_PICKED || s->state == SCR_AIRCRAFT || s->state == SCR_AIR_PICKED) {
        const int i = s->loc_key;   /* V:4A900 as stored by the key (keys 5 and 6: 4 and 5, no active entry: nothing is drawn) */
        if (s->map_frozen) {   /* 04 "Camera mode 6": a pause (V:4A866 non-zero, here P on the aircraft screen, 5.14) draws every entry whatever the camera mode */
            for (int k = 0; k < 4; k++) { out[n].x = MARK_XY[k][0]; out[n].y = MARK_XY[k][1]; out[n].digit = (uint8_t)(k + 1); out[n].pen = 9; out[n].dot_x = MARK_XY[k][2]; out[n].dot_y = MARK_XY[k][3]; n++; }
        } else if (i >= 0 && i < 3) { out[0].x = MARK_XY[i][0]; out[0].y = MARK_XY[i][1]; out[0].digit = (uint8_t)(i + 1); out[0].pen = 9; out[0].dot_x = MARK_XY[i][2]; out[0].dot_y = MARK_XY[i][3]; n = 1; }
    }
    return n;
}

void screen_poll(screen_t *s) { if (s) deliver(s); }

void screen_set_logdisk_result(screen_t *s, int r) { if (s) s->logdisk_result = (uint8_t)(r < 0 || r > 3 ? 0 : r); }

/* The transitions that wait for a flag the engine sets, tested after the engine's passes of the step: the state routine sees the flag in the very next pass, which is
 * the same step (the passes of a step are 1/6 blank). */
static void post(screen_t *s) {
    switch (s->state) {
    case SCR_NOTICE:   /* 0x12E4E: the notice typed and its end hold run out: the simulation switch, the counter cleared (6.2) */
        if (s->t > 0 && screen_text_finished(s)) {
            eng_clear(s);
            s->sim_hold = 0;
            s->out_sim = 1;
            enter(s, SCR_WAIT);
        }
        break;
    case SCR_GAMEOVER:   /* 0x13A36: the engine byte is negative (the text and its end hold): 0x11A70 resets, the menu follows */
        if (screen_text_finished(s)) { leave_flight(s); menu_enter_in(s, STEPS_MENU_BUILD_GAMEOVER); }
        break;
    case SCR_WELCOME:
        if (s->welcome_first) {
            if (!s->name_open && s->t > s->welcome_text_at && s->eng.n > 0 && s->eng.cur >= s->eng.n) {   /* the list is played: the entry opens, tours is 1 (10.4) */
                s->name_open = 1;
                s->nq = 0;   /* 5.8 [V: followup-callsign-ring(-slow)]: the typed-key ring and the typed text are cleared when the entry opens; the presses made before are dropped, not handed on */
                logbook_name_begin(&s->name);
                logbook_set16(s->log, LB_TOURS, 1);
            }
        } else if (s->t > s->welcome_text_at && screen_text_finished(s)) menu_enter_fresh(s);   /* 10.4 [V]: typed over 105 blanks, the end hold, then the menu build */
        break;
    case SCR_BRIEF:
        if (screen_text_finished(s)) approach_to_notice(s);
        break;
    case SCR_TRAIN_BRIEF:   /* 8.1: the text and its end hold; part 1 goes to 0x12DB8 (6 passes), part 2 through the camera approach to 0x12DB8 and the take-off notice */
        if (screen_text_finished(s)) {
            eng_clear(s);
            if (s->mode_byte == 2) set_notice_at(s, STEPS_DB8, STEPS_DB8, 0);
            else { set_notice_at(s, STEPS_TRAIN2_APPROACH + STEPS_TRAIN2_TO_DB8 + STEPS_DB8, STEPS_TRAIN2_TO_DB8 + STEPS_DB8, 1); s->approach_len = STEPS_TRAIN2_APPROACH + STEPS_TRAIN2_TO_DB8; }
            if (s->mode_byte == 2) sredraw(s);   /* part 1 (external chase view): the gate-open passes of the briefing's end ran the panel block and counted R down 3, 2, 1, 0 (07 12.1 [V]) */
            if (s->mode_byte != 2) span(s);   /* 0x124F8 (the mode 0x7D approach) stores V:4A8EE = 3 at 0x1250C */
            enter(s, SCR_APPROACH);
        }
        break;
    case SCR_MISSION:   /* 0x126F6 and 0x12A50: the text and its end hold are over */
        if (s->ms_stage == MS_INTRO && screen_text_finished(s)) { span(s); ms_to_stage7(s, 1); }   /* 0x126F6: the text finished, V:4A8EE = 3 (0x12730) */
        else if (s->ms_stage == MS_ORDERS && screen_text_finished(s)) { eng_clear(s); s90f(s, 1); ms_goto(s, MS_TAIL, 1); }   /* 0x12A50: the text finished, V:4A90F = 1 (0x12A76) */
        break;
    case SCR_END_TEXT:
        if (s->end_after == 0xF0 || s->end_after == 0xEF) {   /* the final screens wait for the text and its end hold */
            if (s->t > 0 && screen_text_finished(s)) { enter(s, SCR_END_HOLD); if (s->end_skip) esc_begin(s); }
        } else if (s->t > 0 && s->eng.mode_byte != 0) {       /* the flight resumes in the step in which the typing ends, no end hold (5.13: the engine byte is nonzero) */
            s->sim_hold = 0; s->out_end = s->end_after; s->end_code = 0; eng_clear(s); enter(s, SCR_FLIGHT);
        }
        break;
    default:
        break;
    }
}

void screen_step(screen_t *s) {
    if (!s) return;
    const int frozen = s->paused && s->state == SCR_APPROACH;   /* P in the approach state 0x12D58 pauses the mover with the pause screen (5.14): the approach counter stands still */
    s->in_timers = 1;
    if (frozen) pause_timer(s); else timers(s);
    s->in_timers = 0;
    deliver(s);
    shift_sound(s);
    eng_run(s);
    if (!frozen) s->t++;
    key_latch_tick(s);
    if (s->restart_clock >= 0) s->restart_clock++;
    if (s->paused) { s->pause_t++; s->pause_blank += s->snd ? sound_step_blanks(s->snd) : 6; }
    post(s);
}

void screen_set_mode_handler(screen_t *s, int (*fn)(void *user, uint8_t mode_byte), void *user) { if (s) { s->mode_handler = fn; s->mode_user = user; } }

void screen_set_approach_len(screen_t *s, int passes) {
    if (!s || s->state != SCR_APPROACH || passes < 1) return;
    set_notice_at(s, passes + STEPS_APPROACH_TO_NOTICE, STEPS_APPROACH_TO_NOTICE, 1);
    s->approach_len = passes;
}
int screen_approach_due(const screen_t *s) { return s && s->state == SCR_APPROACH && s->t >= s->approach_len; }
int screen_sim_hold(const screen_t *s) { return s && s->sim_hold; }

/* ---- the scene --------------------------------------------------------------------------------------------------------------------------- */

void screen_fill(const screen_t *s, game_scene_t *out) {
    if (!s || !out) return;
    int n = 0;
    for (int i = 0; i < s->eng.nlines && n < GAME_MAX_TEXT; i++) {
        if (s->eng.lines[i].shown == 0) continue;
        out->text[n++] = s->eng.lines[i];
    }
    if (s->state == SCR_WELCOME && s->name_open && s->name.ncell > 0 && n < GAME_MAX_TEXT) {   /* the callsign being typed: one line of at most 41 cells at the entry line (10.5) */
        game_text_t *g = &out->text[n++];
        memset(g, 0, sizeof *g);
        g->id = ID_CALLSIGN;
        g->row = 130; g->col = 4; g->pen = 5; g->step = 11;
        if (s->text && ID_CALLSIGN <= s->text->id_count && s->text->id[ID_CALLSIGN - 1].line_count > 0) {   /* the position and pen of the first line of the callsign record */
            const text_line_t *L = &s->text->line[s->text->id[ID_CALLSIGN - 1].first_line];
            g->row = L->row; g->col = L->col; g->pen = (uint8_t)(L->colour >> 4);
        }
        g->length = g->shown = (uint16_t)(s->name.ncell < LOGBOOK_NAME_CELLS ? s->name.ncell : LOGBOOK_NAME_CELLS);
        memcpy(g->text, s->name.cell, g->length);
        g->text[g->length] = '\0';
    }
    out->text_count = (uint16_t)n;
    out->palette_regime = s->regime;
    switch (s->state) {
    case SCR_BLACK: case SCR_DEMO_WAIT: case SCR_ESC_DETOUR: out->fade_target = 0; break;
    case SCR_MISSION: out->fade_target = s->ms_stage == MS_BLACK ? 0 : 15; break;
    case SCR_ESC_FADE: out->fade_target = s->t <= s->esc_pass ? 15 : 0; break;   /* a demo ESC in the fade-in is acted on at the next pass (esc_begin): the fade-in goes on until then */
    case SCR_FLIGHT: case SCR_APPROACH: case SCR_NOTICE: case SCR_WAIT: case SCR_END_WAIT: case SCR_END_TEXT: case SCR_END_HOLD: case SCR_EXTERNAL: case SCR_BRIEF: case SCR_TRAIN_DELAY: case SCR_TRAIN_BRIEF:
        out->fade_target = s->paused ? 10 : out->sky.fade_target ? out->sky.fade_target : 15;
        break;
    default: out->fade_target = 15; break;
    }
    if (s->paused && s->state != SCR_ESC_FADE) out->fade_target = 10;
}

void screen_debug_flight(screen_t *s, int location, int aircraft) {
    eng_clear(s);
    s->nq = 0;
    s->mode_byte = 1;
    s->location = (uint8_t)location;
    s->aircraft = (uint8_t)(aircraft ? 0x10 : 0x11);
    screen_start_flight(s);
    s->sim_hold = 0; s->paused = 0;
    show(s, GAME_MODE_FLIGHT, 1);
    enter(s, SCR_FLIGHT);
}

/* ---- creation ----------------------------------------------------------------------------------------------------------------------------- */

int screen_init(screen_t *s, const game_loader_t *loader, char *err, size_t errlen) {
    memset(s, 0, sizeof *s);
    s->rng = 1;   /* the default seed of the placement generator (screen_random_seed) */
    s->cam_mode = 0xFF; s->brief_target = -1;
    {   /* the text records: absent files (a synthetic asset set) leave the screens without text; present files must be whole */
        uint8_t *probe = NULL; size_t plen = 0;
        if (loader->read(loader->user, "tables/text.bin", &probe, &plen) == 0) {
            loader->release(loader->user, probe);
            s->text = (text_t *)calloc(1, sizeof *s->text);
            if (!s->text) { core_assets_append(err, errlen, "screen_init: out of memory"); return -1; }
            if (assets_load_text(loader, s->text, err, errlen) != 0) { free(s->text); s->text = NULL; return -1; }
        }
    }
    {
        uint8_t *probe = NULL; size_t plen = 0;
        if (loader->read(loader->user, "tables/scenario.bin", &probe, &plen) == 0) {
            loader->release(loader->user, probe);
            s->scn = (scenario_t *)calloc(1, sizeof *s->scn);
            if (!s->scn) { core_assets_append(err, errlen, "screen_init: out of memory"); free(s->text); s->text = NULL; return -1; }
            if (assets_load_scenario(loader, s->scn, err, errlen) != 0) { free(s->scn); s->scn = NULL; free(s->text); s->text = NULL; return -1; }
        }
    }
    {
        uint8_t *probe = NULL; size_t plen = 0;
        if (loader->read(loader->user, "tables/months.bin", &probe, &plen) == 0) {
            loader->release(loader->user, probe);
            if (assets_load_months(loader, s->months, err, errlen) != 0) { free(s->text); free(s->scn); s->text = NULL; s->scn = NULL; return -1; }
            s->have_months = 1;
        }
    }
    {
        uint8_t *probe = NULL; size_t plen = 0;
        if (loader->read(loader->user, "tables/briefing_chain.bin", &probe, &plen) == 0) {
            loader->release(loader->user, probe);
            if (assets_load_briefing_chain(loader, s->chain, err, errlen) != 0) { free(s->text); free(s->scn); s->text = NULL; s->scn = NULL; return -1; }
        }
    }
    eng_clear(s);
    s->mode_byte = 0;
    s->location = 3; s->aircraft = 0x11;
    show(s, GAME_MODE_CREDITS, 0);
    enter(s, SCR_CREDITS);
    s->t = 0;
    return 0;
}

void screen_destroy(screen_t *s) {
    if (!s) return;
    free(s->text); free(s->scn);
    s->text = NULL; s->scn = NULL;
}
