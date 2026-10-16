# Reverse-engineering notes

Each note in this directory documents one subsystem of the original game, so that the
port can be written from the note alone. `00-binary-layout.md` (hunk and address map) and
`01-reference-emulator.md` (what the headless vAmiga can run) are the common ground; every
other note refers to addresses as hunk and offset in the layout note.

## Clean-room rule

Notes are prose and tables. They contain no disassembly listings, no code of any kind
(not even short fragments or pseudo-code) and no decompiler output. Describe what the code
does: the data layout, the algorithm in words and formulas, the constants in tables, and the
rules of behaviour. Addresses, offsets, sizes and numeric constants are facts and belong in
tables. If a reader could paste a passage into a compiler or an assembler, rewrite it.
Nothing extracted from the disk (assets, ROMs, byte dumps beyond what a constant needs) is
committed.

## Slices

One note per slice, one agent per slice. Slices that only read the binary run concurrently.

| File | Slice | Covers |
| --- | --- | --- |
| `00-binary-layout.md`, `01-reference-emulator.md`, `01a-reference-captures.md`, `01b-flight-captures.md` | Common ground | the hunk and address map, what the headless vAmiga can run, the capture logs (organised by capture, exempt from the required sections) |
| `02-assets-and-tables.md` | Assets and tables | model format, terrain and landmark data, fonts, message tables, mission tables, where each lives |
| `03-3d-pipeline.md` | 3D pipeline | object transform, projection, polygon order, clipping, the 320x200 raster layout and palette, view kinds |
| `04-scene-composition.md`, `04a-display-lists.md` | Scene composition | the frame's stages, sky and ground, the viewport, and the display-list builder with its three queues and walkers |
| `05-flight-model.md` | Flight model | thrust, lift, drag, stall, gear, hook, ground and carrier contact, catapult, arrestor, crash rules, fuel, damage effects |
| `06-cockpit-hud.md` | Cockpit and HUD | instruments, HUD symbology, radar, warnings, views, use of the instrument panel bitmap |
| `07-ai-and-weapons.md` | AI and weapons | enemy and friendly behaviour, guns, AIM-9L, AIM-120, chaff, flares, cruise missile, rescue pod, sub, hit and damage rules |
| `08-game-flow.md` | Game flow | every text screen (the text records, the text engine and its font, palette and fades), the screen state machine from boot to flight and back (menus, location and aircraft selection, mission start and end, game over), briefings, mission scripts, scoring, qualification, training, demo, flight log; hidden keys, cheats and jokes for `docs/easter-eggs.md`; controls for `docs/manual.md` |
| `09-security-check.md` | Security check | the countercode prompt (the port leaves it out on purpose) |
| `10-sound.md` | Sound | the sound library and its 37 objects, the synthesised tone and noise, the player and the four voices, the per-pass engine, wind and afterburner routine, the master-volume ramp, the music, clicks and beeps, the event table with objects, voices, periods and volumes |

## Required sections

Every analysis note has these sections, in this order (the capture note `01a-reference-captures.md` is exempt: it is a log of captures, organised by capture). An empty section says so; it is not omitted.

1. **Location**: where the code and data live, as hunk and offset (see `00-binary-layout.md`).
2. **Format or Behaviour**: the decoded data format or the algorithm, in prose.
3. **Constants**: tables of the numeric constants, with units and the address each came from.
4. **Open questions**: what is not understood yet, and what evidence would settle it.
5. **Verified by**: the Musashi rig test (`tools/m68k-rig`) or reference-emulator capture
   (`tools/vamiga-ref`) that confirms each claim. A claim without an entry here is marked
   unverified in the text.

## Open questions by note (state after milestone 7)

The notes' own "Open questions" sections are authoritative; this index says what remains in each after milestone 7, with the
follow-ups document `docs/superpowers/plans/2026-10-16-milestone-7-followups.md` for the states and the evidence. An item is
CLOSED when a capture, the rig or the code settled it, UNOBSERVABLE when the note states why nothing can show it and what would
be needed, and a port choice when no original exists to measure. Notes 00 to 05 and 09 were not changed by milestone 7 in what they leave open (01a gained its capture list; the final fix round replaced
the disk tables printed in 01b, 04, 05, 06 and 07 by prose and a hash); their lists are as the earlier milestones left them (mostly address-level questions the port does not need).

| Note | What remains open |
| --- | --- |
| 00, 01, 02, 03, 04, 04a, 05, 09 | unchanged since milestone 6: unexercised encodings and units in 02 and 03 (a node with bit 12 outside an axis node, the den = 0 hang of the clipped line, the arcsine tables' sizes), the 23 request-writer sites of 04a (classified, not run), the countercode prompt's input handling in 09 (the port leaves the prompt out), AROS in 01. None blocks a rule of the port. |
| 06 | the fade levels 10 to 12 of the colour curve (harness values; 14 never occurs in the fade table), which bit the blank codes clear and the burst pen layers (unobservable: the shipped image makes no difference), the second ball table (no visible effect), the map view's depth scale under zoom (R). Closed in milestone 7: the strain second in a pause, the map-shapes rows (prose and a hash), the approach HUD step and row (start table). |
| 07 | UNOBSERVABLE: the landmark junk-model and beyond-hunk cases (8.2 (b), (c): they depend on Kickstart's vectors, the loader's hunk placement and the RAM beyond the program), the chaff burst's pixels (7.7, 11.4: no shipped view shows them), the pass-93 write and the panel band (12.1: approach only, never displayed), the lost-target loop after the hang (4.4), the dead key body 0x1ECA4 (9.7, 12 item 8). UNREACHABLE: a target index of 16 or more (R). Open and narrow: `scan_abs8`'s two-cell distance (12.5). Closed in milestone 7: the gun order and the 0x4016 post (12.5), the plane-count-0 node (8.2), the class 0x15 skip, the live retarget, the pod, the game-time length of a pass, the flare star and the chaff's plain pixel. |
| 08 | open and narrow: the start matrix of airfields 2 and 3 (12.14), the SHIFT-ESC menu list's order (5.4, 5.11), the pause line's tenth click at +34 (5.14). Closed by the final fix round (final fix A, note 08 5.13 and 12.14): the outcome resume's V:4A968 one pass later; the missions' approaches (sampled in `sound7-approach`, 12.14 C14: external, the mover's) stay open in the camera longs, the HUD step and row of a mission approach and the per-mission counts (NOTE-GAP of C14). UNOBSERVABLE: C3 and C8 (F2 by the AI's landing, F5 by a real shoot-down; 12.6, 12.12 stay R with the AI rig), the demo ESC levels 13 and 14 (7.7). Part A and B items on keys, devices and the log (the mouse buttons, the joystick, the dead key bodies, computed text ids) stay read-only. Closed in milestone 7: the approach counts of keys 7 to 0, the approach camera path (the mover), the mover's flight before the location screen, the qualification's first start and restart, the key-latch kinds, the mission text typing rate, the pause byte in the outcome screens, C2, C4 to C7, C12, N11. |
| 10 | UNOBSERVABLE: the touchdown volume 0x28 and the bump 0x2A834 (8.2, OQ 7), the music-less machine (OQ 3). Port choices: the 50 Hz blank clock against 49.92 Hz, the output level, the filter default, the live-play rules (9.2), `textegn2` and the other unused code (OQ 6), the loader's default periods (2.4). Closed in milestone 7 by captures: the explosion bang and its distance quirk, two gun bursts, the gun shot outside, the music restart, the pause clicks and the P beep, the pod release, the cue bits, the gear sound by camera, the sound routine in the approach cameras, the seconds tick and the phase of a pass inside a blank. |
