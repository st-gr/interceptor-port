# 01a: reference captures from vAmiga

Frames of the original game, made with `ref --script` (see `tools/vamiga-ref/README.md`) from Kickstart 1.2
and `extracted/interceptor-autoboot.adf`. The PPM files live in `extracted/captures/` (git-ignored, never
committed); this note describes them in prose. Game text and manual text are referred to by short
identifiers and paraphrased, not quoted.

## Reproducibility

Every wait in a script is wall-clock time against a real-time emulator. Emulated time per wait therefore
jitters by a second or two between runs, and so do demo and flight moments. To keep captures repeatable the
flight scripts start from snapshots (`.vasnap`) restored just before the part that matters; the credits
snapshot saves the 150 s boot. Anything that depends on a random draw (the security challenge) is frozen
inside a snapshot. The measured numbers below (HDG values, speeds) are from one run and will differ slightly
on another.

## Capture set

Script line numbers are in the file named in the first column; all are under `tools/vamiga-ref/scripts/`.
Run order: `boot-to-credits.txt`, `freeflight-prompt.txt`, `freeflight-takeoff.txt`,
`freeflight-headings.txt`; `demo-flight.txt` is independent (needs only the credits snapshot).

| file | made by | what it shows |
| --- | --- | --- |
| `credits.ppm` | boot-to-credits.txt:4 | Credits screen: black, title and credit lines in blue, red and grey, a blue press-any-key line at the bottom. |
| `log-disk-prompt.ppm` | demo-flight.txt:11 | Grey text screen asking about a write-enabled log disk in DF0 and for RETURN; shown because the disk is write-protected. |
| `menu.ppm` | demo-flight.txt:14 | Main menu: items 1 to 8 in green (1, 2), blue (3 to 5), red (6, 7) and amber (8), plus two grey lines about ESC and SHIFT ESC. |
| `security-prompt.ppm` | freeflight-prompt.txt:13 | Security-countercode prompt over the bay-area map: amber line, a 4-character challenge (5D5A in this run) and a dash where the answer is typed. |
| `freeflight-select.ppm` | freeflight-takeoff.txt:13 | Starting-location selection: the same bay-area map with the numbers 1 to 4 in amber at the four possible starts (1 an airport on the peninsula, 2 and 3 airfields across the bay, 4 the carrier at sea) and a grey caption below the map. |
| `aircraft-select.ppm` | freeflight-takeoff.txt:17 | Aircraft choice over the map: amber list with 1 for the F/A-18 and 2 for the F-16. |
| `cockpit-takeoff.ppm` | freeflight-takeoff.txt:21 | Cockpit view on the runway, engine idle: HUD frame with heading tape, speed, altitude and G readouts, runway ahead, control tower at left, instrument panel at the bottom. |
| `external-view.ppm` | freeflight-takeoff.txt:26 | After keypad Enter: the F/A-18 seen from behind on the runway; bottom text line gives HDG 028, 0 kts, 5 ft. |
| `external-top.ppm` | freeflight-takeoff.txt:29 | Keypad 5 in external mode: the aircraft from directly above. |
| `external-front.ppm` | freeflight-takeoff.txt:32 | Keypad 8: head-on view. |
| `external-side.ppm` | freeflight-takeoff.txt:35 | Keypad 4: a side view. |
| `head-left.ppm`, `head-back.ppm`, `head-down.ppm`, `head-up.ppm` | freeflight-takeoff.txt:41, 44, 47, 50 | In-cockpit head keys 4, 2, 1, 7: view turned left, looking back at the tail, looking straight down at the runway (no panel), looking straight up at sky (no panel). |
| `heading-1.ppm`, `heading-1-hdg.ppm` | freeflight-headings.txt:13, 16 | Airborne cockpit frame, HUD heading tape reads 02 03 (about 025), and the external frame taken right after showing HDG 028, 357 kts, 3165 ft. |
| `heading-2.ppm`, `heading-2-hdg.ppm` | freeflight-headings.txt:21, 24 | After holding LEFT 5 s: tape reads 35 00, external frame HDG 344, banked left, land at the left edge. |
| `heading-3.ppm`, `heading-3-hdg.ppm` | freeflight-headings.txt:29, 32 | After another LEFT 5 s: tape reads 29 30, external frame HDG 293, steeper bank, 1.5 G. |
| `demo-deck.ppm`, `demo-external.ppm`, `demo-cockpit.ppm` | demo-flight.txt:18, 20, 22 | The scripted DEMO (menu item 1, no code needed): deck view at 0 kts, accelerating chase view, and an airborne cockpit frame with a red alert line. Kept for comparison; not player controlled. |

## Answering the prompt (codewheel procedure)

The challenge is four hexadecimal characters (0 to 9, A to F) and changes between runs and between
attempts. The font draws the letters O and 0 the same and 5 and S the same; D is distinct. The answer is a
number of up to six digits typed on the top key row, then RETURN. A wrong answer produces a new challenge;
three wrong answers reboot, so verify before typing. The flight computer is three stacked disks, each with
a rim of sixteen sectors marked 0 to F. Put the first challenge character of the innermost disk (the one
with the sixteen labelled windows) on a radial line, bring the second character of the middle disk and the
third of the outer disk onto the same line, then read the number in the window labelled with the fourth
character. The manual's own worked example checks out in the viewer.

Our scans are git-ignored. `tools/vamiga-ref/wheel_view.py CHALLENGE out.png` stacks the scans as
described and writes an image, in which you read the window by eye. Calibration: the middle scan is the
reference scale; the inner scan is 0.893 of it and the outer 1.065 (derived from the radii of the
windows and number rows; a wrong outer scale shows a plausible but wrong row). Results are kept only in the
git-ignored `disks/countercodes.txt` (one `CHALLENGE ANSWER` line each); no derived code is committed. The
script command `code @disks/countercodes.txt 5D5A` looks the answer up there and types it with RETURN. The
challenge for this run was 5D5A and its answer was accepted. Because the answer depends on the challenge,
`freeflight-prompt.txt` freezes the challenge in `code-prompt.vasnap`, and `freeflight-takeoff.txt` restores
that snapshot and uses the `code` line.

Decoder check: `tools/codewheel.py` was run against the game for four challenges (the one above and three fresh
ones from new runs to the prompt); the game accepted the decoded answer for all four, with no rejections.

Pitfalls found: restoring a snapshot taken at the prompt works, but pressing the answer in a different
run needs a new derivation; a number cut off by the window edge is not the answer (the viewer must show a
whole number inside the window).

## Keys discovered (verified by before and after frames)

Every row below has a before/after frame pair made by `keys-views.txt` (V) or `keys-flight.txt` (F), both under
`tools/vamiga-ref/scripts/`, frames in `extracted/captures/keys/`; the flight-script lines (L) are in
`freeflight-takeoff.txt` / `freeflight-headings.txt`.

| key (script name) | where | effect | evidence |
| --- | --- | --- | --- |
| any key | credits | continue to the log-disk screen | demo-flight.txt:9 |
| RETURN | log-disk screen | to the main menu | demo-flight.txt:12 |
| 1 | menu | scripted demo | demo-flight.txt:15 |
| 2 | menu | free flight; goes to the countercode prompt | freeflight-prompt.txt:11 |
| digits, RETURN | prompt | answer; a wrong answer re-prompts with a new challenge | freeflight-takeoff.txt `code` line |
| 1 to 4 | location screen | pick a start (1 used: airport on the peninsula) | freeflight-takeoff.txt:15 |
| 1 or 2 | aircraft screen | F/A-18 or F-16 (1 used) | freeflight-takeoff.txt:19 |
| F10 | flight | full thrust: from rest to about 240 kts in 22 s on the runway | keys-flight.txt F10-before/after |
| DOWN (1.5 s) | flight | nose up: altitude 3295 ft to 6805 ft | keys-flight.txt DOWN-before/after |
| UP (1.5 s) | flight | nose down: 3380 ft to 4955 ft, sea ahead | keys-flight.txt UP-before/after |
| LEFT (1.5 s) | flight | rolls and turns left: HDG 028 to 001, ground at the left of the cockpit view | keys-flight.txt LEFT-before/after |
| RIGHT (1.5 s) | flight | rolls and turns right: HDG 028 to 051, ground at the right | keys-flight.txt RIGHT-before/after |
| KPENTER | flight | toggles inside and outside views; the outside view starts behind the aircraft and prints HDG, speed and altitude along the bottom | keys-views.txt view-before/view-KPENTER |
| KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9, KP0, KPDOT | outside view | angle of the outside view: KP2 behind (same as the initial one), KP8 head-on, KP5 from above, KPDOT from below, KP4 and KP6 opposite side views, KP1 and KP3 three-quarter views from behind (opposite sides), KP7 and KP9 three-quarter views from the front (opposite sides); KP0 repeats the KP9 view | keys-views.txt view-KPn frames |
| KP4, KP6 | cockpit view | head left, head right | keys-views.txt head-KP4/KP6-before/after |
| KP2, KP1, KP7 | cockpit view | look back, straight down, straight up (panel hidden for down and up) | keys-views.txt head-KP2/KP1/KP7 |
| KP5, KP8 | cockpit view | no visible change from the forward view (hold and forward shortcut) | keys-views.txt head-KP5/KP8 |
| ESC | demo, prompt | restarts the demo; leaves the prompt | demo test earlier, not re-scripted |

