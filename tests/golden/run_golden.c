/* tests/golden/run_golden.c - scripted flight run printing one integer-only line per tick, byte-stable across platforms.
 * Usage: run_golden [--synthetic] [--audio-dump] [assets_dir] [script].
 * --audio-dump prints, instead of the golden lines, one line per register write of the sound library: tick op voice object blank value offset length clock.
 * With --synthetic the in-memory asset set of tests/world_synth.h is used; otherwise the loader reads a directory (default "assets";
 * prints "skipped" and exits 0 when it has no models).
 *
 * Script format (text, one directive per line, '#' starts a comment):
 *   start deck|runway        the flight start (before the first tick; default deck)
 *   freecam                  run the milestone-2 free camera instead of the aircraft (the old free-flight scripts)
 *   screens                  no debug start: the game begins at the credits and the script's keys drive the screens (the demo golden)
 *   at N cmd [args] ...      commands applied just before tick N (1-based), in file order; several 'at' lines may name the same tick
 *   ticks N                  the run length (default: the last 'at' tick)
 *   synthetic N              with --synthetic the run is cut to N ticks (the committed golden of a long real run stays short; with synthfrom it is the length of the synthetic run, which may be longer or shorter)
 *   synthflight              with --synthetic the world gets the INVENTED flight tables (all zero: the aircraft exist and step, nothing moves) and the invented mission tables of world_synth.h (scenario rows,
 *                            templates, position blocks, the briefing chain, ai.bin with a hull the gun cannot miss) and a 60-vertex carrier model, so a menu function key starts its mission; without it
 *                            the synthetic world has no flight tables and the key is refused (the free-flight, demo and training scripts keep that)
 *   synthtrig                with --synthetic and synthflight: two more invented rows (world_synth.h): a ramp for the quarter-wave table (the zero tables give zero matrices) and the hard-point row of the player's long-range missile (the F5 fight)
 *   synthfrom T              with --synthetic the "at" lines after tick T are dropped (the real fight does not fit the invented placement); the "sat" lines replace it
 *   sat N cmd [args] ...     like "at", for the synthetic run only (the real run ignores it); its ticks are ordered among themselves
 *   seed N                 game_random_seed(N) before the first tick (the placement's generator; the default seed is 1; the mission scripts say it explicitly)
 *   log C0 .. C8             a flight log is imported before the first tick: qualified, tours 2, the nine completion bytes C0 .. C8 (a mission F2 to F6 is on the menu when the byte of the
 *                            mission before it is non-zero: C3 for F2 .. C7 for F6); without it the menu's function keys are refused (no log)
 * Commands: stick X Y | throttle A | rudder A (axes -32768..32767) | btn NAME [VALUE] (a GAME_BTN_* name without the prefix (the flight and combat keys: FIRE SELECT_WEAPON FLARE CHAFF ECM RADAR_RANGE TARGET MESSAGE_PAGE EJECT RESCUE REPLENISH ...), value
 * defaults to 1; the combat keys too: FIRE SELECT_WEAPON FLARE CHAFF ECM RADAR_RANGE TARGET MESSAGE_PAGE EJECT RESCUE REPLENISH SHIFT_ESC SMOKE) | key N (a GAME_EV_KEY with the ASCII value N) | kp K | kpup K (keypad key 0..9, dot = 10, enter = 11) | fkey N | clock (one game_clock_second).
 * The timer second is delivered every FLIGHT_STEP_HZ ticks, as the frontend does in a counted run.
 *
 * Line: tick mode view head ring | x y z pitch roll yaw | speed throttle gear hook ground | alt_ft heading_deg airspeed_kt fuel_pct hud_on |
 *       entries entry_hash | sky_hash side zone mesh fade | crash_state flash | text_count text_id text_hash part lesson
 *       | active stock rounds flares chaff | radar threat | end seed      (appended in milestone 5)
 *       | audio_count audio_hash master_target master_set master_ramp      (appended in milestone 6)
 * active is the number of entity records with W0 bit 0x1000 set, stock and rounds the player's missile stock byte and gun rounds (record 0), flares and chaff the
 * counts V:4A905 and V:4A904, radar the scene's radar list count, threat the threat bits V:4A926 (hex), end the end code V:4A850 (hex) and seed the placement
 * generator's state (the screen module's); all 0 where the flight tables are absent.
 * text_count and text_hash describe the text lines of the screen: text_id is the record id of the first line (game_text_t.id; 0 with no text), the hash an FNV-1a over each line's row, column, pen, flags, shown
 * and length and its characters, so a changed screen changes it and no game text appears in the golden; part and lesson are the training module's (0 0 outside the trainings).
 * audio_count is the number of register writes the sound library made in the step (scene.audio, note 10 9.2) and audio_hash an FNV-1a over them in order: op, voice, object, value, blank, offset and length; master_target, master_set (hex, ff none)
 * and master_ramp are the scene's master inputs. (angles in the core's 28800-per-circle integer units; speed is the speed word, in twelfths of a knot). Exits 1 on a malformed script line. */
