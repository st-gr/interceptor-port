/* tests/frontend/effects_driver.c - the combat effects without a window (test_effects.py): reads commands from stdin, builds a scene, runs effects.c on a raster and prints
 * every pixel that was written as "x y pen". Commands:
 *   cam X Y Z PITCH ROLL YAW [VIEW [HUD_STEP]]   the camera (position units, angle units); VIEW 0 cockpit (default)
 *   limit N                                      the window's last row
 *   jolt D                                       scene.jolt_row
 *   fx KIND X Y Z AGE REC OWNER MODEL PHASE      one effect of the scene (KIND is the game_fx_kind_t number)
 *   trail COUNT INDEX LENGTH OOC x y z (6 times) VX VY VZ PX PY PZ SPEED   the trail ring and the followed record's velocity, position and speed word; followed REC via "followed"
 *   followed REC                                 camera.trail
 *   tab                                          followed by 72 shape words, 108 offset bytes, 27 disc bytes, 60 pen bytes and 32 cycle words (invented tables)
 *   window BASE                                  prints the jolted window row for the base row
 *   gunfirst N                                   scene.gun_first
 *   entry KIND TIER                              appends a scene entry (kind: game_entry_kind_t, tier: game_tier_t)
 *   poolpos                                      prints "poolpos N": effects_pool_position of the scene
 *   pool | tail REC LOD | trail_draw             the draw calls; each prints "draw N" (the count they return)
 *   dump                                         prints the pixels and clears the raster */
#include "../../src/frontend/sdl/effects.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static raster_t R;
static game_scene_t S;
static effects_tables_t T;

static void dump(void) {
    for (int y = 0; y < RASTER_H; y++)
        for (int x = 0; x < RASTER_W; x++)
            if (R.px[y * RASTER_W + x] != 16) printf("px %d %d %d\n", x, y, R.px[y * RASTER_W + x] & 15);
    raster_clear(&R, 16);
}

int main(void) {
    char cmd[32];
    raster_clear(&R, 16);
    R.row_limit = 143;
    S.mode = GAME_MODE_FLIGHT;
    S.camera.trail = 0xFF;
    while (scanf("%31s", cmd) == 1) {
        if (!strcmp(cmd, "cam")) {
            int x, y, z, p, r, w, v = 0, hs = 0;
            char line[128];
            if (scanf("%d %d %d %d %d %d", &x, &y, &z, &p, &r, &w) != 6) return 2;
            if (fgets(line, sizeof line, stdin)) sscanf(line, "%d %d", &v, &hs);
            S.camera.pos.x = x; S.camera.pos.y = y; S.camera.pos.z = z; S.camera.pitch = p; S.camera.roll = r; S.camera.yaw = w; S.camera.view = (game_view_t)v; S.camera.hud_step = (int8_t)hs;
        } else if (!strcmp(cmd, "limit")) { int n; if (scanf("%d", &n) != 1) return 2; R.row_limit = n;
        } else if (!strcmp(cmd, "jolt")) { int d; if (scanf("%d", &d) != 1) return 2; S.jolt_row = (int8_t)d;
        } else if (!strcmp(cmd, "fx")) {
            int k, x, y, z, a, rec, own, mod, ph;
            if (scanf("%d %d %d %d %d %d %d %d %d", &k, &x, &y, &z, &a, &rec, &own, &mod, &ph) != 9) return 2;
            game_effect_t *f = &S.effects[S.effect_count++];
            f->kind = (game_fx_kind_t)k; f->pos.x = x; f->pos.y = y; f->pos.z = z; f->age = (uint16_t)a; f->rec = (uint8_t)rec; f->owner = (uint8_t)own; f->model = (uint16_t)mod; f->phase = (int16_t)ph;
        } else if (!strcmp(cmd, "trail")) {
            int c, i, l, o;
            if (scanf("%d %d %d %d", &c, &i, &l, &o) != 4) return 2;
            S.trail.count = (uint8_t)c; S.trail.index = (uint8_t)i; S.trail.length = (uint8_t)l; S.trail.ooc = (uint8_t)o;
            for (int k = 0; k < 6; k++) for (int a = 0; a < 3; a++) if (scanf("%d", &S.trail.ring[k][a]) != 1) return 2;
            for (int a = 0; a < 3; a++) if (scanf("%d", &S.trail.vel[a]) != 1) return 2;
            for (int a = 0; a < 3; a++) if (scanf("%d", &S.trail.pos[a]) != 1) return 2;
            if (scanf("%d", &S.trail.speed) != 1) return 2;
        } else if (!strcmp(cmd, "followed")) { int r; if (scanf("%d", &r) != 1) return 2; S.camera.trail = (uint8_t)r;
        } else if (!strcmp(cmd, "tab")) {
            int v;
            for (int i = 0; i < 8; i++) for (int k = 0; k < 3; k++) for (int a = 0; a < 3; a++) { if (scanf("%d", &v) != 1) return 2; T.shape[i][k][a] = (int16_t)v; }
            for (int g = 0; g < 3; g++) for (int j = 0; j < 12; j++) for (int a = 0; a < 3; a++) { if (scanf("%d", &v) != 1) return 2; T.off[g][j][a] = (int8_t)v; }
            for (int j = 0; j < 9; j++) for (int a = 0; a < 3; a++) { if (scanf("%d", &v) != 1) return 2; T.disc[j][a] = (int8_t)v; }
            for (int t = 0; t < 5; t++) for (int j = 0; j < 12; j++) { if (scanf("%d", &v) != 1) return 2; T.pen[t][j] = (uint8_t)v; }
            for (int i = 0; i < 32; i++) { if (scanf("%d", &v) != 1) return 2; T.cycle[i] = (uint16_t)v; }
            T.loaded = 1;
        } else if (!strcmp(cmd, "star")) {   /* the flare star: 18 words (3 strokes of 2 points of x y z); marks the tables loaded */
            int v;
            for (int k = 0; k < 3; k++) for (int e = 0; e < 2; e++) for (int a = 0; a < 3; a++) { if (scanf("%d", &v) != 1) return 2; T.star[k][e][a] = (int16_t)v; }
            T.loaded = 1;
        } else if (!strcmp(cmd, "window")) {
            int b; if (scanf("%d", &b) != 1) return 2;
            printf("window %d\n", effects_window_row(&S, b));
        } else if (!strcmp(cmd, "gunfirst")) { int g; if (scanf("%d", &g) != 1) return 2; S.gun_first = (uint8_t)g;
        } else if (!strcmp(cmd, "entry")) {
            int k, t;
            if (scanf("%d %d", &k, &t) != 2) return 2;
            game_entry_t *g = &S.entries[S.entry_count++];
            memset(g, 0, sizeof *g); g->kind = (uint8_t)k; g->tier = (uint8_t)t; g->draw = 1;
        } else if (!strcmp(cmd, "poolpos")) { printf("poolpos %u\n", effects_pool_position(&S));
        } else if (!strcmp(cmd, "pool") || !strcmp(cmd, "tail") || !strcmp(cmd, "trail_draw")) {
            effects_t e;
            int a = 0, b = 0, n = 0;
            if (!strcmp(cmd, "tail")) { if (scanf("%d %d", &a, &b) != 2) return 2; }
            effects_begin(&e, &R, &S, &T);
            if (!strcmp(cmd, "pool")) effects_pool(&e, &S);
            else if (!strcmp(cmd, "tail")) n = effects_tail(&e, &S, a, b);
            else n = effects_trail(&e, &S.trail);
            printf("draw %d\n", n);
        } else if (!strcmp(cmd, "dump")) dump();
        else return 2;
    }
    return 0;
}
