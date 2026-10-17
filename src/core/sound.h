/* src/core/sound.h - the core's sound state: the original's sound library as an integer model (docs/re/10-sound.md), emitting Paula voice commands into the scene.
 *
 * What is modelled (note 10): the 37 sound objects (2.1, 2.2), the four voice slots, the per-blank server with the script interpreter (3.1, 4.3), the audio interrupt
 * handler with the block-count rule (4.2: an object with count c followed by another plays c + 1 blocks, the last object of a voice plays c; counts and periods persist
 * across starts), the scripts of the tone, noise, gun, gear and launch objects (3.2 to 3.5), the player and the stops (4.1), the library's own random register (3.3), the
 * wrappers of the event table (section 8), the per-pass routine 0x14AA0 (section 5: sound_pass) and the master-volume inputs (section 6). Paula playback itself (the zero-order
 * hold at 3,546,895 / period, the volume, the stereo pairing, the filter) is the frontend's mixer; the core emits what the hardware would be doing at every blank.
 *
 * Time. The server runs once per vertical blank. The game steps at FLIGHT_STEP_HZ = 8, 6.25 blanks a step (note 08 3.8, note 10 9.3): step k covers the blanks floor(25 k / 4) to
 * floor(25 (k + 1) / 4) - 1, a pattern of 6, 6, 6, 7 blanks. sound_step runs the blanks of one step; the wrappers called by the pass (sound_pass, sound_event) act before the first
 * blank of the step (the original's pass is mid-blank: a start's registers are written at once, its script at the next blank, 9.1), except the typed text's clicks, which call sound_run_to first so
 * that each acts at the blank of its pass (5.7 item 13). Every command carries the blank (0..6) within its step at which it takes effect and the colour clock within that blank (0 for the server and the wrappers, the clock of the block end for the interrupt handler). A blank is the original's order (9.2): the master ramp, the
 * server of each voice, then the hardware: the block clock (4.2) counts colour clocks, 71,051 a blank (313 lines), sample by sample at the period register's value when each sample starts, and a block
 * that ends in the blank runs the audio interrupt after the blank's server.
 *
 * Commands (scene.h, note 10 9.2): the register writes the library made, in order, each stamped with its blank: LATCH (the block the voice plays next), DMA on and off, PERIOD, VOLUME (already
 * capped by the master level, which is entirely inside this module). A write that would not change its register is omitted. The mixer is Paula only.
 * The random draws (the noise pitch 11 bits, the wind 6 + 6, the launch 3) come from the library's own register (seed 0x47091247, 3.3), so the command log is deterministic. */
#ifndef INTERCEPTOR_SOUND_H
#define INTERCEPTOR_SOUND_H
#include <stdint.h>
#include "scene.h"
#include "assets.h"

#define SND_OBJECTS SOUND_OBJECTS          /* 37 (note 10 2.2) */
#define SND_VOICES 4
#define SND_NONE 0xFF
#define SND_CLOCKS_PER_BLANK 71051L        /* a 313-line PAL frame in colour clocks (4.2) */
#define SND_NOISE_BYTES 0x800              /* the noise buffer (3.3) */
#define SND_TONE_BYTES 32                  /* the square wave (3.2) */
#define SND_SCRIPT_GEAR 5                  /* the internal script id of the gear sound (SOUND_SCRIPT_* of assets.h are 1 to 4) */
#define SND_SCRIPT_CMDS 12

/* a sound object's run-time record (2.1); period and volume are 16.16 */
typedef struct {
    uint8_t exists, script, next;
    uint32_t length;                       /* bytes */
    int32_t period, volume, count, reload, pstep, vstep;
    int32_t lc[2], wait, pos, ptimer, vtimer;
} sound_obj_t;

/* one voice: the slot (the object the library will load next) and the hardware state (DMA, the block under way, the block latched, the period and volume registers) */
typedef struct {
    uint8_t slot, dma, cur, pend;
    int32_t left, cnt;                     /* the samples left in the block under way and the colour clocks left of the sample in progress (the period counter, 4.2) */
    uint16_t per;                          /* AUDxPER (floor 0x7C once written) */
    uint8_t vol;                           /* AUDxVOL as the library writes it: the volume field's hi word masked with 0x3F and capped by the master level */
} sound_voice_t;

