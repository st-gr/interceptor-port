/* src/frontend/sdl/assets.h */
#ifndef INTERCEPTOR_SDL_ASSETS_H
#define INTERCEPTOR_SDL_ASSETS_H
#include <stddef.h>
#include <stdint.h>
#include "../../core/game.h"
int  assets_open(const char *dir, game_loader_t *out, char *err, size_t errlen);
void assets_close(game_loader_t *loader);

/* fonts/menu5x7.bin: 64 glyphs (characters 0x20 to 0x5F) x 7 row bytes, bits 7 to 3 (note 08 section 3.2). Returns 0, or -1 with
 * a message naming the file in err when it is missing or not exactly MENU_FONT_BYTES long. */
#define MENU_FONT_BYTES 448
int  assets_load_menu_font(const game_loader_t *loader, uint8_t out[MENU_FONT_BYTES], char *err, size_t errlen);

/* sounds/<name>.raw by the index of sounds/index.txt (one name per line; note 10 2.2): the signed 8-bit sample of sound file `file`. On success *data is a malloc'ed buffer (free it) and *len its
 * length in bytes; -1 when index.txt is missing, has no such line or the file cannot be read (*data NULL, *len 0). */
int  assets_load_sample(const game_loader_t *loader, int file, int8_t **data, uint32_t *len);
#endif
