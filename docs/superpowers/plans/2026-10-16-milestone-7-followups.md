# Milestone 7 follow-ups

What the closure and release milestone did with every item of `2026-10-16-milestone-6-followups.md`, the items it leaves, the
NOTE-GAP inventory of `src/` and `tests/` as grepped at this head, the port choices by symbol, and the hand-over to the Three.js
frontend. Earlier milestones' closed items are in the milestone 4, 5 and 6 documents in this directory. Format: CLOSED (commit,
note section) / UNOBSERVABLE (owner note section, what would be needed) / OUT OF SCOPE (the reason). "R" means read from the code
and not run, "V" verified in the emulator or the rig, "C" a port convention. Every milestone-6 item has one state below; a state
that moved a gap into a new, narrower question lists the question again under "New open items".
The statements held at the head of the Task 5b merge, `cf97aae`, plus the Task 9 documentation commits (`5b9391f`, CI green there:
linux, windows, macos, extract-js and web all success). The final whole-branch review's fix round (agents A and B, commits named
below) changed the items marked "final fix A" and "final fix B"; the commits are named where they apply (`79a0eac` the missions, `d69dde7` the outcome resume, `ae84d18` and `9d7167e` the two tests' tags).

## Milestone 7 in numbers

Nine tasks: the sound captures (Task 1, note 10), the game-flow captures (Task 2, note 08), the combat closure (Task 3, note 07,
the extractor's version 26 and the rig with the whole state), the clean-room scan and the residue (Task 4), the two code rounds for
the deviation tables (Task 5a for note 10, Task 5b for note 08), the tests and CI coverage (Task 6), the browser persistence and the
engine matrix (Task 7), the public export (Task 8) and the documentation round (Task 9, this document).

Tests at this head (counted here, tcc and gcc builds, assets of extractor version 26 symlinked in the worktree):

- `make CC=tcc core test`: 26 test binaries, 945 test cases, 0 failures (the figures are those of `make CC=tcc core test`; the
  gcc `make test` reports 0 failures too).
- Golden: 20 of 20 (ten synthetic, committed; ten real, local and git-ignored): takeoff-and-circuit, demo, training,
  qualification and the six missions. Under the real assets the six missions end in the success code 0xFF at (seed / tick of the
  end code): F1 57 / 1482, F2 14 / 3292, F3 127 / 8470, F4 69 / 2084, F5 1 / 907, F6 629 / 8819 (milestone 6 had 1477, 3280,
  8475, 2059, 883, 8802: the click cost 5,350 of 5b moved F2 to F6 by 1 to 3 ticks and the scripts' "at" lines by +0, +2, +3, +2,
  +1, +3 steps, so 1477, 3282, 8478, 2061, 884, 8805 at the head of 5b; the final round's outcome resume and external mission
  approaches then shifted them by +5, +10, -8, +23, +23, +14 ticks; the seeds are unchanged). The synthetic goldens now fail `make golden` if a mission never reaches 0xFF, and all six
  do, F5 included (Task 6).
- `pytest tools/extract tools/hunk tests/frontend tools/cleanroom`: 734 passed, 1 skipped (with the real assets symlinked in; 17 of them in `tools/cleanroom`). The device tests of
  the audio main run without assets now (`--synthetic`); a tree without assets skips the real-asset ones with their reason.
- `node --test "tools/extract-js/test/*.test.js"`: 87 tests (86 and the log-open timeout test of final fix B; the synthetic disk gives the same files as the Python extractor; the
  real disk 226 files at extractor 26, byte for byte).
- `bash tools/release/tests/test_export.sh`: ok (a clean tree gives one commit and the bundle; a planted 12-byte window stops the
  export with the file named and writes nothing).
- The rig: the parts the milestone touched ran at 0 differences in the task reports (the whole `make -C tools/m68k-rig test` was not
  re-run for this document); the additions of the milestone: `gunorder` 2,000 pools,
  `lmnode` 400 cases (the zero plane fails 365 of 400), `lmstate` 126 real and alias landmark cases (74 hits) and 348 junk-model
  cases equal to a skip, the slots and parachute handlers run through the original's own code (6,000 + 3,000 cases), the sound
  restart (`sound_check.py restart`, 10 cuts), the sound model and core unchanged (102,113 comparisons), the mission and training
  differentials unchanged after the code rounds (8,000 mission states, 20,000 training cases).
- Captures: 71 new scripts in `tools/vamiga-ref/scripts/` (21 `sound7-*`, 32 `flow7-*`, 18 `combat7-*`, with the generators
  `gen-sound7.py`, `gen-flow7.py`, `gen-combat7.py`), run one at a time under the emulator's lock. The port's side of the sound
  comparisons is `tools/vamiga-ref/sound7_port.c` and `sound7_check.py` (hooked into `make -C tools/m68k-rig test-sound-captures`):
  bang 120 of 120 blanks (cockpit and outside), the overwritten bang 80 of 80 and 96 of 96, the gun bursts 108 of 116 (the rest one
  unit off in the tail), the cues 394 of 394, the pod release 48 of 48, the seconds tick 13 of 13 decrements, the pause line's
  nine click starts equal, the SHIFT-ESC restart with no click before the stop (30 click starts after it in the capture, 26 in the
  port, 17 equal to the blank).
- Clean room: `tools/cleanroom/scan.py` exits 0 on the tree (re-run at this head: 72 s); the allow list has 16 lines (`allow.txt`: the two pointer-table lines of the synthetic disk are pinned to their offsets and run lengths since final fix B); the extractor
  went from version 25 to 26 (the plane of the count-0 node); the map-shapes validator checks structure and one FNV-1a hash.
- The browser: the flight log persists (IDBFS at `/log`); the smoke run checks pick, start, keys, reload, nostorage, gamepad and
  (Chromium) audio in three engines; only Chromium runs them all here (see the open items).

## Milestone 6 items and their states

### Note 10: the sound

- CLOSED (95340ef, 6301239, c60d3b0; note 10 8.2, OQ 7): the explosion bang and its distance quirk: d = 0x78 in all five dying
  cycles, first volume 30 in the cockpit view, 60 outside, 121 blanks; the port's curve equals it for 120 of 120 blanks (V,
  `sound7-bang`, `-bang-ext`). Other values of d (an exact division below 0x78) never occurred; rig arithmetic (R).
- CLOSED (same commits; 8.2): an overwritten pending bang (the later writer's arguments win, 80 of 80 and 96 of 96 blanks), two gun
  bursts more than 28 blanks apart (the second at period 280), the gun shot outside (volume 30): V (`sound7-bang-overwrite`,
  `sound7-gun`).
- CLOSED (same commits; 7.2): the music restart after a flight: the chain texti0a x1, texti0b x1, texti1 x2, ... at period 358,
  volume 31, equal to the rig's restart after a cut (`sound_check.py restart`, 10 cuts, 0 differences) and to the port.
- CLOSED (95340ef, 6301239; 7.3, 7.4, 8.3): the clicks of the pause and the P beep: the P handler plays nothing; the beep is the
  typed pause text's clicks, called from the typed-text engine; nine click starts at +13, +16, +18, +20 ... +30 blanks after the
  stop (the first character requests twice, three blanks apart on this screen), caller 0x37B18 (9 hits). The attribution that the
  milestone-6 document called inferred is V now. The port follows it since 5b (e35db4e).
- CLOSED (95340ef; 8.2): the ground-contact touchdown volume 0x1E (period 310, 15 blanks, V, `sound7-landing`), the gear sound's
  outside volume with V:4A86D clear (no sound starts in the fixed camera; in the ring camera 700 falling to 595, volume 10, V),
  the cue bits 0x10, 0x800, 0x40 and 0x01 (394 of 394 blanks, the hold by the cool-down 107 and 136 blanks, V), and the pod release
  (48 of 48, V).
- UNOBSERVABLE (note 10 OQ 7 and 8.2): the touchdown volume 0x28 and the bump 0x2A834. The descent the step compares is the
  record's smoothed velocity, not a word a poke can set, and the next step puts a poked altitude back (five stagings of the dive
  snapshot and the carrier deck tried, listed in 8.2). Needed: a flown hard landing without a crash at a descent of 0x181 to
  0x23F per step, or a way to set the program counter of the emulator for a state that has just left the player's step. Rig
  arithmetic (400 wrapper runs, R) stands and the port plays both from the same tables.
- CLOSED (d8e87a2, 6301239; note 10 5.1 item 2, 5.7 item 12): the V:4AA6E question and the NOTE-GAP at `game.c:2193`: the gear
  wrapper's camera test is true for every camera, so the seat plays the gear sound (volume 10, 700 falling to 595, V
  `sound7-seat`); the fixed camera has V:4A994 = V:4A996 = 0 and the wrapper is never reached (V). Test in `test_game_sound.c`.
