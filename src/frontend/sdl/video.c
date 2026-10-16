/* src/frontend/sdl/video.c - 320x200 framebuffer, integer-scaled to the window.
 * Splash: the picture. Flight (milestone 2): sky and ground, the scene's objects as flat-shaded
 * polygons from the world module, and the cockpit panel pictures over the bottom area. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "stb_image.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#include "video.h"
#include "../../core/assets.h"
#include "../../core/fx.h"
#include "../../core/world.h"
#include "raster.h"
#include "hud.h"
#include "fade.h"
#include "text.h"
#include "assets.h"
#include "effects.h"

#define FB_W 320
#define FB_H 200

struct video {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    uint32_t fb[FB_W * FB_H];           /* 0xRRGGBB */
    uint8_t *splash_rgb;                /* decoded picture, 3 bytes per pixel, or NULL */
    int splash_w, splash_h;
    uint16_t splash_loaded_id;
    int splash_loaded;
    char pix_names[64][CORE_ASSET_NAME_LEN];   /* from pix/index.txt */
    int pix_count;                              /* -1 = not loaded yet */
    /* flight mode */
    raster_t ras;
    uint32_t pal[256];                          /* 0xRRGGBB of flight.pal; entries 16.. are black (note 04) */
    int pal_loaded;
    const world_t *world;                       /* the core's world (game_world), for polygon lists; may be NULL */
    int warned;                                 /* bit mask of one-time diagnostics */
    hud_t *hud;                                 /* cockpit overlay (hud.h), created on the first flight frame */
    int hud_tried;
    uint32_t *comp;                             /* window-resolution composite (world and overlay, letterboxed), 0xRRGGBB */
    int comp_w, comp_h;                         /* the window's size in pixels */
    int scale;                                  /* the integer factor of the 320x200 frame inside the window */
    uint32_t fb_world[FB_W * FB_H];             /* the world as palette colours, before the overlay */
    const uint8_t *layer_keep;                  /* the overlay layer of the last flight frame presented (hud_draw's buffer: it stays valid until the next hud_draw) */
    int have_frame;                             /* a flight frame was rendered: ras.px and layer_keep hold the last presented picture (scene.flight_blank keeps showing it) */
    const uint8_t *layer;                       /* the overlay pens of this frame, or NULL */
    uint8_t below[FB_W * FB_H];                 /* the last frame's overlay pens (0 where nothing was drawn): the page's panel rows that the next frame's alternate blit reads (raster_t.page_below) */
    int below_ok;
    uint32_t flash_fb[FB_W * FB_H];             /* the last uniform orange frame presented (tests: --dump-flash) */
    int flash_seen;
    fade_t fade;                                /* the display state of the fade: V:4A958 and its counter (fade.h) */
    uint8_t fade_pal[16 * 16 * 3];              /* palette/fade.pal: 16 steps of 16 colours */
    int fade_pal_ok;
    int blanks_x100;                            /* tests: the blanks one presented frame stands for, in hundredths (625 = one game step); 0: the wall clock at 50 Hz */
    uint32_t blank_acc;                         /* hundredths of a blank not yet stepped */
    uint32_t last_ms;
    /* text screens (note 08 sections 3 and 4): the scene's text lines as pens on their own 320x200 layer (255 = nothing), drawn after the picture and the cockpit overlay */
    raster_t tras;                              /* the text pens of this frame: px 255 where no glyph pixel is */
    int text_on;                                /* the layer holds this frame's text */
    int text_menu;                              /* the text's pens index the menu palette (regime 0, note 08 4.2), else the flight palette v->pal */
    uint8_t font[MENU_FONT_BYTES];
    int font_state;                             /* 0 not loaded, 1 loaded, -1 missing (reported once) */
    uint32_t menu_pal[32];                      /* palette/menu.pal through the colour curve; all 32 entries, a text pen is 0..15 */
    int menu_pal_state;                         /* 0 not loaded, 1 loaded, -1 missing */
    effects_tables_t fxtab;                     /* the burst tables (effects.h; tables/burst.bin), loaded on the first flight frame */
    int fxtab_tried;
};

