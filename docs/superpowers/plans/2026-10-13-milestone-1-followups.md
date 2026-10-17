# Milestone 1 follow-ups

Deferred findings from the whole-branch review of milestone 1. None block the merge.

## Milestone 2 openers (settle before RE slices and port modules build on them)

- tools/m68k-rig: the rig needs rig_set_reg/rig_get_reg for D0-D7, A0-A6 and SR, because the hunks 4-40 math routines are register-based, not C ABI.
- src/core/scene.h: game_hud_t and game_scene_t lack pitch, roll, radar contacts, target box, ILS cues, terrain window, projectiles, smoke, flares, chaff and rescue pod; game_vec3_t units are undefined.
- tools/extract, src/frontend/sdl/video.c: the manifest's `files` is a hash map, so `asset_id`/`splash_asset` have no ordered list to index, and the splash name mapping lives in the frontend.
- tools/hunk/relocate.py: HUNK_SYMBOL tables are dropped, so stub names are absent from the flat image; add symbols to map.json.
- tools/hunk/wrap_elf.py: the --machine default is 68k while arm is the working variant.
- tools/hunk (test_wrap_elf): does not assert e_ehsize, p_flags or p_align.
- tools/hunk/ghidra_import.md: verify the rea MCP route after the Claude Code restart and record the result here.

## Rig hardening

- tools/m68k-rig: rig_poke/rig_peek are unchecked.
- tools/m68k-rig: rig_init called twice leaks.
- tools/m68k-rig: registers persist between calls.
- tools/m68k-rig: a wandering PC inside RAM is only caught by the timeout.
- tools/m68k-rig: rig_call with NULL ram reports UNEXPECTED_PC.
- tools/m68k-rig: m68kops is generated inside the submodule directory.

## Extractor hardening (when executable-table decoders arrive)

- tools/extract (ILBM decoder): a malformed ILBM raises IndexError/struct.error instead of ValueError.
- tools/extract (ILBM decoder): compression > 1 and nplanes > 8 are unvalidated.
- tools/extract (tests): the byterun1 0x80 branch and the truncated-body path are untested.
- tools/extract (write_png): the palette is unvalidated.
- tools/extract (test_png): CRCs are skipped.
- tools/extract (ADF reader): Adf.close is not in try/finally.
- tools/extract (tests): the success test is thin, with no 16-sound assertion.
- tools/extract (driver): empty parent directories are left behind on failure.
- tools/extract (driver): rmtree/rename outside the inner try can leave staging behind.

## Frontend hardening (when the rasteriser arrives)

- src/frontend/sdl: a missing picture is re-read every frame with no diagnostic.
- src/frontend/sdl/video.c: the (int)len passed to stbi is unguarded.
- src/frontend/sdl/video.c: calloc is unchecked in video_create.
- src/frontend/sdl: scale and frames are unvalidated.
- src/frontend/sdl: snprintf/strncpy truncation is unreported.
- src/frontend/sdl: fseek/ftell are unchecked.
- src/frontend/sdl: PPM fwrite/fclose are unchecked.
- src/frontend/sdl: replace SDL_Delay pacing with emscripten_set_main_loop under __EMSCRIPTEN__ in milestone 6.
- Makefile: the frontend rule lacks header prerequisites.

## Hunk relocator CLI

- tools/hunk/relocate.py: an oversize chunk gives a bare ValueError.
- tools/hunk/relocate.py: a missing input file produces a traceback.
- tools/hunk (tests): --base and the error path are untested.
- tools/hunk: amitools is not declared as a dependency.

## CI

- CI workflow: the publish retry loop lacks a sleep and `git rebase --abort`.
- CI workflow: add a concurrency group.
- CI workflow: results are not keyed by branch.
- CI workflow: gate publish on push events so fork PRs do not fail.
- CI workflow: add top-level `permissions: contents: read`.
- CI workflow: pin actions by SHA.
- CI workflow: setup steps are not logged.

## Toolchain

- Makefile: SDL_RPATH becomes a bare `-Wl,-rpath,` without pkg-config.
- Makefile: `LDFLAGS ?=` is silently overridden by the environment.
- README: silent on binaryen's GitHub origin.
- tools/setup-toolchain.sh: the grep for EM_BINARYEN_ROOT matches comments.
- Makefile: the web rule lacks an assets/ prerequisite.
- tools/setup-toolchain.sh: greaseweazle is installed from a moving tag.
- tools/setup-toolchain.sh: REA_GHIDRA_LANGUAGE_ID is exported globally and forces every rea analysis in the sandbox to 68000.

## Reference emulator

- tools/vamiga-ref/build.sh: silences conda/cmake output and hard-codes the x86_64 ar/ranlib triple.
- tools/vamiga-ref: scripts hard-code /sandbox/interceptor-wasm.
- tools/vamiga-ref/ref.cpp: interlace fields are not distinguished.
- tools/vamiga-ref/fetch_aros_rom.py: the fetch has no timeout.
- tools/vamiga-ref/fetch_aros_rom.py: is_regular_file outside try can throw.
- docs/re/01-reference-emulator.md: the Kickstart example is unrun.
- tools/vamiga-ref: the clone path is not re-run end to end.

## Core tests

- tests/test_game.c: tests leak on the failure path.
- tests/test_game.c: the third test lacks a `g != NULL` assertion.
