# Milestone 2: World (models, terrain, renderer, camera) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A free camera flying over the original San Francisco Bay world, rendered by the SDL frontend the way the original draws it, with every model, terrain and projection fact documented in RE notes and the math checked against the original code in the Musashi rig.

**Architecture:** Three reverse-engineering slices (data formats, math pipeline, scene composition) each produce a prose note under `docs/re/`. The extractor gains decoders written from the data-format note. The core gains a fixed-point math module and a world module written from the other two notes, and emits objects, terrain and camera in the scene struct. The SDL frontend gains a software rasteriser. Differential tests run the original routines in the rig and compare numbers with the port. Emulator captures from the real game under Kickstart 1.2 are the visual reference.

**Tech Stack:** C99 core and frontend (gcc, tcc, emcc), SDL2, Python (amitools, pytest), Musashi rig, vAmiga headless driver, rea/Ghidra through MCP.

**Spec:** `docs/superpowers/specs/2026-10-13-interceptor-port-design.md` (sections 5, 6, 7, 8, 9, 11). Prior plan: `docs/superpowers/plans/2026-10-13-milestone-1-tooling-and-skeleton.md`. Deferred items: `docs/superpowers/plans/2026-10-13-milestone-1-followups.md`.

## Global Constraints

- Clean room: RE notes are prose and tables; no disassembly, decompiler output, code, or copied game data (model vertex lists, terrain tables, palettes) in the repository. Describe formats and locations; the extractor copies the data from the user's disk into git-ignored `assets/`.
- Copyright: nothing under `disks/`, `extracted/`, `assets/` is committed; differential tests take their numbers from the rig at test time and skip when `extracted/` is absent; emulator captures stay under `extracted/captures/`.
- Core: C99, fixed-width types, no OS calls, no compiler-specific code; fixed-point arithmetic wherever the original uses it; constants come from RE notes, never guessed.
- The scene struct is data only; the frontend never touches game state.
- Build stays one Makefile; tests run with `make core test`, `pytest tools/extract tools/hunk tests/frontend -q`, `make -C tools/m68k-rig test`.
- Every RE note has the sections from `docs/re/README.md`: Location, Format or Behaviour, Constants, Open questions, Verified by.
- rea: open `extracted/interceptor-arm.elf` with `mcp__rea__open_binary`; `binary_overview` must report language `68000:BE:32:default` and 636 procedures (procedure in `tools/hunk/ghidra_import.md`).
- Sandbox shell: `. /sandbox/.tools/env.sh` first. Commit messages end with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`. Commit only with explicit paths; other agents commit concurrently.

## Review Focus

1. A model or terrain decoder meets a record the note did not anticipate (unknown polygon type or count). Expected: a clear error naming the offset and table, not a silent garbage mesh. Pinned in Task 8.
2. The rasteriser receives a polygon partly or fully behind the camera or off screen. Expected: clipped or dropped, never a crash or a wrap-around smear. Pinned in Task 10.
3. The free camera flies beyond the world's edge. Expected: terrain stops or repeats as the original does (the note decides), no out-of-range table reads. Pinned in Task 9.
4. The rig is asked to run a routine that reads tables outside the loaded image. Expected: the rig reports an error, the test fails with a message, nothing hangs. Pinned in Task 1.
5. The user's extracted assets come from an older extractor version. Expected: the core refuses with a message naming the extractor version. Pinned in Task 4.

## Task dependency graph (for parallel execution)

```
Wave A (concurrent): Task 1 rig registers · Task 2 symbols in map, ELF default · Task 3 emulator scripting · Task 4 scene fields and ordered manifest
Wave B (concurrent, after Task 2 for symbols and Task 3 for captures): Task 5 RE data formats · Task 6 RE math pipeline · Task 7 RE scene composition
Wave C: Task 8 extractor decoders (after 5) · Task 9 core math + world (after 1, 4, 5, 6, 7) · Task 10 SDL rasteriser (after 4, 7; integrates with 9)
Wave D: Task 11 golden dumps, reference comparison, docs
```

---

### Task 1: Rig register access and run-at-address

**Files:**
- Modify: `tools/m68k-rig/rig.h`, `tools/m68k-rig/rig.c`
- Modify: `tools/m68k-rig/tests/test_rig.c`

**Interfaces:**
- Consumes: existing rig (`rig_init`, `rig_poke`, `rig_call`, `rig_last_error`).
- Produces:
```c
typedef enum { RIG_D0 = 0, RIG_D1, RIG_D2, RIG_D3, RIG_D4, RIG_D5, RIG_D6, RIG_D7,
               RIG_A0, RIG_A1, RIG_A2, RIG_A3, RIG_A4, RIG_A5, RIG_A6, RIG_A7,
               RIG_PC, RIG_SR } rig_reg_t;
