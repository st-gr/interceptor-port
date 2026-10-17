"""Countermeasures (docs/re/07-ai-and-weapons.md, section 9): the key bodies of flare (F), chaff (C) and ECM (J), the dispenser inside the gun routine 0x172DC
(slot creation and the first steps of a flare or chaff), the decoy rule of the missile steering 0x2826A and the ECM rate divisor of the response routine 0x31D12,
run in the Musashi rig on the logged air scene against models written from the note.

    python3 cm_check.py [SEED] [N]

A. key bodies 0x1EE0C (flare), 0x1EE9E (chaff), 0x1EF1E (ECM): the count byte, the post code, the request byte V:4A943, the cool-down, the toggle.
B. dispenser: one call of 0x172DC with a request: the slot, the burst reservoir, the log counter, the sound call, and N further calls (life, velocity, position).
C. decoys: a hostile missile (record 9, target the player) with a flare or chaff slot present: its destination words, the near bit and the lost bit, per class.
D. ECM rate divisor: the response routine for a missile record with ECM off and on, the three divisors.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so, extracted/captures/logs/lists.txt.  Skips with exit 0 when one is missing.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from combat import *

SND = 0x7F0000


def u8(v): return v & 0xFF


def s8(v):
    v &= 0xFF
    return v - 256 if v & 0x80 else v


def check_keys(r, rng, n):
    bad = 0; seen = {}
    for it in range(n):
        stage(place=False)
        stub_sound(r, SND)
        kind = rng.choice(("flare", "chaff", "ecm"))
        cnt = rng.choice((0, 1, 2, 3, 15, 16, rng.randint(0, 20)))
        req = rng.choice((0, 1, 2)); cd = rng.choice((0, 5, 30)); ecm = rng.choice((0, 1))
        r.w8(0x4A905, cnt); r.w8(0x4A904, cnt); r.w8(0x4A943, req); r.w8(0x4A907, cd); r.w8(0x4A906, cd); r.w8(0x4A96D, ecm)
        r.w16(0x4AB98, 0); r.w8(0x4AA53, 0)
        entry = {"flare": 0x1EE0C, "chaff": 0x1EE9E, "ecm": 0x1EF1E}[kind]
        r.run(entry, d6=0, d0=0x41)
        got = (r.u8(0x4A905), r.u8(0x4A904), r.u8(0x4A943), r.u8(0x4A907), r.u8(0x4A906), r.u16(0x4AB98), r.u8(0x4A96D), r.u8(0x4A8F8), r.u8(0x4AA53))
        c2 = max(cnt - 1, 0) if cnt - 1 > 0 else 0
        want = [cnt, cnt, req, cd, cd, 0, ecm, 0, 0]
        if kind == "flare":
            want[8] = 0x02
            if cnt - 1 > 0: want[0] = cnt - 1; want[2] = 2; want[3] = 30; want[5] = 0x4026
            else: want[0] = 0; want[5] = 0x4027
        elif kind == "chaff":
            want[8] = 0x04
            if cnt - 1 > 0: want[1] = cnt - 1; want[2] = 1; want[4] = 30; want[5] = 0x4028
            else: want[1] = 0; want[5] = 0x4029
        else:
            want[8] = 0x08; want[7] = 3; want[6] = 0 if ecm else 1
        key = (kind, want[5])
        seen[key] = seen.get(key, 0) + 1
        if tuple(want) != got:
            bad += 1
            if bad < 6: print("DIFF key %s cnt %d got %s want %s" % (kind, cnt, got, tuple(want)))
    print("keys: %d cases, outcomes %s, %s" % (n, sorted(("%s/%04X" % k, v) for k, v in seen.items()), "FAILED %d" % bad if bad else "0 differences"))
    return bad


POOL = 0x4AD2A


def slot_addr(i): return POOL + 64 * i


def norm(r, length, x, y, z):
    """the original's normaliser 0x29CB0 (length, x, y, z as longs): the three result words at V:4AB04."""
    call(r, 0x29CB0, length, x, y, z)
    return [s16(r.u16(0x4AB04 + 2 * k)) for k in range(3)]


