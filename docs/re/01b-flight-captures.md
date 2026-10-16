# 01b: flight captures (measured state of the running game)

Measurements of the original game in vAmiga (Kickstart 1.2, `extracted/interceptor-autoboot.adf`), made with
`ref --script ... --log` (`peek`, `colors`, `seglist`, `freeze`; see `tools/vamiga-ref/README.md`) and the scripts
`tools/vamiga-ref/scripts/openers-*.txt`. Frames, logs and snapshots stay in the git-ignored `extracted/captures/`;
this note tabulates a few logged numbers per question and states what they settle. Every address is a flat-image
address (`00-binary-layout.md`) unless it starts with `RAM`. Position unit ("pos") means the unit of the 32-bit position
longs in the object records. Reproducibility: waits are wall-clock (see `01a`), so values differ slightly per run; the
scripts freeze the machine before each read so one sample is one instant.

## Address map: flat image to the running game

The game is loaded by AmigaDOS LoadSeg, so each of the 185 hunks is its own allocation in slow RAM (0xC00000 upward on this
machine): `[size long][BPTR to the next segment's link long][data]`. Method that worked (`seglist` command): scan RAM for
the first 16 bytes of the flat image with the relocated operand longs as wildcards (found at RAM C07B28), follow the BPTR
chain (`next*4+4` is the next data address) and compare every segment size with the hunk table (hunk size + 8): all 185
hunks match. Mapping for this boot path (identical in every snapshot made from `credits.vasnap`), hunk data addresses:

| hunk | flat | RAM | size |
| --- | --- | --- | --- |
| 0 | 0x010000 | 0xC07B28 | 41804 |
| 1 | 0x01A34C | 0xC11E80 | 876 |
| 2 | 0x01A6B8 | 0xC121F8 | 10756 |
| 65 | 0x04621C | 0xC3DD30 | 4440 |
| 71 (state, records) | 0x04A6E8 | 0xC42230 | 11604 |
| 72 | 0x04D43C | 0xC44F90 | 25564 |
| 73 (queues) | 0x053818 | 0xC4B378 | 5800 |
| 74 | 0x054EC0 | 0xC4CA28 | 260 |

Neighbouring large hunks are 12 bytes apart (8 header bytes plus 4 of rounding), but the layout is not monotonic: hunk 62 is in
**chip** RAM (0x010178) and many of the small hunks (3, 7, 78, 79, 82 ...) sit at 0xC014E8 to 0xC06610, below hunk 0. So the RAM address
is per hunk (`ram(h) + flat - flat(h)`), never one constant. `peek F:4B23C 128 label` does the translation. `seglist` now stops with
an error if any segment size is not hunk size + 8 (up to +16 is tolerated; all 185 are exactly +8).

## Units and state words (measured)

