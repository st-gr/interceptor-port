/* src/core/game.h - the only entry points a frontend may use. */
#ifndef INTERCEPTOR_GAME_H
#define INTERCEPTOR_GAME_H
#include <stddef.h>
#include <stdint.h>
#include "scene.h"
#include "aircraft.h"

typedef struct game game_t;

/* Asset loader supplied by the frontend. read() returns 0 on success and
 * fills *out and *len with a buffer the core later hands back to release(). */
typedef struct {
    int  (*read)(void *user, const char *path, uint8_t **out, size_t *len);
    void (*release)(void *user, uint8_t *buf);
    void *user;
} game_loader_t;

typedef enum { GAME_EV_AXIS = 0, GAME_EV_BUTTON, GAME_EV_KEY } game_event_kind_t;

/* Axis codes. GAME_AXIS_THROTTLE is an absolute lever position -32768 (idle) .. 32767 (full), analogue input only
 * (the function keys are GAME_BTN_FKEY); the discrete steps are GAME_BTN_THROTTLE_UP/DOWN. The mapping of both
 * to the original's throttle unit belongs to the flight model (note 05, not yet written). */
enum { GAME_AXIS_STICK_X = 0, GAME_AXIS_STICK_Y, GAME_AXIS_THROTTLE, GAME_AXIS_RUDDER,
       GAME_AXIS_POINTER_DY };
/* GAME_AXIS_POINTER_DY: a relative event, the mouse's vertical movement in pixels (SDL's yrel: positive toward the user, which lowers the throttle);
 * the core sums the events between two steps (the original's mouse counter, note 05 17.4). The throttle axis above maps to the pointer height. */
/* Button codes (value 1 = pressed, 0 = released) except where noted.
 * View and flight events (docs/re/01a-reference-captures.md key table, note 04 "View kinds and keys"):
 *   KEYPAD          one keypad press event whose value is the key: 0..9 = KP0..KP9, 10 = KPDOT, 11 = KPENTER. The core routes it by its own current view, as the
 *                   original's single keypad handler dispatches on the view selector (V:4A83D). Cockpit: KP8 and KP9 head 0 (ahead), KP4 and KP6 step the head, KP2 rear
 *                   (6), KP7 and KP1 the views 12 and 13, KP5 locks the present head view, KP3 and KPDOT store the external direction (3, 9) only; KPENTER requests the
 *                   external view (direction 4, behind, level -1). External (also the map): KP8 9 6 3 2 1 4 7 5 and KPDOT pick the direction 0..9; KPENTER requests the cockpit.
 *                   KPENTER and M only SET a request bit: the view changes in the next unpaused flight pass, once however many arrive (note 06 "View kinds, settled" item 7).
 *   KEYPAD_UP       the release of a keypad key (value = the key): the release of KP2, KP4, KP6, KP7 and KP1 starts the head-hold countdown (a head view is momentary:
 *                   it ends 4 to 5 s after the release; a frontend that sends no release events keeps the head view until another key, as KP5 does).
 *   TOWER           'M': value 1, the map view (note 06 item 2): in the cockpit it requests the map view, in an external view it shows the map at once, in the map it returns to the cockpit.
 *   LBRACKET, RBRACKET   '[' (farther) and ']' (closer), value 1 pressed, 0 released: by view the cockpit zoom word V:4AAFA (0x80, 0x40, 0x20), the external distance level
 *                   (-1, 1, 2, 3, 4) or the map altitude. The core processes the view keys, the zoom keys and the axes while the game is paused too (the key routine runs in every pass).
 *   BRAKE           air brake / wheel brake (the flight module decides; value 1/0 held).
 *   PAUSE           value 1 toggles flight <-> paused (game_step does nothing while paused).
 *   THROTTLE_UP/DOWN  F10/F9-style discrete steps (a press event; the size is fixed by note 05).
 *   FKEY            one function-key press event whose value is the key number 1..10 (F1..F10); the core stores the preset 12 n (F10 121; F1 again while the
 *                   stored preset is 12 stores -1, idle) and sets P = Q = min(8 x preset, 960) (note 05 section 2 and 17.4). No release event.
 *   HUD_TOGGLE      'H': value 1 toggles the HUD switch V:4A859 (initially on); the core publishes it as scene.hud.cp_hud_on. */
