/* src/core/aircraft.h - the player aircraft state, shared by the flight model (Task 5), the
 * cockpit and the world (which turns it into a camera, world_camera_build in world.h).
 * Plain data, fixed-width types. Sources: docs/re/04-scene-composition.md ("note 04") for the
 * entity fields, docs/re/03-3d-pipeline.md ("note 03") for the angle unit.
 *
 * Units.
 *   pos      position units (note 02 section 10): x and z horizontal, y altitude; 29 bits, the world
 *            wraps at 2^29 (WORLD_POS_MASK in world.h).
 *   pitch, roll, yaw   28800 units per circle (GAME_ANGLE_CIRCLE, note 03; 80 per degree). yaw is
 *            0..28799 and the heading is (-sin yaw, cos yaw) on (x, z) (world +x is the camera's left; yaw 90 degrees faces -x, note 03); pitch is signed, nose up
 *            positive; roll is signed. Set by the flight module from the note's words (note 05
 *            "Angles": pitch word positive = nose down, roll word positive = left bank): pitch =
 *            -(signed pitch word), yaw = heading word, roll = signed roll word, which are the roles
 *            world_object_matrix gives them.
 *   speed    the indicated speed word W in twelfths of a knot (note 05, "Speed unit": knots = |W| / 12).
 *   throttle the throttle byte T, -32..120 (note 05).
 *   gear, hook   gear: 1 when the gear is fully down (retract progress 0); hook: 1 when down.
 *   on_ground, on_carrier   1 when the wheels are down / when a deck bit of B4 is set.
 *   ring_class   (superseded: world_camera_build reads the class byte f.cls) which camera ring table the external views use: 0 default, 1 = class 0x11,
 *            2 = 0x14, 3 = 0x30 (note 04: the ring is chosen by the class byte of the aircraft's
 *            model record, entity offset +0x62). The flight module sets it from the player's class
 *            byte (0x11 F/A-18 -> 1, 0x10 F-16 -> 0, note 05 section 13). */
#ifndef INTERCEPTOR_AIRCRAFT_H
#define INTERCEPTOR_AIRCRAFT_H
#include <stdint.h>

/* The flight model's state (docs/re/05-flight-model.md): the words of the player record
 * (0x4B23C) and the variables outside it that the step reads or writes, with the original's units,
 * widths and sign conventions. The names follow the note ("W0", "B4", "S", "W", ...); the offset
 * of a record word is in the comment (+0xNN), the address of a variable is V:xxxxx. The flight
 * module (flight.h) owns all of it; the aircraft_t fields above it are derived from it after
 * every step. Angles are the note's: pitch positive = nose DOWN, heading clockwise, roll positive
 * = left bank (right wing up). */
