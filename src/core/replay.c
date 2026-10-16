/* src/core/replay.c - the demo replay: see replay.h. Every number is a section of docs/re/08-game-flow.md. No stream bytes live here: the stream comes from the assets. */
#include <stdlib.h>
#include <string.h>
#include "replay.h"

/* The event bits of 7.3, in the executor's order: the first set bit runs first; the order is word 1 bits 8 to 15, word 1 bits 0 to 3, word 2 bits 8 to 15, word 2 bits 0 to 7.
 * `kind`: the key event the body stands for (the same code path as the key: flight_event), EV_NOP when the body does nothing in the demo, or EV_NONE for a bit with no event (none in the table: all bits are mapped; the gap words stay 0). */
enum { EV_NONE = 0, EV_NOP, EV_BUTTON, EV_KEYPAD, EV_KEYPAD_UP, EV_VIEW_DEPENDENT };

typedef struct { int word; unsigned bit; int kind; int code; int value; } bit_t;

static const bit_t BITS[REPLAY_MAX_EVENTS] = {
    /* word 1, high byte */
    { 1, 8,  EV_BUTTON, GAME_BTN_GEAR, 1 },            /* G: gear toggle */
    { 1, 9,  EV_BUTTON, GAME_BTN_HOOK, 1 },            /* A: hook toggle */
    { 1, 10, EV_BUTTON, GAME_BTN_FIRE, 1 },            /* SPACE press: fire (milestone 5 acts on it) */
    { 1, 11, EV_BUTTON, GAME_BTN_FIRE, 0 },            /* SPACE release */
    { 1, 12, EV_BUTTON, GAME_BTN_SELECT_WEAPON, 1 },   /* RETURN: the weapon-select cycle in the modes of the demo (milestone 5) */
    { 1, 13, EV_NOP, 0, 0 },                           /* E with the shift flag: eject; the shift flag is clear in the demo: the body does nothing */
    { 1, 14, EV_BUTTON, GAME_BTN_RADAR_RANGE, 1 },     /* R: radar range step */
    { 1, 15, EV_BUTTON, GAME_BTN_TARGET, 1 },          /* T: target request */
    /* word 1, low byte */
    { 1, 0,  EV_BUTTON, GAME_BTN_BRAKE, 1 },           /* backslash or BACKSPACE: brake toggle */
    { 1, 1,  EV_BUTTON, GAME_BTN_FLARE, 1 },           /* F: flare (milestone 5 acts on it) */
    { 1, 2,  EV_BUTTON, GAME_BTN_CHAFF, 1 },           /* C: chaff (milestone 5) */
    { 1, 3,  EV_BUTTON, GAME_BTN_ECM, 1 },             /* J: ECM toggle */
    /* word 2, high byte */
    { 2, 8,  EV_BUTTON, GAME_BTN_TOWER, 1 },           /* M: the map view toggle */
    { 2, 9,  EV_KEYPAD, GAME_BTN_KEYPAD, GAME_KP_ENTER },   /* KP-ENTER: the inside/outside toggle */
    { 2, 10, EV_BUTTON, GAME_BTN_FIXED_CAMERA, 1 },    /* Shift and KP-ENTER: the fixed-camera view */
    { 2, 11, EV_VIEW_DEPENDENT, 0, 0 },                /* KP5 in the cockpit, "]" in an outside view */
    { 2, 12, EV_KEYPAD, GAME_BTN_KEYPAD, 8 },          /* KP8 */
    { 2, 13, EV_KEYPAD, GAME_BTN_KEYPAD, 9 },          /* KP9 */
    { 2, 14, EV_KEYPAD, GAME_BTN_KEYPAD, 6 },          /* KP6 */
    { 2, 15, EV_KEYPAD, GAME_BTN_KEYPAD, 3 },          /* KP3 */
    /* word 2, low byte */
    { 2, 0,  EV_KEYPAD, GAME_BTN_KEYPAD, 2 },          /* KP2 */
    { 2, 1,  EV_KEYPAD, GAME_BTN_KEYPAD, 1 },          /* KP1 */
    { 2, 2,  EV_KEYPAD, GAME_BTN_KEYPAD, 4 },          /* KP4 */
    { 2, 3,  EV_KEYPAD, GAME_BTN_KEYPAD, 7 },          /* KP7 */
    { 2, 4,  EV_KEYPAD, GAME_BTN_KEYPAD, 5 },          /* KP5 in an outside view: direction 8 */
    { 2, 5,  EV_KEYPAD, GAME_BTN_KEYPAD, GAME_KP_DOT },/* KP-dot: direction 9 */
    { 2, 6,  EV_KEYPAD_UP, GAME_BTN_KEYPAD_UP, 2 },    /* the release of a head or view key: the head returns (V:4A949 = 5): any of KP2, 4, 6, 7, 1 starts the hold countdown */
    { 2, 7,  EV_BUTTON, GAME_BTN_LBRACKET, 1 },        /* "[" in an outside view: the camera one level further */
};

