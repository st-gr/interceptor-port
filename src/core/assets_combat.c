/* src/core/assets_combat.c - readers of the combat assets: the template table. */
#include <stdint.h>
#include <string.h>
#include "assets.h"

#define TEMPLATES_FILE "tables/templates.bin"

static void fail(char *err, size_t errlen, const char *what) {
    if (!err || errlen == 0) return;
    err[0] = '\0';
    core_assets_append(err, errlen, TEMPLATES_FILE);
    core_assets_append(err, errlen, ": ");
    core_assets_append(err, errlen, what);
}

int assets_load_templates(const game_loader_t *loader, templates_t *out, char *err, size_t errlen) {
    uint8_t *buf = NULL;
    size_t len = 0;
    if (out) memset(out, 0, sizeof *out);
    if (!loader || !loader->read || !loader->release || !out) { fail(err, errlen, "no loader"); return -1; }
    if (loader->read(loader->user, TEMPLATES_FILE, &buf, &len) != 0) { fail(err, errlen, "cannot be read"); return -1; }
    const char *bad = NULL;
    if (len < 4) bad = "shorter than its header";
    else if ((unsigned)(buf[0] | (buf[1] << 8)) != TEMPLATE_COUNT) bad = "record count is not 134";
    else if ((unsigned)(buf[2] | (buf[3] << 8)) != 4 * TEMPLATE_LONGS) bad = "record size is not 20";
    else if (len != 4 + (size_t)TEMPLATE_COUNT * 4 * TEMPLATE_LONGS) bad = "size does not match 134 records of 20 bytes";
    if (bad) { fail(err, errlen, bad); loader->release(loader->user, buf); return -1; }
    for (int i = 0; i < TEMPLATE_COUNT; i++)
        for (int k = 0; k < TEMPLATE_LONGS; k++) {
            const uint8_t *p = buf + 4 + 20 * i + 4 * k;
            out->rec[i][k] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
        }
    out->count = TEMPLATE_COUNT;
    loader->release(loader->user, buf);
    return 0;
}

/* ---- tables/ai.bin ---- */
#define AI_FILE "tables/ai.bin"

static void fail_ai(char *err, size_t errlen, const char *table, const char *what) {
    if (!err || errlen == 0) return;
    err[0] = '\0';
    core_assets_append(err, errlen, AI_FILE);
    core_assets_append(err, errlen, ": ");
    core_assets_append(err, errlen, table);
    core_assets_append(err, errlen, " ");
    core_assets_append(err, errlen, what);
}

static int16_t rd16(const uint8_t *p) { return (int16_t)(uint16_t)(p[0] | (p[1] << 8)); }

/* One sub-table at *pos: header checked against [min_count, max_count] and words_per_record; words copied into dst (count * wpr). */
static const char *ai_sub(const uint8_t *buf, size_t len, size_t *pos, unsigned min_count, unsigned max_count, unsigned wpr,
                          int16_t *dst, int *count_out) {
    if (*pos + 4 > len) return "is missing (file too short)";
    unsigned count = (unsigned)(buf[*pos] | (buf[*pos + 1] << 8)), rs = (unsigned)(buf[*pos + 2] | (buf[*pos + 3] << 8));
    if (count < min_count || count > max_count) return "has the wrong record count";
    if (rs != 2 * wpr) return "has the wrong record size";
    *pos += 4;
    if (*pos + (size_t)count * rs > len) return "is truncated";
    for (unsigned i = 0; i < count * wpr; i++) dst[i] = rd16(buf + *pos + 2 * i);
    *pos += (size_t)count * rs;
    if (count_out) *count_out = (int)count;
    return NULL;
}

