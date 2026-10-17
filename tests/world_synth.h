/* tests/world_synth.h - the synthetic in-memory asset set shared by test_world.c and the golden
 * runner. The byte buffers are built from the formats in tools/extract/README.md; no game data. */
#ifndef INTERCEPTOR_WORLD_SYNTH_H
#define INTERCEPTOR_WORLD_SYNTH_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/game.h"
#include "../src/core/world.h"
#include "../src/core/fx.h"
#include "sound_synth.h"
#include "ai_tables.h"

/* ---- byte buffer helpers -------------------------------------------------------------- */
typedef struct { uint8_t d[4096]; size_t n; } buf_t;
static void p8(buf_t *b, unsigned v) { b->d[b->n++] = (uint8_t)v; }
static void p16(buf_t *b, unsigned v) { p8(b, v & 255); p8(b, v >> 8); }
static void p32(buf_t *b, uint32_t v) { p16(b, v & 0xFFFF); p16(b, v >> 16); }

/* ---- synthetic IMDL v5 ---------------------------------------------------------------- */
typedef struct { uint16_t thr, leaf, tm, tf; uint8_t shape, present, scale_h, scale_l, bflags, b8, b9; int32_t root; uint32_t vf, vc, ef, ec, sf, sc; int16_t data[3]; } mlevel_t;
typedef struct { int32_t x, y, z, st; } mvert_t;
typedef struct { uint8_t kind, flags, axis, scale; int32_t c0, c1; int16_t s[6]; uint32_t lf, lc; } melem_t;
typedef struct { uint32_t ifirst, icount; } mprog_t;
typedef struct { uint16_t op, flags, g1, g2, pen, w, wback, prim, nops; uint32_t opfirst, address, length, skip_first, skip_count; } mitem_t;
typedef struct {
    int kind;   /* header kind: 0 3D, 1 flat, 2 type B */
    int id, nl, nv, ne, npl, npg, ni, nop, nsk, blocked, nsec;
    int phase1;             /* 1 + the index of the first of the four phase-list levels of a cone model (0: none; note 07 11.5) */
    int nn, alt_n, alt_c;   /* nn: normal level count (0: all levels); alt_n, alt_c: index + 1 of the near body / cockpit alternate level (0: none) */
    mlevel_t lv[8]; mvert_t vt[64]; melem_t el[16]; uint32_t pl[16]; mprog_t pg[8]; mitem_t it[16]; int32_t op[128]; int32_t sk[16]; int32_t sec[64];
} model_b;

static void mb_init(model_b *m, int id) { memset(m, 0, sizeof *m); m->id = id; }
static int mb_vertex(model_b *m, int x, int y, int z) { m->vt[m->nv].x = x; m->vt[m->nv].y = y; m->vt[m->nv].z = z; m->vt[m->nv].st = 1; return m->nv++; }
/* a polygon item: op 3, operands [n, slots...]; flags LAST (1) | extra */
static int mb_poly(model_b *m, unsigned flags, unsigned w, unsigned pen, int n, const int *slots) {
    mitem_t *t = &m->it[m->ni];
    t->op = 3; t->flags = (uint16_t)flags; t->g1 = 0x7FFF; t->w = (uint16_t)w; t->pen = (uint16_t)pen; t->prim = 3;
    t->nops = (uint16_t)(n + 1); t->opfirst = (uint32_t)m->nop;
    t->address = 100u + 10u * (uint32_t)m->ni; t->length = 10;   /* consecutive in memory */
    m->op[m->nop++] = n;
    for (int i = 0; i < n; i++) m->op[m->nop++] = slots[i];
    return m->ni++;
}
/* a program of items [first, first+count) */
/* a generic item: operation op with nops operands, last-flagged when flags has 1 */
static int mb_item(model_b *m, int op, unsigned flags, unsigned pen, unsigned w, int nops, const int *ops) {
    mitem_t *t = &m->it[m->ni];
    t->op = (uint16_t)op; t->flags = (uint16_t)flags; t->g1 = 0x7FFF; t->w = (uint16_t)w; t->pen = (uint16_t)pen; t->prim = 1;
    t->nops = (uint16_t)nops; t->opfirst = (uint32_t)m->nop;
    t->address = 100u + 10u * (uint32_t)m->ni; t->length = 10;
    for (int i = 0; i < nops; i++) m->op[m->nop++] = ops[i];
    return m->ni++;
}
static int mb_prog(model_b *m, int first, int count) { m->pg[m->npg].ifirst = (uint32_t)first; m->pg[m->npg].icount = (uint32_t)count; return m->npg++; }
/* a draw element running program p, then ending the stream */
static int mb_draw(model_b *m, int p) {
    melem_t *e = &m->el[m->ne];
    e->kind = 5; e->c0 = -1; e->c1 = -1; e->lf = (uint32_t)m->npl; e->lc = 1;
    m->pl[m->npl++] = (uint32_t)p;
    return m->ne++;
}
static int mb_node(model_b *m, int kind, int flags, int axis, int c0, int c1, int s0, int s1, int s2) {
    melem_t *e = &m->el[m->ne];
    e->kind = (uint8_t)kind; e->flags = (uint8_t)flags; e->axis = (uint8_t)axis; e->c0 = c0; e->c1 = c1;
    e->s[0] = (int16_t)s0; e->s[1] = (int16_t)s1; e->s[2] = (int16_t)s2;
    return m->ne++;
}
static void mb_level(model_b *m, int thr, int shape, int present, int root) {
    mlevel_t *l = &m->lv[m->nl++];
    l->thr = (uint16_t)thr; l->leaf = 0; l->tm = l->tf = 0; l->scale_h = 0; l->shape = (uint8_t)shape; l->present = (uint8_t)present; l->root = root;
    l->vf = 0; l->vc = (uint32_t)m->nv; l->ef = 0; l->ec = (uint32_t)m->ne;
}
static void mb_emit(const model_b *m, buf_t *b) {
    b->n = 0;
    p8(b, 'I'); p8(b, 'M'); p8(b, 'D'); p8(b, 'L'); p16(b, 5); p16(b, (unsigned)m->id); p16(b, (unsigned)m->kind); p16(b, (unsigned)m->blocked);
    p32(b, 0); p32(b, (uint32_t)m->nl); p32(b, (uint32_t)m->nv); p32(b, (uint32_t)m->ne); p32(b, (uint32_t)m->npl);
    p32(b, (uint32_t)m->npg); p32(b, (uint32_t)m->ni); p32(b, (uint32_t)m->nop); p32(b, (uint32_t)m->nsk); p32(b, (uint32_t)m->nsec);
    p16(b, (unsigned)(m->nn ? m->nn : m->nl)); p16(b, m->alt_n ? (unsigned)(m->alt_n - 1) : 0xFFFFu); p16(b, m->alt_c ? (unsigned)(m->alt_c - 1) : 0xFFFFu); p16(b, (unsigned)m->phase1);
    for (int i = 0; i < m->nl; i++) {
        const mlevel_t *l = &m->lv[i];
        p16(b, l->thr); p16(b, l->leaf); p8(b, l->shape); p8(b, l->present); p16(b, 0); p16(b, l->tm); p16(b, l->tf);
        p8(b, l->scale_h); p8(b, 3); p8(b, l->bflags); p8(b, l->b8); p8(b, l->b9); p8(b, 0); p16(b, 0);   /* scale_h, scale_l = 3, flags, b8, b9, pad */
        p32(b, (uint32_t)l->root); p32(b, l->vf); p32(b, l->vc); p32(b, l->ef); p32(b, l->ec); p32(b, l->sf); p32(b, l->sc);
        for (int k = 0; k < 3; k++) p16(b, (uint16_t)l->data[k]);   /* the data triple (version 5) */
    }
    for (int i = 0; i < m->nv; i++) { p32(b, (uint32_t)m->vt[i].x); p32(b, (uint32_t)m->vt[i].y); p32(b, (uint32_t)m->vt[i].z); p32(b, (uint32_t)m->vt[i].st); }
    for (int i = 0; i < m->ne; i++) {
        const melem_t *e = &m->el[i];
        p8(b, e->kind); p8(b, e->flags); p8(b, e->axis); p8(b, e->scale); p32(b, (uint32_t)e->c0); p32(b, (uint32_t)e->c1);
        for (int k = 0; k < 6; k++) p16(b, (uint16_t)e->s[k]);
        p32(b, e->lf); p32(b, e->lc);
    }
    for (int i = 0; i < m->npl; i++) p32(b, m->pl[i]);
    for (int i = 0; i < m->npg; i++) { p32(b, m->pg[i].ifirst); p32(b, m->pg[i].icount); p32(b, 0); }
    for (int i = 0; i < m->ni; i++) {
        const mitem_t *t = &m->it[i];
        p16(b, t->op); p16(b, t->flags); p16(b, t->g1); p16(b, t->g2); p16(b, 0); p16(b, 0); p16(b, t->pen); p16(b, t->w);
        p16(b, t->wback); p16(b, t->prim); p16(b, t->nops); p16(b, t->length); p32(b, t->opfirst); p32(b, t->address);
        p32(b, t->skip_first); p32(b, t->skip_count);
    }
    for (int i = 0; i < m->nop; i++) p32(b, (uint32_t)m->op[i]);
    for (int i = 0; i < m->nsk; i++) p32(b, (uint32_t)m->sk[i]);
    for (int i = 0; i < m->nsec; i++) p32(b, (uint32_t)m->sec[i]);
}

