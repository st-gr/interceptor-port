/* src/core/entities.h - the 16 entity records and the 16 secondary objects of a flight, reproduced from docs/re/07-ai-and-weapons.md Part A ("note 07";
 * section numbers below are that note's, except where another note is named).
 *
 * What lives here
 *   - the records (note 07 1.4): the flight model's state block (aircraft_t, which owns every word the flight step reads or writes) plus the words only the AI, the
 *     placement and the scripts use (the destination +0x2C..+0x34, the target/leader byte +0x38, the route index +0x3A, the hit counts +0x3C, the range +0x4A, the
 *     placement tag +0x5D, the stock +0x5F, the rounds +0x60, the AI flags +0x64, the engagement byte +0x7A), and the model descriptor the placement copies (2.3);
 *   - the secondary objects (1.5) and their creator (refused when 16 are in use);
 *   - the spawner (2): the mission-start placement of a scenario sub-list and the region triggers, with the seeded selector's results supplied by the caller;
 *   - the world update's visit order (1.2): the record roles, the AI tick every 16th pass and the per-record region check every 32nd pass at the record's phase;
 *   - the dying path (5): the result handler and the expiry rules are in the flight step (flight.c); the kill count V:4A963 is collected here;
 *   - the ejection seat and the parachute (6.4): the seat routine of the world update (0x26F60), the spawn (0x27522, 0x2758E) and the seat's own step (0x27BFA);
 *   - the viewed record and the neighbour test's viewed cell (6.1, 6.4).
 * The player (record 0) is the game's aircraft_t, attached with entities_attach_player; the training lead (record 4 while a training part runs) keeps its storage in
 * the trainer (training.h) and is attached with entities_attach_external, so both reach the generic code through entities_ac().
 *
 * Core rules: C99, fixed-width integers, no stdio, no floating point, no wall time. All "V:" names are variables of the original (hunks 71 to 73). */
#ifndef INTERCEPTOR_ENTITIES_H
#define INTERCEPTOR_ENTITIES_H
#include <stddef.h>
#include <stdint.h>
#include "aircraft.h"
#include "assets.h"

#define ENT_RECORDS 16
#define ENT_OBJECTS 16
#define ENT_MAX_REGIONS 8
#define ENT_ROUTE_BLOCKS 33        /* the longest waypoint run (note 07 4.3; entry 0, the table itself, is unused) */
#define ENT_MAX_BLOCKS 64          /* position blocks the world supplies (entry numbers 1 to 63) */

/* record roles by index (1.2) */
#define ENT_PLAYER 0
#define ENT_FIRST_WEAPON 1         /* records 1 to 3: the player's weapon, seat and parachute records */
#define ENT_TRAINING_LEAD 4
#define ENT_CARRIER 14
#define ENT_SHIP2 15

/* W0 bits (1.4) */
#define EW0_HIT 0x8000u
#define EW0_RADAR 0x4000u
#define EW0_LEVEL 0x2000u
#define EW0_ACTIVE 0x1000u
#define EW0_BRAKE 0x0800u
#define EW0_DYING 0x0400u
#define EW0_CRASHED 0x0200u
#define EW0_KEEP_MOVING 0x0100u
#define EW0_AI 0x0080u
#define EW0_VALID 0x0040u
#define EW0_PENDING 0x0010u
#define EW0_NOT_COUNTED 0x0008u
#define EW0_NOQUEUE 0x0004u
#define EW0_SPAWN 0x0002u
#define EW0_ENGAGED 0x0001u
/* W2 bits */
#define EW2_SCRIPTED 0x0100u
#define EW2_GROUND 0x0080u
#define EW2_RELEASE 0x0020u
#define EW2_RECENTRE 0x0010u
#define EW2_PROX 0x0001u
/* B4 bits */
#define EB4_THREATENED 0x20u
#define EB4_RANGE_DONE 0x01u

/* One entity record (1.4). The first member holds W0, W2, the state byte, the cells, the longs, the velocity, the key byte +0x65, the matrices and the rest of the
 * block the flight step uses (aircraft.h). */
