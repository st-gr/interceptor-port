# Milestone 7: Replica closure and release readiness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every open item of the milestone-6 follow-ups document is closed, declared unobservable in its note, or declared out of scope; the repository passes a clean-room scan against the disk; the browser version persists the flight log and is checked in three engines; one script produces the fresh-history public repository with a hosted page.

**Architecture:** Three capture rounds (note 10 sound, note 08 game flow, note 07 combat) run concurrently and produce note updates plus deviation lists; a code round applies them. Independent of the captures: the clean-room residue and the scan tool, the CI coverage gaps (synthetic F5, the device run), the browser persistence and engine checks, and the public export. The final review and fix rounds close the milestone as before.

**Tech Stack:** C99 core and frontend, SDL2, Emscripten 4.0.9 (IDBFS), Python 3 (extractor, scan tool, pytest, Playwright with Chromium, Firefox and WebKit), Node 22, Musashi rig, vAmiga headless driver, the rig's Musashi disassembler on `extracted/interceptor.bin` (rea's Ghidra bridge is failing: NoClassDefFoundError; use rea when it works, the disassembler otherwise).

**Spec:** `docs/superpowers/specs/2026-10-15-milestone-7-closure-and-release-design.md` (this milestone) over `docs/superpowers/specs/2026-10-13-interceptor-port-design.md` (sections 2 constraints, 7 RE workflow, 9 testing, 11 milestones). Binding input: `docs/superpowers/plans/2026-10-16-milestone-6-followups.md` (every item there is assigned below), the ledger `.superpowers/sdd/2026-10-15-milestone-6-sound/progress.md` (git-ignored).

## Global Constraints

