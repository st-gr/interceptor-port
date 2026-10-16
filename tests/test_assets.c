/* tests/test_assets.c - core_assets_load_index and helpers. */
#include "test.h"
#include "../src/core/assets.h"

/* user: pointer to the file text; NULL means the file is missing. */
static int rd(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)path;
    if (!user) return -1;
    size_t n = strlen((const char *)user);
    *out = (uint8_t *)malloc(n + 1);
    memcpy(*out, user, n);
    *len = n;
    return 0;
}
static void rel(void *user, uint8_t *buf) { (void)user; free(buf); }

static int load(const char *text, char names[][CORE_ASSET_NAME_LEN], int max) {
    game_loader_t ld = { rd, rel, (void *)text };
    return core_assets_load_index(&ld, "x/index.txt", names, max);
}

TEST(lf_lines) {
    char n[8][CORE_ASSET_NAME_LEN];
    ASSERT_EQ_INT(load("a\nbb\nccc\n", n, 8), 3);
    ASSERT_STR_EQ(n[0], "a"); ASSERT_STR_EQ(n[1], "bb"); ASSERT_STR_EQ(n[2], "ccc");
}
TEST(crlf_lines) {
    char n[8][CORE_ASSET_NAME_LEN];
    ASSERT_EQ_INT(load("a\r\nbb\r\n", n, 8), 2);
    ASSERT_STR_EQ(n[0], "a"); ASSERT_STR_EQ(n[1], "bb");
}
TEST(no_trailing_newline) {
    char n[8][CORE_ASSET_NAME_LEN];
    ASSERT_EQ_INT(load("a\nb", n, 8), 2);
    ASSERT_STR_EQ(n[1], "b");
}
TEST(blank_lines_skipped) {
    char n[8][CORE_ASSET_NAME_LEN];
    ASSERT_EQ_INT(load("\na\n\n\r\nb\n\n", n, 8), 2);
    ASSERT_STR_EQ(n[0], "a"); ASSERT_STR_EQ(n[1], "b");
}
TEST(name_of_31_chars_ok_32_rejected) {
    char n[8][CORE_ASSET_NAME_LEN];
    ASSERT_EQ_INT(load("1234567890123456789012345678901\n", n, 8), 1);
    ASSERT_EQ_INT(load("12345678901234567890123456789012\n", n, 8), -1);
}
TEST(more_than_max_rejected) {
    char n[2][CORE_ASSET_NAME_LEN];
    ASSERT_EQ_INT(load("a\nb\n", n, 2), 2);
    ASSERT_EQ_INT(load("a\nb\nc\n", n, 2), -1);
}
TEST(empty_file_rejected) {
    char n[2][CORE_ASSET_NAME_LEN];
    ASSERT_EQ_INT(load("", n, 2), -1);
    ASSERT_EQ_INT(load("\n\n", n, 2), -1);
}
TEST(missing_file_rejected) {
    char n[2][CORE_ASSET_NAME_LEN];
    ASSERT_EQ_INT(load(NULL, n, 2), -1);
}
TEST(append_helpers_bound_and_format) {
    char b[16] = "";
    core_assets_append(b, sizeof b, "v");
    core_assets_append_u32(b, sizeof b, 0);
    core_assets_append_u32(b, sizeof b, 4294967295u);
    ASSERT_STR_EQ(b, "v04294967295");
    core_assets_append(b, sizeof b, "xxxxxxxxxx");
    ASSERT_EQ_INT(strlen(b), 15);
}

/* ---- game-flow assets (synthetic files) ---- */
typedef struct { uint8_t d[512]; size_t n; const char *name; } fileset_t;
static int rdf(void *user, const char *path, uint8_t **out, size_t *len) {
    fileset_t *f = (fileset_t *)user;
    if (!f->name || strcmp(path, f->name) != 0) return -1;
    *out = (uint8_t *)malloc(f->n ? f->n : 1);
    memcpy(*out, f->d, f->n);
    *len = f->n;
    return 0;
}
static void put16(fileset_t *f, unsigned v) { f->d[f->n++] = (uint8_t)(v & 255); f->d[f->n++] = (uint8_t)(v >> 8); }
static void put_words(fileset_t *f, int n, const unsigned *w) { for (int i = 0; i < n; i++) put16(f, w[i]); }

static void scenario_file(fileset_t *f) {
    static const unsigned start[8] = { 11, 12, 5, 21, 22, 23, 0xFFE8, 37 };
    static const unsigned reg[7] = { 1, 2, 3, 4, 1, 0, 1 };
    static const unsigned ent[5] = { 40, 7, 9, 31, 0x8001 };
    static const unsigned row[6] = { 0, 2, 0, 3, 0, 4 };
    static const unsigned lst[4] = { 0, 0, 1, 1 };
    static const unsigned sub[4] = { 0, 1, 1, 0 };
    static const unsigned se[5] = { 100, 5, 0x8003, 50, 0x4001 };
    f->n = 0; f->name = "tables/scenario.bin";
    put16(f, 1); put16(f, 16); put_words(f, 8, start);
    put16(f, 1); put16(f, 14); put_words(f, 7, reg);
    put16(f, 1); put16(f, 10); put_words(f, 5, ent);
    put16(f, 1); put16(f, 12); put_words(f, 6, row);
    put16(f, 1); put16(f, 8); put_words(f, 4, lst);
    put16(f, 2); put16(f, 4); put_words(f, 4, sub);
    put16(f, 1); put16(f, 10); put_words(f, 5, se);
    put16(f, 0); put16(f, 0);
}