#include "../world_synth.h"
#include "../../src/core/training.h"
#include "../../src/core/screen.h"
#include "../../src/core/entities.h"
#include "../../src/core/weapons.h"
#include "../../src/core/missions.h"

typedef struct { const char *dir; } fs_t;
static int fs_read(void *user, const char *path, uint8_t **out, size_t *len) {
    char full[512];
    snprintf(full, sizeof full, "%s/%s", ((fs_t *)user)->dir, path);
    FILE *f = fopen(full, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) { fclose(f); return -1; }
    fseek(f, 0, SEEK_SET);
    *out = (uint8_t *)malloc((size_t)n + 1);
    if (!*out || fread(*out, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(*out); return -1; }
    fclose(f);
    *len = (size_t)n;
    return 0;
}
static void fs_release(void *user, uint8_t *buf) { (void)user; free(buf); }

typedef struct { int tick; char cmd[16]; int a, b; int syn; } cmd_t;   /* syn: a "sat" command, for the synthetic run only */
#define MAX_CMDS 2048

static const struct { const char *name; int code; } BTN[] = {
    { "FIRE", GAME_BTN_FIRE }, { "SELECT_WEAPON", GAME_BTN_SELECT_WEAPON }, { "GEAR", GAME_BTN_GEAR }, { "HOOK", GAME_BTN_HOOK },
    { "FLARE", GAME_BTN_FLARE }, { "CHAFF", GAME_BTN_CHAFF }, { "ESC", GAME_BTN_ESC }, { "RETURN", GAME_BTN_RETURN },
    { "TOWER", GAME_BTN_TOWER }, { "BRAKE", GAME_BTN_BRAKE }, { "PAUSE", GAME_BTN_PAUSE },
    { "THROTTLE_UP", GAME_BTN_THROTTLE_UP }, { "THROTTLE_DOWN", GAME_BTN_THROTTLE_DOWN }, { "HUD_TOGGLE", GAME_BTN_HUD_TOGGLE },
    { "LBRACKET", GAME_BTN_LBRACKET }, { "RBRACKET", GAME_BTN_RBRACKET },
    { "TARGET", GAME_BTN_TARGET }, { "ECM", GAME_BTN_ECM }, { "RADAR_RANGE", GAME_BTN_RADAR_RANGE }, { "MESSAGE_PAGE", GAME_BTN_MESSAGE_PAGE },
    { "EJECT", GAME_BTN_EJECT }, { "RESCUE", GAME_BTN_RESCUE }, { "FIXED_CAMERA", GAME_BTN_FIXED_CAMERA }, { "REPLENISH", GAME_BTN_REPLENISH },
    { "SHIFT_ESC", GAME_BTN_SHIFT_ESC }, { "SMOKE", GAME_BTN_SMOKE }, { NULL, 0 }
};

static int is_number(const char *s) { return *s && strspn(s, "-0123456789") == strlen(s); }

/* Parses the script; returns 0 or the 1-based number of the bad line. */
static int screens_mode, seed_set, log_set, syn_ticks, syn_from;
static uint32_t seed_val;
static uint8_t log_comp[9];
static int parse_script(FILE *f, cmd_t *cmds, int *ncmds, int *ticks, int *runway, int *freecam, int *bad_line) {
    char line[512];
    int ln = 0, last = 0, lasts = 0, cur = 0, is_s = 0;
    *ncmds = 0; *ticks = 0; *runway = 0; *freecam = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        char *h = strchr(line, '#');
        if (h) *h = 0;
        char *tok[32]; int nt = 0;
        for (char *t = strtok(line, " \t\r\n"); t && nt < 32; t = strtok(NULL, " \t\r\n")) tok[nt++] = t;
        if (!nt) continue;
        *bad_line = ln;
        if (!strcmp(tok[0], "start") && nt == 2) { *runway = !strcmp(tok[1], "runway"); if (!*runway && strcmp(tok[1], "deck")) return 1; continue; }
        if (!strcmp(tok[0], "freecam") && nt == 1) { *freecam = 1; continue; }
        if (!strcmp(tok[0], "screens") && nt == 1) { screens_mode = 1; continue; }
        if (!strcmp(tok[0], "synthetic") && nt == 2 && is_number(tok[1])) { syn_ticks = atoi(tok[1]); continue; }
        if (!strcmp(tok[0], "synthflight") && nt == 1) { synth_flight = 1; continue; }
        if (!strcmp(tok[0], "synthtrig") && nt == 1) { synth_trig = 1; continue; }
        if (!strcmp(tok[0], "synthfrom") && nt == 2 && is_number(tok[1])) { syn_from = atoi(tok[1]); continue; }
        if (!strcmp(tok[0], "seed") && nt == 2 && is_number(tok[1])) { seed_set = 1; seed_val = (uint32_t)atol(tok[1]); continue; }
        if (!strcmp(tok[0], "log") && nt == 10) {
            for (int k = 0; k < 9; k++) { if (!is_number(tok[1 + k])) return 1; log_comp[k] = (uint8_t)atoi(tok[1 + k]); }
            log_set = 1; continue;
        }
        if (!strcmp(tok[0], "ticks") && nt == 2 && is_number(tok[1])) { *ticks = atoi(tok[1]); continue; }
        if ((strcmp(tok[0], "at") && strcmp(tok[0], "sat")) || nt < 3 || !is_number(tok[1])) return 1;
        is_s = tok[0][0] == 's';
        cur = atoi(tok[1]);
        if (cur < (is_s ? lasts : last) || cur < 1) return 1;
        if (is_s) lasts = cur; else last = cur;
        for (int i = 2; i < nt;) {
            if (*ncmds >= MAX_CMDS) return 1;
            cmd_t *c = &cmds[*ncmds];
            memset(c, 0, sizeof *c);
            c->tick = cur; c->syn = is_s;
            snprintf(c->cmd, sizeof c->cmd, "%s", tok[i]);
            int need = 0;
            if (!strcmp(c->cmd, "stick")) need = 2;
            else if (!strcmp(c->cmd, "throttle") || !strcmp(c->cmd, "rudder") || !strcmp(c->cmd, "kp") || !strcmp(c->cmd, "kpup") || !strcmp(c->cmd, "fkey") || !strcmp(c->cmd, "key")) need = 1;
            else if (!strcmp(c->cmd, "clock")) need = 0;
            else if (!strcmp(c->cmd, "btn")) {
                need = 1;
                int k = 0;
                if (i + 1 >= nt) return 1;
                while (BTN[k].name && strcmp(BTN[k].name, tok[i + 1])) k++;
                if (!BTN[k].name) return 1;
                c->a = BTN[k].code; c->b = 1;
                if (i + 2 < nt && is_number(tok[i + 2])) { c->b = atoi(tok[i + 2]); i++; }
                i += 2; (*ncmds)++;
                continue;
            } else return 1;
            if (need >= 1) { if (i + 1 >= nt || !is_number(tok[i + 1])) return 1; c->a = atoi(tok[i + 1]); }
            if (need >= 2) { if (i + 2 >= nt || !is_number(tok[i + 2])) return 1; c->b = atoi(tok[i + 2]); }
            i += 1 + need; (*ncmds)++;
        }
    }
    if (!*ticks) *ticks = last;
    return 0;
}

