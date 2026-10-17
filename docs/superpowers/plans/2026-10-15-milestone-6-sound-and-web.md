# Milestone 6: Sound, the browser build, and the milestone-5 follow-ups Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The port plays the original's sound (the engine, wind and afterburner of the viewed record, the gear, the ground contacts, the gun, the cues and HUD beeps, the key clicks, the start-up and mission phrases, the crash and ejection sounds, the launch) with the original's rules, runs in the browser from the player's own disk image with no asset hosted, and closes the carried follow-ups of milestones 4 and 5 that reverse engineering or a capture can settle.

**Architecture:** One reverse-engineering slice writes note 10 (`docs/re/10-sound.md`: the sound library, the 37 sound objects and their loader, the synthesisers, the per-pass engine routine, the cue player's tones, the volume ramp, the stops), verified by a Paula register log in the Musashi rig and by audio captures of the reference emulator. The core gains `src/core/sound.*`, a model of the original's sound state (objects, four voices, the notice byte cases, the ramp target), which emits per-pass voice commands into the scene; the SDL frontend gains `src/frontend/sdl/audio.*`, a Paula-like mixer (four 8-bit voices at their periods, volume 0 to 64, the Amiga's stereo pairing) in the SDL audio callback, with a WAV render for tests. The extractor exports the sound object table and the samples' playback periods. The browser build gets a JavaScript port of the extractor (`tools/extract-js`, differential-tested against the Python one) and a page that takes the player's ADF and feeds the assets to the core in memory. Two RE sweeps (notes 03 to 06 and 08; note 07) settle the carried questions, and one code round applies what they settle; the synthetic world gains invented flight and mission tables so CI exercises the missions.

**Tech Stack:** C99 core and frontend, SDL2 (audio callback), Emscripten 4.0.9 (`make web`), Node 22 and ES modules for the JavaScript extractor, Python extractor and pytest, Musashi rig (with a new custom-register write log), vAmiga headless driver (with a new audio register trace and WAV recording), rea/Ghidra through MCP.

**Spec:** `docs/superpowers/specs/2026-10-13-interceptor-port-design.md` (sections 5 (sound triggers in the scene), 6 (sounds as WAV with the playback rate from the notes; the in-browser extractor: "part of milestone 6", decoders as pure functions), 7 slice 7 (sample playback, engine pitch rules, music sequencing), 8 (Audio: SDL callback mixing the extracted 8-bit samples with per-channel pitch and volume; input: keyboard and game controller mapped to core events, mapping in one table), 10 (display-state boundary), 11 milestone 6 ("Sound and polish, then the Emscripten build of the SDL frontend as the first browser version"), 12 (manual)). Prior plan, its ledger and the follow-ups: `docs/superpowers/plans/2026-10-14-milestone-5-combat.md`, `.superpowers/sdd/2026-10-14-milestone-5-combat/progress.md`, `docs/superpowers/plans/2026-10-15-milestone-5-followups.md` and `docs/superpowers/plans/2026-10-14-milestone-4-followups.md` (binding input: every CARRIED item is assigned to a task below).

## Global Constraints

- Clean room: notes are prose and tables; no disassembly, decompiler output, assembly mnemonics or copied tables; test fixtures are invented; nothing under `disks/`, `extracted/`, `assets/` or the codewheel files is committed; captures are described, never committed (WAV recordings stay under `extracted/captures/`); game text is never quoted beyond identification; the extractor carries texts and tables, the repository never does; the public web build hosts no asset and no sample.
- Core: C99, fixed-width types, no stdio, no floating point, no OS calls; the sound state in fixed point as the original; constants from note 10 with section comments; port choices labelled NOTE-GAP with the open question; the W key stays the one PORT FEATURE.
- Determinism: the simulation step is `FLIGHT_STEP_HZ` = 8; the original's per-blank routines (the volume ramp, the per-pass sound routine) are converted with note 08 3.8's contract and stated per routine; the goldens stay byte-stable (integers only): the audio commands are logged as integers; every random value of the sound (the noise's pitch) comes from the seeded core generator.
- The scene struct is the contract: `scene.h` changes once, in Task 5 (the audio command list and the master-volume target replace `game_sound_t`); the frontend never computes game state; the core never mixes audio.
- Audio fidelity: the frontend renders each voice as the hardware does: 8-bit signed samples, zero-order hold at 3,546,895 / period samples per second (PAL), volume 0 to 64 linear, voices 0 and 3 to the left and 1 and 2 to the right, the master ramp applied as note 10 states; no interpolation, no effects. The low-pass filter of the hardware is applied only if note 10 finds the game enables it (R from the capture).
- rea: open `extracted/interceptor-arm.elf` (procedure in `tools/hunk/ghidra_import.md`); apply the map symbols.
- Sandbox: `. /sandbox/.tools/env.sh` first; the node has 2 CPUs and 7 GB shared: `make -j1`, one emulator at a time under `flock /tmp/ref.lock`, Python rig scripts under `ulimit -v 2500000` and `timeout`, one Rig per script; commit messages end with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`; explicit-path commits; other agents commit concurrently; commit after every item; `make` before the frontend pytest; CI has 20-minute job timeouts and the Debian tcc 0.9.27 pitfall (no struct assignment in a for-increment); worktree agents reset their worktree to the branch head first.

## Review Focus

1. The player switches views, pauses, presses M, ejects or crashes while the engine, the gear sound and a cue tone are all playing. Expected: the original's channel stops (0x115F6 stops all four; the view switch stops voice 3; the gear end stops voice 2; the pause stops all and the resume restarts the engine from the notice byte) in the original's order, no voice left running that the original stops and none stopped that it keeps. Pinned in Task 5.
2. The sound objects are missing or truncated (a disk whose `text/*` files are short, or an assets directory from an older extractor). Expected: the loader's own enable bits (`V:4AC10..13`): a sound whose object does not exist plays nothing and the game goes on (the noise wrapper posts its notice instead), never a crash or a read past the buffer. Pinned in Tasks 4 and 6.
3. The browser page is given a file that is not an ADF, an ADF of another disk, or a flux image. Expected: a clear message naming what was expected, no assets created, the page stays usable; a valid ADF produces assets byte-identical to the Python extractor's. Pinned in Task 8.
4. The audio device opens at a rate the mixer did not expect (48 kHz, 22.05 kHz, a mono device) or fails to open (CI, headless). Expected: the mixer resamples to the obtained rate, mixes to mono when the device is mono, and runs silent without audio (the game is playable; `--wav` still renders). Pinned in Task 6.
5. A mission golden runs with the synthetic world's invented tables (CI). Expected: the F-key starts the mission, the script reaches an end code with the scripted keys, and the audio command log per tick is the same under tcc and gcc. Pinned in Task 9.

## Task dependency graph (for parallel execution)

```
Wave A (concurrent): Task 1 RE note 10 (sound) with the rig's register log and the driver's audio trace · Task 2 RE sweep A (notes 03-06, 08: world, flight, cockpit, screens) · Task 3 RE sweep B (note 07: combat) · Task 8a the JavaScript extractor port (independent of the notes)
Wave B (after 1): Task 4 extractor: the sound object table and the periods · Task 5 core sound (after 4's table format) · after 2 and 3: Task 7 the follow-ups code round (core, frontend, synthetic tables)
Wave C (after 5): Task 6 frontend audio (SDL and WAV) · after 8a and 6: Task 8b the browser page and build
Wave D: Task 9 goldens, manual, README, CLAUDE.md, CI, the follow-ups document, the final whole-branch review
```

---

### Task 1: RE slice, note 10: the sound

**Files:**
- Create: `docs/re/10-sound.md` (sections: 1 the sound library and the hardware use (audio.device or direct register writes, periods, volumes, the stereo pairing, the filter); 2 the sound objects: the table at `0x5591C`, the loader `0x199CA`-`0x19C58` and `0x19660`-`0x19830` (which file, byte range, repeat range, default period and volume per object 0 to 0x24); 3 the synthesisers: the tone generator `0x1A042` (the nine parameters p1-p9: frequency, duration, envelope and repeat as the code uses them, the buffer it fills), the noise buffer (0x800 bytes, the generator, the random pitch of `0x1A0DC`); 4 the player `0x19C7C` and the channel slots `0x54ED0` (what a start does, what a stop does: `0x115F6`, `0x19C58`, `0x1A24C`); 5 the per-pass routine `0x14AA0` (engine, wind and afterburner: pitch from the throttle byte `+0x2B` and the angular rates, volume from the speed, one case per value of the notice byte `V:4A968`; the gear countdown `V:4A93D`; the cue player's call); 6 the volume ramp `0x294AC` (`V:4A720`, `V:54FBE`, `V:4A88F`, 0x4000 per blank) and its conversion to steps; 7 the start-up, menu and mission phrases (`0x19CE6`, `0x19D7A`: what `texti*` and `text201` hold, when each plays, their volumes) and the key click `0x37C2E` / the HUD beeps `0x37C32`, `0x37C4A`, `0x37C44`, `0x37C52`; 8 the event table (every trigger of note 07 13.4 and notes 05/06/08 with its object, voice, period and volume as numbers); 9 the pass-to-blank timing of each; Constants; Open questions; Verified by)
- Create: `tools/m68k-rig/tests/lists_model/sound_check.py` (the per-pass routine, the player, the tone and noise generators and the ramp against the original over random states, with the custom-register write log as the witness), `tools/vamiga-ref/scripts/sound-*.txt` (captures: the boot phrases, the menu, a flight start, throttle sweeps, the afterburner, the gear, a touchdown, a crash, an ejection, a gun burst, a launch, a cue, a view switch and a pause)
- Modify: `tools/m68k-rig/rig.c`, `rig.h` (a custom-register write log: every CPU write to `0xDFF000`-`0xDFF1FF` appended as (pc, register, value, instruction count) up to a cap; `rig_io_log_set(on)`, `rig_io_log_get(buf, max)`; and a read hook for the audio registers the loader polls), `tools/m68k-rig/Makefile`, `tools/vamiga-ref/ref.cpp`, `tools/vamiga-ref/README.md` (two commands: `audtrace on|off`: while on, `runto` and `step` log every write to `AUD0..3` LC, LEN, PER, VOL and `DMACON`/`ADKCON` with the frame and raster line; `wav <path.wav> start|stop`: record the emulator's mixed stereo output (vAmiga's audio port) as 16-bit WAV at its sample rate), `docs/re/01a-reference-captures.md`, `docs/re/README.md` (the note 10 row), `docs/re/07-ai-and-weapons.md` 13.4 (pointers to 10 only), `docs/re/08-game-flow.md` 3.6 and 5.4 (the key click and the menu phrase pointers)

**Interfaces:**
- Produces, for Tasks 4 and 5: the object table as numbers (object, file name, byte offset, length, repeat offset, repeat length, default period, default volume, voice), the synthesiser formulas (buffer length, the sample formula per parameter set, the random source), the per-pass routine's formulas (pitch and volume per case of `V:4A968`, the throttle and rate terms, the ranges), the ramp rule in steps, the stop rules per event, the event table with periods and volumes; every rule tagged V with a rig count or capture, R when read only, C when fitted.

- [ ] Read the hand-over points: note 07 13.4 (the wrappers, the cue player, the event table), note 05 §3 sound requests (`V:4A878`, `V:4A8F6`, the notices), note 06 (the warning table's sound flag `V:4A91B`, the beeps), note 08 3.6 (the key click), 5.4, 5.11, 6.1, 6.2 (the music calls, the ramp, the stops), note 00 (hunks 78-95, `audio.device`), note 04 (the ramp per blank); list every address they hand to note 10.
- [ ] Add the register write log to the rig (`rig_io_log_set`, `rig_io_log_get`; `make -C tools/m68k-rig test` still green) and the `audtrace` and `wav` commands to the driver (`bash tools/vamiga-ref/build.sh`; a parse test for each in the driver's README table and `tools/vamiga-ref/tests` if present).
- [ ] Characterise the library and the hardware use with rea (section 1): which routine writes the periods, lengths, volumes and DMA bits; whether the game uses `audio.device` allocation then direct writes; the filter bit of `CIAA PRA`; the stereo pairing. Verify with one capture of the boot phrases (`sound-boot.txt`, `audtrace on` plus `wav`).
- [ ] Decode the object table and the loader (section 2): per object the file, the byte ranges, the repeat ranges, the period and volume defaults; rig: run the loader on the real files (`sound_check.py objects`: the table's longs after the load compared with the note's rule, all 37 objects); write section 2.
- [ ] Decode the tone generator and the noise buffer (section 3): `cue_check.py`'s nine parameters get their meaning; rig: run `0x1A042` with the cue table's parameter sets and dump the buffer (`sound_check.py tone`: the note's formula reproduces every buffer byte, count the sets); the noise buffer's generator and the pitch rule of `0x1A0DC` (`sound_check.py noise`: the random source identified, the buffer compared with the seed pinned).
- [ ] Decode the player, the slots and the stops (section 4) and the per-pass routine (section 5): rig `sound_check.py pass` over random viewed-record states (throttle, speed, rates, the notice byte, the gear countdown, the view) with the register log as the witness (periods and volumes written per pass), 2,000 passes; write sections 4 and 5 with one table per notice case.
- [ ] The ramp, the phrases, the clicks and beeps (sections 6 and 7): captures `sound-menu.txt` (the menu phrase and the ramp: the volume per blank from `audtrace`), `sound-flight-start.txt` (modes 1 and 3: the mission phrase, the engine start), `sound-click.txt` (a typed text screen); convert the ramp to steps with 3.8; write 6 and 7.
- [ ] The event table (section 8) and the timing (section 9): captures `sound-throttle.txt` (a throttle sweep and the afterburner: the engine pitch per blank against section 5's formula), `sound-gear-touchdown.txt`, `sound-crash.txt`, `sound-eject.txt`, `sound-gun-launch.txt`, `sound-cue.txt` (a lock cue), `sound-views.txt` (a view switch, M, a pause: which voices stop); compare each with the rule, state the counts; close the open items of note 07 13.4 (which wrapper a non-camera launch uses; the 150-pass echo `V:4AC13` bit 7; `gun_sound`) and the pitch/volume question of the follow-ups; record every capture in 01a; commit per section; report in `.superpowers/sdd/2026-10-15-milestone-6-sound/task-1-report.md` (not git-added).

Acceptance: Task 5 can implement the sound state from note 10 alone and Task 6 the mixer from section 1; every rule carries a rig count or a capture; the extractor's sample rate is no longer provisional.

---

### Task 2: RE sweep A: the carried questions of notes 03 to 06 and 08

**Files:**
- Modify: `docs/re/03-3d-pipeline.md`, `docs/re/04-scene-composition.md`, `docs/re/04a-display-lists.md`, `docs/re/05-flight-model.md`, `docs/re/06-cockpit-hud.md`, `docs/re/08-game-flow.md` (each settled item written into its section with the tag), `docs/re/01a-reference-captures.md`
- Create: `tools/vamiga-ref/scripts/followup-*.txt`, rig scripts under `tools/m68k-rig/tests/lists_model/` as needed (`mesh_check.py`, `filler_check.py`, `mapcam_check.py`)

**Interfaces:**
- Produces, for Task 7: a settled rule (or a stated unobservability) for each item below, with the evidence named.

- [ ] Note 03: the sea-mesh vertex layout (world.c "assumed", note 03 calls it C): read the routine, rig `mesh_check.py` (the mesh vertices of random camera cells against the original), write the rule.
- [ ] Note 04: the polygon filler's tiny cases (the ball: panel.c, raster.h "Filling the polygon"): read the filler, rig `filler_check.py` (random tiny polygons through the original's filler against the note's rule: pixel sets), write the rule; the map overview camera and the four map numbers (fitted to captures, 5 and 6 pixels differ): read the map code `0x2FE26`-`0x30046` for the overview's camera and the number positions, write the rule.
- [ ] Note 04a: the viewed cell `V:4AA04/06` from the camera position (bits 22 to 28) rather than the aircraft: confirm and state; the external views near cell edges.
- [ ] Note 05: the roll word `V:4AB4A` outside the cockpit and the player's `+0x38`: find the writers (rea, every reference) and state them; the location key 0 heading of 256,000 (05 17.14): how the attitude code treats it (rig: one call with the word); the game-over timers under a pause (17.17 rule 7) and the pause pass rate: capture `followup-pause-gameover.txt` (per-blank `V:4A992`, `V:4AB8E`, the timers) and state the rule in blanks and steps.
- [ ] Note 06: `V:4A859`'s writer (settled: `0x12EE6` in `0x12EDA`): one line pointing 08 6.1 item 1 and the port's check to it; the `V:4A869` glance triples (Q11): the writer or its absence; the HUD step and the window row of each start (one table).
- [ ] Note 08: the demo ESC fade (7.7, 5.11: 6 against 7 blanks and the 28-32 spread): capture `followup-demo-esc.txt` three times, state the rule or the measured spread as the rule; the typed-key ring (5.8) cleared or handed on before the callsign entry: capture `followup-callsign-ring.txt` (11 keys typed before the entry opens; what the entry shows); P in the notice and outcome screens (6.4) and P with the byte at 0 (12.11): capture `followup-pause-screens.txt`; C13 the approach mover's call count and the free-flight airfield start's approach (08 12.14 N1 assumes 112): capture `followup-airfield-start.txt` (an airfield start, the passes to the flight), state the count per start; C1 the briefing camera's flight to the first hostile (stage 8): capture `followup-brief-camera.txt` (`V:4ACF6..4ACFE`, `V:4AD0E`, `V:4A86D` per pass from stage 7 to `0x12AC0`), state the camera rule; C10 the scan rate of the walk's counter, C11 the KP-ENTER skip in the briefing, C12 the visibility rule on sub-lists 2 and 3: one capture each.
- [ ] Commit per note; report in `.superpowers/sdd/2026-10-15-milestone-6-sound/task-2-report.md` (not git-added) with a table: item, settled or open, evidence, the code the port must change (file and rule).

Acceptance: every item of the follow-ups document's note 03-06 and 08 sections is settled with evidence or stated as unobservable with the reason; Task 7 can apply the changes from the notes alone.

---

### Task 3: RE sweep B: the carried questions of note 07

**Files:**
- Modify: `docs/re/07-ai-and-weapons.md` (sections 2, 4, 6, 7, 8, 9, 10, 11, 12, 13, Open questions, Verified by), `docs/re/01a-reference-captures.md`
- Create: `tools/vamiga-ref/scripts/followup-combat-*.txt`, rig parts in `tools/m68k-rig/tests/lists_model/weapon_check.py`, `ai_check.py` (the x-drift harness)

**Interfaces:**
- Produces, for Task 7: a settled rule for each item below, with the evidence named.

- [ ] Part A: the selector `V:4ABB0` distribution (capture `followup-combat-selector.txt`: 30 mission starts from a snapshot with the timer reading logged); the dive x drift (4.12): the harness run of class 0 and 0x15 dives with the position words compared (`ai_check.py dives`), the rule stated; the launch-order conflict: a staged fight with a script request and a hostile launch in one pass (`followup-combat-launch-order.txt`); the five passes before the seat appears and the seat's level 2 to 1 (`followup-combat-eject-frames.txt`: a per-frame capture from the key to the seat view, the screen described per pass); A4, A6-A8 (a counted hostile's kill by the original's own weapon, a seat shot down, a parachute in flight, the crash hand-over with a nibble): one staged capture each, the rule confirmed or corrected.
- [ ] Sections 7-8: the landmark branch of the tracer test (rig: a logged scene's landmark slots filled; `weapon_check.py landmark`); the template part ids (the extractor scans the template list: the nibble per template, a table in 8); a staged missile kill and a gun kill from a fresh MiG (`followup-combat-kills.txt`); `V:4A840` (a watch over a mission in the rig and a peek in the capture); a tracer against an AMRAAM (rig).
- [ ] Sections 9-10: a flare or chaff slot under an aircraft (rig, the log counter `+0x3C` as the witness); roll and yaw out of control (rig: the input conversion with B20 bit 1); readers of `V:4A984` bit 8 (rea: every reference).
- [ ] Sections 11 and 13: the effect life wrap picture (capture: an object poked to 0x7FFF); the revival in the draw handler; the wreck before the burst (the damage picture of models 3 to 7 and the others); the jolt writers (0x2A8A8 kind 1; kind 2); the flare star and chaff burst pixels (capture `followup-combat-flare-chaff.txt`: a frame of each, compared with the port's drawing); the non-camera launch wrapper (goes to note 10, Task 1: coordinate).
- [ ] Section 12: the scope units against the mile labels (a capture with a target at a known range); the words past the erase pages (`0x53818`); a gun kill of a designated target (capture, the 0x4016 post); `V:4A980` bit 15 and `V:4A98E` bit 5; the hostile-missile threat bits (capture with `V:4A925` logged); the first free-flight pass of `V:4A8EE`/`V:4A8EF` (capture `followup-combat-first-pass.txt`: free flight from the menu, both bytes per pass); why the external-view panel draw leaves the screen unchanged (the blitter destination of `0x35238` in an external view); the range key's other nibbles (a key sweep with the nibble read per press).
- [ ] Commit per section; report in `.superpowers/sdd/2026-10-15-milestone-6-sound/task-3-report.md` (not git-added) with the same table as Task 2.

Acceptance: every item of the follow-ups document's note 07 sections is settled or stated as unobservable; Task 7 can apply the changes from the note alone.

---

### Task 4: Extractor: the sound object table and the sample periods

**Files:**
- Modify: `tools/extract/extract/sounds.py` (the WAV rate from note 10 per file: the object's default period, `3546895 / period`; `SAMPLE_RATE_PROVISIONAL` removed), `tools/extract/extract/combat.py` or a new `tools/extract/extract/sound_table.py` (the object table `0x5591C` decoded per note 10 section 2 into `tables/sounds.bin`: one record per object 0 to 0x24: file index into `sounds/index.txt` (0xFF for a synthesised object), byte offset, length, repeat offset, repeat length, default period, default volume, voice, kind (0 sample, 1 tone, 2 noise)), `tools/extract/extract/cli.py`, `manifest.py` (version 24, `sample_rate_hz` per file), `tools/extract/README.md` (the row), `tools/extract/tests/test_sounds.py` (structure only: 37 records, offsets inside the file lengths of the synthetic fixture, kinds), `tools/extract/tests/synth.py` (the synthetic ADF gains invented `text/*` files and an invented object table), `src/core/assets.h`, `src/core/assets_combat.c` (`assets_load_sounds(const game_loader_t *, sound_table_t *out, char *err, size_t errlen)`; `CORE_ASSETS_VERSION` 24), `tests/test_assets.c`, `tests/world_synth.h` (an invented table), `README.md` and `docs/manual.md` (the version sentence)

**Interfaces:**
- Produces, for Tasks 5, 6 and 8: `tables/sounds.bin` (37 x 20 bytes big-endian: u8 file, u8 kind, u8 voice, u8 volume, u32 offset, u32 length, u32 rep_offset, u16 rep_length, u16 period) and `sound_table_t { struct { uint8_t file, kind, voice, volume; uint32_t offset, length, rep_offset; uint16_t rep_length, period; } obj[37]; }`; the samples stay `sounds/<name>.raw` (signed 8-bit) and `.wav`.

- [ ] Write the structure test on the synthetic ADF (`pytest tools/extract/tests/test_sounds.py`: fails); decode the table per note 10; the test passes; the real ADF's table matches note 10's per-object table (checked locally, not asserted in the test).
- [ ] The WAV rate per file from the object's period; the manifest fields; version 24; `python -m extract extracted/interceptor.adf assets`.
- [ ] The loader and its unit test (invented table through the synthetic loader); `tests/world_synth.h` gains the table; both compilers green; commit.

Acceptance: the extractor's sample rate is the note's; the core loads the object table; nothing in the tree holds a table value.

---

### Task 5: Core: the sound state and the scene's audio commands

**Files:**
- Create: `src/core/sound.c`, `src/core/sound.h`
- Modify: `src/core/scene.h` (replace `game_sound_t`, `sounds[]`, `sound_count`, `sound_vars[]` with the audio command list and the master target), `src/core/game.c` (the per-pass call order: the sound routine runs where the original's `0x14AA0` runs, after the arbiter and before the scope walk (07 12.1); the stops on view switch, M, pause, ESC, the crash hand-over; the ramp target per screen state), `src/core/screen.c` (the key-click requests of the text engine become audio commands; the menu and mission phrases; the ramp targets of 08 5.4/5.11/6.1), `src/core/weapons.c`, `src/core/weapons.h` (the combat events raise sound events through `sound_event`; `weapons_t.snd[]`, `click`, `click2`, `ecm_lamp`, `readout` consumed or removed), `src/core/entities.c` (`sound_req_4a921/22/870` consumed by the ejection bang), `src/core/flight.c` (the ground contact volumes, the touchdown, the skid and overspeed requests `V:4A878`, the afterburner `V:4A8F6`), `src/core/radar.c` (the cue player's tones through the generator), `src/core/world.c` (the counter-14 distance words), `tests/test_sound.c` (new), `tests/test_game.c`, `tests/test_screen.c`, `tests/test_weapons.c`, `tests/rig/test_sound_diff.c` (new; `make -C tools/m68k-rig test-sound`), `tools/m68k-rig/Makefile`, `tests/golden/run_golden.c` (the audio columns: commands per tick as count and a 32-bit hash of (op, voice, object, period, volume)), the goldens (synthetic regenerated once with the columns; the old columns unchanged)

**Interfaces:**
- Consumes: `sound_table_t` (Task 4), note 10's formulas.
- Produces, for Task 6: in `scene.h`:
  ```c
  typedef enum { GAME_AUD_START = 1, GAME_AUD_SET = 2, GAME_AUD_STOP = 3 } game_audio_op_t;
  typedef struct {
      uint8_t  op;          /* game_audio_op_t */
      uint8_t  voice;       /* 0..3 */
      uint8_t  object;      /* 0..0x24 (START) */
      uint8_t  volume;      /* 0..64 (START, SET) */
      uint16_t period;      /* Paula period (START, SET) */
      uint32_t offset, length, rep_offset, rep_length;   /* bytes into the object's buffer (START) */
  } game_audio_cmd_t;
  #define GAME_MAX_AUDIO_CMDS 16
  /* in game_scene_t: */
  uint8_t audio_count; game_audio_cmd_t audio[GAME_MAX_AUDIO_CMDS];
  uint8_t audio_master_target;   /* 0..63: the ramp's target in 0x4000 units; the frontend moves the level one unit per blank (20 ms) toward it (note 10 section 6) */
  ```
  and in `sound.h`: `void sound_init(sound_t *, const sound_table_t *, uint32_t seed)`, `void sound_pass(sound_t *, const sound_in_t *, game_audio_cmd_t *out, int max, int *count)` (the per-pass routine), `void sound_event(sound_t *, sound_event_kind_t, int a, int b)` (the wrappers of 07 13.4: `SND_NOISE(a, b)`, `SND_GUN`, `SND_TONE(p[9])`, `SND_GROUND(v)`, `SND_GEAR`, `SND_LAUNCH(v)`, `SND_PHRASE(n, v)`, `SND_CLICK`, `SND_BEEP(n)`), `void sound_stop_all(sound_t *)`, `void sound_stop_voice(sound_t *, int)`, `int sound_buffer(const sound_t *, int object, const int8_t **data, uint32_t *len)` (the synthesised buffers of objects 4, 6, 0xA and 0xC's source; file-backed objects return 0 and the frontend reads the sample), `void sound_set_master(sound_t *, int target)`.

- [ ] Write the unit tests of the per-pass routine from note 10 section 5 (one test per notice case with invented throttle, speed and rate values; the expected periods and volumes computed by the note's formulas in the test), run them: fail; implement `sound_pass`; pass; commit.
- [ ] The synthesisers (the tone buffer from p1-p9, the noise buffer with the seeded generator): tests on the note's formulas with invented parameters; the rig part `test_sound_diff.c tone` (the original's `0x1A042` buffer against `sound_buffer` for the cue table's sets and 200 random sets, 0 differences); commit.
- [ ] The events (every row of note 10 section 8): the stops, the gear countdown, the ground contact, the launch, the phrases, the clicks, the beeps, the cue tones; unit tests per event with the expected command; the rig part `test_sound_diff.c pass` (random viewed-record states through the original's `0x14AA0` with the register log against `sound_pass`'s commands, 2,000 passes, 0 differences); commit.
- [ ] Wire the modules: game.c order, screen.c clicks and phrases and the ramp targets, weapons.c events, entities.c, flight.c, radar.c, world.c; remove the consumed fields and every NOTE-GAP that says "nothing consumes it" (weapons.h header, scene.h, game.c:286, 1208, 1291, 1650); Review Focus 1 test in `tests/test_game.c` (a view switch, M, a pause, an ejection and a crash with the engine, the gear and a cue playing: the commands per pass against the note's stop table); the goldens' audio columns; `make golden-update-synthetic` once with the reason; the real ones local; both compilers, pytest, `make -C tools/m68k-rig test` green; commit.

Acceptance: the core emits the original's voice commands per pass; the rig compares them; the scene's old sound fields are gone.

---

### Task 6: Frontend: the Paula mixer and the WAV render

**Files:**
- Create: `src/frontend/sdl/audio.c`, `src/frontend/sdl/audio.h`, `tests/frontend/audio_driver.c`, `tests/frontend/test_audio.py`
- Modify: `src/frontend/sdl/main.c` (`SDL_INIT_AUDIO`, `SDL_OpenAudioDevice` with the obtained spec, `--wav FILE` renders the run's audio to a 16-bit stereo WAV at 44,100 Hz with one 125 ms block per step, `--no-audio`), `src/frontend/sdl/assets.c`, `assets.h` (`assets_load_sample(const game_loader_t *, int file, int8_t **data, uint32_t *len)` through `sounds/index.txt`), `Makefile` (the driver), `docs/manual.md` (the sound chapter: what plays, the volume, `--no-audio`)

**Interfaces:**
- Consumes: `game_audio_cmd_t`, `audio_master_target`, `sound_buffer` (Task 5), `assets_load_sample` (this task), `tables/sounds.bin` (Task 4).
- Produces: `audio_t *audio_create(const game_loader_t *, const sound_table_t *, int rate_hz, int channels)`, `void audio_apply(audio_t *, const game_scene_t *)` (queues the commands of one step at the step's time), `void audio_render(audio_t *, int16_t *out, int frames)` (the mixer: four voices, zero-order hold at 3,546,895 / period per voice resampled to `rate_hz`, volume 0..64 linear, the master level 0..63 as note 10 section 6 applies it, voices 0 and 3 left and 1 and 2 right, mono by averaging when `channels` is 1), `void audio_destroy(audio_t *)`.

- [ ] Write `test_audio.py` on the driver with invented samples and an invented object table: one voice at period 428 renders a known waveform (the sample repeated at 8,287 Hz, zero-order hold), two voices on the left and right, a STOP mid-buffer, a SET of volume and period, the repeat range looping, a rate of 48,000 and a mono device (Review Focus 4): fail; implement `audio.c`; pass; commit.
- [ ] Review Focus 2: an object whose sample is missing or whose range lies past the file: `audio_apply` ignores the START and reports it once on stderr (the loader's enable bits are the core's; this is the frontend's guard); test; commit.
- [ ] `main.c`: the device, `--wav`, `--no-audio`, silent when the device fails to open (CI runs with `SDL_AUDIODRIVER=dummy`); `tests/frontend/test_audio.py` renders the takeoff golden's first 80 steps to WAV with `--wav` and checks the engine voice's period sequence against the audio columns of the golden; commit.
- [ ] The filter: if note 10 found the low-pass enabled, implement the hardware's RC low-pass as the note states (two first-order stages at the cut-off the note gives) behind the state the game sets; else no filter and a line in the manual; the capture comparison: `sound-flight-start.txt`'s WAV against the port's `--wav` of the same start (the engine's fundamental period per 125 ms block within 1 period; recorded in the report, not asserted in CI); commit.

Acceptance: the port is audible with the original's periods and volumes; the WAV render is deterministic; the frontend tests cover the mixer's rules with invented data.

---

### Task 7: The follow-ups code round: what Tasks 2 and 3 settled, the synthetic mission tables, the controller

**Files:**
- Modify: `src/core/*.c` and `src/frontend/sdl/*.c` as the Task 2 and 3 reports name per item (each change cites the note section), `tests/world_synth.h` (invented flight tables: the scenario rows, the AI tables already invented in `tests/ai_tables.h`, the mission tables, the templates: enough for F1 to F6 to start and for a script to reach an end code), `tests/golden/run_golden.c` (the synthetic run no longer refuses the F-key), `tests/golden/mission-f*.txt` (a `synthetic` section of keys if the synthetic fight differs), `src/frontend/sdl/main.c` and a new `src/frontend/sdl/input.c`, `input.h` (the game controller: SDL_GameController mapped in one table to the same core events the keyboard sends: the stick as the digital stick of note 05 17.4 with a dead zone that the manual states, the buttons to fire, gear, brake, views; no new rule), `docs/manual.md` (the controller table), the tests of each changed rule

**Interfaces:**
- Consumes: the Task 2 and 3 reports' tables (item, rule, file).
- Produces: closures for the follow-ups document (Task 9).

- [ ] For each settled item of the Task 2 report (notes 03-06, 08): write the test from the note's rule, run it (fails), change the code, run it (passes), commit per item with the note section in the message. Items known now: the HUD switch set to 1 in the last pre-flight state (`0x12EE6`) rather than at the flight start; the viewed cell from the camera position; the sea-mesh layout; the filler's tiny cases; the roll word and the player's `+0x38` writers; the location key 0 heading; the pause pass rate of the resume states; the map overview camera and the numbers; the airfield approach count; C13's mover call count.
- [ ] For each settled item of the Task 3 report (note 07): the same cycle. Items known now: the dive x drift; the launch order; the part ids; the flare/chaff slot rule; the out-of-control roll and yaw; the first free-flight pass of the redraw counters; the range key's nibbles; the flare star and chaff burst pixels.
- [ ] The synthetic world's invented tables (Review Focus 5): `tests/world_synth.h` grows the tables; the six synthetic mission goldens are regenerated once (`make golden-update-synthetic`, each now a real run with an end code), `make -j1 CC=tcc golden` and `make -j1 golden` byte-identical; commit.
- [ ] The controller mapping (spec section 8): `input.c` with the one table; `tests/frontend/test_input.py` injects SDL controller events through `--ctrl NAME@N` (added like `--key`) and checks the core events; the manual's table; commit.
- [ ] Remove every NOTE-GAP comment the two sweeps settled; report in `.superpowers/sdd/2026-10-15-milestone-6-sound/task-7-report.md` (not git-added): per item the note section, the commit, the test.

Acceptance: no carried item of the follow-ups document is left in the code without either a change citing the note or a stated unobservability; CI's synthetic mission goldens run the missions.

---

### Task 8a: The JavaScript extractor

**Files:**
- Create: `tools/extract-js/package.json` (type module, no dependencies, `node --test`), `tools/extract-js/src/adf.js`, `hunk.js`, `ilbm.js`, `palette.js`, `png.js` (a minimal PNG encoder: stored deflate blocks and CRC32, so no dependency), `sky.js`, `tables.js`, `cockpit.js`, `flight.js`, `lists.js`, `terrain.js`, `models.js`, `combat.js`, `flow.js`, `sounds.js`, `manifest.js` (sha256 via Web Crypto), `index.js` (`extract(adfBytes) -> Map<path, Uint8Array>`; the manifest's hashes), `tools/extract-js/test/synth.test.js` (the synthetic ADF of `tools/extract/tests/synth.py` written once by a Python helper to `tools/extract-js/test/fixtures/synth.adf` (invented bytes, committed: 880 KB; or generated at test time by calling the Python helper when available), the JavaScript output compared file by file with the Python extractor's output of the same fixture, stored as `tools/extract-js/test/fixtures/synth-expected.tar` or regenerated by the test when Python is present), `tools/extract-js/README.md`
- Modify: `.github/workflows/ci.yml` (a job: `node --test tools/extract-js`, Node 22), `tools/extract/README.md` (the JavaScript port's row and the rule that both ports change together), `tests/frontend/test_extract_js.py` (local: when `extracted/interceptor.adf` exists, run both extractors on it and compare every file byte for byte; skipped in CI)

**Interfaces:**
- Produces, for Task 8b: `extract(adfBytes: Uint8Array): Promise<{files: Map<string, Uint8Array>, manifest: object}>` throwing `ExtractError(message)` with a message that names the expected disk when the ADF is not the game's (the Python extractor's own check: the file names and the executable's hunk signature), for a short file, and for a file that is not an ADF (a flux image is detected by its SCP/HFE/MFI magic and refused by name).

- [ ] Port module by module in the order of the list, each with a `node --test` test on the synthetic fixture against the Python output of that module (byte-identical); commit per module.
- [ ] `index.js` and the manifest (the same `extractor_version` and the same file set as `cli.py`); the whole-output comparison test; the three error cases (Review Focus 3); commit.
- [ ] The local real-ADF comparison test; CI job; commit.

Acceptance: the JavaScript extractor's output is byte-identical to the Python extractor's for the synthetic fixture (CI) and the real disk (local); the two ports share a version number.

---

### Task 8b: The browser page and the web build

**Files:**
- Modify: `src/frontend/web/shell.html` (the page: a drop zone and file picker for the ADF, a progress line, the error messages of Task 8a's `ExtractError`, the canvas, a "start" button that unlocks the audio context (browsers require a gesture), the keyboard help), `Makefile` (`make web`: no `--preload-file`; `-sEXPORTED_RUNTIME_METHODS=FS` and a `-sMODULARIZE` entry so the page creates the assets in MEMFS from the extractor's map before calling `main`; the JavaScript extractor bundled by copying `tools/extract-js/src` into `build/web/extract/`; the output under `build/web/` ready for static hosting), `src/frontend/sdl/main.c` (under `__EMSCRIPTEN__`: the main loop through `emscripten_set_main_loop` instead of ASYNCIFY if the audio callback needs it; `--assets /assets`), `README.md` (the browser version: how to build, how to host, what the player needs), `.github/workflows/ci.yml` (a job building `make web` with the Emscripten SDK action; the artefact uploaded), `tests/frontend/test_web_build.py` (local: `make web` succeeds and `build/web/` holds the page, the wasm, the extractor; skipped without emcc)

**Interfaces:**
- Consumes: Task 8a's `extract`, Task 6's audio (SDL2 audio under Emscripten).

- [ ] The page's ADF path with the three error cases (Review Focus 3) and a successful extraction into MEMFS (checked in a headless Chromium through `tools/web/smoke.mjs` with Playwright if installable; else a Node test of the page's module functions apart from the DOM); commit.
- [ ] `make web` without assets; the audio unlock on the start gesture; the keyboard mapping notes on the page; a smoke run in the sandbox with the real ADF described in the report (no screenshot committed); CI job; commit.

Acceptance: a visitor with their own ADF plays the port in the browser with sound; no asset is hosted; CI builds the page.

---

### Task 9: Goldens, manual, README, CLAUDE.md, CI, the follow-ups document, the final review

**Files:**
- Modify: `tests/golden/*` (the audio columns everywhere; the synthetic mission goldens as real runs (Task 7); the real ones local), `docs/manual.md` (sound, the controller, the browser version, "Not in yet" emptied or stating what remains), `docs/easter-eggs.md` (sound-related items note 10 found: unused phrases or samples), `README.md` (status: milestone 6; the browser build; the extractor version), `CLAUDE.md` (the new commands: `--wav`, `--no-audio`, `--ctrl`, `make -C tools/m68k-rig test-sound`, `node --test tools/extract-js`, the driver's `audtrace` and `wav`), `.github/workflows/ci.yml` (all jobs: the goldens, the rig where it runs, the node test, the web build), `docs/superpowers/plans/2026-10-15-milestone-5-followups.md` (every CARRIED item CLOSED with the commit and section, or re-carried to a new `docs/superpowers/plans/2026-10-16-milestone-6-followups.md` with the reason)

- [ ] Goldens byte-stable under tcc and gcc with the audio columns; manual, easter-eggs, README and CLAUDE.md from the notes and from playing; the follow-ups documents; CI green on the three platforms (the `ci-results` branch); the final whole-branch review (clean room sweep: no sample bytes, no table values, no game text; the NOTE-GAP and port-choice inventories; the acceptance of every task) and its fix round; merge to main.

Acceptance: the port plays sound and runs in the browser; the follow-ups documents are consistent with the tree; CI is green.

---

## Self-review notes

- Spec coverage: section 5's sound triggers (Task 5 redefines them as voice commands: the spec's "sample id, pitch, volume, loop flag" generalised to the hardware's start/set/stop, which the perfect-replica directive requires), section 6's WAV rate (Task 4) and in-browser extractor (Task 8a/8b), slice 7 sound (Task 1), section 8 audio (Task 6) and the controller (Task 7), section 11 milestone 6 (Tasks 6, 8b), section 12 manual (Task 9); the follow-ups documents (Tasks 2, 3, 7, 9).
- Review Focus 1-5 pinned in Tasks 5, 4/6, 8, 6, 9.
- Names used across tasks: `sound_table_t`, `assets_load_sounds`, `game_audio_cmd_t`, `GAME_AUD_START/SET/STOP`, `audio_master_target`, `sound_init/pass/event/stop_all/stop_voice/buffer/set_master`, `audio_create/apply/render/destroy`, `assets_load_sample`, `extract` / `ExtractError`.
- Scope: Task 8a is the largest single task (about 3,800 lines of Python to port); it starts in Wave A because it depends on no note. If it does not finish inside the milestone, the browser page ships with the Python extractor's output uploaded as a zip (a documented interim path) and 8a carries over; that fallback is a decision for the user, recorded in the ledger, not taken silently.
