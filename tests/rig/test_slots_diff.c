/* tests/rig/test_slots_diff.c - differential test of the display lists' vertex-slot store (src/core/lists.c store_vslots, run_drawer) against the original's entity drawer in the Musashi rig
 * (docs/re/07-ai-and-weapons.md 8.2a, note 05 `+0xA4`, note 03 "Vertex synthesis").
 *
 * The original: the model drawer 0x21C44 is called directly for one record with A0 = the model base and V:4AAEE = its detail list, both read from the record table of the image (0x25ECC, 20 bytes a
 * record, relocated to the image base), the camera and level inputs poked (the scaled distance V:4ABF8 and the LOD V:4AB70 are the values the port's walker computes for the entry), the record
 * poked (W0, +0x4C, the matrix +0x92, +0xA4.. prefilled with random words, +0x7B prefilled). The descriptor routine 0x26944 only converts a crashed record and ends in the same jump, so it is not
 * needed; the model pointers of the record table are already absolute in the flat image.
 *
 * The port: lists_draw_entity (the common drawer for a record of the direct list) with the same record, LOD and distance word, the same prefilled slots.
 *
 * Compared after the call: the 58 slots of the record (+0xA4 to +0x1FF), the part id +0x7B, the head of the next record (nothing may be written past the record by the vertex stage).
 *
 * What the original runs: the whole drawer, the polygon stage included, the rasteriser entry points (0x34A28, 0x340D4, 0x340EA, 0x34558, 0x3377C) replaced by returns; the slots then hold the block's own
 * vertices rotated and the fragments the programs' ops 42 to 44, 56, 59 and 63 to 65 run on the record's array. The original's vertex loop stores ONE slot more than the block has vertices (data read
 * from after the last vertex, rotated like a vertex): that slot is not a vertex, nothing reads it, and it is not compared (counted). The results of the ops for the slots 100 and 110 are written nowhere
 * (the record after is compared whole, its fields +0 to +0xA3 included). Environment: SLOTS_DIFF_N, SLOTS_DIFF_SEED, SLOTS_DIFF_VERBOSE, SLOTS_DIFF_DUMP (the block and the slots of a differing case),
 * SLOTS_DIFF_MODELS (a list such as 3,6,8). Run from tools/m68k-rig (make test-slots). Skipped (exit 0) without extracted/interceptor.bin or the extracted assets. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/world.h"
#include "../../src/core/lists.h"
#include "../../src/core/fx.h"

#define BASE 0x10000u
#define V_LO 0x4A6E8u
#define V_HI 0x54EC0u
#define REC 0x4B23Cu
#define RECA(i) (REC + (uint32_t)(i) * 0x200u)
#define RECORD_TABLE 0x25ECCu

static void put8(uint32_t a, int v) { uint8_t b = (uint8_t)v; rig_poke(a, &b, 1); }
static void put16(uint32_t a, int v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 2); }
static void put32(uint32_t a, int32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 4); }
static int get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return (int16_t)((b[0] << 8) | b[1]); }
static int get8(uint32_t a) { uint8_t b; rig_peek(a, &b, 1); return b; }
static uint32_t get32(uint32_t a) { uint8_t b[4]; rig_peek(a, b, 4); return ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]; }

static int chute_cases, chute_p0, chute_p0_nibble;
static uint32_t rng = 0x2545F491u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }
static int pct(int p) { return rnd(0, 99) < p; }

static const char *assets = "../../assets";
static int fs_read(void *user, const char *path, uint8_t **out, size_t *len) {
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
static void fs_release(void *user, uint8_t *buf) { (void)user; free(buf); }
static game_loader_t loader = { fs_read, fs_release, NULL };

static const char *image_path;
static world_t *WORLD;

static void rig_reset_state(void) {
    if (rig_load_image(image_path, BASE) != 0) { printf("image load failed\n"); exit(1); }
    static uint8_t zero[V_HI - V_LO];
    rig_poke(V_LO, zero, sizeof zero);
    rig_hooks_clear();
    static const uint32_t stubs[] = { 0x34A28, 0x340D4, 0x340EA, 0x34558, 0x3377C };   /* the rasteriser entry points return at once; every other routine runs as it is */
    for (unsigned i = 0; i < sizeof stubs / sizeof *stubs; i++) put16(stubs[i], 0x4E75);
}

