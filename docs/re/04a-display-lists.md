# 04a: the display-list builder (cell scan, the three queues, the walk)

Scope: how the original turns the player's position, the view direction and the terrain cell tables into the draw queues that
the frame loop walks, which cells and items enter, in what order, how many, when the queues are rebuilt, how their order is
refined every frame, and what is skipped when they are walked. This answers open question 2 of `04-scene-composition.md` and
replaces its "Visibility range and culling" paragraph (the range word `V:4AA6A` is dead, `01b-flight-captures.md`). Addresses
are flat-image addresses (`00-binary-layout.md`); `V:xxxxx` is a state word in hunk 71; a *position unit* (pu) is the unit of the
32-bit position longs and **u** is 256 pu (1.25 ft; the "offset" unit of `01b`). Formulas are in words; the numbers come from the
code and were checked in the Musashi rig and in the reference emulator. Each claim is marked in "Verified by" as verified (rig and
emulator, or rig with a stated model) or read-only (read from the code, never run).

**Corrections to earlier notes, up front.**
1. `0x26AF8` is **not** the list builder. It is the per-frame entity update (it steps the 16 records at `0x4B23C` and calls the
   per-entity flight routine `0x2A2A2`, see `05-flight-model.md`). The change detector at h8+0x376 calls it unconditionally and
   only *computes a refresh mask* (below). The builder is the routine at h8+0x598 (`0x1F770`).
2. There are **three** queues, not two: `0x53A4A` (tier A), `0x540DA` (tier B) and `0x5476A` (tier C, the fine tier).
3. `V:4A90C` is not a "cell-phase code" written by a cell updater: it is the **view-direction octant**, written every frame by
   h20+0x738 (`0x299AC`). `01b` and `04` were wrong on this and on the thresholds (`0xE10`...`0x6270` are the octant limits of
   a 28800-per-circle angle, not seconds).
4. The row index of the cell tables is the **z** cell (record word `+0x08`) and the sorted search key is the **x** cell
   (record word `+0x06`), the opposite of the old wording of `02-assets-and-tables.md` section 10 (corrected there). The scan
   reads row = `+0x08` and key = `+0x06`, and `05-flight-model.md` defines `+0x06` as the x cell and `+0x08` as the z cell, so the
   assignment is a reading of the code and of that definition. Check on the data: the carrier cell is x 70, z 65; table C has cells in the row z 70 (x 62, 63, 65, 66, among others) and a row
   for z 65 with the single key x 63, so the carrier's cell has no entry, and the deck capture indeed has no tier C terrain entry
   (the carrier and the player are entity entries only).
5. **Item offsets are relative to the sub-cell named by the item's low nibble**, not "inside the cell" (the offset unit is 256 pu
   = 1 u in table C and 1024 pu = 4 u in tables A and B; the nibble is the sub-cell, which also gives the draw order). This
   contradicts the old wording of `02` section 10 (corrected there) and the current `src/core/world.c` (cell/4096 unit, nibble
   read as a drawing-order layer, rows and keys exchanged in the extractor). Worked entry in section 10.
6. **Entities are inserted by tier C only** (no A/B quirk), a record's pending bit is cleared at insertion whether or not its
   sub-cell is later emitted, and bit 2 of the record flags is set by the entity update for records 14 and 15 and cleared by the
   builder when it emits the record (section 6). Earlier drafts of this note had all three wrong.
7. The queue order of tier C is **not final when the builder returns**: the per-frame sort of one run and the landmark reorder
   (section 5) change it every frame. The landmark collision slots of `05-flight-model.md` section 11b are filled at build time
   from the generated order, by the tail of tier C pass 2 (section 7), not by a per-frame post-process.
8. The descriptor routines were mislabelled; the right mapping is in section 3 (the table of `02` section 1 applies with
   item id = object id minus 16).

## Location

| what | where (flat; hunk+offset) |
| --- | --- |
| change detector, refresh mask (calls `0x26AF8`, then `0x1F706`, then the cell words) | `0x1F54E` (h8+0x376) |
| per-frame entity update (not the builder) | `0x26AF8` (h17+0x0); callers `0x1F54E` and h0+0x2CE0 |
| camera offsets `V:4AB1A`...`V:4AB30` | `0x1F45E` (h8+0x286), called from the frame loop |
| view octant `V:4A90C` | h20+0x738 (`0x299AC`), called from the frame loop after `0x32074`/`0x1F45E` |
| pitch gate `V:4A83E` (alternate scan patterns) | h0+0x43F2 (`0x143F2`) |
| **the builder** (reads `V:4A910`, builds the queues, sorts, reorders) | `0x1F770` (h8+0x598); callers: frame loop h0+0x1124 (every frame), mission start h4+0x6E (`0x1D1BE`), h0+0x201E |
| occupancy bit maps (built once at mission start, speed-up only) | `0x1F31C` (h8+0x144); maps at `0x1B74C`, `0x1BF4C`, `0x1C74C` (2048 bytes each) |
| per-tier scan driver, pass 1 (fills the slot buffer) | `0x2001C` (h8+0xE44); cell fetch `0x20304`; row search `0x203F4`; entity insertion `0x20430`, `0x204E8` |
| pass 2 (emits queue entries); tail of tier C pass 2: landmark slots and direct list | `0x20B0C` (h9+0x20); tail `0x210A2`-`0x211E4` |
| run sort (every frame) | `0x2122C` (h9+0x740), selection sort `0x213AA`; distance function `0x2082A`/`0x20884` (h8+0x1652/0x16AC) |
| landmark painter's reorder of tier C (every frame) | `0x21444`, callees `0x21AB4`...`0x21C00` |
| marks all entities and secondary objects "pending" at each rebuild | `0x1F992` (h8+0x7BA) |
| walker, tier A / tier B | `0x1FA24` / `0x1FA36` (h8+0x84C / 0x85E), common part `0x1FA48`; called at frame stages [78] and [80] |
| entity pass: direct entities, then the tier C queue | `0x1FBCC` (h8+0x9F4); stages [A8]/[AC]; entity draw position `0x1FFC6`, secondary object `0x1FFB4` |
| queue buffers | A `0x53A4A`, B `0x540DA`, C `0x5476A` (each 70 entries of 24 bytes = `0x690`), h73 |
| slot scratch buffer | `0x4D43C`, 14 groups of `0x600` bytes (h72, shared with the vertex table of note 03) |
| run records of the tier C sort | `0x4AA86`, 6 bytes each (start address long, count word), ended by a long -1 |
| direct-entity list | `0x53A2A` (h73): word indices, ended by -1 |
| item descriptor table (one 20-byte record per item id) | `0x2600C` (h16+0x140) |
| scan tables | see Constants |

## Behaviour

### 1. The request byte and when the lists are rebuilt

