/* src/core/training.c - the training parts (docs/re/08-game-flow.md section 8). See training.h. */
#include <stddef.h>
#include <string.h>
#include "training.h"
#include "flight.h"

#define W0_ACTIVE 0x1000u
#define W0_AI 0x0080u        /* the AI state routine runs for an aircraft with this bit (8.5) */
#define W0_FRESH 0x0100u     /* set by the placement; the pass that steps the record clears it and makes no motion */
#define W0_WAITED 0x2000u    /* set by the pass after the end routine's wait state 0x0A */
#define W0_VALID 0x0040u     /* cleared when the entity is lost (8.9) */
#define W0_DYING 0x0400u
#define W2_SCRIPTED 0x0100u  /* the instructor is scripted (8.3) */
#define W2_REQUEST 0x0800u   /* the player's RETURN request / the instructor's fire trigger (8.6) */
#define W2_SMOKE 0x1000u
#define W2_RECENTRE 0x0010u
#define F3 0x08u             /* the phase flags: bits of the byte +0x20 (8.5) */
#define F4 0x10u

/* The instructor's fixed throttle (8.3): +0x2B = 0x60, 80 percent. */
#define INSTRUCTOR_THROTTLE 0x60
/* The part 1 start state of record 4 (8.3): the placement 0x2CE36 for the training row of the scenario table. */
#define P1_X 307757056
#define P1_Y 3145728
#define P1_Z 328728576
#define P1_SPEED 8192
#define P1_ID 0x3D
/* Part 2: the lead is placed 72 model units (of 1024 position units) ahead of the player (6.1, 0x2CFDA). */
#define LEAD_AHEAD 72
/* The re-position offset (8.6): 9 to the right, 1 up, 97 behind, in model units through the Q14 matrix at +0x92, shifted right by 4. */
#define REPOS_DX 9
#define REPOS_DY 1
#define REPOS_DZ (-97)

static int16_t w16(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v & 0xFFFFu); }

/* ---- the tables ---------------------------------------------------------------------------------------------------------------------------- */

/* The rate-table entries the states use (8.5): pitch-rate index 4 = 320, 6 = 400, 7 = 480; roll-rate index 7 = 720, 8 = 800. The note's values stand in when the
 * tables are absent. */
static int pitch_rate_at(const trainer_t *t, int i, int dflt) { return t->have_tbl ? t->tbl.pitch_rate[i] : dflt; }
static int roll_rate_at(const trainer_t *t, int i, int dflt) { return t->have_tbl ? t->tbl.roll_rate[i] : dflt; }

void training_init(trainer_t *t, const training_t *tbl) {
    memset(t, 0, sizeof *t);
    if (tbl) { t->tbl = *tbl; t->have_tbl = 1; }
    t->out_ring = -1; t->out_view = -1;
}

void training_clear_outputs(trainer_t *t) {
    t->npost = 0;
    t->out_ring = -1; t->out_view = -1; t->out_redraw = 0;
}

static void post(trainer_t *t, uint16_t code) {
    if (t->npost < TRAINING_MAX_POSTS) t->posts[t->npost++] = code;
}

/* ---- the controls the state routines write (8.5, "Common elements") ----------------------------------------------------------------------- */

static void recentre(flight_state_t *f) { f->w2 |= W2_RECENTRE; }

/* roll command(d): bits 7 to 2 of the key byte cleared, 0x08 (roll left) if d > 0, 0x04 (roll right) if d < 0; the throttle bits 1 and 0 are kept. */
static void roll_cmd(flight_state_t *f, int d) {
    f->keys = (uint8_t)(f->keys & 0x03);
    if (d > 0) f->keys |= FLIGHT_KEY_ROLL_LEFT;
    else if (d < 0) f->keys |= FLIGHT_KEY_ROLL_RIGHT;
}

/* pitch command(d): the target pitch rate d against the pitch rate +0x56. d >= 0: 0x10 (stick forward) when d > rate; d < 0: 0x20 (stick back) when d < rate. The
 * ordinary form keeps bits 1 and 0 and clears the others; the barrel roll's keeps every bit except 5 and 4. */
