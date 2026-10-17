/* src/core/mapmarks.h - the entity marks of the map view (docs/re/06-cockpit-hud.md "Map renderer" and "What V:4A90F and V:4A86D hold, and the paths with them clear";
 * the original's loop 0x2FE26 inside 0x2FC58, the per-entity routine 0x30046 and the ship routine 0x3001C).
 *
 * The original draws the marks once per game pass while the map is shown (V:4A865 positive, an external view), from the camera the pass has: the viewed record's x and z with the map altitude.
 * Each record keeps two bytes of its own, +0x70 (the position nibbles) and +0x71 (the heading sector; 0xFF from the placement), which the per-entity routine reads and writes; the choice
 * between drawing at the true position, drawing the stored mark and drawing nothing is made by V:4A86D, V:4A90F and the frame counter V:4A992 (the table in mapmarks.c). This module is the
 * routine's decision half: it produces the list of marks with the screen point, the shape (a direction sector 0 to 15 or the ship) and the pen; the frontend draws the shape.
 *
 * Core rules: C99, fixed-width integers, no stdio, no floating point, no wall time. */
#ifndef INTERCEPTOR_MAPMARKS_H
#define INTERCEPTOR_MAPMARKS_H
#include <stdint.h>

#define MAPMARKS_RECORDS 16
#define MAPMARKS_SHIP 16   /* the shape index of the ship shape (the sectors are 0 to 15) */

/* What the routine reads of one record (offsets in the 0x200-byte record): word 0, byte +3 (the low byte of W2), the class byte +0x62, byte +0x20, the position longs +0x14 and +0x1C and the heading word +0x68. */
typedef struct { uint16_t w0; uint8_t b03, cls, b20; int32_t x, z; int16_t heading; } mapmarks_rec_t;

typedef struct {
    int32_t cam_x, cam_y, cam_z;   /* the map camera: the viewed record's x and z and the map altitude V:4ACFA, position longs */
    uint16_t n;                    /* V:4A992, the word the routine reads */
    uint8_t mode;                  /* V:4A95E */
    uint8_t v4a86d;                /* the direct-draw flag */
    int8_t v4a90f;                 /* the signed mask byte */
    int16_t last_row;              /* V:4AA3C: a projected row beyond it is rejected */
    mapmarks_rec_t rec[MAPMARKS_RECORDS];
} mapmarks_in_t;

typedef struct { int16_t x, y; uint8_t rec, shape, pen; } mapmark_t;   /* x, y: the point handed to the shape drawer (the row is the call row plus one) */

typedef struct {
    uint8_t b70[MAPMARKS_RECORDS], b71[MAPMARKS_RECORDS];   /* +0x70 (x nibble << 4 | y nibble) and +0x71 (sector, 0xFF unplaced) of each record */
    uint8_t count;
    mapmark_t mark[MAPMARKS_RECORDS];                       /* the marks of the last pass, in ascending record order */
} mapmarks_t;

void mapmarks_init(mapmarks_t *m);   /* every +0x71 = 0xFF, no marks */
/* One call of the map routine's entity loop. Fills m->mark / m->count and updates the stored bytes. */
void mapmarks_pass(mapmarks_t *m, const mapmarks_in_t *in);

#endif