`V:4A910` is a request byte. The builder returns at once, leaving the request untouched, when `V:4AB1E` is below -`0x8000000`
(camera height above 655,360 ft). Otherwise, and only when the byte is non-zero, it marks all records and secondary objects pending (section 6: on every accepted
request, whichever tiers it selects; a zero request and the high-camera return mark nothing; rig: 0x0B and 0x0C leave 31 of 32 bits set, the
active player's cleared at insertion, a zero request 0, a camera above 655,360 ft 0 with the request kept), and with a zero byte does
nothing more in the tier passes. For a non-zero byte only the **low nibble** is checked and it must be
`0xB`, `0xC` or `0xF`; any other low nibble calls the internal-error trap `0x1F1D6` (code `0x27`; the rig does not trap and builds
what the bits select). High bits are ignored for the selection; the tier bits are cleared as each tier is built and the byte reaches zero after the last tier (a per-blank peek sees `0xFF`, `0xFE`, `0xF0`, then zero over two to three blanks; see open question 1). The tiers are selected **by bit**, tested in the order
A, B, C: bit 0 = tier A, bit 1 = tier B, bit 3 = tier C; bit 2 is only cleared and selects nothing.

| request | tiers rebuilt | writers in the game |
| --- | --- | --- |
| `0x0B`, `0x0F`, `0xFF` (and `0x1B`, `0x8F`) | A, B and C | detector, pitch gate, cell changes, mission start, camera switches |
| `0x0C`, `0x8C` | **C only**: the queues and the walked-window offsets `V:4AA64`, `V:4AA66` of A and B stay as they were | `0x0C`: a record deactivated; `0x8C`: a record spawned or armed (weapon launch) |

A rebuilt tier rewrites its queue and clears its own offset word; the A/B scan origin `V:4AA00`/`V:4AA02` is rewritten on every
accepted request, whichever bits are set. The port must **not** treat an arbitrary non-zero request as "rebuild all": 0x0C
leaves A and B alone. (Rig: build with `0x0B`, move the camera, then request `0x0C`: only queue C changed and the A offset kept its
value; `0x0B`, `0x0F`, `0xFF` and `0x1B` changed all three.)

Writers of the request, all of them found by searching the image for the absolute address (all OR a mask into the byte, except
mission start which stores it):

| trigger | condition | value | status |
| --- | --- | --- | --- |
| view octant changes | `V:4A90C` differs from its copy `V:4A90D` (the copy is updated whenever they differ, **also while the pitch gate is set**; the mask is just not raised then), and the gate `V:4A83E` is zero | `0x0B` | rig |
| 200 ft crossing | the camera height -`V:4AB1E` crosses `0xA000` (40,960 pu = 200 ft; `0xA000` counts as high) in either direction, comparing with `V:4AB26`, which is overwritten with the current height **every frame** | `0x0B` | rig |
| altitude band | the high word of -`V:4AB1E` shifted right by 5 (units of 2^21 pu = 10,240 ft) differs from `V:4AB16` (overwritten every frame, also in mode 2), unless the camera mode `V:4A966` is 2 | `0x0B` | rig |
| pitch gate toggles | `0x143F2`: v = the pitch long `V:4AB40` (in the cockpit this is the camera pitch composed with the head view, note 06: code 12 and 13 of a level aircraft give v = 2772 and 828, both inside the gate range; external view: the word `V:4AB4C`) divided by 8, in 0.1 degree (the circle is 28800); the gate is set while 600 < v < 3150, strictly. Positive pitch is **nose down** (`05`), so the gate is set for nose down by more than 60 degrees, through inverted, to nose up by more than 45 degrees; it is clear in level flight, in shallow climbs up to 45 degrees and in shallow dives up to 60 degrees. Every change of state ORs the mask | `0x0B` | rig |
| a record's cell changes | the position update `0x2A52x`-`0x2A68A`, run for **every** record by the entity update (not only the viewed one): a change of the x or z cell word | `0xFF` | read-only (cell test), rig for the viewed record |
| a record's sub-cell changes within 2 cells | same routine: the sub-cell byte `+0x0A` changes and the record's cell is within 2 cells in x and z of `V:4AA04`/`V:4AA06` | `0xFF` | read-only |
| a secondary object is created | `0x2AACE` (copy of a record into `0x4D23C`) | `0xFF` | read-only |
| a record is spawned/armed, deactivated | `0x2783E` (`0x8C`); `0x27DB4`, `0x2A9C6` (`0x0C`) | `0x8C` / `0x0C` | read-only |
| the player is re-placed, view selector change | `0x271B0`, `0x27468` | `0xFF` | read-only |
| mission start | `0x1D1E4` (stores `0xFF`); also stores `V:4A90D` = `0xFF` (`0x1D2C8`) | `0xFF` | read-only |
| external camera crosses a cell, sub-cell or quadrant | the external branch of `0x1F54E` (section 2) | `0xFF` | rig |
| camera and view-mode switches (cockpit/external, distance and direction keys, chase and fixed views, relaunch) | 23 sites: `0x11C66`, `0x11CAC`, `0x1229A`, `0x12398`, `0x12406`, `0x12430`, `0x124CC`, `0x12580`, `0x12D08`, `0x13094`, `0x142B4`, `0x1431A`, `0x143C4`, `0x1D538`, `0x1E4FE`, `0x1E57A`, `0x1E592`, `0x1E600`, `0x1E6CE`, `0x1E6F0`, `0x1E714`, `0x1E868`, `0x1EC14` | `0xFF` | classified below, each row tagged (the key and view-switch sites run in the rig and the emulator; the mission-state sites are read only) |

**The 23 camera and view request sites, classified** (round M3-T4 and the note-fix round; rig `view_check.py keys` runs the key handler `0x1DAA0` and checks the requests of the head keys, the zoom keys and the view words, 120 key presses, 0 differences; rig `view_probe.py` (sites, paused, map) runs the three routines named in the tags; emulator `views-m3-*.txt`, in particular `views-m3-fix.txt` and `views-m3-fix2.txt`). The tag of a row says what was run: V = run in the rig or the emulator with the request byte `V:4A910` read after the event, R = read from the code only (never run):

| site | what executes it | condition | tag |
| --- | --- | --- | --- |
| `0x1E714` | **every cockpit head key that changes the code**: KP8, KP9, KP2, KP4, KP6 (all through 0x1E6F8 and 0x1E706) and the head-hold recentre of `0x141E8` (the code 0 path) | always; KP5 does **not** request (it stores only the hold byte) | V (rig `view_check.py keys`, every code and key; emulator `views-m3-hold.txt` for the recentre) |
| `0x1E6CE`, `0x1E6F0` | KP7 (code 12) and KP1 (code 13), before the common tail | always | V (rig `keys`) |
| `0x1E4FE` | the external ring keys KP8, KP9, KP6, KP3, KP2, KP1, KP4, KP7, KP5, KP-dot (direction store); **KP3 and KP-dot also in the cockpit** (they store the direction 3 and 9 and request) | always (it also stores `V:4A968` = 0xFF when that is 0) | V (KP3 and KP-dot in the cockpit: emulator `views-m3-fix.txt` S1, `V:4A96A` 3 and 9, `V:4A910` = 0xFF, `V:4A968` = 0xFF, the head code and the hold byte unchanged; the external ring keys: the snapshots e-L4-KP5, e-L4-KPDOT and the rig `view_check.py ring`) |
| `0x1E592` | `]` and `[` in an external view that is not the map: the level change (-1, 1, 2, 3, 4) | always, also when the level did not change (the stop at -1 or 4) | V (rig `keys`; emulator `el0` to `el3`) |
| `0x1E57A`, `0x1E600` | `]` and `[` in the map: the altitude change | always, also at the stop | V (emulator `views-m3-fix2.txt` T2: `[` from 0x3000000 to 0x5000000 and 0x7000000, `V:4A910` = 0xFF in the pass of the key; at the clamps 0x8000000 and 0x1000000 the request is stored too; rig `view_probe.py map`) |
| `0x1E868` | the **cockpit zoom keys** `]` and `[` (and in an external view with `V:4A86D` clear) | only when the low seven bits of `V:4A895` are 0, which they are in play (0x80 from the mission start): always, even at the stops 0x20 and 0x80 | V (rig `keys`; emulator `views-m3-fix2.txt` T5: `[` at the stop 0x80 stores 0xFF) |
| `0x142B4` | the view switch of `0x141E8`: the first pass with the gate set after **KP-Enter** (either direction) or **M in the cockpit** or **M in the map**; **KP-Enter and M themselves store no request** (an M pressed in an external view that is not the map requests nothing at all: the later cell crossings of `0x1F54E` do) | once per request bit, so two presses in one pass give one request; deferred through a pause | V (rig `keys`; emulator `views-m3-pause.txt`, `views-m3-fix.txt` S3 for M in an external view: no request in 16 frames) |
| `0x1431A` | `0x141E8`, class 0x30 (the ejection seat) with the HUD step 0 | each such pass | V (rig `view_probe.py sites`: a class 0x30 viewed record, step 0: `V:4A910` = 0xFF, step 50, offset 800, window row 167); the seat is never viewed in a captured flight |
| `0x143C4` | `0x14392`: the viewed record is reset to the player when the viewed record's bit 6 (active) is clear | when it happens | V (rig `view_probe.py sites`: viewed index 4 with the active bit clear: index and offset reset to 0, `V:4A910` = 0xFF, the cockpit head code and step reset); never seen in a capture. 0x14392 is inside the gated part of the frame routine, so it does not run in a pause |
| `0x1EC14` | the viewed-entity switch routine, called by the world update (`0x2753E`, `0x27D00`, `0x286DE`: the ejection seat spawn, a viewed record that dies, see note 06) | when it runs | V (rig `view_probe.py sites`, D1 = 4: `V:4A910` = 0xFF, `V:4A994` = 4, `V:4A996` = 0x800, `V:4A968` = 0xFE, the zoom word reset to 0x80, `V:4A9E2` the record's heading; class 0x30: HUD step 50, row 167); R for the callers (never run in a capture) |
| `0x1D538` | the crash restart (note 05 17.17) | | R |
| `0x11C66`, `0x11CAC`, `0x1229A`, `0x12398`, `0x12406`, `0x12430`, `0x124CC`, `0x12580`, `0x12D08`, `0x13094` | mission start and state routines: they clear the HUD offsets and the view (0x1EFEC and `V:4AA3E`) and request; `0x12398` also stores the camera mode `V:4A966` = 4 and `0x12580` sets the map flag `V:4A865` | | R |

So the port's three requests (KP-Enter, the external keypad keys and M) are **not** the original's set: the original requests on the head keys (all but KP5), on KP3 and KP-dot in the cockpit, on the zoom keys (conditional on `V:4A895`, in play always), on the level and altitude keys, and at the view switch of `0x141E8` (KP-Enter and M reach it only through the request bit `V:4A97E` bit 1 and the gate). A request is idempotent (a store of 0xFF); the rebuild still happens at most once per frame.

The cockpit branch of `0x1F54E` does **not** compare cells: it copies them from the viewed record every frame; a cell change
reaches the builder through the position update of the record. The rebuild happens at most once per frame. In the frame loop the
order is: detector (`0x1F54E`, entity update `0x26AF8`), camera (`0x1F45E`), octant (`0x299AC`), pitch gate (`0x143F2`), builder.
Consequences: a **change of the octant is noticed by the detector one frame after it is written**, so the rebuild for a turn
through an octant boundary happens a frame late (the builder of the frame in which the octant changed already uses the new octant
but the stale request), whereas a pitch-gate toggle raises its mask in the same frame as the change. In steady cockpit flight
rebuilds happen at: octant changes, the 200 ft and 10,240 ft altitude crossings, gate toggles, and every cell crossing of any
active record (a C cell is 2^22 pu = 16,384 u = 20,480 ft, about 30 s at 400 kt).

Initial values. Mission start stores `V:4A90D` = `0xFF` and requests `0xFF`; nothing initialises `V:4AB26`, `V:4AB16` or
`V:4A83E`, which keep their values across missions (the start's own `0xFF` request, and the per-frame overwriting, make that
harmless). In the carrier deck snapshot `V:4A90D` = 0, `V:4AB16` = 0, `V:4AB26` = 31,752 (the height), `V:4A83E` = 0; in the
airborne snapshot `V:4A90D` = 0, `V:4AB16` = 0, `V:4AB26` = 0x8A4E5 (the height), `V:4A83E` = 0.

### 2. The scan inputs

Every frame, before the builder, `0x1F54E` refreshes the *scan inputs* from the viewed record (the player: the record at byte
offset `V:4A996` in the table at `0x4B23C`; in an external view from the camera position instead):

| word | cockpit (viewed record) | external view (camera position longs `V:4ACF6` x, `V:4ACFE` z, computed by `0x2D756`) |
| --- | --- | --- |
| `V:4AA04` / `V:4AA06` (player's C cell, x and z, 0..127) | record `+0x06` / `+0x08` | bits 22 to 28 of the camera x / z long |
| `V:4A909` (sub-cell, 4 by 4 inside the C cell) | record byte `+0x0A` | 4 * (3 - bits 20-21 of z) + (3 - bits 20-21 of x) |
| `V:4A908` (quadrant of the C cell inside its A cell, 0..15) | 4 * (3 - (z cell and 3)) + (3 - (x cell and 3)) | the same from bits 22-23 of the camera longs |
| `V:4AA00` / `V:4AA02` (A/B cell, set by the builder at `0x1F7C0`-`0x1F808`) | the cell words `+0x06` / `+0x08` of the **viewed record** (the record at `V:4A996`; `V:4A994` is its index, `V:4A996` index * 0x200) shifted right by 2; in play equal to `V:4AA04/06` >> 2 because `0x1F54E` copies those from the same record | bits 24 to 28 of the camera longs `V:4ACF6`/`V:4ACFE` (the scan cell) whatever `V:4A996` says |
| `V:4A90C` (view octant 0..7) | the camera heading word (28800 per circle; cockpit `V:4AB46`, external `V:4AB4E`) integer-divided by 3600 | |
| `V:4A974` (re-test period of invisible entries) | 5, 3 or 1 from the angular rates of the player record (record 0 in an external view): 5 when the largest rate `+0x56/+0x58/+0x5A` is at most 0x60 and `+0x6C` at most 0x1000, 3 when at most 0xC0 (or `+0x6C` larger), else 1 (read-only) | |

**The viewed cell in an external view is the camera's, not the aircraft's (V: `viewcell_check.py`, 400 random states, 225 external and 175 cockpit, 0 differences).** The original's `0x1F54E` writes `V:4AA04`/`V:4AA06` from the record `+0x06`/`+0x08` only when `V:4A83D` is zero; otherwise from the camera longs `V:4ACF6` (x) and `V:4ACFE` (z): each masked with 0x1FFFFFFF and shifted right by 22 (bits 22 to 28, 0 to 127), the sub-cell byte `V:4A909` from bits 20 and 21 and the quadrant `V:4A908` from bits 22 and 23 as in the table. Random longs, negative and above the world included, followed the rule in every external state; in the cockpit branch the words, `V:4A909` and the quadrant come from the viewed record. The camera is not at the aircraft in an external view (the ring camera 0x2D756 and the map and tower set-ups plant it up to several cells away), and in 225 of 225 external states the camera's cell differed from the poked record's. **Port: in an external view the core must set the viewed cell (and the sub-cell and quadrant words the builder and the near-view test read) from the camera position as above, not from the aircraft; today `flight.c` (the `v4aa04`/`v4aa06` assignment) always takes the aircraft's cell.** Observable effect: which cells' entries the builder keeps live near the camera (the sub-cell test of the table's third row and the 2-cell near-view test), wrong in external views whenever camera and aircraft are in different 2^22-unit cells.

(Rig, external branch with the entity update and `0x2D756` stubbed: camera cell (70,65) with sub-cell offsets (2,1) gives
`V:4A909` = 9 and `V:4A908` = 9; cell (127,3) with offsets (0,3) gives 3 and 0. Bits above bit 28 are ignored.) The external
branch compares the new C cell, sub-cell and quadrant with the stored ones. **A change of the C cell x or z always requests
`0xFF`. A change of `V:4A909` or `V:4A908` requests `0xFF` unless the camera mode `V:4A966` is 2 and `V:4AB30` (signed) is
at or below -`0x1000` u (camera higher than 4096 u = 5,120 ft); in that case the new value is stored without a request.** The
cockpit branch has no such comparison.

The camera offsets used by the builder and by every distance computation are the words `V:4AB2A` (x), `V:4AB2E` (z) and the long
`V:4AB30` (height), all **in u**. They are the longs `V:4AB1A`/`V:4AB1E`/`V:4AB22` (position units, written by `0x1F45E`) divided by
256. The longs are **minus the camera position relative to the corner of the viewer's C cell** (x and z masked to 22 bits; the
height is absolute, so `V:4AB1E` is negative above ground and the camera height is -`V:4AB1E`). In a cockpit view the camera is
the viewed record's position plus a small eye offset (fixed per aircraft class) rotated by the record's attitude (`0x1F45E`;
camera rules in `06`); verified on the carrier deck, where the offsets equal minus the player position plus the eye offset
(x -6188 u, z -6580 u). In external views the offsets come from `0x1F1D8` (not decoded; the builder only uses the words).

### 3. The scan: tiers, cells, tile patterns

Three cell tables (`02` section 10): A (`0x47374`, cells of 2^24 pu, 32 by 32), B (`0x47474`, same grid, a second coarse layer)
and C (`0x47574`, cells of 2^22 pu, 128 by 128). Each is a table of 128 row offsets; a row (selected by the **z** cell) lists
increasing **x** cell keys with one item-list pointer each. A cell outside a table row is empty.

**Coordinate wrap** (verified in the rig by patching table rows). The scan origin plus the tile offset gives a key (x) and a row
(z). A value below 0 becomes the **limit** and a value above the limit becomes 0, whatever the excess, where the limit is **30**
for A and B and **127** for C: -1 and -2 both give the limit, limit+1 and limit+2 both give 0. For A and B the grid has 32 cells but
cell 31 is never read, neither as a neighbour nor as an origin (an origin of 31, which the C cells 124 to 127 produce, is remapped to
0 even for the offset 0); no A or B cell exists beyond 18 so it has no effect on the data. The **tile label** that goes into the
queue key (section 4) is not wrapped the same way: it is the origin plus the offset in C cell numbers, with 128 added when the sum
is negative, and then only the low 8 bits of each number are kept (x by shifting left by 8 within 16 bits, z by masking to its low byte): a sum of -1 labels
byte `0x7F`, -2 labels `0x7E`, and nothing is done above 127 (z 127 with dz +1 labels z byte 0x80 although row 0 is read). The code
(`0x20B76`-`0x20B96`) and the Python model (`model.py`, which adds 128 and then combines the x number shifted left by 8 within 16 bits with the low byte of the z number) agree;
negative sums need an origin cell below 2 in C numbers, which the shipped data never reach, so no shipped label is affected
(read-only, C; the model is a separate implementation of the same rule, so the agreement checks the transcription, not an independent measurement).

Per rebuild and tier, in order A, B, C, the builder does two passes.

**Pass 1: choose the tiles.** The scan origin is the player's cell of that tier (A and B: `V:4AA00`/`V:4AA02`; C:
`V:4AA04`/`V:4AA06`). A **tile pattern** is a list of at most 14 entries; each entry names a tile and the sub-cells of that
tile that are to be drawn, in drawing order. It is selected by the pattern rule below, and its encoding is given in the next
paragraph. For each tile the cell is fetched (row search by binary search, occupancy map first) and its items are copied into the
**slot buffer**: group g (the tile's position in the pattern, 0..13) times 16 slots, one slot per sub-cell number of the item
(the low nibble, 0..15). A slot holds at most 16 items of 6 bytes (flag byte, id, ox, oz); more than 16 consecutive items with the
same nibble call the internal-error trap. Entities and secondary objects are appended to a slot of tier C (section 6).

**Pass 2: emit queue entries.** For each tile, in pattern order, for each of the n sub-cell numbers of that tile, in the listed order
(mapped through the sub-cell permutation to the slot number s), every item of slot s is emitted in the order in which it appears
in the cell's item list, as one 24-byte queue entry (section 4). So the queue order is: tiles far to near; within a tile,
sub-cells in the pattern order; within a sub-cell, item-list order, then entities, then secondary objects. Tier C is then reordered
(section 5); tiers A and B keep this order.

#### The pattern blobs: encoding and selection

There are three blobs in hunk 65; the items below are extractor outputs, to be read from the image, not copied here.

| blob | address, size | offset table | lists | sample (for the extractor test) |
| --- | --- | --- | --- | --- |
| high altitude | `0x4659C`, 3042 bytes | 32 words, first offset `0x40` | 32 distinct, all reachable; 20 with 6 tiles, 10 with 5, 2 with 9 | list 0 (offset `0x40`): tile 0 with 13 sub-cells, tile 2 with 9, tile 1 with all 16, tile 5 with 3, tile 3 with 1, tile 4 with 4 |
| steep view (pitch gate) | `0x4633A`, 610 bytes | 4 words, first offset 8 | 4 stored (13, 14, 14, 13 tiles), only lists 0 and 1 reachable | list 0 has 13 tiles |
| low altitude (tier A, camera at most 200 ft) | `0x4717E`, 502 bytes (ends where table A starts) | 64 words, first offset `0x80` | 32 distinct offsets referenced (8 per group of 16 words), lists of 1 to 4 tiles (36 index words with 1 tile, 24 with 2, 3 with 3, 1 with 4); the 132 bytes from offset 370 hold 16 further lists that no index reaches | list at offset `0x80`: four tiles with one sub-cell each |

**Format.** A blob starts with a table of 16-bit big-endian words; each word is a **byte offset from the start of the blob** to
one list (no scaling; the table has first-word/2 entries; an offset of 0 or less calls the trap, code `0x42`). A list is a
sequence of entries ended by a tile byte with bit 7 set (always `0xFF`; there is no count field, both passes parse the same
bytes). An entry is: **tile byte** (0 to 20; a larger value calls trap `0x0C`; the data use 0 to 16), **count byte n** (1 to 16;
never 0 in the data), then **n sub-cell bytes** (each 0 to 15; larger calls trap `0x12`). A list holds at most 14 entries (a 15th
calls trap `0x0D`).

**From list to cells.** Let p be the *phase*: V:4A908 for tiers A and B, and the sub-cell V:4A909 for tier C (tier C indexes the
tables below by the sub-cell). Let k = the quadrant class `0x4625C[p]` (0..3) and j = the pattern group `0x4628C[p]` (0..3).

* A **tile byte** t is mapped through row k of the tile permutation (`0x462DC`, 4 rows of 24 bytes): t' = row[k][t], an index 0..20
  into the 21 tiles around the origin (row 0 is the identity; only the first 21 bytes of a row are used, bytes 21 to 23 are
  padding that no tile byte can reach). t' selects the offset pair (dx, dz) of `0x20674`: 0:(1,1) 1:(0,1) 2:(-1,1) 3:(1,0) 4:(0,0)
  5:(-1,0) 6:(1,-1) 7:(0,-1) 8:(-1,-1) 9:(1,2) 10:(0,2) 11:(-1,2) 12:(2,1) 13:(-2,1) 14:(2,0) 15:(-2,0) 16:(2,-1) 17:(-2,-1)
  18:(1,-2) 19:(0,-2) 20:(-1,-2) (dx on the x cell, dz on the z cell): the 3 by 3 block and the 12 cells of the 5 by 5 block without
  its corners.
* A **sub-cell byte** is mapped through row k of the sub-cell permutation (`0x4629C`, 4 rows of 16 bytes) to the slot number s.
* The **list** is chosen by the table index below.

| case | blob | word index into the offset table |
| --- | --- | --- |
| tier A, camera height at most 200 ft (-`V:4AB1E` below `0xA000`); tiers B and C never use it | low altitude | 16 * j + `0x4629C[16 * k + V:4A909]` (here p = V:4A908) |
| otherwise, pitch gate `V:4A83E` set | steep view | `0x4628C[j]` (a value 0 or 1; applied to j, not to p) |
| otherwise | high altitude | 8 * j + `0x4626C[8 * k + V:4A90C]` (the permuted octant, `0x4626C`: 4 rows of 8 bytes) |

The "128 combinations" of the earlier draft are the 16 phases times 8 octants of the high-altitude case; they select among the
**32** lists of the 32-word table (4 groups j times 8 permuted octants), which is the full index space. Over the 128 inputs the
lists have 6 tiles for 80, 5 for 40 and 9 for 8.

**Worked lookup** (airborne snapshot, tier A, camera 2,750 ft, gate clear): inputs V:4A908 = 6, V:4A909 = 3, V:4A90C = 0, A origin
cell (x 16, z 16). Then k = `0x4625C[6]` = 1 and j = `0x4628C[6]` = 3; row 1 of `0x4626C` is 7,6,5,4,3,2,1,0, so octant 0
maps to 7; word index 8 * 3 + 7 = 31; the offset word is 2981, so the list is at `0x4659C` + 2981 = `0x47141`. The list has five
entries (tile, sub-cells), in the tile order 2, 0, 1, 3, 4, the third of them with all sixteen sub-cells and the others with 6, 13, 8 and 7 (the sub-cell numbers are disk data and are not listed: FNV-1a-32 `158f3507` of the 60 bytes of the five entries, tile byte, count byte and sub-cell bytes, from offset 2981 of `tables/lists_high.bin`, the terminator excluded). Row 1 of the tile permutation starts 2,1,0,5,4,3,8,7,6, so the tiles 2, 0, 1, 3, 4 become 0, 2, 1, 5, 4, with
offsets (1,1), (-1,1), (0,1), (-1,0), (0,0), that is the A cells (17,17), (15,17), (16,17), (15,16), (16,16): the far row first,
the player's own cell last. Row 1 of the sub-cell permutation is i XOR 3 for the sixteen sub-cells; the first tile (17,17)
has no entry in table A and emits nothing; the second tile (15,17) visits the slots that the list's sub-cell numbers become through the permutation row (in list order) and the first
items found are id 21 (slot 5), id 70 (slot 4) and id 62 (slot 13), which are the first three queue entries of the example in
section 10 (key `0x00154043`: 16 * 1 + 5 in the high word, the C numbers (64, 67) in the low word).

For tier C the quadrant class and group are indexed by the sub-cell, but the phase terms of pass 2 still use `V:4A908`. `V:4A90E`
is set while tier A uses the low-altitude pattern. Tier B never uses the low-altitude pattern (only tier A switches at 200 ft).

**Position of an item** (u, before the LOD shift): with the tile offset (dx, dz), the slot s of the item, the item's offsets
(ox, oz) as stored and the phase p = `V:4A908`. The sub-cell named by the nibble is x index 3 - (s mod 4), z index 3 - (s div 4),
counted from the low corner of the cell (the numbering of the entity sub-cell byte `+0x0A`); the offsets are relative to the corner
of that sub-cell:

| tier | X | Z |
| --- | --- | --- |
| A, B | TA(dx) + 16384 * (3 - s mod 4) + 4 * ox - 16384 * (3 - p mod 4) | TA(dz) + 16384 * (3 - s div 4) + 4 * oz - 16384 * (3 - p div 4) |
| C | 16384 * dx + 4096 * (3 - s mod 4) + ox | 16384 * dz + 4096 * (3 - s div 4) + oz |

(TA(d) is the tile term of tiers A and B: 65536 * d for d = -2, -1, 0, 1 and **-131,072 for d = +2** (the 16-bit table word wraps; the rig-found correction below). A and B cells are 65536 u wide, the sub-cell 16384 u, the item offset unit 4 u; C cells are 16384 u, the sub-cell 4096 u, the
offset unit 1 u; the offset is the sub-cell size divided by 4096 in both. The phase term of tiers A and B makes the player's
*C cell* the origin; for tier C the phase terms of pass 2 cancel, so the origin is the corner of the player's C cell.) Items with
bit 7 of the id byte set (default-offset table `0x207C6`) occur in no table; the builder supports them, the port may reject them.
The y word of a terrain entry is 0. The item id byte of the cell data is the **descriptor index**, the object id of `02` minus 16.

**Tile origin in tiers A and B (rig-found correction).** The tile term is not 65536 * dx: it is the 16-bit signed word of the origin table `0x2069E` (16384 * dx in 4 u units), sign-extended and multiplied by 4. The words for dx = -2, -1, 0, 1 are -32768, -16384, 0, 16384 (exact), but for dx = +2 the word is -32768 as well, so a tile at +2 on an axis reads **-131,072 u instead of +131,072 u (262,144 u lower)**; the same for dz. Tiles with dx = +2 are the pattern tiles 12, 14 and 16, with dz = +2 the tiles 9, 10 and 11 (the low-altitude blob uses tiles 0 to 4 only; the high and steep blobs use all 21). The cell fetch uses the correct offsets of `0x20674`, so only the position word is affected. Verified: rig, a +2 tile entry of item 116 in tier B has Z word -5376 at LOD 5 (Z = -172,032 u); the Python model reads the same table and agrees.

**LOD shift** (0..15), stored in the low nibble of the entry's first word and applied to the position words. With xc = |X +
`V:4AB2A`| / 4096 and zc = |Z + `V:4AB2E`| / 4096 (integer; X, Z the unshifted positions in u of the table above, the camera
words in u) and yc = -`V:4AB30` / 2048, let m = the largest of the three (as signed words); the shift is the LOD table `0x20E4A`
at index min(m / 2, 239). The table is an extractor output (`03-3d-pipeline.md`, as there): 240 bytes, non-decreasing from 0 to 15, 16 distinct values, each a
run of increasing length; samples: index 0 = 0, index 1 = 1, index 12 = 4, index 239 = 15. The stored position words are the position (X, Z in u) **shifted right arithmetically by the LOD shift first, then truncated to 16 bits** (as in section 4; for example Z = 66432 at shift 3 stores 8304, whereas truncating first would give 112). Because the shift is derived from the camera offsets **at the
time of the rebuild**, an entry keeps the LOD it had when the queue was built (the walker's own refresh changes the distance word,
section 8, not the nibble). For an entity entry, section 6 gives the inputs. The rule is verified against the rig for terrain
items over 900 random states (see Verified by), and the LOD of the deck and airborne examples agree with the emulator (two airborne
entries differ by exactly one LOD step because the snapshot was taken after the rebuild, see Verified by).

**Count limit and walked window.** After every emitted entry a counter (`0x53A28`, never clamped) is incremented. A queue holds 70
slots. Once 70 entries have been generated the write pointer stays at the 70th slot, further entries overwrite it, and the
terminator written at the end overwrites it as well: **at most 69 entries are kept per queue, always the first 69 generated.**
The counter is compared with a limit by camera-height band, D = integer part of (-`V:4AB30` / 2048) (units of 2,560 ft):

The limit is a function of D in four bands of 2,560 ft of camera height (band k covers heights below 2,560 ft * (k + 1)), with a pair of limits per band: one with `V:4A83E` zero and a larger or equal one with it set. Within a column the limits do not fall with D (the ungated column does not rise between D = 0 and 1) and the gated column starts higher; they stay at or below 70; for D of 4 or more (from 10,240 ft) both are 70.

The four pairs are the 8 words at `0x21022` (index 4 * D, plus 2 with the gate); for D of 4 or more the limit is an
immediate (70) in the code at `0x20FE8`, not a table row (rig: heights D = 0 to 6, with and without the gate, give exactly the table's limits and 70; the eight words are disk data and are not listed: `tables/lists_limit.bin`, FNV-1a-32 `88ff09cd`). A positive `V:4AB30` (camera below sea level) would give a negative D and read outside the table; this is unhandled and
cannot occur in flight. For every entry generated while the counter is above the limit, a per-tier byte offset (`V:4AA64` for A,
`V:4AA66` for B, `V:4AA68` for C) grows by 24 and is never clamped; it is cleared when its tier is rebuilt. The walker starts at that
offset, so with G entries generated: **the entries from index G - limit up to the last kept entry are walked** (for G at most 69
that is the last `limit` entries, the nearest; for G of 70 or more the kept entries are indices 0..68 and the walked count is
69 - (G - limit); nothing is walked if G - limit is 69 or more, which cannot occur). The first G - limit entries are generated
and then ignored. This is the real distance cull of the original.

**Finite check of the capacity** (the model of Verified by over every origin cell, every phase, octant, gate setting and, for tier A,
every low-altitude sub-cell; 16 items per slot at most; terrain only): the maximum generated count per tier is

| tier | high altitude, gate clear | gate set | low altitude |
| --- | --- | --- | --- |
| A | 64 (phase 3, octant 5, origin x 15, z 16) | **71** (phase 10 or 15, origin x 15, z 17) | 16 |
| B | 29 | 33 | never used |
| C | 25 | 27 | never used |

Only tier A with the pitch gate set exceeds 69: six (phase, origin) combinations give 70 or 71 (phases 2 and 7 at origin (15,16),
phases 11 and 14 at origin (15,17): 70; phases 10 and 15 at origin (15,17): 71). So the capacity cap is real but reached only in
the airfield cells, nose-down or inverted. Rig confirmation: C cell (61,69), sub-cell 5, gate set, height D = 4: counter 71, 69 kept,
offset 24 (limit 70, walk indices 1 to 68); the same with D = 0: offset 744 = 24 * (71 - 40), walk indices 31 to 68. Entities and
secondary objects add at most one entry each (32 in all) and only in tier C (section 6), where 27 + 32 = 59 stays below 69.

### 4. The queue entry (24 bytes)

| offset | size | content |
| --- | --- | --- |
| +0x00 | word | high byte: item id (0..127; for an entity the record index 0..15, for a secondary object its index); low byte: bit 7 = the item's default-offset flag (always 0 in the data), bit 6 = secondary object, bit 4 = entity, bits 0-3 = LOD shift |
| +0x02 | long | descriptor address = `0x2600C` + 20 * id (for entities the id is the record index 0..15: descriptors 0..15 are the run-time slots). During the tier C reorder (section 5) the high word is used as a scratch mark and cleared again |
| +0x06 | word | X, as in section 3, low word of (X shifted by the LOD). For an entity: the tile offset plus the sub-cell corner only; the exact position comes from the record when it is drawn (section 6) |
| +0x08 | word | Y, 0 for terrain items |
| +0x0A | word | Z, as X |
| +0x0C | long | terrain: high word = the slot number ORed with 16 * i for every i from 1 to n, n the 1-based number of the item in its slot step (an accumulator that is not cleared between the items of one slot step: items 1 to 5 get 0x10, 0x30, 0x30, 0x70, 0x70 plus the slot; verified against the emulator), low word = 256 * x + z of the tile in **C cell** numbers (a negative number gets 128 added; also for tiers A and B, which use the same offsets; the tile's own numbers, not wrapped above 127); entity/secondary: bit 31 set, bits 24-30 = the index. The reader is the landmark slot copy of section 7 (it stores the long as the first field of a slot); the walkers and the reorder read the low word (the tile label) |
| +0x10 | word | distance word: 0 when built; the walker's refresh stores the approximate distance to the camera here (section 8); the sort of section 5 prefers it (shifted left by the LOD) when non-zero; also the model drawers' size scale `V:4ABF8` |
| +0x12 | byte | countdown for re-testing an entry that drew nothing: initial value (4 - G mod 4) mod 4 with G the generation index of the entry within its tier's pass 2 (0, 3, 2, 1, 0, 3, ...; G counts every entry generated in the tier, including the first (generated - limit) entries the walk ignores, entries past the 69 kept, and entity and secondary entries; it restarts at each pass 2, i.e. per tier and build, not per build); rig, 150 frames, 845 entries: equal to the queue index pattern except 3 tier C entries moved by the sort or reorder |
| +0x13 | byte | draw counter, **initially 0** (the builder clears the entry), decremented at every walk with wrap-around, published as `V:4A975` and used by the drawer's visibility pre-test (section 8). The emulator shows 247 and 248 for entries walked 9 and 8 times, as expected |
| +0x14 | word | result of the last draw: positive if the drawer reported something visible, else -1; 0 when built or never walked |
| +0x16 | word | 0 |

The queue is ended by a word of -1 right after the last kept entry. The descriptor (20 bytes, `0x2600C`) is five longs: the draw
routine, the model base, the detail-list start, the cockpit-alternate detail list and an auxiliary pointer (`02` section 1). The
item id of the queue is the descriptor index; the object id of `02` is that plus 16, and the handler table of `02` applies:

| routine | `02` object ids | what the walker does with it |
| --- | --- | --- |
| `0x21C40` | 43-45, 47, 50-57, 59-66, 68-71, 74-85, 89-94, 96, 97, 102, 103, 107, 108, 133 (49 descriptors) | plain model entry; **skipped by the walker when the camera is higher** than 17,920 ft in queues A and B (`V:4AB1E` below -`0x380000`), 20,480 ft in queue C (below -`0x400000`) |
| `0x21C34` | 67, 87 | like `0x21C40`, but **drawn only when the camera is higher** than 12,800 ft (`V:4AB1E` below -`0x280000`); at or below that height the routine returns 0 at once (stored -1; rig: 48 of 4,407 walked entries, section 8b); **in queues A and B the walker gives these entries neither the distance refresh nor the invisible-entry countdown** (steps 4 and 5; its distance word stays as built, 0), **in queue C they are refreshed and counted down like any terrain entry** (the queue C walker has no test of the routine; rig, section 8b) |
| `0x21D0C` | 33-35, 46, 48, 49, 58, 72, 73, 86, 88, 95, 104 | the drawing routine itself (13 descriptors) |
| `0x22CFC`, `0x22D0A` | 36, 38, 98; 37, 39-42, 99-101, 105, 106, 109-132 | type B ground patches with their own cut-offs on `V:4AB1E` (-`0x380000`, -`0x800000`) inside the routine; no walker height test |
| `0x21C44`, `0x26944`, `0x2699E`, `0x26A3E` | templates 0-15 | the run-time descriptors 0-16 are copies of these templates made by the spawner (aircraft, carrier, submarine, cone objects), see section 6; what the handlers do besides drawing (the crashed bit to the dying count, the lock drop, the cone phases): note 07 section 11.5 |

A descriptor whose first word is negative is an unused run-time slot and is skipped. (An earlier draft called `0x21C40` the "flat
ground patterns" and `0x22CFC`/`0x22D0A` "general model drawers"; both were wrong. The run-time descriptors are
`0x2600C`..`0x2615F`, 17 of 20 bytes, descriptor indices 0-16, which `02` calls object ids 16-32; slots 0-15 hold the spawner's
copies, slot 16 is zero in both snapshots.)

### 5. Per-frame order refinement of tier C

Tier C's order after the build is refined in two ways, and **both run every frame** (the builder is called every frame; with a
zero request it only does these and the landmark slot consumer). Tiers A and B are never reordered.

**5a. The run sort.** While tier C is built, every slot (every tile and sub-cell step of pass 2) that holds two or more items
(terrain items, entities and secondary objects together) is recorded as a *run*: the address of its first entry and its entry
count, six bytes per record at `0x4AA86`; the counter `V:4A914` is the number of runs. The sort `0x2122C` sorts one run at a
time. When a rebuild was requested (non-zero request) it sorts **all runs in the same call**, last run first; when the request
is zero it sorts **one run per frame**. The cursor is `V:4A915`. The builder sets it to the run count N (`V:4A914`) at the end
of pass 2, and the all-runs sort of a rebuild walks it down to 0 and reloads it, so **after a rebuild `V:4A915` = N**, and the first
zero-request frame sorts run index N - 1, the last run created (the run sorted in a frame is the one numbered cursor - 1, counted
from the first run created as 0; read-only, C). After the sort the cursor is decremented: frame f (1 to N) after a rebuild sorts
run N - f and leaves the cursor at N - f; the next frame finds the cursor at 0, sorts nothing, and reloads N. So the cycle is
N + 1 frames with N sorts, last-created run first, one idle frame per cycle (rig, V: with N = 5 the cursor after the rebuild and
after the following zero-request calls reads 5; 4, 3, 2, 1, 0; 5, 4, 3; the run index and the idle frame follow from the code at
`0x2123A`-`0x2139A`, read-only). That the builder is called once per frame, so that this is a *one run per frame* cadence in play,
is read-only (C, the frame loop of section 8); the rig calls the builder directly. It does nothing when no pass 2 has run yet (`V:4A85D` is set at the end of every pass 2 and tested at the sort's entry; the builder calls the sort after the tier passes of the same call, so the **first build is sorted like any other**: rig, with `V:4A85D` forced to 0 the deck build gives [14, 0] and 60 random frames give 0 differences; the earlier reading "the first build is not sorted" was a rig artefact, only a zero-request call before any build sorts nothing). A run longer than 22 entries sorts its first 22 only (a slot of terrain items has at most 16 entries). The key of an
entry is a 16-bit word: a secondary object has 0x7FFF; otherwise, if the entry's stored distance word (`+0x10`) is non-zero it is
that word shifted left by the entry's LOD; otherwise it is the distance function `fx_distkey` of the entry position
(`03-3d-pipeline.md`; the stored words, camera offsets shifted by the LOD) shifted left by the LOD. For an entity the position is
the slot word plus the record's fine offsets (record words `+0x0C`, `+0x0E` masked to 12 bits, shifted right by the LOD). The code also adds a *key height term*, ((record word `+0x10` + `V:4AB30`) shifted right by the LOD, then 8 and 4), but it is **always zero in reachable states** (at LOD 0 it needs a relative height of 4096 u, and the LOD table gives a shift above the log2 of the height index), so the sort key has no height term; `+0x10` of the record is y >> 8, not a height long. Rig, deck plus 2 to 5 extra records in the carrier's sub-cell, 300 states, 1651 keys: horizontal-only key 1651 equal, the code's term 1651 equal, the full `+0x10` as a term 441. A walked entity entry has a non-zero `+0x10`, so after its first walk its key is the stored distance word shifted left by the LOD, which includes the refresh height term of section 8 (step 4, the `V:4ABF4` form). The entries are ordered by **decreasing key** (farthest first); equal keys keep their
earlier order. (Keys cannot become negative in the data: the largest distance in tier C is a few thousand units.) Because the
stored distance word is refreshed by the walker and the camera moves, the order inside the runs of tier C can change between
rebuilds. Example: at the carrier deck the builder generates the entities [player 0, carrier 14]; the sort turns them into
[14, 0] (the carrier is farther), in the rig and in the emulator.

The distance function `0x20884` (verified bit-exact in the rig over 3000 random inputs): with the three axis distances a, b (word)
and c, each already divided by 16 after the camera offset: take the larger of a, b as the first and the smaller as the second;
the first estimate is first * T(256 * second / first) / 16384, where T is the hypotenuse table `0x208E8`; repeat the step with c
and the first estimate; the result is capped at 0x7FFF. T is an extractor output: 257 words in Q14 with T(r) = round(16384 *
sqrt(1 + (r/256)^2)) for r = 0 to 256, exact for all 257 entries (T(0) = 16384, T(128) = 18318, T(256) = 23170).

**5b. The landmark reorder** (`0x21444`, called at the end of every builder call). It refines the painter's order of tier C
around the nearest landmark that has collision data, using the landmark's model planes. Rules (rig-verified against a model, 900
random states plus 250 states placed next to the landmarks, no differences):

1. The *reference* is the **last** entry of queue C (the nearest) whose descriptor has a positive model pointer (the long at
   descriptor `+4`, the *model base* M, signed positive), whose model's first leaf is not `0xFFFF` and whose model flag byte has
   bit 4 set. Offsets: let w0 be the word at M. The first leaf is w0 if bit 15 of w0 is set, else the word at M + 2 if bit 14 of
   w0 is set, else the word at M + 4. The flag byte is the byte at M + 7 + (leaf AND `0xFFF`) and bit 4 of it is tested; in the shipped data these are item ids 42, 70 and 88 (the
   landmarks with collision data; item 70, object 86, is a pyramid-roof building and not a flat model, with four collision planes, note 02 section 9). If queue C has at most one entry or none qualifies, nothing happens. `V:54DFA` holds the
   reference index.
2. All positions are expressed in the reference's C cell, in u. A terrain entry's position is its stored word shifted left by its
   LOD, minus 16384 * (its cell minus the player's cell) plus 16384 * (its cell minus the reference's cell), y = 0; the cell is the
   high and low byte of the tile label (`+0x0E`). An entity or secondary entry uses its record's position fields. The camera
   is -`V:4AB2A` and -`V:4AB2E` (x, z) plus the same cell correction, and -`V:4AB30` (height).
3. *Window.* Walking backwards from the reference, the window starts at the first entry such that every entry from it to the
   reference lies within 0xC00 u (3072 u) of the reference on all three axes (the bound is **inclusive**: a difference of exactly 0xC00 passes; the walk stops at the first entry that fails;
   `V:54DFC` holds the start index). Entries **after** the reference are tested only while they stay within 0xA00 u on all three
   axes (inclusive as well); the first one beyond ends the scan.
4. *Test* for each entry of the window against the reference, using the reference's collision data (descriptor `+0x10`: a count
   word and a long pointer at `+6` to 12-byte plane records, each a point of three words and a normal of three words; planes are
   tried in order, **the count word plus one planes** (a count word c names c + 1 plane records, and the model runs through all of them); the plane y is ignored, 16-bit words and 32-bit products): form the camera side = the plane normal dotted with
   (camera minus plane point) and the entry side = the normal dotted with (entry position minus plane point), where the plane point
   is offset by the reference's local x and z (**used at LOD shift 0, not at the reference's own LOD**, a quirk that the rig
   confirms). The local x (z alike) at LOD shift 0 is the reference's raw stored position word (entry `+0x06`, `+0x0A`),
   **unshifted**, minus 16384 * (the reference's cell minus the player's cell) on that axis, kept as a signed 16-bit word. For the first plane whose camera side and entry side are both non-negative, the entry is marked *front* (0x8000) and the
   tests stop; if no plane decides, the entry is marked *behind* (0xC000).
5. *Reorder.* If more than one entry was processed, the processed entries (window start through the last tested one) become: the
   *behind* entries in their old order, then the reference, then the *front* entries in their old order; the marks are cleared.
   The change is made in the queue itself, so it persists and later sorts see it.

Collision data with a negative first word would select a second plane format; no reference model in the data uses it (read-only).
Entity and secondary entries, as tested entries and as window members, take the same test through other entry routines
(`0x21AC4`, `0x2166A`, with a shortcut when bit 6 or 7 of record byte `+4` is set), decoded from the disassembly only.

The routines `0x230B8`/`0x230D8`, called on alternate frames after the reorder, only look up the player's A/B cell in two small
tables and set the flags `V:4A953`/`V:4A954`; they do not touch the queues.

### 6. Entities, secondary objects and the carrier

The 16 records at `0x4B23C` (stride 0x200) are the *entities* (player = the record at `V:4A996`, the carrier = record 14, class
0x20). A record is **active** when bit 6 of its byte `+0x01` is set. The other flag bits of that byte: bit 4 = *pending*, bit 2 =
*not in any queue*.

* **Pending.** Every accepted rebuild request, whichever tiers it selects, first sets bit 4 on all 16 records and all 16 *secondary
  objects* at `0x4D23C` (stride 0x20; the same bytes `+0x01`, `+0x06`, `+0x08`, `+0x0A`; the model id is the word at `+0x02`;
  missiles and similar short-lived objects, role in `06`).
* **Insertion happens in tier C only.** In pass 1 of tier C, when a slot is completed (its terrain items end because the next item
  has another nibble or the list ends) every pending active record whose cell words (`+0x06` x, `+0x08` z) equal the cell being
  scanned and whose sub-cell byte (`+0x0A`) equals the slot is appended to that slot as a two-byte item (flag 0x10 for an
  entity, 0x40 for a secondary object, then the index) and its pending bit is cleared at once. For tiers A and B the insertion
  routines return immediately (rig: over 200 random states with the player and the carrier in cells 5, 20, 28, 31 and 70, 318
  entity entries appeared in tier C and none in A or B). A full slot silently drops the entity.
* **Cells absent from table C still insert.** When the cell has no table entry (an empty row, a clear occupancy bit or the end of
  the cell search) the insertion is made all the same. The carrier deck is the example: the cell (x 70, z 65) is absent from table
  C and so are its 8 neighbours, yet the carrier and the player are placed (rig, deck state: the carrier is placed for the sub-cells
  0 to 7, 9, 10 and 11 of the tile and not for 8 and 12 to 15, which the pattern does not list).
* **Emission.** Pass 2 emits the slot's entity entries with the terrain items of the sub-cell, only for the sub-cells the tile
  pattern lists. An entity in a sub-cell that the pattern does not list is **not drawn at all** unless it is record 14 or 15 (rig:
  record 5 active in an unlisted sub-cell stays out of queue C and the direct list; a record in a cell outside the scanned tiles
  keeps its pending bit and is not drawn). The entry's position words are the tile offset plus the sub-cell corner of the slot only
  (4096 u times (3 - the sub-cell index) on each axis in addition to 16384 u times the tile offset); the entry's LOD uses the record's
  fine offsets (`+0x0C`, `+0x0E` masked to 12 bits added to the position, and the record word `+0x10` as the unreachable key height term of section 5a) as in section 3.
* **Bit 2.** The entity update (`0x26AF8`) sets bit 2 of records **14 and 15** every frame while they are active; no other writer
  was found, and the snapshots show it clear for records 0 to 13. The builder clears bit 2 of a record when it emits the record's
  entry (tier C, `V:4A91E` zero). So after a rebuild bit 2 stays set only for records 14 or 15 that were not emitted.
* **Direct list.** At the end of tier C pass 2 (when `V:4A91E` is zero) the indices of the active records whose bit 2 is still
  set are written to `0x53A2A`, in index order, ended by -1. Only records 14 and 15 can qualify. They are drawn first in the
  entity pass (`0x1FBCC`) by the descriptor draw call; the list is only rewritten when tier C is rebuilt. Airborne: [14]; at the
  deck the list is empty because the carrier was emitted.

**Entity draw position** (`0x1FFC6`; a secondary object is the same through `0x1FFB4`, which indexes records by 0x20). Let L be
the entry's LOD. The 32-bit position passed to the draw routine is, per axis, (the entry's position word shifted left by 8) plus
((the record's position long `+0x14` for x, `+0x1C` for z, masked to 20 bits) shifted right by L); the y is the record long `+0x18`
shifted right by L. The word also supplies the sub-cell corner that the 20-bit mask removed (bits 20-21 of the record x equal
3 - (sub-cell byte mod 4), z likewise with the byte divided by 4), and the cell comes from the queue tile. The longs are stored
at `V:4ABE8`/`4ABEC`/`4ABF0` and the words (the long shifted right by 8) at `V:4ABE2`/`4ABE6`/`4ABE4`; `V:4ABF4` is
(`+0x18` + `V:4AB1E`) shifted right by L, the height relative to the camera. Units: pu divided by 2^L from the corner of the
viewer's C cell. The direct-list path (`0x1FC20`-`0x1FCF6`) gives the same coordinates from the cell difference: ((record cell -
viewer cell) shifted left by 22 plus the low 22 bits of the long) shifted right by L, and chooses its own LOD from the largest
distance with the table `0x20E4A` (x and z as in section 3, y from the record height). **Verified** by running the entity pass in the rig
with the emulator's records (draw routines stubbed): deck carrier position long (1572864, 0, 1572864), deck player (1584128, 30472,
1679360) at LOD 0, airborne carrier from the direct list (2818048, 0, -327680) at LOD 3; each equals the formula computed by hand
from the record fields. The deck player's position minus the camera offsets is the small eye offset, which confirms the sign
convention of the offsets. (No draw-time values could be captured in the emulator, so the check is emulator records and queue plus rig.)