/* An invented demo stream (2060 frames, the marker at 2046; no shipped bytes) for the synthetic set, so that the menu has item 1: the file is bigger than the synthetic store's
 * 4 KB buffers, so it is made here on request. The synthetic set has no flight model: this stream exercises the screens and the modes. */
static int syn_read(void *user, const char *path, uint8_t **out, size_t *len) {
    if (strcmp(path, "tables/demo.bin")) return synth_flight_read(user, path, out, len);
    const int n = 2060;
    uint8_t *b = (uint8_t *)calloc(4 + (size_t)n * 5 + 1, 1);
    if (!b) return -1;
    b[0] = (uint8_t)(n & 255); b[1] = (uint8_t)(n >> 8);
    for (int i = 0; i < n; i++) b[4 + i] = (uint8_t)(i >= 2046 && i < 2050 ? 0xFF : (i % 5 == 0 ? 0x14 : 0x04));
    for (int i = 0; i < n; i++) { const unsigned w2 = i % 97 == 50 ? 0x0200 : 0; b[4 + n + 4 * i + 2] = (uint8_t)(w2 & 255); b[4 + n + 4 * i + 3] = (uint8_t)(w2 >> 8); }
    *out = b; *len = 4 + (size_t)n * 5;
    return 0;
}
static const game_loader_t SYN_LD = { syn_read, fake_release, NULL };

