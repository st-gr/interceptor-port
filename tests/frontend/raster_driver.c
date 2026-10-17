/* Reads commands from stdin, drives raster.c, dumps the framebuffer as P6 PPM (grey = index*8). */
#include "../../src/frontend/sdl/raster.h"
#include "../../src/frontend/sdl/fade.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static raster_t fb;
static fade_t fd;

int main(void)
{
    char cmd[32];
    raster_clear(&fb, 0);
    fade_init(&fd);
    fb.row_limit = 179;
    while (scanf("%31s", cmd) == 1) {
        if (!strcmp(cmd, "clear")) {
            int c; if (scanf("%d", &c) != 1) return 2;
            raster_clear(&fb, (uint8_t)c);
        } else if (!strcmp(cmd, "limit")) {
            int n; if (scanf("%d", &n) != 1) return 2;
            raster_set_row_limit(&fb, n);
        } else if (!strcmp(cmd, "poly") || !strcmp(cmd, "line")) {
            char buf[4096]; int c, n = 0, v; int vals[256]; char *p, *e;
            if (scanf("%d", &c) != 1) return 2;
            if (!fgets(buf, sizeof buf, stdin)) return 2;
            for (p = buf; n < 256; p = e) {
                v = (int)strtol(p, &e, 10);
                if (e == p) break;
                vals[n++] = v;
            }
            if (cmd[0] == 'l') {
                if (n != 4) return 2;
                raster_line(&fb, vals[0], vals[1], vals[2], vals[3], (uint8_t)c);
            } else {
                int16_t xy[256]; int i;
                for (i = 0; i < n; i++) xy[i] = (int16_t)vals[i];
                raster_fill_polygon(&fb, xy, n / 2, (uint8_t)c);
            }
        } else if (!strcmp(cmd, "pply")) {   /* pply mask pen pattern toggle x y x y ... */
            char buf[4096]; int m, c, pat, tog, n = 0, v; int16_t xy[256]; char *p, *e;
            if (scanf("%d %d %d %d", &m, &c, &pat, &tog) != 4) return 2;
            if (!fgets(buf, sizeof buf, stdin)) return 2;
            for (p = buf; n < 256; p = e) {
                v = (int)strtol(p, &e, 10);
                if (e == p) break;
                xy[n++] = (int16_t)v;
            }
            raster_fill_polygon_planes(&fb, xy, n / 2, (unsigned)m, (unsigned)c, pat, (unsigned)tog);
        } else if (!strcmp(cmd, "linep")) {  /* linep mask pen x0 y0 x1 y1 */
            int m, c, a, b, d, e;
            if (scanf("%d %d %d %d %d %d", &m, &c, &a, &b, &d, &e) != 6) return 2;
            raster_line_planes(&fb, a, b, d, e, (unsigned)m, (unsigned)c);
        } else if (!strcmp(cmd, "sprite")) { /* sprite size mask pen x y */
            int z, m, c, x, y;
            if (scanf("%d %d %d %d %d", &z, &m, &c, &x, &y) != 5) return 2;
            raster_point_sprite(&fb, x, y, z, (unsigned)m, (unsigned)c);
        } else if (!strcmp(cmd, "gtri") || !strcmp(cmd, "falt")) {   /* gtri ALT x y x y x y: the near-ground pass's triangle; falt MASK PEN x y ...: the filler's alternate path */
            char buf[1024]; int alt = 0, n = 0, v, fm = 15, fp = 0; int16_t xy[64]; char *p, *e;
            if (cmd[0] == 'g' && scanf("%d", &alt) != 1) return 2;
            if (cmd[0] == 'f' && scanf("%d %d", &fm, &fp) != 2) return 2;
            if (!fgets(buf, sizeof buf, stdin)) return 2;
            for (p = buf; n < 64; p = e) {
                v = (int)strtol(p, &e, 10);
                if (e == p) break;
                xy[n++] = (int16_t)v;
            }
            if (cmd[0] == 'g') raster_ground_triangle(&fb, xy, alt); else raster_fill_alt(&fb, xy, n / 2, (unsigned)fm, (unsigned)fp);
        } else if (!strcmp(cmd, "rand")) {   /* rand SEED: every pixel a pseudo-random pen 0..15 (an LCG, the test reproduces it) */
            unsigned long long st; int i;
            if (scanf("%llu", &st) != 1) return 2;
            for (i = 0; i < RASTER_W * RASTER_H; i++) { st = (st * 1103515245ULL + 12345ULL) & 0xFFFFFFFFULL; fb.px[i] = (uint8_t)((st >> 16) & 15); }
        } else if (!strcmp(cmd, "below")) {   /* below SEED: the page's rows below the window (pseudo-random pens, the same LCG as rand with the seed xor 0x5A5A) */
            static uint8_t below[RASTER_W * RASTER_H]; unsigned long long st; int i;
            if (scanf("%llu", &st) != 1) return 2;
            st ^= 0x5A5A;
            for (i = 0; i < RASTER_W * RASTER_H; i++) { st = (st * 1103515245ULL + 12345ULL) & 0xFFFFFFFFULL; below[i] = (uint8_t)((st >> 16) & 15); }
            fb.page_below = below;
        } else if (!strcmp(cmd, "pen")) {   /* pen x y c: sets one pixel */
            int x, y, c;
            if (scanf("%d %d %d", &x, &y, &c) != 3) return 2;
            fb.px[y * RASTER_W + x] = (uint8_t)c;
        } else if (!strcmp(cmd, "disc")) {   /* disc R mask pen x y */
            int R, m, c, x, y;
            if (scanf("%d %d %d %d %d", &R, &m, &c, &x, &y) != 5) return 2;
            raster_disc(&fb, x, y, R, (unsigned)m, (unsigned)c);
        } else if (!strcmp(cmd, "ground")) {   /* ground pen: the window base (rows 1..limit) */
            int c;
            if (scanf("%d", &c) != 1) return 2;
            raster_window_base(&fb, (uint8_t)c);
        } else if (!strcmp(cmd, "skyfill")) {  /* skyfill sea tiny_pen tiny_mask x y x y ...: the sky polygon filler; prints its return value */
            char buf[4096]; int sea, tp, tm, n = 0, v; int16_t xy[64]; char *p, *e;
            if (scanf("%d %d %d", &sea, &tp, &tm) != 3) return 2;
            if (!fgets(buf, sizeof buf, stdin)) return 2;
            for (p = buf; n < 64; p = e) {
                v = (int)strtol(p, &e, 10);
                if (e == p) break;
                xy[n++] = (int16_t)v;
            }
            printf("skyfill %d\n", raster_sky_fill(&fb, xy, n / 2, sea, (unsigned)tp, (unsigned)tm));
        } else if (!strcmp(cmd, "ball")) {  /* ball x y x y ...: the filler's ball entry 0x34CD0 (limit A = 199); prints its return value, the general fill's pixels as pen 5 */
            char buf[4096]; int n = 0, v, y, x, ret; int16_t xy[64]; char *p, *e; static uint8_t bits[RASTER_W * RASTER_H];
            if (!fgets(buf, sizeof buf, stdin)) return 2;
            for (p = buf; n < 64; p = e) {
                v = (int)strtol(p, &e, 10);
                if (e == p) break;
                xy[n++] = (int16_t)v;
            }
            ret = raster_ball_fill(&fb, xy, n / 2, bits);
            if (ret == 0) for (y = 0; y < RASTER_H; y++) for (x = 0; x < RASTER_W; x++) if (bits[y * RASTER_W + x]) fb.px[y * RASTER_W + x] = 5;
            printf("ball %d\n", ret);
        } else if (!strcmp(cmd, "ball")) {  /* ball x y x y ...: the filler's ball entry 0x34CD0 (limit A = 199); prints its return value, the general fill's pixels as pen 5 */
            char buf[4096]; int n = 0, v, y, x, ret; int16_t xy[64]; char *p, *e; static uint8_t bits[RASTER_W * RASTER_H];
            if (!fgets(buf, sizeof buf, stdin)) return 2;
            for (p = buf; n < 64; p = e) {
                v = (int)strtol(p, &e, 10);
                if (e == p) break;
                xy[n++] = (int16_t)v;
            }
            ret = raster_ball_fill(&fb, xy, n / 2, bits);
            if (ret == 0) for (y = 0; y < RASTER_H; y++) for (x = 0; x < RASTER_W; x++) if (bits[y * RASTER_W + x]) fb.px[y * RASTER_W + x] = 5;
            printf("ball %d\n", ret);
        } else if (!strcmp(cmd, "fade")) {     /* fade TARGET BLANKS: advances the fade level by that many blanks, printing the level after each */
            int t, nb, i;
            if (scanf("%d %d", &t, &nb) != 2) return 2;
            for (i = 0; i < nb; i++) { fade_blank(&fd, t); printf("%d%c", fd.level, i + 1 < nb ? ' ' : '\n'); }
        } else if (!strcmp(cmd, "fadecnt")) {  /* fadecnt N: sets the counter V:4A95B (the snapshot holds 2); fadelevel L sets the level */
            int n; if (scanf("%d", &n) != 1) return 2;
            fd.counter = (int8_t)n;
        } else if (!strcmp(cmd, "fadelevel")) {
            int n; if (scanf("%d", &n) != 1) return 2;
            fd.level = n;
        } else if (!strcmp(cmd, "fadepal")) {  /* fadepal STEP FILE: the 16 colours of a step of the 768-byte fade table file as r g b lines */
            int step, i; char path[1024]; uint8_t tab[768]; uint32_t out[16]; FILE *f;
            if (scanf("%d %1023s", &step, path) != 2) return 2;
            f = fopen(path, "rb"); if (!f) return 3;
            if (fread(tab, 1, sizeof tab, f) != sizeof tab) { fclose(f); return 3; }
            fclose(f);
            fade_palette(tab, step, out);
            for (i = 0; i < 16; i++) printf("%d %d %d\n", (int)(out[i] >> 16) & 255, (int)(out[i] >> 8) & 255, (int)out[i] & 255);
        } else if (!strcmp(cmd, "dump")) {
            char path[1024]; FILE *f; int i;
            if (scanf("%1023s", path) != 1) return 2;
            f = fopen(path, "wb"); if (!f) return 3;
            fprintf(f, "P6\n%d %d\n255\n", RASTER_W, RASTER_H);
            for (i = 0; i < RASTER_W * RASTER_H; i++) {
                int g = fb.px[i] * 8; if (g > 255) g = 255;
                fputc(g, f); fputc(g, f); fputc(g, f);
            }
            fclose(f);
        } else return 2;
    }
    return 0;
}