uint32_t rig_get_reg(rig_reg_t r);
void     rig_set_reg(rig_reg_t r, uint32_t v);
int      rig_run(uint32_t addr);   /* run from addr until its RTS returns to the sentinel; registers as set; 0 ok, else rig_last_error */
```
  Task 9's differential tests use `rig_set_reg`/`rig_run`/`rig_get_reg` for the register-based routines the notes name.

- [ ] **Step 1: Write the failing tests**

Append to `tools/m68k-rig/tests/test_rig.c`:
```c
/* add.l d1,d0 ; rts */
static const uint8_t add_regs_fn[] = { 0xD0, 0x81, 0x4E, 0x75 };

TEST(run_at_address_uses_and_returns_registers) {
    ASSERT_EQ_INT(rig_init(1 << 16), 0);
    rig_poke(0x3000, add_regs_fn, sizeof add_regs_fn);
    rig_set_reg(RIG_D0, 40);
    rig_set_reg(RIG_D1, 2);
    ASSERT_EQ_INT(rig_run(0x3000), 0);
    ASSERT_EQ_INT(rig_last_error, RIG_OK);
    ASSERT_EQ_INT(rig_get_reg(RIG_D0), 42);
    ASSERT_EQ_INT(rig_get_reg(RIG_D1), 2);
    rig_shutdown();
}

TEST(run_reports_timeout_without_hanging) {
    ASSERT_EQ_INT(rig_init(1 << 16), 0);
    static const uint8_t loop_fn[] = { 0x60, 0xFE };   /* bra.s * */
    rig_poke(0x3000, loop_fn, sizeof loop_fn);
    rig_set_max_instructions(1000);
    ASSERT_TRUE(rig_run(0x3000) != 0);
    ASSERT_EQ_INT(rig_last_error, RIG_ERR_TIMEOUT);
    rig_set_max_instructions(50000000u);
    rig_shutdown();
}

TEST(run_outside_image_is_an_error) {
    ASSERT_EQ_INT(rig_init(1 << 16), 0);
    ASSERT_TRUE(rig_run(0x100000) != 0);          /* beyond RAM */
    ASSERT_EQ_INT(rig_last_error, RIG_ERR_UNEXPECTED_PC);
    rig_shutdown();
}
```
Register the three with `RUN`.

- [ ] **Step 2: Run to verify they fail**

Run: `. /sandbox/.tools/env.sh && make -C tools/m68k-rig test`
Expected: compile error, `rig_set_reg` undeclared.

- [ ] **Step 3: Implement**

In `rig.h` add the enum and the three declarations above. In `rig.c`:
```c
static int musashi_reg(rig_reg_t r) {
    static const int map[] = { M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3, M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
                               M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3, M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, M68K_REG_A7,
                               M68K_REG_PC, M68K_REG_SR };
    return map[r];
}
uint32_t rig_get_reg(rig_reg_t r) { return m68k_get_reg(NULL, musashi_reg(r)); }
void     rig_set_reg(rig_reg_t r, uint32_t v) { m68k_set_reg(musashi_reg(r), v); }

