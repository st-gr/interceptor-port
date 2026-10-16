/* src/core/world.c - the world module. See world.h for the interface and the sources.
 *
 * Model walk conventions of the PREVIEW geometry walk (world_model_polys, exact integers; the draw stage, world_draw_entry, reproduces the original's 16-bit word
 * arithmetic instead, see the draw layer below and note 03):
 *  - The viewer ("eye") is in model units along the model axes. The original works in a
 *    shifted vertex scale; the shifts only convert between its working scales, so the port
 *    compares in model units and applies none of them (exact integer arithmetic, int64).
 *  - Three-vertex node/face (note 02 section 3, note 03): value = (v1-v0) x (v2-v0) . (v0-eye);
 *    negative = first child / the face faces the viewer; zero takes the second child.
 *  - Axis node (note 02 section 3): the plane is the vertex coordinate on the axis; the first
 *    child is taken when eye[axis] >= plane; flag bit 0 (the node's bit 12) swaps the children.
 *  - Point-and-normal node / face (note 02 sections 3 and 6): value = (point-eye) . normal;
 *    negative = first child / faces the viewer.
 *  - Axis-aligned polygon (W bits 10-11, note 02 section 6): faces the viewer when
 *    eye[axis] >= coordinate of the last listed vertex, inverted by W bit 12 (the sign is note 02
 *    section 6's, like the axis node's).
 *  - Gates (note 02 section 4): G1 = g1 >> instance scale, G2 = g2 >> instance scale. Distance
 *    d <= G1: drawn when g2 is negative (a signed word). d > G1: drawn when g2 is
 *    non-negative and G2 >= d. g1 = 0x7FFF, or no gated flag, means always.
 *  - Programs: the path from the first item, following each selector's skip list entry for the state (0 = state zero),
 *    up to and including the first last-flagged item; speculative items (flag 64) are never drawn.
 *  - Detail faces (op 4, note 02 section 5): a face is drawn when d <= its distance word
 *    shifted right by the instance scale (INTERPRETATION: the note says only that the first
 *    distance word gates the list); colour 0; a face with a zero flag word is one-sided.
 */
#include <stdlib.h>
#include <string.h>
#include "world.h"
#include "sky.h"
#include "assets.h"
#include "fx.h"
#include "lists.h"
#include "flight.h"

/* ---- file layouts (tools/extract/README.md) ----------------------------------------------- */
#define IMDL_HDR   60u   /* header bytes (version 4: the counts, the second-section pool, then nnormal, alt_near, alt_cockpit and a pad word) */
#define IMDL_LEVEL 54u   /* version 5: 48 bytes and the three words of the data triple */
#define IMDL_VERT  16u
#define IMDL_ELEM  32u
#define IMDL_PROG  12u
#define IMDL_ITEM  40u
#define IMDL_VERSION 5u   /* README: the cockpit-alternate list as extra levels (note 02 section 2.5); older files are rejected */
#define NO_LEVEL 0xFFFFu
#define ITRN_VERSION 2u  /* cells in z then x order (row z, key x: note 04a correction 4) */
#define ITRN_HDR   16u   /* 4s magic, u16 version, u16 table_count, u32 lists, u32 items */
#define ITRN_TABLE 12u
#define ITRN_DEFAULTS 32u   /* 16 x (i8, i8), note 02 section 10 */
#define ITRN_CELL  8u
#define ITRN_LIST  8u
#define ITRN_ITEM  6u
#define RING_ENTRIES 10u    /* note 04: ten camera offsets per class */
#define AIM_ATAN_ADDR 0x42C00u   /* note 03 "External camera aim": the arctangent table, read by address from the image span of the trig tables (flight_span_trig.bin) */

/* element kinds (README table) */
enum { EL_JUMP = 1, EL_AXIS, EL_PLANE, EL_THREE, EL_DRAW };
/* item flags (README) */
enum { IF_LAST = 1, IF_GATED = 2, IF_BLOCKED = 4, IF_TWO_SIDED = 8, IF_BACK = 16, IF_PLANE = 32, IF_SPECULATIVE = 64 };
/* element flag 2 (README): a BSP node on a blocked slot; its subtree is not drawable */
#define EF_BLOCKED_NODE 2
/* element flags 8 and 16: child0 / child1 stands for a stream end by the word 0xFFFF */
#define EF_END0 8
#define EF_END1 16
/* leaf-only last-resort level (README level shape 2) */
#define SHAPE_LEAF_ONLY 2

#define MAX_ENTRY 255        /* position entries kept */
#define MAX_TABLES 3
#define EYE_CLAMP (1 << 26)  /* keeps the int64 products far from overflow */
#define MAX_PROG_VERTS WORLD_POLY_MAX
#define RANGE_TABLE_N 64         /* note 04: 64 bytes at 0x4621C; the value it feeds (V:4AA6A) is dead (note 04a section 8), the table is still read and checked */

typedef struct {
    uint16_t threshold, leaf;
    uint8_t shape, present;
    uint16_t part_id, tm, tf;
    uint8_t scale_h, scale_l, block_flags, b8, b9;
    int32_t root;
    uint32_t vfirst, vcount, efirst, ecount;
    uint32_t sec_first, sec_count;   /* the level's slice of the second-section pool (flat blocks with flag bit 2); 0 words: none */
    int16_t data[3];                 /* the three words after the block's b8 vertices: the draw's vertex loop rotates them into slot b8 when b8 = b9 (note 07 8.10 rule 5) */
} lvl_t;
typedef struct { int32_t x, y, z; uint32_t state; } vtx_t;
typedef struct {
    uint8_t kind, flags, axis, scale;
    int32_t c0, c1;
    int16_t s[6];
    uint32_t lfirst, lcount;
} elem_t;
typedef struct { uint32_t ifirst, icount, address; } prog_t;
typedef struct {
    uint16_t op, flags, g1, g2;
    int16_t extra_n;
    uint16_t extra_slot, pen, w, wback, prim, nops, length;
    uint32_t opfirst, address;
    uint32_t skip_first, skip_count;
} item_t;

typedef struct {
    int status;
    uint16_t kind;          /* header kind: 0 3D, 1 flat, 2 type B */
    uint32_t blocked;
    uint32_t nl, nv, ne, npl, npg, ni, nop, nsk, nsec;
    uint32_t nnormal;       /* levels 0..nnormal-1 are the normal detail list; the rest are the player's cockpit-alternate levels (note 02 section 2.5) */
    uint8_t hull_flags;               /* the pad byte of the level records: +7 of the block the gun's box test reads (extractor 20; note 07 8.2) */
    uint32_t alt_near, alt_cockpit;   /* level indices of the near body and the cockpit block; NO_LEVEL: none */
    uint32_t phase_first;             /* the first of the four phase lists of a cone model (note 07 11.5): the level index; NO_LEVEL: none */
    lvl_t *lv;
    vtx_t *vt;
    elem_t *el;
    uint32_t *pl;
    prog_t *pg;
    item_t *it;
    int32_t *op;
    int32_t *sk;   /* selector skip lists, bytes */
    int32_t *sec;  /* second-section pool: per level groups { n, n x (x, z), m, m x (x, z) } ended by n = -1 (IMDL v3) */
} model_t;

typedef struct { uint16_t x, z; uint32_t list; } cell_t;
typedef struct { uint32_t first, count; } list_t;
typedef struct { unsigned shift, grid, first, count; } table_t;

struct world {
    model_t model[WORLD_MODEL_COUNT];
    /* terrain */
    table_t table[MAX_TABLES];
    unsigned ncells, nlists, nitems;
    cell_t *cell;
    list_t *list;
    world_item_t *item;
    /* positions */
    int32_t entry[MAX_ENTRY + 1][3];
    uint8_t entry_ok[MAX_ENTRY + 1];
    int16_t entry_words[MAX_ENTRY + 1][5];   /* the five words of the position block as the table gives them: cell x, cell z, fine x, fine z, height */
    uint8_t palette[48];
    uint8_t range[RANGE_TABLE_N];   /* tables/range.bin, note 04: read for completeness, the value it feeds is dead */
    int16_t ring[4][RING_ENTRIES][3];
    /* camera */
    world_camera_t cam;
    int32_t in_x, in_y, in_throttle;
    /* the display lists (note 04a) */
    lists_tables_t ltab;
    lists_t lists;
    lists_models_t lmodels;
    int row_limit;  /* V:4AA3C: the 3D window's last row (144, 167 or 179 by view, note 04); the point sprites count against it */
};

/* ---- small helpers -------------------------------------------------------------------- */

static unsigned rd16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }

static void set_err(char *err, size_t errlen, const char *prefix, const char *what) {
    if (!err || !errlen) return;
    err[0] = '\0';
    core_assets_append(err, errlen, prefix);
    core_assets_append(err, errlen, what);
}

static int64_t i64abs(int64_t v) { return v < 0 ? -v : v; }
static int16_t wrap16(int64_t x) { return (int16_t)(uint16_t)((uint64_t)x & 0xFFFFu); }   /* the 68000 word add and subtract wrap */
typedef struct draw draw_t;
static uint32_t angle_units(int32_t a);

static void model_free(model_t *m) {
    free(m->lv); free(m->vt); free(m->el); free(m->pl); free(m->pg); free(m->it); free(m->op); free(m->sk); free(m->sec);
    memset(m, 0, sizeof *m);
}

/* ---- model loading ---------------------------------------------------------------------- */

/* Returns NULL on success or a short reason. */
static const char *model_parse(model_t *m, const uint8_t *b, size_t len, unsigned id) {
    if (len < IMDL_HDR) return "truncated header";
    if (memcmp(b, "IMDL", 4) != 0) return "bad magic";
    if (rd16(b + 4) != IMDL_VERSION) return "IMDL version is not 5 (re-run the extractor)";
    if (rd16(b + 6) != id) return "model id does not match the index";
    m->kind = (uint16_t)rd16(b + 8);
    m->blocked = rd16(b + 10);
    m->nl = rd32(b + 16); m->nv = rd32(b + 20); m->ne = rd32(b + 24); m->npl = rd32(b + 28);
    m->npg = rd32(b + 32); m->ni = rd32(b + 36); m->nop = rd32(b + 40); m->nsk = rd32(b + 44); m->nsec = rd32(b + 48);
    m->nnormal = rd16(b + 52); m->alt_near = rd16(b + 54); m->alt_cockpit = rd16(b + 56);
    m->phase_first = rd16(b + 58) ? rd16(b + 58) - 1u : NO_LEVEL;   /* stored as 1 + the index (0: none) */
    if (m->phase_first != NO_LEVEL && (uint64_t)m->phase_first + 4 > m->nl) return "phase list levels out of range";
    if (m->nnormal > m->nl) return "normal level count out of range";
    if ((m->alt_near != NO_LEVEL && m->alt_near >= m->nl) || (m->alt_cockpit != NO_LEVEL && m->alt_cockpit >= m->nl)) return "alternate level out of range";
    /* counts are bounded by the file size, so the products below cannot overflow 64 bits */
    uint64_t need = IMDL_HDR + (uint64_t)m->nl * IMDL_LEVEL + (uint64_t)m->nv * IMDL_VERT +
                    (uint64_t)m->ne * IMDL_ELEM + 4ull * m->npl + (uint64_t)m->npg * IMDL_PROG +
                    (uint64_t)m->ni * IMDL_ITEM + 4ull * m->nop + 4ull * m->nsk + 4ull * m->nsec;
    if (m->nl > len || m->nv > len || m->ne > len || m->npl > len || m->npg > len ||
        m->ni > len || m->nop > len || m->nsk > len || m->nsec > len || need != len)
        return "size does not match the counts";
    m->lv = (lvl_t *)calloc(m->nl ? m->nl : 1, sizeof(lvl_t));
    m->vt = (vtx_t *)calloc(m->nv ? m->nv : 1, sizeof(vtx_t));
    m->el = (elem_t *)calloc(m->ne ? m->ne : 1, sizeof(elem_t));
    m->pl = (uint32_t *)calloc(m->npl ? m->npl : 1, sizeof(uint32_t));
    m->pg = (prog_t *)calloc(m->npg ? m->npg : 1, sizeof(prog_t));
    m->it = (item_t *)calloc(m->ni ? m->ni : 1, sizeof(item_t));
    m->op = (int32_t *)calloc(m->nop ? m->nop : 1, sizeof(int32_t));
    m->sk = (int32_t *)calloc(m->nsk ? m->nsk : 1, sizeof(int32_t));
    m->sec = (int32_t *)calloc(m->nsec ? m->nsec : 1, sizeof(int32_t));
    if (!m->sec) return "out of memory";
    if (!m->lv || !m->vt || !m->el || !m->pl || !m->pg || !m->it || !m->op || !m->sk) return "out of memory";
    m->hull_flags = m->nl ? b[IMDL_HDR + 17] : 0;
    const uint8_t *p = b + IMDL_HDR;
    for (uint32_t i = 0; i < m->nl; i++, p += IMDL_LEVEL) {
        lvl_t *l = &m->lv[i];
        l->threshold = (uint16_t)rd16(p); l->leaf = (uint16_t)rd16(p + 2);
        l->shape = p[4]; l->present = p[5];
        l->part_id = (uint16_t)rd16(p + 6); l->tm = (uint16_t)rd16(p + 8); l->tf = (uint16_t)rd16(p + 10);
        l->scale_h = p[12]; l->scale_l = p[13]; l->block_flags = p[14]; l->b8 = p[15]; l->b9 = p[16];
        l->root = (int32_t)rd32(p + 20);
        l->vfirst = rd32(p + 24); l->vcount = rd32(p + 28);
        l->efirst = rd32(p + 32); l->ecount = rd32(p + 36);
        l->sec_first = rd32(p + 40); l->sec_count = rd32(p + 44);
        for (int k = 0; k < 3; k++) l->data[k] = (int16_t)rd16(p + 48 + 2 * k);
        if ((uint64_t)l->sec_first + l->sec_count > m->nsec) return "level second-section slice out of range";
        if ((uint64_t)l->vfirst + l->vcount > m->nv) return "level vertex slice out of range";
        if ((uint64_t)l->efirst + l->ecount > m->ne) return "level element slice out of range";
        if (l->root < -1 || l->root >= (int64_t)m->ne) return "level root out of range";
    }
    for (uint32_t i = 0; i < m->nv; i++, p += IMDL_VERT) {
        m->vt[i].x = (int32_t)rd32(p); m->vt[i].y = (int32_t)rd32(p + 4);
        m->vt[i].z = (int32_t)rd32(p + 8); m->vt[i].state = rd32(p + 12);
    }
    for (uint32_t i = 0; i < m->ne; i++, p += IMDL_ELEM) {
        elem_t *e = &m->el[i];
        e->kind = p[0]; e->flags = p[1]; e->axis = p[2]; e->scale = p[3];
        e->c0 = (int32_t)rd32(p + 4); e->c1 = (int32_t)rd32(p + 8);
        for (int k = 0; k < 6; k++) e->s[k] = rds16(p + 12 + 2 * k);
        e->lfirst = rd32(p + 24); e->lcount = rd32(p + 28);
        if (e->c0 < -1 || e->c0 >= (int64_t)m->ne || e->c1 < -1 || e->c1 >= (int64_t)m->ne)
            return "element child out of range";
        if (e->kind == EL_DRAW && (uint64_t)e->lfirst + e->lcount > m->npl)
            return "element program list out of range";
    }
    for (uint32_t i = 0; i < m->npl; i++, p += 4) {
        m->pl[i] = rd32(p);
        if (m->pl[i] >= m->npg) return "program list entry out of range";
    }
    for (uint32_t i = 0; i < m->npg; i++, p += IMDL_PROG) {
        m->pg[i].ifirst = rd32(p); m->pg[i].icount = rd32(p + 4); m->pg[i].address = rd32(p + 8);
        if ((uint64_t)m->pg[i].ifirst + m->pg[i].icount > m->ni) return "program item range out of range";
    }
    for (uint32_t i = 0; i < m->ni; i++, p += IMDL_ITEM) {
        item_t *t = &m->it[i];
        t->op = (uint16_t)rd16(p); t->flags = (uint16_t)rd16(p + 2);
        t->g1 = (uint16_t)rd16(p + 4); t->g2 = (uint16_t)rd16(p + 6);
        t->extra_n = rds16(p + 8); t->extra_slot = (uint16_t)rd16(p + 10);
        t->pen = (uint16_t)rd16(p + 12); t->w = (uint16_t)rd16(p + 14); t->wback = (uint16_t)rd16(p + 16);
        t->prim = (uint16_t)rd16(p + 18); t->nops = (uint16_t)rd16(p + 20); t->length = (uint16_t)rd16(p + 22);
        t->opfirst = rd32(p + 24); t->address = rd32(p + 28);
        t->skip_first = rd32(p + 32); t->skip_count = rd32(p + 36);
        if ((uint64_t)t->skip_first + t->skip_count > m->nsk) return "item skips out of range";
        if ((uint64_t)t->opfirst + t->nops > m->nop) return "item operands out of range";
    }
    for (uint32_t i = 0; i < m->nop; i++, p += 4) m->op[i] = (int32_t)rd32(p);
    for (uint32_t i = 0; i < m->nsk; i++, p += 4) m->sk[i] = (int32_t)rd32(p);
    for (uint32_t i = 0; i < m->nsec; i++, p += 4) m->sec[i] = (int32_t)rd32(p);
    return NULL;
}

/* Reads path through the loader. Returns 0 on success (caller releases). */
static int load_file(const game_loader_t *ld, const char *path, uint8_t **buf, size_t *len) {
    *buf = NULL; *len = 0;
    if (ld->read(ld->user, path, buf, len) != 0 || !*buf) return -1;
    return 0;
}

/* Parses an unsigned/signed decimal at *pp (skipping blanks). Returns 0 on success. */
static int parse_int(const uint8_t **pp, const uint8_t *end, int64_t *out) {
    const uint8_t *p = *pp;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    int neg = 0;
    if (p < end && *p == '-') { neg = 1; p++; }
    if (p >= end || *p < '0' || *p > '9') return -1;
    int64_t v = 0;
    while (p < end && *p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); if (v > 0x7FFFFFFFFLL) return -1; p++; }
    *out = neg ? -v : v;
    *pp = p;
    return 0;
}

static int load_models(world_t *w, const game_loader_t *ld, char *err, size_t errlen) {
    uint8_t *ib; size_t il;
    if (load_file(ld, "models/index.txt", &ib, &il) != 0) {
        set_err(err, errlen, "world_load: cannot read ", "models/index.txt");
        return -1;
    }
    int rc = 0;
    size_t i = 0;
    while (i < il && rc == 0) {
        size_t s = i;
        while (i < il && ib[i] != '\n') i++;
        size_t e = i;
        if (i < il) i++;
        while (e > s && ib[e - 1] == '\r') e--;
        if (e == s || ib[s] == '#') continue;
        const uint8_t *p = ib + s;
        int64_t id;
        if (parse_int(&p, ib + e, &id) != 0 || id < 0 || id >= WORLD_MODEL_COUNT || p >= ib + e || *p != '\t') {
            set_err(err, errlen, "world_load: bad line in ", "models/index.txt"); rc = -1; break;
        }
        p++;
        const uint8_t *fs = p;
        while (p < ib + e && *p != '\t') p++;
        size_t flen = (size_t)(p - fs);
        if (p >= ib + e) { set_err(err, errlen, "world_load: bad line in ", "models/index.txt"); rc = -1; break; }
        p++;
        int st;
        size_t rem = (size_t)(ib + e - p);
        if (rem >= 7 && memcmp(p, "partial", 7) == 0) st = WORLD_MODEL_PARTIAL;
        else if (rem >= 5 && memcmp(p, "empty", 5) == 0) st = WORLD_MODEL_EMPTY;
        else if (rem >= 6 && memcmp(p, "failed", 6) == 0) st = WORLD_MODEL_EMPTY;   /* no file: skipped */
        else if (rem >= 2 && memcmp(p, "ok", 2) == 0) st = WORLD_MODEL_OK;
        else { set_err(err, errlen, "world_load: unknown status in ", "models/index.txt"); rc = -1; break; }
        model_t *m = &w->model[id];
        if (st == WORLD_MODEL_EMPTY) { m->status = st; continue; }
        char path[96];
        if (flen == 0 || flen > 40) { set_err(err, errlen, "world_load: bad file name in ", "models/index.txt"); rc = -1; break; }
        path[0] = '\0';
        core_assets_append(path, sizeof path, "models/");
        size_t pl = strlen(path);
        memcpy(path + pl, fs, flen);
        path[pl + flen] = '\0';
        uint8_t *mb; size_t ml;
        if (load_file(ld, path, &mb, &ml) != 0) {
            set_err(err, errlen, "world_load: cannot read ", path); rc = -1; break;
        }
        const char *why = model_parse(m, mb, ml, (unsigned)id);
        ld->release(ld->user, mb);
        if (why) {
            char msg[160];
            msg[0] = '\0';
            core_assets_append(msg, sizeof msg, path);
            core_assets_append(msg, sizeof msg, ": ");
            core_assets_append(msg, sizeof msg, why);
            set_err(err, errlen, "world_load: ", msg);
            model_free(m);
            rc = -1; break;
        }
        m->status = st;
    }
    ld->release(ld->user, ib);
    return rc;
}

/* ---- terrain, positions, tables --------------------------------------------------------- */

static int load_terrain(world_t *w, const game_loader_t *ld, char *err, size_t errlen) {
    static const char path[] = "terrain/terrain.bin";
    uint8_t *b; size_t len;
    if (load_file(ld, path, &b, &len) != 0) { set_err(err, errlen, "world_load: cannot read ", path); return -1; }
    const char *why = NULL;
    if (len < ITRN_HDR + MAX_TABLES * ITRN_TABLE + ITRN_DEFAULTS) why = "truncated";
    else if (memcmp(b, "ITRN", 4) != 0 || rd16(b + 4) != ITRN_VERSION || rd16(b + 6) != MAX_TABLES) why = "bad magic, version or table count";
    if (!why) {
        w->nlists = rd32(b + 8); w->nitems = rd32(b + 12);
        const uint8_t *p = b + ITRN_HDR;
        unsigned total = 0;
        for (int t = 0; t < MAX_TABLES; t++, p += ITRN_TABLE) {
            table_t *tb = &w->table[t];
            tb->shift = rd16(p); tb->grid = rd16(p + 2); tb->first = rd32(p + 4); tb->count = rd32(p + 8);
            /* the grid must cover the whole 2^29 world (note 02 section 10) so that wrapping works */
            if (tb->grid == 0 || (tb->grid & (tb->grid - 1)) || tb->shift > 28 ||
                ((uint64_t)tb->grid << tb->shift) != (1ull << WORLD_POS_BITS)) { why = "table geometry"; break; }
            if (tb->first != total || tb->count > len) { why = "table cell range"; break; }
            total += tb->count;
        }
        w->ncells = total;
        if (!why) {
            uint64_t need = ITRN_HDR + MAX_TABLES * ITRN_TABLE + ITRN_DEFAULTS + (uint64_t)total * ITRN_CELL +
                            (uint64_t)w->nlists * ITRN_LIST + (uint64_t)w->nitems * ITRN_ITEM;
            if (w->nlists > len || w->nitems > len || need != len) why = "size does not match the counts";
        }
    }
    if (!why) {
        w->cell = (cell_t *)calloc(w->ncells ? w->ncells : 1, sizeof(cell_t));
        w->list = (list_t *)calloc(w->nlists ? w->nlists : 1, sizeof(list_t));
        w->item = (world_item_t *)calloc(w->nitems ? w->nitems : 1, sizeof(world_item_t));
        if (!w->cell || !w->list || !w->item) why = "out of memory";
    }
    if (!why) {
        const uint8_t *p = b + ITRN_HDR + MAX_TABLES * ITRN_TABLE + ITRN_DEFAULTS;
        for (unsigned i = 0; i < w->ncells; i++, p += ITRN_CELL) {
            w->cell[i].x = (uint16_t)rd16(p); w->cell[i].z = (uint16_t)rd16(p + 2); w->cell[i].list = rd32(p + 4);
            if (w->cell[i].list >= w->nlists) { why = "cell list index out of range"; break; }
        }
        for (int t = 0; t < MAX_TABLES && !why; t++) {
            const table_t *tb = &w->table[t];
            for (unsigned i = tb->first; i < tb->first + tb->count; i++) {
                const cell_t *c = &w->cell[i];
                if (c->x >= tb->grid || c->z >= tb->grid) { why = "cell outside its grid"; break; }
                if (i > tb->first) {   /* z (row) then x (key) order, needed by the binary search */
                    const cell_t *q = &w->cell[i - 1];
                    if (c->z < q->z || (c->z == q->z && c->x <= q->x)) { why = "cells not sorted"; break; }
                }
            }
        }
        for (unsigned i = 0; i < w->nlists && !why; i++, p += ITRN_LIST) {
            w->list[i].first = rd32(p); w->list[i].count = rd32(p + 4);
            if ((uint64_t)w->list[i].first + w->list[i].count > w->nitems) why = "list item range out of range";
        }
        for (unsigned i = 0; i < w->nitems && !why; i++, p += ITRN_ITEM) {
            w->item[i].layer = p[0]; w->item[i].object_id = p[1];
            w->item[i].dx = (uint16_t)rd16(p + 2); w->item[i].dz = (uint16_t)rd16(p + 4);
        }
    }
    ld->release(ld->user, b);
    if (why) {
        char msg[96];
        msg[0] = '\0';
        core_assets_append(msg, sizeof msg, path);
        core_assets_append(msg, sizeof msg, ": ");
        core_assets_append(msg, sizeof msg, why);
        set_err(err, errlen, "world_load: ", msg);
        return -1;
    }
    return 0;
}

