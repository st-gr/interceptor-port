/* tests/rig/test_missions_diff.c - the mission dispatcher (src/core/missions.c, docs/re/08-game-flow.md section 12) against the original's end-of-update routine 0x2852E in the
 * Musashi rig, over scripted record states: the nine modes' scripts and the tail, the end code and the shared end (request 3, its countdown, the delay V:4A978, the key lock), the pause
 * byte as the count, the hand-over and the tracking camera, the shadowing timer and the thieves' destinations, the lock and the delay quirk of F6.
 *
 * Every state is random but biased to the thresholds of the note (0x70 cells, 800 speed, the three distances, 0x10000, the cap, the code values, the pause byte values). The same state
 * is poked into the rig's memory and into the port's records; the routine runs in both; the variables it can change are compared. Environment: MISSIONS_DIFF_N (default 8000),
 * MISSIONS_DIFF_SEED, MISSIONS_DIFF_MODE (restrict to a mode), MISSIONS_DIFF_VERBOSE (print every difference's state). Skipped (exit 0) without extracted/interceptor.bin or the extracted
 * assets. The modes 9 and 0x7D (the qualification and the training: the game's own tests) are not drawn. Failing on any difference. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/missions.h"
#include "../../src/core/entities.h"
#include "../../src/core/flight.h"
#include "../../src/core/assets.h"

#define BASE 0x10000u
#define V_LO 0x4A6E8u
#define V_HI 0x54EC0u
#define REC 0x4B23Cu

static void put8(uint32_t a, int v) { uint8_t b = (uint8_t)v; rig_poke(a, &b, 1); }
static void put16(uint32_t a, int v) { uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 2); }
static void put32(uint32_t a, int32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; rig_poke(a, b, 4); }
static int get8(uint32_t a) { uint8_t b; rig_peek(a, &b, 1); return b; }
static int get16(uint32_t a) { uint8_t b[2]; rig_peek(a, b, 2); return (int16_t)((b[0] << 8) | b[1]); }
static int32_t get32(uint32_t a) { uint8_t b[4]; rig_peek(a, b, 4); return (int32_t)(((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]); }

static const char *assets = "../../assets";
static int fl_read(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    char p[256];
    snprintf(p, sizeof p, "%s/%s", assets, path);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = (uint8_t *)malloc((size_t)n + 1);
    *len = fread(*out, 1, (size_t)n, f);
    fclose(f);
    return 0;
}
static void fl_release(void *user, uint8_t *buf) { (void)user; free(buf); }
static game_loader_t loader = { fl_read, fl_release, NULL };

static uint32_t rng = 0x9E3779B9u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }
static int pick(const int *v, int n) { return v[rnd(0, n - 1)]; }

static ai_t AI;
static int route_hook(void *u, unsigned r, int16_t blocks[][5], int max) {
    (void)u;
    if (r >= AI_RUNS) return 0;
    int n = AI.run_len[r];
    if (n > max) n = max;
    for (int k = 0; k < n; k++) memcpy(blocks[k], AI.run[r][k], 5 * sizeof(int16_t));
    return n;
}

typedef struct {
    unsigned w0, w2;
    int cell_x, fine_x, b4, b20, b21, route, counter, heading, speed_target, speed;
    int32_t pos[3];
    int dest[5];
} rec_t;
typedef struct {
    int mode, progress, cap, kills, location, selector, direction;
    int code, pause, req, req_delay, timer, ident, lock, latch, hand, v8eb, v8ec, v9e2, sim, demo;
    rec_t rec[16];
} state_t;

static const int WORD0[] = { 0x0000, 0x0040, 0x0048, 0x0049, 0x0080, 0x00C0, 0x00C8, 0x8040, 0x8048, 0x40C8, 0x10C0, 0x0440, 0x0840, 0x0240 };
static const int W2V[] = { 0, 0x0080, 0x0080, 0x0001, 0x0100, 0x0180, 0x0020 };
static const int CODES[] = { 0, 0, 0, 0, 0, 0xFF, 1, 0xFE, 0xFD, 0xFC, 4, 0xF0, 0xEF, 2, 0xFF, 1 };
static const int PAUSES[] = { 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 0xFF, 0xFE, 0xFD };
static const int MODES[] = { 3, 4, 5, 6, 7, 8, 0x7F, 3, 4, 5, 6, 7, 8, 1, 2, 0xFF, 10 };
static const int SPEEDS[] = { 0, 0x31F, 0x320, 0x321, -1, -0x300, 0x1000 };
static const int DIST[] = { 0, 0x17FFF, 0x18000, 0x18001, 0x23FFF, 0x24000, 0x24001, 0x2FFFF, 0x30000, 0x30001, 0x7FFFF, 0x10000, 0x10001, -0x10000, -0x10001, 0x100000 };

static void gen(state_t *s, int force_mode) {
    memset(s, 0, sizeof *s);
    s->mode = force_mode ? force_mode : pick(MODES, (int)(sizeof MODES / sizeof MODES[0]));
    s->progress = rnd(0, 4);
    s->cap = pick((const int[]){ 2, 2, 3, 4, 4, 1 }, 6);
    s->kills = pick((const int[]){ 0, 1, 2, 3, 4, 5, 0x84, 0xFF, 2, 4 }, 10);
    s->location = rnd(0, 1) ? 3 : 0;
    s->selector = rnd(0, 1);
    s->direction = rnd(0, 9);
    s->code = pick(CODES, (int)(sizeof CODES / sizeof CODES[0]));
    s->pause = pick(PAUSES, (int)(sizeof PAUSES / sizeof PAUSES[0]));
    s->req = pick((const int[]){ 0, 0, 0, 1, 2, 3, 0xFF }, 7);
    s->req_delay = rnd(-1, 4);
    s->timer = pick((const int[]){ 0, 0, 200, 199, 1, -1, -5, 100 }, 8);
    s->ident = pick((const int[]){ 0, 0, -1, -1, 1, 30, 5 }, 7);
    s->lock = rnd(0, 2) ? -1 : rnd(0, 15);
    s->latch = rnd(0, 2) ? 0 : rnd(1, 3);
    s->hand = rnd(0, 3) ? 0 : (0x80 | s->direction);
    s->v8eb = rnd(0, 255); s->v8ec = rnd(0, 1); s->v9e2 = rnd(0, 28799);
    s->sim = rnd(0, 9) ? 1 : 0;
    s->demo = rnd(0, 19) ? 0 : 1;
    for (int i = 0; i < 16; i++) {
        rec_t *r = &s->rec[i];
        r->w0 = (unsigned)pick(WORD0, (int)(sizeof WORD0 / sizeof WORD0[0]));
        r->w2 = (unsigned)pick(W2V, (int)(sizeof W2V / sizeof W2V[0]));
        r->cell_x = rnd(0, 4) ? rnd(40, 0x72) : 0;
        if (rnd(0, 3) == 0) r->cell_x = pick((const int[]){ 0x6F, 0x70, 0x71, -1, 0 }, 5);
        r->fine_x = rnd(0, 3) ? rnd(1, 0x2000) : 0;
        r->b4 = pick((const int[]){ 0, 0x04, 0x40, 0x80, 0xC0, 0x08, 0x44, 0xC4 }, 8);
        r->b20 = pick((const int[]){ 0, 0, 0x02, 0x80, 0x82, 0x01 }, 6);
        r->b21 = rnd(0, 2) ? 1 : 0;
        r->route = rnd(0, 59);
        r->counter = pick((const int[]){ 0, -20, -1, 1, 4, 5, -5 }, 7);
        r->heading = rnd(0, 28799);
        r->speed_target = pick(SPEEDS, 7);
        r->speed = rnd(0, 2) ? 0 : rnd(-100, 3000);
        for (int k = 0; k < 3; k++) r->pos[k] = rnd(0, 6) == 0 ? rnd(0, 0x1FFFFFFF) : 0x4000000 + (rnd(0, 1) ? 0 : pick(DIST, 16));
        for (int k = 0; k < 5; k++) r->dest[k] = rnd(-30000, 30000);
    }
    /* correlated cases: the player and the thieves/pod near each other more often than chance */
    if (rnd(0, 1)) for (int k = 0; k < 3; k++) { s->rec[4].pos[k] = s->rec[0].pos[k] + pick(DIST, 16); s->rec[6].pos[k] = s->rec[0].pos[k] + pick(DIST, 16); }
    if (rnd(0, 1)) { const int pi = s->latch ? s->latch : 1; for (int k = 0; k < 3; k++) s->rec[pi].pos[k] = s->rec[11].pos[k] + pick(DIST, 16); }
    if (s->mode == 6 && rnd(0, 1)) { s->rec[s->latch ? s->latch : 1].w0 |= 0x8000; }
    /* the triggers of the scripts, built on purpose in about two cases out of three (a random state rarely satisfies them) with the other fields left random */
    if (rnd(0, 2) == 0) return;
    if (rnd(0, 4) != 0) s->code = 0;
    if (rnd(0, 1)) s->sim = 1;
    if (rnd(0, 3)) s->demo = 0;
    if (rnd(0, 5) == 0) {   /* the tail: a landed, stopped aircraft after a take-off, with the code 0xFF or 1 */
        s->code = rnd(0, 1) ? 0xFF : 1; s->rec[0].b21 = 1; s->rec[0].w2 = 0x0080; s->rec[0].speed = 0;
        s->rec[0].b4 = pick((const int[]){ 0x04, 0x40, 0x80, 0xC0, 0x44 }, 5);
    }
    switch (s->mode) {
    case 3: if (rnd(0, 1)) s->ident = -1; break;
    case 4:
        s->rec[4].w0 |= 0x0040; s->rec[4].w2 |= 0x0080; if (rnd(0, 1)) s->rec[4].speed_target = pick((const int[]){ 0, 0x320, 0x300, 0x321 }, 4);
        if (rnd(0, 4)) { s->rec[8].w0 &= ~0x0040u; s->rec[10].w0 &= ~0x0040u; }
        if (rnd(0, 3) == 0) { s->rec[4].w0 &= ~0x0040u; s->rec[4].b20 = rnd(0, 1) ? 0x80 : 0; }
        if (rnd(0, 4)) { s->rec[4].cell_x = rnd(1, 100); }
        break;
    case 5:
        if (rnd(0, 4)) { s->rec[4].w0 |= 0x0048; s->rec[6].w0 |= 0x0048; }
        if (rnd(0, 3) == 0) s->rec[4].w0 &= ~0x0048u;
        if (rnd(0, 1)) s->kills = s->cap + rnd(-1, 1);
        if (rnd(0, 1)) for (int k = 0; k < 3; k++) { s->rec[4].pos[k] = s->rec[0].pos[k] + pick(DIST, 16); s->rec[6].pos[k] = s->rec[0].pos[k] + pick(DIST, 16); }
        break;
    case 6:
        if (rnd(0, 4)) { if (!s->latch) s->latch = rnd(1, 3); s->rec[s->latch].w0 |= 0x8040; s->rec[s->latch].counter = rnd(-3, 1); }
        for (int k = 0; k < 3; k++) s->rec[s->latch ? s->latch : 1].pos[k] = s->rec[11].pos[k] + pick(DIST, 16);
        break;
    case 7:
        if (rnd(0, 4)) { s->rec[4].cell_x = rnd(1, 100); s->rec[4].w0 &= ~0x0080u; }
        break;
    default:
        if (rnd(0, 1)) s->kills = s->cap + rnd(-1, 1);
        if (rnd(0, 1)) s->kills = s->cap;
        break;
    }
}

