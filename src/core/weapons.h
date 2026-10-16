/* src/core/weapons.h - the gun, the missiles, the countermeasures, damage, the trail and the jolt, reproduced from docs/re/07-ai-and-weapons.md sections 7 to 11 and 13
 * ("note 07"; section numbers below are that note's, V:xxxxx names are variables of the original).
 *
 * What lives here
 *   - the tracer pool (7.2): 20 slots of which the even ten are used; the gun routine 0x172DC (7.3: bursts of five, the reservoir, the reload timer, the rounds, the log counter);
 *     the slot step and the initialiser (7.4, 7.5); flares and chaff are slots with the flags 0x1000 and 0x2000 (9);
 *   - the missile launch (7.6, hooks for entities.c: weapons_launch_player / weapons_launch_ai), the missile gate 0x27DC2 (8.1, weapons_gate), the destination update 0x2826A with the
 *     decoy branch (8.3, 9.4) and the live retarget 0x27E7E;
 *   - the contact scan 0x2B5E2 (10.2, 10.3, weapons_contact: installed as flight_env_t.contact) and the log counters of 13.5;
 *   - the gun's hit test 0x2ADDE with the hull lists (8.2, 8.2a);
 *   - the trail ring (13.1) and the jolt (13.3);
 *   - the combat sound events of 13.4 (sound_event calls into weapons_t.sound, note 10 section 8) and the key bodies of 9.1 / 7.1.
 * The damage handler of the player (10.4) and the missile response 0x31D12 (8.3) are in flight.c (they are steps of the record); the effect objects are entities.c's
 * (entities_object_create).
 *
 * WIRING (game.c): weapons_init once; weapons_mission_start in the mission start; the keys call weapons_key_*; each pass: weapons_world_start before the player's step (cool-downs),
 * entities hooks launch_player / launch_ai / weapon set to the three functions below (user = the weapons_t), flight_env_t.contact = weapons_contact and .follow = weapons_follow
 * (user = the weapons_t), weapons_jolt_pass after the display lists, weapons_gun once per pass in the flight path, before the entity pass (the walk) when the draw scan finds an active chaff
 * slot or any active slot in an external view or the map view is shown, else after it (note 07 7.3, 12.5: the gun reads the record bytes and the vertex slots the draw left, 8.2a: the
 * previous pass's in the first order, this pass's in the second); not on the pause screen (gate 0).
 *
 * PAUSED: one flag, (the pause screen || the pause byte V:4A866 of a script's count). With it set the tracers do not move, age or hit, the trigger does not fire and the cool-downs
 * do not run; the flare and chaff keys still raise their request, and the gun routine's reload timer still runs in a counted pass (7.3).
 *
 * The sound events of note 07 13.4 are made through weapons_t.sound (set by game.c; NULL: none): the gun, the flare and chaff release, the HUD key click of the T key and the ECM switch. The
 * launch, the bump, the crash, the ejection bang and the seat tone are the sound module's and game.c's (note 10 section 8). The weapon readout and ECM lamp redraw requests and the demo's key-click
 * event words (V:4AA52/53, note 08 7.3) are not state of this module: the panel is redrawn every frame and the demo stream is the replay module's.
 *
 * Core rules: C99, fixed-width integers, no stdio, no floating point, no wall time. */
#ifndef INTERCEPTOR_WEAPONS_H
#define INTERCEPTOR_WEAPONS_H
#include <stdint.h>
#include "entities.h"
#include "flight.h"
#include "scene.h"
#include "sound.h"

#define WP_VSLOTS 58           /* the vertex slots of a record the tests can read (lists.h LISTS_VSLOTS) */
#define WP_SLOTS 20            /* 0x4AD2A, 64 bytes each; only the even slots are used (7.2) */
#define WP_BIG_HULL_BOX 0x800  /* the half-size of a big hull's box in the tracer test (8.2 step 4) */
#define WP_TRACER_BOX 0x140    /* V:4AA5E, set at the flight start 0x1D246 */

/* slot flags +0x26 (7.2) */
#define WPF_ACTIVE 0x0001
#define WPF_STOPPED 0x0002
#define WPF_HIT_CLEAR 0x0008
#define WPF_HIT_SET 0x0010
#define WPF_FLARE 0x1000
#define WPF_CHAFF 0x2000

typedef struct {
    int32_t pos[3];            /* +0x00..: x and z inside the cell (0 to 0x3FFFFF, wrapped below), y absolute */
    int32_t step[3];           /* +0x0C..: the displacement per pass */
    int32_t sub[3];            /* +0x18..: one eighth of the first step, advanced eight times by the hit test */
    uint16_t latch;            /* +0x24: 0 at creation: the first pass after creation does not move the slot */
    uint16_t flags;            /* +0x26 */
    int16_t life;              /* +0x28 */
    uint8_t owner;             /* +0x2E */
    int16_t cell_x, cell_z;    /* +0x30, +0x32 */
} wp_tracer_t;

