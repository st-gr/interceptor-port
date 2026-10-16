/* src/core/screen.h - the front-end screens of note 08: the mode machine from the credits to the flight and back, the text engine that
 * plays the text records, the key queue, the mission start and end.
 *
 * game.c owns the flight and embeds one screen_t; the screen module never touches the flight state. They meet through a few request flags
 * (out_place, out_leave, out_sim, out_end) that game.c reads after every screen_step, and through screen_flight_*(), screen_pause() and
 * screen_gameover() that game.c calls when the flight tells the screen something. The signatures take a screen_t, not a game_t (the game
 * struct is private to game.c); game.c wraps them.
 *
 * Time: one screen_step is one fixed step, 1/FLIGHT_STEP_HZ of a second (6.25 blanks). A duration the original measures in 50 Hz blanks is
 * converted with (blanks * 8 + 25) / 50 (note 05 17.17, note 08 5.1). The text engine is driven by the pass budget of note 08 3.8. In the notes
 * below "+n steps" means: if the event happens in step 0 (the screen_step that delivers the key), the effect shows in the scene after the
 * screen_step number n. Tags as in the note: C = port convention, NOTE-GAP = an open question of the note.
 */
#ifndef INTERCEPTOR_SCREEN_H
#define INTERCEPTOR_SCREEN_H
#include <stdint.h>
#include "game.h"
#include "assets.h"
#include "logbook.h"
#include "sound.h"

#define SCREEN_KEY_RING 10        /* the typed-key ring: 10 presses pending before the callsign entry opens (note 08 5.8, 10.5) */
#define SCREEN_QUEUE 31           /* the keyboard queue: 31 events, later ones are dropped (note 08 5.8; V) */
#define SCREEN_LIST_IDS 16
#define SCREEN_MAX_PLACED 24

/* The states of the machine (note 08 5.1, "The journey at a glance"). */
typedef enum {
    SCR_CREDITS = 0,   /* 5.2: text id 0x0F typed, any key goes on */
    SCR_LOGDISK,       /* 5.3: ids 0x46, 0x5E; RETURN goes on (the capture's write-protected variant; the port reads no disk) */
    SCR_MENU_BUILD,    /* 5.4: the menu build, one pass of 33 blanks: no key is polled, the text starts at the end */
    SCR_MENU,          /* 5.4 */
    SCR_LIST,          /* 5.4: key 6, the list of selectable missions (empty log) */
    SCR_ECHO,          /* 5.4: the confirmation line of the chosen item, then the mode dispatch (6.1) */
    SCR_BLACK,         /* 5.11: the first passes of a free-flight start: black, the scene is built */
    SCR_FADE_IN,       /* 4.4: the fade-in of the flight palette, 0 to 15 */
    SCR_MAP,           /* 5.11: after an ESC restart the map stage (camera mode 4, the prompt skipped by the accepted code) still runs 77 blanks before the location caption */
    SCR_LOCATION,      /* 5.6 */
    SCR_LOC_PICKED,    /* 5.6: after the location key, until the aircraft caption (or the start, for the carrier) */
    SCR_AIRCRAFT,      /* 5.7 */
    SCR_AIR_PICKED,    /* 5.7: after the aircraft key, until the start position is in the record */
    SCR_APPROACH,      /* 6.4: the camera approach (the port counts the passes of 12.14 N1, one per step; KP-ENTER skips it) */
    SCR_NOTICE,        /* 6.2: the take-off or launch notice typed over the cockpit */
    SCR_WAIT,          /* 6.2: the three waiting passes and the acting pass */
    SCR_FLIGHT,        /* the flight runs; overlays (pause text, mission end) are played over it */
    SCR_ESC_FADE,      /* 5.11: the fade-out, the gate pulse and the reset after an ESC */
    SCR_GAMEOVER,      /* 5.12 */
    SCR_END_WAIT,      /* 5.13: the freeze and the build of the outcome list */
    SCR_END_TEXT,      /* 5.13: the outcome list is typed over the frozen picture */
    SCR_END_HOLD,      /* 5.13: the final screens (codes 0xF0 and 0xEF): the hold after the text */
    SCR_WELCOME,       /* 10.4: the first-tour welcome and the callsign entry, or the continuing-tour welcome (Task 8) */
    SCR_LOGSCREEN,     /* 10.6: the flight-log statistics (menu key 8), UPDATE and RESET (Task 8) */
    SCR_BRIEF,         /* 9.2: the qualification briefing (id 0x49) over the carrier deck, left by its end or any key (Task 8) */
    SCR_TRAIN_DELAY,   /* 8.1: the start scene stands still between the dispatch and the post of the training briefing (24 blanks in part 1, 67 in part 2) */
    SCR_TRAIN_BRIEF,   /* 8.1: the training briefing (id 9 or 10) typed over the start scene; no key leaves it; then the common tail (the part 1 countdown of 0x12DB8, or the part 2 approach, 0x12DB8 and the take-off notice) */
    SCR_ESC_DETOUR,    /* 12.14 N6: an ESC restart that carries a lingering request 3 (a script fired in the gate pulse): the menu state consumes it and the menu set-up 0x11D30 runs before the dispatch */
    SCR_QUAL_VERDICT,  /* 9.2 (milestone 7): the verdict state 0x11CC0 of the qualification, 3 passes after the fade-in (first start: then the briefing) or after the map stage (ESC restart: then the approach) */
    SCR_DEMO_WAIT,     /* 7.5: the demo's wait state 0x11B54 between the dispatch and the set-up that starts the flight (screen_demo_begin) */
    SCR_MISSION,       /* 13.1: the briefing chain of the modes 3 to 8 from the dispatch to the player start (ms_stage below); the aircraft prompt of F1 is SCR_AIRCRAFT */
    SCR_EXTERNAL       /* a mode started by a later task's handler (demo, training, qualification, missions): that module owns the display; ESC and SHIFT-ESC still act */
} screen_state_t;

