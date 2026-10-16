/* src/core/radar.c - the radar list, threat bits, designation and the HUD target data (docs/re/07-ai-and-weapons.md section 12, "note 07"; see radar.h). */
#include <string.h>
#include "radar.h"

static int32_t add32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static int32_t sub32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
static uint32_t uabs32(int32_t v) { return v < 0 ? 0u - (uint32_t)v : (uint32_t)v; }
static int iabs_i(int v) { return v < 0 ? -v : v; }
static int16_t w16(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v & 0xFFFFu); }

#define RADAR_PAGE_WORDS 20   /* ten entries of two words */
#define CUE_MISSILE 0x0001u     /* 12.4 */
#define CUE_HOSTILE 0x0002u
#define CUE_FRIENDLY 0x0800u
#define CUE_PROMPT 0x0004u      /* 12.7 */
#define CUE_END 0x0008u
#define CUE_LOCK 0x0010u
#define CUE_IN_BOX 0x0100u
#define LOCK_BOX 0x4000
#define LOCK_NEAR 0x0200

void radar_reset(radar_t *r) {
    memset(r, 0, sizeof *r);
    r->countdown = -1;          /* V:4A93E starts at 0xFF (12.8) */
    r->mx = -1; r->my = -1;
    r->tpx = r->tpy = -1;
    r->emem[0] = r->emem[20] = r->emem[40] = 0xFFFF;   /* both lists empty */
}

/* ---- 12.2: the list builder ---------------------------------------------------------------------------------------------------------------- */

/* The rotation words V:4ACB4 derived from a matrix +0x92 (Q14, mat_b[3 * world axis + body axis]): row 0 the lateral axis, row 1 the vertical, row 2 the forward, each >> 6 (Q8). */
static void derive_rot(const flight_state_t *vf, int16_t m[9]) {
    for (int row = 0; row < 3; row++)
        for (int k = 0; k < 3; k++) m[3 * row + k] = (int16_t)(vf->mat_b[3 * k + row] >> 6);
}

void radar_build(radar_t *r, entities_t *e, const int16_t *rot) {
    int16_t mm[9];
    const int viewed = entities_viewed(e);
    const aircraft_t *va = entities_ac_c(e, viewed);
    if (!rot) { derive_rot(&va->f, mm); rot = mm; }
    r->nlist = 0;
    for (int i = 0; i < ENT_RECORDS; i++) {
        if (i == viewed) continue;
        const aircraft_t *a = entities_ac_c(e, i);
        if (!(a->f.w0 & EW0_VALID)) continue;
        int32_t d[3];
        uint32_t mx = 0;
        for (int k = 0; k < 3; k++) { d[k] = sub32(a->pos[k], va->pos[k]); if (uabs32(d[k]) > mx) mx = uabs32(d[k]); }
        int s = 0;
        while ((mx >> s) > 0x7FFFFFu) s += 2;
        int16_t dw[3];
        for (int k = 0; k < 3; k++) dw[k] = w16(d[k] >> (s + 8));
        int32_t x = add32(add32((int32_t)rot[0] * dw[0], (int32_t)rot[1] * dw[1]), (int32_t)rot[2] * dw[2]) >> 8;
        int32_t z = add32(add32((int32_t)rot[6] * dw[0], (int32_t)rot[7] * dw[1]), (int32_t)rot[8] * dw[2]) >> 8;
        radar_entry_t *en = &r->list[r->nlist++];
        en->x = (int32_t)((uint32_t)x << s);
        en->z = (int32_t)((uint32_t)z << s);
        en->flag = (uint16_t)((i << 8) | RADAR_PLOT);
    }
}

/* ---- 12.3: the scope walk ------------------------------------------------------------------------------------------------------------------- */

/* the list value shifted right by s, rounded half up, negated: the scope coordinate */
static int scope(int32_t v, int s) { return -(int)((v + (1 << (s - 1))) >> s); }
static int in_window(int nx, int nz) { return nx >= -27 && nx <= 27 && nz >= -16 && nz <= 21; }

