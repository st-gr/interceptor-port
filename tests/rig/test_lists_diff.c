/* tests/rig/test_lists_diff.c - differential test: the port's display-list builder (lists_builder, src/core/lists.c) against the original
 * builder 0x1F770 in the Musashi rig (docs/re/04a-display-lists.md, "Verified by": the rig convention is the note author's tools/m68k-rig/tests/
 * test_lists_diff.c: the image at 0x10000, hunks 71 to 73 cleared, the state words and the entity records written, the occupancy maps built by
 * 0x1F31C, the identity words 0..21 at 0x53870, A6 = 0x70000, request byte in V:4A910, rig_run(0x1F770)).
 *
 * Both sides get the same state: the scan inputs (cells, sub-cell, quadrant, octant, pitch gate, camera height and offsets) and 16 entity records.
 * Compared after every builder call: the three queues entry for entry in the original's 24 bytes (id/flags word, descriptor address, X, Y, Z,
 * key, distance word, countdown, draw counter, result), the terminators (counts), the walked-window offsets V:4AA64/66/68, the emitted count V:4A916,
 * the run count V:4A914 and the sort cursor V:4A915, the direct list at 0x53A2A, bit 2 of the records, and the landmark slots (pointer table
 * 0x5283C and the slot records 0x52868: key long, outer box, first node box, reference points).
 *
 * What runs: (1) the logged deck and airborne states (extracted/captures/logs/lists.txt) with request 0x0B, followed by zero-request builder
 * calls (the per-frame run sort cadence and the landmark reorder), compared after each; (2) random states: the cell over the populated region of
 * the three tables and the whole grid, sub-cell, octant, pitch gate on and off, height from the ground to above 20,480 ft in all altitude bands,
 * camera offsets, request 0x0B / 0x0F / 0xFF / 0x1B (all tiers), each followed by zero-request calls with a moved camera; (3) states placed within
 * two cells of the table C landmark cells (items 42, 70, 88), with the camera near the landmark; (4) states with entities (the player, the carrier,
 * records 15 and others) in and around the scanned cells; (5) the request 0x0C and 0x8C (tier C only: A and B and their offsets stay) after a
 * full build and a moved camera.
 *
 * (6) The walk family (docs/re/04a-display-lists.md sections 8 and 8b): the original walkers 0x1FA24 (queue A), 0x1FA36 (B) and the queue C part of
 * 0x1FBCC against the port's lists_walk, on the same built queues, camera matrix and counters. The filler 0x34A28 is replaced by a return and the
 * run-time descriptors 0-16 by a stub (no record is active in this family: terrain entries only). Compared after every walk: +0x10, +0x12, +0x13 and
 * the stored result +0x14 of every walked entry; then the rig's four bytes are copied into the port's entries, so one divergence does not cascade.
 * The port has the original's handedness only (note 03: no mirror). V:4AA3C is 179 in both (the point sprites count against it). What the rig cannot decide
 * is listed in the report of the task (primitive handlers other than polygons, the 0xFFFF stream end the extractor drops, the entity primitive layer).
 *
 * Prints, per family, the states and the entries compared and the first divergence in full. A divergence fails the family except where a
 * named family is informational (the garbage footprints, see below). Skipped (exit 0) when extracted/interceptor.bin or the extracted assets
 * are absent. Run from tools/m68k-rig (make test-lists-port). Environment: LISTS_DIFF_N (random states per family, default 400), LISTS_DIFF_SEED,
 * LISTS_DIFF_VERBOSE (every divergence), LISTS_LOG (the emulator log).
 *
 * Landmark slots of the nine table C items whose nearest block has no footprint (item ids 32-38, 49, 117; their block word is 0xFFFF, so the
 * original copies whatever lies at the offset 0xFFF) cannot be reproduced; slots of those are not compared (counted as skipped). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/world.h"
#include "../../src/core/lists.h"
#include "../../src/core/fx.h"
#include "../lists_state.h"

#define BASE 0x10000u
#define V_LO 0x4A6E8u
#define V_HI 0x54EC0u
#define REC 0x4B23Cu
#define SLOT_PTRS 0x5283Cu
#define SLOT_REC 0x52868u
#define DIRECT 0x53A2Au
#define BUILDER 0x1F770u
#define OCC_MAPS 0x1F31Cu

/* ---- random numbers ----------------------------------------------------------------------------- */
static uint32_t rng = 0x2545F491u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }
static int pct(int p) { return rnd(0, 99) < p; }

/* ---- the assets ------------------------------------------------------------------------------------ */
static const char *root = "assets";
static int fs_read(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    char p[256];
    snprintf(p, sizeof p, "%s/%s", root, path);
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
static void fs_release(void *user, uint8_t *buf) { (void)user; free(buf); }
static game_loader_t loader = { fs_read, fs_release, NULL };

static world_t *world;
static lists_t port;

/* ---- the rig ----------------------------------------------------------------------------------------- */
static void put8(uint32_t a, int v) { uint8_t b = (uint8_t)v; rig_poke(a, &b, 1); }
static void put16(uint32_t a, int v) { uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v }; rig_poke(a, b, 2); }
static void put32(uint32_t a, int32_t v) { uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; rig_poke(a, b, 4); }
static unsigned get8(uint32_t a) { uint8_t b; rig_peek(a, &b, 1); return b; }
static unsigned get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return ((unsigned)b[0] << 8) | b[1]; }
static uint32_t get32(uint32_t a) { uint8_t b[4]; rig_peek(a, b, 4); return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]; }

static int rig_ready;
static int rig_setup(void) {
    if (rig_ready) return 0;
    FILE *f = fopen("../../extracted/interceptor.bin", "rb");
    if (!f) return -1;
    fclose(f);
    rig_init(0xE00000);                                      /* RAM up to the custom chips: the reads of 0xDFF000 return zero, so the drawer's blitter waits end */
    rig_load_image("../../extracted/interceptor.bin", BASE);
    for (int r = 0; r < 16; r++) rig_set_reg(r, 0);
    rig_run(OCC_MAPS);                                       /* occupancy maps: a speed-up, built once at mission start */
    rig_ready = 1;
    return 0;
}

typedef struct { lists_scan_t scan; lists_entity_t ent[LISTS_ENTITIES]; lists_entity_t sec[LISTS_ENTITIES]; uint32_t n; uint8_t period, built, entities; } state_t;

static void record_to_rig(uint32_t a, const lists_entity_t *e, int bit2) {
    put8(a + 1, (e->active ? 0x40 : 0) | (bit2 ? 4 : 0));
    put8(a + 4, e->b4);
    put16(a + 6, e->cell_x); put16(a + 8, e->cell_z); put8(a + 0x0A, e->sub);
    put16(a + 0x0C, e->fine_x & 0xFFFF); put16(a + 0x0E, e->fine_z & 0xFFFF);
    put32(a + 0x10, e->alt8);
    put32(a + 0x14, e->pos[0]); put32(a + 0x18, e->pos[1]); put32(a + 0x1C, e->pos[2]);
}