/* The integer factor of the 320x200 frame in a window: the largest that fits, at least 1 (a smaller window crops). */
static int fit_scale(int w, int h) {
    int sx = w / FB_W, sy = h / FB_H, k = sx < sy ? sx : sy;
    return k < 1 ? 1 : k;
}

/* (Re)creates the composite buffer and the streaming texture for a window of w x h pixels. */
static int resize_target(video_t *v, int w, int h) {
    uint32_t *c = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof *c);
    SDL_Texture *t = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!c || !t) { free(c); if (t) SDL_DestroyTexture(t); return -1; }
    free(v->comp);
    if (v->tex) SDL_DestroyTexture(v->tex);
    v->comp = c; v->tex = t; v->comp_w = w; v->comp_h = h;
    v->scale = fit_scale(w, h);
    return 0;
}

/* The window is resizable; width and height are its initial size in pixels (the frame is scaled by the largest integer factor that fits and
 * letterboxed in black, so a window of 320k x 200k shows it exactly k times). */
video_t *video_create(int width, int height, char *err, size_t errlen) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { snprintf(err, errlen, "SDL_Init: %s", SDL_GetError()); return NULL; }
    video_t *v = (video_t *)calloc(1, sizeof *v);
    if (width < 1) width = FB_W;
    if (height < 1) height = FB_H;
    v->win = SDL_CreateWindow("F/A-18 Interceptor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_RESIZABLE);
    v->ren = v->win ? SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE) : NULL;
    if (!v->win || !v->ren || resize_target(v, width, height) != 0) { snprintf(err, errlen, "SDL: %s", SDL_GetError()); video_destroy(v); return NULL; }
    v->pix_count = -1;
    return v;
}

static void load_splash(video_t *v, uint16_t id, const game_loader_t *loader) {
    if (v->splash_loaded && v->splash_loaded_id == id) return;
    free(v->splash_rgb); v->splash_rgb = NULL;
    if (v->pix_count < 0)
        v->pix_count = core_assets_load_index(loader, "pix/index.txt", v->pix_names, 64);
    if (id >= v->pix_count) return;
    char path[64];
    snprintf(path, sizeof path, "pix/%s.png", v->pix_names[id]);
    uint8_t *png; size_t len;
    if (loader->read(loader->user, path, &png, &len) != 0) return;
    int ch;
    v->splash_rgb = stbi_load_from_memory(png, (int)len, &v->splash_w, &v->splash_h, &ch, 3);
    loader->release(loader->user, png);
    v->splash_loaded = 1; v->splash_loaded_id = id;
}


/* ---- flight mode -------------------------------------------------------------------------- */

static void warn_once(video_t *v, int bit, const char *msg) {
    if (v->warned & bit) return;
    v->warned |= bit;
    fprintf(stderr, "flight: %s\n", msg);
}

/* The palette: fade step 15 - level of palette/fade.pal through the measured colour curve (fade.h); entries 16 and up are black (note 04). */
static void load_flight_palette(video_t *v, const game_loader_t *loader) {
    if (v->pal_loaded) return;
    v->pal_loaded = 1;
    uint8_t *buf; size_t len;
    fade_init(&v->fade);
    v->last_ms = SDL_GetTicks();
    if (loader->read(loader->user, "palette/fade.pal", &buf, &len) != 0) {
        /* no palette: fall back to a grey ramp so the picture stays readable */
        warn_once(v, 1, "assets/palette/fade.pal is missing; using a grey ramp (run the extractor)");
        for (int i = 0; i < 16; i++) v->pal[i] = (uint32_t)FADE_CURVE[i] * 0x010101u;
        return;
    }
    if (len == sizeof v->fade_pal) { memcpy(v->fade_pal, buf, len); v->fade_pal_ok = 1; fade_palette(v->fade_pal, fade_step(&v->fade), v->pal); }
    loader->release(loader->user, buf);
}

/* The fade level steps at the 50 Hz blank rate (the core publishes only the target): in a run with a fixed frame count (tests) one presented frame is one game
 * step of 6.25 blanks, live the wall clock decides. The palette is reloaded when the level changes. */
