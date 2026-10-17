"""The released flare and chaff against the emulator's picture (docs/re/07-ai-and-weapons.md 7.7, 11.4, 13.2): the original's drawers run in the Musashi rig on the
slot bytes, the view matrix, the camera variables and the cells that the capture logged (`followup-combat-flare-chaff-flare.txt` and `-chaff.txt`, generator
tools/vamiga-ref/scripts/gen-followup-combat.py flare|chaff), the pixel routines are replaced by stubs that log the screen coordinates and the pen, and the predicted
points are looked for in the frame dumps of the same samples.

    python3 flare_chaff_replay.py flare|chaff|chaffhead [LOGDIR-or-LOGFILE [FRAMEDIR]]     (run under ulimit -v 2500000 and timeout)

Skips with exit 0 when the capture logs or frames are missing (they are git-ignored: extracted/captures/logs/followup/ and extracted/captures/followup/).
A game pixel is two frame pixels wide and one line high; the game area starts at frame column 186 and line 43 (note 01a, Geometry).
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import ROOT, inputs_missing
import combat as C
from combat import stage, callf

POOL, LOG, PTR = 0x4AD2A, 0x7F0000, 0x7EFFF0


def install(r, addr, kind):
    """a stub that appends (kind, d0..d3, pen word V:4AA0C) to the log at LOG and returns at once"""
    code = bytes.fromhex("2079") + PTR.to_bytes(4, "big") + bytes.fromhex("30FC") + kind.to_bytes(2, "big") + bytes.fromhex("30C030C130C230C3") + \
        bytes.fromhex("30F9") + (0x4AA0C).to_bytes(4, "big") + bytes.fromhex("23C8") + PTR.to_bytes(4, "big") + bytes.fromhex("4E75")
    r.poke(addr, code)


def calls(r):
    end = r.u32(PTR); a = LOG; out = []
    while a < end:
        out.append((r.u16(a), [r.s16(a + 2 + 2 * i) for i in range(4)], r.u16(a + 10))); a += 12
    r.w32(PTR, LOG)
    return out


def frames_hit(framedir, what, k, points):
    """the first frame dump from the sample k + 3 on (the flip lags the pass by about 3 frames) in which the predicted end points (game coordinates) are not black"""
    try:
        from PIL import Image
    except ImportError:
        return None
    for j in range(k + 3, k + 12):
        f = os.path.join(framedir, "fc-%s-%03d.ppm" % (what, j))
        if not os.path.exists(f): continue
        im = Image.open(f).convert("RGB"); lit = {}
        for gx, gy in points:
            c = im.getpixel((186 + 2 * gx, 43 + gy)) if 0 <= gx < 320 and 0 <= gy < 200 else (0, 0, 0)
            if c != (0, 0, 0): lit[c] = lit.get(c, 0) + 1
        if sum(lit.values()): return sum(lit.values()), j, lit
    return None


def main(argv):
    what = argv[1] if len(argv) > 1 else "flare"
    logdir = argv[2] if len(argv) > 2 else os.path.join(ROOT, "extracted", "captures", "logs", "followup")
    framedir = argv[3] if len(argv) > 3 else os.path.join(ROOT, "extracted", "captures", "followup")
    miss = inputs_missing()
    if logdir.endswith(".log"): logs = [logdir] if os.path.exists(logdir) else []      # a log file named directly (the batch logs hold several parts)
    else: logs = [f for f in (os.path.join(logdir, "followup-combat-flare-chaff-%s.log" % what), os.path.join(logdir, "followup-combat-batch-a.log")) if os.path.exists(f)]
    if miss or not logs: print("skipped (missing: %s)" % ", ".join(miss or ["the capture log"])); return 0
    sys.path.insert(0, os.path.join(ROOT, "tools", "vamiga-ref"))
    import logparse
    L = logparse.load(logs[0])
    r = stage(place=False, silence=False)
    for a, k in ((0x3455E, 2), (0x34168, 1), (0x340D4, 3)): install(r, a, k)
    snap = r.peek(0x4A6E8, 0x54EC0 - 0x4A6E8)
    nk = 0
    for k in range(1, 400):
        try: p = L["f%d_pool" % k].data
        except KeyError: break
        fl = struct.unpack(">H", p[0x26:0x28])[0]; life = struct.unpack(">h", p[0x28:0x2A])[0]
        if fl == 0: continue
        r.poke(0x4A6E8, snap); r.poke(POOL, p[:64])
        r.poke(0x4AB1A, L["f%d_cam" % k].data); r.poke(0x4AC90, L["f%d_mat" % k].data); r.poke(0x4AA04, L["f%d_cell" % k].data)
        r.w16(0x4AA3C, 0xB3); r.w16(0x4A79E, 0xF); r.w16(0x4A7A0, 0xFFFF); r.w16(0x4A7A2, 0); r.w32(PTR, LOG)
        drawer = 0x31478 if fl & 0x1002 else (0x3140C if fl & 0x2000 else 0x31384)
        callf(r, drawer, (0,))
        c = calls(r)
        if c:
            print("sample %d flags %04X life %d: %s" % (k, fl, life, [(x[0], x[1], x[2]) for x in c])); nk += 1
            ep = [(d[0], d[1]) for x in c if x[0] == 2 for d in (x[1][:2], x[1][2:])]
            hit = frames_hit(framedir, what, k, ep)
            if hit: print("   picture: %d of %d end points are lit in the frame dump %s, colours %s" % (hit[0], len(ep), hit[1], hit[2]))
    print("replayed %d samples with drawn points" % nk)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
