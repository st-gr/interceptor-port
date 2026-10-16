/* src/frontend/sdl/effects.h - the combat effects drawn into the world raster in the original's painter's order (docs/re/07-ai-and-weapons.md sections 7.7, 11, 13.2; "note 07").
 *
 *   the tracer pool (7.7)  a tracer is one pixel in pen 9 (0x31384); a flare, and a tracer that has stopped, is a three-stroke star in pen 9 (0x31478, the segments of the table at 0x31542);
 *                          chaff is the tracer's pixel in pen 13 for its first two passes, then the burst routine 0x31766 with the age min(0x3A - life, 12), kind 0: twelve lines in pen 13 (0x3140C, 11.4).
 *                          The pool is drawn where the gun routine runs in the frame (note 07 7.3, scene.gun_first): before the first entry of the entity pass or after the last (effects_pool_position), after the near-ground specks.
 *   an effect object (11.3)  is not here: the core walks it by its descriptor (models 33 to 35) and the draw stage runs its program (ops 70, 71, 62 of the model interpreter), like any other entry.
 *   the entity tail (11.4, 13.2, note 04a 8d)  after an entity's model: the dying burst, then the trail of the followed record.
 *
 * Every point goes through the camera matrix of the scene (world_camera_matrix_z) and the projection of note 03; polygons, lines and discs are drawn with raster.c in all four planes.
 */
#ifndef SDL_EFFECTS_H_
#define SDL_EFFECTS_H_
#include <stdint.h>
#include "raster.h"
#include "../../core/scene.h"
#include "../../core/world.h"
#include "../../core/assets.h"

/* The burst tables (11.4), from tables/burst.bin (the core's assets_load_burst, assets.h: the eight triangles of 3 vertices of 3 words, the offset triples of 0x31652, 0x31679 and 0x316A0,
 * twelve each, the nine disc offsets of 0x316C7, the five pen tables and the shape cycle 0x31722). */
typedef struct {
    int16_t shape[8][3][3];
    int8_t off[3][12][3];   /* 0: 0x31652, 1: 0x31679 (twice the base offsets), 2: 0x316A0 (three times) */
    int8_t disc[9][3];      /* 0x316C7 */
    uint8_t pen[5][12];     /* 0x316E5 (kind 1), 0x316F1 (kind 2), 0x316FD (the cruise missile's discs), 0x31709 and 0x31715 (the cruise missile's triangle layers; both hold 9 in all twelve bytes in the shipped image, so which layer takes which is unobservable): fragment j draws in pen[t][j] */
    int16_t star[3][2][3];  /* tables/star.bin (0x31542): the flare's three strokes, two points of (x, y, z) each, in position units divided by 256 */
    uint16_t cycle[32];     /* 0x31722: fragment j at age a draws the shape at the byte offset cycle[j + a] of the pointer table, 4 k for the shape k */
    uint8_t loaded;
} effects_tables_t;

/* Loads tables/burst.bin and tables/star.bin through the loader into *t; returns 0, or -1 when it is absent or malformed (t->loaded stays 0). */
int effects_load_tables(const game_loader_t *loader, effects_tables_t *t);

typedef struct {
    raster_t *r;
    const game_camera_t *cam;
    fx_mat_t m;                      /* the camera matrix V:4AC90 */
    int row_limit;                   /* V:4AA3C */
    const effects_tables_t *tab;     /* may be NULL (no bursts) */
} effects_t;

/* Sets up the draw for the scene's camera and zoom; the window's last row is r->row_limit. */
void effects_begin(effects_t *e, raster_t *r, const game_scene_t *s, const effects_tables_t *tab);

/* One point of the world (absolute position units) in camera space, as words; 0 when the difference does not fit a word (7.7: the cell difference times 16384 plus the position shifted right by 8). */
int effects_view(const effects_t *e, const int32_t pos[3], fx_vec_t *out);

/* Where the tracer pool is drawn among the scene's entries (note 07 7.3, note 04 stage 9): the gun routine that draws it is called by the frame routine before the entity pass when scene.gun_first is set,
 * else after it. Returns the index of the entry before which the pool is drawn: the first entry of the entity pass (the direct entities, queue C and the secondary objects; not the specks of the
 * near-ground pass) when gun_first, else entry_count (after the last entry). */
unsigned effects_pool_position(const game_scene_t *s);
/* The tracer pool of the scene: every effect of the kinds SHELL, FLARE and CHAFF. */
void effects_pool(effects_t *e, const game_scene_t *s);
/* The tail of the entity record `rec` whose entry has the LOD shift `lod`: its dying burst (GAME_FX_BURST) then, when it is the followed record, the trail (13.2). Returns the primitives drawn. */
int effects_tail(effects_t *e, const game_scene_t *s, int rec, int lod);
/* The trail alone (13.2); `speed_toward` is computed inside from the ring. Returns the number of discs drawn. */
int effects_trail(effects_t *e, const game_trail_t *t);

/* The jolt (13.3): the 3D window's last row for the camera's view `base` with the scene's jolt row (cockpit view only: the full-width external views cancel it). */
int effects_window_row(const game_scene_t *s, int base);
#endif