static int load_positions(world_t *w, const game_loader_t *ld, char *err, size_t errlen) {
    static const char path[] = "terrain/positions.txt";
    uint8_t *b; size_t len;
    if (load_file(ld, path, &b, &len) != 0) { set_err(err, errlen, "world_load: cannot read ", path); return -1; }
    int rc = 0;
    size_t i = 0;
    while (i < len) {
        size_t s = i;
        while (i < len && b[i] != '\n') i++;
        size_t e = i;
        if (i < len) i++;
        while (e > s && (b[e - 1] == '\r' || b[e - 1] == ' ')) e--;
        if (e == s || b[s] == '#') continue;
        const uint8_t *p = b + s;
        int64_t v[6];
        for (int k = 0; k < 6; k++)
            if (parse_int(&p, b + e, &v[k]) != 0) { rc = -1; break; }
        if (rc == 0 && (v[0] < 1 || v[0] > MAX_ENTRY)) rc = -1;
        /* cells are table-C indices (0..127); fine offsets and heights are signed words (note 02 section 9) */
        if (rc == 0 && (v[1] < 0 || v[1] > 127 || v[2] < 0 || v[2] > 127)) rc = -1;
        for (int k = 3; rc == 0 && k < 6; k++) if (v[k] < -32768 || v[k] > 32767) rc = -1;
        if (rc != 0) { set_err(err, errlen, "world_load: bad line in ", path); break; }
        /* position = cell * 2^22 + fine * 256, height * 256 (note 02 section 9) */
        uint32_t x = ((uint32_t)v[1] << 22) + ((uint32_t)v[3] << 8);
        uint32_t z = ((uint32_t)v[2] << 22) + ((uint32_t)v[4] << 8);
        w->entry[v[0]][0] = (int32_t)(x & WORLD_POS_MASK);
        w->entry[v[0]][1] = (int32_t)((uint32_t)v[5] << 8);
        w->entry[v[0]][2] = (int32_t)(z & WORLD_POS_MASK);
        w->entry_ok[v[0]] = 1;
        for (int k = 0; k < 5; k++) w->entry_words[v[0]][k] = (int16_t)v[1 + k];
    }
    ld->release(ld->user, b);
    if (rc == 0 && !w->entry_ok[WORLD_ENTRY_CARRIER]) {
        set_err(err, errlen, "world_load: no carrier entry in ", path);
        rc = -1;
    }
    return rc;
}

static int load_small(world_t *w, const game_loader_t *ld, char *err, size_t errlen) {
    uint8_t *b; size_t len;
    static const char *const ring_names[4] = {
        "tables/camera_ring_default.bin", "tables/camera_ring_11.bin",
        "tables/camera_ring_14.bin", "tables/camera_ring_30.bin" };
    if (load_file(ld, "palette/flight.pal", &b, &len) != 0) {
        set_err(err, errlen, "world_load: cannot read ", "palette/flight.pal"); return -1; }
    if (len != sizeof w->palette) {
        ld->release(ld->user, b);
        set_err(err, errlen, "world_load: wrong size: ", "palette/flight.pal"); return -1; }
    memcpy(w->palette, b, sizeof w->palette);
    ld->release(ld->user, b);
    if (load_file(ld, "tables/lod.bin", &b, &len) != 0) {
        set_err(err, errlen, "world_load: cannot read ", "tables/lod.bin"); return -1; }
    if (len != 240) {   /* note 03: 240 bytes */
        ld->release(ld->user, b);
        set_err(err, errlen, "world_load: wrong size (need 240 bytes): ", "tables/lod.bin"); return -1; }
    fx_set_lod_table(b, len);   /* copied by fx */
    ld->release(ld->user, b);
    if (load_file(ld, "tables/range.bin", &b, &len) != 0) {
        set_err(err, errlen, "world_load: cannot read ", "tables/range.bin"); return -1; }
    if (len != RANGE_TABLE_N) {
        ld->release(ld->user, b);
        set_err(err, errlen, "world_load: wrong size (need 64 bytes): ", "tables/range.bin"); return -1; }
    memcpy(w->range, b, RANGE_TABLE_N);
    ld->release(ld->user, b);
    for (int c = 0; c < 4; c++) {
        if (load_file(ld, ring_names[c], &b, &len) != 0) {
            set_err(err, errlen, "world_load: cannot read ", ring_names[c]); return -1; }
        if (len != RING_ENTRIES * 6) {
            ld->release(ld->user, b);
            set_err(err, errlen, "world_load: wrong size: ", ring_names[c]); return -1; }
        for (unsigned k = 0; k < RING_ENTRIES; k++)
            for (int a = 0; a < 3; a++) w->ring[c][k][a] = rds16(b + 6 * k + 2 * a);
        ld->release(ld->user, b);
    }
    return 0;
}

static void install_models(world_t *w);
world_t *world_load(const game_loader_t *ld, char *err, size_t errlen) {
    if (!ld || !ld->read || !ld->release) {
        set_err(err, errlen, "world_load: ", "loader with read and release callbacks is required");
        return NULL;
    }
    world_t *w = (world_t *)calloc(1, sizeof *w);
    if (!w) { set_err(err, errlen, "world_load: ", "out of memory"); return NULL; }
    if (load_models(w, ld, err, errlen) || load_terrain(w, ld, err, errlen) ||
        load_positions(w, ld, err, errlen) || load_small(w, ld, err, errlen)) {
        world_destroy(w);
        return NULL;
    }
    if (lists_tables_load(&w->ltab, ld, err, errlen) != 0) { world_destroy(w); return NULL; }
    lists_init(&w->lists, &w->ltab, world_lists_cell, w);
    w->row_limit = 179;
    install_models(w);
    world_start(w);
    return w;
}

void world_destroy(world_t *w) {
    if (!w) return;
    for (int i = 0; i < WORLD_MODEL_COUNT; i++) model_free(&w->model[i]);
    free(w->cell); free(w->list); free(w->item);
    lists_tables_free(&w->ltab);
    free(w);
}

/* ---- model queries ---------------------------------------------------------------------- */

static const model_t *get_model(const world_t *w, unsigned id) {
    if (!w || id >= WORLD_MODEL_COUNT) return NULL;
    const model_t *m = &w->model[id];
    return (m->status == WORLD_MODEL_OK || m->status == WORLD_MODEL_PARTIAL) ? m : NULL;
}

int world_model_status(const world_t *w, unsigned id) {
    return (w && id < WORLD_MODEL_COUNT) ? w->model[id].status : WORLD_MODEL_ABSENT;
}
int world_model_level_count(const world_t *w, unsigned id) {
    const model_t *m = get_model(w, id);
    return m ? (int)m->nnormal : 0;
}
int world_model_block_precision(const world_t *w, unsigned id, int level, int *tm, int *tf, int *h, int *l) {
    const model_t *m = get_model(w, id);
    if (!m || level < 0 || (uint32_t)level >= m->nl) return -1;
    const lvl_t *b = &m->lv[level];
    *tm = (int16_t)b->tm; *tf = (int16_t)b->tf; *h = b->scale_h; *l = b->scale_l;
    return 0;
}
int world_model_blocked(const world_t *w, unsigned id) {
    const model_t *m = get_model(w, id);
    return m ? (int)m->blocked : 0;
}
int world_model_vertex_count(const world_t *w, unsigned id, int level) {
    const model_t *m = get_model(w, id);
    if (!m || level < 0 || (uint32_t)level >= m->nl) return -1;
    return (int)m->lv[level].vcount;
}
int world_model_vertex(const world_t *w, unsigned id, int level, unsigned slot, int32_t xyz[3]) {
    const model_t *m = get_model(w, id);
    if (!m || level < 0 || (uint32_t)level >= m->nl || slot >= m->lv[level].vcount) return -1;
    const vtx_t *v = &m->vt[m->lv[level].vfirst + slot];
    xyz[0] = v->x; xyz[1] = v->y; xyz[2] = v->z;
    return 0;
}

int world_model_unit_shift(const world_t *w, uint16_t id, int level) {
    const model_t *m = get_model(w, id);
    if (!m || level < 0 || (uint32_t)level >= m->nl) return -1;
    return m->lv[level].scale_l;
}
int world_model_unit_shift_h(const world_t *w, uint16_t id, int level) {
    const model_t *m = get_model(w, id);
    if (!m || level < 0 || (uint32_t)level >= m->nl) return -1;
    return m->lv[level].scale_h;
}

/* The model's block flag byte (+7 of the block 0x2ADDE names through the first word of the model's list, or the word at +4 when the first has bit 15 clear): the byte 0x2ADDE reads for the box of the gun's hit test, bit 4 selecting the half-size 0x800 and the routine 0x2B3F0 (note 07 8.2). -1 for an unknown model. */
int world_model_block_flags(const world_t *w, uint16_t id) {
    const model_t *m = get_model(w, id);
    if (!m || m->nl == 0) return -1;
    return m->hull_flags;
}

static void make_view_l(const int32_t delta_pos[3], unsigned l, unsigned inst_scale, world_view_t *view) {
    unsigned sh = (l > 8 ? 0u : 8u - l) + (inst_scale & 15u);
    for (int i = 0; i < 3; i++) view->eye[i] = delta_pos[i] >> sh;   /* arithmetic shift, fx.h */
    view->inst_scale = (uint8_t)(inst_scale & 15u);
    view->has_gate = 0; view->gate_lod = 0; view->gate_dist = 0; view->op4_closed = 0;
}

/* view distance (note 03 fx_dist over the absolute eye components, each capped at 0x7FFF). The
 * components are first shifted right by view->band, the precision-band shift of note 02 sections
 * 2.2 and 10 (the original shifts the vertices, and the object offset by correspondingly more
 * bits, so the distance used for level and gate tests shrinks by 2^H between Tm and Tf and by
 * 2^L beyond Tf); the geometric eye stays at the near scale. */
static int32_t view_dist(const world_view_t *v) {
    fx_vec_t d;
    int64_t a[3];
    for (int i = 0; i < 3; i++) {
        a[i] = i64abs(v->eye[i]) >> (v->band & 31u);
        if (a[i] > 0x7FFF) a[i] = 0x7FFF;
    }
    d.x = (int32_t)a[0]; d.y = (int32_t)a[1]; d.z = (int32_t)a[2];
    return fx_dist(&d);
}

int world_pick_level(const world_t *w, unsigned id, const world_view_t *view) {
    const model_t *m = get_model(w, id);
    if (!m || !view) return -1;
    int32_t d = view_dist(view);
    for (uint32_t i = 0; i < m->nnormal; i++) {
        const lvl_t *l = &m->lv[i];
        if (l->shape == SHAPE_LEAF_ONLY) return (int)i;
        if (d < (int32_t)(l->threshold >> view->inst_scale)) return (int)i;
    }
    return -1;
}

/* make_view for a given level (forced >= 0) or the level the exact distance picks (forced < 0). */
static int make_view_at(const world_t *w, unsigned id, const int32_t delta_pos[3], unsigned inst_scale, world_view_t *view, int forced) {
    const model_t *m = get_model(w, id);
    view->band = 0;
    if (!m || !m->nnormal) { make_view_l(delta_pos, 0, inst_scale, view); return -1; }
    /* Tm, Tf, H and L are the same in every level block of a model except the last-resort one, so
     * the object's bands come from the nearest level (note 02 section 2.2); the distance they are
     * compared with is in model units at that level's L. */
    const lvl_t *l0 = &m->lv[0];
    make_view_l(delta_pos, l0->scale_l, inst_scale, view);
    int32_t d0 = view_dist(view);
    if (l0->tf && d0 > (int32_t)(l0->tf >> inst_scale)) view->band = l0->scale_l;      /* beyond Tf: L */
    else if (l0->tm && d0 > (int32_t)(l0->tm >> inst_scale)) view->band = l0->scale_h; /* Tm..Tf: H */
    int lvl = forced >= 0 ? (forced < (int)m->nl ? forced : -1) : world_pick_level(w, id, view);
    if (lvl > 0 && m->lv[lvl].scale_l != l0->scale_l) {
        uint8_t band = view->band;
        make_view_l(delta_pos, m->lv[lvl].scale_l, inst_scale, view);
        view->band = band;
    }
    return lvl;
}

int world_make_view(const world_t *w, unsigned id, const int32_t delta_pos[3],
                    unsigned inst_scale, world_view_t *view) {
    return make_view_at(w, id, delta_pos, inst_scale, view, -1);
}

/* ---- the drawing-tree walk -------------------------------------------------------------- */

typedef struct {
    const model_t *m;
    const lvl_t *lv;
    int64_t eye[3];
    int32_t dist;
    unsigned scale;
    int op4_closed;
    world_poly_t *out;
    int max, n;
    draw_t *dr;          /* the draw layer (world_draw_entry); NULL: the geometry walk collects slot lists only */
} walk_t;

/* Vertex states (README): 0 undefined, 1 block, 2 strip, 3 synthesised, 4 unresolved. A slot
 * that is undefined or unresolved has no usable value: any item that needs it is not drawable. */
#define VS_UNDEFINED 0
#define VS_UNRESOLVED 4
static const vtx_t *vtx(const walk_t *c, int64_t slot) {
    if (slot < 0 || slot >= (int64_t)c->lv->vcount) return NULL;
    const vtx_t *v = &c->m->vt[c->lv->vfirst + (uint32_t)slot];
    return (v->state == VS_UNDEFINED || v->state == VS_UNRESOLVED) ? NULL : v;
}

static int64_t vcoord(const vtx_t *v, int axis) { return axis == 1 ? v->y : axis == 2 ? v->x : v->z; }

/* PREVIEW (the exact-integer geometry walk, not the original's word arithmetic; the draw layer uses side_three, side_plane and side_axis): three-vertex test value; 0 when a slot is invalid (-> "second child", not visible) */
static int64_t three_value(const walk_t *c, int64_t s0, int64_t s1, int64_t s2) {
    const vtx_t *a = vtx(c, s0), *b = vtx(c, s1), *d = vtx(c, s2);
    if (!a || !b || !d) return 0;
    int64_t e1[3] = { (int64_t)b->x - a->x, (int64_t)b->y - a->y, (int64_t)b->z - a->z };
    int64_t e2[3] = { (int64_t)d->x - a->x, (int64_t)d->y - a->y, (int64_t)d->z - a->z };
    int64_t n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
    return n[0] * ((int64_t)a->x - c->eye[0]) + n[1] * ((int64_t)a->y - c->eye[1]) + n[2] * ((int64_t)a->z - c->eye[2]);
}

/* PREVIEW: the point-and-normal value of the geometry walk (the draw layer's form is side_plane). */
static int64_t plane_value(const walk_t *c, const int16_t s[6]) {
    return ((int64_t)s[0] - c->eye[0]) * s[3] + ((int64_t)s[1] - c->eye[1]) * s[4] + ((int64_t)s[2] - c->eye[2]) * s[5];
}

static int gate_ok(const walk_t *c, const item_t *it) {
    if (!(it->flags & IF_GATED) || it->g1 == 0x7FFF) return 1;
    int32_t g1 = (int32_t)(it->g1 >> c->scale);
    int16_t g2 = (int16_t)it->g2;
    if (c->dist <= g1) return g2 < 0;
    return g2 >= 0 && (int32_t)((uint16_t)g2 >> c->scale) >= c->dist;
}

static void emit(walk_t *c, const int32_t *slots, unsigned n, unsigned pen, int derived) {
    if (c->n >= c->max || n == 0 || n > WORLD_POLY_MAX) return;
    world_poly_t *p = &c->out[c->n++];
    p->count = (uint8_t)n; p->pen = (uint8_t)(pen & 15); p->derived_quad = (uint8_t)derived; p->kind = WORLD_PRIM_POLY; p->shift_slot = -1;
    for (unsigned i = 0; i < n; i++) p->slot[i] = (uint16_t)slots[i];
    for (unsigned i = n; i < WORLD_POLY_MAX; i++) p->slot[i] = 0;
}

/* a line (two slots) or a point (one slot), optionally shifted by V[shift] - V[shift + 1] (ops 12 and 14) */
static void emit_prim(walk_t *c, int kind, const int32_t *slots, unsigned n, unsigned pen, int shift) {
    for (unsigned i = 0; i < n; i++) if (!vtx(c, slots[i])) return;
    if (shift >= 0 && (!vtx(c, shift) || !vtx(c, (int64_t)shift + 1))) return;
    int n0 = c->n;
    emit(c, slots, n, pen, 0);
    if (c->n > n0) { world_poly_t *p = &c->out[c->n - 1]; p->kind = (uint8_t)kind; p->shift_slot = (int16_t)shift; }
}

/* PREVIEW (geometry walk): 1 = the polygon faces the viewer (note 02 section 6). plane = 6 words or NULL. The draw layer's face tests are fx_visible and side_plane. */
static int faces_viewer(const walk_t *c, const int32_t *slots, unsigned n, unsigned w, const int16_t *plane) {
    if (plane) {
        int16_t s[6];
        for (int i = 0; i < 6; i++) s[i] = plane[i];
        return plane_value(c, s) < 0;
    }
    int axis = (int)((w >> 10) & 3);
    if (axis) {
        const vtx_t *v = n ? vtx(c, slots[n - 1]) : NULL;
        if (!v) return 0;
        int f = c->eye[axis == 1 ? 1 : axis == 2 ? 0 : 2] >= vcoord(v, axis);
        return (w & 0x1000) ? !f : f;
    }
    if (n < 3) return 1;
    return three_value(c, slots[0], slots[1], slots[2]) < 0;
}

/* PREVIEW (geometry walk): one oriented polygon of op 3 / op 55. Returns 1 when the rest of the program is abandoned. */
static int oriented(walk_t *c, const int32_t *slots, unsigned n, unsigned w, unsigned wback,
                    unsigned flags, const int16_t *plane) {
    if (n > WORLD_POLY_MAX) return 0;
    for (unsigned i = 0; i < n; i++) if (!vtx(c, slots[i])) return 0;   /* unresolved: drop */
    if (flags & IF_TWO_SIDED) { emit(c, slots, n, w, 0); return 0; }
    if (faces_viewer(c, slots, n, w, plane)) { emit(c, slots, n, w, 0); return 0; }
    if (flags & IF_BACK) { emit(c, slots, n, wback, 0); return 0; }
    return 2;   /* faces away, no back colour: skipped and the program abandoned (note 02 section 6) */
}

/* ---- the draw layer: the primitive handlers of note 03 ("Draw operations") on camera-space slots ------------------------------------------------------ */
/* A draw (world_draw_entry) walks the model's tree with a draw_t attached to the walker: every program item is then executed by draw_item, which computes the
 * camera-space slots it names (block set-up of note 03: shift, position, height term, camera matrix, the second vertex section), runs the primitive's rule and
 * returns the handler's result in the two forms the drawers read: the word d->word and a code (0 normal, 1 negative condition codes only, 2 negative word and
 * condition codes). Without a draw_t (world_model_polys) the walker only collects slot lists. */
#define DSLOTS 768
enum { RC_OK = 0, RC_CCNEG = 1, RC_NEG = 2 };
struct draw {
    const world_t *w;
    const model_t *m;
    const lvl_t *lv;
    const lists_t *l;               /* the lists whose scan words (V:4AB2A.., V:4AB30, V:4A83E, V:4A924) the handlers read */
    const game_entry_t *o;
    unsigned lod;
    int16_t sd;                     /* the scaled distance word (04a P1) */
    fx_mat_t cm, ro;
    int attitude;
    int16_t pw[3];
    unsigned vsh;
    int flat;                       /* the block holds (x, z) pairs: flat blocks and type B */
    int nv;
    int row_limit;                  /* V:4AA3C */
    uint16_t fmask;                 /* the fill state words (note 03, "Fill state"): V:4A79E plane mask, */
    int16_t  fpat;                  /* V:4A7A0 pattern (negative: solid pen), */
    uint16_t ftog;                  /* V:4A7A2 toggle (the one word a type B drawer entry leaves as the last object left it) */
    uint16_t falt;                  /* V:4A7A4: the alternate fill path flag (0xFFFF after the near-ground pass; cleared by the common drawer's entry and by the operations that write the whole block) */
    int16_t  pshift, pcount;        /* the shift word V:5103C and the count word V:5103E of the polygon buffer (note 03: op 3 clears the shift at entry and stores the count after its all-behind test, before the face test) */
    int16_t  pbuf[7];               /* the depth words of the seven first vertex positions of the polygon buffer V:5103C (note 03, "Op 9 reads past its quad"): written by every polygon that is built, cleared by nothing */
    unsigned cur_pen;               /* V:4AA0C: the pen of the last primitive that wrote it (op 23 leaves it) */
    uint8_t gear;                   /* the viewed record's byte +0x7C (gear progress in bits 4-6): op 52 skips 14 bytes per unit of it (lists_entity_t.b7c) */
    int raw_polys;                  /* test hook: hand the clipper output to the sink as it is, without the filler front (world_test_handler) */
    world_out_sink_t sink; void *user;
    int word;                       /* the word the last handler returned: -1, 0 or 1 */
    int polys;                      /* handlers that returned a positive word and were not abandoned */
    int last_pos_op, last_abort_op, last_op;   /* for world_draw_info_t.decider */
    int rs_n;                       /* the record-copy operations the program ran (world_draw_info_t.rs): ops 42, 43, 44, 59 with their base slot, 56, 63, 64, 65 with base 0 */
    uint8_t rs_op[WORLD_RS_MAX]; int16_t rs_base[WORLD_RS_MAX];
    fx_vec_t cv[DSLOTS];
    uint8_t have[DSLOTS];
};
static draw_t g_draw;               /* one draw at a time, as the original's single vertex table 0x4D43C */

#define VS_SECOND 5   /* IMDL v3: a slot of the second vertex section, filled by the block set-up */

static fx_vec_t vaddsub(fx_vec_t a, fx_vec_t b, fx_vec_t c) {     /* a - b + c, word arithmetic */
    fx_vec_t r;
    r.x = wrap16((int64_t)a.x - b.x + c.x); r.y = wrap16((int64_t)a.y - b.y + c.y); r.z = wrap16((int64_t)a.z - b.z + c.z);
    return r;
}
static fx_vec_t vsum(fx_vec_t a, fx_vec_t b) {
    fx_vec_t r;
    r.x = wrap16((int64_t)a.x + b.x); r.y = wrap16((int64_t)a.y + b.y); r.z = wrap16((int64_t)a.z + b.z);
    return r;
}
static fx_vec_t vdiff(fx_vec_t a, fx_vec_t b) {
    fx_vec_t r;
    r.x = wrap16((int64_t)a.x - b.x); r.y = wrap16((int64_t)a.y - b.y); r.z = wrap16((int64_t)a.z - b.z);
    return r;
}
/* The camera-space vector of a flat or type B vertex (x, z): the pair is shifted, the position added, the camera matrix applied to (x, 0, z) and the height term (the
 * position y word times the matrix column of y, shifted right by 8) added to every transformed vertex (note 03, block set-up). */
static fx_vec_t flat_point(const draw_t *d, int16_t x, int16_t z) {
    int16_t X = wrap16(((int32_t)x >> d->vsh) + d->pw[0]);
    int16_t Z = wrap16(((int32_t)z >> d->vsh) + d->pw[2]);
    int32_t o[3];
    for (int i = 0; i < 3; i++) {
        int32_t sum = (int32_t)d->cm.m[i][0] * X + (int32_t)d->cm.m[i][2] * Z;
        int32_t hh = ((int32_t)d->cm.m[i][1] * d->pw[1]) >> 8;
        o[i] = wrap16(((int64_t)sum >> 8) + hh);
    }
    fx_vec_t r = { o[0], o[1], o[2] };
    return r;
}

static const fx_vec_t *dsl(draw_t *d, int64_t s) {
    if (s < 0 || s >= d->nv) return NULL;
    if (d->have[s]) return &d->cv[s];
    const vtx_t *v = &d->m->vt[d->lv->vfirst + (uint32_t)s];
    if (v->state == VS_UNDEFINED || v->state == VS_UNRESOLVED || v->state == VS_SECOND) return NULL;
    if (d->flat) {
        d->cv[s] = flat_point(d, (int16_t)v->x, (int16_t)v->z);
    } else {
        fx_vec_t mv = { (int16_t)v->x, (int16_t)v->y, (int16_t)v->z }, w_;
        if (d->attitude) { fx_vec_t rv; fx_rotate(&rv, &d->ro, &mv); mv = rv; }
        w_.x = wrap16(((int32_t)(int16_t)mv.x >> d->vsh) + d->pw[0]);
        w_.y = wrap16(((int32_t)(int16_t)mv.y >> d->vsh) + d->pw[1]);
        w_.z = wrap16(((int32_t)(int16_t)mv.z >> d->vsh) + d->pw[2]);
        fx_camera(&d->cv[s], &d->cm, &w_);
    }
    d->have[s] = 1;
    return &d->cv[s];
}

