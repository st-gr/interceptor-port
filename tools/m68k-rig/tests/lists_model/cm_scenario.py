"""Single scenario runs for the note 07 section 9 text (not asserted): a hostile missile (record 9, target the player) launched 30 passes of the player's velocity behind
the player, with a flare (class 0) or chaff (class 1) released at pass K.  Prints, per K, whether the player was hit (W0 bit 0x8000 or the posted damage code) and the
minimum distance of the missile to the player.

    python3 cm_scenario.py      (run under ulimit -v 2500000 and timeout)
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from combat import *


def run(cls, release, behind=30, passes=40, ecm=0, speed=0x4200):
    r = stage(place=False); stub_sound(r)
    P = REC(0); M = REC(9)
    v = [s32(r.u32(P + o)) for o in (0x3E, 0x42, 0x46)]
    pp = pos(r, 0)
    r.poke(M, r.peek(P, 0xA4))
    r.w16(M, 0x10C3); r.w16(M + 2, 0); r.w8(M + 0x62, cls); r.w8(M + 0x38, 0x80); r.w16(M + 0x4C, 200 if cls else 150); r.w16(M + 0x26, 0x14)
    r.w8(M + 0x5E, 3); r.w8(M + 0x5D, 0xFF); r.w16(M + 0x6C, speed); r.w16(M + 0x6E, speed); r.w16(M + 0x2C, 0xFFFF)
    put_at(r, 9, [pp[k] - v[k] * behind for k in range(3)])
    r.w8(0x4A905, 16); r.w8(0x4A904, 16); r.w8(0x4A96D, ecm)
    best = 1 << 30; hit = None
    for i in range(passes):
        if release is not None and i == release:
            r.run(0x1EE0C if cls == 0 else 0x1EE9E, d6=0, d0=0x41)
        world(r); r.run(0x172DC)
        p = pos(r, 0); q = pos(r, 9)
        dist = max(abs((q[k] - p[k]) >> 8) for k in range(3))
        best = min(best, dist)
        if hit is None and r.u16(P) & 0x8000:
            hit = i
        if not r.u16(M) & 0x40: break
    return hit, best, r.u8(M + 0x20) & 0x20


if __name__ == "__main__":
    for behind in (45, 60):
        for cls in (0, 1):
            print("missile %d passes behind, class %d (%s)" % (behind, cls, "flare" if cls == 0 else "chaff"))
            for rel in (None, 0, 5, 10, 15, 20):
                hit, best, lost = run(cls, rel, behind=behind)
                print("  release at pass %-4s hit at pass %-4s closest %5d draw units, lost mark %s" % (rel, hit, best, "yes" if lost else "no"))
