/* src/frontend/sdl/panel.h - the instrument panel, cowl, radar scope, warning line, external text strip
 * and the map overlay of the tower view (docs/re/06-cockpit-hud.md, "Instrument panel", "Message line",
 * "External view text strip", "Map renderer"). Drawn into the hud.h layer. */
#ifndef INTERCEPTOR_SDL_PANEL_H
#define INTERCEPTOR_SDL_PANEL_H
#include "hud.h"

/* The clean panel picture at (0,145) and the cowl cookie-cut at (16,133), both shifted by the view
 * offset, then the radar scope (0x35CF2). Called before the HUD so the HUD's black sides land on the cowl. */
void panel_draw_cockpit(hud_t *h);
/* Everything the panel's other routines draw over the picture: threat lamps, compass, digits, fuel bar,
 * attitude ball, left display, lat/long, gear, message line, afterburner line and lamps. */
void panel_draw_instruments(hud_t *h, const game_scene_t *scene);
/* The three text readouts of the external and tower views, rows 184 to 188. */
void panel_draw_strip(hud_t *h, const game_scene_t *scene);
/* Grid and labels of the tower (map) view. */
void panel_draw_map(hud_t *h, const game_scene_t *scene);

#endif
