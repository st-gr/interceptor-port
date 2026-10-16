/* src/core/assets_flow.c - readers of the game-flow assets: menu and flash palettes, the scenario tables. */
#include <stdint.h>
#include <string.h>
#include "assets.h"

static void fail(char *err, size_t errlen, const char *file, const char *what) {
    if (!err || errlen == 0) return;
    err[0] = '\0';
    core_assets_append(err, errlen, file);
    core_assets_append(err, errlen, ": ");
    core_assets_append(err, errlen, what);
}

static int load_pal(const game_loader_t *loader, const char *path, uint8_t *out, char *err, size_t errlen) {
    if (core_assets_read_exact(loader, path, out, 3 * ASSETS_PALETTE_ENTRIES) != 0) {
        fail(err, errlen, path, "missing or not 96 bytes (32 entries of r, g, b)");
        return -1;
    }
    return 0;
}

int assets_load_menu_palette(const game_loader_t *loader, uint8_t out[3 * ASSETS_PALETTE_ENTRIES], char *err, size_t errlen) {
    return load_pal(loader, "palette/menu.pal", out, err, errlen);
}

int assets_load_flash_palette(const game_loader_t *loader, uint8_t out[3 * ASSETS_PALETTE_ENTRIES], char *err, size_t errlen) {
    return load_pal(loader, "palette/flash.pal", out, err, errlen);
}

#define SCENARIO_FILE "tables/scenario.bin"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* Reads the sub-table header at *at: u16 count, u16 record_size. Checks the record size and the count cap and that the body fits. */
static int section(const uint8_t *buf, size_t len, size_t *at, unsigned rec, unsigned cap, unsigned *count, const uint8_t **body) {
    if (*at + 4 > len) return -1;
    unsigned c = rd16(buf + *at), r = rd16(buf + *at + 2);
    if (c > cap || (c > 0 && r != rec) || (c == 0 && r != 0 && r != rec)) return -1;
    if (*at + 4 + (size_t)c * r > len) return -1;
    *count = c;
    *body = buf + *at + 4;
    *at += 4 + (size_t)c * r;
    return 0;
}

static void words(const uint8_t *p, uint16_t *w, int n) { for (int i = 0; i < n; i++) w[i] = rd16(p + 2 * i); }

int assets_load_scenario(const game_loader_t *loader, scenario_t *out, char *err, size_t errlen) {
    uint8_t *buf = NULL;
    size_t len = 0, at = 0;
    unsigned c;
    const uint8_t *b;
    const char *bad = NULL;
    if (!loader || !loader->read || !loader->release || !out) { fail(err, errlen, SCENARIO_FILE, "no loader"); return -1; }
    if (loader->read(loader->user, SCENARIO_FILE, &buf, &len) != 0) { fail(err, errlen, SCENARIO_FILE, "cannot be read"); return -1; }

    if (section(buf, len, &at, 16, SCENARIO_MAX_START, &c, &b) != 0 || c == 0) { bad = "start blocks: wrong size or count"; goto done; }
    out->start_count = (int)c;
    for (unsigned i = 0; i < c; i++) {
        uint16_t w[8];
        words(b + 16 * i, w, 8);
        scenario_start_t *s = &out->start[i];
        s->cell_x = (int16_t)w[0]; s->cell_z = (int16_t)w[1]; s->n = (int16_t)w[2]; s->ox = (int16_t)w[3];
        s->oz = (int16_t)w[4]; s->fx = (int16_t)w[5]; s->fz = (int16_t)w[6]; s->degrees = (int16_t)w[7];
    }
    if (section(buf, len, &at, 14, SCENARIO_MAX_REGIONS, &c, &b) != 0) { bad = "regions: wrong size or count"; goto done; }
    out->region_count = (int)c;
    for (unsigned i = 0; i < c; i++) {
        uint16_t w[7];
        words(b + 14 * i, w, 7);
        scenario_region_t *r = &out->region[i];
        for (int k = 0; k < 4; k++) r->box[k] = w[k];
        r->count_word = w[4]; r->first = w[5]; r->count = w[6];
    }
    if (section(buf, len, &at, 10, SCENARIO_MAX_REGION_ENTRIES, &c, &b) != 0) { bad = "region entries: wrong size or count"; goto done; }
    out->region_entry_count = (int)c;
    for (unsigned i = 0; i < c; i++) words(b + 10 * i, out->region_entry[i].w, 5);
    for (int i = 0; i < out->region_count; i++)
        if ((unsigned)out->region[i].first + out->region[i].count > c) { bad = "regions: entry range outside the entries"; goto done; }

    if (section(buf, len, &at, 12, SCENARIO_MAX_ROWS, &c, &b) != 0) { bad = "placement rows: wrong size or count"; goto done; }
    out->row_count = (int)c;
    unsigned rows = c;
    for (unsigned i = 0; i < c; i++) {
        uint16_t w[6];
        words(b + 12 * i, w, 6);
        for (int v = 0; v < SCENARIO_VARIANTS; v++) { out->row[i].list[v] = w[2 * v]; out->row[i].count[v] = w[2 * v + 1]; }
    }
    if (section(buf, len, &at, 8, SCENARIO_MAX_LISTS, &c, &b) != 0) { bad = "lists: wrong size or count"; goto done; }
    out->list_count = (int)c;
    for (unsigned i = 0; i < c; i++) words(b + 8 * i, out->list[i].sub, SCENARIO_LIST_SUBS);
    for (unsigned i = 0; i < rows; i++)
        for (int v = 0; v < SCENARIO_VARIANTS; v++)
            if (out->row[i].list[v] >= out->list_count) { bad = "placement rows: list index outside the lists"; goto done; }
    if (section(buf, len, &at, 4, SCENARIO_MAX_SUBLISTS, &c, &b) != 0) { bad = "sub-lists: wrong size or count"; goto done; }
    out->sublist_count = (int)c;
    for (unsigned i = 0; i < c; i++) { out->sublist[i].first = rd16(b + 4 * i); out->sublist[i].count = rd16(b + 4 * i + 2); }
    for (int i = 0; i < out->list_count; i++)
        for (int k = 0; k < SCENARIO_LIST_SUBS; k++)
            if (out->list[i].sub[k] >= out->sublist_count) { bad = "lists: sub-list index outside the sub-lists"; goto done; }
    if (section(buf, len, &at, 10, SCENARIO_MAX_SUB_ENTRIES, &c, &b) != 0) { bad = "sub-list entries: wrong size or count"; goto done; }
    out->sub_entry_count = (int)c;
    for (unsigned i = 0; i < c; i++) words(b + 10 * i, out->sub_entry[i].w, 5);
    for (int i = 0; i < out->sublist_count; i++)
        if ((unsigned)out->sublist[i].first + out->sublist[i].count > c) { bad = "sub-lists: entry range outside the entries"; goto done; }
    if (section(buf, len, &at, 0, 0, &c, &b) != 0) { bad = "mode 0x7D data: wrong size"; goto done; }
    if (at != len) bad = "trailing bytes after the last sub-table";
done:
    loader->release(loader->user, buf);
    if (bad) { fail(err, errlen, SCENARIO_FILE, bad); return -1; }
    return 0;
}

