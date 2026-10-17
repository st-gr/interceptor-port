/* src/core/cockpit.c - frame counter, warning tick generator, warning arbiter, code word and latch (docs/re/06-cockpit-hud.md,
 * "Warning line", "Ticks and blinking", "Durations of alerts", "Arbiter"), and the raw cockpit block of the scene.
 * Section names in the comments are that note's. */
#include <string.h>
#include "cockpit.h"

static int iabs_i(int v) { return v < 0 ? -v : v; }

void cockpit_init(cockpit_t *c, const uint8_t *warn_table) {
    memset(c, 0, sizeof *c);
    if (warn_table) { memcpy(c->warn, warn_table, sizeof c->warn); c->have_warn = 1; }
    c->hud_on = 1;
    c->redraw = 0xFF;   /* V:4A8EF after the boot: 0xFF (captured at the first flight pass of the demo, training part 2 and the qualification, note 07 12.1; the start-up routine's reset block leaves it) */
}

void cockpit_redraw_request(cockpit_t *c) { c->redraw = 3; c->panel = 3; }

/* The decrement 0x113E8 ends the panel block only (the external block 0x113F8 has none) and runs while R is positive (note 07 12.1). */
void cockpit_redraw_pass(cockpit_t *c, int panel_block) {
    /* R is a signed byte: the decrement runs while it is positive, so the boot value 0xFF stays. The scope dispatch 0x35CF2 compares R with 1 as a signed value (read from the code): a negative R takes the same
     * single-walk schedule as 0, 1 gives the double walk, above 1 also sets both erase-list heads (note 07 12.1). */
    c->redraw_now = (int8_t)c->redraw > 0 ? c->redraw : 0;
    if (panel_block && (int8_t)c->redraw > 0) c->redraw--;
}

void cockpit_mission_start(cockpit_t *c) {
    /* R (V:4A8EF) is NOT cleared: the first pass of a flight has no writer and holds what the previous screens left (0xFF after boot, cockpit_init: the demo, training part 2 and the qualification reach their first pass with it; the free flight too, captured 0 and 0xFF, note 07 12.1 [V: followup-combat-first-pass.txt]; training part 1 reaches its first pass with 0 after the gate-1 passes count R down from 3, screen.c out_redraw);
     * the menu, the briefing and the approach passes do not write it, so it keeps its value across them (note 07 12.1). V:4A8EE is not set here: its flow-state writers are the dispatch state (0x120E0), 0x1250C, 0x12730 and 0x12BA0 (screen.c out_panel)
     * and the gate-1 passes that follow them count it down (game.c gate1_screen, the port's approximation of those passes), so free flight reaches its first pass with 0 (note 07 12.1 [V: followup-combat-first-pass.txt]). */
    c->n = c->n_draw = 0;
    c->hud_on = 1;                           /* V:4A859, initially 1; the H key toggles it (cockpit.h; its writers are named in cockpit.h: the H key, the reset block and the state 0x12EDA, settled in the round of 08 6.1 item 1 against note 06) */
    c->last_idx = c->shown = 0;
    c->dcount = 0; c->dur = 0;
    c->text_shown = 0;
    c->snd_a = c->snd_b = 0;
    c->tick = 0;
    c->strip_blank = c->strip_off = 0;
    c->stock = 0x24;                         /* the values of the re-arm routine 0x1F14A (note 05 17.17 rule 4: +0x5F = 0x24, +0x60 = 500, written on every flight start); the HUD data takes the weapon records' values (game.c), these are the block's defaults */
    c->rounds = 500;
    c->zoom = 0x80;                          /* the new-mission routine 0x1D1BE plants the zoom words 168, 252, 128 (note 06 item 2) */
    /* Port rule (a reference-path pin, not a game constant): after the first pass the counter is 8 - ((n0 + 3) mod 8) with
     * n0 = 0; the seed is that value plus one, and the value 8 is reached from the seed 1. */
    int target = 8 - (int)((c->n + 3) % 8);
    c->gen = (uint8_t)(target == 8 ? 1 : target + 1);
}

void cockpit_generator(cockpit_t *c) {
    c->gen = (uint8_t)(c->gen <= 1 ? 8 : c->gen - 1);
    switch (c->gen) {
    case 8: c->tick = 0x86; break;
    case 4: c->tick = 0x06; break;
    case 6: case 2: c->tick = 0x04; break;
    default: c->tick = 0; break;
    }
}

/* The nine conditions in order (note 06 "Arbiter", item 4). Returns the code, or -1 when none holds. Conditions 6, 7 and 9
 * clear bit 7 of V:4A984 (they leave the line to the target data). */