int rig_run(uint32_t addr) {
    rig_last_error = RIG_OK;
    if (!ram || addr >= ram_size) { rig_last_error = RIG_ERR_UNEXPECTED_PC; return -1; }
    uint32_t sp = RIG_STACK_TOP(ram_size) - 4;
    wr(sp, 4, RIG_SENTINEL);
    m68k_set_reg(M68K_REG_SP, sp);
    m68k_set_reg(M68K_REG_PC, addr);
    /* same stepping loop as rig_call: one instruction per m68k_execute(1), stop at the sentinel,
       timeout after max_instructions, error if PC leaves RAM */
    ...
    return rig_last_error == RIG_OK ? 0 : -1;
}
```
Refactor the stepping loop out of `rig_call` into a static `step_until_sentinel()` used by both, so the two cannot drift.

- [ ] **Step 4: Run to verify they pass**

Run: `make -C tools/m68k-rig test`
Expected: `8 tests, 0 failures` (5 existing plus 3).

- [ ] **Step 5: Commit**

```bash
git add tools/m68k-rig/rig.h tools/m68k-rig/rig.c tools/m68k-rig/tests/test_rig.c
git commit -m "Rig: register access and run-at-address for register-based routines

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: Symbols in the address map, ELF default, wrapper test asserts

**Files:**
- Modify: `tools/hunk/relocate.py`, `tools/hunk/tests/test_relocate.py`
- Modify: `tools/hunk/wrap_elf.py`, `tools/hunk/tests/test_wrap_elf.py`
- Modify: `tools/hunk/ghidra_import.md`, `docs/re/00-binary-layout.md` (one paragraph each)

**Interfaces:**
- Produces: `interceptor.map.json` gains `"symbols": [{"name": "_OpenLibrary", "addr": 362xxx, "hunk": 172}, ...]` (absolute addresses, sorted by addr); the RE slices apply them with `mcp__rea__set_addresses_names` at the start of a session. `wrap_elf.py --machine` defaults to `arm`.

- [ ] **Step 1: Failing tests**

In `test_relocate.py`, extend `make_hunk_file()` with a `HUNK_SYMBOL` block (0x3F0) after the CODE hunk: name `"_fn"` at offset 4 (names are padded to longwords: length-in-longs word, then the name, then the 32-bit value, terminated by a 0 length). Add:
```python
def test_symbols_are_absolute_and_sorted():
    _, info = relocate(make_hunk_file(), base=0x1000)
    assert info["symbols"] == [{"name": "_fn", "addr": 0x1004, "hunk": 0}]
```
In `test_wrap_elf.py` add assertions for `e_ehsize == 52`, `p_flags == 7` (RWX), `p_align` (whatever the wrapper writes; set it to 4 and assert 4), and a test that the CLI default machine is arm (`wrap(img, base)` without machine gives `e_machine == 40`).

- [ ] **Step 2: Run to verify they fail** (`cd tools/hunk && pytest -q`).

- [ ] **Step 3: Implement**

In `relocate()`: after `build_segments()`, walk `reader.segments[i][1:]`, collect `h["symbols"]` (list of `(name, value)` tuples), emit `{"name", "addr": addrs[i] + value, "hunk": i}`, sorted by addr. Flip the wrapper default to `arm`; set `p_align` to 4. Update the two docs: the map now carries the 104 link-time symbols; sessions apply them with `set_addresses_names` (give the exact call shape: a list of `{address, name}` from the map).

- [ ] **Step 4: Run tests** (expected: all pass; regenerate `extracted/interceptor.map.json` with the real file and confirm 104 symbols, `_OpenLibrary` in hunk 172).

- [ ] **Step 5: Commit** (`git add tools/hunk docs/re/00-binary-layout.md`, trailer).

---

### Task 3: Emulator scripting and reference captures

**Files:**
- Modify: `tools/vamiga-ref/ref.cpp`
- Create: `tools/vamiga-ref/scripts/credits-to-flight.txt`
- Modify: `tools/vamiga-ref/README.md`
- Create: `docs/re/01a-reference-captures.md`

