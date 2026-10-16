# Browser checklist

What only a person in a real browser can check. The headless smoke run (`tools/web/smoke.py`, see `tools/web/README.md`) already covers the
page, the extraction, the keys the game consumes, the flight log across a reload, a denied storage, a fake gamepad and the audio context in
Chromium. Everything below needs a real keyboard, real speakers or a real controller. Do each check on the hosted page (or `python3 -m http.server -d
build/web`) with your own disk image; one line per check, the expected result after the dash.

## Keys the browser may keep for itself

Click the game once first (the canvas needs the focus). A key the browser takes is not sent to the page; the manual's alternatives are `=` and `-`
for the throttle presets.

- **F5** in free flight, with the canvas focused: the throttle preset changes, the page does not reload (if it reloads, the browser owns F5: use `=` and `-`).
- **F6**: the throttle preset changes, the focus does not move to the address bar (otherwise use `=` and `-`).
- **F11**: not a game key (the presets are F1 to F10 only): the browser goes full screen; the game keeps running and the canvas resizes.
- **F12**: not a game key (F1 to F10 only): the developer tools may open; the game keeps running.
- **F1**: a throttle preset in free flight; no help window opens (a browser or system help window means F1 is taken).
- **`=` and `-`** (and keypad `+` and `-`): the throttle goes up and down in every browser, the whole way, with the keys held.
- **SPACE and the arrow keys**: the page does not scroll while playing.
- **Backspace**: the brake; the browser does not go back a page.
- **Browser shortcuts** (Ctrl+W, Ctrl+T, Ctrl+L) are the browser's: the game must not catch them (Ctrl+L focuses the address bar).

## Sound

Click Start (the click is the gesture the browser wants); then, with the system volume up:

- **Chromium (Chrome, Edge):** the credits music and the engine are heard from the first screen on; no crackle for a minute of flight; the volume matches the desktop build.
- **Firefox:** the same; the page loads without a permission prompt for the sound.
- **Safari (macOS and iOS):** nothing is heard before Start; after Start the sound is heard (Safari starts the AudioContext suspended: the Start click resumes it through `web_audio_unlock`). If it stays silent, tap the game once and report it.
- **Switching tabs and back:** the sound resumes, the game does not run a burst of catch-up steps.
- **Reload and Start again** after the game ended with ESC on the credits: the sound plays again.

## Flight log

- **Keeps across a reload:** play to the callsign entry, type a name, press RETURN; reload the page, drop the disk again and Start: the welcome shows the saved callsign (no entry).
- **Private window:** the page shows one line saying the flight log cannot be kept in this browser; the game plays; after a reload the log is new.
- **Browser data cleared** (site data for the page): the log is gone and the callsign entry returns.

## A physical controller

Plug in or pair a pad, click the page and press a button once (browsers show a pad only after a press):

- **A** leaves the credits (as RETURN) and selects the weapon in flight.
- **Left stick** rolls and pitches (dead zone about a quarter of the travel); **right stick** is the rudder and the throttle.
- **Triggers** fire (right) and brake (left); **bumpers** drop a flare and chaff; **B** the gear; **Start** pauses.
- **Unplug while flying:** the aircraft keeps the last keys released (it does not stay in a roll); plug in again and the pad works without a reload.
- **Layout:** the table of "Game controller" in `docs/manual.md` holds for an Xbox pad and a PlayStation pad (the standard mapping), in Chromium and Firefox; Safari's pad support is per version, note which.