| quantity | where | measured relation | evidence |
| --- | --- | --- | --- |
| player record | 0x4B23C (record 0), class byte +0x62 = 0x11 | the carrier is record 14 (class 0x20), at x 0x11980000, y 0, z 0x10580000 | `openers-carrier-deck.txt` |
| altitude, feet | record +0x18 (long) | HUD altitude = 5 * (y >> 10): y 30472 reads **145** ft on the carrier deck (y / 204.8 = 148.8 ft; the HUD shows the integer-shifted figure; the deck is 145 ft on the HUD and 148.8 ft from y, and the two figures name the same state wherever either appears), 1800 reads 5 ft on the runway; 1 ft = 204.8 pos | deck and runway frames |
| altitude ceiling | record +0x18 | y stops at 8388608 (0x800000) = 40960 ft exactly | `openers-palette-high.txt` (ft 40960 in four samples) |
| camera offsets | 0x4AB2A (word, x), 0x4AB2E (word, z), 0x4AB30 (long, y) | = -(player pos >> 8) reduced to +-16384 (the player's x>>8 mod 0x4000 is 6188 and the word reads -6188), minus the cockpit eye offset (y 5 offsets = 1280 pos on the deck, z about 20 offsets); successive samples differ by 255.8 to 256 per 256 pos. Unit of an offset: **256 pos** (1.25 ft), world wrap 16384 offsets = 2^22 pos | `openers-range-near.txt` |
| camera height | 0x4AB1E (long) | = -(camera y in pos): -31752 on the deck (y 30472), -1137986 at y 1134032; 0x4AB30 = 0x4AB1E >> 8 (floor) | `openers-palette.txt` |
| speed | word 0x4A9B0 (the panel drawer's speed cache: note 06 shows it is written only by the drawer, at most on the frames the drawer runs, so it lags the state by up to 4 steps, 5 after an R burst; "changes every frame" below means the R burst, not the state) | knots = (word and 0x7FFF) / 12, **truncated** (note 06, verified by running the HUD routine: 4231 gives 352). The 353 and 356 read from the frames came from later words of the same climb, not from rounding. Bit 15 is a redraw flag: set for the game frame after each change (blanks 11-15, 32-36, 53-57 ... in the cadence log) and continuously while the word changes every frame (game frame counters 273 to 278); the thrust word 0x4A9B4 (100 = 100 %) and the fuel word 0x4A9AE (24999 on the ground, falling) carry it too | `openers-hud.txt`, `openers-cadence.txt` |
| velocity per step | y change per game frame | (the speed word is the drawer cache, so the per-step figure derived from it is good to within that lag of up to 4 steps) at pitch 30.5 degrees up the y change per game frame is 8,768 to 9,044 pos (28 steps) with the word at 4331 to 4370: **4 pos per game frame per unit of the speed word** (48 pos per frame per knot) | `openers-cadence.txt` |
| game frame | word 0x4A992 | the player record changes once per increment of this counter (that is the simulation step); in this scene one step lasted 5.4 vertical blanks (9.3 Hz) | same |
| altitude word (panel) | 0x4A9BA | equals 5 * (y >> 10) of the record at the moment it was last refreshed. Cadence log (`openers-cadence.txt`): refreshed at game frames 252, 256, 260, 264, 268, 272 (a 4-step period, each value exactly 5*(y>>10) of that frame), then on frames 273, 274 and 275 (a burst, cause not found; the page-redraw counter 0x4A8EF is the candidate), then not again by frame 280. So it lags y by 0 to 4 steps in the steady cadence and **at most 5 steps (220 ft at 360 kt in a 30 degree climb) observed**; it is never above 5*(y>>10) while climbing. The altitude figure in the HUD itself (top right of the view) is redrawn every game frame (`openers-hud-display.txt`: it changes about 1 to 2 vertical blanks after each counter increment, 18 changes in 19 increments) and the HUD speed digits change only when the speed word does | same, `openers-hud-display.txt` |
| G readout | not found | no word in 0x4A700..0x4B020 or in record 0 tracks the HUD G figure at four conditions (1.0, 0.8, 0.3, 1.7) | `openers-hud.txt` |
| horizon row, pitch 0 | frame | last sky row 89, first ground row 90 (bitmap rows; frame row 132 for the first ground row) at every height from the deck to 40,960 ft (73 staged states, Question 6); the focal length is 90 x 0xFC / 0x80 = 177 rows per unit of tan(pitch) from the zoom triple (about 175 measured from four exact pitch states, Question 4) | `carrier-deck` frame, `openers-attitude.txt`, `sky-ground-*.txt` |

## Question 1: view-distance scale, the range unit, the carrier

Scripts `openers-range-near.txt` (0.6M to 5.5M pos from the carrier), `openers-range-far.txt` (6.2M to 10.0M),
`openers-range-altitude.txt`. The player flies away from the carrier deck along +z at full thrust and a shallow climb,
looking back (KP2, re-sent before each sample because the rear view otherwise reverts after a few seconds).

| sample | carrier distance (pos, along z) | player altitude (pos) | 4AB30 | index (-4AB30)>>7 | 4AA6A | carrier in frame |
| --- | --- | --- | --- | --- | --- | --- |
| near 0 | 595,140 | 25,044 | -104 | 0 | 16384 (64*256) | yes, island and deck |
| near 14 | 2,595,440 | 86,332 | -343 | 2 | 18432 (72*256) | yes, small |
| near 29 | 5,526,008 | 177,412 | -699 | 5 | 21504 (84*256) | yes, a few pixels |
| far 0 | 6,163,468 | 193,296 | -761 | 5 | 21504 | yes |
| far 23 | 10,018,768 | 313,480 | -1231 | 9 | 24576 (96*256) | yes, still a speck |

Derived:
- **Index and table (settled):** index = (-4AB30) >> 7 (4AB30 is the camera height in units of 256 pos, 1.25 ft, so one step is 160 ft);
  4AA6A = table byte * 256 with the table of note 04 (observed bytes: a concave ramp from the low sixties to 100 over the indices 0 to 12, steps of four narrowing to one, then a slow rise to a plateau of 127 from index 41 on; the 64 bytes are `tables/range.bin`, FNV-1a-32 `ce48dc02`, exported by the extractor).
- **Read past the table (specification for the port):** the index is the low word of -4AB30 shifted right by 7 and runs 0..255 (logged up to 251
  at 40,208 ft). Indices 64..255 read the bytes at 0x4621C + 64 .. 0x4621C + 255 in hunk 65 (the data after the 64-byte table); 4AA6A is that
  byte * 256 (observed 0, 256, 768, 1536, 2560, 2816, 5120 at indices 65 to 251; 28 of 33 samples equal the image byte exactly and the other
  5 fit a one-frame lag between the two reads). When -4AB30 exceeds 0x7FFF (4AB30 below -32767) the word is clamped to 0x7FFE: at the 40,960 ft
  ceiling 4AB30 = -32788 and 4AA6A = 32766 (`openers-palette-high.txt`, samples h17 to h20).
- **The range word has no effect (settled by capture and by the code's data flow):** 4AA6A has three references: the two writes (0x1FA64 clamp, 0x1FA7E table value) and one word compare at 0x1FB76 in the display-list
  walker, after which the condition codes are overwritten by the long moves from 0x1FB80 onward that load the draw routine arguments before the
  call (no conditional branch follows the compare), so the compare cannot skip a record. Capture evidence: with 4AA6A at 0, 256, 512 and 768
  (index 64 to 79, 10,245 (first index-64 sample) to 12,765 ft, `openers-range-level.txt`) the frames (`lv-*.ppm`) show only the sky and the land and sea ground polygon (no object was in view), unchanged from below 10,240 ft, which shows
  that the ground polygon is not range limited; the carrier evidence is separate: it was captured at indices 0 to 9 (4AA6A 16384 to 24576) and is drawn out to 10.0M pos,
  beyond 4AA6A * 256 there, and beyond the largest table value. The record distance word that the walker stores at
  0x4ABF8 is used by the model draw routines as a size scale (multiplied by the zoom 0x4AAFA), not as a cull limit. **Consequence for the port: do not cull
  by 4AA6A; the visible range is whatever the list builder puts in the queues.** The "range unit in position units" therefore does not exist
  for this word; it is a dead variable, and note 04's statement that records beyond it are skipped is wrong. Confidence: high for "no effect"
  (code and capture agree), but no landmark-pop-in capture was made, so where the list builder cuts terrain items is still unmeasured.
- **Band rule (consistent):** the carrier draws without a gap over the whole 0.6M to 10.0M range (a speck at the far end), as the nearest-level
  precision bands in `world.c` require. 0x4AB70 sampled at the instant of each freeze is 0 to 3 (it holds the level of the last drawn object,
  not of the carrier). Confidence: medium (frame evidence, no per-model band readout).
- Instance scale for cell items: not measured. The first-run logs of this question are in `extracted/captures/logs/range-*.txt` (git-ignored).

## Question 2: handedness and signs

`openers-handedness.txt`: airborne snapshot (pitch about 30 degrees nose up, heading 28), each key held 1.0 s, state frozen 0.5 s later.

| step | +0x66 | +0x68 | +0x6A | frame |
| --- | --- | --- | --- | --- |
| start | 26360 | 2256 | 0 | sky only, 1.0 G |
| after UP 1.0 s | 27472 | 2256 | 0 | nose lower |
| after RIGHT 1.0 s | 27528 | 2504 | 26728 | ground rises toward the right edge, panel horizon tilted |
| after LEFT 1.0 s | 27512 | 3424 | 27936 | bank reduced |
| after LEFT 1.0 s | 27512 | 3920 | 144 | level, slight left |
| after DOWN 1.0 s | 26664 | 3864 | 144 | nose higher |

Derived (units 28800 per circle, 80 per degree):
- +0x66 is **pitch**, nose up = angle below the circle (26360 = 28800 - 2440 = 30.5 degrees up); UP lowers the nose, DOWN raises it.
- +0x68 is **heading**, clockwise from north, equal to the HDG readout (2256 = 28.2 degrees; 028 in the frame), growing in a right turn.
- +0x6A is **roll**; **roll right is negative** (RIGHT gives 26728 = -25.9 degrees: the right wing down, the horizon rising at the right in the
  cockpit frame); LEFT gives positive values (144 = +1.8 degrees after the second LEFT).
- World axes (positions): heading 28 moves x by -90k and z by +168k per 1.5 s; heading 0 moves z up. So **+z is north, -x is east, +y is up**: with
  x pointing west this is a right-handed system (x west, y up, z north). Angles: positive heading is clockwise seen from above.
- Pitch/roll rates ramp with the hold time (UP 1.0 s: +1112 units; UP 2.2 s: +4144), so key holds do not give fixed angles.
Confidence: high for signs, handedness and the roles of the three words.

## Question 3: palette and altitude bands

`openers-palette.txt` (0 to 17,433 ft in 2.5 s steps), `openers-palette-high.txt` (to the 40,960 ft ceiling), `openers-demo-palette.txt`
(attract mode). Denise colour registers (`colors`), the bytes at 0x54F18 and the pixels of the sky in frames, logged each step.

| condition | COLOR00..07 | COLOR08..15 | sky pixel | note |
| --- | --- | --- | --- | --- |
| carrier deck, 145 ft (HUD; y / 204.8 = 148.8 ft) | the first eight entries of `palette/flight.pal` (hash in note 04) | the next eight entries of it | 2b2b59 | COLOR16..31 are 000 |
| 5,537 ft, 10,885 ft, 17,433 ft | identical | identical | 2b2b59 | |
| 24,138 ft to 40,960 ft (20 samples) | identical | identical | 2b2b59 | |
| attract-mode demo, 145 (HUD) to 923 ft | identical | identical | | over 30 s |

Derived: the world palette does **not change with altitude** from 0 to the 40,960 ft ceiling in free flight (no sky-colour or band change in the
registers or in the frames); sky = entry 5 (0x447), sea = entry 6 (0x036), the land base is entry 4 (0x151, shown as (0,56,0); entry 11, 0x030, is not a ground pen: corrected in Question 6), horizon/deck greys per `01a`;
the earlier "darker sky" in the demo capture is not a different palette (the demo registers are identical). The 16 words at 0x54F18 are not a
palette (they read 0DF F0C0 0C4 CA40 ... and did not change either); they are not the colour table. The flag word 0x4A98A reads 0 in flight and 2 on the ground (bit 13, the "sky-skip" of note 04, is never written by any code: Question 6); 0x4A953 is 1 on the deck at start and 0 once airborne.
The colour registers were read at one instant (one beam position) per sample, so a fade or a copper change that happens
elsewhere in the frame would be missed; 150 samples (altitude runs plus the 300 s run below) all agree. The only palette change in flight is the fade table (Question 6: the blackout under a sustained high load; not reached by the altitude runs).

**0x4A90C is not an altitude band** (correcting the first version of this note and note 04 Q6b). It reads 0 on the ground and in climbs, 0x0101 in
flight after some manoeuvres, and equal-byte values 0x0000 to 0x0707 that change with the view key (cockpit 0, KP4 0x0707, KP2 0x0404, external
KP6 0x0606, KP1 0x0101, KP3 0x0707, KP7 0x0303, KP9 0x0505, KP4 0x0202) and in the attract demo (0x0303 to 0x0707), while the palette registers stay identical.
`openers-clock.txt` (carrier deck, idle, 300 samples about one second apart, time-stamped with the frame counter and the timer seconds; it does not freeze the machine, single-word reads only) shows 0x4A90C
constant at 0 for 300 s, so it is **not time based** either (thresholds 0xE10 = 3600 and 0x6270 = 25200 are not seconds here). **What it is** (note 04a, which traced the
writer): the **view-heading octant**, written by h20+0x738 from the view heading divided by 3600 into both bytes (values 0x0101 to 0x0707, 0 for none), and the thresholds 0xE10..0x6270 of
note 04 are the octant limits of that heading, not altitudes. The captures agree with this reading: the value is constant while the view does not change (300 s idle on the deck, and 0 throughout the 1500-blank
steady flight of the stall run below, cockpit view, no view key) and changes with the view keys, which turn the view heading. It is **not** the cell phase: the phase is `V:4A908`, written by the cell code at 0x1F706 (the player's x and z low two bits).
The routine at h8+0x376 (0x1F54E) is the frame hook; from its data flow plus these captures: it compares byte 0x4A90C with its copy 0x4A90D and, when they differ, copies it
and sets refresh bits (value 0x0B) in a mask unless 0x4A83E is non-zero, so **an octant change is one of the triggers** of the terrain cell list rebuild (not a change detector of the list as such);
it sets the same mask when the negated camera height 0x4AB1E crosses 0xA000 (40,960 pos = 200 ft) in either direction, and when the altitude band
(high word of -4AB1E shifted right by 5, that is units of 2^21 pos = 10,240 ft) changes, unless the camera mode 0x4A966 is 2; then it
continues, as the frame hook, into the world update 0x26AF8 (note 05: the whole entity step, hunk 17) and the cell code at 0x1F706 (stores 0x4A908).
The refresh mask is consumed by code after that (note 04a: the builder at 0x1F770 acts on the request byte `V:4A910`); the mask-to-request link was not traced here, so "refreshes the cell list" is an inference from the trigger conditions. It writes no palette entry.
The 10,240 ft band (2^21 pos) is the same boundary at which the range table index passes 63. Confidence: high that no palette
change exists in free flight, high for the octant reading (note 04a code plus these captures), medium for the refresh-mask description.

## Question 4: ground polygon

Logged (palette scripts): camera height word 0x4AB1E (-(camera y), see the table above), camera matrix 0x4AC90 and the object matrix 0x4AC7E in
`openers-hud.txt`. The horizon is at bitmap row 90 at pitch 0 (deck), and the sea/land fill sits below it, which agrees with note 04's
polygon built from corners (+-10240..11008) and projected through the matrix. The ground y coordinate formula (high word of 4AB1E shifted right by two: -1 for any height above 0 up to 262,144 units, -33 at the ceiling) is verified in Question 6: the corners are 10,000 away and the height is a few units, so the height hardly moves the horizon. Sea and land are drawn by the polygon (frames show land and sea polygons joined at a
coastline). The "land edge" measured in `openers-attitude.txt`
(about 3,500 ft, exact pitch states, frames waited 0.8 s after the key) is at bitmap row 133, 113, 77 and 50 in column 250 for pitch
-13.1, -7.2, +4.9 and +13.3 degrees (nose down positive): a straight line in tan(pitch) with 170 to 179 rows per unit, so a **focal length of
about 175 rows** (177 from the zoom triple, Question 6). The "edge dip of about 0.65 degrees at 3,700 ft" read here was the far edge of the sea mesh (stage [68], note 04), not the horizon: the horizon polygon's dip is below one row at every height (Question 6), zero within a row on the carrier deck at 145 ft on the HUD (148.8 ft from y, see the units table; row 90 exactly). The corners are located and the polygon is verified pixel for pixel in Question 6 (the buffer `0x5043C`: during the crossing search it holds 16-byte slots, one per corner or edge, with the camera-space point and its screen point; when the search is done the same buffer is rewritten as the polygon the filler reads, a count word followed by (x, y) word pairs: note 04). Confidence: high.

## Question 5: HUD reference frames and state words

`openers-hud.txt` (frames `hud-*.ppm`, state at the same frozen instant); the climb and the bank are what the key holds gave. Frames re-read
at 3x: HUD figures (top left and top right in the view) and the panel figures (right instrument block) update at different moments, and both
trail the frozen state because the displayed page is one or two game frames old; 5*(y>>10) is the formula for the HUD altitude.

| capture | position x, y, z | attitude +0x66, +0x68, +0x6A | speed word / 12 | HUD (KT, FT, G) | panel (FT, KTS) |
| --- | --- | --- | --- | --- | --- |
| runway, idle | 273963296, 1800, 279184624 | 0, 2240, 0 | 0 | 0, 5, 1.0 | 5, 0 |
| carrier deck, idle | 295185408, 30472, 274309120 | 0, 0, 0 | 0 | 0, 145, 1.0 | 145, 0 |
| near level (pitch 6 degrees up) | 273063764, 706604, 280869756 | 28296, 2256, 0 | 390 | 390, 3,435, 0.8 | 3,410, 390 |
| climb, about 21 degrees | 273138200, 681120, 280730840 | 27096, 2256, 0 | 361 | 364, 3,255, 0.5 | 3,225, 357 |
| bank, about 40 degrees right | 273014292, 711068, 280952280 | 28296, 2848, 25616 | 395 | 395, 3,450, 1.7 | 3,425, 395 |

5*(y>>10) is 5 and 145 at the two idle states (exact), and 3,450, 3,325 and 3,470 in the three flights, so the HUD altitude is at or below it
(lag, see the cadence section) and the altitude word 0x4A9BA read 3,410, 3,225 and 3,425, equal to the panel figure in all three flights. The G figure is not available as a word. Frames with exactly known attitude are in the next section; the timed-hold climb and bank frames
above are references for layout only.

Camera matrix 0x4AC90 (Q8) at the bank: row 0 113 106 63, row 1 -122 192 -109, row 2 -72 14 105 (level: 147 0 78, 12 250 -25, -60 14 112).
Not captured: the landing approach with the ILS. The ILS is not in the manual, no indication appeared in a takeoff-and-return attempt
(the aircraft stayed on the runway at 8 ft and 205 kt after DOWN 0.5 s), and no key for it is known; open for the HUD task.

## View-kind words (V:4A83D etc.)

`openers-views.txt`, state frozen after each key (cockpit view, then the keypad):

| key | 0x4A83D | 0x4A85F | 0x4A96A | 0x4AA3C | 0x4AA3E |
| --- | --- | --- | --- | --- | --- |
| none | 0 | 0 | 0 | 144 | 0 |
| KP4 | 0 | 11 | 0 | 144 | 7 |
| KP6 (after KP4) | 0 | 0 | 0 | 144 | 0 |
| KP2 | 0 | 6 | 0 | 179 | 50 |
| KP1, KP7 | 0 | 13, 12 | 0 | 167 | 50 |
| KPENTER (external) | 255 | 0 | 4 | 167 | 50 |
| external KP8, KP9, KP6, KP3, KP2, KP1, KP4, KP7, KP5, KPDOT | 255 | 0 | 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 | 167 | 50 |

The selector is -1 (255) in the external view; the external direction word follows the keypad as listed (KP2 = 4 behind, KP8 = 0 head-on).
The words 0x4A966 and 0x4A865 stayed 0 in all of these.

## Cadence, step size and exact attitude references

`openers-cadence.txt` (the new `step` command runs the frozen machine one vertical blank at a time): in a steady 30 degree climb at 361 to 371 kt
the game frame counter 0x4A992 advanced every 5.4 vertical blanks on average (4 to 9 blanks; 150 blanks, frames 252 to 280); the player record changed
only when the counter incremented; 0x4A9BA and 0x4A9B0 changed on a slower schedule (see the table above). One simulation step moved y by 8,768 to 9,044 pos (28 steps).

`openers-attitude.txt` uses the new `holduntil KEY ADDR lt|gt VALUE` (hold the key until the folded angle passes the target, release, wait 0.8 s,
freeze, log, frame). The release lags the target by one to four game frames (ref.cpp and the README say the same), so the states reached are not the targets; the logged values are exact (pitch
+0x66 / roll +0x6A, folded, 80 units per degree): pitch -1048, -576, +392, +1064 (13.1 and 7.2 degrees nose up, 4.9 and 13.3 down); roll -1176, -1960,
-2720, -4128 (14.7, 24.5, 34 and 51.6 degrees right bank) and +3576 (44.7 left). Frames `att-*.ppm`. These replace the timed-hold frames as
the reference for the HUD and horizon. Roll keeps a pitch of +1088 to 1120 and the heading turns right under right roll (2256, 2360, 3144, 4056,
5144) and left under left roll after the reversal.

## Simulation step rate (reference machine)

**The original's rule** (note 04, "Timing"): the game runs **one simulation step per displayed frame**; how often that happens is set by the cost of rendering the frame. The frame limiter is the routine h20+0x562 (0x297D6), not a word: it reads timer.device time and busy-waits until the average milliseconds per frame over a window of 1 to 8 frames reaches 67 ms, a ceiling of 15 frames per second (a step at most every 67 ms on average). The display flip waits for the vertical blank separately, so each frame is shown for a whole number of blanks; the vertical-blank counter 0x4A82C has no reader and is only our clock. There is no fixed simulation clock. What the reference machine (plain A500, OCS, 7.09 MHz 68000, PAL, 512 KB slow RAM, no fast RAM) actually reached is below.

`openers-steprate.txt`: the step counter 0x4A992 against the vertical-blank counter 0x4A82C (one increment per 50 Hz blank, so emulated time, independent
of how fast the emulator runs), one sample about every 0.93 s of wall-clock wait (the samples are 46.4 blanks apart on average, 38 to 50, the `wait 0.95` plus the read; the rates are normalised to steps per second of emulated time) for 70 samples (about 64 s) per scene, machine running.

| scene | blanks | steps | mean steps/s | blanks per step | per-sample min / median / max |
| --- | --- | --- | --- | --- | --- |
| cockpit, level-ish flight over open sea (from the carrier) | 3183 | 590 | **9.27** | 5.39 | 7.1 / 9.4 / 11.2 |
| cockpit, flight over the bay area and city (from the airport), samples b000 to b023 | 1071 | 155 | **7.24** | 6.9 | 6.1 / 7.3 / 8.7 |
| external view (climbing, from the airport) | 3193 | 424 | **6.64** | 7.53 | 5.2 / 6.5 / 7.7 |
| carrier deck, idle, cockpit | 3207 | 529 | **8.25** | 6.06 | 6.6 / 8.3 / 9.4 |
| the airport scene after the crash below (aircraft parked on the runway, samples b027 to b069) | 1947 | 218 | 5.60 | 8.9 | 4.3 / 5.4 / 6.7 |

The city run (`b`) is not one steady scene. The script holds UP until the pitch word passes a mark, which leaves the aircraft in a descent at about 480 kt (speed word 0x1663); at about 22 s of emulated time (the run's blank 1092) it
hit the surface short of the airfield (account from `stall.txt` and `stall2.txt`: the player y reaches 1800, the gear-down floor of note 05 section 7, which is the same over sea and land; the cell words are 0x40/0x43, a neighbour of the airfield cell 0x41/0x42, and the terrain bits of the record say water, see below). Blank 1092 of the stall run is counted from its first step, which follows the restore, the UP hold and a 1 s wait, so it is about 22 s after the run's first sample (the b000 counter reads 0x1C8B, the stall run's first 0x1C8D; the impact is at 0x20CE, between b023 at 0x20BA and b024 at 0x20EB). That is the interval that read **zero steps**: b024 to b025 (47 blanks, 0 steps); b024 to b026, the two intervals together, are 96 blanks with 1 step. The earlier "about 25 s after the restore" included the hold and wait and was not measured; the sample timing is only known relative to b000. It is **not** a stalled second right after the restore.
After it the aircraft is back on the runway (the scene of the "airport" snapshots, speed 0, 5 ft) and the rate is about 5.6 steps/s instead of about 7.2 (the drop is not a degradation over time but the change of scene).
The five rows above therefore separate the flight (b000 to b023) from the parked state (b027 to b069); samples b024 to b026 are the crash. The flight-only rows (first four) average 7.97 steps/s over 230 samples (standard deviation 1.30, 10th to 90th percentile 6.3 to 9.8; 1698 steps in 10,654 blanks).
The earlier 150-blank climb capture gave 9.3 (5.4 blanks per step). So on the reference machine the simulation ran between about 5 and 11 steps per second depending on the scene, 6.6 to 9.3 on average per scene, never near the 15 Hz ceiling of the frame limiter, and slower over land with many objects than over sea.
Confidence: medium (one flight path per scene; the rate is a property of the emulated machine and the scene).

**The zero-step interval, investigated** (`openers-steprate-stall.txt` and `openers-steprate-stall2.txt`: the same start as run `b`, then one `step` (one blank) per command with peeks of the blank counter, the step counter, the display-list request byte 0x4A910, the player cell words (record 0 +0x06 and +0x08), the view octant 0x4A90C, the player record, y, the panel speed cache 0x4A9B0 and the page counter 0x4A8EF; CIA-B port B and DMACONR in the first run; logs `stall.txt`, `stall2.txt`; frames from `openers-steprate-crash.txt`). Both runs reproduce it, at blank 1117 to 1212 in the first (95 blanks between the last step before the freeze and the first step after it) and 1108 to 1201 in the second (93 blanks), so it is a property of the scene, not of the emulator or of wall-clock waits. Findings (verified from the logs):
- The player's y falls from 62,476 (blank 1000) to 1800 at blank 1092 in steps of 4,376 per game frame, and 1800 is the same y as the runway of the airfield (5 ft, as the HUD reads), here one cell short of the airfield (the terrain bits of the record say sea: byte +4 of the player record, B4 in note 05, reads 0x04, the stale pad bit from the take-off, until the impact step and 0x02, over water, from the sample after it, which is the terrain query of note 05 section 10 answering at the touchdown; y = 1800 is the gear-down floor over sea and land alike, note 05 section 7); the frames show the HUD crash warning (frame at blank 1115) and, at blank 1235, the aircraft standing on the runway of the airfield with the tower to the left. So the interval is the **crash sequence**. Second run, in order (blank numbers of that run, one sample per blank): the step counter turns 0x19E at blank 1092 and the same sample reads y = 1800; at blank 1093, still step 0x19E, word 0 of the player record goes 0x11C8 to 0x93C8 (the bits 0x8000 and 0x0200 set) and the words after it change too; at blank 1098, still step 0x19E, the panel speed cache (0x4A9B0, not the speed word) goes to 0x8000; at blank 1100 the counter turns 0x19F and word 0 goes 0x93C8 to 0x91C8 (bit 0x0200 cleared again; note 05 reads that as the hand-over to the mission flow, which clears it); the counter turns 0x1A0 at blank 1108 and then does not move until 0x1A1 at blank 1201. So the counter advances **twice after the impact step** (at blanks 1100 and 1108), the step that brings y to 1800 and sets the hit flag being 0x19E; the freeze follows 0x1A0, the second step after the impact step. (Corrected, note 05 17.17: the two advances are the ends of the impact pass and of the hand-over pass; the value 0x1A0 names the restart pass that runs after the hold, and there is no extra step. Counter convention of note 05 section 14: a pass is named by the value while it runs.) Between blanks 1108 and 1188 the player record is unchanged, then the mission restart. Note 05 section 14 carries the same step numbers and the pass-length run (`crash-hold.txt`, step 418 for the hand-over step).
- **Step counters of the two runs (from `stall.txt` and `stall2.txt`, verified).** The first run's step counter reads 0x101 at its first sample and the second run's 0xFF, so the first run is 2 counts ahead from the start; the last step before the freeze is 0x1A3 at blank 1117 in the first run and 0x1A0 at blank 1108 in the second, 3 counts ahead (the first run's other steps near the impact: 0x19F at 1087, 0x1A0 at 1093, 0x1A1 at 1100, 0x1A2 at 1109). The first run logged no y and no record word, so its impact step is not identified. The restart shows as the player record being rewritten at blank 1188 (second run; 1198 in the first): cell words change from 0x40/0x43 to 0x41/0x42 (the airfield cell), the request byte 0x4A910 reads 0xFF and then 0xFC for one blank each (a full display-list rebuild, note 04a; the restart variant of the staged clearing, not 0xFF then 0x00), and the step counter resumes 13 blanks later in the second run (0x1A1 at blank 1201) and 14 blanks later in the first (rewrite at 1198, 0x1A4 at 1212, so the 95 blanks of the first run are 81 to the rewrite and 14 after it, the 93 of the second 80 and 13) (Corrected, note 05 17.17: the counter turn 13 or 14 blanks after the rewrite is the end of restart pass A, a pass that moves nothing, not the first step after the stall; the first simulated step comes seven passes later), when the request byte shows 0xFF and 0xFC again (1201 and 1202 in the second run, 1212 and 1213 in the first; the same pair at the rewrite itself: 1188 and 1189, and 1198 and 1199). So a **cell change and a list rebuild coincide with the end of the stall, not with its start**; the start is the impact. The earlier cell changes of the same run (blanks 43, 304, 453, 810, 973 in the first run; 61, 315, 460, 819, 984 in the second) each showed the staged clearing of the request byte, 0xFF, 0xFE, 0xF0, then 0, over 2 to 3 blanks (the first value, 0xFF, can fall between samples: in the first run the changes at blanks 43 and 453 show only 0xFE and 0xF0, in the second run the one at blank 819), and left no visible gap in the step counter.
- Not a disk load: CIA-B port B (drive select and motor lines) read 0xFF (nothing selected, motor off) at every one of the 1500 blanks; the DMACONR audio bits do change from blank 1010 on (sound effects begin), which is consistent with the crash sound. The view octant 0x4A90C stayed 0 throughout, so it did not play a role (the view did not change).
- Cause: note 05 (round 2, open question 13) traced it read-only: the crash sets `V:4A951` to 40 and the display flip loops while that counter is positive, so the frame routine, which increments the step counter, does not run for 40 passes; note 05 (round 3, section 14) measured the pass at exactly 2 blanks (80 blanks, 1.6 s), the remaining 13 to 15 blanks being the restart. Not established here: what the mission restart reads; note 05 owns the collision result path. The delay length was measured twice (93 and 95 blanks); the first-run list of audio bits is observation only.

## Addendum (note 05, Task 5 gaps): three captures

All three start from `airborne.vasnap` with the `seglist` of the other scripts; logs are in `extracted/captures/logs/` (git-ignored).

- `openers-image-static.txt` (log `image-static.txt`): the image of hunks 0 to 70 (`0x10000..0x4A6E8`) read back from the running game in 4 KB peeks and compared with `extracted/interceptor.bin`. The bytes differ only at relocated pointers (the longs the loader rewrote to RAM addresses), at a text buffer (`0x440C9..0x440CC`, `0x44EA1`) and in the zero-fill tail of hunk 70 from `0x49A12`, which the game uses at run time. So the sine table, the inverse tables and the data around them (`0x4130A..0x49A12`, the exact span the builders read, 05 section 17.9) are static: the table over-reads of note 05 section 17.9 read image bytes.
- `openers-stall-records.txt` (log `stall-records.txt`): the stall run of `openers-steprate-stall2.txt` with `W0`/`W2` of records 1 to 14 and the player byte `+0x7B` peeked per blank from blank 1040. At the impact (`W0` of the player `0x11C8` to `0x93C8` at y = 1800, step `0x19B` in this run; the impact blank is 1097, the first sample reading `0x93C8`, the last `0x11C8` sample being 1096, and `0x91C8` from blank 1105, the hand-over, 8 blanks later) records 1 to 13 read `W0` = `0x0010` (not valid), record 14 (the carrier, six cells away) `0x405C`: no other aircraft, so the extra `0x8000` is not result `0x40`. `+0x7B` reads `0xFF` until blank 1094 and `0x00` from there to the restart (blank 1205 back to `0xFF`).
- `openers-stall-part.txt` (log `stall-part.txt`): the same run with the draw-side variables of the entity prelude (`0x21C44`) per blank from 1060: `V:4A923` = 1, `V:4A85F` = 0, `V:4A83D` = 0, `V:4AB30` rising from -176 (y = 44,752) to -23 (y = 5,368) by about 17 per step; `+0x7B` turns `0x00` in the sample where `V:4AB30` first reads -23 (the sample before reads -40), and the impact follows in the next step, 3 blanks later (`W0` `0x93C8`).

## Addendum 2 (note 05, 17.17): the crash flow, the hold and the restart

The captures of the Task 6 gap round (note 05, 17.17 and 17.18 state the rules). New reference-emulator commands (documented in `tools/vamiga-ref/README.md`): `stepuntil ADDR LEN and|eq|ne|lt|gt VALUE [max]` runs the frozen machine frame by frame until a value matches (frame-exact alignment with an event whatever the wall-clock jitter before it) and `poke ADDR LEN VALUE` writes slow or chip RAM of the frozen machine (used only to set the crash allowance). A script of more than about 8,000 commands makes the driver fail at start; the scripts below stay under it, and a long run needs `--log FILE` to keep the lines when the driver dies at exit.

Start state: `crash-setup-dive.txt` restores the airborne snapshot, holds UP until the pitch passes the mark, freezes, steps until y is below 30,000 and writes `extracted/captures/dive-precrash.vasnap` (y = 27,504 in the capture, 20 frames above the sea). Every flow script restores it, freezes, and (where stated) presses a key, then `stepuntil` on `W0` bit `0x0200` of the player; k below counts blanks from the first sample that shows it. One peek set per blank (`V:4A82C`, the state pointer `0x1A6BC`, `n`, `V:4A951`, `V:4A83D`, `V:4A84D`, `V:4A910`, `V:4A866`, `V:4A94F`, `V:4A952`, `V:4A984`, `V:4A982`, the record's first 32 bytes, `+0x7B`, the fuel, `V:4AB8E`, `+0x4C`, `V:4A959`, `V:4A8EC`, `V:4A994`, `V:4A968`); the state pointer holds a RAM address: `0xC0AA26` is the flight state `0x12EFE`, `0xC0B400` the crash wait `0x138D8`, `0xC0B4A8` `0x13980`, `0xC0B4EA` `0x139C2`, `0xC0B518` `0x139F0`, `0xC0B55E` `0x13A36`, `0xC09598` `0x11A70`, `0xC0B69E` `0x13B76` (the hunk 0 base `0xC07B28` is the flat `0x10000`).

**Cockpit crash** (`crash-flow-cockpit.txt`; two complete runs; the numbers of the earlier run in brackets where they differ): the pass began before the first sample (the logs hold nothing before k = 0, so no earlier turn is observed; `n` reads 0x19B at k = 0); `W0` `0x93C8` at k = 0; the counter turns at k = 9 (8) and `W0` reads `0x91C8`, `V:4A951` = 40, `V:4A952` = 1, `V:4A984` `0x0C41` to `0x0C01`, `V:4A982` `0x0100` to 0 in the same sample, the state pointer shows `0x138D8` one blank later; the counter turns again at k = 18 (16); `V:4A951` then falls by one every 2 blanks from k = 20 (this run has one 3-blank iteration, k = 44 to 47, so 79 blanks from the first fall to 0; in the other six runs of the fix round the figure is 78 or 79, and from the last pre-hold turn to 0 it is 80 or 81) and reads 0 at k = 99 (96); in that sample the state pointer reads `0x13980`, `V:4A910` = `0xFF` (then `0xFC`, then 0), the record is rewritten (cell words `0x40/0x43` to `0x41/0x42`, `W2` `0x2080` to `0x0080`, `W0` `0x11C8` by `0x1D538` with `+0x0A`/`+0x0B` still 05/06, then `0x91C8` and 06/0F after pass A's own world update (a collision result `0x10`, note 05 17.17 rule 2; the cockpit run samples after both, the keys and external-to-cockpit runs show the first state), with bit `0x0010` set for the one blank of the list request `0xFC`, the fuel `0x006151F0` to `0x0061A800`), `V:4A94F` 5 to 4, `V:4A984` `0x0C01` to `0x0C00`, `V:4A952` 0; the counter turns at k = 113 (110) and the pointer reads `0x139C2`, `V:4AB8E` = 5, `V:4A910` `0xFF` again, `W2` `0x0082`, `W0` `0x11C8` (the second `0x1D538`) and `+0x0B` 06, then the counter turns at k = 126, 137, 147, 159, 167, 176, 186 (123, 134, 144, 155, 163, 172, 182) with `V:4AB8E` = 4, 3, 2, 1, 0, -1, -2 (each within a blank of the turn); at 176 (172) the pointer reads `0xC0AA26`, `V:4A984` becomes `0x0840` (bit 6) and in the next blank `0x0C40` (bit `0x0400`, set by the HUD code at `0x29AA0`); bit `0x0400` reads set from the start (`0x0C00`) and is cleared one blank after C1 begins (`0x0C00` to `0x0800` at k = 127; the writer is not traced), the external run never has it, and the external run's bit 0 falls at k = 55 while the restart is still waiting (`0x0801` to `0x0800`). `V:4A982` stays 0 through all of it and `V:4A984` bit `0x0800` is set throughout. `+0x4C` of the player (peeked in `crash-keys-view-in-hold.txt`) falls once between k = 0 and the hold (at the hand-over pass) and once in the restart pass. Frames (`crash-cockpit-K.ppm`, md5 groups): the frame of the impact pass (k = 0, 9) is the flight picture over the sea; from the start of the hold (k = 18) the same scene with a dark triangle on the horizon and the crash message, unchanged to the end of the hold (k = 19, 60, 95 to 98 identical) except one blank, k = 99 (96 in the other run), which is a uniform orange screen (the blank in which `V:4A951` reads 0; fix round, `crash-hold-palette.txt` and `crash-hold-flash.txt`: the 32 colour registers read the orange block on every second blank of the hold, once per iteration, and the game palette on the others, yet the frames of 8 consecutive mid-hold blanks and of the 6 blanks before the end are byte-identical normal frames, and only the frame of the blank in which `V:4A951` first reads 0 is orange); the old frame is back at k = 100 and stays through the whole restart pass (k = 104 to 114) up to the counter turn at 113; the runway scene first appears at k = 115 and is identical in every later dump (115 to 125, 171, 172, 181, 183).

| pass | begins at k | state routine | length (blanks) |
| --- | --- | --- | --- |
| hand-over pass N+1 | 9 | `0x12EFE`, crash branch | 9 |
| hold | 18 | - | 81 |
| restart A | 99 | `0x138D8` | 14 |
| B | 113 | `0x13980` | 13 |
| C1 | 126 | `0x139C2` (4) | 11 |
| C2 | 137 | `0x139C2` (3) | 10 |
| C3 | 147 | `0x139C2` (2) | 12 |
| C4 | 159 | `0x139C2` (1) | 8 |
| C5 | 167 | `0x139C2` (0) | 9 |
| C6 | 176 | `0x139C2` (-1): `V:4A984` bit 6 | 10 |

**External crash** (`crash-flow-external.txt`, KP-ENTER pressed in the dive): `W0` `0x87C8` and `+0x4C` = 15 at k = 0; the counter turns at k = 11 with `W0` `0x85C8` and the state pointer `0x138D8` (`V:4A951` stays 0); then at 21, 32, 37, 42, 46, 51, 55, 59, 63, 67, 71, 76, 80, 84 (the dying passes, 10, 11, then 4 to 5 blanks long, `+0x4C` 14 down to 1); `W0` `0x8188` and `+0x4C` = 20 at k = 85 (the expiry step, in the pass that began at 84); the counter turns at 88 with the pointer `0x13980` already set, `V:4A910` = `0xFF`, `W0` `0x91C8` (after A's world update; `0x11C8` from `0x1D538`) and the fuel refilled (pass A = 88 to 104), then 104 (A ends), 119 (B ends), 131, 142, 155, 166, 179 with `V:4AB8E` = 5, 4, 3, 2, 1, 0, -1 (the pass lengths 16, 15, 12, 11, 13, 11, 13, then the first simulated pass from 179, `V:4A984` `0x0840`). `V:4A83D` = `0xFF` in every sample to k = 260.

**External crash, KP-ENTER to the cockpit while dying** (`crash-flow-external-to-cockpit.txt`; the key pressed at k = 30): the counter turns at k = 32 (the pass begins, the key is read), `V:4A83D` = 0 and `V:4A910` = `0xF0` at k = 33, the dying counter keeps falling (`+0x4C` 12 at k = 33); the counter turns at k = 41 and the pointer shows `0x13980` with `V:4A910` = `0xFF` and `W0` `0x11C8`: the restart began at the next pass. The following passes begin at 41, 55, 68, 79, 89, 98, 106, 115: the flight state is back at 115 (74 blanks after the restart began).

**Keys in the hold** (`crash-keys-view-before-handover.txt`, `crash-keys-view-in-hold.txt`, `crash-keys-pause-in-hold.txt`): KP-ENTER pressed at k = 4 (the make code arrives before the hand-over pass begins at k = 8): `V:4A83D` = `0xFF` at k = 9 and `V:4A910` = `0xFC`, `0xF0`: the toggle is applied in the hand-over pass after the dispatcher; `V:4A951` = 40 was set in the same pass; the hold ended at k = 99 as without the key and the restart (pointer `0x13980`) followed. KP-ENTER pressed at k = 40: `V:4A83D` = `0xFF` at k = 97, the sample in which `V:4A951` first reads 0 and the pointer reads `0x13980`. `P` pressed at k = 40 (and again at k = 200): `V:4A866` = `0xFF` at k = 97/98, the restart and the resume states go on (the pointer reads `0x139C2` at 111 and `0x12EFE` at 171, `V:4A984` `0x0840`), then `0xC0B69E` (`0x13B76`, the pause state) at 179 with `V:4AB8E` = 2, `0xC0B6C8` (`0x13BA0`) from k = 191 to 200 with `V:4A84D` = 0 and `V:4AB8E` jumping (1 at 191, 0 at 193, -6 at 195, then -10, -12, -16, -18, -22 at 200, -23 at 201), and after the second `P` `V:4A866` = 0, `V:4A84D` = 1 and the pointer back at `0x12EFE` at 201 (the pause screen is note 08's); the counter stands at 0x19D from k = 18 to 215 and turns again at 216.

**The pointer in the hold** (`crash-mouse-in-hold.txt`; `mouse` commands at k = 30, 36, 44, 60): `V:4A830` reads 960 before, 860 at k = 45 and 910 at k = 61 (inside the hold), and 72 (with `V:4A834`) at k = 98, the pass A; `V:4A82E` (the horizontal accumulator) moved from 0 to 44 at k = 31.

**Game over** (`crash-flow-gameover.txt`, `poke F:4A94F 1 1` before the crash): the hold ends at k = 96 with `V:4A94F` = 0 (the restart ran: the record is rewritten), the pointer reads `0xC0B518` (`0x139F0`) at k = 100 and `0xC0B55E` (`0x13A36`) at k = 101 with `V:4A802` = `0x0063`; the text screen shows (frames k = 130 and 160 dumps of text on a black screen: the text is typed out); `V:4A802` is 0 again at k = 180, `n` = 0 and the pointer `0xC09598` (`0x11A70`) at k = 188.

**Snapshot reads.** `crash-allowance-snapshots.txt`: `V:4A94F` = 5 and `V:4A95E` = 1 in the airborne, cockpit-ground and cockpit-carrier snapshots (`V:4A900` 0, 0, 3), `V:4A94F` = 3 and `V:4A95E` = 3 in the demo (`V:4A900` 3). `carrier-record-snapshots.txt`: record 14 (`0x4CE3C`): airborne, cockpit-ground and cockpit-carrier: `W0` `0x405C` or `0x404C`, (295,174,144, 0, 274,202,624), cell (70, 65), attitude 0; demo: (286,785,536, 0, 286,785,536), cell (68, 68), attitude 0, 28600, 0; the cockpit-carrier player (295,185,408, 30,472, 274,309,120), `W0` `0x11C8`, `W2` `0x0082`; `V:4ABD4` = 0, `V:4A8E3` = 0 in all of them. `carrier-sublist-select.txt`: the structure `V:1D024` points to (RAM `0xC54498`) reads zero in its first 48 bytes in the four snapshots. `crash-esc-key.txt`: ESC in the air (k from the key press): `V:4A8E2` is consumed within 3 blanks, `V:4A84D` reads 0 from k = 3 and `V:4A866` = 1 from k = 3 to 41, the screen goes black; `V:4A84D` is 1 again from k = 37 to 41; `n` is cleared at k = 42 (`V:4A866` = 0, `V:4A84D` = 0 again) and the pointer goes through `0xC095BE` (k = 3), `0xC095EC` (35), `0xC0960A` (37), `0xC0992C` (49), `0xC09B46` (55, text id `0x66`), `0xC09E74` (87, `V:4A84D` = 1 from then on, `n` counting from 1 at k = 105); the free-flight mission screen is on screen at k = 80 (note 08 owns the screens). `crash-return-key.txt`: RETURN on the deck and in the air changes `+0x63` from `0x0D` to `0x3D` and `V:4A8FB`/`V:4A8FC` from 0 to 3 (counting down to 1 and 0 within 30 blanks), nothing of the position or the flags.

**What these settle.** Note 05 17.17 rules 1 to 12: the wait state's view test and the view toggles, the restart pass, the state names and passes, the write list and what survives, the carrier placement, keys and pause in the hold, the allowance, the effect and the flip routine, the RETURN key, the external restart, the pointer range.

**Fix-round captures** (note 05, re-review of 17.17/17.18; scripts in `tools/vamiga-ref/scripts/`, logs in `extracted/captures/logs/`, all from `dive-precrash.vasnap`): `crash-pass-a-p7b.txt` (`stepuntil` on `V:4A951` = 1, `poke F:4B2B7 1 0xFF`, then one peek set per blank): pass A's record after the forced part id reads `W0` `0x11C8` (not `0x91C8`) with `+0x0A`/`+0x0B` 06/0F, every other byte of the first 32 equal to the unforced cockpit run, and B's rewrite as before: the `0x8000` of pass A is the ground test, gated by `+0x7B`. `crash-hold-palette.txt` (`colors` on every blank from the impact to 130 blanks) and `crash-hold-flash.txt` (frame, `colors` and counters on 8 blanks in the middle of the hold and 14 around its end): the colour registers hold `0x0FB2` in all 32 entries on every second blank of the hold (the blanks at odd distance from the first hold blank, 40 of them, the last one blank before `V:4A951` reads 0) and the game palette on the others; every frame dump of the hold is the same normal frame (one md5) except the dump in the blank in which `V:4A951` first reads 0 (uniform orange, the same md5 as the earlier run's k = 99 dump); the registers read the game palette in that blank. `crash-gameover-gate.txt` (the allowance poked to 1, one peek set per blank from `V:4A951` = 3): at k0 (`V:4A951` first 0) the record is rewritten, the pointer stays `0x138D8` for 4 blanks, `n` does not turn, `V:4A84D` is 1 until k0 + 3 and 0 from k0 + 4 (pointer `0x139F0`, `V:4AB8E` = 5), k0 + 5 pointer `0x13A36`, `V:4A802` = `0x63`, `V:4AB8E` -1, `V:4A898` `0x81` to 0 at k0 + 6, `V:4A802` 0 at k0 + 84, `V:4A898` 1 at k0 + 84 and `0xFF` at k0 + 92 with the pointer `0x11A70` and `n` = 0; `V:4A84D` is 0 in every sample to k0 + 133 (the earlier `crash-flow-gameover.txt` did not peek it). `carrier-record-snapshots.txt` rerun: record 14 `W0` `0x405C` in the airborne and cockpit-ground snapshots, `0x404C` (with `W2` `0x0001`) in the cockpit-carrier and demo snapshots. Hold lengths over the seven runs with a hold (`crash-flow-cockpit`, `-gameover`, the three `crash-keys-*`, `crash-mouse`, `crash-hold-palette`): last pre-hold turn to `V:4A951` = 0 is 80 blanks in three runs and 81 in four; first fall to 0 is 78 in three and 79 in four; a single 3-blank iteration occurs in the four 81-blank runs (at k = 44, 72, 80 and 67 of their logs).

## Question 6: sky fill, ground polygon, palette and the sea-zone flag

Milestone-3 round for Task 8 (note 04, "Sky fill and ground polygon"). The code of the sky fill (`0x3486C`), the ground polygon (`0x32E5A`), the filler (`0x34CD6`) and the zone lookup (`0x230F8`) was run in the
Musashi rig with a blitter model (`tools/vamiga-ref/sky-ground/`) and checked against emulator frames of staged, **paused** states. Method: restore `airborne.vasnap`, freeze, poke the player record's angle words (`F:4B2A2`, `F:4B2A4`,
`F:4B2A6`: pitch, heading, roll; 28800 units per circle, **negative pitch is nose up, positive roll is left wing down**: at roll +1600 the horizon is at row 40 in column 0 and 138 in column 319) and height (`F:4B254`), and the pause byte `V:4A866`;
thaw for 2.2 s, freeze, then log the state block (`peek F:4A700 2336`), the player record, the colour registers and the frame. The scripts are generated by `tools/vamiga-ref/scripts/gen-sky-ground.py`:
`sky-ground-attitude.txt` (44 states, one of them torn as described below: pitch -90 to +90 degrees, roll to full inversion, five headings), `sky-ground-height.txt` (7 heights from 30,472 to 8,388,608 position units, each level and banked),
`sky-ground-sea.txt` (the carrier's zone at two heights, eight view octants, forced list refresh `V:4A910` = 0xFF), `sky-ground-blackout.txt` (the fade). Logs: `extracted/captures/logs/sky-ground-*.txt`; frames: `extracted/captures/sky/`.

**Hygiene found.** A paused machine redraws the same state, but (1) a complex frame needs more than one second to reach the display (at 1.0 s one frame still showed the previous state; 2.2 s was used), and (2) the frozen machine can stop between the
camera-matrix build and the zoom row scale: one of 74 states had rows 1 and 2 unscaled (row norms 255 instead of 252 and 128), which `check_frames.py` detects and skips. State blocks therefore need the row-norm test.

| what | result | evidence |
|---|---|---|
| horizon at pitch 0, roll 0, heading 4000 | last sky row 89, first ground row 90, in every column, at heights 30,472 to 8,388,608 position units (the camera-height word -34,173 to -8,392,309) | `b-y*-r0`, `a-p+0-r0` |
| pitch (nose down positive), roll 0 | polygon rows at the left and right edge: 74 and 73 at +400, 59 and 57 at +800, 105 and 103 at -400, 120 and 119 at -800 (the sky is rows 1 to about that row); no sky at +3200 (all ground), all sky at -7200 and -3200 | `a-p*-r0`; the left-right row difference is the heading-dependent corner geometry |
| roll (positive = left wing down), pitch 0 | polygon (0, 65) (319, 111) at +800, (0, 40) (319, 138) at +1600, (0, 10) (319, 166) at +2400; the sky leaves the left edge at +3600 (polygon (52, 0) (265, 179)); inverted (14400): sky below row 89 | `a-p0-r*` |
| heading | at roll 800 the crossings move by up to two rows with the heading (left 65 to 67, right 112 to 114 for headings 0 to 22,000) | `a-h*` |
| height | the crossing rows change by at most one row between 30,472 and 8,388,608 position units (right edge 153 to 154 at roll 1600) | `sky-ground-height.txt` |
| frames against the model | 73 consistent states plus 19 view frames (cockpit side, rear, up, down, ten external views): 3.1 million classified pixels, no difference | `check_frames.py`; 543,733 sky and 345,671 ground pixels in the view frames |
| colour registers | identical in all 58 states with `colors` (the 16 entries of `palette/flight.pal`, hash in note 04; 16 to 31 black) | `pal_*` lines |

| sea-zone state (player at zone (17, 16), `V:4AA00`, `V:4AA02`) | altitude A (units of 256 pos) | flag `V:4A953` per view octant 0 to 7 | green (pen 4) pixels in the window |
|---|---|---|---|
| 60,000 pos high, heading octant k (heading 3600 k + 1800) | 249 | 1 in all eight | 0 in all eight |
| 600,000 pos high | 2,359 | 0 except octant 5 (1) | 1,341, 1,980, 1,617, 453, 0, 0 (octant 5), 0, 624 for octants 0 to 7 |

All 220 state blocks in the current capture logs (11 with the flag set, 31 with the second flag `V:4A954`) agree with the lookup model of note 04 (`check_zone_logs.py`).

| blackout fade (`sky-ground-blackout.txt`: `V:4A941` poked to 15 at t = 0, samples every 0.2 s then every second) | value |
|---|---|
| fade level `V:4A958` at the samples | 14, 10, 8, 5, 2 (after 0.2 s steps), then 2 until the accumulator falls to 5 |
| target `V:4A959` | 2 from the first sample; back to 15 when `V:4A941` has fallen to 5 (sample 47, about 11 s) |
| colour registers at each sample | all 16 entries equal step (15 minus the level) of the table at `0x29FA4` at every sample checked (levels 14, 10, 8, 5, 2, 5 and 15; e.g. level 14: entries 1, 5, 6, 9 = 0x700, 0x336, 0x025, 0xC81; level 2: all 000) |
| accumulator `V:4A941` | falls by one about every 8 to 9 game frames (every mission-clock second), 15 to 5 over about 10 s, then 4, 3 ... 0, -1 (0xFF) |
| level on the way back | 5, 15 within two one-second samples (a step every third blank, confirmed per blank) |

Flown afterwards (note 04, "blackout accumulator"; `holduntil` scripts from `airborne.vasnap`; script, log and name: `sky-ground-blackout-flown.txt` (the flight), `sky-ground-blackout-pull.txt` (the keyboard pull alone), `sky-ground-fade-perblank.txt` (per blank, with the fade counter); the logs are under `extracted/captures/logs` with the same names, git-ignored; the stale-colour sampling scripts and logs are `sky-ground-stale-colour.txt`, `-deck`, `-external`, `-level`, `-map` and `-hudoff`): the keyboard pull alone gives |E| up to 510 (UP held for 3 s; coupling `V:4A9FE` 0) and a coupling of about -100 only in a hard bank, and E + (E >> 3) at 510 is 573, so the latch (the value (E + (E >> 3) + coupling) >> 3 at least 78 in magnitude) needs both. With the roll held past 6,000 and then full back stick (pitch command -20) the latch was set at E = -461, `V:4A9FE` = -102, roll 9,744 (122 degrees): (-461 - 58 - 102) >> 3 = -78. The accumulator then rose from -1 to 0 on the next mission-clock tick (mission second 98 to 99) and fell back to -1 on the tick after that (second 100), the latch having cleared in between. The seven consecutive ticks that take the accumulator above 5 were not held. The 0.2 s sample fade levels above (14, 10, 8, 5, 2) are wall-clock samples: the emulated machine ran slower than real time; per blank (150 `step` commands with `colors`) the level moves once per 2 blanks down and once per 3 up (note 04).

## Questions settled and not

| open question | status |
| --- | --- |
| note 04 Q2: units and range index | settled: index and table, the beyond-table read (hunk 65 bytes, clamp 0x7FFE), and that 4AA6A has no effect on drawing (dead variable); the list builder's range window is still unmeasured |
| note 04 Q5: position unit | settled: offset = 256 pos = 1.25 ft; 1 ft = 204.8 pos; the cell wrap is 16384 offsets |
| note 04 Q6: palette bands | settled: no palette change from 0 to 40,960 ft in free flight; 0x4A90C is the view-heading octant (note 04a), not a band and not the cell phase (`V:4A908`); h8+0x376 described above. Question 6 above: the only in-flight palette change is the blackout fade; the ground base is pen 4 or 6 by the sea-zone flag |
| note 04: sky fill, ground polygon, selector, `V:4A953` | settled (Question 6): horizon polygon verified pixel for pixel, bit 13 of `V:4A98A` never set, `V:4A953` is the sea-zone flag, stage [68] draws the sea mesh (pen 6) |
| note 04 Q10: carrier visible, distance | visible 0.6M to 10.0M pos, no gap; band rule consistent |
| note 02 Q6: physical units | altitude figure and unit settled as above; instance scale open |
| note 02 Q8 and note 03: handedness, angle roles | settled (right-handed, +z north, roll right negative, pitch/heading/roll roles) |
