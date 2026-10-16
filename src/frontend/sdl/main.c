/* src/frontend/sdl/main.c - SDL2 frontend entry point. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../core/game.h"
#include "../../core/flight.h"
#include "../../core/world.h"
#include "../../core/sound.h"
#include "assets.h"
#include "audio.h"
#include "video.h"
#include "hud.h"
#include "logfile.h"
#include "input.h"

/* Combat keys (note 08 11.2, note 07): j ECM, r the scope range, t the next target, y the message line's next page, Shift-E eject, Shift-F the rescue pod (mission F4), Shift-KP-Enter the fixed camera;
 * e without Shift does nothing, as in the original. w is the PORT FEATURE replenish key (game.h GAME_BTN_REPLENISH: the original's dead rearm body; no key of the original).
 *
 * Flight controls (note 05 section 2, the key bits are the held keys): LEFT/RIGHT stick X, UP/DOWN stick Y
 * (DOWN pulls back = nose up, so DOWN is +), ',' and '.' the rudder (left and right), F1..F10 the throttle presets (GAME_BTN_FKEY,
 * the core stores 12 n, F10 121, F1 again at 12 stores -1), '='/'+' and '-' the throttle keys (THROTTLE_UP/DOWN, held while the key is
 * down; the free camera keeps the absolute axis), 'g' gear, 'a' arrestor hook, 'f' flare,
 * 'c' chaff, SPACE fires the selected weapon, RETURN selects the weapon in flight (and leaves the splash and the
 * debrief), BACKSPACE or '\\' the brake (note 05 lists these two keys), 'h' toggles the HUD switch (a core event, V:4A859), KPENTER
 * requests the outside view, KP1-9 and KPDOT pick the external angle or (in the cockpit) the head
 * direction (KP4/6/2/1/7/8); all keypad keys are one core event (press and release) and the core routes them by its view, 'm' the map view, '[' and ']' (farther, closer:
 * the cockpit zoom, the external level or the map altitude by view; press and release), 'p' pause, RETURN leaves the splash. The
 * frontend keeps no view state: it maps keys to core events and draws scene.camera.view. */
static void send(game_t *g, game_event_kind_t k, int code, int value) {
    game_event_t ev = { k, code, value };
    game_input(g, &ev);
}

/* Returns 1 when the key is a stick key (handled). */
static int axis_key(game_t *g, SDL_Keycode sym, int down, int *sx, int *sy) {
    int changed = 1;
    switch (sym) {
    case SDLK_LEFT:  *sx = down ? -32767 : (*sx < 0 ? 0 : *sx); break;
    case SDLK_RIGHT: *sx = down ? 32767 : (*sx > 0 ? 0 : *sx); break;
    case SDLK_UP:    *sy = down ? -32767 : (*sy < 0 ? 0 : *sy); break;
    case SDLK_DOWN:  *sy = down ? 32767 : (*sy > 0 ? 0 : *sy); break;
    default: changed = 0;
    }
    if (changed) { send(g, GAME_EV_AXIS, GAME_AXIS_STICK_X, *sx); send(g, GAME_EV_AXIS, GAME_AXIS_STICK_Y, *sy); }
    return changed;
}

/* The rudder keys ',' and '.' (key bits 0x80 and 0x40, note 05 section 2) as the rudder axis; the throttle keys '=' and '-' as held-key events
 * (the key bits are the keys held: press sets, release clears). Returns 1 when the key is one of them. */
static int hold_key(game_t *g, SDL_Keycode k, int down, int *rud) {
    int up = k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS, dn = k == SDLK_MINUS || k == SDLK_KP_MINUS;
    if (k == SDLK_COMMA || k == SDLK_PERIOD) {
        *rud = down ? (k == SDLK_COMMA ? -32767 : 32767) : ((k == SDLK_COMMA ? *rud < 0 : *rud > 0) ? 0 : *rud);
        send(g, GAME_EV_AXIS, GAME_AXIS_RUDDER, *rud);
        return 1;
    }
    if (up || dn) { send(g, GAME_EV_BUTTON, up ? GAME_BTN_THROTTLE_UP : GAME_BTN_THROTTLE_DOWN, down); return 1; }
    return 0;
}

/* Keypad keys to the one core event (value = key: 0..9 digits, 10 KPDOT, 11 KPENTER; the release is GAME_BTN_KEYPAD_UP); the core
 * decides what it means in the current view. '[' and ']' are the zoom events (press 1, release 0). Returns 1 when the key was a view key. */
static int view_key(game_t *g, SDL_Keycode k, int down) {
    int key;
    switch (k) {
    case SDLK_KP_0: key = 0; break;
    case SDLK_KP_1: key = 1; break;
    case SDLK_KP_2: key = 2; break;
    case SDLK_KP_3: key = 3; break;
    case SDLK_KP_4: key = 4; break;
    case SDLK_KP_5: key = 5; break;
    case SDLK_KP_6: key = 6; break;
    case SDLK_KP_7: key = 7; break;
    case SDLK_KP_8: key = 8; break;
    case SDLK_KP_9: key = 9; break;
    case SDLK_KP_PERIOD: key = GAME_KP_DOT; break;
    case SDLK_KP_ENTER: key = GAME_KP_ENTER; break;
    case SDLK_LEFTBRACKET: send(g, GAME_EV_BUTTON, GAME_BTN_LBRACKET, down); return 1;
    case SDLK_RIGHTBRACKET: send(g, GAME_EV_BUTTON, GAME_BTN_RBRACKET, down); return 1;
    default: return 0;
    }
    send(g, GAME_EV_BUTTON, down ? GAME_BTN_KEYPAD : GAME_BTN_KEYPAD_UP, key);
    return 1;
}

/* Keys on the text screens (credits, menus, selections, name entry): the original's key table (note 08 3.7) knows the digits, the letters Q to P, A to L and Z to M,
 * the space and the backspace; the port sends them as GAME_EV_KEY with the upper-case ASCII value (the font has no lower case), BACKSPACE as 8. Every other key is
 * GAME_BTN_ANY (the credits take any key; the mouse and the joystick are not menu input, note 08 5.9). Returns the ASCII value or 0. */
static int text_key_char(SDL_Keycode k) {
    if (k >= SDLK_a && k <= SDLK_z) return 'A' + (int)(k - SDLK_a);
    if (k >= SDLK_0 && k <= SDLK_9) return '0' + (int)(k - SDLK_0);
    if (k == SDLK_SPACE) return ' ';
    if (k == SDLK_BACKSPACE) return 8;
    return 0;
}

static int pct(const char *s, int lo, int hi, const char *what) {
    char *end;
    long v = strtol(s, &end, 10);
    if (*end || v < lo || v > hi) { fprintf(stderr, "%s must be %d..%d, got '%s'\n", what, lo, hi, s); exit(2); }
    return (int)v;
}

