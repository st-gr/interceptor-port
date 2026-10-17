/* src/core/lists.h - the original's display-list builder, written from docs/re/04a-display-lists.md ("note 04a") with
 * notes 02, 03, 05 for the table layouts, the draw stage and the entity records.
 *
 * What it reproduces (the section numbers are note 04a's):
 *   1. the request byte V:4A910 and the writers that the port has (octant change, 200 ft crossing, 10,240 ft band, pitch gate,
 *      cell change from the caller, camera and view switches from the caller, mission start), with the detector's order in the
 *      frame and its one-frame lag;
 *   2. the scan inputs (player's C cell, sub-cell, quadrant, view octant, camera offsets in u);
 *   3. the two-pass tile scan of tier A, B and C with the tile patterns, the coordinate wrap, the sub-cell offsets, the LOD
 *      nibble, the generation limit by altitude band and the walked window;
 *   4. the 24-byte queue entry (id/flags word, X, Y, Z, key long, walk state);
 *   5. the per-frame run sort of tier C and the landmark reorder;
 *   6. entity insertion (tier C only), the pending bits, bit 2 and the direct list;
 *   7. the landmark collision slots (tail of tier C pass 2);
 *   8. the per-frame walk (height limit of the plain models, distance refresh of terrain and of entities from the record's position,
 *      invisible-entry countdown, draw counter) and 8b, the drawer's result stored at +0x14, computed here from the rules of the note
 *      (the frontend never reports it): the exits of the common drawer, the level choice from the zoom-scaled distance word, the tier
 *      substitution, the flat-block pre-test and cull, and the primitive layer through the model interface below.
 * Core rules: C99, fixed-width integers, no stdio, no floating point, no wall time. */
#ifndef INTERCEPTOR_LISTS_H
#define INTERCEPTOR_LISTS_H
#include <stddef.h>
#include <stdint.h>
#include "game.h"

#define LISTS_SLOTS 70            /* queue slots; the 70th is overwritten by the terminator, so 69 entries are kept */
#define LISTS_KEPT 69
#define LISTS_ENTITIES 16
#define LISTS_VSLOTS 58           /* the vertex slots of a record kept (+0xA4 on; the aircraft hull lists read 17, note 07 8.2a, the ship and big-hull tests up to 58, note 05 17.11) */
#define LISTS_LANDMARK_SLOTS 11
#define LISTS_RECORDS 134         /* object records at 0x25ECC; the descriptor of item id i is record i + 16 */
#define LISTS_MAX_RUNS 40
#define LISTS_NG_CLASS_BYTES 1024
#define LISTS_NG_MAX_POINTS 96    /* table A; B has 80 entries and C 64 */
#define LISTS_WALK_MAX (3 * LISTS_KEPT + 3 + LISTS_ENTITIES)

enum { LISTS_TIER_A = 0, LISTS_TIER_B = 1, LISTS_TIER_C = 2, LISTS_TIER_DIRECT = 3 };

/* Descriptor classes (the routine of the descriptor; lists_desc.bin byte 0). */
enum {
    LISTS_CLASS_NONE = 0, LISTS_CLASS_PLAIN = 1 /* 0x21C40 */, LISTS_CLASS_LOW = 2 /* 0x21C34 */, LISTS_CLASS_DRAW = 3 /* 0x21D0C */,
    LISTS_CLASS_TYPEB1 = 4 /* 0x22CFC */, LISTS_CLASS_TYPEB2 = 5 /* 0x22D0A */, LISTS_CLASS_TMPL_21C44 = 6, LISTS_CLASS_TMPL_26944 = 7,
    LISTS_CLASS_TMPL_2699E = 8, LISTS_CLASS_TMPL_26A3E = 9, LISTS_CLASS_ENTITY = 32
};

