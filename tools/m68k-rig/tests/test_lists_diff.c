/* tools/m68k-rig/tests/test_lists_diff.c - the display-list builder (0x1F770) in the rig against the
 * three queues peeked from the reference emulator (docs/re/04a-display-lists.md, "Verified by").
 * Input: extracted/captures/logs/lists.txt, written by `ref --script tools/vamiga-ref/scripts/openers-lists.txt
 * --log ...` (override the path with LISTS_LOG). Skipped when the log or extracted/interceptor.bin is absent.
 * Run from tools/m68k-rig (make test-lists). Loaded into the rig for each scene (suffix deck, air):
 *   s71_  state hunk 71 from 0x4A700 (2336 bytes: scan inputs, camera words, request byte, counters)
 *   recs_ the 16 entity records at 0x4B23C (8192 bytes)    sec_ the 16 secondary objects at 0x4D23C (512 bytes)
 * compared: q_ at 0x53A4A, 3 queues of 70 x 24 bytes: id/flags word, X, Y, Z, key long (bytes 0-1 and 6-15 of an entry). */
#include "../../../tests/test.h"
#include "../rig.h"
#include <stdint.h>
#include <ctype.h>

#define MEM 0x100000u
#define QADDR 0x53A4Au
#define QSIZE 5040
#define BUILDER 0x1F770u
#define OCC_MAPS 0x1F31Cu

typedef struct { char label[64]; uint32_t addr; uint8_t *data; size_t n; } blob_t;
static blob_t blobs[64]; static int nblobs;

/* A portable line reader (getline is not on msys2): returns the length, 0 at the end of the file; *buf grows as needed. */
static size_t read_line(char **buf, size_t *cap, FILE *f) {
    size_t n = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (n + 2 > *cap) {
            size_t nc = *cap ? *cap * 2 : 4096;
            char *nb = realloc(*buf, nc);
            if (!nb) break;
            *buf = nb; *cap = nc;
        }
        (*buf)[n++] = (char)c;
        if (c == '\n') break;
    }
    if (*buf) (*buf)[n] = 0;
    return n;
}

static int load_log(const char *path) {
    FILE *f = fopen(path, "r"); if (!f) return -1;
    char *line = NULL; size_t cap = 0;
    while (read_line(&line, &cap, f) > 0 && nblobs < 64) {
        char lab[64], at[32]; int off = 0;
        if (sscanf(line, "%63s %31s%n", lab, at, &off) < 2 || at[0] != 'F' || at[1] != ':') continue;
        blob_t *b = &blobs[nblobs];
        snprintf(b->label, sizeof b->label, "%s", lab);
        b->addr = (uint32_t)strtoul(at + 2, NULL, 16);
        size_t maxn = strlen(line) / 2 + 1; b->data = malloc(maxn); b->n = 0;
        int hi = -1;
        for (const char *p = line + off; *p; p++) {
            if (!isxdigit((unsigned char)*p)) continue;
            int v = isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10);
            if (hi < 0) hi = v; else { b->data[b->n++] = (uint8_t)(hi * 16 + v); hi = -1; }
        }
        nblobs++;
    }
    free(line); fclose(f); return 0;
}
static blob_t *find(const char *prefix, const char *scene) {
    char l[40]; snprintf(l, sizeof l, "%s%s", prefix, scene);
    for (int i = 0; i < nblobs; i++) if (!strcmp(blobs[i].label, l)) return &blobs[i];
    return NULL;
}
static int ready(const char *log) {
    if (nblobs) return 1;
    FILE *f = fopen("../../extracted/interceptor.bin", "rb"); if (!f) return 0; fclose(f);
    if (load_log(log) != 0) return 0;
    return find("q_", "deck") && find("q_", "air");
}