- CLOSED (95340ef, 6301239; 5.7): the sound routine in the approach cameras: entered in every pass, gated by V:4A966 = 6 in the
  first 125 passes (V:4ABFA read by none), 2 + 6 ungated passes, then V:4A84D clear (V, `sound7-approach`). The port's structure
  equals it.
- CLOSED (4eec32d; 5.1 item 2, `strain_pass`): the head hold decrements in the seconds-tick pass with the gear byte and the
  cool-down (V, 5 of 5; the NOTE-GAP at `game.c:1370` removed); the radar-page countdown rides the same block (R); the strain
  counters tied to V:4ABAA do not run in a pause (note 06 'The strain second').
- UNOBSERVABLE (note 10 5, 5.1): the phase of the seconds clock against the mission (the NOTE-GAP at `game.c:1817`, restated and
  then removed): the original's second is the system clock's, 119 changes in 5,896 frames (49.97 a second), arbitrary against the
  mission start (V, `sound7-tick`). Needed: nothing a capture can add; the port's eight steps a second are a convention (C).
- CLOSED (4eec32d, 95340ef; 8.3): wrappers called later in a step that already made clicks: the clicks come first, the wrapper
  (stop-all and the music restart) one blank after the last click and never before (V, `sound7-restart`); the NOTE-GAP at
  `sound.c:218` is gone; the blank itself is the cost model (C).
- CLOSED (95340ef, 6301239, c60d3b0; OQ 4): the phase of a pass inside a blank: the per-write traces carry raster line and colour
  clock; a pass has a free phase inside the blank and the first period and volume follow at the next server. The port still
  quantises to blanks (10 9.1, C); no rule depends on it.
- UNOBSERVABLE (note 10 OQ 3): the music-less machine: the loader's memory test reads the host's free chip memory; every capture
  had 512 KB and the music; no script can choose another size. The port keeps the test as true. Needed: another machine
  configuration in the reference emulator.
- OUT OF SCOPE (port choices, no original to measure): the 50 Hz blank clock against the captures' 49.92 Hz (313-line frames; only a
  decision to use the 313-line blank as the clock settles it); the output level (the recorder's gain is about 140 times lower than
  the port's full scale and the x2 voice gain of the mixer, note 10 9.2 (h)); the filter on by default (the game never writes the CIA
  or ADKCON, the corners are vAmiga's); the live-play rules of 9.2 (lead of 3 blanks, skip beyond 5 queued, trim, hold on underrun,
  the `--wav` block of 125 ms), which have no original (the original has no such buffer).