/* ---- the synthetic asset store -------------------------------------------------------- */
#define F_MAX 64
static struct { char name[48]; buf_t data; int used; } files[F_MAX];
static int nfiles;
static int released, reads;

static void put_file(const char *name) {
    int i;
    for (i = 0; i < nfiles; i++) if (!strcmp(files[i].name, name)) break;
    if (i == nfiles) nfiles++;
    strcpy(files[i].name, name);
}
static buf_t *file_buf(const char *name) { put_file(name); for (int i = 0; i < nfiles; i++) if (!strcmp(files[i].name, name)) return &files[i].data; return NULL; }
static void put_text(const char *name, const char *text) {
    buf_t *b = file_buf(name); b->n = 0;
    for (const char *p = text; *p; p++) p8(b, (unsigned char)*p);
}
static void drop_file(const char *name) { for (int i = 0; i < nfiles; i++) if (!strcmp(files[i].name, name)) { files[i].name[0] = '\1'; } }

static int fake_read(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    for (int i = 0; i < nfiles; i++)
        if (!strcmp(files[i].name, path)) {
            reads++;
            *out = (uint8_t *)malloc(files[i].data.n + 1);
            memcpy(*out, files[i].data.d, files[i].data.n);
            *len = files[i].data.n;
            return 0;
        }
    return -1;
}
static void fake_release(void *user, uint8_t *buf) { (void)user; released++; free(buf); }
static const game_loader_t LD = { fake_read, fake_release, NULL };