static void poke_rig(const state_t *s) {
    static uint8_t zero[V_HI - V_LO];
    rig_poke(V_LO, zero, sizeof zero);
    rig_hooks_clear();
    put8(0x4A95E, s->mode); put8(0x4A95F, s->progress); put8(0x4A962, s->cap); put8(0x4A963, s->kills);
    put8(0x4A900, s->location); put8(0x4A83D, s->selector); put8(0x4A96A, s->direction);
    put8(0x4A850, s->code); put8(0x4A866, s->pause); put8(0x4A8E2, s->req); put8(0x4A8E4, s->req_delay);
    put16(0x4A83A, s->timer); put8(0x4A927, s->ident);
    put16(0x4AA78, s->lock < 0 ? -1 : 0x200 * s->lock);
    put16(0x4A97A, 0x200 * s->latch);
    put8(0x4A876, s->hand); put8(0x4A8EB, s->v8eb); put8(0x4A8EC, s->v8ec); put16(0x4A9E2, s->v9e2);
    put16(0x4A984, s->sim ? 0x0840 : 0x0800); put8(0x4A848, s->demo);
    for (int i = 0; i < 16; i++) {
        const rec_t *r = &s->rec[i];
        const uint32_t b = REC + (uint32_t)i * 0x200u;
        put16(b + 0x00, (int)r->w0); put16(b + 0x02, (int)r->w2); put8(b + 0x04, r->b4);
        put16(b + 0x06, r->cell_x); put16(b + 0x0C, r->fine_x);
        put32(b + 0x14, r->pos[0]); put32(b + 0x18, r->pos[1]); put32(b + 0x1C, r->pos[2]);
        put8(b + 0x20, r->b20); put8(b + 0x21, r->b21); put8(b + 0x3A, r->route); put16(b + 0x4C, r->counter);
        put16(b + 0x68, r->heading); put16(b + 0x6C, r->speed_target); put16(b + 0x6E, r->speed);
        put16(b + 0x2C, r->dest[0]); put16(b + 0x2E, r->dest[1]); put16(b + 0x30, r->dest[2]); put16(b + 0x32, r->dest[3]); put32(b + 0x34, r->dest[4]);
    }
}

