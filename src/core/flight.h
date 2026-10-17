/* src/core/flight.h - the player aircraft's flight model, reproduced from
 * docs/re/05-flight-model.md ("note 05"; section numbers below are that note's).
 *
 * One call of flight_step is one step of the original's world update for the player (the rig
 * convention of section 1: key byte in, record words out). All arithmetic is the original's:
 * 16-bit words with wrap, arithmetic right shifts, 32-bit products, no floating point. The state
 * lives in aircraft_t.f (aircraft.h) in the note's units, so that tests can compare it word by word
 * with the original code in the rig (tests/rig/test_flight_diff.c).
 *
 * Tables the step reads come from the extractor (assets/tables/flight_*.bin, tools/extract/
 * extract/flight.py) through flight_tables_load; the model vertices of the carrier and of the
 * player's aircraft (note 05 "Extractor needs") and the landmark slots (section 11b) are supplied
 * by the caller in a flight_env_t installed with flight_set_env. The step is a no-op until an
 * environment with tables is installed.
 *
 * Core rules: C99, fixed-width integers, no stdio, no floating point, no wall time. */
#ifndef INTERCEPTOR_FLIGHT_H
#define INTERCEPTOR_FLIGHT_H
#include <stddef.h>
#include <stdint.h>
#include "aircraft.h"
#include "game.h"
#include "scene.h"

/* The port's fixed simulation rate in steps per second (the original runs one step per displayed
 * frame, so its rate depends on the scene). Controller's ruling from note 01b (round 3): the
 * flight-only mean over the four flight scenes is 7.97 steps per second (230 one-second samples),
 * rounded to 8; the 5.60 of the parked aircraft and the crash freeze are not flight; the 15 Hz
 * limiter ceiling of note 04 is never reached. Note 05 section 14. The core does not read the
 * clock: a frontend calls flight_step (through game_step) this many times per second. */
#define FLIGHT_STEP_HZ 8

typedef enum { FLIGHT_FA18 = 0, FLIGHT_F16 = 1 } flight_type;
typedef enum { FLIGHT_START_CARRIER = 0, FLIGHT_START_RUNWAY = 1 } flight_start;

/* Key byte of section 2 ("bits of +0x65"). */
enum {
    FLIGHT_KEY_THROTTLE_UP = 0x01, FLIGHT_KEY_THROTTLE_DOWN = 0x02,   /* bits 0-1: 1 up, 2 down */
    FLIGHT_KEY_ROLL_RIGHT = 0x04, FLIGHT_KEY_ROLL_LEFT = 0x08,
    FLIGHT_KEY_STICK_FORWARD = 0x10, FLIGHT_KEY_STICK_BACK = 0x20,      /* forward = nose down */
    FLIGHT_KEY_RUDDER_RIGHT = 0x40, FLIGHT_KEY_RUDDER_LEFT = 0x80
};

/* Inputs of one step (section 2). keys is written to +0x65 before the step. preset, when non-zero, is stored into V:4A928 before the step as the
 * F-key handler 0x1EA64 does (note 05 17.4): 12 * n for the function key F<n> (121 for F10, "F1 again while at 12" is the caller's business), -1 for
 * idle; the handler also sets the pointer words P = Q = min(8 * preset, 960) (a negative preset counts as 0). The four event flags are the key handlers of
 * section 2: gear toggles bit 7 of +0x7C (refused while the wheels are down), hook toggles W2 bit 0x8000 (F/A-18 only), brake toggles W0 bit 0x0800 (and
 * clears the pointer throttle: P = Q = 0, preset 0, handler 0x1E32E), eject starts the ejection cycle.
 *
 * The pointer throttle (V:4A890 = 1, set at every start; note 05 17.4). P (V:4A830) and Q (V:4A834) live in flight_state_t; the step applies rules 1 to 3 of
 * 17.4 (follow, fire) itself. Rule 0, the mouse, is an input:
 *   pointer_dy   the change of the mouse's vertical counter summed over the vertical blanks of this step (the original adds -dy to P at every blank, 50 Hz,
 *                with the per-blank change wrapped to -128..127 and P clamped to -960..960). Pushing the mouse away from the user lowers the counter, so a
 *                frontend passes SDL's yrel (positive = toward the user) as it is. CADENCE (PORT CHOICE): the original applies the clamp at every blank, the
 *                port once per step with the sum; the two differ only when P would pass a limit within a step and come back in the same step. The sum is
 *                applied at the start of the step, before the rules (the interrupt handler runs between steps).
 *   pointer_set  when non-zero, P = clamp(pointer_value, -960, 960) at the start of the step instead (PORT CHOICE: the absolute throttle axis of a joystick or a
 *                lever replaces the pointer; the frontend or game.c maps the axis to the height: 8 per throttle unit, 960 at full travel, below 0 idle). It is applied
 *                before pointer_dy. */
