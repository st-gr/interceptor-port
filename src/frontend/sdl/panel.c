/* src/frontend/sdl/panel.c - instrument panel, radar, warning line, external strip, map overlay.
 * Rules and coordinates: docs/re/06-cockpit-hud.md. Drawing order follows the note's draw schedule. */
#include "panel.h"
#include "raster.h"
#include "../../core/world.h"
#include <stdlib.h>
#include <string.h>

/* offsets inside masks.bin (the note's addresses minus 0x42AAC) */
#define MASK_GEAR_SYMBOLS 0x1C
#define MASK_BALL 0x8C
#define MASK_COMPASS 0x100

static int asr(int v, int n) { return v >> n; }

static void fill(hud_t *h, int x0, int x1, int y0, int y1, int pen) {
    for (int y = y0; y <= y1; y++) hud_hline(h, x0, x1, y, pen);
}

/* A lit lamp: every pixel of the box gets its pen ORed with 1 (0 to 1, 4 to 5, 8 to 9). Pixels of the
 * box that are not part of the picture (a panel moved away by a head view) are left alone. */
static void lamp(hud_t *h, int x0, int x1, int y0, int y1) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int p = hud_get(h, x, y);
            if (p != HUD_NONE) hud_plot(h, x, y, p | 1);
        }
}

/* ---- panel picture, cowl, radar ---- */

/* Radar scope (0x35CF2, "Radar scope (0x35CF2) and left display"): the scope as the original's bitmap keeps it between walks (hud.c scope_update: the own-ship plus, the cone lines and the blips of the
 * core's walks with their erases). */
static void draw_radar(hud_t *h) {
    for (int i = 0; i < HUD_W * HUD_H; i++) if (h->scope[i] != HUD_NONE) h->px[i] = h->scope[i];
}

void panel_draw_cockpit(hud_t *h) {
    if (h->have_panel) {
        for (int y = 0; y < 55; y++)
            for (int x = 0; x < 320; x++) hud_plot(h, x, 145 + y, h->inst[y * 320 + x]);
        for (int y = 0; y < 12; y++)
            for (int x = 0; x < 288; x++)
                if (h->cowl[y * 288 + x]) hud_plot(h, 16 + x, 133 + y, h->cowl[y * 288 + x]);   /* pen 0 is transparent */
    }
    draw_radar(h);
}

/* ---- attitude ball (0x34B1A) ---- */

/* The sky polygon of the 3D window (clipped vertices in 320x180 window coordinates, 0x504DE) scaled into the ball (0x34C7C): x' = (x * 24 >> 8)
 * + 193, y' = (y * 31 >> 8) + 162, then the blitter's area fill: each edge contributes one pixel per row from the row below its upper end to its
 * lower end, horizontal edges none, and the fill covers the span between the edge pixels of a row inclusive; clipped to the ball mask.
 * The polygon is the core's (scene.sky.ball_*: the sky routine on the aircraft's attitude matrix without the zoom row scale, note 04) and the fill is the one of
 * the window's sky (raster.h, raster_blit_cover: the blitter line of note 04 and the inclusive fill): the nine captured att-* balls are reproduced pixel for pixel
 * (tests/frontend/test_hud.py). Without a core polygon (a scene with no sky block) the half-plane stand-in raster_sky_polygon is used.
 * A ball polygon in the filler's tiny cases (a box of at most two rows or one column) draws straight into the page outside the ellipse mask: the ball calls the filler's second entry
 * (0x34CD0, raster_ball_fill) with A = 199, the stale colour 3 and mask 0xF, so dots and blocks of pen 3 on all four planes, and the lines cut at the window's last row V:4AA3C
 * (note 04, "The attitude ball's tiny cases"). */
