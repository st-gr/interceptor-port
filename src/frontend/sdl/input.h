/* src/frontend/sdl/input.h - the game controller (SDL_GameController) as a second source of the keyboard's keys.
 *
 * The one table in input.c maps each button, trigger and stick direction to a key of the keyboard; the frontend feeds the key as an SDL key event, so the controller
 * produces exactly the core events the keyboard does (no rule of its own). The stick is the digital stick of note 05 17.4 (the key bits: a direction is held or not,
 * the ramp is the core's), so the analogue deflection has one number, the dead zone. PORT CHOICE C: the dead zone is not an original value (the original reads keys). */
#ifndef INPUT_H
#define INPUT_H

#include <SDL.h>

/* A stick or trigger counts as held in a direction when its deflection exceeds this (of 32,767: about a quarter of the travel), and as released at or below it. PORT CHOICE C. */
#define CTRL_DEAD_ZONE 8000

typedef struct {
    SDL_Keycode key;
    int down;         /* 1 a press, 0 a release */
    int any_screen;   /* 1: also taken outside the flight (the table's flag); the other entries are flight-only, as the mouse and joystick are not menu input (note 08 5.9) */
} ctrl_key_t;

/* Translates one SDL controller event (button down/up, axis motion) into key presses and releases of the table; returns how many were written to out (at most max; an axis
 * that crosses from one direction to the other gives a release and a press). Other events give 0. The held state of every entry is kept here. */
int input_ctrl_translate(const SDL_Event *e, ctrl_key_t *out, int max);

/* Builds the controller event that --ctrl NAME@N[:MAG] / --ctrlup NAME@N push (NAME as in the table: "a", "rt", "x-left"); an axis name is pushed at deflection MAG (0..32767, 32767 when not given) in its
 * direction; 0 when NAME is not in the table. */
int input_ctrl_inject(const char *name, int down, int mag, SDL_Event *e);

/* The names of the table, space separated (the usage text). */
const char *input_ctrl_names(void);

#endif
