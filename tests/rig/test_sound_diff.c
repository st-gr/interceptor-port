/* tests/rig/test_sound_diff.c - differential test: the port's sound module (src/core/sound.c, docs/re/10-sound.md) against the original's sound library and per-pass routine in the
 * Musashi rig, with the custom-register write log as the witness (rig_io_log_*).
 *
 * The library (the player 0x19C7C, the per-blank server 0x551D8, the audio interrupt handler 0x55158, the wrappers, the loaders) runs on the real image with the operating system's
 * calls replaced by hooks (an allocator, the file routines reading the extracted sample files); the rig delivers the audio interrupts, and this harness plays the part of Paula: it
 * follows the location, length, period, volume and DMA writes of the log and raises a voice's interrupt when its block ends (sample by sample at the period register's value, 71,051 clocks a blank,
 * after the blank's ramp and server: note 10 9.2).
 * The port runs the same operations; after every blank the objects' records, the voice slots, the hardware state, the register values and the commands the port issued (replayed into a
 * shadow mixer) are compared with the rig.
 *
 * Parts:
 *   tone    the tone routine 0x1A042 with the ten cue parameter sets, the click and beep sets and 200 random sets: the 32-byte buffer against sound_buffer, then every blank until the voice stops
 *   blank   the server and the interrupt handler over random object states: random starts, stops, field pokes (steps, timers, counts), the master ramp, 1,000 blanks
 *   music   the start-up music: the block chain of the objects 0xD to 0x22 and two restarts after a stop in the loop (the counts persist), 4,000 blanks
 *   events  every wrapper of note 10 section 8 with random arguments and random enable bits, 60 blanks after each
 *   start   the start-up phrase of a disk without the music (0x19D7A(0x0F), the flag stays 0) and the passes of a flight start: the gated approach with its pending notice, the first ungated pass (outside view:
 *           776/778 at volume 12), the pass after the view change (764/766 at 40), the retargets
 *   pass    2,000 random states through the per-pass routine 0x14AA0 (the viewed record, the gates, the notice, the gear countdown, the cue long, the ejection countdown), six blanks after each
 * Skipped (exit 0) when extracted/interceptor.bin or the extracted assets (version 25) are absent. Run from tools/m68k-rig (make test-sound-core).
 * Environment: SOUND_DIFF_PART (tone, blank, music, events, pass), SOUND_DIFF_N (passes, default 2000), SOUND_DIFF_SEED, SOUND_DIFF_VERBOSE. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/m68k-rig/rig.h"
#include "../../src/core/sound.h"
#include "../../src/core/assets.h"

#define BASE 0x10000u
#define V_LO 0x4A6E8u
#define V_HI 0x54EC0u
#define TABLE 0x5591Cu
#define SLOTS 0x54ED0u
#define HEAP 0x200000u
#define LEVEL 0x54FBEu

static uint32_t rng = 0x2545F491u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int rnd(int lo, int hi) { return lo + (int)(xr() % (uint32_t)(hi - lo + 1)); }
static int pct(int p) { return rnd(0, 99) < p; }

static int diffs, cases, verbose;
static const char *image_path, *assets_dir = "../../assets";

/* ---- rig access ------------------------------------------------------------------------------------------------------------------------------ */
static uint32_t be32(const uint8_t *b) { return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]; }
static void put8(uint32_t a, int v) { uint8_t b = (uint8_t)v; rig_poke(a, &b, 1); }
static void put16(uint32_t a, int v) { uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v }; rig_poke(a, b, 2); }
static void put32(uint32_t a, uint32_t v) { uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; rig_poke(a, b, 4); }
static int get8(uint32_t a) { uint8_t b; rig_peek(a, &b, 1); return b; }
static uint32_t get32(uint32_t a) { uint8_t b[4]; rig_peek(a, b, 4); return be32(b); }

static void zero_regs(void) { for (int i = 0; i < 16; i++) rig_set_reg((rig_reg_t)i, 0); }
static int run_at(uint32_t addr) { zero_regs(); return rig_run(addr); }
static int call(uint32_t addr, int n, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8) {
    const uint32_t args[9] = { a0, a1, a2, a3, a4, a5, a6, a7, a8 };
    zero_regs();
    rig_call(addr, args, n);
    return rig_last_error;
}

/* ---- the operating-system hooks of the library --------------------------------------------------------------------------------------------- */
static uint32_t heap;
static FILE *cur_file;
static uint32_t arg(int i) { uint8_t b[4]; rig_peek(rig_get_reg(RIG_A7) + 4 + 4 * (uint32_t)i, b, 4); return be32(b); }
static void cstr(uint32_t a, char *out, int max) { for (int i = 0; i < max - 1; i++) { uint8_t c; rig_peek(a + (uint32_t)i, &c, 1); out[i] = (char)c; out[i + 1] = 0; if (!c) return; } }
static void sample_path(const char *name, char *out, size_t n) {
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    snprintf(out, n, "%s/sounds/%s.raw", assets_dir, base);
}
static void h_alloc(void) {
    static uint8_t zero[4096];
    uint32_t size = arg(0);
    heap = (heap + 7) & ~7u;
    const uint32_t p = heap;
    heap += size;
    for (uint32_t o = 0; o < size; o += sizeof zero) rig_poke(p + o, zero, size - o < sizeof zero ? size - o : sizeof zero);
    rig_hook_result(p);
}
static void h_nop(void) {}
static void h_size(void) {
    char name[64], path[256];
    cstr(arg(0), name, sizeof name); sample_path(name, path, sizeof path);
    FILE *f = fopen(path, "rb");
    long n = 0;
    if (f) { fseek(f, 0, SEEK_END); n = ftell(f); fclose(f); }
    rig_hook_result((uint32_t)n);
}
static void h_fopen(void) {
    char name[64], path[256];
    cstr(arg(0), name, sizeof name); sample_path(name, path, sizeof path);
    if (cur_file) fclose(cur_file);
    cur_file = fopen(path, "rb");
    rig_hook_result(cur_file ? 1 : 0);
}
static void h_fread(void) {
    const uint32_t ptr = arg(0), size = arg(1), count = arg(2);
    uint8_t *buf = (uint8_t *)malloc((size_t)size * count + 1);
    size_t n = cur_file ? fread(buf, 1, (size_t)size * count, cur_file) : 0;
    rig_poke(ptr, buf, n);
    free(buf);
    rig_hook_result(count);
}

/* ---- the register log and the harness's Paula ------------------------------------------------------------------------------------------------- */
typedef struct { int dma; uint32_t pend_lc, pend_len, cur_lc, cur_len, old_lc, old_len; int per, vol, wrote_per, wrote_vol, lc_pending; long cnt, left; } hwv_t;   /* cnt: colour clocks left of the sample in progress; left: samples left in the block */
static hwv_t hw[4];

