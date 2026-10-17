/* src/core/entities.c - the entity records, the spawner, the world update's record order, the dying path and the ejection (docs/re/07-ai-and-weapons.md Part A). See entities.h. */
#include <stddef.h>
#include <string.h>
#include "entities.h"
#include "ai.h"
#include "flight.h"
#include "fx.h"

#define SEAT_COUNTER_START (-20)

static int16_t w16(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v & 0xFFFFu); }
static int32_t add32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static int32_t abs32(int32_t v) { return v < 0 ? (int32_t)(0u - (uint32_t)v) : v; }

/* ---- setup ----------------------------------------------------------------------------------------------------------------------------------- */

void entities_reset(entities_t *e) {
    memset(e->rec, 0, sizeof e->rec);
    memset(e->obj, 0, sizeof e->obj);
    memset(e->desc, 0, sizeof e->desc);
    e->pass = 0; e->progress = 0; e->hostile_cap = 0; e->hostile_count = 0; e->kills = 0; e->designated = -1; e->lock_tail = -1; e->launch_flag = 0;
    e->req_player_launch = e->req_ai_slot4 = e->req_ai = 0;
    e->region_bits = 0; e->d7 = 0; e->sub_first = e->sub_count = 0; e->request = 0; e->notice = 0; e->debug_word = 0;
    e->chute_latch = 0; *e->req = 0; *e->req_delay = 0;
    e->trail = 0xFF; e->view_cell_x = e->view_cell_z = 0; e->camera_update = 1; e->camera_mode = 0;
    e->pl_dx = e->pl_dz = 0;
    e->n_spawned = e->n_skipped_valid = e->n_refused_cap = e->n_dropped_requests = e->n_objects_refused = 0;
    for (int i = 0; i < ENT_RECORDS; i++) e->rec[i].range = 0x7FFF;
}

void entities_init(entities_t *e, const templates_t *tpl, const ai_t *tab, const scenario_t *scn) {
    memset(e, 0, sizeof *e);
    e->req = &e->end_request; e->req_delay = &e->end_delay;
    e->tpl = tpl; e->tab = tab; e->scn = scn;
    entities_reset(e);
}

void entities_attach_player(entities_t *e, aircraft_t *player) { e->player = player; }
void entities_attach_external(entities_t *e, int idx, aircraft_t *ac) { if (idx >= 0 && idx < ENT_RECORDS) e->ext[idx] = ac; }

aircraft_t *entities_ac(entities_t *e, int idx) {
    idx &= 15;
    if (idx == 0 && e->player) return e->player;
    if (e->ext[idx]) return e->ext[idx];
    return &e->rec[idx].ac;
}
const aircraft_t *entities_ac_c(const entities_t *e, int idx) { return entities_ac((entities_t *)e, idx); }

int entities_viewed(const entities_t *e) {
    if (!e->player) return 0;
    return e->player->f.v4a996 ? (e->player->f.v4a994 & 15) : 0;
}

static int valid(entities_t *e, int i) { return (entities_ac(e, i)->f.w0 & EW0_VALID) != 0; }

/* ---- the secondary objects (1.5) ------------------------------------------------------------------------------------------------------------- */

int entities_object_create(entities_t *e, const secondary_t *from, uint16_t model) {
    for (int k = 0; k < ENT_OBJECTS; k++) {
        if (e->obj[k].w0 & EW0_VALID) continue;
        e->obj[k] = *from;
        e->obj[k].w0 = EW0_VALID;
        e->obj[k].life = 0x7F;
        e->obj[k].model = model ? model : (uint16_t)(((15 - k) & 1) ? 0x13 : 0x12);   /* 0x13 when 15 minus the slot is odd (note 07 1.5) */
        e->request = 0xFF;
        return k;
    }
    e->n_objects_refused++;   /* Review Focus 2: all 16 valid: nothing is created, nothing is reused, no request */
    return -1;
}

/* ---- freeing and killing (5) ----------------------------------------------------------------------------------------------------------------- */

/* ---- the spawner (2) ------------------------------------------------------------------------------------------------------------------------- */

static void set_identity(flight_state_t *f) {
    for (int k = 0; k < 9; k++) f->mat_a[k] = f->mat_b[k] = (k % 4 == 0) ? 16384 : 0;
}

static void block_of(entities_t *e, unsigned entry, int16_t w[5]) {
    memset(w, 0, 5 * sizeof(int16_t));
    if (e->hook.position_block) {
        int16_t b[5];
        if (e->hook.position_block(e->hook.user, entry, b) == 0) memcpy(w, b, sizeof b);
    }
}

