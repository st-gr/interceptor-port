# F/A-18 Interceptor clean-room port: design

Date: 2026-10-13. Status: draft for review.

## 1. Goal

Re-create *F/A-18 Interceptor* (Electronic Arts, 1988, Amiga) as a new program
that plays like the original, written without carrying over any original code.
The first milestone is a native x86_64 build with SDL2. The final target is the
same core compiled to WebAssembly and running in a browser, first with the SDL
frontend under Emscripten, later with a Three.js frontend.

Success means: the whole game is playable, including all six missions, training,
qualification, demo and flight log; the SDL build runs on Linux, Windows and
macOS (Apple Silicon); every reverse-engineered behaviour is documented in a
note that the port code was written from.

## 2. Constraints

- **Clean room.** Analysis of the original binary produces prose and tables in
  `docs/re/`. Port code is written from those notes. No disassembly listings,
  decompiler output or original code in the repository, in notes or in comments.
- **Copyright.** The disk images, anything extracted from them, and anything
  derived from them (pictures, sounds, 3D models, terrain, fonts, texts, mission
  tables) never enter git. They live in git-ignored `disks/`, `extracted/` and
  `assets/`. The port reads them from `assets/`, which the user generates from
  their own disk image with the extractor.
- **Portability.** The core compiles with gcc, clang, MSVC, tcc and emcc. No OS
  calls in the core, fixed-width integer types, explicit little-endian asset
  files, no reliance on struct layout. tcc is used for fast iteration on x86_64
  Linux and Windows only; macOS uses clang.
- **Sandbox.** Builds and tests run on Linux x86_64 in the sandbox. Windows and
  macOS builds are proven by GitHub Actions.

## 3. What is on the disk (established)

- Plain AmigaDOS OFS disk, 1760 of 1760 sectors readable from the SCP flux image
  with Greaseweazle; no custom track format. Carries a stripped Workbench 1.2.
- `F-18 Interceptor`: one 331 KB hunk executable, 185 hunks (126 code, 32 data,
  27 bss). Compiled C with OS library stubs (exec, dos, graphics, intuition,
  audio.device, gameport.device, keyboard.device, timer.device) plus large
  hand-written 68000 blocks for 3D and rendering. 1988 Intellisoft, Bob Dinnerman.
- `pix/splsh`, `pix/inst5`, `pix/frnt5`: IFF ILBM pictures (splash, instrument
  panel, front view overlay).
- `text/*`: despite the name, signed 8-bit sound samples (engine, wind,
  catapult, gear, and five intro music parts), about 300 KB total.
- Embedded in the executable: 3D object models, terrain and landmark data,
  fonts, all menu, briefing and warning texts, mission tables, a manual-lookup
  copy protection ("DefenseNet security countercode").

## 4. Repository layout

```
docs/re/             RE notes, one per subsystem
docs/manual.md       how to play: controls, modes, missions, tips
docs/easter-eggs.md  hidden keys, jokes, unused content found during RE
docs/superpowers/    this spec and the implementation plans
tools/extract/       extractor: ADF in, assets/ out (Python, uses amitools)
tools/m68k-rig/      Musashi-based differential test rig (C)
src/core/            gameplay library, pure C99, no SDL, no I/O
src/frontend/sdl/    SDL2 frontend
src/frontend/web/    later: Emscripten shell and Three.js frontend
tests/               core unit tests, differential tests, golden scene dumps
assets/              git-ignored, extractor output
extracted/, disks/   git-ignored, already present
```

Build: one Makefile. `make CC=tcc` for iteration, `make CC=gcc` for optimised
builds, `make web` for Emscripten. No CMake and no package manager.

## 5. Core API and scene model

The core never draws and never reads input devices. It is a deterministic
state machine.

- `game_create(loader)`: creates a context and loads `assets/` through a
  caller-supplied read callback (path in, bytes out), so a browser can fetch.
- `game_input(ctx, event)`: abstract events only: stick x/y, throttle, rudder,
  named buttons (fire, select weapon, gear, hook, flares, chaff, view keys,
  menu keys, text entry). The frontend owns the mapping from SDL to events.
