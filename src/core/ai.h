/* src/core/ai.h - the generic AI of the other aircraft, reproduced from docs/re/07-ai-and-weapons.md sections 3 and 4 ("note 07").
 *
 * Two layers, as in the original. The GATE (0x278F6, section 4) runs before the flight step of every AI record: the counter, the wreck rule, the speed control, the
 * waypoint rule, target handling, the threatened test and the evasion choice. The STATE ROUTINE (0x30A7E, section 3) runs inside the flight step, before the input
 * conversion: it turns the destination into the key byte +0x65 (and the rate targets of non-aircraft, the bank limit and its own flags). The training instructor
 * (record 4 in the modes 2 and 0x7D) has its own manoeuvre states in training.c; the generic routine here knows the states 0, 1, 3, 4, 6, 8, the evasions 2, 5, 7, 9 and
 * the training wait 0x0A.
 *
 * Core rules: C99, fixed-width integers, no stdio, no floating point, no wall time. */
#ifndef INTERCEPTOR_AI_H
#define INTERCEPTOR_AI_H
#include "entities.h"

/* The gate for the aircraft record idx (class nibble not 0, 0x20 or 0x30; the caller tests). */
void ai_gate(entities_t *e, int idx);
/* The state routine for record idx (not the player). Does nothing while the game is paused or frozen (V:4A866, V:4A867). */
void ai_state(entities_t *e, int idx);
/* The range routine 0x283E0 (4.2): recomputes +0x4A on its schedule. */
void ai_range(entities_t *e, int idx);
/* The per-record region check 0x2D53C (3.4). */
void ai_region_check(entities_t *e, int idx);
/* The launch decision 0x2701A for the aircraft in record `owner` (4.6). */
int ai_launch_decision(entities_t *e, int owner);
/* The steering vector helper shared with tests: the normaliser of note 04a P4 for a vector of 16-bit-word components, to length `len`. */
void ai_normalise(int32_t x, int32_t y, int32_t z, int len, int32_t out[3]);

#endif