static void pitch_cmd(flight_state_t *f, int d, int keep_roll) {
    f->keys = keep_roll ? (uint8_t)(f->keys & ~0x30u) : (uint8_t)(f->keys & 0x03);
    if (d >= 0) { if (d > f->rate_p) f->keys |= FLIGHT_KEY_STICK_FORWARD; }
    else if (d < f->rate_p) f->keys |= FLIGHT_KEY_STICK_BACK;
}

/* The end routine (8.5, "The end routine and the waits"): the manoeuvre is over. V:4A852 >= 0 (every shipped flow): the wait state 0x0A, 36 steps in part 1 and 60
 * in part 2. A negative V:4A852 (not reachable in the shipped flows): counter 10, state 6. No epilogue after it. */
static void end_routine(trainer_t *t, int *epilogue) {
    flight_state_t *f = &t->inst.f;
    if (t->trig < 0x80) { f->state = 0x0A; f->counter = t->running ? 36 : 60; }
    else { f->counter = 10; f->state = 6; }
    *epilogue = 0;
}

/* ---- the state routines (8.5) -------------------------------------------------------------------------------------------------------------- */

void training_ai(trainer_t *t) {
    aircraft_t *a = &t->inst;
    flight_state_t *f = &a->f;
    const int c_pull_320 = -pitch_rate_at(t, 4, 320);
    const int c_pull_400 = -pitch_rate_at(t, 6, 400);
    const int c_pull_480 = -pitch_rate_at(t, 7, 480);
    const int c_roll_720 = roll_rate_at(t, 7, 720);
    const int c_roll_800 = roll_rate_at(t, 8, 800);
    int epilogue = 1;
    for (int guard = 0; guard < 16; guard++) {
        const int pitch = (int16_t)f->pitch, roll = (int16_t)f->roll;
        switch (f->state) {
        /* the end routine's wait */
        case 0x0A:
            epilogue = 0;
            if (f->counter < 0) { f->state = 0; f->keys &= 0x03; }
            goto done;
        case 6: case 8:   /* the wait for a trigger (unreachable in the shipped flows): nothing */
            epilogue = 0;
            goto done;

        /* take-off and climb (lesson 0, part 2) */
        case 0x23:
            if ((int8_t)f->throttle < INSTRUCTOR_THROTTLE) f->keys |= FLIGHT_KEY_THROTTLE_UP;   /* rig: the other throttle bit is kept */
            else f->keys = (uint8_t)(f->keys & ~0x03u);
            if (f->speed >= 0x840) { f->state = 0x24; f->counter = 60; }
            goto done;
        case 0x24:
            if (f->counter < 0) { f->b20 &= (uint8_t)~F4; f->state = 0x25; continue; }
            goto done;
        case 0x25:
            if (!(f->b20 & F4)) {
                if (pitch < 14400) { pitch_cmd(f, -40, 0); goto done; }
                f->b20 |= F4;
            }
            if (pitch >= 26800) { pitch_cmd(f, -40, 0); goto done; }
            pitch_cmd(f, 0, 0);
            f->keys = 0;
            f->state = 0x26;
            continue;
        case 0x26:
            if (a->pos[1] > 0x240000) end_routine(t, &epilogue);
            goto done;

        /* aileron roll (lesson 1) */
        case 0x0B: f->b20 &= (uint8_t)~(F3 | F4); f->counter = 15; f->state = 0x0C; continue;
        case 0x0C:
            if (f->counter > 0) { recentre(f); goto done; }
            f->state = 0x0D; continue;
        case 0x0D:
            if (!(f->b20 & (F3 | F4))) {
                if (roll >= 14400) { roll_cmd(f, c_roll_800); goto done; }
                f->b20 |= F3;
            }
            if ((f->b20 & F3) && !(f->b20 & F4)) {
                if (roll < 14400) { roll_cmd(f, c_roll_800); goto done; }
                f->b20 |= F4;
            }
            if (roll < 800) { end_routine(t, &epilogue); goto done; }
            if (roll >= 28160) roll_cmd(f, 0);
            else roll_cmd(f, c_roll_800);
            goto done;

        /* break turn (lesson 2) */
        case 0x27: f->state = 0x28; f->counter = 15; continue;
        case 0x28:
            if (f->counter > 0) { recentre(f); goto done; }
            f->state = 2; continue;
        case 2: {
            const int T = roll < 14400 ? 7200 : 21600;
            int16_t d = w16(roll - T);
            const int E = roll < T ? w16(-d) : d;   /* word arithmetic: a wrapped difference stays as the 68000 leaves it */
            if (E >= 1200) { roll_cmd(f, roll < T ? c_roll_800 : -c_roll_800); goto done; }
            f->state = 7; f->counter = 32; f->keys &= 0x03;
            goto done;
        }
        case 7:
            if (f->counter < 0) { end_routine(t, &epilogue); goto done; }
            pitch_cmd(f, c_pull_320, 0);
            goto done;

        /* inside loop (lesson 3) */
        case 0x0E: f->b20 &= (uint8_t)~(F3 | F4); f->counter = 15; f->state = 0x0F; continue;
        case 0x0F:
            if (f->counter > 0) { recentre(f); goto done; }
            f->state = 0x10; continue;
        case 0x10:
            if (!(f->b20 & (F3 | F4))) {
                if (pitch <= 14400) { pitch_cmd(f, c_pull_400, 0); goto done; }
                f->b20 |= F3;
            }
            if ((f->b20 & F3) && !(f->b20 & F4)) {
                if (pitch > 14400) { pitch_cmd(f, c_pull_400, 0); goto done; }
                f->b20 |= F4;
            }
            if (pitch > 14400) end_routine(t, &epilogue);
            else pitch_cmd(f, c_pull_400, 0);
            goto done;

        /* split-S (lesson 4) */
        case 0x11:
            if (a->pos[1] <= 0x100000) { end_routine(t, &epilogue); goto done; }
            f->b20 &= (uint8_t)~(F3 | F4); f->counter = 15; f->state = 0x12; continue;
        case 0x12:
            if (f->counter > 0) { recentre(f); goto done; }
            f->state = 0x13; continue;
        case 0x13:
            if (!(f->b20 & F3)) {
                if (roll >= 14400) { roll_cmd(f, c_roll_800); goto done; }
                f->b20 |= F3;
            }
            if (roll == 14400) { f->state = 0x0F; f->counter = 2; continue; }
            if (roll >= 13760) { recentre(f); roll_cmd(f, 0); goto done; }
            roll_cmd(f, c_roll_800);
            goto done;

        /* vertical half-loop (lesson 5) */
        case 0x14: f->b20 &= (uint8_t)~F3; f->counter = 15; f->state = 0x15; continue;
        case 0x15:
            if (f->counter > 0) { recentre(f); goto done; }
            f->state = 0x16; continue;
        case 0x16:
            if (!(f->b20 & F3)) {
                if (pitch <= 14400) { pitch_cmd(f, c_pull_400, 0); goto done; }
                f->b20 |= F3;
            }
            if (pitch > 14400) { pitch_cmd(f, c_pull_400, 0); goto done; }
            f->state = 0x0D; continue;

        /* barrel roll (lesson 6) */
        case 0x17: f->b20 &= (uint8_t)~(F3 | F4); f->counter = 15; f->state = 0x18; continue;
        case 0x18:
            if (f->counter > 0) { recentre(f); goto done; }
            f->state = 0x19; continue;
        case 0x19:
            if (f->b20 & F4) { if (roll < 14400) { end_routine(t, &epilogue); goto done; } }
            else if (f->b20 & F3) { if (roll >= 14400) f->b20 |= F4; }
            else if (roll < 14400) f->b20 |= F3;
            roll_cmd(f, c_roll_720);
            pitch_cmd(f, c_pull_480, 1);
            goto done;

        /* inverted flight (lesson 7) */
        case 0x1A: f->b20 &= (uint8_t)~(F3 | F4); f->counter = 15; f->state = 0x1B; continue;
        case 0x1B:
            if (f->counter > 0) { recentre(f); goto done; }
            f->state = 0x1C; continue;
        case 0x1C:
            if (!(f->b20 & F3)) {
                if (roll >= 14400) { roll_cmd(f, c_roll_800); goto done; }
                f->b20 |= F3;
            }
            if (roll == 14400) { f->b20 &= (uint8_t)~F4; f->state = 0x1E; f->counter = 10; continue; }
            if (roll >= 13760) { recentre(f); roll_cmd(f, 0); goto done; }
            roll_cmd(f, c_roll_800);
            goto done;
        case 0x1E:
            if (f->counter > 0) { recentre(f); goto done; }
            f->state = 0x1F; continue;
        case 0x1F: {
            const int band_a = roll >= 7200 && roll <= 21600;
            int done_flag = 0;
            if (band_a) {
                if (!(f->b20 & F4)) {
                    if (pitch > 14400) { pitch_cmd(f, c_pull_320, 0); goto done; }
                    f->b20 |= F4;
                }
                if (pitch < 2400) { pitch_cmd(f, c_pull_320, 0); goto done; }
                done_flag = 1;
            } else {
                if (!(f->b20 & F4)) {
                    if (pitch < 14400) { pitch_cmd(f, c_pull_320, 0); goto done; }
                    f->b20 |= F4;
                }
                if (pitch < 26400) done_flag = 1;
                else { pitch_cmd(f, c_pull_320, 0); goto done; }
            }
            if (done_flag) { pitch_cmd(f, 0, 0); f->b20 &= (uint8_t)~F4; f->state = 0x21; f->counter = 25; continue; }
            goto done;
        }
        case 0x21:
            if (f->counter > 0) { recentre(f); goto done; }
            f->state = 0x22; continue;
        case 0x22: {
            const int band_a = roll >= 7200 && roll <= 21600;
            const int push = -c_pull_320;
            int done_flag = 0;
            if (band_a) {
                if (!(f->b20 & F4)) {
                    if (pitch < 14400) { pitch_cmd(f, push, 0); goto done; }
                    f->b20 |= F4;
                }
                if (pitch > 26400) { pitch_cmd(f, push, 0); goto done; }
                done_flag = 1;
            } else {
                if (!(f->b20 & F4)) {
                    if (pitch > 14400) { pitch_cmd(f, push, 0); goto done; }
                    f->b20 |= F4;
                }
                if (pitch > 2400) done_flag = 1;
                else { pitch_cmd(f, push, 0); goto done; }
            }
            if (done_flag) { pitch_cmd(f, 0, 0); f->b20 |= F3; f->state = 0x16; continue; }
            goto done;
        }
        default:
            epilogue = 0;
            goto done;
        }
    }
done:
    if (epilogue) t->b64 |= 0x10;
}

