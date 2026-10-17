# Milestone 6 follow-ups

Open items left by the sound and browser milestone, each with the note section that owns it and the evidence that would
settle it; then what the milestone closed, the NOTE-GAP inventory of `src/` as grepped at this head, the declared
approximations and port choices, and the hand-over to the next milestone. Closed items of the earlier milestones are in
`2026-10-14-milestone-4-followups.md` and `2026-10-15-milestone-5-followups.md`; every item the latter still carried is
CLOSED there or points here. Format: CLOSED (commit, note section) / OPEN (owner note section, the evidence that would
settle it). "R" means read from the code and not run, "V" verified in the emulator or the rig, "C" a port convention.
The statements below hold at the head `55779ca`, after the final review and its fix round (five branches: core, notes,
extractor fixtures, web and docs, input and `main.c`, then the rule-check branch).

## Milestone 6 in numbers

Nine tasks: note 10 (sound), the two reverse-engineering sweeps (Task 2: notes 03 to 06 and 08, in three forks; Task 3:
note 07), the extractor's sound table (Task 4), the core sound module (Task 5), the Paula mixer and the frontend audio
(Task 6, parts A and B), the code rounds for the sweeps (Task 7, parts A, B and C: the synthetic mission tables), the
JavaScript extractor and the browser page (Task 8, parts A and B), and the documentation round (Task 9). Rounds the
ledger records: note 10 had a first draft, a review (7 must-fix, 12 should-fix), a fix round and two correction rounds
from the rig and the capture comparison, then the 9.2 alignment; Task 5 had a review (5 must-fix, 8 should-fix) and a fix
round; the Task 5-fix and 6B review (2 must-fix, 4 should-fix) led to the mixer fixes; the Task 7 review (4 must-fix, 15
should-fix) led to fix rounds 1, 2 and 3 and to two note rounds; the final review (4 must-fix, then should-fix lists) led to
the final fix round (A to E) and to this document.

Tests: golden 20 of 20 (ten synthetic, ten real; every run has the audio columns: the count and the FNV-1a hash of the
register-write stream per step, the master target, set and ramp flag); 674 pytest tests over
`tools/extract tools/hunk tests/frontend` (counted at the merged head 3691870; the final fix round touched
`test_input.py` and the extractor fixtures after it); 84 node tests in `tools/extract-js` (counted with `node --test` at
this head; the synthetic disk gives 90 files and the real disk 226, byte for byte equal to the Python extractor); the whole
rig green with 0 differences (`make -C tools/m68k-rig test`, at 3691870). The unit tests pass under tcc and gcc (the full
counts need the local assets; a tree without them skips the flight suites). CI was green at 3691870 on linux, windows,
macos, extract-js and web.

The six real mission goldens, each ending in the success code 0xFF (seed, tick of the end code): F1 57 / 1477, F2 14 /
3280, F3 127 / 8475, F4 69 / 2059, F5 1 / 883, F6 629 / 8802. The synthetic goldens run the six missions on an invented
world (Task 7C, 3552fcf): F1 to F4 and F6 reach 0xFF, F5 ends in 0xFE (see Tests and CI).