int assets_load_ai(const game_loader_t *loader, ai_t *out, char *err, size_t errlen) {
    static const char *const rate_names[AI_RATES] = { "rate table 1", "rate table 2", "rate table 3", "rate table 4", "rate table 5" };
    uint8_t *buf = NULL;
    size_t len = 0, pos = 0;
    const char *bad = NULL, *which = NULL;
    if (out) memset(out, 0, sizeof *out);
    if (!loader || !loader->read || !loader->release || !out) { fail_ai(err, errlen, "reader", "has no loader"); return -1; }
    if (loader->read(loader->user, AI_FILE, &buf, &len) != 0) { fail_ai(err, errlen, "file", "cannot be read"); return -1; }
    for (int i = 0; i < AI_RATES && !bad; i++)
        if ((bad = ai_sub(buf, len, &pos, AI_RATE_ENTRIES, AI_RATE_ENTRIES, 1, out->rate[i], NULL)) != NULL) which = rate_names[i];
    if (!bad && (bad = ai_sub(buf, len, &pos, AI_EVASION_GROUPS, AI_EVASION_GROUPS, 8, &out->evasion[0][0][0], NULL)) != NULL) which = "evasion table";
    if (!bad && (bad = ai_sub(buf, len, &pos, AI_OFFSETS, AI_OFFSETS, 3, &out->offs[0][0], NULL)) != NULL) which = "offset table";
    if (!bad && (bad = ai_sub(buf, len, &pos, AI_RING_DIRS, AI_RING_DIRS, 3, &out->ring3[0][0], NULL)) != NULL) which = "ring offsets";
    if (!bad && (bad = ai_sub(buf, len, &pos, AI_STARTS, AI_STARTS, 8, &out->start[0][0], NULL)) != NULL) which = "start blocks";
    if (!bad && (bad = ai_sub(buf, len, &pos, AI_CAM_POINTS, AI_CAM_POINTS, 6, &out->cam_point[0][0], NULL)) != NULL) which = "airfield camera points";
    if (!bad && (bad = ai_sub(buf, len, &pos, AI_FINE_OFFSETS, AI_FINE_OFFSETS, 2, &out->fine[0][0], NULL)) != NULL) which = "fine offsets";
    if (!bad && (bad = ai_sub(buf, len, &pos, 1, 1, 1, &out->pre_word, NULL)) != NULL) which = "word before the rate tables";
    if (!bad) {   /* the waypoint runs: 60 records of 1 + 5 * 33 words (the header is checked like the other sub-tables'; the words are read in place, no 20 KB local) */
        const unsigned wpr = 1 + 5 * AI_RUN_MAX;
        which = "waypoint runs";
        if (pos + 4 > len) bad = "is missing (file too short)";
        else if ((unsigned)(buf[pos] | (buf[pos + 1] << 8)) != AI_RUNS) bad = "has the wrong record count";
        else if ((unsigned)(buf[pos + 2] | (buf[pos + 3] << 8)) != 2 * wpr) bad = "has the wrong record size";
        else if (pos + 4 + (size_t)AI_RUNS * 2 * wpr > len) bad = "is truncated";
        else {
            pos += 4;
            for (int k = 0; k < AI_RUNS && !bad; k++) {
                const uint8_t *r = buf + pos + (size_t)k * 2 * wpr;
                const int n = rd16(r);
                if (n < 0 || n > AI_RUN_MAX) { bad = "has a run longer than 33 blocks"; break; }
                out->run_len[k] = (int16_t)n;
                for (int q = 0; q < AI_RUN_MAX * 5; q++) (&out->run[k][0][0])[q] = rd16(r + 2 + 2 * q);
            }
            pos += (size_t)AI_RUNS * 2 * wpr;
        }
    }
    if (!bad && (bad = ai_sub(buf, len, &pos, AI_HULL_LISTS * AI_HULL_TRIPLES, AI_HULL_LISTS * AI_HULL_TRIPLES, 3, &out->hull[0][0][0], NULL)) != NULL) which = "hull lists";
    for (int l = 0; l < AI_HULL_LISTS && !bad; l++) {   /* faces: offsets of the slots 0 to 16 (bit 14 of a is the form); the terminator has bit 15 set */
        for (int f = 0; f < AI_HULL_TRIPLES - 1 && !bad; f++) {
            const int16_t *t = out->hull[l][f];
            const int a = t[0] & 0x3FFF;
            if (t[0] < 0 || t[1] < 0 || t[2] < 0 || a % 6 || t[1] % 6 || t[2] % 6 || a > 96 || t[1] > 96 || t[2] > 96) { bad = "has a face outside the vertex slots 0 to 16"; which = "hull lists"; }
        }
        if (!bad && out->hull[l][AI_HULL_TRIPLES - 1][0] >= 0) { bad = "has a list without a terminator"; which = "hull lists"; }
    }
    if (!bad && pos != len) { bad = "is followed by extra bytes"; which = "file"; }
    loader->release(loader->user, buf);
    if (bad) { memset(out, 0, sizeof *out); fail_ai(err, errlen, which, bad); return -1; }
    return 0;
}

/* ---- tables/burst.bin, tables/target_text.bin, tables/months.bin (extractor version 16), tables/prompts.bin (version 19) ---- */

static void fail_file(char *err, size_t errlen, const char *file, const char *what) {
    if (!err || errlen == 0) return;
    err[0] = '\0';
    core_assets_append(err, errlen, file);
    core_assets_append(err, errlen, ": ");
    core_assets_append(err, errlen, what);
}

/* Reads `file` through the loader; on success *buf is the loader's buffer (release it), else the error is written and -1 returned. */
static int read_checked(const game_loader_t *loader, const char *file, size_t want, uint8_t **buf, char *err, size_t errlen) {
    size_t len = 0;
    *buf = NULL;
    if (!loader || !loader->read || !loader->release) { fail_file(err, errlen, file, "no loader"); return -1; }
    if (loader->read(loader->user, file, buf, &len) != 0) { fail_file(err, errlen, file, "cannot be read"); return -1; }
    if (len != want) { fail_file(err, errlen, file, "has the wrong size"); loader->release(loader->user, *buf); return -1; }
    return 0;
}