/* ---- the placement of record 4 (8.3, 6.1) ------------------------------------------------------------------------------------------------------ */

/* The instructor's own switches stay zero (training_start clears the whole instance): it has no pointer throttle and no preset (V:4A890 = 0 and P = Q = 0: the player's pointer
 * block is index 0 only, 8.3). */

/* Part 1, 0x2CE36 for the training row (8.3, 6.1). The placement OVERWRITES the whole head of the record (bytes 0x00 to 0xA3: the template copy, rig: a record filled with 0xA5 is
 * rewritten from +0x00 to +0xA3 and nothing else), so every motion word, the phase flags +0x20, the counter, the key byte, the state byte and W2 are cleared by it. The non-zero
 * words (tests/rig/test_training_traj.c compares them with the original's placement over a cleared record and, at the wrap of the cycle, over the flying instructor): W0 0x11C8,
 * the cell words 73 and 78 with the fine words, the altitude word and the position, +0x38 = 0xFF (unmodelled), the group 4, the class 0x10, the id 0x3D, +0x64 = 0x10, the speed words
 * 8192, the fuel 0x61A800, +0x71 and +0x5D = 0xFF (unmodelled), the gear byte +0x7C = 0xE0 (gear UP), +0x7D = 3, +0x7E = 0 (the first pass of the record writes 0x1400), the
 * attitude 0 with its two matrices. W2 is zero: the scripted bit 0x0100 comes from the mission start of mode 2 and from the sequencer's code after a re-placement. */
