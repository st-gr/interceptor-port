/* src/frontend/sdl/audio.c - the Paula-like mixer; the rules are in docs/re/10-sound.md and cited per function. */
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QUEUE_MAX (1 << 20)     /* register writes waiting for their time: far beyond any run (a step is at most 1,024); a queue that long is reported, never silently cut */
#define INF_TIME INT64_MAX
#define MIN_PERIOD 124          /* the hardware minimum (note 10 section 1.4) */
#define PI 3.14159265358979323846

typedef struct { double a1, a2, tl, tr; } onepole_t;
typedef struct { double a1, a2, b1, b2, xl[2], xr[2], yl[2], yr[2]; } twopole_t;

/* One voice as the hardware has it (note 10 1.4): the latched block (AUDxLC, AUDxLEN: an object and a byte range), the DMA bit, the block playing, the period and volume registers, and the period
 * counter: a sample lasts the period register's value AT THE MOMENT IT STARTS (the counter is reloaded at each sample's end, 9.2), so a period write does not rescale the sample in progress.
 * Times are in units of one colour clock / rate (so that a colour clock, a sample of the device and a blank are all whole numbers): a sample of period p lasts p x rate units, an output frame
 * advances clock_hz units, a blank is 71,051 x rate. */
typedef struct {
    int       dma;
    int       latch_obj;        /* -1: nothing valid latched (a rejected latch, or none yet) */
    uint32_t  latch_off, latch_len;
    int       active;           /* a block is playing */
    int       stopping;         /* DMA went off: the voice ends with the sample in progress */
    int       need_load;        /* a sample begins at load_t but its period is not read yet (the writes of the same instant come first) */
    const int8_t *data;         /* the object's buffer */
    uint32_t  blk_off, blk_len; /* the block now playing */
    uint32_t  idx;              /* the sample in progress */
    int64_t   edge;             /* the time the sample in progress ends (INF_TIME when none) */
    int64_t   load_t;
    uint16_t  period;
    uint8_t   volume;
} voice_t;

typedef struct { game_audio_cmd_t c; int64_t when; } qcmd_t;

struct audio {
    int rate, channels, filter, live;
    uint32_t clock_hz;          /* the colour clocks of a second of output time (the machine's, or the port's 50 blanks a second) */
    struct { const int8_t *data; uint32_t len; } obj[AUDIO_OBJECTS];
    uint8_t   reported[AUDIO_OBJECTS];
    voice_t   v[4];
    qcmd_t    *q;               /* the writes waiting, sorted by time (stable); head .. tail */
    size_t    qhead, qtail, qcap;
    int       qdrop_reported, resync_reported;
    int64_t   t;                /* the output time of the next frame to render */
    int64_t   tcur;             /* the time of the last event processed (a late write acts at this time) */
    int64_t   hs;               /* the lead smoothed over the last steps (the callbacks and the frames make it jitter by a blank or so) */
    int32_t   dm;               /* the live clock trim, in 1/65536: the frames advance clock_hz x (1 + dm / 65536) units */
    int64_t   off;              /* live: the mapping of the game's time to the output time (game clock x rate + off) */
    int64_t   send;             /* live: the end of the stream received so far in the voices' time: the end of the last blank of the last step applied (the voices never run past it, rule (c)) */
    int       anchored;
    uint64_t  steps;            /* simulation steps applied */
    audio_stats_t st;
    onepole_t lo, hi;
    twopole_t led;
};

/* The A500 chain as vAmiga builds it (Core/Components/Paula/Audio/AudioFilter.cpp, setupLoFilter / setupLedFilter / setupHiFilter, and Core/Ports/AudioPort.cpp
 * lines 476-478: lo, then LED, then hi, each on the output sample at the device rate):
 *   lo  : one-pole low-pass, R 360 ohm, C 100 nF -> cut-off 1 / (2 pi R C) = 4,420.97 Hz            (OnePoleFilter::setup(rate, 360.0, 1e-7))
 *   LED : two-pole low-pass, R1 = R2 = 10 kohm, C1 = 6.8 nF, C2 = 3.9 nF -> cut-off 1 / (2 pi sqrt(R1 R2 C1 C2)) = 3,090.5 Hz,
 *         Q = sqrt(R1 R2 C1 C2) / (C2 (R1 + R2)) = 0.6602 (TwoPoleFilter::setup(rate, 10000, 10000, 6.8e-9, 3.9e-9)); on the A500 only with the power LED on
 *   hi  : one-pole high-pass, R 1390 ohm, C 22.33 uF -> cut-off 5.13 Hz (OnePoleFilter::setup(rate, 1390.0, 2.233e-5)); the A500 variant (A1200 uses 1360 / 22 uF)
 * The note's "4.5 kHz and 3.3 kHz" (10 section 1.5) are round figures; these are the constants the reference emulator computes. */