#define BALL_Y0 162
static void draw_ball(hud_t *h, const game_scene_t *scene) {
    if (!h->have_cockpit) return;
    const uint8_t *mask = h->masks + MASK_BALL;
    int16_t pv[16], sv[16];
    int n;
    static uint8_t bits[RASTER_W * RASTER_H];
    static raster_t tiny;   /* the tiny cases' pixels (pen 3 on a zero base) */
    if (scene->sky.valid) {   /* the core's polygon: the routine 0x32E52 on the aircraft's attitude (note 04), the exact clipped vertices */
        n = scene->sky.ball_n;
        for (int i = 0; i < 2 * n; i++) pv[i] = scene->sky.ball_xy[i];
    } else n = raster_sky_polygon(scene->camera.pitch, -scene->camera.roll, pv);   /* no core polygon: the half-plane stand-in (the ball is right wing down positive: the scene carries the roll word) */
    for (int i = 0; i < n; i++) { sv[2 * i] = (int16_t)(((pv[2 * i] * 24) >> 8) + 193); sv[2 * i + 1] = (int16_t)(((pv[2 * i + 1] * 31) >> 8) + BALL_Y0); }
    memset(tiny.px, 0, sizeof tiny.px);
    tiny.row_limit = hud_window_last_row(&scene->camera);   /* V:4AA3C */
    tiny.page_below = NULL;
    int general = n >= 3 && raster_ball_fill(&tiny, sv, n, bits) == 0;   /* the filler's ball entry: the blitter line and the inclusive fill (raster.h), the tiny cases in tiny */
    for (int r = 0; r < 21; r++) {
        int y = 163 + r;
        for (int c = 0; c < 32; c++) {
            if (!((mask[4 * r + c / 8] >> (7 - c % 8)) & 1)) continue;
            int x = 192 + c;
            hud_plot(h, x, y, general && bits[y * RASTER_W + x] ? 5 : 1);   /* pen 1 (red) ellipse, pen 5 where the polygon is */
        }
    }
    if (n >= 3 && !general)   /* the filler's tiny cases: pen 3 on all four planes, outside the ellipse mask */
        for (int y = 0; y < RASTER_H; y++)
            for (int x = 0; x < RASTER_W; x++)
                if (tiny.px[y * RASTER_W + x] == 3) hud_plot(h, x, y, 3);
    hud_hline(h, 204, 214, 173, 2);                    /* the fixed aircraft symbol */
    hud_plot(h, 209, 171, 2); hud_plot(h, 209, 172, 2);
    static const int dots[8][2] = { {206,165},{204,166},{203,167},{201,170},{218,171},{218,172},{218,173},{217,175} };
    for (int i = 0; i < 8; i++) hud_plot(h, dots[i][0], dots[i][1], 13);
}

/* ---- compass (0x35A4C) ---- */

static void draw_compass(hud_t *h, int heading80) {
    if (!h->have_cockpit) return;
    int d = heading80 / 80;
    int scroll = asr((d / 2) * 135, 8) - 4;
    if (scroll < 0) scroll += 96;
    const uint8_t *strip = h->masks + MASK_COMPASS;
    for (int j = 0; j < 32; j++) {
        int col = (j + scroll / 2) % 96;
        for (int r = 0; r < 7; r++) {
            int bit = (strip[r * 12 + col / 8] >> (7 - col % 8)) & 1;
            hud_plot(h, 192 + j, 151 + r, bit ? 2 : 3);
        }
    }
    for (int r = 0; r < 7; r++) hud_plot(h, 208, 151 + r, 1);   /* the red index */
}

/* ---- weapon stations and left display ---- */

static void restroke(hud_t *h, int sx, int sy, int pen) {       /* box (x - 1, y + 1), 3 wide and 6 tall */
    for (int y = sy + 1; y <= sy + 6; y++)
        for (int x = sx - 1; x <= sx + 1; x++)
            if (hud_get(h, x, y) == 4) hud_plot(h, x, y, pen);
}

static void draw_stations(hud_t *h, const game_hud_t *s) {
    static const int sw[2][2] = { {85,172}, {51,172} };
    static const int am[4][2] = { {74,159}, {62,159}, {81,167}, {55,167} };
    int sel = s->cp_sel >> 4;
    int stock_sw = s->cp_stock >> 4, stock_am = s->cp_stock & 15;
    for (int i = 0; i < 2; i++) if (i >= stock_sw) restroke(h, sw[i][0], sw[i][1], 0);
    for (int i = 0; i < 4; i++) if (i >= stock_am) restroke(h, am[i][0], am[i][1], 0);
    if (sel == 3 && stock_am > 0) restroke(h, am[stock_am > 4 ? 3 : stock_am - 1][0], am[stock_am > 4 ? 3 : stock_am - 1][1], 1);
    else if (sel != 0 && sel != 1 && stock_sw > 0) restroke(h, sw[stock_sw > 2 ? 1 : stock_sw - 1][0], sw[stock_sw > 2 ? 1 : stock_sw - 1][1], 1);
    if (sel == 1) { hud_plot(h, 68, 163, 1); hud_plot(h, 68, 164, 1); }       /* gun selected */
    else { hud_plot(h, 68, 163, 0); hud_plot(h, 68, 164, 4); }
}

