"""The HUD target data (docs/re/07-ai-and-weapons.md, section 12): the message-line target data 0x36DB2 (pages 1 to 3, the names by class, the hold-off, the redraw
counter), the Y key page cycle 0x1DF62, and the lock/fire-prompt/range-dot decisions of 0x385F4, against models written from the note, in the Musashi rig.

    python3 hud_target_check.py [SEED] [N]

Part A runs 0x36DB2 with 0x3706A (the draw of the line) replaced in the rig image by a marker store, and compares the 26-byte line buffer V:4A8C2, the colour bits of
V:4A91A, V:4AA7C, V:4A93F, V:4A8F4, V:4A93E and whether the draw was reached.  Part B runs the Y key body.
Part C runs 0x385F4 with the drawing routines replaced by loggers: the fire prompt call (0x36930), V:4A96C, the cue bits, the range number argument (0x367DA).
Part D runs the gun pipper history 0x3878E (the stored point and the projection of the point stored two calls earlier, against the original's own projection 0x3377C).
Part E runs the player's range update 0x2835A for the designated target (E+0x4A, the call counter E+0x39, B4 bit 0).  Part F runs the missile lock state 0x38884.  Part G runs the range number text 0x367DA.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH

REC = 0x4B23C
MARK = 0x7F0000
img = open(IMGPATH, "rb").read() if os.path.exists(IMGPATH) else b""


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s8(v):
    v &= 0xFF
    return v - 256 if v & 0x80 else v


def ibytes(a, n):
    return img[a - 0x10000:a - 0x10000 + n]


def cstr(a):
    b = ibytes(a, 40)
    return b[:b.index(0)]


BLANK = cstr(0x3717C)
NAMES = {0x12: 0x371A6, 0x13: 0x371AD, 0x14: 0x371B4, 0x16: 0x371D0, 0x17: 0x371D7, 0x10: 0x371BB, 0x15: 0x371C2}
NOSIG = 0x371C9


def digits(v, nd, keep):
    s = ("%d" % v)[-nd:].rjust(nd, "0")
    if not keep:
        s = list(s)
        for i in range(nd - 1):
            if s[i] == "0": s[i] = " "
            else: break
        s = "".join(s)
    return s.encode()


def target_line_model(st, rec):
    """the outputs of 0x36DB2 for the input state `st` and the designated record `rec`"""
    o = dict(line=st["line"], col=st["col"], page=st["page"], hold=st["hold"], redraw=st["redraw"], b93e=st["b93e"], draw=False)
    d2 = st["des"]
    if d2 >= 0x8000 or (st["f984"] & 0x81):
        return notarget_model(st, o)
    h = (st["hold"] - 1) & 0xFF
    if s8(h) >= 0:
        o["hold"] = h
        return notarget_model(st, o)
    o["hold"] = 0xFF
    page = st["page"]
    if not s8(st["redraw"]) > 0:
        if s8(st["b93e"]) < 0:
            o["b93e"] = 1
            page = s16(page + 1)
            if page > 3: page = 1
            page &= 0xFFFF
        elif not (st["b93e"] & 1):
            return o                       # nothing is built or drawn; the hold-off byte was already rewritten
        page |= 0x8000
        o["page"] = page
        o["redraw"] = 2
    p = o["page"]
    if not (p & 0x8000):
        r = (st["redraw"] - 1) & 0xFF
        o["redraw"] = r
        if s8(r) >= 0: o["draw"] = True
        return o
    pg = p & 0x7FFF
    o["page"] = pg
    line = bytearray(BLANK)
    col = (st["col"] & 0x3F) | 0x80
    cls = rec["cls"]
    if not (rec["b20"] & 0x40):
        name = cstr(NOSIG); col &= 0x3F
    elif cls in (0x12, 0x13): name = cstr(NAMES[cls])
    elif cls in (0x14, 0x16, 0x17, 0x10): name = cstr(NAMES[cls]); col = (col & 0x3F) | 0x40
    elif cls == 0x15: name = cstr(NAMES[cls])
    else: name = b""
    line[4:4 + len(name)] = name
    if pg == 1:
        line[13:17] = cstr(0x37197)
        line[18:23] = digits((rec["y"] >> 10) * 5, 5, False)
    elif pg == 2:
        line[13:17] = cstr(0x3719C)
        line[19:22] = digits((rec["hdg"] >> 3) // 10, 3, True)
    elif pg == 3:
        line[13:17] = cstr(0x371A1)
        sp = 0 if (rec["w0"] & 0x8000) else abs(rec["spd"])
        line[18:22] = digits(sp // 12, 4, False)
    o["line"] = bytes(line); o["col"] = col; o["draw"] = True
    return o


def notarget_model(st, o):
    ps = s16(st["page"])
    if ps == 0:
        if st["msg"] != st["msgold"]:
            o["line"] = ibytes(0x41FF0 + 28 * (st["msg"] & 0xFF) + 1, 26)
            o["redraw"] = 2; o["draw"] = True
        else:
            r = s8(st["redraw"])
            if r <= 0: return o
            o["redraw"] = r - 1; o["draw"] = True
    else:
        o["page"] = 0xFFFF if ps > 0 else 0
        o["line"] = ibytes(0x41FF0 + 1, 26); o["draw"] = True
    return o


def part_a(r, rng, n):
    bad = 0; st_ = {"built": 0, "pages": [0, 0, 0, 0], "nosig": 0, "notarget": 0, "hold": 0, "redraw": 0, "none": 0}
    for it in range(n):
        r.reset()
        r.poke(0x3706A, bytes.fromhex("13FC0001") + struct.pack(">I", MARK) + b"\x4e\x75")
        r.w8(MARK, 0)
        cls = rng.choice((0x12, 0x13, 0x14, 0x16, 0x17, 0x10, 0x15, 0x11, 0x00, 0x01, 0x21))
        rec = dict(cls=cls, b20=rng.choice((0x40, 0x40, 0x40, 0x00, 0x42)), y=rng.choice((0, 1800, 20000, 300000, 5000000, 10 ** 7)),
                   hdg=rng.randint(0, 28799), spd=rng.choice((0, 100, 960, 5000, -4000, 20000, 0x7FFF, -1)), w0=rng.choice((0x11C0, 0x91C0)))
        idx = rng.randint(1, 7)
        a = REC + 0x200 * idx
        r.w8(a + 0x62, cls); r.w8(a + 0x20, rec["b20"]); r.w32(a + 0x18, rec["y"]); r.w16(a + 0x68, rec["hdg"]); r.w16(a + 0x6E, rec["spd"] & 0xFFFF); r.w16(a, rec["w0"])
        st = dict(des=rng.choice((0x200 * idx, 0x200 * idx, 0x200 * idx, 0xFFFF)), f984=rng.choice((0, 0, 0, 0x100, 1, 0x80)), hold=rng.choice((0, 1, 0xFF, 0xFF, 5, 0x18)),
                  redraw=rng.choice((0, 0, 1, 2, 0xFF)), b93e=rng.choice((0, 1, 5, 0xFF, 2)), page=rng.choice((0, 0, 1, 2, 3, 0x8001, 0xFFFF, 1)),
                  col=rng.randint(0, 255), msg=rng.randint(0, 40), msgold=rng.choice((0, 3, 7)))
        st["line"] = bytes(rng.choice(b"ABC xyz0123") for _ in range(26))
        r.w16(0x4AA78, st["des"]); r.w16(0x4A984, st["f984"]); r.w8(0x4A93F, st["hold"]); r.w8(0x4A8F4, st["redraw"]); r.w8(0x4A93E, st["b93e"])
        r.w16(0x4AA7C, st["page"]); r.w8(0x4A91A, st["col"]); r.poke(0x4A8C2, st["line"]); r.w16(0x4AB96, st["msg"]); r.w16(0x4AB9C, st["msgold"])
        if st["des"] >= 0x8000 and False: pass
        want = target_line_model(st, rec)
        r.run(0x36DB2)
        got = dict(line=r.peek(0x4A8C2, 26), col=r.u8(0x4A91A), page=r.u16(0x4AA7C), hold=r.u8(0x4A93F), redraw=r.u8(0x4A8F4), b93e=r.u8(0x4A93E), draw=bool(r.u8(MARK)))
        if "msg" in st and want["line"] == st["line"] and False: pass
        # the colour byte is only written by the build; the model keeps its input otherwise
        if got != want:
            bad += 1
            if bad < 5:
                print("DIFF target line case %d" % it, st, rec)
                for k in got:
                    if got[k] != want[k]: print("  ", k, "got", got[k], "want", want[k])
        if want["col"] != st["col"] or want["line"] != st["line"]:
            st_["built"] += 1
        if want["page"] & 0x7FFF in (1, 2, 3) and want["draw"] and st["des"] < 0x8000: st_["pages"][want["page"] & 3] += 1
        st_["notarget"] += st["des"] >= 0x8000
        if not (rec["b20"] & 0x40) and want["draw"] and st["des"] < 0x8000: st_["nosig"] += 1
    print("part A: %d message-line target data runs, %s, %s" % (n, st_, "FAILED %d" % bad if bad else "ok"))
    return bad


def part_b(r, rng, n):
    """the Y key body 0x1DF62 (the dispatcher continuation 0x1EF68 replaced by an rts)"""
    bad = 0; seen = set()
    for it in range(n):
        r.reset()
        r.poke(0x1EF68, b"\x4e\x75")
        p = rng.choice((0, 1, 2, 3, 0x8001, 0x8003, 0xFFFF, 4, 0x8000))
        r.w16(0x4AA7C, p); r.w8(0x4A93E, 0); r.w8(0x4A8F4, 0)
        r.run(0x1DF62)
        q = s16(p + 1)
        if q > 3: q = 1
        want = (q & 0xFFFF) | 0x8000
        got = (r.u16(0x4AA7C), r.u8(0x4A93E), r.u8(0x4A8F4))
        seen.add((p, got[0]))
        if got != (want, 5, 2):
            bad += 1; print("DIFF Y key", hex(p), got, hex(want))
    print("part B: %d Y key runs (%d distinct inputs), %s" % (n, len(seen), "FAILED %d" % bad if bad else "ok"))
    return bad


LOGPTR = 0x7FF000; LOGBASE = 0x7F8000; STUBS = 0x7F4000


def w(*v): return b"".join(struct.pack(">H", x & 0xFFFF) for x in v)


def logger(i):
    c = w(0x40E7, 0x48E7, 0x8080) + w(0x2079) + struct.pack(">I", LOGPTR) + w(0x30FC, i, 0x30C0, 0x30C1, 0x30C2) + w(0x23C8) + struct.pack(">I", LOGPTR)
    return c + w(0x4CDF, 0x0101, 0x44DF, 0x4E75)


def install_loggers(r):
    a = STUBS
    for i, t in ((1, 0x36930), (2, 0x340A0), (3, 0x3905C), (4, 0x392AE), (5, 0x367DA)):
        c = logger(i); r.poke(a, c); r.poke(t, w(0x4EF9) + struct.pack(">I", a)); a += (len(c) + 3) & ~3
    r.w32(LOGPTR, LOGBASE)


def read_log(r):
    end = r.u32(LOGPTR); out = []; a = LOGBASE
    while a < end:
        out.append((r.u16(a), s16(r.u16(a + 2)), s16(r.u16(a + 4)), s16(r.u16(a + 6)))); a += 8
    return out


def lock_model(st):
    """0x385F4 up to its tail: the log of the drawing calls, V:4A96C, the cue bits, the stored pipper, B4 bit 0, the range-delta words"""
    o = dict(log=[], f96c=st["f96c"], cue=0, pip=st["pip"], b4=st["b4"], d_old=st["d_old"], d_new=st["d_new"], ran=True)
    if st["zoom"] != 0x80:
        o["ran"] = False; return o
    sel = st["sel"] & 0xF0
    des = s16(st["des"]) >= 0
    pip = st["pip"]; f2 = s16(st["f2"]); f4 = s16(st["f4"]); rng_ = st["range"]
    if sel != 0x10:
        if not des: return o
        if f2 <= 0: pip = (159, 91)
    else:
        cond = (f2 > 0 and abs(f2 - pip[0]) <= 8 and abs(f4 - pip[1]) <= 8 and s16(rng_) <= 0x900)
        if cond and not (o["f96c"] == 0 and not des):
            if o["f96c"] == 0:
                o["cue"] |= 4; o["f96c"] = 1
            o["log"].append((1, 0, 0, 0))
        else:
            if o["f96c"]:
                o["f96c"] = 0; o["cue"] |= 8
        if pip[0] > 0 and s16(pip[0]) > 0: o["log"].append((2, pip[0], pip[1], 0))
        if s16(pip[0]) <= 0:
            o["pip"] = pip; return o
    o["pip"] = pip
    if s16(pip[0]) <= 0: return o
    y = s16(pip[1] + st["offy"])
    o["log"].append((3, pip[0], y, 0x50))
    if not des: return o
    k = ((rng_ & 0xFFFF) * 0x4C) // 0x8CA0
    o["log"].append((4, pip[0], y, k))
    if s16(rng_) > 0x7F00: return o
    if not st["paused"] and (st["b4"] & 1):
        o["b4"] &= ~1
        d = s16(rng_ - st["d_old"]); o["d_new"] = d & 0xFFFF; o["d_old"] = rng_
        arg = d
    else:
        arg = s16(st["d_new"])
    o["log"].append((5, arg, 0, 0))
    return o


def part_c(r, rng, n):
    bad = 0; seen = {"prompt": 0, "lost": 0, "dot": 0, "num": 0, "gun": 0, "msl": 0}
    for it in range(n):
        r.reset(); install_loggers(r)
        a = REC
        sel = rng.choice((0x10, 0x10, 0x20, 0x30, 0x00))
        st = dict(zoom=rng.choice((0x80, 0x80, 0x80, 0x40)), sel=sel | 0x0D, des=rng.choice((0x200, 0xFFFF, 0x400)), f96c=rng.choice((0, 0, 1)),
                  f2=rng.choice((0, 100, 150, 159, -1 & 0xFFFF, 200)), f4=rng.choice((0, 60, 91, 95, 100)), pip=(rng.choice((0, 100, 150, 159, 155)), rng.choice((60, 91, 95, 100))),
                  range=rng.choice((0x100, 0x500, 0x900, 0x901, 0x2000, 0x7F00, 0x7F01, 0x7FFF, 0x36C0)), b4=rng.choice((0, 1)), d_old=rng.choice((0, 300, 0x5000)),
                  d_new=rng.choice((0, 5, 0xFFF0)), paused=rng.choice((0, 1, 1)), offy=rng.choice((0, 3, -2)))
        if sel == 0x10 and rng.random() < 0.6:
            st["f2"] = st["pip"][0] + rng.choice((-8, 0, 3, 9)); st["f4"] = st["pip"][1] + rng.choice((-8, 0, 8, 9))
        st["f2"] &= 0xFFFF
        r.w16(0x4AAFA, st["zoom"]); r.w8(a + 0x63, st["sel"]); r.w16(0x4AA78, st["des"]); r.w8(0x4A96C, st["f96c"]); r.w16(0x4A9F2, st["f2"]); r.w16(0x4A9F4, st["f4"])
        r.w16(0x4A9F6, st["pip"][0]); r.w16(0x4A9F8, st["pip"][1]); r.w16(a + 0x4A, st["range"]); r.w8(a + 4, st["b4"]); r.w16(0x4ABFC, st["d_old"]); r.w16(0x4ABFE, st["d_new"])
        r.w8(0x4A866, 0xFF if st["paused"] else 0); r.w16(0x4A990, st["offy"] & 0xFFFF); r.w32(0x4AC0C, 0); r.w16(0x4A996, 0)
        want = lock_model(st)
        r.run(0x385F4)
        got = dict(log=[(e[0], e[1], 0, 0) if e[0] == 5 else (1, 0, 0, 0) if e[0] == 1 else e for e in read_log(r)], f96c=r.u8(0x4A96C), cue=r.u32(0x4AC0C), b4=r.u8(a + 4), d_old=r.u16(0x4ABFC), d_new=r.u16(0x4ABFE))
        if st["paused"]: got["pip"] = (r.u16(0x4A9F6), r.u16(0x4A9F8)); w_pip = want["pip"]
        else: w_pip = None
        cmpw = {k: want[k] for k in got if k != "pip"}
        if st["paused"]: cmpw["pip"] = want["pip"]
        if not want["ran"]:
            cmpw = dict(log=[], f96c=st["f96c"], cue=0, b4=st["b4"], d_old=st["d_old"], d_new=st["d_new"])
            if st["paused"]: got.pop("pip"); 
        if got != cmpw:
            bad += 1
            if bad < 5:
                print("DIFF lock case %d" % it, st)
                for k in got:
                    if got[k] != cmpw.get(k): print("  ", k, "got", got[k], "want", cmpw.get(k))
        seen["prompt"] += any(e[0] == 1 for e in want["log"]); seen["lost"] += (want["cue"] & 8) != 0
        seen["dot"] += any(e[0] == 4 for e in want["log"]); seen["num"] += any(e[0] == 5 for e in want["log"])
        seen["gun"] += sel == 0x10; seen["msl"] += sel in (0x20, 0x30)
    print("part C: %d lock/prompt/range runs, %s, %s" % (n, seen, "FAILED %d" % bad if bad else "ok"))
    return bad


def part_d(r, rng, n):
    """the gun pipper history 0x3878E: the point 256 units ahead and 24 units above the aircraft is stored; the pipper projects the point stored two calls earlier"""
    bad = 0
    for it in range(n):
        r.reset()
        a = REC
        pos = [rng.randint(0, 0x3FFFFFFF) for _ in range(3)]
        for j, o in enumerate((0x14, 0x18, 0x1C)): r.w32(a + o, pos[j])
        m = [rng.choice((0x4000, -0x4000, 0, rng.randint(-0x4000, 0x4000))) for _ in range(9)]
        # +0x92 matrix: words at +0x92 + 2k; the code reads +0x94,+0x96 (x), +0x9A,+0x9C... as the offsets of the routine
        mat = [rng.randint(-0x4000, 0x4000) for _ in range(18)]
        r.poke(a + 0x92, b"".join(struct.pack(">h", v) for v in mat[:9]))
        vm = [rng.choice((256, 0, -256, rng.randint(-256, 256))) for _ in range(9)]
        r.poke(0x4AC90, b"".join(struct.pack(">h", v) for v in vm))
        old = [pos[j] + rng.randint(-100000, 100000) for j in range(3)]
        newer = [pos[j] + rng.randint(-100000, 100000) for j in range(3)]
        r.poke(0x4A7DA, b"".join(struct.pack(">i", s32(v)) for v in old)); r.poke(0x4A7CE, b"".join(struct.pack(">i", s32(v)) for v in newer))
        r.w16(0x4A996, 0)
        r.run(0x3878E)
        gotp = (s16(r.u16(0x4A9F6)), s16(r.u16(0x4A9F8)))
        hist_new = [s32(r.u32(0x4A7CE + 4 * j)) for j in range(3)]; hist_old = [s32(r.u32(0x4A7DA + 4 * j)) for j in range(3)]
        # model of the stored point: position + ((0x600 * c1 + 0x4000 * c2) >> 6), c1 = words at +0x94, +0x9A, +0xA0; c2 = words at +0x96, +0x9C, +0xA2
        w = lambda k: mat[k]
        def sx(v): return s32(v)
        c1 = (w(1), w(4), w(7)); c2 = (w(2), w(5), w(8))
        want_new = [sx(pos[j] + ((0x600 * c1[j] + 0x4000 * c2[j]) >> 6)) for j in range(3)]
        d = [(old[j] - pos[j]) >> 8 for j in range(3)]
        wv = lambda v: s16(v & 0xFFFF)
        dd = [wv(x) for x in d]
        rot = [ (vm[3*k] * dd[0] + vm[3*k+1] * dd[1] + vm[3*k+2] * dd[2]) >> 8 for k in range(3)]
        r2 = r
        r2.run(0x3377C, d0=rot[0] & 0xFFFF, d1=rot[1] & 0xFFFF, d2=rot[2] & 0xFFFF)
        wantp = (s16(r2.reg("d0")), s16(r2.reg("d1")))
        if (gotp, hist_new, hist_old) != (wantp, want_new, newer):
            bad += 1
            if bad < 4: print("DIFF pipper", it, gotp, wantp, hist_new, want_new, hist_old, newer)
    print("part D: %d pipper history runs, %s" % (n, "FAILED %d" % bad if bad else "ok"))
    return bad


def part_e(r, rng, n):
    """the player's range of the designated target 0x2835A (entry A1 = the player record): which distance is stored in E+0x4A when, the flag B4 bit 0, the counter E+0x39"""
    bad = 0; seen = {"recomputed": 0, "skipped": 0, "far": 0, "norm": 0}
    for it in range(n):
        r.reset()
        a = REC
        r.w16(0x4AA6E, 0); r.w16(0x4AA6C, 0)
        idx = rng.randint(1, 7); t = a + 0x200 * idx
        r.w16(a + 0x6C, rng.choice((0, 0x100)))
        cell = lambda: rng.randint(60, 70)
        fine = lambda: rng.randint(0, 0x3FFF)
        near = rng.random() < 0.6
        sc = [cell(), cell()]; sf = [fine(), fine()]
        if near: tc = [sc[0] + rng.choice((0, 0, 1)), sc[1] + rng.choice((0, 0, -1))]
        else: tc = [cell(), cell()]
        tf = [fine(), fine()]
        sy = rng.randint(0, 5000); ty = rng.randint(0, 9000)
        r.w16(a + 6, sc[0]); r.w16(a + 8, sc[1]); r.w16(a + 0xC, sf[0]); r.w16(a + 0xE, sf[1]); r.w32(a + 0x10, sy)
        r.w16(t + 6, tc[0]); r.w16(t + 8, tc[1]); r.w16(t + 0xC, tf[0]); r.w16(t + 0xE, tf[1]); r.w32(t + 0x10, ty)
        des = rng.choice((0x200 * idx, 0x200 * idx, 0x200 * idx, 0xFFFF, 0))
        rng_old = rng.choice((0x100, 0x47F, 0x480, 0x8FF, 0x900, 0x7FFF, 0x2000))
        c39 = rng.choice((0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x05, 0x25, 0x55))
        b4 = rng.choice((0, 1)); b7a = rng.choice((0, 3, 4))
        r.w16(0x4AA78, des); r.w16(a + 0x4A, rng_old); r.w8(a + 0x39, c39); r.w8(a + 4, b4); r.w8(a + 0x7A, b7a)
        r.run(0x2835A, a1=a)
        got = (r.u16(a + 0x4A), r.u8(a + 0x39), r.u8(a + 4), r.u8(a + 0x7A))
        # model
        want = (rng_old, c39, b4, b7a)
        if s16(des) > 0:
            c = (c39 + 0x10) & 0xFF
            go = False
            if s16(rng_old) < 0x480: go = True
            else:
                thr = 0x20 if s16(rng_old) < 0x900 else 0x50
                n_ = c & 0xF0
                go = not (s8(thr) >= s8(n_))
            if not go: want = (rng_old, c, b4, b7a); seen["skipped"] += 1
            else:
                c &= 0x0F; seen["recomputed"] += 1
                dx = abs(((tc[0] - sc[0]) << 14) + tf[0] - sf[0]); dz = abs(((tc[1] - sc[1]) << 14) + tf[1] - sf[1]); dy = abs(ty - sy)
                if dx > 0x7F00 or dz > 0x7F00 or dy > 0x7F00:
                    want = (0x7FFF, c, b4, b7a); seen["far"] += 1
                else:
                    r.run(0x20884, d2=dx, d3=dy, d4=dz)
                    D = r.u16(0x4ABF8)
                    b7 = b7a
                    if D <= 0x36C0:
                        if D <= 0x1E00 and b7a == 3: b7 = 4
                        elif D > 0x1E00 and b7a == 4: b7 = 3
                    want = (D, c, b4 | 1, b7); seen["norm"] += 1
        if got != want:
            bad += 1
            if bad < 5: print("DIFF range", it, got, want, hex(des), hex(rng_old), hex(c39))
    print("part E: %d range updates of the designated target, %s, %s" % (n, seen, "FAILED %d" % bad if bad else "ok"))
    return bad


