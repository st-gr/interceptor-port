# Milestone 3: Flight (flight model, cockpit, HUD, takeoff and landing) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Free flight complete: the player's aircraft (F/A-18 by default, F-16 selectable by flag) takes off from the carrier catapult or the airfield runway, flies with the original's flight model, shows the original's cockpit instruments and HUD, and lands (arrestor hook or runway), with the flight model checked against the original code in the rig and the HUD compared against emulator captures.

**Architecture:** Two reverse-engineering slices (flight model; cockpit and HUD) produce prose notes. The core gains an aircraft state and a flight module (`src/core/flight.*`) that owns position and attitude and feeds the world module through `world_set_camera`; the free camera from milestone 2 remains as a debug mode. The core owns the view kind and head angle (driven by new view-key events) and writes every HUD field. The SDL frontend renders HUD symbology and instruments as resolution-independent vectors over the world, the panel bitmap, and the real ground polygon with the altitude-band palette. An opener capture campaign settles the view-distance scale, handedness and palette questions left by milestone 2 before the port modules depend on them.

**Tech Stack:** as before: C99 core and frontend, SDL2, Python extractor, Musashi rig, vAmiga headless driver (gains a `peek` command to log the game's memory), rea/Ghidra through MCP.

**Spec:** `docs/superpowers/specs/2026-10-13-interceptor-port-design.md` (sections 5, 7 slices 3 and 4, 8, 9, 11 milestone 3). Prior plans and follow-ups: `docs/superpowers/plans/2026-10-13-milestone-2-world.md`, `docs/superpowers/plans/2026-10-13-milestone-2-followups.md` (the openers section is binding input to Task 1 and Task 2).

## Global Constraints

- Clean room: notes are prose and tables; no disassembly, decompiler output, code or copied tables; test fixtures synthetic; nothing under `disks/`, `extracted/`, `assets/`, `manual-en/` or the codewheel files committed; captures described, never committed.
- Core: C99, fixed-width types, no stdio, no floating point, no OS calls; fixed point as the original; constants from the notes with section comments; port choices labelled.
- Determinism: a fixed simulation step at `FLIGHT_STEP_HZ` = 8 steps per second (the original runs one step per displayed frame at a render-cost-dependent rate, measured flight-only mean 7.97 in note 01b; the 15 Hz limiter is never reached); the frontend steps the core at that rate and renders at the display rate; the golden runner must stay byte-stable (integers only).
- The scene struct is the contract between core and frontends; changes to `scene.h` are made once, in Task 2, with the follow-ups' requirements (amended 2026-10-13: Task 7 added the raw cockpit block, radar list and message counter; Task 10 replaces the objects/terrain split with the original's single draw order; the display-state boundary of spec section 10 applies: the core owns the arbiter, tick generator, code word, latch and frame counter).
- Clean separation: the frontend never computes flight state; the core never draws.
- rea: open `extracted/interceptor-arm.elf` (procedure in `tools/hunk/ghidra_import.md`); apply the map symbols.
- Sandbox: `. /sandbox/.tools/env.sh` first; commit messages end with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`; explicit-path commits; other agents commit concurrently.

## Review Focus

1. The aircraft is on the deck with the throttle at idle and the player holds the stick fully back. Expected: nothing happens until the catapult fires or the speed is sufficient; no negative airspeed, no sinking through the deck. Pinned in Task 5.
2. The player flies below sea level or into terrain. Expected: the original's crash rule (crash message, reset), never a continuing flight underground. Pinned in Task 6.
3. Extreme attitudes (vertical climb, inverted, roll through 360). Expected: angles wrap in the 28800 unit without overflow; the HUD pitch ladder and heading remain consistent. Pinned in Tasks 5 and 7.
4. The frontend window is resized or a different scale is chosen. Expected: HUD symbology scales with the world viewport and stays aligned with the horizon. Pinned in Task 7.
5. A capture-derived constant turns out wrong (range unit, handedness). Expected: one switch each (`X_FLIP`, the range unit) changes the behaviour without touching other code, and the notes say which capture settled it. Pinned in Task 1.

## Task dependency graph (for parallel execution)

```
Wave A (concurrent): Task 1 capture campaign (peek command, openers) · Task 2 scene contract and aircraft state · Task 3 RE flight model · Task 4 RE cockpit and HUD
Wave B (after 2, 3): Task 5 core flight module (from note 05) · (after 2, 4) Task 7 HUD/instrument rendering can start its vector layer from note 06
Wave C: Task 6 takeoff, landing, crash rules (after 5) · Task 7 integration (after 5, 6) · Task 8 ground polygon and palette (after 1; frontend-only, concurrent with 5-7)
Wave D: Task 9 golden, manual, CI, comparison
```

---

### Task 1: Capture campaign for the milestone-2 openers

**Files:**
- Modify: `tools/vamiga-ref/ref.cpp` (new script command `peek <addr> <len> [label]`: reads chip/slow RAM through `mem.debugger.spypeek16`/`spypeek8` and appends `label addr hex...` to a log file given by `--log`; and `seglist` which prints the load addresses of the game's hunks so flat-image addresses can be mapped)
- Create: `tools/vamiga-ref/scripts/openers-*.txt`
- Create: `docs/re/01b-flight-captures.md`
- Modify: `docs/re/02-assets-and-tables.md`, `docs/re/03-3d-pipeline.md`, `docs/re/04-scene-composition.md` (close or update the open questions the captures settle: note 02 open question 6/8, note 03 open question 3, note 04 open questions 2, 6, 10)

**Interfaces:**
- Produces: the mapping from flat-image addresses to the running game's addresses (hunk 71 base in the emulator; method documented), and measured answers to: the view-distance scale and range unit (log `V:4AB30`, `V:4AA6A`, the carrier's record position and the player's position at two known distances); handedness and roll/yaw signs (a roll-right command from level flight with before/after frames and the attitude words at record `+0x66..+0x6A`); the altitude-band palette and sky flag (log the colour table at `0x54F18` and the palette registers at two altitudes); the ground polygon corners; HUD reference frames (level flight, 20 degree climb, 30 degree bank, on the deck, landing approach with the ILS, each with the state words logged). Task 5 and Task 8 read the measured values from the note.
- [ ] Implement `peek`, `--log` and `seglist` in `ref.cpp` (check `MemoryDebuggerAPI` in `/sandbox/vamiga/Core/VAmiga.h` for the accessor enum; the seglist can be found by scanning memory for the first 16 bytes of hunk 0 of the flat image, or by following the DOS process's `pr_SegList`; document the method that worked).
- [ ] Write the scripts (restore the airborne snapshot, apply a command, `peek` the state block, `frame`), run them, convert frames, read the logs.
- [ ] Write the note with a table per question: capture, logged values, derived answer, confidence. Update the three notes' open questions with one sentence each pointing at this note.
- [ ] Commit (scripts, ref.cpp, README, the four notes).

Acceptance: the note states, with evidence, the range unit in position units, whether the nearest-level band assumption holds for the carrier at two distances, the roll-right sign convention, and the sky/ground/altitude-band palette rule.

---

### Task 2: Scene contract, view kind in the core, aircraft state separated from the camera

**Files:**
- Modify: `src/core/scene.h`, `src/core/game.h`, `src/core/game.c`, `src/core/world.h`, `src/core/world.c`
- Create: `src/core/aircraft.h` (the aircraft state struct shared by flight, cockpit and world)
- Modify: `tests/test_game.c`, `tests/test_world.c`, `tests/golden/run_golden.c`, `tests/golden/synthetic-200.golden`, `src/frontend/sdl/video.c`, `src/frontend/sdl/main.c`

**Interfaces:**
- Produces, in `game.h`: new events `GAME_BTN_VIEW_OUTSIDE` (KPENTER toggle), `GAME_BTN_VIEW_ANGLE` with value 0..9 and 10 for KPDOT (external ring angle, note 04), `GAME_BTN_HEAD` with value for KP4/6/2/1/7/8 (cockpit head direction), `GAME_BTN_TOWER` ('M'), `GAME_BTN_BRAKE`, `GAME_BTN_PAUSE`; `GAME_AXIS_THROTTLE` semantics documented (steps per note 05 once known; F10/F9 map to step events `GAME_BTN_THROTTLE_UP/DOWN`).
- In `scene.h`: `game_camera_t.view` and a new `head` field written by the core; `game_object_t` attitude is honoured (document the angle roles per note 03 once Task 1 settles the signs); the HUD struct unchanged in layout but every field is now written by the core (flight module in Task 5; until then zero).
- In `aircraft.h`: `typedef struct { int32_t pos[3]; int32_t pitch, roll, yaw; int32_t speed; int32_t throttle; uint8_t gear, hook, on_ground, on_carrier; ... } aircraft_t;` with units documented (position units, 28800 angle unit, speed in the original's unit once note 05 names it; placeholders carry a comment naming the note section that will fix them).
- In `world.h`: `world_step` becomes `world_freecam_step` (debug mode); `world_set_camera` is the only way the aircraft state reaches the world; a helper `world_camera_for_view(const aircraft_t *, view, head, ring index, world_camera_t *out)` places the camera per note 04's view rules (cockpit at the pilot's eye, external ring from the camera-ring tables, tower, chase).
- `game.c`: flight mode holds an `aircraft_t`; a `--freecam` style flag path keeps the milestone-2 camera available (`game_set_debug_freecam(game_t *, int)`).
- [ ] Failing tests: view events change `scene.camera.view`/`head`; the external ring index 4 (chase) places the camera behind the aircraft by the table's offset; freecam still works when enabled; golden regenerated (synthetic) and the real golden regenerated locally.
- [ ] Implement; keep the frontend compiling with its view overrides removed (the frontend now only maps keys to the new events).
- [ ] Commit.

---

### Task 3: RE slice: flight model (`docs/re/05-flight-model.md`)

**Files:**
- Create: `docs/re/05-flight-model.md`

**Interfaces:**
- Produces: the per-step simulation routine(s) (addresses, call order from the main loop in note 04), the aircraft state block layout in work RAM (hunks 71-73: position, attitude, speed, throttle, fuel, gear, hook, flags; addresses and units), the control inputs (stick, throttle steps, brakes, rudder if any), and the rules in prose and formulas: thrust per throttle setting and aircraft type (F/A-18 vs F-16 tables), drag, lift and the stall rule, pitch/roll/yaw rates and their dependence on speed, gravity and vertical speed, the altitude-to-feet and speed-to-knots conversions (position units per foot settles note 02 open question 6), fuel burn, gear and hook effects, ground contact (deck and runway height, friction, braking), the catapult launch, the arrestor-wire catch rule, crash conditions (sea, terrain, overspeed with gear, structural limits), the "HEY ROOKIE" sequence trigger and the damage flags that milestone 5 will extend. Worked examples: the state after N steps from a documented initial state with fixed inputs, run in the rig (the step routine entry and its state block are the register/memory convention), three cases (deck at idle, level flight at cruise, a pull-up).
- [ ] Locate the step routine via note 04's per-frame list and the throttle/stick readers; characterise; run in the rig with a memory image of the state block (document how the state block is initialised for the rig).
- [ ] Write the note with the required sections; mark verified vs read-only.
- [ ] Commit.

Acceptance: Task 5 can implement `flight_step(aircraft_t *, const inputs *)` from the note and reproduce the three worked examples bit-exactly in the rig differential test.

---

### Task 4: RE slice: cockpit and HUD (`docs/re/06-cockpit-hud.md`)

**Files:**
- Create: `docs/re/06-cockpit-hud.md`

**Interfaces:**
- Produces: the HUD drawing routine(s) and the data they read; each HUD element with its geometry in the 320x180 projection space or screen space: pitch ladder (spacing, labels, range), heading tape or readout, airspeed and altitude readouts (format, units, font from hunk 61), the velocity vector or boresight, the ILS cues, the target box and lock (data only; drawn in milestone 5), the vertical-speed and G readouts; the warning line (which message when: the 53-string table of hunk 59 is indexed how); the instrument panel: which panel bitmap regions are instruments (airspeed, altitude, heading, attitude ball, fuel, throttle, gear/hook lights, radar scope) and how each is drawn (needle angles, tape scroll, digit boxes) over `pix/inst5`; the head views (KP4/6/2/1/7/8: what the cockpit view shows per direction, which parts of the panel remain), the external view's text strip; colours (palette entries). Worked examples: the HUD numbers shown in Task 1's reference frames versus the logged state words.
- [ ] Locate the HUD and panel routines (string table references, the hunk 61 font readers, the `_LoadRGB4`/panel bitmap users), characterise, cross-check against Task 1's frames.
- [ ] Write the note; commit.

Acceptance: Task 7 can draw every HUD element from the note and match the reference frames' layout within a pixel or two at 320x180 before scaling.

---

### Task 5: Core flight module (from note 05)

**Files:**
- Create: `src/core/flight.h`, `src/core/flight.c`
- Modify: `src/core/game.c` (flight mode steps the aircraft, then `world_set_camera` via `world_camera_for_view`, then fills the scene; HUD fields written from the aircraft and flight state)
- Create: `tests/test_flight.c`, `tests/rig/test_flight_diff.c`

**Interfaces:**
- Produces: `void flight_init(aircraft_t *, flight_type, start)` (start = carrier catapult or runway from the position blocks), `void flight_step(aircraft_t *, const flight_inputs_t *)` (one fixed step), `void flight_hud(const aircraft_t *, game_hud_t *)` (airspeed, altitude, heading, vspeed, pitch/roll degrees, throttle, fuel, gear, hook, warning text selection index), all fixed point per note 05 with section comments.
- [ ] Unit tests from note 05's worked examples (bit-exact) and Review Focus 1 and 3; differential test running the original step routine in the rig over 200 random input sequences from documented initial states, comparing the state block fields the note maps.
- [ ] Implement; `make core test`, rig suite; commit.

---

### Task 6: Takeoff, landing and crash rules (from note 05)

**Files:**
- Modify: `src/core/flight.c`, `src/core/game.c` (crash → a `GAME_MODE_DEBRIEF` with the crash text index, RETURN restarts on the deck; milestone 4 replaces this with the real menu flow), `tests/test_flight.c`

**Interfaces:**
- Produces: catapult launch sequence, arrestor catch, runway contact and braking, crash detection (Review Focus 2), the "re-position on catapult" (ESC) behaviour of the qualification screen as far as note 05 documents it.
- [ ] Tests: deck idle stays put; catapult launch reaches flying speed; landing within the wire zone with hook down stops the aircraft; sea-level crash resets; terrain crash resets.
- [ ] Implement; commit.

---

### Task 7: HUD and instrument rendering, head views (from note 06)

**Files:**
- Create: `src/frontend/sdl/hud.h`, `src/frontend/sdl/hud.c` (vector HUD drawn into an RGBA overlay at window resolution, aligned to the world viewport; uses the extracted HUD font or vector glyphs per the spec's resolution-independent rule)
- Create: `src/frontend/sdl/panel.h`, `src/frontend/sdl/panel.c` (instruments over the panel bitmap: needles, tapes, digits, lights)
- Modify: `src/frontend/sdl/video.c`, `src/frontend/sdl/main.c` (key map for the new events)
- Create: `tests/frontend/test_hud.py` (dump-based: the pitch ladder at known attitudes, the horizon alignment at two window scales: Review Focus 4)

- [ ] Implement from note 06 against the scene's HUD fields; compare with Task 1's reference frames (describe differences in note 06 open questions).
- [ ] Commit.

---

### Task 8: Ground polygon, altitude-band palette and sky flag (from Task 1 and note 04)

**Files:**
- Modify: `src/core/world.c`/`world.h` (export the ground polygon corners per note 04 "ground polygon" so the frontend draws the real one), `src/frontend/sdl/raster.c`/`video.c` (replace `raster_sky_ground` with the rotated ground polygon through `fx_clip`; apply the palette band from the extracted fade table per the rule Task 1 measured), `tests/frontend/test_raster.py`

- [ ] Tests: horizon row at level flight equals the capture's; the palette at two altitudes equals the measured registers.
- [ ] Implement; commit.

---

### Task 9: Golden, manual, CI, comparison

**Files:**
- Modify: `tests/golden/*` (a takeoff-and-circuit script of 600 ticks replaces the free-camera script; synthetic golden regenerated; real golden regenerated locally), `docs/manual.md` (free flight section: controls, takeoff, landing, views), `README.md`, `CLAUDE.md`, `.github/workflows/ci.yml` if needed

- [ ] Golden runner prints aircraft state and HUD values per tick; CI runs it; manual written from the implemented keys and note 05/06 rules in the author's words; final verification pass; commit.

---

### Task 10 (added 2026-10-13 after the capture campaign): display-list builder replaces the range window

The capture note 01b showed that the original's "visibility range" value (`V:4AA6A`) is dead: the walker compares against it but never branches on the result. The world module's range window (`world_range`, the square window, the altitude-to-range table) is therefore not the original's behaviour.

**Files:** create `docs/re/04a-display-lists.md` (RE: the list builder at `0x26AF8`, the change detector at h8+0x376 with its three triggers, the near and far lists' record format, which cells and items enter each list and in what order, the per-frame walk, the entity pass); modify `src/core/world.c`/`world.h` (replace the range window with the original's cell-list refresh and ordering; keep `range.bin` extracted because the original reads the table; delete `world_range`'s use for culling), `tests/test_world.c`, the golden files.

**Interfaces:** the scene's `objects` and `terrain` arrays are filled from the original's lists in the original's order; `GAME_MAX_OBJECTS`/`GAME_MAX_TERRAIN` are raised to the lists' real capacities as the note states them.

**Acceptance:** the note gives the builder's rules with rig-verified worked examples (cell tables around a documented position); `world_dump` at the carrier reproduces the original's list contents logged by the capture driver (`peek` of the list buffers) for the same position.
