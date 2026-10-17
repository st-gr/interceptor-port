# tests/frontend/test_audio.py - the Paula-like mixer (src/frontend/sdl/audio.c, docs/re/10-sound.md sections 1.2-1.5, 4.2, 9.2) through build/audio_driver.
# The mixer takes the register writes of the sound library (latch, DMA on and off, period, volume), stamped with their blank. Everything here is invented data: the sample patterns
# are made up, the expectations are computed in this file from the note's rules (zero-order hold at 3,546,895 / period samples a second, volume 0..64 linear, output = sample x
# volume x 2, voices 0 and 3 left and 1 and 2 right, a block restarts the block latched by its end while DMA is on, a write applies when the output time reaches its blank, blank n
# at n x 71,051 colour clocks) and, for the filter, from the RC values of vAmiga's A500 chain written out independently of the C code.
import math
import subprocess
from fractions import Fraction
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "build" / "audio_driver"

pytestmark = pytest.mark.skipif(not DRIVER.exists(), reason="build/audio_driver not built")

CLOCK = 3546895
PAT = [10, -20, 30, -40, 50, -60, 70, -80, 90, -100, 110, -120, 127, -128, 5, -5]      # invented
PAT2 = [-7, 14, -21, 28, -35, 42, -49, 56, -63, 70, -77, 84]                              # invented


