# tests/frontend/test_logwiring.py - the flight log through the binary (main.c, logfile.c, game_log_*; plan Task 8, Review Focus 3).
# The log file is the port container of note 08 10.3: the magic "ILOG", the version byte 1, the 78-byte record. Runs on the extracted assets; skips without them.
import os
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "interceptor"
ASSETS = ROOT / "assets"

pytestmark = pytest.mark.skipif(not (BIN.exists() and (ASSETS / "tables" / "text.bin").exists() and (ASSETS / "models" / "index.txt").exists()),
                                reason="build/interceptor or the extracted assets are absent")


def run(args, frames=700):
    # --no-audio: the log wiring needs no device, and under load the mixer reports an underrun on stderr (which these tests assert empty)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    return subprocess.run([str(BIN), "--assets", str(ASSETS), "--scale", "1", "--no-audio", "--frames", str(frames)] + args, capture_output=True, text=True, env=env)


def test_a_first_tour_saves_the_container_at_the_end_of_the_callsign_entry(tmp_path):
    log = tmp_path / "sub" / "flightlog.bin"                          # the directory is created
    r = run(["--log", str(log), "--start-keys"])
    assert r.returncode == 0, r.stderr
    data = log.read_bytes()
    assert len(data) == 83 and data[:4] == b"ILOG" and data[4] == 1
    assert data[5 + 4:5 + 6] == b"\x00\x01"                          # tours: the first-tour welcome counted itself
    assert r.stderr == ""                                            # a missing file is the normal first run, nothing is reported


def test_a_counted_run_without_log_and_no_log_write_nothing(tmp_path):
    env_home = dict(HOME=str(tmp_path), XDG_CONFIG_HOME=str(tmp_path / "cfg"))
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", **env_home)
    for extra in ([], ["--no-log"]):
        r = subprocess.run([str(BIN), "--assets", str(ASSETS), "--scale", "1", "--no-audio", "--frames", "400", "--start-keys"] + extra, capture_output=True, text=True, env=env)
        assert r.returncode == 0, r.stderr
    assert list(tmp_path.iterdir()) == []                            # no file, no directory


def test_a_refused_file_is_reported_once_the_game_starts_and_the_first_save_replaces_it(tmp_path):
    for bad in (b"hello", b"", b"ILOG\x02" + bytes(78), b"XLOG\x01" + bytes(78), b"ILO"):
        log = tmp_path / "bad.bin"
        log.write_bytes(bad)
        r = run(["--log", str(log), "--start-keys"])
        assert r.returncode == 0, r.stderr
        assert len(r.stderr.strip().splitlines()) == 1, r.stderr
        assert "starting a new log" in r.stderr
        data = log.read_bytes()
        assert len(data) == 83 and data[:5] == b"ILOG\x01"            # replaced by the first save, which is the first tour's callsign entry


def test_a_good_file_continues_the_tour_and_a_longer_or_shorter_container_is_used(tmp_path):
    log = tmp_path / "ok.bin"
    rec = bytearray(78)
    rec[4:6] = b"\x00\x02"                                           # tours 2: the continuing welcome
    rec[0x1E:0x21] = b"ACE"
    for blob in (b"ILOG\x01" + bytes(rec), b"ILOG\x01" + bytes(rec) + b"\x99" * 20, b"ILOG\x01" + bytes(rec)[:40]):
        log.write_bytes(blob)
        r = run(["--log", str(log), "--start-keys"])
        assert r.returncode == 0, r.stderr
        assert r.stderr == ""
        assert log.read_bytes() == blob                              # the welcome does not save, and nothing else happened


def test_an_unwritable_log_is_reported_once_and_the_game_goes_on(tmp_path):
    blocker = tmp_path / "file"
    blocker.write_bytes(b"x")
    r = run(["--log", str(blocker / "flightlog.bin"), "--start-keys"])  # a directory cannot be made under a file
    assert r.returncode == 0, r.stderr
    assert r.stderr.count("cannot be written") == 1, r.stderr


def test_a_log_disk_prompt_with_a_good_file_goes_to_the_menu_with_no_log(tmp_path):
    # 10.3: the disk state is still not 0 when RETURN leaves the prompt: the menu without a log (no welcome, no saving, the log screen of key 8 unreachable)
    log = tmp_path / "ok.bin"
    rec = bytearray(78)
    rec[4:6] = b"\x00\x02"
    rec[0x1E:0x21] = b"ACE"
    blob = b"ILOG\x01" + bytes(rec)
    shots = []
    for extra in ([], ["--key", "8@120"]):
        log.write_bytes(blob)
        out = tmp_path / f"f{len(extra)}.ppm"
        r = run(["--log", str(log), "--logdisk", "3", "--dump", str(out), "--key", "A@3", "--key", "Return@20"] + extra, frames=200)
        assert r.returncode == 0, r.stderr
        assert r.stderr == ""
        assert log.read_bytes() == blob                              # nothing saved, the tour not counted
        shots.append(out.read_bytes())
    assert shots[0] == shots[1]                                      # key 8 did nothing: there is no log screen without a log