static void draw_left_text(hud_t *h, const game_hud_t *s) {
    int sel = s->cp_sel >> 4;
    if (!sel) return;
    static const int x3[3] = { 61, 65, 69 }, x2[2] = { 65, 69 };
    hud_text(h, "ARM", x3, 176, 4, -1);
    if (sel == 1) hud_text(h, "GUN", x3, 184, 4, -1);
    else if (sel == 3) hud_text(h, "AM", x2, 184, 4, -1);
    else hud_text(h, "SW", x2, 184, 4, -1);
}

/* ---- the message line (0x36DB2, 0x3706A) ---- */

static void draw_message(hud_t *h) {
    if (!h->have_cockpit) return;
    const game_hud_t *s = &h->st;
    if (s->tline_mode == 1) return;   /* the blank message: the line is empty (note 07 12.8: the target is gone, or a page was asked for with no target) */
    if (s->tline_mode == 2) {
        /* The target data (0x36DB2, note 07 12.8): 26 characters at x = 108 + 4 i. The name of the target's type at character 4 (by the class; the text of an undetected target when the detected bit is
         * clear), the page label at character 13 (altitude, heading, speed) and the digits of the page from the character column tline_col (altitude 18, heading 19, speed 18): the pen of the
         * colour bits (note 07 12.8): bits 10 and 11 pen 1, bits 01 pen 4, bits 00 pen 8 (the "no signal" text). The strings are tables/target_text.bin (0x3717C to 0x371DE); a space is not drawn. */
        const int pen = (s->tline_colour & 2) ? 1 : s->tline_colour == 1 ? 4 : 8;   /* 0x3706A: the plane minterms of the colour bits give pens 1, 4 and 8 */
        if (h->have_ttext) {
            int name = -1;
            if (!s->tline_detected) name = 5;
            else switch (s->tline_cls) {
            case 0x12: name = 0; break;
            case 0x13: name = 1; break;
            case 0x14: name = 2; break;
            case 0x10: name = 3; break;
            case 0x15: name = 4; break;
            case 0x16: name = 6; break;
            case 0x17: name = 7; break;
            default: break;
            }
            int xs[TARGET_NAME_CHARS + 1];
            if (name >= 0) {
                char buf[TARGET_NAME_CHARS + 1];
                memcpy(buf, h->ttext.name[name], TARGET_NAME_CHARS); buf[TARGET_NAME_CHARS] = 0;
                for (int i = 0; i < TARGET_NAME_CHARS; i++) xs[i] = 108 + 4 * (4 + i);
                hud_text(h, buf, xs, 192, pen, -1);
            }
            if (s->tline_page >= 1 && s->tline_page <= TARGET_LABELS) {
                char buf[TARGET_LABEL_CHARS + 1];
                memcpy(buf, h->ttext.label[s->tline_page - 1], TARGET_LABEL_CHARS); buf[TARGET_LABEL_CHARS] = 0;
                for (int i = 0; i < TARGET_LABEL_CHARS; i++) xs[i] = 108 + 4 * (13 + i);
                hud_text(h, buf, xs, 192, pen, -1);
            }
        }
        for (int i = 0; s->tline_digits[i] && i < 7; i++) hud_glyph(h, s->tline_digits[i], 108 + 4 * (s->tline_col + i), 192, pen, -1);
        return;
    }
    int e = h->st.cp_msg_entry; if (e >= 54) e = 0;       /* the entry the core's arbiter shows (no lag, note 06 "Text versus code word") */
    const uint8_t *w = h->warn[e];
    int cls = w[0] >> 6;
    int pen = cls == 2 ? 1 : 4;
    for (int i = 0; i < 26; i++) hud_glyph(h, w[1 + i], 108 + 4 * i, 192, pen, -1);
}

/* ---- all instruments over the picture ---- */

