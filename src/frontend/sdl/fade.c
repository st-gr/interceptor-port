#include "fade.h"

const uint8_t FADE_CURVE[16] = { 0, 0, 0, 28, 43, 56, 72, 89, 106, 123, 141, 159, 177, 197, 216, 255 };

void fade_init(fade_t *f) { f->level = 15; f->counter = 0; }

int fade_blank(fade_t *f, int target)
{
    if (f->level == target) return 0;
    f->counter = (int8_t)(f->counter - 1);   /* a byte decrement and test */
    if (f->counter >= 0) return 0;
    if (target < f->level) { f->level--; f->counter = 1; }
    else { f->level++; f->counter = 2; }
    return 1;
}

int fade_step(const fade_t *f) { return 15 - f->level; }

void fade_palette(const uint8_t *fade_pal, int step, uint32_t out[16])
{
    int i;
    if (step < 0) step = 0;
    if (step > 15) step = 15;
    for (i = 0; i < 16; i++) {
        const uint8_t *p = fade_pal + (step * 16 + i) * 3;
        out[i] = (uint32_t)FADE_CURVE[(p[0] / 17) & 15] << 16 | (uint32_t)FADE_CURVE[(p[1] / 17) & 15] << 8 | FADE_CURVE[(p[2] / 17) & 15];
    }
}
