/* tests/lists_state.h - the display-list builder's inputs from a logged state of the original (docs/re/04a-display-lists.md, "Verified by"),
 * and the comparison of the port's queues with queues in the original's byte layout (the emulator's peek or the rig's memory).
 * Shared by tests/world_dump.c (port against the reference emulator's queues) and tests/rig/test_lists_diff.c (port against the original
 * builder 0x1F770 in the Musashi rig).
 *
 * The log is extracted/captures/logs/lists.txt (written by the reference emulator from tools/vamiga-ref/scripts/openers-lists.txt): lines
 * "label F:addr hex bytes", labels s71_ (hunk 71 from 0x4A700, 2336 bytes), recs_ (0x4B23C, 8192), sec_ (0x4D23C, 512), act_ (0x53A2A), q_ (0x53A4A,
 * 5040), each with the suffix deck or air. Nothing here is game data: the log is the user's own and git-ignored. */
#ifndef INTERCEPTOR_LISTS_STATE_H
#define INTERCEPTOR_LISTS_STATE_H
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/lists.h"

#define LS_S71_BASE 0x4A700u
#define LS_RECS 0x4B23Cu
#define LS_SEC 0x4D23Cu
#define LS_QUEUES 0x53A4Au
#define LS_QSIZE 5040
#define LS_QSTRIDE 1680

typedef struct { char label[64]; uint32_t addr; uint8_t *data; size_t n; } ls_blob_t;
typedef struct { ls_blob_t b[64]; int n; } ls_log_t;

static inline int ls_load_log(ls_log_t *log, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    static char line[1 << 17];   /* the longest line (recs_, 8192 bytes) is 16 KB of hex */
    log->n = 0;
    while (fgets(line, sizeof line, f) && log->n < 64) {
        char lab[64], at[32]; int off = 0;
        if (sscanf(line, "%63s %31s%n", lab, at, &off) < 2 || at[0] != 'F' || at[1] != ':') continue;
        ls_blob_t *b = &log->b[log->n];
        snprintf(b->label, sizeof b->label, "%s", lab);
        b->addr = (uint32_t)strtoul(at + 2, NULL, 16);
        b->data = (uint8_t *)malloc(strlen(line) / 2 + 1); b->n = 0;
        int hi = -1;
        for (const char *p = line + off; *p; p++) {
            if (!isxdigit((unsigned char)*p)) continue;
            int v = isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10);
            if (hi < 0) hi = v; else { b->data[b->n++] = (uint8_t)(hi * 16 + v); hi = -1; }
        }
        log->n++;
    }
    fclose(f);
    return 0;
}
static inline const ls_blob_t *ls_find(const ls_log_t *log, const char *prefix, const char *scene) {
    char l[80]; snprintf(l, sizeof l, "%s%s", prefix, scene);
    for (int i = 0; i < log->n; i++) if (!strcmp(log->b[i].label, l)) return &log->b[i];
    return NULL;
}
static inline void ls_free_log(ls_log_t *log) { for (int i = 0; i < log->n; i++) free(log->b[i].data); log->n = 0; }

static inline unsigned ls_u16(const uint8_t *p) { return ((unsigned)p[0] << 8) | p[1]; }
static inline uint32_t ls_u32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
#define LS_V8(s71, a) ((s71)[(a) - LS_S71_BASE])
#define LS_V16(s71, a) ls_u16((s71) + (a) - LS_S71_BASE)
#define LS_V32(s71, a) ls_u32((s71) + (a) - LS_S71_BASE)

/* One entity record (0x200 bytes) or secondary object (0x20 bytes) into the port's form. */
static inline void ls_entity(lists_entity_t *e, const uint8_t *r) {
    memset(e, 0, sizeof *e);
    e->active = (r[1] & 0x40) != 0;
    e->b4 = r[4];
    e->cell_x = (uint8_t)ls_u16(r + 6); e->cell_z = (uint8_t)ls_u16(r + 8); e->sub = r[0x0A];
    e->fine_x = (int16_t)ls_u16(r + 0x0C); e->fine_z = (int16_t)ls_u16(r + 0x0E);
    e->alt8 = (int32_t)ls_u32(r + 0x10);
    e->pos[0] = (int32_t)ls_u32(r + 0x14); e->pos[1] = (int32_t)ls_u32(r + 0x18); e->pos[2] = (int32_t)ls_u32(r + 0x1C);
}

