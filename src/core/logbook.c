/* src/core/logbook.c - the flight log (docs/re/08-game-flow.md section 10, and the qualification test of section 9.3). Every number below is a section of that
 * note. No game text lives here, and nothing here reads a file or a clock. */
#include <string.h>
#include "logbook.h"
#include "missions.h"

unsigned logbook_get16(const logbook_t *lb, int off) { return ((unsigned)lb->rec[off] << 8) | lb->rec[off + 1]; }
uint32_t logbook_get32(const logbook_t *lb, int off) { return ((uint32_t)logbook_get16(lb, off) << 16) | logbook_get16(lb, off + 2); }
void logbook_set16(logbook_t *lb, int off, unsigned v) { lb->rec[off] = (uint8_t)(v >> 8); lb->rec[off + 1] = (uint8_t)v; }
static void set32(logbook_t *lb, int off, uint32_t v) { logbook_set16(lb, off, v >> 16); logbook_set16(lb, off + 2, v & 0xFFFFu); }

void logbook_zero(logbook_t *lb) { memset(lb, 0, sizeof *lb); lb->bogey_deg = -1; }

void logbook_reset(logbook_t *lb) { memset(lb->rec, 0, sizeof lb->rec); lb->dirty = 1; }

void logbook_load_record(logbook_t *lb, const uint8_t *rec, size_t len) {
    memset(lb, 0, sizeof *lb);
    if (rec && len) memcpy(lb->rec, rec, len > LOGBOOK_SIZE ? LOGBOOK_SIZE : len);
    lb->live_level = 0;   /* 14.3: 0x1D150 loads V:4A95F from +2 before the log has been read from the disk: the program starts every run at progress 0 */
    lb->bogey_deg = -1;
}

int logbook_file_unwrap(const uint8_t *file, size_t len, const uint8_t **rec, size_t *rec_len) {
    if (!file || len < LOGBOOK_HEADER) return LOGBOOK_ERR_SHORT;
    if (memcmp(file, "ILOG", 4) != 0) return LOGBOOK_ERR_MAGIC;
    if (file[4] != LOGBOOK_VERSION) return LOGBOOK_ERR_VERSION;
    *rec = file + LOGBOOK_HEADER;
    *rec_len = len - LOGBOOK_HEADER;
    return 0;
}

size_t logbook_file_encode(const logbook_t *lb, uint8_t *buf, size_t cap) {
    if (!buf || cap < LOGBOOK_FILE_SIZE) return 0;
    memcpy(buf, "ILOG", 4);
    buf[4] = LOGBOOK_VERSION;
    memcpy(buf + LOGBOOK_HEADER, lb->rec, LOGBOOK_SIZE);
    return LOGBOOK_FILE_SIZE;
}

void logbook_add(logbook_t *lb, int off, unsigned delta) {
    logbook_set16(lb, off, (logbook_get16(lb, off) + delta) & 0xFFFFu);
    lb->dirty = 1;
}

void logbook_save_point(logbook_t *lb) { lb->dirty = 0; lb->save_req = 1; }

/* ---- what the log unlocks ---------------------------------------------------------------------------------------------------------------------- */

int logbook_item7_mode(const logbook_t *lb) {
    if (logbook_get16(lb, LB_QUALIFIED) == 0) return 0;
    const unsigned m = (unsigned)lb->rec[LB_LAST_MISSION] + 1u;
    return m >= 3 && m <= 8 ? (int)m : 3;
}

int logbook_fkey_mode(const logbook_t *lb, int n) {
    if (n < 1 || n > 6 || logbook_get16(lb, LB_QUALIFIED) == 0) return 0;
    const int mission = n + 2;
    if (n == 1) return mission;
    return lb->rec[0x11 + mission] ? mission : 0;
}

unsigned logbook_sublist_selector(const logbook_t *lb, int mode) {
    if (mode < 0 || mode > 9) return 0;   /* the demo rows read a zero selector (6.1) */
    const unsigned v = lb->rec[LB_COMPLETIONS + mode];
    return v > 3 ? 3 : v;
}

void logbook_mission_complete(logbook_t *lb, int mode) {
    if (mode < 3 || mode > 8) return;
    uint8_t *c = &lb->rec[LB_COMPLETIONS + mode];
    logbook_set16(lb, LB_LEVEL, lb->live_level);
    lb->rec[LB_LAST_MISSION] = (uint8_t)mode;
    lb->rec[LB_LAST_COUNT] = *c;
    logbook_add(lb, LB_COMPLETED, 1);
    if (lb->live_level < 3) lb->live_level++;
    if (*c < 3) (*c)++;
    lb->dirty = 1;
}

void logbook_mission_started(logbook_t *lb, int mode) {
    if (mode >= 3 && mode <= 8) logbook_add(lb, LB_STARTED, 1);
}

int logbook_book_flight(logbook_t *lb, unsigned long steps) {
    const unsigned long secs = steps / 8ul;
    if (secs >= (unsigned long)LOGBOOK_MAX_FLIGHT_SECONDS) return -1;
    set32(lb, LB_SECONDS, logbook_get32(lb, LB_SECONDS) + (uint32_t)secs);
    lb->dirty = 1;
    return (int)secs;
}

int logbook_qual_success(uint16_t w0, uint16_t w2, int16_t speed, int request3_pending, uint8_t mission_code) {
    return (w0 & 0x0040u) && (w2 & 0xC080u) == 0xC080u && speed == 0 && !request3_pending && mission_code == 0;
}

/* ---- the figures of the log screen (10.6) ------------------------------------------------------------------------------------------------------ */

/* The formatter 0x116BA: the low n decimal digits of v at the text indices first .. first + n - 1, least significant last; leading zeros become spaces except
 * the last digit. */
