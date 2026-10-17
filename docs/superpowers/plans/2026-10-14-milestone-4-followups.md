# Milestone 4 follow-ups

Open items left by the game-flow milestone, each with the slice that owned it, and
their disposition after milestone 5 (Task 9). The code marks every open one with a
NOTE-GAP comment at the place it applies. Closed: the commit and the note section.
Carried: the reason and the open question of the owning note.

## Draw layer (notes 04, 04a, 07 Part B; Task 8)

- CLOSED in the core, the drawing is Task 8's: the touchdown/lift-off/hit jolt. The
  core raises `scene.jolt` (d2687d1, note 07 13); the frontend moves the window.
- CLOSED (Task 8, 69969e4; the drawing is the frontend's): the instructor's smoke trail. The core keeps the
  six-position ring and the trail length (d2687d1, note 07 13); the segments are drawn
  by the frontend. The trail ring's report is Task 8's.
- CLOSED: the demo cockpit capture's enemy alert, radar blip and warning light (the
  hostiles of the 0x7F placement row): the capture at step 257 matches exactly,
  0 pixels (Task 6, d2687d1, merged 59636ec).

## Keys and events (note 08 section 11; note 07 9, 12)

- CLOSED (d2687d1, 59636ec): R (radar range), T (target), J (ECM) and Shift+KP-ENTER
  (the fixed camera) are core events; F, C, SPACE and RETURN drive the weapons and
  countermeasures.
- CLOSED (d2687d1): the fire counter +0x3E and the other weapon counters are raised by
  the weapons code, also in the demo (the AIM-120 launch of frames 599-602 reads +0x3E
  = 1). The stale NOTE-GAP comment above `log_after_step` in game.c is Task 8's file.
- CLOSED (Task 8, 838d121): Y (message-line pages) in the frontend.
- LEFT (note): the right mouse button and the joystick are read only (R) in the note.

## Flight log (note 08 section 10)

- CLOSED (629c381, 08 10.9, 13.2): the bearing patch (0x58) and the day of the date
  patch (0x5B); the weapon and hit counters (d2687d1); `logbook_mission_started`
  (screen.c, every pass of the mission start that reaches the state, 14.2).
- CLOSED (extractor 16, 1da0e86; 08 C9): the month names of the 0x5B date, exported as
  `tables/months.bin` (36 bytes) and loaded into `logbook_t.months`; the date line
  prints the month.
- CLOSED by the note (08 5.4, 2.1): the key-6 mission list gates its lines on the
  availability bytes only (record + 0x11 + M for F2 to F6; the progress counter does
  not gate it). Implemented in screen.c since milestone 4; no further rule exists.
- CLOSED (29260eb, 08 5.8 and 10.5): the typed-key ring holds 10 presses before the
  callsign entry opens, the 11th is dropped (`SCREEN_KEY_RING`); the keyboard queue of
  31 events stays for every other screen. test_logbook: the ring test. CARRIED inside
  it: the note says the ring is cleared when an input record starts [R] while the port
  hands the pending presses to the entry; whether the original loses them was never
  captured (NOTE-GAP at `screen_key`).

## Screens (note 08 sections 5-6)

- CLOSED (731768f, 08 12.3, note 07 2.4): the placement variant is the low word of a
  draw from the seeded core generator (`screen_random_seed`, default seed 1); a restart
  reuses the stored variant and offsets.