/* models used by the tests (ids): 1 axis-BSP, 2 three-vertex BSP, 3 gates, 4 faces, 5 levels, 6 partial, 8 carrier */
static void build_models(void) {
    model_b m; buf_t b;
    static const int tri[3] = {0, 1, 2};
    /* 1: axis node on x at the plane of vertex 0 (x = 0): eye.x >= 0 -> pen 5, else pen 9 */
    mb_init(&m, 1);
    mb_vertex(&m, 0, 0, 0); mb_vertex(&m, 100, 0, 0); mb_vertex(&m, 0, 100, 0);
    int i0 = mb_poly(&m, 1 | 8, 0x8005, 5, 3, tri), i1 = mb_poly(&m, 1 | 8, 0x8009, 9, 3, tri);
    int p0 = mb_prog(&m, i0, 1), p1 = mb_prog(&m, i1, 1);
    mb_node(&m, 2, 0, 2, 1, 2, 0, 0, 0);   /* e0 */
    mb_draw(&m, p0); mb_draw(&m, p1);
    mb_level(&m, 0, 2, 1, 0);
    mb_emit(&m, &b); memcpy(file_buf("models/model_001.bin")->d, b.d, b.n); file_buf("models/model_001.bin")->n = b.n;
    /* 8: the carrier, same shape */
    m.id = 8; mb_emit(&m, &b); memcpy(file_buf("models/model_008.bin")->d, b.d, b.n); file_buf("models/model_008.bin")->n = b.n;
    /* 2: three-vertex node: value = -10000 * eye.z, negative (first child) for eye.z > 0 */
    mb_init(&m, 2);
    mb_vertex(&m, 0, 0, 0); mb_vertex(&m, 100, 0, 0); mb_vertex(&m, 0, 100, 0);
    i0 = mb_poly(&m, 1 | 8, 0x8005, 5, 3, tri); i1 = mb_poly(&m, 1 | 8, 0x8009, 9, 3, tri);
    p0 = mb_prog(&m, i0, 1); p1 = mb_prog(&m, i1, 1);
    mb_node(&m, 4, 0, 0, 1, 2, 0, 1, 2);
    mb_draw(&m, p0); mb_draw(&m, p1);
    mb_level(&m, 0, 2, 1, 0);
    mb_emit(&m, &b); memcpy(file_buf("models/model_002.bin")->d, b.d, b.n); file_buf("models/model_002.bin")->n = b.n;
    /* 3: gates. A ungated pen 1; B near-only (g1 100, g2 negative) pen 2; C band (g1 100, g2 500) pen 3 */
    mb_init(&m, 3);
    mb_vertex(&m, 0, 0, 0); mb_vertex(&m, 100, 0, 0); mb_vertex(&m, 0, 100, 0);
    int a = mb_poly(&m, 8, 0x8001, 1, 3, tri);
    int bb = mb_poly(&m, 8 | 2, 0x8002, 2, 3, tri); m.it[bb].g1 = 100; m.it[bb].g2 = 0x8000;
    int cc = mb_poly(&m, 1 | 8 | 2, 0x8003, 3, 3, tri); m.it[cc].g1 = 100; m.it[cc].g2 = 500;
    (void)a; (void)cc;
    p0 = mb_prog(&m, 0, 2); p1 = mb_prog(&m, 2, 1);   /* a failed gate ends its program (P6): the band item is a program of its own */
    { int e = mb_draw(&m, p0); m.pl[m.npl++] = (uint32_t)p1; m.el[e].lc = 2; }
    mb_level(&m, 0, 2, 1, 0);
    mb_emit(&m, &b); memcpy(file_buf("models/model_003.bin")->d, b.d, b.n); file_buf("models/model_003.bin")->n = b.n;
    /* 4: one-sided faces. item0 one-sided pen 7 (not last), item1 two-sided pen 8 (last).
     * Facing: n = (0,0,10000), faces the viewer when eye.z > 0. */
    mb_init(&m, 4);
    mb_vertex(&m, 0, 0, 0); mb_vertex(&m, 100, 0, 0); mb_vertex(&m, 0, 100, 0);
    mb_poly(&m, 0, 0x0007, 7, 3, tri); mb_poly(&m, 1 | 8, 0x8008, 8, 3, tri);
    p0 = mb_prog(&m, 0, 2); mb_draw(&m, p0);
    mb_level(&m, 0, 2, 1, 0);
    mb_emit(&m, &b); memcpy(file_buf("models/model_004.bin")->d, b.d, b.n); file_buf("models/model_004.bin")->n = b.n;
    /* 7: like 4 with a back colour (flag 16, W bit 14 style): facing away draws pen 3 */
    m.id = 7; m.it[0].flags = 16; m.it[0].wback = 0x0003; mb_emit(&m, &b);
    memcpy(file_buf("models/model_007.bin")->d, b.d, b.n); file_buf("models/model_007.bin")->n = b.n;
    /* 5: two levels: level 0 up to 100 (pen 5), last-resort level (pen 6). Level 2 has nothing (id 9) */
    mb_init(&m, 5);
    mb_vertex(&m, 0, 0, 0); mb_vertex(&m, 100, 0, 0); mb_vertex(&m, 0, 100, 0);
    i0 = mb_poly(&m, 1 | 8, 0x8005, 5, 3, tri); i1 = mb_poly(&m, 1 | 8, 0x8006, 6, 3, tri);
    p0 = mb_prog(&m, i0, 1); p1 = mb_prog(&m, i1, 1);
    int d0 = mb_draw(&m, p0), d1 = mb_draw(&m, p1);
    mb_level(&m, 100, 0, 1, d0);
    m.lv[0].ec = (uint32_t)m.ne;
    mb_level(&m, 0, 2, 1, d1);
    mb_emit(&m, &b); memcpy(file_buf("models/model_005.bin")->d, b.d, b.n); file_buf("models/model_005.bin")->n = b.n;
    /* 9: threshold level only, no last resort */
    m.id = 9; m.nl = 1; mb_emit(&m, &b); memcpy(file_buf("models/model_009.bin")->d, b.d, b.n); file_buf("models/model_009.bin")->n = b.n;
    /* 13: as 5 with precision bands on level 0: Tm 40, Tf 80, H 1, L 3 */
    m.id = 13; m.nl = 2; m.lv[0].tm = 40; m.lv[0].tf = 80; m.lv[0].scale_h = 1;
    mb_emit(&m, &b); memcpy(file_buf("models/model_013.bin")->d, b.d, b.n); file_buf("models/model_013.bin")->n = b.n;
    /* 10: selector: item0 skips (state zero: entry 0 = 10 bytes) over item1 to item2; item3 is
     * speculative (flag 64) and also unreachable. Expected pens: 1 then 3. */
    mb_init(&m, 10);
    mb_vertex(&m, 0, 0, 0); mb_vertex(&m, 100, 0, 0); mb_vertex(&m, 0, 100, 0);
    mb_poly(&m, 8, 0x8001, 1, 3, tri); mb_poly(&m, 8, 0x8002, 2, 3, tri); mb_poly(&m, 8, 0x8003, 3, 3, tri);
    mb_poly(&m, 1 | 8 | 64, 0x8004, 4, 3, tri);
    m.sk[0] = 10; m.sk[1] = 0; m.nsk = 2; m.it[0].skip_first = 0; m.it[0].skip_count = 2;
    m.it[2].flags |= 0;   /* item2 continues to item3 (speculative: not drawn) */
    p0 = mb_prog(&m, 0, 4); mb_draw(&m, p0); mb_level(&m, 0, 2, 1, 0);
    mb_emit(&m, &b); memcpy(file_buf("models/model_010.bin")->d, b.d, b.n); file_buf("models/model_010.bin")->n = b.n;
    /* 11: the root axis node is flagged as a blocked-slot node (element flag 2): nothing drawn */
    mb_init(&m, 11);
    mb_vertex(&m, 0, 0, 0); mb_vertex(&m, 100, 0, 0); mb_vertex(&m, 0, 100, 0);
    i0 = mb_poly(&m, 1 | 8, 0x8005, 5, 3, tri); p0 = mb_prog(&m, i0, 1);
    mb_node(&m, 2, 2, 2, 1, 1, 0, 0, 0); mb_draw(&m, p0); mb_level(&m, 0, 2, 1, 0);
    mb_emit(&m, &b); memcpy(file_buf("models/model_011.bin")->d, b.d, b.n); file_buf("models/model_011.bin")->n = b.n;
    /* 12: op 67 face-visibility selector (skip list [0, 10]); items: sel, pen 4, pen 5 (last).
     * Faces the viewer (eye.z > 0): "45"; faces away: the next item is skipped: "5". */
    mb_init(&m, 12);
    mb_vertex(&m, 0, 0, 0); mb_vertex(&m, 100, 0, 0); mb_vertex(&m, 0, 100, 0);
    {
        int sel = mb_poly(&m, 0, 0x0000, 0, 3, tri);
        m.it[sel].op = 67; m.it[sel].prim = 11; m.it[sel].nops = 3; m.it[sel].opfirst += 1;
        m.sk[0] = 0; m.sk[1] = 10; m.nsk = 2; m.it[sel].skip_first = 0; m.it[sel].skip_count = 2;
        mb_poly(&m, 8, 0x8004, 4, 3, tri); mb_poly(&m, 1 | 8, 0x8005, 5, 3, tri);
        p0 = mb_prog(&m, 0, 3); mb_draw(&m, p0); mb_level(&m, 0, 2, 1, 0);
    }
    mb_emit(&m, &b); memcpy(file_buf("models/model_012.bin")->d, b.d, b.n); file_buf("models/model_012.bin")->n = b.n;
    /* 14: as 4 with the draw element's child standing for a stream end by the word 0xFFFF (element flag 8): the walk reports WORLD_WALK_END_FFFF.
     * 15: as 4 for the type B drawer (header kind 2): a handler returning negative (the one-sided polygon facing away) aborts the whole drawer. */
    for (int k = 0; k < 2; k++) {
        mb_init(&m, 14 + k);
        m.kind = k ? 2 : 0;
        mb_vertex(&m, 0, 0, 0); mb_vertex(&m, 100, 0, 0); mb_vertex(&m, 0, 100, 0);
        mb_poly(&m, 0, 0x0007, 7, 3, tri); mb_poly(&m, 1 | 8, 0x8008, 8, 3, tri);
        p0 = mb_prog(&m, 0, 2); mb_draw(&m, p0);
        if (!k) m.el[0].flags = 8;
        mb_level(&m, 0, 2, 1, 0);
        mb_emit(&m, &b);
        { char nm[32]; snprintf(nm, sizeof nm, "models/model_%03d.bin", 14 + k); memcpy(file_buf(nm)->d, b.d, b.n); file_buf(nm)->n = b.n; }
    }
    /* 6: partial: a blocked item between two good ones */
    mb_init(&m, 6);
    mb_vertex(&m, 0, 0, 0); mb_vertex(&m, 100, 0, 0); mb_vertex(&m, 0, 100, 0);
    mb_poly(&m, 8, 0x8001, 1, 3, tri); mb_poly(&m, 8 | 4, 0x8002, 2, 3, tri); mb_poly(&m, 1 | 8, 0x8003, 3, 3, tri);
    p0 = mb_prog(&m, 0, 3); mb_draw(&m, p0); mb_level(&m, 0, 2, 1, 0);
    m.blocked = 1;
    mb_emit(&m, &b); memcpy(file_buf("models/model_006.bin")->d, b.d, b.n); file_buf("models/model_006.bin")->n = b.n;
}


