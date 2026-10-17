/* mover.h - the front end's camera mover (note 07 6.2, note 08 6.4, 12.14 N5 / C13): the stage machine V:4A86E that flies the approach camera from the selection camera to the start
 * position. One call per pass of the approach state. The thresholds, the shifts, the blend target and the offset targets are the 68000 routine's (camera_check, 0 differences); the
 * damped step uses the normaliser of note 04a P4 (ai_normalise). Stages 0 to 6 are modelled (the free-flight and training approaches, camera mode 6); the mission briefing's stages 7 and 8
 * (camera modes 2 and 3) belong to the briefing and are not run by this module. */
#ifndef MOVER_H
#define MOVER_H

#include <stdint.h>

#define MOVER_SELECT_X 0x10800000   /* the selection camera: where the location and aircraft screens stand (note 04 "The start marks of the overview map") */
#define MOVER_SELECT_Y 0x03000000
#define MOVER_SELECT_Z 0x10C00000
#define MOVER_ARRIVE_D 0x240        /* the arrival distance */
#define MOVER_MAP_X 0x10C00000      /* the briefing camera's overview position (note 08 13.4 C1: the camera never leaves it in a mission briefing) */
#define MOVER_MAP_Y 0x05000000
#define MOVER_MAP_Z 0x11400000
#define MOVER_MISSION_WAIT 4        /* 08 12.14 C14 [V: sound7-approach]: the mover reads stage 8 in the approach's passes 0 to 2 and stage 0 from pass 3 */
#define MOVER_TRAIN2_X 0x0F248000   /* the camera of the training part 2's dispatch (a fixed camera point, note 04 "fixed camera points"); the approach starts in stage 3 */
#define MOVER_TRAIN2_Y 0x00005000
#define MOVER_TRAIN2_Z 0x0FCCF000

typedef struct mover {
    int32_t cam[3];     /* V:4ACF6, V:4ACFA, V:4ACFE */
    int32_t tgt[3];     /* V:4AD0E: the target */
    int32_t trail[3];   /* V:4AD02: the previous damped step */
    int stage;          /* V:4A86E */
    int count;          /* V:4A95D: the passes left after the arrival */
    int wait;           /* the passes left in the briefing's stage 8 before the approach's stage 0 (mission approaches, MOVER_MISSION_WAIT) */
} mover_t;

/* What the viewed record (the player) gives the mover: its position (+0x14), its attitude matrix (+0x92, Q14) and V:4A900. */
typedef struct {
    const int32_t *pos;
    const int16_t *mat;
    int location;
} mover_ctx_t;

/* The result of one pass. */
typedef struct {
    int look_on;        /* V:4A86C was set to 1 (stage 0 at 0x1C00000), V:4A865 cleared */
    int look_off;       /* V:4A86C was cleared (the arrival) */
    int arrived;        /* the arrival pass: stage 6 entered */
    int cockpit;        /* the view toggle request (V:4A97E bit 1) and the camera mode 0: the cockpit follows */
} mover_out_t;

/* The target of the start: the viewed record's position plus the offset (0, 0, -5760) ring units in its frame (0x90000 x 65,536 / 1024: 0x5A0000 position units behind it, 12.14 C13). */
void mover_start_target(const mover_ctx_t *c, int32_t tgt[3]);
/* The world point of an offset (x, y, z in ring units) in the viewed record's frame (0x1D466): the matrix rows times the words, shifted right by 4, added to the position. */
void mover_offset_point(const mover_ctx_t *c, int x, int y, int z, int32_t out[3]);
/* Resets the mover to the free-flight approach: stage 0, the camera at the selection camera, the trail empty, the target of the start. */
void mover_begin(mover_t *m, const mover_ctx_t *c);
/* The mission approach (modes 3 to 8): the camera at the briefing's overview position, stage 8 for MOVER_MISSION_WAIT passes (no step: the stage 8 target is the camera), then stage 0 toward the start. */
void mover_begin_mission(mover_t *m, const mover_ctx_t *c);
/* The training part 2 approach: the camera at its fixed point, stage 3, the target the player's position. */
void mover_begin_stage3(mover_t *m, const mover_ctx_t *c, const int32_t cam[3]);
/* One pass of the mover in camera mode 6 (0x2D8E8). */
mover_out_t mover_step(mover_t *m, const mover_ctx_t *c);

#endif