An earlier test with a 5 s hold of LEFT showed HDG rising; a hold that long banks past a steep angle with
pitch, where the heading change reverses, so short holds are the reliable test of direction. The longer
5 s holds in `freeflight-headings.txt` (028, 344, 293 with LEFT) were taken at moderate pitch and match the
short test.

Not tested: throttle steps (the = and - keys), brakes, gear (G), hook (A), HUD (H), weapons, pause (P),
bracket zoom and the mouse. The manual lists them; they are for the manual task, not this one.

## Geometry

The frame is the whole PAL beam image: 912 hires pixels across a 228-colour-clock line (each pixel two
super-hires texels, every second kept) by 313 lines. Rows 0 to 25 are vertical blanking (a grey checker),
columns 0 to 68 horizontal blanking (flat grey `#444444`); black is the border. Everything the game draws is
inside a 640x200 hires box with its top-left corner at frame (186, 43): 320x200 in game pixels (a game pixel
is 2 frame pixels wide and 1 line tall). Measured: every capture has non-black pixels only in frame
x 186..825 and y 43..241 (or less). Conversion: `gx = (frame_x - 186) / 2`, `gy = frame_y - 43`. I
measured this and did not read it from the copper list.

### World viewport (feeds Task 10)

- World area: **x 0..319, y 0..166** in the 320x200 game space (frame x 186..825, y 43..209): 320x167. In
  views without the panel (external views, looking down or up, aircraft-select map) the world fills exactly
  this rectangle; rows below it hold black, plus a one-line text strip at about y 182 to 187 in external views.
  (`head-down` has non-black pixels down to frame y 209 only.) The map screens use a slightly taller area
  (to frame y 221, game y 178).
- Cockpit view: same origin and width, with the panel drawn over the lower part of the world. The panel top is
  curved: game y 144 at the left and right edges, 135 at x 107 and 207, 128 at the centre (x 160). The panel
  runs to game y 198 (frame y 241). So the 3D world visible in cockpit mode is 320 wide and 128 to 144 high,
  with the HUD frame drawn over it, and the full 320x167 buffer is only seen when the panel is off.

## Palette seen

Measured 8-bit RGB from vAmiga's default colour mapping. Channel steps seen: 1c 2b 38 48 59 6a 7b 8d 9f c5.
This is what the emulator displays, not the original 12-bit values.

| role | RGB |
| --- | --- |
| border, background | `#000000` |
| sky | `#2b2b59` |
| sea | `#001c48` |
| land | `#003800` |
| greys for planes, runway, panel | `#1c1c1c`, `#2b2b2b`, `#383838`, `#484848`, `#595959`, `#6a6a6a` |
| afterburner, selection text amber | `#c57b00`, `#7b4800` |
| red (alerts, menu) | `#6a0000`, `#8d0000` |
| green (instruments, menu) | `#006a00` |
| blue (title text) | `#38389f` |

Counts per frame: 11 or 12 distinct colours in flight, 4 on the credits and 6 in the menu. Sky, sea and land
are flat fills, with no dithering.

## Front end: text screens, selections and the flow (note 08, milestone 4)

Captures for `docs/re/08-game-flow.md`. Scripts are under `tools/vamiga-ref/scripts/` (`flow-s1-*.txt`, `flow-s2-*.txt`, `flow-t1-*.txt` and their generators `gen-flow-s1.py`, `gen-flow-s2.py`), logs under `extracted/captures/logs/` and frames under `extracted/captures/flow/` (all git-ignored). A log is one `peek` line per variable per blank (`freeze`, `step`, `peek F:addr`, `colors`), so every table of note 08 is blank-exact and independent of wall-clock jitter. Two geometry corrections to the section above: the text screens' game window starts at frame y 42 (not 43: the first scanline of the screen carries text on the title line), x 186, and a game pixel is two frame pixels wide; with that origin the credits, log-disk and menu frames equal the text engine's output pixel for pixel (note 08 section 3, `text_check.py`).

| capture | made by | what it shows |
| --- | --- | --- |
| `flow-menu.vasnap`, `flow-logdisk.vasnap` | `flow-s1-boot.txt`, `flow-s1-mk-logdisk.txt` | the main menu with its text finished (first-event filter armed, note 08 5.8) and the log-disk prompt waiting for RETURN: starting points of the menu scripts |
| `credits-b2.ppm`, `credits-end.ppm` | `flow-s1-credits.txt`, `flow-s1-credits-long.txt` | the credits mid-typing and complete; timeline in 5.2 |
| `logdisk-variant1.ppm`, `logdisk-variant2.ppm` | `flow-s1-logdisk1.txt`, `flow-s1-logdisk2.txt` | the log-disk prompt for "no disk" and "not initialised" (the drive check patched, 5.3) |
| `after-code-4s.ppm`, `after-code-8s.ppm` | `flow-s1-prompt-ok-wall.txt` | the location screen 4 and 8 s after a right countercode |
| per-blank logs `flow-s1-boot`, `-credits`, `-passrate`, `-anykey`, `-mouse`, `-logdisk-keys`, `-keyhold`, `-menukeys-a` to `-d`, `-menu2`, `-menu-shift`, `-shift-probe`, `-keypad`, `-esc-timing`, `-prompt-wrong`, `-prompt-fail`, `-fail-keys`, `-prompt-esc`, `-loc-a/b/c`, `-loc-start-a/b/c`, `-digit-fade`, `-air`, `-keyqueue`, `-keyqueue2`, `-filter-probe` | `flow-s1-*.txt` | the state pointer, countdown, gate, text list, engine mode, fade variables and colour registers per blank through credits, log disk, menu (every key), prompt (right, wrong, third wrong), location and aircraft selection, and the key queue (depth 31, no repeat, the first-event filter) |
| `escf-*`, `escp-*` | `flow-s2-esc-flight(-dense).txt`, `flow-s2-esc-pause.txt` | ESC from flight and from a pause: cockpit, fade, black, echo line, map, location prompt (5.11) |
| `sesc-3/8/12/20/40/100.ppm` | `flow-s2-shiftesc-flight.txt` | SHIFT-ESC from flight: cockpit, black, the menu being typed |
| `pause-10/30/60.ppm` | `flow-s2-pause.txt` | the pause: the dimmed picture and the label (5.14) |
| `go-8/30/60/100/105/112.ppm` | `flow-s2-gameover.txt` | game over after the fifth crash (allowance poked to 1): cockpit, two red typed lines, black, menu set-up (5.12) |
| `ms-*`, `skip-*` | `flow-s2-mission-start(-long,-skip).txt` | mission start in free flight: aircraft prompt, map fly-in, cockpit; the KP-ENTER skip (6.2, 6.4) |
| `end-m3-success-*`, `end-m4-failure-*`, `end-m9-success-*`, `end-m7a-*`, `end-m7b-*`, `end-bailout-*` | `flow-s2-end-*.txt` | the mission-end overlays, **staged by poking the mode byte and the end code into a free-flight snapshot** (no mission can be flown to its end by the harness): outcome text typed over the frozen cockpit (amber), the black pen-0 text of mission 5, the bail-out end (5.13) |
| per-blank logs `flow-s2-esc-twice`, `-gameover-esc`, `-loc-key5`, `-loc-key7`, `flow-s1-probe`, `flow-s1-prompt-ok`, `flow-s1-prompt-ok-early` | `flow-s2-esc-twice.txt`, `flow-s2-gameover-esc.txt`, `flow-s2-loc-key5.txt`, `flow-s2-loc-key7.txt`, `flow-s1-probe.txt`; the last two are generated by `gen-flow-s1.py` into `extracted/scratch` (they type the countercode answer read from `disks/countercodes.txt`) | a second ESC during the restart; ESC during the game-over text; location key 5 and key 7 starts; the credits-screen variable probe; the right countercode answer typed per blank and the location key or KP-ENTER during the location text |
| `flow-t1-engine-5.txt` | `flow-t1-engine-5.txt` | the text engine per blank after the key 5 in the menu: pass counter against blank counter in six states (note 08 3.8) |
| `flow-t1-palettes.txt` | `flow-t1-palettes.txt` | the 32 colour registers and the fade variables in 12 reference snapshots (the two palette regimes, note 08 4.2) |