typedef struct {
    uint8_t keys;
    int8_t  preset;
    uint8_t gear_toggle, hook_toggle, brake_toggle, eject;
    int16_t pointer_dy;
    uint8_t pointer_set;
    int16_t pointer_value;
} flight_inputs_t;

/* Tables (loaded once; sizes are checked on load). */
/* The two image spans the table lookups read by address (note 05 17.9): the words around the pitch scale (0x3249A..0x325B8) and the words around the sine and
 * inverse-sine tables and the composition's lookup range (0x3AE92..0x49A12, 17.18 a), as big-endian image bytes. Every sine, inverse-sine and pitch-scale lookup of the module is flight_image_word on these. */
#define FLIGHT_SPAN_A_BASE 0x3249Au
#define FLIGHT_SPAN_A_SIZE 286
#define FLIGHT_SPAN_B_BASE 0x3AE92u
#define FLIGHT_SPAN_B_SIZE 60288
#define FLIGHT_ADDR_PITCH_SCALE 0x3259Au   /* 15 words */
#define FLIGHT_ADDR_ASIN1 0x42E92u         /* three inverse-sine tables: 259, 552, 256 words */
#define FLIGHT_ADDR_SINE 0x436E8u          /* 901 words, Q14 quarter wave */
#define FLIGHT_TERRAIN_WORDS (32 * 32 + 3724 / 2)
#define FLIGHT_PAD_QUAD_WORDS 40
#define FLIGHT_CARRIER_BASE_L1 0x3E0F8u
#define FLIGHT_CARRIER_STRUCT_BASE 0x3E6AEu
#define FLIGHT_SHIP2_STRUCT_BASE 0x3F2B6u
typedef struct {
    int16_t stick_player[62], stick_ai[64], gust[31], sink[31];
    uint8_t span_a[FLIGHT_SPAN_A_SIZE], span_b[FLIGHT_SPAN_B_SIZE];
    int16_t len[257];
    int16_t terrain[FLIGHT_TERRAIN_WORDS];
    int16_t dir[32];
    int16_t pad_entry[5][8];
    int16_t pad_quads[4][FLIGHT_PAD_QUAD_WORDS];
    uint8_t pad_nquad_words[4];
    /* ship lists and structures, big-endian raw: [0] carrier (class 0x20), [1] the other ship */
    uint8_t ship_l1[2][124], ship_l2[2][44], ship_st[2][684];
} flight_tables_t;

/* Reads tables/flight_*.bin through the loader; returns 0, or -1 with a message naming the file
 * whose size is wrong or which is missing. */
int flight_tables_load(flight_tables_t *t, const game_loader_t *loader, char *err, size_t errlen);

/* Landmark collision slots (section 11b), as the display-list builder fills them. A node is one
 * inner box with its planes; a slot is one landmark instance. */
#define FLIGHT_LM_SLOTS 11
#define FLIGHT_LM_NODES 4
#define FLIGHT_LM_PLANES 8
typedef struct {
    int16_t box[5];                   /* maximum height (y >> 8), x min, x max, z min, z max (fine units, relative to the cell) */
    uint8_t nplanes;
    int16_t ref[FLIGHT_LM_PLANES][3]; /* reference point of each plane */
    int16_t nrm[FLIGHT_LM_PLANES][3]; /* normal of each plane */
} flight_lm_node_t;
typedef struct {
    uint8_t cell_x, cell_z;           /* the landmark's cell bytes */
    uint8_t ordinal;                  /* the slot's ordinal 0..10 in the queue C walk (a slot with a negative pointer keeps its number but is not listed; the gun's tracer test walks by ordinal, note 07 8.2) */
    int16_t box[5];                   /* outer box */
    uint8_t nnodes;
    flight_lm_node_t node[FLIGHT_LM_NODES];
} flight_lm_slot_t;

/* The landmark z quirk (section 11b): the high word of the address of the model's collision data,
 * divided by 4, leaks into the player's fine z. 48 on the reference machine (Kickstart 1.2, program
 * in slow RAM from 0xC00000: every entry in 0xC00000-0xC3FFFF gives 48); the Musashi rig image at
 * 0x10000 gives 1. The port reproduces the reference machine; tests pass 1 for the rig. */
#define FLIGHT_LANDMARK_Z 48

