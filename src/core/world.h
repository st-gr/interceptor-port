/* src/core/world.h - the world: models, terrain, position blocks, palette, tables, the
 * free camera, the per-frame object set and the draw stage (the primitive handlers of note 03). Milestones 2 and 3.
 *
 * Sources: docs/re/02-assets-and-tables.md ("note 02"), 03-3d-pipeline.md ("note 03"),
 * 04-scene-composition.md ("note 04"); file formats from tools/extract/README.md.
 * Everything is read through the game_loader_t callback: no stdio, no OS calls, no floating point.
 *
 * Asset files read (all paths relative to the assets root):
 *   models/index.txt, models/model_NNN.bin   IMDL v4 models (status ok / partial / empty / failed)
 *   terrain/terrain.bin                      ITRN v2: cell tables A, B, C and item lists
 *   tables/lists_*.bin                       the display-list builder's tables (lists.h)
 *   terrain/positions.txt                    position blocks, one line per entry
 *   palette/flight.pal                       16 in-flight colours, u8 r, g, b
 *   tables/lod.bin                           240-byte level-of-detail table (fed to fx_set_lod_table; must be exactly 240 bytes)
 *   tables/range.bin                         64-byte table at 0x4621C, verbatim; read and checked, its value is dead (note 04a section 8)
 *   tables/camera_ring_{default,11,14,30}.bin   10 x i16 x, y, z (note 04)
 * A model named ok or partial in index.txt whose file cannot be read or is malformed is a load
 * error that names the file.
 *
 * Units. Positions are 32-bit "position units" (note 02 section 10): 29 bits are used and the
 * world wraps at 2^29. x and z are horizontal, y is the altitude. Angles are in the 28800 per
 * circle unit of note 03.
 */
#ifndef INTERCEPTOR_WORLD_H
#define INTERCEPTOR_WORLD_H
#include <stddef.h>
#include <stdint.h>
#include "game.h"
#include "scene.h"
#include "aircraft.h"
#include "fx.h"
#include "lists.h"
#include "flight.h"
#include "sky.h"

typedef struct world world_t;

/* Model ids are 0..133 (note 02 section 1). */
#define WORLD_MODEL_COUNT 134
/* The carrier is object id 8 (note 02 section 8; id 9 is the same data). */
#define WORLD_CARRIER_ID 8
/* The carrier's position block: entry 33 of terrain/positions.txt, cell (70, 65), fine
 * (6144, 6144), height 0 (note 02 section 9: "the carrier (class 0x20, id 8) sits at cell
 * (70, 65)"). Real starts use the placement of the mission; this is the no-data fallback of the free camera (C):
 * the free camera starts above this carrier. */
#define WORLD_ENTRY_CARRIER 33

/* Position wrap (note 02 section 10: "Positions wrap at 2^29"). */
#define WORLD_POS_BITS 29
#define WORLD_POS_MASK ((1u << WORLD_POS_BITS) - 1u)

/* status of a model id */
enum { WORLD_MODEL_ABSENT = 0, WORLD_MODEL_OK, WORLD_MODEL_PARTIAL, WORLD_MODEL_EMPTY };

/* Loads everything through the loader. Returns NULL on failure and writes a message naming the
 * offending file into err (if errlen > 0). */
world_t *world_load(const game_loader_t *loader, char *err, size_t errlen);
void     world_destroy(world_t *w);

/* ---- models -------------------------------------------------------------------------- */

int world_model_status(const world_t *w, unsigned id);        /* WORLD_MODEL_* */
int world_model_level_count(const world_t *w, unsigned id);
int world_model_blocked(const world_t *w, unsigned id);       /* items flagged blocked (partial) */

/* The viewer of one model, in model units (one model unit = 2^(8-L) position units, note 02
 * section 10, before the instance scale). eye is the camera position minus the object position,
 * expressed along the model axes; for objects without attitude (everything in milestone 2)
 * that is the plain difference. The object state is fixed at zero (all state words zero: the
 * first variant of every program, note 02 section 4). */