- `game_step(ctx)`: advances exactly one original frame tick. The frontend calls
  it at the original tick rate (to be established in the RE notes) and does no
  interpolation, so the simulation is deterministic and testable.
- `game_scene(ctx, &scene)`: fills a plain C struct with
  - screen mode (splash, menu, briefing, flight, log, debrief, paused);
  - camera: position, orientation, view kind (cockpit, external, tower, chase);
  - 3D objects: asset id, position, orientation, variant or animation state;
  - terrain window: which terrain cells and landmarks are in range;
  - projectiles, explosions, smoke, flares, chaff, rescue pod;
  - HUD and instruments as values: airspeed, altitude, heading, pitch, roll,
    vertical speed, throttle, fuel, gear, hook, weapon selection and counts,
    radar contacts, warning text, target box, ILS cues;
  - menu, briefing and message text as strings with layout hints;
  - sound triggers since the last call (sample id, pitch, volume, loop flag).
- `game_destroy(ctx)`.

All of the scene is data, not draw calls. The core uses fixed-point arithmetic
wherever the original did, so values can be compared with the test rig.

## 6. Extractor and asset formats

`tools/extract` is a Python package run once by the user:
`python -m extract disks/<image>.adf assets/`. It accepts an ADF; a flux
image is first converted with Greaseweazle (`gw convert`). It writes:

- pictures as PNG plus a palette JSON;
- sounds as 8-bit mono WAV with the playback rate from the RE notes;
- executable-embedded tables (models, terrain, landmarks, fonts, texts,
  mission tables) as JSON or small little-endian binary files, in formats that
  the RE notes define;
- `manifest.json` with file hashes and the extractor version, which the core
  checks at load so missing or mismatched assets produce a clear error.

The extractor encodes where data lives and how it is decoded. It is the first
consumer of each RE note and validates it. It copies data from the user's disk
and never ships any of it.

For the public web build, the same extraction must run in the browser: the
page asks the user for their ADF (file picker or drag and drop), converts it
to the `assets/` layout in memory with a JavaScript or WebAssembly port of the
extractor, and hands the files to the core's loader callback. No assets are
hosted. This is part of milestone 6; the native extractor's decoders are
written so they can be ported one to one (pure functions over byte arrays,
no filesystem access inside the decoders).

## 7. Reverse-engineering workflow

The binary is analysed with Ghidra through rea (language `68000:BE:32:default`,
hunk file loaded as raw segments with the relocation table applied by a small
script in `tools/`). Each subsystem is one slice, owned by one agent, and
produces one note in `docs/re/` containing: where the code or data lives
(hunk and offset), the decoded format or algorithm in prose, tables of
constants, behaviour rules, and open questions. Notes contain no listings.

Slices that only read the binary are independent and run concurrently:

1. **Assets and tables:** model format, terrain and landmark data, fonts,
   message tables, mission tables, where each lives.
2. **3D pipeline:** object transform, projection, polygon order, clipping,
   the 320x200 raster layout and palette, view kinds.
3. **Flight model:** thrust, lift, drag, stall, gear, hook, ground and carrier
   contact, catapult, arrestor, crash rules, fuel, damage effects.
4. **Cockpit and HUD:** instruments, HUD symbology, radar, warnings, views,
   the instrument panel bitmap use.
5. **AI and weapons:** enemy and friendly behaviour, guns, AIM-9L, AIM-120,
   chaff, flares, cruise missile, rescue pod, sub, hit and damage rules.
6. **Game flow:** menus, aircraft select, briefings, mission scripts, scoring,
   qualification, training, demo, flight log; also catalogues hidden keys,
   cheats, jokes and unused content for `docs/easter-eggs.md`, and the
   controls and procedures for `docs/manual.md`.
7. **Sound:** sample playback, engine pitch rules, music sequencing.

Port modules follow the same cut. Interface headers per module are agreed up
front so modules can be implemented concurrently from their notes.

## 8. SDL frontend

- Window with the 320x200 world viewport integer-scaled; aspect correction for
  the original pixel shape.
- Software rasteriser: flat-shaded polygons in the original palette, drawn in
  the original painter's order, so screenshots are comparable to the original.
