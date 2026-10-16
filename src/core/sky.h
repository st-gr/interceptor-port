/* src/core/sky.h - the sky fill's pure rules and the sea mesh pass (docs/re/04-scene-composition.md, "Sky fill and ground polygon" and
 * "Stage [68]: the sea mesh pass"). Everything here is a pure function of its inputs (camera matrix, height, angle words, flags, tables) and a few
 * persistent words the original keeps between calls; no stdio, no floating point, no wall time. The frontend draws what these return (the sky
 * polygon through the blitter rules of raster.h, the mesh polygons through the common filler).
 *
 * Conventions of the original that the port keeps: a "word" is a signed 16-bit value; sums of products are 32-bit; shifts are arithmetic; the signed
 * 16-bit divide truncates toward zero with the correction of note 03 (fx_clip) and, on overflow, leaves the dividend register unchanged. */
#ifndef INTERCEPTOR_SKY_H
#define INTERCEPTOR_SKY_H
#include <stddef.h>
#include <stdint.h>
#include "fx.h"
#include "game.h"

/* ---- the sky polygon (ground polygon routine 0x32E5A / 0x32E52) ----------------------------------------------------------------------------- */

/* The words the routine keeps between calls: the last intersection candidate V:4AB7E (x, y, z). It is NOT cleared at the start of a call (the stale
 * z is read by the slot decision when an intersection returns "no point" first, a zero divisor). One state per call chain: the frame's sky fill and the
 * attitude ball's call share it, in the original's order. The counters are the port's: events the original does not survive (rulings, note 04). */
typedef struct {
    int16_t cand[3];
    uint32_t z0_rejects;      /* a valid hit with depth 0 (the point (0, 0, 0)): the original loops forever in the projection; port ruling: the call returns "none", counted */
    uint32_t div_overflows;   /* divides whose quotient did not fit a word (never in play: 0 in 200,000 random camera rotations) */
} sky_state_t;

#define SKY_MAX_VERTS 5
typedef struct {
    uint8_t n;                /* 0 none, 3..5 vertices */
    uint8_t whole;            /* 1: the whole-window result (pitch word above 14400 with nothing crossing planes 0 to 2) */
    uint8_t cnt[4];           /* the plane counters V:538F4..538FA: planes y = z (top), x = z (left), -y = z (bottom), -x = z (right) */
    uint8_t slot[4];          /* the slot of each plane's last hit V:538FC..53902 */
    int16_t xy[2 * SKY_MAX_VERTS];   /* screen x, y of the vertices in the note's order */
} sky_poly_t;

/* The polygon of the sky for the camera: m the nine camera-matrix words after the zoom row scale (V:4AC90), cam_y the long V:4AB1E (minus the camera
 * height, position units), cockpit = V:4A83D clear, inverted = bit 1 of V:4A982, pitch_word V:4AB42 (cockpit) or V:4AB4C, roll_word V:4AB4A (0..28799).
 * Returns the vertex count (0 none). out may be NULL. */
int sky_polygon(sky_state_t *st, const int16_t m[9], int32_t cam_y, int cockpit, int inverted, int pitch_word, int roll_word, sky_poly_t *out);

/* The side flag (0x143F2, bit 1 of V:4A982): 1 when the camera is upside down against the ground. Words 0..28799; the roll counts as 0 outside the cockpit. */
int sky_side_flag(int cockpit, int pitch_word, int roll_word);

/* ---- the tiny-polygon inputs of the fill (note 04 "What the two variables hold in flight") ------------------------------------------------------ */

typedef enum { SKY_TINY_COCKPIT, SKY_TINY_EXTERNAL, SKY_TINY_MAP } sky_tiny_view_t;

/* The colour word V:4AA0C the fill finds (its low nibble is the pen): the cockpit 13, the external view 0xFD80 (pen 0), the map 3; in the three game frames
 * whose counter V:4A992 is 18, 19 or 20 modulo 32 the cockpit has 0 and the external view 4 (the map has 3 there too). rear_age is the number of frames since the
 * cockpit rear view (head code 6) was entered, 0 for its first frame, or -1 outside the rear view: the first 11 frames have 3 and the next four (the sampled frames
 * 266 to 268; 265 was not sampled) 0, then the rule of the front view. NOTE-GAP: where the 11 comes from (the page redraw of the view change, "two seconds after the
 * key" in the note) is not known, nor whether the other HUD-off heads (5, 7) behave as the rear view. Returns the pen 0..15. */