/* Writes the state into a cleared rig. */
static void state_to_rig(const state_t *st) {
    static uint8_t zero[V_HI - V_LO];
    rig_poke(V_LO, zero, sizeof zero);
    uint8_t ids[44];
    for (int i = 0; i < 22; i++) { ids[2 * i] = 0; ids[2 * i + 1] = (uint8_t)i; }
    rig_poke(0x53870, ids, sizeof ids);                      /* the sort's index table: words 0 to 21 (inside the cleared hunks) */
    const lists_scan_t *s = &st->scan;
    put16(0x4AA04, s->cell_x); put16(0x4AA06, s->cell_z);
    put8(0x4A909, s->sub); put8(0x4A908, s->quad); put8(0x4A90C, s->octant); put8(0x4A83E, s->gate);
    put32(0x4AB1E, s->cam_y);
    put16(0x4AB2A, s->off_x & 0xFFFF); put16(0x4AB2E, s->off_z & 0xFFFF); put32(0x4AB30, s->off_y);
    put8(0x4A974, st->period); put16(0x4A992, (int)st->n);
    put8(0x4A85D, st->built);   /* V:4A85D: the lists were built before (steady state); the run sort does nothing without it */
    for (int i = 0; i < LISTS_ENTITIES; i++) {
        record_to_rig(REC + 0x200u * (unsigned)i, &st->ent[i], i >= 14 && st->ent[i].active);
        record_to_rig(0x4D23Cu + 0x20u * (unsigned)i, &st->sec[i], 0);
    }
    /* The builder takes the A/B scan origin V:4AA00/02 from the cell words of the viewed record (the record at V:4A996, here record 0), not from
     * V:4AA04/06 (rig, note 04a section 2 says "the C cells shifted right by 2"): in the cockpit the two are equal, so the player's record carries the scan cell. */
    put16(REC + 6, s->cell_x); put16(REC + 8, s->cell_z);
}

static void state_to_port(lists_t *l, const state_t *st) {
    lists_init(l, world_lists_tables(world), world_lists_cell, world);
    l->scan = st->scan;
    /* V:4AB1A / V:4AB22: a generated state names the offsets only; the longs are the offsets shifted back (a logged state carries its own) */
    if (!l->scan.cam_x && !l->scan.cam_z) { l->scan.cam_x = (int32_t)l->scan.off_x * 256; l->scan.cam_z = (int32_t)l->scan.off_z * 256; }
    l->period = st->period; l->n = st->n; l->built = st->built;
    for (int i = 0; i < LISTS_ENTITIES; i++) {
        l->ent[i] = st->ent[i]; l->sec[i] = st->sec[i];
        l->bit2[i] = (i >= 14 && st->ent[i].active) ? 1 : 0;
    }
}

static void rig_call_builder(int request) {
    put8(0x4A910, request);
    for (int r = 0; r < 16; r++) rig_set_reg(r, 0);
    rig_set_reg(RIG_A6, 0x70000);
    if (rig_run(BUILDER) != 0) { printf("    rig_run failed: error %d\n", rig_last_error); }
}

/* ---- the comparison ----------------------------------------------------------------------------------- */
static int verbose;
static const state_t *cur_state;
typedef struct { long states, entries, entry_diff, other_diff, skipped_slots, slots, reorders, shown; char first[600], first_entry[700]; } stats_t;

static void report(stats_t *s, int *bad, const char *what, const char *fmt, long a, long b, const char *ctx) {
    (void)what;
    (*bad)++;
    if (!s->first[0] || verbose) {
        char line[400];
        snprintf(line, sizeof line, fmt, a, b);
        char all[700];
        snprintf(all, sizeof all, "%s [%s]", line, ctx);
        if (!s->first[0]) snprintf(s->first, sizeof s->first, "%.599s", all);
        if (verbose && s->shown++ < 12) printf("    DIFF %s\n", all);
    }
}

