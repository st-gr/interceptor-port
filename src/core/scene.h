/* src/core/scene.h - everything a frontend needs to draw one frame.
 * Plain data, no pointers into the core, no draw calls. */
#ifndef INTERCEPTOR_SCENE_H
#define INTERCEPTOR_SCENE_H
#include <stdint.h>

/* Capacities of the display lists (docs/re/04a-display-lists.md section 9): one queue keeps 69 entries, three queues give 207 terrain
 * entries at most; the entities number at most 16 plus 16 secondary objects. The scene's entry stream holds both and the
 * specks of the near-ground pass. */
#define GAME_MAX_TERRAIN 207
#define GAME_MAX_OBJECTS 32
#define GAME_MAX_SPECKS 96   /* table A of the near-ground pass has 96 entries (note 04, "Stage [90]") */
#define GAME_MAX_ENTRIES (GAME_MAX_TERRAIN + GAME_MAX_OBJECTS + GAME_MAX_SPECKS)
#define GAME_MAX_TEXT    48       /* lines on screen at once (the longest lists of note 08 hold 17 lines; a mission briefing list is longer, milestone 5) */
#define GAME_TEXT_LEN    46       /* the longest text line is 45 characters (note 08 section 2.2) plus the terminating NUL */
#define GAME_MAX_EFFECTS 64
#define GAME_MAX_CONTACTS 16

/* All positions are in original world units (1 unit = 1 unit of the original
 * fixed-point world). The metre conversion is a constant in
 * docs/re/02-assets-and-tables.md, applied only by frontends that need it. */

typedef enum {
    GAME_MODE_SPLASH = 0,
    GAME_MODE_MENU,
    GAME_MODE_BRIEFING,
    GAME_MODE_FLIGHT,
    GAME_MODE_LOG,
    GAME_MODE_DEBRIEF,
    GAME_MODE_PAUSED,
    GAME_MODE_GAMEOVER,  /* the crash allowance ran out (note 05 17.17 rule 7): scene.text_id names the text; the game-over text is in scene.text[] (note 08 5.12) */
    /* milestone 4 (note 08 section 5): the front-end screens. The values above keep their numbers (the goldens print GAME_MODE_FLIGHT). The first screen of
     * a session is CREDITS (the original has no picture before it, so SPLASH is no longer entered; splash_asset stays for the frontend's own use). */
    GAME_MODE_CREDITS,          /* 5.2: the credits text, any key goes on */
    GAME_MODE_LOGDISK,          /* 5.3: the log-disk prompt, RETURN goes on (the port shows the write-protected variant; the disk is not read) */
    GAME_MODE_SELECT_LOCATION,  /* 5.6: the location selection over the map, and (free flight) the black passes and the fade-in before it */
    GAME_MODE_SELECT_AIRCRAFT,  /* 5.7: the aircraft selection over the map */
    GAME_MODE_WELCOME           /* 10.4: the first-tour or continuing-tour welcome and the callsign entry (menu palette, text only) */
} game_mode_t;

typedef enum {
    GAME_VIEW_COCKPIT = 0,
    GAME_VIEW_EXTERNAL,
    GAME_VIEW_TOWER,
    GAME_VIEW_CHASE
} game_view_t;

/* Positions are exact integers in position units (the world wraps at 2^29, so a float would
 * quantise them to 32 units). Angles are integers in the original's unit, 28800 per full circle
 * (note 03; 80 per degree). yaw is 0..28799; pitch and roll are signed.
 * ONE CONVENTION for every attitude in the scene (the camera's and the entries'): pitch positive NOSE UP (the record's pitch word is its negation), yaw the heading word (clockwise),
 * roll the record's ROLL WORD ITSELF: right wing down is NEGATIVE, a positive roll is a left bank (note 03, "Roll sign and the angle inputs of the camera matrix": fx_cam_ypr takes the record
 * words unchanged, so the camera matrix needs no flip). The HUD, the horizon and the attitude ball are defined right wing down positive (note 06) and take the NEGATED scene roll. */
#define GAME_ANGLE_CIRCLE 28800
typedef struct { int32_t x, y, z; } game_vec3_t;

/* The camera as the core resolved it (the frontend only draws from it).
 * view: cockpit (V:4A83D = 0), external (ring index != 4), chase (external, ring index 4: behind the aircraft,
 * the default of KPENTER) or tower, the map view (note 06 "View kinds, settled"). TOWER is exactly the core's map gate: V:4A865 > 0 and V:4A83D != 0; the frontend draws the map
 * when, and only when, the view is TOWER (a map request still waiting for the view switch, as in a pause, leaves the view at COCKPIT).
 * head: cockpit view code 0..13 (note 06 "Head views": 1 to 11 turn the head about the aircraft's axes, 12 up, 13 down); 0 outside the cockpit. The camera words are the composition of the
 * head triple with the aircraft attitude (pitch, yaw and roll all change when the aircraft is pitched or banked).
 * ring: external direction 0..9 (note 04: index 4 behind); 4 when not external. */
