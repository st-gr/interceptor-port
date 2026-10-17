/* src/core/lists.c - the display-list builder; see lists.h. Source of every rule: docs/re/04a-display-lists.md. */
#include <stdlib.h>
#include <string.h>
#include "lists.h"
#include "assets.h"
#include "fx.h"

/* ---- helpers ---------------------------------------------------------------------------------------------------- */

static int16_t w16(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v & 0xFFFFu); }
static unsigned be16(const uint8_t *p) { return ((unsigned)p[0] << 8) | p[1]; }
static int32_t i32abs(int32_t v) { return v < 0 ? -v : v; }
/* The 68000's shifts are defined for every value; the port computes the same bits without relying on C's undefined left shift of a negative. */
static int32_t shl32(int32_t v, int n) { return (int32_t)((uint32_t)v << n); }
static int16_t asr16(int32_t v, int n) { return w16(w16(v) >> (n > 15 ? 15 : n)); }

/* ---- the tables ----------------------------------------------------------------------------------------------- */

static int read_fixed(const game_loader_t *ld, const char *path, uint8_t *dst, size_t n, char *err, size_t errlen) {
    if (core_assets_read_exact(ld, path, dst, n) == 0) return 0;
    if (err && errlen) {
        err[0] = '\0';
        core_assets_append(err, errlen, "lists: cannot read ");
        core_assets_append(err, errlen, path);
        core_assets_append(err, errlen, " (missing or wrong size)");
    }
    return -1;
}

int lists_tables_load(lists_tables_t *t, const game_loader_t *ld, char *err, size_t errlen) {
    memset(t, 0, sizeof *t);
    uint8_t lim[16], vis[2 * LISTS_VIS_WORDS], desc[LISTS_RECORDS * 8], tiles[42];
    if (read_fixed(ld, "tables/lists_quad.bin", t->quad, 16, err, errlen) ||
        read_fixed(ld, "tables/lists_octperm.bin", t->octperm, 32, err, errlen) ||
        read_fixed(ld, "tables/lists_group.bin", t->group, 16, err, errlen) ||
        read_fixed(ld, "tables/lists_subperm.bin", t->subperm, 64, err, errlen) ||
        read_fixed(ld, "tables/lists_tileperm.bin", t->tileperm, 96, err, errlen) ||
        read_fixed(ld, "tables/lists_tiles.bin", tiles, 42, err, errlen) ||
        read_fixed(ld, "tables/lists_steep.bin", t->steep, sizeof t->steep, err, errlen) ||
        read_fixed(ld, "tables/lists_high.bin", t->high, sizeof t->high, err, errlen) ||
        read_fixed(ld, "tables/lists_low.bin", t->low, sizeof t->low, err, errlen) ||
        read_fixed(ld, "tables/lists_limit.bin", lim, 16, err, errlen) ||
        read_fixed(ld, "tables/lists_vis.bin", vis, 2 * LISTS_VIS_WORDS, err, errlen) ||
        read_fixed(ld, "tables/lists_desc.bin", desc, sizeof desc, err, errlen))
        return -1;
    {   /* the near-ground pass's tables: class bytes, the three point tables as one run of 726 words, the shapes as 36 words */
        static const int cnt[3] = { 96, 80, 64 }, lim[3] = { 2, 3, 4 };
        int16_t w[726];
        if (read_fixed(ld, "tables/lists_ngclass.bin", t->ng_class, sizeof t->ng_class, err, errlen)) return -1;
        if (core_assets_read_words(ld, "tables/lists_ngpts.bin", w, 726) != 0 || core_assets_read_words(ld, "tables/lists_ngshape.bin", &t->ng_shape[0][0][0], 36) != 0) {
            if (err && errlen) { err[0] = '\0'; core_assets_append(err, errlen, "lists: cannot read tables/lists_ngpts.bin or tables/lists_ngshape.bin (missing or wrong size)"); }
            return -1;
        }
        const int16_t *p = w;
        for (int k = 0; k < 3; k++) {
            t->ng[k].count = p[0]; t->ng[k].limit = p[1]; p += 2;
            if (t->ng[k].count != cnt[k] || t->ng[k].limit != lim[k]) {   /* the shipped tables; a synthetic set must keep the header */
                if (err && errlen) { err[0] = '\0'; core_assets_append(err, errlen, "lists: tables/lists_ngpts.bin has an unexpected table header"); }
                return -1;
            }
            for (int i = 0; i < cnt[k]; i++, p += 3) {
                /* the class lookup of the pass takes |x + cx'| and |z + cz'| >> 8 with cx', cz' in -1,023 .. 1,024: an entry beyond 7,167 would leave the 32 by 32 table */
                if (p[0] < -7167 || p[0] > 7167 || p[1] < -7167 || p[1] > 7167) {
                    if (err && errlen) { err[0] = '\0'; core_assets_append(err, errlen, "lists: tables/lists_ngpts.bin has an entry outside the class table's reach"); }
                    return -1;
                }
                t->ng[k].pt[i][0] = p[0]; t->ng[k].pt[i][1] = p[1]; t->ng[k].pt[i][2] = p[2];
            }
        }
    }
    for (int i = 0; i < 21; i++) { t->tiles[i][0] = (int8_t)tiles[2 * i]; t->tiles[i][1] = (int8_t)tiles[2 * i + 1]; }
    for (int i = 0; i < 8; i++) t->limit[i] = (int16_t)(lim[2 * i] | (lim[2 * i + 1] << 8));
    for (int i = 0; i < LISTS_VIS_WORDS; i++) t->vis[i] = (int16_t)(vis[2 * i] | (vis[2 * i + 1] << 8));
    for (int i = 0; i < LISTS_RECORDS; i++) {
        const uint8_t *p = desc + 8 * i;
        t->desc[i].cls = p[0]; t->desc[i].flags = p[1];
        t->desc[i].foot = (uint16_t)(p[2] | (p[3] << 8)); t->desc[i].ref = (uint16_t)(p[4] | (p[5] << 8));
    }
    uint8_t *b = NULL; size_t len = 0;
    if (ld->read(ld->user, "tables/lists_land.bin", &b, &len) != 0) {
        if (err && errlen) { err[0] = '\0'; core_assets_append(err, errlen, "lists: cannot read tables/lists_land.bin"); }
        return -1;
    }
    t->land = (uint8_t *)malloc(len ? len : 1);
    if (!t->land) { ld->release(ld->user, b); if (err && errlen) { err[0] = '\0'; core_assets_append(err, errlen, "lists: out of memory"); } return -1; }
    memcpy(t->land, b, len);
    t->land_len = len;
    ld->release(ld->user, b);
    /* the offsets of the descriptors must lie inside the blob */
    for (int i = 0; i < LISTS_RECORDS; i++) {
        if (t->desc[i].foot != 0xFFFF && t->desc[i].foot + 11u > len) { lists_tables_free(t); if (err && errlen) { err[0] = '\0'; core_assets_append(err, errlen, "lists: footprint offset outside tables/lists_land.bin"); } return -1; }
        if (t->desc[i].ref != 0xFFFF && t->desc[i].ref + 2u > len) { lists_tables_free(t); if (err && errlen) { err[0] = '\0'; core_assets_append(err, errlen, "lists: reference offset outside tables/lists_land.bin"); } return -1; }
    }
    return 0;
}

void lists_tables_free(lists_tables_t *t) { if (t) { free(t->land); t->land = NULL; t->land_len = 0; } }

static unsigned land8(const lists_tables_t *t, size_t off) { return off < t->land_len ? t->land[off] : 0u; }

static int16_t land16(const lists_tables_t *t, size_t off) {
    if (off + 2 > t->land_len) return 0;
    return (int16_t)(t->land[off] | (t->land[off + 1] << 8));
}

const lists_desc_t *lists_desc_of(const lists_t *l, const lists_entry_t *e) {
    static const lists_desc_t none = { 0, 0, 0xFFFF, 0xFFFF };
    if (e->flags & 0x50) return &none;
    unsigned r = (unsigned)e->id + 16u;
    return r < LISTS_RECORDS ? &l->tab->desc[r] : &none;
}

/* ---- setup and the request byte ------------------------------------------------------------------------------- */

void lists_init(lists_t *l, const lists_tables_t *tab, lists_cell_fn cell, void *user) {
    memset(l, 0, sizeof *l);
    l->tab = tab; l->cell = cell; l->cell_user = user;
    l->period = 5;
    l->zoom = 0x80;
}

void lists_set_models(lists_t *l, const lists_models_t *m) { l->models = m; }

void lists_mission_start(lists_t *l) { l->request = 0xFF; l->octant_copy = 0xFF; memset(l->vslot, 0, sizeof l->vslot); l->tail_stale = 0; }
void lists_request(lists_t *l, uint8_t mask) { l->request |= mask; }

/* ---- the scan inputs from a camera ----------------------------------------------------------------------------- */

void lists_scan_from_camera(lists_scan_t *s, const int32_t pos[3], const int32_t eye[3], int32_t yaw) {
    uint32_t px = (uint32_t)pos[0] & 0x1FFFFFFFu, pz = (uint32_t)pos[2] & 0x1FFFFFFFu;
    s->cell_x = (uint8_t)(px >> 22); s->cell_z = (uint8_t)(pz >> 22);
    s->sub = (uint8_t)(4 * (3 - ((pz >> 20) & 3)) + (3 - ((px >> 20) & 3)));
    s->quad = (uint8_t)(4 * (3 - (s->cell_z & 3)) + (3 - (s->cell_x & 3)));
    int32_t y = yaw % 28800; if (y < 0) y += 28800;
    s->octant = (uint8_t)((y / 3600) & 7);
    const int32_t ex = eye ? eye[0] : 0, ey = eye ? eye[1] : 0, ez = eye ? eye[2] : 0;
    int32_t lx = -((int32_t)(px & 0x3FFFFF) + ex), lz = -((int32_t)(pz & 0x3FFFFF) + ez);
    s->cam_x = lx; s->cam_z = lz;
    s->off_x = (int16_t)(lx >> 8); s->off_z = (int16_t)(lz >> 8);
    s->cam_y = -(pos[1] + ey);
    s->off_y = s->cam_y >> 8;
}

/* ---- entry positions ---------------------------------------------------------------------------------------------- */

static int pick_lod(const lists_t *l, int32_t X, int32_t Y, int32_t Z) {
    const lists_scan_t *s = &l->scan;
    int32_t xc = i32abs(X + s->off_x) >> 12, zc = i32abs(Z + s->off_z) >> 12, yc = i32abs(Y + s->off_y) >> 11;
    int32_t m = w16(xc); if (w16(zc) > m) m = w16(zc); if (w16(yc) > m) m = w16(yc);
    if (m < 0) m = 0;
    return fx_lod_shift((uint32_t)(m / 2));
}

/* ---- the scan (passes 1 and 2) -------------------------------------------------------------------------------- */

typedef struct { uint8_t kind; uint8_t idx; const lists_item_t *it; } sitem_t;   /* kind 0 terrain, 1 entity, 2 secondary */

