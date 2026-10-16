"""Radar list builder, scope walk and threat bits (docs/re/07-ai-and-weapons.md, section 12).

    python3 radar_check.py [SEED] [N]

Part 1 (list builder, the tail of the world builder from 0x1FEE6): N random sets of records are written, the tail is run and the list at 0x53368 is compared
with the stated rule (selection: records 0 to 15 except the viewed one with bit 6 of byte +1 set; difference vector of the three position longs; shift pair k = 0, 2,
4, ... until the largest magnitude is at most 0x7FFFFF; the rotation by the first and third rows of the matrix at 0x4ACB4; 16-byte entries; the zero terminator) and
the gating by the view byte, the redraw counter and the frame counter.
Part 2 (scope walk, 0x35E5E onwards): random lists and records; the threat bits V:4A925 and V:4A926 with the cue bits V:4AC0C and the hold-off V:4A93F, the detected
bit (+0x20 bit 6), the flag bit 0x4000 of W0, the drawn bit of the list flag word, the pens and positions of the blips (logged through the pixel routines 0x340D4 and
0x340EA), the erase lists, the 10-blip limit and the page quirk.
Part 3 (designation, 0x362D8 onwards): the cycle over the list.  Part 2b: the cue dispatch alone.  Part 4: the builder, the walk and a T request end to end on the placed hostile of mode 3.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
import handler_hook as hh

REC = 0x4B23C
LIST = 0x53368


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def w16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def setup(r):
    r.reset()
    r.run(0x29D28)                      # list pointer


def put_matrix(r, m):
    r.poke(0x4ACB4, b"".join(struct.pack(">h", v) for v in m))


def build_model(recs, view, mat):
    out = b""
    for i in range(16):
        off = i * 0x200
        if off == view:
            continue
        rc = recs[i]
        if not (rc["b1"] & 0x40):
            continue
        d = [s32(rc["pos"][j] - recs[view // 0x200]["pos"][j]) for j in range(3)]
        mx = max(abs(v) for v in d)
        k = 0
        while mx > 0x7FFFFF:
            k += 2
            mx >>= 2
        dd = [w16(v >> (k + 8)) for v in d]
        x = (mat[0] * dd[0] + mat[1] * dd[1] + mat[2] * dd[2]) >> 8
        z = (mat[6] * dd[0] + mat[7] * dd[1] + mat[8] * dd[2]) >> 8
        x = s32(x << k); z = s32(z << k)
        flag = ((off >> 1) & 0xFFFF) | 0x10
        out += struct.pack(">iiiHH", x, 0, z, flag, 0)
    return out + b"\0" * 12


def part1(r, rng, n):
    bad = 0
    for it in range(n):
        setup(r)
        view = rng.choice((0, 0, 0, 0x200, 0x600))
        r.w16(0x4A996, view)
        mat = [rng.choice((rng.randint(-256, 256), 256, 0, -256)) for _ in range(9)]
        put_matrix(r, mat)
        recs = []
        for i in range(16):
            b1 = rng.choice((0x40, 0x40, 0x00, 0x48, 0x10))
            span = rng.choice((1 << 12, 1 << 20, 1 << 24, 1 << 28, 1 << 31))
            pos = [s32(rng.randrange(-span, span)) for _ in range(3)]
            if rng.random() < 0.1: pos = [0, 0, 0]
            recs.append({"b1": b1, "pos": pos})
            r.w8(REC + i * 0x200 + 1, b1)
            for j in range(3):
                r.w32(REC + i * 0x200 + 0x14 + 4 * j, pos[j])
        r.w8(0x4A83D, 0); r.w8(0x4A8EF, 2); r.w16(0x4A992, rng.randint(0, 255))
        r.run(0x1FEE6)
        want = build_model(recs, view, mat)
        got = r.peek(LIST, len(want))
        if got != want:
            bad += 1
            if bad < 4:
                print("DIFF builder case %d view %x" % (it, view)); print(got.hex()); print(want.hex())
    cases = 0
    for ext in (0, 1):
        for cnt in (0, 1, 2, 5):
            for n_ in range(8):
                setup(r); r.w16(0x4A996, 0); put_matrix(r, [256, 0, 0, 0, 0, 0, 0, 0, 256])
                r.w8(REC + 0x200 + 1, 0x40); r.w32(REC + 0x200 + 0x14, 0x100000)
                r.w8(0x4A83D, ext); r.w8(0x4A8EF, cnt); r.w16(0x4A992, n_); r.w8(0x4A8F0, 0x77); r.w8(0x4A8ED, 0x77)
                r.run(0x1FEE6)
                built = r.u32(LIST) != 0
                f0 = r.u8(0x4A8F0)
                if ext: wb, wf = False, 0
                elif cnt > 1: wb, wf = True, 1
                else: wb, wf = (n_ & 3) == 0, 1 if (n_ & 2) == 0 else 0
                cases += 1
                if built != wb or f0 != wf or r.u8(0x4A8ED) != 0:
                    bad += 1; print("DIFF gate ext=%d cnt=%d n=%d built=%s want=%s f0=%d want=%d" % (ext, cnt, n_, built, wb, f0, wf))
    print("part 1: %d random lists, %d gating cases, %s" % (n, cases, "FAILED %d" % bad if bad else "ok"))
    return bad



def rec_set(r, i, w0, w2, b20, cls, tgt, y, nib=0):
    a = REC + 0x200 * i
    r.w16(a, w0); r.w16(a + 2, w2); r.w8(a + 0x20, b20); r.w8(a + 0x62, cls); r.w8(a + 0x38, tgt); r.w32(a + 0x10, y)
    r.w8(a + 0x63, nib)


def part2(r, rng, n):
    """the scope walk 0x35E5E..0x362D8: one pass on random lists, records and bookkeeping against the stated rule"""
    bad = 0; stats = {"plots": 0, "double": 0, "erased": 0, "threat": 0, "detected": 0, "cues": 0, "limit": 0}
    for it in range(n):
        setup(r); hh.install(r)
        view = rng.choice((0, 0, 0, 1, 3))
        dense = rng.random() < 0.25
        if dense: view = 0
        mode = rng.choice((3, 3, 3, 5, 2, 0x7D))
        page = rng.choice((0, 1)); nib = rng.choice((9, 11, 13, 3, 13))
        offx = rng.choice((0, 0, 7, -5)); offy = rng.choice((0, 0, 3))
        r.w8(0x4A95E, mode); r.w16(0x4A996, 0x200 * view); r.w16(0x4A994, rng.choice((0, 0, 0, 1)))
        r.w16(0x4A724, page); r.w16(0x4AA40, offx & 0xFFFF); r.w16(0x4A990, offy)
        r.w8(0x4A8F0, 1); r.w8(0x4A93B, rng.randint(0, 255)); r.w8(0x4A925, 0x55)
        prev = rng.choice((0, 0, 2, 4, 8, 0x10, 0x20, 0x06, 0x0A, 0x14, 0x08, 0x0C, 0x0E, 0x0A))
        r.w8(0x4A926, prev)
        r.w8(0x4A927, rng.choice((0, 0, 5, 0x1E, 1, 0xFF))); r.w8(0x4A93F, 0); r.w32(0x4AC0C, 0)
        r.w16(0x4AA78, rng.choice((0xFFFF, 0xFFFF, 0x200 * rng.randint(0, 6))))
        r.w32(0x4AB30, rng.choice((0, 12, -13 & 0xFFFFFFFF, 40)))
        r.w16(0x4AA0C, 0x55)
        recs = {}
        for i in range(8):
            w0 = rng.choice((0x11C0, 0x11C8)) if dense and i else rng.choice((0x11C0, 0x11C8)) if dense else rng.choice((0x11C0, 0x11C8, 0x11C0 | 0x4000, 0x0040, 0x0000, 0x11C0 | 0x200, 0x11C0 | 0x400, 0x11C8 | 0x4000))
            w2 = rng.choice((0, 0, 0, 0x80))
            b20 = rng.choice((0, 0, 0x40, 0x02, 0x42))
            cls = rng.choice((0x12, 0x13, 0x11, 0x10, 0x15, 0x14, 0x00, 0x01, 0x20, 0x21, 0x30, 0x16))
            tgt = rng.choice((0xFF, 0x80, 0x80, 0x81))
            y = rng.choice((0, 5, -20 & 0xFFFFFFFF, 4000, 40))
            rec_set(r, i, w0, w2, b20, cls, tgt, y, nib if i == view else 0)
            recs[i] = dict(w0=w0, w2=w2, b20=b20, cls=cls, tgt=tgt, y=s32(y))
        r.w8(REC + 0x200 * view + 0x63, nib)
        # erase list of the page
        base = 0x537C8 + (0x28 if page else 0)
        old = []
        for k in range(rng.choice((0, 1, 3, 10))):
            x = rng.randint(130, 185); y = rng.randint(150, 190)
            dbl = rng.random() < 0.3
            old.append((x | (0x8000 if dbl else 0), y))
        for k, (x, y) in enumerate(old):
            r.w16(base + 4 * k, x); r.w16(base + 4 * k + 2, y)
        if len(old) < 10: r.w16(base + 4 * len(old), 0xFFFF)
        # the list
        ents = []
        for k in range(rng.randint(8, 14) if dense else rng.randint(0, 14)):
            idx = rng.randint(1, 7) if dense else rng.randint(0, 7)
            span = rng.choice((0x4000, 0x4000, 0x40000)) if dense else rng.choice((0x4000, 0x40000, 0x400000, 0x2000000))
            x = rng.randint(-span, span); z = rng.randint(-span, span)
            flag = (idx << 8) | (0x10 if dense else rng.choice((0x10, 0x10, 0x10, 0x30, 0x00, 0x20)))
            ents.append((x, z, flag))
        for k, (x, z, flag) in enumerate(ents):
            r.poke(LIST + 16 * k, struct.pack(">iiiHH", x, 0, z, flag, 0))
        r.poke(LIST + 16 * len(ents), bytes(16))
        # snapshot of inputs for the model
        old = [(r.u16(base + 4 * k), r.u16(base + 4 * k + 2)) for k in range(11)]      # the erase loop reads 11 entries, the 11th from the next page
        st = {"view": view, "mode": mode, "page": page, "nib": nib, "offx": offx, "offy": offy, "prev": prev,
              "t927": r.u8(0x4A927), "des": r.u16(0x4AA78), "b3b": r.u8(0x4A93B), "b30": s32(r.u32(0x4AB30)), "t994": r.u16(0x4A994)}
        r.run(0x35E5E)
        log = hh.read(r)
        mod = scope_model(recs, ents, old, st)
        got_pix = [(e[1], e[2] if e[2] < 0x8000 else e[2] - 0x10000, e[3], e[4]) for e in log if e[0] == "sprite"]
        got_list = []
        a = base
        while a < base + 0x30:
            w = r.u16(a)
            if w == 0xFFFF: break
            got_list.append((w, r.u16(a + 2))); a += 4
        got = dict(pix=got_pix, list=got_list, b925=r.u8(0x4A925), b926=r.u8(0x4A926), cue=r.u32(0x4AC0C), hold=r.u8(0x4A93F), t927=r.u8(0x4A927),
                   cnt=r.u8(0x4A93B), flags=[r.u16(LIST + 16 * k + 12) for k in range(len(ents))],
                   w0=[r.u16(REC + 0x200 * i) for i in range(8)], b20=[r.u8(REC + 0x200 * i + 0x20) for i in range(8)])
        if got != mod:
            bad += 1
            if bad < 4:
                print("DIFF scope case %d" % it)
                for k in got:
                    if got[k] != mod[k]: print(" ", k, "got", got[k], "want", mod[k])
        stats["plots"] += len(mod["list"]); stats["erased"] += sum(1 for o in old if o[0] != 0xFFFF)
        stats["double"] += sum(1 for e in mod["list"] if e[0] & 0x8000)
        stats["threat"] += 1 if mod["b925"] else 0; stats["cues"] += 1 if mod["cue"] else 0
        stats["detected"] += sum(1 for b in mod["b20"] if b & 0x40)
        stats["limit"] += 1 if len(mod["list"]) >= 10 else 0
    print("part 2: %d scope passes, %s, %s" % (n, stats, "FAILED %d" % bad if bad else "ok"))
    return bad


def part2b(r, rng, n):
    """the cue dispatch alone: lists that produce chosen threat bit sets (friendly 2, hostile 4, cruise 8, missiles 0x10/0x20) against chosen previous sets"""
    bad = 0; seen = {}
    kinds = {2: (0x11, 0x11C8), 4: (0x12, 0x11C0), 8: (0x15, 0x11C0), 0x10: (0x00, 0x11C0), 0x20: (0x01, 0x11C0)}
    for it in range(n):
        setup(r); hh.install(r)
        bits = [b for b in kinds if rng.random() < 0.4]
        mode = rng.choice((3, 5))
        prev = rng.choice((0, 2, 4, 8, 0x0A, 0x0C, 0x0E, 0x06, 0x10, 0x20, 0x14))
        r.w8(0x4A95E, mode); r.w16(0x4A996, 0); r.w16(0x4A994, 0); r.w16(0x4A724, 0); r.w16(0x4AA40, 0); r.w16(0x4A990, 0)
        r.w8(0x4A8F0, 1); r.w8(0x4A93B, 0); r.w8(0x4A926, prev); r.w16(0x4AA78, 0xFFFF); r.w32(0x4AB30, 0); r.w32(0x4AC0C, 0)
        recs = {}
        for i in range(8):
            recs[i] = dict(w0=0, w2=0, b20=0, cls=0, tgt=0xFF, y=0)
            rec_set(r, i, 0, 0, 0, 0, 0xFF, 0)
        ents = []
        for k, b in enumerate(bits):
            cls, w0 = kinds[b]; i = k + 1
            recs[i] = dict(w0=w0, w2=0, b20=0, cls=cls, tgt=0x80, y=0)
            rec_set(r, i, w0, 0, 0, cls, 0x80, 0)
            ents.append((rng.randint(-3000, 3000), rng.randint(-3000, 3000), (i << 8) | 0x10))
        for k, (x, z, f) in enumerate(ents): r.poke(LIST + 16 * k, struct.pack(">iiiHH", x | 1, 0, z, f, 0))
        r.poke(LIST + 16 * len(ents), bytes(16))
        st = {"view": 0, "mode": mode, "page": 0, "nib": 13, "offx": 0, "offy": 0, "prev": prev, "t927": 0, "des": 0xFFFF, "b3b": 0, "b30": 0, "t994": 0}
        base = 0x537C8
        old = [(r.u16(base + 4 * k), r.u16(base + 4 * k + 2)) for k in range(11)]
        r.run(0x35E5E); hh.read(r)
        ents2 = [(x | 1, z, f) for (x, z, f) in ents]
        want = scope_model(recs, ents2, old, st)
        got = (r.u8(0x4A925), r.u8(0x4A926), r.u32(0x4AC0C), r.u8(0x4A93F))
        w = (want["b925"], want["b926"], want["cue"], want["hold"])
        key = (want["b925"], prev, want["cue"])
        seen[key] = 1
        if got != w:
            bad += 1
            if bad < 5: print("DIFF dispatch", bits, hex(prev), mode, got, w)
    print("part 2b: %d dispatch runs (%d distinct bit/previous/cue combinations), %s" % (n, len(seen), "FAILED %d" % bad if bad else "ok"))
    return bad


def part4(r, rng, n):
    """end to end on the placed mission-3 hostile (record 8): the builder 0x1FEE6, the walk 0x35E5E and a T request on the real records at random distances"""
    import combat
    bad = 0; seen = {"in": 0, "out": 0, "designated": 0, "detected_only": 0}
    for it in range(n):
        combat.stage(mode=3, silence=False)
        P = REC; H = REC + 0x200 * 8
        px = [s32(r.u32(P + o)) for o in (0x14, 0x18, 0x1C)]
        dist = int(2 ** rng.uniform(16, 28)); ang = rng.uniform(0, 6.2832)
        import math
        dx = int(dist * math.cos(ang)); dz = int(dist * math.sin(ang))
        nx = (px[0] + dx) & 0x3FFFFFFF; nz = (px[2] + dz) & 0x3FFFFFFF
        r.w32(H + 0x14, nx); r.w32(H + 0x1C, nz)
        r.w16(H + 6, nx >> 22); r.w16(H + 8, nz >> 22); r.w16(H + 0xC, (nx & 0x3FFFFF) >> 8); r.w16(H + 0xE, (nz & 0x3FFFFF) >> 8)
        r.w8(H + 0x20, 0)
        r.run(0x29D28); r.w8(0x4A83D, 0); r.w8(0x4A8EF, 2); r.w16(0x4A992, 0)
        r.run(0x1FEE6)
        r.w8(0x4A8F0, 1); r.w8(0x4A871, 1); r.w16(0x4AA78, 0xFFFF)
        # the list entry of record 8 as the builder wrote it
        ent = None
        for k in range(16):
            a = LIST + 16 * k
            if r.u32(a) | r.u32(a + 4) | r.u32(a + 8) == 0: break
            if (r.u16(a + 12) >> 8) == 8: ent = (s32(r.u32(a)), s32(r.u32(a + 8)))
        r.run(0x35E5E)
        x, z = ent
        nxw = -rnd13(x); nzw = -rnd13(z)
        inwin = -27 <= nxw <= 27 and -16 <= nzw < 22
        det = inwin and abs(x) <= 0x10000 and abs(z) <= 0x10000
        got = (bool(r.u8(H + 0x20) & 0x40), r.u16(0x4AA78) == 0x1000, bool(r.u8(0x4A925) & 4))
        want = (det, inwin, det)
        seen["in" if inwin else "out"] += 1; seen["designated"] += got[1]; seen["detected_only"] += det
        if got != want:
            bad += 1
            if bad < 5: print("DIFF end to end", dist, (x, z), got, want)
    print("part 4: %d placed-hostile runs, %s, %s" % (n, seen, "FAILED %d" % bad if bad else "ok"))
    return bad


def rnd13(v):
    return (v >> 13) + ((v >> 12) & 1)


def rndn(v, n):
    return (v >> n) + ((v >> (n - 1)) & 1) if n else v


def scope_model(recs, ents, old, st):
    recs = {i: dict(d) for i, d in recs.items()}
    view = st["view"]; mode = st["mode"]; page = st["page"]
    pix = []
    for (x, y) in old:
        if x == 0xFFFF: break
        if x & 0x8000: pix.append((2, x & 0x7FFF, y, 0))
        else: pix.append((1, x, y, 0))
    b925 = 0; t927 = st["t927"]; lst = []
    b3b = (st["b3b"] + 1) & 255
    flags = []
    limit = 0x537F0 if not page else 0x53818
    a2 = 0x537C8 + (0x28 if page else 0)
    for (x, z, flag) in ents:
        if (x | z) == 0 and False: break
        d3 = flag
        if not (d3 & 0x10):
            flags.append(d3 & ~0x20); continue
        i = (d3 >> 8)
        R = recs[i]
        skip = (i == view) or not (R["w0"] & 0x40) or (R["w2"] & 0x80)
        if skip:
            R["w0"] &= ~0x4000; flags.append(d3 & ~0x20); continue
        nx = -rnd13(x); nz = -rnd13(z)
        inwin = -27 <= nx <= 27 and -16 <= nz < 22
        if not inwin:
            R["w0"] &= ~0x4000; flags.append(d3 & ~0x20); continue
        if mode not in (0x7D, 2):
            if abs(x) > 0x10000 or abs(z) > 0x10000:
                R["b20"] &= ~0x40
            else:
                R["b20"] |= 0x40
                n = R["cls"] & 0xF0
                if n != 0x20:
                    if st["des"] != 0xFFFF and n == 0x10 and view == 0:
                        if t927 == 0: t927 = 0x1E
                        elif t927 < 0x80:
                            t927 -= 1
                            if t927 == 0: t927 = 0xFF
                    if not (R["b20"] & 2) and not (R["w0"] & 0x600) and n != 0x30:
                        if n == 0:
                            if not (R["w0"] & 8) and (R["tgt"] & 0x7F) == st["t994"]:
                                b925 |= 0x10 if R["cls"] == 0 else 0x20
                        else:
                            if R["w0"] & 8: b925 |= 2
                            elif R["cls"] == 0x15: b925 |= 8
                            else: b925 |= 4
        R["w0"] |= 0x4000
        px = -rndn(x, st["nib"]); pz = -rndn(z, st["nib"])
        if px > 27 or px < -27 or pz >= 22 or pz < -16:
            flags.append(d3 & ~0x20); continue
        sx = px + 0x9E + st["offx"]; sy = pz + 0xA7
        if not (0 <= sx < 0x140):
            flags.append(d3 | 0x20); continue
        off = i * 0x200
        if off == st["des"] and not (b3b & 1):
            flags.append(d3 | 0x20); continue
        dbl = not (-st["b30"] > R["y"])
        n = R["cls"] & 0xF0
        if n in (0x20, 0x30): pen = 5
        elif not (R["b20"] & 0x40) or (R["b20"] & 2): pen = 8
        elif R["w0"] & 8: pen = 4
        elif n == 0: pen = 2
        else: pen = 1
        if len(lst) < 10 and a2 + 4 * len(lst) < limit:
            yy = sy + st["offy"]
            if dbl: lst.append((sx & 0x7FFF | 0x8000, yy)); pix.append((2, sx, yy, pen))
            else: lst.append((sx & 0xFFFF, yy)); pix.append((1, sx, yy, pen))
        flags.append(d3 | 0x20)
    # cue dispatch
    cue = 0; hold = 0; prev = st["prev"]; d0 = b925
    if d0:
        if d0 & 0x10:
            if not (prev & 0x10): cue |= 1
        elif d0 & 0x20:
            if not (prev & 0x20): cue |= 1
        elif d0 & 8:
            if not (prev & 8): cue |= 2; hold = 0x18
            elif d0 & 4:
                if not (prev & 4): cue |= 2; hold = 0x18
            elif d0 & 2 and not (prev & 2) and mode != 5: cue |= 0x800
        elif d0 & 4:
            if not (prev & 4): cue |= 2; hold = 0x18
            elif d0 & 2 and not (prev & 2) and mode != 5: cue |= 0x800
        elif d0 & 2:
            if not (prev & 2) and mode != 5: cue |= 0x800
    return dict(pix=pix, list=lst, b925=b925, b926=d0, cue=cue, hold=hold, t927=t927, cnt=b3b, flags=flags,
                w0=[recs[i]["w0"] for i in range(8)], b20=[recs[i]["b20"] for i in range(8)])


def part3(r, rng, n):
    """target designation 0x362D8 (the T key request V:4A871): the cycle over the list"""
    bad = 0; st = {"first": 0, "next": 0, "wrap": 0, "none": 0, "notfound": 0, "skips": 0}
    for it in range(n):
        setup(r)
        for i in range(8):
            rec_set(r, i, 0x11C0, 0, rng.choice((0, 0, 0x02)), rng.choice((0x12, 0x13, 0x11, 0x10, 0x15, 0x00, 0x01, 0x20, 0x30, 0x14)), 0xFF, 0)
        ents = []
        for k in range(rng.randint(0, 9)):
            idx = rng.randint(1, 7)
            ents.append((rng.randint(-5000, 5000) | 1, rng.randint(-5000, 5000), (idx << 8) | rng.choice((0x30, 0x30, 0x10, 0x20, 0x00))))
        for k, (x, z, f) in enumerate(ents):
            r.poke(LIST + 16 * k, struct.pack(">iiiHH", x, 0, z, f, 0))
        r.poke(LIST + 16 * len(ents), bytes(16))
        des = rng.choice((0xFFFF, 0xFFFF)) if rng.random() < 0.3 else 0x200 * rng.randint(1, 7)
        r.w16(0x4AA78, des); r.w8(0x4A871, 1); r.w8(0x4A920, 0x77); r.w16(0x4AA7C, 0x0003); r.w8(0x4A93E, 0x55); r.w8(0x4A93F, 0x66)
        r.w16(0x4A986, 0xFFFF); r.w16(0x4AA56, 0)
        recs = [r.u8(REC + 0x200 * i + 0x62) for i in range(8)], [r.u8(REC + 0x200 * i + 0x20) for i in range(8)]
        r.run(0x362D8)
        # model
        def ok(e):
            x, z, f = e
            n_ = recs[0][(f >> 8)] & 0xF0
            return (f & 0x10) and (f & 0x20) and n_ not in (0, 0x20, 0x30) and not (recs[1][f >> 8] & 2)
        start = 0; notfound = False
        signed = des if des < 0x8000 else des - 0x10000
        if signed >= 0:
            pos = None
            for k, e in enumerate(ents):
                if (e[2] & 0xFF00) * 2 == des: pos = k; break
            if pos is None: notfound = True; start = 0
            else: start = pos + 1
            if pos is None and len(ents) == 0: pass
        want = dict(des=0xFFFF, i920=0, page=3, b93e=0x55, b93f=0x66, f986=0xFFFF)
        k = start
        while True:
            if k >= len(ents): break
            if ok(ents[k]):
                want = dict(des=(ents[k][2] & 0xFF00) * 2, i920=k + 1, page=1, b93e=1, b93f=0xFF, f986=0xDFFF); break
            k += 1
        if k >= len(ents) and True:
            want = dict(des=0xFFFF, i920=0, page=3, b93e=0x55, b93f=0x66, f986=0xFFFF)
        # entries scanned beyond 0x460/0x10 = 70 are not possible here (n <= 9)
        got = dict(des=r.u16(0x4AA78), i920=r.u8(0x4A920), page=r.u16(0x4AA7C), b93e=r.u8(0x4A93E), b93f=r.u8(0x4A93F), f986=r.u16(0x4A986))
        if r.u8(0x4A871) != 0: bad += 1; print("DIFF request not cleared")
        if (notfound and r.u16(0x4AA56) != 0x30) or (not notfound and r.u16(0x4AA56) != 0): bad += 1; print("DIFF notice", notfound, hex(r.u16(0x4AA56)))
        if got != want:
            bad += 1
            if bad < 5: print("DIFF designation", it, "des", hex(des), [(hex(e[2])) for e in ents], "got", got, "want", want)
        st["none" if want["des"] == 0xFFFF else ("first" if signed < 0 else "next")] += 1
        st["notfound"] += notfound
    print("part 3: %d designation requests, %s, %s" % (n, st, "FAILED %d" % bad if bad else "ok"))
    return bad


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print("skipped (missing: extracted/interceptor.bin or librig.so)"); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 300
    r = Rig(); rng = random.Random(seed)
    bad = part1(r, rng, n)
    bad += part2(r, rng, n)
    bad += part2b(r, rng, n)
    bad += part3(r, rng, n * 2)
    bad += part4(r, rng, n // 2)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
