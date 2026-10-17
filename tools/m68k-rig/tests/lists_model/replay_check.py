"""Demo replay reader (docs/re/08-game-flow.md, section 7): the note's stream rules against the original's replay reader 0x1DFAA in the rig.

    python3 replay_check.py [SEED] [N]

Part 1 decodes the shipped demo (extracted/disk/text/textply and textctl, git-ignored) frame by frame: the rig runs the reader once per frame
with the shipped bytes in the original's three buffers, and the note's model (key byte i with the event words of index i+1, wrap at the buffer
size, end at three 0xFF bytes after the key just read or at the record-end pointer) must agree on the key byte V:4B2A1, the two event words
V:4AA4E and V:4AA50, the two stream pointers, V:4A903 and the leave request V:4A8E2, for every frame up to and past the first end.
Part 2 runs N random synthetic streams (random size, random 0xFF markers, random record-end pointer, replay mode 3 and the dev mode 1) through the
same comparison.  Part 3 checks the stream's frame count and event usage figures the note states.
Inputs (git-ignored): extracted/interceptor.bin, extracted/disk/text/textply and textctl, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH, ROOT

PLY = os.path.join(ROOT, "extracted", "disk", "text", "textply")
CTL = os.path.join(ROOT, "extracted", "disk", "text", "textctl")
for p in (IMGPATH, LIBPATH, PLY, CTL):
    if not os.path.exists(p):
        print("replay_check: skipped (missing %s)" % os.path.relpath(p, ROOT)); sys.exit(0)

KEYS, EVT = 0x600000, 0x620000          # the rig's buffers (the original allocates them at run time)
REC = 0x4B23C                           # the player record (A1 of the reader)
V_KP, V_EP, V_END, V_START, V_COUNT, V_EV0, V_EVREC = 0x54E5C, 0x54E58, 0x54E4C, 0x54E44, 0x54E48, 0x54E50, 0x54E54


class Model:
    """The note's reader: one call per player step."""
    def __init__(self, keys, evs, count, mode, rec_end):
        self.K, self.E, self.count, self.mode, self.rec_end = keys, evs, count, mode, rec_end
        self.kp, self.ep = 0, 1             # key index; event index (one ahead, as the start routine sets it)
        self.leave = 0; self.m903 = mode

    def step(self):
        k = self.K[self.kp]; self.kp += 1
        w1, w2 = self.E[self.ep]; self.ep += 1
        r = {"key": k, "w1": w1}
        if self.mode != 1:
            r["w2"] = w2
        if self.ep >= self.count:
            self.ep = 0
        while True:
            nxt = self.K[self.kp:self.kp + 3]
            if nxt == b"\xff\xff\xff" or self.kp == self.rec_end:
                self.kp, self.ep, self.m903, self.leave = 1, 2, 3, 1
                self.mode = 3                   # the end sets replay mode 3 (also leaving the dev mode 1)
                break
            if self.kp < self.count:
                break
            self.kp, self.ep = 0, 1
        return r


def setup(r, keys, evs, count, mode, rec_end_idx):
    r.reset()
    r.poke(KEYS, bytes(keys) + bytes(8)); r.poke(EVT, b"".join(struct.pack(">HH", a, b) for a, b in evs) + bytes(16))
    r.w32(V_START, KEYS); r.w32(V_COUNT, count); r.w32(V_EV0, EVT); r.w32(V_EVREC, EVT)
    r.w32(V_END, KEYS + rec_end_idx); r.w32(V_KP, KEYS); r.w32(V_EP, EVT + 4)
    r.w8(0x4A903, mode); r.w8(0x4A95E, 3); r.w8(0x4A8E2, 0)


def pad(keys, evs, n):
    return keys + bytes(max(0, n - len(keys))), evs + [(0, 0)] * max(0, n - len(evs))


