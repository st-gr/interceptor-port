# Milestone 2 follow-ups

Deferred findings from the task reviews, the final whole-branch review and the fix-wave re-review of milestone 2. None block the merge.

## Milestone 3 openers (settle first)

- View-distance scale (src/core/world.c, docs/re/02, 04): the Tm/Tf precision bands are implemented from the nearest level block on the assumption that they match in all levels (true for models 0, 1, 5, 8, 48 and 50 only). Still open: whether the nearest-level band assumption holds for every model, the instance scale for cell items, and the range unit. One emulator capture logging V:4AB30 and V:4AA6A with the carrier at two known distances settles all of them.
- src/core/scene.h as the contract: the HUD fields (pitch, roll, contacts, cues) must be written by the flight model; object attitude and variant semantics are undefined (attitude is ignored silently by the frontend); the view kind (cockpit, external, head angle) belongs in the core, driven by view-key events (KPENTER, KP1-9, KP4/6/2/1/7/8, F10).
- src/core/world.c: separate the free camera from an aircraft state. world_step is today a camera controller; the flight model should own position and attitude and feed the world through world_set_camera.
- Handedness and roll/yaw signs (src/frontend/sdl/video.c, src/core/world.c): the signs are port choices. A roll-command capture from the emulator confirms them, and X_FLIP in video.c is the single switch if they are wrong.
- src/core/raster.c: the ground polygon, the altitude-band palette changes and the sky flag must replace raster_sky_ground, which ignores altitude and is only a stand-in.
- src/core/world.c: the 64-object and 256-terrain caps and the square range window are guesses until the original's list builder is read.
- src/frontend/sdl: replace SDL_Delay pacing with emscripten_set_main_loop under __EMSCRIPTEN__ in milestone 6.

## Rig (Tasks 1, 2)

- tools/m68k-rig: rig_run overwrites a caller-set A7 and PC, and the SP after a run, both undocumented.
- tools/m68k-rig/rig.h: a stale comment mentions only rig_call.
- tools/m68k-rig: musashi_reg is unbounded, and there is no A-register, PC or SR round-trip test.
- tools/m68k-rig Makefile: test_fx_diff goes stale because libcore.a is not a prerequisite.

## Hunk tools

- tools/hunk (Ghidra helper test): the sort test has a single symbol, duplicate names would break the set_addresses_names call, and the dependence on --base is undocumented.
- tools/hunk/relocate.py: the HUNK_SYMBOL name slice is unchecked; 13 local-label names (.L1 and so on) repeat in the real map, so docs say to skip names starting with .L.
- tools/hunk/ghidra_import.md: the rea MCP route after the Claude Code restart is still unverified.

## Emulator tooling (Task 3)

- tools/vamiga-ref: the script is parsed twice, the mouse range and hold clamp are unchecked, and exit() runs with the emulator thread live.
- tools/vamiga-ref: some captures are unlisted in the README.
- tools/vamiga-ref: `code @FILE` does not validate suffix or parent, duplicate challenge lines are last-wins, a relative path depends on the cwd, and the ESC row has no script line.
- docs/re/01a: the codewheel literal remains in git history until the approved filter-repo rewrite is applied.

## Extractor (Tasks 4, 8)

- tools/extract: index error messages are undifferentiated, names are not checked for NUL or path characters, and max-asset constants are duplicated.
- tools/extract tests: DISK_ORDER depends on the amitools hash order; assert equality with Adf.listdir instead.
- tools/extract/hunk.py: its copy of relocate.py has drifted and is unsynced; share one implementation.
- tools/extract (IMDL reader): recursion in _Stream.element, the read_terrain size check order, and an unresolved BSP slot reported as failed rather than partial.
- tools/extract (model reader): node counts differ from note 02 (extractor 57/8 against 84/16/14/4 occurrences, 41/16/7/4 distinct addresses); settle which is right.
- tools/extract: the selector skip tables are hard-coded in README prose, not carried in the file; speculative targets may add spurious items; 44 models are still partial (bridges 48-52, ids 3/6/7, 26 type-B patches, ids 0/2/10/11/13).
- src/frontend/sdl/video.c: splash_asset is still hardcoded 0, and a failed index is re-read every frame.
- Makefile: the core rule lacks an assets.h dependency.

## Core fx (Task 9a)

- src/core/fx.c: state the two's complement and arithmetic-shift assumptions with a one-line test.
- src/core/fx.h: rename the yaw/pitch/roll parameters to a/b/c until the angle roles are settled; document fx_lod_shift's contract and the fx_clip failure modes and n_out_max unit.
- src/core/fx.c: add a distkey narrow-path unit test and a -b overflow guard; fix the CLIP_BUF arithmetic comment; the fx_project -32768 edge.
- src/core/fx.h: renames (fx_clip, fx_sort, fx_distkey) against the plan's fx_clip_near and fx_sort_key are undocumented.
- tests/ and tools/m68k-rig: the differential test needs compared>0, the project domain and a D0 check; add a libm sine check; fx.h is missing from the Makefile dependencies.

## World (Task 9b and final review)

- src/core/world.c: the comment on world_make_view should say it returns the level, and the level comment is stale; the band rule reads Tm/Tf/H/L from level 0 and shifts the distance, not the vertices as the original does.
- src/core/world.c: world_dump performs a redundant pick.
- src/core/world.c: the comment on world_model_unit_shift_h is stale; the non-NULL game_world path is tested only indirectly.
- src/core/world.c: range.bin is clamped at index 63 where the original reads past the table; x_compose is not needed by 9b and is undocumented as such.

## Rasteriser and frontend (Tasks 10a, 10b)

- src/core/raster.c: raster_line is unclipped, so huge ints overflow and loop long; inclusive span ends overlap shared edges by one pixel (document); the n>128 guard has no test; no concave test; the epsilon is arbitrary.
- src/frontend/sdl/video.c: the differing-level L units, silent missing assets, an unused transparency rule, unchecked --stick/--throttle, extra BTN_ANY events, and a polygon test that depends on the start position.
- src/frontend/sdl/video.c: the carrier deck polygon is visible only in frames ~10-15 at low detail (docs/re/04 item 10); the world-module level and gate question.
- src/frontend/sdl: the milestone 1 frontend hardening items (unchecked calloc, fseek/ftell and PPM writes, unvalidated scale and frames) remain open.

## Golden and CI

- tests/golden and Makefile: decide whether the manifest hash is checked before the real-assets golden is compared (golden-update-real regenerates it; CI uses the synthetic golden only).
- docs/superpowers/specs (section 6): reconcile the golden strategy with the untracked real golden.
- tests/golden: print the core's raw integer angles instead of float-derived milli-radians, give malformed script lines an explicit parse error, and check ftell.
- .github/workflows/ci.yml: the publish retry loop lacks a sleep and `git rebase --abort`; add a concurrency group, key results by branch, and gate publish on push events.

## Notes

- docs/re/02: one long line; wording "passes them" in docs/manual.md.
- docs/re/README.md: the capture note 01a is exempt from the clean-room convention; Task 6 intro says "unmodified image", the winding sentence is awkward, and formatting is uneven.
- docs/re/03: the entry conventions (0x22002, 0x22072, sort count in D7) are verified only by use in 9b's differential tests.
- docs/re/04: item 10 (deck visibility) and the range.bin head (64, 68, 72) correction; note 02 open questions 12-18 are unresolved.
