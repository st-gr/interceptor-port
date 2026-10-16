"""The camera trail: its writer 0x2AC5A and its drawer 0x25804 (docs/re/07-ai-and-weapons.md, section 13.1 and 13.2), original against a model written from the note.

    python3 trail_check.py [SEED] [N]

Part 1, the writer: random states of the followed record (V:54E72) and of the ring (V:54E70 count, V:54E71 write index, the six 12-byte entries at 0x54E74), the
record's speed word +0x6E, W2 bit 0x1000 and trail length +0x3D, the pause V:4A866; 0x2AC5A is run and the ring, the count, the index and +0x3D are compared with the rule
(the position is stored at the index, also in the next slot while the count is below 5; the index wraps after 5; the count saturates at 6; with a non-zero speed and the
trail bit the length becomes count - 1 limited to 4, otherwise it counts down and the ring is emptied when it reaches 0).
Part 2, the drawer: the same kind of random states with the record near the camera of the air scene; the disc routine 0x33788 is replaced by a logger (camera-space
x, y, z, radius, pen).  The model walks the ring in the order of the note (the direction from the sign of velocity . (position - camera), the start index, 1 + 3 interpolated
discs per ring entry, the radius 0x9C0 / distance, the pens by index parity and the out-of-control bit) and takes the two arithmetic helpers (the distance estimate 0x20884
and the camera rotation 0x31566) from the original, so that only the control flow and the formulas around them are tested.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so, extracted/captures/logs/lists.txt.  Skips with exit 0 when one is missing.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, load_scene, inputs_missing
import primlog as P

REC = lambda i: 0x4B23C + 0x200 * i
RING = 0x54E74
STUB = 0x8A0000


def s8(v): v &= 0xFF; return v - 256 if v & 0x80 else v
def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v


def writer_model(st):
    """st: dict(cnt, head, ring[6][3], speed, trail_bit, n, paused, pos); returns the new dict"""
    s = dict(st); s["ring"] = [list(e) for e in st["ring"]]
    if s["paused"]: return s
    s["ring"][s["head"]] = list(s["pos"])
    if s["cnt"] < 5:
        nxt = s["head"] + 1
        if nxt < 6: s["ring"][nxt] = list(s["pos"])
    s["head"] += 1
    if s["head"] > 5: s["head"] = 0
    if s["cnt"] < 6: s["cnt"] += 1
    if s["speed"] != 0 and s["trail_bit"]:
        d = s["cnt"] - 1
        s["n"] = d if d < 5 else 4
    else:
        n = s["n"]
        if n == 0:
            s["cnt"] = 0; s["head"] = 0
        else:
            n -= 1
            if n > 0: s["n"] = n
            else: s["cnt"] = 0; s["head"] = 0; s["n"] = 0
    return s


def check_writer(r, rng, n):
    bad = 0
    for it in range(n):
        load_scene(r, "air")
        rid = rng.choice((1, 2, 4, 8))
        r.w16(0x4AA6E, rid * 0x200); r.w16(0x54E72, rid * 0x200 if rng.random() < 0.9 else 0x200 * (rid + 1))
        paused = rng.random() < 0.15
        r.w8(0x4A866, 1 if paused else 0)
        cnt = rng.randint(0, 6); head = rng.randint(0, 5)
        ring = [[rng.randint(-2**31, 2**31 - 1) for _ in range(3)] for _ in range(6)]
        speed = rng.choice((0, 0, 0x100, 0x3C0, 0x2000, -5)); tb = rng.random() < 0.6
        nlen = rng.choice((0, 0, 1, 2, 3, 4, 5, 9))
        pos = [rng.randint(-2**31, 2**31 - 1) for _ in range(3)]
        R = REC(rid)
        for i, o in enumerate((0x14, 0x18, 0x1C)): r.w32(R + o, pos[i])
        r.w16(R + 0x6E, speed); w2 = r.u16(R + 2) & ~0x1000; r.w16(R + 2, w2 | (0x1000 if tb else 0)); r.w8(R + 0x3D, nlen)
        r.w8(0x54E70, cnt); r.w8(0x54E71, head)
        for i in range(6):
            for j in range(3): r.w32(RING + 12 * i + 4 * j, ring[i][j])
        st = dict(cnt=cnt, head=head, ring=ring, speed=speed, trail_bit=tb, n=nlen, paused=paused, pos=pos)
        followed = r.u16(0x4AA6E) == r.u16(0x54E72)
        want = writer_model(st) if followed else st
        r.run(0x2AC5A)
        got_ring = [[s32(r.u32(RING + 12 * i + 4 * j)) for j in range(3)] for i in range(6)]
        got = (r.u8(0x54E70), r.u8(0x54E71), r.u8(R + 0x3D), got_ring)
        exp = (want["cnt"], want["head"], want["n"] & 0xFF, [[s32(x) for x in e] for e in want["ring"]])
        if got != exp:
            bad += 1
            if bad < 5: print("DIFF writer it %d followed %s paused %s: got %s want %s" % (it, followed, paused, got[:3], exp[:3]))
    print("trail writer: %d random states, %s" % (n, "FAILED %d" % bad if bad else "0 differences"))
    return bad


def helper_dist(r, ax, ay, az):
    r.run(0x20884, d2=ax & 0xFFFF, d3=ay & 0xFFFF, d4=az & 0xFFFF)
    return s32(r.reg("d1"))


def helper_rot(r, dx, dy, dz):
    """0x31566: camera-space coordinates of the word vector (dx, dy, dz): returns (x', y', z')"""
    r.run(0x31566, d3=dx & 0xFFFF, d4=dy & 0xFFFF, d5=dz & 0xFFFF)
    return s16(r.reg("d6")), s16(r.reg("d2")), s16(r.reg("d5"))


def drawer_model(r, R, cnt, head, ring, cam):
    out = []
    n0 = s8(r.u8(R + 0x3D))
    if n0 <= 0: return out
    w2hi = r.u8(R + 2); b20 = r.u8(R + 0x20)
    cur = head if cnt >= 6 else 0
    n = n0
    if cnt < 6: n -= 1
    vel = [s32(r.u32(R + o)) for o in (0x3E, 0x42, 0x46)]
    pos = [s32(r.u32(R + o)) for o in (0x14, 0x18, 0x1C)]
    d = [s32(pos[i] - cam[i]) for i in range(3)]
    if s16(r.u16(R + 0x6E)) >= 0x100: vel = [v >> 8 for v in vel]
    d = [x >> 8 for x in d]
    dot = sum(s16(vel[i]) * s16(d[i]) for i in range(3))
    if dot < 0:
        away = -1
    else:
        away = 0
        c = (head - cnt) & 0xFF
        if cnt < 6: c = (c - 1) & 0xFF
        c = (c + (r.u8(R + 0x3D))) & 0xFF
        if s8(c) < 0: c = (c + cnt) & 0xFF
        cur = c
    prev = None
    while True:
        E = ring[cur % 6]
        dd = [s32(E[i] - cam[i]) for i in range(3)]
        a = [abs(x) for x in dd]
        shift = 0
        if any(x > 0x4000 for x in a):
            shift = 8
            if not any(x > 0x40000 for x in a): shift = 4
        aa = [s16(x >> shift) for x in a]              # the absolute values are shifted, not recomputed from the shifted signed words
        dd = [x >> shift for x in dd]
        dw = [s16(x) for x in dd]
        aw = aa
        if cur & 1: pen = 3 if b20 & 2 else 8
        else: pen = 0xC if b20 & 2 else 0xA
        dist = helper_dist(r, aw[0], aw[1], aw[2])
        scaled = s16(dist) >> (8 - shift)
        x, y, z = helper_rot(r, dw[0], dw[1], dw[2])
        size = 0x9C0
        if scaled > 0:
            rad = size // scaled
            if rad > 0x7F: rad = 0x7F
        else: rad = 0x7F
        disc = [x, y, z, rad, shift]
        if prev is not None and r.u8(R + 0x3D) > 1:
            def scl(v, c):
                c &= 63
                return s16((v << c) & 0xFFFF) if c < 16 else 0
            cnt_sh = (prev[4] - disc[4])
            p3 = [scl(prev[i], cnt_sh) for i in range(3)]
            for q in (1, 2, 3):
                if q == 1: pt = [s16(p3[i] + (s16(disc[i] - p3[i]) >> 2)) for i in range(3)]; rr = s16(prev[3] + (s16(disc[3] - prev[3]) >> 2))
                elif q == 2: pt = [s16(p3[i] + (s16(disc[i] - p3[i]) >> 1)) for i in range(3)]; rr = s16(prev[3] + (s16(disc[3] - prev[3]) >> 1))
                else: pt = [s16(disc[i] + (s16(p3[i] - disc[i]) >> 2)) for i in range(3)]; rr = s16(disc[3] + (s16(prev[3] - disc[3]) >> 2))
                out.append((pen, pt[0], pt[1], pt[2], rr))
        prev = list(disc)
        rr = rad
        if n <= 0: rr = rad - (rad >> 3)
        out.append((pen, x, y, z, rr))
        if away >= 0:
            cur = (cur - 1) & 0xFF
            if s8(cur) < 0: cur = (cnt - 1) & 0xFF
        else:
            cur = (cur + 1) & 0xFF
            if (cnt - 1) < s8(cur): cur = 0
        n -= 1
        if n < 0: break
    return out


def check_drawer(r, rng, n):
    bad = 0; discs = 0; total = 0
    code = P.w(0x40E7, 0x2F08)
    code += P.append_header(9, None, [(2, ("reg", 0)), (4, ("reg", 1)), (6, ("reg", 2)), (8, ("reg", 6)), (10, ("mem", 0x4AA0C))])
    code += P.w(0x205F, 0x44DF, 0x7001, 0x4E75)           # movea.l (a7)+,a0; move (a7)+,ccr; moveq #1,d0; rts
    for it in range(n):
        load_scene(r, "air")
        r.poke(STUB, code); r.poke(0x33788, P.w(0x4EF9) + P.l(STUB))
        r.w32(P.LOGPTR, P.LOGBASE)
        rid = rng.choice((1, 4, 8))
        R = REC(rid)
        r.w16(0x4AA6E, rid * 0x200); r.w16(0x54E72, rid * 0x200)
        cam = [s32(r.u32(a)) for a in (0x4AB34, 0x4AB38, 0x4AB3C)]
        base = [cam[0] + rng.choice((200, 3000, 20000, 300000, 3000000)) * rng.choice((-1, 1)), cam[1] + rng.randint(-5000, 5000), cam[2] + rng.choice((200, 3000, 20000, 300000, 3000000)) * rng.choice((-1, 1))]
        r.poke(R, bytes(0xA4))
        for i, o in enumerate((0x14, 0x18, 0x1C)): r.w32(R + o, base[i])
        for i, o in enumerate((0x3E, 0x42, 0x46)): r.w32(R + o, rng.randint(-0x6000, 0x6000))
        r.w16(R + 0x6E, rng.choice((0, 0x80, 0x100, 0x1200, 0x3000))); r.w8(R + 0x20, rng.choice((0, 2)))
        # reachable states only: the writer keeps head = count while the count is below 6 and sets the length to at most count - 1 (limit 4)
        cnt = rng.randint(1, 6); head = cnt % 6 if cnt < 6 else rng.randint(0, 5)
        r.w8(R + 0x3D, rng.randint(0, min(cnt - 1, 4)))
        ring = []
        for i in range(6):
            e = [base[j] - rng.randint(0, 4000) * (i + 1) * rng.choice((1, 4, 64)) for j in range(3)]
            ring.append(e)
            for j in range(3): r.w32(RING + 12 * i + 4 * j, e[j])
        r.w8(0x54E70, cnt); r.w8(0x54E71, head)
        r.run(0x25804)
        ret = r.reg("d0") & 0xFFFF
        end = r.u32(P.LOGPTR); got = []; a = P.LOGBASE
        while a < end:
            got.append((r.u16(a + 10), s16(r.u16(a + 2)), s16(r.u16(a + 4)), s16(r.u16(a + 6)), s16(r.u16(a + 8)))); a += 16
        got = [(g[0], g[1], g[2], g[3], g[4]) for g in got]
        # the model uses the original's helpers after the run (they overwrite scratch variables only)
        r.poke(0x33788, P.w(0x4E75))
        exp = drawer_model(r, R, cnt, head, ring, cam)
        total += 1; discs += len(got)
        if got != exp or (ret != (1 if got else 0)):
            bad += 1
            if bad < 3: print("DIFF drawer it %d cnt %d head %d n %d: got %d discs %s want %d %s ret %d" % (it, cnt, head, r.u8(R + 0x3D), len(got), got, len(exp), exp, ret))
    print("trail drawer: %d states, %d discs, %s" % (total, discs, "FAILED %d" % bad if bad else "0 differences"))
    return bad


def main(argv):
    miss = inputs_missing()
    if miss: print("skipped (missing: %s)" % ", ".join(miss)); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 300
    r = Rig(); rng = random.Random(seed)
    bad = check_writer(r, rng, n) + check_drawer(r, rng, n)
    print("trail_check:", "FAILED" if bad else "ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