/* models for the draw layer (note 03 "Draw operations"): 20 a two-sided triangle (pen 5), 21 two op 1 points, 22 an op 2 line, 23 an op 48 line, 24 an op 74 line,
 * 25 a flat block with a second vertex section (one paired point) and an op 2 line over its two slots, 27 the same four vertices as plain vertices, 26 type B and 28
 * common drawer: op 26 then op 24 on three slots */
static void put_model(const model_b *m0) {
    buf_t b; model_b m = *m0; mb_emit(&m, &b);
    char nm[40]; snprintf(nm, sizeof nm, "models/model_%03d.bin", m.id);
    memcpy(file_buf(nm)->d, b.d, b.n); file_buf(nm)->n = b.n;
}
static void build_draw_models(void) {
    model_b m;
    static const int tri[3] = { 0, 1, 2 };
    mb_init(&m, 20);
    mb_vertex(&m, 0, 0, 0); mb_vertex(&m, 100, 0, 0); mb_vertex(&m, 0, 100, 0);
    mb_poly(&m, 1 | 8, 0x8005, 5, 3, tri); { int p = mb_prog(&m, 0, 1); mb_draw(&m, p); mb_level(&m, 0, 2, 1, 0); }
    put_model(&m);
    mb_init(&m, 21);
    mb_vertex(&m, 0, 0, 0); mb_vertex(&m, -2000, 0, 0);
    { int o0[1] = { 0 }, o1[1] = { 1 }; mb_item(&m, 1, 0, 3, 0, 1, o0); mb_item(&m, 1, 1, 4, 0, 1, o1); }
    { int p = mb_prog(&m, 0, 2); mb_draw(&m, p); mb_level(&m, 0, 2, 1, 0); }
    put_model(&m);
    for (int k = 0; k < 3; k++) {
        static const int ops_[3] = { 2, 48, 74 };
        mb_init(&m, 22 + k);
        mb_vertex(&m, 0, 0, 0); mb_vertex(&m, -8000, 0, 0);
        { int o[2] = { 0, 1 }; mb_item(&m, ops_[k], 1, 6, 0, 2, o); }
        { int p = mb_prog(&m, 0, 1); mb_draw(&m, p); mb_level(&m, 0, 2, 1, 0); }
        put_model(&m);
    }
    /* 30: the aircraft models' gear shape (note 03, "Ops 52 and 53"): op 54 (4 bytes; skips the 2 bytes of the op 0 item after it when the progress n < 6), the op 0 item with the last-operation flag (the program ends there at n = 6 and 7; the body is the next program of the list),
     * the op 52 item (8 bytes), six op 53 items of 14 bytes with the skips 70, 56, 42, 28, 14, 0 (step s reads B = 6 + s: the vertex x of slot B + j is 400 (B - 5)), then the gear lines (op 13) from the synthesised slot 30 to the
     * zero vertex 14, then the rest of the body (a line). The baked slots 30 to 44 are the origin. */
    mb_init(&m, 30);
    for (int i = 0; i < 45; i++) mb_vertex(&m, i >= 6 && i <= 13 ? 400 * (i - 5) : 0, 0, 0);
    { int i54 = mb_item(&m, 54, 0, 0, 0, 0, NULL); m.it[i54].address = 92; m.it[i54].length = 4;
      m.it[i54].skip_first = (uint32_t)m.nsk; m.it[i54].skip_count = 2; m.sk[m.nsk++] = 2; m.sk[m.nsk++] = 0; }
    { int i00 = mb_item(&m, 0, 1, 0, 0, 0, NULL); m.it[i00].address = 96; m.it[i00].length = 2; m.it[i00].flags = 1; }   /* op 0 with the last-operation flag */
    { int o52[3] = { 0, 6, 20 }; int i0 = mb_item(&m, 52, 0, 0, 0, 3, o52); m.it[i0].address = 98; m.it[i0].length = 8; }
    for (int st = 0; st < 6; st++) {
        int o53[3] = { 0, 6 + st, 14 * (5 - st) };
        int i1 = mb_item(&m, 53, 0, 0, 0, 3, o53);
        m.it[i1].address = (uint32_t)(106 + 14 * st); m.it[i1].length = 14;
        m.it[i1].skip_first = (uint32_t)m.nsk; m.it[i1].skip_count = 2;
        m.sk[m.nsk++] = 14 * (5 - st); m.sk[m.nsk++] = 0;
    }
    { int o13[2] = { 30, 14 }; int i3 = mb_item(&m, 13, 1, 6, 0, 2, o13); m.it[i3].address = 190; m.it[i3].length = 10; }   /* the gear lines (op 13) */
    { int o2[2] = { 0, 14 }; int i2 = mb_item(&m, 2, 1, 6, 0, 2, o2); m.it[i2].address = 200; m.it[i2].length = 10; }       /* the rest of the body: a line from the origin vertex to vertex 14 */
    { int p = mb_prog(&m, 0, 10); mb_draw(&m, p); }   /* [54, 0 last, 52, 53 x 6, 13 last], then the body as the next program of the list */
    { int p = mb_prog(&m, 10, 1); m.pl[m.npl++] = (uint32_t)p; m.el[m.ne - 1].lc = 2; mb_level(&m, 0, 2, 1, 0); }
    put_model(&m);
    /* 25: flat block (header kind 1, block flag bit 1 and 2), two plain vertices and a second section { 1 pair (300, 0), 0 singles } then the terminator */
    mb_init(&m, 25);
    m.kind = 1;
    mb_vertex(&m, 100, 0, 0); mb_vertex(&m, 400, 0, 200);
    mb_vertex(&m, 0, 0, 0); mb_vertex(&m, 0, 0, 0); m.vt[2].st = 5; m.vt[3].st = 5;
    { int o[2] = { 2, 3 }; mb_item(&m, 2, 1, 6, 0, 2, o); }
    { int p = mb_prog(&m, 0, 1); mb_draw(&m, p); mb_level(&m, 0, 2, 1, 0); }
    m.lv[0].bflags = 2 | 4; m.lv[0].b8 = 2;
    m.sec[0] = 1; m.sec[1] = 300; m.sec[2] = 0; m.sec[3] = 0; m.sec[4] = -1; m.nsec = 5; m.lv[0].sf = 0; m.lv[0].sc = 5;
    put_model(&m);
    /* 27: the same slots as plain vertices: the point (300, 0) and the point plus the offset S1 - S0 = (300, 0, 200) in model units is (600, 0, 200) (the identity camera) */
    mb_init(&m, 27);
    m.kind = 1;
    mb_vertex(&m, 100, 0, 0); mb_vertex(&m, 400, 0, 200); mb_vertex(&m, 300, 0, 0); mb_vertex(&m, 600, 0, 200);
    { int o[2] = { 2, 3 }; mb_item(&m, 2, 1, 6, 0, 2, o); }
    { int p = mb_prog(&m, 0, 1); mb_draw(&m, p); mb_level(&m, 0, 2, 1, 0); }
    m.lv[0].bflags = 2; m.lv[0].b8 = 4;
    put_model(&m);
    for (int k = 0; k < 2; k++) {
        mb_init(&m, 26 + 2 * k);
        m.kind = k ? 0 : 2;
        mb_vertex(&m, 0, 0, 0); mb_vertex(&m, 1000, 0, 0); mb_vertex(&m, 0, 0, 1000);
        { int o26[2] = { 3, 0 }, o24[4] = { 3, 0, 1, 2 }; mb_item(&m, 26, 0, 7, 0, 2, o26); mb_item(&m, 24, 1, 9, 0, 4, o24); }
        { int p = mb_prog(&m, 0, 2); mb_draw(&m, p); mb_level(&m, 0, 2, 1, 0); }
        put_model(&m);
    }
}