static int compare_state(stats_t *s, const lists_t *l, const char *ctx) {
    int bad = 0;
    uint8_t q[LS_QSIZE];
    rig_peek(LS_QUEUES, q, sizeof q);
    for (int t = 0; t < 3; t++) {
        int n = ls_queue_count(q, t);
        if (n != (int)l->kept[t]) { char c3[300]; snprintf(c3, sizeof c3, "tier %d: %s", t, ctx); report(s, &bad, "count", "queue count: rig %ld, port %ld", n, l->kept[t], c3); }
        for (int i = 0; i < n && i < (int)l->kept[t]; i++) {
            uint8_t w[24];
            ls_entry_bytes(&l->q[t][i], w);
            const uint8_t *a = q + t * LS_QSTRIDE + 24 * i;
            s->entries++;
            if (memcmp(a, w, 24) != 0) {
                int k = 0;
                while (a[k] == w[k]) k++;
                char c2[500];
                snprintf(c2, sizeof c2, "tier %d entry %d byte +0x%02X; rig %02x%02x %02x%02x%02x%02x x%d y%d z%d key %08x d%u cd%d c%u r%d; port %02x%02x %02x%02x%02x%02x x%d y%d z%d key %08x d%u cd%d c%u r%d; %s",
                         t, i, k, a[0], a[1], a[2], a[3], a[4], a[5], (int16_t)ls_u16(a + 6), (int16_t)ls_u16(a + 8), (int16_t)ls_u16(a + 10), ls_u32(a + 12), ls_u16(a + 16), (int8_t)a[18], a[19], (int16_t)ls_u16(a + 20),
                         w[0], w[1], w[2], w[3], w[4], w[5], (int16_t)ls_u16(w + 6), (int16_t)ls_u16(w + 8), (int16_t)ls_u16(w + 10), ls_u32(w + 12), ls_u16(w + 16), (int8_t)w[18], w[19], (int16_t)ls_u16(w + 20), ctx);
                bad++; s->entry_diff++;
                if (getenv("LISTS_DIFF_DUMP") && s->entry_diff == 1) {
                    printf("    queue %d, rig:", t);
                    for (int k = 0; k < n; k++) { const uint8_t *r2 = q + t * LS_QSTRIDE + 24 * k; printf(" [%d/%02x x%d z%d d%u]", r2[0], r2[1], (int16_t)ls_u16(r2 + 6), (int16_t)ls_u16(r2 + 10), ls_u16(r2 + 16)); }
                    printf("\n    queue %d, port:", t);
                    for (int k = 0; k < (int)l->kept[t]; k++) printf(" [%d/%02x x%d z%d d%u]", l->q[t][k].id, l->q[t][k].flags, l->q[t][k].x, l->q[t][k].z, l->q[t][k].dist);
                    printf("\n");
                    if (cur_state) for (int k = 0; k < LISTS_ENTITIES; k++) if (cur_state->ent[k].active)
                        printf("    entity %d: cell (%d,%d) sub %d fine (%d,%d) alt8 %d b4 %02x\n", k, cur_state->ent[k].cell_x, cur_state->ent[k].cell_z, cur_state->ent[k].sub, cur_state->ent[k].fine_x, cur_state->ent[k].fine_z, cur_state->ent[k].alt8, cur_state->ent[k].b4);
                }
                if (!s->first_entry[0]) snprintf(s->first_entry, sizeof s->first_entry, "%s", c2);
                if (!s->first[0]) snprintf(s->first, sizeof s->first, "%.599s", c2);
                if (verbose && s->shown++ < 12) printf("    DIFF %s\n", c2);
            }
        }
    }
    const uint32_t offs[3] = { 0x4AA64, 0x4AA66, 0x4AA68 };
    for (int t = 0; t < 3; t++) {
        unsigned rv = get16(offs[t]);
        if (rv != 24u * l->start[t]) report(s, &bad, "offset", "walked-window offset: rig %ld, port %ld", (long)rv, 24L * l->start[t], ctx);
    }
    if (l->built) {
        if (get8(0x4A914) != l->nruns) report(s, &bad, "runs", "run count V:4A914: rig %ld, port %ld", get8(0x4A914), l->nruns, ctx);
        if (get8(0x4A915) != l->cursor) report(s, &bad, "cursor", "sort cursor V:4A915: rig %ld, port %ld", get8(0x4A915), l->cursor, ctx);
        if (l->emitted_c < 256 && get8(0x4A916) != l->emitted_c) report(s, &bad, "emitted", "emitted count V:4A916: rig %ld, port %ld", get8(0x4A916), l->emitted_c, ctx);
        /* direct list */
        unsigned i = 0;
        while (i < 17 && get16(DIRECT + 2 * i) != 0xFFFF) i++;
        if (i != l->ndirect) report(s, &bad, "direct", "direct list length: rig %ld, port %ld", i, l->ndirect, ctx);
        else for (unsigned k = 0; k < i; k++) if (get16(DIRECT + 2 * k) != l->direct[k]) report(s, &bad, "direct", "direct list entry: rig %ld, port %ld", get16(DIRECT + 2 * k), l->direct[k], ctx);
        for (int r = 14; r < 16; r++)
            if (l->ent[r].active && (int)((get8(REC + 0x200u * (unsigned)r + 1) >> 2) & 1) != l->bit2[r])
                report(s, &bad, "bit2", "record bit 2: rig %ld, port %ld", (get8(REC + 0x200u * (unsigned)r + 1) >> 2) & 1, l->bit2[r], ctx);
    }
    /* the landmark slots */
    if (l->built) {
        /* the slot ordinals in the rig: the pointer table entries; -1 marks a slot without a model */
        for (int sl = 0; sl < LISTS_LANDMARK_SLOTS; sl++) {
            uint32_t ptr = get32(SLOT_PTRS + 4u * (unsigned)sl);
            int used_rig = ptr != 0;
            if (used_rig != l->slot[sl].used) { report(s, &bad, "slot", "slot used: rig %ld, port %ld", used_rig, l->slot[sl].used, ctx); continue; }
            if (!used_rig) continue;
            if (ptr == 0xFFFFFFFFu) { if (l->slot[sl].valid) report(s, &bad, "slot", "slot pointer -1 in the rig: port valid %ld %ld", 1, 0, ctx); continue; }
            /* find the queue entry of this ordinal: walk back from the last kept entry over non-entities */
            int ord = 0, qi = -1;
            for (int i = (int)l->kept[LISTS_TIER_C] - 1; i >= 0; i--) {
                if (l->q[LISTS_TIER_C][i].flags & 0x50) continue;
                if (ord++ == sl) { qi = i; break; }
            }
            /* the generated order differs from the current order after the sort; the filler's order is in the slot itself: compare to the rig only */
            (void)qi;
            const uint8_t *dummy = NULL; (void)dummy;
            uint8_t rec[0x40];
            rig_peek(SLOT_REC + 0x100u * (unsigned)sl, rec, sizeof rec);
            if (rec[2] != l->slot[sl].cell_x || rec[3] != l->slot[sl].cell_z)
                report(s, &bad, "slot", "slot cell bytes: rig %ld, port %ld", ((long)rec[2] << 8) | rec[3], ((long)l->slot[sl].cell_x << 8) | l->slot[sl].cell_z, ctx);
            else if (!l->slot[sl].valid) s->skipped_slots++;      /* no footprint in the port: the nine garbage footprints */
            else {
                int16_t pb[6] = { l->slot[sl].box[0], l->slot[sl].box[1], l->slot[sl].box[2], l->slot[sl].box[3], l->slot[sl].box[4], l->slot[sl].node[0].box[0] };
                (void)pb;
                int ok = 1;
                for (int k = 0; k < 5; k++) if ((int16_t)ls_u16(rec + 4 + 2 * k) != l->slot[sl].box[k]) ok = 0;
                for (int k = 0; k < 5; k++) if ((int16_t)ls_u16(rec + 0x0E + 2 * k) != l->slot[sl].node[0].box[k]) ok = 0;
                uint8_t big[0x100];
                rig_peek(SLOT_REC + 0x100u * (unsigned)sl, big, sizeof big);
                for (int p = 0; p < l->slot[sl].node[0].nplanes && ok; p++)
                    for (int c = 0; c < 3; c++) if ((int16_t)ls_u16(big + 0x1C + 6 * p + 2 * c) != l->slot[sl].node[0].ref[p][c]) ok = 0;
                s->slots++;
                if (!ok) report(s, &bad, "slot", "slot geometry differs (slot %ld, ptr %ld)", sl, (long)ptr, ctx);
            }
        }
    }
    return bad;
}

/* ---- states ------------------------------------------------------------------------------------------ */
static void derive_quad(lists_scan_t *s) { s->quad = (uint8_t)(4 * (3 - (s->cell_z & 3)) + (3 - (s->cell_x & 3))); }

static void set_height(lists_scan_t *s, int32_t h) { s->cam_y = -h; s->off_y = s->cam_y >> 8; }

/* Above 0x1000000 position units (about 80,000 ft, far above the aircraft's ceiling) the stored distance word of a far tier C entry reaches 0x7FFF; shifted left by
 * the LOD it is a negative sort key, and the original's sort then drops the entry and copies a stale index over its place (a duplicate in the queue; found with
 * LISTS_DIFF_N = 1600 and 3000). Unreachable in play and not reproduced, so the random heights stay below it. */
static void random_height(lists_scan_t *s) {
    int k = rnd(0, 9);
    int32_t h;
    if (k < 3) h = rnd(0, 0x9FFF);                    /* low altitude (at most 200 ft): tier A uses the low pattern */
    else if (k < 5) h = rnd(0xA000, 0x80000);
    else if (k < 7) h = rnd(0x80000, 0x200000);
    else if (k < 8) h = rnd(0x200000, 0x500000);
    else if (k < 9) h = rnd(0x500000, 0x1000000);
    else h = rnd(0x500000, 0x1000000);   /* heights above 0x1000000 (about 80,000 ft) are not compared: see below */
    set_height(s, h);
}

static void set_cell(state_t *st, int cx, int cz) {
    st->scan.cell_x = (uint8_t)(cx & 127); st->scan.cell_z = (uint8_t)(cz & 127);
    derive_quad(&st->scan);
}

static void random_scan(state_t *st) {
    memset(st, 0, sizeof *st);
    set_cell(st, rnd(0, 127), rnd(0, 127));
    st->scan.sub = (uint8_t)rnd(0, 15); st->scan.octant = (uint8_t)rnd(0, 7); st->scan.gate = (uint8_t)pct(35);
    random_height(&st->scan);
    st->scan.off_x = (int16_t)rnd(-16383, 0); st->scan.off_z = (int16_t)rnd(-16383, 0);
    st->period = 5; st->n = (uint32_t)rnd(0, 1000); st->built = pct(15) ? 0 : 1;   /* V:4A85D clear at the call: the first build is sorted like any other (note 04a section 5a) */
}

static int cx_of[4096], cz_of[4096], ncells_c;
static int land_x[64], land_z[64], nland;
static void collect_cells(void) {
    for (int z = 0; z < 128; z++)
        for (int x = 0; x < 128; x++) {
            const lists_item_t *it;
            int n = world_lists_cell(world, 2, (unsigned)x, (unsigned)z, &it);
            if (n <= 0) continue;
            if (ncells_c < 4096) { cx_of[ncells_c] = x; cz_of[ncells_c] = z; ncells_c++; }
            for (int i = 0; i < n; i++)
                if ((it[i].id == 42 || it[i].id == 70 || it[i].id == 88) && nland < 64) { land_x[nland] = x; land_z[nland] = z; nland++; break; }
        }
}