static entities_t E;
static aircraft_t PLAYER;
static missions_t M;

static void set_port(const state_t *s) {
    entities_init(&E, NULL, NULL, NULL);
    memset(&PLAYER, 0, sizeof PLAYER);
    entities_attach_player(&E, &PLAYER);
    E.hook.route = route_hook;
    for (int i = 0; i < 16; i++) {
        const rec_t *r = &s->rec[i];
        aircraft_t *a = entities_ac(&E, i);
        flight_state_t *f = &a->f;
        f->w0 = (uint16_t)r->w0; f->w2 = (uint16_t)r->w2; f->b4 = (uint8_t)r->b4; f->cell_x = (int16_t)r->cell_x; f->fine_x = (int16_t)r->fine_x;
        a->pos[0] = r->pos[0]; a->pos[1] = r->pos[1]; a->pos[2] = r->pos[2];
        f->b20 = (uint8_t)r->b20; f->b21 = (uint8_t)r->b21; E.rec[i].route = (uint8_t)r->route; f->counter = (int16_t)r->counter;
        f->heading = (uint16_t)r->heading; f->speed_target = (int16_t)r->speed_target; f->speed = (int16_t)r->speed;
        E.rec[i].dest_cx = (int16_t)r->dest[0]; E.rec[i].dest_cz = (int16_t)r->dest[1]; E.rec[i].dest_fx = (int16_t)r->dest[2]; E.rec[i].dest_fz = (int16_t)r->dest[3]; E.rec[i].dest_h = r->dest[4];
    }
    PLAYER.f.v4a850 = (uint8_t)s->code; PLAYER.f.v4a866 = (uint8_t)s->pause;
    E.mode = (uint8_t)s->mode; E.progress = s->progress; E.hostile_cap = (uint8_t)s->cap; E.kills = (uint8_t)s->kills;
    E.sim_on = (uint8_t)s->sim; E.demo = (uint8_t)s->demo; E.designated = (int16_t)s->lock; E.lock_tail = (int16_t)s->lock;
    E.chute_latch = (uint16_t)(0x200 * s->latch);
    *E.req = (uint8_t)s->req; *E.req_delay = (int8_t)s->req_delay;
    missions_start(&M, 3, s->location);
    M.timer = (int16_t)s->timer; M.view.in_ident = (int8_t)s->ident;
    M.view.v4a876 = (uint8_t)s->hand; M.view.v4a8eb = (uint8_t)s->v8eb; M.view.v4a8ec = (uint8_t)s->v8ec; M.view.v4a9e2 = (uint16_t)s->v9e2;
    M.view.v4a96a = (uint8_t)s->direction;
    M.view.in_selector = (uint8_t)s->selector; M.view.in_direction = (uint8_t)s->direction;
    M.view.v4a83d = (uint8_t)s->selector;
}