int sky_tiny_pen(sky_tiny_view_t view, int rear_age, uint32_t frame);
/* The mask word V:4A79E the fill finds: 1 whenever the polygon routine built a polygon (its common tail stores (1, 1, 0, 0)); with no polygon the frame-start block
 * (7 in the cockpit except 0xF in the special frames, 0xF elsewhere), which no tiny case then reads. */
int sky_tiny_mask(sky_tiny_view_t view, int polygon_built, uint32_t frame);

/* ---- the zone flags (0x230F8) --------------------------------------------------------------------------------------------------------------- */

/* The zone lookup of the base-colour flag V:4A953 (table sea_zone.bin, 560 bytes) and of V:4A954 (sea_zone2.bin, 80 bytes): row V:4AA02, column V:4AA00,
 * altitude A = minus V:4AB30 (units of 256 position units), phase V:4A908 (0..15), octant V:4A90C (0..7). Returns 1 or 0. */
int sky_zone_lookup(const uint8_t *tab, size_t len, int row, int col, int32_t alt, int phase, int octant);

/* ---- the blackout strain (V:4A940, V:4A941; note 04 "blackout accumulator") ------------------------------------------------------------------- */

typedef struct {
    int8_t acc;               /* V:4A941: a signed byte; 0 at the start and after a crash restart (0x1D538), -1 when idle */
    uint8_t latch;            /* V:4A940: the turn-rate cue's latch */
} sky_strain_t;

void sky_strain_reset(sky_strain_t *s);
/* The turn-rate cue 0x270A0, once per world update (paused passes too): the latch is set when |(E + (E >> 3) + V:4A9FE) >> 3| >= 78 and cleared below. */
void sky_strain_cue(sky_strain_t *s, int pitch_rate_e, int coupling_v4a9fe);
/* Once per mission-clock second (0x298B4): up to 15 while the latch is set, else down by one while 0 or more. */
void sky_strain_second(sky_strain_t *s);
/* The fade target V:4A959 written by the game-frame routine 0x13166: 2 while V:4A941 > 5 in the cockpit view of the own aircraft, else 15. */
int sky_fade_target(const sky_strain_t *s, int cockpit_own_aircraft);

/* ---- the sea mesh pass (0x2F19C) --------------------------------------------------------------------------------------------------------------- */

#define MESH_SCAN_BYTES   2972    /* mesh_scan.bin: 0x2E600..0x2F19B */
#define MESH_FAR_BYTES    36      /* mesh_far.bin: 0x2F4F8..0x2F51B */
#define MESH_CELLS_BYTES  6224    /* mesh_cells.bin: 0x47D7C..0x495CB */
#define SKY_ZONE_BYTES    560
#define SKY_ZONE2_BYTES   80
#define MESH_MAX_VERTS    18
#define MESH_MAX_POLYS    80

typedef struct {
    uint8_t zone[SKY_ZONE_BYTES];
    uint8_t zone2[SKY_ZONE2_BYTES];
    uint8_t scan[MESH_SCAN_BYTES];
    uint8_t far_tab[MESH_FAR_BYTES];
    uint8_t cells[MESH_CELLS_BYTES];
} sky_tables_t;

/* Loads the five tables (tables/sea_zone.bin, sea_zone2.bin, mesh_scan.bin, mesh_far.bin, mesh_cells.bin). Returns 0 or -1 with a message. */
int sky_tables_load(sky_tables_t *t, const game_loader_t *loader, char *err, size_t errlen);

typedef struct {
    int32_t pos[3];           /* x, y, z position units: the viewed record's origin (cockpit) or the external camera; NOT the eye */
    int16_t m[9];             /* V:4AC90 */
    int32_t alt;              /* A = minus V:4AB30 */
    int zoom;                 /* V:4AAFA */
    int zoom_default;         /* V:4A895 non-zero: the zoom factor is the default 0x80 and A' = A */
    int map;                  /* V:4A865: the map view */
    int own_alt;              /* V:4A954 */
    int phase, octant;        /* V:4A908, V:4A90C */
} mesh_in_t;

typedef struct {
    uint8_t n;                /* vertices, at most 18 */
    int16_t v[MESH_MAX_VERTS][3];   /* camera-space words (x, y, z) for the polygon clipper with shift 0 */
} mesh_poly_t;

/* The ordered polygons of the pass (coarse pass first, then the fine pass; cells in list order, polygons in list order). Returns the count, at most max
 * (MESH_MAX_POLYS is generous: the original draws at most 63). The scaled altitude A' is returned in *a_scaled when non-NULL. */
int mesh_compute(const sky_tables_t *t, const mesh_in_t *in, mesh_poly_t *out, int max, int32_t *a_scaled);

#endif
