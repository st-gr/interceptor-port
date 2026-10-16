/* The stand-in for the draw pass of the player's record, for the tests and rigs that run flight_step without the display lists: the model's vertices 0..3 rotated by
 * matrix B >> 6, slot k = (M[row] . v_k) >> 8 (note 05 section 9). The game takes the slots from the draw store (lists vslot[0], copied by game.c after the walk). */
#ifndef PLAYER_DRAW_H
#define PLAYER_DRAW_H
#include <stdint.h>
#include "../src/core/flight.h"

static inline void player_draw_standin(flight_state_t *f, const int16_t mv[4][3]) {
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 3; i++) {
            uint32_t s = 0;
            for (int j = 0; j < 3; j++) s += (uint32_t)((int32_t)(f->mat_b[3 * i + j] >> 6) * mv[k][j]);
            f->vtx[k][i] = (int16_t)((int32_t)s >> 8);
        }
}
#endif
