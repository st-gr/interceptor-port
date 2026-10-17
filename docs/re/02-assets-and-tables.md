# Assets and tables: 3D object models, terrain and landmarks, palette, fonts, data hunks

Scope: where the game keeps its static data (object models, ground and landmark data,
colour tables, glyph tables, lookup tables) and how each table is laid out, so that the
extractor decoders of milestone 2 can be written without looking at the binary. The
projection maths, matrices and the polygon filler belong to `03-3d-pipeline.md`; the
frame order, screen and sky to `04-scene-composition.md`; this note stops at the data.

Conventions. Addresses are in the relocated flat image (base `0x10000`, hunk table in
`00-binary-layout.md`), written `0x...`; "hunk N" is the hunk number of that table.
Multi-byte values are big-endian. "Word" is 16 bits, "long" is 32 bits. A "vertex byte
offset" is always six times the vertex index. Image used for every address and byte
claim: the relocated image from the corrected `tools/hunk/relocate.py` (298988 bytes; relocation fix commit f1ffbb6). The
bytes of hunks 41 to 70, of the record table (hunk 16) and of the palette (hunk 21) are
identical in the old and in the corrected image (compared byte for byte); only code in
hunk 0 and a few other hunks differ. Nothing extracted from the disk is reproduced here
beyond single record values used as worked examples.

## Location

### Tables found

| what | hunk | address range | size | status |
|---|---:|---|---:|---|
| object record table: 134 records of 20 bytes, ids 0 to 133 | 16 | `0x25ECC`-`0x26943` | 2680 | verified |
| run-time copies of records (ids 16 to 32, zero in the file) | 16 | `0x2600C`-`0x2615F` | 340 | verified (written by the spawner) |
| model draw handlers (record field 0) | 10, 12, 16 | `0x21C34`, `0x21C40`, `0x21C44`, `0x21D0C` (hunk 10); `0x22CFC`, `0x22D0A` (hunk 12); `0x26944`, `0x2699E`, `0x26A3E` (hunk 16) | | read from code |
| operation jump table for model programs: 78 longs | 13 | `0x23408`-`0x2353F` | 312 | verified |
| model data, ids 0 to 15, 33 to 35, 43 to 97, 102 to 104, 107, 108, 133 | 41-58, 69 | see data-hunk table | 35568 | decoded |
| ground-patch ("type B") model data, ids 36 to 42, 98 to 101, 105, 106, 109 to 132 | 69, 70 | `0x495CC`-`0x4A6E7` | 4380 | decoded |
| terrain cell index tables A, B, C (128 words each), then the rows | 66 | `0x47374`, `0x47474`, `0x47574`; rows from `0x47674` | | verified |
| terrain cell item lists | 66, 67 | `0x47728`-`0x47D7B` | | verified |
| terrain scan-order, symmetry and distance tables | 65 | `0x4621C`-`0x47373` | 4440 | read from code, partly decoded |
| coarse and fine ground grids and their mesh records | 68 | `0x47D7C`-`0x495CB` | 6224 | located, not decoded |
| scenario spawn regions and placement lists | 27 | region table `0x2DE28`; data `0x2DCE8`-`0x2E5FF` | 2328 | partly decoded |
| ground-mesh scan lists | 28 | `0x2E600`-`0x2F15C` and following | | located, not decoded |
| menu palette, flash palette, 16 fade steps (in-flight palette is step 0) | 21 | `0x29F24`-`0x2A1A3` | 640 | verified against live memory |
| HUD font (3 by 5 pixel glyphs) | 61 | `0x426D0`-`0x42A37` | 872 | verified |
| sine table, inverse tables | 63 | `0x42C00`-`0x43DF3` (sine at `0x436E8`) | 4596 | verified |
| flight-control response tables | 60 | `0x425D8`-`0x426CF` | 248 | read from code |
| HUD and radar symbol masks | 62 | `0x42A38`-`0x42BFF` | 456 | located |
| warning and status messages | 59 | `0x41FF0`-`0x425D7` | 1512 | see layout note |
| menu, briefing and mission text | 64 | `0x43DF4`-`0x4621B` | 9256 | decoded, note 08 sections 1 and 2 (110 ids, 91 records, 337 lines, drawn in the 5 by 7 font of hunk 61 at `0x4283C` by the engine of `0x377B2`) |
| instrument panel, front overlay, splash pictures | disk | `pix/inst5`, `pix/frnt5`, `pix/splsh`: IFF ILBM, 5 planes, 320x55, 288x12, 320x200 | 7230, 988, 23224 | headers and palettes decoded |

### Data hunks 41 to 70 (answers open question 1 of the layout note)

These hunks are CODE-typed but contain no instructions. Hunks 45 to 50, which the layout
note marked least certain, are model data (flat ground-pattern and runway objects).

| hunk | address | size | content | ids or evidence |
|---:|---|---:|---|---|
| 41 | `0x39500` | 2832 | F/A-18 model | id 4 (record base `0x39508`) |
| 42 | `0x3A010` | 3224 | strip-section landmark models (bridge-like) and small pieces | ids 48, 49, 55, 56, 57, 65, 87 |
| 43 | `0x3ACA8` | 2068 | strip-section landmark models | ids 50, 53, 54, 67, 88 |
| 44 | `0x3B4BC` | 2024 | strip-section landmark models | ids 51, 52 |
| 45 | `0x3BCA4` | 724 | flat marking figures (dashed lines, a flat triangle) | ids 43, 46, 47, 66, 82-85, 96 |
| 46 | `0x3BF78` | 1120 | flat ground-pattern models, six detail levels each | ids 44, 60, 68, 69 |
| 47 | `0x3C3D8` | 772 | flat ground-pattern models | ids 59, 71, 97 |
| 48 | `0x3C6DC` | 608 | flat ground-pattern models and one flat triangle | ids 93, 94, 95 |
| 49 | `0x3C93C` | 1512 | flat ground-pattern models, runway number figures | ids 72, 73, 76-81 |
| 50 | `0x3CF24` | 1124 | flat ground-pattern models | ids 45, 70, 74, 75 |
| 51 | `0x3D388` | 3440 | two missiles, F-16, MiG (one model shared by two ids) | ids 0, 1, 3, 6, 7 |
| 52 | `0x3E0F8` | 3284 | aircraft carrier (shared by two ids) | ids 8, 9 |
| 53 | `0x3EDCC` | 2568 | submarine | id 12 |
| 54 | `0x3F7D4` | 2328 | buildings (boxes, towers) | ids 61, 62, 63, 89, 90, 107, 108 |
| 55 | `0x400EC` | 2012 | cruise missile, small effect objects, pyramid-roof buildings | ids 10, 33, 34, 35, 58, 86, 104 |
| 56 | `0x408C8` | 1736 | towers and a small box | ids 64, 102, 103, 133 |
| 57 | `0x40F90` | 1704 | airliner (one model shared by three ids) | ids 5, 14, 15 |
| 58 | `0x41638` | 2488 | cone objects (rescue pod style) | ids 2, 11, 13 |
| 59 | `0x41FF0` | 1512 | warning and status messages | layout note |
| 60 | `0x425D8` | 248 | two flight-control response tables of 62 words each (rising ramps, 18 to 1638); one entry is read as a per-frame rotation increment | referenced from hunk 0 (`0x155E2`, `0x155EC`) and hunk 30 (`0x31FF6`) |
| 61 | `0x426D0` | 872 | HUD font (3x5 glyphs) and a few small symbol bitmaps | see Fonts |
| 62 | `0x42A38` | 456 | rows of bit masks used by the HUD and radar symbols | referenced from hunk 37 |
| 63 | `0x42C00` | 4596 | numeric lookup tables: sine (901 entries) and inverse tables | see Constants |
| 64 | `0x43DF4` | 9256 | menu, briefing and mission text | note 08 sections 1 and 2 |
| 65 | `0x4621C` | 4440 | terrain tables: draw-distance map (64 bytes), orientation permutations, scan lists | see Terrain |
| 66 | `0x47374` | 1848 | terrain cell index tables A, B, C, their rows, then item lists | see Terrain |
| 67 | `0x47AAC` | 720 | cell item lists (table C) | see Terrain |
| 68 | `0x47D7C` | 6224 | ground grids (8 by 8 and 32 by 32 words) and linked mesh records | see Terrain |
| 69 | `0x495CC` | 888 | type B ground patches (ids 36-42, 98-101) and the flat models 91, 92 | decoded |
| 70 | `0x49944` | 3492 | type B ground patches (ids 105, 106, 109-132) | decoded |

Data also sits inside hunks the layout note calls code: hunk 21 (palettes), hunk 27
(`0x2DCE8`-`0x2E5FF`, spawn regions and position blocks), hunk 28 (`0x2E600`-`0x2FAB3`,
ground-mesh scan lists) and hunk 39 (message labels near `0x36929`, the key row at
`0x37C92`). Hunks 71 to 73 are zero-filled and are
used by the program as variable storage (hunk 0 relocates into hunk 71 about five hundred
times). The disk files `text/*` are 8-bit sound samples (smooth signed waveforms), not
tables.

## Format or Behaviour

### 1. Object records and ids

An object id (0 to 133) indexes the record table; the record address is
`0x25ECC + 20 * id`. Ids 16 to 32 are not real records: those 17 slots are zero in the
file and the object spawner fills each with a copy of a template record (live object slot
s uses the copy at `0x2600C + 20 * s`). Each record is five longs.

| field | offset | meaning |
|---|---:|---|
| 0 | +0 | draw handler address (below) |
| 1 | +4 | model base address; every 16-bit offset in the model is added to this |
| 2 | +8 | start of the detail list; equal to field 1 in all 117 real records |
| 3 | +12 | alternate detail list used when the object is the player's own aircraft in cockpit view. For ids 3-7, 10, 14, 15 it is base - 8 (a four-word list: a leaf for the near block, a one-program draw item and a second leaf), for ids 0 and 1 it is base - 2, otherwise equal to base |
| 4 | +16 | auxiliary pointer, zero except ids 8, 9, 12 and the small objects 46, 58, 72, 73, 86, 95, 104 (points into the model: a secondary stream); not decoded |

Handlers (all end in the same drawing routine at `0x21D0C` except type B):