int entities_spawn(entities_t *e, const uint16_t w[5], int d7, int dx, int dz) {
    const unsigned mf = (w[4] >> 8) & 15u;
    const unsigned slot = w[2] & 0x7Fu;
    if (mf && mf != e->mode) return 0;                                    /* 1: the mode filter */
    if (slot >= ENT_RECORDS) return 0;                                    /* Review Focus 2: the slot field is 7 bits, the arrays hold 16: nothing is written past them */
    if (valid(e, (int)slot)) { e->n_skipped_valid++; return 0; }          /* 2: a valid record is never reused or replaced */
    const unsigned tid = w[0] / 20u;                                      /* 3: the template copy, before the refusals */
    for (int k = 0; k < TEMPLATE_LONGS; k++) e->desc[slot][k] = (e->tpl && (int)tid < e->tpl->count) ? e->tpl->rec[tid][k] : 0;
    if ((int8_t)e->hostile_cap <= (int8_t)e->hostile_count) { e->n_refused_cap++; return 0; }   /* 4: the cap, for every class */
    entity_t *r = &e->rec[slot];
    aircraft_t *a = entities_ac(e, (int)slot);
    flight_state_t *f = &a->f;
    if (r->ac.f.cls == 0x15 && r->ac.f.cell_x != 0 && a == &r->ac) return 0;   /* 5: a placed cruise missile keeps its slot */
    /* 6: the first 0xA4 bytes of the record are cleared */
    memset(f, 0, offsetof(flight_state_t, vtx));
    memset(a->pos, 0, sizeof a->pos);
    r->dest_cx = r->dest_cz = r->dest_fx = r->dest_fz = 0; r->dest_h = 0;
    r->target = 0; r->route = 0; r->hits = 0; r->range = 0; r->tag = 0; r->stock = 0; r->rounds = 0; r->ai_flags = 0; r->engage = 0;
    /* 7 */
    r->template_id = (uint16_t)tid;
    f->l7d = (uint8_t)((e->hook.unit_shift ? e->hook.unit_shift(e->hook.user, tid) : 3) & 0x0F);
    f->cls = (uint8_t)(w[1] & 0xFF);
    f->group = (uint8_t)slot;
    r->route = (uint8_t)((w[2] >> 8) & 0x7F);
    /* 8 */
    int16_t b[5];
    block_of(e, w[3] / 2u, b);
    /* 9 */
    if (w[4] & 0x8000u) f->w0 |= EW0_NOT_COUNTED;
    else { f->w0 &= (uint16_t)~EW0_NOT_COUNTED; if ((f->cls & 0xF0) == 0x10) e->hostile_count++; }
    /* 10 */
    if (w[4] & 0x4000u) f->state = 8;
    if (w[4] & 0x2000u) f->w0 |= EW0_ENGAGED;
    /* 11: the random offset */
    int16_t cx = b[0], cz = b[1];
    if (w[4] & 0x1000u) {
        int ox = dx, oz = dz;
        if (w[4] & 0x0080u) { ox >>= 1; oz >>= 1; }
        cx = w16(cx + ox); cz = w16(cz + oz);
    }
    /* 12 */
    r->tag = (uint8_t)(d7 + 1);
    /* 13 */
    r->engage = 3;
    if ((f->cls & 0xF0) != 0x20) f->w0 |= 0x1080;
    f->w0 |= 0x0140;
    f->w0 &= (uint16_t)~EW0_HIT;
    /* 14: the destination or the leader */
    if (w[2] & 0x8000u) {
        const unsigned n = (w[2] >> 8) & 0x7Fu;
        if (n > 0) {
            int16_t db[5];
            block_of(e, n, db);
            r->dest_cx = db[0]; r->dest_cz = db[1]; r->dest_fx = db[2]; r->dest_fz = db[3]; r->dest_h = db[4];
        }
        r->target = 0xFF;
    } else {
        r->dest_cx = cx; r->dest_cz = cz; r->dest_fx = b[2]; r->dest_fz = b[3]; r->dest_h = b[4];
        r->target = (uint8_t)(0x80 + ((w[2] >> 8) & 0x7Fu));
    }
    /* 15 */
    f->cell_x = cx; f->cell_z = cz; f->fine_x = b[2]; f->fine_z = b[3]; f->alt8 = b[4];
    /* 16 */
    if (b[4] != 0) { f->gear = 0xE0; f->speed_target = 0x2000; f->speed = 0x2000; }
    /* 17 */
    r->stock = 0x44; r->rounds = 500; f->fuel = 0x61A800; f->id = 0x3D; r->ai_flags = 0x10; r->b71 = 0xFF;
    /* 18 */
    a->pos[0] = (int32_t)(((uint32_t)(int32_t)cx << 22) + ((uint32_t)(int32_t)b[2] << 8));
    a->pos[2] = (int32_t)(((uint32_t)(int32_t)cz << 22) + ((uint32_t)(int32_t)b[3] << 8));
    a->pos[1] = (int32_t)((uint32_t)(int32_t)b[4] << 8);
    set_identity(f);
    f->sink = 0;
    /* +0x0A stays 0 until the record's first pass computes it (rig: test_spawn_diff) */
    flight_sync(a);
    e->n_spawned++;
    return 1;
}

void entities_aim(entities_t *e, int idx) {
    entity_t *r = &e->rec[idx & 15];
    aircraft_t *a = entities_ac(e, idx);
    flight_state_t *f = &a->f;
    int have = 0;
    if (r->target != 0xFF) {
        const int lead = r->target & 0x7F;
        if (valid(e, lead)) {
            const aircraft_t *la = entities_ac(e, lead);
            r->dest_cx = la->f.cell_x; r->dest_cz = la->f.cell_z; r->dest_fx = la->f.fine_x; r->dest_fz = la->f.fine_z; r->dest_h = la->f.alt8;
            have = 1;
        }
    } else if (r->route > 0) have = 1;
    if (!have) return;
    const int32_t tx = add32((int32_t)((uint32_t)(int32_t)r->dest_cx << 22), (int32_t)((uint32_t)(int32_t)r->dest_fx << 8));
    const int32_t tz = add32((int32_t)((uint32_t)(int32_t)r->dest_cz << 22), (int32_t)((uint32_t)(int32_t)r->dest_fz << 8));
    const int32_t dxv = (int32_t)((uint32_t)tx - (uint32_t)a->pos[0]), dzv = (int32_t)((uint32_t)tz - (uint32_t)a->pos[2]);
    const int32_t hd = e->hook.aim_heading ? e->hook.aim_heading(e->hook.user, dxv, dzv) : 0;
    f->heading = (uint16_t)hd; f->pitch = 0; f->roll = 0;
    flight_set_attitude(a, 0, (uint16_t)hd, 0);
}

int entities_start_mission(entities_t *e, int first, int count, int cap, int dx, int dz) {
    e->hostile_count = 0; e->kills = 0; e->hostile_cap = (uint8_t)cap;
    e->sub_first = first; e->sub_count = count;
    e->pl_dx = dx; e->pl_dz = dz;
    int placed = 0;
    if (!e->scn) return 0;
    for (int k = 0; k < count; k++) {
        const scenario_entry_t *en = &e->scn->sub_entry[first + k];
        if (k > 0 && (int)(en->w[4] & 0x7Fu) > e->progress) continue;   /* the first entry without the threshold test */
        if (entities_spawn(e, en->w, -2, dx, dz)) {
            entities_aim(e, en->w[2] & 0x7F);
            placed++;
        }
    }
    return placed;
}

