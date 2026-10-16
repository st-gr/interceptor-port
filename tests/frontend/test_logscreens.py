# tests/frontend/test_logscreens.py - the flight-log screens through the binary against the emulator captures of note 08 section 10 (extracted/captures/logs).
# The log file the binary reads is built here from the note's values (10.2: the shipped record, the overrun of 10.5); the captures are the original's screens, so
# these runs need the extracted assets and captures and skip without them. A counted run steps the core once per frame; the keys are SDL key events at a frame.
import os
import subprocess
from pathlib import Path

import pytest

from test_text import capture_window, W, H, ASSETS, BIN, CAPTURES

LOGS = CAPTURES / "logs"
pytestmark = pytest.mark.skipif(not (BIN.exists() and (ASSETS / "tables" / "text.bin").exists() and (LOGS / "log-screen.ppm").exists()),
                                reason="build/interceptor, the extracted assets or extracted/captures/logs are absent")


def record(**kw):
    rec = bytearray(78)

    def w16(o, v):
        rec[o] = v >> 8
        rec[o + 1] = v & 255
    w16(0, kw.get("qualified", 0)); w16(2, kw.get("level", 0)); w16(4, kw.get("tours", 0))
    rec[6] = kw.get("last", 0)
    rec[8:12] = kw.get("seconds", 0).to_bytes(4, "big")
    for i, v in enumerate(kw.get("completions", [0] * 9)):
        rec[0x12 + i] = v
    rec[0x1E:0x1E + len(kw.get("callsign", b""))] = kw.get("callsign", b"")
    for off, v in kw.get("words", {}).items():
        w16(off, v)
    for off, b in kw.get("raw", {}).items():
        rec[off:off + len(b)] = b
    return b"ILOG\x01" + bytes(rec)


# the shipped record of 10.2, as the screens read it (the callsign 'R': note 08 10.2 says only "a single usable character"; the letter is the one the capture log-welcome-0.ppm
# shows on the continuing welcome's name line, flow-log-screen.txt, docs/re/01a-reference-captures.md): qualified, level 2, tours 2, mission 5, 1,609 s, F1 to F4 open, 4 started, 3 completed, 18 0, 9 6, 1 1, 3 crashes
SHIPPED = record(qualified=1, level=2, tours=2, last=5, seconds=1609, completions=[0, 0, 0, 1, 1, 1, 0, 0, 0], callsign=b"R",
                 words={0x10: 3, 0x36: 4, 0x38: 3, 0x3A: 18, 0x3C: 0, 0x3E: 9, 0x40: 6, 0x42: 1, 0x44: 1, 0x46: 3, 0x48: 0})
# 10.5, the capture overrun-a: 11 letters, a SPACE, then 15 letters; the first tour is over (tours 1)
OVERRUN = record(tours=1, callsign=b"ABCDEFGHIJK", raw={0x3E: b" ABCDEFGHIJKLMNO"})
ALL = record(qualified=1, level=2, tours=2, last=5, completions=[1] * 9)


def frame(tmp_path, blob, frames, keys, ups=(), reps=()):
    log = tmp_path / "log.bin"
    log.write_bytes(blob)
    out = tmp_path / "f.ppm"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    args = [str(BIN), "--assets", str(ASSETS), "--scale", "1", "--frames", str(frames), "--dump", str(out), "--log", str(log)]
    for k in keys:
        args += ["--key", k]
    for k in ups:
        args += ["--keyup", k]
    for k in reps:
        args += ["--keyrep", k]
    r = subprocess.run(args, capture_output=True, text=True, env=env)
    assert r.returncode == 0, r.stderr
    px = out.read_bytes()[len(b"P6\n320 200\n255\n"):]
    return [tuple(px[3 * i:3 * i + 3]) for i in range(W * H)]


def diff(fr, capture):
    return sum(1 for a, b in zip(fr, capture_window(LOGS / capture)) if a != b)