/* The port's scan inputs and entity records from the logged state. The request byte is left to the caller. */
static inline void ls_load_state(lists_t *l, const uint8_t *s71, const uint8_t *recs, const uint8_t *sec) {
    lists_scan_t *s = &l->scan;
    memset(s, 0, sizeof *s);
    s->cell_x = (uint8_t)LS_V16(s71, 0x4AA04); s->cell_z = (uint8_t)LS_V16(s71, 0x4AA06);
    s->acell_x = (uint8_t)(s->cell_x >> 2); s->acell_z = (uint8_t)(s->cell_z >> 2);
    s->sub = LS_V8(s71, 0x4A909); s->quad = LS_V8(s71, 0x4A908); s->octant = LS_V8(s71, 0x4A90C); s->gate = LS_V8(s71, 0x4A83E);
    s->cam_x = (int32_t)LS_V32(s71, 0x4AB1A); s->cam_z = (int32_t)LS_V32(s71, 0x4AB22);
    s->cam_y = (int32_t)LS_V32(s71, 0x4AB1E);
    s->off_x = (int16_t)LS_V16(s71, 0x4AB2A); s->off_z = (int16_t)LS_V16(s71, 0x4AB2E);
    s->off_y = (int32_t)LS_V32(s71, 0x4AB30);
    l->period = LS_V8(s71, 0x4A974); l->n = LS_V16(s71, 0x4A992); l->built = LS_V8(s71, 0x4A85D) != 0;
    for (int i = 0; i < LISTS_ENTITIES; i++) {
        ls_entity(&l->ent[i], recs + 0x200 * i);
        ls_entity(&l->sec[i], sec + 0x20 * i);
        l->bit2[i] = (i >= 14 && l->ent[i].active) ? 1 : 0;   /* the entity update sets bit 2 of records 14 and 15 every frame */
    }
}

/* The port's entry as the original's 24 bytes (note 04a section 4): the descriptor address 0x2600C + 20 * id, the mark word 0, +0x16 zero. */
static inline void ls_entry_bytes(const lists_entry_t *e, uint8_t w[24]) {
    uint32_t d = 0x2600Cu + 20u * e->id;
    w[0] = e->id; w[1] = e->flags;
    w[2] = (uint8_t)(d >> 24); w[3] = (uint8_t)(d >> 16); w[4] = (uint8_t)(d >> 8); w[5] = (uint8_t)d;
    w[6] = (uint8_t)((uint16_t)e->x >> 8); w[7] = (uint8_t)e->x; w[8] = (uint8_t)((uint16_t)e->y >> 8); w[9] = (uint8_t)e->y;
    w[10] = (uint8_t)((uint16_t)e->z >> 8); w[11] = (uint8_t)e->z;
    w[12] = (uint8_t)(e->key >> 24); w[13] = (uint8_t)(e->key >> 16); w[14] = (uint8_t)(e->key >> 8); w[15] = (uint8_t)e->key;
    w[16] = (uint8_t)(e->dist >> 8); w[17] = (uint8_t)e->dist; w[18] = (uint8_t)e->countdown; w[19] = e->counter;
    w[20] = (uint8_t)((uint16_t)e->result >> 8); w[21] = (uint8_t)e->result; w[22] = 0; w[23] = 0;
}

/* Entries of queue t in the original's layout at q (3 queues of 70 x 24 bytes): the count before the terminator word -1. */
static inline int ls_queue_count(const uint8_t *q, int t) {
    int n = 0;
    while (n < 70 && !(q[t * LS_QSTRIDE + 24 * n] == 0xFF && q[t * LS_QSTRIDE + 24 * n + 1] == 0xFF)) n++;
    return n;
}

/* Compares the port's three queues with the original's bytes: id/flags word, X, Y, Z and the key long of every entry, and the counts.
 * Returns the number of equal entries; *total is the number of original entries; the first difference is described in msg. */
static inline int ls_compare(const lists_t *l, const uint8_t *q, int *total, char *msg, size_t msglen) {
    int same = 0;
    *total = 0;
    msg[0] = '\0';
    for (int t = 0; t < 3; t++) {
        int n = ls_queue_count(q, t);
        if (n != (int)l->kept[t] && !msg[0]) snprintf(msg, msglen, "tier %d: original %d entries, port %d", t, n, (int)l->kept[t]);
        for (int i = 0; i < n; i++) {
            const uint8_t *a = q + t * LS_QSTRIDE + 24 * i;
            (*total)++;
            if (i >= (int)l->kept[t]) continue;
            const lists_entry_t *e = &l->q[t][i];
            uint8_t w[16];
            w[0] = e->id; w[1] = e->flags;
            w[2] = 0; w[3] = 0; w[4] = 0; w[5] = 0;
            w[6] = (uint8_t)((uint16_t)e->x >> 8); w[7] = (uint8_t)e->x; w[8] = (uint8_t)((uint16_t)e->y >> 8); w[9] = (uint8_t)e->y;
            w[10] = (uint8_t)((uint16_t)e->z >> 8); w[11] = (uint8_t)e->z;
            w[12] = (uint8_t)(e->key >> 24); w[13] = (uint8_t)(e->key >> 16); w[14] = (uint8_t)(e->key >> 8); w[15] = (uint8_t)e->key;
            if (!memcmp(a, w, 2) && !memcmp(a + 6, w + 6, 10)) same++;
            else if (!msg[0])
                snprintf(msg, msglen, "tier %d entry %d: original id %d flags %02x x %d y %d z %d key %08x, port id %d flags %02x x %d y %d z %d key %08x",
                         t, i, a[0], a[1], (int16_t)ls_u16(a + 6), (int16_t)ls_u16(a + 8), (int16_t)ls_u16(a + 10), ls_u32(a + 12),
                         e->id, e->flags, e->x, e->y, e->z, e->key);
        }
    }
    return same;
}

#endif