typedef struct {
    int32_t eye[3];      /* x lateral, y up, z forward, model units */
    uint8_t inst_scale;  /* instance scale (low nibble of the render-list entry), 0..15 */
    uint8_t band;        /* precision-band shift (0, H or L, note 02 section 2.2) applied to the distance used
                          * for level and gate tests; the eye itself stays at the near scale */
    uint8_t has_gate;    /* gate_dist and gate_lod replace the exact eye distance and inst_scale in the program-item gates (note 04a P6) */
    uint8_t gate_lod;
    int16_t gate_dist;   /* the scaled distance word of P1 */
    uint8_t op4_closed;  /* the gate of op 4 (detail faces) is closed: world_model_polys emits WORLD_PRIM_ONE for it instead of faces (note 04a P7) */
} world_view_t;

/* Fills *view from the camera-minus-object difference in position units (wrapped to the
 * signed range) for model `id`: divides by 2^(8-L+inst_scale), L being that of the level the
 * resulting distance selects (starting from the nearest level's L), and sets view->band from the
 * level's Tm/Tf (note 02 sections 2.2 and 10). Returns the level index, as world_pick_level would
 * for the finished view, or -1 when nothing is drawn. */
int world_make_view(const world_t *w, unsigned id, const int32_t delta_pos[3],
                     unsigned inst_scale, world_view_t *view);

/* Model unit scale of a level (note 02 section 2.2 and 10): returns L, the low nibble of the
 * block's scale byte; one model unit is 2^(8-L) position units before the instance scale.
 * world_model_unit_shift_h returns H, the high nibble (the shift between Tm and Tf). Both
 * return -1 for an invalid id or level. Use it to place model vertices in the world:
 * position = object position + vertex << (8 - L + instance scale). */
int world_model_unit_shift(const world_t *w, uint16_t id, int level);

/* The precision header of a level's block (note 02 section 2.2, note 04a P2): the thresholds Tm and Tf as signed words and the nibbles H and L. -1 when out of range. */
int world_model_block_precision(const world_t *w, unsigned id, int level, int *tm, int *tf, int *h, int *l);
int world_model_unit_shift_h(const world_t *w, uint16_t id, int level);

/* Detail level for the viewer (note 02 section 2.1): the first level, near to far, whose
 * threshold (shifted right by the instance scale) is greater than the view distance; a
 * "leaf only" last-resort level always matches. The view distance is fx_dist of the absolute
 * eye components, each capped at 0x7FFF. Returns the level index, or -1 when no level applies
 * (nothing is drawn). A level with nothing to draw (leaf 0xFFFF) is returned as is. */
int world_pick_level(const world_t *w, unsigned id, const world_view_t *view);

/* One visible polygon: vertex slots (indices into the level's vertex array) and the pen
 * (palette entry 0..15, note 02 section 6). A polygon with derived_quad set has 4 slots where
 * slot[3] is not a stored vertex: it is slot[0] - slot[1] + slot[2] (ops 9, 27, 30, 69). */
#define WORLD_POLY_MAX 32
typedef struct {
    uint8_t  count;
    uint8_t  pen;
    uint8_t  derived_quad;        /* 1: a quad whose fourth vertex is slot0 - slot1 + slot2; 2: op 39's quad on the slots 0 to 3 (vertices 3 and 4 derived) */
    uint8_t  kind;                /* WORLD_PRIM_POLY, WORLD_PRIM_LINE (count 2) or WORLD_PRIM_POINT (count 1) */
    int16_t  shift_slot;          /* lines of ops 12 and 14: every vertex is shifted by V[shift_slot] - V[shift_slot + 1]; -1 none */
    uint16_t slot[WORLD_POLY_MAX];
} world_poly_t;
enum { WORLD_PRIM_POLY = 0, WORLD_PRIM_LINE = 1, WORLD_PRIM_POINT = 2, WORLD_PRIM_ONE = 3 /* a handler that returns 1 without drawing (op 4 with its gate closed) */ };

/* PREVIEW (the milestone-2 geometry walk, kept for the model dump and the synthetic tests; the game draws through world_draw_entry, which is the original's word arithmetic): walks the
 * level's drawing tree for the viewer from the exact eye in model units and returns the ordered list of visible polygons (painter's order: first entry is drawn first), at most max.
 * Implements the BSP node tests, the program gates and the face rules of note 02 sections 3, 4 and 6 with exact integers (see world.c). Blocked items are skipped. Returns the number
 * of primitives written (-1 for an invalid id or level). Polygon operations covered: 3, 4, 9, 17, 18, 24, 25, 26, 27, 30, 55, 69; lines and points (kind LINE and POINT) from ops 1,
 * 2, 11, 12, 13, 14, 48, 58, 74; the other primitives (hatching, ops 10, 15, 16, 31, 39, 75) are not emitted, and the state selectors (ops 52 and 53 included) follow the state-zero
 * path. */
