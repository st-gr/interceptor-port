/* raster.h - 320x200 indexed framebuffer rasteriser for the world window.
 * Geometry from docs/re/04-scene-composition.md ("3D window", "Horizon and ground")
 * and docs/re/03-3d-pipeline.md ("Projection"):
 *   - the window is x 0..319, bitmap rows 1..row_limit (row 0 is never part of it);
 *     row_limit is 144, 167 or 179 depending on the view kind;
 *   - projection row r is bitmap row r; the projection space is 320x180, centre row 90,
 *     focal constants 160 (horizontal) and 90 (vertical); screen x = 319 - sx', so the
 *     horizontal centre is column 159.
 * Everything is cropped to the window; nothing ever writes outside px. */
#ifndef SDL_RASTER_H_
#define SDL_RASTER_H_
#include <stdint.h>
#include "../../core/scene.h"

#define RASTER_W 320
#define RASTER_H 200
#define RASTER_MAX_VERTS 128   /* polygons with more vertices are ignored */
#define RASTER_ANGLE_CIRCLE 28800 /* angle units per full circle (note 03) */

typedef struct {
    uint8_t px[RASTER_W * RASTER_H];   /* palette indices */
    int row_limit;                      /* last drawable world row, 144/167/179 per note 04 */
    const uint8_t *page_below;          /* the page's rows below the window as they stand at stage [90] (the previous frame's panel: RASTER_W * RASTER_H pens, 0 where nothing was drawn), or NULL (all zero): what the alternate blit's C reads meet there */
} raster_t;

/* Sets every pixel of the 320x200 buffer (including row 0 and rows below the limit). */
void raster_clear(raster_t *r, uint8_t colour);
/* Clamped to 0..199 (0 means nothing is drawable). */
void raster_set_row_limit(raster_t *r, int row_limit);
/* Fill a polygon given as n (x,y) pairs in 320x180 projection space with the blitter filler of note 04 ("Filling the polygon"): the edges XOR-ed into one plane (rows ymin + 1
 * to ymax of every edge, the Bresenham step of the note, the cut at the row limit), the inclusive fill from the odd edge pixel to the next, the tiny boxes as lines and dots.
 * Cropped to rows 1..row_limit. Returns 0 for the general fill, 1 for a tiny case or nothing. raster_fill_polygon replaces the pixels by `colour` (all four planes). */
void raster_fill_polygon(raster_t *r, const int16_t *xy, int n, uint8_t colour);
/* Plane-aware polygon of note 03 "Primitive output" (pen bit k goes to plane k; planes outside mask are untouched; pens >= 16 count as 0). pattern < 0: solid (pen bit k sets or
 * clears the area in plane k); else bit k of pattern sets/clears, bit k of toggle toggles. The same coverage as the filler above for every fill state. */
int raster_fill_polygon_planes(raster_t *r, const int16_t *xy, int n, unsigned mask, unsigned pen, int pattern, unsigned toggle);
/* A line of the object drawer (0x3455E, note 03 "The raster line"): from the end with the smaller y (equal y: the first end), starting at its x and the row below its y, Bresenham error 4 minor - 2 major, every
 * pixel written, cut at the last row. */
void raster_line_planes(raster_t *r, int x0, int y0, int x1, int y1, unsigned mask, unsigned pen);
/* The near-ground pass's triangle (stage [90], note 04): the filler's cases with the pass's block; the line, dot and block cases clear plane 2 of their pixels, a general fill clears plane 2 under the area, or with alt
 * takes the alternate blit (raster_fill_alt's rule). xy holds the three vertices. */
void raster_ground_triangle(raster_t *r, const int16_t *xy, int alt);
/* The filler's alternate path 0x34EEC for a general fill (descending blit, C = D = plane 2 with BLTCMOD 40): in the polygon's word rectangle a pixel under the filled area gets plane 2 := plane 3, any other pixel plane 2 := the
 * bit of its C word, read W words further left on every row up (into the end of plane 1 below the start of plane 2; note 04 "The blit's C channel"); outside the rectangle nothing changes. */
void raster_fill_alt(raster_t *r, const int16_t *xy, int n, unsigned mask, unsigned pen);
/* sprite 0: 1x1, 1: 2x1, 2: 2x2 (rules in the note); y <= 0 rejected. */
void raster_point_sprite(raster_t *r, int x, int y, int sprite, unsigned mask, unsigned pen);
/* Disc of radius R (0 = 2x1 sprite, limited to 127): 2R rows y-R+1..y+R. */
void raster_disc(raster_t *r, int x, int y, int R, unsigned mask, unsigned pen);
/* raster_line_planes with all four planes and a solid colour. */
void raster_line(raster_t *r, int x0, int y0, int x1, int y1, uint8_t colour);