void entities_scene_pass(entities_t *e, int first, int count) {
    if (!e->scn) return;
    /* The scene passes of the mission start (0x123C2, 0x127C8, 0x12452, gate 1) run the world update before the flight: an aircraft record's fresh pass (no motion, W0 bit 0x0100 cleared, the
     * cell bookkeeping) and the gate's range routine happen there, with no state routine and no speed step. The first flying pass therefore moves the record (demo record 10: S 0x1F72 and
     * the displacement of pass 0 in the original; before this the port moved from its second pass, a constant offset of 29 and 122 fine units from pass 1 on) and finds the schedule counter
     * +0x39 at 0x01 (range 0x7FFF, +0x63 = 0xFD already set; the nibble is 0x11 after pass 0, not recomputed: the phase of every later recompute, which decides the evasion at pass 551). */
    for (int k = 0; k < count; k++) {
        const scenario_entry_t *en = &e->scn->sub_entry[first + k];
        const int slot = en->w[2] & 0x7F;
        if (slot < 1 || slot >= ENT_RECORDS || !valid(e, slot) || (entities_ac(e, slot)->f.cls & 0xF0) != 0x10) continue;
        flight_state_t *f = &entities_ac(e, slot)->f;
        if (!(f->w0 & EW0_KEEP_MOVING) || !(f->w0 & EW0_ACTIVE)) continue;
        f->w0 &= (uint16_t)~EW0_KEEP_MOVING;
        /* the gate's range routine: range word, range class, the schedule nibble of +0x39 cleared by the first computation (the old range is 0) */
        if (!(f->b20 & 0x02) && !(f->w2 & EW2_SCRIPTED) && e->rec[slot].dest_cx >= 0) ai_range(e, slot);
        flight_step_entity(entities_ac(e, slot), 0, 1);
    }
}

/* ---- the region triggers (2.5) --------------------------------------------------------------------------------------------------------------- */

static void region_place(entities_t *e, const scenario_region_t *rg) {
    if (e->mode == 1 || e->mode == 2 || e->mode == 0x7D) return;   /* 0x2D22A returns before its first entry in these modes and leaves d7 as the loop had it (note 07 2.5) */
    int n = rg->count_word;
    if (n > rg->count) n = rg->count;
    for (int k = 0; k < n; k++) {
        const scenario_entry_t *en = &e->scn->region_entry[rg->first + k];
        const int placed = entities_spawn(e, en->w, e->d7, e->pl_dx, e->pl_dz);
        e->d7 = placed ? 0 : -1;
    }
    if (n == 0) e->d7 = -1;
}

static void region_exit(entities_t *e, const scenario_region_t *rg) {
    int n_entries = rg->count_word;   /* the exit walks the count word's entries, like the placer (rig: test_spawn_diff, the regions 0 and 1 have entries and a count word of 0: nothing is re-addressed) */
    if (n_entries > rg->count) n_entries = rg->count;
    for (int k = 0; k < n_entries; k++) {
        const scenario_entry_t *en = &e->scn->region_entry[rg->first + k];
        const unsigned slot = en->w[2] & 0x7Fu;
        if (slot >= ENT_RECORDS) continue;
        entity_t *r = &e->rec[slot];
        flight_state_t *f = &entities_ac(e, (int)slot)->f;
        if ((r->target & 0x80) && (r->target & 0x7F) != 0) continue;   /* it already follows someone: a leader index, or 0xFF (a block destination) */   /* it already follows someone (a block destination 0xFF too: its low seven bits are non-zero) */
        const unsigned n = (en->w[2] >> 8) & 0x7Fu;
        int16_t b[5];
        int own = n == 0;   /* bits 8 to 14 = 0 (bit 15 clear too: the player as leader): the entry's own block, the target stays the player (note 07 2.5) */
        if (!own && !(en->w[2] & 0x8000u)) {
            const int lead = (int)n;
            if (valid(e, lead)) {
                const flight_state_t *lf = &entities_ac(e, lead)->f;   /* the leader's POSITION words, not its destination (rig: test_spawn_diff, the carrier leader of the record 12) */
                r->dest_cx = lf->cell_x; r->dest_cz = lf->cell_z; r->dest_fx = lf->fine_x; r->dest_fz = lf->fine_z; r->dest_h = lf->alt8;
                r->target = (uint8_t)(0x80 + lead); r->engage = 3;
            } else own = 1;   /* a leader that is not valid: the first case */
        } else if (!own) {
            block_of(e, n, b);
            r->dest_cx = b[0]; r->dest_cz = b[1]; r->dest_fx = b[2]; r->dest_fz = b[3]; r->dest_h = b[4];
            r->target = 0xFF; r->engage = 3;
            f->w0 &= (uint16_t)~EW0_ENGAGED;
        }
        if (own) {
            block_of(e, en->w[3] / 2u, b);
            /* RIG FINDING, not yet in note 07: the own-block case stores the entry's leader number shifted left by 8 (a register leftover) as the destination's fine z, whatever the block
             * says (rig: test_spawn_diff SPAWN_DIFF_OWNBLOCK, 14 leaders: dest_fz = leader << 8); the other four words are the block's */
            r->dest_cx = b[0]; r->dest_cz = b[1]; r->dest_fx = b[2]; r->dest_fz = (int16_t)(n << 8); r->dest_h = b[4];
            if (r->engage == 3 || r->engage == 4) r->engage = 5;
            r->target = 0x80;
            f->w0 &= (uint16_t)~EW0_ENGAGED;
        }
    }
}

void entities_region_tick(entities_t *e) {
    if ((e->pass & 15) != 3 || e->demo || !e->scn) return;   /* the tick also runs in the modes 1, 2 and 0x7D: only the placer places nothing there */
    const aircraft_t *p = entities_ac(e, 0);
    const int px = p->f.cell_x, pz = p->f.cell_z;
    e->d7 = 0;
    for (int ri = 0; ri < e->scn->region_count && ri < ENT_MAX_REGIONS; ri++) {
        const scenario_region_t *rg = &e->scn->region[ri];
        const int inside = px >= (int16_t)rg->box[0] && px <= (int16_t)rg->box[1] && pz >= (int16_t)rg->box[2] && pz <= (int16_t)rg->box[3];
        const unsigned mask = 1u << (e->d7 & 7);
        if (!(e->region_bits & mask) && inside) { e->region_bits |= (uint8_t)mask; region_place(e, rg); }
        else if ((e->region_bits & mask) && inside) region_place(e, rg);
        else if ((e->region_bits & mask) && !inside) { region_exit(e, rg); e->region_bits &= (uint8_t)~mask; }
        e->d7 += 1;
    }
}