/* A ship record the deck, hull and arrestor tests read (section 9): the carrier is class 0x20 in
 * the examples. vtx holds the model's near-level vertices rotated by the ship's matrix (slot k at
 * record +0xA4 + 6k, (M . v_k) >> 8 with M = matrix B >> 6; the first 58 slots). */
#define FLIGHT_SHIP_SLOTS 58
typedef struct {
    uint8_t  present;
    uint16_t w0;               /* +0x00 */
    uint16_t pitch, heading, roll;   /* +0x66 +0x68 +0x6A: the start copies them (flight_ship_build sets them) */
    uint8_t  cls, group, l7d;  /* +0x62, +0x5E, +0x7D */
    int32_t  pos[3];           /* +0x14 +0x18 +0x1C */
    int16_t  fine_x, fine_z;   /* +0x0C, +0x0E */
    int16_t  alt8;             /* +0x10 */
    int16_t  mat_b[9];         /* +0x92 */
    int16_t  vtx[FLIGHT_SHIP_SLOTS][3];
} flight_ship_t;

/* A runway start block of the table at 0x47ADE (note 05 17.14; note 08 5.6 for the selection): the eight words, as scenario_start_t. When present it replaces the
 * constants of block 0 in the runway start (the start position is cell * 2^24 + dir(n) * 1024 + o * 1024 + f * 16 for x and z alike). */
typedef struct {
    uint8_t present; int16_t cell_x, cell_z, n, ox, oz, fx, fz, degrees;
    uint8_t direct; int32_t x, z;   /* direct: the position x, z is given as it is (the out-of-table starts of the location keys 7 to 0, note 08 5.6) and not computed from the words */
} flight_block_t;

typedef struct {
    const flight_tables_t *tab;
    flight_block_t block;               /* the runway start block the location key chose (V:4A900); not present: block 0 */
    flight_ship_t ship;                 /* the carrier (or other ship) of the scene */
    int landmark_z;                     /* the z quirk: FLIGHT_LANDMARK_Z, or 1 for the rig */
    int n_landmarks;                    /* slots in use, in queue order */
    flight_lm_slot_t landmark[FLIGHT_LM_SLOTS];
    int no_rearm;                       /* tests: skip the landing-complete refuel (the rig patches 0x1F11E out) */
    int view_cell_by_camera;            /* the game sets it: V:4AA04/06 is written by the camera set-up AFTER the update (entities_update_view_cell), so the step must not write it and the records 1 to 15 read the camera's cell of the PREVIOUS pass (note 04a 8a); 0 for the rig harnesses, which have no frame and take the step's cockpit value */
    int no_liftoff_hook;                /* tests: skip the lift-off hook of a non-player record (note 07 4.7; the rig patches 0x1F0E2 out) */
    /* milestone 5 (note 07 10.2): the contact scan against the other records (weapons_contact): phase 0 visits the records after the mover up to 13 (before the ship record of this
     * environment, the carrier 14), phase 1 the record 15 after it; it returns 0x40 on an aircraft contact. NULL: only the ship and the landmarks are tested */
    int (*contact)(void *user, flight_state_t *f, const int32_t prev[3], int phase);
    void *contact_user;
    /* milestone 5 (note 07 13.1): the followed record V:54E72 takes the record whose state is f: the player's skid or crash trail (0x17022/0x17024) and the result handler of a non-player
     * record (0x2A954/0x2A95E). NULL: nothing follows. */
    void (*follow)(void *user, const flight_state_t *f);
    void *follow_user;
    int no_sound_system;                /* tests: the sound system is not running (V:4AC12 bit 0 clear, as in the rig): the first contact then leaves the
                                         * notice 0xFA / V:4A84F = 2 (note 05 17.12); in play the bump is a sound and no notice is shown */
} flight_env_t;

const flight_env_t *flight_env(void);           /* the installed environment (NULL: none) */
/* The rearm routine 0x1F11E (flight.c): the post 0x4005 with the latch, the fuel 0x61A800, and ev_rearm for the stores. The landing and the port's W key call it. */
void flight_rearm(flight_state_t *f);
void flight_set_env(const flight_env_t *env);   /* the pointer must stay valid; NULL removes it */

/* Rotates the vertices of a ship model (model units, FLIGHT_SHIP_SLOTS x 3) by the matrix of a ship at
 * the given heading, as the draw pass does, and fills ship->mat_b and ship->vtx (section 9, "Carrier
 * set-up"). The angles are the ship's pitch, heading and roll words. */
