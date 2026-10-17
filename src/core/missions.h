/* src/core/missions.h - the six missions F1 to F6 (game modes 3 to 8): the per-update script dispatcher, the six scripts, the end codes and the hand-over to the
 * debrief, and the tables of the briefing chain (docs/re/08-game-flow.md sections 12, 13 and 14, "note 08"; the section numbers below are that note's unless another note
 * is named). Tags: C = port convention, NOTE-GAP = an open question of the note.
 *
 * The dispatcher is the end-of-update routine 0x2852E (12.1): the game calls missions_step once per pass of the world update, after the player's step and the entity loop of
 * the same pass (entities_step), also in the passes where the pause byte V:4A866 is non-zero. Everything it reads is in the entity records and the world variables
 * entities.h keeps; what it writes is the end code V:4A850 and the pause byte V:4A866 (both live in the player's flight state, aircraft.h: v4a850 and v4a866), the flow request
 * V:4A8E2 and its countdown V:4A8E4 (the screen module's bytes, which entities.h points at: one request byte shared with the ejection seat), and the variables of this
 * module. The consequences that belong to the camera and the view (the hand-over to the external view and the tracking camera) are left as requests in `view` for the game to
 * apply, since the view state is the game's.
 *
 * Core rules: C99, fixed-width integers, no stdio, no floating point, no wall time. */
#ifndef INTERCEPTOR_MISSIONS_H
#define INTERCEPTOR_MISSIONS_H
#include <stddef.h>
#include <stdint.h>
#include "entities.h"

/* End codes V:4A850 (12.2, 5.13). */
#define MS_END_SUCCESS 0xFF
#define MS_END_FAIL 0xFE
#define MS_END_DESTROYED 0xFD        /* the player destroyed Air Force One himself (F2) */
#define MS_END_COMPLETE 0xFC         /* the landing after a success: mission complete */
#define MS_END_CRASH 4               /* the crash paths: the tail ignores it */
#define MS_END_BAILOUT 0xF0

/* The click cost of the text engine on the mission screens (note 08 3.8 item 3a, C): a pass that requests a click costs about 0.9 of a blank there, so a pass is 1,000 + 5,350 x clicks + 285 x glyph blits units against the budget
 * of 37,500 per step; the eight measured briefing and orders records are reproduced within 1.3 percent (milestone 7, Task 2: 5,100 was 2.9 percent low on the orders). */
#define MS_CLICK_COST 5350L

/* The records the scripts name (12.1): the principal aircraft, the thieves, the fighters, the pod's site marker. */
#define MS_SLOT_PRINCIPAL 4
#define MS_SLOT_THIEF2 6
#define MS_SLOT_FIGHTER1 8
#define MS_SLOT_FIGHTER2 10
#define MS_SLOT_SITE 11

/* The view requests of the hand-over (12.2) and the tracking camera 0x1EC14: the game applies them to its view state and clears `pending`. The variables are the original's. */
typedef struct missions_view {
    /* inputs, set by the game before each missions_step */
    uint8_t in_selector;         /* V:4A83D: the view selector (0 the cockpit) */
    uint8_t in_direction;        /* V:4A96A: the ring direction */
    int8_t in_ident;             /* V:4A927: the F1 contact counter, owned by the radar walk (radar_t.mission1, note 07 12.3): the game feeds it before each step; negative = F1 success */
    /* the state the scripts keep and write */
    uint8_t v4a876;              /* the hand-over flag: bit 7 set once it ran, the low bits the old direction */
    uint8_t v4a8ec;              /* the saved view selector */
    uint8_t v4a8eb;              /* V:4A8EB: 0xFF (F2) or 3 (F5) as the hand-over wrote it; the game feeds the saved external level here before each pass (12.2: the tracking camera takes the selector from it) */
    uint8_t v4a96a;              /* the ring direction written (7: the tracking ring); stays after the game applied it (ring_set) so that the restart keeps it (12.11) */
    uint8_t ring_set;            /* 1: V:4A96A was written in this pass, the game applies it to its ring and clears this */
    uint8_t v4a86d, v4a86c;      /* the camera-update and look flags (1) */
    uint8_t v4a865;              /* 0 */
    uint16_t v4a9e2;             /* the heading saved for the cockpit view; the ring rotation 0x2D756 compares the record's heading with it every frame (note 06 "Ring rotation") */
    uint8_t heading_set;         /* 1: v4a9e2 was written in this pass, the game puts it into the view state's heading (world_viewstate_t.heading) and clears this */
    uint8_t toggle_request;      /* V:4A97E bit 1: the cockpit/external toggle */
    /* the tracking camera (0x1EC14) */
    uint8_t track;               /* 1 when the routine ran in this pass */
    uint8_t v4a8ee;              /* 3 */
    uint8_t v4a83d;              /* the selector the tracking routine wrote in an external view (from V:4A8EB) */
    uint8_t sel_set;             /* 1: v4a83d was written in this pass; the game puts it into its view level and clears this */
    uint8_t pending;             /* 1: something above changed in this pass */
} missions_view_t;