int world_model_polys(const world_t *w, unsigned id, int level, const world_view_t *view,
                      world_poly_t *out, int max);

/* The same with the way the walk ended: *end_flags gets WORLD_WALK_END_FFFF when the walked path ends in a stream end by the word 0xFFFF (the terrain
 * drawer returns 0 whatever was drawn, note 04a 8b rule 9) and WORLD_WALK_ABORTED when a type B model's handler returned
 * negative (the type B drawer returns 0 at once, rule 9; read from the code only, no comparison). */
#define WORLD_WALK_END_FFFF 1u
#define WORLD_WALK_ABORTED  2u
int world_model_polys_ex(const world_t *w, unsigned id, int level, const world_view_t *view,
                         world_poly_t *out, int max, unsigned *end_flags);

/* Vertex of a level (model units). Returns 0 and fills xyz, or -1 when out of range. */
int world_model_vertex(const world_t *w, unsigned id, int level, unsigned slot, int32_t xyz[3]);
int world_model_vertex_count(const world_t *w, unsigned id, int level);

/* ---- terrain and position blocks ------------------------------------------------------- */

/* Item list of one cell of table `table` (0 A, 1 B, 2 C) at cell (cx, cz). Coordinates are
 * bounds-checked against the grid (32 or 128): out of range or empty cells return 0 and read
 * nothing. Otherwise *items is set to an array of count items and *cell_index to the global
 * cell index. */
typedef struct { uint8_t layer, object_id; uint16_t dx, dz; } world_item_t;
int world_cell_items(const world_t *w, unsigned table, unsigned cx, unsigned cz,
                     const world_item_t **items, unsigned *count, unsigned *cell_index);

/* Position block entry `entry` (1-based as in positions.txt) as a position: x, y (altitude),
 * z = cell * 2^22 + fine * 256, height * 256. Returns 0, or -1 when the entry is absent. */
int world_position_entry(const world_t *w, unsigned entry, int32_t pos[3]);
/* The same entry as the five words of the table (cell x, cell z, fine x, fine z, height; note 02 section 9), for the placement. Returns 0, or -1 when the entry is absent. */
int world_position_block(const world_t *w, unsigned entry, int16_t words[5]);

/* The 16 in-flight colours, 48 bytes (r, g, b) (note 02 section 11), and the camera ring
 * (note 04): class 0 default, 1 = 0x11, 2 = 0x14, 3 = 0x30; index 0..9; offsets x, y, z. */
const uint8_t *world_palette(const world_t *w);
int world_camera_ring(const world_t *w, unsigned cls, unsigned index, int16_t xyz[3]);

/* ---- camera and the per-frame set ------------------------------------------------------ */

typedef struct {
    int32_t  pos[3];      /* x, y (altitude), z in position units: the camera (in the cockpit view the viewed record's position plus eye) */
    int32_t  eye[3];      /* the rotated cockpit eye offset already included in pos (position units; zero for every other view and the free camera) */
    uint32_t yaw;         /* heading, 0..28799 */
    int32_t  pitch;       /* nose up positive, -FREECAM_PITCH_MAX..+FREECAM_PITCH_MAX */
    uint32_t roll;        /* 0..28799: the record's roll word (right wing down negative, a positive roll is a left bank; scene.h); the free camera does not roll */
    int32_t  speed;       /* position units per step */
} world_camera_t;

/* Free camera (PORT CHOICES, not game constants; the flight model is milestone 3).
 * Stick X turns (yaw rate), stick Y pitches, the throttle axis sets the speed.
 *   full stick:   FREECAM_YAW_RATE / FREECAM_PITCH_RATE angle units per step
 *                 (240 units = 3 degrees per step, 45 degrees per second at 15 Hz)
 *   throttle:     axis -32768..32767 maps to speed 0..FREECAM_MAX_SPEED position units per step
 *   stick Y > 0 pulls back (nose up); stick X > 0 turns right (yaw increases)
 *   heading:      forward = (sin yaw, cos yaw) on (x, z); the note leaves the handedness open
 *                 (note 03, open questions)
 *   start:        over the carrier's deck axis, FREECAM_START_BACK position units behind the
 *                 carrier's position block and FREECAM_START_ALT above the deck, heading 0 =
 *                 along the deck (the carrier model runs along z, +-7500 model units of 32 position
 *                 units each, its island at negative x up to 2336 model units high) */