- CARRIED: the camera approach is counted in passes (free flight 112, F1 121, F2 112,
  F3 110, F4-F6 99; c6eea10, 08 12.14 N1) but not drawn (the frontend flies its own
  camera; the mover's stage table is R). Open: C13, the mover's call count (the rig
  needs ~1,190 calls for the emulator's ~100 passes); the airfield starts of the free
  flight were not captured and assume the carrier start's 112.
- CARRIED: the map overview camera of the selection screens and the four map numbers
  were fitted to the captures (5 and 6 pixels differ); the briefing camera's flight to
  the first hostile is not modelled (the map overview stands behind the briefing text).
- CARRIED (note 08 5.6, 13.x): location key 0's heading, 256,000 above the 28,800
  circle, is reduced modulo 28,800 in game.c. The note still calls it unsettled: the
  open question is how the attitude code of note 05 17.14 treats a heading word that
  large.
- CARRIED (note 08 7.7): the ESC fade-out from the demo's fade-in estimates one level
  low at blank 6 (6 against the measured 7). The note gives no pass latency beyond
  the measurement; `DEMO_ESC_LATENCY_BLANKS` stays a calibration. The 28-to-32-blank
  spread of the ESC fade-out (5.11) is also still a NOTE-GAP.

## Training (note 08 section 8)

- CLOSED (ba80c4e, 1d72029, 7022466, note 07 5 and 4.12): the dying path of a
  non-player record, the explosion object of class-0 records, the ground contact.
  flight.c's comments now cite them (f89af7c). Rig: dives 900/900, ai-traj 100/100.
- CARRIED: the first-start delays before the briefing are the countercode-answer
  conversion (C).

## Reference captures

- The log-disk prompt variants 1 and 2 were produced by patching the drive check.
- The ng3/probe captures and the training briefing frames live under `extracted/`
  (git-ignored) and are described in notes 01a and 08.

## Milestone-5 carry-overs (settled by the final review and the final fix round)

The items the ledger listed during milestone 5, each with its disposition. What is
still open is carried to `2026-10-15-milestone-5-followups.md`, with its owner note
section and the evidence that would settle it.

- CLOSED (2b3f618, e93232e; note 07 8.9, test_replay): the demo's AMRAAM misses the
  MiG. The cause was upstream of the steering (a constant position offset from pass 225
  on, found by the capture of record 10); the missile now hits at pass 0x27C and the
  kill is counted at 0x28B.
- CLOSED (extractor 16, 1da0e86; 08 C9): the month names.
- CLOSED (3e83695, 7218284, 2d54cd7, extractor 20; note 07 8.2, 8.2a): the landmark
  volumes and big hulls for the tracers (hooks installed; the hull flag byte in the
  level records). The landmark branch of the tracer test itself has no rig run or
  capture: CARRIED (new doc, note 07 Part B sections 7-8 item 1).
- CLOSED (3e83695; note 07 9.5): the ECM divisor class (every class other than 1).
  CARRIED: the range key's other nibbles (radar.c, note 08 11; new doc).
- CLOSED (1591c33; note 08 12.5): F1 cannot be won. The radar walk's counter
  (`radar_t.mission1`, V:4A927) feeds the script; the mission-f1 golden reaches the
  end code 0xFF at tick 1477.
- CLOSED (0dd379f; note 08 12.14 N2, 5.11): scripts stand still during an outcome
  screen, and the ESC detour of 0x11D30 (`SCR_ESC_DETOUR`, +7 steps).
- CLOSED (c6eea10, 0dd379f): the approach by pass counts. CARRIED: C13, the mover's
  call count, and the airfield starts that assume 112 (new doc).
- CARRIED: the ejection seat's "level 2 to level 1" and the effect life wrap picture
  (note 07 6, 11.3; new doc).
- CARRIED: the cruise-missile dives (y jumps to a table height in the second step;
  missile dives are not in the trajectory harness) and the x drift of the ground rows
  (note 07 4.12; new doc).
- CARRIED: the F3-F6 orders and the uncaptured briefings were typed at a fitted rate
  (29,000 units a step, 48622ce) against the model's 37,500 of note 08 3.8. The final
  fix round replaces it with the note's rule; what remains is listed in the new doc.
- CARRIED: the F1-F6 mission goldens: the synthetic ones are one file six times (the
  synthetic assets refuse the F-key); F3 and F4 reach no end code in the real ones
  unless the final fix round's goldens landed (new doc).