/* One entry of queue C as the gun's hit test walks it (note 04a section 4): the entity's record index (or an object's id) and the entry's flag byte (bit 6 secondary object, bit 4 entity). */
typedef struct { uint8_t id, flags; } wp_qentry_t;

typedef struct weapons_hooks {
    void *user;
    /* a flight-log counter (logbook.h LB_*): add delta */
    void (*count)(void *user, int lb_off, unsigned delta);
    /* queue C of the display lists in order (first to last); returns the number of entries written (V:4A916 at most max). NULL: no entity can be hit by the gun */
    int (*queue_c)(void *user, wp_qentry_t *out, int max);
    /* the first n (at most WP_VSLOTS) vertex slots +0xA4.. of record `rec` as the last draw left them: slot k = (M . v_k) >> 8 of the block that draw used (lists.c vslot; the gun runs after
     * or before the draw of its frame by the draw scan, 7.3); returns 0, or -1 when unknown */
    int (*slots)(void *user, int rec, int16_t (*out)[3], int n);
    /* the model's flag byte (block byte +7 of the first level); returns -1 when unknown (the class decides: ships are big hulls) */
    int (*model_flags)(void *user, unsigned model);
    /* the post routine 0x29BC8 (the message line): the flare, chaff and gun keys */
    void (*post)(void *user, uint16_t code);
    /* a record as the ship tests read it (flight_ship_t: its state and its stored vertex slots): the ship rows of the contact scan (class nibble 0x20 in a record other than 14, 10.2) and the
     * big-hull test 0x2B3F0 of the gun (8.2: any record whose model flag byte has bit 4); returns 0, or -1 */
    int (*ship)(void *user, int rec, flight_ship_t *out);
    /* the draw's part of a record that the rearm zeroes whole (7.1): its vertex slots +0xA4.. and its part id +0x7B live in the display lists (lists.c vslot, part_id); NULL: none */
    void (*clear_draw)(void *user, int rec);
} weapons_hooks_t;

typedef struct weapons {
    entities_t *e;
    weapons_hooks_t hook;
    wp_tracer_t slot[WP_SLOTS];
    int8_t reload;              /* E+0x3B: the reload timer of the burst (signed byte) */
    uint8_t trigger;            /* V:4A97E bit 3: the trigger is held */
    uint8_t cm_req;             /* V:4A943: 1 chaff, 2 flare */
    uint8_t flare_ref, chaff_ref;   /* V:4A944, V:4A945: the pool slot + 1 of the most recent flare / chaff */
    uint8_t flares, chaff;      /* V:4A905, V:4A904 */
    uint8_t snd_off;            /* V:4A865: the map-view byte (note 07 7.3: the sound routines test it too, so no sound event is made while it is set) */
    uint8_t hold;               /* V:4A875: freezes the tracer lives (never set in play) */
    uint8_t freeze;             /* V:4A840: freezes the missiles' life (reads 0 in play) */
    uint8_t gun_sound;          /* V:4AC13 bit 5: the gun sample exists (the cannon sound object); the port assumes 1 */
    uint8_t ext_view;           /* V:4A83D: an external view (the gun sound's duration; the click of T and J): refreshed by the gun pass and by game.c at every flight key (note 10 7.4a) */
    uint8_t paused;             /* the paused flag of the last weapons_gun call (the pause screen or the pause byte) */
    /* the trail ring (13.1) */
    int32_t ring[6][3];
    uint8_t ring_count, ring_idx;
    uint8_t trail_len[ENT_RECORDS];   /* +0x3D of each record */
    /* the jolt (13.3) */
    uint8_t jolt_idx;           /* V:4A947 */
    uint8_t jolt_on;            /* V:4A984 bit 0x0400: a jolt is running */
    int8_t jolt_row;            /* V:4A990: the row offset d in force */
    uint8_t jolt_started;       /* the kind that started the running jolt (scene) */
    uint8_t jolt_called;        /* the last weapons_jolt_pass call was one that is not the cancelled one (kind 1 to 4, not an external view): it asks for the redraw of the 14 request bytes, R = 3 (note 07 13.3, 0x1EFF4) */
    sound_t *sound;         /* the sound module the combat events go to (NULL: none) */
    /* counters for tests and the report */
    unsigned n_shots, n_hits, n_launches, n_refused;
    unsigned dbg_inbox;         /* entity entries that passed the box test in the last hit test (tests) */
    int dbg_lm_oob_n;           /* landmark ordinals outside 0 to 10 that the last hit test met (07 8.2 NOTE-GAP: the original reads memory the port does not hold; the rig test makes it a negative pointer) */
    int dbg_lm_oob[16];
    int dbg_reg0;               /* added to the ordinal register's start value of -1 (07 8.2): 0 in play; the rig test lmstate sets it to k so that the walk enters at the ordinal k (a leftover of k - 1) */
} weapons_t;

