/* tests/rig/test_radar_diff.c - differential test: the port's radar module (src/core/radar.c, docs/re/07-ai-and-weapons.md section 12) against the original's routines in the
 * Musashi rig, over random record and list states.
 *
 * Convention (test_ai_diff.c): the image at 0x10000; the variable area 0x4A6E8-0x54EC0 cleared; the case written into the records and the variables; rig_run with all registers 0.
 * Parts:
 *   list   the list builder 0x1FEE6 (gate) / 0x1FF24 (body): the 16-byte entries at 0x53368 against radar_build (entry values, flag words, order, terminator)
 *   walk   the scope walk 0x35E5E: the records' W0 bit 0x4000 and B20 bit 6, the threat byte V:4A925, V:4A926, the cue long V:4AC0C, V:4A93F, V:4A927, the list flag words, against radar_pass
 *   scope  the scope draw 0x35CF2 (gate, redraw counter V:4A8EF 0 to 3, step counter n; two walks on a redraw pass) against radar_pass
 *   desig  the designation 0x362D8 after a walk: V:4AA78, V:4AA7C, V:4A93E, V:4A93F, V:4A920 against radar_designate
 *   slew   the marker slew 0x38D8C (the four-iteration loop: the single steps, the early return of the box point, V:4A866, the jitter, V:4AA40 and V:4A990)
 *   range  the player's range 0x2835A (through the world update 0x26AF8, one pass): E+0x4A, E+0x39, B4 bit 0 against entities_update_player_range
 * The pixel routines run in the rig (their writes land in the low memory the harness leaves free). Skipped (exit 0) when extracted/interceptor.bin is absent. Run from tools/m68k-rig
 * (make test-radar). Environment: RADAR_DIFF_N (cases per part, default 1500), RADAR_DIFF_SEED, RADAR_DIFF_VERBOSE, RADAR_DIFF_PART (list, walk, desig, range). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/radar.h"
#include "../../src/core/flight.h"

#define BASE 0x10000u
#define V_LO 0x4A6E8u
#define V_HI 0x54EC0u
#define REC 0x4B23Cu
#define LISTA 0x53368u

static uint32_t rng = 0x9E3779B9u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }
static int pct(int p) { return rnd(0, 99) < p; }

static void put8(uint32_t a, int v) { uint8_t b = (uint8_t)v; rig_poke(a, &b, 1); }
static void put16(uint32_t a, int v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 2); }
static void put32(uint32_t a, int32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 4); }
static int get8(uint32_t a) { uint8_t b; rig_peek(a, &b, 1); return b; }
static int get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return (int16_t)((b[0] << 8) | b[1]); }
static int32_t get32(uint32_t a) { uint8_t b[4]; rig_peek(a, b, 4); return (int32_t)(((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]); }

static const char *image_path;
static int rig_reset(void) {
    if (rig_load_image(image_path, BASE) != 0) return -1;
    static uint8_t zero[V_HI - V_LO];
    rig_poke(V_LO, zero, sizeof zero);
    rig_hooks_clear();
    return 0;
}

/* a record as the harness writes it into the rig and the port */
typedef struct {
    int w0, w2, b4, cell_x, cell_z, fine_x, fine_z, b20, cls, id, group, target, repeat, range, heading, speed;
    int32_t alt8, pos[3];
    int16_t mat[9];
} rec_t;

typedef struct {
    int mode, n, designated, viewed, hud_row, view_off, frame;
    int32_t cam_y;
    int sel;               /* E+0x63 of the player */
    int threat_prev, holdoff, mission1, req_target, page, countdown;
    int16_t rot[9];
    rec_t r[16];
} cs_t;

static void pos_cells(rec_t *r) {
    r->cell_x = (int)(r->pos[0] >> 22); r->cell_z = (int)(r->pos[2] >> 22);
    r->fine_x = (int)((r->pos[0] & 0x3FFFFF) >> 8); r->fine_z = (int)((r->pos[2] & 0x3FFFFF) >> 8);
    r->alt8 = r->pos[1] >> 8;
}

static void gen_scene(cs_t *c, int near) {
    memset(c, 0, sizeof *c);
    static const int modes[] = { 3, 3, 3, 4, 5, 5, 6, 2, 0x7D, 1 };
    c->mode = modes[rnd(0, 9)];
    c->n = pct(30) ? 4 * rnd(0, 20) : rnd(0, 200);
    c->sel = (pct(50) ? 0x10 : pct(50) ? 0x20 : 0x30) | (pct(33) ? 0x0D : pct(50) ? 0x0B : 0x09);
    c->viewed = 0;
    c->designated = -1;
    c->threat_prev = pct(50) ? 0 : rnd(0, 63) & 0x3E;
    c->holdoff = pct(50) ? 0 : rnd(-5, 40);
    c->mission1 = pct(70) ? 0 : rnd(0, 40);
    for (int i = 0; i < 9; i++) c->rot[i] = (int16_t)rnd(-256, 256);
    if (pct(60)) { int16_t id9[9] = { 256, 0, 0, 0, 256, 0, 0, 0, 256 }; memcpy(c->rot, id9, sizeof id9); }
    const int32_t cx = rnd(60, 70) * 4194304 + rnd(0, 4194303), cz = rnd(60, 70) * 4194304 + rnd(0, 4194303);
    for (int i = 0; i < 16; i++) {
        rec_t *r = &c->r[i];
        static const int cls_tab[] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x00, 0x01, 0x20, 0x30, 0x31, 0x20, 0x12, 0x12, 0x00, 0x12 };
        r->cls = i == 0 ? 0x11 : cls_tab[rnd(0, (int)(sizeof cls_tab / sizeof cls_tab[0]) - 1)];
        r->w0 = (i == 0 || !pct(20)) ? 0x0040 : 0;
        if (pct(25)) r->w0 |= 0x0008;
        if (pct(10)) r->w0 |= 0x0200;
        if (pct(10)) r->w0 |= 0x0400;
        if (pct(15)) r->w0 |= 0x4000;
        if (i == 0) r->w0 = 0x11C8;
        r->w2 = pct(10) ? 0x0080 : 0;
        r->b20 = (pct(15) ? 0x02 : 0) | (pct(40) ? 0x40 : 0);
        r->group = i;
        r->target = pct(40) ? 0x80 : pct(30) ? 0x80 + rnd(0, 3) : 0xFF;
        r->id = 0;
        int32_t span = near ? rnd(2, 5) * 8192 * 256 : (pct(60) ? 8192 * 256 * 30 : pct(60) ? 0x7FFFFF : 0x30000000);
        r->pos[0] = cx + rnd(-span, span); r->pos[2] = cz + rnd(-span, span); r->pos[1] = rnd(0x10000, 0x600000);
        if (pct(5)) r->pos[1] = 0x7FFFFFF;
        if (i == 0) { r->pos[0] = cx; r->pos[2] = cz; }
        pos_cells(r);
        r->heading = rnd(0, 28799); r->speed = rnd(-3000, 3000); r->repeat = 0; r->range = 0;
    }
    c->hud_row = pct(50) ? 0 : rnd(-3, 3); c->view_off = pct(50) ? 0 : rnd(-20, 20); c->cam_y = pct(50) ? 0 : rnd(-0x300000, 0x300000); c->frame = rnd(0, 255);
    c->r[0].id = (uint8_t)c->sel;
    c->r[0].b20 = 0;
    c->designated = pct(40) ? -1 : rnd(1, 15);
    c->req_target = pct(60);
    c->page = pct(50) ? 0 : rnd(0, 3);
    c->countdown = rnd(-2, 6);
}

