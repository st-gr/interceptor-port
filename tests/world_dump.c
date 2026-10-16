/* tests/world_dump.c - scene-dump driver: loads the real assets (default ./assets, or argv[1]),
 * places the camera at the carrier and prints the scene summary. Not a test (no test_ prefix);
 * exits 0 with "skipped" when the assets are absent. Also walks every model at a few viewers as
 * a smoke test of the real data, and reproduces the display lists logged from the reference emulator
 * (extracted/captures/logs/lists.txt, or argv[2]) at the carrier deck and in the air: the port's three
 * queues against the original's, entry for entry (docs/re/04a-display-lists.md, "Verified by"). Exit 1 when
 * the deck lists differ or fewer than all but two airborne entries agree. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/game.h"
#include "../src/core/world.h"
#include "lists_state.h"

typedef struct { const char *dir; } fs_t;

static int fs_read(void *user, const char *path, uint8_t **out, size_t *len) {
    char full[512];
    snprintf(full, sizeof full, "%s/%s", ((fs_t *)user)->dir, path);
    FILE *f = fopen(full, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = (uint8_t *)malloc((size_t)n + 1);
    if (!*out || fread(*out, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(*out); return -1; }
    fclose(f);
    *len = (size_t)n;
    return 0;
}
static void fs_release(void *user, uint8_t *buf) { (void)user; free(buf); }

int main(int argc, char **argv) {
    fs_t fs = { argc > 1 ? argv[1] : "assets" };
    game_loader_t ld = { fs_read, fs_release, &fs };
    uint8_t *probe; size_t pl;
    if (fs_read(&fs, "models/index.txt", &probe, &pl) != 0) {
        printf("world_dump: skipped (%s/models/index.txt not found)\n", fs.dir);
        return 0;
    }
    free(probe);
    char err[256] = "";
    world_t *w = world_load(&ld, err, sizeof err);
    if (!w) { printf("world_dump: load failed: %s\n", err); return 1; }
    world_start(w);   /* the camera over the carrier (WORLD_ENTRY_CARRIER) */
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    {   /* the free camera over the carrier with the carrier as entity 14: one frame of the builder */
        lists_frame_t in;
        memset(&in, 0, sizeof in);
        int32_t cp[3];
        if (world_position_entry(w, WORLD_ENTRY_CARRIER, cp) == 0) {
            lists_entity_t *c = &in.ent[14];
            c->active = 1; c->cell_x = (uint8_t)(cp[0] >> 22); c->cell_z = (uint8_t)(cp[2] >> 22);
            c->fine_x = (int16_t)((cp[0] & 0x3FFFFF) >> 8); c->fine_z = (int16_t)((cp[2] & 0x3FFFFF) >> 8);
            c->sub = (uint8_t)(4 * (3 - (c->fine_z >> 12)) + (3 - (c->fine_x >> 12)));
            for (int i = 0; i < 3; i++) c->pos[i] = cp[i];
            c->model = WORLD_CARRIER_ID;
        }
        world_lists_frame(w, &in);
    }
    world_fill_scene(w, s);
    printf("camera: x %d y %d z %d\n", (int)s->camera.pos.x, (int)s->camera.pos.y, (int)s->camera.pos.z);
    printf("entry_count %u\n", s->entry_count);
    printf("first entry models:");
    for (int i = 0; i < 5 && i < s->entry_count; i++) printf(" %u", s->entries[i].asset_id);
    printf("\n");
    printf("hud: heading %d pitch %d altitude %d\n", (int)s->hud.heading_deg, (int)s->hud.pitch_deg, (int)s->hud.altitude_ft);

    /* smoke test: every loaded model, three viewers, every level */
    int models = 0, levels = 0, polys = 0, maxp = 0;
    static world_poly_t out[256];
    static const int32_t eyes[3][3] = { {0, 0, 2000}, {-3000, 500, -3000}, {40000, 8000, 40000} };
    for (unsigned id = 0; id < WORLD_MODEL_COUNT; id++) {
        int st = world_model_status(w, id);
        if (st != WORLD_MODEL_OK && st != WORLD_MODEL_PARTIAL) continue;
        models++;
        for (int l = 0; l < world_model_level_count(w, id); l++)
            for (int e = 0; e < 3; e++) {
                world_view_t v;
                memset(&v, 0, sizeof v);
                v.eye[0] = eyes[e][0]; v.eye[1] = eyes[e][1]; v.eye[2] = eyes[e][2];
                int n = world_model_polys(w, id, l, &v, out, 256);
                if (n < 0) { printf("model %u level %d: error\n", id, l); return 1; }
                levels++; polys += n; if (n > maxp) maxp = n;
            }
    }
    printf("models %d, walks %d, polygons %d (max %d per walk)\n", models, levels, polys, maxp);
    {
        world_view_t v;
        memset(&v, 0, sizeof v);
        int32_t d[3] = { 0, 20000, 60000 };   /* a viewer behind and above the carrier, position units */
        world_make_view(w, WORLD_CARRIER_ID, d, 0, &v);
        int lvl = world_pick_level(w, WORLD_CARRIER_ID, &v);
        int n = lvl >= 0 ? world_model_polys(w, WORLD_CARRIER_ID, lvl, &v, out, 256) : -1;
        printf("carrier (id 8) from (0, 20000, 60000): eye %d %d %d, level %d, %d polygons\n",
               (int)v.eye[0], (int)v.eye[1], (int)v.eye[2], lvl, n);
    }
    free(s);
    /* the logged lists of the reference emulator */
    int rc = 0;
    {
        const char *logpath = argc > 2 ? argv[2] : "extracted/captures/logs/lists.txt";
        static ls_log_t log;
        if (ls_load_log(&log, logpath) != 0 || !ls_find(&log, "q_", "deck") || !ls_find(&log, "q_", "air")) {
            printf("lists: skipped (no %s)\n", logpath);
        } else {
            static const char *scenes[2] = { "deck", "air" };
            for (int k = 0; k < 2; k++) {
                static lists_t l;
                lists_init(&l, world_lists_tables(w), world_lists_cell, w);
                ls_load_state(&l, ls_find(&log, "s71_", scenes[k])->data, ls_find(&log, "recs_", scenes[k])->data, ls_find(&log, "sec_", scenes[k])->data);
                lists_builder(&l, 0x0B);
                int total; char msg[400];
                int same = ls_compare(&l, ls_find(&log, "q_", scenes[k])->data, &total, msg, sizeof msg);
                printf("lists %s: %d of %d entries identical (A %d, B %d, C %d)%s%s\n", scenes[k], same, total, l.kept[0], l.kept[1], l.kept[2], msg[0] ? "; first difference: " : "", msg);
                if (k == 0 ? same != total : same < total - 2) rc = 1;
            }
        }
        ls_free_log(&log);
    }
    world_destroy(w);
    return rc;
}