static void fade_advance(video_t *v, const game_scene_t *scene) {
    if (!v->fade_pal_ok) return;
    if (v->blanks_x100) v->blank_acc += (uint32_t)v->blanks_x100;
    else {
        uint32_t now = SDL_GetTicks();
        v->blank_acc += (now - v->last_ms) * 5u;
        v->last_ms = now;
    }
    int target = scene->fade_target, changed = 0;   /* the core's V:4A959 for the frame in every mode (scene.h): 0, 10, 15, or 2 in a blackout */
    for (; v->blank_acc >= 100; v->blank_acc -= 100) changed |= fade_blank(&v->fade, target);
    if (changed) fade_palette(v->fade_pal, fade_step(&v->fade), v->pal);
}

/* The sink of world_draw_entry: every primitive the draw stage emits goes to the plane-aware rasterizer (note 03, "Primitive output"). The near
 * body arrives from the core as ordinary primitives of the common filler (rejected by the top row of its box below the window's last row, otherwise cut at that row; the thin
 * triangle of a 167-row view is a line, world.c out_poly). */
static void fill_sink(void *user, const world_out_t *o) {
    raster_t *r = &((video_t *)user)->ras;
    switch (o->kind) {
    case WORLD_OUT_POLY:  if (o->alt) raster_fill_alt(r, o->xy, o->n, o->mask, o->pen); else raster_fill_polygon_planes(r, o->xy, o->n, o->mask, o->pen, o->pattern, o->toggle); break;
    case WORLD_OUT_LINE:  raster_line_planes(r, o->xy[0], o->xy[1], o->xy[2], o->xy[3], o->mask, o->pen); break;
    case WORLD_OUT_POINT: raster_point_sprite(r, o->xy[0], o->xy[1], o->sprite, o->mask, o->pen); break;
    case WORLD_OUT_DISC:  raster_disc(r, o->xy[0], o->xy[1], o->radius, o->mask, o->pen); break;
    }
}

static void render_flight(video_t *v, const game_scene_t *scene, const game_loader_t *loader) {
    if (!v->world) warn_once(v, 2, "no world data (models/index.txt missing); only sky and ground are drawn");
    const game_camera_t *cam = &scene->camera;
    raster_clear(&v->ras, 16);   /* explicit black: pens 0..15 are the palette, 16 and up are black */
    raster_set_row_limit(&v->ras, effects_window_row(scene, hud_window_last_row(cam)));   /* the jolt moves the window's last row by one scanline (note 07 13.3) */
    v->ras.page_below = v->below_ok ? v->below : NULL;   /* the panel is drawn after the world: at stage [90] the page still holds the previous frame's */   /* 144, 167 or 179 by view and head (note 06) */
    /* Sky fill [60] and sea mesh [68]: the core's polygon and mesh polygons through the blitter rules of raster.c (note 04) */
    raster_sky_scene(&v->ras, &scene->sky);

    if (!v->fxtab_tried) { v->fxtab_tried = 1; effects_load_tables(loader, &v->fxtab); }
    effects_t fx;
    effects_begin(&fx, &v->ras, scene, &v->fxtab);
    const unsigned pool_at = effects_pool_position(scene);
    int pool_done = 0;   /* the pool is drawn by the gun routine, which the frame routine calls before the entity pass or after it (scene.gun_first, note 07 7.3): before the first entry of the entity pass, or after the last */
    if (v->world && scene->entry_count) {
        /* The entries come in the original's draw order (scene.h, note 04a): queue A, queue B, the specks of the near-ground pass, the direct
         * entities, queue C with its entities. The core decided what is drawn (entry->draw: the drawer ran its primitive layer; the player's
         * cockpit block and near body show nothing) and computed every result; the frontend only fills the polygons the core's pipeline
         * keeps. Within an object the BSP order is kept. The combat effects (effects.h) join it where the original draws them: the tracer pool where the gun routine runs (before the first entry of the entity pass or after the last: scene.gun_first, note 07 7.3), an entity's dying burst and trail after its model (note 04a 8d). */
        int n = scene->entry_count < GAME_MAX_ENTRIES ? scene->entry_count : GAME_MAX_ENTRIES;
        for (int i = 0; i < n; i++) {
            const game_entry_t *e = &scene->entries[i];
            if (!pool_done && (unsigned)i == pool_at) { pool_done = 1; effects_pool(&fx, scene); }
            switch (e->kind) {   /* stage [90]: the specks of the near-ground pass take plane 2 away from the pixels they cover (note 04) */
            case GAME_ENTRY_SPECK: raster_point_sprite(&v->ras, e->sv[0], e->sv[1], e->variant, 4, 3); break;   /* the pass's block: mask 4, colour 3 */
            case GAME_ENTRY_SPECK_TRI: raster_ground_triangle(&v->ras, e->sv, 0); break;
            case GAME_ENTRY_SPECK_FILL: raster_ground_triangle(&v->ras, e->sv, 1); break;
            default: {
                world_draw_info_t info;
                memset(&info, 0, sizeof info);
                world_draw_entry(v->world, e, cam, fill_sink, v, &info);
                if (e->kind == GAME_ENTRY_ENTITY && e->draw && !info.aborted && !info.ends_ffff) effects_tail(&fx, scene, e->id, e->lod);
            }
            }
        }
    }
    if (!pool_done) effects_pool(&fx, scene);
    for (int i = 0; i < FB_W * FB_H; i++) v->fb_world[i] = v->pal[v->ras.px[i]];
    /* the cockpit overlay (hud.h): HUD, panel, warning line, external strip, map grid, as pens on the 320x200 layer */
    if (!v->hud_tried) { v->hud_tried = 1; v->hud = hud_create(loader); }
    if (v->hud) {
        hud_frame(v->hud, scene);
        v->layer = hud_draw(v->hud, scene);
        if (v->layer) { for (int i = 0; i < FB_W * FB_H; i++) v->below[i] = v->layer[i] == HUD_NONE ? 0 : (uint8_t)(v->layer[i] & 15); v->below_ok = 1; }
    }
}

