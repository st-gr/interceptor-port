/* src/core/game.c - core state machine. Milestones 2 and 3: splash, flight (aircraft plus views; the free camera as a debug mode). */
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "assets.h"
#include "world.h"
#include "flight.h"
#include "cockpit.h"
#include "sky.h"
#include "screen.h"
#include "training.h"
#include "replay.h"
#include "entities.h"
#include "ai.h"
#include "weapons.h"
#include "radar.h"
#include "mapmarks.h"
#include "logbook.h"
#include "missions.h"
#include "sound.h"
#include "mover.h"

#define GAME_MAX_ASSETS 64
/* The carrier record's heading word (+0x68): free flight places the carrier with attitude 0; 28600 is the demonstration flight's (note 05
 * section 17.14). The renderer draws the carrier and the collision code rotates its hull with this one word; flight_init copies it into the
 * player's attitude at a carrier start. */
#define CARRIER_HEADING 0
/* The demo's carrier (note 08 7.5, note 05 17.17 rule 5): record 14 at (286785536, 0, 286785536), heading 28600, instead of free flight's. */
#define DEMO_CARRIER_POS 286785536
#define DEMO_CARRIER_HEADING 28600
/* Carrier position (17.17 rule 5): the carrier is record 14 at (295174144, 0, 274202624), heading 0, for BOTH start kinds of free flight (the world's
 * entry 33); a carrier start puts the player at +11264 / +106496 from it (flight_init does that from the ship record). NOTE: the ship's W0 here is 0x404C;
 * the runway snapshots read 0x405C. The difference is bit 0x0010, the list builder's one-blank mark (17.17 rule 2, pass A (b)); no collision test reads it. */
/* The crash flow (note 05 sections 11, 14, 15, 17.1, 17.3, 17.12, 17.17). One game_step is one PASS of the original's main loop. Sequence of
 * a cockpit crash, passes counted from the impact pass N:
 *   N     the world update raises W0 0x0200.
 *   N+1   the dispatcher hands over (0x12EFE: crash counted, V:4A984 bit 6 cleared, V:4A952 = 1, vertical velocity 0, V:4A951 = 40); the pass runs
 *         its one step, the draw and n; the flip routine's hold then begins (no extra pass: 17.17 rule 3).
 *   hold  80 blanks (40 iterations of 2): no frame routine, no step, no n. The port: CRASH_HOLD_STEPS = 13 game_steps (80 blanks at 6.25 blanks per
 *         step = 12.8, rounded to the nearest step); keys, view toggles and the pause that arrive are queued and applied at the start of A (rule 6).
 *   A     0x138D8 restarts (0x1D538), V:4A94F is decremented (0 = the game-over path, rule 7: the pass then rewrites the record ONLY, below); the pass
 *         also runs the world update (V:4A984 bit 6 is clear: attitude only; its ground test reads the vertex slots of the LAST DRAW before the hold,
 *         the crash attitude, so W0 reads 0x91C8 after A, 17.17 (xi): flight.c refreshes the slots after the step, as the draw does, and the restart
 *         leaves them), the list rebuild and n. One 20 ms uniform orange frame ends the hold (17.17 (xii): scene.flash_frames in this step).
 *   B     0x13980: V:4A952 = 0, W2 bit 1, the head recentred (cockpit view only), 0x1D538 again, V:4AB8E = 5.
 *   C1-C5 0x139C2 waits (V:4AB8E 4..0): ordinary passes with bit 6 clear.
 *   C6    0x139C2 sets V:4A984 bit 6 and V:4A968 = 0xFE and returns to the flight state: the first SIMULATED step, the eighth pass counted from A.
 * Every pass from A to C6 runs the generator, the arbiter and n (the cockpit state keeps running, note 04). In the external view there is no hold:
 * the dying state waits for W0 bit 0x0400 to clear (17.3: pass N+16), then A, B, C1-C5, C6 as above. Timing (PORT CHOICE, one place): the original's
 * passes are 8 to 16 blanks long (the resume passes 8 to 14 with the draw), the port's step is 6.25 blanks (FLIGHT_STEP_HZ = 8), so the PASS
 * SEQUENCE is exact and the durations are not: the hold of 80 blanks stays 13 steps (81.25 blanks; the original's 80-81): the accepted 1.25-blank error
 * of the fixed-step model (controller ruling, 17.17 (iii)); A..C5 = 7 steps (43.75 blanks, 0.875 s) against 76-77 blanks (about 1.5 s) measured in the
 * cockpit and 91 in the external view; the first simulated step is the 8th step counted from A. Hold + restart: 80 + 77 = 157 blanks (about 3.1 s) in
 * the original against 13 + 8 = 21 steps = 2.6 s here. */
#define CRASH_HOLD_BLANKS 80
#define CRASH_HOLD_STEPS ((CRASH_HOLD_BLANKS * FLIGHT_STEP_HZ + 25) / 50)   /* rounded to nearest: 13 */
/* The crash allowance V:4A94F is set by the mission start (screen.c, note 08 6.3: 5 in free flight, 3 in the other modes, 1 in practice); g->allow counts it down. */
/* The fifth crash (17.17 rule 7, (xiii)): k0 is the blank the hold ends. The record is rewritten at k0 and nothing else runs; the state stores nothing for 4
 * blanks (V:4A84D = 0 from k0 + 4), the text screen follows and the leave point 0x11A70 is at k0 + 92 blanks. The port's A step opens GAME_MODE_GAMEOVER at
 * once (k0); the leave comes after 15 intervals of one step: 92 blanks = 14.72 steps, rounded to the nearest: 15 steps after A (93.75 blanks, 1.75 late;
 * 14 steps after A would be 87.5 blanks, 4.5 early), so 16 steps in all, A's included. */
/* The game-over screen is the screen module's since milestone 4 (note 08 5.12): the text types out and the leave comes 15 steps after A (the same 92 blanks). */
/* ESC (rule 9): V:4A84D = 0 from k = 3 (the flight is blanked), n is cleared at k = 42: 42 blanks from the key press = 6.72 steps, rounded: 7. */
/* The ESC flow is the screen module's (screen.c, note 08 5.11): the fade-out and the reset take 7 steps from the key in a flight (41 blanks), 5 from a pause. */
#define EVENT_QUEUE 64
#define W0_CRASH_BIT 0x0200u
#define W0_DYING_BIT 0x0400u
#define W2_CONTACT_BIT 0x0002u
#define A984_SIM_RUNNING 0x0040u   /* V:4A984 bit 6: the flight simulation runs (cleared at the hand-over) */
typedef enum { CR_NONE = 0, CR_RESUME, CR_HOLD, CR_DYING } crash_phase_t;
#define MODEL_FA18 4        /* the aircraft models (note 05 section 13) */
#define MODEL_F16 3

struct game {
    game_loader_t loader;
    uint32_t tick;
    char pix[GAME_MAX_ASSETS][CORE_ASSET_NAME_LEN];   /* pix/index.txt, in order */
    int pix_count;
    game_mode_t mode;
    int32_t stick_x, stick_y, throttle, rudder;
    aircraft_t ac;                  /* the player aircraft (flight mode, not freecam) */
    int freecam;                    /* debug: run the milestone-2 free camera */
    world_viewstate_t vs;           /* the view variables (world.h): V:4A83D level, V:4A96A ring, V:4A85F head, V:4A865 map flag, the aim state */
    int leave_ext;                  /* V:4A8ED: the key left an external view for the cockpit; lasts one entity pass (note 04a section 8a) */
    int req_view;                   /* V:4A97E bit 1: the view switch request, SET by KP-Enter and M (an OR: two presses in one pass are one toggle), applied by view_switch */
    int saved_level;                /* V:4A8EB: the external level the next cockpit-to-external switch takes (-1: every KP-Enter and M handler stores it) */
    int32_t saved_alt;              /* V:4A71C: the saved map altitude (0x3000000 at every mission start, only the M key saves) */
    int8_t head_step[14];           /* tables/head_step.bin: the HUD step bytes of the head codes (0x1E800; zeros when the file is absent, a synthetic asset set) */
    int eject_key;                  /* the SHIFT-E key was taken in the last pass (the scene's cp_eject_key) */
    int seat_wait;                  /* the pass that spawned the seat in the cockpit view: its picture keeps the old window and the panel (note 07 6.5: one picture with the panel), the step 50 takes effect in the next pass */
    int hud_step;                   /* the HUD step byte (note 06 "Head views": 0 cockpit code 0, the table byte of the head code or 50 by the equal-row test, 50 in an external view)  */
    int hold;                       /* V:4A949 as a signed byte: negative holds the head view (0xFF on a head key, KP5, a zoom key), 5 on a key release, one less per timer second down to 0 */
    unsigned seconds, seconds_seen; /* V:4ABAA, the timer device's seconds, advanced by the frontend (game_clock_second); V:4ABBA, the value the previous pass saw */
    world_t *world;   /* NULL when the assets carry no world data (models/index.txt absent) */
    /* the flight model (flight.h): active only when tables/flight_*.bin and the models it needs loaded */
    int has_flight;
    flight_tables_t ftab;
    flight_env_t fenv;
    flight_inputs_t fin;            /* the key byte held and the one-shot events of the next step */
    uint8_t keys_held;              /* key byte bits from the buttons (throttle keys) */
    uint8_t keys_axes;              /* key byte bits derived from the axes */
    cockpit_t ck;                   /* frame counter n, tick generator, warning arbiter, H key (cockpit.h) */
    int start_runway;               /* the start kind of the flight (and of the crash restart): 0 carrier deck, 1 a runway block (the location the screens chose, V:4A900) */
    screen_t scr;                   /* the front-end screens (screen.h): the mode machine, the text engine, the key queue */
    trainer_t tr;                   /* the training parts (training.h): the instructor, record 4, and the lesson sequencer; tr.part is 0 outside them */
    logbook_t log;                  /* the flight log (logbook.h, note 08 section 10); scr.log points at it */
    unsigned long air_steps;        /* 10.7: the steps flown airborne and unpaused since the flight began (booked as steps / 8 seconds when the flight ends) */
    int airborne;                   /* 10.7: between a lift-off event (0x171E2) and the next touchdown (0x16F6A) or crash */
    replay_t rep;                   /* the demo replay (replay.h, note 08 section 7): the stream and its reader; the mode handler of 0x7F lives on scr */
    int16_t carrier_vtx[FLIGHT_SHIP_SLOTS][3];   /* the carrier model's vertices, to rebuild the ship record for the demo's carrier */
    int32_t carrier_free[3];        /* free flight's carrier position (the world's entry 33) */
    uint16_t carrier_heading;       /* the carrier record's heading word: 0 in free flight, DEMO_CARRIER_HEADING in the demo */
    mover_t mv;                     /* the camera mover (mover.h): the approach's camera path, note 07 6.2, 08 12.14 C13 */
    uint8_t appr;                   /* the approach's external view is in force: SV_FREE or SV_TRAIN2 (screen.h), 0 in the cockpit; the camera is the mover's (vs.fixed_pos) */
    uint8_t appr_done;              /* the mover has set the camera mode 0 (V:4A95D ran out): the cockpit request is served in the next pass */
    int appr_row;                   /* V:4AA3C while the approach holds the dispatch value: 179 until the camera height falls below 0x24000 (12.14 C13, 06 start table); 0: the view's own row */
    int preview;                    /* the selection screens show the map overview (camera mode 4 and 5, note 08 5.6, 6.1 item 7): the world is drawn from the map camera, no aircraft */
    crash_phase_t crash;            /* the mission-state routine's crash states: NONE 0x12EFE, HOLD (the flip routine's loop, then A), DYING 0x138D8, RESUME (B, C1..C6) */
    int hold_pending;               /* the hand-over pass ran in the cockpit view: the hold begins at the end of this pass */
    int hold_steps;                 /* frozen game_steps elapsed in the hold */
    int resume;                     /* passes since A: 1 = B (0x13980), 2..6 = C1..C5, 7 = C6 (0x139C2 sets bit 6) */
    int allow;                      /* V:4A94F, the crash allowance */
    uint8_t jolt_call;              /* this pass's jolt call was not the cancelled one: R = 3 (weapons_t.jolt_called) */
    uint8_t gun_first;              /* the gun routine ran before the entity pass in the last frame (gun_precedes_entity_pass): scene.gun_first */
    uint8_t jolt;                   /* the lift-off jolt event of the last step (any record, note 08 8.3); see scene.jolt */
    int flash;                      /* the step in which pass A ran after a hold: the scene carries one uniform orange frame (17.17 (xii)) */
    uint16_t over_text;             /* V:4A802, the text id of the game-over screen */
    game_event_t queue[EVENT_QUEUE];   /* button events that arrived during the hold, applied at the start of pass A */
    int queued;
    int pointer_dy;                 /* the mouse's vertical change since the last step (flight_inputs_t.pointer_dy) */
    int crashes;                    /* the crash statistic: hand-overs with collisions enabled */
    /* the sky fill and the sea mesh (sky.h; note 04): the tables (absent in a synthetic asset set: no zone flags and no mesh), the stale words of the
     * polygon routine, the blackout strain, the latched zone flags and the scene block of the last step */
    sky_tables_t skyt;
    int has_sky;
    sky_state_t sky_st;             /* V:4AB7E: ONE state for the frame's camera call and the ball's call, never cleared in flight (note 04, "The crossing search") */
    int rear_age;                   /* frames since the cockpit rear view (head code 6) was entered, -1 outside it (the tiny colour, sky_tiny_pen) */
    sky_strain_t strain;
    uint8_t zone953, zone954;       /* V:4A953 (re-evaluated on even frame numbers), V:4A954 (odd) */
    unsigned sec_steps;             /* flight passes since the last mission-clock second */
    int fade_target;                /* V:4A959 as the game-frame routine 0x13166 wrote it in the last pass */
    game_sky_t sky_scene;
    /* milestone 5 (note 07 Part A) */
    mapmarks_t mm;                  /* the map view's marks and the records' stored +0x70/+0x71 (mapmarks.h) */
    uint8_t panel_block;            /* the panel block ran in the last flight pass (panel_block_runs) */
    uint8_t resume_skip;            /* the step of an outcome screen's resume: the first flight pass is the next step's (08 5.13 [V: flow7-pause-outcome]) */
    uint8_t in_approach;            /* the screen state was the approach cameras in the last step (the V:4A90F writers) */
    int8_t v4a90f;                  /* V:4A90F, the map routine's signed mask byte: 0xFF at the start-up routine 0x11962 (its store at 0x119AC, after its call of the reset 0x1D1BE; the demo's set-up stores nothing: a first demo keeps 0xFF), then the flow states' stores in their order (screen.h out_90f: the reset 0x1D25E of a return to the menu (screen.c menu_build_in; not the first menu build), 0x1258E, 0x125F8, 0x12756, 0x127AE, 0x12A76, 0x12AE2, 0x12B52, 0x12BD2, 0x12C56) and 1 at the approach's end (0x12D2A, here); no flight view or key writes it (note 06) */
    radar_t rd;                     /* the radar list, the threat bits, the designation and the HUD target data (radar.h) */
    /* milestone 6 (note 10): the sound library, the register writes of the last step and the master inputs (scene.h) */
    sound_t snd;
    game_audio_cmd_t audio[GAME_MAX_AUDIO_CMDS];
    int audio_count, audio_overflow;
    uint8_t audio_target, audio_set, audio_ramp;
    uint8_t pass_n;                 /* V:4A879: the pass counter (every game_step is a pass) */
    uint8_t snd_tick;               /* a seconds tick is due: taken after the pass's sound routine (hold_second) */
    weapons_t wp;                   /* the gun, the missiles, the countermeasures, the contact scan, the trail and the jolt (weapons.h) */
    entities_t ent;                 /* the 16 entity records and 16 secondary objects, the spawner, the world-update order, the dying path, the ejection (entities.h) */
    templates_t tpl;                /* tables/templates.bin (absent in a synthetic asset set) */
    int has_tpl;
    ai_t aitab;                     /* tables/ai.bin (absent: the note's constants stand in) */
    int has_ai;
    int eject_armed;                /* V:4A863: toggled by SHIFT-E */
    missions_t ms;                  /* the mission scripts' state (missions.h, note 08 12): survives a crash restart, cleared by the start of a mission */
    int cam_cell_x, cam_cell_z;     /* the camera's cell of the last frame (V:4AA04/06 in an external view, note 07 6.4) */
    int32_t scan_cam[3];            /* the camera position longs V:4ACF6, V:4ACFA, V:4ACFE: written by the external cameras only, so a cockpit view keeps the last external value (the draw scan reads them, 07 12.5) */
    uint8_t unpause_sound;          /* the pause byte was cleared by P: the first sound pass after it runs 4 blanks into its step (note 10 7.3) */
    uint8_t notice_pause;           /* P in the take-off notice or the wait state set the pause byte V:4A866 to 0xFF: the pause screen opens at the first flight pass (note 08 5.14 (3)) */
};

/* V:4A86D, the direct-draw flag ("the ring camera is on"; note 06 "What V:4A90F and V:4A86D hold", note 03 "External view"). The port keeps it twice, where the world's camera builder (vs.aim_on) and the entities'
 * camera routine (ent.camera_update) read it; every writer goes through here so that the two never differ. Writers in flight: 1 at the flight start (0x1D4EE), by the KP-Enter body 0x1E3B0, the M key
 * into the map 0x1ED58, a ring key and the mission script's end states 0x28698/0x28998; 0 by the fixed camera body 0x1E48E (Shift+KP-Enter) (entities_fixed_camera leaves the byte to its caller). The toggle 0x1EBD4 is the location key of the selection
 * screens (screen.c), the flow states' stores are the screens' (nothing draws the entity marks there). */
/* ---- the sound module's calls (note 10 section 8): see also the helpers before entities_begin ---- */
/* The noise wrapper 0x1A0DC; a missing noise sound posts the notice 0xFA and the hold 2 instead (10 3.4). */
static void snd_noise(game_t *g, int a, int b) {
    if (sound_event(&g->snd, SND_NOISE, a, b)) { g->ac.f.v4a968 = 0xFA; g->ac.f.v4a84f = 2; }
}
static void set_v4a86d(game_t *g, int v) { g->vs.aim_on = v != 0; g->ent.camera_update = v != 0; }

static void set_err(char *err, size_t errlen, const char *msg) {
    if (err && errlen) {
        size_t n = strlen(msg);
        if (n > errlen - 1) n = errlen - 1;
        memcpy(err, msg, n);
        err[n] = '\0';
    }
}

/* The flight model needs its tables and the vertices of the carrier and of the two aircraft models (note 05 "Extractor needs").
 * Without them the aircraft stays where the world module puts it (milestone-2 behaviour). */
static int build_flight(game_t *g, const game_loader_t *loader, char *err, size_t errlen) {
    {   /* tables that are absent (a milestone-2 asset set) leave the static aircraft; tables that exist but are refused (a wrong size, assets extracted
         * without the segment table) are an error: the game would otherwise start without a flight model and say nothing */
        uint8_t *probe = NULL;
        size_t plen = 0;
        int present = 0;
        if (loader->read(loader->user, "tables/flight_stick_player.bin", &probe, &plen) == 0) { loader->release(loader->user, probe); present = 1; }
        probe = NULL;
        if (loader->read(loader->user, "tables/flight_spans_flat.txt", &probe, &plen) == 0) { loader->release(loader->user, probe); present = 1; }
        if (!present) return 0;
    }
    if (flight_tables_load(&g->ftab, loader, err, errlen) != 0) return -1;
    int16_t cv[FLIGHT_SHIP_SLOTS][3];
    for (int k = 0; k < FLIGHT_SHIP_SLOTS; k++) {
        int32_t v[3];
        if (world_model_vertex(g->world, WORLD_CARRIER_ID, 0, (unsigned)k, v) != 0) return 0;
        for (int i = 0; i < 3; i++) cv[k][i] = g->carrier_vtx[k][i] = (int16_t)v[i];
    }
    int32_t p[3];
    if (world_position_entry(g->world, WORLD_ENTRY_CARRIER, p) != 0) return 0;
    flight_ship_t *s = &g->fenv.ship;
    for (int i = 0; i < 3; i++) g->carrier_free[i] = p[i];
    s->present = 1;
    s->w0 = 0x404C; s->cls = 0x20; s->group = 0x0E; s->l7d = 3;   /* the carrier record of note 05 section 16 */
    s->pos[0] = p[0]; s->pos[1] = 0; s->pos[2] = p[2];
    s->fine_x = (int16_t)((p[0] & 0x3FFFFF) >> 8); s->fine_z = (int16_t)((p[2] & 0x3FFFFF) >> 8); s->alt8 = 0;
    flight_ship_build(s, (const int16_t (*)[3])cv, FLIGHT_SHIP_SLOTS, 0, CARRIER_HEADING, 0);
    g->fenv.tab = &g->ftab;
    g->fenv.landmark_z = FLIGHT_LANDMARK_Z;
    g->fenv.view_cell_by_camera = 1;   /* note 04a 8a: the viewed cell the records 1 to 15 read is the camera's of the previous pass (flight_step does not write it) */
    g->has_flight = 1;
    return 0;
}

typedef char lists_vslots_match_ship_slots[LISTS_VSLOTS == FLIGHT_SHIP_SLOTS ? 1 : -1];
static void apply_camera(game_t *g);
static void lists_step(game_t *g);
static void lists_walk_step(game_t *g);
static void sky_frame(game_t *g);

/* The start (0x1D538: 0x1D4E2, 0x1D730 for the carrier, 0x1D8B8, 0x1D858; note 05 sections 15 and 17.14). flight_init and flight_restart place the
 * player from the ship record installed in the environment (the ship's matrix and attitude, copied at a carrier start). */
/* The carrier record for this flight (note 08 7.5): free flight's, or the demo's position and heading. The ship record is rebuilt with the heading (its matrix). */
static void place_carrier(game_t *g, int demo) {
    g->carrier_heading = demo ? DEMO_CARRIER_HEADING : CARRIER_HEADING;
    if (!g->has_flight || !g->fenv.ship.present) return;
    flight_ship_t *s = &g->fenv.ship;
    s->pos[0] = demo ? DEMO_CARRIER_POS : g->carrier_free[0]; s->pos[1] = 0; s->pos[2] = demo ? DEMO_CARRIER_POS : g->carrier_free[2];
    s->fine_x = (int16_t)((s->pos[0] & 0x3FFFFF) >> 8); s->fine_z = (int16_t)((s->pos[2] & 0x3FFFFF) >> 8); s->alt8 = 0;
    flight_ship_build(s, (const int16_t (*)[3])g->carrier_vtx, FLIGHT_SHIP_SLOTS, 0, g->carrier_heading, 0);
}

/* The carrier's +0xA4 slots: the contact test reads the record's slots as the entity drawer 0x21C44 last left them (note 05 open question 9, 07 8.10): the leading run of whichever block it drew (42 coarse,
 * up to 58 near), zero before the first draw. The port reads the draw store and does not recompute the near level. */
static void carrier_slots_sync(game_t *g) {
    if (!g->has_flight || !g->world || !g->fenv.ship.present) return;
    memcpy(g->fenv.ship.vtx, world_lists(g->world)->vslot[ENT_CARRIER], sizeof g->fenv.ship.vtx);
}

static void place_start(game_t *g) {
    carrier_slots_sync(g);
    flight_set_env(&g->fenv);
    flight_init(&g->ac, g->scr.aircraft == 0x10 ? FLIGHT_F16 : FLIGHT_FA18, g->start_runway ? FLIGHT_START_RUNWAY : FLIGHT_START_CARRIER);
    g->ac.f.v4a95e = g->scr.mode_byte;   /* the mode byte (note 08 5.10): 1 in free flight */
    g->ac.f.v4a968 = 0xFE;               /* the player start 0x1D564 posts the restart-the-engine notice (note 10 5.4); the sound routine is gated through the approach, so it stays pending until the first ungated pass (5.7 item 10) */
    if (g->world) memcpy(g->ac.f.vtx, world_lists(g->world)->vslot[0], sizeof g->ac.f.vtx);   /* the player's slots are the draw store's: what the last draw left, zero before the first (the restart keeps the crash attitude's) */
}

/* The start block of the location (V:4A900, note 08 5.6) in the environment: the runway start reads it. A location whose block the tables do not carry (or no tables) starts at block 0. */
static void select_start_block(game_t *g) {
    const int loc = g->scr.mode_byte == TRAINING_MODE_PART1 ? 0 : g->scr.location;   /* part 1: the block the reset block placed the player at (0), not the V:4A900 the dispatch stored afterwards (3) */
    g->start_runway = loc != 3;
    memset(&g->fenv.block, 0, sizeof g->fenv.block);
    const scenario_t *sc = g->scr.scn;
    if (loc >= 6 && loc <= 9) {   /* the location keys 7, 8, 9, 0 (note 08 5.6): the out-of-table starts, heading 0; key 0 reads the degrees word 3200, whose heading 3200 x 80 = 256,000 is stored as the low 16 bits 0xE800 (the start multiplies in a word shift, note 05 17.14 "The heading of the out-of-table starts, key 0"; flight.c) */
        static const int32_t X[4] = { 0x43300, 0xC8E00, 0xC00000, 0x1B86060 }, Z[4] = { 0x1FFFFFFF, 0x1FFFFFFF, 0xC00000, 0xAB6700 };
        const int k = loc - 6;
        g->fenv.block.present = g->fenv.block.direct = 1;
        g->fenv.block.x = X[k]; g->fenv.block.z = Z[k];
        g->fenv.block.degrees = (int16_t)(k == 3 ? 3200 : 0);   /* the word at 0x47B6E; the builders of 17.9 take the heading word 0xE800 as it is */
        return;
    }
    if (g->start_runway && sc && loc < sc->start_count && sc->start[loc].cell_x >= 0) {
        const scenario_start_t *b = &sc->start[loc];
        g->fenv.block.present = 1;
        g->fenv.block.cell_x = b->cell_x; g->fenv.block.cell_z = b->cell_z; g->fenv.block.n = b->n; g->fenv.block.ox = b->ox;
        g->fenv.block.oz = b->oz; g->fenv.block.fx = b->fx; g->fenv.block.fz = b->fz; g->fenv.block.degrees = b->degrees;
    }
}