enum { GAME_BTN_FIRE = 0, GAME_BTN_SELECT_WEAPON, GAME_BTN_GEAR, GAME_BTN_HOOK,
       GAME_BTN_FLARE, GAME_BTN_CHAFF, GAME_BTN_VIEW_NEXT, GAME_BTN_MENU,
       GAME_BTN_ESC, GAME_BTN_RETURN, GAME_BTN_ANY,
       GAME_BTN_KEYPAD, GAME_BTN_TOWER,
       GAME_BTN_BRAKE, GAME_BTN_PAUSE, GAME_BTN_THROTTLE_UP, GAME_BTN_THROTTLE_DOWN,
       GAME_BTN_HUD_TOGGLE, GAME_BTN_FKEY,
       GAME_BTN_KEYPAD_UP, GAME_BTN_LBRACKET, GAME_BTN_RBRACKET,
       GAME_BTN_SHIFT_ESC, GAME_BTN_SMOKE };
/* Milestone 5 (note 07 6.3, 6.4). Their codes are far from the ones above so that the weapon slice's additions do not collide.
 *   GAME_BTN_EJECT          SHIFT and E (a press, not paused): toggles the eject lamp V:4A863 and ORs 0x0A into the player's gear nibble: the ejection cycle (10, 9, 8, 7, 1 to 6, the seat at 3),
 *                           a press while the nibble is not 0 is ignored (the key handler skips its first chain then, note 07 6.4a); the parachute comes from the rescue key.
 *   GAME_BTN_FIXED_CAMERA   SHIFT and KP-ENTER (a press): the fixed camera at the start location's point, looking at the viewed record; a ring key returns to the ring camera.
 *   GAME_BTN_RESCUE         SHIFT and F (a press) in mission F4 (game mode 6), the rescue operation: the parachute when one of the records 1 to 3 is free. */
enum { GAME_BTN_EJECT = 100, GAME_BTN_FIXED_CAMERA = 101, GAME_BTN_RESCUE = 102 };
/* Milestone 5, the weapon slice (note 07 sections 7, 9, 12). Value 1 is a press, 0 a release only for FIRE.
 *   GAME_BTN_FIRE           SPACE: the press holds the trigger (gun) or asks for a missile launch (Sidewinder, AMRAAM) when the gear byte's low nibble is 0; the release lets the trigger go.
 *   GAME_BTN_SELECT_WEAPON  RETURN: the weapon cycle AMRAAM, Sidewinder, gun, none (not in the trainings, whose RETURN is the sequencer's).
 *   GAME_BTN_FLARE          F: a flare (15 releases from 16; the posts 0x4026 / 0x4027).      GAME_BTN_CHAFF  C: chaff (0x4028 / 0x4029).
 *   GAME_BTN_ECM            J: the ECM switch.
 *   GAME_BTN_RADAR_RANGE    R: the scope range, the low nibble of the weapon selector (13, 11, 9).
 *   GAME_BTN_TARGET         T: designate the next target of the radar list (a press).
 *   GAME_BTN_MESSAGE_PAGE   Y: the next page of the message line's target data. */
enum { GAME_BTN_ECM = 103, GAME_BTN_RADAR_RANGE = 104, GAME_BTN_TARGET = 105, GAME_BTN_MESSAGE_PAGE = 106 };
/* PORT FEATURE (the only one of milestone 5): GAME_BTN_REPLENISH, the W key (a press). The original has no key that rearms: the body at 0x1ECAE of its key handler calls the rearm routine
 * 0x1F11E and nothing reaches it (note 08 11.6, note 07 9.7, dead code); W is one of the keys with no handler (note 08 11.2 census) and is free on every keyboard layout. The press runs that body: the fuel to full
 * (0x61A800), the missile stock 0x24, 500 rounds, flares and chaff 16 each, the three weapon records cleared, and the message entry 5 ("refuelled and rearmed", 0x4005) posted once. It acts where the
 * original's control keys act (note 08 11.1 phase F: a running flight, also in a pause and in the ESC countdown) and nowhere else: not in the demo (the live-key rule of the replay), not in
 * training part 1 (which drops the flight controls), not in the approach, the notice and the outcome screens, not with the ejection nibble set, not on the screens. The flight log's counters are not touched.
 * It reuses the landing's rearm body unchanged (flight_rearm: the post and the latch unconditionally, no repeat guard). The dead key body 0x1ECA4 (a bit 6 clear of the word at A0 + D4) is a separate body from the rearm
 * call 0x1ECAE and is never reached (note 07 9.7): the port runs the 0x1ECAE body only and models no bit 6 clear (weapons_rearm clears the weapon records 1 to 3). */