def run_case(r, keys, evs, count, mode, rec_end_idx, frames):
    """Run the original and the model for `frames` calls; return the number of frame comparisons and a list of mismatches."""
    keys, evs = pad(keys, evs, count + 4)
    setup(r, keys, evs, count, mode, rec_end_idx)
    m = Model(keys, evs, count, mode, KEYS + rec_end_idx - KEYS)
    bad = []
    for f in range(frames):
        r.w8(0x4A8E2, 0)
        r.w16(0x4AA4E, 0xDEAD); r.w16(0x4AA50, 0xBEEF)        # sentinels: mode 1 must leave 4AA50 alone
        r.run(0x1DFAA, a1=REC)
        exp = m.step()
        got = {"key": r.u8(0x4B2A1), "w1": r.u16(0x4AA4E)}
        if "w2" in exp:
            got["w2"] = r.u16(0x4AA50)
        elif r.u16(0x4AA50) != 0xBEEF:
            got["w2"] = r.u16(0x4AA50)
        ok = got == exp
        ok = ok and (r.u32(V_KP) - KEYS == m.kp or m.leave and r.u32(V_KP) - KEYS == m.kp)
        ok = ok and r.u32(V_EP) - EVT == 4 * m.ep
        if m.leave:
            ok = ok and r.u8(0x4A8E2) == 1 and r.u8(0x4A903) == 3
        else:
            ok = ok and r.u8(0x4A8E2) == 0
        if not ok:
            bad.append((f, exp, got, r.u32(V_KP) - KEYS, m.kp, r.u32(V_EP) - EVT, 4 * m.ep))
        if m.leave:
            m.leave = 0
    return frames, bad


# Event word bit -> entry address of the handler body the executor 0x1D954 jumps to (the note's table); word 1 = V:4AA4E, word 2 = V:4AA50
W1 = {8: 0x1E93E, 9: 0x1E342, 10: 0x1DF48, 11: 0x1DF58, 12: 0x1E8A6, 13: 0x1DE52, 14: 0x1DEF4, 15: 0x1DED0, 0: 0x1E308, 1: 0x1EE0C, 2: 0x1EE9E, 3: 0x1EF1E}
W2 = {8: 0x1ECB8, 9: 0x1E398, 10: 0x1E3EE, 11: 0x1E51C, 12: 0x1E624, 13: 0x1E4AC, 14: 0x1E68C, 15: 0x1E4C6, 0: 0x1E640, 1: 0x1E6D8, 2: 0x1E65C, 3: 0x1E6B6, 4: 0x1E4E2, 5: 0x1E4EE, 6: 0x1E892, 7: 0x1E5BC}
ORDER = [("w1", b) for b in (8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3)] + [("w2", b) for b in (8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7)]
BODIES = sorted(set(W1.values()) | set(W2.values()))


def executor_trace(r, w1, w2, mode):
    """Run the executor 0x1D954 once per call (as the poll loop does) until it makes no more progress; return the list of bodies visited, in order."""
    import ctypes
    for a in BODIES:                                  # each body returns at once: only the dispatch is under test
        r.poke(a, struct.pack(">H", 0x4E75))
    arr = (ctypes.c_uint32 * len(BODIES))(*BODIES)
    r.w8(0x4A903, mode); r.w16(0x4AA4E, w1); r.w16(0x4AA50, w2); r.w8(0x4A83D, 0)
    seq = []
    for _ in range(40):
        if r.u16(0x4AA4E) == 0 and (mode != 3 or r.u16(0x4AA50) == 0):
            break
        r.L.rig_watch_set(arr, len(BODIES)); r.L.rig_trace_clear()
        r.run(0x1D954)
        n = r.L.rig_trace_get(None, 0)
        buf = (ctypes.c_uint32 * 8)(); r.L.rig_trace_get(buf, 8)
        seq += [buf[i] for i in range(min(n, 8))]
        if n == 0:
            break
    r.L.rig_watch_set(arr, 0)
    return seq