/* ---- the mission-state routine of the crash flow (note 05 sections 11, 14, 15, 17.1, 17.17) ----------------------------------------
 * Runs at the start of every flight pass, BEFORE the world update (flight_step), as the original's state routine does (17.1). States: NONE is the
 * flight state 0x12EFE; HOLD and DYING are the wait state 0x138D8, which tests the view selector V:4A83D FIRST (17.17 rule 1): in the cockpit view it
 * waits for V:4A951 (the flip routine's hold; once the hold is over it is 0 and the state restarts), in an external view it waits for W0 bit 0x0400
 * of the player to be clear (the expiry of 17.3). RESUME is 0x13980 (B) and 0x139C2 (C1..C6). */

/* The restart, 0x1D538 (not 0x1D4E2: 17.17 rule 11): flight_restart applies the writes of rule 4 and keeps everything else; the step counter n and the
 * cockpit state live in cockpit.h and are untouched; the VIEW is not reset (the view selector V:4A83D, the head request and the ring stay). The
 * start writes P = Q = 72 (the mouse movement before it is wiped, rule 6), clears V:4A951 and the one-shot key events already taken in this pass
 * (the key handlers run before the dispatcher, the restart wipes what they set). It does not set V:4A984 bit 6: 0x139C2 does (pass C6). */
static void restart_flight(game_t *g) {
    carrier_slots_sync(g);
    flight_set_env(&g->fenv);
    flight_restart(&g->ac, g->start_runway ? FLIGHT_START_RUNWAY : FLIGHT_START_CARRIER);
    g->ent.rec[0].b71 = 0xFF;   /* the record routine 0x1D8B8 stores +0x71 = 0xFF (0x1D91C) */
    weapons_rearm(&g->wp);   /* 0x1D8B8 calls the refuel and re-arm routine 0x1F14A: stock 0x24, 500 rounds, 16 flares and chaff, records 1 to 3 cleared (note 05 17.17 rule 4, note 07 9.7) */
    flight_sync(&g->ac);
    sky_strain_reset(&g->strain);   /* 0x1D538: V:4A941 = 0 (V:4A959 = 15 at 0x138D8) */
    /* The restart calls the player-start record routine 0x1D8B8, which clears the F1 identification counter V:4A927 and the ECM switch V:4A96D again (note 08 12.5, note 07 12.3 and 9.5); the ECM switch survives a pause only. The
     * port models no other of its stores (V:4A96B, V:4A969). */
    g->rd.mission1 = 0;
    g->ac.f.v4a96d = 0;
    g->pointer_dy = 0;
    g->fin.pointer_set = 0; g->fin.pointer_value = 0; g->fin.preset = 0;
    g->fin.gear_toggle = g->fin.hook_toggle = g->fin.brake_toggle = g->fin.eject = 0;
    world_lists_request(g->world, 0xFF);   /* V:4A910 = 0xFF: the start is a display-list request site */
}

/* The hand-over (the crash branch of 0x12EFE): W0 bit 0x0200 set; the crash pass clears the ejection nibble first and the hand-over runs as for any crash (note 07 6.5, note 05 Q19 corrected). */
static void crash_handover(game_t *g, int external) {   /* external: V:4A83D as the previous pass left it (a key polled in this pass is applied after the dispatcher, 17.17 rule 1b) */
    flight_state_t *f = &g->ac.f;
    if (!f->v4a844) {   /* collisions disabled (training): the crash is only cleared and the notice 0x4021 (entry 33) posted */
        f->w0 &= (uint16_t)~W0_CRASH_BIT;
        cockpit_post(&g->ck, &g->ac, 0x4021);
        return;
    }
    g->crashes++;
    logbook_add(&g->log, LB_HANDOVERS, 1);   /* 0x12F44: not gated by V:4A848, the demo counts too (7.6, 10.7); 10.2: +0x10 counts the hand-overs with collisions on (write-only in the original) */
    f->v4a87d = 1;
    f->v4a982 &= (uint16_t)~0x100u;
    f->v4a984 &= (uint16_t)~A984_SIM_RUNNING;
    f->w0 &= (uint16_t)~W0_CRASH_BIT;
    if (!external) {
        /* the cockpit view only (V:4A83D tested at 0x12F80): V:4A952 = 1 (immune), the vertical velocity zeroed, V:4A951 = 40: the hold begins at the
         * end of this pass, after its single step (17.17 rule 3: no extra pass). The sound: all four voices stop (0x115F6), then the crash noise 0x1A0DC(0x3F, 0x78) (note 10 7.3, 8.2); in an
         * external view the hand-over stops nothing and plays nothing */
        sound_stop_all(&g->snd);
        snd_noise(g, 0x3F, 0x78);
        f->v4a952 = 1;
        f->vel[1] = 0;
        g->hold_pending = 1;
    } else {
        g->crash = CR_DYING;   /* 0x138D8 waits for W0 bit 0x0400 to clear (the expiry, 17.3) */
    }
}

/* The game-over path (0x139F0, 0x13A36; 17.17 rule 7, note 08 5.12): the text id V:4A802, then GAME_MODE_GAMEOVER; the screen module types the text out and leaves
 * 15 steps after A (A's own step included in the caller's return). The steps do not depend on a pause (note 05 17.17 rule 7, followup-pause-gameover, 3 runs): the game-over states do not read the pause
 * byte V:4A866, their timers are counted in passes, and P in those states only toggles the byte (no pause screen opens: the flight state that would open it is not the current state), so a pause queued in
 * the hold changes nothing here and the same 20 steps follow from the restart pass (tests/test_crash.c). */
static void enter_game_over(game_t *g) {
    g->crash = CR_NONE;
    g->over_text = (g->ac.f.cls == 0x10) ? 0x62 : 0x63;   /* V:4A802: the F-16's text when V:4A901 = 0x10, else the F/A-18's */
    g->mode = GAME_MODE_GAMEOVER;
    screen_gameover(&g->scr, g->over_text);   /* the text types out, the leave is 15 steps after A (screen.c) */
}

/* Pass A (the dispatcher of the wait state restarts): 0x1D538, then V:4A94F is decremented. Returns 0 when the allowance is spent: the game-over path
 * 0x139F0 follows and the pass runs NOTHING else (17.17 (xiii): the record is rewritten, no world update, no list rebuild, no generator or arbiter, n
 * does not turn). */
static int restart_pass_a(game_t *g) {
    restart_flight(g);
    g->crash = CR_RESUME;
    g->resume = 1;
    if (--g->allow <= 0) { enter_game_over(g); return 0; }
    return 1;
}

/* Pass B and the C passes (0x13980, 0x139C2). */
static void resume_pass(game_t *g) {
    flight_state_t *f = &g->ac.f;
    switch (g->resume) {
    case 1:   /* B: 0x13980 */
        f->v4a952 = 0;
        if (g->vs.level == 0) { g->vs.head = 0; apply_camera(g); }   /* the head is recentred in the cockpit view only */
        restart_flight(g);
        f->w2 |= W2_CONTACT_BIT;
        g->resume = 2;
        break;
    case 7:   /* C6: 0x139C2 sees V:4AB8E negative */
        f->v4a984 |= A984_SIM_RUNNING;
        f->v4a968 = 0xFE;
        g->crash = CR_NONE;
        g->resume = 0;
        break;
    default:  /* C1..C5: the wait */
        g->resume++;
        break;
    }
}

/* Returns 1 when this pass runs on (the generator, the world update, the arbiter, n), 0 when the pass is the game-over restart. */
static int mission_state(game_t *g) {
    flight_state_t *f = &g->ac.f;
    const uint8_t prev_view = g->vs.level != 0;   /* the state routine tests the live selector V:4A83D: the view as the previous pass left it (a switch request is applied after it, by view_switch) */
    switch (g->crash) {
    case CR_HOLD:
        g->flash = 1;        /* the hold is over: the orange frame (17.17 (xii)) */
        return restart_pass_a(g);   /* only called when the hold is over: V:4A951 is 0 */
    case CR_DYING:
        /* an external view waits for the expiry (pass N+16 for a finalisation in step N, 17.3); a pass that sees the cockpit view waits for V:4A951,
         * which is 0 here, so it restarts at once (the player switched back to the cockpit while dying) */
        if (!prev_view || !(f->w0 & W0_DYING_BIT)) return restart_pass_a(g);
        return 1;
    case CR_RESUME:
        resume_pass(g);
        return 1;
    default:
        if (f->w0 & W0_CRASH_BIT) {
            f->gear &= 0xF0;   /* note 07 6.5 (A8; V: followup-combat-crash-after-eject.txt): the crash pass clears the ejection nibble first, then the hand-over runs as for any crash (the seat is freed at the restart) */
            crash_handover(g, prev_view != 0);
        }
        return 1;
    }
}

static int training_mode_handler(void *user, uint8_t mode);
static int mission_bearing(void *user);
/* The modes this module builds for the screen machine (screen.h, screen_set_mode_handler): the qualification, mode 9 (note 08 section 9). */
static int mode_handler(void *user, uint8_t mode) {
    game_t *g = (game_t *)user;
    if (mode == 9) return screen_qualification_start(&g->scr);
    if (mode >= 3 && mode <= 8) {   /* without a flight model there is no mission (the screen returns to the menu, as for the trainings) */
        if (!g->has_flight) return 0;
        /* the missions F1 to F6 (note 08 13.1): F1 starts on the airfield (V:4A900 = 0), F2 to F6 on the carrier; the briefing chain is the screen module's (answer 3) */
        g->scr.location = (uint8_t)(mode == 3 ? 0 : 3);
        g->scr.aircraft = 0x11;
        missions_start(&g->ms, mode, g->scr.location);   /* V:4A83A, V:4A927, the hand-over flag, the delay and the key lock are cleared by the placement and the player start (12.13 item 1) */
        return 3;
    }
    return training_mode_handler(user, mode);   /* the chain ends in the trainings (2, 0x7D) */
}

/* the start-up routine 0x103CE (10 6.2, 7.1): level and target 0x3F0000, the flag cleared, then with the music loaded (V:4AC13 bit 7) the start-up phrase 0x19CE6(0x32): the music, and the flag 2;
 * without it (a disk whose music files are missing, or too little memory: 2.3) the mission tone pair 0x19D7A(0x0F) at volume 15, and the flag stays 0 (the key clicks are not muted, 0x10548-0x10564) */
static void snd_start_up(game_t *g) {
    sound_master(&g->snd, 63, 63, 0);
    if (g->snd.en13 & 0x80) sound_event(&g->snd, SND_PHRASE, 0, 0x32);
    else sound_event(&g->snd, SND_PHRASE, 1, 0x0F);
}

game_t *game_create(const game_loader_t *loader, char *err, size_t errlen) {
    if (!loader || !loader->read || !loader->release) {
        set_err(err, errlen, "game_create: loader with read and release callbacks is required");
        return NULL;
    }
    int ver = core_assets_read_version(loader);
    if (ver < 0) {
        set_err(err, errlen, "game_create: VERSION is missing or malformed in the assets");
        return NULL;
    }
    if (ver != CORE_ASSETS_VERSION) {
        char msg[96];
        msg[0] = '\0';
        core_assets_append(msg, sizeof msg, "assets were made by extractor version ");
        core_assets_append_u32(msg, sizeof msg, (uint32_t)ver);
        core_assets_append(msg, sizeof msg, ", need ");
        core_assets_append_u32(msg, sizeof msg, CORE_ASSETS_VERSION);
        set_err(err, errlen, msg);
        return NULL;
    }
    game_t *g = (game_t *)calloc(1, sizeof *g);
    if (!g) { set_err(err, errlen, "game_create: out of memory"); return NULL; }
    g->v4a90f = -1;   /* the start-up routine 0x11962 stores V:4A90F = 0xFF */
    g->pix_count = core_assets_load_index(loader, "pix/index.txt", g->pix, GAME_MAX_ASSETS);
    if (g->pix_count < 1) {
        free(g);
        set_err(err, errlen, "game_create: cannot read pix/index.txt from the assets");
        return NULL;
    }
    g->loader = *loader;
    {   /* the front-end screens: text records and scenario tables (absent: the screens run without text); a present file that is refused is an error */
        char serr[200];
        serr[0] = '\0';
        if (screen_init(&g->scr, loader, serr, sizeof serr) != 0) {
            char msg[260];
            msg[0] = '\0';
            core_assets_append(msg, sizeof msg, "game_create: ");
            core_assets_append(msg, sizeof msg, serr);
            set_err(err, errlen, msg);
            free(g);
            return NULL;
        }
    }
    {   /* the training tables (tables/training.bin): absent leaves the note's defaults, a present file that is refused is an error */
        uint8_t *probe = NULL;
        size_t plen = 0;
        training_t tt;
        char terr[160];
        terr[0] = '\0';
        if (loader->read(loader->user, "tables/training.bin", &probe, &plen) == 0) {
            loader->release(loader->user, probe);
            if (assets_load_training(loader, &tt, terr, sizeof terr) != 0) {
                char msg[260];
                msg[0] = '\0';
                core_assets_append(msg, sizeof msg, "game_create: ");
                core_assets_append(msg, sizeof msg, terr);
                set_err(err, errlen, msg);
                screen_destroy(&g->scr);
                free(g);
                return NULL;
            }
            training_init(&g->tr, &tt);
        } else training_init(&g->tr, NULL);
    }
    {   /* the template table and the AI tables of milestone 5: absent in a synthetic or older asset set; a present file that is refused is an error */
        uint8_t *probe = NULL;
        size_t plen = 0;
        char cerr[200];
        cerr[0] = '\0';
        int bad = 0;
        if (loader->read(loader->user, "tables/templates.bin", &probe, &plen) == 0) {
            loader->release(loader->user, probe);
            if (assets_load_templates(loader, &g->tpl, cerr, sizeof cerr) != 0) bad = 1; else g->has_tpl = 1;
        }
        probe = NULL;
        if (!bad && loader->read(loader->user, "tables/ai.bin", &probe, &plen) == 0) {
            loader->release(loader->user, probe);
            if (assets_load_ai(loader, &g->aitab, cerr, sizeof cerr) != 0) bad = 1; else g->has_ai = 1;
        }
        if (bad) {
            char msg[300];
            msg[0] = '\0';
            core_assets_append(msg, sizeof msg, "game_create: ");
            core_assets_append(msg, sizeof msg, cerr);
            set_err(err, errlen, msg);
            screen_destroy(&g->scr);
            free(g);
            return NULL;
        }
    }
    {   /* the head step table (tables/head_step.bin): absent in a synthetic asset set; a present file that is refused is an error */
        uint8_t *probe = NULL;
        size_t plen = 0;
        char herr[160];
        herr[0] = '\0';
        if (loader->read(loader->user, "tables/head_step.bin", &probe, &plen) == 0) {
            loader->release(loader->user, probe);
            if (assets_load_head_step(loader, g->head_step, herr, sizeof herr) != 0) {
                char msg[260];
                msg[0] = '\0';
                core_assets_append(msg, sizeof msg, "game_create: ");
                core_assets_append(msg, sizeof msg, herr);
                set_err(err, errlen, msg);
                screen_destroy(&g->scr);
                free(g);
                return NULL;
            }
        }
    }
    {   /* the sound object table (tables/sounds.bin, extractor 25): absent leaves the library without objects (every wrapper does nothing); a present file that is refused is an error */
        uint8_t *probe = NULL;
        size_t plen = 0;
        if (loader->read(loader->user, "tables/sounds.bin", &probe, &plen) == 0) {
            sound_table_t st;
            char serr[160];
            loader->release(loader->user, probe);
            serr[0] = '\0';
            if (assets_load_sounds(loader, &st, serr, sizeof serr) != 0) {
                char msg[260];
                msg[0] = '\0';
                core_assets_append(msg, sizeof msg, "game_create: ");
                core_assets_append(msg, sizeof msg, serr);
                set_err(err, errlen, msg);
                screen_destroy(&g->scr);
                free(g);
                return NULL;
            }
            sound_init(&g->snd, &st);
        } else sound_init(&g->snd, NULL);
    }
    entities_init(&g->ent, g->has_tpl ? &g->tpl : NULL, g->has_ai ? &g->aitab : NULL, g->scr.scn);
    entities_attach_player(&g->ent, &g->ac);
    weapons_init(&g->wp, &g->ent);
    g->wp.sound = &g->snd;
    g->scr.snd = &g->snd;
    snd_start_up(g);
    g->scr.log = &g->log;   /* the flight log: a new player until game_log_import says otherwise */
    g->log.bogey_deg = -1;  /* no bearing looked up yet (the vector notice 0x58) */
    screen_set_mode_handler(&g->scr, mode_handler, g);
    screen_set_mission_hooks(&g->scr, mission_bearing, NULL, g);
    g->mode = g->scr.mode;   /* the credits are the first screen (note 08 5.2) */
    /* The world is optional so that a splash-only asset set still starts. When
     * models/index.txt exists, everything it names must load (an error names the file). */
    {
        uint8_t *probe = NULL;
        size_t plen = 0;
        if (loader->read(loader->user, "models/index.txt", &probe, &plen) == 0) {
            loader->release(loader->user, probe);
            g->world = world_load(loader, err, errlen);
            if (!g->world) { screen_destroy(&g->scr); free(g); return NULL; }
        }
    }
    if (g->world) {
        char ferr[256];
        ferr[0] = '\0';
        if (build_flight(g, loader, ferr, sizeof ferr) != 0) {
            char msg[300];
            msg[0] = '\0';
            core_assets_append(msg, sizeof msg, "game_create: ");
            core_assets_append(msg, sizeof msg, ferr);
            set_err(err, errlen, msg);
            world_destroy(g->world);
            screen_destroy(&g->scr); free(g);
            return NULL;
        }
    }
    {   /* the sky tables: absent ones (a milestone-2 asset set) leave the zone flags and the sea mesh off; present ones must be whole */
        uint8_t *probe = NULL;
        size_t plen = 0;
        if (loader->read(loader->user, "tables/sea_zone.bin", &probe, &plen) == 0) {
            char serr[160];
            loader->release(loader->user, probe);
            serr[0] = '\0';
            if (sky_tables_load(&g->skyt, loader, serr, sizeof serr) != 0) {
                char msg[200];
                msg[0] = '\0';
                core_assets_append(msg, sizeof msg, "game_create: ");
                core_assets_append(msg, sizeof msg, serr);
                set_err(err, errlen, msg);
                world_destroy(g->world);
                screen_destroy(&g->scr); free(g);
                return NULL;
            }
            g->has_sky = 1;
        }
    }
    g->fade_target = 15;
    cockpit_init(&g->ck, NULL);
    {   /* the warning table (assets/cockpit/warnings.bin, 54 x 28 bytes): the durations, classes and sound flags the arbiter reads */
        uint8_t *buf = NULL;
        size_t blen = 0;
        if (loader->read(loader->user, "cockpit/warnings.bin", &buf, &blen) == 0) {
            if (blen == COCKPIT_WARN_ENTRIES * COCKPIT_WARN_SIZE) cockpit_init(&g->ck, buf);
            loader->release(loader->user, buf);
        }
    }
    {   /* the demo stream (absent in a synthetic asset set: menu key 1 is ignored); a present file that is refused is an error */
        char rerr[200];
        rerr[0] = '\0';
        if (replay_init(&g->rep, &g->scr, loader, rerr, sizeof rerr) != 0) {
            char msg[260];
            msg[0] = '\0';
            core_assets_append(msg, sizeof msg, "game_create: ");
            core_assets_append(msg, sizeof msg, rerr);
            set_err(err, errlen, msg);
            game_destroy(g);
            return NULL;
        }
    }
    return g;
}

const struct world *game_world(const game_t *g) { return g ? g->world : NULL; }

void game_destroy(game_t *g) {
    if (!g) return;
    replay_destroy(&g->rep);
    screen_destroy(&g->scr);
    world_destroy(g->world);
    free(g);
}

/* ---- the flight log (note 08 section 10) ---- */
int game_log_import(game_t *g, const uint8_t *buf, size_t len) {
    if (!g) return GAME_LOG_BAD_SHORT;
    int rc = GAME_LOG_OK;
    const uint8_t *rec = NULL;
    size_t rlen = 0;
    if (buf) {
        rc = logbook_file_unwrap(buf, len, &rec, &rlen);   /* 10.3 rule 2: a refused container is the zero record */
        if (rc != 0) rec = NULL;
    }
    logbook_load_record(&g->log, rec, rec ? rlen : 0);
    g->scr.log_welcome = 1;                                 /* 10.4: the welcome and the callsign entry follow the log-disk screen */
    return rc;
}
int game_log_export(const game_t *g, uint8_t *buf, size_t cap) {
    if (!g || !buf) return -1;
    const size_t n = logbook_file_encode(&g->log, buf, cap);
    return n ? (int)n : -1;
}
void game_log_ack(game_t *g) { if (g) g->log.save_req = 0; }

void game_set_debug_freecam(game_t *g, int on) { if (g) g->freecam = on != 0; }
void game_set_start(game_t *g, int runway) { if (g) { g->start_runway = runway != 0; g->scr.location = (uint8_t)(runway ? 0 : 3); } }
void game_set_logdisk_result(game_t *g, int r) { if (g) screen_set_logdisk_result(&g->scr, r); }
void game_clock_second(game_t *g) { if (g) g->seconds++; }
int game_demo_step(const game_t *g) { return g && g->rep.active ? g->rep.step : -1; }
aircraft_t *game_debug_aircraft(game_t *g) { return g && g->has_flight ? &g->ac : NULL; }
struct trainer *game_debug_trainer(game_t *g) { return g && g->has_flight ? &g->tr : NULL; }
struct entities *game_debug_entities(game_t *g) { return g && g->has_flight ? &g->ent : NULL; }
struct weapons *game_debug_weapons(game_t *g) { return g && g->has_flight ? &g->wp : NULL; }
int game_debug_look(const game_t *g) { return g ? g->vs.look : 0; }
int game_debug_ring_heading(const game_t *g) { return g ? (int)g->vs.heading : 0; }
void *game_debug_radar(game_t *g) { return g && g->has_flight ? (void *)&g->rd : NULL; }
void *game_debug_sound(game_t *g) { return g ? (void *)&g->snd : NULL; }
void game_sound_objects_available(game_t *g, const uint8_t present[37]) {
    if (!g || !present) return;
    sound_objects_available(&g->snd, present);
    g->audio_count = 0;                     /* the writes of the first start-up are void: the library starts again */
    snd_start_up(g);
}
const game_audio_cmd_t *game_audio_commands(const game_t *g, int *count) {
    if (count) *count = g ? g->audio_count : 0;
    return g ? g->audio : NULL;
}
void *game_debug_world(game_t *g) { return g ? (void *)g->world : NULL; }
void game_random_seed(game_t *g, uint32_t seed) { if (g) screen_random_seed(&g->scr, seed); }
int game_crash_state(const game_t *g, int *hold_blanks_left) {
    if (!g) return 0;
    if (hold_blanks_left) {
        int left = CRASH_HOLD_BLANKS - g->hold_steps * 50 / FLIGHT_STEP_HZ;
        *hold_blanks_left = g->crash == CR_HOLD && g->hold_steps <= CRASH_HOLD_STEPS ? (left > 0 ? left : 0) : 0;
    }
    return g->crash == CR_NONE ? GAME_CRASH_NONE : g->crash == CR_DYING ? GAME_CRASH_DYING : g->crash == CR_RESUME ? GAME_CRASH_RESUME : GAME_CRASH_HOLD;
}
const struct missions *game_debug_missions(const game_t *g) { return g ? &g->ms : NULL; }
const struct screen *game_debug_screen(game_t *g) { return &g->scr; }
int game_debug_v4a90f(const game_t *g) { return g->v4a90f; }
int game_debug_panel_counter(const game_t *g) { return g->ck.panel; }
int game_debug_redraw_counter(const game_t *g) { return g->ck.redraw; }
void game_debug_set_zoom(game_t *g, int zoom) { g->ck.zoom = (uint8_t)zoom; }
int game_debug_strain_acc(const game_t *g) { return g->strain.acc; }
const struct mover *game_debug_mover(const game_t *g) { return &g->mv; }
int game_debug_approach_row(const game_t *g) { return g->appr_row; }
int game_debug_camera_mode(const game_t *g) { return g->vs.mode; }
int game_debug_view_level(const game_t *g) { return g->vs.level; }   /* V:4A83D as a signed byte (0 the cockpit, -1 the KP-ENTER chase, 1..4 the level keys) */
int game_debug_zoom(const game_t *g) { return g->ck.zoom ? g->ck.zoom : 0x80; }
int game_flash(const game_t *g) { return g ? g->flash : 0; }
int game_crash_allowance(const game_t *g) { return g ? g->allow : 0; }
int game_crash_count(const game_t *g) { return g ? g->crashes : 0; }

/* ---- the views (docs/re/06-cockpit-hud.md "Head views" and "View kinds, settled", 04a "The 23 camera and view request sites") ----------------------------------------
 * The camera modes V:4A966 = 1 to 4 and 6 are the front end's approach cameras (note 07 6.1: no flight view or key writes them), so in a flight they stay 0. The glance byte
 * V:4A869 of the cockpit view is dead (note 06 "The glance byte V:4A869": no writer; it stays 0). (The fixed-camera view of Shift+KP-Enter, 0x1E3EE, is modelled: fixed_camera below.) */

