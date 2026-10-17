/* hud_driver.c - drives hud.c frame by frame without a window (tests/frontend/test_hud.py).
 * usage: hud_driver ASSETS_DIR < script
 * Each input line is "TICK spec" where spec is a --hud-test spec (see hud.h) applied to a flight scene
 * in the cockpit view at that tick (camera fields in the spec change the view). For each line the driver
 * runs hud_frame and prints
 *   tick speed alt throttle fuel bar msg code eject_lit speed_draws alt_draws fuel_draws
 * (the panel's displayed figures after the frame's gating, the warning entry the scene publishes, its code word, the number of draws the
 * two digit caches made so far). The scene's n is the TICK of the line unless the spec names n. */
#include "../../src/frontend/sdl/hud.h"
#include "../../src/frontend/sdl/assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    char err[256] = "", line[2048];
    game_loader_t loader;
    if (argc < 2 || assets_open(argv[1], &loader, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); return 2; }
    hud_t *h = hud_create(&loader);
    hud_test_t *ht = hud_test_create(&loader);
    static game_scene_t sc;
    while (fgets(line, sizeof line, stdin)) {
        long tick; int off = 0;
        if (sscanf(line, "%ld %n", &tick, &off) < 1) continue;
        memset(&sc, 0, sizeof sc);
        sc.mode = GAME_MODE_FLIGHT; sc.tick = (uint32_t)tick; sc.camera.view = GAME_VIEW_COCKPIT;
        sc.hud.cp_valid = 1; sc.hud.cp_hud_on = 1; sc.hud.cp_n = sc.hud.cp_pass = (uint32_t)tick;
        char *spec = line + off;
        spec[strcspn(spec, "\r\n")] = 0;
        if (*spec && hud_test_apply(ht, &sc, (int)tick, spec, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); return 2; }
        hud_frame(h, &sc);
        printf("%ld %d %d %d %d %d %d %u %d %d %d %d\n", tick, h->speed_shown, h->alt_shown, h->throttle_shown, h->fuel_shown,
               h->fuel_bar_level, h->st.cp_msg_entry, h->st.cp_msg_code, h->eject_lit, h->speed_draws, h->alt_draws, h->fuel_draws);
    }
    hud_test_destroy(ht); hud_destroy(h); assets_close(&loader);
    return 0;
}
