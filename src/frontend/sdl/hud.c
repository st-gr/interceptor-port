/* src/frontend/sdl/hud.c - HUD overlay, frame state, test injection (the warning arbiter is in the core, cockpit.c).
 * Every rule below is from docs/re/06-cockpit-hud.md; section names are given in the comments. The
 * instrument panel is in panel.c. Coordinates are original bitmap pixels (320x200). */
#include "hud.h"
#include "panel.h"
#include "../../core/world.h"
#include "../../core/cockpit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- pens (note 06, "Display model used by every routine here") ---- */
#define PEN_BLACK 0
#define PEN_ALERT 1
#define PEN_GREY 8
#define PEN_TOP 10          /* outline top, crosshair, level marks, external text, target symbols */
#define PEN_ALT_TEXT PEN_TOP
#define PEN_TEXT 13         /* HUD text, tape ticks */

/* ---- assets ---- */
static int read_all(const game_loader_t *l, const char *path, uint8_t *dst, size_t want) {
    uint8_t *buf; size_t len;
    if (l->read(l->user, path, &buf, &len) != 0) return -1;
    int ok = len == want;
    if (ok) memcpy(dst, buf, want);
    l->release(l->user, buf);
    return ok ? 0 : -2;
}

hud_t *hud_create(const game_loader_t *loader) {
    hud_t *h = (hud_t *)calloc(1, sizeof *h);
    if (!h) return NULL;
    memset(h->px, HUD_NONE, sizeof h->px);
    memset(h->scope, HUD_NONE, sizeof h->scope);
    h->have_font = read_all(loader, "fonts/hud.bin", &h->font[0][0], sizeof h->font) == 0;
    int w = read_all(loader, "cockpit/warnings.bin", &h->warn[0][0], sizeof h->warn);
    int m = read_all(loader, "cockpit/masks.bin", h->masks, sizeof h->masks);
    uint8_t mk[112];
    int k = read_all(loader, "cockpit/markers.bin", mk, sizeof mk);
    h->have_cockpit = w == 0 && m == 0 && k == 0;
    for (int i = 0; i < 112; i++) h->markers[i] = (int8_t)mk[i];
    uint8_t arcs[82];
    h->have_arcs = read_all(loader, "cockpit/arcs.bin", arcs, sizeof arcs) == 0;
    for (int i = 0; i < 82; i++) h->arcs[i] = (int8_t)arcs[i];
    h->have_mapshapes = read_all(loader, "cockpit/mapshapes.bin", h->mapshapes, sizeof h->mapshapes) == 0;
    h->have_ttext = assets_load_target_text(loader, &h->ttext, NULL, 0) == 0;
    h->have_prompts = assets_load_prompts(loader, &h->prompts, NULL, 0) == 0;
    static uint8_t raw[320 * 55], rawc[288 * 12];
    int a = read_all(loader, "pix/inst5.idx", raw, sizeof raw);
    int b = read_all(loader, "pix/frnt5.idx", rawc, sizeof rawc);
    h->have_panel = a == 0 && b == 0;
    if (a == 0) for (size_t i = 0; i < sizeof raw; i++) {
        int p = raw[i] & 15;
        h->inst[i] = (uint8_t)(p == 9 ? 11 : p == 11 ? 9 : p);   /* the picture's palette has pens 9 and 11 exchanged */
    }
    if (b == 0) for (size_t i = 0; i < sizeof rawc; i++) h->cowl[i] = rawc[i] & 15;
    if (!h->have_font) fprintf(stderr, "cockpit: assets/fonts/hud.bin is missing; the HUD text is not drawn (run the extractor)\n");
    if (!h->have_cockpit) fprintf(stderr, "cockpit: assets/cockpit/* is missing; warnings, ball, compass and gear symbols are not drawn (re-run the extractor)\n");
    if (!h->have_ttext) fprintf(stderr, "cockpit: assets/tables/target_text.bin is missing; the names and labels of the message line's target data are not drawn (re-run the extractor)\n");
    if (!h->have_prompts) fprintf(stderr, "cockpit: assets/tables/prompts.bin is missing; the fire prompts are not drawn (re-run the extractor)\n");
    if (!h->have_arcs || !h->have_mapshapes) fprintf(stderr, "cockpit: assets/cockpit/arcs.bin or mapshapes.bin is missing; the lock circle, range dot and map marks are not drawn (re-run the extractor)\n");
    if (!h->have_panel) fprintf(stderr, "cockpit: assets/pix/inst5.idx or frnt5.idx is missing; the panel is not drawn (re-run the extractor)\n");
    return h;
}

void hud_destroy(hud_t *h) { free(h); }

/* ---- layer primitives ---- */
void hud_clear(hud_t *h) { memset(h->px, HUD_NONE, sizeof h->px); }

void hud_set_step(hud_t *h, int step) { h->step = step; h->off = 16 * step; }

/* The pixel plotters skip a point when x plus the view offset is below 0 or at least 320, or the row is
 * at or below 0 (note 06, "Pixel plotters"). Rows past 199 do not exist. */
void hud_plot(hud_t *h, int x, int y, int pen) {
    x += h->off; y += h->dy;
    if (x < 0 || x >= HUD_W || y <= 0 || y >= HUD_H) return;
    h->px[y * HUD_W + x] = (uint8_t)pen;
}

int hud_get(const hud_t *h, int x, int y) {
    x += h->off; y += h->dy;
    if (x < 0 || x >= HUD_W || y < 0 || y >= HUD_H) return HUD_NONE;
    return h->px[y * HUD_W + x];
}

void hud_hline(hud_t *h, int x0, int x1, int y, int pen) { for (int x = x0; x <= x1; x++) hud_plot(h, x, y, pen); }

static int glyph_index(int ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 0x20 || ch > 0x5F) ch = ' ';
    return ch - 0x20;
}

/* Glyph cell: 3 columns by 5 rows from the font. A character is skipped when its byte column plus the
 * view offset (in bytes, 2 per step) falls outside 0..39 (note 06, "Text"). bg < 0: only the glyph pixels
 * are written (the HUD); otherwise the whole cell is repainted (panel digits). */
void hud_glyph(hud_t *h, int ch, int x, int y, int fg, int bg) {
    if (!h->have_font) return;
    int bc = (x >> 3) + 2 * h->step;
    if (bc < 0 || bc > 39) return;
    const uint8_t *g = h->font[glyph_index(ch)];
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 3; c++) {
            int pen = (g[r] >> (7 - c)) & 1 ? fg : bg;
            if (pen >= 0) hud_plot(h, x + c, y + r, pen);
        }
}