def install_loggers2(r):
    a = STUBS + 0x400
    for i, t in ((6, 0x3936E), (7, 0x36930)):
        c = logger(i); r.poke(a, c); r.poke(t, w(0x4EF9) + struct.pack(">I", a)); a += (len(c) + 3) & ~3
    r.w32(LOGPTR, LOGBASE)


def lock_state_model(st):
    """0x38884: the missile lock state (V:4AC08 bits 0x4000 and 0x0200, V:4A96C, the cues V:4AC0C), the draw of the marker (0x3936E) and of the prompt (0x36930)"""
    f96c = st["f96c"]; l08 = st["l08"]; cue = 0; log = []
    def clear():
        nonlocal l08, cue
        if l08 & 0x4200: l08 &= ~0x4200; cue |= 8
    def draw():
        log.append(6)
        if f96c: log.append(7)
    sel = st["sel"] & 0xF0; tick = st["tick"] & 0x1F
    mx, my = s16(st["mx"]), s16(st["my"])
    def lost():
        nonlocal l08, cue
        l08 &= ~0x4000; l08 |= 0x200; cue |= 0x100
    if s16(st["des"]) < 0 or sel == 0x10 or mx < 0:
        clear(); return dict(log=log, f96c=f96c, l08=l08, cue=cue)
    if not (0x60 < mx < 0xDE and 0x2E < my < 0x86):
        clear(); f96c = 0; draw(); return dict(log=log, f96c=f96c, l08=l08, cue=cue)
    if abs(s16(st["f2"]) - mx) > 5 or abs(s16(st["f4"]) - my) > 5:
        f96c = 0
        if (l08 & 0x4000) or not (l08 & 0x200): lost()
        elif tick == 9: lost()
        draw(); return dict(log=log, f96c=f96c, l08=l08, cue=cue)
    l08 |= 0x4000; l08 &= ~0x200
    if sel == 0x30: lim = 0x2700
    elif sel == 0x20: lim = 0x1800
    else: return dict(log=log, f96c=f96c, l08=l08, cue=cue)
    if lim <= s16(st["range"]):
        f96c = 0
        if tick == 9: lost()
        draw(); return dict(log=log, f96c=f96c, l08=l08, cue=cue)
    if s16(st["d_new"]) >= -0x2A3:
        if f96c == 1 and tick != 9: pass
        else: cue |= 4; f96c = 1
    else:
        if f96c != 2: cue |= 0x10; f96c = 2
    draw()
    return dict(log=log, f96c=f96c, l08=l08, cue=cue)


