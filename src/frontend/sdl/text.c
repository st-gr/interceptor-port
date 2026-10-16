/* src/frontend/sdl/text.c - the text engine's glyph drawing (docs/re/08-game-flow.md 3.2 and 3.4). */
#include "text.h"

int text_draw_line(raster_t *r, int row, int col, int pen, int shown, const uint8_t *text, int len, const uint8_t *font)
{
    int n = 0;
    if (!r || !text || !font) return 0;
    if (shown > len) shown = len;
    pen &= 15;   /* the engine writes the four planes 0 to 3: a pen is 0 to 15 */
    for (int j = 0; j < shown; j++) {
        int c = text[j];
        if (c < 0x20 || c > 0x5F) continue;
        const uint8_t *g = font + (c - 0x20) * TEXT_GLYPH_H;
        int x0 = 8 * col + TEXT_CELL * j;
        for (int gy = 0; gy < TEXT_GLYPH_H; gy++) {
            int y = row + gy;
            if (y < 0 || y >= RASTER_H) continue;
            for (int gx = 0; gx < TEXT_GLYPH_W; gx++) {
                int x = x0 + gx;
                if (!((g[gy] >> (7 - gx)) & 1) || x < 0 || x >= RASTER_W) continue;
                r->px[y * RASTER_W + x] = (uint8_t)pen;
                n++;
            }
        }
    }
    return n;
}

int text_draw(raster_t *r, const game_text_t *t, const uint8_t *font)
{
    int len = 0;
    if (!t) return 0;
    while (len < GAME_TEXT_LEN && t->text[len]) len++;
    if (len > t->length) len = t->length;
    return text_draw_line(r, t->row, t->col, t->pen, t->shown < len ? t->shown : len, (const uint8_t *)t->text, len, font);
}
