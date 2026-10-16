/* src/core/assets.h - asset index and version helpers. OS-free: loader callback only. */
#ifndef INTERCEPTOR_ASSETS_H
#define INTERCEPTOR_ASSETS_H
#include <stddef.h>
#include <stdint.h>
#include "game.h"

#define CORE_ASSETS_VERSION 27    /* extractor version this core understands */
#define CORE_ASSET_NAME_LEN 32

/* Reads a text file of one name per line through the loader. Returns the number of
 * names stored (at most max, each NUL-terminated, empty lines skipped), or -1 if the
 * file cannot be read or a name is too long. */
int core_assets_load_index(const game_loader_t *loader, const char *path,
                           char names[][CORE_ASSET_NAME_LEN], int max);

/* Reads the VERSION file. Returns the number, or -1 if missing or malformed. */
int core_assets_read_version(const game_loader_t *loader);

/* Reads a file that must be exactly nbytes long into out (a caller buffer). Returns 0, or -1 when the file
 * is missing or has another size. */
int core_assets_read_exact(const game_loader_t *loader, const char *path, uint8_t *out, size_t nbytes);

/* Reads a file of nwords little-endian int16 words (size checked) into out. Returns 0 or -1. */
int core_assets_read_words(const game_loader_t *loader, const char *path, int16_t *out, size_t nwords);

/* Bounded string append: appends src to the NUL-terminated dst of capacity cap,
 * truncating if needed. */
void core_assets_append(char *dst, size_t cap, const char *src);
/* Appends the decimal form of v to dst (bounded). */
void core_assets_append_u32(char *dst, size_t cap, uint32_t v);

/* ---- Game-flow assets (milestone 4, extractor version 13; formats in tools/extract/README.md) ---- */

/* palette/menu.pal and palette/flash.pal: 32 entries of u8 r, g, b (the 4-bit Amiga channels times 17), note 02 section 11.
 * out is the caller's 96 bytes. Return 0, or -1 with a message naming the file in err (exact size 96 required). */
#define ASSETS_PALETTE_ENTRIES 32
int assets_load_menu_palette(const game_loader_t *loader, uint8_t out[3 * ASSETS_PALETTE_ENTRIES], char *err, size_t errlen);
int assets_load_flash_palette(const game_loader_t *loader, uint8_t out[3 * ASSETS_PALETTE_ENTRIES], char *err, size_t errlen);

/* tables/scenario.bin (note 05 17.14 and 17.17 rule 5, note 02 section 9). Fields the notes leave open are raw words. */
#define SCENARIO_MAX_START 8
#define SCENARIO_MAX_REGIONS 8
#define SCENARIO_MAX_REGION_ENTRIES 16
#define SCENARIO_MAX_ROWS 16
#define SCENARIO_MAX_LISTS 16
#define SCENARIO_MAX_SUBLISTS 48
#define SCENARIO_MAX_SUB_ENTRIES 160
#define SCENARIO_LIST_SUBS 4            /* sub-lists per list (chosen by one byte per mode, note 05 17.17 rule 5) */
#define SCENARIO_VARIANTS 3             /* variants per placement row */

/* The start block of V:4A900 (table 0x47ADE): runway: cell_x, cell_z, n, ox, oz, fx, fz, degrees (17.14); a carrier-type block
 * has cell_x = 0x8000 | record (as an int16, negative) and the other fields 0. The terminating -1 block is not stored. */
typedef struct { int16_t cell_x, cell_z, n, ox, oz, fx, fz, degrees; } scenario_start_t;
/* A placement entry (a region entry or a sub-list entry), five raw words. Region entries: record byte offset (20 * id), class,
 * slot and flags, position-block reference, flags (note 02 section 9). Sub-list entries (note 05 17.17 rule 5): template offset into
 * 0x25ECC, class, record index in the low 7 bits with 0x8000 a flag, then two further words (raw). */
typedef struct { uint16_t w[5]; } scenario_entry_t;
/* A spawn region (table 0x2DE28, reached through 32-bit pointers): the box is four words in cells (the order x0, x1, z0, z1 is a port
 * reading until the note states it); count_word is the raw word before the entries (not the entry count; meaning open);
 * entries [first, first + count) of region_entry[]. */
typedef struct { uint16_t box[4], count_word, first, count; } scenario_region_t;
/* A placement row of table 0x2DEDA (rows 0..8 = game modes 1..9, row 9 = modes 0x7E and 0x7F; mode 0x7D is the separate routine 0x2CFDA,
 * whose data is not a table in the image: open): per variant an index into list[] and the small count word (goes to V:4A962). */