typedef struct {
    game_vec3_t pos;
    int32_t pitch, roll, yaw; /* 28800 units per circle */
    game_view_t view;
    uint8_t head;
    uint8_t ring;
    int8_t hud_step;          /* the HUD step byte of the cockpit view (note 06 "Head views": the table byte of the code, -50 for code 4, or 50 by the equal-row test; 50 in an external view); the overlay's offset is 16 times it */
    /* milestone 5 (note 07 section 6): the camera MODE V:4A966 is the front end's alone: 0 in every flight, 2 the approach to the first hostile, 3 arrived, 4 the map overview, 5 the location
     * selection, 6 the fly-in. There are no weapon or crash modes (6.1): the other aircraft and the wrecks are followed through the viewed record, the trail record and the ring tables. */
    uint8_t mode;             /* V:4A966, 0..6 */
    uint8_t viewed;           /* V:4A994: the index of the record the views are about (0 the player, 1 to 3 or 5 the ejection seat or parachute, 4 the training lead) */
    uint8_t fixed;            /* 1 while the fixed camera is in force (SHIFT and KP-ENTER, 6.3): V:4A86D is clear and the camera stands at the start location's point and looks at the viewed record */
    uint8_t trail;            /* V:54E72: the record whose camera trail the drawer shows (part B section 13); 0xFF none */
} game_camera_t;

/* One entry of the original's draw order (docs/re/04a-display-lists.md sections 4, 8 and 9): the stream is walked in the order
 * queue A (the walked window), queue B, the specks of the near-ground pass (stage [90]), the direct entities, then queue C with its entity
 * entries where the queue puts them. Only the entries the walker passes to the draw routine appear: the ones it skips (the plain
 * models above the height limit, the entries that drew nothing and are waiting for their re-test) are left out.
 *   id        the item id (descriptor index) of a terrain entry; the record index 0..15 of an entity or secondary object
 *   asset_id  the model to draw: item id + 16 for terrain (the object id of note 02), the entity's model for an entity, the id word + 16 for an effect object (the id words 0x11 to 0x13 name the descriptors,
 *             the models are 33 to 35, note 07 11.1)
 *   x, y, z   the queue entry's position words (+0x06, +0x08, +0x0A): units of 2^lod u (u = 256 position units) from the corner of the
 *             viewer's C cell as it was at the build; for an entity they hold the tile offset plus the sub-cell corner only
 *   key       the queue entry's key long (+0x0C)
 *   pos       the position to draw at, in position units (the words shifted back by lod, plus an entity's own offsets), in the same
 *             frame as camera.pos (continuous across the wrap of the world)
 *   pitch, roll, yaw   the entity's attitude (28800 per circle); 0 for terrain
 *   dist, countdown, counter, prev_result   the walk state of the entry (+0x10, +0x12, +0x13 after this walk's decrement, and the
 *             result of the previous walk, V:4AB72): what the drawer's visibility pre-test reads
 *   result    the value this walk stored at +0x14 (positive, or -1), computed in the core by the drawer's rule (note 04a section 8b);
 *             the frontend never reports anything back
 *   draw      1 when the drawer ran the primitive layer for this entry (the frontend draws exactly these; the others returned before
 *             the primitives, or are the cockpit block / near body of the player's own entry, which show nothing)
 *   variant   the player's own entry: GAME_VARIANT_NORMAL (the normal list by distance: head views 3 to 9, external views, other
 *             records), GAME_VARIANT_NOTHING (the cockpit block: no program, nothing is drawn) or GAME_VARIANT_NEAR_BODY (the alternate list's
 *             near level: the vertex synthesis and op 4's detail faces in pen 0, two thin triangles at the bottom centre, rows 166 to 179; they
 *             go through the common filler like any polygon, which rejects them in the 144-row cockpit window and draws them clipped in a
 *             167-row view, note 03 "What the near body shows")
 *   op4_closed   the gate of the detail-face handler (op 4) is closed for this record in this view: the handler returns 1 and draws nothing
 *   zoom, zoom_fixed   V:4AAFA and V:4A895: the level is chosen from the zoom-scaled distance word (world_draw_entry), not from the
 *             exact distance */
/* The kinds GAME_ENTRY_SPECK, _SPECK_TRI and _SPECK_FILL are the events of the near-ground pass (stage [90], note 04): each is drawn on the pen layer and clears plane 2 of the pixels it covers (sea
 * pen 6 becomes 2, land 4 becomes 0, sky 5 becomes 1, 7 becomes 3). SPECK: a point sprite, `variant` 0 a 1 by 1, 1 a 2 by 1, the pixel in sv[0], sv[1]. SPECK_TRI: a triangle sv[0..5] whose bounding
 * box takes one of the filler's small shapes (a line, a block of dots or a dot: plane 2 cleared). SPECK_FILL: a triangle of the general fill, which takes the alternate blit 0x34EEC: plane 2 :=
 * plane 3 under the filled area, plane 2 unchanged elsewhere. They carry no model and are never drawn through world_draw_entry. */
