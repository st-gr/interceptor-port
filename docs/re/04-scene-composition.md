# Scene composition, frame loop and view kinds

Scope: how the program starts, what one pass of the main loop does and in which order, how
the frame is paced, the screen set-up (view, viewport, bitmaps, palette, double buffering),
how the 3D window is laid out and filled, and how the point-of-view keys place the camera.
Addresses are in the relocated flat image (base `0x10000`, see `00-binary-layout.md`);
"hN+0xOFF" means hunk N and offset. Variables live in the BSS-like hunk 71
(`0x4A6E8`-`0x4D43B`, zero-filled and used as variables), written `V:xxxxx` below with the absolute image address; hunk 1 and 2
hold the C globals.

Provenance: addresses and call targets were read from the corrected relocated image
(`interceptor.bin` since commit f1ffbb6), partly with a Capstone disassembly, and the
data variables and structures were checked against the memory of the running game in
vAmiga snapshots (see Verified by). Hunk 71 is used as variables (the layout note lists it as CODE).

## Location

| what | where |
|---|---|
| C start-up (entry) | h0+0x0000, `0x10000`; opens dos.library, calls the C runtime and main |
| `main` | h0+0x03CE; opens graphics.library, shows the splash, loads panel pictures, then calls the game routine |
| game routine (init, menu, mission loop) | h0+0x7D48; the endless world loop is at h0+0x7EE6 |
| graphics set-up (view, viewport, bitmaps, planes, colour map, first copper lists) | h0+0x7F04 |
| page flip / LoadView | h0+0x827C; second copper-list builder h0+0x8226 |
| per-frame routine | h0+0x1124 (`0x11124`) |
| device set-up: timer h0+0x8E28, keyboard h0+0x8CDC, gameport h0+0x8E9C, system-time read h0+0x8E54 | hunk 0 |
| vertical-blank interrupt server (installer h0+0x95A6, handler h0+0x92DE) | hunk 0 |
| frame-rate limiter and 10-second flag | h20+0x562 (`0x297D6`), rate meter h20+0x6DA, limit table h20+0x27E |
| mission elapsed-seconds accounting (timer.device seconds) | h0+0x1748 (`0x11748`), start h0+0x2EA4, resume h0+0x3BDC, h0+0x6F64, h0+0x71E8 |
| audio master-volume ramp (called every blank) | h20+0x238 (`0x294AC`) |
| key event reader and dispatcher | read h0+0x8DA6, dispatch h5+0x15C (`0x1DAA0`), event executor h5+0x10 (`0x1D954`) |
| cockpit-view and external-view keys | h5+0xB68 ... h5+0xDC2 (`0x1E4AC`-`0x1E706`) |
| tower-view key ('M') | h5+0x1374 (`0x1ECB8`) |
| camera placement | h26+0x90C (`0x2D740`), offset tables h26+0x830, 0x86C, 0x8A8, 0x8E4 (four classes, 10 entries of 3 words = 60 bytes each), tracking branch h26+0xAB4 |
| fixed camera constants | h4+0x254-0x290 (`0x1D3A4`-`0x1D3E0`) |
| sky fill / ground polygon | sky h36+0x8FC (`0x3486C`), ground h33+0x3A (`0x32E62`, entries `0x32E5A` and `0x32E52`), selector h33+0x10 (`0x32E38`), crossing search h33+0x424 (`0x3324C`), polygon filler `0x34CD6`, edge lines `0x3508A`; rules in "Sky fill and ground polygon" |
| side flag, sea-zone flag | `0x143F2` (h0+0x43F2); `0x230B8`/`0x230F8` (h12+0x3C0, h12+0x400) with the table `0x23188` |
| sea mesh pass (sea polygons) | h28+0xB9C (`0x2F19C`; passes `0x2F25A` coarse and `0x2F234` fine; common part from `0x2F27C`); tables: offsets `0x2E600`, scan lists `0x2E632`-`0x2F19B`, distances `0x2F4F8`; grids `0x47D7C` and `0x47F40` and the cell records to `0x495CB` (hunk 68); the second sea-zone table `0x233B8` |
| sun | h33+0x3CE (`0x331F6`) |
| object and terrain display-list walker | h8+0x84C and h8+0x85E (`0x1FA24`, `0x1FA36`) |
| altitude to visibility table | h65+0x0 (`0x4621C`) |
| raw-key to character table (text entry, not view keys) | `0x37C92` (hunk 39), the "key-row string at 0x37CA2" of the layout note is its second row |

## Behaviour

### Start-up and the world loop

1. `entry` saves the registers, records the command line, reads ExecBase, finds the task
   and opens dos.library; `main` follows.
2. `main` opens graphics.library, runs the C runtime start, allocates, then calls a routine
   that decodes IFF pictures from `df0:pix/splsh` (splash), `df0:pix/inst5` and
   `df0:pix/frnt5` (the instrument panel pictures; the names are in hunk 1). Pictures are
   5-bitplane 320x200. The display is switched on only after the first one is decoded
   (WaitBOVP, LoadRGB4 of 32 colours, then OwnBlitter, a blit to copy the picture, a wait for
   the blitter and DisownBlitter).
3. The game routine (h0+0x7D48) then: frees sprites 0-3 (the game uses no hardware sprite and
   keeps freeing sprite 0 at many points), queries free chip memory, allocates 7000 bytes and
   a 78-byte player record, calls the OS-device set-up (below), runs the title and menu
   routine at h0+0xFAC, and finally enters the world loop.
4. Device set-up (h0+0x1628): timer.device (unit 0, microsecond clock), keyboard.device,
   a priority of -128 for the task named input.device, gameport.device (unit 1, the joystick
   port, trigger set to key-up and key-down events), the mouse and joystick reset
   (mouse limits +-960 on both axes), then the vertical-blank server.
5. World loop (every iteration, forever; the program ends only through the runtime exit):
   1. select the draw page (h36+0xC8, see Double buffering);
   2. OwnBlitter;
   3. the per-frame routine h0+0x1124;
   4. DisownBlitter;
   5. the page flip h0+0x827C: wait for the bottom of the viewport (WaitBOVP), make the other
      copper list current and LoadView; palette fade steps are applied here and in the
      interrupt server.

### Frame pacing and tick rate