static void write_rec(uint32_t base, const rec_t *r) {
    put16(base + 0x00, r->w0); put16(base + 0x02, r->w2); put8(base + 0x04, r->b4);
    put16(base + 0x06, r->cell_x); put16(base + 0x08, r->cell_z); put16(base + 0x0C, r->fine_x); put16(base + 0x0E, r->fine_z); put32(base + 0x10, r->alt8);
    put32(base + 0x14, r->pos[0]); put32(base + 0x18, r->pos[1]); put32(base + 0x1C, r->pos[2]);
    put8(base + 0x20, r->b20); put8(base + 0x38, r->target); put8(base + 0x39, r->repeat); put16(base + 0x4A, r->range);
    put8(base + 0x5E, r->group); put8(base + 0x62, r->cls); put8(base + 0x63, r->id);
    put16(base + 0x68, r->heading); put16(base + 0x6E, r->speed);
}

static void rig_write(const cs_t *c, int cockpit, int redraw) {
    put8(0x4A95E, c->mode); put8(0x4A83D, cockpit ? 0 : 1); put8(0x4A8EF, redraw); put16(0x4A992, c->n);
    put16(0x4A996, 0); put16(0x4A994, 0);
    put16(0x4AA78, c->designated < 0 ? 0xFFFF : c->designated * 0x200);
    put8(0x4A926, c->threat_prev); put8(0x4A93F, c->holdoff); put8(0x4A927, c->mission1); put8(0x4A871, c->req_target);
    put16(0x4AA7C, c->page); put8(0x4A93E, c->countdown);
    put8(0x4A84D, 1); put16(0x4A984, 0x0840); put16(0x4A990, c->hud_row); put16(0x4AA40, c->view_off); put32(0x4AB30, c->cam_y); put8(0x4A93B, c->frame);
    for (int i = 0; i < 9; i++) put16(0x4ACB4 + 2 * i, c->rot[i]);
    for (int i = 0; i < 16; i++) write_rec(REC + (uint32_t)i * 0x200u, &c->r[i]);
}

/* ---- the port ---------------------------------------------------------------------------------------------------------------------------- */
static entities_t E;
static aircraft_t PLAYER;
static radar_t R;

static void load_port(const cs_t *c) {
    memset(&E, 0, sizeof E);
    entities_init(&E, NULL, NULL, NULL);
    memset(&PLAYER, 0, sizeof PLAYER);
    entities_attach_player(&E, &PLAYER);
    for (int i = 0; i < 16; i++) {
        aircraft_t *a = entities_ac(&E, i);
        flight_state_t *f = &a->f;
        const rec_t *r = &c->r[i];
        f->w0 = (uint16_t)r->w0; f->w2 = (uint16_t)r->w2; f->b4 = (uint8_t)r->b4; f->cell_x = (int16_t)r->cell_x; f->cell_z = (int16_t)r->cell_z;
        f->fine_x = (int16_t)r->fine_x; f->fine_z = (int16_t)r->fine_z; f->alt8 = r->alt8; f->b20 = (uint8_t)r->b20; f->cls = (uint8_t)r->cls; f->id = (uint8_t)r->id;
        f->heading = (uint16_t)r->heading; f->speed = (int16_t)r->speed; f->repeat = (uint8_t)r->repeat; f->group = (uint8_t)r->group;
        for (int k = 0; k < 3; k++) a->pos[k] = r->pos[k];
        E.rec[i].target = (uint8_t)r->target; E.rec[i].range = (int16_t)r->range;
    }
    E.mode = (uint8_t)c->mode; E.designated = (int16_t)c->designated;
    PLAYER.f.v4a984 = 0x0840;
    radar_reset(&R);
    R.frame = (uint8_t)c->frame;
    R.threat_prev = (uint8_t)c->threat_prev; R.holdoff = (int8_t)c->holdoff; R.mission1 = (uint8_t)c->mission1; R.req_target = (uint8_t)c->req_target;
    R.page = (uint16_t)c->page; R.countdown = (int8_t)c->countdown;
}

static int diffs, cases;
static int verbose;
static void report(const char *part, int idx, const char *what, long port, long rig) {
    diffs++;
    if (verbose || diffs <= 12) printf("  %s case %d: %s: port %ld (0x%lX), rig %ld (0x%lX)\n", part, idx, what, port, port & 0xFFFFFFFFul, rig, rig & 0xFFFFFFFFul);
}
#define CHK(name, pv, rv) do { if (!bad && (long)(pv) != (long)(rv)) { bad = name; pv_ = (long)(pv); rv_ = (long)(rv); } } while (0)