typedef struct {
    sound_table_t tab;
    sound_obj_t o[SND_OBJECTS];
    sound_voice_t v[SND_VOICES];
    int32_t script[6][SND_SCRIPT_CMDS][2]; /* [id][command] = {field, value}; the patched copies of the five scripts (3.1 to 3.5) */
    uint32_t rnd;                          /* the library's random register (3.3) */
    int8_t noise[SND_NOISE_BYTES];
    int8_t tone[SND_TONE_BYTES];
    uint8_t en12, en13;                    /* the loader's enable bits V:4AC12, V:4AC13 (2.3), derived from the objects that exist */
    /* the master volume (section 6): level V:54FBE and target V:4A720 in 16.16, the flag V:4A88F (0 no ramp, 1 ramp, 2 start-up) */
    int32_t level, target;
    uint8_t flag;
    uint8_t master_set;                    /* 0xFF none, else the level (volume units) set at once during this step */
    /* the bytes the per-pass routine owns (5.1) */
    uint8_t gear_cd;                       /* V:4A93D */
    uint8_t cooldown;                      /* V:4A942 (signed byte; the cue player and the key click test it) */
    uint8_t bang_cd, bang_amp, bang_dur;   /* V:4A870, V:4A922, V:4A921 */
    /* the commands of the step */
    game_audio_cmd_t cmd[GAME_MAX_AUDIO_CMDS];   /* the step's register writes */
    int ncmd, dropped;
    uint8_t blank;                         /* the blank of the step being run (0 for the pass) */
    uint32_t clk;                          /* the colour clock within that blank at which the writes being made take effect: 0, or the clock of the block end whose interrupt is running (hw_advance) */
    uint8_t phase;                         /* 0..3: which step of the 6, 6, 6, 7 pattern is next */
} sound_t;

/* the inputs of the per-pass routine (5): the viewed record and the game's bytes. notice, hold and cues point at V:4A968, V:4A84F and V:4AC0C, which the routine reads and writes. */
typedef struct {
    uint8_t v4a966, v4a865, v4a866, v4a84d;   /* the gates (5.1 item 1): the flow state, the map view, the pause, the game gate (0 closes it) */
    uint8_t view;                             /* V:4A83D: 0 cockpit, else an external view (255 the default, 1 to 4 the levels) */
    uint8_t v4a86d;                           /* the flight value */
    int8_t ext_dir;                           /* V:4A96A */
    int16_t cam_dist;                         /* V:4ABFA */
    int8_t strain;                            /* V:4A941 */
    uint8_t pass_n;                           /* V:4A879: the pass counter */
    uint8_t camera;                           /* the stepped record is the camera's (V:4AA6E = V:4A996): the gear and ground wrappers need it */
    uint16_t w2;                              /* the viewed record's +2 (bit 3 afterburner, bit 7 wheels down) */
    int8_t throttle;                          /* +0x2B */
    int16_t speed, pitch_rate, roll_rate;     /* +0x6E, +0x56 (E), +0x5A (Q) */
    uint8_t cls;                              /* +0x62 */
    uint8_t eject_nibble;                     /* +0x7C low nibble: the ejection state (the cue player discards every cue but 0x40 while it is non-zero) */
    uint8_t *notice, *hold;
    uint32_t *cues;
} sound_in_t;