static void onepole_setup(onepole_t *f, double rate, double cutoff) {
    if (cutoff >= rate / 2.0) cutoff = rate / 2.0 - 1e-4;
    double a = 2.0 - cos(2.0 * PI * cutoff / rate);
    double b = a - sqrt(a * a - 1.0);
    f->a1 = 1.0 - b; f->a2 = b; f->tl = f->tr = 0.0;
}
static void twopole_setup(twopole_t *f, double rate, double r1, double r2, double c1, double c2) {
    double cutoff = 1.0 / (2.0 * PI * sqrt(r1 * r2 * c1 * c2));
    double q = sqrt(r1 * r2 * c1 * c2) / (c2 * (r1 + r2));
    if (cutoff >= rate / 2.0) cutoff = rate / 2.0 - 1e-4;
    double a = 1.0 / tan(2.0 * PI * cutoff / rate), b = 1.0 / q;
    f->a1 = 1.0 / (1.0 + b * a + a * a);
    f->a2 = 2.0 * f->a1;
    f->b1 = 2.0 * (1.0 - a * a) * f->a1;
    f->b2 = (1.0 - b * a + a * a) * f->a1;
    memset(f->xl, 0, sizeof f->xl); memset(f->xr, 0, sizeof f->xr); memset(f->yl, 0, sizeof f->yl); memset(f->yr, 0, sizeof f->yr);
}
static void filters_setup(audio_t *a) {
    double r = a->rate;
    onepole_setup(&a->lo, r, 1.0 / (2.0 * PI * 360.0 * 1e-7));
    twopole_setup(&a->led, r, 10000.0, 10000.0, 6.8e-9, 3.9e-9);
    onepole_setup(&a->hi, r, 1.0 / (2.0 * PI * 1390.0 * 2.233e-5));
}
static void lp1(onepole_t *f, double *l, double *r) {
    f->tl = f->a1 * *l + f->a2 * f->tl; *l = f->tl;
    f->tr = f->a1 * *r + f->a2 * f->tr; *r = f->tr;
}
static void hp1(onepole_t *f, double *l, double *r) {
    f->tl = f->a1 * *l + f->a2 * f->tl; *l -= f->tl;
    f->tr = f->a1 * *r + f->a2 * f->tr; *r -= f->tr;
}
static void lp2(twopole_t *f, double *l, double *r) {
    double il = *l, ir = *r;
    *l = f->a1 * il + f->a2 * f->xl[0] + f->a1 * f->xl[1] - f->b1 * f->yl[0] - f->b2 * f->yl[1];
    *r = f->a1 * ir + f->a2 * f->xr[0] + f->a1 * f->xr[1] - f->b1 * f->yr[0] - f->b2 * f->yr[1];
    f->xl[1] = f->xl[0]; f->xl[0] = il; f->yl[1] = f->yl[0]; f->yl[0] = *l;
    f->xr[1] = f->xr[0]; f->xr[0] = ir; f->yr[1] = f->yr[0]; f->yr[0] = *r;
}

audio_t *audio_create(int rate_hz, int channels) {
    if (rate_hz < 8000 || rate_hz > 384000 || (channels != 1 && channels != 2)) return NULL;
    audio_t *a = (audio_t *)calloc(1, sizeof *a);
    if (!a) return NULL;
    a->rate = rate_hz; a->channels = channels; a->filter = 1; a->clock_hz = AUDIO_PAL_CLOCK;
    for (int i = 0; i < 4; i++) { a->v[i].latch_obj = -1; a->v[i].edge = INF_TIME; }
    filters_setup(a);
    return a;
}
void audio_destroy(audio_t *a) { if (a) free(a->q); free(a); }
void audio_set_filter(audio_t *a, int on) {
    if (!a) return;
    if (on && !a->filter) filters_setup(a);     /* start from rest */
    a->filter = on != 0;
}
void audio_set_clock(audio_t *a, uint32_t hz) { if (a && hz >= 1000000u && hz <= 8000000u) a->clock_hz = hz; }
void audio_set_live(audio_t *a, int on) { if (a) a->live = on != 0; }
audio_stats_t audio_stats(const audio_t *a) {
    audio_stats_t z; memset(&z, 0, sizeof z);
    if (!a) return z;
    z = a->st; z.pending = (uint32_t)(a->qtail - a->qhead); z.trim = a->dm;
    return z;
}
void audio_set_sample(audio_t *a, int object, const int8_t *data, uint32_t len) {
    if (!a || object < 0 || object >= AUDIO_OBJECTS) return;
    a->obj[object].data = len ? data : NULL;
    a->obj[object].len = data ? len : 0;
}