/* The second vertex section of the flat blocks with flag bit 2 (note 03): groups of { n paired points, m single points }; a paired point makes two slots, the point
 * and the point plus the group's offset, the offset being the camera-space difference of the last two slots written before the group (S[k-1] - S[k-2]); the slots follow
 * the plain ones. The sign of the offset and the count form follow note 03 "The second vertex section" (the do-while count, negative ends the section); the extractor normalises the stream. */
static void second_section(draw_t *d) {
    const lvl_t *lv = d->lv;
    if (!lv->sec_count || !d->flat) return;
    int64_t k = lv->b8;
    const int32_t *p = d->m->sec + lv->sec_first, *end = p + lv->sec_count;
    while (p < end && *p >= 0) {
        int32_t n = *p++;
        const fx_vec_t *a = dsl(d, k - 1), *b = dsl(d, k - 2);
        fx_vec_t off = { 0, 0, 0 };
        if (a && b) off = vdiff(*a, *b);
        for (int32_t j = 0; j < n && p + 1 < end; j++, p += 2, k += 2) {
            fx_vec_t pt = flat_point(d, (int16_t)p[0], (int16_t)p[1]);
            if (k + 1 < d->nv) { d->cv[k] = pt; d->have[k] = 1; d->cv[k + 1] = vsum(pt, off); d->have[k + 1] = 1; }
        }
        if (p >= end) break;
        int32_t m = *p++;
        for (int32_t j = 0; j < m && p + 1 < end; j++, p += 2, k++) {
            if (k < d->nv) { d->cv[k] = flat_point(d, (int16_t)p[0], (int16_t)p[1]); d->have[k] = 1; }
        }
    }
}

/* ---- primitive output ---- */
/* The fill state (note 03, "Fill state"): three words per drawn object. The common drawer's tree-walk entry sets mask 0xF, pattern 0xFFFF and clears the toggle word; the type B
 * drawer's entry sets mask and pattern only and leaves the toggle as the last object left it. An operation that writes the words leaves them in force for the operations after it. */
static void fs_set(draw_t *d, unsigned mask, int pat, int tog) {
    d->fmask = (uint16_t)mask; d->fpat = (int16_t)pat;
    /* the operations that write the toggle word (17 to 23, 27 and 30) write the whole block with V:4A7A4 = 0 (note 03, "Fill state"); op 26 and the line operations write mask and pattern only */
    if (tog >= 0) { d->ftog = (uint16_t)tog; d->falt = 0; }
}
static void fs_reset(draw_t *d, int typeb) {
    d->fmask = 0x000F; d->fpat = (int16_t)0xFFFF;
    if (!typeb) { d->ftog = 0; d->falt = 0; }
}

static void emit_poly(draw_t *d, const int16_t *xy, int n, unsigned pen) {
    world_out_t o;
    memset(&o, 0, sizeof o);
    o.kind = WORLD_OUT_POLY; o.pen = (uint8_t)(pen & 15); o.n = (uint8_t)n;
    o.mask = (uint8_t)(d->fmask & 15); o.pattern = d->fpat; o.toggle = d->ftog;
    o.alt = (uint8_t)(d->fpat >= 0 && d->falt != 0);   /* 0x34EEC: taken when the pattern word is not negative and V:4A7A4 is not zero */
    memcpy(o.xy, xy, (size_t)n * 4);
    d->sink(d->user, &o);
}
static void emit_line(draw_t *d, const int16_t *p, unsigned pen, unsigned mask, int pat) {
    world_out_t o;
    memset(&o, 0, sizeof o);
    o.kind = WORLD_OUT_LINE; o.n = 2; o.mask = (uint8_t)(mask & 15);
    /* a line in pattern mode sets or clears plane k by bit k of the byte V:4A7A1, the low byte of the pattern word (note 03): the same as a solid pen equal to those bits */
    o.pen = (uint8_t)((pat < 0 ? pen : (unsigned)pat) & 15); o.pattern = -1;
    memcpy(o.xy, p, 8);
    d->sink(d->user, &o);
}
static void emit_sprite(draw_t *d, int x, int y, int sprite, unsigned pen) {
    world_out_t o;
    memset(&o, 0, sizeof o);
    o.kind = WORLD_OUT_POINT; o.pen = (uint8_t)(pen & 15); o.mask = (uint8_t)(d->fmask & 15); o.pattern = -1; o.n = 1; o.sprite = (uint8_t)sprite;
    o.xy[0] = (int16_t)x; o.xy[1] = (int16_t)y;
    d->sink(d->user, &o);
}
/* The polygon filler's front (0x34A28, note 03 "Small shapes in the filler", R: the rig has no blitter). Rejects a polygon whose bounding box has its top row below the last row; the
 * fill and the edges are then cut at that row (the rasteriser's window). A box of height h (bottom minus top row) and width w (right minus left column) takes a small-shape path before
 * the area fill: h of 3 or more and w of 1 or less, or h of 1 or less and w of 2 or more: one line from the box's top-left to its bottom-right corner (mask 0xF, solid, the pen);
 * h of 2 and w of 2 or less: the 2 by 2 sprite at row top+1; h and w of 1 or less: the 1 by 1 (w 0) or 2 by 1 (w 1) sprite at row top+1; the sprites are skipped when row top+1 lies beyond
 * the last row. The sprites' column is the box's right column (note 04, "Filling the polygon": the block at (maxx - 1, maxx) or (maxx, maxx + 1) when maxx is a multiple of 16; the dot or dots at minx and maxx). */
static void out_poly(draw_t *d, const int16_t *xy, int n, unsigned pen) {
    if (!d->sink) return;
    if (d->raw_polys) { emit_poly(d, xy, n, pen); return; }
    int top = xy[1], bot = xy[1], left = xy[0], right = xy[0];
    for (int i = 1; i < n; i++) {
        if (xy[2 * i + 1] < top) top = xy[2 * i + 1];
        if (xy[2 * i + 1] > bot) bot = xy[2 * i + 1];
        if (xy[2 * i] < left) left = xy[2 * i];
        if (xy[2 * i] > right) right = xy[2 * i];
    }
    if (top > d->row_limit) return;
    const int h = bot - top, w = right - left;
    if ((h >= 3 && w <= 1) || (h <= 1 && w >= 2)) {
        int16_t l[4] = { (int16_t)left, (int16_t)top, (int16_t)right, (int16_t)bot };
        /* the classification used the uncut box; the line drawer clips to the last row by its step count, min(steps, last row - top) (note 03, "Where the near body falls": no blit at
         * all for a last row equal to the top row); the end point is the line after that many steps (R: the blitter's pixel at the last row itself is unknown) */
        if (bot > d->row_limit && h >= w) {
            const int steps = d->row_limit - top;
            if (steps <= 0) return;
            l[3] = (int16_t)(top + steps); l[2] = (int16_t)(left + (w * steps + h / 2) / h);
        }
        emit_line(d, l, pen, 0xF, -1);
    } else if (h == 2 && w <= 2) {
        if (top + 1 <= d->row_limit) emit_sprite(d, right, top + 1, 2, pen);   /* the block sits at the columns (maxx - 1, maxx), or (maxx, maxx + 1) when maxx is a multiple of 16: the sprite's x is maxx (04 "Filling the polygon") */
    } else if (h <= 1 && w <= 1) {
        if (top + 1 <= d->row_limit) emit_sprite(d, right, top + 1, w == 0 ? 0 : 1, pen);   /* one dot at minx, a second at maxx when w is 1: the 2 by 1 sprite at x = maxx covers (minx, maxx) */
    } else emit_poly(d, xy, n, pen);
}
static void out_line(draw_t *d, const int16_t *p, unsigned pen) {
    if (!d->sink) return;
    emit_line(d, p, pen, d->fmask, d->fpat);
}
static void out_point(draw_t *d, int x, int y, int sprite, unsigned pen) {
    if (!d->sink) return;
    emit_sprite(d, x, y, sprite, pen);
}
static void out_disc(draw_t *d, int x, int y, int r, unsigned pen) {
    if (!d->sink) return;
    world_out_t o;
    memset(&o, 0, sizeof o);
    o.kind = WORLD_OUT_DISC; o.pen = (uint8_t)(pen & 15); o.mask = (uint8_t)(d->fmask & 15); o.pattern = -1; o.n = 1; o.radius = (uint8_t)r;
    o.xy[0] = (int16_t)x; o.xy[1] = (int16_t)y;
    d->sink(d->user, &o);
}

/* The polygon buffer V:5103C keeps the depth words of the vertices the polygon builders write (note 03, "Op 9 reads past its quad"); only the first seven positions are ever read. */
static void pbuf_write(draw_t *d, const fx_vec_t *in, int n) {   /* vertices only; the count word is stored by the builders at their own moment (pbuf_count) */
    for (int i = 0; i < n && i < 7; i++) d->pbuf[i] = (int16_t)in[i].z;
}

/* A polygon through the clipper (shift 0): 1 when at least three vertices remain with positive depth, so that the filler is reached. */
/* The builders of fixed shape (quads, triangles) store the vertices and the count with the shift word 0 before their early-outs. */
static void pbuf_build(draw_t *d, const fx_vec_t *in, int n) { pbuf_write(d, in, n); d->pshift = 0; d->pcount = (int16_t)n; }
static int dpoly(draw_t *d, const fx_vec_t *in, int n, unsigned pen) {
    int16_t xy[2 * 64];
    if (n < 3 || n > FX_CLIP_MAX_IN) return 0;
    d->cur_pen = pen & 15;
    pbuf_write(d, in, n);
    d->pcount = (int16_t)n;
    int c = fx_clip(xy, 64, in, n, 0);
    if (c < 3) return 0;
    out_poly(d, xy, c, pen);
    return 1;
}
static int all_behind(const fx_vec_t *in, int n) {
    for (int i = 0; i < n; i++) if (in[i].z >= 0) return 0;
    return 1;
}

/* The unclipped line's projection (0x3385C; the end points are projected and clamped, test_draw_diff.c: the rows are 179 - sy', as the clipped line's, not the point sprites' 180 - sy'). */
static int lp_div(int32_t prod, int den);
static void proj_line_end(const fx_vec_t *p, int16_t *ox, int16_t *oy) {
    int32_t z = p->z;
    int32_t sx = lp_div((int32_t)p->x * 160, z) + 160, sy = lp_div((int32_t)p->y * 90, z) + 90;   /* the 32 by 16 divide: an overflowing quotient leaves the product's low word */
    sx = sx < 0 ? 0 : (sx > 319 ? 319 : sx); sy = sy < 0 ? 0 : (sy > 179 ? 179 : sy);
    *ox = (int16_t)(319 - sx); *oy = (int16_t)(179 - sy);
}

/* The clipped line routine 0x33936 (note 03, "The clipped line routine"; word exact). Each end point E is clipped separately against the OTHER end's raw coordinates F, never against an
 * already clipped end: the planes are tried in an order fixed by where E lies (closed tests: a point on a plane is beyond it, an F on the plane is outside it), the first plane whose hit
 * lies in the closed pyramid supplies the point that is projected, and the first rejection rejects the whole segment. The planes: 0 XP (z = x), 1 XM (z = -x), 2 YP (z = y), 3 YM (z = -y). */
typedef struct { int x, y, z; } lpt_t;
/* Every negation of the routine is a word negation (note 03, "Every negation in the routine is a word operation"): the negation of -32,768 is -32,768. */
static int neg16(int v) { return (int16_t)(uint16_t)(0u - (uint32_t)v); }
static int lp_beyond(int pl, const lpt_t *p) {
    switch (pl) { case 0: return p->x >= p->z; case 1: return neg16(p->x) >= p->z; case 2: return p->y >= p->z; default: return neg16(p->y) >= p->z; }
}
static int g_op6_small;   /* op 6 items with N below 2 in this draw (port ruling: nothing is drawn) */
static int g_den_zero;   /* segments rejected for a zero margin difference (the original hangs there; port ruling of note 03: rejected and counted) */
static int lp_margin(int pl, const lpt_t *p) {   /* z - x, z + x, z - y, z + y, as a word difference */
    switch (pl) { case 0: return wrap16((int64_t)p->z - p->x); case 1: return wrap16((int64_t)p->z + p->x); case 2: return wrap16((int64_t)p->z - p->y); default: return wrap16((int64_t)p->z + p->y); }
}
static int lp_div(int32_t prod, int den) {   /* a signed 16-bit divide truncated toward zero; an overflowing quotient leaves the low word of the 32-bit product */
    int32_t q = prod / den;
    return (q >= -32768 && q <= 32767) ? (int)q : (int)(int16_t)(prod & 0xFFFF);
}
/* The hit of the segment E-F on plane pl, computed from F toward E (0x33BB2, 0x33BE0, 0x33C14, 0x33C42); -1 when the margin difference is zero (the original never leaves that
 * loop: the port ruling of note 03 rejects the whole segment and counts it in g_den_zero). */
static int lp_hit(int pl, const lpt_t *e, const lpt_t *f, lpt_t *h) {
    int m1 = lp_margin(pl, e), m2 = lp_margin(pl, f), den = wrap16((int64_t)m2 - m1);
    if (den == 0) { g_den_zero++; return -1; }
    const int xplane = pl < 2;
    int oe = xplane ? e->y : e->x, of = xplane ? f->y : f->x;
    int oh = wrap16((int64_t)of - lp_div(m2 * wrap16((int64_t)of - oe), den));
    int zh = wrap16((int64_t)f->z - lp_div(m2 * wrap16((int64_t)f->z - e->z), den));
    h->z = zh;
    if (xplane) { h->y = oh; h->x = pl == 0 ? zh : neg16(zh); } else { h->x = oh; h->y = pl == 2 ? zh : neg16(zh); }
    return 1;
}
static int lp_in_pyramid(const lpt_t *h) {   /* |x| > z is "x > z or -x > z" with the word negation */
    return h->z >= 0 && !(h->x > h->z) && !(neg16(h->x) > h->z) && !(h->y > h->z) && !(neg16(h->y) > h->z);
}
/* A plane and its partner try: the hit on `pl` is used when it lies in the pyramid; otherwise the partner (the opposite plane) is tried only when the first hit has z < 0, F is not
 * outside the partner and (cond_e) E is beyond it. Returns 1 used (*out set), 0 not used, -1 reject (F outside `pl`, or a zero margin difference). */
static int lp_pair(int pl, int partner, int cond_e, const lpt_t *e, const lpt_t *f, lpt_t *out) {
    if (lp_beyond(pl, f)) return -1;   /* F on or beyond the plane on the outside: F outside this plane means z' <= the plane's coordinate */
    lpt_t h;
    if (lp_hit(pl, e, f, &h) < 0) return -1;
    if (lp_in_pyramid(&h)) { *out = h; return 1; }
    if (h.z < 0 && !lp_beyond(partner, f) && (!cond_e || lp_beyond(partner, e))) {
        lpt_t h2;
        if (lp_hit(partner, e, f, &h2) < 0) return -1;
        if (lp_in_pyramid(&h2)) { *out = h2; return 1; }
    }
    return 0;
}
/* Clips end e against f: 1 with *out the point to project, 0 reject. */
static int lp_clip_end(const lpt_t *e, const lpt_t *f, lpt_t *out) {
    int r;
    if (lp_beyond(0, e) || lp_beyond(1, e)) {
        const int first = lp_beyond(0, e) ? 0 : 1;   /* XP when beyond XP, else XM */
        r = lp_pair(first, 1 - first, 0, e, f, out);
        if (r < 0) return 0;
        if (r > 0) return 1;
        /* the y stage: E must itself be beyond YP (y >= 0) or YM (y < 0) */
        const int y1 = e->y >= 0 ? 2 : 3;
        if (!lp_beyond(y1, e)) return 0;
        r = lp_pair(y1, 5 - y1, 1, e, f, out);
        return r > 0;
    }
    if (lp_beyond(2, e) || lp_beyond(3, e)) {
        const int first = lp_beyond(2, e) ? 2 : 3;   /* E inside both x half-spaces: YP, then YM (or YM, then YP); no further stage */
        r = lp_pair(first, 5 - first, 0, e, f, out);
        return r > 0;
    }
    *out = *e;   /* inside all four half-spaces: projected as it is */
    return 1;
}
/* The projection of a used point (the clipped line's own: sx' = T(160 x / z) + 160, sy' = T(90 y / z) + 90, clamped, screen (319 - sx', 179 - sy')); a depth that is not positive rejects. */
static int lp_project(const lpt_t *p, int16_t *ox, int16_t *oy) {
    if (p->z <= 0) return 0;
    int sx = lp_div(160 * p->x, p->z) + 160, sy = lp_div(90 * p->y, p->z) + 90;
    sx = sx < 0 ? 0 : (sx > 319 ? 319 : sx); sy = sy < 0 ? 0 : (sy > 179 ? 179 : sy);
    *ox = (int16_t)(319 - sx); *oy = (int16_t)(179 - sy);
    return 1;
}
/* The clipped line: 1 when a non-empty segment was handed to the line routine. */
static int line_clip(const fx_vec_t *a, const fx_vec_t *b, int16_t out[4]) {
    lpt_t pa = { (int16_t)a->x, (int16_t)a->y, (int16_t)a->z }, pb = { (int16_t)b->x, (int16_t)b->y, (int16_t)b->z }, ua, ub;
    if (!lp_clip_end(&pa, &pb, &ua)) return 0;
    if (!lp_clip_end(&pb, &pa, &ub)) return 0;
    if (!lp_project(&ua, &out[0], &out[1]) || !lp_project(&ub, &out[2], &out[3])) return 0;
    return 1;
}
static int dline(draw_t *d, const fx_vec_t *a, const fx_vec_t *b, unsigned pen) {
    int16_t xy[4];
    d->cur_pen = pen & 15;
    if (!line_clip(a, b, xy)) return 0;
    out_line(d, xy, pen);
    return 1;
}
/* |x| <= z and |y| <= z (the closed pyramid; z positive follows from the tests, except that the word negation lets x or y of -32,768 pass for z >= -32,768). `word` 1: the tests are word operations, -x of -32,768 is -32,768 (op 48 with z > 0 tested first; word 2, ops 39 and 40: no z test, so x or y of -32,768 passes for any z >= -32,768; V: DRAW_DIFF_EDGE of tests/rig/test_draw_diff.c);
 * `word` 0: the 32-bit absolute value (no caller now). */
static int inside_closed(const fx_vec_t *p, int word) {
    if (word == 1 && p->z <= 0) return 0;
    if (word) return !(p->x > p->z) && !(neg16(p->x) > p->z) && !(p->y > p->z) && !(neg16(p->y) > p->z);
    if (p->z <= 0) return 0;
    int32_t ax = p->x < 0 ? -p->x : p->x, ay = p->y < 0 ? -p->y : p->y;
    return ax <= p->z && ay <= p->z;
}
/* Op 48 (0x3385C): not clipped; counts and draws only when both end points are inside the closed pyramid. */
static int dline_unclipped(draw_t *d, const fx_vec_t *a, const fx_vec_t *b, unsigned pen) {
    d->cur_pen = pen & 15;
    if (a->z < 0 && b->z < 0) return 0;
    if (!inside_closed(a, 1) || !inside_closed(b, 1)) return 0;
    int16_t xy[4];
    proj_line_end(a, &xy[0], &xy[1]); proj_line_end(b, &xy[2], &xy[3]);
    out_line(d, xy, pen);
    return 1;
}
/* The sprite of ops 1 and 49 by distance (note 03, "Which size an op uses"). */
static int sprite_size(const draw_t *d) {
    if ((0x30 >> d->lod) >= d->sd) return 2;
    if ((0x50 >> d->lod) >= d->sd) return 1;
    return 0;
}
static int dpoint(draw_t *d, const fx_vec_t *v, unsigned pen, int sprite) {
    int16_t sx, sy;
    d->cur_pen = pen & 15;
    if (!fx_project_rows(&sx, &sy, v, (int16_t)d->row_limit)) return 0;
    out_point(d, sx, sy, sprite, pen);
    return 1;
}
/* line pair of op 2/13/14/75 families: both depths negative skips the pair */
static int dline_pair(draw_t *d, const fx_vec_t *a, const fx_vec_t *b, unsigned pen) {
    if (a->z < 0 && b->z < 0) return 0;
    return dline(d, a, b, pen);
}

/* ---- the handlers ---- */
static int slots_get(draw_t *d, const int32_t *s, int n, fx_vec_t *out) {
    for (int i = 0; i < n; i++) { const fx_vec_t *v = dsl(d, s[i]); if (!v) return 0; out[i] = *v; }
    return 1;
}

/* ---- the side tests of the block tree and of a polygon's plane form (note 03, "The side tests of the block tree"; every step is a 16-bit word operation that wraps) ---- */
/* The entry's position words V:4ABE2 (x) and V:4ABE6 (z). */
static int16_t side_ex(const draw_t *d) { return d->o ? d->o->x : 0; }
static int16_t side_ez(const draw_t *d) { return d->o ? d->o->z : 0; }
/* Axis node (el->axis 1 y, 2 x, 3 z; s[0] the block vertex; flag bit 0 = bit 12 inverts): 1 selects the first child. c is the vertex's coordinate word shifted right by the
 * block's L. x and z: Pc = c + (the entry's position word shifted left by the LOD), E = minus the camera word V:4AB2A / V:4AB2E; y: Pc = c, E = minus the long V:4AB30, compared as longs.
 * "Below" is E < Pc (signed); the first child is the not-below side (bit 12 clear). */
static int side_axis(const draw_t *d, const elem_t *el) {
    const lists_scan_t *sc = &d->l->scan;
    if (el->s[0] < 0 || (uint32_t)el->s[0] >= d->lv->vcount) return 0;
    const vtx_t *v = &d->m->vt[d->lv->vfirst + (uint32_t)el->s[0]];
    if (v->state == VS_UNDEFINED || v->state == VS_UNRESOLVED) return 0;
    const int cw = el->axis == 1 ? v->y : el->axis == 2 ? v->x : v->z;
    const int c = (int)(int16_t)cw >> d->lv->scale_l;
    int below;
    if (el->axis == 1) below = -(int64_t)sc->off_y < (int64_t)c;
    else {
        const int pos = el->axis == 2 ? side_ex(d) : side_ez(d), cam = el->axis == 2 ? sc->off_x : sc->off_z;
        const int pc = wrap16((int64_t)c + wrap16((int64_t)pos * (1 << d->lod))), e = wrap16(-(int64_t)cam);
        below = e < pc;
    }
    return (el->flags & 1) ? below : !below;
}
/* Point-and-normal node, and the plane form of a polygon's face test (s[0..2] the point, s[3..5] the normal): d_i = p_i >> LOD (a word), d_x += V:4ABE2, d_z += V:4ABE6, then d_i += the low word
 * of the camera long (V:4AB1A, V:4AB1E, V:4AB22) shifted right by the LOD and by 8; the sum of the three products with the normal, a 32-bit sum; negative selects the first child (faces the viewer). */
static int side_plane(const draw_t *d, const int16_t s[6]) {
    const lists_scan_t *sc = &d->l->scan;
    const int32_t cam[3] = { sc->cam_x, sc->cam_y, sc->cam_z };
    int dd[3];
    for (int i = 0; i < 3; i++) {
        int w = wrap16((int64_t)(s[i] >> d->lod));
        if (i == 0) w = wrap16((int64_t)w + side_ex(d));
        if (i == 2) w = wrap16((int64_t)w + side_ez(d));
        dd[i] = wrap16((int64_t)w + (int16_t)((cam[i] >> d->lod) >> 8));
    }
    uint32_t sum = (uint32_t)((int32_t)dd[0] * s[3]) + (uint32_t)((int32_t)dd[1] * s[4]) + (uint32_t)((int32_t)dd[2] * s[5]);
    return (int32_t)sum < 0;
}
static int side_three(draw_t *d, const elem_t *el) {
    const fx_vec_t *a = dsl(d, el->s[0]), *b = dsl(d, el->s[1]), *c = dsl(d, el->s[2]);
    if (!a || !b || !c) return 0;
    return fx_visible(a, b, c, (uint16_t)(el->scale << 7));
}

/* The tail of op 3 (0x2377C) for n camera-space vertices: every depth negative or the face away without a back colour returns -1; W bit 15 skips the face test. */
static int poly_op3(draw_t *d, const fx_vec_t *in, unsigned n, unsigned prewrite, unsigned W, unsigned wback, unsigned flags, const int16_t *plane) {
    pbuf_write(d, in, (int)prewrite);   /* the vertices are copied to the polygon buffer first: the all-behind and face exits leave them there too (rig probe, test_draw_diff.c); op 69 writes V0 to V2 only, V3 once the test passes (note 03) */
    if (all_behind(in, (int)n)) { d->word = -1; return RC_NEG; }   /* an all-behind rejection leaves the old count word */
    pbuf_write(d, in, (int)n);
    d->pcount = (int16_t)n;                                           /* the count is stored after the all-behind test and before the face test */
    unsigned pen = W & 15;
    if (!(flags & IF_TWO_SIDED)) {
        int facing;   /* the side test 0x22A84: the point-and-normal form when the polygon carries a plane record, else the three-vertex test on the camera-space words */
        if (plane) facing = side_plane(d, plane);
        else facing = n < 3 ? 1 : fx_visible(&in[0], &in[1], &in[2], (uint16_t)W);
        if (!facing) {
            if (flags & IF_BACK) pen = wback & 15;
            else { d->word = -1; return RC_NEG; }
        }
    }
    d->word = dpoly(d, in, (int)n, pen);
    return RC_OK;
}