typedef struct { uint16_t list[SCENARIO_VARIANTS], count[SCENARIO_VARIANTS]; } scenario_row_t;
/* A list: indices into sublist[] of its four sub-lists. A sub-list: entries [first, first + count) of sub_entry[]. */
typedef struct { uint16_t sub[SCENARIO_LIST_SUBS]; } scenario_list_t;
typedef struct { uint16_t first, count; } scenario_sublist_t;

typedef struct {
    int start_count;      scenario_start_t start[SCENARIO_MAX_START];
    int region_count;     scenario_region_t region[SCENARIO_MAX_REGIONS];
    int region_entry_count; scenario_entry_t region_entry[SCENARIO_MAX_REGION_ENTRIES];
    int row_count;        scenario_row_t row[SCENARIO_MAX_ROWS];
    int list_count;       scenario_list_t list[SCENARIO_MAX_LISTS];
    int sublist_count;    scenario_sublist_t sublist[SCENARIO_MAX_SUBLISTS];
    int sub_entry_count;  scenario_entry_t sub_entry[SCENARIO_MAX_SUB_ENTRIES];
} scenario_t;
int assets_load_scenario(const game_loader_t *loader, scenario_t *out, char *err, size_t errlen);

/* tables/text.bin and tables/text_ids.bin (note 08 sections 2.1 to 2.3): the text records of the front end, line by line, and the
 * id table. Positions are in the original's pixel addressing (the record's row is a scanline, col a byte column, x = 8 * col);
 * line k of a record is drawn k line steps below row (11 scanlines, 12 in the main menu list). Caps are above the real counts
 * (337 lines, 7,137 text characters, 110 ids). Game text is data: the core holds the bytes and never the text itself. */
#define TEXT_MAX_LINES 400
#define TEXT_MAX_CHARS 8192             /* text bytes plus one NUL per line, in text[] */
#define TEXT_MAX_IDS 128
#define TEXT_MAX_LINE_LEN 45
typedef struct {
    uint16_t first_id;      /* the lowest id naming the record */
    uint8_t line;           /* index within the record */
    uint8_t row, col;       /* the record's header, repeated on every line */
    uint8_t flags;          /* 0 instant line, 1 typed-out line, 3 typed-out line, spaces without delay */
    uint8_t colour;         /* high nibble the pen, low nibble the typed-line delay */
    uint8_t delay;          /* passes to wait after the line, in units of 4 (0 gives one) */
    uint16_t length;        /* characters (0 to 45) */
    uint16_t text;          /* offset into text_t.text[] of the characters; a NUL follows them */
} text_line_t;
typedef struct { uint16_t id, first_line, line_count; } text_id_t;   /* lines [first_line, first_line + line_count) of line[]; id i is id[i - 1] */
typedef struct {
    int line_count;  text_line_t line[TEXT_MAX_LINES];
    int id_count;    text_id_t id[TEXT_MAX_IDS];
    int text_len;    uint8_t text[TEXT_MAX_CHARS];
} text_t;   /* about 13.8 KB: hold it in the game struct or allocate it, never on the stack (WASM stacks are small) */
int assets_load_text(const game_loader_t *loader, text_t *out, char *err, size_t errlen);

/* fonts/menu5x7.bin (note 08 section 3.2): 64 glyphs x 7 bytes in character order (0x20 + i), rows top to bottom, bits 7 to 3.
 * Loaded by the frontend: assets_load_menu_font in src/frontend/sdl/assets.h. */
/* tables/demo.bin (note 08 7.1): the demo replay stream, key byte and two event words per recorded step. The file carries all 3676
 * frames; the port plays frames 0 to 2045 only (up to the first run of three 0xFF key bytes, DEMO_PLAYED_FRAMES). demo_t is about
 * 22 KB: hold it in the game struct or allocate it, never on the stack. */
#define DEMO_MAX_FRAMES 4096
#define DEMO_PLAYED_FRAMES 2046
typedef struct { uint16_t word1, word2; } demo_events_t;
typedef struct {
    int frame_count;
    uint8_t key[DEMO_MAX_FRAMES];
    demo_events_t ev[DEMO_MAX_FRAMES];
} demo_t;
int assets_load_demo(const game_loader_t *loader, demo_t *out, char *err, size_t errlen);

