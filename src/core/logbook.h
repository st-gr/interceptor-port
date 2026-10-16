/* src/core/logbook.h - the flight log: the player's persistent record (note 08 section 10) and the rules that touch it.
 *
 * The record is the original's 78 bytes, big-endian words, with the offsets of 10.2. The core never touches a file: the record crosses the boundary as
 * the port container of 10.3 (game_log_export / game_log_import in game.h): the magic "ILOG", the version byte (1), then the record. Everything here is a
 * pure function of the record; screen.c and game.c call it. Tags: C = port convention, NOTE-GAP = an open question of the note. */
#ifndef INTERCEPTOR_LOGBOOK_H
#define INTERCEPTOR_LOGBOOK_H
#include <stddef.h>
#include <stdint.h>

#define LOGBOOK_SIZE 78                 /* 10.2: the buffer V:1D024 */
#define LOGBOOK_HEADER 5                /* 10.3 rule 1: 4 magic bytes and the version */
#define LOGBOOK_FILE_SIZE (LOGBOOK_HEADER + LOGBOOK_SIZE)
#define LOGBOOK_VERSION 1
#define LOGBOOK_NAME_FIELD 24           /* the callsign field +0x1E .. +0x35 */
#define LOGBOOK_NAME_CELLS 41           /* 10.5 Display: the entry line holds 41 cells */
#define LOGBOOK_MAX_FLIGHT_SECONDS 18000   /* 10.7: a booking of 0 to 17,999 seconds is added, others dropped */

/* The offsets of 10.2. */
enum {
    LB_QUALIFIED = 0x00, LB_LEVEL = 0x02, LB_TOURS = 0x04, LB_LAST_MISSION = 0x06, LB_LAST_COUNT = 0x07,
    LB_SECONDS = 0x08,          /* long */
    LB_HANDOVERS = 0x10,        /* word: crash hand-overs with collisions on (write-only in the original) */
    LB_COMPLETIONS = 0x12,      /* 9 bytes, one per mode index 0 to 8 */
    LB_CALLSIGN = 0x1E,
    LB_STARTED = 0x36, LB_COMPLETED = 0x38,
    LB_CANNON_FIRED = 0x3A, LB_CANNON_HITS = 0x3C, LB_AIM120_FIRED = 0x3E, LB_AIM120_HITS = 0x40,
    LB_AIM9_FIRED = 0x42, LB_AIM9_HITS = 0x44, LB_CRASHES = 0x46, LB_MISSILE_HITS = 0x48
};

typedef struct {
    uint8_t rec[LOGBOOK_SIZE];  /* the record, byte for byte as the file holds it (big-endian words) */
    uint8_t dirty;              /* V:4A87D: a counter changed since the last save; UPDATE on the log screen writes only when it is set (10.6) */
    uint8_t save_req;           /* C: a save point of 10.8 was passed and the frontend has not yet taken the blob (scene.log_dirty) */
    uint16_t live_level;        /* V:4A95F: the pilot's progress counter. The original loads it from the record's word +2 at start-up BEFORE the log is read from the disk, so every run starts at 0
                                 * whatever the saved level (note 08 14.3); only the 0xFF completions (logbook_mission_complete) raise it; the record's +2 is write-only */
    int16_t bogey_deg;          /* V:4ABD6: the bearing of the vector notice 0x58 in degrees (0 to 359), -1 when the lookup 0x29534 found no aircraft (milestone 5, note 08 13.2) */
    const char *months;         /* the four month names of the date line (0x292CC: 4 x 9 characters), or NULL when the assets do not carry them (the screen points it at tables/months.bin before every patch; with NULL the day is patched and the month is left) */
} logbook_t;

unsigned logbook_get16(const logbook_t *lb, int off);
uint32_t logbook_get32(const logbook_t *lb, int off);
void     logbook_set16(logbook_t *lb, int off, unsigned v);

/* The zero record (10.2: a new player; tours 0 selects the first-tour welcome). Clears the flags and the live level. */
void logbook_zero(logbook_t *lb);
/* RESET on the log screen (10.6, 0x18556): the 78 bytes become zero and the dirty flag is set; the live level V:4A95F, the flags of a pending save and nothing else is touched. */
void logbook_reset(logbook_t *lb);
/* The inner record of 10.3 rule 2: len bytes are the leading bytes of the record, the rest stays zero; more than 78 bytes: the first 78. Sets the live level
 * from the word +2 (0x1D150). */
void logbook_load_record(logbook_t *lb, const uint8_t *rec, size_t len);

/* The port container (10.3 rule 1). logbook_file_unwrap checks the header and returns the record part: 0 and *rec, *rec_len; or a negative code: -1 shorter
 * than the header, -2 wrong magic, -3 wrong version. logbook_file_encode writes the container (83 bytes) and returns its size, or 0 when cap is smaller. */