static void put_digits(char *text, size_t len, int first, int n, uint32_t v) {
    if (first < 0 || (size_t)(first + n) > len) return;
    char d[8];
    for (int i = n - 1; i >= 0; i--) { d[i] = (char)('0' + v % 10u); v /= 10u; }
    for (int i = 0; i < n - 1 && d[i] == '0'; i++) d[i] = ' ';
    memcpy(text + first, d, (size_t)n);
}

int logbook_patch_line(const logbook_t *lb, unsigned id, int line, char *text, size_t len) {
    if (id == 0x57) {
        switch (line) {
        case 2: put_digits(text, len, 36, 3, logbook_get16(lb, LB_STARTED)); return 1;
        case 3: put_digits(text, len, 36, 3, logbook_get16(lb, LB_COMPLETED)); return 1;
        case 6: put_digits(text, len, 22, 5, logbook_get16(lb, LB_CANNON_FIRED)); put_digits(text, len, 31, 4, logbook_get16(lb, LB_CANNON_HITS)); return 1;
        case 7: put_digits(text, len, 22, 5, logbook_get16(lb, LB_AIM120_FIRED)); put_digits(text, len, 31, 4, logbook_get16(lb, LB_AIM120_HITS)); return 1;
        case 8: put_digits(text, len, 22, 5, logbook_get16(lb, LB_AIM9_FIRED)); put_digits(text, len, 31, 4, logbook_get16(lb, LB_AIM9_HITS)); return 1;
        case 10: {
            const uint32_t s = logbook_get32(lb, LB_SECONDS);
            put_digits(text, len, 22, 4, (s / 3600u) & 0xFFFFu);
            put_digits(text, len, 32, 2, (s % 3600u) / 60u);
            return 1;
        }
        case 11: put_digits(text, len, 22, 4, logbook_get16(lb, LB_CRASHES)); return 1;
        case 12: put_digits(text, len, 22, 4, logbook_get16(lb, LB_MISSILE_HITS)); return 1;
        default: return 0;
        }
    }
    if (id == 0x5B && line == 3) {   /* 10.9 (0x292F0): hours = the flight time in whole hours; the month by min(hours, 127) >> 5, the day (hours AND 31) + 1 limited to 30 */
        const unsigned hours = (logbook_get32(lb, LB_SECONDS) / 3600u) & 0xFFFFu;
        if (lb->months && len > 18) memcpy(text + 10, lb->months + 9 * missions_date_month(hours), 9);   /* nine characters ending at index 18 */
        put_digits(text, len, 20, 2, missions_date_day(hours));
        return 1;
    }
    if (id == 0x58 && line == 0) {   /* 13.2 (0x295BE): the bearing in degrees, three decimal digits left to right at indices 13 to 15 */
        if (lb->bogey_deg >= 0 && len > 15) {
            const unsigned d = (unsigned)lb->bogey_deg % 1000u;
            text[13] = (char)('0' + d / 100u); text[14] = (char)('0' + d / 10u % 10u); text[15] = (char)('0' + d % 10u);
        }
        return 1;
    }
    if (id == 0x02 && line == 0) {   /* 0x13774: the callsign bytes from text index 8, up to 24 characters or the first zero byte */
        for (int i = 0; i < LOGBOOK_NAME_FIELD && lb->rec[LB_CALLSIGN + i] != 0 && (size_t)(8 + i) < len; i++) text[8 + i] = (char)lb->rec[LB_CALLSIGN + i];
        return 1;
    }
    return 0;
}

/* ---- name entry (10.5) ------------------------------------------------------------------------------------------------------------------------- */

void logbook_name_begin(logbook_name_t *n) {
    memset(n, 0, sizeof *n);
    n->rem = 20;   /* V:4A8AD = 20 */
}

static void rec_store(logbook_t *lb, unsigned off, uint8_t v) { if (off < LOGBOOK_SIZE) lb->rec[off] = v; }   /* a byte at or beyond 78 is dropped (never a write past the record) */

int logbook_name_key(logbook_name_t *n, logbook_t *lb, int ch) {
    if (ch == 8) {   /* BACKSPACE: nothing at an empty field; else the callsign byte below pos is cleared and the counts go back */
        if (n->pos == 0) return LB_NAME_IGNORED;
        rec_store(lb, LB_CALLSIGN + (unsigned)n->pos - 1u, 0);
        n->pos--;
        n->rem++;
        if (n->ncell > 0) { n->ncell--; if (n->ncell < LOGBOOK_NAME_CELLS) n->cell[n->ncell] = '\0'; }
        return LB_NAME_ERASED;
    }
    int c;
    if (ch >= 'a' && ch <= 'z') c = ch - 'a' + 'A';
    else if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ') c = ch;
    else return LB_NAME_IGNORED;   /* the table maps only the 38 raw codes; everything else is dropped */
    /* the key handler's shadow-buffer write at index pos: the buffer is followed by the counters themselves */
    if (n->pos < 10) n->shadow[n->pos] = (uint8_t)c;
    else if (n->pos == 10) n->rem = (uint8_t)c;
    else if (n->pos == 11) n->pos = (uint8_t)c;
    /* higher indices are other game variables of the original: not modelled (deviation 1) */
    /* the engine's store into the record at +0x1E + pos, then pos + 1 and rem - 1 */
    rec_store(lb, LB_CALLSIGN + (unsigned)n->pos, (uint8_t)c);
    n->pos++;
    n->rem--;
    if (n->ncell < LOGBOOK_NAME_CELLS) { n->cell[n->ncell] = (char)c; n->cell[n->ncell + 1] = '\0'; }
    n->ncell++;
    return (int8_t)n->rem < 0 ? LB_NAME_END : LB_NAME_TYPED;
}