typedef struct entity {
    aircraft_t ac;               /* storage of the record, unless entities_attach_external names another (record 4 in the trainings) */
    int16_t dest_cx, dest_cz;    /* +0x2C, +0x2E: destination cell x, z; negative x: no destination */
    int16_t dest_fx, dest_fz;    /* +0x30, +0x32: destination fine x, z */
    int32_t dest_h;              /* +0x34: destination height in the units of +0x10 */
    uint8_t target;              /* +0x38: 0xFF none; bit 7 set = 0x80 + record index of the target or leader; bit 7 clear: a shift count (3.5 step 2) */
    uint8_t route;               /* +0x3A: the waypoint run index of state 8 (bits 8 to 14 of the entry's word 2) */
    uint8_t hits;                /* +0x3C: low nibble gun hits, high nibble missile hits */
    int16_t range;               /* +0x4A: distance to the destination; 0x7FFF beyond range or unknown */
    uint8_t tag;                 /* +0x5D: 0xFF mission-start placement, else region number + 1 / 1 / 0 (2.5) */
    uint8_t stock;               /* +0x5F: missiles left, high nibble short range, low nibble long range */
    uint16_t rounds;             /* +0x60: gun rounds */
    uint8_t ai_flags;            /* +0x64 (3.7) */
    uint8_t engage;              /* +0x7A: 3 far, 4 near, 5 released to its destination */
    uint8_t b70, b71;            /* +0x70, +0x71: the map routine's stored nibbles and heading sector (note 07 1.4): the placement writes 0xFF in +0x71 (rule 17), a freed record keeps the bytes, a copied record (a launch, the seat) carries the source's, the rearm clears them in the records 1 to 3, the player's start sets +0x71 = 0xFF (0x1D91C) */
    uint16_t template_id;        /* the template (model id) the last placement into the slot copied; the descriptor words are in entities_t.desc */
} entity_t;

/* One secondary object (1.5): the first 0x20 bytes of a record. */
typedef struct secondary {
    uint16_t w0;                 /* +0x00: 0x0040 valid, 0x0010 pending, 0x0004 not in queue */
    uint16_t model;              /* +0x02: model id, 0x12 or 0x13 by default */
    int16_t life;                /* +0x04: 0x7F at creation, decremented once per unpaused pass by the world update */
    int16_t cell_x, cell_z;      /* +0x06, +0x08 */
    uint8_t sub, nb;             /* +0x0A, +0x0B */
    int16_t fine_x, fine_z;      /* +0x0C, +0x0E */
    int32_t alt8;                /* +0x10 */
    int32_t pos[3];              /* +0x14, +0x18, +0x1C */
} secondary_t;

/* What the generic code needs from outside the record arrays. Callbacks may be NULL (tests). */
typedef struct entities_hooks {
    /* record 4 in the trainings (mode byte 2 or 0x7D): the instructor's pass, which replaces the generic gate, state routine and step for that record. Returns nothing. */
    void (*training)(void *user);
    /* the weapon routine for a valid record of class nibble 0 (0x27DC2, part B): returns 1 when the record is to be stepped by the flight step afterwards */
    int  (*weapon)(void *user, int idx);
    /* the launches (0x275C4 for records 1 to 3, 0x275F6 for the AI's missile records 5, 9, 11, 13): called for an invalid record; return 1 when the record was filled */
    int  (*launch_player)(void *user, int idx);
    int  (*launch_ai)(void *user, int idx);
    /* the heading the aim routine of note 03 gives for a vector (dx, dz) with the snap limit (2.2); NULL: 0 */
    int32_t (*aim_heading)(void *user, int32_t dx, int32_t dz);
    /* the player's designation (Task 6): called at the head of each pass to set the player's record fields +0x38 (target byte) and +0x4A (range word); NULL: they keep their values */
    void (*player_target)(void *user, uint8_t *target, int16_t *range);
    /* the aim routine 0x1454A for the attitude step of the classes 0x30 to 0x32 (note 07 6.3a): the pitch and heading words that follow the direction (vx, vy, vz) of the velocity, from the current words *pitch and *heading, with the turn
     * limit 2000 per step (the first call snaps when the camera mode V:4A966 and V:4A85E are both 0, and sets V:4A85E). NULL: the words stay. */
    void (*aim_dir)(void *user, int32_t vx, int32_t vy, int32_t vz, int32_t *pitch, int32_t *heading);
    /* the model-unit shift (low nibble) of a template's first block (2.3); NULL: 3 */
    int (*unit_shift)(void *user, unsigned template_id);
    /* the five-word position block of table entry number n (1-based as the world's positions.txt): cell x, cell z, fine x, fine z, height; returns 0, or -1 when absent */
    int (*position_block)(void *user, unsigned entry, int16_t w[5]);
    /* the waypoint run named by route index r: up to max blocks; returns the count (0: an empty run) */
    int (*route)(void *user, unsigned r, int16_t blocks[][5], int max);
    /* sound requests (note 10 8): code 1 the seat tone 0x26FFE (nibble 9, cockpit view), code 2 the ejection bang request (nibble 1: V:4A922 = 0x3F, V:4A921 = 0x78, V:4A870 = 0xFF) */
    void (*sound)(void *user, unsigned code);
    void *user;
} entities_hooks_t;