static int check(const char *what, long port, long rig) {
    if (port == rig) return 0;
    printf("      %s: port %ld (0x%lX) rig %ld (0x%lX)\n", what, port, port, rig, rig);
    return 1;
}

static int compare(const state_t *s) {
    int bad = 0;
    const flight_state_t *p = &PLAYER.f;
    bad |= check("end code V:4A850", p->v4a850, get8(0x4A850));
    bad |= check("pause byte V:4A866", p->v4a866, get8(0x4A866));
    bad |= check("request V:4A8E2", *E.req, get8(0x4A8E2));
    bad |= check("countdown V:4A8E4", (int8_t)*E.req_delay, (int8_t)get8(0x4A8E4));
    bad |= check("delay V:4A978", (uint16_t)M.delay, (uint16_t)get16(0x4A978));
    bad |= check("key lock V:4A965", M.key_lock, get8(0x4A965));
    bad |= check("timer V:4A83A", (int16_t)M.timer, get16(0x4A83A));
    bad |= check("ident V:4A927", M.view.in_ident, (int8_t)get8(0x4A927));
    bad |= check("hand-over V:4A876", M.view.v4a876, get8(0x4A876));
    bad |= check("saved selector V:4A8EC", M.view.v4a8ec, get8(0x4A8EC));
    bad |= check("level byte V:4A8EB", M.view.toggle_request ? M.view.v4a8eb : s->v8eb, get8(0x4A8EB));
    bad |= check("direction V:4A96A", M.view.v4a96a, get8(0x4A96A));
    bad |= check("V:4A86D", M.view.v4a86d, get8(0x4A86D));
    bad |= check("V:4A86C", M.view.v4a86c, get8(0x4A86C));
    bad |= check("V:4A865", M.view.v4a865, get8(0x4A865));
    bad |= check("heading V:4A9E2", (uint16_t)M.view.v4a9e2, (uint16_t)get16(0x4A9E2));
    bad |= check("toggle V:4A97E bit 1", M.view.toggle_request, (get16(0x4A97E) >> 1) & 1);
    bad |= check("viewed record V:4A994", p->v4a994, (uint16_t)get16(0x4A994));
    bad |= check("viewed offset V:4A996", p->v4a996, (uint16_t)get16(0x4A996));
    bad |= check("notice V:4A968", p->v4a968, get8(0x4A968));
    bad |= check("redraw V:4A8EE", M.view.v4a8ee, get8(0x4A8EE));
    if (s->selector) bad |= check("selector V:4A83D", M.view.v4a83d, get8(0x4A83D));
    bad |= check("lock V:4AA78", E.designated < 0 ? -1 : 0x200 * E.designated, get16(0x4AA78));
    bad |= check("request bytes V:4A872..4", E.req_player_launch + E.req_ai_slot4 + E.req_ai, get8(0x4A872) + get8(0x4A873) + get8(0x4A874));
    for (int i = 4; i <= 6; i += 2) {
        const uint32_t b = REC + (uint32_t)i * 0x200u;
        const entity_t *r = &E.rec[i];
        char nm[40];
        snprintf(nm, sizeof nm, "record %d destination cx", i); bad |= check(nm, r->dest_cx, get16(b + 0x2C));
        snprintf(nm, sizeof nm, "record %d destination cz", i); bad |= check(nm, r->dest_cz, get16(b + 0x2E));
        snprintf(nm, sizeof nm, "record %d destination fx", i); bad |= check(nm, r->dest_fx, get16(b + 0x30));
        snprintf(nm, sizeof nm, "record %d destination fz", i); bad |= check(nm, r->dest_fz, get16(b + 0x32));
        snprintf(nm, sizeof nm, "record %d destination h", i); bad |= check(nm, r->dest_h, get32(b + 0x34));
    }
    return bad;
}