def model_create(r, kind, i):
    """the slot of a flare (kind 'flare') or chaff, as 0x177D8 and 0x17AFE make it and one pass of 0x2ADDE leaves it, from the player record in the rig
    (read before the call).  Returns a dict of the compared fields."""
    P = REC(0)
    m = [s16(r.u16(P + o)) for o in (0x92, 0x94, 0x96, 0x98, 0x9A, 0x9C, 0x9E, 0xA0, 0xA2)]
    lx, ly, lz = (0, -8, 0) if kind == "flare" else (0, -8, -56)
    off = [(m[3 * k] * lx + m[3 * k + 1] * ly + m[3 * k + 2] * lz) >> 6 for k in range(3)]
    ay, az = -64, 0
    aim = [(m[3 * k + 1] * ay + m[3 * k + 2] * az) >> 4 for k in range(3)]
    pxs = [r.u32(P + 0x14), r.u32(P + 0x18), r.u32(P + 0x1C)]
    sx = (pxs[0] & 0x3FFFFF) + off[0]; sy = s32(pxs[1]) + off[1]; sz = (pxs[2] & 0x3FFFFF) + off[2]
    dx = ((pxs[0] & 0x3FFFFF) + aim[0] - sx) >> 8
    dy = (s32(pxs[1]) + aim[1] - sy) >> 8
    dz = ((pxs[2] & 0x3FFFFF) + aim[2] - sz) >> 8
    speed = 13 if kind == "flare" else 5
    if dx == 0 and dy == 0 and dz == 0: return dict(degenerate=True)
    u = norm(r, speed, dx, dy, dz)
    vel8 = [u[k] << 3 for k in range(3)]
    pv = [s32(r.u32(P + o)) for o in (0x3E, 0x42, 0x46)]
    vel8 = [vel8[k] + pv[k] for k in range(3)]
    vel1 = [u[k] + (pv[k] >> 3) for k in range(3)]
    return dict(pos=[sx, sy, sz], vel8=vel8, vel1=vel1)


def wrap(pos, cells):
    """the cell carry of 0x1754C: a coordinate outside 0..0x3FFFFF moves one cell (cells 0 to 127, wrapping)."""
    for k, c in ((0, 0), (2, 1)):
        if pos[k] < 0: pos[k] += 0x400000; cells[c] = (cells[c] - 1) & 0x7F if cells[c] > 0 else 0x7F
        elif pos[k] >= 0x400000:
            pos[k] -= 0x400000; cells[c] += 1
            if cells[c] > 0x7F: cells[c] = 0


def step_vel(kind, v):
    """the velocity update of 0x2ADDE for a flare or chaff slot (gravity 0x100 per call, the drag shifts)."""
    vx, vy, vz = v
    vy -= 0x100
    if kind == "flare":
        sh = 6
    else:
        vy += 0x100; sh = 2
        vy -= vy >> sh
    return [vx - (vx >> sh), vy, vz - (vz >> sh)]


def clear_pool(r):
    r.poke(POOL, bytes(20 * 64))