#define FREECAM_YAW_RATE   240
#define FREECAM_PITCH_RATE 160
#define FREECAM_PITCH_MAX  6400      /* 80 degrees */
#define FREECAM_MAX_SPEED  24576
#define FREECAM_START_ALT  38400     /* 1200 model units of the carrier, PORT CHOICE */
#define FREECAM_START_BACK 288000    /* 9000 model units behind the carrier's origin */

void world_set_controls(world_t *w, int32_t stick_x, int32_t stick_y, int32_t throttle);
void world_start(world_t *w);                     /* camera over the carrier (WORLD_ENTRY_CARRIER) */
void world_freecam_step(world_t *w);              /* debug free camera: one fixed step, apply the controls, move */
void world_set_camera(world_t *w, const world_camera_t *c);
const world_camera_t *world_camera(const world_t *w);

/* ---- the view state and the camera builder (note 06 "View kinds, settled" and "Head views", note 03 "Camera attitude in the head and external views", "External camera aim") ----
 * The persistent variables of the original's view code. The keys (game.c) change them; world_camera_build turns them and the aircraft into the frame's camera and moves the
 * aim state. Angles are in the 28800 unit, the aim pair and the head words as the original's words (positive pitch down). */
typedef struct {
    int level;              /* V:4A83D as a signed byte: 0 the cockpit view, -1 (0xFF) or 1..4 an external view at that distance level */
    int ring;               /* V:4A96A: the external direction 0..9 (KP3 and KP-dot store 3 and 9 in the cockpit too) */
    int head;               /* V:4A85F: the cockpit view code 0..13 */
    int map;                /* V:4A865: the map flag; the map is drawn when it is set and level is not 0 */
    int32_t map_y;          /* V:4ACFA while the map is planted: the map altitude (V:4A71C is the saved value; the keys change this one) */
    int look;               /* V:4A86C, the signed look-at byte: 0 no aim (the map's fixed pair); 1 aim at the viewed record with the turn limit; negative (0xFF) snap to the target every frame (the training starts store it, note 08 6.1 item 7) */
    int aim_on;             /* V:4A86D, the separate "ring camera on" flag: clear snaps the aim and turns the bracket keys into the cockpit zoom keys; the level keys act only when it is set
                             * (cleared by the fixed camera and the approach state routines; note 06 lists every writer, the fixed camera's clear is modelled) */
    int first;              /* V:4A85E: set by every snap path of the aim; cleared by the return to the cockpit */
    int fixed;              /* milestone 5 (note 07 6.3): the fixed camera of SHIFT and KP-ENTER is in force (V:4A86D cleared by it): the external camera stands at fixed_pos and looks at the viewed record; a ring key ends it */
    int32_t fixed_pos[3];   /* the point of the fixed camera (entities_fixed_camera) */
    int mode;               /* V:4A966: the camera mode (0 in play; modes 2, 3, 4 and 6 are the front end's approach cameras, note 07 6.1; 6 changes the aim target offset) */
    int32_t key_word;       /* V:4AB18: the key word (V:4A996 | V:4A96A) of the last aim frame */
    int32_t heading;        /* V:4A9E2: the record heading the ring rotation compares against */
    int32_t aim_pitch, aim_heading;   /* V:4AB4C, V:4AB4E: the aim pair (words, positive pitch down) */
    int32_t cam_pos[3];     /* V:4ACF6, V:4ACFA, V:4ACFE: the external camera position longs of the last frame */
    int head_composed;      /* the caller composed the cockpit camera words (a head code other than 0, or the ejection seat's code 0): world_camera_build uses head_words instead of the record's own */
    int16_t (*image_word)(void *user, uint32_t addr);   /* the image word at an address (the trig span of the flight tables); the aim's arctangent table 0x42C00 is read through it */
    void *image_user;
    int32_t head_words[3];  /* the composed cockpit camera words V:4AB42, V:4AB46, V:4AB4A (flight_head_camera: the composition 0x325B8 of the record matrix and the head triple) */
} world_viewstate_t;

