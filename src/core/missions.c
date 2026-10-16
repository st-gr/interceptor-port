/* src/core/missions.c - the mission scripts (docs/re/08-game-flow.md sections 12 to 14, "note 08"); see missions.h. Every number is a section of that note. No game text lives
 * here: the outcome and briefing texts are ids. */
#include <string.h>
#include "missions.h"

static int valid(const entities_t *e, int i) { return (entities_ac_c(e, i)->f.w0 & 0x0040) != 0; }
static const flight_state_t *fs(const entities_t *e, int i) { return &entities_ac_c(e, i)->f; }

void missions_start(missions_t *m, int mode, int location) {
    /* 12.11: what a script wrote to V:4A96A, V:4A8EB, V:4A8EC and V:4A9E2 lies outside the reset block's ranges and survives the restart; V:4A876 (the flag block), V:4A97E bit 1 and the viewed
     * record (the cleared word range 0x4A978 to 0x4A9DF) do not */
    const missions_view_t keep = m->view;   /* (what the game has not applied yet is not kept: the *_set flags are cleared by the memset) */
    memset(m, 0, sizeof *m);
    m->view.v4a96a = keep.v4a96a; m->view.v4a8eb = keep.v4a8eb; m->view.v4a8ec = keep.v4a8ec; m->view.v4a9e2 = keep.v4a9e2;
    m->mode = (uint8_t)(mode >= 3 && mode <= 8 ? mode : 0);
    m->location = (uint8_t)location;
}

void missions_esc(flight_state_t *pf) { if (pf) pf->v4a866 = 1; }

void missions_resume(missions_t *m, flight_state_t *pf) {
    if (m) m->key_lock = 0;
    if (pf) pf->v4a866 = 0;
}

/* ---- the shared end 0x28ACE (12.2) ---- */

static int request_free(const entities_t *e) { return e->req == NULL || *e->req != 3; }

/* Stores the code and the shared end together (12.2): the delay V:4A978, the request 3, its countdown 4, the key lock. Every call site first tests that the request is not 3. */
static void end_with(missions_t *m, entities_t *e, uint8_t code, uint16_t delay) {
    flight_state_t *pf = &e->player->f;
    pf->v4a850 = code;
    m->delay = delay;
    if (e->req) *e->req = 3;
    if (e->req_delay) *e->req_delay = 4;
    m->key_lock = 1;
    m->ends++;
}

/* ---- the tracking camera 0x1EC14 and the hand-over (12.2) ---- */

/* The count (12.2): the pause byte V:4A866 is the countdown. 0 is loaded with the start value; the byte is decremented once per pass; the pass in which it is 1 or less (signed)
 * does the external-view hand-over (once); the pass in which it reaches 0 acts. `hand_byte` is the value the hand-over stores in V:4A8EB (0xFF F2, 3 F5), or -1 for none
 * (F3, F6). `principal` is the record whose validity the hand-over tests. Returns 1 when the script acts in this pass. */
static int count(missions_t *m, entities_t *e, int start, int hand_byte, int principal) {
    flight_state_t *pf = &e->player->f;
    int8_t b = (int8_t)pf->v4a866;
    m->count_passes++;
    if (b == 0) b = (int8_t)start;
    b = (int8_t)(b - 1);
    pf->v4a866 = (uint8_t)b;
    if (hand_byte >= 0 && b <= 1 && b != 0 && !(m->view.v4a876 & 0x80)) {   /* 1 or less, signed, but not 0: from 1 straight to 0 (ESC) the pass that reads 1 is skipped (12.11) */
        missions_view_t *v = &m->view;
        v->v4a876 = (uint8_t)(v->in_direction | 0x80);
        v->v4a8ec = v->in_selector;
        if (valid(e, principal)) {
            v->v4a96a = 7; v->ring_set = 1;
            v->v4a86d = v->v4a86c = 1;
            v->v4a865 = 0;
            if (v->in_selector == 0) {
                v->v4a9e2 = fs(e, principal)->heading; v->heading_set = 1;
                v->toggle_request = 1;
                v->v4a8eb = (uint8_t)hand_byte;
            }
        }
        v->pending = 1;
    }
    return b == 0;
}

/* The tracking-camera routine 0x1EC14 for slot 4 (12.2): the viewed record becomes 4 (offset 0x800), the notice 0xFE (the designation V:4AA78 is left alone: the routine sets V:4A9F2, the HUD point, to -1); in the cockpit view the heading is
 * copied, in an external view the selector comes from V:4A8EB; V:4A8EE = 3. */
