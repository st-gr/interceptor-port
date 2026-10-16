# 3D pipeline: rotation, camera, projection, clipping, ordering

Scope of this note: the arithmetic of the 3D pipeline, from an object's attitude angles to
polygons ready for the filler. Model and terrain data formats belong to
`02-assets-and-tables.md`; the 320x200 raster layout, palette and view kinds are not
covered here (`04-scene-composition.md` covers them; see also Open questions). Addresses are flat-image addresses as in
`00-binary-layout.md`; the hunk is given with each routine. Everything marked
"verified" was run in the Musashi rig (`tools/m68k-rig`) against the relocation-fixed image and
reproduced by an independent re-implementation written from this text (section
Verified by).

All code is hand-written 68000 assembly with register-based calling conventions. Unless
stated, a routine clobbers only the registers it lists as outputs plus D6/D7/A0 scratch.
"Word" is a signed 16-bit value; products are signed 16x16 to 32 bits, shifts to the
right are arithmetic (round toward minus infinity), and a result stored as a word keeps
only its low 16 bits.

## Location

### Routines

| Address | Hunk | Role | Status |
| --- | --- | --- | --- |
| 0x32CCE | 32 | `sincos2`: sine and cosine of two angles (tenth-degree units) | verified |
| 0x32DB2 | 32 | `sincos1`: sine and cosine of one angle | verified |
| 0x32B52 | 32 | `fx_matrix_a`: attitude angles to 3x3 matrix, Q14 | verified |
| 0x32BEC | 32 | `fx_matrix_b`: second Euler order, Q14 (used with negated angles for the inverse) | verified |
| 0x32A66 | 32 | `fx_cam_yp`: two-angle camera matrix, Q8 | verified |
| 0x32AB6 | 32 | `fx_cam_ypr`: three-angle camera matrix, Q8 | verified |
| 0x32A1E | 32 | `fx_roll_q8`: single-angle roll matrix, Q8 | verified |
| 0x32A48 | 32 | `fx_roll_q14`: single-angle roll matrix, Q14 | verified |
| 0x32C84 | 32 | `fx_rowscale`: scales the three rows of a Q8 matrix by the zoom triple at 0x4AAF6 | verified |
| 0x325B8 | 32 | `fx_compose`: multiplies a matrix by the matrix of three angles and extracts angles again (the flight attitude step, note 05 section 6, and the head views of the cockpit camera, note 06) | verified (rig, 2,000 flight matrices, 600 head records) |
| 0x32074 | 32 | per-frame camera matrix setup (entry); 0x32092 external views (aim 0x1454A), 0x321F0 cockpit (head views through 0x325B8) | verified (rig `view_check.py`) |
| 0x1454A | 5 | external camera aim: target pitch and heading of a vector, and the turn-limited step toward them; results in `V:4AB78`, `V:4AB7A` | verified (rig) |
| 0x1F1D8, 0x1F45E | 4 | external camera longs; the per-frame camera set-up (cockpit eye offset or the external longs) | verified: the eye-offset branch of 0x1F45E in the rig (`eye_check.py`); 0x1F1D8 and the external branch of 0x1F45E in the rig (`view_probe.py extlongs`, 200 random ring positions: the longs, the words and the height long) and on 13 external and 3 map snapshots (`verify_views.py`) |
| 0x2D756 | 26 | external camera position: the ring offset by class, direction and level, rotated and added to the viewed record | verified (rig; note 06 view item 4) |
| 0x29B12, 0x29E44 | 14 | square root by Newton iteration of `V:4AC1C` (result `V:4AC20`); the rounded word quotient of `V:4AB84` by `V:4AB88` (result `V:4AB8A`) | verified (rig, inside the aim model) |
| 0x3202C | 32 | per-object attitude: stores angles, builds the two object matrices | unverified |
| 0x1D478, 0x1D48E | 4 | rotate a body-frame vector by an object matrix, add the object position | unverified |
| 0x21C44 | 10 | model draw program interpreter (vertex transform, face tree walk, command dispatch) | read only |
| 0x22002-0x2203A | 10 | inline: rotate one model vertex by the object matrix | verified (snippet) |
| 0x22072-0x220AC | 10 | inline: apply the camera matrix to one vertex | verified (snippet) |
| 0x22A7A, 0x22ACC | 10 | face visibility (back-face) test from three camera-space vertices | verified |
| 0x22A84, 0x22A94 | 10 | side test from a stored point and normal (flag bit 13); entry 0x22A84 routes to the three-vertex test when bits 12 and 13 are clear | verified (`bsp_check.py`) |
| 0x22B3A | 10 | axis-aligned split test (bounding plane side) | verified (`bsp_check.py`) |
| 0x1F45E | 8 | camera position: the viewed record's position plus the rotated cockpit eye offset; V:4AB1A.., V:4AB2A.., V:4AB30 | verified (`eye_check.py`) |
| 0x33936 | 13 | clipped line (plane cascade, intersections, projection) | verified (`line_model.py`) |
| 0x225F0, 0x227FE | 10 | tree walk of the stream, program interpreter (item gates, handler call, abandonment by condition codes) | verified (return combination) |
| 0x22892, 0x22BDE | 10 | extra vertices of a gated item; second vertex section of flat blocks (flag bit 2) | verified (vertex table) |
| 0x22CFC, 0x22D0A | 12 | type B drawers (level chain, cut-offs, vertex block, programs) | verified (levels, cut-offs, returns) |
| 0x23408 | 13 | table of 4-byte entries: the draw command handlers (return words and condition codes: Format, "Draw operations") | verified |
| 0x2377C | 13 | handler: general polygon, builds the clip input and calls the clipper | verified |
| 0x3455E, 0x340D4, 0x340EA, 0x3414E, 0x33CAC | 36, 35 | line routine, three point sprites, disc | sprites and disc verified (pixels); line verified (pixels against the rig's blitter model, "The raster line") |
| 0x28B6A | 19 | `fx_clip`: clip a polygon to the view pyramid and project it | verified |
| 0x34A28 | 36 | polygon filler (blitter area fill) | interface only |
| 0x3377C | 35 | `fx_project`: perspective projection of one point, no drawing | verified |
| 0x33780, 0x33788-0x33794 | 35 | same projection, then draw a point sprite (size by distance) | read only |
| 0x3385C, 0x33936 | 35 | project and draw a 3D line (unclipped, clipped variants) | read only |
| 0x20884 | 8 | `fx_dist`: approximate length of three non-negative components | verified |
| 0x2082A | 8 | `fx_distkey`: camera-relative distance used as the object ordering key | verified |
| 0x2122C, 0x213AA | 9 | per-group object ordering: build keys, sort farthest first | 0x213AA verified |

### Tables

| Address | Hunk | Size | Content |
| --- | --- | --- | --- |
| 0x436E8 | 63 | 901 words | sine, quarter circle, 0.1 degree steps, amplitude 16384 |
| 0x42E92 | 63 | 257 words (+2 stray) | arcsine of i/256 in 0.1 degree units (index 256 = 900) |
| 0x43098, 0x434E8 | 63 | not measured | finer arcsine tables used near +-1 by `fx_compose` |
| 0x208E8 | 8 | 257 words | hypotenuse correction, 16384*sqrt(1+(i/256)^2) |
| 0x20E4A | 9 | 240 bytes | level-of-detail shift versus coarse distance |
| 0x4621C | 65 | bytes starting 64,68,72,... | detail threshold versus distance (use unverified) |

### State used by the pipeline (work RAM)

| Address | Hunk | Size | Meaning |
| --- | --- | --- | --- |
| 0x4B23C | 71 | 16 records of 0x200 bytes | object table; record n at 0x4B23C + 0x200*n |
| record +0x66, +0x68, +0x6A | 71 | 3 words | attitude angles, 28800 units per circle |
| record +0x80 | 71 | 9 words | matrix from `fx_matrix_a` of the three angles (Q14) |
| record +0x92 | 71 | 9 words | matrix from `fx_matrix_b` of the negated angles (Q14) |
| record +0x14, +0x18, +0x1C | 71 | 3 longs | position |
| 0x4AC90 | 71 | 9 words | camera matrix, Q8, row major; rows give camera x, y, depth |
| 0x4AC7E | 71 | 9 words | object matrix for the model being drawn (record +0x92 shifted right by 6, Q8) |
| 0x4AAF6 | 71 | 3 words | zoom per row of the camera matrix, 0x100 = 1.0 |
| 0x4AB70 | 71 | word | current level-of-detail shift: stored by the walkers before each draw call (the low nibble of the queue entry for queues A, B and C at `0x1FAB8` and `0x1FD76`; the table value for the direct entities at `0x1FCCE`; per slot in the landmark slot filler at `0x21286`), read by the distance key, the drawer and the part id eye test (`04a` section 8a); it is 0 for the player's own entry in the cockpit |
| 0x4AB2A, 0x4AB2E, 0x4AB30 | 71 | word, word, long | camera offsets added to an object's offsets by the distance key (sign convention: they are added) |
| 0x4D43C | 72 | 6 bytes per vertex | camera-space vertex table (x, y, depth words) |
| 0x5103C | 72 | word, word, 3 words per vertex | clipper input: shift, count, vertices |
| 0x5043C | 72 | word, 2 words per vertex | clipper output / filler input: count, (x, y) pairs |
| 0x4AA0C | 71 | word | polygon colour for the filler |
| 0x4AA10 | 71 | 2 words | last projected screen point, long -1 when rejected |
| 0x53818 | 73 | 22 words | ordering keys of the current group; 0x53870 holds 0..21, 0x538C8 receives the sorted indices |
| 0x5476A, 0x53A4A, 0x540DA | 73 | 24-byte entries | draw queues: flags word, model pointer, position words, ... |

All addresses in this note are flat-image addresses (the relocated image of `00-binary-layout.md`), never
hunk-relative offsets. The state area lies in the zero-filled CODE-typed hunks 71 to 73 (work RAM).

## Format or Behaviour

### Angle unit and sine table

Object attitude angles (record +0x66, +0x68, +0x6A) use **28800 units per full circle**
(1 unit = 1/80 degree, 0x7080 = 360 degrees). Every routine that consumes them first
shifts them right by 3, giving **3600 units per circle (0.1 degree)**, valid range
0..3599; a caller must pre-reduce angles into 0..28799 (negative values are not
handled). Where the code needs the inverse rotation it passes `28800 - angle` (zero
stays zero).

The sine table holds sin(i * 0.1 degree) * 16384, rounded to nearest, for i = 0..900 (all
901 entries agree with that formula). Sine of any angle a in 0..3599 is obtained by
symmetry: a up to 900 reads entry a; up to 1800 reads entry 1800 - a; up to 2700 reads
minus entry a - 1800; otherwise minus entry 3600 - a. Cosine is sine of a + 900 with the
same folding. Results are **Q14** (16384 = 1.0). `sincos2` takes angles in D0 and D2 and
returns sin D0, cos D0, sin D2, cos D2 in D0, D1, D2, D3; `sincos1` takes D4 and returns
sin, cos in D4, D5. (Both expect the angle already shifted right by 3.)

### Object matrices (fx_rotate)

`fx_matrix_a` takes the three attitude angles A, B, C (record +0x66, +0x68, +0x6A,
28800 units) in D0, D2, D4 and writes nine words in row-major order to the address in A1.
With sA, cA, sB, cB, sC, cC the Q14 sines and cosines of A, B, C (after the shift) and
`*` followed by "shifted right 14" abbreviated as `x`:

| Element | Formula |
| --- | --- |
| m0 | ((sB x sA) * sC + cB * cC) >> 14 |
| m1 | -(cA x sC) |
| m2 | (sB * cC - (cB x sA) * sC) >> 14 |
| m3 | (cB * sC - (sB x sA) * cC) >> 14 |
| m4 | cA x cC |
| m5 | ((cB x sA) * cC + sB * sC) >> 14 |
| m6 | -(sB x cA) |
| m7 | -sA |
| m8 | cB x cA |

where `p x q` means (p * q) >> 14, evaluated as a 32-bit product shifted, and the inner
`x` result is used as a 16-bit word. With two angles zero the matrix is a rotation about
one axis: A alone rotates about the model X axis (m4 = cA, m5 = sA, m7 = -sA, m8 = cA);
B alone about Y (m0 = cB, m2 = sB, m6 = -sB, m8 = cB); C alone about Z (m0 = cC, m1 = -sC,
m3 = sC, m4 = cC). All angles zero gives the identity (16384 on the diagonal).

`fx_matrix_b` has the same interface, and in the per-object setup it is called with the
negated angles; its elements are

| Element | Formula |
| --- | --- |
| m0 | (cC * cB - (sC x sA) * sB) >> 14 |
| m1 | -((sC * cB + (cC x sA) * sB) >> 14) |
| m2 | cA x sB |
| m3 | sC x cA |
| m4 | cC x cA |
| m5 | sA (Q14, not shifted) |
| m6 | -((cC * sB + (sC x sA) * cB) >> 14) |
| m7 | (sC * sB - (cC x sA) * cB) >> 14 |
| m8 | cA x cB |

and, fed the negated angles, equals the transpose of `fx_matrix_a` of the original
angles to within about 63/16384 (a bit of angle truncation). So per object: record
+0x80 is the matrix of the angles and record +0x92 is (approximately) its inverse.
`0x3202C` stores the three angles in the record, calls `fx_matrix_a` into +0x80, then
calls `fx_matrix_b` with the negated angles into +0x92.

Applying a matrix to a vector (v = x, y, z words): out_i = (m[3i] * x + m[3i+1] * y +
m[3i+2] * z) >> 8, one 32-bit sum, stored as a word. The model-vertex loop uses the
record +0x92 matrix converted to Q8 by shifting each element right by 6 (copied to
0x4AC7E), so the multiplication result stays in the units of the model coordinates.
**fx_rotate** is this operation.

`fx_compose` (0x325B8) builds the Q14 matrix of angles D0, D2, D4, multiplies the matrix
pointed to by A4 by it (Q28 long sums, nine longs at 0x4AC5A), and extracts three
angles from the product with the arcsine tables, returned in D4, D5, D6 in 28800-unit
form. With A4 = identity, inputs (1000, 2000, 3000) return (992, 2000, 3000), so it is a
matrix-to-angles inverse of `fx_matrix_a`; for large angles it returns an equivalent
different triple (see example). Callers: 0x27570, 0x31DD8 (the flight attitude step), 0x32366 (the head views of the cockpit camera, "Camera attitude in the head and external views").

### Camera matrix

The camera matrix at 0x4AC90 is Q8 (256 = 1.0), row major, and is applied in the same
way as the object matrix (out = sum >> 8). Its three output components are camera x, camera
y and **depth** (z, positive in front of the viewer). It is rebuilt every frame by
0x32074: `fx_cam_ypr` (angles from 0x4AB42, 0x4AB46, 0x4AB4A, the view attitude
composed from the own aircraft record and the view direction) or `fx_cam_yp` writes it,
then `fx_rowscale` multiplies each row by one of the zoom words at 0x4AAF6 (a product
shifted right by 8, so 0x100 leaves the row unchanged). `fx_roll_q8` / `fx_roll_q14` build
a pure rotation about the middle axis from one angle (horizon/roll matrix at 0x4ACB4).

`fx_cam_yp` (angles A in D0, B in D2, 28800 units; sA, cA, sB, cB Q14 after the shift;
`x20` means product shifted right by 20 bits) writes:

| Element | Formula |
| --- | --- |
| m0, m1, m2 | cB >> 6, 0, sB >> 6 |
| m3, m4, m5 | -(sB * sA x20), cA >> 6, cB * sA x20 |
| m6, m7, m8 | -(sB * cA x20), (-sA) >> 6, cB * cA x20 |

`fx_cam_ypr` (A, B, C in D0, D2, D4; Q14 values of A, B, C as above, products shifted by
20 unless noted):

| Element | Formula |
| --- | --- |
| m0 | (cB * cC + ((sB * sA) >> 14) * sC) >> 20 |
| m1 | -(cA * sC >> 20) |
| m2 | (sB * cC - ((cB * sA) >> 14) * sC) >> 20 |
| m3 | (cB * sC - ((sB * sA) >> 14) * cC) >> 20 |
| m4 | cA * cC >> 20 |
| m5 | (sB * sC + ((cB * sA) >> 14) * cC) >> 20 |
| m6 | -(sB * cA >> 20) |
| m7 | -(sA >> 6) |
| m8 | cB * cA >> 20 |

`fx_roll_q8` (angle in D4, 28800 units, s and c its Q14 sine and cosine): m0 = c >> 6,
m2 = s >> 6, m4 = 256, m6 = -(s >> 6), m8 = c >> 6, other elements zero.
`fx_roll_q14`: m0 = c, m2 = s, m4 = 16384, m6 = -s, m8 = c, other elements zero.

`fx_rowscale` (matrix in A1, scale words at 0x4AAF6): each element of row r becomes
(element * scale[r]) >> 8 stored as a word.

**Roll sign and the angle inputs of the camera matrix (V: `roll_check.py`, 500 random attitudes, 0 differences).** In the cockpit view (`V:4A83D` zero, the view that the player flies), the camera set-up 0x321F0 hands `fx_cam_ypr` the angles A, B, C = the viewed record's words +0x66, +0x68, +0x6A **unchanged**: `V:4AB42`, `V:4AB46`, `V:4AB4A` receive them without negation or offset
(the head and external views: "Camera attitude in the head and external views" below). The nine words at `V:4AC90` equal `fx_cam_ypr(A, B, C)` row-scaled by the zoom words (0x100 leaves them unchanged), and differ from `fx_cam_ypr` with the roll negated in every banked state. The roll convention is therefore fixed by the matrix: the record's roll word is **right wing down negative**, so a positive C is a **left bank**.
Check on the matrix (pitch and heading 0): the image of the world's up vector (0, 1, 0) is the camera-space triple (m1, m4, m7) = (-sin C, cos C, 0) in Q8, so a positive C moves up toward camera -x; camera +x is the screen's left (the projection mirrors x), hence toward the screen's right: the horizon turns clockwise, as it does for a left bank (C = 3600 units, 45 degrees: m0 = m4 = 181, m1 = -181, m3 = 181).
Equivalent statement for a port that keeps a roll R with right wing down positive (the HUD's and the horizon's convention): the camera matrix is `fx_cam_ypr` with C = -R, i.e. the record word. **The port needs no flip of the scene roll: feed the roll word as C and negate it only for variables that are defined right-wing-down positive.**

**Camera position and the cockpit eye offset (V: `eye_check.py`, 300 random records, 0 differences).** The camera set-up `0x1F45E` runs once per frame. In the non-external view (`V:4A83D` zero) the camera is the viewed record's position plus an eye offset fixed by the record's class byte (`+0x62`), in body axes (x, y up, z forward) and in units of 256 position units (1.25 ft):
class 0x30: (0, 1, -5); class 0x11 (the F/A-18): (0, 5, 20); every other class: (0, 4, 18) (the F/A-18's eye is 6.25 ft above and 25 ft ahead of the record origin). The offset is rotated by the record's nine Q14 words at +0x92 (the matrix of the record's inverse attitude, `fx_matrix_b`; the first output from the words +0x92, +0x94, +0x96, the second from +0x98, +0x9A, +0x9C, the third from +0x9E, +0xA0, +0xA2, each a
32-bit sum of the three products with (x, y, z) of the offset, shifted right arithmetically by 6) and added to the position longs (+0x14 x, +0x18 y, +0x1C z) unmasked into `V:4AB34`; the camera longs `V:4AB1A`, `V:4AB1E`, `V:4AB22` are the **negated** sums in which the x and z position longs are first masked to 22 bits (the rotated offset is added after the mask). `V:4AB2A` (x), `V:4AB2C` (y), `V:4AB2E` (z) are those longs shifted right by 8, kept as words, and `V:4AB30` is the long of the y value shifted right by 8
(the height the drawer's gates use). In the external view `0x1F45E` takes the camera from `0x1F1D8` ("External camera longs" below); the eye offset applies to the non-external views only. A port without the eye offset places the cockpit camera at the record origin: the F/A-18's camera is then 25 ft behind and 6 ft below the original's, which moves the horizon and the near scenery of the carrier-deck frame.

### Camera attitude in the head and external views (0x32074)

The per-frame entry `0x32074` has two branches (rig `view_check.py`: head 600 states, caller 960 states, 0 differences; emulator `views-m3-airborne-heads.txt`):

- **Cockpit view** (`V:4A83D` zero, 0x321F0): builds the horizon matrix `V:4ACB4` (`fx_roll_q8` of the record's heading `E+0x68`) and the record matrix `V:4ACA2` (`fx_cam_ypr` of `E+0x66`, `E+0x68`, `E+0x6A`), picks the head triple by the view code `V:4A85F` and the record class, builds `V:4ACD8` (`fx_cam_ypr` of the triple, rows scaled) and, **unless the code is 0**, composes: the camera angle longs `V:4AB40`, `V:4AB44`, `V:4AB48` are the three outputs of `fx_compose` (0x325B8) with the matrix at `E+0x80` as input A and the head triple (pitch, yaw, roll) as the three angles, so the **camera attitude = head rotation applied to the aircraft's own axes** (the product of the head matrix and the attitude matrix, then the angle extraction by the inverse tables). For code 0 the longs are the record's own angle words. `V:4AB42`, `V:4AB46`, `V:4AB4A` (the low words) are the camera pitch, heading, roll; the camera matrix `V:4AC90` is `fx_cam_ypr` of those words with the rows scaled by the zoom words (the cockpit case of "Roll sign" above). The head triples, the 7 degree level test and the class 0x30 tables are in note 06 ("Head views"); the order of the composition is pinned by the rig: a 90 degree head turn (triple (0, 7200, 0)) of an aircraft pitched -30.5 degrees gives camera (0, heading + 7200, pitch as roll), which only the head-after-aircraft order produces (the camera words of 7 emulator states and 600 random records equal the routine's output; a sum of the head yaw and the aircraft heading is wrong for any pitched or banked aircraft).
- **External view** (`V:4A83D` nonzero, 0x32092). **Two flags, two bytes** [V: rig `view_check.py caller`; R for the writers named]. `V:4A86C` is the **look-at byte**, signed: 0 = nothing is aimed, 1 = the pair is turned toward the target with the turn limit, 0xFF (negative) = the pair snaps to the target on every frame (the crash-sequence state routines store 0xFF at 0x12198 and 0x12212 [R]; KP-Enter, the M-in-map exit, the new flight (0x1D4F6) and the routines 0x2D946, 0x286A0, 0x289A0 store 1; the M handler that plants the map camera and 0x2DC3C store 0). `V:4A86D` is a **separate flag, "the ring camera is on"**: 0x2D756 plants the ring camera only when it is set, the level keys act only when it is set (clear: the bracket keys are the cockpit zoom keys), and the caller below snaps when it is clear; KP-Enter, the M handlers and the flight start set it to 1, the shift KP-Enter fixed camera (0x1E48E) and some state routines clear it. With `V:4A86C` zero the matrix is built from the stored aim pair `V:4AB4C` (pitch), `V:4AB4E` (heading) and nothing is aimed (the map view: 7200 and 0). With `V:4A86C` nonzero the **target record** is the record at the offset `V:4A996` from the record table, or, when that offset is 0, the record at the offset `V:4AA7A` if its active bit (bit 6 of the byte at +1) is set and its whole class byte is 0x30 (the ejection seat), else record 0. **The target point** (`0x1D478`, the entry of `0x1D488`; V: rig `view_probe.py point`, 600 random states with the classes 0x10, 0x11, 0x12, 0x14, 0x20, 0x30, 0x31 and 0x3F, both camera modes, ring-sized and far camera offsets, 0 differences; emulator first frame 720): the offset triple w is (0, 0, 1), or (0, 0, 5) when `V:4A966` is 6, or (0, 0, -4) when the **high nibble** of the target record's class byte (`+0x62`) is 3 (a nibble test, not the whole byte: 0x31 and 0x3F take it too). **Order of the tests**: the class nibble is tested first (0x320C4 to 0x320CC); a nibble of 3 takes (0, 0, -4) whatever the camera mode, and only otherwise is the mode tested (6 gives (0, 0, 5), else (0, 0, 1)) [V: rig `view_probe.py round3`: class 0x30 in mode 6 matches only (0, 0, -4); class 0x11 in mode 6 only (0, 0, 5)]. It is rotated by the **nine Q14 words at +0x92 of the target record** (the same full attitude matrix as the ring directions 0 and 4: the first output from the words +0x92, +0x94, +0x96 and so on, each a 32-bit sum) and shifted right by **4** (arithmetic), so it is in the **ring unit of 1024 position units**, not the 256-unit eye offset of the cockpit camera (above, shift 6); the three results are added to the record's position longs `+0x14`, `+0x18`, `+0x1C`. The vector is that point minus the camera position longs `V:4ACF6`, `V:4ACFA`, `V:4ACFE` (32-bit wrap), and the aim routine below turns the pair toward it. Read in the eye unit (shift 6) the offset would give 760 instead of 720 for the first frame of the ground snapshot, and another pair in 373 of the 600 random states. The matrix is then `fx_cam_yp` of the pair (two angles, no roll) with the zoom rows, and the horizon matrix `V:4ACB4` is the roll matrix of the heading `V:4AB4E`. The longs `V:4AB40`, `V:4AB44`, `V:4AB48` hold the words `+0x66`, `+0x68`, `+0x6A` of **record 0** in every external view. **The limit selection** (0x32106 to 0x3214A, then 0x3215C): the limit is -1 (snap) when `V:4A86C` is negative, or, after the pause test, when `V:4A86D` is zero; 560 when `V:4A966` is nonzero (this branch leaves `V:4AB18` alone); otherwise the key word (`V:4A996` ORed with `V:4A96A`) is compared with `V:4AB18`: equal gives 2000, different stores it in `V:4AB18` and snaps. While `V:4A866` (the pause) is nonzero the aim is not updated and the stored pair is used.

### External camera longs (0x1F1D8)

In an external view the camera set-up `0x1F45E` runs `0x1F1D8` and then takes the **camera position longs `V:4ACF6` (x), `V:4ACFA` (y), `V:4ACFE` (z)** (set by the ring camera 0x2D756, note 06 view item 4, or by the map and tower set-ups) into `V:4AB34`, `V:4AB38`, `V:4AB3C` unmasked. The camera longs `V:4AB1A`, `V:4AB1E`, `V:4AB22` of the draw stage are the **negated** position with x and z masked to 22 bits first: `V:4AB1A` = -(x and 0x3FFFFF), `V:4AB1E` = -y, `V:4AB22` = -(z and 0x3FFFFF), which are the three longs `V:4ACEA`, `V:4ACEE`, `V:4ACF2` (written together with the position by 0x2DCCA, 0x1D3F2 and the map set-up) copied unchanged; `V:4AB2A`, `V:4AB2C`, `V:4AB2E` are those longs shifted right by 8 as words and `V:4AB30` the long of the height shifted right by 8. **There is no eye offset in an external view** (the camera is the ring position). The branch of `0x1F1D8` entered when the byte `V:4A864` is nonzero (a camera pulled toward the viewed record to a distance of 512 x 256 position units, by the distance estimate 0x20884) is **dead**: `V:4A864` is toggled only by bit 4 of `V:4A97E` (0x14338), which nothing sets, and it is 0 in every snapshot [R]; the port needs the first case only. Verified on 13 external and 3 map snapshots (`tools/hud-emu/verify_views.py`: the longs, words, the unmasked copy and the record-0 angles, 0 failures).

### External camera aim (0x1454A)

The aim routine takes six longs (the current pitch and heading words, the camera-to-target vector dx, dy, dz in position units and the limit; arguments in the order named) and leaves the new pair in the words `V:4AB78` (pitch) and `V:4AB7A` (heading); the caller copies them to `V:4AB4C`, `V:4AB4E`. Angles are in the 28800 unit; **positive pitch is down** (the target below the camera), heading grows clockwise as seen from above (the rule below). The rules (rig `view_check.py target` 6,000 vectors per seed (n x 10 with n = 600) against the model written from this text, 0 differences; `aim` 3,000 states, `caller` 960 states, 0 differences):

1. *Target.* Take the absolute values ax, ay, az and keep the signs (flags: dx negative, dy negative, dz negative). Let m be the largest. If m exceeds 0x10000000 all three are first shifted right by 3. The pair (S, T) is (14, 0) for m above 0x2000000 (and after the shift above), (12, 2) above 0x800000, (10, 4) above 0x200000, else (8, 6). Let x' = ax >> S and z' = az >> S (low words).
2. *Heading target h.* If x' is at most z' (signed word compare): h = 3600 when z' is 0, else the index i = (rounded quotient of (ax << T) by z') >> 6 (arithmetic) and h = 8 x `Tab`[i]; otherwise (x' above z') h = 8 x (900 - `Tab`[i]) with i = (rounded quotient of (az << T) by x') >> 6. `Tab` is the table of 261 words at 0x42C00 (indices 0 to 260 hold 0, 0, 5, 5, 10, 10 ... 445, ... 450: tenths of a degree of the arctangent of i/256, with the image's own rounding, up to 450 = 45 degrees from index 256). **The index runs from 0 to 511, not to 260** [V: rig `view_probe.py atan` and `view_check.py target`]: the test x' <= z' is made on the truncated words, so ax can exceed z' << S by up to (1 << S) - 1 and the rounded quotient reaches 32767 (z' of 1), index 511; 537 of the 100,000 random vectors of `view_probe.py atan` (seed 1) had an index above 260, the largest 511. The words past index 260 are the image words that follow the table (from 0x42E0A: small positive words of the next table, the inverse sine); **the port reads the words by address from the trig span** (`flight_span_trig.bin`, image 0x3AE92 to 0x49A12), 512 words from 0x42C00, and does not clamp the index. Example: the vector (-1000, 0, 1000) has S = 8, T = 6, x' = z' = 3, the quotient 64000 / 3 = 21333 remainder 1 rounds to 21334, the index is 333 (a word past the table, value 9), h = 72, the dx flag alone leaves it, and the pitch target is 0 (y' 0, the dy flag clear gives 28800, then 0): the rig returns the pair (pitch 0, heading 72). Quadrant: with the dx flag as bit 0 and the dz flag as bit 2: both set: h = 14400 - h; dz set only: h = h + 14400; neither: h = 28800 - h; dx only: unchanged. A result of 28800 or more becomes 0.
3. *Pitch target p.* The horizontal length H is the square root (below) of the sum of the squares of x' and z' (each a signed word product, a 32-bit sum). With y' = ay >> S (low word): if y' is at most H: p = 3600 when H is 0, else p = 8 x `Tab`[q] with q = the rounded quotient of (y' << 8) by H; otherwise p = 8 x (900 - `Tab`[q]) with q = the rounded quotient of (H << 8) by y'. If the dy flag is clear (the target is at or above the camera) p = 28800 - p; a result of 28800 or more becomes 0. (So the target pitch of a chase camera behind and above the aircraft is the table value, positive: looking down.)
4. *Rounded quotient* (0x29E44): the signed word quotient q and remainder r of the 32-bit dividend by the 16-bit divisor d (68000 DIVS); **one rule**: when |d| >> 1 is not above |r| (that is, |d| >> 1 <= |r|) the quotient is moved by one, minus one when q is negative and plus one otherwise (the quotient word's sign, so a zero quotient with a negative remainder goes to +1; in the domain the dividend and divisor are never negative, so it is "away from zero"); otherwise q is kept. **Divide overflow is unreachable**: the dividends are (ax << T) over z' (x' <= z') with ax below (x' + 1) << S, so the quotient is below 32768 (z' of 1: 32767, the index 511 above), (az << T) over x' (below 16384), and (y' << 8) or (H << 8) over the larger of y' and H (at most 256); the words x', z' are at most 0x4000 and H at most 23170, so no divisor is negative. (Were it reached, the 68000 leaves the dividend and the low word would be used.) The rig model keeps that case; 0 of those 100,000 vectors reached it.
5. *Square root* (0x29B12): for a value up to 0x63F000 Newton's iteration from the guess g = (value / 200) + 2 (word): q = value / g; stop when q - g is 0, 1 or -1 (as a word) and return q; otherwise g = (q + g) >> 1; for a value up to 0x63F0000 the value is shifted right by 4 first and the root shifted left by 2; above that shifted right by 8 and the root left by 4. **The divide-overflow case (q above 0xFFFF, where the 68000 DIVU leaves the dividend) is unreachable** [V: `view_probe.py sqrt`, every value from 0 to 8,400,000, the range the three branches feed: the largest quotient is 2,898, at most 8 iterations, no overflow, no zero guess]: the sum of the two squares is at most 2^29 (x' and z' are at most 0x4000), so the shifted argument stays below 2.2 million.
6. *Step* (the arguments' current pair c and the limit L): **snap** when L is negative, or when both `V:4A966` and the first-frame flag `V:4A85E` are zero (the routine then also sets `V:4A85E` to 1): the pair becomes the target. **Every snap path sets `V:4A85E` to 1**, the negative limit as well as the first-frame test (the one branch at 0x1486C serves both) [V: rig `view_probe.py snap`: limit -1 with `V:4A85E` 0 and camera mode 0 or 5 gives 1; limits 2000 and 560 with `V:4A85E` 0 and mode 5 leave it 0]. Otherwise each of the two angles moves separately: d = target - c; if d is negative and at least -14400: c - min(|d| >> 2, L); if d is below -14400: c + min((d + 28800) >> 2, L), 28800 subtracted when the sum reaches 28800; if d is positive and at most 14400: c + min(d >> 2, L); above 14400: c - min((28800 - d) >> 2, L), 28800 added when the result is negative. So the turn is the **shorter way round**, **a quarter of the remaining difference per pass, at most L units** (2000 = 25 degrees, 560 = 7 degrees in the approach camera modes (`V:4A966` non-zero, note 07 6.1)), pitch and heading independently; nothing wraps the 28800 except those two cases. `V:4A85E` is cleared when the view returns to the cockpit (0x141E8) and at a new flight, so the first external frame after every KP-Enter snaps [V: emulator, `V:4AB4C`, `V:4AB4E` went from the stale (1397, 25550) to (720, 2240) in the pass after the view switch].

**Levels and passes** [V: emulator `views-m3-levels.txt`, log `extracted/captures/logs/views-m3-levels.txt`; ground snapshot, the aircraft stationary at heading 2240, one video frame per `step`, the game pass counter `V:4A992` read with each pair; a game pass takes 11 to 12 video frames there]. The **snap targets** (the first external frame after KP-Enter, staged straight into the level) are, as (pitch, heading): level -1 (720, 2240), level 1 (640, 2240), levels 2, 3 and 4 (760, 2240) (the offsets of the level scaling of note 06 view item 1, the target point above). A level key does **not** snap (the ring key is unchanged): the pair moves **one quarter of the remaining difference per game pass**, at most 2000, and the camera is at the new distance at once. Sequences from the converged level -1 pair 720 (pitch after each pass from the key; the heading stays 2240; the emulator's 8 to 10 passes equal the rule, which gives the rest): level 1 (target 640): 700, 685, 674, 666, 660, 655, 652, 649, 647, 646, 645, 644, 643, then 643 (the step is 0 when the difference is 3 or less in magnitude); levels 2, 3 and 4 (target 760): 730, 737, 742, 746, 749, 751, 753, 754, 755, 756, 757, then 757. The earlier samples 666 (level 1), 751 (level 3), 757 and 644 are passes 4, 6, 11 and 12 on these sequences (the samples were taken after an unrecorded number of passes); the values 729 for level 2 and 673 for level 1 of the earlier notes lie on no sequence from 720 and are withdrawn. **the port derives the pair by the rule and fits no pass count** (a port that fitted 4, 4, 5, 5 passes to those samples is wrong for any other sampling time).

### Translation to camera space

For a model vertex of an object that has its own attitude, the vertex program does, per
vertex (raw model words x, y, z):

1. rotate by the object matrix (Q8, `fx_rotate`), giving a word triple;
2. shift each component right by the level-of-detail shift s;
3. add the object-origin offset (three words, same units after the shift; built from the
   negated camera position plus the object's own offset, 32-bit values shifted right by
   8 minus a per-object extra and then truncated);
4. multiply by the camera matrix (Q8, out = sum >> 8) and store the three words at the
   vertex table 0x4D43C, 6 bytes per vertex.

Objects without attitude (terrain-bound models) skip step 1 and shift the vertex before
adding the offset. If an object's first vertex, after step 4, lies outside the pyramid
with 50 percent slack (|x| or |y| greater than 1.5 * depth, or depth not positive) the
whole object is rejected (flag word set by the caller). The camera-space result of step 4
is what `fx_project` and `fx_clip` consume. Units: the shift makes vertex words and offsets
share one scale; only ratios x/z and y/z reach the screen, so the absolute unit cancels.

### Projection (fx_project)

Entry 0x3377C, D7 internally -5 ("project only"). Input: D0 = x, D1 = y, D2 = z, camera
space words. Output: D0 = screen x, D1 = screen y, and the same pair stored at 0x4AA10;
state word 0x4AA3C is a maximum screen row (rows greater than it are rejected; the value the
game keeps there was not characterised, the examples set it to 0x7FFF).

1. Reject (store long -1 at 0x4AA10, return D0 = 0) if x >= z, or y >= z, or -x >= z, or
   -y >= z. This makes depth positive and the point inside the view pyramid with 45 degree
   half angles in both directions.
2. sx' = trunc(x * 160 / z) + 160, sy' = trunc(y * 90 / z) + 90, division truncating
   toward zero (a signed 32-bit by 16-bit divide), then clamped to 0..319 and 0..179.
3. screen x = 319 - sx'; screen y = 180 - sy' (the point entries only; the other projections take 179 - sy', table "Rows by entry" below). If screen y is greater than the word at
   0x4AA3C the point is rejected as in step 1.

So the **focal constants are 160 horizontally and 90 vertically**, the **viewport is 320
by 180 pixels** (rows 0..179) and the **centre** is x = 159 (sx' = 160 mirrored) and y = 90.
Camera +x maps to the left of the screen and +y to the top. The polygon path (below)
uses the same divide but screen y = 179 - sy' (one row higher than the point path).
Entries 0x33780 and 0x33788-0x33794 run the same projection and then draw a dot sprite
whose size depends on distance and the shift selector in D7; 0x3385C and 0x33936 project
the two endpoints of a line (clamping, or clipping to the screen respectively) and draw it
with the line routine 0x3455E.

**Rows by entry (V: `draw_rules_check.py rows`, 400 calls per entry at seed 1, 0 differences; the rig's output routines logged, the expected values from the formulas).** The divide, the focal constants and the horizontal mirror (319 - sx') are common to every entry; the vertical mirror differs by one row between the point entries and all the others:

| entry | users | window handling | screen y |
| --- | --- | --- | --- |
| `0x3377C`, `0x33780`, `0x33788`-`0x33794` | ops 1, 49, 58 and 62 (sprites and discs) | rejects outside the open pyramid and below `V:4AA3C`; sx', sy' clamped to 0..319, 0..179 | **180 - sy'** |
| `0x3385C` | op 48, the unclipped line | no clipping; the closed-pyramid test of its table row; sx', sy' clamped to 0..319, 0..179 | **179 - sy'** |
| `0x33936` | the clipped line: ops 2, 74, 13, 75, 14, 11, 12, 6, 19, 22 | the cascade ("The clipped line routine") | **179 - sy'** |
| polygon clipper `0x28B6A`, `0x28B6C` | every polygon operation | the four-plane clip | **179 - sy'** |

So op 48 projects like a line, not like a point: a port that sends the end points of op 48 through the point projection puts both ends one row too low (V: the 400 op 48 calls have the 179 rows; the point rule would be wrong on every end point).

### Handedness: the original has no x mirror

The projection above sends camera +x to the **left** of the screen (screen x = 159 - 160 x/z, the quotient truncated toward zero) and depth is positive in front. The
camera matrix builder `fx_cam_yp` (heading B, clockwise, 28800 units per circle) gives camera x = cos B * wx + sin B * wz and depth =
-sin B * wx + cos B * wz. At heading 0 the camera faces +z and its x axis is the world x axis, so **world +x is on the camera's left**; at heading 90
degrees (clockwise) the camera faces -x. The world frame is therefore the one of `01b`: **+z north, -x east, +y up, right-handed** (x cross y = z).

**Rule for the port.** For the camera's right, screen x increases with world **-x** (a point east of a camera that faces north lands right of
the screen centre, x above 159). The original has no mirror anywhere: record positions, model vertices, the object matrices (`fx_matrix_b`) and
the camera matrix share this one frame, and a camera-space vertex is the camera matrix applied to (object matrix times model vertex, plus the
object-minus-camera position difference) per axis with no sign change of x. A model whose x axis is "east" in the file is drawn with its east side
on the right.

**What the port must change** (the shipped build mirrors model x, a milestone-2 choice named X_FLIP; the rig comparison had to flip it back): make
the original's form the only form: no negation of the camera-minus-object x (`world.c`), no mirrored camera vector in the pre-test of the
display-list module (`mirror_x` in `lists.c`, set by `world_set_x_flip`), no mirror in the frontend, and delete `world_set_x_flip` and the flip
field. The object attitude of entities deserves the same pass (tag C, no rig comparison covers it): the original builds the inverse matrix with
`fx_matrix_b` fed all three record angles negated (28800 minus the record words `+0x66`, `+0x68`, `+0x6A`, pitch first); the port's helper negates
the pitch once and `fx_matrix_b` is then given the positive pitch, so its first angle has the opposite sign of the original's.

Verified (V): `hand_check.py` runs the real camera builder (`0x32A66`) and the projection (`0x3377C`) in the rig for random headings and points at
4 to 40 degrees to either side of the heading (world direction (-sin, 0, cos), +z north, -x east): 772 projected points at scale 1000, the screen side
agrees with the bearing in all 772.

### Clipping and projection of polygons (fx_clip)

Entry 0x28B6A. Input in memory at 0x5103C: a word shift S, a word vertex count n (must be
3 or more), then n triples of words (x, y, z) in camera space. Each coordinate is first
sign-extended to 32 bits, shifted left by S, and truncated to 16 bits. Result: D0 = 1 and
the screen polygon at 0x5043C (word count, then count pairs of words, screen x then screen
y), after which the filler (0x34A28) is called; D0 = 0 if nothing remains. The "fewer than 3 vertices" rejection is applied only once, to the
final stage-4 list (a stage whose input is empty is skipped); a final depth not positive also rejects.

There is **no separate near plane**: clipping is Sutherland-Hodgman against four planes
through the eye, in this order, with "inside" meaning g <= z (z - g >= 0):

| Stage | g | Screen edge (after mirroring) |
| --- | --- | --- |
| 1 | y | top |
| 2 | -y | bottom |
| 3 | x | left |
| 4 | -x | right |

The planes |x| <= z and |y| <= z guarantee z >= 0, and the final test rejects z not
positive. For each stage the input list is processed as: the first vertex is emitted if
inside; for every following vertex Q with predecessor P, if P and Q are on different
sides an intersection point is emitted first, then Q if inside; finally the closing edge:
if the last vertex and the first vertex are on different sides, one more intersection
is appended. **The closing-edge intersection is computed from the first vertex towards the
last** (P = first, Q = last); every other intersection is computed from the previous
vertex towards the current one. The output list of a stage is the input list of the next.

Intersection of edge P to Q with fP = zP - g(P), fQ = zQ - g(Q), D = fP - fQ (16-bit
words): for each of x and y (z is rebuilt below), the new value is
c(P) + rdiv((c(Q) - c(P)) * fP, D) where the difference c(Q) - c(P) and fP are wrapped 16-bit words (not 32-bit differences), the numerator is the 32-bit product of those words and
rdiv is a 16-bit signed divide that truncates toward zero, followed by a correction:
let r be the remainder (sign of the numerator); if |r| >= floor(|D| / 2), add +1 to the
quotient when the quotient is zero or positive, and -1 when it is negative (so a zero
quotient with a negative numerator is corrected upwards; reproduce this exactly). The new
z is set to g of the new point (y, -y, x, -x for stages 1 to 4). A zero D stops the
routine through the game's error handler.

Finally every surviving vertex is projected: sx' = trunc(x * 160 / z) + 160 and
sy' = trunc(y * 90 / z) + 90, both clamped to 0..319 and 0..179, output
(319 - sx', 179 - sy'). The output order is the order the stages emit; the first output
vertex is not necessarily the image of the first input vertex.

### Face visibility and polygon order inside a model

A model is a draw program (Task 5 documents the encoding): a tree of tests and command
lists walked by 0x21C44 (hunk 10). Each polygon command (handler 0x2377C and its
relatives, reached through the command table at 0x23408) carries a flags word. Polygons
are therefore ordered by the model's own tree, chosen per frame by the viewer's side of
planes, not by a sort at run time.

Three-vertex plane test (0x22ACC, entry 0x22A7A with the flags word in D7 and A3, three
vertices as 6-byte (x, y, z) word triples at 0x51040, 0x51046, 0x5104C): let k be bits 7
to 9 of the flags word (0..7); e1 = v1 - v0 and e2 = v2 - v0 (word differences), both
shifted left by k and kept as words. n.x = (e1.y * e2.z - e1.z * e2.y) >> 8,
n.y = (e1.z * e2.x - e1.x * e2.z) >> 8, n.z = (e1.x * e2.y - e1.y * e2.x) >> 8, each
kept as a word. D7 = 1 if n.x * v0.x + n.y * v0.y + n.z * v0.z (32-bit sum) is zero or
positive, else 0. Winding convention (from the worked examples): with camera x to the left and y up, the triple (0,0,100) (50,0,100) (0,50,100) gives 1 and the same triple in reverse order gives 0. D7 is therefore 0 when cross(v1-v0, v2-v0) points toward the viewer's side of the polygon plane (negative dot with v0), and 1 for a polygon whose winding faces away from the viewer. In the general polygon handler,
D7 = 0 draws with the first colour word; D7 = 1 culls the polygon unless flags bit 14
is set, in which case a second (back side) colour word is used. A cull returns a negative
value, which makes the interpreter abandon the rest of the current program (the other programs of the draw item's list and the tree walk go on; "Draw operations" below).

**The side tests of the block tree (V: `bsp_check.py`, 5,000 random operand sets per test, 15,000 direct calls of the original routines plus 5,000 runs of the frame-word writer, word-exact, 0 differences).** `02` section 3 has the node kinds and the stream layout. The tree walk (`0x2260E`) tests the node's second word w2 in this order: bits 10 to 15 all set: unconditional jump; bits 10 or 11: axis node; bit 13: point-and-normal node; otherwise the three-vertex node
(bit 12 is never set in a shipped node outside an axis node). Each test leaves a flag: **Z set selects the first child, Z clear the second**. The drawn block (`V:4AAEA`) is the block of the chosen level; L is the low nibble of its byte +6; LOD is `V:4AB70`.

| node | arithmetic (every step a 16-bit word operation that wraps; a product is 16 by 16 to 32 bits, sums of products are 32-bit and unrounded) | first child |
| --- | --- | --- |
| axis (bits 10-11: 1 y, 2 x, 3 z; w1 = byte offset of a block vertex; bit 12 inverts) | c = the vertex's coordinate word of that axis (y at block + 12 + w1, x at + 10 + w1, z at + 14 + w1) shifted right arithmetically by L. **x and z**: Pc = c + (the entry's position word `V:4ABE2` resp. `V:4ABE6` shifted left by the LOD, as a word), E = minus the word `V:4AB2A` resp. `V:4AB2E`; **y**: Pc = c sign-extended (no position term, no LOD shift), E = minus the long `V:4AB30`, compared as longs. The side is "below" when E < Pc (signed) | bit 12 clear: not below (E >= Pc); bit 12 set: below |
| point and normal (bit 13; six inline words: point p, normal n, then the two child offsets) | d_i = p_i shifted right arithmetically by the LOD (word); d_x += `V:4ABE2`, d_z += `V:4ABE6` (the y word gets no position term); d_i += e_i with e_i the **low word of ((V:4AB1A, V:4AB1E, V:4AB22) as longs shifted right arithmetically by the LOD, then by 8)** (these longs are the negated camera, so d runs from the camera to the point; the drawer stores minus e_i at its frame offsets -0x26, -0x24, -0x22 when it loads the block header, `0x21E26`-`0x21E5A`: V, `bsp_check.py` runs those original instructions on random camera longs and LODs and compares the stored words with this formula, 5,000 runs, 0 differences); s = d_x n_x + d_y n_y + d_z n_z, a 32-bit sum of three words products | s negative |
| three-vertex (default) | the three camera-space slots named by w1 and the next two words, k = bits 7 to 9 of w2: the face test of "Three-vertex plane test" above | the dot product negative |

So the "exact eye" of a straightforward port is not what the walk uses. The axis test compares the **camera offsets in u** (`V:4AB2A`, `V:4AB2E` words and `V:4AB30` long, the camera longs shifted right by 8, `04a` section 2), unshifted by the LOD; the point-and-normal test uses the camera longs **shifted by the LOD and by 8 and truncated to words** and adds the entry's position words. Both are plain variables of the camera set-up and need no rotation: they are already in the object frame of the
non-rotating models; the axis nodes also occur in the rotating models 8, 9 and 12 and use the same variables and arithmetic. Shipped: 84 three-vertex nodes, 16 point-and-normal nodes (models 50-57), 14 y-axis and 4 x-axis nodes (models 48, 49, 102, 103 and 8, 9, 12), no z-axis node and no node with bit 12 set, so the inversion is unexercised. An equality (E equal to Pc, a dot product of zero) selects the first child for the axis test with bit 12 clear and the second for the planes.
The tree nodes hold a test and two branch offsets; a node whose flags have bits 10 to 15 all set is an unconditional jump.

### Object ordering key (painter's order between objects)

The list builder (`0x1F770`, passes at `0x2001C` and `0x20B0C`; the full rules are in `04a-display-lists.md`) fills three draw
queues of 24-byte entries (`0x53A4A`, `0x540DA`, `0x5476A`); terrain tiles are walked in a view-dependent far-to-near order from
pattern tables in hunk 65 (`0x4625C`, `0x4626C`, `0x4628C`, `0x4629C`, `0x462DC` and the three pattern blobs). Only the fine
tier (C, `0x5476A`) is ordered by distance, and only inside its *runs*: a run is the entries emitted for one sub-cell step of one
tile, when there are two or more. A run of up to 22 entries is sorted by `0x2122C` and `0x213AA`; the sort runs **every
frame**, all runs after a rebuild and one run per frame otherwise (`04a`, section 5a):

1. each entry gets a 16-bit key: 0x7FFF if flagged (bit 6 of the flags word), the stored distance word at entry `+0x10` shifted
   left by the entry's level-of-detail shift if non-zero (the walker refreshes it, `04a` section 8), else `fx_distkey` of the entry
   position shifted left by the same shift (the entry position being the stored X, Y, Z words, and for an entity the slot word
   plus the record's fine offsets);
2. `fx_sort` (0x213AA) orders the indices **by key descending (farthest first), ties by
   ascending index** (a selection sort over signed words; a negative key would be skipped by the
   selection, but keys are never negative in the data, so no entry is dropped in practice). The sorted indices are written to 0x538C8 and the run's
   entries are copied back over the run in that order, so the queue itself is reordered in
   place (the later walks and the landmark reorder see the new order) and nearer objects
   paint over farther ones.

A second per-frame pass, `0x21444`, refines the order of tier C around the nearest landmark with collision data using the
landmark's model planes (`04a` section 5b).

`fx_dist` (0x20884): D2, D3, D4 are non-negative components (D2 and D4 words, D3 a long
below 0x8000 in practice); result D1 (also stored at 0x4ABF8), capped at 0x7FFF. Let hi
and lo be the larger and smaller of D2 and D3. If lo is 0, t = T[0]; otherwise t =
T[(lo * 256) / hi] with the unsigned truncating division and T the table at 0x208E8
(T[i] = 16384 * sqrt(1 + (i/256)^2), rounded). h = hi * t (unsigned 32-bit, in Q14).
Then with e = D4 << 14: big is the larger and small the smaller of e and h; big is
shifted right by 14; q = small / big (unsigned, 0 if big is 0); result =
(big * T[q >> 6]) >> 14. This is the usual two-step approximation of sqrt(a^2 + b^2 +
c^2).

`fx_distkey` (0x2082A; the camera offsets V:4AB2A/2E/30 are in u, minus the camera position relative to the viewer's C cell corner, `04a` section 2; the result is the distance in units of 16 u divided by 2^LOD, which the walker stores at entry `+0x10`, `04a` section 8): inputs D2 = x offset word, D3 = second offset (a sign-extended word; when byte 0x4A973 is non-zero a long
from 0x4ABF4 shifted right by 8 is used instead), D4 = third word; state:
s = word 0x4AB70. d1 = word(0x4AB2A) >> s, d6 = long(0x4AB30) >> s, d7 = word(0x4AB2E)
>> s. Compute |D2 + d1| (word), |D3 + d6| (long), |D4 + d7| (word); if the middle one
is 0x7FFF0 or larger return 0x7FFF; otherwise shift all three right by 4 and apply
`fx_dist`. Result in D1.

Level-of-detail shift: when the builder emits an entry (and for the direct entities at `0x1FC20`-`0x1FCC8`) a coarse key (the largest
of the three camera-relative offset magnitudes, in units of 4096 u for x and z and 2048 u for the height, then halved and capped at 239;
see `04a`, section 3, verified in the rig against the queues) indexes the byte table at
0x20E4A; the byte (0..15) is stored in the entry and becomes 0x4AB70 when the walker draws it; it is fixed at build time. The table has 240 bytes, non-decreasing from 0 up to 15, with breakpoints that spread out roughly
geometrically (the first entries are 0, then 1, 1, 2, ...; the last run of 15 starts near index 211).
The port loads the table from the locally extracted image (it is an extractor output: 240 bytes in 16 runs, `04a`, section 3); its
contents are not reproduced here. The same shift scales model vertices and
offsets.

The draw stage's visibility, in short (`04a` section 8b has the table): there is no bounding box. The walker's result is the OR of the primitive handlers' returns; the exits before the primitives are the level choice (node threshold >> LOD against the zoom-scaled distance word), the tier substitution, the flat-block pre-test and cull, and the test of the **first vertex** of a non-rotating block in camera space (depth positive, |x| and |y| at most 1.5 depth, word arithmetic); `0x22A7A`/`0x22ACC` is only the back-face test of single polygons, and a polygon counts as visible when `fx_clip` keeps at least three vertices. Status (`04a` section 8b has the tags and counts): the level choice, the tier substitution, the first-vertex test, the flat-block pre-test and cull (the last two on constructed entries), the `0x21C34` cut-off, the type B level search and cut-offs, the handlers' returns (every handler of the table called directly with random vertex arrays) and the op 4 gate and return are verified against the rig (V); the program-item distance gates are read from the code (R: the model's tree walk implements them and equals the rig on every decided entry). The op 4 exception: it returns 1 without drawing in its gated cases, so a near-body program still gives a positive result.

Detail-level selection in the draw stage (0x21D0C, read; verified through the part id it stores, `04a` section 8a): the model's detail
list is walked node by node. A node's threshold is **masked to its low 14 bits (0x3FFF)**, shifted right arithmetically by the same
shift and compared as a signed 16-bit word with the entry's *scaled* distance: the walker's distance word `V:4ABF8` times the zoom
`V:4AAFA` / 0x80 (doubled, divided by 256), unscaled when `V:4A895` is set; a scaled value of 0x8000 or more is a negative word and
picks the nearest level. The first node whose masked, shifted threshold is **greater** than the scaled distance supplies the leaf, i.e.
the block, whose first word (the part id) is stored in the entity record at `+0x7B` (V: `04a` section 8b rule 4 and P1 against the rig,
and the part id of section 8a against the emulator). The type B ground-patch lists (routines `0x22CFC`, `0x22D0A`) differ: their
comparison uses the unmasked threshold and a node fits when the shifted threshold is **at least** the distance (V: `prim_walk.py typeb`; below). The precision exponent of the block chosen (which shift the vertex words get) compares the block's own thresholds with the
*unscaled* distance word (`04a` section 8b, P2).

### Entry conventions for the differential tests

The two per-vertex transforms are fragments inside the large routine 0x21C44 and cannot be
entered through a normal return. The rig procedure used for the examples is: write a return
instruction (0x4E75) over the first word of the instruction at the stated end address in the
rig's RAM copy, set the registers and memory below, call `rig_run` at the entry address.

| Fragment | Entry | End (patch here) | Inputs | Outputs | Clobbers |
| --- | --- | --- | --- | --- | --- |
| Vertex rotation (`fx_rotate`) | 0x22002 | 0x2203C | D2, D3, D4 = x, y, z (low words, sign-extended); nine matrix words at 0x4AC7E | x' in the low word of A4, y' in D6 and D7, z' in D4 (low words) | D5, D6, D7, A2, A4 |
| Camera transform (`fx_camera`) | 0x22072 | 0x220AE | D2, D3, D4 = x, y, z (low words); nine matrix words at 0x4AC90; A3 = output address | three words (camera x, y, depth) written at A3, A3 advanced by 6 | D5, D6, D7, A2, A3 |

Both compute out_i = (m[3i]*x + m[3i+1]*y + m[3i+2]*z) >> 8 on words, as described above;
the matrix is read from the fixed address, not from a register. Note that the fragment at
0x22002 contains no shift by the level-of-detail shift and no offset: those happen between
the two fragments in the real flow.

`fx_sort` (0x213AA) is an ordinary subroutine (ends with a return, saves its registers): the
key count n is passed as a word in D7 (2 to 22); the n key words must be at 0x53818, the
identity words 0, 1, ..., 21 must already be at 0x53870 (the game writes them once at start-up;
a test must preload them), and the sorted index words are written from 0x538C8 on. It uses
0x53844 (22 words) as scratch. The number of valid output words is the number of keys that are
not negative; words after them are left over from earlier use and must be ignored.

Conventions of the builders: the matrix builders (`fx_matrix_a`, `fx_matrix_b`, `fx_cam_yp`,
`fx_cam_ypr`) and the roll builders (`fx_roll_q8`, `fx_roll_q14`) write their nine words at the
address passed in A1 and advance A1 while writing (by 16 for the matrix and camera builders, 18 for
the roll builders); callers must not rely on A1 afterwards. `fx_distkey` treats D3 as a
sign-extended word when byte 0x4A973 is 0 (its shipped value); the long read from 0x4ABF4 is used
only when that byte is non-zero.

### Polygon filler interface

`0x34A28` (hunk 36) reads the screen polygon at 0x5043C: a word vertex count, then
(x, y) word pairs with x in 0..319 and y in 0..179, in the order produced by `fx_clip`.
The colour is the word at 0x4AA0C (set by the command handler) with the dither word at
0x4AA0E; the routine fills through the Amiga blitter (it writes the custom chip DMA
control and blitter registers), one plane at a time. Lines use 0x3455E (endpoints in D0,
D1 and D2, D3, colour in 0x4AA0C). It does not run in the rig (hardware registers); the
examples below patch a return instruction over its first byte in the rig's RAM image.

### Block set-up of the terrain models: vertices, second section, extra vertices, type B

All of this runs once per draw, after the level and block are chosen and the pyramid test of the first vertex has passed (`04a` section 8b, rules 4 to 8), and **before any
program item**: every vertex a program names is already in the table `0x4D43C`. The model `visibility.py` (`terrain_vertices`) is a register-level transliteration of the
three cases below and agrees with the rig on every slot (V: 945 drawn blocks, 9,261 slots, 0 differences; 109 with a second section, 175 with strips, 643 flat, 302 solid).
Precision exponents and the first-vertex position are `04a` P2 and P3; below, "shifted" means the vertex word arithmetic-shifted right by the vertex shift (the block's H or L
nibble, plus the LOD), and "position" is the entry's position words with the camera difference added, in the block's units.

| block kind | what is transformed | rule |
| --- | --- | --- |
| solid, not rotating (flags 0x00, 0x08) | the first b8 vertices of the block (x, y, z words from block offset 10) | each shifted, plus the position word of its axis, then the camera matrix (nine words at `0x4AC90`; each output is the sum of three products shifted right by 8). Flag bit 3 adds the strip section after the plain vertices (`02` section 2.4): chains of points with division counts, the interpolated points written to the slots after the plain vertices |
| flat (flag bit 1: 0x02, 0x42, 0x06) | b8 vertices of (x, z) words, 4 bytes each | the (x, z) pair is shifted, plus position, and the camera matrix is applied to (x, 0, z); the height term (the position y word times the matrix column of y, shifted right by 8) is added to every transformed vertex |
| flat with flag bit 2 (0x06: models 46, 72, 73, 95) | the b8 plain vertices as above, then the **second vertex section** | below |
| rotating (flag bit 0) | the entity path: object matrix first (`03`, translation to camera space) | not used by terrain |

**The second vertex section (flat blocks with flag bit 2; V: `sec2_check.py`, 300 constructed blocks, 0 differences; and the 109 real blocks of `prim_walk.py vertices`).**
It has **no header**: it starts at the block word right after the b8 plain vertices. Its points are transformed in the block set-up exactly like the plain ones (the same shift, position and height term)
and land in the slots numbered from b8 upward (for model 46, b8 = 2, so the section slots are 2 and above); **every program operand may name them** (most of the slots that the programs of models 46, 72, 73 and 95
name lie in the section: the lines of model 46 run between slots 0, 2, 4, 6, 8, 10, 12 and 14). The section exists only when b8 is 2 or more: the plain loop is a do-while that transforms at least one
vertex, so with b8 of 0 or 1 exactly one plain slot exists and the section is **skipped**, whatever follows that vertex in the block (73 of the constructed cases). The section is a run of groups; a group is, in block order:

| part | contents | rule |
| --- | --- | --- |
| count n | one word | negative: the section ends. Otherwise the pairs below run as a **do-while**: n = 0 yields one pair like n = 1, and n yields n pairs |
| the pairs | per pair one point (x, z), two words | stored as **two consecutive slots: the point P, then P + D**, word by word with 16-bit wrap. D = S[k-1] - S[k-2], **the later slot minus the earlier**, where k is the number of the next slot to be written, that is the difference of the last two slots written before the group (for the first group the last two plain vertices, slots b8-2 and b8-1; for a later group the last two slots of the previous group, which are its two single points when it had them). D is taken once, at the start of the group, and is the same for every pair of the group |
| the word after the pairs | one word | **only its sign is tested** (its value is ignored): negative ends the section; zero or positive (any value, 0x7FFF included) is followed by two points (x, z), each stored as **one** slot (no offset copy), and the next group then begins at the next block word |

Without this section no slot above the plain vertices exists: the port's extractor reported such blocks as undefined slots; the sign of +0x14 for these entries cannot be computed without it (`04a` section 8c, the second divergence).
The check builds each case twice in the same state, once with every explicit point as a plain vertex (the rig's transformed value of each point) and once as the block under test, and compares the rig's slot table with the slots the rule predicts
from those values (249 groups with a non-negative word after the pairs, 59 groups with n = 0, 92 cases with two or more groups, 73 cases with b8 of 0 or 1; slots beyond the last written one keep their sentinel).

**Extra vertices of a gated item.** An item with a non-negative first word may carry an operand count N (a non-positive count means none) and a start byte offset; N vertices are read from
the block at offset 10 plus that start offset and transformed like the plain vertices into the slots from the same offset (for a 3D block the offset is also the slot byte offset;
for a flat block, whose vertices are 4 bytes, the slot byte offset is the block offset times 1.5). The shipped models use them in ids 3, 4, 6-9, 12, 43-45, 47, 59, 60, 66,
68-71, 74-85, 93, 94, 96, 97, 102, 103 (`02` section 2.3). A rotating block routes them through the object matrix.

**Gate of an item, and the end of a program.** A program is a run of items; an item whose first word is non-negative and not 0x7FFF carries two gate words G1 and G2 (`04a` P6).
An item that **fails its gate ends the program**, exactly as a negative handler return does; the draw item's remaining programs (the list) and the tree walk go on. `02` section 4 states the same
(R: read from `0x22846`-`0x2285E`; consistent with the model's tree walk over all constructed entries that reach it).

**Type B models (ids 36-42, 98-101, 105, 106, 109-132; handlers `0x22CFC`, `0x22D0A`).**

* *Chain of detail entries.* Each entry is a threshold word, a long address of the next entry, a long address of the vertex block, then the program stream. **Bit 15 of the threshold does not take part in the comparison**:
  a threshold with bit 15 set is neither shifted nor compared; `0xFFFF` ends the search (the drawer returns 0), and the only such entry of the shipped data is the single entry of model 38, so model 38 never draws
  (any other negative word would take the entry's block without a comparison; none exists). For a threshold with bit 15 clear the shift is arithmetic and logical alike. The search takes the first entry whose
  **unmasked** threshold shifted right by the LOD is **at least** the scaled distance, compared as a signed word (the plain drawer masks the threshold to 0x3FFF and needs "greater"; a scaled distance of 0x8000
  or more is a negative word and takes the first entry); a next-entry long with bit 31 set (`0xFFFFFFFF`), or the end, returns 0 (V: 400 sweeps on both sides of every threshold, 91 of them exactly at it, 0 differences).
  **The endless search.** The last entry of **six** models (39-42, 99, 100) names itself as its next entry with the threshold 0x7FFF; for a scaled distance above 0x7FFF shifted right by the LOD the original
  never leaves the search. The case is unreachable: the scaled distance is the walker's distance word times the zoom factor over 0x80 (at most the word, the zoom keys reach 0x20 to 0x80), and the word times
  2^LOD is the distance in units of 16 u, so the loop needs a true distance above 0x7FFF * 16 u = 524,272 u. Every queue entry lies within the tile window of its tier (`04a` section 3: at most 2 cells of 65,536 u
  beyond the viewer's cell: to the far edge 3 cells on one axis and 2 on the other, about 237,000 u horizontally, plus the camera height, at most 32,768 u where the type B routines still draw), at most about 0x3A00 in these units, 45 percent of 0x7FFF;
  measured (`typeb_loop_check.py`: 471 terrain entries of 150 random frames, LODs 0 to 5): the largest true distance word is 12,128, 37 percent. (The 16 endless searches of the primitives run, `prim_walk.py primitives`,
  are constructed walks whose distance word was forced above the limit; they are timeouts of the rig.) **Port ruling:** treat the case as unreachable: the type B search returns 0 (nothing drawn) and a counter
  records the event; it never loops.
* *Vertices.* The vertex block is a count word, two skipped words, then (x, z) pairs; each pair is shifted by the LOD only (no precision exponent, no pyramid test), the position added, the
  height term added, the camera matrix applied. **The zero "placeholder" corners of ids 105, 106, 109-132 are literal data of the file** (for id 109 the near entry declares five pairs and
  stores two real ones and three (0,0)): the transformed pairs of those slots are the patch origin. Nothing writes them at run time (`modeldata_check.py`: after 150 random frames with the builder and the three walkers
  and 126 constructed one-entry walks, the image area 0x10000-0x4A6E8 outside the BSS hunk is byte for byte the file, 0 writes), and the programs name them (id 109, near entry: the first item is op 30 on the slots 2, 3, 4, which are all the origin), a degenerate polygon that still counts
  as drawn when the origin is inside the view pyramid (the clipper does not look at area); the lines of ops 74 and 2 then join the real corners to them. The extractor must export the zero
  pairs as ordinary vertices instead of reporting the patches as blocked.
* *Return.* No tree test of the first vertex and no pre-test; the stream is walked by the same tree walker and programs; the return is the OR of the handler words, a negative word
  aborting with 0 (table above). V: 509 type B tree walks in the primitives run.

### Draw operations: what a handler returns and what it draws

**Call convention.** The program interpreter of the common drawer (`0x227FE`; level and block set-up in `04a` section 8b) takes an item's operation word, looks
its handler up in the table at `0x23408` (index = word and `0x3FFF`, divided by 4) and calls it with the argument words after the operation word in
A2 (the handler advances A2 past them, or past the bytes a selector skips) and the camera-space vertex table `0x4D43C` filled. A handler returns a word
in D0 and the condition codes of its last flag-setting instruction. The two drawers read the return differently:

| drawer | what it tests | negative | non-negative |
| --- | --- | --- | --- |
| common drawer (`0x21C40`, `0x21D0C`, entity entry `0x21C44`) | the condition codes (N xor V) | the **rest of the current program is abandoned**; the word is not added; the walk continues with the next program of the draw item's list, or with the tree | the word is added to the accumulator by OR; the return of the drawer is the accumulator (`04a` section 8b, rule 9) |
| type B drawer (`0x22CFC`, `0x22D0A`) | the word D0 itself | the **whole drawer returns 0**, even after earlier handlers drew | OR into the accumulator; the drawer returns it |

The two readings differ only for a handler that leaves D0 and the condition codes inconsistent: op 26 (negative condition codes unless every one of the N + 1 slots from v is behind the camera, D0 = 0 or 1;
it occurs only in type B models, which test the word) and the rejected points of ops 1, 58, 62 and 49 (D0 = 0, negative condition codes; they occur only in
common-drawer models). "Drew" below means that the primitive reached its output routine: a polygon (`fx_clip`) at least three vertices after the four-plane
clip with every output depth positive, so that the filler `0x34A28` was called (D0 = 1; fewer vertices or a vertex depth not positive: 0), a line
(non-empty after clipping, so that the line routine `0x3455E` was called), a point sprite. The clipper entries `0x28B6A` and `0x28B6C` are the same routine.

**Return table.** Operation numbers are those of `02` section 5; addresses are handler entries (hunk 13). "Models" lists, where useful, the model ids the queues
can draw (items are model id minus 16; aircraft and carrier records, models 0 to 15, use the same handlers through the entity entry). Tags: **V**
verified, direct handler calls in the rig over random camera-space vertex arrays (`prim_ops.py` and the scripts named in Verified by) compared with
the rule; **R** run, exercised without an independent comparison; **C** read from the code only.

| op | handler | draws | returns | tag |
| --- | --- | --- | --- | --- |
| 0 | `0x23540` | nothing | 0, Z set | V |
| 1 | `0x23544` | one point sprite at vertex v in the pen, size by distance (below) | 1 when the vertex is strictly inside the view pyramid (z > 0, \|x\| < z, \|y\| < z) and its screen row is not below `V:4AA3C`; else **0 with negative condition codes** (abandons the program). Models 65, 80, 81 | V |
| 58 | `0x23566` | the same point with the fixed 2 by 1 sprite | as op 1. Models 34, 35 | V |
| 49 | `0x23666` | when the three-vertex face test (flag word W, bits 7-9 the scale) says the face is not away: a point at vertex 0, pen W, size as op 1 | face away: 0, N clear; facing and drawn: 1; facing but rejected: 0 with negative condition codes. Entity models 3, 4, 6, 7 | V |
| 62 | `0x25750` | a filled disc; the operands are, in this order, the vertex byte offset v, the pen, the size S (`ops_points.py` builds the stream in this order and the sprite, radius and pen agree on 400 op 62 calls, 1,600 point calls in all). The disc is at vertex v in the given pen. The vertex's three words are first **shifted left by the drawer frame's shift word** (the block's vertex shift plus the LOD, frame offset -8); "distance" is `fx_dist` (above) of \|x\|, \|y\|, \|z\| of the shifted vertex; the radius is R = 127 when that distance is not positive (as a signed word), else the third operand S divided by the distance (S sign-extended to 32 bits, unsigned word divide, the low word of S staying when the quotient overflows, which every S of 0x8000 or more does), limited to 127 as a signed word; a negative or zero R draws the 2 by 1 sprite | drawn 1; rejected (the shifted vertex not strictly inside the pyramid, row below `V:4AA3C`) 0 with negative condition codes. Models 33-35, entity models | V |
| 71 | `0x2576E` | a list of discs as op 62: triples (vertex byte offset, pen, size) until a size word with bit 15 set (that triple is the last, its size is the word without the bit) | OR; **used by the starburst program of the effect models 33 to 35** (it is reached through the skips of op 70, so the extractor's static decode does not list it; note 07 section 11.3) | V (`effects_stream_check.py`: 11,328 discs of the original's handlers against the stream) |
| 2 | `0x236D4` | the clipped line `0x33936` from v1 to v2 | 0 without drawing when both depths are negative; else 1 when the clipped line is non-empty, 0 otherwise; never negative | V |
| 74 | `0x236C4` | op 2, but only when `V:4AB30` is below -0xC0 (camera above 192 u, 240 ft); otherwise skips its three operand words | 0 when skipped; else as op 2. The pen `V:4AA0C` is written only after the height gate (a skipped item leaves it; table "When V:4AA0C is written"). Models 109-111, 114-120, 122, 124, 125 (type B) | V |
| 48 | `0x236BC` | the **unclipped** line `0x3385C`: both end points are projected and clamped to the window; no clipping | 0 when both depths are negative or either end point is outside the closed pyramid (z <= 0, \|x\| > z or \|y\| > z); else 1. The end points are projected with the **line rows 179 - sy'**, not the point rows 180 - sy' ("Rows by entry" above; V: 400 calls). Models 61-64, 89, 90, 102, 103, 107, 108 | V |
| 13 | `0x249C4` | line list (pen, then v1/v2 pairs, bit 15 of the last v2): each pair as op 2, pairs with both depths negative skipped | OR over the pairs. Most models | V |
| 75 | `0x249B0` | op 13 only when `V:4AB30` is below -0xC0; otherwise skips the whole list | 0 when skipped; else as op 13. The pen is written only after the height gate, as for op 74. Models 112, 113, 121, 123, 126-132 (type B) | V |
| 14 | `0x24A30` | op 13 with both end points of every line **reduced by** the vector V[vref+1] - V[vref] (slot vref+1 minus slot vref, per axis, word subtraction: the lines are drawn at their end points minus the vector; `Line operations 6, 11, 12, 14` below) | OR | V |
| 11, 12 | `0x248F0`, `0x2493E` | N segments (count in bits 8-15 of the first word, pen in bits 0-5; at least one), each from slot s to s+1, two slots further per segment; op 12 with both end points of every segment **reduced by** V[vref+1] - V[vref], as op 14 (`Line operations 6, 11, 12, 14` below) | OR | V |
| 6 | `0x23E04` | N-1 rungs (N of 2 or more) between the vertex pairs (A1, A2) and (B1, B2): rung k joins A1 + k (A2 - A1)/N to B1 + k (B2 - B1)/N, k = 1 to N-1, interpolated in fixed point and rounded (`Line operations 6, 11, 12, 14` below) | OR over the rungs. Models 107, 108 | V |
| 19 | `0x24024` | 2N hatch strokes (clipped lines `0x33936`, solid, pen operand): the first N from C + h(kQ) to A + h(kQ), the second N from A - h(Q) - h(kQ) to C - h(Q) - h(kQ), k = 0 to N-1, A, B, C the slots v, v+1, v+2, Q = B - C, h = halve (`Hatch and stripe geometry` below) | OR over **all** 2N strokes | V |
| 22 | `0x24488` | N1 rows of clipped-line strokes (pen operand, solid): row i, stroke j from slot (v+2+i) + h(j (S0 - S2)) to that plus (S1 - S2) (`Hatch and stripe geometry` below) | OR over all strokes | V |
| 20, 32 | `0x24172`, `0x24160` | 2N filled parallelogram strips (4 vertices each, polygon clipper `0x28B6C`; op 20 pen 13 in the plane-1 toggle mode, op 32 explicit pen, solid): the first N shifted by h(kQ) from the edge C-A, the second N the same strips on the other side, shifted by -h(Q) - h(kQ) and traversed in reverse (`Hatch and stripe geometry` below) | OR over the **first N strips only**; the second N strips are drawn but never count | V |
| 21, 57 | `0x24358`, `0x24342` | N1 rows of filled parallelogram stripes (4 vertices, polygon clipper `0x28B6C`; op 21 pen 13 in the plane-1 toggle mode, op 57 explicit pen, solid): row i has N2(i) stripes, based on slot v+3+i, stripe j shifted by h(j Q) (`Hatch and stripe geometry` below) | OR over all stripes | V |
| 23 | `0x247FA` | N filled parallelogram strips (4 vertices, polygon clipper, plane-1 toggle mode; the pen is not set), one per slot v+3+i, of width vector (S0 - S3) + h(3 (S1 - S3)) (`Hatch and stripe geometry` below) | OR over all strips | V |
| 3 | `0x2377C` | polygon through the listed vertices; pen W bits 0-3, or the following back-side pen | **-1** when every listed depth is negative, or when the face is away and W bit 14 is clear (W bit 15 set: no face test); else 1 when the polygon reaches the filler, 0 otherwise. Most solid models | V |
| 69 | `0x23722` | the quad A, B, C, A-B+C, then exactly the tail of op 3 with the word W | as op 3 (all four depths are tested; the face test uses A, B, C). Models 102, 103 | V |
| 24, 25 | `0x22F92`, `0x22FEA` | polygon through N listed vertices (24), through slots 0 to N-1 (25) | 0 when every depth is negative; else 1 when it reaches the filler. Type B models 39, 99, 100 (24); 37, 98, 101 (25) | V |
| 26 | `0x23036` | polygon through N consecutive slots from v. **Early-out first, over N + 1 slots:** the depth test reads the N slots of the polygon **and the slot after them** (slots v to v+N); when all N + 1 depths are negative the handler returns 0 with the condition codes clear and has written **nothing**: not the pen, not the mask, not the pattern, not the toggle. Otherwise pen 7, mask 3 and pattern 3 for the polygon (toggle untouched), the polygon clipper `0x28B6A`, and **only on this path** the solid defaults mask 0xF and pattern 0xFFFF are written back afterwards (not the earlier state; `Fill state` below) | early-out: **0 with non-negative condition codes**, nothing drawn; otherwise D0 is 1 when it reaches the filler and 0 otherwise, **with negative condition codes in both cases** (in the primitives run 137 of the 139 calls had negative codes). N slots behind and the next slot in front is **not** an early-out (V: `draw_rules_check.py op26`, 400 calls at seed 1: 83 with exactly that pattern drew; 82 early-outs left pen, mask, pattern and toggle at their sentinels; 318 draws wrote pen 7 and left mask 0xF, pattern 0xFFFF, toggle untouched; 231 reached the filler under pen 7, mask 3, pattern 3). Type B models 36, 39-42, 99, 100: the word is tested there, so it counts there | V |
| 27, 30 | `0x24560`, `0x2456E` | the quad A, B, C, A-B+C with stipple (2,0,2,0) and (8,8,0,0). **Order:** the four fill words are written, then the pen (operand 1) to `V:4AA0C`, then the early-out; the stipple and the pen therefore stay set after an early-out | **0 when all four depths are behind (negative): A, B, C and the derived fourth depth A - B + C, a word sum that wraps**; the quad is drawn, and the clipper called, as soon as one of the four is not behind (A, B, C behind with A - B + C not behind is **not** an early-out); else 1 when it reaches the filler. V: `draw_rules_check.py op27`, 400 calls per op at seed 1: 96 (op 27) and 91 (op 30) calls with A, B, C behind and the fourth depth not, all drawn; the early-out and the clipper call agree with the four-depth rule on all 800; the words (2,0,2,0), (8,8,0,0) and the pen are in place after every call, early-out included. Models 44, 45, 59, 60, 68-71, 74-79, 93, 94, 97 (27); 73, 105, 106, 109-132 (30) | V |
| 17, 18 | `0x24774` | list of quads (four vertex offsets each, a negative word ends), pen 12, mask 8 | OR. Models 46, 72, 73, 95 | V |
| 9, 10 | `0x24BA4`, `0x24C14` | parallelogram quads (the bridges, models 48-52) | 0 without drawing when `V:4AB30` is below -0x80 (camera above 128 u, 160 ft); else 1 when the quad reaches the filler, 0 otherwise. Op 10 returns 0 without calling the clipper when all four of its depths are negative; **op 9's "all behind" test is not that: it reads three depth words of the polygon buffer past its quad ("Op 9 reads past its quad", below)** | V |
| 15, 16 | `0x24AB2`, `0x24B26` | quads (models 48-57) | 0 when culled behind; else 1 when it reaches the filler | V |
| 7, 8 | `0x24CB2`, `0x24D9E` | triangle faces of the bridges: vertex rule below | 0 when all three depths are negative, else 1 when it reaches the filler; never negative | V |
| 39 | `0x23AF0` | one or two quads on the absolute slots 0 to 3; pen and shape by viewing direction, rule below | **-1** when slot 1 fails the word test of "The slot-1 test of ops 39 and 40" below (x > z, or the word negation of x > z, or the same for y; no separate z test); else the OR of the clip results (1 when a polygon reaches the filler). Models 61, 63, 64, 89, 90, 107, 108 | V |
| 40 | `0x23CB2` | one triangle on the absolute slots; rule below | -1 as op 39, else 1 when it reaches the filler. Model 62 | V |
| 55 | `0x23820` | polygon lists with vertex lists at base + offset (entity models) | OR of the clip results | C |
| 4 | `0x238C6` | the detail faces of the aircraft near body in pen 0 (below) | 1 whenever the gate is closed or open (the clip results are discarded); -1 when the height is above 5,120 ft. Entity models only | V |
| 31 | `0x23F4A` | nothing: the size and visibility early-out against two consecutive vertices of the drawn block: the operand is a byte offset into that block's vertex list (the pair starts at block base `V:4AAEA` + 10 + operand; the shipped operands are 12 and 20, flat 4-byte vertices 3 and 5), `The block that op 31 names` below | 0; **-1** (abandons the program) when the pre-test of rule 6 rejects (it runs only when `V:4AB30` is above -0x140 and `V:4A924` is zero). Models 44, 45, 59, 60, 68-71, 74-79, 93, 94, 97 | V |
| 33 | `0x23F1E` | nothing: sets the abandon flag of the draw item when `V:4A83D` is zero and the viewed record's byte `+4` has bit 6 | 0. Entity models 8, 9 | R |
| 67 | `0x2362A` | nothing: skips the next 18 bytes when the face of three vertices (word W) is away | condition codes never negative. D0 is **no result**: it is the low word of the product of the first normal component (the face test's `(e1y*e2z - e1z*e2y) >> 8`, edges shifted left by bits 7-9 of W) and x of the first vertex, a leftover of the face test, and the accumulator ORs it in; the entity models that use it (8, 9, 12) have a stored result that nothing reads, so the port returns 0. Entity models 8, 9, 12 | V (skip, codes and the D0 value, 1,000 calls); the use of D0 by the accumulator C (unread) |
| 34, 35, 36, 38, 41, 42, 43, 44, 45, 46, 47, 52, 53, 56, 59, 63, 64, 65, 70 (19 operations, 17 handlers; **op 70 is not vertex synthesis**: it is the timer skip of the effect objects' starburst program, note 07 section 11.3) | 34 `0x245E4`, 35 `0x24630`, 36 `0x24698`, 41 `0x24EFE`, 42 `0x2539A`, 43 `0x2524C`, 44 `0x25342`, 52 `0x250C2`, 53 `0x25134`, 56 `0x25CDC`, 59 `0x25AEC` (the unused ops 60 and 61 share it), 63 `0x256B8`, 64 `0x25DD4`, 65 `0x25E7C`, 70 `0x23588`; **ops 38 and 46 share `0x2551C`, ops 45 and 47 share `0x2560C`** | vertex synthesis: fixed word formulas of slots from the operand's base (formulas of 02 section 5b, corrected below; the operand-free ops 38, 46, 45, 47, 56, 63, 64, 65 are fixed formulas on base 0, below: **none of them depends on state**; ops 52 and 70 also skip bytes by state) | 0, Z set, never negative | V (the return of all 19, `prim_ops.py`; the slot formulas of 34-36, 38, 41-47, 52, 53, 56, 59, 63-65: `ops_synth.py`, `synth2_check.py`) |
| 50, 51, 54, 66, 68, 72, 73, 76, 77 | `0x24FDC`, `0x25054`, `0x250A0`, `0x2502A`, `0x23612`, `0x23604`, `0x235F4`, `0x25074`, `0x2507A` | state selectors: skip bytes of the program that follows. Op 54 (the gear block gate of models 3, 4, 6, 7): with n = (byte `+0x7C` of the record at `V:4AA6E` with bit 7 cleared) shifted right by 4, it skips 2 bytes (the op word of the next item) when n < 6 and none when n >= 6 | 0, Z set | V |
| 5, 28, 29, 37, 60, 61 | | not used by any shipped model | | C |

**When `V:4AA0C` (the current pen) is written (V: `draw_rules_check.py pen`, 400 random calls per operation at seed 1, 14,400 checks, 0 differences; "written" means the word differs from a sentinel stored before the call).** The pen is a word every output routine reads at the moment of its call, so a handler that exits early without writing it leaves the previous object's or item's pen for the next draw. The port keeps one current pen and writes it at the same point:

| when | operations | the value written |
| --- | --- | --- |
| at handler entry, before any test or exit (also when the item then draws nothing) | 1, 58, 2, 48, 13, 14, 11, 12, 6, 19, 22, 24, 25, 32, 57, 7, 8, 15, 16, 17 (and 18, the same handler), 62; **9 and 10 before their height gate**; **27 and 30 before their early-out** (after the four fill words) | the pen operand (ops 17, 18: 12) |
| at handler entry | 20, 21 | 13 |
| after the height gate | 74, 75 (a skipped item leaves the pen as it was) | the pen operand |
| after the early-out | 26 (the N + 1 slot test above) | 7 |
| after the all-behind and face-away exits | 3 and 69 (not written when they return -1), 39 and 40 (not written when slot 1 fails the word test of "The slot-1 test of ops 39 and 40", return -1), 49 (not written when the face is away: 0 with the codes clear; written when it faces, drawn or rejected) | the pen operand (op 3, 69: bits 0-3 of W, or the back-side pen; op 49: W) |
| never | 23 (the strips use the pen left by the previous item) | none |

**Op 4 (the aircraft near body and detail faces).** The program of the player's own entry in the alternate list is op 59 or op 42 (vertex synthesis) and op 4
(pen 0). The gate that decides whether the faces are drawn at all, in this order (the first that applies decides; "closed" returns 1 without drawing):

| step | test | result |
| --- | --- | --- |
| 1 | the record's byte `+4` has exactly one of bits 6 and 7 set | closed |
| 2 | both bits set: the height used below is (`V:4AB1E` minus the value D, shifted right by the LOD), D = -(record word `+0x4E`) * 256; `V:4AB1E` at or above D | closed. (Other bit patterns use `V:4AB1E` unadjusted.) **The carrier-deck record has byte `+4` = 0xC8 (both bits) and `+0x4E` = 112: D = -28,672, the camera height -31,752 gives the adjusted height -3,080 for steps 3 to 7** (V: the logged deck state; the airborne record has byte `+4` = 0x04 and no adjustment) |
| 3 | height below -0x100000 (above 5,120 ft) | returns **-1** (abandons the program) |
| 4 | `V:4A83D` not zero (external view) or the pitch band `V:4A923` is 3 or more | open |
| 5 | the viewed record's own entry (entry id equal to `V:4A994`): band 0, or height below -0x2000 (above 40 ft) | closed; otherwise open |
| 6 | another record: class byte `+0x62` is 0x30 or 0x14, or height at or above -0x10000 (at most 320 ft) | open |
| 7 | another record, otherwise: bands 0 and 1 | closed; band 2: closed when the height is below -0xA0000 (above 3,200 ft), else open |

**Which height each step reads (V: `op4_check.py` draws the two independently).** Steps 2 to 7 read the **camera height `V:4AB1E`** (the long; in the table "the height"); none of them reads the record's altitude.
The record's altitude long `+0x18` is read in exactly one place: the compare of the first distance word below. The check sets `+0x18` to the camera height plus 0, +-1, +-2 or +-0x40 (a larger difference moves the entity out of view before the
handler is reached), so that the two straddle every boundary: 1,500 cases, 0 differences.

Once open: the first word of the list (a distance word, 0xA00 for the F/A-18) times 256 must be at least the record's altitude long `+0x18` (0xA0000: 3,200 ft),
else the handler returns 1 at once. That word is also the distance of the first face, which is not tested again. Each later face is drawn only if its distance word is at least the record's long `+0x10`, and
**the first face that fails this test ends the face list** (it does not skip to the next face; a negative distance word ends the list too). For the shipped F/A-18 list (0xA00, 0xA00, 0x800, 0x800, descending) ending and skipping
give the same faces; the check also runs 487 cases with face words that are not descending, where only ending matches the rig.
A face is a distance word, N, a flag word and N vertex offsets; a face whose flag word is zero is drawn only when it faces the viewer (the three-vertex
test; an away face is skipped, and the list goes on), the shipped F/A-18 faces all have a non-zero flag word and so no face test. Each face goes through the polygon clipper in pen 0 (clip shift 0); the
clip results are discarded, and the handler returns 1 with N clear whatever the clipper returned. The thresholds in feet use 1 ft = 204.8 pu: -0x2000 is 8,192
pu = 40 ft, -0x10000 is 320 ft, -0xA0000 is 3,200 ft, -0x100000 is 5,120 ft. No other op returns negative because of a record: the ops that read the record
at `0x4B23C` plus the entry's offset (50, 51, 54, 66, 76, 77 and the synthesis ops) only choose how many bytes of the program to skip.

**What the near body shows** (V: `nearbody_check.py`, the logged deck state with the entry given the alternate list as in `04a` section 8a). The near body is walked in the cockpit view only in three cases (`04a` section 8a): the pitch band 3 (test 7), the single pass after an
external view (`V:4A8ED`, test 6; every band) and the bands 1 and 2 with the eye measure E at or above -32 (test 8). In the deck state two thin triangles in pen 0 reach the filler, at the bottom centre, screen vertices (159,166), (159,179), (158,179) and
(159,166), (156,179), (162,179), under the instrument panel (it starts near row 145): in band 3, and after an external view in the bands 1, 2 and 3 (band 0: the gate is closed). The deck's bands 1 and 2 outside that pass do **not reach op 4 at all**: E is -125, below -32, so test 9
walks the cockpit block and nothing is drawn. The earlier reason given here, "the camera is 155 ft up, over the 40 ft limit of the op 4 gate", was wrong: the deck record has both bits 6 and 7 of byte `+4` set (0xC8) and the term `+0x4E` = 112, so step 2 of the gate
uses the adjusted height V:4AB1E minus (-112 * 256) = -31,752 + 28,672 = -3,080 pu (about 15 ft above the term), which is above the -0x2000 limit of step 5 and leaves the gate open in the bands 1 to 3 (`op4_check.py` has the rule). The airborne state has
byte `+4` = 0x04 (no adjustment), the unadjusted height -575,057 is far above the limit, and the gate is closed in the bands 0 to 2; in band 3 it is open and no polygon reaches the filler.
The number of triangles depends on the state: the earlier count of about five specks was another state.

**Where the near body falls, and what the filler does with it** (R, rig: the filler's own register writes, `nearbody_rows_check.py`, 28 checks for the last rows 144, 165, 166, 167, 170, 179 and 190; the emulator part below is V for what it shows). The triangles lie in rows 166 to 179. The filler `0x34A28` rejects a polygon only when the **top row** of its bounding box is below the last row `V:4AA3C`: with the cockpit window's last row 144 (or 165) both triangles are rejected and nothing is drawn, and the filler still returns 1.
Otherwise:

| piece | rule (rig) |
| --- | --- |
| classification | the small-shape classification (below) uses the **uncut** bounding box, rows 166 to 179. The first triangle (13 rows, one column wide) therefore takes the line path for every last row from 166 up, and the line drawer `0x3455E` receives the **uncut** end points (158,166) and (159,179), rows past `V:4AA3C` included |
| the first triangle's line | the line drawer itself **clips** to the last row: its step count is min(13, last row - 166): no blit at all for last row 166, one step at 167, four at 170, 13 at 179 (the blitter height word written is the step count times 64 plus 2). Whether the pixel at the last row itself is drawn depends on the blitter's height convention (R) |
| the second triangle | area-filled with its edges cut at the last row; the fill height written is last row - 165 rows (1, 2, 5, 14 for last rows 166, 167, 170, 179), two words wide |

At last row 167 that is one step of the first triangle (one pixel at row 166 if the blitter height counts pixels, R) and the rows 166 and 167 of the second. The pixels of these paths need a blitter model, which the rig does not have (R). From the geometry alone the second triangle's edges start at the apex (159,166) and move one pixel sideways about every 4 to 5 rows, so rows 166 and 167 are **one to two pixels wide**; the earlier "two rows, 3 to 5 pixels wide" had no basis and is dropped.

**Is a 167-row near body seen in play?** (V: `tools/vamiga-ref/scripts/nearbody-167.txt`, deck cockpit snapshot.) The bands of 167 rows with band 3 exist: the views KP7 (view code 12, looking up) and KP1 (code 13, looking down) show `V:4AA3C` = 0xA7 and band `V:4A923` = 3 on the deck (the near body is walked there by test 7 of `04a` section 8a; whether the op 4 gate lets the faces through there was not read). In code 12 the window is all sky: 0 of the 53,440 pixels sampled (every second column, rows 1 to 167) differ from the sky colour, so no mark of the near body is visible; the camera is pitched by about 90 degrees, so the object-frame triangles are not at the bottom centre there (hypothesis, not run). In code 13 the window is entirely black (pen 0): a pen-0 mark would be invisible. The single pass after an external view (test 6) runs with the window already at 144 (`V:4AA3C` = 0x90 at the first peek after the key; `V:4A8ED` was seen set in one of two runs), where both triangles are rejected. **No observed state therefore shows the near body in a 167-row view**; the rules above are the filler's, run in the rig. The frontend must draw what the core selects through the common filler (rejection by the top row, cut at the last row, classification on the uncut box), not skip the near body by view; the core reports the entry positive (op 4 returns 1) in every case **except above 5,120 ft, where it returns -1** (the op 4 row of the table above).

**Ops 7 and 8 (the bridge faces, ids 48-52; V, 1,600 constructed faces).** The operands are not slots. Op 7 takes five words (pen, a, b with the flip in bit 15, c, d); op 8
takes six (pen, c, a, b with the flip in bit 15, f, d). a and b are byte offsets of two **3D block vertices** (six bytes each from block offset 10; only x and z are
used), c and f are slot byte offsets, d is the slot base offset; slots below are numbered from that base. The test value T is the sum of two products: (x of a shifted
right by the block's L nibble, plus the entry x word, shifted left by the LOD, plus the camera offset `V:4AB2A`) times (x of a minus x of b), plus the same for z with
`V:4ABE6`, `V:4AB2E`; T is the 32-bit sum of two products of 16-bit words; every step that forms a factor (the shift right by L, the addition of the entry x word, the shift left by the LOD, the addition of the camera offset, the difference of the block coordinates) is a 16-bit word operation that wraps. The variant is B when (T >= 0) equals the flip bit, else A.

| op | variant A | variant B |
| --- | --- | --- |
| 7 | triangle [slot c, slot 0, slot 2] | triangle [slot c, slot 1, slot 0 - slot 1 + slot 2] |
| 8 | with p = slot f - (slot (c+1) - slot c), e1 = slot 1 - slot 0, e2 = slot 2 - slot 1: [p, slot 3, slot 3 + e1 + e2] | [p, slot 3 + e1, slot 3 + e2] |

The two variants occur about equally often in the rig sweep (half each of 800 faces per op). The polygon shift word `V:5103C` is 0 for every polygon.

**Ops 39 and 40 (quad and triangle on the absolute slots 0 to 4; V, 1,200 calls).** With dx and dz the camera minus the object position, per axis: -`V:4AB2A` minus (entry x word shifted left by the LOD), and
likewise for z. The vertex variant is A when dx and dz have the same sign (both non-negative or both negative), else B. The pen is the first word (high half) of the operand
long when \|dz\| <= \|dx\|, the second word otherwise.

| op | variant A | variant B |
| --- | --- | --- |
| 39 | [S0, S1, S2+S3-S1, S0-2 S1+S2+S3] | [S2, S3, S3+S0-S1, S2+S0-S1] |
| 40 | [S0, S1, S4] | [S0, S2, S3] |

Op 39 draws a second quad when `V:4A83E` is non-zero, with the extra operand word as the pen and the same shape in both variants: [S0, S0+S2-S1, S0-2 S1+S2+S3, S0+S3-S1];
the return is the OR of the two clip results. `V:4A83E` is written only by the pitch-gate routine `0x143F2`: 1 when the pitch word (the view pitch long `V:4AB40`, or the word `V:4AB4C`
in an external view, shifted right by 3, low word, signed) is in (600, 3150) exclusive, else 0 (`04a` section 1; V, 5,000 states, 0 differences).

**The slot-1 test of ops 39 and 40 is word arithmetic (V: `prim_ops.py SEED 4000 edge`, seeds 1 to 4: 16,000 calls of each op with the three words of slot 1 drawn from -32,768, -32,767, -1, 0, 1, 32,766, 32,767 and from -40 to 40, and -32,768 forced into x in half of the calls and into y in 30%; 0 differences; in about 5,500 calls of each op the 32-bit rule would have decided the other way).** Both ops return -1 at once (nothing drawn, negative condition codes) when slot 1 (x, y, z) satisfies x > z, or the **word** negation of x > z, or y > z, or the word negation of y > z, and otherwise go on; the negation is the 68000 word negation, so **the negation of -32,768 is -32,768**: a slot 1 with x = -32,768 is never beyond the minus plane (the first x test and the second both fail for every z), and with x = y = -32,768 it passes for every z, where the arithmetic rule |x| > z would reject it for every z. There is no separate depth test: a slot with z < 0 is rejected because x or -x (or y or -y) exceeds z, except in the -32,768 cases above, and the apex (0, 0, 0) passes. A port that negates in `int` rejects a slot 1 with a -32,768 coordinate that the original lets through to its clipper. Such words are the wrap of a 16-bit sum; whether a game frame produces them for ops 39/40 (the small objects of models 61-64, 89, 90, 107, 108) was not seen (C), and what the clipper then draws is whatever it computes from the wrapped words: in one call of the seed-5 run it overran its buffers and the rig's log pointer, so the harness runs every passing call on a fresh rig and checks the pointer. (The variant choice of the two ops also negates the camera words `V:4AB2A` and `V:4AB2E` with word negations at `0x23B36` and `0x23B38`; those were not run at the word edge: C.)

**Op 9 reads past its quad (V: `prim_ops.py SEED N op9`, seed 1, 1,500 calls, 0 differences; the repro of the draw differential, `DRAW_DIFF_OPS=26,9` seed 3 sequence 446, is this).** The handler builds its four vertices in the polygon buffer (`V:5103C`: shift word, count word, then vertices from `0x51040`, three words each (x, y, z; stride 6 bytes; the depth of vertex k is the word at `0x51044 + 6 k`, V3's at `0x51056`): V0 = slot a, V1 = slot a + 1, V2 = slot a + 2, V3 = V2 - (V1 - V0)) and then tests "all depths behind" by ANDing the depth words of the buffer. The test addresses the buffer relative to the end of the quad, not to its start: it ANDs the depth of V3 with the depth words at `0x5105C`, `0x51062` and `0x51068`, which are the depth words of the vertex positions 4, 5 and 6 (0 based; the fifth, sixth and seventh), and takes V0, V1 and V2 not at all. **The quad is dropped (return 0 with non-negative condition codes, the clipper is not called) exactly when that AND is negative**, that is when the depth of V3 and the three words found there are all negative; otherwise it goes to the clipper, whatever the depths of V0 to V2 (the rig: 1,500 calls with the three words drawn from -3,000 to 3,000 and the special values -1, 0, 7; 49 of them rejected, the clipper reached in exactly the calls the rule predicts). Op 10 takes the AND of its own four depths (V0 = slot a + 3, V1 = V0 + (slot a + 1 - slot a), V2 = V1 + (slot a + 2 - slot a + 1), V3 = V2 - (V1 - V0)) and the stale words play no part: the same harness, 1,500 calls, 27 rejected, 0 differences. **The three words are residue.** Nothing clears the buffer: the 42 code sites that address it write the shift word, the count and the vertices they build, and the clipper does not write it (the buffer after op 26 and its clipper call equals the input, nine vertex positions compared). **Who leaves vertices at positions 4 to 6 (V: `polybuf_writers_check.py`, every handler called directly with a sentinel in 12 vertex positions, 300 calls per operation; and a whole F/A-18 draw).** Operations that write position 4 or higher: **op 3** (a face of 3 to 6 vertices: positions up to 5), **op 24 and op 25** (their count, up to 7 in the shipped data, positions up to 6), **op 26 (count + 1 vertices**: V0 to V_count; its count 6 writes seven, the clipper reads six), and the entity draw (an F/A-18 draw leaves position 4 or 5 written, from op 3 and the five-vertex polygons of op 55). Operations that write positions 0 to 3 only: all the others of the table (the quads and triangles of ops 7 to 10, 15, 16, 17, 19 to 23, 39, 40 and the rest). **Order of the writes (V: `polybuf_writers_check.py early`, 300 random calls per op with a sentinel in the shift word, the count word and the first eight positions).** Ops 3, 69 and 55 write the **shift word and the vertices before their early-outs**, as op 26 does: op 3 (`0x2377C`) clears the shift word and writes every listed vertex, then returns -1 when all depths are negative with the **count word not written**, and stores the count word only after that test (so a face rejected later by the face test, no back pen, has its count word written, and the clipper is not called); op 55 (`0x23820`) does the same polygon by polygon (shift word cleared once at entry); op 69 (`0x23722`) writes shift 0 and count 4 at entry and vertices 0 to 2 before its all-behind test (vertex 3 only if it passes); its all-behind test ANDs **four** depths, z0, z1, z2 and the computed z3 = z0 - z1 + z2 (V: `prim_ops.py` op 69, the code at `0x23750`-`0x2376E`). Observed classes in 300 calls each: op 3: clipper reached 174 / all-behind rejected, count not written 66 / face rejected, count written 60; op 69: 188 / 112 rejected with count written; op 55: 172 / 40 / 88. A port that writes the buffer only inside the fill path leaves stale positions that the original has overwritten. The clipper does not write the buffer (the buffer after op 26 and its clipper call equals the input). **The sea mesh polygons of stage [68] (V: `mesh_check.py`, 1,020 random states, 14,606 polygons, 76,704 vertices, 0 differences, the buffer pre-filled with sentinels)** are built in this buffer with the same layout as the object polygons: the pass clears the shift word at entry (also when no polygon follows: 218 of the 1,020 states drew none), and for each polygon stores the count word and exactly n vertices of three words (x, y, z, stride 6 from `0x51040`, the camera-space point of `04` "Vertices" in that order), n up to 18 by the table check, 14 at most in these states; it writes nothing else, so the positions from n on keep the words of the earlier polygons of the frame (and of earlier frames, as everywhere), and the residue after the pass is the sequence of all its polygons written in order, not only the last. The pass is the one other writer of positions 4 to 6 besides ops 3, 24, 25, 26 and 55; the sky fill hands its polygon to the filler at `V:5043C` and does not use this buffer (read from the code, C). The 42 code sites that address the buffer are not all exercised separately (the general handler at `0x2377C` is C; the mesh is V). So the state that makes the rig reject an op-9 quad that the port draws is the **polygon buffer contents of earlier polygons, carried across objects and frames**; the repro has op 26 with count 6 first, whose slots 5, 6 and 7 are the three words. The buffer is zero at power-on and a zero depth is not negative, so a first op 9 always reaches the clipper. Ruling for the port: keep the 7-vertex buffer as persistent state written by every polygon-building op exactly as the original writes it (op 26 with count + 1 vertices) and apply the op-9 test as stated.

**Vertex synthesis (ops 41-44, 59; V, 200 random slot arrays each).** All adds, subtracts and shifts are 16-bit word operations, every shift an arithmetic shift right; h(x) is x
shifted right by 1 and q(x) by 2. V0..Vn are the slots from the operand's base; "b+k" is slot base + k. Ops 42, 43, 44 and 59 first run the same fragment on a second slot array
inside the object record (`+0xA4`), which no polygon uses. The shipped data uses base 0 only (op 41 only in model 62, op 59 only in model 4).

| op | new slots |
| --- | --- |
| 41 | with d = (V2 - V3) shifted left by s (arithmetic right by -s when s is negative): b+9 = V3+V2-V1, b+10 = V3 - d, b+11 = V5-V6+V7, b+12 = b+11+V8-V6, b+13 = V5+(V4-V6), b+14 = V8+(V4-V6), b+15 = V7+(V4-V6), b+16 = b+11+(V4-V6), b+17 = b+12+(V4-V6) |
| 42 | b+17 = V3+V4-V8; b+18 = V2-V1+V6; b+19 = h(V6-V1)+V7; b+20 = h(V6-V1)+V16; b+21 = h(V6-V1)+h(V3+V4)+2(V6-V1); b+22 = h(b+21+V0); b+23 = V4+V5-V8; b+24 = h(V8+V11) (V11 is the slot op 43 wrote); b+25 = V4+V5-V3; b+26 = V6+V9-V1; b+27 = 2V6-V1; b+28 = V2-2V1+2V6; b+29 = h(V6-V1)+V0; b+100 = h(h(b+13+b+14)+b+10), reading the slots op 43 wrote |
| 43 | b+10 = V3+V4-V6; b+11 = h(V3+V4)+q(V5-V3); b+12 = h(b+11+V0); b+13 = V7+q(V9-V7)+h(q(V9-V7)); b+14 = V4+V5-V3; b+15 = V3+V4-V7 |
| 44 | b+5 = V0+h(V1-V0); b+6 = V0+h(h(V1-V0)) |
| 59 | b+21 = V6-V7+V10; b+22 = h(V3+V4); b+23 = 2h(V3+V4)-V9; b+24 = 2h(V3+V4)-V8; b+25 = V4+V5-V3; b+26 = h(h(V4-V3+2V5)+V5); b+27 = h(h(V4-V3+2V5)+V4-V3+V5); b+28 = h(4h(V3+V4)-2V3+2V5-V8-V9); b+29 = h(V1+V2); b+30 = V6-V7+V12; b+31 = V6-V7+V14; b+32 = h(V6-V7)+V1+V6-V7; b+33 = h(V6-V7)+V2+V6-V7; b+34 = h(V6-V7)+V11+2(V6-V7); b+35 = h(V6-V7)+V13+2(V6-V7); b+36 = h(V6-V7)+V1+2(V6-V7); b+37 = h(V6-V7)+V2+2(V6-V7); b+38 = 2h(V3+V4)-V3+V5-V9; b+39 = 2h(V3+V4)-V3+V5-V8; b+110 = h(h(b+18+b+19)+b+16) |

These supersede the approximate rows of `02` section 5b (b+21 of op 42 is not (V3+V4)/2 + 5/2 (V6-V1) to the unit; b+13 of op 43 is about three eighths of the way, not the midpoint; the
"non-linear" slots b+22, b+100 of op 42, b+11 and b+12 of op 43, b+6 of op 44 and b+26, b+27, b+110 of op 59 are the formulas above: they read slots written by an earlier op, or contain a second shift).

### Primitive output: lines, points, fill modes (what the frontend draws)

The output routines write the 3D window's four bit planes directly (40 bytes per row, 8,000 bytes per plane; the plane pointers are the structure at `V:4A76E`): the pen
is the low four bits of the word `V:4AA0C`, and **pen bit k goes to bit plane k**. Rows are limited to the last 3D row `V:4AA3C` (`04`: 144, 167 or 179 by view). The
tree-walk entry (`0x225F0`) resets the fill state at the start of every drawn object: plane mask `V:4A79E` = 0x000F, pattern word `V:4A7A0` = 0xFFFF (negative:
solid pen), toggle word `V:4A7A2` = 0; the state is **not** reset between the items of one object: an operation that writes it leaves its words in force for the operations after it (`Fill state`, below).

| output | routine | effect | tag |
| --- | --- | --- | --- |
| polygon | filler `0x34A28` (blitter area fill) | solid mode (`V:4A7A0` negative): per plane k in the mask, the polygon area is set where pen bit k is 1 and cleared where it is 0; pattern mode (`V:4A7A0` >= 0): per plane k in the mask, bit k of the pattern word sets (1) or clears (0) the area, and bit k of `V:4A7A2` set toggles the plane instead. The bounding box is rejected when its top row is below `V:4AA3C`, otherwise the fill and the edges are cut at that row (`Where the near body falls`, above); bounding boxes of at most 2 rows or 1 column take the small-shape paths (below). The alternate fill path `0x34EEC` (taken when the pattern word is not negative and `V:4A7A4` is not zero) is **set up in one place only: the near-ground pass**, stage [90] of `04`, which stores `V:4A7A4` = 0xFFFF and leaves it; every writer that stores all four words resets it to 0 (the entries of the plain drawer, ops 17 to 23, 27 and 30, the ground polygon tail, the sea mesh: \"Fill state\" below). Besides the pass's own triangles the path is therefore reached by **a type B object with op 26** (mask 3, pattern 3: models 36, 39-42, 99, 100) drawn after the pass with no reset in between: its polygons take the alternate blit instead of the pattern fill (`04`, \"The block left behind\"; derived from the writers, R/C). The path replaces the per-plane blits by one blit of the three-source form (A = the filled scratch plane, B = plane 3, C = D = plane 2, descending, BLTCMOD 40): **in the word rectangle of the polygon (from the word of max(minx - 1, 0) to the word of maxx, rows miny to min(maxy, last row)) a pixel under the filled area gets plane 2 := plane 3, and every other pixel of the rectangle gets plane 2 from the C word read W words further left for each row up**, whatever the mask and the pattern are (`04`, \"Stage [90]\": the rectangle extent rule, the C rule and their verification) | V for the pixels of the general fill and the small shapes against the rig with a blitter model (`nearground_pixels_check.py`) and the emulator bitmaps (`04`, Stage [90]); R for the blitter model itself |
| line | `0x3455E` | one-pixel blitter line, solid texture; the planes are the bits of the **low byte of the mask word** (`V:4A79F`, planes 0 to 3); per plane k: pattern word `V:4A7A0` negative: the line is set where pen bit k is 1 and cleared where it is 0; otherwise the bit k of the byte `V:4A7A1`, which is **the low byte of the pattern word** (no code writes it separately; `Fill state` below); the toggle word is ignored; rows limited to `V:4AA3C`; the start end, the stepping and the cut: "The raster line" below | V (`line_raster_check.py`, pixels against the rig with a blitter model; 2,000 lines) |
| point sprite | `0x340D4`, `0x340EA`, `0x3414E` | 1 by 1 pixel at (x, y); 2 by 1 at (x-1, x) (at x = 0 also a pixel at (319, y-1)); 2 by 2 at rows y and y+1, columns (x-1, x), or (x, x+1) when x mod 16 is 0, falling back to the 2 by 1 sprite when y is at or beyond `V:4AA3C`; a sprite with y <= 0 is rejected; each plane takes its pen bit (a bit equal to 1 sets the pixel in that plane, 0 clears it), planes outside the mask are untouched | V (1,500 draws per sprite) |
| disc | `0x33CAC` (op 62) | 2R rows from y-R+1 to y+R, each the span [x-h, x+h] clipped to columns 0 to 319 and rows 1 to `V:4AA3C`, h from the table of the generator below (the first R rows, then the same R rows mirrored); R = 0 or less draws the 2 by 1 sprite; R is limited to 127; solid pen | V (200 discs, 15,326 rows, pixel exact) |

**The disc generator (`0x33CAC`; V: `disc_check.py`, the table and every pixel).** The routine fills a table of extents in a scratch buffer: a first word R, then pairs (left, right) with left negative and right positive. R of 1 has the single pair (-1, +1)
and the disc is that pair on two rows. For R of 2 or more, two cursors run over the pair array, the top cursor from pair 0 and the bottom cursor from pair R. The Bresenham midpoint state is x = 0, y = R and the decision value d = 3 - 2R. While x < y:
(1) write (-x, +x) at the top cursor, then (-y, +y) at the bottom cursor (the later write wins where the cursors meet); (2) if d < 0, d = d + 4x + 6; otherwise d = d + 4(x - y) + 10, y = y - 1 and the **top cursor advances by one pair, which is the only
place it advances** (so a top pair keeps the last x written for its y); (3) the bottom cursor goes back by one pair; (4) x = x + 1. After the loop, if x = y, (-x, +x) is written at the top cursor. The disc's rows are the first R pairs in order, then the same R pairs
in reverse order (2R rows, the first at y-R+1). The half-widths h of the rows: R = 2: 1,2,2,1; R = 3: 1,2,3,3,2,1; R = 4: 1,3,3,4,4,3,3,1; R = 5: 2,3,4,5,5,5,5,4,3,2.

**The raster line (`0x3455E`, entry `0x34558` with the last row 199; V: `line_raster_check.py`, seeds 1 to 4, 2,000 random lines with horizontal, vertical and short ones, random last row, mask, pen and solid or pattern mode, the blitter model of the rig: 0 differences).** The routine takes (x0, y0) in D0, D1 and (x1, y1) in D2, D3 and draws a blitter line in which **every pixel is written** (the single-dot bit is clear; the edge routine `0x3508A` of the polygon fill, `04`, is the same code with the single-dot bit set and an XOR minterm in plane 0). Rules: (1) **start end**: the end with the smaller y (an unsigned compare of the two row words; the callers' rows are 0 to 199); with equal y the first end (x0, y0). (2) **start pixel**: (x of the start end, y of the start end + 1): the row of the start end itself is never drawn, the last row drawn is the larger y, and a line with equal y (horizontal) is drawn on the **row below its y**, from x0 toward x1. (3) dy1 = |y1 - y0| - 1 (0 for equal y) and dx = (x of the other end) - (x of the start end), signed; the major axis is x when |dx| >= dy1 and y otherwise, major and minor being the larger and the smaller of |dx| and dy1. (4) **x step**: the error word starts at 4 minor - 2 major (16 bits), the sign flag is its sign (set when negative). Each pixel is written, then: when x is major, x moves one toward the other end (right when dx >= 0, left otherwise) at every step and y moves down one when the flag is clear; when y is major, y moves down one at every step and x moves one toward the other end when the flag is clear; then the error word gets 4 (minor - major) if the flag was clear and 4 minor if it was set, and the flag takes the sign of the new word. (5) The line has major + 1 pixels, cut at the last row L: with a = L - (start row), a steep line (y major) has min(major, a) + 1 pixels and a shallow one has major + 1 when dy1 <= a, else (round half up of major a / dy1) + 1; nothing is drawn when the start row is beyond L. Which planes take which bit is the table row above (mask `V:4A79F`, pen bits or the byte `V:4A7A1`).

**Which size an op uses.** Ops 1 and 49 (entry `0x33780`) pick the sprite from the scaled distance sd (the drawer's local word, `04a` P1) and the LOD L: `0x3414E` when
(0x30 shifted right by L) is at least sd, else `0x340EA` when (0x50 shifted right by L) is at least sd, else `0x340D4` (nearer is larger). Op 58 always draws `0x340EA`; op 62 draws the disc.
The projection is that of `fx_project` (above): screen y = 180 minus the clamped sy' for the point path, and a point is rejected unless strictly inside the pyramid and its row is
at or above `V:4AA3C` (V, 1,600 point calls).

**Fill state: what each operation writes and what it inherits (V: `stipple_check.py`, 1,000 sequences of 3 to 8 operations, 5,467 handler calls, 6,638 output calls, op 49 included: the words seen by the filler, the line routine and the sprites at the moment of the call are those the table below predicts; 0 differences).**
The state is four words: mask `V:4A79E` (planes touched), pattern `V:4A7A0` (negative: solid pen; otherwise the plane bits), toggle `V:4A7A2` (a set bit k toggles plane k instead of setting or clearing it) and `V:4A7A4` (the alternate fill path flag: 0 except after the near-ground pass, which stores 0xFFFF; see the rows of the table and the end of this section). It is **per object, not per
operation**: the entry of the common drawer (`0x225F0`) sets mask 0xF, pattern 0xFFFF and clears toggle and `V:4A7A4`; the entry of the type B drawer (`0x22D1A`) sets mask 0xF and pattern 0xFFFF **only**, leaving the toggle word (and `V:4A7A4`) as the last object left them. **V** (`typeb_toggle_check.py`, 987 one-entry walks of random items with sentinels in the four words: the common drawer leaves toggle 0 and `V:4A7A4` 0 after an object with no toggle-writing operation, 308 walks; the type B drawer leaves both sentinels, 208 walks; the words after every walk equal the rule applied to the operations that ran). It is the one stale word for the toggle; `V:4A7A4` is the second: written 0xFFFF by the near-ground pass of `04`, and reset to 0 by an entry of the common drawer (`0x225F0`, a long clear of the toggle and `V:4A7A4`), by every operation that stores the four words (17, 18, 19, 20, 21, 22, 23, 27 and 30: V, `prim_ops`-style calls with a sentinel, 150 per operation, also on their early-outs, toggle 0 for 17, 18, 19, 22, 30 and 2 for 20, 21, 23, 27), by the tails of the ground polygon routine (1, 1, 0, 0) and by the sea mesh stage (2, 2, 0, 0). The HUD and panel code and the lines (ops 2, 13, 48, 74, 75), 32, 57 and 26 store mask and pattern only. All writers are absolute-address references (no address register is ever loaded with the block: 38 references to `0x4A79E`, 1 to `0x4A7A4` in the image). The writers of toggle 2 (operations 20, 21, 23, 27) occur in **plain** models only (the rig walks saw them in models 47, 59, 60, 66, 69-71, 74-77, 79, 82-85, 93, 94, 96, 97; of 429 walks that ran one of them, 47 ended with toggle 2, the others ended after a later 19 or 22); the type B models write only toggle 0 (op 30, models 105, 106 and 109-132). The **consumers** of the carried words are the type B models that use op 26, models 36, 39-42, 99 and 100: op 26 in pattern mode honours the toggle (plane 1 is toggled instead of set) and, since it stores mask and pattern only, takes the alternate blit `0x34EEC` instead of its pattern fill whenever `V:4A7A4` is non-zero, that is after the near-ground pass until a writer above resets it (`04`, "Stage [90]"). The op-30 models (105, 106, 109-132) are not consumers: op 30 stores all four words with `V:4A7A4` = 0 before its first polygon. In 36 pairs of walks (a plain object ending with toggle 2, then a type B op-26 object, the toggle written back between the walks because the harness rebuilds the frame) the type B entry kept toggle 2 and all 33 filler calls of op 26 saw (mask 3, pattern 3, toggle 2). The pixel effect (plane 1 toggled, not set) is R (no blitter), and **whether a frame ever draws a toggle-2 plain object before an op-26 object** (the queue order) is C, not examined; it is a possible difference in the colour of those ground patches. After 19, 22, 30, 17, 18 or a common-drawer entry the word is 0. Each operation either writes words or leaves them:

| operations | mask | pattern | toggle | notes |
| --- | --- | --- | --- | --- |
| 2, 48, 74, 13, 75 (lines) | 0xFFFF | 0xFFFF | untouched | all planes, solid |
| 19, 22 | 0xF | 0xFFFF | 0 | solid; `V:4A7A4` 0 |
| 32, 57 | 0xF | 0xFFFF | untouched | solid |
| 20, 21, 23, 27 | 2 | 0 | 2 (and `V:4A7A4` := 0) | plane 1 toggled (pattern 0, the pen is unused; ops 20, 21 set pen 13, op 23 leaves the pen; op 27 writes the words before its early-out) |
| 30, 17, 18 | 8 | 8 | 0 (and `V:4A7A4` := 0) | plane 3 set (op 30 writes the words before its early-out) |
| 26 | 3 for the polygon, then 0xF | 3, then 0xFFFF | untouched | **restored to the solid defaults, not to the state before the op**; when all N + 1 depths (the N listed and the slot after them) are behind it writes nothing: neither these words nor the pen |
| 1, 3, 6, 7, 8, 9, 10, 11, 12, 14, 15, 16, 24, 25, 39, 40, 49, 58, 62, 69 and every other operation | untouched | untouched | untouched | inherit the state of the object so far |

The persistence has no visible effect on the shipped data within a model: no model mixes an operation of the pattern rows (20, 21, 23, 27, 30, 17, 18) with an inheriting operation (scan of all decoded models, reviewer's scan: no model has both), and an object's first inheriting operation sees the defaults. A port may therefore
set the words per operation as the table says, provided it keeps the object's state across items (so that the inheriting operations of a model that does contain a pattern operation, such as the lines of ops 11, 12, 14 and 6 after a pattern polygon, would see it) and resets it at the start of every object by the rule above.

**The line pattern byte `V:4A7A1`.** No instruction writes it: it is the low byte of the pattern word `V:4A7A0` (the pattern word is stored as a word; 0xFFFF reads as the byte 0xFF, 0x0008 as 0x08). The line routine reads it only when the pattern word is non-negative. In flight the lines are **solid**: every line operation that draws a line sets the pattern word to 0xFFFF
(ops 2, 48, 74, 13, 75, 19, 22) except ops 6, 11, 12, 14, which inherit the state, and no shipped model gives them a pattern state; the planes then take the pen bits. A line drawn in pattern mode (after one of the pattern rows) would set or clear plane k by bit k of the pattern byte, for the planes in the low byte of the mask.

**Small shapes in the filler (R, rig).** After the row test, a bounding box with height h = bottom row minus top row and width w = right minus left column takes one of these paths before the blitter fill: h of 3 or more and w of 1 or less: one line from the box's top-left to its bottom-right corner (drawn with mask 0xF and a solid pattern while `V:4A85A` is 0, the state restored afterwards), returns 1 without a fill;
h of 2 and w of 2 or less: a 2 by 2 sprite (`0x3414E`) at row top+1 (skipped when beyond the last row); h of 1 or less and w of 2 or more: the same line; h of 1 or less and w of 1 or less: a 1 by 1 sprite (w of 0) or a 2 by 1 sprite (w of 1) at row top+1 (skipped when beyond the last row); everything else is filled as a polygon.
This is why the thin near-body triangle is a diagonal line. The routines return 1 in these cases, which makes the caller skip the area fill.

**The clipped line routine `0x33936` (V: `line_model.py`, a word-exact model equal to the rig on 100,000 random segments, 55,028 of them drawn: the line routine's four screen words and the return agree on every one).** It clips **each end point separately against the other end point's original coordinates** (never against an already clipped end), projects the result and draws; it returns 1 when it drew, 0 otherwise.
For an end point E = (x, y, z) and the other raw end F = (x', y', z'), with the planes XP (z = x), XM (z = -x), YP (z = y) and YM (z = -y), "E is beyond XP" means x >= z, "beyond XM" -x >= z, "beyond YP" y >= z, "beyond YM" -y >= z (**closed**: on the plane counts as beyond); "F is outside" the same plane means z' <= x', -x', y', -y' respectively (**closed**: F on the plane counts as outside).
The procedure tries planes in an order fixed by where E lies (first table), stops at the first success, and rejects the whole segment (return 0, nothing drawn) at the first rejection:

| case (tested in this order) | planes tried, in order | rule |
| --- | --- | --- |
| E beyond XP | XP, then XM, then the y stage | XP: F outside XP rejects the segment; the hit on XP is used when it lies in the closed pyramid. XM: tried only when the XP hit has z < 0 and F is not outside XM (z' > -x'); used when its hit lies in the closed pyramid. Else the y stage. |
| E beyond XM (and not XP) | XM, then XP, then the y stage | the same with the planes exchanged |
| y stage after an x case | E must itself be beyond YP (when y >= 0) or beyond YM (when y < 0), else reject; then YP (resp. YM) as above (F outside rejects), then the opposite y plane only when the first hit has z < 0, E is beyond it too and F is not outside it | a hit is used when it lies in the closed pyramid; otherwise reject |
| E inside both x half-spaces, beyond YP | YP, then YM (only when the YP hit has z < 0 and F is not outside YM) | no further stage: if neither hit is used the segment is rejected (the x stage that follows in the code always rejects, because E is inside both x half-spaces) |
| E inside both x half-spaces, beyond YM (and not YP) | YM, then YP (same condition) | the same |
| E inside all four half-spaces | none | E is projected as it is when its depth is not negative |

A hit is "in the closed pyramid" when its depth is not negative and |x| <= z and |y| <= z. **Intersection** (the four routines `0x33BB2`, `0x33BE0`, `0x33C14`, `0x33C42`, one per plane): with the margins m(P) = z - x (XP), z + x (XM), z - y (YP), z + y (YM) as word differences, m1 of E and m2 of F, den = m2 - m1 (a word; zero is a hang: see "The zero difference" below),
the **other lateral coordinate** o and the depth z are interpolated **from F toward E**: o_hit = o_F - T(m2 * (o_F - o_E) / den) and z_hit = z_F - T(m2 * (z_F - z_E) / den), where T(a b / c) is the 32-bit product of the signed words a and b divided by the signed word c, truncated toward zero, and the difference of two coordinates is a word; when the quotient lies outside -32,768 to 32,767 the value of T is the low word of the 32-bit product instead
(the model reproduces this; the random full-range segments of the check exercise it). The coordinate on the plane is set from the depth: x = z_hit (XP), -z_hit (XM), y = z_hit (YP), -z_hit (YM). (The cut is therefore computed from the far end, not the near one; a port that interpolates from the outside end rounds differently.)
**Projection** of the used point (E or the hit), end by end: a depth that is not positive here stores the word 0x16 in `V:4AA56` and rejects the segment (return 0; V: the word was 0x16 after 210 of the 100,000 segments and untouched after the other 99,790); otherwise sx' = T(160 x / z) + 160 and sy' = T(90 y / z) + 90 as words (the overflowing divide again keeps the product's low word), clamped to 0..319 and 0..179, and the screen point is (319 - sx', 179 - sy'). The line routine then
receives (x1, y1, x2, y2) of the first and second end point in the operand order. (The point ops 1, 49, 58, 62 project with the row 180 - sy' instead; op 48 projects with 179 - sy' like this routine; the polygon path is `fx_project` above.) The geometric summary, "the segment counts when it meets the open pyramid", holds except for the boundary cases that the cascade decides: a segment that only touches a
plane or lies in it (the port drew the segment (-32,1,24)-(-38,5,38) as the point (319,78); in the rig F lies on the XM plane, so E beyond XM with F outside rejects it: V, `line_model.py`). The port replaces its plane loop by this table; **no other order or inclusive/exclusive choice** matches the rig.

**The zero difference (V: `line_model.py`, `den_zero_check`; 18,164 segments).** The original tests the word den = m2 - m1 for zero before dividing and, when it is zero, **stays in a one-instruction loop**: the routine never returns (a hang, not a processor exception). The rig run stops at its instruction cap with no line drawn on every one of the 18,164 segments of full-range words whose margin was made congruent to the first end's (8,954 reaching the first plane tried, 9,210 a later plane), and the model's zero-divide cases are exactly these. **Bound:** den is the difference of two margins modulo 65,536. On the first plane tried E is beyond it (m1 at most 0) and F is not outside (m2 above 0), so den is zero only when m2 - m1 is exactly 65,536 (arithmetic): one end needs a margin of magnitude at least 32,768, that is a coordinate of magnitude at least 16,384; none was found with every coordinate within +-16,384 (400,000 targeted segments per plane at each radius; found within +-32,767). On a later plane (the opposite plane after a first hit with z below 0, or the y stage) den can be zero for a margin that is equal (not only congruent): in the same survey none was found within +-11,000 and 122 within +-12,000 (the rig hangs on 20 of 20 sampled at +-12,000). Whether the game's camera-space words reach such magnitudes is open (R: not seen in any drawn frame of the runs; the notes' "never met" was never a bound). **Port ruling:** a segment with den = 0 is **rejected and counted** (return 0, nothing drawn, a debug counter), never a hang.

**Every negation in the routine is a word operation (V: `draw_rules_check.py neg`, 400 segments at seed 1 with coordinates drawn from -32,768 far more often than chance: the rig equals the word model on all 400).** The tests "beyond a minus plane" (-x >= z, -y >= z), the "outside the closed pyramid" test of a hit point (|x| > z as x > z or -x > z, likewise y), the margins z + x and z + y, and the minus-plane hit coordinates (x = -z_hit on XM, y = -z_hit on YM) are all word negations: **the negation of -32,768 is -32,768**, not +32,768. A coordinate -32,768 is therefore never "beyond" a minus plane by those tests, and a hit depth of -32,768 on a minus plane gives the coordinate -32,768 as well. `line_model.py` takes word negations throughout; in the 400 segments 64 give another result under a 32-bit negation. A port that negates in `int` misses exactly these segments.

**What the port's differential found at 40,000 sequences (V: `tests/rig/test_draw_diff.c`, `DRAW_DIFF_N=40000`, seeds default and 1 to 6, 280,000 sequences, 16 differences; seed 4 none).** Two classes, nothing else: **11 are den = 0 hangs** (ops 6 and 22: the rig run stops at its instruction cap, the harness stores the error word 0x7777 = 30,583 as the return; for each of the 11 the model finds a rung or stroke whose segment has the zero difference of the paragraph above: ops 6 in seeds default, 1, 3, 5 and 6, op 22 in the default seed seq 11838); the port rejects such a segment, correctly, but must count it (`04a` section 8b, rule 9). **5 are the word negation**: seq 16348 and seq 17538 (op 22: default seed and seed 1), seq 5908 (op 14) and seq 6601 (op 6) in seed 2, seq 33022 (op 14, seed 6). In each the segment has a coordinate of -32,768 (or produces a hit at -32,768), and the word model with its negations replaced by 32-bit ones reproduces the port's wrong value in every one of the five (e.g. seq 33022: second end x = 75 instead of 113; seq 17538: second end x = 282 instead of 0; seq 5908 the port rejects a segment that the rig draws; seq 6601 the port draws a rung that the rig rejects). No 16-bit wrap in the Q4 step of op 6, in the subtraction of op 14 or in the stroke offsets is missing: all other calls agree.

**Line operations 6, 11, 12, 14 (V: `hatch_check.py`, 1,000 calls each, exact end points and returns; op 11 included).** Op 6 (`0x23E04`) takes N, A1, A2, B1, B2 (vertex byte offsets) and a pen; N is at least 2 in the shipped data (models 107, 108). For each axis the two end vertices are taken as long values shifted left by 4 (Q4), the step is the signed difference divided by N (a signed 16-bit divide truncated toward zero; an overflowing quotient leaves the low word of the 32-bit
difference), and rung k (k = 1 to N-1) joins A1 + k * stepA to B1 + k * stepB, each coordinate rounded back as (v + 8) shifted right 4 (arithmetic; half rounds up), then the clipped line; the rungs are drawn in order k = 1 to N-1; the return is the OR. **N below 2 (V: `hatch_check.py`, 254 of the 1,000 calls, 42 of them N = 0; and `draw_rules_check.py op6`, one run each of N = -32,768, -32,767, -2, -1, 0, 1, 2, 3, 4, 257, 258, 300 and 32,767, the rungs drawn, the buffer written and the trap compared with the rules below: 0 differences).** The handler has two loops. The first fills a buffer of 24-byte records from `0x4FE3C` (the A point as three longs, then the B point as three longs, one record per rung) with the 68000 count-down loop on the word N-2, which decrements and then tests for -1: it runs (N-2 as an unsigned word) + 1 times, that is N-1 times for N of 2 to 32,767, but **65,536 times for N = 1**, 65,534 for N = -1, 65,533 for N = -2, 32,768 for N = -32,767 and 32,767 for N = -32,768. The second loop draws the rungs from the buffer in order with a do-while on the signed word N-1: **R = max(N-1, 1) rungs**. Hence:

| N | rungs drawn | buffer records written (24 bytes each) |
| --- | --- | --- |
| 2 to 32,767 | N-1 | N-1 |
| 1 | 1 (the line from A2 to B2; step difference/1) | 65,536 (1,572,864 bytes, to `0x1CFE3B`) |
| -1 to -32,767 | 1 (rung k = 1, step difference/N) | 65,535 + N (65,534 for N = -1; 32,768 for N = -32,767) |
| -32,768 | **32,767** (N-1 wraps to +32,767; exact, counted by the stub) | 32,767 |
| 0 | none: the divide is by zero and raises the processor's divide-by-zero exception (vector 5; V: the trap stub was entered, no record, no rung) | 0 |

**N below 2 is undefined behaviour of the original, and no shipped item has it** (the 12 uses of op 6 are in models 107 and 108, all with N = 3): N = 1 and every negative N overwrite up to 1.5 MB above `0x4FE3C` (the rest of the game's data, and more than the 512 KB of a base machine), N = 0 is a processor exception (the Amiga would show a guru). **What the port harness saw as "stale words" is that overwrite** (V: the same call with the drawer frame register at 0x7F0200, outside the buffer, draws 1 rung and returns 1 for N = -1 and 1; with the register at 0x70000, the value `tests/rig/test_draw_diff.c` uses, which lies inside the buffer, N = -1 drew 382 rungs and returned 16,049 and N = 1 returned -16,111 in the same slots; the rung counter and the result word of the frame are read from addresses -0x30 and -0x7E from that register, C: so the fill loop trashes them). Port ruling: **N below 2 draws nothing and returns 0 (as N = 0), with a debug counter; no cap is needed for N = -32,768** (it falls under the same rule). A second limit, also outside the data: the buffer reaches the clipper's input words at `0x5163E` at record 256, so for N of 258 or more exactly **rung 257** reads a record that the previous rung's input words have overwritten and differs from the formula (V: the N = 258, 300, 32,767 and -32,768 runs: every other rung equals the formula).

Ops 11 and 12: the count N is bits 8 to 15 of the first operand, an unsigned byte (0x80 draws 128 segments), and **0 still draws one segment** (V: 247 and 235 of the 1,000 calls of ops 11 and 12), the pen bits 0 to 5; op 11 draws slot s to s+1 starting at v and advancing two slots per segment; **op 12 and op 14
subtract** the vector V[vref+1] - V[vref] from **both** end points of every line (the shift is minus that vector, per axis, word subtraction); op 14's lines are the (v1, v2) pairs of op 13, with the bit 15 flag on v2 of the last. Ops 11, 12 and 14 do not skip a pair with both depths negative (the clipped line decides), op 13 does (both depths negative skip the pair).

**Hatch and stripe geometry (ops 19, 20, 21, 22, 23, 32, 57; V: `hatch_check.py`, 1,000 calls per op with full-range words, the exact vertex lists, shift and count words, pen and fill words and the set of calls that count, 17,000 checks, 0 differences).** All vertices are camera-space slots (word triples); S0, S1, S2, ... are the slots from the operand's vertex v. Every sum and difference is a 16-bit word operation, h(x) is x shifted right arithmetically by 1 as a word,
and the stroke offsets are **h of the accumulated step**: acc_0 = 0, acc_(k+1) = acc_k + Q (wrapping words), o_k = h(acc_k) (so o_k is h(k Q), not k h(Q)). **Counts are do-while on signed words (V: about a quarter of the calls per op used 0, -1, -2, -7 or -32,768: ops 19, 20, 32 and 23 about 250 each, ops 21, 22 and 57 about 800 each because the row count and every row's stripe count are drawn that way):** an N, N1 or N2 of 0 or less draws exactly one stroke (one per half for ops 19, 20 and 32; for ops 20 and 32 the half that counts toward the return is then that one polygon), an N1 of 0 or less still has one row, and the stream still carries one N2 word for it; the shipped counts are 1 to 5. Strokes go to the clipped line routine (above, solid, the pen as stated), polygons to the polygon clipper `0x28B6C` (polygon shift word 0, vertex count 4). A call "counts" when the clipper returned 1 (the line is non-empty, the polygon reached the filler).

| op | operands | vectors | what is drawn, in order | counts toward the return | state written |
| --- | --- | --- | --- | --- | --- |
| 19 | pen, v, N | P = S0 - S2, Q = S1 - S2 | **2N clipped lines**. First N: k = 0 to N-1, from S2 + o_k to S0 + o_k (the edge S2-S0 moved along Q by half steps). Second N: k = 0 to N-1, from S0 - h(Q) - o_k to S2 - h(Q) - o_k (the same strokes on the other side of the edge, one half step further, **reversed in direction**); h(Q) and o_k are separate roundings | all 2N | pen = operand, mask 0xF, pattern 0xFFFF, toggle 0 |
| 20 (pen 13), 32 (pen operand) | [pen,] v, N | P = S0 - S2, Q = S1 - S2 | **2N polygons of 4 vertices**. First N, k = 0 to N-1: [S2 + o_k, S0 + o_k, S3 + P + o_k, S3 + o_k]. Second N: with o'_k = h(Q) + o_k: [S0 - o'_k, S2 - o'_k, S3 - o'_k, S3 + P - o'_k] (the first strips reflected, winding reversed) | **the first N only**; the second N are drawn and never count | op 20: pen 13, mask 2, pattern 0, toggle 2; op 32: pen = operand, mask 0xF, pattern 0xFFFF, toggle untouched |
| 21 (pen 13), 57 (pen operand) | [pen,] v, N1, N2(0) .. N2(N1-1) | P = S0 - S3, Q = S1 - S3, R = S2 - S3 | **N1 rows**; row i has N2(i) stripes and is based on the slot B_i = S(3+i) (row 0 on S3, row 1 on S4, ...; the slots must exist: the programs name them); stripe j of a row (the accumulation restarts in each row), o_j = h(j Q): [B_i + o_j, B_i + P + o_j, B_i + P + R + o_j, B_i + R + o_j] | all stripes | as op 20 (op 21) and op 32 (op 57) |
| 22 | pen, v, N1, N2(0) .. N2(N1-1) | W = S1 - S2 (the stroke vector), step Q' = S0 - S2 | **N1 rows of clipped lines**; row i is based on B_i = S(2+i); stroke j (the accumulation restarts in each row), o_j = h(j Q'): from B_i + o_j to B_i + W + o_j | all strokes | pen = operand, mask 0xF, pattern 0xFFFF, toggle 0 |
| 23 | v, N | P = S0 - S3, Q = S1 - S3, R = S2 - S3, T = P + h(3 Q), 3 Q = Q + 2 Q as words | **N polygons of 4 vertices**, one per slot: strip i (i = 0 to N-1) on B = S(3+i): [B, B + T, B + R + T, B + R] (a strip a step of one and a half edges wide; no stripe offsets) | all strips | mask 2, pattern 0, toggle 2; the pen is not written (unused in the toggle mode) |

Stream lengths: ops 19 and 32 take 3 words, 20 and 23 take 2, 21 takes 2 + N1, 22 and 57 take 3 + N1 (N1 read as 1 when it is 0 or less) (57: pen, v, N1 and the N1 counts); the shipped operand lists are in the Constants table.
**Where the port's residual differences of the code round come from** (read from the port's hatch code, `world.c`; not re-run): the items 50, 67 and 69 are the models 66, 83 and 85 (ops 2, 19, 20) and item 75 is model 91 (ops 22, 57). The port builds every stroke and strip from the three slots A, B, C only; the original's strips (ops 20, 32, 21, 57, 23) use a **fourth slot** (S3) for the far edge, the rows of ops 21, 22 and 57
are consecutive base slots (S3+i, S2+i), not bands i/N1 along A - C, the vectors are those of the table (origin S3 for ops 21, 23, 57; P = S0 - S3 and R = S2 - S3), the second half of ops 19, 20 and 32 is the reflected and reversed copy with two roundings, and the line clipper of the port differs from the cascade above at the boundary (inclusive tests) and in the intersection (from the near end).

**The block that op 31 names (V: `prim_ops.py`, 600 calls with operands 0, 4, 12, 20, 36).** The block is the one the level search chose for the draw: the drawer stores its base in `V:4AAEA` when it loads the block header (`0x21E02`; the other drawers at `0x2289E`, `0x22B3A`, `0x22D7A`, `0x22F18`), and the operand is a **byte offset into that block's vertex list**: the early-out reads the
two consecutive vertices at `V:4AAEA` + 10 + operand (the first two words of each of the two flat vertices: x0, z0, x1, z1). The pre-test of rule 6 (`04a` section 8b) is the same early-out on the block's vertices 0 and 1 (offset 0) at block entry; the shipped op 31 operands are 12 and 20 (flat vertices 3 and 5). Nothing in the early-out depends on any other block.

**Fixed-formula synthesis without an operand (ops 38 and 46, 45 and 47, 56, 63, 64, 65; V: `synth2_check.py`, 500 slot arrays each, every word of the array compared, 0 differences; and `synth_note_check.py` from the text of the table below, 200 arrays each, 0 differences).** These handlers read no operand and no object state: each is a straight-line fragment on the absolute slots (base 0), 16-bit word arithmetic, h the halve, q the quarter (arithmetic shifts right by 1 and 2 of a word). They are **not selectors**; the extractor must apply them as synthesis items (they
are the operations behind the blocks reported as "state dependent, vertex output not documented" for models 0 (ops 38, 45), 2 and 13 (56, 64), 11 (56, 65) and 10 (63)). Ops 56, 63, 64 and 65 first run the fragment on a slot array inside the object record (`+0xA4` of the entry), as ops 42-44 and 59 do, which no polygon reads. Sk is slot k, M = h(S1 + S2):

| op | new slots |
| --- | --- |
| 38, 46 (`0x2551C`) | 11 = S0 + S1 - S2; 12 = S0 + S1 - S4; 13 = S0 + S1 - S3; 14 = S0 - S1 + S6; 15 = S2 + S6 - S1; 16 = S0 - S2 + S6; 17 = S3 - S4 + S9; 18 = S0 + S1 - 2 S4 + S9; 19 = S0 + S1 - S3 - S4 + S9; 20 = 3 S5 - 2 S7 |
| 45, 47 (`0x2560C`) | 5 = M; 6 = 2 M - S3; 7 = M + q(S0 - M); 8 = S1 + S3 + S4 - 2 M; 9 = S2 + S3 + S4 - 2 M; 10 = 2 S3 + S4 - 2 M; 11 = 3 (b+7) - 2 S0, with b+7 the new slot 7 |
| 56 (`0x25CDC`) | 15 = 2 S4 - S7; 16 = 2 S4 - S8; 17 = 2 S4 - S9; 18 = 2 S4 - S10; 19 = 2 S3 - S11; 20 = 2 S3 - S12; 21 = 2 S3 - S13; 22 = 2 S3 - S14 |
| 63 (`0x256B8`) | 16 = S0 - S1 + S2; 17 = S3 - S4 + S5; 18 = S7 - S8 + S9; 19 = S10 - S11 + S12; 20 = S13 - S14 + S15 |
| 64 (`0x25DD4`) | 23 = S1 + S2 - S0; 24 = M; 25 = S0 - h(M - S0); 26 = S0 - h(M - S0) - h(M - S1); 27 = S1 - h(M - S1); 28 = S1 - h(M - S0) - h(M - S1) |
| 65 (`0x25E7C`) | 23 = S1 + S2 - S0; 24 = S2 + S5 - S0 |

**Ops 34, 35, 36, 52, 53 (V: `synth2_check.py`, 500 arrays each).** Op 34 (operands v, s): b+4 = V0 - V1 + V2, b+5 = V0 - V1 + V3, b+6 = V3 + V2 - V1, b+7 = V0 - 2 V1 + V2 + V3; **d = (V2 - V3) shifted left by s (arithmetic right by -s when s is negative), b+8 = V3 - d, b+9 = V3 - d - V1 + V2**. Op 35 (operand v): b+4 to b+7 as op 34. Op 36 (v, s): b+6 = V0 - V1 + V2, b+7 = V0 - V1 + V3, b+8 = V0 - V1 + V4, b+9 = V0 - V1 + V5, b+10 = V2 + V5 - V1, b+11 = V0 - 2 V1 + V2 + V5;

**Ops 52 and 53: the skip rules are complete, and no shipped use changes `+0x14` (C: `draw_rules_check.py census`, the extractor's decode of the shipped models; the skip amounts V, `synth_note_check.py skip_check`, 300 runs each over every value of the `+0x7C` byte).** Rules: **op 52 skips 14 x ((byte `+0x7C` & 0x7F) >> 4) bytes** of the stream after its three operand words, the byte being that of the record of the item being drawn (the record at `V:4AA6E` of `V:4B23C`), that is 14 bytes (one stream item) per unit of the gear progress n = 0 to 7; **op 53 skips the signed word of its third operand** (sign-extended; negative moves back) after its three operand words and reads no state. Only the entity models 3, 4, 6 and 7 use them (4 op 52 items, 24 op 53 items), all in one shape: the op 52 item, six op 53 items (each 14 bytes, with the skips 70, 56, 42, 28, 14 and 0 and source vertices three slots further per step k = 0 to 5), then the polygon-list item op 55 and the line list op 13. Progress n therefore runs exactly the n-th op 53 item (n = 0 to 5: its own skip then jumps to the op 55 item), n = 6 (gear up) does not reach any of them: **the gear block is skipped whole**. The program of these models is (stream order) op 54, an op 0 item whose op word is 0xC000, the gated item of op 52, the six op 53 items, op 55, op 13, and then the next program of the draw item's list (ops 76, 77, 13 and the rest of the body). Op 54 (`0x250A0`) reads the same state n (the byte `+0x7C` with bit 7 cleared, shifted right by 4) and advances the stream by two bytes, **over the 0xC000 word**, when n < 6; when n >= 6 it does not, and the interpreter executes that word: bit 15 makes it an operation item, the low bits are operation 0 (`0x23540`: nothing, returns 0 with Z set, `Draw operations` row 0), and **bit 14 is the \"last operation of the program\" flag**, which makes the interpreter end the program after the operation returns, as at the end of every program (not an abandon: the return word is 0 and the condition codes are clear of the negative flag). The interpreter then goes on with the **next program of the list**, which begins after the line list op 13 of this block, not at the op 52 item: so at n = 6 and 7 the op 52 item, the op 53 items, the op 55 gear polygons and the op 13 gear lines do not run, and the fifteen slots A + 30 .. A + 44 (the ones the op 53 items write) are never read, so there are no \"previous contents\" to reproduce. (V: `gear_block_check.py`, the F/A-18 model 4 drawn through the queue C walker in an external view, the six of 80 camera states in which the gear block is reached, each in the eight states n = 0 to 7: the handler sequence is 54, 52, 53, 55, 13 and then 76, 77, 13 and the rest for n = 0 to 5, and 54, 0, 76, 77, 13 and the rest for n = 6 and 7 (the stream addresses logged before each call: 0x39A2A, 0x39A34, 0x39A88, 0x39A90, 0x39A9E, 0x39ADE for n = 5; 0x39A2A, 0x39A2C, 0x39ADE for n = 6: the op 0 item is the word at 0x39A2A, the next program starts at 0x39ADC, in both states the same ops follow from there); in the three states with a visible gear the n = 6 draw has 9, 9 and 7 primitives fewer (the gear polygons and lines in the window; for example 35 against 26 filler, line and disc calls; in the other three states nothing of the gear is in the window); and the polygons, lines, sprites and discs of all 192 draws (48 plain and 144 with the slot array filled with random words before the walk, three fillings per state) are **identical to the unfilled draw**.) Every skipped item is a synthesis op that returns 0, so in the states 0 to 5 the items that draw (op 55, op 13) run in every state: **the skips change which gear step is synthesised, not the OR of the returns; `+0x14` is the same in the states 0 to 5, and in 6 and 7 the gear items are absent.** A port that does not apply the skips runs the six op 53 items in order, so the last step (b = 82) always overwrites the slots; the obligations are to apply the skip of op 52 by the progress for n < 6, so that the n-th step is the one synthesised, **and, for n = 6 and 7, to execute nothing of the block (no synthesis, no gear polygon, no gear line) and continue with the next program of the draw item's list**; baking the state-zero slots and drawing the gear block at n = 6 would draw a gear that the original does not.
d = (V3 - V4) shifted by s, b+12 = V4 - d, b+13 = V4 - d - V2 + V3. The difference is formed as (earlier slot minus later slot) and **subtracted**: for a negative s the arithmetic shift of the negated difference rounds differently from a shift of V3 - V2 added (the shipped op 34 operands are -1 (six uses), 0, 1, 2; op 36 uses 2, op 41 uses 1). Op 52 (operands a, b, t: byte offsets; A = a/6, B = b/6, T = t/6 are slot numbers and Sk is slot k): with d = S[B] - S[A]: slot T+3 = S[T] + d, slot T+4 = S[T+1] + h(d), slot T+5 = S[T+2] + h(d); then the stream advances by 14 bytes times the state number
((record byte `+0x7C` with bit 7 cleared) shifted right arithmetically by 4, 0 to 7), after its three operand words. Op 53 (operands a, b and a skip; A = a/6, B = b/6): d_j = S[B+j] - S[A] for j = 0, 1, 2, and slot A + 30 + 5 j + (k - 1) = S[A+k] + d_j for k = 1 to 5 (fifteen slots); then the stream advances by the third operand as a signed word of bytes, after the three operand words (the state byte is not read; a negative skip moves back; V: `synth_note_check.py` `skip_check`, 300 runs per op with every value 0 to 255 of the `+0x7C` byte and random skip operands, 39 resp. 70 of them with no skip at seed 1, 0 differences).
**Completeness for the extractor (V: `synth_note_check.py` transcribes the tables of this section for ops 41-44 and 59, and **reads the operand-free rows above (ops 38, 46, 45, 47, 56, 63, 64, 65) from this note's own text** and evaluates them (M = h(S1 + S2), h and q the shifts, "3 (b+7)" the product with the new slot 7), then runs the original handlers on 200 random arrays each: every word of the array equals the expectation, so the tables name every slot the ops write and nothing else changes).** The extractor's older linear tables must follow this section: all slots in 16-bit word arithmetic, each halving applied where stated (the older op 43 b+13 = h(V7 + V9), the slots b+11/b+12 of op 43, b+6 of op 44,
b+22 and b+100 of op 42 and b+26, b+27, b+110 of op 59 are the ones that differ materially; the others agree with the older formulas up to the word wrap).

## Constants

| Constant | Value | Where it came from |
| --- | --- | --- |
| Angle units per circle (object angles) | 28800 (0x7080) | compares in 0x32770, wrap in 0x3202C |
| Angle units per circle (after >> 3) | 3600 | lookup in 0x32CCE |
| Sine amplitude | 16384 (Q14) | table 0x436E8 |
| Matrix element scale | Q14 (object, record +0x80/+0x92); Q8 (camera 0x4AC90; 0x4AC7E is the Q8 copy of the object matrix) | 0x32B52, 0x32A66 |
| Horizontal focal constant | 160 | multiply at 0x337B2; the polygon path uses the same at 0x291F8 |
| Vertical focal constant | 90 | multiply at 0x337C4 |
| Viewport | 320 x 180 (columns 0..319, rows 0..179) | clamps at 0x337BE, 0x337D0 |
| Centre of projection | sx' = 160, sy' = 90 before mirroring; x = 0 gives screen x 159 | 0x337B8, 0x337CA |
| Screen mirror | x: 319 - sx'; y: 180 - sy' (point sprites: ops 1, 49, 58, 62), 179 - sy' (op 48, the clipped lines, the polygons) | 0x337D6, 0x29220 |
| Clip planes (polygon clipper) | y, -y, x, -x against z, in that order (the clipped line routine has its own cascade, "The clipped line routine") | 0x28B6A stages |
| Intersection rounding | half-divisor test with away-from-zero fix-up, quirk for zero quotient | 0x28C00-0x28C1E |
| Object frustum slack | 1.5 * depth | 0x222DA |
| Level-of-detail shifts | 0..15 | 0x20E4A |
| Distance table | 257 words, 16384 .. 23170 | 0x208E8 |
| Group size limit | 22 entries | 0x2126C |
| Distance cap | 0x7FFF; middle component limit 0x7FFF0 | 0x2086C |
| Height gates of the line and quad ops | ops 74 and 75: `V:4AB30` below -0xC0 (camera above 240 ft) draws; ops 9 and 10: `V:4AB30` at or above -0x80 (at most 160 ft) draws; op 31: no test at or below -0x140 | 0x236C4, 0x249B0, 0x24BB2, 0x24C22, 0x23F56 |
| Op 4 gate heights (V:4AB1E, 1 ft = 204.8 pu) | -0x2000 (40 ft), -0x10000 (320 ft), -0xA0000 (3,200 ft), -0x100000 (5,120 ft, returns -1) | 0x238C6-0x23A28 |
| Point sprite thresholds | 0x30 and 0x50 shifted right by the LOD against the scaled distance | 0x337F8, 0x33804 |
| Disc radius limit | 127 | 0x257E8-0x257F4 |
| Fixed pens | op 17/18: 12; ops 20, 21: 13; op 26: 7; 0x21C34 models 67, 87 (op 13): 8; op 4: 0 | 0x24778, 0x24172, 0x23036 |
| Level search of the type B drawer | unmasked threshold shifted right by the LOD at least the scaled distance | 0x22D56-0x22D60 |
| Fill state defaults | mask 0xF, pattern 0xFFFF, toggle 0 (type B entry: mask and pattern only) | 0x225F0-0x225FA, 0x22D1A |
| Hatch geometry | offsets h(acc_k), acc += Q per stroke (word), half step h(x) = x >> 1 as a word; op 20, 21 pen 13; op 23 width P + h(3 Q) | 0x24024-0x24330, 0x24342-0x24486, 0x24488-0x2455E, 0x247FA-0x248EE |
| Hatch operands shipped (extractor view: slot, counts) | op 19: (0,2), (0,3), (0,4), (5,3), (38,4), (45,4); op 20: (0,2), (0,3), (0,4), (5,3); op 21: (0; 4, 4), (9; 2, 2, 1, 1), (9; 3, 3, 1, 1); op 22: (1; 7), (1; 2, 2, 5), (5; 1, 1, 1, 1), (10; 2, 2, 1, 1), (10; 3, 3, 1, 1), (44; 4), (54; 4); op 23: (0, 2); op 32: (38, 4), (45, 4); op 57: (0; 7), (0; 2, 2, 5) | extractor output over all decoded models |
| Op 6 fixed point | Q4 (shift 4), step divided by N, rounding +8 before the shift | 0x23E04-0x23EF6 |
| Clipped line routine | margins z -/+ x, z -/+ y; projection 160 x/z + 160, 90 y/z + 90; screen (319 - sx', 179 - sy'); error code 0x16 in V:4AA56 for a non-positive depth | 0x33936-0x33C9E |
| Eye offsets of the cockpit camera (body axes, 256 pu each) | class 0x30: (0, 1, -5); 0x11: (0, 5, 20); other: (0, 4, 18) | 0x1F474-0x1F496 |
| Side test variables | axis: V:4AB2A, V:4AB2E (words), V:4AB30 (long), V:4ABE2, V:4ABE6, block byte +6 low nibble; point and normal: V:4AB1A, V:4AB1E, V:4AB22 shifted by the LOD and 8 | 0x22B3A, 0x22A94 |

## Open questions

- Projection window height: the projection uses centre row 90 and rows 0..179 (a 180-row space),
  while `01a-reference-captures.md` measures a 320x167 world area in external view and 320x144
  visible above the cockpit panel, and `04-scene-composition.md` gives the 3D window rows 1 to
  `V:4AA3C` with 144, 167 or 179 as the last row by view kind. Unknown: whether the filler and line
  primitives crop at 0x4AA3C only (so the projected centre stays at 90 for every view) or whether
  rows are offset; the point path rejects rows above 0x4AA3C, the polygon path clamps only to 179.
  Evidence that settles it: log 0x4AA3C per view kind in the reference emulator and compare the row
  of a projected landmark (for example a runway threshold at known camera-space coordinates) with a
  capture.
- Camera composition: **settled** (round M3-T4): the head views compose the head triple with the aircraft attitude through 0x325B8, the external views aim at the viewed record through 0x1454A; see "Camera attitude in the head and external views" and "External camera aim" above. Open only for the approach camera modes `V:4A966` 1 to 4 and 6 (0x2D8E8 onwards).
- Which model-space axis is up and which angle is heading, pitch and roll. The matrix
  algebra is exact; the geometric labels need a flight capture (angle fields in record
  +0x66, +0x68, +0x6A versus aircraft motion). Measured: +0x66 pitch (nose up below the circle), +0x68 heading (clockwise), +0x6A roll (right negative), y up, see `01b-flight-captures.md`.
- Units of the 32-bit positions (the number of fractional bits relative to model units)
  and the exact composition of the three-word object-origin offset in step 3 of the camera
  transform (0x21F70-0x21F9A). Position longs: 1 ft = 204.8 units, camera offsets are 256 units, see `01b-flight-captures.md`.
- The meaning of the stored key at queue entry +0xE and of the thresholds at 0x4621C.
- Details of the tree node encodings (leaf end markers, the "stop" bit counters). The three side tests (axis, point-and-normal, three-vertex) are verified word for word; a node with bit 12 set outside an axis node is unexercised (no shipped node).
- The filler: the pen and pattern semantics of 0x4AA0C and 0x4A79E to 0x4A7A4 are described above from
  the code and the rig's output logs (R); the blitter's pixels were not compared with an emulator frame of a
  flat block, a type B patch or the near body (the filler's small shapes and the alternate blit are compared on emulator bitmaps in the near-ground pass, `04` "Stage [90]"). The use of 0x4AA0E (dither word) is still undecided.
- The line routine 0x3455E: its pixels are verified against the rig's blitter model (`line_raster_check.py`) and the row rule against emulator frames (`06`); the clipped line before it (0x33936, the plane cascade, the four intersection routines and the projection) is specified and verified (`The clipped line routine`).
- The polygon buffer residue ("Op 9 reads past its quad"): the mechanism, op 10's own test and the per-operation writers (`polybuf_writers_check.py`) are verified; the sea mesh as writer is V (`mesh_check.py`), the general handler `0x2377C` as writer is C. The camera-word negations of ops 39 and 40 (`0x23B36`, `0x23B38`) were not run at the word edge.
- The arcsine tables 0x43098 and 0x434E8 (sizes and index scaling) used by `fx_compose`.
- Vertex coordinate overflow behaviour (a quotient too large for the 16-bit divide) is characterised for the clipped line (the value used is the low word of the 32-bit product) and for op 6; it was not characterised for the polygon clipper; the fuzz inputs stayed within +-300 (times 4) for polygons.
- The pixels of the near body's filler paths (the one-step line, the clipped fill rows) are R (the rig has no blitter; the register writes are checked). The two emulator views with 167 rows and band 3 on the deck (codes 12 and 13) show no mark (code 13 is all pen 0); a frame that settles the pixels needs a state in which the near body projects into a 167-row window, which no observed view gives (a staged capture, poking the window words, was not made). Whether the op 4 gate lets the faces through in those two views was not read.
- The den = 0 hang of the clipped line ("The zero difference"): whether the game's camera-space words ever reach the magnitudes it needs (+-12,000 or more) is open; the port rejects and counts.

## Verified by

Driver and reference code live outside the repository (`extracted/scratch/`, git-ignored):
a C driver on `tools/m68k-rig/librig.a` loads `extracted/interceptor.bin` at 0x10000,
sets registers and BSS words, and runs each routine with `rig_run`. For the inline
snippets (vertex rotation, camera transform) the driver writes a return instruction into
the rig's RAM at the end of the snippet and starts at its beginning; for `fx_clip` the same
is done over the first byte of the filler 0x34A28 and the output list at 0x5043C is read
back. Re-run on 2026-10-13 against the relocation-fixed image (commit f1ffbb6, `interceptor.bin` sha256
d1053776...2d38): all examples and all fuzz counts below were reproduced unchanged; no value, table
address or pointer-based claim changed (the tables and routines were reached through absolute
references whose targets were the same). A Python re-implementation was written only from the formulas in this note and
compared with the rig:

- 300 random polygons through `fx_clip`, 500 random points through `fx_project`, 200
  random angle triples through each of `fx_matrix_a`, `fx_matrix_b`, `fx_cam_ypr` and
  `fx_cam_yp`, 400 random triples through `fx_dist`, 300 random inputs through
  `fx_distkey` and 200 random key sets through `fx_sort`: all identical bit for bit.
- The worked examples below, including `sincos`, the roll matrices, `fx_rowscale`, vertex
  rotation, the camera transform and the three-vertex visibility test.
- The sine table agrees with the stated formula at all 901 entries, the hypot table at
  all 257 entries.
- Claims marked "unverified", "read only" or "interface only" above were taken from reading
  the code and have no run behind them.
- Draw operations (note 03 sections above; scripts in `tools/m68k-rig/tests/lists_model`, run as `python3 walk_check.py primitives 1 1000`, or each alone as `python3 SCRIPT.py SEED N`):
  `prim_ops.py` 67,000 direct handler calls over 67 operations (return word and condition codes against the rule of the table, random camera-space
  vertex arrays with vertices behind, outside, inside and on the pyramid edge; 70,000 checks, ops 21, 57 and 67 have two per call), 0 differences; `ops_tri.py` ops 7 and 8, 1,600 faces; `ops_pen.py`
  ops 39 and 40, 1,200 calls; `ops_points.py` ops 1, 49, 58, 62, 1,600 calls (S up to 0xFFFF for op 62); `ops_synth.py` ops 41-44 and 59, 200 slot arrays each (the formulas are the symbolic
  evaluation of the handler code in `symx.py`, equal to the rig on every slot); `sprite_check.py` 1,500 draws per sprite and `disc_check.py` 200
  discs (15,326 rows) pixel for pixel; `op4_check.py` 1,500 gate cases on both sides of every boundary, the camera height and the record altitude independent, 487 with non-descending face words;
  `hand_check.py` the handedness, 772 points; `sec2_check.py` the second vertex section, 300 constructed blocks; `line_check.py` the line rule, 6,000 and 30,000 segments (boundary classes in "The clipped line routine"; the exact rule is `line_model.py`);
  `modeldata_check.py` no write to the model data (150 frames, 126 constructed walks); `typeb_loop_check.py` the bound of the type B search (150 frames); `nearbody_check.py` the near body polygons of the deck state;
  `census_ffff.py` the 0xFFFF stream ends (`04a` section 8c). All 0 differences.
- The hatch, line, fill-state, side-test, camera and synthesis rules of this round (scripts in `tools/m68k-rig/tests/lists_model`, seed 1): `hatch_check.py` 1,000 calls per op over ops 19-23, 32, 57, 6, 11, 12, 14 (18,000 checks: exact vertex lists in order, shift and count words, pen and fill words by sentinel, the set of counting calls by a call mask; about a quarter of the calls with a count of 0 or less, and op 6 with N of 1, 0 and negative); `line_model.py` 100,000 segments (55,028 drawn),
  the model of the clipped line equal to the rig in the return, the four screen words and the error word (210 rejections by a non-positive depth), and 18,164 den = 0 segments on which the rig hangs; `stipple_check.py` 1,000 sequences (5,467 handler calls, 6,638 output calls, op 49 included); `typeb_toggle_check.py` 987 walks and 36 pairs (toggle carried into type B objects); `nearbody_rows_check.py` 28 checks (rows 144 to 190); the capture `nearbody-167.txt`; `bsp_check.py` 5,000 sets per test (axis, point and normal, three-vertex, and the frame-word writer); `roll_check.py` 500 attitudes; `eye_check.py` 300 records; `synth2_check.py` ops 38, 45, 46, 47, 56, 63, 64, 65 and ops 34, 35, 36, 52, 53, 500 arrays each;
  `synth_note_check.py` ops 41-44, 59 as transcribed in the tables and ops 38, 46, 45, 47, 56, 63-65 read from the note's text, 200 arrays each, and the stream advance of ops 52 and 53 (300 runs each); `prim_ops.py` op 31 with operands 0, 4, 12, 20, 36 (600 calls); `nearbody_check.py` the deck and airborne states in bands 0 to 3, with and without the pass after an external view; the filler's small-shape paths and its clipping at the last row by the rig's register writes (R, `nearbody_rows_check.py`). All 0 differences.
- The rules of the round after the port's draw differential (`draw_rules_check.py`, seed 1, N = 400 per section, 19,406 checks, 0 differences; section names as the script's): `rows` the screen row of the entries (ops 1, 48, 2, 3: 1,600 calls), `pen` when each of 36 operations writes `V:4AA0C` (14,400 calls), `op26` the N + 1 slot early-out and the fill words (400 calls), `op27` ops 27 and 30 with four depths (800 calls), `op6` the sweep of N (13 values) with the rungs, the buffer extent and the trap, `neg` word negation (400 segments, 64 of which a 32-bit negation changes), `census` the use of ops 52 and 53 (4 models); and the port harness's 280,000 sequences classified (16 differences: 11 den = 0 hangs, 5 word negations).
- The view cameras (round M3-T4, `view_check.py`, seeds 1, 3, 7: 600 head records, 600 ring states, 3,000 aim states, 6,000 target vectors, 960 caller states, 120 key presses, 0 differences): the head composition equals 0x325B8 fed with the head triple for every class and code; the external aim step, the target model and the limit selection of 0x32074; the ring position of note 06; and the emulator airborne head states (7 of 7). `tools/hud-emu/verify_views.py` checks the external camera longs on 13 external and 3 map snapshots. Note-fix round (`view_probe.py`, seed 1, n = 600, 0 differences): `point` the aim target point of 0x32092 (classes, camera modes and the shift 4 against 6, 600 states), `atan` the index range 0 to 511 and the example (-1000, 0, 1000) (100,000 vectors, no divide overflow), `sqrt` every argument 0 to 8,400,000 (no overflow), `snap` the `V:4A85E` store on the snap paths, `extlongs` 0x1F45E in an external view (200 states); emulator `views-m3-levels.txt` (snap targets and the per-pass sequences of the distance levels), with `verify_views.py` now also checking the first-frame target point of the ext-L-1 snapshot.
- The round of the near-ground pass (scripts in `tools/m68k-rig/tests/lists_model` unless named, inputs `extracted/` and `librig.so` or `build/sky-ground/librigblit.so`): `prim_ops.py SEED 4000 edge` (seeds 1 to 4) the slot-1 test of ops 39 and 40 with -32,768 coordinates, 16,000 calls per op, about 5,500 per op on which the 32-bit rule would differ, 0 differences; `prim_ops.py 1 1500 op9` the residue test of op 9 and the own-depth test of op 10, 1,500 calls each, 0 differences, `polybuf_writers_check.py` the writer positions, and the rig buffer after op 26 (counts 3 to 7: count + 1 vertices; ops 24 and 25: count; ops 7 to 10, 15, 16: 3 or 4); `gear_block_check.py` the gear block with op 54 (model 4 in an external view through the queue C walker, six camera states, eight progress states, three random fillings of the slot array each: 192 draws, 0 differences; the handler sequences and the primitive counts are given in the paragraph on ops 52 and 53); `line_raster_check.py` 2,000 random lines against the rig's blitter model, 0 differences; `nearground_pixels_check.py` and `nearground_frames_check.py` for the filler's cases and the alternate blit (`04`).
- The block set-up: `prim_walk.py vertices`, 945 drawn blocks, 9,261 vertex slots, 0 differences (flat 643, solid 302, second section 109, strips 175).

### Worked examples

Angles are in 28800-unit form for matrix builders, 0.1 degree for `sincos`.

`sincos2` (inputs A, B, outputs sin A, cos A, sin B, cos B):

| A | B | Result |
| --- | --- | --- |
| 0 | 900 | 0, 16384, 16384, 0 |
| 300 | 2700 | 8192, 14189, -16384, 0 |
| 1234 | 3599 | 13678, -9019, -29, 16384 |

`sincos1` (D4 to sin, cos): 450 gives 11585, 11585; 1800 gives 0, -16384; 3000 gives
-14189, 8192.

`fx_matrix_a` / `fx_matrix_b` (nine words, row major, from angles A, B, C):

| A, B, C | `fx_matrix_a` | `fx_matrix_b` |
| --- | --- | --- |
| 8000, 4000, 12000 | -2941 1423 -16055 15969 2463 -2706 2180 -16135 -1829 | -15300 5440 -2180 -1423 2463 16135 5685 15257 -1829 |
| 14400, 2400, 0 | 14189 0 8192 0 -16384 0 8192 0 -14189 | 14189 0 -8192 0 -16384 0 -8192 0 -14189 |
| 28000, 100, 7000 | 654 -16119 2857 16366 704 218 -337 2845 16131 | 774 -16361 337 16119 704 -2845 2828 467 16131 |

`fx_cam_yp` (A, B): (8000, 4000) gives 164 0 196 -193 -45 162 35 -253 -29; (14400, 2400)
gives 221 0 128 0 -256 0 128 0 -222; (28000, 100) gives 255 0 5 1 252 -45 -5 44 252.

`fx_cam_ypr` (A, B, C): (8000, 4000, 12000) gives -46 23 -251 249 38 -43 35 -252 -29;
(14400, 2400, 0) gives 221 0 128 0 -256 0 128 0 -222; (28000, 100, 7000) gives
10 -251 44 255 11 3 -5 45 252.

`fx_roll_q8` / `fx_roll_q14`: angle 12000 gives -222 0 128 0 256 0 -128 0 -222 and
-14189 0 8192 0 16384 0 -8192 0 -14189; angle 5000 gives 118 0 227 0 256 0 -227 0 118 and
7565 0 14533 0 16384 0 -14533 0 7565.

`fx_rowscale` on the matrix 100 -150 300 50 500 250 700 450 900: scales (256, 256, 256)
leave it unchanged; (128, 256, 512) gives 50 -75 150 50 500 250 1400 900 1800;
(300, -200, 256) gives 117 -176 351 -40 -391 -196 700 450 900.

Vertex rotation with the matrix 181 -181 0 181 181 0 0 0 256 (a 45 degree turn in Q8):
(100, 200, 300) gives (-71, 212, 300); (-1000, 500, 2000) gives (-1061, -354, 2000);
(32, -17, 9) gives (34, 10, 9).

Camera transform with the matrix 200 0 -100 0 256 0 100 0 200 (the vertex is the
already-shifted, offset-added vector): (10, 20, 300) gives (-110, 20, 238);
(-500, 40, 900) gives (-743, 40, 507); (7, -3, 1) gives (5, -3, 3).

`fx_project` (x, y, z to screen x, y; "reject" means long -1 at 0x4AA10 and D0 = 0, with
the row limit word 0x4AA3C set to 0x7FFF):

| x, y, z | Result |
| --- | --- |
| 0, 0, 100 | 159, 90 |
| 50, -30, 100 | 79, 117 |
| -99, 89, 100 | 317, 10 |
| 1000, 500, 3000 | 106, 75 |
| -7, 3, 13 | 245, 70 |
| 20000, -9000, 30000 | 53, 117 |
| 100, 0, 100 | reject |
| 5, 5, 0 | reject |

`fx_clip` (shift, vertices, then the screen polygon; every case returns D0 = 1):

| Case | Input | Output |
| --- | --- | --- |
| 1 | S=0: (-50,-20,100) (60,-30,120) (0,70,90) | (239,107) (79,111) (159,19) |
| 2 | S=0: (-50,0,100) (150,0,100) (0,50,100) | (239,89) (0,89) (0,74) (159,44) |
| 3 | S=0: (-200,-30,100) (200,-30,100) (200,60,100) (-200,60,100) | (319,116) (0,116) (0,35) (319,35) |
| 4 | S=2: (-10,-10,20) (10,-10,20) (10,10,20) (-10,10,20) | (239,134) (79,134) (79,44) (239,44) |
| 5 | S=0: (-10,-10,-50) (10,-10,100) (0,20,100) | (191,179) (143,98) (159,71) (319,77) (319,179) |
| 6 | S=0: (20,-120,100) (20,120,100) (-20,120,100) (-20,-120,100) | (127,179) (127,0) (191,0) (191,179) |

Three-vertex visibility (vertices at 0x51040, flags word in D7 and A3): flags 0 with
(0,0,100) (50,0,100) (0,50,100) gives D7 = 1; flags 0 with (0,0,100) (0,50,100) (50,0,100)
gives 0; flags 0x100 with (-30,10,200) (40,10,150) (0,-60,180) gives 0; flags 0x80 with
(10,10,100) (20,10,100) (10,20,100) gives 1.

`fx_dist` (D2, D3, D4 to D1): (3,4,0) gives 5; (100,100,100) gives 172; (1000,0,0) gives
1000; (300,400,1200) gives 1298; (1,2,3) gives 3; (0,0,0) gives 0.

`fx_distkey` with 0x4AB2A = -20, 0x4AB30 = -3000 (long), 0x4AB2E = 40, 0x4AB70 = 2,
0x4A973 = 0: (100,200,300) gives 38; (-4000,6000,100) gives 412; (0,0,0) gives 46;
(50,-70,90) gives 51.

`fx_sort` (0x538C8 receives indices; keys listed in index order, 0x53870 preloaded with
0..21): keys 5 40 12 40 7 give 1 3 2 4 0; keys 300 200 100 400 50 10 give 3 0 1 2 4 5; keys
7 7 7 7 give 0 1 2 3. A key of -1 is skipped.

`fx_compose` with A4 pointing at an identity matrix (16384 diagonal), angles in, angles
out (D4, D5, D6): (1000, 2000, 3000) gives (992, 2000, 3000); (8000, 4000, 12000) gives
(6392, 18440, 26424); (0, 0, 7200) gives (0, 0, 7200) (partly verified: the geometry of
the returned triple is not characterised).
