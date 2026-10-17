# Milestone 5 follow-ups

Open items left by the combat milestone, each with the note section that owns it and
the evidence that would settle it. The code marks the open ones with a NOTE-GAP
comment where they apply; the tag legend is in the headers of `screen.c` and
`missions.h`. Closed: the commit and the note section. Carried: the reason and the
open question of the owning note. "R" means read from the code and not run, "V"
verified in the emulator or the rig. The milestone-4 items and their dispositions
are in `2026-10-14-milestone-4-followups.md`.
Milestone 6 (Tasks 2, 3, 7 and its fix rounds, the sound slice and the final fix round) settled most of the carried items
below: each is marked CLOSED with its commit and note section, and every item that stays open reads "CARRIED to the
milestone-6 doc" (`2026-10-16-milestone-6-followups.md`, which holds the owner section and the evidence that would settle
it). No item is open in both documents without that pointer.

## Milestone 5 in numbers

Ten tasks (note 07 Parts A and B, note 08 Part C, the extractor, entities and AI,
weapons and radar, missions, the frontend, the follow-ups, goldens and CI), 347
commits on the branch, and the rounds the ledger records: Task 6 had six fix rounds,
Task 8 three, Task 5 and Task 7 two each, the note authors answered the reviews in
about a dozen rounds, and the final whole-branch review was followed by a final fix
round (clean room, settled rules, stale tags, the typing rate, the goldens of F3 and
F4, these documents). The final review ran at efc86a2 and at the tip: 46 test
binaries and 1,610 unit tests (805 per compiler), 613 pytest tests, 20 goldens, and
the rig suite of 18 binaries with 0 differences (7 min 41 s from a clean tree):
AI 6,000 cases, trajectories 400 of 400 steps in agreement, radar 15,500, weapons
3,680, slots 1,044,000 slots in 6,000 cases, ships 3,000, seat 4,000 plus 4,000,
spawn 600 plus 600 and 3,000 region ticks, missions 8,000 (18,900 states in the
Python check), training 20,000. The emulator side holds 30 combat capture scripts
among 318 scripts. The extractor went from version 13 to 23: templates (14), the AI
tables (14, then 15), months, burst and target names (16), pens and the shape cycle
(18), prompts (19), the hull flag byte (20), the data triple of the level records
(21), the star table (22), the briefing chain and the head step table (23). The leak profile is clean in the core.

## Final fix round (Parts A, C, D and the note author)

These were open at the final review and are in flight in the same round; confirm them
against the merge, and move any that did not land to the matching section below.

- Clean room (M1-M4): the AI fixture of invented values, the star table through the
  extractor, structural assertions in test_combat.py, the fire-prompt words and the
  assembly fragments out of note 07, the burst script without image words.
- Settled rules (M6): the heading word V:4A9E2 applied to the view, a second P in the
  ESC countdown, the look byte V:4A86C at the training starts, the small-shape sprite
  column. M7: the 43 stale NOTE-GAP tags. M5: the rescue pod is F4 (mode 6).
- M8: the typing rate of note 08 3.8 replaces the fitted 29,000 units a step.
- M9: the compare of 0x35CF2 (signed or unsigned) read, a negative R handled as it
  does, one rig case with R = 0xFF.
- M11: the F3 and F4 goldens reach an end code.
- N11: whether the crash restart clears V:4A927 (the player start 0x12CE0). Not addressed by milestone 6: CARRIED to the
  milestone-6 doc (Note 08, with C2 to C8).
- CLOSED (16360d2, 97ea742; M11): the F3 and F4 goldens reach an end code. After milestone 6's selector change F3 lost it;
  all six real mission goldens now end in 0xFF (F1 57 / 1477, F2 14 / 3280, F3 127 / 8475, F4 69 / 2059, F5 1 / 883,
  F6 629 / 8802).

## Combat: the entities, AI and dying path (note 07 Part A)