static uint32_t fnv(uint32_t h, const int32_t *f, int n) {   /* FNV-1a over 32-bit words, little-endian byte order */
    for (int k = 0; k < n; k++) for (int b = 0; b < 4; b++) { h ^= (uint32_t)((uint32_t)f[k] >> (8 * b)) & 0xFF; h *= 16777619u; }
    return h;
}

int main(int argc, char **argv) {
    int synthetic = 0, audio_dump = 0;
    const char *dir = "assets", *script = "tests/golden/takeoff-and-circuit.txt";
    int pos = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--synthetic")) synthetic = 1;
        else if (!strcmp(argv[i], "--audio-dump")) audio_dump = 1;
        else if (pos++ == 0) dir = argv[i];
        else script = argv[i];
    }
    /* keep the shared header's helpers referenced in this build */
    (void)drop_file; (void)fake_release; (void)released; (void)reads;
    fs_t fs = { dir };
    game_loader_t real = { fs_read, fs_release, &fs };
    const game_loader_t *ld = &real;
    if (synthetic) {
        build_all(); ld = &LD;
        ld = &SYN_LD;   /* the synthetic store plus an invented demo stream (below) */
    }
    else {
        uint8_t *probe; size_t pl;
        if (fs_read(&fs, "models/index.txt", &probe, &pl) != 0) {
            printf("run_golden: skipped (%s/models/index.txt not found)\n", dir);
            return 0;
        }
        free(probe);
    }
    FILE *sf = fopen(script, "r");
    if (!sf) { fprintf(stderr, "run_golden: cannot open %s\n", script); return 1; }
    cmd_t *cmds = (cmd_t *)calloc(MAX_CMDS, sizeof *cmds);
    int ncmds, ticks, runway, freecam, bad = 0;
    if (parse_script(sf, cmds, &ncmds, &ticks, &runway, &freecam, &bad)) {
        fprintf(stderr, "run_golden: malformed line %d in %s\n", bad, script);
        fclose(sf); free(cmds);
        return 1;
    }
    fclose(sf);
    {   /* the synthetic run takes the "at" commands up to synthfrom (all when it is absent) and the "sat" commands; the real run takes the "at" commands only. Stable by tick, "sat" after "at" on a tie. */
        int n = 0;
        for (int i = 0; i < ncmds; i++)
            if (synthetic ? (cmds[i].syn || !syn_from || cmds[i].tick <= syn_from) : !cmds[i].syn) cmds[n++] = cmds[i];
        for (int i = 1; i < n; i++) { cmd_t t = cmds[i]; int j = i; while (j > 0 && cmds[j - 1].tick > t.tick) { cmds[j] = cmds[j - 1]; j--; } cmds[j] = t; }
        ncmds = n;
    }
    if (synthetic && syn_ticks > 0 && (syn_from || syn_ticks < ticks)) ticks = syn_ticks;
    if (synthetic && synth_flight) { synth_missions = 1; synth_invent_carrier(); synth_mission_tables(); }   /* the flight start reads the carrier vertices from model 8 */
    char err[256] = "";
    game_t *g = game_create(ld, err, sizeof err);
    if (!g) { fprintf(stderr, "run_golden: %s\n", err); free(cmds); return 1; }
    if (freecam) game_set_debug_freecam(g, 1);
    if (seed_set) game_random_seed(g, seed_val);
    if (log_set) {   /* a log with a player record: qualified, tours 2, the completion bytes (logbook.h: +0x00, +0x04, +0x12) */
        uint8_t buf[GAME_LOG_FILE_SIZE];
        memset(buf, 0, sizeof buf);
        memcpy(buf, "ILOG", 4); buf[4] = 1;
        buf[5 + 1] = 1; buf[5 + 5] = 2;
        memcpy(buf + 5 + 0x12, log_comp, 9);
        game_log_import(g, buf, sizeof buf);
    }
    /* The flight starts at once, as the splash's RETURN did before the screens existed (milestone 4): the golden covers the flight, the screens' own start is tested in
     * tests/test_screen.c. "start runway" is airfield 1 (location 0), the default the carrier deck (location 3). */
    if (!screens_mode) game_debug_start_flight(g, runway ? 0 : 3, 0);
    game_event_t ev = { GAME_EV_BUTTON, GAME_BTN_RETURN, 1 };
    game_scene_t *s = (game_scene_t *)calloc(1, sizeof *s);
    int ci = 0, sec = 0;
    for (int tick = 1; tick <= ticks; tick++) {
        for (; ci < ncmds && cmds[ci].tick == tick; ci++) {
            const cmd_t *c = &cmds[ci];
            if (!strcmp(c->cmd, "stick")) {
                ev.kind = GAME_EV_AXIS; ev.code = GAME_AXIS_STICK_X; ev.value = c->a; game_input(g, &ev);
                ev.code = GAME_AXIS_STICK_Y; ev.value = c->b; game_input(g, &ev);
            } else if (!strcmp(c->cmd, "throttle") || !strcmp(c->cmd, "rudder")) {
                ev.kind = GAME_EV_AXIS; ev.code = c->cmd[0] == 't' ? GAME_AXIS_THROTTLE : GAME_AXIS_RUDDER; ev.value = c->a; game_input(g, &ev);
            } else if (!strcmp(c->cmd, "btn")) {
                ev.kind = GAME_EV_BUTTON; ev.code = c->a; ev.value = c->b; game_input(g, &ev);
            } else if (!strcmp(c->cmd, "key")) {
                ev.kind = GAME_EV_KEY; ev.code = 0; ev.value = c->a; game_input(g, &ev);
            } else if (!strcmp(c->cmd, "kp") || !strcmp(c->cmd, "kpup") || !strcmp(c->cmd, "fkey")) {
                ev.kind = GAME_EV_BUTTON; ev.code = c->cmd[0] == 'f' ? GAME_BTN_FKEY : c->cmd[2] == 'u' ? GAME_BTN_KEYPAD_UP : GAME_BTN_KEYPAD;
                ev.value = c->a; game_input(g, &ev);
            } else if (!strcmp(c->cmd, "clock")) game_clock_second(g);
        }
        if (++sec >= FLIGHT_STEP_HZ) { sec = 0; game_clock_second(g); }
        game_step(g);
        int flash = game_flash(g);
        game_scene(g, s);
        uint32_t hash = 2166136261u;   /* FNV-1a over the entry stream: kind, tier, id, lod and the position words, in order */
        for (int i = 0; i < s->entry_count; i++) {
            const game_entry_t *e = &s->entries[i];
            int32_t f[7] = { e->kind, e->tier, e->id, e->lod, e->x, e->y, e->z };
            hash = fnv(hash, f, 7);
        }
        uint32_t sh = 2166136261u;   /* FNV-1a over the sky block: the nine matrix words, the polygon vertices, the camera height long */
        {
            int32_t f[24]; int nf = 0;
            for (int i = 0; i < 9; i++) f[nf++] = s->sky.matrix[i];
            f[nf++] = s->sky.sky_n; f[nf++] = s->sky.cam_y;
            for (int i = 0; i < 2 * s->sky.sky_n && nf < 24; i++) f[nf++] = s->sky.sky_xy[i];
            sh = fnv(sh, f, nf);
        }
        uint32_t th = 2166136261u;   /* FNV-1a over the text lines: the placement words and the characters */
        for (int i = 0; i < s->text_count; i++) {
            const game_text_t *t = &s->text[i];
            const int32_t f[6] = { t->row, t->col, t->pen, t->flags, t->shown, t->length };
            th = fnv(th, f, 6);
            for (int k = 0; k < t->length && k < GAME_TEXT_LEN; k++) { th ^= (uint8_t)t->text[k]; th *= 16777619u; }
        }
        static int max_audio;
        if (s->audio_count > max_audio) max_audio = s->audio_count;
        if (s->audio_overflow) { fprintf(stderr, "run_golden: the step's register writes overflowed GAME_MAX_AUDIO_CMDS at tick %d (scene.h states the bound)\n", tick); return 3; }
        if (getenv("GOLDEN_AUDIO_MAX") && tick == ticks) fprintf(stderr, "audio writes in one step, at most: %d\n", max_audio);
        uint32_t ah = 2166136261u;   /* FNV-1a over the step's audio commands */
        for (int i = 0; i < s->audio_count; i++) {
            const game_audio_cmd_t *a = &s->audio[i];
            const int32_t f[8] = { a->op, a->voice, a->object, a->value, a->blank, (int32_t)a->offset, (int32_t)a->length, (int32_t)a->clock };   /* the clock within the blank (note 10 9.2) is part of the contract */
            ah = fnv(ah, f, 8);
        }
        if (audio_dump) {   /* tests: the step's register writes in clear instead of the golden line: tick op voice object blank value offset length clock */
            for (int i = 0; i < s->audio_count; i++) {
                const game_audio_cmd_t *a = &s->audio[i];
                printf("%d %d %d %d %d %d %u %u %u\n", tick, a->op, a->voice, a->object, a->blank, a->value, (unsigned)a->offset, (unsigned)a->length, (unsigned)a->clock);
            }
            continue;
        }
        const struct trainer *tr = game_debug_trainer(g);
        const aircraft_t *ac = game_debug_aircraft(g);
        const entities_t *en = (const entities_t *)game_debug_entities(g);
        const weapons_t *wp = (const weapons_t *)game_debug_weapons(g);
        int nactive = 0;
        if (en) for (int i = 0; i < ENT_RECORDS; i++) if (en->rec[i].ac.f.w0 & EW0_ACTIVE) nactive++;
        printf("%d %d %d %d %d | %d %d %d %d %d %d | %d %d %d %d %d | %d %d %d %d %d | %u %08x | %08x %d %d %u %d | %d %d | %u %u %08x %d %d | %d %u %u %u %u | %u %x | %x %u | %u %08x %u %x %u\n", tick,
               (int)s->mode, (int)s->camera.view, (int)s->camera.head, (int)s->camera.ring,
               (int)s->camera.pos.x, (int)s->camera.pos.y, (int)s->camera.pos.z,
               (int)s->camera.pitch, (int)s->camera.roll, (int)s->camera.yaw,
               ac ? (int)ac->speed : 0, ac ? (int)ac->throttle : 0, ac ? ac->gear : 0, ac ? ac->hook : 0, ac ? ac->on_ground : 0,
               (int)s->hud.altitude_ft, (int)s->hud.heading_deg, (int)s->hud.airspeed_kt, (int)s->hud.fuel_pct, (int)s->hud.cp_hud_on,
               (unsigned)s->entry_count, hash,
               sh, (int)s->sky.side_flag, (int)s->sky.sea_zone, (unsigned)s->sky.sea_count, (int)s->sky.fade_target,
               game_crash_state(g, NULL), flash,
               (unsigned)s->text_count, s->text_count ? (unsigned)s->text[0].id : 0u, th, tr ? tr->part : 0, tr ? (int)tr->lesson : 0,
               nactive, en ? (unsigned)en->rec[0].stock : 0u, en ? (unsigned)en->rec[0].rounds : 0u, wp ? (unsigned)wp->flares : 0u, wp ? (unsigned)wp->chaff : 0u,
               (unsigned)s->hud.radar_count, (unsigned)s->hud.cp_threat, ac ? (unsigned)ac->f.v4a850 : 0u, (unsigned)game_debug_screen(g)->rng,
               (unsigned)s->audio_count, ah, (unsigned)s->audio_master_target, (unsigned)s->audio_master_set, (unsigned)s->audio_master_ramp);
    }
    free(cmds);
    free(s);
    game_destroy(g);
    return 0;
}