/* ---- the ejection (6.4) ---------------------------------------------------------------------------------------------------------------------- */

void entities_switch_view(entities_t *e, int idx) {   /* 0x1EC14: the viewed record, the notice 0xFE, the view requests of note 05 17.10. V:4A9F2 (the HUD's previous target point x) is set to -1 there; the DESIGNATION V:4AA78 is
                                                        * untouched (rig: tests/rig/test_missions_diff.c, the tracking camera of the F2 script with a lock held): the stale-lock drop alone clears it */
    flight_state_t *pf = &e->player->f;
    pf->v4a994 = (uint16_t)idx; pf->v4a996 = (uint16_t)(0x200 * idx);
    if (!pf->v4a83d) e->redraw_req = 1;   /* the cockpit view only: the head set-up and the request table (note 05 17.10, note 07 12.1 writer 4) */
    pf->v4a968 = 0xFE; pf->v4a8f6 = 3; pf->v4a8fd = 3;
}

static void spawn_seat(entities_t *e, int slot, int parachute) {
    aircraft_t *p = e->player;
    flight_state_t *pf = &p->f;
    entity_t *r = &e->rec[slot];
    aircraft_t *a = entities_ac(e, slot);
    flight_state_t *f = &a->f;
    memcpy(f, pf, offsetof(flight_state_t, vtx));
    r->b70 = e->rec[0].b70; r->b71 = e->rec[0].b71;   /* the first 0xA4 bytes of the player's record: +0x70 and +0x71 come along */
    memcpy(a->pos, p->pos, sizeof a->pos);
    f->w0 &= (uint16_t)~EW0_HIT;
    f->cls = parachute ? 0x31 : 0x30;   /* the parachute is class 0x31 (note 07 1.3) */
    const unsigned tid = parachute ? 11u : 2u;
    r->template_id = (uint16_t)tid;
    for (int k = 0; k < TEMPLATE_LONGS; k++) e->desc[slot][k] = (e->tpl && (int)tid < e->tpl->count) ? e->tpl->rec[tid][k] : 0;
    f->rate_p = f->rate_h = f->rate_r = 0;
    r->ai_flags = 0;
    f->w2 &= (uint16_t)~0x0004u;
    /* the position: the player's plus (0, 3, 20) (the parachute (0, -3, 0)) model units through the player's matrix, each product sum shifted right by 6 */
    {
        const int16_t *of = e->tab ? e->tab->offs[parachute ? 1 : 0] : NULL;   /* tables/ai.bin, the offset table 0x2789E (the one source): 0 seat (0, 3, 20), 1 parachute and pod (0, -3, 0) */
        const int lx = of ? of[0] : 0, ly = of ? of[1] : 0, lz = of ? of[2] : 0;
        for (int k = 0; k < 3; k++) {
            int32_t sum = 0;
            sum = add32(sum, (int32_t)pf->mat_b[3 * k + 0] * lx);
            sum = add32(sum, (int32_t)pf->mat_b[3 * k + 1] * ly);
            sum = add32(sum, (int32_t)pf->mat_b[3 * k + 2] * lz);
            a->pos[k] = add32(p->pos[k], sum >> 6);
        }
    }
    f->w0 |= 0x10C0u | EW0_KEEP_MOVING | EW0_SPAWN;
    f->ground = 0x14;
    f->sink = 0;
    r->dest_cx = -1; r->dest_cz = 0; r->dest_fx = 0; r->dest_fz = 0; r->dest_h = 0;
    r->target = e->rec[0].target;
    if (pf->w2 & EW2_GROUND) { f->vel[1] = 0; f->w2 &= (uint16_t)~EW2_GROUND; }
    /* the velocity: the player's plus a thrust of length 0x3C0 along the player's up axis (down for the parachute) */
    {
        int32_t n3[3];
        const int sg = parachute ? -1 : 1;   /* down for the parachute: the up axis negated BEFORE the normaliser (rig: the truncation differs from negating its result) */
        ai_normalise(sg * (pf->mat_b[1] >> 2), sg * (pf->mat_b[4] >> 2), sg * (pf->mat_b[7] >> 2), 0x3C0, n3);
        for (int k = 0; k < 3; k++) f->vel[k] = add32(f->vel[k], n3[k]);   /* the copy's velocity (vy cleared when the player is on the ground, 2.8 step 2) plus the thrust */
    }
    f->counter = SEAT_COUNTER_START;
    if (parachute) e->chute_latch = (uint16_t)(0x200 * slot); else pf->v4aa7a = (uint16_t)(0x200 * slot);
    e->request = 0x8C;
    flight_sync(a);
    if (!pf->v4a83d && !parachute) entities_switch_view(e, slot);   /* the seat takes the view in the cockpit; the parachute does not (rig: test_seat_diff) */
}

int entities_seat_routine(entities_t *e, int slot, int allow_chute) {
    if (!e->player) return 0;
    flight_state_t *pf = &e->player->f;
    const int nib = pf->gear & 0x0F;
    if (pf->ev_seat) {
        pf->ev_seat = 0;
        if (pf->v4aa7a != 0) return 0;   /* the latch blocks every later cycle of the flight (6.4) */
        spawn_seat(e, slot, 0);
        pf->v4aa7a = (uint16_t)(0x200 * slot);
        return 1;
    }
    if (pf->v4aa7a != 0) return 0;
    if (nib >= 14 && allow_chute && e->chute_latch == 0) {
        pf->gear = (uint8_t)(pf->gear & 0xF0);
        spawn_seat(e, slot, 1);
        return 1;
    }
    if (nib == 9 && !pf->v4a83d && e->hook.sound) e->hook.sound(e->hook.user, 1);   /* the canopy: 0x26FFE */
    if (nib == 1 && e->hook.sound) e->hook.sound(e->hook.user, 2);   /* the ejection bang request: V:4A922 = 0x3F, V:4A921 = 0x78, V:4A870 = 0xFF (note 10 5.1, 8.2) */
    return 0;
}

