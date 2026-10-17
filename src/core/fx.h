/* src/core/fx.h - fixed-point 3D pipeline reproduced from docs/re/03-3d-pipeline.md.
 *
 * Names for Task 10 (the plan's names -> the real ones):
 *   fx_clip_near -> fx_clip   (there is no separate near plane: four planes through the eye,
 *                              note "Clipping and projection of polygons")
 *   fx_sort_key  -> fx_distkey (key of one object) and fx_sort (order a group by those keys)
 *   fx_sin, fx_cos, fx_rotate, fx_project keep their names.
 *
 * Portability: the port assumes two's complement integers and arithmetic right shift of
 * negative values (true of gcc, clang, tcc, emscripten); tests/test_fx.c asserts it.
 *
 * Every function is bit-exact with the original 68000 routine named in the note
 * (section "Routines"). "Note:" references below name the note section a rule comes from.
 * Fixed-width types only, no floating point, no OS calls.
 *
 * Conventions (note, preamble): a "word" is a signed 16-bit value; products are 16x16 to
 * 32 bits; right shifts are arithmetic; a result stored as a word keeps its low 16 bits.
 * Vectors carry words in int32_t fields: inputs are truncated to 16 bits, outputs that the
 * original stores as words are sign-extended.
 *
 * Angles: 28800 units per circle (1/80 degree), valid range 0..28799 (note, "Angle unit and
 * sine table"). The original does not handle negative or larger angles; this port reduces
 * the 0.1-degree angle modulo 3600 so any uint32_t is safe (a deviation outside the
 * original's domain only). fx_neg_angle() is the "28800 - angle, zero stays zero" inverse.
 *
 * Matrix formats:
 *   Q14 object matrices: fx_matrix_a, fx_matrix_b, fx_roll_q14 (16384 = 1.0).
 *   Q8 camera/vertex matrices: fx_cam_yp, fx_cam_ypr, fx_roll_q8, fx_mat_q14_to_q8 (256 = 1.0).
 *   fx_rotate and fx_camera expect a Q8 matrix (out = sum >> 8).
 *
 * Deviations from the interface sketch (all required by the note's register conventions):
 *   - fx_rowscale takes the three zoom words (the note's table at 0x4AAF6) explicitly.
 *   - fx_clip takes the shift S (the note's word at 0x5103C) as an extra last argument.
 *   - fx_visible takes the flags word (bits 7..9 give the shift k) and returns 1 when
 *     the polygon FACES the viewer (the note's D7 = 0); the original's D7 is the negation.
 *   - fx_distkey takes the camera-offset state (the note's words at 0x4AB2A, 0x4AB2E, long
 *     0x4AB30 and shift word 0x4AB70) as a struct, plus the three offsets.
 *   - fx_project_rows() additionally takes the row limit word 0x4AA3C; fx_project() uses
 *     0x7FFF as in the note's examples.
 *   - Matrix builders name their angles a, b, c (the note's A, B, C = D0, D2, D4); which is
 *     yaw, pitch or roll is an open question in note 03.
 *   - The plan's fx_clip_near / fx_sort_key are fx_clip / fx_distkey + fx_sort (see top).
 *   - fx_compose (0x325B8) is not implemented: only partly verified and its arcsine tables
 *     (0x43098, 0x434E8) are uncharacterised in the note.
 *   - fx_clip output is capped: n_in > FX_CLIP_MAX_IN returns 0 (the original's buffer size
 *     is not in the note).
 *
 * Tables: the sine table (note, "Tables", 0x436E8) and the hypotenuse table (0x208E8) are
 * generated at first use from the formulas the note gives, in integer arithmetic.
 * Only fx_lod_shift needs the level-of-detail table (0x20E4A, 240 bytes), which must be
 * supplied through fx_set_lod_table() (loaded from assets/); every other function works
 * without it.
 */
#ifndef INTERCEPTOR_FX_H
#define INTERCEPTOR_FX_H
#include <stddef.h>
#include <stdint.h>

typedef struct { int16_t m[3][3]; } fx_mat_t;   /* row major; Q14 or Q8 per the list above */
typedef struct { int32_t x, y, z; } fx_vec_t;

/* Camera-offset state read by fx_distkey (note, "Object ordering key"). */
typedef struct {
    int16_t off_x;   /* word 0x4AB2A, added to the first offset (d1) */
    int16_t off_z;   /* word 0x4AB2E, added to the third offset (d7) */
    int32_t off_y;   /* long 0x4AB30, added to the second offset (d6) */
    int     shift;   /* word 0x4AB70, level-of-detail shift */
} fx_cam_off_t;

