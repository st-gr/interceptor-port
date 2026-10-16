/* src/core/weapons.c - the gun, the missiles, the countermeasures, the contact scan, the trail and the jolt (docs/re/07-ai-and-weapons.md sections 7 to 10 and 13). See weapons.h. */
#include <stddef.h>
#include <string.h>
#include "weapons.h"
#include "flight.h"
#include "flight_int.h"
#include "fx.h"
#include "logbook.h"

#define PLAYER 0

static entity_t *rec(weapons_t *w, int i) { return &w->e->rec[i & 15]; }
static aircraft_t *ac(weapons_t *w, int i) { return entities_ac(w->e, i & 15); }
static flight_state_t *pf(weapons_t *w) { return &w->e->player->f; }
static int32_t asr(int32_t v, int n) { return v >> n; }   /* arithmetic right shift (implementation-defined in C99, arithmetic on every target of the port) */

static void count(weapons_t *w, int off, unsigned delta) { if (w->hook.count) w->hook.count(w->hook.user, off, delta); }

/* The sound events of 13.4 go to the sound module (note 10 section 8): the wrappers are sound_event calls. None is made while the map-view byte V:4A865 is set (snd_off) or without a sound module
 * (the tests that build a bare weapons_t). The noise wrapper that finds no noise sound posts the notice 0xFA and V:4A84F = 2 instead (10 3.4). */
static void noise(weapons_t *w, int a, int b) {
    if (w->snd_off || !w->sound) return;
    if (sound_event(w->sound, SND_NOISE, a, b)) { pf(w)->v4a968 = 0xFA; pf(w)->v4a84f = 2; }
}

void weapons_init(weapons_t *w, entities_t *e) {
    memset(w, 0, sizeof *w);
    w->e = e;
    w->gun_sound = 1;
}

/* ---- stores ---------------------------------------------------------------------------------------------------------------------------------- */

void weapons_rearm(weapons_t *w) {
    entity_t *r = rec(w, PLAYER);
    r->stock = 0x24;
    r->rounds = 500;
    w->flares = w->chaff = 16;
    w->cm_req = w->flare_ref = w->chaff_ref = 0;   /* 9.7: V:4A943, V:4A944, V:4A945 (the pool slots stay); the cool-downs are not touched */
    for (int i = 1; i <= 3; i++) {   /* 7.1: records 1 to 3 zeroed whole, the vertex slots +0xA4.. and the part id +0x7B included (they live in the display lists here) */
        memset(&w->e->rec[i], 0, sizeof w->e->rec[i]);
        if (w->hook.clear_draw) w->hook.clear_draw(w->hook.user, i);
    }
}

void weapons_mission_start(weapons_t *w) {
    entities_t *e = w->e;
    memset(w->slot, 0, sizeof w->slot);
    w->trigger = 0; w->cm_req = 0; w->flare_ref = w->chaff_ref = 0;
    w->reload = 0;
    w->ring_count = w->ring_idx = 0; memset(w->ring, 0, sizeof w->ring); memset(w->trail_len, 0, sizeof w->trail_len);
    w->jolt_idx = 0; w->jolt_on = 0; w->jolt_row = 0; w->jolt_started = 0;
    w->n_shots = w->n_hits = w->n_launches = w->n_refused = 0;
    if (e->player) {
        flight_state_t *p = pf(w);
        entity_t *r = rec(w, PLAYER);
        r->stock = 0x24; r->rounds = 500;
        p->v4a96d = 0;          /* the ECM switch: cleared at the mission start 0x1D906 */
        p->l7d = (uint8_t)((p->l7d & 0x0F) | 0x50);
        p->v4a906 = p->v4a907 = 0;
    }
    w->flares = w->chaff = 16;
    e->trail = ENT_TRAINING_LEAD;   /* V:54E72 = 0x800 at every flight start (0x1D266, 13.1): the training lead */
}

/* ---- keys (7.1, 9.1) ------------------------------------------------------------------------------------------------------------------------- */

void weapons_key_fire(weapons_t *w, int down) {
    flight_state_t *p = pf(w);
    if (down) {
        if ((p->gear & 0x0F) != 0) return;            /* nothing unless the gear byte's low nibble is 0 (7.1) */
        const int sel = p->id >> 4;
        if (sel == 1) w->trigger = 1;
        else if (sel == 2 || sel == 3) w->e->req_player_launch = 1;
    } else {
        w->trigger = 0;
    }
}

int weapons_key_select(weapons_t *w) {
    flight_state_t *p = pf(w);
    if ((p->gear & 0x0F) != 0) return 0;
    int sel = p->id >> 4;
    sel = sel == 0 ? 3 : sel - 1;                      /* 3 -> 2 -> 1 -> 0 -> 3 (7.1) */
    p->id = (uint8_t)((sel << 4) | (p->id & 0x0F));
    return 1;   /* the caller clears the fire-prompt state V:4A96C (7.1) */
}

static void post(weapons_t *w, uint16_t code) { if (w->hook.post) w->hook.post(w->hook.user, code); }

void weapons_key_flare(weapons_t *w) {
    flight_state_t *p = pf(w);
    w->flares = (uint8_t)(w->flares - 1);
    if ((int8_t)w->flares > 0) { p->v4a907 = 30; w->cm_req = 2; post(w, 0x4026); }
    else { w->flares = 0; post(w, 0x4027); }
}

void weapons_key_chaff(weapons_t *w) {
    flight_state_t *p = pf(w);
    w->chaff = (uint8_t)(w->chaff - 1);
    if ((int8_t)w->chaff > 0) { p->v4a906 = 30; w->cm_req = 1; post(w, 0x4028); }
    else { w->chaff = 0; post(w, 0x4029); }
}

void weapons_tone_click(weapons_t *w) { if (w->sound) sound_event(w->sound, SND_BEEP_SWITCH, w->ext_view, 0); }   /* 0x37C4A (10 7.4): gated by the view byte V:4A83D (non-zero: nothing) and the cue cool-down, no map flag; the map always has V:4A83D non-zero, so the map is silent through the view byte, as for every key beep */

void weapons_key_ecm(weapons_t *w) {
    flight_state_t *p = pf(w);
    p->v4a96d = (uint8_t)(p->v4a96d ? 0 : 1);
    weapons_tone_click(w);   /* the click 0x37C4A through the tone generator (13.4); the cue cool-down V:4A942 is the cockpit's */
}

/* ---- the pass --------------------------------------------------------------------------------------------------------------------------------- */