static game_view_t current_view(const game_t *g) {
    if (g->vs.level == 0) return GAME_VIEW_COCKPIT;
    if (g->vs.map) return GAME_VIEW_TOWER;   /* the map is drawn when V:4A865 > 0 and V:4A83D != 0 */
    return g->vs.ring == 4 ? GAME_VIEW_CHASE : GAME_VIEW_EXTERNAL;
}
static int ext_view(const game_t *g) { return g->vs.level != 0; }
static int map_view(const game_t *g) { return g->vs.level != 0 && g->vs.map; }

/* The cockpit camera words of a head code (note 06 "Head views" item 1, note 03): the composition 0x325B8 of the record matrix with the head triple. A record without flight
 * tables (the milestone-2 asset sets) cannot compose: C (a missing-asset fallback), the words are the record's own with the triple's yaw added (exact for a level aircraft only). */
static int16_t trig_word(void *user, uint32_t addr) { int known; return flight_image_word((const flight_tables_t *)user, addr, &known); }

static aircraft_t *viewed_ac(game_t *g);
static void head_camera_words(game_t *g) {
    aircraft_t *va = viewed_ac(g);
    const flight_state_t *f = &va->f;
    int t[3];
    world_viewstate_t *vs = &g->vs;
    vs->head_composed = 0;
    if (vs->level != 0 || !world_head_triple(f->cls, vs->head, (int16_t)f->pitch, t)) return;
    if (g->has_flight) flight_head_camera(va, &g->ftab, t[0], t[1], t[2], vs->head_words);
    else { vs->head_words[0] = t[0] ? t[0] : f->pitch; vs->head_words[1] = (f->heading + t[1]) % 28800; vs->head_words[2] = f->roll; }
    vs->head_composed = 1;
}

/* ---- the training parts (training.c, note 08 section 8) --------------------------------------------------------------------------------------------------
 * The module owns the instructor (record 4) and the lessons; this block is the glue: the mode handler for the menu items 3 and 4, the start, the camera on the
 * instructor in part 1, the keys of 8.6 and 8.8, and the forwarding of the module's posts and requests. */
static void request_lists(game_t *g);

/* The record the camera follows (note 07 6.1): the viewed record V:4A994, the player's index 0 unless the training part 1 views the lead (4) or the ejection views the seat (1 to 3, 5);
 * a viewed record that is no longer valid is the player again (0x14392). */
static int viewed_index(const game_t *g) {
    const int v = entities_viewed(&g->ent);
    if (v == 0) return 0;
    return (entities_ac_c(&g->ent, v)->f.w0 & 0x0040) ? v : 0;
}
static aircraft_t *viewed_ac(game_t *g) { return entities_ac(&g->ent, viewed_index(g)); }
static const aircraft_t *viewed_ac_c(const game_t *g) { return entities_ac_c(&g->ent, viewed_index(g)); }
/* The seat (or the parachute) is the viewed record in the cockpit view: its frame is drawn, the HUD step is 50 (offset 800) and the window row 167 (note 07 6.4, "The seat view"). */
static int seat_view(const game_t *g) { const int v = viewed_index(g); return v && g->vs.level == 0 && (entities_ac_c(&g->ent, v)->f.cls & 0xF0) == 0x30; }

/* Menu items 3 (mode byte 2, part 1: the runway start of airfield 1, V:4A900 = 0 as the mission start leaves it, the player dormant, briefing id 9) and 4 (mode byte 0x7D, part 2: airfield 3, briefing id 10); the aircraft is the
 * F/A-18 (the defaults of 6.1). The machine starts the flight through the briefing flow (screen.h, answer 2). Without flight data the item is not built. */
static int training_mode_handler(void *user, uint8_t mode) {
    game_t *g = (game_t *)user;
    if ((mode != TRAINING_MODE_PART1 && mode != TRAINING_MODE_PART2) || !g->has_flight) return 0;
    g->scr.location = mode == TRAINING_MODE_PART1 ? 3 : 2;   /* 08 6.1 item 6: the dispatch stores V:4A900 = 3 for every mode and the mode 2 branch leaves it; part 2 sets 2. The player of part 1 still stands on airfield 1's runway: the reset block placed it at start block 0 (0x1D256, 0x1D342) before the dispatch stored the 3 (select_start_block) */
    g->scr.aircraft = 0x11;
    g->scr.lead_text = mode == TRAINING_MODE_PART1 ? 9 : 10;
    return 2;
}

/* The mission start of a training (called at the end of start_player, after the player's placement): the instructor is placed, part 1 views it from outside. */
static void training_bytes_in(game_t *g) { g->tr.src_b70 = g->ent.rec[0].b70; g->tr.src_b71 = g->ent.rec[0].b71; }   /* the player's +0x70/+0x71 for part 2's copy */
static void training_bytes_out(game_t *g) {   /* the placement's +0x70/+0x71 of record 4 (the head overwrite of part 1, the copy of part 2) */
    if (!g->tr.part || !g->tr.placed_new) return;
    g->tr.placed_new = 0;
    g->ent.rec[ENT_TRAINING_LEAD].b70 = g->tr.b70; g->ent.rec[ENT_TRAINING_LEAD].b71 = g->tr.b71;
}
static void training_begin(game_t *g) {
    const int m = g->scr.mode_byte;
    entities_attach_external(&g->ent, ENT_TRAINING_LEAD, NULL);
    if (!g->has_flight || (m != TRAINING_MODE_PART1 && m != TRAINING_MODE_PART2)) { g->tr.part = 0; return; }
    flight_set_env(&g->fenv);
    training_bytes_in(g);
    training_start(&g->tr, m, &g->ac);
    training_bytes_out(g);
    entities_attach_external(&g->ent, ENT_TRAINING_LEAD, g->tr.part ? &g->tr.inst : NULL);   /* record 4 is the instructor while a training part runs */
    if (g->tr.part) g->vs.look = -1;   /* the mode branch of the training starts (0x12198 for mode 2, 0x12212 for mode 0x7D) stores 0xFF in the look-at byte V:4A86C: the aim snaps to the viewed record every frame (note 08 6.1 item 7, note 06 item 11) */
    if (g->tr.part) weapons_trail_clear(&g->wp);   /* the training placement 0x2CFDA (tail 0x2D09A) empties the trail ring (note 07 13.1) */
    if (g->tr.part == 1) {
        g->ac.f.v4a994 = 4; g->ac.f.v4a996 = 0x800;   /* the viewed record is 4, offset 0x800 */
        g->vs.level = 1; g->vs.ring = 3;               /* V:4A83D = 1, V:4A96A = 3 */
        g->hud_step = 50;                              /* 06 start table, 12.14 [V: flow7-training-2]: step 50 in the external view from the dispatch through the first flight pass */
    }
}

/* The module's outputs: the posts go to the message line, the advance's camera lines to the view state, a moved instructor to the display-list request. */
static void training_flush(game_t *g) {
    trainer_t *t = &g->tr;
    training_bytes_out(g);
    for (int i = 0; i < t->npost; i++) cockpit_post(&g->ck, &g->ac, t->posts[i]);
    if (t->out_ring >= 0) g->vs.ring = t->out_ring;
    if (t->out_view >= 0 && ext_view(g)) g->vs.level = t->out_view;
    if (t->out_redraw) request_lists(g);
    training_clear_outputs(t);
}

/* The player's pre-step 0x270A0 (part 2's re-position; it acts in a paused pass too) and the instructor's pass after the player's step. */
static void training_player_g(game_t *g) {
    if (g->tr.part != 2) return;
    training_bytes_in(g);
    training_player_pass(&g->tr, &g->ac);
    training_flush(g);
}
static void training_instructor_g(game_t *g) {
    if (!g->tr.part) return;
    g->tr.paused = 0;
    training_step(&g->tr, &g->ac);
    training_flush(g);
    if (g->scr.state == SCR_FLIGHT && training_result(&g->tr) == TRAINING_LOST) screen_mission_end(&g->scr, 0xFE, (uint8_t)(g->ck.n & 1));   /* 8.9: the end request when the lead is lost */
}

/* The keys of the trainings (8.6, 8.8). Returns 1 when the event is consumed. Part 1 skips the flight controls and the digit and function keys (the player is dormant and
 * the throttle is fixed): only RETURN, the keypad, the brackets, P, ESC and SHIFT-ESC act. Part 2: RETURN re-positions (or posts the notice), SPACE is also the trigger,
 * S the smoke key. */
static void snd_key_beep(game_t *g);
static int training_key(game_t *g, const game_event_t *ev) {
    if (!g->tr.part) return 0;
    if (ev->kind == GAME_EV_AXIS) return g->tr.part == 1;
    if (ev->kind != GAME_EV_BUTTON) return 0;
    switch (ev->code) {
    case GAME_BTN_SELECT_WEAPON: case GAME_BTN_RETURN:   /* the frontend sends SELECT_WEAPON in flight */
        if (ev->value && training_key_return(&g->tr, &g->ac)) snd_key_beep(g);   /* 0x1E8C2: the beep when the RETURN sets the request (mode 2, or mode 0x7D with V:4A852 negative); the not-ready notice makes none */
        return 1;
    case GAME_BTN_FIRE:
        if (ev->value && g->tr.part == 2) training_key_fire(&g->tr, &g->ac);
        return g->tr.part == 1;
    case GAME_BTN_SMOKE:
        if (ev->value) { if (g->tr.part == 2) snd_key_beep(g); training_key_smoke(&g->tr); }   /* 0x1ED88: mode 0x7D only; the beep first, then the smoke toggle */
        return 1;
    case GAME_BTN_KEYPAD: case GAME_BTN_KEYPAD_UP: case GAME_BTN_LBRACKET: case GAME_BTN_RBRACKET: case GAME_BTN_PAUSE: case GAME_BTN_ESC: case GAME_BTN_SHIFT_ESC:
        return 0;
    default:
        return g->tr.part == 1;
    }
}

/* ---- the entities (entities.c, note 07 Part A) ----------------------------------------------------------------------------------------------------
 * The glue: the hooks the entity module calls (the instructor's pass for record 4 in the trainings, the aim, the position blocks), the mission-start placement, the per-pass
 * call after the player's step, and what the ejection asks of the flow (request 2 -> the end code 0xF0). */
static void request_lists(game_t *g);
static void training_instructor_g(game_t *g);

/* The redraw helper 0x1EFF4 (note 07 12.1): it stores 3 in fourteen counters, one for each HUD drawer. The port keeps the ones whose drawers it has: the panel picture V:4A8EE (it gates the panel block in an external
 * view), the scope V:4A8EF (R) and the message line V:4A8F4; the others (V:4A8F1..F3, F5..F8, FB..FD) are the redraw counters of elements the frontend draws from the scene on every frame, so the picture they
 * produce is the same without them, and V:4A8F9 is never read. */
static void redraw_helper(game_t *g) {
    cockpit_redraw_request(&g->ck);   /* V:4A8EF and V:4A8EE */
    g->rd.line_redraw = 3;            /* V:4A8F4: the message line draws its old text again */
    if (!g->ac.f.v4a878) g->wp.jolt_row = 0;   /* the helper's tail (0x1F04A to 0x1F058) clears the window offsets V:4A990 (and V:4A9D0) unless the jolt byte V:4A878 is non-zero */
}

static void ent_hook_training(void *user) { training_instructor_g((game_t *)user); }
static void ent_hook_aim_dir(void *user, int32_t vx, int32_t vy, int32_t vz, int32_t *pitch, int32_t *heading) {
    game_t *g = (game_t *)user;
    world_viewstate_t vs = g->vs;
    vs.image_word = g->has_flight ? trig_word : NULL; vs.image_user = &g->ftab;
    int32_t tp = 0, th = 0;
    world_aim_target(&vs, vx, vy, vz, &tp, &th);
    const int snap = g->vs.mode == 0 && !g->vs.first;   /* the first call snaps (V:4A966 and V:4A85E both 0) and sets V:4A85E */
    if (snap) g->vs.first = 1;
    if (snap) { *pitch = tp; *heading = th; }
    else { *pitch = world_aim_step(*pitch, tp, WORLD_AIM_LIMIT); *heading = world_aim_step(*heading, th, WORLD_AIM_LIMIT); }
}
static int32_t ent_hook_aim(void *user, int32_t dx, int32_t dz) {
    game_t *g = (game_t *)user;
    world_viewstate_t vs = g->vs;
    int32_t pitch = 0, heading = 0;
    vs.image_word = g->has_flight ? trig_word : NULL; vs.image_user = &g->ftab;
    world_aim_target(&vs, dx, 0, dz, &pitch, &heading);
    return heading;
}
static int ent_hook_unit(void *user, unsigned tid) { game_t *g = (game_t *)user; const int u = g->world ? world_model_unit_shift(g->world, (uint16_t)tid, 0) : 3; return u < 0 ? 3 : u; }
static int ent_hook_block(void *user, unsigned entry, int16_t w[5]) { game_t *g = (game_t *)user; return g->world ? world_position_block(g->world, entry, w) : -1; }
/* The waypoint run of the table entry r (note 07 4.3): the consecutive blocks from the offset the table 0x2DCE8 names up to the first block whose first word has bit 15 set (tables/ai.bin).
 * Without ai.bin (C, a missing-asset fallback) a run is the one block of the entry. */
static int ent_hook_route(void *user, unsigned r, int16_t blocks[][5], int max) {
    game_t *g = (game_t *)user;
    if (g->has_ai && r < AI_RUNS) {
        int n = g->aitab.run_len[r];
        if (n > max) n = max;
        for (int k = 0; k < n; k++) memcpy(blocks[k], g->aitab.run[r][k], 5 * sizeof(int16_t));
        return n;
    }
    if (max < 1 || !g->world || world_position_block(g->world, r, blocks[0]) != 0) return 0;
    return 1;
}

/* ---- the weapons (weapons.c, note 07 Part B) ---------------------------------------------------------------------------------------------------------
 * The hooks the weapons module calls (the flight log, queue C of the display lists, the model vertices, the message line; the landmark volumes come from the environment's slots, flight_env()->landmark), and the three
 * entity hooks that reach it (their user is the game). */
static void wp_count(void *user, int off, unsigned delta) { logbook_add(&((game_t *)user)->log, off, delta); }
static int wp_queue_c(void *user, wp_qentry_t *out, int max) {
    game_t *g = (game_t *)user;
    if (!g->world) return 0;
    const lists_t *l = world_lists(g->world);
    int n = l->kept[2];
    if ((int)l->emitted_c < n) n = l->emitted_c;   /* V:4A916: the count of queue C, the walk starts at the last entry */
    if (n > max) n = max;
    for (int i = 0; i < n; i++) { const lists_entry_t *q = &l->q[2][l->kept[2] - n + i]; out[i].id = q->id; out[i].flags = q->flags; }
    return n;
}
static int wp_slots(void *user, int rec, int16_t (*out)[3], int n) {   /* the vertex slots the last draw stored (lists.c store_vslots) */
    game_t *g = (game_t *)user;
    if (!g->world || rec < 0 || rec >= LISTS_ENTITIES || n > LISTS_VSLOTS) return -1;
    memcpy(out, world_lists(g->world)->vslot[rec], sizeof(int16_t) * 3 * (size_t)n);
    return 0;
}
static int wp_model_flags(void *user, unsigned model) {
    game_t *g = (game_t *)user;
    if (!g->world) return -1;
    return world_model_block_flags(g->world, (uint16_t)model);   /* the block flag byte (+7 of the first block), the byte 0x2ADDE tests: bit 4 marks a big hull (note 07 8.2 step 4; rig: test_weapons_diff gunhit) */
}
/* A record as the ship tests read it (note 07 10.2 ship rows, 8.2 the big-hull test): its state and the vertex slots the draw stored (58). */
static int wp_ship(void *user, int rec, flight_ship_t *out) {
    game_t *g = (game_t *)user;
    if (!g->world || rec < 0 || rec >= ENT_RECORDS) return -1;
    const aircraft_t *a = entities_ac_c(&g->ent, rec);
    const flight_state_t *f = &a->f;
    memset(out, 0, sizeof *out);
    out->present = 1; out->w0 = f->w0; out->cls = f->cls; out->group = f->group; out->l7d = f->l7d;
    out->pitch = (uint16_t)f->pitch; out->heading = (uint16_t)f->heading; out->roll = (uint16_t)f->roll;
    for (int k = 0; k < 3; k++) out->pos[k] = a->pos[k];
    out->fine_x = f->fine_x; out->fine_z = f->fine_z; out->alt8 = (int16_t)f->alt8;
    memcpy(out->mat_b, f->mat_b, sizeof out->mat_b);
    memcpy(out->vtx, world_lists(g->world)->vslot[rec], sizeof out->vtx);   /* the draw store for every record, the carrier's included (note 05 open question 9, settled by 07 8.10): the leading run of the block the drawer last drew, zero before the first draw */
    return 0;
}
static void wp_clear_draw(void *user, int rec) {   /* the rearm zeroes the records 1 to 3 whole: their vertex slots and part id too (note 07 7.1) */
    game_t *g = (game_t *)user;
    if (!g->world || rec < 0 || rec >= LISTS_ENTITIES) return;
    memset(world_lists(g->world)->vslot[rec], 0, sizeof world_lists(g->world)->vslot[rec]);
    world_lists(g->world)->part_id[rec] = 0;
}
static void wp_post(void *user, uint16_t code) { game_t *g = (game_t *)user; cockpit_post(&g->ck, &g->ac, code); }
static int ent_hook_launch_player(void *user, int idx) { return weapons_launch_player(&((game_t *)user)->wp, idx); }
static int ent_hook_launch_ai(void *user, int idx) { return weapons_launch_ai(&((game_t *)user)->wp, idx); }
static int ent_hook_weapon(void *user, int idx) { return weapons_gate(&((game_t *)user)->wp, idx); }

/* The mission-start reset of the entities and the placement of the missions (modes 3 to 8: the free flight, the demo and the qualification place only the carrier, which the game holds
 * as the ship record of the flight environment; the trainings place their lead through training.c). The carrier of a mission is the record 14 the placement made, with the random offset. */
/* ---- the sound module's calls (note 10 section 8) ---------------------------------------------------------------------------------------------------------------- */
/* The key click of a key body 0x37C4A (10 7.4): silent in an external view and during the cue cool-down. */
static void snd_key_beep(game_t *g) { sound_event(&g->snd, SND_BEEP_SWITCH, (uint8_t)g->vs.level, 0); }
/* The wind (volume 8) or the external engine pair (period 0x300, volume 8) of a flight start (10 7.2). */
static void snd_flight_start(game_t *g) {
    if (g->snd.en13 & 0x10) sound_event(&g->snd, SND_WIND, 8, 0);
    else sound_event(&g->snd, SND_ENGINE_EXT, 0x300, 8);
}
/* The entities' sound requests (entities.h): the seat tone 0x26FFE and the ejection bang's arguments. */
static void ent_hook_sound(void *user, unsigned code) {
    game_t *g = (game_t *)user;
    if (code == 1) { static const int seat[9] = { 200, 14, 8, 200, 0, 2, 1, 5, 1 }; sound_tone(&g->snd, seat); }
    else if (code == 2) sound_bang_request(&g->snd, 0x3F, 0x78, 0xFF);
}

static void entities_begin(game_t *g) {
    entities_t *e = &g->ent;
    flight_set_env(&g->fenv);   /* the placement's aim reads the flight environment: install this game's (a destroyed game's pointer must not stay) */
    entities_reset(e);
    for (int i = 0; i < ENT_RECORDS; i++) e->ext[i] = NULL;
    e->hook.user = g;
    e->hook.training = ent_hook_training;
    e->hook.aim_heading = ent_hook_aim;
    e->hook.aim_dir = ent_hook_aim_dir;
    e->hook.unit_shift = ent_hook_unit;
    e->hook.position_block = ent_hook_block;
    e->hook.route = ent_hook_route;
    e->hook.sound = ent_hook_sound;
    e->hook.launch_player = ent_hook_launch_player; e->hook.launch_ai = ent_hook_launch_ai; e->hook.weapon = ent_hook_weapon;
    g->wp.hook.user = g; g->wp.hook.count = wp_count; g->wp.hook.queue_c = wp_queue_c; g->wp.hook.slots = wp_slots; g->wp.hook.ship = wp_ship; g->wp.hook.clear_draw = wp_clear_draw; g->wp.hook.model_flags = wp_model_flags; g->wp.hook.post = wp_post;
    g->fenv.contact = weapons_contact; g->fenv.contact_user = &g->wp;   /* the contact scan against the other records (note 07 10.2) */
    g->fenv.follow = weapons_follow; g->fenv.follow_user = &g->wp;   /* the followed record V:54E72 (note 07 13.1) */
    e->req = &g->scr.request; e->req_delay = &g->scr.request_delay;   /* V:4A8E2 and V:4A8E4: the screen module's request byte, shared with the seat */
    e->tpl = g->has_tpl ? &g->tpl : NULL;
    e->tab = g->has_ai ? &g->aitab : NULL;
    e->scn = g->scr.scn;
    e->mode = g->scr.mode_byte;
    e->progress = g->scr.log ? (int)g->scr.log->live_level : 0;
    e->demo = g->scr.v4a848;
    e->flying = 1;
    e->sim_on = 1;
    e->hostile_cap = g->scr.hostile_cap;
    radar_reset(&g->rd);
    weapons_mission_start(&g->wp);   /* the stores of the flight start 0x1D1BE / 0x1F14A: 0x24, 500 rounds, 16 flares and chaff, the ECM off */
    if (g->has_flight) flight_set_env(&g->fenv);   /* the placement builds attitude matrices through the flight environment: this game's, not a destroyed one's */
    if (g->has_flight && g->scr.sub_count > 0) {   /* every mode with a row of the table 0x2DEDA places from it (note 07 2.4): the modes 1 to 9, the demo's 0x7F row (the carrier and two MiG-29s) */
        entities_start_mission(e, g->scr.sub_first, g->scr.sub_count, g->scr.hostile_cap, g->scr.pl_dx, g->scr.pl_dz);
        entities_scene_pass(e, g->scr.sub_first, g->scr.sub_count);   /* the scene passes before the flight: the fresh pass and the gate's range routine (demo MiG: displacement from pass 0, +0x39 = 0x11) */
        const aircraft_t *c = &e->rec[ENT_CARRIER].ac;
        if ((c->f.w0 & 0x0040) && c->f.cls == 0x20) { g->carrier_free[0] = c->pos[0]; g->carrier_free[2] = c->pos[2]; }
    }
}

/* The record the ship record of the flight environment stands for (note 05 17.14): the game holds the carrier there, the entity array shows it as record 14 (the fixed camera and the gun's box test read it). */
static void entities_sync_carrier(game_t *g) {
    if (!g->has_flight || !g->fenv.ship.present) return;
    aircraft_t *c = &g->ent.rec[ENT_CARRIER].ac;
    const flight_ship_t *s = &g->fenv.ship;
    c->f.w0 = s->w0; c->f.cls = s->cls; c->f.group = s->group;
    for (int i = 0; i < 3; i++) c->pos[i] = s->pos[i];
    for (int i = 0; i < 9; i++) c->f.mat_a[i] = c->f.mat_b[i] = s->mat_b[i];
    c->f.l7d = s->l7d;   /* the unit shift +0x7D, the cell, fine and height words +0x06..+0x10: the gun's box test and its ship hook read them from the record */
    c->f.cell_x = (int16_t)(((uint32_t)s->pos[0] & WORLD_POS_MASK) >> 22); c->f.cell_z = (int16_t)(((uint32_t)s->pos[2] & WORLD_POS_MASK) >> 22);
    c->f.fine_x = s->fine_x; c->f.fine_z = s->fine_z; c->f.alt8 = s->alt8;
}

/* ---- the missions (missions.c, note 08 sections 12 to 14) ------------------------------------------------------------------------------------------------
 * The glue: the end-of-update routine after the entity loop of every pass (frozen passes included), what its hand-over asks of the view, the request 3 the dispatcher takes, the
 * resume of the flight after the outcome screen. */
static void missions_g(game_t *g);

/* The bearing of the vector notice 0x58 (13.2, 0x29534): the first entry of the placement sub-list whose record is valid, of the aircraft class nibble and not the cruise missile; the aim
 * routine of note 03 (limit -1: it snaps) gives the heading word of the vector to the point ahead of it, and the degrees are the word / 80. */
static int mission_bearing(void *user) {
    game_t *g = (game_t *)user;
    const screen_t *s = &g->scr;
    if (!g->has_flight || !s->scn) return -1;
    for (int k = 0; k < s->sub_count && s->sub_first + k < s->scn->sub_entry_count; k++) {
        const int slot = s->scn->sub_entry[s->sub_first + k].w[2] & 0x7F;
        if (slot >= ENT_RECORDS) continue;
        const aircraft_t *a = entities_ac_c(&g->ent, slot);
        if (!(a->f.w0 & 0x0040) || (a->f.cls & 0xF0) != 0x10 || a->f.cls == 0x15) continue;
        /* the point 500 units ahead of the record in its own frame (0x1D478: the third column of the record's +0x92 matrix times 500, shifted right by the ring unit 4), minus the player's x and z,
         * both shifted right by 8; the aim routine with the limit -1 snaps: the plain arctangent heading */
        const int32_t tx = a->pos[0] + ((a->f.mat_b[2] * 500) >> 4), tz = a->pos[2] + ((a->f.mat_b[8] * 500) >> 4);
        return (int)missions_bearing_degrees((unsigned)ent_hook_aim(g, (tx >> 8) - (g->ac.pos[0] >> 8), (tz >> 8) - (g->ac.pos[2] >> 8)));
    }
    return -1;
}

