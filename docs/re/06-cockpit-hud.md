# 06: cockpit view, HUD, instrument panel, warnings, head views and settled view kinds

Scope: everything drawn over the 3D window in the cockpit view (the HUD, the cowl, the instrument
panel and its readouts and lamps, the radar scope, the message line and the warning arbiter), the
text strip of the external views, the head-turn views (offsets, window rows, camera angles), and the
camera rules of the external, map and head views that the core had to guess (section "View kinds,
settled"). Coordinates are bitmap pixels of the 320x200 screen (`04-scene-composition.md`): x 0 to
319, bitmap row y 0 to 199, row 0 is frame row 42. Addresses are flat-image addresses
(`00-binary-layout.md`); `V:xxxxx` is a variable in the hunk 71 area, `E` is the player's entity
record (`V:4B23C` plus the entity offset `V:4A996`, zero for the player) and `E+0x68` means that field.

How the facts were obtained (no code is quoted anywhere in this note). The HUD and panel routines were read from
the image and then **run** in a harness (the committed harness `tools/hud-emu/`, see "Reproducing"): a Unicorn 2.1.4
68020 emulator is given the 512 KB chip RAM and the 512 KB slow RAM of a reference-emulator snapshot at their
real addresses, so the game's own relocated code and data run unchanged; a routine is entered at its flat address
translated through the hunk list; the OS wait-for-blitter stubs return at once; a model of the blitter (copy with A and B
shifts, ascending and descending, area fill, line mode with the single-pixel bit, modulo and pointer rules of the
hardware reference) executes the blitter register writes; the packed-decimal instructions are replaced by the 68000 rule
(the emulator's own differs on the invalid digit the tape arithmetic uses). State is then set by writing words into the
snapshot memory (entity record, state block from a log), the routine is run and the four bitplanes are compared with the
captured frame (PPM crop at frame (186, 42), every second column). Unless a row says "read only", positions, pens and
formats below are measured on the real routines, not inferred. Where the note says "verified" without further qualification
it means identical pixels in such a comparison.

## Location

### HUD and panel routines (all hand-written assembly, register calling)

| Address | Hunk | Role | Status |
| --- | --- | --- | --- |
| 0x112CC to 0x113F6 | 0 | per-frame call schedule of the HUD and panel routines (see "Draw schedule") | verified (read) |
| 0x37D78 | 40 | HUD driver, cockpit views only; calls the five parts below | verified |
| 0x37DBA | 40 | nose datum (small marker above the cowl) | verified |
| 0x38C02 | 40 | crosshair, level marks, tape centre mark data, and the five-segment HUD outline from the table at 0x3901C | verified |
| 0x38D8C | 40 | target marker slew and target box (milestone 5) | run (shapes), read (conditions) |
| 0x38884 | 40 | target box (milestone 5) | read, partly run |
| 0x36724 | 39 | weapon readout left of the nose datum | verified |
| 0x36828 | 39 | G readout (F/A-18); other aircraft take the alternate branch | verified |
| 0x37E2C | 40 | speed, altitude and heading tape (F/A-18 numeric form from 0x37E78; F-16 scale form from 0x37ED6) | verified |
| 0x385F4 | 40 | lock marker and pipper, only at zoom factor 0x80 (milestone 5) | read, shapes run |
| 0x35238 | 37 | panel redraw: copies the clean panel picture (55 rows from row 145) with the view offset | verified |
| 0x3548A | 37 | cowl overlay (cookie-cut, 12 rows from row 133) | verified |
| 0x35CF2 | 38 | radar scope: object list walk, blip plotting, threat flags | verified (draw), read (flags) |
| 0x35BF6 | 38 | five small threat and stall lamps at the right of the panel | verified |
| 0x35A4C | 37 | compass tape (with the red index) | verified |
| 0x36ADE, 0x36A10, 0x36C3C | 39 | panel altitude, speed, throttle percent digits | verified |
| 0x36BEE | 39 | fuel digits | verified |
| 0x353EC | 37 | fuel gauge bar | verified |
| 0x34B1A | 36 | attitude ball | verified |
| 0x36C96, 0x36D24 | 39 | latitude, longitude digits | verified |
| 0x36528, 0x36590 | 39 | radar range digits, magnification digit | verified |
| 0x3736C | 39 | weapon text on the left display ("armed" line and weapon-type line) | verified |
| 0x354D4 | 37 | weapon station markers on the left display | verified (draw), read (rules) |
| 0x35630 | 37 | afterburner line, BRAKE, ECM and EJECT lamps | verified |
| 0x35808 | 37 | GEAR lamp and gear symbol | verified |
| 0x36DB2 | 39 | message line driver (warning text, or target data) | verified (warning path) |
| 0x3706A, 0x37126, 0x37258 | 39 | message-line drawer and the 3x5 text drawer, version 1 | verified |
| 0x37508 to 0x375C4 | 39 | 3x5 text drawer, version 2 (four planes, one pen) used by the HUD and panel digits | verified |
| 0x29ECC | 20 | binary to packed-decimal conversion used by every numeric readout | verified (run) |
| 0x13D4C | 0 | warning arbiter (picks the message code word every frame) | read |
| 0x13C94 | 0 | countdown generator that produces the message tick bits | read |
| 0x13D00, 0x29BC8 | 0, 20 | post a message code word | read |
| 0x377B2 | 39 | mission and training text scroller in the message window | read only |
| 0x2FC58 | 20 | map ("tower") overlay: grid, labels, entity marks; helpers 0x33936, 0x3377C, 0x2F6FA, 0x30046, 0x3001C | verified (frames) |
| 0x3936E, 0x3905C, 0x392AE | 40 | target marker shapes, lock circle, range dot | verified (drawn by the real routines), conditions read |
| 0x1FF24 | 0 | radar list builder (only caller of 0x29D3A) | read, shift run |
| 0x340A0 to 0x3414E | 36 | pixel plotters; 0x34558, 0x3455E: line drawer (blitter line mode) | verified |

### Data

| Address | Hunk | Content |
| --- | --- | --- |
| 0x41FF0 | 59 | 54 warning entries of 28 bytes (see "Warning line") |
| 0x426D0 | 61 | 3x5 glyph table (`02-assets-and-tables.md`, "Fonts and HUD glyphs"); extracted to `assets/fonts/hud.bin` |
| 0x42AAC to 0x42BFF | 62 | one-plane symbol masks: gear lamp mask 0x42AAC; four gear symbols 0x42AC8, 0x42AE4, 0x42B00, 0x42B1C (28 bytes each, 7 rows of 2 words; variant v uses the one at 0x42B1C - 0x1C v, the table at 0x357F6 lists them last first); attitude ball mask 0x42B38 (21 rows of 2 words); 0x42B8C (second mask used with the ball); compass strip 0x42BAC (7 rows of 12 bytes) |
| 0x36922 to 0x371D7 | 39 | not a table of labels: it is mostly the text-drawer code; the only text is the shoot prompts at 0x36923 ("in range" form) and 0x36929 (gun form) and, near 0x3717C, the target data words and aircraft tags (see "Target symbols") |
| 0x3948C, 0x394A6 | 40 | target marker sprites (point lists) |
| 0x39408 (42 bytes), 0x39432 (first sub-list 40 bytes) | 40 | lock circle quarter arc (20 pairs) and range dot arc (19 pairs), signed byte pairs, see "Target symbols"; the region 0x3945A to 0x3948C (25 pairs, unreferenced) is a second terminator pair, a 23-pair arc from (0,14) to (16,0) at 0x3945C and its terminator at 0x3948A |
| 0x1E800 | 5 | 14-byte HUD step table per cockpit view code |
| 0x3239A | 32 | head yaw table, 11 words; 0x323DC the same for the ejected pilot |
| 0x2D664, 0x2D6A0, 0x2D6DC, 0x2D718 | 26 | external camera ring offsets: default, class 0x11, class 0x14, class 0x30 |

## Display model used by every routine here

- **One 16-colour surface.** The drawing planes are four (`V:4A76E` points to a table of plane
  pointers, plane 3 first, then 2, 1, 0). The routines write pens, not RGB. Pen n is entry n of the
  in-flight palette (`02-assets-and-tables.md`, the first 16 words at 0x29FA4).
- **Palette entries used by the HUD and panel** (12-bit value, and the 8-bit RGB the emulator shows):

| pen | shown | use here |
| --- | --- | --- |
| 0 | (0,0,0) | black: panel text background, HUD sides |
| 1 | (106,0,0) | alert text, lit lamps on black, ball ground, red compass index |
| 2 | (106,106,106) | compass letters, ball aircraft symbol |
| 3 | (0,0,0) in the reference emulator; (17,17,17) if the 4-bit level is simply multiplied by 17 | panel and strip background (see "Pen 3 and the emulator's colour curve") |
| 4 | (0,56,0) | panel digits, information text, fuel bar, left display text |
| 5 | (43,43,89) | sky; ball sky; lat/long box fill |
| 8 | (56,56,56) | HUD nose datum and chamfers; lit lamp grey |
| 9 | (197,123,0) amber | lit lamps over grey, afterburner line |
| 10 | (72,72,72) | HUD outline top, crosshair, level marks, external text |
| 13 | (89,89,89) | HUD text, tape ticks, ball bank dots |

  The emulator colour steps are 4-bit value 3 to 28, 4 to 43, 5 to 56, 6 to 72, 7 to 89, 8 to 106,
  9 to 123 per channel.
- **Pen 3 and the emulator's colour curve.** Pen 3 is palette entry 3 of the in-flight palette (hunk 21 at 0x29FA4; its 4-bit level is 1 in each channel); no HUD or panel
  routine changes it. The reference emulator shows it as pure black because its colour output is not linear: every captured level of
  a channel maps 0 to 0, 1 to 0, 3 to 28, 4 to 43, 5 to 56, 6 to 72, 7 to 89, 8 to 106 (the pens 3, 6, 12, 4, 5, 7 and the greys of the in-flight
  palette give these points; levels 9 and 13 are observed too, on pen 9 (0xD92: 123 and 197); only levels 2, 10 to 12 and 14 are the harness table, not observed on a HUD pen; level 15 is measured on the crash-hold flash frame, 236, see open question 8). The emulator's pixel engine applies a gamma in and gamma out curve
  with brightness, contrast and saturation settings to every colour, which is why a level of 1 vanishes. This is therefore a palette-conversion
  matter (note 02 and the frontend palette task), **not a HUD matter**: the cockpit code only writes pen indices. A frontend that converts
  the 12-bit entries linearly (level times 17) shows pen 3 as (17,17,17), faint grey boxes beside the black rows; to reproduce the captures
  one converts through the measured table above, to reproduce a real monitor one keeps the linear value. **Rule:** the reference captures are the authority, so the
  port's palette conversion uses the measured emulator curve (level 1 maps to 0, so pen 3 is black and the radar cone lines drawn in pen 3 are invisible, as in every capture). The curve is the table of measured points above; levels 9 (123) and 13 (197) are observed on pen 9, and levels 2, 10 to 12 and 14 were not observed on a HUD pen; the harness
  fills those with 0 for level 2 and 141, 159, 177, 216 for levels 10 to 12 and 14, and 255 for level 15, which the flash frame corrects to 236 (the table `M4` in `tools/hud-emu/emu3.py` also holds 123 and 197 for the observed levels 9 and 13). Note 02 lists the palette words (first 16 words at 0x29FA4) but holds no conversion table; the conversion belongs to the frontend palette code, which applies this curve to all 16 entries.
- **Pixel plotters** (0x340A0 and the family): a point is skipped when x plus the view offset
  `V:4AA40` is below 0 or at least 320, or the row is at or below 0; the plot writes the pen
  `V:4AA0C` (low 4 bits) into the four planes, and is gated by a 20-bit pattern word `V:4A79E`
  (the HUD sets it to all ones before drawing). 0x340EA plots two pixels (x and x-1). A line
  (0x34558 clips at row 199, 0x3455E at the window's last row `V:4AA3C`) is drawn by the blitter in line mode:
  rows from the upper end plus 1 to the lower end; **a horizontal line is drawn one row below its y**;
  the pen is `V:4AA0C`.
- **Text:** glyphs are 3 columns by 5 rows (table 0x426D0, character code minus 0x20, 64 entries). Every
  character is positioned from a per-element table of (byte column, shift) pairs, so pitches differ per
  element (usually 4 pixels, with an extra gap of 2 or 6 pixels before the thousands or fraction digits).
  A character is skipped when its byte column plus the view offset falls outside 0 to 39.
  Version 1 of the drawer (message line, external strip) writes two planes: the glyph pixels in one and the
  inverse in another, so the pen of the glyph is the background pen with plane bits changed (message line:
  see below). Version 2 (HUD and panel) writes a single pen (`V:4AA0C`) into all four planes for the glyph
  pixels. The cell background differs by caller: the panel digit routines repaint the whole 3x5 cell (glyph pen and box pen), while the HUD
  writes the glyph pixels only and leaves the rest of the cell as it is (see "Glyph cells" below).
- **View offset.** `V:4AA3E` is a signed step and `V:4AA40` is 16 times it: every HUD, cowl, panel and
  lamp coordinate is shifted right by that many pixels (negative: left). See "Head views".

## Draw schedule (one frame, cockpit view)

The frame routine calls these in this order after the world (variable `n` is the frame counter
`V:4A992`, `R` is the redraw counter `V:4A8EF`, set to 3 after any view change and counting down):

1. 0x35238 only while `V:4A8EE` is positive (set to 3 on a view change; each call copies the clean panel
   and counts down), then 0x3548A (cowl), 0x35CF2 (radar), **0x37D78 (HUD)**, 0x35BF6, 0x35A4C.
2. 0x3697A and 0x36A10 when R > 0 or (n and 3) > 1 (these draw the external text strip, and in the cockpit the
   speed digits).
3. 0x36C3C (throttle) when R > 0 or (n and 3) < 2; 0x36ADE (altitude) when R > 0 or (n and 3) < 2 or `V:4AB1E`
   exceeds -0x8000 (a long, -(camera y) per 01b: camera below 32,768 pos, about 160 ft); 0x36BEE (fuel digits) when R > 0 or (n and 7) < 2; 0x353EC (fuel bar) when R > 0 or
   (n and 15) < 2.
4. Always: 0x34B1A (ball), 0x3736C, 0x354D4, 0x36C96, 0x36D24, 0x36590, 0x35808, 0x36528, 0x36DB2, 0x35630.
5. R counts down by one per frame while non-negative.

**Gating a replica must keep.** The calls above are gated twice: by the frame schedule (a window of the frame counter `n`) and by the
drawing routine itself. Window: speed on (n and 3) of 2 or 3, throttle and altitude on (n and 3) of 0 or 1, fuel digits on (n and 7) of 0 or 1, the fuel bar
on (n and 15) of 0 or 1 (verified by reading the caller at 0x112F0 to 0x11392). Inside the window the speed and altitude routines (0x36A10, 0x36ADE; the
speed one shares the helper at 0x366E4) keep a cache of the value they last drew and decide as follows (verified by reading the code): (1) while R > 0 the value is drawn
every frame and stored in the cache with a redraw flag set (bit 15 of the speed word `V:4A9B0`; bit 31 of the altitude long `V:4A9B8`, whose low word
`V:4A9BA` is the 5 times (y shifted right by 10) figure that 01b reads); (2) otherwise, if the flag is set, it is cleared and the cached value is drawn once more
(the second draw of the same figure, on the next call); (3) otherwise, if the value equals the cache, nothing is drawn; (4) otherwise, if bit 0 of the frame
counter (the byte `V:4A993`, the low byte of the word `n` = `V:4A992`, which is what the increment advances; the increment is on the word) is set, nothing is drawn, and if it is clear the new value is stored with the flag set and drawn (when the pause-like byte `V:4A866` is nonzero the old cached value is drawn instead and the
cache keeps its old value). So a new figure can only be taken on frames with bit 0 of `n` clear: for the altitude, (n and 3) = 0 (the window frame (n and 3) = 1
draws the stored figure again for the flag); for the speed, (n and 3) = 2, with the repeat on 3. **This settles the cadence:** the panel altitude takes a new value at most once per
four increments of `n`, and the displayed digits lag the state by up to 4 frames for speed and altitude (5 after an R burst, because the first window frame after the burst only clears the flag; 01b observes 5) (8 and 16 for the fuel digits and bar by the same pattern, window frames
0 and 1 only). The caches are written only by the drawing routine (the only absolute-address stores to `V:4A9B8` are in 0x36ADE; `V:4A9B0` is reached only through 0x36A10 and
the helper), not by the world update; there is no other writer found. 01b's four-step period and lag of 0 to 4 (at most 5) steps match this if one
game step is one increment of `n` (inference: the increment is at 0x114B6, once per flight-path pass of the frame routine, that is once per pass after the `V:4A84D` gate (0x11140) and the pause test at 0x114A6 have both let the pass through, not on idle-path passes and not while `V:4A866` is nonzero; the equality of 01b's step counter with `n` holds: 01b reads the same word `V:4A992` that is defined as `n` here, and the panel refreshes at `n` = 0 mod 4 in 01b's log corroborate the gate).
01b's burst on steps 273 to 275 (three consecutive refreshes) is **not produced by the steady gate**; it is consistent with R, the page-redraw counter `V:4A8EF`, being 3 for
three frames (R > 0 forces the draw every frame, rule 1; R is set to 3 by view changes; other code references it at 0x1EFF6, 0x1FEFE, 0x253FA, 0x25A56 and 0x25D1A, not read), which is the best candidate (inference, the
setter during the climb was not traced). `V:4AB1E` above -0x8000 (a long; it is -(camera y) per 01b, so the camera is below 32,768 pos, about 160 ft) would call the altitude routine on every frame, but rule 4 would still limit new values to every second
frame, so it cannot by itself give three consecutive refreshes. A replica should take a new panel altitude when (n and 3) = 0 (draw it again on 1) and keep the old
value otherwise; the burst is explained only as an inference.
The HUD readouts (speed, altitude, G, tape, weapon text) are not cached and are redrawn
from the current state every frame, with the display lagging the logged state by one frame. Elements with their own counter are drawn
while that counter is positive (it is set when their value changes) and the picture keeps them in between; the counters are:

| counter | element | set by |
| --- | --- | --- |
| `V:4A8F1`, `V:4A8F2` | latitude, longitude digits | the digit routines themselves on a change (read) |
| `V:4A8F3` | radar range digits | on a change (read) |
| `V:4A8F4` | message line (2 frames per entry) | the message driver |
| `V:4A8F5` | magnification digit | the zoom keys (raw 0x1A, 0x1B in the cockpit set it to 3) |
| `V:4A8F6` | afterburner line | the throttle code (read) |
| `V:4A8F7` | BRAKE lamp | the brake key code (read) |
| `V:4A8F8` | ECM lamp | the J key (raw 0x26) sets it to 3 |
| `V:4A8FA` | EJECT lamp flash (counts 8 down) | the E key (raw 0x12) |
| `V:4A8FB`, `V:4A8FC` | weapon stations, weapon text | the weapon-select code (read) |
| `V:4A8FD` | gear symbol | the gear code (read) |

`V:4A8F9` lies inside this range but is unused: the only absolute reference in the image is the clear that zeroes the whole group at 0x1F020 (verified for absolute
operands; an access through a base register was not excluded), and no routine here reads it.

`V:4A8FE` is **not** a timer: it is the GEAR lamp state (see the lamp table) and must be reproduced. In the external views the schedule
is 0x36A10, 0x36ADE, 0x3697A (and 0x36DB2 when `V:4A95E` is 2) and nothing else.

## HUD (F/A-18, entity class `E+0x62` = 0x11): elements

The HUD is drawn only in cockpit views (selector `V:4A83D` zero); with the H key (`V:4A859`, toggled 0/1; the reset block of note 08 6.1 clears it, and the only writer of the constant 1 is the last pre-flight state `0x12EDA` at `0x12EE6` (note 08 6.2), so it is 1 from the first flight pass of every start that passes through that state; the key routine `0x1DF90`-`0x1DFA0` flips it, and `0x37DA0` is the HUD driver's reader [R: a scan of the image for the absolute address finds these four references and no others]) off, only the outline, crosshair, level marks, nose datum, weapon readout and target symbols
remain. There is **no pitch ladder, no roll scale, no vertical speed, no velocity vector and no ILS cue** in
the F/A-18 HUD or anywhere in the cockpit code: the horizon is the 3D world itself (verified on every
cockpit capture, level, climbing and banked, and none of the HUD routines draws one). Colours
are fixed; the HUD does not change with altitude, G or mode. All coordinates below are exact (pixel
lists from the running routines), before any view offset.

### Fixed marks (0x38C02, 0x37DBA)

| element | pixels | pen |
| --- | --- | --- |
| outline top | row 37, x 93 to 224 | 10 |
| outline left chamfer | (85,45) rising to (92,38), one pixel per row | 8 |
| outline right chamfer | (225,37) falling to (233,45) | 8 |
| outline sides | x 85 and x 233, rows 46 to 144 | 0 (black, drawn over the world) |
| crosshair | x 159 at rows 71, 72, 74, 75; row 73 x 157 to 161 | 10 |
| level marks | row 91, x 141 to 145 and x 173 to 177 | 10 |
| nose datum | x 159 to 160, rows 129 to 131; row 132 x 158 to 161 | 8 |

(The horizontal lines are drawn one row below their nominal y: the code gives 72 and 90 for the crosshair
bar and level marks and 36 for the top.) The outline is a polyline of six points at 0x3901C (word pairs) with one pen per segment at
0x39034 (pens 0, 8, 10, 8, 0): (85,144)-(85,45) pen 0, (85,45)-(93,36) pen 8,
(93,36)-(225,36) pen 10, (225,36)-(233,45) pen 8, (233,45)-(233,144) pen 0; it is drawn only when the
player's entity class is 0x10, 0x11 or 0x12. The crosshair centre is (159, 73) and the level marks (the
horizon of level flight) sit on row 91.

### Heading tape (inside 0x37E2C)

A strip at the top: two or three labels, ticks and a centre mark.

- Heading `h` in tenths of a degree is `E+0x68` right shifted by 3 (the field is in 1/80 degree).
- Let r = h modulo 100. If r is above 40, the base label is the next multiple of 10 degrees and the
  signed remainder s = r - 100; otherwise the base label is the multiple below and s = r. (Compare:
  heading 28.2 rounds to 30, heading 24.0 stays 20.)
- Offset `o` = minus the quotient of s divided by 5 (truncated toward zero). Pixels per degree: 2.
- Base tick x = 159 + o. Ticks repeat every 10 pixels (5 degrees) to the left and right, only for
  x from 134 to 184 inclusive. The ticks at the base and at multiples of 20 pixels from it (the 10-degree
  ticks) are two pixels tall (rows 60 and 61); the ticks between them are one pixel (row 61). All pen 13.
- Centre mark: x 159, rows 63 to 65, pen 13.
- Labels: the heading in degrees as three digits with the last digit dropped, that is, the tens-of-
  degrees number 00 to 35, in two glyphs at x = 156 + o and 160 + o (glyph origin; glyph width 3), rows 53
  to 57, pen 13. The base label is always drawn; the label for base plus 10 degrees at 156 + o + 20 is drawn
  when o is at most 2; the label for base minus 10 degrees at 156 + o - 20 is drawn when o is at least -2
  (so one to three labels). Values wrap at 36 to 00.
  (The original does this with decimal-adjusted additions of the packed value; the behaviour on the invalid
  digit pair is the standard 68000 one, so the labels are the plain decimal ones given here.)

### Readouts (numeric form)

| readout | slots (glyph x), row of glyph tops | value | notes |
| --- | --- | --- | --- |
| speed | x 92, 98, 102, 106 at y 84 (rows 84 to 88) | the absolute value of the word `E+0x6E` divided by 12 (knots) | right aligned, leading zeros blank, one digit minimum; value 0 when the "hit this step" flag (bit 0x8000 of the word `E+0x00`, note 05) is set; label "KT" at x 102 and 106, y 91 |
| altitude | x 204, 208, 212, 218, 222, 226 at y 84 | 5 times (`E+0x18` long, right shifted by 10) (feet; the field is a position unit of 1024 per 5 feet) | six digit slots with a 6-pixel gap before the last three; label "FT" at x 222 and 226, y 91 |
| G | glyphs at x 96 (tens), 102 (units); a decimal point pixel at (100, 125); "G" at x 108; row 121 | see below | pen 13 |

All pen 13. **Glyph cells.** The HUD drawer writes only the glyph pixels: the other pixels of the 3x5 cell are left as they are
(checked by drawing over a flat sky: 12 pen 13 pixels for the "0" and nothing else), and a blank slot writes nothing; the panel
digit drawer instead repaints the whole cell (glyph pen and box pen) and a blank slot repaints its cell with the box pen, so old
digits never survive (checked by drawing 8888 and then 1111: zero residual pixels).

**Leading digits (verified by running every readout over value sweeps, "Addendum rules").** All numeric readouts print through one digit
drawer that turns the packed-decimal value into characters and then replaces leading zero characters by spaces for every digit slot
except the last, so every number shows at least one digit and never a padded zero. The HUD draws nothing for a blank slot; the panel
repaints the cell with its box pen.

| readout | slots (glyph x) | value taken | result |
| --- | --- | --- | --- |
| HUD speed | 92, 98, 102, 106 | the absolute word divided by 12 | 0 shows "0" at 106; 120 shows "10" at 102, 106; 12000 shows "1000" |
| HUD altitude | 204, 208, 212, 218, 222, 226 | 5 times (long >> 10) | 0 shows "0" at 226; 60000 shows "60000" at 208 to 226 |
| HUD G | tens 96, units 102, sign 92 | see below | tens blank when zero, units always drawn: a figure below 1.0 shows as the point and one digit (".8", ".9") with no leading "0" |
| gun rounds | 108, 112, 116 | `E+0x60` as an unsigned 16-bit word, **modulo 1000** (three decimal digits kept) | 0 shows "0" at 116; 5 shows "5"; 50 shows "50"; 500 "500"; 1234 shows "234"; the word 0xFFFF shows "535" |
| missile count | 108 | one nibble of `E+0x5F` | a single digit; a stock of 0 draws no digit at all (verified), the "AM" or "SW" tag stays |
| panel speed | 242, 248, 252, 256 | as the HUD | leading cells blank-painted, last digit always |
| panel altitude | 240, 244, 248, 254, 258, 262 | as the HUD | same |
| throttle | 240, 244, 248 | byte `E+0x2B` times 256 divided by 307 | 0 shows "0" at 248; 0x28 shows "33"; 0x78 shows "100" |
| fuel digits | 104, 108, 112, 116, 120 | `E+0x72` >> 8 as a signed word | 0 shows "0" at 120; a negative word draws nothing (the cache helper rejects it) |
| latitude, longitude | 292 (blank for latitude), 295, 299, 305; longitude 292 to 305 | tenths of a degree | leading zero blank, the digit before the point in the picture always drawn |
| radar range | 144 (always blank), 148, 152 | 2, 10, 40 | "2" is drawn at 152 with 148 blank |
| magnification | 242, 248 | 10, 20 or 40 | two digits |

**G readout.** With w = `E+0x56` (word, the pitch rate, note 05), c = `V:4A9FE` when its magnitude exceeds 1 and 0 otherwise, and
s = +10 when bit 1 of `V:4A982` is set and -10 otherwise (note 05, "G meter"): v = ((w + floor(w / 8) + c) shifted right by 3) + s,
shifts arithmetic. The figure is |v| in tenths: printed with a minus sign at x 92 when v is at least 0 and a blank sign when v is
negative (so level flight, v = -10, prints 1.0). Two digits (tens digit at x 96 blank below 10, the units digit at x 102, point pixel
at (100, 125)), so values of 10 G or more wrap. (An alternate altitude path guarded by `V:4A85C` exists; nothing in the image ever
sets that byte, so it is never taken.)

### Weapon readout (0x36724), left of the nose datum

Row 129 (rows 129 to 133). Selection is the high nibble of `E+0x63`: 0 shows nothing; 0x10 is the gun:
"GUN" at x 92, 96, 100 and the rounds count (word `E+0x60`, unsigned, the three low decimal digits, leading zeros blank, see "Leading digits") at x 108, 112, 116; 0x30 is the
AMRAAM: "AM" at x 96 and 100 and the count (low nibble of `E+0x5F`, one digit) at x 108; any other nonzero
nibble is the Sidewinder: "SW" at 96 and 100 and the count (high nibble of `E+0x5F`) at x 108. Pen 13.

### Target symbols (data only; drawn in milestone 5)

Variables (settled in the addendum round; the earlier text mixed up the roles):

| variable | meaning | written by |
| --- | --- | --- |
| `V:4A9EE`, `V:4A9F0` | the designated target's projected screen point of this frame with the view offsets removed; -1 when it was not drawn | the object pipeline (0x220DA) when the entity being drawn is `V:4AA78` |
| `V:4A9F2`, `V:4A9F4` | copy of the previous values (the cue's "target position") | 0x38FE8 at the end of the slew |
| `V:4A9FA`, `V:4A9FC` | the slewing marker (what the earlier text called the cue) | 0x38D8C |
| `V:4A9F6`, `V:4A9F8` | gun-sight and lock-circle centre (pipper) | 0x3878E, at the end of every 0x385F4 |

- **Target box, 0x38D8C (verified in the harness).** A rectangle in pen 10 with the corners (x-9,y-8), (x+9,y-8), (x+9,y+8), (x-9,y+8) around the target point `V:4A9EE/F0` (not the slewing marker), drawn as four blitter lines from the segment
  table at 0x39048; because the line routine puts horizontal lines one row lower, the lit pixels are columns x-9 to x+9 and rows y-7 to y+9 (19 by 17: for the target point (150,70) columns 141 to 159, rows 63 to 79).
  Each end point is clamped to x 86 to 232 and y 45 to 144 plus the view offsets (the HUD outline limits) and a segment wholly outside is skipped; the box is drawn only when the target point x plus the offset and y are above 0.
  When `E+0x56` (the pitch rate) is nonzero a jitter pair from the 8-pair table at 0x38FFC is added to the **target point** that the box is centred on, not to the slewing marker (read from the code: the routine loads the target point `V:4A9EE/F0` into its working pair, stores the marker `V:4A9FA/FC` first, and only then adds the jitter to the working pair, which is what the box corners are taken from; the marker and the gap it chases are not jittered), index (`E+0x16` and 0x1C) shifted right by 2: pairs (0,0), (0,1), (1,0), (1,1), (0,0), (0,-1), (-1,0), (-1,-1) (read only).
- **Marker slew, 0x38D8C body, four sub-steps per frame (the loop that draws the four sides; the first frame moves the marker from (159,91) to (155,87) for a target at (150,70), harness).**
  The marker is invalid (x = -1) when no weapon is selected (high nibble of `E+0x63` zero) or the target point x is at most 0; when the marker x is at most 0 and the target is valid the marker is set to (159,91). The **invalid** marker is stored as x = -1 with y = the marker's old x (the register D2: `0x38DAC`, `0x38ECA`), which a whole four-iteration pass turns into y = -1.
  Otherwise per axis: gap = marker minus target point; the shifts are (4, 2) normally and (2, 1) when `V:4AC08` bit 0x4000 (in the box) is set; if |gap| is above 15 the step is gap shifted right
  (arithmetic) by the first shift, else by the second; the step is clamped to at most 1 in magnitude (2 in the box); marker minus step. Because the shift floors, a marker to the right of or below the target by 1 to 3 does not move while one to the left or above moves (verified for one case, the rest is read only).
  The routine only **reads** the in-box bit 0x4000 of `V:4AC08` (`0x38DE8`); the in-box event bits (0x200 and 0x4000) are set by the lock state `0x38884` (note 07 12.7), not here. It runs four iterations, each reloading the target point and the marker; `V:4A866` skips only the marker update; it returns early, after one step, when the jittered box point is at or below 0 (note 07 12.7). At the end of a completed loop it copies `V:4A9EE/F0` to `V:4A9F2/F4` and sets `V:4A9EE` to -1.
- **Marker shapes, 0x38884 calling 0x3936E, pen 10, at the marker (verified in the harness).** Far from the target: the 12-point diamond of radius 3 (list 0x3948C: extreme points (+-3,0) and (0,+-3)), drawn only for x 89 to 229 and rows 40 to 140.
  When the marker is within 5 pixels of the target point in both axes (the lock box): the 44-point ring (list 0x394A6, measured at (159,91): x 148 to 169, that is -11 to +10, and rows 81 to 100, that is -10 to +9, because the reflection is not symmetric), drawn only on frames where (n and 3) is not 0 (it blinks), for x 97 to 221 and rows 47 to 133, final x clamped to 10 to 310.
  Both lists are signed byte pairs (dx, dy) added to the position (+y down), ended by the pair (0,0). Not drawn for the F-16 (class 0x10).
- **Lock marker, 0x385F4 (read only, shapes verified).** Runs only at zoom factor `V:4AAFA` = 0x80. With the gun (high nibble of `E+0x63` = 1): the pipper pixel at (`V:4A9F6`, `V:4A9F8`), the lock circle there in pen 10, and, with a target designated,
  the range dot and the range number (0x367DA); the fire prompt condition is: target point x above 0, |`V:4A9F2` - `V:4A9F6`| and |`V:4A9F4` - `V:4A9F8`| at most 8 and the range `E+0x4A` at most 0x900, on which the first time
  the event bit 4 of `V:4AC0C` is set, `V:4A96C` becomes 1 and 0x36930 is called; when it stops holding `V:4A96C` is cleared and bit 3 of `V:4AC0C` set. With a missile and a target designated (`V:4AA78` not negative): the circle centre is (159,91) while `V:4A9F2` is at most 0 and the stored pipper point
  otherwise; then the circle, the range dot (pen 13) and the range number follow. With no target nothing is drawn. (The earlier statement that the gun has no lock marker is withdrawn.)
- **Lock circle point list.** 0x39408 holds a quarter arc as signed byte pairs, 20 pairs and a terminator pair (-1,-1), 42 bytes, running from (0,12) to (14,0); samples (0,12), (5,11), (10,9), (14,0). The drawer 0x3905C reflects each pair four ways:
  (dx,-dy), (dx,dy-1), (-dx,dy-1), (-dx,-dy), giving 74 distinct pixels in x 145 to 173, rows 79 to 102 around (159,91) with the full count 0x50 (harness). The full arc is drawn when the centre lies in x 100 to 218 plus the view offset and rows 58 to 131. Outside that window the arc is drawn pixel by pixel through the range dot's clip (x 86 to 232 plus the offset, rows 46 to 143): the harness gives 67 pixels for the centres (99,91), (219,91), (159,57) and (159,132) and 63 for (98,91), and nothing for a centre at x 15 or 305 (`verify_addendum.py` asserts the full arc; the clipped counts were measured interactively).
- **Range dot, 0x392AE (verified).** A 2 by 2 pixel dot in pen 13 on the second arc: list 0x39432 (the first sub-list, 19 pairs from (0,11) to (13,0) and a terminator pair at 0x39458, 40 bytes; samples (0,11), (8,9), (13,0); the 50 bytes from 0x3945A to 0x3948C follow it, referenced nowhere: not 24 pairs plus a terminator but a second terminator pair at 0x3945A, 23 pairs from (0,14) to (16,0) at 0x3945C and a terminator at 0x3948A, asserted in `verify_addendum.py`).
  Position index k = (`E+0x4A` times 76) divided by 36000 as an unsigned 16-bit quotient (`E+0x4A` is the range to the target); the dot walks the arc clockwise from the top over 76 positions (19 per quadrant); k >= 76 draws nothing. Offsets from the centre, k = 0: x -1..0, rows -11..-10; k = 19: x 12..13, rows -1..0; k = 38: x -1..0, rows 10..11.
  The dot covers columns x-1..x and rows y..y+1, or x..x+1 when x mod 16 = 0 (checked at centres 150, 159, 160); clipped to x 86 to 232 and rows 46 to 143.
- **Fire prompt, 0x36930:** row 129, pen 13; the gun form is five characters at x 208, 212, 216, 220, 224 and the missile form five characters at x 204, 208, 216, 220, 224 (a gap at 212); the strings are the extractor's `prompts.bin`, not quoted here.
- No snapshot or capture has a target designated (`V:4AA78` = 0xFFFF in all), so none of this is compared with a capture; the shapes were drawn by the real routines in the harness with chosen variables.

## HUD (F-16, entity class 0x10): scale form

Selected by the class byte `E+0x62` = 0x10: 0x37E2C diverts to 0x37ED6 (the altitude scale) and 0x38090 to 0x38100 (the speed scale). Everything is pen 13; rows get `V:4A990` added and x the view offset `V:4AA40`. Tape, outline, crosshair, level marks and nose datum are as for the F/A-18.
**Settled in the addendum round: the model below reproduces the original routine pixel for pixel over 2,731 states** (speed 0 to 2,730 knots with all twelve remainders of the speed word, altitude to 27,300 feet in 5 foot steps, 600 G values; `tools/hud-emu/verify_f16.py`).

**Speed scale.** v = the absolute `E+0x6E` divided by 12 knots (0 when the hit flag is set). L = v modulo 100 chooses the centre label N (in units of 10 knots) and a displacement d:

| L | N | d |
| --- | --- | --- |
| 0 to 20 | (v / 100) x 10 | 0 |
| 21 to 70 | (v / 100) x 10 + 5 | -50 |
| 71 to 99 | ((v + 100) / 100) x 10 | -100 |

R0 = (L + d) / 3 truncated toward zero (-9 to +6). The centre row is y0 = 91 + R0 and the centre label's glyph top row 89 + R0: one row is 3 knots, and the labels step by 5 (50 knots) every 15 rows, which is why the stack stays continuous across the case boundaries.
Labels are three slots at x 92, 96, 100 holding the low three digits of N (leading zeros blank, the last digit always drawn); labels above the centre are N + 5k at glyph top 89 + R0 - 15k, drawn while the top row is at least 70; below the centre N - 5k at 89 + R0 + 15k, drawn while the top row is at most 109; the stack stops after a label of 0 has
been drawn (no negative labels); the centre label is always drawn. Ticks: one pixel at x 105 on rows y0 +/- 3k, upward while the row is at least 72, downward while at most 111; the centre row and every fifth tick (k a multiple of 5) also light x 104.
The fixed pointer is a line (107 to 111, 91) plus a chevron at (109,86), (110,86), (108,87), (108,88), (109,89), (110,89). The tick column is skipped when the shifted base x is not within 1 to 318, the pointer line when its shifted left end is at least 315 or at most 0.

**Altitude scale.** T = floor(5 x (`E+0x18` >> 10) / 10), the altitude in tens of feet; the same case split, d, R0 and stacking with T for v (30 feet per row, one label step 500 feet). Labels are three slots at x 215, 219, 225 holding the low three digits of N with **no** blanking, so N = 25 reads "02.5"; the decimal point is one pixel at x 223, four rows below the glyph top (y0 + 2 for the centre label).
Ticks: one pixel at x 212 on rows y0 +/- 3k (the same limits), the centre row and every fifth tick also light x 213. The pointer is the line (206 to 210, 91). Read from the code (not testable at zero offsets): the label point pixels are plotted by a routine that adds `V:4A990` a second time, so a nonzero `V:4A990` would move the points by twice the offset (the value is 0 in every state measured).

**G figure.** The F-16 takes the "class not 0x11" branch of 0x36828 with the same v as the F/A-18 readout: a sign slot at x 106 ('-' when v is at least 0, blank otherwise), the tens digit at x 110 (blank when zero), the units digit at x 117, glyph row 61, a single point pixel at (114, 65) and no letter "G".

**Other class effects (scan of the reads of `E+0x62` in hunks 36 to 40; a test through a copy of the byte would not show).** 0x36846: class 0x11 gets the F/A-18 G readout and every other class the F-16 figure. 0x37E6E and 0x38090: only class 0x10 gets the scales, so class 0x12 gets the numeric readouts with the F-16 style G figure. 0x38CE6: the HUD outline is drawn for classes 0x10, 0x11, 0x12.
0x354EE tests the high nibble of the weapon byte (same for all). 0x36E94 tests the target's class (message-line target data). The radar walk tests object classes. Nothing else on the panel changes for the F-16.

## Instrument panel

### Picture and overlays

- `pix/inst5` (320x55, five planes; use the low four bits of each index and **swap pens 9 and 11**, because
  the picture's palette has those two entries exchanged against the flight palette) is the clean panel at
  **(0, 145)**; the last row is 199. Verified: only the dynamic pixels differ from a captured frame.
- `pix/frnt5` (288x12) is the cowl, drawn **cookie-cut at (16, 133)**: pixels with pen 0 are transparent,
  all others replace the world. Verified: 1939 of 1953 non-zero pixels equal the capture (the rest are the
  HUD sides).
- Panel and cowl move together with the view offset (16 pixels per step); the area they uncover and the
  rows below the 3D window are black.

### Dynamic elements (pens and glyph pens are final pens; digits are 3x5 glyphs)

Each digit cell is 3x5; "fg/bg" gives the glyph pen and the pen of the other cell pixels (the box colour).

| element | position | value | format |
| --- | --- | --- | --- |
| altitude digits | glyph x 240, 244, 248, 254, 258, 262, y 158 | 5 times (`E+0x18` >> 10) feet | six slots right aligned, leading blank, fg 4 bg 0; "FT" is in the picture |
| speed digits | x 242, 248, 252, 256, y 166 | the absolute `E+0x6E` divided by 12 | four slots, fg 4 bg 0 |
| throttle digits | x 240, 244, 248, y 175 | the absolute byte `E+0x2B` times 256 divided by 307 (0x78 gives 100) | three slots, fg 4 bg 0 |
| magnification | x 242 and 248, y 184 | `V:4AAFA`: 0x80 shows "10", 0x40 shows "20", else "40" | the picture has the point and the magnification label |
| radar range | x 148 and 152, y 184 | low nibble of `E+0x63`: 9 gives 2, 0xB gives 10, anything else 40 | the picture has "MI" |
| fuel digits | x 104, 108, 112, 116, 120, y 183 | `E+0x72` long right shifted by 8 (pounds; full 24999) | five slots, fg 0 bg 8 (grey box) |
| fuel bar | x 111 to 114 (two 2-pixel plots at x 112 and 114), rows 159 to 180 (22 rows) | the routine walks 22 rows from row 180 upward and lights row i (0 at row 180) when `level - (i + 1)` is positive, so **lit rows = min(22, max(0, level - 1))**: the subtraction comes first and the 22 row loop is the cap (a full tank, level 24, lights 22 rows, rows 159 to 180; level 23 is already 22, level 22 is 21). level = (`V:4A9AE` with bit 15 removed) right shifted by 10, where `V:4A9AE` is the fuel digit routine's cache of the last drawn digit value (`E+0x72` >> 8, 0 to 24999, flag bit 15 as for the speed cache); the bar therefore follows the *drawn* digits, not `E+0x72`. Unlit rows are painted pen 0. The routine **draws nothing** when its first plot x (112 plus the view offset `V:4AA40`) is at most 0 or above 316, so in head codes 1 and 2 (offset -112, -224) the bar is not drawn and what shows is the clean picture, which already carries a full-looking bar of 19 lit rows (rows 162 to 180, x 111 to 114, shifted with the picture): that is the 19 rows seen after a head change, a rule and not a stale draw | pen 4 green, pen 0 empty; gate: with R > 0 the level is stored in `V:4AA5C` and drawn; otherwise nothing is drawn when the level equals `V:4AA5C`, and when it differs the bar is drawn, with the store only on frames where bit 0 of `n` is set |
| latitude | glyph x 295, 299, 305, y 180 | 375 plus the quotient of (`E+0x1C` - 0x10000000, right shifted by 8) divided by 0x7000, truncated toward zero (tenths of a degree, "NN.N"; the point is in the picture) | fg 0 bg 5 |
| longitude | x 292, 295, 299, 305, y 188 | 1220 plus the quotient of (`E+0x14` - 0x0F000000, right shifted by 8) divided by 0x5999, truncated toward zero | "NNN.N", fg 0 bg 5 |
| compass | window x 192 to 223, rows 151 to 157; red index x 208, rows 151 to 157 | see below | letters pen 2 on pen 3, index pen 1 |
| attitude ball | box x 192 to 223, rows 163 to 183 | see below | |
| EJECT lamp | box x 9 to 31, rows 157 to 164 | `V:4A863` nonzero (eject armed, toggled by the E key, raw 0x12); the key sets the timer `V:4A8FA` to 8 and each draw decrements it; the lamp is lit on the draws where bit 0 of the decremented value is 1 (7, 5, 3, 1: it blinks four times in 8 frames and ends unlit) | lit: every pixel of the box gets pen OR 1 (0 to 1, 4 to 5) |
| BRAKE lamp | box x 9 to 32, rows 169 to 175 | bit 0x800 of the word `E+0x00` | lit: pen OR 1 (8 to 9, 0 to 1) |
| ECM lamp | box x 292 to 309, rows 166 to 172 | `V:4A96D` nonzero (ECM on, toggled by the J key, raw 0x26, which also sets the redraw timer `V:4A8F8` to 3) | lit: pen OR 1 |
| GEAR lamp | box x 10 to 30, rows 182 to 189 | `V:4A8FE` positive (set to 1 by the aircraft code when the word `E+0x6C` is at least 0x1200 and bits 4 to 6 of `E+0x7C` are clear, cleared otherwise; the aircraft code also requests a warning tone) and bit 1 of the frame counter byte `V:4A993` (the low byte of `n`): lit on those frames, drawn every frame | lit: pen OR 1 (8 to 9: amber); it flashes |
| gear symbol | x 14 to 28, mask rows start at row 189 (the pen 0 pixels fall in rows 190 to 195 for variant 0, 190 to 194 for 1, 190 to 193 for 2, 189 to 192 for 3) | variant v = (`E+0x7C` with bit 7 cleared) shifted right by 5 (0 to 3); the mask is picked through a four-pointer table at 0x357F6 that lists the symbols **last first**: v = 0 draws the mask at 0x42B1C (gear down, the idle aircraft's capture), 1 the one at 0x42B00, 2 at 0x42AE4, 3 at 0x42AC8; that is mask index 3 - v counting 0x42AC8 as index 0, address 0x42B1C - 0x1C v. Verified (see "Addendum rules") | pen 0 glyph on the lamp; drawn through the lamp box mask when the symbol timer `V:4A8FD` is positive; 24 to 27 pixels per variant |
| afterburner line | x 241 to 250, row 181 | bit 3 of `E+0x03` set: pen 9; clear: pen 0 | counter `V:4A8F6` |
| threat lamps (0x35BF6), each 4 pixels wide and 2 rows tall | A x 292 to 295, rows 156 to 157; B x 297 to 300, rows 156 to 157; C x 297 to 300, rows 159 to 160; D x 292 to 295, rows 159 to 160; E x 302 to 305, rows 159 to 160 | A: `V:4A926` bit 2 (hostile in range) pen 1 else 3; B: bit 1 (friendly) pen 4 else 3; C: bit 4 (IR missile) pen 2 else 3; D: bit 5 (radar missile) pen 2 else 3; E: pen 1 when bit 2 of `E+0x20` (stall) and bit 1 of the frame counter, else 3 | the lamps are drawn every frame |

Of the counters `V:4A8F1` to `V:4A8FE` (`V:4A8F9` is unused, see above), only some are pure redraw timers (the draw routine runs while they are positive and the picture keeps
the result in between: `V:4A8F1` to `V:4A8F4`, `V:4A8F5`, `V:4A8F6`, `V:4A8F7`, `V:4A8FB`, `V:4A8FC`, `V:4A8FD`, and `V:4A8F8` for the ECM lamp).
A replica that redraws those elements every frame from their state need not reproduce these timers. Two carry displayed state and must be reproduced:
`V:4A8FA` (the EJECT countdown, set to 8 by the E key, whose bit 0 decides each blink phase) and `V:4A8FE` (the GEAR lamp state, set to 1 or cleared by the
aircraft code). The ECM timer `V:4A8F8` (set to 3 by the J key) only gates the redraw; the lamp itself follows `V:4A96D`.

**Compass.** Degrees d = `E+0x68` divided by 80 (integer). Scroll = ((d divided by 2) times 135 right shifted
by 8) minus 4, plus 96 when negative: a number from 0 to 95 stored in `V:4A97C`. Window column j (0 to 31, screen x 192 + j) shows the
strip pixel in column (j + floor(scroll / 2)) modulo 96 of the one-plane strip at 0x42BAC (7 rows of 12 bytes, most significant bit
leftmost; the 96 columns hold the 48-pixel pattern twice: the letters N, E, S, W every 12 pixels with a dot between them, rows 1 to 5
of the 7); a set strip pixel is pen 2, a clear one pen 3, then the red index (pen 1) is drawn at x 208 over rows 151 to 157. The
window rows are 151 to 157. Measured against the running routine at 0, 10, 45, 90, 180, 270 and 359 degrees: at most 3 differing
pixels of 224 (the index column and the window edge). Measured scrolls: 0 degrees gives 92, 45 gives 7, 90 gives 19, 180 gives 43,
270 gives 67, 359 gives 90.

**Attitude ball.** The picture's ball is covered by a pen 1 (red) ellipse and then the **sky polygon** of the
3D window (the polygon the world filled with the sky colour: its clipped vertices in 320x179 window
coordinates, held at 0x504DE as a count followed by (x, y) words) is drawn in pen 5, scaled: x' = (x times
24 right shifted by 8) plus 193, y' = (y times 31 right shifted by 8) plus 162, plus the view offsets. **A scaled row is sky when
it is at or above y'** (the horizon row inclusive): for level flight on the ground the polygon is the rectangle (0,89), (319,89),
(319,0), (0,0), 89 scales to 172, so rows 163 to 172 are sky and rows 173 to 183 ground (measured; a polygon down to y 120 gives sky down
to row 176); in a 30 degree dive the lower edge is at row 142. The polygon is filled by the blitter's area fill and clipped to the ball
mask (21 rows):
rows 163 to 183 span x 206 to 212, 204 to 214, 202 to 216, 201 to 217, 200 to 218, 199 to 219 (two rows),
198 to 220 (six rows from row 170 to 175), 199 to 219 (two rows), 200 to 218 (two rows), 201 to 217,
202 to 216, 204 to 214, 206 to 212. Drawn after the fill, in this order: the fixed aircraft symbol (pen 2:
row 173 x 204 to 214, and x 209 rows 171 and 172) and eight pen 13 bank dots at (206,165), (204,166),
(203,167), (201,170), (218,171), (218,172), (218,173), (217,175). The ball shows only the sky polygon, so
the roll shown is the horizon of the 3D view. The mask at 0x42B38 (21 rows of 2 words) is the ellipse above; the second table at
0x42B8C (8 rows of 4 bytes) is read by the edge pass at 0x34FDA, which runs on every call, but no visible effect was found in the
tests (every ball in the tests, 163 to 183 rows, is reproduced by the ellipse and the fill alone): its role is open (question 5).

### Radar scope (0x35CF2) and left display

- Scope window: the black box x 129 to 188, rows 146 to 192 in the picture. Objects come from a list of **16-byte entries** rebuilt each redraw by 0x29D3A,
  whose only caller is the list builder at 0x1FF24 (entities 1 to 15, skipping the player and records without bit 6 of byte `+1`). An entry is three longs (rotated x, 0,
  rotated z), a flag word (bits 15 to 8 the entity index, so the record is at (flags and 0xFF00) times 2; bit 4 "plot me"; bit 5 is set by the scope once the blip was drawn) and two unused bytes.
  The longs are the position relative to the player rotated by the player's heading matrix (`V:4ACB4`, 256 = 1.0), which is roughly the relative position divided by 256, then pre-scaled left by an even
  shift k (0, 2, 4, ...: the smallest that brings the largest relative component to at most 0x7FFFFF), so the scope is heading-up (ahead is up).
  **Blip position (verified by sweeping range nibbles 9, 11, 12, 13 and 3 with four vectors each, off-scope cases included).** The shift is the **low nibble of `E+0x63` exactly** (13 for the 40 mile
  setting, 11 for 10, 9 for 2), not 13 plus the nibble: each long is shifted right arithmetically by the nibble and rounded half up (one is added when the last bit shifted out was set); then x = 158 minus
  the first value, y = 167 minus the second; the blip is kept when x - 158 is within -27 to 27 and y - 167 within -16 to 21. (The value 13 of the earlier text belongs to the threat test, which compares the longs shifted by 13
  against +-0x10000.) A blip is one pixel, or two pixels wide (x - 1 and x) when the object's long at `+0x10` (its y shifted right by 8) is at least `V:4AB30` negated (the camera y shifted right by 8,
  negated, eye offset included): verified in the harness, a value of 12 against -13 draws one pixel, 13 and 14 draw two. At most 10 blips are drawn per frame; each is recorded in a per-page erase list (0x537C8,
  page `V:4A724`, 0x28 bytes per page) and the next draw of that page erases the old blips with pen 0 before drawing the new ones. The designated
  target's blip is drawn only when bit 0 of `V:4A93B` is set: that byte is a radar frame counter (incremented once per radar draw
  and by 0x2FAD6), so it is drawn on alternate frames. Pens: 5 for classes 0x2x and 0x3x (ships, subs, pilot); otherwise 8 when bit 6 of the object's
  byte `+0x20` is clear or bit 1 is set (not detected or hidden), else 4 when bit 3 of the object's byte `+1` is set
  (friendly), else 2 when the class high nibble is 0 (missile), else 1 (hostile aircraft).
  **Own-ship mark and cone lines (verified in the harness).** The routine also draws, in pen 12, a plus of four pixels: (158,167) and (157 to 159,168); it is drawn only when the redraw counter `V:4A8EF` is nonzero
  or when (n and 14) = 8 (frame counter values 8 and 9 of every 16), is never erased except by a blip's pen-0 erase that lands on it, and persists in the bitmap until the next repaint, so it is
  visible in every frame except under a blip. Two pen 3 lines, (154,163) to (139,147) and (162,163) to (177,147) (the edges of the forward cone), are drawn when (n and 14) = 0 or when the redraw counter
  is nonzero (a value above 1 also empties the erase lists first; the lit pixels run from (154,163) to (139,148) and from (162,163) to (177,148), 16 each, because the line routine puts the end point one row lower); pen 3 is black on the reference emulator (see "Pen 3 and the emulator's colour curve"), so the captures do not show them, and a port that converts pen 3 linearly shows them as faint grey lines. The routine also
  raises the threat bits in `V:4A925` and `V:4A926` that feed the warning arbiter (see "Warning line").
- Left display: weapon text (0x3736C): "ARM" at glyph x 61, 65, 69 on row 176 when a weapon is selected, and a
  second line on row 184: "GUN" at 61, 65, 69, or "SW" or "AM" at 65 and 69; pen 4. Weapon stations (0x354D4): six stroke
  boxes in the picture, each 3 wide and 6 tall with the corner (x - 1, y + 1) for the station point (x, y): Sidewinder stations
  (85,172) first and (51,172) second (boxes x 84 to 86 and 50 to 52, rows 173 to 178); AMRAAM stations (74,159), (62,159), (81,167),
  (55,167) in that order (boxes x 73 to 75 and 61 to 63 at rows 160 to 165, x 80 to 82 and 54 to 56 at rows 168 to 173). Station i
  of a type keeps its picture stroke (pen 4) while i is below the stock of that type (the nibble of `E+0x5F`: high for Sidewinder, low
  for AMRAAM) and is repainted pen 0 otherwise; for the selected type the last station still stocked is repainted pen 1 (measured:
  stocks 2 and 2 with Sidewinder selected leave station 2 red, with AMRAAM selected station 2 of the AMRAAM row red). A centre marker
  at x 68: with the gun selected rows 163 and 164 are pen 1, otherwise row 163 is pen 0 and row 164 pen 4. The strokes are drawn only by
  erasing or recolouring, so a replica draws the picture and then applies these repaints.

### Message line (0x36DB2, 0x3706A)

Row 192 (glyph rows 192 to 196), **26 characters from x 108 at a pitch of 4 pixels** (last at x 208), on the
black window x 100 to 215, rows 190 to 197. Class from bits 7 and 6 of the entry's first byte: 2 draws the text in
pen 1 (alerts), 1 draws it in pen 4 (information); the cell background is black. The text redraws when the
index changes and persists on screen until another message or the blank message replaces it; the driver
keeps a two-frame counter for the repaint. Which entry is current is decided by the arbiter on the ticks, not by the driver: an alert alternates between its own text and its partner entry N + 1, or the blank entry for the codes 0xC0xx and 0xD0xx, and an alert whose duration is not 0 ends as steady plain text (see "Warning line").

## Warning line (hunk 59 table and the arbiter)

The table at 0x41FF0 has 54 slots of 28 bytes (indices 0 to 53). Byte 0 is the flag byte, bytes 1 to 26 the
26 characters (space padded), byte 27 a one-byte flag (1 only for index 20, used as a sound request via
`V:4A91B`). Flag byte: bits 7 to 6 = class (2 alert red, 1 information green; the value 0x40 blank), bits 5 to 0
= duration in ticks (see below). The line shows entry `V:4AB96` (its low byte) when it differs from the shown
one; the arbiter sets `V:4A91A` and `V:4A91B` from the entry's bytes.

**Code word** (`V:4AB98`, set by the arbiter or by the post routines): low byte = entry index; bit 15 = alert
(the arbiter produces it for as long as the condition holds; the line alternates between the entry's text and its partner or blank on the ticks of the code's kind, and an entry whose duration D is not 0 expires after D + 1 text phases into steady plain text; see "Ticks and blinking" and "Durations of alerts"); bit 14 = timed information; bit 13 (0x2000) = set when first shown (the duration
counter has been loaded); bit 12 = fast countdown and fast blink; **bits 8 to 11 are not decoded** (the post routine only compares
the masked value with 0x4000 and 0x4800, the blank codes, to clear the "shown" bit). A code with neither bit 15 nor bit 14 (the
arbiter's fuel-low code 0x0C09 is one, its bits 8 and 9 are irrelevant) is shown plainly: it has no duration and no blink and
stays while the arbiter keeps producing it.

**Ticks and blinking (settled in the addendum round by running the generator 0x13C94, the arbiter 0x13D4C and the driver 0x36DB2 frame by frame, `tools/hud-emu/verify_warn.py`).**
**Scope.** The generator runs once per main-loop pass, at the start of the frame routine (0x11124, called from the main loop at 0x17EF2), **before** the test of the in-flight flag `V:4A84D` at 0x11140: it runs on every pass, in flight or not and during a pause. The arbiter (0x13D4C, called from 0x1127C) is on the flight path only, so it runs only on passes where `V:4A84D` is nonzero (the idle path that
the test takes at 0x11146 goes to 0x114C0 and does not reach it); the driver runs as described under "External views and pause". The code locations are asserted in `verify_warn.py`.
Its counter `V:4A948` is 0 in the image; per pass it is decremented and reloaded to 8 when it reaches 0. The tick word `V:4A946` is then set from the counter value after the decrement, and the arbiter clears it at
its end, so a tick lives for one frame:

| counter after the decrement | `V:4A946` | ticks |
| --- | --- | --- |
| 8 (the reload frame), 4 | 0x86 and 0x06 | slow (bit 1) and fast (bit 2) |
| 6, 2 | 0x04 | fast only |
| 7, 5, 3, 1 | 0 | none |

So a slow tick falls every 4 frames and a fast tick every 2. **Phase at the first flight frame.** In the original the counter is a free-running count of generator passes since boot (modulo 8). It also advances on the menu passes between flights and during a pause, so its phase at the start of a flight depends on how long the menus ran: **it is undefined, not a constant of the game.**
The frame counter `n` (`V:4A992`) does not drive it. `n` counts flight passes only: its single increment (read 0x114B0, add one at 0x114B6, write 0x114B8) is on the flight path after the pause test at 0x114A6. `n` is cleared at mission start (0x12EC0, before the first flight pass; verified: the code that clears it is the mission-start routine, which then waits three state ticks and enters the flight state 0x12EFE) and in a second state routine at 0x138AC (the clear is at 0x138B6; traced from the code in the residual round of note 05: it waits for the countdown `V:4AB8E` to go negative, then clears `n` and enters the flight state 0x12EFE; **no reference to 0x138AC was found anywhere in the image** (no absolute address, no pc-relative load, no branch), so it is probably unused or reached by a computed jump, and it is not on the crash-restart path, whose states are 0x12FB8 to 0x138D8 to 0x13980 to 0x139C2 and never clear `n`; the logs agree: `n` ran 0x1A0 to 0x1A1 in `stall2.txt` and 419 to 420 in `crash-hold.txt` across the restart), and **the code at 0x1EDF8 that sets `V:4A866` and clears the low three bits of `n` is unreachable** (no reference to it exists; the pause key's handler is 0x1ED9A and does neither; emulator: `n` stands still during a pause and keeps its low bits, "View kinds, settled" item 8). Note 05 section 15 does not list `V:4A992`: the aircraft start routines there leave it alone, and the clear is in the mission-start code. So `n` is small in the snapshots (11 to 432), not large: it is the number of flight passes since the clear.
In the reference path the counter after the generator pass in which `n` has the pre-increment value m equals 8 - ((m + 3) mod 8): this holds in 37 of the 38 flight snapshots (a snapshot is taken after the increment, so its `n` is m + 1); the carrier snapshot is off by one (a pause earlier in its session would break the relation, inference).

**Port rule (a reference-path pin, not a verified game constant).** Under the replica directive the port pins the undefined phase to the reference path. The port starts `n` at 0 at mission start (the original's clear at 0x12EC0; n0 = 0 on the first flight pass of a mission), **not** at the crash restart of note 05 section 15, which keeps `n` (log evidence above) and, as the phase relation is independent of `n`, does not reseed the tick counter either. Before the first generator pass of a flight it seeds `V:4A948` so that
after that pass the counter is 8 - ((n0 + 3) mod 8), that is **5 for n0 = 0** (the seed is that value plus one; the value 8 is reached from the seed 1). From then on the generator runs free, once per pass, **independent of `n`**, through a pause and through passes where the flight is not running; the port does not reseed on a pause or a restart (the original never does). The sequence from n0 = 0 is counters 5, 4, 3, 2, 1, 8, 7, ... with tick words 0, 0x06, 0, 0x04, 0, 0x86, 0, 0x04, ... (the first slow and fast tick on the second pass).
`verify_warn.py` reproduces the seeded sequence for n0 = 0 to 7, also with `n`, the pause byte and `V:4A84D` varied between passes, and asserts the relation on every flight snapshot. A "tick" for a code is a fast tick if the code has bit 12 (0xD0xx) and a slow tick otherwise.

*Alerts (bit 15).* The arbiter holds the code while its condition holds. Each tick of the code's kind is one phase: on a tick the arbiter either shows the entry's own text (and sets bit 5 of `V:4A986`, the "text shown" flag, and counts
the duration down) or, when that flag was set, clears it and shows the partner (entry N + 1 when the code is not also timed: 2 to 3, 6 to 7, 14 to 15, 16 to 17, 18 to 19, 20 to 21, 36 to 37) or the blank entry (when the code also has bit 14, the codes 0xC0xx and 0xD0xx: stall, fuel
exhausted, entries 10, 11, 12 and 42). So a slow alert shows text for 4 frames, partner or blank for 4 frames, and so on; a fast one 2 and 2. A change of the entry index does not reset the flag: the old line stays until the next tick of the new code's kind and, if the old alert was in its text
phase, that first tick shows the partner or blank instead of the text (verified with two switch scenarios).

*Durations of alerts.* **The earlier statement that alerts are held for as long as the condition holds is wrong for entries whose duration D is not 0.** On a change of the low byte of the code against `V:4AB9A` the arbiter installs the code, clears bit 13 of `V:4A986` and bit 2 of `V:4A984`, loads the entry's flag bytes and the
counter `V:4A94A` = D. The counter is decremented on each *text-phase* tick (the tick that shows the entry's own text, not the partner or blank tick) and the alert expires when it goes below 0, that is after D + 1 text phases. At expiry
the code word becomes the bare entry index (no flag bits), `V:4A984` bit 0 and `V:4A986` bit 13 are cleared, and the line shows the plain entry text, steady and not blinking, even though the condition still holds; the alert restarts only after the arbiter's choice has
changed to another entry and back. D is the low six bits of the entry's flag byte in the warning table on the disk (byte 0 of each 28-byte entry; the table is disk data and is not listed here). The 54 durations take the values 0, 1, 2, 4, 5, 6 and 8: 29 entries have 0 (alerts with 0 never expire and blink while the condition holds), 12 have 8, 4 have 4 and 4 have 5, 3 have 6, 1 has 2 and 1 has 1. The entries a rule uses are named where it uses them (the fuel-critical alert has 6). FNV-1a-32 of the 54 duration values (the flag byte & 0x3F, entries 0 to 53 in order, one byte each) is `cfc9b490`. The shipped table is what the extractor writes to `cockpit/warnings.bin`.

Example (verified): the fuel-critical alert, entry 2, D = 6, blinks four frames on and four off for six cycles (the code is held for 49 passes in the harness run) and then, on the seventh text-phase tick, becomes the bare index and stays steady. When the condition ends the code goes to 0 and the line is blank on that same frame.

*Timed information (bit 14 only, e.g. 0x400D).* It shows at once, without waiting for a tick, and counts D down on **slow ticks only** (bit 1; never bit 2, also for information codes with bit 12) from the first slow tick after loading; it lasts D + 1 slow ticks (D = 8: the entry is current for 32 to 35 passes, counted from the load pass, depending on how many passes the next slow tick is away: 32 when the load pass itself carries a slow tick, 35 when the next one is three passes later; D = 0: it expires on the first slow tick, so a posted entry 29 is current for 0 to 3 passes, 0 when the load pass carries the tick;
the earlier 33 and 1 were the case of a slow tick one pass after the load; asserted for all eight phases in `verify_warn.py`). **At expiry the line is blank (entry 0)**: the arbiter sets the code word, `V:4AB96` and `V:4AB98` to 0 but leaves `V:4AB9A` at the old index; since the next comparison finds the low bytes equal nothing reloads while the condition
holds, so the line stays blank until the arbiter's choice changes (all clear sets `V:4AB9A` to 0, so a later return restarts the message). Plain codes (neither bit 14 nor bit 15, e.g. 0x0C09) show steadily with no counter. Verified.

*Posted codes.* A post sets `V:4A984` bit 0 ("latched"), which the arbiter keeps until the code word is 0; a posted code replaces an arbiter alert on the same frame and is not overridden afterwards. A posted alert with D = 0 (0xD00A, entry 10) blinks fast forever; only a posted
0x4800, or a timed code that runs out, clears the latch (verified).

*Text versus code word.* The driver copies the entry's text into its buffer in the same pass in which `V:4AB96` changes and repaints it for two more frames through `V:4A8F4`; so in state the text does not lag the code word. (The one frame lag seen against the logged state is the page flip, as for every other readout. The earlier "text switches on the frame after the code word changes" is withdrawn.)

*External views and pause.* The generator runs on every main-loop pass and the arbiter on every pass where `V:4A84D` is nonzero (see "Scope" above), in every view; the driver 0x36DB2 is called every frame in cockpit views and in the external views only when `V:4A95E` is 2 (0x1141C). `V:4A95E` is 1 in every flight snapshot, cockpit and external, and 3 in the demo, so in normal play **the warning line is not drawn in the external
views**; the value 2 is a mode byte tested by the training prompt posters (0x27262 to 0x27446), probably training mode (inference; its writers were not traced). `V:4A866` (the pause-like byte) stops neither generator, arbiter nor driver; it only keeps the frame counter `n` from advancing (0x114A6 is the pause test before the update: read 0x114B0, increment 0x114B6, write 0x114B8), and the arbiter reads it once: a nonzero value turns an alert's
blank-or-partner tick into a text tick. So during a pause an alert shows its steady text, its duration still counts down on each tick and it expires as above (verified in the harness). When a target is designated (`V:4AA78` not negative) and bits 0 and 7 of `V:4A984` are both clear the driver shows target data instead of the warning line
(0x36DC2; arbiter conditions 6, 7 and 9 clear bit 7, so those leave the line to the target data; read only).

**Arbiter** (0x13D4C), evaluated every frame, first match wins:

1. While a message is already latched (`V:4A984` bit 0), keep it (the arbiter does not run its list).
2. Bit 15 of `V:4A980` set: entry 6 (crash imminent), code 0x9C06. No writer of `V:4A980` or of `V:4A98E` exists in the image as
   an absolute store (they are written through indexed or word-pair stores that were not located); both read 0 in every snapshot, and
   note 05 does not list them either: open question 2.
3. Bit 5 of `V:4A98E` set: code 0x4000 (blank line); the same bit selects the alternate duration byte `V:4AC2C` and is cleared when a
   message expires.
4. Otherwise, when `V:4AC08` or `V:4A926` is nonzero, `V:4A984` bit 7 is set and the first of these that is set is chosen:

| order | condition | entry (paraphrase) | code |
| --- | --- | --- | --- |
| 1 | `V:4A926` bit 4 (IR missile locked on the player) | 16, IR missile alert | 0x8010 |
| 2 | `V:4A926` bit 5 (radar-homing missile) | 18, radar-missile alert | 0x8012 |
| 3 | low byte of `V:4AC08` bit 6 (fuel exhausted: `E+0x72` at or below 0) | 4, fuel exhausted | 0xC004 |
| 4 | low byte bit 7 (stall flag, bit 2 of `E+0x20`, airborne) | 1, stall | 0xC001 |
| 5 | low byte bit 1 (fuel below 0x70800, that is 1800 on the digits) | 2, fuel critical | 0x8002 |
| 6 | `V:4A926` bit 3 (cruise missile in range) | 36, cruise-missile alert | 0x8024 |
| 7 | `V:4A926` bit 2 (hostile aircraft in range) | 14, enemy nearby | 0x800E |
| 8 | low byte bit 0 (fuel below 0xBB800, that is 3000 on the digits) | 9, fuel low | 0x0C09 |
| 9 | `V:4A926` bit 1 (friendly aircraft in range) | 13, friendly nearby | 0x400D |

   If one of the two words is nonzero but none of the nine conditions holds (a bit the list does not know), the code word is left as it
   was. **All clear:** when both `V:4AC08` and `V:4A926` are zero, `V:4A984` bit 7 is cleared and, unless `V:4A984` bit 2 is set, the code
   word is set to 0 (blank line). The `V:4A926` bits (threat flags) and `V:4AC08` are set and cleared by the radar walk and by the fuel
   and stall code (note 05); when a condition ends its flag is cleared there and the next arbiter run blanks the line.

   The threat bits come from the radar walk (an object within 0x10000 in each axis; class 0 objects are missiles
   whose target field equals the player: bit 4 for entry class 0, else bit 5; class 0x15 sets bit 3; aircraft
   class 0x1x sets bit 2 when hostile and bit 1 when friendly).

Messages posted directly (code word, from the caller):

| entry (paraphrase) | code | posted by |
| --- | --- | --- |
| 5 refuelled and rearmed | 0x4005 | 0x1F124 |
| 10 collision proximity | 0xD00A | 0x2A48A |
| 11 hydraulic damage | 0xD00B | 0x2A8A0 |
| 12 structure damage | 0xD00C | 0x2A8A0 |
| 22 target destroyed | 0x4016 | 0x2850E |
| 31 not ready to reposition | 0x401F | 0x1E8DA |
| 33 destruct immune | 0x4021 | 0x12FD4 |
| 35 hook down | 0x4023 | 0x1E380 |
| 38 flare fired / 39 no flares | 0x4026 / 0x4027 | 0x1EE2A / 0x1EE42 |
| 40 chaff dispensed / 41 no chaff | 0x4028 / 0x4029 | 0x1EEB8 / 0x1EED0 |
| 42 eject sequence | 0xD02A | 0x2A456 |
| 29, 30 training pattern exhausted / end | 0x1D, 0x1E | 0x273AA, 0x273B6 |
| training prompts and names | from the 8-byte records at 0x2749A | 0x2712E, 0x27358, 0x27492 |
| blank (clear) | 0x4800 | 0x28318 |

Training records (prompt entry, maneuver name entry): seven 8-byte records at 0x2749A, one per maneuver of the flight school; the prompt entries are the seven consecutive entries 46 to 52 in a permuted order, the name entry of each lies in 23 to 28 (six maneuvers) or is the entry 43 (the vertical half-loop); the pairs are disk data and are not listed here (FNV-1a-32 of the 14 index bytes, prompt then name, in the order aileron roll, break turn, inside loop, split-S, vertical half-loop, barrel roll, inverted flight: `ccc4891c`; the bytes are those of `tables/training.bin` table A rows 1..7, prompt then name, in record order, which is that lesson order). Entries 3, 7, 15, 17, 19, 21 and 37 are the blink partners of 2, 6, 14, 16, 18, 20 and 36 (see above). Entry 20 is posted at `0x2A87C` (the first-hit engine failure `0x9014`), entry 53 at `0x27480` (training prompt); entries 8, 32 and 34 are posted nowhere (note 07 12.9).

All 54 flag bytes: the class is the top two bits (2 for the alerts, 0x80 to 0x88, 24 entries; 1 for the information entries, 0x40 to 0x48, 30 entries). The duration is the low six bits and is described in "Ticks and blinking". The column is disk data and is not listed here; FNV-1a-32 of the 54 flag bytes (byte 0 of each entry, entries 0 to 53 in order) is `84b20d10`; the class entries each rule uses are named where the rule uses them (the partner pairs, the posted codes).

## Head views

Cockpit view codes `V:4A85F` 0 to 13 (the key handlers 0x1E624 to 0x1E724, the key dispatch 0x1DAA0; tags: V = verified in the rig or the reference emulator, R = read from the code only).

**Keys in the cockpit** (the key handler runs with the camera mode `V:4A966` = 0; in the tracking camera modes the view keys are ignored except KP-Enter in mode 6 and ESC; rig `view_check.py` keys: 112 head-key presses over all 14 codes, 0 differences; emulator `views-m3-heads.txt`; each row carries its own tag, V = run in the rig or the emulator, R = read from the code only):

| key (raw) | code after the press |
| --- | --- |
| KP8 (0x3E), KP9 (0x3F) | 0 (KP9 reaches the same code path as KP8: **KP9 gives code 0**, not a second forward view) [V] |
| KP2 (0x1E) | 6 [V] |
| KP7 (0x3D), KP1 (0x1D) | 12, 13 [V] |
| KP4 (0x2D) | code - 1; from 0 it gives 11; a result of 0, 11 or more (that is, from 1, 12 or 13) gives 0 [V] |
| KP6 (0x2F) | code + 1; a result above 11 (from 11, 12 or 13) gives 0 [V] |
| KP5 (0x2E) | no code change and **no list request**: it only sets the hold byte below to 0xFF (a lock of the present view) [V rig; the lock is R: no capture holds KP5 for longer than the timer] |
| KP3 (0x1F), KP-dot (0x3C) | no cockpit code change; they store 3 and 9 in the external direction `V:4A96A`, set the event echo `V:4A968` = 0xFF and request a list rebuild (0xFF) like every external ring key; the hold byte is not touched [V: emulator `views-m3-fix.txt` S1, cockpit-ground snapshot: after KP3 `V:4A96A` = 3, `V:4A910` = 0xFF (then 0xFC, 0 as the builder consumes it), `V:4A968` = 0xFF, `V:4A83D` and `V:4A85F` still 0; KP-dot the same with 9] |
| KP0 | no handler (the dispatch compares no raw code 0x0F) [R] |

Every head key (all but KP5) writes the list request `V:4A910` = 0xFF (0x1E714), sets the **hold byte `V:4A949` to 0xFF** and the HUD offset and window row of the code (table below). **Head-hold timer** [V, emulator `views-m3-hold.txt`: `V:4A949` and the seconds word every 0.5 s after a KP6 press and release: 4, 4, 3, 3, 2, 2, 1, 1, 1, then 255 with code 0 from the sample at which the seconds word had changed five times since the release; and `views-m3-fix.txt` S6: a sample every 5 video frames, 160 samples: the byte went 255, 5 (release), then fell by one in exactly the sample in which the seconds word changed (samples 4, 14, 24, 35: 4, 3, 2, 1), and at the sample of the fifth change (43) it was 255 with code 0]: the byte is a signed counter: negative means hold. The key **release** of KP2, KP4, KP6, KP7 and KP1 (raw 0x9E, 0xAD, 0xAF, 0xBD, 0x9D) sets it to 5; the group of per-second counters `V:4A93D`, `V:4A93E`, `V:4A949`, `V:4A942` falls by one (0x29882, each a byte that is **not decremented when negative**, so 0 becomes 0xFF, a hold) whenever the **timer-device seconds long `V:4ABAA` differs from the value kept in `V:4ABBA`**. **The phase rule** [V; the code is the routine 0x297D6, called at 0x11446 inside the world part of the frame, so only in a **drawn** frame, after the state routine 0x141E8 of the same frame]: once per drawn frame the routine refreshes `V:4ABAA` (the seconds of the system clock, 0x18E54) and compares; any change of the long (not only +1) is a second; the decrement happens in that frame and nowhere else; the phase of the clock relative to the key release is arbitrary (the clock runs on, not restarted by the key), so the first decrement comes between 0 and 1 s after the release. In the cockpit view the state routine 0x141E8 of the **next** drawn frame, finding the byte 0, applies code 0 by the KP8 path (which stores 0xFF again, so it happens once); that path runs through 0x1E706, so **the recentre also resets the zoom word `V:4AAFA` to 0x80, sets bit 7 of `V:4A895` and `V:4A8F5` = 3, and requests the list rebuild** [V: rig `view_probe.py round3`: hold 0, code 5, zoom 0x40 give code 0, hold 0xFF, zoom 0x80, request 0xFF]. A head view therefore lasts 4 to 5 real seconds after the release plus one frame, at any frame rate; in the emulator the system clock advances 1 s per 50 video frames. **The pause and the other undrawn frames do not count** [V: emulator `views-m3-fix.txt` S4b: KP6 in a pause, then 10 s of pause: the byte stayed 5 and `V:4ABAA` was not refreshed at all (it is read only by the drawn-frame routine); after the second P `V:4ABAA` had jumped by the 13 s the pause lasted and the byte was 3 one second later (the jump counted as one change, then one real second), then 2, 1, and 255 with code 0 on the following seconds]: the frame routine skips the whole world part when the gate `V:4A84D` is 0 (a pause, the hold screens), and with it the routine 0x297D6. KP8, KP9 and KP5 leave it at 0xFF (KP8 and KP9 show code 0, so nothing changes; KP5 locks the present head view until another head key is pressed and released). The zoom keys `[` and `]` also set it to 0xFF (and their release to 5). The ejection seat's camera (class 0x30) has its own table (below) and the same codes.

| code | HUD step (`V:4AA3E`) | pixel offset | window last row | head yaw (word table 0x3239A, 1/80 degree; a rotation of the head, see below) | HUD, panel visible |
| --- | --- | --- | --- | --- | --- |
| 0 | 0 | 0 | 144 | 0 | HUD, cowl and panel |
| 1 | -u | -u x 16 | 144 | 2256 (28.2 degrees right) | HUD right part, panel shifted left |
| 2 | -2u | -2u x 16 | 144 | 4800 (60) | HUD edge only, panel right part |
| 3 | -3u (the table byte; H when the present row is already 167) | -3u x 16 (H x 16) | 167 | 7200 (90) | none |
| 4 | -H (the table byte; +H when the present row is already 167) | -H x 16 (H x 16) | 167 | 9600 (120) | none |
| 5 to 8 | H | H x 16 | 179 (5 to 7), 167 (8) | 12000, 14400, 16800, 19200 (150 to 240) | none |
| 9 | +3u (the table byte; H when the present row is already 167) | +3u x 16 (H x 16) | 167 | 21600 (270) | none |
| 10 | +2u | +2u x 16 | 144 | 24000 (300) | HUD left part and panel left part at the right |
| 11 | +u | +u x 16 | 144 | 26544 (331.8, that is 28.2 degrees left) | HUD, panel shifted right |
| 12 (KP7) | H | H x 16 | 167 | none: a pitch triple 0x5690 (277 degrees) or 0x5460 (270) | none |
| 13 (KP1) | H | H x 16 | 167 | none: a pitch triple 0x19F0 (83 degrees) or 0x1C20 (90) | none |

The step column is the byte table at 0x1E800 (14 signed bytes for the codes 0 to 13: a symmetric ramp of three sizes of the step u on the codes 0 to 3 (down from the straight-ahead code) and 9 to 11 (up), and a hold H, the hidden marker, on the codes 5 to 8, 12 and 13; the byte of code 4 is **-H**, the earlier constants row that said H for code 4 was wrong; the values are disk data and are not listed here; the bytes are `tables/head_step.bin`, FNV-1a-32 `1df64ecf`, which the extractor exports and the port reads; the pixel offset is the step times 16) **except** that the routine takes H (offset 800) instead **only for the codes that select a window row of 167 or 179**, namely 3 to 9, 12 and 13, and **only when that row equals the present window row `V:4AA3C`** (the test at 0x1E774 compares the row the new code selects, 167 for 3, 4, 8, 9, 12 and 13, 179 for 5 to 7, with `V:4AA3C`); the codes 0, 1, 2, 10 and 11 (row 144) never take the test and always use the table byte (c-KP6-1: code 1 from code 0 gives -u, whatever the present row). Code 4 therefore reads **+H from code 3** (snapshot c-KP6-4) and from codes 8, 9, 12 and 13 (row 167), and **-H (offset -16 H) from codes 0, 1, 2, 5 to 7, 10 and 11**; code 3 reads H from 4, 8, 9, 12 and 13 and -3u from the rest; code 9 reads H from the row-167 codes and +3u from the rest. The pixel offset is the step times 16 (800 for 50). In every one of the equal-row cases the HUD is off the screen, so the difference is not visible; the rule for the port is the one stated here, which a port may replace by "codes 1, 2, 10, 11 use the table steps, the others hide the HUD" without a visible difference [V: rig `view_probe.py step`, every head key from each of the 14 codes with each present row 144, 167, 179, 252 cases, 0 differences, step and offset; V for the table values on c-KP6-1 to c-KP6-4, c-KP7, c-KP1 and c-KP2]. The head keys and every view switch also **reset the cockpit zoom word `V:4AAFA` to 0x80** (with bit 7 of `V:4A895` and `V:4A8F5` = 3, by the routine 0x1F060 at 0x1E706, 0x142A8 and 0x1EC14) [V: rig `view_probe.py step`: `]` gives 0x40, a following head key 0x80].

**Camera attitude in the head views** (0x32074 cockpit branch 0x321F0; the head triples of this section; rig `view_check.py head`: 600 random records of the classes 0x10, 0x11, 0x14, 0x20, 0x30 and all 14 codes, 0 differences against the composition routine; emulator `views-m3-airborne-heads.txt`: codes 1, 3, 6, 9, 11, 12, 13 on the airborne snapshot, 7 of 7 equal) [V]:

1. **The head is a rotation about the aircraft's own axes, composed with the aircraft attitude.** The camera angle words are the output of the composition routine of note 05 section 6 (step 3, 0x325B8) with the nine words at `E+0x80` (the attitude matrix of the viewed record, the matrix builder of its angle words `E+0x66`, `E+0x68`, `E+0x6A`) as input A and the **head triple (pitch, yaw, roll)** in place of the three rates: B = the matrix builder `fx_matrix_a` of the triple, the product P with the rule of note 05 (P[3j + i] = A[i] B[3j] + A[3 + i] B[3j + 1] + A[6 + i] B[3j + 2]) and the three angles extracted from P with the inverse tables (negative triple words get +28800 first). The port calls the same composition function as the flight step (`flight_compose`-equivalent, note 05 section 6), with the head triple; it does **not** add the head yaw to the aircraft heading (that is exact only for a level aircraft, and even then differs from the original by the table resolution: the stored values of the ground snapshot, 4504 for 2256 + 2240, 7040, 11832, 16640 and 2176 for the up view, are the composition's output).
2. **Head triples.** Aircraft classes (high nibble of `E+0x62` not 0x30), codes 1 to 11: (0, T[code], 0) with T = 2256, 4800, 7200, 9600, 12000, 14400, 16800, 19200, 21600, 24000, 26544. Codes 12 and 13: (P, 0, 0), where **the 7 degree level test on the aircraft's pitch word p = `E+0x66`** (signed compare) is: p < 560 or p >= 28240 (within 7 degrees of level either way) gives P = 0x5690 (22160) for code 12 and 0x19F0 (6640) for code 13; every other p gives 0x5460 (21600) for code 12 and 0x1C20 (7200) for code 13. Code 0: **no composition**: the camera words are the record's own words (p, `E+0x68`, `E+0x6A`) unchanged, which is the forward cockpit view of note 03 (`V:4AB42`, `V:4AB46`, `V:4AB4A`), as long as `V:4A869` is 0 or 1; with `V:4A869` 2 the triple is (0, 0x6E00, 0) and with 3 or more (0, 0x280, 0) (a glance of -8 and +8 degrees; no writer of `V:4A869` exists in the code and it is 0 in every capture, so this is [R], reached by nothing in play).
2b. **HUD step, offset and window row of a class 0x30 viewed record** [V: rig `view_probe.py round3` (the head keys KP6, KP4, KP8, KP2, KP7, KP1 from the codes 0, 3, 4, 6, 11 with the viewed record of class 0x30: step 50, offset 800, row 167 and the zoom 0x80 in all 30 cases); the code is 0x1E72C to 0x1E744 and 0x14290 to 0x142E4]: the head-key tail tests the class nibble of the viewed record first and **returns before the step, offset and equal-row test (0x1E748 onward)** when it is 3, so the equal-row test is never made for the seat; the step stays 50, the offset 800 and the window row 167 (179 in the map) for every head code 0 to 13 (set by 0x141E8 at the view switch, by the 0x142F4 test when the step is 0, and by 0x1EC14), and the head keys still change the code `V:4A85F`, request the list rebuild and reset the zoom. Nothing else about the seat's HUD follows the code table above. Unreachable in the port until the eject.
3. **The class 0x30 (ejection seat, `E+0x62` high nibble 0x30) triples**: codes 1 to 11 the rows (pitch, yaw, roll) of the table at 0x323DC: (24000, 7200, 21600), (26400, 7200, 21600), (0, 7200, 21600), (2400, 7200, 21600) and so on, the pitch rising by 2400 per code to 19200 for code 11 (yaw 7200 and roll 21600 throughout); **code 0 uses a twelfth row (21600, 0, 0)** (the seat's camera is always rotated); codes 12 and 13: pitch 0x3610 (13840) or 0x3840 (14400) for 12 and 0x230 (560) or 0 for 13, the first of each pair when the seat's pitch word is above 6640 and below 7760, else the second [V in the rig, R for the seat's use].
4. **What the words hold.** `V:4AB40`, `V:4AB44`, `V:4AB48` are longs whose low words `V:4AB42`, `V:4AB46`, `V:4AB4A` are the camera pitch, heading and roll in the 28800 unit; the camera matrix at `V:4AC90` is the matrix builder `fx_cam_ypr` of these three words with the rows scaled by the zoom words (note 03). `V:4ACD8` receives the matrix of the head triple alone (scaled the same way; the draw stage reads it at 0x221FE) and `V:4ACB4` the horizon matrix of the record's heading `E+0x68` (independent of the head). Examples, airborne snapshot (pitch 26360, heading 2256, roll 0): code 0 (26360, 2256, 0); code 1 (26664, 4800, 27552); code 3 (0, 9456, 26360); code 6 (2440, 16656, 0); code 9 (0, 23856, 2440); code 11 (26664, 28496, 1248); code 12 (24024, 16680, 14400); code 13 (4776, 2280, 0). So a 90 degree head turn of the pitched aircraft turns its pitch into roll (code 3: pitch 0, roll 26360 = -30.5 degrees), and the up view of a pitched aircraft passes over the vertical (heading +180 degrees, roll 180). The ground snapshot (pitch 0, heading 2240): code 1 (0, 4504, 0); 2 (0, 7040, 0); 3 (0, 9440, 0); 4 (0, 11832, 0); 6 (0, 16640, 0); 9 (0, 23840, 0); 11 (0, 16, 0); 12 (22176, 2176, 0); 13 (6624, 2176, 0).
5. **In the external views** the longs `V:4AB40`, `V:4AB44`, `V:4AB48` hold the pitch, heading and roll words of **record 0** (the player, `V:4B2A2` onwards) whatever entity is viewed; the camera attitude is then the aim pair `V:4AB4C`, `V:4AB4E` (note 03, "External camera aim") [V: ext and tower snapshots].
6. **The pitch gate** (0x143F2, note 04a) reads the **camera** pitch: in the cockpit the long `V:4AB40` (so with the head views it is the composed camera pitch: code 12 and 13 on a level aircraft give 22176 and 6624, that is v = 2772 and 828 in tenths of a degree, both inside 600 < v < 3150, so the gate is set in both), in the external views the word `V:4AB4C` [R].

In codes 3 and above the 3D window is taller (167 or 179 rows) and the area below it is black. The head views change only the camera attitude and the offsets; the HUD is drawn with the same routine and falls off the screen
(its pixels are skipped, not hidden by a flag). Frames `head-left.ppm` (code 11) and `c-KP6-1` (code 1) show
the panel shifted by 112 pixels and the cowl and HUD with it; `head-down.ppm` and `head-up.ppm` show no panel.

## External view text strip (0x3697A, 0x36A10, 0x36ADE)

Drawn when the view is external (`V:4A83D` nonzero), `V:4A891` is nonzero and bit 6 of `V:4A985` is set, on rows 184 to
188, background pen 3, glyph pen 10 (version 1 drawer); each text is 6 to 11 glyph slots of 4 pixels wide, between x 84 and 245. `V:4A891`
is set to 1 by the frame routine on every external-view frame (0x11400) and cleared when the view switches to another entity (0x27D06), so
it is 1 except for one frame after such a switch. `V:4A985` is the low byte of the word `V:4A984`, whose bit 6 means "flight simulation
running" (note 05: cleared at the crash hand-over; 0x40 in every snapshot), so the strip disappears after a crash:

| text | glyph x | content |
| --- | --- | --- |
| heading | "HDG" at 84, 88, 92; three digits at 100, 104, 108 | degrees = (`E+0x68` >> 3) divided by 10, truncated, three digits with leading zeros |
| speed | digits at 150, 154, 158 (blank leading); the speed unit label at 167, 171, 175 | the absolute `E+0x6E` divided by 12 |
| altitude | slots 208, 212, 216, 222, 226, 230 (blank leading); "FT" at 239, 243 | 5 times (`E+0x18` >> 10) |

(Frame: `heading-1-hdg.ppm` shows the heading 028, the speed 357 and the altitude 3 165.)

## View kinds, settled

Each item has code evidence and a snapshot or frame check made for this note (reference emulator, flight
snapshot `cockpit-ground.vasnap`, a stationary aircraft on the runway, so every camera number is exact; the airborne
check uses `airborne.vasnap`; scripts `views-ground.txt` and `views-airborne.txt`, outputs under `extracted/captures/views/`, git-ignored).

1. **Default external distance.** KP-Enter in the cockpit sets the external selector from the saved level
   `V:4A8EB`, which the key handler resets to -1 each time (0x1E3E2); the direction becomes 4 (behind). So the
   view starts at level -1 with the ring multiplier 1. The key with raw code 0x1B (script name RBRACKET) steps closer: from -1 it
   stays at -1; raw 0x1A (LBRACKET) steps farther along -1, 1, 2, 3, 4 and stops at 4. Multiplier per level
   (code at 0x2D858, applied to the three offset words before rotation): level -1 times 1, level 1 times 1.5
   (offset plus half of it, arithmetic shift), level 2 times 2, level 3 times 4, level 4 times 8. The scaling is **per word**: level 1 adds half of each word with an arithmetic shift (3 becomes 4, -9 becomes -14, -18 becomes -27), so it
   is 1.5 only for even words; levels 2 to 4 shift each word left by 1, 2, 3. Measured (class 0x11 ring entry 4, (0, 3, -18)
   in units of 1024, aircraft heading 2240): horizontal offset magnitude 18432 at level -1 (x 8653, z -16275); at level 1 the words are
   (0, 4, -27), measured x 12980 and y 4096 (not 4608); level 2 x 17307, y 6144; level 3 x 34614, y 12288; level 4 x 69228, y 24576.
   **Level keys** (`V:4A83D` in the external view, rig and emulator `el0` to `el3`) [V]: `[` (raw 0x1A, farther): -1 gives 1, then 2, 3, 4, and 4 stays; `]` (raw 0x1B, closer): 4 gives 3, 2, 1, then -1, and -1 stays (the code subtracts one and takes -1 when the result is not positive). Both only when the camera mode `V:4A966` is 0 and the ring flag `V:4A86D` is set (note 03, "External camera aim": `V:4A86D` is the "ring camera on" flag, not the signed look-at byte `V:4A86C`); each requests a list rebuild (`V:4A910` = 0xFF). The scaling acts on the three words of the ring entry as **16-bit words** before the rotation (the sum is an arithmetic shift of a word; no table entry overflows at level 4: the largest, class 0x14 entry 4, is -48 times 8 = -384).
2. **The tower view is a map view** (the key handler 0x1ECB8, the state routine 0x141E8; rig `view_check.py keys`, emulator `views-m3-map.txt`) [V]. The key M (raw 0x37), accepted only when the camera mode `V:4A966` is 0, **branches on the flag `V:4A865`**: with the flag clear it plants the map camera and sets the flag to 1, with the flag set it leaves the map (below); either way the view it leaves **ends in the cockpit**:
   - *M in the cockpit.* The handler plants the map camera (position longs `V:4ACF6` = 0x10C00000, `V:4ACFA` = the saved altitude `V:4A71C`, `V:4ACFE` = 0x11400000 and their negated masked copies `V:4ACEA`, `V:4ACEE`, `V:4ACF2`), clears the vector words `V:4AD02`, `V:4AD06`, `V:4AD0A`, stores the aim pair `V:4AB4C` = 0x1C20 (7200: straight down) and `V:4AB4E` = 0 (north up), clears the look-at byte `V:4A86C`, sets the ring flag `V:4A86D` = 1, stops the sounds (0x115F6), and then takes the tail of the KP-Enter handler: the direction `V:4A96A` is **not** changed, `V:4A9E2` = the record's heading, the request bit `V:4A97E` bit 1 is set and `V:4A8EB` = 0xFF. The state routine 0x141E8 then switches the view: **`V:4A83D` = `V:4A8EB` = -1** (0xFF), HUD step 50, and, because the map flag is set, window last row 179 (0xB3, not the 167 of the external view). The first map frame shows the map (frame `m-map.ppm`: sea and land with the grid, the carrier mark and the text strip).
   - *M in an external view (not in the map).* The same camera is planted, the flag is set, **`V:4A83D` keeps its level** (the map is drawn from level 4 as well; the level is only an ordinary byte then), the window row becomes 179 at once, and no request bit is set. **M here writes neither `V:4A9E2` nor `V:4A8EB`**: the handler's branch at 0x1ED6E jumps to the KP-Enter tail (which writes both and the request bit) only when `V:4A83D` is zero [V: rig `view_probe.py map`: `V:4A9E2` and `V:4A8EB` poked to marks stay; emulator `views-m3-fix.txt` S3 in 16 frames: `V:4A8EB` poked to 0x55 stays 0x55, `V:4A83D` stays 0xFF, `V:4A865` 1, the row 179, the request word 0]. The saved altitude `V:4A71C` is not touched either (the camera takes it).
   - *M in the map* (`V:4A865` = 1): the altitude `V:4ACFA` is saved in `V:4A71C`, then the KP-Enter tail runs (flag `V:4A865` cleared by it, `V:4A86C` = 1, `V:4A97E` bit 1 set): 0x141E8 toggles an external selector to **the cockpit, never back to the external view the map was entered from** (emulator: external level 4, M, M gives `V:4A83D` = 0 with the saved altitude kept; the earlier statement "pressing M again returns to the previous view" is withdrawn).
   - *KP-Enter in the map* (emulator `mk_a`, `mk_b`): the handler clears `V:4A865` and sets the request bit, so the next 0x141E8 returns to the cockpit (window row 144, code 0, `V:4A86C` = 1); the map is left exactly as by M, except that the altitude is **not** saved (only the M handler saves it).
   In the map the camera is **directly above the aircraft** (x and z equal the aircraft's position every frame, y = the altitude
   `V:4ACFA`), **looking straight down** with the pitch fixed at 90 degrees (7200 units, `V:4AB4C`) and the yaw at
   0 (`V:4AB4E`: north up; the free-look keys do nothing in this view), no look-at tracking (`V:4A86C` is
   cleared), and the 3D window bottom row 179. The terrain is drawn as a flat map with grid lines and numbered
   latitude and longitude labels, by 0x2FC58 (below). **The altitude's initial value** (V:4A71C is 0 in the image; the one writer apart from the M handler is the **new-mission initialisation routine 0x1D1BE** at 0x1D276): 0x3000000 at every mission start (the same routine plants `V:4AB4C` = 7200, `V:4AB4E` = 0, the window row 167 and the zoom words 168, 252, 128 with `V:4A895` = 0x80) [V: the value is 0x3000000 in every snapshot that has not seen M, and 0x1000000 after the M exit at the low altitude]. `]` (raw 0x1B) **lowers** it by 0x2000000 and `[` (raw 0x1A) **raises** it by 0x2000000, each with a list rebuild request (0x1E57A, 0x1E600), only while `V:4A83D` is nonzero and the camera mode is 0. **The limits are clamps, not refusals**: the result of `[` above 0x8000000 is stored as 0x8000000 (from 0x7000000 or 0x6000000 the altitude becomes 0x8000000; at 0x8000000 it stays and the request is still stored), the result of `]` below 0x1000000 is stored as 0x1000000 (from 0x2000000 or 0x3000000... to 0x1000000; at 0x1000000 it stays and the request is still stored). The altitudes reached from 0x3000000 are therefore 0x1000000, 0x3000000, 0x5000000, 0x7000000, 0x8000000, and a `]` from 0x8000000 goes to 0x6000000, 0x4000000, 0x2000000, 0x1000000 [V: emulator `views-m3-fix2.txt` T2: `[` 0x3000000 to 0x5000000 to 0x7000000, `]` back to 0x5000000, from a poked 0x7000000 `[` gives 0x8000000 and again 0x8000000 with `V:4A910` = 0xFF both times, from a poked 0x3000000 `]` gives 0x1000000 and stays there with the request stored; rig `view_probe.py map`: the eight start altitudes; earlier emulator `el_m`, `el_m_r`]. The keys do not write the hold byte in the map (the hold byte is 5 after their releases, below). The altitude is a position-unit height (204.8 units per foot, so 0x3000000 is far above the 40960-foot
   ceiling of 01b: this is a map scale, not a flight altitude). See "Map renderer" below for what the view draws. Frames:
   `extracted/captures/views/tower-1.ppm` (0x3000000), `tower-2.ppm` (0x1000000), `tower-low.ppm` (0x7000000).
   The base point 0x10C00000 / 0x11400000 written when M is pressed is only the first value; the per-frame
   rule above replaces it (0x2D756 sets the camera x and z from the viewed record while the flag is set).
3. **Cockpit head codes 12 and 13.** KP7 (code 12) looks up and KP1 (code 13) looks down, both at the window
   bottom row 167, panel and HUD off screen (offset 800). The camera attitude is **the composition of the head triple (pitch 0x5690 or 0x5460 for 12, 0x19F0 or 0x1C20 for 13, chosen by the 7 degree level test of the aircraft's pitch word, yaw 0, roll 0) with the aircraft attitude** (see "Head views": the earlier wording "yaw the aircraft heading, roll 0" holds only for a level aircraft). Measured near level: pitch 22176 and 6624 and yaw 2176 for heading 2240; for the pitched airborne snapshot (code 12: (24024, 16680, 14400), code 13: (4776, 2280, 0)). Both branches of the level test are now run (rig 600 records; emulator codes 12 and 13 on the airborne snapshot, which takes the second branch: pitch 26360 lies between 560 and 28240). Frames `head-up.ppm` and `head-down.ppm`.
4. **Ring offset unit and camera attitude.** **The integer rule** [V: rig `view_check.py ring`, 600 random records, classes, directions and levels, 0 differences; emulator snapshots of ext-L-1 and the airborne ext snapshots]: for the ring direction d (`V:4A96A`) and the level (`V:4A83D`) the three words w_j are the ring entry of the class for d (below), scaled by the level (-1: unchanged; 1: w + (w >> 1), arithmetic shift of each word; 2, 3, 4: w shifted left by 1, 2, 3); the offset is **out_i = (sum over j of M[3i + j] x w_j) >> 4**, an arithmetic shift of the 32-bit sum, floor for negative sums, where M is the nine Q14 words at `E+0x92` of the viewed record for the directions 0 and 4 (of every class except 0x30) and the heading-only matrix otherwise (rows (c, 0, s), (0, 16384, 0), (-s, 0, c) with s and c the sine and cosine, Q14, of the angle 28800 minus the record's heading `E+0x68`, or 0 when the heading word is 0; built at 0x321CA into `V:4ACC6`). The camera position is the record's position longs (`E+0x14`, `E+0x18`, `E+0x1C`, unmasked) plus out. So **one table unit is
   1024 position units** (the words are the units, not 256 times the unit: the extractor's files hold them little-endian, 0x0030 is 48; the earlier "raw 12288 is 48" was a byte-order misreading), the position unit 1 foot = 204.8 units, one table unit is 5 feet
   and the chase camera of class 0x11 is 90 feet behind (01b's 256 positions per camera-offset unit is another quantity: the unit of `V:4AB2A` and following). **Verified on the ground snapshot** (heading 2240, matrix words 14466, 0, -7692, 0, 16384, 0, 7692, 0, 14466; level -1, direction 4, w = (0, 3, -18)): out = ((-18) x (-7692) >> 4, 3 x 16384 >> 4, (-18) x 14466 >> 4) = (138,456 >> 4, 3,072, -260,388 >> 4) = **(8653, 3072, -16275)**; **on the airborne snapshot** (pitch 26360, heading 2256, matrix words 14439, 3930, -6671, 0, 14117, 8316, 7742, -7329, 12441; level -1) direction 4 gives **(8241, -6709, -15371)** (131,868 >> 4, -107,337 >> 4, -245,925 >> 4) and direction 0 (w = (0, 3, 18)) gives (-6768, 12002, 12621): the full 3x3 attitude matrix, so directions **0 and 4 follow pitch and roll**; for direction 2 the
   offset (-16244, 3072, -8710) and direction 8 (2419, 21504, -4513) differ from the full-matrix values (-15507, 2646, -10084) and
   (7242, 15929, -13508) and keep the table's y (3 times 1024; 21 times 1024), so the other eight directions use the heading matrix. **The rule "heading only for class 0x30 regardless of direction" is now verified in the rig** (the class test comes before the direction test). The **ring rotates with sudden heading changes**: every frame 0x2D756 compares the record's heading with the stored `V:4A9E2` (then stores it) and when the **absolute difference** (not wrapped round the circle) lies between 7200 and 21600 inclusive and the direction is 1, 2, 3, 5, 6 or 7, the direction `V:4A96A` is advanced by 4 modulo 8 (the same side of the world), directions 0, 4, 8, 9 stay [V: 12 cases]. The camera's own
   **orientation points at the aircraft** (aim pair `V:4AB4C`, `V:4AB4E`, no roll) by the aim routine 0x1454A of note 03 ("External camera aim"): the target is an arctangent of the camera-to-target vector with the 0.1 degree table at 0x42C00 and precision chosen from the vector length, where the target is **not the record origin but the point one ring unit (1024 position units) ahead of it in the record's own axes** (note 03, "The target point"): the first frame of the level -1 chase on the ground snapshot is 720 (9.0 degrees; the pair to the origin itself would be about 757, 9.5 degrees); the snap targets of the other levels are 640 (level 1) and 760 (levels 2 to 4), and the pair reaches them by quarter steps per pass (the sequences in note 03, "Levels and passes"; the earlier figures 644 and 673 at level 1, 729, 751 and 757 were samples at unrecorded passes); **the port runs that routine, no exact arctangent and no fitted pass count**; the per-frame **turn limit** is the rule of note 03: pitch and heading limited separately to a quarter of the remaining difference but at most 2000 units (25 degrees), or 560 (7 degrees) in the tracking camera modes (`V:4A966` nonzero), and the pair **snaps** (no limit; `V:4A85E` is set to 1 by every snap) on the first external frame after entering the view (the flag `V:4A85E` is 0), on a change of the ring key (record offset or direction), when the ring flag `V:4A86D` is clear, and **every frame while the look-at byte `V:4A86C` is negative** (0xFF; written by the crash-sequence state routines, [R]); with `V:4A86C` zero nothing is aimed at all (the map). The camera height is clamped to at least
   (deck height + 7) times 256 position units, where the deck height is the word `E+0x4E` (112 over the carrier deck, note 05; the code reads
   it only when bits 6 or 7 of the byte at `E+4` are set and otherwise uses 0), so the camera never goes below 1792 units (about 9 feet) over the
   runway or the sea: the "below and in front" camera (direction 9) measured y offset -8 at the runway instead of -15 times 8192; the clamp applies to the camera y long `V:4ACFA`, after the offset is added [V].
5. **Class byte that selects the ring.** `E+0x62` of the player's entity (record 0, `V:4B29E`): 0x11 for the F/A-18
   and 0x10 for the F-16. It is copied there from `V:4A901` when the flight is set up (0x1D588); `V:4A901` is
   set by the aircraft menu (key 1 gives 0x11, key 2 gives 0x10) and initialised to 0x11. Ring choice [V: rig, 7 classes]: the **whole byte** 0x11 uses the table at 0x2D6A0 and the whole byte 0x14 the table at 0x2D6DC; the **high nibble** 0x30 (the byte 0x30 to 0x3F) the table at 0x2D718 and heading-only for every direction; every other class,
   including the F-16 (0x10), uses the default table 0x2D664. (Class 0x30 is the player's entity after an ejection, from the eject code, read only; the HUD
   form, scale or numeric, is chosen by class 0x10 versus the rest.)
6. **Keypad to ring index** (key handler 0x1DC20 to 0x1DC74 and the dispatch at 0x1DA20 to 0x1DA8C; in the
   external view the handlers 0x1E4A8 to 0x1E4F6 store the index in `V:4A96A`): KP8 (0x3E) = 0 (front),
   KP9 (0x3F) = 1, KP6 (0x2F) = 2, KP3 (0x1F) = 3, KP2 (0x1E) = 4 (behind), KP1 (0x1D) = 5, KP4 (0x2D) = 6,
   KP7 (0x3D) = 7, KP5 (0x2E) = 8 (above and behind), KP-dot (0x3C) = 9 (below and in front). KP0 (0x0F) has no
   handler and does nothing (the earlier note that it repeats KP9 was an artefact of the script order).
   Confirmed by reading `V:4A96A` after each key in the snapshots (KP8 gave 0, KP5 8, KP-dot 9, KP6 2, KP4 6,
   KP9 1, KP3 3, KP7 7, KP1 5, KP2 4) and by the measured offsets (direction 0: 147456 units ahead and 24576
   above at level 4; direction 8: 172032 above and 40960 behind).

7. **Applying a view switch: the state routine 0x141E8, the request bit and idempotence** (frame routine 0x11124 calls it at 0x11152 after the mission state routine, and **only while the flight gate `V:4A84D` is 1**; rig `view_check.py keys`, emulator `views-m3-pause.txt`) [V]. KP-Enter (raw 0x43; the handler 0x1E390 also accepts it with `V:4A965` = 2), and M in the cockpit, **set** bit 1 of the word `V:4A97E` (an OR, not a toggle) after the camera set-up described above; the routine 0x141E8 tests the bit, clears it and **toggles the view once**: 
   - external to cockpit: `V:4A83D` = 0, HUD step and offset 0, `V:4A85E` = 0 (this is what makes the next external frame snap, below), `V:4A8ED` = 1;
   - cockpit to external: `V:4A83D` = `V:4A8EB` (0xFF, written by every KP-Enter/M handler pass), HUD step 50, offset 800;
   - both: the event echo byte `V:4A968` = 0xFE, the call 0x19C58(3), the routine 0x1F060, `V:4A85F` = 0, the list request `V:4A910` = 0xFF, the routine 0x1E7B2 (the redraw counters and the window row of code 0), and the window row: 167 (0xA7) in an external view, 179 (0xB3) when `V:4A865` is set; class 0x30: HUD step 50 always and the window row as for the external view in both. **The two calls**: `0x19C58(n)` clears the entry n of the table of sound-channel slots (the longs at 0x54ED0) and stops that channel through the sound library call 0x5503C; with n = 3 the view switch **stops the sound on channel 3** and changes no view or HUD state [R: read, never run in a capture; its effect belongs to the sound slice, note 10]. `0x1F060` is the **zoom reset**: bit 7 of `V:4A895` set, the zoom word `V:4AAFA` = 0x80 and `V:4A8F5` = 3 (the redraw counter of the zoom mark), so every view switch, and every head key (at 0x1E706), puts the cockpit zoom back to 0x80 [V: rig `view_probe.py step`]. `0x1E7B2` loads the value 3 into the redraw counters `V:4A8EE`, `V:4A8EF`, `V:4A8F1`.. (the HUD and panel elements redraw for three passes) and clears `V:4A990`/`V:4A9D0` unless `V:4A878` is set [R].
   So **two KP-Enters in one pass toggle once** (the second only sets the already set bit: rig, two presses give bit 1 set and nothing else changes), and a KP-Enter in a pass where the gate is 0 is kept until a later pass (see 8). KP-Enter handler contents (shift not held): the cockpit case sets `V:4A96A` = 4, then always `V:4A86D` = 1, `V:4A86C` = 1, `V:4A865` = 0, `V:4A9E2` = the viewed record's heading, the bit, `V:4A8EB` = 0xFF; with the left shift held (`V:4A930`) the handler runs 0x1E3EE instead, a **fixed-camera view** that this note does not settle (read only: it sets `V:4A85E` = 0xFF, takes the camera position from the table at 0x47ADE indexed by `V:4A900` (an entity's position plus an offset, or a table position at 0x47B30) through 0x1D3F2, clears `V:4A86D` so that 0x2D756 plants no ring, and leaves the view as above). **The keys reach the view selector only through this routine** (and the viewed-entity switch 0x1EC14, called by the world update: `V:4A83D` = `V:4A8EB` when an external view is already selected); the other writers belong to the mission and state flows: 0x1D4E2 (new flight: cleared), the states at 0x11B6C, 0x121B0, 0x122A2 and 0x130C0 and the seat spawn at 0x27462 (note 05). Cockpit recentring of the head by the hold byte also runs in this routine (head keys above).
8. **Keys while the game is paused** (the P key, raw 0x19, handler 0x1ED9A; emulator `views-m3-pause.txt`, frames `p-paused.ppm`, `p-kp6.ppm`, `p-unpaused.ppm`; the per-blank peek of `V:4A992` and `V:4A866` around the key) [V]. The key routine 0x1DAA0 runs in every pass of the frame routine (it is called from 0x11514, before the gate test), **so head keys, KP-Enter, M, the zoom keys and the level keys are processed during a pause**: the head code and HUD step changed (KP6 gave code 1 with step -7 while `V:4A866` was 0xFF), `V:4A96A` = 4, `V:4A865` = 1 with `V:4A86C` cleared, and `V:4AAFA` halved. **The world is not redrawn**: the pause state keeps the gate `V:4A84D` at 0, the frame routine takes the branch at 0x11146 past the whole world, camera, list and HUD part, and the screen is the pause picture (the last flight picture dimmed by the fade to level 10, nearly black but not blank, with one short line of text announcing the pause, note 08 5.14; the text is the text slice's, note 08 (the paused text is id 0x26, section 5.14); frames before and after KP6 are identical). **The view switch is deferred**: 0x141E8 is not called, so `V:4A83D` stays 0 and the request bit stays set; after the second P (the handler 0x1ED9A: when paused it stores 0xFE in `V:4A968` and clears `V:4A866`) the first flight pass applies the pending request **once** (captured: KP-Enter then M while paused gave one switch: `V:4A83D` = 0xFF, `V:4A865` = 1, window row 179, `V:4A866` = 0, `V:4A84D` = 1). The aim routine's update is skipped while `V:4A866` is nonzero (0x32144) and the ring camera 0x2D756 is not recomputed; nothing is drawn in that time anyway. P itself (not paused): the handler stops the sounds (0x115F6) and stores `V:4A866` = 0xFF; `V:4A866` reads 0xFF through the pause. **No handler clears the low three bits of `n`**: the code at 0x1EDF8 to 0x1EE08 (which would store 1 in `V:4A866` and clear the low three bits of `n`) has **no reference** (no branch, no address long or pointer to 0x1EDF8, 0x1EDEC leads to the end of the routine by an unconditional branch): it is dead; the per-blank peek shows `n` stopping at its value in the pass of the key (28, 29, then 30 for 30 blanks and until the unpause, no change of its low bits). The port must **not** clear `n & 7` on the pause (the task 7 change was based on the unreachable code).

   **Which handlers act in a pause** [V: rig `view_probe.py paused`, the key routine 0x1DAA0 run with the pause byte `V:4A866` clear and set from one staged state, 23 keys, the memory difference of the whole variable area compared; emulator `views-m3-fix.txt` S4 on the ground snapshot and `views-m3-fix2.txt` T4 on the airborne one]. Every handler of the dispatch runs in a pause and writes the same bytes as in flight, **except E** (the eject arming, raw 0x12: it tests `V:4A866` and does nothing) and **P** (which clears the pause). That covers the gear key G (bit 7 of the control byte `V:4B2B8`: emulator, airborne snapshot: 0x00 to 0x80 and the message byte `V:4A93D` = 0x83 in the pause, the flight toggling further, 0xE0, after the unpause), the hook key A (the hook bit 0x8000 of the second flag word of record 0 and `V:4A8FF` bit 7: emulator, 0x00 to 0x80), the brake key (BACKSPACE: bit 0x0800 of the first flag word of record 0: emulator, 0x11C8 to 0x19C8), H (the HUD toggle `V:4A859`: emulator, 1 to 0), the throttle keys (the low bits of `V:4B2A1`, set while the key is down, cleared by the release: rig), the cursor keys, the flare, chaff and ECM keys (the counters `V:4A904`, `V:4A905` fall in a pause), SPACE and RETURN (RETURN is the "not ready to reposition" key, note 05 17.17); the function keys (throttle presets) were not run in a pause [R]. **ESC** is not gated either, and the pause does **not** end at the key [V: emulator `views-m3-esc-pause.txt`, airborne snapshot, P then ESC, one peek per blank, k counted in blanks from the key press (the byte `V:4A8E2` is consumed by k = 1)]: `V:4A866` goes from 0xFF to **1 at k = 1** and stays 1 (the world stays unrendered and the aim frozen); `V:4A84D` is 0 until k = 24, **1 for k = 25 to 29**, and at **k = 30 `n` is cleared, `V:4A866` is 0 and `V:4A84D` is 0 again** (the flow then continues as in flight, note 05 17.17 rule 9, whose k = 3, 37 to 41 and 42 become 1, 25 to 29 and 30 when the flow starts from a pause; the earlier reading "V:4A866 read 0" was taken 35 blanks after the key, after k = 30). **P during the ESC countdown** [V: emulator `views-m3-esc-pause.txt`, the second and third segments: ESC, 10 blanks, then P; per blank from the P key]: the P handler sees the pause byte nonzero (1) and **clears it to 0 (an unpause, plus the echo byte `V:4A968` = 0xFE)** in the first blank; it does not cancel the ESC flow: from a pause the gate showed 1 at P + 14 to 19 and `n` was cleared at P + 20 (ESC + about 30, as without the P), and from free flight (the flow's own `V:4A866` = 1 from k = 3) the gate showed 1 at P + 29 to 34 and `n` was cleared at P + 35 (ESC + 42 within the 1 to 3 blanks of key-delivery jitter of the step mode). The pause byte stays 0 afterwards; the countdown, the gate pulse and the clear of `n` run on unchanged. A further P would find the byte 0 and set it to 0xFF again [R, not run]. **Rule:** a P during either ESC flow clears the pause byte; it neither restarts nor stops the flow. The port's ESC in a pause must therefore keep the pause byte nonzero (1) for the 30 blanks, not leave the pause at once, and use 30 blanks instead of 42 for the clear of `n`. The effects that need a flight pass (the gear and hook motion, the throttle) are taken by the first flight pass after the unpause. The head keys, KP-Enter, M, the zoom and level keys act on their bytes at once in a pause (above); the view switch itself waits.

   **The hold timer does not run in a pause** [V: emulator `views-m3-fix.txt` S4b, see "Head views"]: the routine 0x29882 is inside 0x297D6, which the frame routine calls only in a drawn frame, so neither the head-hold byte nor the other per-second counters fall, and `V:4ABAA` is not refreshed.
9. **The zoom keys in the cockpit** (0x1E80E and 0x1E82E, also taken in an external view while `V:4A86D` is clear) [V: emulator `zr0` to `zl3`, rig]: `]` halves the word `V:4AAFA` (the third zoom word, the depth row scale of note 03, initial 0x80) when it is above 0x20, `[` doubles it when it is below 0x80: the sequence `]` `]` `]` gives 0x40, 0x20, 0x20 and `[` `[` `[` back 0x40, 0x80, 0x80. Both set `V:4A8F5` = 3 and the hold byte `V:4A949` = 0xFF (their release, raw 0x9B and 0x9A, sets the hold byte to 5: see the paragraph on the releases below), request a list rebuild only when the low seven bits of `V:4A895` are 0 (they are, 0x80 at mission start) and keep bit 7 of `V:4A895` equal to (zoom = 0x80).
   **Releases of the bracket keys and of the head keys** [V for the bracket releases: emulator `views-m3-fix2.txt` T5, `V:4A949` poked to 0 before a `[` press and read afterwards; R for the dispatch conditions]. The release of `]` or `[` (raw 0x9B, 0x9A) sets the hold byte `V:4A949` to 5 (and bit 6 of `V:4AA55`, the event word) **in every view**: the cockpit (the press had set 0xFF, the release 5), an external view (the press, a level key, leaves the byte, the release sets 5) and the map (the same); the handler (0x1E892) tests no view. The five head-key releases (raw 0x9E, 0xAD, 0xAF, 0xBD, 0x9D) are in the camera-mode-0 branch of the dispatch only. The dispatch reaches the bracket handlers and their releases in every pass that is not diverted to the tail 0x1DD64, that is unless the camera mode `V:4A966` is 3, or `V:4A88B` or `V:4A965` is nonzero (R); in the camera modes 1, 2 and 4 they are taken only when the first block's conditions fail (`V:4A849` nonzero, `V:4A83F`, `V:4A95E` zero or `V:4A903` negative) or `V:4A95E` is 2 (R). So in the flight, in the camera modes 0, 5 and 6, **the port takes the release in every view**; the modes 1 to 4 are not covered.
10. **List rebuild requests by the view keys** (note 04a, "view-mode switches", 23 sites): see the classification there.
11. **Camera mode 5** (`V:4A966` = 5; R: read from the code, never captured). It is stored at 0x12B4A by a state routine of the mission flow (the chain 0x12A92, 0x12AC0, 0x12AFC; not decoded further here): after the countdown `V:4AB8E` has run out and the gate `V:4A84D` has been cleared, when the game-mode byte `V:4A95E` is 1; it sets the direct-draw flag `V:4A90F` = 1 and `V:4A802` = 7 and clears `V:4A965` and `V:4A87B`, and the chain goes on to mode 6 at 0x12BD8 (with `V:4A849` = 1, `V:4A90F` = 0). The other writers of `V:4A966` are mode 4 (0x123A0, 0x2DA72), 3 (0x2DA3A), 2 (0x12780), 6 (0x124F8, 0x12C90) and the clears (0x121B6, 0x12252, 0x12D10, 0x2DC54). The key dispatch in mode 5 skips the whole flight-key block (gear, hook, brake, throttle bits, H, M, the head and ring keys, KP-Enter) and handles only ESC (raw 0x45), the digit keys 1 to 0, the function keys F1 to F10 and the keypad keys KP1 to KP8 (the last two groups through 0x1E9A4, a selection: the aircraft choice `V:4A901` while `V:4A849` is set, otherwise the selector `V:4A83F` when it is 0 and the key is one of nine, other cases at 0x1EAA4 not decoded), the zoom keys `]` and `[` with their releases (the cockpit zoom works; the level and altitude keys do nothing in a non-zero mode) and P (which needs `V:4A966` non-negative); KP-Enter works in mode 5 only when `V:4A965` is 2. Modes 1 to 4 and 6 stay uncovered.

**Ring tables** (values in table units: the words are the units themselves, 0x0030 is 48; the extractor's files hold them little-endian, so a big-endian read of `camera_ring_14.bin` shows 12288 for 48, which is the misreading the earlier wording recorded: x lateral, positive to the aircraft's left; y up; z forward; ten entries per class, 3 words each, 60 bytes;
the full contents are in `assets/tables/camera_ring_*.bin`, which the extractor already writes). Index order: 0 front, 1 front-right, 2 right,
3 back-right, 4 back, 5 back-left, 6 left, 7 front-left, 8 above-behind, 9 below-front. Characterising samples: all entries have y = 3 for
the default and class 0x11 rings in indices 0 to 7 (the class 0x14 and 0x30 rings use 4 to 12); the class 0x11 radius is 18 to the sides and
front-back, so entry 2 is (-18, 3, 0) and entry 4 is (0, 3, -18), and entry 0 is (0, 3, 18); the default ring has radius 14 to 17
(entry 0 (0, 3, 15)); entry 8 is above and slightly behind (positive y 18 to 21, z about -5 for the two fighter rings; for class 0x14 entry 8 is (0, 48, -12) and entry 9 (0, -32, 48), class 0x30 entry 8 (1, 18, -12) and entry 9 (0, -6, 12); the figure 48 is a y value in entry 8);
entry 9 is below and ahead (class 0x11: y -15). The class 0x14 ring is about 2.6 times the class 0x11 radius and class 0x30 about 1.1 times.

Handedness, measured: for the stationary aircraft at heading word 2240 (28 degrees), direction 2 and level 4 the camera offset was (-130194, 24576,
-69228), which is 8 times (-18, 3, 0) rotated by the heading; direction 0 gave (-69228, 24576, 130194), ahead of the
aircraft. At heading 0 a positive table x is the world's positive x axis, which is to the aircraft's left, so
index 2 is on the aircraft's right, as stated in note 04.

### The HUD step and the window row at each start [V for free flight, R for the others]

The pair `V:4AA3E` (HUD step; the offset `V:4AA40` is 16 times it) and `V:4AA3C` (window last row) is written by four groups of code before the first flight pass, in this order; the table gives the values after each.

| point | step | row | view | source |
| --- | --- | --- | --- | --- |
| the reset block `0x1D256` (every start of every mode, `0x1D2A2`, `0x1D2AA`) | 50 | 167 | | R |
| the dispatch `0x1201E` (note 08 6.1 items 2 and 7): modes 1, 3 to 8, 9, 0x7E, 0x7F | 50 | 179 | | R |
| the dispatch: mode 2 (training part 1) and mode 0x7D (part 2) | 50 | 167 | | R |
| the dispatch: mode 9 (the branch `V:4AA3E` = `V:4AA40` = 0 and the helper `0x1EFEC`, which stores 144) | 0 | 144 | cockpit | R |
| the selection screens of free flight (location, camera mode 5; aircraft, mode 6), sampled | 50 | 179 | external, map flag set (`V:4A865`) at the location screen | V (`followup-mapcam.txt`, `followup-mapcam-air.txt`) |
| free flight, carrier start (key 4 or the default) and airfield 1 start (key 1): the approach, state `0x12D58` (camera mode 6), sampled at the first and the 100th video frame | 50 | 179 | external (`V:4A83D` = 1), map flag 0, HUD switch 0 | V (`followup-hudstep-carrier.txt`, `followup-hudstep-airfield.txt`) |
| free flight, carrier start and airfield 1 start: the approach `0x12D58` **pass by pass** (milestone 7, `flow7-approach-air1.txt`, `flow7-approach-carrier.txt`) | 50 | 179 until the first pass in which the camera height long `V:4ACFA` is below 0x24000 in camera mode 6 with an external view, then **167** (pass 60 of 118 at airfield 1, pass 57 of 104 at the carrier; the view-state routine's tail, which also runs the view refresh) | external, map flag 0, HUD switch 0 | V (the two logs) and R (the routine) |
| training part 2 (mode 0x7D): the briefing, the approach `0x12D58` (31 passes) and `0x12DB8` | 50 | 167 (the dispatch value, never changed in the approach) | external (`V:4A83D` = 1), camera mode 6 in the approach | V (`flow7-training-7D.txt`, every 8 blanks) |
| training part 1 (mode 2): the briefing, `0x12DB8`, `0x12EDA` and the first flight pass | 50 | 167 | external, camera mode 0 throughout | V (`flow7-training-2.txt`, every 8 blanks) |
| the missions F1 to F6 (modes 3 to 8): the approach `0x12D58` pass by pass (milestone 7, final round, `sound7-approach.txt`, an F1 start: the view byte, the camera mode and the mover stage every pass) | 50 (R: the dispatch value of modes 3 to 8 and the view routine's tail; not peeked) | 179 then 167 by the height rule (R; not peeked) | **external (`V:4A83D` = 1) in passes 0 to 125, camera mode 6 until pass 124; the cockpit (`V:4A83D` = 0) from pass 126** | V (view, camera mode, stages: note 08 12.14 C14) and R (step, row) |
| free flight, both starts, from the first video frame of the state `0x12DB8` (the approach is over, camera mode 0) through `0x12E4E` and `0x12EDA` | **0** | **144** | **cockpit** (`V:4A83D` = 0), HUD switch 0 (set to 1 in `0x12EDA` after its countdown, note 08 6.2) | V (same logs: 80 samples of one video frame from `0x12DB8`; the carrier run reaches `0x12EDA` at sample 76, the airfield run ends in `0x12E4E`; no change in the values) |

After the approach the view is the cockpit's code 0 whatever the dispatch row was: step 0, row 144, and nothing but the head keys, the view switch routine `0x14232` (cockpit: step 0 and the row of the code; external: step 50 and row 167, 179 in the map view or for the ejection seat) and the resume routine `0x13056` (note 08 12 N7: step 0 and row 144 for a cockpit, 50 and 167 for an external view) change them afterwards ("Head views" for the table by code). The training modes 2 and 0x7D were sampled in milestone 7 (the two rows above: part 2 ends its approach with the cockpit, step 0 and row 144, one pass before `0x12DB8`; part 1 never leaves the external view with step 50 and row 167, the first flight pass included); the demo was not sampled and the modes 3 to 8 only in their view, camera mode and mover stage (the row above: the step and the row of a mission approach are R); their values at the first flight pass follow the same last writer, the view switch at the end of their approach or briefing camera (the rows of the table above are the starting values, R). **Port: a flight begins in the cockpit view with step 0 and row 144 at the first pass of the HUD (as `game.c` does) in every mode but training part 1, whose external view keeps step 50 and row 167 (V, milestone 7); the free-flight approach uses the external view with step 50, row 179 and then 167 by the height rule above (the milestone-6 port showed the cockpit with step 0 and row 144 for the whole approach; the milestone-7 code round ports the external approach: `src/core/mover.c` flies the camera, `game.c` approach_begin / approach_pass switch the row and the cockpit, `tests/test_missions.c` and `test_training.c` pin the passes; the missions F1 to F6 take the external approach too, 08 12.14 C14: the mover enters in stage 8 at the briefing camera's overview position, the step and the rows are the free-flight ones, R), the selection screens step 50 and row 179, and the training dispatch rows store 167 (V).**

### The glance byte `V:4A869` [R by the reference scan, V by 312 state blocks]

The only instruction of the image that mentions `V:4A869` is the read in the cockpit camera set-up (`0x3226A`, head code 0, aircraft classes other than 0x30): a value of 1 or below (a signed compare) leaves the camera words equal to the record's own pitch, heading and roll (the forward view); a value of 2 selects the head triple (0, 28160, 0) (8 degrees left, `0x6E00`) and any larger value the triple (0, 640, 0) (8 degrees right), composed like the head codes of "Head views". **No instruction of any width writes the byte**: there is no byte store to `V:4A869`, no word or long store at `V:4A866` or `V:4A868` that covers it (a linear disassembly of the whole code range, searched for the operands `V:4A866` to `V:4A869`: the only word or long operand of those addresses is none; `V:4A869` itself appears once, in the read; the four references to `V:4A868` are the reads and address loads of note 04's dead flag), and the reset block clears it with the rest of the flag block `V:4A848` to `V:4A87C` (note 08 6.1 item 1). It is 0 in all 312 captured state blocks. So the two glance triples can never be selected: **the port keeps the byte as the constant 0 and implements neither triple** (the forward cockpit branch only).

### Map renderer (what the "tower" view draws)

Located and run in the addendum round: **0x2FC58** (hunk 20), called from the frame routine at 0x11440 after the world, only when `V:4A865` is positive (the M-key map mode) and `V:4A83D` is nonzero (external view).
Its helpers: line projector and clipper 0x33936 (the shared frustum line clipper of note 03; end points go out through the line routine 0x3455E), entity point projector 0x3377C, point transform 0x2F6FA, entity mark
0x30046 (generic) and 0x3001C (ship), the 16 direction shapes at 0x2FED8 and the ship shape at 0x30012, and the version 2 digit drawer 0x37508 (number to packed decimal). Draw order: all vertical lines in ascending index, each followed by its label; then all horizontal
lines the same way; then the entities 0 to 15 (record stride 0x200). Terrain pens are the world renderer's (land pen 4, sea pen 6; window rows 1 to 179; rows 180 to 199 black with the external text strip, rows 184 to 188), measured on `tower-1.ppm`, `tower-2.ppm`, `tower-low.ppm`
(altitudes `V:4ACFA` = 0x3000000, 0x1000000, 0x7000000).

**Projection.** The camera is the viewed entity's x and z (`E+0x14`, `E+0x1C`) with y = `V:4ACFA`, looking straight down. The matrix at `V:4AC90` is three scales: `V:4AAF6` = 168 (world x to screen x), `V:4AAF8` = 252 (world z to screen y) and `V:4AAFA` = 128 (minus world y to depth).
All map coordinates are the **high words** of the position longs (floor of position / 65536); write cx, cz, cy for the camera's. Arithmetic is 16-bit wrapping with arithmetic right shifts and truncating divisions. Depth Z = cy x 128 / 256 = cy / 2 (the grid and the marks are at ground level, y_rel = -cy).
Screen x = 319 - (160 + trunc(160 X / Z)) with X = (168 x_rel) >> 8; the value handed to the line routine for y is 179 - (90 + trunc(90 Y / Z)) with Y = (252 z_rel) >> 8 (marks use that value plus 1). The scale is about 210 x_rel / cy pixels horizontally and 177 z_rel / cy vertically
(a half-degree longitude line is 52.5 pixels apart at 0x7000000; the measured vertical spacing of 54.5 against the formula's 55.4 is within the truncation, the pixels are reproduced exactly), the unequal factors are just the scales 168 and 252.

**Grid (replaces the earlier fitted offsets).** Pen 8, pattern word `V:4A79E` all ones, one-pixel lines; x_rel and z_rel are the line position minus the camera high word, and the line extents are camera-centred and fixed.

| | vertical lines (constant world x) | horizontal lines (constant world z) |
| --- | --- | --- |
| count | 24, i = 0 to 23 | 16, j = 0 to 15 |
| position (high word) | 0xF680 + 0x1C0 i, that is -2432 + 448 i | 0xFEB0 + 0x230 j, that is -336 + 560 j |
| in position units | -159,383,552 + 29,360,128 i | -22,020,096 + 36,700,160 j |
| spacing | 448 high-word units = half a degree of longitude | 560 = half a degree of latitude |
| extent | z_rel from -6720 to +6720 | x_rel from -5376 to +5376 |
| drawn at | the screen column | one row below the call y |

The earlier fitted offsets are explained: -29,270,000 in x is one line spacing (-29,360,128) and -3,000,000 in z is 0x300000 (48 high-word units). Relation to the panel formulas (which are unchanged and agree between map and panel at any position): vertical line i sits at panel longitude 115.0 + i/2 (the labelled odd lines at x.5 degrees carry the whole number below, e.g. the line at
122.5 is labelled "122"); horizontal line j = 2m + 1 sits at panel latitude 34.04 + m (exactly 0x300000 above the whole degree) and is labelled 34 + m.

**Labels (pen 2, verified: all glyph sets of the three frames exact).** Only odd line indices carry a label, and only when the clipper accepted the line; leading zeros are printed (both ranges are full width).

| | longitude (odd i) | latitude (odd j) |
| --- | --- | --- |
| value | 115 + (i - 1)/2, three digits, 115 to 126 | 34 + (j - 1)/2, two digits, 34 to 41 |
| glyph rows | 174 to 178 | five rows from clamp(call y - 1, 4, 175) |
| first glyph x | 4 x (d0 >> 2) + 8 with d0 = min(line x - 12, 300), then +4 per digit | 8, then +4 per digit |
| skipped when | line x - 12 is below 8 | never beyond the line being rejected |

The drawer snaps the first glyph to a 4 pixel grid (checked for every d0 from 0 to 319; deviations only at off-screen columns). The byte-column rule 8 x (x >> 3) for the middle glyph, seen on some lines, is a coincidence of this rule.

**Marks (entities 0 to 15, record word 0 = `E+0x00`).**

| condition | rule |
| --- | --- |
| drawn | bit 0x40 of word 0 set |
| hidden | bit 0x0008 of word 0 set and bit 7 of byte `+3` set (on the ground, note 05; the 0x0008 bit is the one the radar scope reads as friendly) |
| class high nibble 0x20 (ship, carrier) | the ship shape in **pen 3** (black on the reference emulator), every frame, only when `V:4A90F` is not negative |
| the player (entity 0) | pen 13 with the shape rule below |
| other entity with bit 0x0008 | pen 13 |
| other entity without bit 0x0008 | pen 1 (red) |
| mode 3 (`V:4A95E` = 3) with bit 6 of byte `+0x20` clear | pen 3 |
| position | the entity's x and z through the grid projection at ground level; centre column x, centre row y_call + 1 |
| shape selection | sector = floor((`E+0x68` + 900) / 1800) modulo 16 (1/80 degree; 1800 = 22.5 degrees; sector 0 faces up, 8 down); stored in byte `+0x71` together with position nibbles in `+0x70` |
| shape layout | triples (dy, dx, length) ended by 0x80: a run on row y + dy from column x + dx for length pixels, drawn one row lower; dropped when y + dy is outside 0 to 179, x + dx is negative or the run end exceeds 319 |
| gating in flight | with `V:4A86D` = 1 and `V:4A90F` = 1 (both set at flight start) the entity takes the direct path and is drawn only on frames with bit 0 of the pass counter set (the test reads the low byte `V:4A993`, the same bit as bit 0 of the word `V:4A992`; every other frame); the ship row has its own condition, `V:4A90F` not negative, **independent of `V:4A86D`** (`0x3001C`) |

Shape data: the table is disk data and is not listed here. It holds 17 offset words (the 16 sectors and a repeat of sector 0) and then the 17 shapes, 324 bytes in all at `0x2FED8` (the extractor's `cockpit/mapshapes.bin`, verbatim). Sector 0, which faces up, has five rows and is the narrowest at the top and the widest at the bottom; the ship shape has three rows. The extractor's validator checks the structure (the offset words point at the shape starts, every run is at least one pixel long, the row counts of sector 0 and the ship) and then an FNV-1a (32-bit) hash of the bytes of the 17 shapes against one constant that identifies the shipped set (`MAPSHAPES_FNV1A` in `cockpit.py` and `cockpit.js`); the synthetic disk carries an invented set whose own hash the tests pass instead. The carrier at position (0x11980000, 0x10580000) projects to column 71, row 107 in `tower-1`, exactly where the ship is drawn. The player's mark
is visible only airborne (in the tower snapshots the aircraft is on the ground and hidden).

**Verified** by running 0x2FC58 over each tower snapshot (`tools/hud-emu/verify_map.py`): 898, 521 and 2140 overlay pixels (grid, labels, ship) with 0 differing from `tower-1`, `tower-2`, `tower-low`; the mark pens, hide rule, gate and all 16 sectors including the heading wrap
(28799, 27900) on synthetic entities; the airborne player gives the sector shape in pen 13. **Read only:** the clipper internals (note 03), the `V:4A95E` = 3 pen rule and the `V:4A90F` = 0 / `V:4A86D` = 0 paths.

**What `V:4A90F` and `V:4A86D` hold, and the paths with them clear** [R: every store to the two bytes]. `V:4A90F` is written only by the flow states and the flight start, never by a flight view or key: `0x1D25E` (flight start) = **1**; `0x119AC` = `0xFF` (the boot start-up state `0x11962`, with `V:4A865` = `0xFF`; so the credits, the menu and a first demo); `0x1258E` and `0x12756` = 1 (the map behind the security and location screens, `V:4A865` = 1); `0x125F8` = 0; `0x127AE`, `0x12AE2`, `0x12BD2` = 0 (camera modes 3, 5 and 6 of the approach); `0x12A76`, `0x12B52`, `0x12C56`, `0x12D2A` = 1. So **in a flight it is 1 in every view, in the map view, after the view keys and in the fixed camera** (no view code touches it), `0xFF` in the demo only, and 0 only during the approach cameras of the mission start. `V:4A86D` (the direct-draw flag) is stored 0 by the flow states (`0x121BC`/`0x124E0` by value, `0x127A8`, `0x12ADC`, `0x12C04`, `0x12D22`) and 1 by `0x12788`, `0x12AAC`, `0x12B28`, `0x12C78`, the flight start tail `0x1D4EE`, the key bodies `0x1E3B0` and `0x1ED58` (the M key into the map), `0x28698` and `0x28998` (the mission script's end states); `0x1E48E` clears it and the toggle body at `0x1EBD4` flips it. The flight value is therefore **(`V:4A86D`, `V:4A90F`) = (1, 1)**: the gating row of the table above. The paths with them clear: [R: the per-entity routine `0x30046` and the ship routine `0x3001C`, read in full in the round of note 07 Part B; the entity loop `0x2FE26` itself reads neither byte]. After the entity's projection (a failed projection draws nothing and clears `V:4A967`): **`V:4A86D` non-zero**: the direct path (`0x30142`): the sector (from `+0x68`) and the position nibbles are stored in `+0x71`, `+0x70` and the shape is drawn at the true position **only on frames with bit 0 of `V:4A993` set**. **`V:4A86D` = 0**: by the signed byte `V:4A90F`: **negative** (`0xFF`, stored by `0x119AC` in the **boot start-up state `0x11962`**, installed once by the main program's initialisation (`0x116AC`): it runs the reset block (which stores 1), clears the mode, stores `V:4A865` = `V:4A90F` = `0xFF` and hands over to the credits state `0x13596`; it is not the demo set-up. A demo started from the menu **after the boot** therefore still holds `0xFF` for its whole length [V: `combat-panel-first-demo.txt`, `V:4A90F` = `0xFF` in all 140 samples from the key to the flight and beyond] and shows no marks, while one started after a reset block (ESC, reset-to-menu) holds 1; the flight value is 1, see below): **no entity mark at all** (and, in `0x3001C`, no ship either); **0**: every frame the shape is drawn from the **stored** sector and a position pulled back to the stored nibbles (the new position minus the wrapped difference of the low nibbles, so a mark moves only when the true position differs from the stored one by 8 pixels or more), the store being refreshed only while the stored sector byte `+0x71` is negative (the first time); **positive m**: frames with (the whole word `V:4A992` & the sign-extended mask m) = 0 take the direct path **and draw nothing** (they only refresh the stored sector and nibbles), the other frames draw the stored mark as in the zero case. The ship shape (pen 3) is drawn through `0x3001C` on every frame, with no `V:4A993` test, whenever `V:4A90F` is not negative. **The stored-sector reset (`+0x71` negative):** the spawner writes `+0x71` = `0xFF` in the constants block of **every placement** (note 07 1.4, rule 17), so a freshly placed record, including a slot freed and placed again, takes the direct path (refreshing the stored sector and nibbles) at its first use; freeing does not touch it. A record that is **copied** from another (a missile launch copies the owner's first `0xA4` bytes, the ejection seat the player's) carries the source's `+0x70`/`+0x71`; the player's record has them cleared by the flight start (`0x1D216`) but then **`0xFF` is stored in `+0x71`** by `0x1D91C` (reached through `0x1D242` and `0x1D8B8`), so the player's first mark takes the direct path; the rearm `0x1F11E` clears the first `0xA4` bytes of the records 1 to 3 (their `+0x70`/`+0x71` become 0, overwritten again by the next launch's copy). **The stored-mark path is reachable in flight**: Shift+KP-Enter (the fixed camera, note 07 6.3) leaves the map open with `V:4A86D` = 0 and `V:4A90F` = 1, so the marks are then drawn from the stored sector and nibbles, refreshed (without drawing) on the passes with the mask bit clear. **`V:4A967`**: written only by the approach's target choice (`0x2DA7C`, `0x2DAF8`) and by the per-entity mark routine `0x30046` (clears it when a projection fails, sets it to 1 for the chosen hostile `V:4AA72`), and read only by the approach's stage 7 (`0x2DA0E`); in flight the routine runs in the map view alone, so the port need not model it (its writes are unobservable after the approach). **The mode 3 pen does not apply to the player**: the loop sends entity 0 straight to pen 13 (`0x2FE6A`: the test of the entity offset against 0 precedes the mode test), so the player's own mark stays pen 13 in mode 3 while every other record without bit 6 of `+0x20` is drawn in pen 3.

**Not produced by this routine (inference by exclusion).** The red (pen 1) and dark green (pen 11) marks seen in the frames (6 and 13 pixels at the lowest zoom, up to 98 for pen 11 at 0x1000000) and the pen 8 curves (the bay bridges) are not drawn by 0x2FC58 (it reproduces the frames' overlay pixels
exactly without them); their size grows with the inverse square of the altitude, so they are scenery polygons of the world renderer seen from straight above (note 04a), not map marks. The airport, hostile and other class marks are verified on synthetic entities only (see open question 6).

## Constants

| constant | value | where |
| --- | --- | --- |
| HUD glyph pen | 13 | `V:4AA0C` = 13 set by the HUD routines |
| HUD outline, crosshair, level marks, nose datum | pens 10, 10, 10, 8 | 0x38C02, 0x37DBA |
| view offset per step | 16 pixels | `V:4AA40` = 16 times `V:4AA3E` |
| HUD step table (14 signed bytes, one per head code 0 to 13) | the shape is in "Head views" (a ramp of equal steps u on both sides of the straight-ahead code 0, the hidden marker H on the codes whose HUD is off screen; code 4 carries the marker with the opposite sign); the bytes are `tables/head_step.bin`, FNV-1a-32 `1df64ecf`, exported by the extractor. The equal-row rule replaces the byte by H only for the codes 3 to 9, 12, 13 whose window row is the present one | 0x1E800, test 0x1E774 |
| head yaw table | 2256, 4800, 7200, 9600, 12000, 14400, 16800, 19200, 21600, 24000, 26544 | 0x3239A |
| window last row per code | 144 (0 to 2, 10, 11), 167 (3, 4, 8, 9, 12, 13), 179 (5 to 7) | 0x1E760 to 0x1E7F4 |
| panel picture, cowl | (0,145) 320x55; (16,133) 288x12, pen 0 transparent | measured |
| compass scroll | ((degrees / 2) x 135 >> 8) - 4, wrap +96 | 0x35B7E |
| ball scale | x: 24/256 plus 193; y: 31/256 plus 162 | 0x34C7C |
| message line | row 192, 26 characters from x 108, pitch 4 | 0x3706A |
| G readout | the single formula in "G readout" (v from `E+0x56`, `V:4A9FE`, `V:4A982`) | 0x36828 and following |
| lat, lon | 0x7000 and 0x5999 divisors | 0x36C96, 0x36D24 |
| fuel thresholds | 0x70800 critical, 0xBB800 low (on `E+0x72`) | 0x1565A, 0x1568A |
| camera ring unit | 1024 position units | 0x1D48E (matrix 16384, shift 4) |
| distance multipliers | x1, x1.5, x2, x4, x8 for levels -1, 1, 2, 3, 4 | 0x2D858 |
| map altitude | 0x1000000 to 0x8000000, step 0x2000000 | 0x1E55A, 0x1E5E0 |
| map scales, grid | 168, 252, 128 (`V:4AAF6`, `V:4AAF8`, `V:4AAFA`); 24 vertical lines at 0xF680 + 0x1C0 i, 16 horizontal at 0xFEB0 + 0x230 j (high words) | 0x2FC58 |
| map ship / player pens | 3 / 13 (other entities 13 with bit 0x0008, else 1) | 0x30046 |
| gear symbol masks | variant v: 0x42B1C - 0x1C v | 0x357F6 |
| fuel bar | 22 rows 159 to 180, lit min(22, max(0, level - 1)), level = (`V:4A9AE` and 0x7FFF) >> 10 | 0x353EC |
| radar blip shift | low nibble of `E+0x63` (13, 11, 9), round half up; scope offset (158,167) | 0x35CF2 |
| radar own-ship, cone | pen 12 plus at (158,167); pen 3 lines (154,163)-(139,147), (162,163)-(177,147) | 0x35CF2 |
| warning tick generator | counter `V:4A948` 8 down to 1; slow tick at 8 and 4, fast at 8, 6, 4, 2; reference-path pin (not a game constant): counter 5 after the first pass at n0 = 0, then free-running | 0x13C94 |
| target box, jitter | corners +-9, +-8 around the target point; clamp x 86 to 232, y 45 to 144 | 0x38D8C, 0x39048, 0x38FFC |
| camera aim step | per angle: the old angle plus min(difference >> 2, limit) toward the target the short way round; limit 2000 (25 degrees), 560 (7 degrees) in the approach camera modes (note 07 6.1) (`V:4A966` nonzero); snap when the look-at byte `V:4A86C` is negative, when `V:4A85E` and `V:4A966` are 0 (first external frame), when the ring key changed or the ring flag `V:4A86D` is 0 (every snap sets `V:4A85E` to 1); no aim at all when `V:4A86C` is 0; no update while paused | 0x1454A, 0x32106, 0x32126, 0x3211C, 0x3215C |
| aim target point | the target record's position plus the offset (0, 0, 1) [(0, 0, 5) in camera mode 6, (0, 0, -4) when the high nibble of the class byte is 3] times the nine words at +0x92 >> 4 (ring unit 1024 pu) | 0x320C0-0x320E6, 0x1D478, 0x1D488 |
| map altitude keys | +-0x2000000 per key, clamped to 0x1000000 .. 0x8000000 (a result outside is stored as the limit; the request is stored at the stop) | 0x1E55A, 0x1E5E0 |
| head-key window rows | the equal-row test applies to the codes 3 to 9, 12, 13 (rows 167, 179), never to 0, 1, 2, 10, 11 | 0x1E748-0x1E78C |
| head hold | `V:4A949` = 0xFF on a head key, 5 on the release of a head key or of `[` `]`, minus 1 (0x29946: not when negative) per change of the timer-device seconds long `V:4ABAA` seen by a drawn frame (0x297D6 at 0x11446; never in a pause), recentre by the next 0x141E8 when the byte is 0 (4 to 5 s plus a frame) | 0x1E724, 0x1E89A, 0x29882, 0x29946, 0x297D6, 0x141F6 |
| head yaw table, class 0x30 table | 0x3239A (11 triples, only yaw), 0x323DC (12 triples) | 0x32320, 0x3231A |
| head pitch pairs, 7 degree test | codes 12/13: 0x5690, 0x19F0 (pitch < 560 or >= 28240), else 0x5460, 0x1C20; class 0x30: 0x3610, 0x3840 / 0x230, 0 (6640 < pitch < 7760) | 0x322A6 to 0x32308 |
| ring unit, level, offset | out = (matrix x scaled words) >> 4; level 1: w + (w >> 1), levels 2 to 4: w << 1, 2, 3 | 0x2D858, 0x1D488 |
| ring height clamp | (deck word `E+0x4E` when byte `E+4` bit 6 or 7, else 0, + 7) x 256 | 0x2D8BC |
| map altitude | initial 0x3000000 (0x1D276); +-0x2000000 within 0x1000000 to 0x8000000 | 0x1E55A, 0x1E5E0 |
| cockpit zoom word `V:4AAFA` | halve above 0x20, double below 0x80 (initial 0x80) | 0x1E81E, 0x1E83C |

## Worked examples

State words come from `extracted/captures/logs/hud.txt` (script `openers-hud.txt`, note 01b section "Question 5"); the displayed figures are
read from the captured frames.

**Cockpit frame, runway idle (`cockpit-takeoff.ppm`, state `runway-idle`).** Entity: heading word
2240, pitch 0, roll 0, speed word 0, altitude long 1800, throttle byte 9, `E+0x56` 0, fuel long 6399880, class
0x11, `E+0x63` 0x0D. Displayed: HUD speed "0" at x 106, y 84, "KT" at 102 and 106, y 91; altitude "5" at x 226,
"FT" at 222 and 226; G "1.0" and "G"; tape: heading 28.0 gives h = 280, r = 80, base 30, s = -20, o = 4: labels "03" at x 160 and "02" at x 140 (the base plus 10 label is
not drawn because o exceeds 2), ticks at x 143, 153, 163, 173, 183 (two pixels tall at 143, 163, 183), centre
mark x 159. Panel: altitude "5", speed "0", throttle "7" (9 x 256 / 307), magnification "10", radar range 40
(`E+0x63` low nibble 13), fuel 24999, latitude 37.6 N and longitude 122.3 W, compass scroll 3, level ball, no
lamps lit, gear symbol variant 0. The whole 320x200 frame's HUD and panel pixels were reproduced exactly from
this state.

**In-flight frames (01b states `level`, `climb20`, `bank30`), reconciled.** The logged state is the frozen instant; the displayed frame is one or
two game frames older, and the panel digits are cached (see "Draw schedule"), so the figures differ in a predictable way:

| quantity | level | climb | bank |
| --- | --- | --- | --- |
| y (long `E+0x18`) | 706604 | 681120 | 711068 |
| 5 times (y >> 10) | 3,450 | 3,325 | 3,470 |
| HUD altitude shown | 3,435 | 3,255 | 3,450 |
| panel altitude cache `V:4A9BA` / panel shown | 3,410 / 3,410 | 3,225 / 3,225 | 3,425 / 3,425 |
| speed word `E+0x6E` (logged) and /12 truncated | 4726, 393 | 4422, 368 | 4751, 395 |
| speed cache `V:4A9B0` (flag bit 15 removed) and /12 truncated | 4684, 390 | 4333, 361 | 4741, 395 |
| HUD speed shown / panel speed shown | 390 / 390 | 364 / 357 (see below) | 395 / 395 |
| `E+0x56`, `V:4A9FE`, bit 1 of `V:4A982` | 14, 0, 0 | 24, 0, 0 | 8, -73, 0 |
| G from the logged state, formula above | 0.9 | 0.7 | 1.8 |
| G shown | 0.8 | 0.5 | 1.7 |

In the climb column the panel speed 357 does not match the cache word 4333 (361): 357 corresponds to a speed word of 4284 to 4295, older than the
cache at the logged instant, and the HUD figure 364 (from the current word, 4368 to 4379) is newer. The cache is written by the drawing routine at the moment it draws (see "Gating a replica must keep"), so the cache word 4333 is the figure
that routine last drew and the panel should show 361. That the panel shows 357 is therefore not a stale panel versus a newer cache; the two candidate
causes are the position of the sampled frame between the cache store and the visible frame (the digits are drawn into the page that is shown a frame later; inference) and the
logged cache having been read a frame after the captured panel frame. The exact frame that drew 357 was not logged; this one discrepancy is open.
The altitude word is refreshed every 4 steps with an occasional burst (01b: lag 0 to 5 steps): the panel figure equals the cache exactly in all three frames, and
the HUD figure, drawn from the current y each frame, trails the logged y by one frame (about 43 feet per frame at 360 knots in a climb:
3,325 against 3,255 is 70 feet, one and a half frames). Speed truncates: the real routine run on 4231 shows 352 and on 4271 shows 355
(note 01b's observed 353 and 356 are the frames after, from a later speed word; the unit is 12 per knot, velocity 4 position units per frame per
unit of the word). The G figure is one to two tenths off because `E+0x56` is the pitch rate of the current step and the displayed figure
is a frame older; the formula itself reproduces 1.0 at rest and the
sign rule (blank sign for negative v).

**External frame (`heading-1-hdg.ppm`).** Displayed: the heading 028, the speed 357 and the altitude 3 165, each with its unit label, on rows 184 to 188, pen 10 on
pen 3. The state is not logged for this flight frame; the readouts imply a heading word of about 2240 (028 degrees),
speed word about 4284 (357 x 12) and altitude long about 648192 (3165 / 5 = 633, times 1024). The camera of a
similar external frame on the runway (snapshot `ext-L-1`, heading 2240): level -1, direction 4, offset from the
aircraft (8653, 3072, -16275), aimed at the aircraft (pitch 720, yaw 2240 in `V:4AB4C` and `V:4AB4E`).

## Extractor needs

Data the port needs from the executable (clean-room: nothing is committed; the extractor writes it under `assets/`): the warning table at 0x41FF0
(54 entries of 28 bytes: flag byte, 26 characters, one flag byte); the hunk 62 masks: gear lamp mask 0x42AAC, four gear symbols 0x42AC8, 0x42AE4,
0x42B00, 0x42B1C (7 rows of 2 words each), ball mask 0x42B38 (21 rows of 2 words), second ball table 0x42B8C (8 rows of 4 bytes), compass strip
0x42BAC (7 rows of 12 bytes); the HUD outline polyline at 0x3901C (six word pairs, 4 bytes apart) and its pens at 0x39034; the target marker point
lists 0x3948C (12 pairs) and 0x394A6 (44 pairs) and the lock marker arcs 0x39408 (42 bytes) and 0x39432 (40 bytes); the target box segment table 0x39048 and jitter pairs 0x38FFC (8 signed byte pairs); the four gear mask pointers at 0x357F6 are only an order (variant v uses 0x42B1C - 0x1C v); the 16 map direction shapes at 0x2FED8 and the ship shape at 0x30012 (triples ended by 0x80); the HUD step table 0x1E800 and the head yaw table 0x3239A
(already given in this note as numbers); the glyph table is already `assets/fonts/hud.bin`.

## Reproducing

- `cockpit-ground.vasnap`, `airborne.vasnap` and the other restore points are made by the scripts in `docs/re/01a-reference-captures.md` (`freeflight-takeoff.txt`,
  `freeflight-headings.txt`); the logged states (`hud.txt`) by `tools/vamiga-ref/scripts/openers-hud.txt`; the external, map and head snapshots and frames by
  `views-ground.txt` and `views-airborne.txt` (same directory; output `extracted/captures/views/`, git-ignored). The names used in this note:
  `ext-L-1` (first external frame), `e-L1` to `e-L4` (levels), `e-L4-KP*` (directions), `tower-1`, `tower-2`, `tower-low`, `c-KP6-1` to `c-KP6-4`, `c-KP7`,
  `c-KP1`, `c-KP2` (head codes), `a-ext*` (airborne).
- The emulator-plus-blitter harness is `tools/hud-emu/` (Python, needs `unicorn` and `Pillow`): `emu3.py` (memory, hooks), `blit.py` (blitter model),
  `frame.py` (the frame schedule), `verify_hud.py` and `verify_panel.py` (comparison with the openers frames and `hud.txt`), and for the addendum `verify_addendum.py`, `verify_map.py`, `verify_f16.py`, `verify_warn.py`. Run from the repository root with
  the `extracted/` files present. Its scratch outputs stay in `extracted/scratch/`, which is git-ignored. The comparison is a pixel equality test, not a rig test.

## Open questions

Settled in the addendum round (in the sections of the same names): the F-16 scale form, the radar blip shift, the own-ship mark and height rule, the lock marker and cue shapes, the warning line choices (tick phase, holding, expiry, external views, pause), the map renderer
and its marks, leading digits and gun rounds, the gear symbol order, the fuel bar, the pen 3 colour. What remains:

1. HUD y offset words `V:4A990` and `V:4A9D0`. Answered in the addendum of this round (note 08 sections 8.10 and 11.6): neither the demo nor the training scripts set them. Their live writer is the **jolt routine** `0x29A5E` (per frame, in the draw path): a one-byte kind in `V:4A878` (0 none) selects one of four 4-entry byte tables (`0x29A12`, `0x29A1B`, `0x29A24`, `0x29A2D`: the offsets -1, +1, 0, 0 for the kinds 1 and 3, +1, -1, 0, 0 for the kinds 2 and 4; read as bytes at `0x29A14`, `0x29A1D`, `0x29A26`, `0x29A2F`), the index `V:4A947` steps once per call, and each offset d is stored as `V:4A990` = d, `V:4AA3C` = d + 0x90 (the window row) and `V:4A9D0` = 40 d (one bitplane row of bytes per scanline); after the fourth entry (or when the absolute value of `V:4AA3E` exceeds 14) the kind is cleared, `V:4A947` cleared, the bit 0x0400 of `V:4A984` cleared and the offsets reset (`0x29AE8`-`0x29B02`). The kinds are set at the touchdown (`0x16F7E`: 1), the lift-off (`0x171D4`: 4), a landing after a stop (`0x170E8`: 3), a take-off roll event (`0x1706E`: 4), a hard touchdown (`0x1683E`: 3) and a hit (`0x2A8A8`: 1) [R]. So the whole 3D window moves one scanline up and down for four frames at every touchdown, lift-off and hit; the HUD rows follow it. Not captured with a frame; not in the port (read only: Task 9 / a milestone-3 follow-up). The interactive toggle `0x1DE8A` of the same words has no caller (dead code). Method for a capture: stage a touchdown (a poked deck contact) and log `V:4A990` per blank. **Closed by note 07 section 13.3**: the routine and its tables are checked in the rig (`jolt_check.py`, 8,570 calls, 0 differences) and captured on the reference emulator (`combat-fx-jolt.txt`: poking the kind byte of the airborne snapshot gives the index 1 to 4 over four passes, `V:4A990` = -1, +1, 0, 0, `V:4AA3C` = 143, 145, 144, 144, `V:4A9D0` = -40, +40, 0, 0; a pass is about 6.4 video frames); the jolt runs only when |`V:4AA3E`| is at most 14 (the cockpit view with the panel), the kind byte is cleared in the full-width views; the writers are the touchdown `0x16F7E` (kind 1), `0x1683E` and `0x170E8` (3), `0x1706E` and `0x171D4` (4) and the hit `0x2A8A8` (1).
2. (Partly closed by note 07 section 12.9) Warning entries 20 and 53 have their posting sites (entry 20, `0x9014`, is the engine failure posted at `0x2A87C` when the frame counter is even at a contact above 80 knots with no decoy cool-down (note 07 10.4: it is not the first hit); entry 53 is posted at `0x27480` by the training prompt for the fourth manoeuvre record when the height is at most `0x100000`); entries 8, 32 and 34 are posted nowhere in the image (complete scan of the 14 sites of `0x29BC8`, the call of `0x13D00` and the arbiter's nine codes). `V:4A980` bit 15 and `V:4A98E` bit 5 still have no located writer; the writers of `V:4A95E` are all listed in note 08 section 5.10 (the value 2 is the training demonstration, stored by the menu key 3 through `0x1EB18`; the value 0x7D the practice, stored by key 4 and by ESC in mode 2 at `0x11AF8`). The first-frame tick phase of the original is undefined (a count of generator passes since boot, see "Ticks and blinking"); the port pins it to the reference path (counter 5 after the first pass at n0 = 0), which is a choice forced by the missing original value, not a measurement. What would settle the original's value: nothing, it depends on the time spent in the menus. No capture logs `V:4AB96` per frame: a stall or fuel run sampled each frame would let the blink sequence be compared with a capture (the harness sequence is verified against the code only).
3. (Closed by note 07 sections 7 and 12) Target symbols: the firing conditions are now read and run in the rig (`hud_target_check.py`): the lock state `0x38884` (600 runs), the fire prompt, pipper, circle, range dot and range number of `0x385F4` (600 runs), the gun pipper history `0x3878E` (the point 256 units ahead and 24 up, projected two calls later; `V:4A7CE`, `V:4A7DA`; 200 runs) and the writer and unit of `E+0x4A` (the player's `0x2835A`: fine units of 256 position units, 1.25 ft; 400 runs). The range number is the change of `E+0x4A` per update, with a space for a negative change and a minus sign otherwise (note 07 12.7). The emulator capture of a designated target is `combat-radar-demo.txt` (note 01a); the pixel comparison of a frame with a designated target is not done for the symbols; the range number's screen position was captured (note 07 12.7).
4. (Closed by note 07 section 12) The radar list builder (0x1FF24) and the scope walk's detection, threat bits, cues and designation are note 07 section 12 (rig: `radar_check.py`); this note fixes the output (16-byte entries, scale shift k) and the picture.
5. The role of the second ball table 0x42B8C (see the ball): no visible effect found.
6. Map: marks of other classes (airports, hostiles) are verified on synthetic entities only (the tower captures hold the player and the carrier); mode 3 (`V:4A95E` = 3) is the demonstration's flight mode (note 08 section 7.5: stored at `0x11C32`); the demo snapshot reads `V:4A90F` = 0xFF (-1, the flight value), `V:4A865` = 0, `V:4A83D` = 2 (`flow-demo-map.txt`); the paths with `V:4A90F` = 0 or 1 are the camera modes 5 and 6 of item 11 (note 08 6.4). No capture of the map view inside the demo was made: the shipped stream has no map toggle (event bit 0x0100 of the second word is never set in its 2046 played frames, note 08 7.3). The red, dark green and grey scenery in the frames
   is inferred to be world-renderer polygons (run the polygon renderer over the tower snapshots, or find them in the scenery tables of note 04a).
7. (Settled, round M3-T4) The aim arctangent is the routine 0x1454A with its tables, now described completely in note 03 ("External camera aim") and checked in the rig against a model (6,000 vectors, 0 differences): the replica runs that rule; the earlier advice to use the exact angle is withdrawn. Still open for the views: the fixed-camera view of shift + KP-Enter (0x1E3EE, tables 0x47ADE and 0x47B30, not captured) and the camera modes `V:4A966` 1 to 4 and 6 (the missile, fixed and chase cameras of the state machine at `V:4A86E`: 0x2D8E8 onwards) were read only as far as the keys' gating; `V:4A869` (the 8 degree glances) has no writer (settled: dead, "The glance byte `V:4A869`").
8. The palette levels 2, 10 to 12 and 14 of the emulator's colour curve are harness values, not observed on a HUD pen (levels 9 and 13 are observed on pen 9). Level 15 is measured on the crash-hold flash frame (`extracted/captures/flash-e6.ppm`, uniformly (236,159,0) for the palette word 0x0FB2): the red channel at level 15 maps to 236, not the harness's 255, and no 255 appears in any capture; whether a grey pen 15 (0xFFF) maps to 236 as well is unproven. The port uses 236 for the flash only and keeps the curve's 255 for pens (the `M4` table in the harness still says 255) (the frontend palette code applies the measured curve; see "Pen 3 and the emulator's colour curve").
9. F-16 scales: altitudes above 27,300 feet were not swept (the 16-bit quotient would fail above 327,680 feet, unreachable), nor a nonzero `V:4A990` or view offset on the scales.
10. Whether the map view's depth scale `V:4AAFA` (128 at flight start, written at 0x1D2FA and reset by 0x1F06A) follows the magnification zoom, which halves and doubles the same word (0x1E826, 0x1E846): the tower snapshots all hold 128. `V:4AAF6` (168) and `V:4AAF8` (252) are written only at flight start (0x1D2EA, 0x1D2F2); 0x2FC58 itself stores to none of the three. Method: set a zoom in a snapshot and run 0x2FC58.

11. (Closed by note 07 sections 6.1 to 6.4) Camera modes 1 to 4 and 6: mode 1 is never written, modes 2, 3, 4 and 6 are the front end's approach cameras (the stage machine `V:4A86E`, `0x2D8E8`; `camera_check.py` 0 differences in 800 random cases), none follows a missile or a wreck; Shift+KP-Enter, the fixed camera `0x1E3EE` (tables `0x47ADE`, `0x47B30`), is rig-checked (200 cases); the 0xFF value of `V:4A86C` is the look-at snap (aim limit -1) that the training starts write (`0x12198` for mode 2, `0x12212` for mode 0x7D; note 08 6.1 item 7), not a crash state. Settled in the milestone-6 sweep: the glance triples of `V:4A869` are dead (below, "The glance byte `V:4A869`"). Still open here: the sound stop of M and of the view switch, and the vector words `V:4AD02..` that M clears (their reader is the camera mover: the previous damped step, note 07 6.2).

## Verified by

- **HUD, pixel exact.** The real routine 0x37D78 was run in an emulator over the memory of
  `cockpit-ground.vasnap` and the blitter modelled; every changed pixel (516 of 516) equals the pixel of
  `cockpit-takeoff.ppm`. With logged states from `extracted/captures/logs/hud.txt`: runway idle 318 of 318
  pixels and carrier idle 349 of 349 equal the captured frames; in flight (level 359 of 363, climb 357 of
  365, bank 306 of 364) the remaining pixels are digits and tape labels that belong to the previous frame
  (the display lags the logged state by one frame).
- **Panel, pixel exact.** The schedule above run with the redraw counters on reproduces the whole panel of the
  runway-idle frame with zero differing pixels (the HUD included), and the carrier-idle panel except two
  radar pixels. `pix/inst5` placed at (0,145) with pens 9 and 11 swapped matches the capture except the
  dynamic pixels (239 of 17600); `pix/frnt5` at (16,133) matches 1939 of 1953 non-zero pixels.
- **Head offsets.** `head-left.ppm` (code 11) reproduced with offset step 7: HUD, cowl and panel shifted by 112
  pixels. Snapshots of codes 1 to 4, 6, 12, 13 gave the steps, offsets, window rows and camera values above.
- **External strip.** The three routines run on the external snapshot `ext-L-1` over a blanked strip reproduce rows
  183 to 189 of its captured frame exactly (0 differing pixels; 133 text pixels, pen 10 on pen 3).
- **Heading tape.** The model in the section matched the running routine for 347 headings (every 11th tenth of a
  degree plus the boundary cases), labels and ticks, with no mismatch.
- **Readout formulas.** Speed, altitude, throttle, fuel, latitude and G swept in the running routines
  (values listed in the tables); fuel bar sweep (levels 0 to 24), compass scrolls and the window match, lamp pen changes, message
  line positions and pens measured on the real routines; ball fill rows for four polygons; weapon station repaints for six stock combinations;
  cell repaint tests (zero residual).
- **View kinds.** Reference-emulator snapshots with a stationary aircraft: levels -1 to 4, all ten directions
  (`V:4A96A` read after each key), camera minus aircraft offsets, camera angles; the full-matrix test on the airborne snapshot (directions 4 and 0 equal the matrix
  prediction exactly, directions 2 and 8 do not); the map view at three altitudes (frames and `V:4ACFA` values; grid spacings measured from the frames); cockpit head
  codes 1 to 4, 6, 12, 13.
- **Note-fix round (views).** Rig, `tools/m68k-rig/tests/lists_model/view_probe.py` (seed 1, 600; about a minute, `sqrt` adds the exhaustive range): `point` the aim target point (600 random states, classes 0x10 to 0x3F, camera modes 0 and 6, shift 4, 0 differences; shift 6 would differ in 373), `atan` the example (-1000, 0, 1000) (index 333, pair (0, 72)) and the index range (largest 511, 537 of 100,000 above 260, no divide overflow), `sqrt` (0 to 8,400,000, largest quotient 2,898), `snap` (`V:4A85E` on the snap paths), `paused` (23 keys through 0x1DAA0 with `V:4A866` clear and set: the same changed bytes except E and P), `map` (M in the cockpit and an external view, the eight altitude clamps), `step` (252 cases of code, key and present window row; the zoom reset), `sites` (0x1431A, 0x143C4, 0x1EC14), `extlongs` (0x1F45E in an external view, 200 states); 0 differences. Emulator (scripts `views-m3-levels.txt`, `views-m3-fix.txt`, `views-m3-fix2.txt`; logs `extracted/captures/logs/views-m3-*.txt`, git-ignored): the snap targets and per-pass sequences of the distance levels, KP3 and KP-dot in the cockpit, M in an external view (`V:4A8EB` unchanged), the keys in a pause (hook, brake, H, gear on the airborne snapshot, ESC), the hold byte through a 10 s pause and after it, the hold byte per 5 video frames against the seconds long, the map keys `[` and `]` with the clamps and the requests, the bracket releases in the cockpit, an external view and the map. `tools/hud-emu/verify_views.py` now checks the map camera above the viewed record (the former height comparison compared an address with itself) and the ext-L-1 target point.
- **View rules (round M3-T4).** Rig, `tools/m68k-rig/tests/lists_model/view_check.py` (seeded, runs in about a second; needs `extracted/interceptor.bin` and `librig.so`): `head` (0x32074 over 600 random records, all classes and head codes: the camera words equal 0x325B8 fed with the head triple of the rule; 0 differences), `ring` (0x2D756: position, class tables, levels, the height clamp, 600 states, and the heading-jump ring rotation, 12 cases; 0 differences), `aim` (0x1454A: the limited step, 3,000 states, and the first-frame snap, 500 cases; 0 differences), `target` (the exact target angles, 6,000 vectors per seed (n x 10 with n = 600), model of note 03; 0 differences), `caller` (the 0x32074 external branch: limit 2000 or 560, snap on first frame, ring change, `V:4A86D` clear, no update when paused; 960 states; 0 differences), `keys` (0x1DAA0: 112 head-key presses over the 14 codes, the zoom word sequence, KP-Enter and M; 0 differences). Emulator (scripts `views-m3-heads.txt`, `views-m3-map.txt`, `views-m3-pause.txt`, `views-m3-airborne-heads.txt`, `views-m3-hold.txt`; logs `extracted/captures/logs/views-m3-*.txt`, frames `extracted/captures/views3/`, all git-ignored): the head hold timer (`V:4A949` after a KP6 press and release), KP4 and KP6 from codes 12 and 13 (code 0 in four of four), KP9 and KP5, M from the cockpit and from an external view, M twice, KP-Enter in the map, the cockpit zoom keys, the external levels 1 to 4, the pause keys (P, KP-Enter, KP6, M, `]`, P) and their deferred view switch, `n` per blank around P, the first external aim frames after a staged KP-Enter (the aim goes from the stale pair to 720 and 2240 in one pass), and the head composition on the airborne snapshot (7 codes, 7 equal to the rig).
- **Addendum rules (the second round).** Four scripts in `tools/hud-emu` (inputs listed in their docstrings and in `tools/hud-emu/README.md`; each exits 1 on a failed check and skips with exit 0 when an input is missing). All pass.
  `verify_addendum.py`: gear: the pointer table at 0x357F6 read from the image, the first row (189 for variant 3, 190 for the others), height (4 to 6 rows) and pixel count (25, 24, 25, 27) of each of the four variants equal to the mask bits, and the box and symbol of variant 0 equal to `cockpit-takeoff.ppm`;
  fuel bar: nine digit values from 0 to 32,767 give lit rows min(22, max(0, level - 1)); view code 1: the fuel region (bar, digits, box) of `views/c-KP6-1.ppm` is identical to the harness frame (19 lit rows come from the picture because the bar routine returns at that offset);
  leading digits: gun rounds (0, 5, 50, 500, 1234, 0xFFFF), panel speed (0 to 12,000) and the G readout (four values); radar (0x35CF2 over the cockpit-ground RAM with synthetic list entries on a blank bitmap): own-ship plus and cone line pixels and the frames that draw them (twelve combinations of the redraw counter and `n`),
  blip position over five range nibbles with 15 vectors each (rounding half up, window edges), blip width against `V:4AB30`, the pens for ships, subs, friendly, missile, hostile and hidden records, the cap of 10 blips and the target blink on `V:4A93B`; target box pixels (68, pen 10), marker slew (several starts, no weapon, axes), diamond and ring shapes and their draw windows and blink, lock circle (74 pixels), range dot offsets at k = 0, 19, 38 and the x mod 16 case, and the three arc lists in the image.
  `verify_map.py`: 0x2FC58 over the three tower snapshots gives 898, 521 and 2140 overlay pixels with 0 differences from the captures, plus marks on synthetic entities. `verify_f16.py`: the F-16 scales, 2,731 states, 0 mismatches. `verify_warn.py`: the code locations of generator, arbiter and the frame counter; the seeded tick sequence for n0 = 0 to 7;
  the counter-against-n relation on every flight snapshot (37 of 38); the duration ranges over all eight phases (D = 8: 32 to 35 passes, D = 0: 0 to 3); the D = 6 fuel alert (six blink cycles, then steady), the D = 8 alert with its partner entry, a posted 0xD00A, and the pause; the frame logs of twelve scenarios (stall, fuel, timed information, switches, post over alert) are printed by name and were read, not asserted line by line.
- Not scripted, measured interactively in the harness: the lock circle's clipped counts outside its window; the sweeps of every other readout (characterised in the tables).
- The radar flag rules (threat bits), the target symbols' firing conditions and the posters of the arbiter's entries are now run or scanned and recorded in note 07 section 12 (rig: `radar_check.py`, `hud_target_check.py`). The F-16 scales' arithmetic is verified (above), not read only.
