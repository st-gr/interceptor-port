/* src/frontend/sdl/audio.h - the Paula-like mixer (docs/re/10-sound.md sections 1.2-1.5, 4.2, 9.2): four voices of signed 8-bit samples at 3,546,895 / period samples a second, volume 0..64,
 * voices 0 and 3 left and 1 and 2 right, the A500 filter chain. The mixer implements the hardware only: the core (src/core/sound.c) runs the sound library and hands it the register writes
 * of every blank (scene.h, game_audio_cmd_t); the master volume is inside the library (it caps the volumes written), so the mixer has no master input.
 * The mixer takes no SDL: the device (part B) calls audio_render from its callback (under SDL_LockAudioDevice, audio_apply being called from the game thread). */
#ifndef INTERCEPTOR_SDL_AUDIO_H
#define INTERCEPTOR_SDL_AUDIO_H
#include <stdint.h>
#include "../../core/scene.h"

#define AUDIO_OBJECTS 0x25          /* objects 0..0x24 (note 10 section 2.2) */
#define AUDIO_PAL_CLOCK 3546895u    /* colour clocks a second; a sample lasts `period` of them (note 10 section 1.4) */
#define AUDIO_PORT_CLOCK 3552550u   /* the port's colour clocks a second: 50 blanks of 71,051, the clock its 8 steps a second keep (the machine makes 49.92 blanks a second) */
#define AUDIO_BLANK_CLOCKS 71051u   /* colour clocks in a blank of the game's display (313 lines, note 10 4.2) */

typedef struct audio audio_t;
/* what the live clock did (tests, diagnostics): the writes waiting, the underruns (the output passed the step before it came; the clock was anchored again), the blanks of output skipped (the game got more than 5 blanks ahead: whole blanks, never merged, note 10 9.2 (d); the field keeps its name merged_blanks),
 * the trim of the mixer's clock in 1/65536 and the lead of the last step in colour clocks */
typedef struct { uint32_t pending, underruns, merged_blanks; int32_t trim; int64_t lead_clocks; } audio_stats_t;

/* rate_hz: the device's obtained rate; channels 1 (mono: the average of left and right) or 2. Returns NULL when out of memory or the arguments are out of range.
 * The A500 filter chain is ON (the reference machine has the power LED on, note 10 section 1.5). */
audio_t *audio_create(int rate_hz, int channels);
void     audio_destroy(audio_t *);
void     audio_set_filter(audio_t *, int on);
/* The sample of an object (file-backed or the core's synthesised buffer sound_buffer). The buffer is not copied and must outlive its use; a voice that plays it must be
 * stopped before it is replaced. data NULL or len 0 marks the object missing. */
void     audio_set_sample(audio_t *, int object, const int8_t *data, uint32_t len);
/* The colour clocks of a second of output time. The default is the machine's, 3,546,895 (the pitches of the original). The port makes 50 blanks of 71,051 clocks a second, 3,552,550: with that clock the
 * output time is the game's time (a step is 125 ms of output, 0.16 % shorter than the machine's 125.19), the frontend sets it for the device and for --wav. */
void     audio_set_clock(audio_t *, uint32_t clocks_per_second);
/* Live mode: the device runs by itself and the game thread applies the steps as they come. The first step is placed three blanks (60 ms) ahead of the output, the later steps keep that mapping
 * (each at its own step boundary); a step that comes after the output passed it re-anchors the clock (an underrun, counted and reported once); a step that begins more than five blanks ahead makes the mixer
 * skip whole blanks of output down to the lead (every write and block end applied in order, nothing merged), and the small drift of the two clocks is a trim of at most 0.5 % on the mixer's clock. Off (the default): the grid mode, in
 * which the mixer's time is the game's (blank n at n x 71,051 colour clocks): --wav and the tests. */
void     audio_set_live(audio_t *, int on);
audio_stats_t audio_stats(const audio_t *);
/* Queues the register writes of one simulation step (also with count 0: every step must be applied, in order). The step's first blank is floor(25 k / 4) for the k-th call (0-based, note 10
 * 9.3). Each write is applied at its own colour clock: the blank of the write times 71,051 plus its clock (game_audio_cmd_t.clock), in time order (the writes of one voice in the order given). */
void     audio_apply(audio_t *, const game_audio_cmd_t *cmds, int count);
/* Renders `frames` output frames (interleaved when channels is 2). */
void     audio_render(audio_t *, int16_t *out, int frames);
#endif