void panel_draw_instruments(hud_t *h, const game_scene_t *scene) {
    const game_hud_t *s = &h->st;
    uint32_t n = h->tick;
    (void)scene;
    /* threat and stall lamps, 4x2 each (0x35BF6) */
    fill(h, 292, 295, 156, 157, (s->cp_threat & 4) ? 1 : 3);
    fill(h, 297, 300, 156, 157, (s->cp_threat & 2) ? 4 : 3);
    fill(h, 297, 300, 159, 160, (s->cp_threat & 16) ? 2 : 3);
    fill(h, 292, 295, 159, 160, (s->cp_threat & 32) ? 2 : 3);
    fill(h, 302, 305, 159, 160, ((s->cp_b20 & 4) && (n & 2)) ? 1 : 3);
    draw_compass(h, s->cp_heading80);
    /* digits: glyph pen 4 on box pen 0 */
    static const int alt_x[6] = { 240, 244, 248, 254, 258, 262 }, spd_x[4] = { 242, 248, 252, 256 },
                     thr_x[3] = { 240, 244, 248 }, mag_x[2] = { 242, 248 }, rng_x[2] = { 148, 152 },
                     fuel_x[5] = { 104, 108, 112, 116, 120 }, lat_x[3] = { 295, 299, 305 }, lon_x[4] = { 292, 295, 299, 305 };
    hud_number(h, h->alt_shown, alt_x, 6, 158, 4, 0);
    hud_number(h, h->speed_shown, spd_x, 4, 166, 4, 0);
    hud_number(h, h->throttle_shown, thr_x, 3, 175, 4, 0);
    hud_text(h, s->cp_zoom == 0x80 ? "10" : s->cp_zoom == 0x40 ? "20" : "40", mag_x, 184, 4, 0);
    int rl = s->cp_sel & 15;
    hud_number(h, rl == 9 ? 2 : rl == 11 ? 10 : 40, rng_x, 2, 184, 4, 0);
    hud_number(h, h->fuel_shown, fuel_x, 5, 183, 0, 8);
    /* fuel bar (0x353EC): 22 rows from row 180 upward, lit rows = min(22, max(0, level - 1)), level = (V:4A9AE & 0x7FFF) >> 10 of the drawn
     * digits. The routine draws nothing when its first plot x (112 plus the view offset) is at most 0 or above 316: in head codes 1 and 2 the
     * clean picture's own bar (19 lit rows) shows. */
    if (h->off + 112 > 0 && h->off + 112 <= 316) {
        int level = h->fuel_bar_level;
        int lit = level - 1 < 0 ? 0 : level - 1;
        if (lit > 22) lit = 22;
        for (int y = 159; y <= 180; y++) {
            int pen = (180 - y) < lit ? 4 : 0;
            hud_plot(h, 111, y, pen); hud_plot(h, 112, y, pen); hud_plot(h, 113, y, pen); hud_plot(h, 114, y, pen);
        }
    }
    /* latitude and longitude, glyph pen 0 on pen 5 */
    int lat = 375 + (int)((((int64_t)s->cp_lat - 0x10000000) >> 8) / 0x7000);
    int lon = 1220 + (int)((((int64_t)s->cp_lon - 0x0F000000) >> 8) / 0x5999);
    if (lat < 0) lat = 0;
    if (lon < 0) lon = 0;
    /* leading zeros blank (the box pen repaints the cell); the digit before the point (x 299) and the tenths always drawn */
    {
        int td = (lat / 100) % 10, ud = (lat / 10) % 10, fd = lat % 10;
        hud_glyph(h, td ? '0' + td : ' ', lat_x[0], 180, 0, 5);
        hud_glyph(h, '0' + ud, lat_x[1], 180, 0, 5);
        hud_glyph(h, '0' + fd, lat_x[2], 180, 0, 5);
        int hd = (lon / 1000) % 10, tn = (lon / 100) % 10, un = (lon / 10) % 10, fr = lon % 10;
        hud_glyph(h, hd ? '0' + hd : ' ', lon_x[0], 188, 0, 5);
        hud_glyph(h, (hd || tn) ? '0' + tn : ' ', lon_x[1], 188, 0, 5);
        hud_glyph(h, '0' + un, lon_x[2], 188, 0, 5);
        hud_glyph(h, '0' + fr, lon_x[3], 188, 0, 5);
    }
    draw_ball(h, scene);
    draw_stations(h, s);
    draw_left_text(h, s);
    /* gear symbol on the lamp (0x35808) */
    if (h->have_cockpit) {
        /* variant v = (E+0x7C without bit 7) >> 5 picks the mask at 0x42B1C - 0x1C v, that is index 3 - v counting 0x42AC8 as index 0
         * (note 06 gear symbol); the mask is drawn from row 189 (its pen 0 pixels fall in rows 190..195 for variant 0, 189..192 for 3) */
        int var = 3 - ((s->cp_b7c & 0x7F) >> 5);
        const uint8_t *g = h->masks + MASK_GEAR_SYMBOLS + 28 * var;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 32; c++)
                if ((g[4 * r + c / 8] >> (7 - c % 8)) & 1) hud_plot(h, c, 189 + r, 0);
    }
    draw_message(h);
    /* afterburner line, then the lamps (0x35630, 0x35808) */
    hud_hline(h, 241, 250, 181, (s->cp_b03 & 8) ? 9 : 0);
    if (h->eject_lit) lamp(h, 9, 31, 157, 164);
    if (s->cp_w00 & 0x800) lamp(h, 9, 32, 169, 175);
    if (s->cp_ecm) lamp(h, 292, 309, 166, 172);
    if (s->cp_gear_lamp > 0 && (n & 2)) lamp(h, 10, 30, 182, 189);
}

