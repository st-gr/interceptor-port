/* src/frontend/sdl/fade.h - the palette fade of the flight (docs/re/04-scene-composition.md, "Palette in flight"). The core publishes the target
 * (game_sky_t.fade_target: V:4A959); the level V:4A958 and its counter V:4A95B are display state, advanced here once per 50 Hz vertical blank; the 16 colour
 * registers are fade step 15 - level of palette/fade.pal, converted with the emulator's measured colour curve. */
#ifndef SDL_FADE_H_
#define SDL_FADE_H_
#include <stdint.h>

typedef struct {
    int level;       /* V:4A958, 0..15; 15 is the normal palette (step 0) */
    int8_t counter;  /* V:4A95B: a signed byte, 0 in the image (the airborne snapshot holds 2), counted down only while level != target, re-armed 1 (down) / 2 (up) */
} fade_t;

void fade_init(fade_t *f);
/* One blank (the vertical-blank server, note 04 "Palette in flight"): while the level differs from the target the counter (a signed byte) is decremented; when the
 * result is negative the level moves one toward the target and the counter is re-armed with 1 (down) or 2 (up) in the same blank. With the levels equal the
 * counter is not touched (it stands at 1 after a descent, 2 after an ascent). So the first step after a target change comes counter + 1 blanks after the blank
 * in which the target is first seen (counter 0: that blank; 2: the third). Returns 1 when the level changed (the palette is reloaded). */
int fade_blank(fade_t *f, int target);
/* The fade step loaded for the level: 15 - level. */
int fade_step(const fade_t *f);

/* The measured colour curve of the reference emulator, 4-bit level to 8-bit channel (note 06, "Pen 3 and the emulator's colour curve"; note 04). Measured:
 * 0 and 1 and 2 to 0, 3 to 28, 4 to 43, 5 to 56, 6 to 72, 7 to 89, 8 to 106, 9 to 123, 13 to 197. NOTE-GAP: 10, 11 and 12 (they occur in the fade table only in
 * entry 9, the sun, at fade steps 0 to 3) are not measured (the sun was not in a captured fade frame) and take the harness table's 141, 159 and 177; 14 and 15
 * never occur in the fade table (15 is 236 on the crash flash frame, 255 is kept for the pens). */
extern const uint8_t FADE_CURVE[16];

/* Entries 0 to 15 of fade step `step` (0..15) of the 768-byte table (16 steps x 16 colours x r, g, b, each the nibble times 17) as 0xRRGGBB through the curve. */
void fade_palette(const uint8_t *fade_pal, int step, uint32_t out[16]);
#endif
