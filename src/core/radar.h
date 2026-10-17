/* src/core/radar.h - the radar list, the threat bits, target designation and the HUD target data (docs/re/07-ai-and-weapons.md section 12, "note 07"; section numbers
 * below are that note's; note 06 is "Target symbols" and "Radar scope").
 *
 * What lives here
 *   - the list builder (12.2) and the scope walk (12.3): the 16-byte list of the records relative to the viewed one, the window and detection tests, the detected bit
 *     (B20 bit 6), the mission-1 contact counter V:4A927, the threat bits V:4A925, the blips (pens, the ten-blip limit, the designated blip's blink) and the erase list of the page;
 *   - the threat bits and the cues (12.4): V:4A926, the cue bits raised in the player's V:4AC0C, the hold-off V:4A93F;
 *   - designation (12.5, the T key) and the range of the designated target E+0x4A (12.6, the player's 0x2835A);
 *   - the HUD target data per frame (12.7, 12.8): the marker slew, the lock state (V:4A96C, V:4AC08 bits 0x4000 and 0x0200), the fire prompt, the pipper history, the range dot,
 *     the range number, and the message-line target data (pages, name class, digits);
 *   - the keys R (radar range nibble), T (designation request) and Y (page).
 *
 * The record state is the entity module's (entities_t: e->designated is V:4AA78 as a record index, negative none; the records' W0/B20/W2; the player's range is e->rec[0].range,
 * its call counter the high nibble of the player's f.repeat (+0x39), B4 bit 0 its f.b4). Everything else is radar_t. No globals, no floating point.
 *
 * WIRING (what game.c does; nothing in this file calls game.c):
 *   mission start    radar_reset(&g->rd)                                   (with the entity reset: V:4A93E = 0xFF, the counters cleared)
 *   each unpaused pass, after the player's step (and before cockpit_arbiter, so the threat bits reach the arbiter of the same pass):
 *                    (the player's range 0x2835A, 12.6, is entities_update_player_range, run by entities_step)
 *                    radar_in_t in = { .e = &g->ent, .n = g->ck.n, .cockpit = !ext_view, .paused = 0, .zoom = ck zoom (0x80 default), .hud_row = V:4A990 (the jolt row, 0),
 *                                      .view_off = V:4AA40 (the HUD view offset, 0 in the 3D cockpit view), .cam_y = V:4AB30 (minus the viewed record's alt8 as the camera has it),
 *                                      .redraw = V:4A8EF (0), .rot = NULL (derived from the viewed record's matrix), .cam = the 3x3 Q8 view matrix V:4AC90 (fx_mat_t) of the
 *                                      frame, .tgt_x/.tgt_y = the designated target's projected point V:4A9EE/F0 of this frame (-1 when it was not drawn) };
 *                    radar_pass(&g->rd, &in);                               (12.2 to 12.5: build, walk, threat, cues, the T request)
 *                    g->ck.threat = g->rd.threat_prev;                      (V:4A926, the arbiter's input: "after the row V:4A926 = the new bits")
 *                    radar_hud_pass(&g->rd, &in);                           (12.7, 12.8, per frame in the cockpit view)
 *   paused pass      radar_hud_pass is NOT called (the range number then reads the stored closing delta; the pipper and the prompt stand still): call
 *                    radar_hud_pass with in.paused = 1 if the draws of a paused pass should still refresh the fields (the range number then does not consume B4 bit 0)
 *   once per mission-clock tick (V:4A93E, not while paused)   radar_clock_tick(&g->rd)
 *   keys             T: radar_key_target(&g->rd)  (also the click 0x37C4A, the caller's)       Y: radar_key_page(&g->rd)       R: radar_key_range(&g->ent)
 *   the stale-lock drop 0x26F28 and the draw handler's drop 0x284F8 are the entity module's / the step's: after either, call radar_lock_dropped(&g->rd) (V:4A920 = 0, V:4A9F2 = -1)
 *   scene            radar_fill_hud(&g->rd, &g->ent, &out->hud) after cockpit_fill_hud (it fills the target block, cp_threat, the radar list, the blips and the line data below)
 *
 * Verified against the original in the rig (tests/rig/test_radar_diff.c, make -C tools/m68k-rig test-radar; 0 differences over 210,000 random cases in seven parts): the list builder 0x1FEE6,
 * the walk 0x35E5E (records' W0/B20, threat bits, cues, hold-off, mission-1 counter, flag words, blips with the page words), the designation (in the walk), the range 0x2835A, the lock state
 * 0x38884, the sight 0x385F4 with the pipper, the range-number state, and the state of the message-line driver 0x36DB2 (not its text). The rig corrected the note in places (tagged "RIG FINDING" in
 * radar.c): the drawn bit is the scaled blip window's; rows 6 and 7 of the lock table clear 0x0200; any selection but the gun takes the missile branch of the sight; the prompt condition is
 * geometric; the number needs the pipper x above 0 and a range of at most 0x7F00 (signed); the driver's single posted-message branch and its rebuild/redraw order.
 * Settled: the order of lock state, sight and slew inside a frame (8441ce0) and the hold-off test (on the result of the decrement; the rig's line part). Open: a rejected pipper projection leaves a stale
 * register in V:4A9F8 (x is 0; the port stores 0; the prompt rule of note 06 reads V:4A9F8 only with x above 0, so it is unobservable, C); the erase-list page word past the second page (note 07 Open 12 #2); the message text (names, labels) is the asset's, the radar gives the class, colour, page and digits.
 *
 * Tags: R = read from the code, V = verified in the rig by the note, NOTE-GAP = open. */