def check_dispenser(r, rng, n, calls=6):
    bad = 0; seen = {}
    for it in range(n):
        stage(place=False)
        log = stub_sound(r, SND)
        clear_pool(r)
        P = REC(0)
        # random attitude matrix words and velocity (any words: the rules are plain arithmetic)
        for o in (0x92, 0x94, 0x96, 0x98, 0x9A, 0x9C, 0x9E, 0xA0, 0xA2):
            r.w16(P + o, rng.choice((0, 0x4000, -0x4000, rng.randint(-0x4000, 0x4000))))
        for o in (0x3E, 0x42, 0x46):
            r.w32(P + o, rng.randint(-3000, 3000))
        r.w32(P + 0x14, rng.randint(0x1000000, 0x2000000)); r.w32(P + 0x18, rng.randint(0x100000, 0x200000)); r.w32(P + 0x1C, rng.randint(0x1000000, 0x2000000))
        req = rng.choice((1, 2)); kind = "chaff" if req == 1 else "flare"
        nib = rng.choice((0, 1, 5, 15)); rounds = rng.choice((0, 1, 2, 500))
        busy = rng.choice((0, 0, 1, 2, 10))                  # the first `busy` even slots are taken by plain tracers
        r.w8(P + 0x7D, (nib << 4) | rng.randint(0, 15)); r.w16(P + 0x60, rounds)
        tmr = rng.choice((0, 1, 2, 0x14)); r.w8(P + 0x3B, tmr)
        for k in range(busy):
            a = slot_addr(2 * k); r.w16(a + 0x26, 0x0401); r.w16(a + 0x28, 100); r.w16(a + 0x2E, 0)
            r.w32(a + 4, 0x100000)
        r.w8(0x4A943, req); r.w8(0x4A944, 0); r.w8(0x4A945, 0); r.w8(0x4A865, rng.choice((0, 0, 1))); r.w16(0x4A97E, 0)
        r.w8(0x4A866, 0); r.w8(0x4A875, 0)
        r.w16(0x4B29C, rounds)
        before_log = r.u16(LOGBUF + 0x3A); e7d = r.u8(P + 0x7D); snd_off = r.u8(0x4A865)
        try:
            mdl = model_create(r, kind, 2 * busy) if (nib and rounds >= 1) else None
        except ZeroDivisionError:
            continue
        if mdl is not None and mdl.get("degenerate"): continue
        rc = r.run(0x172DC)
        if rc != 0: print('rig error', rc)
        i = 2 * busy
        a = slot_addr(min(i, 19))
        fired = bool(nib and rounds >= 1 and busy < 10)
        got = dict(flag=r.u16(a + 0x26), life=s16(r.u16(a + 0x28)), owner=r.u16(a + 0x2E), req=r.u8(0x4A943), f44=r.u8(0x4A944), f45=r.u8(0x4A945),
                   e7d=r.u8(P + 0x7D), rounds=r.u16(0x4B29C), log=r.u16(LOGBUF + 0x3A), snd=r.u32(log), a87d=r.u8(0x4A87D))
        want = dict(flag=0x401 | (0x1000 if kind == "flare" else 0x2000) if fired else 0, life=(30 if kind == "flare" else 60) if fired else 0, owner=0,
                    req=0 if fired else req, f44=(i + 1) if fired and kind == "flare" else 0, f45=(i + 1) if fired and kind == "chaff" else 0,
                    e7d=((e7d - 0x10) & 0xFF) if fired else e7d, rounds=rounds, log=before_log + (3 if fired else 0), snd=1 if (fired and not snd_off) else 0,
                    a87d=1 if fired else r.u8(0x4A87D))
        if not fired:
            want["flag"] = got["flag"] if busy >= 10 else 0
            want["life"] = got["life"]; want["owner"] = got["owner"] if busy >= 10 else 0
            anyc = busy < 10 and s8(tmr - 1) > 0
            if not anyc: want["e7d"] = (e7d & 0x0F) | 0x50
        ok = got == want
        seen[(kind, fired)] = seen.get((kind, fired), 0) + 1
        if fired and ok:
            # the state after the creation call: first pass of the mover (velocity updated, no displacement)
            v8 = step_vel(kind, mdl["vel8"]); v1 = mdl["vel1"]
            gv8 = [s32(r.u32(a + o)) for o in (0xC, 0x10, 0x14)]; gp = [s32(r.u32(a + o)) for o in (0, 4, 8)]; g1 = [s32(r.u32(a + o)) for o in (0x18, 0x1C, 0x20)]
            gc = (r.u16(a + 0x24), r.u16(a + 0x30), r.u16(a + 0x32))
            wp = list(mdl["pos"]); cells = [r.u16(0x4B242), r.u16(0x4B244)]
            wrap(wp, cells)
            wc = (1, cells[0], cells[1])
            if (gv8, gp, g1, gc) != (v8, wp, v1, wc):
                ok = False
                got = dict(v8=gv8, p=gp, v1=g1, c=gc); want = dict(v8=v8, p=wp, v1=v1, c=wc)
            else:
                # further calls: life counts down, velocity and position follow the model until the slot ends
                pos = list(wp); vel = list(v8)
                for c in range(1, calls):
                    r.run(0x172DC)
                    vel = step_vel(kind, vel); pos = [pos[k] + vel[k] for k in range(3)]
                    wrap(pos, cells)
                    gv8 = [s32(r.u32(a + o)) for o in (0xC, 0x10, 0x14)]; gp = [s32(r.u32(a + o)) for o in (0, 4, 8)]
                    lf = s16(r.u16(a + 0x28))
                    if gp[1] < 0: break                           # the ground: the slot turns into the impact state (not modelled here)
                    if (gv8, gp, lf, [r.u16(a + 0x30), r.u16(a + 0x32)]) != (vel, pos, (30 if kind == "flare" else 60) - c, cells):
                        ok = False; got = dict(call=c, v=gv8, p=gp, life=lf); want = dict(v=vel, p=pos, life=(30 if kind == "flare" else 60) - c); break
        if not ok:
            bad += 1
            if bad < 6: print("DIFF dispenser %s busy %d nib %d rounds %d: got %s want %s" % (kind, busy, nib, rounds, got, want))
    print("dispenser: %d cases, %s, %s" % (n, sorted(("%s/%s" % k, v) for k, v in seen.items()), "FAILED %d" % bad if bad else "0 differences"))
    return bad


