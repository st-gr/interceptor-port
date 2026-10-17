/* src/core/cockpit.h - the cockpit state the original keeps in game variables: the frame counter n, the warning tick
 * generator, the warning arbiter with its code word and latch, the HUD switch, and the raw cockpit block published
 * in the scene (docs/re/06-cockpit-hud.md, "Warning line", "Draw schedule"; spec section 10: the core owns everything the
 * original keeps in game state, the frontend keeps only draw caches and redraw timers).
 *
 * One pass of the original's main loop is, on the flight path (note 04 "Draw order of one frame", steps 1, 3 and 10: the generator
 * runs at the start of the frame routine, the world update follows, the arbiter runs after it; note 06: the increment of n at 0x114B6
 * is the last thing of a flight-path pass, so n is settled when the draws see it):
 *   cockpit_generator   at the start of the frame routine, on every pass (0x13C94)
 *   flight_step         the world update (note 05)
 *   cockpit_arbiter     only while the flight runs (0x13D4C); clears the tick word at its end
 *   (the frontend draws with n as published in the scene)
 *   cockpit_end_pass    n = V:4A992 advances unless the pause byte is set
 * game.c calls them in that order. The shared variables of the flight state are used where they live: the code word V:4AB98
 * is f->v4ab98, the latch V:4A984 bit 0 and bit 7 and bit 2 are bits of f->v4a984, the event word V:4AC08 is f->v4ac08.
 *
 * Core rules: C99, fixed-width integers, no stdio, no floating point, no wall time. */
#ifndef INTERCEPTOR_COCKPIT_H
#define INTERCEPTOR_COCKPIT_H
#include <stdint.h>
#include "aircraft.h"
#include "scene.h"

#define COCKPIT_WARN_ENTRIES 54
#define COCKPIT_WARN_SIZE 28

/* Code word bits (V:4AB98, note 06 "Code word"). */
#define COCKPIT_CODE_ALERT 0x8000u   /* bit 15: alert, blinks with its partner or the blank entry */
#define COCKPIT_CODE_TIMED 0x4000u   /* bit 14: timed (information when alone; with bit 15 the blink partner is the blank entry) */
#define COCKPIT_CODE_FAST  0x1000u   /* bit 12: fast countdown and fast blink */

typedef struct {
    uint8_t warn[COCKPIT_WARN_ENTRIES][COCKPIT_WARN_SIZE];   /* the table at 0x41FF0 (flag byte, 26 characters, sound flag) */
    uint8_t have_warn;
    uint32_t n;              /* V:4A992: flight passes since the mission start */
    uint32_t n_draw;         /* the value of n the draws of the last pass saw (n before the increment) */
    uint32_t pass;           /* passes of the frame routine that reached the draws, paused ones included (the draw schedule precedes the pause test, note 06
                              * "Draw schedule"); never reset, so the frontend can tell a new pass from a repeat of the scene */
    uint8_t paused;          /* V:4A866 as the draws of the last pass saw it */
    uint8_t gen;             /* V:4A948: tick generator counter */
    uint8_t tick;            /* V:4A946: tick word of this pass (bit 1 slow, bit 2 fast); the arbiter clears it */
    uint8_t last_idx;        /* V:4AB9A: the entry index last installed */
    uint8_t shown;           /* V:4AB96 (low byte): the entry the line shows */
    int8_t  dcount;          /* V:4A94A: duration counter */
    uint8_t dur;             /* the installed entry's duration D (flag byte bits 5 to 0); an alert with D = 0 never expires */
    uint8_t text_shown;      /* V:4A986 bit 5: the entry's own text was the last shown by a tick */
    uint8_t snd_a, snd_b;    /* V:4A91A (flag byte of the installed entry), V:4A91B (its sound flag) */
    uint8_t hud_on;          /* V:4A859: the H key toggles it; the flag-block clear of a reset turns it off and the state 0x12EDA stores 1 when its countdown is over (note 08 6.1 item 1 and the state table; note 06 "HUD"):
                              * the screen module's out_hud at that state (SCR_WAIT's end) sets it, game.c copies it here. No other writer */
    uint8_t threat;          /* V:4A926 threat bits. written by the radar walk (the radar module's threat bits); game.c copies them here */
    uint8_t strip_blank;     /* a view switch to another entity is pending: V:4A891 is clear for the next pass */
    uint8_t strip_off;       /* V:4A891 clear during the last pass: the external text strip was skipped */
    uint8_t stock;           /* E+0x5F weapon stock nibbles (high Sidewinder, low AMRAAM): 0x24 and 500 rounds are what the refuel and re-arm routine 0x1F14A writes
                              * (note 05 section 7 and 17.17 rule 4, the start of a flight); that the first flight of a mission starts from that state is inferred
                              * from the restart, not measured, until the weapon slice (milestone 5) owns the stock */
    uint16_t rounds;         /* E+0x60 gun rounds */
    uint8_t redraw;          /* V:4A8EF, the scope's redraw counter R: set to 3 whenever the helper 0x1EFF4 runs (cockpit_redraw_request: note 07 12.1 lists the callers), one less after each pass that runs the panel block (0x113E8). The first pass of a flight has NO writer: R holds what the previous screens left (0xFF after boot, a signed byte; the first pass of the demo, training part 2 and the qualification holds it; training part 1 and the free flight count it from 3 to 0 in the gate-1 passes before the flight, 07 12.1) */
    uint8_t panel;           /* V:4A8EE, the panel picture's counter: 3 from the helper, from every change of the head code (0x1E6FE), from the seat's view switch in an external view (0x1EC54) and from the flow states (the dispatch 0x120E0, 0x1250C, 0x12730, 0x12BA0: screen.c out_panel; its value at the first free-flight pass is 0 [R, 07 12.1, cockpit.c]; its value at the first pass of the demo, the trainings and the qualification is 0 [V, 07 12.1]; the missions' is not captured); the panel draw 0x35240 counts it down. It gates the panel block in an external view (07 12.1) */
    uint8_t redraw_now;      /* the value of R the draws of the last pass saw (the radar walk of note 07 12.2 and the frontend's panel, HUD and scope read it) */
    uint8_t zoom;            /* V:4AAFA: the third zoom word (the depth row scale and the radar magnification digit), 0x80 at the mission start, 0x40 and 0x20 by the `]` key; 0 reads as 0x80 */
} cockpit_t;