static void place_near_populated(state_t *st) {
    int r = rnd(0, 9);
    if (r < 5 && ncells_c) { int k = rnd(0, ncells_c - 1); set_cell(st, cx_of[k] + rnd(-3, 3), cz_of[k] + rnd(-3, 3)); }
    else if (r < 8) set_cell(st, rnd(58, 74), rnd(60, 74));       /* the airfield region (A and B cells 15 to 18) */
    /* else: the whole grid, as drawn */
}

static void random_entity(lists_entity_t *e, int cx, int cz) {
    memset(e, 0, sizeof *e);
    e->active = 1;
    e->cell_x = (uint8_t)((cx + rnd(-2, 2)) & 127); e->cell_z = (uint8_t)((cz + rnd(-2, 2)) & 127);
    e->fine_x = (int16_t)rnd(0, 16383); e->fine_z = (int16_t)rnd(0, 16383);
    e->sub = (uint8_t)(4 * (3 - (e->fine_z >> 12)) + (3 - (e->fine_x >> 12)));
    e->alt8 = pct(50) ? rnd(0, 400) : rnd(0, 30000);
    e->b4 = pct(25) ? (uint8_t)(pct(50) ? 0x40 : 0x80) : 0;
    e->pos[0] = ((int32_t)e->cell_x << 22) | ((int32_t)e->fine_x << 8); e->pos[2] = ((int32_t)e->cell_z << 22) | ((int32_t)e->fine_z << 8);
    e->pos[1] = e->alt8 << 8;
}

/* One state: build in both, compare, then `calls` zero-request calls with a moved camera. Returns the number of differing comparisons. */
static int run_state(stats_t *s, state_t *st, int request, int calls, const char *family, int index) {
    char ctx[200];
    snprintf(ctx, sizeof ctx, "%s #%d cell (%d,%d) sub %d quad %d oct %d gate %d h %d off (%d,%d) req %02x built %d", family, index, st->scan.cell_x, st->scan.cell_z,
             st->scan.sub, st->scan.quad, st->scan.octant, st->scan.gate, -st->scan.cam_y, st->scan.off_x, st->scan.off_z, request, st->built);
    cur_state = st;
    state_to_rig(st);
    state_to_port(&port, st);
    rig_call_builder(request);
    lists_builder(&port, (uint8_t)request);
    int bad = compare_state(s, &port, ctx);
    s->reorders += (long)port.reorders;
    for (int c = 0; c < calls; c++) {
        if (pct(60)) {   /* the camera moves between frames (no new request) */
            int16_t dx = (int16_t)rnd(-60, 60), dz = (int16_t)rnd(-60, 60);
            port.scan.off_x = (int16_t)(port.scan.off_x + dx); port.scan.off_z = (int16_t)(port.scan.off_z + dz);
            port.scan.cam_x = (int32_t)port.scan.off_x * 256; port.scan.cam_z = (int32_t)port.scan.off_z * 256;
            put16(0x4AB2A, port.scan.off_x & 0xFFFF); put16(0x4AB2E, port.scan.off_z & 0xFFFF);
        }
        rig_call_builder(0);
        lists_builder(&port, 0);
        char c2[260];
        snprintf(c2, sizeof c2, "%s, zero-request call %d", ctx, c + 1);
        bad += compare_state(s, &port, c2);
    }
    s->reorders += (long)port.reorders;
    s->states++;
    return bad;
}

/* ---- the families ----------------------------------------------------------------------------------- */
static int env_int(const char *name, int dflt) { const char *e = getenv(name); return e ? atoi(e) : dflt; }

static int finish(const char *name, stats_t *s) {
    printf("  %-34s %5ld states, %6ld entries and %5ld landmark slots compared, %ld reorders, %ld entry differences, %ld other differences%s\n", name, s->states, s->entries, s->slots,
           s->reorders, s->entry_diff, s->other_diff, s->skipped_slots ? " (some garbage-footprint slots not compared)" : "");
    if (s->first[0]) printf("    first divergence: %s\n", s->first);
    if (s->first_entry[0] && strcmp(s->first_entry, s->first)) printf("    first entry divergence: %s\n", s->first_entry);
    return s->first[0] ? 1 : 0;
}


/* ---- the walk family ---------------------------------------------------------------------------------------------------- */
#define STUB_FILLER 0x34A28u
#define V_ZOOM 0x4AAF6u
#define V_CAMMAT 0x4AC90u
#define V_CAMLONG 0x4AB1Au
static void rig_walk_prepare(void) {
    put16(STUB_FILLER, 0x4E75);                               /* the polygon filler: a return */
    {   /* the line, point sprite and disc output routines write the bit planes through pointers the cleared data does not set (V:4AA3C is now 179, so the points reach them): returns */
        static const uint32_t out_routines[] = { 0x3455E, 0x340D4, 0x340EA, 0x3414E, 0x33CAC };
        for (size_t i = 0; i < sizeof out_routines / sizeof *out_routines; i++) put16(out_routines[i], 0x4E75);
    }
    for (int i = 0; i < 17; i++) { put32(0x2600Cu + 20u * (unsigned)i, STUB_FILLER); for (int k = 1; k < 5; k++) put32(0x2600Cu + 20u * (unsigned)i + 4u * (unsigned)k, 0); }
}

typedef struct { long states, walks, entries, equal_result, pos_rig, pos_port, rig_pos_port_neg, rig_neg_port_pos, field_diff, prim_diff, shown; char first[800], first_prim[800]; long hits[10]; long prim_item[128], item_n[128]; long ent_n, ent_diff; long by_tier[3]; long cls_diff[8]; long prec_built, prim_proven, prim_rig_early, prim_unmatched, endless_skipped; } wstats_t;

/* The rig's exit of each drawer call, from a program-counter trace (rig_watch_set): the entry addresses of the descriptor routines mark the calls, the handler
 * table 0x23408 (the primitive layer: every handler is entered through it) marks that the call reached its primitives. A call that enters no handler left
 * the drawer before them (rules 2 to 8 and the early returns). The calls are matched to the port's walked entries by their order. */
#define TRACE_MAX 20000
static const uint32_t drawer_entry[] = { 0x21C40, 0x21C34, 0x21D0C, 0x22CFC, 0x22D0A, 0x21C44, 0x26944, 0x2699E, 0x26A3E };
static uint32_t handler_addr[96]; static int nhandler;
static unsigned char seg_prim[TRACE_MAX]; static uint32_t seg_h1[TRACE_MAX];   /* the first handler a call entered */
static long hcount_rigpos[96], hcount_portpos[96], dec_rigpos[96], dec_portpos[96]; static uint32_t op_handler[96]; static long items_portpos[128]; static int nseg, seg_expected, seg_ok;
static int walk_seg[LISTS_WALK_MAX];
static long seg_checked, seg_mismatch;

static void rig_trace_setup(void) {
    uint32_t w[128]; int n = 0;
    nhandler = 0;
    for (int i = 0; i < 80; i++) {
        uint8_t b[4]; rig_peek(0x23408u + 4u * (unsigned)i, b, 4);
        uint32_t a = ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
        op_handler[i] = a;
        if (a < 0x22000 || a > 0x26000) break;
        int dup = 0; for (int k = 0; k < nhandler; k++) if (handler_addr[k] == a) dup = 1;
        if (!dup) handler_addr[nhandler++] = a;
    }
    for (size_t i = 0; i < sizeof drawer_entry / sizeof *drawer_entry; i++) w[n++] = drawer_entry[i];
    for (int i = 0; i < nhandler && n < 128; i++) w[n++] = handler_addr[i];
    rig_watch_set(w, n);
}