def make_missile(r, idx, cls, b38, life, mpos, w0=0x10C3, w2=0):
    a = REC(idx)
    r.poke(a, bytes(0x200))
    r.w16(a, w0); r.w16(a + 2, w2); r.w8(a + 0x62, cls); r.w8(a + 0x38, b38); r.w16(a + 0x4C, life); r.w16(a + 0x26, 0x14)
    r.w16(a + 0x6C, 0x2000); r.w16(a + 0x6E, 0x2000)
    put_at(r, idx, mpos)
    r.w16(a + 0x2C, 0xFFFF)
    return a


def check_decoys(r, rng, n):
    bad = 0; seen = {}
    for it in range(n):
        stage(place=False)
        clear_pool(r)
        cls = rng.choice((0, 1)); b38 = rng.choice((0x80, 0x80, 0x80, 0x88, 0xFF))
        if b38 == 0xFF: b38 = 0x80
        life = rng.randint(0x40, 0x90)
        w0 = rng.choice((0x10C3, 0x10C3, 0x10CB)) & ~0x2          # spawn flag clear: the steering of every step
        slots = {}
        for nm in ("f", "c"):
            if rng.random() < 0.7:
                k = rng.randint(0, 19); slots[nm] = k
        if len(slots) == 2 and slots["f"] == slots["c"]: slots["c"] = (slots["f"] + 1) % 20
        base = [rng.randint(0x1000000, 0x2000000) for _ in range(3)]
        for nm, k in slots.items():
            a = slot_addr(k)
            r.w16(a + 0x26, 0x401 | (0x1000 if nm == "f" else 0x2000)); r.w16(a + 0x28, 20)
            r.w32(a, rng.randint(0, 0x3FFFFF)); r.w32(a + 4, rng.randint(0x10000, 0x300000)); r.w32(a + 8, rng.randint(0, 0x3FFFFF))
            r.w16(a + 0x30, rng.randint(1, 100)); r.w16(a + 0x32, rng.randint(1, 100))
        r.w8(0x4A944, slots["f"] + 1 if "f" in slots else 0); r.w8(0x4A945, slots["c"] + 1 if "c" in slots else 0)
        # the missile near the chosen decoy (or anywhere)
        use = slots.get("f" if cls == 0 else "c")
        if use is not None:
            a = slot_addr(use)
            ax = (r.u16(a + 0x30) << 22) + r.u32(a); ay = s32(r.u32(a + 4)); az = (r.u16(a + 0x32) << 22) + r.u32(a + 8)
            sz = rng.choice((0x1000, 0x4800, 0x4801, 0x8000))
            mpos = [ax + rng.randint(-sz, sz), ay + rng.randint(-sz, sz), az + rng.randint(-sz, sz)]
        else:
            mpos = [base[0], base[1], base[2]]
        w2 = rng.choice((0, 1)); b20 = rng.choice((0, 0x20))
        M = make_missile(r, 9, cls, b38, life, mpos, w0, w2)
        r.w8(M + 0x20, b20)
        # a record 8 to be a plausible target for b38 = 0x88
        r.w16(REC(8), 0x11C0); put_at(r, 8, [base[0] + 5000, base[1], base[2]])
        r.w16(0x4AA6C, 9); r.w16(0x4AA6E, 0x1200); r.w8(0x4A866, 0); r.w8(0x4A840, 0)
        r.run(0x278F6, a1=M)
        got = [r.s16(M + o) for o in (0x2C, 0x2E, 0x30, 0x32)] + [r.s32(M + 0x34), r.u8(M + 0x20) & 0x20, r.u8(M + 3) & 1, r.u16(M) & 1]
        # model
        if b38 & 0x7F == 0 and use is not None:
            a = slot_addr(use)
            cx, cz = r.u16(a + 0x30), r.u16(a + 0x32)
            sx, sy, sz_ = r.u32(a), s32(r.u32(a + 4)), r.u32(a + 8)
            ax = ((cx << 22) + sx) & 0xFFFFFFFF; az = ((cz << 22) + sz_) & 0xFFFFFFFF
            fx = s16((sx - (sx >> 31 << 32 if False else 0)) >> 8) if False else s16(s32(sx) >> 8)
            fz = s16(s32(sz_) >> 8)
            want = [s16(cx), s16(cz), fx, fz, sy >> 8]
            near = all(abs(s32(d)) <= 0x4800 for d in ((ax - mpos[0]) & 0xFFFFFFFF, (sy - mpos[1]) & 0xFFFFFFFF, (az - mpos[2]) & 0xFFFFFFFF))
            if near: w2b = 1; lost = b20 & 0x20
            else: w2b = w2; lost = 0x20 if w2 else (b20 & 0x20)
            want += [lost, w2b, 1]
            kind = "decoy-near" if near else "decoy-far"
        else:
            T = REC(b38 & 0x7F)
            want = [r.s16(T + 6), r.s16(T + 8), r.s16(T + 0xC), r.s16(T + 0xE), r.s32(T + 0x10), b20 & 0x20, w2, 1]
            kind = "record %d" % (b38 & 0x7F) if b38 & 0x7F else "player"
        seen[(kind, cls)] = seen.get((kind, cls), 0) + 1
        if got != want:
            bad += 1
            if bad < 6: print("DIFF decoy cls %d b38 %02X: got %s want %s" % (cls, b38, got, want))
    print("decoys: %d cases, %s, %s" % (n, sorted(("%s/c%d" % k, v) for k, v in seen.items()), "FAILED %d" % bad if bad else "0 differences"))
    return bad


