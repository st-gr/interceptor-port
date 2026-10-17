/* mover.c - the camera mover of the front end, note 07 6.2 and note 08 6.4 (the 68000 routine at 0x2D8E8 with its stage routines and target routines). */
#include "mover.h"
#include "ai.h"
#include <string.h>

static int32_t asr(int32_t v, int n) { return v >> n; }
static int32_t absl(int32_t v) { return v < 0 ? (int32_t)(0u - (uint32_t)v) : v; }
static int32_t add(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static int32_t sub(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }

void mover_offset_point(const mover_ctx_t *c, int x, int y, int z, int32_t out[3]) {
    for (int i = 0; i < 3; i++) {
        const int32_t s = (int32_t)c->mat[3 * i] * (int16_t)x + (int32_t)c->mat[3 * i + 1] * (int16_t)y + (int32_t)c->mat[3 * i + 2] * (int16_t)z;
        out[i] = add(asr(s, 4), c->pos[i]);
    }
}

void mover_start_target(const mover_ctx_t *c, int32_t tgt[3]) { mover_offset_point(c, 0, 0, -5760, tgt); }

void mover_begin(mover_t *m, const mover_ctx_t *c) {
    memset(m, 0, sizeof *m);
    m->cam[0] = MOVER_SELECT_X; m->cam[1] = MOVER_SELECT_Y; m->cam[2] = MOVER_SELECT_Z;
    mover_start_target(c, m->tgt);
}

void mover_begin_mission(mover_t *m, const mover_ctx_t *c) {
    memset(m, 0, sizeof *m);
    m->cam[0] = MOVER_MAP_X; m->cam[1] = MOVER_MAP_Y; m->cam[2] = MOVER_MAP_Z;
    mover_start_target(c, m->tgt);
    m->stage = 8; m->wait = MOVER_MISSION_WAIT;
}

void mover_begin_stage3(mover_t *m, const mover_ctx_t *c, const int32_t cam[3]) {
    memset(m, 0, sizeof *m);
    for (int i = 0; i < 3; i++) { m->cam[i] = cam[i]; m->tgt[i] = c->pos[i]; }
    m->stage = 3;
}

/* The blend target of the stages 0 to 2 (0x2DB1E): a = (camera + record) / 2; b = (a + camera) / 2 in x and z and a / 2 in y; c = (b + a) / 2; e = (c + a) / 2; target = (e + c) / 2. */
static void blend_target(mover_t *m, const mover_ctx_t *c) {
    int32_t a[3], b[3], cc[3], e[3];
    for (int i = 0; i < 3; i++) a[i] = asr(add(m->cam[i], c->pos[i]), 1);
    for (int i = 0; i < 3; i++) b[i] = i == 1 ? asr(a[1], 1) : asr(add(a[i], m->cam[i]), 1);
    for (int i = 0; i < 3; i++) cc[i] = asr(add(b[i], a[i]), 1);
    for (int i = 0; i < 3; i++) e[i] = asr(add(cc[i], a[i]), 1);
    for (int i = 0; i < 3; i++) m->tgt[i] = asr(add(e[i], cc[i]), 1);
}

/* The offset targets of the stages 3 to 4 (0x2DBC0: y 4, z 3 or -9) and 4 to 5 (0x2DBE6: y 1, z 6); x is 6 for the starts 1 and 2 (V:4A900), else -6. */
static void offset_target(mover_t *m, const mover_ctx_t *c, int to_stage5) {
    const int near = c->location == 1 || c->location == 2;
    const int x = near ? 6 : -6;
    if (!to_stage5) mover_offset_point(c, x, 4, near ? 3 : -9, m->tgt);
    else mover_offset_point(c, x, 1, 6, m->tgt);
}

mover_out_t mover_step(mover_t *m, const mover_ctx_t *c) {
    mover_out_t out;
    memset(&out, 0, sizeof out);
    if (m->stage == 8 && m->wait > 0 && --m->wait == 0) m->stage = 0;   /* the approach's stage 0 takes over in the pass that ends the wait and makes its step in that pass (08 12.14 C14) */
    int32_t v[3];
    int32_t dmax = 0;
    for (int i = 0; i < 3; i++) { v[i] = sub(m->tgt[i], m->cam[i]); if (absl(v[i]) > dmax) dmax = absl(v[i]); }
    int shift = 0;
    int move = 1;         /* the stage routine ends in the step (0x2DC1A), except the stage 6 */
    switch (m->stage) {
    case 0:
        shift = 13;
        if (dmax <= 0x1C00000) {
            out.look_on = 1;
            shift = 11;
            if (dmax <= 0xD00000) { m->stage = 1; blend_target(m, c); }
        }
        break;
    case 1:
        shift = 11;
        if (dmax <= 0x200000) {
            shift = 10;
            if (dmax <= 0x60000) { m->stage = 2; blend_target(m, c); }
        }
        break;
    case 2:
        shift = 9;
        if (dmax > 0x60000) { blend_target(m, c); break; }
        shift = 7;
        if (dmax > 0x30000) { blend_target(m, c); break; }
        m->stage = 3;
        memcpy(m->tgt, c->pos, sizeof m->tgt);
        break;
    case 3:
        shift = 7;
        if (dmax > 0x60000) { memcpy(m->tgt, c->pos, sizeof m->tgt); break; }
        shift = 5;
        if (dmax > 0xD000) { memcpy(m->tgt, c->pos, sizeof m->tgt); break; }
        shift = 3;
        if (dmax > 0x8000) { memcpy(m->tgt, c->pos, sizeof m->tgt); break; }
        m->stage = 4;
        offset_target(m, c, 0);
        break;
    case 4:
        shift = 2;
        if (dmax <= 0x2800) { m->stage = 5; offset_target(m, c, 1); }
        break;
    case 5:
        shift = 1;
        break;
    case 8:   /* the briefing's stage 8: the camera is at its target, no step */
        move = 0;
        break;
    default:
        move = 0;   /* stage 6 (0x2DC44) and the briefing stages: no step */
        break;
    }
    if (move) {
        if (dmax <= MOVER_ARRIVE_D) {   /* 0x2DC1A: the arrival, in camera mode 6 only */
            out.arrived = 1;
            m->stage = 6; m->count = 5; out.look_off = 1;
        } else {
            /* the damped step (0x2DC5C): the difference divided by 4 until the distance is below 0x4800, normalised to 0x200, shifted left by the stage's shift and averaged with the previous step */
            int32_t d = dmax;
            while (d >= 0x4800) { for (int i = 0; i < 3; i++) v[i] = asr(v[i], 2); d = asr(d, 2); }
            int32_t n[3], st[3];
            ai_normalise(v[0], v[1], v[2], 0x200, n);
            for (int i = 0; i < 3; i++) st[i] = (int32_t)((uint32_t)(int32_t)(int16_t)n[i] << shift);
            if (m->trail[0] | m->trail[1] | m->trail[2]) {
                for (int i = 0; i < 3; i++) st[i] = add(m->trail[i], asr(sub(st[i], m->trail[i]), 1));
            }
            for (int i = 0; i < 3; i++) { m->trail[i] = st[i]; m->cam[i] = add(m->cam[i], st[i]); }
            return out;
        }
    }
    if (m->stage == 6) {   /* 0x2DC44: the passes after the arrival, the first counted in the arrival pass itself */
        m->count--;
        if (m->count <= 0) out.cockpit = 1;
    }
    return out;
}