/* The tables the builder reads (extractor outputs, assets/tables/lists_*.bin). */
typedef struct { int16_t box[5]; uint8_t nplanes; int16_t ref[8][3], nrm[8][3]; } lists_node_t;   /* a footprint node of a landmark slot; nplanes 0 still holds the one plane that follows the count (extractor version 26) */
typedef struct { uint8_t cls, flags; uint16_t foot, ref; } lists_desc_t;   /* flags: bit 0 model base positive, bit 1 reference-qualified, bit 2 footprint */
#define LISTS_VIS_WORDS 24
typedef struct {
    uint8_t quad[16], octperm[32], group[16], subperm[64], tileperm[96];
    int8_t tiles[21][2];
    uint8_t steep[610], high[3042], low[502];
    int16_t limit[8];
    int16_t vis[LISTS_VIS_WORDS];   /* lists_vis.bin: the pre-test tables 0x23FF4 (camera above -0x80) and 0x2400C, 12 words each */
    lists_desc_t desc[LISTS_RECORDS];
    /* the near-ground pass, stage [90] (note 04): the 32 by 32 class table 0x2C440, the three point tables 0x2C840 / 0x2CA84 / 0x2CC68 (A, B, C: count, class limit, then (x, z, k)
     * word triples) and the six triangle shapes 0x2CDEC..0x2CE33 (k = -12, -16, -20, each unscaled (tables B and C) then scaled (table A); three (dx, dz) word pairs each) */
    uint8_t ng_class[LISTS_NG_CLASS_BYTES];
    struct { int16_t count, limit; int16_t pt[LISTS_NG_MAX_POINTS][3]; } ng[3];
    int16_t ng_shape[6][3][2];
    uint8_t *land;           /* lists_land.bin, owned */
    size_t land_len;
} lists_tables_t;

/* Reads the tables through the loader. Returns 0, or -1 with a message naming the file. */
int  lists_tables_load(lists_tables_t *t, const game_loader_t *loader, char *err, size_t errlen);
/* The footprint of the descriptor `desc` (a record of the object table, 0 to 133) as the builder's tail copies it into a landmark slot (04a section 7), offset by ox and oz (the entry's X and Z shifted
 * left by the LOD, masked to 0x3FFF): the outer box, the node count and the nodes. Returns 0, or -1 when the descriptor has no footprint. */
int  lists_footprint(const lists_tables_t *t, unsigned desc, int ox, int oz, int16_t box[5], uint8_t *nnodes, lists_node_t node[4]);
void lists_tables_free(lists_tables_t *t);

/* One item of a terrain cell (the world's world_item_t has the same layout). */
typedef struct { uint8_t layer, id; uint16_t dx, dz; } lists_item_t;
/* Item list of cell (cx, cz) of table 0 A, 1 B, 2 C; returns the count (0 when the cell is absent). */
typedef int (*lists_cell_fn)(void *user, unsigned table, unsigned cx, unsigned cz, const lists_item_t **items);

/* An entity record or secondary object as the builder reads it (record words +0x01.., note 05 section "State block"). */
typedef struct {
    uint8_t active;                 /* +0x01 bit 6 */
    uint8_t b4;                     /* +0x04 */
    int16_t w4e;                    /* +0x4E (the flight state's deck word): with both bits 6 and 7 of b4 set the op 4 gate compares the height adjusted by D = -w4e * 256 (note 03, op 4 table step 2; the carrier-deck record has b4 = 0xC8 and +0x4E = 112, so the adjusted camera height is -3,080 at the deck); game.c sets it from f->deck */
    uint8_t b7c;                    /* +0x7C (the gear byte): bits 4-6 the gear progress 0 to 6; op 52 skips 14 bytes per unit of it (note 03, "Ops 52 and 53"); 0 when the caller does not set it */
    uint8_t cls;                    /* +0x62 class byte (0x30 and 0x14 keep the op 4 gate open in a cockpit view; 0 when the caller does not set it) */
    uint8_t cell_x, cell_z;         /* +0x06, +0x08 (0..127) */
    uint8_t sub;                    /* +0x0A */
    int16_t fine_x, fine_z;         /* +0x0C, +0x0E */
    int32_t alt8;                   /* +0x10 */
    int32_t pos[3];                 /* +0x14, +0x18, +0x1C */
    uint16_t model;                 /* model id the frontend draws (the port's mapping of the descriptor copy) */
    uint8_t alt_same;               /* the descriptor's cockpit-alternate list is its normal list (templates 2, 8, 9, 11, 12, 13; note 04a section 8a test 5) */
    int32_t pitch, roll, yaw;       /* attitude for the frontend (scene convention) */
    uint8_t damage;                 /* milestone 5: the GAME_DMG_* bits of the record's W0, B20, W2 and B4 (scene.h), display state for the frontend */
    int16_t phase;                  /* milestone 5: a secondary object's life counter +0x04, a record's counter +0x4C */
    int16_t mat_b[9];               /* the attitude matrix +0x92 (Q14) the draw rotates the record's model vertices by (the gun's hull slots, note 07 8.2a); zero when the caller does not set it */
} lists_entity_t;