static void walk(radar_t *r, const radar_in_t *in) {
    entities_t *e = in->e;
    flight_state_t *pf = &entities_ac(e, 0)->f;
    const int viewed = entities_viewed(e);
    const flight_state_t *vf = &entities_ac_c(e, viewed)->f;
    const int range_shift = vf->id & 0x0F;
    const int mode_skip = e->mode == 2 || e->mode == 0x7D;
    uint8_t pg = r->cur_page;
    /* the erase list of the page, read from the memory as it stands: up to eleven entries (the eleventh from past the page), a first word 0xFFFF ends it */
    r->nerase = 0;
    for (int k = 0; k < RADAR_ERASE; k++) {
        const uint16_t x = r->emem[RADAR_PAGE_WORDS * pg + 2 * k], y = r->emem[RADAR_PAGE_WORDS * pg + 2 * k + 1];
        if (x == 0xFFFF) break;
        radar_blip_t *b = &r->erase[r->nerase++];
        memset(b, 0, sizeof *b);
        b->x = (int16_t)(x & 0x7FFF); b->wide = (x & 0x8000) != 0; b->y = (int16_t)y;
    }
    r->nblip[pg] = 0;
    r->last_page = pg;
    r->frame++;
    r->threat = 0;
    for (int i = 0; i < r->nlist; i++) {
        radar_entry_t *en = &r->list[i];
        if (!(en->flag & RADAR_PLOT)) { en->flag &= (uint16_t)~RADAR_DRAWN; continue; }
        const int idx = en->flag >> 8;
        aircraft_t *a = entities_ac(e, idx);
        flight_state_t *f = &a->f;
        if (idx == viewed || !(f->w0 & EW0_VALID) || (f->w2 & EW2_GROUND)) { f->w0 &= (uint16_t)~EW0_RADAR; en->flag &= (uint16_t)~RADAR_DRAWN; continue; }
        const int nx = scope(en->x, 13), nz = scope(en->z, 13);
        if (!in_window(nx, nz)) { f->w0 &= (uint16_t)~EW0_RADAR; en->flag &= (uint16_t)~RADAR_DRAWN; continue; }
        const int nib = f->cls & 0xF0;
        int detected;
        if (mode_skip) detected = (f->b20 & 0x40) != 0;
        else if (uabs32(en->x) > 0x10000u || uabs32(en->z) > 0x10000u) { f->b20 &= (uint8_t)~0x40; detected = 0; }
        else { f->b20 |= 0x40; detected = 1; }
        if (detected && !mode_skip && nib != 0x20) {
            if (e->designated >= 0 && nib == 0x10 && viewed == 0) {   /* the mission-1 counter */
                int8_t c = (int8_t)r->mission1;
                if (c == 0) r->mission1 = 0x1E;
                else if (c > 0) { c--; if (c == 0) c--; r->mission1 = (uint8_t)c; }
            }
            if (!(f->b20 & 0x02) && !(f->w0 & (EW0_CRASHED | EW0_DYING)) && nib != 0x30) {
                if (nib == 0x00) {
                    if (!(f->w0 & EW0_NOT_COUNTED) && (e->rec[idx].target & 0x7F) == (pf->v4a994 & 0x7F))
                        r->threat |= (f->cls == 0) ? 0x10 : 0x20;
                } else if (nib == 0x10) {
                    if (f->w0 & EW0_NOT_COUNTED) r->threat |= 0x02;
                    else if (f->cls == 0x15) r->threat |= 0x08;
                    else r->threat |= 0x04;
                }
            }
        }
        f->w0 |= EW0_RADAR;
        {   /* the blip. RIG FINDING (tests/rig/test_radar_diff.c, walk): the drawn bit (flag bit 5) is set only when the blip's own scaled point passes the window; an entry that passed the
             * 13-nibble window but not the scaled one keeps it clear (12.3 says "every entry that passed the window") */
            const int bx = scope(en->x, range_shift), bz = scope(en->z, range_shift);
            if (in_window(bx, bz)) {
                en->flag |= RADAR_DRAWN;
                const int sx = bx + 158 + in->view_off, sy = bz + 167;
                const int blink_off = idx == e->designated && !(r->frame & 1);
                if (sx >= 0 && sx <= 319 && !blink_off && r->nblip[pg] < RADAR_BLIPS) {
                    radar_blip_t *b = &r->blip[pg][r->nblip[pg]++];
                    b->x = (int16_t)sx; b->y = (int16_t)(sy + in->hud_row);
                    b->wide = f->alt8 >= -in->cam_y;
                    b->pen = (uint8_t)((nib == 0x20 || nib == 0x30) ? 5 : (!detected || (f->b20 & 0x02)) ? 8 : (f->w0 & EW0_NOT_COUNTED) ? 4 : nib == 0 ? 2 : 1);
                    b->rec = (uint8_t)idx;
                }
            }
        }
    }
    for (int k = 0; k < r->nblip[pg]; k++) {   /* the page's list in memory, then its terminator (past the page after ten) */
        const radar_blip_t *b = &r->blip[pg][k];
        r->emem[RADAR_PAGE_WORDS * pg + 2 * k] = (uint16_t)(b->x | (b->wide ? 0x8000 : 0));
        r->emem[RADAR_PAGE_WORDS * pg + 2 * k + 1] = (uint16_t)b->y;
    }
    r->emem[RADAR_PAGE_WORDS * pg + 2 * r->nblip[pg]] = 0xFFFF;   /* the page flip belongs to the display flip, once per pass (radar_pass), not to the walk */
}

/* ---- 12.4: threat bits and cues ---------------------------------------------------------------------------------------------------------------- */