/* terrain: table C cells (0,0), (10,10), (11,10), (12,10); A and B empty */
static void build_terrain(void) {
    buf_t *b = file_buf("terrain/terrain.bin"); b->n = 0;
    p8(b, 'I'); p8(b, 'T'); p8(b, 'R'); p8(b, 'N'); p16(b, 2); p16(b, 3); p32(b, 4); p32(b, 6);   /* ITRN version 2: cells in z then x order */
    p16(b, 24); p16(b, 32); p32(b, 0); p32(b, 0);
    p16(b, 24); p16(b, 32); p32(b, 0); p32(b, 0);
    p16(b, 22); p16(b, 128); p32(b, 0); p32(b, 4);
    for (int i = 0; i < 32; i++) p8(b, 0);
    p16(b, 0); p16(b, 0); p32(b, 0);
    p16(b, 10); p16(b, 10); p32(b, 1);
    p16(b, 11); p16(b, 10); p32(b, 2);
    p16(b, 12); p16(b, 10); p32(b, 3);
    p32(b, 0); p32(b, 1);      /* list 0: item 0 */
    p32(b, 1); p32(b, 2);      /* list 1: items 1, 2 */
    p32(b, 3); p32(b, 1);      /* list 2: item 3 */
    p32(b, 4); p32(b, 2);      /* list 3: items 4, 5 */
    p8(b, 0); p8(b, 1); p16(b, 0); p16(b, 0);          /* item 0: id 1 at the cell corner */
    p8(b, 0); p8(b, 1); p16(b, 2048); p16(b, 2048);    /* item 1: id 1 at the cell centre */
    p8(b, 1); p8(b, 20); p16(b, 100); p16(b, 100);     /* item 2: id 20 (a run-time slot: no model) */
    p8(b, 0); p8(b, 1); p16(b, 0); p16(b, 0);          /* item 3: id 1 at the cell corner */
    p8(b, 0); p8(b, 1); p16(b, 0); p16(b, 0);          /* item 4: id 1 at the cell corner (12, 10): far */
    p8(b, 3); p8(b, 5); p16(b, 4095); p16(b, 4095);    /* item 5: id 5 at the far corner of (12, 10) */
}

/* The display-list builder's tables (lists.h): identity permutations, invented tile offsets and generation limits, and one-list pattern blobs. The lists
 * cover the tiles (3 = (1,0)) and (4 = (0,0)) with all 16 sub-cells (high and steep patterns), and the low-altitude pattern the tile 4. */
static void put_blob(const char *name, int nwords, const uint8_t *lists_bytes, size_t nbytes, size_t total) {
    buf_t *b = file_buf(name); b->n = 0;
    for (int i = 0; i < nwords; i++) { unsigned off = (unsigned)(2 * nwords); p8(b, off >> 8); p8(b, off & 255); }
    for (size_t i = 0; i < nbytes; i++) p8(b, lists_bytes[i]);
    while (b->n < total) p8(b, 0);   /* the real blobs' sizes: 3042, 610 and 502 bytes */
}
static void build_lists(void) {
    buf_t *b;
    /* INVENTED offsets of the right shape (21 cell offsets); the tests express the rule, not the shipped order */
    static const int8_t tiles[21][2] = { {2,2},{-2,2},{0,-1},{1,0},{0,0},{-1,1},{1,1},{2,-1},{-1,-1},{0,1},{-2,0},{2,0},{1,-1},{-1,0},{0,2},{1,2},{-1,2},{0,-2},{-2,-1},{-2,1},{2,1} };   /* the tests' patterns list the indices 3 and 4: (1, 0) and (0, 0) */
    b = file_buf("tables/lists_quad.bin"); b->n = 0; for (int i = 0; i < 16; i++) p8(b, 0);
    b = file_buf("tables/lists_group.bin"); b->n = 0; for (int i = 0; i < 16; i++) p8(b, 0);
    b = file_buf("tables/lists_octperm.bin"); b->n = 0; for (int i = 0; i < 32; i++) p8(b, i & 7);
    b = file_buf("tables/lists_subperm.bin"); b->n = 0; for (int i = 0; i < 64; i++) p8(b, i & 15);
    b = file_buf("tables/lists_tileperm.bin"); b->n = 0; for (int i = 0; i < 96; i++) p8(b, (i % 24) > 20 ? 0 : (unsigned)(i % 24));
    b = file_buf("tables/lists_tiles.bin"); b->n = 0; for (int i = 0; i < 21; i++) { p8(b, (unsigned)(tiles[i][0] & 255)); p8(b, (unsigned)(tiles[i][1] & 255)); }
    uint8_t two[2 * (2 + 2 + 16) + 1], one[2 + 2 + 16 + 1];
    size_t n = 0;
    for (int e = 0; e < 2; e++) { two[n++] = (uint8_t)(e == 0 ? 3 : 4); two[n++] = 16; for (int k = 0; k < 16; k++) two[n++] = (uint8_t)k; }
    two[n++] = 0xFF;
    n = 0; one[n++] = 4; one[n++] = 16; for (int k = 0; k < 16; k++) one[n++] = (uint8_t)k; one[n++] = 0xFF;
    put_blob("tables/lists_high.bin", 32, two, 2 * (2 + 16) + 1, 3042);
    put_blob("tables/lists_steep.bin", 4, one, 2 + 16 + 1, 610);
    put_blob("tables/lists_low.bin", 64, one, 2 + 16 + 1, 502);
    b = file_buf("tables/lists_limit.bin"); b->n = 0;
    { static const int lim[8] = { 21, 33, 25, 37, 29, 45, 41, 57 };   /* invented: the limit of the height band D and the pitch gate is lim[2 D + gate] */ for (int i = 0; i < 8; i++) p16(b, (unsigned)lim[i]); }
    b = file_buf("tables/lists_vis.bin"); b->n = 0;
    { static const int vis[24] = { 0, 0x14, 0x28, 0x120, 0x240, 0x440, 0x840, 0xA40, 0xC40, 0xE40, 0xF40, 0xFC0,
                                   0, 0x14, 0x28, 0x1A0, 0x640, 0xA40, 0xC40, 0xE40, 0xF40, 0x1040, 0x1440, 0x1840 };   /* sample values, not the original's */
      for (int i = 0; i < 24; i++) p16(b, (unsigned)vis[i]); }
    b = file_buf("tables/lists_desc.bin"); b->n = 0;
    for (int r = 0; r < 134; r++) { p8(b, r >= 16 ? 1 : 0); p8(b, 1); p16(b, 0xFFFF); p16(b, 0xFFFF); p16(b, 0); }
    b = file_buf("tables/lists_land.bin"); b->n = 0; p16(b, 0);
    /* the near-ground pass's tables (stage [90]): every cell class 1 (a candidate under every limit), the point tables with the shipped headers and kind 0 (no speck) in every entry,
     * six invented isosceles shapes (apex dx = 0; the shipped values are not copied); a test that wants specks overwrites the files (ng_put_points) */
    b = file_buf("tables/lists_ngclass.bin"); b->n = 0; for (int i = 0; i < 1024; i++) p8(b, 1);
    b = file_buf("tables/lists_ngpts.bin"); b->n = 0;
    { static const int cnt[3] = { 96, 80, 64 };
      for (int k = 0; k < 3; k++) { p16(b, (unsigned)cnt[k]); p16(b, (unsigned)(2 + k)); for (int i = 0; i < cnt[k] * 3; i++) p16(b, 0); } }
    b = file_buf("tables/lists_ngshape.bin"); b->n = 0;
    { static const int sh[36] = { 9, -6, -9, -6, 0, 3,  31, -17, -31, -17, 0, 5,  14, -10, -14, -10, 0, 7,  52, -29, -52, -29, 0, 11,  19, -13, -19, -13, 0, 13,  71, -41, -71, -41, 0, 17 };
      for (int i = 0; i < 36; i++) p16(b, (unsigned)(sh[i] & 0xFFFF)); }
}

