/* src/core/training.h - the two training parts of the main menu (docs/re/08-game-flow.md section 8, "note 08"; section numbers below are that note's).
 *
 * The lessons are the INSTRUCTOR aircraft's own state machine: there is no detection of the player's flying (8.1). The module owns
 *   - the instructor, record 4, a second flying entity of class 0x10 (8.3), stepped by the flight model (flight_step_entity) with the key byte its state
 *     routines write (8.5),
 *   - the lesson sequencer 0x27222 (8.4): lesson number, trigger state, script cursor, the advance, the end of a manoeuvre,
 *   - the player's re-position 0x270A0 (8.6) and the three keys that drive the lessons (RETURN, the fire trigger, S),
 *   - the posts of 8.7 (warning-line entries; the caller forwards them to the cockpit's message line),
 *   - the end check of part 2 (8.9).
 *
 * It does not know game_t (that struct is private to game.c): the caller (game.c) passes the player's aircraft_t and reads the outputs below. The order of one
 * pass is the original's (8.3): training_player_pass (0x270A0, in the player's pre-steps), the player's own flight_step, then training_step (record 4: the
 * sequencer, the decrement of +0x4C, the state routine, the step). Time is in steps; no blank conversion exists in this section (8.1).
 *
 * Part 1 is the mode byte 2 (the instructor flies the seven manoeuvres in a loop and the player watches), part 2 the mode byte 0x7D (the player follows and
 * triggers the manoeuvres). Core rules: C99, fixed-width integers, no stdio, no floating point, no wall time. */
#ifndef INTERCEPTOR_TRAINING_H
#define INTERCEPTOR_TRAINING_H
#include <stdint.h>
#include "aircraft.h"
#include "assets.h"

#define TRAINING_MODE_PART1 2
#define TRAINING_MODE_PART2 0x7D
#define TRAINING_MAX_POSTS 8

/* The warning-table entries the training uses (8.7). */
enum { TRAINING_POST_SPLITS_LOW = 53, TRAINING_POST_NOT_READY = 31 };
#define TRAINING_CODE_NOT_READY 0x401Fu   /* the "not ready to re-position" notice: a timed information (8.6) */

/* training_result */
enum { TRAINING_RUNNING = 0, TRAINING_LOST = 0xFE };   /* LOST: part 2's instructor is lost (W0 bit 0x0040 clear): the end request 0xFE of 8.9, reported once */

typedef struct trainer {
    training_t tbl;            /* tables/training.bin (8.2, 8.13); the defaults of the note when absent */
    int have_tbl;
    int part;                  /* 0 not started, 1 part 1 (mode byte 2), 2 part 2 (mode byte 0x7D) */
    aircraft_t inst;           /* record 4. Its flight-state switch fields (V:4A984 etc.) are copied from the player's before every step */
    uint8_t b64;               /* +0x64: bit 0x10 set after every state that commands the controls (8.5, "epilogue") */
    uint8_t lesson;            /* V:4A851: 0 to 7 */
    uint8_t trig;              /* V:4A852: 0 normal, 0xFF the prompt of the next lesson is posted, 0xFE the same after a RETURN re-position (part 2 only) */
    uint16_t cursor;           /* V:54E42: 0 until the instructor has been given its state for this lesson, 1 after */
    uint8_t running;           /* V:4A84B: the part 1 flight is running (gates the sequencer of part 1 and selects the wait after a manoeuvre) */
    uint8_t paused;            /* V:4A866: the sequencer does nothing while it is non-zero (the caller sets it) */
    uint8_t viewed_inst;       /* V:4A994 is the instructor (part 1: the camera is on record 4) */
    uint8_t lost_reported;
    int placements;            /* the number of placements of record 4 (tests) */
    uint8_t src_b70, src_b71;  /* input: the player's record bytes +0x70 and +0x71 (the entity record, set by the caller before any call that can place): part 2's placement copies the first 0xA4 bytes of the player's record, these two included */
    uint8_t placed_new;        /* output: a placement happened since the caller last stored b70 and b71 (the caller clears it) */
    uint8_t b70, b71;          /* output: the instructor record's +0x70 and +0x71 after the last placement (part 1: the template's 0 and 0xFF, 08 8.3; part 2: the player's); the caller stores them in the entity record */
    /* outputs of the last call(s); the caller consumes and clears them */
    uint16_t posts[TRAINING_MAX_POSTS];   /* codes for the message line, in order (8.7) */
    int npost;
    int8_t  out_ring;          /* V:4A96A: the external direction the advance stores (-1: none) */
    int16_t out_view;          /* V:4A83D: the view selector the advance stores in part 1 when the view is external (-1: none) */
    uint8_t out_redraw;        /* V:4A910 = 0xFF: the display lists are to be rebuilt */
} trainer_t;

/* Clears the module. tbl may be NULL (no training.bin: the note's rate-table values and camera bytes are used). */
void training_init(trainer_t *t, const training_t *tbl);

/* Starts a part from the mode byte V:4A95E (TRAINING_MODE_PART1 = 2 or TRAINING_MODE_PART2 = 0x7D; any other value leaves the module inactive): the sequencer's variables are cleared, record 4 is placed (8.3). The player is the aircraft the flight start
 * has just placed; part 2 copies its record 72 model units ahead (6.1). part 1: the instructor at its fixed start with W2 0x0100 set; part 2: W2 0x0100 clear until
 * the player's indicated speed is not zero. */
void training_start(trainer_t *t, int mode_byte, const aircraft_t *player);

/* The player's pass (0x270A0, part 2 only; the sound cue of its first job is game.c's strain_pass): the RETURN re-position (8.6). Acts in a paused pass too. */
void training_player_pass(trainer_t *t, aircraft_t *player);

/* The instructor's pass (record 4, 8.3 and 8.4), after the player's own step: the sequencer, +0x4C decremented, the state routine, the flight step. Does nothing
 * while t->paused. Without an installed flight environment the instructor's state machine still runs and only the flight step is skipped. */
void training_step(trainer_t *t, aircraft_t *player);

/* TRAINING_RUNNING, or TRAINING_LOST once when part 2's instructor has lost W0 bit 0x0040 (8.9: the end request; the caller starts the outcome screen). */
int training_result(trainer_t *t);

/* Keys (8.6). RETURN: part 1 asks the sequencer to re-place the instructor and skip a lesson; part 2 re-positions the player when V:4A852 is negative, else posts
 * the "not ready" notice. The fire key (SPACE): part 2, V:4A852 negative and the player not ejecting: the instructor's trigger. S: part 2, toggles the instructor's
 * trail bit (W2 0x1000); no effect in part 1. They only set request bits; the passes above act on them. */
int training_key_return(trainer_t *t, aircraft_t *player);   /* 1 when the RETURN set the request (the key beeps then) */
void training_key_fire(trainer_t *t, const aircraft_t *player);
void training_key_smoke(trainer_t *t);

/* Clears the posts and requests the caller has consumed. */
void training_clear_outputs(trainer_t *t);

/* The state routine 0x30A7E for the instructor as it stands (8.5): exposed for tests and the differential test. Returns nothing; it writes the key byte, the
 * counter, the phase flags, the recentre bit, the state byte and +0x64 of t->inst. */
void training_ai(trainer_t *t);

#endif