static int pick(uint32_t ev, uint8_t th, uint16_t *state984) {
    if (th & 0x10) return 0x8010;
    if (th & 0x20) return 0x8012;
    if (ev & 0x40) return 0xC004;
    if (ev & 0x80) return 0xC001;
    if (ev & 0x02) return 0x8002;
    if (th & 0x08) { *state984 &= (uint16_t)~0x80; return 0x8024; }
    if (th & 0x04) { *state984 &= (uint16_t)~0x80; return 0x800E; }
    if (ev & 0x01) return 0x0C09;
    if (th & 0x02) { *state984 &= (uint16_t)~0x80; return 0x400D; }
    return -1;
}

/* The arbiter installs a new entry index: the duration counter is loaded from the entry's flag byte (bits 5 to 0),
 * bit 13 of V:4A986 and bit 2 of V:4A984 are cleared, and the entry's bytes become V:4A91A and V:4A91B.
 * The arbiter loads them at the install (note 06); the alert text's phases raise the cues 0x04 and 0x08 of the sound byte (cockpit_arbiter, note 10 8.3). */
static void install(cockpit_t *c, flight_state_t *f, int idx) {
    c->last_idx = (uint8_t)idx;
    const uint8_t *e = idx < COCKPIT_WARN_ENTRIES ? c->warn[idx] : c->warn[0];
    c->dur = (uint8_t)(e[0] & 63);
    c->dcount = (int8_t)c->dur;
    f->v4a984 &= (uint16_t)~4;
    c->snd_a = e[0];
    c->snd_b = e[27];
}

void cockpit_arbiter(cockpit_t *c, aircraft_t *ac, int paused) {
    flight_state_t *f = &ac->f;
    uint16_t code = f->v4ab98;
    /* 1. a latched message is kept; the latch lasts until the code word is 0 */
    if ((f->v4a984 & 1) && code == 0) f->v4a984 &= (uint16_t)~1;
    if (!(f->v4a984 & 1)) {
        int cand = -1;
        uint32_t ev = (uint32_t)f->v4ac08;
        /* The arbiter's rules 2 (V:4A980 bit 15: crash imminent, entry 6) and 3 (V:4A98E bit 5: blank line with the alternate duration byte V:4AC2C, entries 8, 32, 34) are dead: no store sets either
         * bit (note 07 sweep B: V:4A980 is cleared at the flight start only, V:4A98E bit 5 only cleared), so the list below is reached whenever the latch is clear. */
        if (ev || c->threat) {                            /* 4. the nine conditions */
            f->v4a984 |= 0x80;
            cand = pick(ev, c->threat, &f->v4a984);       /* none known: the code word is left as it was */
        } else {                                          /* all clear */
            f->v4a984 &= (uint16_t)~0x80;
            if (!(f->v4a984 & 4)) { code = 0; c->last_idx = 0; }
        }
        /* a change of the low byte against V:4AB9A installs the code; the same index leaves the code word alone, which is how
         * an expired alert stays steady and an expired timed information stays blank while the condition holds */
        if (cand >= 0 && (cand & 0xFF) != c->last_idx) { code = (uint16_t)cand; install(c, f, cand & 0xFF); }
    } else if ((code & 0xFF) != c->last_idx) {
        install(c, f, code & 0xFF);                       /* a posted code: replaces an arbiter alert on the same pass */
    }

    /* the line (V:4AB96) */
    int idx = code & 0xFF;
    int alert = (code & COCKPIT_CODE_ALERT) != 0, timed = (code & COCKPIT_CODE_TIMED) != 0;
    uint8_t t = c->tick;
    if (alert) {
        /* "Ticks and blinking": one phase per tick of the code's kind (fast with bit 12, else slow); a change of the entry does not reset the flag */
        int tk = (code & COCKPIT_CODE_FAST) ? (t & 4) : (t & 2);
        if (tk) {
            if (c->text_shown && !paused) {
                c->text_shown = 0;
                if (c->snd_b) f->v4ac0c |= 0x08;                 /* 0x14054: the alert text goes off and the entry has a sound byte: the cue 0x08 stops the tone (note 10 8.3) */
                c->shown = (uint8_t)(timed ? 0 : (idx + 1 < COCKPIT_WARN_ENTRIES ? idx + 1 : idx));   /* partner N + 1, or the blank entry */
            } else {
                c->text_shown = 1;
                if (c->snd_b) f->v4ac0c |= 0x04;                 /* 0x14082: the alert text comes on and the entry has a sound byte: the cue 0x04 plays the alert tone */
                c->shown = (uint8_t)idx;
                /* "Durations of alerts": an alert with D above 0 expires after D + 1 text phases, into steady plain text; D = 0 never expires */
                if (c->dur && --c->dcount < 0) {
                    code = (uint16_t)idx;
                    f->v4a984 &= (uint16_t)~1;
                }
            }
        }
    } else if (timed) {
        c->shown = (uint8_t)idx;                          /* shows at once */
        if ((t & 2) && --c->dcount < 0) {                 /* counts D down on slow ticks only; at expiry the line is blank */
            code = 0;
            c->shown = 0;
            f->v4a984 &= (uint16_t)~1;
        }
    } else {
        c->shown = (uint8_t)(code ? idx : 0);             /* a plain code shows steadily; the code 0 is the blank line */
    }
    f->v4ab98 = code;
    c->tick = 0;                                          /* a tick lives for one pass */
}