/* World plus overlay at w x h with the integer factor k: every original pixel (x, y) is a k x k cell at the window offset that centres the frame
 * (black bars around it), so the overlay stays aligned with the world at any size. k = 0 means the frame itself (320x200, no bars). */
static void compose(const video_t *v, uint32_t *dst, int w, int h, int k) {
    if (k < 1) k = 1;
    int fw = FB_W * k, fh = FB_H * k;
    int ox = (w - fw) / 2, oy = (h - fh) / 2;       /* negative when the window is smaller than the frame: the frame is cropped */
    for (int i = 0; i < w * h; i++) dst[i] = 0;
    for (int y = 0; y < h; y++) {
        int wy = y - oy;
        if (wy < 0 || wy >= fh) continue;
        int ly = wy / k;
        const uint32_t *wr = v->fb_world + ly * FB_W;
        const uint8_t *lr = v->layer ? v->layer + ly * HUD_W : NULL;
        for (int x = 0; x < w; x++) {
            int wx = x - ox;
            if (wx < 0 || wx >= fw) continue;
            int lx = wx / k, pen = lr ? lr[lx] : HUD_NONE;
            dst[y * w + x] = pen == HUD_NONE ? wr[lx] : v->pal[pen & 15];
            if (v->text_on) { int tp = v->tras.px[ly * FB_W + lx]; if (tp != 255) dst[y * w + x] = (v->text_menu ? v->menu_pal : v->pal)[tp & 15]; }
        }
    }
}

static void present(video_t *v) {
    int w, h;
    SDL_GetRendererOutputSize(v->ren, &w, &h);
    if ((w != v->comp_w || h != v->comp_h) && w > 0 && h > 0) resize_target(v, w, h);   /* the window was resized */
    compose(v, v->fb, FB_W, FB_H, 1);
    compose(v, v->comp, v->comp_w, v->comp_h, v->scale);
    SDL_UpdateTexture(v->tex, NULL, v->comp, v->comp_w * 4);
    SDL_RenderClear(v->ren);
    SDL_RenderCopy(v->ren, v->tex, NULL, NULL);
    SDL_RenderPresent(v->ren);
}