/* The core steps at the port's fixed rate FLIGHT_STEP_HZ (flight.h; the original runs one step per displayed frame, so its rate depends on
 * the scene; the controller's ruling is 8 steps per second, notes 04 and 05), 125 ms per step. The frontend presents at the display rate and
 * does no interpolation: frames between two steps show the same scene. With --frames N (tests) every presented frame is one step. */
#define STEP_MS (1000 / FLIGHT_STEP_HZ)
#define MAX_CATCH_UP 4   /* steps per presented frame after a stall */

static int parse_size(const char *s, int *w, int *h) {
    char x;
    return sscanf(s, "%d%c%d", w, &x, h) == 3 && (x == 'x' || x == 'X') && *w >= 1 && *h >= 1;
}

/* --start-keys: the keys that take the game from the credits into free flight, as game.h states the normal start (note 08 5.2 to 5.7): any key on the credits
 * (RETURN), RETURN on the log-disk prompt, '2' in the menu (free flight), the location '1' (the first airfield) and the aircraft '1' (the F/A-18). Each key waits for its
 * screen (the mode) and for the screen's text (the selections show their caption after the black passes); a key that the screen did not take is repeated after
 * START_RETRY frames. A key is sent as GAME_EV_KEY (key = 1, the ASCII value) or as the button of the code. */
static const struct { game_mode_t mode; int key, code; } START_SEQ[] = {
    { GAME_MODE_CREDITS, 0, GAME_BTN_RETURN }, { GAME_MODE_LOGDISK, 0, GAME_BTN_RETURN }, { GAME_MODE_WELCOME, 0, GAME_BTN_RETURN }, { GAME_MODE_MENU, 1, '2' },
    { GAME_MODE_SELECT_LOCATION, 1, '1' }, { GAME_MODE_SELECT_AIRCRAFT, 1, '1' } };
#define START_KEYS ((int)(sizeof START_SEQ / sizeof START_SEQ[0]))
#define START_RETRY 40

/* Tests: --text-test SPEC overrides the scene each frame: items separated by ';': mode=NAME (splash credits logdisk menu select_location select_aircraft briefing log debrief
 * gameover paused flight), regime=0|1, fade=0..15, blank=0|1 (flight_blank), and any number of t=ROW:COL:PEN:SHOWN:LENGTH:STRING lines (replacing the scene's text). Returns 0, or -1. */
static int text_test_apply(game_scene_t *sc, const char *spec, char *err, size_t errlen) {
    static const struct { const char *name; game_mode_t mode; } modes[] = { { "splash", GAME_MODE_SPLASH }, { "credits", GAME_MODE_CREDITS }, { "logdisk", GAME_MODE_LOGDISK }, { "menu", GAME_MODE_MENU },
        { "select_location", GAME_MODE_SELECT_LOCATION }, { "select_aircraft", GAME_MODE_SELECT_AIRCRAFT }, { "welcome", GAME_MODE_WELCOME }, { "briefing", GAME_MODE_BRIEFING }, { "log", GAME_MODE_LOG },
        { "debrief", GAME_MODE_DEBRIEF }, { "gameover", GAME_MODE_GAMEOVER }, { "paused", GAME_MODE_PAUSED }, { "flight", GAME_MODE_FLIGHT } };
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", spec);
    sc->text_count = 0;
    for (char *item = strtok(buf, ";"); item; item = strtok(NULL, ";")) {
        int a, b, c, d, e2, n;
        char str[GAME_TEXT_LEN];
        if (!strncmp(item, "mode=", 5)) {
            n = -1;
            for (size_t i = 0; i < sizeof modes / sizeof *modes; i++) if (!strcmp(item + 5, modes[i].name)) n = (int)i;
            if (n < 0) { snprintf(err, errlen, "--text-test: unknown mode '%s'", item + 5); return -1; }
            sc->mode = modes[n].mode;
        }
        else if (sscanf(item, "regime=%d", &a) == 1) sc->palette_regime = (uint8_t)a;
        else if (sscanf(item, "fade=%d", &a) == 1) sc->fade_target = (uint8_t)a;
        else if (sscanf(item, "blank=%d", &a) == 1) sc->flight_blank = (uint8_t)a;
        else if (sscanf(item, "t=%d:%d:%d:%d:%d:%45s", &a, &b, &c, &d, &e2, str) == 6 && sc->text_count < GAME_MAX_TEXT) {
            game_text_t *t = &sc->text[sc->text_count++];
            memset(t, 0, sizeof *t);
            t->row = (uint8_t)a; t->col = (uint8_t)b; t->pen = (uint8_t)c; t->shown = (uint16_t)d; t->length = (uint16_t)e2;
            memcpy(t->text, str, strlen(str));
        }
        else { snprintf(err, errlen, "--text-test: bad item '%s'", item); return -1; }
    }
    return 0;
}

static sky_state_t hud_sky_st;   /* V:4AB7E for the hud-test path, persistent across frames like game.c's */
/* The state of main(), at file scope so that one frame can run as a callback (the browser cannot block in a loop). main() is called once: the native program is unchanged. */
static const char *log_arg = NULL, *text_test = NULL, *assets_dir = "assets", *dump = NULL, *dump_win = NULL, *dump_flash = NULL, *dump_layer = NULL, *hud_test = NULL;
static struct { SDL_Keycode k; int frame, down, repeat; } injected[64];   /* tests: --key NAME@N, --keyrep NAME@N (an auto-repeat press) and --keyup NAME@N push an SDL key event at the start of frame N (the real key path) */
static struct { SDL_Event e; int frame; } cinj[64];   /* tests: --ctrl NAME@N / --ctrlup NAME@N push the SDL controller event of the table (input.c) at the start of frame N: the path of a real controller */
static int ninj = 0, ncinj = 0, runway = 0, no_log = 0;
static SDL_GameController *pad = NULL;   /* the first controller attached (the table is the same for any) */
static int frames = -1, win_w = 0, win_h = 0, resize_at = -1, resize_w = 0, resize_h = 0, scale = 3, flight = 0, start_keys = 0, logdisk = 0, freecam = 0, external = 1, sx = 0, sy = 0, rud = 0, thr = -32768, psx = 0, psy = 0, pthr = 0, thr_given = 0;
static long seed = -1;   /* --seed N pins the placement generator (the mission-start selector, note 07 2.4); without it the wall clock seeds it, like the original's timer read */
static char err[256] = "";
static game_loader_t loader;
static game_t *g;
static int synthetic = 0;   /* --synthetic: the invented world of tests/world_synth.h instead of the extracted assets (the native build of the Makefile only; CI has no assets) */
#ifdef INTERCEPTOR_SYNTHETIC
int synthetic_loader(game_loader_t *out, char *errbuf, size_t errlen);   /* tests/frontend/synthetic_world.c */
#endif
static char logpath[512] = "";
static int use_log = 0, save_warned = 0;
static video_t *v;
static game_scene_t scene;
static hud_test_t *htest;
static int running = 1, rc_bad = 0, flight_started = 0, shift_held = 0, start_stage = 0, start_last = -START_RETRY, mouse_dy = 0, sec_steps = 0, fn = 0;
static uint32_t last_ms, acc_ms = 0, sec_ms = 0, sec_last;
/* ---- The sound (note 10 section 9.2; plan Task 6). The core hands the register writes of every step (scene.audio); the mixer (audio.c) renders them. Two mixers may run: the device's, at the rate the
 * device gave (rendered by the callback under the device lock; the game thread applies under SDL_LockAudioDevice), and the --wav one, at 44,100 Hz stereo, rendered by the game thread, the time of one step's
 * library blanks per step (125 ms on average), so the file depends neither on a device nor on the wall clock. No device (--no-audio, a failed open) is not an error: the game runs silent. ---- */