/* ---- the hatch and stripe family (ops 19-23, 32, 57; note 03, "Hatch and stripe geometry") ---- */
/* Every sum and difference is a 16-bit word operation, h(x) is x shifted right arithmetically by 1 as a word; the stroke offsets are h of the accumulated step (acc_0 = 0, acc_(k+1) = acc_k + Q), so o_k
 * is h(k Q), not k h(Q). Counts are do-whiles: a count of 0 or less still draws one stroke. A call counts when the clipper returned 1. */
static fx_vec_t vhalf(fx_vec_t a) { fx_vec_t r = { wrap16(a.x >> 1), wrap16(a.y >> 1), wrap16(a.z >> 1) }; return r; }
static int hcount(int32_t n) { int16_t w = (int16_t)n; return w < 1 ? 1 : w; }   /* the do-while count: the word, 0 or less draws one (up to 32,767; no cap) */

static int hatch_op19(draw_t *d, const int32_t *o, int nops, unsigned pen) {   /* 2N clipped lines */
    if (nops < 2) return 0;
    const fx_vec_t *S0 = dsl(d, o[0]), *S1 = dsl(d, o[0] + 1), *S2 = dsl(d, o[0] + 2);
    if (!S0 || !S1 || !S2) return 0;
    fs_set(d, 0xF, -1, 0);
    const int N = hcount(o[1]);
    const fx_vec_t Q = vdiff(*S1, *S2), hQ = vhalf(Q);
    fx_vec_t acc = { 0, 0, 0 };
    int w = 0;
    for (int k = 0; k < N; k++) {   /* S2 + o_k to S0 + o_k */
        fx_vec_t ok = vhalf(acc), a = vsum(*S2, ok), b = vsum(*S0, ok);
        w |= dline(d, &a, &b, pen);
        acc = vsum(acc, Q);   /* the step after the stroke; not in the for-increment: tcc 0.9.27 (Debian's 2020-08-14 snapshot) miscompiles a struct assignment there and the counter never advances */
    }
    acc.x = acc.y = acc.z = 0;
    for (int k = 0; k < N; k++) {   /* S0 - h(Q) - o_k to S2 - h(Q) - o_k: the other side of the edge, reversed */
        fx_vec_t ok = vhalf(acc), a = vdiff(vdiff(*S0, hQ), ok), b = vdiff(vdiff(*S2, hQ), ok);
        w |= dline(d, &a, &b, pen);
        acc = vsum(acc, Q);   /* the step after the stroke; not in the for-increment: tcc 0.9.27 (Debian's 2020-08-14 snapshot) miscompiles a struct assignment there and the counter never advances */
    }
    return w;
}
static int hatch_op20_32(draw_t *d, const item_t *it, const int32_t *o, int nops) {   /* 2N polygons; only the first N count */
    if (nops < 2) return 0;
    const fx_vec_t *S0 = dsl(d, o[0]), *S1 = dsl(d, o[0] + 1), *S2 = dsl(d, o[0] + 2), *S3 = dsl(d, o[0] + 3);
    if (!S0 || !S1 || !S2 || !S3) return 0;
    const int is20 = it->op == 20;
    const unsigned pen = is20 ? 13u : it->pen;
    if (is20) fs_set(d, 2, 0, 2); else fs_set(d, 0xF, -1, -1);
    const int N = hcount(o[1]);
    const fx_vec_t P = vdiff(*S0, *S2), Q = vdiff(*S1, *S2), hQ = vhalf(Q);
    fx_vec_t acc = { 0, 0, 0 };
    int w = 0;
    for (int k = 0; k < N; k++) {   /* [S2 + o_k, S0 + o_k, S3 + P + o_k, S3 + o_k] */
        fx_vec_t ok = vhalf(acc), q[4];
        q[0] = vsum(*S2, ok); q[1] = vsum(*S0, ok); q[2] = vsum(vsum(*S3, P), ok); q[3] = vsum(*S3, ok);
        w |= dpoly(d, q, 4, pen);
        acc = vsum(acc, Q);   /* the step after the stroke; not in the for-increment: tcc 0.9.27 (Debian's 2020-08-14 snapshot) miscompiles a struct assignment there and the counter never advances */
    }
    acc.x = acc.y = acc.z = 0;
    for (int k = 0; k < N; k++) {   /* o'_k = h(Q) + o_k: [S0 - o', S2 - o', S3 - o', S3 + P - o']; drawn, never counted */
        fx_vec_t ok = vsum(hQ, vhalf(acc)), q[4];
        q[0] = vdiff(*S0, ok); q[1] = vdiff(*S2, ok); q[2] = vdiff(*S3, ok); q[3] = vdiff(vsum(*S3, P), ok);
        dpoly(d, q, 4, pen);
        acc = vsum(acc, Q);   /* the step after the stroke; not in the for-increment: tcc 0.9.27 (Debian's 2020-08-14 snapshot) miscompiles a struct assignment there and the counter never advances */
    }
    return w;
}
static int hatch_op21_57(draw_t *d, const item_t *it, const int32_t *o, int nops) {   /* N1 rows of stripes on the slots S(3+i) */
    if (nops < 3) return 0;
    const fx_vec_t *S0 = dsl(d, o[0]), *S1 = dsl(d, o[0] + 1), *S2 = dsl(d, o[0] + 2), *S3 = dsl(d, o[0] + 3);
    if (!S0 || !S1 || !S2 || !S3) return 0;
    const int is21 = it->op == 21;
    const unsigned pen = is21 ? 13u : it->pen;
    if (is21) fs_set(d, 2, 0, 2); else fs_set(d, 0xF, -1, -1);
    const int N1 = hcount(o[1]);
    const fx_vec_t P = vdiff(*S0, *S3), Q = vdiff(*S1, *S3), R = vdiff(*S2, *S3);
    int w = 0;
    for (int i = 0; i < N1 && 2 + i < nops; i++) {
        const fx_vec_t *B = dsl(d, o[0] + 3 + i);
        if (!B) continue;
        const int N2 = hcount(o[2 + i]);
        fx_vec_t acc = { 0, 0, 0 };
        for (int j = 0; j < N2; j++) {   /* [B + o_j, B + P + o_j, B + P + R + o_j, B + R + o_j], o_j = h(j Q) */
            fx_vec_t oj = vhalf(acc), q[4];
            q[0] = vsum(*B, oj); q[1] = vsum(vsum(*B, P), oj); q[2] = vsum(vsum(vsum(*B, P), R), oj); q[3] = vsum(vsum(*B, R), oj);
            w |= dpoly(d, q, 4, pen);
            acc = vsum(acc, Q);   /* the step after the stroke; not in the for-increment: tcc 0.9.27 (Debian's 2020-08-14 snapshot) miscompiles a struct assignment there and the counter never advances */
        }
    }
    return w;
}
static int hatch_op22(draw_t *d, const item_t *it, const int32_t *o, int nops) {   /* N1 rows of clipped-line strokes on the slots S(2+i) */
    if (nops < 3) return 0;
    const fx_vec_t *S0 = dsl(d, o[0]), *S1 = dsl(d, o[0] + 1), *S2 = dsl(d, o[0] + 2);
    if (!S0 || !S1 || !S2) return 0;
    fs_set(d, 0xF, -1, 0);
    const int N1 = hcount(o[1]);
    const fx_vec_t W = vdiff(*S1, *S2), Qp = vdiff(*S0, *S2);
    int w = 0;
    for (int i = 0; i < N1 && 2 + i < nops; i++) {
        const fx_vec_t *B = dsl(d, o[0] + 2 + i);
        if (!B) continue;
        const int N2 = hcount(o[2 + i]);
        fx_vec_t acc = { 0, 0, 0 };
        for (int j = 0; j < N2; j++) {   /* from B + o_j to B + W + o_j, o_j = h(j Q') */
            fx_vec_t oj = vhalf(acc), a = vsum(*B, oj), b = vsum(vsum(*B, W), oj);
            w |= dline(d, &a, &b, it->pen);
            acc = vsum(acc, Qp);   /* the step after the stroke; not in the for-increment: tcc 0.9.27 (Debian's 2020-08-14 snapshot) miscompiles a struct assignment there and the counter never advances */
        }
    }
    return w;
}
static int hatch_op23(draw_t *d, const int32_t *o, int nops) {   /* N strips, one per slot S(3+i), a step of one and a half edges wide */
    if (nops < 2) return 0;
    const fx_vec_t *S0 = dsl(d, o[0]), *S1 = dsl(d, o[0] + 1), *S2 = dsl(d, o[0] + 2), *S3 = dsl(d, o[0] + 3);
    if (!S0 || !S1 || !S2 || !S3) return 0;
    fs_set(d, 2, 0, 2);   /* the pen is not written (unused in the toggle mode) */
    const int N = hcount(o[1]);
    const fx_vec_t P = vdiff(*S0, *S3), Q = vdiff(*S1, *S3), R = vdiff(*S2, *S3);
    const fx_vec_t T = vsum(P, vhalf(vsum(Q, vsum(Q, Q))));   /* P + h(3 Q), 3 Q = Q + 2 Q as words */
    int w = 0;
    for (int i = 0; i < N; i++) {
        const fx_vec_t *B = dsl(d, o[0] + 3 + i);
        if (!B) continue;
        fx_vec_t q[4];
        q[0] = *B; q[1] = vsum(*B, T); q[2] = vsum(vsum(*B, R), T); q[3] = vsum(*B, R);
        w |= dpoly(d, q, 4, d->cur_pen);
    }
    return w;
}

static int box_pretest(const draw_t *d, unsigned operand);   /* op 31, below */
/* A synthesised slot written by an item at run time (ops 52 and 53 compute the camera-space word triple the original stores in its vertex table). */
static void synth_put(draw_t *d, int64_t slot, fx_vec_t v) {
    if (slot < 0 || slot >= d->nv) return;
    d->cv[slot] = v; d->have[slot] = 1;
}

/* A filled disc (op 62 and the items of op 71, note 03): the vertex words shifted left by the frame's shift word (the block's vertex shift plus the LOD), distance fx_dist of their absolute values,
 * R = 127 when it is not positive, else the size S (sign-extended) divided by it with the divide-overflow rule, limited to 127; R <= 0 draws the 2 by 1 sprite (op 62's rule). */
static int draw_disc(draw_t *d, int32_t voff, int32_t pen_word, int32_t size_word) {
    const fx_vec_t *v0 = dsl(d, voff / 6);
    if (!v0) return RC_OK;
    fx_vec_t v = { wrap16((int64_t)v0->x * (1 << d->vsh)), wrap16((int64_t)v0->y * (1 << d->vsh)), wrap16((int64_t)v0->z * (1 << d->vsh)) };
    fx_vec_t a = { v.x < 0 ? -v.x : v.x, v.y < 0 ? -v.y : v.y, v.z < 0 ? -v.z : v.z };
    if (a.x > 0x7FFF) a.x = 0x7FFF;
    if (a.y > 0x7FFF) a.y = 0x7FFF;
    if (a.z > 0x7FFF) a.z = 0x7FFF;
    int32_t dist = (int16_t)fx_dist(&a);
    int32_t S = (int16_t)(uint16_t)((unsigned)size_word & 0xFFFFu);   /* sign-extended to 32 bits */
    int32_t r;
    if (dist <= 0) r = 127;
    else {
        uint32_t q = (uint32_t)S / (uint32_t)dist;
        r = q <= 0xFFFFu ? (int16_t)q : (int16_t)((uint32_t)S & 0xFFFFu);   /* a divide overflow leaves the dividend's low word */
        if (r > 127) r = 127;
    }
    int16_t sx, sy;
    if (!fx_project_rows(&sx, &sy, &v, (int16_t)d->row_limit)) return RC_CCNEG;
    d->cur_pen = (unsigned)pen_word & 15;
    out_disc(d, sx, sy, r > 0 ? (int)r : 0, (unsigned)pen_word & 15);
    d->word = 1;
    return RC_OK;
}