/* The window base of the sky fill (note 04, "The window clear and the pens"): rows 1 to row_limit over the full width cleared to the ground pen, 4 over land or 6
 * over the sea zone. Row 0 and the rows below the limit are not touched. */
void raster_window_base(raster_t *r, uint8_t pen);

/* The sky polygon filler 0x34CD6 as the sky fill uses it (note 04, "Filling the polygon"): xy holds n vertices in window coordinates (the core's sky polygon).
 * The general fill draws every edge into plane 0 with the blitter line (XOR; the rows ymin + 1 to ymax of the edge, the Bresenham step of the note, one dot
 * per row on shallow lines, the length cut at the row limit, an edge whose first row is below the limit skipped) and fills the words from the one of
 * max(minx - 1, 0) to the one of maxx, rows miny to min(maxy, limit), right to left and inclusive with the carry cleared per row; the polygon's pixels become the
 * sky pen 5 (plane 0 set; over the sea, `sea`, plane 1 cleared as well). Boxes of at most two rows or one column are the tiny cases: a line in the pen
 * tiny_pen (all four planes), a 2 by 2 block of dots or a dot or two (planes tiny_mask take their bit of tiny_pen). Nothing is drawn in row 0 or below the limit.
 * Returns 0 for the general fill, 1 otherwise (the sea clear of plane 1 is skipped then, as in the original). The pixels are those of the window already
 * cleared by raster_window_base. */
int raster_sky_fill(raster_t *r, const int16_t *xy, int n, int sea, unsigned tiny_pen, unsigned tiny_mask);

/* The coverage of the general fill alone (the attitude ball fills its scaled copy of the sky polygon with it): bits is RASTER_W x RASTER_H bytes, set to 1 where the
 * polygon's pixels are; rows are limited to row_limit; no pixel of a raster is touched. Returns 0 for the general fill, 1 when the box is one of the tiny cases or
 * lies below the limit (bits is then not written). */
int raster_blit_cover(const int16_t *xy, int n, int row_limit, uint8_t *bits);

/* The filler's second entry 0x34CD0, the attitude ball's (note 04, "The two entries of the filler and the limit of the tiny cases", "The attitude ball's tiny cases"): the row limit A of the early exit,
 * the dot and block cases and the general fill's edge cut is 199; the two line cases cut at the window's last row (r->row_limit, V:4AA3C) and the 2 by 2 block chooses its one-row pair by it. The
 * tiny cases draw straight into r->px in the stale colour 3 with the plane mask 0xF (pen 3 on all four planes, outside the ball's ellipse mask); the general fill leaves its pixels set in `bits`
 * and returns 0, anything else returns 1. */
int raster_ball_fill(raster_t *r, const int16_t *xy, int n, uint8_t *bits);

/* The same filler for any solid polygon: the pixels of the polygon take the planes of `mask` from `pen` (the planes outside the mask keep their value); the tiny
 * cases as in raster_sky_fill with tiny_pen = pen and tiny_mask = mask. Returns 0 for the general fill, 1 otherwise. The object drawing's polygons use the same filler
 * (raster_fill_polygon_planes); the sea mesh polygons use this solid form (frame comparison, tests/frontend/test_sky_frames.py). */
int raster_fill_polygon_blit(raster_t *r, const int16_t *xy, int n, unsigned mask, unsigned pen);

/* The whole of the sky fill and the sea mesh stage for one frame (steps [60] and [68] of note 04): the window base (6 over the sea zone, else 4), the sky polygon
 * (raster_sky_fill) and, over land, the mesh polygons with pen 6 and the plane mask 2 (plane 1 set: land 4 becomes 6, sky 5 becomes 7). A block that is not valid
 * (no flight step yet) leaves the window as the land base. */
void raster_sky_scene(raster_t *r, const game_sky_t *sky);

/* The sky region as a half-plane polygon (the earlier stand-in of the ground; the 3D window now uses the core's exact polygon), for the attitude ball (note 06 "Attitude ball", which scales the polygon the world
 * filled with the sky colour, 0x504DE). Vertices in 320x180 window coordinates (x 0..319, y 0..179), clockwise from the top, written to
 * xy as (x, y) pairs (room for 8 pairs); returns the number of vertices: 0 when the whole window is ground, 4 for the whole window.
 * Level flight on the ground gives the note's rectangle (0,0), (319,0), (319,89), (0,89): the edge is the last sky row.
 * C: the fallback for a scene with no sky block (the core's polygon is the exact one): the half-plane clipped to the window, with the edge on the last sky row. */
int raster_sky_polygon(int32_t pitch, int32_t roll, int16_t *xy);
#endif