static void run_until(audio_t *a, int64_t T);

/* ---- the clock of the mixer ----------------------------------------------------------------------------------------------------------------------------------- */
#define BLANK_U(a) ((int64_t)AUDIO_BLANK_CLOCKS * (a)->rate)          /* one blank in units */
#define STEP_U(a) (BLANK_U(a) * 25 / 4)                                /* a simulation step: 6.25 blanks */
#define LEAD_BLANKS 3                                                  /* the core's lead over the output: 2 to 3 blanks, about 60 ms (note 10 9.2 live-play rules (a)) */
#define MERGE_BLANKS 5                                                 /* a step that begins more than this far ahead of the output skips whole blanks of output down to the lead (note 10 9.2 rule (d): 5, not 4, because the jitter of the callbacks and of the frames is up to +-1 around the lead of 3) */
#define TRIM_MAX 327                                                   /* the live trim is at most 0.5 % (327 / 65536) */

/* Queues the writes of one step. The game's time of a write is (blank of the step's first + the write's blank) x 71,051 + its clock; the mixer time of it is that x rate + off. In the grid
 * mode (--wav, the tests) off is 0: the mixer's time is the game's. Live, the first step is placed 3 blanks (about 60 ms) ahead of the output time and the later steps keep that mapping (note 10 9.2 (a)),
 * re-anchored after a stall (9.2 (c)); when a step begins more than 5 blanks ahead the mixer skips whole blanks of output, running every write and block end in order, never merging (9.2 (d)); the trim is reset. */