static int draw_item(walk_t *c, const item_t *it) {
    draw_t *d = c->dr;
    const int32_t *o = c->m->op + it->opfirst;
    const int nops = (int)it->nops;
    const lists_scan_t *sc = &d->l->scan;
    fx_vec_t in[WORLD_POLY_MAX + 4];
    d->word = 0;
    /* V:4AA0C: the handlers that take a pen write it when they are entered, whatever they go on to draw (test_draw_diff.c: a line or polygon operation that draws nothing still leaves its pen
     * for the operation that does not write one, op 23) */
    switch (it->op) {
    case 1: case 58: case 2: case 48: case 13: case 14: case 11: case 12: case 6: case 19: case 22: case 24: case 25: case 27: case 30: case 17: case 18: case 32: case 57: case 20: case 21:
    case 15: case 16: case 9: case 10:   /* 9 and 10 before their height gate; 27 and 30 before their early-out (note 03, "When V:4AA0C is written") */
        d->cur_pen = it->pen & 15u; break;   /* ops 74 and 75 write it after their height gate, op 26 after its early-out; ops 3, 69, 39, 40 and 49 after their -1 and away exits; op 23 never */
    case 7: case 8:
        if (nops >= 1) d->cur_pen = (unsigned)o[0] & 15u;
        break;
    case 62:
        if (nops >= 2) d->cur_pen = (unsigned)o[1] & 15u;
        break;
    default: break;
    }
    switch (it->op) {
    case 1: case 58: {   /* a point sprite; a rejected point returns 0 with negative condition codes and abandons the program */
        const fx_vec_t *v = nops >= 1 ? dsl(d, o[0]) : NULL;
        if (!v) return RC_OK;
        if (dpoint(d, v, it->pen, it->op == 58 ? 1 : sprite_size(d))) { d->word = 1; return RC_OK; }
        return RC_CCNEG;
    }
    case 49: {   /* a point at vertex 0 when the face of the three vertices is not away */
        if (nops < 3 || !slots_get(d, o, 3, in)) return RC_OK;
        if (!fx_visible(&in[0], &in[1], &in[2], it->w)) return RC_OK;
        if (dpoint(d, &in[0], it->w & 15, sprite_size(d))) { d->word = 1; return RC_OK; }
        return RC_CCNEG;
    }
    case 62: {   /* a filled disc (note 03): the operands, in this order: the vertex byte offset v, the pen, the size S */
        if (nops < 3) return RC_OK;
        return draw_disc(d, o[0], o[1], o[2]);
    }
    case 71: {   /* a list of discs as op 62 (note 07 11.3 item 2, note 03 op 71; the starburst of the effect models 33 to 35): ops = [count, then (vertex byte offset, pen, size) per disc]. The loop of
                  * 0x2576E ends only on the size word's bit 15: a disc that fails its projection does not end the list, every disc is handed to the disc routine (the same as op 62's) and the
                  * results are ORed; the pen word V:4AA0C after the list is the last disc's pen even when that disc was rejected. The return when every disc was rejected (a negative code that
                  * abandons the program) matters for nothing: op 71 ends the program and the result of a secondary entry's drawer is never read (note 07 11.1). */
        if (nops < 1) return RC_OK;
        const int n = o[0];
        int drew = 0, last = -1;
        for (int k = 0; k < n && 3 * k + 3 < nops; k++) {
            if (draw_disc(d, o[1 + 3 * k], o[2 + 3 * k], o[3 + 3 * k]) == RC_OK) drew = 1;
            last = k;
        }
        if (last >= 0) d->cur_pen = (unsigned)o[2 + 3 * last] & 15;
        if (!drew) return RC_CCNEG;
        d->word = 1;
        return RC_OK;
    }
    case 70: return RC_OK;   /* the timer skip: the op word alone, it draws nothing; the stream skip is next_item's (the object's life word, note 07 11.3) */
    case 2: case 74: {   /* the clipped line; op 74 only above 240 ft */
        if (it->op == 74 && !(sc->off_y < -0xC0)) return RC_OK;
        if (it->op == 74) d->cur_pen = it->pen & 15u;
        if (nops < 2 || !slots_get(d, o, 2, in)) return RC_OK;
        fs_set(d, 0xFFFF, -1, -1);
        d->word = dline_pair(d, &in[0], &in[1], it->pen);
        return RC_OK;
    }
    case 48: {
        if (nops < 2 || !slots_get(d, o, 2, in)) return RC_OK;
        fs_set(d, 0xFFFF, -1, -1);
        d->word = dline_unclipped(d, &in[0], &in[1], it->pen);
        return RC_OK;
    }
    case 13: case 14: case 75: {   /* line lists; op 14 draws the pairs of op 13 with both end points reduced by V[vref+1] - V[vref] and does not skip a pair with both depths behind (the clipped line decides) */
        if (it->op == 75 && !(sc->off_y < -0xC0)) return RC_OK;
        if (it->op == 75 && nops >= 1) d->cur_pen = (unsigned)o[0] & 15u;
        int w = 0;
        unsigned pen = it->pen;
        fx_vec_t sh = { 0, 0, 0 };
        const int32_t *pr = o;
        int np = nops;
        if (it->op == 14) {
            const fx_vec_t *a = nops >= 1 ? dsl(d, o[0]) : NULL, *b = nops >= 1 ? dsl(d, o[0] + 1) : NULL;
            if (!a || !b) return RC_OK;
            sh = vdiff(*b, *a); pr = o + 1; np = nops - 1;
        } else {
            fs_set(d, 0xFFFF, -1, -1);
            if (it->op == 75) { pen = (unsigned)o[0] & 15; pr = o + 1; np = nops - 1; }   /* raw: colour, then v1/v2 pairs in bytes, bit 15 on the last v2 */
        }
        for (int k = 0; k + 1 < np; k += 2) {
            int32_t s0 = pr[k], s1 = pr[k + 1];
            int last = 0;
            if (it->op == 75) { last = (s1 & 0x8000) != 0; s0 = (s0 & 0x7FFF) / 6; s1 = (s1 & 0x7FFF) / 6; }
            const fx_vec_t *a = dsl(d, s0), *b = dsl(d, s1);
            if (a && b) {
                if (it->op == 14) { fx_vec_t pa = vdiff(*a, sh), pb = vdiff(*b, sh); w |= dline(d, &pa, &pb, pen); }
                else w |= dline_pair(d, a, b, pen);
            }
            if (last) break;
        }
        d->word = w;
        return RC_OK;
    }
    case 11: case 12: {   /* N segments (at least one) from slot s to s+1, two slots further per segment; op 12 with both end points reduced by V[vref+1] - V[vref]; no pair is skipped */
        int w = 0;
        fx_vec_t sh = { 0, 0, 0 };
        int32_t cnt, first;
        if (it->op == 12) {
            if (nops < 3) return RC_OK;
            const fx_vec_t *a = dsl(d, o[1]), *b = dsl(d, o[1] + 1);
            if (!a || !b) return RC_OK;
            sh = vdiff(*b, *a); cnt = o[0]; first = o[2];
        } else { if (nops < 2) return RC_OK; cnt = o[0]; first = o[1]; }
        if (cnt < 1) cnt = 1;
        for (int32_t k = 0; k < cnt; k++) {   /* cnt is bits 8-15 of the first word: at most 255 segments */
            const fx_vec_t *a = dsl(d, first + 2 * k), *b = dsl(d, first + 2 * k + 1);
            if (!a || !b) continue;
            fx_vec_t pa = *a, pb = *b;
            if (it->op == 12) { pa = vdiff(pa, sh); pb = vdiff(pb, sh); }
            w |= dline(d, &pa, &pb, it->pen);
        }
        d->word = w;
        return RC_OK;
    }
    case 6: {   /* rungs from A1 + k stepA to B1 + k stepB, k = 1..N-1: per axis the end vertices as longs shifted left by 4, the step the signed difference divided by N (a 16-bit
                 * divide; an overflowing quotient leaves the low word of the difference), each coordinate rounded back as (v + 8) >> 4; the clipped line, the OR of the rungs.
                 * N below 2 draws nothing (port ruling below; note 03, "Line operations 6, 11, 12, 14") */
        if (nops < 5 || !slots_get(d, o + 1, 4, in)) return RC_OK;
        const int N = (int16_t)o[0];
        /* N below 2 is undefined behaviour of the original (N = 0 divides by zero, N = 1 and every negative N overwrite up to 1.5 MB above 0x4FE3C; no shipped item has it, the
         * 12 uses are N = 3): PORT RULING of note 03, nothing is drawn and the word is 0, counted. For N of 258 or more the original's rung 257 reads a record that the
         * previous rung's clipper input has overwritten (note 03): unreachable in the data, so every rung here follows the formula. */
        if (N < 2) { g_op6_small++; return RC_OK; }
        const int R = N - 1;
        const int32_t A1[3] = { in[0].x, in[0].y, in[0].z }, A2[3] = { in[1].x, in[1].y, in[1].z }, B1[3] = { in[2].x, in[2].y, in[2].z }, B2[3] = { in[3].x, in[3].y, in[3].z };
        int sa[3], sb[3];
        for (int a = 0; a < 3; a++) { sa[a] = lp_div((A2[a] - A1[a]) * 16, N); sb[a] = lp_div((B2[a] - B1[a]) * 16, N); }
        int w = 0;
        for (int k = 1; k <= R; k++) {
            fx_vec_t p, q;
            int32_t pv[3], qv[3];
            for (int a = 0; a < 3; a++) {
                pv[a] = wrap16((((int64_t)A1[a] * 16 + (int64_t)k * sa[a]) + 8) >> 4);
                qv[a] = wrap16((((int64_t)B1[a] * 16 + (int64_t)k * sb[a]) + 8) >> 4);
            }
            p.x = pv[0]; p.y = pv[1]; p.z = pv[2]; q.x = qv[0]; q.y = qv[1]; q.z = qv[2];
            w |= dline(d, &p, &q, it->pen);
        }
        d->word = w;
        return RC_OK;
    }
    case 3: {
        if (nops < 1 || o[0] < 1 || o[0] > WORLD_POLY_MAX || o[0] + 1 > nops) return RC_OK;
        unsigned n = (unsigned)o[0];
        int16_t plane[6];
        int have = (it->flags & IF_PLANE) && (unsigned)nops >= n + 7;
        for (int i = 0; have && i < 6; i++) plane[i] = (int16_t)o[1 + n + (unsigned)i];
        if (!slots_get(d, o + 1, (int)n, in)) return RC_OK;
        d->pshift = 0;   /* op 3 clears the shift word at entry */
        return poly_op3(d, in, n, n, it->w, it->wback, it->flags, have ? plane : NULL);
    }
    case 69: {   /* the quad A, B, C, A - B + C, then the tail of op 3 */
        if (nops < 3 || !slots_get(d, o, 3, in)) return RC_OK;
        in[3] = vaddsub(in[0], in[1], in[2]);
        d->pshift = 0; d->pcount = 4;   /* op 69 stores shift 0 and count 4 at entry (V0 to V2 before the all-behind test, V3 after it: note 03, rig-verified) */
        return poly_op3(d, in, 4, 3, it->w, it->wback, it->flags, NULL);
    }
    case 55: {
        d->pshift = 0;   /* op 55 clears the shift word once; each polygon writes its vertices (and its count after its all-behind test) */
        uint32_t pos = 1;
        int32_t cnt = nops ? o[0] : 0;
        int w = 0;
        for (int32_t k = 0; k < cnt; k++) {
            if (pos + 4 > (uint32_t)nops) break;
            unsigned W = (unsigned)o[pos] & 0xFFFF, wb = (unsigned)o[pos + 1] & 0xFFFF, fl = (unsigned)o[pos + 2] & 0xFFFF;
            int32_t n = o[pos + 3];
            pos += 4;
            if (n < 1 || n > WORLD_POLY_MAX || pos + (uint32_t)n > (uint32_t)nops) break;
            const int32_t *sl = o + pos;
            pos += (uint32_t)n;
            int16_t plane[6];
            int have = 0;
            if (fl & IF_PLANE) {
                if (pos + 6 > (uint32_t)nops) break;
                for (int i = 0; i < 6; i++) plane[i] = (int16_t)o[pos + (uint32_t)i];
                pos += 6; have = 1;
            }
            if (!slots_get(d, sl, n, in)) continue;
            int rc = poly_op3(d, in, (unsigned)n, (unsigned)n, W, wb, fl, have ? plane : NULL);
            if (!rc) w |= d->word;   /* a rejected polygon (all behind, face away: op 3's -1) adds nothing and the list goes on (rig, test_draw_diff.c: the word is the OR of the results) */
        }
        d->word = w;
        return RC_OK;
    }
    case 4: {   /* the detail faces (note 03, op 4): returns 1 whatever the clipper returned, -1 above 5,120 ft */
        if (d->o && d->o->op4_neg) { d->word = -1; return RC_NEG; }
        d->word = 1;
        if (d->o && d->o->op4_closed) return RC_OK;
        uint32_t pos = 1;
        int32_t cnt = nops ? o[0] : 0;
        /* the record's altitude long +0x18 itself (04a section 8a: V:4AB1E serves the gate steps, +0x18 only this compare), not the entry's LOD-quantised position */
        const int64_t alt = (d->o && d->l && d->o->kind == GAME_ENTRY_ENTITY) ? (int64_t)d->l->ent[d->o->id & 15].pos[1] : (d->o ? d->o->pos.y : 0);
        /* the first distance word times 256 must be at least +0x18, else the handler returns 1 at once; that word is the first face's distance and is not tested again */
        if (cnt > 0 && nops >= 2 && (int64_t)(o[1] & 0x7FFF) * 256 < alt) return RC_OK;
        for (int32_t k = 0; k < cnt; k++) {
            if (pos + 3 > (uint32_t)nops) break;
            int32_t dd = o[pos], n = o[pos + 1], fl = o[pos + 2];
            pos += 3;
            if (n < 1 || n > WORLD_POLY_MAX || pos + (uint32_t)n > (uint32_t)nops) break;
            const int32_t *sl = o + pos;
            pos += (uint32_t)n;
            /* each later face is drawn only if its distance word is at least the record's long +0x10 (y >> 8); the first face that fails ends the list */
            if (k > 0 && (int64_t)(dd & 0x7FFF) < (alt >> 8)) break;
            if (!slots_get(d, sl, n, in)) continue;
            if (fl != 0 || n < 3 || fx_visible(&in[0], &in[1], &in[2], 0)) dpoly(d, in, n, 0);   /* an away face with a zero flag word is skipped and the list goes on */
        }
        return RC_OK;
    }
    case 24: case 25: case 26: {   /* polygon through listed slots (24), slots 0..N-1 (25), N consecutive slots from v (26, pen 7, stipple 3,3) */
        if (nops < 1 || o[0] < 1 || o[0] > WORLD_POLY_MAX) return RC_OK;
        int n = o[0];
        int32_t sl[WORLD_POLY_MAX];
        for (int i = 0; i < n; i++) {
            if (it->op == 24) { if (i + 1 >= nops) return RC_OK; sl[i] = o[1 + i]; }
            else if (it->op == 25) sl[i] = i;
            else { if (nops < 2) return RC_OK; sl[i] = o[1] + i; }
        }
        if (!slots_get(d, sl, n, in)) return RC_OK;
        pbuf_write(d, in, n);   /* the vertices are copied to the polygon buffer before the early-outs below (test_draw_diff.c) */
        d->pshift = 0; d->pcount = (int16_t)n;   /* ops 24, 25 and 26 store shift 0 and the count with the vertices, before their early-outs */
        if (it->op == 26) {
            /* QUIRK (test_draw_diff.c, 30,000 calls): the early-out "every listed depth is behind" of op 26 looks at N + 1 slots, the polygon's and the one after it; it returns 0 with
             * non-negative condition codes and writes nothing. Otherwise the polygon goes through the clipper in the fill state 3/3, then the solid defaults are restored (not the state before
             * the op), the word is 1 when the filler was reached, and the condition codes are negative. A missing slot after the list counts as a depth that is not behind. */
            const fx_vec_t *ext = dsl(d, (int64_t)o[1] + n);
            if (ext && n < 7) d->pbuf[n] = (int16_t)ext->z;   /* op 26 writes count + 1 vertices: the slot after the list as well (note 03, "Op 9 reads past its quad") */
            if (ext && ext->z < 0 && all_behind(in, n)) return RC_OK;
            fs_set(d, 3, 3, -1);
            d->cur_pen = it->pen & 15u;
            d->word = dpoly(d, in, n, it->pen);
            fs_set(d, 0xF, -1, -1);
            return RC_CCNEG;
        }
        if (all_behind(in, n)) return RC_OK;   /* every depth behind: 0 */
        d->word = dpoly(d, in, n, it->pen);
        return RC_OK;
    }
    case 9: case 10: case 27: case 30: {   /* the quad A, B, C, A - B + C (ops 9, 27, 30), or D, D + (B - A), D + (C - A), D + (C - B) (op 10) */
        if (nops < 1) return RC_OK;
        if (it->op == 9 || it->op == 10) { if (sc->off_y < -0x80) return RC_OK; }   /* bridges: only up to 160 ft */
        int cnt = it->op == 10 ? 4 : 3;
        int32_t sl[4] = { o[0], o[0] + 1, o[0] + 2, o[0] + 3 };
        if (!slots_get(d, sl, cnt, in)) return RC_OK;
        fx_vec_t q[4];
        if (it->op == 10) {
            q[0] = in[3]; q[1] = vsum(in[3], vdiff(in[1], in[0])); q[2] = vsum(in[3], vdiff(in[2], in[0])); q[3] = vsum(in[3], vdiff(in[2], in[1]));
        } else { q[0] = in[0]; q[1] = in[1]; q[2] = in[2]; q[3] = vaddsub(in[0], in[1], in[2]); }
        pbuf_build(d, q, 4);
        if (it->op == 9) {
            /* Op 9 builds V0..V3 in the polygon buffer and then tests "all depths behind" by ANDing the depth of V3 with the depth words at the buffer positions 4 to 6, which are residue of earlier polygons
             * (note 03, "Op 9 reads past its quad"); it drops the quad exactly when that AND is negative, and V0 to V2 are not tested at all. */
            if ((d->pbuf[3] & d->pbuf[4] & d->pbuf[5] & d->pbuf[6]) < 0) { d->word = 0; return RC_OK; }
        }
        if (it->op == 27) fs_set(d, 2, 0, 2);          /* stipple (2, 0, 2, 0): plane 1 toggled */
        else if (it->op == 30) fs_set(d, 8, 8, 0);     /* stipple (8, 8, 0, 0): plane 3 set */
        d->word = (it->op == 27 || it->op == 30) && all_behind(q, 4) ? 0   /* the early-out sees the derived fourth depth too (test_draw_diff.c) */ : dpoly(d, q, 4, it->pen);
        return RC_OK;
    }
    case 15: case 16: {
        fx_vec_t q[4];
        if (it->op == 15) {   /* quad V[b], V[c], V[e], V[e] - (V[a+6] - V[a]) */
            if (nops < 4) return RC_OK;
            const fx_vec_t *a0 = dsl(d, o[0]), *a1 = dsl(d, o[0] + 1), *b = dsl(d, o[1]), *cc = dsl(d, o[2]), *e = dsl(d, o[3]);
            if (!a0 || !a1 || !b || !cc || !e) return RC_OK;
            q[0] = *b; q[1] = *cc; q[2] = *e; q[3] = vdiff(*e, vdiff(*a1, *a0));
        } else {   /* quad V[a], V[a] - d, V[c] - d, V[c], d = V[b+6] - V[b] */
            if (nops < 3) return RC_OK;
            const fx_vec_t *a = dsl(d, o[0]), *b0 = dsl(d, o[1]), *b1 = dsl(d, o[1] + 1), *cc = dsl(d, o[2]);
            if (!a || !b0 || !b1 || !cc) return RC_OK;
            fx_vec_t dd = vdiff(*b1, *b0);
            q[0] = *a; q[1] = vdiff(*a, dd); q[2] = vdiff(*cc, dd); q[3] = *cc;
        }
        pbuf_build(d, q, 4);
        d->word = all_behind(q, 4) ? 0 : dpoly(d, q, 4, it->pen);
        return RC_OK;
    }
    case 17: case 18: {   /* quads of four slots, pen 12, mask 8 */
        int w = 0;
        fs_set(d, 8, 8, 0);   /* mask 8, pattern 8: plane 3 set */
        for (int k = 0; k + 4 <= nops; k += 4) {
            if (!slots_get(d, o + k, 4, in)) continue;
            w |= dpoly(d, in, 4, 12);
        }
        d->word = w;
        return RC_OK;
    }
    case 7: case 8: {   /* the bridge faces: raw operands (pen, [c,] a, b|flip, [f,] d); c and f are slot byte offsets, a and b byte offsets of 3D block vertices (10 + 6 i), note 03 "Ops 7 and 8" */
        int op8 = it->op == 8;
        if (nops < (op8 ? 6 : 5)) return RC_OK;
        unsigned pen = (unsigned)o[0] & 15;
        int32_t wa, wbv, wc, wd, wf = 0;
        if (!op8) { wa = o[1]; wbv = o[2]; wc = o[3]; wd = o[4]; } else { wc = o[1]; wa = o[2]; wbv = o[3]; wf = o[4]; wd = o[5]; }
        int flip = (wbv >> 15) & 1;
        int ia = (int)((wa & 0xFFFF) - 10) / 6, ib = (int)((wbv & 0x7FFF) - 10) / 6;
        if (ia < 0 || ib < 0 || (uint32_t)ia >= d->lv->vcount || (uint32_t)ib >= d->lv->vcount) return RC_OK;
        const vtx_t *va = &d->m->vt[d->lv->vfirst + (uint32_t)ia], *vb = &d->m->vt[d->lv->vfirst + (uint32_t)ib];
        const int L = d->lv->scale_l;
        int16_t ex = d->o ? d->o->x : 0, ez = d->o ? d->o->z : 0;
        int16_t tx = wrap16((int64_t)((int16_t)va->x >> L) + wrap16((int64_t)ex * (1 << d->lod)) + sc->off_x);
        int16_t tz = wrap16((int64_t)((int16_t)va->z >> L) + wrap16((int64_t)ez * (1 << d->lod)) + sc->off_z);
        int32_t T = (int32_t)tx * wrap16((int64_t)va->x - vb->x) + (int32_t)tz * wrap16((int64_t)va->z - vb->z);
        int varB = ((T >= 0) ? 1 : 0) == flip;
        int base = (wd & 0xFFFF) / 6;
        fx_vec_t t[4];
        const fx_vec_t *s0 = dsl(d, base), *s1 = dsl(d, base + 1), *s2 = dsl(d, base + 2);
        if (!s0 || !s1 || !s2) return RC_OK;
        if (!op8) {
            const fx_vec_t *sc_ = dsl(d, (wc & 0xFFFF) / 6);
            if (!sc_) return RC_OK;
            t[0] = *sc_;
            if (!varB) { t[1] = *s0; t[2] = *s2; } else { t[1] = *s1; t[2] = vaddsub(*s0, *s1, *s2); }
        } else {
            const fx_vec_t *c0 = dsl(d, (wc & 0xFFFF) / 6), *c1 = dsl(d, (wc & 0xFFFF) / 6 + 1), *sf = dsl(d, (wf & 0xFFFF) / 6), *s3 = dsl(d, base + 3);
            if (!c0 || !c1 || !sf || !s3) return RC_OK;
            fx_vec_t p = vdiff(*sf, vdiff(*c1, *c0)), e1 = vdiff(*s1, *s0), e2 = vdiff(*s2, *s1);
            t[0] = p;
            if (!varB) { t[1] = *s3; t[2] = vsum(vsum(*s3, e1), e2); } else { t[1] = vsum(*s3, e1); t[2] = vsum(*s3, e2); }
        }
        pbuf_build(d, t, 3);
        d->word = all_behind(t, 3) ? 0 : dpoly(d, t, 3, pen);
        return RC_OK;
    }
    case 39: case 40: {   /* quad / triangle on the absolute slots 0 to 4 */
        const fx_vec_t *s[5];
        for (int i = 0; i < 5; i++) s[i] = dsl(d, i);
        if (!s[1]) return RC_OK;
        if (!inside_closed(s[1], 2)) { d->word = -1; return RC_NEG; }
        int16_t ex = d->o ? d->o->x : 0, ez = d->o ? d->o->z : 0;
        int32_t dx = -(int32_t)sc->off_x - (int32_t)wrap16((int64_t)ex * (1 << d->lod));
        int32_t dz = -(int32_t)sc->off_z - (int32_t)wrap16((int64_t)ez * (1 << d->lod));
        int varB = (dx >= 0) != (dz >= 0);
        int32_t adx = dx < 0 ? -dx : dx, adz = dz < 0 ? -dz : dz;
        unsigned pen = (unsigned)(adz <= adx ? o[0] : o[1]) & 15;
        if (it->op == 40) {
            if (nops < 2) return RC_OK;
            fx_vec_t t[3];
            if (!s[0] || !s[varB ? 2 : 4] || (varB && !s[3])) return RC_OK;
            if (!varB) { t[0] = *s[0]; t[1] = *s[1]; t[2] = *s[4]; } else { t[0] = *s[0]; t[1] = *s[2]; t[2] = *s[3]; }
            d->word = dpoly(d, t, 3, pen);
            return RC_OK;
        }
        if (nops < 3 || !s[0] || !s[2] || !s[3]) return RC_OK;
        fx_vec_t q[4];
        if (!varB) { q[0] = *s[0]; q[1] = *s[1]; q[2] = vaddsub(*s[2], *s[1], *s[3]); q[3] = vsum(vaddsub(*s[0], *s[1], *s[2]), vdiff(*s[3], *s[1])); }
        else { q[0] = *s[2]; q[1] = *s[3]; q[2] = vaddsub(*s[3], *s[1], *s[0]); q[3] = vaddsub(*s[2], *s[1], *s[0]); }
        int w = dpoly(d, q, 4, pen);
        if (sc->gate) {   /* V:4A83E: the second quad, pen the extra operand word */
            fx_vec_t r[4];
            r[0] = *s[0]; r[1] = vaddsub(*s[0], *s[1], *s[2]); r[2] = vsum(vaddsub(*s[0], *s[1], *s[2]), vdiff(*s[3], *s[1])); r[3] = vaddsub(*s[0], *s[1], *s[3]);
            w |= dpoly(d, r, 4, (unsigned)o[2] & 15);
        }
        d->word = w;
        return RC_OK;
    }
    case 31: {   /* the size and visibility early-out: the pre-test of rule 6 against the first two vertices of the block at the operand */
        if (nops >= 1 && box_pretest(d, (unsigned)o[0])) { d->word = -1; return RC_NEG; }
        return RC_OK;
    }
    case 52: {   /* vertex synthesis in camera space (note 03): d = S[B] - S[A]; slot T+3 = S[T] + d, T+4 = S[T+1] + h(d), T+5 = S[T+2] + h(d); the stream skip is next_item's */
        if (nops < 3) return RC_OK;
        const fx_vec_t *a = dsl(d, o[0]), *b = dsl(d, o[1]);
        if (!a || !b) return RC_OK;
        const fx_vec_t dv = vdiff(*b, *a), hd = vhalf(dv);
        for (int i = 0; i < 3; i++) { const fx_vec_t *src = dsl(d, (int64_t)o[2] + i); if (src) synth_put(d, (int64_t)o[2] + 3 + i, vsum(*src, i ? hd : dv)); }
        return RC_OK;
    }
    case 53: {   /* d_j = S[B+j] - S[A], slot A + 30 + 5 j + (k - 1) = S[A+k] + d_j for k = 1 to 5 (fifteen slots); the stream skip is next_item's (the third operand) */
        if (nops < 3) return RC_OK;
        const fx_vec_t *a = dsl(d, o[0]);
        if (!a) return RC_OK;
        for (int j = 0; j < 3; j++) {
            const fx_vec_t *bj = dsl(d, (int64_t)o[1] + j);
            if (!bj) continue;
            const fx_vec_t dj = vdiff(*bj, *a);
            for (int k = 1; k <= 5; k++) { const fx_vec_t *src = dsl(d, (int64_t)o[0] + k); if (src) synth_put(d, (int64_t)o[0] + 30 + 5 * j + (k - 1), vsum(*src, dj)); }
        }
        return RC_OK;
    }
    case 19: d->word = hatch_op19(d, o, nops, it->pen); return RC_OK;
    case 20: case 32: d->word = hatch_op20_32(d, it, o, nops); return RC_OK;
    case 21: case 57: d->word = hatch_op21_57(d, it, o, nops); return RC_OK;
    case 22: d->word = hatch_op22(d, it, o, nops); return RC_OK;
    case 23: d->word = hatch_op23(d, o, nops); return RC_OK;
    case 42: case 43: case 44: case 59: case 56: case 63: case 64: case 65:
        /* The vertex synthesis of the polygons' slots is the extractor's (the state-zero path); these operations also run their fragment on the object record's own slot array (+0xA4, note 03
         * "Vertex synthesis" and op 56, 63 to 65): the walk's caller applies it to the display lists' slots, in the order the program ran them. 56 and 63 to 65 have no operand (base 0). */
        if (d->rs_n < WORLD_RS_MAX) { d->rs_op[d->rs_n] = (uint8_t)it->op; d->rs_base[d->rs_n] = (it->op >= 56 && it->op != 59) ? 0 : (nops >= 1 ? (int16_t)o[0] : 0); d->rs_n++; }
        return RC_OK;
    default:
        return RC_OK;   /* selectors, vertex synthesis (done by the extractor for the state-zero path) and ops that draw nothing return 0 */
    }
}

/* The result codes of a handler: 0, 1 negative condition codes only (a rejected point, op 26), 2 negative word (and condition codes). */
/* Everything below the draw_item call is the PREVIEW geometry walk (world_model_polys: slot lists of the primitives for the model dump and the synthetic tests; no word arithmetic). */
static int run_item(walk_t *c, const item_t *it) {
    if (c->dr) return draw_item(c, it);
    const int32_t *o = c->m->op + it->opfirst;
    int32_t s[WORLD_POLY_MAX];
    int16_t plane[6];
    switch (it->op) {
    case 3: {   /* n, n slots, [6 words when flag 32] */
        if (it->nops < 1 || o[0] < 1 || o[0] > WORLD_POLY_MAX || (uint32_t)o[0] + 1 > it->nops) return 0;
        unsigned n = (unsigned)o[0];
        for (unsigned i = 0; i < n; i++) s[i] = o[1 + i];
        int have = (it->flags & IF_PLANE) && it->nops >= n + 7;
        for (int i = 0; have && i < 6; i++) plane[i] = (int16_t)o[1 + n + i];
        return oriented(c, s, n, it->w, it->wback, it->flags, have ? plane : NULL);
    }
    case 55: {  /* count, then per polygon: W, wback, flags, n, n slots, [6 words] */
        uint32_t pos = 1;
        int32_t cnt = it->nops ? o[0] : 0;
        for (int32_t k = 0; k < cnt; k++) {
            if (pos + 4 > it->nops) return 0;
            unsigned w = (unsigned)o[pos] & 0xFFFF, wb = (unsigned)o[pos + 1] & 0xFFFF, fl = (unsigned)o[pos + 2] & 0xFFFF;
            int32_t n = o[pos + 3];
            pos += 4;
            if (n < 1 || n > WORLD_POLY_MAX || pos + (uint32_t)n > it->nops) return 0;
            for (int32_t i = 0; i < n; i++) s[i] = o[pos + (uint32_t)i];
            pos += (uint32_t)n;
            int have = 0;
            if (fl & IF_PLANE) {
                if (pos + 6 > it->nops) return 0;
                for (int i = 0; i < 6; i++) plane[i] = (int16_t)o[pos + (uint32_t)i];
                pos += 6; have = 1;
            }
            if (oriented(c, s, (unsigned)n, w, wb, fl, have ? plane : NULL)) return 2;
        }
        return 0;
    }
    case 4: {   /* faces: count, then per face: distance, n, flag word, n slots */
        if (c->op4_closed) {   /* the handler returns 1 at its gate exit without drawing (note 04a P7) */
            if (c->n < c->max) { world_poly_t *p = &c->out[c->n++]; memset(p, 0, sizeof *p); p->kind = WORLD_PRIM_ONE; p->shift_slot = -1; }
            return 0;
        }
        uint32_t pos = 1;
        int32_t cnt = it->nops ? o[0] : 0;
        /* note 02 section 5: the first distance word also gates the whole list */
        if (cnt > 0 && it->nops >= 2 && c->dist > (int32_t)(((uint32_t)o[1] & 0x7FFF) >> c->scale)) return 0;
        for (int32_t k = 0; k < cnt; k++) {
            if (pos + 3 > it->nops) return 0;
            int32_t d = o[pos], n = o[pos + 1], fl = o[pos + 2];
            pos += 3;
            if (n < 1 || n > WORLD_POLY_MAX || pos + (uint32_t)n > it->nops) return 0;
            for (int32_t i = 0; i < n; i++) s[i] = o[pos + (uint32_t)i];
            pos += (uint32_t)n;
            if (c->dist > (int32_t)(((uint32_t)d & 0x7FFF) >> c->scale)) continue;
            int ok = 1;
            for (int32_t i = 0; i < n; i++) if (!vtx(c, s[i])) ok = 0;
            if (!ok) continue;
            if (fl != 0 || faces_viewer(c, s, (unsigned)n, 0, NULL)) emit(c, s, (unsigned)n, 0, 0);
        }
        return 0;
    }
    case 24: {  /* n, n slots; colour in pen; no face test */
        if (it->nops < 1 || o[0] < 1 || o[0] > WORLD_POLY_MAX || (uint32_t)o[0] + 1 > it->nops) return 0;
        unsigned n = (unsigned)o[0];
        for (unsigned i = 0; i < n; i++) { s[i] = o[1 + i]; if (!vtx(c, s[i])) return 0; }
        emit(c, s, n, it->pen, 0);
        return 0;
    }
    case 25: case 26: {   /* 25: slots 0..n-1; 26: n consecutive slots from v */
        if (it->nops < 1 || o[0] < 1 || o[0] > WORLD_POLY_MAX) return 0;
        unsigned n = (unsigned)o[0];
        int32_t first = it->op == 26 ? (it->nops >= 2 ? o[1] : -1) : 0;
        for (unsigned i = 0; i < n; i++) { s[i] = first + (int32_t)i; if (!vtx(c, s[i])) return 0; }
        emit(c, s, n, it->pen, 0);
        return 0;
    }
    case 9: case 27: case 30: case 69: {   /* three slots, fourth derived: A - B + C */
        if (it->op == 69 ? it->nops < 3 : it->nops < 1) return 0;
        if (it->op == 69) { s[0] = o[0]; s[1] = o[1]; s[2] = o[2]; }
        else { s[0] = o[0]; s[1] = o[0] + 1; s[2] = o[0] + 2; }
        for (int i = 0; i < 3; i++) if (!vtx(c, s[i])) return 0;
        s[3] = 0;
        emit(c, s, 4, it->pen, 1);
        return 0;
    }
    case 17: case 18: {   /* quads of four slots, fixed colour 12 (carried in pen) */
        for (uint32_t k = 0; k + 4 <= it->nops; k += 4) {
            int ok = 1;
            for (int i = 0; i < 4; i++) { s[i] = o[k + (uint32_t)i]; if (!vtx(c, s[i])) ok = 0; }
            if (ok) emit(c, s, 4, it->pen, 0);
        }
        return 0;
    }
    case 39: {   /* a quad on the absolute slots 0 to 3: S0, S1, S2 + S3 - S1, S0 - 2 S1 + S2 + S3 (note 02 section 5; the pen is inferred) */
        for (int i = 0; i < 4; i++) { s[i] = i; if (!vtx(c, s[i])) return 0; }
        emit(c, s, 4, it->nops ? (unsigned)o[0] : 0u, 2);
        return 0;
    }
    case 40: {   /* a triangle on the absolute slots 0, 1 and 4 */
        s[0] = 0; s[1] = 1; s[2] = 4;
        for (int i = 0; i < 3; i++) if (!vtx(c, s[i])) return 0;
        emit(c, s, 3, it->nops ? (unsigned)o[0] : 0u, 0);
        return 0;
    }
    case 1: case 58:   /* a point */
        if (it->nops >= 1) { s[0] = o[0]; emit_prim(c, WORLD_PRIM_POINT, s, 1, it->pen, -1); }
        return 0;
    case 2: case 48: case 74:   /* a line */
        if (it->nops >= 2) { s[0] = o[0]; s[1] = o[1]; emit_prim(c, WORLD_PRIM_LINE, s, 2, it->pen, -1); }
        return 0;
    case 13: case 14: {   /* a list of lines (op 14: shifted by the reference) */
        uint32_t first = it->op == 14 ? 1 : 0;
        int shift = it->op == 14 && it->nops >= 1 ? o[0] : -1;
        for (uint32_t k = first; k + 2 <= it->nops; k += 2) { s[0] = o[k]; s[1] = o[k + 1]; emit_prim(c, WORLD_PRIM_LINE, s, 2, it->pen, shift); }
        return 0;
    }
    case 11: case 12: {   /* count segments from slot s, two slots per segment (op 12: shifted) */
        int shift = it->op == 12 && it->nops >= 3 ? o[1] : -1;
        uint32_t si = it->op == 12 ? 2 : 1;
        if (it->nops <= si) return 0;
        int32_t cnt = o[0], first = o[si];
        for (int32_t k = 0; k < cnt && k < 64; k++) { s[0] = first + 2 * k; s[1] = first + 2 * k + 1; emit_prim(c, WORLD_PRIM_LINE, s, 2, it->pen, shift); }
        return 0;
    }
    default:
        return 0;   /* not a primitive of slot indices */
    }
}

/* Selector state (README, "Skip lists"): the index into an item's skip list. 0 is the state-zero
 * choice; milestone 2 always passes 0. An index beyond the list falls back to entry 0. */
#define SELECTOR_STATE_ZERO 0

/* The item that follows `it` in the chosen path: address + length + skip (README), found among
 * the items of the program. Returns NULL when the path leaves the program. This is the one
 * place that knows about selectors. */