/* One placed entity of the mission placement 0x2CE36 (note 08 6.1): the words of the scenario entry, decoded as the note lists them. The positions are the
 * position-block table's (0x2DCE8, the waypoint runs of tables/ai.bin) and are left as the raw reference. */
typedef struct {
    uint16_t template_id;     /* word 0 / 20: the record template (note 02 section 1): 8 carrier, 3 F-16, 6 MiG-29 */
    uint8_t cls;              /* word 1, low byte: the class stored at +0x62 */
    uint8_t slot;             /* word 2, bits 0-6: the record index */
    uint16_t pos_ref;         /* word 3: byte offset into the position-block table */
    uint16_t flags;           /* word 4 */
    uint8_t counted;          /* 1 when the entity is counted as a hostile (aircraft class, "not counted" bit clear) */
    uint16_t w[5];            /* the five words of the entry as the scenario table gives them (entities.c places from them) */
} screen_placed_t;

typedef struct {
    uint8_t type;             /* SK_* below */
    uint8_t val;              /* the digit 0..9, or the function key number of SK_FKEY */
    uint8_t ch;               /* the ASCII value of a GAME_EV_KEY (the name entry of 10.5 reads it); 0 for the buttons */
} screen_keyev_t;

typedef struct screen {
    text_t *text;             /* the text records (NULL: no text assets, the screens run with empty lists) */
    scenario_t *scn;          /* the scenario tables (NULL: location 0 and the carrier only, no placement) */
    char months[36];          /* tables/months.bin: the four month names of the date line (note 08 10.9); have_months 0 when the file is absent (a synthetic asset set) */
    uint8_t have_months;
    uint8_t chain[30];        /* tables/briefing_chain.bin: the text-record ids of the briefing chain (note 08 2.1; zeros when the file is absent, a synthetic asset set) */
    screen_state_t state;
    int t;                    /* steps since the state was entered (0 in the step of entry) */
    game_mode_t mode;         /* the scene mode this screen shows; game.c maps it onto g->mode (PAUSED is game.c's) */
    uint8_t mode_byte;        /* V:4A95E, note 08 5.10: 0 menu, 1 free flight, 2, 3-8, 9, 0x7D, 0x7F; 0xFF/0xFE pseudo-modes never stored */
    uint8_t location;         /* V:4A900 as the FLIGHT reads it: the start block (0..2 airfields, 3 the carrier; the keys 5 and 6 give 0 because 0x1D730 clears the byte at the start, 08 5.6; the keys 7 to 0 keep 6 to 9) */
    uint8_t loc_key;          /* V:4A900 as the location key STORED it (key - 1, key 0: 9; 08 5.6, 5.x: key 5 stores 4, `flow-s2-loc-key5.txt`): what the aircraft screen's mark pass reads (04 "The start marks") */
    uint8_t aircraft;         /* V:4A901: 0x11 F/A-18, 0x10 F-16 */
    int allowance;            /* V:4A94F, set at the dispatch (6.3) */
    uint8_t demo_available;   /* menu key 1 needs the demo stream (Task 6 sets it) */
    uint8_t paused;           /* the pause screen is up (game.c reports it) */
    uint8_t map_frozen;       /* P on the location or aircraft screen: the map mover is frozen, no pause screen (6.4); a second P clears it */
    uint8_t logdisk_result;   /* the disk check the frontend reports (5.3): 0 usable log, no prompt (default); 1 to 3 show the prompt of that result */
    uint8_t end_menu, end_skip;   /* 5.13: an ESC with an end code set: the final screen goes to the menu; on 0xEF the countdown is skipped */
    uint8_t regime;           /* scene.palette_regime of the screen shown */
    uint8_t in_timers;        /* set while the timers run: a state entered there starts at t = 0, one entered by a key or a call from game.c at t = 1 (see enter) */
    uint8_t shift_defer;      /* SHIFT-ESC from the air: the menu build's sound waits for shift_blank = 11 (menu_build_in, shift_sound); no click comes before it */
    uint8_t shift_menu;       /* the menu list that follows is posted at k = 44 and its first click falls at k = 46 (note 08 5.11)  */
    int shift_blank;          /* the library blanks since the key, at the start of the step */
    int menu_steps;           /* steps from the reset to the first menu text: 7 after RETURN and SHIFT-ESC, 6 after a game over or the final screens (5.12 sums to 20 steps from A) */
    int notice_at;            /* SCR_APPROACH: the step in which the notice starts (the natural end of the approach, or the KP-ENTER skip) */
    uint16_t end_ids_text;    /* SCR_GAMEOVER: the text id */
    uint8_t end_after;        /* SCR_END_*: the code that follows the text (1, 2, 4, 0xF0, 0xEF) */
    /* A later task's mode handler (demo 0x7F, trainings 2 and 0x7D, qualification 9, missions 3 to 8): called by the dispatch with the mode byte, returns 1 when
     * it takes the screen over (the machine goes to SCR_EXTERNAL; it calls screen_start_flight itself for the allowance and placement), 0 for "not built", and 2 for
     * "start the flight through the briefing flow" (the trainings, note 08 8.1): the handler has set location, aircraft and lead_text; the machine does the allowance
     * and the placement request, types lead_text over the placed start scene (SCR_TRAIN_DELAY, SCR_TRAIN_BRIEF), then the tail of 8.1 and the three-tick wait and the flight follow. */
    uint16_t lead_text;       /* handler result 2: the text id typed before the flight starts (the training briefing: 9 for part 1, 10 for part 2) */
    int (*mode_handler)(void *user, uint8_t mode_byte);
    void *mode_user;
    /* the flight log (Task 8, note 08 section 10): the record of the game (NULL: no log, the menu keys 7 and 8 do nothing and there is no welcome) */
    logbook_t *log;
    sound_t *snd;             /* the sound module (note 10): the key clicks, the music, the master ramp's inputs and the voice stops of the screens (NULL: silent, the unit tests of a bare screen module) */
    uint8_t log_welcome;      /* the welcome and the callsign entry follow the log-disk screen (game_log_import was called) */
    uint8_t welcome_first;    /* SCR_WELCOME: the first-tour welcome with the callsign entry (tours was 0) */
    int welcome_text_at;      /* SCR_WELCOME: the step at which the text starts (19 from the credits key, 2 from the log-disk prompt and UPDATE) */
    uint8_t welcome_credits;  /* SCR_WELCOME entered from the credits: they stay on screen for their own 87-blank wait */
    uint8_t name_open;        /* the callsign entry is taking keys */
    uint8_t echo_fkey;        /* the mode was chosen with a function key: its echo screen shows no line (5.4) */
    logbook_name_t name;
    uint8_t demo;             /* the demo (7.5): 1 from screen_demo_begin until the menu is entered again; the mode byte is 0x7F until the set-up, then 3 */
    uint8_t v4a848;           /* V:4A848 (7.5, 7.6, 10.7): set by the demo's set-up, cleared by the next mission start; while set no flight time is booked and the crash counters (+0x46, +0x10) stay */
    uint8_t direct_restart;   /* the demo's set-up has run: an ESC restarts the demo directly, without the echo screen (7.7); cleared at the restart */
    int demo_wait;            /* SCR_DEMO_WAIT: the steps until the set-up */
    int pause_t;              /* steps since the pause began */
    int pause_blank;          /* the library blanks of the steps since the pause began (the stop of the voices is the start of the first of them) */
    int restart_clock;        /* steps since the last ESC (esc_begin); counts on (starts at 0 at the first ESC; the screen is zero-initialised, so 0 before any ESC is harmless: train_restart gates its use) */
    uint8_t train_restart;    /* the training dispatch came from an ESC restart: SCR_TRAIN_DELAY waits until restart_clock reaches STEPS_TRAIN2_RESTART_POST */
    uint8_t echo_restart;     /* 1: the echo set-up of an ESC restart (150 passes) */
    uint8_t esc_l0;           /* the fade level when the ESC was taken */
    int esc_r;                /* steps from the ESC to the reset */
    uint8_t key_latch;        /* V:4A85B: a key was stored in the pass that is running (08 5.8); held for the whole pass in the states with a pass longer than a step (key_pass_units in screen.c) */
    uint8_t key_phase_state;  /* the state + 1 that key_phase counts for (0: none) */
    uint16_t key_phase;       /* 1/8 blanks since the start of the running pass of that state */
    int esc_pass;             /* steps from the ESC to the pass that consumes it (a demo ESC in the fade-in waits for the next pass boundary, 08 7.7); 0 otherwise */
    /* the key queue */
    screen_keyev_t q[SCREEN_QUEUE];
    int nq;
    /* the placement of 6.1 */
    uint8_t variant;          /* V:4ABD4: the variant of the last fresh start whose selector was not 0 (note 07 2.4); an ESC restart (echo_restart) uses it as it stands */
    uint32_t rng;             /* the core's seeded generator (game_random_seed): one 16-bit draw per fresh mission start is the selector V:4ABB0 */
    uint16_t selector;        /* V:4ABB0 of the last fresh start */
    int pl_dx, pl_dz;         /* V:4ABD0, V:4ABD2: the random offset pair of the last fresh start (unhalved); an ESC restart reuses it */
    int sub_first, sub_count; /* the sub-list the placement walks: entries [sub_first, sub_first + sub_count) of the scenario's sub_entry[] (V:4A7F2) */
    uint8_t hostile_cap;      /* V:4A962 */
    uint8_t hostile_count;    /* V:4A961 */
    int placed_count;
    screen_placed_t placed[SCREEN_MAX_PLACED];
    uint8_t request;          /* V:4A8E2: the flow request byte (1 ESC, 0xFF SHIFT-ESC; 2 and 3 from the scripts and, for 2, the landed ejection seat); the seat makes its request only while this is 0 */
    int8_t request_delay;     /* V:4A8E4: the seat's request 2 sets it to 4; the dispatcher counts it down once per pass and acts in the first pass in which it is below 0 (note 07 6.4/6.5, note 08 5.11) */
    /* the missions F1 to F6 (note 08 13, milestone 5): handler answer 3 */
    uint8_t mission;          /* 1 from the dispatch of a mode 3 to 8 until the menu is entered again */
    uint8_t ms_stage;         /* MS_* below */
    int ms_t;                 /* steps since the stage began */
    int8_t brief_target;      /* V:4AA72: the record the briefing camera flies to (13.4), -1 none */
    uint8_t cam_mode;         /* V:4A966 of the briefing: 2 approach (stage 7), 3 arrived, 4 the map overview, 6 the start approach; 0xFF outside */
    uint8_t out_pulse;        /* the gate-1 pulse pass of an ESC restart (0x11AE2, 5.11): one world update runs, so a script whose trigger holds fires (12.11); game.c runs it and clears this */
    uint8_t out_esc;          /* an ESC was taken (esc_begin): game.c sets the pause byte V:4A866 to 1 (5.11, 12.11) and clears this */
    int (*bearing_fn)(void *user);          /* the outcome lookup 0x29534: the bearing in degrees 0 to 359 of the first aircraft of the sub-list, -1 for none; NULL: none (no vector notice) */
    int (*visible_fn)(void *user, int record);   /* the draw's projection test of 13.4 (the target inside x 20 to 300, y 14 to 186): NULL: visible from the first pass (the observed behaviour); the briefing camera stays at the overview and stage 7 lasts 2 passes, 08 13.4 C1; the game passes NULL, tests supply a stand-in */
    void *ms_user;
    /* requests for game.c; each is cleared by game.c after it has acted */
    uint8_t out_wind;         /* the state 0x12C6E/0x12C90 starts the approach of a mission (modes 3 to 8): the wind or the engine call of a flight start (note 10 7.2); game.c plays it and clears this */
    int approach_len;         /* the passes of the approach state 0x12D58 (the cockpit pass is the last of them): the table's count, 29 + 2 for training part 2, the key's step for a KP-ENTER skip */
    uint8_t view_kind;        /* SV_*: how the flight start is seen (12.14 C13, 06 start table): set by the free-flight placement and the trainings' dispatch, cleared by every other start (screen_start_flight); game.c reads it with out_place */
    uint8_t out_place;        /* the start position is to be written to the player record (location, aircraft) and the flight shown (frozen) */
    uint8_t out_leave;        /* the flight is lost: game.c resets the cockpit, the crash flow and the flight mode */
    uint8_t out_sim;          /* the simulation switch is set (6.2): the aircraft is simulated from this step, n restarts at 0 */
    uint8_t out_hud;          /* the acting pass of the wait: the HUD is switched on */
    uint8_t out_redraw;       /* a flow state stored V:4A8EF = 3 together with V:4A8EE (the helper's pair): the end of training part 1's briefing text and the location key (0x12BA0); [R] for the location key: the free flight reaches its first pass with 0 (07 12.1); game.c stores it in the cockpit's R */
    uint8_t out_panel;        /* a flow state stored V:4A8EE = 3 (0x120E0: the dispatch state; 0x1250C: the training part 2 approach; 0x12730: the end of a mission's intro text or a key there; 0x12BA0: the location key); game.c stores it in the cockpit's panel counter */
    uint8_t out_90f_set;      /* a flow state stored V:4A90F; out_90f is the last value stored (-1 = 0xFF). The stores (note 06 "What V:4A90F and V:4A86D hold"): 1 at the reset 0x1D25E (the menu build and the ESC reset), 1 at 0x1258E, 0 at 0x125F8, 1 at 0x12756, 0 at 0x127AE, 1 at 0x12A76, 0 at 0x12AE2, 1 at 0x12B52, 0 at 0x12BD2 (a location key but the carrier's), 1 at 0x12C56 (the aircraft key); 0x12D2A (the approach's end) is game.c's */
    int8_t out_90f;
    uint8_t out_book;         /* 10.7: the dispatcher took the leave request 3 (STEPS_END_FREEZE, 5.13): game.c books the flight time into the log now, before the debrief saves it */
    uint8_t out_end;          /* the outcome screen is over and the flight resumes (code 1) or continues (code 2) */
    uint8_t snd_tail, snd_ext;   /* the last steps of the approach state in which the sound routine runs, and whether the first has the approach's outside view (screen_sound_pass) */
    uint8_t sim_hold;         /* 1: the flight is frozen (the world is not stepped): the approach, the notice, an ESC being taken, the outcome screens */
    /* mission end (5.13) */
    uint8_t end_code, end_mode, end_counter;
    uint8_t end_blank;        /* the outcome is the blank record 0x42 (code 0xFE of a mode other than 4 to 7: the lost lead of training part 2, 8.9) */
    uint16_t end_ids[8];
    int end_n;
    int end_freeze_at, end_list_at;   /* SCR_END_WAIT: the step (counted in the state) at which the world freezes (V:4A866 = 1 by 0x131C8) and at which the list is built (0x131F4); 4 and 7 for the callers of
                               * screen_mission_end (the poked requests of the captures), D + 1 and D + 4 for a script's request 3 with the delay D (screen_mission_end_delayed) */
    /* the text engine (note 08 3.4) */
    struct {
        uint16_t ids[SCREEN_LIST_IDS + 1];
        int n, cur;
        int running;
        uint8_t mode_byte;    /* V:4A898: 0 typing, 1 list done and holding, bit 7 finished, 0xFF after a reset */
        int hold;
        const text_id_t *rec; /* the record being played */
        int line;             /* line within the record */
        uint8_t flags, pen, D;
        int row, col;         /* position of the current line */
        int last_row, last_col;
        int step;             /* the line step: 11, 12 in the menu list */
        int cell, cd, pending, lp, between, last, line_delay, first_pass, bit14;
        int slot;             /* the index in lines[] of the current line, -1 for a blank line */
        long budget;          /* units left (can be negative): 1000 per pass, 1500 per click, 285 per glyph */
        long per_step;        /* units added per step: 37,500 (note 08 3.8), less for the lists the note says run longer in blanks than the model */
        uint32_t passes;      /* passes run since the last clear (diagnostics and tests) */
        long pass_cost, blit_cost;   /* units per pass and per glyph blit: 1,000 and 285, or the pause screen's 2,000 and 0 (a pass is a third of a blank, 6 passes a character: note 10 7.3) */
        uint8_t exact;        /* the budget of a step is its blanks x 6,000 units, so that a pass has its blank exactly (the pause text) */
        long click_cost;      /* units per click request: 1,500 (the credits fit, note 08 3.8) or MS_CLICK_COST on the mission screens (3.8 item 3a) */
        long idle_step;       /* units per step once the list is played: 50,000, the 50-pass end hold is one 8-blank stage = 1 step */
        game_text_t lines[GAME_MAX_TEXT];
        int nlines;
    } eng;
} screen_t;