void weapons_world_start(weapons_t *w, int paused) {
    if (!w->e->player) return;
    flight_state_t *p = pf(w);
    if (paused || p->v4a866) return;
    if (p->v4a907) p->v4a907--;
    if (p->v4a906) p->v4a906--;
}

/* ---- the tracer pool: the initialiser 0x177D8 and the normaliser 0x29CB0 (7.5) ------------------------------------------------------------------ */

/* The normaliser of note 04a P4 with the scale exponent starting at 3 instead of 8 (result 32 times as long): the same rule as ai_normalise with the final shift 5 smaller. */
static void norm_x32(int32_t x, int32_t y, int32_t z, int len, int32_t out[3]) {
    int32_t cx = w16(x), cy = w16(y), cz = w16(z);
    int32_t ax = cx < 0 ? w16(-cx) : cx, ay = cy < 0 ? w16(-cy) : cy, az = cz < 0 ? w16(-cz) : cz;
    fx_vec_t v = { ax, ay, az };
    const int32_t a = fx_dist(&v);
    if (len == 0 || (a & 0xFFFF) == 0) { out[0] = out[1] = out[2] = 0; return; }
    const uint32_t A = (uint32_t)(a & 0xFFFF);
    uint32_t n = 4, lq;
    int s = 2;
    for (;;) {
        const int s2 = s + 2;
        const uint32_t l2 = s2 < 10 ? (uint32_t)len >> (10 - s2) << (10 - s2) : (uint32_t)len;
        if ((uint64_t)l2 * ((uint64_t)n << 2) / A > 1024) break;
        n <<= 2; s = s2;
    }
    lq = s < 10 ? (uint32_t)len >> (10 - s) << (10 - s) : (uint32_t)len;
    uint32_t q = (lq * n) / A;
    if (q > 0xFFFFu) q &= 0xFFFFu;
    const int32_t qq = (int32_t)q;
    const int sh = s - 5;
    const int64_t m[3] = { (int64_t)cx * qq, (int64_t)cy * qq, (int64_t)cz * qq };
    for (int k = 0; k < 3; k++) out[k] = (int32_t)(sh >= 0 ? (m[k] >> sh) : (m[k] << -sh));
}

/* a vector (a, b, c) = (lateral, up, forward) mapped to the world by the rows of the attitude matrix at +0x92 and shifted right by `shift` (7.5) */
static void rows(const int16_t *m, int a, int b, int c, int shift, int32_t out[3]) {
    for (int k = 0; k < 3; k++) {
        int32_t s = (int32_t)m[3 * k + 0] * a;
        s = fl_add(s, (int32_t)m[3 * k + 1] * b);
        s = fl_add(s, (int32_t)m[3 * k + 2] * c);
        out[k] = asr(s, shift);
    }
}

static void tracer_init(weapons_t *w, int k, uint16_t kind, uint32_t n) {
    wp_tracer_t *s = &w->slot[k];
    aircraft_t *pa = w->e->player;
    flight_state_t *p = &pa->f;
    int a = 0, b = 0, c = 0, b2, c2, life, len;
    static const int wob_dir[4] = { 3, -3, 5, 0 }, wob_len[4] = { 0, 16, 0, -16 };
    if (kind == WPF_FLARE) { b = -8; b2 = -64; c2 = 0; life = 30; len = 13; }
    else if (kind == WPF_CHAFF) { b = -8; c = -56; b2 = -64; c2 = 0; life = 60; len = 5; }
    else {
        if (p->cls == 0x11) c = 56; else { a = 6; b = 1; c = 24; }
        b2 = 64 + wob_dir[n & 3]; c2 = 1024; life = 20;
        len = 0xC6 + asr(p->speed_target, 6) + wob_len[n & 3];
    }
    int32_t o[3], d[3];
    rows(p->mat_b, a, b, c, 6, o);
    s->cell_x = p->cell_x; s->cell_z = p->cell_z;
    s->pos[0] = fl_add(pa->pos[0] & 0x3FFFFF, o[0]);
    s->pos[1] = fl_add(pa->pos[1], o[1]);
    s->pos[2] = fl_add(pa->pos[2] & 0x3FFFFF, o[2]);
    rows(p->mat_b, 0, b2, c2, 4, d);
    int32_t v[3], r[3];
    for (int i = 0; i < 3; i++) v[i] = w16(asr(fl_sub(d[i], o[i]), 8));
    norm_x32(v[0], v[1], v[2], len, r);
    for (int i = 0; i < 3; i++) { s->step[i] = fl_shl(r[i], 3); s->sub[i] = r[i]; }
    s->latch = 0;
    if (kind) for (int i = 0; i < 3; i++) { s->step[i] = fl_add(s->step[i], p->vel[i]); s->sub[i] = fl_add(s->sub[i], asr(p->vel[i], 3)); }
    s->life = (int16_t)life;
}

/* ---- the gun's hit test 0x2ADDE and the hull lists (8.2, 8.2a) ----------------------------------------------------------------------------------- */

static int16_t vdiff(int16_t a, int16_t b) { return w16((int32_t)a - (int32_t)b); }

int weapons_hull_inside(const ai_t *tab, const flight_state_t *rf, int part_id, const int16_t vtx[17][3], const int32_t fine[3], const int32_t point[3]) {
    if (!tab) return 0;
    if ((int8_t)part_id < 0) return 0;   /* the far level and the cockpit block: no hit, nothing read */
    const int list = part_id & 15;
    if (list >= AI_HULL_LISTS) return 0;
    int sh = (rf->l7d & 15) - ((part_id >> 4) & 15);
    if (sh < 0) sh = 0;
    for (int face = 0; face < AI_HULL_TRIPLES; face++) {
        const int16_t *t = tab->hull[list][face];
        if ((uint16_t)t[0] & 0x8000u) return 1;   /* the terminator: all faces passed */
        const int form_b = (t[0] & 0x4000) != 0;
        const int ia = (((uint16_t)t[0]) & 0x3FFF) / 6, ib = ((uint16_t)t[1]) / 6, ic = ((uint16_t)t[2]) / 6;
        if (ia > 16 || ib > 16 || ic > 16) return 0;
        int16_t nrm[3];
        if (!form_b) {
            for (int k = 0; k < 3; k++) nrm[k] = vdiff(vtx[ic][k], vtx[ib][k]);
        } else {
            const int16_t u[3] = { vdiff(vtx[ib][0], vtx[ia][0]), vdiff(vtx[ib][1], vtx[ia][1]), vdiff(vtx[ib][2], vtx[ia][2]) };
            const int16_t v[3] = { vdiff(vtx[ic][0], vtx[ia][0]), vdiff(vtx[ic][1], vtx[ia][1]), vdiff(vtx[ic][2], vtx[ia][2]) };
            const int32_t cr[3] = { (int32_t)u[1] * v[2] - (int32_t)u[2] * v[1], (int32_t)u[2] * v[0] - (int32_t)u[0] * v[2], (int32_t)u[0] * v[1] - (int32_t)u[1] * v[0] };
            for (int k = 0; k < 3; k++) nrm[k] = w16((int32_t)((uint32_t)cr[k] >> 4));
        }
        int32_t dot = 0;
        for (int k = 0; k < 3; k++) {
            const int32_t q = fl_add(asr(vtx[ia][k], sh), fine[k]);
            const int16_t dk = w16(point[k] - q);
            dot = fl_add(dot, (int32_t)nrm[k] * dk);
        }
        if (dot < 0) return 0;
    }
    return 1;
}