#define TEXT_FILE "tables/text.bin"
#define TEXT_IDS_FILE "tables/text_ids.bin"

static int read_file(const game_loader_t *loader, const char *path, uint8_t **buf, size_t *len, char *err, size_t errlen) {
    if (loader->read(loader->user, path, buf, len) != 0) { fail(err, errlen, path, "missing or cannot be read (re-run the extractor)"); return -1; }
    return 0;
}

int assets_load_text(const game_loader_t *loader, text_t *out, char *err, size_t errlen) {
    uint8_t *tb = NULL, *ib = NULL;
    size_t tl = 0, il = 0, at;
    const char *file = TEXT_FILE, *bad = NULL;
    if (!loader || !loader->read || !loader->release || !out) { fail(err, errlen, TEXT_FILE, "no loader"); return -1; }
    if (read_file(loader, TEXT_FILE, &tb, &tl, err, errlen) != 0) return -1;
    if (read_file(loader, TEXT_IDS_FILE, &ib, &il, err, errlen) != 0) { loader->release(loader->user, tb); return -1; }

    if (tl < 2) { bad = "truncated"; goto done; }
    unsigned n = rd16(tb);
    if (n > TEXT_MAX_LINES) { bad = "more lines than the core holds"; goto done; }
    out->line_count = (int)n;
    out->text_len = 0;
    at = 2;
    for (unsigned i = 0; i < n; i++) {
        if (at + 10 > tl) { bad = "truncated in a line header"; goto done; }
        text_line_t *l = &out->line[i];
        l->first_id = rd16(tb + at); l->line = tb[at + 2]; l->row = tb[at + 3]; l->col = tb[at + 4];
        l->flags = tb[at + 5]; l->colour = tb[at + 6]; l->delay = tb[at + 7]; l->length = rd16(tb + at + 8);
        at += 10;
        if (l->row >= 200 || (l->col & 1) || l->flags > 3 || l->flags == 2 || l->delay > 120 || ((l->flags & 1) && (l->colour & 15) < 2)) { bad = "a line header is outside the ranges of the format"; goto done; }
        if (l->length > TEXT_MAX_LINE_LEN || at + l->length > tl) { bad = "a line is too long or truncated"; goto done; }
        if (out->text_len + l->length + 1 > TEXT_MAX_CHARS) { bad = "more text than the core holds"; goto done; }
        if (l->length && 8u * l->col + 7u * (l->length - 1u) + 5u > 320u) { bad = "a line is wider than 320 pixels"; goto done; }
        l->text = (uint16_t)out->text_len;
        for (unsigned k = 0; k < l->length; k++) {
            uint8_t c = tb[at + k];
            if (c < 0x20 || c > 0x5F) { bad = "a text byte is outside 0x20-0x5F"; goto done; }
            out->text[out->text_len++] = c;
        }
        out->text[out->text_len++] = 0;
        at += l->length;
    }
    if (at != tl) { bad = "trailing bytes after the last line"; goto done; }

    file = TEXT_IDS_FILE;
    if (il < 2) { bad = "truncated"; goto done; }
    unsigned m = rd16(ib);
    if (m == 0 || m > TEXT_MAX_IDS) { bad = "no ids or more than the core holds"; goto done; }
    if (il != 2 + 6 * (size_t)m) { bad = "wrong size for its id count"; goto done; }
    out->id_count = (int)m;
    for (unsigned i = 0; i < m; i++) {
        text_id_t *d = &out->id[i];
        d->id = rd16(ib + 2 + 6 * i); d->first_line = rd16(ib + 4 + 6 * i); d->line_count = rd16(ib + 6 + 6 * i);
        if (d->id != i + 1) { bad = "ids are not 1, 2, 3 ..."; goto done; }
        if ((unsigned)d->first_line + d->line_count > n) { bad = "a line range is outside text.bin"; goto done; }
    }
done:
    loader->release(loader->user, tb);
    loader->release(loader->user, ib);
    if (bad) { out->line_count = out->id_count = out->text_len = 0; fail(err, errlen, file, bad); return -1; }
    return 0;
}

