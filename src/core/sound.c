/* src/core/sound.c - the sound library of the original as an integer model (docs/re/10-sound.md). See sound.h for the contract. Section numbers are note 10's. */
#include <string.h>
#include "sound.h"

#define PER_MIN 0x7C            /* the least period written (1.4, Constants) */
#define SEED 0x47091247u        /* the random register's initial value (3.3) */
#define NOISE_PERIOD 0x231E     /* the noise period base: 0x231E + 4 r, r 11 bits (3.4) */
#define WIND_PERIOD 0x168       /* 0x168 + r, r 6 bits per voice (5.6) */
#define LAUNCH_PERIOD 0x8C      /* 0x8C + r, r 3 bits (3.5) */
#define GLIDE_TICKS 32          /* the engine glide: 32 blanks (Constants) */

/* script field codes (3.1) */
enum { F_PERIOD = 0x08, F_VOLUME = 0x0C, F_PSTEP = 0x18, F_VSTEP = 0x1C, F_LC0 = 0x24, F_LC1 = 0x28, F_WAIT = 0x2C, F_LOOP0 = 0x40, F_LOOP1 = 0x44 };

/* The five scripts (3.1 to 3.5). Only the structure the note states is here: the fixed commands and the jump targets. Every parameter slot is 0 until a wrapper writes it, and every wrapper writes
 * all of its slots before the object starts (tone: nine, noise: three, gun: two, launch: two); the gear script has no parameter. Nothing here is a value of the image. */
#define SLOT 0
static const int32_t SCRIPT_TONE[12][2] = {      /* 3.2: outer repeat p8 { inner repeat p7 { (p1, p2) for p3 blanks, (p4, p5) for p6 blanks }, wait p9 }, then the end */
    { F_LC0, SLOT },         /* p8: the outer repeats */
    { F_LC1, SLOT },         /* p7: the inner repeats */
    { F_PERIOD, SLOT },      /* p1 */
    { F_VOLUME, SLOT },      /* p2 */
    { F_WAIT, SLOT },        /* p3 */
    { F_PERIOD, SLOT },      /* p4 */
    { F_VOLUME, SLOT },      /* p5 */
    { F_WAIT, SLOT },        /* p6 */
    { F_LOOP1, 16 },         /* the inner loop: back to the command that sets p1 (byte offset 16) */
    { F_WAIT, SLOT },        /* p9 */
    { F_LOOP0, 8 },          /* the outer loop: back to the command that sets the inner counter (byte offset 8) */
    { F_WAIT, 0 } };         /* the end */
static const int32_t SCRIPT_NOISE[5][2] = {      /* 3.4: volume a, step -(a << 16) / b, wait b; volume 0; the end */
    { F_VOLUME, SLOT }, { F_VSTEP, SLOT }, { F_WAIT, SLOT }, { F_VOLUME, 0 }, { F_WAIT, 0 } };
static const int32_t SCRIPT_GEAR[5][2] = {       /* 3.5: period 700, step -7 for 15 blanks, step 0, wait for ever (the stop of voice 2 ends it) */
    { F_PERIOD, 700 << 16 }, { F_PSTEP, -7 * 65536 }, { F_WAIT, 15 }, { F_PSTEP, 0 }, { F_WAIT, -1 } };
static const int32_t SCRIPT_LAUNCH[8][2] = {     /* 3.5: volume 11 rising by 2 for 8 blanks; volume v, step -((v - 1) << 16) / 60, wait 60; volume 0; the end */
    { F_VOLUME, 11 << 16 }, { F_VSTEP, 2 << 16 }, { F_WAIT, 8 }, { F_VOLUME, SLOT }, { F_VSTEP, SLOT }, { F_WAIT, 60 }, { F_VOLUME, 0 }, { F_WAIT, 0 } };
static const int32_t SCRIPT_GUN[12][2] = {       /* 3.5: a pulses of { volume b, step 0, wait 2; volume 0, wait 1 }, then period 280, volume 20, step -61,440, wait 10, the end */
    { F_LC0, SLOT }, { F_VOLUME, SLOT }, { F_VSTEP, 0 }, { F_WAIT, 2 }, { F_VOLUME, 0 }, { F_WAIT, 1 }, { F_LOOP0, 8 },
    { F_PERIOD, 280 << 16 }, { F_VOLUME, 20 << 16 }, { F_VSTEP, -61440 }, { F_WAIT, 10 }, { F_WAIT, 0 } };

/* ---- the command list: the register writes (scene.h) ----------------------------------------------------------------------------------------------------------- */
static void emit(sound_t *s, int op, int v, int obj, int value, uint32_t off, uint32_t len) {
    game_audio_cmd_t *c;
    if (s->ncmd >= GAME_MAX_AUDIO_CMDS) { s->dropped++; return; }   /* the bound of scene.h is proven, so this is not reached; it is counted and reported (scene audio_overflow), never hidden */
    c = &s->cmd[s->ncmd++];
    c->op = (uint8_t)op; c->voice = (uint8_t)v; c->object = (uint8_t)obj; c->value = (uint16_t)value; c->blank = s->blank; c->clock = s->clk; c->offset = off; c->length = len;
}