static const item_t *next_item(const walk_t *c, const prog_t *pg, const item_t *it, unsigned state) {
    int64_t skip = 0;
    unsigned idx = state;
    if (it->op == 67 && it->nops >= 3) {
        /* op 67 is a face-visibility selector (note 02 section 5): the plane test of section 6 on its
         * three vertices with W; when the face points away the next 18 bytes are skipped (entry 1) */
        int32_t s3[3] = { c->m->op[it->opfirst], c->m->op[it->opfirst + 1], c->m->op[it->opfirst + 2] };
        if (c->dr) {   /* the draw layer: the three-vertex test on the camera-space words (0x22A7A) */
            fx_vec_t v3[3];
            idx = slots_get(c->dr, s3, 3, v3) ? (fx_visible(&v3[0], &v3[1], &v3[2], (uint16_t)it->w) ? 0u : 1u) : 0u;
        } else idx = faces_viewer(c, s3, 3, it->w, NULL) ? 0u : 1u;   /* PREVIEW branch (no draw layer) */
    }
    if (it->skip_count) skip = c->m->sk[it->skip_first + (idx < it->skip_count ? idx : 0)];
    /* Op 54 (note 03, the state selectors) with n = ((byte +0x7C) & 0x7F) >> 4: at n < 6 it skips the 2 bytes of the op 0 item after it; at n >= 6 it skips nothing, so that the op 0 item runs, which carries
     * the last-operation flag (bit 14): it ends the program (run_program: IF_LAST), and the interpreter resumes at the next program of the item's list, the body. The shipped programs are exactly
     * [54, 0 (last), 52, 53 x 6, 55, 13 (last)], so at n >= 6 the op 52 item, the op 53 items, the gear polygons (55) and the gear lines (13) are not executed (the slots they write are never read).
     * Op 52 advances by 14 bytes times n, so that the n-th of the six op 53 items is the one that synthesises, and op 53 skips the signed word of its third operand (skip entry 0 of the extractor's list).
     * Without the draw layer (the geometry preview) the state is 0. */
    if (c->dr && (it->op == 54 || it->op == 52)) {
        const unsigned n = (c->dr->gear & 0x7Fu) >> 4;
        if (it->op == 54) skip = n < 6 ? 2 : 0;
        else skip = 14 * (int64_t)n;
    }
    /* Op 70 (note 07 11.3): the timer skip of the effect models. t is the life word of the object being drawn (the word at +0x04 of the secondary object of the drawn record's index 0x4D23C + (V:4AA6C << 5), 07 11.3; scene t04). A negative word skips nothing, so the
     * empty item that follows ends the program (nothing is drawn); otherwise 2 bytes (the empty item) plus 0x86 per variant k = (t & 7) >> 1, plus 0x26 and 8 per step of (0x0B - t) >> 1 below 0x0C, plus
     * 0x56 and 8 per step of (t - 0x74) >> 1 above 0x74. For t of 0x80 and above (reachable only after 32,896 passes, about 68 minutes) the picture of the original is nothing, no scar either (V: the emulator, an object at 100 du with the life word 0x7FFF, 0x80, 0x8C, 0x9C, 0xA0 and 0x100 draws nothing; 0x7F and 0x40 draw the starburst), and the port draws nothing (no skip) [V for the table and the picture, note 07 11.3].
     * Without the draw layer (the geometry preview) the first entry of the skip list, the steady picture's first variant, is followed. */
    if (c->dr && it->op == 70) {
        const int t = c->dr->o ? (int)c->dr->o->t04 : -1;
        if (t < 0 || t >= 0x80) skip = 0;
        else {
            skip = 2 + 0x86 * (int64_t)((t & 7) >> 1);
            if (t < 0x0C) skip += 0x26 + 8 * (int64_t)((0x0B - t) >> 1);
            if (t > 0x74) skip += 0x56 + 8 * (int64_t)((t - 0x74) >> 1);
        }
    }
    int64_t target = (int64_t)it->address + it->length + skip;
    for (uint32_t i = 0; i < pg->icount; i++) {
        const item_t *n = &c->m->it[pg->ifirst + i];
        if ((int64_t)n->address == target && n != it) return n;
    }
    return NULL;
}

/* Runs one program. A failed gate ends the program (note 04a P6, note 03 "Gate of an item"). A handler's result: the common drawer abandons the rest of the program on
 * negative condition codes or a negative word and adds nothing; the type B drawer tests the word only (a negative word aborts the whole drawer). Returns 0 (ran to its end or
 * ended by a gate), 1 (abandoned) or 2 (a type B negative word: abort). In the draw layer the handler words of the items that were not abandoned are counted (draw_t.polys). */
static int run_program(walk_t *c, const prog_t *pg, unsigned state) {
    const item_t *it = pg->icount ? &c->m->it[pg->ifirst] : NULL;
    for (uint32_t steps = 0; it && steps < pg->icount; steps++) {
        if (!(it->flags & (IF_BLOCKED | IF_SPECULATIVE))) {
            if (!gate_ok(c, it)) return 0;
            int r = run_item(c, it);
            draw_t *dr = c->dr;
            if (dr) dr->last_op = it->op;
            if (c->m->kind == 2) {
                if (r == 2) { if (dr) dr->last_abort_op = it->op; return 2; }
                if (dr && dr->word > 0) { dr->polys++; dr->last_pos_op = it->op; }
            } else {
                if (r) { if (dr) dr->last_abort_op = it->op; return 1; }
                if (dr && dr->word > 0) { dr->polys++; dr->last_pos_op = it->op; }
            }
        }
        if (it->flags & IF_LAST) return 0;   /* the path ends at the first last-flagged item */
        it = next_item(c, pg, it, state);
    }
    return 0;
}

int world_model_polys_ex(const world_t *w, unsigned id, int level, const world_view_t *view,
                         world_poly_t *out, int max, unsigned *end_flags);
int world_model_polys(const world_t *w, unsigned id, int level, const world_view_t *view,
                      world_poly_t *out, int max) {
    return world_model_polys_ex(w, id, level, view, out, max, NULL);
}

static int walk_model(const world_t *w, unsigned id, int level, const world_view_t *view,
                      world_poly_t *out, int max, unsigned *end_flags, draw_t *dr) {
    unsigned dummy;
    if (!end_flags) end_flags = &dummy;
    *end_flags = 0;
    const model_t *m = get_model(w, id);
    if (!m || !view || !out || max < 0 || level < 0 || (uint32_t)level >= m->nl) return -1;
    walk_t c;
    c.m = m; c.lv = &m->lv[level]; c.out = out; c.max = max; c.n = 0; c.dr = dr;
    c.scale = view->inst_scale & 15u;
    c.op4_closed = view->op4_closed;
    c.dist = view->has_gate ? (int32_t)view->gate_dist : view_dist(view);
    if (view->has_gate) c.scale = view->gate_lod;
    for (int i = 0; i < 3; i++) {
        int64_t e = view->eye[i];
        c.eye[i] = e > EYE_CLAMP ? EYE_CLAMP : (e < -EYE_CLAMP ? -EYE_CLAMP : e);
    }
    if (!c.lv->present) return 0;
    int32_t e = c.lv->root;
    for (uint32_t steps = 0; e >= 0 && steps <= m->ne && c.n < max; steps++) {
        const elem_t *el = &m->el[e];
        int first = 0;
        if (el->kind >= EL_AXIS && el->kind <= EL_THREE && (el->flags & EF_BLOCKED_NODE))
            return c.n;   /* node on a blocked slot: the subtree is not drawable */
        switch (el->kind) {
        case EL_JUMP:
            if (el->flags & EF_END0) *end_flags |= WORLD_WALK_END_FFFF;
            e = el->c0; continue;
        case EL_AXIS: {
            if (c.dr) { first = side_axis(c.dr, el); break; }   /* the draw layer: the word arithmetic of 0x22B3A (note 03) */
            /* PREVIEW (world_model_polys, the milestone-2 geometry walk of the model dump and the synthetic tests; not the original's arithmetic): the exact eye in model space against the
             * model-space vertex. The draw layer never takes this path. */
            const vtx_t *v = vtx(&c, el->s[0]);
            int64_t p = v ? vcoord(v, el->axis) : 0;
            int axis = el->axis == 1 ? 1 : el->axis == 2 ? 0 : 2;
            first = c.eye[axis] >= p;
            if (el->flags & 1) first = !first;
            break;
        }
        case EL_PLANE:
            first = c.dr ? side_plane(c.dr, el->s) : plane_value(&c, el->s) < 0;   /* PREVIEW when c.dr is NULL */
            break;
        case EL_THREE:
            first = c.dr ? side_three(c.dr, el) : three_value(&c, el->s[0], el->s[1], el->s[2]) < 0;   /* PREVIEW when c.dr is NULL */
            break;
        case EL_DRAW:
            for (uint32_t k = 0; k < el->lcount; k++) {
                if (run_program(&c, &m->pg[m->pl[el->lfirst + k]], SELECTOR_STATE_ZERO) == 2) {
                    /* NOTE-GAP / marker (note 04a 8b rule 9, read from the code only): in the type B drawer a negative handler return aborts the whole drawer with 0 */
                    *end_flags |= WORLD_WALK_ABORTED;
                    return c.n;
                }
            }
            if (el->flags & EF_END0) *end_flags |= WORLD_WALK_END_FFFF;
            e = el->c0;
            continue;
        default:
            return c.n;   /* unknown element: stop */
        }
        if (first ? (el->flags & EF_END0) : (el->flags & EF_END1)) *end_flags |= WORLD_WALK_END_FFFF;
        e = first ? el->c0 : el->c1;
    }
    return c.n;
}

int world_model_polys_ex(const world_t *w, unsigned id, int level, const world_view_t *view,
                         world_poly_t *out, int max, unsigned *end_flags) {
    return walk_model(w, id, level, view, out, max, end_flags, NULL);
}

/* ---- terrain queries -------------------------------------------------------------------- */

int world_cell_items(const world_t *w, unsigned table, unsigned cx, unsigned cz,
                     const world_item_t **items, unsigned *count, unsigned *cell_index) {
    if (!w || table >= MAX_TABLES) return 0;
    const table_t *tb = &w->table[table];
    if (cx >= tb->grid || cz >= tb->grid) return 0;   /* outside the grid: nothing is read */
    unsigned lo = 0, hi = tb->count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        const cell_t *c = &w->cell[tb->first + mid];
        if (c->z < cz || (c->z == cz && c->x < cx)) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= tb->count) return 0;
    const cell_t *c = &w->cell[tb->first + lo];
    if (c->x != cx || c->z != cz) return 0;
    const list_t *l = &w->list[c->list];
    if (items) *items = w->item + l->first;
    if (count) *count = l->count;
    if (cell_index) *cell_index = tb->first + lo;
    return l->count ? 1 : 0;
}

int world_position_entry(const world_t *w, unsigned entry, int32_t pos[3]) {
    if (!w || entry > MAX_ENTRY || !w->entry_ok[entry]) return -1;
    for (int i = 0; i < 3; i++) pos[i] = w->entry[entry][i];
    return 0;
}

int world_position_block(const world_t *w, unsigned entry, int16_t words[5]) {
    if (!w || entry > MAX_ENTRY || !w->entry_ok[entry]) return -1;
    for (int i = 0; i < 5; i++) words[i] = w->entry_words[entry][i];
    return 0;
}

const uint8_t *world_palette(const world_t *w) { return w ? w->palette : NULL; }

int world_camera_ring(const world_t *w, unsigned cls, unsigned index, int16_t xyz[3]) {
    if (!w || cls >= 4 || index >= RING_ENTRIES) return -1;
    for (int i = 0; i < 3; i++) xyz[i] = w->ring[cls][index][i];
    return 0;
}

/* ---- camera ----------------------------------------------------------------------------- */

#define ANGLE_CIRCLE 28800   /* note 03, "Angle unit": 28800 units per circle */
#define STICK_RANGE 32768    /* game.h: axes are -32768..32767 */
#define ALT_MAX (1 << 28)

void world_set_controls(world_t *w, int32_t stick_x, int32_t stick_y, int32_t throttle) {
    if (!w) return;
    if (stick_x < -STICK_RANGE) stick_x = -STICK_RANGE;
    if (stick_x > STICK_RANGE - 1) stick_x = STICK_RANGE - 1;
    if (stick_y < -STICK_RANGE) stick_y = -STICK_RANGE;
    if (stick_y > STICK_RANGE - 1) stick_y = STICK_RANGE - 1;
    if (throttle < -STICK_RANGE) throttle = -STICK_RANGE;
    if (throttle > STICK_RANGE - 1) throttle = STICK_RANGE - 1;
    w->in_x = stick_x; w->in_y = stick_y; w->in_throttle = throttle;
}

void world_set_camera(world_t *w, const world_camera_t *c) { if (w && c) w->cam = *c; }
const world_camera_t *world_camera(const world_t *w) { return w ? &w->cam : NULL; }

void world_start(world_t *w) {
    if (!w) return;
    memset(&w->cam, 0, sizeof w->cam);
    int32_t p[3] = {0, 0, 0};
    world_position_entry(w, WORLD_ENTRY_CARRIER, p);
    w->cam.pos[0] = p[0];
    w->cam.pos[1] = p[1] + FREECAM_START_ALT;
    w->cam.pos[2] = (int32_t)((uint32_t)(p[2] - FREECAM_START_BACK) & WORLD_POS_MASK);   /* behind the carrier, looking along the deck */
    w->in_x = w->in_y = 0;
    w->in_throttle = -STICK_RANGE;   /* idle */
    lists_mission_start(&w->lists);   /* the request byte 0xFF and V:4A90D = 0xFF (note 04a section 1) */
}

void world_freecam_step(world_t *w) {
    if (!w) return;
    world_camera_t *c = &w->cam;
    int32_t y = (int32_t)c->yaw + (int32_t)(((int64_t)w->in_x * FREECAM_YAW_RATE) / STICK_RANGE);
    y %= ANGLE_CIRCLE;
    if (y < 0) y += ANGLE_CIRCLE;
    c->yaw = (uint32_t)y;
    int32_t p = c->pitch + (int32_t)(((int64_t)w->in_y * FREECAM_PITCH_RATE) / STICK_RANGE);
    if (p > FREECAM_PITCH_MAX) p = FREECAM_PITCH_MAX;
    if (p < -FREECAM_PITCH_MAX) p = -FREECAM_PITCH_MAX;
    c->pitch = p;
    c->speed = (int32_t)(((int64_t)(w->in_throttle + STICK_RANGE) * FREECAM_MAX_SPEED) / (2 * STICK_RANGE));
    int64_t sy = fx_sin(c->yaw), cy = fx_cos(c->yaw);
    uint32_t pa = (uint32_t)(p + ANGLE_CIRCLE);
    int64_t sp = fx_sin(pa), cp = fx_cos(pa);
    int64_t horiz = ((int64_t)c->speed * cp) >> 14;                  /* Q14 products (note 03) */
    int64_t dx = -((horiz * sy) >> 14), dz = (horiz * cy) >> 14, dy = ((int64_t)c->speed * sp) >> 14;
    c->pos[0] = (int32_t)(((uint32_t)c->pos[0] + (uint32_t)dx) & WORLD_POS_MASK);   /* the world wraps */
    c->pos[2] = (int32_t)(((uint32_t)c->pos[2] + (uint32_t)dz) & WORLD_POS_MASK);
    int64_t ny = (int64_t)c->pos[1] + dy;
    c->pos[1] = (int32_t)(ny < 0 ? 0 : (ny > ALT_MAX ? ALT_MAX : ny));
}

int world_aircraft_start(const world_t *w, aircraft_t *ac) {
    if (!ac) return -1;
    memset(ac, 0, sizeof *ac);
    int32_t p[3];
    if (world_position_entry(w, WORLD_ENTRY_CARRIER, p) != 0) return -1;
    ac->pos[0] = p[0]; ac->pos[1] = p[1]; ac->pos[2] = p[2];
    ac->on_carrier = 1;
    return 0;
}

void world_object_matrix(fx_mat_t *q8, int32_t pitch, int32_t roll, int32_t yaw) {
    fx_mat_t q14;
    /* Note 03 (handedness): the original builds the inverse matrix with fx_matrix_b fed ALL THREE RECORD ANGLES NEGATED, pitch first: 28800 minus the record words +0x66
     * (pitch), +0x68 (heading), +0x6A (roll). The record's pitch word is positive nose down (aircraft.h: scene pitch = minus the word), the heading word is the scene yaw and
     * the roll word the scene roll. Tag C in the note: no rig comparison covers the object attitude. */
    uint32_t w66 = angle_units(-pitch), w68 = angle_units(yaw), w6a = angle_units(roll);
    fx_matrix_b(&q14, fx_neg_angle(w66), fx_neg_angle(w68), fx_neg_angle(w6a));
    fx_mat_q14_to_q8(q8, &q14);
}

void world_object_eye(const fx_mat_t *q8, const int32_t delta[3], int32_t eye[3]) {
    for (int i = 0; i < 3; i++) {
        int64_t s = 0;
        for (int j = 0; j < 3; j++) s += (int64_t)q8->m[j][i] * delta[j];
        eye[i] = (int32_t)(s >> 8);
    }
}

/* The cockpit eye offset (note 03, "Camera position and the cockpit eye offset", V: eye_check.py): body axes (x, y up, z forward) in units of 256 position units, fixed by the record's class
 * byte +0x62 (0x30: (0, 1, -5); 0x11, the F/A-18: (0, 5, 20); every other class: (0, 4, 18)), rotated by the record's nine Q14 words at +0x92 (the inverse attitude matrix, mat_b: each output the
 * 32-bit sum of the three products, shifted right arithmetically by 6) into position units. */
void world_eye_offset(const aircraft_t *ac, int32_t eye[3]) {
    const int cls = ac->f.cls;
    const int32_t v[3] = { 0, cls == 0x30 ? 1 : (cls == 0x11 ? 5 : 4), cls == 0x30 ? -5 : (cls == 0x11 ? 20 : 18) };
    for (int i = 0; i < 3; i++) {
        int32_t sum = 0;
        for (int j = 0; j < 3; j++) sum += (int32_t)ac->f.mat_b[3 * i + j] * v[j];
        eye[i] = sum >> 6;
    }
}

/* ---- the external camera aim (0x1454A, note 03 "External camera aim") ------------------------------------------------------ */

static int16_t aim_w16(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v & 0xFFFFu); }

/* 0x29E44: the signed word quotient of the 32-bit dividend by the 16-bit divisor, moved one away from zero when half the divisor's magnitude (shifted right) is not above the
 * remainder's magnitude; a quotient outside the word range leaves the dividend (68000 divide overflow): its low word is the quotient and its high word the remainder. div is not 0. */
static int32_t aim_rdiv(int32_t num, int32_t div) {
    int64_t q = (int64_t)num / div, r = (int64_t)num % div;
    if (q < -32768 || q > 32767) { q = aim_w16(num); r = aim_w16(num >> 16); }
    const int64_t ad = div < 0 ? -(int64_t)div : div, ar = r < 0 ? -r : r;
    if ((ad >> 1) <= ar) q = q >= 0 ? q + 1 : q - 1;
    return aim_w16((int32_t)q);
}

/* 0x29B12: the square root by Newton's iteration; a value above 0x63F000 is shifted right by 4 (root left by 2), above 0x63F0000 by 8 (root left by 4). */
static int32_t aim_isqrt(uint32_t v) {
    if (v > 0x63F0000u) return aim_isqrt(v >> 8) << 4;
    if (v > 0x63F000u) return aim_isqrt(v >> 4) << 2;
    int32_t g = (int32_t)(v / 200u) + 2;
    for (;;) {
        const int32_t q = (int32_t)(v / (uint32_t)g);
        const int d = aim_w16(q - g);
        if (d == 0 || d == 1 || d == -1) return q;
        g = (q + g) >> 1;
    }
}

/* The table word i: tenths of a degree of the arctangent of i / 256, 261 words (0, 0, 5, 5, ... 450). The index is not clamped: the x' <= z' test is made on truncated words, so it reaches 511 and
 * the lookup reads the image words after the table (note 03 as corrected by the review of the view round); the words come from the image span the trig tables are read from. */
static int aim_tab(const world_viewstate_t *vs, int32_t i) { return vs->image_word ? vs->image_word(vs->image_user, AIM_ATAN_ADDR + 2u * (uint32_t)i) : 0; }

void world_aim_target(const world_viewstate_t *vs, int32_t dx, int32_t dy, int32_t dz, int32_t *pitch, int32_t *heading) {
    uint32_t ax = (uint32_t)(dx < 0 ? -(int64_t)dx : dx), ay = (uint32_t)(dy < 0 ? -(int64_t)dy : dy), az = (uint32_t)(dz < 0 ? -(int64_t)dz : dz);
    const int fx = dx < 0, fy = dy < 0, fz = dz < 0;
    uint32_t m = ax > ay ? ax : ay;
    if (az > m) m = az;
    if (m > 0x10000000u) { ax >>= 3; ay >>= 3; az >>= 3; m >>= 3; }
    int S, T;
    if (m > 0x2000000u) { S = 14; T = 0; } else if (m > 0x800000u) { S = 12; T = 2; } else if (m > 0x200000u) { S = 10; T = 4; } else { S = 8; T = 6; }
    const int32_t xw = aim_w16((int32_t)(ax >> S)), zw = aim_w16((int32_t)(az >> S)), yw = aim_w16((int32_t)(ay >> S));
    int32_t h;
    if (xw <= zw) h = zw == 0 ? 3600 : 8 * aim_tab(vs, aim_rdiv((int32_t)(ax << T), zw) >> 6);
    else h = 8 * (900 - aim_tab(vs, aim_rdiv((int32_t)(az << T), xw) >> 6));
    if (fx && fz) h = 14400 - h;
    else if (fz) h = h + 14400;
    else if (!fx) h = 28800 - h;
    if (h >= 28800) h = 0;
    const int32_t H = aim_isqrt((uint32_t)((int32_t)xw * xw + (int32_t)zw * zw));
    int32_t p;
    if (yw <= H) p = H == 0 ? 3600 : 8 * aim_tab(vs, aim_rdiv((int32_t)yw << 8, H));
    else p = 8 * (900 - aim_tab(vs, aim_rdiv(H << 8, yw)));
    if (!fy) p = 28800 - p;
    if (p >= 28800) p = 0;
    *pitch = p; *heading = h;
}

int32_t world_aim_step(int32_t cur, int32_t target, int32_t limit) {
    const int32_t d = target - cur;
    int32_t m, r;
    if (d < 0) {
        if (d >= -14400) { m = (-d) >> 2; if (m > limit) m = limit; return cur - m; }
        m = (d + 28800) >> 2; if (m > limit) m = limit;
        r = cur + m;
        return r >= 28800 ? r - 28800 : r;
    }
    if (d > 0) {
        if (d <= 14400) { m = d >> 2; if (m > limit) m = limit; return cur + m; }
        m = (28800 - d) >> 2; if (m > limit) m = limit;
        r = cur - m;
        return r < 0 ? r + 28800 : r;
    }
    return cur;
}

/* ---- the head triples (note 06 "Head views") ------------------------------------------------------------------------------ */

int world_head_triple(unsigned cls, int code, int pitch_word, int triple[3]) {
    static const int16_t head_yaw[12] = { 0, 2256, 4800, 7200, 9600, 12000, 14400, 16800, 19200, 21600, 24000, 26544 };   /* the word table 0x3239A, codes 1 to 11 */
    if (code < 0 || code > 13) code = 0;
    if ((cls & 0xF0u) == 0x30u) {   /* the ejection seat: the table 0x323DC and its twelfth row; codes 12 and 13 pick the first value of the pair for a seat pitch above 6640 and below 7760 */
        const int first = pitch_word > 6640 && pitch_word < 7760;
        triple[1] = 0; triple[2] = 0;
        if (code == 0) { triple[0] = 21600; }
        else if (code <= 11) { triple[0] = (24000 + 2400 * (code - 1)) % 28800; triple[1] = 7200; triple[2] = 21600; }
        else if (code == 12) triple[0] = first ? 0x3610 : 0x3840;
        else triple[0] = first ? 0x230 : 0;
        return 1;
    }
    triple[0] = triple[1] = triple[2] = 0;
    if (code == 0) return 0;   /* V:4A869 is dead (note 06 "The glance byte V:4A869": one reader, no writer of any width, 0 in 312 state blocks): its triples (2: (0, 0x6E00, 0), 3 and more: (0, 0x280, 0)) are never selected; the value stays 0, no triple is implemented */
    if (code <= 11) { triple[1] = head_yaw[code]; return 1; }
    const int level = pitch_word < 560 || pitch_word >= 28240;   /* the 7 degree level test on the record's pitch word, signed */
    triple[0] = code == 12 ? (level ? 0x5690 : 0x5460) : (level ? 0x19F0 : 0x1C20);
    return 1;
}

/* ---- the camera of a frame (note 06 "View kinds, settled", note 03) ------------------------------------------------------- */

static int32_t scene_pitch_of_word(int32_t word) {   /* the scene's pitch is nose up positive: minus the signed pitch word */
    word %= ANGLE_CIRCLE; if (word < 0) word += ANGLE_CIRCLE;
    return -(word >= ANGLE_CIRCLE / 2 ? word - ANGLE_CIRCLE : word);
}