| handler | ids | note |
|---|---|---|
| `0x21C44` | 0, 1 | standard entry plus special handling when the object is the player's own aircraft |
| `0x21C40` | 43-45, 47, 50-57, 59-66, 68-71, 74-85, 89-94, 96, 97, 102, 103, 107, 108, 133 | plain entry, jumps to `0x21D0C` |
| `0x21D0C` | 33-35, 46, 48, 49, 58, 72, 73, 86, 88, 95, 104 | the drawing routine itself |
| `0x21C34` | 67, 87 | draws only when the camera is higher than 12,800 ft (`V:4AB1E` below -0x280000); it returns 0 at or below that height, whatever the object's distance (note 03/04a) |
| `0x26944` | 3-10, 12, 14, 15 | aircraft, carrier, submarine: damage and explosion state machine, then `0x21C44` |
| `0x2699E`, `0x26A3E` | 2, 13; 11 | cone objects: pick one of several detail-list start addresses from the object's phase counter, then `0x21C44` |
| `0x22CFC` | 36, 38, 98 | type B ground patches; the routine returns 0 when the camera height `V:4AB1E` is below -`0x380000` (above 17,920 ft); no object distance is involved (note 03/04a) |
| `0x22D0A` | 37, 39-42, 99-101, 105, 106, 109-132 | type B ground patches; the routine returns 0 when `V:4AB1E` is below -`0x800000` (above 40,960 ft; note 03/04a) |

### 2. Model container (all models except type B)

A model is built from three kinds of record, all addressed by 16-bit offsets from the model
base: a **detail list** that selects a detail level by distance, one **block** (header plus
vertices) per level, and **programs** (command lists) reached from a **stream** that
follows the chosen level in the list. To draw an object the renderer needs: base, list,
one block, one stream.

**2.1 Detail list.** The list starts at the model base (record field 2) and is read as
16-bit words. A word with bit 15 clear is a distance threshold (bits 0 to 13 the distance,
bit 14 selects the node shape). The renderer compares the threshold, shifted right by the
instance scale (low nibble of the render-list entry, an instance property), with the view
distance of the object. To enumerate all levels of a model walk the nodes like this:

| first word | shape | words | near case (object closer than the threshold) | far case |
|---|---|---:|---|---|
| bit 15 clear, bit 14 clear | threshold, jump, leaf | 3 | leaf is the third word; the stream starts right after it | if the jump word has bit 15 set nothing is drawn, else continue at base + jump |
| bit 15 clear, bit 14 set | threshold, leaf, stream item | 3 | leaf is the second word; the stream starts at the third word | skip all three words and read the next node |
| bit 15 set | leaf only | 1 | the word is the leaf; the stream starts after it | (last-resort level, reached when no threshold applied) |

Leaf word: `0xFFFF` means draw nothing; otherwise bits 0 to 11 are the byte offset of the
block from the model base. Bit 14 clear means the level needs a clipping test, set means
it never does. Bit 13 selects between two gating rules tied to which terrain tier is
being drawn (read from code at `0x21D82`-`0x21DD2`, not fully decoded). The near-most level
is the first leaf met.

Example (id 48): the list at `0x3A010` begins with the words `00D0 0016 4350`: threshold
`0x0D0`, jump `0x16`, leaf `0x4350`. The near level's block is at
`0x3A010 + 0x350 = 0x3A360` and its stream starts at `0x3A016`; the far path continues at
`0x3A026`, a node of the "bit 14 set" shape (`4140 4430 A258`). Id 48 has six levels with
blocks at offsets `0x350`, `0x430`, `0x4E0`, `0x56C`, `0x5E4`, `0x638`, each with its own
simpler stream. Level counts for every id are in the catalogue (section 8).

**2.2 Block.** A header of 10 bytes, then vertices.

| offset | size | field |
|---:|---:|---|
| +0 | word | part id: 0, 1, 2, 33, 50 or `0xFFFF` in the aircraft and ships, other values (112 to 1218) in the buildings; its low byte is stored by the renderer as the object's current part id (object byte `+0x7B`); not otherwise decoded |
| +2 | word | mid distance threshold Tm (model units) |
| +4 | word | far distance threshold Tf (Tf is twice Tm in the aircraft) |
| +6 | byte | scale nibbles: high nibble H, low nibble L |
| +7 | byte | flags (below) |
| +8 | byte | b8: number of vertices transformed up front |
| +9 | byte | b9: number of those also projected (equal to b8 in every flag-1 model) |
| +10 | | vertex data |

Precision rule (read from `0x21E5E`-`0x21E86`): when the object is farther than Tf the
vertex coordinates are shifted right by L bits (plus the instance scale), between Tm and Tf
by H bits, nearer than Tm by none. Aircraft use `0x23` (H = 2, L = 3), the carrier and
submarine `0x13`, missiles `0x36`.

Flags, bit by bit, with the ids concerned (near-level flags):

| bit | meaning | ids |
|---:|---|---|
| 0 | the object rotates with its own attitude matrix (aircraft, missiles, ships); clear = fixed ground object using only the camera matrix | flags 0x01: 0-7, 10, 11, 13-15; 0x11: 8, 9, 12 |
| 1 | flat: vertices are two words (x, z) on the ground plane | 0x02: 44, 45, 59, 60, 68-71, 74-81, 91-94, 97; 0x42: 43, 47, 66, 82-85, 96; 0x06: 46, 72, 73, 95 |
| 2 | a second vertex section of relative groups follows the plain vertices (path at `0x22BDE`; format and transform in note 03, "Block set-up of the terrain models") | 46, 72, 73, 95 |
| 3 | a strip section follows the plain vertices (section 2.4) | 0x08: 48-57, 65 |
| 6 | together with bit 1: a size and visibility test on the first two vertices runs before drawing | 43, 47, 66, 82-85, 96 |
| 4, 5, 7 | not examined by the renderer (probably collision code); bit 4 set in 8, 9, 12, 58, 86, 104 | |
| none | fixed 3D ground object | 0x00: 33-35, 61-64, 67, 87-90, 102, 103, 107, 108, 133 |

**2.3 Vertices and slot addressing.** The draw routine copies block vertices into a
vertex array of 6-byte slots (x, y, z as three words) and every program operand is a byte
offset into that array, always six times the slot index. How block bytes map to slots:

| block kind | vertex i in the block | slot i |
|---|---|---|
| 3D block (flag bit 1 clear) | three words at block + 10 + 6 i | (x, y, z) as stored |
| flat block (flag bit 1 set) | two words (x, z) at block + 10 + 4 i | (x, 0, z) |
| type B vertex block (section 7) | two words (x, z) at vertex block + 6 + 4 i, after a count word and two skipped words | (x, 0, z) |

The first b8 vertices are processed up front; later items bring further vertices in by an
extra-vertex count and a start offset (section 4). For 3D blocks that start offset is a
block byte offset and equals the slot byte offset (6 per vertex); for flat blocks it is a
block byte offset with a 4-byte stride (slot byte offset = 1.5 times it). Extra-vertex items
occur in ids 3, 4, 6-9, 12, 43-45, 47, 59, 60, 66, 68-71, 74-85, 93, 94, 96, 97, 102, 103 and,
in type B models, ids 39, 99, 100 (their offset base for type B is not established: open
question 10). So the block is an array of vertices that continues beyond b8, and a decoder
reads a vertex by index. Axes: x lateral, y up, z forward (the F/A-18 nose is at +z, its fins
at +y). Note 03 states the camera x axis points left; the carrier island at negative x agrees
with +x being the pilot's left. Values are model units, scaled at draw time by the shifts
above (section 10 for the relation to positions).

**2.4 Strip section (flag bit 3).** After the b8 plain vertices the block holds chains
that generate two interleaved polylines in the vertex array: one in the even slots
starting at slot b8, one in the odd slots. A chain, repeated until a negative count word:

| step | contents |
|---|---|
| start | count C (word; negative ends the section), then the first point A (three words) |
| forward pass | C times: a point B (three words) and a division count n (word) |
| backward pass | count D (word; zero means none; bit 15 set starts one slot earlier); then D times: a point B and a division count n |

Each segment from the previous point to B is cut into n equal parts (n of 1 or less means
no cut): the interior points are the previous point plus k/n of the difference, k = 1
to n-1, computed with four extra fraction bits and truncating division, followed by B
itself. In the forward pass they go to the even slots in steps of two, A in the first
slot. The backward pass starts from the last point of the forward pass and fills the odd
slots downward: its first point goes three slots before the end of the forward chain (one
slot before if bit 15 of D is set), then steps of minus two. The next chain starts at
the slot after the forward pass. Consecutive slot pairs (even, odd) are therefore opposite
points of a band; operations 11 to 14 draw rungs, rails and hangers from them. For id 48
the forward chain is one segment of 13 divisions (the deck line) and the backward chain has
five segments (the cable arch), giving 27 slots; the plot is the deck line with a
suspension cable and vertical hangers (verified by plot).

**2.5 The cockpit-alternate detail list (what the extractor must also export).** Record field 3 (+12, section 1) points at a second detail list used for the player's own record in the cockpit views and in the pass after an external view (`04a` section 8a, tests 6 to 9). It is read by the same drawer as the normal list, with the same word grammar (section 2.1) and the same blocks (offsets from the model base).
Its location and shape by id (the offsets are file data and are not listed here):

| ids | field 3 | list |
| --- | --- | --- |
| 3-7, 10, 14, 15 | base - 8 | four words: (1) a leaf-only word (bit 15 set) naming the **near body block**, its bit 14 clear (the clip test is needed); (2) the stream item that follows it: one draw item (bit 15 and bit 13 set: one program follows, after which the stream ends; bits 0 to 11 = the program at that offset from the base) whose program is the near body (op 59 or op 42 vertex synthesis, then op 4 with the detail faces in pen 0; no gear, flap or damage selector); (3) a second leaf-only word naming the **cockpit block**, the block whose part id word is 0xFFFF (four vertices; the word that follows is 0xFFFF, the end of the stream: this level has no program); (4) the terminator 0xFFFF, which is also the cockpit level's stream. Id 10 names the same block in words (1) and (3), so its cockpit variant draws the body. The near body block is the same block as the nearest normal level's: word (1) and the first leaf of the normal list carry the same block offset from the model base (read from the image for the two F/A-18 templates, 0x6FC for template 4 and 0x790 for template 3), but the normal leaf has bit 14 set (no clip test) and word (1) of the alternate list has bit 14 clear and bit 15 set (leaf only; the clip test is made). The alternate list therefore adds to the normal level only the near body program and the cockpit leaf. |
| 0, 1 | base - 2 | one word 0xFFFF (draw nothing) followed by the normal list; the whole alternate list draws nothing and the drawer returns 0 |
| all others | base | the normal list itself |