static int is_drawer_entry(uint32_t a) { for (size_t i = 0; i < sizeof drawer_entry / sizeof *drawer_entry; i++) if (drawer_entry[i] == a) return 1; return 0; }

static void rig_segments(int low_chains) {   /* low_chains: 0x21C34 leads on to the common drawer (the camera is below the 0x21C34 cut, rule 2); else it returns at once */
    static uint32_t tr[TRACE_MAX];
    size_t total = rig_trace_get(tr, TRACE_MAX);
    size_t nt = total < TRACE_MAX ? total : TRACE_MAX;
    nseg = 0; seg_ok = total <= TRACE_MAX;
    int in_call = 0;
    for (size_t i = 0; i < nt; i++) {
        if (is_drawer_entry(tr[i])) {
            /* 0x21C40 enters the common drawer 0x21D0C, and 0x21C34 leads on to 0x21C40: an entry address right after those, with no handler between, continues the same call */
            if (in_call && i > 0 && !seg_prim[nseg - 1] && ((tr[i - 1] == 0x21C40 && tr[i] == 0x21D0C) || (low_chains && tr[i - 1] == 0x21C34 && (tr[i] == 0x21C40 || tr[i] == 0x21D0C)))) continue;
            if (nseg < TRACE_MAX) seg_prim[nseg++] = 0;
            in_call = 1;
        } else if (in_call) { if (!seg_prim[nseg - 1]) seg_h1[nseg - 1] = tr[i]; seg_prim[nseg - 1] = 1; }
    }
}

static void walk_cam(const lists_scan_t *sc, int octant, world_camera_t *c, game_camera_t *g) {
    memset(c, 0, sizeof *c);
    c->pos[0] = (int32_t)(((int64_t)sc->cell_x << 22) - (int64_t)sc->off_x * 256);
    c->pos[2] = (int32_t)(((int64_t)sc->cell_z << 22) - (int64_t)sc->off_z * 256);
    c->pos[1] = -sc->cam_y;
    c->yaw = (uint32_t)((octant * 3600 + rnd(-1700, 1700) + 28800) % 28800);
    c->pitch = rnd(-1800, 600);
    c->roll = (uint32_t)((rnd(-900, 900) + 28800) % 28800);
    memset(g, 0, sizeof *g);
    g->pos.x = c->pos[0]; g->pos.y = c->pos[1]; g->pos.z = c->pos[2];
    g->yaw = (int32_t)c->yaw; g->pitch = c->pitch; g->roll = (int32_t)c->roll;
}

static void rig_walk_state(const lists_scan_t *sc, const game_camera_t *g) {
    put32(V_CAMLONG, sc->cam_x); put32(V_CAMLONG + 4, sc->cam_y); put32(V_CAMLONG + 8, sc->cam_z);
    put16(V_ZOOM, 0x100); put16(V_ZOOM + 2, 0x100); put16(V_ZOOM + 4, 0x80);
    fx_mat_t m;
    world_camera_matrix(g, &m);
    for (int i = 0; i < 9; i++) put16(V_CAMMAT + 2u * (unsigned)i, (int)(uint16_t)m.m[i / 3][i % 3]);
    put8(0x4A895, 0); put8(0x4A924, 0);
    put16(0x4AA3C, 179);   /* the window's last row: the point routines reject rows below it (it is 0 in the cleared data) */
}

static void rig_run_a6(uint32_t addr) {
    for (int r = 0; r < 16; r++) rig_set_reg(r, 0);
    rig_set_reg(RIG_A6, 0x70000);
    if (rig_run(addr) != 0) {
        printf("    rig_run(%x) failed: error %d\n", addr, rig_last_error);
        if (getenv("LISTS_DIFF_TRACE")) {
            static uint32_t tr[TRACE_MAX]; size_t tn = rig_trace_get(tr, TRACE_MAX); if (tn > TRACE_MAX) tn = TRACE_MAX;
            printf("      last watched pcs:"); for (size_t i = tn > 12 ? tn - 12 : 0; i < tn; i++) printf(" %x", tr[i]); printf("   pc %x\n", rig_get_reg(RIG_PC));
        }
    }
}

static void walk_compare(wstats_t *w, const char *ctx, int wk) {
    static const uint32_t qaddr[3] = { 0x53A4A, 0x540DA, 0x5476A };
    static const uint32_t offs[3] = { 0x4AA64, 0x4AA66, 0x4AA68 };
    for (int t = 0; t < 3; t++) {
        for (int i = (int)(get16(offs[t]) / 24); i < (int)port.kept[t]; i++) {
            uint8_t rb[24], pb[24];
            rig_peek(qaddr[t] + 24u * (unsigned)i, rb, 24);
            ls_entry_bytes(&port.q[t][i], pb);
            w->entries++;
            int16_t rr = (int16_t)ls_u16(rb + 20), pr = (int16_t)ls_u16(pb + 20);
            const int entity = (rb[1] & 0x50) != 0;   /* an entity or secondary entry: the draw is the stub, only the walker's own fields (+0x10, +0x12, +0x13) are compared */
            int bad = memcmp(rb + 16, pb + 16, entity ? 4 : 6) != 0;
            if (entity) { w->ent_n++; if (bad) w->ent_diff++; }
            int drew = -1, polys = -1, rigprim = -1, walk_seg_of = 0, decider = 0;
            for (unsigned q = 0; q < port.nwalk; q++) if (port.walk[q].kind == 0 && port.walk[q].tier == t && port.walk[q].index == i) {
                drew = port.walk[q].drew; polys = port.walk[q].polys; decider = port.walk[q].decider;
                if (walk_seg[q] >= 0) { rigprim = seg_prim[walk_seg[q]]; walk_seg_of = walk_seg[q]; }
            }
            if (!entity) {
                if (rr > 0) w->pos_rig++;
                if (pr > 0) w->pos_port++;
                if (rr == pr) w->equal_result++;
                else if (rr > 0 && pr <= 0) w->rig_pos_port_neg++;
                else if (rr <= 0 && pr > 0) w->rig_neg_port_pos++;
            }
            /* a difference of the result where the port reached its primitive layer is a primitive-layer divergence (the model walk and the ops it
             * covers, note 04a section 8b rule 9: informational); every other difference is a rule-layer difference and fails the family */
            int prim_only = !entity && bad && drew == 1 && memcmp(rb + 16, pb + 16, 4) == 0 && (rb[20] != pb[20] || rb[21] != pb[21]) && memcmp(rb + 18, pb + 18, 2) == 0;
            /* proof that it is the primitive layer on the rig side too: the rig's call entered a handler of the table 0x23408 (rigprim 1). A difference where the
             * port reached its primitives and the rig left the drawer before any handler (rigprim 0) is a rule-layer difference; with no trace match (-1) it is unproven */
            if (prim_only) { if (rigprim == 1) { w->prim_proven++; int hi = -1; for (int h = 0; h < nhandler; h++) if (handler_addr[h] == seg_h1[walk_seg_of]) hi = h; if (hi >= 0) { if (rr > 0) hcount_rigpos[hi]++; else hcount_portpos[hi]++; } if (decider > 0 && decider < 80) { if (rr > 0) dec_rigpos[decider]++; else dec_portpos[decider]++; } if (rr <= 0) items_portpos[rb[0] & 127]++; } else if (rigprim == 0) { w->prim_rig_early++; prim_only = 0; } else w->prim_unmatched++; }
            w->item_n[rb[0] & 127]++;
            if (prim_only) { w->prim_diff++; w->prim_item[rb[0] & 127]++; if (!w->first_prim[0]) snprintf(w->first_prim, sizeof w->first_prim, "tier %d entry %d item %u lod %u dist %u: rig result %d, port result %d (port polys %d) [%s]", t, i, rb[0], rb[1] & 15u, ls_u16(rb + 16), rr, pr, polys, ctx); }
            else if (bad) {
                w->field_diff++; w->by_tier[t]++;
                if (!w->first[0] || (verbose && w->shown < 100000)) {
                    char line[800];
                    unsigned id = rb[0];
                    snprintf(line, sizeof line, "tier %d entry %d item %u (class %d) lod %u walk %d: rig dist %u cd %d c %u r %d, port dist %u cd %d c %u r %d (port drew %d polys %d) [%s]", t, i, id,
                             lists_desc_of(&port, &port.q[t][i])->cls, rb[1] & 15, wk, ls_u16(rb + 16), (int8_t)rb[18], rb[19], rr, ls_u16(pb + 16), (int8_t)pb[18], pb[19], pr, drew, polys, ctx);
                    if (!w->first[0]) snprintf(w->first, sizeof w->first, "%s", line);
                    if (verbose && w->shown++ < 100000) printf("    DIFF %s\n", line);
                }
            }
            /* re-synchronise: the port continues from the rig's walk state */
            port.q[t][i].dist = (uint16_t)ls_u16(rb + 16); port.q[t][i].countdown = (int8_t)rb[18]; port.q[t][i].counter = rb[19]; port.q[t][i].result = rr;
        }
    }
}