/* the register writes of the log as events (voice, kind, value), without the writes that change nothing; the port's commands are converted to the same list and the two compared in order */
enum { EV_LC = 1, EV_LEN, EV_DMA_ON, EV_DMA_OFF, EV_PER, EV_VOL };
typedef struct { int voice, kind; uint32_t value, clock; } ev_t;   /* clock: the colour clock within the blank at which the write was made (the interrupt of a block end: where the block ended; the server and the wrappers: 0) */
static uint32_t rig_clock;     /* the harness's own position in the blank while the audio interrupt runs */
#define EV_MAX 4096
static ev_t rig_ev[EV_MAX], port_ev[EV_MAX];
static int n_rig_ev, n_port_ev;
static void rig_add(int voice, int kind, uint32_t value) { if (n_rig_ev < EV_MAX) { rig_ev[n_rig_ev].voice = voice; rig_ev[n_rig_ev].kind = kind; rig_ev[n_rig_ev].value = value; rig_ev[n_rig_ev].clock = rig_clock; n_rig_ev++; } }

static void hw_apply(const uint32_t *e, size_t n) {
    for (size_t i = 0; i < n; i++) {
        const uint32_t reg = e[4 * i + 1] & 0xFFFF, size = e[4 * i + 1] >> 16, val = e[4 * i + 2];
        if (reg >= 0xA0 && reg < 0xE0) {
            const int ch = (int)(reg - 0xA0) >> 4, off = (int)(reg - 0xA0) & 15;
            if (off == 0 && size == 4) { hw[ch].old_lc = hw[ch].pend_lc; hw[ch].old_len = hw[ch].pend_len; hw[ch].pend_lc = val; hw[ch].lc_pending = 1; }
            else if (off == 0) hw[ch].pend_lc = (hw[ch].pend_lc & 0xFFFF) | (val << 16);
            else if (off == 2) hw[ch].pend_lc = (hw[ch].pend_lc & 0xFFFF0000u) | (val & 0xFFFF);
            else if (off == 4) {
                hw[ch].pend_len = val & 0xFFFF;
                if (hw[ch].lc_pending) { hw[ch].lc_pending = 0; if (hw[ch].pend_lc != hw[ch].old_lc || hw[ch].pend_len != hw[ch].old_len) { rig_add(ch, EV_LC, hw[ch].pend_lc); rig_add(ch, EV_LEN, hw[ch].pend_len); } }
                else rig_add(ch, EV_LEN, val & 0xFFFF);
            }
            else if (off == 6) { if ((int)(val & 0xFFFF) != hw[ch].per) rig_add(ch, EV_PER, val & 0xFFFF); hw[ch].per = (int)(val & 0xFFFF); hw[ch].wrote_per = 1; }
            else if (off == 8) { if ((int)(val & 0xFFFF) != hw[ch].vol) rig_add(ch, EV_VOL, val & 0xFFFF); hw[ch].vol = (int)(val & 0xFFFF); hw[ch].wrote_vol = 1; }
        } else if (reg == 0x096) {
            for (int ch = 0; ch < 4; ch++) {
                if (!(val & (1u << ch))) continue;
                if (val & 0x8000) { if (!hw[ch].dma) { hw[ch].dma = 1; hw[ch].cur_lc = hw[ch].pend_lc; hw[ch].cur_len = hw[ch].pend_len; hw[ch].cnt = 0; hw[ch].left = 2L * hw[ch].cur_len; rig_add(ch, EV_DMA_ON, 0); } }
                else { if (hw[ch].dma) rig_add(ch, EV_DMA_OFF, 0); hw[ch].dma = 0; }
            }
        }
    }
}
static void drain(void) {
    size_t n = rig_io_log_get(NULL, 0);
    if (n > RIG_IO_LOG_MAX) { printf("  register log overflow\n"); exit(2); }
    if (n) {
        uint32_t *buf = (uint32_t *)malloc(n * 16);
        rig_io_log_get(buf, (int)n);
        hw_apply(buf, n);
        free(buf);
    }
    rig_io_log_set(1);
}

static uint32_t optr[SND_OBJECTS];
static uint32_t odata[SND_OBJECTS], olen[SND_OBJECTS];
static sound_t S;
static sound_table_t TAB;

static int rd_file(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    char p[300];
    snprintf(p, sizeof p, "%s/%s", assets_dir, path);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    *out = (uint8_t *)malloc((size_t)n + 1);
    *len = fread(*out, 1, (size_t)n, f);
    fclose(f);
    return 0;
}
static void rd_release(void *user, uint8_t *buf) { (void)user; free(buf); }

static void objects_read(void) {
    for (int n = 0; n < SND_OBJECTS; n++) {
        optr[n] = get32(TABLE + 4 * (uint32_t)n);
        odata[n] = optr[n] ? get32(optr[n]) : 0;
        olen[n] = optr[n] ? get32(optr[n] + 4) & 0x7FFFFFFFu : 0;
    }
}

static const uint32_t SCRIPT_ADDR[6] = { 0, 0x55CA0, 0x55D00, 0x55D98, 0x55D58, 0x55D28 };

/* the port's commands as the same event list */
static int cmds_total, cmds_latch, cmds_per, cmds_vol, cmds_dma;
static void shadow_drain(void) {
    game_audio_cmd_t c[GAME_MAX_AUDIO_CMDS];
    int n = 0;
    uint8_t mt, ms, mr;
    sound_drain(&S, c, GAME_MAX_AUDIO_CMDS, &n, &mt, &ms, &mr);
    for (int i = 0; i < n; i++) {
        const int v = c[i].voice & 3;
        cmds_total++;
        if (n_port_ev + 2 > EV_MAX) break;
        if (c[i].op == GAME_AUD_LATCH) {
            port_ev[n_port_ev].voice = v; port_ev[n_port_ev].kind = EV_LC; port_ev[n_port_ev].clock = c[i].clock; port_ev[n_port_ev++].value = odata[c[i].object];
            port_ev[n_port_ev].voice = v; port_ev[n_port_ev].kind = EV_LEN; port_ev[n_port_ev].clock = c[i].clock; port_ev[n_port_ev++].value = c[i].length / 2;
            if (c[i].length != olen[c[i].object]) { diffs++; printf("  DIFF latch length of object %d: %u vs %u\n", c[i].object, c[i].length, olen[c[i].object]); }
            cmds_latch++;
        } else {
            port_ev[n_port_ev].voice = v;
            port_ev[n_port_ev].kind = c[i].op == GAME_AUD_DMA_ON ? EV_DMA_ON : c[i].op == GAME_AUD_DMA_OFF ? EV_DMA_OFF : c[i].op == GAME_AUD_PERIOD ? EV_PER : EV_VOL;
            port_ev[n_port_ev].clock = c[i].clock;
            port_ev[n_port_ev++].value = c[i].value;
            if (c[i].op == GAME_AUD_PERIOD) cmds_per++; else if (c[i].op == GAME_AUD_VOLUME) cmds_vol++; else cmds_dma++;
        }
    }
}

