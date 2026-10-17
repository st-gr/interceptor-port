"""The briefing camera approach of the missions (docs/re/08-game-flow.md, section 13.4) run in the rig with the original's placement, target choice and mover.

    python3 approach_check.py [SEED]

For each mode 3 to 8 and each completion count 0 to 3 (variant 0 and, for mode 3, the other variants; no random offsets: V:4ABB0 = 0 and a nonzero
draw variant) the placement 0x2CE36 is run for real, then the approach target 0x2DA7C and the camera mover 0x2D756 once per pass, with the state
of the briefing chain (camera mode 2, view flags as 0x12780 leaves them), until the mover sets camera mode 3 and then 4.  NOTE: the rig has no draw, so this script FORCES the two passes of stage 7 (V:4A967 poked to 1 after the first mover pass); the real test is observed in six emulator runs.  With the argument novis the projection test of the draw (which sets V:4A967 to 1 when the chosen hostile is projected inside the window, observed in the emulator one pass after the target choice) is not applied.  Prints the pass counts
(the number of calls of the mover) and the chosen target; exit 1 only on a runaway (more than 5000 passes).
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.  Run under ulimit -v 2500000; timeout 900.
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH

LOG = 0x90000


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print("skipped (missing: extracted/interceptor.bin or librig.so)"); return 0
    r = Rig(); bad = 0
    visible = len(argv) < 2 or argv[1] != 'novis'
    print("mode sub variant target(record, class, counted) passes_to_mode3 passes_to_mode4")
    for mode in range(3, 9):
        for sub in range(4):
            for draw in ((0, 2, 3) if mode == 3 else (0,)):
                r.reset()
                for a in (0x321CA, 0x18E54):
                    r.poke(a, b"\x4e\x75")
                r.poke(LOG, bytes(0x4E)); r.w32(0x1D024, LOG)
                r.w8(LOG + 0x12 + mode, sub)
                r.w8(0x4A95E, mode); r.w16(0x4ABB0, draw); r.w8(0x4A8E3, 0); r.w8(0x4A95F, 0)
                r.run(0x2CE36)
                # the camera state the dispatch and the chain leave: map camera constants, external view, trail cleared
                r.w32(0x4ACF6, 0x10C00000); r.w32(0x4ACFA, 0x05000000); r.w32(0x4ACFE, 0x11400000)
                for i in range(3): r.w32(0x4AD02 + 4 * i, 0)
                r.w8(0x4A83D, 1); r.w8(0x4A865, 1); r.w8(0x4A866, 0); r.w8(0x4A86E, 0)
                r.run(0x2DA7C)
                tgt = r.u16(0x4AA72)
                cls = r.u8(0x4B23C + tgt + 0x62) if tgt else 0
                counted = not (r.u8(0x4B23C + tgt + 1) & 8) if tgt else False
                r.w8(0x4A966, 2); r.w8(0x4A86D, 1)
                p3 = p4 = None
                for n in range(1, 5001):
                    r.run(0x2D756)
                    if n == 1 and r.u8(0x4A967) == 0xFF and visible: r.w8(0x4A967, 1)    # the projection test of the draw (0x30092-0x300AC) fires after the first pass
                    cm = r.u8(0x4A966)
                    if cm == 3 and p3 is None: p3 = n
                    if cm == 4: p4 = n; break
                    if cm == 3 and p3 is not None and r.u8(0x4A86E) == 8 and r.u8(0x4A967) > 0x7F and False: pass
                if p4 is None: bad += 1
                print("%d %d %d rec %d class 0x%02x %s %s %s" % (mode, sub, draw, tgt // 0x200, cls, "counted" if counted else "not counted", p3, p4))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