static int diffs, cases, compared_slots, next_written;
static int verbose;

static int run_case(int model, int mode) {   /* mode 0: a record 1 to 13 in the external view; 1: the player's entry, near body variant; 2: the player's entry, the cockpit block variant */
    lists_t *L = world_lists(WORLD);
    const int near = mode == 1, cockpit = mode == 2;
    const int ri = mode ? 0 : rnd(1, 13);   /* near: the player's own entry (record 0) in the cockpit with the pitch band 3: test 7 of 04a 8a walks the alternate list, the near body block */
    /* the matrix: three random angles through the matrix-B builder (the record's +0x92 words, Q14) */
    fx_mat_t fm;
    fx_matrix_b(&fm, (int16_t)rnd(0, 28799), (int16_t)rnd(0, 28799), (int16_t)rnd(0, 28799));
    int16_t mat[9];
    for (int i = 0; i < 9; i++) mat[i] = (int16_t)fm.m[i / 3][i % 3];
    const int lod = pct(40) ? 0 : (int)rnd(0, 6);
    const int16_t dist = (int16_t)(pct(30) ? rnd(0, 0x200) : pct(60) ? rnd(0, 0x1800) : rnd(0, 0x7FFF));
    const int dying = pct(20);
    int counter = dying ? (int)rnd(-3, 15) : (int)rnd(-3, 40);
    if (model == 11 && pct(15)) counter = 0;   /* the parachute's phase 0 is the slot case: the list depends on the low nibble of +0x7C (note 07 11.5) */
    int16_t pre[58][3];
    for (int k = 0; k < 58; k++) for (int a = 0; a < 3; a++) pre[k][a] = (int16_t)rnd(-30000, 30000);
    const int pre_part = (int)rnd(0, 255);
    const int32_t cam_y = -0x10000 - rnd(0, 0x100000);
    const int w04 = pct(50) ? (int)rnd(-4, 140) : (int)rnd(-32768, 32767);
    int b7c = (int)rnd(0, 255);
    if (model == 11 && counter == 0 && pct(50)) b7c &= 0xF0;   /* +0x7C: the parachute's list at the phase 0 depends on its low nibble */

    /* ---- the original */
    rig_reset_state();
    const uint32_t desc = RECORD_TABLE + 20u * (uint32_t)model;
    const uint32_t base = get32(desc + 4);
    uint32_t list = get32(desc + 8);
    const int cone = model == 2 || model == 13 || model == 11;
    const int own_handler = cone && !getenv("SLOTS_DIFF_TESTLIST");   /* the original's own cone handler (0x2699E for the pod, 0x26A3E for the parachute) chooses the list and sets +0x7C bit 0 (milestone 7: the rule is no longer written into the test) */
    if (cone && !own_handler) {   /* SLOTS_DIFF_TESTLIST: the earlier form, the list chosen by the test from the note 07 11.5 table, as lists.c cone_list does */
        static const uint32_t pod[5] = { 0x41638, 0x41658, 0x41666, 0x41678, 0x41674 }, chute[5] = { 0x418DE, 0x418FE, 0x4190C, 0x418DA, 0x4191A };
        const uint32_t *t = model == 11 ? chute : pod;
        const int p = counter;
        int idx = p >= 5 ? 0 : p >= 3 ? 1 : p >= 1 ? 2 : p == 0 ? 3 : 4;
        list = t[idx];
        if (model == 11 && p == 0 && (b7c & 15) == 0) list = 0x4191A;
        put32(0x4AAF2, (int32_t)list);
    }
    put16(RECA(ri), 0x0040 | (dying ? 0x0400 : 0));
    put16(RECA(ri) + 0x4C, counter);
    put16(0x4AA6C, ri);
    put16(0x4D23C + 0x20u * (uint32_t)ri + 4, w04);   /* op 70 of a model program reads the word at +0x04 of the secondary object V:4AA6C, never the record's (note 07 11.3) */
    for (int i = 0; i < 9; i++) put16(RECA(ri) + 0x92 + 2 * (uint32_t)i, mat[i]);
    for (int k = 0; k < 58; k++) for (int a = 0; a < 3; a++) put16(RECA(ri) + 0xA4 + 6 * (uint32_t)k + 2 * (uint32_t)a, pre[k][a]);
    put8(RECA(ri) + 0x7B, pre_part);
    put8(RECA(ri) + 0x7C, b7c);
    int16_t nxt[58][3];
    for (int k = 0; k < 58; k++) for (int a = 0; a < 3; a++) { nxt[k][a] = (int16_t)rnd(-30000, 30000); put16(RECA(ri + 1) + 0xA4 + 6 * (uint32_t)k + 2 * (uint32_t)a, nxt[k][a]); }
    put16(0x4AA6E, ri * 0x200); put16(0x4A996, 0); put16(0x4A994, 0);
    put8(0x4A83D, mode ? 0 : 1);
    if (near) { put8(0x4A923, 3); put32(0x4AAF2, (int32_t)get32(desc + 12)); }
    if (cockpit) { put8(0x4A923, 0); put32(0x4AAF2, (int32_t)get32(desc + 12)); }   /* test 9 of 04a 8a: band 0 and E below -32: the cockpit block, part id 0xFF */   /* the descriptor's alternate list (field 3, +12): the prelude 0x21C44 makes it the list of the draw (V:4AAEE) when it differs from the normal list */
    put8(0x4A91C, 1);   /* queue C and the direct list: the walker's tier flag (rule 5 of note 04a 8b) */
    put16(0x4AAFA, 0x80); put16(0x4ABF8, dist); put16(0x4AB70, lod);
    put32(0x4AB30, cam_y);
    put32(0x4AAEE, (int32_t)list);
    for (int d = 0; d < 3; d++) put16(0x4AC90 + 2 * (d * 3 + d), 0x100);
    for (int rr = RIG_D0; rr <= RIG_A7; rr++) if (rr != RIG_A7) rig_set_reg((rig_reg_t)rr, 0);
    rig_set_reg(RIG_A0, base);
    if (rig_run(own_handler ? (model == 11 ? 0x26A3Eu : 0x2699Eu) : 0x21C44u) != 0) { if (verbose) printf("  rig run failed (%d) model %d dying %d counter %d\n", rig_last_error, model, dying, counter); return -1; }

    /* ---- the port */
    lists_entity_t *e = &L->ent[ri];
    memset(e, 0, sizeof *e);
    e->active = 1; e->model = (uint16_t)model;
    for (int i = 0; i < 9; i++) e->mat_b[i] = mat[i];
    e->damage = dying ? 4 : 0; e->phase = (int16_t)counter; e->b7c = (uint8_t)b7c; L->sec[ri].phase = (int16_t)w04;
    memcpy(L->vslot[ri], pre, sizeof pre);
    memcpy(L->vslot[ri + 1], nxt, sizeof nxt);
    L->part_id[ri] = (uint8_t)pre_part;
    L->scan.cam_y = cam_y;
    L->view.external = mode ? 0 : 1; L->view.viewed = 0; L->zoom = 0x80; L->pitch_band = near ? 3 : 0;
    int completed = 0;
    lists_draw_entity(L, (unsigned)ri, (unsigned)lod, (uint16_t)dist, &completed);

    cases++;
    int bad = 0;
    /* The slots stored are decided by the rule of note 07 8.10 rule 5, not by the original's run: the loop stores one vertex, then b9 more, then b8 - b9 - 1 more while that is positive, so a block with
     * b8 = b9 stores b8 + 1 slots (the last is the rotated data triple after the vertex list) and a block with b8 > b9 stores b8. The port must end at exactly that slot (the programs' record copies may
     * write further slots), and every slot is compared as it stands, the trailing one included. b8 < b9 abandons the draw (the error post 0x17): no shipped block has it, the case is not run. */
    int Pr = 0, P = 0;
    for (int k = 0; k < 58; k++) {
        int chr = 0, chp = 0;
        for (int a = 0; a < 3; a++) { chr |= get16(RECA(ri) + 0xA4 + 6 * (uint32_t)k + 2 * (uint32_t)a) != pre[k][a]; chp |= L->vslot[ri][k][a] != pre[k][a]; }
        if (chr) Pr = k + 1;
        if (chp) P = k + 1;
    }
    {   /* the rule against the original: a block of the record kind with b8 = b9 changed its slot b8 (the data triple), whatever else the programs wrote */
        const uint32_t blk = get32(0x4AAEA);
        const int b8 = get8(blk + 8), b9 = get8(blk + 9);
        if (Pr > 0 && (get8(blk + 7) & 1) && b8 == b9 && b8 < 58) {
            int chr = 0;
            for (int a = 0; a < 3; a++) chr |= get16(RECA(ri) + 0xA4 + 6 * (uint32_t)b8 + 2 * (uint32_t)a) != pre[b8][a];
            if (!chr) { printf("  model %d: the original did not store the trailing data slot %d of a block with b8 = b9\n", model, b8); bad = 1; }
            if (b8 + 1 > Pr) { printf("  model %d: the original's slots end at %d, before the trailing slot %d\n", model, Pr, b8); bad = 1; }
        }
        if (b8 < b9) { printf("  model %d: a block with b8 < b9\n", model); bad = 1; }
    }
    if (P != Pr) {
        if (verbose || diffs < 5) printf("  model %d lod %d dist %d dying %d counter %d mode %d: the original changed %d slots, the port %d\n", model, lod, dist, dying, counter, mode, Pr, P);
        bad = 1;
    }
    for (int k = 0; k < 58 && !bad; k++)
        for (int a = 0; a < 3; a++) {
            const int rv = get16(RECA(ri) + 0xA4 + 6 * (uint32_t)k + 2 * (uint32_t)a);
            compared_slots++;
            if (rv != L->vslot[ri][k][a]) {
                if (verbose || diffs < 5) printf("  model %d lod %d dist %d dying %d counter %d: slot %d axis %d port %d rig %d (pre %d), the port stored %d slots\n", model, lod, dist, dying, counter, k, a, L->vslot[ri][k][a], rv, pre[k][a], P);
                if (getenv("SLOTS_DIFF_DUMP")) {
                    const uint32_t a1 = get32(0x4AAEA);
                    printf("    block %X:", a1); for (int i = 0; i < 24; i++) printf(" %02X", get8(a1 + (uint32_t)i)); printf("\n");
                    for (int kk = 0; kk < P + 2 && kk < 58; kk++) printf("    slot %2d port (%6d %6d %6d) rig (%6d %6d %6d) pre (%6d %6d %6d)\n", kk, L->vslot[ri][kk][0], L->vslot[ri][kk][1], L->vslot[ri][kk][2],
                        get16(RECA(ri) + 0xA4 + 6 * (uint32_t)kk), get16(RECA(ri) + 0xA6 + 6 * (uint32_t)kk), get16(RECA(ri) + 0xA8 + 6 * (uint32_t)kk), pre[kk][0], pre[kk][1], pre[kk][2]);
                }
                bad = 1; break;
            }
        }
    /* the record after: op 42 writes the slot b + 100, which lies in it (3 words at its +0xFC); its slots must agree and its other fields (+0 to +0xA3) must be untouched */
    for (int k = 0; k < 58 && !bad; k++)
        for (int a = 0; a < 3; a++) {
            const int rv = get16(RECA(ri + 1) + 0xA4 + 6 * (uint32_t)k + 2 * (uint32_t)a);
            if (rv != L->vslot[ri + 1][k][a]) { printf("  model %d: NEXT record slot %d axis %d port %d rig %d (pre %d)\n", model, k, a, L->vslot[ri + 1][k][a], rv, nxt[k][a]); bad = 1; break; }
            if (rv != nxt[k][a]) next_written++;
        }
    for (int o = 0; o < 0xA4 && !bad; o += 2) if (get16(RECA(ri + 1) + (uint32_t)o) != 0) { printf("  model %d: the record after has its field +0x%X written (%d)\n", model, o, get16(RECA(ri + 1) + (uint32_t)o)); bad = 1; }
    /* the parachute's handler also sets +0x7C bit 0 from the phase 1 up; the port does it in game.c draw_handler_one (the record's gear byte), not in the list code, so the expectation is the port's byte with that rule */
    const int b7c_after = e->b7c | ((model == 11 && counter >= 1) ? 1 : 0);
    if (!bad && own_handler && get8(RECA(ri) + 0x7C) != b7c_after) {
        if (verbose || diffs < 5) printf("  model %d phase %d: +0x7C port %02X rig %02X (before %02X)\n", model, counter, b7c_after, get8(RECA(ri) + 0x7C), b7c);
        bad = 1;
    }
    if (own_handler && model == 11) { chute_cases++; if (counter == 0) { chute_p0++; if ((b7c & 15) != 0) chute_p0_nibble++; } }
    if (!bad && get8(RECA(ri) + 0x7B) != L->part_id[ri]) {
        if (verbose || diffs < 5) printf("  model %d lod %d dist %d: part id port %02X rig %02X\n", model, lod, dist, L->part_id[ri], get8(RECA(ri) + 0x7B));
        bad = 1;
    }
    if (bad) diffs++;
    return bad;
}