static int obj_of_ptr(uint32_t p) { if (!p) return SND_NONE; for (int n = 0; n < SND_OBJECTS; n++) if (optr[n] == p) return n; return 0xFE; }

static int report(const char *tag, const char *what, long a, long b) {
    diffs++;
    if (verbose || diffs <= 12) printf("  DIFF %s: %s rig %ld (0x%lX) port %ld (0x%lX)\n", tag, what, a, a, b, b);
    return 1;
}

/* every observable of the library after an operation: the objects' records, the slots, the hardware state, the registers, the shadow mixer, the random register and the master level */
static int compare(const char *tag) {
    int bad = 0;
    cases++;
    for (int n = 0; n < SND_OBJECTS; n++) {
        if (!optr[n] != !S.o[n].exists) { bad += report(tag, "object exists", optr[n] != 0, S.o[n].exists); continue; }
        if (!optr[n]) continue;
        const uint32_t p = optr[n];
        const sound_obj_t *o = &S.o[n];
        char w[48];
#define CMP(f, off, mine) do { uint32_t r_ = get32(p + (off)); if (r_ != (uint32_t)(mine)) { snprintf(w, sizeof w, "object %d " f, n); bad += report(tag, w, (long)(int32_t)r_, (long)(int32_t)(mine)); } } while (0)
        CMP("period", 8, o->period); CMP("volume", 0xC, o->volume); CMP("count", 0x10, o->count); CMP("reload", 0x14, o->reload); CMP("pstep", 0x18, o->pstep); CMP("vstep", 0x1C, o->vstep);
        CMP("lc0", 0x24, o->lc[0]); CMP("lc1", 0x28, o->lc[1]); CMP("wait", 0x2C, o->wait); CMP("pos", 0x34, o->pos); CMP("ptimer", 0x38, o->ptimer); CMP("vtimer", 0x3C, o->vtimer);
        const uint32_t sc = get32(p + 0x30);
        if (sc != SCRIPT_ADDR[o->script]) { snprintf(w, sizeof w, "object %d script", n); bad += report(tag, w, sc, SCRIPT_ADDR[o->script]); }
#undef CMP
    }
    if (get32(LEVEL) != (uint32_t)S.level) bad += report(tag, "master level", get32(LEVEL), S.level);
    if (get32(0x55C9C) != S.rnd) bad += report(tag, "random register", get32(0x55C9C), S.rnd);
    for (int v = 0; v < 4; v++) {
        const sound_voice_t *w = &S.v[v];
        char m[48];
        const int rs = obj_of_ptr(get32(SLOTS + 4 * (uint32_t)v));
        if (rs != w->slot) { snprintf(m, sizeof m, "voice %d slot", v); bad += report(tag, m, rs, w->slot); }
        if (hw[v].dma != w->dma) { snprintf(m, sizeof m, "voice %d dma", v); bad += report(tag, m, hw[v].dma, w->dma); continue; }
        if (hw[v].per != w->per) { snprintf(m, sizeof m, "voice %d period", v); bad += report(tag, m, hw[v].per, w->per); }
        if (hw[v].vol != w->vol) { snprintf(m, sizeof m, "voice %d volume", v); bad += report(tag, m, hw[v].vol, w->vol); }
        if (hw[v].dma) {
            if (hw[v].cur_lc != odata[w->cur] || hw[v].cur_len != olen[w->cur] / 2) { snprintf(m, sizeof m, "voice %d block", v); bad += report(tag, m, hw[v].cur_lc, odata[w->cur]); }
        }
    }
    /* the register writes since the last comparison, in order */
    {
        static const char *kn[] = { "?", "LC", "LEN", "DMA on", "DMA off", "PER", "VOL" };
        int same = n_rig_ev == n_port_ev;
        for (int i = 0; same && i < n_rig_ev; i++) same = rig_ev[i].voice == port_ev[i].voice && rig_ev[i].kind == port_ev[i].kind && rig_ev[i].value == port_ev[i].value && rig_ev[i].clock == port_ev[i].clock;
        if (!same) {
            diffs++; bad++;
            if (verbose || diffs <= 12) {
                printf("  DIFF %s: register writes differ\n    rig :", tag);
                for (int i = 0; i < n_rig_ev; i++) printf(" v%d %s %X @%u;", rig_ev[i].voice, kn[rig_ev[i].kind], rig_ev[i].value, rig_ev[i].clock);
                printf("\n    port:");
                for (int i = 0; i < n_port_ev; i++) printf(" v%d %s %X @%u;", port_ev[i].voice, kn[port_ev[i].kind], port_ev[i].value, port_ev[i].clock);
                printf("\n");
            }
        }
        n_rig_ev = n_port_ev = 0;
    }
    return bad;
}

/* One blank in both. The order is the original's (note 10 9.2): the game's ramp (0x294AC), the library's server (0x551D8), and then the blocks that end in the blank (their audio interrupts).
 * The harness's Paula is written here from the hardware's description alone, sample by sample, and shares nothing with the port's advance: a sample lasts the period register's value at the moment it
 * starts (the counter is reloaded from the register at every sample's end), a block of n words is 2n samples, and the interrupt of a voice runs when its last sample is out. */
static void paula_blank(int ch) {
    long avail = 71051;
    while (hw[ch].dma && avail > 0) {
        if (hw[ch].cnt == 0) hw[ch].cnt = hw[ch].per > 0 ? hw[ch].per : 1;
        const long step = avail < hw[ch].cnt ? avail : hw[ch].cnt;
        hw[ch].cnt -= step; avail -= step;
        if (hw[ch].cnt == 0 && --hw[ch].left <= 0) {
            hw[ch].cur_lc = hw[ch].pend_lc; hw[ch].cur_len = hw[ch].pend_len; hw[ch].left = 2L * hw[ch].cur_len;
            rig_irq_raise(ch);
            run_at(0x110);
            rig_clock = (uint32_t)(71051 - avail);   /* the block ended here: the interrupt's writes are made at this clock */
            drain();
            rig_clock = 0;
        }
    }
}
static void rig_blank(void) {
    run_at(0x294AC); drain();
    run_at(0x551D8); drain();
    for (int ch = 0; ch < 4; ch++) paula_blank(ch);
}
static int both_blank(const char *tag) {
    rig_blank();
    sound_blank(&S);
    S.blank = 0;
    shadow_drain();
    return compare(tag);
}