static int family_walk(int n) {
    wstats_t w; memset(&w, 0, sizeof w);
    world_set_window_last_row(world, 179);
    rig_trace_setup();
    for (int i = 0; i < n; i++) {
        state_t st;
        if (getenv("LISTS_DIFF_TRACE")) fprintf(stderr, "walk state %d\n", i);
        random_scan(&st);
        place_near_populated(&st);
        st.built = 1;
        if (pct(60)) set_height(&st.scan, rnd(0x400, 0x14000));   /* low cameras: the pre-test and the flat cull run */
        if (pct(50)) {   /* active records in and around the scanned cells: entity entries in queue C and the direct list (their fields are compared; the entity draw is the stub) */
            random_entity(&st.ent[0], st.scan.cell_x, st.scan.cell_z);
            st.ent[0].cell_x = st.scan.cell_x; st.ent[0].cell_z = st.scan.cell_z;
            if (pct(70)) st.ent[0].sub = st.scan.sub;
            if (pct(80)) random_entity(&st.ent[14], st.scan.cell_x, st.scan.cell_z);
            if (pct(30)) random_entity(&st.ent[15], st.scan.cell_x, st.scan.cell_z);
            for (int e = 1; e < 14; e++) if (pct(15)) random_entity(&st.ent[e], st.scan.cell_x, st.scan.cell_z);
            st.entities = 1;
        }
        char ctx[240];
        snprintf(ctx, sizeof ctx, "walk #%d cell (%d,%d) sub %d oct %d gate %d h %d off (%d,%d)", i, st.scan.cell_x, st.scan.cell_z, st.scan.sub, st.scan.octant, st.scan.gate, -st.scan.cam_y, st.scan.off_x, st.scan.off_z);
        state_to_rig(&st);
        state_to_port(&port, &st);
        rig_walk_prepare();
        lists_set_models(&port, world_lists_models(world));
        rig_call_builder(0x0B);
        lists_builder(&port, 0x0B);
        /* constructed entries (the builder never makes these combinations on purpose): a flat block with the pre-test and the flat cull, or a plain or common-drawer
         * item, with a random LOD and a position near the camera, replaces an entry of a random queue */
        if (pct(50)) {
            static const int flat_items[8] = { 27, 31, 50, 66, 67, 68, 69, 80 };
            int t = rnd(0, 2);
            if (port.kept[t] > 0) {
                int idx = rnd((int)port.start[t], (int)port.kept[t] - 1);
                lists_entry_t *e = &port.q[t][idx];
                if (!(e->flags & 0x50)) {
                    int item = flat_items[rnd(0, 7)], lod = rnd(0, 6), spread = (int[]){ 200, 800, 2500 }[rnd(0, 2)];
                    e->id = (uint8_t)item; e->flags = (uint8_t)lod;
                    e->x = (int16_t)((-(st.scan.off_x >> lod)) + rnd(-spread, spread)); e->y = 0; e->z = (int16_t)((-(st.scan.off_z >> lod)) + rnd(-spread, spread));
                    e->dist = 0; e->result = pct(50) ? 0 : -1; e->countdown = 0;
                    uint8_t b[24]; ls_entry_bytes(e, b);
                    static const uint32_t qa[3] = { 0x53A4A, 0x540DA, 0x5476A };
                    rig_poke(qa[t] + 24u * (unsigned)idx, b, 24);
                }
            }
        }
        /* constructed entries at the precision boundaries of P2 (note 04a section 8b): a model of the plain or common drawer whose block thresholds Tm / Tf are
         * non-zero, positioned so that the distance word lands within a few percent of Tm or Tf shifted right by the LOD (the vertex shift and the position exponent
         * change there, and with them the first-vertex test of rule 8 and the primitives) */
        if (pct(45)) {
            int t = rnd(0, 2);
            if (port.kept[t] > 0) {
                int idx = rnd((int)port.start[t], (int)port.kept[t] - 1);
                lists_entry_t *e = &port.q[t][idx];
                int item = 0, tm = 0, tf = 0, h, lo, found = 0, lod = rnd(0, 6), target = 0;
                for (int tries = 0; tries < 40 && !found; tries++) {
                    item = rnd(0, 133);
                    const int cls = lists_desc_of(&port, &(lists_entry_t){ .id = (uint8_t)item })->cls;
                    if (cls != LISTS_CLASS_PLAIN && cls != LISTS_CLASS_DRAW) continue;
                    if (item >= 17 && item <= 19) continue;   /* models 33-35 run op 70, which reads the object record the terrain walkers do not have (the original jumps into data) */
                    /* the boundary of the level the entry would choose at that distance: start at the first level's Tm or Tf, then follow the chosen level until it is stable */
                    if (world_model_block_precision(world, (unsigned)item + 16u, 0, &tm, &tf, &h, &lo) != 0 || tm <= 0 || tf <= 0) continue;
                    int use_tm = pct(50);
                    target = (use_tm ? tm : tf) >> lod;
                    for (int it = 0; it < 4; it++) {
                        lists_level_t lv;
                        if (world_pick_level_word(world, (unsigned)item + 16u, (unsigned)lod, (int16_t)target, &lv) != 0 || !lv.found) break;   /* zoom 0x80: the scaled distance is the word */
                        int ctm, ctf;
                        if (world_model_block_precision(world, (unsigned)item + 16u, lv.level, &ctm, &ctf, &h, &lo) != 0 || ctm <= 0 || ctf <= 0) break;
                        int nt = (use_tm ? ctm : ctf) >> lod;
                        if (nt == target) { found = 1; tm = ctm; tf = ctf; break; }
                        target = nt;
                    }
                }
                if (!(e->flags & 0x50) && found) {
                    target += target * rnd(-8, 8) / 100;
                    int span = 16 * target, sgn = pct(50) ? 1 : -1;
                    if (span < 30000) {
                        e->id = (uint8_t)item; e->flags = (uint8_t)lod;
                        if (pct(50)) { e->x = (int16_t)((-(st.scan.off_x >> lod)) + sgn * span); e->z = (int16_t)(-(st.scan.off_z >> lod)); }
                        else { e->z = (int16_t)((-(st.scan.off_z >> lod)) + sgn * span); e->x = (int16_t)(-(st.scan.off_x >> lod)); }
                        e->y = 0; e->dist = 0; e->result = 0; e->countdown = 0;
                        w.prec_built++;
                        uint8_t b[24]; ls_entry_bytes(e, b);
                        static const uint32_t qa[3] = { 0x53A4A, 0x540DA, 0x5476A };
                        rig_poke(qa[t] + 24u * (unsigned)idx, b, 24);
                    }
                }
            }
        }
        world_camera_t c; game_camera_t g;
        st.scan.cam_x = (int32_t)st.scan.off_x * 256; st.scan.cam_z = (int32_t)st.scan.off_z * 256;   /* V:4AB1A, V:4AB22 */
        walk_cam(&st.scan, st.scan.octant, &c, &g);
        world_set_camera(world, &c);
        rig_walk_state(&st.scan, &g);
        for (int k = 0; k < 6; k++) {
            int nn = (int)st.n + k;
            put16(0x4A992, nn); port.n = (uint32_t)nn;
            port.pitch_band = 0;
            {   /* the type B search never leaves its loop for a scaled distance above 0x7FFF >> LOD (models 39-42, 99, 100; note 03 "Type B models", unreachable in play): a dry run of the port
                 * finds such a walk, which is skipped (the original would not return) */
                static lists_t probe;
                probe = port;
                lists_walk(&probe);
                int endless = 0;
                for (unsigned q = 0; q < probe.nwalk; q++) {
                    const lists_walk_t *pw = &probe.walk[q];
                    if (pw->kind != 0 || !pw->draw) continue;
                    int cls = lists_desc_of(&probe, &probe.q[pw->tier][pw->index])->cls;
                    if ((cls == LISTS_CLASS_TYPEB1 || cls == LISTS_CLASS_TYPEB2) && lists_scaled_distance(pw->dist, probe.zoom, probe.zoom_fixed) > (0x7FFF >> pw->lod)) endless = 1;
                }
                if (endless) { w.endless_skipped++; continue; }
            }
            rig_trace_clear();
            if (getenv("LISTS_DIFF_TRACE")) {
                fprintf(stderr, "  rig walk %d\n", k);
                for (int t2 = 0; t2 < 3; t2++) for (int i2 = port.start[t2]; i2 < port.kept[t2]; i2++) { const lists_entry_t *e2 = &port.q[t2][i2]; fprintf(stderr, "    t%d #%d flags %02x id %d cls %d dist %u cd %d res %d\n", t2, i2, e2->flags, e2->id, (e2->flags & 0x50) ? -1 : lists_desc_of(&port, e2)->cls, e2->dist, e2->countdown, e2->result); }
            }
            if (getenv("LISTS_DIFF_TRACE")) fprintf(stderr, "   A\n");
            rig_run_a6(0x1FA24); if (getenv("LISTS_DIFF_TRACE")) fprintf(stderr, "   B\n"); rig_run_a6(0x1FA36); if (getenv("LISTS_DIFF_TRACE")) fprintf(stderr, "   C\n"); rig_run_a6(0x1FBCC);
            rig_segments(port.scan.cam_y < -0x280000);
            if (getenv("LISTS_DIFF_TRACE")) fprintf(stderr, "  port walk %d\n", k);
            lists_walk(&port);
            seg_expected = 0;
            for (unsigned q = 0; q < port.nwalk; q++) { walk_seg[q] = -1; if (port.walk[q].kind == 0 && port.walk[q].draw) walk_seg[q] = seg_expected++;   /* the rig's records are stubbed: only the terrain entries call a drawer */ }
            seg_checked++; if (!seg_ok || nseg != seg_expected) { seg_mismatch++; for (unsigned q = 0; q < port.nwalk; q++) walk_seg[q] = -1; }
            w.walks++;
            walk_compare(&w, ctx, k);
        }
        for (int h = 0; h < 10; h++) w.hits[h] += (long)port.rule_hits[h];
        w.states++;
    }
    printf("  %-34s %5ld states, %ld walks, %ld entries compared: stored result +0x14 equal in %ld (rig positive %ld, port positive %ld; rig positive and port not %ld, port positive and rig not %ld)\n",
           "walker and drawer (+0x14)", w.states, w.walks, w.entries, w.equal_result, w.pos_rig, w.pos_port, w.rig_pos_port_neg, w.rig_neg_port_pos);
    printf("    rule-layer differences (distance word, countdown, counter, or a result decided before the primitives): %ld (tier A %ld, B %ld, C %ld); primitive-layer differences (rule 9, informational): %ld\n",
           w.field_diff, w.by_tier[0], w.by_tier[1], w.by_tier[2], w.prim_diff);
    printf("    exits of the port's drawer by rule (2 0x21C34 cut, 3 type B cut, 4 no level, 5 tier substitution, 6 pre-test, 7 flat cull, 8 first vertex, 9 primitives; the pre-test ran %ld times): ", w.hits[1]);
    for (int h = 2; h < 10; h++) printf("%d:%ld ", h, w.hits[h]);
    printf("\n    primitive-layer differences, the rig's exit from its handler trace: %ld entered a handler of the primitive layer (proven), %ld left the drawer before any handler (counted as rule-layer), %ld without a trace match; the trace matched the port's walked entries in %ld of %ld walks\n",
           w.prim_proven, w.prim_rig_early, w.prim_unmatched, seg_checked - seg_mismatch, seg_checked);
    printf("    walks skipped because a type B search never ends in the original (scaled distance above 0x7FFF >> LOD, unreachable in play): %ld\n", w.endless_skipped);
    printf("    handler that decided the sign on the port's side (the last that counted, else the one that abandoned; op:handler address:count) where the rig was positive and the port not:");
    for (int h = 0; h < 80; h++) if (dec_rigpos[h]) printf(" %d:%x:%ld", h, op_handler[h], dec_rigpos[h]);
    printf("\n    the same where the port was positive and the rig not:");
    for (int h = 0; h < 80; h++) if (dec_portpos[h]) printf(" %d:%x:%ld", h, op_handler[h], dec_portpos[h]);
    printf("\n    first handler entered by the rig where it was positive and the port not (handler address:count):");
    for (int h = 0; h < nhandler; h++) if (hcount_rigpos[h]) printf(" %x:%ld", handler_addr[h], hcount_rigpos[h]);
    printf("\n    first handler entered by the rig where the port was positive and the rig not:");
    for (int h = 0; h < nhandler; h++) if (hcount_portpos[h]) printf(" %x:%ld", handler_addr[h], hcount_portpos[h]);
    printf("\n    items of the port-positive, rig-not entries (item:count):");
    for (int i = 0; i < 128; i++) if (items_portpos[i]) printf(" %d:%ld", i, items_portpos[i]);
    printf("\n    constructed precision-boundary entries (P2: distance word within 8 percent of Tm or Tf >> LOD): %ld\n", w.prec_built);
    {   /* the items with the most primitive-layer differences (item id = object id - 16), with the entries compared */
        printf("    primitive-layer differences by item (differing/compared):");
        for (int rank = 0; rank < 8; rank++) {
            int best = -1;
            for (int k = 0; k < 128; k++) if (w.prim_item[k] > 0 && (best < 0 || w.prim_item[k] > w.prim_item[best])) best = k;
            if (best < 0) break;
            printf(" %d (%ld/%ld)", best, w.prim_item[best], w.item_n[best]);
            w.prim_item[best] = -w.prim_item[best];
        }
        printf("\n");
    }
    printf("    entity and secondary entries of queue C (distance word from the record, no countdown): %ld compared, %ld differences\n", w.ent_n, w.ent_diff);
    if (w.first[0]) printf("    first rule-layer divergence: %s\n", w.first);
    if (w.first_prim[0]) printf("    first primitive-layer divergence: %s\n", w.first_prim);
    return w.field_diff ? 1 : 0;
}