static int big_hull(weapons_t *w, int idx) {
    const flight_state_t *f = &ac(w, idx)->f;
    if (w->hook.model_flags) {
        const int fl = w->hook.model_flags(w->hook.user, w->e->rec[idx].template_id);
        if (fl >= 0) return (fl & 0x10) != 0;
    }
    return (f->cls & 0xF0) == 0x20;
}

/* the effect of a hit on a record whose model is not a big hull (8.2, with the coordinator's correction: the hit nibble +0x3C is stored only while the new count is below 3, so
 * every later hit repeats the kill marks) */
static void gun_hit_effect(weapons_t *w, int idx) {
    entity_t *r = rec(w, idx);
    flight_state_t *f = &ac(w, idx)->f;
    count(w, LB_CANNON_HITS, 1);
    w->n_hits++;
    const int nib = (r->hits & 0x0F) + 1;
    if (nib < 3) { r->hits = (uint8_t)((r->hits & 0xF0) | nib); return; }
    f->w0 |= EW0_CRASHED;
    if (f->cls != 0x15 && (f->cls & 0xF0) != 0) {   /* RIG (test_weapons_diff.c): the out-of-control mark, the followed record, the skid bit and the state are all behind the class 0x15 exemption */
        f->b20 |= 0x02;
        w->e->trail = (uint8_t)idx;
        f->w2 |= 0x1000;
        f->state = 1;
    }
}

/* The landmark volume of the ordinal k against the tracer (8.2 "Landmarks"; V: weapon_check.py landmark, 12,600 cases, 0 differences). Returns 1 for a hit (the slot keeps the position of the
 * sub-step), 0 for no hit (the walk goes on) and -1 when the last chain element's eight sub-steps are all outside: the call ends with no hit and without the motion step, the tracer keeping the
 * position of the eight sub-steps. The slot of the environment is the volume record of the ordinal (a slot beyond the table has a negative pointer: skipped). */
static int landmark_test(weapons_t *w, wp_tracer_t *s, int k, int *left_valid, int16_t *left) {
    (void)w;
    *left_valid = 0;
    const flight_env_t *fe = flight_env();
    if (!fe) return 0;
    const flight_lm_slot_t *sl = NULL;
    /* 07 8.2 "An ordinal outside 0 to 10", port rule (C): the exact aliases k = j - 16384 and k = j - 32768 (j = 0 to 10; the second only from j = 1) read the real pointer and the real record of entry j
     * (4 k and 256 k equal 4 j and 256 j modulo 65536), so the volume is tested in full as entry j. Every other out-of-range ordinal skips the entry like a negative table pointer (the counter goes on).
     * UNOBSERVABLE (07 8.2 (b) and (c), milestone 7: tools/m68k-rig/tests/lists_model/landmark_state_check.py and test_weapons_diff.c part lmstate, on the whole machine state of a free flight): the other
     * ordinals read a pointer and a record from memory the port does not hold (the zero-filled variables of the state hunks, the RAM past the last hunk, which the allocator left non-zero). With a tracer
     * placed inside the junk record's box the original differs from a skip for most of them (a hit, a tracer moved by the eight sub-steps, a call that does not return); the result is a function of the
     * loader's layout of that run, not of the game. The ordinals the original treats exactly like a skip, the real ordinals 0 to 10 and the aliases are compared with the port with 0 differences. */
    int e = k;
    if (k < 0 || k > 10) {
        if (k + 16384 >= 0 && k + 16384 <= 10) e = k + 16384;
        else if (k + 32768 >= 1 && k + 32768 <= 10) e = k + 32768;
        else return 0;
    }
    for (int j = 0; j < fe->n_landmarks && j < FLIGHT_LM_SLOTS; j++) if (fe->landmark[j].ordinal == e) { sl = &fe->landmark[j]; break; }
    if (!sl) return 0;                                           /* a negative table pointer: the ordinal is used, the entry skipped */
    const int dx = w16((int32_t)s->cell_x - sl->cell_x), dz = w16((int32_t)s->cell_z - sl->cell_z);
    if (iabs32(dx) > 1 || iabs32(dz) > 1) return 0;
    const int offx = dx * 16384, offz = dz * 16384 + fe->landmark_z;   /* the z offset also carries the high word of the table pointer divided by 4 (the artefact of the player's volumes); the PLANE test of every element adds both again (8.2 step 4; review M1) */
    int boxx = offx, boxz = offz;   /* the offsets of the BOX compares: dropped after the back-up of the whole step (8.2 step 4: "box coordinates that no longer carry the cell offsets") */
    for (int n = 0; ; n++) {
        const int32_t Y = asr(s->pos[1], 8);
        const int32_t X = asr(s->pos[0], 8) + boxx, Z = asr(s->pos[2], 8) + boxz;   /* long compares against the sign-extended box words (0x2ADDE adds and compares longs; review S9) */
        const int in_top = Y <= 0x7FFF && (int16_t)Y <= sl->box[0] && X > sl->box[1] && X < sl->box[2] && Z > sl->box[3] && Z < sl->box[4];
        if (n == 0 && !in_top) return 0;                          /* the top box fails: no hit in this volume */
        if (n >= sl->nnodes || n >= FLIGHT_LM_NODES) return 0;    /* the chain ends (a negative link) */
        const flight_lm_node_t *nd = &sl->node[n];
        const int in_el = Y <= 0x7FFF && (int16_t)Y <= nd->box[0] && X > nd->box[1] && X < nd->box[2] && Z > nd->box[3] && Z < nd->box[4];
        if (in_el) {
            for (int j = 0; j < 8; j++) {   /* the plane test: the tracer advances by its sub-step vector, the point is inside when every dot product is negative */
                for (int a = 0; a < 3; a++) s->pos[a] = fl_add(s->pos[a], s->sub[a]);
                const int16_t P[3] = { w16(asr(s->pos[0], 8) + offx), w16(asr(s->pos[1], 8)), w16(asr(s->pos[2], 8) + offz) };
                /* the first dot product is formed before the count is looked at: a count of 0 (or less) tests one plane (review S10, the loop of 0x2ADDE). The plane of a node with a count of 0 is the one
                 * that follows the count in the model data: the table carries it since extractor version 26 (the footprint of record 34 is the one shipped node of this kind; V: test_weapons_diff.c part lmnode,
                 * 400 cases through the real table, 0 differences; with zeros for the plane 365 of 400 differ). */
                int inside = 1;
                const int np = nd->nplanes < 1 ? 1 : nd->nplanes;
                for (int p = 0; p < np && p < FLIGHT_LM_PLANES && inside; p++) {
                    int32_t dot = 0;
                    for (int c = 0; c < 3; c++) dot = fl_add(dot, (int32_t)w16(P[c] - nd->ref[p][c]) * nd->nrm[p][c]);
                    if (dot >= 0) inside = 0;
                }
                if (inside) return 1;
            }
            if (n + 1 >= sl->nnodes || n + 1 >= FLIGHT_LM_NODES) return -1;   /* the link is not positive: the call ends */
            for (int a = 0; a < 3; a++) s->pos[a] = fl_sub(s->pos[a], s->step[a]);   /* the whole step undone, the next element from the start */
            /* 07 8.2 (1), as 0x2B376 reads it: the undo reloads the step vector into the three registers, so the ordinal register (the plane test's last z difference until then) holds the step's
             * y component. Every way on from a plane-test miss goes through this undo (a link that is not positive ends the call above), so the value the walk goes on with is always the low word of
             * step y: the next ordinal is that word plus one. [Refines the note's "the last z difference": the rig shows the z difference replaced by the undo, the rig compares it.] */
            *left_valid = 1; *left = (int16_t)(uint16_t)((uint32_t)s->step[1] & 0xFFFFu);
            boxx = boxz = 0;   /* its BOX coordinates no longer carry the cell offsets (the original reloads x, y and z without them); the plane test keeps both offsets (8.2 step 4, review M1) */
        } else if (n + 1 >= sl->nnodes) return 0;                 /* an element that fails its box passes to the next by the link; a negative link ends the volume with no hit */
    }
}