typedef struct missions {
    uint8_t mode;                /* V:4A95E as the module was started with (3 to 8; 0 when idle) */
    uint8_t location;            /* V:4A900: 3 carrier start, 0 an airfield start (the landing test) */
    int16_t timer;               /* V:4A83A: F3's shadowing timer, a signed word; cleared by the placement */
    uint16_t delay;              /* V:4A978: the delay of the shared end (a word: the F6 quirk makes it negative) */
    uint8_t key_lock;            /* V:4A965 = 1 while the result is being handed over (flight keys locked) */
    missions_view_t view;
    /* counters for the tests and the report */
    unsigned ends, count_passes;
} missions_t;

/* The mission start (12.13 item 1, 14.4 item 1): the script state of a mode 3 to 8 is cleared (V:4A83A, V:4A927, the hand-over flag, the delay, the key lock), as the placement and the
 * player start do. `location` is V:4A900. A crash restart does NOT call it (12.11): the state survives. Any other mode leaves the module idle (mode 0). */
void missions_start(missions_t *m, int mode, int location);
/* ESC (5.11): the request sets the pause byte V:4A866 to 1 (a held trigger then ends its count during the restart's fade and the reset block clears what it stored). */
void missions_esc(flight_state_t *pf);

/* The end-of-update routine 0x2852E (12.1): the request bytes V:4A872 to V:4A874 are cleared, the stale-lock drop runs, then the dispatcher: it acts only when the simulation bit is
 * set and the demo flag is clear, does nothing for a mode byte of 2 or less (signed), runs the tail when the end code is not 0 and the script of the mode otherwise (modes 3 to
 * 7 their scripts, 9 and 0x7D the qualification and the training, which the game runs elsewhere, every other mode the kill-count script). Modes 9 and 0x7D do nothing here. */
void missions_step(missions_t *m, entities_t *e);

/* The debrief's resume (5.13, 0x1305E, 0x130BA): the flight resumes (code 1 or 2): V:4A965 = 0, V:4A866 = 0, and the hand-over's view bytes are the game's to restore. */
void missions_resume(missions_t *m, flight_state_t *pf);

/* ---- the tables of the briefing chain (2.1, 13) ---- */
/* `chain` is tables/briefing_chain.bin (30 bytes, 5 ids for each mode 3 to 8). Word `index` (0 to 4) of the id list of mission `mode` (3 to 8): the two briefings, the shared notice 0x5F, the orders, the outcome (2.1). 0 for a mode outside 3 to 8. */
unsigned missions_list_id(const uint8_t *chain, int mode, int index);
/* The briefing id of the mission for a completion byte (13.1: index 0 when it is 0, else index 1). */
unsigned missions_briefing_id(const uint8_t *chain, int mode, unsigned completions);
/* The approach target of 13.4 (0x2DA7C): the record of the first valid, counted aircraft entry of the placement sub-list, else the last valid aircraft found, else -1.
 * `slots[]`, `cls[]`, `counted[]` are the placed entries in sub-list order (the screen's list). */
int missions_approach_target(const uint8_t *slots, const uint8_t *cls, const uint8_t *counted, int n);

/* ---- the text patches (2.4, 10.9, 13.2) ---- */
/* The day of the date line of record 0x5B: (hours AND 31) + 1, limited to 30 (10.9); and the month index min(hours, 127) >> 5. `hours` is the flight time in whole hours. */
unsigned missions_date_day(unsigned hours);
unsigned missions_date_month(unsigned hours);
/* The bearing of the vector notice 0x58 in degrees: heading word / 80, 360 and above wrap (13.2). */
unsigned missions_bearing_degrees(unsigned heading_word);

#endif
