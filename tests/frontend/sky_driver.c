/* tests/frontend/sky_driver.c - the sky fill and sea mesh stage for one logged camera state, without a window (tests/frontend/test_sky_frames.py).
 * Reads one state from stdin as "key value ..." lines, builds the scene's sky block with the core's pure functions (sky_polygon, mesh_compute, fx_clip) as
 * game.c does, draws it with raster_sky_scene and writes the 320x200 pens to the file named by "out". Keys:
 *   matrix m0..m8 | camy N | words pitch roll | flags side cockpit sea limit tinypen tinymask | pos x y z | alt A | zoom Z default D map M ownalt O phase P octant Q | tables DIR | out FILE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/core/sky.h"
#include "../../src/frontend/sdl/raster.h"

static sky_tables_t tab;
static game_sky_t sky;
static raster_t ras;

static int load(const char *dir)
{
    static const struct { const char *n; void *d; size_t sz; } f[] = {
        { "sea_zone.bin", tab.zone, SKY_ZONE_BYTES }, { "sea_zone2.bin", tab.zone2, SKY_ZONE2_BYTES }, { "mesh_scan.bin", tab.scan, MESH_SCAN_BYTES },
        { "mesh_far.bin", tab.far_tab, MESH_FAR_BYTES }, { "mesh_cells.bin", tab.cells, MESH_CELLS_BYTES } };
    char path[1024];
    for (size_t i = 0; i < sizeof f / sizeof f[0]; i++) {
        FILE *fp;
        snprintf(path, sizeof path, "%s/%s", dir, f[i].n);
        fp = fopen(path, "rb");
        if (!fp || fread(f[i].d, 1, f[i].sz, fp) != f[i].sz) { if (fp) fclose(fp); return -1; }
        fclose(fp);
    }
    return 0;
}

int main(void)
{
    char key[64], out[1024] = "", dir[1024] = "assets/tables";
    mesh_in_t in;
    int side = 0, cockpit = 1, limit = 144, pw = 0, rw = 0, have_tab = 0;
    sky_state_t st;
    sky_poly_t poly;
    memset(&in, 0, sizeof in);
    memset(&st, 0, sizeof st);
    memset(&sky, 0, sizeof sky);
    in.zoom = 0x80; in.zoom_default = 1;
    sky.tiny_pen = 13; sky.tiny_mask = 1;
    while (scanf("%63s", key) == 1) {
        if (!strcmp(key, "matrix")) { for (int i = 0; i < 9; i++) { int v; if (scanf("%d", &v) != 1) return 2; in.m[i] = (int16_t)v; } }
        else if (!strcmp(key, "camy")) { if (scanf("%d", &sky.cam_y) != 1) return 2; }
        else if (!strcmp(key, "words")) { if (scanf("%d %d", &pw, &rw) != 2) return 2; }
        else if (!strcmp(key, "flags")) { int s, c, z, l, tp, tm; if (scanf("%d %d %d %d %d %d", &s, &c, &z, &l, &tp, &tm) != 6) return 2; side = s; cockpit = c; sky.sea_zone = (uint8_t)z; limit = l; sky.tiny_pen = (uint8_t)tp; sky.tiny_mask = (uint8_t)tm; }
        else if (!strcmp(key, "pos")) { if (scanf("%d %d %d", &in.pos[0], &in.pos[1], &in.pos[2]) != 3) return 2; }
        else if (!strcmp(key, "alt")) { if (scanf("%d", &in.alt) != 1) return 2; }
        else if (!strcmp(key, "zoom")) { if (scanf("%d", &in.zoom) != 1) return 2; }
        else if (!strcmp(key, "default")) { if (scanf("%d", &in.zoom_default) != 1) return 2; }
        else if (!strcmp(key, "map")) { if (scanf("%d", &in.map) != 1) return 2; }
        else if (!strcmp(key, "ownalt")) { if (scanf("%d", &in.own_alt) != 1) return 2; }
        else if (!strcmp(key, "phase")) { if (scanf("%d", &in.phase) != 1) return 2; }
        else if (!strcmp(key, "octant")) { if (scanf("%d", &in.octant) != 1) return 2; }
        else if (!strcmp(key, "tables")) { if (scanf("%1023s", dir) != 1) return 2; }
        else if (!strcmp(key, "out")) { if (scanf("%1023s", out) != 1) return 2; }
        else return 2;
    }
    have_tab = load(dir) == 0;
    sky_polygon(&st, in.m, sky.cam_y, cockpit, side, pw, rw, &poly);
    sky.valid = 1;
    sky.sky_n = poly.n;
    for (int i = 0; i < 2 * poly.n; i++) sky.sky_xy[i] = poly.xy[i];
    if (have_tab && !sky.sea_zone) {
        static mesh_poly_t mp[MESH_MAX_POLYS];
        int n = mesh_compute(&tab, &in, mp, MESH_MAX_POLYS, NULL);
        for (int k = 0; k < n && sky.sea_count < GAME_MAX_SEA_POLYS; k++) {
            fx_vec_t v[MESH_MAX_VERTS];
            int c;
            for (c = 0; c < mp[k].n; c++) { v[c].x = mp[k].v[c][0]; v[c].y = mp[k].v[c][1]; v[c].z = mp[k].v[c][2]; }
            c = fx_clip(sky.sea[sky.sea_count].xy, GAME_SEA_POLY_VERTS, v, mp[k].n, 0);
            if (c >= 3) { sky.sea[sky.sea_count].n = (uint8_t)c; sky.sea_count++; }
        }
    }
    raster_clear(&ras, 0);
    raster_set_row_limit(&ras, limit);
    raster_sky_scene(&ras, &sky);
    if (out[0]) {
        FILE *f = fopen(out, "wb");
        if (!f) return 3;
        fwrite(ras.px, 1, sizeof ras.px, f);
        fclose(f);
    }
    printf("sky %d vertices, %d mesh polygons\n", sky.sky_n, sky.sea_count);
    return 0;
}