static int wrap_cell(int v, int limit) { if (v < 0) return limit; if (v > limit) return 0; return v; }

static const uint8_t *pick_list(const lists_t *l, int tier, unsigned *kq) {
    const lists_tables_t *t = l->tab;
    const lists_scan_t *s = &l->scan;
    unsigned p = (tier == LISTS_TIER_C ? s->sub : s->quad) & 15;
    unsigned k = t->quad[p] & 3, j = t->group[p] & 3;
    *kq = k;
    const uint8_t *blob; size_t len; unsigned idx;
    if (tier == LISTS_TIER_A && -s->cam_y < 0xA000) {
        blob = t->low; len = sizeof t->low; idx = 16 * j + t->subperm[16 * k + (s->sub & 15)];
    } else if (s->gate) {
        blob = t->steep; len = sizeof t->steep; idx = t->group[j];
    } else {
        blob = t->high; len = sizeof t->high; idx = 8 * j + t->octperm[8 * k + (s->octant & 7)];
    }
    if (2 * idx + 2 > len) return NULL;
    unsigned off = be16(blob + 2 * idx);
    return off < len ? blob + off : NULL;
}

static void put_entry(lists_t *l, int tier, unsigned *gen, const lists_entry_t *e) {
    if (*gen < LISTS_KEPT) l->q[tier][*gen] = *e;
    (*gen)++;
}

static void build_tier(lists_t *l, int tier) {
    const lists_tables_t *t = l->tab;
    const lists_scan_t *s = &l->scan;
    const int isC = tier == LISTS_TIER_C;
    const int limit_cell = isC ? 127 : 30;
    const int ox0 = isC ? s->cell_x : s->acell_x, oz0 = isC ? s->cell_z : s->acell_z;
    unsigned k;
    const uint8_t *p = pick_list(l, tier, &k);
    unsigned gen = 0;
    int nruns = 0;
    memset(l->q[tier], 0, sizeof l->q[tier]);
    l->corner[tier][0] = (int32_t)s->cell_x << 22; l->corner[tier][1] = (int32_t)s->cell_z << 22;
    const unsigned phase = s->quad;
    /* the height band decides the limit (section 3) */
    int D = (-s->off_y) >> 11; if (D < 0) D = 0;
    int limit = D >= 4 ? 70 : t->limit[2 * D + (s->gate ? 1 : 0)];
    for (int ent_n = 0; p && ent_n < 14; ent_n++) {
        unsigned tile = p[0];
        if (tile & 0x80) break;
        unsigned nsub = p[1];
        const uint8_t *subs = p + 2;
        p += 2 + nsub;
        if (tile > 20) break;
        unsigned tp = t->tileperm[24 * k + tile];
        if (tp > 20) break;
        int dx = t->tiles[tp][0], dz = t->tiles[tp][1];
        int cx = wrap_cell(ox0 + dx, limit_cell), cz = wrap_cell(oz0 + dz, limit_cell);
        /* the tile label in C cell numbers */
        int lx = s->cell_x + dx, lz = s->cell_z + dz;
        if (lx < 0) lx += 128;
        if (lz < 0) lz += 128;
        uint32_t label = (uint32_t)(((lx & 0xFF) << 8) | (lz & 0xFF));
        /* pass 1: the slots */
        sitem_t slot[16][16];
        unsigned ns[16] = { 0 };
        const lists_item_t *items = NULL;
        int n = l->cell ? l->cell(l->cell_user, (unsigned)tier, (unsigned)cx, (unsigned)cz, &items) : 0;
        for (int i = 0; i < n; i++) {
            unsigned sn = items[i].layer & 15;
            if (ns[sn] < 16) { slot[sn][ns[sn]].kind = 0; slot[sn][ns[sn]].idx = (uint8_t)i; slot[sn][ns[sn]].it = &items[i]; ns[sn]++; }
        }
        if (isC) {
            for (int kind = 0; kind < 2; kind++) {
                const lists_entity_t *arr = kind == 0 ? l->ent : l->sec;
                for (int r = 0; r < LISTS_ENTITIES; r++) {
                    if (!l->pending[kind][r] || !arr[r].active) continue;
                    if (arr[r].cell_x != cx || arr[r].cell_z != cz) continue;
                    l->pending[kind][r] = 0;
                    unsigned sn = arr[r].sub & 15;
                    if (ns[sn] < 16) { slot[sn][ns[sn]].kind = (uint8_t)(1 + kind); slot[sn][ns[sn]].idx = (uint8_t)r; slot[sn][ns[sn]].it = NULL; ns[sn]++; }
                }
            }
        }
        /* pass 2: the listed sub-cells in order */
        for (unsigned u = 0; u < nsub; u++) {
            unsigned sn = t->subperm[16 * k + (subs[u] & 15)] & 15;
            uint32_t acc = 0;
            unsigned first = gen, emitted = 0, terrain_no = 0;
            for (unsigned q = 0; q < ns[sn]; q++) {
                const sitem_t *si = &slot[sn][q];
                lists_entry_t e;
                memset(&e, 0, sizeof e);
                if (si->kind == 0) {
                    const lists_item_t *it = si->it;
                    int32_t X, Z;
                    if (isC) {
                        X = 16384 * dx + 4096 * (3 - (int)(sn & 3)) + it->dx;
                        Z = 16384 * dz + 4096 * (3 - (int)(sn >> 2)) + it->dz;
                    } else {
                        int px = 3 - (int)(phase & 3), pz = 3 - (int)(phase >> 2);
                        /* Tiers A and B (note 04a section 3, correction (c)): the tile origin is a word of the table at 0x2069E (16384 * dx in units of
                         * 4 u), so a tile two cells in +x or +z reads as -32768 (-131072 u) and the sum is a long. */
                        int32_t x4 = w16(16384 * dx) + 4096 * (3 - (int)(sn & 3)) + (int)it->dx - 4096 * px;
                        int32_t z4 = w16(16384 * dz) + 4096 * (3 - (int)(sn >> 2)) + (int)it->dz - 4096 * pz;
                        X = 4 * x4; Z = 4 * z4;
                    }
                    int lod = pick_lod(l, X, 0, Z);
                    terrain_no++;
                    for (unsigned i = 1; i <= terrain_no; i++) acc |= 16u * i;
                    e.id = (uint8_t)(it->id & 0x7F);
                    e.flags = (uint8_t)(((it->id & 0x80) ? 0x80 : 0) | lod);
                    e.x = (int16_t)(X >> lod); e.y = 0; e.z = (int16_t)(Z >> lod);
                    e.key = (acc | sn) << 16 | label;
                } else {
                    const lists_entity_t *en = si->kind == 1 ? &l->ent[si->idx] : &l->sec[si->idx];
                    int32_t X = 16384 * dx + 4096 * (3 - (int)(sn & 3)), Z = 16384 * dz + 4096 * (3 - (int)(sn >> 2));
                    int lod = pick_lod(l, X + (en->fine_x & 0xFFF), en->alt8, Z + (en->fine_z & 0xFFF));
                    e.id = si->idx;
                    e.flags = (uint8_t)((si->kind == 1 ? 0x10 : 0x40) | lod);
                    e.x = (int16_t)(X >> lod); e.z = (int16_t)(Z >> lod);
                    e.key = 0x80000000u | ((uint32_t)si->idx << 24);
                    if (si->kind == 1) l->bit2[si->idx] = 0;
                }
                e.countdown = (int8_t)((4 - (int)(gen & 3)) & 3);
                put_entry(l, tier, &gen, &e);
                emitted++;
            }
            if (isC && emitted >= 2 && nruns < LISTS_MAX_RUNS && first < LISTS_KEPT) {
                l->run[nruns].first = (uint8_t)first; l->run[nruns].count = (uint8_t)emitted; nruns++;
            }
        }
    }
    l->gen[tier] = (uint16_t)gen;
    l->kept[tier] = (uint8_t)(gen < LISTS_KEPT ? gen : LISTS_KEPT);
    l->start[tier] = gen > (unsigned)limit ? (uint16_t)(gen - (unsigned)limit) : 0;
    if (isC) {
        l->nruns = (uint8_t)nruns;
        l->cursor = (uint8_t)nruns;
        l->emitted_c = (uint16_t)gen;
        l->built = 1;
    }
}

int lists_footprint(const lists_tables_t *t, unsigned desc, int ox, int oz, int16_t box[5], uint8_t *nnodes, lists_node_t node[4]) {
    if (!t || desc >= LISTS_RECORDS || !(t->desc[desc].flags & 1) || t->desc[desc].foot == 0xFFFF) return -1;
    size_t p = t->desc[desc].foot;
    box[0] = land16(t, p);
    box[1] = w16(land16(t, p + 2) + ox); box[2] = w16(land16(t, p + 4) + ox);
    box[3] = w16(land16(t, p + 6) + oz); box[4] = w16(land16(t, p + 8) + oz);
    unsigned nn = land8(t, p + 10);
    p += 11;
    if (nn > 4) nn = 4;
    *nnodes = (uint8_t)nn;
    for (unsigned n = 0; n < nn; n++) {
        node[n].box[0] = land16(t, p);
        node[n].box[1] = w16(land16(t, p + 2) + ox); node[n].box[2] = w16(land16(t, p + 4) + ox);
        node[n].box[3] = w16(land16(t, p + 6) + oz); node[n].box[4] = w16(land16(t, p + 8) + oz);
        unsigned np = land8(t, p + 10);
        p += 11;
        if (np > 8) np = 8;
        node[n].nplanes = (uint8_t)np;
        /* extractor version 26: a node with a count of 0 carries the one plane that follows the count (07 8.2 (4): the first dot product is formed before the count is looked at) */
        for (unsigned k = 0; k < (np ? np : 1u); k++, p += 12) {
            node[n].ref[k][0] = w16(land16(t, p) + ox);
            node[n].ref[k][1] = land16(t, p + 2);
            node[n].ref[k][2] = w16(land16(t, p + 4) + oz);
            for (int c = 0; c < 3; c++) node[n].nrm[k][c] = land16(t, p + 6 + 2 * c);
        }
    }
    return 0;
}

/* ---- the tail of tier C pass 2: direct list and landmark slots (sections 6 and 7) ------------------------------------ */

/* C (note 04a section 7: the block word is masked to 0xFFF, so the original copies the model's bytes at offset 0xFFF for the nine table C items whose nearest block has no footprint, item ids 32-38, 49
 * and 117; the extractor's tables/lists_land.bin carries footprints only, so those bytes are not reproduced): such a slot keeps its ordinal and its cell bytes and has an empty box, so the
 * landmark test never hits it. The secondary objects (note 07 1.5) are filled by the game from entities.c (lists_frame_t.sec). */
