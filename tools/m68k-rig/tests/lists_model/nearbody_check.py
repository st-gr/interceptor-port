"""The near body in the logged deck state (docs/re/03-3d-pipeline.md, "What the near body shows").

    python3 nearbody_check.py        (needs extracted/interceptor.bin, extracted/captures/logs/lists.txt and librig.so; skips with exit 0 without them)

The deck state of the emulator log is loaded, the queues are built by the original builder, the player's descriptor 0 is the F/A-18 template (model 4,
entity routine 0x21C44), the pitch band V:4A923 is set to 1, 2 and 3 in turn (alternate list, test 7 and 8 of 04a section 8a) and queue C is walked
with the polygon filler replaced by a stub that logs the screen polygon at 0x5043C and the pen.  Prints the polygons drawn in pen 0 per band; the
airborne state is run the same way.  The check passes when the deck state draws exactly the two triangles of the note in band 3 and, after an external view (V:4A8ED), in bands 1 to 3 (bands 1 and 2 otherwise: none, the eye
measure is -125, below -32, so the cockpit block is chosen), and the airborne state none.  Polygon records with a count of 0xFFFF are stale buffer reads of other callers."""
import os, sys, struct
from common import Rig, inputs_missing
import walk_check as W
import primlog as P
from ent_common import player_desc

EXPECT = [((159, 166), (159, 179), (158, 179)), ((159, 166), (156, 179), (162, 179))]


def install_filler_log(r):
    addr = P.STUBS + 0x30000
    code = P.w(0x40E7, 0x2F08)
    code += P.append_header(9, None, [(2, ("mem", 0x5043C))] + [(4 + 2 * k, ("mem", 0x5043E + 2 * k)) for k in range(6)])
    code += P.append_header(10, None, [(2, ("mem", 0x4AA0C))])
    code += P.w(0x205F, 0x44DF, 0x4E75)
    r.poke(addr, code); r.poke(0x34A28, P.w(0x4EF9) + P.l(addr))


def run(tag, band, after_external=0):
    r = Rig(); r.L.rig_set_max_instructions(3000000)
    W.scene(r, tag)
    player_desc(r, 4)
    r.w8(0x4A923, band); r.w8(0x4A83D, 0); r.w8(0x4A8ED, after_external)
    P.clear_log(r); install_filler_log(r); P.clear_log(r)
    W.walk(r, 'C')
    log = P.read_log(r)
    polys = []
    for i, rec in enumerate(log):
        if rec[0] == 9:
            pen = log[i + 1][1] & 0xF if i + 1 < len(log) and log[i + 1][0] == 10 else None
            n = rec[1]
            pts = [(rec[2 + 2 * k], rec[3 + 2 * k]) for k in range(min(n, 3))]
            polys.append((pen, n, tuple(pts)))
    return polys


def main():
    if inputs_missing():
        print('skipped (missing: %s)' % ', '.join(inputs_missing())); return 0
    bad = 0
    for tag in ('deck', 'air'):
        for band in (0, 1, 2, 3):
            for ext in (0, 1):                  # ext: V:4A8ED, the single pass after leaving an external view (test 6 of 04a section 8a)
                polys = run(tag, band, ext)
                pen0 = [p for p in polys if p[0] == 0]
                print('%-4s band %d%s: %d polygons, pen 0: %s' % (tag, band, ' after external view' if ext else '', len(polys), [p[2] for p in pen0]))
                got = sorted(p[2] for p in pen0 if p[1] == 3)
                if tag == 'deck':
                    # the near body is walked in band 3 (test 7), after an external view in every band (test 6), and in bands 1 and 2 only when the eye measure is -32 or above
                    # (test 8: the deck's is -125, so bands 1 and 2 take the cockpit block and draw nothing).  Op 4's gate: the deck record has both bits 6 and 7 of byte +4 set, so the
                    # height is V:4AB1E minus the term -(+0x4E) * 256 (112 on the deck: -3,080 instead of -31,752), which is above the 40 ft limit of step 5 in bands 1 to 3; band 0 is closed.
                    want = sorted(EXPECT) if (band == 3 or (ext and band >= 1)) else []
                    if got != want: bad += 1
                else:
                    if pen0: bad += 1
    print('nearbody:', 'FAILED' if bad else 'ok')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