static void cues(radar_t *r, entities_t *e, flight_state_t *pf) {
    const uint8_t nw = r->threat, old = r->threat_prev;
    uint32_t cue = 0;
    int hold = 0, done = 0;
    if (nw & 0x10) { if (!(old & 0x10)) cue = CUE_MISSILE; done = 1; }
    else if (nw & 0x20) { if (!(old & 0x20)) cue = CUE_MISSILE; done = 1; }
    else if (nw & 0x08) {
        if (!(old & 0x08)) { cue = CUE_HOSTILE; hold = 1; done = 1; }
        else if (nw & 0x04) { if (!(old & 0x04)) cue = CUE_HOSTILE, hold = 1; done = 1; }
    } else if (nw & 0x04) {
        if (!(old & 0x04)) { cue = CUE_HOSTILE; hold = 1; done = 1; }
    }
    if (!done && (nw & 0x02) && !(old & 0x02) && e->mode != 5) cue = CUE_FRIENDLY;
    if (cue) pf->v4ac0c |= cue;
    if (hold) r->holdoff = 0x18;
    r->threat_prev = nw;
}

/* ---- 12.5: designation -------------------------------------------------------------------------------------------------------------------------- */

void radar_designate(radar_t *r, entities_t *e) {
    flight_state_t *pf = &entities_ac(e, 0)->f;
    int start = 0;
    if (e->designated >= 0) {
        int found = -1;
        for (int i = 0; i < r->nlist; i++) if ((r->list[i].flag >> 8) == e->designated) { found = i; break; }
        if (found >= 0) start = found + 1;   /* not in the list: a notice with no effect, the search starts at the top (12.5) */
    }
    for (int i = start; i < r->nlist; i++) {
        const radar_entry_t *en = &r->list[i];
        if ((en->flag & (RADAR_PLOT | RADAR_DRAWN)) != (RADAR_PLOT | RADAR_DRAWN)) continue;
        const flight_state_t *f = &entities_ac_c(e, en->flag >> 8)->f;
        const int nib = f->cls & 0xF0;
        if (nib == 0x00 || nib == 0x20 || nib == 0x30 || (f->b20 & 0x02)) continue;
        e->designated = (int16_t)(en->flag >> 8);
        r->page = 1; r->countdown = 1; r->holdoff = -1; r->listpos = (int16_t)(i + 1);
        pf->v4a986 &= (uint16_t)~0x2000;
        return;
    }
    e->designated = -1;
    r->listpos = 0;
}

void radar_pass(radar_t *r, const radar_in_t *in) {
    entities_t *e = in->e;
    flight_state_t *pf = &entities_ac(e, 0)->f;
    r->walk_done = 0;
    if (in->no_panel) return;   /* an external view without the panel counter runs the external block, not the scope draw: the list and the flags stay as they are (12.1, 12.2). No pause test: the builder 0x1FEE6 and the scope draw 0x35CF2 run in the passes of a script's count (V:4A866 at 1 to 5), n standing still (07 12.2) */
    if (in->redraw > 1) {   /* a pass with the page-redraw counter above 1 empties the erase lists first (12.2; the panel picture it copies has no blips) */
        r->emem[0] = r->emem[20] = 0xFFFF;
        r->nblip[0] = r->nblip[1] = 0;
    }
    const int build = in->redraw > 1 || (in->n & 3) == 0;
    const int walkf = in->redraw > 1 || !(in->n & 2);
    if (build) {
        radar_build(r, e, in->rot);
    }
    if (!walkf) return;
    /* The scope draw 0x35CF2 walks TWICE on a redraw pass (07 12.1): with R (V:4A8EF) of 1 or more it calls the cone lines 0x35D66 and the plus 0x35DDE, each of which ends in a walk 0x35E5E; with R = 0
     * the three schedules end in one. Both walks use the same page (the flip is the display's); the second reads the erase list the first wrote and rebuilds the blips, the threat bits and the list flags
     * (V:4A93B and the mission-1 counter count twice), and raises no cue of its own because the first stored V:4A926. The picture is the second walk's, drawn over the page the first walk erased:
     * the frontend erases what the FIRST walk read. */
    const int walks = in->redraw >= 1 ? 2 : 1;
    radar_blip_t first_erase[RADAR_ERASE];
    int nfirst = 0;
    for (int w = 0; w < walks; w++) {
        walk(r, in);
        if (w == 0) { nfirst = r->nerase; memcpy(first_erase, r->erase, sizeof first_erase); }
        r->walk_done = 1;
        cues(r, e, pf);
        if (r->req_target) { r->req_target = 0; radar_designate(r, e); }
    }
    r->nerase = (uint8_t)nfirst;
    memcpy(r->erase, first_erase, sizeof first_erase);
    r->cur_page ^= 1;
}

void radar_key_target(radar_t *r) { r->req_target = 1; }

void radar_lock_dropped(radar_t *r) { r->listpos = 0; r->tpx = r->tpy = -1; }