The walker chooses between the lists and the two levels by the tests of `04a` section 8a: tests 6 to 8 walk the whole alternate list (the near body level, whose result is op 4's +1), test 9 skips its first two words and walks the cockpit level (returns 0, the part id word 0xFF). **The extractor exports the alternate list as two extra levels per model with a distinct alternate list (ids 3-7, 10, 14, 15; 0 and 1 as one level with `present` 0): level N (near body): the block of word (1), the single draw item of word (2) with its program list, shape "leaf only" and threshold 0;
level C (cockpit): the block of word (3), no stream (`present` 0 for the program, part id 0xFF), threshold 0**; plus a flag or index in the model header that tells the core which levels are the alternate ones. Everything else (vertices, synthesis, programs, gates) is decoded by the existing machinery; the near body's program needs the formulas of ops 42, 43, 44, 59 (note 03) to resolve its vertex slots, which is why the models 3, 4, 6 and 7 are reported "partial" today.

### 3. Streams: the tree of drawing commands

The stream that follows a selected level is a small tree stored as words (offsets from the
model base) whose leaves reference programs. The renderer follows **one** branch per node,
not both: each node tests on which side of a plane the viewer is and continues with either
its first or its second child offset. A zero test result takes the first offset, a
non-zero result the second (note 03 describes the same rule). The two branches carry
different draw items (the faces worth drawing from that side), so the tree is a view-
dependent selection, not a painter's sort over one set. Each stream item starts with a
word; bit 15 clear starts a node, bit 15 set a draw item.

Nodes (a second word w2 follows the first word w1):

| condition | node | words that follow |
|---|---|---|
| bits 10 to 15 of w2 all set (`0xFC00`) | jump | one offset; continue there (w1 unused) |
| bits 10 or 11 of w2 set | axis node | w1 is the byte offset of a block vertex whose coordinate is the plane (bits 10-11: 1 = y, 2 = x, 3 = z; bit 12 swaps the two children); then two child offsets |
| bit 13 of w2 set | point-and-normal node | six words (a point and a normal, model space at the vertex scale; w1 unused), then two child offsets |
| otherwise | three-vertex node | w1 is the byte offset of the first vertex, two more vertex offsets follow, then two child offsets; bits 7 to 9 of w2 are the scale shift of the test |

Rows are tested in the order listed (jump, axis, point-and-normal, three-vertex): bit 12
of w2 means "swap" only inside an axis node, and a node with bit 12 set but neither an axis
field nor bit 13 would fall to the three-vertex row (and in the code reads its plane from a
different place); no shipped node has bit 12 set (84 three-vertex, 16 point-and-normal, 14
y-axis and 4 x-axis nodes; no z-axis node occurs).

"First child" below always refers to the boolean test flag the code produces (zero flag
means first child), not to a dot product that happens to be zero; a dot product of exactly
zero counts as non-negative and gives the second child.

- Three-vertex node (verified, note 03): first child when the dot product of the cross
  product (second - first) x (third - first) (rotated vertices, edge words shifted left by
  the scale bits) with the first vertex is negative.
- Axis node (verified, note 03 "The side tests of the block tree"; summary of the older text follows, the arithmetic there is the rule): the vertex coordinate named by w1 (y, x or z of
  block vertex w1; at block offsets +0xC, +0xA, +0xE) is shifted right by the low nibble L of
  block byte +6 and, for x and z, increased by the object offset word (`V:4ABE2` for x,
  `V:4ABE6` for z) shifted left by the instance scale; this is the plane value p. It is
  compared (signed) with the negated camera component on the same axis (`V:4AB2A` for x,
  `V:4AB30` for y, a long, `V:4AB2E` for z; the camera expressed in object space at the
  vertex scale). With bit 12 clear the first child is taken when the negated camera
  component is greater than or equal to p, the second when it is less; bit 12 set swaps them.
- Point-and-normal node (verified, note 03; the eye vector is the low word of the camera longs `V:4AB1A/1E/22` shifted right by the LOD and by 8): the point (three words) is shifted
  right by the instance scale, offset by the object offset words (unshifted) and reduced by the
  eye vector (this differs from the axis test, which shifts the vertex right by L and the
  offset left by the instance scale; both are as in the code and are not reconciled, open
  question 9); the normal is three unnormalised words in object space (the orientation of the
  block vertices) and the node value is their dot product; negative selects the first child.

Note 03's wording of the bit-12 dispatch differs slightly from this section; this note is authoritative for the node dispatch (note 03 needs the edit).

The extractor stores the plane descriptor (kind, the vertex indices or the point and normal,
the swap bit, L) and both child addresses; the core evaluates the test per frame. The signs and the arithmetic of the three tests are verified in the rig (note 03, `bsp_check.py`); open question 9 is settled.

Draw items: a first word with bit 15 set. `0xFFFF` ends the stream. Bit 13 set: one
program follows; it is at the 12-bit offset in bits 0 to 11, or at the 32-bit address in
the next long when bit 12 is set; after it the stream ends. Bit 13 clear: the 12-bit
offset (or the long when bit 12 is set) addresses a **list of program references**: each
entry is a word whose bit 15 marks the last entry, whose bit 12 means a long address
follows (else a 12-bit offset), and whose other bits are the program offset. When the
list is done the walk continues in the stream, unless bit 14 of the draw item was set,
which ends the stream. The draw item that sits in the third word of a "bit 14 set"
detail-list node (section 2.1) is always the single-word form: the far path skips exactly
three words, so no long can follow it.

Order of drawing within a branch: draw items are executed in stream order and the programs
of a list in list order; later items overdraw earlier ones. That order is the painter's order
the extractor must preserve.

### 4. Programs

A program is a run of items executed in order until one whose operation word has bit 14
set. Each item has one of two shapes:

| shape | words |
|---|---|
| short | an operation word with bit 15 set, then the operation's arguments |
| gated | a first gate word with bit 15 clear (`0x7FFF` means always), a second gate word unless the first is `0x7FFF`, a count N of extra vertices (word; zero or negative means none), the start byte offset of the extra vertices when N > 0, then an operation word (bit 15 either way), then the arguments |

The operation word's bits 0 to 13 are a byte offset into the 78-entry jump table
(operation index = value / 4); bit 14 marks the last item of the program.

Gate rule: g1 shifted right by the instance scale is compared with the scaled view distance (note 04a, P1).
When the object is at most that far (nearer than g1) the item runs if the second gate
word is negative; when it is farther the item runs only if the second gate word is
non-negative and its scaled value is still at least the view distance (a distance band).
**An item that fails its gate ends the program it belongs to** (its later items do not run; the other
programs of the draw item's list and the tree walk go on), as a negative handler return does (note 04a, P6).
Extra vertices: N vertices starting at the given byte offset are copied from the block
(block offset +10) into the same slots of the vertex array before the operation runs;
landing gear and afterburner detail comes in this way.

State-dependent items. Aircraft programs contain selectors that depend on object state
words (damage, gear, flaps, animation frame): operations 42-45, 50-54, 59-61, 66, 68, 70,
72, 73, 76, 77. A selector skips a number of bytes of the program that follows, so only
the variant chosen by the state is executed, or builds vertices (control surfaces). With
every state word zero the skips are zero and the program runs straight through its
first variant; the other variants follow in memory, each ending with a last-flagged
item, and are reached by the skip amounts. Examples: operation 50 skips 0, `0x14`, `0x52`,
`0x7C`, `0xA6`, `0xD0` or `0xFA` bytes for state 0 to 6 (the low nibble of object
byte `+0x7C`); operation 66 skips 8 bytes in states 3 to 6; operation 54 skips one word
for state below 6; operation 77 skips ten bytes per unit of the status byte `V:4A8FF`;
operations 68 and 72 skip (frame counter `V:4A992` low nibble) times 0x34 and 0x12 bytes;
operation 51 skips its argument when bit 3 of object word `+2` is set; operation 73 skips two
bytes when the object index `V:4AA6C` is 0x57.

### 5. Operation table

Operation index = jump-table index. Arguments are the words after the operation word. **Note 03 ("Draw operations") is authoritative for the return words, the pens, the height gates and the vertex rules of ops 7, 8, 39, 40, 41-44, 48, 49, 59, 62, 74 and 75, which supersede the probe fits and the "BLOCKING" remarks of this table and of 5b.**
Notation: "v" is a vertex operand, a byte offset into the vertex array (six times the slot
index); A, B, C, D are the vertices in the slots at v, v+6, v+12, v+18 (consecutive
slots); "V[x]" is the slot at byte offset x. In vertex lists bit 15 marks the last
element. "Roles" were established by probing the handlers in an emulator: random vertex
arrays go in, the polygons and lines handed to the drawing routines come out, and each output
point is fitted as a linear combination of input slots (all fits exact); items marked
(inferred) were not probed. "Ids" lists the models that contain the operation.

| op | arguments | role | ids |
|---:|---|---|---|
| 0 | none | no operation | 8, 9, 12, 102, 103 |
| 1 | v, colour | single point | 4, 5, 14, 15, 65, 80, 81 |
| 2 | v1, v2, colour | clipped line V[v1] to V[v2] | most ids |
| 3 | v list (last has bit 15), W, back-colour word when W selects it | filled polygon, rules in section 6 | 0-15, 34, 35, 58, 61-64, 86, 89, 90, 102-104, 107, 108, 133 |
| 4 | list of faces; a face is distance word (negative ends the list), N, flag word, N vertex offsets | detail faces in colour 0; the first distance word also gates the whole list; the face is skipped if its flag word is zero and it faces away. The measured length is state dependent only because of that gate; the layout above is the real one | 2-7, 10, 11, 13-15 |
| 5 | not used | | none |
| 6 | N, vA1, vA2, vB1, vB2, colour | N-1 rungs; rung k (k = 1..N-1) joins A1 + k (A2 - A1)/N to B1 + k (B2 - B1)/N, fixed point with four fraction bits, rounded: exact rule in note 03, "Line operations 6, 11, 12, 14" | 107, 108 |
| 7, 8 | five and six words (colour, vertex offsets; bit 15 of one of them flips the winding) | triangle faces. Probing gave a triangle whose first point is the slot of the third operand (op 7) and whose other points are two of the other named slots, chosen by bit 15; the exact rule is not reduced. BLOCKING for the bridges (ids 48-52: 17 occurrences of op 7 and 17 of op 8 in their programs, repeats across levels included) | 48-52 |
| 9 | colour, v | quad A, B, C, A-B+C | 48-57 |
| 10 | colour, v | quad D, D+(B-A), D+(C-A), D+(C-B) (parallelogram anchored at the fourth slot) | 48-52 |
| 11 | word (bits 8-15 count, bits 0-5 colour), v | count line segments, each from slot s to slot s+1, starting at v and advancing two slots per segment | 48-57 |
| 12 | same word, vref, v | like 11 with both end points of every segment **reduced by** V[vref+1] - V[vref] (note 03) | 48-57 |
| 13 | colour, then (v1, v2) pairs, last v2 with bit 15 | line list | 0-15, 34, 35, 40, 44-46, 48-60, 63-65, 67-81, 86-88, 93-95, 102-106, 112, 113, 121, 123, 126-132 |
| 14 | colour, vref, then (v1, v2) pairs, last v2 with bit 15 | line list with both end points of every line **reduced by** V[vref+1] - V[vref] (note 03) | 48-57 |
| 15 | colour, a, b, c, e | quad V[b], V[c], V[e], V[e]-(V[a+6]-V[a]) | 48-54 |
| 16 | colour, a, b, c | quad V[a], V[a]-d, V[c]-d, V[c] where d = V[b+6]-V[b] | 48, 49, 51, 55-57 |
| 17, 18 | quads of four vertex offsets, ended by a negative word | filled quads, fixed colour 12 (only op 17 is used) | 46, 72, 73, 95 |
| 19 | colour, v, N | 2N line strokes with A, B, C = the slots v, v+1, v+2, P = A - C, Q = B - C, o_k = h(k Q) (h = halve as a word): the first N from C + o_k to A + o_k, the second N from A - h(Q) - o_k to C - h(Q) - o_k. Exact rule and arithmetic: note 03, "Hatch and stripe geometry" | 8, 9, 12, 43-45, 47, 59, 60, 66, 68-71, 74-79, 82-85, 93, 94, 96, 97 |
| 20 | v, N | 2N filled parallelogram strips (four slots v..v+3), colour 13 in the plane-1 toggle mode; the first N shifted by o_k from the edge C-A, the second N the reflected strips. Note 03 | same ground patterns as 19 |
| 21 | v, N1, then N1 inner counts N2(0..N1-1) | N1 rows of filled parallelogram stripes, colour 13 in the toggle mode: row i is based on slot v+3+i, stripe j shifted by h(j (S1 - S3)), edges S0 - S3 and S2 - S3. Note 03 | 44, 45, 59, 60, 68-71, 74-79, 93, 94, 97 |
| 22 | colour, v, N1, then N1 inner counts | N1 rows of line strokes in the given colour: row i is based on slot v+2+i, stroke j shifted by h(j (S0 - S2)), stroke vector S1 - S2. Note 03 | 8, 9, 12, 44, 45, 59-60, 68-71, 74-79, 91-94, 97 |
| 23 | v, N | N filled parallelogram strips, one per slot v+3+i, each of width (S0 - S3) + h(3 (S1 - S3)) and length S2 - S3, in the plane-1 toggle mode (no stripe offsets). Note 03 | 44, 45, 59, 60, 68-71, 74-79, 93, 94, 97 |
| 24 | N, N vertex offsets, colour | filled polygon through the listed vertices (length N+2 words) | 39, 99, 100 |
| 25 | N, colour | filled polygon through slots 0 to N-1 | 37, 98, 101 |
| 26 | N, v | filled polygon through N consecutive slots from v, fixed colour 7 with stipple parameters 3, 3 | 36, 39-42, 99, 100 |
| 27 | colour, v | quad A, B, C, A-B+C with stipple parameters 2, 0, 2, 0 | 44, 45, 59, 60, 68-71, 74-79, 93, 94, 97 |
| 30 | colour, v | same quad as 27 with stipple parameters 8, 8, 0, 0 | 73, 105, 106, 109-132 |
| 31 | one word | size and visibility early-out on two consecutive flat vertices of the drawn block: the word is a byte offset into that block's vertex list (block base `V:4AAEA` + 10 + word); aborts the rest of the program when too small (note 03, "The block that op 31 names") | 44, 45, 59, 60, 68-71, 74-79, 93, 94, 97 |
| 32 | colour, v, N | like 20 with an explicit colour and solid fill (note 03) | 8, 9, 12 |
| 33 | none | traversal control: sets the stop flag for objects marked 0x40 | 8, 9 |
| 34 | v, shift s | parallelepiped completion, see section 5b | 61, 63, 64, 89, 90, 102, 103, 107, 108 |
| 35 | v | as 34 without the extra slots | 102, 103 |
| 36 | v, shift s | six-slot variant of 34 (section 5b) | 64 |
| 38 | none | no operand: fixed vertex synthesis on the absolute slots (base 0), shared handler with op 46 (note 03, "Fixed-formula synthesis without an operand") | 0 |
| 39 | long (two words), word | quad on the absolute slots 0 to 3 (no vertex operand): S0, S1, S2+S3-S1, S0-2 S1+S2+S3, with Sk the array slot k; the pen is one of the two halves of the long chosen by the viewing direction (inferred), the last word is a second colour | 61, 63, 64, 89, 90, 107, 108 |
| 40 | long | triangle on the absolute slots 0, 1 and 4 (colour as in 39) | 62 |
| 41 | v, shift | derives nine slots, section 5b | 62 |
| 42, 43, 44, 59 | v (one word) | derive slots for control surfaces, section 5b | 3, 6, 7 (42); 3, 4, 6, 7 (43, 44); 4 (59) |
| 45 | none | no operand: fixed vertex synthesis (shared handler with op 47), note 03; not a selector | 0 |
| 48 | v1, v2, colour | line that is **not clipped** (note 03): drawn, and counted, only when both end points are inside the view pyramid | 61-64, 89, 90, 102, 103, 107, 108 |
| 49 | v1, v2, v3, W | single point at the first vertex when the face test passes (inferred) | 3, 4, 6, 7 |
| 50 | none | state skip, seven variants | 3, 4, 6, 7 |
| 51 | skip bytes | skips that many bytes when bit 3 of object word `+2` is set | 3, 4, 6, 7 |
| 52 | a, b, t (byte offsets; slot = offset / 6) | with d = V[b]-V[a]: slot t/6+3 = V[t] + d, slot t/6+4 = V[t+6] + d/2, slot t/6+5 = V[t+12] + d/2; then skips 14 bytes per state unit | 3, 4, 6, 7 |
| 53 | a, b (byte offsets; slot = offset / 6), skip bytes | with sa = a/6 and sb = b/6: slot sa+30+5j+(k-1) = slot sa+k + (slot sb+j - slot sa) for k = 1..5, j = 0, 1, 2 (fifteen slots; gear and flap clusters); then skips the given bytes (the skipped words hold the other animation steps, each again an op 53) | 3, 4, 6, 7 |
| 54 | one word | skips one word when state bits 4-6 of `+0x7C` are below 6 | 3, 4, 6, 7 |
| 55 | long base, then offset words until a negative word | polygon list: each offset selects a vertex list at base+offset (itself ended by a bit-15 vertex and followed by a polygon word as in op 3) | 2-4, 6, 7, 11, 13 |
| 56 | none | no operand: fixed vertex synthesis (slots 15 to 22), note 03; not a selector | 2, 11, 13 |
| 57 | colour, v, N1, N2(0..N1-1) | op 21 with an explicit colour and solid fill (note 03) | 91, 92 |
| 58 | v, colour | point (second shape, lights) | 3, 6, 7, 10, 34, 35 |
| 60, 61 | one word | share the handler of 59 | none |
| 62 | vertex byte offset, pen, size S (in this order, verified in the rig) | filled disc at the vertex shifted left by the drawer frame's shift word, radius S / distance limited to 127; rule in note 03 | 2-4, 6, 7, 13, 33-35 |
| 63, 64, 65, 66 | none | ops 63, 64, 65: no operand, fixed vertex synthesis (note 03, not selectors); op 66 is a state selector (skips 8 bytes in states 3 to 6) | 10; 2, 13; 11; 3, 4, 6, 7 |
| 67 | v0, v1, v2, W, then nine words | face-visibility selector: runs the plane test of section 6 on the three vertices with W; when the face faces away the next nine words (18 bytes) are skipped, otherwise they run as ordinary items (observed content: a line and a quad item). It draws nothing itself, so the carrier decodes | 8, 9, 12 |
| 68 | none | animation frame skip, 0x34 bytes per frame (frame = low nibble of `V:4A992`) | 8, 9, 12 |
| 69 | a, b, c, colour | quad V[a], V[b], V[c], V[a]-V[b]+V[c] | 8, 9, 12 |
| 70 | forty words | state-dependent skip over a table | 33-35 |
| 73 | none | skip two bytes when the object index (`V:4AA6C`) is 0x57 | 102, 103 |
| 74 | v1, v2, colour | line (as op 2), drawn only when the camera is higher than 240 ft (`V:4AB30` below -0xC0); otherwise its three words are skipped | 109-111, 114-120, 122, 124, 125 |
| 75 | colour, v1, v2 pairs, bit 15 of the last v2 | line list (as op 13), drawn only when the camera is higher than 240 ft; otherwise skipped | 112, 113, 121, 123, 126-132 |
| 76 | one word | skip a word | 4 |
| 77 | none | skip ten bytes per unit of `V:4A8FF` | 4 |
| 28, 29, 37, 71 | | never used by the shipped models | none |
| 72 | none | animation frame skip, 0x12 bytes per frame | 102, 103 |

Which operations are used: 67 of 78 (all but 5, 18, 28, 29, 37, 46, 47, 60, 61, 71; op 72 is used by ids 102 and 103).
Lengths: every operation is fixed length except 3 (vertex list, W, optional back colour:
vertex list length plus 1 or 2), 4 (until a negative distance word), 13 and 14 (pairs until a
bit-15 second vertex), 17 (quads until a negative word), 21, 22, 57 (N1 inner counts) and
55 (offset words until a negative word); op 24 is N + 2 words (N from its first word); op 67 is four words, and the next 18 bytes are gated items. Fixed lengths in words: op 0, 33, 38, 45-47, 50,
56, 63-66, 68, 72, 73, 77: 0; ops 1, 20, 23, 25, 26, 27, 30, 40, 41, 42-44, 59, 76,
31, 35, 54: as in the table; op 53: three words and the skip bytes. Op 52 and 53 skip
words follow the arguments (not counted).

### 5b. Vertex synthesis operations

**Superseded by note 03 ("Vertex synthesis" and the fixed-formula tables after it), which gives every slot in word-exact form, verified in the rig for ops 34-36, 38, 41-47, 52, 53, 56, 59, 63-65 (`ops_synth.py`, `synth2_check.py`, `synth_note_check.py`).** The table below keeps the older approximate rows for orientation; where they differ (ops 34 and 36 for a negative shift s: the shifted difference is formed as the earlier slot minus the later one and subtracted; ops 42, 43, 44, 59: the halvings, b+13 of op 43, the slots called non-linear) the note 03 formulas are the rule. Ops 38, 46, 45, 47, 56, 63, 64 and 65 are not state selectors: they have no operand and fixed formulas (note 03).

Some operations write new vertices into slots instead of drawing. All results are exact
integer combinations of input slots; "/2" means an arithmetic shift right by one after the
sum, so odd sums truncate toward minus infinity (read from the code: arithmetic shift right; the emulation used even values). All operands of ops 34-36, 41-44, 52, 53, 59 are byte offsets (six per slot); the table below
uses slot numbers: base = v / 6 for the single-operand ops and Vk is the slot base+k (slots "b+k" in the table mean base+k); for ops 52 and
53 the byte operands a, b, t are divided by 6 first. With V0..V3 = the slots b..b+3:

| op | new slots | formula |
|---:|---|---|
| 34 | b+4, b+5, b+6, b+7 | V0-V1+V2; V0-V1+V3; V3+V2-V1; V0-2V1+V2+V3 (the other corners of the parallelepiped with origin V1 and edge ends V0, V2, V3) |
| 34 | b+8, b+9 | with d = (V3-V2) shifted left by s (right when s is negative): V3+d; 2V3-V1+(2^s-1)(V3-V2) |
| 35 | b+4 .. b+7 | the first four of op 34 (v = 6 is used: base slot 1) |
| 36 | b+6 .. b+11 | six-slot variant: V0-V1+V2, V0-V1+V3, V0-V1+V4, V0-V1+V5, V2+V5-V1, V0-2V1+V2+V5; b+12 = V4 + (V4-V3) times 2^s; b+13 = 2 V4 - V2 + (2^s - 1)(V4-V3) |
| 41 | b+9 .. b+17 | with d = (V3-V2) shifted by s: b+9 = V2+V3-V1; b+10 = V3+d; b+11 = V5-V6+V7; b+12 = V5-2V6+V7+V8; b+13 = V4+V5-V6; b+14 = V4-V6+V8; b+15 = V4-V6+V7; b+16 = V4+V5-2V6+V7; b+17 = V4+V5-3V6+V7+V8 |
| 42 | b+17 .. b+29 and b+100 | b+17 = V3+V4-V8; b+18 = V2-V1+V6; b+19 = V7+(V6-V1)/2; b+20 = V16+(V6-V1)/2; b+21 = (V3+V4)/2+(5/2)(V6-V1); b+23 = V4+V5-V8; b+24 = (V8+V11)/2; b+25 = V4+V5-V3; b+26 = V6+V9-V1; b+27 = 2V6-V1; b+28 = V2+2V6-2V1; b+29 = V0+(V6-V1)/2. Not captured by one linear formula: b+22 and b+100 (they differ per axis; BLOCKING for one polygon each in ids 3, 6, 7) |
| 43 | b+10, b+13, b+14, b+15 | V3+V4-V6; (V7+V9)/2; V4+V5-V3; V3+V4-V7 (b+11 and b+12 are not linear: not used by any polygon) |
| 44 | b+5 | (V0+V1)/2 (b+6 not linear: not used by any polygon) |
| 52 | st+3, st+4, st+5 (st = t/6) | slot st + (sb - sa); slot st+1 + (slot sb minus slot sa)/2; slot st+2 + (slot sb minus slot sa)/2, with sa = a/6, sb = b/6; the first term is slot st + (slot sb minus slot sa) |
| 53 | sa+30 .. sa+44 | slot sa+k + (slot sb+j - slot sa) for k = 1..5 written to slot sa+30+5j+(k-1), j = 0, 1, 2 (sa = a/6, sb = b/6) |
| 59 | b+21 .. b+39 | b+21 = V6-V7+V10; b+22 = (V3+V4)/2; b+23 = V3+V4-V9; b+24 = V3+V4-V8; b+25 = V4+V5-V3; b+28 = V4+V5-(V8+V9)/2; b+29 = (V1+V2)/2; b+30 = V6-V7+V12; b+31 = V6-V7+V14; b+32 = V1+(3/2)(V6-V7); b+33 = V2+(3/2)(V6-V7); b+34 = V11+(5/2)(V6-V7); b+35 = V13+(5/2)(V6-V7); b+36 = V1+(5/2)(V6-V7); b+37 = V2+(5/2)(V6-V7); b+38 = V4+V5-V9; b+39 = V4+V5-V8. Not linear: b+26, b+27, b+110 (b+110 is not referenced by any polygon; b+26/27 already hold rest values in the F/A-18 block) |

How to use them (older text; with the note 03 formulas no slot is unresolved): id 4 (F/A-18) already contains rest-pose values for the slots that op 59
overwrites, so its polygons decode from the block plus ops 52 and 53 (exact formulas) with
nothing blocking; ids 3, 6, 7 need op 42 (exact except b+22 and b+100: one polygon each is
blocked, vertex slot 22 in each) plus ops 52 and 53; buildings need 34, 35, 36, 41 (exact).
The probes used all-state-zero memory; the object-copy half of ops 42-44 (it writes a second
array inside the object record, not the model array) is not needed.

### 6. Colour word and polygon rules

The pen is the low four bits of a colour word: the span filler hands one bit to each of
four bit planes (the palette in flight has 16 colours, section 11). The upper bits are not
read by the span routine (`0x33DC4`). Lines in operations 11 and 12 carry six colour bits
(only the low four reach the filler).

Polygon (op 3): vertex byte offsets, the last with bit 15 set, then one word W.

| W | meaning |
|---|---|
| bit 15 set | two-sided: no back-face test; the colour is W (low four bits) |
| bit 15 clear | back-face test; the visible side is drawn with W's colour; when bit 14 is set an extra word follows holding the colour of the back side (drawn from both sides); when bit 14 is clear and the polygon faces away it is skipped and the remaining items of that program are abandoned |
| bits 7 to 9 | shift applied to the edge vectors before the plane test |
| bits 10 and 11 | non-zero: axis-aligned face; the side test compares the camera with the coordinate (1 = y, 2 = x, 3 = z) of the last listed vertex; bit 12 inverts the side |
| bits 0 to 3 | colour |

The general test uses the first three listed vertices and a cross product (`0x22ACC`).
Vertex counts seen: 3 (602 uses), 4 (805), 5 (87), 6 (20), and a few of 10, 14, 16.

### 7. Type B models (ground patches)

Ids 36-42, 98-101, 105, 106 and 109-132 are flat ground shapes (lakes, fields, pads). Field
1 of the record points at a chain of detail entries. An entry is: a distance word (bit 15
set: never compared; `0xFFFF` ends the search with nothing drawn, and is the only entry of model 38), a long address of the next entry (`0xFFFFFFFF` for the last; the last entry of models 39-42, 99 and 100 names itself with the word `0x7FFF`, note 03 "Type B models"), a long
address of a vertex block, then the program stream, which uses the section 4 grammar and
operations. The vertex block is a count word, two words that are skipped, then (x, z) word
pairs; y is zero; slot i is pair i (stride rule in section 2.3). Program operands are slot byte offsets as everywhere. Most patches have two entries (thresholds `0x4D0` and `0xC00`); the
stream of an entry ends exactly where the next entry or the vertex block begins (checked
for ids 36, 39, 105, 109, 121). Operations 30, 74 and 75 are typical. The near entry of ids 105, 106 and 109-132 declares more
vertices (4 to 15) than it stores: only 1 to 7 pairs are non-zero in the file and the others
are zero placeholders (for id 109: five declared, the first two real corner points, three
zero). No operation of the program writes them and nothing writes the model data at run
time (note 03, "Type B models"; `modeldata_check.py`): **the zero pairs are literal data and
ordinary vertices**. The extractor exports them as the vertices (0, 0, 0) of their slots;
after the LOD shift, the position and the camera matrix they are the patch origin, which the
programs name (id 109: the first item is op 30 on the slots 2, 3 and 4, all the origin). The
far entries have no placeholders and the ids 36-42, 98-101 have none. The shapes reach up to
8192 units.

### 8. Object catalogue

Names are assigned from the evidence in the last column: spawn tables (section 9), the
mission texts in hunk 64, the model shape and the HUD tags. Sizes are the near level's
extent in model units (before the draw-time shift).

| id | model base | levels | what | evidence |
|---:|---|---:|---|---|
| 0 | `0x3DDF4` | 3 | air-to-air missile, short (probably AIM-9L) | shape, z extent 1551 |
| 1 | `0x3DF2A` | 3 | air-to-air missile, long (probably AIM-120), 1.3 times id 0 | shape |
| 2 | `0x41638` | 2 | cone object (pod phase, probable) | shape; same data as 13 |
| 3 | `0x3D390` | 5 | F-16 Falcon | init code at `0x1D56C` takes id 3 for aircraft code 0x10; single fin |
| 4 | `0x39508` | 5 | F/A-18 Hornet | init code takes id 4 otherwise; twin canted fins; silhouette matches captures |
| 5 | `0x40F98` | 5 | Air Force One (swept wings, T tail) | class 0x14 (its radar tag reads AF-1) |
| 6 | `0x3D436` | 5 | MiG-29 (delta wing) | class 0x12 pairs with id 6 |
| 7 | `0x3D436` | 5 | MiG-25 | class 0x13; same model data as 6 |
| 8 | `0x3E1D6` | 3 | aircraft carrier (island, angled deck; 14976 long) | class 0x20 pairs with id 8 |
| 9 | `0x3E1D6` | 3 | second carrier id, same data | |
| 10 | `0x4071A` | 3 | cruise missile | class 0x15 (radar tag CRUISE) |
| 11 | `0x418DE` | 2 | cone object (pod or parachute phase) | shape |
| 12 | `0x3EE3A` | 3 | submarine (hull and tower, 10176 long) | class 0x21; missions describe it as a surfaced submarine under a code name |
| 13 | `0x41638` | 2 | rescue pod | class 0x32 pairs with id 13; mission text deploys a rescue pod |
| 14 | `0x40F98` | 5 | Boeing 707 | class 0x16 (tag 707); same data as 5 |
| 15 | `0x40F98` | 5 | Boeing 767 | class 0x17 (tag 767) (by elimination); same data as 5 |
| 16-32 | | | run-time copies; empty in the file | |
| 33-35 | `0x400EC`, `0x400F4`, `0x40102` | 1-2 | the effect objects of a missile's ground or landmark impact, descriptor ids `0x11` (33), `0x12` (34) and `0x13` (35): one shared starburst program (a column of flame discs, op 70 timer skip, ops 71 and 62) and, for 34 and 35 (mirror images), a ground scar | note 07 section 11.3 (rig: `effects_stream_check.py`) |
| 36-42, 98-101 | hunk 69 | 1 | type B ground patches | |
| 105-132 | hunk 70 | 2 | type B ground patches | |
| 43, 47, 66, 82-85, 96 | hunk 45 | 3 | dashed marking lines, flat, with the size test | shape |
| 44, 45, 59, 60, 68-71, 74-79, 93, 94, 97 | hunks 46-50 | 6 | ground patterns and runway surfaces (flat, hatch operations 19 to 23) | shape; 45 and 74 fill the uniform cells of table C |
| 80, 81 | hunk 49 | 4 | runway number figures (two digits as line segments) | shape |
| 46, 72, 73, 95 | hunks 45, 49, 48 | 2 | flat triangles and pads (flag 6) | shape |
| 91, 92 | `0x495CC`, `0x49626` | 2 | parallel strips (piers or moorings) | plotted |
| 48, 49 | `0x3A010`, `0x3A048` | 6 | suspension bridge, main span 3920 units between the towers, tall towers | strip section plotted: deck and cable arch with hangers |
| 50-53 | hunks 43, 44 | 5-7 | other bridge spans (cantilever, trusses, low spans) | plotted |
| 54-57, 65 | hunks 42, 43 | 4-5 | long low strips (causeways, piers, road runs; widest 14556 units) | plotted |
| 58, 86, 104 | `0x4061A`, `0x4046C`, `0x4051A` | 2-3 | pyramid-roof buildings (flag 0x10) | plotted |
| 61, 63, 64, 89, 90, 107, 108 | hunks 54, 56 | 4 | box buildings and towers | plotted |
| 62 | `0x3FCEC` | 4 | tall pointed tower, 1376 units high (probably the Transamerica Pyramid) | shape only |
| 102, 103 | `0x40B38` | 4 | slim tower with flared cap (probably Coit Tower); identical data | shape only |
| 67, 87, 88 | `0x3ACB2`, `0x3A678`, `0x3ACA8` | 1 | short line pieces (roads or rails) | plotted |
| 133 | `0x408C8` | 2 | small box | plotted |

The names Golden Gate, Bay Bridge, Transamerica and Coit Tower are suggestions from
geometry only; the world has not been captured (open question 4).

### 9. Spawn classes and scenario entries

Each live object has a one-byte class at object offset `+0x62` (high nibble: 1 aircraft,
2 ships, 3 and above other objects); the radar tag shown for a target comes from it (code
at `0x36E94`; the tags are short model designations of six characters). The scenario
spawner copies a record into the object's slot. Placement entries (hunk 27; the region
table at `0x2DE28` has four regions, each with a bounding box of four words in fine cells,
a count word and entries, ended by a negative word) are five words each: record byte offset
(20 times the id), class, slot and flags, position-block reference, flags. Observed pairs:

| class | what | record id |
|---:|---|---:|
| 0x10 | F-16 | 3 |
| 0x12 | MiG-29 | 6 |
| 0x13 | MiG-25 | 7 |
| 0x14 | Air Force One | 5 |
| 0x15 | cruise missile | 10 |
| 0x16 | 707 airliner | 14 |
| 0x17 | 767 airliner | 15 (not seen directly) |
| 0x20 | carrier | 8 |
| 0x21 | submarine | 12 |
| 0x32 | rescue pod | 13 |

The player's F/A-18 (id 4) does not occur in the region tables; the player's own aircraft is created by the init code, and the F-16 entry here (class 0x10) is the scenario's F-16.

**Position block (airfield, carrier and aircraft start positions).** The position-block
reference is a byte offset into the word table at `0x2DCE8`; the entry there is an offset
(from `0x2DCE8`) of a block of five signed words:

| word | meaning |
|---:|---|
| 0 | x cell (table-C cell index, bits 22 and up of the position long) |
| 1 | z cell |
| 2 | x fine offset, units of 256 position units (may exceed one cell: a cell is 16384 of these) |
| 3 | z fine offset, same unit |
| 4 | height, units of 256 position units |

The spawner builds the position as cell * 2^22 + fine * 256 (read at `0x2D4E8`-`0x2D500`).
Examples: the carrier (class 0x20, id 8) sits at cell (70, 65) with fine offsets (6144,
6144), i.e. at (70.375, 65.375) cells and height 0. The carrier's cell (x 70, z 65) has no table entry; the nearest table-C cells are listed in section 10. The airfield: the following cells (x, z; corrected axes, an earlier version of this note had them exchanged) hold the airfield
items. The ids below are **item ids**, the bytes of the cell data; the object id of section 1 is the item id plus 16 (checked
against the cell data): table A cells (15,15) (ids 77-81), (15,16) (71), (15,17) (59, 60, 68-70), table B cells
(15,15) (93, 94), (15,16) (96, 97), (16,16) (43, 44), (16,17) (70, 75, 76), and table C
cell (65,69) (47) and cells (62, 70), (63, 70), (65, 70), (66, 70) (70). As object ids these are: A (15,15) 93-97 (flat
patterns and marking figures), A (15,16) 87 (a strip-section landmark of hunk 42, **not** a flat shape), A (15,17) 75, 76, 84, 85
(flat) and 86 (a pyramid-roof building of hunk 55, **not** flat: see below), B (15,15) 109, 110 and B (15,16) 112, 113 (type B ground
patches), B (16,16) 59, 60 (flat), B (16,17) 86 (the same building), 91, 92 (91 and 92 are
not placed by the hunk table of section 1), C (65,69) 63 (a building of hunk 54, not a flat shape) and C (62..66, 70) 86 (the same building).
Object 86 (item id 70) is therefore **not a flat model**: its flag byte is 0x10 (bit 1 clear, bit 4 set), it is a fixed 3D model with
collision data (the descriptor's auxiliary pointer is set; the data is a count word 3, so four planes, which the landmark rules of note 04a
read), and it is one of the three landmarks (item ids 42, 70, 88) that can serve as the reference of the landmark reorder. So
"runway-type flat" holds for the item ids 77-81, 59, 60, 68, 69 (A), 93, 94 (B), 43, 44 (B) and 75, 76 (B, cell (16,17); objects 91 and 92, flat: the block flag byte is 0x02 in both detail levels of each, read from the extracted models, hunk 69) and, as type B ground patches without a flag byte (section 7, handler `0x22D0A`), for the item ids 96 and 97 (B, cell (15,16); objects 112 and 113, flat ground shapes like 93 and 94, whose objects 109 and 110 are type B too; their extraction stops at an unresolved vertex slot, section 5b/7, so the shape is read from the type B convention, not from a decoded model); the item ids 70 (A, B and C), 71 (A) and
47 (C) are a pyramid-roof building, a landmark and a building, none of them flat. Which runway belongs
to which airport (and the exact orientation of each) is not decoded (open question 4).

### 10. Terrain and landmarks

**Structure.** The ground is not a polygon grid. Terrain is a sparse set of cells, each a
list of items, drawn around the player. Flat objects (ids 43-97 and the type B patches)
supply ground textures and runway markings; the sea and the sky are fills
(`04-scene-composition.md`); land and sea come from the coarse relief grids below.

**Three cell tables.** The world position is a 32-bit long of which 29 bits are used
(mask `0x1FFFFFFF` in the cell code). Tables A (`0x47374`) and B (`0x47474`) index cells of
2^24 position units on a grid of 32 by 32 (cell index = bits 24 to 28 of the long). Table
C (`0x47574`) indexes cells of 2^22 units on a grid of 128 by 128 (bits 22 to 28). A and
B are coarse tiers and C the fine tier; the world scan runs once per tier (the list builder
of `04a-display-lists.md`, which also gives the view-dependent order in which cells are
read). The content of table C covers x 41 to 73 and z 44 to 70 in C cells, which is
x 10-18 and z 11-17 in A cells; table A also has cells further out (below).

| table | cell size (position units) | cells with content | non-empty rows | extent (cells) | distinct item lists |
|---|---:|---:|---:|---|---:|
| A | 2^24 | 36 | 8 | x 0-18, z 0-17 | 9 |
| B | 2^24 | 8 | 3 | x 14-18, z 15-17 | 8 |
| C | 2^22 | 158 | 26 | x 41-73, z 44-70 | 19 |

(Extents re-derived with the corrected axes below; an earlier version of this table had x
and z exchanged.) Each table is 128 words, one per **z** cell index (the cell's number along
the z axis, record word `+0x08` of `05-flight-model.md`): a byte offset from the table
start to the row of that z. A row is a word giving twice the number of cells (a negative
first word means an empty row; empty z indices share one empty row), then that many words of
strictly increasing **x** cell indices (record word `+0x06`; the scan binary-searches them),
then one 32-bit pointer per cell to its item list. Example: the row for z = 44 in table C
is at `0x478DA`; its first word is 12, so six cells follow with x 41, 44, 47, 50, 53, 56,
each pointing at the list `0x47A40`; the same list serves 138 cells (a uniform ground
fill). Evidence for the axes: `04a-display-lists.md` (the scan reads row = z, key = x, and
reproduces the emulator's queues only so; the carrier cell x 70, z 65 then has no entry).

**Item list.** Items until a `0xFF` byte. An item is one byte (low nibble: the **sub-cell**
0 to 15 of the cell that the offsets are relative to, see below; it also orders the draw,
the builder emits the sub-cells in a view-dependent order; high nibble: an index into a table of sixteen default offsets at
`0x207C6`, used only when bit 7 of the next byte is set), one byte whose bits 0 to 6 are the
item id (the record index counted from `0x2600C`, that is the object id of section 1 minus 16; ids below 17 are the run-time entity slots) and whose bit 7 selects the default offset, and, when bit 7 is
clear, two words (dx, dz) inside that sub-cell, 0 to 4095 (up to 3,936 seen). The cell is
divided into 4 by 4 sub-cells; the nibble n names the sub-cell whose x index is
3 - (n mod 4) and z index is 3 - (n div 4), counted from the cell's low corner (the same
numbering as the sub-cell byte `+0x0A` of the entity records). The **offset unit is the
sub-cell size divided by 4096**, i.e. the cell size divided by 16384: 256 position units in
table C (one unit **u** of `04a-display-lists.md`) and 1024 position units in A and B.
So an item is at: cell corner + sub-cell corner + offset times the unit, per axis. Example
(`0x47A40`): bytes `00 2D 08 00 08 00 | 0E 4A 08 00 08 00 | FF` are sub-cell 0, item id 45 (object id 61)
at offset (2048, 2048) in it (the middle of that sub-cell) and sub-cell 14, id 74 at
(2048, 2048) in it. Worked entry (the airborne snapshot of `04a-display-lists.md`, player
in C cell x 65, z 66): the tier C cell x 63, z 65 (list `0x47A4E`) starts with sub-cell 0,
id 45 at (3840, 256). That cell is the tile (-2, -1) from the player's, so its corner is at
(-32768, -16384) u from the player's cell corner, and sub-cell 0 starts 3 * 4096 u in on
each axis: x = -32768 + 12288 + 3840 = -16640 u, z = -16384 + 12288 + 256 = -3840 u. The
emulator's queue holds the level-of-detail-shifted words (x -8320, z -1920, shift 1), which
are exactly these halved. No orientation is stored; it is baked into
model variants (object ids 48 and 49, which are item ids 32 and 33 in the cell data, e.g. in table C cell (66, 70), are one bridge in two orientations).

Default-offset table (`0x207C6`, 16 entries of two signed bytes, 32 bytes): entry n is a
(dx, dz) pair, sign-extended to words and used in place of the literal pair when the item's
bit 7 is set and n is the high nibble of its first byte. The entries form a regular 4 by 4 grid of offsets around the cell centre; the exact bytes are read from the image by the extractor. Only if an item with bit 7 set exists is the table needed; none of the
items in the three tables has it, so a decoder may raise an error for such an item.

Ids 118 to 133 (type B patches and the small box) and ids 0 to 19 never appear in cell lists.
Ids seen: table C has 32-38, 42, 45-49, 70, 74, 86-88, 91, 92, 117; table A has 20, 21, 23-41,
45, 50-65, 67-72, 74, 77-85, 117; table B has 42-44, 70, 75, 76, 88-90, 93-116. Ids 16-32 that
occur (20, 21, 23-32) are run-time slot copies whose model the spawner decides.

**Relief grids (hunk 68).** The ground-mesh routines in hunk 30 (`0x2F234`, `0x2F25A`) read
an 8 by 8 table of words at `0x47D7C` (a staircase of two values `0x1C2` and `0x1AA`
running diagonally with a few special values `0x80`, `0x11C`, `0x154`, `0x172`, `0x196`
near the middle, which looks like a coast line with a bay) and a 32 by 32 table of words at
`0x47F40` whose entries grow row by row in steps of `0x1A` from `0x800` and which seem to
index 26-byte mesh records from `0x48742`. These are probably the land and sea
classification and the coastline; the format is not decoded (open question 5).

**Extents and units.** Positions wrap at 2^29: 128 cells of 2^22 units along each axis in
the fine tier. The offset unit of an item is the sub-cell size / 4096 (256 position units in
table C, 1024 in A and B; see the item list above). The relation between a model unit and a position unit is set by the draw code:
the object offset is shifted right by (8 - n) bits, where n is the low nibble L of the
block byte `+6` for an object nearer than Tm (smaller for mid and far ranges), and the
vertices are shifted by 0 in that range, so one model unit is 2^(8-L) position units before
the instance scale (low nibble of the render-list entry, `V:4AB70`) is added; for the
F/A-18 (L = 3) that is 32 position units per model unit. How the instance scale is chosen
for items of the cell lists is not decoded. Feet per position unit is therefore not
established here: the number of fraction bits of the position longs relative to model units
is the same open question as in `03-3d-pipeline.md` ("Units of the 32-bit positions"); a
rough bridge-span comparison (main span of ids 48/49 against the real 4200 feet) gives a
cell of a few thousand feet to a few miles depending on the instance scale, so no figure is
claimed. Position blocks use the finer unit of 256 position units (section 9). The carrier is
a spawned object (class 0x20) at cell x 70, z 65 plus a fine offset (section 9); table C has
no cell at (70, 65) (the row of z = 65 has the single cell x = 63, list `0x47A4E`), and
the nearest cells are (x, z): (63, 65), (61, 66), (65, 66), (65, 67),
(66, 67, empty list), (61, 68), (63, 68), (66, 68), (73, 68); the carrier is therefore the
only thing in its own cell. Airfield positions are listed in section 9.

### 11. Palette and colour

The display is 320 by 200 with five bit planes. Palettes load with `LoadRGB4` on one
view port (structure at `0x1A6DA`); call sites `0x105CA`, `0x1065E` (start-up), `0x17E98`,
`0x18308`, `0x1833E`, `0x183B6`, `0x194AA`, `0x194F8`, `0x1958C` (fades). Storage (hunk 21):

| address | entries | use |
|---|---:|---|
| `0x29F24` | 32 | menu and briefing palette (text colours blue, green, red, brown, greys); copied to the load buffer by `0x13C1C` (callers near `0x13540` and `0x13676`) |
| `0x29F64` | 32 | flash palette: all 32 entries the one value `0x0FB2` (orange; the emulator shows it as (236, 159, 0), note 06 question 8; an earlier text said yellow); loaded for one frame at the end of the crash hold (note 05 17.17 rule 8) and used by the flip routine at `0x182FC` |
| `0x29FA4` | 16 steps x 16 | fade steps; step = 15 minus the fade level (`V:4A958`), each step loaded with count 16 (colours 0 to 15); step 0 is the in-flight palette |

The in-flight palette is step 0 of the fade table: entries 0 to 15 of the 16-word step
(the full values are the first 16 words at `0x29FA4`; they are not repeated here). Pen
numbers given by model colour words are indices into it. It equals the live buffer `V:4A718`
in the reference emulator snapshots (fade level 15), and the colour map of `pix/inst5` and
`pix/frnt5` except that two entries (9 and 11) are swapped in the pictures. Measured on the
captures: the sky is entry 5 (value `0x447`), the sea entry 6 (`0x036`), land green entry 4,
the engine glow (orange) entry 9, aircraft greys entries 3, 8, 10, 12, 13. The emulator maps
the 4-bit values 0 to 2 to 0, 3 to 28, 4 to 43, 5 to 56, 6 to 72, 7 to 89, 9 to 123, 13 to 197
per channel. Entries 16 to 31 are not loaded by the 16-colour fades; the pictures repeat
entries 0 to 15 there, and note 04 finds the fifth plane empty in the 3D window, so they are
not displayed in flight. Note 04 now also reads the sky as entry 5 (the earlier "colour
2" reading came from the plane masks of the sky fill; the resolved plane order is in note
04). Menu colours (blue, green, red, brown, greys) come from the separate 32-entry set at
`0x29F24`.

No separate night palette was found: the tables are the fade (16 steps, probably used for
the fade in and out) and the flash only; any day or night effect would have to come from
the sky fill (`04-scene-composition.md`), not from a second palette.

Model colours: the low four bits of a colour word index the 16 in-flight colours directly.
The F/A-18 and F-16 polygons use entries 3, 8, 10, 12, 13 (and 0); the carrier 3, 7, 8, 10,
12; engine glow polygons use 9; colour 0 (black) is used by the detail faces of operation 4.

### 12. Fonts and HUD glyphs

HUD font: hunk 61, `0x426D0`, 872 bytes. The first 64 words are a table indexed by
character code minus `0x20` (space to underscore) giving byte offsets from the start of
the hunk to the glyph. A glyph is 5 bytes, one per row from the top, pixels in bits 7 to 5
(3 columns), left to right. There are 48 distinct glyphs (upper-case letters, digits and
some punctuation); characters the font lacks (for example `"`, `#`, `$`, `%`, `&`, `'`,
`;`, `<`, `>`, `?`, `@`) all point at one shared glyph, and the space points at a blank
glyph at offset `0x1EC`. The data from `0x1EC` to the end holds further small bitmaps (symbols)
that are not decoded. The text drawer is at `0x3706A` and `0x37258` (glyph pointer built at
`0x37290`) and the string buffer at `V:4A8C2`. Verified by rendering text from the table
(the heading, speed and altitude labels with their figures read correctly; the captured readout is in
`external-view.png`). The cockpit instrument faces and green digits are part of the
panel picture (`pix/inst5`, 320 by 55, five planes), not glyphs. The menu and briefing text
of the front end is not drawn through the console and not in an Amiga ROM font (the earlier
reading, corrected by note 08): every text screen is drawn by the engine at `0x377B2` in a
second font of the executable, a 5 by 7 pixel font at `0x4283C` in hunk 61 (a table of 64
word offsets, 7-byte glyphs, characters 0x20 to 0x5F; note 08 section 3). The `con:` string at
`0x58908` is the C start-up's default console, never opened for the front end.

### 13. Recommendation for the port

Controller ruling (milestone 2): the port takes the middle route. The extractor (Task 8)
decodes each model detail level into a neutral intermediate form and the core (Task 9)
walks that form at run time. Flattening to polygon lists is rejected because it loses
painter's order, the per-side face selection and the distance gates.

Intermediate form per detail level:

1. The vertex array: block vertices in slot order, flat and type B vertices widened to
   (x, 0, z), strip-generated slots (section 2.4) and the synthesised slots of section 5b
   evaluated for state zero by the formulas of note 03 (a slot that no rule defines is marked unresolved and
   counted; none remains in the shipped models once the second vertex section of the flat blocks with flag
   bit 2, note 03 "Block set-up of the terrain models", the type B zero corners and the ops 7 and 8 rules are applied).
2. The stream as explicit nodes: for each node its kind (jump, axis, point-and-normal,
   three-vertex), the plane descriptor (vertex slot indices or point and normal, flip bit,
   scale shift) and the two child indices; draw items with their program lists.
3. Programs as items in order: gate words (g1, g2 or "always"), extra-vertex ranges, the
   state selector kind and its skip table (the extractor emits every variant so the core can
   pick one), the decoded primitive kind (point, line, polygon, quad, hatch family and their
   parameters), the vertex indices (slot numbers, not byte offsets) and the pen (low four
   bits) and, for polygons, the W rules of section 6.

Scope ruling for Task 8: fully decode id 4 (F/A-18; no blocking item), the carrier (ids 8 and
9; ops 19, 22, 32, 33, 67-69, no synthesised vertices; nothing blocking), the terrain tables
and the bridges 53-57 (strip section and ops 9-16); bridges 48-52 except the faces of ops 7 and
8 (the vertex rule of ops 7 and 8 is in note 03, "Ops 7 and 8"; 34 occurrences, decodable).
Ids 3, 6, 7 (F-16 and MiG): the slots of ops 42, 43, 44 and 59 follow the formulas of note 03 ("Vertex
synthesis"), including b+22 and b+100 of op 42; buildings decode fully with ops 34-36, 39-41 (none of 7, 8,
49, 62, 67 occurs in them). Type B patches decode completely: the zero placeholder corners are
literal vertices (section 7). An operation without a handler rule, or a slot that no rule of note 03 defines,
must raise an error naming the block address (review focus 1); the zero corners of the type B
patches are not such a slot and are exported as the vertices (0, 0, 0).

## Constants

| constant | value | where | status |
|---|---|---|---|
| object record | 20 bytes (five longs); 134 records at `0x25ECC` | hunk 16 | verified |
| model operation table | 78 longs at `0x23408`; operation word: bits 0-13 = 4 x index, bit 14 last item, bit 15 short form | hunk 13 | verified |
| vertex slot | 6 bytes (x, y, z words); 3D blocks store 6 bytes per vertex, flat blocks and type B blocks store 4 bytes (x, z) and are widened to (x, 0, z); operands are 6 x slot index | block +10 | verified |
| model axes | x lateral (+x probably left), y up, z forward | plots | y and z verified, x sign inferred |
| drawing matrices | Q8 object matrix `V:4AC7E` and camera matrix `V:4AC90` (see `03-3d-pipeline.md`) | | read from code |
| angle unit | object attitude angles: 28800 units per circle (1/80 degree; note 03); every consumer shifts right by 3 to 3600 units per circle (tenth of a degree), the unit of the lookup tables; sine table `0x436E8`: 901 words, `sin(i/10 deg) * 16384` for i = 0 to 900, all entries equal the rounded formula | hunk 63 | verified numerically |
| inverse tables | `0x42E92` 259 words (0 to 900), `0x43098` 552 words (599 to 900), `0x434E8` 256 words (850 to 900), `0x42C00` 261 words (0 to 450), all in tenths of a degree | hunk 63 | partly decoded |
| world position | 29 bits used; table A, B cell 2^24 (bits 24-28, 32x32 grid); table C cell 2^22 (bits 22-28, 128x128 grid) | `0x1F7C8`, `0x2001C` | read from code |
| object coarse position words | `+6`, `+8` hold bits 22-31 of the position longs | `0x1F5E4` | read from code |
| cell item offset | 0 to 4095 inside a cell | item lists | verified |
| visibility distance map | 64 bytes at `0x4621C`, indexed by view distance >> 7, value x 256 stored in `V:4AA6A` | `0x1FA6E` | read from code |
| HUD altitude figure | 5 x (altitude long >> 10) | `0x36F40` | read from code, unit unverified |
| HUD speed figure | magnitude of object word `+0x6E` divided by 12 | `0x36FBC` | read from code |
| polygon filler | 4 bit planes (pens 0-15) of 8000 bytes each; display 5 planes | `0x33E9C`-`0x33F24` | verified |
| palette fade | 16 steps of 16 words at `0x29FA4`; step = 15 - level | | verified |
| HUD glyph | 3 by 5 pixels, 5 bytes; 64-entry offset table | hunk 61 | verified |

## Open questions

1. BLOCKING items (counts), as resolved by note 03. (a) Op 42 slots b+22 and b+100: resolved, the formulas
   are the symbolic evaluation of the handler in note 03 ("Vertex synthesis"; `ops_synth.py`, equal to the
   rig on every slot); the polygon with slot 22 of ids 3, 6 and 7 is defined. (b) Type B patches ids 105,
   106, 109-132 (26 ids) store zero placeholder vertices in the near entry (for id 109: three of five):
   resolved, they are literal file data (section 7; nothing writes them, `modeldata_check.py`) and the
   programs name them. (c) Ops 43, 44 and 59 have non-linear slots (b+11, b+12 of op 43; b+6 of op 44;
   b+26, b+27, b+110 of op 59); resolved by the same table of note 03; no polygon of the shipped models
   uses them (checked). (d) Ops 7 and 8 (triangle faces of the bridges, ids 48-52, 34 occurrences):
   resolved, the vertex rule (choice of the variant by the sign of a test value, the operand meanings) is in
   note 03 ("Ops 7 and 8", 1,600 constructed faces). (e) Conditional: if the axis and
   point-and-normal sign conventions (open question 9) are wrong, the face selection of 14 axis
   and 16 point-and-normal nodes is blocked.
2. Roles marked "(inferred)" in section 5 are not reduced to formulas; the hatch family (ops 19-23, 32, 57) is specified exactly in note 03 ("Hatch and stripe geometry", verified in the rig). Compare drawings against a flight
   capture over the bay.
3. State selectors: which state variable (gear, damage, flaps, afterburner) each of
   operations 42-45, 50-54, 59-61, 66, 68, 70, 72, 73, 76, 77 reads, and what state 0 looks
   like (gear up or down). State bytes: object `+0x7C`, `+0x4C`, `+0x02`, status byte `V:4A8FF`.
   A cockpit capture with gear down versus up settles it.
4. Landmark names and airfield identity. Ids 48-57, 62, 102, 103 are named by shape only;
   which airport each runway cell belongs to and the orientation of runways is not decoded;
   cell lists use ids 20, 21, 23-32, run-time slots whose model depends on the scenario
   (spawner at `0x2D29C`). Resolve with captures over the Golden Gate, the bay bridge, the
   city and the airports; trace the spawner for the slot models.
5. Relief grids and mesh records in hunk 68 (coast and land): the entry encoding of the
   26-byte mesh records and which value means land or sea. Routines `0x2F234`, `0x2F25A`,
   scan lists in hunk 28. Resolve by running the routines in the rig for a synthetic position
   or with a high-altitude capture.
6. Physical units. Feet per position unit and the fraction bits of the position longs relative
   to model units (same question as in `03-3d-pipeline.md`); the instance scale `V:4AB70`
   for items of the cell lists; the HUD altitude figure (5 times the long shifted right by
   10). Resolve from the flight model (altitude change per frame at a known climb rate) or
   two HUD readouts at different positions in a capture. Milestone-3 opener: one emulator capture logging `V:4AB30` and `V:4AA6A` with the carrier at two known distances settles the view-distance scale, the LOD shift and the range unit at once. Update: measured in `01b-flight-captures.md` (altitude unit 1 ft = 204.8 pos, HUD altitude = 5 * (y >> 10), camera offsets 256 pos per unit; instance scale and range unit still open).
7. Settled (note 08 sections 1 and 3, Task 1): the front-end font is the game's own 5 by 7 font at `0x4283C`
   (hunk 61); the `_Write` sites at `0x184A8` and `0x1861C` write the player record to the log file
   and no console is used; the menu, credits and log-disk captures equal the engine's output pixel for pixel.
8. Handedness is by inference (note 03 gives camera x to the left; the carrier island is at
   negative x). A capture of a lateral marker (a wing light) or a roll command settles it. Settled by a roll capture, see `01b-flight-captures.md` (x west, y up, z north is right-handed; roll right is negative).
9. Settled: the axis and point-and-normal tests are verified word for word in the rig, with the camera variables they read (note 03, "The side tests of the block tree", `bsp_check.py`).
10. Extra-vertex offset base inside type B vertex blocks (ids 39, 99, 100), the leaf bit 13
    tier gating, flag bits 2, 4, 5, 7 of blocks and the relative-group section of flag 4 (ids 46,
    72, 73, 95), record fields 3 and 4 beyond the cockpit alternate list, and the unprobed
    ops 49 and 62 (op 67 is decoded; ops 7 and 8 were probed but not reduced, see question 1).
11. Hunk 60 (which two tables, what indexes them), hunk 62 (symbol mask format) and the data
    after offset `0x1EC` in hunk 61.

12. Extractor finding (Task 8, real data): a one-sided polygon whose W has bit 12 set and bits
    10-11 clear is followed by one extra offset word pointing at a 12-byte plane record. The
    decoder places that word before the back-colour word; the placement is unverified. Without
    the word programs misalign; with it the 23 checked programs tile exactly. Section 6 does
    not yet describe it.
13. Extractor finding: ops 54 and 76 take no argument word; the "one word" is the next
    last-flagged nop, which the state-zero path skips. The state-zero straight path must therefore
    follow the selector skips and not stop at the first last-flagged item (section 4 and the
    rows of ops 54 and 76 should be read that way).
14. Extractor finding: the strip backward pass starts at end-3 (end-1 with bit 15 of D) where
    "end" is the slot one past the forward chain; the parity-consistent reading is what the
    decoder uses. Section 2.4's "end of the forward chain" is ambiguous in the same way.
15. Extractor finding, settled: ids 0, 2, 10, 11 and 13 use vertex slots built by ops 38, 45, 56, 63, 64 and 65, which are not state ops: they have no operand and fixed formulas (note 03, "Fixed-formula synthesis without an operand"); the extractor must apply them; and the far levels 2 and 3 of id 4 reference op-43 and op-44
    slots 11 and 12 (the note called them unused by any polygon). Those op-4 faces are
    distance-gated, so they may be unreachable at far levels; settle by the gate values.
16. Extractor finding: type B id 38's chain head is `0xFFFF`, so it has no levels (consistent with
    the catalogue giving it no entry).
