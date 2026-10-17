# tools/web - the browser build's smoke run

`smoke.py` drives `build/web` (from `make web`) in headless Playwright engines with the player's own ADF (never committed, never in the page):

    make web
    . tools/web/pw-env.sh                       # the sandbox's library paths and engine switches (below)
    python tools/web/smoke.py extracted/interceptor.adf /tmp/web-smoke            # Chromium, Firefox, WebKit
    python tools/web/smoke.py extracted/interceptor.adf /tmp/web-smoke --engines chromium --quick

Per engine (the same run): `pick` (refused files, then the ADF extracted in the page), `start` (the credits draw), `keys` (which keys the
game consumes; a few keys each leave the credits on a fresh page), `reload` (the flight log written at the first save point in one visit is read
back in the next visit of the same browser context, and is absent in a fresh context), `nostorage` (a context where `indexedDB.open` throws: one
message line, the run reaches a save point with the in-memory log), `gamepad` (a fake pad through `navigator.getGamepads`, button 0 pressed
after the credits stood still: the credits are left, through SDL's gamepad path and the table of `src/frontend/sdl/input.c`). In Chromium also
`audio` (`--autoplay-policy=no-user-gesture-required`; after Start the AudioContext is `running` and the ScriptProcessor callback count grows). An
engine that cannot launch, or whose headless mode cannot run the game's frame loop, is **skipped with the reason printed** (a skip is no failure;
a failed check exits 1). Screenshots go to the output directory.

The physical things (a real keyboard's F-keys, real sound, a real controller, Safari) are in `docs/browser-checklist.md`.

## The sandbox

No root and no apt: the engines' shared libraries come from conda-forge into `/sandbox/pwlibs` (`micromamba install -p /sandbox/pwlibs -c conda-forge ...`).
Do not run the engines under `ulimit -v` (Chromium dies with SIGTRAP). Chromium (milestone 6) needed: alsa-lib at-spi2-atk at-spi2-core atk dbus
expat glib libdrm libgbm libxcb libxkbcommon nspr nss and the xorg libraries (x11 xcomposite xdamage xext xfixes xrandr xtst). The engines of
milestone 7 are `python -m playwright install firefox webkit` (into `/sandbox/.pw`, with `PLAYWRIGHT_BROWSERS_PATH=/sandbox/.pw`) and:

* **Firefox 157:** `gtk3 cairo pango freetype fontconfig gdk-pixbuf xorg-libxcursor` (its own libnss3 must come first: `smoke.py` puts the Firefox directory
  at the head of its `LD_LIBRARY_PATH`) and `MOZ_DISABLE_*_SANDBOX=1` (in `pw-env.sh`). **It still does not start here**: the process dies with SIGSEGV
  right after `Sandbox: CanCreateUserNamespace() clone() failure: EPERM`, in every combination of the sandbox switches tried, so the run reports it skipped.
* **WebKit (the WPE build, which headless uses):** `flite libxml2=2.13 libxslt libjxl=0.8 libavif=1.0 libevent=2.1.12 enchant hyphen libsoup gstreamer
  gst-plugins-base lcms2 libopus libgbm mesalib mesa-llvmpipe` in `/sandbox/pwlibs`, in **one** `micromamba install` (installed separately the solver
  swaps libxml2/libjxl/libgbm for newer ones; the two old sonames libxml2.so.2 and libjxl.so.0.8 are kept in a second prefix,
  `micromamba create -p /sandbox/pwlibs-wk -c conda-forge libxml2=2.13 libjxl=0.8`, searched last), plus libs that conda-forge lacks, taken from
  Ubuntu 24.04 (noble) packages and unpacked into `pwlibs/lib`: `libicu74` (ICU 74), `libmanette-0.2-0`, `libevdev2`, `libgudev-1.0-0`,
  `libgstreamer-plugins-bad1.0-0` (only `libgstcodecparsers`), and `libbacktrace.so.0` built from github.com/ianlancetaylor/libbacktrace
  (`./configure --enable-shared`). `libgbm.so.1` needs a symlink to `libgbm.so.1.0.x` if the solver leaves none. Playwright's host check wants the
  GTK variant's libraries (gtk4, vulkan, x264), which headless does not use: `PLAYWRIGHT_SKIP_VALIDATE_HOST_REQUIREMENTS=1`. The bundle's
  `minibrowser-wpe/MiniBrowser` wrapper overwrites `LD_LIBRARY_PATH`; make it append the caller's:
  `sed -i 's|^export LD_LIBRARY_PATH=.*|export LD_LIBRARY_PATH="${MYDIR}/lib:${MYDIR}/sys/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"|' /sandbox/.pw/webkit-*/minibrowser-wpe/MiniBrowser`.
  The sandbox forbids `memfd_create` and `/dev/shm`, which WebKit needs for its shared memory (every page hangs): build the shim
  `cc -O1 -shared -fPIC -o /sandbox/pwlibs/lib/libshmshim.so tools/web/shmshim.c -ldl` (`smoke.py` preloads it for WebKit only). With all that WebKit
  launches and loads and extracts the page (`pick` passes) but `requestAnimationFrame` ticks once and stops (the headless WPE view has no display to
  pace it), and the game's main loop is rAF-driven, so the frame-driven checks are reported skipped, with that reason.