void hud_number(hud_t *h, long v, const int *xs, int len, int y, int fg, int bg) {
    char buf[16];
    if (v < 0) v = 0;
    long mod = 1; for (int i = 0; i < len; i++) mod *= 10;
    v %= mod;                                   /* values that do not fit wrap, like the original's digit pairs */
    int n = 0;
    do { buf[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < len);
    for (int i = 0; i < len; i++) {
        int d = len - 1 - i;                    /* digit position counted from the right */
        hud_glyph(h, d < n ? buf[d] : ' ', xs[i], y, fg, bg);
    }
}

void hud_text(hud_t *h, const char *s, const int *xs, int y, int fg, int bg) {
    for (int i = 0; s[i]; i++) hud_glyph(h, s[i], xs[i], y, fg, bg);
}

/* ---- view tables (note 06, "Head views" and "Constants") ---- */

int hud_window_last_row(const game_camera_t *cam) {   /* the core's table, so the overlay and the list drawer agree on V:4AA3C */
    return world_window_last_row(cam->view == GAME_VIEW_TOWER, cam->view != GAME_VIEW_COCKPIT && cam->view != GAME_VIEW_TOWER, cam->head);
}

static int asr(int v, int n) { return v >> n; }          /* arithmetic shift (gcc and tcc) */
static int iabs(int v) { return v < 0 ? -v : v; }

/* ---- frame state (note 06, "Draw schedule" and "Gating a replica must keep") ----
 * The HUD row offset words V:4A990 and V:4A9D0 are the jolt's (note 07 13.3): scene.jolt_row, applied as h->dy in hud_draw. */

/* One cache of the speed, altitude or fuel digits (0x36A10, 0x36ADE, 0x36BEE, rules 1 to 4 of "Gating a replica must keep", with the pause variant of rule 4). */
static void cache_gate(int R, int n, int paused, int value, int *cache, int *flag, int *shown, int *draws) {
    if (R) { *cache = value; *flag = 1; *shown = value; (*draws)++; }                       /* 1: drawn every frame while R > 0 */
    else if (*flag) { *flag = 0; *shown = *cache; (*draws)++; }                             /* 2: the flagged repeat of the cached figure */
    else if (value == *cache) { }                                                           /* 3: equal, nothing drawn */
    else if (n & 1) { }                                                                     /* 4: a new figure only when bit 0 of n is clear */
    else if (paused) { *shown = *cache; (*draws)++; }                                       /* 4, V:4A866 nonzero: the old cached figure is drawn, the cache keeps its value (and its flag) */
    else { *cache = value; *flag = 1; *shown = value; (*draws)++; }
}

/* ---- the radar scope between walks (note 06 "Radar scope", note 07 12.3) ----
 * The original keeps the scope in its bitmap: the own-ship plus (pen 12) is drawn when the redraw counter R is nonzero or (n and 14) = 8 and stays until a blip's pen-0 erase lands on it; the cone lines
 * (pen 3) when R is nonzero or (n and 14) = 0; a walk (the core's: scene.hud.scope_walked, which also says when R above 1 emptied the erase lists) erases the blips of its page's walk two before with
 * pen 0 (the core's erase list, up to eleven entries, the last possibly stale data of the other page: clipped to the bitmap) and then draws the blips of this walk, which carry the HUD row offset and the view
 * offset already. An R pass copies the clean panel picture, which removes every blip (and erase pixel): the scope buffer is cleared then. The buffer holds final coordinates. */
static void scope_put(hud_t *h, int x, int y, int pen) {
    if (x < 0 || x >= HUD_W || y <= 0 || y >= HUD_H) return;
    h->scope[y * HUD_W + x] = (uint8_t)pen;
}
static void scope_blip(hud_t *h, const game_blip_t *b, int pen) {
    scope_put(h, b->x, b->y, pen);
    if (b->wide) scope_put(h, b->x - 1, b->y, pen);
}

static void scope_plus(hud_t *h) {   /* the own-ship plus: (158,167) and (157 to 159,168) */
    scope_put(h, 158 + h->off, 167 + h->dy, 12);
    for (int x = 157; x <= 159; x++) scope_put(h, x + h->off, 168 + h->dy, 12);
}

/* The order of one scope draw 0x35CF2 (note 07 12.1): with R = 0 the schedule is the cone lines (n and 14 = 0) or the plus (n and 14 = 8), each followed by the walk (its erase, then its blips), or the walk alone;
 * with R of 1 or more it is the cone lines, a walk, the plus, a second walk. The scene's erase list is the first walk's and its blips the last walk's (the second walk erases the first walk's blips and draws
 * the same ones again), so on an R pass the picture is: the cone lines, the erase of the old blips, the plus, the new blips. An old blip lying on the plus is erased before the plus is drawn. */
static void scope_update(hud_t *h, int R) {
    const game_hud_t *s = &h->st;
    const uint32_t n = h->tick;
    if (R) memset(h->scope, HUD_NONE, sizeof h->scope);
    if (R || (n & 14) == 0)     /* the cone lines (154,163)-(139,148) and (162,163)-(177,148), 16 pixels each, pen 3 */
        for (int i = 0; i < 16; i++) { scope_put(h, 154 - i + h->off, 163 - i + h->dy, 3); scope_put(h, 162 + i + h->off, 163 - i + h->dy, 3); }
    if (!R && (n & 14) == 8) scope_plus(h);   /* the plus pass of R = 0 draws before its walk */
    if (s->scope_walked) {   /* no walk in a pass when the core says so (a paused pass, an external view, bit 1 of n with R at most 1) */
        const int ne = (int)(sizeof s->erase / sizeof s->erase[0]);
        for (int i = 0; i < s->erase_count && i < ne; i++) scope_blip(h, &s->erase[i], 0);
    }
    if (R) scope_plus(h);   /* the plus of an R pass comes after the first walk's erase */
    if (!s->scope_walked) return;
    const int nb = (int)(sizeof s->blips / sizeof s->blips[0]);
    for (int i = 0; i < s->blip_count && i < nb; i++) scope_blip(h, &s->blips[i], s->blips[i].pen);
}

void hud_frame(hud_t *h, const game_scene_t *scene) {
    if (!h) return;
    if ((scene->mode != GAME_MODE_FLIGHT && scene->mode != GAME_MODE_PAUSED) || !scene->hud.cp_valid) { h->started = 0; return; }
    h->st = scene->hud;                                       /* the published values (message line, HUD switch) are always current */
    const game_hud_t *s = &h->st;
    uint32_t n = s->cp_n;
    int view = scene->camera.view;
    /* One advance per pass of the frame routine. The draw schedule precedes the pause test, so a paused pass runs every draw again (the radar
     * blink counter and the EJECT timer keep running; n stands still and the caches take the pause variant): the dedupe is on the
     * pass counter, not on n. */
    if (h->started && s->cp_pass == h->pass) return;          /* the same pass (further presented frames of one step) */
    h->pass = s->cp_pass;
    h->tick = n;

    int R = s->cp_redraw > 0;   /* the core's V:4A8EF: 3 on the first pass of a flight and after each writer of the helper 0x1EFF4 (note 07 12.1) */
    int cockpit = view == GAME_VIEW_COCKPIT;
    /* speed (0x36A10): in the cockpit the window n and 3 of 2 or 3; the external views call it on every pass ("In the external
     * views the schedule is 0x36A10, 0x36ADE, 0x3697A"), so only rule 4 limits new figures there */
    int speed = iabs(s->cp_speed_w) / 12;
    if (!cockpit || R || (n & 3) > 1) cache_gate(R, (int)n, s->cp_paused != 0, speed, &h->speed_cache, &h->speed_flag, &h->speed_shown, &h->speed_draws);
    /* altitude (0x36ADE): window n and 3 of 0 or 1 in the cockpit, and on every pass while the camera is below 32768 position units
     * (V:4AB1E above -0x8000, about 160 feet; note 06 "Draw schedule" item 3) */
    int alt = 5 * asr(s->cp_y, 10);
    if (!cockpit || R || (n & 3) < 2 || scene->camera.pos.y < 32768)
        cache_gate(R, (int)n, s->cp_paused != 0, alt, &h->alt_cache, &h->alt_flag, &h->alt_shown, &h->alt_draws);
    if (cockpit) {
        /* throttle digits (0x36C3C): window 0 or 1; byte times 256 divided by 307 */
        if (R || (n & 3) < 2) h->throttle_shown = (int)((unsigned)s->cp_throttle * 256u / 307u);
        /* fuel digits (0x36BEE): window n and 7 of 0 or 1, the same four cache rules as speed and altitude (cache V:4A9AE with the
         * flag in bit 15; note 06 "Gating a replica must keep" and the fuel bar row); fuel bar (0x353EC): window n and 15 of 0 or 1,
         * its level taken from that cache (E+0x72 >> 8 as last drawn), not from the current fuel */
        if (R || (n & 7) < 2) cache_gate(R, (int)n, s->cp_paused != 0, asr(s->cp_fuel, 8), &h->fuel_cache, &h->fuel_flag, &h->fuel_shown, &h->fuel_draws);
        if (R || (n & 15) < 2) h->fuel_bar_level = (h->fuel_cache & 0x7FFF) >> 10;
        /* EJECT lamp (V:4A8FA, note 07 6.5, V: followup-combat-eject-frames.txt, settled by the per-frame capture): the key handler runs before the panel draw of its own pass and sets the timer to 8, each cockpit
         * draw decrements it and the lamp is lit on the draws whose decremented value is odd while the key state is armed: the lamp is lit in the pictures of the key's own pass and every second pass after it
         * (offsets 0, 2, 4, 6: the key pass's draw gives 7) and unlit at 1, 3, 5. */
        if (s->cp_eject_key) h->eject_timer = 8;
        h->eject_lit = 0;
        if (h->eject_timer > 0) { h->eject_timer--; h->eject_lit = s->cp_eject_armed != 0 && (h->eject_timer & 1); }
    } else if (s->cp_eject_key) h->eject_timer = 8;   /* the key sets the timer in whatever view it is taken; the countdown is the cockpit draw's (the next one) */
    /* The panel-block passes of an external view (scene.hud.panel_block, V:4A8EE non-zero) draw nothing visible [R/V: note 07 12.1, capture combat-panel-block.txt: the panel rows hold 0 non-black pixels and the
     * chase view with its three-label line stays]: only their side effects count (the counters, the scope walks' threat bits and V:4A93B, the HUD external return clearing V:4A96C), and those are the core's.
     * The frontend therefore draws the external view only: no panel picture and no scope blips there (scope_update runs in the cockpit view only). */
    h->off = 16 * scene->camera.hud_step; h->dy = 0;
    if (cockpit) {
        const int hs = scene->camera.hud_step < 0 ? -scene->camera.hud_step : scene->camera.hud_step;
        h->dy = hs <= 14 ? scene->jolt_row : 0;
        scope_update(h, R);
    }
    h->off = 0; h->dy = 0;
    h->started = 1;
}

/* ---- the HUD (note 06, "HUD (F/A-18...)") ---- */

static void draw_fixed(hud_t *h, const game_hud_t *s) {
    /* outline (0x38C02): only for the player's class 0x10, 0x11, 0x12 */
    if (s->cp_class >= 0x10 && s->cp_class <= 0x12) {
        hud_hline(h, 93, 224, 37, PEN_TOP);
        for (int i = 0; i < 8; i++) hud_plot(h, 85 + i, 45 - i, PEN_GREY);      /* left chamfer (85,45) to (92,38) */
        for (int i = 0; i < 9; i++) hud_plot(h, 225 + i, 37 + i, PEN_GREY);     /* right chamfer (225,37) to (233,45) */
        for (int y = 46; y <= 144; y++) { hud_plot(h, 85, y, PEN_BLACK); hud_plot(h, 233, y, PEN_BLACK); }
    }
    /* crosshair: x 159 at rows 71, 72, 74, 75 and row 73 from 157 to 161 (the code's 72 is drawn one row lower) */
    static const int ch_rows[4] = { 71, 72, 74, 75 };
    for (int i = 0; i < 4; i++) hud_plot(h, 159, ch_rows[i], PEN_TOP);
    hud_hline(h, 157, 161, 73, PEN_TOP);
    /* level marks: row 91, the horizon of level flight */
    hud_hline(h, 141, 145, 91, PEN_TOP);
    hud_hline(h, 173, 177, 91, PEN_TOP);
    /* nose datum (0x37DBA) */
    for (int y = 129; y <= 131; y++) { hud_plot(h, 159, y, PEN_GREY); hud_plot(h, 160, y, PEN_GREY); }
    hud_hline(h, 158, 161, 132, PEN_GREY);
}

static void draw_tape_label(hud_t *h, int v, int x) {
    char c[2] = { (char)('0' + v / 10), (char)('0' + v % 10) };
    hud_glyph(h, c[0], x, 53, PEN_TEXT, -1);
    hud_glyph(h, c[1], x + 4, 53, PEN_TEXT, -1);
}

/* Heading tape (inside 0x37E2C). */
static void draw_tape(hud_t *h, int heading80) {
    int ht = asr(heading80, 3) % 3600; if (ht < 0) ht += 3600;           /* tenths of a degree */
    int r = ht % 100, base = ht / 100, sres;
    if (r > 40) { base = (base + 1) % 36; sres = r - 100; } else sres = r;
    int o = -(sres / 5);                                                  /* truncation toward zero; 2 pixels per degree */
    int bx = 159 + o;
    for (int k = -10; k <= 10; k++) {
        int x = bx + 10 * k;
        if (x < 134 || x > 184) continue;
        hud_plot(h, x, 61, PEN_TEXT);
        if ((k & 1) == 0) hud_plot(h, x, 60, PEN_TEXT);                   /* the base tick and every 20 pixels */
    }
    for (int y = 63; y <= 65; y++) hud_plot(h, 159, y, PEN_TEXT);
    draw_tape_label(h, base, 156 + o);
    if (o <= 2) draw_tape_label(h, (base + 1) % 36, 156 + o + 20);
    if (o >= -2) draw_tape_label(h, (base + 35) % 36, 156 + o - 20);
}

/* The G readout (0x36828): the F/A-18 form has the sign at x 92, the tens digit at 96, the units digit at 102, the point at (100, 125) and the letter G
 * at 108 on row 121; every other class takes the F-16 form: sign at 106, tens 110, units 117, glyph row 61, the point at (114, 65), no letter. */
static void draw_g(hud_t *h, const game_hud_t *s, int x_sign, int x_tens, int x_units, int px, int row_top) {
    int w = s->cp_pitch_rate;
    int c = iabs(s->cp_gcorr) > 1 ? s->cp_gcorr : 0;
    int sgn = s->cp_gsign ? 10 : -10;
    int v = asr(w + asr(w, 3) + c, 3) + sgn;
    int mag = iabs(v) % 100;
    if (v >= 0) hud_glyph(h, '-', x_sign, row_top, PEN_TEXT, -1);       /* a minus sign when v is at least 0 (level flight, v = -10, prints 1.0) */
    if (mag >= 10) hud_glyph(h, '0' + mag / 10, x_tens, row_top, PEN_TEXT, -1);
    hud_glyph(h, '0' + mag % 10, x_units, row_top, PEN_TEXT, -1);        /* the units digit always, a figure below 1.0 shows as ".8" */
    hud_plot(h, px, row_top + 4, PEN_TEXT);
}

static void draw_weapon(hud_t *h, const game_hud_t *s) {       /* 0x36724 */
    int sel = s->cp_sel >> 4;
    if (!sel) return;
    static const int gun_x[3] = { 92, 96, 100 }, amm_x[3] = { 108, 112, 116 }, two_x[2] = { 96, 100 }, one_x[1] = { 108 };
    if (sel == 1) {
        hud_text(h, "GUN", gun_x, 129, PEN_TEXT, -1);
        hud_number(h, s->cp_rounds % 1000, amm_x, 3, 129, PEN_TEXT, -1);   /* the unsigned word modulo 1000, leading zeros blank, the last digit always */
    } else if (sel == 3) {
        hud_text(h, "AM", two_x, 129, PEN_TEXT, -1);
        if (s->cp_stock & 15) hud_number(h, s->cp_stock & 15, one_x, 1, 129, PEN_TEXT, -1);       /* a stock of 0 draws no digit */
    } else {
        hud_text(h, "SW", two_x, 129, PEN_TEXT, -1);
        if (s->cp_stock >> 4) hud_number(h, s->cp_stock >> 4, one_x, 1, 129, PEN_TEXT, -1);
    }
}

static void draw_numeric(hud_t *h, const game_hud_t *s) {
    static const int sx[4] = { 92, 98, 102, 106 }, ax[6] = { 204, 208, 212, 218, 222, 226 }, kt[2] = { 102, 106 }, ft[2] = { 222, 226 };
    int speed = (s->cp_w00 & 0x8000) ? 0 : iabs(s->cp_speed_w) / 12;     /* 0 when the hit flag is set */
    hud_number(h, speed, sx, 4, 84, PEN_TEXT, -1);
    hud_text(h, "KT", kt, 91, PEN_TEXT, -1);
    hud_number(h, 5 * asr(s->cp_y, 10), ax, 6, 84, PEN_TEXT, -1);
    hud_text(h, "FT", ft, 91, PEN_TEXT, -1);
}

/* F-16 scale form (class 0x10; 0x37ED6 and 0x38100, note 06 "HUD (F-16, entity class 0x10): scale form", reproduced pixel for pixel there).
 * v is the speed in knots or the altitude in tens of feet. L = v mod 100 picks the centre label N (units of 10 knots or 500 feet: the labels step
 * by 5) and a displacement d: L 0..20 N = (v / 100) * 10, d 0; 21..70 N = (v / 100) * 10 + 5, d -50; 71..99 N = ((v + 100) / 100) * 10, d -100.
 * R0 = (L + d) / 3 toward zero; the centre row is 91 + R0 and the centre label's glyph top row 89 + R0. Labels N + 5k above at top 89 + R0 - 15k
 * while the top row is at least 70, N - 5k below at 89 + R0 + 15k while at most 109, stopping after a label of 0; ticks one pixel at tick_x on the rows
 * y0 +- 3k (upward while the row is at least 72, downward while at most 111), the centre row and every fifth tick also light tick_x + xtra. */
static void scale_label(hud_t *h, int n, const int *xs, int top, int blank, int dot_x) {
    n %= 1000;
    int d[3] = { n / 100, (n / 10) % 10, n % 10 };
    for (int i = 0; i < 3; i++) {
        int lead = blank && i < 2 && ((i == 0 && d[0] == 0) || (i == 1 && d[0] == 0 && d[1] == 0));
        hud_glyph(h, lead ? ' ' : '0' + d[i], xs[i], top, PEN_TEXT, -1);
    }
    if (dot_x) hud_plot(h, dot_x, top + 4, PEN_TEXT);                   /* the altitude labels carry a point: "02.5" */
}

static void draw_scale(hud_t *h, int v, const int *xs, int blank, int dot_x, int tick_x, int xtra) {
    int L = v % 100, N, d;
    if (L <= 20) { N = (v / 100) * 10; d = 0; }
    else if (L <= 70) { N = (v / 100) * 10 + 5; d = -50; }
    else { N = ((v + 100) / 100) * 10; d = -100; }
    int R0 = (L + d) / 3, y0 = 91 + R0, top0 = 89 + R0;
    scale_label(h, N, xs, top0, blank, dot_x);
    for (int k = 1; top0 - 15 * k >= 70; k++) scale_label(h, N + 5 * k, xs, top0 - 15 * k, blank, dot_x);
    for (int k = 1; N - 5 * (k - 1) > 0 && top0 + 15 * k <= 109; k++) scale_label(h, N - 5 * k, xs, top0 + 15 * k, blank, dot_x);
    for (int k = 0; y0 - 3 * k >= 72; k++) {
        hud_plot(h, tick_x, y0 - 3 * k, PEN_TEXT);
        if (k % 5 == 0) hud_plot(h, tick_x + xtra, y0 - 3 * k, PEN_TEXT);
    }
    for (int k = 1; y0 + 3 * k <= 111; k++) {
        hud_plot(h, tick_x, y0 + 3 * k, PEN_TEXT);
        if (k % 5 == 0) hud_plot(h, tick_x + xtra, y0 + 3 * k, PEN_TEXT);
    }
}

static void draw_scales(hud_t *h, const game_hud_t *s) {
    static const int speed_x[3] = { 92, 96, 100 }, alt_x[3] = { 215, 219, 225 };
    int speed = (s->cp_w00 & 0x8000) ? 0 : iabs(s->cp_speed_w) / 12;
    int tens = (5 * asr(s->cp_y, 10)) / 10;                              /* altitude in tens of feet */
    draw_scale(h, speed, speed_x, 1, 0, 105, -1);
    hud_hline(h, 107, 111, 91, PEN_TEXT);                                /* the fixed pointers */
    static const int chev[6][2] = { {109,86}, {110,86}, {108,87}, {108,88}, {109,89}, {110,89} };
    for (int i = 0; i < 6; i++) hud_plot(h, chev[i][0], chev[i][1], PEN_TEXT);
    draw_scale(h, tens, alt_x, 0, 223, 212, 1);
    hud_hline(h, 206, 210, 91, PEN_TEXT);
}

/* ---- target symbols (note 06, "Target symbols"; the producers are the weapon slice's, milestone 5) ---- */

static int in_x(int x, int lo, int hi) { return x >= lo && x <= hi; }

/* The target box (0x38D8C): four lines in pen 10 around the target point. Each end point is clamped to x 86..232 and y 45..144
 * (before the view offset), a side wholly outside is skipped; a horizontal line is drawn one row below its y, a vertical one from
 * the upper end plus 1 to the lower end, so the lit pixels are columns x-9..x+9 and rows y-7..y+9 (68 pixels). */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void box_side(hud_t *h, int x0, int y0, int x1, int y1) {
    if ((x0 < 86 && x1 < 86) || (x0 > 232 && x1 > 232) || (y0 < 45 && y1 < 45) || (y0 > 144 && y1 > 144)) return;
    x0 = clampi(x0, 86, 232); x1 = clampi(x1, 86, 232); y0 = clampi(y0, 45, 144); y1 = clampi(y1, 45, 144);
    if (y0 == y1) { hud_hline(h, x0 < x1 ? x0 : x1, x0 < x1 ? x1 : x0, y0 + 1, PEN_TOP); return; }
    int top = y0 < y1 ? y0 : y1, bot = y0 < y1 ? y1 : y0;
    for (int y = top + 1; y <= bot; y++) hud_plot(h, x0, y, PEN_TOP);              /* the boxes' vertical sides have x0 == x1 */
}

static void draw_box(hud_t *h, int tx, int ty) {
    box_side(h, tx - 9, ty - 8, tx + 9, ty - 8);
    box_side(h, tx + 9, ty - 8, tx + 9, ty + 8);
    box_side(h, tx + 9, ty + 8, tx - 9, ty + 8);
    box_side(h, tx - 9, ty + 8, tx - 9, ty - 8);
}

/* The lock circle (0x3905C): the quarter arc of the extracted list reflected four ways, pen 10. The full arc is drawn when the
 * centre lies in x 100..218 and rows 58..131; outside that window each pixel goes through the range dot's clip (x 86..232, rows 46..143). */
static void draw_lock_circle(hud_t *h, int cx, int cy) {
    if (!h->have_arcs) return;
    int full = in_x(cx, 100, 218) && in_x(cy, 58, 131);
    for (int i = 0; i < 20; i++) {
        int dx = h->arcs[2 * i], dy = h->arcs[2 * i + 1];
        int pt[4][2] = { { cx + dx, cy - dy }, { cx + dx, cy + dy - 1 }, { cx - dx, cy + dy - 1 }, { cx - dx, cy - dy } };
        for (int k = 0; k < 4; k++)
            if (full || (in_x(pt[k][0], 86, 232) && in_x(pt[k][1], 46, 143))) hud_plot(h, pt[k][0], pt[k][1], PEN_ALT_TEXT);
    }
}

/* The range dot (0x392AE): a 2 by 2 dot in pen 13 on the second arc (19 pairs) at the position index k (0 to 75, the core's: k = range * 76 / 36000 as an unsigned 16-bit quotient), 19 positions per
 * quadrant clockwise from the top. Quadrants: pairs in order reflected (dx,-dy), then reversed with (dx,dy-1), in order with (-dx,dy-1), reversed with (-dx,-dy) (k = 0, 19, 38 give the offsets of the note). */
static void draw_range_dot(hud_t *h, int cx, int cy, int k) {
    if (!h->have_arcs || k < 0 || k >= 76) return;
    int q = k / 19, i = k % 19;
    const int8_t *p = h->arcs + 42;
    int j = (q & 1) ? 18 - i : i, dx = p[2 * j], dy = p[2 * j + 1], x, y;
    switch (q) {
    case 0: x = cx + dx; y = cy - dy; break;
    case 1: x = cx + dx; y = cy + dy - 1; break;
    case 2: x = cx - dx; y = cy + dy - 1; break;
    default: x = cx - dx; y = cy - dy; break;
    }
    int x0 = (x & 15) == 0 ? x : x - 1;                     /* columns x-1..x, or x..x+1 when x mod 16 = 0 */
    for (int c = 0; c < 2; c++)
        for (int r = 0; r < 2; r++)
            if (in_x(x0 + c, 86, 232) && in_x(y + r, 46, 143)) hud_plot(h, x0 + c, y + r, PEN_TEXT);
}

/* The target box, the marker and the lock symbols. The decisions are the core's (note 07 12.7: the lock state, the sight with the fire prompt, the pipper, the circle, the range dot and the range number,
 * scene.hud.tgt_*); this routine only draws them with the shapes of note 06 "Target symbols". The pipper pixel is pen 13 (note 07 12.7: the HUD draw leaves the pen word at 13 before 0x385F4). */
static void draw_target(hud_t *h, const game_hud_t *s, uint32_t n) {
    static const int8_t jit[8][2] = { {0,0}, {0,1}, {1,0}, {1,1}, {0,0}, {0,-1}, {-1,0}, {-1,-1} };
    int tx = s->target_box_x, ty = s->target_box_y;
    int tvalid = s->target_box_visible && tx + h->off > 0 && ty > 0;
    if (tvalid) {   /* the jitter pair (the pitch rate nonzero) moves the target point the box is taken from, not the marker (note 06) */
        if (s->cp_pitch_rate) { int k = (s->cp_lon & 0x1C) >> 2; tx += jit[k][0]; ty += jit[k][1]; }
        draw_box(h, tx, ty);
    }
    /* the marker shapes (0x38884 calling 0x3936E), not for the F-16: the diamond, or the ring in the lock box (it blinks) */
    if (s->tgt_marker && s->cp_class != 0x10) {
        const int mx = s->tgt_marker_x, my = s->tgt_marker_y;
        if (s->tgt_marker == 2) {
            if ((n & 3) != 0 && in_x(mx, 97, 221) && in_x(my, 47, 133))
                for (int i = 0; i < 44; i++) {
                    int x = mx + h->markers[24 + 2 * i], y = my + h->markers[24 + 2 * i + 1];
                    hud_plot(h, clampi(x, 10, 310), y, PEN_TOP);
                }
        } else if (in_x(mx, 89, 229) && in_x(my, 40, 140)) {
            for (int i = 0; i < 12; i++) hud_plot(h, mx + h->markers[2 * i], my + h->markers[2 * i + 1], PEN_TOP);
        }
    }
    if (s->tgt_pip_pixel) hud_plot(h, s->cp_pipper_x, s->cp_pipper_y, PEN_TEXT);
    if (s->tgt_circle) {
        draw_lock_circle(h, s->tgt_circle_x, s->tgt_circle_y);
        draw_range_dot(h, s->tgt_circle_x, s->tgt_circle_y, s->tgt_dot);
    }
    /* The range number (0x367DA, note 07 12.7): five characters from x 188, 4 pixels apart, top row 123, pen 13 (the routine's text cursor is the byte column 22 of row 123 plus the 12 pixels of the 3x5
     * drawer's first cell). A space (the sign of a closing target) is not drawn. */
    if (s->tgt_number) {
        static const int num_x[5] = { 188, 192, 196, 200, 204 };
        hud_text(h, s->tgt_number_text, num_x, 123, PEN_TEXT, -1);
    }
    /* the fire prompt (0x36930): the core says when (V:4A96C non-zero after the update, or the gun branch of the sight); the string is the game's (tables/prompts.bin): six characters from x 204, four pixels
     * apart, the gun form (selection nibble 1) or the missile form, a blank is not drawn */
    if (s->tgt_prompt && h->have_prompts) {
        static const int prompt_x[PROMPT_CHARS] = { 204, 208, 212, 216, 220, 224 };
        char buf[PROMPT_CHARS + 1];
        memcpy(buf, h->prompts.text[(s->tgt_sel >> 4) == 1 ? 1 : 0], PROMPT_CHARS); buf[PROMPT_CHARS] = 0;
        hud_text(h, buf, prompt_x, 129, PEN_TEXT, -1);
    }
}

static void draw_hud(hud_t *h, const game_hud_t *s, uint32_t n) {
    draw_fixed(h, s);
    draw_weapon(h, s);
    draw_target(h, s, n);
    if (!s->cp_hud_on) return;
    draw_tape(h, s->cp_heading80);
    if (s->cp_class == 0x10) draw_scales(h, s); else draw_numeric(h, s);
    if (s->cp_class == 0x11) { draw_g(h, s, 92, 96, 102, 100, 121); hud_glyph(h, 'G', 108, 121, PEN_TEXT, -1); }
    else draw_g(h, s, 106, 110, 117, 114, 61);                             /* the F-16 form for every class but 0x11, also for 0x12 */
}

/* ---- the frame ---- */

const uint8_t *hud_draw(hud_t *h, const game_scene_t *scene) {
    hud_clear(h);
    h->step = h->off = h->dy = 0;
    if ((scene->mode != GAME_MODE_FLIGHT && scene->mode != GAME_MODE_PAUSED) || !scene->hud.cp_valid) return h->px;
    if (!h->started) hud_frame(h, scene);
    const game_camera_t *cam = &scene->camera;
    const game_hud_t *s = &h->st;
    if (cam->view == GAME_VIEW_COCKPIT) {
        hud_set_step(h, cam->hud_step);   /* the core's step byte: the table byte or the equal-row 50 */
        h->dy = (cam->hud_step < 0 ? -cam->hud_step : cam->hud_step) <= 14 ? scene->jolt_row : 0;   /* the jolt shifts the HUD and panel rows (note 07 13.3; the full-width views cancel it) */
        panel_draw_cockpit(h);               /* panel picture, cowl, radar, then the instruments */
        draw_hud(h, s, h->tick);
        panel_draw_instruments(h, scene);
    } else {
        hud_set_step(h, 0);
        if (cam->view == GAME_VIEW_TOWER) panel_draw_map(h, scene);
        panel_draw_strip(h, scene);
    }
    return h->px;
}

/* ---- scene injection for tests ---- */

typedef struct { const char *name; size_t off; int size; int sgn; } field_t;
#define F(n, T, S) { #n, offsetof(game_hud_t, cp_##n), sizeof(T), S }
static const field_t FIELDS[] = {
    F(class, uint8_t, 0), F(sel, uint8_t, 0), F(stock, uint8_t, 0), F(b03, uint8_t, 0), F(b20, uint8_t, 0),
    F(b7c, uint8_t, 0), F(throttle, uint8_t, 0), F(w00, uint16_t, 0), F(rounds, uint16_t, 0), F(w6c, uint16_t, 0),
    F(pitch_rate, int16_t, 1), F(gcorr, int16_t, 1), F(heading80, int32_t, 1), F(speed_w, int32_t, 1),
    F(y, int32_t, 1), F(lat, int32_t, 1), F(lon, int32_t, 1), F(fuel, int32_t, 1), F(gsign, uint8_t, 0),
    F(eject_armed, uint8_t, 0), F(ecm, uint8_t, 0), F(gear_lamp, uint8_t, 0), F(zoom, uint8_t, 0),
    F(crashed, uint8_t, 0), F(threat, uint8_t, 0), F(strip_off, uint8_t, 0), F(n, uint32_t, 0), F(pass, uint32_t, 0), F(paused, uint8_t, 0), F(hud_on, uint8_t, 0),
    F(msg_code, uint16_t, 0), F(msg_entry, uint8_t, 0), F(msg_phase, uint8_t, 0), F(msg_ext, uint8_t, 0), F(sound_req, uint8_t, 0),
    F(designated, uint8_t, 0), F(range, uint16_t, 0), F(pipper_x, int16_t, 1), F(pipper_y, int16_t, 1),
};

static void put(void *base, const field_t *f, long v) {
    uint8_t *p = (uint8_t *)base + f->off;
    switch (f->size) {
    case 1: *p = (uint8_t)v; break;
    case 2: { uint16_t x = (uint16_t)v; memcpy(p, &x, 2); break; }
    default: { int32_t x = (int32_t)v; memcpy(p, &x, 4); }
    }
}

/* The harness's own state: the core's cockpit state, whose R rule (cockpit_redraw_pass, cockpit_redraw_request) supplies V:4A8EF for a scene that has no core behind it. */
struct hud_test { int ball; int16_t ball_xy[6]; /* ball=...: the ball's sky triangle, applied after the frame's sky (hud_test_post) */ cockpit_t ck; int last_frame; int8_t head_step[14]; /* tables/head_step.bin (zeros when absent): the "head" command's step */ };
hud_test_t *hud_test_create(const game_loader_t *loader) {
    hud_test_t *t = (hud_test_t *)calloc(1, sizeof *t);
    if (t) { cockpit_init(&t->ck, NULL); t->ck.redraw = 3; t->last_frame = -1; }   /* the stand-in starts from a fresh redraw (a harness convention: the real core has no flight-start writer of R, note 07 12.1; its first pass holds what the screens left) */
    if (t && loader) assets_load_head_step(loader, t->head_step, NULL, 0);
    return t;
}
void hud_test_destroy(hud_test_t *t) { free(t); }

void hud_test_post(hud_test_t *t, game_scene_t *s) {
    if (!t->ball) return;
    s->sky.valid = 1; s->sky.ball_n = 3;
    for (int i = 0; i < 6; i++) s->sky.ball_xy[i] = t->ball_xy[i];
}

int hud_test_apply(hud_test_t *t, game_scene_t *s, int frame, const char *spec, char *err, size_t errlen) {
    char buf[1024];
    if (strlen(spec) >= sizeof buf) { snprintf(err, errlen, "--hud-test: spec too long"); return -1; }
    strcpy(buf, spec);
    /* the spec defines the whole raw block: zeros plus the named fields, n is the frame number unless named */
    memset(&s->hud, 0, sizeof s->hud);
    t->ball = 0;
    int named_redraw = 0, named_tsel = 0, rreq = 0;
    s->jolt_row = 0;   /* the spec names the whole cockpit state; a jolt of the real core is not part of it */
    s->hud.cp_valid = 1; s->hud.cp_hud_on = 1; s->hud.cp_n = s->hud.cp_pass = (uint32_t)frame;
    s->hud.tgt_dot = -1;
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        char *eq = strchr(tok, '=');
        if (!eq) { snprintf(err, errlen, "--hud-test: '%s' is not name=value", tok); return -1; }
        *eq = 0; const char *name = tok, *val = eq + 1;
        char *end; long v = strtol(val, &end, 0);
        game_hud_t *hd = &s->hud;
        if (!strcmp(name, "cam_pitch")) { s->camera.pitch = (int32_t)v; continue; }
        if (!strcmp(name, "cam_roll")) { s->camera.roll = (int32_t)v; continue; }
        if (!strcmp(name, "cam_yaw")) { s->camera.yaw = (int32_t)v; continue; }
        if (!strcmp(name, "cam_x")) { s->camera.pos.x = (int32_t)v; continue; }
        if (!strcmp(name, "cam_y")) { s->camera.pos.y = (int32_t)v; continue; }
        if (!strcmp(name, "cam_z")) { s->camera.pos.z = (int32_t)v; continue; }
        if (!strcmp(name, "view")) { s->camera.view = (game_view_t)v; continue; }
        if (!strcmp(name, "head")) {   /* the table byte of the code (note 06: the bytes at 0x1E800, code 4's is -50); "hud_step" overrides it (the core's equal-row 50) */
            s->camera.head = (uint8_t)v; s->camera.hud_step = (int8_t)world_head_step(t->head_step, (int)v); continue;
        }
        if (!strcmp(name, "hud_step")) { s->camera.hud_step = (int8_t)v; continue; }
        if (!strcmp(name, "ring")) { s->camera.ring = (uint8_t)v; continue; }
        if (!strcmp(name, "walk")) { hd->scope_walked = v != 0; continue; }
        if (!strcmp(name, "redraw")) { hd->cp_redraw = (uint8_t)v; named_redraw = 1; continue; }
        if (!strcmp(name, "rreq")) { rreq = v != 0; continue; }   /* one of the writers of V:4A8EF ran before this frame's draws (note 07 12.1): the core's helper sets R = 3 */
        if (!strcmp(name, "pip")) { hd->tgt_pip_pixel = v != 0; continue; }
        if (!strcmp(name, "prompt")) { hd->tgt_prompt = (uint8_t)v; continue; }
        if (!strcmp(name, "tsel")) { hd->tgt_sel = (uint8_t)v; named_tsel = 1; continue; }   /* +0x63 of the viewed record */
        if (!strcmp(name, "dot")) { hd->tgt_dot = (int16_t)v; continue; }
        if (!strcmp(name, "marker") || !strcmp(name, "circle")) {   /* marker=KIND:x:y (1 the diamond, 2 the ring), circle=x:y: the core's decisions (scene.hud.tgt_marker, tgt_circle) */
            int a[3] = { 0 };
            const int isc = name[0] == 'c';
            if (sscanf(val, "%d:%d:%d", &a[0], &a[1], &a[2]) < (isc ? 2 : 3)) { snprintf(err, errlen, "--hud-test: marker=KIND:x:y, circle=x:y"); return -1; }
            if (isc) { hd->tgt_circle = 1; hd->tgt_circle_x = (int16_t)a[0]; hd->tgt_circle_y = (int16_t)a[1]; }
            else { hd->tgt_marker = (uint8_t)a[0]; hd->tgt_marker_x = (int16_t)a[1]; hd->tgt_marker_y = (int16_t)a[2]; }
            continue;
        }
        if (!strcmp(name, "ball")) {   /* ball=x0:y0:x1:y1:x2:y2: the sky polygon of the attitude ball (window coordinates, three vertices) as the core's scene gives it (scene.sky.ball_*) */
            int a[6] = { 0 };
            if (sscanf(val, "%d:%d:%d:%d:%d:%d", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) < 6) { snprintf(err, errlen, "--hud-test: ball=x0:y0:x1:y1:x2:y2"); return -1; }
            t->ball = 1;
            for (int i = 0; i < 6; i++) t->ball_xy[i] = (int16_t)a[i];
            continue;
        }
        if (!strcmp(name, "mark")) {   /* mark=rec:x:y:shape:pen - a mark of the map view as the core's list gives it (scene.hud.marks; repeatable) */
            long a[5] = { 0 };
            if (sscanf(val, "%ld:%ld:%ld:%ld:%ld", &a[0], &a[1], &a[2], &a[3], &a[4]) < 5 || hd->mark_count >= GAME_MAX_MARKS) {
                snprintf(err, errlen, "--hud-test: mark=rec:x:y:shape:pen"); return -1; }
            game_mark_t *m = &hd->marks[hd->mark_count++];
            m->rec = (uint8_t)a[0]; m->x = (int16_t)a[1]; m->y = (int16_t)a[2]; m->shape = (uint8_t)a[3]; m->pen = (uint8_t)a[4];
            continue;
        }
        if (!strcmp(name, "tline")) {   /* tline=mode:colour:col:digits[:cls:detected:page] - the message line's target data (note 07 12.8); the digits may be empty, detected defaults to 1 */
            long f[3] = { 0 }, x[3] = { 0, 1, 0 };
            char dg[8] = "";
            const char *q = val;
            char *e2;
            for (int i = 0; i < 3; i++) {
                f[i] = strtol(q, &e2, 0);
                if (e2 == q || *e2 != ':') { snprintf(err, errlen, "--hud-test: tline=mode:colour:col:digits[:cls:detected:page]"); return -1; }
                q = e2 + 1;
            }
            const size_t n = strcspn(q, ":");
            if (n > 7) { snprintf(err, errlen, "--hud-test: the tline digits are at most 7 characters"); return -1; }
            memcpy(dg, q, n); dg[n] = 0; q += n;
            for (int i = 0; i < 3 && *q == ':'; i++) { x[i] = strtol(q + 1, &e2, 0); q = e2; }
            hd->tline_mode = (uint8_t)f[0]; hd->tline_colour = (uint8_t)f[1]; hd->tline_col = (uint8_t)f[2]; strcpy(hd->tline_digits, dg);
            hd->tline_cls = (uint8_t)x[0]; hd->tline_detected = (uint8_t)x[1]; hd->tline_page = (uint8_t)x[2];
            continue;
        }
        if (!strcmp(name, "number")) {   /* number=TEXT - the range number of the lock marker (note 07 12.7): five characters, a space or minus and four digits */
            if (strlen(val) != 5) { snprintf(err, errlen, "--hud-test: number= takes five characters"); return -1; }
            hd->tgt_number = 1; memcpy(hd->tgt_number_text, val, 6);
            continue;
        }
        if (!strcmp(name, "jolt_row")) { s->jolt_row = (int8_t)v; continue; }
        if (!strcmp(name, "blip") || !strcmp(name, "erase")) {   /* blip=x:y:wide:pen[:rec], erase=x:y:wide */
            int a[5] = { 0 };
            const int isb = name[0] == 'b';
            if (sscanf(val, "%d:%d:%d:%d:%d", &a[0], &a[1], &a[2], &a[3], &a[4]) < 3) { snprintf(err, errlen, "--hud-test: %s=x:y:wide[:pen[:rec]]", name); return -1; }
            const int cap = isb ? 10 : 11;   /* the erase read goes one past a page of ten */
            if ((isb ? hd->blip_count : hd->erase_count) >= cap) { snprintf(err, errlen, "--hud-test: at most %d %ss", cap, name); return -1; }
            game_blip_t *b = isb ? &hd->blips[hd->blip_count] : &hd->erase[hd->erase_count];
            b->x = (int16_t)a[0]; b->y = (int16_t)a[1]; b->wide = (uint8_t)a[2]; b->pen = (uint8_t)a[3]; b->rec = (uint8_t)a[4];
            if (isb) hd->blip_count++; else hd->erase_count++;
            continue;
        }
        if (!strcmp(name, "eject_at")) { hd->cp_eject_armed = (uint8_t)(frame >= v); hd->cp_eject_key = (uint8_t)(frame == v); continue; }   /* the E key is pressed on this frame */
        if (!strcmp(name, "target")) {
            int x, y;
            if (sscanf(val, "%d:%d", &x, &y) != 2) { snprintf(err, errlen, "--hud-test: target=x:y"); return -1; }
            hd->target_box_x = (int16_t)x; hd->target_box_y = (int16_t)y; hd->target_box_visible = 1; continue;
        }
        int found = 0;
        if (*end == 0) for (size_t i = 0; i < sizeof FIELDS / sizeof *FIELDS; i++)
            if (!strcmp(FIELDS[i].name, name)) { put(hd, &FIELDS[i], v); found = 1; break; }
        if (!found) { snprintf(err, errlen, "--hud-test: unknown name or bad value '%s=%s'", name, val); return -1; }
    }
    if (!named_tsel) s->hud.tgt_sel = s->hud.cp_sel;   /* the viewed record is the player unless tsel= says otherwise */
    if (!named_redraw) {   /* R by the core's rule: 0 until a writer runs (rreq=1: the helper sets 3), one less after each pass that runs the panel block (the draws of a pass see it before the decrement) */
        if (frame != t->last_frame) {
            if (rreq) cockpit_redraw_request(&t->ck);
            cockpit_redraw_pass(&t->ck, s->camera.view == GAME_VIEW_COCKPIT);
            t->last_frame = frame;
        }
        s->hud.cp_redraw = t->ck.redraw_now;
    }
    return 0;
}