17. Node counts: the note's 84 three-vertex, 16 point-and-normal, 14 y-axis and 4 x-axis nodes
    were counted as occurrences summed over every detail level of every model (shared streams
    counted once per level, both branches walked from each level start). Counted by distinct
    node address the same walk gives 41, 16, 7 and 4. The extractor found 57 three-vertex and 8
    y-axis nodes, which matches neither; the difference (probably per-id counting of shared
    models and the jump nodes) must be settled by comparing the two sets of node addresses,
    because a branch missed by the walker would drop faces silently.
18. Extractor finding: type B extra-vertex items are decoded at stride 4 (an assumption on
    question 10; unverified).

## Verified by

No Musashi rig test exists for this slice. The evidence by claim:

- Record table, handlers, operation table, container grammar (sections 1 to 5), strip
  section, type B grammar and item-list grammar were decoded with throwaway scripts under
  `extracted/scratch/t5` (not committed) from `extracted/interceptor.bin`, which equals
  the corrected-relocation image `extracted/scratch/t7/fixed.bin`. Argument lengths of
  the operations were measured by running every handler in a Unicorn 68020 emulator over
  the real stream words with all state memory zero, and the draw calls were intercepted
  to read polygons and lines. Coverage check: for the 23 ids 1, 33, 46, 48, 49, 50, 58,
  62-65, 67, 72, 73, 81, 86-88, 91, 92, 95, 104, 133 the programs reached through the
  tree (walked over the union of both branches, which the renderer never does in one frame) tile the program area with no gap; the other ids have gaps that are the state
  variants of section 4, which the walk does not enter.
