# tests/frontend/test_audio_main.py - the SDL frontend's sound path (src/frontend/sdl/main.c: --wav, --no-audio, --no-filter, the device) end to end.
# The synthetic device tests need build/interceptor only (--synthetic, the invented world: they run in CI); the others need build/interceptor, build/tests/run_golden and the extracted assets/ (the flight is the real core's; the samples are the extracted ones, read here with the table of
# assets/tables/sounds.bin and sounds/index.txt, independently of the C loader). Nothing from the game is stored in the repository.
#
# The engine check: run_golden --audio-dump prints the register writes of the sound library per tick in clear. A model of voices 0 and 3 (the left channel) is built here from those
# writes alone, as an event model of the hardware (the sample lasts the period in force at its start, the voice restarts the block LATCHED at the block's end, a write acts at its blank and
# colour clock, step k starting at blank floor(25 k / 4), the port's clock of 50 blanks a second) and compared frame for frame with the left channel of the port's own --wav (filters off).
import array
import math
import os
import struct
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / "build" / "interceptor"
GOLD = ROOT / "build" / "tests" / "run_golden"
ASSETS = ROOT / "assets"

# Two groups. The tests with the invented world (--synthetic: tests/world_synth.h, no game data) need only build/interceptor and run in CI. The tests that compare the port with the extracted samples need
# build/tests/run_golden and the extracted assets/, which are the publisher's and never committed: each of them skips on its own, with this reason, where they are absent.
needs_exe = pytest.mark.skipif(not EXE.exists(), reason="build/interceptor missing (make)")
needs_assets = pytest.mark.skipif(not (EXE.exists() and GOLD.exists() and (ASSETS / "tables" / "sounds.bin").exists()),
                                  reason="needs the extracted assets/ (the publisher's: never committed) and build/tests/run_golden")

RATE = 44100
STEPS = 80
ENV = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")


def read_wav(path):
    d = Path(path).read_bytes()
    assert d[:4] == b"RIFF" and d[8:16] == b"WAVEfmt "
    fmt, ch, rate, byte_rate, align, bits = struct.unpack("<HHIIHH", d[20:36])
    assert (fmt, ch, rate, bits) == (1, 2, RATE, 16) and align == 4 and byte_rate == RATE * 4
    assert d[36:40] == b"data"
    n = struct.unpack("<I", d[40:44])[0]
    assert n == len(d) - 44 and struct.unpack("<I", d[4:8])[0] == len(d) - 8
    a = array.array("h")
    a.frombytes(d[44:])
    return a


def wav_run(tmp_path, name, *extra):
    out = tmp_path / name
    r = subprocess.run([str(EXE), "--flight", "--seed", "1", "--frames", str(STEPS), "--no-audio", "--wav", str(out), *extra],
                       capture_output=True, text=True, env=ENV, timeout=120)
    assert r.returncode == 0, r.stderr
    return out


@needs_assets
def test_wav_length_and_determinism(tmp_path):
    a = wav_run(tmp_path, "a.wav")
    b = wav_run(tmp_path, "b.wav")
    s = read_wav(a)
    assert len(s) == STEPS * 25 // 4 * 882 * 2      # 80 steps = 500 blanks of 882 frames (the port's blank is 1/50 s: 0.125 s a step on average), stereo
    assert a.read_bytes() == b.read_bytes()
    assert any(v != 0 for v in s)                     # the engine plays


@needs_assets
def test_no_filter_changes_the_samples(tmp_path):
    a = read_wav(wav_run(tmp_path, "f.wav"))
    b = read_wav(wav_run(tmp_path, "n.wav", "--no-filter"))
    assert len(a) == len(b) and a != b


def sound_files():
    table = (ASSETS / "tables" / "sounds.bin").read_bytes()
    names = (ASSETS / "sounds" / "index.txt").read_text().split()
    cache = {}

    def synth(obj):
        """the two synthesised buffers (note 10 3.2, 3.3), built here from the note's rules: the square wave, and the first 16,384 bits of the library's shift register"""
        if obj == 4:
            return bytes([127] * 16 + [130] * 16)
        r, out = 0x47091247, bytearray()
        for _ in range(2048):
            byte = 0
            for _ in range(8):
                bit = (r ^ (r >> 3)) & 1
                r = (r >> 1) | (bit << 31)
                byte = (byte << 1) | bit
            out.append(byte)
        return bytes(out)

    def sample(obj):
        rec = table[obj * 20:obj * 20 + 20]
        f = rec[0]
        if f == 0xFF:
            return cache.setdefault(("s", obj if obj == 4 else 6), synth(obj))
        if f not in cache:
            cache[f] = (ASSETS / "sounds" / (names[f] + ".raw")).read_bytes()
        return cache[f]
    return sample