static void build_all(void) {
    nfiles = 0;
    put_text("VERSION", "27\n");
    put_text("pix/index.txt", "splsh\n");
    put_text("models/index.txt",
        "# id\tfile\tstatus\n"
        "1\tmodel_001.bin\tok\n2\tmodel_002.bin\tok\n3\tmodel_003.bin\tok\n4\tmodel_004.bin\tok\n"
        "5\tmodel_005.bin\tok\n6\tmodel_006.bin\tpartial: blocked (1 items flagged blocked)\n"
        "7\tmodel_007.bin\tok\n8\tmodel_008.bin\tok\n9\tmodel_009.bin\tok\n10\tmodel_010.bin\tok\n11\tmodel_011.bin\tok\n12\tmodel_012.bin\tok\n13\tmodel_013.bin\tok\n14\tmodel_014.bin\tok\n15\tmodel_015.bin\tok\n"
        "16\t-\tempty: run-time slot\n"
        "20\tmodel_020.bin\tok\n21\tmodel_021.bin\tok\n22\tmodel_022.bin\tok\n23\tmodel_023.bin\tok\n24\tmodel_024.bin\tok\n25\tmodel_025.bin\tok\n26\tmodel_026.bin\tok\n27\tmodel_027.bin\tok\n28\tmodel_028.bin\tok\n30\tmodel_030.bin\tok\n");
    build_models();
    build_draw_models();
    build_terrain();
    build_lists();
    /* carrier at cell (10, 10), fine (8192, 8192) = the cell centre, height 4 */
    put_text("terrain/positions.txt", "1 61 69 100 8100 10300\n33 10 10 8192 8192 4\n");
    buf_t *b = file_buf("palette/flight.pal"); b->n = 0;
    for (int i = 0; i < 48; i++) p8(b, i * 5);
    /* the sound object table (extractor version 25): the invented table of tests/sound_synth.h, which has the shape of the note's table (note 10 2.2) with invented sample lengths: the cockpit and external
     * engine pairs, the tone and noise, the ground contact, the wind pair, the gun, the gear and the launch, the music chain and the mission tone pair. 37 records of 20 bytes, big-endian. */
    {   sound_table_t st;
        sound_synth_table(&st);
        b = file_buf("tables/sounds.bin"); b->n = 0;
        for (int i = 0; i < SOUND_OBJECTS; i++) {
            p8(b, st.obj[i].file); p8(b, st.obj[i].kind); p8(b, st.obj[i].voice); p8(b, st.obj[i].volume);
            for (int k = 3; k >= 0; k--) p8(b, (st.obj[i].offset >> (8 * k)) & 255);
            for (int k = 3; k >= 0; k--) p8(b, (st.obj[i].length >> (8 * k)) & 255);
            p8(b, st.obj[i].period >> 8); p8(b, st.obj[i].period & 255);
            p8(b, (unsigned)(uint8_t)st.obj[i].count); p8(b, st.obj[i].reload); p8(b, st.obj[i].next); p8(b, st.obj[i].script);
            p8(b, 0); p8(b, 0);
        }
    }
    b = file_buf("tables/lod.bin"); b->n = 0;
    for (int i = 0; i < 240; i++) p8(b, i / 16);
    b = file_buf("tables/range.bin"); b->n = 0;   /* synthetic table: 64..98 by steps, then 99.. up to 127 */
    for (int i = 0; i < 64; i++) p8(b, i < 11 ? 64 + 4 * i - (i > 5 ? i - 5 : 0) : (i < 40 ? 99 + (i - 11) : 127));
    const char *rings[4] = { "tables/camera_ring_default.bin", "tables/camera_ring_11.bin", "tables/camera_ring_14.bin", "tables/camera_ring_30.bin" };
    for (int c = 0; c < 4; c++) {
        b = file_buf(rings[c]); b->n = 0;
        for (int k = 0; k < 10; k++) { p16(b, (unsigned)(c * 100 + k)); p16(b, (unsigned)(-k & 0xFFFF)); p16(b, k == 4 ? (unsigned)(-15 & 0xFFFF) : 7u); }
    }
}

/* ---- the invented flight tables ------------------------------------------------------------------------------------------ */
/* The flight step's tables (flight_tables.c) are served at their sizes and filled with zeros: the aircraft exists and steps, with the physics of an all-zero table set
 * (nothing from the disk). They are bigger than a buf_t, so this reader serves them on top of fake_read (the sound test and the golden runner use it); synth_flight is
 * the switch (0: the tables are absent, as in a milestone-2 asset set). */
static int synth_flight;
static int synth_trig;   /* with synth_flight: the invented quarter-wave table below and the invented hard-point row (the F5 fight); 0: the all-zero tables (every matrix is zero) */
typedef struct { const char *name; size_t bytes; } zt_t;
static const zt_t SYNTH_ZT[] = { { "tables/flight_stick_player.bin", 124 }, { "tables/flight_stick_ai.bin", 128 }, { "tables/flight_gust.bin", 62 }, { "tables/flight_sink.bin", 62 },
    { "tables/flight_span_pitch.bin", FLIGHT_SPAN_A_SIZE }, { "tables/flight_span_trig.bin", FLIGHT_SPAN_B_SIZE }, { "tables/flight_len.bin", 514 }, { "tables/flight_terrain.bin", FLIGHT_TERRAIN_WORDS * 2 },
    { "tables/flight_dir.bin", 64 }, { "tables/flight_pads.bin", 88 }, { "tables/flight_carrier.bin", 852 }, { "tables/flight_ship2.bin", 438 } };
/* tables/ai.bin of the mission runs (synth_missions): the formula tables of ai_tables.h (rates, evasion, offsets), the waypoint runs and the other blocks zero, and a hull whose faces are all
 * zero triples closed by the terminator: every point inside the box test of the gun is a hit (the weapons module reads the hull after the box). Little endian, the sub-table headers of assets_load_ai. */