/* ---- external view text strip (0x3697A, 0x36A10, 0x36ADE) ---- */

static void strip_cell(hud_t *h, int ch, int x) {            /* version 1 drawer: glyph pen 10 on pen 3, a 4 pixel slot */
    for (int y = 184; y <= 188; y++) hud_plot(h, x + 3, y, 3);
    hud_glyph(h, ch, x, 184, 10, 3);
}

static void strip_number(hud_t *h, long v, const int *xs, int len, int zeros) {
    char b[8]; int n = 0;
    long mod = 1; for (int i = 0; i < len; i++) mod *= 10;
    if (v < 0) v = 0;
    v %= mod;
    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < len);
    for (int i = 0; i < len; i++) { int d = len - 1 - i; strip_cell(h, d < n ? b[d] : (zeros ? '0' : ' '), xs[i]); }
}

void panel_draw_strip(hud_t *h, const game_scene_t *scene) {
    (void)scene;
    const game_hud_t *s = &h->st;
    if (s->cp_msg_ext) draw_message(h);            /* 0x11426: the message line in the external views when the mode byte is 2 (training part 1: the lesson names, note 08 8.7) */
    if (s->cp_crashed) return;                     /* V:4A985 bit 6 clear after the crash hand-over */
    if (s->cp_strip_off) return;                   /* V:4A891 clear: one pass after a switch to another entity */
    static const int hdg_l[3] = { 84, 88, 92 }, hdg_d[3] = { 100, 104, 108 }, spd_d[3] = { 150, 154, 158 },
                     kts[3] = { 167, 171, 175 }, alt_d[6] = { 208, 212, 216, 222, 226, 230 }, ft[2] = { 239, 243 };
    const char *t = "HDG"; for (int i = 0; i < 3; i++) strip_cell(h, t[i], hdg_l[i]);
    int ht = asr(s->cp_heading80, 3); if (ht < 0) ht += 3600;
    strip_number(h, (ht / 10) % 360, hdg_d, 3, 1);
    strip_number(h, h->speed_shown, spd_d, 3, 0);
    t = "KTS"; for (int i = 0; i < 3; i++) strip_cell(h, t[i], kts[i]);
    strip_number(h, h->alt_shown, alt_d, 6, 0);
    t = "FT"; for (int i = 0; i < 2; i++) strip_cell(h, t[i], ft[i]);
}

/* ---- the tower (map) view: the routine 0x2FC58 (note 06, "Map renderer") ---- */

static int w16(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v & 0xFFFFu); }

/* The camera is the viewed entity's x and z with y = the map altitude, all as high words of the position longs; the matrix is the three scales
 * 168 (x), 252 (z) and 128 (depth). Depth Z = cy * 128 / 256; screen x = 319 - (160 + trunc(160 X / Z)) with X = (168 x_rel) >> 8, and the y
 * handed to the line routine is 179 - (90 + trunc(90 Y / Z)) with Y = (252 z_rel) >> 8. Arithmetic is 16-bit wrapping. */
typedef struct { int cx, cz, Z; } mapcam_t;