/* Applies what the scripts left for the view (missions_view_t): the hand-over to the external view of F2 and F5 (12.2) and the tracking camera. */
static void missions_apply_view(game_t *g) {
    missions_view_t *v = &g->ms.view;
    if (!v->pending) return;
    v->pending = 0;
    if (v->ring_set) { g->vs.ring = v->v4a96a; v->ring_set = 0; }     /* V:4A96A = 7 (the value stays in missions_t for the restart, 12.11) */
    if (v->v4a86d) set_v4a86d(g, 1);    /* V:4A86D, V:4A86C = 1 */
    if (v->v4a86c) g->vs.look = 1;
    if (v->toggle_request) { v->toggle_request = 0; g->req_view = 1; g->saved_level = (int8_t)v->v4a8eb; }   /* V:4A97E bit 1 and the level V:4A8EB the toggle takes (0xFF is -1, F5 3) */
    if (v->sel_set) { v->sel_set = 0; g->vs.level = (int8_t)v->v4a83d; }   /* the tracking camera in an external view: V:4A83D = V:4A8EB (F2 0xFF, F5 3) */
    if (v->track) { v->track = 0; g->ac.f.v4a968 = 0xFE; }
    if (v->heading_set) { g->vs.heading = v->v4a9e2; v->heading_set = 0; }   /* V:4A9E2: the ring rotation 0x2D756 compares the record's heading with it every frame (note 06), so the saved heading is the next comparison's old value */
    /* The helper 0x1EFF4 the resume runs (redraw_helper) stores R, the panel counter V:4A8EE and the message line's V:4A8F4; its other counters are redraws the frontend does every frame. */
    v->v4a86d = v->v4a86c = 0;
}

/* The end-of-update routine (12.1), after the player's step and the entity loop: also in the passes where the pause byte is set by a script's count and in those of the outcome screens (12.14 N2:
 * with an end code set only the tail runs, so a landing can store 0xFC while the delay or the screens of a 0xFF run). */
static void missions_g(game_t *g) {
    if (!g->has_flight) return;
    missions_t *m = &g->ms;
    m->view.in_selector = (uint8_t)(g->vs.level != 0);
    m->view.in_direction = (uint8_t)g->vs.ring;
    m->view.in_ident = (int8_t)g->rd.mission1;   /* the radar walk's counter is the one V:4A927 (F1 reads it, 08 12.5) */
    m->view.v4a8eb = (uint8_t)g->saved_level;   /* V:4A8EB: the saved external level, which the tracking camera reads in an external view */
    g->ent.mode = g->scr.mode_byte;
    missions_step(m, &g->ent);
    missions_apply_view(g);
}

/* The debrief's resume (0x1305E, 0x130BA): the flight goes on with the code 1 (after 0xFF), 2 (0xFE) or 4 (0xFC), the keys unlocked and the pause byte cleared; with the tracking camera on
 * (V:4A994 non-zero) the viewed record returns to the player and the hand-over's bytes restore the direction and the view (12.14 N7). */
static void missions_resume_g(game_t *g, uint8_t code) {
    missions_t *m = &g->ms;
    g->ac.f.v4a850 = code;
    request_lists(g);   /* V:4A910 = 0xFF (0x13094): the display-list request of the resume, before the V:4A994 test (S5 of the review) */
    if (g->ac.f.v4a994 != 0) {   /* N7 (12.14; 0x130A2 skips this whole restore when V:4A994 is 0): the viewed record returns to the player, the selector V:4A83D comes back from the saved V:4A8EC and the ring direction from V:4A876 & 0x7F, both
                                  * directly and unconditionally (no test of bit 7 of V:4A876, no pending view toggle); the window offsets follow the selector (0x32, 0x320, 0xA7 external; 0, 0, 0x90 cockpit), the head code is 0, then the helper */
        g->vs.level = (int8_t)m->view.v4a8ec;
        g->vs.ring = m->view.v4a876 & 0x7F;
        entities_switch_view(&g->ent, 0);
        g->ac.f.v4a994 = 0; g->ac.f.v4a996 = 0;
        g->vs.head = 0; g->hud_step = g->vs.level != 0 ? 50 : 0;
        redraw_helper(g);   /* 0x130FE: the helper 0x1EFF4 (note 07 12.1 writer 2) */
        /* V:4A876 stays set (bit 7): a second hand-over does not happen in the same mission (12.14 N7) */
    }
    missions_resume(m, &g->ac.f);
}

static int request_dispatcher(game_t *g);

/* The radar's inputs for this pass (radar.h WIRING). */
static void radar_inputs(game_t *g, radar_in_t *in, uint32_t n) {
    memset(in, 0, sizeof *in);
    in->e = &g->ent;
    in->n = n;
    in->cockpit = !ext_view(g);
    in->hud_off = !g->ck.hud_on;   /* V:4A859 */
    in->paused = g->ac.f.v4a866 != 0;   /* the pause byte (a script's count): the sight and the range number test it, the list builder and the scope walk do not (note 07 12.2) */
    in->zoom = g->ck.zoom ? g->ck.zoom : 0x80;
    in->hud_row = g->wp.jolt_row;
    in->view_off = (int16_t)(g->hud_step * 16);
    in->cam_y = -(int32_t)entities_ac_c(&g->ent, entities_viewed(&g->ent))->f.alt8;
    in->tgt_x = in->tgt_y = -1;
}

/* What the weapons need after the world update of a pass: the trail ring of the followed record (13.1), the rearm of a landing (the flight step raised ev_rearm), the lost mark's
 * clearing of the fire-prompt state (ev_lost, the radar module's V:4A96C). */
/* The sound events the flight steps raised (note 10 8.2, 7.4): the bump of a first contact, the gear-down warning's beep and the ground contacts of the viewed record. Cleared for every record. */
static void sound_flight_events(game_t *g) {
    const int viewed = viewed_index(g);
    for (int i = 0; i < ENT_RECORDS; i++) {
        flight_state_t *f = i == 0 ? &g->ac.f : &entities_ac(&g->ent, i)->f;
        /* the call order of the player's step: the contacts of the motion step (the nose settling 0x15C44 in the speed step, the touchdown 0x1703A/0x1704A) and the gear-down warning's beep (0x2837E) precede the
         * collision's bump (0x2A834), so on voice 2 the later bump cuts the contact sound */
        if (f->ev_ground) sound_event(&g->snd, SND_GROUND, f->ev_ground, i == viewed);   /* the wrapper needs the stepped record to be the camera's */
        if (i == 0 && f->ev_beep) sound_event(&g->snd, SND_BEEP_N, f->ev_beep, (uint8_t)g->vs.level);   /* 0x2837E */
        if (i == 0 && f->ev_bump) snd_noise(g, 0x1C, 0x30);            /* 0x2A834 */
        f->ev_bump = f->ev_beep = f->ev_ground = 0;
    }
}

static void weapons_g(game_t *g) {
    flight_state_t *pf = &g->ac.f;
    if (g->ent.trail < ENT_RECORDS && (entities_ac(&g->ent, g->ent.trail)->f.w0 & 0x0040)) weapons_trail_step(&g->wp, g->ent.trail);
    if (pf->ev_rearm) { pf->ev_rearm = 0; weapons_rearm(&g->wp); }
    for (int i = 0; i < ENT_RECORDS; i++) {
        flight_state_t *f = &entities_ac(&g->ent, i)->f;
        if (f->ev_lost) { f->ev_lost = 0; g->rd.prompt = 0; }   /* the lost mark clears V:4A96C (note 07 8.4) */
    }
}

/* One pass of the world update after the player's step (1.2, 4.8). The pause freezes the pass counter; the stale-lock drop and the region tick run in a paused pass too. */
static void entities_g_begin(game_t *g) {
    entities_t *e = &g->ent;
    flight_state_t *pf = &g->ac.f;
    request_dispatcher(g);   /* the dispatcher is in the accounting routine 0x11748, before the gate test: the pass in which it acts runs the WHOLE world update (note 08 5.11, the ruling: request 2 as a frozen pass with the pause byte 1; ESC alone makes none, and that is the screen module's hold) */
    e->mode = g->scr.mode_byte;
    e->progress = g->scr.log ? (int)g->scr.log->live_level : 0;
    e->demo = g->scr.v4a848;
    e->sim_on = (pf->v4a984 & A984_SIM_RUNNING) != 0;
    e->trig = g->tr.part ? (int8_t)g->tr.trig : 0;
    e->training_part1 = g->tr.running;
    e->flying = 1; e->paused = pf->v4a866 != 0; e->frozen = 0;   /* the pause byte V:4A866: a script's count freezes the entity timers and motion like a pause (note 08 12.2) */
    e->scn = g->scr.scn;
    entities_sync_carrier(g);
    e->saved_view = (uint8_t)g->saved_level;
    e->pass = g->ck.n;   /* V:4A992: the cockpit's counter, the one counter (it turns at the end of the pass) */
}

static void set_head(game_t *g, int code);
/* The record loop and the tail of the world update after the player's step: records 1 to 15, the requests, the view switches, the end-of-update routine. */
static void entities_g_end(game_t *g) {
    entities_t *e = &g->ent;
    flight_state_t *pf = &g->ac.f;
    e->sim_on = (pf->v4a984 & A984_SIM_RUNNING) != 0;   /* the player's step may have stopped the simulation */
    entities_step(e);
    if (e->request) { pf->v4a910 |= e->request; e->request = 0; }
    for (int i = 1; i < ENT_RECORDS; i++) {   /* the jolt events of every record (note 08 8.3), and the clocks' events that only the player's step uses */
        flight_state_t *f = &entities_ac(e, i)->f;
        g->jolt |= f->ev_jolt;
        f->ev_jolt = f->ev_liftoff = f->ev_touchdown = f->ev_crash = 0;
    }
    if (e->redraw_req) {   /* 0x1EC14 in the cockpit view (the seat takes the view, a tracking camera starts): the seat's head set-up 0x1EC60 to 0x1EC98, any other class the head code 0 through 0x1E632 */
        e->redraw_req = 0;
        if (seat_view(g)) { g->vs.head = 0; g->seat_wait = 1; cockpit_zoom_reset(&g->ck); request_lists(g); redraw_helper(g); }   /* the step 50 follows in the next pass (game_step) */
        else set_head(g, 0);
    }
    if (e->strip_off) { e->strip_off = 0; }
    if (e->view_switched) {   /* the seat's view switch: the view byte it left, the ring direction 4 (behind); 0x1EC3C calls the zoom reset 0x1F060 in both views and 0x1EC54 stores V:4A8EE = 3 in the external branch only (0x1EC42 beq 0x1EC60, 07 12.1) */
        const int was_ext = e->view_switched == 2;
        e->view_switched = 0; g->vs.level = (int8_t)pf->v4a83d; g->vs.ring = 4;
        cockpit_zoom_reset(&g->ck);
        if (was_ext) g->ck.panel = 3;
    }
    missions_g(g);
}

/* The flow dispatcher's part for the request byte (0x11748; note 08 5.11, note 07 6.4): the request 2 of the landed seat (or a script) is acted on in the first pass in which the countdown
 * V:4A8E4 is below 0; the dispatcher decrements it once per pass, at its end: set to 4 in the world update of pass P, it reads 3, 2, 1, 0, -1 at the end of the passes P+1 to P+5 and the
 * end code 0xF0 follows in the pass P+6 (captured: request pass 1340, end code pass 1346). It runs at the head of the pass, before the world update, and only while the flight runs. */
static int request_dispatcher(game_t *g) {
    screen_t *s = &g->scr;
    if (s->request != 2 && s->request != 3) return 0;
    if (s->state != SCR_FLIGHT && !(s->request == 3 && screen_in_outcome(s))) { s->request = 0; return 0; }   /* N2: a second request 3 is taken in every state of the outcome */
    if (s->request_delay >= 0) { s->request_delay--; return 0; }
    if (s->request == 3) {   /* a script's result (12.2): six passes after the store, with the code it stored and the delay V:4A978; a negative delay (the F6 quirk) is none */
        const uint8_t code = g->ac.f.v4a850;
        s->request = 0;
        if (code != 0xFF && code != 0xFE && code != 0xFD && code != 0xFC) return 0;
        const int d = (int16_t)g->ms.delay > 0 ? (int16_t)g->ms.delay : 0;
        g->ac.f.v4a87d = 1;
        screen_mission_end_delayed(s, code, (uint8_t)((g->ck.n + (unsigned)d) & 1), d);   /* the parity of the step counter at the debrief: it runs on for D passes before the freeze */
        return 1;
    }
    s->request = 0;
    g->ac.f.v4a850 = 0xF0; g->ac.f.v4a87d = 1;
    g->ac.f.v4a866 = 1;   /* request 2 sets the pause byte with the gate still 1: this pass is a frozen full pass (08 5.11) */
    screen_mission_end(s, 0xF0, (uint8_t)(g->ck.n & 1));
    return 1;
}

/* A pass of the outcome screens' frozen world (12.14 N2, N4): the gate is 1, the pause byte is 1, so the update runs without the entity timers; the dispatcher takes a request 3 whose countdown ran out
 * and the tail of the end-of-update routine runs (the scripts do not: the code is not 0). A landing that stores 0xFC in a stopped aircraft restarts the outcome for that code. */
static void outcome_frozen_pass(game_t *g) {
    entities_t *e = &g->ent;
    flight_state_t *pf = &g->ac.f;
    request_dispatcher(g);   /* acts in the pass, which runs on whole (08 5.11) */
    e->sim_on = (pf->v4a984 & A984_SIM_RUNNING) != 0;
    e->demo = g->scr.v4a848;
    missions_g(g);
}

/* The aircraft reaches the world only through world_set_camera (world.h). Called once per unpaused pass (the aim and the ring camera do not update while paused). */
static void apply_camera(game_t *g) {
    if (!g->world || g->freecam) return;
    world_camera_t c;
    head_camera_words(g);
    g->vs.image_word = g->has_flight ? trig_word : NULL; g->vs.image_user = &g->ftab;
    if (world_camera_build(g->world, viewed_ac(g), &g->vs, &c) == 0) {
        world_set_camera(g->world, &c);
        g->cam_cell_x = (int)(((uint32_t)c.pos[0] & WORLD_POS_MASK) >> 22); g->cam_cell_z = (int)(((uint32_t)c.pos[2] & WORLD_POS_MASK) >> 22);
        if (ext_view(g)) memcpy(g->scan_cam, c.pos, sizeof g->scan_cam);
    }
}

static void request_lists(game_t *g) { world_lists_request(g->world, 0xFF); }
/* The ring-key site 0x1E4FE: the direction store, the request, and V:4A968 = 0xFF when it is 0 (04a). */
static void ring_store(game_t *g, int r) {
    g->vs.ring = r;
    set_v4a86d(g, 1); g->vs.fixed = 0;   /* a ring key sets V:4A86D again: the camera returns to the ring (note 07 6.3) */
    if (!g->ac.f.v4a968) g->ac.f.v4a968 = 0xFF;
    request_lists(g);
}   /* V:4A910 = 0xFF: a request site (04a) */

/* Keypad key (0..9 = KP0..KP9, 10 = KPDOT) to the external ring index, note 04; -1 for KP0. */
static int ring_for_key(int key) {
    static const signed char map[11] = { -1, 5, 4, 3, 6, 8, 2, 7, 0, 1, 9 };
    return key >= 0 && key <= 10 ? map[key] : -1;
}

/* A head key that changes the code (0x1E6F8 .. 0x1E714): the code, the hold byte 0xFF and the list request. The HUD step is computed here from the table (world_head_step) and the equal-row test; the
 * window row follows from the code (world_window_last_row). The tail returns before the step, offset and equal-row logic when the viewed record is of class 0x30 (the ejection seat: step 50, offset 800,
 * row 167, note 06 "Head views" 2b), and before the helper 0x1E7B2 (R = 3, note 07 12.1 writer 4) when the equal-row test takes its 50 (0x1E78C); every other code ends in 0x1E7B2. */
static void set_head(game_t *g, int code) {
    const int present = world_window_last_row(0, 0, g->vs.head), row = world_window_last_row(0, 0, code);
    const int seat = seat_view(g);
    g->ck.panel = 3;   /* V:4A8EE: the head-key body 0x1E6FE stores it on EVERY change of the head code, before the seat and equal-row tests (note 07 12.1) */
    int redraw = 1;
    if (!seat) {
        /* the 50 (offset 800) replaces the table byte only for the codes 3 to 9, 12 and 13 whose window row equals the present one (the test at 0x1E774) */
        const int equal_row = ((code >= 3 && code <= 9) || code == 12 || code == 13) && row == present;
        g->hud_step = equal_row ? 50 : world_head_step(g->head_step, code);
        if (equal_row) redraw = 0;
    } else redraw = 0;
    g->vs.head = code;
    g->hold = -1;
    cockpit_zoom_reset(&g->ck);   /* 0x1F060 at 0x1E706 (V:4A8F5 = 3: the redraw counter of the magnification digit, read by its drawer 0x36590, note 07 12.1: the port draws the digit from the scene every frame, so the counter has no visible effect) */
    request_lists(g);
    if (redraw) redraw_helper(g);   /* 0x1E7B2 */
}

/* The KP-Enter handler (0x1E390): in the cockpit the direction becomes 4 (behind); always the look-at and aim flags, the map flag cleared, the stored heading, the request bit and the
 * saved level -1. It stores no list request: the view switch does (0x142B4). */
static void view_request_tail(game_t *g) {
    g->vs.heading = g->ac.yaw % 28800; if (g->vs.heading < 0) g->vs.heading += 28800;
    g->req_view = 1;
    g->saved_level = -1;
}
static void key_enter(game_t *g) {
    if (g->vs.level == 0) g->vs.ring = 4;
    set_v4a86d(g, 1); g->vs.look = 1; g->vs.map = 0;
    view_request_tail(g);
}

/* Keypad keys in the cockpit (the table of note 06 "Head views"). */
static void key_cockpit(game_t *g, int key) {
    const int h = g->vs.head;
    switch (key) {
    case 8: case 9: set_head(g, 0); break;                        /* KP9 reaches the same path as KP8 */
    case 2: set_head(g, 6); break;
    case 7: set_head(g, 12); break;
    case 1: set_head(g, 13); break;
    case 4: set_head(g, h == 0 ? 11 : (h == 1 || h >= 12 ? 0 : h - 1)); break;
    case 6: set_head(g, h >= 11 ? 0 : h + 1); break;
    case 5: g->hold = -1; break;                                   /* a lock of the present view: no code change, no request */
    case 3: ring_store(g, 3); break;               /* KP3 and KP-dot store the direction even in the cockpit and request (0x1E4FE) */
    case GAME_KP_DOT: ring_store(g, 9); break;
    default: break;                                                /* KP0: no handler */
    }
}

static void keypad_event(game_t *g, int key) {
    if (key == GAME_KP_ENTER) { key_enter(g); return; }
    if (g->vs.level == 0) { key_cockpit(g, key); return; }
    const int r = ring_for_key(key);   /* the external handlers 0x1E4A8 .. 0x1E4F6, also while the map is shown (the keys route on V:4A83D) */
    if (r < 0) return;
    ring_store(g, r);
}

/* The release of a keypad key: KP2, KP4, KP6, KP7 and KP1 (raw 0x9E, 0xAD, 0xAF, 0xBD, 0x9D) start the head-hold countdown at 5 (note 06 "Head views"). */
static void keypad_up_event(game_t *g, int key) {
    if (key == 2 || key == 4 || key == 6 || key == 7 || key == 1) g->hold = 5;
}

/* The M key (0x1ECB8), accepted in camera mode 0: M in the map saves the altitude and takes the KP-Enter tail; otherwise it plants the map camera (aim (7200, 0), no look-at), sets the
 * map flag and, in the cockpit only, requests the view switch. M in an external view writes neither V:4A9E2 (the stored heading) nor V:4A8EB (the saved level): only the cockpit and map-exit
 * paths take the KP-Enter tail. The original also stops the sounds (0x115F6 when the map goes on; the view switch stops channel 3) and clears the vector words V:4AD02... */
static void key_map(game_t *g) {
    world_viewstate_t *vs = &g->vs;
    if (vs->mode) return;
    if (vs->map) {
        g->saved_alt = vs->map_y;   /* only the M handler saves the altitude */
        set_v4a86d(g, 1); vs->look = 1; vs->map = 0;
        view_request_tail(g);
        return;
    }
    sound_stop_all(&g->snd);   /* 0x1ECE2: the map key stops all four voices when it switches the map on (note 10 4.1, 7.3); the gate V:4A865 then keeps the sound routine out */
    vs->map = 1; vs->map_y = g->saved_alt;
    vs->aim_pitch = WORLD_MAP_PITCH; vs->aim_heading = 0;
    vs->look = 0; set_v4a86d(g, 1);
    if (vs->level == 0) view_request_tail(g);   /* in an external view the level stays, the window row changes at once and nothing is requested */
}

/* `]` (closer, raw 0x1B) and `[` (farther, raw 0x1A) dispatch on the view (0x1E80E, 0x1E82E, 0x1E57A, 0x1E592, 0x1E600): the cockpit zoom word V:4AAFA (also an external view with V:4A86D
 * clear), the external level -1, 1..4, or the map altitude; each requests a list rebuild, also at the stops. */
static void key_bracket(game_t *g, int closer) {
    world_viewstate_t *vs = &g->vs;
    if (vs->level == 0 || !vs->aim_on) {
        cockpit_zoom_key(&g->ck, closer);
        g->hold = -1;                                              /* the zoom keys set the hold byte 0xFF too (and V:4A8F5 = 3, the magnification digit's redraw counter: no visible effect, the frontend draws it every frame) */
        request_lists(g);                                          /* conditional on the low seven bits of V:4A895, which are 0 in play */
    } else if (vs->mode == 0) {
        if (vs->map) {
            /* clamps, not refusals: `[` stores min(alt + 0x2000000, 0x8000000), `]` stores max(alt - 0x2000000, 0x1000000); the request below is stored at the stop too */
            if (closer) { const int32_t a = vs->map_y - WORLD_MAP_ALT_STEP; vs->map_y = a > WORLD_MAP_ALT_MIN ? a : WORLD_MAP_ALT_MIN; }
            else { const int32_t a = vs->map_y + WORLD_MAP_ALT_STEP; vs->map_y = a < WORLD_MAP_ALT_MAX ? a : WORLD_MAP_ALT_MAX; }
        } else if (closer) {
            vs->level = vs->level - 1 > 0 ? vs->level - 1 : -1;     /* subtract one, -1 when the result is not positive */
        } else {
            vs->level = vs->level < 0 ? 1 : (vs->level < 4 ? vs->level + 1 : 4);
        }
        request_lists(g);
    }
}

/* The first chain of the flight keys (0x1DB54 to 0x1DC08: the trigger, the stick and rudder keys, the throttle keys, the brake, E, R, G, A, F, C, J, H, S, T, Y and M) is skipped when the low nibble of the
 * player's gear byte V:4B2B8 is not 0, the ejection state (the test at 0x1DB2E; note 08 11.1, note 07 6.4a). The nibble the handler reads is the one the previous pass left, plus the OR of an ejection press
 * of the same drain (the original ORs it in the handler; fin.eject stands for it until the step). The second chain (RETURN, the head and view keys, the F-keys, the brackets, ESC) is not gated. The trigger
 * and the stick keys have their own nibble tests (weapons.c, flight.c), so they are not repeated in flight_button. */
static int chain_open(const game_t *g) { return (g->ac.f.gear & 0x0F) == 0 && !g->fin.eject; }

static void view_event(game_t *g, const game_event_t *ev) {
    switch (ev->code) {
    case GAME_BTN_KEYPAD: keypad_event(g, ev->value); break;
    case GAME_BTN_KEYPAD_UP: keypad_up_event(g, ev->value); break;
    case GAME_BTN_TOWER: if (ev->value == 1 && chain_open(g)) key_map(g); break;   /* M is in the first chain (0x1DC04) */
    /* the release of the bracket keys sets the hold byte to 5 in every view (the handler 0x1E892 tests no view) in the camera modes 0, 5 and 6 (R: not in mode 3, nor with V:4A88B or
     * V:4A965 nonzero; modes 1, 2 and 4 are not covered); the port has mode 0 only */
    case GAME_BTN_RBRACKET: if (ev->value == 1) key_bracket(g, 1); else g->hold = 5; break;
    case GAME_BTN_LBRACKET: if (ev->value == 1) key_bracket(g, 0); else g->hold = 5; break;
    default: break;
    }
}

/* The per-second counters (0x29882, inside 0x297D6 at 0x11446: a DRAWN frame only, after the state routine 0x141E8 of the frame, never in a pause or a hold): when the timer second differs
 * from the one kept (any change counts), the head-hold byte falls by one unless it is negative, so 0 becomes 0xFF, a hold; and the radar page countdown V:4A93E (radar_clock_tick) falls. */
static void hold_second(game_t *g) {
    if (g->seconds == g->seconds_seen) return;
    g->seconds_seen = g->seconds;
    g->snd_tick = 1;   /* the gear countdown V:4A93D and the cue cool-down V:4A942 count the same seconds, but the tick is the LAST thing a pass does: the sound routine of the pass runs first (note 10 5.1 item 7), so a gear
                        * 5.1 item 7), so a gear key of this pass is seen before its bit 7 is cleared and the cool-down counts after the cue player; game_step_pass takes the tick at its end. The head hold decrements in the same block as
                        * the gear byte and the cool-down [V: sound7-tick, 5 of 5 decrements fell in the sample of a change of V:4ABAA]; the radar page countdown rides the same block [R]. */
    if (g->hold >= 0) g->hold--;
    if (g->has_flight) radar_clock_tick(&g->rd);   /* the same block counts V:4A93E down (0x29896, note 07 12.8): per second of the timer device, not per pass; not in a pause */
}