static void track(missions_t *m, entities_t *e, int slot) {
    missions_view_t *v = &m->view;
    entities_switch_view(e, slot);   /* V:4A994 = slot, V:4A996 = 0x200 slot, V:4A968 = 0xFE (the lock stays) */
    v->track = 1;
    if (v->in_selector == 0) { v->v4a9e2 = fs(e, slot)->heading; v->heading_set = 1; }
    else { v->v4a83d = v->v4a8eb; v->sel_set = 1; }
    v->v4a8ee = 3;
    v->pending = 1;
}

/* ---- the scripts ---- */

static void script_f1(missions_t *m, entities_t *e) {   /* 12.5 */
    if (m->view.in_ident < 0 && request_free(e)) end_with(m, e, MS_END_SUCCESS, 0);
}

static void script_f2(missions_t *m, entities_t *e) {   /* 12.6 */
    const flight_state_t *a = fs(e, MS_SLOT_PRINCIPAL);
    if (a->cell_x == 0 && a->fine_x == 0) return;
    if (valid(e, MS_SLOT_PRINCIPAL)) {
        if (!((a->w2 & 0x0080) && a->speed_target <= 0x320 && !valid(e, MS_SLOT_FIGHTER1) && !valid(e, MS_SLOT_FIGHTER2))) return;
    }
    if (!count(m, e, 5, 0xFF, MS_SLOT_PRINCIPAL)) return;
    if (valid(e, MS_SLOT_PRINCIPAL)) {
        track(m, e, MS_SLOT_PRINCIPAL);
        if (request_free(e)) end_with(m, e, MS_END_SUCCESS, 4);
    } else if (request_free(e)) {
        end_with(m, e, (a->b20 & 0x80) ? MS_END_DESTROYED : MS_END_FAIL, 0);
    }
}

static int32_t labs32(int32_t v) { return v < 0 ? -v : v; }

static void script_f3(missions_t *m, entities_t *e) {   /* 12.7 */
    int a = -1;
    if (valid(e, MS_SLOT_PRINCIPAL)) a = MS_SLOT_PRINCIPAL;
    else if (valid(e, MS_SLOT_THIEF2)) a = MS_SLOT_THIEF2;
    int to_check = (a < 0);      /* step 5 */
    if (a >= 0) {
        const flight_state_t *t = fs(e, a);
        if (t->w0 & 0x0008) {   /* still neutral */
            if (t->cell_x >= 0x70) {
                if (request_free(e)) end_with(m, e, MS_END_FAIL, 0);
                return;
            }
            const int32_t d = e->progress < 2 ? 0x30000 : e->progress == 2 ? 0x24000 : 0x18000;
            const aircraft_t *tp = entities_ac_c(e, a);
            const aircraft_t *pl = e->player;
            const int near = labs32(tp->pos[0] - pl->pos[0]) <= d && labs32(tp->pos[1] - pl->pos[1]) <= d && labs32(tp->pos[2] - pl->pos[2]) <= d;
            if (near) {
                if (m->timer >= 0) m->timer--;
                else {   /* both thieves are re-routed to the first waypoint of the route named by +0x3A */
                    for (int k = 0; k < 2; k++) {
                        entity_t *r = &e->rec[k ? MS_SLOT_THIEF2 : MS_SLOT_PRINCIPAL];
                        int16_t blocks[ENT_ROUTE_BLOCKS][5];
                        const int n = e->hook.route ? e->hook.route(e->hook.user, r->route, blocks, ENT_ROUTE_BLOCKS) : 0;
                        if (n > 0) { r->dest_cx = blocks[0][0]; r->dest_cz = blocks[0][1]; r->dest_fx = blocks[0][2]; r->dest_fz = blocks[0][3]; r->dest_h = blocks[0][4]; }
                    }
                }
            } else if (m->timer >= 0) m->timer = 200;
        }
        to_check = m->timer < 0;   /* step 4 */
    }
    if (!to_check) return;
    if ((int8_t)entities_kill_count(e) < (int8_t)e->hostile_cap) return;
    if (!count(m, e, 2, -1, MS_SLOT_PRINCIPAL)) return;
    if (request_free(e)) end_with(m, e, MS_END_SUCCESS, 0);
}

static void script_f4(missions_t *m, entities_t *e) {   /* 12.10 */
    if (e->chute_latch == 0) return;
    const int idx = e->chute_latch / 0x200;
    if (idx < 0 || idx >= ENT_RECORDS) return;
    const aircraft_t *p = entities_ac_c(e, idx);
    if (!(p->f.w0 & 0x8000) || p->f.counter > 0) return;
    const aircraft_t *site = entities_ac_c(e, MS_SLOT_SITE);
    if (!request_free(e)) return;
    const int ok = labs32(p->pos[0] - site->pos[0]) <= 0x10000 && labs32(p->pos[2] - site->pos[2]) <= 0x10000;
    end_with(m, e, ok ? MS_END_SUCCESS : MS_END_FAIL, 0);
}

