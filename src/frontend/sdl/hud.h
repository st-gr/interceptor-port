/* src/frontend/sdl/hud.h - the cockpit overlay: HUD, instrument panel, warning line, external text strip.
 *
 * Source: docs/re/06-cockpit-hud.md ("note 06"). Everything here is drawn as pens (palette entries of
 * the in-flight palette) on a 320x200 layer in the original's bitmap coordinates (row 0 = the top of
 * the 3D window area, note 04). The layer is HUD_NONE where nothing is drawn. The frontend scales the
 * layer by the integer window factor and composites it over the world (video.c): every original pixel
 * becomes one scale x scale cell, so the overlay is aligned with the world viewport by construction and
 * keeps the original's pixel rules (glyph cells, tape ticks, pens) at any window size.
 *
 * The core owns the game state (docs/superpowers/specs: section 10): the frame counter n, the warning arbiter
 * with its tick generator, code word and latch, the HUD switch. They arrive in the scene (scene.hud.cp_*); this file keeps
 * only draw caches (the speed, altitude and fuel caches, the EJECT timer, the scope bitmap). R (V:4A8EF), the scope walk, the target marker and the lock symbols are the core's. The scene's raw cockpit block must be valid (cp_valid): nothing is drawn
 * without it.
 *
 * Per core step: hud_frame() once per new value of the scene's pass counter cp_pass (a no-op for the further presented frames of the same step;
 * a paused pass is a pass: n stands still but the per-pass state advances and the digit caches take the pause variant), then hud_draw(). */
#ifndef INTERCEPTOR_SDL_HUD_H
#define INTERCEPTOR_SDL_HUD_H
#include <stddef.h>
#include <stdint.h>
#include "../../core/game.h"
#include "../../core/assets.h"

#define HUD_W 320
#define HUD_H 200
#define HUD_NONE 0xFF

typedef struct hud {
    /* extracted assets (zero when missing; hud_create reports once) */
    int have_font, have_cockpit, have_panel;
    uint8_t font[64][5];          /* 3x5 glyphs, code - 0x20, pixels in bits 7..5 */
    uint8_t warn[54][28];         /* warning table: flag byte, 26 characters, one flag byte */
    uint8_t masks[340];           /* gear lamp, gear symbols, ball mask, ball table 2, compass strip */
    int8_t markers[112];          /* target markers: 12 then 44 (dx, dy) pairs */
    int8_t arcs[82];              /* lock circle arc (20 pairs and the terminator), then the range dot arc (19 pairs and the terminator) */
    uint8_t mapshapes[324];       /* map direction table (17 words), 16 direction shapes, the ship shape (triples ended by 0x80) */
    int have_arcs, have_mapshapes;
    int have_ttext;               /* tables/target_text.bin: the message line's label and name strings (note 07 12.8) */
    target_text_t ttext;
    int have_prompts;             /* tables/prompts.bin: the two fire prompts (note 06 "Target symbols", note 07 12.7) */
    prompts_t prompts;
    uint8_t inst[320 * 55];       /* panel picture, pens 0..15 (pens 9 and 11 already swapped) */
    uint8_t cowl[288 * 12];       /* cowl picture, pens */

    /* the layer */
    uint8_t px[HUD_W * HUD_H];
    int step, off;                /* view offset: step and 16 * step pixels (note 06, "Head views") */
    int dy;                       /* the jolt's row offset d (V:4A990, note 07 13.3): every HUD and panel row is drawn d rows lower (V:4A9D0 = 40 d shifts the row addresses); 0 outside the cockpit view */
    uint8_t scope[HUD_W * HUD_H]; /* the radar scope as the original's bitmap keeps it between walks (note 06 "Radar scope", note 07 12.3): the own-ship plus, the cone lines and the blips, with the pen-0 erases, at final coordinates; HUD_NONE where the panel picture shows */

    /* state advanced by hud_frame (draw caches and redraw timers only) */
    uint32_t tick;                /* n (V:4A992) of the drawn pass, from the scene */
    uint32_t pass;                /* the scene's pass counter of the last hud_frame advance (paused passes count) */
    int started;                  /* hud_frame has seen a flight frame */
    int speed_cache, speed_flag, speed_shown;     /* V:4A9B0 gate (value in knots) */
    int alt_cache, alt_flag, alt_shown;           /* V:4A9B8 gate (feet) */
    int fuel_cache, fuel_flag;                    /* V:4A9AE gate (pounds; the flag is bit 15 of the word in the original) */
    int fuel_draws;                               /* draws the fuel cache made (tests) */
    int speed_draws, alt_draws;                   /* number of draws the two caches made (tests) */
    int throttle_shown, fuel_shown;               /* throttle percent, fuel digits (pounds), window-gated */
    int fuel_bar_level;
    int eject_timer;                              /* V:4A8FA */
    int eject_lit;                                /* the EJECT lamp is lit on this draw */
    game_hud_t st;                                /* the raw cockpit block of the scene in force for this frame */
} hud_t;