static void reg_period(sound_t *s, int v, int per) { if (s->v[v].per != per) { s->v[v].per = (uint16_t)per; emit(s, GAME_AUD_PERIOD, v, 0, per, 0, 0); } }
static void reg_volume(sound_t *s, int v, int vol) { if (s->v[v].vol != vol) { s->v[v].vol = (uint8_t)vol; emit(s, GAME_AUD_VOLUME, v, 0, vol, 0, 0); } }
static void reg_dma(sound_t *s, int v, int on) { if (s->v[v].dma != on) { s->v[v].dma = (uint8_t)on; emit(s, on ? GAME_AUD_DMA_ON : GAME_AUD_DMA_OFF, v, 0, 0, 0, 0); } }
/* two objects play the same block when they are one object, clones sharing a file range, or the two users of the noise buffer */
static int same_block(const sound_t *s, int a, int b) {
    if (a == b) return 1;
    if (a == SND_NONE || b == SND_NONE) return 0;
    const sound_table_t *t = &s->tab;
    if (t->obj[a].kind != t->obj[b].kind) return 0;
    if (t->obj[a].kind == SOUND_KIND_NOISE) return 1;
    if (t->obj[a].kind == SOUND_KIND_SAMPLE) return t->obj[a].file == t->obj[b].file && t->obj[a].offset == t->obj[b].offset && t->obj[a].length == t->obj[b].length;
    return 0;
}
static void reg_latch(sound_t *s, int v, int obj) {   /* AUDxLC and AUDxLEN: a rewrite of the block already latched is omitted (the hardware repeat re-reads the same registers) */
    uint32_t off = 0;
    if (same_block(s, s->v[v].pend, obj)) { s->v[v].pend = (uint8_t)obj; return; }
    if (s->tab.obj[obj].kind == SOUND_KIND_SAMPLE) off = s->tab.obj[obj].offset;
    s->v[v].pend = (uint8_t)obj;
    emit(s, GAME_AUD_LATCH, v, obj, 0, off, s->o[obj].length);
}

/* ---- the random register (3.3) -------------------------------------------------------------------------------------------------------------------------------- */
static int rnd_bit(sound_t *s) {
    const uint32_t b = (s->rnd ^ (s->rnd >> 3)) & 1u;
    s->rnd = (s->rnd >> 1) | (b << 31);
    return (int)b;
}
uint32_t sound_random(sound_t *s, int bits) {
    uint32_t r = 0;
    for (int i = 0; i < bits; i++) r = (r << 1) | (uint32_t)rnd_bit(s);
    return r;
}

/* ---- the registers, the interrupt handler and the stops (4.1, 4.2) ------------------------------------------------------------------------------------------------- */
static void write_regs(sound_t *s, int v, const sound_obj_t *o) {   /* 0x55260: the period (floor 0x7C), then the volume (masked, capped by the master level) */
    const int hi = (int16_t)((uint32_t)o->period >> 16);
    int vol = (int)(((uint32_t)o->volume >> 16) & 0x3F);
    const int cap = s->level >> 16;
    if (vol > cap) vol = cap;
    reg_period(s, v, hi < PER_MIN ? PER_MIN : hi);
    reg_volume(s, v, vol);
}

/* the audio interrupt for voice v: with no object the sound stops; else the block is loaded (the first load of a stopped channel starts its DMA and asks for a second at once), the registers written,
 * the count run down and the slot moved on when it falls below 0 */
static void handler(sound_t *s, int v) {
    sound_voice_t *w = &s->v[v];
    if (w->slot == SND_NONE) { reg_volume(s, v, 0); reg_period(s, v, PER_MIN); reg_dma(s, v, 0); w->cur = SND_NONE; return; }   /* the latch registers keep what they hold */
    sound_obj_t *o = &s->o[w->slot];
    const int started = !w->dma;
    reg_latch(s, v, w->slot);
    if (started) { w->cur = w->slot; w->left = (int32_t)s->o[w->cur].length; w->cnt = 0; reg_dma(s, v, 1); }
    write_regs(s, v, o);
    if (o->count != -1) {
        o->count--;
        if (o->count < 0) { w->slot = o->next; o->count = o->reload; }
    }
    if (started) handler(s, v);
}

static void stop_raw(sound_t *s, int v) { s->v[v].slot = SND_NONE; handler(s, v); }

void sound_start(sound_t *s, int object, int voice, int volume) {
    if (object < 0 || object >= SND_OBJECTS || !s->o[object].exists || voice < 0 || voice >= SND_VOICES) return;
    stop_raw(s, voice);
    s->o[object].volume = (int32_t)((uint32_t)volume << 16);
    s->v[voice].slot = (uint8_t)object;
    handler(s, voice);
}
void sound_stop_voice(sound_t *s, int voice) {
    if (voice < 0 || voice >= SND_VOICES) return;
    stop_raw(s, voice);
}
void sound_stop_all(sound_t *s) {
    for (int v = 0; v < SND_VOICES; v++) stop_raw(s, v);
}