#define WAV_RATE 44100
static int no_audio = 0, no_filter = 0, dev_channels = 2;
static const char *wav_path = NULL, *alog_path = NULL;
static FILE *alog = NULL;          /* --audio-log: one line per register write (tick, op, voice, object, blank, value, offset, length, clock), the format of run_golden --audio-dump */
static uint64_t alog_steps = 0;
static SDL_AudioDeviceID adev = 0;
static audio_t *dev_au = NULL, *wav_au = NULL;
static int16_t *wav_buf = NULL;
static size_t wav_frames = 0, wav_cap = 0;
static uint64_t wav_steps = 0;
static int8_t *sample_data[SOUND_OBJECTS];     /* the file-backed samples, by object */
static uint32_t sample_len[SOUND_OBJECTS];
static const int8_t *synth_data[SOUND_OBJECTS];
static uint32_t synth_len[SOUND_OBJECTS];

static void audio_callback(void *user, Uint8 *stream, int len) {
    memset(stream, 0, (size_t)len);
    (void)user;
    audio_t *a = dev_au;   /* created before the device is unpaused, destroyed after it is closed */
    if (a) audio_render(a, (int16_t *)stream, len / (int)(sizeof(int16_t) * (size_t)dev_channels));
}

static void install_samples(audio_t *a) {
    for (int o = 0; o < SOUND_OBJECTS; o++) {
        if (sample_data[o]) audio_set_sample(a, o, sample_data[o], sample_len[o]);
        else if (synth_data[o]) audio_set_sample(a, o, synth_data[o], synth_len[o]);
    }
}

/* Opens the device (once; later calls resume it). Called at start and, in the browser, from the page's Start click: a user gesture, which the browser wants before it lets sound play. */
static void audio_start(void) {
    if (no_audio) return;
    if (adev) { SDL_PauseAudioDevice(adev, 0); return; }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { fprintf(stderr, "audio: no sound (%s)\n", SDL_GetError()); no_audio = 1; return; }
    SDL_AudioSpec want, got;
    SDL_zero(want); SDL_zero(got);
    want.freq = WAV_RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audio_callback;
    adev = SDL_OpenAudioDevice(NULL, 0, &want, &got, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);   /* two channels asked, never changed: a device with more (or one) gets SDL's conversion, so the mixer (1 or 2 channels, audio.c) always fits */
    if (!adev) { fprintf(stderr, "audio: no sound (%s)\n", SDL_GetError()); no_audio = 1; return; }
    dev_channels = got.channels;
    dev_au = got.format == AUDIO_S16SYS ? audio_create(got.freq, got.channels) : NULL;
    if (dev_au) { audio_set_clock(dev_au, AUDIO_PORT_CLOCK); audio_set_live(dev_au, 1); }   /* the game's clock is the port's 50 blanks a second; the steps arrive on the wall clock (note 10 9.2 live-play rules) */
    if (!dev_au) {
        fprintf(stderr, "audio: no sound (device gave %d Hz, %d channels, format 0x%x)\n", got.freq, got.channels, (unsigned)got.format);
        SDL_CloseAudioDevice(adev); adev = 0; no_audio = 1; return;
    }
    if (no_filter) audio_set_filter(dev_au, 0);
    install_samples(dev_au);
    SDL_PauseAudioDevice(adev, 0);
}

/* The sample buffers (Review Focus 2): the file-backed objects from the sample files by the table, the synthesised ones from the core. A missing or short file, or a range past it, leaves the object without
 * a sample: it plays nothing, one line on stderr, the game goes on. */
static void samples_load(void) {
    char e2[256];
    static sound_table_t sound_tab;
    if (assets_load_sounds(&loader, &sound_tab, e2, sizeof e2) != 0) { fprintf(stderr, "audio: no sound objects (%s)\n", e2); return; }
    const sound_t *snd = (const sound_t *)game_debug_sound(g);
    uint8_t present[SOUND_OBJECTS];
    memset(present, 1, sizeof present);
    for (int o = 0; o < SOUND_OBJECTS; o++) {
        const uint8_t kind = sound_tab.obj[o].kind;
        if (kind == SOUND_NONE) continue;
        if (kind == SOUND_KIND_SAMPLE) {
            int8_t *d = NULL; uint32_t n = 0;
            if (sound_tab.obj[o].file == SOUND_NONE || assets_load_sample(&loader, sound_tab.obj[o].file, &d, &n) != 0) {
                fprintf(stderr, "audio: sound object 0x%X: its sample file cannot be read; the game goes without it (note 10 2.5)\n", o);
                present[o] = 0;
                continue;
            }
            const uint64_t need = (uint64_t)sound_tab.obj[o].offset + sound_tab.obj[o].length;
            if (need > n) {   /* note 10 2.5: "a file that is short is not detected (the object is as long as the file)": the original plays the object, so the port keeps it enabled and plays the file's bytes. The core does
                               * not clamp the lengths (the register writes carry the table's); the bytes past the file's end are unknown (the original's DMA reads whatever follows the buffer in memory), PORT CHOICE C: silence. */
                int8_t *pd = (int8_t *)realloc(d, (size_t)need);
                if (!pd) { free(d); fprintf(stderr, "audio: sound object 0x%X: out of memory\n", o); present[o] = 0; continue; }
                memset(pd + n, 0, (size_t)(need - n));
                fprintf(stderr, "audio: sound object 0x%X: its sample file is %u bytes, shorter than the %u + %u the table asks; played as long as the file, then silence (note 10 2.5)\n",
                        o, (unsigned)n, (unsigned)sound_tab.obj[o].offset, (unsigned)sound_tab.obj[o].length);
                d = pd; n = (uint32_t)need;
            }
            sample_data[o] = d; sample_len[o] = n;
        } else if (!sound_buffer(snd, o, &synth_data[o], &synth_len[o])) {
            synth_data[o] = NULL; synth_len[o] = 0;
        }
    }
    game_sound_objects_available(g, present);   /* before the first pass: the loaders of the original leave out what they could not build, and the wrappers fall back (note 10 2.5) */
    if (dev_au) { if (adev) SDL_LockAudioDevice(adev); install_samples(dev_au); if (adev) SDL_UnlockAudioDevice(adev); }
    if (wav_au) install_samples(wav_au);
}

