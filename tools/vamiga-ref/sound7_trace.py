#!/usr/bin/env python3
"""Compare a reference-emulator audio trace with the port's register-write log (milestone 7, note 10 captures).

    python3 tools/vamiga-ref/sound7_trace.py runs EMU.log [VOICE]          the DMA runs of a voice in an `audtrace on frame` log
    python3 tools/vamiga-ref/sound7_trace.py port DUMP [VOICE]             the same for a `run_golden --audio-dump` file (tick op voice object blank value offset length clock)
    python3 tools/vamiga-ref/sound7_trace.py cmp EMU.log DUMP VOICE [I J]   compare the I-th run of the capture with the J-th run of the port: period and volume per blank from the run start

A run is a stretch of blanks in which the voice's DMA bit is on; the sequence is (period, volume) per blank.  The capture's frame sample is the end of the video frame, the port's
sample the end of the blank (after the server's writes), so the two are the same point of the blank to within the slip of one blank at a run's edges (note 10 8.2).  Reads only logs
(git-ignored); no sample byte is read or written.
"""
import re
import sys

STEPS_PER_25 = 4          # the port's step k covers blanks floor(25 k / 4) ..: 6, 6, 6, 7 blanks


def load_aud(path):
    """frame -> (dmacon, [(lc, len, per, vol) x 4]) from the last `aud` line of each frame"""
    fr = {}
    for ln in open(path):
        if not ln.startswith("aud frame"):
            continue
        m = re.match(r"aud frame (\d+) .*dmacon (\w+) .*\| (.*)", ln)
        if not m:
            continue
        v = re.findall(r"(\d): lc (\w+) len (\d+) per (\d+) vol (\d+)", m.group(3))
        fr[int(m.group(1))] = (int(m.group(2), 16), [(int(a[1], 16), int(a[2]), int(a[3]), int(a[4])) for a in v])
    return fr


def emu_series(path, voice):
    fr = load_aud(path)
    out = []
    for f in sorted(fr):
        d, v = fr[f]
        out.append((f, bool(d & (1 << voice)) and bool(d & 0x200), v[voice][2], v[voice][3]))
    return out


def port_series(path, voice):
    """per blank: (blank, dma, per, vol); the port's absolute blank of tick t (1-based), blank b is floor(25 (t - 1) / 4) + b"""
    ev = []
    for ln in open(path):
        p = ln.split()
        if len(p) < 9 or not p[0].isdigit():
            continue
        t, op, vo, ob, b, val = (int(x) for x in p[:6])
        if vo != voice:
            continue
        ev.append((25 * (t - 1) // STEPS_PER_25 + b, op, val, ob))
    if not ev:
        return []
    last = max(e[0] for e in ev)
    dma, per, vol = False, 124, 0
    out, k = [], 0
    for blank in range(min(e[0] for e in ev), last + 40):
        while k < len(ev) and ev[k][0] <= blank:
            _, op, val, _ob = ev[k]
            if op == 2:
                dma = True
            elif op == 3:
                dma = False
            elif op == 4:
                per = val
            elif op == 5:
                vol = val
            k += 1
        out.append((blank, dma, per, vol))
    return out


def runs(series):
    """[(first index, [(per, vol), ...])] over the DMA-on stretches; a run also ends when the sample list has a gap in frame numbers"""
    out, cur = [], None
    prev = None
    for i, (f, dma, per, vol) in enumerate(series):
        if dma and (cur is None or (prev is not None and f != prev + 1)):
            if cur is not None:
                out.append(cur)
            cur = (f, [])
        if dma:
            cur[1].append((per, vol))
        elif cur is not None:
            out.append(cur)
            cur = None
        prev = f
    if cur is not None:
        out.append(cur)
    return out


def show(rs, limit=70):
    for i, (f, seq) in enumerate(rs):
        comp, last = [], None
        for pv in seq:
            if pv != last:
                comp.append(pv)
            last = pv
        print("run %d: from %d, %d blanks, %d distinct (period, volume): %s%s" % (i, f, len(seq), len(comp), comp[:limit], " ..." if len(comp) > limit else ""))


def compare(a, b):
    """a, b: (period, volume) sequences; the count of blanks equal, the length difference, and the first blanks that differ"""
    n = min(len(a), len(b))
    diff = [(i, a[i], b[i]) for i in range(n) if a[i] != b[i]]
    return n, len(a) - len(b), diff


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "runs":
        show(runs(emu_series(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 2)))
    elif cmd == "port":
        show(runs(port_series(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 2)))
    elif cmd == "cmp":
        e = runs(emu_series(sys.argv[2], int(sys.argv[4])))
        p = runs(port_series(sys.argv[3], int(sys.argv[4])))
        i = int(sys.argv[5]) if len(sys.argv) > 5 else 0
        j = int(sys.argv[6]) if len(sys.argv) > 6 else 0
        n, dl, diff = compare(e[i][1], p[j][1])
        print("capture run %d (%d blanks) vs port run %d (%d blanks): %d blanks compared, %d equal, length difference %d" % (i, len(e[i][1]), j, len(p[j][1]), n, n - len(diff), dl))
        for d in diff[:20]:
            print("  blank %d: capture %s port %s" % d)