/* ---- the script interpreter and the per-blank server (3.1, 4.3) ---------------------------------------------------------------------------------------------- */
static int32_t *field(sound_obj_t *o, int code) {
    switch (code) {
    case F_PERIOD: return &o->period;
    case F_VOLUME: return &o->volume;
    case F_PSTEP: return &o->pstep;
    case F_VSTEP: return &o->vstep;
    case F_LC0: return &o->lc[0];
    case F_LC1: return &o->lc[1];
    default: return NULL;
    }
}

static void script_run(sound_t *s, int v, sound_obj_t *o) {
    int guard = 0;
    while (o->script && guard++ < 4096) {
        const int idx = o->pos / 8;
        if (idx < 0 || idx >= SND_SCRIPT_CMDS) break;
        const int code = s->script[o->script][idx][0];
        const int32_t val = s->script[o->script][idx][1];
        o->pos += 8;
        if (code == F_WAIT) {
            if (val == 0) { o->wait = 0; stop_raw(s, v); return; }   /* the script ends: the voice is stopped */
            o->wait = val;
            return;
        }
        if (code == F_LOOP0 || code == F_LOOP1) {
            int32_t *c = &o->lc[code == F_LOOP1];
            if (*c == 0) o->pos = val;                  /* a counter of 0 loops for ever */
            else { (*c)--; if (*c != 0) o->pos = val; }
            continue;
        }
        int32_t *f = field(o, code);
        if (f) *f = val;
    }
}

static void server_voice(sound_t *s, int v) {
    sound_obj_t *o = &s->o[s->v[v].slot];
    if (o->wait != 0) {
        o->wait--;
        if (o->wait == 0) script_run(s, v, o);
    }
    /* a script that ended the voice does not end the blank's work: the registers are written from the object and the steps added (the stop already ran the handler) */
    write_regs(s, v, o);
    o->period = (int32_t)((uint32_t)o->period + (uint32_t)o->pstep);
    o->volume = (int32_t)((uint32_t)o->volume + (uint32_t)o->vstep);
    if (o->ptimer) { if (--o->ptimer == 0) o->pstep = 0; }
    if (o->vtimer) { if (--o->vtimer == 0) o->vstep = 0; }
}

/* The hardware over one blank (4.2, 9.2 "The block clock, exactly as the hardware keeps it"): a sample lasts the period register's value at the moment it starts (the counter is reloaded at every sample's
 * end); a block of n bytes ends after n samples; the next loaded block begins at once and the voice's interrupt runs. Within a blank the register is constant, so the blank's 71,051 colour clocks
 * finish the sample in progress (cnt clocks left), then run whole samples at the register's period; a block end inside the blank runs the handler there and the rest of the blank goes on in the new block. */
static void hw_advance(sound_t *s, int v) {
    sound_voice_t *w = &s->v[v];
    int32_t avail = (int32_t)SND_CLOCKS_PER_BLANK;
    for (int guard = 0; w->dma && avail > 0 && guard < 64; guard++) {
        const int32_t per = w->per ? w->per : 1;
        if (w->cnt > 0) {                                    /* the sample in progress */
            if (avail < w->cnt) { w->cnt -= avail; return; }
            avail -= w->cnt; w->cnt = 0; w->left--;
        } else if (w->left > 0) {                            /* whole samples at the register's period */
            const int32_t fit = avail / per;
            if (fit >= w->left) { avail -= w->left * per; w->left = 0; }
            else { w->left -= fit; avail -= fit * per; if (avail > 0) w->cnt = per - avail; return; }
        }
        if (w->left <= 0) {
            w->cur = w->pend; w->left = (int32_t)s->o[w->cur].length;
            s->clk = (uint32_t)(SND_CLOCKS_PER_BLANK - avail);   /* the interrupt runs where the block ended (9.2): its writes carry that clock */
            handler(s, v);
            s->clk = 0;
        }   /* the block ends: the latched block begins, the interrupt loads the next */
    }
}

static void master_step(sound_t *s) {   /* 0x294AC (6.1) */
    if (!s->flag || s->level == s->target) return;
    if (s->level < s->target) { s->level += 0x4000; if (s->level > 0x3F0000) s->level = 0x3F0000; }
    else { s->level -= 0x4000; if (s->level < 0) s->level = 0; }
}

void sound_blank(sound_t *s) {   /* the order of 9.2: the game's ramp, then the sound server, then the blocks that end in the blank (their interrupts) */
    master_step(s);
    for (int v = 0; v < SND_VOICES; v++) if (s->v[v].slot != SND_NONE) server_voice(s, v);
    for (int v = 0; v < SND_VOICES; v++) hw_advance(s, v);
    s->blank++;
}

int sound_step_blanks(const sound_t *s) { return s->phase == 3 ? 7 : 6; }
#define step_blanks sound_step_blanks   /* floor(25 k / 4): 6, 6, 6, 7 blanks (note 10 9.3) */

/* Run the step's blanks up to (not including) blank b: a wrapper called there acts at that blank (the typed text's clicks, 8.3). A wrapper called later in the same step (a flight pass's, the screen's requests after the
 * clicks) is stamped at the blank the step has reached, never before the clicks that preceded it: the order is the original's [V: note 10 5.7 item 13, sound7-restart: the clicks first, the stop-all and the music restart
 * one blank after the last click]. The exact blank of such a wrapper is the pass cost model's (C, note 08 3.8); the text screens make clicks only, and a flight step has none, so the two meet only where a screen
 * schedules both itself (SHIFT-ESC from the air, screen.c shift_sound). */