/* One queue entry (note 04a section 4). */
typedef struct {
    uint8_t id;          /* word 0 high byte: item id (the entity's record index for entities) */
    uint8_t flags;       /* word 0 low byte: bit 7 default offset, bit 6 secondary object, bit 4 entity, bits 0-3 the LOD shift */
    int16_t x, y, z;     /* +0x06, +0x08, +0x0A */
    uint32_t key;        /* +0x0C */
    uint16_t dist;       /* +0x10 distance word (20 ft units divided by 2^LOD) */
    int8_t countdown;    /* +0x12 */
    uint8_t counter;     /* +0x13 draw counter */
    int16_t result;      /* +0x14 result of the last draw: positive, -1, or 0 never walked */
} lists_entry_t;

/* The scan inputs (the V: words of note 04a section 2). */
typedef struct {
    uint8_t cell_x, cell_z;      /* V:4AA04, V:4AA06 */
    uint8_t acell_x, acell_z;    /* V:4AA00, V:4AA02 */
    uint8_t sub;                 /* V:4A909 */
    uint8_t quad;                /* V:4A908 */
    uint8_t octant;              /* V:4A90C */
    uint8_t gate;                /* V:4A83E */
    int32_t cam_x, cam_z;        /* V:4AB1A, V:4AB22, longs: minus the camera's x and z within the viewer's C cell (the position masked to 22 bits, then the rotated eye offset added), position units */
    int32_t cam_y;               /* V:4AB1E, long: minus the camera height, position units */
    int16_t off_x, off_z;        /* V:4AB2A, V:4AB2E, words in u */
    int32_t off_y;               /* V:4AB30, long in u */
} lists_scan_t;