static int synth_missions;
static void synth_ai_sub(buf_t *b, const int16_t *w, unsigned count, unsigned wpr, uint8_t *raw, size_t *n) {
    (void)b;
    raw[(*n)++] = (uint8_t)(count & 255); raw[(*n)++] = (uint8_t)(count >> 8);
    raw[(*n)++] = (uint8_t)((2 * wpr) & 255); raw[(*n)++] = (uint8_t)((2 * wpr) >> 8);
    for (unsigned i = 0; i < count * wpr; i++) { raw[(*n)++] = (uint8_t)((uint16_t)w[i] & 255); raw[(*n)++] = (uint8_t)((uint16_t)w[i] >> 8); }
}
static uint8_t *synth_ai_bin(size_t *len) {
    static ai_t t;
    static int16_t runs[AI_RUNS][1 + 5 * AI_RUN_MAX];
    t = AI_FIXTURE;
    for (int l = 0; l < AI_HULL_LISTS; l++) t.hull[l][AI_HULL_TRIPLES - 1][0] = (int16_t)0x8000;
    if (synth_trig) {   /* INVENTED: the hard point of the player's long-range missile (row 8 + 5, the stock 4 after the launch) lies 57 x 256 = 14,592 position units above the owner, so that the missile starts at the height of the F5 cruise missile (45,056) */
        t.offs[13][0] = 0; t.offs[13][1] = 57; t.offs[13][2] = 0;
    }
    uint8_t *raw = (uint8_t *)calloc(1, 40000);
    size_t n = 0;
    for (int i = 0; i < AI_RATES; i++) synth_ai_sub(NULL, t.rate[i], AI_RATE_ENTRIES, 1, raw, &n);
    synth_ai_sub(NULL, &t.evasion[0][0][0], AI_EVASION_GROUPS, 8, raw, &n);
    synth_ai_sub(NULL, &t.offs[0][0], AI_OFFSETS, 3, raw, &n);
    synth_ai_sub(NULL, &t.ring3[0][0], AI_RING_DIRS, 3, raw, &n);
    synth_ai_sub(NULL, &t.start[0][0], AI_STARTS, 8, raw, &n);
    synth_ai_sub(NULL, &t.cam_point[0][0], AI_CAM_POINTS, 6, raw, &n);
    synth_ai_sub(NULL, &t.fine[0][0], AI_FINE_OFFSETS, 2, raw, &n);
    synth_ai_sub(NULL, &t.pre_word, 1, 1, raw, &n);
    memset(runs, 0, sizeof runs);
    synth_ai_sub(NULL, &runs[0][0], AI_RUNS, 1 + 5 * AI_RUN_MAX, raw, &n);
    synth_ai_sub(NULL, &t.hull[0][0][0], AI_HULL_LISTS * AI_HULL_TRIPLES, 3, raw, &n);
    *len = n;
    return raw;
}
static inline int synth_flight_read(void *user, const char *path, uint8_t **out, size_t *len) {
    if (synth_missions && !strcmp(path, "tables/ai.bin")) { *out = synth_ai_bin(len); return 0; }
    if (synth_flight && synth_trig && !strcmp(path, "tables/flight_span_trig.bin")) {
        /* INVENTED (no shipped value): the span holding the quarter-wave table (FLIGHT_ADDR_SINE, 901 words in Q14) is zero except that table, which rises linearly, s[i] = 16384 * i / 900: not a sine, only a
         * ramp from 0 to 1.0 with s(0) = 0 and s(900) = 16384, so that an attitude of zero builds the unit matrix (the player's matrices are zero with the all-zero table, and so is every offset they rotate). */
        uint8_t *b = (uint8_t *)calloc(FLIGHT_SPAN_B_SIZE + 1, 1);
        for (unsigned i = 0; i <= 900; i++) { const unsigned v = 16384u * i / 900u, o = FLIGHT_ADDR_SINE - FLIGHT_SPAN_B_BASE + 2 * i; b[o] = (uint8_t)(v >> 8); b[o + 1] = (uint8_t)(v & 255); }
        *out = b; *len = FLIGHT_SPAN_B_SIZE; return 0;
    }
    if (synth_flight)
        for (size_t i = 0; i < sizeof SYNTH_ZT / sizeof SYNTH_ZT[0]; i++)
            if (!strcmp(path, SYNTH_ZT[i].name)) { *out = (uint8_t *)calloc(SYNTH_ZT[i].bytes + 1, 1); *len = SYNTH_ZT[i].bytes; return 0; }
    return fake_read(user, path, out, len);
}

/* ---- the invented mission tables (the golden runner's synthflight; no shipped values) ------------------------------------------------------------------------- */
/* What the F-key start reads once the flight tables exist (screen.c placement, entities.c spawn, the briefing chain): the scenario rows of the modes 3 to 8, their lists and sub-lists,
 * the templates, the position blocks the entries name, and the chain of record ids (the text records are absent: the screens run without text, as in every synthetic run).
 * Every number below is invented for the golden scripts, in the formats of tools/extract/README.md. The placement of a mission is a few aircraft ahead of the carrier. */