void sound_run_to(sound_t *s, int blank) {
    const int n = step_blanks(s);
    if (blank > n) blank = n;
    while (s->blank < blank) sound_blank(s);
}

void sound_step(sound_t *s) {
    sound_run_to(s, step_blanks(s));
    s->blank = 0;
    s->phase = (uint8_t)((s->phase + 1) & 3);
}

/* ---- the master volume (section 6) ------------------------------------------------------------------------------------------------------------------------------ */
void sound_master(sound_t *s, int target, int set, int flag) {
    if (target >= 0) s->target = (int32_t)((uint32_t)(target > 63 ? 63 : target) << 16);
    if (set >= 0) { s->level = (int32_t)((uint32_t)(set > 63 ? 63 : set) << 16); s->master_set = (uint8_t)(set > 63 ? 63 : set); }
    if (flag >= 0) s->flag = (uint8_t)flag;
}
int sound_flag(const sound_t *s) { return s->flag; }
int sound_level(const sound_t *s) { return s->level >> 16; }
int sound_slot(const sound_t *s, int voice) { return s->v[voice].slot; }

/* ---- the wrappers (section 8; 3.2 to 3.5, 5.4, 5.6, 7) ----------------------------------------------------------------------------------------------------------- */
static void begin_script(sound_obj_t *o) { o->pos = 0; o->wait = 1; }

void sound_tone(sound_t *s, const int p[9]) {   /* 0x1A042 (3.2) */
    if (!s->o[4].exists) return;
    stop_raw(s, 3);
    int32_t (*t)[2] = s->script[SOUND_SCRIPT_TONE];
    t[0][1] = p[7];                         /* outer repeats: loop counter 0 */
    t[1][1] = p[6];                         /* inner repeats: loop counter 1 */
    t[2][1] = (int32_t)((uint32_t)p[0] << 16); t[3][1] = (int32_t)((uint32_t)p[1] << 16); t[4][1] = p[2];
    t[5][1] = (int32_t)((uint32_t)p[3] << 16); t[6][1] = (int32_t)((uint32_t)p[4] << 16); t[7][1] = p[5];
    t[9][1] = p[8];
    begin_script(&s->o[4]);
    sound_start(s, 4, 3, 0);
}

static int wrap_noise(sound_t *s, int a, int b) {   /* 0x1A0DC (3.4) */
    if (!(s->en12 & 1) || !s->o[6].exists) return 1;
    stop_raw(s, 2);
    int32_t (*t)[2] = s->script[SOUND_SCRIPT_NOISE];
    t[0][1] = a * 65536;
    t[1][1] = b != 0 ? -((a * 65536) / b) : 0;   /* the step: -(a << 16) / b toward 0. The guard b != 0 is the port's: the 68000 division would trap, and no caller passes a duration of 0 (the callers' b are 0x30, 0x78 and the explosion's d >= 1) */
    t[2][1] = b;
    const uint32_t r = sound_random(s, 11);
    s->o[6].period = (int32_t)((uint32_t)(NOISE_PERIOD + 4 * (int)r) << 16);
    begin_script(&s->o[6]);
    sound_start(s, 6, 2, 0);
    return 0;
}

static void wrap_gun(sound_t *s, int a, int b) {   /* 0x1A2F0 (3.5) */
    if (!s->o[0xA].exists) return;   /* the caller chooses between this and the noise wrapper by the enable bit (V:4AC13 bit 5) */
    stop_raw(s, 2);
    int32_t (*t)[2] = s->script[SOUND_SCRIPT_GUN];
    t[0][1] = a; t[1][1] = (int32_t)((uint32_t)b << 16);
    begin_script(&s->o[0xA]);
    sound_start(s, 0xA, 2, 0);
}

static void wrap_ground(sound_t *s, int v, int camera) {   /* 0x1A18C (3.5) */
    if (!(s->en13 & 4) || !s->o[5].exists || !camera) return;
    s->o[5].period = 310 << 16; s->o[5].pstep = -2 * 65536; s->o[5].count = 1;
    sound_start(s, 5, 2, v);
}

static void wrap_gear(sound_t *s, int v, int camera) {   /* 0x1A1E6 (3.5) */
    if (!(s->en13 & 0x40) || !s->o[0xB].exists || !camera) return;
    s->o[0xB].script = SND_SCRIPT_GEAR;
    memcpy(s->script[SND_SCRIPT_GEAR], SCRIPT_GEAR, sizeof SCRIPT_GEAR);
    begin_script(&s->o[0xB]);
    sound_start(s, 0xB, 2, v);
}

static void wrap_launch(sound_t *s, int v) {   /* 0x1A258 (3.5) */
    if (!(s->en12 & 2) || !s->o[0xC].exists) return;
    stop_raw(s, 2);
    int32_t (*t)[2] = s->script[SOUND_SCRIPT_LAUNCH];
    t[3][1] = v * 65536;
    t[4][1] = -(((v - 1) * 65536) / 60);   /* signed: a volume of 0 gives a rising step */
    const uint32_t r = sound_random(s, 3);
    s->o[0xC].period = (int32_t)((uint32_t)(LAUNCH_PERIOD + (int)r) << 16);
    begin_script(&s->o[0xC]);
    sound_start(s, 0xC, 2, 0);
}