static void build_tail(lists_t *l) {
    l->ndirect = 0;
    for (int i = 0; i < LISTS_ENTITIES; i++)
        if (l->ent[i].active && l->bit2[i]) l->direct[l->ndirect++] = (uint8_t)i;
    /* The slot tables are not cleared: an ordinal beyond this build's count keeps its old contents (the collision phase never reads it). */
    int ordinal = 0;
    int last = (int)l->kept[LISTS_TIER_C] - 1;
    unsigned walked = 0;
    for (int i = last; i >= 0 && walked < l->emitted_c && ordinal < LISTS_LANDMARK_SLOTS; i--, walked++) {
        const lists_entry_t *e = &l->q[LISTS_TIER_C][i];
        if (e->flags & 0x50) continue;
        const lists_desc_t *d = lists_desc_of(l, e);
        l->slot[ordinal].used = 1; l->slot[ordinal].valid = 0;   /* the pointer table entry is taken; a model without footprint leaves it -1 and writes no record */
        if ((d->flags & 1) && d->foot != 0xFFFF) {
            l->slot[ordinal].cell_x = (uint8_t)(e->key >> 8);
            l->slot[ordinal].cell_z = (uint8_t)e->key;
            const lists_tables_t *t = l->tab;
            int lod = e->flags & 15;
            int16_t ox = (int16_t)(shl32(e->x, lod) & 0x3FFF), oz = (int16_t)(shl32(e->z, lod) & 0x3FFF);
            l->slot[ordinal].valid = 1;
            lists_footprint(t, (unsigned)(d - t->desc), ox, oz, l->slot[ordinal].box, &l->slot[ordinal].nnodes, l->slot[ordinal].node);
        }
        ordinal++;
    }
    l->nslots = (uint8_t)ordinal;
}

/* ---- the run sort (section 5a) -------------------------------------------------------------------------------------- */

static int16_t entry_key(const lists_t *l, const lists_entry_t *e) {
    if (e->flags & 0x40) return 0x7FFF;
    int lod = e->flags & 15;
    if (e->dist) return w16(shl32(e->dist, lod));
    if (e->flags & 0x10) {
        /* A fresh entity entry (note 04a section 5a): the slot word plus the record's fine offsets (masked to 12 bits, shifted by the LOD) in x and z,
         * and no vertical term: the code adds ((record +0x10 + V:4AB30) >> LOD, then 8 and 4) but it is always zero in reachable states. After the
         * first walk the stored distance word (with its vertical term, section 8) is the key (the branch above). */
        const lists_entity_t *en = &l->ent[e->id & 15];
        int32_t x = e->x + ((en->fine_x & 0xFFF) >> lod), z = e->z + ((en->fine_z & 0xFFF) >> lod);
        int32_t a = w16(w16(x) + (l->scan.off_x >> lod)), c = w16(w16(z) + (l->scan.off_z >> lod));
        if (a < 0) a = w16(-a);
        if (c < 0) c = w16(-c);
        fx_vec_t v = { a >> 4, 0, c >> 4 };
        return w16(shl32(fx_dist(&v), lod));
    }
    fx_cam_off_t st = { l->scan.off_x, l->scan.off_z, l->scan.off_y, lod };
    return w16(shl32(fx_distkey(&st, e->x, e->y, e->z), lod));
}

static void sort_run(lists_t *l, unsigned r) {
    if (r >= l->nruns) return;
    unsigned first = l->run[r].first, cnt = l->run[r].count;
    if (cnt > 22) cnt = 22;
    if (first + cnt > LISTS_KEPT) return;
    lists_entry_t *q = l->q[LISTS_TIER_C];
    int16_t key[22];
    lists_entry_t tmp[22];
    for (unsigned i = 0; i < cnt; i++) { key[i] = entry_key(l, &q[first + i]); tmp[i] = q[first + i]; }
    /* fx_sort (0x213AA, note 03): decreasing key, ties by ascending index, over the signed words; a negative key is never selected, so only the
     * non-negative keys appear in the output and the run's remaining positions are filled from the sorted-index buffer 0x538C8 as the earlier sorts left it.
     * That happens when a key goes negative, a distance above 0x1000000 position units (80,000 ft) in the shifted word: not reachable in play (found by
     * the rig differential at large N). NOTE-QUESTION (note 03, fx_sort): the buffer's initial contents are taken as zero (work RAM) and an index
     * of the stale output at or above the run length is read from the queue slot of that index (the snapshot taken before this sort). */
    unsigned order[22], m = 0;
    for (unsigned i = 0; i < cnt; i++) {
        if (key[i] < 0) continue;
        unsigned j = m++;
        while (j > 0 && key[order[j - 1]] < key[i]) { order[j] = order[j - 1]; j--; }
        order[j] = i;
    }
    for (unsigned i = 0; i < m; i++) l->sort_out[i] = (uint8_t)order[i];
    lists_entry_t snap[LISTS_KEPT];
    if (m < cnt) memcpy(snap, q, sizeof snap);
    for (unsigned i = 0; i < cnt; i++) {
        unsigned idx = i < m ? order[i] : l->sort_out[i];
        q[first + i] = idx < cnt ? tmp[idx] : (first + idx < LISTS_KEPT ? snap[first + idx] : tmp[0]);
    }
}

static void run_sort(lists_t *l, int all) {
    if (!l->built) return;
    if (all) {
        for (int r = (int)l->nruns - 1; r >= 0; r--) sort_run(l, (unsigned)r);
        l->cursor = l->nruns;
        return;
    }
    if (l->cursor == 0) { l->cursor = l->nruns; return; }
    sort_run(l, (unsigned)l->cursor - 1u);
    l->cursor--;
}

/* ---- the landmark reorder (section 5b) -------------------------------------------------------------------------------- */

typedef struct { int32_t x, y, z; } p3_t;

/* position of queue C entry i in the cell of the reference, in u (the reference cell is rcx, rcz) */
static p3_t entry_pos(const lists_t *l, const lists_entry_t *e, int rcx, int rcz) {
    p3_t r;
    if (e->flags & 0x50) {
        const lists_entity_t *en = (e->flags & 0x40) ? &l->sec[e->id & 15] : &l->ent[e->id & 15];
        r.x = 16384 * ((int)en->cell_x - rcx) + (en->fine_x & 0x3FFF);
        r.z = 16384 * ((int)en->cell_z - rcz) + (en->fine_z & 0x3FFF);
        r.y = en->alt8;
        return r;
    }
    int lod = e->flags & 15;
    int ecx = (e->key >> 8) & 0xFF, ecz = e->key & 0xFF;
    int pcx = l->scan.cell_x, pcz = l->scan.cell_z;
    r.x = shl32(e->x, lod) - 16384 * (ecx - pcx) + 16384 * (ecx - rcx);
    r.z = shl32(e->z, lod) - 16384 * (ecz - pcz) + 16384 * (ecz - rcz);
    r.y = 0;
    return r;
}

static void landmark_reorder(lists_t *l) {
    lists_entry_t *q = l->q[LISTS_TIER_C];
    int n = l->kept[LISTS_TIER_C];
    if (n <= 1) return;
    int ref = -1;
    for (int i = n - 1; i >= 0; i--) {
        const lists_desc_t *d = lists_desc_of(l, &q[i]);
        if ((d->flags & 2) && d->ref != 0xFFFF && !(q[i].flags & 0x50)) { ref = i; break; }
    }
    if (ref < 0) return;
    l->ref_index = (uint8_t)ref;
    const lists_desc_t *rd = lists_desc_of(l, &q[ref]);
    const lists_tables_t *t = l->tab;
    int rcx = (q[ref].key >> 8) & 0xFF, rcz = q[ref].key & 0xFF;
    int pcx = l->scan.cell_x, pcz = l->scan.cell_z;
    p3_t rp = entry_pos(l, &q[ref], rcx, rcz);
    int16_t lx = w16(q[ref].x - 16384 * (rcx - pcx)), lz = w16(q[ref].z - 16384 * (rcz - pcz));
    p3_t cam = { -l->scan.off_x + 16384 * (pcx - rcx), -l->scan.off_y, -l->scan.off_z + 16384 * (pcz - rcz) };
    /* the window */
    int s0 = ref;
    while (s0 > 0) {
        p3_t p = entry_pos(l, &q[s0 - 1], rcx, rcz);
        if (i32abs(p.x - rp.x) > 0xC00 || i32abs(p.y - rp.y) > 0xC00 || i32abs(p.z - rp.z) > 0xC00) break;
        s0--;
    }
    int e0 = ref;
    while (e0 + 1 < n) {
        p3_t p = entry_pos(l, &q[e0 + 1], rcx, rcz);
        if (i32abs(p.x - rp.x) > 0xA00 || i32abs(p.y - rp.y) > 0xA00 || i32abs(p.z - rp.z) > 0xA00) break;
        e0++;
    }
    l->win_start = (uint8_t)s0;
    if (e0 - s0 + 1 <= 1) return;
    unsigned c = (unsigned)land16(t, rd->ref) + 1;
    uint8_t mark[LISTS_SLOTS];
    for (int i = s0; i <= e0; i++) {
        if (i == ref) continue;
        mark[i] = 2;   /* behind */
        /* NOTE-GAP: note 04a section 5b: an entity or secondary entry is tested through other routines, "with a shortcut when bit 6 or 7 of record byte +4
         * is set", decoded from the listing only. The port reads the shortcut as "in front" (an aircraft over the deck is drawn after the carrier) and
         * places an entity by its record's cell and fine words; only terrain items qualify as the reference (the note: item ids 42, 70, 88). */
        if ((q[i].flags & 0x10) && (l->ent[q[i].id & 15].b4 & 0xC0)) { mark[i] = 1; continue; }
        p3_t p = entry_pos(l, &q[i], rcx, rcz);
        for (unsigned k = 0; k < c && k < 12; k++) {
            size_t o = (size_t)rd->ref + 2 + 12 * (size_t)k;
            int16_t pt[3] = { land16(t, o), land16(t, o + 2), land16(t, o + 4) }, nm[3] = { land16(t, o + 6), land16(t, o + 8), land16(t, o + 10) };
            int16_t ppx = w16(pt[0] + lx), ppz = w16(pt[2] + lz);
            /* Note 04a section 5, step 4: "the plane y is ignored": the reference point's y is not subtracted; the normal's y component multiplies the height
             * itself. Equal to the original in the rig over 2,000 states next to the landmarks. */
            int32_t cs = (int32_t)nm[0] * w16(cam.x - ppx) + (int32_t)nm[1] * w16(cam.y) + (int32_t)nm[2] * w16(cam.z - ppz);
            int32_t es = (int32_t)nm[0] * w16(p.x - ppx) + (int32_t)nm[1] * w16(p.y) + (int32_t)nm[2] * w16(p.z - ppz);
            if (cs >= 0 && es >= 0) { mark[i] = 1; break; }
        }
    }
    lists_entry_t tmp[LISTS_SLOTS];
    int m = 0;
    for (int i = s0; i <= e0; i++) if (i != ref && mark[i] == 2) tmp[m++] = q[i];
    tmp[m++] = q[ref];
    int newref = s0 + m - 1;
    for (int i = s0; i <= e0; i++) if (i != ref && mark[i] == 1) tmp[m++] = q[i];
    for (int i = 0; i < m; i++) { if (memcmp(&q[s0 + i], &tmp[i], sizeof tmp[i])) { l->reorders++; break; } }
    for (int i = 0; i < m; i++) q[s0 + i] = tmp[i];
    l->ref_index = (uint8_t)newref;
}