* The carrier (record 14) at the deck start is in the same C cell as the player (cell x 70, z 65, sub-cell 10) and is therefore a
  queue entry of tier C, drawn **before** the player's own entry because of the sort. The player is an ordinary active entity, so
  its own entry is in queue C too; its draw routine (`0x21C44`) special-cases the player's own record: in an external view it draws
  the normal model; in a cockpit view the camera views 3 to 9 (`V:4A85F`) take the full model path and the other views use the
  cockpit-alternate detail list of the descriptor (`02` field 3). Secondary entries return at once from that routine (read-only). The exact choice of the part, and the part id byte it leaves in the record, are in section 8a.

**Secondary objects: what they are and how they are drawn** (note 07 section 11). The 16 objects are the effect objects of a weapon record's ground or landmark impact. An object's id word `+0x02` (`0x11`, `0x12` or `0x13`) is the descriptor index, so its descriptor is `0x2600C + 20 * id`, which for these ids lies in the static template table: templates 33, 34 and 35 (the draw routine `0x21D0C`, models at `0x400EC`, `0x400F4`, `0x40102`; note 02 section 8). The entry is a queue C entry with item flag bit 6; it is inserted like a record by the cell words `+0x06`, `+0x08` and the sub-cell byte `+0x0A` of the 32 copied bytes, and its draw position is the formula above through `0x1FFB4` (the record longs `+0x14`, `+0x18`, `+0x1C` of the copy). An object is never removed from the table: its valid bit stays set after its picture has run out (it draws nothing when its life word `+0x04` is negative), and a flight start clears the table. The life word is read by the model program (op 70, note 07 section 11.3), not by the walker.

### 7. The landmark collision slots (tail of tier C pass 2)

For `05-flight-model.md` section 11b. The slots are **filled at build time**, not by a per-frame post-process, at the end of tier
C pass 2 (`0x210A2`-`0x211E4`), and not at all when tier C is not rebuilt (the slots then keep their contents).