static int family_logged(void) {
    stats_t s; memset(&s, 0, sizeof s);
    const char *lp = getenv("LISTS_LOG");
    static ls_log_t log;
    if (ls_load_log(&log, lp ? lp : "../../extracted/captures/logs/lists.txt") != 0 || !ls_find(&log, "q_", "deck")) {
        printf("  logged deck and airborne states      (skipped, no log)\n");
        return 0;
    }
    int bad = 0;
    static const char *scenes[2] = { "deck", "air" };
    for (int k = 0; k < 2; k++) {
        state_t st; memset(&st, 0, sizeof st);
        lists_t tmp;
        lists_init(&tmp, world_lists_tables(world), world_lists_cell, world);
        ls_load_state(&tmp, ls_find(&log, "s71_", scenes[k])->data, ls_find(&log, "recs_", scenes[k])->data, ls_find(&log, "sec_", scenes[k])->data);
        st.scan = tmp.scan; st.period = tmp.period; st.n = tmp.n; st.built = tmp.built;
        for (int i = 0; i < LISTS_ENTITIES; i++) { st.ent[i] = tmp.ent[i]; st.sec[i] = tmp.sec[i]; }
        bad += run_state(&s, &st, 0x0B, 40, scenes[k], k);
    }
    ls_free_log(&log);
    return finish("logged deck and airborne states", &s) + (bad ? 0 : 0);
}