int entities_rescue_key(entities_t *e) {
    if (!e->player || e->chute_latch) return 0;
    if (e->player->f.gear & 0x0F) return 0;   /* the key body is reached through the control chain, which is skipped while the ejection nibble is non-zero (note 10 8.2, 08 11) */
    int free_slot = 0;
    for (int i = 1; i <= 3; i++) if (!valid(e, i)) free_slot = 1;
    if (!free_slot) return 0;
    e->player->f.gear = (uint8_t)(e->player->f.gear | 0x0F);
    return 1;   /* the caller makes the release noise (0x1EE90: after the nibble, under the same condition; note 10 8.2) */
}

int entities_seat_step(entities_t *e, int idx) {
    aircraft_t *a = entities_ac(e, idx);
    flight_state_t *f = &a->f;
    flight_state_t *pf = &e->player->f;
    const int cls30 = f->cls == 0x30;
    if (f->w0 & EW0_DYING) {   /* destroyed: the player's release, the record is free, the display request */
        pf->w2 |= EW2_RELEASE;
        f->w0 = 0;
        e->request = 0x0C;
        return 0;
    }
    if (f->w0 & EW0_HIT) {   /* landed */
        int c = f->counter;
        if (c >= 5) c = 4; else c--;
        if (c < 0) c = 0;
        f->counter = (int16_t)c;
        if (cls30 && c <= 0 && *e->req == 0) { *e->req = 2; *e->req_delay = 4; }   /* request 2: the end of the ejection (6.4); only while no request is pending */
        f->vel[1] = 0;
        {   /* vx rises toward 0xC0 by 0xC and stays when above it, vz moves one toward 0 (rig: test_seat_diff; every class) */
            if (f->vel[0] < 0xC0) f->vel[0] += 0xC;   /* no clamp: 185 gives 197 (rig) */
            if (f->vel[2] > 0) f->vel[2]--; else f->vel[2]++;   /* 0 gives 1 (rig) */
        }
        {
            int32_t m = abs32(f->vel[0]);
            if (abs32(f->vel[1]) > m) m = abs32(f->vel[1]);
            if (abs32(f->vel[2]) > m) m = abs32(f->vel[2]);
            f->speed = (int16_t)(m >> 2);
        }
        return 1;
    }
    /* flying */
    const int c0 = f->counter;
    f->counter = w16(c0 + 1);
    const int c = f->counter;
    int d = c0 < 0 ? 6 : 2;
    if (c >= 3) d = 3;
    {   /* the view-switch point */
        int point = 0;
        const int pvalid = (pf->w0 & EW0_VALID) != 0;
        if (cls30 && c >= 3) { if (!pvalid || ((pf->w0 & 0x0004) && c < 5)) point = 1; }
        /* the second group: a negative counter (any class) or, for the seat, a valid player without W0 bit 0x0004 (rig: test_seat_diff, 20,000 cases) */
        if (c0 < 0 || (cls30 && c >= 3 && pvalid && !(pf->w0 & 0x0004))) { if ((pf->w0 & 0x0600) || a->pos[1] < 0x2000 || (cls30 && c >= 0x14)) point = 1; }
        /* the view switch (note 07 6.4, 0x1EC14 and 0x1E4D2): the viewed record becomes the seat; in the cockpit view the view byte stays 0, in an external view it is replaced by the saved view byte
         * V:4A8EB (rig: test_seat_diff, where it is 0); the ring direction becomes 4 (behind). Note 07 6.4: a class 0x31 or 0x32 record does not switch while its counter is 0 or more, but it can switch while the counter is negative (second group of conditions, any class;
         * test_seat_diff covers the negative counters of every class). The note's "drops to level 1 from level 2" is the value of the saved byte, which the game keeps (saved_view). */
        if (point && e->camera_update && entities_viewed(e) != idx) {
            const int ext = e->player->f.v4a83d != 0;   /* 0x1EC42: the external branch */
            entities_switch_view(e, idx); e->strip_off = 1;
            if (ext) e->player->f.v4a83d = e->saved_view;
            e->view_switched = (uint8_t)(ext ? 2 : 1);
        }
    }
    {
        const int32_t ax = abs32(f->vel[0]), az = abs32(f->vel[2]);
        int32_t thr = 0xF0 - c;   /* the counter as it is, negative too (rig) */
        if ((ax > az ? ax : az) >= thr) { f->vel[0] -= f->vel[0] >> d; f->vel[2] -= f->vel[2] >> d; }
        if (c >= 3 && f->vel[1] > 0) f->vel[1] -= 0x3C0;
        else if (f->vel[1] > -0x300) f->vel[1] -= 0x3C;   /* no clamp at -0x300: the next step converges on it (rig) */
        else f->vel[1] += (-0x300 - f->vel[1]) / 2;
        int32_t m = abs32(f->vel[0]);
        if (abs32(f->vel[1]) > m) m = abs32(f->vel[1]);
        if (abs32(f->vel[2]) > m) m = abs32(f->vel[2]);
        f->speed = (int16_t)(m >> 2);
    }
    return 1;
}

/* ---- the world update (1.2) ------------------------------------------------------------------------------------------------------------------ */

static void sync_globals(entities_t *e, flight_state_t *f, int idx) {
    const flight_state_t *p = &e->player->f;
    f->v4a984 = p->v4a984; f->v4a844 = p->v4a844; f->v4a847 = p->v4a847; f->v4a866 = p->v4a866; f->v4a867 = p->v4a867; f->v4a7b2 = p->v4a7b2;
    f->v4a95e = p->v4a95e; f->v4a903 = p->v4a903; f->v4a952 = p->v4a952; f->v4a83c = p->v4a83c;
    f->v4aa04 = p->v4aa04; f->v4aa06 = p->v4aa06;
    f->v4ab98 = p->v4ab98;   /* V:4AB98 is ONE global post word, last writer wins (note 07 4.8): a record's step reads and writes the player's copy */
    f->v4a96d = p->v4a96d; f->v4a992 = p->v4a992; f->v4a906 = p->v4a906; f->v4a907 = p->v4a907;
    f->v4a83d = 1;   /* a non-player record takes the external-view branches of the step (note 08 8.3, the dying path) */
    f->v4a890 = 0;
    f->self_off = (uint16_t)(0x200 * idx); f->view_off = p->v4a996;   /* the stepped record's offset (V:4AA6E) and the viewed record's (V:4A996): the ship test's "viewed mover" (note 07 10.2) */
}