static int16_t ring_level_word(int16_t v, int level) {   /* 0x2D858: the per-word scaling, 16-bit words */
    switch (level) {
    case 1: return (int16_t)(v + (v >> 1));
    case 2: return (int16_t)(v * 2);
    case 3: return (int16_t)(v * 4);
    case 4: return (int16_t)(v * 8);
    default: return v;
    }
}

int world_camera_build(const world_t *w, const aircraft_t *ac, world_viewstate_t *vs, world_camera_t *out) {
    if (!w || !ac || !vs || !out || vs->level < -1 || vs->level > 4 || vs->ring < 0 || vs->ring >= (int)RING_ENTRIES) return -1;
    memset(out, 0, sizeof *out);
    const flight_state_t *f = &ac->f;
    int32_t yaw = ac->yaw % ANGLE_CIRCLE;
    if (yaw < 0) yaw += ANGLE_CIRCLE;
    if (vs->level == 0) {
        world_eye_offset(ac, out->eye);
        for (int i = 0; i < 3; i++) out->pos[i] = ac->pos[i] + out->eye[i];
        if (!vs->head_composed) {   /* the record's own words (head code 0) */
            out->yaw = (uint32_t)yaw;
            out->pitch = ac->pitch;
            out->roll = (uint32_t)((ac->roll % ANGLE_CIRCLE + ANGLE_CIRCLE) % ANGLE_CIRCLE);   /* the record's roll word as it is (right wing down negative, scene.h): fx_cam_ypr takes it unchanged (note 03) */
        } else {
            out->pitch = scene_pitch_of_word(vs->head_words[0]);
            out->yaw = (uint32_t)(((vs->head_words[1] % ANGLE_CIRCLE) + ANGLE_CIRCLE) % ANGLE_CIRCLE);
            out->roll = (uint32_t)(((vs->head_words[2] % ANGLE_CIRCLE) + ANGLE_CIRCLE) % ANGLE_CIRCLE);
        }
        return 0;
    }
    /* the ring rotation (0x2D756): the record's heading against the stored one, the absolute difference not wrapped round the circle */
    {
        const int32_t diff = yaw > vs->heading ? yaw - vs->heading : vs->heading - yaw;
        vs->heading = yaw;
        if (diff >= 7200 && diff <= 21600 && vs->ring != 0 && vs->ring != 4 && vs->ring < 8) vs->ring = (vs->ring + 4) % 8;
    }
    int32_t pos[3];
    if (vs->map) {   /* the map: directly above the aircraft every frame, at the map altitude */
        pos[0] = ac->pos[0]; pos[1] = vs->map_y; pos[2] = ac->pos[2];
    } else if (vs->fixed) {   /* the fixed camera (note 07 6.3): the camera routine does not move it (V:4A86D is clear); the aim turns it to the viewed record every frame */
        for (int i = 0; i < 3; i++) pos[i] = vs->fixed_pos[i];
    } else {
        const unsigned cls = f->cls;
        const int ci = cls == 0x11 ? 1 : (cls == 0x14 ? 2 : ((cls & 0xF0u) == 0x30u ? 3 : 0));
        int32_t wv[3], m[9];
        for (int j = 0; j < 3; j++) wv[j] = ring_level_word(w->ring[ci][vs->ring][j], vs->level);
        int full = (vs->ring == 0 || vs->ring == 4) && ci != 3;
        if (full) { int any = 0; for (int i = 0; i < 9; i++) any |= f->mat_b[i]; full = any != 0; }   /* C: a record without matrices (no flight tables) takes the heading-only matrix */
        if (full) {
            for (int i = 0; i < 9; i++) m[i] = f->mat_b[i];
        } else {
            const uint32_t a = fx_neg_angle((uint32_t)yaw);
            const int32_t s = fx_sin(a), c = fx_cos(a);
            m[0] = c; m[1] = 0; m[2] = s; m[3] = 0; m[4] = 16384; m[5] = 0; m[6] = -s; m[7] = 0; m[8] = c;
        }
        for (int i = 0; i < 3; i++) pos[i] = ac->pos[i] + ((m[3 * i] * wv[0] + m[3 * i + 1] * wv[1] + m[3 * i + 2] * wv[2]) >> WORLD_RING_UNIT_SHIFT);
        {   /* the height clamp: at least (deck + 7) x 256, the deck word +0x4E read only when bit 6 or 7 of +0x04 is set */
            const int32_t floor_y = ((f->b4 & 0xC0) ? f->deck + 7 : 7) * 256;
            if (pos[1] < floor_y) pos[1] = floor_y;
        }
    }
    for (int i = 0; i < 3; i++) { out->pos[i] = pos[i]; vs->cam_pos[i] = pos[i]; }
    if (vs->look && vs->image_word) {   /* the aim at the viewed record (0x32126 to 0x3214A, 0x1454A); C: a record without the trig tables (no flight tables) keeps its pair */
        int32_t tg[3], tp, th, limit;
        /* The target point (0x1D478, note 03 "External camera aim"): the offset (0, 0, 1), (0, 0, 5) in camera mode 6, (0, 0, -4) when the HIGH NIBBLE of the target record's class is 3 (the
         * ejection seat and the classes that share the nibble), rotated by the target record's +0x92 matrix and shifted right by 4 (the ring unit, 1024 position units). The target record is
         * V:4A996's, else V:4AA7A's when active with the whole class 0x30: the port has the one viewed record, the aircraft. */
        const int32_t vz = (f->cls & 0xF0u) == 0x30u ? -4 : (vs->mode == 6 ? 5 : 1);
        for (int i = 0; i < 3; i++) tg[i] = ac->pos[i] + ((f->mat_b[3 * i + 2] * vz) >> WORLD_RING_UNIT_SHIFT);   /* the sum of the three products with w = (0, 0, vz) */
        world_aim_target(vs, tg[0] - pos[0], tg[1] - pos[1], tg[2] - pos[2], &tp, &th);
        const int32_t key = (int32_t)(f->v4a996 | (unsigned)vs->ring);
        if (vs->look < 0 || !vs->aim_on) limit = -1;   /* V:4A86C negative (0xFF): snap every frame; V:4A86D clear: snap */
        else if (vs->mode) limit = WORLD_AIM_LIMIT_MODE;
        else if (key == vs->key_word) limit = WORLD_AIM_LIMIT;
        else { vs->key_word = key; limit = -1; }
        if (limit >= 0 && !vs->mode && !vs->first) limit = -1;   /* both V:4A966 and V:4A85E zero: snap */
        if (limit < 0) { vs->aim_pitch = tp; vs->aim_heading = th; vs->first = 1; }   /* every snap path sets V:4A85E = 1 (the one branch 0x1486C) */
        else { vs->aim_pitch = world_aim_step(vs->aim_pitch, tp, limit); vs->aim_heading = world_aim_step(vs->aim_heading, th, limit); }
    }
    out->pitch = scene_pitch_of_word(vs->aim_pitch);
    out->yaw = (uint32_t)(((vs->aim_heading % ANGLE_CIRCLE) + ANGLE_CIRCLE) % ANGLE_CIRCLE);
    return 0;
}


/* ---- the display lists and the scene (note 04a) ------------------------------------------------------------- */

int world_lists_cell(void *user, unsigned table, unsigned cx, unsigned cz, const lists_item_t **items) {
    typedef char layouts_equal[sizeof(lists_item_t) == sizeof(world_item_t) ? 1 : -1];
    (void)sizeof(layouts_equal);
    const world_item_t *it; unsigned n, ci;
    if (!world_cell_items((const world_t *)user, table, cx, cz, &it, &n, &ci)) return 0;
    *items = (const lists_item_t *)it;
    return (int)n;
}

lists_t *world_lists(world_t *w) { return w ? &w->lists : NULL; }

/* V:4AA3C, the last row of the 3D window by view (note 04 "3D window", note 06 head views): cockpit by head code, external and chase 167, the tower's map view 179. */
/* The HUD step byte of a head view code (the signed bytes at 0x1E800, note 06 "Head views"; code 4's is -50): the extractor's tables/head_step.bin (14 bytes; NULL: 0), read by the core's set_head and the frontend's --hud-test. */
int world_head_step(const int8_t *step_tab, int code) {
    return step_tab && code >= 0 && code < 14 ? step_tab[code] : 0;
}

int world_window_last_row(int map, int outside, int head) {
    static const uint8_t head_rows[14] = { 144, 144, 144, 167, 167, 179, 179, 179, 167, 167, 144, 144, 167, 167 };
    if (map) return 179;
    if (outside) return 167;
    return head >= 0 && head < 14 ? head_rows[head] : 144;
}
void world_set_window_last_row(world_t *w, int row) { if (w) w->row_limit = row; }
const lists_models_t *world_lists_models(const world_t *w) { return w ? &w->lmodels : NULL; }
const lists_tables_t *world_lists_tables(const world_t *w) { return w ? &w->ltab : NULL; }

void world_lists_request(world_t *w, unsigned mask) { if (w) lists_request(&w->lists, (uint8_t)mask); }

static void lists_frame_cam(world_t *w, const lists_frame_t *in, lists_frame_t *f) {
    *f = *in;
    for (int i = 0; i < 3; i++) { f->cam_pos[i] = w->cam.pos[i] - w->cam.eye[i]; f->cam_eye[i] = w->cam.eye[i]; }   /* the record's position and the rotated eye offset: the camera longs add the offset after the 22-bit mask */
    f->cam_yaw = (int32_t)w->cam.yaw;
    f->cam_pitch = w->cam.pitch;
}
void world_lists_build(world_t *w, const lists_frame_t *in) {
    lists_frame_t f;
    if (!w || !in) return;
    lists_frame_cam(w, in, &f);
    lists_frame_build(&w->lists, &f);
}
void world_lists_walk(world_t *w) { if (w) lists_frame_walk(&w->lists); }
void world_lists_walk_map(world_t *w) { if (w) lists_frame_walk_map(&w->lists); }
int world_altitude_gate(const world_t *w) { return w && lists_altitude_gate(&w->lists); }

void world_lists_frame(world_t *w, const lists_frame_t *in) {
    if (!w || !in) return;
    lists_frame_t f = *in;
    for (int i = 0; i < 3; i++) { f.cam_pos[i] = w->cam.pos[i] - w->cam.eye[i]; f.cam_eye[i] = w->cam.eye[i]; }   /* the record's position and the rotated eye offset: the camera longs add the offset after the 22-bit mask */
    f.cam_yaw = (int32_t)w->cam.yaw;
    f.cam_pitch = w->cam.pitch;
    lists_frame(&w->lists, &f);
}

int world_landmarks(const world_t *w, flight_lm_slot_t out[FLIGHT_LM_SLOTS]) {
    if (!w) return 0;
    const lists_t *l = &w->lists;
    int n = 0;
    for (int s = 0; s < l->nslots && s < LISTS_LANDMARK_SLOTS; s++) {
        if (!l->slot[s].valid) continue;   /* pointer -1 in the original: no record, the collision pass skips the slot */
        flight_lm_slot_t *o = &out[n];
        memset(o, 0, sizeof *o);
        o->cell_x = l->slot[s].cell_x; o->cell_z = l->slot[s].cell_z; o->ordinal = (uint8_t)s;
        {
            memcpy(o->box, l->slot[s].box, sizeof o->box);
            o->nnodes = l->slot[s].nnodes;
            for (int k = 0; k < l->slot[s].nnodes && k < FLIGHT_LM_NODES; k++) {
                memcpy(o->node[k].box, l->slot[s].node[k].box, sizeof o->node[k].box);
                o->node[k].nplanes = l->slot[s].node[k].nplanes;
                memcpy(o->node[k].ref, l->slot[s].node[k].ref, sizeof o->node[k].ref);
                memcpy(o->node[k].nrm, l->slot[s].node[k].nrm, sizeof o->node[k].nrm);
            }
        }
        n++;
    }
    return n;
}

int world_footprint_slot(const world_t *w, unsigned desc, int ox, int oz, flight_lm_slot_t *out) {
    if (!w || !out || !w->lists.tab) return -1;
    lists_node_t node[4];
    memset(node, 0, sizeof node);
    memset(out, 0, sizeof *out);
    if (lists_footprint(w->lists.tab, desc, ox, oz, out->box, &out->nnodes, node) != 0) return -1;
    for (int k = 0; k < out->nnodes && k < FLIGHT_LM_NODES; k++) {
        memcpy(out->node[k].box, node[k].box, sizeof out->node[k].box);
        out->node[k].nplanes = node[k].nplanes;
        memcpy(out->node[k].ref, node[k].ref, sizeof out->node[k].ref);
        memcpy(out->node[k].nrm, node[k].nrm, sizeof out->node[k].nrm);
    }
    return 0;
}

/* ---- the draw stage: level choice and the primitive layer (note 04a section 8b, note 03) ---------------------------------- */

int world_pick_level_word(const world_t *w, unsigned id, unsigned lod, int16_t scaled, lists_level_t *out) {
    const model_t *m = get_model(w, id);
    memset(out, 0, sizeof *out);
    if (!m) return -1;
    for (uint32_t i = 0; i < m->nnormal; i++) {
        const lvl_t *l = &m->lv[i];
        int fits;
        if (l->shape == SHAPE_LEAF_ONLY) fits = 1;                                  /* the last-resort leaf */
        else if (l->shape == 3) fits = ((int)(int16_t)l->threshold >> (lod & 15u)) >= (int)scaled;   /* type B: at least, unmasked; the note's shift is arithmetic: the extractor stops a type B list at a word with bit 15 set, so every threshold here is below 0x8000 and the shifts agree */
        else fits = (int)((l->threshold & 0x3FFFu) >> (lod & 15u)) > (int)scaled;   /* a signed word compare */
        if (!fits) continue;
        out->found = 1; out->present = l->present; out->kind = (uint8_t)m->kind; out->block_flags = l->block_flags;
        out->leaf = l->leaf; out->part_id = l->part_id; out->level = (uint16_t)i;
        return 0;
    }
    /* Type B, note 03 "Chain of detail entries": the last entry of models 39-42, 99 and 100 (threshold 0x7FFF) names itself as its next entry, so for a scaled distance above
     * 0x7FFF >> LOD the original never leaves the search; unreachable in play. Port ruling (the note): return nothing drawn and count the event. Bit 15 of a threshold takes no part in
     * the comparison (0xFFFF ends the search: the only such entry is model 38's, which the extractor exports with no level, so it never draws). The function stays pure: the event is
     * reported by world_draw_entry in world_draw_info_t.endless, once per draw. */
    return 0;
}

static void pk_from_level(const model_t *m, uint32_t i, lists_level_t *out) {
    const lvl_t *l = &m->lv[i];
    memset(out, 0, sizeof *out);
    out->found = 1; out->present = l->present; out->kind = (uint8_t)m->kind; out->block_flags = l->block_flags;
    out->leaf = l->leaf; out->part_id = l->part_id; out->level = (uint16_t)i;
}

int world_pick_cone_level(const world_t *w, unsigned id, unsigned list, lists_level_t *out) {
    const model_t *m = get_model(w, id);
    memset(out, 0, sizeof *out);
    if (!m || m->phase_first == NO_LEVEL || list < 1 || list > 4) return -1;
    pk_from_level(m, m->phase_first + list - 1u, out);
    return 0;
}

int world_pick_cockpit_level(const world_t *w, unsigned id, lists_level_t *out) {
    const model_t *m = get_model(w, id);
    memset(out, 0, sizeof *out);
    if (!m || m->alt_cockpit == NO_LEVEL) return -1;
    pk_from_level(m, m->alt_cockpit, out);
    return 0;
}

static int typeb_endless_model(const world_t *w, unsigned id) {
    const model_t *m = get_model(w, id);
    return m && m->kind == 2 && m->nnormal && (m->lv[m->nnormal - 1].threshold & 0x7FFFu) == 0x7FFFu;
}

static uint32_t angle_units(int32_t a) {      /* scene angle (28800 per circle, any integer) -> 0..28799 */
    a %= ANGLE_CIRCLE;
    return (uint32_t)(a < 0 ? a + ANGLE_CIRCLE : a);
}

void world_camera_matrix_z(const game_camera_t *cam, int zoom, fx_mat_t *m) {
    /* Camera matrix (note 03, "Roll sign and the angle inputs of the camera matrix"): fx_cam_ypr takes the viewed record's words unchanged. The scene's pitch is nose up positive, the
     * record's pitch word its negation; the heading is the word; the scene's roll is the record's roll word itself (right wing down negative, a positive roll is a left bank; scene.h). The frame
     * is the original's, with no mirror: camera x = cos B wx + sin B wz, depth = -sin B wx + cos B wz, so world +x is the camera's left and heading 90 degrees faces -x.
     * Then the rows are scaled by the zoom triple V:4AAF6 (note 03 "Camera matrix", fx_rowscale; note 04): (0xA8, 0xFC, V:4AAFA), the third halved or doubled by the zoom keys (0x20 to 0x80);
     * the matrix every consumer (the objects, the sky polygon, the sea mesh) uses is this one. */
    int16_t triple[3];
    if (cam->view == GAME_VIEW_COCKPIT) fx_cam_ypr(m, fx_neg_angle(angle_units(cam->pitch)), angle_units(cam->yaw), angle_units(cam->roll));
    else fx_cam_yp(m, fx_neg_angle(angle_units(cam->pitch)), angle_units(cam->yaw));   /* the external views and the map: the matrix of the aim pair, two angles and no roll (note 03, "Camera attitude in the head and external views", 0x32A66) */
    triple[0] = WORLD_ZOOM_ROW_X; triple[1] = WORLD_ZOOM_ROW_Y; triple[2] = (int16_t)(zoom ? zoom : 0x80);
    fx_rowscale(m, triple);
}

void world_camera_matrix(const game_camera_t *cam, fx_mat_t *m) { world_camera_matrix_z(cam, 0x80, m); }

void world_attitude_matrix(const game_camera_t *cam, fx_mat_t *m) {
    fx_cam_ypr(m, fx_neg_angle(angle_units(cam->pitch)), angle_units(cam->yaw), angle_units(cam->roll));
}

void world_sky_for_camera(game_sky_t *s, const game_camera_t *cam, int zoom, int row_limit, sky_state_t *st, uint32_t frame, int rear_age) {
    sky_poly_t poly;
    fx_mat_t m;
    const int cockpit = cam->view == GAME_VIEW_COCKPIT;
    int i;
    memset(s, 0, sizeof *s);
    if (!zoom) zoom = 0x80;
    world_camera_matrix_z(cam, zoom, &m);
    s->valid = 1;
    for (i = 0; i < 9; i++) s->matrix[i] = m.m[i / 3][i % 3];
    s->cam_y = -cam->pos.y;
    s->pitch_word = (int16_t)angle_units(-cam->pitch);
    s->roll_word = (int16_t)angle_units(cam->roll);
    s->cockpit = (uint8_t)cockpit;
    s->row_limit = (uint8_t)row_limit;
    s->zoom[0] = WORLD_ZOOM_ROW_X; s->zoom[1] = WORLD_ZOOM_ROW_Y; s->zoom[2] = (int16_t)zoom;
    s->side_flag = (uint8_t)sky_side_flag(cockpit, s->pitch_word, s->roll_word);
    sky_polygon(st, s->matrix, s->cam_y, cockpit, s->side_flag, s->pitch_word, s->roll_word, &poly);
    s->sky_n = poly.n;
    for (i = 0; i < 2 * poly.n; i++) s->sky_xy[i] = poly.xy[i];
    if (cockpit) {   /* the attitude ball: the same routine on the attitude matrix (unscaled, see game.c), the camera's words and the SAME persistent state */
        fx_mat_t am;
        int16_t aw[9];
        sky_poly_t bp;
        world_attitude_matrix(cam, &am);
        for (i = 0; i < 9; i++) aw[i] = am.m[i / 3][i % 3];
        sky_polygon(st, aw, s->cam_y, 1, s->side_flag, s->pitch_word, s->roll_word, &bp);
        s->ball_n = bp.n;
        for (i = 0; i < 2 * bp.n; i++) s->ball_xy[i] = bp.xy[i];
    }
    {   /* the tiny-polygon inputs as game.c's sky_frame sets them (sky_tiny_pen, sky_tiny_mask) */
        const sky_tiny_view_t tv = cockpit ? SKY_TINY_COCKPIT : (cam->view == GAME_VIEW_TOWER ? SKY_TINY_MAP : SKY_TINY_EXTERNAL);
        s->tiny_pen = (uint8_t)sky_tiny_pen(tv, rear_age, frame);
        s->tiny_mask = (uint8_t)sky_tiny_mask(tv, poly.n > 0, frame);
    }
    s->fade_target = 15;
}


/* One object: the model's drawing program through the draw layer for the camera. The tree walk (BSP order) runs the program items; every handler computes its
 * camera-space slots, rules and primitives (draw_item). `l` supplies the scan words the handlers read (the lists of the walk). */