typedef enum { GAME_ENTRY_TERRAIN = 0, GAME_ENTRY_ENTITY, GAME_ENTRY_SECONDARY, GAME_ENTRY_SPECK, GAME_ENTRY_SPECK_TRI, GAME_ENTRY_SPECK_FILL } game_entry_kind_t;
typedef enum { GAME_TIER_A = 0, GAME_TIER_B, GAME_TIER_C, GAME_TIER_DIRECT } game_tier_t;
enum { GAME_VARIANT_NORMAL = 0, GAME_VARIANT_NOTHING = 1, GAME_VARIANT_NEAR_BODY = 2 };
typedef struct {
    uint8_t kind;             /* game_entry_kind_t */
    uint8_t tier;             /* game_tier_t */
    uint8_t dclass;           /* descriptor class: the routine of the descriptor (lists.h LISTS_CLASS_*: 1 plain 0x21C40, 2 0x21C34,
                               * 3 0x21D0C, 4 and 5 type B 0x22CFC/0x22D0A; 32 entity) */
    uint8_t lod;
    uint16_t id, asset_id;
    int16_t x, y, z;
    uint32_t key;
    game_vec3_t pos;
    int32_t pitch, roll, yaw;
    uint16_t dist;
    int8_t countdown;
    uint8_t counter;
    int16_t prev_result;
    int16_t result;
    uint8_t draw, variant, zoom_fixed;
    uint8_t op4_closed;       /* entities: the op 4 handler's gate is closed (note 04a P7): it returns 1 without drawing its faces */
    uint8_t op4_neg;          /* entities: the op 4 handler returns -1 (above 5,120 ft, not closed by its first exits; abandons the program) */
    uint16_t fill_alt;        /* V:4A7A4 as the object's drawer found it at its entry (0xFFFF after the near-ground pass until a common drawer entry clears it; a type B drawer inherits it, its pattern fills then take the alternate blit) */
    int16_t pbuf_in[3];       /* the depth words at the vertex positions 4 to 6 of the polygon buffer V:5103C as the drawer found them (note 03, "Op 9 reads past its quad") */
    int16_t sv[6];            /* a near-ground speck (stage [90]): the pixel (sv[0], sv[1]) of a sprite, or the three screen vertices of a triangle */
    uint16_t fill_toggle;     /* the fill state's toggle word V:4A7A2 as the object's drawer found it at its entry (note 03, "Fill state"): the one word a type B drawer inherits from the previous object */
    uint16_t zoom;
    /* milestone 5 (note 07 sections 1, 5): the display state of an entity or secondary object entry (all zero for terrain and specks).
     *   rec_class   an entity: the class byte +0x62 (note 07 1.3); a secondary object: 0
     *   damage      an entity: GAME_DMG_* bits of W0 and B20 (a bit is set while the condition holds); a secondary object: 0
     *   phase       a secondary object: the life counter +0x04 (0x7F at creation, counted down once per pass), which the draw handlers read as the phase of an explosion or cone (part B
     *               section 11); an entity: the counter +0x4C (the dying countdown 15 to 0, then 20 after the free)
     *   model_id    the model id the record carries (a secondary object: the id word +0x02, 0x11 to 0x13, the descriptor index of an effect object, whose model is that plus 16; an entity: the template id copied by the placement, equal to asset_id) */
    uint8_t rec_class, damage;
    uint8_t cone_list;        /* an entity of the models 2, 13 and 11 (note 07 11.5): 0 the model's own detail list, 1 to 4 the phase list its counter selects (lists.h lists_walk_t.cone_list); the draw takes that list's level */
    int16_t phase;
    int16_t t04;              /* the word op 70 of the model program reads: the secondary object's life word at the index of the drawn record: op 70 reads the object table 0x4D23C + (V:4AA6C << 5), never an entity record's field (note 07 11.3, 04a) */
    uint16_t model_id;
} game_entry_t;
enum { GAME_DMG_HIT = 1, GAME_DMG_CRASHED = 2, GAME_DMG_DYING = 4, GAME_DMG_OUT_OF_CONTROL = 8, GAME_DMG_GROUND = 16, GAME_DMG_THREATENED = 32 };

typedef struct { int16_t bearing_deg, range; uint8_t hostile; } game_contact_t;

/* One radar-scope object (note 06, "Radar scope"): an entry of the radar list as the list builder (0x1FF24, the AI slice) leaves it,
 * the position relative to the player rotated into the player's heading frame (x lateral, z ahead, original units), and the
 * object bytes the pen rule reads. alt8 is the object's long at +0x10 (its y shifted right by 8): a blip is two pixels wide when it is
 * at least the camera's y shifted right by 8. */
#define GAME_MAX_RADAR 32
typedef struct {
    int32_t x, z;
    int32_t alt8;             /* object +0x10 */
    uint8_t cls;              /* object class byte (E+0x62) */
    uint8_t b01;              /* object byte +1 (bit 3 friendly) */
    uint8_t b20;              /* object byte +0x20 (bit 6 detected, bit 1 hidden) */
    uint8_t flags;            /* bit 1: the designated target (drawn on alternate frames) */
} game_radar_t;

/* One mark the map routine draws (note 06, "Map renderer", the entities 0 to 15; core: mapmarks.c decides which, where, in which shape and pen): the screen point handed to the shape drawer (the row
 * is the call row plus one), the record index, the shape (0 to 15 the direction sectors of the table at 0x2FED8, 16 the ship shape) and the pen. In ascending record order. */
#define GAME_MAX_MARKS 16
typedef struct { int16_t x, y; uint8_t rec, shape, pen; } game_mark_t;

typedef struct { int16_t x, y; uint8_t wide, pen, rec; } game_blip_t;   /* a scope blip: the screen point (row includes the HUD offset), two pixels wide, the pen */