static int hit_test(weapons_t *w, int k) {
    wp_tracer_t *s = &w->slot[k];
    entities_t *e = w->e;
    if (w->hold || w->paused) return 0;   /* 8.2: V:4A875 or the pause (the pause screen and a script's count) */
    wp_qentry_t q[96];
    int nq = w->hook.queue_c ? w->hook.queue_c(w->hook.user, q, 96) : 0;
    int ents = 0, hit = 0;
    /* The ordinal register D5 of 0x2ADDE (07 8.2 "Landmark and entity entries in one queue"): its low word is the landmark ordinal counter, start -1, and the entity sub-step counter shares it. The
     * walk ENDS when the low word is 10 or more (a signed word compare), when the sixth entity entry has been examined or when the entries are used up. */
    int reg = -1 + w->dbg_reg0;
    w->dbg_inbox = 0; w->dbg_lm_oob_n = 0;
    for (int i = nq - 1; i >= 0 && !hit; i--) {
        if (reg >= 10 || ents >= 6) break;
        const uint8_t fl = q[i].flags;
        if (fl & 0x40) continue;                                      /* a secondary object: skipped, no ordinal (8.2) */
        if (!(fl & 0x10)) {                                           /* a landmark volume */
            const int k = ++reg;                                      /* the ordinal is the register's low word plus one: 0 after the start and after every entity entry whose box was entered (below) */
            if ((k < 0 || k > 10) && w->dbg_lm_oob_n < 16) w->dbg_lm_oob[w->dbg_lm_oob_n++] = k;
            int lv = 0;
            int16_t lz = 0;
            const int r = landmark_test(w, s, k, &lv, &lz);
            if (r == 1) hit = 2;                                      /* stops the tracer, changes no record and no counter */
            else if (r < 0) return 0;                                 /* a miss at the last chain element: the call ends here, without gravity, drag or the first-pass latch */
            else if (lv) reg = lz;                                    /* 07 8.2 (1): a chain whose plane test missed and whose step was undone leaves the step's y word in the register: the next ordinal is that word plus one (arbitrary) */
            continue;
        }
        ents++;
        const int idx = (int8_t)q[i].id;
        if (idx < 0 || idx >= ENT_RECORDS) continue;
        if (idx == s->owner) continue;
        flight_state_t *rf = &ac(w, idx)->f;
        if (rf->cell_x != s->cell_x || rf->cell_z != s->cell_z) continue;
        if (!(rf->w0 & EW0_VALID)) continue;
        const int big = big_hull(w, idx);
        const int half = big ? WP_BIG_HULL_BOX : WP_TRACER_BOX;
        const int16_t dx = vdiff(rf->fine_x, (int16_t)(s->pos[0] >> 8)), dz = vdiff(rf->fine_z, (int16_t)(s->pos[2] >> 8));
        const int32_t dy = fl_sub(rf->alt8, asr(s->pos[1], 8));
        if (!(iabs32(dx) < half && iabs32(dz) < half && iabs32(dy) < half)) continue;
        w->dbg_inbox++;
        int16_t vtx[17][3];
        int have = 0;
        int part = rf->b7b;   /* +0x7B and the slots as the draw left them: the PREVIOUS pass's when the gun runs before the entity pass (map view, a chaff slot, an external view), this pass's when it runs after it (7.3, 12.5) */
        int32_t fine[3] = { rf->fine_x, rf->alt8, rf->fine_z };
        if (!big) have = w->hook.slots && w->hook.slots(w->hook.user, idx, vtx, 17) == 0;
        for (int j = 0; j < 8 && !hit; j++) {
            for (int a = 0; a < 3; a++) s->pos[a] = fl_add(s->pos[a], s->sub[a]);
            const int32_t T[3] = { (int32_t)((uint32_t)s->pos[0] >> 8), (int32_t)((uint32_t)s->pos[1] >> 8), (int32_t)((uint32_t)s->pos[2] >> 8) };
            int inside;
            if (big) {   /* 0x2B3F0 (8.2, "Models whose flag byte has bit 4"): the ship volumes against the tracer point; false for a missile in the air */
                flight_ship_t sh;
                const flight_env_t *fe = flight_env();
                const int32_t P[3] = { w16(T[0]), T[1], w16(T[2]) };   /* x and z the words, y the long (0x2B3F0 compares it as a long) */
                inside = fe && fe->tab && w->hook.ship && w->hook.ship(w->hook.user, idx, &sh) == 0 && fl_bighull_hit(fe, &sh, P);
            } else inside = have && weapons_hull_inside(e->tab, rf, part, vtx, fine, T);
            if (inside) hit = 1 + (big ? 2 : 0);
        }
        if (!hit) { for (int a = 0; a < 3; a++) s->pos[a] = fl_sub(s->pos[a], s->step[a]); reg = -1; continue; }   /* 07 8.2 (1): the eight sub-steps leave the shared register's low word at -1, so the landmark ordinals restart at 0 */
        if (!big) gun_hit_effect(w, idx);
        hit = 1;
    }
    if (hit) return 1;
    /* no entity or landmark hit: the motion (7.4) */
    s->step[1] = fl_sub(s->step[1], 0x100);
    if (s->flags & WPF_FLARE) { s->step[0] -= asr(s->step[0], 6); s->step[2] -= asr(s->step[2], 6); }
    if (s->flags & WPF_CHAFF) {
        s->step[1] = fl_add(s->step[1], 0x100);   /* no gravity */
        for (int a = 0; a < 3; a++) s->step[a] -= asr(s->step[a], 2);
    }
    if (s->latch == 0) s->latch = 1;
    else for (int a = 0; a < 3; a++) s->pos[a] = fl_add(s->pos[a], s->step[a]);
    return 0;
}

