"""Which request launches an AI missile in the whole world update (docs/re/07-ai-and-weapons.md, section 4.6): the owners in slots 4, 8, 10 and 12, with and
without the out-of-control bit, with the request byte pending (V:4A873 for slot 4's missile record 5, V:4A874 for the decision of records 9, 11, 13).

    python3 launch_order_check.py      (run under ulimit -v 2500000 and timeout)
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped (missing extracted/interceptor.bin or librig.so)"); sys.exit(0)
P = 0x4B23C
rig = Rig()
res = {}
for owner, missile, reqaddr in ((4, 5, 0x4A873), (8, 9, 0x4A874), (10, 11, 0x4A874), (12, 13, 0x4A874)):
    for oc in (0, 1):
        for req in (0, 1):
            rig.reset()
            for a in (0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x1F1D6, 0x1A0DC, 0x1A042): rig.poke(a, b'\x4e\x75')
            rig.w8(0x4A95E, 3); rig.w16(0x4A984, 0x0840); rig.w8(0x4A844, 1); rig.w32(0x1D024, 0x700000)
            for a in range(0x4A83C, 0x4A848):
                if a not in (0x4A83D, 0x4A83E, 0x4A840): rig.w8(a, 1)
            rig.w16(P, 0x11C8); rig.w8(P + 0x62, 0x11)
            H = P + 0x200 * owner
            rig.w16(H, 0x11C0); rig.w8(H + 0x62, 0x12); rig.w8(H + 0x5E, owner); rig.w8(H + 0x38, 0x80); rig.w8(H + 0x5D, 0xFF)
            rig.w8(H + 0x5F, 0x44); rig.w8(H + 0x63, 0x3D); rig.w16(H + 0x4C, 500)
            rig.w8(H + 0x20, 2 if oc else 0)
            rig.w16(H + 6, 70); rig.w16(H + 8, 70); rig.w32(H + 0x14, 70 << 22); rig.w32(H + 0x1C, 70 << 22); rig.w32(H + 0x18, 0x800000)
            for o in (0x92, 0x9A, 0xA2, 0x80, 0x88, 0x90): rig.w16(H + o, 0x4000)
            rig.w8(0x4A841, 1); rig.w8(0x4A95F, 0); rig.w16(0x4A992, 8)
            rig.w8(reqaddr, req)
            rig.w8(0x4A874 if reqaddr != 0x4A874 else 0x4A873, 0)
            rig.run(0x26AF8, a6=0x710000)
            M = P + 0x200 * missile
            res[(owner, oc, req)] = bool(rig.u16(M) & 0x40)
for owner in (4, 8, 10, 12):
    print("owner %2d: no request: %s | request, not out of control: %s | request, OUT OF CONTROL: %s" % (owner,
          res[(owner, 0, 0)], res[(owner, 0, 1)], res[(owner, 1, 1)]))

# One pass with both requests pending and all four owners able to fire (the script request V:4A873 and the hostile launch through V:4A874, in the same world update):
# the record order of the update decides (5 before 9, 11, 13); the first asker of 9, 11, 13 takes the global request (note 07 4.6).
def stage_all(oc_owner=None, req873=1, req874=1, frame=8):
    rig.reset()
    for a in (0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x1F1D6, 0x1A0DC, 0x1A042): rig.poke(a, b'\x4e\x75')
    rig.w8(0x4A95E, 3); rig.w16(0x4A984, 0x0840); rig.w8(0x4A844, 1); rig.w32(0x1D024, 0x700000)
    for a in range(0x4A83C, 0x4A848):
        if a not in (0x4A83D, 0x4A83E, 0x4A840): rig.w8(a, 1)
    rig.w16(P, 0x11C8); rig.w8(P + 0x62, 0x11)
    for owner in (4, 8, 10, 12):
        H = P + 0x200 * owner
        rig.w16(H, 0x11C0); rig.w8(H + 0x62, 0x12); rig.w8(H + 0x5E, owner); rig.w8(H + 0x38, 0x80); rig.w8(H + 0x5D, 0xFF)
        rig.w8(H + 0x5F, 0x44); rig.w8(H + 0x63, 0x3D); rig.w16(H + 0x4C, 500)
        rig.w8(H + 0x20, 2 if owner == oc_owner else 0)
        rig.w16(H + 6, 70); rig.w16(H + 8, 70); rig.w32(H + 0x14, (70 << 22) + owner * 0x100000); rig.w32(H + 0x1C, 70 << 22); rig.w32(H + 0x18, 0x800000)
        for o in (0x92, 0x9A, 0xA2, 0x80, 0x88, 0x90): rig.w16(H + o, 0x4000)
    rig.w8(0x4A841, 1); rig.w8(0x4A95F, 0); rig.w16(0x4A992, frame)
    rig.w8(0x4A873, req873); rig.w8(0x4A874, req874)

print("one pass, both requests pending (frame counter 8: the rate gate of progress 0 is open):")
for oc in (None, 8):
    stage_all(oc)
    rig.run(0x26AF8, a6=0x710000)
    got = [m for m in (5, 9, 11, 13) if rig.u16(P + 0x200 * m) & 0x40]
    print("  owner 8 %s: launched records %s; V:4A873 after %d, V:4A874 after %d" % ("out of control" if oc else "able", got, rig.u8(0x4A873), rig.u8(0x4A874)))