/* ---- 12.6: the range of the designated target is entities_update_player_range (entities.c: the world update runs it once per pass; the one routine) ---- */

/* ---- keys ----------------------------------------------------------------------------------------------------------------------------------------- */

/* R (0x1DEF4, note 07 12.3 "The range key", rig `weapon_check.py rangekey`, 80 cases; note 08 section 11 key census): the low nibble of the VIEWED record's +0x63 (record base + V:4A996, as the lock state and the
 * sight read it, 12.7) steps 9 -> 0xD, 0xB -> 9 and any other value, 0xD included, -> 0xB, the high nibble kept; 39 emulator presses showed one place per press of 0xD, 0xB, 9 and no other value.
 * In the seat view or a weapon camera the viewed record is not the player (the rig harness pokes V:4A996 = 0, which does not tell the two apart; the note says "viewed"). */
void radar_key_range(entities_t *e) {
    flight_state_t *f = &entities_ac(e, entities_viewed(e))->f;
    const int n = f->id & 0x0F;
    const int nn = n == 0x09 ? 0x0D : n == 0x0B ? 0x09 : 0x0B;
    f->id = (uint8_t)((f->id & 0xF0) | nn);
}

/* Y (0x1DF62): V:4AA7C + 1 (signed compare with 3: above 3 it becomes 1), bit 15 set, V:4A93E = 5, V:4A8F4 = 2. */
void radar_key_page(radar_t *r) {
    int32_t w = (int16_t)r->page + 1;
    if (w > 3) w = 1;
    r->page = (uint16_t)((uint16_t)w | 0x8000u);
    r->countdown = 5;
    r->line_redraw = 2;
}

/* the mission-clock tick (0x29896): V:4A93E counts down while it is not negative, 0 included */
void radar_clock_tick(radar_t *r) { if (r->countdown >= 0) r->countdown--; }

/* ---- 12.7: the marker, the lock state, the prompt, the pipper, the range number ---------------------------------------------------------------- */

/* The selector byte +0x63 of the VIEWED record (V:4A996 added to the record base): the lock state 0x38884, the slew and the sight 0x385F4 read it, not record 0's (note 07 12.7). */
static int viewed_sel(const radar_in_t *in) { return entities_ac_c(in->e, entities_viewed(in->e))->f.id & 0xF0; }
/* The range word +0x4A and the B4 byte of the VIEWED record (A1 = the record base + V:4A996 in 0x38884 and 0x385F4; 0x38722/0x38744/0x38756 through A0): the lock state, the sight's gun condition, the range dot, the
 * number and the clear of B4 bit 0 all read and write that record, not record 0 (they coincide while the player is viewed). */
static int16_t viewed_range(const radar_in_t *in) { return in->e->rec[entities_viewed(in->e)].range; }

/* The marker slew 0x38D8C (note 06 "Target symbols", 07 12.7) is a loop of four iterations (V:4AA62 = 0, 4, 8, 12). Each iteration reloads the target point V:4A9EE/F0 and the marker V:4A9FA/FC, then
 *  1. unless the script's count V:4A866 is set it moves the marker by ONE step: the invalid marker (no selection in the viewed record's +0x63, or the target x at or below 0) stores x = -1 and the old x in y
 *     (D2 still holds it); a marker at x <= 0 is initialised to (159, 91) and takes no step in that iteration; otherwise each axis moves by the gap shifted by (4, 2), or (2, 1) in the lock box
 *     (V:4AC08 bit 0x4000), the shift 4 or 2 above a gap of 15, limited to 1 (2 in the box);
 *  2. forms the box point: the target point plus the jitter pair 0x38FFC (only when record 0's +0x56 is non-zero, indexed by its +0x16 & 0x1C) plus V:4AA40 on x and V:4A990 on y;
 *  3. returns at once, over the copy, when either coordinate of the box point is at or below 0 (0x38EFE, 0x38F04);
 *  4. draws one edge of the box (the frontend's target box) and loops.
 * Only the end of the fourth iteration copies the point to V:4A9F2/F4 (0x38FE8) and stores -1 in V:4A9EE. */