int assets_load_burst(const game_loader_t *loader, burst_tables_t *out, char *err, size_t errlen) {
    uint8_t *buf;
    if (out) memset(out, 0, sizeof *out);
    if (!out) { fail_file(err, errlen, "tables/burst.bin", "no output"); return -1; }
    if (read_checked(loader, "tables/burst.bin", BURST_FILE_BYTES, &buf, err, errlen) != 0) return -1;
    if (memcmp(buf, "BRST", 4) != 0) { fail_file(err, errlen, "tables/burst.bin", "has the wrong magic"); loader->release(loader->user, buf); return -1; }
    if (buf[4] != 2 || buf[5] != 0) { fail_file(err, errlen, "tables/burst.bin", "has an unknown version"); loader->release(loader->user, buf); return -1; }
    const uint8_t *p = buf + 6;
    for (int i = 0; i < BURST_SHAPES; i++) for (int k = 0; k < 3; k++) for (int a = 0; a < 3; a++, p += 2) out->shape[i][k][a] = rd16(p);
    for (int g = 0; g < 3; g++) for (int j = 0; j < BURST_FRAGMENTS; j++) for (int a = 0; a < 3; a++) out->off[g][j][a] = (int8_t)*p++;
    for (int j = 0; j < BURST_DISC_COUNT; j++) for (int a = 0; a < 3; a++) out->disc[j][a] = (int8_t)*p++;
    for (int t = 0; t < BURST_PEN_TABLES; t++) for (int j = 0; j < BURST_FRAGMENTS; j++) out->pen[t][j] = *p++;
    for (int i = 0; i < BURST_CYCLE_WORDS; i++, p += 2) out->cycle[i] = (uint16_t)(p[0] | (p[1] << 8));
    loader->release(loader->user, buf);
    return 0;
}

int assets_load_target_text(const game_loader_t *loader, target_text_t *out, char *err, size_t errlen) {
    uint8_t *buf;
    if (out) memset(out, 0, sizeof *out);
    if (!out) { fail_file(err, errlen, "tables/target_text.bin", "no output"); return -1; }
    if (read_checked(loader, "tables/target_text.bin", TARGET_TEXT_FILE_BYTES, &buf, err, errlen) != 0) return -1;
    if (rd16(buf) != TARGET_LABELS || rd16(buf + 2) != TARGET_LABEL_CHARS || rd16(buf + 4) != TARGET_NAMES || rd16(buf + 6) != TARGET_NAME_CHARS) {
        fail_file(err, errlen, "tables/target_text.bin", "has another layout (counts or lengths)");
        loader->release(loader->user, buf);
        return -1;
    }
    memcpy(out->label, buf + 8, sizeof out->label);
    memcpy(out->name, buf + 8 + sizeof out->label, sizeof out->name);
    loader->release(loader->user, buf);
    return 0;
}

int assets_load_prompts(const game_loader_t *loader, prompts_t *out, char *err, size_t errlen) {
    uint8_t *buf;
    if (out) memset(out, 0, sizeof *out);
    if (!out) { fail_file(err, errlen, "tables/prompts.bin", "no output"); return -1; }
    if (read_checked(loader, "tables/prompts.bin", PROMPTS_FILE_BYTES, &buf, err, errlen) != 0) return -1;
    if (rd16(buf) != PROMPT_COUNT || rd16(buf + 2) != PROMPT_CHARS) {
        fail_file(err, errlen, "tables/prompts.bin", "has another layout (count or length)");
        loader->release(loader->user, buf);
        return -1;
    }
    memcpy(out->text, buf + 4, sizeof out->text);
    loader->release(loader->user, buf);
    return 0;
}

int assets_load_star(const game_loader_t *loader, star_t *out, char *err, size_t errlen) {
    uint8_t *buf;
    if (out) memset(out, 0, sizeof *out);
    if (!out) { fail_file(err, errlen, "tables/star.bin", "no output"); return -1; }
    if (read_checked(loader, "tables/star.bin", STAR_FILE_BYTES, &buf, err, errlen) != 0) return -1;
    if (rd16(buf) != STAR_SEGMENTS) {
        fail_file(err, errlen, "tables/star.bin", "has another segment count");
        loader->release(loader->user, buf);
        return -1;
    }
    for (int k = 0; k < STAR_SEGMENTS; k++) for (int e = 0; e < 2; e++) for (int a = 0; a < 3; a++) out->pt[k][e][a] = rd16(buf + 2 + 2 * (k * 6 + e * 3 + a));
    loader->release(loader->user, buf);
    return 0;
}