def part4(rng, N):
    """The executor's dispatch: each single bit goes to the body of the table; random sets run the bodies in the note's order; mode 1 ignores word 2."""
    r = Rig(); r.reset()
    bad = cases = 0
    for w, tab in (("w1", W1), ("w2", W2)):
        for b, addr in tab.items():
            for mode in (3, 1):
                r.reset()
                v = 1 << b
                seq = executor_trace(r, v if w == "w1" else 0, v if w == "w2" else 0, mode)
                want = [addr] if (w == "w1" or mode == 3) else []
                cases += 1
                if seq != want:
                    bad += 1; print("  MISMATCH single", w, b, mode, [hex(x) for x in seq], [hex(x) for x in want])
    for _ in range(N):
        r.reset()
        m1 = sum(1 << b for b in W1 if rng.random() < 0.25); m2 = sum(1 << b for b in W2 if rng.random() < 0.25)
        mode = rng.choice((3, 3, 1))
        seq = executor_trace(r, m1, m2, mode)
        want = []
        for w, b in ORDER:
            if w == "w1" and m1 >> b & 1: want.append(W1[b])
            if w == "w2" and mode == 3 and m2 >> b & 1: want.append(W2[b])
        cases += 1
        if seq != want:
            bad += 1; print("  MISMATCH random", hex(m1), hex(m2), mode, [hex(x) for x in seq], [hex(x) for x in want])
    return cases, bad


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    N = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    ply, ctl = open(PLY, "rb").read(), open(CTL, "rb").read()
    n = len(ply)
    evs = [struct.unpack(">HH", ctl[4 * i:4 * i + 4]) for i in range(n)]
    r = Rig()
    # Part 1: the shipped demo; the first end is at the first 0xFF 0xFF 0xFF after a key byte
    first = next(i for i in range(n - 3) if ply[i + 1:i + 4] == b"\xff\xff\xff")
    frames, bad = run_case(r, ply, evs, 7812, 3, 0, first + 1 + 5)
    print("replay_check part 1: shipped demo, %d frames of the original's reader compared (first end after frame index %d, stream %d frames, %d of the %d played frames carry an event): %d mismatches"
          % (frames, first, n, sum(1 for e in evs[1:first + 2] if e != (0, 0)), first + 1, len(bad)))
    for b in bad[:5]:
        print("  MISMATCH", b)
    total_bad = len(bad)
    # Part 2: random streams
    rng = random.Random(seed)
    cases = frames_t = 0
    for c in range(N):
        count = rng.randint(4, 60)
        size = count + 4
        keys = bytes(rng.randrange(256) if rng.random() < 0.3 else rng.choice((0, 1, 4, 8, 16, 32)) for _ in range(size))
        keys = bytearray(keys)
        for _ in range(rng.randint(0, 3)):
            p = rng.randrange(size - 3)
            keys[p:p + rng.choice((1, 2, 3, 4))] = b"\xff" * rng.choice((1, 2, 3, 4))
        keys = bytes(keys[:size])
        evs2 = [(rng.randrange(65536), rng.randrange(65536)) for _ in range(size)]
        mode = rng.choice((3, 3, 3, 1))
        rec_end = rng.choice((0, 0, 0, rng.randint(1, count)))
        fr, bad = run_case(r, keys, evs2, count, mode, rec_end, rng.randint(count, 3 * count))
        cases += 1; frames_t += fr; total_bad += len(bad)
        for b in bad[:2]:
            print("  MISMATCH case", c, b)
    print("replay_check part 2: %d random streams, %d frames compared (replay states 3 and 1, markers, wraps, record-end pointer): %d mismatches" % (cases, frames_t, total_bad - 0))
    c4, b4 = part4(rng, N)
    print("replay_check part 4: executor dispatch, %d cases (28 single bits in modes 3 and 1, %d random sets in the note's order): %d mismatches" % (c4, N, b4))
    total_bad += b4
    # Part 3: figures the note states
    fl = [i for i in range(n) if ply[i] == 0xFF]
    print("replay_check part 3: %d key bytes, %d event words, 0xFF bytes at %s" % (n, len(ctl) // 2, fl))
    sys.exit(1 if total_bad else 0)


main()