/* Builds the frame's camera from the aircraft (its position, attitude words and matrices at +0x80 / +0x92) and the view state, and moves the aim state.
 *   COCKPIT (level 0)   the record position plus the eye offset (world_eye_offset, note 03); the camera words are the record's own with the head code 0, else vs->head_words.
 *   MAP (level != 0, map set)   x and z the aircraft's, y = vs->map_y, aim pair (7200, 0), no roll.
 *   EXTERNAL (level != 0)   the ring camera 0x2D756: the table entry of the aircraft's class and the direction, scaled per word by the level, rotated by the +0x92 matrix (directions 0
 *             and 4 of a class that is not 0x30) or the heading-only matrix, out_i = (sum M w) >> 4 (one table unit is 1024 position units), added to the record position, the height
 *             clamped to (deck + 7) x 256; the direction advances by 4 (mod 8) when the heading has changed by 7200 to 21600 since the last frame; the camera then aims at the
 *             record by the aim of note 03 (limit 2000 units per frame, 560 in a camera mode, a snap on the first external frame, on a changed key word and when V:4A86D is 0).
 * Returns 0, or -1 for a bad argument (direction outside 0..9, level outside -1..4). */
int world_camera_build(const world_t *w, const aircraft_t *ac, world_viewstate_t *vs, world_camera_t *out);

/* The head triple (pitch, yaw, roll) of a cockpit view code (note 06 "Head views"): cls is the record's class byte +0x62, code 0..13, pitch_word the record's pitch word +0x66.
 * Returns 0 for code 0 of an aircraft class (no composition: the camera words are the record's own), else 1 and fills the triple. */
int world_head_triple(unsigned cls, int code, int pitch_word, int triple[3]);

/* The aim of 0x1454A: the target pitch and heading words of the camera-to-target vector (position units), and the step of one angle toward it (limit < 0: snap). */
void world_aim_target(const world_viewstate_t *vs, int32_t dx, int32_t dy, int32_t dz, int32_t *pitch, int32_t *heading);
int32_t world_aim_step(int32_t current, int32_t target, int32_t limit);

#define WORLD_RING_UNIT_SHIFT 4       /* the ring offset is the sum of the three products with the Q14 matrix words, shifted right 4: one table unit is 1024 position units (note 06 item 4) */
#define WORLD_MAP_PITCH 7200          /* the map camera's aim pair V:4AB4C, V:4AB4E (note 06 item 2) */
#define WORLD_MAP_ALT_MIN 0x1000000   /* the map altitude's limits and step (the `]` and `[` keys) */
#define WORLD_MAP_ALT_MAX 0x8000000
#define WORLD_MAP_ALT_STEP 0x2000000
#define WORLD_MAP_ALT_START 0x3000000 /* V:4A71C at every mission start (0x1D276) */
#define WORLD_AIM_LIMIT 2000          /* the aim's per-frame limit, 560 in a camera mode */
#define WORLD_AIM_LIMIT_MODE 560
/* Object attitude (note 03 "Object matrices" and "Translation to camera space", handedness). world_object_matrix writes the Q8 matrix the vertex program applies to model
 * vertices (fx_rotate) before the shift and the offset: fx_matrix_b fed all three record angles negated, pitch first (tag C: no rig comparison covers the object
 * attitude), converted with the documented >> 6. The arguments are the scene's: pitch nose up positive (the record's pitch word is its negation), roll the record's
 * roll word (right negative), yaw the heading word. All zero gives the identity (256 on the diagonal). world_object_eye turns the camera-minus-object difference
 * into the model axes, eye = R^T delta (R orthonormal), for the BSP side tests and the level choice. */
void world_object_matrix(fx_mat_t *q8, int32_t pitch, int32_t roll, int32_t yaw);
void world_object_eye(const fx_mat_t *q8, const int32_t delta[3], int32_t eye[3]);

/* The cockpit eye offset in position units (note 03): the class's body-axes offset rotated by the record's matrix mat_b (+0x92); the cockpit camera is the record's position plus this. */
void world_eye_offset(const aircraft_t *ac, int32_t eye[3]);
/* The player's start, the no-data fallback (C; real starts use the mission placement): the carrier's position block, entry WORLD_ENTRY_CARRIER (33),
 * heading 0, on the carrier, speed 0. Returns 0, or -1 when the entry is absent (aircraft zeroed). */
