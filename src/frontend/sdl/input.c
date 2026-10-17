/* src/frontend/sdl/input.c - the game controller table (see input.h). ONE table: the controller's control -> the keyboard key whose core event it sends.
 * Nothing here is a game rule; the keys' meanings are main.c's and note 08 11. The manual's "Game controller" section prints this table. */
#include "input.h"
#include <string.h>

enum { BTN, AXIS };
typedef struct {
    const char *name;   /* the --ctrl name and the manual's */
    int kind;           /* BTN: sdl = SDL_GameControllerButton; AXIS: sdl = SDL_GameControllerAxis, sign = the direction that counts (triggers: +1) */
    int sdl, sign;
    SDL_Keycode key;
    int any_screen;
} entry_t;

static const entry_t TABLE[] = {
    /* the left stick: the digital stick (y-up is stick forward, as the keyboard's UP) */
    { "x-left",     AXIS, SDL_CONTROLLER_AXIS_LEFTX,  -1, SDLK_LEFT,  0 },
    { "x-right",    AXIS, SDL_CONTROLLER_AXIS_LEFTX,  +1, SDLK_RIGHT, 0 },
    { "y-up",       AXIS, SDL_CONTROLLER_AXIS_LEFTY,  -1, SDLK_UP,    0 },
    { "y-down",     AXIS, SDL_CONTROLLER_AXIS_LEFTY,  +1, SDLK_DOWN,  0 },
    /* the right stick: the rudder (left, right) and the throttle keys (up = more) */
    { "rx-left",    AXIS, SDL_CONTROLLER_AXIS_RIGHTX, -1, SDLK_COMMA,  0 },
    { "rx-right",   AXIS, SDL_CONTROLLER_AXIS_RIGHTX, +1, SDLK_PERIOD, 0 },
    { "ry-up",      AXIS, SDL_CONTROLLER_AXIS_RIGHTY, -1, SDLK_EQUALS, 0 },
    { "ry-down",    AXIS, SDL_CONTROLLER_AXIS_RIGHTY, +1, SDLK_MINUS,  0 },
    /* the triggers */
    { "rt",         AXIS, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, +1, SDLK_SPACE,     0 },   /* fire */
    { "lt",         AXIS, SDL_CONTROLLER_AXIS_TRIGGERLEFT,  +1, SDLK_BACKSLASH, 0 },   /* brake */
    /* the buttons */
    { "a",          BTN, SDL_CONTROLLER_BUTTON_A,             0, SDLK_RETURN,   1 },   /* select the weapon in flight; leaves the splash and the debrief */
    { "b",          BTN, SDL_CONTROLLER_BUTTON_B,             0, SDLK_g,        0 },   /* gear */
    { "x",          BTN, SDL_CONTROLLER_BUTTON_X,             0, SDLK_t,        0 },   /* next target */
    { "y",          BTN, SDL_CONTROLLER_BUTTON_Y,             0, SDLK_r,        0 },   /* radar range */
    { "lb",         BTN, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  0, SDLK_f,        0 },   /* flare */
    { "rb",         BTN, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0, SDLK_c,        0 },   /* chaff */
    { "ls",         BTN, SDL_CONTROLLER_BUTTON_LEFTSTICK,     0, SDLK_j,        0 },   /* ECM */
    { "rs",         BTN, SDL_CONTROLLER_BUTTON_RIGHTSTICK,    0, SDLK_KP_ENTER, 0 },   /* cockpit / outside view */
    { "start",      BTN, SDL_CONTROLLER_BUTTON_START,         0, SDLK_p,        0 },   /* pause */
    { "back",       BTN, SDL_CONTROLLER_BUTTON_BACK,          0, SDLK_m,        0 },   /* the map */
    { "dpad-up",    BTN, SDL_CONTROLLER_BUTTON_DPAD_UP,       0, SDLK_KP_8,     0 },   /* head / viewing direction */
    { "dpad-down",  BTN, SDL_CONTROLLER_BUTTON_DPAD_DOWN,     0, SDLK_KP_2,     0 },
    { "dpad-left",  BTN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,     0, SDLK_KP_4,     0 },
    { "dpad-right", BTN, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    0, SDLK_KP_6,     0 },
};
#define NENT ((int)(sizeof TABLE / sizeof TABLE[0]))

static unsigned char held[NENT];

int input_ctrl_translate(const SDL_Event *e, ctrl_key_t *out, int max) {
    int n = 0;
    for (int i = 0; i < NENT && n < max; i++) {
        const entry_t *t = &TABLE[i];
        int now;
        if (t->kind == BTN && (e->type == SDL_CONTROLLERBUTTONDOWN || e->type == SDL_CONTROLLERBUTTONUP) && e->cbutton.button == t->sdl)
            now = e->type == SDL_CONTROLLERBUTTONDOWN;
        else if (t->kind == AXIS && e->type == SDL_CONTROLLERAXISMOTION && e->caxis.axis == t->sdl)
            now = t->sign * (int)e->caxis.value > CTRL_DEAD_ZONE;
        else continue;
        if (now == held[i]) continue;
        held[i] = (unsigned char)now;
        out[n].key = t->key; out[n].down = now; out[n].any_screen = t->any_screen; n++;
    }
    return n;
}

int input_ctrl_inject(const char *name, int down, int mag, SDL_Event *e) {
    for (int i = 0; i < NENT; i++) {
        const entry_t *t = &TABLE[i];
        if (strcmp(t->name, name)) continue;
        memset(e, 0, sizeof *e);
        if (t->kind == BTN) {
            e->type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
            e->cbutton.which = -1;   /* a test event: no device */
            e->cbutton.button = (Uint8)t->sdl; e->cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
        } else {
            e->type = SDL_CONTROLLERAXISMOTION;
            e->caxis.which = -1;
            e->caxis.axis = (Uint8)t->sdl; e->caxis.value = (Sint16)(down ? t->sign * mag : 0);
        }
        return 1;
    }
    return 0;
}

const char *input_ctrl_names(void) {
    return "x-left x-right y-up y-down rx-left rx-right ry-up ry-down rt lt a b x y lb rb ls rs start back dpad-up dpad-down dpad-left dpad-right";
}