TEST(scenario_reads_every_field_little_endian) {
    fileset_t f; scenario_t sc; char err[96] = "";
    scenario_file(&f);
    game_loader_t ld = { rdf, rel, &f };
    ASSERT_EQ_INT(assets_load_scenario(&ld, &sc, err, sizeof err), 0);
    ASSERT_EQ_INT(sc.start_count, 1);
    ASSERT_EQ_INT(sc.start[0].cell_x, 11); ASSERT_EQ_INT(sc.start[0].n, 5); ASSERT_EQ_INT(sc.start[0].fz, -24); ASSERT_EQ_INT(sc.start[0].degrees, 37);
    ASSERT_EQ_INT(sc.region_count, 1);
    ASSERT_EQ_INT(sc.region[0].box[3], 4); ASSERT_EQ_INT(sc.region[0].count_word, 1);
    ASSERT_EQ_INT(sc.region[0].first, 0); ASSERT_EQ_INT(sc.region[0].count, 1);
    ASSERT_EQ_INT(sc.region_entry[0].w[0], 40); ASSERT_EQ_INT(sc.region_entry[0].w[4], 0x8001);
    ASSERT_EQ_INT(sc.row_count, 1); ASSERT_EQ_INT(sc.row[0].count[2], 4); ASSERT_EQ_INT(sc.row[0].list[1], 0);
    ASSERT_EQ_INT(sc.list_count, 1); ASSERT_EQ_INT(sc.list[0].sub[2], 1);
    ASSERT_EQ_INT(sc.sublist_count, 2); ASSERT_EQ_INT(sc.sublist[1].first, 1); ASSERT_EQ_INT(sc.sublist[1].count, 0);
    ASSERT_EQ_INT(sc.sub_entry_count, 1); ASSERT_EQ_INT(sc.sub_entry[0].w[2], 0x8003);
}
TEST(scenario_refusals_name_the_file) {
    fileset_t f; scenario_t sc; char err[96] = "";
    game_loader_t ld = { rdf, rel, &f };
    scenario_file(&f);
    f.n -= 1;                                   /* truncated */
    ASSERT_EQ_INT(assets_load_scenario(&ld, &sc, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/scenario.bin") != NULL);
    scenario_file(&f);
    put16(&f, 0);                               /* trailing bytes */
    ASSERT_EQ_INT(assets_load_scenario(&ld, &sc, err, sizeof err), -1);
    scenario_file(&f);
    f.d[2] = 12;                                /* wrong record size of the start blocks */
    ASSERT_EQ_INT(assets_load_scenario(&ld, &sc, err, sizeof err), -1);
    scenario_file(&f);
    f.d[4 + 16 + 4 + 2 * 5] = 9;                /* region first entry beyond the entries */
    ASSERT_EQ_INT(assets_load_scenario(&ld, &sc, err, sizeof err), -1);
    scenario_file(&f);
    f.name = "other";                           /* missing */
    err[0] = 0;
    ASSERT_EQ_INT(assets_load_scenario(&ld, &sc, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/scenario.bin") != NULL);
}
TEST(palettes_need_exactly_96_bytes) {
    fileset_t f; uint8_t pal[96]; char err[96] = "";
    game_loader_t ld = { rdf, rel, &f };
    f.n = 96; for (int i = 0; i < 96; i++) f.d[i] = (uint8_t)(i * 3);
    f.name = "palette/menu.pal";
    ASSERT_EQ_INT(assets_load_menu_palette(&ld, pal, err, sizeof err), 0);
    ASSERT_EQ_INT(pal[95], 95 * 3 & 255);
    ASSERT_EQ_INT(assets_load_flash_palette(&ld, pal, err, sizeof err), -1);   /* the other file is absent */
    ASSERT_TRUE(strstr(err, "palette/flash.pal") != NULL);
    f.name = "palette/flash.pal";
    ASSERT_EQ_INT(assets_load_flash_palette(&ld, pal, err, sizeof err), 0);
    f.n = 95;
    ASSERT_EQ_INT(assets_load_flash_palette(&ld, pal, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "palette/flash.pal") != NULL);
    f.n = 97;
    ASSERT_EQ_INT(assets_load_flash_palette(&ld, pal, err, sizeof err), -1);
}

/* ---- text records (invented bytes; only structure is tested) ---- */
typedef struct { uint8_t d[2][512]; size_t n[2]; const char *name[2]; } pair_t;
static int rdp(void *user, const char *path, uint8_t **out, size_t *len) {
    pair_t *p = (pair_t *)user;
    for (int i = 0; i < 2; i++)
        if (p->name[i] && strcmp(path, p->name[i]) == 0) {
            *out = (uint8_t *)malloc(p->n[i] ? p->n[i] : 1);
            memcpy(*out, p->d[i], p->n[i]);
            *len = p->n[i];
            return 0;
        }
    return -1;
}
static void pp(pair_t *p, int f, unsigned v) { p->d[f][p->n[f]++] = (uint8_t)(v & 255); }
static void pp16(pair_t *p, int f, unsigned v) { pp(p, f, v); pp(p, f, v >> 8); }
/* three lines: record 1 has two (one empty text), record 2 has one; four ids */
static void text_files(pair_t *p) {
    memset(p, 0, sizeof *p);
    p->name[0] = "tables/text.bin"; p->name[1] = "tables/text_ids.bin";
    pp16(p, 0, 3);
    pp16(p, 0, 1); pp(p, 0, 0); pp(p, 0, 10); pp(p, 0, 4); pp(p, 0, 0); pp(p, 0, 0x23); pp(p, 0, 0); pp16(p, 0, 3);
    pp(p, 0, 0x41); pp(p, 0, 0x20); pp(p, 0, 0x5F);
    pp16(p, 0, 1); pp(p, 0, 1); pp(p, 0, 10); pp(p, 0, 4); pp(p, 0, 3); pp(p, 0, 0x56); pp(p, 0, 20); pp16(p, 0, 0);
    pp16(p, 0, 3); pp(p, 0, 0); pp(p, 0, 199); pp(p, 0, 34); pp(p, 0, 1); pp(p, 0, 0x95); pp(p, 0, 120); pp16(p, 0, 7);
    for (int i = 0; i < 7; i++) pp(p, 0, 0x20);
    pp16(p, 1, 4);
    unsigned ids[4][3] = { {1, 0, 2}, {2, 0, 2}, {3, 2, 1}, {4, 0, 0} };
    for (int i = 0; i < 4; i++) { pp16(p, 1, ids[i][0]); pp16(p, 1, ids[i][1]); pp16(p, 1, ids[i][2]); }
}

static const uint8_t *big_data; static size_t big_len;
static int rdbig(void *user, const char *path, uint8_t **out, size_t *len) {
    if (strcmp(path, "tables/text.bin") == 0) {
        *out = (uint8_t *)malloc(big_len); memcpy(*out, big_data, big_len); *len = big_len; return 0;
    }
    return rdp(user, path, out, len);
}

TEST(text_reads_lines_and_ids) {
    pair_t p; text_t *t = (text_t *)malloc(sizeof *t); char err[96] = "";
    text_files(&p);
    game_loader_t ld = { rdp, rel, &p };
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), 0);
    ASSERT_EQ_INT(t->line_count, 3);
    ASSERT_EQ_INT(t->line[0].first_id, 1); ASSERT_EQ_INT(t->line[0].row, 10); ASSERT_EQ_INT(t->line[0].col, 4);
    ASSERT_EQ_INT(t->line[0].colour, 0x23); ASSERT_EQ_INT(t->line[0].length, 3);
    ASSERT_EQ_INT(t->text[t->line[0].text], 0x41); ASSERT_EQ_INT(t->text[t->line[0].text + 3], 0);
    ASSERT_EQ_INT(t->line[1].flags, 3); ASSERT_EQ_INT(t->line[1].delay, 20); ASSERT_EQ_INT(t->line[1].length, 0);
    ASSERT_EQ_INT(t->line[2].col, 34); ASSERT_EQ_INT(t->line[2].delay, 120); ASSERT_EQ_INT(t->line[2].length, 7);
    ASSERT_EQ_INT(t->id_count, 4);
    ASSERT_EQ_INT(t->id[2].first_line, 2); ASSERT_EQ_INT(t->id[2].line_count, 1); ASSERT_EQ_INT(t->id[3].line_count, 0);
    ASSERT_EQ_INT(t->id[1].id, 2);
    free(t);
}

TEST(text_refusals_name_the_file) {
    pair_t p; text_t *t = (text_t *)malloc(sizeof *t); char err[96] = "";
    game_loader_t ld = { rdp, rel, &p };
    text_files(&p); p.name[0] = NULL;                       /* text.bin missing */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/text.bin") != NULL);
    text_files(&p); p.name[1] = NULL;                       /* ids missing */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/text_ids.bin") != NULL);
    text_files(&p); p.n[0] -= 1;                            /* truncated text */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/text.bin") != NULL);
    text_files(&p); p.n[0] += 1; p.d[0][p.n[0] - 1] = 0;    /* trailing byte */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    text_files(&p); p.d[0][2 + 3] = 200;                    /* row 200 */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    text_files(&p); p.d[0][2 + 4] = 5;                      /* odd column */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    text_files(&p); p.d[0][2 + 5] = 2;                      /* flags 2 */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    text_files(&p); p.d[0][2 + 10] = 0x7F;                  /* text byte */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    text_files(&p); p.d[0][2 + 7] = 121;                    /* delay */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    text_files(&p); p.d[1][2 + 4] = 9;                      /* line range past the lines */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/text_ids.bin") != NULL);
    text_files(&p); p.d[1][2] = 5;                          /* ids not 1, 2, ... */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    text_files(&p); p.n[1] -= 2;                            /* size does not match the count */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/text_ids.bin") != NULL);
    text_files(&p); p.d[0][2 + 5] = 1; p.d[0][2 + 6] = 0x21;                 /* typed line with delay 1 */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    ASSERT_EQ_INT(t->line_count, 0);                        /* a failed load leaves nothing half-filled */
    text_files(&p); p.d[0][2 + 4] = 10; p.d[0][2 + 8] = 45; /* 45 chars from column 10: too wide */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    text_files(&p); p.d[0][2 + 8] = 46;                     /* length 46 */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    text_files(&p); p.d[1][0] = 0; p.d[1][1] = 0;           /* id_count 0 */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/text_ids.bin") != NULL);
    text_files(&p); p.d[0][0] = 0xFF; p.d[0][1] = 0x7F;     /* count above the cap */
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    free(t);
}

TEST(text_character_cap_is_refused) {
    /* 200 lines of 45 characters: 200 * 46 = 9200 bytes, above TEXT_MAX_CHARS */
    static pair_t p;
    text_t *t = (text_t *)malloc(sizeof *t); char err[96] = "";
    static uint8_t big[2 + 200 * 55];
    size_t n = 0;
    big[n++] = 200; big[n++] = 0;
    for (int i = 0; i < 200; i++) {
        uint8_t h[10] = { 1, 0, 0, 10, 0, 0, 0x23, 0, 45, 0 };
        h[2] = (uint8_t)(i & 255);
        memcpy(big + n, h, 10); n += 10;
        memset(big + n, 0x41, 45); n += 45;
    }
    memset(&p, 0, sizeof p);
    text_files(&p);
    p.name[0] = "tables/text.bin";
    game_loader_t ld = { rdbig, rel, &p };
    big_data = big; big_len = n;
    ASSERT_EQ_INT(assets_load_text(&ld, t, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/text.bin") != NULL);
    free(t);
}

/* ---- demo and training (invented bytes) ---- */
static uint8_t *dbuf; static size_t dlen; static const char *dname;
static int rdd(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    if (!dname || strcmp(path, dname) != 0) return -1;
    *out = (uint8_t *)malloc(dlen ? dlen : 1); memcpy(*out, dbuf, dlen); *len = dlen; return 0;
}
static void demo_file(unsigned n) {
    free(dbuf); dbuf = (uint8_t *)calloc(4 + (size_t)n * 5 + 8, 1); dname = "tables/demo.bin"; dlen = 4 + (size_t)n * 5;
    dbuf[0] = (uint8_t)n; dbuf[1] = (uint8_t)(n >> 8);
    for (unsigned i = 0; i < n; i++) { dbuf[4 + i] = (uint8_t)(i * 3); dbuf[4 + n + 4 * i] = (uint8_t)i; dbuf[4 + n + 4 * i + 1] = 0x12; dbuf[4 + n + 4 * i + 2] = 0x34; dbuf[4 + n + 4 * i + 3] = 0x80; }
}
TEST(demo_reads_keys_and_event_words) {
    demo_t *d = (demo_t *)malloc(sizeof *d); char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    demo_file(3000);
    ASSERT_EQ_INT(assets_load_demo(&ld, d, err, sizeof err), 0);
    ASSERT_EQ_INT(d->frame_count, 3000); ASSERT_EQ_INT(d->key[10], 30);
    ASSERT_EQ_INT(d->ev[5].word1, 0x1205); ASSERT_EQ_INT(d->ev[5].word2, 0x8034);
    free(d);
}
TEST(demo_refusals_name_the_file_and_zero_the_count) {
    demo_t *d = (demo_t *)malloc(sizeof *d); char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    demo_file(3000); dlen -= 1;                                  /* truncated */
    d->frame_count = 7;
    ASSERT_EQ_INT(assets_load_demo(&ld, d, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/demo.bin") != NULL); ASSERT_EQ_INT(d->frame_count, 0);
    demo_file(3000); dlen += 1;                                  /* trailing byte */
    ASSERT_EQ_INT(assets_load_demo(&ld, d, err, sizeof err), -1);
    demo_file(3000); dbuf[0] = 0xFF; dbuf[1] = 0x7F;             /* count above the cap */
    ASSERT_EQ_INT(assets_load_demo(&ld, d, err, sizeof err), -1);
    demo_file(100);                                              /* fewer frames than the played run */
    ASSERT_EQ_INT(assets_load_demo(&ld, d, err, sizeof err), -1);
    dlen = 3;
    ASSERT_EQ_INT(assets_load_demo(&ld, d, err, sizeof err), -1);
    dname = NULL;
    ASSERT_EQ_INT(assets_load_demo(&ld, d, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/demo.bin") != NULL);
    free(d);
}

static void training_file(void) {
    free(dbuf); dbuf = (uint8_t *)calloc(512, 1); dname = "tables/training.bin"; dlen = 0;
    #define P16(v) do { dbuf[dlen++] = (uint8_t)((v) & 255); dbuf[dlen++] = (uint8_t)(((v) >> 8) & 255); } while (0)
    P16(8); P16(8);
    for (int i = 0; i < 8; i++) { P16(40 + i); P16(0xFFF0 + i); P16(0); P16(0); }
    P16(8); P16(8);
    for (int i = 0; i < 8; i++) { P16(0x103); P16(0); P16(0x0B + i); P16(0x8000); }
    P16(11); P16(2);
    for (int i = 0; i < 11; i++) P16(i - 5);
    P16(11); P16(2);
    for (int i = 0; i < 11; i++) P16(100 + i);
    #undef P16
}
TEST(training_reads_tables) {
    training_t t; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    training_file();
    ASSERT_EQ_INT(assets_load_training(&ld, &t, err, sizeof err), 0);
    ASSERT_EQ_INT(t.a[3].prompt_id, 43); ASSERT_EQ_INT(t.a[3].name_id, -13);
    ASSERT_EQ_INT(t.b[2].camera, 0x103); ASSERT_TRUE(t.b[2].script == 0x8000000Du);
    ASSERT_EQ_INT(t.pitch_rate[0], -5); ASSERT_EQ_INT(t.roll_rate[10], 110);
}
TEST(training_refusals_name_the_file_and_zero_the_struct) {
    training_t t; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    training_file(); dlen -= 2;
    ASSERT_EQ_INT(assets_load_training(&ld, &t, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/training.bin") != NULL);
    training_file(); dlen += 1;
    ASSERT_EQ_INT(assets_load_training(&ld, &t, err, sizeof err), -1);
    training_file(); dbuf[4 + 64 + 4 + 7] = 0;         /* script bit 31 clear in table B row 0 */
    t.a[0].prompt_id = 9;
    ASSERT_EQ_INT(assets_load_training(&ld, &t, err, sizeof err), -1);
    ASSERT_EQ_INT(t.a[0].prompt_id, 0);
    training_file(); dbuf[0] = 7;                                /* table A count 7 */
    ASSERT_EQ_INT(assets_load_training(&ld, &t, err, sizeof err), -1);
    training_file(); dbuf[2] = 6;                                /* record size 6 */
    ASSERT_EQ_INT(assets_load_training(&ld, &t, err, sizeof err), -1);
    dname = NULL;
    ASSERT_EQ_INT(assets_load_training(&ld, &t, err, sizeof err), -1);
}

/* ---- templates (invented bytes) ---- */
static void templates_file(void) {
    free(dbuf); dbuf = (uint8_t *)calloc(4 + 2680 + 8, 1); dname = "tables/templates.bin"; dlen = 4 + 2680;
    dbuf[0] = 134; dbuf[2] = 20;
    for (int i = 0; i < 134 * 5; i++) { uint8_t *p = dbuf + 4 + 4 * i; p[0] = (uint8_t)i; p[1] = 0xA5; p[2] = (uint8_t)(i >> 3); p[3] = (uint8_t)(i * 7); }
}
TEST(templates_read_big_endian_longs) {
    templates_t t; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    templates_file();
    ASSERT_EQ_INT(assets_load_templates(&ld, &t, err, sizeof err), 0);
    ASSERT_EQ_INT(t.count, 134);
    ASSERT_TRUE(t.rec[0][0] == 0x00A50000u);
    ASSERT_TRUE(t.rec[2][3] == ((13u << 24) | (0xA5u << 16) | (1u << 8) | 91u));
    ASSERT_TRUE(t.rec[133][4] == (((uint32_t)(669 & 255) << 24) | (0xA5u << 16) | ((uint32_t)(669 >> 3) << 8) | (uint8_t)(669 * 7)));
}
TEST(templates_refusals_name_the_file_and_zero_the_struct) {
    templates_t t; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    templates_file(); dlen -= 1;
    memset(&t, 0xEE, sizeof t);
    ASSERT_EQ_INT(assets_load_templates(&ld, &t, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/templates.bin") != NULL);
    ASSERT_EQ_INT(t.count, 0); ASSERT_TRUE(t.rec[5][1] == 0);
    templates_file(); dbuf[0] = 133; dlen -= 20;
    ASSERT_EQ_INT(assets_load_templates(&ld, &t, err, sizeof err), -1);
    templates_file(); dbuf[0] = 135;
    ASSERT_EQ_INT(assets_load_templates(&ld, &t, err, sizeof err), -1);
    templates_file(); dbuf[2] = 24;
    ASSERT_EQ_INT(assets_load_templates(&ld, &t, err, sizeof err), -1);
    templates_file(); dlen = 3;
    ASSERT_EQ_INT(assets_load_templates(&ld, &t, err, sizeof err), -1);
    dname = "tables/other.bin";
    ASSERT_EQ_INT(assets_load_templates(&ld, &t, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/templates.bin") != NULL);
}

/* ---- burst, target text and months (extractor version 16; invented bytes) ---- */
static void burst_file(void) {
    free(dbuf); dbuf = (uint8_t *)calloc(BURST_FILE_BYTES + 8, 1); dname = "tables/burst.bin"; dlen = BURST_FILE_BYTES;
    memcpy(dbuf, "BRST", 4); dbuf[4] = 2;
    for (int i = 0; i < 72; i++) { int w = (i - 36) * 100 + 7; dbuf[6 + 2 * i] = (uint8_t)w; dbuf[7 + 2 * i] = (uint8_t)(w >> 8); }
    for (int i = 0; i < 135; i++) dbuf[150 + i] = (uint8_t)(i - 60);
    for (int i = 0; i < 60; i++) dbuf[285 + i] = (uint8_t)(i % 16);                                 /* the pen tables */
    for (int i = 0; i < 32; i++) { dbuf[345 + 2 * i] = (uint8_t)(4 * ((i * 5) % 8)); dbuf[346 + 2 * i] = 0; }   /* the shape cycle */
}
TEST(burst_reads_the_shapes_the_offsets_and_the_discs) {
    burst_tables_t b; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    burst_file();
    ASSERT_EQ_INT(assets_load_burst(&ld, &b, err, sizeof err), 0);
    ASSERT_EQ_INT(b.shape[0][0][0], -3593); ASSERT_EQ_INT(b.shape[0][0][1], -3493); ASSERT_EQ_INT(b.shape[7][2][2], 3507);   /* the words are little endian and signed */
    ASSERT_EQ_INT(b.off[0][0][0], -60); ASSERT_EQ_INT(b.off[0][11][2], -60 + 35); ASSERT_EQ_INT(b.off[1][0][0], -60 + 36); ASSERT_EQ_INT(b.off[2][11][2], -60 + 107);
    ASSERT_EQ_INT(b.disc[0][0], -60 + 108); ASSERT_EQ_INT(b.disc[8][2], -60 + 134);
    ASSERT_EQ_INT(b.pen[0][0], 0); ASSERT_EQ_INT(b.pen[2][5], (24 + 5) % 16); ASSERT_EQ_INT(b.pen[4][11], 59 % 16);   /* the pen tables, twelve bytes each */
    ASSERT_EQ_INT(b.cycle[0], 0); ASSERT_EQ_INT(b.cycle[3], 4 * ((3 * 5) % 8)); ASSERT_EQ_INT(b.cycle[31], 4 * ((31 * 5) % 8));   /* the cycle words, little endian */
}
TEST(burst_refusals_name_the_file_and_zero_the_struct) {
    burst_tables_t b; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    burst_file(); dlen -= 1; memset(&b, 0xEE, sizeof b);
    ASSERT_EQ_INT(assets_load_burst(&ld, &b, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/burst.bin") != NULL && strstr(err, "size") != NULL); ASSERT_EQ_INT(b.shape[0][0][0], 0);
    burst_file(); dlen += 1;
    ASSERT_EQ_INT(assets_load_burst(&ld, &b, err, sizeof err), -1);
    burst_file(); dbuf[0] = 'X';
    ASSERT_EQ_INT(assets_load_burst(&ld, &b, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "magic") != NULL);
    burst_file(); dbuf[4] = 1;
    ASSERT_EQ_INT(assets_load_burst(&ld, &b, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "version") != NULL);
    burst_file(); dname = "tables/other.bin";
    ASSERT_EQ_INT(assets_load_burst(&ld, &b, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "tables/burst.bin") != NULL);
}
static void target_text_file(void) {
    free(dbuf); dbuf = (uint8_t *)calloc(TARGET_TEXT_FILE_BYTES + 8, 1); dname = "tables/target_text.bin"; dlen = TARGET_TEXT_FILE_BYTES;
    dbuf[0] = TARGET_LABELS; dbuf[2] = TARGET_LABEL_CHARS; dbuf[4] = TARGET_NAMES; dbuf[6] = TARGET_NAME_CHARS;
    for (int i = 0; i < TARGET_LABELS * TARGET_LABEL_CHARS; i++) dbuf[8 + i] = (uint8_t)('a' + i);              /* invented characters */
    for (int i = 0; i < TARGET_NAMES * TARGET_NAME_CHARS; i++) dbuf[8 + TARGET_LABELS * TARGET_LABEL_CHARS + i] = (uint8_t)('A' + i);
}
TEST(target_text_reads_the_labels_and_the_names) {
    target_text_t t; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    target_text_file();
    ASSERT_EQ_INT(assets_load_target_text(&ld, &t, err, sizeof err), 0);
    ASSERT_TRUE(memcmp(t.label[0], "abcd", 4) == 0 && memcmp(t.label[2], "ijkl", 4) == 0);
    ASSERT_TRUE(memcmp(t.name[0], "ABCDEF", 6) == 0 && memcmp(t.name[7], "klmnop", 6) == 0);
}
TEST(target_text_refusals_name_the_file_and_zero_the_struct) {
    target_text_t t; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    target_text_file(); dlen -= 1; memset(&t, 0xEE, sizeof t);
    ASSERT_EQ_INT(assets_load_target_text(&ld, &t, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/target_text.bin") != NULL); ASSERT_EQ_INT(t.label[0][0], 0);
    target_text_file(); dbuf[0] = 4;
    ASSERT_EQ_INT(assets_load_target_text(&ld, &t, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "layout") != NULL);
    target_text_file(); dbuf[6] = 7;
    ASSERT_EQ_INT(assets_load_target_text(&ld, &t, err, sizeof err), -1);
    target_text_file(); dname = "tables/other.bin";
    ASSERT_EQ_INT(assets_load_target_text(&ld, &t, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "tables/target_text.bin") != NULL);
}
static void prompts_file(void) {
    free(dbuf); dbuf = (uint8_t *)calloc(PROMPTS_FILE_BYTES + 8, 1); dname = "tables/prompts.bin"; dlen = PROMPTS_FILE_BYTES;
    dbuf[0] = PROMPT_COUNT; dbuf[2] = PROMPT_CHARS;
    memcpy(dbuf + 4, "qr stu vwxyz!", 12);   /* invented characters */
}
TEST(prompts_read_the_two_strings) {
    prompts_t p; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    prompts_file();
    ASSERT_EQ_INT(assets_load_prompts(&ld, &p, err, sizeof err), 0);
    ASSERT_TRUE(memcmp(p.text[0], "qr stu", 6) == 0 && memcmp(p.text[1], " vwxyz", 6) == 0);
}
TEST(prompts_refusals_name_the_file_and_zero_the_struct) {
    prompts_t p; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    prompts_file(); dlen -= 1; memset(&p, 0xEE, sizeof p);
    ASSERT_EQ_INT(assets_load_prompts(&ld, &p, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/prompts.bin") != NULL); ASSERT_EQ_INT(p.text[0][0], 0);
    prompts_file(); dbuf[0] = 3;
    ASSERT_EQ_INT(assets_load_prompts(&ld, &p, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "layout") != NULL);
    prompts_file(); dbuf[2] = 5;
    ASSERT_EQ_INT(assets_load_prompts(&ld, &p, err, sizeof err), -1);
    prompts_file(); dname = "tables/other.bin";
    ASSERT_EQ_INT(assets_load_prompts(&ld, &p, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "tables/prompts.bin") != NULL);
}
static void star_file(void) {
    free(dbuf); dbuf = (uint8_t *)calloc(STAR_FILE_BYTES + 8, 1); dname = "tables/star.bin"; dlen = STAR_FILE_BYTES;
    dbuf[0] = STAR_SEGMENTS;
    for (int i = 0; i < 18; i++) { const int v = (i % 7) - 3 + i; dbuf[2 + 2 * i] = (uint8_t)(v & 0xFF); dbuf[3 + 2 * i] = (uint8_t)((v >> 8) & 0xFF); }   /* invented offsets */
}
TEST(star_reads_three_segments_of_two_points) {
    star_t st; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    star_file();
    ASSERT_EQ_INT(assets_load_star(&ld, &st, err, sizeof err), 0);
    for (int i = 0; i < 18; i++) ASSERT_EQ_INT(st.pt[i / 6][(i / 3) % 2][i % 3], (i % 7) - 3 + i);
}
TEST(star_refusals_name_the_file_and_zero_the_struct) {
    star_t st; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    star_file(); dlen -= 1; memset(&st, 0xEE, sizeof st);
    ASSERT_EQ_INT(assets_load_star(&ld, &st, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/star.bin") != NULL); ASSERT_EQ_INT(st.pt[0][0][0], 0);
    star_file(); dbuf[0] = 4;
    ASSERT_EQ_INT(assets_load_star(&ld, &st, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "segment count") != NULL);
    star_file(); dname = "tables/other.bin";
    ASSERT_EQ_INT(assets_load_star(&ld, &st, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "tables/star.bin") != NULL);
}
TEST(briefing_chain_and_head_step_read_their_bytes_and_refuse_other_sizes) {
    uint8_t c[CHAIN_FILE_BYTES]; int8_t hs[HEAD_STEP_FILE_BYTES]; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    free(dbuf); dbuf = (uint8_t *)calloc(CHAIN_FILE_BYTES + 8, 1); dname = "tables/briefing_chain.bin"; dlen = CHAIN_FILE_BYTES;
    for (int i = 0; i < CHAIN_FILE_BYTES; i++) dbuf[i] = (uint8_t)(3 + 2 * i);   /* invented ids */
    ASSERT_EQ_INT(assets_load_briefing_chain(&ld, c, err, sizeof err), 0);
    for (int i = 0; i < CHAIN_FILE_BYTES; i++) ASSERT_EQ_INT(c[i], 3 + 2 * i);
    dlen = CHAIN_FILE_BYTES - 1;
    ASSERT_EQ_INT(assets_load_briefing_chain(&ld, c, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/briefing_chain.bin") != NULL); ASSERT_EQ_INT(c[0], 0);
    free(dbuf); dbuf = (uint8_t *)calloc(HEAD_STEP_FILE_BYTES + 8, 1); dname = "tables/head_step.bin"; dlen = HEAD_STEP_FILE_BYTES;
    for (int i = 0; i < HEAD_STEP_FILE_BYTES; i++) dbuf[i] = (uint8_t)(i * 9 - 40);   /* invented, some negative */
    ASSERT_EQ_INT(assets_load_head_step(&ld, hs, err, sizeof err), 0);
    for (int i = 0; i < HEAD_STEP_FILE_BYTES; i++) ASSERT_EQ_INT(hs[i], i * 9 - 40);
    dlen = HEAD_STEP_FILE_BYTES + 1;
    ASSERT_EQ_INT(assets_load_head_step(&ld, hs, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/head_step.bin") != NULL); ASSERT_EQ_INT(hs[3], 0);
}
TEST(sounds_read_the_37_records_and_refuse_a_bad_table) {
    sound_table_t st; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    free(dbuf); dbuf = (uint8_t *)calloc(SOUND_TABLE_BYTES + 8, 1); dname = "tables/sounds.bin"; dlen = SOUND_TABLE_BYTES;
    for (int i = 0; i < SOUND_OBJECTS; i++) {   /* invented records: every third absent, the rest samples, a tone and a noise */
        uint8_t *p = dbuf + SOUND_RECORD_BYTES * i;
        if (i % 3 == 2) { p[0] = SOUND_NONE; p[1] = SOUND_NONE; p[16] = SOUND_NONE; continue; }
        p[0] = (uint8_t)(i % 5); p[1] = SOUND_KIND_SAMPLE; p[2] = (uint8_t)(i % 4); p[3] = (uint8_t)(10 + i);
        p[4] = 0; p[5] = 1; p[6] = (uint8_t)i; p[7] = (uint8_t)(i * 3);                        /* offset 0x0001ii.. */
        p[8] = 0; p[9] = 2; p[10] = (uint8_t)(i + 7); p[11] = (uint8_t)(i * 5);                /* length */
        p[12] = 1; p[13] = (uint8_t)(100 + i); p[14] = (uint8_t)(i - 1); p[15] = (uint8_t)i;   /* period, count (signed), reload */
        p[16] = (uint8_t)((i + 4) % SOUND_OBJECTS); p[17] = (uint8_t)(i % 5);
        if (i % 7 == 0) { p[0] = SOUND_NONE; p[1] = (uint8_t)(1 + i % 2); }                    /* a synthesised object */
    }
    ASSERT_EQ_INT(assets_load_sounds(&ld, &st, err, sizeof err), 0);
    for (int i = 0; i < SOUND_OBJECTS; i++) {
        const uint8_t *p = dbuf + SOUND_RECORD_BYTES * i;
        if (i % 3 == 2) { ASSERT_EQ_INT(st.obj[i].kind, SOUND_NONE); ASSERT_EQ_INT(st.obj[i].length, 0); continue; }
        ASSERT_EQ_INT(st.obj[i].file, p[0]); ASSERT_EQ_INT(st.obj[i].kind, p[1]); ASSERT_EQ_INT(st.obj[i].voice, p[2]); ASSERT_EQ_INT(st.obj[i].volume, p[3]);
        ASSERT_EQ_INT((long)st.obj[i].offset, 0x10000L + 256L * i + i * 3); ASSERT_EQ_INT((long)st.obj[i].length, 0x20000L + 256L * (i + 7) + i * 5);
        ASSERT_EQ_INT(st.obj[i].period, 256 + 100 + i); ASSERT_EQ_INT(st.obj[i].count, (int8_t)(i - 1)); ASSERT_EQ_INT(st.obj[i].reload, i);
        ASSERT_EQ_INT(st.obj[i].next, (i + 4) % SOUND_OBJECTS); ASSERT_EQ_INT(st.obj[i].script, i % 5);
    }
    ASSERT_EQ_INT(st.obj[0].count, -1);
    dlen = SOUND_TABLE_BYTES - 1; memset(&st, 0xEE, sizeof st);                       /* a short table */
    ASSERT_EQ_INT(assets_load_sounds(&ld, &st, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/sounds.bin") != NULL); ASSERT_EQ_INT(st.obj[4].kind, SOUND_NONE); ASSERT_EQ_INT(st.obj[4].length, 0);
    dlen = SOUND_TABLE_BYTES + 1;                                                     /* a long one */
    ASSERT_EQ_INT(assets_load_sounds(&ld, &st, err, sizeof err), -1);
    dlen = SOUND_TABLE_BYTES; dname = "tables/other.bin";                             /* a missing one */
    ASSERT_EQ_INT(assets_load_sounds(&ld, &st, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "tables/sounds.bin") != NULL);
    dname = "tables/sounds.bin";
    dbuf[1] = 9;  ASSERT_EQ_INT(assets_load_sounds(&ld, &st, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "kind") != NULL); ASSERT_EQ_INT(st.obj[3].kind, SOUND_NONE);
    dbuf[1] = 0;  dbuf[2] = 4;  ASSERT_EQ_INT(assets_load_sounds(&ld, &st, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "voice") != NULL);
    dbuf[2] = 0;  dbuf[16] = 37; ASSERT_EQ_INT(assets_load_sounds(&ld, &st, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "links") != NULL);
    dbuf[16] = 4; dbuf[17] = 5;  ASSERT_EQ_INT(assets_load_sounds(&ld, &st, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "script") != NULL);
    dbuf[17] = 0; dbuf[0] = SOUND_NONE; ASSERT_EQ_INT(assets_load_sounds(&ld, &st, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "without a file") != NULL);
    ASSERT_EQ_INT(assets_load_sounds(&ld, NULL, err, sizeof err), -1);
}
TEST(months_reads_36_characters_and_nothing_else) {
    char m[MONTHS_BYTES], err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    free(dbuf); dbuf = (uint8_t *)calloc(MONTHS_BYTES + 8, 1); dname = "tables/months.bin"; dlen = MONTHS_BYTES;
    for (int i = 0; i < MONTHS_BYTES; i++) dbuf[i] = (uint8_t)('0' + i);
    ASSERT_EQ_INT(assets_load_months(&ld, m, err, sizeof err), 0);
    ASSERT_TRUE(memcmp(m, "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRS", 36) == 0);
    dlen = 35;
    ASSERT_EQ_INT(assets_load_months(&ld, m, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/months.bin") != NULL); ASSERT_EQ_INT(m[0], 0);
    dlen = 37;
    ASSERT_EQ_INT(assets_load_months(&ld, m, err, sizeof err), -1);
    dlen = MONTHS_BYTES; dname = "tables/other.bin";
    ASSERT_EQ_INT(assets_load_months(&ld, m, err, sizeof err), -1);
}

/* ---- ai (invented bytes) ---- */
static const struct { unsigned c, s; } ai_layout[14] = { {11,2},{11,2},{11,2},{11,2},{11,2},{19,16},{14,6},{10,6},{5,16},{3,12},{16,4},{1,2},{60,332},{21,6} };
static void ai_file(void) {
    size_t n = 0; for (int k = 0; k < 14; k++) n += 4 + ai_layout[k].c * ai_layout[k].s;
    free(dbuf); dbuf = (uint8_t *)calloc(n + 8, 1); dname = "tables/ai.bin"; dlen = n;
    size_t o = 0; int v = 1;
    for (int k = 0; k < 14; k++) {
        dbuf[o] = (uint8_t)ai_layout[k].c; dbuf[o + 1] = (uint8_t)(ai_layout[k].c >> 8); dbuf[o + 2] = (uint8_t)ai_layout[k].s; dbuf[o + 3] = (uint8_t)(ai_layout[k].s >> 8); o += 4;
        for (unsigned i = 0; i < ai_layout[k].c * ai_layout[k].s / 2; i++, v++) {
            int w = (k & 1) ? -v : v;
            if (k == 12 && i % 166 == 0) w = (int)(i / 166 % 34);   /* the run length of the record */
            if (k == 13) {   /* invented hull lists: face f of list l = (6 * ((l + f) % 17) | form bit, 6 * f, 6 * l + 12); triple 6 the terminator */
                int l = (int)i / 21, f = (int)i % 21 / 3, c = (int)i % 3;
                w = f == 6 ? (c == 0 ? -1 : 0) : c == 0 ? (6 * ((l + f) % 17) | (f & 1 ? 0x4000 : 0)) : c == 1 ? 6 * f : 6 * l + 12;
            }
            dbuf[o++] = (uint8_t)w; dbuf[o++] = (uint8_t)(w >> 8);
        }
    }
}
TEST(ai_reads_every_subtable_little_endian) {
    ai_t a; char err[96] = "";
    game_loader_t ld = { rdd, rel, NULL };
    ai_file();
    ASSERT_EQ_INT(assets_load_ai(&ld, &a, err, sizeof err), 0);
    ASSERT_EQ_INT(a.rate[0][0], 1); ASSERT_EQ_INT(a.rate[1][10], -22); ASSERT_EQ_INT(a.rate[4][10], 55);
    ASSERT_EQ_INT(a.evasion[0][0][0], -56); ASSERT_EQ_INT(a.evasion[18][3][1], -207);
    ASSERT_EQ_INT(a.offs[0][0], 208); ASSERT_EQ_INT(a.offs[13][2], 249); ASSERT_EQ_INT(a.pre_word, -370);
    ASSERT_EQ_INT(a.fine[15][1], 369);
    ASSERT_EQ_INT(a.run_len[0], 0); ASSERT_EQ_INT(a.run_len[5], 5); ASSERT_EQ_INT(a.run_len[40], 6); ASSERT_EQ_INT(a.run_len[59], 25);   /* the run lengths of the invented records */
    ASSERT_EQ_INT(a.hull[0][0][0], 0); ASSERT_EQ_INT(a.hull[2][1][0], 6 * 3 | 0x4000); ASSERT_EQ_INT(a.hull[2][5][1], 30); ASSERT_EQ_INT(a.hull[1][3][2], 18);
    ASSERT_EQ_INT(a.hull[1][6][0], -1); ASSERT_EQ_INT(a.hull[1][6][1], 0);
    ASSERT_EQ_INT(a.run[2][0][0], 371 + 2 * 166 + 1); ASSERT_EQ_INT(a.run[2][32][4], 371 + 2 * 166 + 1 + 5 * 32 + 4);   /* the block words keep their order */
}
TEST(ai_refusals_name_the_file_and_table_and_zero_the_struct) {
    ai_t a; char err[128] = "";
    game_loader_t ld = { rdd, rel, NULL };
    ai_file(); dlen -= 1; memset(&a, 0xEE, sizeof a);
    ASSERT_EQ_INT(assets_load_ai(&ld, &a, err, sizeof err), -1);
    ASSERT_TRUE(strstr(err, "tables/ai.bin") != NULL && strstr(err, "hull lists") != NULL);
    ASSERT_EQ_INT(a.rate[0][0], 0); ASSERT_EQ_INT(a.fine[3][1], 0);
    ai_file(); dbuf[0] = 10;
    ASSERT_EQ_INT(assets_load_ai(&ld, &a, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "rate table 1") != NULL);
    ai_file(); dbuf[2] = 4;
    ASSERT_EQ_INT(assets_load_ai(&ld, &a, err, sizeof err), -1);
    ai_file(); dlen += 1;
    ASSERT_EQ_INT(assets_load_ai(&ld, &a, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "extra") != NULL);
    ai_file(); dlen = 3;
    ASSERT_EQ_INT(assets_load_ai(&ld, &a, err, sizeof err), -1);
    ai_file(); dbuf[5 * 26 + 4 + 19 * 16] = 13;          /* offset table count 13 != 14 */
    ASSERT_EQ_INT(assets_load_ai(&ld, &a, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "offset table") != NULL);
    ai_file(); dname = "tables/other.bin";
    ASSERT_EQ_INT(assets_load_ai(&ld, &a, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "tables/ai.bin") != NULL);
    {   /* the hull lists: truncation, wrong count, a slot beyond 16, a missing terminator; the struct is zeroed */
        size_t hull_at;
        ai_file(); hull_at = dlen - (4 + 21 * 6);
        dlen -= 6; memset(&a, 0xEE, sizeof a);
        ASSERT_EQ_INT(assets_load_ai(&ld, &a, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "hull lists") != NULL && strstr(err, "truncated") != NULL);
        ASSERT_EQ_INT(a.hull[0][0][1], 0); ASSERT_EQ_INT(a.rate[1][1], 0);
        ai_file(); dlen = hull_at;
        ASSERT_EQ_INT(assets_load_ai(&ld, &a, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "hull lists") != NULL && strstr(err, "missing") != NULL);
        ai_file(); dbuf[hull_at] = 20;
        ASSERT_EQ_INT(assets_load_ai(&ld, &a, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "hull lists") != NULL && strstr(err, "record count") != NULL);
        ai_file(); dbuf[hull_at + 2] = 4;
        ASSERT_EQ_INT(assets_load_ai(&ld, &a, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "record size") != NULL);
        ai_file(); dbuf[hull_at + 4 + 2] = 102;             /* list 0 face 0 word b = 102 = slot 17 */
        ASSERT_EQ_INT(assets_load_ai(&ld, &a, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "vertex slots") != NULL);
        ai_file(); dbuf[hull_at + 4 + 6 * 6 + 1] = 0;       /* list 0 terminator word 0 = 0x0000 (bit 15 clear) */
        ASSERT_EQ_INT(assets_load_ai(&ld, &a, err, sizeof err), -1); ASSERT_TRUE(strstr(err, "terminator") != NULL);
    }
}

int main(void) {
    RUN(lf_lines); RUN(crlf_lines); RUN(no_trailing_newline); RUN(blank_lines_skipped);
    RUN(name_of_31_chars_ok_32_rejected); RUN(more_than_max_rejected);
    RUN(scenario_reads_every_field_little_endian); RUN(scenario_refusals_name_the_file); RUN(palettes_need_exactly_96_bytes);
    RUN(demo_reads_keys_and_event_words); RUN(demo_refusals_name_the_file_and_zero_the_count); RUN(training_reads_tables); RUN(training_refusals_name_the_file_and_zero_the_struct); RUN(templates_read_big_endian_longs); RUN(templates_refusals_name_the_file_and_zero_the_struct); RUN(ai_reads_every_subtable_little_endian); RUN(ai_refusals_name_the_file_and_table_and_zero_the_struct); RUN(burst_reads_the_shapes_the_offsets_and_the_discs); RUN(burst_refusals_name_the_file_and_zero_the_struct); RUN(target_text_reads_the_labels_and_the_names); RUN(target_text_refusals_name_the_file_and_zero_the_struct); RUN(prompts_read_the_two_strings); RUN(prompts_refusals_name_the_file_and_zero_the_struct); RUN(star_reads_three_segments_of_two_points); RUN(star_refusals_name_the_file_and_zero_the_struct); RUN(briefing_chain_and_head_step_read_their_bytes_and_refuse_other_sizes); RUN(sounds_read_the_37_records_and_refuse_a_bad_table); RUN(months_reads_36_characters_and_nothing_else); RUN(text_reads_lines_and_ids); RUN(text_refusals_name_the_file); RUN(text_character_cap_is_refused); RUN(empty_file_rejected); RUN(missing_file_rejected); RUN(append_helpers_bound_and_format);
    TEST_MAIN_END();
}