static void place_part1(trainer_t *t) {
    aircraft_t *a = &t->inst;
    flight_state_t *f = &a->f;
    memset(f, 0, offsetof(flight_state_t, vtx));
    f->w0 = 0x11C8;
    f->gear = 0xE0;
    f->l7d = 3;
    f->turn_limit = 0;
    f->fuel = 0x61A800;
    f->group = 4;
    f->cls = 0x10;
    f->id = P1_ID;
    t->b64 = 0x10;
    t->b70 = 0; t->b71 = 0xFF;   /* the head overwrite: +0x70 from the template (0), +0x71 = 0xFF */
    flight_set_position(a, P1_X, P1_Y, P1_Z);
    f->cell_a = f->cell_b = 0;
    flight_set_attitude(a, 0, 0, 0);
    f->speed_target = P1_SPEED; f->speed = P1_SPEED;
    f->w2 = t->placements ? 0 : W2_SCRIPTED;   /* the first placement: the mission start's flag; a re-placement leaves it to the sequencer, which sets it at once */
    flight_sync(a);
    t->placements++;
}

/* Part 2, 0x2CFDA: a copy of the first 0xA4 bytes of the player's record (the members of the flight state in front of the vertex slots), the class set to 0x10, the
 * group 4, and the position 72 model units straight ahead of the player along its heading: (0, 0, 72) through the heading-only matrix (rows (cos h, 0, -sin h),
 * (0, 1, 0), (sin h, 0, cos h), Q14) shifted right by 4, i.e. 73,728 position units along (-sin h, 0, cos h). */