#ifndef INTERCEPTOR_RADAR_H
#define INTERCEPTOR_RADAR_H
#include <stdint.h>
#include "entities.h"
#include "fx.h"
#include "scene.h"

#define RADAR_LIST_MAX 16          /* records 0 to 15 (the viewed one is left out) */
#define RADAR_BLIPS 10             /* blips stored per page (12.3) */
#define RADAR_ERASE 11             /* entries the erase reads: ten and the eleventh, read past the page (12.3 step 1) */
#define RADAR_EMEM 42              /* words of the erase lists: page 0 at 0x537C8 (0 to 19), page 1 at 0x537F0 (20 to 39), the pair at 0x53818 (40, 41) */

/* A list entry (12.2): x and z in the units of the list (the relative position divided by 256, z ahead), the flag word (record index in the high byte, bit 4 "plot me", bit 5
 * "drawn by the scope this pass"). */
typedef struct { int32_t x, z; uint16_t flag; } radar_entry_t;
#define RADAR_PLOT 0x0010u
#define RADAR_DRAWN 0x0020u

/* A blip on the scope: the screen point (the row includes V:4A990), two pixels wide when `wide`, the pen, and the record. */
typedef struct { int16_t x, y; uint8_t wide, pen, rec; } radar_blip_t;

/* The message-line target data (12.8): the facts the line is made of. The strings (the type names, the labels, "no signal") are the assets'; the radar gives the class that
 * selects the name, the colour bits (the top two bits of V:4A91A), the page and the digits. mode: 0 the posted message shows (the cockpit's), 1 the blank message, 2 target data. */
typedef struct {
    uint8_t mode;
    uint8_t redraw;        /* the line is drawn again this pass (a fresh build or the old text while V:4A8F4 lasts) */
    uint8_t cls;           /* the target's class byte +0x62 (selects the name) */
    uint8_t detected;      /* B20 bit 6: clear selects the "no signal" text and colour 0 */
    uint8_t colour;        /* 0, 1 or 2: the colour bits (0 undetected, 1 = bits 01, 2 = bits 10) */
    uint8_t page;          /* 1 altitude, 2 heading, 3 speed; anything else: no label */
    uint8_t col;           /* the first digit's character column (18, 19, 18) */
    char digits[8];        /* the digits as characters, leading zeros blanked (page 1 and 3) or kept (page 2); NUL terminated */
} radar_line_t;

/* The per-frame HUD results. */
typedef struct {
    uint8_t pip_pixel;     /* draw the pipper pixel at (pipper_x, pipper_y) */
    uint8_t circle;        /* draw the lock circle at (circle_x, circle_y) (pen 10) */
    int16_t circle_x, circle_y;
    int16_t dot;           /* the range dot's position index k (0 to 75), -1 none */
    uint8_t number;        /* draw the range number */
    char number_text[8];   /* sign and four digits, NUL terminated */
    uint8_t prompt;        /* the fire prompt to draw: for a missile V:4A96C after the update (1 and 2 draw the missile form); for the gun gun_prompt */
    uint8_t gun_prompt;    /* the gun branch of the sight called the prompt routine (0x386A0) in this pass */
    uint8_t marker;        /* draw the marker shape at (marker_x, marker_y): 1 the diamond, 2 the ring (lock box) */
} radar_hud_t;