def part_f(r, rng, n):
    bad = 0; seen = {"lock": 0, "ready": 0, "fast": 0, "lost": 0, "far": 0, "gun": 0, "periodic": 0}
    for it in range(n):
        r.reset(); install_loggers2(r)
        a = REC
        sel = rng.choice((0x20, 0x30, 0x30, 0x20, 0x10, 0x00))
        mx = rng.choice((159, 100, 0x60, 0x61, 0xDD, 0xDE, -1 & 0xFFFF, 150)); my = rng.choice((91, 0x2E, 0x2F, 0x85, 0x86, 60))
        st = dict(des=rng.choice((0x200, 0x200, 0x200, 0xFFFF)), sel=sel | 0x0D, mx=mx, my=my, f96c=rng.choice((0, 1, 2)), l08=rng.choice((0, 0x4000, 0x200, 0x4200, 0x4040)),
                  tick=rng.choice((9, 9, 0, 1, 0x29, 31)), range=rng.choice((0x100, 0x17FF, 0x1800, 0x26FF, 0x2700, 0x7FFF, 0x900)), d_new=rng.choice((0, 5, 0xFD5D, 0xFD5C, 0xF000, 0xFFF0)))
        s16v = lambda v: s16(v)
        st["f2"] = (mx + rng.choice((0, 3, 5, 6, -5, -6, 20))) & 0xFFFF if rng.random() < 0.8 else rng.randint(0, 300)
        st["f4"] = (my + rng.choice((0, 3, 5, 6, -5, -6, 20))) & 0xFFFF
        r.w16(0x4AA78, st["des"]); r.w8(a + 0x63, st["sel"]); r.w16(0x4A9FA, st["mx"]); r.w16(0x4A9FC, st["my"]); r.w8(0x4A96C, st["f96c"])
        r.w32(0x4AC08, st["l08"]); r.w32(0x4AC0C, 0); r.w8(0x4A993, st["tick"]); r.w16(a + 0x4A, st["range"]); r.w16(0x4ABFE, st["d_new"])
        r.w16(0x4A9F2, st["f2"]); r.w16(0x4A9F4, st["f4"]); r.w16(0x4A996, 0)
        want = lock_state_model(st)
        r.run(0x38884)
        got = dict(log=[e[0] for e in read_log(r)], f96c=r.u8(0x4A96C), l08=r.u32(0x4AC08), cue=r.u32(0x4AC0C))
        if got != want:
            bad += 1
            if bad < 5:
                print("DIFF lock state", it, st)
                for k in got:
                    if got[k] != want[k]: print("  ", k, got[k], want[k])
        seen["lock"] += (want["l08"] & 0x4000) != 0; seen["ready"] += want["f96c"] == 1; seen["fast"] += want["f96c"] == 2
        seen["lost"] += (want["cue"] & 0x100) != 0; seen["far"] += any(x == 6 for x in want["log"]) and want["f96c"] == 0 and (want["l08"] & 0x4000) != 0
        seen["gun"] += sel == 0x10; seen["periodic"] += (st["tick"] & 0x1F) == 9 and bool(want["cue"])
    print("part F: %d lock-state runs, %s, %s" % (n, seen, "FAILED %d" % bad if bad else "ok"))
    return bad