# the credits at frame 0, any key at 60 (no log-disk prompt by default), the continuing welcome (its text starts 19 steps after the key, note 08 10.4; frame 90 is the capture's typed state), then the menu
TO_MENU = ["A@60"]


def test_the_continuing_welcome_with_the_stored_callsign_equals_the_capture(tmp_path):
    assert diff(frame(tmp_path, SHIPPED, 90, TO_MENU), "log-welcome-0.ppm") == 0


def test_the_menu_after_the_welcome_equals_the_capture(tmp_path):
    assert diff(frame(tmp_path, SHIPPED, 360, TO_MENU), "log-menu.ppm") == 0


def test_the_log_screen_of_the_shipped_record_equals_the_capture(tmp_path):
    assert diff(frame(tmp_path, SHIPPED, 440, TO_MENU + ["8@400"]), "log-screen.ppm") == 0


def test_reset_shows_the_zero_record_and_equals_the_capture(tmp_path):
    assert diff(frame(tmp_path, SHIPPED, 480, TO_MENU + ["8@400", "Left Shift@430", "2@432"]), "log-reset.ppm") == 0


def test_the_shift_flag_survives_the_release_of_shift_and_dies_with_the_next_other_key_event(tmp_path):
    """Note 08 11.1 (b): the shift press sets the flag, the shift release leaves it, any other key press or release clears it."""
    chord = TO_MENU + ["8@400", "Left Shift@430", "2@432"]
    assert diff(frame(tmp_path, SHIPPED, 480, chord, ups=["Left Shift@431"]), "log-reset.ppm") == 0         # shift released before the 2: still the chord
    assert diff(frame(tmp_path, SHIPPED, 480, TO_MENU + ["8@400", "Left Shift@430", "X@431", "2@432"]), "log-reset.ppm") != 0   # another key press in between cancels it
    assert diff(frame(tmp_path, SHIPPED, 480, chord, ups=["X@431"]), "log-reset.ppm") != 0                  # so does another key's release


def test_the_auto_repeat_of_the_shift_key_does_not_set_the_one_event_flag_again(tmp_path):
    """Note 08 11.1 (b) and 5.8: the queue holds press events only. Shift, X (clears the flag), a repeat of the shift key, 2: the 2 is not a chord ("hold shift, E, E" acts once)."""
    cancelled = TO_MENU + ["8@400", "Left Shift@430", "X@431", "2@433"]
    assert diff(frame(tmp_path, SHIPPED, 480, cancelled, reps=["Left Shift@432"]), "log-reset.ppm") != 0
    assert diff(frame(tmp_path, SHIPPED, 480, TO_MENU + ["8@400", "Left Shift@430", "2@433"], reps=["Left Shift@432"]), "log-reset.ppm") == 0   # a repeat does not clear it either


def test_the_overrun_of_the_callsign_shows_the_counters_it_overwrote_and_equals_the_capture(tmp_path):
    assert diff(frame(tmp_path, OVERRUN, 440, TO_MENU + ["8@400"]), "log-overrun-a-screen.ppm") == 0


def test_the_mission_list_shows_the_lines_the_record_unlocks(tmp_path):
    assert diff(frame(tmp_path, SHIPPED, 440, TO_MENU + ["6@400"]), "log-missionlist-shipped.ppm") == 0
    assert diff(frame(tmp_path, ALL, 440, TO_MENU + ["6@400"]), "log-missionlist-all.ppm") == 0


def test_the_qualification_briefing_over_the_deck_equals_the_capture_above_the_panel(tmp_path):
    fr = frame(tmp_path, SHIPPED, 600, TO_MENU + ["5@400"])
    want = capture_window(LOGS / "qual-briefing.ppm")
    top = [i for i in range(W * H) if i // W < 165 and fr[i] != want[i]]
    assert top == []                                                  # the deck picture and the typed briefing; the panel's digits are the cockpit module's
    assert sum(1 for a, b in zip(fr, want) if a != b) <= 20