static int run_rig(uint32_t addr) {
    for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
    return rig_run(addr);
}

static void part_list(int N) {
    for (int k = 0; k < N; k++) {
        cs_t c; gen_scene(&c, pct(50));
        rig_reset(); rig_write(&c, 1, 2);
        if (run_rig(0x29D28) != 0 || run_rig(0x1FEE6) != 0) { printf("  list case %d: rig run failed (%d)\n", k, rig_last_error); diffs++; continue; }
        load_port(&c);
        radar_build(&R, &E, c.rot);
        cases++;
        const char *bad = NULL; long pv_ = 0, rv_ = 0;
        int n = 0;
        while (n < 17 && (get32(LISTA + 16 * n + 0) || get32(LISTA + 16 * n + 4) || get32(LISTA + 16 * n + 8) || get16(LISTA + 16 * n + 12))) n++;
        CHK("count", R.nlist, n);
        for (int i = 0; i < R.nlist && i < n && !bad; i++) {
            CHK("x", R.list[i].x, get32(LISTA + 16 * i + 0));
            CHK("z", R.list[i].z, get32(LISTA + 16 * i + 8));
            CHK("flag", R.list[i].flag, get16(LISTA + 16 * i + 12) & 0xFFFF);
        }
        if (bad) { printf("  [mode %d n %d]\n", c.mode, c.n); report("list", k, bad, pv_, rv_); }
    }
}

/* the player's range routine 0x2835A, called directly (all registers 0): E+0x4A, the counter byte E+0x39 and B4 bit 0 */
static void part_range(int N) {
    for (int k = 0; k < N; k++) {
        cs_t c; gen_scene(&c, 1);
        c.mode = 3;
        c.designated = rnd(1, 15);
        rec_t *p = &c.r[0], *t = &c.r[c.designated];
        const int closeness = rnd(0, 3);
        const int32_t sp = closeness == 0 ? 0x7000 : closeness == 1 ? 0x30000 : closeness == 2 ? 0x300000 : 0x1000000;
        t->pos[0] = p->pos[0] + rnd(-sp, sp); t->pos[2] = p->pos[2] + rnd(-sp, sp); t->pos[1] = p->pos[1] + rnd(-sp, sp);
        if (t->pos[1] < 0) t->pos[1] = 0;
        pos_cells(t);
        p->repeat = rnd(0, 255); p->b4 = pct(50) ? 1 : 0;
        p->range = pct(30) ? 0x7FFF : pct(40) ? rnd(0, 0x480) : rnd(0, 0x3000);
        rig_reset(); rig_write(&c, 1, 0);
        put8(REC + 0x04, p->b4);
        for (int rr = RIG_D0; rr <= RIG_A6; rr++) rig_set_reg((rig_reg_t)rr, rr >= RIG_A0 ? REC : 0);
        if (rig_run(0x2835A) != 0) { printf("  range case %d: rig run failed (%d)\n", k, rig_last_error); diffs++; continue; }
        load_port(&c);
        E.rec[0].range = (int16_t)p->range; PLAYER.f.repeat = (uint8_t)p->repeat; PLAYER.f.b4 = (uint8_t)p->b4;
        entities_update_player_range(&E);
        cases++;
        const char *bad = NULL; long pv_ = 0, rv_ = 0;
        CHK("range +0x4A", (uint16_t)E.rec[0].range, get16(REC + 0x4A) & 0xFFFF);
        CHK("counter +0x39", PLAYER.f.repeat, get8(REC + 0x39));
        CHK("B4 bit 0", PLAYER.f.b4 & 1, get8(REC + 4) & 1);
        if (bad) {
            printf("  [repeat %02X old range %04X b4 %d: target d (%d,%d,%d)]\n", p->repeat, (uint16_t)p->range, p->b4, t->cell_x - p->cell_x, t->cell_z - p->cell_z, t->alt8 - p->alt8);
            report("range", k, bad, pv_, rv_);
        }
    }
}

/* the lock state 0x38884 (A registers = the player's record): V:4AC08, V:4AC0C and V:4A96C after one call */
static void part_lock(int N) {
    for (int k = 0; k < N; k++) {
        cs_t c; gen_scene(&c, 1);
        c.mode = 3;
        c.designated = pct(90) ? rnd(1, 15) : -1;
        static const int sels[] = { 0x10, 0x20, 0x30, 0x20, 0x30, 0x00, 0x40 };
        const int vi = pct(70) ? 0 : rnd(1, 15);   /* the viewed record V:4A996: the lock state reads ITS selector and range (record 0 holds decoys) */
        c.r[vi].id = (uint8_t)(sels[rnd(0, 6)] | 0x0D);
        c.r[vi].range = pct(20) ? 0x7FFF : pct(50) ? rnd(0x1000, 0x2800) : rnd(0, 0x1900);
        if (vi) { c.r[0].id = (uint8_t)(sels[rnd(0, 6)] | 0x0D); c.r[0].range = rnd(0, 0x8000); }
        const int mx = pct(15) ? -1 : pct(15) ? rnd(0, 320) : rnd(90, 230), my = rnd(40, 140);
        const int tx = pct(70) ? mx + rnd(-8, 8) : rnd(-1, 300), ty = pct(70) ? my + rnd(-8, 8) : rnd(-1, 190);
        const int closing = pct(40) ? rnd(-1500, -600) : rnd(-100, 700);
        const int prompt = rnd(0, 2), ev08 = pct(30) ? 0 : rnd(0, 3) == 0 ? 0x4000 : rnd(0, 1) ? 0x0200 : 0x4200, ev0c = pct(80) ? 0 : 0x40;
        rig_reset(); rig_write(&c, 1, 0);
        put16(0x4A9FA, mx); put16(0x4A9FC, my); put16(0x4A9F2, tx); put16(0x4A9F4, ty);
        put16(0x4ABFE, closing); put8(0x4A96C, prompt); put32(0x4AC08, ev08); put32(0x4AC0C, ev0c);
        put8(0x4A83D, 0); put16(0x4A996, vi * 0x200); put16(0x4A994, vi);
        for (int rr = RIG_D0; rr <= RIG_A6; rr++) rig_set_reg((rig_reg_t)rr, rr >= RIG_A0 ? REC : 0);
        if (rig_run(0x38884) != 0) { printf("  lock case %d: rig run failed (%d)\n", k, rig_last_error); diffs++; continue; }
        load_port(&c);
        for (int i = 0; i < 16; i++) { E.rec[i].range = (int16_t)c.r[i].range; entities_ac(&E, i)->f.id = c.r[i].id; }
        PLAYER.f.v4ac08 = ev08; PLAYER.f.v4ac0c = (uint32_t)ev0c; PLAYER.f.v4a996 = (uint16_t)(vi * 0x200); PLAYER.f.v4a994 = (uint16_t)vi;
        R.mx = (int16_t)mx; R.my = (int16_t)my; R.tpx = (int16_t)tx; R.tpy = (int16_t)ty; R.closing = (int16_t)closing; R.prompt = (uint8_t)prompt;
        radar_in_t in; memset(&in, 0, sizeof in);
        in.e = &E; in.n = (uint32_t)c.n; in.cockpit = 1; in.zoom = 0x80;
        radar_lock_state(&R, &in);
        cases++;
        const char *bad = NULL; long pv_ = 0, rv_ = 0;
        CHK("V:4AC08", PLAYER.f.v4ac08, get32(0x4AC08));
        CHK("V:4AC0C", PLAYER.f.v4ac0c, get32(0x4AC0C));
        CHK("V:4A96C", R.prompt, get8(0x4A96C));
        if (bad) {
            printf("  [viewed %d sel %02X range %04X marker (%d,%d) tp (%d,%d) closing %d prompt %d ev08 %04X ev0c %02X n&1F %d target %d]\n", vi, c.r[vi].id, (uint16_t)c.r[vi].range, mx, my, tx, ty, closing, prompt, ev08, ev0c, c.n & 0x1F, c.designated);
            report("lock", k, bad, pv_, rv_);
        }
    }
}