static void view_toggle(game_t *g);
/* The state routine 0x141E8 (called after the mission-state routine, only while the flight runs: not in a pause): the head-hold recentre, then the pending view switch, once. */
static void view_switch(game_t *g) {
    if (g->vs.level == 0 && g->hold == 0) set_head(g, 0);   /* the hold ran out in the cockpit view: code 0 by the KP8 path (which stores 0xFF again) */
    if (g->req_view) view_toggle(g);
    /* 0x142EC to 0x14328: a viewed record of class 0x30 whose offset word V:4AA3E is 0 gets the seat's set-up (step 50, offset 800, head code 0, the zoom reset, the list request and 0x1E7B2) */
    if ((viewed_ac_c(g)->f.cls & 0xF0) == 0x30 && g->hud_step == 0) {
        g->hud_step = 50; g->vs.head = 0;
        cockpit_zoom_reset(&g->ck);
        request_lists(g);
        redraw_helper(g);
    }
}

/* The pending toggle of the state routine 0x141E8 (0x1422C): the view switch proper. */
static void view_toggle(game_t *g) {
    g->req_view = 0;
    if (g->vs.level != 0) { g->vs.level = 0; g->vs.first = 0; g->leave_ext = 1; }   /* to the cockpit; the next external frame snaps */
    else g->vs.level = g->saved_level;                                               /* to the external view, level -1 */
    g->ac.f.v4a968 = 0xFE;                                                           /* the event echo byte */
    g->vs.head = 0;
    g->hud_step = g->vs.level != 0 ? 50 : 0;                                         /* external: step 50 (offset 800); cockpit: 0 */
    cockpit_zoom_reset(&g->ck);                                                      /* 0x1F060 at 0x142A8 (V:4A8F5 = 3: the redraw counter of the magnification digit, read by its drawer 0x36590, note 07 12.1: the port draws the digit from the scene every frame, so the counter has no visible effect) */
    sound_stop_voice(&g->snd, 3);                                                    /* 0x14282: the switch stops the tone on voice 3 (note 10 4.1, 7.3) */
    request_lists(g);                                                                /* 0x142B4 */
    redraw_helper(g);                                                                /* 0x142BC: the helper 0x1E7B2 (note 07 12.1 writer 4) */
}

/* ---- the controls as the flight model's key byte (note 05 section 2) ---------------------------------------------
 * PORT CHOICE (C, input device): the original is driven by the keyboard, the mouse and a digital stick (note 05 17.4, note 08 11.4), so the analogue axes are read as keys beyond a quarter of their travel; stick Y > 0 pulls back (nose up). The
 * throttle is the pointer throttle of the original (note 05 17.4): the mouse height P (V:4A830, 0..960 in 8 per throttle unit) drives the
 * throttle. The absolute lever axis (-32768 idle .. 32767 full; F9/F10 or analogue input) stands for the pointer: PORT CHOICE (input device),
 * P = 960 * (axis + 32768) / 65535 is set at the next step (flight_inputs_t.pointer_set), and the bottom of the travel (below 8) is a negative P, the idle preset. The relative mouse arrives as GAME_AXIS_POINTER_DY and
 * is summed per step (pointer_dy). The function keys are GAME_BTN_FKEY events (in.preset, with P = Q = min(8 x preset, 960)). */
#define AXIS_KEY_THRESHOLD 8192
static void axes_to_keys(game_t *g) {
    uint8_t k = 0;
    if (g->stick_x > AXIS_KEY_THRESHOLD) k |= FLIGHT_KEY_ROLL_RIGHT;
    else if (g->stick_x < -AXIS_KEY_THRESHOLD) k |= FLIGHT_KEY_ROLL_LEFT;
    if (g->stick_y > AXIS_KEY_THRESHOLD) k |= FLIGHT_KEY_STICK_BACK;
    else if (g->stick_y < -AXIS_KEY_THRESHOLD) k |= FLIGHT_KEY_STICK_FORWARD;
    if (g->rudder > AXIS_KEY_THRESHOLD) k |= FLIGHT_KEY_RUDDER_RIGHT;
    else if (g->rudder < -AXIS_KEY_THRESHOLD) k |= FLIGHT_KEY_RUDDER_LEFT;
    g->keys_axes = k;
}

static void flight_axis(game_t *g, const game_event_t *ev) {
    if (ev->code == GAME_AXIS_THROTTLE) {
        g->fin.pointer_set = 1;
        const int32_t p = (int32_t)(960 * ((int64_t)ev->value + 32768) / 65535);
        /* 17.4: the original reaches idle only through a negative P (rule 2 sets P = 0 and the preset -1 when |P - Q| >= 17). A height of -1 does not
         * reliably fire that when Q <= 16, so the bottom of the lever (below 8) sends -960, the mouse's lower stop, which does. */
        g->fin.pointer_value = (int16_t)(p < 8 ? -960 : p);
    } else {
        axes_to_keys(g);
    }
}

/* SHIFT and KP-ENTER (0x1E3EE, note 07 6.3): the camera point of the start location, V:4A86D cleared; in the cockpit the key enters the external view as KP-ENTER does, in an external view
 * it clears V:4A85E. Without the AI tables (the airfield points are theirs) only the carrier starts have a point. */
static void fixed_camera(game_t *g) {
    int32_t pos[3];
    if (!g->has_flight) return;
    entities_sync_carrier(g);
    if (entities_fixed_camera(&g->ent, g->scr.location, pos) != 0) return;
    for (int i = 0; i < 3; i++) g->vs.fixed_pos[i] = pos[i];
    g->vs.fixed = 1; set_v4a86d(g, 0); g->vs.first = 1;   /* V:4A85E = 0xFF: the next aim snaps */
    if (g->vs.level == 0) {
        g->vs.look = 1; g->vs.map = 0;
        view_request_tail(g);
    } else g->vs.first = 0;
}

/* PORT FEATURE: the W key (GAME_BTN_REPLENISH) runs the original's dead key body 0x1ECAE, which calls the rearm routine 0x1F11E (note 08 11.6, note 07 9.7): the one rearm body of the landing
 * (flight_rearm: the fuel V:4B2AE = 0x61A800, the message entry 5, 0x4005, posted with the latch unconditionally, as 0x1F124 does) and the stores (weapons_rearm: 0x24, 500 rounds, 16 and 16, the
 * records 1 to 3 cleared), applied at once because a key acts in a pause too, where the step that consumes ev_rearm does not run. The guards are those of the control chain (note 08 11.1): the
 * ejection nibble of the gear byte (V:4B2B8) must be clear; the replay, the trainings' part 1, the approach and the outcome screens are filtered before this point.
 * Note 07 9.7: the dead key body 0x1ECA4 (it clears bit 6 of the word at A0 + D4 and sets no register) and the rearm call 0x1ECAE are two separate bodies and nothing reaches either one; the port
 * runs the 0x1ECAE body only (the rearm) and models no bit 6 clear. */
static void replenish(game_t *g) {
    if (g->ac.f.gear & 0x0F) return;
    flight_rearm(&g->ac.f);
    g->ac.f.ev_rearm = 0;
    weapons_rearm(&g->wp);
}


static void flight_button(game_t *g, const game_event_t *ev) {
    int down = ev->value != 0;
    /* the switch beep 0x37C4A reads V:4A83D when the key is handled (note 10 7.4, 7.4a): the weapons module's copy is refreshed by the gun pass only, so the keys that click through it (T, J) get the live
     * byte. The levels `[` and `]` store the byte in their handlers; the view requests of M and KP-ENTER leave it to the state routine of the pass (view_switch), as in the original. */
    g->wp.ext_view = (uint8_t)ext_view(g);
    if (down && !chain_open(g)) {
        switch (ev->code) {
        case GAME_BTN_GEAR: case GAME_BTN_HOOK: case GAME_BTN_BRAKE: case GAME_BTN_HUD_TOGGLE: case GAME_BTN_EJECT: case GAME_BTN_RESCUE:
        case GAME_BTN_FLARE: case GAME_BTN_CHAFF: case GAME_BTN_ECM: case GAME_BTN_TARGET: case GAME_BTN_MESSAGE_PAGE: case GAME_BTN_RADAR_RANGE:
            return;
        default: break;
        }
    }
    switch (ev->code) {
    case GAME_BTN_THROTTLE_UP:   g->keys_held = (uint8_t)(down ? ((g->keys_held & ~3) | FLIGHT_KEY_THROTTLE_UP) : (g->keys_held & ~FLIGHT_KEY_THROTTLE_UP)); break;
    case GAME_BTN_THROTTLE_DOWN: g->keys_held = (uint8_t)(down ? ((g->keys_held & ~3) | FLIGHT_KEY_THROTTLE_DOWN) : (g->keys_held & ~FLIGHT_KEY_THROTTLE_DOWN)); break;
    case GAME_BTN_FKEY:   /* F1..F10 (raw 0x50..0x59): the preset 12 n, F10 121; F1 again while the stored preset is 12 stores -1 (idle) (note 05 section 2) */
        if (ev->value >= 1 && ev->value <= 10) {
            int pre = ev->value == 10 ? 121 : 12 * ev->value;
            if (ev->value == 1 && g->ac.f.v4a928 == 12) pre = -1;
            g->fin.preset = (int8_t)pre;   /* flight_step stores it and sets P = Q = min(8 x preset, 960) (flight.h) */
        }
        break;
    case GAME_BTN_GEAR:  if (down) { g->fin.gear_toggle = 1; if (!sound_gear_key(&g->snd)) snd_key_beep(g); } break;   /* 0x1E93E: the gear sound's countdown V:4A93D = 0x83, or the key click without a gear sound (note 10 5.1, 7.4) */
    case GAME_BTN_HOOK:  if (down) { g->fin.hook_toggle = 1; snd_key_beep(g); } break;
    case GAME_BTN_BRAKE: if (down) { g->fin.brake_toggle = 1; snd_key_beep(g); } break;   /* the brake key toggles (note 05 section 2) */
    case GAME_BTN_HUD_TOGGLE: if (down) cockpit_toggle_hud(&g->ck); break;   /* the H key, V:4A859 (note 06, "HUD") */
    case GAME_BTN_EJECT: if (down && g->mode != GAME_MODE_PAUSED) { g->eject_armed ^= 1; g->fin.eject = 1; } break;   /* SHIFT-E: the lamp V:4A863 and the nibble (6.4) */
    case GAME_BTN_FIXED_CAMERA: if (down) fixed_camera(g); break;
    case GAME_BTN_RESCUE:
        /* 0x1EE4E: mode 6, latch clear, a free record among 1 to 3 -> the nibble, then 0x1A0DC(0x1C, 0x30) (note 10 8.2, 07 13.4; the 0xFA/hold fallback of 3.4) */
        if (down && g->scr.mode_byte == 6 && entities_rescue_key(&g->ent)) snd_noise(g, 0x1C, 0x30);
        break;
    case GAME_BTN_FIRE: weapons_key_fire(&g->wp, down); break;   /* SPACE (the trainings take it in training_key): the trigger, or a missile launch request (note 07 7.1) */
    case GAME_BTN_SELECT_WEAPON: case GAME_BTN_RETURN:   /* RETURN: the weapon cycle (modes other than 2 and 0x7D, whose RETURN is the sequencer's: training_key) */
        if (down && g->scr.mode_byte != 2 && g->scr.mode_byte != 0x7D) {
            snd_key_beep(g);   /* 0x1E8E6: the beep comes first and always, also while an ejection nibble keeps the cycle out (note 10 7.4) */
            if (weapons_key_select(&g->wp)) g->rd.prompt = 0;   /* the select clears the fire-prompt state V:4A96C (note 07 7.1) */
        }
        break;
    case GAME_BTN_FLARE: if (down) weapons_key_flare(&g->wp); break;   /* F, C, J act in a pause too (note 07 9.1) */
    case GAME_BTN_CHAFF: if (down) weapons_key_chaff(&g->wp); break;
    case GAME_BTN_ECM: if (down) weapons_key_ecm(&g->wp); break;
    case GAME_BTN_TARGET: if (down) { radar_key_target(&g->rd); weapons_tone_click(&g->wp); } break;   /* T: the designation request (served by the next scope walk) and the key click */
    case GAME_BTN_MESSAGE_PAGE: if (down) radar_key_page(&g->rd); break;   /* Y */
    case GAME_BTN_RADAR_RANGE: if (down) { radar_key_range(&g->ent); snd_key_beep(g); } break;   /* R (0x1DEF4: the click) */
    case GAME_BTN_REPLENISH: if (down) replenish(g); break;   /* W: PORT FEATURE */
    default: break;
    }
}

/* The flight-mode part of the input, after the queue of the hold (17.17 rule 6). */
static void flight_event(game_t *g, const game_event_t *ev) {
    if (ev->kind == GAME_EV_BUTTON && ev->value == 1 && (ev->code == GAME_BTN_ESC || ev->code == GAME_BTN_SHIFT_ESC) &&
        (g->mode == GAME_MODE_FLIGHT || g->mode == GAME_MODE_PAUSED)) {
        screen_key(&g->scr, ev);   /* ESC restarts the selection, SHIFT-ESC returns to the menu (note 08 5.11): the screen module's, acted on in the next step */
        return;
    }
    if (ev->kind == GAME_EV_BUTTON && ev->code == GAME_BTN_PAUSE && ev->value == 1 &&
        (g->mode == GAME_MODE_FLIGHT || g->mode == GAME_MODE_PAUSED)) {
        if (g->ms.key_lock) return;   /* a script's result is being handed over (V:4A965 = 1): P is not in the restricted key set (note 08 6.4) */
        if (g->scr.state == SCR_ESC_FADE) {   /* P during an ESC countdown is the same key handler as anywhere (note 06 item 8): with the pause byte non-zero it clears it (V:4A866 = 0, V:4A968 = 0xFE); the countdown, the gate pulse and the
                            * reset run on at their times. The byte is the 1 the ESC stored (an ESC not yet seen by the game loop is withdrawn too): a trigger that holds then starts its count from 0 in the gate pulse instead of
                            * firing there (12.11). A further P finds the byte 0 and stores 0xFF in it again (the flight state that would open the pause screen is not running during the countdown), and so on, each P flipping the byte. */
            if (g->ac.f.v4a866 == 0 && !g->scr.out_esc && g->mode == GAME_MODE_FLIGHT) { g->ac.f.v4a866 = 0xFF; sound_stop_all(&g->snd); return; }   /* the pause key's store stops all four voices (0x1EDC0, note 10 4.1) */
            g->ac.f.v4a866 = 0; g->scr.out_esc = 0;
            if (g->mode == GAME_MODE_PAUSED) { g->mode = GAME_MODE_FLIGHT; g->ac.f.v4a968 = 0xFE; g->unpause_sound = 1; screen_pause(&g->scr, 0); }
            return;
        }
        /* The outcome screens (note 08 5.14 (2), followup-pause-screens): P changes nothing there, the byte stays 1; only ESC and SHIFT-ESC are taken (the restricted key set, 6.4). */
        if (screen_in_outcome(&g->scr)) return;
        /* The take-off notice and the wait state 0x12EDA (5.14 (3), followup-pause-screens and -pause-notice): P is accepted and sets the byte to 0xFF, but no pause screen opens (these states are neither the
         * flight state nor the approach state); the notice and the wait run on, the byte still set, and the pause screen opens at the first flight pass. A second P before that clears the byte (V:4A968 = 0xFE) and
         * the flight starts normally. */
        if (g->has_flight && g->mode == GAME_MODE_FLIGHT && (g->scr.state == SCR_NOTICE || g->scr.state == SCR_WAIT)) {
            if (g->ac.f.v4a866 == 0) { g->ac.f.v4a866 = 0xFF; g->notice_pause = 1; sound_stop_all(&g->snd); }
            else { g->ac.f.v4a866 = 0; g->notice_pause = 0; g->ac.f.v4a968 = 0xFE; g->unpause_sound = 1; }
            return;
        }
        /* P while a script's count is running (the pause byte V:4A866 non-zero, 12.2, 12.12): the key handler sees the byte non-zero and clears it, with no pause screen; the next pass in which the
         * trigger still holds restarts the count from its start value. With the byte at 0 (12.11, 5.14 (1)): the key is polled at the start of a pass, the handler stores 0xFF and the flight state of that same
         * pass takes it: exactly one pass runs with the byte set and it is a paused one (no step counter, no player step), so the pause is entered in the step in which P arrives (below). */
        if (g->has_flight && g->mode == GAME_MODE_FLIGHT && g->ac.f.v4a866 != 0) { g->ac.f.v4a866 = 0; return; }
        if (screen_sim_hold(&g->scr) && g->scr.state != SCR_APPROACH) return;   /* the approach state 0x12D58 starts the pause (5.14); the selection screens' P is the screen module's (no pause screen, 6.4) */
        if (g->mode == GAME_MODE_FLIGHT) { g->mode = GAME_MODE_PAUSED; screen_pause(&g->scr, 1); sound_stop_all(&g->snd); }   /* pause on: all four voices stop (0x1EDC0); no handler touches n on a pause (note 06 "View kinds, settled" item 8) */
        else { g->mode = GAME_MODE_FLIGHT; g->ac.f.v4a968 = 0xFE; g->unpause_sound = 1; screen_pause(&g->scr, 0); }   /* the second P: V:4A968 = 0xFE */
        return;
    }
    if ((g->mode != GAME_MODE_FLIGHT && g->mode != GAME_MODE_PAUSED) || !g->world) return;
    if ((screen_sim_hold(&g->scr) || g->ms.key_lock) && g->scr.state != SCR_ESC_FADE && ev->kind != GAME_EV_AXIS) return;   /* the approach, the notice and the outcome screens take no flight key (the restricted key set, note 08 6.4); a script's result locks them from its store (V:4A965 = 1, 12.2) */
    if (training_key(g, ev)) return;   /* the trainings: RETURN, the trigger and S; part 1 drops the flight controls (note 08 8.6, 8.8) */
    /* The key routine 0x1DAA0 runs in every pass, paused or not (note 06 item 8): every key event goes through during a pause, so a key released in a pause does not stick; the view, zoom
     * and level keys act at once (their effect shows after the pause: the world is not redrawn and the view switch waits for the first flight pass) and the pointer and the axes keep summing.
     * Every handler acts in a pause (gear, hook, brake, throttle bits, H, cursor keys, flare, chaff, ECM, SPACE, RETURN) except E (the eject arming tests the pause byte and does nothing) and P; the flight toggles are taken by the first step after the pause. ESC in a pause starts the ESC flow inside the pause (ESC_PAUSE_STEPS). */
    if (ev->kind == GAME_EV_BUTTON) {
        /* RETURN is the weapon-select key in free flight (the frontend sends SELECT_WEAPON in flight; the cycle is the weapons slice's): there is no
         * catapult re-position (17.17 rule 9). ESC leaves the flight: V:4A84D = 0 at once, n is cleared ESC_STEPS later, then the mission screen. */
        if (g->has_flight && !g->freecam) flight_button(g, ev);
        view_event(g, ev);
        return;
    }
    if (ev->kind == GAME_EV_AXIS) {
        if (ev->code == GAME_AXIS_POINTER_DY) {   /* the mouse: summed until the next step */
            if (g->has_flight && !g->freecam) {
                int64_t d = (int64_t)g->pointer_dy + ev->value;
                g->pointer_dy = (int)(d > 32767 ? 32767 : d < -32768 ? -32768 : d);
            }
            return;
        }
        if (ev->code == GAME_AXIS_STICK_X) g->stick_x = ev->value;
        else if (ev->code == GAME_AXIS_STICK_Y) g->stick_y = ev->value;
        else if (ev->code == GAME_AXIS_THROTTLE) g->throttle = ev->value;
        else if (ev->code == GAME_AXIS_RUDDER) g->rudder = ev->value;
        else return;
        if (g->has_flight && !g->freecam) flight_axis(g, ev);
        world_set_controls(g->world, g->stick_x, g->stick_y, g->throttle);
    }
}

/* The player's start (the old RETURN of the splash, now the screens' out_place or the debug start): the world and the aircraft, the cockpit and the view, from the
 * location and aircraft the screens chose (V:4A900, V:4A901). The flight is shown frozen until the screen module releases the simulation. */
static void start_player(game_t *g, int hud_on) {
    if (!g->world) return;
    g->preview = 0;
    if (g->scr.mode_byte < 3 || g->scr.mode_byte > 8) missions_start(&g->ms, 0, g->scr.location);   /* a flight outside the missions: no script state, the keys unlocked (a mission's own state was cleared by its handler) */
    select_start_block(g);
    entities_begin(g);   /* the records are cleared and the mission's entities placed (the carrier of a mission first: it moves the ship record) */
    place_carrier(g, g->scr.demo);
    world_start(g->world);
    if (g->has_flight) {
        place_start(g);
        g->ent.rec[0].b71 = 0xFF;   /* the flight start 0x1D8B8 stores the player's +0x71 (0x1D91C) */
        g->allow = g->scr.allowance;
        g->crash = CR_NONE; g->crashes = 0; g->hold_steps = 0; g->hold_pending = 0; g->resume = 0; g->pointer_dy = 0; g->notice_pause = 0;
        g->queued = 0;
        g->air_steps = 0; g->airborne = 0;   /* 10.7: the flight starts on the ground, its interval open */
        memset(&g->fin, 0, sizeof g->fin);
        g->keys_held = g->keys_axes = 0;
    } else {
        world_aircraft_start(g->world, &g->ac);
    }
    cockpit_mission_start(&g->ck);   /* n = 0, the tick phase pin, the arbiter cleared (note 06) */
    g->ck.hud_on = (uint8_t)hud_on;  /* the reset block turns the HUD off, 0x12EDA on (note 08 6.1, 6.2); the debug start has it on */
    sky_strain_reset(&g->strain); g->sec_steps = 0; g->zone953 = g->zone954 = 0; g->fade_target = 15; memset(&g->sky_st, 0, sizeof g->sky_st); g->rear_age = -1;
    g->stick_x = g->stick_y = 0;
    g->throttle = -32768;   /* idle: speed 0 until a throttle event */
    memset(&g->vs, 0, sizeof g->vs);   /* the cockpit view, direction 4, head 0; the look-at and aim flags set (KP-Enter sets them too) */
    g->vs.ring = 4; g->vs.look = 1; set_v4a86d(g, 1);   /* 0x1D4EE */
    mapmarks_init(&g->mm);   /* every record's +0x71 is 0xFF from its placement */
    g->req_view = 0; g->saved_level = -1; g->saved_alt = WORLD_MAP_ALT_START; g->hold = -1; g->hud_step = 0; g->leave_ext = 0;
    g->appr = 0; g->appr_done = 0; g->appr_row = 0;
    training_begin(g);   /* the trainings: record 4 and the part 1 camera (after the view state is reset) */
    g->mode = GAME_MODE_FLIGHT;
    apply_camera(g);
}

void game_debug_start_flight(game_t *g, int location, int aircraft) {
    if (!g || !g->world) return;   /* without world data nothing starts (the flight needs the models) */
    screen_debug_flight(&g->scr, location < 0 || location > 3 ? 0 : location, aircraft);
    start_player(g, 1);
    sound_master(&g->snd, -1, 63, 0);   /* the dispatch's sound (10 6.2): the flag 0, the music stopped, the level 0x3F0000, then the wind */
    sound_stop_voice(&g->snd, 0); sound_stop_voice(&g->snd, 1);
    snd_flight_start(g);
    g->v4a90f = 1;   /* the debug start has no flow states before it: the value the reset 0x1D25E leaves */
    g->mode = g->scr.mode;
    /* The --flight debug start (C): the display pass the approach scene gives the real start before the first flight step (the carrier's slots are the draw store, note 05 open question 9: the carrier must have been drawn before the first step) */
    if (g->has_flight && g->mode == GAME_MODE_FLIGHT) { apply_camera(g); lists_step(g); sky_frame(g); lists_walk_step(g); }
}

void game_input(game_t *g, const game_event_t *ev) {
    if (!g || !ev) return;
    if (g->rep.active) {   /* the demo flight (note 08 7.6): only the system keys act, P and ESC; the controls come from the stream and the mouse does not move the throttle */
        const int sys = (ev->kind == GAME_EV_KEY && ev->value == 27) ||
                        (ev->kind == GAME_EV_BUTTON && (ev->code == GAME_BTN_ESC || ev->code == GAME_BTN_SHIFT_ESC || ev->code == GAME_BTN_PAUSE));
        if (!sys) return;
    }
    if (g->mode != GAME_MODE_FLIGHT && g->mode != GAME_MODE_PAUSED) {
        /* every other screen: the key queue of the screen module (note 08 5.8); axes mean nothing outside the flight */
        if (ev->kind == GAME_EV_KEY || ev->kind == GAME_EV_BUTTON) screen_key(&g->scr, ev);
        return;
    }
    /* During the hold the frame routine (which polls the keys) does not run, but the keys are not lost: the system's keyboard handling queues the
     * events and pass A delivers them, in arrival order, before its dispatcher (17.17 rule 6). The pointer and the axes are the interrupt's. */
    if (g->mode == GAME_MODE_FLIGHT && g->crash == CR_HOLD && ev->kind == GAME_EV_BUTTON) {
        if (g->queued < EVENT_QUEUE) g->queue[g->queued++] = *ev;   /* PORT CHOICE: a hold of 1.6 s cannot overflow 64 key events; the rest is dropped */
        return;
    }
    if (ev->kind == GAME_EV_KEY || (ev->kind == GAME_EV_BUTTON && ev->code == GAME_BTN_KEYPAD && ev->value == GAME_KP_ENTER && g->scr.state == SCR_APPROACH)) {
        screen_key(&g->scr, ev);   /* KP-ENTER skips the approach (camera mode 6, note 08 6.4) */
        return;
    }
    flight_event(g, ev);
}