static void clear_steps(sound_obj_t *o) { o->pstep = 0; o->vstep = 0; }

static void wrap_pair(sound_t *s, int a, int b, int pitch, int vol) {
    clear_steps(&s->o[a]); clear_steps(&s->o[b]);
    s->o[a].period = (int32_t)((uint32_t)pitch << 16);
    s->o[b].period = (int32_t)((uint32_t)(pitch + 2) << 16);   /* the detune of voice 1 */
    sound_start(s, a, 0, vol);
    sound_start(s, b, 1, vol);
}
static void wrap_engine_ext(sound_t *s, int pitch, int vol) {   /* 0x19E46 */
    if (!(s->en13 & 2) || !s->o[3].exists) return;
    wrap_pair(s, 2, 3, pitch, vol);
}
static void wrap_engine_cockpit(sound_t *s, int pitch, int vol) {   /* 0x19DB2 */
    if (!(s->en13 & 1) || !s->o[1].exists) { wrap_engine_ext(s, pitch, vol >> 2); return; }
    wrap_pair(s, 0, 1, pitch, vol);
}
void sound_glide(sound_t *s, int pitch, int volume, int ticks) {   /* 0x19EFA */
    /* The original tests voice 1 only (8.5). The two guards after it are the port's memory safety and are unreachable: the callers pass GLIDE_TICKS, and voices 0 and 1 are always started and stopped together
     * (a table in which the pair's second object is missing clears the enable bit, 2.3). */
    if (!(s->en13 & 2) || s->v[1].slot == SND_NONE || s->v[0].slot == SND_NONE || ticks <= 0) return;
    const sound_obj_t *o0 = &s->o[s->v[0].slot];
    const int32_t ps = (int32_t)((int64_t)((int32_t)((uint32_t)pitch << 16) - o0->period) / ticks);
    const int32_t vs = (int32_t)((int64_t)((int32_t)((uint32_t)volume << 16) - o0->volume) / ticks);
    for (int v = 0; v < 2; v++) {
        sound_obj_t *o = &s->o[s->v[v].slot];
        o->pstep = ps; o->vstep = vs; o->ptimer = o->vtimer = ticks;
    }
}
static void wrap_wind(sound_t *s, int vol) {   /* 0x19F9A (5.6) */
    if (!(s->en13 & 0x10) || !s->o[9].exists) { stop_raw(s, 0); stop_raw(s, 1); return; }
    const uint32_t r1 = sound_random(s, 6), r2 = sound_random(s, 6);
    s->o[8].period = (int32_t)((uint32_t)(WIND_PERIOD + (int)r1) << 16);
    s->o[9].period = (int32_t)((uint32_t)(WIND_PERIOD + (int)r2) << 16);
    sound_start(s, 8, 0, vol);
    sound_start(s, 9, 1, vol);
}

static void wrap_phrase(sound_t *s, int mission, int v) {   /* 0x19CE6, 0x19D7A (7.1) */
    if (mission) {
        if ((s->en12 & 4) && s->o[0x24].exists) { sound_start(s, 0x23, 0, v); sound_start(s, 0x24, 1, v); }
        return;
    }
    if (s->flag != 0) return;
    if (s->en13 & 0x80) {
        sound_stop_all(s);
        sound_start(s, 0xD, 0, 0x3F); sound_start(s, 0xE, 1, 0x3F);
        s->flag = 2;
    } else if ((s->en12 & 4) && s->o[0x24].exists) {
        sound_start(s, 0x23, 0, v); sound_start(s, 0x24, 1, v);
        s->flag = 2;
    } else {
        sound_stop_all(s);
    }
}

static void tone_n(sound_t *s, int v, int n) {   /* the click and the beeps (7.4): p1 = p4 = 0x12C, p3 = p6 = p7 = 1 */
    const int p[9] = { 0x12C, v, 1, 0x12C, v, 1, 1, n, n };
    sound_tone(s, p);
}

int sound_event(sound_t *s, sound_event_kind_t kind, int a, int b) {
    int r = 0;
    switch (kind) {
    case SND_NOISE: r = wrap_noise(s, a, b); break;
    case SND_GUN: wrap_gun(s, a, b); break;
    case SND_GROUND: wrap_ground(s, a, b); break;
    case SND_GEAR: wrap_gear(s, a, b); break;
    case SND_GEAR_STOP: stop_raw(s, 2); break;
    case SND_LAUNCH: wrap_launch(s, a); break;
    case SND_PHRASE: wrap_phrase(s, a, b); break;
    case SND_CLICK: if ((int8_t)s->cooldown <= 0) { const int p[9] = { 0x12C, a, 1, 0x12C, a, 1, 1, 1, 1 }; sound_tone(s, p); } break;
    case SND_BEEP_SWITCH: if (!a && (int8_t)s->cooldown <= 0) tone_n(s, s->flag ? 2 : 4, 2); break;   /* 0x37C4A: silent in an external view and during the cool-down; v = 2 while the flag is set */
    case SND_BEEP_N: if (!b) tone_n(s, 4, a); break;                                                    /* 0x37C32: not held back by the cool-down */
    case SND_BEEP_44: if ((int8_t)s->cooldown <= 0) tone_n(s, 2, 2); break;
    case SND_WIND: wrap_wind(s, a); break;
    case SND_ENGINE_COCKPIT: wrap_engine_cockpit(s, a, b); break;
    case SND_ENGINE_EXT: wrap_engine_ext(s, a, b); break;
    case SND_STOP_ALL: sound_stop_all(s); break;
    case SND_STOP_VOICE: if (a >= 0 && a < SND_VOICES) stop_raw(s, a); break;
    }
    return r;
}