/* ---- the slot step 0x1754C and the gun routine 0x172DC ------------------------------------------------------------------------------------------- */

static void slot_step(weapons_t *w, int k) {
    wp_tracer_t *s = &w->slot[k];
    if (s->life <= 0) {   /* RIG (tests/rig/test_weapons_diff.c): the slot is deactivated here, after the creation test of the same call saw it active (7.2) */
        s->flags &= (uint16_t)~WPF_ACTIVE;
        if (w->flare_ref == k + 1) w->flare_ref = 0;
        if (w->chaff_ref == k + 1) w->chaff_ref = 0;
        return;
    }
    if (s->flags & WPF_STOPPED) return;
    const int h = hit_test(w, k);
    if (h) {
        s->flags = (uint16_t)((s->flags & ~WPF_HIT_CLEAR) | 0x12);
        s->life = 2;
        return;
    }
    if (s->pos[1] < 0) {
        s->flags = (uint16_t)((s->flags & ~0x18) | WPF_STOPPED);
        s->life = 2;
        const int16_t dy = (int16_t)asr(s->step[1], 8);
        if (dy != 0) {
            const int16_t hgt = (int16_t)(-(asr(s->pos[1], 8)));
            for (int a = 0; a < 3; a += 2) {
                const int32_t num = fl_shl(s->step[a], 4);
                int32_t q = num / dy;
                if (q > 32767 || q < -32768) q = (int16_t)num;   /* divs.w overflow leaves the dividend: its low word */
                s->pos[a] = fl_add(s->pos[a], asr((int32_t)(int16_t)q * hgt, 4));
            }
            s->pos[1] = 0;
        }
        return;
    }
    if (s->pos[0] < 0) { s->cell_x = (int16_t)(s->cell_x == 0 ? 127 : s->cell_x - 1); s->pos[0] = fl_add(s->pos[0], 0x400000); }
    else if (s->pos[0] >= 0x400000) { s->cell_x = (int16_t)(s->cell_x == 127 ? 0 : s->cell_x + 1); s->pos[0] = fl_sub(s->pos[0], 0x400000); }
    if (s->pos[2] < 0) { s->cell_z = (int16_t)(s->cell_z == 0 ? 127 : s->cell_z - 1); s->pos[2] = fl_add(s->pos[2], 0x400000); }
    else if (s->pos[2] >= 0x400000) { s->cell_z = (int16_t)(s->cell_z == 127 ? 0 : s->cell_z + 1); s->pos[2] = fl_sub(s->pos[2], 0x400000); }
}

/* The draw scan 0x2AD18 (07 7.3, 12.5; V: tools/m68k-rig/tests/lists_model/gunorder_check.py on the whole-pool captures combat7-gunorder*, test_weapons_diff.c part gunorder). */
/* The three inputs of the distance routine 0x20884 are the LOW WORDS of the registers (the routine compares, extends and divides words). The per-axis absolute difference shifted right by 8 is a word below 0x8000
 * for any two points less than two cells apart; beyond that the routine's word compares go negative and its table lookup reads the image at a wrapped offset (the table is at 0x208E8, the offset a signed word),
 * which the port does not hold. NOTE-GAP (07 12.5): such a distance (two cells or more, only a stale camera in a cockpit view with a chaff slot, or a tracer that far from its owner) is taken as the
 * unsigned word; the rig test does not draw those cases. */
static int32_t scan_abs8(int32_t d) { return asr(d < 0 ? (int32_t)(0u - (uint32_t)d) : d, 8) & 0xFFFF; }
static int scan_dist(int32_t dx, int32_t dy, int32_t dz) {
    fx_vec_t v;
    v.x = scan_abs8(dx); v.y = scan_abs8(dy); v.z = scan_abs8(dz);
    return (int16_t)fx_dist(&v);   /* the routine returns a word; the compare is a signed word compare */
}

int weapons_scan(const weapons_t *w, int ext_view, const int32_t cam[3]) {
    for (int k = WP_SLOTS - 1; k >= 0; k--) {
        const wp_tracer_t *s = &w->slot[k];
        if (!(s->flags & WPF_ACTIVE)) continue;
        if (!(s->flags & WPF_CHAFF) && !ext_view) return 0;
        const int own = s->owner < ENT_RECORDS ? s->owner : 0;
        const int32_t *op = entities_ac(w->e, own)->pos;
        const int owner_d = scan_dist(fl_sub(op[0], cam[0]), fl_sub(op[1], cam[1]), fl_sub(op[2], cam[2]));
        const int32_t sx = fl_add(s->pos[0], (int32_t)((uint32_t)(int32_t)s->cell_x << 22)), sz = fl_add(s->pos[2], (int32_t)((uint32_t)(int32_t)s->cell_z << 22));
        const int slot_d = scan_dist(fl_sub(sx, cam[0]), fl_sub(s->pos[1], cam[1]), fl_sub(sz, cam[2]));
        return owner_d <= slot_d;
    }
    return 0;
}