/* ---- boot --------------------------------------------------------------------------------------------------------------------------------------- */
static int boot(void) {
    if (rig_load_image(image_path, BASE) != 0) return -1;
    static uint8_t zero[V_HI - V_LO];
    rig_poke(V_LO, zero, sizeof zero);
    rig_hooks_clear();
    heap = HEAP;
    put32(0x55462, 1);
    rig_hook_set(0x58B60, h_alloc, 0); rig_hook_set(0x58B78, h_nop, 0);
    rig_hook_set(0x55480, h_size, 0); rig_hook_set(0x56014, h_fopen, 0); rig_hook_set(0x56428, h_fread, 0); rig_hook_set(0x5626E, h_nop, 0);
    rig_io_log_set(1);
    rig_set_max_instructions(200000000);
    const uint32_t loaders[3] = { 0x19660, 0x196BA, 0x199CA };
    for (int i = 0; i < 3; i++) if (run_at(loaders[i]) != 0) { printf("  loader %X failed (%d)\n", loaders[i], rig_last_error); return -1; }
    rig_irq_audio(1, 0x55158, 0x54EC0);
    memset(hw, 0, sizeof hw);
    rig_io_log_set(1);
    objects_read();
    put32(LEVEL, 0x3F0000); put32(0x4A720, 0x3F0000);
    put8(0x4A88F, 0);
    n_rig_ev = n_port_ev = 0;
    sound_init(&S, &TAB);
    S.en12 = (uint8_t)get8(0x4AC12); S.en13 = (uint8_t)get8(0x4AC13);
    return 0;
}

/* ---- the operations, applied to both sides ------------------------------------------------------------------------------------------------------- */
static void set_enable(int e12, int e13) { put8(0x4AC12, e12); put8(0x4AC13, e13); S.en12 = (uint8_t)e12; S.en13 = (uint8_t)e13; }
static void set_camera(int cam) { put16(0x4AA6E, 7); put16(0x4A996, cam ? 7 : 8); }

/* the object field pokes of the blank part: which = 0 period, 1 volume, 2 pstep, 3 vstep, 4 ptimer, 5 vtimer, 6 count */
static void poke_field(int n, int which, int32_t v) {
    if (!optr[n]) return;
    static const int off[7] = { 8, 0xC, 0x18, 0x1C, 0x38, 0x3C, 0x10 };
    put32(optr[n] + (uint32_t)off[which], (uint32_t)v);
    sound_obj_t *o = &S.o[n];
    int32_t *f[7] = { &o->period, &o->volume, &o->pstep, &o->vstep, &o->ptimer, &o->vtimer, &o->count };
    *f[which] = v;
}


static int op_random(const char *tag, int op, int quiet) {
    char t[80];
    snprintf(t, sizeof t, "%s op %d", tag, op);
    (void)quiet;
    switch (op) {
    case 0: {   /* the player */
        const int n = rnd(0, 0x24), v = pct(70) && S.o[n].exists ? S.tab.obj[n].voice : rnd(0, 3), vol = rnd(0, 0x40);
        call(0x19C7C, 3, (uint32_t)n, (uint32_t)v, (uint32_t)vol, 0, 0, 0, 0, 0, 0);
        sound_start(&S, n, v, vol);
        break; }
    case 1: { const int v = rnd(0, 3); call(0x19C58, 1, (uint32_t)v, 0, 0, 0, 0, 0, 0, 0, 0); sound_stop_voice(&S, v); break; }
    case 2: call(0x115F6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0); sound_stop_all(&S); break;
    case 3: {   /* a poke into an object's record */
        const int n = rnd(0, 0x24), which = rnd(0, 6);
        int32_t v;
        switch (which) {
        case 0: v = rnd(0x40, 0x3000) << 16; break;
        case 1: v = rnd(0, 0x44) << 16; break;
        case 2: v = (rnd(-300, 300) << 12); break;
        case 3: v = (rnd(-200, 200) << 12); break;
        case 4: case 5: v = pct(50) ? 0 : rnd(1, 20); break;
        default: v = pct(40) ? -1 : rnd(0, 3); break;
        }
        poke_field(n, which, v);
        break; }
    case 4: { const int a = rnd(1, 0x40), b = rnd(1, 0x90); call(0x1A0DC, 2, (uint32_t)a, (uint32_t)b, 0, 0, 0, 0, 0, 0, 0); sound_event(&S, SND_NOISE, a, b); break; }
    case 5: { const int b = pct(50) ? 0x16 : 0x1E; call(0x1A2F0, 2, 6, (uint32_t)b, 0, 0, 0, 0, 0, 0, 0); sound_event(&S, SND_GUN, 6, b); break; }
    case 6: { const int v = pct(40) ? 0x0F : pct(50) ? 0x1E : 0x28, cam = pct(85); set_camera(cam); call(0x1A18C, 1, (uint32_t)v, 0, 0, 0, 0, 0, 0, 0, 0); sound_event(&S, SND_GROUND, v, cam); break; }
    case 7: { const int v = pct(50) ? 10 : 5, cam = pct(85); set_camera(cam); call(0x1A1E6, 1, (uint32_t)v, 0, 0, 0, 0, 0, 0, 0, 0); sound_event(&S, SND_GEAR, v, cam); break; }
    case 8: call(0x1A24C, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0); sound_event(&S, SND_GEAR_STOP, 0, 0); break;
    case 9: { const int v = rnd(2, 0x3F); call(0x1A258, 1, (uint32_t)v, 0, 0, 0, 0, 0, 0, 0, 0); sound_event(&S, SND_LAUNCH, v, 0); break; }
    case 10: {
        int p[9];
        p[0] = rnd(0x7C, 0x600); p[1] = rnd(0, 0x3F); p[2] = rnd(1, 20); p[3] = rnd(0x7C, 0x600); p[4] = rnd(0, 0x3F); p[5] = rnd(1, 20); p[6] = rnd(0, 4); p[7] = rnd(0, 4); p[8] = rnd(0, 12);
        call(0x1A042, 9, (uint32_t)p[0], (uint32_t)p[1], (uint32_t)p[2], (uint32_t)p[3], (uint32_t)p[4], (uint32_t)p[5], (uint32_t)p[6], (uint32_t)p[7], (uint32_t)p[8]);
        sound_tone(&S, p);
        break; }
    case 11: {   /* the phrase wrappers in the variants of the enable bits and the flag */
        static const int variants[][3] = { { 7, 0xF7, 0 }, { 3, 0xF7, 0 }, { 7, 0x77, 0 }, { 3, 0x77, 0 }, { 7, 0xF7, 1 }, { 7, 0xF7, 2 } };
        const int *vr = variants[rnd(0, 5)];
        const int which = rnd(0, 1), v = pct(50) ? 0x0F : pct(50) ? 0x32 : 0x28;
        set_enable(vr[0], vr[1]); put8(0x4A88F, vr[2]); sound_master(&S, -1, -1, vr[2]);
        call(which ? 0x19D7A : 0x19CE6, 1, (uint32_t)v, 0, 0, 0, 0, 0, 0, 0, 0);
        sound_event(&S, SND_PHRASE, which, v);
        if ((int)S.flag != get8(0x4A88F)) report(t, "flag after the phrase", get8(0x4A88F), S.flag);
        set_enable(7, 0xF7);
        break; }
    case 12: {   /* the click and the beeps */
        const int view = pct(60) ? 0 : rnd(1, 3), flag = rnd(0, 2), cool = pct(50) ? 0 : pct(50) ? rnd(1, 3) : 0xFF;
        put8(0x4A83D, view); put8(0x4A88F, flag); put8(0x4A942, cool); sound_master(&S, -1, -1, flag); S.cooldown = (uint8_t)cool;
        const int k = rnd(0, 3);
        zero_regs();
        if (k == 0) { const int v = pct(50) ? 2 : 5; rig_set_reg(RIG_D1, (uint32_t)v); rig_run(0x37C2E); sound_event(&S, SND_CLICK, v, 0); }
        else if (k == 1) { const int n = pct(50) ? 8 : 4; rig_set_reg(RIG_D0, (uint32_t)n); rig_run(0x37C32); sound_event(&S, SND_BEEP_N, n, view); }
        else if (k == 2) { rig_run(0x37C44); sound_event(&S, SND_BEEP_44, 0, 0); }
        else { rig_run(0x37C4A); sound_event(&S, SND_BEEP_SWITCH, view, 0); }
        put8(0x4A88F, 0); sound_master(&S, -1, -1, 0); put8(0x4A942, 0); S.cooldown = 0;
        break; }
    case 13: { const int v = rnd(1, 0x3F); call(0x19F9A, 1, (uint32_t)v, 0, 0, 0, 0, 0, 0, 0, 0); sound_event(&S, SND_WIND, v, 0); break; }
    case 14: { const int p = rnd(0x1E0, 0x340), v = rnd(0, 0x3F); call(0x19DB2, 2, (uint32_t)p, (uint32_t)v, 0, 0, 0, 0, 0, 0, 0); sound_event(&S, SND_ENGINE_COCKPIT, p, v); break; }
    case 15: { const int p = rnd(0x1E0, 0x340), v = rnd(0, 0x3F); call(0x19E46, 2, (uint32_t)p, (uint32_t)v, 0, 0, 0, 0, 0, 0, 0); sound_event(&S, SND_ENGINE_EXT, p, v); break; }
    case 16: {   /* the master inputs */
        const int k = rnd(0, 3);
        if (k == 0) { const int tg = pct(30) ? 0 : pct(50) ? 31 : rnd(0, 63); put32(0x4A720, (uint32_t)tg << 16); sound_master(&S, tg, -1, -1); }
        else if (k == 1) { const int fl = rnd(0, 2); put8(0x4A88F, fl); sound_master(&S, -1, -1, fl); }
        else if (k == 2) { const int lv = pct(50) ? 31 : pct(50) ? 63 : rnd(0, 63); put32(LEVEL, (uint32_t)lv << 16); sound_master(&S, -1, lv, -1); }
        else { put8(0x4A88F, 1); sound_master(&S, -1, -1, 1); }
        break; }
    default: break;
    }
    drain();
    shadow_drain();
    return compare(t);
}