/* The model side of the drawer (note 04a section 8b). The world module implements it; without one every walked terrain entry reports "visible". */
struct lists;
typedef struct {
    uint8_t found;            /* a detail level fits (rule 4 passed) */
    uint8_t present;          /* the leaf is not 0xFFFF */
    uint8_t kind;             /* model kind: 0 3D, 1 flat, 2 type B */
    uint8_t block_flags;      /* block byte +7 of the chosen level */
    uint16_t leaf;            /* raw leaf word: bit 14 never needs the clip test, bit 13 the tier gate */
    uint16_t part_id;         /* block word 0 */
    uint16_t level;           /* index of the level */
} lists_level_t;
#define LISTS_RS_MAX 32
typedef struct {
    int polys;                /* polygons that fx_clip kept with at least three vertices (the OR of the handlers' positive returns) */
    uint8_t fv_ok;            /* the first-vertex test of the chosen block passed (depth > 0, |x|, |y| <= 1.5 depth); 1 when not applicable */
    uint8_t ends_ffff;        /* the walked path ended with the word 0xFFFF (terrain drawer: result 0 whatever was drawn) */
    uint8_t aborted;          /* a type B handler returned negative: the type B drawer returns 0 (rule 9; read from the code only) */
    uint8_t decider;          /* the operation number of the handler that decided the sign: the last one that counted, else the one that abandoned the program (0: none ran) */
    uint16_t toggle_in;       /* V:4A7A2 at the entry of the object's drawer (note 03, "Fill state": a type B drawer inherits it) */
    uint16_t alt_in;          /* V:4A7A4 at the entry (0xFFFF after the near-ground pass until a common drawer entry clears it; a type B drawer inherits it) */
    int16_t pbuf_in[3];       /* the depth words at the vertex positions 4 to 6 of the polygon buffer V:5103C at the entry (note 03, "Op 9 reads past its quad": residue of earlier polygons) */
    int rs_n;                 /* the record-copy operations the program ran, in order (ops 42, 43, 44, 59 with their base slot, 56, 63, 64, 65 with base 0): the display lists run their fragments on the record's slots */
    uint8_t rs_op[LISTS_RS_MAX]; int16_t rs_base[LISTS_RS_MAX];
} lists_prims_t;
typedef struct {
    void *user;
    /* the detail level for object id `model`: a node fits when (threshold & 0x3FFF) >> lod is greater than `scaled`, a signed word (type B lists: the
     * unmasked threshold >> lod is at least `scaled`); returns 0, or -1 when the model is unknown */
    int (*pick)(void *user, unsigned model, unsigned lod, int16_t scaled, lists_level_t *out);
    /* x and z words of block vertex `slot` of a level (model units); 0, or -1 */
    int (*vertex_xz)(void *user, unsigned model, unsigned level, unsigned slot, int16_t *x, int16_t *z);
    /* the primitive layer of walk record `walk` of the last walk, on the level chosen */
    int (*prims)(void *user, const struct lists *l, unsigned walk, const lists_level_t *lv, lists_prims_t *out);
    /* the call of the near-ground pass in the walk (0x11212, after queues A and B, before the entities; note 04, "Stage [90]"): it stores the fill block (4, 0, 0, 0xFFFF) and the colour 3 before
     * any height test, so it is called on every walk, whatever the camera height. May be NULL. */
    void (*stage90)(void *user);
    /* all three words of vertex `slot` of a level (model units): 0 for a vertex the block itself holds (block or strip), 1 for a synthesised one (the record copy of the operations makes those), -1 when
     * the level has no such vertex or it is undefined. May be NULL (the record's slots are then not stored). */
    int (*vertex)(void *user, unsigned model, unsigned level, unsigned slot, int32_t v[3]);
    /* the number of slots the vertex loop stores for a level (07 8.10 rule 5): b8 + 1 when b8 = b9 (the trailing data triple), b8 when b8 > b9, 0 when b8 < b9. May be NULL (the loop then runs to the first slot
     * `vertex` does not give). */
    unsigned (*vslot_count)(void *user, unsigned model, unsigned level);
    /* the level of the phase list `list` (1 to 4, lists_walk_t.cone_list) of a cone model; 0, or -1 when the model has no phase lists. May be NULL (the record then draws its own list). */
    int (*pick_cone)(void *user, unsigned model, unsigned list, lists_level_t *out);
    int (*pick_cockpit)(void *user, unsigned model, lists_level_t *out);   /* the cockpit block of the alternate list (test 9 of 04a 8a): 0 and found/present when the model has one */
} lists_models_t;

/* The variant of the player's own entry (note 04a section 8a table and "What the player's own entry shows"). */
enum { LISTS_VARIANT_NORMAL = 0, LISTS_VARIANT_NOTHING = 1 /* cockpit block: no program */, LISTS_VARIANT_NEAR_BODY = 2 /* alternate list: the near body, two thin triangles in rows 166 to 179 (note 03), below the cockpit window */ };

/* The events of the near-ground pass (stage [90], note 04), in table order. LISTS_SPECK_1X1 and LISTS_SPECK_2X1 are the point sprites (xy[0], xy[1] the pixel); LISTS_SPECK_TRI is a triangle
 * whose bounding box takes one of the filler's small shapes (a line, a block of dots, a dot: they clear plane 2 of their pixels); LISTS_SPECK_FILL is a triangle of the general fill, which
 * takes the alternate blit 0x34EEC (plane 2 := plane 3 under the filled area). Triangles carry the three screen vertices as pixels (319 - sx, 179 - sy). */
enum { LISTS_SPECK_1X1 = 0, LISTS_SPECK_2X1 = 1, LISTS_SPECK_TRI = 2, LISTS_SPECK_FILL = 3 };
typedef struct { uint8_t kind; int16_t xy[6]; } lists_speck_t;
#define LISTS_MAX_SPECKS LISTS_NG_MAX_POINTS