Dispositions after milestone 6 (Tasks 3 and 7B; the sweep is `8cce2ca`, the notes' Open questions name the sections).

- CLOSED (3708988, note 07 2.4 and Open question A1): the mission-start selector V:4ABB0 is not uniform. The clock
  is read a fixed time after a blank, so the selector is 20,000 times the frame within the second plus a path constant
  (demo about 1.2 ms, missions about 1.6 ms) plus the jitter; 13 demonstration starts and 4 mission starts measured
  (V). The port draws the frame and the jitter from its seeded generator through the same path for the demo and the
  missions. The sweep-B author's later measurement (4097c2a) replaced the single constants: the deltas are free flight
  1,620, school part 1 1,888 and part 2 335, qualification 583, mode 5 1,597, mode 7 1,619, the demo 1,172, with a
  13-entry jitter list (0 x8, +1 x2, -2, -4, -6); modes 5 and 7 take their measured deltas, no longer 1,600. The jitter
  stays a measured list and not a derived rule: CARRIED to the milestone-6 doc (its numbers section names it; the list
  is the port's pick). The placements moved with it, so the goldens changed (CLOSED in M11 above).
- CARRIED to the milestone-6 doc (Note 07), A2: the class 0x15 skip of the placer and the W0 bit 1 case of the target
  handling. No shipped state reaches them (R).
- CARRIED to the milestone-6 doc (Note 07), A3: the lost-target loop (4.4) that never ends in the original; the port ends
  the search. No shipped record reaches it.
- CLOSED (56fb039, cc5568b, 9a05023; note 07 5.3, 6.4, 6.5, 8.9, A4 and A6 to A8): a counted hostile's kill by the
  original's own weapon (a staged Sidewinder and a gun kill from a fresh MiG, both counted 15 passes after the dying
  start; V), a seat shot down (staged, V), a parachute in flight (V), and the crash hand-over with an ejection nibble
  (V). Two corrections came with them: a kept wreck hit again restarts the dying cycle and counts again (the port
  already did; test 56fb039), and the crash pass CLEARS the ejection nibble and the hand-over runs as for any crash
  (code 9a05023; notes 05 17.12 and 17.17 rule 8 corrected).
- CARRIED to the milestone-6 doc (Note 07), A5: what the rescue operation does with the class 0x32 pod (0x1EE4E) is the
  mission script's part; the F4 golden's pod landing is the end-to-end witness.
- CLOSED (comments 9a837c8, note 07 4.12): the dive x drift. On the floor the aircraft takes its x and z velocity at
  once (ground rows), the missile never; airborne, the x and z differ by 4 to 20 units because the speed words differ
  by 4 (the speed step reads the height). The trajectory harness: 400 of 400 steps equal (V).
- CLOSED (note 07 8.6 and 7.6, rig `launch_order_check.py`): the launch-order conflict. Both orders hold for different
  records (R and V); no code sets V:4A873 or V:4A874, so the state is unobservable in play.
- CLOSED (01fd8fc, 5e72490; note 07 6.5): the five passes before the seat: the cockpit picture unchanged, the EJECT
  lamp flashing, the seat spawned in the sixth pass, the first seat picture with the old panel for one picture.
- CLOSED (note 07 6.5, 11.5): the seat's "level 2 to level 1" (in an external view the switch comes at the seat
  counter 0x14 and the level is reset to 0xFF; test 01fd8fc); the revival of an invalid record by the aircraft draw
  handler is declared unobservable (every freeing site requests a rebuild first).

## Combat: weapons, effects, radar (note 07 Part B)

Sections 7-8:

- CLOSED (3128fd3, d71aaba; note 07 8.2): the landmark branch of the tracer test: eleven ordinals (not ten), the cell
  and box tests, the chain, the 8 sub-steps, the pointer artefact in the z offset, the no-motion exit; the model
  against the original 12,600 cases, 0 differences. The port's `hook.landmark` had never been set, so tracers never
  stopped at buildings before this round. The port is now compared to the original directly (CLOSED, 4097c2a and the
  Task 7 fix rounds 88b1fc6 to 0c6a081, note 07 8.2): `test_weapons_diff` runs the landmark queues, 3,680 cases, 0
  differences, and the readings of the chain are fixed (the offsets are kept after the back-up, the shared ordinal
  register, the aliases j-16384 and j-32768). CARRIED to the milestone-6 doc (Note 07): the junk-model and beyond-hunk
  cases (07 8.2 (b), (c)) and the plane-count-0 node (S10).
