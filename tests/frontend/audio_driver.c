/* tests/frontend/audio_driver.c - the Paula-like mixer without a device (test_audio.py): reads commands from stdin and prints what is rendered. Commands:
 *   rate N | channels N | filter 0/1      before the first other command (create is lazy); the filter defaults to the mixer's default (ON)
 *   sample OBJ N b0 .. bN-1               the object's buffer (signed bytes)
 *   clock HZ | live 0/1                    the colour clocks a second of output time (default the machine's 3,546,895), live mode (audio_set_live); before the first other command
 *   at B | atc C                          the blank (0..6) and the colour clock within it of the register writes queued next (default 0, 0; both reset by apply)
 *   latch VOICE OBJ OFF LEN | dmaon VOICE | dmaoff VOICE | period VOICE P | volume VOICE V     queue a register write for the next apply
 *   apply                                 audio_apply of the queued writes (one simulation step; steps are applied in order, the k-th starts at blank floor(25 k / 4))
 *   render N | skip N                     prints N frames, "l r" per line (one value in mono) | renders them and prints nothing (the device's callbacks of a simulation)
                               prints "stats pending underruns merged_blanks trim lead_clocks" */
#include "../../src/frontend/sdl/audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static audio_t *A;
static int rate = 44100, channels = 2, filter = -1, at = 0, live = 0;
static unsigned atclock = 0, clock_hz = 0;
static game_audio_cmd_t Q[1024];
static int qn;
static int8_t *bufs[AUDIO_OBJECTS];
static long long gframe = 0, run_first = -1, run_len = 0;

static void ensure(void) {
    if (A) return;
    A = audio_create(rate, channels);
    if (!A) { fprintf(stderr, "audio_create failed\n"); exit(2); }
    if (filter >= 0) audio_set_filter(A, filter);
    if (clock_hz) audio_set_clock(A, clock_hz);
    if (live) audio_set_live(A, 1);
}
static int queue(int op, unsigned voice, unsigned obj, unsigned value, unsigned off, unsigned len) {
    if (qn >= 1024) return -1;
    game_audio_cmd_t c; memset(&c, 0, sizeof c);
    c.op = (uint8_t)op; c.voice = (uint8_t)voice; c.object = (uint8_t)obj; c.blank = (uint8_t)at; c.clock = atclock; c.value = (uint16_t)value; c.offset = off; c.length = len;
    Q[qn++] = c;
    return 0;
}

int main(void) {
    char cmd[32];
    while (scanf("%31s", cmd) == 1) {
        unsigned v, o, x, y;
        if (!strcmp(cmd, "rate")) { if (scanf("%d", &rate) != 1) return 2; }
        else if (!strcmp(cmd, "channels")) { if (scanf("%d", &channels) != 1) return 2; }
        else if (!strcmp(cmd, "filter")) { if (scanf("%d", &filter) != 1) return 2; }
        else if (!strcmp(cmd, "clock")) { if (scanf("%u", &clock_hz) != 1) return 2; }
        else if (!strcmp(cmd, "live")) { if (scanf("%d", &live) != 1) return 2; }
        else if (!strcmp(cmd, "atc")) { if (scanf("%u", &atclock) != 1) return 2; }
        else if (!strcmp(cmd, "runs")) {
            int n;
            if (scanf("%d", &n) != 1) return 2;
            ensure();
            int16_t *buf = (int16_t *)malloc((size_t)n * (size_t)channels * sizeof(int16_t) + 2);
            audio_render(A, buf, n);
            for (int i = 0; i < n; i++, gframe++) {
                const int nz = buf[i * channels] != 0 || (channels == 2 && buf[2 * i + 1] != 0);
                if (nz) { if (run_first < 0) { run_first = gframe; run_len = 0; } run_len++; }
                else if (run_first >= 0) { printf("run %lld %lld\n", run_first, run_len); run_first = -1; }
            }
            free(buf);
        }
        else if (!strcmp(cmd, "flush")) { if (run_first >= 0) printf("run %lld %lld\n", run_first, run_len); run_first = -1; }
        else if (!strcmp(cmd, "stats")) {
            ensure();
            audio_stats_t st = audio_stats(A);
            printf("stats %u %u %u %d %lld\n", st.pending, st.underruns, st.merged_blanks, (int)st.trim, (long long)st.lead_clocks);
        }
        else if (!strcmp(cmd, "at")) { if (scanf("%d", &at) != 1) return 2; }
        else if (!strcmp(cmd, "sample")) {
            int oo, n;
            if (scanf("%d %d", &oo, &n) != 2 || oo < 0 || oo >= AUDIO_OBJECTS) return 2;
            ensure();
            free(bufs[oo]);
            bufs[oo] = (int8_t *)malloc((size_t)n + 1);
            for (int i = 0; i < n; i++) { int b; if (scanf("%d", &b) != 1) return 2; bufs[oo][i] = (int8_t)b; }
            audio_set_sample(A, oo, bufs[oo], (uint32_t)n);
        } else if (!strcmp(cmd, "latch")) { if (scanf("%u %u %u %u", &v, &o, &x, &y) != 4 || queue(GAME_AUD_LATCH, v, o, 0, x, y)) return 2; }
        else if (!strcmp(cmd, "dmaon")) { if (scanf("%u", &v) != 1 || queue(GAME_AUD_DMA_ON, v, 0, 0, 0, 0)) return 2; }
        else if (!strcmp(cmd, "dmaoff")) { if (scanf("%u", &v) != 1 || queue(GAME_AUD_DMA_OFF, v, 0, 0, 0, 0)) return 2; }
        else if (!strcmp(cmd, "period")) { if (scanf("%u %u", &v, &x) != 2 || queue(GAME_AUD_PERIOD, v, 0, x, 0, 0)) return 2; }
        else if (!strcmp(cmd, "volume")) { if (scanf("%u %u", &v, &x) != 2 || queue(GAME_AUD_VOLUME, v, 0, x, 0, 0)) return 2; }
        else if (!strcmp(cmd, "apply")) { ensure(); audio_apply(A, Q, qn); qn = 0; at = 0; atclock = 0; }
        else if (!strcmp(cmd, "render") || !strcmp(cmd, "skip")) {
            const int quiet = cmd[0] == 's';
            int n;
            if (scanf("%d", &n) != 1) return 2;
            ensure();
            int16_t *buf = (int16_t *)malloc((size_t)n * (size_t)channels * sizeof(int16_t) + 2);
            audio_render(A, buf, n);
            for (int i = 0; !quiet && i < n; i++) {
                if (channels == 1) printf("%d\n", buf[i]);
                else printf("%d %d\n", buf[2 * i], buf[2 * i + 1]);
            }
            free(buf);
        } else { fprintf(stderr, "unknown command %s\n", cmd); return 2; }
    }
    audio_destroy(A);
    for (int i = 0; i < AUDIO_OBJECTS; i++) free(bufs[i]);
    return 0;
}