void audio_apply(audio_t *a, const game_audio_cmd_t *cmds, int count) {
    if (!a) return;
    const int64_t g0 = (int64_t)(a->steps * 25 / 4) * (int64_t)AUDIO_BLANK_CLOCKS;     /* step k covers the blanks floor(25 k / 4) to floor(25 (k + 1) / 4) - 1 (note 10 9.3) */
    a->steps++;
    if (a->live) {
        const int64_t lead = LEAD_BLANKS * BLANK_U(a);
        const int64_t start = (int64_t)(a->steps - 1) * 25 * AUDIO_BLANK_CLOCKS * a->rate / 4;      /* where the wall clock puts the step (k x 6.25 blanks): the writes' own blanks are floor(25 k / 4) (0 to 0.75 blank earlier) */
        if (!a->anchored) { a->off = a->t + lead - start; a->anchored = 1; a->hs = lead; }
        else {
            const int64_t h = start + a->off - a->t;                       /* how far ahead of the output the step begins */
            a->st.lead_clocks = h / a->rate;
            if (h < 0 || a->t > a->send) {                                /* an underrun: the output ran past the end of the stream before the next step came. The voices stood still at the end of the stream (the output held its sample, rule (c)); the clock is anchored again, not stretched */
                if (!a->resync_reported) { fprintf(stderr, "audio: the game fell behind the sound device (underrun), re-anchoring the clock\n"); a->resync_reported = 1; }
                run_until(a, a->send);                                    /* what the stream still held up to its end */
                int64_t d = a->t + lead - a->send;                        /* the voices, the writes waiting and the offset move together: the step continues from the held position, the lead restored */
                if (d < 0) d = 0;
                for (int i = 0; i < 4; i++) {
                    voice_t *v = &a->v[i];
                    if (v->edge != INF_TIME) v->edge += d;
                    if (v->need_load) v->load_t += d;
                }
                for (size_t k = a->qhead; k < a->qtail; k++) a->q[k].when += d;
                a->tcur += d; a->off += d; a->send += d;
                a->st.underruns++; a->dm = 0; a->hs = lead;
            } else if (h > MERGE_BLANKS * BLANK_U(a)) {                   /* the game got ahead: whole blanks of output are skipped, every write and block end still applied in order, none merged (note 10 9.2 (d)) */
                const int64_t nb = (h - lead) / BLANK_U(a);
                if (nb > 0) { run_until(a, a->t + nb * BLANK_U(a)); a->t += nb * BLANK_U(a); a->st.merged_blanks += (uint32_t)nb; }
                a->dm = 0; a->hs = lead;
            } else {                                                      /* the steady drift of the two clocks (the machine's 49.92 and the port's 50 blanks, a device's rate): a small trim of the mixer's clock */
                a->hs += (h - a->hs) / 8;
                const int64_t d = (a->hs - lead) * 65536 / (32 * STEP_U(a));
                a->dm = (int32_t)(d > TRIM_MAX ? TRIM_MAX : d < -TRIM_MAX ? -TRIM_MAX : d);
            }
        }
    }
    for (int i = 0; i < count && cmds; i++) {
        if (a->qtail == a->qcap) {
            if (a->qhead > 0) { memmove(a->q, a->q + a->qhead, (a->qtail - a->qhead) * sizeof a->q[0]); a->qtail -= a->qhead; a->qhead = 0; }
            if (a->qtail == a->qcap) {
                size_t nc = a->qcap ? a->qcap * 2 : 1024;
                qcmd_t *nq = nc <= QUEUE_MAX ? (qcmd_t *)realloc(a->q, nc * sizeof *nq) : NULL;
                if (!nq) {
                    if (!a->qdrop_reported) { fprintf(stderr, "audio: command queue full, commands dropped (render is not being called)\n"); a->qdrop_reported = 1; }
                    return;
                }
                a->q = nq; a->qcap = nc;
            }
        }
        qcmd_t e;
        e.c = cmds[i];
        e.when = (g0 + (int64_t)cmds[i].blank * AUDIO_BLANK_CLOCKS + cmds[i].clock) * a->rate + (a->live ? a->off : 0);
        size_t k = a->qtail++;                 /* stable insertion by time: the voices' block-end writes lie inside the blank, after the later voices' writes of its start */
        while (k > a->qhead && a->q[k - 1].when > e.when) { a->q[k] = a->q[k - 1]; k--; }
        a->q[k] = e;
    }
    a->send = (int64_t)(a->steps * 25 / 4) * (int64_t)AUDIO_BLANK_CLOCKS * a->rate + (a->live ? a->off : 0);
}

/* ---- the voices ------------------------------------------------------------------------------------------------------------------------------------------------ */
static void report_once(audio_t *a, const game_audio_cmd_t *c, const char *why) {
    if (c->object < AUDIO_OBJECTS) { if (a->reported[c->object]) return; a->reported[c->object] = 1; }
    fprintf(stderr, "audio: LATCH of object 0x%X ignored (%s)\n", c->object, why);
}

/* a block begins at time t with its first sample: from the first byte of the latched block, or nothing when no valid block is latched. The sample's length is read at t after the writes of that instant. */
static void begin_block(voice_t *v, const audio_t *a, int64_t t) {
    v->stopping = 0; v->edge = INF_TIME; v->need_load = 0;
    if (v->latch_obj < 0) { v->active = 0; return; }
    v->data = a->obj[v->latch_obj].data;
    v->blk_off = v->latch_off; v->blk_len = v->latch_len;
    v->idx = 0;
    v->active = 1;
    v->need_load = 1; v->load_t = t;
}

static void do_cmd(audio_t *a, const game_audio_cmd_t *c, int64_t t) {
    if (c->voice > 3) return;
    voice_t *v = &a->v[c->voice];
    switch (c->op) {
    case GAME_AUD_LATCH:    /* AUDxLC and AUDxLEN: the block the voice plays next (note 10 4.2) */
        v->latch_obj = -1;
        if (c->object >= AUDIO_OBJECTS) { report_once(a, c, "no such object"); break; }
        if (!a->obj[c->object].data) { report_once(a, c, "sample missing"); break; }
        if (c->length == 0 || (uint64_t)c->offset + c->length > a->obj[c->object].len) { report_once(a, c, "range past the sample"); break; }
        v->latch_obj = c->object; v->latch_off = c->offset; v->latch_len = c->length;
        break;
    case GAME_AUD_DMA_ON: if (!v->dma) { v->dma = 1; begin_block(v, a, t); } break;
    case GAME_AUD_DMA_OFF:  /* the voice stops at the end of the sample in progress */
        if (v->dma) {
            v->dma = 0;
            if (v->active && v->need_load) { v->active = 0; v->need_load = 0; }     /* the sample had not begun */
            else if (v->active) v->stopping = 1;
        }
        break;
    case GAME_AUD_PERIOD: v->period = c->value; break;      /* read at the next sample's start (the hardware reloads the counter at each sample's end) */
    case GAME_AUD_VOLUME: v->volume = c->value > 64 ? 64 : (uint8_t)c->value; break;
    default: break;
    }
}