/* ---- the builder (0x1F770) ----------------------------------------------------------------------------------------- */

void lists_builder(lists_t *l, uint8_t request) {
    l->request |= request;
    if (l->scan.cam_y < -0x8000000) return;
    for (int k = 0; k < 2; k++) memset(l->pending[k], 1, LISTS_ENTITIES);
    uint8_t req = l->request;
    if (req) {
        /* Note 04a section 2: the builder takes the A/B scan origin V:4AA00/02 from the cell words of the viewed record (the record at V:4A996),
         * shifted right by 2; in a cockpit view that is the scan cell (V:4AA04/06 >> 2), in an external view the cell of the camera (bits 24 to 28 of
         * the camera longs, the scan cell lists_scan_from_camera derives). */
        l->scan.acell_x = (uint8_t)(l->scan.cell_x >> 2); l->scan.acell_z = (uint8_t)(l->scan.cell_z >> 2);
        if (req & 1) build_tier(l, LISTS_TIER_A);
        if (req & 2) build_tier(l, LISTS_TIER_B);
        if (req & 8) { build_tier(l, LISTS_TIER_C); build_tail(l); }
        l->request = 0;
    }
    run_sort(l, req != 0);   /* the sort tests V:4A85D, set at the end of tier C pass 2 of this very call: the first build is sorted too (note 04a section 5a) */
    landmark_reorder(l);
}

/* ---- the walk (section 8) and the drawer's result (section 8b) --------------------------------------------------------------- */

/* The 0x21C34 descriptors skip the distance refresh and the invisible-entry countdown in queues A and B only; in queue C they are walked like the rest
 * (note 04a section 8 step 4 and section 8b rule 1(c); verified against the rig walkers). */

/* The distance function with the long middle term: a and c are the words, mid the long already shifted right by 8 (the entity form of fx_distkey). */
static uint16_t dist_with_mid(const lists_t *l, int32_t wx, int32_t wz, int32_t mid, int lod) {
    const lists_scan_t *s = &l->scan;
    int32_t a = w16(w16(wx) + (s->off_x >> lod)), c = w16(w16(wz) + (s->off_z >> lod));
    int64_t b = mid;   /* V:4ABF4 already contains the camera height: no further camera term (note 04a section 8, entity entries) */
    if (a < 0) a = w16(-a);
    if (b < 0) b = -b;
    if (c < 0) c = w16(-c);
    if (b >= 0x7FFF0) return 0x7FFF;
    fx_vec_t v = { a >> 4, (int32_t)(b >> 4), c >> 4 };
    return (uint16_t)fx_dist(&v);
}

/* The distance word of an entity entry from the record's current position (0x1FFC6, note 04a section 8): x and z words are the entry word
 * shifted left by 8 plus the record long (+0x14, +0x1C) masked to 20 bits and shifted right by the LOD, then shifted right by 8; the middle term is
 * V:4ABF4 = (record +0x18 plus V:4AB1E) shifted right by the LOD, shifted right by 8. */
static uint16_t entity_dist(const lists_t *l, int32_t ex, int32_t ez, const lists_entity_t *rec, int lod) {
    int32_t wx = shl32(w16(ex), 8) + (int32_t)(((uint32_t)rec->pos[0] & 0xFFFFFu) >> lod);
    int32_t wz = shl32(w16(ez), 8) + (int32_t)(((uint32_t)rec->pos[2] & 0xFFFFFu) >> lod);
    int32_t v4abf4 = (int32_t)((uint32_t)rec->pos[1] + (uint32_t)l->scan.cam_y) >> lod;
    return dist_with_mid(l, wx >> 8, wz >> 8, v4abf4 >> 8, lod);
}

/* The scaled distance of 8b: the distance word times the zoom (not applied when V:4A895 is set), as a signed word. */
int16_t lists_scaled_distance(uint16_t dist, uint16_t zoom, int fixed) {
    int32_t d1 = dist;
    if (!fixed) {
        uint32_t d3 = (uint32_t)(int32_t)(int16_t)zoom << 8;
        uint32_t q = d3 / 0x80u;
        uint32_t d3q = q <= 0xFFFFu ? q : (d3 & 0xFFFFu);   /* a 32 by 16 division that overflows leaves the register: its low word */
        d1 = (int32_t)((uint32_t)d1 * d3q) >> 8;
    }
    return w16(d1);
}
static int16_t scaled_distance(const lists_t *l, uint16_t dist) { return lists_scaled_distance(dist, l->zoom, l->zoom_fixed); }

/* The normaliser 0x29C0E for the length 256 and a horizontal vector (dx, 0, dz): scales the vector to length 256 with the routine's approximate length (fx_dist),
 * a shift found by quadrupling, a division with a 16-bit quotient (an overflow leaves the register: its low word) and an arithmetic shift. The shape of the
 * routine is note 04a 8b P4 (verified bit for bit against the original, 3,000 random vectors), and the walk family of tests/rig/test_lists_diff.c checks it
 * through the pre-test. The quadrupling loop below is written as P4 states it. */
static void norm256(int32_t dx, int32_t dz, int32_t *ox, int32_t *oz) {
    int32_t ax = w16(dx), az = w16(dz);
    if (ax < 0) ax = w16(-ax);
    if (az < 0) az = w16(-az);
    fx_vec_t v = { ax, 0, az };
    int32_t d1 = fx_dist(&v);
    if ((d1 & 0xFFFF) == 0) { *ox = *oz = 0; return; }
    int sh = 8;
    uint32_t l = 0x100;
    while (!((int32_t)l > d1)) { l <<= 2; sh += 2; }
    for (;;) {
        l >>= 2; sh -= 2;
        if (sh > 1) { if ((int32_t)l <= d1) break; continue; }
        break;
    }
    l <<= 2; sh += 2;
    l <<= 8;
    uint32_t q = l / (uint32_t)(d1 & 0xFFFF);
    int32_t mm = w16((int32_t)(q <= 0xFFFFu ? q : (l & 0xFFFFu)));
    *ox = (int32_t)(((int64_t)mm * w16(dx)) >> sh);
    *oz = (int32_t)(((int64_t)mm * w16(dz)) >> sh);
}

/* rule 6: the visibility pre-test of the flat blocks with flag bits 0x42; returns 1 when it rejects. `level` is the drawn block and first_vertex the slot of the first of the two
 * consecutive flat vertices it reads (0 for rule 6; op 31 names a byte offset into the block's vertex list, the pair starting at vertex offset/4, note 03 "The block that op 31 names"),
 * ex and ez the entry's position words, lod its shift, sd the scaled distance. */
int lists_pretest_rejects(const lists_t *l, unsigned model, unsigned level, unsigned lod, int16_t ex, int16_t ez, int16_t sd, unsigned first_vertex) {
    const lists_scan_t *s = &l->scan;
    int16_t x0, z0, x1, z1;
    if (!l->models || l->models->vertex_xz(l->models->user, model, level, first_vertex, &x0, &z0) != 0 || l->models->vertex_xz(l->models->user, model, level, first_vertex + 1, &x1, &z1) != 0) return 0;
    int32_t ox, oz, cx, cz;
    norm256(w16(x1 - x0), w16(z1 - z0), &ox, &oz);
    /* the horizontal vector from the object to the camera: the negated camera word minus the entry's position word (the camera words are the
     * offsets shifted right by the LOD) */
    norm256(w16(-(s->off_x >> lod) - ex), w16(-(s->off_z >> lod) - ez), &cx, &cz);
    int32_t d2 = ox * cx + oz * cz;
    if (d2 < 0) d2 = -d2;
    d2 >>= 4;
    const int first = s->off_y > -0x80;
    const int16_t *tab = first ? l->tab->vis : l->tab->vis + 12;
    int idx = asr16(sd, 4);
    if (idx > 10) idx = 11;
    int32_t minimum;
    if (idx >= 0) minimum = tab[idx];
    else if (!first && idx >= -12) minimum = tab[idx];   /* a scaled distance of 0x8000 or more: the read goes before the table; for the second table those words are the first table's, in reverse (note 04a P5) */
    else if (first && idx >= -5) minimum = 0x7FFF;       /* the first table is preceded by code: indices -1 to -5 read large positive words (reject), -6 a negative one (passes) */
    else if (first && idx == -6) minimum = -1;
    else minimum = tab[0];   /* NOTE-GAP: further back the words are code of either sign; unreachable in the shipped data (note 04a P5) */
    return w16(d2) < minimum;
}
static int pretest_rejects(const lists_t *l, const lists_walk_t *w, unsigned model, const lists_level_t *lv, int16_t sd) {
    const lists_entry_t *e = &l->q[w->tier][w->index];
    return lists_pretest_rejects(l, model, lv->level, w->lod, e->x, e->z, sd, 0);
}

/* rule 5 for the item id of a terrain entry: does queue C hold it (zoom below 0x80, queues A and B).
 * NOTE-QUESTION (note 04a rule 5): the scan compares the id word of every queue C entry, entity and secondary entries included (their id is a record index); the
 * note does not say whether the original filters them, and an entity record index equal to a terrain item id would count here. Not filtered. */
static int in_queue_c(const lists_t *l, unsigned id) {
    for (unsigned i = 0; i < l->kept[LISTS_TIER_C]; i++) if (l->q[LISTS_TIER_C][i].id == id) return 1;
    return 0;
}

/* The pitch-band-dependent variant of the player's own entry (note 04a section 8a, tests 2 to 9). */
/* NOT MODELLED (E2 and the template 10 alias, note 04a 8a): the viewed record of the port is always the player, template 3 or 4, whose alternate list is the
 * three-item list with its own near body block; templates 0 and 1 (an alternate list that starts with 0xFFFF: test 9 returns 0 at once, tests 6 to 8 fail the level
 * choice) and template 10 (the cockpit variant draws the body block) cannot be the viewed record, so neither exit is reachable. */
static int player_variant(const lists_t *l, const lists_entity_t *rec, unsigned id, int lod) {
    if (l->view.external) return LISTS_VARIANT_NORMAL;            /* test 2 */
    if ((int)id != l->view.viewed) return LISTS_VARIANT_NORMAL;   /* test 3: the record is not the viewed one (V:4AA6E differs from V:4A996) */
    if (l->view.head >= 3 && l->view.head <= 9) return LISTS_VARIANT_NORMAL;   /* test 4 */
    if (rec->alt_same) return LISTS_VARIANT_NORMAL;               /* test 5 */
    if (l->view.leave_external) return LISTS_VARIANT_NEAR_BODY;   /* test 6 */
    if (l->pitch_band == 3) return LISTS_VARIANT_NEAR_BODY;       /* test 7 */
    if ((l->pitch_band == 1 || l->pitch_band == 2) && (l->scan.off_y >> lod) >= -32) return LISTS_VARIANT_NEAR_BODY;   /* test 8: the eye measure E */
    return LISTS_VARIANT_NOTHING;                                 /* test 9 */
}

