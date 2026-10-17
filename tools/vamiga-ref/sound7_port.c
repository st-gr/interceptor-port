/* tools/vamiga-ref/sound7_port.c - the port's side of the milestone-7 sound captures (note 10): the port's flight started at once (the debug start), its sound library driven by a
 * script of per-tick commands, and the step's register writes printed in the format of run_golden --audio-dump: tick op voice object blank value offset length clock.
 *
 *   cc -std=c99 -O1 -Isrc/core tools/vamiga-ref/sound7_port.c build/libcore.a -o build/sound7_port -lm
 *   build/sound7_port assets SCRIPT > dump.txt
 *
 * Script (one directive per line, '#' comments):
 *   start deck|runway        the flight start (default deck: the carrier)
 *   ticks N                  the run length
 *   at N cmd ...             commands applied just before tick N (1-based), in file order:
 *     btn NAME [V]           a GAME_BTN_* press (the names of run_golden: FIRE GEAR PAUSE SHIFT_ESC FIXED_CAMERA EJECT RESCUE TOWER ...)
 *     kp K | kpup K          keypad key press and release (11 is ENTER)
 *     key N                  a typed ASCII key
 *     fkey N                 a function key
 *     cue HEX                OR the cue long V:4AC0C (the aircraft record's v4ac0c)
 *     bang A D C             sound_bang_request(amplitude V:4A922, duration V:4A921, countdown V:4A870)
 *     geardown               the gear sound's countdown byte V:4A93D = 0x83 (the key's store)
 *     ground V [C]           the ground wrapper 0x1A18C(V), C = the stepped record is the camera's (default 1)
 *     second                 one game_clock_second
 * The timer second is delivered every FLIGHT_STEP_HZ ticks as the frontend does in a counted run.  Reads the extracted assets directory; no byte of them is printed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../src/core/game.h"
#include "../../src/core/sound.h"
#include "../../src/core/aircraft.h"
#include "../../src/core/flight.h"

static int fs_read(void *user, const char *path, uint8_t **out, size_t *len) {
    char full[512];
    snprintf(full, sizeof full, "%s/%s", (const char *)user, path);
    FILE *f = fopen(full, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = (uint8_t *)malloc((size_t)n + 1);
    if (!*out || fread(*out, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(*out); return -1; }
    fclose(f);
    *len = (size_t)n;
    return 0;
}
static void fs_release(void *user, uint8_t *buf) { (void)user; free(buf); }

static const struct { const char *name; int code; } BTN[] = {
    { "FIRE", GAME_BTN_FIRE }, { "SELECT_WEAPON", GAME_BTN_SELECT_WEAPON }, { "GEAR", GAME_BTN_GEAR }, { "HOOK", GAME_BTN_HOOK }, { "FLARE", GAME_BTN_FLARE },
    { "CHAFF", GAME_BTN_CHAFF }, { "ESC", GAME_BTN_ESC }, { "RETURN", GAME_BTN_RETURN }, { "TOWER", GAME_BTN_TOWER }, { "BRAKE", GAME_BTN_BRAKE },
    { "PAUSE", GAME_BTN_PAUSE }, { "THROTTLE_UP", GAME_BTN_THROTTLE_UP }, { "THROTTLE_DOWN", GAME_BTN_THROTTLE_DOWN }, { "HUD_TOGGLE", GAME_BTN_HUD_TOGGLE },
    { "LBRACKET", GAME_BTN_LBRACKET }, { "RBRACKET", GAME_BTN_RBRACKET }, { "TARGET", GAME_BTN_TARGET }, { "ECM", GAME_BTN_ECM }, { "RADAR_RANGE", GAME_BTN_RADAR_RANGE },
    { "MESSAGE_PAGE", GAME_BTN_MESSAGE_PAGE }, { "EJECT", GAME_BTN_EJECT }, { "RESCUE", GAME_BTN_RESCUE }, { "FIXED_CAMERA", GAME_BTN_FIXED_CAMERA },
    { "REPLENISH", GAME_BTN_REPLENISH }, { "SHIFT_ESC", GAME_BTN_SHIFT_ESC }, { "SMOKE", GAME_BTN_SMOKE }, { NULL, 0 }
};

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "assets";
    FILE *f = fopen(argc > 2 ? argv[2] : "/dev/stdin", "r");
    if (!f) { fprintf(stderr, "sound7_port: no script\n"); return 1; }
    char line[512];
    struct { int tick; char text[400]; } cmd[1024];
    int n = 0, ticks = 0, runway = 0;
    while (fgets(line, sizeof line, f)) {
        char *h = strchr(line, '#');
        if (h) *h = 0;
        char a[32] = "", b[32] = "";
        int t = 0;
        if (sscanf(line, "%31s %31s", a, b) < 1) continue;
        if (!strcmp(a, "start")) runway = !strcmp(b, "runway");
        else if (!strcmp(a, "ticks")) ticks = atoi(b);
        else if (!strcmp(a, "at") && sscanf(line, "at %d %399[^\n]", &t, cmd[n].text) == 2 && n < 1023) { cmd[n].tick = t; n++; }
    }
    fclose(f);
    game_loader_t ld = { fs_read, fs_release, (void *)dir };
    char err[256] = "";
    game_t *g = game_create(&ld, err, sizeof err);
    if (!g) { fprintf(stderr, "sound7_port: %s\n", err); return 1; }
    game_debug_start_flight(g, runway ? 0 : 3, 0);
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    int ci = 0, sec = 0;
    for (int tick = 1; tick <= ticks; tick++) {
        for (; ci < n && cmd[ci].tick == tick; ci++) {
            char w[32] = "", x[32] = "", y[32] = "", z[32] = "";
            sscanf(cmd[ci].text, "%31s %31s %31s %31s", w, x, y, z);
            game_event_t ev = { GAME_EV_BUTTON, 0, 1 };
            if (!strcmp(w, "btn")) {
                int k = 0;
                while (BTN[k].name && strcmp(BTN[k].name, x)) k++;
                if (!BTN[k].name) { fprintf(stderr, "sound7_port: unknown button %s\n", x); return 1; }
                ev.code = BTN[k].code; ev.value = y[0] ? atoi(y) : 1; game_input(g, &ev);
            } else if (!strcmp(w, "kp") || !strcmp(w, "kpup")) {
                ev.code = w[2] == 'u' ? GAME_BTN_KEYPAD_UP : GAME_BTN_KEYPAD; ev.value = atoi(x); game_input(g, &ev);
            } else if (!strcmp(w, "fkey")) {
                ev.code = GAME_BTN_FKEY; ev.value = atoi(x); game_input(g, &ev);
            } else if (!strcmp(w, "key")) {
                ev.kind = GAME_EV_KEY; ev.code = 0; ev.value = atoi(x); game_input(g, &ev);
            } else if (!strcmp(w, "cue")) {
                game_debug_aircraft(g)->f.v4ac0c |= (uint32_t)strtoul(x, NULL, 16);
            } else if (!strcmp(w, "bang")) {
                sound_bang_request((sound_t *)game_debug_sound(g), atoi(x), atoi(y), atoi(z));
            } else if (!strcmp(w, "geardown")) {
                sound_gear_key((sound_t *)game_debug_sound(g));
            } else if (!strcmp(w, "ground")) {   /* the ground wrapper 0x1A18C(volume) for the stepped record: C (y) = it is the camera's, default 1 */
                sound_event((sound_t *)game_debug_sound(g), SND_GROUND, atoi(x), y[0] ? atoi(y) : 1);
            } else if (!strcmp(w, "second")) {
                game_clock_second(g);
            } else { fprintf(stderr, "sound7_port: unknown command %s\n", w); return 1; }
        }
        if (++sec >= FLIGHT_STEP_HZ) { sec = 0; game_clock_second(g); }
        game_step(g);
        game_scene(g, s);
        for (int i = 0; i < s->audio_count; i++) {
            const game_audio_cmd_t *a = &s->audio[i];
            printf("%d %d %d %d %d %d %d %d %d\n", tick, (int)a->op, a->voice, a->object, a->blank, (int)a->value, (int)a->offset, (int)a->length, (int)a->clock);
        }
    }
    free(s);
    return 0;
}
