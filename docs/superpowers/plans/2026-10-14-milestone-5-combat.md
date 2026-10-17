# Milestone 5: Combat (AI aircraft, weapons, countermeasures, damage, the six missions) and the milestone-4 follow-ups Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The six missions are playable end to end with the original's rules: hostile and friendly aircraft fly the original's AI, the gun and both missiles fire, guide and hit as the original's code does, flares, chaff and ECM act on them, damage and the dying path follow the original, the radar, target box, range and message line show the original's data, the mission scripts decide success and failure, the log counts what the original counts, and the milestone-4 follow-ups (smoke trail, jolt, the remaining keys, the log patches, the screen leftovers) are closed. A port feature is added on top: one key the original leaves unused replenishes fuel and weapons through the original's own rearm routine.

**Architecture:** Three reverse-engineering slices written concurrently: note 07 part A (`docs/re/07-ai-and-weapons.md` sections 1-6: entities, the generic AI, the dying path, tracking cameras, the ejection seat), note 07 part B (sections 7-13: weapons, guidance, hits, countermeasures, damage, explosions and effects, the radar list and threat bits, target designation and the HUD data, the trail drawer and the jolt, the sound trigger points), and note 08 part C (`docs/re/08-game-flow.md` sections 12-14: the mission scripts of hunks 26 and 27, the briefing chain of modes 3-8, scoring and the log counters). The core gains `src/core/entities.*` (the 16 records and the 16 secondary objects as the original keeps them, spawning from the placement, the dying path), `src/core/ai.*` (the generic AI and steering), `src/core/weapons.*` (gun, missiles, countermeasures, damage, effects), `src/core/radar.*` (the radar list, threat bits, designation, the HUD target data), `src/core/missions.*` (scripts, dispatcher, briefing chain, end codes, scoring); the frontend draws effects, the trail, the jolt and the range number, and maps the remaining keys. The extractor exports the template table and the mission and parameter tables the notes name.

