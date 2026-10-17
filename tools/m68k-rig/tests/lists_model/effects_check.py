"""Explosion effect records (docs/re/07-ai-and-weapons.md, section 11).

    python3 effects_check.py [SEED] [N]

Runs in the rig: (1) the effect spawner 0x2AA8E on random 32-byte record heads, with every combination of the free slots of the table at
0x4D23C and the argument D1: the first free slot (bit 6 of the byte at +1 clear) receives the 32 bytes with the word at +0 replaced by 0x0040, the word at
+4 by 0x007F and the word at +2 by D1 (or, when D1 is 0, by 0x12 for an even slot number and 0x13 for an odd one counted from the table start... see the
note), and V:4A910 becomes 0xFF; with all 16 slots in use nothing is written and V:4A910 is unchanged. (2) The op 70 stream skip 0x23588 for every
timer value -4 .. 0x80 and every secondary index 0 .. 15: the stream pointer A2 moves by the skip stated in section 11 (none for a negative timer).
(3) The cone handlers 0x2699E and 0x26A3E: the detail-list pointers written to V:4AAEE/V:4AAF2 for the phase counter +0x4C of -3 .. 8.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH

SEC = 0x4D23C
REC = 0x4B23C
TMP = 0x700000


def s16(v): return v - 0x10000 if v & 0x8000 else v


def op70_skip(t):
    if t < 0:
        return 0
    s = 2 + 0x86 * ((t & 7) >> 1)
    if t < 0xC:
        s += 0x26 + 8 * ((0xB - t) >> 1)
    elif t > 0x74:
        s += 0x56 + 8 * ((t - 0x74) >> 1)
    return s


def cone2(p):      # 0x2699E, models 2 and 13
    if p > 0:
        if p >= 5: return 0x41638
        return 0x41666 if p in (1, 2) else 0x41658
    return 0x41678 if p == 0 else 0x41674


def cone11(p, nib):   # 0x26A3E, model 11
    if p > 0:
        if p >= 5: return 0x418DE
        return 0x4190C if p in (1, 2) else 0x418FE
    if p < 0: return 0x4191A
    return 0x418DA if nib else 0x4191A


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print("skipped (missing: extracted/interceptor.bin or librig.so)"); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 300
    rng = random.Random(seed); r = Rig(); r.reset(); bad = 0; chk = 0
    # (1) the spawner
    for it in range(n):
        r.reset()
        used = [rng.random() < 0.5 for _ in range(16)]
        if it % 7 == 0: used = [True] * 16
        if it % 11 == 0: used = [False] * 16
        for i in range(16):
            r.poke(SEC + 32 * i, bytes(32))
            if used[i]: r.w8(SEC + 32 * i + 1, 0x40 | rng.choice((0, 0x10)))
        head = bytes(rng.randrange(256) for _ in range(32))
        r.poke(TMP, head)
        d1 = rng.choice((0, 0x11, 0x12, 0x13, 0, 0))
        r.w8(0x4A910, 0x55)
        before = r.peek(SEC, 512)
        r.run(0x2AA8E, a1=TMP, d1=d1, d0=rng.randrange(256))
        after = r.peek(SEC, 512)
        chk += 1
        if all(used):
            ok = after == before and r.u8(0x4A910) == 0x55
        else:
            slot = used.index(False)
            exp = bytearray(before)
            body = bytearray(head)
            struct.pack_into(">H", body, 0, 0x40); struct.pack_into(">H", body, 4, 0x7F)
            mid = d1 if d1 else (0x13 if (15 - slot) & 1 else 0x12)
            struct.pack_into(">H", body, 2, mid)
            exp[32 * slot:32 * slot + 32] = body
            ok = after == bytes(exp) and r.u8(0x4A910) == 0xFF
        if not ok:
            bad += 1
            if bad < 6: print("DIFF spawn it=%d used=%s d1=%x" % (it, used, d1))
    # (2) op 70
    for idx in range(16):
        for t in range(-4, 0x81):
            r.reset()
            r.w16(0x4AA6C, idx)
            r.poke(SEC + 32 * idx, bytes(32)); r.w16(SEC + 32 * idx + 4, t)
            r.run(0x23588, a2=0x40142)
            got = r.reg("a2") - 0x40142; chk += 1
            if got != op70_skip(t) or r.reg("d0") != 0:
                bad += 1
                if bad < 6: print("DIFF op70 idx=%d t=%d got %d want %d" % (idx, t, got, op70_skip(t)))
    # (3) the cone handlers (A1 is set by the handlers; the record is the one at V:4AA6E)
    for p in range(-3, 9):
        for nib in (0, 1, 5):
            for which, fn in ((0x2699E, cone2), (0x26A3E, None)):
                r.reset()
                r.w16(0x4AA6E, 0x200)
                r.poke(REC + 0x200, bytes(0x200))
                r.w16(REC + 0x200 + 0x4C, p); r.w8(REC + 0x200 + 0x7C, nib)
                # 0x21C44 is entered at the end; stub it with an rts so that only the list choice runs
                r.poke(0x21C44, b"\x4e\x75")
                r.run(which)
                want = cone2(p) if which == 0x2699E else cone11(p, nib & 0xF)
                a, b = r.u32(0x4AAEE), r.u32(0x4AAF2); chk += 1
                if a != want or b != want:
                    bad += 1
                    if bad < 6: print("DIFF cone %x p=%d nib=%d got %x/%x want %x" % (which, p, nib, a, b, want))
    # (4) the aircraft draw handler 0x26944: the crashed bit becomes the dying state (and drops the designation of the drawn record), an invalid record with a
    # negative counter and a state other than 5 becomes valid again; 0x21C44 is stubbed with an rts
    for it in range(1500):
        r.reset(); r.poke(0x21C44, b"\x4e\x75")
        idx = rng.choice((1, 4, 8, 14)); a = REC + 0x200 * idx
        w0 = rng.choice((0x11C0, 0x13C0, 0x11C0 | 0x200, 0x0080, 0x0280, 0x03C0, 0x0000, 0x8240))
        st = rng.choice((0, 1, 3, 5, 8)); cn = rng.choice((-5, -1, 0, 1, 20, 15))
        des = rng.choice((0xFFFF, 0x200 * idx, 0x200 * idx, 0x400, 0x0000))
        r.w16(0x4AA6E, 0x200 * idx); r.w16(0x4AA6C, idx); r.w16(a, w0); r.w8(a + 0x7A, st); r.w16(a + 0x4C, cn & 0xFFFF)
        r.w16(0x4AA78, des); r.w32(0x4AC08, 0xFFFFFFFF); r.w16(0x4AB98, 0)
        r.run(0x26944)
        e_w0, e_cn, e_des, e_ac08, e_post = w0, cn, des, 0xFFFFFFFF, 0
        if w0 & 0x40:
            if w0 & 0x200:
                e_cn = 15; e_w0 = (w0 & ~0x200 & 0xFFFF) | 0x400
                if des != 0xFFFF and des == 0x200 * idx:
                    e_post = 0x4016; e_des = 0xFFFF; e_ac08 = 0xFFFFFFFF & ~0x4200
        else:
            if st != 5 and cn < 0: e_w0 = w0 | 0x40
        got = (r.u16(a), s16(r.u16(a + 0x4C)), r.u16(0x4AA78), r.u32(0x4AC08), r.u16(0x4AB98))
        want = (e_w0, e_cn, e_des, e_ac08, e_post)
        chk += 1
        if got != want:
            bad += 1
            if bad < 6: print("DIFF handler 26944 w0 %04X st %d cn %d des %X: got %s want %s" % (w0, st, cn, des, got, want))
    print("effects_check: %d comparisons, %s" % (chk, "FAILED %d" % bad if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