static void settle_set(void) { set_enable(7, 0xF7); }

/* ---- the parts ------------------------------------------------------------------------------------------------------------------------------------ */
static void part_tone(void) {
    static const int cues[][9] = {
        { 0xA0, 0x10, 8, 0xC8, 0x10, 5, 3, 4, 4 }, { 0x144, 0x0A, 8, 0xC8, 0x0A, 5, 3, 1, 4 }, { 0x7C, 0x10, 8, 0xB4, 0x10, 5, 3, 3, 4 }, { 0xE5, 3, 5, 0xE5, 3, 5, 1, 0, 1 },
        { 0xC0, 4, 9, 0xC8, 0, 3, 1, 0, 1 }, { 0x1F4, 0x0F, 5, 0x1F4, 0x0F, 5, 3, 1, 5 }, { 0x1F4, 0x0A, 5, 0x82, 0, 1, 3, 3, 5 }, { 0x190, 8, 5, 0x14A, 0, 1, 3, 2, 5 },
        { 0x1F4, 0x0A, 5, 0x82, 0, 1, 3, 1, 5 }, { 0xC8, 0xE, 8, 0xC8, 0, 2, 1, 5, 1 }, { 0x12C, 4, 1, 0x12C, 4, 1, 1, 2, 2 }, { 0x12C, 2, 1, 0x12C, 2, 1, 1, 1, 1 } };
    if (boot()) { printf("tone: boot failed\n"); diffs++; return; }
    int sets = 0, stops = 0, bufbad = 0;
    for (int k = 0; k < 12 + 200; k++) {
        int p[9];
        if (k < 12) memcpy(p, cues[k], sizeof p);
        else { p[0] = rnd(0x7C, 0x600); p[1] = rnd(0, 0x3F); p[2] = rnd(1, 20); p[3] = rnd(0x7C, 0x600); p[4] = rnd(0, 0x3F); p[5] = rnd(1, 20); p[6] = rnd(0, 4); p[7] = rnd(0, 4); p[8] = rnd(0, 12); }
        call(0x1A042, 9, (uint32_t)p[0], (uint32_t)p[1], (uint32_t)p[2], (uint32_t)p[3], (uint32_t)p[4], (uint32_t)p[5], (uint32_t)p[6], (uint32_t)p[7], (uint32_t)p[8]);
        drain();
        sound_tone(&S, p);
        shadow_drain();
        sets++;
        char tag[40]; snprintf(tag, sizeof tag, "tone set %d", k);
        compare(tag);
        const int8_t *data; uint32_t len;
        uint8_t buf[SND_TONE_BYTES];
        rig_peek(odata[4], buf, sizeof buf);
        if (!sound_buffer(&S, 4, &data, &len) || len != SND_TONE_BYTES || memcmp(buf, data, len)) { bufbad++; diffs++; printf("  DIFF tone buffer\n"); }
        for (int b = 0; b < 500; b++) {
            both_blank(tag);
            if (!hw[3].dma) { stops++; break; }
        }
        call(0x19C58, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0); drain(); sound_stop_voice(&S, 3); shadow_drain();
    }
    {   /* the noise buffer (3.3) */
        uint8_t nb[SND_NOISE_BYTES];
        const int8_t *data; uint32_t len;
        for (int k = 0; k < 2; k++) {
            const int n = k ? 0xA : 6;
            rig_peek(odata[n], nb, sizeof nb);
            if (!sound_buffer(&S, n, &data, &len) || len != SND_NOISE_BYTES || memcmp(nb, data, len)) { diffs++; printf("  DIFF noise buffer of object %X\n", n); }
        }
    }
    printf("tone: %d parameter sets (12 cue, click and beep sets, 200 random), %d ended by their own script, buffers %s\n", sets, stops, bufbad ? "DIFFER" : "equal");
}