/* The end of the crash hold (note 05 17.17 (xii)): one uniform orange frame, one 50 Hz blank (20 ms), before the frame of the step. The palette block holds
 * 0x0FB2 in all 32 colour registers; the level of each channel goes through the measured curve like every pen (R 15, G 11, B 2; the measured frame is (236, 159, 0), so level 15 maps to 236 here). */
#define FLASH_RGB ((uint32_t)236 << 16 | (uint32_t)FADE_CURVE[0xB] << 8 | FADE_CURVE[0x2])   /* R: level 15 measured on the flash frame (note 06, open question 8) */
static void present_flash(video_t *v) {
    for (int i = 0; i < FB_W * FB_H; i++) v->fb_world[i] = FLASH_RGB;
    v->layer = NULL;
    present(v);
    memcpy(v->flash_fb, v->fb, sizeof v->flash_fb);
    v->flash_seen = 1;
    SDL_Delay(20);
}

/* The menu palette (palette/menu.pal, 32 entries of x17-scaled levels) through the measured colour curve like every pen (note 08 4.1). */
static int load_menu_palette(video_t *v, const game_loader_t *loader) {
    if (v->menu_pal_state) return v->menu_pal_state > 0;
    uint8_t raw[3 * ASSETS_PALETTE_ENTRIES];
    char err[128];
    if (assets_load_menu_palette(loader, raw, err, sizeof err) != 0) {
        v->menu_pal_state = -1;
        fprintf(stderr, "text: %s\n", err);
        return 0;
    }
    for (int i = 0; i < 32; i++)
        v->menu_pal[i] = (uint32_t)FADE_CURVE[(raw[3 * i] / 17) & 15] << 16 | (uint32_t)FADE_CURVE[(raw[3 * i + 1] / 17) & 15] << 8 | FADE_CURVE[(raw[3 * i + 2] / 17) & 15];
    v->menu_pal_state = 1;
    return 1;
}

/* The scene's text lines on the text layer (note 08 section 3): every line with its characters shown so far, set pixels only, in the line's pen. The layer is
 * rebuilt every frame from the scene, so a change of palette regime (a cut, 4.4) or of screen never leaves a line of the previous one behind. */
/* The numerals 1 to 4 of the location map (scene.map_marks): 3x5 glyphs, rows top to bottom, bit 2 the left column. */
static const uint8_t MARK_GLYPH[5][5] = { { 0, 0, 0, 0, 0 }, { 2, 2, 2, 2, 2 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 }, { 5, 5, 7, 1, 1 } };

static void draw_map_marks(video_t *v, const game_scene_t *scene) {
    for (int m = 0; m < scene->map_mark_count && m < GAME_MAX_MAP_MARKS; m++) {
        const game_map_mark_t *k = &scene->map_marks[m];
        if (k->digit < 1 || k->digit > 4) continue;
        if (k->dot_x >= 0 && k->dot_x < FB_W && k->dot_y >= 0 && k->dot_y < FB_H) v->tras.px[k->dot_y * FB_W + k->dot_x] = 2;   /* the marker beside the number: one pixel in pen 2 */
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 3; c++) {
                int x = k->x + c, y = k->y + r;
                if ((MARK_GLYPH[k->digit][r] >> (2 - c) & 1) && x >= 0 && x < FB_W && y >= 0 && y < FB_H) v->tras.px[y * FB_W + x] = k->pen & 15;
            }
    }
}

static void draw_scene_text(video_t *v, const game_scene_t *scene, const game_loader_t *loader, int menu_regime) {
    v->text_on = 0;
    if (!scene->text_count && !scene->map_mark_count) return;
    if (v->font_state == 0) {
        char err[160];
        v->font_state = assets_load_menu_font(loader, v->font, err, sizeof err) == 0 ? 1 : -1;
        if (v->font_state < 0) fprintf(stderr, "text: %s\n", err);
    }
    if (v->font_state < 0 || (menu_regime && !load_menu_palette(v, loader))) return;
    memset(v->tras.px, 255, sizeof v->tras.px);
    int n = scene->text_count < GAME_MAX_TEXT ? scene->text_count : GAME_MAX_TEXT;
    for (int i = 0; i < n; i++) text_draw(&v->tras, &scene->text[i], v->font);
    if (!menu_regime) draw_map_marks(v, scene);
    v->text_on = 1;
    v->text_menu = menu_regime;
}