typedef struct {
    uint16_t w0, w2;                    /* +0x00 flags, +0x02 flags */
    uint8_t  b4, state;                 /* +0x04 deck/pad/water bits, +0x05 state byte */
    int16_t  cell_x, cell_z;            /* +0x06, +0x08 */
    uint8_t  cell_a, cell_b;            /* +0x0A, +0x0B */
    int16_t  fine_x, fine_z;            /* +0x0C, +0x0E */
    int32_t  alt8;                      /* +0x10 y >> 8 */
    uint8_t  b20, b21;                  /* +0x20, +0x21 */
    int16_t  roll_sin, roll_cos;        /* +0x22, +0x24 */
    int16_t  ground;                    /* +0x26 */
    int8_t   cmd_pitch, cmd_rudder, cmd_roll, throttle;   /* +0x28 +0x29 +0x2A +0x2B (T) */
    uint8_t  repeat;                    /* +0x39 */
    int32_t  vel[3];                    /* +0x3E +0x42 +0x46 displacement added per step */
    int16_t  counter, deck;             /* +0x4C, +0x4E */
    int16_t  turn50, turn52, turn54;    /* +0x50 +0x52 +0x54 */
    int16_t  rate_p, rate_h, rate_r;    /* +0x56 +0x58 +0x5A (E, Y, Q) */
    uint8_t  group, cls, id, keys;      /* +0x5E +0x62 +0x63 +0x65 */
    uint16_t pitch, heading, roll;      /* +0x66 +0x68 +0x6A */
    int16_t  speed_target, speed;       /* +0x6C (S), +0x6E (W) */
    int32_t  fuel;                      /* +0x72 */
    int16_t  sink, gust;                /* +0x76, +0x78 */
    uint8_t  b7b, gear, l7d;            /* +0x7B, +0x7C, +0x7D */
    int16_t  turn_limit;                /* +0x7E */
    int16_t  mat_a[9], mat_b[9];        /* +0x80, +0x92 (Q14) */
    int16_t  vtx[4][3];                 /* +0xA4 + 6k: the player's slots 0..3: the draw store's (game.c copies lists vslot[0] after the walk; a rig stand-in: tests/player_draw.h) */
    /* variables outside the record (the V: addresses are the note's) */
    int32_t  v4a7b2;                    /* altitude ceiling */
    int32_t  v4ac08;                    /* warning flags (low byte = V:4AC0B) */
    uint32_t v4ac0c;                    /* long at V:4AC0C: its low byte is V:4AC0F (cues 0x80 crash imminent, 0x40 eject, 0x20 stall lamp), bit 0x200 is V:4AC0E bit 1 */
    uint16_t v4a984, v4a986, v4a98a, v4a982, v4ab98;
    int16_t  v4a9fe;
    uint8_t  v4a83c, v4a847, v4a83d, v4a844, v4a857, v4a858, v4a866, v4a867;
    uint8_t  v4a890, v4a903, v4a95e, v4a952, v4a957, v4a910;
    int8_t   v4a928;                    /* throttle preset (0 none, -1 idle) */
    uint8_t  v4a878, v4a8f6, v4a8fd, v4a968, v4a84f, v4a87d, v4a850, v4a8fe, v4a8ff;
    /* milestone 5 (note 07 sections 8 to 10): the chaff and flare cool-downs V:4A906 / V:4A907 (set to 30 by the keys, decreased once per unpaused pass: while either is above 0 the
     * damage handler's engine-failure outcome does nothing), the ECM switch V:4A96D, the frame counter V:4A992 as the damage parity and the gun's wobble read it (the game sets it before
     * the player's step), and ev_lost: a missile took the lost mark in this step (B20 bit 5): the game clears the fire-prompt state V:4A96C (radar module) and the flag */
    uint8_t  v4a906, v4a907, v4a96d, ev_lost;
    uint32_t v4a992;
    int8_t   v4a9a8;
    /* the pointer throttle (note 05 17.4): P = V:4A830 (pointer height, -960..960), Q = V:4A834 (the value the throttle last agreed with);
     * the ejection latch V:4AA7A (0 until the seat is spawned, then the record's offset 0x0200: blocks every later cycle, 17.10); the statistics
     * counter V:4A963 (17.3); the viewed cell V:4AA04 / V:4AA06 (17.10, +0x0B) */
    int16_t  v4a830, v4a834;
    uint16_t v4aa7a;
    uint8_t  v4a963;
    uint16_t v4a994, v4a996;            /* the seat view (V:4A994 = 1 and V:4A996 = the viewed record's offset, 0x0200, after the seat spawn in the cockpit view): the player is then no longer the viewed entity (V:4A996 non-zero) */
    int16_t  v4aa04, v4aa06;
    /* derived by the port, not part of the original's state */
    uint32_t reads[8];                  /* counters of table over-reads and unreachable bounds (flight.h, FL_COUNT_*; tests only) */
    uint32_t steps;                     /* step counter (V:4A992 role: the draw-schedule counter n) */
    int16_t  panel_speed;               /* speed digits as the panel shows them (note 06 cadence) */
    int32_t  panel_alt;                 /* altitude digits as the panel shows them */
    uint8_t  ev_liftoff, ev_touchdown, ev_crash, ev_jolt;   /* the log's events of note 08 10.7/10.2, raised by the writers (0x171E2 lift-off, 0x16F6A touchdown, 0x16FB0 and 0x2A8C6 crash) and cleared by the consumer (game.c); flight_step never reads them. ev_jolt: the jolt kind of 0x171D4 at a lift-off, stored for ANY record (note 08 8.3), so a lead's lift-off raises it too */
    int16_t  lvl_roll;                  /* milestone 5 (note 07 3.8 item 4): the roll rate target a missile's input conversion picks from the roll word (-960, +960 or 0) when its flag bits 5 and 6 are set; used by the response in this step only */
    uint8_t  lvl_on;
    uint8_t  ev_rearm;                  /* milestone 5 (note 07 7.1): 1 in the step in which the landing complete routine 0x1F11E ran (the game rearms: weapons_rearm); cleared by the consumer */
    uint8_t  jam;                       /* milestone 5 (note 07 8.3, 9.5): a missile aimed at the player while the ECM is on (the response divides the turn target by 5); set by the missile gate each pass */
    uint8_t  ev_ground;                 /* milestone 6 (note 10 8.2): the ground-contact sound volume (0x0F nose settling, 0x1E or 0x28 touchdown) the step asks for, 0 none; the wrapper plays it when the record is the camera's; cleared by the consumer (game.c) */
    uint8_t  ev_bump;                   /* milestone 6 (note 10 8.2): 1 in the step of a first contact at an indicated speed of 0x3C0 or less (0x2A834): the bump noise (0x1C, 0x30); cleared by the consumer */
    uint8_t  ev_beep;                   /* milestone 6 (note 10 7.4): the gear-down warning's HUD beep (0x2837E, n = 4) in this step; cleared by the consumer */
    uint8_t  ev_seat;                  /* milestone 5 (note 07 6.4): 1 in the step in which the ejection count reached 3 and the latch V:4AA7A was set: the world update's seat routine fills a record with the seat; cleared by the consumer (entities.c) */
    uint8_t  ev_object;                 /* milestone 5 (note 07 5.1): the model id (0x11 explosion, 0x12 ground object) of a secondary object the step asks for at ev_obj_pos; cleared by the consumer */
    int32_t  ev_obj_pos[3];
    int16_t  ev_obj_cell[2], ev_obj_fine[2];   /* the object is a copy of the record's first 32 bytes: its cell and fine words are the record's own (note 07 5.1), only the longs carry a backed-up point */
    uint8_t  ev_obj_sub, ev_obj_nb;
    int32_t  ev_obj_alt8;
    uint16_t self_off, view_off;        /* a non-player record's pass (entities.c sync_globals): the record's own offset (V:4AA6E) and the viewed record's (V:4A996); the ship test adds the mover's vertex slots only when they are equal (note 07 10.2) */
    int16_t  slot[4][3];                /* a non-player record: its vertex slots 0 to 3 (+0xA4 to +0xB6) as the last draw left them (game.c copies them from the display lists after the walk); the ship test, the landmark phase and the ground test read them (the player: vtx) */
    uint8_t  entity;                    /* 1 in the pass of a non-player record (flight_step_entity: the rules where the original's entity path differs from the player's) */
} flight_state_t;

typedef struct {
    int32_t pos[3];
    int32_t pitch, roll, yaw;   /* 28800 units per circle, derived from f (see flight.c): pitch is nose-UP positive
                                 * (the negated pitch word of the note), yaw is the heading word, roll is the note's
                                 * roll word as a signed value (positive = left bank). */
    int32_t speed;              /* W, twelfths of a knot (note 05 "Speed unit") */
    int32_t throttle;           /* T, -32..120 (note 05) */
    uint8_t gear, hook, on_ground, on_carrier;
    flight_state_t f;           /* the flight model's state; pos above is the position of the record (+0x14..) */
} aircraft_t;

#endif