def check_ecm(r, rng, n):
    bad = 0; seen = {}
    for it in range(n):
        stage(place=False)
        cls = rng.choice((0, 1)); b38 = rng.choice((0x80, 0x80, 0x81, 0xFF, 0x05, 0x00, 0x88))
        ecm = rng.choice((0, 1)); b20 = rng.choice((0, 0, 0x20)); l26 = rng.choice((-3, 0, 1, 4, 5, 6, 9, 10, 14, 15, 16, 20))
        M = make_missile(r, 9, cls, b38, 100, [0x1000000, 0x100000, 0x1000000])
        r.w8(M + 0x20, b20); r.w16(M + 0x26, l26 & 0xFFFF); r.w8(0x4A96D, ecm)
        cmd = [rng.choice((0, 1, -1, 7, 100, -100, rng.randint(-3000, 3000))) for _ in range(3)]
        old = [rng.choice((0, 5, -5, rng.randint(-3000, 3000))) for _ in range(3)]
        for k in range(3):
            r.w16(M + 0x50 + 2 * k, cmd[k]); r.w16(M + 0x56 + 2 * k, old[k])
        r.w16(0x4AA6C, 9); r.w16(0x4AA6E, 0x1200); r.w8(0x4A866, 0); r.w8(0x4A867, 0)
        r.run(0x31AE0, a1=M)
        got = [r.s16(M + 0x56 + 2 * k) for k in range(3)]
        idx = b38 & 0x7F
        if b20 & 0x20: d3 = 10; why = "lost"
        elif b38 & 0x80 and b38 != 0xFF and idx == 0 and ecm and cls != 1: d3 = 5; why = "ecm"
        else: d3 = l26; why = "life"
        if why == "life" and d3 > 15: d3 = 15
        want = []
        for k in range(3):
            q = cmd[k] if (why == "life" and d3 <= 0) else int(cmd[k] / d3)       # divs truncates toward zero
            d5 = s16(old[k] + q) >> 1
            want.append(s16(d5 + q) >> 1)
        seen[(why, d3 if why != "life" else (0 if d3 <= 0 else d3))] = 1 + seen.get((why, d3 if why != "life" else (0 if d3 <= 0 else d3)), 0)
        if got != want:
            bad += 1
            if bad < 6: print("DIFF ecm cls %d b38 %02X ecm %d b20 %02X l26 %d: got %s want %s" % (cls, b38, ecm, b20, l26, got, want))
    print("ecm divisor: %d cases, %d divisor kinds, %s" % (n, len(seen), "FAILED %d" % bad if bad else "0 differences"))
    return bad