#define LOGBOOK_ERR_SHORT (-1)
#define LOGBOOK_ERR_MAGIC (-2)
#define LOGBOOK_ERR_VERSION (-3)
int    logbook_file_unwrap(const uint8_t *file, size_t len, const uint8_t **rec, size_t *rec_len);
size_t logbook_file_encode(const logbook_t *lb, uint8_t *buf, size_t cap);

/* Adds delta to the word at off (16-bit wrap) and sets the dirty flag: the counters above +0x36 (10.2). */
void logbook_add(logbook_t *lb, int off, unsigned delta);
/* A save point of 10.8: the save routine clears the dirty flag and the port asks the frontend for the blob (save_req). */
void logbook_save_point(logbook_t *lb);

/* ---- what the log unlocks (9.6, 5.4) ---- */
/* Menu key 7: the next mission, byte +6 plus 1 (3 when outside 3 to 8), or 0 when the first word is zero (only the click sounds). */
int logbook_item7_mode(const logbook_t *lb);
/* Menu keys F1 to F6 (n = 1..6, missions 3 to 8): the mode, or 0 when the first word is zero or the mission's availability byte (record + 0x11 + mission) is 0;
 * F1 is available once a record exists. */
int logbook_fkey_mode(const logbook_t *lb, int n);
/* The unlock byte V:1D024 + 0x12 + mode that selects the sub-list of the placement (6.1), limited to 3 where it indexes (10.3 rule 3). */
unsigned logbook_sublist_selector(const logbook_t *lb, int mode);
/* The mission completion bookkeeping 0x134A0 (10.8): +2 takes the live level, +6 the mode, +7 the old completion count of the mode, +0x38 plus one, the live level
 * plus one (at most 3), the completion count of the mode plus one (at most 3), V:4A960 takes the new level. Sets the dirty flag. */
void logbook_mission_complete(logbook_t *lb, int mode);
/* The mission start counter +0x36 (0x1294C): modes 3 to 8 only. Milestone 5 (the missions F1 to F6 after the countercode is accepted) calls it; nothing in the port does yet: tested only. */
void logbook_mission_started(logbook_t *lb, int mode);

/* ---- the mission clock (10.7) ---- */
/* Books steps airborne steps (8 per second): seconds = steps / 8 is added to +0x08 when it is 0 to 17,999, else dropped; the dirty flag is set when added.
 * Returns the seconds added, or -1 when dropped. */
int logbook_book_flight(logbook_t *lb, unsigned long steps);

/* ---- the qualification (9.3) ---- */
/* The single success test of mode 9 (0x28A18): the record is active (W0 bit 0x0040), hook down, hook engaged and wheels down (W2 & 0xC080 == 0xC080), the
 * indicated speed is 0, no leave request 3 is pending, and the mission code V:4A850 is 0. */
int logbook_qual_success(uint16_t w0, uint16_t w2, int16_t speed, int request3_pending, uint8_t mission_code);

/* ---- the log screen (10.6) ---- */
/* Patches the runtime figures into the text of line `line` of the record with id `id` (text = the line's characters, len = its length): the twelve figures of
 * record 0x57 (digits right to left, leading zeros blanked, the last digit always shown), the callsign of the continuing-tour welcome 0x02 (from text index 8,
 * up to 24 characters or the first zero), the date line of the mission intro 0x5B (line 3: the month name ending at index 18, the day at indices 20 and 21, from the hours of flight
 * time, 10.9; the month name only when `months` is set) and the bogey bearing of the vector notice 0x58 (indices 13 to 15, three digits with leading zeros, 13.2; only when
 * `bogey_deg` is not negative). Returns 1 when the line is one of those, else 0. */
int logbook_patch_line(const logbook_t *lb, unsigned id, int line, char *text, size_t len);

/* ---- name entry (10.5) ---- */
enum { LB_NAME_IGNORED = 0, LB_NAME_TYPED, LB_NAME_ERASED, LB_NAME_END };
typedef struct {
    uint8_t pos;                /* V:4A8AE: characters stored */
    uint8_t rem;                /* V:4A8AD: characters still allowed (a signed byte: negative ends the entry) */
    uint8_t shadow[10];         /* V:4A8A3 .. 4A8AC */
    int ncell;                  /* cells drawn (the characters typed minus the erased); only the first LOGBOOK_NAME_CELLS are kept */
    char cell[LOGBOOK_NAME_CELLS + 1];
} logbook_name_t;
void logbook_name_begin(logbook_name_t *n);
/* One key of the entry: ch is the ASCII value of a letter (either case), digit or space, or 8 for BACKSPACE; anything else is ignored. Reproduces the table of
 * 10.5 on the record: the 11th and 12th characters overwrite rem and pos, record bytes at +0x1E + pos are dropped at 78 or beyond, BACKSPACE does nothing at
 * pos 0. Returns LB_NAME_END when rem becomes negative (the automatic end, which the port treats as RETURN: deviation 2), LB_NAME_TYPED, LB_NAME_ERASED or
 * LB_NAME_IGNORED. */
int logbook_name_key(logbook_name_t *n, logbook_t *lb, int ch);

#endif