/* P7's exception and the sign of the op 4 handler (note 03, "Op 4" table; steps in order): (1) a record whose byte +4 has exactly one of bits 6 and 7 set: closed; (2) both
 * set: the height is V:4AB1E minus D, shifted right by the LOD, D = -(record word +0x4E) * 256, and V:4AB1E at or above D closes the gate; (3) a height below -0x100000
 * (5,120 ft): the handler returns -1; (4) an external view or the pitch band 3: open; (5) the viewed record's own entry: band 0, or height below -0x2000: closed; (6) another
 * record whose class byte is 0x30 or 0x14, or a height at or above -0x10000: open; (7) otherwise bands 0 and 1: closed, band 2: closed when the height is below -0xA0000.
 * Returns 0 open, 1 closed (returns 1 without drawing), 2 the handler returns -1. */
static int op4_state(const lists_t *l, const lists_entity_t *rec, unsigned ri, int lod) {
    int bits = ((rec->b4 >> 6) & 1) + ((rec->b4 >> 7) & 1);
    if (bits == 1) return 1;
    int32_t h = l->scan.cam_y;
    if (bits == 2) {
        int32_t D = -(int32_t)((uint32_t)(int32_t)rec->w4e << 8);
        if (l->scan.cam_y >= D) return 1;
        h = (int32_t)(((int64_t)l->scan.cam_y - D) >> lod);
    }
    if (h < -0x100000) return 2;
    if (l->view.external || l->pitch_band == 3) return 0;
    if ((int)ri == l->view.viewed) return (l->pitch_band == 0 || h < -0x2000) ? 1 : 0;
    if (rec->cls == 0x30 || rec->cls == 0x14 || h >= -0x10000) return 0;
    return (l->pitch_band <= 1 || (l->pitch_band == 2 && h < -0xA0000)) ? 1 : 0;
}

/* The vertex slots the draw writes at the record's +0xA4 (note 07 8.2a, note 05 `+0xA4`): slot k = the drawn block's vertex k rotated by the record's matrix, (M . v) >> 8 with M the matrix >> 6.
 * Only the vertices of the block itself are stored (the leading run); a block with fewer vertices than the slots leaves the others as they were. */
static void store_vslots(lists_t *l, unsigned ri, const lists_entity_t *rec, unsigned level) {
    const lists_models_t *mi = l->models;
    if (!mi || !mi->vertex || ri >= LISTS_ENTITIES) return;
    int32_t mb[9];
    for (int i = 0; i < 9; i++) mb[i] = rec->mat_b[i] >> 6;
    unsigned n = LISTS_VSLOTS;   /* the rule's count: b8 + 1 slots when b8 = b9, b8 when b8 > b9 (07 8.10 rule 5) */
    if (mi->vslot_count) { n = mi->vslot_count(mi->user, rec->model, level); if (n > LISTS_VSLOTS) n = LISTS_VSLOTS; }
    for (unsigned k = 0; k < n; k++) {
        int32_t v[3];
        if (mi->vertex(mi->user, rec->model, level, k, v) != 0) { if (mi->vslot_count) continue; else break; }   /* the block's own vertices are the leading run of the level's array; the slots after it are synthesised (the record copy's) or loaded by items for the polygons only (rig: test_slots_diff) */
        for (int i = 0; i < 3; i++) {
            uint32_t s = 0;
            for (int j = 0; j < 3; j++) s += (uint32_t)(mb[3 * i + j] * v[j]);
            l->vslot[ri][k][i] = (int16_t)(uint16_t)(((int32_t)s >> 8) & 0xFFFF);
        }
    }
}


/* ---- the record-copy half of the vertex synthesis (note 03 "Vertex synthesis", ops 42 to 44, 56, 59, 63 to 65) ---------------------------------------------------------------------------
 * These operations first run their fragment on the object record's own slot array (+0xA4: the rotated block vertices the draw stored), which no polygon uses. Every add, subtract and shift
 * is a 16-bit word operation, every shift an arithmetic shift right; the slot b + k is read where it stands (a slot an earlier operation of the same draw wrote is read back). The results
 * for the slots 100 and 110 (ops 42 and 59) are not stored: they lie past the record's 58 slots and the original leaves the memory there alone (rig: test_slots_diff, the record after). */
typedef struct { lists_t *l; unsigned ri; int b; } rs_t;
static int16_t *rs_word(const rs_t *c, int slot, int axis) {
    if (slot < 0 || slot >= LISTS_VSLOTS) return NULL;   /* the slots 100 and 110 that ops 42 and 59 name lie past the 58 of the record: the original writes nothing there (rig: the record after is untouched) */
    return &c->l->vslot[c->ri][slot][axis];
}
static int16_t rs_get(const rs_t *c, int k, int a) { const int16_t *p = rs_word(c, c->b + k, a); return p ? *p : 0; }
static void rs_put(const rs_t *c, int k, int a, int v) { int16_t *p = rs_word(c, c->b + k, a); if (p) *p = (int16_t)v; }
#define RV(k) rs_get(c, (k), a)
#define RP(k, e) rs_put(c, (k), a, (e))
#define RH(x) ((int16_t)(((int16_t)(x)) >> 1))
#define RQ(x) ((int16_t)(((int16_t)(x)) >> 2))
static void rs_apply(rs_t *c, int op) {
    for (int a = 0; a < 3; a++) {
        switch (op) {
        case 44:
            RP(5, RV(0) + RH(RV(1) - RV(0)));
            RP(6, RV(0) + RH(RH(RV(1) - RV(0))));
            break;
        case 43:
            RP(10, RV(3) + RV(4) - RV(6));
            RP(11, RH(RV(3) + RV(4)) + RQ(RV(5) - RV(3)));
            RP(12, RH(RV(11) + RV(0)));
            RP(13, RV(7) + RQ(RV(9) - RV(7)) + RH(RQ(RV(9) - RV(7))));
            RP(14, RV(4) + RV(5) - RV(3));
            RP(15, RV(3) + RV(4) - RV(7));
            break;
        case 42:
            RP(17, RV(3) + RV(4) - RV(8));
            RP(18, RV(2) - RV(1) + RV(6));
            RP(19, RH(RV(6) - RV(1)) + RV(7));
            RP(20, RH(RV(6) - RV(1)) + RV(16));
            RP(21, RH(RV(6) - RV(1)) + RH(RV(3) + RV(4)) + 2 * (RV(6) - RV(1)));
            RP(22, RH(RV(21) + RV(0)));
            RP(23, RV(4) + RV(5) - RV(8));
            RP(24, RH(RV(8) + RV(11)));
            RP(25, RV(4) + RV(5) - RV(3));
            RP(26, RV(6) + RV(9) - RV(1));
            RP(27, 2 * RV(6) - RV(1));
            RP(28, RV(2) - 2 * RV(1) + 2 * RV(6));
            RP(29, RH(RV(6) - RV(1)) + RV(0));
            RP(100, RH(RH(RV(13) + RV(14)) + RV(10)));
            break;
        case 59:
            RP(21, RV(6) - RV(7) + RV(10));
            RP(22, RH(RV(3) + RV(4)));
            RP(23, 2 * RH(RV(3) + RV(4)) - RV(9));
            RP(24, 2 * RH(RV(3) + RV(4)) - RV(8));
            RP(25, RV(4) + RV(5) - RV(3));
            RP(26, RH(RH(RV(4) - RV(3) + 2 * RV(5)) + RV(5)));
            RP(27, RH(RH(RV(4) - RV(3) + 2 * RV(5)) + RV(4) - RV(3) + RV(5)));
            RP(28, RH(4 * RH(RV(3) + RV(4)) - 2 * RV(3) + 2 * RV(5) - RV(8) - RV(9)));
            RP(29, RH(RV(1) + RV(2)));
            RP(30, RV(6) - RV(7) + RV(12));
            RP(31, RV(6) - RV(7) + RV(14));
            RP(32, RH(RV(6) - RV(7)) + RV(1) + RV(6) - RV(7));
            RP(33, RH(RV(6) - RV(7)) + RV(2) + RV(6) - RV(7));
            RP(34, RH(RV(6) - RV(7)) + RV(11) + 2 * (RV(6) - RV(7)));
            RP(35, RH(RV(6) - RV(7)) + RV(13) + 2 * (RV(6) - RV(7)));
            RP(36, RH(RV(6) - RV(7)) + RV(1) + 2 * (RV(6) - RV(7)));
            RP(37, RH(RV(6) - RV(7)) + RV(2) + 2 * (RV(6) - RV(7)));
            RP(38, 2 * RH(RV(3) + RV(4)) - RV(3) + RV(5) - RV(9));
            RP(39, 2 * RH(RV(3) + RV(4)) - RV(3) + RV(5) - RV(8));
            RP(110, RH(RH(RV(18) + RV(19)) + RV(16)));
            break;
        case 56: {   /* all results from the slots as they stood: none of the written slots is read */
            const int16_t v3 = RV(3), v4 = RV(4);
            RP(15, 2 * v4 - RV(7)); RP(16, 2 * v4 - RV(8)); RP(17, 2 * v4 - RV(9)); RP(18, 2 * v4 - RV(10));
            RP(19, 2 * v3 - RV(11)); RP(20, 2 * v3 - RV(12)); RP(21, 2 * v3 - RV(13)); RP(22, 2 * v3 - RV(14));
            break;
        }
        case 63:
            RP(16, RV(0) - RV(1) + RV(2)); RP(17, RV(3) - RV(4) + RV(5)); RP(18, RV(7) - RV(8) + RV(9));
            RP(19, RV(10) - RV(11) + RV(12)); RP(20, RV(13) - RV(14) + RV(15));
            break;
        case 64: {
            const int16_t v0 = RV(0), v1 = RV(1), v2 = RV(2), m = RH(v1 + v2);
            RP(23, v1 + v2 - v0); RP(24, m); RP(25, v0 - RH(m - v0)); RP(26, v0 - RH(m - v0) - RH(m - v1));
            RP(27, v1 - RH(m - v1)); RP(28, v1 - RH(m - v0) - RH(m - v1));
            break;
        }
        case 65:
            RP(23, RV(1) + RV(2) - RV(0)); RP(24, RV(2) + RV(5) - RV(0));
            break;
        default: break;
        }
    }
}
#undef RV
#undef RP
#undef RH
#undef RQ

/* The cone handlers (note 07 11.5): the rescue pod and the ejection seat (models 2 and 13, the handler 0x2699E) and the parachute (model 11, 0x26A3E) start their detail list by the phase
 * p = the record's counter +0x4C: p of 5 and up the model's own list (0); 3 and 4 the list 1; 1 and 2 the list 2; 0 the list 3 (the parachute: the list 4 when the low nibble of +0x7C is 0); below 0 the list 4. */