void weapons_gun(weapons_t *w, uint32_t n, int paused) {
    entities_t *e = w->e;
    if (!e->player) return;
    flight_state_t *p = pf(w);
    paused = paused || p->v4a866 != 0;   /* one flag: the pause screen or the pause byte (a script's count) */
    w->paused = (uint8_t)paused;
    entity_t *r0 = rec(w, PLAYER);
    int shot = 0, running = 0;
    w->reload = (int8_t)(w->reload - 1);
    const int req_trigger = w->trigger && !paused;
    for (int k = 0; k < WP_SLOTS; k += 2) {
        wp_tracer_t *s = &w->slot[k];
        if (!paused && !w->hold) {
            s->life = w16(s->life - 1);
        }
        const int request = req_trigger || w->cm_req;
        if (request && !(s->flags & WPF_ACTIVE)) {
            if (!shot && (p->l7d >> 4) != 0 && (int16_t)r0->rounds >= 1) {
                const int cm = w->cm_req;
                shot = running = 1;
                p->l7d = (uint8_t)(p->l7d - 0x10);
                count(w, LB_CANNON_FIRED, 3);
                p->v4a87d = 1;
                s->flags = 0x401;
                s->owner = 0;
                w->n_shots++;
                if (cm) {
                    noise(w, 0x1C, 0x30);
                    if (cm == 1) { s->flags |= WPF_CHAFF; w->chaff_ref = (uint8_t)(k + 1); }
                    else { s->flags |= WPF_FLARE; w->flare_ref = (uint8_t)(k + 1); }
                    w->cm_req = 0;
                } else {
                    if (w->sound ? (w->sound->en13 & 0x20) != 0 : w->gun_sound) { if (!w->snd_off && w->sound) sound_event(w->sound, SND_GUN, 6, w->ext_view ? 0x1E : 0x16); } else noise(w, 0x1C, 0x30);   /* 0x1A2F0(6, 0x16 or 0x1E) (10 8.2) */
                    const int rn = (int16_t)r0->rounds - 3;
                    r0->rounds = (uint16_t)(rn < 0 ? 0 : rn);
                    w->reload = 20;
                }
                tracer_init(w, k, (uint16_t)(s->flags & (WPF_FLARE | WPF_CHAFF)), n);
            } else if (w->reload > 0) running = 1;
        }
        if (s->flags & WPF_ACTIVE) slot_step(w, k);
    }
    if (!shot && !running) { w->reload = 0; p->l7d = (uint8_t)((p->l7d & 0x0F) | 0x50); }
}

/* ---- the missiles: the destination updates (8.3, 9.4) ------------------------------------------------------------------------------------------- */

static void dest_from_record(entity_t *r, const flight_state_t *tf) {
    r->dest_cx = tf->cell_x; r->dest_cz = tf->cell_z; r->dest_fx = tf->fine_x; r->dest_fz = tf->fine_z; r->dest_h = tf->alt8;
}

/* 0x2826A: the AI's missile (W0 bit 3 clear): the destination is the target record or, for a missile aimed at the player with a decoy of its kind in the air, the decoy (9.4) */
static void ai_destination(weapons_t *w, int idx) {
    flight_state_t *f = &ac(w, idx)->f;
    entity_t *r = rec(w, idx);
    f->w0 |= EW0_ENGAGED;
    const int t = r->target & 0x7F;
    if (t >= ENT_RECORDS) return;   /* NOTE-GAP: the original would read a record beyond the 16; no shipped state reaches it (the byte 0xFF names record 127) */
    if (t == 0) {
        const uint8_t ref = (f->cls == 0) ? w->flare_ref : w->chaff_ref;   /* class 0 looks at the flare, every other class at the chaff */
        if (ref) {
            const wp_tracer_t *s = &w->slot[(ref - 1) % WP_SLOTS];
            r->dest_cx = s->cell_x; r->dest_cz = s->cell_z;
            r->dest_fx = w16(s->pos[0] >> 8); r->dest_fz = w16(s->pos[2] >> 8);
            r->dest_h = asr(s->pos[1], 8);
            const aircraft_t *a = ac(w, idx);
            const int32_t sx = fl_add(fl_shl(s->cell_x, 22), s->pos[0]), sz = fl_add(fl_shl(s->cell_z, 22), s->pos[2]);
            if (iabs32(fl_sub(a->pos[0], sx)) <= 0x4800 && iabs32(fl_sub(a->pos[1], s->pos[1])) <= 0x4800 && iabs32(fl_sub(a->pos[2], sz)) <= 0x4800) f->w2 |= EW2_PROX;
            else if (f->w2 & EW2_PROX) f->b20 |= 0x20;
            return;
        }
    }
    dest_from_record(r, &ac(w, t)->f);
}

static void speed_toward(flight_state_t *f, int target) {
    int s = f->speed_target;
    if (s > target) { s -= 0x240; if (s < target) s = target; }
    else if (s < target) { s += 0x240; if (s > target) s = target; }
    f->speed_target = (int16_t)s;
    f->speed = (int16_t)s;
}

/* 0x27E7E: the player's missile reads the player's designation every pass (8.3) */
static void live_retarget(weapons_t *w, int idx) {
    entities_t *e = w->e;
    flight_state_t *f = &ac(w, idx)->f;
    entity_t *r = rec(w, idx);
    const int d = e->designated;
    if (d < 0 || d == idx || d >= ENT_RECORDS) {
        speed_toward(f, 0x4200);
        r->dest_h = 0;
        return;
    }
    dest_from_record(r, &ac(w, d)->f);
    r->target = (uint8_t)(0x80 | d);
}

/* ---- the gate 0x27DC2 (8.1) -------------------------------------------------------------------------------------------------------------------- */