/* Loads the font, the cockpit tables and the two pictures from the asset loader. Missing pieces are
 * reported once on stderr; whatever is present is drawn. */
hud_t *hud_create(const game_loader_t *loader);
void   hud_destroy(hud_t *h);

/* The last 3D window row for a view: 144, 167 or 179 (note 06, "Head views" and "View kinds, settled"). */
int    hud_window_last_row(const game_camera_t *cam);

void   hud_frame(hud_t *h, const game_scene_t *scene);
/* Draws the overlay for the scene's view into the layer and returns it (HUD_W * HUD_H pens); all HUD_NONE without cp_valid. */
const uint8_t *hud_draw(hud_t *h, const game_scene_t *scene);

/* ---- helpers shared with panel.c ---- */
void hud_clear(hud_t *h);
void hud_set_step(hud_t *h, int step);
void hud_plot(hud_t *h, int x, int y, int pen);               /* x and y before the view offset; clipped like the original's plotters */
int  hud_get(const hud_t *h, int x, int y);                   /* pen at (x, y) before the offset, HUD_NONE if none or outside */
void hud_hline(hud_t *h, int x0, int x1, int y, int pen);     /* final row y (note 06 lists the row actually drawn) */
void hud_glyph(hud_t *h, int ch, int x, int y, int fg, int bg);  /* 3x5 cell; bg < 0 writes only the glyph pixels */
/* Right aligned decimal in len slots at xs[] (glyph x), blank leading slots, at least one digit. */
void hud_number(hud_t *h, long v, const int *xs, int len, int y, int fg, int bg);
void hud_text(hud_t *h, const char *s, const int *xs, int y, int fg, int bg);   /* xs[i] for character i; a space is skipped for bg < 0 */

/* Scene injection for tests (main.c --hud-test): "name=value,name=value,..." replaces the scene's whole raw cockpit block by
 * zeros plus the named fields (the cp_ fields without the prefix: class, sel, stock, speed_w, y, heading80, n, hud_on, msg_entry,
 * msg_phase, designated, range, pipper_x, ...; n defaults to the frame number, hud_on to 1), plus ball=x0:y0:x1:y1:x2:y2 (the attitude ball's sky triangle), cam_pitch, cam_roll,
 * cam_yaw, cam_x, cam_y, cam_z, view, head, ring, eject_at=N (the eject key is pressed on frame N), target=x:y,
 * walk=1 (the scope walk ran in this pass) with blip=x:y:wide:pen and erase=x:y:wide (repeatable; the scope keeps its pixels between walks), redraw=R (V:4A8EF; by default the core's rule (cockpit.c) is played: 3 on the first frame and after a frame
 * that names rreq=1 (a writer of the helper 0x1EFF4 ran), counting down in the cockpit view), marker=KIND:x:y, circle=x:y, dot=K, pip=1, prompt=N, number=TEXT, mark=rec:x:y:shape:pen (the map view's marks as the core
 * lists them, repeatable), jolt_row=d. Returns 0 or -1 with a message in err. See hud.c for the full list. */
typedef struct hud_test hud_test_t;   /* the harness state: the core's cockpit_t for the R rule */
hud_test_t *hud_test_create(const game_loader_t *loader);   /* loader: for tables/head_step.bin (may be NULL) */
void hud_test_destroy(hud_test_t *t);
int hud_test_apply(hud_test_t *t, game_scene_t *s, int frame, const char *spec, char *err, size_t errlen);
/* After the scene's sky was recomputed for the overridden camera: applies the spec's ball=... polygon. */
void hud_test_post(hud_test_t *t, game_scene_t *s);

#endif
