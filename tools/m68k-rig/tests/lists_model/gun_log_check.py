"""The gun routine 0x172DC and the launch counters of the flight log (docs/re/07-ai-and-weapons.md, sections 7 and 13.4).

    python3 gun_log_check.py [SEED] [N]

Part 1 (0x172DC, the gun/dispenser routine, called once per frame): random initial values of the burst counter E+0x7D (high nibble = shots left in the burst), the cool-down byte
E+0x3B, the rounds word E+0x60, the trigger bit 3 of V:4A97E, the dispenser request V:4A943 (0 none, 1 and 2) and the sound-off byte V:4A865; the activity of the ten slots
of the pool at 0x4AD2A (the even slots 0, 2 ... 18 of a 20-slot table of 64 bytes) is taken from the rig before the call.  Compared after one call (and over 24-call sequences with
the trigger held or released): the burst counter, the cool-down, the rounds, the log counter +0x3A, the flags V:4A87D and V:4A8FB, the slot that was taken and its flags,
V:4A944 / V:4A945 and V:4A943.  The rule is the one of section 7.
Part 2 (the launch counters, 0x275C4 through the world update): a free weapon record, the selection nibble of E+0x63, the stocks E+0x5F: the log counters +0x3E (AIM-120)
and +0x42 (AIM-9L), the stock nibbles and the class byte of the new record against the rule of section 8.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so, extracted/captures/logs/lists.txt.  Skips with exit 0 when one is missing.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, load_scene, inputs_missing
from combat import REC

POOL = 0x4AD2A
LOGBUF = 0x700000
E = 0x4B23C


def s8(v): v &= 0xFF; return v - 256 if v & 0x80 else v
def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v


def setup(r, rng, A, T, rounds, trig, req, snd, active):
    load_scene(r, "air")
    r.poke(LOGBUF, bytes(0x80)); r.w32(0x1D024, LOGBUF)
    r.w8(E + 0x7D, A); r.w8(E + 0x3B, T & 0xFF); r.w16(E + 0x60, rounds)
    r.w16(0x4A97E, 0x08 if trig else 0); r.w8(0x4A943, req); r.w8(0x4A865, snd)
    r.w8(0x4A866, 0); r.w8(0x4A875, 0)
    for i in range(20):
        a = POOL + 64 * i
        r.poke(a, bytes(64))
        if i % 2 == 0 and active[i // 2]:
            r.w16(a + 0x26, 0x0401); r.w16(a + 0x28, 0x40)     # an active slot with a long life (the update only moves it)
    r.w32(E + 0x14, 0x10000000); r.w32(E + 0x18, 0x40000000); r.w32(E + 0x1C, 0x10000000)   # high above the ground: the tracers do not end by a ground hit


def model_call(st, active):
    """one call of 0x172DC on the state dict st; active: list of ten booleans taken before the call; returns the slot index taken or None"""
    st = st
    st["T"] = s8(st["T"] - 1)
    fired = 0; anyf = 0; taken = None
    for k in range(10):
        trig_cond = ((not st["pause"]) and st["trig"]) or st["req"] != 0
        if trig_cond:
            if active[k]:
                continue
            if (not fired) and (st["A"] & 0xF0) != 0 and st["rounds"] >= 1:
                fired = 1; taken = k
                st["A"] = (st["A"] - 0x10) & 0xFF
                anyf = 1
                st["log3A"] = (st["log3A"] + 3) & 0xFFFF
                st["v87d"] = 1
                if st["req"] != 0:
                    if st["req"] == 1: st["v945"] = 2 * k + 1
                    else: st["v944"] = 2 * k + 1
                    st["req"] = 0
                else:
                    st["rounds"] = max(0, st["rounds"] - 3)
                    st["v8fb"] = 3
                    st["T"] = 0x14
                continue
            if st["T"] > 0: anyf = 1
    if not anyf:
        st["T"] = 0; st["A"] = (st["A"] & 0xF) | 0x50
    return taken


def main(argv):
    miss = inputs_missing()
    if miss: print("skipped (missing: %s)" % ", ".join(miss)); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 400
    rng = random.Random(seed); r = Rig(); bad = 0; calls = 0; shots = 0; disp = 0
    for it in range(n):
        A = rng.choice((0x00, 0x10, 0x20, 0x50, 0x54, 0x04, 0x30)); T = rng.choice((-3, 0, 1, 5, 0x14)); rounds = rng.choice((0, 1, 2, 3, 4, 100, 500))
        trig = rng.random() < 0.7; req = rng.choice((0, 0, 0, 1, 2)); snd = rng.choice((0, 1))
        active = [rng.random() < 0.3 for _ in range(10)]
        setup(r, rng, A, T, rounds, trig, req, snd, active)
        st = dict(A=A, T=s8(T), rounds=rounds, trig=trig, req=req, pause=False, log3A=0, v87d=0, v8fb=0, v944=0, v945=0)
        for call in range(rng.choice((1, 1, 24))):
            act = [bool(r.u16(POOL + 64 * (2 * k) + 0x26) & 1) for k in range(10)]
            if call == 6 and st["req"] == 0: pass
            if call and rng.random() < 0.2:
                st["trig"] = not st["trig"]; r.w16(0x4A97E, 0x08 if st["trig"] else 0)
            taken = model_call(st, act)
            r.run(0x172DC)
            calls += 1
            for k in range(10):                      # a tracer that ends clears the dispenser pointers that name its slot
                if act[k] and not (r.u16(POOL + 64 * (2 * k) + 0x26) & 1):
                    if st["v944"] == 2 * k + 1: st["v944"] = 0
                    if st["v945"] == 2 * k + 1: st["v945"] = 0
            if taken is not None:
                shots += 1
                if st["req"] == 0 and (r.u8(0x4A944) or r.u8(0x4A945)): disp += 1
            got = (r.u8(E + 0x7D), s8(r.u8(E + 0x3B)), r.u16(E + 0x60), r.u16(LOGBUF + 0x3A), r.u8(0x4A87D), r.u8(0x4A8FB), r.u8(0x4A944), r.u8(0x4A945), r.u8(0x4A943))
            want = (st["A"], st["T"], st["rounds"], st["log3A"], st["v87d"], st["v8fb"], st["v944"], st["v945"], st["req"])
            if got != want:
                bad += 1
                if bad < 6: print("DIFF it %d call %d: got %s want %s taken %s" % (it, call, got, want, taken))
                break
            if taken is not None:
                fl = r.u16(POOL + 64 * (2 * taken) + 0x26)
                if (fl & 0x401) != 0x401 or (st["v944"] and False):
                    bad += 1; print("DIFF slot flags %04X taken %d" % (fl, taken)); break
    print("gun_log_check part 1: %d calls, %d shots/dispensings, %s" % (calls, shots, "FAILED %d" % bad if bad else "0 differences"))
    # part 2: the launch counters
    from combat import stage, world
    b2 = 0; launches = 0; refused = 0
    for it in range(max(60, n // 4)):
        r = stage(mode=3, silence=False)
        sel = rng.choice((0x20, 0x30, 0x20, 0x30, 0x40)); stock = rng.choice((0x00, 0x04, 0x40, 0x44, 0x24, 0x20, 0x02, 0x13, 0x31))
        r.w8(E + 0x63, sel | 0x0D); r.w8(E + 0x5F, stock); r.w16(0x4AA78, 0xFFFF)
        r.poke(LOGBUF, bytes(0x80)); r.w32(0x1D024, LOGBUF); r.w8(0x4A872, 1)
        world(r)
        have = (stock & 0xF) if sel == 0x30 else (stock & 0xF0)
        rec1 = REC(1)
        if have == 0:
            refused += 1
            want = (stock, 0, 0, False)
        else:
            launches += 1
            nstock = ((stock & 0xF0) | ((stock & 0xF) - 1)) if sel == 0x30 else ((stock - 0x10) & 0xFF)
            want = (nstock, 1 if sel == 0x30 else 0, 1 if sel != 0x30 else 0, True)
        got = (r.u8(E + 0x5F), r.u16(LOGBUF + 0x3E), r.u16(LOGBUF + 0x42), bool(r.u16(rec1) & 0x40))
        if got != want or (got[3] and r.u8(rec1 + 0x62) != (1 if sel == 0x30 else 0)):
            b2 += 1
            if b2 < 6: print("DIFF launch sel %02X stock %02X: got %s want %s class %02X" % (sel, stock, got, want, r.u8(rec1 + 0x62)))
    print("gun_log_check part 2: %d launches, %d refused (no stock), %s" % (launches, refused, "FAILED %d" % b2 if b2 else "0 differences"))
    return 1 if (bad or b2) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