static int map_col(const mapcam_t *m, int wx) {
    int X = w16((168 * w16(wx - m->cx)) >> 8);
    return 319 - (160 + (160 * X) / m->Z);
}

static int map_row(const mapcam_t *m, int wz) {
    int Y = w16((252 * w16(wz - m->cz)) >> 8);
    return 179 - (90 + (90 * Y) / m->Z);
}

/* The shape k of mapshapes.bin: k = 0..15 the direction sectors, 16 the ship; triples (dy, dx, length) ended by 0x80 (the table of 17 offset words
 * is skipped, the shapes follow each other). */
static const uint8_t *map_shape(const hud_t *h, int k) {
    const uint8_t *p = h->mapshapes + 34;
    while (k-- > 0) { while (*p != 0x80) p += 3; p++; }
    return p;
}

/* A run on row y + dy from column x + dx, length pixels (0x3019C): dropped when y + dy is outside 0..179, x + dx is negative or its last pixel x + dx + length - 1 is beyond 319; the line routine draws a
 * horizontal run one row below its y (the grid's lines do the same). */
static void map_mark(hud_t *h, int x, int y, const uint8_t *shape, int pen) {
    for (const uint8_t *t = shape; *t != 0x80; t += 3) {
        int dy = (int8_t)t[0], dx = (int8_t)t[1], len = t[2];
        int row = y + dy;
        if (row < 0 || row > 179 || x + dx < 0 || x + dx + len - 1 > 319) continue;
        hud_hline(h, x + dx, x + dx + len - 1, row + 1, pen);
    }
}

void panel_draw_map(hud_t *h, const game_scene_t *scene) {
    const game_camera_t *cam = &scene->camera;
    const game_hud_t *s = &h->st;
    mapcam_t m;
    m.cx = w16(cam->pos.x >> 16); m.cz = w16(cam->pos.z >> 16);
    m.Z = w16((w16(cam->pos.y >> 16) * 128) >> 8);
    if (m.Z <= 0) return;
    /* pen 8, one-pixel lines; 24 vertical lines at 0xF680 + 0x1C0 i and 16 horizontal at 0xFEB0 + 0x230 j (high words), each of fixed extent so
     * that it crosses the whole window; a horizontal line is drawn one row below its y. Odd lines carry a label in pen 2, only when the line is
     * accepted; each line is followed by its label. */
    for (int i = 0; i < 24; i++) {
        int sx = map_col(&m, w16(0xF680 + 0x1C0 * i));
        if (sx < 0 || sx > 319) continue;
        for (int y = 1; y <= 179; y++) hud_plot(h, sx, y, 8);
        if ((i & 1) && sx - 12 >= 8) {
            int d0 = sx - 12 < 300 ? sx - 12 : 300, v = 115 + (i - 1) / 2, x0 = 4 * (d0 >> 2) + 8;
            for (int k = 0; k < 3; k++) hud_glyph(h, '0' + (k == 0 ? v / 100 : k == 1 ? (v / 10) % 10 : v % 10), x0 + 4 * k, 174, 2, -1);
        }
    }
    for (int j = 0; j < 16; j++) {
        int yc = map_row(&m, w16(0xFEB0 + 0x230 * j));
        if (yc < 0 || yc > 179) continue;
        hud_hline(h, 0, 319, yc + 1, 8);
        if (j & 1) {
            int v = 34 + (j - 1) / 2, top = yc - 1 < 4 ? 4 : yc - 1 > 175 ? 175 : yc - 1;
            hud_glyph(h, '0' + v / 10, 8, top, 2, -1);
            hud_glyph(h, '0' + v % 10, 12, top, 2, -1);
        }
    }
    /* The marks: the core's list of the pass (src/core/mapmarks.c: which records are drawn, the stored-mark paths of V:4A86D and V:4A90F, the point, the shape index and the pen, note 06 "Map renderer" and
     * "What V:4A90F and V:4A86D hold"). The shape is drawn as the shape routine 0x3019C does. */
    if (h->have_mapshapes)
        for (int i = 0; i < s->mark_count && i < GAME_MAX_MARKS; i++) {
            const game_mark_t *k = &s->marks[i];
            if (k->shape <= 16) map_mark(h, k->x, k->y, map_shape(h, k->shape), k->pen);
        }
}