/* tables/training.bin (note 08 8.2, 8.13): table A rows (prompt and name warning-table entries), table B rows (camera long, script long with
 * bit 31 set: the low byte is the instructor's manoeuvre state) and the pitch and roll rate tables of the instructor's states. */
#define TRAINING_ROWS 8
#define TRAINING_RATES 11
typedef struct { int16_t prompt_id, name_id; int32_t unused; } training_a_t;
typedef struct { int32_t camera; uint32_t script; } training_b_t;
typedef struct {
    training_a_t a[TRAINING_ROWS];
    training_b_t b[TRAINING_ROWS];
    int16_t pitch_rate[TRAINING_RATES], roll_rate[TRAINING_RATES];
} training_t;
int assets_load_training(const game_loader_t *loader, training_t *out, char *err, size_t errlen);

/* ---- Combat assets (milestone 5, extractor version 15) ---- */

/* tables/templates.bin (note 02 section 1): u16 count (134), u16 record_size (20), then the records as five longs each, stored in the image's
 * big-endian order and returned here as host u32 (field meanings: note 07). */
#define TEMPLATE_COUNT 134
#define TEMPLATE_LONGS 5
typedef struct { int count; uint32_t rec[TEMPLATE_COUNT][TEMPLATE_LONGS]; } templates_t;
int assets_load_templates(const game_loader_t *loader, templates_t *out, char *err, size_t errlen);

/* tables/ai.bin (note 07 sections 3.6, 4.3, 4.5, 6.1, 6.3; extractor version 15): little endian, thirteen sub-tables each prefixed u16 count, u16 record_size,
 * all signed 16-bit words, in this order: five rate tables (11 words: 0x31316 non-aircraft, 0x3132C pitch, 0x31342 roll, 0x31358, 0x3136E),
 * the evasion table (19 groups of 4 x (counter, state)), the offset table 0x2789E (14 x 3 words: 0 seat, 1 parachute/pod, 2-7 hard points of class-0x10 owners,
 * 8-13 of the others), the class-nibble-3 ring offsets (10 x 3), the start blocks (5 x 8), the airfield camera points (3 x 6), the fine
 * offsets (16 x 2), the word before the rate tables 0x31314 (1 x 1), and the waypoint runs of state 8 (60 records of a count and 33 blocks of five words). training.bin keeps its own
 * copy of the pitch and roll tables. */
#define AI_RATES 5
#define AI_RATE_ENTRIES 11
#define AI_EVASION_GROUPS 19
#define AI_OFFSETS 14
#define AI_RING_DIRS 10
#define AI_STARTS 5
#define AI_CAM_POINTS 3
#define AI_FINE_OFFSETS 16
#define AI_RUNS 60
#define AI_RUN_MAX 33
#define AI_HULL_LISTS 3         /* the part ids 0, 1, 2 (note 07 8.2a) */
#define AI_HULL_TRIPLES 7       /* six face triples and the terminator triple */
typedef struct {
    int16_t rate[AI_RATES][AI_RATE_ENTRIES];
    int16_t evasion[AI_EVASION_GROUPS][4][2];   /* [group][progress 0..3][counter, state] */
    int16_t offs[AI_OFFSETS][3];                /* 0x2789E: seat 0, parachute/pod 1, hard points 2-7 (class 0x10 owners), 8-13 (others) */
    int16_t ring3[AI_RING_DIRS][3];             /* x, y, z model units, direction 0..9 */
    int16_t start[AI_STARTS][8];                /* 0x47ADE: 16-byte entries of eight words */
    int16_t cam_point[AI_CAM_POINTS][6];        /* 0x47B30: cell x, cell z, k, h, offset x, offset z */
    int16_t fine[AI_FINE_OFFSETS][2];           /* 0x206F2: x, z */
    int16_t pre_word;                           /* 0x31314: the word before the first rate table (20085) */
    int16_t run_len[AI_RUNS];                   /* 0x2DCE8: the blocks of each waypoint run (4.3) */
    int16_t run[AI_RUNS][AI_RUN_MAX][5];        /* cell x, cell z, fine x, fine z, height of each block of the run */
    /* 0x3DACC: the hull lists of the aircraft hit test. Per face (a, b, c) = byte offsets into the record's vertex array (slot k = 6k, slots 0 to 16);
     * bit 14 of a set = form B (a & 0x3FFF is the plane point). Triple 6 is the terminator: word 0 has bit 15 set (negative), the others are 0. */
    int16_t hull[AI_HULL_LISTS][AI_HULL_TRIPLES][3];
} ai_t;
int assets_load_ai(const game_loader_t *loader, ai_t *out, char *err, size_t errlen);