## Play modes: demo, training, qualification, flight log, hidden keys (note 08, milestone 4)

Captures for sections 7 to 11 of `docs/re/08-game-flow.md`. Scripts are under `tools/vamiga-ref/scripts/` (`flow-demo-*.txt` with the generator `gen-flow-demo.py`, `flow-training-*.txt` with `gen-flow-training.py`, `flow-qual-*.txt`, `flow-log-*.txt`, `flow-egg-*.txt` with `gen-flow-egg.py`), logs under `extracted/captures/logs/` and frames under `extracted/captures/` and `extracted/captures/logs/` (all git-ignored). The logs are one `peek` line per variable per blank in the style of the section above (`freeze`, `step`, `peek F:addr`), so the tables of note 08 are blank-exact. The reference driver gained commands for these runs (`keydown`, `keyup`, raw key codes `key 0xNN`, `unprotect`, `savedisk`, the option `--rw`; `tools/vamiga-ref/README.md`). A pointer staged with `poke` is stated in the note where it was used.

| capture | made by | what it shows |
| --- | --- | --- |
| log `flow-demo-start-dense.txt` (and `flow-demo-start.txt`) | flow-demo-start-dense.txt | Menu item 1 (demo) from `flow-menu.vasnap`: per blank the state pointer, mode byte, replay mode, gate, pause, countdown, fade level and target, view, key pointer, step counter and text id for 380 blanks (700 coarse): the echo line, the five-pass wait, the fade-in and the mission-start tail to the first replayed step at blank 326 after the mode store (note 08 7.5). |
| log `flow-demo-esc.txt`, `flow-demo-esc-echo.txt`, `-wait.txt`, `-fade.txt`, `-tick.txt` | same names | ESC in the running demo and during each stage of its start: the same variables per blank (330 to 520 blanks each); the restart chain, the stream pointers back at the buffer starts (note 08 7.7). |
| log `flow-demo-end-staged.txt` | flow-demo-end-staged.txt | The end of the stream, staged from `demo.vasnap` with the pointers moved to stream step 2040: the step that reads index 2045 sets the leave request, then the restart chain per blank (note 08 7.4, 7.7). |
| log `flow-demo-full.txt` | flow-demo-full.txt | The demo from `demo.vasnap` (step 47) to step 2044 with the player record, mode and allowance logged at every 100th stream step: take-off, circuit, landing and rollout, no crash (note 08 7.8). |
| log `flow-demo-events.txt`, `flow-demo-boot.txt`, `flow-demo-map.txt` | same names | Per-blank event words and view variables around stream frames 60 to 72 (events execute before the step of their index); the replay pointers in `credits.vasnap` and `flow-menu.vasnap`; the demo's map and view variables. |
| `training-menu.ppm`, `training-menu.vasnap` | flow-training-menu.txt | The main menu reached from the credits snapshot (item 3 and 4 are the training parts); the snapshot is the start of every flow-training script. |
| `training-code-3.ppm`, `training-code-4.ppm`, `training-code-3.vasnap`, `training-code-4.vasnap` | flow-training-prompts.txt | The security prompt over the training scene after menu keys 3 and 4 (so training asks for the code); the challenge frozen (194A and 3CCE) and answered with `code @disks/countercodes.txt`. |
| `training-item3-first.ppm` | (first probe) | Same prompt as training-code-3, the instructor aircraft behind the prompt text. |
| (log only) | flow-training-part1.txt | Part 1 cycle sampled every 50 blanks: lessons 1 to 7 in order and the wrap; per-lesson step counts in note 08 8.12 (cycle 962 steps). |
| `training-part1-kp5.ppm` | flow-training-part1-return.txt | RETURN in lesson 2 skips to lesson 3 (instructor re-placed); KP5 changes the external direction 3 to 8; the next advance resets it to 3. |
| `training-part1-splits-low.ppm` | flow-training-part1-splits-low.txt | Instructor staged below the split-S limit: lesson 4 shows the too-low message and the manoeuvre is skipped. |
| (log only) | flow-training-part1-keys.txt | UP, the digit 5 and F5 do nothing in part 1; KP4 turns the camera. |
| (log only) | flow-training-part1-esc.txt, flow-training-part2-esc.txt | ESC in part 1 starts part 2 (text 0x68, briefing 10, 0x7D flight, no prompt); ESC in part 2 restarts part 2. |
| `training-part1-shiftesc-end.ppm`, `training-part2-shiftesc-end.ppm` | flow-training-part1-shiftesc.txt, flow-training-part2-shiftesc.txt | SHIFT-ESC from either part: the main menu. |
| `training-part2-prompt.ppm`, `training-part2-prompt.vasnap` | flow-training-part2-prompt.txt, flow-training-part2.txt | Part 2 after the take-off (climb staged with poke) at the first prompt (entry 46); peeks: HUD offsets 0 and 0, allowance 1, switches 4A83C to 4A847 = 01 00 00 01 00 01 01 01 01 01 01 01. |
| `training-part2-reposition.ppm`, `training-part2-notready.ppm` | flow-training-part2-keys.txt | S toggles the lead's trail bit; RETURN places the player 99,760 units behind the lead and sets the trigger state; a second RETURN skips a lesson; the trigger starts the next; RETURN mid-lesson shows the not-ready message. |
| `training-part2-collide-end.ppm` | flow-training-part2-collide.txt | Instructor's active bit cleared (staged): end request issued for one step, flight continues. |
| (log only) | flow-training-part2-lost.txt | Part 2 with the lead's active bit cleared for one step: request 3 and code 0xFE, `0x131F4`, the blank-record text, 14 blanks of freeze, code 2 and the flight resumes (note 08 8.9). |
| (log only) | flow-training-brief1.txt, flow-training-brief2.txt | The briefings of part 1 (record 9, 635 blanks of typing) and part 2 (record 10, 816 blanks) after the countercode, per blank (note 08 8.1). |
| `training-part2-crash-end.ppm` | flow-training-part2-crash.txt | First crash in part 2: game-over text, then the menu. |
| `qual-prompt.ppm` | flow-qual-prompt.txt | menu item 5: the security prompt (a challenge) over the carrier deck; snapshot `qual-prompt.vasnap` |
| `qual-briefing.ppm`, `qual-deck.ppm` | flow-qual-airborne.txt | the qualification briefing typed over the cockpit on the deck, and the deck view after a key with the launch notice |
| `qual-airborne.ppm` | flow-qual-airborne.txt | just after the catapult launch; snapshot `qual-airborne.vasnap` |
| `qual-landing-caught.ppm`, `-stopped.ppm`, `-ok0.ppm`, `-ok25.ppm`, `-ok75.ppm`, `-ok175.ppm`, `-end.ppm` | flow-qual-landing.txt | the arrested landing: wire caught, stopped, the success pass, the refuel message, the landing-successful screen, the end |
| `flow-qual-bolter-100.ppm` to `-600.ppm` (six) | flow-qual-bolter.txt | the bolter: deck contact without a wire, rolling off the bow |
| `flow-qual-ditch-100.ppm` to `-500.ppm`, `flow-qual-ditch-stopped.ppm` | flow-qual-ditch.txt | the ditching: the skid in the sea, the stopped aircraft |
| `qual-crash-restart.ppm` | flow-qual-crash.txt | the player back on the catapult after the crash |
| `log-name-prompt.ppm`, `log-name-typed-empty.ppm`, `log-name-20.ppm`, `log-name-21.ppm`, `log-name-after.ppm`, `log-name-ret.ppm`, `log-name-menu.ppm`, `log-name2-menu.ppm` | flow-log-name-prompt.txt, flow-log-name-entry.txt, flow-log-name-entry2.txt | first tour of duty: the callsign prompt, the empty entry, 20 and 21 letters, the 25-letter overlong name on the line, after RETURN, the menu |
| `log-welcome-0.ppm`, `log-welcome-1.ppm`, `log-menu.ppm` | flow-log-screen.txt | the continuing-tour welcome with the shipped callsign, then the menu |
| `log-screen.ppm`, `log-update.ppm`, `log-reset.ppm`, `log-reset-update.ppm` | flow-log-screen.txt | the flight-log screen of the shipped record, after UPDATE, after RESET (all zeros), the callsign prompt after RESET and UPDATE |
| `log-missionlist-shipped.ppm`, `log-missionlist-all.ppm` | flow-log-missionlist.txt | the selectable-missions list for the shipped log and with all six unlock bytes set |
| `credits-zero.ppm`, `credits-rw.ppm` | flow-log-boot-zero.txt, flow-log-boot-rw.txt | the credits screens of the two write-enabled boots (snapshots `credits-zero.vasnap`, `credits-rw.vasnap`) |
| `egg-loc-after6.ppm`, `egg-loc-after5.ppm`, `egg-loc-flight5.ppm`, `egg-loc-flight5b.ppm` | flow-egg-loc5.txt, flow-egg-loc5b.txt | The location selection after the digit 6 and the digit 5 (both accepted: the aircraft selection follows) and the flights that follow: runway start of airfield 1 in both. |
| `egg-select-after-p.ppm`, `egg-select-after-z.ppm` | flow-egg-keys-select.txt | The location selection after P (paused, same picture) and after Z and DEL (unchanged); also P, P, Z, DEL and the Amiga key then 2 as peeks. |
| `egg-checksum-60.ppm`, `-100.ppm`, `-150.ppm`, `-170.ppm` | flow-egg-checksum.txt (boot of a disk copy with one bit changed in `pix/frnt5`) | Boot sequence, then a black screen with one red line: the checksum-error text, a digit and four hexadecimal digits; unchanged from 100 s to 170 s. |
| (log only) | flow-egg-sweep-0.txt, flow-egg-sweep-1.txt, flow-egg-sweep-2.txt, flow-egg-sweep-3.txt, flow-egg-sweep-4.txt, flow-egg-sweep-5.txt, flow-egg-sweep-6.txt, flow-egg-sweep-7.txt, flow-egg-sweep-8.txt (written by gen-flow-egg.py) | Key census in free flight from `airborne.vasnap`: nine groups of 12 raw key codes (101 in all, ESC and the shift keys left out), two control samples then, per key, the variable block `0x4A700`-`0x4AE3B` and the first 256 bytes of the player record before and after a 0.15 s press and 0.7 s wait; logs `flow-egg-sweep-N.txt`. No frames. |
| (log only) | flow-egg-phases.txt | Thirteen variables of the key handler's phase (`V:4A83F`, `4A849`, `4A965`, `4A966`, `4A88B`, `4A95E`, `4A903`, `4A898`, `4A930`, `4A88D`, `4A865`, `4A84D`, `4A866`) read from six snapshots (credits, countercode prompt, location selection, aircraft selection, demo, airborne). |
| (log only) | flow-egg-mouse.txt | The button state words `V:4A836`, `V:4A838`, the data byte `0x1A84A` and the CIA register `0xBFE001` in the airborne snapshot, before and after one mouse click: the state word reads 1 throughout. |
| `egg-amiga-f1.ppm`, `egg-amiga-f1-flight.ppm` | flow-egg-amiga-f1.txt | The location selection after the left Amiga key and F1 (the aircraft selection follows; `V:4A900` = 10) and the flight that follows. |
| (log only) | flow-log-clock.txt | The mission clock: `V:4ABAA`, `V:4A9CC` and the record's flight-time long before and after 500 blanks in the air and an ESC (10 s counted, 20 read: note 08 10.7). |
| (log only) | flow-log-weapons.txt | The weapon counters of the log record (cannon rounds, AIM-120 and AIM-9L fired) in free flight: SPACE with each weapon selected by RETURN (note 08 section 10). |
| (log only), `qual-prompt-rw.vasnap` | flow-qual-prompt-rw.txt | A never-used log on a write-enabled disk (from `credits-zero.vasnap`): callsign TEST, menu item 5, the snapshot at the security prompt that `flow-qual-landing.txt` restores (run with `--rw`). |
| (log only) | flow-log-name-overrun-a.txt, flow-log-name-overrun-b.txt | The name-entry overrun (note 08 10.5): 27 characters with a RETURN, and ten letters plus a space followed by letters until the entry ends by itself; then the log screen and the saved file. |
| (log only) | combat-dying-lead.txt (generator `gen-combat-dying-lead.py`) | Note 07 section 5: the training lead of part 1 (record 4, a real placed class 0x10 aircraft) given the hit state of the gun's third hit (W0 0x0200 and out of control) in the real world update: the dying count of 15 passes, the expiry, the uncounted kill and the record kept (airborne, no hit bit). |
| (log only) | combat-dying-probe.txt (generator `gen-combat-dying.py`, which writes the staging scripts `air` and `low`; the generated scripts are not kept: they poked the snapshot's whole player record, vertices included, and the machine stopped after the poke in most runs) | Note 07 section 5: a staged hostile (the first 0xA4 bytes of the player's record in slot 8, class 0x12) given the gun's third-hit state. The first run showed the count-down of the counter; later runs stopped the machine after the poke, so the counted kill rests on the rig. |
| (log only) | combat-seat-eject-cockpit.txt (250 samples of 4 frames), combat-seat-eject-long.txt (330 samples of 30 frames) (generator `gen-combat-seat.py`) | Note 07 section 6.4: a real ejection from `airborne.vasnap` (SHIFT and E): the nibble 9, 8, 7, 1 to 6, the seat spawning at 3 (class 0x30, counter -19), the view to the seat, the hold, the seat landing about 1,100 passes later, request 2 and the end code 0xF0, the return to the menu. |
| (log only) | combat-seat-eject-long.txt, combat-seat-eject-cockpit.txt (generator `gen-combat-seat.py`); combat-seat-landing.txt (generator `gen-combat-seat-landing.py`, queued for the emulator lock, not run when the note was written) | Note 07 section 6.5: the ejection to the end code 0xF0 (the long log) and the per-frame landing script. An earlier attempt to lower the aircraft and the seat inside the hold (`combat-seat-crash`) did not reach the ground and was dropped. |
| (log only) | combat-fx-jolt.txt (hand-written script) | Note 07 section 13.3: from `airborne.vasnap` the jolt kind byte `V:4A878` poked to 1, 2, 3 and 4 in turn (16 samples of one video frame each, then the full-width view word `V:4AA3E` poked to 50 with kind 1): the index `V:4A947` steps 1, 2, 3, 4 over four game passes (one pass is 6.4 video frames: `V:4A992` advanced 232 to 242 in 64 samples), `V:4A990` = -1, +1, 0, 0 for kinds 1 and 3, `V:4AA3C` = 143, 145, 144, 144, `V:4A9D0` = -40, +40, 0, 0; a kind poked into a running jolt continues at the running index; with `V:4AA3E` = 50 the kind is cleared and no offset is written. |
| (log only) | combat-radar-demo.txt (generator `gen-combat-radar.py`; the re-timed run is `combat-radar-demo2.txt`, 63 samples of 62 video frames; the game runs about 12 video frames per pass; a first run took 86 samples at 2 to 60 frames) | Note 07 section 12: from `demo.vasnap` (mode 3, stream step 47): the hostile (record 10, class 0x12, detected bit set) is in the radar list with the threat byte `V:4A925` = 4 and the alert code 0x0E from stream step 258 (before the demo's T at 263); the target `V:4AA78` = 0x1400 (record 10) is designated by step 326 with `V:4A920` = 1 and the message line shows the target data (name field, then the heading page, the speed page, the altitude page: the page word `V:4AA7C` 2, 3, 1 at steps 326, 385, 500, advanced by the timer byte `V:4A93E`), the lock state `V:4A96C` = 1 with the AIM-120 selected (`+0x63` = 0x3B) at 594 to 595 (range `+0x4A` 0x12CA, `+0x38` of the hostile 0x80 = the leader field), and from step 640 the designation is gone (`V:4AA78` = 0xFFFF), record 10 out of control (`B20` 0x42) and dying (`W0` 0x54C1), the first weapon record holding a spent class-1 missile (0x509A); the line buffer matches the model of 12.8 (name at character 4, label at 13, digits at 18 to 21). `V:4A96C` is left at 1 after the target is gone. |
| (log only) | combat-cm-keys.txt (generator `gen-combat-cm.py keys`, 40 samples of 1 frame) | Note 07 section 9: `airborne.vasnap`, F at sample 3, C at 15, J at 27: the stores 16 to 15, the cool-downs 30, the posts `0x4026` and `0x4028`, the requests 2 and 1, the pool slots (flags `0x1401`, life 30; `0x2401`, life 60), `V:4A944` = 1 and `V:4A945` = 3, the reservoir 5 to 4 to 5, `V:4A96D` and the lamp timer. |
| (log only) | combat-hit-player-odd.txt (generator `gen-combat-cm.py hit 60 odd`; 60 samples; pokes only the record fields +0x00 to +0xA3) | Note 07 section 10.4: the odd-parity form of the staged contact: the inactive class-0x12 aircraft on the player's track, the frame counter pinned to `0x00E9`; the contact at sample 31: the player's W0 `0x91C8`, the post `0xD00B`, `V:4A984` `0x0C41`, the jolt byte 1, no cool-down; the aircraft is not marked. |
| `range-number-0.ppm` to `range-number-3.ppm`, log only for the peeks | combat-range-number.txt (from `demo.vasnap`, run by `stepuntil F:4A96C 1 eq 1`: 4,853 video frames, then three single-frame steps) | Note 07 section 12.7, the range number: with the lock state 1 on the demo's AIM-120 (pass 0x23A, the pipper at (154, 76), `V:4ABFE` = 0xFF18, the closing delta -232) the number string at `V:4A8B2` reads a blank sign cell and the digits 0232; the frame shows the four digits as 3 x 5 glyphs in pen 13 at x 192, 196, 200 and 204, rows 123 to 127 (the capture's game area starts at texture column 186 and row 42, fixed by the HUD outline top at row 37). |
| (log only) | combat-panel-first-demo.txt, combat-panel-first-train1.txt, combat-panel-first-train2.txt, combat-panel-first-qual.txt (hand-generated: the menu snapshot with key 1; `training-code-3` with code 194A; `training-code-4` with code 3CCE; `qual-prompt` with code F7BD; 140 to 200 samples of 8 to 20 video frames of `V:4A8EE`, `V:4A8EF`, `V:4A84D`, `V:4A83D`, `V:4A984`, `V:4A992`, `V:4A866`, `V:4A95E`, the state pointer; the demo run also `V:4A90F`) | Note 07 12.1: the redraw counters at the first flight pass of each mode (0 and 0 / `0xFF`), the gate-open passes that count them down, the periodic writer at n = `0x10`; `V:4A90F` = `0xFF` throughout a first demo. |
| (log only), `panel-block-000.ppm` to `panel-block-047.ppm` (git-ignored) | combat-panel-block.txt (hand-generated: `airborne.vasnap`, KP-ENTER at sample 2, 48 samples of 2 video frames with `V:4A8EE`, `V:4A8EF`, `V:4A83D`, `V:4A992`, `V:4A93B`, `V:4A96C` and a frame dump each) | Note 07 12.2: the switch to the external view: `V:4A8EE` and `V:4A8EF` = 3 in the switch pass `0xE8`, counting 2, 1, 0 and set again at `n` = `0xF0`; the cockpit panel is not drawn over the external view (0 non-black pixels in the panel rows after the switch against 2,214 before). |
| (log only) | combat-demo-mig.txt, combat-demo-mig-early.txt (per-pass record samples of the demo's MiG, record 10, passes 224 to 330 of the demo, from `demo.vasnap`) | Note 07 section 4 (Part A's AI comparison): the hostile's record per pass in the demo's fight, used to compare the port's AI trajectory with the original's. |
| (log only) | combat-fire-demo.txt (generator `gen-combat-fire.py demo`; 70 samples of 1 video frame from `demo.vasnap`, run to the demo's fire event by `stepuntil F:54E5C 4 eq 0xC5473F`) | Note 07 sections 7.6, 8.1 and 13.4: the demo's AIM-120 launch: `V:4A872` = 1 at pass `0x0257`, stock `0x24` to `0x23`, record 1 W0 `0x10CA`, life 199, target byte `0x8A`, speed word `0x2443` then +`0x1E0` per pass, `V:4A968` = `0xFB`, `V:4AC0C` = 8. |
| (log only) | combat-fx-burst.txt (generator `gen-combat-fx.py`) | Note 07 sections 11 and 13: the dying burst, the smoke trail and an effect object on the staged copy of the player's record (the template descriptor is staged with the driver command `copy <src> <dst> <len>` (commit 076789d, `tools/vamiga-ref/README.md`), which copies the bytes the running game holds into the target record, so the script names addresses only and carries no model data; frames `fx-burst-NNN.ppm` are written next to the log, git-ignored). |
| (log only) | combat-hit-demo.txt (150 samples of 3 frames) and combat-hit-demolong.txt (215 samples, records 1 and 10 only), generator `gen-combat-hit.py demo` / `demolong`, both from `demo.vasnap` at the fire event | Note 07 sections 8.4, 8.9 and 5.3: the demo's AIM-120 hits the MiG (record 10) in pass `0x027C`: missile removed, MiG W0 `0x54C1`, W2 `0x1001`, B20 `0x42`, state 1, `V:54E72` `0x1400`, log `+0x40` = 1, counter 15; `V:4A963` = 1 in pass `0x028B`. |
| (log only) | combat-hit-gun.txt, combat-hit-gunkill.txt (generator `gen-combat-hit.py gun` / `gunkill`; 150 samples of 3 frames; `airborne.vasnap` with the placed MiG-29 of the mode-3 row staged with `copy` into record 8 about 600 du ahead (the template descriptor copied inside the machine; position and state fields poked), the gun selected by poking `E+0x63`, SPACE held) | Note 07 sections 7.3, 8.2: 3 log rounds per shot, five shots per burst, two tracer hits counted (`+0x3C` = 2, the MiG's own nibble 2); with the MiG's nibble poked to 2 the first hit marks it (W0 `0x52C1`, W2 `0x1000`, B20 `0x42`, state 1, `V:54E72`) and the kill is counted fifteen passes after the dying start. |
| (log only) | combat-hit-missile.txt, combat-hit-missilehit.txt (generator `gen-combat-hit.py missile` / `missilehit`; 150 and 200 samples of 3 frames; the MiG staged with `copy` about 1,450 and 5,800 du ahead, designated by `V:4AA78` = `0x1000`, the Sidewinder fired by SPACE at sample 3) | Note 07 sections 8.1, 8.3, 8.4: launch in pass `0x00EA` (`+0x42` = 1), `+0x26` from 19, the rates 0 until it reads 9, the divisor ramp with the low pass (106, 146, 173, ...), the proximity bit W2 `0x2002`, the lost mark, the rate 128 for ever; both pass the MiG (228 and 167 du): no hit. |
| (log only) | combat-hit-player-even.txt, combat-hit-player-flare-even.txt, combat-hit-player-flare.txt (generator `gen-combat-cm.py hit 60 even`, `hitflare 60 even`, `hitflare 60 odd`; 60 samples) | Note 07 section 10.4: a stationary inactive class-0x12 aircraft on the player's straight track, the frame counter pinned by a poke each sample (even `0x00E8`, odd `0x00E9`). Even: the contact gives W0 `0x91C8`, B20 `0x02`, the post `0x9014`, `V:4A984` bit 8, the jolt byte 1. Even with a flare released before (`V:4A907` 23): the hit bit only. Odd with a flare: `0xD00B` and the jolt byte regardless of the cool-down. |
| (log only) | combat-cm-missile.txt, combat-cm-missile-flare.txt (generator `gen-combat-cm.py missile 150`, `missileflare 150`; combat-cm-missile-90.txt is an earlier 90-sample run) | Note 07 section 9.4: a hostile Sidewinder poked into record 9 as a copy of the player's record, 45 passes of the player's velocity behind it. Without a decoy: alert `0x8010`, the destination words on the player, the hit at frame counter 250 (`0x9014`, damage bit, jolt). With a flare released at frame 1: the lost mark B20 `0x20`, the miss at (-29, -127, 50) draw units, no damage. |

## Missions (note 08 part C, milestone 5)

Run all of them with `--rw` under `flock /tmp/ref.lock`; the logs are git-ignored under `extracted/captures/mb/` and `extracted/captures/logs/`. The state pointer `V:1A6BC` holds run-time addresses (flat + 0xBF7B28 in these snapshots; `stepuntil` on a state needs them). "Staged" = a poke set the state, as stated in note 08 section 12/13.

| frames / snapshot | script | what |
| --- | --- | --- |
| `mb/menu.vasnap`, `mb/m3-prompt.vasnap` to `m7-prompt.vasnap`, `mission-m6-prompt.vasnap` to `m8-prompt.vasnap` | mission-brief-menu.txt, mission-brief-m3-a.txt to m8-a.txt, mission-prompts.txt (generated by gen-mission-brief.py, gen-mission-out2.py) | the six missions from the menu F-key to the countercode prompt, per 10 blanks: states, fade, intro and briefing ids, the placement counts, the approach target and the camera stages (note 08 13.5); F4 to F6 with the previous mission's unlock byte poked (staged) |
| `mb/m3-b-*.ppm` | mission-brief-m3-b.txt, mission-brief-m4-b.txt | the orders, the aircraft prompt (F1 only), the approach, the notices and the first flight pass of F1 and the carrier start of F2, per few blanks; the log's missions-started counter 4 -> 5 |
| `mb/m3-flight.vasnap`, `mb/m4-flight.vasnap`, `mb/m5-flight.vasnap` | mission-m3-start.txt, mission-m4-start.txt, mission-m5-start.txt | F1, F2 and F3 in real time to the first flight pass; the records of the first pass |
| `logs/mission-m3-ok.ppm`, `-ok-text.ppm`, `-ok-complete.ppm`, `mb/m3-after.vasnap` | mission-m3-ok.txt, mission-m3-ok2.txt | F1: the MiG moved beside the player, the lock set (staged); the counter V:4A927 30 -> 0xFF, success, debrief, and the 0xFC landing (staged lift-off bit) |
| (log only) | mission-m4-ok.txt, -fe_odd.txt, -fe_even.txt, -fd.txt, -dead.txt, -pause.txt, -esc.txt (gen-mission-f23.py) | F2: success with the four frozen passes, the two failure texts by parity, the 0xFD case, code 4 dead, P and ESC in the frozen passes (all staged records, the principal aircraft inactive) |
| (log only) | mission-m5-ok.txt, -fail.txt, -shadow.txt | F3: success (staged counters), the thief's cell x, the shadowing timer |
| `logs/mission-m6-*.ppm` | mission-m6-out-a.txt, mission-m6-ok.txt (gen-mission-out2.py, gen-mission-pod.py) | F4: the parachute released on the deck (failure) and moved onto the pod (success, staged) |
| `logs/mission-m7-*.log` | mission-m7-out-a.txt, mission-m7-out-b.txt | F5: the real failure after 12,555 frames, both parities (staged), the staged success and the 0xFC landing |
| `logs/mission-m8-*.log` | mission-m8-out-a.txt, mission-m8-out-b.txt | F6: the region triggers (position staged), success with and without a lock (the delay 0xFF04 and 0x1004), the 0xFC landing (staged) |
| (log only; frames `logs/followup-callsign-ring*-N.ppm`) | followup-callsign-ring.txt, followup-callsign-ring-slow.txt (gen-followup.py; `--rw` with the zero-record ADF, from `credits-zero.vasnap`) | note 08 5.8: SPACE, then 11 letters 10 or 15 blanks apart before the callsign entry opens; per 5 blanks the key ring, the typed buffer, the counters, the record's callsign; the ring is zero when the entry opens |
| (log only) | followup-demo-esc.txt (gen-followup.py; from `flow-menu.vasnap`) | note 08 7.7: ESC 15, 18 and 21 blanks into the demo's fade-in, three runs, the fade bytes and the 32 colour registers per blank |
| (log only) | followup-pause-gameover.txt (gen-followup.py; needs `crash-setup-dive.txt`) | note 05 17.17 rule 7: the game-over count with the allowance poked to 1, P pressed at blanks 1, 20 and 105 and again later; the states, the countdown, the engine byte and the pause byte per blank (3 runs, 7,700 lines: keep under the driver's script limit) |
| (log only) | followup-pause-screens.txt, followup-pause-notice.txt (gen-followup.py; from `airborne.vasnap`, `aircraft-select.vasnap`) | note 08 5.14, 6.4, 12.11: P in flight with the pass counter `V:4A879` and the pause byte per blank, P in the staged outcome screen (mode 3, code 0xFF, `V:4A965` = 1), P in the take-off notice screen with and without a second P |
| (log only) | followup-brief-camera.txt, followup-brief-camera-s7.txt, followup-radar-scan.txt, followup-brief-skip.txt, followup-visibility.txt (gen-followup-b2.py) | Task 2 B2 (note 08 13.4, 12.5, 6.4): F2 briefing camera per 2 blanks through the orders and per blank in stage 7 (the camera stays at the overview), F1 radar walk counter per 2 blanks with the redraw counter, KP-ENTER 100 blanks into the orders, the stage 7 flag on sub-lists 2 and 3 of F2 and F4 (completion byte poked) |
| (log only) | followup-airfield-start.txt, -2.txt, followup-approach-state.txt (gen-followup-b2.py) | Task 2 B2 (note 08 12.14 C13): free flight from the location screen with keys 1 to 4 (aircraft key 1 for 1 to 3): the step counter, state and camera every 4 blanks through the approach `0x12D58`; the mover's whole state at the entry for keys 1 and 4 (replayed in the rig by `approach_replay.py`) |
| `followup-mapcam-0.ppm` to `-2.ppm` (git-ignored), log only for the peeks | followup-mapcam.txt (hand-written; `location-select.vasnap`, three single-frame steps) | Note 04, "The start marks of the overview map": the overview camera state at the location screen (camera mode 5, map flag `V:4A865` = 0xFF, camera longs 0x10800000, 0x03000000, 0x10C00000, matrix 168, 0, 0, 0, 0, 252, 0, -128, 0, row 179, step 50) and the frame with the four start marks; the rig run of `0x2FAB6` on that state with the carrier record poked reproduces its 4 dots and 36 amber pixels |
| `followup-mapcam-air-0.ppm` to `-5.ppm` (git-ignored), log only | followup-mapcam-air.txt (hand-written; `aircraft-select.vasnap`, six single-frame steps with `V:4A93B`) | Note 04, same section: camera mode 6, `V:4A90F` = 0, `V:4A93B` advancing by 3 a frame; the six frames hold the same chosen number (529 amber pixels, 2 grey): no blink on the aircraft screen |
| (log only) | followup-hudstep-carrier.txt, followup-hudstep-airfield.txt (hand-generated; `aircraft-select.vasnap`, the carrier run with `V:4A900` poked to 3; two samples in the approach, then `stepuntil` the state `0x12DB8` and 80 samples of one frame) | Note 06, "The HUD step and the window row at each start": the approach has step 50, row 179, external view, HUD switch 0; from `0x12DB8` the cockpit with step 0, row 144, HUD switch still 0 (the carrier run reaches `0x12EDA`) |

## Sound (note 10, milestone 6)

Made with `audtrace` and `wav` (README of `tools/vamiga-ref`) from `tools/vamiga-ref/scripts/gen-sound.py`; the logs are `extracted/captures/logs/sound-*.txt`, the recordings `extracted/captures/audio/*.wav` (hard panning, no filter; git-ignored,
described here, never committed). One `aud` line per video frame unless noted: the frame, the line, the registers that changed and then the location, length, period and volume of the four voices. The checks are `tools/m68k-rig/tests/lists_model/sound_capture_check.py`
(`make -C tools/m68k-rig test-sound-captures`). Each script is run alone under `flock /tmp/ref.lock`.

| log (recording) | script | what it shows |
| --- | --- | --- |
| `sound-boot.txt` (`.wav`, 164 s) | sound-boot.txt (cold boot, `step 8200` under `audtrace on frame`) | The start-up music begins at frame 4,259 (85 s after power-on): both voices at volume 63 and period 358, `texti0a` 14 blocks, `texti0b` 2, then the 79.5-second loop; 12 changes of the loaded sample within 0.7 frame of the block schedule (note 10 4.2). The CIA peeks at its end were lost (an early driver version crashed on `audtrace off`): see `sound-cia` |
| `sound-cia.txt` | sound-cia.txt (credits snapshot) | CIAA port A `0xFC`, direction `0x03` (the power LED and filter bit is 0: filter on); CIAB port A `0xFF`, direction `0xC0`; the enable longs `0x000007F7`; level `0x3F0000`; flag 2 (note 10 1.5, 2.3) |
| `sound-click.txt` (`.wav`) | sound-click.txt (credits snapshot, the any-key, per-write `audtrace on`, 90 frames) | The key click: DMA on in the pass (location, length, DMA, period 358, volume 0 in that order), period 300 and volume 2 for three blanks, DMA off in the fourth; the master cap falling one unit every four blanks |
| `sound-menu.txt` (`.wav`, 60 s) | sound-menu.txt (credits snapshot; SPACE, 250 frames, RETURN, the menu) | The ramp after the any-key: the level cell blank by blank from `0x3F0000` to `0x1F0000` by `0x4000` (149 consecutive blanks agree with the rule), the voice volumes 63 down to 31 in 125 blanks; the log-disk prompt's clicks |
| `sound-menu-mode.txt` (`.wav`, 18 s) | sound-menu-mode.txt (`flow-menu.vasnap`, key 5) | The echo of a mode key: target 0, the cap falls from 31; 48 blanks after the key the dispatch sets the flag 0, the level `0x3F0000`, stops voices 0 and 1 and starts the wind at volume 8 (periods 392, 422); 118 blanks later the first engine pass: cockpit pair 764/766, volume 40 |
| `sound-throttle.txt` (`.wav`) | sound-throttle.txt (`airborne.vasnap`; F1 twice, F5, F10, F3, F10, F1) | The engine pitch and volume against the throttle every 6 blanks with the inputs of the per-pass routine peeked (note 10 5): military power 514/516 at volume 62 to 63, idle 755 to 782 at volume 40, the wind at volume 1 with the throttle at 0, the cue 0x80 near the sea (324/200, volume 10) and the crash noise with the stop of all voices |
| `sound-afterburner.txt` | sound-afterburner.txt (`airborne.vasnap`; F10, `=` held, `-` held) | The afterburner: the external pair at 549 and volume 30 after the switching-on notice, then 514/516 at volume 21; switching off: the cockpit pair at 549, volume 63 |
| `sound-views.txt` (`.wav`) | sound-views.txt (`airborne.vasnap`; KP-Enter twice, M twice, P twice, KP4, KP2) | The external pair 470/472 at volume 36 outside, the cockpit pair 514/516 at 63 inside; the map and the pause stop all four voices, the engine returns with the notice |
| `sound-gear.txt` | sound-gear.txt (`airborne.vasnap`; G four times, A, KP-Enter) | The gear sound on voice 2: period 700, volume 10 in the cockpit (102 and 64 frames) and 5 outside (105 and 62 frames); the byte `V:4A93D` 0x83, 3, 2, 1, 0 once per second |
| `sound-gear-touchdown.txt` | sound-gear-touchdown.txt (`cockpit-ground.vasnap`; F10, DOWN, G, G) | The ground contact when the nose settles (voice 2, 1,731 words, period 310, volume 15, DMA off after 16 frames); G while the wheels are down is refused with a beep (voice 3, volume 4) |
| `sound-crash.txt` (`.wav`) | sound-crash.txt (`dive-precrash.vasnap`) | The cue 0x80 in progress, then the hand-over: voices 0, 1, 2, 3 stopped in one frame, the crash noise (period 15,114, volume 63 falling to 0 over 120 blanks, 121 frames), the engine back 97 blanks later |
| `sound-eject.txt` (`.wav`), `sound-eject-ext.txt` | sound-eject.txt, sound-eject-ext.txt (`airborne.vasnap`; SHIFT-E, the second after KP-Enter) | The ejection bang on voice 2: volume 31 falling to 0 when the key is pressed in the cockpit view, 63 in the outside view, 121 frames each; the seat tone (200, volume 14, 8 on and 3 off five times); the wind at volume 16 for the seat |
| `sound-gun-launch.txt` | sound-gun-launch.txt (`airborne.vasnap`; the weapon selection cycled with RETURN, SPACE held, then F, C, J) | Weapon 0x0D fires nothing; 0x3D and 0x2D launch (voice 2, 6,909 words, periods 145, 147, 146, volume 11 rising to 25 then 61 falling, 69 frames); 0x1D is the gun (period 150, volume 22, restarted by each shot); the flare and the chaff (period about 14,000 to 15,000, volume 28, 49 frames) |
| `sound-flight-start.txt`, `sound-flight-start-m3.txt` | sound-flight-start.txt, sound-flight-start-m3.txt (`code-prompt.vasnap`, `mb/m3-prompt.vasnap`; the countercode in real time) | Free flight: the wind at volume 8, the external pair at 776/778 volume 12, then the cockpit pair 764/766 volume 40; mission 3: the `text201` pair (period 179 and 181, volume 40) for under 40 blanks, then the wind |
| `sound-cue.txt` | sound-cue.txt (`demo.vasnap`, `stepuntil` the AIM-120 fire event) | The demonstration's gear sound (61 frames, period 700, volume 10), the cue 0x02 (70 frames), the in-box pulses (577 Hz, 9 on and 4 off) and the alert tone until stopped, the launch at the fire event (period 145, 69 frames) |
| (log only) | followup-combat-selector.txt (generator-free: 30 restores of `flow-menu.vasnap`, key 1, `thaw` before each restore; `V:4ABAE`, `V:4ABB0`, `V:4ABD4`, `V:4ABD0` per start) | Note 07 2.4: 13 of the 30 reached the demonstration's start; the clock long is 20,000 x the blank number within the second + 1,172 microseconds (1,166 to 1,173): the selector is not uniform. |
| (log only) | followup-combat-selector-missions.txt (generated from the briefing captures' `chal` lines: the answer of `mb/mM-prompt.vasnap` looked up in the git-ignored countercode table, the briefing to the first flight pass for modes 4, 6 and 8; mode 3 from an earlier, stopped run) | Note 07 2.4: microseconds 401,612 (mode 3), 681,620 (4), 121,594 (6) and 61,635 (8): delta = 1,612, 1,620, 1,594 and 1,635 after the blank. |
| `ej-cp-000.ppm` to `-149.ppm`, `ej-ext-*.ppm`, `el-*.ppm` (git-ignored, `extracted/captures/followup/`), log only for the peeks | followup-combat-eject-frames.txt, -ext.txt, -ext-long.txt (generator `gen-followup-combat.py eject-cockpit`, `eject-external`, `eject-external-long`; `airborne.vasnap`, SHIFT and E, one sample and one frame dump per video frame; the long run 330 samples of 4 frames) | Note 07 6.5: the nibble 10, 9, 8, 7, 1, 2, 3 (the seat in the sixth pass); the five cockpit pictures before the seat are the unchanged cockpit with the EJECT lamp flashing; the first seat picture is 15 frames after the spawn with the old panel, the next without it; from an external view (the level byte poked to 2) the viewed record stays the player for 39 passes and then the view switches to the seat with the level byte `0xFF`; the seat is drawn as an open canopy. |
| `pc-*.ppm` (git-ignored), log only | followup-combat-parachute.txt (generator `parachute`; the low nibble of the gear byte poked to 15, 300 samples of 12 frames) | Note 07 6.5: class `0x31`, counter from -18, no view switch, 769 position units per pass downward, 600 passes without a landing. |
| (log only) | followup-combat-kills.txt, followup-combat-kills-missile.txt (generator `gun`, `missile`; a placed MiG in formation 12 and 40 passes ahead, designated; the gun held, the Sidewinder fired; 170 and 200 samples of 6 frames) | Note 07 8.9, 5.3: the gun kill from a fresh MiG (three hits, W0 `0x5241`, W2 `0x1100`, B20 `0x42`), the kept wreck counted again at each dying cycle (`V:4A963` 1, 2, 3), a staged Sidewinder hit in pass 28 after the launch and the kill counted 15 passes later. |
| (log only) | followup-combat-gunkill-post.txt and the part `gunkillpost-ext` of followup-combat-batch-a.txt (generator `gunkillpost`, `gunkillpost-ext`; one sample per video frame from the crashed bit; the MiG 500 and 150 du ahead, the cockpit and the chase view) | Note 07 12.5: no `0x4016` post in either view; the designation drops two frames after the crashed bit. |
| `fc-flare-*.ppm`, `fc-chaff*-*.ppm` (git-ignored), log only | followup-combat-flare-chaff-flare.txt (generator `flare`; KP-ENTER, the request byte `V:4A943` poked, 60 single-frame steps with dumps and the camera variables) and the chaff parts of followup-combat-batch-a.txt, -batch-c.txt (`chaffback`), -batch-e.txt (`chaffhead`) | Note 07 7.7: the flare star is 50 pen-9 pixels, three lines, equal to the rig's line calls for the logged slot and camera (`flare_chaff_replay.py`); no chaff pixel found at the predicted positions in three views. |
| `rk-*.ppm`, `sc-*.ppm`, `w2-*.ppm` (git-ignored), log only | followup-combat-batch-a.txt (parts `chaff`, `gunkillpost-ext`, `rangekey`, `scope`, `wrap`, `threat`), followup-combat-batch-d.txt (`wrap2`, `crashafter`: the first, collided-label version of the crash-after-eject capture) (generator `gen-followup-combat.py`; several captures in one run, a `thaw` and a restore before each) | Note 07 12.3: 39 presses of the range key give one place per press of the cycle 0xD, 0xB, 9 with the labels 40, 10 and 2 miles; blips at 1.01 to 6.89 miles on the rows the 2^n rule predicts. 12.4: `V:4A925` = `0x10` and the alert `0x8010` for a Sidewinder aimed at the player. 11.3: effect objects with life words from `0x7FFF` and `0x80` up draw nothing (0x7F and 0x40: the starburst). |
| (log only) | followup-combat-batch-b.txt (`firstpass`: `aircraft-select.vasnap`, key 1, 350 samples of 2 frames; `crashnibble`: `dive-precrash.vasnap`, SHIFT and E, 260 samples of 4 frames) | Note 07 12.1: `V:4A8EE` = 0, `V:4A8EF` = `0xFF` from the first sample to pass 93 of that run; 6.5: the crash in the third pass with the nibble at 8: nibble 0, end code 4, hold 40, allowance 5 to 4. |
| (log only) | followup-combat-batch-c.txt, -batch-e.txt (`seatshot`: the seat's W0 poked to the crashed bit, then to the dying bit), followup-combat-batch-f.txt (`threat1`: a class-1 missile behind the player) and followup-combat-crash-after-eject.txt (`crashafter2`: `dive-precrash.vasnap`, height poked to 36,808, SHIFT and E) | Note 07 6.5, 12.4: a seat marked crashed keeps flying; with the dying bit it is freed, the release bit and the nibble 0 two passes later; the crash with the seat in flight clears the nibble and hands over as any crash; a class-1 missile gives `V:4A925` = `0x20` and the alert `0x8012`. |
| (log only, `extracted/captures/logs/sound7-*.txt`) | sound7-gun.txt, sound7-bang.txt, sound7-bang-ext.txt, sound7-bang-overwrite.txt, sound7-cues.txt, sound7-cues-hold.txt, sound7-pod.txt, sound7-fixedcam.txt, sound7-seat.txt, sound7-landing.txt, sound7-swallow.txt, sound7-tick.txt, sound7-approach.txt, sound7-restart.txt, sound7-pause.txt, sound7-pause-frames.txt, sound7-pause-at-376F8.txt, -37B18.txt, -37C2E.txt, sound7-restart-at-376F8.txt, -37B18.txt (generator `gen-sound7.py`; `audtrace on frame` for the register state per frame, `audtrace on nodmacon` for the per-write traces of `sound7-pause` and `sound7-restart`; the staged scripts poke only run-time variables: the cue long, the sound-request bytes, the gear countdown byte, the altitude and speed words, the weapon selector; the staged MiG is the one of followup-combat-kills.txt) | Note 10 (milestone 7): the gun bursts, the explosion bang and its overwrite, the cue bits, the pod release, the gear sound in the fixed camera and with the camera on the seat, the touchdown volume, the swallow, the seconds tick over 120 s, the sound routine's entry through an approach, the music restart after a flight, the pause clicks and their caller. The port's side: `tools/vamiga-ref/sound7_port.c` with `sound7-*.port` (or `run_golden --audio-dump` with `sound7-restart.rg`), compared by `tools/vamiga-ref/sound7_check.py` (`make -C tools/m68k-rig test-sound-captures` runs it when the logs and `build/sound7_port` exist). A per-write log that stops with `runto` slows the machine to a crawl and misses breakpoints: the caller attribution uses runs without `audtrace`. |

## vAmiga driver notes

- Snapshot files must end in `.vasnap`.
- `restore` in the first seconds after power-on fails the hash check; `ref` waits until 3 s of uptime.
  Restoring repeatedly in one process works.
- A snapshot taken at the prompt preserves the frozen challenge; typed digits echo to the right of the dash.
- Script errors print `script line N: ...` and exit 2, including runtime failures (bad snapshot, unwritable path).

## Milestone 7, Task 2: the game-flow captures (`flow7-*.txt`)

Generated by `tools/vamiga-ref/scripts/gen-flow7.py`; run each alone under `flock /tmp/ref.lock` from a checkout that has `extracted/`, `disks/` and `build/vamiga-ref/`, with `--log extracted/captures/logs/flow7-NAME.txt`. They peek, step and press keys; the only pokes are named staging aids (the player's height, a record's position, the selector `V:4ABB0`, the ring count `V:4A8B1`, the collision switch `V:4A844`). A script must stay under about 8,000 commands (the driver crashes at start otherwise: `c5-probe` and `qual-restart` are cut to fit). The scripts that carry a countercode answer (`qual-answer-digits`, `orders-digits-m5` to `-m8`) are written by the generator to `extracted/scratch/` and are not kept in the repository. The results are in note 08 (5.4, 5.6, 5.8, 5.11, 5.13, 5.14, 6.1, 9.2, 12.14, Part C, 3.8) and note 06 (the start table). Logs: `inspect` (the first flight pass of every mission snapshot, the staging data), `keys7890`, `approach-air1`, `approach-carrier`, `training-2`, `training-7D`, `loc-mover`, `latch-loc`, `latch-air`, `qual-dispatch`, `qual-answer`, `qual-restart`, `brief-m5` to `-m8`, `orders-m5` to `-m8`, `crash-f1`, `pause-outcome`, `c2-flying`, `c3-landing` (three earlier staging attempts as `-v1` to `-v3`), `c4-sub`, `c5-probe`, `c5-same-pass`, `c5-early`, `c7-fail`, `c7-shadow`, `c8-shoot` (earlier attempts `-v1` to `-v3`), `c12-f1var`.