* `V:4A916` is the number of entries emitted in this tier C pass 2; `V:4AA7E` is set to the address of the **last emitted** entry
  of queue C (the terminator position minus 24), in the generated order (before the sort and the reorder).
* The slot filler walks backwards from `V:4AA7E` over at most `V:4A916` entries, skips entity and secondary entries (flag bits 4
  and 6) and takes at most 11 non-entity entries: slot 0 is the last generated one. The table of slot pointers is at `0x5283C`
  (11 longs) and the slot records, stride 0x100, start at `0x52868`. A non-entity entry whose descriptor's model base `+4` is not
  positive gets the pointer -1 in `0x5283C`, still occupies its slot ordinal and gets no record.
* The table `0x5283C` and the records are **not cleared between builds**: after a tier C rebuild with fewer than 11 non-entity entries the ordinals beyond the new count keep their old pointers and records (rig: 5 slots, then a rebuild with 2 non-entity entries rewrites ordinals 0 and 1 and keeps 2 to 4; a rebuild with only entities rewrites none). Nothing reads a stale ordinal: the consumers number the current non-entity entries from 0 and stop at their count (and at 10). A port that reads the table by slot count would see stale boxes. A slot record starts with **the key long `+0x0C` of the entry** (the missing reader of the earlier open question 1; its low
  word, x cell byte then z cell byte, is the "cell bytes" of `05`), followed by a copy of the footprint blocks of the model's
  nearest detail level (reached through descriptor `+4`, the first node of the detail list and the block offset, word masked to
  0xFFF), each x pair and z pair offset by the entry's X and Z words **shifted left by the LOD and masked to 0x3FFF**, so they
  are relative to the entry's own C cell; the next-node long (or -1 ends the list) and then a point list (x + ox, y, z + oz). Not
  every block word is decoded.
* **Consumer.** The collision phase (`0x2BC28`, and the entity scan `0x2AE4A`) re-walks queue C **backwards from `V:4AA7E` for
  `V:4A916` entries every frame**, skips entity and secondary entries and numbers the others from 0 up to 10 (eleven ordinals, not ten; the tracer test of note 07 8.2 walks the same eleven, `weapon_check.py landmark`, 12,600 cases: its z offset carries the high word of the table pointer divided by 4 and a miss ends the call without the motion step) in the *current* queue
  order. The slot set is the same as the filler's (the first 11 non-entities from the end) but the *order* can differ after the sort and
  the reorder (3 of 400 random states in the rig); this matters only for first-hit results. Rig, airborne state: slots 0 to 4 are
  filled, in reverse generated order: ids 86, 74, 42 and then 45 twice (keys `0x00303F41` and `0x00103F41`); at the deck queue C has
  only entities, no slot is written and `V:4AA7E` = `0x54782`.

**Ruling for note 05 (section 11b):** replace "the draw pass walks the display list backwards" by: *the builder's tail, at build
time, copies the 11 nearest non-entity entries of queue C in reverse generated order (before the sort) with their model
footprint data into `0x5283C`/`0x52868`; the collision code then re-walks queue C backwards from `V:4AA7E` each frame for the same
entries in their current order.*

### 8. The per-frame walk

Frame order (`04`, steps 7 and 9, corrected): the change detector and entity update (`0x1F54E`/`0x26AF8`); the camera and view
set-up (`0x1F45E`, `0x299AC`, `0x143F2`); the builder (`0x1F770`: rebuild only when `V:4A910` is not zero, then in every frame the
run sort of section 5a, the landmark reorder of 5b and the `0x230B8`/`0x230D8` flags); then at stage [78] the walker over
queue A, at [80] queue B, at [90] the near-ground pass (`0x2C0EC`, called at `0x11212` after the two walkers inside the same block that is skipped when `V:4AB1E` is at or below -0x8000000; it scatters a lattice of specks, point sprites and small triangles that take plane 2 away from the pixels they cover (no tiles and no pen 3 shapes: note 04, "Stage [90]: the near-ground pass") when the camera height -`V:4AB30` is at most 2,048 u (2,560 ft) in pitch band `V:4A923` 0, 3,456 u (4,320 ft) in band 1, 4,608 u (5,760 ft) in bands 2 and 3; verified in the rig at both sides of each limit; it returns nothing the core uses), then the entity pass at stage
[A8]/[AC]: first the direct entities (section 6), then queue C. The queues are walked in the order A, B, near-ground pass, direct
entities, C; the entity and secondary entries of queue C are drawn in queue order among the terrain entries.

For each entry from the start offset (section 3) to the terminator:

1. Read the id/flags word; set `V:4AB70` (the LOD) to its low nibble and `V:4AA6C` to the id.
2. Skip the entry when the first word of its descriptor is negative. Skip a descriptor whose routine is `0x21C40` when the camera
   is higher than 3,670,016 pu (17,920 ft; `V:4AB1E` below -`0x380000`) in queues A and B, or higher than 4,194,304 pu
   (20,480 ft; below -`0x400000`) in queue C. (The 49 plain model descriptors vanish above that height; the type B ground
   patches have their own cut-offs inside their routines.)
3. Load the position words and form the 32-bit position (each word shifted left by 8, that is pu divided by 2^LOD) for the draw call.
4. **Distance refresh** (every entry, except that in queues A and B an entry whose routine is `0x21C34` is exempt from both steps 4 and 5; in queue C there is no such exception): The distance word is the approximate camera distance in units of
   16 u (20 ft) divided by 2^LOD (the function `fx_distkey` of `03-3d-pipeline.md`: the position words and the camera offsets
   `V:4AB2A`/`4AB2E`/`4AB30` shifted right by the LOD, summed, absolute values, shifted right by 4, then the two-step hypotenuse of
   section 5a). Let s = (distance word at `+0x10`) shifted left by the LOD, that is the distance in units of 20 ft. If s is at
   least 0x400 (20,480 ft) the stored distance word stands. If s is below 0x100 (5,120 ft) the distance is recomputed at
   every walk. Between them it is recomputed when the game-frame counter `V:4A992` is 0 modulo 4 for an even item id (the lowest
   id bit clear) or 2 modulo 4 for an odd id. The distance word is stored back at `+0x10` and copied to `V:4ABF8`. (The earlier
   wording of `04` that below 0x100 the item is "drawn only on selected frames" is wrong: below 0x100 the distance is refreshed
   at every walk; the item is always drawn.)
5. **Invisible entries are skipped on most walks.** The result word at `+0x14` is the walker's record of the previous draw
   (below). If it is negative the countdown byte at `+0x12` is decremented and the entry is skipped while the byte stays
   non-negative; when it goes below zero it is reloaded with `V:4A974` (5, 3 or 1) and the entry is drawn again (a trial draw).
   Entities and secondary objects are not subject to this rule. For the core: an entry that drew nothing is retried every
   `V:4A974` + 1 walks; the first walk after a build always draws (`+0x14` is 0). Queues A and B skip this step for the `0x21C34` entries (step 4).
6. Decrement the draw counter at `+0x13` (byte, wrapping), publish it as `V:4A975`, copy the previous result to `V:4AB72`, set
   `V:4ABF8` and call the descriptor's draw routine with the model pointer and the two descriptor words.
7. Store the routine's result at `+0x14`: the value if positive, else -1.

**Entity entries in the refresh (queue C walker `0x1FD60`-`0x1FE46`).** The refresh of step 4 and the schedule apply to every queue C entry, entity (flag bit 4) and secondary object (bit 6) included, and to the `0x21C34` descriptors too: the only exclusion is the `0x21C34` entries of queues A and B (settled from the two walkers' code, which test the routine in the A/B walker only, and by the rig, section 8b; the earlier reading that entities get no refresh was wrong, and so was an earlier reading that excluded `0x21C34` in every queue). For an entity the walker first calls `0x1FFC6` (a secondary object `0x1FFB4`, the same on the table `0x4D23C`, index * 0x20), so the distance is computed from the **record's current position**, not from the entry's words: x (z alike) word = the sum of the entry word shifted left by 8 and the record long `+0x14` (`+0x1C`), masked to 20 bits and shifted right by L, the sum shifted right by 8; the middle term is not the y word but the long `V:4ABF4` = the record long `+0x18` plus `V:4AB1E`, shifted right by L, which the distance function takes shifted right by 8 (`0x1FFC6` sets `V:4A973` to 1, `0x1FDAE` clears it for the next entry); the camera terms are the words `V:4AB2A` and `V:4AB2E` shifted right by L, as for terrain. L is the entry's LOD nibble; the schedule uses the entity's record index as the id. A fresh entry has +0x10 = 0, below 0x100, so the first walk after a build always refreshes it. Entities and secondary objects skip the invisible-entry countdown of step 5. Effects: (1) the run sort key of section 5a for a walked entity is its stored word shifted left by the LOD, which now includes the refresh height term, so the order of an entity run can change after the first walk (a constructed case: record 5 100 u horizontally and 800 u vertically from the player; build order [14, 5, 0], after one walk and the first zero-request sort [5, 14, 0]; the real deck pair stays [14, 0], refreshed distances 27 and 1); (2) V:4ABF8 is the distance word that the part id level choice of section 8a compares for an external view or another record. For the viewed record `0x1FE8C`-`0x1FEA0` also stores the refreshed word (external view) or 0x7FFF (cockpit view) at `V:4ABFA`, which is read only by the HUD size-scaling routines `0x14F3E` and `0x1544E` (read-only). Verified: `make -C tools/m68k-rig test-lists-walk`, mode entities (rig walker against the port-free Python model, active records in the viewer's cell, record longs, old `+0x10` and `V:4A992` randomised): 2,323 queue C entries in 900 walks (entity 1,718, terrain 605), 0 mismatches (an early model that used the y word plus the camera height was off in 4 of about 280 entities; the `V:4ABF4` form, which replaces the word and the camera height, fixed them). Secondary objects: read-only (none active in the captures; the model handles their path).

**8b. The drawer's result, stored at `+0x14` (what the port must compute in the core).** The walker stores the drawer's return word if it is positive, else -1 (step 7). The return word is 0 or 1 from an early exit, or the **OR of the return words of the primitive handlers that ran**; only its sign matters (rig and emulator show only 1 and -1). Nothing reads it back from a renderer in the port: the core computes it from the model tree and the camera, and the frontend only draws what the core selected. Status tags: **V** = verified (the rig's walker compared with the emulator's logged results, or with the port-free model `tools/m68k-rig/tests/lists_model/visibility.py` on random and constructed entries; the model is a register-level transliteration and stays a rig tool, the rules below are the statement), **R** = run in the rig without an independent comparison, or read from the code and exercised, **C** = read from the code only. The counts are in the Verification paragraph below.

*Exits.* They are evaluated in this order and the first that applies decides.

| # | rule | stores | status |
| --- | --- | --- | --- |
| 1 | **Walker skips** (nothing is stored; `+0x14` keeps its value): (a) the descriptor's first word is negative (an unused run-time slot), in every queue; (b) routine `0x21C40` and `V:4AB1E` below -0x380000 (queues A and B) or -0x400000 (queue C); (c) a terrain entry whose previous result is negative and whose countdown has not gone below zero (step 5), in every queue; the `0x21C34` entries of queues A and B are exempt from (c), and entity and secondary entries are never counted down | unchanged | (b) and (c) V; (a) C (no queue entry has such a descriptor) |
| 2 | routine `0x21C34` and `V:4AB1E` at or above -0x280000 (the camera at or below 12,800 ft: 1 ft = 204.8 pu, so 0x280000 pu is 12,800 ft; it draws only above). In queues A and B these entries keep the distance word 0 they were built with (steps 4 and 5 do not apply to them), so P1 picks their nearest level | 0 | V |
| 3 | type B routines: `0x22CFC` with `V:4AB1E` below -0x380000 (camera above 17,920 ft), `0x22D0A` below -0x800000 (above 40,960 ft) | 0 | V (rig, 67 walks at the limit and one unit and 0x1000 either side, 0 differences; section 8c) |
| 4 | **no detail level fits** (plain and common drawer): each node of the detail list has a threshold word; the node fits when (its threshold masked to 0x3FFF) shifted right arithmetically by the LOD is **greater** than the scaled distance (P1) as a signed 16-bit word; a far node whose jump word is negative, the end of the list, or a leaf 0xFFFF gives 0 | 0 | V |
| 4B | the same for the type B lists, whose comparison is the unmasked threshold shifted right by the LOD **at least** the scaled distance; bit 15 of a threshold does not take part (a negative threshold word is neither shifted nor compared, `0xFFFF` ends the search); the six models 39-42, 99, 100 end their chain with an entry that names itself (threshold 0x7FFF): a scaled distance above 0x7FFF shifted right by the LOD would never leave the search, which no queue entry reaches (note 03, "Type B models"); the port returns 0 and counts the event | 0 | V (rig, 400 sweeps on both sides of every threshold, 91 exactly at it; section 8c; the bound of the loop: `typeb_loop_check.py`) |
| 5 | **tier substitution** (the level is drawn by another tier): leaf bit 13 set: queue C returns 1 without drawing, A and B draw; leaf bit 13 clear: with `V:4AAFA` at or above 0x80 queues A and B return 1 without drawing and C draws; below 0x80 A and B return 1 only if the item id occurs in queue C, else they draw. (The earlier note had only the last case and omitted the zoom-at-or-above-0x80 case, which is the normal one.) The game's zoom keys reach only the words 0x80, 0x40 and 0x20 (P1), so the third case is the one below 0x80 | 1 | V (zoom 0x80 in the snapshots and random frames; zoom 0x20, 0x40, 0xFF and 0x100 by `zoom_check.py`, section 8c) |
| 6 | **visibility pre-test** (flat blocks whose flag byte has both bits 0x42: objects 43, 47, 66, 82-85, 96): runs only when `V:4AB30` is above -0x140, `V:4A924` is 0 (the steep-pitch flag: written only by the pitch-gate routine `0x143F2`, once per frame, from the view pitch word v of section 1. Outside the gate it is set to 1 when 450 < v < 3200, that is for 450 < v <= 600 (45 to 60 degrees nose down) and 3150 <= v < 3200 (45 to 40 degrees nose up), and to 0 for every other v; inside the gate, 600 < v < 3150, the routine does not write it and it keeps its value. V: `gate_check.py`), and either the entry's previous result `V:4AB72` (its `+0x14` before this walk, 0 on the first walk) is not positive or the published counter `V:4A975` (the entry's `+0x13` after its decrement) and 3 is 0 (a visible entry is re-tested every fourth walk). It takes the horizontal edge from vertex 0 to vertex 1 of the block (x and z words, vertex 1 minus vertex 0) and the horizontal vector from the object to the camera (negated camera word minus the entry position word, per axis), normalises both to length 256 (P4), forms the magnitude of the dot product shifted right by 4 (call it the dot measure) and **rejects when it is below the table entry** of P5. A small dot measure means that the horizontal line of sight is close to **perpendicular** to the edge; the required minimum is 0 for the nearest entries and grows with the scaled distance, so far flat blocks seen along their edge direction pass and those seen across it are culled | 0 | V (constructed entries) |
| 7 | **flat cull** (block flag bit 1): when `V:4AB30` is at or above -0x20 and the constant 0x60 (0x170 with flag bit 2), shifted right by the LOD, is below the scaled distance | 0 | V (constructed entries) |
| 8 | **first-vertex pyramid test** (leaf bit 14 clear, non-rotating blocks only): the block's first vertex in camera space (P3) must have depth > 0 and the magnitudes of x and y at most 1.5 * depth (word arithmetic: depth + depth >> 1); this is the whole "bounding box / behind the camera" test, there is no other, and it tests one vertex, not a box. For rotating blocks (flag bit 0: aircraft, carrier, ships) the corresponding test at `0x220E6` compares with 1 and is dead | 0 | V |
| 9 | **primitives**: the tree of the stream is walked (note 02 section 3) and every program item whose gate passes (P6) runs its handler (the return words of P7 and the table in note 03). The result is the OR of the words of the handlers that did not abandon their program; a stream that ends with the word 0xFFFF returns **0 even if primitives drew** (the common drawer: the cockpit block of the aircraft ends so, no terrain or landmark model does), and in the type B drawer a negative handler word aborts the whole drawer with 0 (section 8c). An entity's block with an empty stream (cockpit block) returns 0. **Port obligation (the zero difference of the clipped line):** a segment whose margin difference den is zero never returns in the original (the routine stays in a one-instruction loop; note 03, "The clipped line routine"); the port rejects it, so that line contributes 0 to the OR and draws nothing, **and increments a debug counter of such segments** (like the type B search of rule 4B); the counter is the evidence of whether the game's camera-space words ever reach the magnitudes that make den zero (no drawn frame of the runs did). The port's differential found 11 such segments in 280,000 random sequences with full-range words; none was seen in a drawn frame of the rig and emulator runs | OR | V (section 8c: 2,204 one-entry walks, 781 tree walks, 0 differences from the combination of the logged handler returns; the returns themselves 67,000 direct calls) |