/* After the player's step (note 08 9.3, 10.2, 10.7): the lift-off and touchdown events open and close the clock's airborne interval, a crash counts at +0x46 and closes it
 * as a touchdown does, the qualification's single success test is evaluated once per world step. */
/* The weapon counters (+0x3A to +0x44, the AIM-120 fired counter +0x3E among them) are incremented by the same code in the demo as in play and set the dirty flag (7.6, 10.7); the weapons module
 * raises them (weapons.c), so the fire event of the shipped stream (frames 599 to 602) raises +0x3E. */
static void log_after_step(game_t *g) {
    flight_state_t *f = &g->ac.f;
    const int clock_on = !g->scr.v4a848;                     /* V:4A848 gates the mission clock only (7.6, 10.7: the demo's flight time is not booked); the crash counters +0x46 and +0x10 and the weapon counters are not gated by it */
    if (f->ev_touchdown) g->airborne = 0;                    /* the touchdown, 0x16F6A */
    if (f->ev_liftoff) g->airborne = 1;                      /* the lift-off, 0x171E2 */
    if (f->ev_crash) {                                       /* the crash writers 0x16FB0 and 0x2A8C6: +0x46, and the interval closes as at a touchdown */
        logbook_add(&g->log, LB_CRASHES, 1);                 /* 0x16FB0 and 0x2A8C6 carry no demo test (7.6) */
        g->airborne = 0;
    }
    f->ev_liftoff = f->ev_touchdown = f->ev_crash = 0;
    if (!clock_on) g->airborne = 0;
    if (g->airborne && !f->v4a866) g->air_steps++;
    if (g->scr.mode_byte == 9 && g->scr.state == SCR_FLIGHT && logbook_qual_success(f->w0, f->w2, f->speed, 0, f->v4a850)) {
        f->v4a850 = 0xFF;                                    /* 9.3: the success code; the outcome screen, the log and the restart follow (screen.c) */
        screen_mission_end(&g->scr, 0xFF, 0);
    }
}

/* V:4A992 as the pass reads it (note 07 7.3, "The frame counter at the call"): n_draw is the pre-increment value of an unpaused pass; in a counted pass (the pause byte set by a script) n stands still
 * and the pass reads the current n. */
static uint32_t pass_n(const game_t *g) { return g->ac.f.v4a866 ? g->ck.n : g->ck.n_draw; }

/* The damage bits of a scene entry (GAME_DMG_*): the record's W0, B20, W2 and B4 as the walk draws it. */
static uint8_t entry_damage(const flight_state_t *fi) {
    return (uint8_t)(((fi->w0 & 0x8000) ? GAME_DMG_HIT : 0) | ((fi->w0 & 0x0200) ? GAME_DMG_CRASHED : 0) | ((fi->w0 & 0x0400) ? GAME_DMG_DYING : 0) | ((fi->b20 & 0x02) ? GAME_DMG_OUT_OF_CONTROL : 0) | ((fi->w2 & 0x0080) ? GAME_DMG_GROUND : 0) | ((fi->b4 & 0x20) ? GAME_DMG_THREATENED : 0));
}

/* The display-list frame (note 04a): the entity records the builder reads, the viewed entity, then the landmark slots for the next step. */
static void lists_step(game_t *g) {
    lists_frame_t in;
    memset(&in, 0, sizeof in);
    in.n = pass_n(g);
    const int fly = g->has_flight && !g->freecam && !g->preview;
    if (g->preview) { in.external = 1; in.viewer_valid = 0; }   /* the map overview: an external camera, no player record (it is placed when the flight starts) */
    if (fly) {
        const flight_state_t *f = &g->ac.f;
        lists_entity_t *p = &in.ent[0];
        p->active = 1; p->b4 = f->b4;
        p->cell_x = (uint8_t)(f->cell_x & 0x7F); p->cell_z = (uint8_t)(f->cell_z & 0x7F); p->sub = f->cell_a;
        p->fine_x = f->fine_x; p->fine_z = f->fine_z; p->alt8 = f->alt8;
        for (int i = 0; i < 3; i++) p->pos[i] = g->ac.pos[i];
        p->model = (uint16_t)((f->cls & 0x0F) == 1 ? MODEL_FA18 : MODEL_F16);
        p->pitch = g->ac.pitch; p->roll = g->ac.roll; p->yaw = g->ac.yaw;
        memcpy(p->mat_b, f->mat_b, sizeof p->mat_b);   /* the draw rotates the player's block vertices by matrix B into its slots 0 to 3 (07 8.10): the ground test of the next step reads them */
        in.viewer_valid = !ext_view(g);
        in.v_cell_x = p->cell_x; in.v_cell_z = p->cell_z; in.v_sub = p->sub;
        in.rates[0] = f->rate_p; in.rates[1] = f->rate_h; in.rates[2] = f->rate_r;
        in.speed_target = f->speed_target;
        in.external = ext_view(g); in.head = g->vs.head; in.leave_external = g->leave_ext;
        {   /* the zoom word V:4AAFA, the cockpit's own field; the viewed record index is 0 (the player), alt_same stays 0 (templates 3 and 4 have their own alternate list, 04a 8a test 5) */
            game_hud_t hz; memset(&hz, 0, sizeof hz);
            cockpit_fill_hud(&g->ck, &g->ac, &hz);
            in.zoom = hz.cp_zoom; in.viewed = 0;
        }
        p->b7c = f->gear;  /* +0x7C: the gear progress the aircraft models' op 52 selects the gear step by */
        p->cls = f->cls;   /* +0x62, read by the op 4 gate */
        p->w4e = f->deck;  /* +0x4E: the op 4 gate's height adjustment D = -w4e * 256 when b4 has both bits 6 and 7 (the deck: 0xC8 and 112, note 03) */
        world_lists(g->world)->part_id[0] = f->b7b;   /* the record keeps its byte unless the draw of this pass writes it */
    }
    if (fly) {   /* the records 1 to 15 but the carrier: the training instructor (note 08 8.3), the placed aircraft and ships, the seat and the weapons; the viewed record: the scan runs around it */
        const int vi = viewed_index(g);
        for (int i = 1; i < ENT_RECORDS; i++) {
            if (i == ENT_CARRIER) continue;
            const aircraft_t *ia = entities_ac_c(&g->ent, i);
            const flight_state_t *fi = &ia->f;
            const int lead = i == ENT_TRAINING_LEAD && g->tr.part;
            if (!lead && !(fi->w0 & 0x0040)) continue;
            lists_entity_t *e = &in.ent[i];
            e->active = (fi->w0 & 0x0040) != 0; e->b4 = fi->b4;   /* 8.9: a lost lead (W0 bit 0x0040 clear) is not drawn */
            e->cell_x = (uint8_t)(fi->cell_x & 0x7F); e->cell_z = (uint8_t)(fi->cell_z & 0x7F); e->sub = fi->cell_a;
            /* Port rule (C), note 08 8.1 "Frames and the lead behind the briefing": the original's scene passes run a world update before the briefing's picture, so the lead's +0x0A is already
             * computed (10 in part 1, 13 in part 2) and the lead is visible over the bay; the port computes the sub-cell index from the placed fine words until the first pass (W0 bit 0x0100) */
            if (fi->w0 & 0x0100) e->sub = (uint8_t)(4 * (3 - (fi->fine_z >> 12)) + (3 - (fi->fine_x >> 12)));
            e->fine_x = fi->fine_x; e->fine_z = fi->fine_z; e->alt8 = fi->alt8;
            for (int k = 0; k < 3; k++) e->pos[k] = ia->pos[k];
            e->model = lead ? MODEL_F16 : g->ent.rec[i].template_id;
            e->pitch = ia->pitch; e->roll = ia->roll; e->yaw = ia->yaw;
            e->b7c = fi->gear; e->cls = fi->cls; e->w4e = fi->deck;
            memcpy(e->mat_b, fi->mat_b, sizeof e->mat_b);   /* the draw rotates the record's model vertices by it: the gun's hull slots (note 07 8.2a) */
            e->damage = entry_damage(fi);
            e->phase = fi->counter;
            world_lists(g->world)->part_id[i] = fi->b7b;
            if (vi == i) {
                in.v_cell_x = e->cell_x; in.v_cell_z = e->cell_z; in.v_sub = e->sub;
                in.rates[0] = fi->rate_p; in.rates[1] = fi->rate_h; in.rates[2] = fi->rate_r;
                in.speed_target = fi->speed_target;
                in.viewed = i;
            }
        }
        in.viewer_valid = !ext_view(g) && vi == 0;
        for (int k = 0; k < ENT_OBJECTS; k++) {   /* the secondary objects (note 07 1.5): explosion fragments and cones */
            const secondary_t *o = &g->ent.obj[k];
            lists_entity_t *e = &in.sec[k];
            if (!(o->w0 & 0x0040)) continue;
            e->active = 1;
            e->cell_x = (uint8_t)(o->cell_x & 0x7F); e->cell_z = (uint8_t)(o->cell_z & 0x7F); e->sub = o->sub;
            e->fine_x = o->fine_x; e->fine_z = o->fine_z; e->alt8 = o->alt8;
            for (int j = 0; j < 3; j++) e->pos[j] = o->pos[j];
            e->model = o->model;
            e->phase = o->life;
        }
    }
    {   /* the carrier, record 14 (class 0x20) */
        int32_t cp[3]; int have = 0;
        if (g->has_flight && g->fenv.ship.present) { for (int i = 0; i < 3; i++) cp[i] = g->fenv.ship.pos[i]; have = 1; }
        else if (world_position_entry(g->world, WORLD_ENTRY_CARRIER, cp) == 0) have = 1;
        if (have) {
            lists_entity_t *c = &in.ent[14];
            uint32_t px = (uint32_t)cp[0] & WORLD_POS_MASK, pz = (uint32_t)cp[2] & WORLD_POS_MASK;
            c->active = 1;
            c->cell_x = (uint8_t)(px >> 22); c->cell_z = (uint8_t)(pz >> 22);
            c->fine_x = (int16_t)((px & 0x3FFFFF) >> 8); c->fine_z = (int16_t)((pz & 0x3FFFFF) >> 8);
            c->sub = (uint8_t)(4 * (3 - (c->fine_z >> 12)) + (3 - (c->fine_x >> 12)));
            c->alt8 = cp[1] >> 8;
            c->pos[0] = cp[0]; c->pos[1] = cp[1]; c->pos[2] = cp[2];
            c->model = WORLD_CARRIER_ID;
            c->cls = 0x20;   /* +0x62 of the carrier (note 05 section 16), read by the op 4 gate */
            if (g->has_flight && g->fenv.ship.present) memcpy(c->mat_b, g->fenv.ship.mat_b, sizeof c->mat_b);   /* the draw rotates model 8's vertices by the carrier's matrix into its slots (note 05 section 9) */
            c->yaw = g->carrier_heading;
        }
    }
    if (fly) { world_lists_request(g->world, g->ac.f.v4a910); g->ac.f.v4a910 = 0; }   /* the position update's cell-change request */
    world_set_window_last_row(g->world, (seat_view(g) && !g->seat_wait) ? 167 : g->appr_row ? g->appr_row : world_window_last_row(map_view(g), ext_view(g), g->vs.head));   /* V:4AA3C: the point sprites count against it (note 03); the seat's cockpit view 167 (note 07 6.4) */
    world_lists_build(g->world, &in);   /* the builder; the sky fill and the sea mesh follow (sky_frame), then the walk (lists_walk_step): the original's order */
    g->fenv.n_landmarks = world_landmarks(g->world, g->fenv.landmark);
}
static void lists_walk_step(game_t *g) {
    const int fly = g->has_flight && !g->freecam && !g->preview;
    world_lists_walk(g->world);
    if (world_lists(g->world)->dist_words_new) {   /* the counter-14 draw of a dying record wrote V:4A922, V:4A921 and V:4A870 (07 11.4): the explosion's bang, played by the sound routine of the same pass (the entity pass precedes it, note 10 5.7 item 11) */
        const uint8_t *dw = world_lists(g->world)->dist_words;
        sound_bang_request(&g->snd, dw[1], dw[0], dw[2]);
        world_lists(g->world)->dist_words_new = 0;
    }
    if (fly) { g->ac.f.b7b = world_lists(g->world)->part_id[0]; g->leave_ext = 0; }
    if (fly) memcpy(g->ac.f.vtx, world_lists(g->world)->vslot[0], sizeof g->ac.f.vtx);   /* the player's slots 0 to 3 are the draw store's too: the ground test, the landmark phase and the ship test of the next step read them (note 05 17.11/17.12, 07 8.10) */
    if (fly)   /* +0x7B: the collision of the next step reads it (note 04a section 8a); +0xA4 slots 0 to 3: the collision routine of a non-player mover reads them (note 07 10.2, 05 17.11; the ground test and the landmark phase: slots 0 to 2) */
        for (int i = 1; i < ENT_RECORDS; i++) {
            flight_state_t *fi = &entities_ac(&g->ent, i)->f;
            memcpy(fi->slot, world_lists(g->world)->vslot[i], sizeof fi->slot);
            if (i != ENT_CARRIER && ((fi->w0 & 0x0040) || (i == ENT_TRAINING_LEAD && g->tr.part))) fi->b7b = world_lists(g->world)->part_id[i];
        }
}

/* ---- the sky fill, the zone flags, the sea mesh and the fade target of one pass (docs/re/04-scene-composition.md) ------------------------------------------ */

static int wrap_circle(int32_t a) { a %= 28800; return (int)(a < 0 ? a + 28800 : a); }

/* The zoom word V:4AAFA (the cockpit's own field, 0x80 at the start). */
static int current_zoom(const game_t *g) {
    game_hud_t hz;
    memset(&hz, 0, sizeof hz);
    cockpit_fill_hud(&g->ck, &g->ac, &hz);
    return hz.cp_zoom ? hz.cp_zoom : 0x80;
}

/* The turn-rate cue 0x270A0, once per world update and in a paused pass too, from the pitch rate and V:4A9FE as they stand; the mission clock second (0x298B4) advances the strain accumulator. The second
 * is the per-second counters' block, inside the drawn frame's limiter, which a pause does not reach (note 06 "The hold timer does not run in a pause" [V: views-m3-fix S4b], note 04 "blackout accumulator"):
 * a paused pass does not count it. The second has no phase against the mission (V:4ABAA is the system clock's seconds, note 10 5.1 item 2, [V: sound7-tick]: the tick has no fixed phase, so the port
 * counts FLIGHT_STEP_HZ passes: the eight steps a second are C and the phase is arbitrary, unobservable). */
static void strain_pass(game_t *g, int paused) {
    if (!g->has_flight || g->freecam) return;
    const uint8_t was = g->strain.latch;
    sky_strain_cue(&g->strain, g->ac.f.rate_p, g->ac.f.v4a9fe);
    if (g->strain.latch && !was) sound_event(&g->snd, SND_BEEP_N, 8, (uint8_t)g->vs.level);   /* 0x270CE: the high turn rate's HUD beep on the rising edge of V:4A940 (note 10 7.4) */
    if (!paused && ++g->sec_steps >= FLIGHT_STEP_HZ) { g->sec_steps = 0; sky_strain_second(&g->strain); }
}

/* The side flag (0x143F2, before the display-list builder): written into V:4A982 bit 1 for the cockpit's G readout as well. */
static int side_flag_for(const game_t *g, const world_camera_t *cam) {
    const int cockpit = !ext_view(g);
    return sky_side_flag(cockpit, wrap_circle(-cam->pitch), wrap_circle((int32_t)cam->roll));
}

/* Called after the display-list builder of the pass: the camera words, the polygon of the sky (and the attitude ball's second call), the zone flags (V:4A953 on
 * even frame numbers, V:4A954 on odd ones, from the builder's latched cell words), the fade target and, over land, the sea mesh polygons. */
static void sky_frame(game_t *g) {
    game_sky_t *s = &g->sky_scene;
    const world_camera_t *cam = world_camera(g->world);
    const lists_t *l = world_lists(g->world);
    const int cockpit = !ext_view(g);
    const int zoom = current_zoom(g);
    game_camera_t gc;
    fx_mat_t m;
    sky_poly_t poly;
    int i, mesh_ran = 0;
    if (!cam) return;
    memset(s, 0, sizeof *s);
    memset(&gc, 0, sizeof gc);
    gc.pitch = cam->pitch; gc.yaw = (int32_t)cam->yaw; gc.roll = (int32_t)cam->roll;
    gc.view = current_view(g);   /* the external views build the matrix from the aim pair alone (fx_cam_yp) */
    world_camera_matrix_z(&gc, zoom, &m);
    s->valid = 1;
    for (i = 0; i < 9; i++) s->matrix[i] = m.m[i / 3][i % 3];
    s->cam_y = l->scan.cam_y;
    s->pitch_word = (int16_t)wrap_circle(-cam->pitch);
    /* the roll word V:4AB4A: the viewed record's in the cockpit; in every external view (the map and the tower included) the end of the external branch copies the PLAYER's record 0 roll (+0x6A) to it (note 05 17.14,
     * "The roll word V:4AB4A outside the cockpit"); only the whole-window branch of the ground polygon reads it (the vertex order), so it shows nowhere in the pixels */
    s->roll_word = (int16_t)wrap_circle(!cockpit && g->has_flight ? (int32_t)(int16_t)g->ac.f.roll : (int32_t)cam->roll);
    s->cockpit = (uint8_t)cockpit;
    s->row_limit = (uint8_t)(g->appr_row ? g->appr_row : world_window_last_row(map_view(g), ext_view(g), g->vs.head));
    s->zoom[0] = WORLD_ZOOM_ROW_X; s->zoom[1] = WORLD_ZOOM_ROW_Y; s->zoom[2] = (int16_t)zoom;
    /* the side flag counts R as 0 outside the cockpit (sky_side_flag), whatever the roll word */
    s->side_flag = (uint8_t)side_flag_for(g, cam);
    if (g->has_flight && !g->freecam) { g->ac.f.v4a982 = (uint16_t)((g->ac.f.v4a982 & ~2u) | (s->side_flag ? 2u : 0u)); }
    /* the zone flags: the lookup of 0x230B8 or 0x230D8 at the builder's tail, with the cell words the builder latched */
    if (g->has_sky) {
        const int32_t alt = -l->scan.off_y;
        if ((pass_n(g) & 1) == 0)
            g->zone953 = (uint8_t)sky_zone_lookup(g->skyt.zone, SKY_ZONE_BYTES, l->scan.acell_z, l->scan.acell_x, alt, l->scan.quad, l->scan.octant);
        else
            g->zone954 = (uint8_t)sky_zone_lookup(g->skyt.zone2, SKY_ZONE2_BYTES, l->scan.acell_z, l->scan.acell_x, alt, l->scan.quad, l->scan.octant);
    }
    s->sea_zone = g->zone953; s->sea_zone2 = g->zone954;
    sky_polygon(&g->sky_st, s->matrix, s->cam_y, cockpit, s->side_flag, s->pitch_word, s->roll_word, &poly);
    s->sky_n = poly.n;
    for (i = 0; i < 2 * poly.n; i++) s->sky_xy[i] = poly.xy[i];
    if (cockpit && g->has_flight && !g->freecam) {
        /* the attitude ball: the same routine, second call, on the aircraft's attitude matrix V:4ACA2 (fx_cam_ypr of the aircraft's angles, no head offset and NO
         * zoom row scale: the nine captured balls are reproduced exactly with it, world.h). The words it reads are those the frame's camera call left: the side flag
         * V:4A982 bit 1 and the pitch and roll words V:4AB42 and V:4AB4A, the camera's (identical in the forward view, different in a head view), and the SAME
         * candidate state V:4AB7E (one sky_st: the ball's call starts with the camera call's last candidate, the next frame's camera call with the ball's) [V] */
        game_camera_t ac;
        fx_mat_t am;
        sky_poly_t bp;
        int16_t aw[9];
        memset(&ac, 0, sizeof ac);
        ac.pitch = g->ac.pitch; ac.yaw = g->ac.yaw; ac.roll = g->ac.roll;
        world_attitude_matrix(&ac, &am);
        for (i = 0; i < 9; i++) aw[i] = am.m[i / 3][i % 3];
        sky_polygon(&g->sky_st, aw, s->cam_y, 1, s->side_flag, s->pitch_word, s->roll_word, &bp);
        s->ball_n = bp.n;
        for (i = 0; i < 2 * bp.n; i++) s->ball_xy[i] = bp.xy[i];
    }
    /* V:4AA0C and V:4A79E as the sky fill finds them for the small polygons (note 04 "What the two variables hold in flight"): the colour by the view and the frame
     * counter, the mask 1 whenever a polygon was built. V:4A85A (the stage [90] flag, which keeps the filler's line case from forcing the mask 0xF) is 0 here, in the
     * sky fill and the mesh: their line case always forces 0xF; only the near-ground pass (stage [90]) sets it while it draws (raster_ground_triangle). */
    {
        const sky_tiny_view_t tv = cockpit ? SKY_TINY_COCKPIT : (map_view(g) ? SKY_TINY_MAP : SKY_TINY_EXTERNAL);
        if (cockpit && g->vs.head == 6) g->rear_age = g->rear_age < 0 ? 0 : g->rear_age + 1; else g->rear_age = -1;
        s->tiny_pen = (uint8_t)sky_tiny_pen(tv, g->rear_age, pass_n(g));
        s->tiny_mask = (uint8_t)sky_tiny_mask(tv, poly.n > 0, pass_n(g));
    }
    /* the fade target V:4A959 (0x13166): 2 while the accumulator exceeds 5 in the cockpit view of the own aircraft */
    g->fade_target = sky_fade_target(&g->strain, cockpit && !(g->has_flight && g->ac.f.v4a994));
    s->fade_target = (uint8_t)g->fade_target;
    s->strain = g->strain.acc;
    /* the sea mesh pass (stage [68]): only when V:4A953 is clear; the position is the viewed record's origin (cockpit) or the camera (external, map) */
    if (g->has_sky && !g->zone953) {
        mesh_ran = 1;   /* the mesh routine stores its block at entry, unconditionally: (2, 2, 0, 0), or (0xF, 0xFFFF, 0, 0) when V:4A868 is not zero, and the colour 6 before any cell scan; the call is skipped over the sea zone unless V:4A868 is set. The port has no V:4A868 (constant 0 by code, note 04 open item 4), so (2, 2, 0, 0) and 6 are stored whenever the stage runs (world_fill_block_sky) */
        world_mesh_pbuf_begin(g->world);   /* note 03 "Op 9 reads past its quad": the pass clears the polygon buffer's shift word at entry and writes every polygon to it, in order */
        static mesh_poly_t mp[MESH_MAX_POLYS];
        mesh_in_t in;
        int n, k;
        for (i = 0; i < 3; i++) in.pos[i] = cockpit ? cam->pos[i] - cam->eye[i] : cam->pos[i];
        for (i = 0; i < 9; i++) in.m[i] = s->matrix[i];
        in.alt = -l->scan.off_y;
        in.zoom = zoom; in.zoom_default = zoom == 0x80; in.map = map_view(g); in.own_alt = g->zone954;
        in.phase = l->scan.quad; in.octant = l->scan.octant;
        n = mesh_compute(&g->skyt, &in, mp, MESH_MAX_POLYS, NULL);
       
        for (k = 0; k < n; k++) {
            fx_vec_t v[MESH_MAX_VERTS];
            int c;
            for (c = 0; c < mp[k].n; c++) { v[c].x = mp[k].v[c][0]; v[c].y = mp[k].v[c][1]; v[c].z = mp[k].v[c][2]; }
            world_mesh_pbuf_polygon(g->world, v, mp[k].n);
            if (s->sea_count >= GAME_MAX_SEA_POLYS) continue;
            c = fx_clip(s->sea[s->sea_count].xy, GAME_SEA_POLY_VERTS, v, mp[k].n, 0);
            if (c >= 3) { s->sea[s->sea_count].n = (uint8_t)c; s->sea_count++; }
        }
    }
    /* the fill block the walk finds (the sky fill's tail, the sea mesh stage) and the buffer residue of the mesh polygons: this stage runs between the builder and the walk, as in the original */
    world_fill_block_sky(g->world, poly.n > 0, mesh_ran);
}

/* The map routine's entity loop (0x2FC58 at 0x11440, note 06 "Map renderer"): once per pass while the map is shown, from this pass's camera and counter. The stored bytes +0x70/+0x71 belong to the
 * records (entity_t.b70, b71) and are written by the placement, the launch, the rearm and the flight start; the routine itself only refreshes them. */
static void map_marks_pass(game_t *g, int map) {
    mapmarks_in_t in;
    memset(&in, 0, sizeof in);
    for (int i = 0; i < MAPMARKS_RECORDS; i++) {
        const aircraft_t *a = entities_ac_c(&g->ent, i);
        mapmarks_rec_t *r = &in.rec[i];
        r->w0 = a->f.w0; r->b03 = (uint8_t)(a->f.w2 & 0xFF); r->cls = a->f.cls; r->b20 = a->f.b20;
        r->x = a->pos[0]; r->z = a->pos[2]; r->heading = (int16_t)a->f.heading;
    }
    g->mm.count = 0;
    if (!map || !g->world) return;
    for (int i = 0; i < MAPMARKS_RECORDS; i++) { g->mm.b70[i] = g->ent.rec[i].b70; g->mm.b71[i] = g->ent.rec[i].b71; }   /* the records' own bytes (note 07 1.4): the placement, the launch, the rearm and the start write them, the routine only reads and refreshes */
    const world_camera_t *c = world_camera(g->world);
    in.cam_x = c->pos[0]; in.cam_y = c->pos[1]; in.cam_z = c->pos[2];
    in.n = (uint16_t)pass_n(g);
    in.mode = g->ac.f.v4a95e;
    in.v4a86d = g->vs.aim_on;
    in.v4a90f = g->v4a90f;
    in.last_row = (int16_t)world_window_last_row(1, 1, g->vs.head);
    mapmarks_pass(&g->mm, &in);
    for (int i = 0; i < MAPMARKS_RECORDS; i++) { g->ent.rec[i].b70 = g->mm.b70[i]; g->ent.rec[i].b71 = g->mm.b71[i]; }
}