- Geometry: ids 0-15, 33-35, 43-97, 102-108 and 133 were rendered as wireframes and filled
  drawings from the decoded data. The F/A-18 (id 4) was compared with
  `extracted/captures/external-view.png` and `demo-deck.png`: twin canted fins, swept
  wings with leading-edge extensions, grey body colours (palette entries 8, 10, 13) and
  orange engine glow match, and an oblique rendering from behind and left reproduces the
  silhouette of the external view; the rear view shows the V of the fins as in the demo
  capture. The F-16 (id 3) shows its single fin and gear in the plots. The bridge
  models show deck line, cable arch and hangers; no capture of any landmark exists, so
  those names are unconfirmed.
- Image identity: the record of id 4 (`0x25F1C`) was also read through rea on the regenerated
  ELF (`extracted/interceptor-arm.elf`, 676 procedures, language `68000:BE:32:default`) and
  matches the Python reads of `extracted/interceptor.bin`.
- Operation geometry (section 5) and vertex synthesis (section 5b): each handler was run in
  the emulator with random vertex arrays (multiples of 16 so that halves are exact), the
  points handed to the polygon, line and point drawing routines were captured and every
  output coordinate was fitted against the input slots by least squares; all quoted formulas
  fit with zero error over 120 trials. Coverage of ids per operation comes from a parse of all
  programs. This is a scratch aid (`extracted/scratch/t5/probe*.py`), not a rig test.
- Position blocks (section 9): decoded from the data and the spawner code; the carrier and
  player-start values are read, not checked against a capture.
- Sine table: all 901 entries recomputed and compared (no mismatch).
- Palette: the 16 colours equal the live colour buffer read from the reference-emulator
  snapshots `extracted/scratch/t7/a1.vasnap` and `extracted/captures/demo.vasnap` (buffer
  `V:4A718`, fade level 15); the colour map of `pix/inst5` equals them except for entries
  9 and 11; sky, sea, land, aircraft and
  menu colours were measured on `cockpit-takeoff.ppm`, `external-view.ppm`, `menu.ppm`.
- Terrain: item lists, rows and the three index tables decoded from the image; every row
  pointer lands on a list that ends in `0xFF`; the three tables cover the same region.
  Not confirmed by a capture of the world.
- Font: glyphs rendered as text (legible); the drawer code reads exactly this table.
- Everything marked "read from code", "inferred", "probable" or "unverified" is not
  confirmed by a rig test or a capture. The classification of hunks 60, 62, 65 and 68 is
  from the code that reads them.