typedef enum {
    SND_NOISE = 1,        /* 0x1A0DC(a amplitude, b duration): returns 1 when the noise sound does not exist (the caller posts the notice 0xFA and V:4A84F = 2, 3.4) */
    SND_GUN,              /* 0x1A2F0(a pulses, b volume): needs the gun object; the caller falls back to SND_NOISE(0x1C, 0x30) when the enable bit V:4AC13 bit 5 is clear */
    SND_GROUND,           /* 0x1A18C(a volume); b = the stepped record is the camera's */
    SND_GEAR,             /* 0x1A1E6(a volume); b = camera */
    SND_GEAR_STOP,        /* 0x1A24C */
    SND_LAUNCH,           /* 0x1A258(a volume) */
    SND_PHRASE,           /* a = 0: 0x19CE6(b) the start-up phrase (the music), a = 1: 0x19D7A(b) the mission tone pair */
    SND_CLICK,            /* 0x37C2E(a volume): gate V:4A942 */
    SND_BEEP_SWITCH,      /* 0x37C4A: a = the view byte V:4A83D */
    SND_BEEP_N,           /* 0x37C32(a = n): b = the view byte */
    SND_BEEP_44,          /* 0x37C44 */
    SND_WIND,             /* 0x19F9A(a volume) */
    SND_ENGINE_COCKPIT,   /* 0x19DB2(a pitch, b volume) */
    SND_ENGINE_EXT,       /* 0x19E46(a pitch, b volume) */
    SND_STOP_ALL,         /* 0x115F6 */
    SND_STOP_VOICE        /* 0x19C58(a voice) */
} sound_event_kind_t;

void sound_init(sound_t *s, const sound_table_t *tab);
/* The loaders of the original (2.3, 2.5) leave an object they could not build out of the library, and the enable bits clear with it, so the wrappers take their fallbacks. The frontend knows which sample
 * files it could read; present[o] is 0 for an object whose file is missing or too short. Called before the first pass: the library starts again (the state is as sound_init left it, the random register
 * included) with those objects absent. */
void sound_objects_available(sound_t *s, const uint8_t present[SND_OBJECTS]);

/* the wrappers of note 10 section 8 */
int  sound_event(sound_t *s, sound_event_kind_t kind, int a, int b);
void sound_tone(sound_t *s, const int p[9]);                   /* 0x1A042: the nine parameters of 3.2 */
void sound_glide(sound_t *s, int pitch, int volume, int ticks);/* 0x19EFA */
void sound_stop_all(sound_t *s);
void sound_stop_voice(sound_t *s, int voice);
void sound_start(sound_t *s, int object, int voice, int volume);   /* the player 0x19C7C */

/* the per-pass routine 0x14AA0 (section 5) and the clock tick (once per change of the system clock's seconds: the gear countdown and the cue cool-down) */
void sound_pass(sound_t *s, const sound_in_t *in);
void sound_second(sound_t *s);
int  sound_gear_key(sound_t *s);                               /* the gear key's store V:4A93D = 0x83 when the gear sound exists (returns 1); else 0: the key clicks instead */
void sound_bang_request(sound_t *s, int amplitude, int duration, int countdown);   /* V:4A922, V:4A921, V:4A870 */

/* the master volume: target in volume units 0..63 (-1 unchanged), set = the level in volume units to take at once (-1 none), flag V:4A88F (-1 unchanged) */
void sound_master(sound_t *s, int target, int set, int flag);

/* the library's server and interrupt handler for one blank (hardware, game's ramp, per-blank server), and the blanks of one step (6, 6, 6, 7) */
void sound_blank(sound_t *s);
int  sound_step_blanks(const sound_t *s);   /* the blanks of the step being run: 6, 6, 6 or 7 */
void sound_run_to(sound_t *s, int blank);   /* run the blanks of the step before `blank`, so that wrappers called now are stamped with it */
void sound_step(sound_t *s);

/* the commands accumulated over the step and the master inputs; clears them */
void sound_drain(sound_t *s, game_audio_cmd_t *out, int max, int *count, uint8_t *master_target, uint8_t *master_set, uint8_t *master_ramp);

/* the synthesised buffers: the tone (object 4), the noise (objects 6 and 0xA). File-backed objects return 0 and the frontend reads the sample. */
int  sound_buffer(const sound_t *s, int object, const int8_t **data, uint32_t *len);

uint32_t sound_random(sound_t *s, int bits);                   /* the library's random draw 0x55C32 */
int  sound_flag(const sound_t *s);                             /* V:4A88F */
int  sound_level(const sound_t *s);                            /* V:54FBE hi word */
int  sound_slot(const sound_t *s, int voice);                  /* the object a voice's slot holds, SND_NONE if empty */

#endif