int replay_played_frames(const uint8_t *keys, int n) {
    for (int i = 0; i + 2 < n; i++)
        if (keys[i] == 0xFF && keys[i + 1] == 0xFF && keys[i + 2] == 0xFF) return i;   /* 7.4 rule 3: the three bytes at the key pointer */
    return n;
}

static void install(replay_t *r, screen_t *scr) {
    r->scr = scr;
    r->prev = scr->mode_handler; r->prev_user = scr->mode_user;
    screen_set_mode_handler(scr, replay_mode_handler, r);
    scr->demo_available = (uint8_t)(r->played > 0);   /* menu key 1 needs a stream (7.1: the key is ignored when the buffer pointer is zero) */
}

int replay_set_stream(replay_t *r, screen_t *scr, const uint8_t *keys, const demo_events_t *ev, int n) {
    free(r->demo);   /* a stream set before is replaced (r is zeroed or was set up by replay_init / replay_set_stream) */
    memset(r, 0, sizeof *r);
    if (n < 0 || n > DEMO_MAX_FRAMES) return -1;
    r->demo = (demo_t *)calloc(1, sizeof *r->demo);
    if (!r->demo) return -1;
    r->demo->frame_count = n;
    if (n) memcpy(r->demo->key, keys, (size_t)n);
    if (n && ev) memcpy(r->demo->ev, ev, (size_t)n * sizeof *ev);
    r->played = replay_played_frames(r->demo->key, n);
    install(r, scr);
    return 0;
}

int replay_init(replay_t *r, screen_t *scr, const game_loader_t *loader, char *err, size_t errlen) {
    memset(r, 0, sizeof *r);
    uint8_t *probe = NULL;
    size_t plen = 0;
    if (loader->read(loader->user, "tables/demo.bin", &probe, &plen) != 0) return 0;   /* a synthetic asset set: no demo, key 1 is ignored */
    loader->release(loader->user, probe);
    r->demo = (demo_t *)calloc(1, sizeof *r->demo);
    if (!r->demo) { core_assets_append(err, errlen, "replay_init: out of memory"); return -1; }
    if (assets_load_demo(loader, r->demo, err, errlen) != 0) { free(r->demo); r->demo = NULL; return -1; }
    r->played = replay_played_frames(r->demo->key, r->demo->frame_count);
    install(r, scr);
    return 0;
}

void replay_destroy(replay_t *r) {
    if (!r) return;
    free(r->demo);
    r->demo = NULL; r->played = 0; r->active = 0;
}

int replay_mode_handler(void *user, uint8_t mode_byte) {
    replay_t *r = (replay_t *)user;
    if (mode_byte == 0x7F && r->demo && r->played > 0) {
        screen_demo_begin(r->scr, 6);   /* 7.5: STEPS_DEMO_WAIT */
        return 1;
    }
    return r->prev ? r->prev(r->prev_user, mode_byte) : 0;
}

void replay_start(replay_t *r) { if (r && r->demo) { r->step = 0; r->active = 1; } }
void replay_stop(replay_t *r) { if (r) r->active = 0; }

static void add(replay_frame_t *o, int code, int value) {
    game_event_t *e = &o->ev[o->n_events++];
    e->kind = GAME_EV_BUTTON; e->code = code; e->value = value;
}

game_event_t replay_event_in_view(const replay_frame_t *f, int i, int ext_view) {
    game_event_t e = f->ev[i];
    if (f->view_dep[i] && ext_view) { e.code = GAME_BTN_RBRACKET; e.value = 1; }   /* 0x1E51C: in an outside view the camera one level closer ("]") */
    return e;
}

int replay_step(replay_t *r, replay_frame_t *out) {
    memset(out, 0, sizeof *out);
    if (!r || !r->active || !r->demo || r->step >= r->played) return 0;
    const int s = r->step++;
    out->valid = 1;
    out->index = s;
    out->key = r->demo->key[s];
    if (s > 0) {   /* 7.4 rule 1: the words of index 0 are never executed; the events of index s run before step s */
        out->word1 = r->demo->ev[s].word1; out->word2 = r->demo->ev[s].word2;
        for (int i = 0; i < REPLAY_MAX_EVENTS; i++) {
            const bit_t *b = &BITS[i];
            const unsigned w = b->word == 1 ? out->word1 : out->word2;
            if (!(w & (1u << b->bit))) continue;
            switch (b->kind) {
            case EV_BUTTON: case EV_KEYPAD: case EV_KEYPAD_UP: add(out, b->code, b->value); break;
            case EV_VIEW_DEPENDENT:   /* 0x1E51C: the body reads the live view when it runs, after the bodies of the earlier bits of the frame: the event is the cockpit's head-forward request (KP5) and the caller resolves it at its turn (replay_event_in_view) */
                out->view_dep[out->n_events] = 1;
                add(out, GAME_BTN_KEYPAD, 5);
                break;
            case EV_NOP: break;
            default: if (b->word == 1) out->gap1 |= (uint16_t)(1u << b->bit); else out->gap2 |= (uint16_t)(1u << b->bit); break;
            }
        }
        /* 7.3: a bit of the mask 0x00F0 of word 1 is not an event (the original posts a notice and loops; no frame of the shipped stream has one): ignored */
    }
    return r->step < r->played;
}