void cockpit_end_pass(cockpit_t *c, int paused) {
    c->strip_off = c->strip_blank;
    c->strip_blank = 0;
    c->pass++;                                   /* every pass that reaches the draws, paused or not */
    c->paused = (uint8_t)(paused != 0);
    if (!paused) { c->n_draw = c->n; c->n++; }   /* a paused pass leaves n and the published n alone: n stands still through a pause and keeps its low bits (note 06 "View kinds, settled" item 8: no handler clears n & 7) */
}

void cockpit_post(cockpit_t *c, aircraft_t *ac, uint16_t code) {
    flight_state_t *f = &ac->f;
    f->v4ab98 = code;
    if (code == 0x4800) f->v4a984 &= (uint16_t)~1;        /* only a posted 0x4800 clears the latch */
    else f->v4a984 |= 1;
    if (code == 0x4000 || code == 0x4800) c->text_shown = 0;   /* NOTE-GAP: the note says the masked compare with the two blank codes clears "the shown bit"; which bit is not stated */
}


void cockpit_toggle_hud(cockpit_t *c) { c->hud_on = !c->hud_on; }
void cockpit_entity_switch(cockpit_t *c) { c->strip_blank = 1; }

void cockpit_fill_hud(const cockpit_t *c, const aircraft_t *ac, game_hud_t *h) {
    const flight_state_t *f = &ac->f;
    h->cp_valid = 1;
    h->cp_class = f->cls;
    h->cp_sel = f->id;
    h->cp_stock = c->stock;
    h->cp_b03 = (uint8_t)(f->w2 & 0xFF);
    h->cp_b20 = f->b20;
    h->cp_b7c = f->gear;
    h->cp_throttle = (uint8_t)iabs_i(f->throttle);
    h->cp_w00 = f->w0;
    h->cp_rounds = c->rounds;
    h->cp_w6c = (uint16_t)f->speed_target;
    h->cp_pitch_rate = f->rate_p;
    h->cp_gcorr = f->v4a9fe;
    h->cp_heading80 = f->heading;
    h->cp_speed_w = f->speed;
    h->cp_y = ac->pos[1];
    h->cp_lat = ac->pos[2];            /* E+0x1C */
    h->cp_lon = ac->pos[0];            /* E+0x14 */
    h->cp_fuel = f->fuel;
    h->cp_gsign = (uint8_t)((f->v4a982 >> 1) & 1);
    /* V:4A863 (the eject arming) and V:4A96D (the ECM switch) are published by game.c from the keys' events (game_scene). */
    h->cp_eject_armed = 0;
    h->cp_ecm = 0;
    h->cp_gear_lamp = f->v4a8fe;
    h->cp_zoom = c->zoom ? c->zoom : 0x80;   /* V:4AAFA, the zoom keys' word (0x80 at the mission start) */
    h->cp_crashed = (uint8_t)!(f->v4a984 & 0x40);   /* V:4A985 bit 6 clear after the crash hand-over */
    h->cp_threat = c->threat;
    h->cp_n = c->n_draw;
    h->cp_pass = c->pass;
    h->cp_redraw = c->redraw_now;
    h->cp_paused = c->paused;
    h->cp_hud_on = c->hud_on;
    h->cp_msg_code = f->v4ab98;
    h->cp_msg_entry = c->shown;
    h->cp_msg_ext = f->v4a95e == 2;   /* 0x11426: the external message line is drawn only when the mode byte is 2 (note 08 8.7) */
    h->cp_msg_phase = c->text_shown;
    h->cp_sound_req = c->snd_b;   /* V:4A91B: the sound byte of the installed entry (the arbiter raises the cues 0x04 and 0x08 itself) */
    h->cp_strip_off = c->strip_off;
}

/* The cockpit zoom keys (0x1E80E, 0x1E82E; note 06 "View kinds, settled" item 9): `]` halves the word V:4AAFA when it is above 0x20, `[` doubles it when it is below 0x80. */
void cockpit_zoom_key(cockpit_t *c, int closer) {
    uint8_t z = c->zoom ? c->zoom : 0x80;
    if (closer) { if (z > 0x20) z >>= 1; }
    else if (z < 0x80) z <<= 1;
    c->zoom = z;
}

/* The zoom reset 0x1F060 (every head key, every view switch and the viewed-entity switch): the zoom word V:4AAFA = 0x80 (bit 7 of V:4A895 set). It also stores V:4A8F5 = 3, the redraw
 * counter of the radar magnification digit (drawer 0x36590, note 07 12.1): the frontend draws the digit from the scene on every frame, so the counter has no visible effect and only the zoom word is modelled. */
void cockpit_zoom_reset(cockpit_t *c) { c->zoom = 0x80; }