- OUT OF SCOPE (unused code and loader defaults): `textegn2` (never loaded while `textegn` exists), object 7, the shut-down
  0x553D8, the free-all 0x558F6 and the waveform builder 0x559C0 (never reached; the port has none), the seat tone, the engine
  pitch mid-range files (engine 650, wind 391) and the per-file rates (the loader's defaults or the first play's period; the
  engine file's middle period is arbitrary in the original, 2.4).

### Note 07: combat

- CLOSED (aa080f8, 8cbf117; note 07 12.5, 7.3): the gun order in external views: the scan 0x2AD18 decides on the highest active
  slot; a gun kill of a designated target posts 0x4016 in the pass of the kill when the pool held an active slot at the scan, and
  one pass later with no post when it did not (whole-pool captures `combat7-gunorder`, `-gunorder1`, `gunorder_check.py`, rig part
  `gunorder` 2,000 pools, 0 differences). The NOTE-GAPs at `game.c:2107` and `scene.h:392` are gone. One residue is a new
  narrower question (`weapons.c` `scan_abs8`, below).
- UNOBSERVABLE (note 07 8.2 (b), (c); `weapons.c`): the landmark junk-model and beyond-hunk cases. The outcome depends on Kickstart's
  exception vectors (pointer 0), the loader's hunk placement and the RAM beyond the program (2,614 of 2,700 beyond-hunk pointer
  reads were non-zero; 2,078 of the 2,426 enterable ordinals differ from a skip, 1,036 never return). The whole-state rig (part
  `lmstate`: 126 real and alias cases, 74 hits, 348 junk cases equal to a skip) bounds it and the port keeps the skip. Needed: the
  original's loader state, i.e. the machine's memory image around the program at a given moment, which no emulator run reproduces.
- CLOSED (abc0123, aa080f8; 8.2 (4)): the plane-count-0 node: the first dot product is formed before the count is looked at, the
  plane is the six words after the count; extractor version 26 carries the plane, rig part `lmnode` 400 cases, 0 differences.
- CLOSED, burst UNOBSERVABLE (8cbf117, 82faa69; 7.7, 11.4): the chaff: the plain pixel is seen at the draw-time camera (pen 13,
  one row above the routine's y; the "not seen" negative came from a stale post-pass camera and a missing row shift). The burst's
  lines plot nothing in any shipped view: the slot is outside the view in the chase, look-back and head-on views from the third
  pass, and in the fixed tower camera the original's own routines write no pixel for any of the 14 burst passes. Needed: none for
  play.
- CLOSED, in flight UNOBSERVABLE (8cbf117; 12.1): the pass-93 write is the approach camera's window-row change at 0x14388 (camera
  mode 6, height below 0x24000, then the helper 0x1EFF4); it acts in the approach only. The panel band is filled with colour index 3
  in memory in both pages and never shown (black in every frame); the display-side cause (probably a copper palette split) was not
  traced and has no effect on the port.
