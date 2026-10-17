# Milestone 4: Game flow (menus, selections, demo, training, qualification, flight log) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Everything the original shows and does outside combat: the credits, the log-disk prompt, the main menu, the free-flight location and aircraft selections, the demo replay, the seven training manoeuvres, carrier qualification, the flight log with name entry, the pause text, the ESC, mission-end and game-over screens, all with the original's text records, palette, fades and timing, plus `docs/manual.md` per mode and `docs/easter-eggs.md`.

**Architecture:** One reverse-engineering slice, note 08 (`docs/re/08-game-flow.md`), written in two halves that run concurrently: the front end (text records, console path, palette, screen state machine, mission start and end) and the play modes (demo replay, training, qualification, flight log, easter eggs). The core gains a screen module (`src/core/screen.*`) that owns the mode machine and fills the scene's text records, a replay module (`src/core/replay.*`) for the demo, a training module (`src/core/training.*`), a qualification and log module (`src/core/logbook.*`); the frontend gains a text renderer (`src/frontend/sdl/text.*`) that draws 8x8 glyphs through the menu palette and the fades, and a persistence hook for the log file. The extractor exports the text records, the menu palette, the scenario and training tables, the demo stream and the front end's own 5x7 font.

**Tech Stack:** as before: C99 core and frontend, SDL2, Python extractor, Musashi rig, vAmiga headless driver, rea/Ghidra through MCP.

**Spec:** `docs/superpowers/specs/2026-10-13-interceptor-port-design.md` (sections 5 (scene text and modes), 7 slice 6, 8, 10 (security check not reproduced; the flight log as a local file; both aircraft kept), 11 milestone 4, 12 player documentation). Prior plan and its ledger: `docs/superpowers/plans/2026-10-13-milestone-3-flight.md`, `.superpowers/sdd/2026-10-13-milestone-3-flight/progress.md` (the NOTE-GAPs marked "note 08" are binding input to Tasks 1 and 2).

## Global Constraints