def part_g(r, rng, n):
    """the range number text 0x367DA: a sign character and the four low decimal digits with the leading zeros kept"""
    bad = 0
    for it in range(n):
        r.reset()
        r.w32(0x4A76E, 0x780000)
        for k in range(8): r.w32(0x780000 + 4 * k, 0x790000 + 0x4000 * k)
        v = rng.choice((0, 1, -1, 9999, -9999, 0x7F00, -0x7F00, rng.randint(-32768, 32767), rng.randint(-300, 300)))
        r.poke(0x4A8B2, b"XXXXXXXX")
        r.run(0x367DA, d0=v & 0xFFFFFFFF)
        want = (b" " if v < 0 else b"-") + ("%04d" % (abs(v) % 10000)).encode()
        got = r.peek(0x4A8B2, 5)
        if got != want:
            bad += 1
            if bad < 4: print("DIFF range number", v, got, want)
    print("part G: %d range number texts, %s" % (n, "FAILED %d" % bad if bad else "ok"))
    return bad


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print("skipped (missing: extracted/interceptor.bin or librig.so)"); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 600
    r = Rig(); rng = random.Random(seed)
    bad = part_a(r, rng, n)
    bad += part_b(r, rng, 60)
    bad += part_c(r, rng, n)
    bad += part_d(r, rng, 200)
    bad += part_e(r, rng, 400)
    bad += part_f(r, rng, 600)
    bad += part_g(r, rng, 200)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