- CLOSED (note 07 8.2a): the part ids of the other templates: the low nibbles are 0, 1, 2 and 0xF only; the missiles and
  ships are always 0xFF.
- CLOSED (note 07 8.9): a staged missile kill and a gun kill from a fresh MiG (see A4 above).
- CLOSED (note 07 8.1, R): V:4A840 is 1 only between the mission start and the flight start, 0 in flight.
- CLOSED (note 07 8.2, 8.2a): a tracer against an AMRAAM: the missile's part id is 0xFF at every level, no hit.

Sections 9-10:

- CLOSED (note 07 9.3, rig `weapon_check.py decoy`): a flare or chaff slot under an aircraft counts as a gun hit (the
  hit test is blind to the slot kind).
- CLOSED (878d48c; note 07 10.5): roll and yaw of an out-of-control player keep acting; only the speed and the pitch are
  lost.
- CLOSED (note 07 10.5, R): the other readers of V:4A984 bit 8: none (one reader 0x13D0A, one writer 0x2A880).
- CLOSED (note 07 6.5): the five passes (see Part A above).
- The 29-pass protection of a released decoy is a rule, not a question; keep it.

Sections 11 and 13:

- CLOSED (9a837c8; note 07 11.3): the effect life word wrap picture. An object with the life word 0x80 or more draws
  nothing, the scar included: the port's rule is the original's picture, no longer a declared deviation.
- CLOSED (note 07 11.5, unobservable): the revival in the aircraft draw handler.
- CLOSED (note 07 11.4, R): what drew the wreck before the burst: the intact model (no model program reads the damage
  words).
- CLOSED (note 10 3.2, 7.4, 8.3; note 07 13.4): the pitch and volume parameters of the sounds and the cue tones. The
  cues on whose capture note 10 is silent are listed in the milestone-6 follow-ups.
- CLOSED (note 10 Open question 1a): the wrapper of a non-camera launch: none. A launch by a hostile or the AI makes no
  sound (the notice 0xFB is posted only for the camera's record).
- CLOSED (note 07 13.3, R): the jolt writers: touchdown (kind 1), hard touchdown and landing after a stop (3), take-off
  roll and lift-off (4); kind 2 is stored by nothing.
- CLOSED for the flare star (note 07 7.7, `flare_chaff_replay.py`): 50 pixels in pen 9, three lines, equal to the rig's
  line calls. CARRIED to the milestone-6 doc (Note 07) for the chaff burst: three views show no pen-13 pixel at the
  replayed positions (the camera of the logged samples is the post-pass state); evidence needed: the camera logged before
  the draw.
- CLOSED (note 07 11.4, declared unobservable): the tail-only draw's result word (lists.c, test_lists.c).

Section 12:

- CLOSED (note 07 12.3): the scope window's units against the mile labels: 2^n list units per pixel (n the range
  nibble); the labels 40, 10 and 2 miles are the 21-row distance of the window's lower edge rounded down.
- CLOSED (note 07 12.3, R, unobservable): the word 0x53818 past the second erase page.
- CLOSED (6a17871; note 07 12.5): a gun kill of a designated target posts NO 0x4016 in either view (this contradicts the
  earlier reading): the designation drops two frames after the crashed bit, in the next world update. The missile rule
  is kept. CARRIED to the milestone-6 doc (Note 07, gun order): the scan 0x2AD18 rule for external views stays unsettled;
  the port keeps the captured order (entity pass first), marked as a NOTE-GAP with a port choice beside it.