*Exits that exist only for entity entries* (the routine `0x21C44` and the drawer's entity path; `04a` section 8a tells which of them leave the part id unchanged). The walker stores the result of the player's and other records' entries, but nothing reads it.

| # | rule | stores | status |
| --- | --- | --- | --- |
| E1 | a secondary entry (bit 6 of the low byte `V:4A913`) **handed to the entity routine `0x21C44`**: that routine returns 0 at once, before anything else; this exit belongs to `0x21C44` only: effect objects (secondary entries) are drawn through `0x21D0C` with their own descriptors (note 07 section 11), never through `0x21C44` | 0 | R |
| E2 | the viewed record's own entry in its own cockpit (test 9 of section 8a) with an alternate list whose first word is 0xFFFF (templates 0 and 1): returns 0 at once | 0 | R |
| E3 | the alternate-frame skip (a non-clipping leaf, byte `V:4A856` non-zero and `V:4A992` even): nothing ever sets `V:4A856`, so it never applies | 0 | C (dead code) |
| E4 | an inactive record (block flag bit 0 set, record `+0x00` bit 6 clear): returns before the part id store | 0 | R |

*Quantities and gates the exits depend on.* They are rules of the same standing as the exits: the core needs each of them to reproduce the sign of `+0x14`.

| # | rule | status |
| --- | --- | --- |
| P1 | **Scaled distance.** The compare value is the walker's distance word d (`V:4ABF8`, units of 16 u divided by 2^LOD) times the zoom word `V:4AAFA`, doubled, divided by 256 and kept as a signed 16-bit word: floor(d * 2 * zoom / 256), equal to d at zoom 0x80; it is not scaled when the byte `V:4A895` is non-zero. `V:4A895` is set to 0x80 at the mission start (`0x1D304`, `0x1F060`) and by the zoom-key handler (`0x1E870`) when it leaves the zoom word at 0x80, and cleared when the zoom word is another value; the keys halve the zoom word down to 0x20 and double it up to 0x80, so the words the game reaches are 0x80, 0x40 and 0x20 (scale 1, 1/2, 1/4); 0xFF and 0x100 are never reached, and the byte only makes the 0x80 case skip a multiplication by one. A scaled value of 0x8000 or more is a negative word, so every node fits and the **nearest** level is chosen (zoom 0x100 and d 16384 gives block `0x6FC`). The *unscaled* d is used by P2 | V (rig sweep of `V:4ABF8` against the F/A-18 list; `zoom_check.py`: zoom words 0x20, 0x40, 0x80, 0xFF and 0x100 (the first three are the words the keys reach), each with `V:4A895` zero and not, 150 frames at scale 1000: 0 differences; the writers C, read from the code) |
| P2 | **Precision exponent of a block.** The block header (`02` section 2.2) has the thresholds Tm (word `+2`) and Tf (word `+4`) and the nibbles H (high) and L (low) of byte `+6`. Each threshold is shifted right arithmetically by the LOD and compared as a signed word with the **unscaled** distance d (not P1). Farther than Tf: the vertex shift is L and the position exponent 0. Between Tm and Tf: vertex shift H, position exponent L - H. Nearer than Tm: vertex shift 0, exponent L. The comparisons are strict ("below d" means farther) | V (through rule 8 outcomes) |
| P3 | **First-vertex camera-space position.** The camera-relative position in pu divided by 2^LOD is the camera long (`V:4AB1A`, `V:4AB1E`, `V:4AB22` for x, height, z) shifted right by the LOD plus the entry's longs (word shifted left by 8); it is shifted right by 8 minus the position exponent of P2 and kept as a 16-bit word per axis. The vertex words are shifted right arithmetically by the vertex shift of P2 plus the LOD, the position words are added, and the camera matrix (nine words at `V:4AC90`) is applied row by row: each camera-space component is the sum of the three products, shifted right by 8 and kept as a 16-bit word. Flat blocks store (x, z) pairs only: the height term (position y word times the matrix's second column, shifted right by 8) is added to each transformed vertex | V (rule 8: 449 first-vertex exits, 0 differences) |
| P4 | **Normaliser `0x29C0E`** (scale a vector to a given length, here 256). A length argument of 0, or an approximate vector length of 0, gives the zero vector. Otherwise the approximate length A is the hypotenuse function of section 5a applied to the three component magnitudes (each made non-negative as a 16-bit word, the result at most 0x7FFF). The scale n is the smallest power of four that exceeds A (and is at least 4), with s = log2 n. The quotient q = floor(256 * n / A) is an unsigned 16-bit division; it cannot overflow for length 256 (q is at most 1024), so the hardware's overflow case (the register unchanged) does not arise here. Each result component is floor(component * q / 2^s), the 32-bit signed product shifted right arithmetically. The result is approximate: for a vector with a component of at least 64 the length comes out between 253.9 and 260 (-0.8 and +1.6 percent), and the integer length A makes vectors of a few words up to 40 percent long | V (3,000 random inputs against `visibility.vnorm`, bit for bit) |
| P5 | **Pre-test minimum.** The table is the first (`0x23FF4`) when `V:4AB30` is above -0x80, else the second (`0x2400C`); both have 12 words (Constants). The index is the scaled distance of P1 shifted right arithmetically by 4, limited to 11 (so indices 0 to 10 are the distance in steps of 16 and 11 stands for 176 and more). The minimum is compared with the dot measure as signed words: reject when below. The dot measure is at most 0x1000 for exact unit vectors of length 256 and 0x1081 over 60,000 random pairs of vectors with a component of at least 64 words (the edges of the eight block types are thousands of words long, and at index 10 or more the camera vector is more than 2,000 words), so **entries 10 and 11 of the second table (0x1400 and 0x1A00) always reject** in play: a flat block seen from a camera at least 128 u (160 ft) high, the case that selects the second table, is culled, at its next pre-test, at a scaled distance of 160 and more. A scaled distance of 0x8000 or more gives a negative index and the read goes before the table (entry -1 is the word before it); for the second table those are the first table's entries in reverse order (index -1 is its last entry), for the first table the preceding words are code: indices -1 to -5 read large positive values (reject) and index -6 a negative one (passes), further back they are of either sign. This case is unreachable in the shipped data (the distance words of queue entries stay in the low hundreds, and the pre-test needs the camera below 400 ft) | rule in range V (constructed entries); negative index C |
| P6 | **Program-item distance gates** (before each handler of a program). An item whose first word is non-negative and not 0x7FFF carries two gate words G1 and G2: let g1 = G1 shifted right by the LOD. With g1 below the scaled distance (the object is beyond the first gate) the item runs only when G2 is non-negative and G2 shifted right by the LOD is **not below** the scaled distance; with g1 at or above it the item runs only when G2 is negative. An item that fails its gate **ends the current program**, exactly as a negative handler return does: its remaining items do not run, while the other programs of the draw item's list and the tree walk go on (`02` section 4 states the same). Items whose first word is 0x7FFF have no gate. An item with a positive operand count brings that many extra vertices in before its handler runs (note 03, Block set-up) | R (read from `0x22846`-`0x2285E`; the model's tree walk implements it and equals the rig on every decided entry; the effect on the sign is inside the combination check of section 8c) |
| P7 | **Handler returns.** The return word and condition codes of every operation, with the primitive it draws, are the table of note 03 ("Draw operations: what a handler returns and what it draws"). In short: 1 when the primitive reached the filler, the line routine or a point sprite, 0 when everything was clipped away or nothing is drawn, and a negative result (negative word or negative condition codes) in the defined cases: op 3 and op 69 (face away without bit 14, or every depth behind), ops 39 and 40 (slot 1 outside the closed pyramid), op 31 (the pre-test rejects), op 4 (camera above 5,120 ft), op 26 (negative condition codes unless every listed depth is behind the camera, in which case 0 with non-negative codes), and a rejected point of ops 1, 58, 62 and 49. In the common drawer such a result abandons the rest of the current program and adds nothing to the OR; in the type B drawer a negative word aborts the whole drawer with 0. **There is no record check that returns negative**: the ops that read the record at `0x4B23C` only choose skip bytes. **Exceptions that decide the sign:** the op 4 handler (`0x238C6`, the detail faces of the aircraft near body and normal levels) returns 1 without drawing at every exit of its gate and after its faces (the clipper results are discarded), so a near-body program yields a positive `+0x14` although no primitive is produced (were it 0, the walker would store -1); op 20 and op 32 count only their first N strips; op 48 does not clip; ops 74, 75 (camera higher than 192 u, 240 ft) and 9, 10 (camera not higher than 128 u, 160 ft) draw only on that side. The gate of op 4 in feet (1 ft = 204.8 pu): exit (i) (the record's byte `+4` with exactly one of bits 6 and 7 set; both set adjust the height by the record's word `+0x4E`) **precedes** the opener (external view or pitch band 3), then for the viewed record's own entry (entry id `V:4A994`) band 0, or `V:4AB1E` below -0x2000 (40 ft), closes it; for another record whose class byte `+0x62` is not 0x30 or 0x14, `V:4AB1E` below -0x10000 (320 ft) closes it in bands 0 and 1 and in band 2 when also below -0xA0000 (3,200 ft); above 5,120 ft (-0x100000) the handler returns -1. Section 8a uses the same exits for what is visible | V (direct calls, 67,000; op 4 gate and return: 1,500 constructed entries on both sides of every boundary) |

The port's core therefore decides `+0x14` as follows: apply the skips of rule 1; apply rules 2 to 8 in order with P1 to P5; for the entries that reach their programs, run the primitive layer (its own clip on the walked tree with P6 and P7) and take the OR; an entry whose program ends without any positive handler return stores -1. The op 4 exception makes the near-body program of the player's own entry +1 in the gated cases. The level choice (rule 4 with P1) belongs to the core and not only to the frontend: it decides `+0x14` through rules 4, 5 and 8 and the part id of section 8a, so the scene carries the entry's distance word and the zoom state, the core picks the level, and the frontend draws the level the core chose.

Result of the rule and its effect on the next walk: an entry whose result is -1 is skipped at the following walks (step 5): its countdown byte `+0x12` is decremented at every walk, the entry is skipped while the byte is non-negative, and when it goes below zero it is reloaded with `V:4A974` (5, 3 or 1) and the entry is drawn again (a trial draw, which is a real draw: a trial that finds something stores a positive result and the entry is then drawn at every walk). So a build-time countdown c (section 4) makes the first retry the (c + 1)th walk after the first, and later retries every `V:4A974` + 1 walks; the first walk after a build always draws (`+0x14` 0). Entries that are visible stay drawn every walk; only the pre-test of rule 6 re-checks them every fourth walk. Entities and secondary objects have no countdown and their result is read by nothing; the `0x21C34` entries of queues A and B have none either (they are drawn at every walk and keep the distance word 0 they were built with).

**Verification.** `make -C tools/m68k-rig test-lists-walk` runs the driver `tools/m68k-rig/tests/lists_model/walk_check.py` (modes, seeds and sizes in its header). It needs the user's git-ignored `extracted/interceptor.bin`, the emulator log `extracted/captures/logs/lists.txt` and `librig.so`, and skips cleanly (exit 0) without them. It loads the logged deck and airborne states, or random frames built from them, into the rig, builds the queues with the original builder `0x1F770` (request `0x0B`), replaces the polygon filler by a return, runs the original walkers (`0x1FA24` and `0x1FA36` for queues A and B, `0x1FBCC` for the direct entities and queue C) and compares the entry fields `+0x10`, `+0x12`, `+0x13` and the result `+0x14` with the emulator's log or with the model `visibility.py`, which walks the same memory image and decides only the entries whose drawer path it implements (the others are counted as undecided with the reason, never as equal). Checks, as run by the make target:

| check | what | result |
| --- | --- | --- |
| snapshots | the rig's `+0x14` over the walked window of tiers A and B, deck and airborne, against the emulator's logged queue | 61 of 61 equal (airborne A 30 with one positive, B 22, deck B 9, deck A empty); the model decided 21, 0 differences |
| random frames | 400 random frames (cell, sub-cell, height, camera offsets, octant, matrix), three walks per queue so that countdown and refresh schedule run: 3,739 entries drawn, 2,762 more skipped by the height limit or the countdown | the model decided 1,909, 0 differences |
| entities | 900 walks of queue C with 0 to 3 further active records in the viewer's cell, random record longs, old distance words and frame counters: 2,323 entries, 1,718 of them entity entries and 605 terrain | 0 differences in `+0x10`, `+0x12`, `+0x13` (the model now handles entity entries, section 8) |
| constructed entries | 3,000 frames with a low camera (height up to 0x14000 pu), a random terrain entry of the window of a random queue replaced by a random plain or common-drawer item (60 percent of them the flat blocks of rule 6, in queues A and B) with a random LOD and a position near the camera: 607 entries | the model decided 384, 0 differences |
| normaliser | 3,000 random vectors through `0x29C0E` with the length argument 0x100 | 0 differences, bit for bit |
| `0x21C34` | descriptor 51 given to the last entry of each queue (distance word 0, result -1, countdown 3) | A and B: distance word and countdown unchanged, counter decremented; C: distance word refreshed, countdown decremented; the model agrees |

Exits decided by the model (the number of entries that took each rule; all of them agree with the rig):

| rule | snapshots (61) | random frames (3,739) | constructed (607) |
| --- | --- | --- | --- |
| 2 (`0x21C34` cut-off) | 1 | 33 | 14 |
| 4 (no level) | 11 | 781 | 234 |
| 5 (tier substitution) | 1 (the only positive) | 681 | 88 |
| 6 (pre-test) | 0 | 0 | 16 |
| 7 (flat cull) | 0 | 0 | 5 |
| 8 (first vertex) | 8 | 414 | 27 |
| not decided: type B (rules 3 and 4B) | 25 | 1,430 | 0 |
| not decided: program reached an op or vertex synthesis the model lacks (rule 9) | 15 | 400 | 223 |

So rules 6 and 7 are verified on constructed entries only (flat blocks in queues A and B with the camera below 400 ft, respectively 40 ft; none of the snapshots or random frames takes them). Rule 3, rule 4B and rule 9 are not decided by this model (`visibility.py` stops at the type B routines and at the first operation of a program), but they are verified in section 8c against the rig's own results: the cut-offs (67 walks at the limits), the level search (400 sweeps) and the combination of the handler returns (2,204 one-entry walks). In the airborne snapshot all 51 entries with -1 (29 in tier A, 22 in tier B) split into 12 in tier A and 6 in tier B that take rules 2, 4 or 8 (the model decides them), and 17 in tier A (5 type B patches, 12 terrain programs) and 16 in tier B (15 type B patches, 1 program) that ran to their ends with every primitive clipped away or rejected (the rig's own result; not a bounding box). The one positive is a tier substitution (rule 5). The exact sign of the entries that reach their programs needs the port's own clip run on the walked tree, with the vertex table of note 03 (open question 7); section 8c states the combination and verifies every handler return.

**The dead compare.** Before the draw call the walker compares the distance word with `V:4AA6A`, the "visibility range" that
`0x1FA48` computes for each walker run from the camera height (table `0x4621C`, 64 bytes, read beyond its end, `01b`); the result
is never used, no branch follows (`04`, "Visibility range and culling"). The range word has no effect. The walker's culls are the
ones above: the generation limit and walked window by altitude band, the height limit for the plain model descriptors, the skipping
of entries that drew nothing, and the clipper of `03-3d-pipeline.md` for polygons behind the camera.

**How the walker's position reaches note 03's transform.** The three position longs (words shifted left by 8) are pu divided by
2^LOD, relative to the corner of the viewer's C cell. The drawer adds them to the camera longs `V:4AB1A`/`1E`/`22` shifted right
by the same LOD (minus the camera position), then scales by 8 minus the exponent of the detail block into model units; model
vertices are shifted by the LOD plus that exponent, and the detail-list distance thresholds are compared as the threshold masked to
0x3FFF and shifted right by the LOD against the scaled distance word (P1). The camera offsets are the words of section 2, **in u, minus the
camera position relative to the viewer's C cell corner**.

### 8c. The primitive layer: what the programs add to the result