static void place_part2(trainer_t *t, const aircraft_t *player) {
    aircraft_t *a = &t->inst;
    flight_state_t *f = &a->f;
    memcpy(f, &player->f, offsetof(flight_state_t, vtx));
    t->b70 = t->src_b70; t->b71 = t->src_b71;   /* the copy carries +0x70 and +0x71 of the player's record (they lie inside the first 0xA4 bytes) */
    int32_t s = 0, c = 0;
    flight_sincos_word(a, player->f.heading, &s, &c);   /* the table lookups count their reads on the instructor, not on the player */
    const int32_t dx = (int32_t)(((int64_t)(-s) * LEAD_AHEAD) >> 4), dz = (int32_t)(((int64_t)c * LEAD_AHEAD) >> 4);
    flight_set_position(a, (int32_t)((uint32_t)player->pos[0] + (uint32_t)dx), player->pos[1], (int32_t)((uint32_t)player->pos[2] + (uint32_t)dz));
    f->fine_x = player->f.fine_x; f->fine_z = player->f.fine_z;   /* rig: the copied fine words are not recomputed (the cell words are); the lead's first pass refreshes them */
    f->cls = 0x10;
    f->group = 4;
    f->state = 0; f->keys = 0;
    f->w2 &= (uint16_t)~(W2_SCRIPTED | W2_REQUEST);
    flight_sync(a);
    t->placements++;
}

static void place(trainer_t *t, const aircraft_t *player) {
    t->placed_new = 1;
    if (t->part == 1) place_part1(t); else place_part2(t, player);
}

void training_start(trainer_t *t, int mode_byte, const aircraft_t *player) {
    const training_t tbl = t->tbl;
    const int have = t->have_tbl;
    const uint8_t sb70 = t->src_b70, sb71 = t->src_b71;   /* the caller's input survives the clear */
    memset(t, 0, sizeof *t);
    t->tbl = tbl; t->have_tbl = have;
    t->src_b70 = sb70; t->src_b71 = sb71;
    t->out_ring = -1; t->out_view = -1;
    t->part = mode_byte == TRAINING_MODE_PART1 ? 1 : mode_byte == TRAINING_MODE_PART2 ? 2 : 0;
    if (!t->part) return;
    t->running = t->part == 1;        /* V:4A84B is set at the instant the part 1 mission starts (6.2) */
    t->viewed_inst = t->part == 1;    /* V:4A994 = 4 in part 1; the player in part 2 */
    place(t, player);
}

/* ---- the sequencer (8.4) -------------------------------------------------------------------------------------------------------------------- */