int main(void) {
    const char *paths[] = {"../../extracted/interceptor.bin", "extracted/interceptor.bin"};
    const char *image_path = NULL;
    if (rig_init(1 << 21) != 0) { printf("rig_init failed\n"); return 1; }
    for (int i = 0; i < 2 && !image_path; i++) if (rig_load_image(paths[i], BASE) == 0) image_path = paths[i];
    if (!image_path) { printf("test_missions_diff: skipped (extracted/interceptor.bin not found)\n"); rig_shutdown(); return 0; }
    {
        FILE *t = fopen("../../assets/VERSION", "r");
        if (!t) assets = "assets"; else fclose(t);
    }
    char err[200];
    if (assets_load_ai(&loader, &AI, err, sizeof err) != 0) { printf("test_missions_diff: skipped (%s)\n", err); rig_shutdown(); return 0; }
    const int N = getenv("MISSIONS_DIFF_N") ? atoi(getenv("MISSIONS_DIFF_N")) : 8000;
    const int only = getenv("MISSIONS_DIFF_MODE") ? (int)strtol(getenv("MISSIONS_DIFF_MODE"), NULL, 0) : 0;
    if (getenv("MISSIONS_DIFF_SEED")) rng = (uint32_t)atoi(getenv("MISSIONS_DIFF_SEED")) * 2654435761u + 1;
    int diffs = 0, ran = 0, acted = 0, c_ff = 0, c_fe = 0, c_fd = 0, c_fc = 0, c_hand = 0, c_track = 0, c_reroute = 0, c_timer = 0, c_count = 0, c_delay = 0;
    unsigned by_mode[256] = { 0 };
    for (int n = 0; n < N; n++) {
        static state_t st;
        gen(&st, only);
        if (st.mode == 9 || st.mode == 0x7D) continue;
        if (rig_load_image(image_path, BASE) != 0) return 1;
        poke_rig(&st);
        for (int r = RIG_D0; r <= RIG_A6; r++) rig_set_reg((rig_reg_t)r, 0);
        if (rig_run(0x2852Eu) != 0) { printf("config %d: rig_run failed (error %d)\n", n, rig_last_error); diffs++; continue; }
        set_port(&st);
        missions_step(&M, &E);
        ran++;
        by_mode[st.mode & 255]++;
        if (PLAYER.f.v4a850 != st.code || PLAYER.f.v4a866 != st.pause) acted++;
        if (PLAYER.f.v4a850 != st.code) { switch (PLAYER.f.v4a850) { case 0xFF: c_ff++; break; case 0xFE: c_fe++; break; case 0xFD: c_fd++; break; case 0xFC: c_fc++; break; default: break; } }
        if (M.view.v4a876 != st.hand) c_hand++;
        if (M.view.track) c_track++;
        if (E.rec[4].dest_cx != st.rec[4].dest[0]) c_reroute++;
        if (M.timer != st.timer) c_timer++;
        if (PLAYER.f.v4a866 != st.pause && PLAYER.f.v4a866 != 0) c_count++;
        if (M.delay) c_delay++;
        if (compare(&st)) {
            diffs++;
            printf("  config %d: mode %02X progress %d cap %d kills %d loc %d code %02X pause %02X req %d/%d timer %d ident %d lock %d latch %d hand %02X sel %d sim %d demo %d\n", n, st.mode, st.progress, st.cap, st.kills, st.location, st.code, st.pause, st.req, st.req_delay, st.timer, st.ident, st.lock, st.latch, st.hand, st.selector, st.sim, st.demo);
            if (getenv("MISSIONS_DIFF_VERBOSE")) for (int i = 0; i < 16; i++) { const rec_t *r = &st.rec[i]; printf("      rec %2d w0 %04X w2 %04X b4 %02X cell_x %d fine_x %d b20 %02X b21 %d route %d counter %d S %d W %d pos %d %d %d\n", i, r->w0, r->w2, r->b4, r->cell_x, r->fine_x, r->b20, r->b21, r->route, r->counter, r->speed_target, r->speed, r->pos[0], r->pos[1], r->pos[2]); }
        }
    }
    printf("test_missions_diff: %d states, %d changed the code or the pause byte, %d differences; per mode:", ran, acted, diffs);
    for (int m = 0; m < 256; m++) if (by_mode[m]) printf(" %02X:%u", m, by_mode[m]);
    printf("\n  coverage: codes stored 0xFF %d, 0xFE %d, 0xFD %d, 0xFC %d; counts started %d; hand-overs %d; tracking camera %d; thieves re-routed %d; timer changed %d; non-zero delays %d\n", c_ff, c_fe, c_fd, c_fc, c_count, c_hand, c_track, c_reroute, c_timer, c_delay);
    rig_shutdown();
    return diffs != 0;
}