- The simulation is not locked to a clock tick. One loop iteration is one simulation step
  and one rendered frame, and the step is not scaled by elapsed time. Evidence (static,
  complete): every reference in the image to the timer samples `V:4ABAA` and `V:4ABAE`
  (the seconds and microseconds from timer.device, read by h0+0x8E54) lies in the clock and
  limiter code listed below, the pause/resume bookkeeping and the mission-seconds
  accounting; none lies in the flight, weapon, AI or object code (hunks 4 to 40 except
  hunk 20's limiter). The vertical-blank counter `V:4A82C` has no reader, no code reads the
  CIA timers or the beam counters, and the per-frame elapsed time (`V:4AB9E`, `V:4ABC2`) is
  only displayed. So every rate in the simulation is per frame. Not checked by a
  two-snapshot position comparison (see Verified by).
- All consumers of real time: (1) the frame-rate limiter and the frame-rate figure;
  (2) mission elapsed seconds: whole timer.device seconds, sampled at mission start, pause,
  resume and landing (h0+0x1748 adds the seconds since the last sample to the player record
  when the difference is between 0 and 18000; the other four sites take the samples), so time
  spent paused is excluded; (3) a millisecond accumulator in the limiter (`V:4ABC8`) that
  raises bit 0x40 of the flag word `V:4A986` every 10 000 ms (consumer not traced);
  (4) the vertical-blank server (mouse, palette fade, volume ramp: the only 50 Hz work).
  The mission clock therefore counts timer.device time, not frames.
- Port rule: run one fixed simulation step at 8 steps per second (controller's ruling; the flight-only mean
  measured on the reference machine is 7.97 steps per second over the four flight scenes of 01b "Simulation step rate", which range from 6.6 to 9.3
  by scene; the parked-after-crash row of 5.6 and the crash freeze are not flight; the 15 Hz limiter ceiling is never reached; see note 05 section 14).
  The original's 4 to 5 frames per second in heavy scenes only slowed the game in real
  time. Keep mouse, fade and volume ramps on a 50 Hz tick and the mission clock on wall time
  (or 8 steps per second).
- The only wait inside the loop is the frame-rate limiter (h20+0x562; it is called at
  stage 1D4, not by the mission clock). It reads the system
  time from timer.device (command 10, get system time), computes the average milliseconds
  per frame over the current window of 1 to 8 frames (the window restarts every 8th frame,
  whose number is `V:4A992` modulo 8) and busy-waits, re-reading the clock, until that average is at
  least the limit table entry for the limit setting `V:4A976`. The table is -1 (no limit) for setting 0, then 1000 divided by n for setting n. The setting is
  initialised to 15 (h4+0x17E), so the limit is 67 ms per frame: a ceiling of 15 frames
  per second.
- Display: the flip waits for the vertical blank of the viewport (50 Hz PAL, 312.5 lines),
  so each frame is shown for a whole number of vertical blanks.
- Vertical-blank server: an interrupt server on INTB_VERTB (level 3, vector 5, node type 2,
  priority 0) installed at h0+0x95A6. At every blank it reads the mouse counters
  (JOY0DAT, `0xDFF00A`), turns the change into a clamped pointer position (`V:4A82E`,
  `V:4A830`, limits set at h0+0x9254), increments a tick counter `V:4A82C` (nothing reads
  it), steps the palette fade (16 colours from a table of 16 fade levels, one level per one to two ticks) and calls
  the routine at h20+0x238 (`0x294AC`), which moves a master-volume cell (`0x54FBE`) by 0x4000 per
  blank toward a target (`V:4A720`) between 0 and 0x3F0000 when the flag `V:4A88F` is set; the
  audio helper at `0x55280` limits every channel volume to its high word (0..63). It is the
  only fixed 50 Hz game logic besides the mouse and the fade. The frame counter `V:4A992` advances once per
  frame in the frame routine (not while the pause flag `V:4A866` is set).
- Measured under Kickstart 1.2 (attract-mode demo, carrier scene, snapshots 8 s apart):
  `V:4A82C` advanced 382 and 395 ticks over 7.58 s and 7.96 s of system time (50.4 and
  49.6 Hz, consistent with the PAL vertical blank); `V:4A992` advanced 32 and 40 frames in the same spans, that is 11.9
  and 9.9 blanks per frame (about 4.2 to 5 frames per second). The game therefore
  ran between about 4 Hz and the 15 Hz ceiling depending on scene cost (see the port rule above).

### Screen set-up

- One View and one ViewPort (`InitView` once, `InitVPort` once): there is no separate
  viewport for the instrument panel; the panel is part of the same 320x200 bitmap.
- ViewPort: width 320, height 200, lores (modes 0), DxOffset 0, DyOffset -2; View
  DxOffset 0x81, DyOffset 0x2C (read from live memory). Bitmap row 0 therefore starts at
  PAL line 42; in the 912x313 reference frames bitmap row r is frame row 42+r, and the
  viewport spans frame columns 186-825 (640 columns = 320 low-resolution pixels). The last
  bitmap row 199 appears at frame row 241 (measured on the cockpit capture), so row 0 is frame row 42.
  `01a-reference-captures.md` uses game y = frame row - 43, that is bitmap row - 1: its
  y 0..166 are bitmap rows 1..167. Use the bitmap row as the one coordinate system (row 0
  is cleared and never part of the window; the window is rows 1 to the row limit).
- Bitmaps: bitmap A is 40 bytes per row, 200 rows, depth 5 (32 colours), bitmap B is 40 x
  200 with depth 4. Plane memory is 8000 bytes each from `AllocMem` in chip memory,
  cleared. One colour map of 32 entries is created; the palette is loaded with LoadRGB4
  (viewport, table, 32) at start and through the fade routine (16 colours) afterwards.
  Only one viewport exists, so there is one palette load target.
- Double buffering: two separate plane sets (read from the snapshots). Page 0 (bitmap A)
  has five planes of its own (plane memory `0x4B2D8`, `0x4D218`, `0x4F158`, `0x51098` and a
  fifth at `0x52FD8` in the demo run); page 1 (bitmap B) has four planes at `0x10368`, `0x122A8`,
  `0x141E8`, `0x16128` (the planes of the decoded cockpit picture, installed by h0+0x806; the
  first plane tables are initially copies of A's, which is why the set-up code looks shared).
  The two copper lists were each built once (MakeVPort and MrgCop at h0+0x7F04 and h0+0x8226):
  list 0 shows page 0, list 1 shows page 1. The flip shows list `V:4A724` and then sets
  `V:4A724` to 1 minus its value; h36+0xC8 then picks the drawing planes by the new value, so the
  world is always drawn into the page that is not on display, and the displayed page is never the
  one being drawn. The drawing plane tables are `V:4A726` (planes 3 down to 0 of page 0, then of
  page 1) and `V:4A746` (five pointers per page, B repeating its plane 3), copied into `V:4A76E`
  and `V:4A772`; the world is drawn into the four planes only.
- Five planes versus four: plane 5 of page 0 is all zero in flight and palette entries 16 to 31
  are black (both read from the snapshots), so the depth difference changes nothing visible in
  flight: every frame is 16 colours (entries 0 to 15), and both pages hold full frames. Whether
  plane 5 or colours 16-31 are used by the menu or splash is not checked. The SDL core can ignore
  plane 5 and treat both pages as one 16-colour 320x200 surface.
- The row 0 of the four drawing planes is never cleared by the sky fill (it starts at row
  1); the frame routine clears row 0 of all planes on every 32nd frame (h36+0xF2).

### 3D window

- The world window spans the full width: x 0 to 319. The sky fill (a blitter operation
  over the four drawing planes, 20 words wide) starts at bitmap row 1 and covers rows 1 to
  `V:4AA3C` where `V:4AA3C` is the last row; so the top row is 1 and the bottom varies with the
  view:

  | situation | last row `V:4AA3C` | rows |
  |---|---|---|
  | cockpit looking straight ahead or slightly right (view codes 0 to 2) and slightly left (10, 11) | 144 (0x90) | 144 |
  | cockpit side views 3, 4, 8, 9, up/down views (12, 13); external view, normal | 167 (0xA7) | 167 |
  | cockpit rear views 5 to 7; some training layouts | 179 (0xB3) | 179 |

  (Read live: 167 during external views and 179 during a cockpit rear view of the demo.)
- Sky colour and the planes: the sky is palette entry 5 (0x447), the ground entry 4 (0x151, land) or entry 6 (0x036, over the sea zone); the four blitter passes, their planes and the
  polygon that adds plane 0 are resolved in "Sky fill and ground polygon" (verified: the sky fill run in the rig reproduces the frames pixel for pixel). The flag `V:4A953` is the
  sea-zone flag: it switches the ground base from pen 4 to pen 6 and has no effect on the sky.
- Projection space (answers the question in `03-3d-pipeline.md`): the projection itself runs in
  the fixed 180-row space (rows 0 to 179, centre row 90) for every view; the row limit
  `V:4AA3C` (`fx_project` rejects rows above it) crops only the point, line and fill primitives, which
  address bitmap row y directly (row times 40 bytes, plus the HUD y offset `V:4A990`, zero in
  flight) with no other offset. So projection row r is bitmap row r, the sky fill starts at row 1
  and rows 1 to the row limit are the window. Verified (the level-flight capture shows the last sky row at 89 and the first ground row at 90 for camera pitch 0, 73 staged states in all:
  "Sky fill and ground polygon").
- HUD offset: `V:4AA3E` holds a signed step from the table below indexed by the cockpit
  view code; `V:4AA40` is 16 times that and is added to every HUD x coordinate. A value of
  +-50 (x offset +-800) pushes every HUD pixel off screen, which is how the HUD is hidden
  in rear and external views; the ramp's third size 3u (view codes 3 and 9, offset 16 * 3u) also exceeds the
  320-pixel width, so the HUD is visible only for view codes 0, 1, 2, 10 and 11.

### Draw order of one frame

The per-frame routine writes a progress marker to `V:4AB8C` before most stages (values in
square brackets). Order (all stages are skipped from step 2 to step 12 while the in-flight
flag `V:4A84D` is zero; then only the idle path runs):

1. Input and clock: pending gameport events, mouse/joystick buttons, all key events
   (reader h0+0x8DA6, dispatch h5+0x15C, pending bits executed at h5+0x10), mission clock
   (h0+0x1748, mission elapsed seconds) and one further routine (h0+0x3C94).
2. [8] Cockpit/view-window update from pending view keys (h0+0x41E8).
3. [10-20] The frame hook h8+0x376 (`0x1F54E`): a change detector. It refreshes the scan inputs (player cell, sub-cell, quadrant) and sets a
   refresh mask in the request byte `V:4A910` (value 0x0B) when the **view-heading octant** `V:4A90C` (written each frame by h20+0x738,
   `0x299AC`, from the camera heading divided by 3600) differs from its copy `V:4A90D`, when -`V:4AB1E` crosses 0xA000 (200 ft) or when the
   altitude band (the high word of -`V:4AB1E` shifted right by 5, units of 2^21 position units, 10,240 ft) changes; it changes no palette,
   measured in `01b-flight-captures.md`. It also calls the world update `0x26AF8` (note 05) and the re-test period routine `0x1F706`. The
   list builder `0x1F770` consumes the request later in the frame (`04a-display-lists.md`, section 1: triggers, tiers by bit). Then
   camera pose (h32+0x594: cockpit or external camera), cockpit eye offsets, the camera offsets `V:4AB2A`... (`0x1F45E`), the view octant
   (h20+0x738) and further per-frame set-up.
4. [60] Sky fill and ground polygon. The selector (h33+0x10) calls the sky fill (h36+0x8FC) when bit 13 of the flag word `V:4A98A` is zero, which is always (the bit is never set);
   the fill clears and sets the four window planes (ground pen 4 or 6, sky pen 5), builds the horizon polygon from the four corners of a ground square around the camera and fills it (h33+0x3A and
   the filler); the whole-window branch for bit 13 is dead code. Rules, constants and verification: "Sky fill and ground polygon".
5. [68] The sea mesh pass (h28+0xB9C, `0x2F19C`): pen 6 polygons (the water of the terrain cells, from two cell grids) drawn through the polygon clipper over the ground base, in plane 1 only, after the sky fill and before the sun. It is skipped over the sea
   zone (`V:4A953` set). The cell lists, levels of detail, the polygon format and the order are written as rules in "Stage [68]: the sea mesh pass", verified against the rig and 207 emulator frames (the 73 sky states and the 134 mesh states of Verified by (5)).
6. [70] The sun (h33+0x3CE): a fixed direction vector rotated by the camera matrix and
   drawn with colour 9. This stage and the whole of steps 7 to 9 are skipped when the altitude
   variable `V:4AB1E` is at or below -0x8000000.
7. [78] and [80] Display lists. Three queues in hunk 73 of 24-byte records (A `0x53A4A`, B `0x540DA`, C `0x5476A`) are filled by the
   builder (`0x1F770`, `04a-display-lists.md`). The walker (h8+0x84C then h8+0x85E) walks queue A at [78] and queue B at [80]; queue C is
   walked in the entity pass (step 9). Each record calls the drawing routine of its object or terrain cell. Only the **walked window**
   of each queue is visited: the first (generated - limit) records of a queue, the farthest, are generated and ignored, where the limit
   depends on the camera height band (30 to 70 records). The distance compare against `V:4AA6A` is dead (see below).
8. [90] The near-ground pass (`0x2C0EC`, h25+0x0), after the two walkers and before the entities: a fixed lattice of 2,048 u spacing, of which the points within a class distance of the camera
   (34 to 52 of 64 to 96 table entries on a node) are drawn as point sprites (1 by 1 or 2 by 1) and small triangles that **clear plane 2** of the pixels they cover (a general-fill triangle copies plane 3 into
   plane 2 under its area, and the rest of its blit rectangle takes plane 2 from the strip the C channel reads, "The blit's C channel"): grey specks on the sea (pen 6 to 2), black specks on land (pen 4 to 0). It runs only at a camera height of at most 2,048 u (pitch band 0), 3,456 u (band 1) or 4,608 u (bands 2 and 3), sets the
   flag `V:4A85A` while it runs, and **leaves the fill block (4, 0, 0, 0xFFFF) and the colour word 3 behind** (also above the height limit). Rules, tables and verification: "Stage [90]: the near-ground pass".
9. [A0-B0] Entity pass (the direct entities, then queue C, `04a-display-lists.md`, section 8): a scan of the 20-slot table at `0x4AD2A` (stride 64 bytes) for an
   active flagged entry (h23+0x0, which returns a result), after which the object-draw routine
   h0+0x72DC runs once or twice depending on that result. The entity table proper is at
   `0x4B23C` (stride 512 bytes, see camera). The 20-slot table is the tracer pool of the gun and of the flare and chaff dispenser (ten even slots in use, note 07 section 7); the draw routine draws the model of an entity or secondary object and, for an entity, the dying burst and the trail behind it (`04a-display-lists.md` section 8d, note 07 sections 11 and 13).
10. [C0-D0] Cockpit pictures and 2D overlay set-up (h0+0x3D4C, h0+0x4AA0).
11. [D0-1D0] HUD and panel, hunks 35 to 39: main instrument set (h37+0xA4 and following),
    then conditional pieces redrawn on a staggered schedule (frame number modulo 2, 4, 8 and 16,
    or whenever the page-redraw counter `V:4A8EF` is positive), then the gauge group at
    [1A0] (h36+0xBAA and following). The hunk 36 primitives (pixel, line, filled
    polygon through the blitter, text) are used here, so the HUD is drawn after the
    world and over it.
12. [1D4] Frame-rate limiter (h20+0x562); every 8th frame the rate meter; every 16th frame a
    sprite release; every 32nd frame the row-0 clear; then the frame counter increments unless paused.
13. [210-220] Last overlays (h36+0xC, h39+0x276, h39+0x13EE) when the in-flight flag allows.

Steps 3 to 9 are the world; steps 10 to 13 are HUD and panel. Stages that are not named in
the text above are not yet identified (see Open questions).

### Sky fill and ground polygon

Settled in the milestone-3 capture round (Task 1 follow-up for Task 8). Evidence tags: **[V]** verified by the rig and the emulator (see Verified by), **[R]**
read from the code only, **[I]** inferred. The whole of step [60] is a pure function of the camera (matrix, angle words, height word), the window row
limit and two flags; it needs no terrain data. There is no horizon-line computation and no altitude band: **the horizon is the clipped image of a
square of ground far around the camera**, filled by the blitter.

#### What runs [R, V]

| piece | address | what it does |
|---|---|---|
| selector | `0x32E38` (h33+0x10) | one test, bit 13 (`0x2000`) of the word `V:4A98A`. Clear: call the sky fill. Set: call `0x33140`, which only builds the whole-window polygon at `V:5043C` and draws nothing. **Bit 13 is never set**: the image has no write to it (all 14 references to `V:4A98A` use bits 1, 8 and 14, 01b and 05), so the second branch is dead and the sky fill runs every frame in every view, including the map view |
| sky fill | `0x3486C` (h36+0x8FC) | clears and sets the window planes (table below), calls the ground polygon with the player's camera matrix, fills the result, and for the cockpit view calls it a second time with the attitude-only matrix (no zoom row scale) for the attitude ball (below) |
| ground polygon | `0x32E5A` (matrix `V:4AC90`) and `0x32E52` (matrix `V:4ACA2`), both falling into `0x32E62` (h33+0x3A) | camera-space corners, crossing search `0x3324C`, case table; returns D0 = 0 when a polygon was built, 1 when there is none |
| polygon filler | `0x34CD6` (edges and fill into one plane), lines `0x3508A`, plane copy `0x34F46` | blitter lines (XOR) and an inclusive fill |
| side flag | `0x143F2` (h0+0x43F2), run once per frame before the display-list builder | writes bit 1 of `V:4A982`: the camera is "upside down" (below) |
| base-colour flag | `0x230B8`, from the list builder's tail on even frame numbers (`V:4A992` even; odd frames run `0x230D8` for `V:4A954`) | sets `V:4A953` (below) |

The step order inside one frame is: camera matrix (`0x32074`) and the camera words, `0x143F2`, the display-list builder `0x1F770`, the two routines at `0x29A5E` and
`0x29D28`, then `0x32E38`. So the flags the fill reads (`V:4A982` bit 1, `V:4A953`) are those of the same frame, except `V:4A953`, which is refreshed on
even frames only and by the builder, that is before the fill of the same frame.

#### The window clear and the pens [V]

The fill covers bitmap rows 1 to `V:4AA3C` (the window last row, 144, 167 or 179 above) over the full width of 20 words, never row 0, in four blitter passes over the
planes of the drawing page. The plane table `V:4A76E` points to (plane 3, plane 2, plane 1, plane 0), written below as passes 1 to 4. The blitter writes a constant
(all zeros or all ones, channel D only), not a copy.

| pass | plane (pen bit) | operation | result before the polygon |
|---|---|---|---|
| 1 | 3 (value 8) | clear | 0 |
| 2 | 2 (value 4) | set | 1 everywhere |
| 3 | 1 (value 2) | clear when `V:4A953` is 0, set when it is not | 0 or 1 |
| 4 | 0 (value 1) | clear | 0 |

The whole window is therefore the **ground pen**: **4** (palette entry 4, 0x151, the land green) when `V:4A953` is 0, **6** (entry 6, 0x036, the sea blue) when it is not.
Then the ground polygon (the sky polygon, below) is filled into plane 0 with the **sky pen 5** (0x447): plane 0 gets the polygon and, when `V:4A953` is not 0, plane 1 is
cleared where the polygon is (one more blitter pass over the polygon's bounding words: B and not A). Pen 5 is entry 5 and plane 0 is the only plane the polygon
touches besides that clear. The clear and the general fill produce the pens 4, 5 and 6 only; the tiny-polygon cases ("Filling the polygon") draw with the stale colour word instead: pen 13 lines and dots that turn pen 4 into 5 and pen 6 into 7 in the cockpit view, pen 0 lines and dots in the external view. When the polygon routine returns "none" (below) nothing is added and the whole
window stays the ground pen. Nothing else is cleared: the HUD, the panel and row 0 belong to other stages.

The sea pen is not a separate polygon of this stage: the **sea mesh** (stage [68], below) draws pen 6 polygons over a base of pen 4; over the sea zone the base itself is pen 6.

#### Corners in camera space [V]

The ground is a square centred on the camera and fixed in the world axes (the corners are offsets from the camera, never translated), slightly irregular, so that it
does not line up with any heading. It is transformed by the camera matrix, which carries the heading, so the corners swing with the heading and the horizon rows depend on the heading by up to two rows
(the `a-h*` states of the captures). Four corners, each a pair (x, z) at `0x32E28`; the height is common:

| corner | x | z |
|---|---|---|
| 0 | 10240 | -9216 |
| 1 | -11008 | -9728 |
| 2 | -10240 | 9216 |
| 3 | 9728 | 11008 |

Height y = the arithmetic shift right by 2 of the high word of the long `V:4AB1E` (that long is minus the camera's height in position units, which in the cockpit is the player's
height plus the rotated eye offset of note 03, "Camera position and the cockpit eye offset": for the F/A-18 the offset vector (0, 5, 20) multiplied by the record's Q14 matrix at +0x92, each sum of products shifted right by 6, whose y component is (5 m4 + 20 m5) >> 6 = 1,280 position units at level attitude (m4 = 16,384, m5 = 0);
the figure 3,701 of the first measurement was an artefact of the staging: the paused record kept the snapshot's attitude matrix (30 degrees nose up), which turns the offset into 5 x 14,117 / 64 + 20 x 8,316 / 64 = 3,701): y = floor(floor(`V:4AB1E` / 65536) / 4): -1 for every camera height from 1 to 262,144 position units (the high word is -1 to -4 and the floor of that over four is -1), 0 only at height 0, and -33 at the ceiling of 40,960 ft (8,388,608 position units plus the eye offset: the high word is -129);
it is never positive over the world. The unit of x, z and y is the same (arbitrary), so at the ceiling the corners lie below the camera by 33 of about 10,000 and the
horizon dips by less than a row: **the corners' height has no visible effect on the horizon rows; the heading and the camera matrix do**.

Each corner is multiplied by the camera matrix `V:4AC90` (nine words, after the zoom row scale, note 03): the camera-space x is (m0 x + m1 y + m2 z), the y is
(m3 x + m4 y + m5 z), the depth z is (m6 x + m7 y + m8 z), each sum shifted right by 8 with **round half up** (add the last bit shifted out), stored as 16-bit
words. Camera +x is the screen's left (note 03). The matrix rows carry the zoom triple of `V:4AAF6` (0xA8, 0xFC, 0x80 at the start, the third halved or doubled by
the zoom keys, 0x20 to 0x80): with 0x80 the effective screen scale is 160 x 0xA8 / 0x80 = 210 columns per unit of tan (horizontal) and 90 x 0xFC / 0x80 = 177 rows per unit
of tan (vertical): the 160 and 90 of the projection (note 03) times the zoom ratio. That is the "focal length of about 175 rows" of 01b Question 4.

#### The crossing search [V]

The pyramid is |x| <= z and |y| <= z of the **scaled** camera space (it is the window: the half angles follow from the zoom triple). The routine walks eight slots,
0 to 7. Even slot 2k is corner k; the odd slot 2k+1 is the edge between corner k and corner k+1. For each slot it takes a **current corner C** (even slot: itself; odd slot:
corner k+1, slot 2k+2, wrapping 8 to 0) and an **other corner O** (even slot: corner k+1, wrapping; odd slot: corner k). A slot is skipped when the previous odd slot
found C inside (flag), which only ever skips the even slot after it. The result per slot is one of: none (the slot's screen point is stored as (0, 0)), inside
(nothing stored), or a **hit** on one of four planes with the intersection point, whose screen point is stored.

The four planes and their counters (the counters are words at `V:538F4`, the slot number of the last hit of each plane at `V:538FC`):

| index | plane (camera space) | screen edge | counter | slot word |
|---|---|---|---|---|
| 0 | y = z | top (row 0) | `V:538F4` | `V:538FC` |
| 1 | x = z | **left** (column 0) | `V:538F6` | `V:538FE` |
| 2 | -y = z | bottom (row 179) | `V:538F8` | `V:53900` |
| 3 | -x = z | **right** (column 319) | `V:538FA` | `V:53902` |

Intersection of the segment from O to C with a plane (the same arithmetic as `fx_clip`, note 03, with the signed 16-bit divide and its correction; the differences are 16-bit words, the product of two of them a 32-bit number, the divisor a word and the quotient a 16-bit word): with g the plane's coordinate (x, -x, y or -y), f(P) = z - g(P), D = f(O) - f(C), the new value of each of the two other coordinates is
c(O) - rdiv((c(O) - c(C)) f(O) / D), the third coordinate is set to the plane's value (z for x = z and y = z, minus z for the negated planes), and the point is **valid** when its z is
not negative and |x| and |y| do not exceed z. A zero D returns "no point" without changing the stored candidate. **Divide overflow** (the quotient does not fit a signed word): the divide instruction leaves its register unchanged, so the "remainder" the correction examines is the high word of the product and the "quotient" the low word of the product; the model
(`gmodel.py`, function `rdiv`) reproduces exactly that. It never happens in play: 0 overflows in 200,000 random camera rotations (model) and in 10,000 rig states; the case is unreachable for real matrices. **The stored candidate `V:4AB7E` (three words x, y, z at `V:4AB7E`, `V:4AB80`, `V:4AB82`) is never cleared and persists across calls** [R, V]: its only writer is the intersection routine (`0x3372A`, one store of the three words per intersection that has a denominator), no other instruction touches the three words, and the image holds 0 there, so the value at the start of a flight is 0 (x = y = z = 0). The ground-polygon routine is called twice in a cockpit frame (the camera matrix `0x32E5A`, then the attitude ball's matrix `0x32E52` after the sky fill) and once in the other views, so the camera call of a frame starts with the last candidate of the previous frame's ball call, and the ball call with the last candidate of the same frame's camera call; the lastz of the decision table is the z word `V:4AB82` as it stands at the moment of each test. A test reads a value from an earlier call only when no candidate has been stored yet in the current call, that is, when the first intersection tried returns "no point" (D = 0), which does not store; the Python model resets the candidate to 0 at the start of a call (the rig runs start from a zeroed variable block, so the two agree), the port keeps the value in the scene state across calls. Counted [V]: in 40,000 random camera states (171,499 intersections, `check_rig.py`-style states, the rig's matrices) D was 0 in none of them, so the stale read did not occur and the carry-over has no effect on any state sampled; it is kept for the exactness of the rule. **A hit with z = 0** (the point (0, 0, 0)) makes the original loop forever in the projection (a branch to itself on z <= 0): never reached [V]: 0 valid hits with z = 0 in the same 40,000 states (64 invalid candidates had z = 0), and a valid hit needs |x|, |y| <= z, i.e. an edge of the ground square through the camera, which the corners (10,000 away, the height 0 to 33) do not allow in the sampled states. The candidate (even when invalid) is stored in `V:4AB7E`
(x, y, z) and its z, `V:4AB82`, is read by later tests.

Decision for one slot (x1 = x + 1 and y1 = y + 1 of C; (x, y, z) = C, (xo, yo, zo) = O; "Z" is the depth z of C; "zo <= xo" and so on compare the O words; a test written
"try P" is an intersection with plane P, accepted only when valid; "lastz" is `V:4AB82`):

| step | condition | action |
|---|---|---|
| X-high | x1 >= z | if zo <= xo: none. Try R: hit 1. If lastz >= 0, or zo <= -xo: go to Y. Try L: hit 3. Else go to Y |
| X-low | not X-high and -x1 >= z | if zo <= -xo: none. Try L: hit 3. If lastz >= 0 or zo <= xo: go to Y. Try R: hit 1. Else go to Y |
| Y-high | x inside and y1 >= z | if zo <= yo: none. Try T: hit 0. If lastz >= 0 or zo <= -yo: go to X2. Try B: hit 2. Else X2 |
| Y-low | x inside and -y1 >= z | if zo <= -yo: none. Try B: hit 2. If lastz >= 0 or zo <= yo: go to X2. Try T: hit 0. Else X2 |
| inside | all four inside | none when z < 0, else inside (an odd slot sets the skip flag, an even slot does nothing) |
| Y (after X-high or X-low failed) | y1 < 0 | if -y1 < z or zo <= -yo: none. Try B: hit 2. If lastz >= 0 or y1 < z or zo <= yo: none. Try T: hit 0, else none |
| Y | y1 >= 0 | if y1 < z or zo <= yo: none. Try T: hit 0. If lastz >= 0 or -y1 < z or zo <= -yo: none. Try B: hit 2, else none |
| X2 (after Y-high or Y-low failed) | x1 < 0 | if -x1 < z or zo <= -xo: none. Try L: hit 3. If lastz >= 0 or x1 < z or zo <= xo: none. Try R: hit 1, else none |
| X2 | x1 >= 0 | if x1 < z or zo <= xo: none. Try R: hit 1. If lastz >= 0 or -x1 < z or zo <= -xo: none. Try L: hit 3, else none |

(R = plane 1, L = plane 3, T = plane 0, B = plane 2 of the table above.) A hit increments the plane's counter, stores the slot number, and projects the point: the screen coordinates
of a crossing are sx = 160 + h(trunc(x * 320 / z)) and sy = 90 + h(trunc(y * 180 / z)), where h(q) halves with rounding half up (arithmetic shift right by 1, plus the bit
shifted out), each clamped to 0..319 and 0..179. The stored point is then converted to the screen (x = 319 - sx, y = 179 - sy). So a crossing of plane R is at screen column 0, of plane L at
column 319, of plane T at row 0, of plane B at row 179; the free coordinate is the rounded projection (the division is the 16-bit divide, truncating toward zero, **not** the
`fx_project` division of note 03, which does not round and uses 160 and 90).

#### Polygon assembly: the sky polygon [V]

Counters are tested in this order, picking the first row that applies. Vertex names are screen corners TL (0, 0), TR (319, 0), BR (319, 179), BL (0, 179); Pn is the stored screen
point of plane n's last hit. "Inverted" is bit 1 of `V:4A982` in the cockpit view (`V:4A83D` zero); in every other view the bit is treated as 0 in the first row only (both-sides crossing) and used as it
stands in the others.

| counters non-zero (first match) | polygon, not inverted | polygon, inverted |
|---|---|---|
| plane 1 and plane 3 | P1, P3, TR, TL | P1, P3, BR, BL (other views: as not inverted) |
| plane 1 and plane 0 | P1, P0, TL | P1, P0, TR, BR, BL |
| plane 1 and plane 2 | P1, P2, BR, TR, TL | P1, P2, BL |
| plane 1 only | none | none |
| plane 0 and plane 3 | P0, P3, TR | P0, P3, BR, BL, TL |
| plane 0 and plane 2 | P0, P2, BL, TL when the roll word `V:4AB4A` >= 14400 (0x3840), else P0, P2, BR, TR (the inversion flag is not used) | same |
| plane 0 only (planes 1, 2, 3 zero) | none | none |
| none of planes 0, 1, 2 (plane 3 may be set) | the whole window TL, TR, BR, BL when the pitch word is greater than 14400, else none (pitch word: `V:4AB42` in the cockpit, `V:4AB4C` in the other views) | same |
| plane 2 and plane 3 (planes 0 and 1 zero) | P2, P3, TR, TL, BL | P2, P3, BR |
| plane 2 only | none | none |

Sky is therefore the side of the line through the two crossing points that contains the top of the screen (the polygon names the corners it keeps), or, when the camera is flagged
inverted, the side that contains the bottom. "None" means no sky at all this frame (the window stays the ground pen), also for a view that looks straight down.
Only counters, not slot values, pick the row: a plane can only be listed once, so with three crossings the earlier row of the table wins (verified on all random states).

The side flag, bit 1 of `V:4A982` (`0x143F2`, every frame): P is the camera pitch word and R the camera roll word, each divided by 8 (units of a tenth of a degree, 0 to 3599); in a
non-cockpit view P comes from `V:4AB4C` and R is 0. The flag is set when exactly one of the two angles is "far": the flag is cleared first; for P in 900 to 2699 it is set unless R is in
900 to 2699; for any other P it is set when R is in 900 to 2700. (R = 2700 sets the flag in both.) That is, the camera is upside down against the ground when the pitch is
past the vertical (between 90 and 270 degrees) xor the roll is beyond 90 degrees to either side.

The attitude ball in the cockpit view uses the same routine entry `0x32E52` with the aircraft attitude matrix `V:4ACA2`, which **has no zoom row scale** (only the camera matrix `V:4AC90` is scaled by the zoom triple; the ball's cases are exact only with the unscaled matrix [rig: Task 8 differential, nine attitude-ball cases]); after the sky fill, if a polygon was built, its
count and five vertex pairs are copied to `V:504DE` (count 0 when there is none). Note 06 owns what the ball does with it; the roll and flag rules are the same.

#### Filling the polygon [V]

`0x34CD6` takes the polygon at `V:5043C` (count, then the vertex pairs). Bounding box: minimum and maximum x and y over the vertices. Let h = maxy - miny, w = maxx - minx, and
L the row limit `V:4AA3C`:

| situation | action |
|---|---|
| miny > L | nothing drawn (returns 1) |
| h >= 3 and w >= 2, or h = 2 and w >= 3 | the general fill (below); returns 0 |
| h >= 3 and w <= 1, or h <= 1 and w >= 2 | a **line** in the colour `V:4AA0C` (all four planes, the pen replaced; the plane mask is forced to 0xF, second word 0xFFFF, for the duration of the line unless the byte `V:4A85A` is non-zero; `V:4A85A` is the flag of the near-ground pass, stage [90]: it is set to 1 at `0x2C13A` once that pass has passed its altitude test and cleared at `0x2C360`, on its early exit too, so it is **0 in flight everywhere else, in the sky fill and in stage [68] included**, and the forcing always applies there; the previous block is restored after the line): h <= 1: the pixels (minx .. maxx, miny + 1); h >= 3: the diagonal of the box from (minx, miny) to (maxx, maxy) with the edge rule of the general fill (first row skipped, rows miny + 1 to maxy, the Bresenham line of step (2) below, all pixels written), cut at row L; nothing if miny + 1 > L; returns 1 |
| h = 2 and w <= 2 | a 2 by 2 **block** of dots in rows miny + 1 and miny + 2 at the columns (maxx - 1, maxx), or (maxx, maxx + 1) when maxx is a multiple of 16; when miny + 1 = L only the first row, as two dots at maxx and maxx - 1 (maxx - 1 = -1 wraps to the end of the row above); nothing when miny + 1 > L; returns 1 |
| h <= 1 and w <= 1 | one **dot** at (minx, miny + 1), plus a second at (maxx, miny + 1) when w = 1; nothing when miny + 1 > L; returns 1 |

A **dot** takes the plane mask `V:4A79E` (low four bits) and the colour: every plane k in the mask is set when bit k of `V:4AA0C` is 1 and cleared when it is 0, the other planes keep their value (new pen = (old and not mask) or (colour and mask)). The line replaces the pen by the colour. Returns 1 in all
these cases, so the sea plane clear below does not run. The cases appear when the horizon is within two pixels of a window corner or edge (a bounding box of at most two rows or one column). The same cases serve the near-ground pass of stage [90] with its own block (mask 4, pattern 0, colour 3, `V:4A85A` set): there every line, dot and block clears plane 2 and the general fill is the alternate blit `0x34EEC` (plane 2 := plane 3 under the area, and elsewhere in the blit's rectangle plane 2 copied from W words further left per row up, the stale C modulus), "Stage [90]: the near-ground pass".

**The two entries of the filler and the limit of the tiny cases [V].** The filler has two entries: `0x34CD6` takes its row limit A from `V:4AA3C` (every stage of this note), and `0x34CD0`, the entry of the attitude ball (note 06), takes A = 199. A decides the early exit (miny > A: nothing) and the dot cases (a dot or a pair of dots needs miny + 1 <= A), and the general fill's edge cut; **the two line cases (the horizontal and diagonal lines of the table above) always cut at the window's last row `V:4AA3C`, whatever A is**, because the line routine reads that variable itself, and **the 2 by 2 block takes its choice of the one-row pair from `V:4AA3C`** (the pair, a single row of two dots, when miny + 1 >= `V:4AA3C`) and its exit from A (miny + 1 > A: nothing). So with A = 199 and a 144-row window a one-row dot at row 177 is drawn while a line at the same row is not. For the entry `0x34CD6` the two limits are equal and the table above holds unchanged. Verified (`filler_check.py tiny`, seeds 1 and 2: 777 random boxes of the tiny cases, window rows 144, 167 and 179, both entries, random stale colour and mask and base pen, the changed pixels of the four planes against the rule, 0 differences).

**The attitude ball's tiny cases [V].** The ball (`0x34B1A`, note 06 "Attitude ball") scales the sky polygon `V:504DE` (window coordinates, at most five vertex pairs) into the panel with x' = ((x * 24) >> 8) + 193 + `V:4AA40` and y' = ((y * 31) >> 8) + 162 + `V:4A990` (`0x34C7C`, unsigned word products), writes it to `V:5043C` and calls the filler by the entry `0x34CD0` (A = 199). A polygon whose scaled box is a tiny case therefore **draws directly into the four planes, outside the ball's ellipse mask** (the ellipse is blitted before; the general fill is the only case that goes through the mask), with the **colour word `V:4AA0C` and the plane mask `V:4A79E` as the panel routines before it left them**. Those are, in every state the rig could reach, **pen 3 and mask 0xF** (the second word 0xFFFF; `filler_check.py state`: the twelve panel and HUD routines of the frame run in their order on the two logged scenes, 32 values of the frame counter each, 64 of 64 states end with colour 3 and mask 0xF; the unconditional routines before the ball leave 3 and 0xF, and the conditional ones that follow leave the same), and `V:4A85A` is 0. So a one-column or one-row ball polygon is a line or dots of **pen 3 on all four planes at the scaled place** (the dots with mask 0xF are the same pen), cut at the window's last row `V:4AA3C` for the lines (rows 163 to 183 of the ball lie below the 144-row window of the cockpit, so in the cockpit view a ball line is cut away entirely and only the dot cases, limit 199, show; in the full-window views the ball is not drawn). Verified (`filler_check.py ball`, seeds 1 and 2: 708 random sky polygons through the routine `0x34C7C`, the scaled polygons compared with the rule above, 0 differences; lines are rare in this sample, 11 of 708, the dot cases dominate). The port's ball therefore draws nothing for a tiny polygon where the original draws pen 3 dots (and lines in a window of 167 or 179 rows); the rule is the table above with A = 199, colour 3, mask 0xF.

**What the two variables hold in flight [V, R].** The tiny cases draw with two inputs that the sky fill does not set, the colour word `V:4AA0C` and the mask block `V:4A79E..4A7A5` (mask word, second word, `V:4A7A2`, `V:4A7A4`). They were sampled at video-frame boundaries (`step`, then `V:4AB8C`, `V:4AA0C`, the block and the frame counter `V:4A992`; the logs are named at the end of this paragraph). **The mask is not a stale value.** The ground-polygon routine ends every path that builds a polygon in one common tail (`0x33178`, the whole-window row of the decision table included) that stores the block (1, 1, 0, 0) and sets the byte `V:4A956` to 1 (0 when no polygon is built; nothing reads it), and returns 0; the path that builds none (`0x3319C`) leaves the block alone. So whenever the fill has a polygon to draw, **the block is (mask 1, second word 1, 0, 0) in every view**, and the dot cases act on plane 0 only. Evidence [V]: the original selector in the rig leaves (1, 1, 0, 0) after five staged attitudes (level, 24 degrees up, 20 degrees up and down, inverted) from a poked block of (7, 0xFFFF), and the pc watch on `0x33178` fires in the three attitudes traced (level, 24 degrees up, 20 degrees down; the 24 degree case through the whole-window row `0x33140`); on the emulator the samples with the marker 0x60 read (1, 1, 0, 0) in 22 of 22 (cockpit, flight), 28 of 28 (deck), 19 of 20 (level flight), 11 of 13 (external view); the exceptions are samples taken before the tail had run, which show the block of the frame start: (7, 0xFFFF) in the cockpit (1 of 20) and (0xF, 0xFFFF) in the external view (2 of 13). The frame-start block (marker 0x10) is (7, 0xFFFF, 0, 0) in the cockpit view, except in the three special frames below, where it is (0xF, 0xFFFF, 0, 0); it is (0xF, 0xFFFF, 0, 0) in every sample of the external and map views. In the map view no polygon is built (the pitch word `V:4AB4C` is 7,200, below the 14,400 of the whole-window row, and the crossing search finds no sky), so the block at the fill stays (0xF, 0xFFFF, 0, 0) there. **The colour is the last colour the previous frame drew with**, and it follows the frame counter `V:4A992` exactly [V]: in the cockpit view (flight, deck and the level-flight run: 309 samples at marker 0x10 and 69 at the fill outside the special frames) it is **13** (entry 13, 0x777, shown as (89, 89, 89)) except in the three game frames whose counter is 18, 19 or 20 modulo 32, where it is **0** (33 samples at the frame start and one at the fill, none different); in the external view it is **0xFD80** (low nibble 0: pen 0, black; 80 + 13 samples) except in those same frames, where it is **4** (7 samples); in the map view it is **3** in every sample (31 frames, 243 to 273, the three special frames included; no polygon is built there); in a cockpit view with the HUD off the screen (rear view, KP2) it is 3 in the first 11 sampled frames of the run (254 to 264, two seconds after the key: the page redraw of the view change) and 0 in the frames 266 to 268, and from then on exactly as in the front view (13, and 0 in the frames 18 to 20 modulo 32: 274 to 276, 306 to 308, 338 to 340, 371 and 372 in the run). The earlier statement that the cockpit value never differs over frame numbers 236 to 456 and 0 to 700 was true of the 50 samples at the fill only, which happened to miss the special frames. So in the cockpit view a horizon within the tiny cases draws a **pen 13 line** (light grey, 0x777) of one pixel height across row 1 when the whole horizon is at row 1 (pitch word 2,112 to 2,150 at roll 0, the box (0, 0) to (319, 1)), and a pen 13 run of 69 pixels (columns 0 to 68) on row 1 when the sky is a sliver at the top left (pitch 2,160 to 2,170): both confirmed by the frames of the eight staged pitch states of `sky-ground-tiny.txt` (pixel for pixel, rig with `V:4AA0C` = 13 and mask 1: `check_frames`-style comparison in the log; rows 0 and 2 untouched; the staged frames have a frozen counter, outside the special frames), the pitch 2,100 state gives the general fill (rows 1 and 2) and 2,180 the all-ground window; in the three special frames the same line would be a pen 0 (black) line in the cockpit and a pen 4 line (the land green) in the external view [R: not staged]. The dot cases set plane 0 only (colour bit 0, mask 1): a dot in the cockpit view with colour 13 turns land pen 4 into pen 5 (the sky colour) and sea pen 6 into 7; with colour 0 or 0xFD80 it clears plane 0 (pen 5 becomes 4, pen 7 becomes 6, pen 4 and 6 stay), with colour 4 it leaves planes 0 untouched. Logs (under `extracted/captures/logs`, git-ignored; scripts in `tools/vamiga-ref/scripts`): `sky-ground-stale-colour.txt` (cockpit in flight, 1,200 frames), `-deck.txt` (carrier deck, 700), `-external.txt` (external view, 700, with the pitch words), `-level.txt` (level flight, 400), `-map.txt` (map view, 400), `-hudoff.txt` (rear view, 1,200). The model of all these cases is `fmodel.tiny`, checked against the rig on 1,350 random boxes (`check_rig.py tiny`), every combination of colour, mask, base pen, limit and edge.

The general fill: (1) every edge from vertex i to i+1 and the closing edge are drawn into plane 0 with the blitter line (XOR), skipping horizontal edges; an edge from (x0, y0) to (x1, y1)
covers the rows **ymin + 1 to ymax** (the top row of an edge is not drawn, so a shared vertex is not drawn twice; an edge whose first row ymin + 1 is below the limit L is not drawn at all), starts at (the x of the end that has the smaller y, that smaller y plus 1) and has
dy1 = |y1 - y0| - 1 and dx = signed x difference from that start; (2) the line is a Bresenham line with major axis x when |dx| >= dy1 and y otherwise (major and minor are the larger and smaller of |dx| and dy1);
the error term starts at 4 minor - 2 major (the line starts with the sign flag set when this is negative); at every step the major axis moves by one and the minor axis by one when the flag is clear (the flag of that moment), then the term gets 4 (minor - major) when the flag was clear and 4 minor when it was set, and the flag becomes
the sign of the new value (a 16-bit word); the line has length + 1 pixels, where length = major, cut to the rows
left (a = L - first row) as: steep line: length = min(major, a); shallow line: length = major when dy1 <= a, else round-half-up of major x a / dy1 (halved from twice the truncated product quotient);
(3) with the one-dot flag a shallow line writes only its first pixel on each row; (4) after all edges the rectangle of whole 16-pixel words from the word of max(minx - 1, 0) to the word of maxx,
rows miny to min(maxy, L), is filled **right to left, one row at a time with the carry cleared per row, inclusively** (each run from an odd edge pixel to the next edge pixel, both included). For a convex
polygon every row of miny + 1 to the last row holds two edge pixels (left and right), so the result is the row span between them, both edge pixels included; the top row miny holds none and is not set.
Consequences that show on the screen: the sky is rows 1 to the horizon row, the horizon row of a column is the last row that the horizon line reaches in that column plus the one-row shift of the
start (a polygon vertex at y = 40 on the left gives a last sky row of 41 in column 0), and **at pitch 0 and roll 0 the last sky row is 89 and the first ground row is 90** (the vertices are at y = 89).

#### Palette in flight [V, R]

| rule | value |
|---|---|
| colour registers in free flight | entries 0 to 15 are the first step of the fade table (`0x29FA4`, 16 words; `palette/flight.pal`, 48 bytes, FNV-1a-32 `87b6cb23`, note 08 section 4.1); 16 to 31 are 000. Identical at every height from the deck to 40,960 ft, over land and over sea, in every attitude (01b: 150 samples of the altitude runs and the 58 states of `sky-ground-attitude.txt` and `-height.txt`) |
| pens of this stage | 4 = 0x151 land base, 5 = 0x447 sky, 6 = 0x036 sea. Entry 11 (0x030) is not drawn by the sky fill; "land green 0x030, entry 11" in 01b Question 3 was a misattribution (the land pixels are entry 4) |
| the fade table | 16 steps of 16 words at `0x29FA4`; the step loaded is 15 minus the fade level `V:4A958`. Level 15 is step 0, the normal palette |
| who moves the level | the vertical-blank server (h0+0x9420 to 0x9596, inside the handler at h0+0x92DE): once per blank it counts `V:4A95B` down; when the result is below 0 and `V:4A958` differs from the target `V:4A959` it moves the level by one toward it, loads the step (LoadRGB4, 16 colours, both copper lists) and re-arms the counter with 1 when stepping down and 2 when stepping up; with the levels equal the counter is left alone. **The counter `V:4A95B`** [R, V] is a signed byte (decrement and test as a byte; it is never set elsewhere, the image holds 0 and the airborne snapshot 2). It is decremented once per blank **only while the level differs from the target**: at the equal level the handler skips it, so it stands unchanged (1 after a descent, 2 after an ascent, held for the 46 blanks between the two halves of the measurement below). A step is made in the blank in which the decremented value is negative, and the re-arm (1 down, 2 up) follows in the same blank, so the visible sequence is 1, 0, step (two blanks per level) going down and 2, 1, 0, step (three blanks) going up. **The delay of the first step after a target change is the standing counter plus one blanks counted from the first blank in which the new target is stored**: from 2 (the snapshot's start, and after any ascent) the first step comes in the third blank after the target is seen (level 14 at blank 3, the target being in place at blank 0); from 1 (after a descent) in the second (target 15 seen at blank 74, level 3 at blank 76); a counter of 0 would step in the first blank. A second byte, `V:4A95C` (set to 3 at the arrival, to 2 at `0x13C52` and `0x13C62`, counted down once per game frame at `0x18376`), makes the handler and the frame routine reload the same 16 colours a few more times after the arrival; the register values do not change [R]. **Measured per blank** [V] (`step` one video frame at a time, 150 blanks, accumulator poked to 15, then to -1): the target is written by the game-frame routine (`0x13166`, once per game frame), so the first step follows it by up to a game frame; the poke of 15 was in place at blank 0 and the first step showed after the 4th blank, then **one level per 2 blanks going down** (15 to 2: steps after blanks 4, 6, 8, ... 28: 13 steps, 0.56 s) and, after the target returned to 15 (poked at blank 70, first step after blank 77), **one level per 3 blanks going up** (3 to 15 after blanks 77, 80, ... 113: 0.72 s). The irregular figures of 01b (0.2 s samples: 15, 14, 10, 8, 5, 2) come from sampling in wall-clock time while the emulated machine ran slower than real time; per blank the cadence is exact |
| the target in flight | 15, except **blackout**: `V:4A959` = 2 (step 13, every entry 000: the screen goes black) while `V:4A941` > 5 in the cockpit view of the own aircraft (`0x13166`); in other views, and when the accumulator is 5 or less, 15. Measured: poking `V:4A941` to 15 moves the level 15 to 2 in about 0.56 s (28 blanks) and back up when the accumulator has run down to 5 (one per mission-clock second) |
| blackout accumulator | `V:4A941`, a signed byte, -1 when idle. Once per mission-clock tick (when the seconds word `V:4ABAA` changes; `0x298B4`): while the latch `V:4A940` is set it increases by one up to 15, else when it is 0 or more it decreases by one. The latch is set by the turn-rate cue `0x270A0` when the magnitude of ((E + (E >> 3) + `V:4A9FE`) >> 3) is at least 78 (E the pitch rate, record +0x56; V:4A9FE the bank-turn coupling term of note 05) and cleared when it is below. **The natural trigger, flown [V]** (`holduntil` on the emulator from `airborne.vasnap`: LEFT held until the roll word passed 6,000, then DOWN, full back stick, held until the latch was set): at the first sample with the latch set the record read roll 9,744 (122 degrees of left bank), pitch command -20 (the stick's maximum), E = -461, `V:4A9FE` = -102, so (-461 - 58 - 102) >> 3 = -78: the **latch is set at 78 exactly and the accumulator moved from -1 to 0 on the next mission-clock tick**, then fell back to -1 on the tick after that, the latch having cleared meanwhile (the stick was released and E fell to -27 within the second): the samples are at mission seconds 98 (latch set, accumulator -1, E = -461), 99 (latch clear, accumulator 0, E = -260, then -147) and 100 (accumulator -1, E = -27): rule confirmed in play (log `sky-ground-blackout-flown.txt` under `extracted/captures/logs`). The latch needs both terms: the stick's commanded rate is at most 468 (command 20, table 0x425D8) and the largest rate sampled with the keyboard pull alone was |E| = 510 (`sky-ground-blackout-pull.txt`: UP held for 3 s, coupling 0), for which E + (E >> 3) is 573 and the shifted value 71, below 78; the sum must reach 624 in magnitude, so the coupling term of the same sign as E must supply at least 51 more (about -100 in the flown trigger: a bank beyond 90 degrees at speed). Sustaining the latch for the seven consecutive ticks that take the accumulator from -1 to 6 (it exceeds 5 at the seventh) was not achieved by the stick (the nose rises and the speed falls within a second); the fade it causes was verified by poking the accumulator; to hold the latch for a replay, poke `+0x56` (E) to -461 and `V:4A9FE` to -102 each blank (an open-item method) |
| other targets | 0 (fade out) and 10 (dimmed) are mission-state changes, read from the writers [R]: the value 0 is stored with the in-flight flag `V:4A84D` cleared at `0x1188C` and `0x13B64` (leaving flight; frame-state `V:4AB8E` = 3) and at `0x11B1A` (game mode 0x7F, `V:4AB8E` = 2, with the pause byte set); the value 10 at `0x12DA4` and `0x12FF8` (`V:4AB8E` = 2, when the pause byte is negative: the pause, `0x12FF8` in the flight state and `0x12DA4` in the approach state, note 08 5.14; the debrief and the crash hold do not change the target); 15 is stored at `0x11B7A`, `0x1235E`, `0x123D4`, `0x1254E`, `0x13922`, `0x13BB8` and `0x1D19C` (entering flight, the start-up). None of them happens in free flight except the blackout. The level word is also copied to the target at `0x120E8` (freeze the fade where it is) |
| conversion | note 06 "Pen 3 and the emulator's colour curve" owns the 4-bit to 8-bit table: 0 and 1 map to 0, 3 to 28, 4 to 43, 5 to 56, 6 to 72, 7 to 89, 8 to 106, 9 to 123, 13 to 197. Measured in the fade frames [V] (pens 4 and 5 over the 150 blanks; the frame shows the colour registers of the previous blank): nibble **2 maps to 0** (20 of 20 samples), 3 to 28, 4 to 43, 5 to 56, 6 to 72, 7 to 89. The components that occur in pens 4, 5 and 6 at any step are 0 to 7 only. Across all 16 steps of the fade table the nibbles 10, 11 and 12 occur only in entry 9 (the sun, colour 9 at stage [70]; steps 0 to 3), 13 in entry 9 at step 0, and **14 and 15 never occur**; the curve at 10, 11 and 12 is not measured (the sun was not in a captured fade frame) and is flagged for Task 8's palette conversion, which needs the measured curve for every level that occurs in the fade steps. Sky (4, 4, 7) shows (43, 43, 89), sea (0, 3, 6) shows (0, 28, 72), land (1, 5, 1) shows (0, 56, 0) |

There is no altitude band, no day or night palette and no per-mission palette in flight: the "band" the plan's Task 8 refers to does not exist. Open question 6 is closed.

#### The base colour flag: the sea zone [V]

`V:4A953` is 1 when the player's zone is in the **sea zone** table at `0x23188` (h12+0x490, 560 bytes up to `0x233B7`, data inside the code hunk; the second table `0x233B8` serves `V:4A954`, used by stage [68]). The lookup (`0x230F8`):
inputs are the zone column `V:4AA00` and row `V:4AA02` (the player record's cell words at +6 and +8 shifted right by 2, equivalently the position longs shifted right by 24 after masking with `0x1FFFFFFF`:
0 to 31; for an external camera taken from the camera position `V:4ACF6` and `V:4ACFE`; read at the moment `0x230B8` or `0x230D8` runs, see below), the altitude A = minus `V:4AB30` (units of 256 position units), the cell phase `V:4A908` (0 to 15) and the
view octant `V:4A90C` (0 to 7).

1. A list of row numbers ends with -1 and is followed by one offset word per entry (offsets are from the table start); the row must be in the list, its offset leads to a list of column numbers of the same shape; the column must be in it, and its
   offset leads to a chain of records.
2. A record is a word that is negative at the end of the chain, else a flag word (0 or not), a long bound, and groups. Flag 0 requires A <= bound, flag non-zero requires A >= bound; **a failed bound ends the whole lookup with 0** (no later record of the chain is tried).
3. A group is a phase byte, then octant bytes, then a negative terminator byte. A phase byte equal to the phase starts the octant test; the group list ends with a negative byte, after which a record starts at the next even address. A phase group that matches
   but whose octants do not **overwrites the comparison value with the octant**, so the later groups of that record are compared against the octant, not the phase (reproduce this).
4. The result is 1 at the first matching octant, 0 when the chain ends.

The keys of the table (row `V:4AA02`: columns `V:4AA00`) are: rows 12 to 16: 17, 18, 19, 20; row 17: 16, 18, 19, 20; row 18: 19, 20; row 19: 20; around the carrier's start zone (17, 16). The chains behind the keys
say, per altitude, phase and octant, when the view looks over water. Measured (`sky-ground-sea.txt`, the player staged in the carrier's zone at two heights and eight view octants): at A = 249 (60,000 position units high) the flag is 1 in
all eight octants; at A = 2,359 only octant 5 gives 1; the base pen follows (no pen 4 pixel is left when the flag is 1). The 220 state blocks of the current capture logs (11 of them with the flag set, 31 with the second flag `V:4A954`) and 4,000 random rig lookups agree with the model.

#### Stage [68]: the sea mesh pass [V]

Settled in the mesh round. Tags as above. The pass `0x2F19C` (hunk 28, h28+0xB9C) draws **polygons of the sea** over the ground base of the sky fill, one polygon per water region of each terrain cell, in a fixed order, through the generic polygon
clipper `0x28B6C` (note 03: perspective clip against the view pyramid, projection, the blitter filler `0x34CD6`). It is a pure function of the player's (or the external camera's) position, the camera matrix, the zoom, the scaled altitude
and a few flags; the model `tools/vamiga-ref/sky-ground/meshmodel.py` reproduces the original's polygon list exactly (below).

**When it runs [R, V].** Only in flight (step 4 onward of the draw order), after the sky fill [60] and before the sun [70]. The frame routine calls it when the base-colour flag `V:4A953` is 0, that is **not over the sea zone**; the other
condition of that test, `V:4A868` non-zero, never holds (below). Inside the pass: the coarse pass runs only when the scaled altitude A' exceeds 1016 (0x3F8); the fine pass always runs. Either can draw nothing (all cells land, or cells off the grid).

**Inputs [V].**

| input | where | meaning |
|---|---|---|
| position (x, y, z) | cockpit and chase views (`V:4A83D` zero): the viewed record `V:4B23C + V:4A996`, longs at +0x14, +0x18, +0x1C. External view (`V:4A83D` non-zero) and the map view: the camera position longs `V:4ACF6`, `V:4ACFA`, `V:4ACFE` | **the record origin, not the camera**: the cockpit eye offset of note 03 is not added; only the height gets its own constant (below) |
| camera matrix | nine words `V:4AC90` (after the zoom row scale) | row 1 (m0, m1, m2) gives camera x from the ground x and z, row 2 camera y, row 3 camera z; the ground is flat, so only m0, m2, m3, m5, m6, m8 act on the horizontal offsets and the column (m1, m4, m7) on the height |
| altitude A | minus the long `V:4AB30` (the camera height in units of 256 position units, eye offset included) | selects scan lists and levels of detail |
| A' | A scaled by the zoom: if `V:4A895` is zero (the zoom factor is not the default 0x80) and A <= 0x7FFF0: A' = (((A >> 4) low 16 bits) x (0x8000 / z)) >> 4 with z = `V:4AAFA` (a value below 2 counts as 2; the quotient is an integer division); otherwise A' = A | at the default zoom A' = A; at 0x40, about 2 A; at 0x20, about 4 A (the low four bits of A are lost). `V:4A895` is 0x80 at start and whenever the zoom word is 0x80, 0 otherwise (written by the zoom keys and the view resets) |
| map flag `V:4A865` | non-zero in the map view | turns the levels of detail off (below) |
| `V:4A954` | second sea-zone flag, below | simplified geometry for the player's own cell |
| phase `V:4A908` | 0 to 15, `qx + 4 qz` with qx = 3 - ((x >> 22) & 3) and qz = 3 - ((z >> 22) & 3): the quarter of the 2^24 cell the position is in (4 by 4 sub-cells of 2^22; the bits of the same position words) | rewritten every frame by the frame hook (`0x1F54E`) from the viewed record's cell words or the external camera position |
| octant `V:4A90C` | the view heading in eighths of a circle: the heading word divided by 3600 (28,800 per circle), 0 to 7; the heading word is `V:4AB46` in the cockpit and `V:4AB4E` in the external view; a negative word counts as 0 | written every frame by `0x299AC` |
| `V:4A868` | never written by any code (the key handler `0x1EF1E` only loads its address, then reloads the register for other use) and zero in every snapshot: treat as constant 0. If it were set the pass would also run over the sea zone, draw with the plane mask 0xF and a full-ones second word, and still skip cells whose record is negative | dead [R] |
| `V:4A955` | written 0 at entry and 1 when the own-cell-only list is chosen, **read by nothing** (the image has two references, both stores) | dead [R] |

**The two passes [V].** Both use the same code with different constants.

| item | coarse pass `0x2F25A` | fine pass `0x2F234` |
|---|---|---|
| runs | A' > 1016 | always |
| grid | 8 by 8 words at `0x47D7C` (row x 16 bytes + column x 2); its cell is 2^28 position units (one quarter of the world's side 2^29 in each axis, so the world is 2 by 2 cells) | 32 by 32 words at `0x47F40` (row x 64 + column x 2); cell 2^24 position units (the world is 32 by 32) |
| player's cell | column = the top nibble of x + 4, row = the top nibble of z + 4, the nibble being bits 31 to 28 **taken unsigned** (the high word of the position shifted right by 12, not an arithmetic shift of the long): the world (x, z in 0 to 2^29) occupies columns and rows 4 and 5, and a negative position (nibble 8 to 15) gives 12 to 19, off the 8 by 8 grid, so every cell of the pass is skipped [rig: confirmed by the Task 8 differential, 186,000 cases] | column = x >> 24, row = z >> 24 (word arithmetic on the high word: a negative position gives 128 to 255) |
| cell-relative position | x and z masked with 0x0FFFFFFF | masked with 0x00FFFFFF |
| unit of vertex words | 65,536 position units (the cell is 4,096 units) | 4,096 position units (the cell is 4,096 units) |
| height term | h = the high word of y (y >> 16) | h = (y + 0x1000) >> 12 (low 16 bits): **the mesh's eye is 4,096 position units above the record origin**, not the camera's 1,280 (note 03: level F/A-18) |
| vertex pre-shift s | 0 | 0, 1 or 2 (below) |
| cell range | columns and rows 0 to 7 | 0 to 31 (cells outside the grid are skipped; there is no wrap) |

**Which cells [V].** A scan list is a byte string: each byte is a cell type 0 to 24 (bits 0 to 6) with bit 7 set on the last byte; 0xFF (alone) ends a list. The cell type indexes the table `0x2E600` (the first 50 bytes of `tables/mesh_scan.bin`): 25 pairs of signed bytes (dx, dz), the offset of the cell from the player's cell in grid steps. Samples: type 4 is (0, 0), **the player's own cell**; types 0 to 8 are the 3 by 3 block around it (type 0 is (1, 1), type 8 is (-1, -1)); types 9 to 24 are the 16 cells of the outer ring of the 5 by 5 block (type 9 is (2, 2), type 24 is (-2, -2)). Cells are visited in list order; that order is the draw order. A cell outside the grid is skipped. The lists (all inside hunk 28, `0x2E632` to `0x2F19B`, 2,922 bytes; the extractor keeps the whole range `0x2E600`-`0x2F19B`, 2,972 bytes, as `tables/mesh_scan.bin`; the word table of the far test is a separate file, `tables/mesh_far.bin`, below):

| pass | condition (A' = the scaled altitude) | list address | selection |
|---|---|---|---|
| coarse | A' <= 0x5000 | `0x2E732`, 16 lists of 4 bytes | by the quarter of the coarse cell: qx = 3 - ((x >> 26) & 3), qz likewise, index qx + 4 qz (the high word of the position, bits 10 and 11) |
| coarse | A' > 0x5000 | `0x2E632`, 16 lists of 16 bytes | the same index |
| fine | A' > 0x10000 | `0x2E7C2`, one list of all 25 types in order 0 to 24 | none |
| fine | 0x9000 < A' <= 0x10000 | `0x2E772`, one list of types 0 to 8 (the 3 by 3 block) | none |
| fine | A' <= 0x9000 and (A' > 0x3A0, or A' <= 0x3A0 and the own-cell test below fails) | three families of 128 lists (16 phases x 8 octants): 4 bytes each at `0x2EEDC` for A' <= 0x3000, 6 bytes each at `0x2EBDC` for 0x3000 < A' <= 0x4800, 8 bytes each at `0x2E7DC` above | phase `V:4A908` x (4, 6 or 8 bytes x 8) + octant `V:4A90C` x (4, 6 or 8) |
| fine | A' <= 0x3A0 and the own-cell test passes | the fixed list {type 4} (the player's own cell only); `V:4A955` is set (dead) | see below |

The 3 by 3 list is stored eight times: the bytes `0x2E772`-`0x2E7C1` of `mesh_scan.bin` (80 bytes) are eight identical copies of the nine types 0 to 8, each followed by 0xFF (ten bytes each), so `0x2E77B`-`0x2E7C1` (71 bytes) is the first copy's terminator and the seven further copies; the table row above names the first, and the copies are interchangeable (checked on the extractor output). The own-cell test at very low altitude: a table of 16 phases x 4 signed bytes, chosen by A' (0x161 to 0x3A0: `0x2F0DC`; 0x11 to 0x160: `0x2F11C`; 0 to 0x10: `0x2F15C`; 64 bytes each, `mesh_scan.bin` offsets 0xADC, 0xB1C and 0xB5C). For the current phase the four bytes are octants; the test passes when the first byte is
negative or the octant `V:4A90C` equals one of the four. Padding is the value 50 (never an octant). The three tables are in `tables/mesh_scan.bin` at the offsets above; a phase with four padding bytes (50) passes no octant, a phase whose first byte is -1 passes every octant. Samples: in the 0x161 to 0x3A0 table phase 0 passes none, phase 1 only octant 3 and phase 5 octants 2 and 3; in the 0x11 to 0x160 table phase 1 passes octants 3 and 4 and phase 5 every octant; in the 0 to 0x10 table phase 0 passes octants 2 and 3 and phase 5 every octant. (These are the viewer-near, looking-away cases in which the neighbouring cells cannot show water: very close to the ground in the middle of a cell every octant draws only the own cell.) Lists seen: 16-byte coarse lists are 16 types each (types 0 to 12 and
some of 14 to 19); the 4-byte lists 4 types; the three phase families have 2 to 8 types per list (4-byte family 2 to 4, 6-byte 4 to 6, 8-byte 4 to 8).

**Per cell [V].** For a cell with the grid word w (a word <= 0 is a table error and the cell is skipped; every word of the shipped grids is positive), the record is at the grid base + w. Record layout (hunk 68, `0x47D7C` to `0x495CB`):

| offset | size | content |
|---|---|---|
| +0 | long | the alternate list: an absolute address of a polygon list (the image address; subtract `0x47D7C` for the offset in the hunk). **Negative: the cell has no water, nothing is drawn.** Records of land cells (38 of 103 distinct fine records, 1 of 7 coarse) are exactly these |
| +4 | polygon list | the detailed list; for 57 of the 65 water records in the fine grid and all 6 in the coarse grid the alternate pointer points here |
| list | words | a count n > 0 (at most 18; more is a table error and the rest of the cell is dropped) followed by n pairs (vx, vz) of words; or a **gate**: a count word that is not positive and not 0xFFFF, whose low 15 bits times 4 (as a 16-bit word, sign-extended) is compared with A': if the product is greater than A' the cell's list ends here, else the next word is the count of the following polygon; 0xFFFF ends the list |

Vertex words are in the unit of the pass (above), measured from the cell's origin in x and z; the shipped polygons are cell squares of 0 to 4,224 (the cell plus a margin of 128 on the +x/+z side so neighbours overlap) and coast outlines of up to 14 vertices. There are 6 distinct coarse
records (7 with the land record) and 65 water records in the fine grid, 8 of which have an alternate list that differs (simplified coast outlines: the records at the carrier's coast; they are found by comparing each record's alternate pointer with its detailed list, and the eight grid values are not listed). Gates occur only in coarse records (`0x47D7C + 0x80` has four, `+ 0x154` one).

The list used for a cell is the detailed one, except when the **alternate** is chosen (the long at +0, same list format):

| condition (fine pass, `V:4A865` zero) | pre-shift s | alternate list |
|---|---|---|
| own cell (type 4), A' <= 0x400 | 2 | when `V:4A954` is non-zero |
| own cell, 0x400 < A' <= 0xC80 | 1 | when `V:4A954` is non-zero |
| own cell, A' > 0xC80 | 0 | when `V:4A954` is non-zero |
| other cell, A' <= 0xC80 | 1 | when the cell is far (below) |
| other cell, A' > 0xC80 | 0 | never |

With `V:4A865` non-zero (the map view) s is always 0, nothing uses the alternate, and the coarse pass has s = 0 anyway. The far test (other cells, A' <= 0xC80): with t = the word table `0x2F4F8` (`tables/mesh_far.bin`, 36 bytes, 18 words rising from 132 at index 0 through 240 at index 12 to 288 at indices 16 and 17; the last two are equal) indexed by min(A' >> 8, 17) (only 0 to 12 occur), scaled like A' when `V:4A895` is zero (t x (0x8000 / z) >> 8), the cell is far when |d_x| > t x 65,536 or, if that fails, |d_z| > t x 65,536, where d_x = (the cell origin minus the position, in x) + 0x800000 is the offset of
the cell centre (in position units, 32 bits). **Quirk [V]:** when the x test fires, the original keeps the distance itself as the flag and later tests only its low 16 bits: the cell is then far only if the low word of |d_x| is non-zero (that is, unless the position's x is a multiple of 65,536; it happened in the staged sea
states whose positions were round numbers); the z test sets the flag cleanly. Reproduce it.

**Vertices [V].** For a cell: o_x = the cell origin minus the position in x (a signed 32-bit number: dx times the cell size minus the masked x), o_z likewise, converted to the pass unit (fine: >> 12, coarse: >> 16, taking the low 16 bits). Height term
(a, b, c) = (-h m1, -h m4, -h m7) >> 8 each, with h as in the pass table and the signs as written (the ground lies below the eye). Each vertex (vx, vz) of the list gives X = o_x + vx and Z = o_z + vz (16-bit words) and the camera-space point
(((X m0 + Z m2) >> 8) + a, ((X m3 + Z m5) >> 8) + b, ((X m6 + Z m8) >> 8) + c), each sum of two 32-bit products arithmetically shifted, the result added as 16-bit words, then shifted left by s (the 16-bit word). The ground height is 0, so the y of the
vertex enters only through b. The polygon (count n, up to 18 points of three words) is handed to the polygon clipper `0x28B6C` with shift word 0 (`V:5103C` is cleared at entry); the clipper's convention is note 03's.

**Pixel result [V].** The pass sets the polygon colour `V:4AA0C` to 6 and the plane mask block `V:4A79E..4A7A4` to (2, 2, 0, 0). The filler writes plane 1 only: **under every polygon pixel the pen gets plane 1 set**: land 4 becomes 6 (sea), sky 5 becomes 7 (0x444, grey), sea 6 stays 6. Pen 7 pixels
appear where a polygon reaches above the horizon (37 pixels in the 73 original sky states; entry 7 is (43, 43, 43) in the frames); a pen 7 pixel in the frame is otherwise produced by the world stages only by a tiny-polygon dot on sea or sky in the cockpit view (a dot turns sea 6 into 7, "Filling the polygon"; none occurred in these frames), but the HUD uses pen 7 too, so the check compares the rig's pen 7 pixels against the frame, one way.
**Degenerate mesh polygons [V]:** a polygon the clipper reduces to a bounding box that falls into the filler's **line case** (at most one row high and at least two columns wide, or at least three rows high and at most one column wide) is drawn as a line that **replaces the pen with the colour 6** on all four planes: the mask is forced to 0xF (and the second mask word to 0xFFFF) for the line, because `V:4A85A` is 0 outside the near-ground pass, and then restored to (2, 2, 0, 0). Such a line turns land 4 into 6 like the plane-1 rule, but sky 5 into 6 and not into 7. The dot and block cases keep the pass's mask and set plane 1 like any other pixel. The line case is common: the original filler took it 76 times in 47 of the 210 rig-drawn states (sky states and mesh states, the polygons of the model through the original clipper), and the frames of all of them agree with the model.
Edge rules and clipping are the generic polygon routine's (note 03).

**The flag `V:4A954` [V].** Written on odd frames (`V:4A992` odd) by `0x230D8` from the lookup `0x230F8` on the table `0x233B8` (the same rule as `V:4A953`, "The base colour flag"). The table (`tables/sea_zone2.bin`, 80 bytes, `0x233B8`-`0x23407`, same layout as the sea-zone table) holds one zone: the player's cell column 16, row 17 (`V:4AA00` = 16, `V:4AA02` = 17), with one record: flag 0 (altitude A <= 2,560) and nine phase groups, one for each of the phases 4, 6, 8, 9, 10, 12, 13, 14 and 15, each with its octant list (the group comparison quirk of the sea-zone rule applies). Samples: phase 6 passes only octant 5, phase 12 every octant. **Effect:** only the own cell (type 4) of the fine pass is drawn from the alternate
list; that cell (grid value 0x105E) is one of the eight with a simplified outline. So `V:4A954` changes the picture only inside that cell. Verified on the 31 captured states with the flag set (frames identical to the model with the alternate list; 22 of them in the first mesh-c run, 31 in the logs now on disk).

**Order in the polygon stream [V].** Coarse pass first (if A' > 1016), then the fine pass; within a pass the cells in list order, within a cell the polygons in list order (the polygon list of a record is drawn top to bottom as stored). The pass draws nothing else. The count per frame: 0 to 63
(63 at the highest scaled altitude with all 25 fine cells; typically 5 to 25 in the cockpit).

**The pixel (14, 135) of the attitude state at pitch 135 degrees [V, I].** The rig's sky fill leaves pen 4 at (14, 135) and the mesh pass leaves it at 4 (the model and the original pass agree, and a pixel of the sea polygons above the horizon would be pen 7, not 6). The frame shows sea blue (pen 6) at the right end of a run of four pixels
(x 11 to 14, row 135) whose other three are grey pens (0x666 at x 11, 0x777 at x 12 and 13) in a sloping band of 0x333 above it: strokes of a later stage on the land pen 4, so plane 1 of that pixel was set after the mesh. Attribution by elimination, not by running the later stage: the pixel is an overlay pixel (the HUD's strokes), which the one-sided colour
comparison counts as a difference because pen 6 is also the sea colour. All other compared states are exact: 72 of the 73 compared sky states (74 staged, one torn and skipped) and every mesh state (134, counting `mb17r` for `mb17` and `mc12r` for `mc12`).

**Pure rule for Task 8 [V].** Input: the position (x, y, z) as above, the nine matrix words, A (the camera height in units of 256), the zoom word and the flags `V:4A895`, `V:4A865`, `V:4A954`, the phase and the octant (both derived from the position and the heading as in the table). Output: the ordered list of polygons (count, up to 18 camera-space triples
of 16-bit words) for the clipper with pen 6 and plane mask 2 (plane 1 set under the polygon), in the order of the stream: coarse polygons, then fine. The reference implementation is `meshmodel.py`; a unit test can use these vectors (all from the rig): the staged position x = 295,174,144, y = 600,000, z = 274,202,624 (cell 17, 16), matrix (63, 0, 154, 0, 252, 0, -118, 0, 48), A = 2,359, phase 9, octant 1, default zoom gives eight polygons with 14, 7, 12, 7, 4, 9, 9, 9 vertices (the first vertex of the first is (3535, -9, -1019), of the fifth (4270, -290, -596));
with x one larger (the low word of x is not zero any more) the same input gives 14, 7, 12, 7, 4, 5, 4 (the far test fires on the cell (-1, 0) and its alternate list is used).

#### Worked examples (rig, confirmed on the frame) [V]

The camera matrix words are those of the state block; altitude `V:4AB1E` = -566,501 (corner y = -3) in all four rows. Sky rows are the first and last sky row of the column in the frame.

| state (pitch, roll words) | matrix `V:4AC90` | polygon (screen) | counters (planes 0 to 3) | sky rows at column 0, 80, 160, 240, 319 |
|---|---|---|---|---|
| 0, 0 | 107 0 128 / 0 252 0 / -98 0 82 | (0, 89) (319, 89) (319, 0) (0, 0) | 0 1 0 1 | 1 to 89 in every column |
| 0, +1600 | 101 -58 120 / 55 236 65 / -98 0 82 | (0, 40) (319, 138) (319, 0) (0, 0) | 0 1 0 1 | 1 to 41, 65, 90, 114, 138 |
| 0, 14400 (inverted, flag 1) | -109 0 -130 / 0 -252 0 / -98 0 82 | (0, 89) (319, 89) (319, 179) (0, 179) | 0 1 0 1 | 90 to 144 (the window) in every column |
| 0, +3600 | 76 -119 90 / 114 178 135 / -98 0 82 | (52, 0) (265, 179) (319, 179) (319, 0) | 1 0 1 0 | column 0: no sky; column 80: 1 to 24; column 160: 1 to 91; columns 240 and 319: 1 to 144 (the window) |

Corner camera coordinates of the first row, for the unit test of the crossing search: (-328, -3, -6872), (-9465, -3, 1098), (328, -3, 6872), (9570, -3, -198); the crossings are in slots 5 (plane 1) and 2 (plane 3).

#### Stage [90]: the near-ground pass [V]

The pass is the routine `0x2C0EC` (hunk 25, offset 0). The frame routine calls it at `0x11212` with the marker word `V:4AB8C` = 0x90, **after the two walkers of queues A and B and before the entity pass** (marker 0xA0: the direct entities, then queue C), inside the block that is skipped when `V:4AB1E` is at or below -0x8000000. It has no view test of its own: the pitch band `V:4A923` only picks the height limit, and the map and external views reach it like the cockpit. **It draws no tiles and no pen 3 shapes.** It scatters a fixed lattice of specks on the ground, point sprites and small triangles, and **a speck takes plane 2 away from the pixels it covers**: over the sea pen 6 (0x036, blue) becomes pen 2 (0x888, grey), over land pen 4 (0x151, green) becomes pen 0 (black), sky pen 5 becomes 1 and pen 7 becomes 3; a pen without plane 2 stays. The specks are therefore dark on land and grey on water, drawn over the terrain and the objects of queues A and B and under the entities, queue C, the HUD and the panel. In the carrier-deck snapshot of the openers (`cockpit-carrier.vasnap`) the pass draws eight specks (two 1 by 1 sprites, three 2 by 1 sprites, three line-case triangles) of which three pixels show, the 2 by 1 sprite at (87, 101) and (88, 101) and the 1 by 1 sprite at (36, 107): they turn sea pen 6 into pen 2, while the others fall on the black deck (pen 0, no plane 2) and change nothing.

| step | rule | constants and addresses | tag |
| --- | --- | --- | --- |
| 1. entry | **before any test** the fill block `V:4A79E..4A7A5` is stored as mask 4 (plane 2 only), second word (the pattern word `V:4A7A0`) 0, toggle word 0, `V:4A7A4` = 0xFFFF, and the colour word `V:4AA0C` = 3; nothing restores them (the block is (4, 0, 0, 0xFFFF) and the colour 3 after the pass, also when the height test below fails) | `0x2C0F8`-`0x2C106` | V (144 of 144 emulator runs stopped after the pass show the block `0004 0000 0000 FFFF`; the rig leaves it at the heights above the limit too: 300 u in band 0 and others) |
| 2. height gate | H = -`V:4AB30` (the camera height in u; the long). The pass goes on when H <= 2,048 (band `V:4A923` 0), 3,456 (band 1), 4,608 (bands 2 and 3); otherwise it jumps to the exit | `0x2C116`, `0x2C120`, `0x2C128`, compare `0x2C134` | V (rig, `04a` section 8; the boundary one unit either side) |
| 3. flag | `V:4A85A` := 1 (cleared at the exit, `0x2C360`, on every path) so that the filler's line case does not force the mask to 0xF (the lines of this pass clear plane 2 only) | `0x2C13A`, `0x2C360`, read at `0x34D84` | V |
| 4. table | L = the low word of `V:4AB30` (signed): L >= -128 (H <= 128): table A, **96** entries, class limit **2**, scale shift **3**; -224 <= L < -128 (H up to 224): table B, **80** entries, limit **3**, shift 0; L < -224: table C, **64** entries, limit **4**, shift 0. The far flag F = 1 when the long `V:4AB30` is below -512 (H > 512) | table start words `0x2C840`, `0x2CA84`, `0x2CC68`; compares `0x2C178`, `0x2C18A`, `0x2C146` | V |
| 5. lattice offset | cx = `V:4AB2A`, cz = `V:4AB2E` (words: the negated camera position in u, wrapped at 16,384): cx' = cx + ((0x400 - cx) anded with 0xF800) in word arithmetic, likewise cz'. cx' is cx reduced to -1,023 .. 1,024: **the lattice is a grid of nodes 2,048 u apart in both axes (at the multiples of 2,048 of the position), cx' and cz' are the nearest node's offset from the camera** (cx = +1,024 and cx = -1,024 both give cx' = +1,024); the pattern jumps by 2,048 u when the camera passes the middle between two nodes | `0x2C1A4`-`0x2C1C8` | V |
| 6. candidate | each table entry is (x, z, k), words in u relative to the node. X = x + cx', Z = z + cz' (words). The class c is the byte of the class table at row (|Z| >> 8), column (|X| >> 8) (word negation), 32 bytes per row. The entry is a candidate when c <= the limit of step 4; others are skipped | class table `0x2C440`, 32 by 32 bytes; compare `0x2C26C` | V |
| 7. kind | k = 1: a 1 by 1 sprite (`0x340D4`); k = 2: a 2 by 1 sprite (`0x340EA`); k = -12, -16, -20: a **triangle**, except that it becomes a sprite when F = 1 or c >= 2 (k = -12 becomes the 1 by 1 sprite, -16 and -20 the 2 by 1 sprite) | `0x2C274`-`0x2C292`, `0x2C418`, `0x2C420` | V |
| 8. position | the point is (X, Z) shifted left by the table's shift (word arithmetic, so the unscaled value is multiplied by 8 in table A only) and the height H' = L shifted left by the same shift. With the camera matrix words m0 .. m8 (`V:4AC90`, row by row): x' = ((m0 X + m2 Z) >> 8) + ((H' m1) >> 8), y' = ((m3 X + m5 Z) >> 8) + ((H' m4) >> 8), z' = ((m6 X + m8 Z) >> 8) + ((H' m7) >> 8), every sum and product truncated to a word as the code does (arithmetic shifts, products in 32 bits). The point is dropped unless z' > 0 and x' <= z', -x' <= z', y' <= z', -y' <= z' (the closed pyramid, word negation). Screen: sx = 160 + (x' 160 / z'), sy = 90 + (y' 90 / z') (divides truncate toward zero), sx clamped to 0 .. 319 and sy to 0 .. 179; **the pixel is (319 - sx, 179 - sy)**. This pyramid test needs z' > 0 strictly (z' = 0 drops the point), where the polygon clipper's closed pyramid of `03` ("Clipping and projection") keeps a hit with a non-negative depth; the pass's words are also compared and negated as words (-x' of -32,768 is -32,768; the model uses word negation, the rig runs of random states do not reach that edge: C) | `0x2C2F4`-`0x2C33E` | V |
| 9. sprite | a sprite is dropped when its row is beyond the last 3D row `V:4AA3C`; the plotters then take the pixel (a sprite with row <= 0 is rejected): the 1 by 1 sprite clears plane 2 of (X, Y); the 2 by 1 sprite of (X - 1, Y) and (X, Y) (at X = 0 of (0, Y) and (319, Y - 1)) | `0x2C400`, plotters `0x340D4`, `0x340EA` | V |
| 10. triangle | the three vertices are the shape's offsets added to the scaled point (before the matrix; step 8 for each); **all three must pass the pyramid test**, otherwise the whole triangle is dropped; there is no row test. The three screen vertices go to the filler `0x34A28` (count word 3 at `V:5043C`) | shapes below | V |
| 11. filler cases | the filler's cases of "Filling the polygon" apply with the pass's block: bounding box of at most one row and two columns or more (line: pixels (minx .. maxx, miny + 1)), three rows or more and at most one column (the diagonal line), the 2 by 2 block and the dots all **clear plane 2 of their pixels** (mask 4, colour 3, pattern 0; the line case because `V:4A85A` is set). A triangle of the **general fill** (three rows or more and two columns or more, or two rows and three columns or more) takes the **alternate blit** `0x34EEC` (the pattern word is not negative and `V:4A7A4` is not zero): over the rectangle of whole words from the word of max(minx - 1, 0) to the word of maxx and the rows miny to min(maxy, L), **plane 2 := plane 3 under the filled area** (the area of the general fill, edges and inclusive fill); a pen with plane 3 set (pens 8 to 15, objects) gets plane 2 copied from it, not cleared. **Elsewhere in the rectangle plane 2 := the plane-2 bit read W words further to the left for every row up** (the C modulus, "The blit's C channel" below); outside the rectangle nothing changes | `0x34A28`, `0x34D3C`-`0x34DCC`, `0x34EEC` | V (the bitmaps and the blitter trace; see below) |

**Shapes of the triangles (step 10).** Six shapes of three (dx, dz) pairs at `0x2CDEC`-`0x2CE33` (12 bytes each, words in u before the matrix, added to the shifted point as words and not scaled again): for the unscaled tables the shapes of k = -16 and k = -20 are isosceles triangles of half-width 3 and 5 (apex offset 2 and 4, the first two vertices at dz = -half-width); table A uses three larger shapes (half-widths 11, 21, 43 for k = -12, -16, -20) that sit under the shift of 3, so they are 8 times smaller in u than their numbers; the unscaled k = -12 shape is in the image and never used (tables B and C have no such entry). The extractor exports the six shapes; the port reads them.

**The three point tables and the class table (characterised; the extractor exports them: 1,452 and 1,024 bytes).** Each point table starts with the word pair (count, class limit) and holds the entries as word triples (x, z, k). Table A (count 96): k = 1 in 22 entries, k = 2 in 26, k = -12, -16, -20 in 16 each; table B (count 80): k = 1 in 47, k = 2 in 1, k = -16 and -20 in 16 each; table C (count 64): k = 1 in 48, k = -20 in 16. In all three tables x runs from -3,744 to 3,776 and z from -3,968 to 3,776 (u relative to the node). The class table is monotone (non-decreasing along every row and every column), minimum 1, maximum 9, not symmetric in its two indices and not a closed formula (about 1 + (distance in 256-u cells + 0.75) / 4, with 147 of 1,024 cells off); only row <= 19 and column <= 18 can be reached (the largest |x + cx'| is at most 4,800, 3,776 + 1,024). The cells with a class at most 2, 3 and 4 are 47, 101 and 197 of 1,024; in row 0 the last column of class at most 2 / 3 / 4 is 7 / 11 / 15, and in row 7 it is 1 / 7 / 13. For a camera exactly on a node (cx' = cz' = 0) the candidates are 34 of 96, 42 of 80 and 52 of 64.

**Counts per frame (V, 12,000 random camera states, rig).** The pass was active in 11,891 of them (the others lay above the height limit of the band). Per active frame: 42.5 candidates (the deck at 125 u: 35; the emulator states at 135 to 500 u: 38 to 51), 4.1 one-by-one sprites, 2.9 two-by-one sprites and 0.7 triangles (about 7 sprites and fewer than one triangle per frame; most candidates lie outside the pyramid of the view). In the 132 emulator states (below) 416 sprites, 54 line, dot or block triangles and 46 general-fill triangles were drawn.

**What was compared.** (1) `nearground_check.py` (Musashi rig): the original pass with the sprite plotters and the filler replaced by loggers, against `nearground_model.py` (steps 1 to 10), on 12,000 random camera states (four seeds of 3,000: random rotation matrices, offsets, heights 0 to 4,608 and all four bands; 11,891 active, 504,754 candidates, 48,425 one-by-one sprites, 34,272 two-by-one sprites and 8,802 triangles): the call sequences are equal (kind, coordinates, order) in all of them, and the flag `V:4A85A` is 0 afterwards. (2) `nearground_pixels_check.py` (the rig with the blitter model of `tools/vamiga-ref/sky-ground`, original code, random pen maps): the planes after the pass equal `nearground_pixels.py`'s prediction in every pixel for 1,200 states (three seeds of 400: 4,688 one-by-one sprites, 3,944 two-by-one sprites, 1,240 triangles of which 82 take the general fill). (3) `nearground_frames_check.py` (reference emulator; `gen-nearground.py` stages 144 paired states by the recipe of the sky-ground captures, camera heights 135 to 500 u over the carrier, the runway and the airborne place, pitch 0 to 7,200, with enlarged triangle shapes in 48 of them, and stops the game right after the pass with a breakpoint at the instruction after the call, `0x11218` (the driver's `runto`), so that the bitmap holds exactly the stages up to [90]; each state is run to that breakpoint, then once more for one whole frame with the pass and, in the second run, with its entry replaced by RTS; captures `extracted/captures/ng3`): of 144 pairs 138 reached identical values of the six camera words the pass reads, logged after each run (`V:4AB2A`, `V:4AB2E`, `V:4AB30`, `V:4A923`, the matrix `V:4AC90` and `V:4AA3C`; the other 6 pairs were staged at different instants and are discarded), one more pair differs in plane 3 (an object drawn differently: the pass writes plane 2 only, so it is discarded too), and in **all 137 the bitmap rows 0 to L after the pass equal the prediction pixel for pixel (5,975 changed pixels)**. The cases that decide the rules: the 50 general-fill triangles; in 7 states the alternate blit (plane 2 := plane 3) differs from a plain clear of plane 2 under the area (pens with plane 3 set; the checker counts them as `alt_decides`) and the bitmap follows the alternate blit in all 7; the checker fails unless the port's rule (alternate blit, C read with BLTCMOD = 40) equals every paired state. No state of these captures decides the C modulus (plane 2 is uniform where C is read); the blitter probe below decides it. (4) An earlier comparison on finished frames (HUD included: five snapshot scenes and 36 staged pairs) found every changed pixel inside the predicted set, except where later HUD drawing covers the pixel or the two frames of a pair were caught at different instants (the carrier deck: 3 of 3 pixels, the runway snapshot 19 of 19).

**The blit's C channel [V].** The alternate blit (BLTCON0 0x0FCA, BLTCON1 2: descending, A = the filled scratch plane, B = plane 3, C = D = plane 2, D = A ? B : C) never writes a modulus: A, B and D use 40 - 2 W, left by the fill blit just before it (`0x34ED8`-`0x34EE0` write BLTAMOD, BLTBMOD and BLTDMOD only), and **BLTCMOD holds 40, written by every edge line of the triangle (`0x3516A`)**. The chip applies each channel's own modulus at the end of each row and, in descending mode, subtracts it (Amiga Hardware Reference Manual, 3rd edition, chapter 6, "DMA Channels": "each DMA channel has its own modulo register", added after each row; "Descending Mode": "the modulo values will be subtracted rather than added"). So **for the row r of the rectangle counted from its bottom row (r = 0) and its word i counted from the right (i = 0), C is read at the plane-2 address of the bottom-right word minus r (40 + 2 W) minus 2 i**: on the bottom row in place, on every row above W words further to the left (across the start of the row into the row above, and below the start of plane 2 into the end of plane 1, the planes of a page being contiguous, 8,000 bytes apart). No C read meets a word the blit has already written (above the bottom row every C address lies W r words or more left of the words of its own row and below no written row), so the source is the planes as they were before the blit. **Rule for the port: in the rectangle, a pixel under the filled area gets plane 2 := its plane 3; any other pixel of the rectangle gets plane 2 := the bit at the same position of that C word read from the planes as they were before the blit; outside the rectangle nothing changes.** Where plane 2 is uniform around the triangle (open sea, open land) this equals "unchanged outside the area"; next to a coastline, the deck or an object the copied strip shows. The rig's blitter (`rigblit.c`, ported from vAmiga) does the same and stays as it is.

**Extent of the rectangle, row 0 and the wrap [V for the extent, R for the wrap].** (1) The rectangle rule (left word of max(minx - 1, 0), right word of maxx, rows miny to min(maxy, L), the blit starting at plane 2 of the page plus the bottom row times 40 plus twice the right word) was compared with every traced alternate blit against the model's triangle (`nearground_rect_check.py`, the 38 probe states, also in the fill run): H, W and the D pointer equal in 38 of 38 (W 1 to 4; none of the 38 had miny = 0, a bottom row cut by L, or a left word clamp, so those three clauses are the code's and the rig's, not the trace's). (2) **Row 0 is inside the rectangle when miny = 0**: the blit then covers rows 0 to the bottom row, and row 0 has no area bit (the top row of a fill holds no edge pixel), so it takes the C strip. V in the rig: 12 of 12 looking-down states with a general-fill triangle reaching row 0 changed plane 2 of row 0. miny = 0 is reachable: a vertex at the top pyramid edge projects to row 0 (sy clamps at 179); in 40,000 random looking-down camera states (pitch 69 to 90 degrees, heights 100 to 500 u) 97 of 28,660 pass triangles had miny 0, 24 of them with the general fill (about 0.1 % of the triangles); the sprites at row 0 are rejected by the plotters, and a clipped op-26 polygon reaching row 0 would take the same path (not run). Row 0 is not displayed (the viewport starts at bitmap row 1), but it is a destination and a C source: the port must not clamp the rectangle at row 1 (`WIN_Y0`). (3) **The wrap into plane 1 was never observed** (R: the traced C offsets reach down to +140 bytes from the plane start in the 38 states; the arithmetic is the chip's, tested by the probe on rectangles that do not wrap). It happens when the top row's C address is negative, that is miny * 40 + 2 wr < (H - 1) * 2 W + 2 i, for tall rectangles near the top rows (miny small); then C comes from the end of plane 1 of the same page (planes 8,000 bytes apart, contiguous). **What the page holds there at stage [90] time (V, 144 of 144 probe states, cockpit views with L = 144):** row 0 of all four planes is zero in every state (row 0 is cleared every 32nd frame and nothing draws into it); the last rows of plane 1 hold the **instrument panel's plane 1** (rows 185 to 199 are non-zero in every state, and the bytes of rows 197 to 199 are the same in all 144 states): the page keeps its panel from the frames before, since the window stages rewrite rows 1 to L only. External views (L = 167 or 179: the panel is smaller or absent) were not captured here (C). The port supplies, for C reads before the start of plane 2, the plane-1 bytes of its page at the same offsets (the panel layer plane 1 in rows below L, zero in row 0), and zero for rows of plane 1 above the panel that the window has just rewritten as plane 1 of the sky and ground pens (read from the page as it stands before the pass).

**Verified by the blitter probe (V, 38 blits, 0 exceptions).** The reference driver traces the blitter (`blittrace`, `blitstop`, `runto`: at each BLTSIZE write vAmiga's own registers BLTCON0/1, the four pointers, the masks and all four moduli, and the accuracy level). `gen-nearground.py probe` / `probefill` run each of the 144 states to the pass, stop right after the BLTSIZE write of the first alternate blit, dump both pages and the scratch plane, and run to the BLTSIZE write of the next blit (the scratch clear `0x34F92`, which waits for the blitter and sets no C pointer), then dump the pages again; `nearground_blit_check.py` compares. 38 states draw an alternate blit; in all 38 the trace shows BLTCMOD = 40 (A, B, D: 40 - 2 W) and accuracy level 2 (vAmiga's cycle-exact SlowBlitter, the configured default), **the C pointer ends at its start minus H (2 W + 40)** (state nb016: start 0x4FC30, W 3, H 14, end 0x4F9AC, 644 bytes; nb019: 0x507E6, W 4, H 20, end 0x50426, 960 bytes), and plane 2 after the blit equals the rule above in every pixel, with nothing changed outside the rectangle. On the game's own planes none of the 38 decides (C in place and C shifted read equal words). With plane 2 of both pages filled with a 7-byte pattern at the stop (`pokefill`, a staging aid), **all 38 decide (4,608 pixels in which the two readings differ) and all 38 follow BLTCMOD = 40, none the in-place reading**.

**Why the first round found the opposite (R, explained).** The 2 "deciding" states of the first captures (`ng2`, nb016 and nb019, 29 pixels) were misread: those captures froze the machine at a random instant and then poked the RTS, and when the freeze fell inside the pass the "off" run still drew it, so its planes (the checker's background) already held the triangle's own result; the plain rule reproduces such a background (it is idempotent there), the C-shifted rule does not. In `ng2` 6 pairs show the pass's pixels in the "off" run (nb016: 229 pixel bits, nb019: 368, four others 4 to 14); with the breakpoint stop of `ng3` none does. Just before the blit, the probe's nb016 and nb019 planes are uniform where C is read, so the true result is the same under both readings. The earlier trampoline that forced BLTCMOD = 0x28 could not decide because 0x28 is the value it already had.

The pass also starts a third blit after the alternate one, the scratch clear (BLTCON0 0x0D0C, A = B = D = the scratch plane, `0x34F92`); it writes the scratch plane only and has no pixel effect.

**The block left behind, and who can inherit it.** The pass leaves the fill block (4, 0, 0, 0xFFFF) and the colour word 3 in every frame in which the call at `0x11212` is reached, whatever the height (V: 144 of 144 emulator runs stopped after the pass when active; the rig at heights above the limit). Writers of the block (V: the code and the rig; all are absolute-address references, no address register is ever loaded with the block, so indirect writes are excluded by that scan only. The counts reconcile like this: the 38 references to `0x4A79E` are the stores of the block, of which a multi-register word store of four data registers writes the four words 0x4A79E..0x4A7A5 at once (ops 17/18 `0x2478C`, 19 `0x24030`, 20 `0x24188`, 21 `0x24372`, 22 `0x24498`, 23 `0x24808`, 27 and 30 sharing `0x2457A`, stage [90] `0x2C0FE`, the sea mesh `0x2F1CA`, none of which names `0x4A7A4`), the others store mask and pattern only (frame start `0x1F914`, the two drawer entries `0x225F0` and `0x22D1A`, op 26 `0x23090` and `0x230AA`, the line ops `0x236DA` and `0x249C8`, ops 32 and 57, the ground tail `0x33178`, the filler's save and restore around the forced mask `0x34D7E`-`0x34D9A`, the HUD and panel sites `0x354FC` to `0x38C02`, and others); the 8 references to `0x4A7A2` are the four long clears of the toggle and `V:4A7A4` (`0x225FA` the plain drawer entry, `0x314EE` and `0x31776` two routines of hunk 30 that draw with the full block, `0x33188` the ground tail) and the four reads of the filler; the single reference to `0x4A7A4` is the filler's read at `0x34A40`): `V:4A7A4` is reset to 0 by an entry of the plain drawer (`0x225F0`, which clears the toggle and `V:4A7A4` together), by every operation that stores all four words (17, 18, 19, 20, 21, 22, 23, 27 and 30, also on their early-outs: `03`, "Fill state"), by the tail of the ground polygon routine (`0x33178`, block (1, 1, 0, 0)) **only on the paths that build a polygon** (the no-polygon path `0x3319C` leaves the block alone), and by the sea mesh stage. **The mesh stage stores its block at the routine's entry, unconditionally**, (2, 2, 0, 0) or (0xF, 0xFFFF, 0, 0) when `V:4A868` is non-zero, and the colour word 6 right after, also unconditionally, before any cell is scanned (so also when no cell survives the clip); the stage itself is called only when the sea-zone flag `V:4A953` is clear or `V:4A868` is set (`0x111BA`-`0x111CC`), and skipped otherwise. The HUD and panel code (hunks 35 to 38), the lines, the ops 26, 32 and 57 and the frame-start code store mask and pattern only (a long store of 0xFFFFF or 0x7FFFF to `V:4A79E`) and leave the toggle and `V:4A7A4`. The type B drawer sets mask and pattern at its entry and keeps `V:4A7A4`. **From the writer list it follows (R/C: derived, not seen in a drawn frame) that every type B object drawn after the pass without a reset in between, whose model writes a non-negative pattern word, takes the alternate blit instead of its pattern fill: the models with op 26 (36, 39-42, 99, 100: mask 3, pattern 3, which stores mask and pattern only) are the only consumers; the op-30 models (105, 106, 109-132) are not, because op 30 stores `V:4A7A4` = 0 before its first polygon**; and in the next frame, when neither the ground polygon tail (no sky polygon, looking down) nor the sea mesh (sea zone) runs, the type B objects of queues A and B meet the block too. Whether a shipped scene draws such an object in that window: in 600 random frames of the two logged scenes (deck, air; the rig walk of the direct entities and queue C with the block poked to the leftover) no real draw saw `V:4A7A4` non-zero (only the stub routine of the rig's run-time descriptors did), but the model-42 patch of the carrier's queue C was not isolated: C. The earlier count of 13 of 144 emulator runs that found the block (4, 0, 0, 0xFFFF) at the place of the removed pass is **not** used as evidence: the poke of RTS takes effect at the next entry, so those runs were stopped inside the pass of the frame before, and some of them were states (pitch 0) whose sky fill tail would have stored (1, 1, 0, 0). The port must carry the block as one global state with the writers and resets above so that this happens by itself.

**Obligations.** Task 10 (core, the scene stream): emit the pass's events per frame in table order: for each candidate that survives steps 7 to 10 a sprite record (size 1 by 1 or 2 by 1, pixel (X, Y)) or a triangle record (three screen vertices), computed from the camera words (`V:4AB2A`, `V:4AB2E`, `V:4AB30`, band, camera matrix, `V:4AA3C`) with the tables of step 4 and the class table; between the lists of queues A and B and the entities. Task 8 (frontend, the plot rule): a sprite or small shape clears plane 2 of its pixels (pen and 0xB); the general-fill triangle sets plane 2 of its filled pixels to their plane 3 and the other pixels of its word rectangle to the plane-2 bit read W words further left per row up (BLTCMOD = 40, "The blit's C channel"; `src/frontend/sdl/raster.c` `raster_fill_alt` still leaves them unchanged and must follow); the pass's pixels are in the window rows 1 to L only and under everything drawn later. The extractor exports the point tables (`0x2C840`-`0x2CDEB`), the class table (`0x2C440`, 1,024 bytes) and the six shapes (`0x2CDEC`-`0x2CE33`). The fill block and the colour word are left as in the table above.

#### Task 8 obligations

What the core must export per frame (new `scene` field, set by the world step from the camera it already builds):

1. The nine camera-matrix words after the zoom row scale (the matrix the objects use), the camera height long (`V:4AB1E`: minus the camera's height in position units), the pitch word (`V:4AB42` in the cockpit, `V:4AB4C` otherwise), the roll word (`V:4AB4A`),
   the "cockpit" selector, the window last row `V:4AA3C` and the zoom triple.
2. The side flag (bit 1 of `V:4A982`, rule above, recomputed every frame from the two angle words), the base-colour flag `V:4A953` (the zone lookup, re-evaluated on even frame numbers) and the fade target (2 while the strain accumulator `V:4A941` exceeds 5 in the cockpit view of the own aircraft, else 15; the accumulator and the latch follow the rules of the palette table).
3. A pure function of items 1 and 2 that returns the sky polygon as the screen vertex list (up to five vertices, or "none"), exactly as above, with a test against the worked examples and the rig (`tools/vamiga-ref/sky-ground/check_rig.py polygon`).
   No `sin`/`tan` approximation: the horizon rows depend on the corner arithmetic and the heading by up to two rows. The inputs of the zone flags (`V:4AA00`, `V:4AA02`, `V:4A908`, `V:4A90C`, minus `V:4AB30`) are read when `0x230B8` (even frames) or `0x230D8` (odd frames) run from the builder's tail, and the flags keep
   their value in between: the port evaluates the lookup on those frames only.
4. The extractor provides `tables/sea_zone.bin` (the 560 bytes at `0x23188`) and `tables/sea_zone2.bin` (the second table, 80 bytes at `0x233B8`-`0x23407`: one zone, one record, the flag `V:4A954` of stage [68]); the zone table has no other consumer in flight (the second table is stage [68]'s).

5. Stage [68] (new): the core exports the scaled-altitude inputs (A from the camera height, the zoom word, the default-zoom flag `V:4A895`, the map flag), the position of the viewed record or the external camera, the phase and octant, and `V:4A954`; a pure function `mesh(position, matrix, A, zoom, flags)` returns the ordered polygons of "Stage [68]: the sea mesh pass" (pen 6, plane 1 set, shift 0)
   to the scene's polygon stream between the ground base and the sun; it needs the extractor tables `tables/mesh_scan.bin` (2,972 bytes, `0x2E600`-`0x2F19B`), `tables/mesh_far.bin` (36 bytes, the word table `0x2F4F8`-`0x2F51B`, its own file) and `tables/mesh_cells.bin` (hunk 68, `0x47D7C`-`0x495CB`, 6,224 bytes, with absolute list pointers to be rebased by `0x47D7C`). Tests: the two worked vectors of that section and the model against the rig.
6. Tiny sky polygons: the frontend follows the table in "Filling the polygon" with the mask 1 (plane 0 only, always when a polygon is built) and the colour by the view and the game frame counter `V:4A992`: in the cockpit view 13 (a pen 13 line across row 1 when the horizon is at row 1, a 69-pixel run at the top left; dots set plane 0, so land becomes pen 5 and sea pen 7), except 0 in the frames whose counter is 18, 19 or 20 modulo 32; in the external view 0xFD80 (pen 0), except 4 in those frames; the rear view like the front view after its first dozen frames. No polygon is built in the map view.
7. The fade conversion needs the measured curve for every level of the fade steps: 0 to 7 are measured; 10 to 12 (entry 9 only, the sun) are not (open item 3).
8. Carried state the port must keep between frames: the ground-polygon candidate (three words, initial 0, shared by the camera call and the attitude-ball call of a cockpit frame, "The crossing search"), the fade counter `V:4A95B` (signed byte; decremented per blank only while level and target differ; re-armed 1 down, 2 up; initial 0) with the delay of the first step after a target change (counter plus one blanks), the zone flags between their even and odd refresh frames, and the game frame counter `V:4A992` for the colour of the tiny-polygon cases (special frames 18 to 20 modulo 32). `V:4A85A` is 0 in all of it.

What the frontend draws: for rows 1 to the row limit a ground pen (4 over land, 6 over the sea zone), then the sky pen 5 on every pixel of the filled polygon by the fill rule above (edge rows ymin + 1 to ymax, one dot per row on shallow lines, inclusive span of the
two edge pixels; the small polygons by the table of "Filling the polygon"; an edge whose first row is below the limit is skipped; the shallow-line length cut at the limit and the word-rectangle fill from max(minx - 1, 0): an edge pixel without partner fills to the rectangle's left edge), nothing in row 0 and nothing below the limit. This replaces `raster_sky_ground` and its half-plane. The mesh pass [68] and the lists then paint over it as before.

Palette: the 16 entries of fade step (15 minus the fade level) of `palette/fade.pal`, converted with the measured curve of note 06 (not by 17), reloaded when the level changes; the level moves toward the target one step per two blanks down and per three up (50 Hz tick in the frontend), the target is 2 while
`V:4A941` > 5 in the cockpit view and 15 otherwise; the core keeps the accumulator by the mission-clock second and the latch from the turn-rate rule. No altitude dependence.

Tests (`tests/frontend/test_raster.py`): the last sky row at pitch 0 is 89 and the first ground row 90; the worked examples; the 73 captured states of `sky-ground-*.txt` as the golden polygons (the rig produces them); the palette words at two heights equal; the fade steps.

#### Open items after this round

1. **Small polygons**: settled ("What the two variables hold in flight": the mask is written by the polygon routine, the colour follows the view and the frame counter modulo 32). Not staged: a tiny polygon in one of the special frames (counter 18 to 20 modulo 32, where the colour differs) and a dot case on the emulator (the staged states of `sky-ground-tiny.txt` have a frozen counter and gave lines only); they follow from the rig (`fmodel.tiny` checked against it).
2. **The natural sustained blackout**: the latch was set by flying (full back stick at 120 degrees of bank), the seven ticks that raise the accumulator from -1 above 5 were not held by the stick. Method: poke `+0x56` and `V:4A9FE` each blank to hold the latch (the staging in "blackout accumulator"), watch `V:4A941` and the fade with the per-blank script.
3. **Fade curve at the nibbles 10 to 12**: only entry 9 (the sun) holds them in the fade table. Method: a fade capture with the sun in view (heading towards the fixed sun direction), registers and frame per blank as in the cadence measurement.
4. **`V:4A868`**: constant 0 by code (no store exists); if set it would change the plane mask and run the pass over the sea zone. Method if ever wanted: poke it and compare the frame with the rig pass; not needed for the replica.
5. **The pixel (14, 135)**: attributed to the overlay by elimination; to prove it, run the HUD routines in the rig on the state and compare, or capture the same state with the HUD off.
6. **Stale frames**: a frame of a complex scene can lag the state block by a whole pose; the settle times of the mesh captures are 5 s (`mesh-a`, `-b`, `-d`), 4 s after a 2 s unpaused phase (`mesh-c`, `-e`) and 6 s for the retakes (`mesh-r`, `-r2`); the first shots of `ma12` and `mb31` (settle 2.5 s) were stale, `mb17` (5.6 ft above the ground) was inconsistent in block and frame at any time and was replaced by `mb17r`, and `mc12` had a torn camera matrix (replaced by `mc12r`, `sky-ground-mesh-r3.txt`).

### Visibility range and culling

- The maximum draw distance `V:4AA6A` comes from the 64-byte altitude table at h65+0x0
  (`0x4621C`; the port loads it from the extracted image, `assets/tables/range.bin`). The table is
  monotonic from 64 to 127: it rises by 4 per entry at first, then more slowly (entry 0 is 64,
  entry 6 is 87, entry 11 is 99), reaches 127 around entry 39 and stays at 127 to entry 63.
  Indexing: the negated altitude variable `V:4AB30` (a long that counts downward) is
  compared with 0x7FFF; above that the range is fixed at 0x7FFE; otherwise the low word is
  shifted right by 7, so the index is the altitude in steps of 128 of that variable's unit, and
  the range is the table byte times 256. The index can reach 255 (altitude 0x7FFF) while the table
  has 64 entries, so for altitudes above 0x1FFF the code reads the bytes that follow the table
  (other data); flight does reach them (the ceiling is 40,960 ft). Characterised by capture
  (`01b-flight-captures.md`): indices 64 to 255 read the bytes at 0x4621C + index (0..10 mostly, 0 to 5120 after the
  multiplication), and above -`V:4AB30` = 0x7FFF the value is the clamp 0x7FFE (reached at the 40,960 ft ceiling). **The value has no effect:**
  `V:4AA6A` has three references, the two writes at `0x1FA64` and `0x1FA7E` and one word compare at `0x1FB76` in the walker, whose condition codes
  are overwritten by the moves at `0x1FB80` onward before the call; no branch follows the compare, so no record is skipped by distance. The
  unit question therefore does not arise.
- Each display-list record has a distance word (stored at `V:4ABF8`), which the model draw routines use as a size scale (times the zoom `V:4AAFA`),
  not as a cull limit. It is the approximate camera distance in units of 16 u (20 ft) divided by 2^LOD. The walker refreshes it with a size-dependent
  cadence (`04a-display-lists.md`, section 8, step 4; thresholds on the distance in units of 20 ft, that is the word shifted left by the LOD): at or
  above 0x400 (20,480 ft) the stored word stands; below 0x100 (5,120 ft) it is recomputed at every walk; between them it is recomputed every fourth
  walk (frame counter 0 modulo 4 for an even item id, 2 modulo 4 for an odd id). The object is drawn in all three cases; only the refresh differs.
  (An earlier wording here said that below 0x100 the object is drawn only on selected frames; the code does not skip the draw there. Objects whose
  last draw produced nothing are skipped on most walks instead, `04a` step 5.)
- A record with a descriptor routine `0x21C40` (49 plain model descriptors: bridges, buildings, flat marking figures) is skipped by the walker
  unless `V:4AB1E` is at or above -0x380000 in queues A and B (-0x400000 in queue C); the sun cut-off in step 6 uses `V:4AB1E` against
  -0x8000000; the range index uses the different variable `V:4AB30`. There is no range window around the aircraft: the terrain cells that
  are drawn are decided by the list builder's view-dependent tile scan and the generation limit (`04a-display-lists.md`), not by this walker.

### View kinds and keys

State variables: `V:4A83D` is the view selector (0 = cockpit; any other value = an external
camera: -1, 1, 2, 3, 4 give the distance level). `V:4A85F` is the cockpit view code (0 to 13)
and `V:4A96A` the external direction (0 to 9). Keypad raw codes are the keyboard.device
codes (KP0 0x0F, KP1 0x1D, KP2 0x1E, KP3 0x1F, KP4 0x2D, KP5 0x2E, KP6 0x2F, KP7 0x3D,
KP8 0x3E, KP9 0x3F, KP. 0x3C, KP-Enter 0x43). The handler records each press as a bit in
two event words (`V:4AA50`, `V:4AA52`...) that the event executor consumes, which is also how the demo
and training playback re-inject keys.

**Cockpit** (selector 0): twelve headings at 30 degree steps (the first step each way is 28.2 degrees, yaw table 0x3239A, then 60, 90 ...), KP8 = straight ahead (0), KP2 =
rear (6), KP4 steps to the left (0 to 11, 10, 9 ... wrapping), KP6 steps to the right
(1, 2, 3 ...), KP7 and KP1 select two further views (12 and 13). The HUD x offset per view
code is a symmetric ramp (x16 pixels): -u, -2u, -3u steps for 1, 2, 3 (turning right), +3u, +2u, +u for 9, 10, 11
(turning left), 0 for 0, -H for 4 and the hold H (hidden) for 5 to 8, 12 and 13 (3 and 9 are off screen too); the values are disk data and are not listed (`tables/head_step.bin`, FNV-1a-32 `1df64ecf`; note 06 "Head views"). The camera is the player aircraft
position and the attitude of the aircraft **composed with the head rotation** (entity index `V:4A994`; the composition is the flight attitude routine 0x325B8 fed with the head triple, note 06 "Head views", note 03 "Camera attitude in the head and external views"; the head view returns to code 0 4 to 5 seconds after the key release, note 06).

**External** (selector non-zero, flag `V:4A865` zero, camera mode `V:4A966` 0 or 5): KP-Enter
enters it and sets the direction to 4 (behind). In this view the keypad picks one of
ten directions and the keys `]` (raw 0x1B, closer) and `[` (raw 0x1A, farther) step the
distance level. The direction table is a ring of 8 around the aircraft at height 3,
KP8 (0), KP9 (1), KP6 (2), KP3 (3), KP2 (4, behind: the **chase** view, the default), KP1 (5),
KP4 (6), KP7 (7), then KP5 (8, above and slightly behind) and KP. (9, below and in front).
With the table orientation (z positive ahead, x positive to the left) index 0 is in front of
the aircraft looking back at it and index 2 is on its right. In the cockpit the same `]` and
`[` keys halve or double a zoom factor `V:4AAFA` (0x20 to 0x80) instead.
The camera position is the aircraft position plus the table offset scaled by the distance
level (-1: x1, 1: x1.5, 2: x2, 3: x4, 4: x8, per word), rotated by the aircraft attitude matrix for ring directions 0 and 4 only (heading only for the other eight and for class 0x30), and the camera looks at the aircraft; see `06-cockpit-hud.md`.

**Tower** (superseded: it is a top-down map view above the aircraft, see `06-cockpit-hud.md`, "View kinds, settled"): the 'M' key (raw 0x37) sets the flag `V:4A865`, places the camera at a fixed
base point (x 0x10C00000, z 0x11400000, the stored altitude `V:4A71C`) with the 3D window
bottom 179 when entered from an external view. While in this mode `]` (raw 0x1B) lowers and `[` (raw 0x1A) raises the
camera altitude by 0x2000000 within 0x1000000 to 0x8000000 world units. Pressing 'M' again
leaves it **for the cockpit** (the altitude is saved; never back to the external view it was entered from, note 06 view item 2). (Measured later: the camera follows the aircraft's x and z at the saved altitude, looking straight down, north up.)

**Tracking cameras** (camera mode `V:4A966` 2 or higher): the camera stays at fixed points
chosen by a nine-state machine `V:4A86E` driven by the distance from the camera to the subject
(thresholds in the constants table); it follows the subject with a half-step smoothing, looks at
it and falls back to the ring when the subject is close. **Settled in `07-ai-and-weapons.md` section 6.1 and 6.2:** these cameras are the front end's approach cameras (camera modes 2, 3, 4 and 6, the stage machine of note 08 6.4, rig-checked by `camera_check.py`); in a flight `V:4A966` is always 0 and no camera mode follows a weapon or a crash. What follows an object in flight is the viewed record, the trail record and the ring tables (section 6.1).

Settled by code and by snapshots in `06-cockpit-hud.md` (section "View kinds, settled"): the "tower" is a top-down map view above the aircraft, the ring offset unit is 1024 position units, the external view starts at distance level -1, and the keypad-to-ring mapping is confirmed.

### The start marks of the overview map [V]

The numbers 1 to 4 of the location screen (free flight) and the single chosen number of the aircraft screen are drawn by the **mark pass `0x2FAB6`**, called at the end of every frame (`0x114D0`, after the world and the cockpit overlay), not by text records and not by the map overlay `0x2FC58` of the M key. It draws only when the game mode `V:4A95E` is 1 (free flight), the camera mode `V:4A966` is 5 or more (5 the location screen, 6 the aircraft screen) and the camera height long `V:4AB1E` is below -0x1800000. The camera is the external camera (`V:4A83D` non-zero): the longs `V:4ACF6`, `V:4ACFA`, `V:4ACFE`.

**The overview camera.** The state sampled at the location screen (`followup-mapcam.txt`, the snapshot `location-select`): camera mode 5, `V:4A83D` = 1, the map flag `V:4A865` = 0xFF, the flag `V:4A90F` = 1, the camera longs x = 0x10800000, y = 0x03000000, z = 0x10C00000, the height long `V:4AB1E` = -0x03000000, the aim pair `V:4AB4C` = 7,200 and `V:4AB4E` = 0 (straight down, north up), the nine matrix words `V:4AC90` = (168, 0, 0, 0, 0, 252, 0, -128, 0) (the map scales of "Map renderer" in note 06), window row `V:4AA3C` = 179, HUD step `V:4AA3E` = 50 and `V:4AA40` = 800. These are the values the port's overview already uses (`MAP_OVERVIEW_X/Y/Z`); the planted values of the reset block (0x10C00000, 0x05000000, 0x11400000, note 08 6.1 item 7) are where the mover starts, and 0x10800000, 0x03000000, 0x10C00000 where it stands at the selection screens.

**The projection.** The start table `0x47ADE` (16-byte entries, note 05 17.14) is walked from entry 0 to the entry whose first word is 0xFFFF. For a runway entry (first word not negative) the world point is x = cell x * 2^24 + dirx(n) * 1024 + ox * 1024 + fx * 16 and z likewise (the formula of the player start; n, the direction pair from the table `0x206F2`, the offsets and the fine offsets are words 2 to 6 of the entry); for a ship entry (first word 0x8000 + record r) the point is the record's x and z (`+0x14`, `+0x1C`) when the record's active bit (bit 6 of the byte at +1) is set, else the entry is skipped. The point relative to the camera: dx = (x - cam x), dy = -cam y, dz = (z - cam z), each as a 32-bit difference shifted right by 14 (arithmetic) and taken as a word; the three camera-space words are X = (dx m0 + dy m1 + dz m2) >> 8, Y = (dx m3 + dy m4 + dz m5) >> 8 and Z = (dx m6 + dy m7 + dz m8) >> 8 (each sum a signed 32-bit, the result a word). **Visibility**: the point is dropped unless X < Z, Y < Z, -X < Z, -Y < Z and Z > 0. Then px = trunc(160 X / Z) + 160 and py = trunc(90 Y / Z) + 90, each clamped to 0 .. 319 and 0 .. 179, and the screen point is **(319 - px, 180 - py)**; the point is dropped when its row is below the window row `V:4AA3C`. The **dot** (pen 2, one pixel) is set at that point; the **digit** (the entry index + 1, pen 9, the 3 by 5 digit glyph of the version 2 digit drawer `0x37508`) has its top-left pixel at **((x and not 3) + 8, y)**: the drawer places the glyph in steps of four pixels. Order per entry: its dot, then its digit; a later entry's dot covers an earlier digit pixel and a digit pixel covers an earlier dot; a glyph is cut at the right edge of the screen (nothing wraps). With the overview camera the four starts fall at the dots (170, 96), (211, 73), (253, 146) and (83, 114) and the glyph top-left corners (176, 96), (216, 73), (260, 146) and (88, 114) for the airfields 1 to 3 and the carrier (free flight's carrier position (295174144, 274202624)): **the pixels equal the frame of `freeflight-select` and the port's fitted table (`MARK_XY`)**; the "5 and 6 pixels" of the old note were the offset of the glyph from the dot, which is the four-pixel step plus 8, not a constant. The carrier's mark exists only while record 14 is active, and the fourth table entry (record 15) never.

**Camera mode 6, the aircraft screen.** With `V:4A966` = 6 only the entry whose index equals `V:4A900` is drawn, and, when `V:4A90F` is non-zero, only on the passes in which the counter `V:4A93B` (incremented once per call of the pass) is odd; with `V:4A90F` = 0 the chosen entry is drawn on every pass. **On the aircraft screen `V:4A90F` is 0** (sampled in `followup-mapcam-air.txt`, the snapshot `aircraft-select`, six video frames: camera mode 6, `V:4A900` = 0, `V:4A90F` = 0, `V:4A93B` = 0x71, 0x74, ... 0x80, three passes per video frame, and the six frames hold the same 529 amber and 2 grey pixels), so the chosen number is steady there and the port's static number is right. A pause (`V:4A866` non-zero) draws every entry whatever the camera mode.

Verified: `mapcam_check.py` (900 random states: the overview's matrix and general ones, camera positions across the map, window rows 144, 167 and 179, a ship record poked at random positions: the pixel sets of pens 2 and 9 of the original's `0x2FAB6` in the rig against the rule, 0 differences, 854 dots), the emulator frame (`followup-mapcam-0.ppm`: 4 dots and 36 amber pixels, the rig with the sampled state and the carrier record reproduces all 40), tag V.

## Constants

| constant | value | unit / meaning | where |
|---|---|---|---|
| frame limit setting | 15 | ceiling frames per second (period 67 ms) | h4+0x17E; table `0x294F2` (words): -1, then 1000/n |
| limiter window | 1 to 8 frames | `V:4A992` modulo 8 plus 1 | `0x2992A` |
| vertical blank | 49.6 to 50.4 Hz measured | PAL | snapshots, see Verified by |
| bitmap A | 320 x 200, depth 5, 40 bytes/row | 8000 bytes per plane | h0+0x7F04 |
| bitmap B | 320 x 200, depth 4 | own planes (see Double buffering) | h0+0x7F04 |
| ViewPort | DWidth 320, DHeight 200, DxOffset 0, DyOffset -2, lores | pixels | live memory |
| View | DxOffset 0x81, DyOffset 0x2C | | live memory |
| window top row | 1 | bitmap row | h36+0x8FC |
| window last row | 144, 167 or 179 | bitmap row `V:4AA3C` | h5+0xDC2 and h0+0x41E8 |
| HUD step table (14 signed bytes) | a symmetric ramp of three sizes of a step u on the codes 0 to 3 and 9 to 11 and a hold H, the hidden marker, on the other codes (code 4's byte has the marker's opposite sign, note 06); the values are not listed here; bytes in `tables/head_step.bin` (FNV-1a-32 `1df64ecf`, exported by the extractor) | per cockpit view code 0 to 13; x16 pixels, 50 = hidden; the ramp's third size is off screen | `0x1E800`, h5+0xEBC |
| ground corners (x, z) | (10240, -9216), (-11008, -9728), (-10240, 9216), (9728, 11008) | offsets from the camera in the matrix input unit; y = high word of `V:4AB1E` shifted right by 2 (0 to -33) | `0x32E28` (h33+0x0) |
| screen clip used by ground fill | 0x13F by 0xB3 | pixels (319 and 179, the corner and clamp values of the polygon) | h33+0x380 |
| zoom triple | 0xA8, 0xFC, 0x80 (third word halved or doubled between 0x20 and 0x80 by the zoom keys) | row scale of the camera matrix: effective 210 columns and 177 rows per unit of tan at 0x80 | `V:4AAF6`, `V:4AAF8`, `V:4AAFA`: set at flight start by the three move instructions at `0x1D2E8`, `0x1D2F0` and `0x1D2F8` (note 06 gives their immediate words, `0x1D2EA`, `0x1D2F2` and `0x1D2FA`); the third word alone is reset to 0x80 at `0x1F068` (immediate word `0x1F06A`, with `V:4A895` bit 7 set at `0x1F060`) when the spawn switches to the cockpit seat (note 05); the keys halve it (arithmetic shift right at `0x1E826`, down to the minimum 0x20) and double it (`0x1E846`, up to 0x80) |
| horizon at pitch 0 | last sky row 89, first ground row 90 (any height 0 to 40,960 ft) | bitmap rows | 73 staged states |
| fade step | 15 minus the level `V:4A958`; 16 steps of 16 words | colour registers | `0x29FA4` |
| sea-zone table | 560 bytes | zone rows 12 to 19 | `0x23188` |
| second zone table (`V:4A954`) | 80 bytes (`0x233B8`-`0x23407`, `tables/sea_zone2.bin`), zone column 16 row 17, one record of nine phase groups, altitude A <= 2,560 | position units / 256 | `0x233B8` |
| mesh cell offsets | 25 pairs of signed bytes (dx, dz), type 4 = (0, 0) | grid cells | `0x2E600` |
| mesh scan lists | 2,922 bytes (the file `mesh_scan.bin` is 2,972 bytes with the 50-byte offset table) | cell types, 0xFF = end of a list | `0x2E632`-`0x2F19B` |
| mesh far-cell distances | 18 words, 132 to 288, x 65,536 position units (`tables/mesh_far.bin`, 36 bytes) | fine-pass distance test | `0x2F4F8`-`0x2F51B` |
| mesh grids | 8 by 8 words (cell 2^28) and 32 by 32 words (cell 2^24) | offsets to cell records | `0x47D7C`, `0x47F40` |
| mesh level thresholds | A' 0x10, 0x160, 0x3A0, 0x3F8 (coarse starts above), 0x400, 0xC80, 0x3000, 0x4800, 0x5000, 0x9000, 0x10000 | scaled altitude, units of 256 position units | `0x2F19C` ff |
| mesh eye height | 0x1000 (fine pass), none (coarse) | position units above the record origin | `0x2F2A0`, `0x2F610` |
| tiny-polygon colour word at the sky fill | cockpit 13 (0 when the frame counter `V:4A992` is 18 to 20 modulo 32); external 0xFD80 (4 in those frames); map 3; rear view as the front after its first frames | `V:4AA0C` | sampled |
| tiny-polygon mask block at the sky fill | (1, 1, 0, 0) whenever a polygon is built (stored by the polygon routine's tail `0x33178`, which also sets `V:4A956`), otherwise the frame-start block (7, 0xFFFF, 0, 0) in the cockpit and (0xF, 0xFFFF, 0, 0) elsewhere | `V:4A79E..4A7A5` | rig, sampled |
| near-ground pass constants | height limits 2,048 / 3,456 / 4,608 u by band; table A (H <= 128 u) 96 entries, class limit 2, shift 3; table B (H <= 224 u) 80, 3, 0; table C 64, 4, 0; far flag above 512 u; lattice 2,048 u (offset reduced to -1,023 .. 1,024); screen 160 by 90 centre, pixel (319 - sx, 179 - sy); fill block (4, 0, 0, 0xFFFF), colour 3 | u = 256 position units | `0x2C116`-`0x2C128`, `0x2C146`, `0x2C16C`-`0x2C196`, `0x2C1A4`, `0x2C310`-`0x2C33E`, `0x2C0F8`-`0x2C106`; tables `0x2C440`, `0x2C840`, `0x2CA84`, `0x2CC68`, shapes `0x2CDEC`-`0x2CE33` | V (rig and emulator, "Stage [90]") |
| near-ground flag | 1 while stage [90] draws (set at `0x2C13A`, cleared at `0x2C360` on every exit), 0 everywhere else, so the filler's line case forces the mask 0xF in the sky fill and in stage [68] | `V:4A85A`, read at `0x34D84` | V |
| fade counter | signed byte, decremented per blank while the level differs from the target, re-armed with 1 (down) or 2 (up) at each step, 0 in the image | `V:4A95B` | read, per-blank capture |
| ground-polygon candidate | three words persisting across calls (the camera call and the attitude-ball call share it), 0 at the start of a flight | `V:4AB7E`..`V:4AB82` | read, counted |
| visibility range | table byte x 256 | world units | `0x4621C` |
| range above 32767 altitude | 0x7FFE | | h8+0x84C |
| external ring (x, y, z), default class | a ring of 8 at height about 3 and radius about 14-18 (nearer 9 laterally at 45 degrees), then one position above and slightly behind and one below and ahead; samples: (0,3,15) and (17,3,0); exact values are the extractor outputs `assets/tables/camera_ring_*.bin` | camera offsets, index 0 to 9 | h26+0x830 |
| other ring classes | class 0x11, class 0x14 (a larger aircraft, radius about 44-48) and class 0x30, same layout of 10 entries; exact values are in `assets/tables/camera_ring_11.bin`, `_14.bin` and `_30.bin` | | h26+0x86C, 0x8A8, 0x8E4 (60 bytes each) |
| offset use | rotated by the matrix (16384 = 1.0), shifted right 4, added to the position: one offset word = x1024 position units | | h4+0x2F0 |
| distance scale | -1: x1; 1: x1.5; 2: x2; 3: x4; 4: x8 | | h26+0x90C |
| tower altitude step and range | step 0x2000000, 0x1000000 to 0x8000000 | position units | h5+0xBD8, h5+0xC78 (reached from `]` and `[` at h5+0xECA and h5+0xEEA) |
| fixed camera points | (0x10800000, 0x3000000, 0x10C00000), (0x10546800, 0x5000, 0x10A36700), (0xF248000, 0x5000, 0xFCCF000) | x, y, z | h4+0x268, 0x27C, 0x290 |
| tracking switch distances | 0x1C00000, 0xD00000, 0x200000, 0x60000, 0x30000, 0x60000, 0xD000, 0x8000, 0x2800, 0x800000, 0x100000, 0x4800 | max-axis distance | h26+0xAB4 ff |
| position format | 32-bit, camera x and z wrap at 0x400000 (22 bits) | | h4+0x2C4 |
| entity table | `0x4B23C`, stride 512 bytes (index shift 9); position at +0x14, +0x18, +0x1C; class byte +0x62; attitude matrix +0x92 | | h26+0x90C |
| level-of-detail thresholds | 0x100 and 0x400 | scaled object size | h8+0x84C |
| zoom keys | raw 0x1B and 0x1A (`]` and `[`): factor `V:4AAFA` halves or doubles between 0x20 and 0x80 in the cockpit | halve `0x1E826`, double `0x1E846` (handler h5+0xECA, h5+0xEEA); reset to 0x80 at `0x1F068` and `0x1D2F8` | `V:4AAFA` |

## Open questions

1. Re-check the cross-hunk claims of Tasks 5 and 6 against the corrected image (relocation
   was fixed in commit f1ffbb6 after this note's first version).
2. Visibility table: index range versus the 64 entries and the units of altitude and record
   distance (see Visibility range); a flight capture logging `V:4AB30` and `V:4AA6A` would settle it (milestone-3 opener: one emulator capture logging both with the carrier at two known distances settles the view-distance scale, the LOD shift and the range unit at once). Measured in `01b-flight-captures.md`: index and table confirmed, the table is read beyond its 64 entries above 10,240 ft, the carrier is drawn out to 10M position units, and 4AA6A has no effect on drawing (its only reader is a compare whose result is discarded), so there is no range unit to find and the walker does not cull by it.
   Also: which stage routines exactly draw terrain cells and objects: the display-list walker is
   identified, but who builds the two lists (range window around the aircraft), and which
   of the lists is near and which far, is not yet read. Settled in `04a-display-lists.md`: the builder is `0x1F770` (h8+0x598), there are three queues (`0x53A4A`, `0x540DA`, `0x5476A`) filled by a view-dependent tile scan with no range window, and the walker's compare against `V:4AA6A` is dead.
3. The stage at marker 68 (h28+0xB9C) and the near-ground pass at marker 90 (h25+0x0). Marker 68 is settled in role: it draws the pen 6 polygons of the ground mesh (the sea) over the ground base
   (settled: "Stage [68]: the sea mesh pass"). Marker 90 is settled in role and rules: "Stage [90]: the near-ground pass" (a lattice of specks that clear plane 2; no pen 3 tiles; the alternate blit's C channel is read with BLTCMOD = 40, settled by the blitter probe). Open there: whether a shipped scene draws a type B op-26 object under the block the pass leaves, and the C modulus of an alternate blit that runs without any edge line before it (all edges skipped: BLTCMOD then holds whatever the previous blit left; C).
4. Answered in `06-cockpit-hud.md`: `V:4A865` is the map (formerly "tower") view flag and `V:4A966` the tracking camera mode; captures of the map and chase views with camera
   values settle the camera rules.
5. World-unit to metre scale of the camera offsets and ranges (offset tables use values up
   to 48 and a shift of 4 on a Q14 matrix (16384 = 1.0), so one table unit is 1024 position
   units, about 5 feet at 204.8 units per foot; the earlier "16 units" assumed a unit of 0x100 and was wrong). `01b-flight-captures.md`'s 256 position units per offset is the unit of the camera-offset words `V:4AB2A` and following, a different quantity.
6. Rasteriser colour gaps: closed. (a) Ground colour: pen 4 (entry 4, 0x151) over land, pen 6 (0x036) over the sea zone; the horizon has no haze colour. (b) There is no altitude-band routine: the palette registers
   do not change from 0 to 40,960 ft in free flight (`01b-flight-captures.md`); `V:4A90C` is the view-heading octant (`04a`) and h8+0x376 only raises the list-refresh request. The palette changes only by the fade
   (blackout and mission-state changes), see "Sky fill and ground polygon". (c) The effect of `V:4A953`: it is the sea-zone flag, it changes the ground base from pen 4 to pen 6 and nothing else.
7. Which keys (and in which game situations) set the tower flag besides 'M', and what the
   training-mode text means by "point of view / distance" (the keypad drives the cockpit
   headings, the external directions, the distance level and the tower altitude).
8. Time base: answered by note 05 section 14. The flight constants carry no clock: unit conversions fit about 7 steps per second (a speed unit
   is 1/12 knot and a step moves 4 position units per unit), the 15 Hz limiter ceiling is never reached, and the reference machine
   ran 7.97 steps per second in flight (6.6 to 9.3 by scene). The step does not depend on the frame time (one step per frame, no flight rule reads the clock);
   a two-snapshot comparison at two frame times would only confirm this.
9. `00-binary-layout.md` places the key table in hunk 40; it is in hunk 39 (`0x37C92`,
   first row of digits at `0x37C93`, second row of letters at `0x37CA2`).
10. SDL flight render versus the capture (Task 10b, updated after fix round 1). The exact camera of
    `demo-deck.ppm` is not known, so the free camera was flown from its start (behind the carrier, looking
    along the deck) at full throttle and frame 13 compared by eye with `demo-deck.ppm` (game area crop;
    PNGs in `extracted/scratch/t10b/`, not in git). What matches: straight horizon with sky above and
    a sea band below, the near-black deck (pen 3) filling the lower view, and its rounded far edge
    cutting the sea like the capture's curved edge. What differs: the horizon row follows the camera
    pitch (0, row 90) where the capture shows it near row 32 (unknown aircraft pitch); the sky colour is the same (43, 43, 89 on the capture, entry 5; the first reading of "darker, about 0x224" was wrong, and
    there is no altitude-band palette, open question 6); the aircraft, the text strip and the dashed
    far-deck lights are missing (no entities or text yet); the deck appears only in a window of
    distances (frames 10 to 15 of the full-throttle run, then the carrier gives no polygons at lower detail
    levels or past the island), a suspected world-module detail-level or program-gate issue. Update: this was fixed by applying the precision bands (nearest-level thresholds) to the view distance; the carrier now draws continuously until it is behind the camera, but the distance scale remains an estimate. Update: `01b-flight-captures.md` shows the carrier drawn continuously from 0.6M to 10M position units. Milestone-3 opener: one emulator capture logging `V:4AB30` and `V:4AA6A` with the carrier at two known distances settles the view-distance scale, the LOD shift and the range unit at once.

## Verified by

No Musashi rig test exists for this slice. Evidence from the reference emulator
(`tools/vamiga-ref`, Kickstart 1.2, autoboot disk, restored attract-mode snapshot
`extracted/captures/demo.vasnap`, driven by a script with `restore`, `wait`, `frame` and
`snapshot`):

- Viewport rectangle (columns 186 to 825 of the 912-wide frame, sky first row at frame
  row 43 = bitmap row 1): measured on the frames `extracted/scratch/t7/d0-d2.ppm`.
- View, ViewPort and BitMap field values (320, 200, DyOffset -2, depth 5 and 4, 40 bytes per
  row): read from the memory of the saved snapshots (`a1`-`a3.vasnap`) through the hunk
  chain of the running program (hunk address from the segment list, chip memory base at the
  start of the RAM block in the file).
- Tick rate: `V:4A82C` and `V:4A992` and the system time read from the same three
  snapshots (8 s apart): 49.6 to 50.4 Hz blank, 4.2 to 5 game frames per second in that scene.
- Double buffering, plane memory, plane 5 empty, palette entries 16 to 31 black, sky = entry 5 on
  both pages: read from the snapshots (plane pointers, plane contents, colour table at `0x54F18`).
- Frame row of bitmap row 199 (241): measured on `cockpit-takeoff.ppm`.
- No reader of timer samples in the flight code: complete reference scan of the image (static).
- Window last row 167 (external) and 179 (cockpit rear view code 6) and the HUD offsets
  (`V:4AA3E` = 0x32, `V:4AA40` = 0x320): read from the snapshots.
- Sky fill, ground polygon, palette and sea-zone flag (section "Sky fill and ground polygon", milestone-3 round): the code runs in the Musashi rig with a blitter model
  (`tools/vamiga-ref/sky-ground/`, `rigblit.c`; the blitter model is a port of vAmiga's FastBlitter logic (GPL-3.0, see the header of `rigblit.c`)). (1) Model of the polygon routine (`gmodel.py`) against `0x32E5A` on 7,500 random camera states (pitch, heading, roll, height; both flag values; about a third of them with an external view (the selector `V:4A83D` set) and a random pitch word `V:4AB4C` for the whole-window test): no
  difference, in all polygon shapes and the "none" and whole-window results (`check_rig.py polygon`; since the divide model follows the instruction semantics the former skipped states are modelled and there are none); the side-flag rule against `0x143F2` on 3,000 random pitch and roll words including the boundaries (`check_rig.py flag`). (2) Model of the fill (`fmodel.py`) against the sky fill `0x3486C` on 1,850 random states, windows 144, 167 and 179, base flag 0 and 1 (the sea case clears plane 1 under the sky): the pen map of the four
  planes agrees pixel for pixel, the small polygons being compared by `fmodel.tiny` (`check_rig.py fill`, `tiny`). (3) Frames: 74 paused states staged on the emulator, 73 compared (`sky-ground-attitude.txt`: 44 attitudes at one place, `-height.txt`: 14 heights, `-sea.txt`: 16 sea-zone states,
  generated by `gen-sky-ground.py`; pitch -90 to +90 degrees, roll to a full inversion, heights 30,472 to 8,388,608 position units, the player record's angle words and height poked and the pause byte set), plus the 19 view frames of `views.txt` (cockpit side, rear, up and down views, the ten external
  views): of 3.1 million classified sky and ground pixels (sky pen colour against the two ground pen colours) none differs from the model (`check_frames.py`); one of the 74 (the pitch +1600, roll 0 attitude state) was skipped, its camera matrix having been caught between the build and the row scale of the zoom triple (the frozen machine can stop
  there). The staged frames needed 2.2 s after the poke; at 1.0 s one complex frame still showed the previous state. (4) The sea-zone model against the rig on 4,000 random lookups and against the flags `V:4A953` and `V:4A954` of the 220 state blocks in the current capture logs (retakes and the first shots included; 11 with `V:4A953` set, 31 with `V:4A954` set; no difference: `check_zone_logs.py`), and the base pen on the 16 sea-zone frames. (5) The sea mesh pass: the model `meshmodel.py` against the original pass `0x2F19C` in the rig (the clipper replaced by a logging stub, `meshrig.py`), 17,500 random states (position over and beyond the whole map, heights 200 to 8.4 million, zooms 0x80, 0x40, 0x20, 0x10 and 1, both flag values of `V:4A954`, `V:4A865`, external view,
  round positions to hit the far-test quirk): no difference in any polygon vertex or in the order (`check_rig`-style loop in the report; the loop is the one in `meshrig.py`'s docstring usage with `meshmodel.mesh`). Frames: the model's polygons handed to the original clipper `0x28B6C` after the rig's sky fill, compared with the frame (sky, sea, land colours by pixel; the rig's pen 7 pixels against the frame's (43, 43, 43); `check_meshmodel.py`):
  the 73 compared original sky states (3.13 million pixels; the original pass and the model agree; one pixel differs, (14, 135), see above) and the 134 mesh states (`sky-ground-mesh-a` to `-e`: 36 + 36 + 40 + 8 + 14, about 5.9 million pixels; 36 cockpit places over the map and heights 1,000 to 8,000,000; 40 in the cell (16, 17) at five heights (20,000 to 900,000) and all eight octants, five states per octant; zooms 0x40 and 0x20 reaching the 25-cell list; eight external-view and six map-view states), all exact. **How the mesh captures were staged.** The player record's angle words are rebuilt from its attitude matrices (record `+0x80`, `+0x92`) by every flight step, so a heading poked into the words alone survives only in a paused game, and the external camera's heading word `V:4AB4E` is written only by unpaused frames. The first mesh-c and mesh-e captures staged the position and keys unpaused and poked the words afterwards: all 40 mesh-c states and `md4` came back with the snapshot's own attitude (pitch 26,360, heading 2,256, octant 0, pause byte 0: the game ran at capture; heights 221,100 to 1,263,516 instead of 20,000 to 900,000; phase 8 in 11 states), and the 14 states of mesh-e logged octant 0 (the eight external ones because the camera heading word stayed at the snapshot's 2,240), so the octant paths of the cell (16, 17) and the external view were exercised by the rig only. They were re-shot with a working staging (`gen-sky-ground.py`: the unpaused phase flies a level, climbing for low states, attitude set through the matrices at the wanted heading; the final poke sets the words **and** the matrices) and every state is checked by `check_stage.py` (script and log: the last pokes before each `log` against the record and the state block; the octant against the heading word the camera used): in the re-shot logs mesh-c holds 40 of 40 states as poked (octants 0 to 7, five states each; the pokes of 5 states had been undone by a step between the thaw and the pause before the final poke included the matrices), mesh-d 8 of 8 (octants 1, 2, 3, 3, 5, 6, 6 and 1), and mesh-a and mesh-b 36 of 36 each (octants 0 to 7: 3, 2, 2, 7, 5, 6, 6, 5 and 4, 1, 4, 7, 6, 7, 2, 5); in mesh-e the eight external states give the octants 5, 3, 1, 5, 5, 3, 4, 6 (heading words within 3 to 22 units of the aircraft heading: the chase camera trails the aircraft) and the six map states octant 0 (the map camera is north up, the heading word is 0); in two external states (me1: pitch 1,608 for 1,600, heading 13,040 for 13,043; me7: the climb pitch of the unpaused phase for the poked 1,600) the final words were rebuilt by a step from the matrix, which does not change the octant. Retakes: `ma12` and `mb31` were stale at the first shot (settle 2.5 s; re-shot at 6 s as `ma12r`, `mb31r`, `sky-ground-mesh-r.txt`; the current a and b logs hold the 5 s shots, which compare exactly as well), `mb17` (5.6 ft above the ground; the frame and the block are inconsistent at any settle and the rig's own pass differs too) became `mb17r` (`sky-ground-mesh-r2.txt`, exact) and `mc12` (torn camera matrix) became `mc12r` (`sky-ground-mesh-r3.txt`, exact). Compared by `check_meshmodel.py`: the states a (36), b (35, `mb17` excluded), c (39, `mc12` torn), d (8), e (14) and the three retakes. The blitter model of the rig is validated **only by these frame comparisons** (the model-against-rig checks above compare two descriptions of the same code and cannot detect a wrong blitter): sky and mesh frames agree pixel for pixel, but lines of the polygon filler in unusual octants are covered only as far as these frames reach.
  (6) The fade: `sky-ground-blackout.txt` (accumulator poked): at every sample the 16 colour registers equal step (15 minus the level) of the fade table; per blank, 150 blanks (`step`, `peek` and `colors` each blank, `sky-ground-fade-perblank.txt`, log of the same name under `extracted/captures/logs`, frames in `extracted/captures/fade`): one level per 2 blanks down, per 3 up, with the byte counter `V:4A95B` read each blank (the rule above). The natural trigger: `holduntil` script `sky-ground-blackout-flown.txt` (log of the same name), latch set by flying (E = -461, `V:4A9FE` = -102, roll 122 degrees), accumulator 0 on the next tick and -1 on the one after; the keyboard pull alone: `sky-ground-blackout-pull.txt` (maximum |E| = 510). (7) Tiny polygons: eight staged pitch states (`sky-ground-tiny.txt`), the rig with the stale values reproduces rows 0 to 3 exactly; `fmodel.tiny` against the rig on 1,350 random boxes. (8) Divide overflow: 0 in 200,000 random rotations and 10,000 rig states.
- Stage [90] (section "Stage [90]: the near-ground pass", this round): `nearground_check.py` (rig, original pass against `nearground_model.py`, 12,000 random camera states, call sequences equal), `nearground_pixels_check.py` (rig with the blitter model, planes against `nearground_pixels.py`, 1,200 states from three seeds), `nearground_frames_check.py` on the bitmaps of the reference emulator (`gen-nearground.py` / `nearground-N.txt`, captures `ng3`: 144 pairs of runs, the game stopped by a breakpoint at `0x11218`, the pass removed in the second run of each pair; 137 usable pairs, all 137 equal the prediction with BLTCMOD = 40 pixel for pixel in rows 0 to L, 5,975 changed pixels, 50 general-fill triangles, 7 states deciding the alternate blit), `nearground_blit_check.py` (the blitter probe, `gen-nearground.py probe` / `probefill`: 38 alternate blits traced in vAmiga, BLTCMOD 40, C end pointer by the descending rule in 38 of 38, plane 2 by the BLTCMOD rule in 38 of 38, 38 of 38 deciding and following it on patterned planes, 4,608 pixels), the carrier-deck snapshot (3 pixels), and the fill block read after the pass (144 of 144). The scripts need the git-ignored `extracted/` inputs and `build/sky-ground/librigblit.so`.
- The filler's tiny cases, the two entries and the ball (milestone-6 sweep): `filler_check.py tiny` (777 boxes, both entries) and `filler_check.py ball` (708 polygons through `0x34C7C`), `filler_check.py state` (colour 3 and mask 0xF at the ball call in 64 of 64 states), 0 differences; the start marks of the overview map: `mapcam_check.py` (900 states, 854 dots) and the frames `followup-mapcam-0.ppm`, `followup-mapcam-air-0.ppm` to `-5` (note 01a); the sea mesh buffer layout is in note 03 (`mesh_check.py`).
- Everything else (draw order inferred from call order and referenced variables, the ring
  offsets, the keypad mapping) is read from the code and
  data of the relocated image and is **unverified** by capture (this includes the draw order, the
  keypad mapping, the step-not-scaled rule beyond the static scan and the tower and tracking camera roles; the projection row mapping, the ground polygon and the visibility range are verified or settled above): needs flight captures
  to confirm the ring directions and which key gives which direction.