The selector (note 07 2.4, 3708988 and the sweep-B author's 4097c2a): frame x 20,000 plus a delta per path plus the
jitter. Deltas: free flight 1,620; flight school part 1 1,888, part 2 335; qualification 583; mode 5 1,597; mode 7 1,619;
the demo (mode 0x7E) 1,172. The jitter is a 13-entry list measured on the demonstration starts (0 eight times, +1 twice,
-2, -4, -6). Modes 5 and 7 are measured values now (an earlier text said 1,600).

Rig cases for sound (`make -C tools/m68k-rig test-sound`, original against the model): objects 157 checks, tone 312 sets,
noise buffer and 400 wrapper runs, music 7 variants and 120 block ends per voice, beeps 216, per-pass routine 2,000 passes
(12,000 blanks; 30,000 passes in a longer run), ramp 2,000. The port's own module against the original
(`test-sound-core`): tone 212 sets, blank 1,000 blanks, music 4,600 blanks, events 800 wrapper calls over 29,334 blanks,
pass 2,000 passes (12,000 blanks, 71,251 register writes; 30,000 passes in the long run), the start part (20 passes, 190
blanks); 102,113 comparisons, 0 differences. The other rig additions: the sea mesh (1,020 states, 14,606 polygons), the
filler (777 boxes and 708 ball polygons), the map camera (900 states), the viewed cell (400), the key 0 heading, the
landmark branch (12,600 cases; the port's own landmark queues against the original in `test_weapons_diff`, 3,680 cases,
0 differences), the decoy hits (2 x 1,500).

Captures (`make -C tools/m68k-rig test-sound-captures`): 17 sound scripts in `tools/vamiga-ref/scripts/` (generator
`gen-sound.py`) and the follow-up scripts of the sweeps; the driver gained `audtrace`, `wav` and the soft `runto`. The
comparisons: the click (5 of 5, 4 blanks each), the volume ramp (149 blanks, 0 differences), the music chain (12 of 12
within 0.7 frame), the tone (53 tones), the second voice (16 effects), the engine (810 of 811 settled samples), the stereo
pairing.

The extractor went from version 23 to 25: 24 added the sound object table (`tables/sounds.bin`, 37 records of 20 bytes) and
the per-file WAV rates; 25 uses the reference rates of the first play in a flight (note 10 2.4) and drops the manifest's
`rate_note`. The Python extractor's PNG writer writes stored deflate blocks (the PNG hashes changed with version 24).

## Closed by milestone 6 and its fix rounds

- CLOSED (2117e81; milestone 5's S14): the replay stream leak in `test_replay`: `replay_set_stream` frees a stream set
  before, the tests free what they allocate; the leak profile shows only libc's stdout buffer live at exit.
- CLOSED (457a772; milestone 5's S6): the dead code `entities_paused_pass`, `entities_free`, `entities_ai_launch_decision`
  is gone (and the arbiter's dead rules, ebaf2f4; note 07 12.9).
- CLOSED (97ea742, after 16360d2; milestone 5's M11): the F3 golden, and all six real mission goldens, reach 0xFF under the
  new selector rule (seeds and ticks above; only the low six bits of the selector matter for the placement). The scripts'
  header lines were re-wrapped under 400 characters (the script parser reads 511 bytes a line).
- CLOSED (3552fcf, 4459c54, 3691870; milestone 5's S4): the synthetic mission goldens run the missions on an invented flight
  and mission world (`synthflight`, `synthfrom`, `sat` directives); the F-key starts the missions in the synthetic world.
- CLOSED (6f7764d, merged 75b5ae7, 1f4215a; plan Task 7 and spec 8): the game controller mapping: `input.c` holds the one
  table of SDL_GameController controls to the keyboard's keys (dead zone 8000, port choice C), the `--ctrl` and `--ctrlup`
  injection flags, `test_input.py`, the manual's Game controller section. In the same commit: `web_game_end()` for ESC on the
  credits in the browser, the 2-channel device request (SDL converts, so a device of more channels is no longer silent), a
  short sample file plays as long as the file (note 10 2.5), `--seed` and `--ctrl` in the usage text.
- CLOSED (479c781, merged f2e1cb9; note 10 8.2): the pod release plays its noise (object 6, voice 2): the F key with the
  shift flag, the three tests of the rule, the ejection nibble, once per qualifying press, silent when the tests fail. Test
  in `test_game_sound.c`.
- CLOSED (479c781; note 10 7.4): the T and J click gates by the view byte V:4A83D only (the map always has it non-zero, so
  every key beep is silent in the map); tests for the map-view beeps; the ejection stop order with the gear sound and a cue
  playing.
- CLOSED (b3798ee, 7f52a07; final review must-fix 2): no quoted mnemonics in note 07 8.2, `weapons.c` or `lists.c`; the
  `D5LOG` stub of the weapons rig test reads its displaced words from the loaded image; the audio command bound restated from
  the real worst case (`scene.h`); the stale briefing-camera NOTE-GAPs removed (the closed C1); the unmodelled long-pass kinds
  of 08 5.8 named in `screen.c`; the selector's V/C tags in note 07 2.4.
- CLOSED (c2a55e5, merged 3acb8ac; the extractor part of milestone 5's clean-room S10): the head-step bytes, the burst pens,
  the shape cycle and the template record in the extractor tests are invented, the JavaScript fixtures regenerated; the node
  tests are 84. What stays is the map-shapes validator (open below).
- CLOSED (e5baeb1, merged 33fa7a6): the page re-picks after a game end (`Module.onGameEnd`), `callMain` errors and core
  refusals are shown, stale paths, README, manual, `CLAUDE.md`, easter-eggs and the F6 golden header corrected; the manual's
  browser claim names the keys the smoke run presses (F1 to F4 and F6 to F10).
- CLOSED (5a3c829, merged 56382d3; note 10 9.2): the live-play rules aligned; the mixer fix 6d97ac1: voices stop at the
  received stream's end in live play, the re-anchor keeps the 3-blank lead, a stall test. The device queue no longer drops at
  4,096 commands: `QUEUE_MAX` is 1 << 20 with realloc, and a queue that long is reported, never silently cut.
- CLOSED (88b1fc6 to 04eac52, fix round 1 of Task 7; M1 to M4, S1 to S15): the landmark plane test keeps the offsets after the
  back-up, the walk stops past ordinal 10 and after the sixth entity, the step-end viewed-cell write order, the briefing camera
  at the overview, the selector deltas for modes 5 and 7, the demo variant seed, the one key per pass label, keys 5 and 6 at
  airfield 1, KP-ENTER camera mode 0, the EJECT lamp timing comment, the real-table landmark test, the HUD step and row test.
  S8 (gun order), S10 (plane-count-0 node) and S14 of that review (approach HUD) stayed NOTE-GAPs: open below.
- CLOSED (7fefd67 merged 0d09cbf, b4fc240; note 07 8.2, note 08 12.14): fix round 2 of Task 7 and the notes 07/08 round: the
  leftover landmark ordinal is step y's low word plus 1 after the undo (erratum, rig 3,000 of 3,000); the register is shared
  with the entity sub-step; keys 5 and 6 store V:4A900 = 4 and 5 (the "reads 3" was the pre-key value, the NOTE-GAP is void);
  the key-latch rules of 08 5.8 (no cap on the aircraft screen; the location screen at 10 blanks a pass).
- CLOSED (0c6a081, 31f6e16; note 07 8.2, note 08 7.7): fix round 3: the landmark reading is fixed: ordinals outside 0 to 10
  are step y's low word, the aliases j-16384 and j-32768 act as the real entry j (the register kept as a signed 16-bit value),
  every other out-of-range ordinal skips and counts; an ESC after the 34-blank boundary takes level floor(T/3) and resets
  2L+9 blanks after the consuming pass; the key-latch cap redone; no golden shifted.
- CLOSED (3708988, 4097c2a; note 07 2.4): the selector rule, stated above with its deltas and the jitter list (the earlier
  "uniform" reading and the 1,600 for modes 5 and 7 are gone).
- CLOSED (4097c2a, 0c6a081; note 08 7.7): the demo ESC levels measured at 4, 7, 10 and 11 (reset delays 21, 24, 29 and 32
  blanks); the level is floor(T/3) (T = 3 + 9m up to the boundary), and past the 34-blank boundary the reset is 2L+9 blanks.
  The "interpolated" statement is gone. What cannot be pinned is open below.
- CLOSED (c084990, f38577d, 8cadd3c; note 10 5.7, 9.2): the command queue is 1,024 register-write commands a step
  (`GAME_MAX_AUDIO_CMDS`) with a sticky overflow flag (the largest real step is 56 writes); the Task 5 review's must-fix items
  (seconds tick last, V:4ABFA from the walker, the beeps of 7.4, the start-up without music, the cockpit-start engine latch)
  are closed by the same commits.
- CLOSED (4e09622, merged 55779ca; note 07 6.4a, 08 11.1, 10 7.4a): the rule check of the two items the final fix A left
  open. The eject key with the gear sound running: the original wipes the nibble too (V, airborne snapshot); the port had
  deviated on a second press (ORed 0x0A again), and now the first key chain is skipped while the ejection nibble is
  non-zero (a second SHIFT-E at the nibble 6 is ignored). The click gate's cached view byte: T and J read the live view
  level (the poll drains all events in one pass); `weapons_tone_click` no longer uses the per-pass cache. Tests pin both
  (`test_eject.c`, `test_game_sound.c`).
- CLOSED: the `seen_neg_x` gcc warning at `tests/test_screen.c` is gone (the variable is used, line 1257).
- CLOSED: the CI web job ran and was green at 3691870.

## Open items by owner

### Note 10: the sound

- Not captured (note 10 Open question 7; each needs one capture): the explosion bang with its distance quirk (a gun kill of
  a MiG in view); two gun bursts more than 28 blanks apart (the pulses at 280); the music restart after a flight (rig only:
  the menu from a post-flight snapshot); the clicks of the pause (the typed pause text); an overwritten pending bang; the
  gun shot outside (volume 30); the ground-contact touchdown volumes 0x1E and 0x28; the gear sound's outside volume with
  V:4A86D clear; the bump 0x2A834; the cue bits 0x40, 0x01, 0x10 and 0x800. All are rig-verified arithmetic; the port plays
  them from the same tables. (The pod release is closed above as a rule read from the code, R; a capture would make it V.)
- The P beep attribution (note 10 7.4, 8.3) is inferred: the handler itself plays nothing, so the beep is attributed to the
  typed pause text. The capture that settles it is the pause-click capture (the typed pause text) named in the final fix B
  report (`final-fix-b-report.md`, git-ignored); until then the port plays it as inferred.
- The music-less machine (Open question 3): the memory test is taken as true (free chip memory above 200,000); the paths for a
  missing file or object are in the model and the rig, not offered as an option.
- The phase of a pass inside a blank (Open question 4) cannot be seen in the frame samples; the port quantises to blanks
  (note 10 9.1). The click's position within a pass and the pass-to-blank cost model are port choices (C).
- Whether the sound routine runs in the approach cameras where V:4ABFA matters (5.7 items 8 and 12): the port runs it in the
  last passes of the approach and the countdown and not in the typed notice passes (gate 0). Evidence: an audtrace capture
  through an approach with the sound routine's entry logged.
- V:4AA6E: the wrappers' camera test V:4AA6E = V:4A996 is taken as "the viewed record is the player" in the pass and the
  stepped record for the ground contacts; not tied for the seat and the fixed camera (NOTE-GAP at `game.c:2193`). Evidence:
  a capture with the seat or the fixed camera and a wrapper call.
- The head hold and the radar-page countdown of the seconds tick keep their calibrated place (NOTE-GAP at `game.c:1370`,
  `1817`); V:4ABAA is the system clock's seconds. Evidence: the original's second against the mission clock over a long
  capture.
- Wrappers called later in a step that already made clicks (NOTE-GAP at `sound.c:218`, note 10 8.3). Evidence: a capture of
  a click and a wrapper call in one step.
- 49.92 Hz: the captures run 313-line frames. The port keeps the 50 Hz blank clock (C). Only a decision to use the 313-line
  blank as the port's clock settles it; no capture can.
- The output level: the reference recorder's WAV is about 140 times quieter than the port's for the same register volumes (a
  constant ratio, the recorder's gain). The port keeps full scale (16,384 per voice) and the x2 voice gain of the mixer (a port
  choice, 10 9.2 (h)).
- The filter: ON by default (the game never writes the CIA or ADKCON); the corners are vAmiga's. The default is C.
- Live-play port rules (note 10 9.2): the lead of 3 blanks, the skip beyond 5 queued, the trim, the hold on underrun, and the
  `--wav` block of 125 ms per game step: port rules (C) without an original (the original has no such buffer), so no capture
  can settle them.
- `textegn2` fallback is not implemented in the sound table (never loaded on the shipped disk). Unused code and data (Open
  question 6): object 7, the shut-down 0x553D8, the free-all 0x558F6 and the waveform builder 0x559C0 are never reached; the
  port has none of them.
- The seat tone, the engine pitch mid-range files (engine 650, wind 391) and the per-file rates are the loader's defaults or
  the first play's period; the engine file's middle-of-range period is arbitrary in the original (note 10 2.4).

### Note 07: combat

- Gun order in external views (07 12.5; NOTE-GAP at `game.c:2107`, `scene.h:392`, the port's choice (C) beside it): the scan
  0x2AD18 looks at the highest active slot; the logged data predicts the gun first yet shows no conversion; slots 10 to 19
  were never seen. Evidence: a capture that logs the whole pool (slots 10 to 19) at a gun kill in an external view. The port
  keeps the captured order.
- The landmark junk-model and beyond-hunk cases (07 8.2 cases (b) and (c); NOTE-GAP at `weapons.c:264`): 318 leftover
  ordinals (0 to 2 modulo 256) test a real record's box with a model pointer read from unrelated memory, and 3,438 addresses
  lie beyond the loaded hunks. Evidence: the rig with the whole state hunk and its neighbours at the original's addresses.
- The plane-count-0 node (S10; NOTE-GAP at `weapons.c:291`): one shipped node has a plane count of 0 and the extractor has no
  block for it. Evidence: extractor support for the block, then a real-table landmark rig case.
- The chaff burst's pixels (7.7, 11.4): no pen-13 pixel at the replayed positions in three views. Evidence: the camera logged
  before the draw, or a view that keeps the slot near.
- The pass-93 write: the first non-zero V:4A8EE in a free flight is at pass 93 of the capture and is not identified (not n
  mod 32 = 16); the external-view panel band is filled by pen 12 from row 180 and shows black, why is not traced (12.1).
  Evidence: a trace of the writes to V:4A8EE around pass 93.
- A2, A3, A5 (milestone 5): the class 0x15 skip and the W0 bit 1 case; the lost-target loop; the pod of the rescue mission.
  Section 12 items 4 (the game-time length of a pass) and 8 (the dead key body 0x1ECA4, the basis of W). None is reachable in
  shipped data (R); a capture would need a poked state.
- Model 11's slot case and the trailing-slot data triple (milestone 5): synthetic tests only; the rig's 6,000 cases of the
  parachute list are the real witness. A rig case that reaches the slot case would settle it.
- Kept wreck counted again, F6's fourth count (milestone 5's C4): a run that shoots the submarine would tie it.
- Target index of 16 or more and a target record beyond 16 (`ai.c:645`, `732`, `weapons.c:483`): not reachable in shipped data;
  `tests/test_ai.c:458` pins the port's treatment.

### Note 08: game flow

- The approach camera path (N5; 08 12.14 N5 and C13; the note 06 start table; NOTE-GAP at `tests/test_missions.c:992`): the
  port shows HUD step 0 and window row 144 in the approach, the note's table says 50 and 179 (the selection screens and the
  approach). The mover's camera path is not modelled; the port counts the approach's passes (`approach_passes`). Evidence:
  model the mover's camera path, then capture the step and row of each approach pass. The training dispatch rows (167, modes
  2 and 0x7D) were not sampled either (R only, `tests/test_missions.c:971`).
- The mover's flight before the location screen (NOTE-GAP at `game.c:1998`, review M4, 04 / 08 6.1 item 7) is planted to the
  standing position. Evidence: a capture of the mover over those passes.
- The demo ESC levels 13 and 14 are unpinned: at step resolution (6.25 blanks) only T = 37 falls in the window past the
  boundary; T = 36, 39 and 42 rest on the formula and levels 1 and 13 are unreachable as boundary rows. Nothing at step
  resolution can settle them; they stay stated as unobservable in steps (08 7.7).
- The approach count for the location keys 7, 8, 9 and 0 (the out-of-table starts): taken as 118 (NOTE-GAP at
  `screen.c:654`, `672`; not pinned, `tests/test_screen.c:727`). Evidence: a capture of those four keys.
- The timing from the dispatch to the briefing is the free flight's (NOTE-GAP `screen.c:787`) and the first-start delays before
  the briefing (the countercode-answer conversion, C). Evidence: a capture of the first start through a countercode.
- Key-latch long passes (08 5.8; NOTE-GAP at `screen.c:1056`): two kinds are not modelled, the location screen's first passes
  after a change of state (15 to 21 blanks, about 3 steps a key instead of 1.6) and the three or four gate-1 passes. Evidence:
  a key-per-step census on those screens against the port.
- C2 to C8 of milestone 5 (the F1 contact, F2 by the AI's landing, the F6 victory count, the seat's request order, a crash
  restart in a mission, F3 failure and shadowing, F5 by a real shoot-down), N11 (whether the crash restart clears V:4A927),
  the mission text typing rate where the note's rule does not reproduce the captured briefings of F3 to F6 (evidence: the
  per-record typing durations of the six pairs), and F1's variants 1 and 2 of the visibility rule (C12).
- The pause pass: one pass per step; the outcome screens freeze through the screen's hold, so the pause byte is not 1 there as
  in the original (the P test asserts that the byte and the mode do not change).

### Cockpit and extractor

- The map-shapes validator (note 06 Marks; `check_mapshapes` in `cockpit.py` of `tools/extract`): it compares sector 0 and the
  ship shape against the real rows (8 triples, also listed in note 06). Replacing the rows with a hash needs the synthetic
  fixture path changed. Evidence: a hash check plus a fixture that carries an invented shape.
- Which bit the masked compare with the two blank codes clears (NOTE-GAP `cockpit.c:159`); the burst pen assignment of tables 3
  and 4 to the layers b and c is not observable (`assets.h:194`: both hold 9 in the shipped image).
- The segment table for the browser (`tools/extract-js/segments.txt`) holds the loader's RAM bases only; the Python extractor
  still reads `extracted/captures/logs/image-static.txt`. WAV odd-length padding follows Python 3.15's wave module (all real
  samples are even).
- The clean-room carries of milestone 5's S10 that no round addressed: the briefing-chain id table in `missions.c`,
  `tiles[21][2]` and `lim[8]` in `tests/world_synth.h`, the step table in `world.c`, the prompt words in notes 06 and 09, a
  month name and the config file fields in note 08, the old mnemonics in notes 01, 02, 04 and 08, and the relocated-descriptor
  pokes of four capture scripts. Evidence: each replaced by an invented value or a run-time extraction, then a window scan of
  the repository against the assets (the scan of the fixture round found only counting sequences, the sound-table scaffolding
  and the map-shapes rows).

### Tests, CI and the goldens

- Synthetic F5 ends in 0xFE: a zero-physics world has no flying missile, so the success path of F5 is not covered by the
  synthetic golden (a limit of CI coverage, RF5). Evidence: invented physics tables with a flying missile.
- The device tests of the audio main (`test_audio_main.py`) keep their module-level skip in CI: they need assets and the
  `--flight` device run has no synthetic mode. Evidence: a synthetic start for the device run.
- The training S and RETURN beep tests need the real assets (the synthetic world has no training); CI covers the return value of
  `training_key_return`.
- The Task 6 capture comparison is not asserted in CI (the logs are git-ignored): `test-sound-captures` skips without them.

### The browser version

- IDBFS persistence of the flight log: the log lives in the in-memory file system for the visit only (documented). Evidence:
  the feature, then a reload test in the smoke run.
- Real-browser checks: F5, F6, F11 and F12 (and F1 in some browsers) belong to the browser and cannot be tested headless (the
  manual names = and - as the alternatives); Firefox and Safari were not run (Safari's AudioContext resume included); no audio
  check in a real browser (the headless run has only the dummy device).
- The browser gamepad is untested in a real browser (the controller mapping of 6f7764d is tested through `--ctrl` injection and
  `test_input.py` only). Evidence: a run with a physical or virtual controller in Chromium and Firefox.
- A rebuild of the page after the final fix round: `make web` last ran in CI at 3691870; the core commits after it did not
  rerun it.

## NOTE-GAP inventory of `src/` (grepped at 55779ca)

38 hits in `src/`: 32 open gaps and 6 tag legends (`flight.c:2`, `logbook.h:5`, `missions.h:3`, `radar.h:42`, `screen.c:2`,
`screen.h:12`). Each gap with its owner section; the open item is above.

| Place | What | Owner |
|---|---|---|
| `src/core/ai.c:645`, `732` | target index of 16 or more | 07 Part A (unreachable) |
| `src/core/assets.h:194` | burst pen tables 3 and 4 to the layers b and c | 07 11.4 |
| `src/core/cockpit.c:159` | which bit the blank codes clear | 06 |
| `src/core/game.c:1370` | head hold and radar-page countdown of the seconds tick | 10 5 |
| `src/core/game.c:1817` | the seconds clock has no phase relative to the mission | 10 5 |
| `src/core/game.c:1998` | the mover's flight before the location screen | 04 / 08 6.1 item 7 |
| `src/core/game.c:2107`, `src/core/scene.h:392` | gun order in external views | 07 12.5 |
| `src/core/game.c:2193` | the second call at 0x114C0, V:4AA6E for the seat and fixed camera | 10 5.7 items 8, 12 |
| `src/core/lists.c:476`, `616`, `1044`, `1193` | shortcut bits of byte +4; words before a table; negation at -32,768; rate-class thresholds | 04a 5b, P5, 2 |
| `src/core/radar.h:126` | what lives at 0x53818 and 0x5381A | 07 12.3 |
| `src/core/screen.c:654`, `672` | approach count for keys 7 to 0 | 08 12.14 C13 |
| `src/core/screen.c:787` | dispatch-to-briefing timing | 08 5.6 |
| `src/core/screen.c:1056` | two unmodelled long-pass kinds | 08 5.8 |
| `src/core/sky.c:35`, `528`; `src/core/sky.h:50` | edge cases d and h = -32,768; the constant 11 | 04 |
| `src/core/sound.c:218` | wrappers called later in a step that made clicks | 10 8.3 |
| `src/core/weapons.c:264` | landmark junk-model and beyond-hunk | 07 8.2 (b), (c) |
| `src/core/weapons.c:291` | plane-count-0 node (S10) | 07 8.2 |
| `src/core/weapons.c:483` | target record beyond 16 | 07 Part B |
| `src/core/weapons.h:114` | debug counter of out-of-range ordinals | 07 8.2 |
| `src/core/world.c:1997` | negative handler return in the type B drawer (R only) | 04a 8b rule 9 |
| `src/core/world.c:2727` | type B op-26 object in the alternate-blit window (C) | 04a |
| `src/frontend/sdl/fade.h:23` | fade levels 10 to 12 | 04 |
| `src/frontend/sdl/raster.c:287`, `288` | rows below L in external views; alternate blit without edge line | 04 Q4 |

Test-side NOTE-GAPs: `tests/test_missions.c:971`, `972` (training dispatch rows) and `992` (the approach camera path, N5),
`tests/test_screen.c:727` (keys 7 to 0), `tests/test_ai.c:458`, `tests/test_world.c:899` (life word 0x80 and above, 07 11.3),
`tests/rig/test_weapons_diff.c:695` (the out-of-range ordinal case). `src/core/screen.h` no longer carries the stale "no
briefing camera" gap.

## Declared approximations and port choices still in the code

Found by their tags in `src/`; the line numbers of the earlier lists drifted with every round, so the places are named by
symbol or comment (grep for "PORT CHOICE", "(C" and "port choice").

- Sound, `src/core/sound.c` and `src/frontend/sdl/audio.c`: the 50 Hz blank clock; the position of a click within its pass; the
  pass-to-blank cost model; the filter default and corners; the x2 output gain; the live-play rules of 9.2 (lead, skip, trim,
  hold on underrun); the master level reaching the mixer as a target, an immediate set and a ramp flag; the commands kept in a
  list of 1,024 per step with a sticky overflow flag; the device queue (`QUEUE_MAX` 1 << 20, reported when exceeded); the `--wav`
  mixer renders 125 ms per game step at 44,100 Hz; the guards `sound_glide` and `wrap_noise` (unreachable); a missing sample
  leaves its object silent with one stderr line.
- `src/core/screen.c`: the screen timing conversions (`STEPS_QUAL_TO_NOTICE`, `STEPS_TRAIN1_POST`, `STEPS_TRAIN2_POST`,
  `STEPS_MS_RESTART`, `STEPS_WELCOME_TEXT`; C: counted from the dispatch and not from the countercode answer, or borrowed from
  a neighbour screen); the mission text click cost and typing budget (08 3.8 item 3a); a bare screen module returns to the menu
  at a mode end.
- The landmark alias rule (C, `weapons.c` "An ordinal outside 0 to 10") and the gun order (C, beside the NOTE-GAP of
  `game.c` 12.5).
- `game.c`: without `ai.bin` a run is the one block of the entry (a missing-asset fallback); the analogue axes are read as
  keys beyond a quarter of their travel (PORT CHOICE, input device) and the hold key queue is capped at 64; the controller dead
  zone 8000 (`input.c`, C).
- The W key (replenish, `GAME_BTN_REPLENISH`, `game.c` and `src/frontend/sdl/main.c`): the one PORT FEATURE.
- `game.c`: the `--flight` debug start gives the approach scene's display pass before the first flight step (C); the lead's
  +0x0A is set before the briefing's picture (08 8.1, C); the states and the one count per step for the passes of the take-off
  notice (08 3.8 item 4, C).
- `src/core/cockpit.c`: `gate1_screen` approximates the gate-1 passes that count V:4A8EE down.
- `src/core/lists.c`: the tail-only draw's result word and the cockpit-block variant (07 11.4, declared); the rate-class
  thresholds (C).
- `src/core/radar.h`: V:4A9F8 is stored as 0 (unobservable, C).
- `src/core/world.h`: the no-data fallback camera and the start of the free camera (C).
- `src/core/missions.h`: the click cost of the mission screens, 0.85 of a blank (C).
- `src/frontend/sdl/text.h`: glyph columns beyond the plane are not drawn (08 3.4 rule 6; the shipped data never does it).
- The throttle-lever axis has no original (`flight.h`).

## Hand-over to the next milestone: the Three.js frontend

The design (`docs/superpowers/specs/2026-10-13-interceptor-port-design.md`, section 11) puts the Three.js frontend after the
SDL frontend, as a separate project against the finished scene API. The core is a pure library; a frontend sends events and
reads the scene after every step:

- Time: `game_step` advances one game step, the controller's ruling of 8 steps per second (125 ms each); the frontend
  presents at its own rate. Each step carries the blank accounting of its sound (6, 6, 6, 7 blanks a step).
- The scene carries what the original's passes produced: the screens as text records and pictures (menu, briefing,
  outcome, flight log), the world as display lists with the camera, matrices and zoom (every polygon of the original's
  display-list builder, tiers A to C, with the colours as pens), the entities, the effects (explosions, tracers, flare
  stars, chaff, the smoke trail), the HUD data and the radar list as numbers, the cockpit state (lamps, panel counters, the
  EJECT lamp key), the fade level and the jolt. The SDL frontend rasterises the lists with the original's filler into an
  indexed picture; a Three.js frontend may draw the same lists as geometry, but the core's tests and goldens fix the lists,
  not the pixels.
- The audio is a register-write stream: per step up to 1,024 commands (the bound is restated from the real worst case in `scene.h`) in order (`GAME_AUD_LATCH`, `DMA_ON`, `DMA_OFF`,
  `PERIOD`, `VOLUME`; each with voice, object, a blank stamp within the step, a value, and for a latch the byte offset
  and length of the block) plus three master inputs (`audio_master_target` in volume units, `audio_master_set` for an
  immediate level, `audio_master_ramp` for the V:4A88F flag). The commands are the original's writes to the Paula
  registers; an `AudioWorklet` can be a copy of `audio.c`'s voices (zero-order hold at 3,546,895 Hz over the period, volume
  times 2, voices 0 and 3 left, 1 and 2 right, the three-stage filter chain), fed by a queue stamped with the blank so that
  the 50 Hz clock stays exact. The sample bytes are the extracted WAV files (the object table `tables/sounds.bin` names the
  file, offset and length of each object; the tone and the noise buffers come from `sound_buffer`). `--audio-log` of the
  SDL frontend and `run_golden --audio-dump` print the stream for comparison.
- Assets come from the player's disk: the JavaScript extractor already runs in the browser and writes the files into an
  in-memory file system; a Three.js frontend reads the same `assets/` tree (or the extractor's `files` map directly) and
  needs the core compiled to WebAssembly (the `make web` build exports `_web_audio_unlock` and `callMain`; a library build
  with a C API for `game_create`, `game_event`, `game_step` and the scene would be the next step).
- What the frontend must not change: the step clock, the key mapping of `docs/manual.md` (keys become core events), and the audio stream as the core emits it. A Three.js renderer is free in how it draws the lists (smoothing,
  lighting) only if the replica's rule (a perfect replica of the original's pictures) is kept as the default mode.
- Open for that milestone from this list: the IDBFS persistence of the flight log, the real-browser key, audio and gamepad
  checks, the 49.92 Hz statement, and the output level (the x2 gain, a port choice) if the player expects the recorder's.