#define FX_CLIP_MAX_IN 32   /* port limit, see deviations */

/* Sine/cosine, Q14, of an angle in 28800-per-circle units (shifted right by 3 inside, as
 * every consumer in the note does). The 0.1-degree entry points are fx_sin_tenth / fx_cos_tenth. */
int16_t fx_sin(uint32_t angle);
int16_t fx_cos(uint32_t angle);
int16_t fx_sin_tenth(uint32_t a);   /* the note's sincos1/sincos2 input unit, 3600 per circle */
int16_t fx_cos_tenth(uint32_t a);
uint32_t fx_neg_angle(uint32_t angle);   /* 28800 - angle, zero stays zero */

/* Matrix builders; the angles are in 28800 units. a,b,c = D0,D2,D4 of the original. */
void fx_matrix_a(fx_mat_t *out, uint32_t a, uint32_t b, uint32_t c);   /* Q14, 0x32B52 */
void fx_matrix_b(fx_mat_t *out, uint32_t a, uint32_t b, uint32_t c);   /* Q14, 0x32BEC */
void fx_cam_yp(fx_mat_t *out, uint32_t a, uint32_t b);                    /* Q8, 0x32A66 */
void fx_cam_ypr(fx_mat_t *out, uint32_t a, uint32_t b, uint32_t c);    /* Q8, 0x32AB6 */
void fx_roll_q8(fx_mat_t *out, uint32_t angle);                                 /* 0x32A1E */
void fx_roll_q14(fx_mat_t *out, uint32_t angle);                                /* 0x32A48 */
void fx_rowscale(fx_mat_t *m, const int16_t scale[3]);                          /* 0x32C84 */
void fx_mat_q14_to_q8(fx_mat_t *out, const fx_mat_t *in);                       /* each element >> 6 */

/* Q8 matrix times a vector of words, sum >> 8 stored as a word (note, "Object matrices"
 * and "Camera matrix"). fx_camera is the same operation, kept as a separate name. */
void fx_rotate(fx_vec_t *out, const fx_mat_t *m, const fx_vec_t *v);
void fx_camera(fx_vec_t *out, const fx_mat_t *cam, const fx_vec_t *v);

/* Perspective projection of one camera-space point (0x3377C). Returns 1 and the screen
 * point (320x180 space, x mirrored), or 0 when rejected (sx = sy = 0 then). */
int fx_project(int16_t *sx, int16_t *sy, const fx_vec_t *v);
int fx_project_rows(int16_t *sx, int16_t *sy, const fx_vec_t *v, int16_t row_limit);

/* n_out_max is the capacity of out_xy in VERTICES (2 * n_out_max int16_t).
 * Clip a camera-space polygon to the four planes and project it (0x28B6A). in: n_in
 * vertices (words), shift = S. out_xy receives x,y pairs; returns the vertex count, or 0
 * if nothing remains (also when out_xy cannot hold the result or an intersection divides
 * by zero, where the original calls the error handler). */
int fx_clip(int16_t *out_xy, int n_out_max, const fx_vec_t *in, int n_in, int shift);

/* Three-vertex back-face test (0x22ACC). 1 = faces the viewer. */
int fx_visible(const fx_vec_t *a, const fx_vec_t *b, const fx_vec_t *c, uint16_t flags);

/* Distance approximations (0x20884, 0x2082A). v->x, v->y, v->z are D2, D3, D4. */
int32_t fx_dist(const fx_vec_t *v);
/* d3 is taken as a word (low 16 bits sign-extended), the case with byte 0x4A973 == 0; the
 * note's alternative (a long from 0x4ABF4 >> 8) is not reproduced: its register convention
 * is not characterised and a rig probe disagreed with the obvious reading. */
int32_t fx_distkey(const fx_cam_off_t *st, int32_t d2, int32_t d3, int32_t d4);

/* Object ordering (0x213AA): indices of keys sorted by key descending, ties ascending
 * index, negative keys dropped. Returns the number of indices written to out. */
int fx_sort(uint8_t *out, const int16_t *keys, int n);

/* Level-of-detail table (240 bytes, 0x20E4A) loaded from assets; fx_lod_shift(coarse)
 * returns table[min(coarse, 239)], or 0 (full detail) when no table is set. coarse is the
 * note's coarse key: the largest camera-relative offset magnitude, halved. Only this function needs it. */
void    fx_set_lod_table(const uint8_t *t, size_t n);
uint8_t fx_lod_shift(uint32_t coarse);

#endif