typedef struct entities {
    entity_t rec[ENT_RECORDS];
    secondary_t obj[ENT_OBJECTS];
    uint32_t desc[ENT_RECORDS][TEMPLATE_LONGS];   /* the descriptor copies 0x2600C + 20 * slot (2.1 step 3) */
    aircraft_t *player;          /* record 0 (the game's aircraft) */
    aircraft_t *ext[ENT_RECORDS];/* storage override per record (NULL: rec[i].ac) */
    entities_hooks_t hook;
    const templates_t *tpl;      /* tables/templates.bin (NULL: the template copy writes zeros and the id) */
    const ai_t *tab;             /* tables/ai.bin (NULL: the rate tables, the evasion table and the seat offsets of the note are not available: ai.c falls back to its built-in copies) */
    const scenario_t *scn;       /* regions and sub-lists (NULL: no region triggers, no lost-target search) */
    /* ---- world variables the AI, the spawner and the dying path read or write ---- */
    uint8_t head_done;           /* entities_head_b has run the head of the world update for this pass (the game, before the player's step); entities_step consumes the mark */
    uint32_t gate_seen;          /* records whose gate got past the active test in this pass (the stale target register of a state 8 record's threatened test, 4.5) */
    uint32_t pass;               /* V:4A992 as the world update sees it: ONE counter, owned by the cockpit module (cockpit_t.n: cleared at the mission start, held in a pause, kept across a crash restart; note 07 4.8); the game copies it here before each pass */
    uint8_t mode;                /* V:4A95E: the mode byte */
    int progress;                /* V:4A95F: the campaign progress counter (signed) */
    uint8_t paused;              /* V:4A866 */
    uint8_t frozen;              /* V:4A867 */
    uint8_t flying;              /* V:4A846: set to 1 at the start of every flight */
    uint8_t demo;                /* V:4A848: the demonstration flag (switches the region tick off) */
    uint8_t sim_on;              /* V:4A984 bit 6 as 0/1: the flight simulation runs */
    uint8_t hostile_cap;         /* V:4A962 */
    uint8_t hostile_count;       /* V:4A961 */
    uint8_t kills;               /* V:4A963 (the part the non-player records add; the player's own dying path adds to its f.v4a963, entities_kill_count sums both): kills of counted aircraft at their first expiry in the air */
    int16_t designated;          /* V:4AA78: the player's designated target record (negative: none) */
    int16_t lock_tail;           /* the lock as the end of the update loads it (0x2852E), before the stale-lock drop of the same routine: entities_step sets it (the F6 script's delay quirk, note 08 12.2) */
    uint8_t launch_flag;         /* V:4A86F: the player's launch flag read by the threatened test */
    uint8_t req_player_launch;   /* V:4A872 */
    uint8_t req_ai_slot4;        /* V:4A873: the request for the aircraft in slot 4 to fire. No code in the image sets it (note 07 7.6): dead in play, kept for the rig */
    uint8_t req_ai;              /* V:4A874: the request the launch decision reads. No code in the image sets it (note 07 7.6, 4.6): dead in play, kept for the rig */
    int8_t trig;                 /* V:4A852: the training trigger state, signed (negative: waiting for the trigger) */
    uint8_t training_part1;      /* V:4A84B: the part 1 flight is running (the end routine's wait length) */
    uint8_t region_bits;         /* V:4A855: the region "inside" bits */
    int d7;                      /* the placer's working register (2.5) */
    int sub_first, sub_count;    /* V:4A7F2: the current sub-list, entries [first, first + count) of scenario sub_entry[] */
    uint8_t request;             /* V:4A910: the display-list request byte raised by the world update (0x8C seat spawn, 0x0C removals) */
    uint8_t notice;              /* V:4A968: the notice code the world update raised for the message line (0 none) */
    uint8_t debug_word;          /* V:4AA56: the value the AI stores before calling the bare return 0x1F1D6 (0x1E, 0x34, 0x35, 0x3C, 0x3E): no visible effect (4.8) */
    /* ---- the ejection (6.4) ---- */
    uint16_t chute_latch;        /* V:4A97A */
    uint8_t end_request;         /* V:4A8E2 (local backing when `req` is not pointed elsewhere): a pending request (2 = the seat has landed) */
    int8_t end_delay;            /* V:4A8E4 (local backing) */
    uint8_t *req;                /* the flow's request byte V:4A8E2: the game points it at the screen module's, so the seat and the keys and scripts share ONE byte */
    int8_t *req_delay;           /* the flow's countdown V:4A8E4 */
    /* ---- the views (6.1): the viewed record is the player's f.v4a994 / f.v4a996 (aircraft.h), the one source ---- */
    uint8_t trail;               /* V:54E72: the trail record (0xFF none) */
    int16_t view_cell_x, view_cell_z;   /* V:4AA04, V:4AA06 as the previous pass left them: the neighbour test reads these (6.4, "The seat view") */
    uint8_t saved_view;          /* V:4A8EB: the saved external view byte the seat's view switch takes in an external view (the game keeps it current) */
    uint8_t redraw_req;          /* 0x1EC14 ran in the cockpit view (V:4A83D was 0): the caller applies the head set-up and R = 3 (0x1EC94, or 0x1E632 for a class other than 0x30) and clears this; 2 when the switch left an external view (0x1EC54 stores V:4A8EE only there), 1 in the cockpit view */
    uint8_t view_switched;       /* the seat step switched the view (6.4): the caller brings its view state along (the view byte, the ring direction 4) and clears this */
    uint8_t strip_off;           /* V:4A891 cleared by the seat step's view switch (6.4): the caller blanks the external text strip for a pass and clears this */
    uint8_t camera_update;       /* V:4A86D: the ring camera is updated every pass while set; cleared by the fixed camera */
    uint8_t camera_mode;         /* V:4A966 (the front end's; 0 in a flight) */
    /* ---- the stored selector results of the placement (2.4): reused by an ESC restart ---- */
    int pl_dx, pl_dz;            /* V:4ABD0, V:4ABD2 (unhalved) */
    /* counters of the refusals (tests and the report) */
    unsigned n_spawned, n_skipped_valid, n_refused_cap, n_dropped_requests, n_objects_refused;
} entities_t;

