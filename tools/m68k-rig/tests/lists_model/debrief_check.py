"""The debrief list builder 0x131F4, the briefing list builder 0x29468 and the completion bookkeeping 0x134A0 (docs/re/08-game-flow.md, sections 13 and 14).

    python3 debrief_check.py [SEED] [N]

  1. 0x29468(mode, slot, index) for modes 3 to 8 and indices 0 to 5, slots 0 and 1, against the mission-list table read from the image;
  2. 0x131F4 in the rig over random states (mode 1 to 9 and 0x7D, code 0xFF, 0xFE, 0xFD, 0xFC, others, step-counter parity, a random log record
     behind the pointer V:1D024) against a model of the id list it writes (the failure-list quirk included), the code it leaves, V:4A8E3, the first
     word of the log for mode 9, and, for code 0xFC, the bookkeeping 0x134A0 (progress, last mission, old count, completed, the completion byte, V:4A95F, V:4A960).
     The disk writer 0x1858A is replaced by a return in the rig.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.  Run under ulimit -v 2500000; timeout 600.
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH

LOG = 0x90000          # the rig's log record buffer; V:1D024 points here


def s8(v): v &= 0xFF; return v - 256 if v & 0x80 else v


def table(r, mode, idx):
    off = r.u16(0x43EDA + 2 * (mode - 3))
    return r.u16(0x43DFE + off + 2 * idx)


def model(r, mode, code, parity, rec):
    """Returns (list words (8), code, e3, rec dict changes) for the state in r."""
    lst = [0x7777] * 8; lst[0] = 0; lst[1] = 0       # the reset clears the first two words
    p = 0; e3 = 0; ch = {}
    if code == 0xFF:
        if 3 <= mode <= 8:
            lst[0] = table(r, mode, 4); p = 1
            lst[1] = 0x8055 if mode == 3 else 0x8056; p = 2
        elif mode == 9:
            ch["w0"] = 1; lst[0] = 0x4A; lst[1] = 0x8053; p = 2; e3 = 1; code = 0xEF
        elif mode == 0x7D:
            lst[0] = 0x48; p = 1; code = 0xF0
        else:
            lst[0] = 0x41; p = 1
    elif code == 0xFE:
        m = mode - 4
        if 0 <= m < 4:
            if m == 0: lst[p] = 0x24 if parity else 0x25; p += 1
            elif m == 1: lst[p] = 0x2C; p += 1; code = 0xF0
            elif m == 2: lst[p] = 0x33; p += 1
            else:
                lst[p] = 0x39; p += 1
                if parity: lst[p] = 0x803A; p += 1; code = 0xF0
                else: lst[p] = 0x805C; p += 1
        else:
            lst[p] = 0x42; p += 1
        if code != 0xF0:
            p += 1; lst[p] = 0x8056; p += 1; lst[p] = 0x8053; p += 1
    elif code == 0xFD:
        if mode == 4: lst[p] = 0x4C; p += 1; code = 0xF0
    if code == 0xFC:
        lst[p] = 0x54; p += 1; lst[p] = 0x8053; p += 1
        # 0x134A0
        prog = s8(rec["prog_live"]); cnt_addr = 0x12 + mode
        ch["w2"] = prog & 0xFFFF if prog >= 0 else (prog & 0xFFFF)
        ch["b6"] = mode; ch["b7"] = rec["bytes"][cnt_addr]
        ch["w38"] = (rec["w38"] + 1) & 0xFFFF
        live = prog + 1
        if live > 3: live = 3
        ch["live"] = live & 0xFF
        c = (rec["bytes"][cnt_addr] + 1) & 0xFF
        if c > 3: c = 3
        ch["cnt"] = c
        ch["a960"] = live & 0xFF
    lst[p] = 0
    return lst, code, e3, ch


def vector_notice(rng, r, n):
    """0x29534 (the outcome lookup that decides whether the vector notice 0x58 is posted): found (0) or not (-1) over random sub-lists."""
    bad = 0; found = 0
    for _ in range(n):
        r.reset()
        sub = 0x91000; ents = []
        for _ in range(rng.randint(0, 5)):
            slot = rng.randint(0, 15)
            ents.append(slot)
            r.poke(sub + 10 * (len(ents) - 1), struct.pack(">HHHHH", 0x20 * rng.randint(0, 8), 0x10, slot | (rng.randint(0, 0x7F) << 8), 0, 0))
        r.poke(sub + 10 * len(ents), struct.pack(">H", 0xFFFF))
        r.w32(0x4A7F2, sub)
        want = -1
        for slot in ents:
            a = 0x4B23C + 0x200 * slot
            cls = rng.choice((0x10, 0x12, 0x15, 0x20, 0x32, 0x14))
            r.w8(a + 0x62, cls); r.w8(a + 1, rng.choice((0x40, 0, 0x40)))
        for slot in ents:       # a later entry may re-draw the same slot: model reads the final memory
            pass
        for slot in ents:
            a = 0x4B23C + 0x200 * slot
            cls = r.u8(a + 0x62); w0 = r.u8(a + 1)
            if cls == 0x15: continue
            if (cls & 0xF0) != 0x10: continue
            if not (w0 & 0x40): continue
            want = 0; break
        r.run(0x29534)
        got = r.reg("d0"); got = got - (1 << 32) if got & 0x80000000 else got
        if want == 0: found += 1
        if got != want:
            bad += 1
            if bad < 4: print("DIFF 29534", ents, got, want)
    return bad, found


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print("skipped (missing: extracted/interceptor.bin or librig.so)"); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 2000
    rng = random.Random(seed); r = Rig(); bad = 0; cnt = 0; fc = 0
    # 1. the builder
    r.reset()
    for mode in range(3, 9):
        for idx in range(0, 6):
            for slot in (0, 1):
                r.reset()
                for i in range(6): r.w16(0x4A802 + 2 * i, 0x5555)
                r.w32(0xDFFF00, mode); r.w32(0xDFFF04, slot); r.w32(0xDFFF08, idx)   # the C arguments above the return slot
                r.run(0x29468)
                got = (r.u16(0x4A802 + 2 * slot), r.u16(0x4A802 + 2 * slot + 2))
                want = (table(r, mode, idx), 0)
                cnt += 1
                if got != want:
                    bad += 1
                    if bad < 6: print("DIFF 29468 mode", mode, "idx", idx, "slot", slot, got, want)
    # 2. the debrief
    for _ in range(n):
        r.reset()
        r.poke(0x1858A, b"\x4e\x75")
        mode = rng.choice((1, 2, 3, 4, 5, 6, 7, 8, 9, 0x7D))
        code = rng.choice((0xFF, 0xFF, 0xFE, 0xFE, 0xFD, 0xFC, 1, 2, 4, 0xF0))
        if code == 0xFC: mode = rng.randint(3, 8)
        parity = rng.randint(0, 1)
        rec = {"bytes": [rng.choice((0, 0, 1, 2, 3)) for _ in range(0x4E)], "w38": rng.randint(0, 65535), "prog_live": rng.randint(0, 3)}
        rec["bytes"][0] = rng.choice((0, 0)); rec["bytes"][1] = 0
        r.poke(LOG, bytes(rec["bytes"])); r.w16(LOG + 0x38, rec["w38"]); r.w32(0x1D024, LOG)
        r.w8(0x4A95E, mode); r.w8(0x4A850, code); r.w8(0x4A95F, rec["prog_live"])
        r.w16(0x4A992, 0x1230 | parity); r.w16(0x4AB8E, 0xFFFF); r.w8(0x4A8E3, 0); r.w8(0x4A84D, 1)
        for i in range(8): r.w16(0x4A802 + 2 * i, 0x7777)
        lst, ncode, e3, ch = model(r, mode, code, parity, rec)
        r.run(0x131F4, a6=0x70000)
        got = [r.u16(0x4A802 + 2 * i) for i in range(8)]
        diffs = []
        if got != lst: diffs.append(("list", [hex(x) for x in got], [hex(x) for x in lst]))
        if r.u8(0x4A850) != ncode: diffs.append(("code", r.u8(0x4A850), ncode))
        if r.u8(0x4A8E3) != e3: diffs.append(("e3", r.u8(0x4A8E3), e3))
        if r.u32(0x1A6BC) != 0x12EFE: diffs.append(("state", hex(r.u32(0x1A6BC))))
        if r.u8(0x4A84D) != 0: diffs.append(("gate", r.u8(0x4A84D)))
        if mode == 9 and code == 0xFF and r.u16(LOG) != 1: diffs.append(("w0", r.u16(LOG)))
        if code == 0xFC:
            fc += 1
            rb = r.peek(LOG, 0x4E)
            if struct.unpack(">h", rb[2:4])[0] != ch["w2"] - (65536 if ch["w2"] > 32767 else 0): diffs.append(("w2", rb[2:4].hex(), ch["w2"]))
            if rb[6] != ch["b6"]: diffs.append(("b6", rb[6], ch["b6"]))
            if rb[7] != ch["b7"]: diffs.append(("b7", rb[7], ch["b7"]))
            if struct.unpack(">H", rb[0x38:0x3A])[0] != ch["w38"]: diffs.append(("w38", rb[0x38:0x3A].hex(), ch["w38"]))
            if rb[0x12 + mode] != ch["cnt"]: diffs.append(("cnt", rb[0x12 + mode], ch["cnt"]))
            if r.u8(0x4A95F) != ch["live"]: diffs.append(("live", r.u8(0x4A95F), ch["live"]))
            if r.u8(0x4A960) != ch["a960"]: diffs.append(("a960", r.u8(0x4A960), ch["a960"]))
        cnt += 1
        if diffs:
            bad += 1
            if bad < 8: print("DIFF debrief mode %02x code %02x parity %d: %r" % (mode, code, parity, diffs))
    vb, vf = vector_notice(rng, r, 300); bad += vb
    print("vector notice lookup 0x29534: 300 sub-lists (%d with a qualifying aircraft), %s" % (vf, "FAILED %d" % vb if vb else "ok"))
    print("debrief_check: %d cases (%d with code 0xFC), %s" % (cnt, fc, "FAILED %d" % bad if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