enum { GAME_BTN_REPLENISH = 107 };
/* GAME_BTN_SMOKE (value 1, a press): the S key of the training flight school (part 2 toggles the lead aircraft's smoke trail, note 08 8.6); no effect elsewhere.
 * GAME_BTN_SHIFT_ESC (value 1, a press): SHIFT then ESC, back to the menu from any screen with a mode (note 08 5.11). The frontend sends it when ESC is pressed while the one-event shift flag is set (a shift press
 * that no other key event has followed, note 08 11.1 quirk (b); the key repeat of a held shift does not set it), in place of GAME_BTN_ESC. The screens outside the flight take every key press as GAME_EV_KEY (ASCII: digits '0'..'9', letters; 13 RETURN, 27 ESC are also
 * accepted), the buttons RETURN, ESC, SHIFT_ESC, ANY, FKEY and KEYPAD (KP1..KP8 act as the digits 1..8, KPENTER skips the approach); releases and repeats are not sent
 * (note 08 5.8). */
/* Values of GAME_BTN_KEYPAD */
enum { GAME_KP_DOT = 10, GAME_KP_ENTER = 11 };
/* GAME_EV_KEY: value is an ASCII character for text entry */
/* GAME_KEY_SHIFT2: the key SHIFT then 2, sent as a GAME_EV_KEY by the frontend only on the flight-log screen (scene.mode == GAME_MODE_LOG), where it is RESET (note 08 10.6);
 * on every other screen the frontend sends '2'. */
#define GAME_KEY_SHIFT2 '@'

typedef struct {
    game_event_kind_t kind;
    int32_t code;
    int32_t value;            /* axes: -32768..32767, buttons: 0/1, keys: ASCII */
} game_event_t;

/* Returns NULL on failure and writes a message into err (if errlen > 0). */
game_t *game_create(const game_loader_t *loader, char *err, size_t errlen);
void    game_destroy(game_t *g);
void    game_input(game_t *g, const game_event_t *ev);
void    game_step(game_t *g);                       /* exactly one original tick */
/* Debug: while on, the flight mode runs the milestone-2 free camera (world_freecam_step) instead of
 * the aircraft. The scene still reports the view kind, head and ring that the view keys select (only
 * the camera position and attitude stay the free camera's). Set it before the splash is left (it does not move the camera by itself). */
void    game_set_debug_freecam(game_t *g, int on);
/* The flight start (the splash's RETURN and the crash restart): 0 (default) the carrier deck of free flight, 1 the runway start block 0
 * (note 05 section 17.14). Set it before the splash is left; the menu of milestone 4 will choose. */
void    game_set_start(game_t *g, int runway);
/* The result of the log-disk check (note 08 5.3), set by the frontend before the credits are left: 0 (default) the log is usable and no prompt is shown, 1 no disk, 2 not
 * initialised, 3 not write enabled (the prompt of that variant, mode GAME_MODE_LOGDISK, RETURN goes on). */
void    game_set_logdisk_result(game_t *g, int r);
/* Debug and tools: the flight starts at once, as the splash's RETURN did before milestone 4 (no screens, no notice, no wait; the HUD on). location 0..2 an airfield
 * runway (note 08 5.6 keys 1 to 3), 3 the carrier deck (key 4); aircraft 0 the F/A-18, 1 the F-16. The normal start is the key sequence of the screens: any key on the
 * credits, RETURN on the log-disk prompt, '2' in the menu, the location digit '1'..'4', the aircraft digit '1'/'2' (not for the carrier). */