static uint16_t prompt_of(const trainer_t *t, int lesson) {
    static const int16_t dflt[8] = { 0, 46, 49, 48, 50, 51, 47, 52 };
    if (lesson < 0 || lesson > 7) return 0;
    return (uint16_t)(t->have_tbl ? t->tbl.a[lesson].prompt_id : dflt[lesson]);
}
static uint16_t name_of(const trainer_t *t, int lesson) {
    static const int16_t dflt[8] = { 0, 23, 26, 25, 27, 43, 24, 28 };
    if (lesson < 0 || lesson > 7) return 0;
    return (uint16_t)(t->have_tbl ? t->tbl.a[lesson].name_id : dflt[lesson]);
}
static uint32_t script_of(const trainer_t *t, int lesson) {
    static const uint32_t dflt[8] = { 0x80000023u, 0x8000000Bu, 0x80000027u, 0x8000000Eu, 0x80000011u, 0x80000014u, 0x80000017u, 0x8000001Au };
    return t->have_tbl ? t->tbl.b[lesson].script : dflt[lesson];
}
/* the lesson after n: n + 1, or lesson 1 after lesson 7 */
static int next_lesson(int n) { return n >= 7 ? 1 : n + 1; }

static void walk(trainer_t *t, aircraft_t *player);

/* The advance (0x27362 and 0x273F0). The RETURN of part 1 has re-placed the instructor already and enters the advance past the re-placement (rig: one placement). */
static void advance(trainer_t *t, aircraft_t *player, int replace_after_7) {
    flight_state_t *f = &t->inst.f;
    t->trig = 0;
    if (replace_after_7 && t->lesson >= 7) {   /* the instructor is re-placed: W0 cleared, the placement, W2 0x0100, the throttle 0x60 */
        f->w0 = 0;
        place(t, player);
        f->w2 |= W2_SCRIPTED;
        f->throttle = INSTRUCTOR_THROTTLE;
    }
    t->lesson = (uint8_t)next_lesson(t->lesson);
    t->cursor = 0;
    f->keys = 0;
    if (t->viewed_inst) {   /* the camera lines index table B by the entity index of the instructor (4), not by the lesson (8.2) */
        const int32_t cam = t->have_tbl ? t->tbl.b[4].camera : 0x0103;
        t->out_ring = (int8_t)(cam & 0xFF);
        if (t->part == 1) t->out_view = (int16_t)((cam >> 8) & 0xFF);   /* part 1, external view */
    }
    t->out_redraw = 1;
    if (t->lesson == 4 && t->inst.pos[1] <= 0x100000) post(t, TRAINING_POST_SPLITS_LOW);
    else post(t, name_of(t, t->lesson));
    walk(t, player);
}

/* The end of a manoeuvre (0x27314). */
static void end_of_manoeuvre(trainer_t *t, aircraft_t *player) {
    flight_state_t *f = &t->inst.f;
    if (t->part == 1) { advance(t, player, 1); return; }
    if (f->w2 & W2_REQUEST) { f->w2 &= (uint16_t)~W2_REQUEST; advance(t, player, 1); return; }
    if (t->trig >= 0x80) return;   /* waiting for the trigger */
    t->trig = 0xFF;
    post(t, prompt_of(t, next_lesson(t->lesson)));
}

/* Step 5, the table walk. */
static void walk(trainer_t *t, aircraft_t *player) {
    flight_state_t *f = &t->inst.f;
    const uint32_t script = script_of(t, t->lesson & 7);
    if (script == 0) return;
    if (!(script & 0x80000000u)) return;   /* a positive script pointer: the recorded-key interpreter is not reachable in the shipped game (8.2) and not reproduced */
    if (t->cursor == 0) {
        f->w2 &= (uint16_t)~W2_REQUEST;
        f->state = (uint8_t)(script & 0xFF);
        t->cursor = 1;
        return;
    }
    if (f->state != 0) return;   /* the manoeuvre is running */
    f->keys &= 0xC3;
    end_of_manoeuvre(t, player);
}

static void sequencer(trainer_t *t, aircraft_t *player) {
    flight_state_t *f = &t->inst.f;
    if (t->part == 2 && !(f->w2 & W2_SCRIPTED)) {   /* step 1: the instructor waits until the player's indicated speed is not zero */
        f->state = 0;
        if (player->f.speed == 0) return;
        f->w2 |= W2_SCRIPTED;
        t->cursor = 0;
    }
    if (t->paused || !(f->w2 & W2_SCRIPTED)) return;   /* step 2 */
    if (t->part == 1) {
        if (!t->running) return;   /* step 3 */
        if (player->f.w2 & W2_REQUEST) {   /* RETURN: the instructor is re-placed and the lesson skipped */
            player->f.w2 &= (uint16_t)~W2_REQUEST;
            f->w0 = 0;
            place(t, player);
            f->w2 |= W2_SCRIPTED;
            f->throttle = INSTRUCTOR_THROTTLE;
            advance(t, player, 0);
            return;
        }
        if ((int8_t)t->lesson <= 0) {   /* step 4: lesson 0 is never flown in part 1 */
            f->throttle = INSTRUCTOR_THROTTLE;
            player->f.v4a968 = 0xFE;
            end_of_manoeuvre(t, player);
            return;
        }
    }
    walk(t, player);
}