/* ---- the per-pass routine 0x14AA0 (section 5) ----------------------------------------------------------------------------------------------------------------------- */
void sound_second(sound_t *s) {
    s->gear_cd &= 0x7F;
    if (s->gear_cd > 0) s->gear_cd--;
    if ((int8_t)s->cooldown > 0) s->cooldown--;
}
int sound_gear_key(sound_t *s) { if (!(s->en13 & 0x40) || !s->o[0xB].exists) return 0; s->gear_cd = 0x83; return 1; }
void sound_bang_request(sound_t *s, int amplitude, int duration, int countdown) {
    s->bang_amp = (uint8_t)amplitude; s->bang_dur = (uint8_t)duration; s->bang_cd = (uint8_t)countdown;
}

/* the cue player 0x14B5A (5.1 item 3; note 07 13.4): one cue per pass in this order, the whole long cleared afterwards */
static const struct { uint32_t bit; int p[9]; } CUES[] = {
    { 0x40,  { 0xA0, 0x10, 8, 0xC8, 0x10, 5, 3, 4, 4 } },
    { 0x80,  { 0x144, 0x0A, 8, 0xC8, 0x0A, 5, 3, 1, 4 } },
    { 0x01,  { 0x7C, 0x10, 8, 0xB4, 0x10, 5, 3, 3, 4 } },
    { 0x04,  { 0xE5, 3, 5, 0xE5, 3, 5, 1, 0, 1 } },
    { 0x100, { 0xC0, 4, 9, 0xC8, 0, 3, 1, 0, 1 } },
    { 0x10,  { 0x1F4, 0x0F, 5, 0x1F4, 0x0F, 5, 3, 1, 5 } },
    { 0x02,  { 0x1F4, 0x0A, 5, 0x82, 0, 1, 3, 3, 5 } },
    { 0x20,  { 0x190, 8, 5, 0x14A, 0, 1, 3, 2, 5 } },
    { 0x800, { 0x1F4, 0x0A, 5, 0x82, 0, 1, 3, 1, 5 } },
    { 0x08,  { 0 } } };

static void cue_player(sound_t *s, const sound_in_t *in) {
    const uint32_t bits = *in->cues;
    if ((int8_t)s->cooldown > 0 || !bits) return;   /* the cool-down holds the bits back */
    if (in->eject_nibble) {                         /* during an ejection every cue but 0x40 is discarded */
        if (bits & 0x40) sound_tone(s, CUES[0].p);
        *in->cues = 0;
        return;
    }
    for (unsigned i = 0; i < sizeof CUES / sizeof CUES[0]; i++) {
        if (!(bits & CUES[i].bit)) continue;
        if (CUES[i].bit == 0x08) sound_stop_voice(s, 3);
        else { sound_tone(s, CUES[i].p); if (CUES[i].bit == 0x01) s->cooldown = 3; }
        *in->cues = 0;
        return;
    }
    *in->cues = 0;   /* a long with only unplayed bits (0x200) is cleared too */
}

static int iabs(int x) { return x < 0 ? -x : x; }

/* the engine volume P0 of the throttle magnitude a0 (5.3) */
static int engine_p0(const sound_in_t *in, int a0, int hold) {
    int x;
    if (a0 == 0) return ((in->w2 & 0x80) && in->speed != 0) ? 0x0C : 0;
    if (hold) x = 63;
    else if (a0 > 8) x = 63 - ((120 - a0) >> 2);
    else x = 60 - ((120 - a0) >> 1);
    if (in->view == 0) { if (in->strain > 5) x >>= 1; return x; }
    if (in->v4a86d) {
        int d = in->ext_dir;
        if (d > 4) { d = 8 - d; if (d < 0) d = 0; }
        if (d == 0 || d == 1) x = (x >> 1) + (x >> 3);   /* a negative direction matches no case: x stays */
        else if (d == 2) x = x - (x >> 2);
        else if (d == 3) x = x - (x >> 3);
        switch ((int)(int8_t)in->view - 1) {
        case 0: x = x - (x >> 3); break;
        case 1: x = x - (x >> 2); break;
        case 2: x = (x >> 1) + (x >> 3); break;
        case 3: x = x >> 1; break;
        default: break;
        }
    } else if (in->cam_dist != 0) {
        x = 32 * x / in->cam_dist;
        if (x > 0x54) x = 0x54;
    }
    if (!(in->w2 & 8)) x = x - (x >> 2);
    if (x < 1) x = 1;
    return x;
}