/* After a record's flight step: the kill counter, the display requests and the objects the step asked for. */
static void collect(entities_t *e, flight_state_t *f) {
    e->player->f.v4ab98 = f->v4ab98;
    if (f->v4a963) { e->kills = (uint8_t)(e->kills + f->v4a963); f->v4a963 = 0; }
    if (f->v4a910) { e->request |= f->v4a910; f->v4a910 = 0; }
    if (f->ev_object) {   /* a copy of the record's first 32 bytes (note 07 5.1): the cell and fine words are the record's, the longs the backed-up point */
        secondary_t s;
        memset(&s, 0, sizeof s);
        for (int k = 0; k < 3; k++) s.pos[k] = f->ev_obj_pos[k];
        s.cell_x = f->ev_obj_cell[0]; s.cell_z = f->ev_obj_cell[1]; s.fine_x = f->ev_obj_fine[0]; s.fine_z = f->ev_obj_fine[1];
        s.sub = f->ev_obj_sub; s.nb = f->ev_obj_nb; s.alt8 = f->ev_obj_alt8;
        entities_object_create(e, &s, f->ev_object == 0x12 ? 0 : f->ev_object);   /* the ground object takes its model from the slot's parity */
        f->ev_object = 0;
    }
    f->v4a957 = 0;
}

/* The flight step of a record the generic way: the fresh pass of a placed record makes no motion (note 07 / training rig finding), an inactive record without the dying bit is not stepped. */
static void flight_part(entities_t *e, int i, int levels) {
    aircraft_t *a = entities_ac(e, i);
    flight_state_t *f = &a->f;
    const int roll_before = (int16_t)f->roll;
    if (!levels) f->keys &= 0xC3;
    if (f->w0 & (EW0_ACTIVE | EW0_DYING)) {
        const int fresh = (f->w0 & EW0_KEEP_MOVING) != 0;
        f->w0 &= (uint16_t)~EW0_KEEP_MOVING;
        if (fresh) f->turn_limit = 0x1400;
        flight_step_entity(a, f->keys, fresh);
    }
    /* note 07 3.8 item 4: an unscripted record with the epilogue flag in a state other than 6 and 0x0A clears the levelling mark when the hysteresis bits (flag bits 1 and 2) are both clear;
     * the aligned branches of the steering set one of them, so a pass that commands nothing keeps the mark (rig: test_ai_traj, 400 cases x 400 passes, with the flags read after the state routine) */
    if (levels && !(f->w2 & EW2_SCRIPTED) && f->state != 0x0A && f->state != 6 && !(e->rec[i].ai_flags & 0x06)) f->w0 &= (uint16_t)~EW0_LEVEL;
    if (levels && (f->state == 0x0A || f->state == 6)) {
        f->w0 |= EW0_LEVEL;
        f->keys = (uint8_t)(f->keys & 0x03);
        if (roll_before > 280 && roll_before <= 14400) f->keys |= FLIGHT_KEY_ROLL_RIGHT;
        else if (roll_before > 14400 && roll_before < 28520) f->keys |= FLIGHT_KEY_ROLL_LEFT;
    }
}

static void step_aircraft(entities_t *e, int i) {
    aircraft_t *a = entities_ac(e, i);
    flight_state_t *f = &a->f;
    entity_t *r = &e->rec[i];
    sync_globals(e, f, i);
    ai_gate(e, i);
    if (e->sim_on) {
        if ((f->w2 & EW2_SCRIPTED) && f->state == 0) f->w2 |= EW2_RECENTRE;
        else {
            ai_state(e, i);
            /* note 07 3.8 item 4 (rig: tests/rig/test_ai_traj.c): an unscripted aircraft whose steering ends with the target ahead on both paths (flag bits 5 and 6, the condition of the missile
             * launch, 4.6) is in state 6 with counter 30 at the end of the pass: it holds its controls for 30 steps with the wings levelled by the conversion */
            if ((f->state == 0 || f->state == 1) && (r->ai_flags & 0x60) == 0x60) { f->state = 6; f->counter = 30; }
        }
    }
    if ((e->pass & 31) == (unsigned)i) ai_region_check(e, i);
    const int levels = (f->w2 & EW2_SCRIPTED) || (r->ai_flags & 0x10);
    if (!e->paused) flight_part(e, i, levels);
    collect(e, f);
}

/* The attitude step 0x31AE0 of the classes 0x30 to 0x32 (note 07 6.3a), called before the position update and also when the hit bit is set: with the counter above 0 the roll word goes to 0
 * (at once in an external view; by 2,000 per step in the cockpit view, through 0 and 28800 as a circle) and the pitch and heading follow the direction of the velocity through the aim
 * routine with the limit 2,000; with the counter 0 or below only the matrices are rebuilt from the stored angles. */
static void seat_attitude(entities_t *e, int i) {
    aircraft_t *a = entities_ac(e, i);
    flight_state_t *f = &a->f;
    int32_t pitch = (uint16_t)f->pitch, heading = (uint16_t)f->heading, roll = (uint16_t)f->roll;
    if (f->counter > 0) {
        if (e->player->f.v4a83d) roll = 0;
        else if (roll != 0) {
            if (roll <= 14400) { roll -= 2000; if (roll < 0) roll = 0; }
            else { roll += 2000; if (roll >= 28800) roll = 0; }
        }
        if (e->hook.aim_dir) e->hook.aim_dir(e->hook.user, f->vel[0], f->vel[1], f->vel[2], &pitch, &heading);
    }
    flight_set_attitude(a, (uint16_t)pitch, (uint16_t)heading, (uint16_t)roll);
}