int world_aircraft_start(const world_t *w, aircraft_t *ac);

/* ---- the display lists (docs/re/04a-display-lists.md) ----------------------------------------------------------------- */

/* The cell callback the builder reads the terrain through, and the builder state of this world. The tables come from assets/tables/lists_*.bin. */
int world_lists_cell(void *world, unsigned table, unsigned cx, unsigned cz, const lists_item_t **items);
lists_t *world_lists(world_t *w);
const lists_models_t *world_lists_models(const world_t *w);   /* the drawer's model side (for a lists_t of a test) */
const lists_tables_t *world_lists_tables(const world_t *w);

/* A writer of the request byte outside the builder: the position update on a cell change, camera and view switches (note 04a section 1). */
void world_lists_request(world_t *w, unsigned mask);

/* One frame of the builder: the detector, the camera words (from world_set_camera's camera), the builder with the run sort and the
 * landmark reorder, and the walk. in->cam_* are overwritten from the world's camera. */
void world_lists_frame(world_t *w, const lists_frame_t *in);
/* The two halves (build, then walk) with the sky fill and the sea mesh between them, the original's order: builder, sky fill [60], mesh [68], queues A and B, [90]. */
void world_lists_build(world_t *w, const lists_frame_t *in);
void world_lists_walk(world_t *w);
void world_lists_walk_map(world_t *w);   /* the map view's walk: queues A and B and the [90] pass, no entity pass */
int world_altitude_gate(const world_t *w);   /* 0x111E0 (07 7.3) */

/* The landmark collision slots the last tier C build filled (note 04a section 7) in the flight module's form; returns the slots in use. */
int world_landmarks(const world_t *w, flight_lm_slot_t out[FLIGHT_LM_SLOTS]);
/* The footprint of the object record `desc` (0 to 133) as a landmark slot (04a section 7) with the cell offsets ox and oz, as the builder's tail copies it: for the rig tests that put a table footprint in
 * the original's memory. Returns 0, or -1 when the record has no footprint. */
int world_footprint_slot(const world_t *w, unsigned desc, int ox, int oz, flight_lm_slot_t *out);

/* ---- the draw stage (note 04a section 8b, note 03) ------------------------------------------------------------------------ */

/* V:4AA3C: the 3D window's last row (the point sprites and discs of the draw stage count against it); world_window_last_row gives it by view (map shown: 179, external: 167, cockpit: by head code). */
int  world_window_last_row(int map, int outside, int head);
int  world_head_step(const int8_t *step_tab, int code);   /* the HUD step byte of a head code from tables/head_step.bin (the table at 0x1E800; 0 outside the codes 0..13 or with no table) */
void world_set_window_last_row(world_t *w, int row);

/* The camera matrix of a scene camera (Q8): fx_cam_ypr of the camera's angles with its rows scaled by the zoom triple V:4AAF6 = (0xA8, 0xFC, zoom), zoom being
 * V:4AAFA (0x80 at the start; 0 counts as 0x80). world_camera_matrix is the default zoom. This is the matrix V:4AC90 that the objects, the sky polygon and the
 * sea mesh pass use (notes 03 and 04). */
#define WORLD_ZOOM_ROW_X 0xA8
#define WORLD_ZOOM_ROW_Y 0xFC
void world_camera_matrix(const game_camera_t *cam, fx_mat_t *m);
void world_camera_matrix_z(const game_camera_t *cam, int zoom, fx_mat_t *m);
/* The attitude matrix V:4ACA2 of the attitude ball (note 04: the second call of the sky routine, note 06): fx_cam_ypr of the aircraft's angles WITHOUT the zoom row
 * scale. Evidence: the nine captured attitude balls (att-*, pitch-only and banked) are reproduced pixel for pixel with it and the filler of raster.h, and not with
 * the scaled matrix (a pitched horizon would be 15 rows off). */
void world_attitude_matrix(const game_camera_t *cam, fx_mat_t *m);
/* The sky block of a bare camera (tests that override the scene's camera, --hud-test): the matrix, the words, the side flag and the polygon of the camera with
 * the land base, no sea mesh and fade target 15; cam->pos.y is the camera height (V:4AB1E = minus it). */