/* One entry of the walk: where it lives and what the walker decided. */
typedef struct {
    uint8_t tier;                /* LISTS_TIER_* */
    uint8_t kind;                /* 0 terrain, 1 entity, 2 secondary object, 3 near-ground marker */
    uint8_t index;               /* queue index (or the record index for the direct list) */
    uint8_t draw;                /* 1 when the walker called the draw routine, 0 when it skipped the entry */
    uint8_t counter;             /* the draw counter as published (V:4A975) */
    uint8_t lod;                 /* V:4AB70 */
    uint8_t drew;                /* 1 when the drawer ran the primitive layer (the frontend draws these) */
    uint8_t variant;             /* LISTS_VARIANT_* (entities) */
    uint8_t op4_closed;          /* entities: the op 4 handler returns 1 without drawing (note 04a P7) */
    uint8_t op4_neg;             /* entities: the op 4 handler returns -1 (the camera is above 5,120 ft and the gate's first exits did not close it, note 03) */
    uint8_t cone_list;           /* entities of the models 2, 13 and 11 (the handlers 0x2699E and 0x26A3E, note 07 11.5): 0 the model's own detail list (phase 5 and up), 1 to 4 the phase list (3 and 4, 1 and 2, 0, below 0) */
    int16_t prev_result;         /* V:4AB72: the result of the previous walk */
    int16_t result;              /* the value stored at +0x14 by this walk (positive or -1); 0 when nothing was stored */
    uint16_t dist;               /* V:4ABF8: the distance word the drawer saw (+0x10 after the refresh) */
    int16_t polys;               /* handlers of the primitive layer that counted */
    uint8_t decider;             /* the operation of the handler that decided the sign (lists_prims_t) */
    uint16_t toggle_in;          /* the toggle word the drawer saw at its entry (lists_prims_t) */
    uint16_t alt_in;             /* V:4A7A4 at the entry (lists_prims_t) */
    int16_t pbuf_in[3];          /* the polygon buffer residue at the entry (lists_prims_t) */
} lists_walk_t;