static unsigned cone_list(const lists_entity_t *rec) {
    if (rec->model != 2 && rec->model != 13 && rec->model != 11) return 0;
    const int p = rec->phase;
    if (p >= 5) return 0;
    if (p >= 3) return 1;
    if (p >= 1) return 2;
    if (p == 0) return (rec->model == 11 && (rec->b7c & 0x0F) == 0) ? 4u : 3u;
    return 4;
}

/* The vertex stage's write at a dying record's counter 14 (0x21F1C-0x21F62): d = (scaled distance << V:4AB70) >> 1 as a long; a word d of 0 or less (the word test) gives 0x78, else the divide
 * 0x10000 / d (unsigned word divisor): the long left in D2 is remainder:quotient, and it is kept only when it is at most 0x78 as a long (an exact quotient up to 0x78), else 0x78; an overflowing
 * divide (d = 1) leaves 0x10000, so 0x78. V:4A921 = that value, V:4A922 = half of it, V:4A870 = 4 - (that value >> 5). */
void lists_explosion_words(int16_t scaled_dist, unsigned shift, uint8_t out[3]) {
    int32_t d = ((int32_t)scaled_dist << (shift & 63)) >> 1;   /* the long shifted left by the shift word, then right by 1 (arithmetic) */
    uint32_t v = 0x78;
    if ((int16_t)d > 0) {
        const uint32_t div = (uint16_t)d, num = 0x10000;
        if (num / div <= 0xFFFF) { const uint32_t dr = ((num % div) << 16) | (num / div); if ((int32_t)dr <= 0x78) v = dr; }
    }
    out[0] = (uint8_t)v;
    out[1] = (uint8_t)((int16_t)v >> 1);
    out[2] = (uint8_t)(4 - (int16_t)((int16_t)v >> 5));
}

/* The common drawer, the part that decides the result (note 04a section 8b, rules in order). Returns the drawer's return word; sets the walk record's
 * drew, variant and polys. *completed is set when the drawer reached the part id store (section 8a). */
#define EXIT(R, V) do { l->rule_hits[R]++; return (V); } while (0)
static int run_drawer_core(lists_t *l, unsigned wi, int *completed) {
    lists_walk_t *w = &l->walk[wi];
    const lists_scan_t *s = &l->scan;
    const lists_models_t *mi = l->models;
    *completed = 0;
    /* E1 (note 04a 8b) belongs to the entity routine 0x21C44 only. A secondary entry (an effect object, note 07 11.1) has the descriptor of its id word 0x11 to 0x13 (the records 33 to 35, the
     * draw routine 0x21D0C), so it takes the exits of the common drawer below with the model id word + 16 (models 33 to 35). */
    const int tierC = w->tier == LISTS_TIER_C || w->tier == LISTS_TIER_DIRECT;
    const int16_t sd = scaled_distance(l, w->dist);
    if (w->kind == 1) {
        const unsigned ri = w->tier == LISTS_TIER_DIRECT ? (unsigned)(w->index & 15) : (unsigned)(l->q[w->tier][w->index].id & 15);   /* the record index */
        const lists_entity_t *rec = &l->ent[ri];
        if (!rec->active) EXIT(0, 0);
        w->variant = (uint8_t)player_variant(l, rec, ri, w->lod);
        { int o4 = op4_state(l, rec, ri, w->lod); w->op4_closed = (uint8_t)(o4 == 1); w->op4_neg = (uint8_t)(o4 == 2); }
        if (w->variant == LISTS_VARIANT_NEAR_BODY) {   /* the alternate list's near level: its program (vertex synthesis, then op 4) runs through the draw layer; the part id is 0 */
            l->part_id[ri] = 0x00; *completed = 1;
            if (mi) {
                lists_level_t nlv; memset(&nlv, 0, sizeof nlv);
                lists_prims_t pr = { .polys = 0, .fv_ok = 1 };
                /* The slots: the near body block is the same block as the nearest normal level (same offset from the model base, note 04a 8a), so the vertex stage of 0x21C44 stores the same
                 * vertices as for that level, under the same rules as the normal path below (block flag bit 0, not a dying record with a counter of 12 or less); the program's record-copy
                 * operations (59 or 42) then run on them. The nearest normal level is the pick at the smallest scaled distance. */
                int near_stored = 0;
                lists_level_t nl;
                if (mi->pick && mi->pick(mi->user, rec->model, w->lod, INT16_MIN, &nl) == 0 && nl.found && nl.present && (nl.block_flags & 1) && !((rec->damage & GAME_DMG_DYING) && rec->phase <= 12)) {
                    store_vslots(l, ri, rec, nl.level); near_stored = 1;
                }
                if (mi->prims(mi->user, l, wi, &nlv, &pr) == 0) {
                    w->drew = 1; w->polys = (int16_t)pr.polys; w->decider = pr.decider; w->toggle_in = pr.toggle_in; w->alt_in = pr.alt_in; memcpy(w->pbuf_in, pr.pbuf_in, sizeof w->pbuf_in);
                    if (near_stored) { rs_t rc = { l, ri, 0 }; for (int k = 0; k < pr.rs_n && k < LISTS_RS_MAX; k++) { rc.b = pr.rs_base[k]; rs_apply(&rc, pr.rs_op[k]); } }
                }
                EXIT(0, w->op4_neg ? -1 : (pr.polys > 0 ? 1 : 0));   /* op 4 returns +1 at every exit of its gate, but -1 above 5,120 ft (it abandons the program; the walker stores -1) */
            }
            EXIT(0, w->op4_neg ? -1 : 1);   /* without a model side */
        }
        if (w->variant == LISTS_VARIANT_NOTHING) {
            /* the cockpit block (test 9 of 04a 8a): part id 0xFF, no program; its vertex stage stores the block's own vertices like any block with flag bit 0 (rig: test_slots_diff, cockpit case) */
            l->part_id[ri] = 0xFF; *completed = 1;
            lists_level_t cl;
            if (mi && mi->pick_cockpit && mi->pick_cockpit(mi->user, rec->model, &cl) == 0 && cl.found && (cl.block_flags & 1) && !((rec->damage & GAME_DMG_DYING) && rec->phase <= 12)) store_vslots(l, ri, rec, cl.level);
            EXIT(0, 0);
        }
        if (!mi) EXIT(0, 1);
        lists_level_t lv;
        int stored = 0;
        w->cone_list = (uint8_t)cone_list(rec);
        if (w->cone_list && mi->pick_cone) {   /* the cone handlers 0x2699E and 0x26A3E start the detail list by the phase (note 07 11.5): a single leaf, no distance test */
            if (mi->pick_cone(mi->user, rec->model, w->cone_list, &lv) != 0 || !lv.found || !lv.present) EXIT(4, 0);
        } else if (mi->pick(mi->user, rec->model, w->lod, sd, &lv) != 0 || !lv.found || !lv.present) EXIT(4, 0);
        if (lv.leaf & 0x2000) { if (tierC) EXIT(5, 1); }
        if (lv.block_flags & 1) {   /* a block of the record kind (flag bit 0): the part id is stored and the vertices go into the record's slots; any other block takes another path of 0x21C44 that writes neither (0x2235C) */
            l->part_id[ri] = (uint8_t)lv.part_id; *completed = 1;
            /* a dying record whose counter is 12 or less leaves the routine's record path for the burst path (0x21F68, 0x22772) before the vertex loop: its slots keep their last contents (rig: test_slots_diff) */
            if ((rec->damage & GAME_DMG_DYING) && rec->phase == 14) { lists_explosion_words(sd, w->lod, l->dist_words); l->dist_words_new = 1; }   /* 0x21F20: the counter 14 writes the explosion's words for the sound */
            if ((rec->damage & GAME_DMG_DYING) && rec->phase <= 12) {
                /* and no model either (note 07 11.4, 04a 8d; code 0x21F68 -> 0x22772): the jump goes to the entity tail, so the vertex loop and the program do not run and only the burst and
                 * the trail are drawn (the scene's effects). The tail returns the stack frame's result word, which the cut at 0x21F68 left uncleared (the clear is at 0x2260A, after it): the burst's
                 * and the trail's own results are discarded, and the word is, in practice, the previous drawer call's accumulated result at the same depth (note 07 11.4, inferred: tail_stale;
                 * 0 before any call). A declared approximation, as the note says: the exact word is whatever the frame held. */
                w->drew = 0; w->polys = 0;
                EXIT(0, l->tail_stale);
            }
            store_vslots(l, ri, rec, lv.level); stored = 1;   /* the draw rotates the drawn block's vertices into the record's slots */
        }
        lists_prims_t pr = { .polys = 0, .fv_ok = 1 };
        if (mi->prims(mi->user, l, wi, &lv, &pr) != 0) EXIT(0, 0);
        w->drew = 1; w->polys = (int16_t)pr.polys; w->decider = pr.decider; w->toggle_in = pr.toggle_in; w->alt_in = pr.alt_in; memcpy(w->pbuf_in, pr.pbuf_in, sizeof w->pbuf_in);
        if (stored) {   /* the programs' record-copy operations, in the order they ran (the vertex stage above wrote the block's own vertices) */
            rs_t rc = { l, ri, 0 };
            for (int k = 0; k < pr.rs_n && k < LISTS_RS_MAX; k++) { rc.b = pr.rs_base[k]; rs_apply(&rc, pr.rs_op[k]); }
        }
        EXIT(9, pr.polys > 0 ? 1 : 0);
    }
    /* terrain */
    const lists_entry_t *e = &l->q[w->tier][w->index];
    unsigned model = (unsigned)e->id + 16u;
    int cls;
    if (w->kind == 2) {   /* the effect object's descriptor: the record of its id word */
        model = (unsigned)l->sec[e->id & 15].model + 16u;
        cls = model < LISTS_RECORDS ? l->tab->desc[model].cls : LISTS_CLASS_NONE;
        if (cls == LISTS_CLASS_NONE) EXIT(0, 0);   /* an id word without a descriptor (rule 1a): nothing is drawn */
    } else cls = lists_desc_of(l, e)->cls;
    if (cls == LISTS_CLASS_LOW && s->cam_y >= -0x280000) EXIT(2, 0);                            /* rule 2 */
    if (cls == LISTS_CLASS_TYPEB1 && s->cam_y < -0x380000) EXIT(3, 0);                          /* rule 3 */
    if (cls == LISTS_CLASS_TYPEB2 && s->cam_y < -0x800000) EXIT(3, 0);
    if (!mi) EXIT(0, 1);
    lists_level_t lv;
    if (mi->pick(mi->user, model, w->lod, sd, &lv) != 0 || !lv.found || !lv.present) EXIT(4, 0);   /* rule 4 */
    const int typeb = cls == LISTS_CLASS_TYPEB1 || cls == LISTS_CLASS_TYPEB2;
    if (!typeb) {
        if (lv.leaf & 0x2000) { if (tierC) EXIT(5, 1); }                                         /* rule 5 */
        else if (!tierC) {
            if ((int16_t)l->zoom >= 0x80) EXIT(5, 1);
            if (in_queue_c(l, e->id)) EXIT(5, 1);
        }
        const unsigned bf = lv.block_flags;
        if (s->off_y > -0x140 && !l->v4a924 && (bf & 0x42) == 0x42 && (w->prev_result <= 0 || (w->counter & 3) == 0)) {   /* rule 6 */
            l->rule_hits[1]++;   /* the pre-test ran */
            if (pretest_rejects(l, w, model, &lv, sd)) EXIT(6, 0);
        }
        if ((bf & 2) && s->off_y >= -0x20) {                                                   /* rule 7 */
            int lim = (bf & 4) ? 0x170 : 0x60;
            if (asr16(lim, w->lod) < sd) EXIT(7, 0);
        }
    }
    lists_prims_t pr = { .polys = 0, .fv_ok = 1 };
    if (mi->prims(mi->user, l, wi, &lv, &pr) != 0) EXIT(0, 0);
    if (!typeb && !(lv.leaf & 0x4000) && !(lv.block_flags & 1) && !pr.fv_ok) EXIT(8, 0);         /* rule 8 */
    w->drew = 1; w->polys = (int16_t)pr.polys; w->decider = pr.decider; w->toggle_in = pr.toggle_in; w->alt_in = pr.alt_in; memcpy(w->pbuf_in, pr.pbuf_in, sizeof w->pbuf_in);
    /* rule 9: a terrain stream whose walked path ends in the word 0xFFFF returns 0 even if primitives drew; the type B drawer returns the OR, except that a negative
     * handler return aborts the whole drawer with 0 (status R / C: read from the code, no decided entry reaches it in the rig) */
    if (typeb ? pr.aborted : pr.ends_ffff) EXIT(9, 0);
    EXIT(9, pr.polys > 0 ? 1 : 0);
}