**Interfaces:**
- Produces: `ref --script FILE` executing, one per line: `wait <seconds>`, `key <NAME> [hold_seconds]` (NAME from the table below), `type <text>` (letters, digits, space, with RETURN at the end if the text ends in `\n`), `mouse <x> <y>` (absolute texture coordinates), `click`, `frame <path.ppm>`, `snapshot <path>` (vAmiga snapshot for fast resume), `restore <path>`. Capture set under `extracted/captures/` (git-ignored): `credits.ppm`, `menu.ppm`, `freeflight-select.ppm`, `cockpit-takeoff.ppm`, `external-view.ppm`, and three in-flight frames with different headings. The note lists what each shows and the exact script line that produced it.

Key names map to Amiga raw key codes (vAmiga's `KeyCode` is that code): `1`-`9` 0x01-0x09, `0` 0x0A, `Q W E R T Y U I O P` 0x10-0x19, `A S D F G H J K L` 0x20-0x28, `Z X C V B N M` 0x31-0x37, `SPACE` 0x40, `RETURN` 0x44, `ESC` 0x45, `F1`-`F10` 0x50-0x59, `UP DOWN RIGHT LEFT` 0x4C 0x4D 0x4E 0x4F, `LSHIFT` 0x60, keypad `KP0` 0x0F, `KP1`-`KP3` 0x1D-0x1F, `KP4`-`KP6` 0x2D-0x2F, `KP7`-`KP9` 0x3D-0x3F. Verify the type alias in `Core/Infrastructure/Aliases.h` and the press signature `keyboard.press(KeyCode, delay, duration)`.

- [ ] **Step 1: Implement the script interpreter** in `ref.cpp` (a `run_script(VAmiga&, path)` function; unknown command or bad argument prints `script line N: ...` and exits 2; `frame` reuses the existing PPM writer). Keep `--seconds/--out` working without a script.
- [ ] **Step 2: Write `credits-to-flight.txt`**: wait for the credits (about 150 s real time from boot; use `snapshot` right after the credits appear so later runs `restore` it in seconds), press a key, capture the menu, select free flight (menu item 2), pick a starting location, capture the cockpit, use the view keys (the manual's keypad view keys; discover them from the menu text and the key-row string at `0x37CA2`) to capture an external view, hold a turn key and capture three headings. Record every discovery (which key does what) in the note; this is the first source for `docs/manual.md`.
- [ ] **Step 3: Run it** with `--rom disks/kick12.rom --adf extracted/interceptor-autoboot.adf`, convert frames with `magick`, look at every PNG with the Read tool, and describe each in `docs/re/01a-reference-captures.md` (what is on screen, the visible palette, the viewport size in pixels measured from the frame, the cockpit panel area). Keep PNGs out of git.
- [ ] **Step 4: README update and commit** (`git add tools/vamiga-ref docs/re/01a-reference-captures.md`, trailer).

Acceptance: at least one in-flight cockpit frame and one external frame exist, and the note states the measured world viewport rectangle in Amiga pixels (this number feeds Task 10).

---

### Task 4: Scene fields, units, ordered asset lists, extractor version check

**Files:**
- Modify: `src/core/scene.h`, `src/core/game.c`, `tests/test_game.c`
- Modify: `tools/extract/extract/cli.py`, `tools/extract/extract/manifest.py`, `tools/extract/tests/test_cli.py`
- Modify: `src/frontend/sdl/video.c`, `src/frontend/sdl/assets.c`, `tests/frontend/test_splash.py`

**Interfaces:**
- Produces: `scene.h` additions, all in original world units (1 unit = 1 unit of the original fixed-point world; the metre conversion is a constant in `docs/re/02-assets-and-tables.md`, applied only by frontends that need it):
```c
#define GAME_MAX_TERRAIN 256
#define GAME_MAX_EFFECTS 64
#define GAME_MAX_CONTACTS 16

typedef struct { uint16_t cell_id; game_vec3_t pos; } game_terrain_t;      /* cell_id indexes assets/terrain index */
typedef enum { GAME_FX_SHELL = 0, GAME_FX_MISSILE, GAME_FX_EXPLOSION, GAME_FX_SMOKE, GAME_FX_FLARE, GAME_FX_CHAFF, GAME_FX_POD } game_fx_kind_t;
typedef struct { game_fx_kind_t kind; game_vec3_t pos; uint16_t age; } game_effect_t;
typedef struct { int16_t bearing_deg, range; uint8_t hostile; } game_contact_t;

/* added to game_hud_t */
int32_t pitch_deg, roll_deg;
uint16_t contact_count; game_contact_t contacts[GAME_MAX_CONTACTS];
int16_t target_box_x, target_box_y; uint8_t target_box_visible;
int16_t ils_h, ils_v; uint8_t ils_visible;

/* added to game_scene_t */
uint16_t terrain_count; game_terrain_t terrain[GAME_MAX_TERRAIN];
uint16_t effect_count;  game_effect_t effects[GAME_MAX_EFFECTS];
```
  `assets/pix/index.txt` and `assets/sounds/index.txt`: one name per line in order; `splash_asset` and `sample_id` index those lists. `assets/VERSION` holds the extractor version (`1`); `game_create` reads it through the loader and fails with `"assets were made by extractor version X, need Y"` on mismatch (bump `EXTRACTOR_VERSION` to 2 in this task because the layout changed).

- [ ] **Step 1: Failing tests**: in `test_game.c`, `create_refuses_wrong_extractor_version` (fake loader returning `"1\n"` for `VERSION` while the core needs 2) and `create_reads_asset_indexes` (fake loader returning `splsh\ninst5\nfrnt5\n` for `pix/index.txt`; after create, `game_scene` gives `splash_asset == 0`). In `test_cli.py`, assert `pix/index.txt` lists the three names in order, `sounds/index.txt` lists the sound names in disk order, `VERSION` reads `2`, and the manifest's `extractor_version` is 2. In `test_splash.py`, build the synthetic assets with `VERSION` and `pix/index.txt`, and assert the frontend still renders the splash and that a missing `pix/index.txt` makes the core fail with a message naming the file.
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement**: the core gets a small `assets.c` (`src/core/assets.c/.h`: `int core_assets_load_index(const game_loader_t*, const char *path, char names[][32], int max)`, and the VERSION check in `game_create`); the frontend's hard-coded `names[]` is replaced by the index loaded through the loader; the extractor writes the two index files and VERSION.
- [ ] **Step 4: Run all three suites**, then the real extractor (`python -m extract extracted/interceptor.adf assets`) and the real splash dump.
- [ ] **Step 5: Commit** (explicit paths, trailer).

---

### Task 5: RE slice: assets and tables (`docs/re/02-assets-and-tables.md`)

**Files:**
- Create: `docs/re/02-assets-and-tables.md`
- Create: `tools/re/` throwaway decoders are NOT committed; keep them under `extracted/scratch/`.

**Interfaces:**
- Consumes: Ghidra via rea (MCP), the symbol map from Task 2, captures from Task 3, rig for table reads.
- Produces: a note that lets Task 8 write decoders without looking at the binary. Required content:
  - **3D object models**: where the model table lives (hunk, address range), how an object is addressed (index to pointer or fixed stride), the record layout (vertex count and vertex format, polygon count and polygon format: vertex indices, colour index, flags such as double-sided or always-visible), coordinate system and units, the list of object ids with a short description of each (F/A-18, F-16, MiGs, carrier, Golden Gate, Transamerica, etc.), and any per-object scale or bounding value. The way to verify: decode one model with a throwaway script, plot it as a wireframe PNG (matplotlib or a hand-written PPM), and compare its silhouette with a capture from Task 3; describe the comparison in "Verified by".
  - **Terrain and landmarks**: how the Bay area is stored (grid cells, polygons, coastline list, elevation), world extents, cell size, where landmarks are placed and which model each uses, the airfield and carrier positions.
  - **Palette and colour**: the in-flight palette (which graphics registers are loaded, from which table; day/night variants if any), the sky and ground colours, and how model colour indices map to palette entries.
  - **Fonts and HUD glyphs**: where the character sets live and their format (for Task 10's HUD later; a location and size are enough).
  - **Data hunks** 41-58, 60-63, 65-70: a table of what each holds, answering open question 1 of the layout note.
  - **Constants**: fixed-point formats (how many fraction bits for positions, angles, the angle unit per full circle), world unit to metres or feet if the HUD altitude code reveals it.
- [ ] **Step 1**: open the image in rea, apply the symbol names, locate the IFF loader in hunk 0 and the data references from the assembly hunks into 41-70 (xrefs from the code that walks object records). Identify the model table by finding the drawing code's loads (vertex fetch loops) and following their base pointers.
- [ ] **Step 2**: decode with a throwaway script under `extracted/scratch/` (not committed), render wireframes, compare with captures.
- [ ] **Step 3**: write the note with every section above; record unknowns under Open questions, each with how to resolve it.
- [ ] **Step 4**: commit the note only (`git add docs/re/02-assets-and-tables.md`, trailer).

Acceptance: a fresh agent can write Task 8's decoders from the note alone; the note names at least the aircraft, carrier and two landmarks with their ids; the terrain extents and cell size are stated with units.

---

### Task 6: RE slice: math pipeline (`docs/re/03-3d-pipeline.md`)

**Files:**
- Create: `docs/re/03-3d-pipeline.md`
- Create: `tests/rig/vectors.md` is NOT needed; test vectors are produced at test time (see Task 9).

**Interfaces:**
- Produces: for each routine in the pipeline, its address, its register-based calling convention (inputs, outputs, clobbers), its algorithm in prose with the fixed-point formats, and the tables it reads (sine/cosine, reciprocal, perspective divide): rotation of a point by the aircraft or camera attitude, translation to camera space, perspective projection to screen coordinates (including the focal length constant and the viewport centre), near-plane and screen clipping, back-face or visibility test, the painter's ordering key (distance, centroid, or precomputed order), and the polygon filler's interface (what it takes: screen vertex list and colour). Also the angle representation (units per circle), the matrix form if the code builds one per frame, and the order of rotations.
- [ ] **Step 1**: starting from the vertex-fetch loops found in Task 5 (or independently from the sine table, which is easy to spot as a 90 or 256 entry monotonic table), follow the data flow to the screen write; name each routine in the note by its address and role.
- [ ] **Step 2**: for each pure routine, record a worked example in prose: "with input registers D0..D2 = (x, y, z) and angle A, the routine leaves D0, D1 = (sx, sy)", obtained by running it in the rig with `rig_set_reg`/`rig_run` (Task 1). Give three input triples and outputs per routine; these go in the note as examples, which is allowed (they are our chosen inputs and the resulting numbers, not game data).
- [ ] **Step 3**: write the note; commit it only.

Acceptance: Task 9 can implement `fx_rotate`, `fx_project`, `fx_clip` and the sort key from the note and reproduce the note's worked examples bit-exactly.

---

### Task 7: RE slice: scene composition and views (`docs/re/04-scene-composition.md`)

**Files:**
- Create: `docs/re/04-scene-composition.md`

**Interfaces:**
- Produces: the per-frame drawing order and screen layout: the world viewport rectangle in pixels and its position on the 320x200 screen (cross-checked with Task 3's measurement), how the sky and ground are drawn (horizon line from pitch and roll, fill colours), which terrain cells and landmarks are considered each frame (range window around the aircraft, culling rule), object visibility distance, the four view kinds (cockpit, external, tower, chase: where the camera sits relative to the aircraft, how the keypad changes point of view and distance as the training text says), what the main loop does per frame and in what order (answers open question 5), and the frame tick rate (how the game paces: vertical blank, timer.device, or free-running).
- [ ] **Step 1**: locate the main loop in hunk 0 and the frame routine it calls in hunks 4-40; follow the call order; identify the horizon drawing and the fill routines; identify the view-key handling (the key-row string at `0x37CA2` and the keypad codes).
- [ ] **Step 2**: confirm with captures from Task 3 (the viewport rectangle, the horizon at level flight, an external view).
- [ ] **Step 3**: write the note; commit it only.

Acceptance: the tick rate is stated with its evidence; the viewport rectangle is stated; the draw order is a numbered list.

---

### Task 8: Extractor decoders for models, terrain, palette (from note 02)

**Files:**
- Create: `tools/extract/extract/tables.py` (locating tables in the executable by the note's addresses, with the hunk map), `tools/extract/extract/models.py`, `tools/extract/extract/terrain.py`, `tools/extract/extract/palette.py`
- Modify: `tools/extract/extract/cli.py`, `tools/extract/extract/manifest.py`
- Create: `tools/extract/tests/test_models.py`, `test_terrain.py`, `test_palette.py`, `test_tables.py`

**Interfaces:**
- Consumes: `docs/re/02-assets-and-tables.md`; the executable from the ADF (`F-18 Interceptor`), read through `Adf.read`.
- Produces (all little-endian, formats documented in `tools/extract/README.md`):
  - `assets/models/index.txt` (one model name per line, in id order) and `assets/models/<name>.bin`: header `u32 magic 'IMDL'`, `u16 vertex_count`, `u16 polygon_count`, then vertices as `i32 x, y, z` (the original fixed-point values, unscaled) and polygons as `u16 n, u16 colour, u16 flags, u16 idx[n]`.
  - `assets/terrain/index.txt` and `assets/terrain/<cell>.bin` with the same polygon encoding plus a `i32 origin_x, origin_z` header; `assets/terrain/world.txt` with extents, cell size and landmark placements (`<model> <x> <y> <z> <yaw>` per line).
  - `assets/palette/flight.pal`: 32 entries of `u8 r, g, b` (12-bit Amiga colours scaled to 8 bits by `v * 17`).
- [ ] **Step 1: Failing tests** with synthetic executables built in the tests from the note's record layout (the test constructs bytes following the documented format; no game bytes). Include a malformed-record test that expects `ValueError` naming the table and offset (Review Focus 1).
- [ ] **Step 2: Implement** the decoders as pure functions over bytes (`decode_model(data, offset) -> Model`, `decode_terrain(...)`, `decode_palette(...)`), the writers, and the CLI integration; bump `EXTRACTOR_VERSION` to 3 and the core's expected version with it.
- [ ] **Step 3: Run on the real disk**; load one model `.bin` in a throwaway script and check vertex and polygon counts against the note; `git status` must not show `assets/`.
- [ ] **Step 4: Commit.**

---

### Task 9: Core fixed-point math and world module (from notes 02, 03, 04)

**Files:**
- Create: `src/core/fx.h`, `src/core/fx.c` (fixed-point: `fx_sin`, `fx_cos` from the extracted or regenerated table as the note decides, `fx_rotate`, `fx_project`, `fx_clip_near`, `fx_sort_key`)
- Create: `src/core/world.h`, `src/core/world.c` (loads models, terrain, palette through the loader; holds the camera; produces objects, terrain window and camera in the scene)
- Modify: `src/core/game.c` (mode `GAME_MODE_FLIGHT` with the free camera when `GAME_BTN_RETURN` is pressed on the splash, for milestone 2 only; the real menu comes in milestone 4)
- Create: `tests/test_fx.c`, `tests/test_world.c`, `tests/rig/test_fx_diff.c` (built and run by the rig Makefile; skipped without `extracted/`)

**Interfaces:**
- Consumes: `scene.h` from Task 4, assets from Task 8, rig from Task 1.
- Produces: `game_input` axis events move the free camera (stick = pitch and yaw rate, throttle = speed); `game_scene` fills `camera`, `objects` (landmarks and the carrier in range), `terrain` (cells in the window), `hud.pitch_deg`, `hud.roll_deg`, `hud.heading_deg`, `hud.altitude_ft`; `fx.h` exposes the pure functions with the exact fixed-point formats from note 03 (document them in the header).
- [ ] **Step 1: Unit tests** for `fx` from the worked examples in note 03 (the three inputs and outputs per routine) and for the world window (camera at the carrier sees the carrier; beyond the extents nothing is read out of range: Review Focus 3).
- [ ] **Step 2: Differential tests**: `tests/rig/test_fx_diff.c` loads the relocated image, and for 200 pseudo-random inputs (fixed seed) runs the original rotation and projection routines with `rig_set_reg`/`rig_run` and the port's `fx_rotate`/`fx_project`, asserting equality; it prints "skipped" when `extracted/interceptor.bin` is absent. Hook it into `make -C tools/m68k-rig test`.
- [ ] **Step 3: Implement** `fx` and `world`; keep every constant sourced from the notes with a comment naming the note section.
- [ ] **Step 4: Run** `make core test`, the rig suite, and a scene dump: a tiny test driver prints `object_count` and `terrain_count` for a camera at the carrier.
- [ ] **Step 5: Commit.**

---

### Task 10: SDL software rasteriser and world view (from notes 03, 04)

**Files:**
- Create: `src/frontend/sdl/raster.h`, `src/frontend/sdl/raster.c` (flat-shaded convex polygon fill into the 320x200 framebuffer with the viewport rectangle from note 04; screen clipping; sky and ground fill with the horizon from camera pitch and roll)
- Modify: `src/frontend/sdl/video.c` (in `GAME_MODE_FLIGHT`: load the palette from `assets/palette/flight.pal`, build the polygon list from `scene.objects` and `scene.terrain` using the model data loaded through the loader, transform with the core's `fx` functions, sort by `fx_sort_key`, fill in order, then draw the cockpit panel bitmap over the bottom area)
- Modify: `src/frontend/sdl/main.c` (keyboard mapping for the free camera: arrows for stick, `+`/`-` throttle, keypad view keys per note 04)
- Create: `tests/frontend/test_raster.py` (dump-based: a scene with one known polygon; assert the filled pixel count and bounding box; a polygon straddling the viewport edge and one behind the camera: Review Focus 2)
- Create: `tests/frontend/raster_driver.c` (a tiny C program linking raster.c to fill a given polygon and dump a PPM for the Python test)

- [ ] **Step 1: Failing tests** (`tests/frontend/test_raster.py`, run through the driver).
- [ ] **Step 2: Implement** the rasteriser and the flight-mode render path.
- [ ] **Step 3: Compare** a dump with a Task 3 capture of the same view (place the free camera at the capture's cockpit position; the note gives the start position): same horizon height, same terrain outlines, same colours; describe differences in `docs/re/04-scene-composition.md` under Open questions.
- [ ] **Step 4: Commit.**

---

### Task 11: Golden scene dumps, documentation, CI

**Files:**
- Create: `tests/golden/freeflight-200.txt` (a scripted input sequence of 200 ticks and the resulting scene log as text: tick, camera, object and terrain counts) and `tests/golden/run_golden.c`
- Modify: `Makefile` (target `golden`), `.github/workflows/ci.yml` (run `make golden`)
- Create: `docs/manual.md` (first section: free-camera controls, view keys as discovered in Task 3)
- Modify: `README.md`, `CLAUDE.md` Daily commands (golden target, rig differential tests)

- [ ] **Step 1**: write the golden runner (feeds the input script to the core, prints the scene summary per tick) and commit its output as the golden file; `make golden` diffs a fresh run against it.
- [ ] **Step 2**: CI runs `make golden` on all three platforms (the core has no platform dependence; a difference across platforms is a bug).
- [ ] **Step 3**: docs; final verification pass (all suites, CI green via `ci-results`); commit; push.