Rule 9 of section 8b says the drawer's result is the OR of the handler returns. This section states the combination exactly, the cases that decide the sign, and the two first
divergences of the code round. The return word of every operation, with the condition codes and the primitive each draws, is the table of `03-3d-pipeline.md` ("Draw operations: what
a handler returns and what it draws"); the vertex table the programs read (first vertices, the flat second section, strips, extra vertices, type B) is "Block set-up of the terrain models"
there. Tags as in section 8b; the counts are in the table at the end of this section.

**Combination (V).** An entry that reaches the tree walk of its block (`0x225F0` for the common drawer, `0x22E56` for the type B drawer) has the result:

| drawer | result | stored by the walker |
| --- | --- | --- |
| common (`0x21C40`, `0x21D0C`, entity `0x21C44`) | the OR of the words of the handlers that did not return negative condition codes; **0 when the walk ends on the word 0xFFFF**, whatever was added before | the word if positive, else -1 |
| type B (`0x22CFC`, `0x22D0A`) | the OR of the handler words; **0 as soon as one handler returns a negative word**, even after earlier ones drew | the same |

A handler with negative condition codes abandons the rest of its **program** only (the draw item's other programs, and the tree walk, go on); a program also ends at the item with
bit 14 set, at the first item that fails its gate (`02` section 4, P6), and the tree walk ends at the draw item with bit 14 or 13. An entry that reaches the tree walk and whose
programs add nothing positive stores -1 (retried by the countdown of section 8, step 5). The stream end 0xFFFF occurs in the **cockpit block of the aircraft** (its whole stream is the one
word 0xFFFF: result 0) and in no terrain or landmark model (`census_ffff.py`: the 64 terrain and landmark descriptors have 255 leaf streams, and following both children of every node no path reaches the word; the 8 alternate lists of the aircraft models, which have a second leaf whose stream is the one word 0xFFFF, are the control). The type B abort needs a
handler that returns a negative word; none of the operations the type B models use (2, 13, 24, 25, 26, 30, 74, 75) can, so it is code only (C).

**What decides the sign for the port** (everything after rule 8 of section 8b, in the order of how often it mattered in the rig). An entry that gets this far is **positive iff some
handler, not abandoned, drew**:

1. **Polygons** count when the four-plane clip leaves at least three vertices and every depth is positive (the filler is reached), not when the polygon is merely in front: degenerate
   polygons (the type B placeholder quads) count when their point is inside the pyramid.
2. **Lines**: op 2, 13, 14, 11, 12, 6, 19, 22 and the height-gated 74 and 75 count when the clipped segment is non-empty; **op 48 does not clip**: it counts only when both end points lie inside
   the closed pyramid (the first divergence below). Ops 74 and 75 do nothing unless the camera is higher than 192 u (240 ft); ops 9 and 10 (bridges) do nothing when it is higher than 128 u.
3. **Points** (1, 58, 49, 62): a point outside the pyramid returns 0 **with negative condition codes**: it abandons the rest of its program. Model 65 is a program of ten op 1 points (a string of
   lights): the first point that is rejected drops the rest of the string; models 80 and 81 have one point per program, models 34 and 35 one op 58 point.
4. **Ops that never count or always count**: op 20 and op 32 count only their first N strips; op 26 never counts in the common drawer (it occurs only in type B models, where it counts); op 4 returns 1
   without drawing (entity models); the selectors and synthesis ops add 0. Op 3 and op 69 abandon the program when the face is away and bit 14 of their word is clear, or when every depth is behind.
5. **The pre-test of rule 6 also runs as an item**: op 31 returns -1 for the flat hatch models (44, 45, 59, 60, 68-71, 74-79, 93, 94, 97) under the same conditions and table; its operand is a byte offset into the vertex list of the drawn block (`V:4AAEA` + 10 + operand: the shipped operands are 12 and 20), the pair of flat vertices is read there (note 03, "The block that op 31 names").
6. **The vertices must exist**: the second section of the flat blocks with flag bit 2 (models 46, 72, 73, 95), the strips of flag bit 3 and the extra vertices are part of the block set-up; an entry
   whose lines and quads name those slots is positive or negative by the geometry of those vertices, and a port without them can only store -1.

**The two first divergences of the code round, explained (V, `prim_walk.py divergences`).**

* *Tier C, item 45 (model 61), LOD 1, distance word 914: rig -1, port +1.* The scaled distance 914 is beyond every threshold of model 61 (80, 256, 768 shifted right by 1), so the level is the
  last-resort leaf, whose whole stream is one program with **one op 48**. Op 48 is the line that does not clip. The port clipped the line (as for op 2) and counted partly visible lines; the original
  counts a line only when both ends are inside the pyramid. In 446 constructed walks of this entry the program was reached 40 times and op 48 returned 1 in 14; the clipped-line rule would have
  counted 9 more (the port's +1 against the original's -1) and the converse never happens.
* *Tier A, item 30 (model 46), LOD 3, distance word 401: rig +1, port -1.* Model 46 is a flat block with flag bit 2. At this distance the level is the second one (threshold 0xD00 shifted right
  by 3 is 416, above 401), whose program is one op 13 line list over the slots 0, 2, 4, 6, 8, 10, 12 and 14; the block has two plain vertices, **slots 2 and above exist only in its second vertex section**.
  The port does not make those vertices, so no line can be drawn and it stores -1. With the second section made as the block set-up says, the line rule (the segment meets the open pyramid) reproduces the rig:
  of 146 constructed walks that reach the program 142 are positive in the rig and the same 142 in the model, 0 differences.
* *The remaining differences of the round*, attributed from the models' operations (not re-run against the port): items 56, 79 and 30 (models 72, 95, 46) have the second vertex section; items 112, 113, 90, 94
  (models 128, 129, 106, 110) are type B patches whose zero placeholder corners the extractor dropped and whose lines are the height-gated ops 74 and 75; item 74 (model 90) draws with op 48 (the unclipped
  line) and op 39 (-1 when slot 1 fails the word test of the closed pyramid, `03` "The slot-1 test of ops 39 and 40"). The hatch and stripe family (ops 19-23, 32, 57), op 31 and ops 10, 15, 16, 7, 8 were counted as unmodelled and are fully specified in `03` ("Hatch and stripe geometry": the exact vertex lists and which calls count, 17,000 checks).

**One block per draw.** A draw walks exactly one stream under one block (the chosen level's); the stream never switches block, so rule 8 tests that block's first vertex only and there is no OR across blocks. The player's alternate list holds a near-body block and a cockpit block, but a draw walks one of them (section 8a, tests 6 to 9). A level whose stream ends in 0xFFFF returns 0 for the whole draw (the cockpit block).

**What the port's drawer must therefore do**, in the order of the cases above: (a) make the block vertices exactly (second section, strips, extra vertices, type B zeros with the LOD-only shift);
(b) run each program item through the gate rule P6 (a failed gate ends the program); (c) apply the return of each op as the `03` table says, including the abandonment by condition codes for rejected
points and the height gates; (d) OR the kept words, force 0 on a 0xFFFF stream end and on a type B negative word; (e) the entry's sign is the OR's sign.

**Counts.** Reproduce: `python3 walk_check.py primitives SEED SCALE` in `tools/m68k-rig/tests/lists_model` (SCALE 1000 is the size below; `make -C tools/m68k-rig test-lists-walk` runs scale 60); the
scripts run alone as `python3 SCRIPT.py SEED N`.

| check | claim | N (SCALE 1000, seed 1) | differences |
| --- | --- | --- | --- |
| `prim_ops.py` | the return word and condition codes of every handler of the table, direct calls with random vertex arrays (some behind, some outside the pyramid, some on its edge) | 67,000 calls over 67 operations, 70,000 checks (ops 21, 57 and 67 have two per call) | 0 |
| `prim_walk.py primitives` | the stored `+0x14` of one-entry walks (any image descriptor, terrain, flat, type B, bridges, landmarks) equals the combination rule from the logged handler returns | 2,204 walks, 781 reach the tree walk (272 common, 509 type B), 368 positive, 41 common-drawer walks with a negative-flag handler, 16 constructed walks whose type B search loops (the distance word was forced above 0x7FFF shifted right by the LOD; rig timeouts, not reachable in play) | 0 |
| `prim_walk.py vertices` | the vertex table after the draw equals the model's block pipeline | 945 blocks, 9,261 slots (643 flat, 302 solid, 109 second section, 175 strips) | 0 |
| `prim_walk.py divergences` | the two first divergences | 446 walks of item 45; 146 reaching the program of item 30 | 0 (and the 9 + 142 cases above) |
| `prim_walk.py typeb` | rule 4B | 400 sweeps (91 exactly at a threshold) | 0 |
| `ops_tri.py`, `ops_pen.py`, `ops_points.py`, `ops_synth.py` | ops 7, 8; 39, 40; 1, 49, 58, 62; 41-44, 59 | 1,600; 1,200; 1,600; 200 per op | 0 |
| `sprite_check.py`, `disc_check.py` | the three sprites, the disc | 1,500 each; 200 discs, 15,326 rows | 0 |
| `op4_check.py` | op 4 gate (camera height and record altitude set independently), first word, face list ending, return | 1,500 entries (997 closed, 112 returning -1, 391 open; 487 with non-descending face words) | 0 |
| `ent_check.py` | entity entries through the real draw path (descriptors filled as the game does) and the combination rule | 300 frames, 562 entity entries, 475 with handler calls; 129 constructed common and type B entries | 0 |
| `zoom_check.py` | P1 and rule 5 at zoom 0x20, 0x40, 0x80, 0xFF, 0x100 with `V:4A895` zero and not | 150 frames (6,382 entries, 190 decided by rule 5 below zoom 0x80) | 0 |
| `gate_check.py` | the writers of `V:4A83E` and `V:4A924` | 5,000 pitch states (1,170 inside the gate) | 0 |
| `hand_check.py` | the x handedness (`03`) | 772 points | 0 |
| `cut34_check.py` | the `0x21C34` cut-off at both sides of 0x280000 | 300 frames, 16 draws, all pen 8 | 0 |
| `sec2_check.py` | the second vertex section of the flat blocks with flag bit 2 (note 03), constructed blocks against the rig's table | 300 blocks (92 with two or more groups, 59 groups with n = 0, 73 with b8 of 0 or 1) | 0 |
| `line_check.py` | the clipped line against the open-pyramid rule (note 03, "The clipped line routine") | 6,000 and 30,000 segments: 13 and 60 boundary segments drawn by the rig, 1 sliver not drawn | 0 unexplained |
| `census_ffff.py` | no stream of the terrain and landmark models ends on 0xFFFF | 64 descriptors, 255 streams; control: 8 aircraft cockpit blocks | 0 |
| `modeldata_check.py` | the builder, the walkers and the drawers never write the model data (note 03, type B vertices) | 150 frames, 126 constructed walks | 0 writes |
| `typeb_loop_check.py` | the self-naming last entry of six type B models is out of reach | 150 frames, 471 entries; largest true distance 37 percent of the limit | 0 beyond |
| `nearbody_check.py` | the near body polygons of the deck state (band 3; bands 1 to 3 after an external view) and none airborne | deck and airborne, bands 0 to 3, with and without the external-view pass | 0 |
| `hatch_check.py` | the hatch and stripe family (ops 19-23, 32, 57) and ops 6, 11, 12, 14: the exact vertex lists given to the clippers (word arithmetic), shift and count words (counts of 0 or less included), the pen and fill words, which calls count in the return (note 03, "Hatch and stripe geometry") | 1,000 calls per op, 18,000 checks | 0 |
| `line_model.py` | the clipped line routine `0x33936`: the plane cascade, the intersections and the projection, word exact (note 03, "The clipped line routine") | 100,000 segments, 55,028 drawn; the error word; 18,164 den = 0 segments (the rig hangs) | 0 |
| `stipple_check.py` | the fill state each operation writes and the next one inherits (note 03, "Fill state") | 1,000 sequences, 5,467 handler calls, 6,638 output calls (op 49 included) | 0 |
| `bsp_check.py` | the three side tests of the block tree (note 03, "The side tests of the block tree"), with the frame words written by the original instructions | 5,000 sets per test and 5,000 writer runs | 0 |
| `roll_check.py`, `eye_check.py` | the cockpit camera: the angles and the roll sign of the matrix; the position with the eye offset (note 03) | 500 attitudes; 300 records | 0 |
| `synth2_check.py`, `synth_note_check.py` | the fixed-formula synthesis ops 38, 45-47, 56, 63-65 and ops 34-36, 52, 53; the tables of ops 41-44 and 59 as written, and the operand-free rows read from the text of note 03; the stream advance of ops 52, 53 | 500 arrays per op; 200 per op; 300 runs per op | 0 |
| `typeb_toggle_check.py` | the toggle word across objects: kept by the type B entry, cleared by the common one, seen by op 26 (note 03, "Fill state") | 987 walks, 36 pairs (33 op 26 filler calls) | 0 |
| `nearbody_rows_check.py` | the near body triangles for last rows 144 to 190: uncut classification, line height, fill height (note 03, "Where the near body falls") | 28 checks | 0 |

### 8a. Entity part id (+0x7B)

The byte `+0x7B` of an entity record is the **part id**: the first word of the model block that the draw routine used last for that
entity, low byte only (`02` section 2.2, the block header). It is the input of the three-vertex ground test of the collision
routine (`05` section 17.12, which this subsection settles; open question 15 of `05`) and of the projectile hull test (below).
Everything here is the rule the port's world module must compute itself, from the same inputs, and never read back from a renderer.

**Written only after a completed draw.** Every return before the store leaves the byte unchanged: a secondary entry, an alternate list starting with 0xFFFF, no level fitting or a 0xFFFF leaf, the three tier-substitution returns (rule 5 of section 8b), the pre-test (cannot apply to aircraft: their flag is 0x01), the flat cull, the alternate-frame skip, and an inactive record. The block flag byte must have bit 0, else the non-rotating path never reaches the store; the first-vertex test of rotating blocks is dead. Rig: with the byte preset to 0x55 the deck entity pass leaves 0xFF for records 0 and 14, and the airborne pass leaves 0xFF for record 0 and 0x55 for the carrier (its draw took the no-level return).

**Writer.** There is exactly one instruction that stores to `+0x7B` by name, at `0x21F10` in the common drawing routine (`0x21D0C`), run once per
draw of an entity entry. It happens after the detail level has been chosen and after every early return of the drawer, and only when
the record's active bit (bit 6 of `+0x00`) is set and the chosen block has flag bit 0 (a model that rotates with its own attitude;
every aircraft, carrier and ship block has it). Nothing else sets, restores or resets it for the player (the flight start `0x1D8B8`, the clears of `0x1D858` and the restart do not
touch it; only the whole-record clear of records 1 to 3 at `0x1F14A` zeroes it, for those records). The byte is therefore the part id of the last *completed*
draw of that record.

**Which block is drawn for the player** (the prelude `0x21C44`, entered through the entity wrapper `0x26944` for an active record, whose own state machine is note 07 section 11.5;
the descriptor's second list is the *cockpit-alternate list*, `02` field 3). The tests run in this order and the first that applies decides:

| # | test | list walked | block drawn, part id | status |
| --- | --- | --- | --- | --- |
| 1 | the entry is a secondary object (bit 6 of `V:4A913`, the low byte of the entry word) | - | returns at once, no write | R |
| 2 | external view (`V:4A83D` not 0) | the normal list | the level chosen by distance (table below); in the ring views always id 0 | V |
| 3 | the record is not the player's (`V:4AA6E` differs from `V:4A996`) | the normal list | by distance | R |
| 4 | head view code `V:4A85F` 3 to 9 | the normal list | by distance (the camera is at the aircraft: the nearest level, id 0) | R |
| 5 | the descriptor's alternate list is its normal list (templates 2, 8, 9, 11, 12, 13) | the normal list | by distance | R |
| 6 | `V:4A8ED` not 0 (set to 1 by `0x141E8` at 0x14262 when the view switch leaves an external view for the cockpit, cleared at the end of the entity pass that follows: one pass) | the alternate list | body, id 0 | R |
| 7 | the pitch band `V:4A923` is 3 | the alternate list | body, id 0 | V |
| 8 | `V:4A923` is 1 or 2 and the eye measure E = (`V:4AB30` shifted right by the entry LOD `V:4AB70`) is at or above -32 | the alternate list | body, id 0 | V (the boundary -32 itself R) |
| 9 | everything else (`V:4A923` is 0, or 1 or 2 with E below -32; view codes 0 to 2 and 10 to 13) | the alternate list with its first leaf and stream item skipped | the cockpit block, id `0xFF` | V |

The alternate list of the F/A-18 (and of templates 3, 5, 6, 7, 10, 14 and 15) is three items: a leaf-only item for the near body block
(the same block as the nearest normal level: the same offset from the model base, 0x6FC for template 4 and 0x790 for template 3; its leaf word is 0x86FC resp. 0x8790, bit 14 clear and bit 15 set, against the normal level's 0x46FC resp. 0x4790 with bit 14 set, read from the image), its stream item, and a second leaf-only item for the cockpit
block (first word `0xFFFF`, so id `0xFF`), then a terminator. Test 9 skips the first two. In tests 6 to 8 the whole list is walked and
the single near body block is drawn at every distance. **Template shapes** (read from the 16 template records at `0x25ECC`, 20 bytes each; the player's descriptor 0 is a copy of template 3 when the aircraft code `V:4A901` is 0x10 and of template 4 otherwise, made at the mission start `0x1D56C`; other records copy theirs when they are spawned, `0x276D2`; R): (a) the alternate list is the normal list: templates 2, 8, 9, 11, 12, 13 (test 5); (b) a three-item alternate list: templates 3, 4, 5, 6, 7, 10, 14 and 15, eight in all, of which template 10 uses the same word for its body leaf and its cockpit leaf (so its cockpit variant draws the body block); (c) an alternate list whose first word is 0xFFFF, followed by the normal list: templates 0 and 1. For (c) every cockpit test that walks the alternate list draws nothing, returns 0 (the walker stores -1) and leaves the part id unchanged: test 9 returns at once, tests 6 to 8 fail the level choice at the leaf 0xFFFF (rule 4 of section 8b). Only a viewed record of template 0 or 1 can meet this, since the player is template 3 or 4 and every other record takes test 3. The earlier reading that no descriptor has such an alternate list, and the count "eight three-item lists plus one alias", were wrong: the eight include the alias, and templates 0 and 1 are the two with 0xFFFF.

**The eye measure E.** `V:4AB30` is the long `V:4AB1E` (the negated camera height, `0x1F45E`: -(y + a body-frame eye offset rotated by
the attitude matrix)) shifted right by 8, so E is about -(y / 256) - 1 to -2 (position units y, 256 pu = 1.25 ft; observed pairs of
y and E: 44,752 and -176; 40,376 and -159; 9,744 and -40; 5,368 and -23; 1,800 and -9). E at or above -32 means y below about 7,700 pu (38 ft). In the
cockpit the player's own entry is at the camera, its LOD `V:4AB70` is 0 (observed), so E = `V:4AB30`.

**The pitch band `V:4A923`.** Its only writer is the pitch-gate routine `0x143F2` (it also latches `V:4A83E`, section 1), run once
per frame in the camera set-up. Let v be the signed word (pitch long `V:4AB40` / 8) in 0.1 degree; the cockpit value of `V:4AB40`
equals the player's record word `+0x66` (positive is nose down; the nose-up angles appear as 3600 - angle). In an external view v is
the external camera's pitch word `V:4AB4C` / 8, but there test 2 applies and the band is not used.

| v (0.1 degree) | `V:4A923` | meaning | status |
| --- | --- | --- | --- |
| up to 100, including all negative values | 0 | level flight and shallow dives to 10 degrees nose down | V (edge 100/101 read-only, bracketed by the samples; ground at rest, E = -13: value 0, id `0xFF`) |
| 101 to 300 | 1 | 10 to 30 degrees nose down | V (v = 122, 250, 292; edges 100/101 and 300/301 R, bracketed by 13 to 122 and 292 to 313) |
| 301 to 600 and 3150 to 3199 | 2 | 30 to 60 degrees nose down; 40 to 45 degrees nose up | V (v = 313; edge 600/601 R, bracketed by 313 and 624); 3150 to 3199 R |
| 601 to 3149 (the gate: `V:4A83E` set) | 3 | set when the gate turns on, kept while it stays on | V (v = 624 to 710: value 3, id 0 at E = -1,964 to -2,399, far above 38 ft) |
| 3200 and above | 0 | nose up by less than 40 degrees (v is 3600 minus the angle) | R |

So in cockpit flight the player's body, and with it the live ground test, appears only when the nose is more than 10 degrees down and
the record altitude (the player record's y, not the camera height) is below about 38 ft, which is the eye measure E at or above -32, or whenever the nose is more than 60 degrees down or more than 45 degrees up. Level flight, flares
and the roll-out use the cockpit block (id `0xFF`) at any height, so the three-vertex test is off for them (observed on the runway
at rest, E = -13, band 0: `0xFF`).

**The levels of the normal list, F/A-18 (template 4, model base flat `0x39508`).** A level is chosen when the scaled distance (P1: the entry's distance word
`V:4ABF8`, units of 16 u divided by 2^LOD, scaled by the zoom `V:4AAFA` / 0x80 unless `V:4A895` is set) is below the node's
threshold masked to 0x3FFF and shifted right by the LOD, taking the nodes in the order of the list; the leaf word names the block (low 12 bits, offset
from the model base).

| distance word below | block offset | part id | note |
| --- | --- | --- | --- |
| 18 | `0x6FC` | `0x00` | the body; the same block serves the next level |
| 48 | `0x6FC` | `0x00` | |
| 78 | `0xA0C` | `0x01` | a coarser body; low nibble not 0: ground test off |
| 288 | `0xA52` | `0x02` | coarser still: ground test off |
| beyond (last-resort leaf) | `0xA7A` | `0xFF` | negative: ground test off |

The other aircraft templates have the same shape (templates 3, 6, 7: id 0 below 23 or 16 and again to 40, `0x21` to 104, `0x32` to 384, then `0xFF`; 5, 14, 15: 0 up to 1,280
then `0xFF`; 10 and 8, 9, 12: all `0xFF`, 8, 9 and 12 with "nothing" as the last resort). In the ring external views the player's entry
distance word was 3 to 44 (all eight directions and the farthest distance level, 12 samples), always below 48: id 0. Tracking
cameras (camera mode `V:4A966` 2 or higher, `04`) can stand farther away; there ids 1, 2 or `0xFF` are possible and the ground test is
off (R). The same selection gives every other active aircraft record its part id from its own queue-C entry's distance word.

**Stale values.** The byte keeps its last value whenever the entity is not drawn in a pass.

| situation | `+0x7B` | status |
| --- | --- | --- |
| record never drawn since the machine was loaded (all inactive records, the player before its first draw) | 0 (inactive records read 0 in all four snapshots) | V for the inactive records, R for the player |
| record inactive (bit 6 of `+0x00` clear), not in queue C and not in the direct list, in a sub-cell the tile pattern does not list (section 6), secondary entry, the drawer culls with a return before the store (a `0xFFFF` leaf, the alternate-frame skip `V:4A856`, which no code ever sets, the pre-test) | unchanged | R |
| the whole entity pass skipped (in-flight flag zero, or the altitude skip of `04` step 6) | unchanged | R |
| mission restart (the position and attitude reset at one pass) | unchanged until the first draw of the new flight: observed `0x00` for 11 blanks, then `0xFF` | V |

At the restart the first draw of the new flight (cockpit, band 0: `0xFF`) came 4 blanks before the first step of the new flight, so
that step already sees `0xFF`. Whether the very first step of a fresh mission start, before any draw, sees the zero of the cleared record
(which would switch the ground test on for that one step) was not captured: open question 11.

**Timing.** In a frame the world update (`0x1F54E` calling `0x26AF8`, with the flight step and collision of `05`) runs before the camera
set-up and the pitch gate, which run before the entity pass (`04`, steps 3 and 9). So the collision of frame N reads the part id stored
by the draw of frame N-1, chosen with the band and eye measure of the camera set-up of frame N-1, which saw the position of the step
N-1. The stall run shows it: the step that brought y to 5,368 (E = -23) is followed in the same frame by the draw that turns the byte to
`0x00`, and the next step (y = 1,800) is the touchdown with result `0x10` (`W0` `0x93C8`); in the steep dive the band was already 3 at the first sample after the step and the byte turned `0x00` 3 blanks later, before the
next step changed the pitch word.

**Other reader.** The projectile hit test `0x2B4BA` (called from the sub-stepped shell motion at `0x2B1B0`-`0x2B228`) selects the target's
collision hull from the table `0x3DACC` (three entries, for part ids 0, 1 and 2, by the low nibble), reduces the hull scale by the high
nibble, and reports no hit when `+0x7B` is negative. A hull is therefore never tested against a record whose part id is `0xFF`
(the cockpit block, or a far level). Read only; the rules of shots belong to `07`.

**What the core needs.** To compute the byte each frame after the step, the world module needs the view state (`V:4A83D`,
`V:4A85F`, `V:4A8ED`), the player's pitch word `+0x66` with the gate latch `V:4A83E` (to derive `V:4A923`), the eye measure E (the
camera height and the attitude-rotated eye offset), and, for external views and other records, the entry's distance word and LOD;
it keeps the previous value for any record not drawn in that frame. Inputs also needed: the viewed record `V:4A996` against `V:4AA6E` (test 3) and the **template / alternate-list shape** of the descriptor (test 5: templates whose alternate list is their normal list always choose by distance), besides the view state, band, eye measure and distance word. The ejection seat spawn (`05` section 17.10) copies the player's record, including `+0x7B`, into the first free record of 1 to 3, so the copy starts with the player's current part id until its own first draw.

**The player's cockpit block and the slot count.** The cockpit block (`0x39BE2`, part id `0xFFFF`, four vertices; b8 = b9 = 4) stores its **own** vertices under the same rules, and with the trailing data slot (note 07 8.10: b8 = b9 stores b8 + 1) that is **5 slots**.

**The parachute's lists (model 11, base `0x418DE`)** [R: read from the image; the program lists decoded to their ops]. The handler `0x26A3E` chooses the list by the counter `+0x4C`: 5 or more `0x418DE`; 3 and 4 `0x418FE`; 1 and 2 `0x4190C`; 0 with the low nibble of `+0x7C` non-zero `0x418DA`; below 0, or 0 with that nibble 0, **`0x4191A`**. Each list is a leaf (the block: `0x41B62` for `0x418DA`, `0x41A36` for `0x418DE`, `0x41A9A` for `0x418FE`, `0x41AFE` for `0x4190C` and `0x4191A`; b8 = b9 = 15 or 16, flag 1) followed by its stream. `0x418DA`: one draw item `0xC154` naming the program list at `0x41A32` (the programs `0x41C04`, starting with op 69, and `0x41F94`, starting with op 13). `0x418DE`, `0x418FE`, `0x4190C`: a three-vertex node (`0x0012 0x0080`, children at `+0x40` and `+0xA4`); the child at `+0xA4` is the draw item `0xC136` whose program list starts at **`0x41C26`: the program [op 56, op 65]**, then `0x41C2A` (op 4), five op-3 programs `0x41BCC`..`0x41BFA`, `0x41EEC` (op 13) and `0x41C44` (op 55). **`0x4191A`**: leaf `0xC220` (block `0x41AFE`), then the single draw item `0xC148` (bit 14: the stream ends; bit 13 clear: a program-reference list at `0x41A26`) with six references: **`0x41C28`** (the program [op 65], one word after the `0x41C26` program), then the five op-3 programs `0x41BCC`, `0x41BD8`, `0x41BE4`, `0x41BF0`, `0x41BFA`. So the list `0x4191A` **starts at op 65 and its programs contain no op 56**: the first program of the other lists begins one word earlier, with op 56. An extractor that exports this level starts at `0x41C28`, as the data say. If the rig sees op 56's record copy applied for this list, the slots it changes are those left by an earlier draw of another phase of the same record (the record copy writes only when its op runs; the slots stay); the harness should clear the slot array before the draw to tell the two apart.

**The near body and the slot store** (note 07 8.10) [V: `test_slots_diff.c` near-body case for models 3 and 4, 600 cases each, 3,300 in the run, 0 differences]. The prelude `0x21C44` copies the descriptor's alternate list (`V:4AAF2`, descriptor `+12`) into `V:4AAEE` and, for the pitch band 3, enters the common drawer on that list; the near body block is the same block as the nearest normal level, so the vertex stage **stores that block's vertices under the normal store rules** (block flag bit 0, not a dying record with a counter of 12 or less) and the program's record-copy ops (42 or 59) run on them.

**What the player's own entry shows in a cockpit view** (verified in the rig with the deck and airborne states and the emulator). Test 9 (cockpit block, id 0xFF; levels: band 0, or bands 1 and 2 with E below -32, which is a record altitude y above about 7,700 pu or 38 ft; the eye measure E is taken at the camera, which sits an eye offset from y): **nothing**. The cockpit block of the F/A-18 (`0x39BE2`, part id 0xFFFF, four vertices) has the single stream word 0xFFFF, so no program runs: the drawer reaches the part id store, returns 0, the walker stores -1 (the emulator's -1 for the deck player), and no polygon, line or fill is produced (rig: 0 clipper, 0 line and 0 filler calls). The cockpit view's picture is the HUD and panel layer plus the world. Tests 6 to 8 (the whole alternate list): the **near body** program (op 59 or op 42 vertex synthesis and op 4, detail faces in pen 0, no gear, flap or damage selector); the faces that reach the screen depend on the state: with the logged deck state (band 3, or the pass after an external view in bands 1 to 3; bands 1 and 2 otherwise do not walk the near body at all, E = -125 being below -32; `nearbody_check.py`) the rig draws two thin triangles of three vertices at the bottom centre, screen vertices (159,166), (159,179), (158,179) and (159,166), (156,179), (162,179), pen 0, under the instrument panel, which starts near row 145; the airborne state draws none; the emulator frames in band 3 with 167 rows (view codes 12 and 13 on the deck, `nearbody-167.txt`) show nothing of it, and the cockpit's 144-row window rejects both triangles (an earlier count of five specks came from another state); result +1 and part id 0. Tests 2 to 5 (external view, head codes 3 to 9, other records): the full normal list by distance (rig: 18 clips and 10 lines on the deck for a head view; the emulator rear view shows the fins and wings); it reads the state words of gear and flaps (`V:4A8FF`, record `+0x7C`, `+2` bit 3, `V:4A992`). The frontend may therefore keep skipping the draw of the cockpit and near-body variants (invisible), **but the core must still produce their effects**: part id 0xFF or 0, and walker result -1 or +1 (nothing else reads the result). The scene must expose for the player's own entry a **variant** chosen in the core from the rules of this section: *nothing* (cockpit block), *near body* (invisible under the panel) or *normal list by distance* (head views 3 to 9, external views, other records), and for the last the distance word and the model state words. A further gate in the op 4 handler (`0x238C6`, read only) decides whether the near-body faces are drawn at all: in the viewed record's own cockpit they are drawn only in band 3, or in bands 1 and 2 when the (adjusted) height is at or above -0x2000 (about 40 ft; a record with both bits 6 and 7 of byte `+4`, such as the deck player with `+0x4E` = 112, has its height reduced by that term first: -3,080 instead of -31,752, so the gate is open in bands 1 to 3 whenever the near body is walked, note 03); the handler returns 1 **in both cases**, so the walker's +1 and the part id do not depend on the gate, only the specks under the panel do (the exits of the gate, for the viewed record and for others, are rule P7 of section 8b, which names this as the exception to "a handler returns 1 only when it drew").

### 8d. The entity tail: the dying burst and the trail

After the program of an **entity** item (item flag bit 4 of `V:4A913`) has been drawn, the tail of the plain drawer (`0x2273C` to `0x227D6`) draws, in this order: (1) the **dying burst** (`0x31766`) when the record's dying bit `0x0400` is set and its counter `+0x4C` is 0 to 15 (twelve triangles, or discs for the cruise missile, flying outwards with the age 15 - counter; rules and tables in note 07 section 11.4); (2) the **trail** (`0x25804`) when the record is the followed record `V:54E72` and its trail length `+0x3D` is above 0 (chained discs along the last stored positions; note 07 section 13.2). The tail is reached when the drawer's walk has run to its end (`0x22612`, `0x226FC`); the exits that return earlier (`0x22692`, an abandoned program) skip it. Both draw with the same primitives as the model programs (the polygon filler `0x34A28` and the disc routine `0x33CAC`), after the model, so they lie over it in the painter's order of the entity pass. A secondary-object item (flag bit 6) returns before both. The result word of the tail is the OR of the primitive results and feeds the item's visibility result like a program's.

### 9. What the port must do (replaces `world_range`)

1. Delete the square range window and the altitude-to-range table as a culling rule. `range.bin` stays extracted because the
   original's walker reads the 64-byte table at `0x4621C` (indexed beyond its end) into `V:4AA6A`, but nothing uses the value.
2. Extract the tables of Constants (all are plain bytes or words in the image; pattern blobs, permutation tables, LOD table and
   hypotenuse table and the two pre-test tables are extractor outputs) and implement: the view octant, the scan inputs (phase, sub-cell, cells) for the
   cockpit and the external camera, the request byte with the triggers of section 1 (and the tier selection by bit), the two-pass
   tile scan with the per-tier patterns, the position and LOD arithmetic, the generation limit and walked window, the entity
   insertion of tier C, the direct list, the landmark slots, and **the per-frame refinements** (run sort one run per frame, landmark
   reorder).
3. **The scene carries the original's single draw order**, not "terrain then objects": queue A (walked window), queue B (walked
   window), the near-ground pass marker (stage [90]), the direct entities, then queue C with its entity and secondary entries
   interleaved where the queue puts them. Each scene entry must carry: the item id (descriptor index), the LOD shift, the
   cell-relative position (the three words, or u with the LOD; entity entries carry the record index and are placed from the
   record), the descriptor class (the routine: plain `0x21C40`, `0x21C34`, `0x21D0C`, type B `0x22CFC`/`0x22D0A`, entity), the
   tier (for the ground-height limits), and the walk state of the entry: the distance word `+0x10`, the countdown `+0x12`,
   the draw counter `+0x13` and the result `+0x14`. The design of the scene struct is left to the code task.
4. Capacities: one queue holds 69 entries, so `GAME_MAX_TERRAIN` should be at least 3 * 69 = **207**; entities number at most 16
   plus 16 secondary objects, so `GAME_MAX_OBJECTS` is **32** at least. The finite maxima of generated entries are 71 (A, gate
   set), 33 (B) and 27 (C); the observed maxima over 300 random positions at height were 54, 25 and 19.
5. Rebuild the lists **on the same triggers** (section 1) and nowhere else; between rebuilds the original keeps the old entries,
   including the LOD shifts decided at build time and the old cell-relative positions. But the original does run, every frame, the
   sort of one run of tier C, the landmark reorder, and the walker's distance refresh and invisible-entry rules; a deterministic
   golden needs those, not a per-frame re-evaluation of visibility.
6. Apply the walk rules of section 8 (height limit for the plain model descriptors, refresh cadence of the distance word with the
   `0x21C34` exemption of queues A and B, the re-test period for invisible entries) and the drawer-result rules of section 8b (the exits
   1 to 9, the quantities P1 to P7, the entity prelude E1 to E4, and the primitive layer of section 8c with the handler table of note 03) when it draws, not a range compare.
7. The corrected assumptions that `src/core/world.c` and the extractor must change: item offsets relative to the sub-cell named by
   the nibble with the unit 1 u (C) or 4 u (A, B); rows are z cells, keys are x cells; entity insertion in tier C only.

### 10. Worked examples

Both snapshot examples give the first entries of each queue as: id, LOD, X, Z (words), key; the emulator and the rig agree on every
field (airborne: after the camera adjustment described in Verified by).

**Carrier deck** (`cockpit-carrier.vasnap`: record 0 cell x 70, z 65, sub-cell 10, height 30,472 pu = 148 ft, `V:4A908` = 9,
`V:4A909` = 10, `V:4A90C` = 0, `V:4A83E` = 0; limit 30; no tier A entry because the 200 ft low-altitude pattern finds no cell):

| tier | count | first entries |
| --- | --- | --- |
| A | 0 | none |
| B | 9 | id 116, LOD 3, X -9216, Z 7168, key `0x001E4542`; id 114, LOD 3, X -10752, Z 2560, key `0x00174541`; id 90, LOD 3, X -11264, Z 1024, key `0x001B4541` |
| C | 2 | entity 14 (the carrier), LOD 0, X 4096, Z 4096, key `0x8E000000`; entity 0 (the player), same position, key `0x80000000` |

**Airborne** (`airborne.vasnap`: record 0 cell x 65, z 66, sub-cell 3, height 562,800 pu = 2,750 ft, `V:4A908` = 6, `V:4A909` = 3,
`V:4A90C` = 0; D = 1, limit 30, so the first 30 of the 60 tier A entries are skipped and the walk starts at index 30):

| tier | count | first entries |
| --- | --- | --- |
| A | 60 | index 0-2 (skipped): id 21, LOD 3, X -4352, Z 8960, key `0x00154043`; id 70, LOD 3, X -2304, Z 8960, key `0x00144043`; id 62, LOD 2, X -8397, Z 10569, key `0x001D4043`. First walked, index 30: id 34, LOD 3, X 272, Z 8304, key `0x00764143` (index 29 is id 72, last index 59 is id 53) |
| B | 22 | id 109, LOD 2, X -9216, Z 13312, key `0x00194043`; id 110, LOD 2, X -10240, Z 10240, key `0x001D4043`; id 108, LOD 2, X -6144, Z 14336, key `0x00184043` |
| C | 6 | id 45, LOD 1, X -8320, Z -1920, key `0x00103F41`; id 45, LOD 1, X -8320, Z -1728, key `0x00303F41`; id 42, LOD 1, X 7168, Z 15360, key `0x00104143` |

The key's low word is the tile in C cell numbers (hex): `0x4542` is x 69, z 66, the tile (-1, +1) of the deck example; the high
word is 16 * (item number in the slot) + slot.

**One entry from the table to the queue** (airborne tier C, first entry): the player's C cell is x 65, z 66; the tile is
(x 63, z 65), offset (-2, -1) (label `0x3F41`); table C row z 65 holds the single cell x 63, whose item list at `0x47A4E` begins
with sub-cell 0, id 45, offsets (3840, 256). Sub-cell 0 has x index 3 and z index 3 (3 - 0), so X = 16384 * (-2) + 4096 * 3 + 3840 =
-16640 u and Z = 16384 * (-1) + 4096 * 3 + 256 = -3840 u; at LOD 1 the words are -8320 and -1920, exactly the emulator's entry. The
second item of the slot (offsets 3840, 640) gives Z = -3456 u, word -1728, also exact, with the key `0x00303F41`: the high word is
the accumulated 0x10 | 0x20 (item 2) and slot 0 (the third list entry at `0x47A4E` is in sub-cell 4).

## Constants

Tables marked *extractor output* are read from the image by the asset extractor (address, size, shape and two or three samples
are given; their contents are not reproduced).

| item | value | address |
| --- | --- | --- |
| queue buffers | A `0x53A4A`, B `0x540DA`, C `0x5476A`; 70 slots of 24 bytes; 69 kept | h73 |
| walked-window offsets | `V:4AA64` (A), `V:4AA66` (B), `V:4AA68` (C): 24 * max(0, generated - limit) | h71 |
| limits by band | four pairs (without/with `V:4A83E`) for D = 0..3, rising with D up to the immediate; 70 for D of 4 and more (immediate in the code); the eight words are disk data and are not listed (`tables/lists_limit.bin`, FNV-1a-32 `88ff09cd`) | `0x21022` (8 words); immediate at `0x20FE8` |
| band size | -`V:4AB30` / 2048 u = 524,288 pu = 2,560 ft | `0x20FD4` |
| slots per tile / items per slot / tiles per pattern | 16 / 16 / at most 14 | `0x20304`, `0x2001C` |
| tile offsets | 21 pairs of signed bytes (dx, dz), listed in section 3 | `0x20674` |
| cell origin (A, B) | 21 pairs of words, 16384 * (dx, dz) (the cell size in units of 4 u; extractor output, 84 bytes). **The product wraps in 16 bits: the word for +2 is -32768, not +32768**, so the position term of a tile two cells in +x or +z is 4 * -32768 = -131,072 u, not +131,072 u (262,144 u lower; section 3, rig-found correction); the words for -2, -1, 0, +1 are exact (-32768, -16384, 0, 16384). The cell fetch uses the offsets of `0x20674`, so only the position is affected | `0x2069E` |
| cell origin (C) | 21 pairs of words, 4096 * (dx, dz) (the cell size in units of 4 u; extractor output, 84 bytes) | `0x20732` |
| sub-cell origin and phase offset (A, B) | 16 pairs of words, 4096 * (3 - n mod 4, 3 - n div 4) in units of 4 u; indexed also by the phase (A, B) | `0x206F2` |
| sub-cell origin (C) | 16 pairs of words, 1024 * (3 - n mod 4, 3 - n div 4) in units of 4 u | `0x20786` |
| quadrant class of a phase / sub-cell | 16 bytes, values 0..3 (extractor output; samples: entries 0-3 are 0,0,1,1 and entries 8-11 are 2,2,3,3) | `0x4625C` |
| octant permutations | 4 rows of 8 bytes (row 0 is 0..7, row 1 is 7..0, row 2 starts 3,2,1,0, row 3 starts 4,5,6,7) | `0x4626C` |
| pattern group of a phase | 16 bytes, values 0..3 (entries 0-3 are 0,1,1,0) | `0x4628C` |
| sub-cell draw-order permutation | 4 rows of 16 bytes (row 0 is the identity, row 1 starts 3,2,1,0,7,6,5,4) | `0x4629C` |
| tile permutation | 4 rows of 24 bytes (21 used; row 0 is the identity, row 1 starts 2,1,0,5,4,3,8,7,6) | `0x462DC` |
| pattern blobs | `0x4633A` (610 bytes, 4-word table), `0x4659C` (3042 bytes, 32-word table), `0x4717E` (502 bytes, 64-word table) in h65; format and samples in section 3 | h65 |
| LOD table | 240 bytes, non-decreasing 0..15, 16 runs (extractor output); samples: index 0 = 0, index 1 = 1, index 12 = 4, index 239 = 15 | `0x20E4A` |
| hypotenuse table | 257 words, Q14, T(r) = round(16384 * sqrt(1 + (r/256)^2)); samples T(0) = 16384, T(128) = 18318, T(256) = 23170 | `0x208E8` |
| cell tables | A `0x47374`, B `0x47474`, C `0x47574` (`02`) | h66 |
| occupancy maps | 128 rows of 16 bytes per tier (bit = a cell exists); a speed-up, no effect on the result | `0x1B74C`, `0x1BF4C`, `0x1C74C` |
| LOD walker thresholds | 0x100 and 0x400 (scaled distance word; 5,120 ft and 20,480 ft) | `0x1FB06`, `0x1FB0E` |
| plain-model height limits | 0x380000 (A, B), 0x400000 (C) pu | `0x1FACE`, `0x1FD8C` |
| `0x21C34` cut-off | V:4AB1E below -0x280000 pu (12,800 ft) draws; at or above it returns 0 | `0x21C34` |
| type B cut-offs | the routine returns 0 when V:4AB1E is below -0x380000 pu (camera above 17,920 ft) for `0x22CFC`, below -0x800000 pu (above 40,960 ft) for `0x22D0A` | `0x22CFC`, `0x22D0A` |
| pre-test run limit | V:4AB30 above -0x140 u (400 ft); also `V:4A924` zero, flag byte bits 0x42 | `0x21E9A`, `0x23F56` |
| pre-test table choice | first table when V:4AB30 is above -0x80 u (160 ft), else the second | `0x23FC0` |
| pre-test minimum tables | two tables of 12 words (24 bytes each), indexed by the scaled distance shifted right by 4, at most 11; extractor outputs, non-decreasing, entry 0 = 0; samples: first table entry 3 = 0x100 and entry 11 = 0xF80, second table entry 3 = 0x180 and entry 11 = 0x1A00 (the second follows the first directly) | `0x23FF4`, `0x2400C` |
| flat cull | V:4AB30 at or above -0x20 u (40 ft); limits 0x60, or 0x170 with block flag bit 2, shifted right by the LOD | `0x22242` |
| line-op height gates | ops 74 and 75 draw only when `V:4AB30` is below -0xC0 u (camera above 240 ft); ops 9 and 10 draw only when it is at or above -0x80 u (at most 160 ft) | `0x236C4`, `0x249B0`, `0x24BB2`, `0x24C22` |
| op 4 gate heights | V:4AB1E: -0x2000 (40 ft), -0x10000 (320 ft), -0xA0000 (3,200 ft), -0x100000 (5,120 ft: returns -1); first distance word 0xA00 times 256 (3,200 ft) | `0x238C6`-`0x23A28` |
| type B level search | first entry with (unmasked threshold shifted right by the LOD) at least the scaled distance; the last entry of models 39-42, 99, 100 names itself (threshold 0x7FFF) | `0x22D52`-`0x22D70` |
| near-ground pass limits | camera height -V:4AB30 at most 2,048 u (band 0), 3,456 u (band 1), 4,608 u (bands 2 and 3) | `0x2C116`, `0x2C120`, `0x2C128` (routine `0x2C10E`) |
| normaliser | length argument 0x100 in the pre-test; scale a power of four, quotient at most 1024 | `0x29C0E` |
| view octant boundaries | 3600 * k, k = 1..7 (angle units 1/80 degree: 45 degrees) | `0x299AC` |
| pitch gate | 600 < v < 3150 (v = pitch / 8, 0.1 degree, positive nose down) | `0x14428` |
| pitch band `V:4A923` | v <= 100: 0; <= 300: 1; <= 600 or in 3150..3199: 2; gate (600 < v < 3150): 3; >= 3200: 0 | `0x14484`-`0x144D8` |
| cockpit eye test | E = `V:4AB30` shifted right by the LOD; E at or above -32 draws the body when `V:4A923` is 1 or 2 | `0x21CBA` |
| part ids, F/A-18 | body `0x00` (blocks `0x6FC`), `0x01` (`0xA0C`), `0x02` (`0xA52`), far `0xFF` (`0xA7A`), cockpit block `0xFF` (`0x6DA`); thresholds 18, 48, 78, 288 | model base `0x39508` (template 4, `0x25F1C`) |
| hit-hull table | 3 pointers for part ids 0, 1, 2 | `0x3DACC` |
| height triggers | 0xA000 pu (200 ft); 2^21 pu (10,240 ft) | `0x1F570`, `0x1F5A4` |
| re-test period | 5, 3 or 1 | `0x1F706` |
| run sort | 22 entries per run | `0x21270` |
| landmark reorder | window 0xC00 u before and 0xA00 u after the reference | `0x21444` |
| landmark slots | 11 pointers `0x5283C`, records of 0x100 bytes from `0x52868` | h73 |

### Extractor needs

Extractor outputs (verbatim from the image, validated, with the samples listed in the table above and in section 3; contents never committed): the three pattern blobs with their offset tables, the five permutation and class tables (`0x4625C`, `0x4626C`, `0x4628C`, `0x4629C`, `0x462DC`), the tile offsets `0x20674`, the four origin tables (`0x2069E`, `0x20732`, `0x206F2`, `0x20786`), the LOD table `0x20E4A`, the hypotenuse table `0x208E8`, the limit words `0x21022`, the cell tables (`02`) and the descriptor table `0x2600C`. **Exported since:** the two pre-test minimum tables at `0x23FF4` and `0x2400C` (12 words each, big-endian; extractor output `lists_vis.bin`, version 6, commit 655cd1b; the extractor test compares the file with the image and commits no table words, policy commit a923cfa). The model tree walk of the primitive layer (`02` section 3, the op table `0x23408`) and the block set-up of note 03 (second vertex section of the flat blocks with flag bit 2, strips, extra vertices, **the zero vertices of the type B patches as ordinary data**) are needed by the core for rule 9 of section 8b; the extractor must stop reporting the models with a second section (46, 72, 73, 95) and the type B patches 105, 106, 109-132 as blocked. **Still needed (the models 0, 2, 3, 4, 6, 7, 10, 11, 13 are "partial" until then):** (a) the synthesis of the entity models in word-exact form: the tables of note 03 for ops 41-44 and 59 (every slot, including the ones the older tables called non-linear), the fixed-formula ops 38, 45, 46, 47, 56, 63, 64, 65 (no operand, not selectors; models 0, 2, 10, 11, 13 are blocked on 38, 45, 56, 63, 64, 65) and the corrected shift rule of ops 34 and 36 for a negative s, all in 16-bit word arithmetic (note 03, "Vertex synthesis" and the tables after it, `synth_note_check.py`, `synth2_check.py`); (b) the player's cockpit-alternate list as two extra levels (the near body level with its program of op 59 or 42 and op 4, and the cockpit level), format and location in `02` section 2.5, so that the core can run the near-body program (op 4's gate and its +1/-1), the tests of section 8a choose between the lists, and the part id and the walker result are produced.

## Open questions

1. The camera and view-mode writer sites of the request (23 sites in section 1) are classified from the key handlers around them,
   not run. What is certain from the code (read-only): every one of the 23 stores the full value `0xFF` without a condition (a plain
   store, not a bit mask), so the value is never in doubt, only whether and when each site executes. **Capture method.** The request
   byte can be caught by a peek at every blank, which the stall runs of note 01b did (`V:4A910` peeked after each of 1500 blanks): the
   builder does not clear the byte in one stroke but bit by bit as it builds the tiers (the stall runs show the sequence `0xFF`, `0xFE`,
   `0xF0`, then zero, one value per blank, at each of the cell changes, and at the mission restart `0xFF`, `0xFC`; the tier bits go first
   and the high nibble stays until the last clear), and a frame lasts several blanks, so a stored `0xFF` stays visible for two to three
   consecutive blanks before it reaches zero. The first value can still fall between two peeks (two of the cell changes of the first run, at
   blanks 43 and 453, show only `0xFE` and `0xF0`; the second run shows that for the change at blank 819 only), and the rest of the sequence tells it apart from a `0x0B` or `0x0C` request, which does not have
   the high nibble `0xF`. So the plan is simple: one script per key (direction keys, distance keys, chase, fixed, relaunch, cockpit/external), with the
   request byte, the trigger inputs `V:4A90C`, `V:4A90D`, `V:4AB26`, `V:4A83E` and the cell words peeked at **every blank** around the press;
   a request whose high nibble is set, appearing in the blanks after the press while none of the other triggers (octant, 200 ft, band,
   gate, cell crossing) changed, is the key's store. The earlier plan, a side effect of the rebuild, is only the fallback for a peek that
   misses the whole sequence: the counter `+0x13` of the walked entries, which a rebuild restarts from 0 so that it reads 255 after the first
   walk (section 4), the offsets `V:4AA64`/`V:4AA66`, and the cursor `V:4A915`, which a rebuild reloads to `V:4A914`, out of its countdown
   sequence (section 5a). Label the sites by which keys rebuild. Not done in this round (the rig cannot run the key handlers without the
   rest of the frame state, and the 23 fragments are not independent).
2. The role of the secondary-object table (`0x4D23C`) and its draw position (`0x1FFB4`); belongs to `06`. The entity and
   secondary-object paths of the landmark reorder (`0x21AC4`, `0x2166A`) were read from the disassembly only.
3. The post-queue loop of `0x1FBCC` (every 4th frame, for each other active record: call `0x29D3A` with the distance vector):
   looks like the radar/target distance update, not drawing; unverified.
4. `V:4A91E` is tested in the builder but no code writes it by absolute address; treated as 0 (it is 0 in every capture).
5. The case of more than 16 items in one slot (trap) and the sort of runs longer than 22 were read from the code only; no table
   has such a slot (the data maximum is 16 per slot).
6. Whether the camera heading word used for `V:4A90C` in the cockpit (`V:4AB46`) is the camera yaw including the head-turn offset
   is inferred from the captures (KP4 gives octant 7, KP2 gives 4), not from the writer of `V:4AB44`. The external-view camera
   offsets (`0x1F1D8`) are not decoded.
7. The primitive layer of the drawer result is specified (note 03 table, section 8c) and its combination is verified against the rig; what remains: the Python model `visibility.py` still stops at the first operation of a program (the rig decides those entries; extending it would need a Python clipper and adds no rule); the negative-word abort of the type B drawer (no shipped operation of the type B models can return a negative word: code only); and the pixels of the blitter fill and line routines, which the rig cannot run (compare with an emulator frame of a flat block and a type B patch). The integer boundary cases of the line clipper are settled: the plane cascade, the closed/open tests and the intersection rounding are specified and equal to the rig on 100,000 segments (note 03, "The clipped line routine", `line_model.py`).
8. The exact frame of the airborne snapshot relative to its last rebuild is unknown (only a window is known, Verified by).
9. Collision data with a negative first word (a second plane format) is unreachable for the reference models in the data.
10. Entities in A/B cell numbers: none; settled (insertion is tier C only).
11. The part id (section 8a) at the very first step of a fresh mission start: the record is cleared, no draw has happened, and the
    zero would switch the ground test on for that step. Method: restore a snapshot taken before the first flight frame (the code
    prompt or the location select), start the flight with the script of `freeflight-takeoff.txt` frozen and stepped one blank at a
    time, and peek `+0x7B`, `V:4A992` and `W0` of record 0 per blank. Approach-camera distances (ids 1, 2, `0xFF` for the player)
    were not run, and the exact boundary E = -32 (read: at or above draws the body) lies between the samples -40 and -23.
12. The other readers of `V:4A923` are decoded: `0x2C10E` is the near-ground threshold (section 8) and `0x23926`-`0x2396A` is a gate in the op 4 handler (section 8a, rule P7 of section 8b); the gate is decoded and run on both sides of every boundary (rule P7); what record byte `+4` bits 6 and 7 mean outside this test is not decoded. Stake: the handler changes what a band-0 to 2 cockpit shows (faces vs nothing), the near-ground rule changes whether the lattice is drawn.

## Verified by

Status: **V** = verified (rig compared with the emulator, or rig compared with an independent model on random states), **R** =
rig run of the rule without an emulator comparison, **C** = read from the code only. Reproduce: `make -C tools/m68k-rig test-lists`
(deck and airborne comparison of the queues), `make -C tools/m68k-rig test-lists-scan` (the scan model against the rig's builder on random
states, `random_check.py`) and `make -C tools/m68k-rig test-lists-walk` (the walkers and the drawer result, `walk_check.py`, section 8b; its mode `primitives` is the primitive layer of section 8c, `python3 walk_check.py primitives SEED SCALE`, scale 1000 being the size cited there);
all three skip cleanly when `extracted/` or `librig.so` is absent; the capture script is
`tools/vamiga-ref/scripts/openers-lists.txt` (run: `build/vamiga-ref/ref --rom disks/kick12.rom --adf extracted/interceptor.adf
--script tools/vamiga-ref/scripts/openers-lists.txt --log extracted/captures/logs/lists.txt`). The script restores the snapshots
`extracted/captures/cockpit-carrier.vasnap` (deck) and `extracted/captures/airborne.vasnap`, and logs per scene the labels
`s71_` (hunk 71 from `0x4A700`, 2336 bytes), `recs_` (`0x4B23C`, 8192), `sec_` (`0x4D23C`, 512), `act_` (`0x53A2A`, 64) and `q_`
(`0x53A4A`, 5040). The test loads `s71_`, `recs_` and `sec_` into the rig, runs `0x1F31C` (occupancy maps), writes the identity
words 0 to 21 at `0x53870` (the sort's index table), stores the request `0x0B` in `V:4A910`, sets A6 to `0x70000` and runs `0x1F770`;
it compares the three queues entry by entry (id/flags word, X, Y, Z, key). The Python model and the probes live in
`tools/m68k-rig/tests/lists_model/` (they read the user's extracted image and log at run time; `make -C tools/m68k-rig
librig.so` first): `random_check.py SEED N CAMRANGE [landmarks] [active]` is the random comparison with the rig (the make target runs 60 states), `walk_check.py` the walker and drawer-result checks and
`finite_check.py` the capacity check.

| claim | status | evidence |
| --- | --- | --- |
| builder, triggers, scan, emission, limit, walk | C + V | read with rea (Ghidra, `68000:BE:32:default`) at the addresses above; entry `0x26AF8` read and found to be the entity update; queues compared below |
| lists equal the original's | V | deck: A 0, B 9, C 2 entries, **11 of 11 identical** in id, flags, LOD, position, key. Airborne: A 60, B 22, C 6 entries, **86 of 88 identical with no adjustment**; the two others (tier A index 4, id 21, and index 36, id 34) have the same position and key, and differ in the LOD nibble by one (emulator 3, rig 2: the words differ by exactly one shift, for example X 0xF200 at LOD 3 and 0xE400 at LOD 2); for index 36 the emulator's distance word `+0x10` is 395 where the rig has 0 (the walker refreshed it). The cause is that the LOD is derived from the camera offsets at **rebuild** time and the snapshot was taken after some frames of flight |
| the camera adjustment | V (fitted window) | moving the z offset word `V:4AB2E` by +210 to +1580 u (tested in steps of 10) gives 88 of 88; no single move of `V:4AB2A` or `V:4AB30` does. The lower bound is the LOD flip of index 4, the upper bound the flip of index 35. The earlier single figure +1500 u was a point inside this window, not a measured parameter. Corroboration: the player's velocity words (x -6872, y 8556, z 12824 pu per step, that is -26.8, +33.4, +50.1 u per step) move the offsets by velocity * N after N steps; N = 5 to 31 steps back gives 88 of 88 (N = 0 to 4 and the opposite sign fail), i.e. the snapshot is 5 to 31 steps after the rebuild, about 0.5 to 5 s at the 6 to 9 steps per second of `01b`. The offsets at the snapshot (x -2371 u, z -14544 u, height -2247 u) are close to minus the position inside the cell (record words `+0x0C`, `+0x0E` = 2377, 14531) |
| the sort cursor `V:4A915` | V (rig) + C | rig, airborne state moved to C cell (66, 69) (N = 5 runs): cursor 5 after the rebuild, then 4, 3, 2, 1, 0, 5, 4, 3 over successive zero-request builder calls (one idle call per cycle); the run index sorted per call is read from `0x2123A`-`0x2139A` |
| tile label of negative sums, landmark reference qualification offsets, plane count, inclusive window bounds | C (+ model) | read from `0x20B76`-`0x20B96`; the reference rule, the plane loop (count + 1) and the bounds are the ones of the Python model, which equals the rig in the 900 + 250 states above |
| independent model of the scan, LOD rule, run sort and landmark reorder | V | the Python model (`tools/m68k-rig/tests/lists_model/`, written from the tables and the rules above) equals the rig for tiers A, B and C in id, order, LOD nibble and position words over **900 random states** (3 seeds, camera offset ranges 200, 3000 and 30000 u; heading octant, phase, sub-cell, cell over the populated region and the whole 128 by 128 grid, height above and below 200 ft, pitch gate on and off, all records inactive), plus 250 states placed within 2 cells of the six table C cells with landmarks (the reorder fired in 74, 45 had a window larger than one entry and it reordered 29) and 300 states with the records active (terrain subsequence compared): 0 differences. The first run of the model (before the landmark reorder was found) had one difference in 900 states, which led to the reorder rule |
| what the model does not cover | - | entity and secondary entries in the run sort and the reorder (terrain only; the entity paths are C), the one-run-per-frame cadence of the sort (the model sorts all runs, as a rebuild does), and states that exceed 69 entries (the cap is checked by the finite check and two rig states); the walker and the drawer exits are covered by `walk_check.py`, see the rows below |
| finite check of capacity | V (model) + R | the model over every origin cell (A, B 31 by 31 with the wrap rule, C 128 by 128), every phase, octant, gate and low-altitude sub-cell: maxima A 64 / 71 (gate) / 16, B 29 / 33, C 25 / 27; only the six tier A gate combinations reach 70 or 71. Rig: C cell (61,69), sub-cell 5, gate set: counter 71, 69 kept, offset 24 at D = 4 and 744 at D = 0; A 64/64, B 33/33 and 29/29, C 26 and 28 (one more than the model: the player entity) |
| request byte and tier selection | R | rig: 0x0B then 0x0C leaves queues A, B and the offsets of A and B unchanged; 0x0B, 0x0F, 0xFF, 0x1B rebuild all three |
| triggers: octant, 200 ft, band, mode 2, gate | R | rig on the airborne state: octant 0 to 3 with the gate clear gives 0x0B and `V:4A90D` = 3, with the gate set gives 0 and `V:4A90D` = 3; crossing 0xA000 both ways gives 0x0B, staying above gives 0; a band change gives 0x0B, 0 in mode 2 with `V:4AB16` still updated; the gate at 0, 30, 59 degrees clear and 61, 90, 180, 270, 314 set, 316 and 330 clear |
| trigger writers beyond the detector | C | the position update, spawn/deactivate sites and the 23 camera sites |
| external camera derivation | R | rig with `0x26AF8` and `0x2D756` stubbed and the camera longs poked: cell (70,65) offsets (2,1) gives sub-cell 9, quadrant 9; cell (127,3) offsets (0,3) gives 3 and 0; a new sub-cell gives `0xFF` in mode 0 and in mode 2 at `V:4AB30` = -0x800, and 0 at -0x2000; a new cell in mode 2 high gives `0xFF` |
| limit table | R | rig: heights D = 0 to 6, gate on and off, skipped counts as in section 3 |
| wrap rule | R | rig with table rows patched: tier C z = 0 (rows 127 and 126), z = 127 (rows 0 and 1), tier A origin 31 with keys 0, 29, 30, 31 |
| sub-cell rule, item position | V | the worked entry of section 10 equals the emulator's entry; the model reproduces all positions |
| distance function | V | `0x20884` against an independent function over 3000 random inputs, no difference; hypotenuse table exact for all 257 entries |
| entity placement, direct list, bit 2 | R | rig sweeps over the deck state (sub-cells), 200 random states (tier C only), record 5 in an unlisted sub-cell, records 14/15 with bit 2 |
| entity draw position | R | `0x1FBCC` run in the rig with the emulator's records and stubbed draw routines: three positions equal the formula; no draw-time values from the emulator |
| landmark slots | R | rig on the airborne state: slots 0 to 4 as in section 7; the slot-order difference after the sort in 3 of 400 states |
| walk rules (distance refresh cadence, invisible-entry countdown, `V:4A974`, `+0x14`, `+0x13`) | V (rig against the model; emulator `+0x13` 247, 248 and `+0x14` -1, 1 consistent) | `make -C tools/m68k-rig test-lists-walk`, modes random (three walks per queue, 400 frames: refresh schedule on `V:4A992`, countdown, `V:4A974` 1, 3 and 5) and constructed entries (previous result -1 or 0): 0 differences in `+0x10`, `+0x12`, `+0x13`, `+0x14`; not run against an emulator trace over several walks |
| the `0x21C34` entries in the walkers | V (rig, model) | mode c34: queues A and B give neither refresh nor countdown (the walker jumps to the counter), queue C refreshes and counts down; the code of both walkers read (the A/B walker tests the routine at `0x1FAF6`, the queue C walker does not) |
| plain-model height limits | V (rig, model) | rule 1(b) of section 8b: the skipped entries of the random frames (mode random) equal the model's; the limits 0x380000 and 0x400000 read at `0x1FACE`, `0x1FD8C` |
| pattern-gate polarity | C | read from the code |
| part id byte `+0x7B`: writer, selection, pitch band, eye test, staleness, timing | V (emulator) + C | `openers-stall-part.txt` (log `stall-part.txt`, per blank: byte, band, E, step counter, `W0`), `openers-partid-bands.txt` and `openers-partid-gate.txt` (logs `partid-bands.txt`, `partid-gate.txt`: bands 1, 1, 2, 3 at pitch words 976, 2336, 2504, 4992, then the byte turning `0x00` inside the gate at E near -2,400, stepped per blank), `openers-partid-external.txt` (log `partid-external.txt`: the external-view run (eight distance levels and four directions: byte `0x00`, the player entry's distance word 3 to 44), the four snapshots (inactive records `0x00`, ground at rest `0xFF` with band 0) and the restart in `stall-records.txt`; the prelude and the drawer read with rea and the Musashi disassembler |
| drawer result rule (section 8b) | V (rules 1b, 1c, 2, 3, 4, 4B, 5, 6, 7, 8, 9; P1 to P5, P7) + R (E1 to E4, P6) + C (rule 1a, the type B abort) | `make -C tools/m68k-rig test-lists-walk` (modes snapshots, random, forced, norm): the rig's walkers equal the emulator's `+0x14` for all 61 entries of the snapshots (one positive); the port-free model `visibility.py` decides 21 of them, 1,909 of 3,739 random-frame entries and 384 of 607 constructed entries with 0 differences; rules 6 and 7 only on the constructed entries (16 and 5); the normaliser is bit-exact over 3,000 inputs; the per-rule counts are in section 8b |
| entity distance refresh (section 8) | V (rig, model) | `make -C tools/m68k-rig test-lists-walk`, mode entities: 900 walks, 2,323 queue C entries (1,718 entity, 605 terrain), 0 differences in `+0x10`, `+0x12`, `+0x13`; secondary objects read-only (none active in the captures, the model handles their path) |
| near-ground pass threshold | V (rig) | `make -C tools/m68k-rig test-lists-walk`, mode ground: `0x2C0EC` called at the limit and one unit either side for each pitch band, the pass flag at `0x2C13A` set exactly at or below 2,048 u (band 0), 3,456 u (band 1), 4,608 u (bands 2 and 3) |
| tile origin +2 in A/B, `+0x12` initial value, first build sorted, slot table not cleared, pending marking, entity key without a height term | V (rig) | 6 corrections of the code round, each re-run (845 entries for `+0x12`, 60 frames for the sort, 1651 keys); `V:4A996` is 0 in all 37 snapshots and 120 demo samples (viewed record not captured as non-zero; the rig builder follows the record named by `V:4A996` in the cockpit branch and the camera in the external branch) |
| cockpit entry of the player | V (rig, emulator) | cockpit block draws nothing (0 clip, line, fill calls; emulator -1); near body: result +1, part id 0, two thin pen 0 triangles under the panel in the deck state and none in the airborne state (rig), section 8a |
| primitive layer (section 8c, note 03) | V (handler returns, combination, vertex table, type B search and cut-offs, the two divergences, op 4, sprites, handedness, gate writers) + R (blitter output) + C (ops 5, 55, 71, the writers of `V:4A895`) | `python3 walk_check.py primitives 1 1000` (`prim_suite.py`): 67,000 direct handler calls over 67 operations; 2,204 one-entry walks (781 tree walks); 945 blocks and 9,261 vertex slots; 400 type B level sweeps; 446 and 146 walks for the two divergences; 1,500 op 4 entries; 562 entity entries; the other scripts as in the table of section 8c: 0 differences |
| not verified | - | the sort of runs longer than 22, the post-queue loop of `0x1FBCC`, the external-view camera offsets; the negative pre-test index, the `V:4A856` skip (dead), the pixels of the blitter routines |
