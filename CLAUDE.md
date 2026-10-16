# interceptor-wasm

Reverse engineering *F/A-18 Interceptor* (Electronic Arts, 1988, Amiga) from its
original disk images, with Claude Code and [rea](https://github.com/morluto/rea).
The repository name points at a WebAssembly port as the target.

## Environment

This repo lives in an OpenShell sandbox (`rea`) on a Kyma cluster.

- **Workspace:** `/sandbox`, 50 GB. It survives sandbox restarts, but not the
  sandbox's deletion. GitHub is the only durable copy: commit and push often.
- **Shell setup:** run `. /sandbox/.tools/env.sh` first. It puts git, Java, rea,
  micromamba and npm on `PATH` and sets `GHIDRA_INSTALL_DIR`. Start `claude` after
  it, so rea's MCP server inherits that environment.
- **Toolchain** (`/sandbox/.tools`, installed by `openshell/setup-rea.sh`, safe to
  re-run): git, unzip and OpenJDK 21 (conda-forge, via micromamba), Ghidra 12.1.4,
  rea 6.4.0 (registered with Claude Code as the MCP server `rea`), Node 22.
- **No root, no apt.** Install system packages with
  `micromamba install -p /sandbox/.tools/env -c conda-forge <package>`, npm
  packages with `npm install -g <package>` (goes to `/sandbox/.tools/npm`), and
  Python packages with `pip install --user` or into a venv under `/sandbox`.
- **Network:** HTTP and HTTPS (ports 80 and 443) to public hosts, for every
  process. Hostnames with three or more labels work (`www.example.com`); a bare
  two-label domain (`example.com`) works only if `openshell/policy.yaml` lists it.
  Ask the operator to add one. No SSH, no private or cluster addresses.
- **Credentials:** none are visible in the sandbox. Claude Code uses the owner's
  Claude subscription, and git authenticates to github.com for
  `st-gr/interceptor-wasm` only. OpenShell inserts the real tokens on the way out.
- **Git only:** the GitHub credential works for git (clone, fetch, push) and nothing
  else: there is no `gh` and no API access. CI results come back through the
  `ci-results` branch (see Daily commands).
- **`make clean` deletes `build/`.** Long-lived out-of-tree builds (the vAmiga CMake
  tree, `/sandbox/vamiga-ref-build`) live outside the repo for that reason.
- **rea and Ghidra language:** `tools/setup-toolchain.sh` exports
  `REA_GHIDRA_LANGUAGE_ID=68000:BE:32:default` from `/sandbox/.tools/env.sh`. The rea
  MCP server only sees it if Claude Code was started from a shell that had sourced
  env.sh after the script ran, so restart Claude Code once after the first run. rea
  rejects native 68000 ELF files, so the image is wrapped as an ARM-labelled ELF and
  the variable selects the real language; procedure in `tools/hunk/ghidra_import.md`.

## Daily commands

Run `. /sandbox/.tools/env.sh` first. After running the setup script for the first
time, restart Claude Code so the rea MCP server sees the new environment.

    bash tools/setup-toolchain.sh                      # once per sandbox
    pip install -e tools/extract                       # once; extract and pytest need it
    python -m extract extracted/interceptor.adf assets # assets (git-ignored); extractor version 27: assets of an older one are refused, so re-extract after pulling
    node --test "tools/extract-js/test/*.test.js"      # the JavaScript extractor of the browser build (byte-identical to the Python one on the synthetic disk)
    python tools/hunk/relocate.py "extracted/disk/F-18 Interceptor" \
        extracted/interceptor.bin extracted/interceptor.map.json
    make CC=tcc core test                              # fast iteration
    make && SDL_VIDEODRIVER=dummy build/interceptor --frames 1 --dump /tmp/f.ppm
    make golden                                        # scripted runs through the screens vs tests/golden: takeoff-and-circuit, demo, training, qualification, mission-f1 .. mission-f6 (the six missions: menu F-key, briefing chain, a seeded fight (seed 1), the end code and outcome; synthetic; real too if assets/ exists)
    make golden-update-real                            # local, git-ignored tests/golden/*-real.golden; make golden-update-synthetic regenerates the committed ones
    build/tests/run_golden --audio-dump assets tests/golden/demo.txt   # the step's register writes in clear instead of the golden line: tick op voice object blank value offset length clock (--synthetic: the invented world)
    make web                                           # browser build in build/web/ (emcc on PATH, no assets: the page extracts them from the player's ADF); serve with python3 -m http.server -d build/web
    . tools/web/pw-env.sh && python tools/web/smoke.py extracted/interceptor.adf /tmp/web-smoke   # the smoke run in headless Chromium, Firefox and WebKit (pick, start, keys, flight-log reload, denied storage, gamepad, Chromium audio); --engines chromium --quick for one engine; an engine that cannot launch here is skipped with its reason (Firefox: no user namespaces; WebKit: no display for requestAnimationFrame); tools/web/README.md has the library setup, docs/browser-checklist.md the checks only a person can make
    make && build/interceptor --start-keys             # the real key path through the screens into free flight (--flight skips them; --seed N pins the placement generator of the mission start, without it the clock seeds it; --log FILE / --no-log / --logdisk N: flight log file, log-disk prompt; --ctrl NAME@N / --ctrlup NAME@N inject game-controller controls, the table is src/frontend/sdl/input.c and the manual)
    make && SDL_VIDEODRIVER=dummy build/interceptor --flight --frames 60 --dump /tmp/f.ppm   # free-camera flight frame
    pytest tools/extract tools/hunk tests/frontend tools/cleanroom -q  # needs the pip install above (tools/cleanroom: the scan tool's tests, invented bytes)
    python tools/cleanroom/scan.py --repo . --assets assets \
        --disk extracted/interceptor.adf --bin extracted/interceptor.bin   # clean-room scan: no 12-byte window of a tracked file may occur in the assets, the disk or the program image; exit 0 clean, 1 matches, 2 allow-list error; local only (the inputs are ignored by version control), about 85 s, run it under ulimit -v 2500000; tools/cleanroom/allow.txt names each tolerated run with its reason
    bash tools/release/tests/test_export.sh            # the public export on a scratch repository (a planted window must stop it)
    bash tools/release/export-public.sh --out /sandbox/interceptor-public --assets assets \
        --disk extracted/interceptor.adf --bin extracted/interceptor.bin --github-repo st-gr/interceptor-port   # the public repository: one commit of main (--ref picks another; everything but the scan inputs comes from that ref), the scan runs on it first, it never pushes; --github-repo writes the concrete Pages URL; an existing --out needs --force; the paths in tools/release/exclude.txt stay out; exit 0 done, 1 scan match, 2 usage or failure, 3 submodule not fetched, 4 bundle failed
    SDL_VIDEODRIVER=dummy build/interceptor --synthetic --start-keys --no-audio --frames 20   # the invented world of tests/world_synth.h: runs without assets (native build only; the browser build refuses it)
    make -C tools/m68k-rig test                        # Musashi rig: differential tests against the 68000 code
    make -C tools/m68k-rig test-nearground             # one of them: the near-ground pass (stage [90])
    make -C tools/m68k-rig test-sound test-sound-core test-sound-captures   # note 10: the original's sound library vs the model; src/core/sound.c vs the original (Paula register-write log); the captures' logs
    cc -std=c99 -O1 tools/vamiga-ref/sound7_port.c build/libcore.a -o build/sound7_port -lm   # once, after make: the port's side of the milestone-7 sound captures; test-sound-captures then also runs tools/vamiga-ref/sound7_check.py on the sound7-* logs (skips without them)
    make -C tools/m68k-rig test-training               # the flight school's instructor against the 68000 code (test-training-traj: whole trajectories)
    make -C tools/m68k-rig test-ai test-ai-traj        # the AI aircraft's state routines and gate (6,000 cases), and whole trajectories (AI_DIFF_N, AI_TRAJ_STEPS, AI_TRAJ_CASES, *_SEED)
    make -C tools/m68k-rig test-weapons test-radar    # the weapons (gun, missile, flares, hull test; WEAPONS_DIFF_PART, _N, _SEED) and the radar scope (RADAR_DIFF_PART, _N, _SEED)
    cd tools/m68k-rig && WEAPONS_DIFF_PART=gunorder ./test_weapons_diff   # one part: gunorder (the draw scan 0x2AD18, 2,000 pools), lmnode (the count-0 node, 400 cases) or lmstate (the landmark test with the whole machine state, 474 cases; needs extracted/captures/state-flight.bin and landmark-state-cases.bin, written by state_dump.py and landmark_state_check.py; test-weapons runs it last)
    make -C tools/m68k-rig test-missions test-seat    # the mission scripts (MISSIONS_DIFF_MODE, _N) and the ejection seat, parachute and pod (SEAT_DIFF_N, SEAT_DIFF_SPAWN=1 for the spawn)
    make -C tools/m68k-rig test-spawn test-slots test-ships   # the region spawner (SPAWN_DIFF_*), the slot builder (SLOTS_DIFF_*) and the ship blocks (SHIP_DIFF_PART, _N)
    make -C tools/m68k-rig test-lists test-lists-port test-draw test-sky test-lists-scan test-lists-walk   # the display lists, their port, the draw layer and the sky (LISTS_DIFF_*, DRAW_DIFF_*, SKY_DIFF_*)
    cc -O1 -g -shared -fPIC -o build/leakcheck.so tools/leakcheck/leakcheck.c -ldl && \
        LD_PRELOAD=$PWD/build/leakcheck.so build/tests/test_flight 2>&1 | grep leakcheck   # leak profile (valgrind and LSan cannot run here; see tools/leakcheck/README.md)
    build/interceptor --start-keys --no-audio --no-filter --wav /tmp/s.wav --frames 330 --audio-log /tmp/a.log   # sound: --no-audio opens no device, --no-filter drops the A500 filters (on by default), --wav writes 44.1 kHz stereo, 125 ms a step
    bash tools/vamiga-ref/build.sh                     # reference emulator (about 15 minutes)
    build/vamiga-ref/ref --rom disks/kick12.rom --adf extracted/interceptor.adf \
        --seconds 60 --out /tmp/game.ppm
    git fetch origin ci-results && \
        git show origin/ci-results:commit.txt origin/ci-results:status.txt   # CI result

The rig harnesses compare the port against the 68000 code and read the extracted assets (`assets/`, for example `assets/tables/ai.bin`) at run time; they skip without the image or the assets. The unit tests use invented tables.

Objects are built per compiler in `build/obj-<cc>/`, so `make CC=tcc core test` then `make test` is safe.
`make clean` deletes `build/`, including `build/vamiga-ref/ref`. Run
`bash tools/vamiga-ref/build.sh` to re-install the driver quickly: the CMake tree is
cached outside `build/`.

The `code @disks/countercodes.txt CHALLENGE` script command looks the answer up in a table in the
git-ignored `disks/`; `disks/countercodes-reachable.txt` lists only the challenges reachable in play.
Scripts and the key table: `tools/vamiga-ref/scripts/`, `docs/re/01a-reference-captures.md`.
Audio captures: the driver commands `audtrace on [nodmacon|frame]|off` (the Paula registers per frame or per CPU write)
and `wav FILE start [vamiga]|stop` (the stereo output, hard-panned and unfiltered by default) are in
`tools/vamiga-ref/README.md`; `runto ADDR SECONDS soft` makes a timeout under `audtrace` no error. The emulator is one
real-time machine: run each script alone under `flock /tmp/ref.lock build/vamiga-ref/ref ...` (a second run on the same
lock waits; wall-clock waits are jittery, so the scripts log per frame with `step`/`stepuntil`). The sound scripts are
`tools/vamiga-ref/scripts/sound-*.txt` (generator `gen-sound.py`).

The reverse-engineering notes are `docs/re/00` to `docs/re/10` (note 10: the sound library, its 37 objects, the per-pass
engine routine, the master ramp, the register-write contract of the core); the sound itself is `src/core/sound.c` (the
core, an integer model of the original's library, one server per blank, emitting register writes) and
`src/frontend/sdl/audio.c` (the Paula mixer: four voices, filters on by default).

AROS (the open-source Kickstart replacement) cannot run the game. The intended
configuration is a real Kickstart 1.2 ROM at `disks/kick12.rom` (git-ignored), run
without `--ext`, as in the `ref` line above; see `tools/vamiga-ref/README.md` and
`docs/re/01-reference-emulator.md`. Reverse-engineering note conventions are in
`docs/re/README.md`.

## Disk images

`disks/` holds the original disks and is git-ignored: one disk in three flux
formats, SCP (the most complete capture), HFE and MFI. **Never commit the disk
images or anything extracted from them**; that is the publisher's copyrighted
data. Put extracted files in `extracted/`, which is git-ignored too.

Starting points:

- Flux images must be decoded to sectors first with Greaseweazle. `pip` lives in
  the micromamba env (`micromamba install -p /sandbox/.tools/env -c conda-forge pip gcc`
  once; greaseweazle needs gcc), then
  `pip install "git+https://github.com/keirf/greaseweazle@latest"` and
  `gw convert --format amiga.amigados disks/scp/*.scp extracted/interceptor.adf`.
  The disk is plain AmigaDOS OFS; all 1760 sectors decode. `amitools` (pip)
  provides `xdftool` to unpack the ADF and `hunktool` for the executable.
- Amiga executables use the hunk format for the Motorola 68000 (big-endian). For
  Ghidra, set `REA_GHIDRA_LANGUAGE_ID=68000:BE:32:default` when importing raw
  code.

## Rebuilding the sandbox

`openshell/` holds everything needed to recreate the sandbox: the network policy,
the two provider profiles (Claude subscription, GitHub for this repo), and the
setup script.