**Tech Stack:** as before: C99 core and frontend, SDL2, Python extractor, Musashi rig (differential tests of the AI, the guidance and the mission scripts against the original), vAmiga headless driver (captures of fights, hits, ejections and every mission's outcomes), rea/Ghidra through MCP.

**Spec:** `docs/superpowers/specs/2026-10-13-interceptor-port-design.md` (sections 5 (projectiles, explosions, radar contacts, target box, sound triggers), 7 slices 5 and 6, 8, 10, 11 milestone 5, 12 (weapons use and each mission's objective in the manual)). Prior plan, its ledger and the follow-ups: `docs/superpowers/plans/2026-10-14-milestone-4-game-flow.md`, `.superpowers/sdd/2026-10-14-milestone-4-game-flow/progress.md`, `docs/superpowers/plans/2026-10-14-milestone-4-followups.md` (binding input: every item is assigned to a task below).

## Global Constraints

- Clean room: notes are prose and tables; no disassembly, decompiler output, code or copied tables; test fixtures synthetic; nothing under `disks/`, `extracted/`, `assets/`, `manual-en/` or the codewheel files committed; captures described, never committed; game text never quoted beyond identification; the extractor carries texts and tables, the repository never does.
- Core: C99, fixed-width types, no stdio, no floating point, no OS calls; fixed point as the original; constants from the notes with section comments; port choices labelled NOTE-GAP with the open question; the one deliberate port addition (the replenish key) labelled PORT FEATURE.
- Determinism: a fixed simulation step at `FLIGHT_STEP_HZ` = 8; the original's "every 16th frame" and "every 32nd frame" routines run at the step the notes convert them to, stated per routine; the goldens stay byte-stable (integers only); any randomness the original draws from the timer (the placement variant, AI random offsets) comes from a seeded core generator whose seed the frontend supplies, so a run is reproducible from its seed.
- The scene struct is the contract: changes to `scene.h` are made once, in Task 5 (entities and secondary objects as scene entries, effects, the radar list and threat bits filled by the core, the target block, the jolt kind, sound triggers with sample ids and pitch/volume words as the notes give them); the display-state boundary of spec section 10 applies.
- Clean separation: the frontend never computes game state; the core never draws.
- Non-player aircraft run the flight model's non-player branches as note 08 §8.3 tabulated them (the AI response table `0x42654`, the halved pitch decay, the coupling gate, no player-only pre-steps); any further branch the AI slice finds is added to `flight_step_entity` under the `entity` flag with the player's step unchanged (test_flight_diff stays at 0 problems).
- rea: open `extracted/interceptor-arm.elf` (procedure in `tools/hunk/ghidra_import.md`); apply the map symbols.
- Sandbox: `. /sandbox/.tools/env.sh` first; commit messages end with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`; explicit-path commits; other agents commit concurrently; commit after every item; `make` before the frontend pytest; CI has 20-minute job timeouts.

## Review Focus

1. A missile is fired with no target designated, or at a target that dies or leaves range in flight. Expected: the original's rule from note 07 (the missile's own guidance state, its expiry, the record freed), never a missile that lives forever or a scene entry left behind. Pinned in Task 6.
2. All 16 entity records or all 16 secondary objects are in use when the script spawns or the player fires. Expected: the original's allocation rule (which record is reused or the spawn refused, as the note states), never a write past the arrays. Pinned in Task 5.
3. The player is hit, ejects, and the seat and parachute run while the mission script ends the mission. Expected: the original's order of the ejection hand-over and the end code (note 05 §17.17 and note 07), one outcome screen, the log counted once. Pinned in Tasks 5 and 7.
4. The replenish key is pressed during a pause, during the ESC countdown, in the demo, in training part 1, on the deck and in the air. Expected: the original's rearm body's own guards (note 08 §11: it is a dead body with no key, reachable from landing) applied as the note states; in the demo and the replay it does nothing (the replay's live-key rule); the log counters unchanged by the rearm; the message entry 5 posted once. Pinned in Task 8.
5. A mission is played to its end code with a crash restart, a pause and an ESC in the middle. Expected: the script's state survives the restart as the original's does (note 08 part C), the end code arrives once, the outcome screen and the log match the original's captured runs. Pinned in Task 7.

## Task dependency graph (for parallel execution)

```
Wave A (concurrent): Task 1 RE note 07 part A (entities, AI, dying, cameras, seat) · Task 2 RE note 07 part B (weapons, damage, effects, radar, HUD data, trail, jolt, sound points) · Task 3 RE note 08 part C (missions, briefing chain, scoring) · Task 4a extractor: the template table and the already-located tables
Wave B (after 1-3): Task 4b extractor: the tables the notes name · Task 5 core entities and AI · Task 6 core weapons, damage, radar and HUD data (after 2, 5's scene.h) · Task 7 core missions (after 3, 5)
Wave C (after 5, 6): Task 8 frontend: effects, trail, jolt, range number, keys, the replenish key · Task 9 the remaining milestone-4 follow-ups
Wave D: Task 10 goldens, manual (weapons and each mission), easter-eggs (the replenish key), CI
```

---

### Task 1: RE slice, note 07 part A: entities, the generic AI, the dying path, tracking cameras, the ejection seat

**Files:**
- Create: `docs/re/07-ai-and-weapons.md` (the skeleton with sections 1-13 plus Constants, Open questions, Verified by, split Part A (1-6) and Part B (7-13) as note 08 was; you write 1-6)
- Create: `tools/m68k-rig/tests/lists_model/ai_check.py` (the generic AI's state routine and steering against the original over random records), `camera_check.py` (the tracking camera modes against `0x2D8E8` onwards), `seat_check.py`; `tools/vamiga-ref/scripts/combat-ai-*.txt`, `combat-seat-*.txt`, `combat-camera-*.txt`
- Modify: `docs/re/05-flight-model.md` (open questions 3, 6, 17, 19 closed or pointed at 07), `docs/re/06-cockpit-hud.md` (open questions 4, 11), `docs/re/04-scene-composition.md` (lines 704-708: the tracking camera selection), `docs/re/01a-reference-captures.md`

**Interfaces:**
- Produces, for Task 5: the entity records (the 16 records at `0x4B23C`, stride 0x200: every field the AI, the weapons and the scripts read or write, as a table with offset, size, meaning, writers; the active bit, pending and queued flags; the secondary objects at `0x4D23C`: 16 x 0x20, their fields); the spawner `0x2781A` and the template copy from `0x25ECC` (which words are copied, which are computed: stock, rounds, class, model id, throttle, destination/leader from the placement entry's word 2); the world-update order for records 1-14 (`0x2D0AA` every 16th frame, `0x2D53C` every 32nd: the step conversion), the generic AI `0x30A7E` (every state, counter, transition and the random choices with their source), the steering `0x27998` onwards (pursuit, destination and leader following, the altitude/target bytes, the rate tables `0x3132C`/`0x31342`), the AI hook at lift-off `0x1F0E2`, the stale-lock drop `0x26F28`, the non-player dying path `0x2A2C4`-`0x2A2E2` and `0x2D53C` (when a record expires, when `V:4A963` increments, what frees the record), the tracking camera modes 1-4 and 6 (`0x2D8E8`+: selection for weapon and crash follows, the stages, the keys each mode allows) and the fixed camera `0x1E3EE` with its tables `0x47ADE`/`0x47B30`, the ejection: the nibble cycle, the seat record (class 0x30) and its own step, the parachute (class 0x31, `0x2758E`), the W2 0x0020 release, request 2 (`0x27C58`), the hand-over with an ejection nibble, the seat view; every rule tagged with a rig or emulator count.

- [ ] Read the hand-over points in notes 02 (§9, the class table, the model catalogue), 04a (records, secondary objects, requests 0x8C/0x0C), 05 (§3 records, §17.17, open questions 3/6/17/19), 06 (open questions 4/11), 08 (§8.3 the entity step table, §6.1 the placement entries and word 2, §11 the key census); list every address they hand to note 07.
- [ ] Characterise the records and secondary objects with rea; write section 1 (the field table) and section 2 (the spawner and the template copy).
- [ ] Decode the generic AI and the steering; write `ai_check.py` (random records through the note's rules vs `0x30A7E`/`0x27998` in the rig; count); write sections 3-4 with one table per state.
- [ ] Decode the dying path and the expiry; capture a hostile being destroyed in the emulator (a staged fight: the demo's AIM-120 at frames 599-602 is the first fire known; the mission F1 start with a poked hit); write section 5.
- [ ] Decode the tracking cameras and the fixed camera (`camera_check.py`; emulator captures per mode), the ejection (`seat_check.py`; a real ejection captured per blank: Shift+E in flight); write section 6; close the open questions in notes 04/05/06; record captures in 01a; commit per section; report in `.superpowers/sdd/2026-10-14-milestone-5-combat/task-1-report.md` (not git-added).

Acceptance: Task 5 can implement the entities, the AI, the dying path, the cameras and the seat from the note alone; every rule carries a count.

---

### Task 2: RE slice, note 07 part B: weapons, guidance, hits, countermeasures, damage, effects, radar, HUD target data, the trail and the jolt, sound points

**Files:**
- Create: sections 7-13 of `docs/re/07-ai-and-weapons.md` and the Part B rows; `tools/m68k-rig/tests/lists_model/weapon_check.py` (launch, guidance and hit tests against the original), `damage_check.py`, `radar_check.py` (the list builder and threat bits), `hud_target_check.py`; `tools/vamiga-ref/scripts/combat-fire-*.txt`, `combat-hit-*.txt`, `combat-cm-*.txt`, `combat-radar-*.txt`
- Modify: `docs/re/06-cockpit-hud.md` (open questions 1-3, the message-line target data, the range number, the Y pages, entries 8/20/32/34/53 posters), `docs/re/04-scene-composition.md`/`04a` (the hunk-15 entity drawer `0x25820`-`0x25AE4`: the trail segments and the crash/skid use; the draw handlers `0x26944` (damage and explosion state machine), `0x2699E`/`0x26A3E` (cone phases)), `docs/re/01a-reference-captures.md`

**Interfaces:**
- Produces, for Task 6: the gun (`0x173EA` firing, rounds, the hit test `0x2B25A`, the pipper history `0x3878E`); the missiles (the three weapon records, launch `0x275EA`/`0x276A6`/`0x276C6`, the AIM-9L and AIM-120 guidance states, speeds and turn limits, expiry, the hit tests `0x2C02A`/`0x2C040`, the hostile missiles (threat bits 4/5) and the missile hit on the player `0x2C082`); countermeasures (flares `V:4A905`, chaff `V:4A904`, ECM `V:4A96D`/`V:4A8F8`: their effect on each guidance kind, the posts); damage (result 0x40 at speed > 0x3C0, the first-hit engine failure 0x9014, the later hits 0xD00B/0xD00C keyed on `V:4A993`, the cool-downs `V:4A907`/`V:4A906`, the collision boxes, what a damaged aircraft can no longer do); explosions and effects (`0x2AA8E`, records 0x11-0x13 in the effect table, timers `V:4D240`, the draw state machine `0x26944`, the cone phases, the fragments 33-35); the radar list builder `0x1FF24`/`0x29D3A` (entry selection, the detected bit, the 10-blip limit, pens by class, erase lists and schedule, the cone lines) and the threat bits `V:4A926`; target designation (T, `V:4AA78`, `E+0x4A` range and its unit, the lock, the fire prompt condition, the range number `0x367DA` format, the message-line target data `0x36DC2`/`0x36E94` tags, the Y pages `V:4AA7C`); the trail drawer and the jolt kinds 1-4 with their display; the sound trigger points of combat (`0x1A0DC`, `0x1A042`, `V:4A91B`: which sample index each fires, so note 10 and milestone 6 have the list); the log counters' writers (+0x3A gun, +0x3E, +0x3F.., hits, kills).

- [ ] Read the hand-over points (notes 05 §12/§17, 06 §281-321/§419-450/§531-586, 08 §7.3 bits, §10.9, §11); list the addresses.
- [ ] Decode the gun, the missiles and the guidance; write `weapon_check.py` (count); capture fires and hits (the demo's AIM-120; staged shots at a placed hostile); write sections 7-8.
- [ ] Decode countermeasures and damage; write `damage_check.py`; capture a hit on the player and each countermeasure; write sections 9-10.
- [ ] Decode effects, the draw state machines, the trail drawer and the jolt display; write sections 11 and 13 (with notes 04/04a cross-references).
- [ ] Decode the radar list builder, the threat bits, designation and the HUD target data, the message-line data and the Y pages; write `radar_check.py`, `hud_target_check.py`; write section 12; close note 06's open questions; record captures; commit per section; report in `task-2-report.md`.

Acceptance: Task 6 can implement weapons, damage, effects, the radar and the HUD data from the note alone; Task 8 can draw effects, the trail and the jolt; milestone 6 has the sound trigger list.

---

### Task 3: RE slice, note 08 part C: the six missions, the briefing chain, scoring

**Files:**
- Create: sections 12-14 of `docs/re/08-game-flow.md` (12 the mission scripts; 13 the briefing and outcome chain of modes 3-8; 14 scoring and the log counters) and their Part C rows; `tools/m68k-rig/tests/lists_model/mission_check.py` (the dispatcher and the scripts against the original over scripted states), `tools/vamiga-ref/scripts/mission-*.txt` (each mission played to success and to failure with staged events; the briefing chains; the outcome screens)
- Modify: `docs/re/08-game-flow.md` §5.13 (end codes now sourced), §6.1 (the destination/leader encoding of word 2; the region triggers `0x2D22A`/`0x2D116`/`0x2D5D4`), §11.8, Open questions Part A/B (17 etc.), `docs/re/01a-reference-captures.md`

**Interfaces:**
- Produces, for Task 7: per mission (modes 3-8 = F1-F6): the briefing ids, the placement rows and sub-lists, the hostile cap, the script's triggers (region entry/exit, kills vs the cap, time, the pod/airliner/cruise-missile events), the end codes 0xFF/0xFE/0xFD/0xFC and what sets each, the outcome texts and their patches (date 0x5B, bearing 0x58 from `V:4AB7A`), the success test `0x28A96`, request 3 with the delay `V:4A978`, the progress counter `V:4A95F` and the unlock bytes, the "missions started" counter, the cruise missile's and the pod's flows (which mode uses which: reconcile the mode-7/8 question), the briefing state chain (`0x12534` -> `0x12612` -> the camera-mode-2 approach `0x12780` -> `0x1279C` -> the prompt (skipped)) with per-blank tables, the script state across a crash restart, a pause and an ESC, scoring and the log counters' sources, each tagged with a rig or emulator count.

- [ ] Read notes 08 §5.4/§5.13/§6.1/§6.3/§11.8, 02 §9; list the script addresses of hunks 26/27 and the dispatcher `0x28550`.
- [ ] Decode the dispatcher and each mission's script; write `mission_check.py` (the dispatcher over scripted record states vs the original; count); write section 12 with one table per mission.
- [ ] Capture each mission's briefing chain, a success and a failure (staged with pokes where a real fight is impractical; say which); write section 13 with per-blank tables and step conversions.
- [ ] Write section 14 (scoring, the log counters, the mission list unlocking); fix §5.13/§6.1; record captures; commit per section; report in `task-3-report.md`.

Acceptance: Task 7 can implement every mission from the note alone; every end code has a capture.

---

### Task 4: Extractor: the template table, mission and parameter tables

**Files:**
- Create: `tools/extract/extract/combat.py`, `tools/extract/tests/test_combat.py`
- Modify: `tools/extract/extract/cli.py`, `tools/extract/extract/manifest.py` (`EXTRACTOR_VERSION = 14`), `tools/extract/README.md`, `src/core/assets.h` (`CORE_ASSETS_VERSION 14`, same commit as the readers), `src/core/assets_flow.c` or a new `src/core/assets_combat.c` (readers)

**Interfaces:**
- Produces (little-endian; formats per notes 02/07/08): `tables/templates.bin` (the 134 records of `0x25ECC`, 20 bytes each, raw, prefixed `u16 count, u16 record_size`); `tables/missions.bin` (the script tables of hunks 26/27 in the layout note 08 §12 defines, if data-driven; else nothing and a statement); `tables/ai.bin` (every AI and guidance parameter table note 07 names, each sub-table prefixed `u16 count, u16 record_size`); `tables/effects.bin` (the effect templates for records 0x11-0x13 and the fragment timers); the `0x36923`/`0x36929` prompt strings as text records if they are not in `text.bin`; readers `assets_load_templates`, `assets_load_missions`, `assets_load_ai`, `assets_load_effects` with caps, refusals naming the file, zeroed on failure, documented sizes.
- 4a (no dependency): `templates.bin` and the readers; the version bump 14 in the same commit. 4b (after Tasks 1-3): the rest.

- [ ] 4a: write `templates.bin`; synthetic tests (invented records); real-asset structural test (134 records); the reader; version 14. Commit.
- [ ] 4b: write the tables the notes name, their tests and readers; README rows; re-extract the shared assets. Commit per table.

---

### Task 5: Core entities and AI (from note 07 part A)

**Files:**
- Create: `src/core/entities.c`, `src/core/entities.h`, `src/core/ai.c`, `src/core/ai.h`, `tests/test_entities.c`, `tests/test_ai.c`, `tests/rig/test_ai_diff.c`
- Modify: `src/core/scene.h` (once: entities and secondary objects as scene entries with model id, phase and damage state; `effects[]` filled; `radar[]`, `cp_threat`, the target block (`cp_designated`, `cp_range`, `cp_pipper`, `target_box_*`), `jolt` kind 1-4, `sounds[]` with sample index, pitch and volume as the notes give them; `camera.mode` 0-6), `src/core/game.c` (the world update order: the AI tick every 16th frame, the 32nd-frame routine, records 1-14 stepped as AI/weapons/carrier; the placement spawning the placed entities into the records at mission start; the camera modes; the seat), `src/core/screen.c` (the placement variant from the seeded generator), `src/core/flight.c` (`flight_step_entity` branches the note adds; the seat's step; `seat_spawn` creating the records), `src/core/lists.c` (secondary objects into the draw lists per note 04a), `tests/test_screen.c`, `tests/test_training.c` (the lead through the generic entity path where the note says so)

**Interfaces:**
- Consumes: `assets_load_templates`, `assets_load_ai`, `assets_load_scenario`; note 07 sections 1-6.
- Produces: `entity_t records[16]` and `secondary_t objects[16]` inside `game_t` with the original's fields; `entities_spawn(g, placement_entry)` (the template copy; Review Focus 2: the original's allocation rule when all are in use), `entities_step(g)` (the world update order), `ai_step(g, record)` (the generic AI and steering), `entities_kill(g, record, result)` (the dying path, expiry, `V:4A963`), the tracking cameras (`camera_mode`, the follow of weapons and crashes), the seat and parachute records and their steps, the seeded generator `game_random_seed(g, seed)` (frontend-supplied; the golden runner seeds 1).

- [ ] `tests/test_entities.c` first: spawn from a synthetic placement entry copies the note's template words; the allocation rule when all 16 are in use (Review Focus 2); the dying path's expiry step counts; `V:4A963` increments per the note's condition; the seat and parachute records and the hand-over order with an ejection nibble (Review Focus 3, with Task 7's end code: the core-level test lives here).
- [ ] `tests/test_ai.c` first: one test per AI state from the note's tables; steering toward a destination and a leader; the random choices from the seeded generator reproduce the note's distribution rule.
- [ ] Implement; `tests/rig/test_ai_diff.c` (the port's AI vs `0x30A7E`/`0x27998` over random records: 0 differences at the note's count); `make -C tools/m68k-rig test-ai`; the training lead unchanged (test_training, the trajectory test stay green); commit per module.

---

### Task 6: Core weapons, damage, effects, radar and HUD target data (from note 07 part B)

**Files:**
- Create: `src/core/weapons.c`, `src/core/weapons.h`, `src/core/radar.c`, `src/core/radar.h`, `tests/test_weapons.c`, `tests/test_radar.c`, `tests/rig/test_weapons_diff.c`, `tests/rig/test_radar_diff.c`
- Modify: `src/core/game.c` (flight_button: FIRE press/release, SELECT_WEAPON, FLARE, CHAFF, the new buttons ECM (J), RADAR_RANGE (R), TARGET (T), MESSAGE_PAGE (Y), EJECT (Shift+E), POD (Shift+F in mode 6), FIXED_CAMERA (Shift+KP-ENTER); the log counters; the jolt kinds), `src/core/game.h` (the buttons), `src/core/flight.c` (`handle_result` 0x40; the weapon bookkeeping), `src/core/flight_coll.c` (the other-aircraft and missile collision scan with the note's boxes), `src/core/cockpit.c` (the threat bits writer, the message-line pages, the target data line, the weapon readout from the records), `src/core/replay.c` (the R/T/J/Shift+KP-ENTER events now real), `src/core/logbook.c` (the counters' callers), `tests/test_cockpit.c`, `tests/test_replay.c` (the demo's 21 checkpoints must still match exactly with the weapons live: the demo fires an AIM-120 at frames 599-602)

**Interfaces:**
- Consumes: Task 5's records and scene fields; note 07 sections 7-13.
- Produces: `weapons_fire(g)`, `weapons_step(g)` (the three weapon records, guidance, hits, expiry: Review Focus 1), `weapons_select(g)`, countermeasures, `damage_apply(g, record, result)`, effects spawning into the effect table with the note's timers; `radar_step(g)` (the list, the threat bits, erase schedule), `radar_designate(g)`, the HUD target block; the sound triggers with the note's sample indices.

- [ ] `tests/test_weapons.c` first: the gun's rounds and hit test on a synthetic target; each missile's launch, guidance states, expiry with no target / a dead target / a target out of range (Review Focus 1); countermeasure effects per guidance kind; damage results and cool-downs; the effect records and timers; the log counters.
- [ ] `tests/test_radar.c` first: the list from synthetic records (selection, limit, pens, erase schedule), the threat bits, designation and the HUD target block values, the message-line pages.
- [ ] Implement; the rig differentials (`make -C tools/m68k-rig test-weapons`, `test-radar`); test_replay's checkpoints still exact; commit per module.

---

### Task 7: Core missions (from note 08 part C)

**Files:**
- Create: `src/core/missions.c`, `src/core/missions.h`, `tests/test_missions.c`, `tests/rig/test_missions_diff.c`
- Modify: `src/core/screen.c` (the mode 3-8 handler: the briefing chain, the camera-mode-2 approach, the outcome screens with the patched records 0x5B/0x58, the mission list unlocking, menu item 7), `src/core/game.c` (the dispatcher each step, the end codes, request 3 with the delay), `src/core/logbook.c` (`logbook_mission_started`, the counters), `tests/test_screen.c`, `tests/test_logbook.c`

**Interfaces:**
- Consumes: Task 5's entities, Task 6's weapons, `assets_load_missions`; note 08 sections 12-14.
- Produces: `missions_start(g, mode)`, `missions_step(g)` (the dispatcher, the triggers, the end code), the outcome screens through the screen module, the progress and unlock bytes, scoring.

- [ ] `tests/test_missions.c` first: each mission's triggers from the note's tables on synthetic records (success, failure, the 0xFD and 0xFC cases), the script state across a crash restart, a pause and an ESC (Review Focus 5), the end code once, the outcome ids and patches, the unlock bytes and the list, the log counters.
- [ ] Implement; the rig differential; the briefing chain pinned per blank; commit per mission.

---

### Task 8: Frontend: effects, the trail, the jolt, the range number, the remaining keys, the replenish key

**Files:**
- Create: `src/frontend/sdl/effects.c`, `src/frontend/sdl/effects.h`, `tests/frontend/test_effects.py`, `tests/frontend/effects_driver.c`
- Modify: `src/frontend/sdl/video.c` (effects and secondary objects drawn in the original's painter's order per note 04a; the jolt: the 3D window shifted one scanline for four frames per kind), `src/frontend/sdl/panel.c` (the radar erase lists and schedule, the hostile map marks), `src/frontend/sdl/hud.c` (the range number `0x367DA`, the target data line, the Y pages), `src/frontend/sdl/main.c` (keys e with shift, j, r, t, y, Shift+F, Shift+KP-ENTER; the replenish key), `src/core/game.c` (PORT FEATURE: `GAME_BTN_REPLENISH` runs the original's dead rearm body `0x1ECA4`-`0x1ECB4` through `0x1F11E`: fuel to full, stock 0x24, 500 rounds, the weapon records cleared, entry 5 posted; guarded exactly as the body is, plus: ignored in the replay, in training part 1 and while the flight is not running), `docs/manual.md` and `docs/easter-eggs.md` (the key and its origin), `tests/test_game.c`

**Interfaces:**
- Consumes: scene effects, secondary objects, `jolt`, the target block, the radar list.
- Produces: the key map: W = replenish (a key with no handler in the original, note 08 §11's census; chosen because it is free on every keyboard layout the port supports); the effect draw per kind; the jolt; the range number.

- [ ] `tests/frontend/test_effects.py` first: each effect kind draws at its scene position in the note's pens; the jolt shifts the window one scanline for four frames per kind; the range number's format; the demo cockpit capture `demo-cockpit.ppm` now matches to the alert line, blip and lamp (report the remaining count); `tests/test_game.c`: the replenish key's guards (Review Focus 4) and its effect (fuel, stock, rounds, the weapon records, entry 5 once).
- [ ] Implement; `make && pytest tests/frontend -q`; commit per item.

---

### Task 9: The remaining milestone-4 follow-ups

**Files:**
- Modify: `src/core/screen.c` (the placement variant from the seeded generator instead of 0; location key 0's heading per the note if settled, else the NOTE-GAP stays; the demo fade-out level estimate at blank 6 if the note settles the latency), `src/core/logbook.c`/`screen.c` (the 0x5B date and 0x58 bearing patches now sourced by Task 7; the key-6 list gating per §5.4 with the progress bytes), `src/core/screen.c` (the queue depth before the callsign entry: the original's ring of 10), `src/core/flight.c` (the non-player dying path now note 07's), `docs/superpowers/plans/2026-10-14-milestone-4-followups.md` (each item closed with its commit, or carried with its reason)

- [ ] For each follow-up item: implement from the note's rule where a note now states it (cite), else leave the NOTE-GAP with the owning note's open question; tests for each implemented item; commit per item; update the follow-ups document.

---

### Task 10: Goldens, manual, easter-eggs, CI

**Files:**
- Modify: `tests/golden/*` (a mission golden per mission: menu F-key, the briefing chain, a scripted fight with the seeded generator (seed 1), the end code: synthetic goldens committed, real ones local; the demo golden must be unchanged except added columns), `tests/golden/run_golden.c` (columns: records active, weapons, radar count, threat bits, end code, seed), `docs/manual.md` (weapons use, the radar and the target box, countermeasures, damage, ejection, each mission's objective and win/lose rules, the replenish key as a port feature, the controls table), `docs/easter-eggs.md` (the replenish key: the original's dead rearm body; the keys now mapped), `README.md`, `CLAUDE.md`, `.github/workflows/ci.yml` (the new synthetic goldens; the timeouts stay)

- [ ] Goldens byte-stable under tcc and gcc; manual and easter-eggs from the notes and from playing; README status; CI green; commit.

---

## Self-review notes

- Spec coverage: slice 5 (Tasks 1, 2 -> 5, 6, 8), slice 6's mission scripts and scoring (Tasks 3 -> 7), the scene's projectiles/explosions/contacts/target box/sound triggers (Tasks 5, 6, 8), milestone 5's "all six missions" (Task 7, goldens in Task 10), the manual's weapons and missions (Task 10), the milestone-4 follow-ups (Tasks 6, 8, 9), the user's replenish feature (Task 8, documented in Task 10).
- Review Focus 1-5 pinned in Tasks 6, 5, 5/7, 8, 7.
- Names used across tasks: `entities_spawn/step/kill`, `ai_step`, `weapons_fire/step/select`, `damage_apply`, `radar_step/designate`, `missions_start/step`, `game_random_seed`, `GAME_BTN_REPLENISH`, `assets_load_templates/missions/ai/effects`.