static void part_blank(int sequences) {
    int blanks = 0, d0 = diffs;
    for (int q = 0; q < sequences; q++) {
        if (boot()) { printf("blank: boot failed\n"); diffs++; return; }
        for (int b = 0; b < 100; b++) {
            if (pct(30)) { static const int ops[] = { 0, 0, 1, 3, 3, 3, 3, 16, 16, 2 }; op_random("blank", ops[rnd(0, 9)], 0); }
            char tag[32]; snprintf(tag, sizeof tag, "blank seq %d #%d", q, b);
            both_blank(tag);
            blanks++;
        }
    }
    printf("blank: %d blanks over %d sequences of random starts, stops, pokes of steps, timers and counts, ramp inputs; %d register writes (%d latch, %d period, %d volume, %d dma), %d differences\n",
           blanks, sequences, cmds_total, cmds_latch, cmds_per, cmds_vol, cmds_dma, diffs - d0);
}

/* the music chain (4.2, 7.1): the phrase wrapper starts objects 0xD and 0xE; the object counts run the chain; after a stop of all voices in the middle of the loop and a phrase again (a flight in between,
 * flag 0), the objects keep the counts they were left with (the intro plays one block each); 4,000 blanks in all, every register write compared */
static void part_music(void) {
    int d0 = diffs, blanks = 0;
    if (boot()) { printf("music: boot failed\n"); diffs++; return; }
    for (int round = 0; round < 3; round++) {
        put8(0x4A88F, 0); sound_master(&S, -1, -1, 0);
        call(0x19CE6, 1, round == 1 ? 0x0F : 0x32, 0, 0, 0, 0, 0, 0, 0, 0);
        sound_event(&S, SND_PHRASE, 0, round == 1 ? 0x0F : 0x32);
        drain(); shadow_drain(); compare("music start");
        const int n = round == 0 ? 2200 : 1200;
        for (int b = 0; b < n; b++) { both_blank("music"); blanks++; }
        call(0x115F6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0); sound_stop_all(&S);
        drain(); shadow_drain(); compare("music stop");
    }
    printf("music: %d blanks over the intro, the loop and two restarts after a stop in the loop, %d register writes (%d latch), %d differences\n", blanks, cmds_total, cmds_latch, diffs - d0);
}

static void part_events(int runs) {
    int d0 = diffs, n = 0, blanks = 0;
    for (int q = 0; q < runs; q++) {
        if (boot()) { printf("events: boot failed\n"); diffs++; return; }
        for (int k = 0; k < 40; k++) {
            if (pct(15)) { static const int e12[] = { 7, 7, 6, 5, 3 }, e13[] = { 0xF7, 0xF7, 0xF6, 0xF5, 0xE7, 0xD7, 0xB7, 0x77, 0xF3 }; set_enable(e12[rnd(0, 4)], e13[rnd(0, 8)]); }
            else if (pct(40)) settle_set();
            op_random("events", rnd(4, 15), 0);
            n++;
            for (int b = rnd(1, 70); b > 0; b--) { char tag[32]; snprintf(tag, sizeof tag, "events run %d #%d", q, k); both_blank(tag); blanks++; }
        }
    }
    printf("events: %d wrapper calls with random arguments and enable bits, %d blanks after them, %d differences\n", n, blanks, diffs - d0);
}

/* ---- the per-pass routine -------------------------------------------------------------------------------------------------------------------------- */
typedef struct {
    int notice, v4a966, v4a865, v4a866, v4a84d, gear, view, v4a86d, v4a879, cls, w2, t2b, w56, w5a, w6e, v4a96a, v4abfa, v4a941, v4a870, v4a921, v4a922, v4a84f, en12, en13, cam;
    uint32_t cues; int cool, nib;
} pstate_t;

static pstate_t draw_state(void) {
    pstate_t S_;
    memset(&S_, 0, sizeof S_);
    static const int notices[] = { 0, 0, 0, 0xFE, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xFF, 0x7F };
    S_.notice = notices[rnd(0, 10)];
    S_.v4a966 = pct(3); S_.v4a865 = pct(3); S_.v4a866 = pct(3); S_.v4a84d = !pct(3);
    static const int gears[] = { 0, 0, 0x83, 0x83, 1, 2, 3, 0x82, 0x80 };
    S_.gear = gears[rnd(0, 8)];
    S_.view = pct(50) ? 0 : pct(30) ? 0xFF : rnd(1, 6);
    S_.v4a86d = rnd(0, 1); S_.v4a879 = rnd(0, 255);
    static const int cls[] = { 0x10, 0x20, 0x30, 0x40, 0x11 };
    S_.cls = pct(85) ? cls[rnd(0, 4)] : rnd(0, 255);
    S_.w2 = rnd(0, 0xFFFF);
    S_.t2b = pct(20) ? 0 : pct(50) ? rnd(0, 255) : pct(50) ? rnd(0x78, 0x7F) : rnd(0, 255);
    S_.w56 = pct(30) ? 0 : pct(50) ? rnd(0, 0xFFFF) : rnd(-0x400, 0x400) & 0xFFFF;
    S_.w5a = pct(30) ? 0 : pct(50) ? rnd(0, 0xFFFF) : rnd(-0x400, 0x400) & 0xFFFF;
    S_.w6e = pct(20) ? 0 : pct(50) ? rnd(0, 0x7FFF) : rnd(0, 0xFFFF);
    S_.v4a96a = rnd(-8, 12) & 0xFF;
    S_.v4abfa = pct(50) ? 0 : pct(90) ? rnd(1, 4000) : rnd(-100, -1) & 0xFFFF;
    S_.v4a941 = pct(60) ? rnd(0, 15) : pct(50) ? 0xFF : rnd(0, 255);
    S_.v4a870 = pct(55) ? 0 : pct(30) ? 0xFF : rnd(1, 4);
    S_.v4a921 = rnd(1, 0x7F); S_.v4a922 = rnd(0, 0x7F);
    S_.v4a84f = pct(50) ? 0 : pct(50) ? 1 : pct(50) ? 2 : pct(50) ? 8 : rnd(0, 255);
    S_.en12 = 7; S_.en13 = 0xF7;
    S_.cam = pct(85);
    if (pct(35)) {
        static const uint32_t bits[] = { 0x40, 0x80, 0x01, 0x04, 0x100, 0x10, 0x02, 0x20, 0x800, 0x08, 0x200 };
        for (int i = 0; i < 11; i++) if (pct(25)) S_.cues |= bits[i];
    }
    S_.cool = pct(70) ? 0 : pct(50) ? rnd(1, 3) : 0xFF;
    S_.nib = pct(75) ? 0 : pct(50) ? rnd(1, 15) : rnd(0, 255);
    if (pct(8)) {   /* a file missing: the sound's objects are not there and the enable bits are clear */
        switch (rnd(0, 6)) {
        case 0: S_.en13 &= ~1; break; case 1: S_.en13 &= ~2; break; case 2: S_.en13 &= ~0x10; break; case 3: S_.en13 &= ~0x40; break;
        case 4: S_.en13 &= ~4; break; case 5: S_.en12 &= ~1; break; default: S_.en12 &= ~2; break;
        }
    }
    return S_;
}