/* Clears everything and installs the tables; any pointer may be NULL. The hooks, the player and the external records are cleared too: attach them afterwards. */
void entities_init(entities_t *e, const templates_t *tpl, const ai_t *tab, const scenario_t *scn);
/* The mission start's reset: the records, the objects and the variables of the world update are cleared; the tables, the hooks and the attachments stay. */
void entities_reset(entities_t *e);
void entities_attach_player(entities_t *e, aircraft_t *player);
void entities_attach_external(entities_t *e, int idx, aircraft_t *ac);   /* ac NULL: back to the record's own storage */
/* The aircraft of record i (the player's, an attached external one, or the record's own). */
aircraft_t *entities_ac(entities_t *e, int idx);
const aircraft_t *entities_ac_c(const entities_t *e, int idx);

/* ---- the spawner (2) ---- */

/* The mission-start order (2.4) for one placement: the caller has drawn the selector (the screen module) and passes the results. `mode` filters the entries (step 1), `cap` is the
 * count word of the row (V:4A962), `offset_dx/dz` the random offset pair (unhalved) applied to the entries with bit 12. Clears V:4A961 and V:4A963; walks entries [first, first + count)
 * of the scenario's sub_entry[] (the first without the threshold test, the others only when their threshold is not above `progress`) and places each with entries_spawn. Returns the
 * number placed. Sets sub_first/sub_count. */
int entities_start_mission(entities_t *e, int first, int count, int cap, int dx, int dz);
/* The scene passes of the mission start run the world update before the flight (the fresh pass of a placed aircraft and the gate's range routine, no state routine): the game calls this once after
 * entities_start_mission with the same sub-list (the rig compares the placement alone, without it). */
void entities_scene_pass(entities_t *e, int first, int count);

/* The entry placer 0x2D248 for one entry of five words (2.1), steps 1 to 19 except the aim (the caller of the mission-start path aims, 2.2). `d7` is the placer's tag register
 * (-2 at the mission start). Returns 1 when the entry was placed, 0 when it was skipped or refused. Review Focus 2: a valid record in the slot skips the entry. */
int entities_spawn(entities_t *e, const uint16_t w[5], int d7, int dx, int dz);

/* The aim after a mission-start placement (2.2). */
void entities_aim(entities_t *e, int idx);