/* Allocates the text and scenario tables and loads them through the loader. Missing files are not an error (a synthetic asset set); a file that
 * is present and refused is: it returns -1 with the message. On success the machine is in SCR_CREDITS at step 0. */
int  screen_init(screen_t *s, const game_loader_t *loader, char *err, size_t errlen);
void screen_destroy(screen_t *s);

/* Every key outside a running flight goes here (and ESC / SHIFT-ESC in the flight): GAME_EV_KEY with an ASCII value, and the buttons RETURN, ESC,
 * SHIFT_ESC, ANY, KEYPAD (1 to 8 act as the digits), FKEY. Presses only: a release (value 0) is ignored; the frontend sends no repeats (5.8). The
 * event is queued (31 events); screen_step delivers the queue in order while the state polls. */
void screen_key(screen_t *s, const game_event_t *ev);

/* The qualification (mode 9, note 08 section 9): called by the mode handler at the dispatch. The carrier start (location 3, F/A-18), the allowance 3, the player placed
 * and shown through the black passes and the fade-in, the briefing (id 0x49, only when the ESC restart flag is clear: V:4A8E3 = 0), the launch notice, the wait and the
 * flight. The success is reported by the game with screen_mission_end(0xFF, 0); the debrief writes the log (qualified = 1) and saves it (10.8). Returns 1. */
