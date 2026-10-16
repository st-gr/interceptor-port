/* text_driver.c - drives text.c and logfile.c without a window (tests/frontend/test_text.py).
 * usage: text_driver FONT.bin [ASSETS_DIR] < script        (FONT.bin: 448 bytes, 64 glyphs x 7 rows, a test's invented font or the extracted one)
 * Script lines:
 *   clear PEN                       fill the 320x200 pen buffer with PEN
 *   line ROW COL PEN SHOWN HEX      text_draw_line of the characters given as hex bytes (HEX may be "-" for none); prints "drawn N"
 *   scene ROW COL PEN SHOWN LENGTH HEX   text_draw of a game_text_t (scene.h) holding the hex bytes as its NUL-terminated text; prints "drawn N"
 *   rec ID STEP                     with ASSETS_DIR: every line of the extracted text record ID, line k at scanline row + STEP * k, in the colour's pen
 *   dump                            writes the 64,000 pen bytes to stdout (binary) after a line "PENS 64000\n"
 *   save PATH HEX / load PATH CAP   logfile_save / logfile_load, prints "save RC" / "load RC HEX"
 *   defpath                         prints logfile_default_path
 * Game text never appears in the repository: records come from the extracted assets at run time. */
#include "../../src/frontend/sdl/text.h"
#include "../../src/frontend/sdl/logfile.h"
#include "../../src/frontend/sdl/assets.h"
#include "../../src/core/assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static raster_t R;
static uint8_t font[MENU_FONT_BYTES];

static int unhex(const char *s, uint8_t *out, int cap) {
    int n = 0;
    if (!strcmp(s, "-")) return 0;
    while (s[0] && s[1] && s[0] != '\n' && n < cap) {
        unsigned v;
        if (sscanf(s, "%2x", &v) != 1) break;
        out[n++] = (uint8_t)v; s += 2;
    }
    return n;
}

int main(int argc, char **argv) {
    char line[4096], err[256] = "";
    static uint8_t buf[8192];
    game_loader_t loader; int have_assets = 0;
    static text_t T;
    FILE *ff = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!ff || fread(font, 1, sizeof font, ff) != sizeof font) { fprintf(stderr, "font\n"); return 2; }
    fclose(ff);
    if (argc > 2) {
        if (assets_open(argv[2], &loader, err, sizeof err) != 0 || assets_load_text(&loader, &T, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); return 2; }
        have_assets = 1;
    }
    while (fgets(line, sizeof line, stdin)) {
        char cmd[16] = "", a[3000] = "";
        int p1, p2, p3, p4, p5;
        if (sscanf(line, "%15s", cmd) != 1) continue;
        if (!strcmp(cmd, "clear") && sscanf(line, "%*s %d", &p1) == 1) memset(R.px, p1, sizeof R.px);
        else if (!strcmp(cmd, "line") && sscanf(line, "%*s %d %d %d %d %2999s", &p1, &p2, &p3, &p4, a) == 5) {
            int n = unhex(a, buf, sizeof buf);
            printf("drawn %d\n", text_draw_line(&R, p1, p2, p3, p4, buf, n, font));
        }
        else if (!strcmp(cmd, "scene") && sscanf(line, "%*s %d %d %d %d %d %2999s", &p1, &p2, &p3, &p4, &p5, a) == 6) {
            game_text_t t;
            memset(&t, 0, sizeof t);
            int n = unhex(a, buf, GAME_TEXT_LEN - 1);
            memcpy(t.text, buf, (size_t)n);
            t.row = (uint8_t)p1; t.col = (uint8_t)p2; t.pen = (uint8_t)p3; t.shown = (uint16_t)p4; t.length = (uint16_t)p5;
            printf("drawn %d\n", text_draw(&R, &t, font));
        }
        else if (!strcmp(cmd, "rec") && have_assets && sscanf(line, "%*s %d %d", &p1, &p2) == 2) {
            if (p1 < 1 || p1 > T.id_count) { printf("rec bad\n"); continue; }
            const text_id_t *id = &T.id[p1 - 1];
            for (int k = 0; k < id->line_count; k++) {
                const text_line_t *l = &T.line[id->first_line + k];
                text_draw_line(&R, l->row + p2 * k, l->col, l->colour >> 4, l->length, T.text + l->text, l->length, font);
            }
        }
        else if (!strcmp(cmd, "dump")) { printf("PENS %d\n", RASTER_W * RASTER_H); fwrite(R.px, 1, sizeof R.px, stdout); }
        else if (!strcmp(cmd, "save") && sscanf(line, "%*s %2999s", a) == 1) {
            char hex[3000] = ""; sscanf(line, "%*s %*s %2999s", hex);
            int n = unhex(hex, buf, sizeof buf);
            printf("save %d\n", logfile_save(a, buf, (size_t)n));
        }
        else if (!strcmp(cmd, "load") && sscanf(line, "%*s %2999s %d", a, &p1) == 2) {
            int rc = logfile_load(a, buf, (size_t)p1);
            printf("load %d ", rc);
            for (int i = 0; i < (rc > 0 ? rc : 0); i++) printf("%02x", buf[i]);
            printf("\n");
        }
        else if (!strcmp(cmd, "defpath")) { char p[1100]; printf("defpath %d %s\n", logfile_default_path(p, sizeof p), p); }
        fflush(stdout);
    }
    return 0;
}