typedef struct lists {
    const lists_tables_t *tab;
    lists_cell_fn cell;
    void *cell_user;
    lists_scan_t scan;
    uint8_t request;             /* V:4A910 */
    uint8_t octant_copy;         /* V:4A90D */
    int32_t h26;                 /* V:4AB26: the height of the last detector pass */
    int32_t band16;              /* V:4AB16 */
    uint8_t mode;                /* V:4A966 camera mode (0 in the port) */
    const lists_models_t *models;   /* the model side of the drawer; NULL: every walked entry reports visible */
    uint32_t rule_hits[10];      /* how often the drawer left through rule 2..9 of note 04a section 8b (index 0: other exits); a test counter */
    uint16_t zoom;               /* V:4AAFA (0x80 at the flight start) */
    uint8_t zoom_fixed;          /* V:4A895: the zoom scale is not applied when non-zero */
    uint8_t v4a924;              /* V:4A924: the pre-test runs only when zero */
    struct { int external, head, leave_external, viewed; } view;   /* the frame's view state for the drawer (section 8a) */
    uint8_t period;              /* V:4A974 */
    uint32_t n;                  /* V:4A992 */
    uint8_t built;               /* V:4A85D: set at the end of tier C pass 2 */
    lists_entry_t q[3][LISTS_SLOTS];
    uint16_t gen[3];             /* the counter 0x53A28 per tier: entries generated (never clamped) */
    uint8_t kept[3];             /* entries kept (at most 69) */
    uint16_t start[3];           /* first walked index (the walked-window offset V:4AA64/66/68 divided by 24) */
    int32_t corner[3][2];        /* the viewer's C cell corner (position units) that the tier's words are relative to */
    /* tier C */
    struct { uint8_t first, count; } run[LISTS_MAX_RUNS];
    uint8_t nruns;               /* V:4A914 */
    uint8_t cursor;              /* V:4A915 */
    uint16_t emitted_c;          /* V:4A916 */
    lists_entity_t ent[LISTS_ENTITIES], sec[LISTS_ENTITIES];   /* the records as of the last frame */
    uint8_t pending[2][LISTS_ENTITIES];
    uint8_t bit2[LISTS_ENTITIES];
    uint8_t direct[LISTS_ENTITIES]; uint8_t ndirect;           /* 0x53A2A: the direct list */
    uint8_t pitch_band;          /* V:4A923: 0 level, 1 and 2 dives, 3 the steep gate (note 04a section 8a) */
    uint8_t dist_words[3];   /* V:4A921, V:4A922, V:4A870 as the last counter-14 draw of a dying record wrote them (note 07 11.4, code 0x21F1C-0x21F62) */
    int tail_stale;          /* the result word of the last drawer call that reached the program walk: what a tail-only draw (a dying record, counter 12 or less) returns (07 11.4, inferred) */
    uint16_t v4abfa;         /* V:4ABFA: the viewed record's distance word of its last queue C walk (external view) or 0x7FFF (cockpit); read by the sound routine (note 10 5.3), kept across frames like the variable */
    uint8_t dist_words_new;  /* set by that draw, cleared by the consumer (game.c copies the words into the entities' sound request bytes) */
    uint8_t part_id[LISTS_ENTITIES];   /* +0x7B of the entity records: the part id of the last completed draw (section 8a): written when the drawn block has the completion flag (block flag bit 0) */
    int16_t vslot[LISTS_ENTITIES][LISTS_VSLOTS][3];   /* the vertex slots +0xA4.. of the entity records: slot k = (M . v_k) >> 8, M = the record's matrix >> 6, of the block the last draw used (the picker's level; vertices that block lacks keep their earlier contents); cleared at the flight start (0x1D228). The gun's hull test reads them (note 07 8.2a) */
    uint32_t reorders;           /* landmark reorders that moved entries (a counter for the tests) */
    uint8_t ref_index;           /* V:54DFA */
    uint8_t sort_out[22];        /* the sorted-index buffer 0x538C8 (fx_sort output, kept between sorts: positions of negative keys read it stale) */
    uint8_t win_start;           /* V:54DFC */
    /* the landmark slots (section 7) */
    struct {
        uint8_t used, valid;     /* used: the ordinal is taken; valid: has a footprint */
        uint8_t cell_x, cell_z;
        int16_t box[5];
        uint8_t nnodes;
        lists_node_t node[4];
    } slot[LISTS_LANDMARK_SLOTS];
    uint8_t nslots;              /* ordinals taken by the last tier C build (slots beyond it keep stale contents) */
    /* the walk */
    lists_walk_t walk[LISTS_WALK_MAX];
    uint16_t nwalk;
    /* the last frame's camera, for the detector's one-frame lag */
    uint8_t last_ext_cell[4];
    uint8_t have_last;
} lists_t;

void lists_init(lists_t *l, const lists_tables_t *tab, lists_cell_fn cell, void *user);
void lists_set_models(lists_t *l, const lists_models_t *m);   /* installs the model side of the drawer */

/* Mission start (0x1D1E4, 0x1D2C8): the request byte is stored as 0xFF and V:4A90D as 0xFF. */
void lists_mission_start(lists_t *l);

/* Writers of the request byte outside the builder (position update, camera and view switches, spawn): ORs mask in. */
void lists_request(lists_t *l, uint8_t mask);

/* One frame input: the viewed entity and the camera. cam_* is the camera of the frame (position units, 28800 per circle, pitch nose
 * up positive). viewer_valid: the cockpit branch of the detector (cells from the viewed record); otherwise the external branch
 * (cells from the camera position). */
typedef struct {
    int32_t cam_pos[3];          /* the viewed record's position in the cockpit view; the camera position in an external view */
    int32_t cam_eye[3];          /* the rotated cockpit eye offset in position units (note 03, "Camera position and the cockpit eye offset"); zero in the external views */
    int32_t cam_yaw, cam_pitch;
    int viewer_valid;
    uint8_t v_cell_x, v_cell_z, v_sub;
    int16_t rates[3];            /* +0x56, +0x58, +0x5A of the viewed record */
    int16_t speed_target;        /* +0x6C */
    int external;                /* V:4A83D: an external view (the ring, chase or tower camera) */
    int head;                    /* V:4A85F: the cockpit view code 0..13 */
    int leave_external;          /* V:4A8ED: the key left an external view for the cockpit; consumed by the entity pass that follows */
    int viewed;                  /* the index of the viewed record (V:4A996 / 0x200); 0 in the port */
    uint32_t n;                  /* V:4A992 */
    uint16_t zoom;               /* V:4AAFA; 0 means the flight-start value 0x80 */
    lists_entity_t ent[LISTS_ENTITIES];
    lists_entity_t sec[LISTS_ENTITIES];   /* the secondary objects (note 07 1.5): the 16 objects as of this frame */
} lists_frame_t;