void flight_ship_build(flight_ship_t *ship, const int16_t verts[][3], int nverts,
                       uint16_t pitch, uint16_t heading, uint16_t roll);

/* The player's start (sections 9, 15 and 17.14). FLIGHT_START_RUNWAY is block 0 of the table at 0x47ADE (runway of airfield 1: cell (16, 16), n = 6, offsets
 * (1184, 2528), fine (7570, -14257), 28 degrees; the position of the worked example E1). FLIGHT_START_CARRIER is the carrier start 0x1D730: the player's x and z are
 * the ship record's position plus ((11 * M0 + 104 * M2) >> 4, (11 * M6 + 104 * M8) >> 4) with M the ship's matrix B words, y = (D << 8) + 0x708 with D = 112 the
 * deck height of the carrier model, B4 = 0xC8, the attitude copied from the ship record (so free flight, where the carrier is record 14 at (295174144, 274202624), heading 0, for both start kinds, starts at
 * +11,264 / +106,496 and the demonstration, heading 28600, at +15,900 / +105,900). Without an active ship in the installed environment the start is made as the
 * runway block, as the original does when the record is inactive. */
void flight_init(aircraft_t *ac, flight_type type, flight_start start);
/* The restart of the crash flow (0x1D538, note 05 17.17 rule 4; the full write and survive lists are above flight_restart in flight.c). Writes W0 = 0x11C8, W2
 * (bits 0xB108 cleared, 0x0080 set, the rest kept), B4 & 0x35, the attitude (level for a runway start, the carrier's for a carrier start) and the matrices, the
 * position, the motion words, refills the fuel, clears the preset V:4A928, the message line V:4AB98 and the ejection latch V:4AA7A, sets V:4A968 = 0xFE, and keeps
 * everything else, in particular the view selector V:4A83D. The caller owns V:4A951 (the hold counter), the clearing of the records 1 to 3, V:4A910's request and
 * the re-set of V:4A984 bit 6 by the resume state (this function does not set it). */
void flight_restart(aircraft_t *ac, flight_start start);

/* Sets the position (and the derived cell fields) / the attitude words (and the two matrices) of an aircraft, as the
 * worked examples and tests do for their initial states. Both call flight_sync. */
void flight_set_position(aircraft_t *ac, int32_t x, int32_t y, int32_t z);
void flight_set_attitude(aircraft_t *ac, uint16_t pitch, uint16_t heading, uint16_t roll);

/* One fixed step (sections 1 to 12). */
void flight_step(aircraft_t *ac, const flight_inputs_t *in);

/* A second aircraft stepped by the player's rules (note 08 8.3, the training instructor); see flight.c for what it leaves out. The caller has already decremented
 * +0x4C and run the aircraft's state routine; keys is the key byte that routine left. A no-op without an installed environment. */
void flight_step_entity(aircraft_t *ac, uint8_t keys, int no_motion);
/* Test hook: the result handler of a non-player record (note 07 5.1) for the collision result r (0x10 or 0x20) with the previous position `before`. */
void flight_test_result_entity(aircraft_t *ac, int r, const int32_t before[3]);
/* tests: the result handler of the installed environment for the record `ac` (the player's damage handler for a record that is not an entity, note 07 10.4) */
void flight_test_result(aircraft_t *ac, int r, const int32_t before[3]);
/* sine and cosine (Q14) of an angle word (28,800 per circle) from the installed environment's tables; 0, 0 without them. */
void flight_sincos_word(aircraft_t *ac, uint16_t w, int32_t *s, int32_t *c);

/* The camera words of a head view: the composition 0x325B8 of the record matrix mat_a with the head triple (pitch, yaw, roll), out = pitch, heading, roll (note 03, note 06). */
void flight_head_camera(aircraft_t *ac, const flight_tables_t *t, int pitch, int yaw, int roll, int32_t out[3]);

/* Rewrites the derived aircraft_t fields (pos stays, pitch/yaw/roll/speed/throttle/gear/hook/...)
 * from ac->f. flight_init and flight_step call it. */
void flight_sync(aircraft_t *ac);

/* HUD figures (section 12 and note 06), all fixed point:
 *   airspeed_kt  |W| / 12 truncated (0 while W0 bit 0x8000 is set)
 *   altitude_ft  5 * (y >> 10) from the current y
 *   heading_deg  (heading >> 3) / 10
 *   (no vertical-speed figure: the original has none, note 05 section 12, correction C30)
 *   pitch_deg    nose up positive; roll_deg positive = left bank (the note's sign)
 *   throttle_pct |T| * 256 / 307; fuel_pct fuel * 100 / 0x61A800 (the panel digits are fuel >> 8, 25000 full)
 *   gear_down, hook_down. The warning text is not set here: the arbiter of the core cockpit module (cockpit.h) owns it; the step leaves the accepted post in
 *   V:4AB98 and the cue bits in V:4AC0C for it.
 * The panel digits are caches (note 06, "Draw schedule"): flight_panel_altitude / flight_panel_speed
 * give them. */
