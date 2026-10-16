/* tests/frontend/synthetic_world.c - the invented world of tests/world_synth.h as a loader for the SDL frontend (interceptor --synthetic), so that the program runs where there are no assets (CI).
 * Test-only: it is compiled into the native build/interceptor by the Makefile (-DINTERCEPTOR_SYNTHETIC) and into nothing else; the browser build has no copy of it. The tables are the invented ones of the
 * golden runner's synthflight (flight tables, mission tables, the carrier model) and the sound samples below are invented too: 13 files of a rising sawtooth and a square wave, long enough for the
 * invented sound table of tests/sound_synth.h (offsets and lengths up to 7,000 bytes). No game data is in it. */
#include "../world_synth.h"

#define SYN_SAMPLE_FILES 13
#define SYN_SAMPLE_BYTES 8000

static int syn_read(void *user, const char *path, uint8_t **out, size_t *len) {
    if (!strcmp(path, "sounds/index.txt")) {
        static const char names[] = "s00\ns01\ns02\ns03\ns04\ns05\ns06\ns07\ns08\ns09\ns10\ns11\ns12\n";
        uint8_t *b = (uint8_t *)malloc(sizeof names);
        if (!b) return -1;
        memcpy(b, names, sizeof names - 1);
        *out = b; *len = sizeof names - 1;
        return 0;
    }
    int k = -1;
    if (sscanf(path, "sounds/s%2d.raw", &k) == 1 && k >= 0 && k < SYN_SAMPLE_FILES) {
        uint8_t *b = (uint8_t *)malloc(SYN_SAMPLE_BYTES + 1);
        if (!b) return -1;
        for (int i = 0; i < SYN_SAMPLE_BYTES; i++) {
            const int period = 16 + 4 * k;   /* invented: file k is a sawtooth of 16 + 4 k samples (odd files a square wave) */
            const int ph = i % period;
            b[i] = (uint8_t)(int8_t)((k & 1) ? (ph < period / 2 ? 90 : -90) : (ph * 160 / period - 80));
        }
        *out = b; *len = SYN_SAMPLE_BYTES;
        return 0;
    }
    return synth_flight_read(user, path, out, len);
}

int synthetic_loader(game_loader_t *out, char *errbuf, size_t errlen) {
    (void)drop_file; (void)released; (void)reads; (void)errbuf; (void)errlen;   /* the shared header's helpers the golden runner uses */
    build_all();
    synth_flight = 1;
    synth_missions = 1;
    synth_invent_carrier();
    synth_mission_tables();
    out->read = syn_read; out->release = fake_release; out->user = NULL;
    return 0;
}
