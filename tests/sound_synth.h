/* The counts, reloads, next links, voices and scripts below are the loaders' rule constants (note 10 2.2: code immediates the extractor writes into sounds.bin); the sample offsets and lengths are invented. */
/* tests/sound_synth.h - an invented sound object table for the tests (src/core/sound.c, docs/re/10-sound.md section 2.2): the SHAPE of the note's table (which objects exist, their voices, counts, reloads, next links
 * and scripts) with invented sample lengths, so that a block lasts a few blanks and a chain can be run through in a test. No value of a real file is in it. */
#ifndef INTERCEPTOR_TEST_SOUND_SYNTH_H
#define INTERCEPTOR_TEST_SOUND_SYNTH_H
#include <string.h>
#include "../src/core/assets.h"

static void sound_synth_obj(sound_table_t *t, int n, int file, int kind, int voice, uint32_t off, uint32_t len, int period, int count, int reload, int next, int script) {
    t->obj[n].file = (uint8_t)file; t->obj[n].kind = (uint8_t)kind; t->obj[n].voice = (uint8_t)voice; t->obj[n].volume = 0x3F;
    t->obj[n].offset = off; t->obj[n].length = len; t->obj[n].period = (uint16_t)period;
    t->obj[n].count = (int8_t)count; t->obj[n].reload = (uint8_t)reload; t->obj[n].next = (uint8_t)next; t->obj[n].script = (uint8_t)script;
}

static void sound_synth_table(sound_table_t *t) {
    memset(t, 0, sizeof *t);
    for (int i = 0; i < SOUND_OBJECTS; i++) { t->obj[i].file = SOUND_NONE; t->obj[i].kind = SOUND_NONE; t->obj[i].next = SOUND_NONE; }
    const int S = SOUND_KIND_SAMPLE, N = SOUND_NONE;
    sound_synth_obj(t, 0, 0, S, 0, 0, 4000, 358, -1, 0, N, 0);          /* the cockpit engine pair */
    sound_synth_obj(t, 1, 0, S, 1, 0, 3000, 358, -1, 0, N, 0);
    sound_synth_obj(t, 2, 1, S, 0, 0, 5000, 358, -1, 0, N, 0);          /* the external pair */
    sound_synth_obj(t, 3, 1, S, 1, 100, 4000, 358, -1, 0, N, 0);
    sound_synth_obj(t, 4, N, SOUND_KIND_TONE, 3, 0, 32, 358, -1, 0, N, SOUND_SCRIPT_TONE);
    sound_synth_obj(t, 5, 2, S, 2, 0, 1500, 358, 1, 0, N, 0);           /* the ground contact: one block */
    sound_synth_obj(t, 6, N, SOUND_KIND_NOISE, 2, 0, 0x800, 358, -1, 0, N, SOUND_SCRIPT_NOISE);
    sound_synth_obj(t, 8, 3, S, 0, 0, 6000, 358, -1, 0, N, 0);          /* the wind pair */
    sound_synth_obj(t, 9, 3, S, 1, 50, 5000, 358, -1, 0, N, 0);
    sound_synth_obj(t, 0xA, N, SOUND_KIND_NOISE, 2, 0, 0x800, 150, -1, 0, N, SOUND_SCRIPT_GUN);
    sound_synth_obj(t, 0xB, 4, S, 2, 0, 2000, 358, -1, 0, N, 0);        /* the gear */
    sound_synth_obj(t, 0xC, 1, S, 2, 0, 5000, 358, -1, 0, N, SOUND_SCRIPT_LAUNCH);
    /* the music: pairs, the even object on voice 0 and the odd on voice 1; the intro 0xD, 0xF then the loop 0x11 ... 0x21 and back to 0x11 */
    for (int v = 0; v < 2; v++) {
        sound_synth_obj(t, 0xD + v, 5, S, v, 0, 700, 358, 13, 0, 0xF + v, 0);
        sound_synth_obj(t, 0xF + v, 6, S, v, 0, 600, 358, 1, 0, 0x11 + v, 0);
        static const int loop[10][3] = { { 7, 2, 2 }, { 8, 0, 0 }, { 7, 2, 2 }, { 8, 0, 0 }, { 9, 2, 2 }, { 10, 0, 0 }, { 9, 2, 2 }, { 10, 0, 0 }, { 11, 3, 3 }, { 0, 0, 0 } };
        static const uint32_t lens[10] = { 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 2500, 0 };
        for (int k = 0; k < 9; k++) {
            const int o = 0x11 + 2 * k + v;
            const int next = k == 8 ? 0x11 + v : 0x13 + 2 * k + v;
            sound_synth_obj(t, o, loop[k][0], S, v, 0, lens[k], 358, loop[k][1], loop[k][2], next, 0);
        }
    }
    sound_synth_obj(t, 0x23, 12, S, 0, 0, 568, 179, -1, 0, N, 0);       /* the start-up pair when there is no music */
    sound_synth_obj(t, 0x24, 12, S, 1, 0, 568, 181, -1, 0, N, 0);
}
#endif