int weapons_gate(void *user, int idx) {
    weapons_t *w = (weapons_t *)user;
    entities_t *e = w->e;
    flight_state_t *f = &ac(w, idx)->f;
    if (e->paused) return 0;
    if (!(f->w0 & EW0_VALID)) return 0;
    f->jam = (uint8_t)(w->e->player && w->e->player->f.v4a96d && (rec(w, idx)->target & 0x80) && (rec(w, idx)->target & 0x7F) == 0 && f->cls != 1);   /* 9.5: the class is NOT 1 (the AMRAAM is not affected) */
    if (!w->freeze) f->counter = w16(f->counter - 1);
    if (f->counter <= 0) {   /* expiry: the whole W0 word becomes 0, the display request 0x0C, the flight step is skipped; nothing is counted */
        f->w0 = 0;
        e->request = 0x0C;
        return 0;
    }
    if ((f->w0 & EW0_DYING) || f->counter < 0x32) {
        if (f->counter < 0x0C) f->w0 |= EW0_DYING;
        rec(w, idx)->dest_h = 0;
        speed_toward(f, 0x3000);
        return 1;
    }
    /* RIG (tests/rig/test_weapons_diff.c): the launch pass boosts like every other pass of the boost phase; its step moves nothing (W0 bit 0x0100, the fresh record rule of the flight step) */
    {
        int s = w16(f->speed_target + 0x1E0);
        if (s > 0x4200) s = 0x4200;
        f->speed_target = (int16_t)s; f->speed = (int16_t)s;
    }
    if (!(f->w0 & EW0_SPAWN)) ai_destination(w, idx);
    else {
        f->w0 &= (uint16_t)~EW0_ENGAGED;
        if (!(f->w0 & EW0_NOT_COUNTED)) ai_destination(w, idx);
        else live_retarget(w, idx);
    }
    return 1;
}

/* ---- the launch (7.6) -------------------------------------------------------------------------------------------------------------------------- */

static int do_launch(weapons_t *w, int idx, int own, int player) {
    entities_t *e = w->e;
    if (!e->player) return 0;
    aircraft_t *oa = ac(w, own), *na = ac(w, idx);
    flight_state_t *of = &oa->f, *nf = &na->f, *p = pf(w);
    entity_t *orc = rec(w, own), *nrc = rec(w, idx);
    if (player) {
        e->req_player_launch = 0;
        e->launch_flag = 1;
        if (p->v4ac08 & 0x4000) { p->v4ac08 &= ~0x4000; p->v4ac0c |= 8; }
    } else {
        int mask = e->progress <= 1 ? 7 : e->progress == 2 ? 5 : 3;
        if (p->v4a96d) mask >>= 1;
        if ((e->pass & (unsigned)mask) != 0) return 0;
    }
    const int long_range = (of->id >> 4) == 3;
    unsigned stock = long_range ? (orc->stock & 0x0F) : (orc->stock >> 4);
    if (stock == 0) { w->n_refused++; return 0; }
    if ((unsigned)own * 0x200u == p->v4a996) { p->v4a968 = 0xFB; p->v4a84f = 8; }   /* the pass routine's case 0xFB plays the launch (10 5.4) */
    /* the first 0xA4 bytes of the owner are copied, the state byte cleared */
    memcpy(nf, of, offsetof(flight_state_t, vtx));
    na->pos[0] = oa->pos[0]; na->pos[1] = oa->pos[1]; na->pos[2] = oa->pos[2];
    nrc->dest_cx = orc->dest_cx; nrc->dest_cz = orc->dest_cz; nrc->dest_fx = orc->dest_fx; nrc->dest_fz = orc->dest_fz; nrc->dest_h = orc->dest_h;
    nrc->target = orc->target; nrc->route = orc->route; nrc->hits = orc->hits; nrc->range = orc->range; nrc->tag = orc->tag;
    nrc->stock = orc->stock; nrc->rounds = orc->rounds; nrc->ai_flags = orc->ai_flags; nrc->engage = orc->engage;
    nrc->b70 = orc->b70; nrc->b71 = orc->b71;   /* inside the first 0xA4 bytes: the owner's stored map bytes */
    nf->state = 0;
    nf->cls = long_range ? 1 : 0;
    nrc->template_id = long_range ? 1 : 0;
    for (int k = 0; k < TEMPLATE_LONGS; k++) e->desc[idx][k] = (e->tpl && (int)nrc->template_id < e->tpl->count) ? e->tpl->rec[nrc->template_id][k] : 0;
    if (player) {
        count(w, long_range ? LB_AIM120_FIRED : LB_AIM9_FIRED, 1);
        p->v4a87d = 1;
    }
    nf->rate_p = nf->rate_h = nf->rate_r = 0;
    nrc->ai_flags = 0;
    nf->w2 &= (uint16_t)~0x0004;
    if (long_range) orc->stock = (uint8_t)(orc->stock - 1); else orc->stock = (uint8_t)(orc->stock - 0x10);
    const int h = long_range ? (int)((orc->stock & 0x0F) + 2) : (int)(orc->stock >> 4);
    {   /* the hard point: the table of the owner's class (the F-16 has its own), rotated by the owner's matrix, added to the position */
        int32_t o[3] = { 0, 0, 0 };
        if (e->tab && h >= 0 && h < 6) {
            const int16_t *t = e->tab->offs[of->cls == 0x10 ? 2 + h : 8 + h];
            rows(of->mat_b, t[0], t[1], t[2], 6, o);
        }
        for (int k = 0; k < 3; k++) na->pos[k] = fl_add(na->pos[k], o[k]);
    }
    nf->counter = long_range ? 200 : 150;
    nf->ground = 20;
    nf->w0 |= 0x10C0 | EW0_KEEP_MOVING | EW0_SPAWN;
    nf->sink = 0;
    if (of->w2 & EW2_GROUND) { nf->vel[1] = 0; nf->w2 &= (uint16_t)~EW2_GROUND; }
    nrc->dest_cx = -1;
    e->request = 0x8C;
    nrc->target = orc->target;
    w->n_launches++;
    flight_sync(na);
    return 1;
}

int weapons_launch_player(void *w, int idx) { return do_launch((weapons_t *)w, idx, PLAYER, 1); }
int weapons_launch_ai(void *w, int idx) { return do_launch((weapons_t *)w, idx, idx - 1, 0); }

/* ---- the contact scan 0x2B5E2 and its tail (10.2, 10.3) -------------------------------------------------------------------------------------------- */