void radar_slew(radar_t *r, const radar_in_t *in) {
    flight_state_t *pf = &entities_ac(in->e, 0)->f;
    const int sel = viewed_sel(in);
    static const int8_t JIT[8][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 0, -1 }, { -1, 0 }, { -1, -1 } };   /* 0x38FFC */
    const int16_t tx = (int16_t)in->tgt_x, ty = (int16_t)in->tgt_y;
    for (int it = 0; it < 4; it++) {
        if (!in->paused) {
            const int16_t ox = r->mx;
            if (sel == 0 || tx <= 0) { r->mx = -1; r->my = ox; }
            else if (ox <= 0) { r->mx = 159; r->my = 91; }
            else {
                const int box = (pf->v4ac08 & LOCK_BOX) != 0;
                const int s1 = box ? 2 : 4, s2 = box ? 1 : 2, lim = box ? 2 : 1;
                int m[2] = { r->mx, r->my };
                const int t[2] = { tx, ty };
                for (int a = 0; a < 2; a++) {
                    const int gap = (int16_t)(m[a] - t[a]);
                    int step = iabs_i(gap) > 15 ? gap >> s1 : gap >> s2;
                    if (step > lim) step = lim; else if (step < -lim) step = -lim;
                    m[a] = (int16_t)(m[a] - step);
                }
                r->mx = (int16_t)m[0]; r->my = (int16_t)m[1];
            }
        }
        int16_t bx = tx, by = ty;
        if (pf->rate_p != 0) {
            const int j = (int)(((uint32_t)entities_ac(in->e, 0)->pos[0] & 0x1C) >> 2);
            bx = (int16_t)(bx + JIT[j][0]); by = (int16_t)(by + JIT[j][1]);
        }
        bx = (int16_t)(bx + in->view_off); by = (int16_t)(by + in->hud_row);
        if (bx <= 0 || by <= 0) return;
    }
    r->tpx = tx; r->tpy = ty;
}

/* the lock state 0x38884 (12.7 table, first match). Returns the marker shape to draw. */
void radar_lock_state(radar_t *r, const radar_in_t *in) {
    entities_t *e = in->e;
    flight_state_t *pf = &entities_ac(e, 0)->f;
    const int sel = viewed_sel(in);
    const int periodic = (in->n & 0x1F) == 9;
    int32_t *ev = &pf->v4ac08;
    r->hud.marker = 0;
    const int target = e->designated >= 0;
    if (!target || sel == 0x10 || r->mx < 0) {
        if (*ev & (LOCK_BOX | LOCK_NEAR)) { *ev &= ~(LOCK_BOX | LOCK_NEAR); pf->v4ac0c |= CUE_END; }
        return;
    }
    r->hud.marker = 1;
    if (r->mx < 97 || r->mx > 221 || r->my < 47 || r->my > 133) {
        if (*ev & (LOCK_BOX | LOCK_NEAR)) { *ev &= ~(LOCK_BOX | LOCK_NEAR); pf->v4ac0c |= CUE_END; }
        r->prompt = 0;
        return;
    }
    if (iabs_i(r->mx - r->tpx) > 5 || iabs_i(r->my - r->tpy) > 5) {
        if ((*ev & LOCK_BOX) || !(*ev & LOCK_NEAR) || periodic) { *ev &= ~LOCK_BOX; *ev |= LOCK_NEAR; pf->v4ac0c |= CUE_IN_BOX; }
        r->prompt = 0;
        return;
    }
    r->hud.marker = 2;
    if (sel != 0x20 && sel != 0x30) { *ev |= LOCK_BOX; *ev &= ~LOCK_NEAR; return; }
    const uint16_t limit = sel == 0x30 ? 0x2700 : 0x1800;
    if ((uint16_t)viewed_range(in) >= limit) {
        *ev |= LOCK_BOX; *ev &= ~LOCK_NEAR;
        if (periodic) { *ev &= ~LOCK_BOX; *ev |= LOCK_NEAR; pf->v4ac0c |= CUE_IN_BOX; }
        r->prompt = 0;
        return;
    }
    *ev |= LOCK_BOX; *ev &= ~LOCK_NEAR;   /* RIG FINDING (test_radar_diff, lock): rows 6 and 7 clear 0x0200 as rows 4 and 5 do (12.7 lists only 0x4000) */
    if (r->closing >= -675) {
        if (r->prompt != 1 || periodic) pf->v4ac0c |= CUE_PROMPT;
        r->prompt = 1;
    } else {
        if (r->prompt != 2) pf->v4ac0c |= CUE_LOCK;
        r->prompt = 2;
    }
}

/* the pipper history 0x3878E: the screen point of the aim point two calls old, then the new aim point (24 up and 256 ahead of the nose) */
static void pipper(radar_t *r, const radar_in_t *in) {
    const aircraft_t *va = entities_ac_c(in->e, entities_viewed(in->e));
    fx_mat_t id;
    memset(&id, 0, sizeof id);
    id.m[0][0] = id.m[1][1] = id.m[2][2] = 0x100;
    const fx_mat_t *cam = in->cam ? in->cam : &id;
    fx_vec_t v, cv;
    v.x = w16(sub32(r->hist[1][0], va->pos[0]) >> 8);
    v.y = w16(sub32(r->hist[1][1], va->pos[1]) >> 8);
    v.z = w16(sub32(r->hist[1][2], va->pos[2]) >> 8);
    fx_rotate(&cv, cam, &v);
    int16_t sx, sy;
    fx_project(&sx, &sy, &cv);
    r->px = sx; r->py = sy;
    /* the new point: position + (0x600 * second column + 0x4000 * third column of the matrix +0x92) >> 6 */
    const int16_t *m = va->f.mat_b;
    int32_t np[3];
    for (int k = 0; k < 3; k++) {
        const int32_t s = add32((int32_t)0x600 * m[3 * k + 1], (int32_t)0x4000 * m[3 * k + 2]);
        np[k] = add32(va->pos[k], s >> 6);
    }
    memcpy(r->hist[1], r->hist[0], sizeof r->hist[1]);
    memcpy(r->hist[0], np, sizeof np);
}