- Cockpit panel from the extracted bitmap; instruments and HUD drawn as a
  320x200 pen layer following note 06's pixel rules (glyph cells, plotter
  clipping, line rows), composited over the world and scaled to the window
  with the same integer factor (amended 2026-10-13 under the perfect-replica
  directive: the original's pixel rules are the replica; a vector redraw would
  have to invent rasterisation). The window is resizable; the scale follows the
  window size, letterboxed. The Three.js frontend renders the same pen layer as
  a texture.
- Audio: SDL callback mixing the extracted 8-bit samples with per-channel pitch
  and volume from the scene's sound triggers.
- Input: keyboard and game controller mapped to core events; mapping in one
  table.

The frontend stays small so the Three.js frontend is a rewrite of the frontend
only, against the same scene struct.

## 9. Testing and verification

- **Unit tests** per core module, built with tcc, run in seconds, run in CI.
- **Differential tests:** `tools/m68k-rig` embeds the Musashi 68000 core, loads
  the original hunks from `extracted/`, relocates them, and calls selected pure
  functions (rotation, projection, flight model step, table decoders) with
  fixed inputs. Tests assert the port returns the same numbers. The rig runs
  no OS code; OS stubs return canned values where unavoidable. The rig and its
  tests are skipped automatically when `extracted/` is absent (CI).
- **Golden scene dumps:** scripted input sequences produce a scene log that is
  committed and diffed on every change.
- **Reference emulator:** FS-UAE with the AROS replacement ROM first. If the
  disk does not boot under AROS, the user buys a Kickstart 1.2 ROM (known to
  run the game) and places it in `disks/`, git-ignored. Screenshots and short
  recordings from the emulator serve as references for the frontend and as
  behavioural evidence for the RE notes.
- **CI:** GitHub Actions builds the core and runs unit and golden tests on
  Linux, Windows and macOS arm64 on every push.

## 10. Decisions

- Copy protection is removed; the security-check screen is documented, not
  reproduced.
- The flight log is a local file in the user's config directory instead of a
  log disk.
- Both aircraft (F/A-18 and F-16) are kept.
- Fidelity: the world is rendered faithfully; HUD and instruments follow the
  original's pixel rules on a 320x200 pen layer scaled with the world (amended
  2026-10-13, see section 8; the earlier "resolution independent" vector HUD
  is superseded by the perfect-replica directive).
- Scene boundary for display state: the core owns everything the original
  keeps in game state (the warning arbiter, tick generator, code word and
  latch, the frame counter n) and publishes the shown entry, blink phase and
  sound request in the scene; the frontend keeps only draw caches and redraw
  timers.
- Tick rate, fixed-point formats and all numeric constants come from the RE
  notes, never guessed.

## 11. Milestones

Each milestone gets its own implementation plan. Inside a milestone, RE slices
and port modules run in parallel wherever they are independent.

1. **Tooling and skeleton:** Makefile, extractor for pictures and sounds, core
   API stubs, SDL window showing the splash, Ghidra project and hunk loader,
   rig loading and relocating the executable, CI on three platforms.
2. **World:** models, terrain, landmarks, renderer, camera views. A free camera
   over San Francisco Bay.
3. **Flight:** flight model, cockpit, HUD, takeoff and landing from carrier and
   runway. Free flight complete.
4. **Game flow:** menus, aircraft select, training, qualification, flight log,
   demo, manual and easter-egg documents.
5. **Combat:** AI aircraft, weapons, countermeasures, damage, all six missions.
6. **Sound and polish,** then the Emscripten build of the SDL frontend as the
   first browser version.

The Three.js frontend is a separate project afterwards, against the finished
scene API.

## 12. Player documentation

`docs/manual.md` is written from the game-flow and cockpit RE notes and from
playing the port: controls per mode, instrument meanings, takeoff and landing
procedures, carrier operations, weapons use, each mission's objective and
win/lose rules. `docs/easter-eggs.md` lists hidden keys, jokes (for example the
taunting crash message), cheats and unused
content, each with how to trigger it in the port. Neither document quotes
original text beyond what is needed to identify the item.
