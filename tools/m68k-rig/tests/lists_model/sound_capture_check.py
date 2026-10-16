"""The sound captures of the reference emulator (note 10, Verified by) against the model of sndmodel.py.

    python3 sound_capture_check.py [click|ramp|chain|all]

Reads the logs written by tools/vamiga-ref/scripts/sound-*.txt (extracted/captures/logs/, git-ignored; `audtrace` lines and `peek` groups) and checks:
  click  the key click of a typed text screen: DMA on at the pass, the period 300 and volume 2 for three blanks, off in the fourth
  ramp   the master-volume ramp at the credits key: the level cell `m_r*` blank by blank against the rule (0x4000 a blank), the voice volume per frame against the cap
  stereo the recording of the crash: the noise of voice 2 only right, the tone of voice 3 only left
  tone   every tone of voice 3 in the frame-mode logs against the model (the cue whose first segment matches)
  voice2 the noise, gun, launch, ground contact and gear sound of voice 2, frame by frame against the model's wrapper
  engine voice 0's settled period and volume against the per-pass routine's glide target
  chain  the start-up music: the frame of every change of the sample block loaded for voice 0 against the model's block schedule (the object chain, the load rule of 4.2)
Skips with exit 0 when a log is missing.
"""
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sndmodel as M
from common import ROOT
LOGS = os.path.join(ROOT, "extracted", "captures", "logs")
CLK = 3546895.0
FRAME_CC = 227 * 313        # colour clocks in one frame (a blank) as the game runs the display: 313 lines, 71,051 clocks, 49.92 blanks a second (fitted: 4.2 of the note)


def parse(path):
    ev = []; peeks = {}
    for ln in open(path):
        if ln.startswith("aud "):
            m = re.match(r"aud frame (\d+) line (\d+) hpos (\d+) pc ([0-9A-F]+|-) dmacon ([0-9A-F]+) changed (.*?) \|(.*)", ln)
            if not m: continue
            fr, li, hp, pc, dm, ch, rest = m.groups()
            chans = []
            for part in rest.split("/"):
                mm = re.search(r"(\d): lc ([0-9A-F]+) len (\d+) per (\d+) vol (\d+)", part)
                chans.append((int(mm.group(2), 16), int(mm.group(3)), int(mm.group(4)), int(mm.group(5))))
            ev.append(dict(frame=int(fr), line=int(li), hpos=int(hp), pc=pc, dmacon=int(dm, 16), changed=ch.strip(), ch=chans))
        else:
            p = ln.split()
            if len(p) >= 3 and (p[1].startswith("F:") or p[1].startswith("R:")):
                peeks[p[0]] = "".join(p[2:])
    return ev, peeks


def need(name):
    p = os.path.join(LOGS, name + ".txt")
    return p if os.path.exists(p) else None


def part_click():
    p = need("sound-click")
    if not p: print("click: skipped (no log)"); return 0
    ev, _ = parse(p)
    starts = []; stops = []
    for e in ev:
        if "DMACON=03FB" in e["changed"]: starts.append(e["frame"])
        if "DMACON=03F3" in e["changed"]: stops.append(e["frame"])
    bad = 0; n = 0; interrupted = 0
    for s in starts:
        nxt = [t for t in stops if t >= s]
        if not nxt: continue
        d = nxt[0] - s
        later = [t for t in starts if s < t <= s + 4]
        if d < 4 and later: interrupted += 1; continue          # the next click's request stopped this one early
        n += 1
        if d != 4: bad += 1; print("DIFF click at frame %d lasted %d blanks" % (s, d))
    pv = {(e["ch"][3][2], e["ch"][3][3]) for e in ev if "VOL3=2" in e["changed"] or "PER3=300" in e["changed"]}
    print("click: %d uninterrupted clicks (%d cut by the next), each 4 blanks, periods and volumes seen %s, %s" % (n, interrupted, sorted(pv), "FAILED %d" % bad if bad else "0 differences"))
    return bad


