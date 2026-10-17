# extract

Turns your own *F/A-18 Interceptor* disk image into the `assets/` directory
the port reads. The output is copyrighted data: it is git-ignored and must not
be committed or shared.

    # flux image -> ADF (only if you have an SCP/HFE capture)
    gw convert --format amiga.amigados "disks/scp/FA-18 Interceptor.scp" extracted/interceptor.adf
    # ADF -> assets/
    pip install -e tools/extract
    python -m extract extracted/interceptor.adf assets

Output: `pix/*.png` with `*.palette.json`, `sounds/*.wav` and `*.raw` (the 14 samples of the disk's text/ directory; its other two files, `textply` and `textctl`, are the demo stream, see `tables/demo.bin`),
`manifest.json` with hashes. The WAV sample rate of each file is 3,546,895 / period, the reference period of note 10 section 2.4
(manifest `sample_rate_hz`, a map from file name to Hz; a fixed rule of note 10 2.4, so no per-file note: textcpt 764, textegn and textegn2 776, textwnd 392, texttre 310, textger 700, text201 179, any other file 358; extractor version 25); the `.raw` files are byte-exact. The PNG files hold their pixels in stored (uncompressed) deflate
blocks, written by hand (`extract/png.py`), so that the JavaScript port can write the same bytes without a deflate
implementation.

## The JavaScript port (`tools/extract-js`)

The browser build runs the extractor in the page, from the player's own ADF (milestone 6, `tools/extract-js/README.md`).
`tools/extract-js/src/` has one module per module of `extract/` (`adf`, `hunk`, `ilbm`, `palette`, `png`, `sky`, `tables`,
`cockpit`, `flight`, `lists`, `terrain`, `models`, `combat`, `flow`, `sounds`, `manifest`) and `index.js`, whose
`extract(adfBytes)` returns the same files and the same manifest as `python -m extract`.

**Both ports change together, and share the version.** A change to a decoder, a table address, a message text (the models'
failure texts are written to `models/index.txt`), the file set, a file format or `EXTRACTOR_VERSION` is made in
`tools/extract/extract/` and `tools/extract-js/src/` in the same commit:

- `EXTRACTOR_VERSION` in `extract/manifest.py` and `src/manifest.js` (and `version` in `tools/extract-js/package.json`, `N.0.0`);
  a test compares the three;
- regenerate the JavaScript port's fixtures when the Python output changes: `python tools/extract/tests/write_synth.py
  tools/extract-js/test/fixtures/synth.adf tools/extract-js/test/fixtures/synth-expected.tar` (the committed disk is kept and
  the tar rewritten from it; `--rebuild` makes a new disk, needed only when `tests/synth.py` changes) and
  `python tools/extract/tests/write_corpus.py tools/extract-js/test/fixtures/corpus.json` (run it in a checkout without
  `extracted/` and `assets/`, e.g. a fresh worktree: it refuses otherwise, so that nothing of the game enters the file);
- `node --test "tools/extract-js/test/*.test.js"` compares the JavaScript output with the committed Python output, replays
  about 4,500 recorded Python decoder calls (the Python tests' own inputs and a mutation fuzz) and, when Python with amitools is
  available, checks that the committed expected output is still what the Python extractor writes;
- `pytest tests/frontend/test_extract_js.py` runs both extractors on the synthetic disk (CI) and on `extracted/interceptor.adf`
  (local only) and demands byte-identical output.

## World data (extractor version 5)

The executable `F-18 Interceptor` is read from the disk, relocated in memory to the
flat image at base `0x10000` (`extract/hunk.py`, the project's `tools/hunk/relocate.py`)
and decoded by the addresses of `docs/re/02-assets-and-tables.md`. Every decoder is a
pure function over bytes; only the CLI touches the file system. Malformed tables raise
`ValueError` (class `DecodeError`) naming the table and the address.

All multi-byte values in the files below are little endian.

| file | content |
|---|---|
| `palette/fade.pal` | 16 fade steps x 16 colours x `u8 r, g, b` (12-bit Amiga colour times 17); step = 15 minus the fade level |
| `palette/flight.pal` | step 0 of the fade table: the 16 in-flight colours |
| `fonts/hud.bin` | 64 glyphs (character code `0x20` to `0x5F`) x 5 row bytes; the three pixels are in bits 7 to 5, left to right; characters the font lacks repeat the shared glyph |
| `tables/lod.bin` | 240 bytes, level-of-detail shift against coarse distance (note 03), non-decreasing 0..15 |
| `tables/range.bin` | 64 bytes at `0x4621C`, verbatim: visibility range per view distance >> 7, value x 256 world units (notes 02 and 04) |
| `tables/camera_ring_{default,11,14,30}.bin` | 10 camera offsets x `i16 x, y, z` (note 04), classes default, `0x11`, `0x14`, `0x30` |
| `tables/lists_{quad,octperm,group,subperm,tileperm,tiles}.bin` | display-list builder tables (note 04a), verbatim: quadrant class 16 bytes `0x4625C`, octant permutations 32 `0x4626C`, pattern group 16 `0x4628C`, sub-cell permutations 64 `0x4629C`, tile permutations 96 `0x462DC`, tile offsets 42 bytes (21 signed byte pairs dx, dz) `0x20674` |
| `tables/lists_{steep,high,low}.bin` | the three tile-pattern blobs verbatim (610, 3042, 502 bytes at `0x4633A`, `0x4659C`, `0x4717E`): a table of 4, 32, 64 big-endian offset words, then the lists (entries tile, count, sub-cells; a tile byte with bit 7 set ends a list). Lists reachable from the table are validated |
| `tables/lists_limit.bin` | 8 `i16` words from `0x21022`: generation limit by altitude band D = 0..3, as (no pitch gate, pitch gate) pairs: 30 40 30 50 32 60 40 70 |
| `tables/lists_vis.bin` | 24 `i16` words from `0x23FF4`: the two 12-word tables of the drawer's visibility pre-test (note 04a section 8b rule 6), the first used when the camera offset V:4AB30 is above -0x80, the second otherwise |
| `tables/lists_ngclass.bin`, `lists_ngpts.bin`, `lists_ngshape.bin` | the near-ground pass, stage [90] (note 04): the 32 by 32 class bytes verbatim from `0x2C440` (1024); the three point tables `0x2C840..0x2CDEB` as `i16` little endian (726 words: A = count 96, class limit 2, 96 triples (x, z, k); B = 80, 3; C = 64, 4); the six triangle shapes `0x2CDEC..0x2CE33` as 36 `i16` (k = -12, -16, -20, unscaled then scaled; three (dx, dz) pairs each) |
| `tables/lists_desc.bin` | 134 records of 8 bytes, one per record of the object table at `0x25ECC` (item id i is record i + 16): `u8 class` (0 zero record, 1 `0x21C40`, 2 `0x21C34`, 3 `0x21D0C`, 4 `0x22CFC`, 5 `0x22D0A`, 6 `0x21C44`, 7 `0x26944`, 8 `0x2699E`, 9 `0x26A3E`, 255 other), `u8 flags` (bit 0 model base positive, bit 1 reference-qualified landmark, bit 2 footprint present), `u16 footprint offset`, `u16 reference offset` (both into `lists_land.bin`, `0xFFFF` none), `u16 0`. Landmarks and footprints are only decoded for classes 1 to 3 and item ids (records 16 and up) |
| `tables/lists_land.bin` | landmark data, `i16` little endian. Footprint: 5 words outer box (max height, x min, x max, z min, z max), `u8 node count`, per node 5 words box, `u8 n`, n planes of 6 words (reference point, normal), **or one plane when n is 0** (extractor version 26: the original forms the first dot product before it looks at the count, note 07 8.2 (4); the one plane is the six words that follow the count). Reference record: `u16 c` then c + 1 planes of 6 words (note 04a section 5b). A model whose block part-id word is `0xFFFF` has no footprint |
| `tables/sea_zone.bin`, `sea_zone2.bin` | the base-colour flag's zone tables (note 04, "The base colour flag"), raw big-endian: 560 bytes `0x23188`..`0x233B7` (V:4A953) and 80 bytes `0x233B8`..`0x23407` (V:4A954: row 17, column 16, one record; the note's "28 bytes" is the header and the first groups) |
| `tables/mesh_scan.bin`, `mesh_far.bin`, `mesh_cells.bin` | the sea mesh pass (note 04, stage [68]), raw big-endian: 2972 bytes `0x2E600`..`0x2F19B` (25 signed byte pairs dx, dz, the scan lists, the own-cell octant tables), 36 bytes `0x2F4F8`..`0x2F51B` (the far-test word table), 6224 bytes `0x47D7C`..`0x495CB` (the 8 x 8 coarse grid, its records, the 32 x 32 fine grid at `0x47F40`, its records; the list pointers inside are absolute image addresses, rebase by `0x47D7C`). Extractor version 11 |
| `tables/flight_*.bin` | the flight step's tables (note 05, "Extractor needs"): `flight_stick_player` (62 words), `flight_stick_ai` (64), `flight_gust` (31), `flight_sink` (31), `flight_span_pitch` (286 raw big-endian image bytes 0x3249A..0x325B8) and `flight_span_trig` (60,288 bytes 0x3AE92..0x49A12: the sine and inverse-sine tables and everything the junk reads and the composition's lookups touch; the 340 relocated longs (337 in this span) carry their reference-machine values with the loader's segment table (`extract/segments.txt`, the RAM bases; `--segments FILE` names another)), `flight_len` (257), `flight_terrain` (grid 1024 words then cell records 1862 words), `flight_dir` (32), `flight_pads` (5 entries x 8 words, then per pad a count and its quad list), all `i16` little endian; `flight_carrier` (852 bytes) and `flight_ship2` (438 bytes), raw big-endian pointer lists and structures. Formats in `extract/flight.py` |
| `cockpit/*.bin` | the cockpit tables of note 06, all verbatim (big endian, as in the image): `warnings.bin` (54 x 28 bytes), `masks.bin` (340: gear lamp, four gear symbols, ball mask, ball table 2, compass strip), `markers.bin` (112: 12 then 44 signed byte pairs), `arcs.bin` (82: the lock circle arc, 20 pairs and the terminator pair (-1,-1), then the range dot arc, 19 pairs and a terminator pair), `mapshapes.bin` (324: 17 offset words, the 16 map direction shapes and the ship shape, triples (dy, dx, length) ended by 0x80). `pix/{splsh,inst5,frnt5}.idx` are the raw picture pen indices |
| `palette/menu.pal`, `palette/flash.pal` | the menu and briefing palette (`0x29F24`) and the flash palette (`0x29F64`, all entries one orange value `0x0FB2`): 32 entries x `u8 r, g, b` (12-bit Amiga colour times 17, as the fade table). Extractor version 13 |
| `tables/scenario.bin` | note 05 17.14 and 17.17 rule 5, note 02 section 9. Eight sub-tables in this order, each prefixed by `u16 count, u16 record_size`, every field a little-endian word: (1) start blocks `0x47ADE`, 16 bytes, `i16 cell_x, cell_z, n, ox, oz, fx, fz, degrees` (a carrier-type block has `0x8000 \| record` in the first word and zeros; the walk ends at a block whose first word is -1, not stored); (2) spawn regions, reached through the four 32-bit absolute pointers at `0x2DE28`, 14 bytes, `u16 x0, x1, z0, z1, count_word, first_entry, entry_count` (the box order is a port reading until the note states it; `count_word` is the raw word before the entries, not the entry count, meaning open); (3) region entries, 10 bytes, five raw words (record byte offset = 20 x id, class, slot and flags, position-block reference, flags); the walk of a region ends at an entry whose first word has bit 15 set (not stored); (4) placement rows `0x2DEDA`, 12 bytes, three variants x `u16 list_index, u16 count_word` (rows 0 to 8 = game modes 1 to 9, row 9 = modes `0x7E` and `0x7F`; the list index is into table 5 instead of the stored offset); (5) lists, 8 bytes, four `u16` indices into table 6 (the four header offsets of a list; shared sub-lists stored once); (6) sub-lists, 4 bytes, `u16 first_entry, entry_count` into table 7; the walk of a sub-list ends at the first word `0xFFFF` (not stored); (7) sub-list entries, 10 bytes, five raw words (template offset into `0x25ECC`, class, record index in the low 7 bits with `0x8000` a flag, then two words kept raw); (8) the mode-0x7D data of `0x2CFDA`: empty (`count 0, record_size 0`), because the notes give only the routine's result for that mode, not a table in the image: open. The extractor refuses counts above the core's caps (`SCENARIO_MAX_*` in `src/core/assets.h`). Extractor version 13 |
| `tables/text.bin` | note 08 sections 2.2 to 2.4, the records of hunk 64 (`0x43F24`-`0x46159`) re-encoded line by line, little endian: `u16 line_count` (337), then per line `u16 first_id` (the lowest id naming the record), `u8 line` (index within the record), `u8 row` (scanline, below 200), `u8 col` (byte column, even; x = 8 x col), `u8 flags` (0, 1 or 3), `u8 colour` (high nibble the pen, low nibble the typed-line delay), `u8 delay` (0 to 120), `u16 length` (0 to 45), `length` text bytes (0x20 to 0x5F, no terminator). Records are stored in order of their first id; the records with no lines are not stored. The core's line step is 11 scanlines (12 in the main menu list). The extractor refuses, naming the record's id and address, a flags value outside {0, 1, 3}, a text byte outside 0x20-0x5F, a line over 45 characters or whose last lit pixel `8*col + 7*(n-1) + 4` is beyond 319, a typed line (flags bit 0) with delay below 2, more than 255 lines in a record, more than 400 lines or 8192 text bytes (with a NUL per line) in all (the core's caps), an odd column, a row of 200 or more, a delay above 120 and a record running outside the span. Extractor version 13 |
| `tables/text_ids.bin` | note 08 section 2.1: `u16 id_count` (110), then per id 1 to 110 `u16 id`, `u16 first_line` (index into `text.bin`), `u16 line_count` (0 for the empty records and for the four ids that name no record; `first_line` is then 0). Ids sharing a record carry the same `first_line` (91 distinct records, 3 of them empty). Extractor version 13 |
| `fonts/menu5x7.bin` | note 08 section 3.2, the front end's own font (hunk 61, offset table `0x4283C`): 64 glyphs x 7 bytes in character order (glyph i is the glyph of character `0x20 + i`, the offset table followed), rows top to bottom, pixels in bits 7 to 3 (bit 7 leftmost; bits 2 to 0 clear). Refused: a first offset other than 0x0080, an offset outside 0x0080-0x01F3 (naming the character), a glyph with a low bit set, a non-blank glyph at offset 0x80. No Kickstart ROM is involved. Extractor version 13 |
| `tables/demo.bin` | note 08 7.1, the demo replay stream, which is two disk files (`text/textply`, `text/textctl`), not executable data: `u32 frame_count` (3676), then `frame_count` key bytes, then per frame `u16 word1, u16 word2` (little endian; the file's words are big endian). Sizes other than 3676 and 14,704 bytes are refused naming the file. The whole stream is carried; the port plays frames 0 to 2045 only (up to the first run of three `0xFF` key bytes). These two files are not written to `sounds/`. Extractor version 13 |
| `tables/training.bin` | note 08 8.2 and 8.13, four sub-tables each prefixed `u16 count, u16 record_size`, little endian: A (8 rows of 8 bytes, image `0x2749A`: `i16 prompt_id, i16 name_id, i32 unused`); B (8 rows of 8, image `0x274E2`: `i32 camera, i32 script`; a script long with bit 31 clear is refused naming the row); pitch rates (11 x `i16`, record size 2, image `0x3132C`); roll rates (11 x `i16`, image `0x31342`). Extractor version 13 |
| `tables/templates.bin` | note 02 section 1, image `0x25ECC`: `u16 count` (134), `u16 record_size` (20), then the 134 records raw, 20 bytes each, five longs per record, kept in the image's big-endian byte order (not converted; each long is one field of note 02 section 1's table, carried raw; the pointer longs are image addresses (base `0x10000`), not reference-machine values, and `--segments` does not change them). The core reader `assets_load_templates` converts each long to a host u32. Refused: the table outside the image. Extractor version 14 |
| `tables/ai.bin` | note 07 sections 3.6, 4.5, 6.1, 6.3 and the Part A constants; little endian, fourteen sub-tables each prefixed `u16 count, u16 record_size`, all signed 16-bit words (the image's are big endian), in this order: the five rate tables `0x31316`, `0x3132C`, `0x31342`, `0x31358`, `0x3136E` (11 x 2 bytes; the pitch and roll tables are also in `training.bin`, which is unchanged); the evasion table `0x30284` (19 groups x 16 bytes: four `(counter, state)` pairs for progress 0 to 3); the offset table `0x2789E` (14 x 6 bytes, x y z: seat, parachute/pod, six hard points for class-0x10 owners, six for the others); the class-nibble-3 ring offsets `0x2D718` (10 x 6 bytes, x y z); the start blocks `0x47ADE` (5 x 16 bytes); the airfield camera points `0x47B30` (3 records of 6 words, 16 bytes apart in the image because the code shifts the index left by 4; packed to 3 x 12 bytes; extractor version 27 reads the 16-byte stride); the fine offsets `0x206F2` (16 x 4 bytes); and last the one word before the rate tables `0x31314` (1 x 2 bytes, 20085: read by the non-aircraft pitch case with a negative index); and the waypoint runs of state 8 (note 07 4.3: the table `0x2DCE8` of 60 word offsets, each to the first of consecutive 10-byte blocks that ends before the first block whose first word has bit 15 set; 60 records of 332 bytes: `i16` block count 0 to 33, then 33 blocks of five words, zero padded; entry 0 is the table itself); and last the hull lists of the aircraft hit test (note 07 8.2a: the pointer table `0x3DACC` is read and its three pointers must be `0x3DAFA`, `0x3DD28`, `0x3DD94`, else the part id is refused; 21 records of 6 bytes = 3 lists x 7 triples of words, no header word since the lists have equal length: six face triples `(a, b, c)` of byte offsets into the record's vertex array, slots 0 to 16 = offsets 0 to 96, bit 14 of `a` = form B, then a terminator triple whose first word has bit 15 set and whose other two words are written as 0; no model vertices; faces outside slots 0 to 16 or a missing terminator are refused naming the part id). A table outside the image is refused naming the table; a run of more than 33 blocks is refused; values are carried unchecked. Extractor version 15 (the offset table, the pre-table word and the runs) |
| `tables/burst.bin` | note 07 11.1 and 11.4, the burst of a dying record, little endian: the magic `BRST`, `u16` version 2, then 72 `i16` (the eight triangles of the pointer table `0x315A2`, shape k = the 18 bytes at `0x315C2 + 18 k`: three vertices of three words x y z), 3 x 36 `i8` (the fragment offsets `0x31652`, `0x31679`, `0x316A0`: twelve triples dx dy dz each) 27 `i8` (the nine disc offsets `0x316C7`), 5 x 12 `u8` (the pen tables `0x316E5`, `0x316F1`, `0x316FD`, `0x31709`, `0x31715` as the image has them: fragment j of the layer takes byte j) and 32 `u16` (the shape cycle `0x31722`: the byte offset of the shape fragment j draws at age a is word j + a); 409 bytes. The pointer table is checked against the note (shape k at `0x315C2 + 18 k`) and not exported; the pens must be pen numbers (0 to 15) and the cycle words multiples of 4 below 32, else the table is refused by name. Extractor version 16, the pens and the cycle from version 18 |
| `tables/target_text.bin` | note 06 "Target symbols" and note 07 12.8, the strings of the message line's target data (driver `0x36DB2`), little endian: `u16 label_count` (3), `u16 label_chars` (4), `u16 name_count` (8), `u16 name_chars` (6), then 3 x 4 label characters (`0x37197`, `0x3719C`, `0x371A1`: altitude, heading, speed) and 8 x 6 name characters (`0x371A6` and seven bytes on, in the image's order: the two hostile fighters, Air Force One, the F-16, the cruise missile, the undetected text, the two airliners), no terminators; 68 bytes. The blank line `0x3717C` (26 spaces) is checked and not exported; a string that is not its characters and a terminator is refused naming the address. The class rule that picks a name is note 07 12.8. Extractor version 16 |
| `tables/prompts.bin` | note 06 "Target symbols" and note 07 12.7, the two fire prompts of the routine `0x36930`, little endian: `u16 count` (2), `u16 chars` (6), then the six characters of the missile form (image `0x36923`) and of the gun form (`0x36929`, which starts with a blank), no terminators; 16 bytes. Each must be printable; the byte after the second string must be the terminator, else the table is refused naming the address. Extractor version 19 |
| `tables/star.bin` | note 07 7.7 and 11, the flare's star (the table at `0x31542`), little endian: `u16` segment count (3), then 18 `i16`: three strokes of two points of x, y, z, offsets from the slot's centre in position units divided by 256; 38 bytes. An offset beyond 64 is refused by name. Extractor version 22 |
| `tables/briefing_chain.bin` | note 08 2.1, the text-record ids of the briefing chain (image `0x43EE6`, big-endian words in rows of five at the byte offsets 0, 10, 22, 32, 42 and 52: a stray word sits between the second and third rows): 30 bytes, one byte per id, five for each of the mission modes 3 to 8 (briefing 0, briefing 1, the shared notice, the orders, the success outcome). An id outside 1 to 110 is refused naming the row and entry. Extractor version 23 |
| `tables/sounds.bin` | note 10 2.2 to 2.3 (extractor version 24, unchanged in 25), the 37 sound objects as the game's loaders build them from the lengths of the files of text/ (`extract/sound_table.py`, `src/sound_table.js`): 37 records of 20 bytes, big-endian, object n at 20 n: `u8 file` (index into `sounds/index.txt`, 0xFF none), `u8 kind` (0 sample, 1 tone, 2 noise, 0xFF absent: object 7, or its file is not on the disk), `u8 voice`, `u8 volume` (default), `u32 offset`, `u32 length` (the byte range played, replayed whole: note 10 4.2 has no separate repeat range), `u16 period` (the loader's default for the object, 358 unless the note says otherwise, e.g. 179 and 181 for text201; it is not the WAV's reference period, which is the file's first play in a flight and is only the listening copy's rate), `i8 count` (-1 for ever), `u8 reload`, `u8 next` (0xFF none), `u8 script` (0 none, 1 tone, 2 noise, 3 gun, 4 launch), `u16` reserved 0. A range is clamped to the file. |
| `tables/head_step.bin` | note 06 "Head views", the HUD step table (image `0x1E800`): 14 signed bytes, one per head view code (50 = hidden), kept as they stand. A step beyond 64 is refused. Extractor version 23 |
| `tables/months.bin` | note 08 10.9 and C9, the four month names of the date line (image `0x292CC`, 4 x 9 bytes, stored reversed): 36 bytes, each name un-reversed to read left to right as the line shows it, in calendar order, space padded on the right to nine characters. A non-printable byte is refused. Extractor version 16 |
| `terrain/terrain.bin` | cell tables A, B, C and their item lists (below) |
| `terrain/index.txt` | counts: cells per table, lists, items |
| `terrain/positions.txt` | one line per position block of the word table at `0x2DCE8`: `entry cell_x cell_z fine_x fine_z height` (fine and height in units of 256 position units; the position is `cell * 2^22 + fine * 256`) |

### `terrain/terrain.bin` ("ITRN" version 2)

    header   4s magic "ITRN", u16 version = 2, u16 table_count = 3, u32 list_count, u32 item_count
    tables   table_count x { u16 cell_shift, u16 grid, u32 first_cell, u32 cell_count }
             (A and B: shift 24, grid 32; C: shift 22, grid 128; cells are stored per table)
    defaults 16 x { i8 dx, i8 dz }       the default-offset table (`0x207C6`); no shipped item uses it
    cells    { u16 x, u16 z, u32 list_index }   x and z are the x and z cells; stored row by row, in z then x order (the image's rows are the z cell, the keys of a row the x cells, note 04a), for all tables back to back
    lists    list_count x { u32 first_item, u32 item_count }
    items    item_count x { u8 layer, u8 object_id, u16 dx, u16 dz }

A cell is the square `x, z` of its table (`cell_shift` position bits wide); `dx`, `dz`
are 0 to 4095 inside it. The list order of an item list is the draw order within its
layer. Lists shared by several cells are stored once. The reader is
`extract.terrain.read_terrain`.

## Models (`models/model_NNN.bin`, "IMDL" version 5)

Models are drawing programs, not polygon lists. The extractor decodes each detail level
into a neutral form that the core walks at run time (docs/re/02, section 13); nothing is
flattened, so painter order, the per-side face selection (BSP) and the distance gates
survive. Reader: `extract.models.read_model` (round-trips with `write_model`).

`models/index.txt` has one line per object id 0..133: `id<TAB>file<TAB>status`, `#` lines
are comments. Status is `ok`; `partial: <reason> (N items flagged blocked)` (the file is
written, but the strict decode stopped at a documented BLOCKING item; see below);
`empty: ...` (ids 16-32 are zero in the file, no file); or `failed: <reason>` (malformed
data, no file). A failure names the model id, the operation number and the flat address.

File layout, little endian, counts first, every table an array of fixed-size records:

    header   60 bytes: 4s "IMDL", u16 version = 5, u16 id, u16 kind (0 3D, 1 flat, 2 type B),
             u16 blocked (items flagged blocked), u32 draw handler address, then u32 counts:
             levels, vertices, elements, proglist, programs, items, operands, skips,
             then u32 nsec (byte 48) = words of the second-section pool, then the
             cockpit-alternate fields: u16 nnormal (byte 52), u16 alt_near (54), u16 alt_cockpit (56), u16 pad (58)
    levels   54 bytes each; bytes 48 to 53 are three s16, the data triple (the words after the block's b8 vertices, 0 for a flat block: the draw's vertex loop rotates them into slot b8 when b8 = b9, note 07 8.10 rule 5); byte 17 of every level record (a pad byte before extractor 20) is the model's hull flag byte: +7 of the block the gun's box test reads
             (the leaf named by the first word of the model's list when its bit 15 is set, else by the word at +4; note 07 8.2): bit 4 marks a big hull (ids 0, 8, 9, 10, 12)
    vertices 16 bytes each
    elements 32 bytes each
    proglist u32 each (program indices)
    programs 12 bytes each
    items    40 bytes each
    operands i32 each
    skips    i32 each (selector skip lists, in bytes)
    secpool  i32 each (second vertex sections, see below), at the end of the file

Cockpit-alternate levels (note 02 section 2.5). `levels[0 .. nnormal-1]` are the normal detail list, near to far;
the levels after them are the player's alternate list. `alt_near` and `alt_cockpit` are level indices (0xFFFF: none).
Ids 3-7, 10, 14, 15 (alternate list at base - 8): the near-body level (shape 2, threshold 0, present 1, the block of
the alternate list's first word, its single draw item decoded like any stream: op 59 or 42 or 63 synthesis then op 4)
and the cockpit level (shape 2, threshold 0, present 0, the block with part id 0xFFFF, root -1). Ids 0 and 1: one level
with present 0 and leaf 0xFFFF, both indices naming it. All other models: nnormal = levels, both indices 0xFFFF.
Consumers that walk `0 .. nnormal-1` are unaffected.

Every index is global to the file. A level, in near-to-far order:

    u16 threshold   distance (bits 0-13 of the list word, in model units; compare after the
                    instance scale shift as note 02 section 2.1 says)
    u16 leaf        raw leaf word (bit 14 = never needs a clip test, bit 13 = tier gate)
    u8 shape        0 threshold+jump+leaf node, 1 threshold+leaf+inline draw item,
                    2 leaf only (last resort level), 3 type B entry
    u8 present      0 when the leaf is 0xFFFF: draw nothing at this level
    u16 part_id, u16 tm, u16 tf       block header words 0, 1, 2 (tm, tf: precision rule)
    u8 scale_h, u8 scale_l            block byte 6 nibbles
    u8 block_flags, u8 b8, u8 b9, u8 pad, u16 pad
    i32 root        element index where the stream starts (-1: none)
    u32 vertex_first, vertex_count    slice of the vertex array
    u32 elem_first, elem_count        slice of the elements
    u32 sec_first, sec_count          (bytes 40, 44) slice of the secpool in words; 0 = no second section

Vertex (`i32 x, y, z, u32 state`): the slot array of the level, slot = index inside the
slice, operands are slot numbers (the game's byte offsets divided by 6). State: 0 undefined,
1 block vertex (3D, flat widened to (x, 0, z), type B widened), 2 strip generated
(note 02 section 2.4), 3 synthesised (section 5b, evaluated for state zero with every
documented formula), 4 unresolved (BLOCKING slot, value 0), 5 second-section slot (value 0, the core
computes it at run time, see below). The zero corners of the type B patches (ids 105, 106, 109-132) are literal file data and are state 1. Slots reached through extra
vertex ranges (`extra_n`, `extra_slot` of an item) hold the block vertex of the same index.

Second vertex section (flat blocks with flag bit 2: models 46, 72, 73, 95; note 03, "The second
vertex section"). The camera-space points cannot be computed statically, so the level carries the
section's structure. Its slice of `secpool` is a normalised stream of groups
`{ n, n pairs x z, m, m pairs x z }` (m is 0 or 2) ended by the word `-1` (counted in sec_count): each
of the n points becomes two slots (the point and the point plus the group's fixed offset, the camera-space
difference of the last two slots written before the group), each of the m points one slot. Slots are
numbered b8, b8+1, ... in stream order, after the plain block vertices; the level's vertex slice is
extended with one state 5 placeholder per slot (2n + m per group), so operand validation passes. A block with
b8 < 2 has no section (sec_count 0).

Element flags: on kinds 2 and 4, bit 1 (value 2) marks a node whose slot is blocked (lenient decode; counted in the header `blocked` field together with blocked items); on kind 5 the flags hold bits 12-14 of the source word; on kinds 1 to 5, bit 3 (value 8) marks child0 and bit 4 (value 16) child1 as a stream end by the word `0xFFFF` (the terrain drawer returns 0 at such an end, note 04a section 8b rule 9), as opposed to the end after a draw item with bit 13 or 14. (No stream of the shipped models ends this way: the output is unchanged and the extractor version stays 6.) Element (`u8 kind, u8 flags, u8 axis, u8 scale, i32 child0, i32 child1, i16 s[6],
u32 list_first, u32 list_count`). Children are element indices, -1 ends the stream.

| kind | meaning | fields |
|---:|---|---|
| 1 jump | continue at child0 | |
| 2 axis | plane = coordinate of the block vertex `s[0]` on `axis` (1 y, 2 x, 3 z); first child when the negated camera coordinate is >= the plane value p (block coordinate shifted right by L, plus the object offset for x and z); flags bit 0 swaps the two children; scale = block L | child0 (test true), child1 |
| 3 plane | point `s[0..2]` and normal `s[3..5]` (raw words, vertex scale); scale = block L | child0 (negative dot), child1 |
| 4 three-vertex | vertices `s[0..2]` (slots), scale = shift of the test (W bits 7-9 of the node) | child0 (negative result), child1 |
| 5 draw | programs `proglist[list_first .. +list_count]` in order; flags = bits 12-14 of the source word; child0 = next element after the list (-1 when the stream ends) | |

Test semantics are exactly note 02 section 3 (first child on a negative cross-product
dot, an exactly zero result takes the second child). Axis and plane sign conventions are
unverified (note 02 open question 9).

Program (`u32 item_first, u32 item_count, u32 address`): items in memory order. The
state-zero path starts at the first item and, at each selector, continues at
`address + length + skips[0]` (so op 54 and 76 skip their word and op 53 skips its
argument), until the first last-flagged item reached. Items off that path are state
variants. Item (40 bytes):

    u16 op          operation (jump-table index, note 02 section 5)
    u16 flags       1 last, 2 gated, 4 blocked, 8 two-sided polygon, 16 back colour word,
                    32 plane record in the operands, 64 speculative (reached only through the
                    op 68/72 frame skips, k = 1..15, whose real frame count is unchecked)
    u16 g1, g2      gate words (0x7FFF, 0 when ungated); see note 02 section 4
    i16 extra_n     extra vertices copied before the operation; u16 extra_slot first slot
    u16 pen         low four bits of the colour (fixed pens of ops 17, 20, 21, 23, 26)
    u16 w, wback    polygon W word and back colour word (op 11/12: the count+colour word)
    u16 prim        0 nop, 1 point, 2 line, 3 polygon, 4 detail faces, 5 quad, 6 rungs,
                    7 segments, 8 line list, 9 hatch, 10 synthesis, 11 selector, 12 polygon
                    list, 13 triangle, 14 early out, 15 figure, 16 raw
    u16 nops        operand count
    u16 length      bytes the item occupies in the source
    u32 op_first    first operand in the operand pool
    u32 address     flat source address (a selector's skip target is
                    `address + length + skip`, and is the `address` of another item)
    u32 skip_first, skip_count   slice of the skip pool: the selector's skip list, bytes,
                    entry 0 = the state-zero choice, the others the alternatives; empty for
                    items that are not selectors with a documented skip rule

Operands per operation (slots are slot numbers; "raw" are the source words):

| op | operands |
|---|---|
| 1, 58 | slot |
| 2, 48, 74 | slot, slot |
| 3 | n, n slots, then 6 words (point, normal) when flag 32 |
| 4 | face count, then per face: distance word, n, flag word, n slots |
| 6 | n, slotA1, slotA2, slotB1, slotB2 |
| 7, 8 | raw words (BLOCKING, flagged) |
| 9, 10, 27, 30 | the first slot only (A); the note's A, B, C (D) are the consecutive slots from it |
| 49, 67 | three slots |
| 69 | three slots A, B, C; the tail is op 3's: item.w = W (bit 15 two-sided, bit 14 back colour word in item.wback), plane record words appended for bit 12 as in op 3 |
| 7, 8 | raw words, never blocked (note 03 "Ops 7 and 8"): op 7 pen, a, b\|flip, c, d; op 8 pen, c, a, b\|flip, f, d (a, b block byte offsets, c, f, d slot byte offsets, d the base). The slots named by the formulas (c, base+0..2; op 8: c, c+1, f, base+0..3) are checked to exist |
| 11 | count, slot (w = count/colour word); 12: count, vref, slot |
| 13 | slot pairs; 14: vref, then slot pairs |
| 15, 16 | a, b, c (, e) slots |
| 17, 18 | slots of the quads, four per quad |
| 19, 20, 23, 32 | slot, N; 21, 22, 57: slot, N1, then N1 inner counts |
| 24 | n, n slots; 25: n; 26: n, slot |
| 31, 39, 40, 62, 70, 75 | raw words, stored as read (no division): op 31 one word = the byte offset into the drawn block's vertex list (the pair starts at block base + 10 + operand); op 39 three words, op 40 two (pens); op 62 three words in stream order = vertex byte offset, pen, size S; op 70 none (the op word alone: its skip is a function of the object's life word, note 07 11.3; its skip list carries every skip a life word 0 to 0x7F makes, entry 0 the steady picture's first variant); op 71 the count of discs and then the triples (vertex byte offset, pen, size with bit 15 removed) of the disc list; op 75 five words |
| 34, 36, 41 | slot, shift (signed); 35, 42, 43, 44, 59: slot; 38, 45, 46, 47, 56, 63, 64, 65: none (synthesis items on the absolute slots, base 0); 52: a, b, t slots; 53: a, b slots and the skip bytes |
| 51 | skip bytes |
| 55 | polygon count, then per polygon: W, back colour word, flags, then the op 3 operand list |
| no operands | 0, 33, 38, 45, 46, 47, 50, 54, 56, 63-66, 68, 72, 73, 76, 77 (38, 45-47, 56, 63-65 are synthesis items, prim 10, with `synth` formulas of note 03) |

Skip lists (emitted per item, so the core needs no table of its own; entry 0 is state zero):
op 50 `[0, 0x14, 0x52, 0x7C, 0xA6, 0xD0, 0xFA]`; 51 `[0, arg]`; 53 `[arg, 0]` (the first
animation step wins and skips the other steps); 54 and 76 `[2, 0]`; 66 `[0, 8]`; 67
`[0, 18]`; 73 `[0, 2]`; 68 `[0, 0x34 * k]` and 72 `[0, 0x12 * k]` for k = 1..15 (frames
0..15; the items found through these are flagged speculative). Entry selection by state (note 02 section 4): op 50 entry = state (0..6); 51 entry 1 when bit 3 of object word +2 is set; 53 and 76 and 54 entry 0 whenever the state-zero rule of the note applies (53: always the first step that passes its gate; 54/76: entry 0 when the state is below 6, entry 1 otherwise); 66 entry 1 for states 3-6, else 0; 67 entry 1 when the face faces away; 68/72 entry = frame (low nibble); 73 entry 1 when the object index is 0x57. The state variable that
picks an entry is the note's (section 4). Op 52 carries no skip list: the core applies the note's rule
(it skips 14 bytes per unit of the gear progress, `(byte +0x7C & 0x7F) >> 4`, 0 to 7) when it walks, and the six op 53 items that follow it are
decoded through op 53's own list, so every gear step is in the program (the core synthesises the step the progress selects in camera space; the vertex
slots the extractor bakes are the state-zero path's, the first step). Op 70's skips are all followed (so the four variants and the runs of the effect models 33 to 35 are decoded); variants of op 77 are
not followed (state ranges undocumented), so those gaps stay undecoded. Op 54 and 76 take
no argument word: the word they skip is the next item (observed: a last-flagged nop).

Deviations from the note found while decoding real data (also in the report): a one-sided
polygon whose W has bit 12 set and bits 10-11 clear is followed by an offset word to a
12-byte point-and-normal record at model base + offset (flag 32 and six operand words);
operation 72 occurs in ids 102 and 103; the strip section's backward pass starts at
`s0 + 2 * len(forward) - 3` (`- 1` with bit 15) and the next chain at `s0 + 2 * len(forward)`;
synthesis values are applied variants first and the state-zero path last, so a slot
written by several variants holds the state-zero path's value (e.g. the six op 53
animation steps: the first step's values).

Vertex slot fill: a level's slot array holds exactly the block vertices 0..b8-1, the
extra vertex ranges of the items (`extra_n` vertices from slot `extra_slot`, same index in
the block), the strip slots and the synthesised slots. Slots in between are state 0
(undefined) and referencing one is an error. All synthesis is 16-bit word arithmetic (note 03, "Vertex synthesis" and "Fixed-formula synthesis": ops 34-36, 38, 41-47, 52, 53, 56, 59, 63-65); op 59's b+26 and b+27 are synthesised like the others (state 3).
Halved terms of the synthesis formulas ("/2", "(5/2)(V6-V1)") are shifted
arithmetically right before they are added to the other terms, as note 02 5b says; the
interior points of the strip section use step = trunc(16 d / n) and prev + (k step >> 4),
which can differ from the game by one unit.

Strict and lenient decoding. `decode_model(..., strict=True)` raises `BlockedError` (a
`DecodeError`, so a `ValueError`) at a documented BLOCKING item: an unresolved
synthesis slot (a formula that reads an undefined slot), and a slot that stays undefined in a
model with a state-dependent operation whose vertex output is not documented (none is left: op 70 builds no vertices) (type B zero corners and the second vertex section of flat blocks are decoded, see above). Unknown
operations, bad vertex slots, truncated data always raise `DecodeError`. With `strict=False`
the blocked items are flagged (flag 4) and counted in the header; the CLI writes such a
model as `partial`. The core must not draw flagged items.