/* the sight 0x385F4 with its tail 0x3878E (A registers = the player's record): the prompt state, the cues, the pipper, the aim-point history, the range-number state */
static void part_sight(int N) {
    for (int k = 0; k < N; k++) {
        cs_t c; gen_scene(&c, 1);
        c.mode = 3;
        c.designated = pct(85) ? rnd(1, 15) : -1;
        static const int sels[] = { 0x10, 0x10, 0x20, 0x30, 0x00 };
        const int vi = pct(65) ? 0 : rnd(1, 15), paused = pct(30);   /* the viewed record V:4A996 (the sight reads ITS selector, range and B4) and the script's count V:4A866 */
        c.r[vi].id = (uint8_t)(sels[rnd(0, 4)] | 0x0D);
        c.r[vi].range = pct(20) ? 0x7FFF : pct(50) ? rnd(0, 0x900) : rnd(0, 0x8000);
        c.r[vi].b4 = pct(50) ? 1 : 0;
        if (vi) { c.r[0].id = (uint8_t)(sels[rnd(0, 4)] | 0x0D); c.r[0].range = rnd(0, 0x8000); c.r[0].b4 = pct(50) ? 1 : 0; }   /* decoys in record 0 */
        int16_t mat14[9];
        const int ident = pct(40);
        for (int i = 0; i < 9; i++) mat14[i] = (int16_t)(ident ? ((i % 4 == 0) ? 16384 : 0) : rnd(-16384, 16384));
        int16_t cam[9];
        for (int i = 0; i < 9; i++) cam[i] = (int16_t)(pct(50) ? ((i % 4 == 0) ? 256 : 0) : rnd(-256, 256));
        int32_t hist[2][3];
        const int vis = pct(60);
        for (int a = 0; a < 2; a++) for (int b = 0; b < 3; b++) hist[a][b] = c.r[vi].pos[b] + (vis ? (b == 2 ? rnd(2000, 20000) * 256 : rnd(-1500, 1500) * 256) : rnd(-0x2000000, 0x2000000));
        if (vis) for (int i = 0; i < 9; i++) cam[i] = (int16_t)((i % 4 == 0) ? 256 : 0);
        const int px = pct(60) ? rnd(60, 260) : rnd(-5, 330), py = rnd(30, 150);
        const int tx = pct(60) ? px + rnd(-10, 10) : rnd(-1, 300), ty = pct(60) ? py + rnd(-10, 10) : rnd(-1, 190);
        const int prompt = rnd(0, 2), last_range = rnd(0, 0x8000), closing = rnd(-200, 200);
        rig_reset(); rig_write(&c, 1, 0);
        for (int i = 0; i < 9; i++) { put16(0x4AC90 + 2 * i, cam[i]); put16(REC + (uint32_t)vi * 0x200u + 0x92 + 2 * (uint32_t)i, mat14[i]); }
        put16(0x4AA3C, 0x7FFF); put16(0x4AAFA, 0x80);
        put16(0x4A9F6, px); put16(0x4A9F8, py); put16(0x4A9F2, tx); put16(0x4A9F4, ty);
        for (int a = 0; a < 2; a++) for (int b = 0; b < 3; b++) put32((a == 0 ? 0x4A7CE : 0x4A7DA) + 4 * b, hist[a][b]);
        put16(0x4ABFC, last_range); put16(0x4ABFE, closing); put8(0x4A96C, prompt);
        put8(0x4A83D, 0); put16(0x4A996, vi * 0x200); put16(0x4A994, vi); put8(0x4A866, paused);
        for (int rr = RIG_D0; rr <= RIG_A6; rr++) rig_set_reg((rig_reg_t)rr, rr >= RIG_A0 ? REC : 0);
        if (rig_run(0x385F4) != 0) { printf("  sight case %d: rig run failed (%d)\n", k, rig_last_error); diffs++; continue; }
        load_port(&c);
        for (int i = 0; i < 16; i++) E.rec[i].range = (int16_t)c.r[i].range;
        PLAYER.f.v4ac0c = 0; PLAYER.f.v4a996 = (uint16_t)(vi * 0x200); PLAYER.f.v4a994 = (uint16_t)vi;
        aircraft_t *va = entities_ac(&E, vi);
        va->f.id = c.r[vi].id; va->f.b4 = (uint8_t)c.r[vi].b4;
        for (int i = 0; i < 9; i++) va->f.mat_b[i] = mat14[i];
        R.px = (int16_t)px; R.py = (int16_t)py; R.tpx = (int16_t)tx; R.tpy = (int16_t)ty; R.last_range = (int16_t)last_range; R.closing = (int16_t)closing; R.prompt = (uint8_t)prompt;
        memcpy(R.hist, hist, sizeof hist);
        fx_mat_t cm; for (int i = 0; i < 9; i++) cm.m[i / 3][i % 3] = cam[i];
        radar_in_t in; memset(&in, 0, sizeof in);
        in.e = &E; in.n = (uint32_t)c.n; in.cockpit = 1; in.zoom = 0x80; in.cam = &cm; in.paused = (uint8_t)paused;
        radar_sight(&R, &in);
        cases++;
        const char *bad = NULL; long pv_ = 0, rv_ = 0;
        CHK("V:4AC0C", PLAYER.f.v4ac0c, get32(0x4AC0C));
        CHK("V:4A96C", R.prompt, get8(0x4A96C));
        CHK("V:4A9F6", R.px, get16(0x4A9F6)); if (R.px != 0) CHK("V:4A9F8", R.py, get16(0x4A9F8));   /* a rejected projection stores x = 0 and a leftover register in y, read only with x above 0 (note 06 prompt rule): unobservable, not compared */
        for (int b = 0; b < 3; b++) { CHK("V:4A7CE", R.hist[0][b], get32(0x4A7CE + 4 * b)); CHK("V:4A7DA", R.hist[1][b], get32(0x4A7DA + 4 * b)); }
        CHK("V:4ABFC", R.last_range, get16(0x4ABFC)); CHK("V:4ABFE", R.closing, get16(0x4ABFE));
        CHK("B4", va->f.b4 & 1, get8(REC + (uint32_t)vi * 0x200u + 4) & 1);
        for (int i = 0; i < 16 && !bad; i++) if (i != vi) CHK("B4 of another record", entities_ac(&E, i)->f.b4 & 1, get8(REC + (uint32_t)i * 0x200u + 4) & 1);
        if (bad) {
            printf("  [viewed %d paused %d sel %02X range %04X designated %d marker/pip (%d,%d) tp (%d,%d) prompt %d last %d closing %d b4 %d ident %d]\n", vi, paused, c.r[vi].id, (uint16_t)c.r[vi].range, c.designated, px, py, tx, ty, prompt, last_range, closing, c.r[vi].b4, ident);
            printf("  [rig pipper words %d %d, port %d %d]\n", get16(0x4A9F6), get16(0x4A9F8), R.px, R.py);
            report("sight", k, bad, pv_, rv_);
        }
    }
}