static void script_f5(missions_t *m, entities_t *e) {   /* 12.8 */
    const flight_state_t *a = fs(e, MS_SLOT_PRINCIPAL);
    if (a->cell_x == 0 && a->fine_x == 0) return;
    if (valid(e, MS_SLOT_PRINCIPAL) && (a->w0 & 0x0080)) return;
    if (!count(m, e, 2, 3, MS_SLOT_PRINCIPAL)) return;
    if (!(a->b20 & 0x02)) {
        track(m, e, MS_SLOT_PRINCIPAL);
        if (request_free(e)) end_with(m, e, MS_END_FAIL, 7);
    } else if (request_free(e)) end_with(m, e, MS_END_SUCCESS, 0);
}

static void script_default(missions_t *m, entities_t *e, int16_t lock_offset) {   /* 12.9: mode 8 and every mode the switch does not name */
    if ((int8_t)entities_kill_count(e) < (int8_t)e->hostile_cap) return;
    if (!count(m, e, 2, -1, MS_SLOT_PRINCIPAL)) return;
    if (!request_free(e)) return;
    /* The quirk of 0x28AC6 (12.2): only the low byte of D0 (the cap) is loaded, the word is stored whole: the delay is (the high byte of the lock offset as the pass began) * 256 + cap; with no
     * lock the offset is -1 and the word is 0xFF04, negative (no delay). */
    end_with(m, e, MS_END_SUCCESS, (uint16_t)(((uint16_t)lock_offset & 0xFF00u) | e->hostile_cap));
}

/* The tail (12.1, 12.2): the landing test for the codes 0xFF and 1. */
static void tail(missions_t *m, entities_t *e) {
    flight_state_t *pf = &e->player->f;
    if (pf->v4a850 != 0xFF && pf->v4a850 != 1) return;
    if (!(pf->b21 & 1)) return;
    if (!(pf->w2 & 0x0080)) return;
    if (m->location == 3) { if (!(pf->b4 & 0xC0)) return; }
    else if (!(pf->b4 & 0x04)) return;
    if (pf->speed != 0) return;
    end_with(m, e, MS_END_COMPLETE, 0);
}

void missions_step(missions_t *m, entities_t *e) {
    if (!m || !e || !e->player) return;
    e->req_player_launch = e->req_ai_slot4 = e->req_ai = 0;   /* 0x2852E: V:4A872, V:4A873, V:4A874 */
    const int16_t lock_offset = e->lock_tail >= 0 ? (int16_t)(0x200 * e->lock_tail) : (int16_t)-1;
    entities_drop_stale_lock(e);
    if (!e->sim_on || e->demo) return;           /* 0x28546, 0x28550: the simulation bit and the demo flag */
    const int8_t mode = (int8_t)e->mode;
    if (mode <= 2) return;
    flight_state_t *pf = &e->player->f;
    if ((uint8_t)mode == 9 || (uint8_t)mode == 0x7D) return;   /* 12.14 N3: the scripts of the qualification and the training return at once, with no tail, when the code is not 0 */
    if (pf->v4a850 != 0) { tail(m, e); return; }
    switch ((uint8_t)mode) {
    case 3: script_f1(m, e); break;
    case 4: script_f2(m, e); break;
    case 5: script_f3(m, e); break;
    case 6: script_f4(m, e); break;
    case 7: script_f5(m, e); break;
    case 9: case 0x7D: break;                    /* the qualification (9.3) and the training (8.9) are the game's */
    default: script_default(m, e, lock_offset); break;
    }
}

/* ---- the briefing chain's tables ---- */

/* The id lists are the extractor's tables/briefing_chain.bin (2.1): `chain` is its 30 bytes, 5 ids for each of the modes 3 to 8 (NULL: no ids, 0). */
unsigned missions_list_id(const uint8_t *chain, int mode, int index) {
    if (!chain || mode < 3 || mode > 8 || index < 0 || index > 4) return 0;
    return chain[(mode - 3) * 5 + index];
}
unsigned missions_briefing_id(const uint8_t *chain, int mode, unsigned completions) { return missions_list_id(chain, mode, completions ? 1 : 0); }

int missions_approach_target(const uint8_t *slots, const uint8_t *cls, const uint8_t *counted, int n) {
    int found = -1;
    for (int i = 0; i < n; i++) {
        if ((cls[i] & 0xF0) != 0x10) continue;
        found = slots[i];
        if (counted[i]) break;
    }
    return found;
}

unsigned missions_date_day(unsigned hours) { const unsigned d = (hours & 31u) + 1u; return d > 30u ? 30u : d; }
unsigned missions_date_month(unsigned hours) { return (hours > 127u ? 127u : hours) >> 5; }
unsigned missions_bearing_degrees(unsigned heading_word) { const unsigned d = heading_word / 80u; return d >= 360u ? 0u : d; }