/* the range number 0x367DA: the argument, and the text (a space for a negative change, a minus sign otherwise, four digits with the leading zeros kept) */
static void range_number(radar_t *r, const radar_in_t *in) {
    flight_state_t *pf = &entities_ac(in->e, entities_viewed(in->e))->f;   /* B4 bit 0 of the viewed record (0x38756) */
    const int16_t range = viewed_range(in);
    int16_t arg;
    /* RIG FINDING (test_radar_diff, sight): the routine does nothing at all (B4 bit 0 and V:4ABFC stay) for a range above 0x7F00 */
    if (range > 0x7F00) { r->hud.number = 0; return; }   /* a signed compare: a negative word passes (rig, range 0x8000) */
    if ((pf->b4 & EB4_RANGE_DONE) && !in->paused) {
        pf->b4 &= (uint8_t)~EB4_RANGE_DONE;
        arg = w16(range - r->last_range);
        r->closing = arg;
        r->last_range = range;
    } else arg = r->closing;
    r->hud.number = 1;
    unsigned m = (unsigned)iabs_i(arg) % 10000u;
    char *t = r->hud.number_text;
    t[0] = arg < 0 ? ' ' : '-';
    t[1] = (char)('0' + m / 1000); t[2] = (char)('0' + m / 100 % 10); t[3] = (char)('0' + m / 10 % 10); t[4] = (char)('0' + m % 10); t[5] = 0;
}

/* ---- 12.8: the message-line target data ------------------------------------------------------------------------------------------------------------ */

static void format_digits(radar_line_t *l, unsigned v, int ndig, int blank) {
    char tmp[8];
    for (int k = ndig - 1; k >= 0; k--) { tmp[k] = (char)('0' + v % 10); v /= 10; }
    tmp[ndig] = 0;
    if (blank) for (int k = 0; k < ndig - 1 && tmp[k] == '0'; k++) tmp[k] = ' ';
    memcpy(l->digits, tmp, (size_t)ndig + 1);
}

static void build_line(radar_t *r, const entities_t *e) {
    radar_line_t *l = &r->line;
    const aircraft_t *ta = entities_ac_c(e, e->designated);
    const flight_state_t *tf = &ta->f;
    const int pg = r->page & 0x7FFF;
    l->mode = 2; l->redraw = 1;
    l->cls = tf->cls; l->detected = (tf->b20 & 0x40) != 0;
    switch (tf->cls) {
    case 0x12: case 0x13: l->colour = 2; break;
    case 0x14: case 0x16: case 0x17: case 0x10: l->colour = 1; break;
    case 0x15: l->colour = 2; break;
    default: l->colour = 2; break;
    }
    if (!l->detected) l->colour = 0;
    l->page = (uint8_t)(pg >= 1 && pg <= 3 ? pg : 0);
    l->digits[0] = 0; l->col = 0;
    if (pg == 1) { l->col = 18; format_digits(l, (unsigned)(5 * (ta->pos[1] >> 10)) % 100000u, 5, 1); }
    else if (pg == 2) { l->col = 19; format_digits(l, (unsigned)(((uint16_t)tf->heading >> 3) / 10) % 1000u, 3, 0); }
    else if (pg == 3) {
        unsigned kt = (tf->w0 & EW0_HIT) ? 0u : (unsigned)iabs_i(tf->speed) / 12u;
        l->col = 18; format_digits(l, kt % 10000u, 4, 1);
    }
}