typedef struct {
    int32_t airspeed_kt, altitude_ft, heading_deg;   /* no vertical-speed figure: the original has none (note 05 C30) */
    int32_t throttle_pct, fuel_pct;
    uint8_t gear_down, hook_down;
    uint8_t weapon;           /* 0 gun, 1 AIM-9L, 2 AIM-120 */
    uint16_t gun_rounds, aim9_count, aim120_count, flares, chaff;
    char warning[GAME_TEXT_LEN];
    int32_t pitch_deg, roll_deg;
    uint16_t contact_count; game_contact_t contacts[GAME_MAX_CONTACTS];
    int16_t target_box_x, target_box_y; uint8_t target_box_visible;
    int16_t ils_h, ils_v; uint8_t ils_visible;
    /* Raw cockpit state for the frontend's HUD, panel and warning line (docs/re/06-cockpit-hud.md). The note's readouts are formulas
     * over the player's entity words, so the scene carries those words (E+offset) and the few game variables (V:xxxxx) they use,
     * unconverted. The core sets cp_valid to 1 when it fills the block (the flight mode does); while it is 0 the frontend draws no
     * cockpit. The core owns the warning arbiter, the tick generator, the code word, the latch and the frame counter n and publishes
     * the shown entry, the blink phase and the sound request below; the frontend keeps only draw caches and redraw timers. */
    uint8_t cp_valid;
    uint8_t cp_class;         /* E+0x62: 0x11 F/A-18, 0x10 F-16 (outline for 0x10..0x12, scale HUD for 0x10) */
    uint8_t cp_sel;           /* E+0x63: high nibble weapon (0 none, 0x10 gun, 0x30 AMRAAM, else Sidewinder), low nibble radar range */
    uint8_t cp_stock;         /* E+0x5F: high nibble Sidewinders, low nibble AMRAAMs */
    uint8_t cp_b03;           /* E+0x03: bit 3 afterburner */
    uint8_t cp_b20;           /* E+0x20: bit 2 stall */
    uint8_t cp_b7c;           /* E+0x7C: gear byte (bit 7 selected, bits 4..6 progress) */
    uint8_t cp_throttle;      /* E+0x2B: throttle byte as an absolute value (0x78 = 100 percent) */
    uint16_t cp_w00;          /* E+0x00: bit 0x8000 hit this step, bit 0x0800 brake, bit 0x40 drawn on the map, bit 0x08 friendly */
    uint16_t cp_rounds;       /* E+0x60: gun rounds */
    uint16_t cp_w6c;          /* E+0x6C: gear lamp rule input */
    int16_t cp_pitch_rate;    /* E+0x56 */
    int16_t cp_gcorr;         /* V:4A9FE (G meter correction) */
    int32_t cp_heading80;     /* E+0x68: heading in 1/80 degree (0..28799) */
    int32_t cp_speed_w;       /* E+0x6E: speed word (12 per knot) */
    int32_t cp_y;             /* E+0x18: altitude long (1024 per 5 feet) */
    int32_t cp_lat;           /* E+0x1C */
    int32_t cp_lon;           /* E+0x14 */
    int32_t cp_fuel;          /* E+0x72: fuel long (pounds times 256) */
    uint8_t cp_gsign;         /* V:4A982 bit 1 */
    uint8_t cp_eject_armed;   /* V:4A863 */
    uint8_t cp_eject_key;     /* the E key was taken in this pass (note 07 6.5: the key sets the EJECT lamp timer V:4A8FA to 8 before this pass's own draw, which counts it down to 7: the lamp is lit in the pictures of the key's pass and every second pass after) */
    uint8_t cp_ecm;           /* V:4A96D */
    uint8_t cp_gear_lamp;     /* V:4A8FE */
    uint8_t cp_zoom;          /* V:4AAFA: 0x80, 0x40 or 0x20 (radar magnification digit) */
    uint8_t cp_crashed;       /* 1 after the crash hand-over (V:4A985 bit 6 clear): the external strip is not drawn */
    uint8_t cp_threat;        /* V:4A926 threat bits (threat lamps) */
    uint8_t cp_strip_off;     /* V:4A891 clear for this pass: the external text strip is not drawn (one pass after a switch to another entity) */
    uint32_t cp_n;            /* n = V:4A992 as the draws of this pass saw it (0 on the first flight pass); stands still on paused passes */
    uint8_t cp_redraw;        /* V:4A8EF as the draws of this pass saw it (the core's counter: 3 on the first pass of a flight and after each writer of the helper 0x1EFF4 (note 07 12.1: every 32nd pass, the pause resume, the view switches and head keys, the seat switch, the jolt call); the panel, the HUD figures and the scope read it) */
    uint32_t cp_pass;         /* counts the passes of the frame routine that reached the draws, paused passes included: the frontend runs its per-pass draw state
                               * (redraw timers, blink counters, marker slew, EJECT timer) once per new value */
    uint8_t cp_paused;        /* V:4A866 during the draws of this pass: the speed, altitude and fuel caches draw the old figure instead of taking a new one (note 06 rule 4) */
    uint8_t cp_hud_on;        /* V:4A859: the H key state */
    uint16_t cp_msg_code;     /* V:4AB98: the code word */
    uint8_t cp_msg_entry;     /* V:4AB96 (low byte): the warning table entry the line shows (0 is the blank entry) */
    uint8_t cp_msg_ext;       /* the gate of 0x11426: V:4A95E == 2 (training part 1): the message line (row 192) is drawn in the external views too, so the lesson names show (note 08 8.7); in every other mode the external views hide it */
    uint8_t cp_msg_phase;     /* V:4A986 bit 5: 1 when a blinking alert shows its own text, 0 for its partner or the blank phase */
    uint8_t cp_sound_req;     /* V:4A91B: the sound byte of the installed entry (information: the cues 0x04 and 0x08 it causes are the core's, note 10 8.3) */
    /* Target symbols (milestone 5; the weapon slice produces these, all zero until then; note 06 "Target symbols"). */
    uint8_t cp_designated;    /* V:4AA78 not negative: a target is designated */
    uint16_t cp_range;        /* E+0x4A: range to the target */
    int16_t cp_pipper_x, cp_pipper_y; /* V:4A9F6, V:4A9F8: gun-sight and lock circle centre */
    /* the rest of the target block (part B sections 8 and 12; the weapon slice fills it, zero until then; target_box_x/y/visible above is the box) */
    uint8_t cp_target_rec;    /* V:4AA78 as the record index of the designated target, 0xFF none */
    uint8_t cp_target_cls;    /* the designated record's class byte +0x62 (0 when none) */
    uint8_t cp_lock;          /* V:4A920: the lock state (0 none) */
    int16_t target_box_w, target_box_h;   /* the box's half-sizes in pixels */
    uint16_t cp_target_speed; /* the designated record's speed word +0x6E (0 when none) */
    uint16_t radar_count; game_radar_t radar[GAME_MAX_RADAR];
    /* milestone 5, radar.c (note 07 section 12): the scope blips of the last walk and the blips to erase first (the page's walk two before), the target symbols and the
     * message-line target data. cp_lock above is V:4A96C (0 none, 1 and 2 the fire prompt), not V:4A920. */
    uint8_t mark_count;                      /* the map view's marks (game_mark_t, ascending record index) of the last pass, empty unless the map is shown */
    game_mark_t marks[GAME_MAX_MARKS];
    uint8_t panel_block;                     /* the frame routine ran the panel block in this pass (game.c panel_block_runs: the cockpit view, or an external view while V:4A8EE is non-zero): the original's side effects of the panel block (the counters, the scope walks, the HUD external return) happen in it; it draws nothing visible in an external view (07 12.1, combat-panel-block.txt), so the frontend draws the external view only and uses this flag for nothing */
    uint8_t scope_walked;                    /* the scope walk ran in this pass (note 07 12.2, 12.3: a cockpit pass with R above 1 or bit 1 of n clear; the pause byte of a script's count does not stop it; on a pass with R of 1 or more the scope draw walks twice and the erase list is the first walk's): the frontend erases the erase list and plots the blips of this pass, and does nothing else to them */
    uint8_t blip_count, erase_count;
    game_blip_t blips[10], erase[11];        /* erase: up to ELEVEN entries, the walk's erase reads one past the page (note 07 12.3 step 1); an entry may be stale data of the other page: clip it to the screen */
    uint8_t tgt_prompt;                      /* V:4A96C: 0 none, 1 and 2 draw the prompt (the gun form with the gun selected, the missile form otherwise) */
    uint8_t tgt_sel;                         /* +0x63 of the VIEWED record (V:4A996 added to the record base; note 07 12.7): its high nibble picks the gun form (1) or the missile form of the prompt */
    uint8_t tgt_marker;                      /* 0 none, 1 the diamond, 2 the ring (lock box) at (tgt_marker_x, tgt_marker_y) */
    int16_t tgt_marker_x, tgt_marker_y;
    uint8_t tgt_pip_pixel, tgt_circle;       /* the pipper pixel at (cp_pipper_x, cp_pipper_y); the lock circle at (tgt_circle_x, tgt_circle_y) */
    int16_t tgt_circle_x, tgt_circle_y;
    int16_t tgt_dot;                         /* the range dot's position index 0..75, -1 none */
    uint8_t tgt_number; char tgt_number_text[8];   /* the range number: a space (closing) or minus, four digits */
    uint8_t tline_mode;                      /* the message line: 0 the posted message, 1 the blank message, 2 target data */
    uint8_t tline_redraw, tline_cls, tline_detected, tline_colour, tline_page, tline_col;
    char tline_digits[8];
} game_hud_t;