int  screen_qualification_start(screen_t *s);

/* The briefing chain of a mission (modes 3 to 8, handler answer 3): the stages of note 08 13.1, in steps from the dispatch (the key is 7 steps earlier). */
enum { MS_BLACK = 0, MS_FADE, MS_WAIT, MS_INTRO, MS_STAGE7, MS_ORDERS, MS_TAIL, MS_RESTART };   /* MS_RESTART: 0x1279C and 0x127C8 of an ESC restart (5.5), before the orders */
/* The mission outcome of a script's request 3 (note 08 5.13, 12.2): the dispatcher took the request (six passes after the store) with the delay D of V:4A978 (negative: none); the flight
 * goes on for D more steps, then the world freezes (D + 1 steps in the state), the list is built three steps later. counter_bit0 is bit 0 of the step counter at the debrief. */
/* 1 while an outcome screen (the delay, the frozen wait, the text, the final hold) is up (5.13). */
int screen_in_outcome(const screen_t *s);
void screen_mission_end_delayed(screen_t *s, uint8_t code, uint8_t counter_bit0, int delay);
void screen_set_mission_hooks(screen_t *s, int (*bearing)(void *user), int (*visible)(void *user, int record), void *user);

/* Installs the handler of the modes the later tasks build (see screen_t.mode_handler). */
void screen_set_mode_handler(screen_t *s, int (*fn)(void *user, uint8_t mode_byte), void *user);