/* The pass inputs (see WIRING). */
typedef struct {
    entities_t *e;
    uint32_t n;            /* V:4A992 */
    uint8_t cockpit;       /* V:4A83D = 0 */
    uint8_t paused;        /* V:4A866 */
    uint8_t hud_off;       /* V:4A859 is zero (the H key): the HUD draw 0x37D78 returns before the sight 0x385F4 (note 07 12.7): no prompt state, no cue, no pipper history, no circle, dot, number or prompt. The lock state 0x38884 runs before that test. */
    uint8_t zoom;          /* V:4AAFA (0 reads as 0x80) */
    int16_t hud_row;       /* V:4A990 */
    int16_t view_off;      /* V:4AA40 */
    int32_t cam_y;         /* V:4AB30 */
    int16_t redraw;        /* V:4A8EF */
    uint8_t no_panel;      /* the panel block does not run in this pass (an external view with V:4A8EE = 0, or the seat view with it 0): no scope draw and walk, no HUD driver (note 07 12.1). 0 for the cockpit view */
    const int16_t *rot;    /* V:4ACB4: nine words, 0x100 = 1.0, rows x, y, z; NULL: derived from the viewed record's matrix +0x92 (columns lateral, up, forward) shifted right by 6 */
    const fx_mat_t *cam;   /* V:4AC90 (Q8) for the pipper's projection; NULL: the identity */
    int16_t tgt_x, tgt_y;  /* V:4A9EE/F0 */
} radar_in_t;

typedef struct {
    radar_entry_t list[RADAR_LIST_MAX + 1];   /* terminated by nlist */
    int nlist;
    uint8_t walk_done;          /* a walk ran in the last radar_pass */
    uint8_t threat;             /* V:4A925: the bits of the last walk */
    uint8_t threat_prev;        /* V:4A926: the bits the cues were compared with (the arbiter's input) */
    uint8_t mission1;           /* V:4A927 */
    int8_t holdoff;             /* V:4A93F */
    uint8_t req_target;         /* V:4A871 */
    uint8_t frame;              /* V:4A93B */
    uint8_t cur_page;           /* V:4A724: the erase page the next walk uses (0 or 1) */
    radar_blip_t blip[2][RADAR_BLIPS];
    uint8_t nblip[2];
    radar_blip_t erase[RADAR_ERASE];   /* what the walk's erase reads from its page (up to eleven entries, see emem): the frontend erases them before drawing blip[last page] */
    uint8_t nerase;
    /* The erase lists as the original keeps them in memory (12.3): per entry two words (x, with bit 15 for a two-pixel blip; y), the list ended by a word 0xFFFF written after the last stored
     * entry. A page of ten entries has no room for it: the terminator lands on the first word of the next page (page 0) or on the word at 0x53818 (page 1), and the erase reads an eleventh entry
     * from there. So a walk of ten blips on page 0 overwrites the first word of page 1's list (page 1's old first blip is then not erased, the original's picture keeps it), and page 0's
     * erase may erase the pixel pair that page 1's first entry holds. NOTE-GAP: what lives at 0x53818 and 0x5381A (nothing reads them but this erase, R): the port keeps 0xFFFF and 0. blip[] below is
     * the set each page drew, which is on the screen until its page erases it. */
    uint16_t emem[RADAR_EMEM];
    uint8_t last_page;          /* the page the last walk wrote */
    int16_t listpos;            /* V:4A920 */
    /* the HUD block */
    int16_t mx, my;             /* V:4A9FA/FC the slewing marker (x = -1: invalid) */
    int16_t tpx, tpy;           /* V:4A9F2/F4 the previous frame's target point */
    int16_t px, py;             /* V:4A9F6/F8 the pipper */
    int32_t hist[2][3];         /* V:4A7CE (newest) and V:4A7DA: the aim points of the last two calls */
    int16_t last_range, closing;/* V:4ABFC, V:4ABFE */
    uint8_t prompt;             /* V:4A96C */
    uint16_t page;              /* V:4AA7C (bit 15: rebuild) */
    int8_t countdown;           /* V:4A93E */
    int8_t line_redraw;         /* V:4A8F4 */
    radar_line_t line;
    radar_hud_t hud;
} radar_t;

void radar_reset(radar_t *r);
void radar_pass(radar_t *r, const radar_in_t *in);
void radar_hud_pass(radar_t *r, const radar_in_t *in);
void radar_clock_tick(radar_t *r);
void radar_key_target(radar_t *r);
void radar_key_page(radar_t *r);
void radar_key_range(entities_t *e);
void radar_lock_dropped(radar_t *r);
void radar_fill_hud(const radar_t *r, const entities_t *e, game_hud_t *hud);

/* exposed for tests: the list builder alone (12.2) and the designation walk (12.5) */
void radar_build(radar_t *r, entities_t *e, const int16_t *rot);
void radar_designate(radar_t *r, entities_t *e);
void radar_lock_state(radar_t *r, const radar_in_t *in);   /* 0x38884, 12.7 */
void radar_slew(radar_t *r, const radar_in_t *in);         /* 0x38D8C, note 06 */
void radar_sight(radar_t *r, const radar_in_t *in);        /* 0x385F4 and the pipper tail 0x3878E, 12.7 */
void radar_line_driver(radar_t *r, const radar_in_t *in);  /* 0x36DB2, 12.8 */

#endif