/* One line of a text record of note 08 (section 2.2): the original addresses text by scanline and byte column, in 7-pixel cells of a 5x7 glyph font. The core
 * has already placed the line (row includes the record's line steps, the continuation ids and the line step of the menu list); the frontend draws the first
 * `shown` characters of text[] in `pen`, a pen of the live 32-colour palette (the palette_regime below says which table), at x = 8 * col + 7 * i, scanlines
 * row .. row + 6. The text is the asset's, the core only copies it.
 *   row     scanline of the first glyph row (0..199)       col    byte column, x = 8 * col (even)
 *   pen     0..15                                          flags  bit 0: typed-out line, bit 1: spaces are skipped without delay (note 08 2.2)
 *   step    the line step in force when the line was placed (11 or 12 scanlines)
 *   shown   characters drawn so far (0..length); the core advances it at the engine's pace (note 08 3.4); a line with shown == 0 is not drawn yet
 *   length  characters in the line (0 for a blank line)
 *   id      the id of the text record the line belongs to (note 08 section 2: the record number, bits 14 and 15 of the posted id removed; display state, not drawn) */
typedef struct {
    uint16_t id;
    uint8_t row;
    uint8_t col;
    uint8_t pen;
    uint8_t flags;
    uint8_t step;
    uint16_t shown;
    uint16_t length;
    char text[GAME_TEXT_LEN];
} game_text_t;

