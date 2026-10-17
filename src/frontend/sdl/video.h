/* src/frontend/sdl/video.h */
#ifndef INTERCEPTOR_SDL_VIDEO_H
#define INTERCEPTOR_SDL_VIDEO_H
#include <stddef.h>
#include "../../core/game.h"
typedef struct video video_t;
struct world;
/* Gives the renderer the core world (game_world(g)) for model polygons; NULL draws sky and ground only. */
void     video_set_world(video_t *v, const struct world *w);
/* The window is resizable; width x height is its initial size. The 320x200 frame is scaled by the largest integer factor that fits and
 * letterboxed. */
video_t *video_create(int width, int height, char *err, size_t errlen);
void     video_set_window_size(video_t *v, int w, int h);           /* resizes the window (tests use it to exercise the resize path) */
void     video_render(video_t *v, const game_scene_t *scene, const game_loader_t *loader);
int      video_dump_ppm(video_t *v, const char *path);          /* the 320x200 frame, overlay included */
int      video_dump_window_ppm(video_t *v, const char *path);   /* the window-resolution frame (world and overlay, letterboxed, the window's size) */
int      video_dump_flash_ppm(video_t *v, const char *path);   /* the last uniform orange frame of the crash hold's end (scene.flash_frames); -1 when none was presented */
int      video_dump_layer(video_t *v, const char *path);        /* 320x200 bytes: the overlay's pens, 255 where nothing is drawn */
/* Tests: a presented frame stands for x100/100 vertical blanks of the fade (625 = one game step of 8 Hz); 0 (the default) counts the wall clock at 50 Hz. */
void     video_set_blanks_per_frame(video_t *v, int x100);
void     video_destroy(video_t *v);
#endif