def writes(script):
    r = subprocess.run([str(GOLD), "--audio-dump", str(ASSETS), str(script)], capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stderr
    out = []
    for line in r.stdout.splitlines():
        tick, op, voice, obj, blank, value, off, ln, clock = (int(x) for x in line.split())
        out.append((tick - 1, op, voice, obj, blank, value, off, ln, clock))
    return out


def model_left(cmds, nframes, sample):
    """Voices 0 and 3 (the left channel) from the register writes alone, written from the hardware rules of note 10 (1.4, 4.2, 9.2) and not from the mixer's code. Time is in units of one
    colour clock of the port's 3,552,550 a second / RATE, so everything is an integer: a write is made at (its blank x 71,051 + its clock) x RATE (step k starts at blank floor(25 k / 4)), the
    output frame f is taken at f x 3,552,550, a sample lasts the period register's value at the moment it starts x RATE (the counter is reloaded at each sample's end), a block of n bytes ends
    after n samples and then the voice restarts the block LATCHED by then (a latch written at that end comes after it). At one instant: the samples that end, then the writes in order, then the
    start of the new samples (their period is read last)."""
    PORT = 3552550
    ev = []
    for step, op, voice, obj, blank, value, off, ln, clock in cmds:
        if voice in (0, 3):
            ev.append(((step * 25 // 4 + blank) * 71051 + clock, len(ev), op, voice, obj, value, off, ln))
    ev.sort()                                         # by time, then in the order the library made them
    v = {i: dict(dma=False, lat=None, blk=None, idx=0, edge=None, load=None, stop=False, period=0, vol=0) for i in (0, 3)}
    data = {}
    e = 0
    out = []
    INF = float("inf")
    for f in range(nframes):
        T = f * PORT
        while True:
            # the next thing that happens, at or before T
            t_edge = min((s["edge"] for s in v.values() if s["edge"] is not None), default=INF)
            t_load = min((s["load"] for s in v.values() if s["load"] is not None), default=INF)
            t_cmd = ev[e][0] * RATE if e < len(ev) else INF
            if t_edge <= T and t_edge <= t_cmd and t_edge <= t_load:
                s = next(s for s in v.values() if s["edge"] == t_edge)
                s["edge"] = None
                if s["stop"]:
                    s["blk"] = None; s["stop"] = False
                    continue
                s["idx"] += 1
                if s["idx"] >= s["blk"][2]:                # the block ends: the voice restarts the block latched by now
                    s["blk"] = s["lat"] if (s["dma"] and s["lat"]) else None
                    s["idx"] = 0
                if s["blk"]:
                    s["load"] = t_edge
            elif t_cmd <= T and t_cmd <= t_load:
                _, _, op, voice, obj, value, off, ln = ev[e]
                e += 1
                s = v[voice]
                if op == 1:
                    s["lat"] = (obj, off, ln)
                elif op == 2 and not s["dma"]:
                    s["dma"] = True
                    s["blk"], s["idx"], s["stop"] = s["lat"], 0, False
                    s["edge"] = None
                    s["load"] = t_cmd if s["blk"] else None
                elif op == 3 and s["dma"]:
                    s["dma"] = False
                    if s["load"] is not None:
                        s["blk"], s["load"] = None, None       # the sample had not begun
                    elif s["blk"]:
                        s["stop"] = True                       # the sample in progress finishes
                elif op == 4:
                    s["period"] = value
                elif op == 5:
                    s["vol"] = min(value, 64)
            elif t_load <= T:
                s = next(s for s in v.values() if s["load"] == t_load)
                s["edge"] = t_load + max(s["period"], 124) * RATE
                s["load"] = None
            else:
                break
        acc = 0
        for s in v.values():
            if s["blk"]:
                obj, off, ln = s["blk"]
                if obj not in data:
                    data[obj] = sample(obj)
                b = data[obj][off + s["idx"]]
                acc += (b - 256 if b > 127 else b) * s["vol"] * 2
        out.append(acc)
    return out


@needs_assets
@pytest.mark.parametrize("script,extra", [
    ("start deck\nseed 1\nticks 80\nat 1 kp 11\n", []),
    ("start deck\nseed 1\nticks 80\nat 1 kp 11\nat 20 fkey 10\n", ["--key", "F10@19"]),   # the throttle preset: the period glides
])
def test_engine_matches_the_register_writes(tmp_path, script, extra):
    sp = tmp_path / "s.txt"
    sp.write_text(script)
    left = read_wav(wav_run(tmp_path, "e.wav", "--no-filter", *extra))[0::2].tolist()
    model = model_left(writes(sp), len(left), sound_files())
    assert any(model)
    assert left == model, next(i for i, (a, b) in enumerate(zip(left, model)) if a != b)      # frame for frame: both are integer models of the same hardware


# The device tests use SDL's dummy audio driver (SDL_AUDIODRIVER=dummy in ENV: it opens a device that consumes the samples in real time, so the live path with its callback runs without
# hardware). The synthetic ones (--synthetic: the invented world of tests/world_synth.h with its invented sound table and samples, no game data) need only the built binary and run in CI;
# the real-asset ones need the extracted assets and skip where they are absent.
SYN_STEPS = 40


def synthetic_device_run(extra=(), env=ENV):
    return subprocess.run([str(EXE), "--synthetic", "--flight", "--frames", str(SYN_STEPS), *extra], capture_output=True, text=True, env=env, timeout=120)


@needs_exe
def test_synthetic_device_run_writes_the_registers_and_exits_zero(tmp_path):
    """Review Focus 4 (milestone 7): where there are no assets the synthetic start opens the dummy device, the mixer runs and the run ends cleanly."""
    log = tmp_path / "syn.log"
    r = synthetic_device_run(["--audio-log", str(log)])
    assert r.returncode == 0, r.stderr
    assert not [l for l in r.stderr.splitlines() if l.startswith("audio:") and "underrun" not in l], r.stderr   # the device opened and every sample object was read from the invented files (an underrun under load is not a failure)
    cmds = parse_log(log)
    assert len(cmds) > 20
    assert {op for _, op, *_ in cmds} >= {1, 4, 5}            # latches, periods and volumes: the sound library ran
    assert max(t for t, *_ in cmds) < SYN_STEPS


@needs_exe
def test_synthetic_run_is_deterministic(tmp_path):
    logs = []
    for n in ("a", "b"):
        log = tmp_path / (n + ".log")
        r = synthetic_device_run(["--audio-log", str(log), "--seed", "1"])
        assert r.returncode == 0, r.stderr
        logs.append(log.read_bytes())
    assert logs[0] == logs[1] and logs[0]


@needs_exe
def test_synthetic_wav_has_the_length_of_the_steps(tmp_path):
    out = tmp_path / "s.wav"
    r = subprocess.run([str(EXE), "--synthetic", "--flight", "--seed", "1", "--frames", "20", "--no-audio", "--wav", str(out)], capture_output=True, text=True, env=ENV, timeout=120)
    assert r.returncode == 0, r.stderr
    s = read_wav(out)
    assert len(s) == 20 * 25 // 4 * 882 * 2 and any(v != 0 for v in s)


@needs_exe
def test_synthetic_run_without_a_device_driver_is_silent_not_fatal():
    r = synthetic_device_run(env=dict(ENV, SDL_AUDIODRIVER="nonexistent-driver"))
    assert r.returncode == 0, r.stderr


@needs_assets
def test_device_run_exits_zero():
    r = subprocess.run([str(EXE), "--flight", "--frames", "20"], capture_output=True, text=True, env=ENV, timeout=120)
    assert r.returncode == 0, r.stderr


@needs_assets
def test_no_device_driver_is_silent_not_fatal():
    env = dict(ENV, SDL_AUDIODRIVER="nonexistent-driver")
    r = subprocess.run([str(EXE), "--flight", "--frames", "5"], capture_output=True, text=True, env=env, timeout=120)
    assert r.returncode == 0, r.stderr


@needs_assets
def test_short_sample_file_plays_nothing(tmp_path):
    """Review Focus 2: an assets directory whose engine sample is cut short: one line on stderr, the run goes on."""
    import shutil
    d = tmp_path / "assets"
    shutil.copytree(ASSETS, d)
    names = (d / "sounds" / "index.txt").read_text().split()
    p = d / "sounds" / (names[2] + ".raw")
    p.write_bytes(p.read_bytes()[:100])
    out = tmp_path / "s.wav"
    r = subprocess.run([str(EXE), "--assets", str(d), "--flight", "--seed", "1", "--frames", "10", "--no-audio", "--wav", str(out)],
                       capture_output=True, text=True, env=ENV, timeout=120)
    assert r.returncode == 0, r.stderr
    assert "shorter than" in r.stderr
    assert len(read_wav(out)) == 10 * 25 // 4 * 882 * 2


def parse_log(path):
    out = []
    for line in Path(path).read_text().splitlines():
        tick, op, voice, obj, blank, value, off, ln, clock = (int(x) for x in line.split())
        out.append((tick - 1, op, voice, obj, blank, value, off, ln, clock))
    return out


@needs_assets
def test_the_cockpit_start_latches_the_engine_pair_and_matches_the_register_writes(tmp_path):
    """The screens' own way into free flight (credits, menu, location, aircraft; the approach, the notice, the wait) with the cockpit view: the pending notice 0xFE plays the external pair at 776/778
    in the first ungated pass (note 10 5.7 item 10: volume 12) and the cockpit pair at 764/766, volume 40, the pass after the view change; the wind is never glided into the engine."""
    frames = 330
    wav = tmp_path / "k.wav"
    log = tmp_path / "k.log"
    r = subprocess.run([str(EXE), "--start-keys", "--cockpit", "--seed", "1", "--frames", str(frames), "--no-audio", "--no-filter", "--wav", str(wav), "--audio-log", str(log)],
                       capture_output=True, text=True, env=ENV, timeout=300)
    assert r.returncode == 0, r.stderr
    cmds = parse_log(log)
    latches = [(t, v, o) for t, op, v, o, b, val, off, ln, ck in cmds if op == 1 and v in (0, 1) and o in (0, 1, 2, 3)]
    assert [o for _, _, o in latches[:4]] == [2, 3, 0, 1], latches[:6]          # the external pair, then the cockpit pair, in the passes after the approach
    t_ext, t_cp = latches[0][0], latches[2][0]
    assert t_cp == t_ext + 1
    per = {}
    vol = {}
    for t, op, v, o, b, val, off, ln, ck in cmds:
        if v in (0, 1) and t_ext <= t <= t_cp:
            if op == 4 and val != 124:
                per[(t, v)] = val
            if op == 5 and val:
                vol[(t, v)] = val
    assert per[(t_ext, 0)] == 776 and per[(t_ext, 1)] == 778 and vol[(t_ext, 0)] == 12 and vol[(t_ext, 1)] == 12
    assert per[(t_cp, 0)] == 764 and per[(t_cp, 1)] == 766 and vol[(t_cp, 0)] == 40 and vol[(t_cp, 1)] == 40
    # the engine is never the wind object glided: from the cockpit latch on, voice 1's period stays 2 above voice 0's
    last = {}
    for t, op, v, o, b, val, off, ln, ck in cmds:
        if t_cp <= t < t_cp + 40 and op == 4 and v in (0, 1) and val != 124:
            last[v] = val
            if len(last) == 2:
                assert last[1] - last[0] in (0, 2), (t, last)
    left = read_wav(wav)[0::2].tolist()
    assert len(left) == frames * 25 // 4 * 882
    model = model_left(cmds, len(left), sound_files())
    assert any(model)
    assert left == model, next(i for i, (a, b) in enumerate(zip(left, model)) if a != b)


@needs_assets
def test_a_missing_cockpit_engine_sample_makes_the_game_take_the_external_pair_at_a_quarter_volume(tmp_path):
    """Note 10 2.5: the loader leaves out an object whose file could not be read, its enable bit stays clear and the cockpit engine call plays the external pair at a quarter of the volume. The
    frontend reports the missing objects to the core before the first pass (game_sound_objects_available)."""
    import shutil
    table = (ASSETS / "tables" / "sounds.bin").read_bytes()
    names = (ASSETS / "sounds" / "index.txt").read_text().split()
    cockpit_file = names[table[0]]
    d = tmp_path / "assets"
    shutil.copytree(ASSETS, d)
    (d / "sounds" / (cockpit_file + ".raw")).unlink()
    logs = {}
    for tag, assets in (("full", ASSETS), ("cut", d)):
        wav, log = tmp_path / (tag + ".wav"), tmp_path / (tag + ".log")
        r = subprocess.run([str(EXE), "--assets", str(assets), "--flight", "--cockpit", "--seed", "1", "--frames", "40", "--no-audio", "--wav", str(wav), "--audio-log", str(log)],
                           capture_output=True, text=True, env=ENV, timeout=120)
        assert r.returncode == 0, r.stderr
        logs[tag] = (parse_log(log), r.stderr)
    assert "cannot be read" in logs["cut"][1]
    def engine(cmds):
        lat = [(t, v, o) for t, op, v, o, b, val, off, ln, ck in cmds if op == 1 and v in (0, 1) and o in (0, 1, 2, 3)]
        first = next(i for i, c in enumerate(cmds) if c[1] == 1 and c[2] == 0 and c[3] in (0, 1, 2, 3))
        vol = [c[5] for c in cmds[first:] if c[1] == 5 and c[2] == 0 and c[5]]      # the volumes written from the first engine latch on (the start-up music came before)
        return lat, vol
    full_lat, full_vol = engine(logs["full"][0])
    cut_lat, cut_vol = engine(logs["cut"][0])
    assert {o for _, _, o in full_lat} == {0, 1}
    assert {o for _, _, o in cut_lat} == {2, 3}
    assert cut_vol[0] == full_vol[0] // 4