static void engine_launch(sound_t *s, const sound_in_t *in, int pitch, int vol) {   /* 0x152C6 */
    if (s->en12 & 2) { sound_event(s, SND_LAUNCH, vol, 0); *in->hold = 0; }
    else sound_event(s, SND_ENGINE_EXT, pitch + 0x16, vol);
}
static void glide_b(sound_t *s, int pitch, int vol) {   /* 0x19EBE: the volume divided by 4 without the cockpit pair */
    if (!(s->en13 & 1)) vol >>= 2;
    sound_glide(s, pitch, vol, GLIDE_TICKS);
}

static void engine_block(sound_t *s, const sound_in_t *in, int n) {
    const int cockpit = in->view == 0;
    const int ab = (in->w2 & 8) != 0;
    const int hold = *in->hold;
    if ((in->cls & 0xF0) == 0x30) { sound_event(s, SND_WIND, 0x10, 0); return; }
    const int a0 = iabs(in->throttle);
    const int p0 = engine_p0(in, a0, hold);
    if (p0 == 0) {   /* the wind (5.6) */
        int w = iabs(in->speed) >> 9;
        if (w > 63) w = 63;
        if (cockpit) w >>= 1;
        if (w < 1) w = 1;
        sound_event(s, SND_WIND, w, 0);
        return;
    }
    int u = iabs(in->roll_rate >> 4);
    const int w2 = iabs(in->pitch_rate >> 4);
    if (u < w2) u = u + (in->pitch_rate >> 4);
    int a;
    if (a0 >= 120) a = a0 - u; else { a = a0 + u; if (a > 120) a = 120; }
    int b, p;
    if (cockpit) { b = 784 - 2 * a - (a >> 2); p = p0 < 0x28 ? 0x28 : p0; }
    else { b = 800 - 2 * a - (a >> 1) - (a >> 2); p = p0 - (p0 >> 2); }
    int v6 = 0x3D;
    if (!cockpit && !in->v4a86d && in->cam_dist != 0) { v6 = 0x3D * 32 / in->cam_dist; if (v6 > 0x3F) v6 = 0x3F; }
    if (!cockpit) {
        switch (n) {
        case 0xFE: sound_event(s, SND_ENGINE_EXT, b, p); break;
        case 0xFD: sound_event(s, SND_ENGINE_EXT, b + 0x20, v6); break;
        case 0xFC: sound_event(s, SND_ENGINE_EXT, b + 0x20, p); break;
        case 0xFB: engine_launch(s, in, b, v6); break;
        case 0xFA: sound_event(s, SND_ENGINE_EXT, b + 0x16, 2 * v6 > 0x3F ? 0x3F : 2 * v6); break;
        default: sound_glide(s, b, p, GLIDE_TICKS); break;
        }
        return;
    }
    switch (n) {
    case 0xFE: if (!ab) sound_event(s, SND_ENGINE_COCKPIT, b, p); else sound_event(s, SND_ENGINE_EXT, b, p >> 2); break;
    case 0xFD: sound_event(s, SND_ENGINE_EXT, b + 0x23, 0x1E); break;
    case 0xFC: sound_event(s, SND_ENGINE_COCKPIT, b + 0x20, p); break;
    case 0xFB: engine_launch(s, in, b, 0x3D); break;
    case 0xFA: sound_event(s, SND_ENGINE_EXT, b + 0x16, 0x28); break;
    default:
        if ((uint8_t)hold != 0) glide_b(s, b, (s->en13 & 1) ? 0x0A : 0x28);   /* H non-zero (any byte value) */
        else if (!ab || !(s->en13 & 1)) glide_b(s, b, p);
        else glide_b(s, b, (p >> 2) + (p >> 3));
        break;
    }
}

void sound_pass(sound_t *s, const sound_in_t *in) {
    if (in->v4a966 || in->v4a865 || in->v4a866 || !in->v4a84d) return;   /* 5.1 item 1 */
    const int cockpit = in->view == 0;
    const uint8_t notice = *in->notice;   /* the notice is read once, at the head of the routine: one posted by the bang below waits for the next pass */
    /* the gear countdown (5.1 item 2) */
    if (s->gear_cd & 0x80) {
        s->gear_cd &= 0x7F;
        if (cockpit) sound_event(s, SND_GEAR, 10, in->camera);
        else if (in->v4a86d) sound_event(s, SND_GEAR, 5, in->camera);
    } else if (s->gear_cd == 1) {
        sound_event(s, SND_GEAR_STOP, 0, 0);
        s->gear_cd = 0;
    }
    cue_player(s, in);
    /* the bang of an explosion or of the ejection (5.1 item 3): the countdown V:4A870 falls in every ungated pass and fires at 0 or below (signed) */
    if (s->bang_cd) {
        s->bang_cd--;
        if ((int8_t)s->bang_cd <= 0) {
            s->bang_cd = 0;
            const int amp = cockpit ? (int8_t)s->bang_amp >> 1 : (int8_t)s->bang_amp;   /* the bytes are sign-extended */
            if (sound_event(s, SND_NOISE, amp, (int8_t)s->bang_dur)) { *in->notice = 0xFA; *in->hold = 2; }
        }
    }
    /* the engine gate (5.1 item 4) */
    if (notice != 0 || (in->pass_n & 3) == 0) {
        engine_block(s, in, notice);
        *in->notice = 0;
    }
    /* the hold counter falls in every pass; at 0 in the cockpit with the afterburner off it asks for the normal engine level (5.1 item 5) */
    if ((int8_t)*in->hold > 0) {
        (*in->hold)--;
        if (*in->hold == 0 && cockpit && !(in->w2 & 8)) *in->notice = 0xFE;
    }
}