static int family_random(int n, int mode) {
    static const char *names[5] = { "random states", "landmark states", "states with entities", "requests 0x0C and 0x8C", "(unused)" };
    stats_t s; memset(&s, 0, sizeof s);
    static const int reqs[4] = { 0x0B, 0x0F, 0xFF, 0x1B };
    for (int i = 0; i < n; i++) {
        state_t st;
        random_scan(&st);
        if (mode == 0) { place_near_populated(&st); }
        else if (mode == 1 && nland) {
            int k = rnd(0, nland - 1);
            set_cell(&st, land_x[k] + rnd(-2, 2), land_z[k] + rnd(-2, 2));
            st.scan.off_x = (int16_t)rnd(-12000, -2000); st.scan.off_z = (int16_t)rnd(-12000, -2000);
            if (pct(60)) set_height(&st.scan, rnd(0x500, 0x8000));
        } else if (mode >= 2) {
            place_near_populated(&st);
            if (pct(50)) { int k = nland ? rnd(0, nland - 1) : 0; if (nland) set_cell(&st, land_x[k] + rnd(-2, 2), land_z[k] + rnd(-2, 2)); }
        }
        if (mode == 2 || mode == 3) {
            random_entity(&st.ent[0], st.scan.cell_x, st.scan.cell_z);   /* the player: in the scanned cell (the cockpit's cells are the record's) */
            st.ent[0].cell_x = st.scan.cell_x; st.ent[0].cell_z = st.scan.cell_z;
            if (pct(70)) st.ent[0].sub = st.scan.sub;
            if (pct(80)) random_entity(&st.ent[14], st.scan.cell_x, st.scan.cell_z);
            if (pct(30)) random_entity(&st.ent[15], st.scan.cell_x, st.scan.cell_z);
            for (int e = 1; e < 14; e++) if (pct(8)) random_entity(&st.ent[e], st.scan.cell_x, st.scan.cell_z);
        }
        int req = reqs[rnd(0, 3)];
        char fam[40]; snprintf(fam, sizeof fam, "%s", names[mode]);
        if (mode == 3) {
            /* a full build, then a moved camera and cell, then a tier C only request */
            state_to_rig(&st); state_to_port(&port, &st);
            rig_call_builder(0x0B); lists_builder(&port, 0x0B);
            st.scan.sub = (uint8_t)rnd(0, 15); st.scan.octant = (uint8_t)rnd(0, 7);
            set_cell(&st, st.scan.cell_x + rnd(-1, 1), st.scan.cell_z + rnd(-1, 1));
            port.scan.cell_x = st.scan.cell_x; port.scan.cell_z = st.scan.cell_z; port.scan.sub = st.scan.sub; port.scan.octant = st.scan.octant; port.scan.quad = st.scan.quad;
            put16(0x4AA04, st.scan.cell_x); put16(0x4AA06, st.scan.cell_z); put8(0x4A909, st.scan.sub); put8(0x4A908, st.scan.quad); put8(0x4A90C, st.scan.octant);
            req = pct(50) ? 0x0C : 0x8C;
            char ctx[160]; snprintf(ctx, sizeof ctx, "request %02x after a full build, #%d", req, i);
            rig_call_builder(req); lists_builder(&port, (uint8_t)req);
            compare_state(&s, &port, ctx); s.states++;
            for (int c = 0; c < 6; c++) { rig_call_builder(0); lists_builder(&port, 0); compare_state(&s, &port, ctx); }
            continue;
        }
        run_state(&s, &st, req, 6, fam, i);
    }
    return finish(names[mode], &s);
}

int main(int argc, char **argv) {
    if (argc > 1) root = argv[1];
    verbose = getenv("LISTS_DIFF_VERBOSE") != NULL;
    if (getenv("LISTS_DIFF_SEED")) rng = (uint32_t)strtoul(getenv("LISTS_DIFF_SEED"), NULL, 0) | 1u;
    int n = env_int("LISTS_DIFF_N", 400);
    char err[160] = "";
    uint8_t *probe; size_t pl;
    if (fs_read(NULL, "models/index.txt", &probe, &pl) != 0 || rig_setup() != 0) { printf("test_lists_diff: skipped (no assets or extracted/interceptor.bin)\n"); return 0; }
    free(probe);
    rig_set_max_instructions(4000000u);   /* a walker call is far below this; an endless type B search ends as a counted timeout instead of 50 million steps */
    world = world_load(&loader, err, sizeof err);
    if (!world) { printf("test_lists_diff: skipped (%s)\n", err); return 0; }
    collect_cells();
    printf("display-list builder: port against the original 0x1F770 (%d table C cells, %d landmark cells)\n", ncells_c, nland);
    int bad = 0;
    bad += family_logged();
    bad += family_random(n, 0);
    bad += family_random(n / 2, 1);
    bad += family_random(n / 2, 2);
    bad += family_random(n / 4, 3);
    bad += family_walk(n / 2);
    world_destroy(world);
    printf("%s\n", bad ? "FAILED" : "all families equal");
    return bad ? 1 : 0;
}