/* A number on the map of the location selection (note 08 5.6): drawn by the original's map code, not by a text record, in a 3x5 pixel glyph (the numerals 1 to 4) at a pixel of the
 * 320x200 page: x, y the top-left pixel; pen 9 of the flight palette (amber). The core knows the four starts' places on the overview map (note 04, "The start marks of the overview map": the mark pass 0x2FAB6 projects the start table by the overview camera, the dot at
 * the point and the glyph at ((x and not 3) + 8, y); screen.c's table is that projection for the four starts, pixel for pixel). On the aircraft screen only the number of the chosen start stays. */
#define GAME_MAX_MAP_MARKS 4
typedef struct { int16_t x, y; uint8_t digit, pen; int16_t dot_x, dot_y; } game_map_mark_t;   /* dot: the grey pixel (pen 2) of the start's own marker beside the number */

/* The audio commands of one step (note 10 section 9.2; src/core/sound.h): the core runs the sound library of the original as an integer model, one server per vertical blank, and hands the
 * Paula-like mixer the REGISTER WRITES the library made, in order, each stamped with the blank of the step (6, 6, 6 or 7 blanks a step, 25 per 4 steps) in which it was made and the colour clock within
 * that blank (note 10 9.2: the writes of a block end's interrupt take effect at the end, which lies inside the blank; the mixer applies each write at its position). The mixer
 * implements the hardware only (note 10 1.4): a voice has a latched block (the location and length registers), a DMA bit, a period and a volume register; at DMA on it starts the latched
 * block from its first byte; at the end of a block it restarts the block latched by then (a LATCH written meanwhile takes effect at that end); DMA off silences it; period and volume
 * apply at the next output sample. Only the writes that change a register are issued (a rewrite of the same value is omitted).
 *   LATCH     object (0..0x24), offset and length: the byte range of the object's buffer (a file's range, or the core's tone and noise buffers: sound_buffer) the voice will play next
 *   DMA_ON    the voice's DMA is switched on     DMA_OFF  switched off
 *   PERIOD    value = the period register (at least 0x7C)     VOLUME  value = the volume register 0..63, already capped by the library's master level */
#ifndef GAME_AUDIO_CMD_DEFINED
#define GAME_AUDIO_CMD_DEFINED 1
typedef enum { GAME_AUD_LATCH = 1, GAME_AUD_DMA_ON, GAME_AUD_DMA_OFF, GAME_AUD_PERIOD, GAME_AUD_VOLUME } game_audio_op_t;
typedef struct {
    uint8_t  op;          /* game_audio_op_t */
    uint8_t  voice;       /* 0..3 */
    uint8_t  object;      /* LATCH: 0..0x24 */
    uint8_t  blank;       /* 0..6: the blank within this step */
    uint32_t clock;       /* colour clocks from the start of that blank (0..71,050): 0 for the writes of the server and of the wrappers (they act at the blank's start); for the handler of a block end the clock at which the block ended */
    uint16_t value;       /* PERIOD or VOLUME */
    uint32_t offset, length;   /* LATCH: bytes into the object's buffer */
} game_audio_cmd_t;
/* The most register writes one step can make. Only a write that changes a register is emitted. Per voice and blank (note 10 4.1 to 4.3, 9.2): the server writes the period and the volume (2) and, when its
 * script ends the voice, the stop's three (volume, period, DMA off): 5. On top, every block end that loads a different object writes the latch, the period and the volume (3); a block that loads the
 * same location and length again with the same period and volume (a looping tone or noise) writes nothing. The block ends in a blank (hw_advance, 9.2): the sample in progress ends, then whole blocks
 * of L samples at the period P (at least 0x7C = 124 colour clocks) fit 71,051 / (L P) times, so n = 1 + floor(71,051 / (L P)) at the most. The tone (object 4, voice 3) has L = 32 and so up to 18 block
 * ends a blank at P = 124, but they all re-latch the same buffer with the same period and volume and write nothing; every other object is at least 568 samples long in the tables (objects 0x23 and 0x24,
 * note 10 2.2; the noise buffer is 2,048): n = 1 + floor(71,051 / (568 x 124)) = 2, so at most 5 + 3 x 2 = 11 writes a voice and blank, 4 x 7 x 11 = 308 for the server and the blocks of a step of seven
 * blanks (a 6-blank step has fewer). The wrappers called by the passes add 12 for a stop-all, 10 for one start (the stop's three, the load's four, the second load's three) and 20 for an engine pair; the
 * flight pass makes at most a dozen calls (engine, gear, cue, bang, the keys' beeps, the contacts), 120 writes. A text-screen step makes one key click per pass (7 writes: a click restarts voice 3, the
 * stop's three and the start's four) and no other call; its passes are the step's cost (37,500 units, 1,000 a pass at least, note 08 3.8, 60 at the most for the fastest list) so 420. A step is a text
 * step or a flight step: 308 + 420 = 728 at the most, below the 1,024. A table with sound files shorter than the shipped ones raises n (hw_advance runs at most 64 block ends a voice and blank, so
 * 5 + 3 x 64 = 197 a voice and blank is the ceiling for any table). The largest step of the ten golden scripts (real assets) made 56. A write that did not fit is counted (audio_overflow), never hidden;
 * the tests and the golden runner require 0. */
