/* src/core/flight_int.h - shared by flight.c and flight_coll.c; not part of the core's interface. */
#ifndef INTERCEPTOR_FLIGHT_INT_H
#define INTERCEPTOR_FLIGHT_INT_H
#include "flight.h"

/* word helpers: the note's arithmetic is 16-bit with wrap, right shifts of negatives are arithmetic */
static inline int16_t w16(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v & 0xFFFFu); }
/* 32-bit arithmetic wraps as the 68000's does (no signed overflow in C): neg.l of INT32_MIN leaves INT32_MIN, which stays negative */
static inline int32_t fl_add(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static inline int32_t fl_sub(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
static inline int32_t fl_shl(int32_t a, int n) { return (int32_t)((uint32_t)a << n); }
static inline int32_t iabs32(int32_t v) { return v < 0 ? (int32_t)(0u - (uint32_t)v) : v; }
/* the gimbal guard's difference (note 05 §17.9): a - b as a word, negated as a word when a < b (true signed compare); a wrapped result stays negative */
static inline int gg_diff(int a, int b) { int16_t x = (int16_t)a, y = (int16_t)b; int16_t d = w16(x - y); return x < y ? w16(-d) : d; }

/* Counters (flight_state_t.reads, FL_COUNT_* / FL_GUARD_* of flight.h). */
static inline void fl_count(flight_state_t *f, int kind) { if (f && kind >= 0 && kind < FL_GUARD_KINDS) f->reads[kind]++; }

/* The word at a flat image address as the original's table lookups see it (note 05 17.9): from the two extracted spans; the live variable block
 * 0x4A6E8..0x4AE92 from the port's own variable where it keeps that word (FL_COUNT_VARIABLE), else 0 and FL_COUNT_RUNTIME; 0x49A12..0x4A6E8 (display data
 * written at run time) 0 and FL_COUNT_RUNTIME; any other address outside the spans 0 and FL_COUNT_IMAGE. */
int16_t fl_image_word(flight_state_t *f, const flight_tables_t *t, uint32_t addr);

/* the sine table word at index i (tenths of a degree, 0..900; Q14): address 0x436E8 + 2 i, so an index below 0 reads the inverse-sine tables in front of the
 * table and an index beyond 900 the words after it (note 05, "Gust and sink", 17.9) */
int16_t fl_sine(flight_state_t *f, const flight_tables_t *t, int i);

/* collision routine 0x2B5E2 (sections 9 and 11b): returns 0 (no result) or 0x20 (hull or landmark hit).
 * pos is the current position, prev the position before this step's displacement. */
int fl_collide(flight_state_t *f, const flight_env_t *env, const int32_t prev[3]);
int fl_bighull_hit(const flight_env_t *env, const flight_ship_t *ship, const int32_t P[3]);   /* the gun's big-hull test 0x2B3F0 (note 07 8.2) */
int fl_ship_contact(flight_state_t *f, const flight_env_t *env, const flight_ship_t *ship, const int32_t prev[3]);   /* one ship record: the box, the proximity flag, the deck and hull tests (0x20 on a hit) */

/* the terrain query of flight_coll.c with its counters in f */
int32_t fl_terrain_query(flight_state_t *f, const flight_tables_t *t, int32_t x, int32_t z, uint8_t *b4);

#endif