/* ---- the passes ------------------------------------------------------------------------------------------------------------------------------ */

void training_player_pass(trainer_t *t, aircraft_t *player) {
    if (t->part != 2 || !(player->f.w2 & W2_REQUEST)) return;
    flight_state_t *p = &player->f, *i = &t->inst.f;
    p->w2 &= (uint16_t)~W2_REQUEST;
    if (t->trig != 0xFF) t->lesson = (uint8_t)next_lesson(t->lesson);   /* each further RETURN skips one lesson */
    post(t, prompt_of(t, next_lesson(t->lesson)));
    t->trig = 0xFE;
    /* record 4 over the player's record (the first 0xA4 bytes), then the id restored, the group cleared, the class of the F/A-18, W2 0x0100 cleared, the state and key bytes cleared */
    const uint8_t id = p->id;
    memcpy(p, i, offsetof(flight_state_t, vtx));
    p->id = id; p->group = 0; p->cls = 0x11;
    p->w2 &= (uint16_t)~W2_SCRIPTED;
    p->state = 0; p->keys = 0;
    /* the position: the instructor's plus its matrix at +0x92 applied to (+9, +1, -97), Q14 products shifted right by 4 */
    int32_t pos[3];
    const int w[3] = { REPOS_DX, REPOS_DY, REPOS_DZ };
    for (int r = 0; r < 3; r++) {
        int32_t sum = 0;
        for (int c = 0; c < 3; c++) sum = (int32_t)((uint32_t)sum + (uint32_t)((int32_t)i->mat_b[3 * r + c] * w[c]));
        pos[r] = (int32_t)((uint32_t)t->inst.pos[r] + (uint32_t)(sum >> 4));
    }
    flight_set_position(player, pos[0], pos[1], pos[2]);
    p->v4a910 = 0xFF;
    t->out_redraw = 1;
}