/* The frame: detector, camera words, octant, pitch gate, builder, run sort, landmark reorder, walk. */
void lists_frame(lists_t *l, const lists_frame_t *in);
/* The same in its two halves: the build (detector, camera words, builder) and the walk; the sky fill and the sea mesh run between them in the original's order. */
void lists_frame_build(lists_t *l, const lists_frame_t *in);
void lists_frame_walk(lists_t *l);
void lists_frame_walk_map(lists_t *l);   /* the map view: queues A and B and the [90] pass only (07 7.3) */
/* The counter-14 explosion words of the entity drawer from the scaled distance word and the LOD shift: out = { V:4A921, V:4A922, V:4A870 }. */
void lists_explosion_words(int16_t scaled_dist, unsigned shift, uint8_t out[3]);
int lists_altitude_gate(const lists_t *l);   /* 0x111E0: V:4AB1E at or below -0x8000000 skips the walks, the gun and the entity pass */
void lists_walk_terrain(lists_t *l);   /* the walkers 0x1FA24, 0x1FA36, 0x2C0EC: resets the walk, queues A and B, the [90] pass, the near-ground marker */
void lists_walk_entities(lists_t *l);  /* the entity pass 0x1FBCC: the direct entities and queue C, appended */

/* The walk alone (the frame does it after the builder): fills nwalk/walk[], decides the draw result of every walked entry and stores it at +0x14. The view state
 * (l->view, l->zoom, l->pitch_band) must be set by the caller. */
void lists_walk(lists_t *l);

/* The builder alone (0x1F770): scan inputs and entity records must be set by the caller. request is OR-ed into the stored byte. */
void lists_builder(lists_t *l, uint8_t request);

/* Derives the scan inputs from a position and heading (the external branch of the detector, section 2). eye (may be NULL) is the rotated eye offset: the camera longs are minus the
 * position (x and z masked to 22 bits first) plus the offset (note 03). */
void lists_scan_from_camera(lists_scan_t *s, const int32_t cam_pos[3], const int32_t eye[3], int32_t cam_yaw);

/* The near-ground pass (stage [90], note 04) for the camera words of the lists (V:4AB2A, V:4AB2E, V:4AB30, the pitch band) and the camera matrix m (nine words row by row, the matrix the
 * objects use) with the last 3D row row_limit (V:4AA3C): the specks in table order. Returns their number (0 above the height limit of the band), at most `max`. */
int lists_nearground(const lists_t *l, const int16_t m[9], int row_limit, lists_speck_t *out, int max);

/* The distance word times the zoom (not applied when `fixed`), as the signed word of note 04a section 8b. */
int16_t lists_scaled_distance(uint16_t dist, uint16_t zoom, int fixed);

/* The rule 6 pre-test of the flat blocks (note 04a section 8b), also run as op 31: 1 when it rejects the entry; level is the drawn block, first_vertex the first of the two vertices used. */
int lists_pretest_rejects(const lists_t *l, unsigned model, unsigned level, unsigned lod, int16_t ex, int16_t ez, int16_t sd, unsigned first_vertex);

/* The common drawer for one record of l->ent as the direct list walks it (tests and the rig): walk entry 0 is replaced; returns the drawer's word, *completed as in the part id store (section 8a). */
int lists_draw_entity(lists_t *l, unsigned ri, unsigned lod, uint16_t dist, int *completed);

/* Descriptor class and record of a queue entry. */
const lists_desc_t *lists_desc_of(const lists_t *l, const lists_entry_t *e);

#endif