/* The sky block for a camera set from outside (the hud-test path), with the inputs and rules of game.c's sky_frame: st is the persistent candidate state
 * (V:4AB7E, kept by the caller across frames, zeroed at the start), frame the game frame counter V:4A992, rear_age the frames since the rear view was entered (-1: not
 * the rear view). No mesh, no zone flags. */
void world_sky_for_camera(game_sky_t *s, const game_camera_t *cam, int zoom, int row_limit, sky_state_t *st, uint32_t frame, int rear_age);

/* The detail level of object id `id` for the walker's distance word: a node fits when (threshold & 0x3FFF) >> lod is greater than the zoom-scaled
 * distance as a signed word (type B lists: the unmasked threshold >> lod is at least the distance). out->found is 0 when no level fits.
 * Returns 0, or -1 for an unknown model. */
int world_pick_level_word(const world_t *w, unsigned id, unsigned lod, int16_t scaled, lists_level_t *out);
/* The level of the phase list `list` (1 to 4) of a cone model (models 2, 13 and 11, note 07 11.5); 0, or -1 when the model has none. */
int world_model_block_flags(const world_t *w, uint16_t id);   /* +7 of the model's first block (07 8.2: bit 4 = a big hull) */
int world_pick_cockpit_level(const world_t *w, unsigned id, lists_level_t *out);
int world_pick_cone_level(const world_t *w, unsigned id, unsigned list, lists_level_t *out);

/* One primitive the draw stage hands to the frontend, in the original's draw order (note 03, "Primitive output: lines, points, fill modes"). Coordinates are
 * the 320 x 180 projection space (screen x 0..319, row 0..179) as the clipper and the line and point routines leave them; the frontend limits the rows to V:4AA3C
 * (the window's last row) and column 0..319 itself. The pen is the low four bits of V:4AA0C; pen bit k goes to bit plane k. */
enum { WORLD_OUT_POLY = 0, WORLD_OUT_LINE = 1, WORLD_OUT_POINT = 2, WORLD_OUT_DISC = 3 };
typedef struct {
    uint8_t  kind;      /* WORLD_OUT_* */
    uint8_t  pen;       /* 0..15 */
    uint8_t  mask;      /* the plane mask V:4A79E (low four bits): the planes the primitive writes */
    uint8_t  sprite;    /* points: 0 the 1 by 1 sprite (0x340D4), 1 the 2 by 1 sprite (0x340EA), 2 the 2 by 2 sprite (0x3414E) */
    uint8_t  alt;       /* polygons: the alternate fill path 0x34EEC is taken (V:4A7A4 not zero and the pattern word not negative): the general fill sets plane 2 from plane 3 under the area and nothing else */
    uint8_t  radius;    /* discs: the radius R, 0..127 (R = 0 is the 2 by 1 sprite) */
    uint8_t  n;         /* polygons: vertex count; lines: 2; points and discs: 1 */
    int16_t  pattern;   /* polygons: V:4A7A0; negative = solid (pen bit k sets or clears the area in plane k); otherwise bit k of the word sets (1) or clears (0) the area in plane k */
    uint16_t toggle;    /* polygons: V:4A7A2; bit k set toggles plane k instead (pattern mode) */
    int16_t  xy[2 * 64];
} world_out_t;
typedef void (*world_out_sink_t)(void *user, const world_out_t *o);
#define WORLD_RS_MAX 32
typedef struct {
    int polys;          /* polygons fx_clip kept with at least three vertices */
    int fv_ok;          /* the first-vertex test passed, or does not apply */
    int level;          /* the level drawn (-1: none) */
    int ends_ffff;      /* the walked path ended by the word 0xFFFF (terrain drawer: result 0) */
    int aborted;        /* a type B handler returned negative: the drawer returns 0 */
    int endless;        /* a type B search the original would never leave (scaled distance above 0x7FFF >> LOD; note 03 port ruling: nothing is drawn); unreachable in play, counted by the caller */
    int decider;        /* the operation of the handler that decided the sign: the last that counted, else the one that abandoned the program (0: none) */
    uint16_t toggle_in; /* the toggle word V:4A7A2 at the entry of this object's drawer (only a type B drawer keeps it from the previous object) */
    uint16_t alt_in;    /* V:4A7A4 at the entry (a type B drawer keeps it from the previous object: 0xFFFF after the near-ground pass) */
    int16_t pbuf_in[3]; /* the depth words at the vertex positions 4 to 6 of the polygon buffer V:5103C at the entry (the residue op 9 reads, note 03) */
    int den_zero;       /* clipped-line segments rejected because the margin difference was zero (the original hangs; note 03 port ruling: rejected and counted) */
    int rs_n;           /* the record-copy operations the program ran (42, 43, 44, 59 with their base slot; 56, 63, 64, 65 with base 0), in program order, for the display lists' slot store */
    uint8_t rs_op[WORLD_RS_MAX]; int16_t rs_base[WORLD_RS_MAX];
    int op6_small;      /* op 6 items with N below 2 (N = 0 divides by zero, N = 1 and the negative N overwrite 1.5 MB of the original's data): drawn as nothing (note 03 port ruling); none in the shipped data */
} world_draw_info_t;