- CARRIED to the milestone-6 doc (Note 07), 4: the game-time length of a pass; note 08 8.1's conversion stands.
- CLOSED (ebaf2f4; note 07 12.9): V:4A980 bit 15 and V:4A98E bit 5 are never set; the arbiter's rules 2 and 3 and the
  entries 6, 8, 32 and 34 are dead code and gone.
- CLOSED (note 07 12.4): the hostile-missile threat bits 4 and 5 (V:4A925 = 0x10 with the alert 0x8010 for a class-0
  missile; 0x20 and 0x8012 for class 1).
- CLOSED (93a8e73; note 07 12.1): V:4A8EE and V:4A8EF at the first pass of a free flight are 0 and 0xFF (the port had
  reached the pass with V:4A8EF = 0: the location key now stores V:4A8EE only). The external-view panel draw 0x35238
  leaves the screen unchanged because the clip helper returns "none" for V:4AA3E of 0x14 or more and a 20-row pen-12 fill
  from row 180 replaces the copy. CARRIED: the first non-zero V:4A8EE at pass 93 of that capture, and why the filled band
  shows black; the per-port-step count-down of V:4A8EE is the same item (CARRIED to the milestone-6 doc, Note 07, the
  pass-93 write).
- CARRIED to the milestone-6 doc (Note 07), 8: the entry the dead key body 0x1ECA4 clears; never run, nothing jumps to it.
  It is the basis of the W key (the one PORT FEATURE).
- CLOSED (68f15f8; note 08 11): the range key: 9 goes to 0xD, 0xB to 9, anything else (0xD too) to 0xB (the port had
  "other to 0xD"). The census of 39 presses shows no other value.

## Game flow: the scripts and the screens (note 08 Part C and earlier)

- CLOSED (095b05d, 319a08e; note 08 13.4 C1): the briefing camera: there is no camera path in a mission briefing. The
  camera stays at the map overview (0x10C00000, 0x05000000, 0x11400000), mode 3 two steps after the target choice, mode 4
  at the first 0x12AC0 pass.
- CARRIED to the milestone-6 doc (Note 08), C2: the F1 contact with the player flying after the MiG.
- CARRIED to the milestone-6 doc (Note 08), C3: F2 success by the AI's own landing (speed at touchdown against the 0x320
  limit); the captures poke the state.
- CARRIED to the milestone-6 doc (Note 07, kept wreck counted again), C4: the F6 victory condition: four counted kills where three counted fighters exist once the box-3 missile
  has been fired, and no store counts the submarine. New evidence from milestone 6: a gun hit on a kept airborne wreck
  counts again at each dying cycle (V:4A963 = 1, 2, 3), which may explain the fourth count; and only one counted
  hostile is placed at the mission start, the others come from the region 2 entry (cap 4 in F6). Not yet tied to a run
  that shoots the submarine.
- CARRIED to the milestone-6 doc (Note 08), C5: the order of the seat's request 2 and a script's request 3 in one pass.
- CARRIED to the milestone-6 doc (Note 08), C6: a real crash restart in a mission keeping code 4.
- CARRIED to the milestone-6 doc (Note 08), C7 and C8: F3 failure and shadowing with the AI running; F5 success by a real shoot-down (the wreck is
  staged).
- CLOSED (extractor 16, 1da0e86): C9, the month names.
- CLOSED (319a08e, 928c6b3; note 08 12.x, 13.4): C10, the radar walk's scan rate (two walks per redraw pass while V:4A8EF
  is 1 or 2: 31 contacts in 49 passes match); C11, KP-ENTER during the orders skips the approach too; C12, the visibility
  rule on sub-lists 2 and 3 (F2 and F4, four runs). CARRIED to the milestone-6 doc (Note 08) inside C12: F1's variants 1
  and 2 were not run.
- CLOSED (928c6b3, fdb21a7, 319a08e; note 08 12.14 N1): C13, the approach mover (one call per pass; the rig's 1,190 calls
  were the zero target): the free-flight approach lasts 118 passes (airfield key 1), 93 (keys 2 and 3) and 104 (key 4,
  the carrier), not 112 for all; missions unchanged. The takeoff-and-circuit script moved by 6 ticks. CARRIED to the milestone-6 doc
  (Note 08): the keys 7, 8, 9 and 0 (the out-of-table starts) were not captured and take 118 (NOTE-GAP in
  `approach_passes`). The approach's HUD step and row (N5) is a new open item there as well.