/* ---- Combat text and effect tables (milestone 5, extractor version 16) ---- */

/* tables/burst.bin (note 07 11.1 and 11.4): the magic "BRST", u16 version 2 (little endian), 72 i16 (the eight burst triangles, three vertices of x y z), 36 + 36 + 36 i8 (the fragment
 * offset triples of 0x31652, 0x31679 and 0x316A0, twelve each), 27 i8 (the nine disc offsets of 0x316C7), 5 x 12 u8 (the pen tables 0x316E5, 0x316F1, 0x316FD, 0x31709, 0x31715) and 32 u16 (the
 * shape cycle 0x31722); exactly 409 bytes. */
#define BURST_SHAPES 8
#define BURST_FRAGMENTS 12
#define BURST_DISC_COUNT 9
#define BURST_PEN_TABLES 5
#define BURST_CYCLE_WORDS 32
#define BURST_FILE_BYTES 409
typedef struct {
    int16_t shape[BURST_SHAPES][3][3];
    int8_t off[3][BURST_FRAGMENTS][3];    /* 0: 0x31652, 1: 0x31679 (twice the base offsets), 2: 0x316A0 (three times) */
    int8_t disc[BURST_DISC_COUNT][3];     /* 0x316C7 */
    uint8_t pen[BURST_PEN_TABLES][BURST_FRAGMENTS];   /* 0: kind 1, 1: kind 2, 2: the cruise missile's discs, 3 and 4: the two tables of the cruise missile's triangle layers (the assignment of 3 and 4 to the layers b and c is NOT observable: both tables hold 9 in all twelve bytes in the shipped image, so the frontend's choice of table 3 for layer b and 4 for layer c cannot be seen; NOTE-GAP if a table ever differs); fragment j draws in pen[t][j] */
    uint16_t cycle[BURST_CYCLE_WORDS];    /* 0x31722: fragment j at age a draws the shape at the byte offset cycle[j + a] of the pointer table (4 k: shape k) */
} burst_tables_t;
int assets_load_burst(const game_loader_t *loader, burst_tables_t *out, char *err, size_t errlen);

/* tables/target_text.bin (note 06 "Target symbols", note 07 12.8): u16 label_count (3), label_chars (4), name_count (8), name_chars (6), then the label characters and the name
 * characters, no terminators. Labels: 0 altitude, 1 heading, 2 speed. Names, in the image's order: 0 and 1 the hostile fighter types (classes 0x12, 0x13), 2 Air Force One (0x14),
 * 3 the F-16 (0x10), 4 the cruise missile (0x15), 5 the text of an undetected target, 6 and 7 the airliners (0x16, 0x17). Game text is data: the core holds the bytes only. */
#define TARGET_LABELS 3
#define TARGET_LABEL_CHARS 4
#define TARGET_NAMES 8
#define TARGET_NAME_CHARS 6
#define TARGET_TEXT_FILE_BYTES (8 + TARGET_LABELS * TARGET_LABEL_CHARS + TARGET_NAMES * TARGET_NAME_CHARS)
typedef struct {
    char label[TARGET_LABELS][TARGET_LABEL_CHARS];
    char name[TARGET_NAMES][TARGET_NAME_CHARS];
} target_text_t;
int assets_load_target_text(const game_loader_t *loader, target_text_t *out, char *err, size_t errlen);

/* tables/prompts.bin (note 06 "Target symbols", note 07 12.7; extractor version 19): u16 count (2), u16 chars (6), then the characters of the two fire prompts, no terminators: 0 the missile form
 * (0x36923), 1 the gun form (0x36929, which starts with a blank). The prompt routine 0x36930 draws six characters, a blank is skipped. Game text is data: the core holds the bytes only. */
#define PROMPT_COUNT 2
#define PROMPT_CHARS 6
#define PROMPTS_FILE_BYTES (4 + PROMPT_COUNT * PROMPT_CHARS)
typedef struct { char text[PROMPT_COUNT][PROMPT_CHARS]; } prompts_t;
int assets_load_prompts(const game_loader_t *loader, prompts_t *out, char *err, size_t errlen);