int weapons_contact(void *user, flight_state_t *f, const int32_t prev[3], int phase) {
    weapons_t *w = (weapons_t *)user;
    entities_t *e = w->e;
    int m = -1;
    for (int i = 0; i < ENT_RECORDS && m < 0; i++) if (&ac(w, i)->f == f) m = i;
    if (m < 0 || (f->cls & 0xF0) == 0x20) return 0;
    const int first = phase == 0 ? m + 1 : 15, last = phase == 0 ? 13 : 15;
    for (int o = first; o <= last; o++) {
        aircraft_t *oa = ac(w, o);
        flight_state_t *of = &oa->f;
        if (!(of->w0 & EW0_VALID) || (of->w0 & (EW0_CRASHED | EW0_DYING))) continue;
        if (of->group == f->group) continue;
        if (!((f->w0 | of->w0) & EW0_NOT_COUNTED)) continue;
        if ((of->cls & 0xF0) == 0x30) continue;
        int outer, inner;
        if (of->cls == 0x15) { outer = 0x5000; inner = 0x1000; }
        else if ((of->cls & 0xF0) == 0x20) { outer = 0xA0000; inner = -1; }   /* the ship row: below */
        else if ((f->cls & 0xF0) == 0 || (f->cls & 0xF0) != (of->cls & 0xF0)) { outer = 0x10000; inner = 0x5000; }
        else if ((f->w2 | of->w2) & EW2_SCRIPTED) { outer = 0x6000; inner = 0x0800; }
        else { outer = 0x7000; inner = 0x1000; }
        int32_t d[3];
        for (int k = 0; k < 3; k++) d[k] = iabs32(fl_sub(oa->pos[k], prev[k]));
        if (d[0] > outer || d[1] > outer || d[2] > outer) continue;
        f->v4a957 = 1;
        of->w2 |= EW2_PROX;
        int hit = d[0] <= inner && d[1] <= inner && d[2] <= inner;
        if (!hit) {
            int32_t d2[3];
            for (int k = 0; k < 3; k++) d2[k] = iabs32(fl_sub(fl_sub(oa->pos[k], asr(of->vel[k], 1)), prev[k]));
            hit = d2[0] <= inner && d2[1] <= inner && d2[2] <= inner;
        }
        if (inner < 0) {   /* the ship row (10.2): the proximity above, then the deck and hull tests of note 05 17.11 for this ship record; a hit returns 0x20 and none of the marks of 10.3 */
            flight_ship_t sh;
            const flight_env_t *env = flight_env();
            if (env && w->hook.ship && w->hook.ship(w->hook.user, o, &sh) == 0) {
                const int r = fl_ship_contact(f, env, &sh, prev);
                if (r) return r;
            }
            continue;
        }
        if (!hit) continue;
        /* 10.3: the marks on the other record, the counters, the friendly-fire mark */
        if (of->w0 & EW0_ACTIVE) {
            of->w0 |= EW0_CRASHED;
            if ((of->cls & 0xF0) != 0) { of->b20 |= 0x02; e->trail = (uint8_t)o; of->w2 |= 0x1000; of->state = 1; }
        }
        if ((f->w0 & EW0_NOT_COUNTED) && f->group == 0 && !(of->w0 & EW0_NOT_COUNTED)) {
            if (f->cls == 0) count(w, LB_AIM9_HITS, 1);
            else if (f->cls == 1) count(w, LB_AIM120_HITS, 1);
        }
        if ((f->w0 & EW0_NOT_COUNTED) && (of->w0 & EW0_NOT_COUNTED)) { f->b20 |= 0x80; of->b20 |= 0x80; }
        else {
            f->b20 &= (uint8_t)~0x80; of->b20 &= (uint8_t)~0x80;
            if (m == PLAYER && (of->cls & 0xF0) == 0) count(w, LB_MISSILE_HITS, 1);
        }
        return 0x40;
    }
    return 0;
}

/* ---- the trail ring 0x2AC5A (13.1) ---------------------------------------------------------------------------------------------------------- */

void weapons_trail_step(weapons_t *w, int idx) {
    entities_t *e = w->e;
    if (e->trail != idx || e->paused) return;
    aircraft_t *a = ac(w, idx);
    flight_state_t *f = &a->f;
    for (int k = 0; k < 3; k++) {
        w->ring[w->ring_idx][k] = a->pos[k];
        if (w->ring_count < 5) w->ring[(w->ring_idx + 1) % 6][k] = a->pos[k];
    }
    w->ring_idx = (uint8_t)(w->ring_idx == 5 ? 0 : w->ring_idx + 1);
    if (w->ring_count < 6) w->ring_count++;
    uint8_t *len = &w->trail_len[idx];
    if (f->speed != 0 && (f->w2 & 0x1000)) {
        const int l = (int)w->ring_count - 1;
        *len = (uint8_t)(l > 4 ? 4 : l);
    } else {
        if (*len > 0) (*len)--;
        if (*len == 0) { w->ring_count = 0; w->ring_idx = 0; }
    }
}

void weapons_follow(void *user, const flight_state_t *f) {
    weapons_t *w = (weapons_t *)user;
    for (int i = 0; i < ENT_RECORDS; i++) if (&ac(w, i)->f == f) { w->e->trail = (uint8_t)i; return; }
}

void weapons_trail_clear(weapons_t *w) { w->ring_count = 0; w->ring_idx = 0; }

/* ---- the jolt 0x29A5E (13.3) ------------------------------------------------------------------------------------------------------------------- */

int weapons_jolt_pass(weapons_t *w, flight_state_t *p, int external) {
    static const int8_t tab[4][4] = { { -1, 1, 0, 0 }, { 1, -1, 0, 0 }, { -1, 1, 0, 0 }, { 1, -1, 0, 0 } };
    const int kind = p->v4a878;
    int started = 0;
    w->jolt_called = 0;
    if (kind < 1 || kind > 4) return 0;
    if (external) { p->v4a878 = 0; return 0; }
    w->jolt_called = 1;   /* every call that is not the cancelled one asks for a redraw of the 14 request bytes (all set to 3, 0x1EFF4): the ending call included */
    if (w->jolt_idx >= 4) {   /* the jolt ends */
        p->v4a878 = 0; w->jolt_idx = 0; w->jolt_on = 0;
        p->v4a984 &= (uint16_t)~0x0400;
        w->jolt_row = 0;
        return 0;
    }
    if (!w->jolt_on) { w->jolt_idx = 0; w->jolt_on = 1; started = kind; p->v4a984 |= 0x0400; w->jolt_started = (uint8_t)kind; }
    w->jolt_row = tab[kind - 1][w->jolt_idx];
    w->jolt_idx++;
    return started;
}