- CLOSED (8cbf117, 9d4447d; Part A OQ 2, 3, 5; 12 items 4): the class 0x15 skip of the placer (`combat7-placer15.txt`,
  `tests/test_entities.c`), the player's missile live retarget (`combat7-retarget.txt`), the lost-target loop (captured: the pass
  counter stops; no shipped record reaches it, the port ends the search, a safe choice since nothing can be compared after a hang),
  the pod of the rescue mission (class 0x32 stepped by the seat routine, 769 units a pass), and the game-time length of a pass
  (defined by note 08 8.1's conversion).
- UNREACHABLE (R; note 07 12 item 8, 9.7): the dead key body 0x1ECA4, the basis of the W key (the port feature): nothing in the
  image enters it and a poke cannot set the program counter. Its behaviour is a port choice. Needed: none.
- CLOSED (8cbf117; 11.5, 8.10): model 11's slot case and the trailing data triple: the slot builder runs through the original's
  own handlers in the rig (6,000 cases and 3,000 parachute cases, 289 of them at phase 0 with a zero nibble), 0 differences.
- CLOSED (99ec4cb, 8d1d94d with 8cbf117; note 08 Part C C4): the kept wreck counted again and F6's fourth count: a missile reaches
  the submarine and counts nothing, the ship-class dying counter and kill counter never count down (`combat7-subkill.txt`: V:4A963
  unchanged); the submarine does not explain a fourth count and the kept-wreck recount of milestone 6 (8.9) stays the candidate; the
  F6 golden pins the port's counts.
- UNREACHABLE (R; 07 4.5, 4.6, 8.3; NOTE-GAPs at `ai.c:645`, `732`, `weapons.c:511`): a target index of 16 or more and a target
  record beyond the 16: no writer produces it (placement `0xFF`, `0x80`, `0x80 + r` with r at most 14; the range rule; the
  region exit; the live retarget). The test that pinned the port's treatment was removed (82faa69).

### Note 08: game flow

- CLOSED (bebab94, ce56eb5, fa95c2c; note 08 12.14 C13, 5.6, 6.1; note 06 start table): the approach camera path N5. The port
  sees the approach from outside: external view, HUD step 50, window row 179 from the first pass and 167 from the first pass whose
  camera height long is below 0x24000 (camera mode 6), the cockpit (0, 144) from the pass after camera mode 0 (the carrier: the
  same pass); `src/core/mover.c` is the mover of 07 6.2 (stages 0 to 6, blend target, offset targets, damped step) and reproduces
  the camera longs of the airfield-1 log (125 samples) and the carrier log (111) with 0 misses. Training part 2's approach is 29 + 2
  passes, part 1 keeps step 50 (rows 3 and 4 of the table). Test `tests/test_mover.c` and the approach tests of
  `test_missions.c`, `test_training.c`. The mover's flight before the location screen is not a flight: the standing position is
  planted at the dispatch (08 6.1, V `flow7-loc-mover`); the NOTE-GAP at `game.c:1998` is gone. What stays open is below (airfields
  2 and 3; the missions' approaches).
- CLOSED (bebab94; 5.6, 12.14 C13): the approach count of the location keys 7, 8, 9 and 0: 227, 223, 200 and 192 passes (V:4A900 =
  6 to 9, +-1; `flow7-keys7890`); the NOTE-GAPs at `screen.c:654`, `672` and `tests/test_screen.c:727` are gone.
- CLOSED (2ec360d; 9.2): the dispatch-to-briefing timing and the first start through a countercode: key to prompt screen 132 blanks
  (20 steps), RETURN to the briefing text 21 blanks (3 steps), 23 steps in all, the scene build 40 blanks; the qualification ESC
  restart (black 6, fade-in 7, a map stage of 6, verdict 3, flight 53 steps after the ESC); new state `SCR_QUAL_VERDICT`; the
  NOTE-GAP at `screen.c:787` is gone. The qualification golden changed (synthetic, committed).
- CLOSED (3291f0f; 5.8): the key latch: no cap while the location caption types (one key a pass of a blank), one key a pass of 11.5
  blanks after it, the aircraft path with three slow passes of 11 to 12 blanks, then 2 to 3 passes a blank (`flow7-latch-loc`,
  `flow7-latch-air`); the NOTE-GAP at `screen.c:1056` is gone.
- CLOSED (b6773c9; 3.8): the mission-text typing rate where the model did not reproduce F3 to F6: the click cost is 5,350 (max
  error 1.3 % over eight lists, was 2.9 %); briefings 490, 430, 464, 484, 530, 472 and orders 404, 312, 516, 504, 340, 381 pinned
  within 2 %; the mission scripts re-timed (the F6 outlier was a measurement). The F1 and F2 orders (404 and 312) were sampled about
  10 blanks late and are pinned at their sampled values.
- CLOSED, 13 and 14 UNOBSERVABLE (8d1d94d; 7.7): the demo ESC levels 13 and 14 stay stated as unobservable at step resolution
  (6.25 blanks): at that resolution only T = 37 falls in the window past the boundary and T = 36, 39, 42 rest on the formula.
  Needed: a finer-than-a-step reset delay measurement, i.e. the port itself running at the blank (not planned).
- CLOSED (8d1d94d, 99ec4cb; Part C): C2 (the F1 contact with the player flying: V:4A927 counts 0x1E down to 1, then 0xFF in 47
  passes), C4 (above), C5 (request 3 wins and request 2 repeats after the resume; the port's rule was already equal), C6 and N11 (a real crash in F1: code 4 kept, allowance 3 to 2, V:4A927 10 to 0, resume 176 blanks after; the
  crash restart clears V:4A927), C7 (failure by the thief's own flight, shadowing timer 3, 2, 1, 0, -1, then both destinations
  re-installed; the far pass reloads 200), C12 (F1's variants 1 and 2 of the visibility rule: selector 2 and 3).
- UNOBSERVABLE (08 Part C C3, C8): F2 success by Air Force One's own landing (it never lands at the first route block in four staged
  attempts; the landing ends a multi-waypoint route about 8,500 blanks long, hours of exclusive emulator time) and F5 success by a
  real shoot-down (three staged shots at the cruise missile: it ended in the carrier's hull test, passed beneath, or the lifted
  player crashed; a pursuit geometry needs closed-loop flying). The rules of 12.6 and 12.12 stay R with the AI trajectory rig
  (`test-ai-traj`) and the staged success branch. Needed: a real run of the mission in the emulator with a pilot (a scripted
  closed-loop controller).
- CLOSED (f0e4dbe; 5.13): the pause pass in the outcome screens: V:4A866 = 1 from the freeze state to the resume, 0 at the resume;
  the P test asserts the byte stays 1 (`flow7-pause-outcome`). The remaining detail (the original's V:4A968 = 0xFE at the resume
  taken one pass later) is CLOSED by final fix A (`d69dde7`, note 08 5.13): the resume and the first flight pass are two steps.
- CLOSED (ce56eb5; 12.14 C13, 8.1): the training dispatch rows that milestone 6 had read only (modes 2 and 0x7D, the NOTE-GAPs
  at `tests/test_missions.c:971`, `972`): sampled in `flow7-training-7D` and `-2`: part 2 is 31 passes of 0x12D58 with step 50 and
  row 167 through the briefing and approach, part 1 keeps step 50.

### Cockpit and extractor

- CLOSED (68aaeb5, be4cebf; note 06 Marks): the map-shapes validator checks the structure and one FNV-1a-32 of the shape bytes
  against one constant in the validator (both languages); the fixture carries an invented 324-byte set with its own hash; the real
  rows are prose and the hash in note 06. No extractor version bump (the real output is unchanged).
- UNOBSERVABLE (note 06; `cockpit.c:159`, `assets.h:194`): which bit the masked compare with the two blank codes clears, and the
  burst pen assignment of tables 3 and 4 to the layers b and c (both hold 9 in the shipped image, so no shipped state shows a
  difference). Needed: a data image whose tables differ.
- CLOSED (4b25eac; the browser's segment table): the Python extractor ships `extract/segments.txt` (a copy of
  `tools/extract-js/segments.txt`, a test keeps them equal) and no longer reads `extracted/captures/logs/image-static.txt`
  by default; byte-identical on the real and the synthetic disk. WAV odd-length padding stays as Python 3.15's wave module does it
  (all real samples are even).
- CLOSED (37174e2, c86507f, d418a60, 924d5dc, 0ced03c, 68aaeb5; the clean-room carries of milestone 5's S10): the scan tool and
  its allow list; the airfield start row of `world_synth.h` is invented; the limit-table and pad samples of the extractor tests are
  structure checks; the briefing-chain ids, `tiles[21][2]`, `lim[8]`, the step table, the month names and the config fields were
  found already closed or in prose; no capture script pokes the descriptor region. `scan.py` exits 0 on the tree. What the scan does
  not catch is open (the policy on rule tables printed as numbers, below).
- CLOSED (86874f0; note 07 6.3; extractor 27): the three airfield camera points at `0x47B30` are 16 bytes apart in the image (the code
  at `0x1E40C` shifts the index left by 4), but both extractors packed 12-byte records, so entries 1 and 2 were misaligned and the fixed
  camera (SHIFT+KP-ENTER) stood at cell (0, 0) for start locations 1 and 2. Both extractors read the 16-byte stride; the synthetic disk
  lays its points out at that stride; the fixtures are regenerated; `tests/test_entities.c` has a real-asset test of the three cameras.
  The note's hash of the sub-table is `c7edde9b` (the earlier one covered the misaligned bytes). No golden changed.

### Tests, CI and the goldens

- CLOSED (a6dbe8a, bc1f85a): synthetic F5 reaches 0xFF; `make golden` fails a synthetic mission golden that never reaches it. The
  hit is a launch-position hit: see the open item.
- CLOSED (a5d34db): the device tests of the audio main run without assets through `interceptor --synthetic` (native build only; the
  browser build refuses the flag). The mixer callback is not observable from outside: see the open item.
- CLOSED (630a1f1): the training RETURN and S beep test runs in CI on the invented world (the gap was the flight tables, not the
  training table); the S beep of part 2 (mode 0x7D) is covered by no test: open item.
- OUT OF SCOPE: the Task 6 capture comparison is not asserted in CI (the logs are the emulator's captures of the original and stay
  local and git-ignored): `make -C tools/m68k-rig test-sound-captures` skips without them and, with the logs and
  `build/sound7_port`, runs `sound_capture_check.py all` and `tools/vamiga-ref/sound7_check.py`; documented in `CLAUDE.md`.

### The browser version

- CLOSED (2ede99a, 4dc42fe, ddf826c; manual 'In the browser'): the flight log persists (IDBFS at `/log`, `FS.syncfs(true)` before
  `callMain`, `web_log_saved` after every save, a one-at-a-time syncer in `page.js`); the denied-storage case shows one line and
  keeps the log in memory; the smoke run reloads a context and finds the log, and finds none in a fresh context.
- CLOSED (2ede99a, 4dc42fe): the browser gamepad in Chromium through a fake pad (`navigator.getGamepads`), and the audio context
  running with ScriptProcessor callbacks counted. The web build with the IDBFS commits (first in `cf97aae` and `5b9391f`) ran green
  in CI at `5b9391f` (the earlier green, 630a1f1, predates those commits).
- OUT OF SCOPE (physical): F5, F6, F11 and F12 (and F1 in some browsers) in a real browser, the sound heard in Chromium, Firefox
  and Safari, a physical controller. `docs/browser-checklist.md` lists each check for a person.

### The Three.js frontend

- OUT OF SCOPE: the Three.js frontend stays a later project against the scene API (the hand-over below; the user's ruling of
  2026-10-15). The IDBFS persistence and the engine matrix, which the milestone-6 document listed for that milestone, are done.

## New open items

Each with its owner note section and what would settle it. Order: code-visible rules first.

1. Airfields 2 and 3: the start matrix one table step off (note 08 12.14). The original's mover target and offset targets use an
   attitude matrix one table step (0.1 degree) off the player's start matrix (airfield 2: the sine and cosine words (12532, 10553),
   heading 24801 to 24808, not (12551, 10531) for 24800; airfield 3: about (5630, 15386), heading 27185 to 27192, not 27200); 87 of 87
   and 86 of 86 camera longs of `followup-airfield-start`, `-2` match with those words, and the rig's start routine gives the
   port's matrix, so something changes the heading between the start and the approach. Airfield 1, the carrier and the keys 7 to 0
   are exact. The port keeps the player's own matrix: the camera differs from the log by a few thousand units after the first
   passes and airfield 2's mover would arrive 2 passes after the table's 93 (the screen's count forces the cockpit). Evidence: a
   per-pass log of the heading between the airfield start and the approach's first pass.
2. (b) CLOSED by final fix A (`d69dde7`, note 08 5.13): the outcome resume. The original shows V:4A968 = 0xFE at the resume
   sample (blank 179) and its first flight pass takes it at blank 185; the port now runs the resume and that flight pass as two
   steps, the goldens regenerated once with this reason.
3. The SHIFT-ESC menu list order (note 08 5.4, 5.11; 10 7.2). The port types the menu's title first; the capture plays the instant
   lines first, then the typed ones (`SHIFT_MENU_DELAY` sweep maximum: click starts equal to the blank in 17 of 30). The order of
   the real list is a text-engine question. Evidence: the text engine's per-pass request log of the menu set-up chain from a
   SHIFT-ESC.
4. The tenth pause click (`screen.c:1255` NOTE-GAP, note 08 5.14, note 10 7.3; after final fix A no test pins its absence or its
   presence: the test asserts the nine starts only): the capture has a tenth click start at +34 blanks, one pass-pair after the line's last character; the 3.6 and 3.4 rule 5 model
   explains nine. Evidence: the text engine's request log for the pause line's last pass.
5. (e) the missions' approaches (`79a0eac`, note 08 12.14 C14), CLOSED as to the view and the stages, OPEN as to one NOTE-GAP in two parts. They were sampled (`sound7-approach`, a mode-3
   start, 210 passes: the external view for passes 0 to 125, the mover's stages 8, 0 to 6, the cockpit from 126); the port now
   follows that log. What stays open is C14's NOTE-GAP at `game.c:2056`: (a) the camera longs (the path from the overview position and the
   entry trail; the port's stage 6 comes one pass before the log's) and (b) the per-mission counts (the earlier single runs of F2 to F6
   are not reproduced). The HUD step 50 and the rows 179, 167 are R, not gaps. Evidence: one `flow7-approach-air1` style capture per
   mission with the camera longs and the player's start position every pass.
6. `scan_abs8`'s two-cell distance (`weapons.c:430`, note 07 12.5). The scan's distance for a per-axis difference of two cells or
   more reads the original's table at a word-wrapped offset; the port takes the unsigned word. It arises only with a stale camera
   in a cockpit view with a chaff slot, or a tracer that far from its owner; the rig test does not draw those cases. Evidence: an
   extractor table for the wrapped read, then a rig case.
7. The synthetic F5's launch-position hit (`synthtrig`, Task 6): nothing flies in the invented world (a world with motion drops the
   player off the invented carrier, which has no deck test), so the success path is covered by the missile starting at the cruise
   missile's height. Evidence: invented physics tables plus a deck test in the invented carrier.
8. The S beep of training part 2 (mode 0x7D) is covered by no test. Evidence: a synthetic training part 2 start (the invented
   world has part 1 only).
9. The device tests' mixer callback is not observable: `test_audio_main.py` checks that the device opened (no `audio:` stderr
   line), that the run ends 0 and that the register log has latches, periods and volumes for 40 steps; the callback itself has no
   counter on the outside. Related: one frontend pytest (audio underrun line on stderr under load) failed rarely and passed on rerun
   (seen by Tasks 4 and 7). CLOSED for the flake by `2b48a1d`: the synthetic device test ignores an underrun line and the log-wiring
   runs open no audio device; the blindness to the callback stays.