/* The drawer with its stale result word: the word -$7C(a6) of the drawer frame, which the entity and the terrain draws share (both enter at 0x21D0C), is cleared at 0x2260A and ORed by the program
 * walk; a later tail-only call returns it (note 07 11.4). The port records the ACCUMULATED word of a call that reached the program walk (a primitive drew: 1), not the drawer's return word, which differs
 * at the exits of rules 9 and 4. Declared approximation (07 11.4 "in practice"): the cockpit-block variant (never `drew`) and other routines called at the same depth that may overwrite the
 * slot are not modelled; unobservable for entities if nothing reads an entity's walker result (04a 8a). */
static int run_drawer(lists_t *l, unsigned wi, int *completed) {
    l->walk[wi].drew = 0;
    const int r = run_drawer_core(l, wi, completed);
    if (l->walk[wi].drew) l->tail_stale = l->walk[wi].polys > 0 ? 1 : 0;
    return r;
}

#undef EXIT

/* The common drawer for one entity record as the direct list walks it (tests and the rig, tools/m68k-rig test_slots_diff): the record `ri` of l->ent with the LOD and the distance word given.
 * Returns the drawer's return word; *completed as run_drawer. */
int lists_draw_entity(lists_t *l, unsigned ri, unsigned lod, uint16_t dist, int *completed) {
    lists_walk_t *w = &l->walk[0];
    memset(w, 0, sizeof *w);
    w->tier = LISTS_TIER_DIRECT; w->kind = 1; w->index = (uint8_t)ri; w->draw = 1; w->lod = (uint8_t)lod; w->dist = dist;
    l->nwalk = 1;
    return run_drawer(l, 0, completed);
}

/* One entry of queue `tier` at index i in the walk. kind: 0 terrain, 1 entity, 2 secondary object. */
static void walk_queue_entry(lists_t *l, int tier, unsigned i, int kind) {
    lists_walk_t *w = &l->walk[l->nwalk];
    lists_entry_t *e = &l->q[tier][i];
    memset(w, 0, sizeof *w);
    w->tier = (uint8_t)tier; w->kind = (uint8_t)kind; w->index = (uint8_t)i;
    const unsigned wi = l->nwalk++;
    const lists_scan_t *s = &l->scan;
    int lod = e->flags & 15;
    w->lod = (uint8_t)lod;
    int cls = kind == 0 ? lists_desc_of(l, e)->cls : LISTS_CLASS_ENTITY;
    /* Rule 1(a) (status C: no queue entry has such a descriptor): a descriptor whose first word is negative, an unused run-time slot, is skipped in every
     * queue and nothing is stored. READING: the port's class NONE (u8 class 0, the zero record or a negative first word of the descriptor) stands for it. */
    if (kind == 0 && cls == LISTS_CLASS_NONE) return;
    if (cls == LISTS_CLASS_PLAIN) {
        int32_t lim = tier == LISTS_TIER_C ? 0x400000 : 0x380000;
        if (s->cam_y < -lim) return;
    }
    int refresh_ok = 1;
    const int low_skip = cls == LISTS_CLASS_LOW && tier != LISTS_TIER_C;   /* step 4; rule 1(c) */
    if (low_skip) refresh_ok = 0;
    if (refresh_ok) {
        uint32_t sd = (uint32_t)e->dist << lod;
        int refresh = sd < 0x100;
        const unsigned sid = kind == 0 ? e->id : (unsigned)(e->id & 15);   /* the schedule uses the entity's record index as the id */
        if (!refresh && sd < 0x400) refresh = (sid & 1) ? ((l->n & 3) == 2) : ((l->n & 3) == 0);
        if (refresh) {
            if (kind == 0) {
                fx_cam_off_t st = { s->off_x, s->off_z, s->off_y, lod };
                e->dist = (uint16_t)fx_distkey(&st, e->x, e->y, e->z);
            } else {
                const lists_entity_t *rec = kind == 2 ? &l->sec[e->id & 15] : &l->ent[e->id & 15];
                e->dist = entity_dist(l, e->x, e->z, rec, lod);
            }
        }
    }
    w->prev_result = e->result;
    if (kind == 0 && !low_skip && e->result < 0) {
        e->countdown--;
        if (e->countdown >= 0) return;
        e->countdown = (int8_t)l->period;
    }
    e->counter--;
    w->counter = e->counter;
    w->dist = e->dist;
    if (tier == LISTS_TIER_C && (uint16_t)(e->id << 9) == (uint16_t)(l->view.viewed << 9))   /* 0x1FE8C-0x1FEA0: the entry of the viewed record (V:4AA6E = the item id * 0x200 against V:4A996) stores the refreshed distance word
                                                                                              * in an external view and 0x7FFF in the cockpit at V:4ABFA, which the sound routine reads (note 10 5.3, 5.7 item 9) */
        l->v4abfa = l->view.external ? e->dist : 0x7FFF;
    w->draw = 1;
    int completed;
    int r = run_drawer(l, wi, &completed);
    e->result = (int16_t)(r > 0 ? (r > 32767 ? 32767 : r) : -1);
    w->result = e->result;
}

/* The LOD of a direct-list entity (section 6): the largest distance with the table of section 3, x and z from the cell difference. */
static int direct_lod(const lists_t *l, const lists_entity_t *en) {
    const lists_scan_t *s = &l->scan;
    int32_t X = 16384 * ((int)en->cell_x - (int)s->cell_x) + (int32_t)(((uint32_t)en->pos[0] & 0x3FFFFFu) >> 8);
    int32_t Z = 16384 * ((int)en->cell_z - (int)s->cell_z) + (int32_t)(((uint32_t)en->pos[2] & 0x3FFFFFu) >> 8);
    return pick_lod(l, X, en->alt8, Z);
}

static void walk_direct(lists_t *l, unsigned rec_index) {
    lists_walk_t *w = &l->walk[l->nwalk];
    const lists_entity_t *en = &l->ent[rec_index & 15];
    memset(w, 0, sizeof *w);
    w->tier = LISTS_TIER_DIRECT; w->kind = 1; w->index = (uint8_t)rec_index; w->draw = 1;
    const unsigned wi = l->nwalk++;
    int lod = direct_lod(l, en);
    w->lod = (uint8_t)lod;
    /* The direct pass (note 04a section 6) forms the same coordinates from the cell difference (shifted left by 22 plus the low 22 bits, shifted right by the LOD) and chooses its own LOD from the largest distance. */
    int64_t px = ((int64_t)((int)en->cell_x - (int)l->scan.cell_x) * 4194304 + ((int64_t)en->pos[0] & 0x3FFFFF)) >> lod;
    int64_t pz = ((int64_t)((int)en->cell_z - (int)l->scan.cell_z) * 4194304 + ((int64_t)en->pos[2] & 0x3FFFFF)) >> lod;
    int32_t v4abf4 = (int32_t)((uint32_t)en->pos[1] + (uint32_t)l->scan.cam_y) >> lod;
    w->dist = dist_with_mid(l, (int32_t)(px >> 8), (int32_t)(pz >> 8), v4abf4 >> 8, lod);
    int completed;
    run_drawer(l, wi, &completed);
}

/* The height of the near-ground pass (stage [90], 0x2C0EC): drawn when the camera height is at most 2,048 u in pitch band 0, 3,456 u in band 1 and
 * 4,608 u in bands 2 and 3 (the block that holds it is skipped at the top of the altitude range, which lists_frame handles). */
static int near_ground_active(const lists_t *l) {
    int32_t h = -l->scan.off_y;
    int32_t lim = l->pitch_band == 0 ? 2048 : (l->pitch_band == 1 ? 3456 : 4608);
    return h <= lim;
}

/* The pass's projection of one point (step 8 of note 04, "Stage [90]"): every product is 32 bits, every shift arithmetic and every sum truncated to a word. Returns 0 when the point
 * is outside the pyramid (z' > 0 strictly, unlike the clipper's depth >= 0; x' <= z', -x' <= z', y' <= z', -y' <= z', the negations 68000 word negations), else 1 with the pixel (319 - sx, 179 - sy).
 * NOTE-GAP (C): the word negation of x' or y' = -32,768 (which stays -32,768) is implemented as the note states and was not run in the rig at that edge. */
static int ng_project(const int16_t m[9], int16_t X, int16_t Z, int16_t Hs, int16_t *px, int16_t *py) {
    int16_t r[3];
    for (int i = 0; i < 3; i++) {
        int32_t sum = (int32_t)m[3 * i] * X + (int32_t)m[3 * i + 2] * Z;
        int32_t hh = ((int32_t)m[3 * i + 1] * Hs) >> 8;
        r[i] = w16((sum >> 8) + hh);
    }
    const int16_t x = r[0], y = r[1], z = r[2];
    if (z <= 0 || x > z || w16(-x) > z || y > z || w16(-y) > z) return 0;
    int sx = 160 + (int)(((int32_t)x * 160) / z), sy = 90 + (int)(((int32_t)y * 90) / z);
    sx = sx < 0 ? 0 : (sx > 319 ? 319 : sx); sy = sy < 0 ? 0 : (sy > 179 ? 179 : sy);
    *px = (int16_t)(319 - sx); *py = (int16_t)(179 - sy);
    return 1;
}