#define GAME_MAX_AUDIO_CMDS 1024
#endif

typedef enum { GAME_FX_SHELL = 0, GAME_FX_MISSILE, GAME_FX_EXPLOSION, GAME_FX_SMOKE,
               GAME_FX_FLARE, GAME_FX_CHAFF, GAME_FX_POD,
               GAME_FX_BURST   /* milestone 5 (note 07 11.4): the dying burst of a record, drawn after its model and before its trail: age 0..15 = 15 - the counter, model = the kind (1 air or land, 2 water, 3 cruise missile) */
} game_fx_kind_t;

/* milestone 5: kind as above; pos in position units; age in passes since the effect began; rec the secondary object (or record) index it belongs to (0xFF none); model the model id of
 * the object (0x11 explosion, 0x12 and 0x13 ground and cone objects, note 07 1.5); phase the object's life counter +0x04 (0x7F at creation) the draw handlers read; owner the record that caused it. */
typedef struct { game_fx_kind_t kind; game_vec3_t pos; uint16_t age; uint8_t rec; uint8_t owner; uint16_t model; int16_t phase; } game_effect_t;

/* The trail ring of the followed record (note 07 13.1, 13.2): six positions, the count, the write index, the record's trail length (0 to 4) and the out-of-control bit of the record; the
 * frontend draws the discs (13.2) when the length is above 0 and the record is camera.trail. */
typedef struct { int32_t ring[6][3]; uint8_t count, index, length, ooc;
                 int32_t vel[3], pos[3], speed; } game_trail_t;   /* vel, pos, speed: the followed record's velocity longs, position longs and speed word +0x6E, which give the direction of the drawer (13.2 step 1) */

/* The sky fill, the ground base and the sea mesh of the frame (docs/re/04-scene-composition.md, "Sky fill and ground polygon", "Stage [68]"): the core computes
 * the polygon and the polygons of the mesh, the frontend fills them. The variable names are the original's.
 *   matrix        the nine words of V:4AC90 after the zoom row scale (the matrix the objects use), cam_y the long V:4AB1E (minus the camera height), pitch_word V:4AB42
 *                 (cockpit) or V:4AB4C, roll_word V:4AB4A, both 0..28799; zoom the triple V:4AAF6; cockpit = V:4A83D clear; row_limit V:4AA3C
 *   side_flag     bit 1 of V:4A982 (0x143F2, every frame); sea_zone V:4A953 (the ground pen is 6 and the mesh is not drawn when set); sea_zone2 V:4A954
 *   sky_n, sky_xy the sky polygon in screen coordinates (0 vertices: none; the whole window is 4 vertices); ball_n, ball_xy the second call of the routine with the
 *                 aircraft attitude (the attitude ball, note 06; cockpit view only)
 *   tiny_pen, tiny_mask   V:4AA0C and V:4A79E as the fill finds them for the small-polygon cases (13 and 1 in the cockpit view; the external view: pen 0, the mask 1 or 0xF)
 *   fade_target   V:4A959: 2 while the strain accumulator V:4A941 exceeds 5 in the cockpit view of the own aircraft, else 15. The core owns the target; the
 *                 level V:4A958 is display state: the frontend steps it at the 50 Hz blank rate (one level per 2 blanks down, per 3 up) and loads fade step 15 - level
 *   sea           the polygons of the sea mesh pass, already clipped (shift 0, fx_clip), in drawing order; the pen is 6 and the plane mask 2 (plane 1 set: land 4
 *                 becomes 6, sky 5 becomes 7)
 * valid is 0 until the first flight step. */
#define GAME_MAX_SEA_POLYS 64
#define GAME_SEA_POLY_VERTS 24
typedef struct { uint8_t n; int16_t xy[2 * GAME_SEA_POLY_VERTS]; } game_sea_poly_t;
typedef struct {
    uint8_t valid;
    int16_t matrix[9];
    int32_t cam_y;
    int16_t pitch_word, roll_word;
    uint8_t cockpit, row_limit;
    int16_t zoom[3];
    uint8_t side_flag, sea_zone, sea_zone2, fade_target;
    int8_t strain;            /* V:4A941, informational */
    uint8_t sky_n; int16_t sky_xy[10];
    uint8_t ball_n; int16_t ball_xy[10];
    uint8_t tiny_pen, tiny_mask;
    uint16_t sea_count;
    game_sea_poly_t sea[GAME_MAX_SEA_POLYS];
} game_sky_t;