static void step_seat_or_weapon_flight(entities_t *e, int i) {
    aircraft_t *a = entities_ac(e, i);
    flight_state_t *f = &a->f;
    sync_globals(e, f, i);
    if ((f->cls & 0xF0) == 0x30) seat_attitude(e, i);
    f->lvl_on = 0;
    if ((f->cls & 0xF0) == 0 && e->sim_on && !(f->w0 & EW0_HIT)) {   /* RIG: the state routine does not run for a missile that has struck the ground */
        ai_state(e, i);   /* a missile: the state routine (the steering with the blind period, note 07 8.3) runs after the gate (3.1) */
        /* RIG (test_weapons_diff.c; note 07 3.8 item 4): with the epilogue flag (bit 4) and the flag bits 5 and 6 both set the conversion picks a roll rate target from the roll word for a
         * non-aircraft class: -960 for 281 to 14400, +960 for 14401 to 28519, else 0; the response uses it in this step (+0x54 itself is not written) */
        if ((e->rec[i].ai_flags & 0x10) && (e->rec[i].ai_flags & 0x60) == 0x60 && f->state != 6 && f->state != 0x0A) {
            const int roll = (uint16_t)f->roll;
            f->lvl_on = 1;
            f->lvl_roll = (int16_t)(roll >= 281 && roll <= 14400 ? -960 : roll >= 14401 && roll <= 28519 ? 960 : 0);
        }
    }
    flight_part(e, i, 1);
    if ((f->cls & 0xF0) == 0 && !(f->w2 & EW2_SCRIPTED) && !(e->rec[i].ai_flags & 0x10)) f->keys &= 0xC3;   /* a missile: the conversion drops the roll and stick bits unless the epilogue flag is set (3.8 item 1), after the commands were made from them (RIG) */
    collect(e, f);
}

static void dispatch(entities_t *e, int i) {
    aircraft_t *a = entities_ac(e, i);
    flight_state_t *f = &a->f;
    const int cn = f->cls & 0xF0;
    if (i == ENT_TRAINING_LEAD && e->ext[i] && e->hook.training) { e->hook.training(e->hook.user); return; }
    if (cn == 0x00) {
        if (e->hook.weapon && e->hook.weapon(e->hook.user, i)) step_seat_or_weapon_flight(e, i);
        return;
    }
    if (cn == 0x30) {
        if (e->paused) return;
        if (entities_seat_step(e, i)) step_seat_or_weapon_flight(e, i);
        return;
    }
    if (cn == 0x20) return;   /* the ships: the flight step alone, which the game runs for the carrier record */
    step_aircraft(e, i);
}

void entities_drop_stale_lock(entities_t *e) {
    if (e->designated >= 0 && e->designated < ENT_RECORDS) {
        const flight_state_t *f = &entities_ac(e, e->designated)->f;
        if (!(f->w0 & EW0_VALID) || (f->b20 & 0x02)) e->designated = -1;
    }
}

/* The player's range word +0x4A toward the designated target (0x2835A, note 07 12.6): only with a designation (index above 0); each pass adds 0x10 to the high nibble of +0x39; recomputed when the
 * old range is below 0x480, else when the counter exceeds 2 (old range below 0x900) or 5 (further), then the nibble is cleared; the value is the approximate length of the three absolute differences in
 * fine units, or 0x7FFF when one exceeds 0x7F00 (then B4 bit 0 is not set). The one range routine of the port (the live one: the world update calls it, tests/test_radar.c and the rig's range part
 * test this function); the designation itself (the T key) is radar.c's: it sets e->designated. */
void entities_update_player_range(entities_t *e) {
    if (!e->player || e->designated <= 0 || e->designated >= ENT_RECORDS) return;
    flight_state_t *pf = &e->player->f;
    const flight_state_t *tf = &entities_ac(e, e->designated)->f;
    entity_t *r = &e->rec[0];
    pf->repeat = (uint8_t)(pf->repeat + 0x10);
    const int8_t hi = (int8_t)(pf->repeat & 0xF0);
    const uint16_t old = (uint16_t)r->range;
    const int again = old < 0x480 ? 1 : (old < 0x900 ? hi > 0x20 : hi > 0x50);
    if (!again) return;
    pf->repeat &= 0x0F;
    int32_t dx = add32((int32_t)((uint32_t)(int32_t)w16(tf->cell_x - pf->cell_x) << 14), tf->fine_x - pf->fine_x);
    int32_t dz = add32((int32_t)((uint32_t)(int32_t)w16(tf->cell_z - pf->cell_z) << 14), tf->fine_z - pf->fine_z);
    int32_t dy = add32(tf->alt8, -pf->alt8);
    dx = abs32(dx); dz = abs32(dz); dy = abs32(dy);
    if (dx > 0x7F00 || dy > 0x7F00 || dz > 0x7F00) { r->range = 0x7FFF; return; }
    fx_vec_t v = { dx, dy, dz };
    int range = fx_dist(&v);
    if (range > 0x7FFF) range = 0x7FFF;
    r->range = (int16_t)range;
    pf->b4 |= 0x01;
}

/* The head of the world update 0x26AF8 (note 07 4.8, items 1 to 5), before the record loop whose first record is the player: the life words of the secondary objects (not in a pass with the pause
 * byte set), the region tick, the proximity bit (W2 bit 0) of the records 0 to 14 (so the player's contact scan then marks the records near it for their own gates in the same pass: 10.2, 9.4), and
 * the stale-lock drop. The damage cool-downs (item 4) are the weapons module's (weapons_world_start); the game calls it between the clear and the drop. A caller that steps the player before
 * entities_step (the game) calls this first; entities_step then skips the head. */
void entities_head_a(entities_t *e) {
    if (!e->player) return;
    if (!e->paused) for (int k = 0; k < ENT_OBJECTS; k++) e->obj[k].life--;
    entities_region_tick(e);
    for (int i = 0; i <= 14; i++) entities_ac(e, i)->f.w2 &= (uint16_t)~EW2_PROX;
}
void entities_head_b(entities_t *e) {
    if (!e->player) return;
    entities_drop_stale_lock(e);
    e->head_done = 1;
}

