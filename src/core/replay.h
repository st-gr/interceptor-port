/* src/core/replay.h - the demo replay (docs/re/08-game-flow.md section 7): the recorded key byte and the two event words of every step of the original's demo flight,
 * played into the ordinary flight. The demo is a normal flight of the F/A-18 from the carrier deck whose controls come from the stream (7.1); this module holds the
 * stream and its reader (7.4) and turns the event bits (7.3) into the key events of the flight; game.c owns the flight and feeds the result in.
 *
 * Time: the stream is per step, not per blank (7.8): frame i is applied at core step i (the i-th simulated step of the demo flight), no conversion. The screens around
 * the flight (7.5, 7.7) are the screen module's (screen_demo_begin) and use the blank-to-step rule of note 05 17.17.
 *
 * Tags as in the note: C = port convention.
 */
#ifndef INTERCEPTOR_REPLAY_H
#define INTERCEPTOR_REPLAY_H
#include <stdint.h>
#include "game.h"
#include "assets.h"
#include "screen.h"

#define REPLAY_MAX_EVENTS 28   /* the 28 event bits of 7.3 */

/* One step of the stream as the reader hands it to the flight. */
typedef struct {
    int valid;                           /* 0: no frame (the replay is not running, or it is over) */
    int index;                           /* the frame's index (= the demo step number, 0 for the first simulated step) */
    uint8_t key;                         /* the player record's control byte for this step (+0x65, note 05 section 2) */
    uint16_t word1, word2;               /* the event words of index `index` (the words of index 0 are never executed: 7.4 rule 1) */
    int n_events;
    game_event_t ev[REPLAY_MAX_EVENTS];  /* the events to run before the step, in the executor's order (7.3), as the key events of the flight */
    uint8_t view_dep[REPLAY_MAX_EVENTS]; /* 1: the body of the bit reads the live view when it runs (KP5 in the cockpit, "]" outside): ev[i] is the cockpit form, replay_event_in_view gives the live one */
    uint16_t gap1, gap2;                 /* the set bits that have no key event: none since all bits are mapped (R, T, J, the fixed camera and the eject arm are key events); kept as a check, always 0 */
} replay_frame_t;

typedef struct replay {
    demo_t *demo;                        /* the stream, NULL without a demo file */
    int played;                          /* the frames played: up to the first run of three 0xFF key bytes (7.4 rules 3 and 4), or the file's length */
    int active;                          /* the flight is a demo flight: the live control keys are ignored (7.6) */
    int step;                            /* the next frame to hand out (the key pointer of 7.4) */
    screen_t *scr;
    int (*prev)(void *user, uint8_t mode_byte);   /* the handler that was installed before (the other tasks' modes): chained for every mode but 0x7F */
    void *prev_user;
} replay_t;

/* Loads tables/demo.bin when the asset set carries it (absent: no demo, menu key 1 is ignored, returns 0; present but refused: -1 with the message) and installs the
 * mode handler of 0x7F on the screen machine, chaining the handler that was there. */
int  replay_init(replay_t *r, screen_t *scr, const game_loader_t *loader, char *err, size_t errlen);
void replay_destroy(replay_t *r);
/* Tests and tools: the stream from memory (r must be zero-initialised or already set up; a previous stream is freed) instead of the file (n frames; word arrays may be NULL for no events). Installs the handler as replay_init does. */
int  replay_set_stream(replay_t *r, screen_t *scr, const uint8_t *keys, const demo_events_t *ev, int n);
/* The number of frames played of a key array: the index of the first run of three 0xFF bytes (7.4 rule 4), or n. */
int  replay_played_frames(const uint8_t *keys, int n);

/* The mode handler (screen_set_mode_handler): the mode 0x7F starts the demo's wait state; every other mode goes to the chained handler. */
int  replay_mode_handler(void *user, uint8_t mode_byte);

/* The flight of the demo starts (the set-up 0x11BF4 placed the player): the reader is at frame 0 and the live control keys are ignored. */
void replay_start(replay_t *r);
/* The flight is left (ESC flow reset, the menu): the reader stops. */
void replay_stop(replay_t *r);
/* The event i of a frame as it runs when the live view is (ext_view = 1) or is not an outside view: only the view-dependent bit differs (7.3). The caller asks at the event's
 * turn, after the bodies of the earlier bits of the same frame have run. */
game_event_t replay_event_in_view(const replay_frame_t *f, int i, int ext_view);
/* One simulated step (7.4): fills *out with the frame for this step. Returns 1 when more frames follow, 0 when this was the last played frame (the stream has ended: the caller requests the ESC flow after the step, 7.4 rule 5) or when
 * there is no frame (out->valid = 0). */
int  replay_step(replay_t *r, replay_frame_t *out);

#endif