int lists_nearground(const lists_t *l, const int16_t m[9], int row_limit, lists_speck_t *out, int max) {
    if (!near_ground_active(l)) return 0;
    const lists_tables_t *t = l->tab;
    const int16_t L = w16(l->scan.off_y);           /* the low word of V:4AB30 */
    const int far_flag = l->scan.off_y < -512;      /* the camera is above 512 u */
    const int tab = L >= -128 ? 0 : (L >= -224 ? 1 : 2);
    const int shift = tab == 0 ? 3 : 0;
    /* the lattice offset (word arithmetic): cx' = cx + ((0x400 - cx) & 0xF800) */
    const int16_t cx = l->scan.off_x, cz = l->scan.off_z;
    const int16_t ox = w16(cx + (w16(0x400 - cx) & (int16_t)0xF800)), oz = w16(cz + (w16(0x400 - cz) & (int16_t)0xF800));
    const int16_t Hs = w16(shl32(L, shift));
    int n = 0;
    for (int i = 0; i < t->ng[tab].count; i++) {
        const int16_t *e = t->ng[tab].pt[i];
        const int16_t X = w16(e[0] + ox), Z = w16(e[1] + oz), k = e[2];
        const int ar = (Z < 0 ? w16(-Z) : Z) >> 8, ac = (X < 0 ? w16(-X) : X) >> 8;   /* |Z| >> 8 and |X| >> 8 (word negation) */
        /* the lookup reaches row 19 and column 18 at most with the shipped tables (|x + cx'| is at most 4,800 and |z + cz'| 4,992); lists_tables_load refuses tables whose entries could leave the 32 by 32 table */
        const int c = t->ng_class[ar * 32 + ac];
        if (c > t->ng[tab].limit) continue;
        const int16_t Xs = w16(shl32(X, shift)), Zs = w16(shl32(Z, shift));
        lists_speck_t *s = &out[n];
        int sprite = -1;   /* 0: 1 by 1, 1: 2 by 1 */
        int shape = -1;
        if (k == 1) sprite = 0;
        else if (k == 2) sprite = 1;
        else if (k == -12 || k == -16 || k == -20) {
            if (far_flag || c >= 2) sprite = k == -12 ? 0 : 1;
            else shape = 2 * (k == -12 ? 0 : (k == -16 ? 1 : 2)) + (tab == 0 ? 1 : 0);
        } else continue;
        if (sprite >= 0) {
            int16_t px, py;
            if (!ng_project(m, Xs, Zs, Hs, &px, &py)) continue;
            if (py > row_limit) continue;   /* beyond the last 3D row */
            s->kind = (uint8_t)(sprite ? LISTS_SPECK_2X1 : LISTS_SPECK_1X1); s->xy[0] = px; s->xy[1] = py;
        } else {
            int16_t v[6];
            int ok = 1;
            for (int j = 0; j < 3 && ok; j++)
                ok = ng_project(m, w16(Xs + t->ng_shape[shape][j][0]), w16(Zs + t->ng_shape[shape][j][1]), Hs, &v[2 * j], &v[2 * j + 1]);
            if (!ok) continue;
            /* the filler (0x34A28) rejects a box whose top row is below the last row; the general fill is a box of three rows and two columns or more, or two rows and three columns or more */
            int top = v[1], bot = v[1], left = v[0], right = v[0];
            for (int j = 1; j < 3; j++) {
                if (v[2 * j + 1] < top) top = v[2 * j + 1];
                if (v[2 * j + 1] > bot) bot = v[2 * j + 1];
                if (v[2 * j] < left) left = v[2 * j];
                if (v[2 * j] > right) right = v[2 * j];
            }
            if (top > row_limit) continue;
            const int h = bot - top, w = right - left;
            s->kind = (uint8_t)(((h >= 3 && w >= 2) || (h == 2 && w >= 3)) ? LISTS_SPECK_FILL : LISTS_SPECK_TRI);
            memcpy(s->xy, v, sizeof v);
        }
        if (++n >= max) break;
    }
    return n;
}

/* The two parts of the frame routine's walk (note 07 7.3): the walkers 0x1FA24, 0x1FA36 and 0x2C0EC (queues A and B and the [90] pass with the near-ground marker) run first in every
 * view; the entity pass 0x1FBCC (the direct entities and queue C) follows and is the part the map view skips. */
void lists_walk_terrain(lists_t *l) {
    l->nwalk = 0;
    for (int tier = 0; tier < 2; tier++)
        for (unsigned i = l->start[tier]; i < l->kept[tier]; i++) walk_queue_entry(l, tier, i, 0);
    if (l->models && l->models->stage90) l->models->stage90(l->models->user);
    if (near_ground_active(l)) {
        lists_walk_t *m = &l->walk[l->nwalk++];
        memset(m, 0, sizeof *m);
        m->kind = 3; m->tier = LISTS_TIER_DIRECT; m->draw = 1;
    }
}

void lists_walk_entities(lists_t *l) {
    for (unsigned i = 0; i < l->ndirect; i++) walk_direct(l, l->direct[i]);
    for (unsigned i = l->start[LISTS_TIER_C]; i < l->kept[LISTS_TIER_C]; i++) {
        const lists_entry_t *e = &l->q[LISTS_TIER_C][i];
        walk_queue_entry(l, LISTS_TIER_C, i, e->flags & 0x40 ? 2 : (e->flags & 0x10 ? 1 : 0));
    }
}

void lists_walk(lists_t *l) {
    lists_walk_terrain(l);
    lists_walk_entities(l);
}

/* ---- the frame ------------------------------------------------------------------------------------------------------- */

/* The first half of the frame: detector, camera words, builder (the sky fill and the sea mesh of the original's order run after it, before the walk). */
void lists_frame_build(lists_t *l, const lists_frame_t *in) {
    lists_scan_t *s = &l->scan;
    l->n = in->n;
    for (int i = 0; i < LISTS_ENTITIES; i++) { l->ent[i] = in->ent[i]; l->sec[i] = in->sec[i]; }
    /* the entity update: bit 2 of records 14 and 15 while they are active (section 6) */
    for (int i = 14; i < 16; i++) if (l->ent[i].active) l->bit2[i] = 1;
    /* the detector (0x1F54E) sees the values of the previous frame's camera, octant and gate */
    uint8_t req = 0;
    if (s->octant != l->octant_copy) { l->octant_copy = s->octant; if (!s->gate) req |= 0x0B; }
    int32_t h = -s->cam_y;
    if ((h >= 0xA000) != (l->h26 >= 0xA000)) req |= 0x0B;
    l->h26 = h;
    int32_t band = h >> 21;
    if (band != l->band16 && l->mode != 2) req |= 0x0B;
    l->band16 = band;
    /* the viewed entity's cells (cockpit) or the camera's (external) */
    lists_scan_t ns = *s;
    lists_scan_from_camera(&ns, in->cam_pos, in->cam_eye, in->cam_yaw);
    if (in->viewer_valid) {
        ns.cell_x = in->v_cell_x; ns.cell_z = in->v_cell_z; ns.sub = in->v_sub;
        ns.quad = (uint8_t)(4 * (3 - (ns.cell_z & 3)) + (3 - (ns.cell_x & 3)));
    } else if (l->have_last) {
        if (ns.cell_x != s->cell_x || ns.cell_z != s->cell_z) req |= 0xFF;
        else if (ns.sub != s->sub || ns.quad != s->quad) { if (!(l->mode == 2 && ns.off_y <= -0x1000)) req |= 0xFF; }
    }
    l->have_last = 1;
    /* the pitch gate (0x143F2): the pitch long is positive for nose down, 28800 per circle, divided by 8 */
    int32_t pd = (-in->cam_pitch) % 28800; if (pd < 0) pd += 28800;
    int gate = pd / 8 > 600 && pd / 8 < 3150;
    if (gate != s->gate) req |= 0x0B;
    {   /* the pitch band V:4A923 (0x143F2): v = pitch / 8 in 0.1 degree, positive nose down; a nose-up angle a appears as 3600 - a */
        int v = pd / 8;
        l->pitch_band = (uint8_t)(v <= 100 ? 0 : (v <= 300 ? 1 : (v <= 600 ? 2 : (v < 3150 ? 3 : (v < 3200 ? 2 : 0)))));
    }
    {   /* V:4A924, the steep-pitch flag the pre-test reads (note 04a rule 6; written only by 0x143F2, once per frame): outside the gate (v <= 600 or v >= 3150) it is 1 for 450 < v < 3200 and 0 for every other v; inside the gate it keeps its value */
        int v = pd / 8;
        if (!(v > 600 && v < 3150)) l->v4a924 = (uint8_t)(v > 450 && v < 3200);
    }
    ns.gate = (uint8_t)gate;
    ns.acell_x = s->acell_x; ns.acell_z = s->acell_z;
    *s = ns;
    /* the re-test period of invisible entries (0x1F706) */
    {
        int m = 0;
        for (int i = 0; i < 3; i++) { int v = in->rates[i] < 0 ? -in->rates[i] : in->rates[i]; if (v > m) m = v; }
        /* NOTE-GAP: note 04a section 2 (read-only): 5 when the largest rate is at most 0x60 and +0x6C at most 0x1000, 3 when at most 0xC0 "(or +0x6C
         * larger)", else 1; read as: 3 when the rates allow 5 but +0x6C is larger, or when the rate is up to 0xC0. */
        l->period = (uint8_t)((m <= 0x60 && in->speed_target <= 0x1000) ? 5 : (m <= 0xC0 ? 3 : 1));
    }
    lists_builder(l, req);
    l->view.external = in->external; l->view.head = in->head; l->view.leave_external = in->leave_external; l->view.viewed = in->viewed;
    l->zoom = in->zoom ? in->zoom : 0x80;
}

/* The second half: the walk (queues A and B, the pass [90], the direct entities, queue C). At the top of the altitude range (the gate 0x111E0, V:4AB1E at or below -0x8000000; note 07 7.3) the
 * three walks, the gun and the entity pass are all skipped: nothing is walked or drawn. */
int lists_altitude_gate(const lists_t *l) { return l->scan.cam_y <= -0x8000000; }

void lists_frame_walk(lists_t *l) {
    if (lists_altitude_gate(l)) { l->nwalk = 0; return; }
    lists_walk(l);
}

/* The map view's walk: the walkers of queues A and B and the [90] pass, then the test of V:4A865 jumps over the entity pass (note 07 7.3); queue C and the record slots stay as the last
 * frame that built them left them. */
void lists_frame_walk_map(lists_t *l) {
    if (lists_altitude_gate(l)) { l->nwalk = 0; return; }
    lists_walk_terrain(l);
}

void lists_frame(lists_t *l, const lists_frame_t *in) {
    lists_frame_build(l, in);
    lists_frame_walk(l);
}