void radar_line_driver(radar_t *r, const radar_in_t *in) {
    entities_t *e = in->e;
    const flight_state_t *pf = &entities_ac_c(e, 0)->f;
    radar_line_t *l = &r->line;
    l->redraw = 0;
    int data = e->designated >= 0 && !(pf->v4a984 & 0x81);   /* a target and no latched message */
    if (data) {   /* the hold-off: decrement; the work proceeds only when the result is negative */
        /* The test is on the decremented value (the rig's line part): 0x18 gives 24 calls that do not proceed, the 25th does, as the note's 24-pass delay says. */
        r->holdoff = (int8_t)(r->holdoff - 1);
        if (r->holdoff >= 0) data = 0; else r->holdoff = -1;
    }
    if (!data) {
        /* RIG FINDING (test_radar_diff, line): everything but the target data goes through the one branch of the posted message: with no target, with a latched message and while the
         * hold-off lasts. A positive page word becomes 0xFFFF and a negative one 0 (the blank message); with the page word 0 the posted message shows and the redraw counter
         * V:4A8F4 counts down while positive (the copy of a new text when the code word changed sets it, the cockpit's part). */
        l->mode = 0;
        if ((int16_t)r->page > 0) { r->page = 0xFFFF; l->mode = 1; l->redraw = 1; }
        else if ((int16_t)r->page < 0) { r->page = 0; l->mode = 1; l->redraw = 1; }
        else if (r->line_redraw > 0) r->line_redraw--;
        return;
    }
    /* RIG FINDING (test_radar_diff, line): with the redraw counter V:4A8F4 positive, a pending rebuild (bit 15 of the page word, set by the Y key) is consumed (the bit cleared, the text
     * built, the counter unchanged); without the bit the counter counts down and the old text is drawn again. With the counter 0 a new page is chosen (the countdown negative: the page
     * word + 1 as a signed word, wrapping above 3 to 1, then the countdown 1) or kept (the countdown positive and odd, just after a designation or Y); an even positive countdown or 0
     * does nothing (the bit 15 stays). A chosen or kept page clears the bit 15, builds the text and sets the counter to 2. */
    if (r->line_redraw > 0) {
        if (r->page & 0x8000u) { r->page &= 0x7FFFu; build_line(r, e); }
        else { r->line_redraw--; if (l->mode == 2) l->redraw = 1; }
        return;
    }
    if (r->countdown < 0) {
        int32_t w = (int16_t)r->page + 1;
        if (w > 3) w = 1;
        r->page = (uint16_t)w;
        r->countdown = 1;
    } else if (!(r->countdown > 0 && (r->countdown & 1))) return;
    r->page &= 0x7FFFu;
    r->line_redraw = 2;
    build_line(r, e);
}

/* ---- the per-frame HUD pass -------------------------------------------------------------------------------------------------------------------------- */

/* The sight 0x385F4 (only at the zoom factor 0x80; the script's count V:4A866 stops only the number's delta and the pipper history): the fire prompt of the gun, the lock circle, the range dot and the
 * range number, then the pipper history at the common tail 0x38780. It reads the target point the slew of the same pass copied (the last copied one when its box returned early), the viewed
 * record's selector, range +0x4A and B4. */
void radar_sight(radar_t *r, const radar_in_t *in) {
    entities_t *e = in->e;
    flight_state_t *pf = &entities_ac(e, 0)->f;
    radar_hud_t *h = &r->hud;
    h->pip_pixel = h->circle = h->number = 0; h->dot = -1;
    const int sel = viewed_sel(in);
    const int zoom = (in->zoom ? in->zoom : 0x80) == 0x80;
    {
        const int target = e->designated >= 0;
        if (!zoom) return;   /* 0x385F4 tests the zoom word only; the script's count V:4A866 is tested at 0x3874E (the number's delta) and 0x38780 (the pipper history) */
        if (sel == 0x10) {
            const int range_ok = viewed_range(in) <= 0x900;
            const int cond = r->tpx > 0 && iabs_i(r->tpx - r->px) <= 8 && iabs_i(r->tpy - r->py) <= 8 && range_ok;
            /* RIG FINDING (test_radar_diff, sight): the condition is geometric (it does not look at the designation); a true condition raises the prompt only when a target is designated and
             * V:4A96C is 0 ("already shown" is any non-zero value); a false one clears a non-zero V:4A96C with the cue 8 whether or not a target is designated */
            if (cond) { if (target && r->prompt == 0) { pf->v4ac0c |= CUE_PROMPT; r->prompt = 1; } h->gun_prompt = r->prompt != 0; }   /* the prompt routine 0x36930 is called (0x386A0) while the condition holds and the state is non-zero */
            else if (r->prompt) { r->prompt = 0; pf->v4ac0c |= CUE_END; }
            h->pip_pixel = r->px > 0;
            h->circle = 1; h->circle_x = r->px; h->circle_y = r->py;
            if (target && r->px > 0) {   /* RIG FINDING: the dot and the number need the pipper x above 0 (the value before this call's tail) */
                h->dot = (int16_t)((uint32_t)(uint16_t)viewed_range(in) * 76u / 36000u);
                if (h->dot >= 76) h->dot = -1;
                range_number(r, in);
            }
        } else if (target) {   /* RIG FINDING (test_radar_diff, sight): any selection but the gun takes the missile branch (the selection 0 included) */
            if (r->tpx <= 0) { r->px = 159; r->py = 91; }
            h->circle = 1; h->circle_x = r->px; h->circle_y = r->py;
            if (r->px > 0) {
                h->dot = (int16_t)((uint32_t)(uint16_t)viewed_range(in) * 76u / 36000u);
                if (h->dot >= 76) h->dot = -1;
                range_number(r, in);
            }
        }
        if (!in->paused) pipper(r, in);   /* 0x38780 */
    }
}