- CLOSED (b3b3daa; note 05 17.14 and 08 5.6): location key 0's heading: the stored word is (3200 x 80) modulo 65,536 =
  0xE800 = 59,392 (V, `keyheading_check.py`); the modulo 28,800 is gone.
- CLOSED (60f3839; note 08 7.7, 5.11): the demo ESC fade: the ESC is taken at the next pass boundary, T = 3 + 9m blanks
  after 0x11B9C first ran; the level is floor(T/3), the fall starts one blank later, the reset follows 2L + 10 blanks
  (L = 7) or 2L + 9 (L = 10) after that pass; the spread of 28 to 32 blanks is the fade timer's phase, plus or minus 2.
  `DEMO_ESC_LATENCY_BLANKS` is gone. Superseded and CLOSED (4097c2a, 0c6a081; note 08 7.7): the levels 4, 7, 10 and 11 were measured (reset delays 21, 24,
  29 and 32 blanks), and past the 34-blank boundary the level is floor(T/3) and the reset 2L + 9 blanks (T = 36, 39 and
  42 consumed within a blank). No interpolation remains. CARRIED to the milestone-6 doc (Note 08): the levels 13 and 14
  are unpinned (only T = 37 is reachable at step resolution), stated as unobservable in steps.
- CLOSED (60f3839, 489c5c3; note 08 5.8, 5.14, 12.11): the typed-key ring is cleared (with the typed text) when the
  callsign entry opens, it is not handed on; P in the notice or wait state sets the byte only and the pause screen opens
  at the first flight pass, a second P clears the byte; P is ignored in the outcome screens; P with the byte at 0 runs one
  0xFF pass and pauses in the step it arrives.
- CLOSED (b3b3daa, 095b05d; note 05 17.17 rule 7): the game-over steps (20) do not depend on a pause (P only toggles
  V:4A866, no pause screen); the paused pass is as long as an unpaused one (60 against 63 blanks in the captures), so the
  resume states keep their 7 steps.
- CLOSED (429e100; note 08 6.1 item 1, note 06): V:4A859's writer is the state 0x12EDA (the port's `out_hud`); V:4A869 is
  dead (one reader 0x3226A, no writer, 312 state blocks all 0).
- CARRIED to the milestone-6 doc (Note 08): the mission text typing rate. If the note's rule from the final fix round does not reproduce the captured
  briefings of F3-F6, their durations stay fitted; evidence: the per-record typing durations captured as 13.5 did for the
  six pairs.
- CLOSED (e2c8297; note 04 "The start marks of the overview map"): the map overview camera and the four map numbers: the
  rule reproduces the emulator frame (40 of 40 pixels, `mapcam_check.py` 900 states, 0 differences); the port's MARK_XY
  table equals it. No blink on the aircraft screen (V:4A90F is 0 there). CARRIED to the milestone-6 doc (Note 08): the first-start delays before the
  briefing (the countercode-answer conversion, C).

## Declared approximations and port choices still in the code

Updated after milestone 6; the inventory with file and line is in the milestone-6 follow-ups.