/* One game step's commands to the mixers (also none: every step is applied), and the --wav block of the step. */
static void audio_step(const game_audio_cmd_t *cmds, int count) {
    if (alog) {
        alog_steps++;
        for (int i = 0; i < count; i++) {
            const game_audio_cmd_t *c = &cmds[i];
            fprintf(alog, "%llu %u %u %u %u %u %u %u %u\n", (unsigned long long)alog_steps, c->op, c->voice, c->object, c->blank, c->value, (unsigned)c->offset, (unsigned)c->length, (unsigned)c->clock);
        }
    }
    if (dev_au) {
        SDL_LockAudioDevice(adev);
        audio_apply(dev_au, cmds, count);
        SDL_UnlockAudioDevice(adev);
    }
    if (wav_au) {
        audio_apply(wav_au, cmds, count);
        /* the step's output is the time of its library blanks (6, 6, 6 or 7, floor(25 k / 4) to floor(25 (k + 1) / 4), so that its writes, stamped by those blanks, are applied before the output reaches them):
         * 5,292 or 6,174 frames at 44,100 Hz, 125 ms a step on average (882 frames a blank of the port's 50 a second) */
        const uint64_t b0 = (wav_steps * 25) / 4, b1 = ((wav_steps + 1) * 25) / 4;
        const uint64_t f0 = (b0 * AUDIO_BLANK_CLOCKS * WAV_RATE + AUDIO_PORT_CLOCK - 1) / AUDIO_PORT_CLOCK, f1 = (b1 * AUDIO_BLANK_CLOCKS * WAV_RATE + AUDIO_PORT_CLOCK - 1) / AUDIO_PORT_CLOCK;
        const size_t n = (size_t)(f1 - f0);
        wav_steps++;
        if (wav_frames + n > wav_cap) {
            size_t cap = wav_cap ? wav_cap * 2 : (size_t)WAV_RATE * 16;
            while (cap < wav_frames + n) cap *= 2;
            int16_t *nb = (int16_t *)realloc(wav_buf, cap * 2 * sizeof(int16_t));
            if (!nb) { fprintf(stderr, "--wav: out of memory; the file stops here\n"); audio_destroy(wav_au); wav_au = NULL; return; }
            wav_buf = nb; wav_cap = cap;
        }
        audio_render(wav_au, wav_buf + wav_frames * 2, (int)n);
        wav_frames += n;
    }
}

static void put32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static int wav_write(void) {
    FILE *f = fopen(wav_path, "wb");
    if (!f) return -1;
    unsigned char h[44];
    const uint32_t bytes = (uint32_t)(wav_frames * 4);
    memcpy(h, "RIFF", 4); put32(h + 4, 36 + bytes); memcpy(h + 8, "WAVEfmt ", 8); put32(h + 16, 16);
    h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0; put32(h + 24, WAV_RATE); put32(h + 28, WAV_RATE * 4); h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4); put32(h + 40, bytes);
    int ok = fwrite(h, 1, 44, f) == 44;
    for (size_t i = 0; ok && i < wav_frames * 2; i++) {   /* little-endian samples whatever the host */
        unsigned char b[2] = { (unsigned char)(uint16_t)wav_buf[i], (unsigned char)((uint16_t)wav_buf[i] >> 8) };
        ok = fwrite(b, 1, 2, f) == 2;
    }
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EMSCRIPTEN_KEEPALIVE void web_audio_unlock(void) { audio_start(); }
/* The game ended by itself (ESC on the credits): the page is told through Module.onGameEnd, if it defines one (the page shows its picker or a restart); web_frame stops the loop. */
EMSCRIPTEN_KEEPALIVE void web_game_end(void) { emscripten_run_script("if (typeof Module !== 'undefined' && Module.onGameEnd) Module.onGameEnd();"); }
/* A save point of the flight log was written to the log file: the page writes the log directory on to the browser's storage (IDBFS) through Module.onLogSaved. */
EMSCRIPTEN_KEEPALIVE void web_log_saved(void) { emscripten_run_script("if (typeof Module !== 'undefined' && Module.onLogSaved) Module.onLogSaved();"); }
#endif