void    game_debug_start_flight(game_t *g, int location, int aircraft);
/* The timer device's second changed (V:4ABAA): the frontend calls it once per second of wall time (in a counted run, once per FLIGHT_STEP_HZ steps). The only thing that counts it is the
 * head-hold byte of the cockpit head views (note 06 "Head views": a head view lasts 4 to 5 seconds after the key release). The phase of the system second against the key release is arbitrary (the note settles it: the system
 * clock has no relation to the mission), so the length of one hold is anywhere in that range. */
void    game_clock_second(game_t *g);
/* Seeds the placement's generator (note 07 2.4): the original reads the system timer at every mission start; the port draws one 16-bit word per fresh start from this seeded generator.
 * The default seed is 1 (the golden runs use it explicitly). */
void    game_random_seed(game_t *g, uint32_t seed);

/* The crash flow (game.c, note 05 sections 11, 14, 15, 17.3, 17.17). game_crash_state returns one of GAME_CRASH_*: NONE (flying, including the first
 * simulated step after a restart), HOLD (cockpit view: from the end of the hand-over pass the flip routine's hold, 13 frozen game_steps for the
 * 80 blanks; *hold_blanks_left, when given, is the blanks still to run), DYING (external view: no hold, the dying state runs until W0 bit 0x0400
 * clears, then the restart pass), RESUME (the restart pass A, B and C1..C5: seven passes that run the cockpit state but simulate nothing; the eighth
 * pass, C6, is the first simulated step). Buttons that arrive during HOLD are queued and applied at the start of pass A. game_crash_count is the
 * number of hand-overs; game_crash_allowance is V:4A94F (5 in free flight, decremented in pass A; at 0 the mode becomes GAME_MODE_GAMEOVER with the
 * text id in scene.text_id, the text types out, then the menu). ESC (GAME_BTN_ESC) in flight starts the restart of note 08 5.11 (the fade-out, the reset after 7 steps, the echo line,
 * the black passes, the fade-in, the location selection). */
enum { GAME_CRASH_NONE = 0, GAME_CRASH_RESUME, GAME_CRASH_HOLD, GAME_CRASH_DYING };
int     game_crash_state(const game_t *g, int *hold_blanks_left);
int     game_crash_count(const game_t *g);
int     game_crash_allowance(const game_t *g);
int     game_flash(const game_t *g);                /* 1 after a step in which the hold ended (scene.flash_frames); main ORs it over the steps of a frame */
/* The demo replay (note 08 section 7): the number of stream frames consumed so far in the running demo flight (0 before the first simulated step, i after the step that
 * applied frame i - 1), or -1 when no demo flight is running. A tool/test accessor; the frontend needs nothing of it. */
int     game_demo_step(const game_t *g);
/* Tests and tools only: the player aircraft the flight mode steps (NULL without flight data). Writing it is how tests stage a state. */
aircraft_t *game_debug_aircraft(game_t *g);
/* Tests only: the training module's state (training.h; NULL without flight data). tr.part is 0 outside the trainings. */
struct trainer;
struct trainer *game_debug_trainer(game_t *g);
/* Tests only: the entity records and secondary objects (entities.h; NULL without flight data). */
struct entities;
struct entities *game_debug_entities(game_t *g);
/* Tests only: the weapon stores (weapons.h; NULL without flight data). */
struct weapons;
struct weapons *game_debug_weapons(game_t *g);
/* Tests only: the view state's stored heading V:4A9E2 (world_viewstate_t.heading). */
int game_debug_ring_heading(const game_t *g);
/* Tests only: the look-at byte V:4A86C (signed: 0xFF is -1). */
int game_debug_look(const game_t *g);
/* Tests only: the radar state (a radar_t *; radar.h; NULL without flight data). */
void *game_debug_radar(game_t *g);
void *game_debug_sound(game_t *g);   /* the sound module (sound.h): tests */
/* The frontend reports which of the 37 sound objects it could build (present[o] = 0: the sample file is missing or short), before the first game_step: the loaders of the original leave such an
 * object out and the wrappers take their fallbacks (note 10 2.3, 2.5). The sound library restarts (the start-up sound is chosen again). */