- The tail-only draw (note 07 11.4) is a declared deviation with tests (test_lists.c); op 70 with a life word of 0x80 or
  more is no longer one (it is the original's picture, see above).
- CLOSED (note 10 Open question 1c): `gun_sound` (V:4AC13 bit 5) is 1 on the reference machine (the enable longs read
  0x000007F7); it is 0 only when the 2,048-byte chip allocation fails.
- The absolute throttle-lever axis (flight.h) has no original (the mouse and a digital stick only, 05 17.4, 08 11.4); and
  the hold key queue capped at 64 (05 17.17 rule 6 is silent on the capacity). Remove or declare in the plan.
- CLOSED (a3ca5d7; note 04a "The viewed cell in an external view is the camera's"): the viewed cell comes from the camera's
  bits 22 to 28 in external views (400 states, 0 differences).
- CLOSED (963e41f, c908cc0; notes 03 and 04): the sea-mesh vertex layout (all polygons, 1,020 states, 0 differences) and
  the ball's tiny cases of the polygon filler (two entries; 777 plus 708 cases, 0 differences).
- CLOSED (b3b3daa; note 05 17.14): the roll word V:4AB4A outside the cockpit is the player's roll (record 0 +0x6A); the
  player's +0x38 stays 0 (12 store sites, all AI, placement or spawn).
- The W key (replenish) is the plan's one PORT FEATURE; the `--flight` start is a debug path (C), not a feature.
- The pre-existing clean-room items of the review's S10: the briefing-chain id table in `missions.c` (extract it or record
  the decision), `tiles[21][2]` and `lim[8]` in tests/world_synth.h, the step table in world.c, the prompt words in notes 06
  and 09, a month name and the config file fields in note 08, the older mnemonics in notes 01, 02, 04 and 08, and the
  relocated-descriptor pokes of four capture scripts. Not addressed by milestone 6 beyond the new sound code (whose script
  tables hold structure only); CARRIED to the milestone-6 follow-ups.
- CLOSED (457a772; S6): the dead code `entities_paused_pass`, `entities_free` and `entities_ai_launch_decision` is gone.
- CLOSED (2117e81; S14): the replay stream leak in `test_replay` (`replay_set_stream` frees a stream set before, the tests
  free what they allocate; the leak profile is clean).

## Tests and CI

- CLOSED (3552fcf, 4459c54, 3691870; S4): the synthetic world has invented flight and mission tables (`synthflight`,
  `synthfrom`, `sat` directives); the six synthetic mission goldens run their missions: F1 to F4 and F6 reach 0xFF, F5
  ends in 0xFE (a zero-physics world has no flying missile; CARRIED to the milestone-6 doc, Tests and CI).
- CARRIED to the milestone-6 doc (Note 07): model 11's slot case and the trailing-slot data triple (rule 5) have synthetic tests only; the real witness is
  the rig's 6,000 cases of the parachute list.
- CLOSED (note 07 4.12, 400 of 400): missile dives in the trajectory harness are covered; the near-ground, hostile and
  model-1 cases are rig-covered.

## Milestone 6: sound hand-over

Closed by milestone 6 (note 10 `docs/re/10-sound.md`; core `src/core/sound.c`; frontend `src/frontend/sdl/audio.c`):

- CLOSED (75ce0f4, ec27841, 5e50af9, c084990; note 10 sections 5 to 9): the trigger list of note 07 13.4 and the weapons.h
  header comment. The requests no longer wait unconsumed: `scene.sound_vars`, `click`, `click2`, `ecm_lamp`, `readout`
  (display events, not sound), `cp_sound_req` and the entities' `sound_req_*` were removed or turned into the sound
  module's calls; the M-key and view-switch channel stops, the key-click sound and the consumers of V:4A91B, V:4A921,
  V:4A922 and V:4A870 are in `sound.c`.
- CLOSED (d22a8e1, 3f2b577, 0efecab): the extractor's sample rates: per file from note 10 2.4 (extractor 24 added the sound
  object table and the rates; 25 uses the reference rates of the first play in a flight and drops the manifest's
  `rate_note`).
- CLOSED (note 10 Open question 1): `gun_sound` is 1; the pitch and volume parameters are the tables of 3.2, 7.4 and 8.3; a
  non-camera launch makes no sound; the 150-pass echo of V:4AC13 bit 7 is the music fading (target 0), audible, stopped
  about 48 blanks after the mode key by the dispatch.
- What stays open for sound is in the milestone-6 follow-ups (Note 10). The pod release (CLOSED 479c781) and the game
  controller mapping (CLOSED 6f7764d) were added by the final fix round.