/* The demo's start (note 08 7.5, 7.7), called by the mode handler of 0x7F from the dispatch or by the direct restart: the wait state of `wait_steps` steps (the screen
 * is black), then the set-up: the carrier placement of the mode 0x7F row (screen_start_flight), the mode byte 3, location 3 (the deck) and aircraft 0x11, the flight
 * placed frozen (out_place) and the 17 steps of the fade-in and the tail states before the notice; the notice and the wait are the ordinary ones (6.2). The camera approach
 * and its KP-ENTER skip do not exist in the demo. */
void screen_demo_begin(screen_t *s, int wait_steps);

/* One step: timers, the delivery of the queued keys, the text engine's passes for the step's budget. */
void screen_step(screen_t *s);

/* Delivers the queued keys at once, outside a step (the key poll of pass A after the crash hold, game.c). */
void screen_poll(screen_t *s);

/* The result of the log-disk check the frontend supplies before the credits are left (5.3): 0 (default) no prompt, 1 to 3 the prompt of that result. */
void screen_set_logdisk_result(screen_t *s, int r);

/* Fills scene.text / text_count, palette_regime and fade_target for the screen (not the mode: game.c maps s->mode). In SCR_FLIGHT the fade target
 * is left as the caller set it. */
void screen_fill(const screen_t *s, game_scene_t *out);