/* one pass in both: the original's 0x14AA0 with the state P poked into the variables, the port's sound_pass with the same inputs; compared, then six blanks. The bytes the routine owns are carried
 * from pass to pass by the caller in `carry` (the notice, the hold, the cue long) when the state asks for it (P->keep), else they are P's. */
static int pass_both(const pstate_t *Pp, const char *tag, int *blanks, int *statebad, int verbose_state) {
    const pstate_t P = *Pp;
    const uint32_t R = 0x4B23C;
    put16(0x4A994, 0);
    put16(R + 2, P.w2); put8(R + 0x2B, P.t2b); put16(R + 0x56, P.w56); put16(R + 0x5A, P.w5a); put16(R + 0x6E, P.w6e); put8(R + 0x62, P.cls); put8(R + 0x7C, P.nib);
    put8(0x4A968, P.notice); put8(0x4A966, P.v4a966); put8(0x4A865, P.v4a865); put8(0x4A866, P.v4a866); put8(0x4A84D, P.v4a84d);
    put8(0x4A93D, P.gear); put8(0x4A83D, P.view); put8(0x4A86D, P.v4a86d); put8(0x4A879, P.v4a879); put8(0x4A96A, P.v4a96a);
    put16(0x4ABFA, P.v4abfa); put8(0x4A941, P.v4a941); put8(0x4A84F, P.v4a84f);
    set_enable(P.en12, P.en13);
    set_camera(P.cam);
    put32(0x4AC0C, P.cues); put8(0x4A942, P.cool); put8(0x4A870, P.v4a870); put8(0x4A921, P.v4a921); put8(0x4A922, P.v4a922);
    run_at(0x14AA0);
    drain();
    uint8_t notice = (uint8_t)P.notice, hold = (uint8_t)P.v4a84f;
    uint32_t cues = P.cues;
    S.gear_cd = (uint8_t)P.gear; S.cooldown = (uint8_t)P.cool; S.bang_cd = (uint8_t)P.v4a870; S.bang_dur = (uint8_t)P.v4a921; S.bang_amp = (uint8_t)P.v4a922;
    sound_in_t in;
    memset(&in, 0, sizeof in);
    in.v4a966 = (uint8_t)P.v4a966; in.v4a865 = (uint8_t)P.v4a865; in.v4a866 = (uint8_t)P.v4a866; in.v4a84d = (uint8_t)P.v4a84d;
    in.view = (uint8_t)P.view; in.v4a86d = (uint8_t)P.v4a86d; in.ext_dir = (int8_t)P.v4a96a; in.cam_dist = (int16_t)P.v4abfa; in.strain = (int8_t)P.v4a941;
    in.pass_n = (uint8_t)P.v4a879; in.camera = (uint8_t)P.cam; in.w2 = (uint16_t)P.w2; in.throttle = (int8_t)P.t2b;
    in.speed = (int16_t)P.w6e; in.pitch_rate = (int16_t)P.w56; in.roll_rate = (int16_t)P.w5a; in.cls = (uint8_t)P.cls; in.eject_nibble = (uint8_t)(P.nib & 0xF);
    in.notice = &notice; in.hold = &hold; in.cues = &cues;
    sound_pass(&S, &in);
    shadow_drain();
    int b0 = diffs;
    compare(tag);
    if (get8(0x4A968) != notice || get8(0x4A93D) != S.gear_cd || get8(0x4A84F) != hold || get8(0x4A870) != S.bang_cd || get32(0x4AC0C) != cues || get8(0x4A942) != S.cooldown) {
        (*statebad)++; diffs++;
        if (*statebad <= 8) printf("  DIFF %s state: rig notice %02X gear %02X hold %d bang %d cues %X cool %d | port %02X %02X %d %d %X %d | in view %02X cls %02X notice %02X t %d nib %02X cues %X cool %d\n", tag,
                                  get8(0x4A968), get8(0x4A93D), get8(0x4A84F), get8(0x4A870), get32(0x4AC0C), get8(0x4A942), notice, S.gear_cd, hold, S.bang_cd, cues, S.cooldown,
                                  P.view, P.cls, P.notice, P.t2b, P.nib, P.cues, P.cool);
    }
    if (diffs > b0 && verbose_state) printf("  state: view %02X cls %02X notice %02X t %d w2 %04X E %04X Q %04X W %04X v86d %d dir %d dist %d strain %d hold %d pass %d cam %d\n", P.view, P.cls, P.notice, (int8_t)P.t2b,
                                      P.w2, P.w56, P.w5a, P.w6e, P.v4a86d, (int8_t)P.v4a96a, (int16_t)P.v4abfa, (int8_t)P.v4a941, P.v4a84f, P.v4a879, P.cam),
                                      printf("         en12 %02X en13 %02X bang %d (%d,%d) gear %02X cues %X cool %d nib %02X gates %d%d%d%d\n", P.en12, P.en13, P.v4a870, P.v4a922, P.v4a921, P.gear, P.cues, P.cool, P.nib, P.v4a966, P.v4a865, P.v4a866, P.v4a84d);
    for (int b = 0; b < 6; b++) { both_blank(tag); (*blanks)++; }
    return diffs > b0;
}