void training_step(trainer_t *t, aircraft_t *player) {
    if (!t->part || t->paused || player->f.v4a866) return;
    if (!(t->inst.f.w0 & W0_VALID)) return;   /* 8.9: the pass skips a record whose valid bit is clear (0x26CDA): no sequencer, no step, no motion; the lost lead stays lost */
    flight_state_t *f = &t->inst.f;
    sequencer(t, player);
    f->counter = w16(f->counter - 1);   /* 0x278F6, before the state routine */
    f->w2 &= (uint16_t)~0x0001u;   /* W2 bit 0x0001 (proximity) is clear in the state the state routine and the step see (rig, test_training_diff) */
    /* the switches the step reads are the game's, one set for all records */
    f->v4a984 = player->f.v4a984; f->v4a844 = player->f.v4a844; f->v4a847 = player->f.v4a847;
    f->v4a866 = player->f.v4a866; f->v4a867 = player->f.v4a867; f->v4a7b2 = player->f.v4a7b2;
    f->v4a95e = player->f.v4a95e; f->v4a903 = player->f.v4a903; f->v4a952 = player->f.v4a952;
    f->v4a83c = player->f.v4a83c;
    f->v4aa04 = player->f.v4aa04; f->v4aa06 = player->f.v4aa06;   /* the viewed cell is one global (the +0x0B rule of the cell bookkeeping reads it) */
    f->v4a83d = 1;   /* a non-player record takes the external-view branches of the step (8.3, the dying path) */
    if (f->w2 & W2_SCRIPTED) {
        if (f->state != 0 && (f->w0 & W0_AI) && (f->v4a984 & 0x40)) { f->turn_limit = 0x1400; training_ai(t); }   /* the state routine writes the turn limit at its entry on every call (07 3.1) */
        else if (f->state == 0) recentre(f);
    } else if ((f->w0 & W0_AI) && (f->v4a984 & 0x40)) {
        /* an unscripted instructor (part 2 before the player moves; its destination +0x2C is -1) runs the original's generic AI, which does nothing but this in the rig:
         * the counter running out takes the wait state 6 (the same as the end routine with V:4A852 negative); the lead does nothing otherwise (note 08 8.3, "The unscripted lead", rig training_step_check part 4). The state routine writes the turn limit at its entry on every call (07 3.1). */
        f->turn_limit = 0x1400;
        if (f->counter < 0) { f->counter = 10; f->state = 6; }
    }
    const int roll_before = (int16_t)f->roll;
    /* 8.3 (state byte 0x0A and 6): the input conversion's levelling is reached only by a record that is scripted (W2 0x0100) or has +0x64 bit 4 set (the AI epilogue flag);
     * any other record gets its key byte ANDed with 0xC3 (0x31B8A-0x31BA8), the unscripted part 2 lead included (rig: test_training_diff, end-of-pass key byte) */
    const int levels = (f->w2 & W2_SCRIPTED) || (t->b64 & 0x10);
    if (!levels) f->keys &= 0xC3;
    if ((f->w0 & W0_ACTIVE) || (f->w0 & W0_DYING)) {   /* an inactive record is not stepped (the state routine ran: it does not test W0 bit 0x1000, rig) */
        const int fresh = (f->w0 & W0_FRESH) != 0;   /* a placed record: its first pass makes no motion and clears the bit (rig) */
        f->w0 &= (uint16_t)~W0_FRESH;
        flight_step_entity(&t->inst, f->keys, fresh);
    }
    if (levels && (f->state == 0x0A || f->state == 6)) {   /* the note names the states 0x0A and 6 (8.3, the input conversion 0x31BA8-0x31CD4); only 0x0A is reached by the shipped flows of a scripted lead */
        f->w0 |= W0_WAITED;
        /* rig: during the wait the wings are levelled by a roll command taken from the roll the pass began with, a dead band of 280 units (3.5 degrees) each side of 0:
         * roll right above it up to 180 degrees, roll left beyond; it runs after the input conversion, so the next pass flies it (bits 7 to 2 of the key byte replaced,
         * the throttle bits kept) */
        f->keys = (uint8_t)(f->keys & 0x03);
        if (roll_before > 280 && roll_before <= 14400) f->keys |= FLIGHT_KEY_ROLL_RIGHT;
        else if (roll_before > 14400 && roll_before < 28520) f->keys |= FLIGHT_KEY_ROLL_LEFT;
    }
    f->v4a957 = 0;
    if (f->v4a910) t->out_redraw = 1;   /* a cell change of record 4 requests the display-list rebuild as any record's does */
    f->v4a910 = 0;
}

int training_result(trainer_t *t) {
    if (t->part == 2 && t->placements && !(t->inst.f.w0 & W0_VALID) && !t->lost_reported) { t->lost_reported = 1; return TRAINING_LOST; }
    return TRAINING_RUNNING;
}

/* ---- the keys (8.6) -------------------------------------------------------------------------------------------------------------------------- */

int training_key_return(trainer_t *t, aircraft_t *player) {   /* 1 when the request was set (the original beeps there, 0x1E8C2, note 10 7.4) */
    if (t->part == 1) { player->f.w2 |= W2_REQUEST; return 1; }
    if (t->part != 2) return 0;
    if (t->trig >= 0x80) { player->f.w2 |= W2_REQUEST; return 1; }
    post(t, TRAINING_CODE_NOT_READY);
    return 0;
}

void training_key_fire(trainer_t *t, const aircraft_t *player) {
    if (t->part != 2 || t->trig < 0x80 || (player->f.gear & 0x0F)) return;
    t->inst.f.w2 |= W2_REQUEST;
}

/* Only the toggle of W2 bit 0x1000 is kept here (note 08 8.6); the trail it makes is drawn by the effects (the smoke trail, note 07 7.7). */
void training_key_smoke(trainer_t *t) {
    if (t->part == 2) t->inst.f.w2 ^= W2_SMOKE;
}
