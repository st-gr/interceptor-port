#!/usr/bin/env python3
"""Note 07 12.5 (milestone 7): the gun-or-entity-pass order scan 0x2AD18 of the original, run on the states of a gun kill in the chase view.

    python gunorder_check.py [LOG]

LOG is the emulator log of tools/vamiga-ref/scripts/combat7-gunorder.txt (default extracted/captures/logs/combat7-gunorder.txt), which logs per
video frame the whole tracer pool (20 slots of 64 bytes), the camera longs, the player's record, the target record, the view byte and the
message variables. For every frame the scan runs in the rig on the frame's pool, camera, owner record and view byte (the rest of the state
is the free-flight dump if present, else zeros) and its answer (1 = the gun runs before the entity pass, 0 = after it) is printed beside the
frame's observation: the pool's active slots, the target's W0, the post code V:4AB98, the message latch V:4A984 and the designation V:4AA78.
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "vamiga-ref"))
from common import Rig, ROOT  # noqa: E402
import logparse  # noqa: E402


def scan(rig, pool, cam, r0, view):
    rig.poke(0x4AD2A, pool)
    rig.poke(0x4ACF6, cam)
    rig.poke(0x4B23C, r0)
    rig.w8(0x4A83D, view)
    rig.L.rig_call.restype = ctypes.c_uint32
    rig.L.rig_call.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_int]
    return rig.L.rig_call(0x2AD18, None, 0)


def main(argv):
    path = argv[1] if len(argv) > 1 else os.path.join(ROOT, "extracted", "captures", "logs", "combat7-gunorder.txt")
    if not os.path.exists(path) or not os.path.exists(os.path.join(ROOT, "extracted", "interceptor.bin")):
        print("gunorder_check: skipped (no log or image)")
        return 0
    L = logparse.load(path)
    rig = Rig()
    rig.reset()
    n = 0
    while "f%d_pool" % n in L:
        n += 1
    prev = None
    for f in range(n):
        pool, cam = L["f%d_pool" % f].data, L["f%d_cam" % f].data
        r0, view = L["f%d_r0" % f].data, L["f%d_view" % f].u8(0)
        r = scan(rig, pool, cam, r0, view)
        P = L["f%d_pool" % f]
        act = [(s, "%X" % P.u16(64 * s + 0x26), P.s16(64 * s + 0x28)) for s in range(20) if P.u16(64 * s + 0x26)]
        obs = "W0 %04X post %04X latch %04X designation %04X" % (L["f%d_r8" % f].u16(0), L["f%d_4AB98" % f].u16(0), L["f%d_4A984" % f].u16(0), L["f%d_4AA78" % f].u16(0))
        row = (r, act, obs)
        if row != prev:
            print("frame %2d scan %d  %s  slots %s" % (f, r, obs, act))
        prev = row
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