/* Runs the drawing program of a walked entry through the core's pipeline (rotate, shift, offset, camera, fx_clip) for the camera `cam`. The level
 * is chosen from the entry's distance word and zoom (not from the exact distance). Entries with draw == 0, and the variants that show nothing,
 * return 0 at once with no polygon. sink may be NULL (counting only). Returns 0, or -1 when the model has no level to draw. */
int world_draw_entry(const world_t *w, const game_entry_t *e, const game_camera_t *cam, world_out_sink_t sink, void *user, world_draw_info_t *info);

/* Test hooks (tests/rig/test_draw_diff.c): one primitive handler on camera-space slots; see world.c. */
int  world_test_handler(int op, const int32_t *operands, int nops, unsigned pen, const fx_vec_t *slots, int nslots, int off_y, int reset,
                        world_out_sink_t sink, void *user, int *code);
void world_test_filler(const int16_t *xy, int n, int row_limit, unsigned pen, world_out_sink_t sink, void *user);   /* the filler's front on screen vertices (small shapes) */
void world_test_fill_state(unsigned *mask, int *pattern, unsigned *toggle, unsigned *pen);
/* The count of clipped-line segments rejected for a zero margin difference since the last world_test_handler call (the rig hangs on them). */
int  world_test_den_zero(void);
/* The count of op 6 items with N below 2 since the last world_test_handler call (nothing is drawn for them). */
int  world_test_op6_small(void);

/* Fills camera, hud pitch/roll/heading/altitude and the entry stream of *out (the other fields are untouched; the caller clears and sets
 * mode, tick): the entries the last world_lists_frame's walk passed to the draw routine, in the original's draw order (scene.h). */
void world_fill_scene(const world_t *w, game_scene_t *out);
/* The same with the view the frame's camera matrix is built for (the cockpit view builds it from pitch, yaw and roll, the others from pitch and yaw alone): the near-ground specks
 * (stage [90], GAME_ENTRY_SPECK*) are projected with that matrix. world_fill_scene is the cockpit view. */
void world_fill_scene_view(const world_t *w, game_scene_t *out, game_view_t view);
/* The stores of the sky fill's tail and of the sea mesh to the fill block (note 04, "Stage [90]": the block the walk finds): (1, 1, 0, 0) when the sky fill built a polygon, then (2, 2, 0, 0) and the colour 6 when the
 * sea mesh stage ran (mesh_ran). Called after the frame's sky stage and before the walk (the original's order). */
void world_fill_block_sky(world_t *w, int sky_polygon, int mesh_ran);
/* The sea mesh pass as a writer of the polygon buffer (note 03, "Op 9 reads past its quad"): begin clears the shift word at entry (also when no polygon follows); each polygon of the pass, in order, stores
 * its count word and its n vertices (the residue is all of them in order, not the last only). */
void world_mesh_pbuf_begin(world_t *w);
void world_mesh_pbuf_polygon(world_t *w, const fx_vec_t *v, int n);
/* Test hooks: the fill block's fourth word V:4A7A4 and the seven depth words of the polygon buffer V:5103C. */
unsigned world_test_fill_alt(void);
void world_test_pwords(int *shift, int *count);   /* the shift and count words of the polygon buffer V:5103C */
void world_test_set_fill_alt(unsigned v);
void world_test_pbuf(int16_t out[7], int set);   /* set != 0 stores out into the buffer, else reads it */

#endif
