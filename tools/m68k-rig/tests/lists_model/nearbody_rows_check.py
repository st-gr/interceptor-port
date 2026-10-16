"""The two near-body triangles in windows whose last row V:4AA3C is 166 to 179 (docs/re/03-3d-pipeline.md, "Where the near body falls").

    python3 nearbody_rows_check.py        (skips with exit 0 without extracted/interceptor.bin or librig.so)

The deck-state triangles T1 = (159,166) (159,179) (158,179) and T2 = (159,166) (156,179) (162,179) are handed to the filler 0x34A28 for each last row; the
custom-chip registers are plain RAM in the rig, so the check reads the blitter size word that the last blit left.  Expected, per the note:
 (1) a polygon whose top row is below the last row is rejected: nothing is drawn (last row 165 and 144);
 (2) the small-shape classification uses the UNCUT bounding box: for T1 (13 rows, one column wide) the line routine receives the uncut end points
     (158,166) to (159,179) for every last row from 166 up;
 (3) the line routine does the cutting: its blitter height is min(13, last - 166) steps (no blit at all for last row 166, 1 step at 167, 4 at 170, 13 at 179);
 (4) T2 is area-filled with its edges cut at the last row: the fill height is last - 165 rows for last rows 166 to 179 (1, 2, 5, 14), two words wide.
Pixels are not checked: the rig has no blitter."""
import ctypes, os, sys
from common import Rig, IMGPATH, LIBPATH
import primlog as P

T1 = [(159, 166), (159, 179), (158, 179)]
T2 = [(159, 166), (156, 179), (162, 179)]


def setup(r, poly, last):
    r.w16(0x4AA3C, last); r.w16(0x4AA0C, 0); r.w16(0x4A79E, 0xF); r.w16(0x4A7A0, 0xFFFF); r.w16(0x4A7A2, 0)
    r.w16(0x5043C, len(poly))
    for i, (x, y) in enumerate(poly): r.w16(0x5043E + 4 * i, x); r.w16(0x50440 + 4 * i, y)
    r.w16(0xDFF058, 0)


def run(poly, last, stub_line):
    r = Rig(); r.reset(); r.L.rig_set_max_instructions(3000000); P.clear_log(r)
    if stub_line:
        code = P.w(0x40E7, 0x2F08) + P.append_header(9, None, [(2, ("reg", 0)), (4, ("reg", 1)), (6, ("reg", 2)), (8, ("reg", 3))]) + P.w(0x205F, 0x44DF, 0x4E75)
        r.poke(0x8F0000, code); r.poke(0x3455E, P.w(0x4EF9) + P.l(0x8F0000))
    setup(r, poly, last)
    r.run(0x34A28)
    lines = [tuple(P.s16(v) for v in x[1:5]) for x in P.read_log(r) if x[0] == 9]
    return r.reg('d0') & 0xFFFF, lines, r.u16(0xDFF058)


def main():
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    bad = 0; n = 0
    def ck(ok, what):
        nonlocal bad, n
        n += 1
        if not ok: bad += 1; print('DIFF', what)
    for last in (144, 165, 166, 167, 170, 179, 190):
        d0, lines, _ = run(T1, last, True)
        ck(d0 == 1, ('T1 return', last, d0))
        ck(lines == ([] if last < 166 else [(158, 166, 159, 179)]), ('T1 line end points (uncut)', last, lines))
        d0, lines, size = run(T1, last, False)
        steps = 0 if last < 166 else min(13, last - 166)
        ck(size == (steps << 6 | 2 if steps else 0), ('T1 line height', last, size, steps))
        d0, lines, size = run(T2, last, False)
        rows = 0 if last < 166 else min(14, last - 165)
        ck(size == (rows << 6 | 2 if rows else 0), ('T2 fill height', last, size, rows))
    print('nearbody_rows: %d checks, %s' % (n, 'FAILED %d' % bad if bad else 'ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