void video_render(video_t *v, const game_scene_t *scene, const game_loader_t *loader) {
    if (scene->flash_frames) present_flash(v);
    memset(v->fb_world, 0, sizeof v->fb_world);
    v->layer = NULL;
    v->text_on = 0;
    load_flight_palette(v, loader);
    fade_advance(v, scene);   /* the fade level is display state and runs in every mode: a cut to the menu palette ignores it, the flight palette follows it */
    if (scene->mode == GAME_MODE_SPLASH) {
        load_splash(v, scene->splash_asset, loader);
        if (v->splash_rgb) {
            int w = v->splash_w < FB_W ? v->splash_w : FB_W, h = v->splash_h < FB_H ? v->splash_h : FB_H;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    const uint8_t *p = v->splash_rgb + (y * v->splash_w + x) * 3;
                    v->fb_world[y * FB_W + x] = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
                }
        }
    }
    else {
        const int play = scene->mode == GAME_MODE_FLIGHT || scene->mode == GAME_MODE_PAUSED;
        /* the palette regime (note 08 4.2): regime 0 is the menu palette on a cleared screen, text only (a cut: no fade); regime 1 the flight palette with the
         * scene behind or beside the text. Flight and the pause are always the flight palette. */
        const int menu_regime = !play && scene->palette_regime == 0;
        if (!menu_regime && !scene->flight_blank) { render_flight(v, scene, loader); v->layer_keep = v->layer; v->have_frame = 1; }
        else if (!menu_regime && v->have_frame) {   /* flight_blank (V:4A84D = 0: an ESC being taken, the pause): the world is not redrawn, the last picture stays in the planes and is shown through the palette of the
                                                     * moment, so it fades out or dims with the fade target (note 05 17.17 rule 9, note 08 5.11, 5.14) */
            for (int i = 0; i < FB_W * FB_H; i++) v->fb_world[i] = v->pal[v->ras.px[i]];
            v->layer = v->layer_keep;
        }
        draw_scene_text(v, scene, loader, menu_regime);
    }
    present(v);
}

void video_set_world(video_t *v, const world_t *w) { if (v) v->world = w; }
void video_set_blanks_per_frame(video_t *v, int x100) { if (v) v->blanks_x100 = x100; }
void video_set_window_size(video_t *v, int w, int h) { if (v && v->win && w > 0 && h > 0) SDL_SetWindowSize(v->win, w, h); }

int video_dump_ppm(video_t *v, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", FB_W, FB_H);
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint8_t rgb[3] = { (uint8_t)(v->fb[i] >> 16), (uint8_t)(v->fb[i] >> 8), (uint8_t)v->fb[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

int video_dump_flash_ppm(video_t *v, const char *path) {
    if (!v->flash_seen) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", FB_W, FB_H);
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint8_t rgb[3] = { (uint8_t)(v->flash_fb[i] >> 16), (uint8_t)(v->flash_fb[i] >> 8), (uint8_t)v->flash_fb[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

int video_dump_layer(video_t *v, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    for (int i = 0; i < FB_W * FB_H; i++) fputc(v->layer ? v->layer[i] : HUD_NONE, f);
    fclose(f);
    return 0;
}

int video_dump_window_ppm(video_t *v, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", v->comp_w, v->comp_h);
    for (int i = 0; i < v->comp_w * v->comp_h; i++) {
        uint8_t rgb[3] = { (uint8_t)(v->comp[i] >> 16), (uint8_t)(v->comp[i] >> 8), (uint8_t)v->comp[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

void video_destroy(video_t *v) {
    if (!v) return;
    free(v->splash_rgb);
    hud_destroy(v->hud);
    free(v->comp);
    if (v->tex) SDL_DestroyTexture(v->tex);
    if (v->ren) SDL_DestroyRenderer(v->ren);
    if (v->win) SDL_DestroyWindow(v->win);
    free(v);
    SDL_Quit();
}