/* One pass of the main loop: the events, the steps the elapsed time asks for, the scene, the frame. Returns 0 when the program is to end (a counted run done, a quit, a bad test option). */
static int frame_once(void) {
    if (!(running && (frames < 0 || fn < frames))) return 0;
    SDL_Event e;
    int send_opts = 0;
    if (flight && fn == 0) { game_debug_start_flight(g, runway ? 0 : 3, 0); send_opts = 1; }   /* the flight at once (game.h): the carrier deck of free flight, with --runway the first airfield */
    if (start_keys && start_stage < START_KEYS) {
        for (int i = start_stage + 1; i < START_KEYS; i++)
            if (scene.mode == START_SEQ[i].mode) { start_stage = i; start_last = -START_RETRY; }   /* a later screen is on: the earlier keys were taken */
        if (scene.mode == START_SEQ[start_stage].mode && scene.text_count && fn - start_last >= START_RETRY) {
            if (START_SEQ[start_stage].key) send(g, GAME_EV_KEY, 0, START_SEQ[start_stage].code);
            else send(g, GAME_EV_BUTTON, START_SEQ[start_stage].code, 1);
            start_last = fn;
        }
    }
    if (start_keys && !flight_started && scene.mode == GAME_MODE_FLIGHT) send_opts = 1;
    if (send_opts) {   /* the flight is on: the stick, the throttle and the view the options ask for */
        flight_started = 1;
        send(g, GAME_EV_AXIS, GAME_AXIS_STICK_X, psx * 32767 / 100);
        send(g, GAME_EV_AXIS, GAME_AXIS_STICK_Y, psy * 32767 / 100);
        if (thr_given || freecam) send(g, GAME_EV_AXIS, GAME_AXIS_THROTTLE, thr);   /* the flight start keeps T = 9, P = Q = 72 unless asked */
        if (external) send(g, GAME_EV_BUTTON, GAME_BTN_KEYPAD, GAME_KP_ENTER);   /* default view: outside (chase) */
    }
    for (int j = 0; j < ninj; j++) {
        if (injected[j].frame != fn) continue;
        SDL_Event ke;
        memset(&ke, 0, sizeof ke);
        ke.type = injected[j].down ? SDL_KEYDOWN : SDL_KEYUP;
        ke.key.keysym.sym = injected[j].k;
        ke.key.state = injected[j].down ? SDL_PRESSED : SDL_RELEASED;
        ke.key.repeat = injected[j].repeat;
        SDL_PushEvent(&ke);
    }
    for (int j = 0; j < ncinj; j++)
        if (cinj[j].frame == fn) SDL_PushEvent(&cinj[j].e);
    ctrl_key_t pend[8];   /* the keys the controller events of this frame made, handled next in order (as if the keyboard had sent them at the same time) */
    int pn = 0, pi = 0;
    for (;;) {
        if (pi < pn) {
            memset(&e, 0, sizeof e);
            e.type = pend[pi].down ? SDL_KEYDOWN : SDL_KEYUP;
            e.key.keysym.sym = pend[pi].key;
            e.key.state = pend[pi].down ? SDL_PRESSED : SDL_RELEASED;
            pi++;
        } else if (!SDL_PollEvent(&e)) break;
        if (e.type == SDL_QUIT) running = 0;
        if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) pad = SDL_GameControllerOpen(e.cdevice.which);
        if (e.type == SDL_CONTROLLERDEVICEREMOVED && pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) { SDL_GameControllerClose(pad); pad = NULL; }
        if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP || e.type == SDL_CONTROLLERAXISMOTION) {   /* the table (input.c): the controller's control is a key of the keyboard, sent as that key event, so the keyboard's code below makes the core events */
            ctrl_key_t ck[4];
            const int nk = input_ctrl_translate(&e, ck, 4);
            const int in_play = scene.mode == GAME_MODE_FLIGHT || scene.mode == GAME_MODE_PAUSED;
            if (pi == pn) pi = pn = 0;
            for (int j = 0; j < nk && pn < 8; j++) {
                if (ck[j].down && !in_play && !ck[j].any_screen) continue;   /* the joystick is not menu input (note 08 5.9) */
                pend[pn++] = ck[j];
            }
            continue;
        }
        if (e.type == SDL_MOUSEMOTION) mouse_dy += e.motion.yrel;   /* the pointer throttle (note 05 17.4): summed per frame */
        /* The shift flag of the original (note 08 11.1 quirk (b)): a shift press sets it, its release leaves it alone, and every press or release of another key clears it after that key's handling,
         * so "shift, then E" is a chord and "shift down, any key up" cancels it. The ALT and command keys (the Amiga keys) are flag keys too: they leave it alone. */
        const SDL_Keycode ek = (e.type == SDL_KEYUP || e.type == SDL_KEYDOWN) ? e.key.keysym.sym : 0;
        const int flag_key = ek == SDLK_LSHIFT || ek == SDLK_RSHIFT || ek == SDLK_LALT || ek == SDLK_RALT || ek == SDLK_LGUI || ek == SDLK_RGUI;
        if (e.type == SDL_KEYUP && (scene.mode == GAME_MODE_FLIGHT || scene.mode == GAME_MODE_PAUSED)) {   /* the key bits are the held keys (note 05 section 2): every held key is released; the screens outside the flight take press events only (game.h) */
            if (!axis_key(g, e.key.keysym.sym, 0, &sx, &sy) && !hold_key(g, e.key.keysym.sym, 0, &rud)) (void)view_key(g, e.key.keysym.sym, 0);
            if (e.key.keysym.sym == SDLK_SPACE) send(g, GAME_EV_BUTTON, GAME_BTN_FIRE, 0);   /* the release lets the trigger go (note 07 7.1; the release is taken with or without the shift) */
        }
        if (e.type == SDL_KEYUP && !flag_key) shift_held = 0;   /* the release of another key clears the flag */
        if (e.type == SDL_KEYDOWN) {
            SDL_Keycode k = e.key.keysym.sym;
            if (k == SDLK_LSHIFT || k == SDLK_RSHIFT) { if (!e.key.repeat) shift_held = 1; continue; }   /* the key repeat of a held shift must not set the one-event flag again: "hold shift, E, E" acts once (note 08 11.1 (b), 5.8) */
            if (k == SDLK_LALT || k == SDLK_RALT || k == SDLK_LGUI || k == SDLK_RGUI) continue;   /* the ALT and command keys are modifiers, not keys: they do not leave the credits (note 08 5.2) */
            if (e.key.repeat) continue;   /* a held key acts once, on every screen (the original's key queue holds the press events only, note 08 5.8): a held RETURN or ESC must not carry into the next screen */
            const int shift = shift_held;   /* the one-event flag, not the held state of the shift key */
            shift_held = 0;                 /* cleared by every other key event, this one included, after its handling */
            const int play = scene.mode == GAME_MODE_FLIGHT || scene.mode == GAME_MODE_PAUSED;
            if (k == SDLK_ESCAPE) {   /* ESC is the game's own key everywhere (the core's ESC flow); only the credits, the first screen, take it as quit, so the program stays quittable */
                if (scene.mode == GAME_MODE_SPLASH || scene.mode == GAME_MODE_CREDITS) running = 0;
                else send(g, GAME_EV_BUTTON, shift ? GAME_BTN_SHIFT_ESC : GAME_BTN_ESC, 1);
                continue;
            }
            if (!play) {   /* the text screens: the letters, digits, space and backspace as characters, RETURN, anything else "a key" */
                int ch = text_key_char(k);
                if (ch == '2' && shift && scene.mode == GAME_MODE_LOG) ch = GAME_KEY_SHIFT2;   /* RESET on the flight-log screen (note 08 10.6) */
                if (ch) send(g, GAME_EV_KEY, 0, ch);
                else if (view_key(g, k, 1)) {}   /* the keypad: KP1..KP8 act as the digits, KPENTER skips the approach (game.h) */
                else send(g, GAME_EV_BUTTON, k == SDLK_RETURN || k == SDLK_KP_ENTER ? GAME_BTN_RETURN : GAME_BTN_ANY, 1);
                continue;
            }
            int handled = axis_key(g, k, 1, &sx, &sy);
            int fk = k >= SDLK_F1 && k <= SDLK_F10 ? (int)(k - SDLK_F1) + 1 : 0;
            if (fk) { send(g, GAME_EV_BUTTON, GAME_BTN_FKEY, fk); handled = 1; }
            int up = k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS, down = k == SDLK_MINUS || k == SDLK_KP_MINUS;
            if (freecam && (up || down)) {   /* the free camera has no key bits: the absolute axis steps */
                thr += up ? 4096 : -4096;
                thr = thr > 32767 ? 32767 : thr < -32768 ? -32768 : thr;
                send(g, GAME_EV_AXIS, GAME_AXIS_THROTTLE, thr);
                handled = 1;
            } else if (hold_key(g, k, 1, &rud)) handled = 1;
            if (k == SDLK_KP_ENTER && shift) send(g, GAME_EV_BUTTON, GAME_BTN_FIXED_CAMERA, 1);   /* Shift-KP-Enter: the fixed camera (note 07 6.3) */
            else if (view_key(g, k, 1)) {}
            else if (k == SDLK_m) send(g, GAME_EV_BUTTON, GAME_BTN_TOWER, 1);
            else if (k == SDLK_p) send(g, GAME_EV_BUTTON, GAME_BTN_PAUSE, 1);
            else if (k == SDLK_BACKSLASH || k == SDLK_BACKSPACE) send(g, GAME_EV_BUTTON, GAME_BTN_BRAKE, 1);
            else if (k == SDLK_g) send(g, GAME_EV_BUTTON, GAME_BTN_GEAR, 1);
            else if (k == SDLK_a) send(g, GAME_EV_BUTTON, GAME_BTN_HOOK, 1);
            else if (k == SDLK_f) send(g, GAME_EV_BUTTON, shift ? GAME_BTN_RESCUE : GAME_BTN_FLARE, 1);   /* Shift-F: the rescue pod of mission F4 */
            else if (k == SDLK_e) { if (shift) send(g, GAME_EV_BUTTON, GAME_BTN_EJECT, 1); }   /* Shift-E ejects; E alone does nothing (note 08 11.2) */
            else if (k == SDLK_j) send(g, GAME_EV_BUTTON, GAME_BTN_ECM, 1);
            else if (k == SDLK_r) send(g, GAME_EV_BUTTON, GAME_BTN_RADAR_RANGE, 1);
            else if (k == SDLK_t) send(g, GAME_EV_BUTTON, GAME_BTN_TARGET, 1);
            else if (k == SDLK_y) send(g, GAME_EV_BUTTON, GAME_BTN_MESSAGE_PAGE, 1);
            else if (k == SDLK_w) send(g, GAME_EV_BUTTON, GAME_BTN_REPLENISH, 1);   /* PORT FEATURE: replenish (the original's dead rearm body) */
            else if (k == SDLK_c) send(g, GAME_EV_BUTTON, GAME_BTN_CHAFF, 1);
            else if (k == SDLK_SPACE) { if (!shift) send(g, GAME_EV_BUTTON, GAME_BTN_FIRE, 1); }   /* SPACE with the shift flag is ignored (0x1DF48, note 07 7.1) */
            else if (k == SDLK_h) send(g, GAME_EV_BUTTON, GAME_BTN_HUD_TOGGLE, 1);
            else if (k == SDLK_s) send(g, GAME_EV_BUTTON, GAME_BTN_SMOKE, 1);   /* the training flight school's smoke key (note 08 8.6); no effect elsewhere */
            else if (k == SDLK_RETURN) send(g, GAME_EV_BUTTON, scene.mode == GAME_MODE_FLIGHT ? GAME_BTN_SELECT_WEAPON : GAME_BTN_RETURN, 1);
            else if (!handled) send(g, GAME_EV_BUTTON, GAME_BTN_ANY, 1);
        }
    }
    if (mouse_dy) { send(g, GAME_EV_AXIS, GAME_AXIS_POINTER_DY, mouse_dy); mouse_dy = 0; }
    int steps = 1;
    if (frames < 0) {   /* live: as many steps as the elapsed time asks for, at the fixed rate */
        uint32_t now = SDL_GetTicks();
        acc_ms += now - last_ms; last_ms = now;
        steps = 0;
        while (acc_ms >= STEP_MS && steps < MAX_CATCH_UP) { acc_ms -= STEP_MS; steps++; }
        if (acc_ms >= STEP_MS) acc_ms = 0;
    }
    int flash = 0;
    for (int s = 0; s < steps; s++) {
        if (frames >= 0) { if (++sec_steps >= FLIGHT_STEP_HZ) { sec_steps = 0; game_clock_second(g); } }   /* a counted run: one second is FLIGHT_STEP_HZ steps */
        game_step(g); flash |= game_flash(g);
        if (dev_au || wav_au || alog) {   /* the commands of every step, not only of the last of the frame */
            int na = 0;
            const game_audio_cmd_t *ac = game_audio_commands(g, &na);
            audio_step(ac, na);
        }
    }
    if (frames < 0) { uint32_t now = SDL_GetTicks(); sec_ms += now - sec_last; sec_last = now; while (sec_ms >= 1000) { sec_ms -= 1000; game_clock_second(g); } }   /* live: the wall-clock second */
    game_scene(g, &scene);
    if (scene.log_dirty) {   /* a save point of the flight log (10.8): the file gets the container; the core is told either way */
        unsigned char out[GAME_LOG_FILE_SIZE];
        const int len = game_log_export(g, out, sizeof out);
        if (use_log && len > 0 && logfile_save(logpath, out, (size_t)len) != 0 && !save_warned) {
            fprintf(stderr, "flight log %s: cannot be written; the log stays in memory\n", logpath);
            save_warned = 1;
        }
#ifdef __EMSCRIPTEN__
        else if (use_log && len > 0) web_log_saved();
#endif
        game_log_ack(g);
    }
    scene.flash_frames = (uint8_t)(scene.flash_frames | flash);
    if (hud_test) {
        if (hud_test_apply(htest, &scene, fn + 1, hud_test, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); rc_bad = 2; return 0; }
        world_sky_for_camera(&scene.sky, &scene.camera, 0x80, hud_window_last_row(&scene.camera), &hud_sky_st, scene.tick, -1);   /* the test overrode the camera: the sky follows it (no mesh) */
        hud_test_post(htest, &scene);
    }
    if (text_test && text_test_apply(&scene, text_test, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); rc_bad = 2; return 0; }
    if (fn == resize_at) video_set_window_size(v, resize_w, resize_h);
    video_render(v, &scene, &loader);