/* the enable bits of the loaders (2.3) from the objects that exist: a bit is set when everything its loader makes was built */
static void enable_bits(sound_t *s) {
    s->en12 = (uint8_t)((s->o[6].exists ? 1 : 0) | ((s->o[0xC].exists && s->o[2].exists) ? 2 : 0) | ((s->o[0x23].exists && s->o[0x24].exists) ? 4 : 0));
    s->en13 = (uint8_t)(((s->o[0].exists && s->o[1].exists) ? 1 : 0) | ((s->o[2].exists && s->o[3].exists) ? 2 : 0) | (s->o[5].exists ? 4 : 0) |
                        ((s->o[8].exists && s->o[9].exists) ? 0x10 : 0) | ((s->o[0xA].exists && s->o[6].exists) ? 0x20 : 0) | (s->o[0xB].exists ? 0x40 : 0));
    int music = 1;
    for (int i = 0xD; i <= 0x22; i++) if (!s->o[i].exists) music = 0;
    if (music) s->en13 |= 0x80;
}

/* ---- set-up and the buffers ------------------------------------------------------------------------------------------------------------------------------------------ */
void sound_init(sound_t *s, const sound_table_t *tab) {
    memset(s, 0, sizeof *s);
    if (tab) s->tab = *tab;
    else for (int i = 0; i < SND_OBJECTS; i++) s->tab.obj[i].kind = SOUND_NONE;
    s->rnd = SEED;
    for (int i = 0; i < SND_NOISE_BYTES; i++) s->noise[i] = (int8_t)(uint8_t)sound_random(s, 8);   /* the first 16,384 bits from the seed (3.3) */
    for (int i = 0; i < SND_TONE_BYTES; i++) s->tone[i] = i < 16 ? 127 : -126;                       /* 3.2 */
    for (int i = 0; i < SND_OBJECTS; i++) {
        sound_obj_t *o = &s->o[i];
        o->exists = s->tab.obj[i].kind != SOUND_NONE;
        o->length = s->tab.obj[i].length;
        o->period = (int32_t)((uint32_t)s->tab.obj[i].period << 16);
        o->volume = (int32_t)((uint32_t)s->tab.obj[i].volume << 16);
        o->count = s->tab.obj[i].count; o->reload = s->tab.obj[i].reload; o->next = s->tab.obj[i].next; o->script = s->tab.obj[i].script;
    }
    memcpy(s->script[SOUND_SCRIPT_TONE], SCRIPT_TONE, sizeof SCRIPT_TONE);
    memcpy(s->script[SOUND_SCRIPT_NOISE], SCRIPT_NOISE, sizeof SCRIPT_NOISE);
    memcpy(s->script[SOUND_SCRIPT_GUN], SCRIPT_GUN, sizeof SCRIPT_GUN);
    memcpy(s->script[SOUND_SCRIPT_LAUNCH], SCRIPT_LAUNCH, sizeof SCRIPT_LAUNCH);
    memcpy(s->script[SND_SCRIPT_GEAR], SCRIPT_GEAR, sizeof SCRIPT_GEAR);
    for (int v = 0; v < SND_VOICES; v++) {
        s->v[v].slot = s->v[v].cur = s->v[v].pend = SND_NONE;
    }
    enable_bits(s);
    s->level = s->target = 0x3F0000;
    s->master_set = 0xFF;
}

void sound_objects_available(sound_t *s, const uint8_t present[SND_OBJECTS]) {
    const sound_table_t tab = s->tab;
    sound_init(s, &tab);                                   /* before the first pass: the session starts again with the objects that could be built */
    for (int i = 0; i < SND_OBJECTS; i++) if (!present[i]) s->o[i].exists = 0;
    enable_bits(s);
}

int sound_buffer(const sound_t *s, int object, const int8_t **data, uint32_t *len) {
    if (object == 4 && s->o[4].exists) { *data = s->tone; *len = SND_TONE_BYTES; return 1; }
    if ((object == 6 || object == 0xA) && s->o[object].exists) { *data = s->noise; *len = SND_NOISE_BYTES; return 1; }
    return 0;
}

void sound_drain(sound_t *s, game_audio_cmd_t *out, int max, int *count, uint8_t *master_target, uint8_t *master_set, uint8_t *master_ramp) {
    const int n = s->ncmd < max ? s->ncmd : max;
    if (out && n > 0) memcpy(out, s->cmd, (size_t)n * sizeof s->cmd[0]);
    if (count) *count = n;
    if (master_target) *master_target = (uint8_t)(s->target >> 16);
    if (master_set) *master_set = s->master_set;
    if (master_ramp) *master_ramp = s->flag;
    s->ncmd = 0; s->master_set = 0xFF;
}