/* ---- the world update (1.2) ---- */

/* One pass of the world update for records 1 to 15, called after the player's own step. `step_ac` of each record is run through flight_step_entity by the generic path:
 * the gate, the state routine, the per-record check, the step. The clock `pass` is the caller's; the function does not increment it. Clears the launch requests at its end. */
void entities_step(entities_t *e);
/* The player's range to the designated target (note 07 12.6); entities_step calls it at the head of the pass. */
void entities_update_player_range(entities_t *e);
void entities_head_a(entities_t *e);   /* the head of the world update (07 4.8) before the player's step: life words, region tick, W2 bit 0 of the records 0 to 14 */
void entities_head_b(entities_t *e);   /* ... and, after the cool-downs, the stale-lock drop; entities_step then skips the head */
void entities_prestep(entities_t *e);  /* the player's range pre-step 0x2835A (and the player_target hook), before the player's step */
/* The region tick 0x2D0AA (2.5): only when (pass & 15) == 3. Exposed for tests. */
void entities_region_tick(entities_t *e);
/* The stale-lock drop 0x26F28 (4.7): a designated target that is no longer valid or has gone out of control is dropped (e->designated = -1). */
void entities_drop_stale_lock(entities_t *e);
/* The crash wait state clears the step counter V:4A992 (4.8): the 16-step and 32-step phases restart. */

/* The secondary objects (1.5). Creates the object from the 32 bytes of a record (pos[], cells, fine words, alt8); returns the index, or -1 when all 16 are valid (nothing is created). */
int entities_object_create(entities_t *e, const secondary_t *from, uint16_t model);

/* ---- the dying path (5) ---- */
/* How a record is made to die, as the code that destroys it does it. KILL_CRASHED: the gun's third hit / the missile kill: crashed bit, out of control, W2 0x0010, state 1 (5.1);
 * the next flight step turns the crashed bit into the dying count. KILL_GROUND (result 0x10) and KILL_HULL (0x20): the result handler's writes (hit, out of control, active cleared,
 * the dying start). KILL_AIRCRAFT (result 0x40): the record is removed at once, V:4A910 = 0x0C, nothing is counted. Records that are not aircraft (class nibble 0) are removed the same way. */

/* ---- the ejection (6.4) ---- */
/* The seat routine 0x26F60 asked for record `slot` (an invalid record among 1 to 3, or 5 without the parachute): reads the player's nibble after the player's step. Returns 1 when it filled
 * the record. */
int entities_seat_routine(entities_t *e, int slot, int allow_chute);
/* The seat's and parachute's step 0x27BFA for record idx (class 0x30 to 0x32). Returns 1 when the flight step is to run. */
int entities_seat_step(entities_t *e, int idx);
/* The SHIFT-F rescue of mode 6 (0x1EE4E, 6.4): ORs 0x0F into the nibble when the chute latch is clear and one of the records 1 to 3 is free. */
/* Returns 1 when the key took effect (the nibble was set); the pod release then makes its noise (note 10 8.2, 0x1EE90). */
int entities_rescue_key(entities_t *e);

/* ---- the views (6.1, 6.4) ---- */
/* The index of the viewed record (the player's V:4A994). */
int entities_viewed(const entities_t *e);
/* Updates the viewed cell (V:4AA04/06) after the world update from the viewed record (the cockpit view; the caller passes the camera's cell in an external view); the flight step of the next
 * pass tests its new cell against it. Writes it into e->view_cell_* and into the player's and every record's flight state. Called by the game once per pass. */
void entities_update_view_cell(entities_t *e, int external, int camera_cell_x, int camera_cell_z);
/* The fixed camera 0x1E3EE (6.3): sets *pos to the camera point of start location `loc` (0 to 4) and clears V:4A86D. Returns 0, or -1 without the table. */
int entities_fixed_camera(entities_t *e, int loc, int32_t pos[3]);

/* V:4A963: the one counter of the original (the non-player part plus the player's own dying path). */
unsigned entities_kill_count(const entities_t *e);

/* The tracking-camera / view-switch routine 0x1EC14 (note 07 6.1, note 08 12.2): the viewed record becomes idx, the notice 0xFE; the designation V:4AA78 stays (the routine sets the HUD point V:4A9F2 to -1, rig). */
void entities_switch_view(entities_t *e, int idx);


/* The seeded generator of the placement (frontend supplied seed, golden runs seed 1): the next 16-bit draw. State is the caller's. */
uint16_t entities_random16(uint32_t *state);

#endif