void weapons_init(weapons_t *w, entities_t *e);
/* The mission start: the stores of the flight start (0x1D1BE, 0x1F14A): the stock 0x24, 500 rounds, flares and chaff 16, ECM off, the pool, the reservoir 5, the timer 0, the trail, the jolt. */
void weapons_mission_start(weapons_t *w);
/* The rearm routine 0x1F11E (note 07 7.1, 9.7): stock 0x24, 500 rounds, flares and chaff 16, V:4A943/4/5 cleared, the records 1 to 3 zeroed (not the pool slots, not the cool-downs, not the
 * reservoir or the reload timer). The caller sets fuel and the message. */
void weapons_rearm(weapons_t *w);

/* ---- keys (7.1, 9.1) ---- */
void weapons_key_fire(weapons_t *w, int down);       /* SPACE press (down = 1) / release */
int weapons_key_select(weapons_t *w);                /* RETURN: the weapon select (not in the modes 2 and 0x7D: the caller). Returns 1 when it acted: the caller clears the fire-prompt state V:4A96C (7.1) */
void weapons_key_flare(weapons_t *w);                /* F */
void weapons_key_chaff(weapons_t *w);                /* C */
void weapons_key_ecm(weapons_t *w);                  /* J */
void weapons_tone_click(weapons_t *w);               /* the HUD key click 0x37C4A (the T key, the ECM switch): a tone on object 4 unless an external view is shown */

/* ---- the pass ---- */
/* Before the player's step: the cool-downs V:4A906 / V:4A907 are decreased once per unpaused pass (10.1; paused = the pause screen, the pause byte also stops them). */
void weapons_world_start(weapons_t *w, int paused);
/* The gun routine 0x172DC (7.3-7.5) once per pass. n = V:4A992 as the draws of the pass saw it. In a paused pass (see PAUSED above) only the reload timer runs: no tracer moves, ages or
 * hits, the trigger does not fire. */
void weapons_gun(weapons_t *w, uint32_t n, int paused);
/* The draw scan 0x2AD18 (07 7.3, 12.5): 1 when the gun routine runs BEFORE the entity pass of the frame, 0 after it. The highest-index active slot decides alone: a slot that is not chaff in the cockpit view
 * (ext_view 0) gives 0; otherwise the owner record's distance from the camera is compared with the slot's (the fixed-point distance of the larger-axis routine on the per-axis absolute differences shifted
 * right by 8; the slot's x and z carry its cell words shifted left by 22): 1 when the owner's is not greater. cam is the camera position longs V:4ACF6, V:4ACFA, V:4ACFE. No active slot: 0. */
int weapons_scan(const weapons_t *w, int ext_view, const int32_t cam[3]);
/* The hooks for entities.c (entities_hooks_t): the launches of the player (records 1 to 3) and of the AI (records 5, 9, 11, 13), the gate of a valid class nibble 0 record. */
int weapons_launch_player(void *w, int idx);
int weapons_launch_ai(void *w, int idx);
int weapons_gate(void *w, int idx);

/* The contact scan 0x2B5E2 for the mover whose state is f (flight_env_t.contact): phase 0 visits the records after the mover up to 13, phase 1 the record 15 (the record 14 is the carrier
 * of the flight environment, tested by the flight module's ship scan between the two). Returns 0x40 on an aircraft contact (the marks and counters of 10.3 are made), else 0. */
int weapons_contact(void *w, flight_state_t *f, const int32_t prev[3], int phase);

/* The trail ring writer 0x2AC5A (13.1), called at the end of every record step with the record index and its aircraft. */
void weapons_trail_step(weapons_t *w, int idx);
/* The followed record V:54E72 takes the record whose state is f (flight_env_t.follow): the writers 0x17022/0x17024 (the player's skid or crash trail) and 0x2A954/0x2A95E (the result handler
 * of a non-player record; the gun's third hit and the contact scan write e->trail themselves). */
void weapons_follow(void *w, const flight_state_t *f);
/* The training placement 0x2CFDA (tail 0x2D09A) empties the ring (13.1). */
void weapons_trail_clear(weapons_t *w);

/* The jolt (13.3) once per pass at the draw path. kind_byte is the player's V:4A878 (read and cleared here), external is |V:4AA3E| > 14 (an external or full-width view). The scene
 * carries jolt_row (V:4A990, -1, 0 or +1). Returns the kind that started a jolt in this call (0 none) so that the scene's jolt field can be raised. */
int weapons_jolt_pass(weapons_t *w, flight_state_t *pf, int external);

/* Hull test of 8.2a for tests: the six planes of the list for the record `rec` with its vertices in v[17][3] (record slots, units of the record) against the point T = (x, y, z) in
 * fine units (position >> 8). Returns 1 when inside. */
int weapons_hull_inside(const ai_t *tab, const flight_state_t *rf, int part_id, const int16_t vtx[17][3], const int32_t fine[3], const int32_t point[3]);

#endif