/* The marker slew 0x38D8C: the four-iteration loop, called directly (registers 0; the line routine 0x3455E returns at once, its pixels are the picture). Varied: the viewed record's selector
 * +0x63, the target point V:4A9EE/F0 (positive or not), the marker V:4A9FA/FC (valid or not), the lock box bit 0x4000 of V:4AC08, the script's count V:4A866, record 0's +0x56 (the jitter gate)
 * and +0x14 (its index), V:4AA40 and V:4A990. Compared: the marker, and the point V:4A9F2/F4 (copied by a completed loop only, which the rig shows by V:4A9EE = -1). */
static void part_slew(int N) {
    for (int k = 0; k < N; k++) {
        cs_t c; gen_scene(&c, 1);
        c.mode = 3;
        static const int sels[] = { 0x00, 0x10, 0x20, 0x30, 0x20 };
        c.r[0].id = (uint8_t)(sels[rnd(0, 4)] | 0x0D);
        c.r[0].pos[0] = (int32_t)xr();
        c.view_off = pct(50) ? 0 : rnd(-300, 800); c.hud_row = pct(60) ? 0 : rnd(-100, 6);
        int tx = pct(75) ? rnd(1, 300) : rnd(-4, 300), ty = pct(75) ? rnd(1, 190) : rnd(-4, 190);
        if (pct(15)) tx = rnd(1, 2);   /* the box point's edge: the jitter pair and V:4AA40 decide */
        if (pct(15)) ty = rnd(1, 2);
        if (pct(10)) c.view_off = rnd(-3, 0);
        if (tx == -1) tx = -2;   /* -1 is the rig's completion mark in V:4A9EE */
        int mx = pct(75) ? rnd(60, 260) : rnd(-3, 2), my = rnd(20, 170);
        if (pct(15)) { tx = mx + rnd(-3, 3); ty = my + rnd(-3, 3); }
        const int box = pct(50), paused = pct(25), rate_p = pct(50) ? 0 : rnd(-300, 300), tpx = rnd(-5, 300), tpy = rnd(-5, 190);
        rig_reset(); rig_write(&c, 1, 0);
        put16(0x3455E, 0x4E75);
        put16(REC + 0x56, rate_p); put8(0x4A866, paused); put32(0x4AC08, box ? 0x4000 : 0);
        put16(0x4A9EE, tx); put16(0x4A9F0, ty); put16(0x4A9FA, mx); put16(0x4A9FC, my); put16(0x4A9F2, tpx); put16(0x4A9F4, tpy);
        if (run_rig(0x38D8C) != 0) { printf("  slew case %d: rig run failed (%d)\n", k, rig_last_error); diffs++; continue; }
        load_port(&c);
        PLAYER.f.rate_p = (int16_t)rate_p; PLAYER.f.v4ac08 = box ? 0x4000 : 0; PLAYER.f.id = c.r[0].id;
        R.mx = (int16_t)mx; R.my = (int16_t)my; R.tpx = (int16_t)tpx; R.tpy = (int16_t)tpy;
        radar_in_t in; memset(&in, 0, sizeof in);
        in.e = &E; in.n = (uint32_t)c.n; in.cockpit = 1; in.zoom = 0x80; in.paused = (uint8_t)paused; in.view_off = (int16_t)c.view_off; in.hud_row = (int16_t)c.hud_row;
        in.tgt_x = (int16_t)tx; in.tgt_y = (int16_t)ty;
        radar_slew(&R, &in);
        cases++;
        const char *bad = NULL; long pv_ = 0, rv_ = 0;
        CHK("V:4A9FA", R.mx, get16(0x4A9FA)); CHK("V:4A9FC", R.my, get16(0x4A9FC));
        CHK("V:4A9F2", R.tpx, get16(0x4A9F2)); CHK("V:4A9F4", R.tpy, get16(0x4A9F4));
        if (bad) {
            printf("  [sel %02X target (%d,%d) marker (%d,%d) box %d paused %d rate_p %d pos_x %08X view_off %d hud_row %d prev tp (%d,%d); rig V:4A9EE %d, V:4AA62 %d]\n", c.r[0].id, tx, ty, mx, my, box, paused, rate_p, (unsigned)c.r[0].pos[0], c.view_off, c.hud_row, tpx, tpy, get16(0x4A9EE), get16(0x4AA62));
            report("slew", k, bad, pv_, rv_);
        }
    }
}