/* Clears the state and loads the warning table (54 x 28 bytes) when given (NULL keeps the previous table). */
void cockpit_init(cockpit_t *c, const uint8_t *warn_table);

/* Mission start (0x12EC0): n = 0, H key on, arbiter state cleared, and the port's tick-phase pin: the generator counter is
 * seeded so that after the first pass it equals 8 - ((n0 + 3) mod 8) with n0 = 0, that is 5. The crash restart keeps n and
 * does not reseed (it is simply not a mission start). */
void cockpit_mission_start(cockpit_t *c);

/* The helper 0x1EFF4 (its V:4A8EF and V:4A8EE stores): R = 3 and the panel counter = 3 (the port's other twelve counters: game.c redraw_helper). Callers (note 07 12.1): the frame routine every 32nd pass (n & 0x1F = 0x10, camera mode 0), the resume from the pause screen (12.14 N7), the view
 * routine's window-row change (0x14388, camera mode 6: no flight pass reaches it), the head and window helper 0x1E7B2 (the view switch 0x142BC, the seat's set-up 0x14322, the viewed record's loss
 * 0x143EA, the head keys' tail, the seat/tracking-camera switch 0x1EC94) and the end of the jolt routine 0x29B0A. */
void cockpit_redraw_request(cockpit_t *c);

/* R for this pass (V:4A8EF): the value the pass's draws see is published as redraw_now, then, when the pass runs the panel block (panel_block: the cockpit view, or an external view while V:4A8EE is non-zero),
 * R counts down by one while it is positive (0x113E8). Called once per pass that reaches the draws; the paused pass (gate 0) does not call it. */
void cockpit_redraw_pass(cockpit_t *c, int panel_block);

/* One pass of the generator 0x13C94: decrement, reload to 8 at 0, tick word from the counter after the decrement
 * (8: 0x86, 4: 0x06, 6 and 2: 0x04, else 0). Runs on every pass, flight or not. */
void cockpit_generator(cockpit_t *c);

/* The arbiter 0x13D4C. paused is the pause byte V:4A866 (a nonzero value turns an alert's partner tick into a text tick). */
void cockpit_arbiter(cockpit_t *c, aircraft_t *ac, int paused);

/* n advances unless paused; n_draw is the value the draws of the last unpaused pass saw; pass advances on every pass, paused[] records the pause byte. */
void cockpit_end_pass(cockpit_t *c, int paused);

/* The post routines 0x13D00 and 0x29BC8: the code word is replaced and latched (V:4A984 bit 0); the blank code 0x4800 unlatches. */
void cockpit_post(cockpit_t *c, aircraft_t *ac, uint16_t code);

/* The zoom keys `]` (closer = 1) and `[`: V:4AAFA halves to 0x20 or doubles to 0x80. There is no pause entry action on n: no handler clears n & 7 on a pause (note 06 "View kinds, settled" item 8). */
void cockpit_zoom_key(cockpit_t *c, int closer);
/* 0x1F060: the zoom word back to 0x80 (every head key, every view switch). */
void cockpit_zoom_reset(cockpit_t *c);

/* The H key. */
void cockpit_toggle_hud(cockpit_t *c);

/* The view switches to another entity (0x27D06): the external strip is blank for one frame (V:4A891). */
void cockpit_entity_switch(cockpit_t *c);

/* Fills the raw cockpit block (cp_valid and everything after it in game_hud_t) from the aircraft and this state. */
void cockpit_fill_hud(const cockpit_t *c, const aircraft_t *ac, game_hud_t *hud);

#endif