/* The mission start of the dispatch (note 08 6.1): the allowance by mode (5, 3 or 1, 6.3) and the scenario placement. Called by the machine at the
 * dispatch; exposed for tests. Returns the number of entities placed. */
int  screen_start_flight(screen_t *s);
/* Seeds the generator of the placement (the frontend supplies the seed; the golden runs use 1). The default seed is 1. A fresh mission start draws one 16-bit selector from it
 * (note 07 2.4: the variant and the random offset pair); an ESC restart reuses the stored variant and offsets. */
int screen_start_flight_selector(screen_t *s, unsigned selector);   /* screen_start_flight with the selector V:4ABB0 given (rigs); ignored on an ESC restart */
unsigned screen_draw_selector(uint32_t *rng, int mode);   /* note 07 2.4: the measured distribution of V:4ABB0 */
void screen_random_seed(screen_t *s, uint32_t seed);

/* The flight tells the screen: a pause begun or ended (5.14); the allowance reached 0 (5.12, text id 0x62 or 0x63); the mission scripts' outcome
 * request (5.13; code 0xFF success, 0xFE failure, 0xFD, 0xFC, 0xF0, 0xEF; counter_bit0 is bit 0 of the step counter). */
void screen_pause(screen_t *s, int on);
void screen_gameover(screen_t *s, uint16_t text_id);
void screen_mission_end(screen_t *s, uint8_t code, uint8_t counter_bit0);