/* the message-line driver 0x36DB2 (A registers = the player's record): its state only (V:4AA7C, V:4A93E, V:4A93F, V:4A8F4); the text it draws goes to the screen planes */
static void part_line(int N) {
    for (int k = 0; k < N; k++) {
        cs_t c; gen_scene(&c, 1);
        c.mode = 3;
        c.designated = pct(80) ? rnd(1, 15) : -1;
        c.page = pct(30) ? 0 : pct(30) ? (0x8000 | rnd(0, 5)) : pct(10) ? 0xFFFF : rnd(1, 4);
        c.countdown = pct(30) ? -1 : rnd(-2, 6);
        c.holdoff = pct(40) ? -1 : pct(30) ? 0x18 : rnd(-3, 30);
        const int a984 = 0x0840 | (pct(20) ? 1 : 0) | (pct(15) ? 0x80 : 0), redraw = pct(50) ? 0 : rnd(0, 3);
        rig_reset(); rig_write(&c, 1, 0);
        put16(0x4A984, a984); put8(0x4A8F4, redraw); put16(0x4AA7C, c.page);
        put8(0x4A83D, 0); put8(0x4A859, 1); put16(0x4AA78, c.designated < 0 ? 0xFFFF : c.designated * 0x200);
        for (int rr = RIG_D0; rr <= RIG_A6; rr++) rig_set_reg((rig_reg_t)rr, rr >= RIG_A0 ? REC : 0);
        if (rig_run(0x36DB2) != 0) { printf("  line case %d: rig run failed (%d)\n", k, rig_last_error); diffs++; continue; }
        load_port(&c);
        PLAYER.f.v4a984 = (uint16_t)a984; R.line_redraw = (int8_t)redraw;
        radar_in_t in; memset(&in, 0, sizeof in);
        in.e = &E; in.n = (uint32_t)c.n; in.cockpit = 1; in.zoom = 0x80;
        radar_line_driver(&R, &in);
        cases++;
        const char *bad = NULL; long pv_ = 0, rv_ = 0;
        CHK("page V:4AA7C", R.page, get16(0x4AA7C) & 0xFFFF);
        CHK("countdown V:4A93E", (uint8_t)R.countdown, get8(0x4A93E));
        CHK("holdoff V:4A93F", (uint8_t)R.holdoff, get8(0x4A93F));
        CHK("redraw V:4A8F4", (uint8_t)R.line_redraw, get8(0x4A8F4));
        if (bad) {
            printf("  [page %04X countdown %d holdoff %d redraw %d a984 %04X target %d]\n", c.page, c.countdown, c.holdoff, redraw, a984, c.designated);
            report("line", k, bad, pv_, rv_);
        }
    }
}

static void part_walk(int N, int with_desig) {
    const char *part = with_desig ? "desig" : "walk";
    for (int k = 0; k < N; k++) {
        cs_t c; gen_scene(&c, pct(70));
        if (!with_desig) c.req_target = 0;
        c.n &= ~3;   /* the walk runs on n mod 4 = 0 and 1; the builder on 0 */
        rig_reset(); rig_write(&c, 1, 0);
        if (run_rig(0x29D28) != 0 || run_rig(0x1FEE6) != 0) { printf("  %s case %d: list run failed\n", part, k); diffs++; continue; }
        if (run_rig(0x35E5E) != 0) { printf("  %s case %d: rig run failed (%d)\n", part, k, rig_last_error); diffs++; continue; }
        load_port(&c);
        radar_in_t in; memset(&in, 0, sizeof in);
        in.e = &E; in.n = (uint32_t)c.n; in.cockpit = 1; in.zoom = 0x80; in.rot = c.rot; in.cam_y = c.cam_y; in.hud_row = (int16_t)c.hud_row; in.view_off = (int16_t)c.view_off;
        radar_pass(&R, &in);
        cases++;
        const char *bad = NULL; long pv_ = 0, rv_ = 0;
        CHK("threat V:4A925", R.threat, get8(0x4A925));
        CHK("prev V:4A926", R.threat_prev, get8(0x4A926));
        CHK("cue V:4AC0C", PLAYER.f.v4ac0c, get32(0x4AC0C));
        CHK("holdoff V:4A93F", (uint8_t)R.holdoff, get8(0x4A93F));
        CHK("mission1 V:4A927", R.mission1, get8(0x4A927));
        for (int i = 1; i < 16 && !bad; i++) {
            const uint32_t b = REC + (uint32_t)i * 0x200u;
            CHK("w0", entities_ac(&E, i)->f.w0, get16(b) & 0xFFFF);
            CHK("b20", entities_ac(&E, i)->f.b20, get8(b + 0x20));
        }
        for (int i = 0; i < R.nlist && !bad; i++) CHK("flag", R.list[i].flag, get16(LISTA + 16 * i + 12) & 0xFFFF);
        CHK("frame V:4A93B", R.frame, get8(0x4A93B));
        {   /* the blips of the page the walk wrote (V:4A724 = 0: 0x537C8): x (bit 15: two pixels wide) and y per entry, then the 0xFFFF terminator */
            const int nb = R.nblip[R.last_page];
            for (int i = 0; i < nb && !bad; i++) {
                CHK("blip x", R.blip[R.last_page][i].x | (R.blip[R.last_page][i].wide ? 0x8000 : 0), get16(0x537C8 + 4 * i) & 0xFFFF);
                CHK("blip y", R.blip[R.last_page][i].y, get16(0x537C8 + 4 * i + 2));
            }
            if (nb < 10) CHK("blip terminator", 0xFFFF, get16(0x537C8 + 4 * nb) & 0xFFFF);
        }
        if (with_desig) {
            CHK("designated", E.designated < 0 ? 0xFFFF : E.designated * 0x200, get16(0x4AA78) & 0xFFFF);
            CHK("page V:4AA7C", R.page, get16(0x4AA7C) & 0xFFFF);
            CHK("countdown V:4A93E", (uint8_t)R.countdown, get8(0x4A93E));
            CHK("listpos V:4A920", R.listpos, get8(0x4A920));
        }
        if (bad && verbose) {
            for (int i = 0; i < R.nlist; i++) {
                const int id = R.list[i].flag >> 8; const flight_state_t *f = &entities_ac(&E, id)->f;
                printf("    entry %d rec %d: x %d z %d port flag %04X rig flag %04X | w0 %04X w2 %04X cls %02X b20 %02X rig w0 %04X b20 %02X\n", i, id, R.list[i].x, R.list[i].z, R.list[i].flag, get16(LISTA + 16 * i + 12) & 0xFFFF,
                       c.r[id].w0, c.r[id].w2, c.r[id].cls, c.r[id].b20, get16(REC + id * 0x200) & 0xFFFF, get8(REC + id * 0x200 + 0x20));
                (void)f;
            }
        }
        if (bad) report(part, k, bad, pv_, rv_);
    }
}

