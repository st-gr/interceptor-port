/* src/core/flight_tables.c - loads the flight step's tables (note 05, "Extractor needs"). */
#include <string.h>
#include "flight.h"
#include "assets.h"

static void fail(char *err, size_t errlen, const char *name) {
    if (err && errlen) {
        err[0] = '\0';
        core_assets_append(err, errlen, "flight tables: ");
        core_assets_append(err, errlen, name);
        core_assets_append(err, errlen, " is missing or has the wrong size");
    }
}

static int16_t rw(const uint8_t *raw, size_t i) { return (int16_t)(uint16_t)(raw[2 * i] | (raw[2 * i + 1] << 8)); }

#define WORDS(file, dst, n) do { if (core_assets_read_words(loader, "tables/" file, (dst), (n)) != 0) { fail(err, errlen, "tables/" file); return -1; } } while (0)
#define BYTES(file, dst, n) do { if (core_assets_read_exact(loader, "tables/" file, (dst), (n)) != 0) { fail(err, errlen, "tables/" file); return -1; } } while (0)

int flight_tables_load(flight_tables_t *t, const game_loader_t *loader, char *err, size_t errlen) {
    memset(t, 0, sizeof *t);
    {   /* assets extracted without the segment table: the junk spans carry flat values where the reference machine has RAM addresses (note 05 17.9) */
        uint8_t *buf = NULL;
        size_t len = 0;
        if (loader->read(loader->user, "tables/flight_spans_flat.txt", &buf, &len) == 0) {
            loader->release(loader->user, buf);
            if (err && errlen) {
                err[0] = '\0';
                core_assets_append(err, errlen, "flight tables: the assets were extracted without the segment table (spans_relocated is false); re-run the extractor without --allow-flat-spans");
            }
            return -1;
        }
    }
    WORDS("flight_stick_player.bin", t->stick_player, 62);
    WORDS("flight_stick_ai.bin", t->stick_ai, 64);
    WORDS("flight_gust.bin", t->gust, 31);
    WORDS("flight_sink.bin", t->sink, 31);
    BYTES("flight_span_pitch.bin", t->span_a, FLIGHT_SPAN_A_SIZE);
    BYTES("flight_span_trig.bin", t->span_b, FLIGHT_SPAN_B_SIZE);
    WORDS("flight_len.bin", t->len, 257);
    WORDS("flight_terrain.bin", t->terrain, FLIGHT_TERRAIN_WORDS);
    WORDS("flight_dir.bin", t->dir, 32);
    {   /* flight_pads.bin: 5 entries of 8 words, then for each of the four pads a count and its quad list */
        uint8_t raw[512];
        uint8_t *buf = NULL;
        size_t len = 0;
        if (loader->read(loader->user, "tables/flight_pads.bin", &buf, &len) != 0 || len < 80 || len > sizeof raw) {
            if (buf) loader->release(loader->user, buf);
            fail(err, errlen, "tables/flight_pads.bin");
            return -1;
        }
        memcpy(raw, buf, len);
        loader->release(loader->user, buf);
        size_t nw = len / 2, pos = 0;
        for (int e = 0; e < 5; e++) for (int k = 0; k < 8; k++) t->pad_entry[e][k] = rw(raw, pos++);
        for (int e = 0; e < 4; e++) {
            if (pos >= nw) { fail(err, errlen, "tables/flight_pads.bin"); return -1; }
            int n = rw(raw, pos++);
            if (n < 0 || n > FLIGHT_PAD_QUAD_WORDS || pos + (size_t)n > nw) { fail(err, errlen, "tables/flight_pads.bin"); return -1; }
            for (int k = 0; k < n; k++) t->pad_quads[e][k] = rw(raw, pos++);
            t->pad_nquad_words[e] = (uint8_t)n;
        }
    }
    {
        uint8_t c[852], s[438];
        BYTES("flight_carrier.bin", c, 852);
        BYTES("flight_ship2.bin", s, 438);
        memcpy(t->ship_l1[0], c, 124);       memcpy(t->ship_l2[0], c + 124, 44);  memcpy(t->ship_st[0], c + 168, 684);
        memcpy(t->ship_l1[1], s, 32);        memcpy(t->ship_l2[1], s + 32, 44);   memcpy(t->ship_st[1], s + 76, 362);
    }
    return 0;
}