/* The frame routine's own writer of R (0x114A0, note 07 12.1 writer 1): at the end of the draws, every 32nd pass (the pass counter n & 0x1F = 0x10) while the camera mode V:4A966 is 0. It sits before the
 * pause byte V:4A866 test, so a pass of a script's count (the pause byte set, the gate open) whose n stands at such a value asks again; a paused pass (the gate V:4A84D clear) does not reach it. */
static void periodic_redraw(game_t *g) {
    if (g->vs.mode == 0 && (pass_n(g) & 0x1F) == 0x10) redraw_helper(g);
}

/* The cockpit state of a paused pass: the generator and the arbiter run with the pause byte set, n stands still (note 06 "External views and pause"). */
static void paused_pass(game_t *g) {
    strain_pass(g, 1);
    cockpit_generator(&g->ck);
    cockpit_arbiter(&g->ck, &g->ac, 1);
    cockpit_end_pass(&g->ck, 1);
    /* The pause screen has the gate V:4A84D = 0: the frame routine skips 0x1114A to 0x114BE altogether (note 07 12.1 "The paused pass"): no decrement of R (0x113E8), no map call (0x11440), no periodic writer
     * (0x114A0), no scope and no HUD. The pause-byte test at 0x114A6 is not the gate. */
    g->rd.walk_done = 0;
}

/* The flight is lost: the reset block of note 08 6.1 on the way to a menu, an echo screen or a game over (17.17 rule 9: n is cleared, state 0x11A70). The screen the
 * player sees next is the screen module's (g->mode follows scr.mode). */
static void book_clock(game_t *g) {
    if (g->ac.f.v4a95e != 0 && g->ac.f.v4a95e != 0x7F && !g->scr.v4a848 && g->air_steps) logbook_book_flight(&g->log, g->air_steps);
    g->air_steps = 0;
}

static void leave_flight(game_t *g) {
    g->ms.key_lock = 0;   /* the keys of the flight are locked only while a script's result is handed over (V:4A965, 12.2) */
    /* 10.7 (0x11748): at the end of a flight the airborne unpaused steps are booked as seconds (8 steps each); not in the demo and only while the mode byte is above 0 (the flight's own copy V:4A95E: the screens have cleared the machine's by now) */
    book_clock(g);
    g->airborne = 0;
    cockpit_mission_start(&g->ck);
    g->crash = CR_NONE; g->hold_pending = 0; g->resume = 0; g->queued = 0;
    g->ac.f.v4a866 = 0;   /* the reset block clears the pause byte (5.11 [V: flow7-qual-restart]; the outcome screens held it at 1, 5.13) */
}

/* The map overview behind the location and aircraft screens (note 08 5.6, 6.1 item 7, note 06 item 2): the camera is planted by the reset block at x = 0x10C00000, z = 0x11400000 and the
 * height 0x05000000, looking straight down (the aim pair 7200, 0: north up), no look-at; the world is drawn as the map view of the M key is, without the grid and the labels (the cockpit
 * overlay is not drawn in these modes).
 * A MISSION BRIEFING holds the planted position for its whole length (note 08 13.4, C1 settled: "the camera holds the map overview position (x 0x10C00000, height 0x05000000, z 0x11400000)
 * ... unchanged to the unit"; stage 7 never flies), so MAP_PLANTED_* is what the briefing draws.
 * The SELECTION SCREENS of free flight stand at 0x10800000, 0x03000000, 0x10C00000 (note 04 "The start marks of the overview map": where the mover stands there, the planted values being where it
 * starts). There is no flight from the planted position to that standing position: the standing position is planted at the dispatch (blank 48 from the menu key) and the mover never runs before the approach
 * (note 08 6.1 "The camera before the location screen" [V: flow7-loc-mover]: V:4A86D 0, target 0), so the camera is at the standing position from the first picture. */
#define MAP_OVERVIEW_X 0x10800000
#define MAP_OVERVIEW_Y 0x03000000
#define MAP_OVERVIEW_Z 0x10C00000
#define MAP_PLANTED_X 0x10C00000
#define MAP_PLANTED_Y 0x05000000
#define MAP_PLANTED_Z 0x11400000
static int is_select(game_mode_t m) { return m == GAME_MODE_SELECT_LOCATION || m == GAME_MODE_SELECT_AIRCRAFT; }
static void preview_step(game_t *g) {
    if (!g->world || g->freecam) return;
    if (!g->preview) {
        g->preview = 1;
        world_start(g->world);
        memset(&g->vs, 0, sizeof g->vs);
        g->vs.level = 1; g->vs.ring = 4; g->vs.map = 1; g->vs.look = 0; set_v4a86d(g, 1); g->vs.first = 1;   /* the map behind the security and location screens (V:4A90F is the flow states' own: screen.h out_90f) */
        g->vs.aim_pitch = 7200; g->vs.aim_heading = 0;   /* the aim pair V:4AB4C = 0x1C20, V:4AB4E = 0 (the reset block): straight down, north up */
        g->hud_step = 50; g->leave_ext = 0;
    }
    const int brief = g->scr.mission != 0;   /* the mission briefing keeps the planted position (08 13.4 C1) */
    g->vs.map_y = brief ? MAP_PLANTED_Y : MAP_OVERVIEW_Y;
    g->ac.pos[0] = brief ? MAP_PLANTED_X : MAP_OVERVIEW_X; g->ac.pos[1] = 0; g->ac.pos[2] = brief ? MAP_PLANTED_Z : MAP_OVERVIEW_Z;
    apply_camera(g);
    lists_step(g);
    sky_frame(g);
    lists_walk_step(g);
}

/* What the screen module asks of the flight after its step (screen.h). */
/* The screen states whose passes are gate-1 passes that run the panel block outside the flight pass (SCR_WAIT is a flight state: radar_scope_pass counts there; a mission's stages before the fade's end
 * 0x1257A have the gate 0; the pause screen has the gate 0). */
static int gate1_screen(const screen_t *sc) {
    const screen_state_t st = sc->state;
    if (sc->paused) return 0;
    return st == SCR_LOC_PICKED || st == SCR_AIR_PICKED || (st == SCR_MISSION && sc->ms_stage >= MS_WAIT) || st == SCR_APPROACH || st == SCR_NOTICE;
}
/* The approach's view (08 12.14 C13, 06 start table [V: flow7-approach-air1 / -carrier / -training-7D / -training-2]). The free-flight approach (camera mode 6) is seen from outside: the external view, HUD step 50, window row
 * 179 (the dispatch value the selection screens keep), the camera flown by the mover from the selection camera to the start position; the row becomes 167 in the first pass in which the camera height long V:4ACFA is
 * below 0x24000 (the view-state routine's tail, which also runs the view refresh); when the mover has set the camera mode 0 the cockpit request is served in the next pass (step 0, row 144, view 0). Training part 2 is
 * seen from its fixed camera point from the dispatch (step 50, row 167) and the mover flies from stage 3 over the approach's 31 passes. A KP-ENTER skip, or an approach whose count ends before the mover, switches at once.
 * The missions F1 to F6 (SV_MISSION, 12.14 C14 [V: sound7-approach]) are seen from outside too: the mover enters in stage 8 at the briefing camera's overview position (13.4 C1), reads stage 0 from its fourth pass, and its own
 * end decides the count (screen_set_approach_len: a run ahead on a copy). NOTE-GAP (note 08 12.14 C14): the log peeked the view byte, the stage, the camera mode and the counter but not the camera longs, the HUD step or the
 * window row of a mission approach: the path from the overview position, the entry trail (empty here), the stage 8 length (4 passes) and the step 50 and rows 179 then 167 (R: the dispatch value of modes 3 to 8 and the view routine's tail) are
 * the free-flight rules carried over; the F1 log's stages 1 to 5 agree to the pass, its stage 6 comes one pass later than here. */
static mover_ctx_t mover_ctx(const game_t *g) {
    mover_ctx_t c;
    c.pos = g->ac.pos; c.mat = g->ac.f.mat_b; c.location = g->scr.location;
    return c;
}
static void approach_begin(game_t *g, int kind) {
    g->appr = 0; g->appr_done = 0; g->appr_row = 0;
    if (!g->has_flight || !g->world || (kind != SV_FREE && kind != SV_TRAIN2 && kind != SV_MISSION)) return;
    const mover_ctx_t c = mover_ctx(g);
    memset(&g->vs, 0, sizeof g->vs);
    g->vs.level = 1; g->vs.ring = 4; g->vs.fixed = 1; set_v4a86d(g, 1); g->vs.first = 1;
    g->hud_step = 50;
    if (kind == SV_FREE || kind == SV_MISSION) {
        if (kind == SV_FREE) mover_begin(&g->mv, &c); else mover_begin_mission(&g->mv, &c);
        g->vs.mode = 6; g->vs.look = 0;
        g->vs.aim_pitch = 7200; g->vs.aim_heading = 0;   /* the map's pair (7200, 0), kept until the look-at byte is set at stage 0's threshold */
        g->appr_row = 179;
    } else {
        const int32_t cam[3] = { MOVER_TRAIN2_X, MOVER_TRAIN2_Y, MOVER_TRAIN2_Z };
        mover_begin_stage3(&g->mv, &c, cam);
        g->vs.mode = 0xFF; g->vs.look = -1;   /* the training start's snap (0x12212) and the flow states' camera mode 0xFF until the approach state */
    }
    memcpy(g->vs.fixed_pos, g->mv.cam, sizeof g->vs.fixed_pos);
    if (kind == SV_MISSION) {   /* the mover decides the length (it depends on the start's position, which the placement draws): a run ahead on a copy; the cockpit pass is the one after the call that asks for it, as in the free-flight starts (118 for airfield 1 = 117 calls + 1, 104 for the carrier) */
        mover_t ahead = g->mv;
        int calls = 0;
        while (calls < 1000) { calls++; if (mover_step(&ahead, &c).cockpit) break; }
        screen_set_approach_len(&g->scr, calls + 1);
    }
    g->appr = (uint8_t)kind;
}
static void approach_cockpit(game_t *g) {
    g->appr = 0; g->appr_done = 0; g->appr_row = 0;
    const int look = g->tr.part ? -1 : 1;
    memset(&g->vs, 0, sizeof g->vs);
    g->vs.ring = 4; g->vs.look = look; set_v4a86d(g, 1);   /* the cockpit view of 0x1D4EE again */
    g->hud_step = 0; g->leave_ext = 0;
    request_lists(g);
}
/* One pass of the approach state (before the camera is built). */
static void approach_pass(game_t *g) {
    if (!g->appr || g->scr.state != SCR_APPROACH) return;
    if (g->appr_done) { approach_cockpit(g); return; }
    if (screen_approach_due(&g->scr)) { approach_cockpit(g); return; }   /* the screen's own count (the KP-ENTER skip, a count shorter than the mover) */
    if (g->vs.mode == 0xFF) g->vs.mode = 6;   /* the approach state sets the camera mode 6 (training part 2) */
    const mover_ctx_t c = mover_ctx(g);
    const mover_out_t o = mover_step(&g->mv, &c);
    memcpy(g->vs.fixed_pos, g->mv.cam, sizeof g->vs.fixed_pos);
    if (o.look_on) { g->vs.look = 1; g->vs.map = 0; }
    if (o.look_off && g->vs.look > 0) g->vs.look = 0;
    if (g->appr_row == 179 && g->mv.cam[1] < 0x24000) { g->appr_row = 167; request_lists(g); }
    if (o.cockpit) { g->appr_done = 1; g->vs.mode = 0; }
}

static void screen_requests(game_t *g) {
    screen_t *sc = &g->scr;
    if (sc->out_redraw) { sc->out_redraw = 0; g->ck.redraw = 3; }   /* V:4A8EF with it (screen.h out_redraw) */
    if (sc->out_panel) { sc->out_panel = 0; g->ck.panel = 3; }   /* V:4A8EE by the flow states 0x120E0 (the dispatch state), 0x1250C, 0x12730, 0x12BA0 */
    if (sc->out_90f_set) { sc->out_90f_set = 0; g->v4a90f = sc->out_90f; }   /* V:4A90F by the flow states (screen.h) */
    if (sc->out_place) {
        sc->out_place = 0; start_player(g, 0);
        approach_begin(g, sc->view_kind);
        if (sc->out_wind) { sc->out_wind = 0; snd_flight_start(g); }   /* 0x12C90: the state that starts a mission's approach replaces the mission tone pair by the wind or the engine pair (note 10 7.2) */
        if (sc->demo) replay_start(&g->rep);   /* the demo's flight: the reader at frame 0, the live control keys ignored (note 08 7.6). V:4A90F is what the flow states left: 0xFF after the boot (0x119AC; a first demo shows no map marks, note 06 [V]), 1 after a reset block (SHIFT-ESC, a flight: screen.c menu_build_in); the demo set-up stores nothing */
    }
    if (sc->out_sim) {   /* 0x12E4E: the simulation switch, the step counter cleared; the HUD stays off until the acting pass (0x12EDA, note 08 6.2) */
        sc->out_sim = 0;
        cockpit_mission_start(&g->ck);
        g->ck.hud_on = 0;
    }
    if (sc->out_hud) { sc->out_hud = 0; g->ck.hud_on = 1; sound_event(&g->snd, SND_BEEP_44, 0, 0); }   /* 0x12EDA: V:4A859 = 1 and the start sound 0x12EEC (0x37C44, volume 2, held back only by the cool-down; note 10 7.4) */
    if (sc->out_book) { sc->out_book = 0; book_clock(g); }   /* 10.7: the request 3 is taken, the time is booked before the debrief saves */
    if (g->has_flight && screen_in_outcome(sc) && screen_sim_hold(sc)) g->ac.f.v4a866 = 1;   /* 5.13 (milestone 7 [V: flow7-pause-outcome]): the pause byte is 1 from the freeze state through the typed outcome text and the final screens' hold */
    if (sc->out_end) { const uint8_t code = sc->out_end; sc->out_end = 0; g->ac.f.v4a866 = 0; g->ac.f.v4a968 = 0xFE; missions_resume_g(g, code); g->resume_skip = 1; }   /* the resume: the byte 0 and V:4A968 = 0xFE (the next flight pass consumes it) */   /* the outcome screen is over: the flight resumes (5.13) with the code 1, 2 or 4 */
    if (sc->out_esc) { sc->out_esc = 0; missions_esc(&g->ac.f); }
    if (sc->out_pulse) {   /* the gate pulse of the ESC restart (0x11AE2): one world update, the pause byte is 1: a script whose trigger holds fires (12.11); the reset then clears the code */
        sc->out_pulse = 0;
        if (g->has_flight && (g->mode == GAME_MODE_FLIGHT || g->mode == GAME_MODE_PAUSED) && (g->ac.f.v4a984 & A984_SIM_RUNNING)) missions_g(g);
    }   /* 5.11: the ESC request sets the pause byte V:4A866 to 1 (12.11) */
    if (sc->out_leave) { sc->out_leave = 0; leave_flight(g); replay_stop(&g->rep); g->ac.f.v4a903 = 0; }
    if (!(g->mode == GAME_MODE_PAUSED && sc->mode == GAME_MODE_FLIGHT)) g->mode = sc->mode;
}

/* The aircraft draw handler 0x26944 (note 07 11.5, 12.5): the descriptor routine of the models 3 to 10, 12, 14 and 15, which every drawn entity entry runs before its model: the entries of the direct list
 * (note 04a section 6: the walk runs the common drawer for them as for queue C) as well as those of queue C. A valid record that carries the crashed bit - a gun kill, marked after the world
 * update - gets the dying count 15 and the dying bit; when it is the designated target the notice 0x4016 is posted, the designation is dropped and the lock bits 0x4200 cleared.
 * A missile kill is converted by the record's own step (no notice). The cone handlers of the pod (models 2 and 13) and the parachute (model 11) are the same kind of descriptor routine. */
static void draw_handler_one(game_t *g, int id) {
    if (id <= 0 || id >= ENT_RECORDS) return;
    /* the descriptor decides the handler: the models 3 to 10, 12, 14 and 15 (note 07 11.1), not the class nibble */
    const unsigned model = (id == ENT_TRAINING_LEAD && g->tr.part) ? (unsigned)MODEL_F16 : g->ent.rec[id].template_id;
    /* the parachute handler 0x26A3E (note 07 11.5): +0x7C bit 0 is set in the phases 1 and up; the detail list by the phase is the walk's (lists.c cone_list) */
    if (model == 11 && entities_ac(&g->ent, id)->f.counter >= 1) entities_ac(&g->ent, id)->f.gear |= 1;
    if (!((model >= 3 && model <= 10) || model == 12 || model == 14 || model == 15)) return;
    aircraft_t *a = entities_ac(&g->ent, id);
    if (!(a->f.w0 & 0x0040)) {   /* 11.5: a record that is not valid: nothing, unless its +0x7A is not 5 and its counter is negative: the valid bit is set again (nothing is drawn) */
        if (g->ent.rec[id].engage != 5 && a->f.counter < 0) a->f.w0 |= 0x0040;
        return;
    }
    if (!(a->f.w0 & EW0_CRASHED)) return;
    a->f.counter = 15;
    a->f.w0 = (uint16_t)((a->f.w0 & ~EW0_CRASHED) | EW0_DYING);
    {   /* the handler runs before its entry's model and burst are drawn: the entry the walk shows carries the dying state of this frame */
        lists_entity_t *le = &world_lists(g->world)->ent[id];
        le->damage = entry_damage(&a->f);
        le->phase = a->f.counter;
    }
    if (g->ent.designated == id) {
        cockpit_post(&g->ck, &g->ac, 0x4016);
        g->ent.designated = -1;
        g->ac.f.v4ac08 &= ~0x4200;
        radar_lock_dropped(&g->rd);
    }
}

static void draw_handlers(game_t *g) {
    const lists_t *l = world_lists(g->world);
    for (unsigned i = 0; i < l->ndirect; i++) draw_handler_one(g, l->direct[i]);   /* the walk's order: the direct entities, then queue C */
    wp_qentry_t q[LISTS_KEPT];   /* the 69 entries queue C can hold */
    const int n = wp_queue_c(g, q, LISTS_KEPT);
    for (int i = 0; i < n; i++)
        if (q[i].flags & 0x10) draw_handler_one(g, q[i].id);
}

/* The gun routine's place in the frame (note 07 7.3, 12.5; the calls 0x11228, 0x11242, 0x1126E of the frame routine): in the map view the gun comes BEFORE the entity pass (it reads the record bytes and vertex
 * slots the previous frame's draw left); otherwise the draw scan 0x2AD18 decides (weapons_scan: the highest-index active slot, a distance test in an external view or for a chaff slot) and the gun comes
 * before the entity pass only when it answers 1, after it (the slots of this frame's draw) when it answers 0. [V: combat7-gunorder.txt, the whole pool at a 3-hit gun kill in the chase view: the pool held the
 * stopped tracers of the two earlier hits, the scan answered 1 in the rig (gunorder_check.py), the entity pass saw the crashed bit in the pass of the kill, posted 0x4016 and dropped the designation in
 * that pass; combat7-gunorder1.txt, a kill by the first tracer of an empty pool: the scan answered 0, the gun marked the kill after the entity pass, no post, the next pass converted it.] */
static int gun_precedes_entity_pass(const game_t *g) {
    if (g->vs.map) return 1;
    return weapons_scan(&g->wp, ext_view(g), g->scan_cam);
}

static void weapons_gun_pass(game_t *g) {
    g->wp.ext_view = (uint8_t)ext_view(g);
    g->wp.snd_off = (uint8_t)(g->vs.map != 0);   /* V:4A865 */
    const int paused = g->ac.f.v4a866 != 0;
    weapons_gun(&g->wp, pass_n(g), paused);   /* V:4A992 as the pass read it (note 07 7.3, "The frame counter at the call"): n_draw is the pre-increment value of an unpaused pass; in a counted pass n stands still */
}

/* The jolt 0x29A5E follows the display lists in the frame (before the gun and the scope walk, whose blip rows read its offset). */
static void jolt_pass(game_t *g) {
    const int external = g->hud_step > 14 || g->hud_step < -14;   /* |V:4AA3E| > 14: the full-width external views and the head views cancel the jolt */
    const int started = weapons_jolt_pass(&g->wp, &g->ac.f, external);
    g->jolt_call = g->wp.jolt_called;
    if (g->jolt_call) redraw_helper(g);   /* the end of the jolt routine, 0x29B0A (note 07 12.1 writer 5) */
    if (started) g->jolt = (uint8_t)started;
}

/* The radar list, the scope walk, the threat bits and the cues (note 07 12.1 to 12.5): the list builder is the tail of the entity pass and the scope draw (0x112D8) follows the gun, the entity pass
 * and the alert arbiter (0x1127C) of the same pass. So the walk sees a gun kill and a missile kill of its own pass, and the arbiter reads the threat bits of the PREVIOUS pass's walk: the threat bits,
 * the cues and the lock state reach it one pass late (12.1 "Where the pieces sit in the frame"). */
/* Whether the panel block (0x112CC: the panel, the scope draw, the HUD and the lamps) runs in this pass: in the cockpit view, or in an external view only while the panel counter V:4A8EE is non-zero; for a viewed
 * record of class 0x30 only while V:4A8EE is non-zero in the cockpit view too (note 07 12.1 "Where the counters count down"). Otherwise the external-view block 0x113F8 runs. */
static int panel_block_runs(const game_t *g) { return (!ext_view(g) && !seat_view(g)) || g->ck.panel > 0; }

static void radar_scope_pass(game_t *g) {
    radar_in_t rin;
    const int block = panel_block_runs(g);
    g->panel_block = (uint8_t)block;
    if (block && g->ck.panel > 0) g->ck.panel--;   /* the panel draw 0x35240 counts V:4A8EE down */
    cockpit_redraw_pass(&g->ck, block);   /* R (V:4A8EF) of this pass: the writers called the helper before; it counts down at the end of the panel block (0x113E8, note 07 12.1) */
    radar_inputs(g, &rin, pass_n(g));
    rin.no_panel = !block;
    rin.redraw = g->ck.redraw_now;
    radar_pass(&g->rd, &rin);
    g->ck.threat = g->rd.threat_prev;
}

/* The viewed-record-lost routine 0x14392 (writer 6 of the helper): it runs at 0x11434 in the common tail of the flight path AFTER the panel block, not in the world update. When V:4A994 names a record whose valid
 * bit is clear it resets V:4A994 and V:4A996 to 0 (the player is viewed again) and, in the cockpit view, the head code and the window offsets, then calls the helper (0x143D6 to 0x143EA). */
static void viewed_lost_g(game_t *g) {
    flight_state_t *pf = &g->ac.f;
    if (pf->v4a996 && !(entities_ac(&g->ent, entities_viewed(&g->ent))->f.w0 & 0x0040)) {
        pf->v4a994 = 0; pf->v4a996 = 0;
        request_lists(g);
        if (g->vs.level == 0) { g->vs.head = 0; g->hud_step = 0; redraw_helper(g); }
    }
}

/* The HUD target data of the frame (note 07 12.7, 12.8): the lock state, the prompt, the pipper, the range number and the message line, after the scope walk. */
static void radar_hud_g(game_t *g) {
    radar_in_t rin;
    fx_mat_t cam;
    radar_inputs(g, &rin, pass_n(g));
    rin.no_panel = !g->panel_block;
    for (int i = 0; i < 9; i++) cam.m[i / 3][i % 3] = g->sky_scene.matrix[i];
    rin.cam = &cam;
    if (g->ent.designated >= 0 && g->ent.designated < ENT_RECORDS) {   /* the designated target's projected point V:4A9EE/F0 as the entity pass leaves it (-1: not drawn) */
        const aircraft_t *va = entities_ac_c(&g->ent, entities_viewed(&g->ent)), *ta = entities_ac_c(&g->ent, g->ent.designated);
        fx_vec_t v, cv;
        int16_t sx, sy;
        int32_t dd[3];
        for (int k = 0; k < 3; k++) dd[k] = (int16_t)((int32_t)((uint32_t)ta->pos[k] - (uint32_t)va->pos[k]) >> 8);
        v.x = dd[0]; v.y = dd[1]; v.z = dd[2];
        fx_rotate(&cv, &cam, &v);
        if (fx_project(&sx, &sy, &cv)) { rin.tgt_x = sx; rin.tgt_y = sy; }
    }
    radar_hud_pass(&g->rd, &rin);
}

/* The per-pass sound routine 0x14AA0 (note 10 section 5), where the original's frame routine calls it: after the alert arbiter and before the scope walk (note 07 12.1). The gates are the map byte
 * V:4A865, the pause byte V:4A866 and the game gate V:4A84D (1 in every pass that gets here); the flow states' gate V:4A966 is the screen module's (the approach and the notice are silent: sound_screen_pass).
 * approach = 1: the first pass after the approach, whose view is still the approach's outside view (V:4A83D = 1, V:4A96A = 0, V:4A86D = 1: note 10 5.7 item 10).
 * The second call at 0x114C0 changes nothing (5.7 item 8) and is not modelled.
 * The gear wrapper's camera test is true for every camera in this pass (5.1 item 2, 5.7 item 12 [V: sound7-seat]: the seat plays the gear sound; the fixed camera never reaches the wrapper, V: sound7-fixedcam). */