/* load a scene into a fresh rig and run the builder with a request of 0x0B (the low nibble every flight writer uses) */
static void build(const char *scene, int dz_u, uint8_t out[QSIZE]) {
    static const uint8_t zero[0x54EC0 - 0x4A6E8];
    rig_init(MEM);
    rig_load_image("../../extracted/interceptor.bin", 0x10000);
    rig_poke(0x4A6E8, zero, sizeof zero);
    const char *pre[3] = { "s71_", "recs_", "sec_" };
    for (int i = 0; i < 3; i++) { blob_t *b = find(pre[i], scene); rig_poke(b->addr, b->data, b->n); }
    for (int r = 0; r < 16; r++) rig_set_reg(r, 0);
    rig_run(OCC_MAPS);                                   /* occupancy maps: a speed-up, built once at mission start */
    uint8_t ids[44]; for (int i = 0; i < 22; i++) { ids[2 * i] = 0; ids[2 * i + 1] = (uint8_t)i; }
    rig_poke(0x53870, ids, sizeof ids);                  /* word table 0..21 the scan reads */
    if (dz_u) {                                          /* camera offset word V:4AB2E moved by dz_u position-word units */
        uint8_t w[2]; rig_peek(0x4AB2E, w, 2);
        int v = (int16_t)(w[0] << 8 | w[1]) + dz_u; w[0] = (uint8_t)(v >> 8); w[1] = (uint8_t)v; rig_poke(0x4AB2E, w, 2);
    }
    uint8_t req = 0x0B; rig_poke(0x4A910, &req, 1);
    for (int r = 0; r < 16; r++) rig_set_reg(r, 0);
    rig_set_reg(RIG_A6, 0x70000);
    rig_run(BUILDER);
    rig_peek(QADDR, out, QSIZE);
    rig_shutdown();
}

/* number of emulator entries (all three queues) and of equal ones; -1 when the counts differ */
static int compare(const uint8_t *emu, const uint8_t *rig, int *total) {
    int same = 0; *total = 0;
    for (int q = 0; q < 3; q++) {
        int ne = 0, nr = 0;
        while (ne < 70 && !(emu[q * 1680 + 24 * ne] == 0xFF && emu[q * 1680 + 24 * ne + 1] == 0xFF)) ne++;
        while (nr < 70 && !(rig[q * 1680 + 24 * nr] == 0xFF && rig[q * 1680 + 24 * nr + 1] == 0xFF)) nr++;
        if (ne != nr) return -1;
        for (int i = 0; i < ne; i++) {
            const uint8_t *a = emu + q * 1680 + 24 * i, *b = rig + q * 1680 + 24 * i;
            (*total)++;
            if (!memcmp(a, b, 2) && !memcmp(a + 6, b + 6, 10)) same++;
        }
    }
    return same;
}

static const char *logpath(void) { const char *e = getenv("LISTS_LOG"); return e ? e : "../../extracted/captures/logs/lists.txt"; }

TEST(deck_queues_equal_the_emulators) {
    if (!ready(logpath())) { printf("(skipped, no extracted/ log) "); return; }
    uint8_t rig[QSIZE]; int total;
    build("deck", 0, rig);
    int same = compare(find("q_", "deck")->data, rig, &total);
    ASSERT_TRUE(total >= 1);
    ASSERT_EQ_INT(same, total);          /* 11 of 11 in the recorded capture */
}

TEST(airborne_queues_equal_the_emulators_up_to_the_lod_nibble) {
    if (!ready(logpath())) { printf("(skipped, no extracted/ log) "); return; }
    uint8_t rig[QSIZE]; int total;
    build("air", 0, rig);
    int same = compare(find("q_", "air")->data, rig, &total);
    ASSERT_TRUE(total >= 1);
    ASSERT_TRUE(same >= total - 2);      /* 86 of 88: the snapshot is some steps after the last rebuild */
    /* with the camera offset moved back to its value at the rebuild (a window of about 210..1580 units in
     * the recorded capture; the moved distance is the player's z velocity times 5 to 31 steps) all agree */
    int best = 0;
    for (int d = 0; d <= 3000 && best != total; d += 50) {
        build("air", d, rig);
        int s = compare(find("q_", "air")->data, rig, &total);
        if (s > best) best = s;
    }
    ASSERT_EQ_INT(best, total);
}

int main(void) {
    RUN(deck_queues_equal_the_emulators);
    RUN(airborne_queues_equal_the_emulators_up_to_the_lod_nibble);
    TEST_MAIN_END();
}
