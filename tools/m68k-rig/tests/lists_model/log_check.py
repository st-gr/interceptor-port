"""Flight-log screen figures and the briefing date (docs/re/08-game-flow.md, sections 10.4 and 10.5).

    python3 log_check.py [SEED] [N]

The original's log-screen fill routine 0x2934E (twelve numbers into the text lines of the log screen) and the date routine 0x292F0 are run in the rig on N random
78-byte player records and compared with the stated rules: every figure is the low n decimal digits of the field (n = 3, 3, 5, 4, 5, 4, 5, 4, 4 for the nine word
fields +0x36 .. +0x44, 4 for +0x46 and +0x48, hours 4, minutes 2), leading zeros replaced by spaces except the last digit; hours = seconds / 3600 and minutes =
(seconds mod 3600) / 60 for the long at +0x08; the date: the month is one of four names by min(hours, 127) >> 5 and the day is (hours & 31) + 1 limited to 30.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH

PTR = 0x1D024          # V:1D024, the pointer to the record
BUF = 0x700000         # the record buffer in the rig
# (field offset in the record, text-line base address, offset D3, digits) as read from 0x2934E
FIELDS = [(0x36, 0x4464C, 0x25, 3), (0x38, 0x44677, 0x25, 3), (0x3A, 0x446CD, 0x17, 5), (0x3C, 0x446CD, 0x20, 4), (0x3E, 0x446F5, 0x17, 5),
          (0x40, 0x446F5, 0x20, 4), (0x42, 0x4471D, 0x17, 5), (0x44, 0x4471D, 0x20, 4), (0x46, 0x44773, 0x17, 4), (0x48, 0x44791, 0x17, 4)]
HOURS = (0x44749, 0x17, 4)
MINUTES = (0x44749, 0x21, 2)
# the four month names, read from the image at run time: nine bytes each from 0x292CC, stored reversed in the code (nothing is committed here)
_IMG = open(IMGPATH, "rb").read() if os.path.exists(IMGPATH) else b""
MONTHS = [_IMG[0x292CC - 0x10000 + 9 * i:0x292CC - 0x10000 + 9 * i + 9][::-1] for i in range(4)]


def fmt(v, n):
    s = ("%d" % v)[-n:].rjust(n, "0")
    body = list(s)
    for i in range(n - 1):
        if body[i] == "0":
            body[i] = " "
        else:
            break
    return "".join(body).encode()


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print("skipped (missing: extracted/interceptor.bin or librig.so)"); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 400
    r = Rig(); r.reset(); rng = random.Random(seed); bad = 0; checked = 0
    for it in range(n):
        rec = bytearray(78)
        for off in range(0x36, 0x4A, 2):
            v = rng.choice((0, 1, 9, 10, 99, 100, 999, 1000, 9999, 10000, 65535, rng.randint(0, 65535), rng.randint(0, 999)))
            struct.pack_into(">H", rec, off, v)
        secs = rng.choice((0, 59, 60, 3599, 3600, rng.randint(0, 40000), rng.randint(0, 600000), rng.randint(0, 10000000)))
        struct.pack_into(">I", rec, 8, secs)
        r.poke(BUF, bytes(rec)); r.w32(PTR, BUF)
        r.run(0x2934E)
        for off, base, d3, nd in FIELDS:
            v = struct.unpack_from(">H", rec, off)[0]
            got = r.peek(base + d3 + 1, nd); want = fmt(v, nd); checked += 1
            if got != want:
                bad += 1
                if bad < 6: print("DIFF field +0x%02X v=%d got %r want %r" % (off, v, got, want))
        hours, minutes = secs // 3600, (secs % 3600) // 60
        for (base, d3, nd), v in ((HOURS, hours), (MINUTES, minutes)):
            got = r.peek(base + d3 + 1, nd); want = fmt(v, nd); checked += 1
            if got != want:
                bad += 1
                if bad < 6: print("DIFF time secs=%d got %r want %r" % (secs, got, want))
        # the date
        r.run(0x292F0)
        h = min(hours, 0x7F)
        month = MONTHS[h >> 5]
        day = min((hours & 0x1F) + 1, 30)
        gm = r.peek(0x44E8A + 0x0C, 9); gd = r.peek(0x44E8A + 0x16, 2); checked += 2
        if gm != month:
            bad += 1
            if bad < 6: print("DIFF month hours=%d got %r want %r" % (hours, gm, month))
        if gd != fmt(day, 2):
            bad += 1
            if bad < 6: print("DIFF day hours=%d got %r want %r" % (hours, gd, fmt(day, 2)))
    print("log_check: %d records, %d comparisons, %s" % (n, checked, "FAILED %d" % bad if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