void radar_hud_pass(radar_t *r, const radar_in_t *in) {
    radar_hud_t *h = &r->hud;
    h->pip_pixel = h->circle = h->number = 0; h->dot = -1; h->marker = 0; h->gun_prompt = 0;
    if (in->no_panel) return;   /* the HUD driver is part of the panel block: an external view whose panel counter V:4A8EE is 0 runs the external block, which has none */
    if (!in->cockpit) { r->prompt = 0; return; }   /* the driver's external return 0x37D70 clears the fire-prompt state V:4A96C (note 07 12.1) */
    /* The HUD driver 0x37D78 calls, in this order: the slew 0x38D8C, the lock state 0x38884, the line routine 0x36724, the HUD lines (the message-line data among them) and, last, the sight 0x385F4 (note 07 12.7).
     * The lock state and the sight see the target point the slew has just copied (or the last copied one when its box returned early). */
    radar_slew(r, in);
    radar_lock_state(r, in);
    radar_line_driver(r, in);
    if (!in->hud_off) radar_sight(r, in);   /* 0x37DA0: with V:4A859 clear the HUD draw returns before 0x385F4 (the lock state 0x38884 ran before the test) */
    /* The prompt is drawn by the lock state 0x38884 for a missile (whenever the state is non-zero) and by the gun branch of the sight 0x385F4 for the gun (only on a pass in which the sight ran and its
     * condition holds: not with the HUD off, not at another zoom factor). */
    h->prompt = (viewed_sel(in) == 0x10) ? (uint8_t)h->gun_prompt : r->prompt;
}

/* ---- the scene -------------------------------------------------------------------------------------------------------------------------------------------- */

void radar_fill_hud(const radar_t *r, const entities_t *e, game_hud_t *hud) {
    const flight_state_t *pf = &entities_ac_c(e, 0)->f;
    const int t = e->designated;
    hud->cp_threat = r->threat_prev;
    hud->cp_designated = t >= 0;
    hud->cp_target_rec = t >= 0 ? (uint8_t)t : 0xFF;
    hud->cp_range = (uint16_t)e->rec[0].range;
    hud->cp_pipper_x = r->px; hud->cp_pipper_y = r->py;
    hud->cp_lock = r->prompt;
    hud->cp_target_cls = t >= 0 ? entities_ac_c(e, t)->f.cls : 0;
    hud->cp_target_speed = t >= 0 ? (uint16_t)entities_ac_c(e, t)->f.speed : 0;
    hud->target_box_visible = t >= 0 && r->tpx > 0;
    hud->target_box_x = r->tpx; hud->target_box_y = r->tpy;
    hud->target_box_w = 9; hud->target_box_h = 8;
    (void)pf;
    hud->radar_count = 0;
    for (int i = 0; i < r->nlist && hud->radar_count < GAME_MAX_RADAR; i++) {
        const radar_entry_t *en = &r->list[i];
        const aircraft_t *a = entities_ac_c(e, en->flag >> 8);
        game_radar_t *o = &hud->radar[hud->radar_count++];
        o->x = en->x; o->z = en->z; o->alt8 = a->f.alt8; o->cls = a->f.cls; o->b01 = (uint8_t)((a->f.w0 >> 8) & 0xFF); o->b20 = a->f.b20;
        o->flags = (uint8_t)(((int)(en->flag >> 8) == t) ? 2 : 0);
    }
    hud->scope_walked = r->walk_done;
    hud->blip_count = r->nblip[r->last_page];
    memcpy(hud->blips, r->blip[r->last_page], sizeof hud->blips[0] * RADAR_BLIPS);
    hud->erase_count = r->nerase;
    memcpy(hud->erase, r->erase, sizeof hud->erase[0] * RADAR_ERASE);
    hud->tgt_prompt = r->hud.prompt;
    hud->tgt_sel = entities_ac_c(e, entities_viewed(e))->f.id;
    hud->tgt_marker = r->hud.marker; hud->tgt_marker_x = r->mx; hud->tgt_marker_y = r->my;
    hud->tgt_pip_pixel = r->hud.pip_pixel;
    hud->tgt_circle = r->hud.circle; hud->tgt_circle_x = r->hud.circle_x; hud->tgt_circle_y = r->hud.circle_y;
    hud->tgt_dot = r->hud.dot;
    hud->tgt_number = r->hud.number;
    memcpy(hud->tgt_number_text, r->hud.number_text, sizeof hud->tgt_number_text);
    hud->tline_mode = r->line.mode; hud->tline_redraw = r->line.redraw; hud->tline_cls = r->line.cls; hud->tline_detected = r->line.detected;
    hud->tline_colour = r->line.colour; hud->tline_page = r->line.page; hud->tline_col = r->line.col;
    memcpy(hud->tline_digits, r->line.digits, sizeof hud->tline_digits);
}