int main(void) {
    const char *paths[] = {"../../extracted/interceptor.bin", "extracted/interceptor.bin"};
    char err[160];
    if (rig_init(1 << 21) != 0) { printf("rig_init failed\n"); return 1; }
    for (int i = 0; i < 2 && !image_path; i++) if (rig_load_image(paths[i], BASE) == 0) image_path = paths[i];
    if (!image_path) { printf("test_slots_diff: skipped (extracted/interceptor.bin not found)\n"); rig_shutdown(); return 0; }
    { FILE *t = fopen("../../assets/VERSION", "r"); if (!t) assets = "assets"; else fclose(t); }
    WORLD = world_load(&loader, err, sizeof err);
    if (!WORLD) { printf("test_slots_diff: skipped (%s)\n", err); rig_shutdown(); return 0; }
    const int N = getenv("SLOTS_DIFF_N") ? atoi(getenv("SLOTS_DIFF_N")) : 300;
    if (getenv("SLOTS_DIFF_SEED")) rng = (uint32_t)atoi(getenv("SLOTS_DIFF_SEED")) * 2654435761u + 1;
    verbose = getenv("SLOTS_DIFF_VERBOSE") != NULL;
    int models[16], nm = 0;
    if (getenv("SLOTS_DIFF_MODELS")) {
        const char *p = getenv("SLOTS_DIFF_MODELS");
        while (*p && nm < 16) { models[nm++] = atoi(p); while (*p && *p != ',') p++; if (*p == ',') p++; }
    } else { static const int def[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }; nm = (int)(sizeof def / sizeof *def); memcpy(models, def, sizeof def); }
    int skipped = 0;
    for (int m = 0; m < nm; m++) {
        int d0 = diffs, c0 = cases, sk0 = skipped;
        for (int c = 0; c < N; c++) { int r = run_case(models[m], 0); if (r == -1) skipped++; }
        if (models[m] == 3 || models[m] == 4)   /* the player's templates: the near body variant and the cockpit block variant of the entry */
            for (int mode = 1; mode <= 2; mode++)
                for (int c = 0; c < N; c++) if (run_case(models[m], mode) < 0) skipped++;
        printf("model %2d: %d cases, %d differences, %d rig runs failed\n", models[m], cases - c0, diffs - d0, skipped - sk0);
    }
    printf("test_slots_diff: %d cases, %d slots compared (%d words written into the record after), %d differences, %d skipped\n", cases, compared_slots, next_written, diffs, skipped);
    if (chute_cases) printf("test_slots_diff: the original's parachute handler 0x26A3E ran in %d cases (%d at phase 0, %d of those with a non-zero low nibble of +0x7C and %d with a zero one: the slot case)\n", chute_cases, chute_p0, chute_p0_nibble, chute_p0 - chute_p0_nibble);
    rig_shutdown();
    return diffs ? 1 : 0;
}