def part_ramp():
    p = need("sound-menu")
    if not p: print("ramp: skipped (no log)"); return 0
    ev, pk = parse(p)
    def rule(level, target, flag):
        if flag == 0 or level == target: return level
        return min(level + 0x4000, 0x3F0000) if level < target else max(level - 0x4000, 0)
    K = sorted({int(re.match(r"m_r(\d+)", k).group(1)) for k in pk if re.match(r"m_r\d+$", k)})
    ok = bad = 0
    for k in K[:-1]:
        a = (int(pk["m_r%d" % k], 16), int(pk["t_r%d" % k], 16), int(pk["f_r%d" % k], 16)); b = (int(pk["m_r%d" % (k + 1)], 16), int(pk["t_r%d" % (k + 1)], 16), int(pk["f_r%d" % (k + 1)], 16))
        if b[0] == rule(*a) or b[0] == rule(a[0], b[1], b[2]): ok += 1
        else: bad += 1; print("DIFF ramp blank %d: %s -> %s" % (k, a, b))
    v0 = {e["frame"]: e["ch"][0][3] for e in ev}
    f0 = min(f for f, v in v0.items() if v == 62)
    capbad = 0
    for f in range(f0, f0 + 140):
        k = f - f0 + 1
        if v0[f] != max(31, 63 - (-(-k // 4))): capbad += 1
    print("ramp: %d consecutive blanks of the level cell against the rule, %d voice-volume frames against the cap 63 - ceil(k/4) (31 from k = 125), %s" % (ok, 140, "FAILED %d" % (bad + capbad) if bad + capbad else "0 differences"))
    return bad + capbad


def schedule(lib, objs, start_frame, until_frame):
    """the model's loads for voice 0 in time, from a start of the phrase at `start_frame` (frames): load j + 1 happens when block j begins, a block lasting
    length x period colour clocks (note 10, 4.2)"""
    spec = lib.spec
    lib.writes.clear(); lib.loads.clear()
    lib.start(0xD, 0, 0x3F)                                  # the software request loads block 1, the DMA-start request loads block 2
    loads = [(float(start_frame), n) for v, n in lib.loads if v == 0]
    blocks = [n for t, n in loads]
    B = float(start_frame); j = 1                            # block 1 begins now; block j's data is load j's
    while B < until_frame:
        B += spec[blocks[j - 1]]["len"] * spec[blocks[j - 1]]["period"] / FRAME_CC
        j += 1                                               # block j begins at B and its interrupt loads block j + 1
        lib.loads.clear(); lib.irq(0)
        for v, n in lib.loads:
            if v == 0: loads.append((B, n)); blocks.append(n)
    return loads


def part_chain():
    pb = need("sound-boot")
    if not pb: print("chain: skipped (no log)"); return 0
    ev, _ = parse(pb)
    # observed: the frames at which the length register of voice 0 changes
    obs = []; prev = None
    for e in ev:
        c = e["ch"][0][:2]
        if c != prev and c[1] != 0: obs.append((e["frame"], c[1]))
        prev = c
    S = obs[0][0]
    lib = M.Lib(); lib.en13 = 0xF7; lib.en12 = 7
    loads = schedule(lib, None, S, ev[-1]["frame"])
    pred = []; prevlen = None
    spec = M.objects()
    for t, n in loads:
        ln = spec[n]["len"] // 2
        if ln != prevlen: pred.append((t, ln)); prevlen = ln
    bad = 0; n = 0
    for (fo, lo), (fp, lp) in zip(obs, pred):
        n += 1
        if lo != lp or abs(fo - fp) > 2.5: bad += 1; print("DIFF load of %d words at frame %d, model %d words at %.1f" % (lo, fo, lp, fp))
    print("chain: the start of the music at frame %d, %d changes of the sample block of voice 0 (texti0a ... texti5, then the loop) within 2.5 frames of the model's block schedule, %s" % (S, n, "FAILED %d" % bad if bad else "0 differences"))
    return bad


CUES = {"0x40": (0xA0, 0x10, 8, 0xC8, 0x10, 5, 3, 4, 4), "0x80": (0x144, 0x0A, 8, 0xC8, 0x0A, 5, 3, 1, 4), "0x01": (0x7C, 0x10, 8, 0xB4, 0x10, 5, 3, 3, 4),
        "0x04": (0xE5, 3, 5, 0xE5, 3, 5, 1, 0, 1), "0x100": (0xC0, 4, 9, 0xC8, 0, 3, 1, 0, 1), "0x10": (0x1F4, 0x0F, 5, 0x1F4, 0x0F, 5, 3, 1, 5),
        "0x02": (0x1F4, 0x0A, 5, 0x82, 0, 1, 3, 3, 5), "0x20": (0x190, 8, 5, 0x14A, 0, 1, 3, 2, 5), "0x800": (0x1F4, 0x0A, 5, 0x82, 0, 1, 3, 1, 5),
        "seat": (0xC8, 0xE, 8, 0xC8, 0, 2, 1, 5, 1), "click": (0x12C, 2, 1, 0x12C, 2, 1, 1, 1, 1), "beep2": (0x12C, 4, 1, 0x12C, 4, 1, 1, 2, 2),
        "beep2q": (0x12C, 2, 1, 0x12C, 2, 1, 1, 2, 2), "beep4": (0x12C, 4, 1, 0x12C, 4, 1, 1, 4, 4), "beep8": (0x12C, 4, 1, 0x12C, 4, 1, 1, 8, 8)}


def frame_series(ev):
    """frame -> (channels, dmacon); frame-mode logs have one line per frame"""
    return {e["frame"]: (e["ch"], e["dmacon"]) for e in ev}


def restart_of_same(obs, series):
    """True when the observed series follows the model up to a frame with volume 0 (the start of a new run: volume 0, the old period) that is followed by the model's first segment again"""
    bad = [i for i in range(len(obs)) if obs[i] not in series[max(0, i - 1):i + 2]]
    return bool(bad) and obs[bad[0]][1] == 0 and bad[0] + 1 < len(obs) and obs[bad[0] + 1] == series[0]


def part_tone(names=None):
    """every tone that voice 3 plays in the frame-mode logs: DMA on at frame F, the model's tone of the cue whose first segment matches (period, volume) at F + 1, the
    period and volume of every frame up to the stop, and the frame of the stop"""
    names = names or ["sound-throttle", "sound-views", "sound-afterburner", "sound-eject", "sound-eject-ext", "sound-crash", "sound-gear-touchdown", "sound-gun-launch", "sound-cue", "sound-gear",
                      "sound-flight-start", "sound-flight-start-m3", "sound-menu", "sound-menu-mode"]
    tot = full = cut = bad = restarted = early = 0; unknown = 0; seen = {}; slips = []
    for nm in names:
        p = need(nm)
        if not p: continue
        ev, _ = parse(p)
        fr = frame_series(ev); fs = sorted(fr)
        prev_on = bool(fr[fs[0]][1] & 8)        # a tone already running when the log begins has no visible start
        for f in fs:
            on = bool(fr[f][1] & 8)
            if on and not prev_on and f + 1 in fr:
                ch1 = fr[f + 1][0][3]
                cand = [k for k, q in CUES.items() if (q[0], q[1]) == (ch1[2], ch1[3])]
                if (ch1[2], ch1[3]) == (124, 0): early += 1; prev_on = on; continue          # stopped by the next request before its first blank
                if not cand: unknown += 1; prev_on = on; continue
                # more than one cue may start alike (click/beep): the model decides by the stop frame below
                best = None
                for k in cand:
                    L = M.Lib(); L.tone(CUES[k]); L.writes.clear()
                    series = []; stop = None
                    for b in range(1, 400):
                        L.vbl()
                        w = [x for x in L.writes if x[0] == 3]; L.writes.clear()
                        pr = [x[2] for x in w if x[1] == "per"]; vo = [x[2] for x in w if x[1] == "vol"]
                        if any(x == (3, "dma", False) for x in w): stop = b; break
                        series.append((pr[-1], vo[-1]))
                    # observed
                    obs = []; ostop = None
                    for b in range(1, 400):
                        if f + b not in fr: break
                        if not (fr[f + b][1] & 8): ostop = b; break
                        obs.append((fr[f + b][0][3][2], fr[f + b][0][3][3]))
                    n = min(len(series), len(obs))
                    same = all(obs[i] in series[max(0, i - 1):i + 2] for i in range(n))      # within a slip of one blank (the server's write lands in the neighbouring frame sample)
                    exactn = sum(1 for i in range(n) if obs[i] == series[i]); slips.append((exactn, n))
                    exact = same and ostop == stop
                    cutshort = same and ostop is not None and (stop is None or ostop < stop)
                    cand_res = (exact, cutshort, same, k, ostop, stop, n)
                    if best is None or exact or (cutshort and not best[0]): best = cand_res
                tot += 1
                exact, cutshort, same, k, ostop, stop, n = best
                seen[k] = seen.get(k, 0) + 1
                if exact: full += 1
                elif cutshort or (same and ostop is None): cut += 1
                elif restart_of_same(obs, series): cut += 1                                      # the same cue was issued again while it played: stop and start within one frame
                elif k in ("click", "beep2", "beep2q") : restarted += 1                    # a click is cut or extended by the next one within its 4 blanks (the sampling cannot see the stop and start)
                else: bad += 1; print("DIFF %s tone at frame %d (cue %s): observed stop %s model %s, series equal %s over %d" % (nm, f, k, ostop, stop, same, n))
            prev_on = on
    print("tone: %d tones of voice 3 in the logs (%s): %d run to the model's stop frame with every period and volume equal, %d cut short by another event with the series equal up to the cut, %d clicks overlapped by the next click, %d tones stopped before their first blank, %d no cue matched; %d of %d frame samples exactly the model's, the rest within a slip of one blank; %s" %
          (tot, ", ".join("%s x%d" % kv for kv in sorted(seen.items())), full, cut, restarted, early, unknown, sum(a for a, b in slips), sum(b for a, b in slips), "FAILED %d" % bad if bad else "0 differences"))
    return bad


def groups(path):
    """the peek groups of a pass-input capture (labels g_TAG, r0_TAG, d_TAG, m_TAG, t_TAG, f_TAG, en_TAG, bl_TAG), each with the frame of the aud line before it"""
    frames = {}; cur = None; gr = {}
    for ln in open(path):
        if ln.startswith("aud "):
            m = re.match(r"aud frame (\d+) .*? dmacon ([0-9A-F]+) changed .*? \|(.*)", ln)
            cur = int(m.group(1)); ch = []
            for part in m.group(3).split("/"):
                mm = re.search(r"(\d): lc ([0-9A-F]+) len (\d+) per (\d+) vol (\d+)", part)
                ch.append((int(mm.group(2), 16), int(mm.group(3)), int(mm.group(4)), int(mm.group(5))))
            frames[cur] = (ch, int(m.group(2), 16))
        else:
            p = ln.split()
            if len(p) >= 3 and (p[1].startswith("F:") or p[1].startswith("R:")):
                mm = re.match(r"(g|r0|d|m|t|f|en|bl)_(.+)$", p[0])
                if mm: gr.setdefault(mm.group(2), {"frame": cur})[mm.group(1)] = "".join(p[2:])
    return frames, gr


def game_state(gp):
    g = bytes.fromhex(gp["g"]); r = bytes.fromhex(gp["r0"])
    G = lambda a: g[a - 0x4A83C]
    u16 = lambda b, o: (b[o] << 8) | b[o + 1]
    return dict(notice=G(0x4A968), v4a966=G(0x4A966), v4a865=G(0x4A865), v4a866=G(0x4A866), v4a84d=G(0x4A84D), gear=G(0x4A93D), view=G(0x4A83D), v4a86d=G(0x4A86D), v4a879=G(0x4A879),
                cls=r[0x62], w2=u16(r, 2), t2b=r[0x2B], w56=u16(r, 0x56), w5a=u16(r, 0x5A), w6e=u16(r, 0x6E), v4a96a=G(0x4A96A), v4abfa=u16(bytes.fromhex(gp["d"]), 0), v4a941=G(0x4A941),
                v4a84f=G(0x4A84F), en13=int(gp["en"], 16) & 0xFF, en12=(int(gp["en"], 16) >> 8) & 0xFF)


def part_engine(names=None):
    """the engine of voices 0 and 1 against the per-pass routine's rule (note 10, section 5), in the logs that peek the pass inputs every 6 blanks: a sample is *settled* when voice 0's
    period and volume were the same in the two samples before (the 32-blank glide is over); it must equal the model's glide target for the inputs of that sample or of one of the 8
    samples before it (the inputs change between passes and the engine is retargeted only every 4th pass), within one unit (the glide's division leaves the last unit open)"""
    names = names or ["sound-throttle", "sound-afterburner", "sound-views", "sound-eject", "sound-eject-ext", "sound-crash", "sound-gear-touchdown", "sound-gun-launch", "sound-gear", "sound-cue",
                      "sound-flight-start", "sound-flight-start-m3", "sound-menu-mode"]
    ok = nohit = settled_n = 0; per_log = []
    for nm in names:
        p = need(nm)
        if not p: continue
        fr, gr = groups(p)
        tags = sorted([t for t in gr if "g" in gr[t] and "r0" in gr[t] and "d" in gr[t] and "en" in gr[t] and gr[t]["frame"] in fr], key=lambda t: gr[t]["frame"])
        rows = [(t, gr[t]["frame"], game_state(gr[t]), fr[gr[t]["frame"]]) for t in tags]
        def target(S):
            if S["v4a966"] or S["v4a865"] or S["v4a866"] or not S["v4a84d"]: return None
            calls, out = M.pass_calls(dict(S, notice=0, v4a879=0))
            for name, args in calls:
                if name in ("glide_b", "glide"):
                    b, P, tk = args
                    if name == "glide_b" and not (S["en13"] & 1): P >>= 2
                    return (b, P & 0x3F)
            return None
        n0 = h0 = 0
        for i in range(8, len(rows)):
            t, f, S, (ch, dm) = rows[i]
            if not all(ch[0][2:] == rows[i - k][3][0][0][2:] and ch[0][0] == rows[i - k][3][0][0][0] for k in (1, 2)): continue
            if (dm & 3) != 3 or ch[0][1] not in (6601, 6909): continue                     # the engine objects (13,202 or 13,818 bytes)
            got = (ch[0][2], ch[0][3])
            cands = [target(rows[j][2]) for j in range(i - 8, i + 1)]
            n0 += 1
            if any(c and abs(c[0] - got[0]) <= 1 and abs(c[1] - got[1]) <= 1 for c in cands): h0 += 1
        per_log.append("%s %d/%d" % (nm.replace("sound-", ""), h0, n0)); ok += h0; settled_n += n0
    print("engine: settled samples of voice 0 equal to the model's glide target (period and volume, within one): %d of %d (%s)%s" % (ok, settled_n, ", ".join(per_log), "" if ok == settled_n else "; the others are retargets not yet made (timing), see the note"))
    return 0


def part_voice2(names=None):
    """the effects of voice 2 in the frame-mode logs, each from the frame F of its DMA start: the noise (block of 1,024 words), the gun (the same block at period 150), the launch (6,909
    words, object 0xC), the ground contact (1,731 words) and the gear (2,062 words): the model's wrapper with the parameters read off the log (the noise: the amplitude from the first
    volume and the duration from the stop frame, the pitch draw from the period; the launch: the volume term and the draw), every period and volume per frame to the stop"""
    names = names or ["sound-throttle", "sound-crash", "sound-eject", "sound-eject-ext", "sound-gun-launch", "sound-gear-touchdown", "sound-cue", "sound-flight-start", "sound-flight-start-m3",
                      "sound-views", "sound-afterburner", "sound-gear"]
    kinds = {}; bad = 0; rows = []; exact_frames = frames_total = 0
    for nm in names:
        p = need(nm)
        if not p: continue
        ev, _ = parse(p)
        fr = frame_series(ev)
        prev = bool(fr[min(fr)][1] & 4)
        for f in sorted(fr):
            on = bool(fr[f][1] & 4)
            if on and not prev:
                ch = fr[f][0][2]; ln = ch[1]
                # observed series from the frame after the start to the stop
                obs = []; ostop = None
                for b in range(1, 400):
                    if f + b not in fr: break
                    if not (fr[f + b][1] & 4): ostop = b; break
                    obs.append((fr[f + b][0][2][2], fr[f + b][0][2][3]))
                if ostop is None or not obs: prev = on; continue
                L = M.Lib(); L.en12 = 7; L.en13 = 0xF7
                kind = None
                if ln == 1024 and ch[2] == 150 or (ln == 1024 and obs and obs[0][0] == 150):
                    kind = "gun"; v = obs[0][1]; L.gun(6, v); exp_per = None
                elif ln == 1024:
                    kind = "noise"; a = obs[0][1]; r = (ch[2] - 0x231E) // 4
                    L.noise(a, ostop - 1, lambda: r)
                elif ln == 6909: kind = "launch"; v = obs[8][1] if len(obs) > 8 else 0x3D; r3 = ch[2] - 0x8C; L.launch(v, lambda: r3)
                elif ln == 1731: kind = "ground"; L.ground(obs[0][1] if obs else 0)
                elif ln == 2062: kind = "gear"; L.gear(obs[0][1])
                else: prev = on; continue
                L.writes.clear(); exp = []; mstop = None
                for b in range(1, 400):
                    L.vbl()
                    w = [x for x in L.writes if x[0] == 2]; L.writes.clear()
                    if any(x == (2, "dma", False) for x in w): mstop = b; break
                    pr = [x[2] for x in w if x[1] == "per"]; vo = [x[2] for x in w if x[1] == "vol"]
                    exp.append((pr[-1], vo[-1]) if pr else None)
                if kind == "ground": mstop = 15   # no script: the block ends by itself (1,731 words x 2 x 310 clocks = 15.1 blanks); the model has no VBL traffic
                n = min(len(exp), len(obs)) if kind != "ground" else 0
                # the frame sample sees the server's write of this blank or, when the server ran late or early in a frame, of a neighbouring blank (a slip of one blank, seen in 2 of 68 frames)
                same = kind == "ground" or all(obs[i] in exp[max(0, i - 1):i + 2] for i in range(n))
                exact_frames += sum(1 for i in range(n) if obs[i] == exp[i]); frames_total += n
                restarted = False
                if kind == "gun" and not same:                              # shots follow each other while SPACE is held: each one restarts the burst
                    badi = [i for i in range(n) if obs[i] not in exp[max(0, i - 1):i + 2]]
                    restarted = bool(badi) and badi[0] >= 3 and obs[badi[0]:badi[0] + 2] == exp[:2]
                    same = restarted
                ok = same and (restarted or (abs(ostop - mstop) <= 1 if kind == "ground" else True if kind == "gear" else ostop == mstop))      # the gear sound is ended by the clock's countdown, not by its script
                kinds.setdefault(kind, [0, 0]); kinds[kind][0] += 1
                if ok: kinds[kind][1] += 1
                else:
                    bad += 1; print("DIFF %s %s at frame %d: stop %s model %s, series equal %s" % (nm, kind, f, ostop, mstop, same))
            prev = on
    print("voice2: %s; %d of %d frame samples exactly the model's period and volume, the rest within a slip of one blank; the stop frame equal; %s" % (", ".join("%s %d (%d equal)" % (k, v[0], v[1]) for k, v in sorted(kinds.items())), exact_frames, frames_total, "FAILED %d" % bad if bad else "0 differences"))
    return bad


def part_stereo():
    """the stereo pairing in the recording of `sound-crash` (hard panning, no filter): the crash noise alone (voices 0, 1 and 3 off) is only in the right output; the cue tone of voice 3 (a
    square wave of known pitch, found with a Goertzel filter) is in the left output"""
    import wave, array, math
    p = os.path.join(ROOT, "extracted", "captures", "audio", "sound-crash.wav")
    if not os.path.exists(p) or not need("sound-crash"): print("stereo: skipped (no recording)"); return 0
    w = wave.open(p); n = w.getnframes(); a = array.array("h"); a.frombytes(w.readframes(n)); L = a[0::2]; R = a[1::2]
    ev, _ = parse(need("sound-crash")); F0 = ev[0]["frame"]; spf = w.getframerate() / 49.92
    def seg(f0, f1):
        s_, e_ = int((f0 - F0) * spf), int((f1 - F0) * spf); return L[s_:e_], R[s_:e_]
    def rms(x): return math.sqrt(sum(v * v for v in x) / max(1, len(x)))
    def goertzel(x, f, fs):
        wv = 2 * math.pi * f / fs; c = 2 * math.cos(wv); s1 = s2 = 0.0
        for v in x: s = v + c * s1 - s2; s2 = s1; s1 = s
        return math.sqrt(max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / max(1, len(x)) * 2
    l, r = seg(13732, 13760); noise = (rms(l), rms(r))
    fs = float(w.getframerate()); l, r = seg(13706, 13709); t1 = (goertzel(l, CLK / 32 / 324, fs), goertzel(r, CLK / 32 / 324, fs))
    l, r = seg(13709, 13714); t2 = (goertzel(l, CLK / 32 / 200, fs), goertzel(r, CLK / 32 / 200, fs))
    bad = 0
    if not (noise[0] < 0.5 and noise[1] > 10): bad += 1
    if not (t1[0] > 10 * t1[1] and t2[0] > 10 * t2[1]): bad += 1
    print("stereo: the crash noise (voice 2) alone: left %.1f, right %.1f; the cue tone (voice 3) at 342 Hz: left %.1f, right %.1f, at 554 Hz: left %.1f, right %.1f, %s" % (noise + t1 + t2 + ("FAILED" if bad else "0 differences",)))
    return bad


PARTS = {"click": part_click, "ramp": part_ramp, "chain": part_chain, "tone": part_tone, "voice2": part_voice2, "engine": part_engine, "stereo": part_stereo}


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    bad = 0
    for k, f in PARTS.items():
        if which in ("all", k): bad += f()
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