/* tables/star.bin (note 07 7.7 and 11; extractor version 22): u16 segment count (3), then 18 i16: three segments of two points of (x, y, z) words, the offsets of the flare's star from the slot's centre
 * in position units divided by 256 (the table at 0x31542). Return 0 or -1. */
#define STAR_SEGMENTS 3
#define STAR_FILE_BYTES (2 + STAR_SEGMENTS * 6 * 2)
typedef struct { int16_t pt[STAR_SEGMENTS][2][3]; } star_t;
int assets_load_star(const game_loader_t *loader, star_t *out, char *err, size_t errlen);

/* tables/months.bin (note 08 10.9): 36 bytes, the four month names of the date line, nine characters each, as they read on the line. Return 0 or -1. */
#define MONTHS_BYTES 36
int assets_load_months(const game_loader_t *loader, char out[MONTHS_BYTES], char *err, size_t errlen);

/* tables/briefing_chain.bin (note 08 2.1; extractor version 23): 30 bytes, the text-record ids of the briefing chain, five for each of the mission modes 3 to 8 (mode-major). Return 0 or -1. */
#define CHAIN_MODES 6
#define CHAIN_IDS 5
#define CHAIN_FILE_BYTES (CHAIN_MODES * CHAIN_IDS)
int assets_load_briefing_chain(const game_loader_t *loader, uint8_t out[CHAIN_FILE_BYTES], char *err, size_t errlen);

/* tables/head_step.bin (note 06; extractor version 23): 14 signed bytes, the HUD step of each head view code (the table at 0x1E800). Return 0 or -1. */
#define HEAD_STEP_FILE_BYTES 14
int assets_load_head_step(const game_loader_t *loader, int8_t out[HEAD_STEP_FILE_BYTES], char *err, size_t errlen);

/* tables/sounds.bin (note 10 2.2 and 2.3; extractor version 24, unchanged in 25): the 37 sound objects, 20 bytes each, big-endian, object n at 20 n. The extractor applies the sound loaders' rules to the
 * lengths of the files of text/ (the objects are built at run time in the original, not stored in the image). Fields, as in tools/extract/extract/sound_table.py:
 *   file    index into sounds/index.txt of the sample file; SOUND_NONE for a synthesised or absent object
 *   kind    SOUND_KIND_SAMPLE (a range of a file), SOUND_KIND_TONE (the 32-byte square wave of 3.2), SOUND_KIND_NOISE (the noise buffer of 3.3, also the gun, which has period 150),
 *           SOUND_NONE (0xFF): no such object (object 7, or its file is not on the disk): it plays nothing and the game goes on (the wrappers of 2.3)
 *   voice   0 to 3;  volume  the default volume 0 to 63 (the start volume is the wrapper's)
 *   offset, length   the byte range played, the whole block, replayed from its first byte (4.2: no separate repeat range, so the plan's rep_offset and rep_length are not carried)
 *   period  the default period in colour clocks;  count  blocks played before the voice moves on (-1: for ever);  reload  the count restored then (4.2)
 *   next    the object the voice moves on to, SOUND_NONE: none;  script  SOUND_SCRIPT_*: the script the object runs (3.1 to 3.5)
 * A present synthesised object has file SOUND_NONE and offset 0. The loader refuses a file of another size and a record that names a kind, a voice, a next object or a script that
 * does not exist; it does not check the ranges against the sample files (assets_load_sample does, when it reads one). Return 0 or -1 (out zeroed, every kind SOUND_NONE). */
#define SOUND_OBJECTS 37
#define SOUND_RECORD_BYTES 20
#define SOUND_TABLE_BYTES (SOUND_OBJECTS * SOUND_RECORD_BYTES)
#define SOUND_NONE 0xFF
enum { SOUND_KIND_SAMPLE = 0, SOUND_KIND_TONE = 1, SOUND_KIND_NOISE = 2 };
enum { SOUND_SCRIPT_NONE = 0, SOUND_SCRIPT_TONE = 1, SOUND_SCRIPT_NOISE = 2, SOUND_SCRIPT_GUN = 3, SOUND_SCRIPT_LAUNCH = 4 };
typedef struct {
    struct {
        uint8_t file, kind, voice, volume;
        uint32_t offset, length;
        uint16_t period;
        int8_t count;
        uint8_t reload, next, script;
    } obj[SOUND_OBJECTS];
} sound_table_t;
int assets_load_sounds(const game_loader_t *loader, sound_table_t *out, char *err, size_t errlen);

#endif