void flight_hud(const aircraft_t *ac, game_hud_t *hud);
int32_t flight_panel_altitude(const aircraft_t *ac);   /* feet, refreshed when the step counter n has (n & 3) = 0 */
int32_t flight_panel_speed(const aircraft_t *ac);      /* knots, refreshed when (n & 3) = 2 */
/* The direct post (section 12) the state asks for, evaluated with the state at the call (y and +0x42 as the step reads them: call it
 * before flight_step to see what the next step posts): 0xD02A, 0x9C06 or 0. The step itself applies the acceptance rule and leaves the result
 * in V:4AB98 (flight_state_t.v4ab98) and the cue bits in V:4AC0C, which is what the arbiter (cockpit.c) reads. */
uint16_t flight_direct_post_code(const aircraft_t *ac);
/* Counters of the module (flight_state_t.reads, tests only; a differential test must report a sequence that counted one of the last three, because its
 * comparison is then not a verdict):
 *   FL_COUNT_VARIABLE   a table over-read that landed in the live variable block (0x4A6E8..0x4AE92) on a word the port keeps: answered from that variable (exact)
 *   FL_COUNT_RUNTIME    a table over-read in 0x49A12..0x4AE92 on a word the port does not keep (display data written at run time, other variables), or on one of the
 *                       five run-time text bytes of span 2 (0x440C9..0x440CC, 0x44EA1): answered 0
 *                       (accepted divergence, note 05 17.9, Q16)
 *   FL_COUNT_IMAGE      a table over-read that landed in the image outside the two extracted spans: answered 0 (the note says no state reaches it)
 *   FL_GUARD_EDGE_DEN   zero divisor in the water edge slope (the vertices are multiples of 4096, so never with the extracted cell data)
 *   FL_GUARD_TERRAIN_DATA  a terrain walk that left the extracted words (the extracted data is well formed, tests/test_flight.c walks all of it: never) */
enum { FL_COUNT_VARIABLE, FL_COUNT_RUNTIME, FL_COUNT_IMAGE, FL_GUARD_EDGE_DEN, FL_GUARD_TERRAIN_DATA, FL_GUARD_KINDS };
unsigned flight_guard_hits(const aircraft_t *ac, int kind);
void flight_guard_reset(aircraft_t *ac);

/* The image word at a flat address the way the original's table lookups see it: from the two spans, 0 elsewhere. *known is 1 when the address lies in a span. */
int16_t flight_image_word(const flight_tables_t *t, uint32_t addr, int *known);
/* The word the module answers for a lookup at any flat address, counted like a lookup: the spans, the port's own variables in 0x4A6E8..0x4AE92 (FL_COUNT_VARIABLE),
 * 0 elsewhere (FL_COUNT_RUNTIME in 0x49A12..0x4AE92, FL_COUNT_IMAGE otherwise). Tests only. */
int16_t flight_lookup_word(aircraft_t *ac, const flight_tables_t *t, uint32_t addr);

/* Pure helpers exposed for tests. */
/* The two matrix builders 0x32B52 (A) and 0x32BEC (B) as the flight module runs them (note 05 17.9): the angle word is shifted right by 3 logically as an unsigned
 * 16-bit number and folded with no range guard, over the address-mapped sine table. ac supplies the counters only. */
void flight_matrix_a(aircraft_t *ac, const flight_tables_t *t, uint16_t a, uint16_t b, uint16_t c, int16_t out[9]);
void flight_matrix_b(aircraft_t *ac, const flight_tables_t *t, uint16_t a, uint16_t b, uint16_t c, int16_t out[9]);
/* Test hook (section 10): the height of one water edge at xc; form 0 is the original's u normalisation, 1 the other reading. Returns 0 when the edge is skipped. */
int flight_terrain_edge_height(int32_t X1, int32_t Z1, int32_t X2, int32_t Z2, int32_t xc, int form, int32_t *ez);
/* section 10: updates bits 0x02 and 0x04 of *b4. counters (may be NULL) is flight_state_t.reads. */
int32_t flight_terrain_query(const flight_tables_t *t, int32_t x, int32_t z, uint8_t *b4, uint32_t *counters);

#endif