/* The approach view kinds (view_kind): the cockpit of the placed flight (the qualification, the demo, the skipped approach), the free-flight approach (the mover flies from the selection camera, external view, step 50, window row 179 then 167),
 * training part 1 (external, step 50, row 167 from the dispatch through the first flight pass) and part 2 (external, step 50, row 167; the mover from stage 3 over the 31 passes). */
enum { SV_COCKPIT = 0, SV_FREE, SV_TRAIN1, SV_TRAIN2, SV_MISSION };   /* SV_MISSION: the missions F1 to F6 (modes 3 to 8): the external approach from the briefing camera's position, the mover entered in its stage 8 (08 12.14 C14 [V: sound7-approach]) */
/* The approach state's cockpit pass is due: the state has run its approach_len passes (12D58's, the count of the table), the last of which shows the cockpit. game.c switches the view at the mover's own end or when this holds. */
int screen_approach_due(const screen_t *s);
/* A mission's approach is as long as its mover takes from the briefing camera to the start (08 12.14 C14): game.c runs the mover ahead on a copy and gives the count (the cockpit pass is the last); the notice follows by STEPS_APPROACH_TO_NOTICE. Only in the approach state. */
void screen_set_approach_len(screen_t *s, int passes);

/* The numbers of the starts on the overview map (scene.map_marks): all four on the location screen, the chosen one on the aircraft screen; returns the count. */
int  screen_map_marks(const screen_t *s, game_map_mark_t out[GAME_MAX_MAP_MARKS]);

/* True while the flight must not be stepped (the world is frozen). */
int  screen_sim_hold(const screen_t *s);
int  screen_sound_pass(const screen_t *s);   /* 0: the sound routine does not run in this step of the approach state; 1: it runs with the approach's outside view (the first pass after a natural end); 2: the pass after the end's switch to the cockpit view (the view setter posts the notice 0xFE and stops voice 3); 3: it runs (note 10 5.7 item 10) */
/* The finished flag of the engine (V:4A898 bit 7). */
int  screen_text_finished(const screen_t *s);
/* Debug and tests: the machine goes to the flight at once (the placement and the notice are skipped). */
void screen_debug_flight(screen_t *s, int location, int aircraft);

#endif