static int draw_entry_impl(const world_t *w, const lists_t *l, const game_entry_t *o, const game_camera_t *cam, world_out_sink_t sink, void *user, world_draw_info_t *info, int replay) {
    world_draw_info_t local;
    if (!info) info = &local;
    info->polys = 0; info->fv_ok = 1; info->level = -1; info->ends_ffff = 0; info->aborted = 0; info->decider = 0; info->endless = 0; info->toggle_in = 0; info->alt_in = 0; memset(info->pbuf_in, 0, sizeof info->pbuf_in); info->den_zero = 0; info->op6_small = 0; info->rs_n = 0;
    g_den_zero = 0; g_op6_small = 0;
    if (!w || !o || !cam || !o->draw) return 0;
    /* The player's own entry: the cockpit block (variant NOTHING) has no program and draws nothing; the near body (variant NEAR_BODY) is the alternate list's near level, whose program
     * is the vertex synthesis of op 59 or 42 and op 4 with the detail faces in pen 0 (note 03, "What the near body shows"). It reaches the filler like any polygon: the filler rejects it
     * by the top row of its box against the window's last row (invisible in the 144-row cockpit window) and clips it there, the thin triangle becoming a line (out_poly). */
    if (o->kind == GAME_ENTRY_ENTITY && o->variant == GAME_VARIANT_NOTHING) return 0;
    fx_mat_t m;
    world_camera_matrix_z(cam, o->zoom, &m);
    /* Object attitude (world_object_matrix). With all three angles zero the model axes are the world axes. Otherwise each vertex is rotated by the object's Q8 matrix
     * (fx_rotate), shifted, offset by the object-minus-camera difference in world axes, and only then multiplied by the camera matrix; the eye offset is also taken
     * into the model axes (world_object_eye) for the BSP side tests. */
    int attitude = o->pitch || o->roll || o->yaw;
    fx_mat_t ro;
    if (attitude) world_object_matrix(&ro, o->pitch, o->roll, o->yaw);
    unsigned id = o->asset_id;
    int st = world_model_status(w, id);
    if (st != WORLD_MODEL_OK && st != WORLD_MODEL_PARTIAL) return -1;
    const int16_t scaled = lists_scaled_distance(o->dist, o->zoom, o->zoom_fixed);
    lists_level_t pk;
    if (o->kind == GAME_ENTRY_ENTITY && o->variant == GAME_VARIANT_NEAR_BODY) {
        const model_t *nm = get_model(w, id);
        if (!nm || nm->alt_near == NO_LEVEL || !nm->lv[nm->alt_near].present) return -1;
        const lvl_t *al = &nm->lv[nm->alt_near];
        memset(&pk, 0, sizeof pk);
        pk.found = 1; pk.present = al->present; pk.kind = (uint8_t)nm->kind; pk.block_flags = al->block_flags; pk.leaf = al->leaf; pk.part_id = al->part_id; pk.level = (uint16_t)nm->alt_near;
    } else if (o->kind == GAME_ENTRY_ENTITY && o->cone_list && world_pick_cone_level(w, id, o->cone_list, &pk) == 0) {
        if (!pk.present) return -1;   /* the cone handlers' phase list: one leaf, chosen by the phase (note 07 11.5) */
    } else if (world_pick_level_word(w, id, o->lod, scaled, &pk) != 0 || !pk.found || !pk.present) {
        if (world_pick_level_word(w, id, o->lod, scaled, &pk) == 0 && !pk.found && typeb_endless_model(w, id)) info->endless = 1;
        return -1;
    }
    int32_t delta[3];   /* camera minus object, position units; no mirror (note 03: the original has none) */
    delta[0] = cam->pos.x - o->pos.x;
    delta[1] = cam->pos.y - o->pos.y;
    delta[2] = cam->pos.z - o->pos.z;
    world_view_t view;   /* the eye in model axes (BSP side tests) */
    int32_t mdelta[3] = { delta[0], delta[1], delta[2] };
    if (attitude) world_object_eye(&ro, delta, mdelta);
    int level = make_view_at(w, id, mdelta, 0, &view, pk.level);
    if (level < 0) return -1;
    view.op4_closed = o->kind == GAME_ENTRY_ENTITY ? o->op4_closed : 0;
    /* P6: the program-item distance gates compare the LOD-shifted gate words with the scaled distance word (not the exact eye distance) */
    view.has_gate = 1; view.gate_dist = scaled; view.gate_lod = (uint8_t)(o->lod & 15u);
    info->level = level;
    const model_t *mod = get_model(w, id);
    int nv = (int)mod->lv[level].vcount;
    if (nv <= 0) return -1;
    /* P2 and P3 of note 04a section 8b (the word pipeline of note 03, "Translation to camera space"). The block's precision exponent compares its thresholds Tm and
     * Tf (signed words, shifted right by the LOD) with the UNSCALED distance word: farther than Tf, vertex shift L and position exponent 0; between Tm and Tf, vertex
     * shift H and exponent L - H; nearer, vertex shift 0 and exponent L. The position (the object-minus-camera difference in position units divided by 2^LOD) is
     * shifted right by 8 minus the exponent and kept as a word per axis; the vertex words (rotated by the object matrix first when the object has an attitude) are
     * shifted right by the vertex shift plus the LOD, the position words are added and the Q8 camera matrix is applied; every step is a 16-bit word. */
    const lvl_t *blk = &mod->lv[level];
    const unsigned lod = o->lod & 15u;
    const int dword = (int16_t)o->dist;
    unsigned vshift, pexp;
    if (mod->kind == 2) { vshift = 0; pexp = 0; }   /* type B: the pairs are shifted by the LOD only, no precision exponent */
    else if (dword > ((int16_t)blk->tf >> lod)) { vshift = blk->scale_l; pexp = 0; }
    else if (dword > ((int16_t)blk->tm >> lod)) { vshift = blk->scale_h; pexp = (unsigned)(blk->scale_l - blk->scale_h) & 15u; }
    else { vshift = 0; pexp = blk->scale_l; }
    /* C: an exponent above 8 would be a negative shift count (a level with L above 8; no shipped level has one): taken as 8 here (shift 0), as make_view_l does */
    const unsigned psh = lod + (pexp > 8 ? 0u : 8u - pexp);
    draw_t *d = &g_draw;
    d->w = w; d->m = mod; d->lv = blk; d->l = l; d->o = o; d->lod = lod; d->sd = scaled;
    d->rs_n = 0;
    d->gear = (o->kind == GAME_ENTRY_ENTITY && l) ? l->ent[o->id & 15].b7c : 0;
    d->cm = m; d->ro = ro; d->attitude = attitude;
    d->vsh = vshift + lod;
    for (int k = 0; k < 3; k++) d->pw[k] = (int16_t)(int32_t)(((int64_t)-(int64_t)delta[k] >> psh) & 0xFFFF);
    d->flat = (mod->kind == 1 && (blk->block_flags & 2)) || mod->kind == 2;
    d->nv = nv > DSLOTS ? DSLOTS : nv;
    d->row_limit = w->row_limit;
    d->sink = sink; d->user = user; d->word = 0; d->polys = 0; d->last_pos_op = d->last_abort_op = d->last_op = 0;
    memset(d->have, 0, (size_t)d->nv);
    second_section(d);
    /* rule 8 of note 04a section 8b: the first vertex of a non-rotating block that needs the clip test must lie in the view pyramid */
    if (!(pk.leaf & 0x4000) && !(pk.block_flags & 1) && pk.kind != 2) {
        const fx_vec_t *v0 = dsl(d, 0);
        if (v0) {
            int32_t d4 = v0->z;
            int16_t lim = (int16_t)(d4 + (d4 >> 1));
            info->fv_ok = d4 > 0 && v0->x <= lim && (int16_t)-v0->x <= lim && v0->y <= lim && (int16_t)-v0->y <= lim;
        }
        if (!info->fv_ok) return 0;
    }
    /* the tree-walk entry: the fill state of a new object (note 03, "Fill state"); the type B drawer leaves the toggle word as the last object left it, which a replay of the walk's draw takes from the entry */
    if (replay) { for (int i = 0; i < 3; i++) d->pbuf[4 + i] = o->pbuf_in[i]; }   /* the polygon buffer residue the walk found at this object's entry */
    if (replay && mod->kind == 2) { d->ftog = o->fill_toggle; d->falt = o->fill_alt; }
    info->toggle_in = d->ftog; info->alt_in = d->falt;
    for (int i = 0; i < 3; i++) info->pbuf_in[i] = d->pbuf[4 + i];
    fs_reset(d, mod->kind == 2);
    static world_poly_t none[1];
    unsigned endf = 0;
    int np = walk_model(w, id, level, &view, none, 1, &endf, d);
    d->sink = NULL;
    if (np < 0) return -1;
    info->ends_ffff = (endf & WORLD_WALK_END_FFFF) != 0; info->aborted = (endf & WORLD_WALK_ABORTED) != 0;
    info->polys = d->polys; info->den_zero = g_den_zero; info->op6_small = g_op6_small;
    info->rs_n = d->rs_n; memcpy(info->rs_op, d->rs_op, sizeof info->rs_op); memcpy(info->rs_base, d->rs_base, sizeof info->rs_base);
    info->decider = d->polys > 0 ? d->last_pos_op : (d->last_abort_op ? d->last_abort_op : d->last_op);
    return 0;
}

int world_draw_entry(const world_t *w, const game_entry_t *o, const game_camera_t *cam, world_out_sink_t sink, void *user, world_draw_info_t *info) {
    /* A replay of the walk's draw must not move the persistent state of the walk (the fill block, the pen and the polygon buffer carry across objects and frames): the entry holds what the walk found. */
    draw_t *d = &g_draw;
    const uint16_t fmask = d->fmask, ftog = d->ftog, falt = d->falt;
    const int16_t fpat = d->fpat;
    int16_t pbuf[7];
    memcpy(pbuf, d->pbuf, sizeof pbuf);
    const unsigned pen = d->cur_pen;
    int r = draw_entry_impl(w, w ? &w->lists : NULL, o, cam, sink, user, info, 1);
    d->fmask = fmask; d->ftog = ftog; d->falt = falt; d->fpat = fpat; d->cur_pen = pen;
    memcpy(d->pbuf, pbuf, sizeof pbuf);
    return r;
}

/* op 31 (below the draw layer: it needs the lists' pre-test): the early-out of rule 6 on the two consecutive flat vertices of the DRAWN block at byte offset `operand` of its vertex list
 * (the pair starts at V:4AAEA + 10 + operand; flat vertices are 4 bytes, so vertex operand/4 and the next one; the shipped operands are 12 and 20; note 03, "The block that op 31 names"). */
static int box_pretest(const draw_t *d, unsigned operand) {
    const lists_scan_t *s = &d->l->scan;
    if (!(s->off_y > -0x140) || d->l->v4a924 || !d->o) return 0;
    return lists_pretest_rejects(d->l, d->o->asset_id, (unsigned)(d->lv - d->m->lv), d->lod, d->o->x, d->o->z, d->sd, operand / 4);
}

/* ---- test hook ---------------------------------------------------------------------------------------------------------------------------------------- */
/* Runs one primitive handler of the draw layer on camera-space slots, for the differential test against the original's handlers (tests/rig/test_draw_diff.c). The operands are the
 * extractor's (slot numbers, counts), the pen the item's; `slots` fill the vertex table; off_y is V:4AB30 (the height gates of ops 74 and 75). reset 1 sets the fill state of a new
 * object first (the common drawer's entry). The polygon sink receives the clipper's output as it is (no filler front). Returns the handler's word; *code gets 0, 1 (negative condition
 * codes) or 2 (negative word). */
int world_test_handler(int op, const int32_t *operands, int nops, unsigned pen, const fx_vec_t *slots, int nslots, int off_y, int reset,
                       world_out_sink_t sink, void *user, int *code) {
    static lists_t zl;
    static model_t fm;
    static item_t it;
    static int32_t ops[512];
    draw_t *d = &g_draw;
    if (nslots > DSLOTS) nslots = DSLOTS;
    if (nops > 512) nops = 512;
    for (int i = 0; i < nops; i++) ops[i] = operands[i];
    memset(&zl, 0, sizeof zl);
    g_den_zero = 0; g_op6_small = 0;
    zl.scan.off_y = off_y;
    memset(&fm, 0, sizeof fm);
    fm.op = ops;
    memset(&it, 0, sizeof it);
    it.op = (uint16_t)op; it.pen = (uint16_t)pen; it.w = (uint16_t)pen; /* the polygon word W: pen bits only (ops 3 and 69 read it) */ it.nops = (uint16_t)nops; it.opfirst = 0; it.g1 = 0x7FFF;
    d->l = &zl; d->o = NULL; d->m = &fm; d->lv = NULL; d->lod = 0; d->sd = 0; d->row_limit = 179; d->gear = 0;
    d->nv = nslots; d->sink = sink; d->user = user; d->raw_polys = 1;
    memset(d->have, 0, (size_t)DSLOTS);
    for (int i = 0; i < nslots; i++) { d->cv[i] = slots[i]; d->have[i] = 1; }
    if (reset) { fs_reset(d, 0); d->cur_pen = 5; memset(d->pbuf, 0, sizeof d->pbuf); d->pshift = d->pcount = 0; }   /* the harness's sentinel pen */
    walk_t c;
    memset(&c, 0, sizeof c);
    c.m = &fm; c.dr = d;
    int rc = draw_item(&c, &it);
    d->sink = NULL; d->raw_polys = 0;
    if (code) *code = rc;
    return d->word;
}
/* The polygon filler's front (out_poly) on screen vertices, for tests: row_limit is V:4AA3C. */
void world_test_filler(const int16_t *xy, int n, int row_limit, unsigned pen, world_out_sink_t sink, void *user) {
    draw_t *d = &g_draw;
    d->sink = sink; d->user = user; d->raw_polys = 0; d->row_limit = row_limit; d->fmask = 0xF; d->fpat = -1; d->ftog = 0; d->falt = 0;
    out_poly(d, xy, n, pen);
    d->sink = NULL;
}
int world_test_den_zero(void) { return g_den_zero; }
int world_test_op6_small(void) { return g_op6_small; }
void world_test_fill_state(unsigned *mask, int *pattern, unsigned *toggle, unsigned *pen) { *mask = g_draw.fmask; *pattern = g_draw.fpat; *toggle = g_draw.ftog; *pen = g_draw.cur_pen; }

void world_test_pwords(int *shift, int *count) { *shift = g_draw.pshift; *count = g_draw.pcount; }
unsigned world_test_fill_alt(void) { return g_draw.falt; }
void world_test_set_fill_alt(unsigned v) { g_draw.falt = (uint16_t)v; }
void world_test_pbuf(int16_t out[7], int set) { if (set) memcpy(g_draw.pbuf, out, sizeof g_draw.pbuf); else memcpy(out, g_draw.pbuf, sizeof g_draw.pbuf); }

/* The call of the near-ground pass (0x11212, note 04, "Stage [90]") stores the fill block (4, 0, 0, 0xFFFF) and the colour word 3 before any test, so every walk that reaches the call leaves them, also
 * above the height limit; nothing restores them. A type B object drawn after it without a plain drawer's entry in between inherits V:4A7A4 = 0xFFFF and, when its first polygon has a non-negative pattern
 * word (op 26: mask 3, pattern 3), takes the alternate blit instead of its pattern fill (world_out_t.alt). NOTE-GAP (C): whether a shipped scene draws such a type B op-26 object in that window (models
 * 36, 39-42, 99, 100) is not examined in the notes; the port carries the block as one global state with the writers and resets of "Fill state", so it happens by itself. */
static void mi_stage90(void *user) {
    draw_t *d = &g_draw;
    (void)user;
    d->fmask = 4; d->fpat = 0; d->ftog = 0; d->falt = 0xFFFF;
    d->cur_pen = 3;
}

void world_fill_block_sky(world_t *w, int sky_polygon, int mesh_ran) {
    draw_t *d = &g_draw;
    (void)w;
    if (sky_polygon) { d->fmask = 1; d->fpat = 1; d->ftog = 0; d->falt = 0; }        /* the sky fill's tail (1, 1, 0, 0) */
    if (mesh_ran) { d->fmask = 2; d->fpat = 2; d->ftog = 0; d->falt = 0; d->cur_pen = 6; }   /* the sea mesh stage, when it runs: the block (2, 2, 0, 0) and the colour 6 (note 04, stage [68]) */
}

/* Note 03, "Op 9 reads past its quad": the sea mesh polygons are built in the polygon buffer with the object polygons' layout (V mesh_check.py, 0 differences). */
void world_mesh_pbuf_begin(world_t *w) { (void)w; g_draw.pshift = 0; }
void world_mesh_pbuf_polygon(world_t *w, const fx_vec_t *v, int n) {
    (void)w;
    pbuf_write(&g_draw, v, n);
    g_draw.pcount = (int16_t)n;
}

/* ---- the model interface of the drawer ------------------------------------------------------------------------------------- */

static int mi_pick(void *user, unsigned model, unsigned lod, int16_t scaled, lists_level_t *out) {
    return world_pick_level_word((const world_t *)user, model, lod, scaled, out);
}

static int mi_pick_cone(void *user, unsigned model, unsigned list, lists_level_t *out) { return world_pick_cone_level((const world_t *)user, model, list, out); }

static int mi_pick_cockpit(void *user, unsigned model, lists_level_t *out) { return world_pick_cockpit_level((const world_t *)user, model, out); }

static int mi_vertex_xz(void *user, unsigned model, unsigned level, unsigned slot, int16_t *x, int16_t *z) {
    int32_t xyz[3];
    if (world_model_vertex((const world_t *)user, model, (int)level, slot, xyz) != 0) return -1;
    *x = (int16_t)xyz[0]; *z = (int16_t)xyz[2];
    return 0;
}

/* A vertex the block itself holds (state block or strip): 0 with the words; 1 for a synthesised one (the extractor's model-space value, not what the draw stores in the record: the record copy of the
 * operations computes it from the rotated slots), -1 for a slot the level has not got or that is undefined or unresolved. */
static int mi_vertex(void *user, unsigned model, unsigned level, unsigned slot, int32_t v[3]) {
    const world_t *w = (const world_t *)user;
    const model_t *m = get_model(w, model);
    if (!m || level >= m->nl) return -1;
    const lvl_t *lv = &m->lv[level];
    if (slot == lv->b8 && lv->b8 == lv->b9) {   /* the vertex loop's last pass: the data triple after the vertices, rotated like a vertex (07 8.10 rule 5, any record-kind block); nothing reads it */
        for (int k = 0; k < 3; k++) v[k] = lv->data[k];
        return 0;
    }
    if (slot >= lv->vcount) return -1;
    const vtx_t *x = &m->vt[lv->vfirst + slot];
    if (x->state == VS_UNDEFINED || x->state == VS_UNRESOLVED || x->state == VS_SECOND) return -1;
    v[0] = x->x; v[1] = x->y; v[2] = x->z;
    return x->state == 3 ? 1 : 0;
}

static unsigned mi_vslot_count(void *user, unsigned model, unsigned level) {
    const model_t *m = get_model((const world_t *)user, model);
    if (!m || level >= m->nl) return 0;
    const lvl_t *lv = &m->lv[level];
    return lv->b8 == lv->b9 ? (unsigned)lv->b8 + 1u : lv->b8 > lv->b9 ? lv->b8 : 0u;   /* 07 8.10 rule 5 */
}

static void fill_entry(const world_t *w, const lists_t *l, const lists_walk_t *wk, game_entry_t *g);

static int mi_prims(void *user, const lists_t *l, unsigned wi, const lists_level_t *lv, lists_prims_t *out) {
    const world_t *w = (const world_t *)user;
    (void)lv;
    game_entry_t g;
    fill_entry(w, l, &l->walk[wi], &g);
    g.draw = 1;
    game_camera_t cam;
    memset(&cam, 0, sizeof cam);
    cam.pos.x = w->cam.pos[0]; cam.pos.y = w->cam.pos[1]; cam.pos.z = w->cam.pos[2];
    cam.yaw = (int32_t)w->cam.yaw; cam.pitch = w->cam.pitch; cam.roll = (int32_t)w->cam.roll;
    world_draw_info_t info;
    int r = draw_entry_impl(w, l, &g, &cam, NULL, NULL, &info, 0);
    out->polys = r < 0 ? 0 : info.polys;
    out->fv_ok = r < 0 ? 1 : (uint8_t)info.fv_ok;
    out->ends_ffff = r < 0 ? 0 : info.ends_ffff;
    out->aborted = r < 0 ? 0 : info.aborted;
    out->decider = r < 0 ? 0 : (uint8_t)info.decider;
    out->toggle_in = r < 0 ? 0 : info.toggle_in;
    out->alt_in = r < 0 ? 0 : info.alt_in;
    for (int i = 0; i < 3; i++) out->pbuf_in[i] = r < 0 ? 0 : info.pbuf_in[i];
    out->rs_n = r < 0 ? 0 : info.rs_n;
    for (int i = 0; i < out->rs_n && i < LISTS_RS_MAX; i++) { out->rs_op[i] = info.rs_op[i]; out->rs_base[i] = info.rs_base[i]; }
    return 0;
}

static void install_models(world_t *w) {
    w->lmodels.pick_cone = mi_pick_cone;
    w->lmodels.pick_cockpit = mi_pick_cockpit;
    w->lmodels.user = w; w->lmodels.pick = mi_pick; w->lmodels.vertex_xz = mi_vertex_xz; w->lmodels.vertex = mi_vertex; w->lmodels.vslot_count = mi_vslot_count; w->lmodels.prims = mi_prims; w->lmodels.stage90 = mi_stage90;
    lists_set_models(&w->lists, &w->lmodels);
}

/* The camera of the walk is the scene's camera: in the cockpit view the viewed record's position plus the rotated eye offset (world_eye_offset, note 03 "Camera position and the
 * cockpit eye offset"), added by world_camera_build; the builder and the walker read the same camera words. */
static void fill_entry(const world_t *w, const lists_t *l, const lists_walk_t *wk, game_entry_t *g) {
    (void)w;
    memset(g, 0, sizeof *g);
    g->tier = wk->tier;
    g->counter = wk->counter; g->prev_result = wk->prev_result; g->result = wk->result;
    g->fill_toggle = wk->toggle_in; g->fill_alt = wk->alt_in;
    for (int i = 0; i < 3; i++) g->pbuf_in[i] = wk->pbuf_in[i];
    g->draw = wk->drew; g->variant = wk->variant; g->op4_closed = wk->op4_closed; g->op4_neg = wk->op4_neg;
    g->zoom = l->zoom; g->zoom_fixed = l->zoom_fixed;
    g->dist = wk->dist;
    int lod = wk->lod;
    if (wk->tier == LISTS_TIER_DIRECT) {   /* a record of the direct list: placed from the cell difference (0x1FC20) */
        const lists_entity_t *en = &l->ent[wk->index & 15];
        int64_t px = ((int64_t)((int)en->cell_x - (int)l->scan.cell_x) * 4194304 + ((int64_t)en->pos[0] & 0x3FFFFF)) >> lod;
        int64_t pz = ((int64_t)((int)en->cell_z - (int)l->scan.cell_z) * 4194304 + ((int64_t)en->pos[2] & 0x3FFFFF)) >> lod;
        g->kind = GAME_ENTRY_ENTITY; g->dclass = LISTS_CLASS_ENTITY; g->lod = (uint8_t)lod; g->cone_list = wk->cone_list;
        g->id = wk->index; g->asset_id = en->model;
        g->rec_class = en->cls; g->damage = en->damage; g->phase = en->phase; g->t04 = l->sec[wk->index & 15].phase; g->model_id = en->model;   /* op 70 reads the secondary object of the same index (07 11.3) */
        g->pos.x = (int32_t)(((int64_t)l->scan.cell_x << 22) + px * (1 << lod)); g->pos.z = (int32_t)(((int64_t)l->scan.cell_z << 22) + pz * (1 << lod));
        g->pos.y = (int32_t)(((int64_t)en->pos[1] >> lod) * (1 << lod));
        g->pitch = en->pitch; g->roll = en->roll; g->yaw = en->yaw;
        return;
    }
    const lists_entry_t *e = &l->q[wk->tier][wk->index];
    g->lod = (uint8_t)lod; g->id = e->id; g->x = e->x; g->y = e->y; g->z = e->z; g->key = e->key;
    g->countdown = e->countdown;
    int64_t cx = l->corner[wk->tier][0], cz = l->corner[wk->tier][1];
    int64_t px = cx + ((int64_t)e->x * (1 << (8 + lod))), pz = cz + ((int64_t)e->z * (1 << (8 + lod))), py = (int64_t)e->y * (1 << (8 + lod));
    if (e->flags & 0x50) {
        const lists_entity_t *en = (e->flags & 0x40) ? &l->sec[e->id & 15] : &l->ent[e->id & 15];
        g->kind = (e->flags & 0x40) ? GAME_ENTRY_SECONDARY : GAME_ENTRY_ENTITY;
        g->dclass = LISTS_CLASS_ENTITY; g->asset_id = en->model;
        if (e->flags & 0x40) {   /* an effect object: the id word 0x11 to 0x13 is the descriptor index, the models are 33 to 35 (note 07 11.1, 11.3; the terrain entries add 16 the same way) */
            g->dclass = LISTS_CLASS_DRAW; g->asset_id = (uint16_t)(en->model + 16u);
        }
        g->rec_class = (e->flags & 0x40) ? 0 : en->cls; g->damage = en->damage; g->phase = en->phase; g->t04 = l->sec[e->id & 15].phase; g->model_id = en->model;   /* op 70 reads the life word of the secondary object V:4AA6C (the index of the drawn record, its own for an effect object), 07 11.3 */
        if (!(e->flags & 0x40)) g->cone_list = wk->cone_list;
        px += (((int64_t)en->pos[0] & 0xFFFFF) >> lod) * (1 << lod);
        pz += (((int64_t)en->pos[2] & 0xFFFFF) >> lod) * (1 << lod);
        py = ((int64_t)en->pos[1] >> lod) * (1 << lod);
        g->pitch = en->pitch; g->roll = en->roll; g->yaw = en->yaw;
    } else {
        g->kind = GAME_ENTRY_TERRAIN;
        g->dclass = lists_desc_of(l, e)->cls;
        g->asset_id = (uint16_t)(e->id + 16);
    }
    g->pos.x = (int32_t)px; g->pos.y = (int32_t)py; g->pos.z = (int32_t)pz;
}

void world_fill_scene_view(const world_t *w, game_scene_t *out, game_view_t view) {
    if (!w || !out) return;
    const world_camera_t *cam = &w->cam;
    /* scene.h carries exact integers: positions in position units, angles in 28800 per circle */
    out->camera.pos.x = cam->pos[0]; out->camera.pos.y = cam->pos[1]; out->camera.pos.z = cam->pos[2];
    out->camera.yaw = (int32_t)cam->yaw;
    out->camera.pitch = (int32_t)cam->pitch;
    out->camera.roll = (int32_t)cam->roll;
    out->camera.view = view;
    /* HUD: 80 angle units per degree (28800 / 360); altitude figure 5 x (altitude >> 10),
     * note 02 "Constants" (unit unverified) */
    out->hud.heading_deg = (int32_t)(cam->yaw / 80u);
    out->hud.pitch_deg = cam->pitch / 80;
    out->hud.roll_deg = 0;
    out->hud.altitude_ft = 5 * (cam->pos[1] >> 10);
    out->hud.throttle_pct = (int32_t)(((int64_t)cam->speed * 100) / FREECAM_MAX_SPEED);

    out->entry_count = 0;
    for (unsigned i = 0; i < w->lists.nwalk && out->entry_count < GAME_MAX_ENTRIES; i++) {
        if (!w->lists.walk[i].draw) continue;
        if (w->lists.walk[i].kind == 3) {   /* the near-ground pass (stage [90]): its specks, in table order, at its place in the draw order */
            lists_speck_t sp[LISTS_MAX_SPECKS];
            fx_mat_t m;
            int16_t mw[9];
            game_camera_t gc;
            memset(&gc, 0, sizeof gc);
            gc.pos.x = cam->pos[0]; gc.pos.y = cam->pos[1]; gc.pos.z = cam->pos[2];
            gc.yaw = (int32_t)cam->yaw; gc.pitch = cam->pitch; gc.roll = (int32_t)cam->roll; gc.view = view;
            world_camera_matrix_z(&gc, w->lists.zoom, &m);
            for (int k = 0; k < 9; k++) mw[k] = m.m[k / 3][k % 3];
            int ns = lists_nearground(&w->lists, mw, w->row_limit, sp, LISTS_MAX_SPECKS);
            for (int k = 0; k < ns && out->entry_count < GAME_MAX_ENTRIES; k++) {
                game_entry_t *g = &out->entries[out->entry_count++];
                memset(g, 0, sizeof *g);
                g->tier = GAME_TIER_DIRECT; g->draw = 1;
                g->kind = sp[k].kind == LISTS_SPECK_FILL ? GAME_ENTRY_SPECK_FILL : (sp[k].kind == LISTS_SPECK_TRI ? GAME_ENTRY_SPECK_TRI : GAME_ENTRY_SPECK);
                g->variant = sp[k].kind == LISTS_SPECK_2X1 ? 1 : 0;
                memcpy(g->sv, sp[k].xy, sizeof g->sv);
            }
            continue;
        }
        fill_entry(w, &w->lists, &w->lists.walk[i], &out->entries[out->entry_count++]);
    }
}

void world_fill_scene(const world_t *w, game_scene_t *out) { world_fill_scene_view(w, out, GAME_VIEW_COCKPIT); }