/* The scope's erase lists in memory over several walks (12.3 steps 1 and 4): two pages of ten entries at 0x537C8 and 0x537F0, the terminator after the last stored entry (past the page after ten: the
 * first word of the other page, or 0x53818), the walk alternating the pages. Random plausible lists are written first; the 42 words of the memory and the page word are compared after each walk. */
static void part_pages(int N) {
    for (int k = 0; k < N; k++) {
        cs_t c; gen_scene(&c, 1);
        c.req_target = 0;
        c.n &= ~3;
        rig_reset(); rig_write(&c, 1, 0);
        uint16_t mem[RADAR_EMEM];
        for (int i = 0; i < RADAR_EMEM; i++) mem[i] = (uint16_t)((i & 1) ? rnd(0, 199) : (pct(25) ? 0xFFFF : (rnd(0, 319) | (pct(40) ? 0x8000 : 0))));
        mem[0] = pct(30) ? 0xFFFF : mem[0]; mem[20] = pct(30) ? 0xFFFF : mem[20]; mem[40] = pct(50) ? 0xFFFF : mem[40];
        const int page = rnd(0, 1);
        for (int i = 0; i < RADAR_EMEM; i++) put16(0x537C8 + 2 * (uint32_t)i, mem[i]);
        put16(0x4A724, page);
        load_port(&c);
        memcpy(R.emem, mem, sizeof mem);
        R.cur_page = (uint8_t)page;
        for (int w = 0; w < 4; w++) {
            if (run_rig(0x29D28) != 0 || run_rig(0x1FEE6) != 0) { printf("  pages case %d: list run failed\n", k); diffs++; break; }
            if (run_rig(0x35E5E) != 0) { printf("  pages case %d: rig run failed (%d)\n", k, rig_last_error); diffs++; break; }
            radar_in_t in; memset(&in, 0, sizeof in);
            in.e = &E; in.n = (uint32_t)c.n; in.cockpit = 1; in.zoom = 0x80; in.rot = c.rot; in.cam_y = c.cam_y; in.hud_row = (int16_t)c.hud_row; in.view_off = (int16_t)c.view_off;
            radar_pass(&R, &in);
            cases++;
            const char *bad = NULL; long pv_ = 0, rv_ = 0;
            for (int i = 0; i < RADAR_EMEM && !bad; i++) CHK("erase memory word", R.emem[i], get16(0x537C8 + 2 * (uint32_t)i) & 0xFFFF);
            CHK("page used", R.last_page, page ^ (w & 1));   /* the walk routine itself leaves V:4A724 alone (the rig); the caller alternates it, the port after every walk */
            put16(0x4A724, get16(0x4A724) ^ 1);
            if (bad) { printf("  [walk %d of the case, start page %d, %d blips on the page]\n", w, page, R.nblip[R.last_page]); report("pages", k, bad, pv_, rv_); break; }
        }
    }
}

/* The scope draw 0x35CF2 (its gate 0x35BB6, the redraw counter V:4A8EF and the step counter n choose the cone lines 0x35D66, the plus 0x35DDE and the walk 0x35E5E; with V:4A8EF of 1 or more it walks TWICE, note 07 12.1)
 * against radar_pass with the same counter: the builder 0x1FEE6 runs first (it sets the walk's enable V:4A8F0 from n and V:4A8EF), then the draw. One call per case from random prefilled erase
 * memory; compared: the threat bits, the cue long, the hold-off, the mission-1 counter, V:4A93B (walks counted), the records' W0 and B20, the list flags, the designation and the 42 words of erase memory. */