def run(script):
    r = subprocess.run([str(DRIVER)], input=script, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return r


def frames(r, channels=2):
    rows = [l.split() for l in r.stdout.splitlines() if not l.startswith("level")]
    return [tuple(int(x) for x in row) for row in rows]


def sample_cmd(obj, data):
    return f"sample {obj} {len(data)} " + " ".join(str(b) for b in data) + "\n"


def start(voice, obj, vol, period, off, length):
    """the register writes of a start by the library: latch the block, DMA on, period, volume"""
    return f"latch {voice} {obj} {off} {length}\ndmaon {voice}\nperiod {voice} {period}\nvolume {voice} {vol}\n"


def frame_of(blank, rate=44100):
    """the output frame at which blank n begins: n x 71,051 colour clocks of output time (note 10 9.2)"""
    return -(-blank * 71051 * rate // CLOCK)


def inc_fixed(period, rate):
    return (CLOCK << 32) // (max(period, 124) * rate)


def hold(data, n, period, rate, start=0):
    """Index sequence by the exact rational (no fixed point): the value at frame i is data[floor(i x clock / (period x rate)) mod len]."""
    return [data[int(Fraction((start + i) * CLOCK, period * rate)) % len(data)] for i in range(n)]


def play(data, writes, n, rate=44100, clock=CLOCK, start_t=0):
    """One voice computed from the hardware rule alone, in exact rationals (units of colour clock / rate): a sample lasts the period register's value at the moment it starts
    (note 10 9.2), a write at time T is seen by the samples that start at T or later, the output at frame i is the sample in progress at i x clock units. `writes` is a list of
    (time in units, period); the first sample starts at start_t. Returns the raw sample values (0 before the start)."""
    per = 428
    t = Fraction(start_t)
    idx = 0
    starts = []                 # (start, index)
    ws = sorted(writes)
    while t < (n + 2) * clock:
        for w, p in ws:
            if w <= t:
                per = p
        starts.append((t, idx % len(data)))
        t += max(per, 124) * rate
        idx += 1
    out = []
    j = 0
    for i in range(n):
        ft = i * clock
        while j + 1 < len(starts) and starts[j + 1][0] <= ft:
            j += 1
        out.append(data[starts[j][1]] if ft >= start_t else 0)
    return out


def test_one_voice_period_428_zero_order_hold():
    # 8,287 Hz: 3,546,895 / 428 = 8,287.1; at 44,100 Hz each source sample lasts 5.32 output frames
    n = 600
    out = frames(run("filter 0\n" + sample_cmd(0, PAT) + start(0, 0, 63, 428, 0, len(PAT)) + f"apply\nrender {n}\n"))
    assert len(out) == n
    assert [l for l, r in out] == [s * 63 * 2 for s in hold(PAT, n, 428, 44100)]
    assert all(r == 0 for l, r in out)
    # zero-order hold: the first source sample is held for 5 or 6 frames, nothing in between
    assert out[0][0] == out[4][0] == 10 * 126
    assert out[6][0] == -20 * 126 and out[5][0] in (10 * 126, -20 * 126)


def test_volume_is_linear():
    out = frames(run("filter 0\n" + sample_cmd(0, PAT) + start(0, 0, 16, 428, 0, len(PAT)) + "apply\nrender 20\n"))
    assert [l for l, r in out] == [s * 16 * 2 for s in hold(PAT, 20, 428, 44100)]


def test_the_volume_register_is_played_as_written_up_to_64():
    # the mixer has no master input: the library caps the volumes it writes (10 section 9.2); the hardware plays 64 as 64 and clamps what is above
    out = frames(run("filter 0\n" + sample_cmd(0, [100] * 16) + start(0, 0, 64, 428, 0, 16) + "apply\nrender 5\n"))
    assert out[0] == (100 * 64 * 2, 0)
    out = frames(run("filter 0\n" + sample_cmd(0, [100] * 16) + start(0, 0, 200, 428, 0, 16) + "apply\nrender 5\n"))
    assert out[0] == (100 * 64 * 2, 0)


def test_voices_left_and_right_pairing():
    n = 500
    script = "filter 0\n" + sample_cmd(0, PAT) + sample_cmd(1, PAT2)
    script += start(0, 0, 63, 428, 0, 16) + start(3, 1, 32, 500, 0, 12) + start(1, 0, 48, 300, 0, 16) + start(2, 1, 20, 428, 0, 12) + f"apply\nrender {n}\n"
    out = frames(run(script))
    a428, a500, a300 = hold(PAT, n, 428, 44100), hold(PAT2, n, 500, 44100), hold(PAT, n, 300, 44100)
    b428 = hold(PAT2, n, 428, 44100)
    for i, (l, r) in enumerate(out):
        assert l == a428[i] * 63 * 2 + a500[i] * 32 * 2      # voices 0 and 3
        assert r == a300[i] * 48 * 2 + b428[i] * 20 * 2      # voices 1 and 2


def test_each_voice_alone_goes_to_its_side():
    for voice, side in ((0, 0), (1, 1), (2, 1), (3, 0)):
        out = frames(run("filter 0\n" + sample_cmd(0, PAT) + start(voice, 0, 63, 428, 0, 16) + "apply\nrender 100\n"))
        assert any(o[side] for o in out) and not any(o[1 - side] for o in out), voice


def test_the_librarys_stop_is_silent_at_once():
    # volume 0, period 0x7C, DMA off, in that order (10 4.1), written at the start of the second step (blank 6): the volume register silences the voice before the DMA bit goes
    t = frame_of(6)
    script = "filter 0\n" + sample_cmd(0, PAT) + start(0, 0, 63, 428, 0, 16) + f"apply\nvolume 0 0\nperiod 0 124\ndmaoff 0\napply\nrender {t + 100}\n"
    out = frames(run(script))
    assert [l for l, r in out[:t]] == [s * 126 for s in hold(PAT, t, 428, 44100)]
    assert all(o == (0, 0) for o in out[t:])
    assert any(l for l, r in out[:t])


def test_dma_off_alone_ends_at_the_end_of_the_current_sample():
    t = frame_of(6)
    inc = inc_fixed(428, 44100)
    out = [l for l, r in frames(run("filter 0\n" + sample_cmd(0, PAT) + start(0, 0, 63, 428, 0, 16) + f"apply\ndmaoff 0\napply\nrender {t + 100}\n"))]
    cur = (t * inc) >> 32
    k = 0
    while ((t + k) * inc) >> 32 == cur:
        k += 1                      # frames of the current sample still to come
    assert k > 1
    assert out[t:t + k] == [PAT[cur % 16] * 126] * k
    assert all(v == 0 for v in out[t + k:])


def test_period_write_is_read_at_the_next_sample_start_and_the_volume_at_once():
    # the hardware reloads the period counter at each sample's end: a period written in the middle of a sample does not rescale it (note 10 9.2). The write is at blank 6 (frame
    # 5,300.4, inside a sample of 5.32 frames); the volume applies at the next output frame
    t = frame_of(6)
    script = "filter 0\n" + sample_cmd(0, PAT) + start(0, 0, 63, 428, 0, 16) + f"apply\nperiod 0 300\nvolume 0 16\napply\nrender {t + 200}\n"
    out = [l for l, r in frames(run(script))]
    ref = play(PAT, [(Fraction(6 * 71051 * 44100), 300)], t + 200)
    assert out == [v * 126 for v in ref[:t]] + [v * 32 for v in ref[t:]]
    # the sample in progress at the write kept its 428 clocks: it is held to the end of its 5.32 frames
    plain = play(PAT, [], t + 200)
    assert ref[t] == plain[t]


def test_period_below_the_hardware_minimum_is_124():
    a = frames(run("filter 0\n" + sample_cmd(0, PAT) + start(0, 0, 63, 50, 0, 16) + "apply\nrender 200\n"))
    b = frames(run("filter 0\n" + sample_cmd(0, PAT) + start(0, 0, 63, 124, 0, 16) + "apply\nrender 200\n"))
    assert a == b


def test_a_block_restarts_the_block_latched_by_its_end():
    # block A (bytes 0..8) plays, B (8..12) is latched meanwhile (written after the DMA on, in the same blank): A ends, B starts at its first byte and loops for ever while DMA stays on
    n = 800
    script = "filter 0\n" + sample_cmd(3, PAT) + start(0, 3, 63, 428, 0, 8) + "latch 0 3 8 4\napply\nrender %d\n" % n
    out = [l for l, r in frames(run(script))]
    inc = inc_fixed(428, 44100)
    exp = []
    for i in range(n):
        idx = (i * inc) >> 32
        exp.append(PAT[idx] if idx < 8 else PAT[8 + (idx - 8) % 4])
    assert [e * 126 for e in exp] == out
    assert set(PAT[8:12]) <= set(exp) and exp[-1] in PAT[8:12]
    assert PAT[0] in exp and PAT[4] in exp          # the first block really played before the loop


def test_the_same_latch_is_replayed_from_its_first_byte_while_dma_is_on():
    n = 600
    out = [l for l, r in frames(run("filter 0\n" + sample_cmd(0, PAT) + start(0, 0, 63, 428, 4, 6) + f"apply\nrender {n}\n"))]
    inc = inc_fixed(428, 44100)
    assert out == [PAT[4 + ((i * inc) >> 32) % 6] * 126 for i in range(n)]


def test_dma_off_then_on_restarts_the_voice():
    t = frame_of(6)
    script = "filter 0\n" + sample_cmd(0, PAT) + start(0, 0, 63, 428, 0, 16) + f"apply\ndmaoff 0\ndmaon 0\napply\nrender {t + 100}\n"
    out = [l for l, r in frames(run(script))]
    tw = Fraction(6 * 71051 * 44100)       # the restarted block's first sample begins at the exact time of the blank
    assert out[t:t + 100] == [v * 126 for v in play(PAT, [], t + 100, start_t=tw)[t:t + 100]]


def test_a_write_applies_when_the_output_time_reaches_its_blank():
    # blank n starts at n x 71,051 colour clocks: frame ceil(n x 71,051 x rate / 3,546,895); 44,100 Hz gives 883.4 frames a blank. The step pattern is 6, 6, 6, 7 blanks:
    # the steps 0..4 start at the blanks 0, 6, 12, 18, 25
    def first_sound(step, blank, rate=44100):
        script = f"rate {rate}\nfilter 0\n" + sample_cmd(0, [100] * 16) + "".join("apply\n" for _ in range(step))
        script += f"at {blank}\n" + start(0, 0, 63, 428, 0, 16) + f"apply\nrender {int(40 * rate / 50 * 1.0) + 4000}\n"
        out = frames(run(script))
        return next(i for i, o in enumerate(out) if o[0])
    for rate in (44100, 48000):
        def frame_of(b):
            return -(-b * 71051 * rate // CLOCK)
        # the steps before were applied empty, so the first render frame is 0 and the write lands at its frame
        assert first_sound(0, 0, rate) == frame_of(0)
        assert first_sound(0, 3, rate) == frame_of(3)
        assert first_sound(1, 0, rate) == frame_of(6)
        assert first_sound(3, 0, rate) == frame_of(18)
        assert first_sound(4, 0, rate) == frame_of(25)           # the fourth step has 7 blanks
        assert first_sound(4, 2, rate) == frame_of(27)


def test_writes_of_one_blank_apply_in_order():
    # a latch, DMA on and a volume in one blank, the volume written twice: the last value stays
    out = frames(run("filter 0\n" + sample_cmd(0, [100] * 16) + "latch 0 0 0 16\ndmaon 0\nperiod 0 428\nvolume 0 10\nvolume 0 20\napply\nrender 5\n"))
    assert out[0] == (100 * 20 * 2, 0)


@pytest.mark.parametrize("rate", [44100, 48000, 22050])
def test_rates_keep_the_source_speed(rate):
    n = 700
    out = frames(run(f"rate {rate}\nfilter 0\n" + sample_cmd(0, PAT) + start(0, 0, 63, 428, 0, 16) + f"apply\nrender {n}\n"))
    assert [l for l, r in out] == [s * 126 for s in hold(PAT, n, 428, rate)]


def test_rate_48000_and_mono():
    n = 800
    out = frames(run("rate 48000\nfilter 0\n" + sample_cmd(0, PAT) + start(0, 0, 63, 428, 0, 16) + "apply\nrender %d\n" % n))
    assert [l for l, r in out] == [s * 126 for s in hold(PAT, n, 428, 48000)]
    assert hold(PAT, n, 428, 48000) != hold(PAT, n, 428, 44100)[:n]
    # mono: the average of left and right; voice 0 alone is half
    mono = frames(run("rate 22050\nchannels 1\nfilter 0\n" + sample_cmd(0, PAT) + start(0, 0, 63, 428, 0, 16) + "apply\nrender 300\n"))
    assert [m[0] for m in mono] == [(s * 126) >> 1 for s in hold(PAT, 300, 428, 22050)]
    # voices 0 and 1 together (the engine pair, the same data): mono is the common value
    mono = frames(run("channels 1\nfilter 0\n" + sample_cmd(0, PAT) + start(0, 0, 63, 428, 0, 16) + start(1, 0, 63, 428, 0, 16) + "apply\nrender 100\n"))
    assert [m[0] for m in mono] == [s * 126 for s in hold(PAT, 100, 428, 44100)]


def test_missing_sample_or_range_past_the_buffer_is_ignored_and_reported_once():
    script = "filter 0\n" + sample_cmd(1, PAT)
    script += start(0, 5, 63, 428, 0, 16) + start(0, 5, 63, 428, 0, 16) + "apply\n"            # object 5 missing
    script += start(1, 1, 63, 428, 0, 17) + start(1, 1, 63, 428, 0, 17) + "apply\n"            # range past the end
    script += start(2, 1, 63, 428, 8, 16) + "apply\n"                                          # range past the end from an offset
    script += start(3, 99, 63, 428, 0, 16) + "apply\n"                                         # no such object
    script += start(3, 1, 63, 428, 0, 0) + "apply\nrender 40000\n"                           # empty block
    r = run(script)
    assert all(o == (0, 0) for o in frames(r))
    err = r.stderr.splitlines()
    assert sum("object 0x5" in l for l in err) == 1
    assert sum("object 0x1 " in l for l in err) == 1          # once per object, not per kind of fault
    assert any("object 0x63" in l for l in err)
    assert all(l.startswith("audio: LATCH") for l in err)


def test_a_rejected_latch_lets_the_block_under_way_finish_and_then_silences_the_voice():
    big = [(i * 7 % 200) - 100 for i in range(2000)]                    # a block of 2,000 samples lasts 10,645 frames at period 428
    t = frame_of(6)
    n = 14000
    script = "filter 0\n" + sample_cmd(0, big) + start(0, 0, 63, 428, 0, 2000) + "apply\nlatch 0 7 0 16\napply\nrender %d\n" % n
    r = run(script)
    out = [l for l, _ in frames(r)]
    inc = inc_fixed(428, 44100)
    block_frames = -(-(2000 << 32) // inc)
    assert block_frames > t + 100
    assert out[:block_frames - 1] == [big[(i * inc) >> 32] * 126 for i in range(block_frames - 1)]      # the invalid latch at blank 6 does not interrupt the block under way
    assert all(v == 0 for v in out[block_frames + 2:])
    assert "object 0x7" in r.stderr


# --- the filter: the A500 chain of vAmiga (AudioFilter.cpp), written out here from the R and C values ---

def chain(xs, rate):
    """lo: one-pole 1/(2 pi 360 x 1e-7) Hz; LED: two-pole, R 10k/10k, C 6.8n/3.9n; hi: one-pole high-pass 1/(2 pi 1390 x 2.233e-5) Hz; in that order."""
    def one(cut):
        a = 2.0 - math.cos(2 * math.pi * cut / rate)
        b = a - math.sqrt(a * a - 1.0)
        return 1.0 - b, b
    lo_a1, lo_a2 = one(1 / (2 * math.pi * 360 * 1e-7))
    hi_a1, hi_a2 = one(1 / (2 * math.pi * 1390 * 2.233e-5))
    r1 = r2 = 10000.0
    c1, c2 = 6.8e-9, 3.9e-9
    cut = 1 / (2 * math.pi * math.sqrt(r1 * r2 * c1 * c2))
    q = math.sqrt(r1 * r2 * c1 * c2) / (c2 * (r1 + r2))
    a = 1 / math.tan(2 * math.pi * cut / rate)
    b = 1 / q
    k1 = 1 / (1 + b * a + a * a)
    k2 = 2 * k1
    j1 = 2 * (1 - a * a) * k1
    j2 = (1 - b * a + a * a) * k1
    lo = hp = 0.0
    x0 = x1 = y0 = y1 = 0.0
    out = []
    for x in xs:
        lo = lo_a1 * x + lo_a2 * lo
        y = k1 * lo + k2 * x0 + k1 * x1 - j1 * y0 - j2 * y1
        x1, x0, y1, y0 = x0, lo, y0, y
        hp = hi_a1 * y + hi_a2 * hp
        out.append(y - hp)
    return out


def test_filter_constants_of_the_reference():
    # the cut-offs written in the comment of audio.c: 4,420.97 Hz, 3,090.5 Hz with Q 0.6602 and 5.13 Hz
    assert abs(1 / (2 * math.pi * 360 * 1e-7) - 4420.97) < 0.01
    assert abs(1 / (2 * math.pi * math.sqrt(1e8 * 6.8e-9 * 3.9e-9)) - 3090.5) < 0.1
    assert abs(math.sqrt(1e8 * 6.8e-9 * 3.9e-9) / (3.9e-9 * 2e4) - 0.6602) < 1e-3
    assert abs(1 / (2 * math.pi * 1390 * 2.233e-5) - 5.13) < 0.01


def test_filter_is_on_by_default_and_off_on_request():
    base = sample_cmd(0, [100] * 16) + start(0, 0, 63, 428, 0, 16) + "apply\nrender 50\n"
    on = frames(run(base))
    off = frames(run("filter 0\n" + base))
    assert off[0] == (12600, 0) and on[0] != off[0]
    assert frames(run("filter 1\n" + base)) == on


def test_filter_step_response_and_decay():
    n = 4410
    out = frames(run(sample_cmd(0, [100] * 16) + start(0, 0, 63, 428, 0, 16) + "apply\nrender %d\n" % n))
    left = [l for l, r in out]
    ref = chain([12600.0] * n, 44100)
    assert all(abs(a - round(b)) <= 1 for a, b in zip(left, ref))
    assert all(r == 0 for l, r in out)
    # the documented shape: a step of 12,800 rises through the two low-pass stages within a handful of frames (0 at the first sample is
    # impossible: the first output is the sum of the stage gains) and then decays by the high-pass with tau = 1 / (2 pi 5.13 Hz) = 31 ms
    assert 0 < left[0] < left[1] < left[2] < left[6]
    peak = max(left[:20])
    assert 11000 < peak < 14000
    tail = left[100:]
    assert all(a >= b for a, b in zip(tail, tail[1:]))               # monotone decay
    tau = 1 / (2 * math.pi * 5.13)
    want = 12600 * math.exp(-(n - 1) / 44100 / tau)
    assert abs(left[-1] - want) < 0.1 * want + 5                     # about 12,800 x e^(-3.2) = 520 after 0.1 s




# --- the time of a block end's writes: inside the blank, at the clock where the block ended (note 10 4.2 and 9.2) ---

def test_a_block_ends_before_the_writes_of_its_own_end_take_effect():
    # four objects of 400 samples with different values, period 200: a block lasts 80,000 colour clocks, 1.126 blanks. The library's writes: at the start the first block, DMA on, the second block
    # (the DMA-start request); at each block end the interrupt latches the block after the next. The first end is at clock 80,000 = blank 1 + 8,949, the second at blank 2 + 17,898, the third at blank 3 + 26,847.
    # The writes of an end must not take effect before that end: the voice plays 1, 2, 3, 4 and not 1, 3, 4 (the writes applied at the blank's start would replace the block that restarts)
    objs = "".join(sample_cmd(i, [10 * i] * 400) for i in (1, 2, 3, 4))
    script = "filter 0\n" + objs
    script += "latch 0 1 0 400\ndmaon 0\nlatch 0 2 0 400\nperiod 0 200\nvolume 0 63\n"
    for blank, clock, obj in ((1, 8949, 3), (2, 17898, 4)):
        script += f"at {blank}\natc {clock}\nlatch 0 {obj} 0 400\n"
    script += "apply\nrender 20000\n"
    out = [l // 126 for l, r in frames(run(script))]
    blk = lambda f: int(Fraction(f * CLOCK, 200 * 44100)) // 400          # the block index at frame f
    exp = [10 * min(blk(f) + 1, 4) for f in range(20000)]    # the fourth block stays latched and repeats
    assert exp[0] == 10 and exp[-1] == 40
    assert out == exp
    # the same writes all stamped at the start of their blanks (the old contract) skip block 2: the test would have caught it
    script2 = script.replace("atc 8949", "atc 0").replace("atc 17898", "atc 0")
    bad = [l // 126 for l, r in frames(run(script2))]
    assert bad != exp and 20 not in bad[1000:]


def test_the_last_block_plays_to_its_end_and_its_dma_off_is_at_the_end():
    # one block of 400 at period 200 (the ground contact's shape: count 1): the second load is the same block, the handler at the end stops the voice (volume 0, period 0x7C, DMA off, at the end's clock)
    script = "filter 0\n" + sample_cmd(1, [50] * 400)
    script += "latch 0 1 0 400\ndmaon 0\nperiod 0 200\nvolume 0 63\n"
    script += "at 1\natc 8949\nvolume 0 0\nperiod 0 124\ndmaoff 0\napply\nrender 4000\n"
    out = [l for l, r in frames(run(script))]
    end = -(-80000 * 44100 // CLOCK)                  # the first frame at or after the end of the block (80,000 clocks)
    assert all(v == 50 * 126 for v in out[:end]) and all(v == 0 for v in out[end:])


# --- the live clock: the device runs by itself, the game's steps arrive on the wall clock (note 10 9.2) ---

PORT = 3552550
STEP_S = 0.125


def live_script(t0, seconds, clicks, drift=0.0, stall=None, quiet_after=None):
    """A simulated session: callbacks of 1,024 frames at 44,100 Hz on the device's clock, steps on the wall clock t0 + k x 0.125 s x (1 + drift). Each click (given as a step number) is a
    4-blank tone: latch, DMA on, period and volume at its blank 0, volume 0 and DMA off at its blank 4. stall = (first step, last step, resume time): the steps in between come all at once."""
    ev = []
    ncb = int(seconds * 44100 / 1024)
    for n in range(ncb):
        ev.append((n * 1024 / 44100, 0, "runs 1024\n"))
    nsteps = int((seconds - t0) / STEP_S / (1 + drift))
    for k in range(nsteps):
        t = t0 + k * STEP_S * (1 + drift)
        body = ""
        if k in clicks:
            body = "latch 0 1 0 16\ndmaon 0\nperiod 0 124\nvolume 0 63\nat 4\nvolume 0 0\ndmaoff 0\n"
        if stall and stall[0] <= k <= stall[1]:
            t = stall[2]
        ev.append((t, 1, body + "apply\n"))
    ev.sort(key=lambda e: (e[0], e[1], ))
    return "clock %d\nlive 1\nfilter 0\n" % PORT + sample_cmd(1, [100] * 16) + "".join(e[2] for e in ev) + "flush\nstats\n"


def runs_of(r):
    return [(int(l.split()[1]), int(l.split()[2])) for l in r.stdout.splitlines() if l.startswith("run ")]


def stats_of(r):
    l = next(l for l in r.stdout.splitlines() if l.startswith("stats"))
    return dict(zip(("pending", "underruns", "merged", "trim", "lead"), (int(x) for x in l.split()[1:])))


BLANK4 = 4 * 71051 * 44100 / PORT           # 3,528 frames: four blanks of the port's clock
TOL = 0.006 * BLANK4                        # the clock trim is at most 0.5 % (the drift of the device and the game clocks, applied smoothly)


@pytest.mark.parametrize("t0", [0.0, 0.03, 0.1, 0.3])
def test_a_four_blank_click_lasts_four_blanks_whenever_the_game_starts(t0):
    r = run(live_script(t0, 3.0, {0, 8}))
    runs = runs_of(r)
    assert len(runs) == 2
    for first, length in runs:
        assert abs(length - BLANK4) <= TOL, (t0, runs)
    st = stats_of(r)
    assert st["underruns"] == 0 and st["merged"] == 0


@pytest.mark.parametrize("drift", [0.002, -0.002, 0.0016])
def test_the_queue_stays_bounded_and_every_click_plays_over_ten_minutes(drift):
    # the game's clock against the device's 0.2 % either way (and the machine's 49.92 against 50 Hz): the lead stays near its three blanks (the trim of the clock), nothing is dropped
    clicks = set(range(8, 4800, 80))
    r = run(live_script(0.2, 600.0, clicks, drift))
    runs = runs_of(r)
    st = stats_of(r)
    assert len(runs) == len(clicks)
    assert all(abs(length - BLANK4) <= TOL for first, length in runs), [x for x in runs if abs(x[1] - BLANK4) > TOL][:5]
    assert st["pending"] < 200 and st["underruns"] == 0 and st["merged"] == 0, st
    assert 0 <= st["lead"] <= 6 * 71051, st


def test_a_stall_re_anchors_and_a_burst_of_steps_is_merged_without_losing_a_write():
    # the game stops for 1.5 s (steps 16 to 27 come at once at 3.5 s): an underrun, then the surplus is merged; the clicks after it are whole and every click that was not skipped over plays
    clicks = {4, 12, 40, 48}
    r = run(live_script(0.2, 8.0, clicks, 0.0, stall=(16, 27, 3.5)))
    runs = runs_of(r)
    st = stats_of(r)
    assert st["underruns"] >= 1 and st["merged"] >= 1, st
    assert len(runs) == len(clicks)
    assert all(abs(length - BLANK4) <= TOL for first, length in runs), runs
    assert "re-anchoring" in r.stderr


def chain_script(t0, seconds, stall=None, nobj=24, block=80000, cb=64):
    """A chained voice (the music's shape) in live mode: objects 1..nobj of 400 samples, each a different constant, period 200 (a block lasts 80,000 colour clocks, 1.126 blanks). The library's writes:
    the first block, DMA on and the second block at the start, then at each block end n the latch of the block after next (at the end's own blank and clock). The writes go to the step that
    holds their blank. Callbacks of `cb` frames print every frame; steps come on the wall clock, from step stall[0] on come stall[1] seconds late (a pause of the game: no step is lost, none is bunched up)."""
    nsteps = int((seconds - t0 - (stall[1] if stall else 0)) / STEP_S)       # the steps end before the callbacks do (the late ones must not come after the device stopped)
    first = lambda k: k * 25 // 4                      # the first blank of step k
    obj = lambda n: n % nobj + 1                       # the n-th block played
    wr = {}                                            # step -> list of (blank in the step, clock, text)

    def put(blank_abs, clock, text):
        k = blank_abs * 4 // 25
        while first(k + 1) <= blank_abs:
            k += 1
        while first(k) > blank_abs:
            k -= 1
        wr.setdefault(k, []).append((blank_abs - first(k), clock, text))
    put(0, 0, f"latch 0 {obj(0)} 0 400\ndmaon 0\nlatch 0 {obj(1)} 0 400\nperiod 0 200\nvolume 0 63\n")
    n = 1
    while n * block < nsteps * 25 * 71051 // 4:
        put(n * block // 71051, n * block % 71051, f"latch 0 {obj(n + 1)} 0 400\n")
        n += 1
    ev = []
    for c in range(int(seconds * 44100 / cb)):
        ev.append((c * cb / 44100, 0, f"render {cb}\n"))
    for k in range(nsteps):
        t = t0 + k * STEP_S
        body = ""
        for b, c, text in sorted(wr.get(k, []), key=lambda w: (w[0], w[1])):
            body += f"at {b}\natc {c}\n" + text
        if stall and k >= stall[0]:
            t += stall[1]
        ev.append((t, 1, body + "apply\n"))
    ev.sort(key=lambda e: (e[0], e[1]))
    return "clock %d\nlive 1\nfilter 0\n" % PORT + "".join(sample_cmd(i, [5 * i] * 400) for i in range(1, nobj + 1)) + "".join(e[2] for e in ev) + "stats\n"


def value_runs(r):
    vals = [int(l.split()[0]) // 126 for l in r.stdout.splitlines() if not l.startswith("stats")]
    runs = []
    for v in vals:
        if v == 0 and not runs:
            continue
        if runs and runs[-1][0] == v:
            runs[-1][1] += 1
        else:
            runs.append([v, 1])
    return runs


def run_starts(runs):
    out, c = [], 0
    for v, l in runs:
        out.append(c)
        c += l
    return out


@pytest.mark.parametrize("late", [0.3, 0.325, 0.349, 0.374, 0.398, 0.423, 0.472, 0.497, 0.595, 1.0])
def test_a_chain_is_not_late_after_a_stall_whose_gap_holds_block_ends(late):
    # the game pauses (every step from the 16th on comes `late` seconds late, none lost or bunched up): the output passes the end of the stream received, and block ends of the chained voice fall
    # inside the gap. The mixer must not run its voices through the gap: a looping voice would repeat the block latched and take the chain's next latch late, by up to a block (the old mixer's
    # hold was a whole number of blocks, 0 to 993 frames off the pause). The output holds its sample, and after the re-anchor the blocks follow each other as without the pause, in the same order
    # and with the same lengths, delayed by the hold, which is the pause itself (to the 64 frames of a callback and the clock trim)
    ref = value_runs(run(chain_script(0.2, 6.0)))
    r = run(chain_script(0.2, 6.0, stall=(16, late)))
    got = value_runs(r)
    nominal = 80000 * 44100 / PORT                      # 993 frames
    assert all(abs(l - nominal) <= 2 for v, l in ref[1:-1])
    assert stats_of(r)["underruns"] == 1 and stats_of(r)["merged"] == 0
    assert [v for v, l in got] == [v for v, l in ref][:len(got)]
    held = [i for i, (v, l) in enumerate(got[1:-1], 1) if abs(l - nominal) > 8]       # the clock trim (at most 0.5 %) lengthens or shortens a block by a few frames
    assert len(held) == 1, held                         # the one block held through the gap
    delay = run_starts(got)[held[0] + 1] - run_starts(ref)[held[0] + 1]
    assert abs(delay - late * 44100) <= 80, (delay, late * 44100)