static void sec16(buf_t *b, unsigned count, unsigned rec) { p16(b, count); p16(b, rec); }
static void sentry(buf_t *b, unsigned w0, unsigned w1, unsigned w2, unsigned w3, unsigned w4) { p16(b, w0); p16(b, w1); p16(b, w2); p16(b, w3); p16(b, w4); }
typedef struct { int tid, cls, slot, pos, flags; } synth_place_t;
static inline void synth_mission_tables(void) {
    /* the position blocks (entry cell_x cell_z fine_x fine_z height): the carrier of the base set at cell (10, 10); 40 to 42 ahead of the start; 49 a parked point (height 0); 50 and 51 right below
     * the start of the carrier mission flights (the carrier start of the all-zero flight tables stays where the placement puts it: fine 8236, 8608, height 119), 52 and 53 within 768 fine units of it,
     * 54 on the ground below it */
    put_text("terrain/positions.txt",
        "1 61 69 100 8100 10300\n33 10 10 8192 8192 4\n"
        "40 10 10 8192 9400 12\n41 10 10 8300 9800 12\n42 10 10 8100 10200 12\n"
        "49 10 10 8192 9400 0\n"
        "50 10 10 8236 8608 40\n51 10 10 8236 8608 20\n52 10 10 8800 8700 4\n53 10 10 7700 8500 4\n54 10 10 8236 8608 0\n");
    /* the templates: 134 records of five longs, invented words (a class byte pattern and a size); the spawner copies them to the descriptor words and reads nothing else */
    {   buf_t *b = file_buf("tables/templates.bin"); b->n = 0;
        p8(b, 134); p8(b, 0); p8(b, 20); p8(b, 0);
        for (int i = 0; i < 134; i++) {
            const uint32_t v[5] = { 0x10000000u | (uint32_t)i, 0x00400000u + (uint32_t)(i * 16), 0x00001000u + (uint32_t)i, (uint32_t)i * 3u, 0u };
            for (int k = 0; k < 5; k++) { p8(b, v[k] >> 24); p8(b, (v[k] >> 16) & 255); p8(b, (v[k] >> 8) & 255); p8(b, v[k] & 255); }
        }
    }
    /* the briefing chain: five record ids for each of the modes 3 to 8 (ids of records the synthetic set does not have) */
    {   buf_t *b = file_buf("tables/briefing_chain.bin"); b->n = 0;
        static const uint8_t chain[30] = { 0x60, 0x61, 0x62, 0x63, 0x64,  0x65, 0x66, 0x67, 0x68, 0x69,  0x6A, 0x6B, 0x6C, 0x47, 0x48,
                                           0x49, 0x4A, 0x4D, 0x4E, 0x4F,  0x50, 0x51, 0x52, 0x53, 0x54,  0x56, 0x57, 0x58, 0x59, 0x5A };
        for (int i = 0; i < 30; i++) p8(b, chain[i]);
    }
    /* the scenario: five start blocks (three runway blocks and the two carrier records), no regions, ten placement rows (row m - 1 for the mode m), eight lists (the list of a mission 3 to 8 is
     * the mission's, 0 to 5; 6 the others) each with four equal sub-lists, and the sub-list entries (template offset 20 x id, class, record slot, position block x 2, flags) */
    static const synth_place_t S[6][8] = {
        /* F1 (mode 3): the unknown aircraft, three of them, ahead of the start */
        { { 8, 0x20, 14, 66, 0x8000 }, { 4, 0x10, 4, 80, 0 }, { 5, 0x10, 5, 82, 0 }, { 2, 0x10, 6, 84, 0 }, { 0 } },
        /* F2 (mode 4): the principal aircraft, parked on the ground (height 0) */
        { { 8, 0x20, 14, 66, 0x8000 }, { 4, 0x10, 4, 98, 0 }, { 0 } },
        /* F3 (mode 5): two thieves (not counted: the neutral bit) within the shadowing box of the start, and two escorts stacked right below it, in the line of the gun */
        { { 8, 0x20, 14, 66, 0x8000 }, { 4, 0x10, 4, 104, 0x8000 }, { 4, 0x10, 6, 106, 0x8000 }, { 5, 0x10, 8, 100, 0 }, { 2, 0x10, 10, 102, 0 } },
        /* F4 (mode 6): the pod's site marker (class 0x32) under the start */
        { { 8, 0x20, 14, 66, 0x8000 }, { 1, 0x32, 11, 108, 0x8000 }, { 0 } },
        /* F5 (mode 7): the cruise missile (class 0x15) right below the start */
        { { 8, 0x20, 14, 66, 0x8000 }, { 2, 0x15, 4, 100, 0 }, { 0 } },
        /* F6 (mode 8): two hostile aircraft right below the start */
        { { 8, 0x20, 14, 66, 0x8000 }, { 4, 0x10, 4, 100, 0 }, { 5, 0x10, 5, 102, 0 }, { 0 } } };
    static const int CAP[6] = { 3, 1, 2, 1, 1, 2 };
    int first[6], count[6], total = 0;
    for (int m = 0; m < 6; m++) { first[m] = total; count[m] = 0; while (S[m][count[m]].tid) count[m]++; total += count[m]; }
    buf_t *b = file_buf("tables/scenario.bin"); b->n = 0;
    sec16(b, 5, 16);
    {   static const int blk[3][8] = { { 16, 16, 2, 100, 200, 300, -400, 90 }, { 15, 17, 3, -50, 60, 70, 80, 180 }, { 14, 15, 4, 11, 12, 13, -14, 270 } };
        for (int k = 0; k < 3; k++) for (int j = 0; j < 8; j++) p16(b, (unsigned)blk[k][j] & 0xFFFF);
        p16(b, 0x800E); for (int j = 1; j < 8; j++) p16(b, 0);
        p16(b, 0x800F); for (int j = 1; j < 8; j++) p16(b, 0);
    }
    sec16(b, 0, 14);
    sec16(b, 0, 10);
    sec16(b, 10, 12);
    for (int r = 0; r < 10; r++) {
        const int m = r - 2, list = (m >= 0 && m < 6) ? m : 6;
        const int cap = (m >= 0 && m < 6) ? CAP[m] : 0;
        for (int v = 0; v < 3; v++) { p16(b, (unsigned)list); p16(b, (unsigned)cap); }
    }
    sec16(b, 7, 8);
    for (int l = 0; l < 7; l++) for (int k = 0; k < 4; k++) p16(b, (unsigned)l);
    sec16(b, 7, 4);
    for (int l = 0; l < 6; l++) { p16(b, (unsigned)first[l]); p16(b, (unsigned)count[l]); }
    p16(b, 0); p16(b, 1);
    sec16(b, (unsigned)total, 10);
    for (int m = 0; m < 6; m++) for (int k = 0; k < count[m]; k++) sentry(b, (unsigned)(20 * S[m][k].tid), (unsigned)S[m][k].cls, (unsigned)S[m][k].slot, (unsigned)S[m][k].pos, (unsigned)S[m][k].flags);
    sec16(b, 0, 0);
}

/* ---- the invented training table (tables/training.bin, format of tools/extract/README.md and assets_load_training) -------------------------------------------------------------------------- */
/* Eight rows of the table A (prompt id, name id, a long), eight rows of the table B (camera long, script long with bit 31 set), and the 11 pitch and 11 roll rates of the instructor's states. Every number is
 * invented (an id pattern, a camera index, a ramp of rates); with the file present the menu's item 3 starts the flight school's part 1 in the synthetic world once the flight tables exist (synthflight),
 * which the world without the file does not (the training scripts keep that: the golden asserts the refusal). */
static inline void synth_training_table(void) {
    buf_t *b = file_buf("tables/training.bin"); b->n = 0;
    p16(b, 8); p16(b, 8);
    for (int i = 0; i < 8; i++) { p16(b, 0xA0u + (unsigned)i); p16(b, 0xB0u + (unsigned)i); p16(b, 0); p16(b, 0); }
    p16(b, 8); p16(b, 8);
    for (int i = 0; i < 8; i++) { p16(b, 0x0100u + (unsigned)i); p16(b, 0); p16(b, 0); p16(b, 0x8000u); }
    p16(b, 11); p16(b, 2);
    for (int i = 0; i < 11; i++) p16(b, (unsigned)(10 * i - 50) & 0xFFFFu);
    p16(b, 11); p16(b, 2);
    for (int i = 0; i < 11; i++) p16(b, (unsigned)(20 * i - 100) & 0xFFFFu);
}

static inline void synth_invent_carrier(void) {   /* model 8 with 60 vertices: the ship record takes 58 slots of it */
    model_b m; buf_t b;
    static const int tri[3] = { 0, 1, 2 };
    mb_init(&m, 8);
    for (int i = 0; i < 60; i++) mb_vertex(&m, 100 * (i % 7), 10 * i, 100 * (i / 7));
    int i0 = mb_poly(&m, 1 | 8, 0x8005, 5, 3, tri), i1 = mb_poly(&m, 1 | 8, 0x8009, 9, 3, tri);
    int p0 = mb_prog(&m, i0, 1), p1 = mb_prog(&m, i1, 1);
    mb_node(&m, 2, 0, 2, 1, 2, 0, 0, 0); mb_draw(&m, p0); mb_draw(&m, p1); mb_level(&m, 0, 2, 1, 0);
    mb_emit(&m, &b);
    memcpy(file_buf("models/model_008.bin")->d, b.d, b.n); file_buf("models/model_008.bin")->n = b.n;
}

#endif