static void part_scope(int N) {
    for (int k = 0; k < N; k++) {
        cs_t c; gen_scene(&c, 1);
        const int redraw = rnd(0, 3);
        rig_reset(); rig_write(&c, 1, 0);
        uint16_t mem[RADAR_EMEM];
        for (int i = 0; i < RADAR_EMEM; i++) mem[i] = (uint16_t)((i & 1) ? rnd(0, 199) : (pct(25) ? 0xFFFF : (rnd(0, 319) | (pct(40) ? 0x8000 : 0))));
        mem[0] = pct(30) ? 0xFFFF : mem[0]; mem[20] = pct(30) ? 0xFFFF : mem[20]; mem[40] = pct(50) ? 0xFFFF : mem[40];
        for (int i = 0; i < RADAR_EMEM; i++) put16(0x537C8 + 2 * (uint32_t)i, mem[i]);
        put16(0x4A724, 0); put8(0x4A8EF, redraw); put16(0x4AA3E, 0);
        put16(0x34558, 0x4E75);   /* the cone lines' line routine returns at once (its pixels are the picture, not the state under test) */
        if (run_rig(0x29D28) != 0 || run_rig(0x1FEE6) != 0) { printf("  scope case %d: list run failed\n", k); diffs++; continue; }
        if (run_rig(0x35CF2) != 0) { printf("  scope case %d: rig run failed (%d)\n", k, rig_last_error); diffs++; continue; }
        load_port(&c);
        memcpy(R.emem, mem, sizeof mem);
        R.cur_page = 0;
        radar_in_t in; memset(&in, 0, sizeof in);
        in.e = &E; in.n = (uint32_t)c.n; in.cockpit = 1; in.zoom = 0x80; in.rot = c.rot; in.cam_y = c.cam_y; in.hud_row = (int16_t)c.hud_row; in.view_off = (int16_t)c.view_off; in.redraw = (int16_t)redraw;
        radar_pass(&R, &in);
        cases++;
        const char *bad = NULL; long pv_ = 0, rv_ = 0;
        CHK("threat V:4A925", R.threat, get8(0x4A925));
        CHK("prev V:4A926", R.threat_prev, get8(0x4A926));
        CHK("cue V:4AC0C", PLAYER.f.v4ac0c, get32(0x4AC0C));
        CHK("holdoff V:4A93F", (uint8_t)R.holdoff, get8(0x4A93F));
        CHK("mission1 V:4A927", R.mission1, get8(0x4A927));
        CHK("frame V:4A93B", R.frame, get8(0x4A93B));
        for (int i = 1; i < 16 && !bad; i++) {
            const uint32_t b = REC + (uint32_t)i * 0x200u;
            CHK("w0", entities_ac(&E, i)->f.w0, get16(b) & 0xFFFF);
            CHK("b20", entities_ac(&E, i)->f.b20, get8(b + 0x20));
        }
        for (int i = 0; i < R.nlist && !bad; i++) CHK("flag", R.list[i].flag, get16(LISTA + 16 * i + 12) & 0xFFFF);
        CHK("designated", E.designated < 0 ? 0xFFFF : E.designated * 0x200, get16(0x4AA78) & 0xFFFF);
        for (int i = 0; i < RADAR_EMEM && !bad; i++) CHK("erase memory word", R.emem[i], get16(0x537C8 + 2 * (uint32_t)i) & 0xFFFF);
        if (bad) { printf("  [redraw %d n %d, %d walks in the rig (V:4A93B)]\n", redraw, c.n, get8(0x4A93B)); report("scope", k, bad, pv_, rv_); }
    }
}

int main(void) {
    const char *paths[] = {"../../extracted/interceptor.bin", "extracted/interceptor.bin"};
    image_path = NULL;
    if (rig_init(1 << 21) != 0) { printf("rig_init failed\n"); return 1; }
    for (int i = 0; i < 2 && !image_path; i++) if (rig_load_image(paths[i], BASE) == 0) image_path = paths[i];
    if (!image_path) { printf("test_radar_diff: skipped (extracted/interceptor.bin not found)\n"); rig_shutdown(); return 0; }
    const int N = getenv("RADAR_DIFF_N") ? atoi(getenv("RADAR_DIFF_N")) : 1500;
    if (getenv("RADAR_DIFF_SEED")) rng = (uint32_t)atoi(getenv("RADAR_DIFF_SEED")) * 2654435761u + 1;
    verbose = getenv("RADAR_DIFF_VERBOSE") != NULL;
    const char *only = getenv("RADAR_DIFF_PART");
    if (!only || !strcmp(only, "list")) { int d0 = diffs, c0 = cases; part_list(N); printf("list:  %d cases, %d differences\n", cases - c0, diffs - d0); }
    if (!only || !strcmp(only, "walk")) { int d0 = diffs, c0 = cases; part_walk(N, 0); printf("walk:  %d cases, %d differences\n", cases - c0, diffs - d0); }
    if (!only || !strcmp(only, "desig")) { int d0 = diffs, c0 = cases; part_walk(N, 1); printf("desig: %d cases, %d differences\n", cases - c0, diffs - d0); }
    if (!only || !strcmp(only, "pages")) { int d0 = diffs, c0 = cases; part_pages(N / 3); printf("pages: %d cases, %d differences\n", cases - c0, diffs - d0); }
    if (!only || !strcmp(only, "scope")) { int d0 = diffs, c0 = cases; part_scope(N); printf("scope: %d cases, %d differences\n", cases - c0, diffs - d0); }
    if (!only || !strcmp(only, "range")) { int d0 = diffs, c0 = cases; part_range(N); printf("range: %d cases, %d differences\n", cases - c0, diffs - d0); }
    if (!only || !strcmp(only, "lock")) { int d0 = diffs, c0 = cases; part_lock(N); printf("lock:  %d cases, %d differences\n", cases - c0, diffs - d0); }
    if (!only || !strcmp(only, "sight")) { int d0 = diffs, c0 = cases; part_sight(N); printf("sight: %d cases, %d differences\n", cases - c0, diffs - d0); }
    if (!only || !strcmp(only, "slew")) { int d0 = diffs, c0 = cases; part_slew(N); printf("slew:  %d cases, %d differences\n", cases - c0, diffs - d0); }
    if (!only || !strcmp(only, "line")) { int d0 = diffs, c0 = cases; part_line(N); printf("line:  %d cases, %d differences\n", cases - c0, diffs - d0); }
    rig_shutdown();
    printf("test_radar_diff: %d cases, %d differences\n", cases, diffs);
    return diffs ? 1 : 0;
}