static void sound_pass_g(game_t *g, int approach) {
    const flight_state_t *vf = &viewed_ac_c(g)->f;
    sound_in_t in;
    memset(&in, 0, sizeof in);
    in.v4a865 = (uint8_t)(g->vs.map != 0);
    in.v4a866 = g->ac.f.v4a866;
    in.v4a84d = 1;
    if (g->unpause_sound && !in.v4a866) {   /* the engine pair returns 4 blanks after the pause byte clears (note 10 7.3 [V: sound7-pause-frames]): the first flight pass after the key acts at blank 4 of its step */
        g->unpause_sound = 0;
        sound_run_to(&g->snd, 4);
    }
    in.view = (uint8_t)(approach ? 1 : g->vs.level);
    in.v4a86d = (uint8_t)(approach ? 1 : g->vs.aim_on != 0);
    in.ext_dir = (int8_t)(approach ? 0 : g->vs.ring);
    in.cam_dist = g->world ? (int16_t)world_lists(g->world)->v4abfa : 0;   /* V:4ABFA: the viewed record's distance word, 0x7FFF in the cockpit (the fixed camera, V:4A86D clear, reads it) */
    in.strain = g->strain.acc;
    in.pass_n = g->pass_n;   /* V:4A879 counts the passes from the mission initialisation and is never reset by a flight start: it has no fixed phase there (note 10 5.7 item 10), so the port's count of steps is as good as any */
    in.camera = 1;
    in.w2 = vf->w2; in.throttle = vf->throttle; in.speed = vf->speed; in.pitch_rate = vf->rate_p; in.roll_rate = vf->rate_r;
    in.cls = vf->cls; in.eject_nibble = (uint8_t)(vf->gear & 0x0F);
    in.notice = &g->ac.f.v4a968; in.hold = &g->ac.f.v4a84f; in.cues = &g->ac.f.v4ac0c;
    sound_pass(&g->snd, &in);
}

static void game_step_pass(game_t *g);

void game_step(game_t *g) {
    if (!g) return;
    g->pass_n++;
    game_step_pass(g);
    sound_step(&g->snd);   /* the library's blanks of this step (6, 6, 6, 7), after the pass's wrapper calls */
    sound_drain(&g->snd, g->audio, GAME_MAX_AUDIO_CMDS, &g->audio_count, &g->audio_target, &g->audio_set, &g->audio_ramp);
    if (g->snd.dropped) { g->audio_overflow = 1; g->snd.dropped = 0; }   /* sticky: the bound of scene.h was exceeded in this session */
}

static void game_step_pass(game_t *g) {
    g->flash = 0;
    g->jolt = 0;
    if (g->seat_wait) { g->seat_wait = 0; if (seat_view(g)) g->hud_step = 50; }   /* note 07 6.5 (V: followup-combat-eject-frames.txt): the window offset 800 and the window row 167 of the seat's view start in the pass after the spawn */
    screen_step(&g->scr);
    screen_requests(g);
    /* V:4A8EE counts down in the gate-1 passes that run the panel block (07 12.1: the 30 blanks of passes of 0x12C6E after the location-chosen store; free flight reaches its first pass with 0 [V: followup-combat-first-pass.txt]). The
     * choice of the states below and the one count per step are the port's conversion of those passes (C, note 08 3.8 item 4); the values at the first flight pass of every mode are the captured ones (07 12.1, tests/test_game.c). */
    if (g->ck.panel > 0 && gate1_screen(&g->scr)) g->ck.panel--;
    if ((int8_t)g->ck.redraw > 0 && gate1_screen(&g->scr)) g->ck.redraw--;   /* R counts down with it in those passes (07 12.1 [V]: training part 1's 3, 2, 1, 0) */
    if (g->has_flight) {   /* the approach cameras store V:4A90F = 0 while they run and 1 at their end (06 "What V:4A90F and V:4A86D hold") */
        const int ap = g->scr.state == SCR_APPROACH;
        if (!ap && g->in_approach && !g->scr.demo) g->v4a90f = 1;   /* 0x12D2A: the approach ends (the demo has none and keeps what it holds) */
        g->in_approach = (uint8_t)ap;
    }
    if (is_select(g->mode)) preview_step(g); else g->preview = 0;
    if (g->notice_pause && g->scr.state == SCR_FLIGHT && g->mode == GAME_MODE_FLIGHT) {   /* 5.14 (3): the first flight pass takes the byte P set in the notice and opens the pause screen */
        g->notice_pause = 0;
        g->ac.f.v4a866 = 0;
        g->mode = GAME_MODE_PAUSED; screen_pause(&g->scr, 1);
    }
    if (g->mode == GAME_MODE_PAUSED) {
        /* paused: nothing advances, tick included; the original's generator and arbiter keep running with the pause byte set (the alert shows its
         * steady text) and only n stays. The pause does not stop the state routines (17.17 rule 6): the resume states B, C1..C6 run on, nothing is
         * simulated, and the first flight-state pass after the pause takes the pause branch. A paused pass is as long as an unpaused one (the gate is 1, the scene is drawn): the resume keeps its seven
         * passes, one step each, paused or not (note 05 17.17 rule 7, "The resume states under a pause": 60 blanks paused against 63 unpaused). */
        if (g->has_flight && !g->freecam) {
            if (g->crash == CR_RESUME) mission_state(g);
            training_player_g(g);   /* 0x270A0 acts in a paused pass: RETURN re-positions in part 2 */
            /* 12.14 N4: the pause screen has the gate V:4A84D at 0 and the frame routine skips the whole world pass: no region tick, no stale-lock drop, no dispatcher, no script runs (the frozen passes with the
             * pause byte 1 to 5 and the gate 1 do run them: the scripts' count and the outcome screens) */
            /* the gun routine is not called on the pause screen either (gate 0: the short path of the frame routine, note 07 7.3) */
            paused_pass(g);
        }
        return;
    }
    g->tick++;
    if (g->mode != GAME_MODE_FLIGHT || !g->world) return;
    if (g->freecam) {
        view_switch(g);
        hold_second(g);
        world_freecam_step(g->world);   /* one fixed step per frame */
        lists_step(g);
        sky_frame(g);
        lists_walk_step(g);
    } else {
        int arbiter_due = 0, script_frozen = 0;
        if (g->has_flight) {
            const int resume_skip = g->resume_skip;   /* 5.13: the resume pass (blank 179) is not a flight pass; the first one consumes the 0xFE at blank 185, a step later */
            g->resume_skip = 0;
            if (resume_skip) return;
            if (screen_sim_hold(&g->scr)) {   /* the world is frozen: an ESC is being taken (V:4A84D = 0, no flight path runs), the outcome screens, or the approach and the notice (the cockpit is drawn from the start) */
                if (screen_in_outcome(&g->scr)) outcome_frozen_pass(g);
                if (g->scr.state == SCR_APPROACH || g->scr.state == SCR_NOTICE || g->scr.state == SCR_FADE_IN || g->scr.state == SCR_BRIEF || g->scr.state == SCR_TRAIN_DELAY || g->scr.state == SCR_TRAIN_BRIEF) { approach_pass(g); apply_camera(g); lists_step(g); sky_frame(g); lists_walk_step(g); }
                {   /* the sound routine of the last steps of the approach state (screen_sound_pass): the gate V:4A966 is open from the arrival and the routine plays the pending notice 0xFE; the typed notice has the gate closed */
                    const int k = screen_sound_pass(&g->scr);
                    if (k) sound_pass_g(g, k == 1);
                    if (k == 1) { sound_stop_voice(&g->snd, 3); g->ac.f.v4a968 = 0xFE; }   /* the approach's end switches to the cockpit view in the same frame, after the sound routine of the pass that latched the external pair: the view setter 0x1423E stops voice 3 and posts 0xFE (10 5.7 item 10, 7.3) */
                }
                return;
            }
            if (g->crash == CR_HOLD) {
                /* the flip routine's hold: the frame routine does not run at all (section 14): no generator, no step, no arbiter, no n, no new lists;
                 * the display keeps the last frame (the arbiter's line frozen, the crash flag set so the external strip is not drawn). */
                /* No frame routine runs during the hold, so no tracer advances in it: note 05 section 14 stands and note 07 7.3 says the same (the gun routine is part of the frame routine). */
                if (g->hold_steps < CRASH_HOLD_STEPS) { g->hold_steps++; return; }
                /* pass A: the key poll delivers the queued events first (17.17 rule 6) */
                for (int i = 0; i < g->queued; i++) flight_event(g, &g->queue[i]);
                g->queued = 0;
                screen_poll(&g->scr);    /* the key poll of pass A delivers an ESC at once */
                if (screen_sim_hold(&g->scr)) return;   /* ESC taken in the hold: the restart pass is not run, the flight is left when the screen's fade-out is over (note 08 5.11) */
                if (g->mode == GAME_MODE_PAUSED) {   /* a pause among them: the restart still completes, nothing is simulated, n stands still */
                    if (!mission_state(g)) return;   /* the fifth crash: the game-over branch runs in A regardless of the pause (enter_game_over) */
                    paused_pass(g);
                    return;
                }
            }
            /* the demo (note 08 7.3, 7.4): the events of the frame run through the key handlers before the pass, as the poll runs them before the dispatcher; the reader's
             * key byte is the player's control byte. Only a pass that simulates the player reads the stream (a crash restart's passes do not). */
            replay_frame_t rf;
            int rep_more = 1;
            memset(&rf, 0, sizeof rf);
            if (g->rep.active && g->crash == CR_NONE && (g->ac.f.v4a984 & A984_SIM_RUNNING)) {
                rep_more = replay_step(&g->rep, &rf);
                for (int i = 0; i < rf.n_events; i++) { const game_event_t ev = replay_event_in_view(&rf, i, ext_view(g)); flight_event(g, &ev); }   /* the view is read at the event's turn (7.3) */
            }
            /* one pass of the main loop: the mission-state routine first (note 05 section 17.1: before the entity update, so every transition
             * is seen by the step of the same pass), then cockpit.h's order: the generator, the world update (the dispatcher first), then the display lists, the jolt, the gun and the entity pass, the arbiter and the scope walk; n advances in cockpit_end_pass right after the update (every reader below takes the value the pass saw). */
            if (!mission_state(g)) return;   /* the fifth crash's pass A: the record is rewritten and nothing else runs (17.17 (xiii)): no step, no lists, no n */
            view_switch(g);                  /* 0x141E8, right after the mission-state routine (note 06 item 7): the keys of the previous pass act now */
            hold_second(g);                  /* 0x297D6 of the same drawn frame: after the recentre of the hold byte at 0 */
            g->ac.f.v4a83d = (uint8_t)(g->vs.level != 0);   /* the entity update below sees the selector after the switch */
            cockpit_generator(&g->ck);
            flight_inputs_t in = g->fin;
            in.pointer_dy = (int16_t)g->pointer_dy;
            in.keys = (uint8_t)(g->keys_held | g->keys_axes);
            if (g->rep.active) { in.keys = rf.valid ? rf.key : 0; g->ac.f.v4a903 = 3; }   /* replay state 3 (7.4): the throttle follows the two key bits only (no preset block) */
            g->ac.f.v4a992 = g->ck.n;   /* V:4A992 for the damage parity and the world update's schedules */
            carrier_slots_sync(g);
            flight_set_env(&g->fenv);
            /* The world update 0x26AF8 in its order (note 07 4.8): the dispatcher (the accounting routine 0x11748, before the gate test) first, the head items 1 to 5 (life words, region tick, W2 bit 0, the damage cool-downs, the stale-lock
             * drop), then the record loop whose first record is the player: its pre-steps (the cue and the instructor, the range toward the designated target 0x2835A), its step, the records 1 to 15, the tail */
            entities_g_begin(g);
            const int des_before = g->ent.designated;
            entities_head_a(&g->ent);
            weapons_world_start(&g->wp, g->ac.f.v4a866 != 0);   /* the flare and chaff cool-downs (note 07 10.1): not in a script's count */
            entities_head_b(&g->ent);
            strain_pass(g, 0);   /* the turn-rate cue of the world update, before the player's step */
            training_player_g(g);   /* 0x270A0, in the player's pre-steps */
            entities_prestep(&g->ent);   /* the range word toward the designated target (note 07 12.6) */
            g->ac.f.ev_liftoff = g->ac.f.ev_touchdown = g->ac.f.ev_crash = 0;
            flight_step(&g->ac, &in);
            entities_g_end(g);   /* the records 1 to 15 after the player's step (record 4 is the training instructor in the trainings, note 08 8.3) */
            sound_flight_events(g);
            weapons_g(g);
            if (des_before >= 0 && g->ent.designated < 0) radar_lock_dropped(&g->rd);   /* the stale-lock drop 0x26F28 (note 07 4.7, 12.5) */
            g->jolt |= g->ac.f.ev_jolt;
            g->ac.f.ev_jolt = 0;
            log_after_step(g);
            g->eject_key = g->fin.eject;
            g->fin.gear_toggle = g->fin.hook_toggle = g->fin.brake_toggle = g->fin.eject = 0;
            g->fin.preset = 0;
            g->fin.pointer_set = 0; g->fin.pointer_value = 0; g->pointer_dy = 0;
            script_frozen = g->ac.f.v4a866 != 0;   /* a script's count (12.2): n stands still, as in a pause */
            arbiter_due = 1;   /* the alert arbiter 0x13D4C runs after the entity pass (07 12.1): below, with the 0x4016 post of the draw handler in the same pass */
            cockpit_end_pass(&g->ck, script_frozen);   /* n advances here in the port; every reader below takes the value the pass saw (n_draw, or n in a counted pass), so the order against the arbiter and the walk is not observable */
            if (g->hold_pending) { g->hold_pending = 0; g->crash = CR_HOLD; g->hold_steps = 0; }   /* the hold begins after the hand-over pass */
            if (rf.valid && !rep_more) {   /* 7.4 rule 5: the step that read the last frame has completed; the leave request is the ESC request, destination: restart the demo (7.7) */
                game_event_t esc; esc.kind = GAME_EV_BUTTON; esc.code = GAME_BTN_ESC; esc.value = 1;
                screen_key(&g->scr, &esc);
            }
        } else { view_switch(g); hold_second(g); }
        apply_camera(g);
        if (g->has_flight) entities_update_view_cell(&g->ent, ext_view(g), g->cam_cell_x, g->cam_cell_z);   /* the camera set-up of the frame, every pass: the next pass's neighbour test reads it (note 07 6.4; note 04a, the viewed cell is the camera's in an external view, the record's in the cockpit) */
        lists_step(g);
        sky_frame(g);
        /* Every pass that gets here is a full gate-1 pass of the frame routine (note 07 7.3, note 05 14 and 17.17): the hand-over pass, the seven restart passes A, B, C1 to C5 and the passes of an
         * external-view crash all run the jolt, the gun, the entity pass with its draw handlers, the arbiter and the walk. Only the crash hold itself (the 40 x 2-blank flip loop) runs no frame
         * routine, and it returned above. */
        const int flying = g->has_flight;
        const int map = flying && map_view(g);
        const int high = flying && world_altitude_gate(g->world);   /* 0x111E0: V:4AB1E at or below -0x8000000 jumps to 0x11274 over the three walks, the gun and the entity pass (07 7.3); the jolt before it and the arbiter, the sound and the HUD after it run */
        int gun_first = 0;
        g->jolt_call = 0;
        if (flying) jolt_pass(g);
        if (flying && !high) { gun_first = gun_precedes_entity_pass(g); if (gun_first) weapons_gun_pass(g); }
        g->gun_first = (uint8_t)gun_first;
        if (map) world_lists_walk_map(g->world);   /* the walkers of queues A and B and the [90] pass run before the test of V:4A865; the map branch then calls the gun alone and jumps over the entity pass 0x1FBCC (07 7.3, 0x11228): no direct or queue C entry, no part-id or slot store, no draw handlers; queue C and the slots stay as the last frame that built them left them */
        else lists_walk_step(g);   /* the walkers and the entity pass (an altitude-gated frame walks nothing: lists_frame_walk) */
        if (flying && !high) {
            if (!map) draw_handlers(g);   /* in the entity pass, before the entry's draw: the dying burst starts in the frame of the kill (11.5) */
            if (!gun_first) weapons_gun_pass(g);
        }
        if (arbiter_due) cockpit_arbiter(&g->ck, &g->ac, script_frozen);   /* 0x13D4C after the gun and the entity pass: the 0x4016 post and the lock-bit clear of the handler are seen in the same pass (12.1) */
        if (flying) sound_pass_g(g, 0);   /* 0x14AA0 */
        if (flying) radar_scope_pass(g);
        if (flying) radar_hud_g(g);
        if (flying) { viewed_lost_g(g); map_marks_pass(g, map); periodic_redraw(g); }
    }
    if (g->snd_tick) { g->snd_tick = 0; sound_second(&g->snd); }   /* 0x29882 inside the limiter 0x297D6 at 0x11446: the last of the pass (note 10 5.1 item 7) */
}

/* The weapon slice's part of the scene (note 07 sections 7 to 13): the stores, the effects (the tracer pool, the effect objects, the dying bursts), the combat sounds, the jolt's row and the
 * trail ring. */
static void add_effect(game_scene_t *out, game_fx_kind_t kind, const int32_t pos[3], unsigned age, int rec, int owner, unsigned model, int phase) {
    if (out->effect_count >= GAME_MAX_EFFECTS) return;
    game_effect_t *e = &out->effects[out->effect_count++];
    e->kind = kind; e->pos.x = pos[0]; e->pos.y = pos[1]; e->pos.z = pos[2]; e->age = (uint16_t)age; e->rec = (uint8_t)rec; e->owner = (uint8_t)owner; e->model = (uint16_t)model; e->phase = (int16_t)phase;
}
static void weapons_fill_scene(const game_t *g, game_scene_t *out) {
    const entities_t *e = &g->ent;
    const weapons_t *w = &g->wp;
    out->hud.cp_stock = e->rec[0].stock;
    out->hud.cp_rounds = e->rec[0].rounds;
    out->hud.cp_ecm = g->ac.f.v4a96d;
    out->hud.flares = w->flares; out->hud.chaff = w->chaff;
    out->jolt_row = w->jolt_row;
    out->gun_first = g->gun_first;
    for (int k = 0; k < ENT_OBJECTS; k++) {   /* the effect objects (11.2, 11.3): never freed */
        const secondary_t *o = &e->obj[k];
        if (!(o->w0 & EW0_VALID)) continue;
        add_effect(out, GAME_FX_EXPLOSION, o->pos, (unsigned)(0x7F - o->life < 0 ? 0 : 0x7F - o->life), k, 0xFF, o->model, o->life);
    }
    for (int k = 0; k < WP_SLOTS; k += 2) {   /* the tracer pool (7.2): tracers, flares, chaff */
        const wp_tracer_t *s = &w->slot[k];
        if (!(s->flags & WPF_ACTIVE)) continue;
        const int32_t p[3] = { (int32_t)(((uint32_t)s->cell_x << 22) + (uint32_t)s->pos[0]), s->pos[1], (int32_t)(((uint32_t)s->cell_z << 22) + (uint32_t)s->pos[2]) };
        add_effect(out, (s->flags & WPF_FLARE) ? GAME_FX_FLARE : (s->flags & WPF_CHAFF) ? GAME_FX_CHAFF : GAME_FX_SHELL, p, 0, k, 0, s->flags, s->life);
    }
    for (int i = 0; i < ENT_RECORDS; i++) {   /* the dying bursts (11.4): the dying bit with the counter 0 to 15 */
        const flight_state_t *f = &entities_ac_c(e, i)->f;
        if (!(f->w0 & EW0_VALID) || !(f->w0 & EW0_DYING) || f->counter < 0 || f->counter > 15) continue;
        const int kind = f->cls == 0x15 ? 3 : (f->b4 & 0x02) ? 2 : 1;
        add_effect(out, GAME_FX_BURST, entities_ac_c(e, i)->pos, (unsigned)(15 - f->counter), i, 0xFF, (unsigned)kind, f->counter);
    }
    if (e->trail < ENT_RECORDS) {
        out->trail.count = w->ring_count; out->trail.index = w->ring_idx; out->trail.length = w->trail_len[e->trail];
        out->trail.ooc = (entities_ac_c(e, e->trail)->f.b20 & 0x02) != 0;
        memcpy(out->trail.ring, w->ring, sizeof out->trail.ring);
        for (int k = 0; k < 3; k++) { out->trail.vel[k] = entities_ac_c(e, e->trail)->f.vel[k]; out->trail.pos[k] = entities_ac_c(e, e->trail)->pos[k]; }
        out->trail.speed = entities_ac_c(e, e->trail)->f.speed;
    }
}

/* The marks of the map view: the list the map routine's entity loop produced in the last pass (mapmarks.h), the screen point, the shape and the pen of each. */
static void marks_fill_scene(const game_t *g, game_scene_t *out) {
    game_hud_t *h = &out->hud;
    h->mark_count = g->mm.count;
    for (int i = 0; i < g->mm.count; i++) {
        game_mark_t *m = &h->marks[i];
        m->x = g->mm.mark[i].x; m->y = g->mm.mark[i].y; m->rec = g->mm.mark[i].rec; m->shape = g->mm.mark[i].shape; m->pen = g->mm.mark[i].pen;
    }
}

void game_scene(const game_t *g, game_scene_t *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!g) return;
    out->mode = g->mode;
    out->tick = g->tick;
    out->splash_asset = 0;   /* index 0 of pix/index.txt; the index is validated at create */
    out->flash_frames = (uint8_t)g->flash;
    out->jolt = g->jolt;
    out->log_dirty = g->log.save_req;
    if (g->mode == GAME_MODE_GAMEOVER) out->text_id = g->over_text;
    if (g->preview && is_select(g->mode) && g->world) {   /* the map overview behind the selection text */
        world_fill_scene_view(g->world, out, current_view(g));
        out->sky = g->sky_scene;
        out->camera.view = current_view(g);
        out->camera.ring = 4;
        out->camera.hud_step = 50;
    } else if ((g->mode == GAME_MODE_FLIGHT || g->mode == GAME_MODE_PAUSED) && g->world) {
        if (g->mode == GAME_MODE_PAUSED) out->flight_blank = 1;   /* the pause keeps V:4A84D at 0: nothing of the world is redrawn, the last picture stays on screen dimmed to level 10 (note 06 item 8, note 08 5.14). The pause text is the screen module's (text id 0x26, note 08 5.14) */
        if (g->scr.state == SCR_ESC_FADE && !g->freecam) out->flight_blank = 1;   /* ESC taken: V:4A84D = 0 from k = 3, the world is not redrawn while the picture fades out (rule 9) */
        world_fill_scene_view(g->world, out, current_view(g));
        out->sky = g->sky_scene;
        out->camera.view = current_view(g);
        out->camera.head = (uint8_t)(ext_view(g) ? 0 : g->vs.head);
        out->camera.hud_step = (int8_t)g->hud_step;
        out->camera.ring = (uint8_t)(ext_view(g) ? g->vs.ring : 4);
        out->camera.mode = (uint8_t)g->vs.mode;
        out->camera.viewed = (uint8_t)viewed_index(g);
        out->camera.fixed = (uint8_t)(g->vs.fixed && ext_view(g));
        out->camera.trail = g->ent.trail;
        if (seat_view(g) && !g->seat_wait) out->camera.hud_step = 50;   /* the seat's cockpit view: HUD step offset 800 (note 07 6.4), from the pass after the spawn (6.5) */
        /* the HUD belongs to the flight module; only the free camera derives one from the camera */
        if (!g->freecam) {
            memset(&out->hud, 0, sizeof out->hud);
            /* the radar list (built by 0x1FF24) and the target symbols' fields are filled by radar_fill_hud below */
            if (g->has_flight) {
                const aircraft_t *va = viewed_ac_c(g);   /* the external strip reads the VIEWED record (V:4A996): in part 1 the lead (note 08 8.7, 8.3) */
                flight_hud(va, &out->hud);
                cockpit_fill_hud(&g->ck, &g->ac, &out->hud);
                radar_fill_hud(&g->rd, &g->ent, &out->hud);
                out->hud.panel_block = g->panel_block;
                out->hud.cp_eject_armed = (uint8_t)g->eject_armed;
                out->hud.cp_eject_key = (uint8_t)g->eject_key;
                if (screen_sim_hold(&g->scr)) out->hud.cp_msg_ext = 0;   /* 8.7: the external message line (0x370E6, 0x37130) needs V:4A84B, set by the acting pass of 0x12E4E: not before the flight starts or while an end screen holds it */
                if (va != &g->ac) { out->hud.cp_heading80 = va->f.heading; out->hud.cp_speed_w = va->f.speed; out->hud.cp_y = va->pos[1]; }
            }
        }
    }
    if ((g->mode == GAME_MODE_FLIGHT || g->mode == GAME_MODE_PAUSED) && g->has_flight && !g->freecam && g->world) { weapons_fill_scene(g, out); marks_fill_scene(g, out); }
    out->audio_count = (uint16_t)g->audio_count;
    out->audio_overflow = (uint8_t)g->audio_overflow;
    memcpy(out->audio, g->audio, (size_t)g->audio_count * sizeof g->audio[0]);
    out->audio_master_target = g->audio_target; out->audio_master_set = g->audio_set; out->audio_master_ramp = g->audio_ramp;
    if (is_select(g->mode)) out->map_mark_count = (uint8_t)screen_map_marks(&g->scr, out->map_marks);   /* the numbers of the starts: the map code's, not text records (5.6) */
    screen_fill(&g->scr, out);   /* the text records, the palette regime and the fade target of the screen (after the flight block: the fade target of a flight is sky.fade_target) */
}