/* The player's pre-steps that sit in the entity layer (0x26BFC): the range word toward the designated target (0x2835A, 12.6), then the hook that lets a caller write the player's target byte. They
 * run before the player's step 0x2A2A2 (the record 0 block of the loop: the cue 0x270A0, 0x27B1E, 0x2835A, then the step). */
void entities_prestep(entities_t *e) {
    if (!e->player) return;
    /* The player's +0x38 byte is never written (note 05 17.14, "The player's +0x38 byte": the 12 store sites are all AI, placement and spawn; record 0's byte is 0 in 2,841 of 2,841 captured dumps), so the evasion group with the player as the target is always 18 and the seat copies target 0; the hook player_target may write it. */
    entities_update_player_range(e);
    if (e->hook.player_target) e->hook.player_target(e->hook.user, &e->rec[0].target, &e->rec[0].range);
}

void entities_step(entities_t *e) {
    e->gate_seen = 0;
    if (!e->player) return;
    if (!e->head_done) {   /* a caller that did not run the head and the pre-steps before a player step of its own (the rigs, the unit tests): the head, then the range */
        entities_head_a(e);
        entities_head_b(e);
        entities_prestep(e);
    }
    e->head_done = 0;
    for (int i = 1; i < ENT_RECORDS; i++) {
        if (i == 7) continue;
        if (i == ENT_TRAINING_LEAD && e->ext[i] && e->hook.training) { e->hook.training(e->hook.user); continue; }   /* the instructor's pass (it handles a lost lead itself, 8.9) */
        const int weapon_slot = i >= 1 && i <= 3;
        const int ai_missile = i == 9 || i == 11 || i == 13;
        if (weapon_slot && !valid(e, i)) {
            int filled;
            if (!e->req_player_launch) filled = entities_seat_routine(e, i, 1);
            else filled = e->hook.launch_player ? e->hook.launch_player(e->hook.user, i) : 0;
            if (!filled || !valid(e, i)) continue;
            dispatch(e, i);
            continue;
        }
        if (i == 5 && !valid(e, i)) {
            int filled;
            if (!e->req_ai_slot4) filled = entities_seat_routine(e, i, 0);
            else filled = e->hook.launch_ai ? e->hook.launch_ai(e->hook.user, i) : 0;
            if (!filled || !valid(e, i)) continue;
            dispatch(e, i);
            continue;
        }
        if (ai_missile && !valid(e, i)) {
            if (ai_launch_decision(e, i - 1) && e->hook.launch_ai && e->hook.launch_ai(e->hook.user, i) && valid(e, i)) dispatch(e, i);
            continue;
        }
        if (!valid(e, i)) continue;
        if (i == ENT_CARRIER || i == ENT_SHIP2) entities_ac(e, i)->f.w0 |= EW0_NOQUEUE;
        dispatch(e, i);
    }
    /* a request that found no free record in its pass is dropped, not kept (1.2) */
    if (e->req_player_launch || e->req_ai_slot4 || e->req_ai) e->n_dropped_requests++;
    e->req_player_launch = e->req_ai_slot4 = e->req_ai = 0;
    e->player->f.ev_seat = 0;
    e->lock_tail = e->designated;   /* the lock as the tail of the update (0x2852E) loads it, before its own drop: the F6 script's delay reads it (note 08 12.2) */
    entities_drop_stale_lock(e);
}

/* ---- the views (6.1, 6.3, 6.4) --------------------------------------------------------------------------------------------------------------- */

void entities_update_view_cell(entities_t *e, int external, int camera_cell_x, int camera_cell_z) {
    int cx, cz;
    if (external) { cx = camera_cell_x; cz = camera_cell_z; }
    else { const flight_state_t *vf = &entities_ac(e, entities_viewed(e))->f; cx = vf->cell_x; cz = vf->cell_z; }
    e->view_cell_x = (int16_t)cx; e->view_cell_z = (int16_t)cz;
    for (int i = 0; i < ENT_RECORDS; i++) { flight_state_t *f = &entities_ac(e, i)->f; f->v4aa04 = (int16_t)cx; f->v4aa06 = (int16_t)cz; }
}

int entities_fixed_camera(entities_t *e, int loc, int32_t pos[3]) {
    if (loc < 0 || loc > 4) return -1;
    if (loc <= 2 && !e->tab) return -1;   /* the airfield points and the fine offsets are the extractor's tables */
    if (loc <= 2) {
        const int16_t *c = e->tab->cam_point[loc];
        const int k = c[2] & 15;
        pos[0] = (int32_t)(((uint32_t)(int32_t)c[0] << 22) + ((uint32_t)(int32_t)(e->tab->fine[k][0] + c[4]) << 8));
        pos[2] = (int32_t)(((uint32_t)(int32_t)c[1] << 22) + ((uint32_t)(int32_t)(e->tab->fine[k][1] + c[5]) << 8));
        pos[1] = (int32_t)((uint32_t)(int32_t)c[3] << 8);
    } else {
        const int rec = e->tab ? (e->tab->start[loc][0] & 0x7FFF) : (loc == 3 ? ENT_CARRIER : ENT_SHIP2);   /* the table 0x47ADE names the carrier records 14 and 15 */
        if (rec >= ENT_RECORDS) return -1;
        const aircraft_t *s = entities_ac(e, rec);
        const int ship = s->f.cls == 0x20;
        const int lx = ship ? -36 : 0, ly = ship ? 47 : 20, lz = ship ? -48 : -106;
        for (int k = 0; k < 3; k++) {
            int32_t sum = 0;
            sum = add32(sum, (int32_t)s->f.mat_b[3 * k + 0] * lx);
            sum = add32(sum, (int32_t)s->f.mat_b[3 * k + 1] * ly);
            sum = add32(sum, (int32_t)s->f.mat_b[3 * k + 2] * lz);
            pos[k] = add32(s->pos[k], sum >> 4);
        }
    }
    return 0;   /* V:4A86D is cleared by the caller (game.c set_v4a86d), the one writer that keeps the world's and the entities' copies of the byte equal */
}

unsigned entities_kill_count(const entities_t *e) { return (unsigned)(e->kills + (e->player ? e->player->f.v4a963 : 0)) & 0xFF; }


uint16_t entities_random16(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return (uint16_t)(*state >> 16);
}