static void part_pass(int passes) {
    int d0 = diffs, blanks = 0, statebad = 0;
    unsigned long calls = 0;
    if (boot()) { printf("pass: boot failed\n"); diffs++; return; }
    for (int it = 0; it < passes; it++) {
        const pstate_t P = draw_state();
        char tag[48]; snprintf(tag, sizeof tag, "pass %d", it);
        pass_both(&P, tag, &blanks, &statebad, verbose);
        calls++;
        if (diffs - d0 > 8) break;
    }
    printf("pass: %lu passes, %d blanks, %d register writes (%d latch, %d period, %d volume, %d dma), %d differences\n", calls, blanks, cmds_total, cmds_latch, cmds_per, cmds_vol, cmds_dma, diffs - d0);
}

/* The flight start of a mission or free flight (note 10 5.7 item 10, 7.1, 7.2): the start-up without the music, and the passes of the start. */
static pstate_t start_state(int notice, int gate, int view, int v86d, int dir, int pass_n) {
    pstate_t P;
    memset(&P, 0, sizeof P);
    P.notice = notice; P.v4a966 = gate; P.v4a84d = 1; P.view = view; P.v4a86d = v86d; P.v4a96a = dir; P.v4a879 = pass_n;
    P.cls = 0x11; P.w2 = 0x0080; P.t2b = 9; P.en12 = 7; P.en13 = 0xF7; P.cam = 1; P.nib = 0; P.cool = 0;
    return P;
}
static void part_start(void) {
    int d0 = diffs, blanks = 0, statebad = 0, passes = 0;
    /* (a) the start-up routine 0x10548: with V:4AC13 bit 7 clear (no music) it calls 0x19D7A(0x0F), not 0x19CE6(0x32), and the flag V:4A88F stays 0 */
    if (boot()) { printf("start: boot failed\n"); diffs++; return; }
    set_enable(7, 0x77);
    put32(LEVEL, 0x3F0000); put32(0x4A720, 0x3F0000); put8(0x4A88F, 0); sound_master(&S, 63, 63, 0);
    call(0x19D7A, 1, 0x0F, 0, 0, 0, 0, 0, 0, 0, 0);
    sound_event(&S, SND_PHRASE, 1, 0x0F);
    drain(); shadow_drain(); compare("start-up phrase");
    if (get8(0x4A88F) != S.flag) { diffs++; printf("  DIFF the start-up flag: rig %d port %d\n", get8(0x4A88F), S.flag); }
    for (int b = 0; b < 30; b++) { both_blank("start-up phrase"); blanks++; }
    /* (b) a flight start: the wind of the dispatch, the approach (the routine gated by V:4A966 = 6, the notice 0xFE pending), the first ungated pass with the approach's outside view (the external pair at
     * 776/778, volume 12), the pass after the switch to the cockpit view (the cockpit pair at 764/766, volume 40), then the engine's own retargets (a glide of the cockpit pair, never the wind) */
    if (boot()) { printf("start: boot failed\n"); diffs++; return; }
    set_enable(7, 0xF7);
    put32(LEVEL, 0x3F0000); put8(0x4A88F, 0); sound_master(&S, -1, 63, 0);
    call(0x19F9A, 1, 8, 0, 0, 0, 0, 0, 0, 0, 0);
    sound_event(&S, SND_WIND, 8, 0);
    drain(); shadow_drain(); compare("dispatch wind");
    for (int b = 0; b < 40; b++) { both_blank("dispatch wind"); blanks++; }
    pstate_t P;
    for (int k = 0; k < 6; k++) { P = start_state(0xFE, 6, 1, 1, 0, 100 + k); pass_both(&P, "gated approach", &blanks, &statebad, 1); passes++; }
    P = start_state(0xFE, 0, 1, 1, 0, 106); pass_both(&P, "first ungated pass", &blanks, &statebad, 1); passes++;
    if (S.v[0].per != 776 || S.v[0].vol != 12 || S.v[1].per != 778) { diffs++; printf("  DIFF the first ungated pass: port periods %d/%d volume %d, expected 776/778 and 12\n", S.v[0].per, S.v[1].per, S.v[0].vol); }
    P = start_state(0xFE, 0, 0, 1, 4, 107); pass_both(&P, "cockpit pass", &blanks, &statebad, 1); passes++;
    if (S.v[0].per != 764 || S.v[0].vol != 40 || S.v[1].per != 766) { diffs++; printf("  DIFF the cockpit pass: port periods %d/%d volume %d, expected 764/766 and 40\n", S.v[0].per, S.v[1].per, S.v[0].vol); }
    for (int k = 0; k < 12; k++) { P = start_state(0, 0, 0, 1, 4, 108 + k); pass_both(&P, "engine retarget", &blanks, &statebad, 1); passes++; }
    printf("start: the start-up phrase without the music and %d passes of a flight start, %d blanks, %d differences\n", passes, blanks, diffs - d0);
}

int main(void) {
    const char *paths[] = { "../../extracted/interceptor.bin", "extracted/interceptor.bin" };
    image_path = NULL;
    if (rig_init(0xE00000) != 0) { printf("rig_init failed\n"); return 1; }
    for (int i = 0; i < 2 && !image_path; i++) if (rig_load_image(paths[i], BASE) == 0) image_path = paths[i];
    if (!image_path) { printf("test_sound_diff: skipped (extracted/interceptor.bin not found)\n"); rig_shutdown(); return 0; }
    if (image_path == paths[1]) assets_dir = "assets";
    game_loader_t ld = { rd_file, rd_release, NULL };
    char err[128];
    if (assets_load_sounds(&ld, &TAB, err, sizeof err) != 0) { printf("test_sound_diff: skipped (%s)\n", err); rig_shutdown(); return 0; }
    { char p[300]; snprintf(p, sizeof p, "%s/sounds/textcpt.raw", assets_dir); FILE *f = fopen(p, "rb"); if (!f) { printf("test_sound_diff: skipped (no sample files)\n"); rig_shutdown(); return 0; } fclose(f); }
    if (getenv("SOUND_DIFF_SEED")) rng = (uint32_t)atoi(getenv("SOUND_DIFF_SEED")) * 2654435761u + 1;
    verbose = getenv("SOUND_DIFF_VERBOSE") != NULL;
    const int N = getenv("SOUND_DIFF_N") ? atoi(getenv("SOUND_DIFF_N")) : 2000;
    const char *only = getenv("SOUND_DIFF_PART");
    if (!only || !strcmp(only, "tone")) part_tone();
    if (!only || !strcmp(only, "blank")) part_blank(N / 200 > 0 ? N / 200 : 1);
    if (!only || !strcmp(only, "music")) part_music();
    if (!only || !strcmp(only, "events")) part_events(N / 100 > 0 ? N / 100 : 1);
    if (!only || !strcmp(only, "pass")) part_pass(N);
    if (!only || !strcmp(only, "start")) part_start();
    rig_shutdown();
    printf("test_sound_diff: %d comparisons, %d differences\n", cases, diffs);
    return diffs ? 1 : 0;
}