#ifndef __EMSCRIPTEN__
    if (frames < 0) SDL_Delay(4);   /* the software renderer has no vsync (the browser paces the frames itself) */
#endif
    fn++;
    return 1;
}

static int finish(void) {
    if (!running && frames >= 0) fprintf(stderr, "quit\n");   /* tests: a counted run that ended early (the window closed or ESC on the credits) says so */
    int rc = rc_bad;
    if (dump_layer && video_dump_layer(v, dump_layer) != 0) { fprintf(stderr, "could not write %s\n", dump_layer); rc = 1; }
    if (dump_flash && video_dump_flash_ppm(v, dump_flash) != 0) { fprintf(stderr, "no flash frame to write to %s\n", dump_flash); rc = 1; }
    if (dump_win && video_dump_window_ppm(v, dump_win) != 0) { fprintf(stderr, "could not write %s\n", dump_win); rc = 1; }
    if (dump && video_dump_ppm(v, dump) != 0) { fprintf(stderr, "could not write %s\n", dump); rc = 1; }
    if (alog && fclose(alog) != 0) { fprintf(stderr, "could not write %s\n", alog_path); rc = 1; }
    if (wav_path && wav_write() != 0) { fprintf(stderr, "could not write %s\n", wav_path); rc = 1; }
    if (adev) SDL_CloseAudioDevice(adev);
    audio_destroy(dev_au); audio_destroy(wav_au);
    free(wav_buf);
    for (int o = 0; o < SOUND_OBJECTS; o++) free(sample_data[o]);
    hud_test_destroy(htest);
    video_destroy(v);
    game_destroy(g);
    assets_close(&loader);
    return rc;
}
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
static void web_frame(void) {
    if (!frame_once()) { emscripten_cancel_main_loop(); web_game_end(); (void)finish(); }
}
#endif
int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--assets") && i + 1 < argc) assets_dir = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--resize-at") && i + 2 < argc) {   /* tests: resize the window after frame N */
            resize_at = atoi(argv[++i]);
            if (!parse_size(argv[++i], &resize_w, &resize_h)) { fprintf(stderr, "--resize-at N WxH\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--window-size") && i + 1 < argc) {
            if (!parse_size(argv[++i], &win_w, &win_h)) { fprintf(stderr, "--window-size must be WxH, got '%s'\n", argv[i]); return 2; }
        }
        else if (!strcmp(argv[i], "--flight") || !strcmp(argv[i], "--autostart")) flight = 1;   /* the flight at once (game_debug_start_flight) */
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = atol(argv[++i]);
        else if (!strcmp(argv[i], "--logdisk") && i + 1 < argc) logdisk = atoi(argv[++i]);   /* the log-disk check result the frontend reports (note 08 5.3): 0 usable, no prompt (default); 1 to 3 show the prompt */
        else if (!strcmp(argv[i], "--start-keys")) start_keys = 1;   /* the real key path from the credits into free flight (START_SEQ) */
        else if (!strcmp(argv[i], "--freecam")) { flight = 1; freecam = 1; }   /* the milestone-2 free camera */
        else if (!strcmp(argv[i], "--stick") && i + 2 < argc) { psx = pct(argv[i + 1], -100, 100, "--stick X"); psy = pct(argv[i + 2], -100, 100, "--stick Y"); i += 2; }
        else if (!strcmp(argv[i], "--throttle") && i + 1 < argc) { pthr = pct(argv[++i], 0, 100, "--throttle"); thr_given = 1; }
        else if (!strcmp(argv[i], "--cockpit")) external = 0;
        else if (!strcmp(argv[i], "--runway")) runway = 1;   /* the flight starts on the runway (start block 0) instead of the carrier deck */
        else if ((!strcmp(argv[i], "--key") || !strcmp(argv[i], "--keyup") || !strcmp(argv[i], "--keyrep")) && i + 1 < argc) {
            const int down = strcmp(argv[i], "--keyup") != 0, rep = !strcmp(argv[i], "--keyrep");
            char *at = strrchr(argv[++i], '@');
            SDL_Keycode kc = at ? SDL_GetKeyFromName((*at = 0, argv[i])) : SDLK_UNKNOWN;
            if (!at || kc == SDLK_UNKNOWN || ninj >= 64) { fprintf(stderr, "--key/--keyup NAME@FRAME (an SDL key name such as \"Keypad 6\", \"[\" or \"M\")\n"); return 2; }
            injected[ninj].k = kc; injected[ninj].frame = atoi(at + 1); injected[ninj].down = down; injected[ninj].repeat = rep; ninj++;
        }
        else if ((!strcmp(argv[i], "--ctrl") || !strcmp(argv[i], "--ctrlup")) && i + 1 < argc) {
            const int down = !strcmp(argv[i], "--ctrl");
            char *at = strrchr(argv[++i], '@');
            const char *colon = at ? strchr(at, ':') : NULL;   /* NAME@N:MAG pushes an axis at the deflection MAG (0..32767; the dead zone tests) */
            const int mag = colon ? atoi(colon + 1) : 32767;
            if (!at || ncinj >= 64 || mag < 0 || mag > 32767 || !input_ctrl_inject((*at = 0, argv[i]), down, mag, &cinj[ncinj].e)) { fprintf(stderr, "--ctrl/--ctrlup NAME@FRAME[:MAG] (NAME one of: %s)\n", input_ctrl_names()); return 2; }
            cinj[ncinj].frame = atoi(at + 1); ncinj++;
        }
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) log_arg = argv[++i];
        else if (!strcmp(argv[i], "--no-log")) no_log = 1;
        else if (!strcmp(argv[i], "--synthetic")) {
#ifdef INTERCEPTOR_SYNTHETIC
            synthetic = 1;
#else
            fprintf(stderr, "--synthetic: this build has no invented world (the native build has it, the browser build does not)\n"); return 2;
#endif
        }
        else if (!strcmp(argv[i], "--no-audio")) no_audio = 1;
        else if (!strcmp(argv[i], "--no-filter")) no_filter = 1;
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--audio-log") && i + 1 < argc) alog_path = argv[++i];
        else if (!strcmp(argv[i], "--hud-test") && i + 1 < argc) hud_test = argv[++i];
        else if (!strcmp(argv[i], "--text-test") && i + 1 < argc) text_test = argv[++i];
        else if (!strcmp(argv[i], "--dump-window") && i + 1 < argc) dump_win = argv[++i];
        else if (!strcmp(argv[i], "--dump-layer") && i + 1 < argc) dump_layer = argv[++i];
        else if (!strcmp(argv[i], "--dump-flash") && i + 1 < argc) dump_flash = argv[++i];
        else { fprintf(stderr, "usage: interceptor [--assets DIR] [--frames N] [--dump FILE.ppm] [--scale K | --window-size WxH]\n"
                               "                   [--flight|--autostart|--freecam] [--cockpit]\n                   [--stick X Y] [--throttle P]\n"
                               "  --scale K   initial window size 320K x 200K (the window is resizable; the frame is scaled by the largest integer factor, letterboxed)\n"
                               "  --window-size WxH   initial window size in pixels (instead of --scale)\n"
                               "  --resize-at N WxH   resize the window after frame N (tests of the resize path)\n"
                               "  --flight    start the flight at once (game_debug_start_flight: the carrier deck, the F/A-18; no screens)\n"
                               "  --logdisk N    the log-disk check result (0 usable log, no prompt, the default; 1 no disk, 2 not initialised, 3 not write enabled: the prompt of note 08 5.3)\n"
                               "  --start-keys   take the screens' own way to free flight: the keys of START_SEQ, each when its screen is on (credits, welcome (only with a flight log), menu 2, location 1, aircraft 1)\n"
                               "  --log FILE   load the flight log from FILE at start and save it there at the save points (note 08 10.8); a counted run (--frames) uses no log unless this is given\n"
                               "  --no-audio   open no sound device (the mixer still runs for --wav)\n"
                               "  --no-filter   leave the A500 output filters off (default: on, as on the reference machine, note 10 1.5)\n"
                               "  --audio-log FILE   write the register writes of the sound library, one line per write: step op voice object blank value offset length clock (tests)\n"
                               "  --wav FILE   write the run's audio to FILE: 16-bit stereo 44,100 Hz, the time of each step's 6 or 7 blanks (125 ms on average), at exit\n"
                               "  --synthetic   run the invented world of tests/world_synth.h (flight tables, missions, sound table and samples; no game data) instead of the assets; the native build only\n"
                               "  --no-log   use no flight log: no welcome or callsign entry, nothing is read or written (the default of --flight and --freecam)\n"
                               "  --freecam   like --flight but with the debug free camera (stick turns, throttle sets the speed)\n"
                               "  --stick X Y   hold the stick at X (right +) and Y (nose up +) percent, -100..100, from frame 1 (use with --freecam)\n"
                               "  --throttle P  set the throttle lever to P percent from frame 1 (default: not sent; the flight keeps its start throttle, the free camera idles)\n"
                               "  --runway   start the flight on the runway (start block 0) instead of the carrier deck\n"
                               "  --cockpit   start in the cockpit view (default: outside view; KPENTER toggles)\n"
                               "  --key NAME@N, --keyup NAME@N   push the key press / release (an SDL key name: \"Keypad 6\", \"[\", \"M\") at the start of frame N, for tests\n"
                               "  --ctrl NAME@N[:MAG], --ctrlup NAME@N   push the game controller's control press / release (a name of the table in input.c: %s) at the start of frame N, for tests\n"
                               "  --seed N   pin the placement generator of the mission start (note 07 2.4); without it the clock seeds it\n"
                               "  --text-test SPEC   override the scene's mode, palette regime, fade target and text lines each frame (see main.c), for tests\n"
                               "  --keyrep NAME@N   an auto-repeat key press at the start of frame N, for tests\n"
                               "  --hud-test SPEC   override the scene's cockpit state each frame: name=value,... (see hud.h), for tests\n"
                               "  --dump-layer FILE   also write the overlay pens (320x200 bytes, 255 = nothing drawn)\n"
                               "  --dump-flash FILE.ppm   write the last uniform orange frame (the end of a crash hold), if one was presented\n"
                               "  --dump-window FILE.ppm   also write the last frame at window resolution (scale x 320x200)\n", input_ctrl_names()); return 2; }
    }