int assets_load_months(const game_loader_t *loader, char out[MONTHS_BYTES], char *err, size_t errlen) {
    uint8_t *buf;
    if (out) memset(out, 0, MONTHS_BYTES);
    if (!out) { fail_file(err, errlen, "tables/months.bin", "no output"); return -1; }
    if (read_checked(loader, "tables/months.bin", MONTHS_BYTES, &buf, err, errlen) != 0) return -1;
    memcpy(out, buf, MONTHS_BYTES);
    loader->release(loader->user, buf);
    return 0;
}

int assets_load_briefing_chain(const game_loader_t *loader, uint8_t out[CHAIN_FILE_BYTES], char *err, size_t errlen) {
    uint8_t *buf;
    if (out) memset(out, 0, CHAIN_FILE_BYTES);
    if (!out) { fail_file(err, errlen, "tables/briefing_chain.bin", "no output"); return -1; }
    if (read_checked(loader, "tables/briefing_chain.bin", CHAIN_FILE_BYTES, &buf, err, errlen) != 0) return -1;
    memcpy(out, buf, CHAIN_FILE_BYTES);
    loader->release(loader->user, buf);
    return 0;
}

int assets_load_head_step(const game_loader_t *loader, int8_t out[HEAD_STEP_FILE_BYTES], char *err, size_t errlen) {
    uint8_t *buf;
    if (out) memset(out, 0, HEAD_STEP_FILE_BYTES);
    if (!out) { fail_file(err, errlen, "tables/head_step.bin", "no output"); return -1; }
    if (read_checked(loader, "tables/head_step.bin", HEAD_STEP_FILE_BYTES, &buf, err, errlen) != 0) return -1;
    for (int i = 0; i < HEAD_STEP_FILE_BYTES; i++) out[i] = (int8_t)buf[i];
    loader->release(loader->user, buf);
    return 0;
}

int assets_load_sounds(const game_loader_t *loader, sound_table_t *out, char *err, size_t errlen) {
    uint8_t *buf;
    if (out) {
        memset(out, 0, sizeof *out);
        for (int i = 0; i < SOUND_OBJECTS; i++) out->obj[i].kind = out->obj[i].file = out->obj[i].next = SOUND_NONE;
    }
    if (!out) { fail_file(err, errlen, "tables/sounds.bin", "no output"); return -1; }
    if (read_checked(loader, "tables/sounds.bin", SOUND_TABLE_BYTES, &buf, err, errlen) != 0) return -1;
    for (int i = 0; i < SOUND_OBJECTS; i++) {
        const uint8_t *p = buf + SOUND_RECORD_BYTES * i;
        const char *bad = NULL;
        if (p[1] > SOUND_KIND_NOISE && p[1] != SOUND_NONE) bad = "has a record of an unknown kind";
        else if (p[1] != SOUND_NONE && p[2] > 3) bad = "has a record with a voice above 3";
        else if (p[1] != SOUND_NONE && p[3] > 63) bad = "has a record with a volume above 63";
        else if (p[1] != SOUND_NONE && p[16] != SOUND_NONE && p[16] >= SOUND_OBJECTS) bad = "has a record that links to an object that does not exist";
        else if (p[1] != SOUND_NONE && p[17] > SOUND_SCRIPT_LAUNCH) bad = "has a record with an unknown script";
        else if (p[1] == SOUND_KIND_SAMPLE && p[0] == SOUND_NONE) bad = "has a sample record without a file";
        if (bad) {
            fail_file(err, errlen, "tables/sounds.bin", bad);
            loader->release(loader->user, buf);
            for (int k = 0; k < SOUND_OBJECTS; k++) { memset(&out->obj[k], 0, sizeof out->obj[k]); out->obj[k].kind = out->obj[k].file = out->obj[k].next = SOUND_NONE; }
            return -1;
        }
        if (p[1] == SOUND_NONE) continue;   /* absent: left as the zeroed record with kind, file and next SOUND_NONE */
        out->obj[i].file = p[0]; out->obj[i].kind = p[1]; out->obj[i].voice = p[2]; out->obj[i].volume = p[3];
        out->obj[i].offset = ((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 8) | p[7];
        out->obj[i].length = ((uint32_t)p[8] << 24) | ((uint32_t)p[9] << 16) | ((uint32_t)p[10] << 8) | p[11];
        out->obj[i].period = (uint16_t)((p[12] << 8) | p[13]);
        out->obj[i].count = (int8_t)p[14]; out->obj[i].reload = p[15]; out->obj[i].next = p[16]; out->obj[i].script = p[17];
    }
    loader->release(loader->user, buf);
    return 0;
}