#define DEMO_FILE "tables/demo.bin"
#define TRAINING_FILE "tables/training.bin"

int assets_load_demo(const game_loader_t *loader, demo_t *out, char *err, size_t errlen) {
    uint8_t *b = NULL;
    size_t len = 0;
    const char *bad = NULL;
    if (!loader || !loader->read || !loader->release || !out) { fail(err, errlen, DEMO_FILE, "no loader"); return -1; }
    if (read_file(loader, DEMO_FILE, &b, &len, err, errlen) != 0) return -1;
    out->frame_count = 0;
    if (len < 4) { bad = "truncated"; goto done; }
    uint32_t n = (uint32_t)rd16(b) | ((uint32_t)rd16(b + 2) << 16);
    if (n < DEMO_PLAYED_FRAMES || n > DEMO_MAX_FRAMES) { bad = "frame count outside the range the core holds"; goto done; }
    if (len != 4 + (size_t)n * 5) { bad = "wrong size for its frame count"; goto done; }
    for (uint32_t i = 0; i < n; i++) {
        out->key[i] = b[4 + i];
        out->ev[i].word1 = rd16(b + 4 + n + 4 * i);
        out->ev[i].word2 = rd16(b + 4 + n + 4 * i + 2);
    }
    out->frame_count = (int)n;
done:
    loader->release(loader->user, b);
    if (bad) { fail(err, errlen, DEMO_FILE, bad); return -1; }
    return 0;
}

int assets_load_training(const game_loader_t *loader, training_t *out, char *err, size_t errlen) {
    uint8_t *b = NULL;
    size_t len = 0, at = 0;
    unsigned c;
    const uint8_t *body;
    const char *bad = NULL;
    if (!loader || !loader->read || !loader->release || !out) { fail(err, errlen, TRAINING_FILE, "no loader"); return -1; }
    if (read_file(loader, TRAINING_FILE, &b, &len, err, errlen) != 0) return -1;
    memset(out, 0, sizeof *out);
    if (section(b, len, &at, 8, TRAINING_ROWS, &c, &body) != 0 || c != TRAINING_ROWS) { bad = "table A: wrong size or count"; goto done; }
    for (unsigned i = 0; i < c; i++) {
        out->a[i].prompt_id = (int16_t)rd16(body + 8 * i); out->a[i].name_id = (int16_t)rd16(body + 8 * i + 2);
        out->a[i].unused = (int32_t)((uint32_t)rd16(body + 8 * i + 4) | ((uint32_t)rd16(body + 8 * i + 6) << 16));
    }
    if (section(b, len, &at, 8, TRAINING_ROWS, &c, &body) != 0 || c != TRAINING_ROWS) { bad = "table B: wrong size or count"; goto done; }
    for (unsigned i = 0; i < c; i++) {
        out->b[i].camera = (int32_t)((uint32_t)rd16(body + 8 * i) | ((uint32_t)rd16(body + 8 * i + 2) << 16));
        out->b[i].script = (uint32_t)rd16(body + 8 * i + 4) | ((uint32_t)rd16(body + 8 * i + 6) << 16);
        if (!(out->b[i].script & 0x80000000u)) { bad = "table B: a script long has bit 31 clear"; goto done; }
    }
    if (section(b, len, &at, 2, TRAINING_RATES, &c, &body) != 0 || c != TRAINING_RATES) { bad = "pitch rates: wrong size or count"; goto done; }
    for (unsigned i = 0; i < c; i++) out->pitch_rate[i] = (int16_t)rd16(body + 2 * i);
    if (section(b, len, &at, 2, TRAINING_RATES, &c, &body) != 0 || c != TRAINING_RATES) { bad = "roll rates: wrong size or count"; goto done; }
    for (unsigned i = 0; i < c; i++) out->roll_rate[i] = (int16_t)rd16(body + 2 * i);
    if (at != len) bad = "trailing bytes after the last sub-table";
done:
    loader->release(loader->user, b);
    if (bad) { memset(out, 0, sizeof *out); fail(err, errlen, TRAINING_FILE, bad); return -1; }
    return 0;
}
