/* tests/test_spawn_diff.c - the mission-start placement (docs/re/07-ai-and-weapons.md 2.1 to 2.4) against the original's 0x2CE36 in the Musashi rig: the screen module's selection of the
 * variant and the offsets from the selector word, the sub-list by the completion byte, the threshold and the mode filter, the cap, and entities_start_mission's records
 * (template copy, constants, destination or leader, the aim). For modes 1 to 9 and 0x7F (the rows of the table 0x2DEDA), the four sub-lists, the progress values and random
 * selectors, fresh starts and restarts. Every mapped word of the records 4 to 15 and the descriptors are compared. Environment: SPAWN_DIFF_N (default 600), SPAWN_DIFF_SEED,
 * SPAWN_DIFF_TARGETS (random +0x38 bytes before the region ticks), SPAWN_DIFF_REGIONS / SPAWN_DIFF_DUMP (diagnostics), SPAWN_DIFF_EXITPROBE / SPAWN_DIFF_OWNBLOCK (the exit's rules by experiment).
 * After the placement the region ticks 0x2D0AA run in both worlds for random player cells (the placer, the re-placement of freed slots, the exit 0x2D116). Skipped (exit 0) without
 * extracted/interceptor.bin or the extracted assets. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/flight.h"
#include "../../src/core/entities.h"
#include "../../src/core/screen.h"
#include "../../src/core/assets.h"
#include "../../src/core/logbook.h"
#include "../../src/core/world.h"

#define BASE 0x10000u
#define V_LO 0x4A6E8u
#define V_HI 0x54EC0u
#define REC 0x4B23Cu


static void put8(uint32_t a, int v) { uint8_t b = (uint8_t)v; rig_poke(a, &b, 1); }
static void put16(uint32_t a, int v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 2); }
static void put32(uint32_t a, int32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 4); }
static int get8(uint32_t a) { uint8_t b; rig_peek(a, &b, 1); return b; }
static int get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return (int16_t)((b[0] << 8) | b[1]); }
static int32_t get32(uint32_t a) { uint8_t b[4]; rig_peek(a, b, 4); return (int32_t)(((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]); }

static const char *assets = "../../assets";
static int fl_read(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    char p[256];
    snprintf(p, sizeof p, "%s/%s", assets, path);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = (uint8_t *)malloc((size_t)n + 1);
    *len = fread(*out, 1, (size_t)n, f);
    fclose(f);
    return 0;
}
static void fl_release(void *user, uint8_t *buf) { (void)user; free(buf); }
static game_loader_t loader = { fl_read, fl_release, NULL };
static flight_tables_t tables;
static flight_env_t env;

static int build_env(void) {
    char err[160];
    if (flight_tables_load(&tables, &loader, err, sizeof err) != 0) { printf("test_spawn_diff: skipped (%s)\n", err); return -1; }
    world_t *w = world_load(&loader, err, sizeof err);
    if (!w) { printf("test_spawn_diff: skipped (%s)\n", err); return -1; }
    memset(&env, 0, sizeof env);
    env.tab = &tables;
    env.landmark_z = 1; env.no_rearm = 1; env.no_sound_system = 1; env.no_liftoff_hook = 1;
    flight_set_env(&env);
    world_destroy(w);
    return 0;
}

static const char *image_path;
static void hook_none(void) {}

static uint32_t rng = 0x9E3779B9u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }

static world_t *WORLD;
static templates_t TPL;
static aircraft_t PLAYER;
static entities_t E;

static int blk(void *u, unsigned n, int16_t w[5]) { (void)u; return world_position_block(WORLD, n, w); }
static int unit_hook(void *u, unsigned t) { (void)u; const int v = world_model_unit_shift(WORLD, (uint16_t)t, 0); return v < 0 ? 3 : v; }
static int16_t trig(void *u, uint32_t addr) { int known; return flight_image_word((const flight_tables_t *)u, addr, &known); }
static int32_t aim_hook(void *u, int32_t dx, int32_t dz) {
    (void)u;
    world_viewstate_t vs;
    memset(&vs, 0, sizeof vs);
    vs.image_word = trig; vs.image_user = &tables;
    int32_t p = 0, h = 0;
    world_aim_target(&vs, dx, 0, dz, &p, &h);
    return h;
}

static int compare_records(int n, const char *what) {
    static const struct { const char *name; uint32_t off; int size; } W[] = {
        { "W0", 0x00, 2 }, { "W2", 0x02, 2 }, { "B4", 0x04, 1 }, { "state", 0x05, 1 }, { "cell_x", 0x06, 2 }, { "cell_z", 0x08, 2 }, { "cell+0A", 0x0A, 1 }, { "fine_x", 0x0C, 2 }, { "fine_z", 0x0E, 2 },
        { "alt8", 0x10, 4 }, { "x", 0x14, 4 }, { "y", 0x18, 4 }, { "z", 0x1C, 4 }, { "dest_cx", 0x2C, 2 }, { "dest_cz", 0x2E, 2 }, { "dest_fx", 0x30, 2 }, { "dest_fz", 0x32, 2 }, { "dest_h", 0x34, 4 },
        { "target", 0x38, 1 }, { "route", 0x3A, 1 }, { "hits", 0x3C, 1 }, { "range", 0x4A, 2 }, { "counter", 0x4C, 2 }, { "tag", 0x5D, 1 }, { "group", 0x5E, 1 }, { "stock", 0x5F, 1 }, { "rounds", 0x60, 2 },
        { "cls", 0x62, 1 }, { "id", 0x63, 1 }, { "flags", 0x64, 1 }, { "pitch", 0x66, 2 }, { "heading", 0x68, 2 }, { "roll", 0x6A, 2 }, { "S", 0x6C, 2 }, { "W", 0x6E, 2 }, { "fuel", 0x72, 4 }, { "engage", 0x7A, 1 },
        { "gear", 0x7C, 1 }, { "l7d", 0x7D, 1 }, { "matA0", 0x80, 2 }, { "matB0", 0x92, 2 }, { "matB1", 0x94, 2 }, { "matB2", 0x96, 2 }, { "matB3", 0x98, 2 }, { "matB4", 0x9A, 2 } };
    for (int i = 4; i < 16; i++) {
        if (i == 7) continue;   // slot 7 is never named
        const uint32_t base = REC + (uint32_t)i * 0x200u;
        const entity_t *r = &E.rec[i];
        const flight_state_t *f = &r->ac.f;
        const int32_t pv[sizeof W / sizeof W[0]] = { f->w0, f->w2, f->b4, f->state, f->cell_x, f->cell_z, f->cell_a, f->fine_x, f->fine_z, f->alt8, r->ac.pos[0], r->ac.pos[1], r->ac.pos[2],
            r->dest_cx, r->dest_cz, r->dest_fx, r->dest_fz, r->dest_h, r->target, r->route, r->hits, (int16_t)r->range, f->counter, r->tag, f->group, r->stock, (int16_t)r->rounds, f->cls, f->id, r->ai_flags,
            (int16_t)f->pitch, (int16_t)f->heading, (int16_t)f->roll, f->speed_target, f->speed, f->fuel, r->engage, f->gear, f->l7d, f->mat_a[0], f->mat_b[0], f->mat_b[1], f->mat_b[2], f->mat_b[3], f->mat_b[4] };
        for (unsigned k = 0; k < sizeof W / sizeof W[0]; k++) {
            int32_t rv = W[k].size == 1 ? get8(base + W[k].off) : W[k].size == 2 ? get16(base + W[k].off) : get32(base + W[k].off);
            int32_t pw = pv[k];
            if (W[k].size == 1) { rv &= 0xFF; pw &= 0xFF; }
            if (W[k].size == 2) pw = (int16_t)pw;
            if (!(f->w0 & 0x0040) && !(get16(base) & 0x0040) && k > 1) continue;   // two free records: only the flags count
            if (rv != pw) {
                printf("  config %d: %s: record %d field %s: port %d (0x%X) rig %d (0x%X)\n", n, what, i, W[k].name, pw, (unsigned)pw, rv, (unsigned)rv);
                if (getenv("SPAWN_DIFF_DUMP")) for (unsigned q = 0; q < sizeof W / sizeof W[0]; q++) { int32_t r2 = W[q].size == 1 ? get8(base + W[q].off) : W[q].size == 2 ? get16(base + W[q].off) : get32(base + W[q].off); printf("      %s: port %d rig %d\n", W[q].name, W[q].size == 1 ? pv[q] & 0xFF : W[q].size == 2 ? (int16_t)pv[q] : pv[q], W[q].size == 1 ? r2 & 0xFF : r2); }
                return 1;
            }
        }
        if ((f->w0 & 0x0040)) {   // the descriptor copy (2.3): the five longs
            for (int k = 0; k < 5; k++) {
                const uint32_t rv = (uint32_t)get32(0x2600C + 20u * (uint32_t)i + 4u * (uint32_t)k);
                if (rv != E.desc[i][k]) { printf("  config %d: %s: record %d descriptor long %d: port 0x%X rig 0x%X\n", n, what, i, k, E.desc[i][k], rv); return 1; }
            }
        }
    }
    return 0;
}

static game_loader_t loader;
int main(int argc, char **argv) {
    const char *paths[] = {"../../extracted/interceptor.bin", "extracted/interceptor.bin"};
    (void)argc; (void)argv;
    if (rig_init(1 << 21) != 0) { printf("rig_init failed\n"); return 1; }
    for (int i = 0; i < 2 && !image_path; i++) if (rig_load_image(paths[i], BASE) == 0) image_path = paths[i];
    if (!image_path) { printf("test_spawn_diff: skipped (extracted/interceptor.bin not found)\n"); rig_shutdown(); return 0; }
    {
        FILE *t = fopen("../../assets/VERSION", "r");
        if (!t) assets = "assets"; else fclose(t);
    }
    if (build_env() != 0) { rig_shutdown(); return 0; }
    loader = (game_loader_t){ fl_read, fl_release, NULL };
    char err[200];
    WORLD = world_load(&loader, err, sizeof err);
    if (!WORLD || assets_load_templates(&loader, &TPL, err, sizeof err) != 0) { printf("test_spawn_diff: skipped (%s)\n", err); rig_shutdown(); return 0; }
    screen_t *scr = (screen_t *)calloc(1, sizeof *scr);
    if (screen_init(scr, &loader, err, sizeof err) != 0 || !scr->scn) { printf("test_spawn_diff: skipped (%s)\n", err); rig_shutdown(); return 0; }
    static logbook_t log;
    scr->log = &log;
    const int N = getenv("SPAWN_DIFF_N") ? atoi(getenv("SPAWN_DIFF_N")) : 600;
    if (getenv("SPAWN_DIFF_SEED")) rng = (uint32_t)atoi(getenv("SPAWN_DIFF_SEED")) * 2654435761u + 1;
    if (getenv("SPAWN_DIFF_EXITPROBE")) {   /* which records the exit 0x2D116 re-addresses: for every +0x38 byte, the four entry types of the regions 1 to 3 */
        for (int T = 0; T < 256; T += (getenv("SPAWN_DIFF_EXITALL") ? 1 : 5)) {
            for (int variant = 0; variant < 4; variant++) {
                if (rig_load_image(image_path, BASE) != 0) return 1;
                static const uint32_t patch[] = { 0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2 };
                for (int i = 0; i < 4; i++) put16(patch[i], 0x4E75);
                static uint8_t zero[V_HI - V_LO];
                rig_poke(V_LO, zero, sizeof zero);
                rig_hooks_clear();
                put8(0x4A95E, 3); put8(0x4A95F, 1); put8(0x4A855, 0x0F); put16(0x4A992, 3);
                put16(REC + 6, 0); put16(REC + 8, 0);
                /* records 4, 8, 12, 14 valid; the leader 14 has a known destination; variant 1 makes the leader invalid */
                for (int q = 4; q < 16; q++) { put16(REC + q * 0x200, (q == 14 && variant == 1) ? 0 : 0x0040); put8(REC + q * 0x200 + 0x38, T); put16(REC + q * 0x200 + 0x2C, 11); put8(REC + q * 0x200 + 0x7A, 3); }
                put16(REC + 14 * 0x200 + 0x2C, 99); put8(REC + 14 * 0x200 + 0x38, 0x80);
                if (getenv("SPAWN_DIFF_EXITFIELDS")) {   /* distinct values in the leader's position, destination and neighbouring words: which of them the re-addressed record receives */
                    put16(REC + 14 * 0x200 + 0x06, 101); put16(REC + 14 * 0x200 + 0x08, 102); put16(REC + 14 * 0x200 + 0x0C, 103); put16(REC + 14 * 0x200 + 0x0E, 104);
                    put32(REC + 14 * 0x200 + 0x10, 105); put32(REC + 14 * 0x200 + 0x14, 0x0640000); put32(REC + 14 * 0x200 + 0x18, 0x0720000); put32(REC + 14 * 0x200 + 0x1C, 0x0820000);
                    put16(REC + 14 * 0x200 + 0x2C, 201); put16(REC + 14 * 0x200 + 0x2E, 202); put16(REC + 14 * 0x200 + 0x30, 203); put16(REC + 14 * 0x200 + 0x32, 204); put32(REC + 14 * 0x200 + 0x34, 205);
                    printf("  leader words set\n");
                }
                for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
                if (rig_run(0x2D0AAu) != 0) { printf("probe failed\n"); return 1; }
                printf("T=%02X variant %d:", T, variant);
                for (int q = 4; q <= 12; q += (q == 4 ? 4 : 2)) printf(" rec%d(target %02X dest %d,%d,%d,%d,%d eng %d w0 %04X)", q, get8(REC + q * 0x200 + 0x38), get16(REC + q * 0x200 + 0x2C), get16(REC + q * 0x200 + 0x2E), get16(REC + q * 0x200 + 0x30), get16(REC + q * 0x200 + 0x32), get32(REC + q * 0x200 + 0x34), get8(REC + q * 0x200 + 0x7A), get16(REC + q * 0x200) & 0xFFFF);
                printf("\n");
            }
        }
        return 0;
    }
    if (getenv("SPAWN_DIFF_OWNBLOCK")) {   /* the exit's own-block case for several blocks: poke the word 3 of the region 2 entry [0078 0012 0E0C 006E 0001] and read the record 12's destination */
        for (int k = 1; k <= 58; k++) {
            if (rig_load_image(image_path, BASE) != 0) return 1;
            static const uint32_t patch[] = { 0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2 };
            for (int i = 0; i < 4; i++) put16(patch[i], 0x4E75);
            const int leader = getenv("SPAWN_DIFF_LEADERS") ? (k % 14) + 1 : 14;
            uint32_t at = 0;
            for (uint32_t a = 0x2DE28; a < 0x2E400; a += 2) if ((get16(a) & 0xFFFF) == 0x78 && (get16(a + 2) & 0xFFFF) == 0x12 && (get16(a + 4) & 0xFFFF) == 0x0E0C) { at = a; break; }
            if (!at) { printf("entry not found\n"); return 1; }
            put16(at + 6, getenv("SPAWN_DIFF_LEADERS") ? 110 : 2 * k); put16(at + 4, (leader << 8) | 12);
            static uint8_t zero[V_HI - V_LO];
            rig_poke(V_LO, zero, sizeof zero);
            rig_hooks_clear();
            put8(0x4A95E, 3); put8(0x4A95F, 1); put8(0x4A855, 0x0F); put16(0x4A992, 3);
            put16(REC + 6, 0); put16(REC + 8, 0);
            put16(REC + 12 * 0x200, 0x0040); put8(REC + 12 * 0x200 + 0x38, 0); put8(REC + 12 * 0x200 + 0x7A, 3);   /* the leader 14 stays invalid: the own block */
            for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
            rig_run(0x2D0AAu);
            int16_t b[5];
            const int have = world_position_block(WORLD, (unsigned)k, b) == 0;
            printf("leader %d block %d: rig dest %d,%d,%d,%d,%d | table %d,%d,%d,%d,%d\n", leader, k, get16(REC + 12 * 0x200 + 0x2C), get16(REC + 12 * 0x200 + 0x2E), get16(REC + 12 * 0x200 + 0x30), get16(REC + 12 * 0x200 + 0x32), get32(REC + 12 * 0x200 + 0x34), have ? b[0] : -1, have ? b[1] : -1, have ? b[2] : -1, have ? b[3] : -1, have ? b[4] : -1);
        }
        return 0;
    }
    int diffs = 0, placed_total = 0, offsets = 0, ticks_run = 0;
    for (int n = 0; n < N; n++) {
        static const int modes[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x7F, 3, 4, 5, 6, 7, 8 };
        const int mode = modes[rnd(0, 15)];
        const unsigned sel = xr() & 0xFFFF;
        const int progress = rnd(0, 4), unlock = rnd(0, 3);
        const int restart = (rng & 1) && n % 3 == 0;
        const int old_variant = rnd(0, 2);
        const int old_dx = rnd(-3, 3), old_dz = rnd(-3, 3);
        /* the rig */
        if (rig_load_image(image_path, BASE) != 0) return 1;
        static const uint32_t patch[] = { 0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2 };
        for (int i = 0; i < 4; i++) put16(patch[i], 0x4E75);
        static uint8_t zero[V_HI - V_LO];
        rig_poke(V_LO, zero, sizeof zero);
        rig_hooks_clear();
        rig_hook_set(0x29BC8, hook_none, 0);
        put8(0x4A95E, mode); put8(0x4A95F, progress); put16(0x4ABB0, sel); put8(0x4A8E3, restart);
        put16(0x4ABD4, old_variant); put16(0x4ABD0, old_dx); put16(0x4ABD2, old_dz);
        /* V:1D024 holds the POINTER to the player record (note 08 18.x); the completion byte of the mode is at pointer + 0x12 + mode */
        { static uint8_t zrec[0x100]; rig_poke(0x180000, zrec, sizeof zrec); put32(0x1D024, 0x180000); put8(0x180000 + 0x12 + (mode > 9 ? 0 : mode), mode > 9 ? 0 : unlock); }
        put8(0x4A846, 1); put16(0x4A984, 0x0840);
        /* the rig's player record: invalid (the leader of a following aircraft is the carrier or slot 4 in the data) */
        for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
        if (rig_run(0x2CE36u) != 0) { printf("config %d: rig_run failed\n", n); continue; }
        if (getenv("SPAWN_DIFF_MEM")) { printf("config %d mode %02X sel %04X restart %d old %d: 4ABB0..4ABE0:", n, mode, sel, restart, old_variant); for (int q = 0; q < 0x30; q++) printf(" %02X", get8(0x4ABB0 + q)); printf("\n"); }
        /* the port */
        memset(&PLAYER, 0, sizeof PLAYER);
        scr->mode_byte = (uint8_t)mode;
        memset(&log, 0, sizeof log);
        log.live_level = (uint16_t)progress;
        if (mode <= 9) log.rec[LB_COMPLETIONS + mode] = (uint8_t)unlock;
        scr->log = &log;
        scr->echo_restart = (uint8_t)restart;
        scr->variant = (uint8_t)old_variant; scr->pl_dx = old_dx; scr->pl_dz = old_dz;
        screen_start_flight_selector(scr, sel);   /* the selector the rig got (the port draws it from the timer model, note 07 2.4; the placement is what is compared) */
        entities_init(&E, &TPL, NULL, scr->scn);
        entities_attach_player(&E, &PLAYER);
        E.hook.position_block = blk; E.hook.unit_shift = unit_hook; E.hook.aim_heading = aim_hook;
        E.mode = (uint8_t)mode; E.progress = progress; E.flying = 1; E.sim_on = 1;
        flight_set_env(&env);
        const int placed = entities_start_mission(&E, scr->sub_first, scr->sub_count, scr->hostile_cap, scr->pl_dx, scr->pl_dz);
        placed_total += placed;
        if (scr->pl_dx || scr->pl_dz) offsets++;
        int bad = compare_records(n, "mission start");
        if (!bad && E.hostile_count != get8(0x4A961)) { printf("  config %d: V:4A961: port %d rig %d\n", n, E.hostile_count, get8(0x4A961)); bad = 1; }
        if (!bad && E.hostile_cap != get8(0x4A962)) { printf("  config %d: V:4A962: port %d rig %d\n", n, E.hostile_cap, get8(0x4A962)); bad = 1; }
        if (!bad && scr->variant != get16(0x4ABD4)) { printf("  config %d: V:4ABD4: port %d rig %d\n", n, scr->variant, get16(0x4ABD4)); bad = 1; }
        /* the region triggers (2.5) after the placement: 0x2D0AA against entities_region_tick for a player cell inside or outside the boxes, three to five ticks, with a record
         * freed in between now and then (the re-placement of a freed slot, the exit that re-addresses the entries) */
        for (int tick = 0; !bad && tick < 5 && scr->scn->region_count > 0; tick++) {
            const int ri = rnd(0, scr->scn->region_count - 1);
            const scenario_region_t *rg = &scr->scn->region[ri];
            int cx, cz;
            if (rnd(0, 2) == 0) { cx = rnd(0, 120); cz = rnd(0, 120); }
            else { cx = rnd((int16_t)rg->box[0], (int16_t)rg->box[1]); cz = rnd((int16_t)rg->box[2], (int16_t)rg->box[3]); }
            if (getenv("SPAWN_DIFF_TARGETS")) {   /* random +0x38 bytes on the placed records: which ones the exit leaves alone */
                static const uint8_t tv[] = { 0x00, 0x05, 0x0E, 0x7F, 0x80, 0x85, 0x8E, 0xFF };
                for (int q = 4; q < 16; q++) if (q != 7 && (E.rec[q].ac.f.w0 & 0x40)) { const uint8_t t = tv[rnd(0, 7)]; put8(REC + q * 0x200 + 0x38, t); E.rec[q].target = t; }
            }
            if (rnd(0, 3) == 0) {   /* free one of the records 4 to 15 in both worlds */
                const int fr = rnd(4, 15);
                if (fr != 7) {
                    put16(REC + fr * 0x200, get16(REC + fr * 0x200) & ~0x0040);
                    E.rec[fr].ac.f.w0 &= (uint16_t)~0x0040;
                }
            }
            put16(REC + 6, cx); put16(REC + 8, cz);
            put16(0x4A992, 16 * tick + 3);
            for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
            if (rig_run(0x2D0AAu) != 0) { printf("config %d: rig_run of the tick failed\n", n); bad = 1; break; }
            PLAYER.f.cell_x = (int16_t)cx; PLAYER.f.cell_z = (int16_t)cz;
            E.pass = (uint32_t)(16 * tick + 3);
            entities_region_tick(&E);
            if (E.region_bits != get8(0x4A855)) { printf("  config %d: region tick %d at (%d,%d): V:4A855 port %02X rig %02X\n", n, tick, cx, cz, E.region_bits, get8(0x4A855)); bad = 1; }
            if (!bad) bad = compare_records(n, "region tick");
            if (!bad && E.hostile_count != get8(0x4A961)) { printf("  config %d: region tick %d: V:4A961 port %d rig %d\n", n, tick, E.hostile_count, get8(0x4A961)); bad = 1; }
            if (bad && getenv("SPAWN_DIFF_REGIONS")) printf("      tick %d at (%d,%d) pass %d; leader 14: port cell (%d,%d) fine (%d,%d) alt8 %d, rig cell (%d,%d) fine (%d,%d) alt8 %d\n", tick, cx, cz, 16 * tick + 3, E.rec[14].ac.f.cell_x, E.rec[14].ac.f.cell_z, E.rec[14].ac.f.fine_x, E.rec[14].ac.f.fine_z, E.rec[14].ac.f.alt8, get16(REC + 14 * 0x200 + 6), get16(REC + 14 * 0x200 + 8), get16(REC + 14 * 0x200 + 0xC), get16(REC + 14 * 0x200 + 0xE), get32(REC + 14 * 0x200 + 0x10));
            ticks_run++;
        }
        if (bad) {
            diffs++;
            printf("      mode %02X sel %04X progress %d unlock %d restart %d (old variant %d); sub-list %d+%d cap %d; entries:", mode, sel, progress, unlock, restart, old_variant, scr->sub_first, scr->sub_count, scr->hostile_cap);
            for (int k = 0; k < scr->placed_count; k++) printf(" [%04X %04X %04X %04X %04X]", scr->placed[k].w[0], scr->placed[k].w[1], scr->placed[k].w[2], scr->placed[k].w[3], scr->placed[k].w[4]);
            printf("\n");
            if (getenv("SPAWN_DIFF_REGIONS")) for (int q = 0; q < scr->scn->region_count; q++) {
                const scenario_region_t *rg = &scr->scn->region[q];
                printf("      region %d box %d..%d x %d..%d count_word %d:", q, (int16_t)rg->box[0], (int16_t)rg->box[1], (int16_t)rg->box[2], (int16_t)rg->box[3], rg->count_word);
                for (int k = 0; k < rg->count; k++) printf(" [%04X %04X %04X %04X %04X]", scr->scn->region_entry[rg->first + k].w[0], scr->scn->region_entry[rg->first + k].w[1], scr->scn->region_entry[rg->first + k].w[2], scr->scn->region_entry[rg->first + k].w[3], scr->scn->region_entry[rg->first + k].w[4]);
                printf("\n");
            }
        }
    }
    printf("test_spawn_diff: %d configurations, %d differences; %d entities placed in all, %d with a non-zero offset pair; %d region ticks\n", N, diffs, placed_total, offsets, ticks_run);
    rig_shutdown();
    return diffs != 0;
}