- Clean room: notes are prose and tables; no disassembly, decompiler output, code or copied tables; test fixtures synthetic; nothing under `disks/`, `extracted/`, `assets/`, `manual-en/` or the codewheel files committed; captures described, never committed. Game text is never quoted in notes, tests or docs beyond the few words needed to identify a screen (spec section 12); the extractor carries the texts, the repository never does.
- Core: C99, fixed-width types, no stdio, no floating point, no OS calls; fixed point as the original; constants from the notes with section comments; port choices labelled NOTE-GAP with the open question.
- Determinism: a fixed simulation step at `FLIGHT_STEP_HZ` = 8 steps per second; screens that the original times in video blanks (50 Hz) are converted with the rounding rule of note 05 §17.17 (`(blanks * 8 + 25) / 50`), stated per screen in note 08; the frontend steps the core at that rate and renders at the display rate; the golden runner must stay byte-stable (integers only).
- The scene struct is the contract between core and frontends; changes to `scene.h` are made once, in Task 4 (the text record gains colour, a typed-out length and a cursor flag; the modes MENU, BRIEFING, LOG, DEBRIEF, CREDITS, LOGDISK, SELECT are driven; `splash_asset` stays); the display-state boundary of spec section 10 applies: the core owns every counter the original keeps, the frontend keeps draw caches only.
- Clean separation: the frontend never computes game state; the core never draws and never touches files: the flight log crosses the boundary as a byte blob through the loader/saver interface of Task 8.
- The security-check screen is skipped (spec section 10): the port goes from the menu's free-flight item straight to the location selection; note 09 stays the record of the original's behaviour.
- The front-end font (amended 2026-10-14 after note 08 section 3): the original does NOT draw through the console device or the ROM's Topaz font (note 02's assumption is withdrawn); it has its own text engine with a 5x7 glyph font in hunk 61 (`0x4283C`: a 64-word offset table for characters 0x20-0x5F, 54 distinct 7-byte glyphs) and addresses text by scanline and byte column in 7-pixel cells. The extractor writes `fonts/menu5x7.bin` (64 glyphs x 7 bytes, note 08 §3.2); no Kickstart ROM is needed, there is no `--rom` option and no fallback font.
- Note numbering, settled here: 07 AI and weapons, 08 game flow including every text screen, 10 sound (as `docs/re/README.md`); Task 1 fixes the stray "note 07" text-slice references in `src/core/game.c`, `src/core/scene.h`, notes 05/06 and `tools/extract/extract/sounds.py`.
- rea: open `extracted/interceptor-arm.elf` (procedure in `tools/hunk/ghidra_import.md`); apply the map symbols.
- Sandbox: `. /sandbox/.tools/env.sh` first; commit messages end with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`; explicit-path commits; other agents commit concurrently; commit after every item (the user asks for frequent commits); `make` before the frontend pytest (a stale binary fails it).

## Review Focus

1. A key is held down across a screen change (RETURN on the log-disk prompt into the menu, ESC out of the demo). Expected: one action per press, exactly as the original's key queue delivers it (note 08 states the queue depth and the repeat behaviour); never a skipped screen or a quit. Pinned in Task 4 (core queue test) and Task 5 (frontend repeat filter test).
2. Name entry for the flight log receives more characters than the record holds, a non-ASCII key, or BACKSPACE at an empty field. Expected: the original's rule (clipped, ignored or erased as note 08 states), never a write past the record. Pinned in Task 8.
3. The flight log file is missing, truncated, unwritable or from an older version. Expected: the game starts, the log screen shows the empty log, nothing crashes; a save failure is reported once by the frontend and the game goes on. Pinned in Task 8 (core blob round trip with bad input) and Task 5 (frontend save/load errors).
4. A text record addresses a scanline or byte column beyond the screen, or a character outside the font's 0x20-0x5F range (the glyph-half bytes 0x24/0x5E/0x26 included). Expected: the original's rule from note 08 §3.4 (clipped, skipped or the glyph-half composition), never a write outside the 320x200 page. Pinned in Tasks 3b (extractor refusal) and 5 (renderer test).
5. ESC or a digit arrives during a fade, during typed-out text or during the three-tick wait before flight. Expected: the original's rule per screen (queued, ignored or acted on immediately, as note 08 states with per-blank evidence); never a stuck black screen. Pinned in Task 4.

## Task dependency graph (for parallel execution)

```
Wave A (concurrent): Task 1 RE note 08 front end (texts, console, palette, state machine, mission start and end) · Task 2 RE note 08 play modes (demo, training, qualification, flight log, easter eggs) · Task 3a extractor: palette and scenario tables (no dependency) 
Wave B (after 1): Task 3b extractor: text records, ROM font · Task 4 core screen module · Task 5 frontend text renderer (can start its glyph layer from the font format while 4 is in progress)
Wave C (after 2, 4): Task 6 demo replay · Task 7 training · Task 8 qualification and flight log
Wave D: Task 9 goldens, manual, easter-eggs document, numbering, CI
```

---

### Task 1: RE slice, note 08 part A: the front end

**Files:**
- Create: `docs/re/08-game-flow.md` (sections 1-6: Location; Text records; Console path and font; Palette and fades; Screen state machine; Mission start and end; then Constants, Open questions, Verified by, per `docs/re/README.md`)
- Modify: `docs/re/README.md` (the slice table row for 08: "including every text screen"), `docs/re/02-assets-and-tables.md` (open question 7 closed; the text table row points at 08), `docs/re/05-flight-model.md` and `docs/re/06-cockpit-hud.md` ("note 07" text references become "note 08"), `docs/re/01a-reference-captures.md` (new capture rows)
- Create: `tools/vamiga-ref/scripts/flow-*.txt` (one script per screen: credits, log-disk prompt, main menu items 1-8 with ESC and SHIFT-ESC, location select, aircraft select, the three-tick wait, ESC from flight with every per-blank screen, game over, pause text, mission end), `tools/m68k-rig/tests/lists_model/text_check.py` (the text-record decoder of the note against the original's routine on every record)
- Modify: `tools/extract/extract/sounds.py:4` (the slice reference), `src/core/game.c:1008` and `src/core/scene.h:264` comments ("note 07" to "note 08"; comments only, no behaviour)

**Interfaces:**
- Produces, for Task 3b: the text-record format of hunk 64 (`0x43DF4`-`0x4621B`, 239 strings): the control bytes (position, colour, typed-out speed, clear-screen, newline), the record terminator, the id-to-record table behind `V:4A802` (ids 7, 0x62, 0x63, 0x66 and all others), which screen uses which records, stated as a table with record index, id, screen, row, column, colour, length in characters (no text).
- Produces, for Task 5: the text engine (note 08 section 3, as written): the record addressing by scanline and byte column in 7-pixel cells, the 5x7 font of hunk 61 (offset table, glyph bitmaps, the glyph-half bytes), the pass rate, the typed-out rule, the pens, the cursor and the input mode.
- Produces, for Task 4: the screen state machine from boot to flight and back: credits (any key), the log-disk prompt (what RETURN, ESC and other keys do; the port skips the disk question per spec section 10 and states it), the main menu (items 1-8 and what each starts; ESC and SHIFT-ESC lines), the security prompt (skipped: the port enters the location selection directly), the location selection (keys 1-4 and the writers of `V:4A900`: `0x1EBBA`, `0x121F8`, `0x12222`, `0x12102`, `0x12252`, `0x11C10`), the aircraft selection (`V:4A901` 0x11/0x10), mission start (`0x12EC0`, `0x1D1BE`, `0x2CE36`, the full initialisation list, the three-tick wait, the allowance by mode), the game-mode byte `V:4A95E` (every value 1-9, 0x7D, 0x7E, 0x7F with its meaning and writers `0x11A7E`, `0x1EB18`, `0x11C32`), the camera mode 5/6 chain (`0x12A92`, `0x12AC0`, `0x12AFC`, `0x12BD8`) and its key set, the fades (targets 0, 10, 15 at `0x1188C`, `0x13B64`, `0x11B1A`, `0x12DA4`, `0x12FF8`), the pause text, the ESC flow's screens with per-blank timing (states `0x11A96`, `0x11AE2`, `0x11E04`, `0x1201E` text 0x66, `0x1234C`; the gate pulse k 37-41 in flight and 25-29 from a pause), the game-over screen (text 0x62/0x63 typed out at k 130-160, leave at k 188, writers of `V:4A898` and `V:4AB8E`), the mission-end and debrief screens, the key queue (depth, repeat handling, which keys are consumed by which screen), and the mouse and joystick buttons as menu keys if the menus accept them.

- [ ] Read notes 00, 01a, 02 (sections 9, 12 and open question 7), 04 (lines 21-58, 428, 676), 05 (sections 15, 17.14, 17.17, open questions 5, 7, 18, 21), 06 (items 8 and 11, lines 63, 487-492, 530, 699, 724), 09, and the ledger's "note 08" NOTE-GAPs; list every address and variable they hand to note 08.
- [ ] Characterise the hunk-64 text records with rea and `text_check.py` (decode every record with the note's rule, compare with the original's decoder in the rig on all 239 records; count); write section 2 with the record table (no text).
- [ ] Trace the text engine: the draw routine, the glyph blit, the font tables of hunk 61, the pass rate while lines are drawn, the typed-out rule, the cursor; verify by rendering the menu, credits and log-disk captures pixel for pixel; write section 3.
- [ ] Export-check the menu palette `0x29F24` and the fade targets at every mode change with emulator `colors` logs; write section 4.
- [ ] Drive every screen in the emulator with the `flow-*` scripts (frames and per-blank peeks of `V:4A95E`, `V:4A900`, `V:4A901`, `V:4A802`, `V:4A84D`, `V:4A866`, `V:4A898`, `V:4AB8E`, `V:4A94F`, `V:4A966`, the key queue), read the state routines with rea, and write sections 5 and 6 with per-blank tables; state the key queue depth and repeat behaviour from a held-key capture.
- [ ] Fix the numbering references (README row, notes 02/05/06, sounds.py, game.c:1008, scene.h:264 comments) and record the new captures in note 01a.
- [ ] Commit per section; report in `.superpowers/sdd/2026-10-14-milestone-4-game-flow/task-1-report.md` (not git-added).

Acceptance: Task 3b can write the text and font exporters, Task 4 the screen machine and Task 5 the renderer from the note alone; every screen has a capture; every timing is per blank with its step conversion; every rule is tagged V, R or C with counts.

---

### Task 2: RE slice, note 08 part B: demo, training, qualification, flight log, easter eggs

**Files:**
- Create: sections 7-10 of `docs/re/08-game-flow.md` (Demo replay; Training; Qualification; Flight log and name entry) and section 11 (Hidden keys, jokes, cheats and unused content: the source list for `docs/easter-eggs.md`, each item with its trigger, no text quoted beyond identification)
- Create: `tools/vamiga-ref/scripts/flow-demo-*.txt`, `flow-training-*.txt`, `flow-qual-*.txt`, `flow-log-*.txt`; `tools/m68k-rig/tests/lists_model/replay_check.py` (the demo stream decoder of the note against the original's replay reader), `training_check.py` (the manoeuvre detection rules of the note against the original's checker over random flight states)
- Modify: `docs/re/05-flight-model.md` (open questions 5, 10, 12, 21, 22, 23 closed or pointed at 08), `docs/re/06-cockpit-hud.md` (open questions 1, 2, 6; the training posts rows), `docs/re/01a-reference-captures.md`

**Interfaces:**
- Produces, for Task 6: the demo replay: where the stream lives (hunk, address range, size), its record format (the key byte plus the two event words `V:4AA50`/`V:4AA52` per frame, or whatever it is), how `V:4A903` = 1, 3, 4 selects recording, replay and off, how the demo switches modes 0x7E and 0x7F and views, the carrier at heading 28600, ESC restarting the demo, what ends it, the frame rate the stream assumes (per blank or per step) and the port's conversion.
- Produces, for Task 7: the training mode (`V:4A95E` = 2): the lesson list and the seven manoeuvre records at `0x2749A` (fields), the script interpreter (prompts in the message window, the scroller `0x377B2`, posts 0x1D/0x1E, entries 29/30), the detection rules per manoeuvre (which state words, thresholds, time limits, the `V:4A83C`-`V:4A847` switches, `V:4A844` = 0), the RETURN re-position (`0x1E8A6`, `W2` 0x0800, `0x27276`: where the aircraft is placed), the HUD offsets `V:4A990`/`V:4A9D0`, the success and failure outcomes and their texts' ids, how a lesson is chosen and left.
- Produces, for Task 8: carrier qualification (which menu item, mode byte value, placement, the landing rules and the "landed" flag `V:4A98A`, scoring, the number of attempts, ditching, the debrief text ids); the flight log: the player record (78 bytes: fields), the `config` file layout and defaults, the log-disk handling the port replaces by a local file, name entry (keys accepted, length, BACKSPACE, RETURN, the cursor), the log screen's layout (rows, columns, colours per record), mission elapsed seconds (`0x11748`), the crash statistic `V:4A963`/`V:4A87D`, when records are written.
- Produces, for Task 9: section 11, the easter-egg catalogue: SHIFT-ESC, hidden keys, the "HEY ROOKIE" message's trigger and the other jokes, cheats, unused texts and records, each with the exact trigger in the original and the capture that shows it.

- [ ] Demo: find the stream and the recorder/replayer (`0x54E4C` buffers, `V:4A903`), decode the stream with `replay_check.py` against the original (every frame of the shipped demo; count), capture the demo's mode and view switches per blank; write section 7.
- [ ] Training: drive each of the seven lessons in the emulator to success and to failure (scripts), read the checker with rea, write `training_check.py` (random states through the note's rules vs the original; count), write section 8 with one table per manoeuvre.
- [ ] Qualification: drive a carrier qualification to a landing, a bolter and a ditch; read the scorer; write section 9.
- [ ] Flight log: read the record writer and the name-entry loop; drive name entry with overlong and non-letter input; write section 10 including the `config` layout.
- [ ] Easter eggs: sweep the key handlers of every screen for hidden keys, list every unreferenced or unreachable text record, find the crash-message trigger; write section 11.
- [ ] Close or redirect the open questions in notes 05/06; record captures in 01a; commit per section; report in `task-2-report.md` (not git-added).

Acceptance: Tasks 6, 7, 8 and 9 can implement the demo, every lesson, qualification, the log and the easter-eggs document from the note alone; each rule carries a rig or emulator count.

---

### Task 3: Extractor: palettes, scenario and training tables, text records, demo stream, menu font

**Files:**
- Create: `tools/extract/extract/flow.py` (the writers below), `tools/extract/tests/test_flow.py`
- Modify: `tools/extract/extract/cli.py` (the new writers called from the staging step), `tools/extract/extract/manifest.py` (`EXTRACTOR_VERSION = 13`), `tools/extract/README.md` (one format row per new file), `src/core/assets.h` (`CORE_ASSETS_VERSION 13`, in the same commit as the manifest bump), `src/core/assets.c` or the module that loads tables (the readers of the new files, same commit)

**Interfaces:**
- Produces (little-endian, formats defined by note 08 and note 02):
  - `palette/menu.pal`: 32 entries of 3 bytes (R, G, B scaled 0-255 from the 4-bit words at `0x29F24`); `palette/flash.pal` likewise from `0x29F64`.
  - `tables/text.bin` and `tables/text_ids.bin`: the hunk-64 records and the id table re-encoded exactly as note 08 §2.3 defines (row = scanline, col = byte column, pen, flags, the text bytes verbatim including the glyph-half bytes).
  - `tables/scenario.bin`: the start blocks `0x47ADE` (per block the fields note 05 §17.14 names), the placement table `0x2DEDA` with its sub-lists and the mode-0x7D data of `0x2CFDA`, the spawn regions `0x2DE28`; each sub-table prefixed by `u16 count, u16 record_size`.
  - `tables/training.bin`: the records at `0x2749A` (8 bytes each, count from the note) and any lesson script data note 08 section 8 names.
  - `tables/demo.bin`: the replay stream verbatim with a `u32 frame_count` header, in the record format of note 08 section 7.
  - `fonts/menu5x7.bin`: 64 glyphs x 7 bytes in character order 0x20-0x5F, rows top to bottom, pixels in bits 7-3 (note 08 §3.2).
- Core readers (same commit as the version bump): `assets_load_text(table, ids)`, `assets_load_menu_palette`, `assets_load_scenario`, `assets_load_training`, `assets_load_demo`; the font is read by the frontend only (`src/frontend/sdl/assets.c`), the core never needs the glyphs.

- [ ] 3a (no dependency): write `palette/menu.pal` and `palette/flash.pal` and `tables/scenario.bin` from notes 02 and 05 §17.14/17.17 rule 5; tests: synthetic image bytes through the writers give the expected little-endian files; real-asset test checks sizes and record counts only. Commit with the core readers and the version bump 13 in one commit.
- [ ] 3b (after Task 1 sections 2-3): write `tables/text.bin` and `tables/text_ids.bin`; tests: a synthetic record set round-trips; the real-asset test checks count 239, every row < 25, every col < 40, every byte printable or a control the note allows (no text literals in the test). Commit.
- [ ] 3b: write `fonts/menu5x7.bin` from hunk 61 (`0x4283C` offset table, glyph bitmaps `0x428BC`-`0x42A36`); test: a synthetic offset table and glyph set round-trip; the real-asset test checks 448 bytes and that every offset lands inside the bitmap span (no glyph literals). Commit.
- [ ] 3b (after Task 2 sections 7-8): write `tables/training.bin` and `tables/demo.bin`; structural tests. Commit.
- [ ] Update `tools/extract/README.md` rows. Commit.

---

### Task 4: Core screen module: modes, text records, key queue, mission start and end

**Files:**
- Create: `src/core/screen.c`, `src/core/screen.h`, `tests/test_screen.c`
- Modify: `src/core/scene.h` (once: `game_text_t` becomes note 08's record: `uint8_t row` is the scanline (0-199), `uint8_t col` the byte column, plus `uint8_t pen, flags; uint16_t shown;` (shown = characters typed out so far); new modes `GAME_MODE_CREDITS, GAME_MODE_LOGDISK, GAME_MODE_SELECT_LOCATION, GAME_MODE_SELECT_AIRCRAFT` beside the existing MENU, BRIEFING, LOG, DEBRIEF; `uint8_t fade_target` (0, 10, 15 per note 04:428/08); `uint8_t cursor_row, cursor_col, cursor_on`), `src/core/game.c` (the splash RETURN path is replaced by the screen machine; `game_set_start` and `FLIGHT_FA18`-only placement go; `leave_flight` enters the screen the note names instead of SPLASH; the ESC flow shows its screens; the pause text; the game-over text typed out at the note's rate), `src/core/game.h` (`game_select_t { uint8_t location; uint8_t aircraft; }` read by `game_scene`; `GAME_BTN_SHIFT_ESC`; the digit keys as `GAME_EV_KEY` with ASCII values), `tests/test_game.c`, `tests/test_crash.c` (the splash-return expectations become the note's screens)

**Interfaces:**
- Consumes: Task 3's `assets_load_text/ids/menu_palette/scenario`; note 08 sections 2, 5, 6.
- Produces: `screen_t` inside `game_t`; `void screen_key(game_t *g, const game_event_t *ev)` (every key outside flight goes here; the queue of the note's depth; one action per press); `void screen_step(game_t *g)` (one step: typed-out text advances, fades count, timers run, the three-tick wait before flight, the ESC/mission-end/game-over sequences in steps per the note's blank-to-step rule); `void screen_fill(const game_t *g, game_scene_t *out)` (text records with row/col/colour/shown, the fade target, the cursor); `int screen_start_flight(game_t *g)` (the mission-start initialisation list of the note: mode byte, allowance, placement by `V:4A900`/`V:4A901` through the scenario table, `cockpit_mission_start`, sky and strain reset, view reset, the three-tick wait); the mode byte `V:4A95E` as `g->mode_byte` with the note's values; `GAME_MODE_*` transitions exactly as the note's state table.

- [ ] Write `tests/test_screen.c` first: boot shows CREDITS with the note's records (ids only, no text compare); any key goes to LOGDISK; RETURN to MENU; `2` to SELECT_LOCATION (the security prompt skipped: assert directly); `1`..`4` then `1`/`2` start a flight with the scenario table's placement for each pair (positions from the note's table) and the F-16 class for key 2; the three-tick wait; ESC in the menu and SHIFT-ESC per the note; a held key delivers one action (queue test, Review Focus 1); ESC during a fade and during typed-out text per the note (Review Focus 5); game over types out text 0x62/0x63 at the note's rate then shows the note's next screen; ESC from flight shows the note's screens at the note's step counts then the menu.
- [ ] Implement `screen.c` to the tests; replace game.c's splash RETURN path and `leave_flight`; wire `screen_step` into `game_step` for the non-flight modes and `screen_fill` into `game_scene`; keep `--flight` working in main.c by having the frontend send the key sequence the note needs (RETURN, `2`, location, aircraft) on the first frames (Task 5 wires it).
- [ ] Update `tests/test_game.c` and `tests/test_crash.c` expectations to the note's screens (not weakened: the step counts come from the note).
- [ ] Run `make CC=tcc core test && make test && make golden` (the goldens change: the start sequence now goes through the screens; regenerate and explain); commit per step.

---

### Task 5: Frontend: text renderer, menu palette and fades, key map, log persistence

**Files:**
- Create: `src/frontend/sdl/text.c`, `src/frontend/sdl/text.h`, `src/frontend/sdl/logfile.c`, `src/frontend/sdl/logfile.h`, `tests/frontend/test_text.py`, `tests/frontend/text_driver.c`
- Modify: `src/frontend/sdl/video.c` (modes CREDITS, LOGDISK, MENU, SELECT_*, BRIEFING, LOG, DEBRIEF, GAMEOVER and the pause text draw the scene's text records through `text_draw` on the 320x200 pen layer with the menu palette `palette/menu.pal`; the fade counter follows `scene.fade_target` through the existing fade.c with the menu palette as the source; the cursor), `src/frontend/sdl/main.c` (key map: digits and letters as `GAME_EV_KEY`, SHIFT-ESC, BACKSPACE in name entry; the repeat filter applies to every key; ESC quits only on the credits screen; `--flight` sends the note's start sequence; the font comes from `assets/fonts/menu5x7.bin`), `Makefile` (new sources, `text_driver`)

**Interfaces:**
- Consumes: `game_scene_t.text[]` with row/col/colour/shown, `fade_target`, cursor fields (Task 4); `assets/fonts/menu5x7.bin` and `palette/menu.pal` (Task 3).
- Produces: `void text_draw(raster_t *r, const game_text_t *t, const uint8_t *font)` (note 08 §3.2/3.4: 5x7 glyphs in 7-pixel cells at (col*8 + the engine's offset, scanline row), set pixels only in the record's pen, the glyph-half composition for 0x24/0x5E/0x26, only the first `shown` characters); `int logfile_load(const char *path, uint8_t *buf, size_t cap)` and `int logfile_save(const char *path, const uint8_t *buf, size_t len)` (the path from `$XDG_CONFIG_HOME/interceptor/flightlog.bin` or `~/.config/...`; errors returned, printed once by main.c; never fatal); `text_driver` (reads a script of records, dumps the pen layer) for the pytest.

- [ ] `tests/frontend/test_text.py` first: a record at (scanline, byte column) with pen p sets exactly the glyph's pixels to p and leaves the rest untouched (synthetic 5x7 font with known glyphs); `shown` limits the characters; a record reaching past the page edge is clipped per note 08 §3.4 (Review Focus 4); the menu capture `menu.ppm`, `freeflight-select.ppm`, `aircraft-select.ppm`, `credits.ppm` and `log-disk-prompt.ppm` compared pixel for pixel with the real font when `assets/fonts/topaz8.bin` exists (skipped otherwise); a held RETURN produces one screen change (Review Focus 1); `logfile_save` to an unwritable path returns an error and the game continues (Review Focus 3).
- [ ] Implement `text.c`, `logfile.c`, the video.c modes and fades, the main.c key map; `make && pytest tests/frontend -q`; commit per file group.

---

### Task 6: Core demo replay (from note 08 section 7)

**Files:**
- Create: `src/core/replay.c`, `src/core/replay.h`, `tests/test_replay.c`, `tests/golden/demo.txt` (a scripted run: menu item 1, N ticks of demo)
- Modify: `src/core/game.c` (menu item 1 starts the demo: mode 0x7E/0x7F per the note, the stream feeds `flight_event`/the key byte each step, ESC restarts, the end returns to the note's screen), `src/core/screen.c` (the demo entry and exit)

**Interfaces:**
- Consumes: `assets_load_demo` (Task 3), note 08 section 7.
- Produces: `void replay_start(game_t *g)`, `int replay_step(game_t *g)` (feeds the frame's key byte and event words; returns 0 at the end), `void replay_stop(game_t *g)`; the frame-to-step conversion the note states (NOTE-GAP if the stream is per blank and the port runs 8 steps per second: state the rule chosen from the note).

- [ ] `tests/test_replay.c` first: a synthetic stream of known frames drives the key byte and the event words into the flight in order; the end stops the demo and enters the note's screen; ESC restarts at frame 0; the mode and view switches at the frames the note gives.
- [ ] Implement; compare the demo frames with the captures `demo-deck`, `demo-external`, `demo-cockpit` through the frontend test (pixel compare at the capture's frame index, with the note's conversion); `make golden` gains the demo golden (synthetic assets: the stream is absent, so the synthetic run asserts the mode sequence only); commit.

---

### Task 7: Core training (from note 08 section 8)

**Files:**
- Create: `src/core/training.c`, `src/core/training.h`, `tests/test_training.c`, `tests/rig/test_training_diff.c` (the detection rules against the original's checker over random flight states, like `test_flight_diff.c`)
- Modify: `src/core/game.c` (mode byte 2; the lesson prompt in the message window through the cockpit's message counter; RETURN re-positions; the HUD offsets; success and failure outcomes), `src/core/cockpit.c` (the training posts 0x1D/0x1E and the scroller rows per note 06 §580-585 as the note settles them)

**Interfaces:**
- Consumes: `assets_load_training` (Task 3), note 08 section 8, note 06's training posts.
- Produces: `void training_start(game_t *g, int lesson)`, `void training_step(game_t *g)` (the detection of the current manoeuvre from the aircraft state words the note names, the time limit, the prompts, the outcome), `int training_result(const game_t *g)`; the RETURN re-position through `flight_restart`-like placement at the note's point.

- [ ] `tests/test_training.c` first, one test per manoeuvre with the note's state sequence (a synthetic flight path that satisfies the rule and one that fails it), the prompt ids, the re-position, the outcome texts' ids.
- [ ] Implement; `make -C tools/m68k-rig test-training` (0 differences over the note's count); commit per manoeuvre group.

---

### Task 8: Core qualification and flight log (from note 08 sections 9-10)

**Files:**
- Create: `src/core/logbook.c`, `src/core/logbook.h`, `tests/test_logbook.c`
- Modify: `src/core/game.c` (the qualification mode: placement, landing rules, scoring, the debrief screen; the log screen; name entry through `GAME_EV_KEY`), `src/core/game.h` (`int game_log_export(const game_t *g, uint8_t *buf, size_t cap)`, `int game_log_import(game_t *g, const uint8_t *buf, size_t len)`: the blob is the note's record layout with a 4-byte magic and version), `src/frontend/sdl/main.c` (load at start, save when the core reports a change through `scene.log_dirty`)

**Interfaces:**
- Consumes: note 08 sections 9-10; `logfile_load/save` (Task 5).
- Produces: `logbook_t` with the note's player records; `logbook_add(g, record)`; `scene.log_dirty` set when a record is written; name entry: `GAME_EV_KEY` letters appended up to the record's length, BACKSPACE erases, RETURN confirms, every other key per the note (Review Focus 2); import of a short, long, wrong-magic or wrong-version blob returns an error code and leaves an empty log (Review Focus 3).

- [ ] `tests/test_logbook.c` first: a qualification landing scores per the note's table; a bolter and a ditch per the note; the record written has the note's fields; export then import round-trips; the four bad blobs are refused and the log stays empty; name entry clips at the note's length, ignores a non-letter, BACKSPACE at an empty field does nothing.
- [ ] Implement; the log screen's text records through `screen_fill`; commit per step.

---

### Task 9: Goldens, manual, easter-eggs document, numbering, CI

**Files:**
- Modify: `tests/golden/takeoff-and-circuit.txt` (the start now goes through the screens: menu, free flight, location, aircraft; synthetic golden regenerated; real golden regenerated locally), `tests/golden/run_golden.c` (the screen mode and the first text record id per tick in the output), `docs/manual.md` (sections per mode: start screens, menu, free flight selection, demo, training lessons, qualification, flight log; the controls table per mode; what is not yet in: missions and combat (milestone 5), sound (milestone 6)), `README.md`, `CLAUDE.md` (the `--rom` option; new make targets), `.github/workflows/ci.yml` (nothing new runs without assets: confirm; the timeouts stay)
- Create: `docs/easter-eggs.md` (from note 08 section 11: each item, its trigger in the original and in the port, no text beyond identification)

- [ ] Goldens regenerated and explained; manual and easter-eggs written from the notes and from playing the port; README status; CI green on the three platforms; commit.

---

## Self-review notes

- Spec coverage: section 5 text and modes (Tasks 4, 5); section 10 security skipped (Task 4), log as a local file (Tasks 5, 8), both aircraft (Task 4); section 11 milestone 4 items: menus (4, 5), aircraft select (4), training (7), qualification (8), flight log (8), demo (6), manual and easter-egg documents (9). Mission briefings' content and the six missions are milestone 5; their screens' plumbing is Task 4.
- Review Focus 1-5 each pinned in the owning task's first step.
- Type names used across tasks: `game_text_t` fields `row, col, colour, flags, shown`; `fade_target`; `screen_key/step/fill/start_flight`; `replay_start/step/stop`; `training_start/step/result`; `game_log_export/import`; `logfile_load/save`; `text_draw`; `assets_load_*` as listed in Task 3.