/* the sample in progress ends at t: the next sample of the block, or at the block's end the block latched by now (note 10 4.2) */
static void sample_end(audio_t *a, voice_t *v, int64_t t) {
    v->edge = INF_TIME;
    if (v->stopping) { v->active = 0; v->stopping = 0; return; }
    if (++v->idx >= v->blk_len) {
        if (v->dma && v->latch_obj >= 0) { begin_block(v, a, t); return; }
        v->active = 0; return;
    }
    v->need_load = 1; v->load_t = t;
}

/* Everything that happens up to the time T, in time order; at the same time first the samples that end (a block end restarts the block latched BEFORE the writes of the instant: the interrupt
 * handler's writes load the block after it), then the register writes in the order given, then the period reads of the samples that begin. */
static void run_until(audio_t *a, int64_t T) {
    for (;;) {
        int64_t tb = INF_TIME, tl = INF_TIME, tc = a->qhead < a->qtail ? a->q[a->qhead].when : INF_TIME;
        int vb = -1, vl = -1;
        for (int i = 0; i < 4; i++) {
            const voice_t *v = &a->v[i];
            if (v->active && !v->need_load && v->edge < tb) { tb = v->edge; vb = i; }
            if (v->active && v->need_load && v->load_t < tl) { tl = v->load_t; vl = i; }
        }
        if (vb >= 0 && tb <= T && tb <= tc && tb <= tl) { if (tb > a->tcur) a->tcur = tb; sample_end(a, &a->v[vb], tb); }
        else if (tc <= T && tc <= tl) {
            const int64_t te = tc < a->tcur ? a->tcur : tc;
            a->tcur = te;
            do_cmd(a, &a->q[a->qhead].c, te);
            a->qhead++;
        } else if (vl >= 0 && tl <= T) {
            voice_t *v = &a->v[vl];
            const int64_t p = v->period < MIN_PERIOD ? MIN_PERIOD : v->period;
            if (tl > a->tcur) a->tcur = tl;
            v->need_load = 0; v->edge = tl + p * a->rate;
        } else break;
    }
    if (a->qhead == a->qtail) a->qhead = a->qtail = 0;
}

static inline int voice_out(const voice_t *v) {
    if (!v->active) return 0;
    return v->data[v->blk_off + v->idx] * (int)v->volume * 2;        /* full scale 128 x 64 x 2 = 16,384 per voice: two voices a side never clip */
}

static int16_t clip16(double x) {
    long r = lrint(x);
    return (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
}

void audio_render(audio_t *a, int16_t *out, int frames) {
    if (!a || !out) return;
    const int64_t adv = (int64_t)a->clock_hz * (65536 + a->dm) / 65536;
    for (int i = 0; i < frames; i++, a->t += adv) {
        /* the writes and the sample ends up to this frame's start; live, not past the end of the stream received: while the game is late the voices stand still and the output holds its last sample */
        run_until(a, a->live && a->anchored && a->send < a->t ? a->send : a->t);
        int l = voice_out(&a->v[0]) + voice_out(&a->v[3]);
        int r = voice_out(&a->v[1]) + voice_out(&a->v[2]);
        int16_t ol, orr;
        if (a->filter) {
            double dl = l, dr = r;
            lp1(&a->lo, &dl, &dr);
            lp2(&a->led, &dl, &dr);
            hp1(&a->hi, &dl, &dr);
            ol = clip16(dl); orr = clip16(dr);
        } else { ol = (int16_t)l; orr = (int16_t)r; }
        if (a->channels == 1) out[i] = (int16_t)(((int)ol + (int)orr) >> 1);
        else { out[2 * i] = ol; out[2 * i + 1] = orr; }
    }
}