10. The Firefox and WebKit skips: Firefox 157 cannot launch in this sandbox (SIGSEGV after the user-namespace clone fails); WebKit
    (WPE) loads and extracts but `requestAnimationFrame` stops without a display, so its frame-driven checks skip with that
    reason. Only Chromium runs every smoke check here. Evidence: the same smoke run on a machine with user namespaces and a display
    (a CI runner with Playwright's browsers).
11. The two allow entries for pointer-table addresses (`tools/cleanroom/allow.txt`: the hull pointer table, 3 consecutive 32-bit
    addresses, and the burst shape pointer table, 9). They are identification (the extractor validators require them; the notes
    state the addresses), and the ruling of Task 4 keeps them. The final review ruled them acceptable; `allow.txt`'s header and the scan's
    README now name this one exception to "no entry for a value a note asserts", and final fix B pinned the two lines to the
    fixture's exact offsets and run lengths.
12. The policy on tables printed as numbers. RULED (coordinator, final review must-fix 5, by the plan's Global Constraint "the
    extractor carries texts and tables, the repository never does"): the notes print no disk table. Final fix B reduced the HUD step
    table (notes 06 and 04; `head_step.bin`, FNV-1a-32 `1df64ecf`), the AI rate tables (note 07; the first five sub-tables of
    `ai.bin`, `627c7507`), the start-block words (note 05; `ai.bin` start sub-table, `f2fbff97`) and the range curve (note 01b;
    `range.bin`, `ce48dc02`) to prose plus the hash, and the rig probe reads `head_step.bin`. A numeric-literal check (runs of 8 or
    more comma-separated integers against `assets/tables/*.bin` as bytes, words and longs) found no other disk table in the
    tracked tree except: the same two AI rate rows in note 08 (agent A's file, reduced in final fix A), small index permutations
    (notes 04a and extractor tests: orderings of 0 to 15, rules rather than data) and counting runs. Short rule tables of fewer
    than 8 entries and the head-yaw formula (`world.c`) remain as rule constants. The scan cannot see numbers printed as text
    (limits documented in `tools/cleanroom/README.md`); the public README states this honestly.
13. The demo ESC fade levels 13 and 14 (note 08 7.7) stay unobservable (above); the fade table's levels 10 to 12 of the colour curve
    are unmeasured and OBSERVABLE (`fade.h:23`, notes 04 and 06 OQ 8; item 15).
14. The `tools/codewheel.py` / `codewheel_table.csv` finding (Task 8): both are untracked working files; the public export
    excludes them and `openshell/` by `tools/release/exclude.txt`, and re-creates the Musashi submodule at its pinned commit (exit 3
    with a README step if the clone fails). The user confirms the exclusions (`docs/superpowers` is kept by default).

15. The observable NOTE-GAPs, each OPEN with the capture or rig case that settles it (the table below has the state of every gap):
    fade levels 10 to 12 (a capture with the sun, entry 9 of the fade table, in a fade frame at steps 0 to 3: the colour registers
    give the three channel values); `sky.h:50`'s 11 frames and the heads 5 and 7 (a per-frame capture of the tiny-panel pen after
    entering the rear view, and after heads 5 and 7); `lists.c:483` (a rig case of the original's entity test with bits 6 and 7 of
    byte +4, or a capture of an aircraft over the deck); `lists.c:1200` (a rig case of the state routine at the thresholds 0x60, 0xC0
    and 0x1000); `lists.c:1051` (a rig case at x' or y' = -32,768); `world.c:1997` (a rig case with a constructed type B record);
    `world.c:2742` (a static scan of the models 36, 39 to 42, 99 and 100 for an op-26 object of type B); `raster.c:287` (a capture
    of an external view's rows below L); `weapons.c:430` (open item 6); `screen.c:1255` (open item 4); `game.c:2056` (open item 5).

## Acceptance gaps of milestone 7

Stated plainly, as the final review required:

- Review Focus 5, "the invented missile flies", is NOT met: the synthetic F5 is a launch-position hit (open item 7); nothing flies in
  the invented world.
- Task 7's "three engines" is met in Chromium only (open item 10): Firefox cannot launch here and WebKit's `requestAnimationFrame`
  stops.
- The S beep of training part 2 (mode 0x7D) has no test (open item 8).
- The device test is blind to the mixer callback (open item 9): it sees that the device opened, the exit status and the register
  log, not the callback.

## NOTE-GAP inventory

`git grep -n "NOTE-GAP" src tests` at the head of `80763ce` (after the final fix round A, which removed the old approach tag and the
two tests' tags and added one for C14 in `game.c:2056`): 28 hits in `src/` (22 open gaps and 6 tag legends: `flight.c:2`, `logbook.h:5`,
`missions.h:3`, `radar.h:42`, `screen.c:2`, `screen.h:12`) and 2 in `tests/` (`test_screen.c:1067`, a comment, and `rig/test_weapons_diff.c:695`). Milestone 6 had 38 hits in `src/` (32 gaps). Each gap
with its owner section and its state (CLOSED, UNOBSERVABLE, or OPEN with the evidence that settles it; the observable ones are
open item 15).

| Place | What | Owner | State |
|---|---|---|---|
| `src/core/ai.c:645`, `732` | target index of 16 or more | 07 Part A item 9, 4.5, 4.6 | UNOBSERVABLE (unreachable: no shipped record) |
| `src/core/assets.h:194` | burst pen tables 3 and 4 to the layers b and c | 06, 07 11.4 | UNOBSERVABLE (the shipped image makes no difference) |
| `src/core/cockpit.c:159` | which bit the blank codes clear | 06 | UNOBSERVABLE (no visible effect) |
| `src/core/lists.c:483` | shortcut bits 6 and 7 of byte +4 | 04a 5b | OPEN (decoded from the listing only; a rig case of the original's entity test, or a capture of an aircraft over the deck, settles it) |
| `src/core/lists.c:623` | words before a table are code of either sign | 04a P5 | UNOBSERVABLE (unreachable in the data) |
| `src/core/lists.c:1051` | negation of -32,768 not run in the rig at that edge | 04 'Stage [90]' step 8 (C) | OPEN (one rig case at x' or y' = -32,768; not reached in play) |
| `src/core/lists.c:1200` | rate-class thresholds | 04a 2 | OPEN (read only; a rig case of the state routine at the thresholds) |
| `src/core/radar.h:126` | what lives at 0x53818 and 0x5381A | 07 12.3 | CLOSED (R; not observable in play) |
| `src/core/game.c:2056` | the mission approaches: (a) the camera longs (path from the overview position, the entry trail, the stage-6 pass one early), (b) the per-mission counts | 08 12.14 C14 | OPEN (one capture per mission with the camera longs and the player's start position every pass; the step 50 and the rows are R, not gaps) |
| `src/core/screen.c:1255` | the tenth pause click at +34 | 08 5.14, 10 7.3 | OPEN (already observed in the capture, cause not settled: open item 4; the text engine's request log for the line's last pass settles it) |
| `src/core/sky.c:35` | edge cases d = -32,768 and a remainder word of -32,768 | 04 | UNOBSERVABLE in play (0 in 200,000 random rotations); a rig call with a forced divisor would show the original's value |
| `src/core/sky.c:528` | h = -32,768 | 04 | UNOBSERVABLE (the camera stays below 8,388,608 position units, so the height word cannot reach it) |
| `src/core/sky.h:50` | where the constant 11 comes from; whether heads 5 and 7 behave as the rear view | 04 | OPEN (observable: a per-frame capture of the tiny-panel pen after the rear view and after heads 5 and 7; item 15) |
| `src/core/weapons.c:430` | `scan_abs8`: a two-cell distance | 07 12.5 | OPEN (observable: an extractor table for the wrapped read at 0x208E8 and a rig case; open item 6) |
| `src/core/weapons.c:511` | target record beyond the 16 | 07 Part B | UNOBSERVABLE (unreachable) |
| `src/core/weapons.h:114` | debug counter of out-of-range landmark ordinals | 07 8.2 | UNOBSERVABLE ((b), (c)) |
| `src/core/world.c:1997` | negative handler return in the type B drawer | 04a 8b rule 9 | OPEN (R only; a rig case with a constructed type B record settles it) |
| `src/core/world.c:2742` | type B op-26 object in the alternate-blit window | 04a (C) | OPEN (a static scan of the models 36, 39 to 42, 99 and 100 for such an object settles it, no emulator needed) |
| `src/frontend/sdl/fade.h:23` | fade colour levels 10 to 12 | 04, 06 OQ 8 | OPEN (observable: a capture with the sun in a fade frame, entry 9 at steps 0 to 3; open item 13) |
| `src/frontend/sdl/raster.c:287` | rows below L in external views | 04 Q4 | OPEN (a capture of an external view's rows below L) |
| `src/frontend/sdl/raster.c:288` | alternate blit without edge line | 04 Q4 | UNOBSERVABLE (every triangle of the pass has edge lines) |
| `tests/test_screen.c:1067` | the tenth pause click | 08 5.14 | CLOSED by final fix A (`ae84d18`): the test asserts the nine starts only and pins neither presence nor absence of a tenth (a comment names the gap) |
| `tests/test_world.c:899` | life word 0x80 and above draws nothing | 07 11.3 | CLOSED by final fix A (`9d7167e`): the note settled it; the tag is gone |
| `tests/rig/test_weapons_diff.c:695` | the out-of-range ordinal case | 07 8.2 | UNOBSERVABLE |

Gone since milestone 6 (each with the commit that removed it): `game.c:1370` (head hold, 4eec32d), `1817` (restated, 4eec32d),
`1998` (the mover's flight, fa95c2c), `2107` and `scene.h:392` (gun order, aa080f8), `2193` (d8e87a2); `sound.c:218` (4eec32d);
`screen.c:654`, `672` (bebab94), `787` (2ec360d), `1056` (3291f0f); `weapons.c:264`, `291` (aa080f8, abc0123);
`tests/test_missions.c:971`, `972`, `992` (ce56eb5), `tests/test_screen.c:727` (bebab94), `tests/test_ai.c:458` (82faa69).
New since milestone 6: `screen.c:1255` and `tests/test_screen.c:1067` (the tenth pause click) `weapons.c:430` (`scan_abs8`) and `game.c:2056` (C14, the mission approaches).

## Declared approximations and port choices still in the code

Found by their tags in `src/` (grep for "PORT CHOICE", "PORT FEATURE", "(C" and "port choice"); named by symbol, because line
numbers drift.

- Sound, `src/core/sound.c` and `src/frontend/sdl/audio.c`: the 50 Hz blank clock; the click's position within its pass; the
  pass-to-blank cost model; the filter default and corners; the x2 output gain; the live-play rules of 9.2 (lead, skip, trim, hold
  on underrun); the master level reaching the mixer as a target, an immediate set and a ramp flag; the 1,024 commands a step with a
  sticky overflow flag; `QUEUE_MAX` (1 << 20, reported when exceeded); the `--wav` mixer's 125 ms a step; the guards `sound_glide`
  and `wrap_noise` (unreachable); a missing sample leaves its object silent with one stderr line; the music-less machine's memory
  test taken as true.
- `src/core/screen.c`: the screen timing conversions (`STEPS_QUAL_TO_NOTICE`, `STEPS_TRAIN1_POST`, `STEPS_TRAIN2_POST`,
  `STEPS_MS_RESTART`, `STEPS_WELCOME_TEXT`, counted from the dispatch or borrowed from a neighbour screen; the qualification's
  first-start and restart stages `STEPS_QUAL_BLACK`, `_FADE_IN`, `_MAP` and `_VERDICT` are measured now, 08 9.2); `SHIFT_MENU_DELAY` (the SHIFT-ESC menu list's click placement, open item 3);
  `PAUSE_PASS_UNITS`, `PAUSE_FIRST_PASS_START` and `PAUSE_SLOW_EXTRA` (the pause line's pass model, measured, with the tenth click
  missing); `MS_CLICK_COST` 5,350 in `missions.h` (a fit within 1.3 %, 08 3.8); a bare screen module returns to the menu at a mode
  end.
- `src/core/mover.c` and `game.c` `approach_pass`: the approach's aim follows the existing aim code (`vs.fixed` at the mover's
  position, mode 6 target offset 5, look-at from stage 0's threshold); the airfield 2 and 3 start matrix is the player's own
  (open item 1); the missions' approaches follow the sound7-approach log (open item 5, `79a0eac`; its C14 NOTE-GAP stays open).
- The landmark alias rule (C, `weapons.c` 'An ordinal outside 0 to 10': the aliases j-16384 and j-32768 act as the real entry, every
  other out-of-range ordinal skips and counts) and the target-index treatment at `ai.c`/`weapons.c` (NOTE-GAPs above).
- `game.c`: without `ai.bin` a run is the one block of the entry (a missing-asset fallback); the analogue axes are read as keys
  beyond a quarter of their travel (PORT CHOICE, input device) and the hold key queue is capped at 64; the controller dead zone 8000
  (`input.c`/`input.h`, C); the absolute throttle axis and the summed per-step clamp (`flight.h`, CADENCE); the `--flight` debug
  start gives the approach scene's display pass before the first flight step (C); the lead's +0x0A is set before the briefing's
  picture (08 8.1, C); the states and the one count per step for the passes of the take-off notice (08 3.8 item 4, C).
- The W key (`GAME_BTN_REPLENISH`, `game.c`, `src/frontend/sdl/main.c`): the one PORT FEATURE.
- `src/core/cockpit.c`: `gate1_screen` approximates the gate-1 passes that count V:4A8EE down.
- `src/core/lists.c`: the tail-only draw's result word and the cockpit-block variant (07 11.4, declared); the rate-class thresholds
  (C).
- `src/core/radar.h`: V:4A9F8 is stored as 0 (unobservable, C).
- `src/core/world.h`: the no-data fallback camera and the free camera's start (`FREECAM_START_ALT`, PORT CHOICE).
- `src/frontend/sdl/text.h`: glyph columns beyond the plane are not drawn (08 3.4 rule 6; the shipped data never does it).
- The throttle-lever axis has no original (`flight.h`).
- `tests/frontend/synthetic_world.c` and `--synthetic`: an invented world for tests and the device run, native build only.

## Hand-over to the next project: the Three.js frontend

The design (`docs/superpowers/specs/2026-10-13-interceptor-port-design.md`, section 11) puts the Three.js frontend after the SDL
frontend, as a separate project against the finished scene API (the user's ruling, kept by milestone 7). The core is a pure
library; a frontend sends events and reads the scene after every step:

- Time: `game_step` advances one game step, the controller's ruling of 8 steps per second (125 ms each); the frontend presents at
  its own rate. Each step carries the blank accounting of its sound (6, 6, 6, 7 blanks a step).
- The scene carries what the original's passes produced: the screens as text records and pictures (menu, briefing, outcome,
  flight log), the world as display lists with the camera, matrices and zoom (every polygon of the original's display-list
  builder, tiers A to C, with the colours as pens), the entities, the effects (explosions, tracers, flare stars, chaff, the smoke
  trail), the HUD data and the radar list as numbers, the cockpit state (lamps, panel counters, the EJECT lamp key), the fade
  level and the jolt. The approach is now an external view with the mover's camera path (milestone 7), so the scene carries the
  camera and the HUD step and window row it needs for the approach. The SDL frontend rasterises the lists with the original's
  filler into an indexed picture; a Three.js frontend may draw the same lists as geometry, but the core's tests and goldens fix the
  lists, not the pixels.
- The audio is a register-write stream: per step up to 1,024 commands (the bound is restated from the real worst case in
  `scene.h`) in order (`GAME_AUD_LATCH`, `DMA_ON`, `DMA_OFF`, `PERIOD`, `VOLUME`; each with voice, object, a blank stamp within the
  step, a value, and for a latch the byte offset and length of the block) plus three master inputs (`audio_master_target` in
  volume units, `audio_master_set` for an immediate level, `audio_master_ramp` for the V:4A88F flag). The commands are the
  original's writes to the Paula registers; an `AudioWorklet` can be a copy of `audio.c`'s voices (zero-order hold at 3,546,895 Hz
  over the period, volume times 2, voices 0 and 3 left, 1 and 2 right, the three-stage filter chain), fed by a queue stamped with
  the blank so that the 50 Hz clock stays exact. The sample bytes are the extracted WAV files (the object table
  `tables/sounds.bin` names the file, offset and length of each object; the tone and the noise buffers come from `sound_buffer`).
  `--audio-log` of the SDL frontend and `run_golden --audio-dump` print the stream for comparison.
- Assets come from the player's disk: the JavaScript extractor already runs in the browser and writes the files into an in-memory
  file system; a Three.js frontend reads the same `assets/` tree (or the extractor's `files` map directly) and needs the core
  compiled to WebAssembly (the `make web` build exports `_web_audio_unlock`, `web_game_end`, `web_log_saved` and `callMain`; a
  library build with a C API for `game_create`, `game_event`, `game_step` and the scene would be the next step). The page's
  flight-log persistence (IDBFS at `/log` with the one-at-a-time syncer in `page.js`) is the pattern for any frontend that keeps the
  log.
- What the frontend must not change: the step clock, the key mapping of `docs/manual.md` (keys become core events), and the audio
  stream as the core emits it. A Three.js renderer is free in how it draws the lists (smoothing, lighting) only if the replica's
  rule (a perfect replica of the original's pictures) is kept as the default mode.
- Carried from this document into that project: the 49.92 Hz statement (the blank clock), the output level (the x2 gain, a port
  choice) if the player expects the recorder's, the physical-browser checks of `docs/browser-checklist.md`, and the Firefox and
  WebKit runs of the smoke script on a machine that can launch them.
