"""The draw program of the effect objects (docs/re/07-ai-and-weapons.md, section 11): the model streams of models 33, 34 and 35 (the starburst program at 0x40140:
op 70 timer skip, op 71 disc list, op 62 single discs) walked by the original's handlers in the rig and compared with the disc set the note states.

    python3 effects_stream_check.py [wrap]      (wrap: print the walk for the life words from 0x80 on)

For every object index 0 to 15 and every life word t from -4 to 0x7F (the creation value; a larger t is reached only after the 16-bit wrap, see the note) the object's life word (V:4D240 + 0x20 * index) is set, the program is walked from its first word
(0x40140) exactly as the drawer's loop does (a handler per item; the walk ends after the item with bit 14 of its operation word, or when the handler skips onto it), with the
disc routine 0x257B2 (entry of op 62 and 71) replaced by a logger of (slot pointer - 0x4D43C, pen V:4AA0C, size D6).  The log is compared with the model: for t < 0 nothing is
drawn; otherwise the variant k = (t & 7) >> 1 selects one of four copies (pens differ), and the discs are the six of the list (steady state, 0x0C <= t <= 0x74), the ascending
run from disc m = (0x0B - t) >> 1 (t < 0x0C: the base discs vanish first) or the descending run from disc m = (t - 0x74) >> 1 (t > 0x74: the column grows upward).
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
import primlog as P

PROG = 0x40140
SEC = 0x4D23C
TABLE = 0x23408
FRAME = 0x7F0200
STUB = 0x8A0000
LOGPTR, LOGBASE = P.LOGPTR, P.LOGBASE


def image():
    return open(IMGPATH, "rb").read()


def model(img, t):
    """the expected disc list [(slot offset, pen, size)] for the life word t; parsed from the stream bytes (the variants are 0x86 bytes each after the 2-byte head)"""
    if t < 0:
        return []
    k = (t & 7) >> 1
    base = PROG + 2 + 2 + 0x86 * k           # the op word of op 70 (2) and the NOP word (2) lie before the first variant
    w = lambda a: struct.unpack(">H", img[a - 0x10000:a - 0x10000 + 2])[0]
    lst = []
    a = base + 2                              # past the op 71 word
    for i in range(6):
        slot, pen, size = w(a), w(a + 2), w(a + 4)
        lst.append((slot, pen, size & 0x7FFF)); a += 6
    run1 = []                                  # six op 62 items ascending, then six descending
    for i in range(12):
        slot, pen, size = w(a + 2), w(a + 4), w(a + 6)
        run1.append((slot, pen, size)); a += 8
    asc, desc = run1[:6], run1[6:]
    if t < 0x0C:
        return asc[(0x0B - t) >> 1:]
    if t > 0x74:
        return desc[(t - 0x74) >> 1:]
    return lst


def main():
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print("skipped (missing: extracted/interceptor.bin or librig.so)"); return 0
    img = image()
    r = Rig(); r.reset()
    orig = [r.u32(TABLE + 4 * i) for i in range(78)]
    # the logger replaces the disc routine 0x257B2
    code = P.w(0x40E7, 0x2F08)
    code += P.append_header(9, None, [(2, ("reg", 0x0B)), (4, ("mem", 0x4AA0C)), (6, ("reg", 6))])
    code += P.w(0x205F, 0x44DF, 0x4E75)
    r.poke(STUB, code); r.poke(0x257B2, P.w(0x4EF9) + P.l(STUB))
    bad = 0; n = 0; discs = 0
    for idx in range(16):
        for t in range(-4, 0x80):
            r.w16(0x4AA6C, idx)
            r.poke(SEC + 32 * idx, bytes(32)); r.w16(SEC + 32 * idx + 4, t & 0xFFFF)
            r.w32(LOGPTR, LOGBASE)
            a2 = PROG
            guard = 0
            while guard < 40:
                guard += 1
                opw = r.u16(a2); last = bool(opw & 0x4000)
                op = (opw & 0x3FFF) >> 2
                r.run(orig[op], a2=a2 + 2, a6=FRAME)
                a2 = r.reg("a2")
                if last: break
            end = r.u32(LOGPTR); got = []; a = LOGBASE
            while a < end:
                got.append((r.u16(a + 2) - 0x4D43C & 0xFFFF, r.u16(a + 4), r.u16(a + 6))); a += 16
            # slot offsets in the stream are relative to 0x4D43C already: the logger holds A3 = 0x4D43C + word
            got = [(s, p, z) for (s, p, z) in got]
            exp = model(img, t)
            n += 1; discs += len(got)
            if got != exp:
                bad += 1
                if bad < 6: print("DIFF idx %d t %d got %s want %s" % (idx, t, got[:3], exp[:3]))
    print("effects_stream_check: %d walks, %d discs, %s" % (n, discs, "FAILED %d" % bad if bad else "0 differences"))
    return 1 if bad else 0


def wrap_table():
    """life words from 0x80 (reached only after the 16-bit wrap, about 68 minutes): the original's handlers walk the stream from the skip the formula gives; printed as
    (discs drawn, address where the walk ends, items walked); a walk that reaches 40 items (the guard) has left the stream (it ran on into other data)."""
    img = image(); r = Rig(); r.reset()
    orig = [r.u32(TABLE + 4 * i) for i in range(78)]
    code = P.w(0x40E7, 0x2F08)
    code += P.append_header(9, None, [(2, ("reg", 0x0B)), (4, ("mem", 0x4AA0C)), (6, ("reg", 6))])
    code += P.w(0x205F, 0x44DF, 0x4E75)
    r.poke(STUB, code); r.poke(0x257B2, P.w(0x4EF9) + P.l(STUB))
    rows = {}
    for t in list(range(0x80, 0x100)) + [0x180, 0x1FF, 0x300, 0x1000, 0x7FFF]:
        r.w16(0x4AA6C, 3); r.poke(SEC + 96, bytes(32)); r.w16(SEC + 96 + 4, t); r.w32(LOGPTR, LOGBASE)
        a2 = PROG; guard = 0
        while guard < 40:
            guard += 1
            opw = r.u16(a2); op = (opw & 0x3FFF) >> 2
            if op >= 78: break
            r.run(orig[op], a2=a2 + 2, a6=FRAME)
            a2 = r.reg("a2")
            if opw & 0x4000: break
        rows[t] = ((r.u32(LOGPTR) - LOGBASE) // 16, hex(a2), guard)
    return rows


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == 'wrap':
        for t, v in wrap_table().items(): print(hex(t), v)
        sys.exit(0)
    sys.exit(main())