typedef struct {
    game_mode_t mode;
    uint32_t tick;            /* frames since game_create */
    game_camera_t camera;
    uint16_t entry_count;
    game_entry_t entries[GAME_MAX_ENTRIES];   /* the display lists in the original's draw order */
    game_hud_t hud;           /* written by the flight module (Task 5); all zero until it exists */
    game_sky_t sky;           /* the sky fill, the ground base and the sea mesh (above) */
    uint16_t text_count;
    game_text_t text[GAME_MAX_TEXT];
    uint8_t map_mark_count;
    game_map_mark_t map_marks[GAME_MAX_MAP_MARKS];   /* the numbers on the location map (above); in the selection modes the world entries are the map overview seen from above */
    uint16_t audio_count;
    uint8_t audio_overflow;   /* the writes that did not fit GAME_MAX_AUDIO_CMDS (0 by the bound above; a non-zero value is a bug) */
    game_audio_cmd_t audio[GAME_MAX_AUDIO_CMDS];   /* the voice commands of the step, in order (above) */
    uint8_t audio_master_target;   /* the master volume's target V:4A720 in volume units 0..63 (note 10 6.1): the library's level moves a quarter unit per blank toward it while the ramp flag is set. Information: the
                                    * volume writes above are already capped by the library's level, which is entirely inside the core */
    uint8_t audio_master_set;      /* 0xFF none, else the level in volume units the game set at once during this step (the menu build 31, the mode dispatch 63) */
    uint8_t audio_master_ramp;     /* the ramp flag V:4A88F (0 the level stays; 1 and 2 (the start-up) let it move toward the target) */
    uint16_t effect_count;  game_effect_t effects[GAME_MAX_EFFECTS];
    uint8_t gun_first;        /* milestone 5, settled in milestone 7 (note 07 7.3, 12.5; note 04 stage 9): the gun routine, which draws the tracer pool, runs BEFORE the entity pass of this frame (1: the draw scan
                               * 0x2AD18 answered 1 on the highest active slot, or the map view) or AFTER it (0: the scan answered 0, in particular an empty pool, and every plain tracer in the cockpit view; game.c
                               * gun_precedes_entity_pass, weapons.c weapons_scan): the frontend draws the pool over the whole of queue C in the second case */
    int8_t jolt_row;          /* milestone 5 (note 07 13.3): the jolt's row offset d in force (V:4A990: -1, 0 or +1): the 3D window's bottom row and the HUD rows are shifted by d scanlines while a jolt runs (cockpit view only) */
    game_trail_t trail;       /* milestone 5 (note 07 13.1): the trail ring */
    uint16_t splash_asset;    /* index into assets/pix/index.txt, shown in GAME_MODE_SPLASH */
    uint16_t text_id;         /* GAME_MODE_GAMEOVER: the text id V:4A802 (0x62 F-16, 0x63 F/A-18); the text itself is an asset (note 08) */
    uint8_t palette_regime;   /* note 08 section 4.2: 0 the menu palette (palette/menu.pal) for the text-only screens (credits, log-disk, menu and its echo, game over), 1 the flight palette
                               * (fade step 0 of the fade table) for every screen with the scene behind or beside the text (selections, mission-end overlays, pause, flight). A change of
                               * regime is a cut: the frontend switches the palette in the step in which the mode changes and clears its text layer. */
    uint8_t fade_target;      /* V:4A959 for the frame, 0, 10 or 15 (note 08 4.4): the level steps toward it, one per 2 blanks down and 3 up. In FLIGHT it is sky.fade_target (2 in a blackout)
                               * unless an ESC is being taken (0); in PAUSED 10; on a text-only screen 15; on the black passes before a selection 0 first, then 15 (the fade-in). Authoritative in every mode. */
    uint8_t jolt;             /* the jolt KIND 1 to 4 that started a jolt in this step (0 none; note 07 13.3): 1 a touchdown (0x16F7E) or a hit on the player (0x2A8A8), 2 never set by shipped code, 3 a landing after a stop (0x170E8) or a hard
                               * touchdown (0x1683E), 4 a lift-off (0x171D4) or an event of the take-off roll (0x1706E). The offsets (the rows d = -1, +1, 0, 0 for the kinds 1 and 3, +1, -1, 0, 0 for 2 and 4) are scene.jolt_row, one
                               * per pass for four passes. Was: 1 in the step in which an aircraft lifted off or touched down hard (note 08 8.3). */
    uint8_t flash_frames;     /* 1 in the step where the crash hold ends (pass A): the frontend presents one uniform orange frame (all 32 colour registers 0x0FB2,
                               * through the pen curve) for one 50 Hz blank (20 ms) BEFORE the frame of the scene; never more (note 05 17.17 (xii)) */
    uint8_t log_dirty;        /* note 08 10.8: a save point of the flight log was passed and the frontend has not yet saved it: game_log_export, store the bytes, then game_log_ack */
    uint8_t flight_blank;     /* GAME_MODE_FLIGHT and PAUSED: the world is not redrawn (V:4A84D = 0: an ESC being taken, the pause, note 05 17.17 rule 9, note 08 5.11, 5.14): the frontend keeps the last
                               * picture it presented (pens, not colours) and shows it through the palette of fade_target, so it fades out (ESC: target 0) or dims (pause: level 10) under the text */
} game_scene_t;

#endif
