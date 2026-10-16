/* src/frontend/sdl/text.h - the front end's text engine as a pixel drawer (docs/re/08-game-flow.md sections 3.2, 3.4, 3.5).
 * The original draws every text screen itself: 5 by 7 glyphs of hunk 61 (assets/fonts/menu5x7.bin, 64 glyphs x 7 bytes for the characters 0x20 to
 * 0x5F, the pixels in bits 7 to 3), one glyph per 7-pixel cell, a record addressed by scanline (row) and byte column (x = 8 x col). A glyph pixel sets
 * the screen pixel to the line's pen, a clear bit leaves the pixel as it is (no cell is ever filled). */
#ifndef SDL_TEXT_H_
#define SDL_TEXT_H_
#include <stdint.h>
#include "raster.h"

#define TEXT_CELL 7        /* pixels from one character cell to the next (3.2) */
#define TEXT_GLYPH_W 5
#define TEXT_GLYPH_H 7

/* One line of text: its first glyph row at scanline `row`, its first cell at x = 8 * col; the first `shown` of the `len` characters are drawn (shown is
 * clamped to len); pixel k of character j goes to x = 8 * col + 7 j + k. Characters outside 0x20..0x5F draw nothing (the data holds none). The glyph halves
 * 0x24, 0x5E and 0x26 (the trademark and copyright signs of the title lines, 2.2 quirk 4) are ordinary characters of the font and are drawn like any other,
 * each in its own cell. The engine does not clip (3.4 rule 6): its long-word write of the last cell may reach byte 41 of the scanline, the first bytes of the
 * next one, but writes them back unchanged (2.3), so a pixel beyond x = 319 is simply not drawn; rows at scanline 200 and below would be written past the
 * plane by the engine and are not drawn here (a port choice, 3.4 rule 6: the shipped data never does it). Returns the number of pixels written. */
int text_draw_line(raster_t *r, int row, int col, int pen, int shown, const uint8_t *text, int len, const uint8_t *font);

/* One line of the scene (game_text_t, scene.h): the core has placed it (row includes the line steps, `step` is informational), so this is
 * text_draw_line at (t->row, t->col) in t->pen with the first t->shown characters; shown and length are clamped to the string. A line with shown 0 draws
 * nothing. Returns the pixels written. */
int text_draw(raster_t *r, const game_text_t *t, const uint8_t *font);
#endif