void game_sound_objects_available(game_t *g, const uint8_t present[37]);
/* The register writes of the most recent step without the rest of the scene (what game_scene puts in audio and audio_count). */
const game_audio_cmd_t *game_audio_commands(const game_t *g, int *count);
/* Tests only: the world (a world_t *; world.h) for the display lists' stores. */
void *game_debug_world(game_t *g);
/* Tests only: the mission scripts' state (missions.h; the key lock, the delay, the shadowing timer). */
struct missions;
struct mover;
const struct missions *game_debug_missions(const game_t *g);
/* The screen module (tests: the flow request byte V:4A8E2 and its countdown). */
const struct screen *game_debug_screen(game_t *g);
/* Tests only: V:4A90F (the signed map-routine byte the flow states store) and V:4A8EE (the panel counter). */
int game_debug_v4a90f(const game_t *g);
int game_debug_panel_counter(const game_t *g);
int game_debug_redraw_counter(const game_t *g);   /* V:4A8EF as the byte (0xFF after the boot) */
void game_debug_set_zoom(game_t *g, int zoom);   /* tests: V:4AAFA (an external view's `]` is not the zoom key) */
int game_debug_view_level(const game_t *g);   /* V:4A83D */
int game_debug_strain_acc(const game_t *g);   /* V:4A941, the blackout accumulator (-1 idle) */
/* Tests only: the approach's camera mover (mover.h) and the window row override V:4AA3C (0: none), the camera mode V:4A966 and the look-at byte V:4A86C. */
const struct mover *game_debug_mover(const game_t *g);
int game_debug_approach_row(const game_t *g);
int game_debug_camera_mode(const game_t *g);
int game_debug_zoom(const game_t *g);   /* V:4AAFA, 0 read as 0x80 */

/* The flight log (note 08 section 10, src/core/logbook.h). The core owns the record; the frontend moves the bytes of the port container (the magic "ILOG", the
 * version byte, the 78-byte record: GAME_LOG_FILE_SIZE bytes) to and from a file.
 *   game_log_import  called once at start-up, before the first step. buf == NULL: there is no file (the normal first run): the zero record, the first tour of duty,
 *                    returns 0. Otherwise the container is checked: a blob shorter than the 5-byte header, with another magic or another version is refused (GAME_LOG_BAD_SHORT,
 *                    GAME_LOG_BAD_MAGIC, GAME_LOG_BAD_VERSION), the log starts as the zero record and the frontend reports it once; a container shorter than 83 bytes gives the
 *                    leading bytes of the record and zeros after them, a longer one the first 78 record bytes (returns 0). A call enables the welcome and the callsign entry of 10.4
 *                    after the log-disk screen; a game that never imports a log (tests, the debug starts) goes from that screen straight to the menu.
 *   game_log_export  writes the container into buf (at least GAME_LOG_FILE_SIZE bytes) and returns its size, or -1 when cap is too small.
 *   game_log_ack     the frontend has taken scene.log_dirty (saved or failed: the failure is reported once by the frontend and the game goes on). */
#define GAME_LOG_FILE_SIZE 83
enum { GAME_LOG_OK = 0, GAME_LOG_BAD_SHORT = -1, GAME_LOG_BAD_MAGIC = -2, GAME_LOG_BAD_VERSION = -3 };
int     game_log_import(game_t *g, const uint8_t *buf, size_t len);
int     game_log_export(const game_t *g, uint8_t *buf, size_t cap);
void    game_log_ack(game_t *g);

/* Fills *out with the current scene. The audio array holds the register writes the sound
 * library made in the most recent game_step (scene.h, note 10 9.2) with the master inputs;
 * calling game_scene twice without stepping returns the same writes. game_scene never
 * mutates the context (hence the const pointer). */
void    game_scene(const game_t *g, game_scene_t *out);

/* Read-only access to the world (see world.h). Frontends use it only to look up model
 * geometry (levels, vertices, polygons, palette) for the objects named in the scene. Returns
 * NULL when the assets carry no world data. */
struct world;
const struct world *game_world(const game_t *g);

#endif