- Clean room: notes are prose and tables; no disassembly, decompiler output or assembly mnemonics (describe instructions in prose); test fixtures are invented; nothing under `disks/`, `extracted/`, `assets/` or `manual-en/` is committed; captures are described, never committed; game text is never quoted beyond identification; capture scripts peek, step, press keys and poke run-time state only (no image bytes, no relocated descriptors: use the driver's `copy` from the loaded image); the public web build hosts no asset and no sample.
- Perfect replica: every rule in the code cites its note section; a rule the note leaves open keeps a NOTE-GAP and no test pins the port's choice; a capture that contradicts the port changes the port, the note and the goldens (with the reason in the commit), never the capture's reading.
- Core: C99, fixed-width types, no stdio, no floating point, no OS calls; the W key stays the one PORT FEATURE.
- Determinism: the goldens stay byte-stable; a changed rule regenerates them once with the reason; the six real missions keep 0xFF (re-search the scripts if a rule moves a placement; F1 seed 57, F2 14, F3 127, F4 69, F5 1, F6 629 at this head).
- Private history: no rewrite of the private repository; the public repository is a fresh one-commit export.
- Sandbox: `. /sandbox/.tools/env.sh` first; 2 CPUs and 7 GB shared: `make -j1`, one emulator at a time under `flock /tmp/ref.lock`, Python rig scripts under `ulimit -v 2500000` and `timeout`, one Rig per script, Chromium without `ulimit -v` (it dies under it) with `LD_LIBRARY_PATH=/sandbox/pwlibs/lib PLAYWRIGHT_BROWSERS_PATH=/sandbox/.pw`; `pip install -e tools/extract` only from the main checkout (a worktree install re-points it to a path that is deleted later); commit messages end with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`; explicit-path commits; reports under `.superpowers/sdd/2026-10-15-milestone-7-closure/` (git-ignored); worktree agents reset their worktree to the branch head first; the branch is `milestone-7`.

## Review Focus

1. A capture of this milestone contradicts a rule a golden or a unit test pins (a sound volume, a key timing, the gun order). Expected: the note changes first, the port follows, the goldens regenerate once with the reason, the six missions keep 0xFF or the scripts are re-searched; never a test edited to keep the old rule. Pinned in Task 5 (the deviation list format carries the test that must change).
2. The public export is run on a tree that holds a byte string of the disk (a planted 12-byte window in a scratch copy). Expected: the scan names the file and offset and the export stops with a non-zero status; nothing is written. Pinned in Task 8.
3. The browser's storage is denied or full (private mode, quota). Expected: the page says the log will not persist, the game runs with the in-memory log, nothing else changes. Pinned in Task 7.
4. The device run in CI, where no assets exist. Expected: the synthetic start opens the dummy device, the mixer's callback runs and the run ends cleanly. Pinned in Task 6.
5. The synthetic F5 under tcc and gcc. Expected: the invented missile flies, the end code is 0xFF at the same tick under both compilers. Pinned in Task 6.

## Task dependency graph (for parallel execution)

```
Wave A (concurrent): Task 1 sound captures (note 10) · Task 2 game-flow captures (note 08) · Task 3 combat closure (note 07, rig with the whole state) · Task 4 clean-room residue and the scan tool · Task 6 tests and CI coverage · Task 7 browser persistence and engine checks
Wave B (after 1, 2, 3): Task 5 the code round (deviation lists applied, NOTE-GAPs removed or restated, goldens)
Wave C (after 4, 5): Task 8 the public export and the hosted page
Wave D: Task 9 docs, the milestone-7 follow-ups document, the final whole-branch review, merge
```

---

### Task 1: Sound captures (note 10)

**Files:**
- Create: `tools/vamiga-ref/scripts/sound7-*.txt` (one script per item below), `.superpowers/sdd/2026-10-15-milestone-7-closure/task-1-report.md`
- Modify: `docs/re/10-sound.md` (sections 5, 7.4, 8.2, 8.3, 9.1, Open questions 3, 4, 7), `tools/m68k-rig/tests/lists_model/sound_check.py` (a `restart` part: the menu from a post-flight snapshot), `tests/rig/test_sound_diff.c` (the captures part reads the new logs when present)

**Interfaces:**
- Produces, for Task 5: a deviation table (item, the note section as corrected, the rule, the port's current behaviour, the file and the test that must change), and the capture logs under `extracted/captures/logs/sound7-*.txt` (git-ignored).

- [ ] The uncaptured sounds of Open question 7, one capture each with `audtrace on` and the comparison against the port's `--audio-log` of the same scripted keys: the explosion bang with its distance quirk (a gun kill of a MiG in view; the demo's kill at pass 0x28B is the cheapest path), two gun bursts more than 28 blanks apart, the gun shot outside (volume 30), the touchdown volumes 0x1E and 0x28, the gear sound's outside volume with V:4A86D clear, the bump 0x2A834, the cue bits 0x40, 0x01, 0x10 and 0x800, an overwritten pending bang, and the pod release (SHIFT-F in mode 6: makes its 8.2 row V). For each: the capture's periods, volumes and blanks against the port's; state the count; a difference is a deviation row.
- [ ] The pause clicks (the typed pause text) and the P beep (7.4, 8.3): the capture named in `final-fix-b-report.md` (the audio trace per CPU write with a run-to stop at 0x376F8): settle the attribution; the wrappers-called-later case (`sound.c:218`): a click and a wrapper call in one step.
- [ ] V:4AA6E for the seat and the fixed camera (`game.c:2193`): a wrapper call during the seat and under the fixed camera; the sound routine in the approach cameras (5.7 items 8 and 12, V:4ABFA): the routine's entry logged through an approach.
- [ ] The seconds tick (`game.c:1370`, `1817`): a long capture (120 s) logging V:4ABAA against the mission clock; state the phase rule or the unobservability.
- [ ] The music restart after a flight: `sound_check.py restart` from a post-flight snapshot (rig only), the chain's first blocks compared with the port's after a flight end; the music-less machine (Open question 3) stays as the model states unless the rig shows a path.
- [ ] Write the sections; every closed item tagged V with the capture name; the deviation table in the report; commit the scripts and the note by explicit path.

Acceptance: Open question 7 is empty or lists only what no script can reach (with the reason); every remaining NOTE-GAP of note 10's owner list in the code is either removed by Task 5 or restated as unobservable.

---

### Task 2: Game-flow captures (note 08)

**Files:**
- Create: `tools/vamiga-ref/scripts/flow7-*.txt`, `.superpowers/sdd/2026-10-15-milestone-7-closure/task-2-report.md`
- Modify: `docs/re/08-game-flow.md` (5.6, 5.8, 6.1, 7.7, 12.14 N5 and C13, section 12 C2 to C8, N11, C12, 3.8), `docs/re/06-cockpit-and-hud.md` or wherever the start table lives (the approach rows), `tools/m68k-rig/tests/lists_model/*.py` (a `mover` part if the mover's camera path is modelled in the rig)

**Interfaces:**
- Produces, for Task 5: the deviation table as in Task 1.

- [ ] Keys 7, 8, 9 and 0 (the out-of-table starts): four captures counting the approach passes (V:4A900 and the pass counter), against the port's 118.
- [ ] The dispatch-to-briefing timing (`screen.c:787`) and the first start through a countercode: a capture from the dispatch with the `code @disks/countercodes.txt` answer, the blank counts per state; the typing rate of F3 to F6 (the per-record typing durations of the six briefing pairs from the existing captures or new ones) against 3.8's rule.
- [ ] The key-latch long passes (5.8): a key-per-step census on the location screen's first passes after a state change and the gate-1 passes of the aircraft path (count keys stored per step over 40 steps with a key every step), against the port.
- [ ] The mover's flight before the location screen (`game.c:1998`): the mover's position per pass over those passes.
- [ ] The approach camera path (N5, C13): the mover's stage table and call count: model it (the HUD step and the window row per approach pass, the selection screens' 50/179 against the port's 0/144), with the rig (`mover` part, the stage table's rows reproduced) and one capture per start kind (carrier, airfield 1, the training dispatch rows 167 for modes 2 and 0x7D).
- [ ] C2 to C8, N11, C12: the F1 contact, F2 by the AI's landing, the F6 victory count, the seat's request order, a crash restart in a mission (does it clear V:4A927), F3 failure and shadowing, F5 by a real shoot-down, F1's variants 1 and 2 of the visibility rule: one scripted run each where the mission scripts of `tests/golden/` give the path (the port's run first to find the tick, then the emulator with the same keys); the pause byte in the outcome screens.
- [ ] Write the sections and the deviation table; commit.

Acceptance: 12.14 lists the approach rows per start kind; 5.8 has no unmodelled kind or states why; section 12's C items are each V or unobservable.

---

### Task 3: Combat closure (note 07, the rig with the whole state)

**Files:**
- Create: `tools/vamiga-ref/scripts/combat7-*.txt`, `tools/m68k-rig/tests/lists_model/state_dump.py` (writes the state hunk and its neighbours from the emulator's `peek` log into `extracted/captures/state-flight.bin`, git-ignored), `.superpowers/sdd/2026-10-15-milestone-7-closure/task-3-report.md`
- Modify: `docs/re/07-ai-and-weapons.md` (7.7, 8.2 (b) and (c), 11.4, 12.1, 12.5, Part A's unreachable items, model 11), `tools/m68k-rig/tests/lists_model/weapon_check.py` (a `whole-state` part loading the dump at the original's addresses), `tests/rig/test_weapons_diff.c` (the same, in C), `tools/extract/extract/combat.py` and `tools/extract-js/src/combat.js` (the plane-count-0 node's block, extractor version 26, `CORE_ASSETS_VERSION` 26, both extractors byte-identical on the synthetic disk), `tools/extract/tests/synth.py` (an invented node with plane count 0)

**Interfaces:**
- Produces, for Task 5: the deviation table; for Task 4: nothing (the extractor change is this task's, with its fixtures).

- [ ] The gun order in external views (12.5): a capture that logs the whole pool (slots 10 to 19) at a gun kill in an external view (the demo's kill with the view switched, or a scripted free-flight kill); state the order; a deviation row if the port's captured order differs.
- [ ] The landmark junk-model and beyond-hunk cases (8.2 (b), (c)): dump the state in flight (`state_dump.py`), load it in the rig at the original's addresses, run the 318 junk-model leftovers and the alias cases against the port with the real bytes (`weapon_check.py whole-state`, then the C part); the beyond-hunk addresses: what the emulator reads there (a `peek` of the range in flight) and whether any is non-zero; write (b) and (c) as settled or unobservable.
- [ ] The plane-count-0 node (S10): the block in both extractors, the synthetic node, a real-table landmark rig case through that node (0 differences).
- [ ] The chaff burst's pixels (7.7, 11.4): the camera logged before the draw in a capture that keeps the slot near (the fixed camera on the player), the pen-13 pixels found or the rule corrected.
- [ ] The pass-93 write (12.1): a trace of the writes to V:4A8EE around pass 93 (`audtrace`-style per-write log is for audio; use the driver's `watch` if present, else `peek` per frame plus a `runto` on the writer's address found with the disassembler); the external-view panel band (pen 12 from row 180) traced.
- [ ] Model 11's slot case, the trailing-slot data triple, F6's fourth count (a run that shoots the submarine), A2/A3/A5 with a poked run-time state (the class 0x15 skip, the W0 bit 1 case, the lost-target loop, the rescue mission's pod): one capture each where a run-time poke reaches the path; the target index of 16 or more stays unreachable (R).
- [ ] Write the sections and the deviation table; commit (extractor 26 with its tests and fixtures in its own commit).

Acceptance: note 07 Part B has no item whose evidence is "a capture would settle it" without the capture or a stated reason it cannot be made.

---

### Task 4: Clean-room residue and the scan tool

**Files:**
- Create: `tools/cleanroom/scan.py` (the window scan: every tracked file of the repository against `assets/` and `extracted/interceptor.adf` and `extracted/interceptor.bin`; 12-byte windows with at least 6 distinct bytes; an allow list `tools/cleanroom/allow.txt` with one line per allowed match: file, offset, reason), `tools/cleanroom/README.md`, `tools/cleanroom/tests/test_scan.py` (a planted window in a scratch tree is found; the allow list suppresses it only with a matching reason line)
- Modify: `tools/extract/extract/cockpit.py` (`check_mapshapes`: the row compare replaced by a structural check plus an FNV-1a hash of the 17 shapes' bytes against one constant; the synthetic disk carries an invented shape set and the test checks the hash path with the invented hash), `tools/extract-js/src/cockpit.js` (the same), `docs/re/06-*.md` (the rows of the marks replaced by the hash and prose: "five rows, the widest at the bottom"), `src/core/missions.c` (the briefing-chain id table: loaded from an extracted table `tables/briefing_chain.bin` if it is disk data, else the note's rule in prose), `tests/world_synth.h` (`tiles[21][2]` and `lim[8]` invented), `src/core/world.c` (the step table: from `tables/` or the rule), `docs/re/06-*.md`, `docs/re/09-*.md` (the prompt words), `docs/re/08-game-flow.md` (the month name, the config file fields), `docs/re/01-*.md`, `02-*.md`, `04-*.md`, `08-*.md` (the old mnemonics), four capture scripts (the relocated-descriptor pokes replaced by the driver's `copy` from the loaded image), `tools/extract/extract/*.py` (the `extracted/captures/logs/image-static.txt` dependency replaced by `tools/extract-js/segments.txt`'s rule, both extractors byte-identical)

**Interfaces:**
- Produces, for Task 8: `tools/cleanroom/scan.py --repo DIR --assets assets --disk extracted/interceptor.adf --bin extracted/interceptor.bin` exits 0 with no unallowed match, 1 with a list (file, offset, length, the asset or image offset it matches).

- [ ] Write `test_scan.py` (a scratch tree with one file holding 12 bytes copied from a scratch "asset" file; the scan lists it; with the allow line it passes; an allow line without a reason is an error), run it (fails), write `scan.py`, run it (passes), commit.
- [ ] Run the scan on the repository (locally, with the assets); for every match decide: invent (the fixture or table), extract (a table the extractor should carry), or allow with the reason (counting sequences, the sound-table scaffolding of the extractor's own constants); apply each; commit per file group with the reason.
- [ ] The map-shapes validator: the hash check and the invented synthetic shapes in both extractors and `synth.py`; the note 06 rows replaced; `pytest tools/extract`, `node --test`, the byte-identity test; commit.
- [ ] The S10 carries: each replaced as listed in Files; the four scripts' pokes replaced by `copy`; re-run the affected captures' comparison if any (Task 1 to 3 agents own the emulator; this task only changes the scripts' way of loading, verified by one run of each script under the lock); commit.
- [ ] The Python extractor's `image-static.txt` dependency removed; commit.

Acceptance: `scan.py` exits 0 on the tree with the allow list, every allow line has a reason, and the allow list has no entry for a value a note or test asserts.

---

### Task 5: The code round (after Tasks 1 to 3)

**Files:**
- Modify: `src/core/*.c`, `src/frontend/sdl/*.c` per the three deviation tables (each change cites the note section), their tests, `tests/golden/*-synthetic.golden` (regenerated once per changed rule with the reason), `tests/golden/mission-f*.txt` (re-searched if a placement moved), `.superpowers/sdd/2026-10-15-milestone-7-closure/task-5-report.md`

**Interfaces:**
- Consumes: the deviation tables of Tasks 1, 2 and 3 (item, section, rule, current behaviour, file, test).
- Produces: closures for Task 9's follow-ups document.

- [ ] For each deviation row: write the test from the note's rule, run it (fails), change the code, run it (passes), commit per row with the section in the message.
- [ ] For each item the captures settled as unobservable: the NOTE-GAP comment restated with the note's wording and the reason; no test pins it; commit.
- [ ] For each item settled as V without a deviation: the NOTE-GAP removed, the comment cites the section; commit.
- [ ] `make golden-update-real`, `make golden` 20 of 20; the six missions' end codes 0xFF (re-search per the golden-research method if lost); `make -C tools/m68k-rig test` 0 differences; commit the goldens with the reason.

Acceptance: the NOTE-GAP inventory of `src/` lists only items the notes state as unobservable or unreachable in shipped data.

---

### Task 6: Tests and CI coverage

**Files:**
- Modify: `tests/world_synth.h` (invented physics tables so a missile flies: the AI launch and the missile step reach a hit; F5's synthetic run ends 0xFF), `tests/golden/mission-f5.txt` (`sat` lines of the synthetic fight), `tests/golden/mission-f5-synthetic.golden` (regenerated with the reason), `tests/frontend/test_audio_main.py` (a `--synthetic` start for the device run: the frontend's `--flight --synthetic` opens the dummy device with the invented world; the module-level skip removed; the tests check the callback ran and the run ended cleanly), `src/frontend/sdl/main.c` (`--synthetic`: the frontend builds the invented world of `tests/world_synth.h` through a core entry `game_init_synthetic` already used by `run_golden`, or the same tables compiled into a test-only object), `tests/test_training.c` or the synthetic world (an invented training table so the S and RETURN beep tests run without assets), `.github/workflows/ci.yml` (nothing new unless a job needs the flag)

**Interfaces:**
- Consumes: `run_golden`'s synthetic directives (`synthflight`, `synthfrom`, `sat`).
- Produces: `build/interceptor --synthetic` for Task 7's browser checks without assets if useful.

- [ ] Write the F5 synthetic test: the golden's end column reaches 0xFF; run (fails: 0xFE); invent the physics rows (the missile's speed and the hit radius in `world_synth.h`, cited as invented); run (passes); `make -j1 CC=tcc golden` and `make -j1 golden` byte-identical; commit.
- [ ] Write the device test without assets (Review Focus 4): `build/interceptor --synthetic --flight --frames 40` under `SDL_AUDIODRIVER=dummy` exits 0 and `--audio-log` shows writes; run (fails: no flag); implement `--synthetic`; run (passes); the module-level skip removed; commit.
- [ ] The training beeps in the synthetic world: an invented training table (the lesson rows the beep tests need) in `world_synth.h`; the two tests run in CI; commit.
- [ ] `test-sound-captures` stays skipped without the logs (documented); `pytest` and the CI jobs green on the branch (push and read `ci-results`).

Acceptance: CI runs the F5 success path, the device run and the training beeps without assets.

---

### Task 7: Browser persistence and engine checks

**Files:**
- Create: `docs/browser-checklist.md` (the checks a person makes in a real browser: F5, F6, F11, F12 and F1 where the browser owns them with the = and - alternatives, audio in Chromium, Firefox and Safari including Safari's AudioContext resume, a physical controller; one line per check with the expected result)
- Modify: `src/frontend/web/shell.html`, `page.js` (IDBFS: the log directory mounted on an IDBFS at start with `FS.syncfs(true)` before `callMain`; a storage error shows one line and falls back; the save points call `web_log_saved()` which the core's save hook triggers, the page runs `FS.syncfs(false)`), `src/frontend/sdl/main.c` (`web_log_saved` exported, called after every log save in the web build; `WEB_EXPORTS`), `Makefile` (`-lidbfs.js`), `tools/web/smoke.py` (three engines: Chromium, Firefox, WebKit, each the same run; a reload test: the log written in one visit is read in the next; an audio check in Chromium with `--autoplay-policy=no-user-gesture-required` and the fake audio output: after Start the AudioContext state is running and the ScriptProcessor callback count grows; a gamepad check: a page script replaces `navigator.getGamepads` with a fake pad whose button 0 is pressed at a frame, and the run leaves the credits as the keyboard does), `tests/frontend/test_web_build.py` (the exports and the IDBFS flag in the build), `docs/manual.md` ("In the browser": the log persists per browser profile; the checklist pointer)

**Interfaces:**
- Consumes: `Module.onGameEnd` (milestone 6), `web_audio_unlock`.
- Produces: `web_log_saved`, the smoke run's three-engine report.

- [ ] Write the reload test in `smoke.py` (fails: no persistence); mount the IDBFS and wire `web_log_saved`; run (passes); commit.
- [ ] Review Focus 3: a Playwright context with storage denied (an injected `indexedDB.open` that throws) shows the message and the run proceeds; test and commit.
- [ ] The engine matrix: `python -m playwright install firefox webkit` (the sandbox's `pwlibs` may need more shared libraries from conda-forge: install them with micromamba into `/sandbox/pwlibs`, document the list in `tools/web/README.md`); the smoke run in all three; the audio and gamepad checks; the report lists per engine what passed; commit.
- [ ] The checklist document; the manual; commit.

Acceptance: the smoke run reports three engines, the reload test and the audio and gamepad checks; the checklist names only what headless cannot do.

---

### Task 8: The public export and the hosted page (after Tasks 4 and 5)

**Files:**
- Create: `tools/release/export-public.sh` (from `main`: `git archive` of the tree into `/sandbox/interceptor-public/` (a path argument), minus `.superpowers/`-style ignored material by construction; adds `README.public.md` as `README.md`, `LICENSE`, `.github/workflows/pages.yml`; runs `tools/cleanroom/scan.py` against the assets and the disk and stops on any match; `git init`, one commit "Interceptor port: initial public release" with the Co-Authored-By line, `git bundle create`), `tools/release/README.public.md` (what it is, what it is not (no asset, no disk; the player's own disk image is extracted in the browser), how to play (the hosted page, the desktop build), the status table of the milestones, the clean-room statement, the trademark and copyright notice: F/A-18 Interceptor is Electronic Arts' title, the port holds none of its data), `tools/release/pages.yml` (GitHub Pages: on push to main, `make web` with emsdk 4.0.9, deploy `build/web`), `tools/release/tests/test_export.sh` (Review Focus 2: a scratch tree with a planted window makes the export fail with the file named and no output directory; a clean scratch tree exports with the three added files and one commit)
- Modify: `README.md` (a "Public release" section pointing at the script), `CLAUDE.md` (the export command), `LICENSE` (GPL-3.0, the user's decision of 2026-10-15; the `tools/vamiga-ref/` tooling note of the README stays: vAmiga is GPL-3 too; the extractor and the port code carry the GPL-3.0 header reference in their READMEs, no per-file headers)

**Interfaces:**
- Consumes: `tools/cleanroom/scan.py`'s exit code.
- Produces: `/sandbox/interceptor-public/` and `interceptor-public.bundle` for the user to push.

- [ ] Write `test_export.sh` (fails: no script); write the script; run (passes); commit.
- [ ] Run the export on `main` after the merge of Task 5 and 4 (locally); the scan passes; the bundle's one commit builds (`make -j1`, `make -j1 test`, `make web`) from a clean clone of the bundle; the Pages workflow is validated by `act` if available, else by reading; commit the tooling.
- [ ] The public README and the licence (GPL-3.0: the full text as `LICENSE` in the private repository too, since the tree is what the export copies; the README's "licence not decided" paragraph replaced); the trademark notice; commit.

Acceptance: a clean clone of the bundle builds and tests green without assets, and the scan reports no match.

---

### Task 9: Docs, the milestone-7 follow-ups document, the final review

**Files:**
- Create: `docs/superpowers/plans/2026-10-16-milestone-7-followups.md` (every item of the milestone-6 document with its state: CLOSED (commit, section), UNOBSERVABLE (note section, what would be needed), OUT OF SCOPE (reason); the NOTE-GAP inventory regenerated; the hand-over to the Three.js frontend carried forward)
- Modify: `docs/re/README.md` (the notes' open-question lists updated), `docs/manual.md`, `docs/easter-eggs.md`, `README.md` (status: milestone 7; the public release), `CLAUDE.md` (the scan, the export, `--synthetic`, the three-engine smoke run), `.github/workflows/ci.yml` (the scan tool's unit test in the Linux job; the scan itself never runs in CI)

- [ ] The follow-ups document from the task reports; commit.
- [ ] The docs; commit.
- [ ] The final whole-branch review (Opus, worktree): clean room (the scan's allow list reviewed line by line), perfect replica (every NOTE-GAP against its note), acceptance of Tasks 1 to 8, the export's content, the docs; fix rounds until no must-fix; merge `milestone-7` to `main`; CI green; the memory file updated.

Acceptance: the milestone-7 follow-ups document lists no item without a state, and the review's must-fix list is empty.

---

## Self-review

- Spec coverage: section 2's three states (Tasks 5 and 9), section 3's captures-first (Tasks 1 to 3 then 5), the whole-state rig (Task 3), the scan (Task 4, gating Task 8), persistence (Task 7), the export (Task 8); section 4's non-goals (the checklist of Task 7 lists the physical checks; no Three.js task).
- Every open item of the milestone-6 document: note 10's list (Task 1; the 49.92 Hz, output level, filter, live-play rules, textegn2 and the loader's defaults are port choices or unused code: Task 9 records them as OUT OF SCOPE with the reason), note 07's (Task 3), note 08's (Task 2; the demo ESC levels 13/14 UNOBSERVABLE), cockpit and extractor (Task 4; the blank-code bit and the burst pen assignment UNOBSERVABLE in shipped data), tests and CI (Task 6; `test-sound-captures` documented), the browser (Task 7; the physical controller and hardware audio in the checklist).
- Type consistency: `web_log_saved` (Task 7) and `web_game_end` (milestone 6) follow the same export pattern; `--synthetic` (Task 6) reuses `run_golden`'s world; extractor 26 (Task 3) and the hash validator (Task 4) both change `synth.py`: Task 4 merges after Task 3's extractor commit or rebases on it.
- Review Focus: each line has its owning task and test.