#ifdef INTERCEPTOR_SYNTHETIC
    if (synthetic) { if (synthetic_loader(&loader, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); return 1; } }
    else
#endif
    if (assets_open(assets_dir, &loader, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); return 1; }
    g = game_create(&loader, err, sizeof err);
    if (!g) { fprintf(stderr, "%s\n", err); assets_close(&loader); return 1; }
    game_random_seed(g, seed >= 0 ? (uint32_t)seed : ((uint32_t)time(NULL) ^ (uint32_t)SDL_GetTicks() * 2654435761u) | 1u);
    /* The flight log (note 08 section 10, plan Task 8): a local file replaces the log disk. Loaded once here, saved when the core reports a save point (scene.log_dirty).
     * Nothing is fatal (Review Focus 3): a missing file is a new player, an unreadable or refused one starts a new log and is reported once, a failed save is reported
     * once and the game goes on with the log in memory. A counted run, a debug start and --no-log use no file. */
    if (log_arg) { snprintf(logpath, sizeof logpath, "%s", log_arg); use_log = 1; }
    else if (!no_log && frames < 0 && !flight && logfile_default_path(logpath, sizeof logpath) == 0) use_log = 1;
    if (use_log) {
        unsigned char lb[4096];
        const int n = logfile_load(logpath, lb, sizeof lb);
        if (n == -1) game_log_import(g, NULL, 0);   /* no file: the first tour of duty, the file is created at the first save */
        else if (n == -2) { fprintf(stderr, "flight log %s: cannot be read; starting a new log\n", logpath); game_log_import(g, NULL, 0); }
        else {
            const int rc = game_log_import(g, lb, (size_t)(n < 0 ? (int)sizeof lb : n));   /* n == -3: longer than the buffer, the first bytes are the container */
            if (rc != GAME_LOG_OK) fprintf(stderr, "flight log %s: not a flight log of this version (%d); starting a new log, the file is replaced at the first save\n", logpath, rc);
        }
    }
    if (scale < 1) scale = 1;
    if (!win_w) { win_w = 320 * scale; win_h = 200 * scale; }
    v = video_create(win_w, win_h, err, sizeof err);
    if (!v) { fprintf(stderr, "%s\n", err); game_destroy(g); assets_close(&loader); return 1; }
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) fprintf(stderr, "no game controller support (%s)\n", SDL_GetError());   /* not fatal: the keyboard plays */

    if (alog_path && !(alog = fopen(alog_path, "w"))) { fprintf(stderr, "could not open %s\n", alog_path); game_destroy(g); assets_close(&loader); return 1; }
    if (wav_path) {
        wav_au = audio_create(WAV_RATE, 2);
        if (wav_au) audio_set_clock(wav_au, AUDIO_PORT_CLOCK);   /* the exact step grid: the port's blank is 1/50 s (the machine's is 1/49.92: the sounds are 0.16 % faster in time and pitch than the original's, in step with the game) */
        if (wav_au && no_filter) audio_set_filter(wav_au, 0);
    }
    audio_start();
    if (dev_au && no_filter) audio_set_filter(dev_au, 0);
    samples_load();
    thr = pthr * 65535 / 100 - 32768;   /* the free camera's keyboard throttle steps start from --throttle (idle when not given) */
    game_set_logdisk_result(g, logdisk);   /* until Task 8 wires the log file the check is the write-enabled one: no prompt */
    if (freecam) game_set_debug_freecam(g, 1);
    if (runway) game_set_start(g, 1);
    video_set_world(v, game_world(g));
    if (frames >= 0) video_set_blanks_per_frame(v, 100 * 50 / FLIGHT_STEP_HZ);   /* a counted run steps the game once per frame: the fade sees 6.25 blanks of it */
    memset(&scene, 0, sizeof scene);
    htest = hud_test ? hud_test_create(&loader) : NULL;
    last_ms = SDL_GetTicks();
    sec_last = SDL_GetTicks();
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(web_frame, 0, 1);   /* one frame per animation frame; the loop never returns (the page owns the program) */
    return 0;
#else
    while (frame_once()) {}
    return finish();
#endif
}