def tdiv(a, b):
    """divs.w semantics for a 32-bit dividend and a 16-bit divisor: (quotient word) or None on overflow."""
    q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
    return q if -0x8000 <= q <= 0x7FFF else None


def impact_model(pos, vel):
    """0x2B3A2: the slot that went below the ground is put on it, x and z backed up along the velocity by the part of the last move below the ground."""
    vy8 = s16(vel[1] >> 8)
    out = list(pos)
    if vy8 == 0: return out
    y8 = s16(pos[1] >> 8)
    d3 = s16(-y8)
    for k in (0, 2):
        d0 = (vel[k] << 4)
        q = tdiv(d0, vy8)
        w = s16(q) if q is not None else s16(d0)
        out[k] = pos[k] + ((w * d3) >> 4)
    out[1] = 0
    return out


def check_impact(r, rng, n):
    bad = 0; imp = 0
    for it in range(n):
        stage(place=False); stub_sound(r, SND); clear_pool(r)
        P = REC(0)
        for o in (0x92, 0x94, 0x96, 0x98, 0x9A, 0x9C, 0x9E, 0xA0, 0xA2):
            r.w16(P + o, rng.choice((0x4000, -0x4000, rng.randint(-0x4000, 0x4000))))
        for o in (0x3E, 0x42, 0x46): r.w32(P + o, rng.randint(-4000, 4000))
        r.w32(P + 0x14, rng.randint(0x1000000, 0x2000000)); r.w32(P + 0x18, rng.randint(0x2000, 0x40000)); r.w32(P + 0x1C, rng.randint(0x1000000, 0x2000000))
        req = rng.choice((1, 2)); kind = "chaff" if req == 1 else "flare"
        r.w8(P + 0x7D, 0x50); r.w8(0x4A943, req); r.w16(0x4B29C, 500); r.w16(0x4A97E, 0)
        try:
            mdl = model_create(r, kind, 0)
        except ZeroDivisionError:
            continue
        if mdl.get("degenerate"): continue
        a = slot_addr(0)
        pos = list(mdl["pos"]); vel = list(mdl["vel8"]); cells = [r.u16(0x4B242), r.u16(0x4B244)]
        life = 30 if kind == "flare" else 60; state = "air"; ok = True
        for c in range(0, 70):
            r.run(0x172DC)
            if c: life -= 1
            if state == "air":
                if life <= 0:
                    if r.u16(a + 0x26) & 1: ok = False
                    break
                vel = step_vel(kind, vel)
                if c: pos = [pos[k] + vel[k] for k in range(3)]
                wrap(pos, cells)
                if pos[1] < 0:
                    pos = impact_model(pos, vel); wrap(pos, cells); state = "hit"; life = 2; imp += 1
                    if (r.u16(a + 0x26) & 3) != 3 or s16(r.u16(a + 0x28)) != 2: ok = False
            else:
                if life <= 0:
                    if r.u16(a + 0x26) & 1: ok = False
                    break
            gp = [s32(r.u32(a + o)) for o in (0, 4, 8)]
            if gp != pos or [r.u16(a + 0x30), r.u16(a + 0x32)] != cells:
                ok = False; print("DIFF impact", kind, c, state, gp, pos); break
        if not ok:
            bad += 1
            if bad > 5: break
    print("impact: %d cases, %d reached the ground, %s" % (n, imp, "FAILED %d" % bad if bad else "0 differences"))
    return bad


def main(argv):
    miss = inputs_missing()
    if miss: print("skipped (missing: %s)" % ", ".join(miss)); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 300
    r = rig(); rng = random.Random(seed)
    bad = check_keys(r, rng, n) + check_dispenser(r, rng, n) + check_decoys(r, rng, n) + check_ecm(r, rng, n) + check_impact(r, rng, n)
    print("cm_check:", "FAILED" if bad else "ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
