#!/usr/bin/env python3
"""The landmark junk-model and beyond-hunk cases of note 07 8.2 (b) and (c), run with the whole machine state of a free flight in the rig.

    python landmark_state_check.py [--limit N] [--cases FILE]

Input: extracted/captures/state-flight.bin (state_dump.py, from the emulator script tools/vamiga-ref/scripts/combat7-statedump.txt), the
state hunks at their flat addresses and the machine's RAM at its real addresses; extracted/interceptor.bin (the image). Both are git-ignored.

What the check does. The gun's hit test (the routine at 0x2ADDE) walks the landmark entries of queue C with an ordinal held in the low
word of a register; a leftover outside 0 to 10 (the ordinal register after a failed chain element holds the low word of the step vector's y,
note 07 8.2) makes the original read the pointer table and the volume records at 16-bit wrapped offsets from their bases. The check
visits every ordinal k from -32,767 to 10 (the leftovers -32,768 to 9 plus one) with a single landmark entry in the queue and the register
loaded with k - 1 (a three-instruction trampoline in free rig memory, written here; the original's own words it displaces are read from the
loaded image). For each k:

  * the pointer long and the 256 bytes of the volume record that the wrapped offsets name are copied from the real machine's RAM (the dump's
    real addresses: the 12 bytes of loader header between the state hunks make the flat layout of the rig differ from the machine's, so the
    bytes of these reads are patched in per case; a case whose patched bytes overlap a variable the case itself sets is dropped);
  * a tracer is placed where the junk record's cell test and top box would let it in (every one of the nine placements of the tracer's cell
    around the record's cell bytes that exist in the world 0 to 127, the box centre, the pointer's high word divided by 4 as the z offset);
  * the original runs twice: with the junk as read (A) and with the table pointer made negative (B, the entry skipped as the port does);
  * the verdict is "equal" when A and B return the same value and leave the 64 bytes of the tracer slot equal.

A case in which A and B differ is an ordinal the port's rule (an ordinal outside 0 to 10 skips the entry and the counting goes on, except the
exact aliases k = j - 16,384 and k = j - 32,768) does not reproduce. The cases of this run are written to --cases for the C part
(tests/rig/test_weapons_diff.c, part lmstate).
"""
import ctypes
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from common import Rig, ROOT  # noqa: E402
import state_dump  # noqa: E402

TABLE, RECS = 0x5283C, 0x52868        # flat addresses of the pointer table and of the volume records
QC = 0x150000                          # the queue entry (rig memory, free)
TRAMP = 0xD00000                       # the trampoline (rig memory above the machine's RAM)
D5_INIT = 0x2AE5A                      # the register's initial load in the hit test
POOL = 0x4AD2A
LANDMARK_Z_SHIFT = 2
MAX_INSN = 400000


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def u32(b, a):
    return struct.unpack_from(">I", b, a)[0]


class State:
    def __init__(self, path):
        txt, ents = state_dump.load(path)
        self.flat, self.real = [], {}
        for kind, a, d in ents:
            (self.flat if kind == 0 else self.real.setdefault(0, [])).append((a, d))
        self.real_mem = bytearray(0x1000000)
        self.real_have = bytearray(0x1000000)
        for a, d in self.real.get(0, []):
            self.real_mem[a:a + len(d)] = d
            self.real_have[a:a + len(d)] = b"\1" * len(d)
        seg72 = [l.split() for l in txt.splitlines() if l.startswith("seg 72 ")][0]
        self.seg72_real = int(seg72[5], 16)
        self.seg72_flat = int(seg72[3], 16)
        self.Tr = self.seg72_real + (TABLE - self.seg72_flat)    # the real address of the pointer table
        self.Rr = self.Tr + (RECS - TABLE)                       # and of the first volume record

    def install(self, rig):
        rig.reset()
        for a, d in self.flat:
            rig.poke(a, d)
        for a, d in self.real.get(0, []):
            if a == 0:                                           # the chip RAM below the image: keep the rig's reset vector and sentinels
                keep = rig.peek(0, 0x120)
                rig.poke(0, d)
                rig.poke(0, keep[:8])
                rig.poke(0x100, keep[0x100:0x120])
            else:
                rig.poke(a, d)


def cell_ok(c):
    return 0 <= c <= 127


def plan(st, k):
    """The junk reads of ordinal k and the tracer placements. Returns (ptr, rec_bytes, placements)."""
    pf, pr = TABLE + s16(4 * k), st.Tr + s16(4 * k)
    ptr = u32(st.real_mem, pr)
    rf, rr = RECS + s16(256 * k), st.Rr + s16(256 * k)
    rec = bytes(st.real_mem[rr:rr + 0x100])
    return pf, pr, ptr, rf, rr, rec


def placements(ptr, rec, rng_state):
    """Tracer placements (cell x, cell z, pos x, y, z) inside the junk record's cell test and top box, or []."""
    cx, cz = rec[2], rec[3]
    b = struct.unpack_from(">5h", rec, 4)
    art = (ptr >> 16) >> LANDMARK_Z_SHIFT
    out = []
    for dx in (0, -1, 1):
        for dz in (0, -1, 1):
            tx, tz = cx + dx, cz + dz
            if not (cell_ok(tx) and cell_ok(tz)):
                continue
            # X = (pos x >> 8) + dx' * 16384 where dx' = tracer cell - record cell (as a signed word difference)
            offx, offz = dx * 16384, dz * 16384 + art
            lo_x, hi_x = max(b[1] + 1, offx), min(b[2] - 1, offx + 0x3FFF)
            lo_z, hi_z = max(b[3] + 1, offz), min(b[4] - 1, offz + 0x3FFF)
            if lo_x > hi_x or lo_z > hi_z:
                continue
            Y = b[0]                                              # the ceiling: the tracer is at or below it
            for fx, fz, fy in ((0.5, 0.5, 0), (0.1, 0.9, -7), (0.9, 0.1, -40)):
                X = int(lo_x + (hi_x - lo_x) * fx)
                Z = int(lo_z + (hi_z - lo_z) * fz)
                out.append((tx, tz, (X - offx) * 256 + 0x80, (Y + fy) * 256 + 0x40, (Z - offz) * 256 + 0x80))
            if out:
                return out
    return out


SAVE = [(0, 0x1000), (0x4A6E8, 0x54EC0), (D5_INIT, D5_INIT + 8), (TRAMP, TRAMP + 32), (QC, QC + 32), (0xDFFE00, 0xE00000)]


class Saved:
    """The memory a run changes: the low words, the state range, the patched code, the trampoline, the queue entry and the stack; plus the
    junk windows patched in per case. restore() puts the whole of it back, so that a state is installed once."""

    def __init__(self, rig):
        self.rig = rig
        self.regions = [(lo, rig.peek(lo, hi - lo)) for lo, hi in SAVE]
        self.extra = []

    def patch(self, addr, data):
        self.extra.append((addr, self.rig.peek(addr, len(data))))
        self.rig.poke(addr, data)

    def restore(self):
        for addr, data in reversed(self.extra):
            self.rig.poke(addr, data)
        self.extra = []
        for lo, data in self.regions:
            self.rig.poke(lo, data)


def random_placements(ptr, rec, n, seed):
    """n tracer placements inside the record's cell and top box, at random places of the box (the cell offsets as in placements())."""
    import random
    rnd = random.Random(seed * 7919 + 13)
    cx, cz = rec[2], rec[3]
    b = struct.unpack_from(">5h", rec, 4)
    art = (ptr >> 16) >> LANDMARK_Z_SHIFT
    out = []
    for _ in range(n):
        lo_x, hi_x = max(b[1] + 1, 0), min(b[2] - 1, 0x3FFF)
        lo_z, hi_z = max(b[3] + 1 - art, 0), min(b[4] - 1 - art, 0x3FFF)
        if lo_x > hi_x or lo_z > hi_z:
            return out
        X = rnd.randint(lo_x, hi_x)
        Z = rnd.randint(lo_z, hi_z)
        Y = rnd.randint(max(b[0] - 400, -50), b[0])
        out.append((cx, cz, X * 256 + rnd.randint(0, 255), Y * 256 + rnd.randint(0, 255), Z * 256 + rnd.randint(0, 255)))
    return out


def run_case(rig, st, k, pf, ptr, rf, rec, pl, neg, trampoline_words, saved=None):
    """One run of 0x2ADDE; returns (d0, slot bytes, trace of the walk's end)."""
    tx, tz, px, py, pz = pl
    # the junk reads of the case (patched into the flat state), the entry, the tracer
    saved.patch(rf, rec)
    saved.patch(pf, struct.pack(">I", 0xFFFFFFFF if neg else ptr))
    rig.w16(QC, 0x0000)
    rig.w32(0x4AA7E, QC)
    rig.w8(0x4A916, 1)
    base = POOL
    sub = (300, -200, 150)
    for i in range(3):
        rig.w32(base + 4 * i, (px, py, pz)[i])
        rig.w32(base + 0x0C + 4 * i, sub[i] * 8)
        rig.w32(base + 0x18 + 4 * i, sub[i])
    rig.w16(base + 0x24, 1)
    rig.w16(base + 0x26, 0x401)
    rig.w16(base + 0x28, 20)
    rig.w16(base + 0x2E, 0)
    rig.w16(base + 0x30, tx)
    rig.w16(base + 0x32, tz)
    # the register load: D5 = 0xFFFF0000 | (k - 1)
    d5 = 0xFFFF0000 | ((k - 1) & 0xFFFF)
    code = struct.pack(">HI", 0x2A3C, d5) + trampoline_words + struct.pack(">HI", 0x4EF9, D5_INIT + 8)
    rig.poke(TRAMP, code)
    rig.poke(D5_INIT, struct.pack(">HI", 0x4EF9, TRAMP))
    args = (ctypes.c_uint32 * 1)(0)
    rig.L.rig_call.restype = ctypes.c_uint32
    rig.L.rig_call.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_int]
    d0 = rig.L.rig_call(0x2ADDE, ctypes.cast(args, ctypes.c_void_p), 1)
    return d0, rig.peek(base, 64), ctypes.c_int.in_dll(rig.L, "rig_last_error").value


def volume(st, j):
    """The real volume j of the dump as the port's slot holds it: (cell x, cell z, box[5], nodes[(box[5], nplanes, [(ref3, nrm3)])]), or None when the
    table entry is negative or zero (no model) or the volume does not fit the port's slot (more than 4 nodes or 8 planes)."""
    m = st.real_mem
    ptr = u32(m, st.Tr + 4 * j)
    if ptr == 0 or ptr & 0x80000000:
        return None
    rec = st.Rr + 256 * j
    box = struct.unpack_from(">5h", m, rec + 4)
    nodes, e, me = [], rec + 0x0E, ptr + 0x0A
    for _ in range(9):
        nb = struct.unpack_from(">5h", m, e)
        link = u32(m, e + 0x0A)
        cnt = struct.unpack_from(">H", m, me + 0x0E)[0]
        npw = max(cnt, 1)
        if npw > 8:
            return None
        planes = []
        for q in range(npw):
            planes.append((struct.unpack_from(">3h", m, e + 0x0E + 6 * q), struct.unpack_from(">3h", m, me + 0x16 + 12 * q)))
        nodes.append((nb, cnt, planes))
        if link & 0x80000000 or link == 0:
            break
        e, me = link, u32(m, me + 0x0A)
    if len(nodes) > 4:
        return None
    return (m[rec + 2], m[rec + 3], box, nodes)


def write_cases(path, st, cases):
    """tests/rig/test_weapons_diff.c part lmstate reads this: the real volumes, then the cases (k, kind, tracer, the patches of the junk reads)."""
    with open(path, "wb") as f:
        f.write(b"LMC2")
        vols = [(j, volume(st, j)) for j in range(11)]
        vols = [(j, v) for j, v in vols if v]
        f.write(struct.pack(">I", len(vols)))
        for j, (cx, cz, box, nodes) in vols:
            f.write(struct.pack(">9i", j, cx, cz, *box, len(nodes)))
            for nb, cnt, planes in nodes:
                f.write(struct.pack(">7i", *nb, cnt, len(planes)))
                for ref, nrm in planes:
                    f.write(struct.pack(">6i", *ref, *nrm))
        f.write(struct.pack(">I", len(cases)))
        for k, kind, pl, pf, ptr, rf, rec in cases:
            f.write(struct.pack(">7iII", k, kind, pl[0], pl[1], pl[2], pl[3], pl[4], pf, rf) + struct.pack(">I", ptr) + rec)


def main(argv):
    limit = None
    cases_path = os.path.join(ROOT, "extracted", "captures", "landmark-state-cases.bin")
    i = 1
    while i < len(argv):
        if argv[i] == "--limit":
            limit = int(argv[i + 1]); i += 2
        elif argv[i] == "--cases":
            cases_path = argv[i + 1]; i += 2
        else:
            print(__doc__); return 2
    path = os.path.join(ROOT, "extracted", "captures", "state-flight.bin")
    if not os.path.exists(path):
        print("landmark_state_check: skipped (no extracted/captures/state-flight.bin: run tools/vamiga-ref/scripts/combat7-statedump.txt and state_dump.py)")
        return 0
    st = State(path)
    rig = Rig()
    rig.L.rig_set_max_instructions.argtypes = [ctypes.c_uint32]
    rig.L.rig_set_max_instructions(MAX_INSN)   # a normal call is a few thousand instructions; a longer one is a loop the machine would not leave
    st.install(rig)
    saved = Saved(rig)
    # the words the trampoline re-executes: the six bytes after the register load, read from the loaded image
    tramp_words = rig.peek(D5_INIT + 2, 6)
    print("table at real %X, records at real %X" % (st.Tr, st.Rr))
    ks = list(range(-32767, 11))
    if limit:
        ks = ks[:limit]
    # variables the case itself sets or the routine writes: a junk window that touches them is dropped
    mine = [(POOL, POOL + 64), (0x4A916, 0x4A917), (0x4AA7E, 0x4AA82), (0x4B230, 0x4B240), (0x4AA70, 0x4AA72), (0x4A875, 0x4A876), (0x4A866, 0x4A867)]
    stats = {"real": 0, "real_hits": 0, "alias": 0, "neg": 0, "unreachable": 0, "dropped": 0, "equal": 0, "diff_hit": 0, "diff_other": 0, "beyond_nonzero_ptr": 0, "in_hunk": 0, "beyond_hunk": 0}
    diffs, cases, out_cases = [], [], []
    t0 = time.time()
    for k in ks:
        pf, pr, ptr, rf, rr, rec = plan(st, k)
        # the last hunk (184, data at C50E68, 16 bytes) ends the loaded program; reads at or past it are beyond the loaded hunks
        end = 0xC50E68 + 16
        if pr >= end:
            stats["beyond_hunk"] += 1
            if ptr:
                stats["beyond_nonzero_ptr"] += 1
        else:
            stats["in_hunk"] += 1
        if (0 <= k + 16384 <= 10) or (1 <= k + 32768 <= 10) or (0 <= k <= 10):
            stats["alias" if k < 0 else "real"] += 1               # a real ordinal or an exact alias of a real entry: the port tests it as that entry (07 8.2 port rule C), the C part checks it
            # tracers inside the real volume's box at random places: the cases that the original answers with a hit are kept (up to 6), and two that it does not
            got_hit, got_miss = 0, 0
            for pl in random_placements(ptr, rec, 300, k):
                if got_hit >= 6 and got_miss >= 2:
                    break
                a = run_case(rig, st, k, pf, ptr, rf, rec, pl, False, tramp_words, saved)
                saved.restore()
                hit = a[0] != 0
                if (hit and got_hit < 6) or (not hit and got_miss < 2):
                    out_cases.append((k, 1, pl, pf, ptr, rf, rec))
                    got_hit += hit
                    got_miss += not hit
            stats["real_hits"] = stats.get("real_hits", 0) + got_hit
            continue
        if ptr & 0x80000000:
            stats["neg"] += 1
            continue
        if any(rf < hi and rf + 0x100 > lo for lo, hi in mine) or any(pf < hi and pf + 4 > lo for lo, hi in mine):
            stats["dropped"] += 1
            continue
        pls = placements(ptr, rec, None)
        if not pls:
            stats["unreachable"] += 1
            continue
        res = []
        for pl in pls:
            a = run_case(rig, st, k, pf, ptr, rf, rec, pl, False, tramp_words, saved)
            saved.restore()
            b = run_case(rig, st, k, pf, ptr, rf, rec, pl, True, tramp_words, saved)
            saved.restore()
            res.append((a, b, pl))
            cases.append((k, pl))
        bad = [r for r in res if r[0][0] != r[1][0] or r[0][1] != r[1][1] or r[0][2] != 0]
        if not bad:
            stats["equal"] += 1
            out_cases.append((k, 0, pls[0], pf, ptr, rf, rec))
        else:
            hit = any(r[0][0] != 0 for r in bad)
            stats["diff_hit" if hit else "diff_other"] += 1
            out_cases.append((k, 2, pls[0], pf, ptr, rf, rec))
            diffs.append((k, ptr, rec[2], rec[3], bad[0][0][0], bad[0][1][0], bad[0][0][2], [i for i in range(64) if bad[0][0][1][i] != bad[0][1][1][i]], bad[0][2]))
    print("ordinals k = -32767..10: %d, %d s" % (len(ks), time.time() - t0))
    for key in ("in_hunk", "beyond_hunk", "beyond_nonzero_ptr", "real", "real_hits", "alias", "neg", "dropped", "unreachable", "equal", "diff_hit", "diff_other"):
        print("  %-20s %d" % (key, stats[key]))
    # the differing ordinals by kind: the call ran past the instruction bound (a loop the machine would not leave), a hit, or a miss that moved the tracer
    # differently from a skip (the call ended without the motion step, or the sub-steps moved it); and by the pointer the junk table entry held
    kinds = {}
    for d in diffs:
        kind = "no return (instruction bound)" if d[6] else ("hit" if d[4] else "miss, tracer moved differently")
        kinds[(kind, "pointer 0" if d[1] == 0 else "pointer in RAM" if 0xC00000 <= d[1] < 0xC80000 else "other pointer")] = kinds.get((kind, "pointer 0" if d[1] == 0 else "pointer in RAM" if 0xC00000 <= d[1] < 0xC80000 else "other pointer"), 0) + 1
    for key in sorted(kinds):
        print("  differs: %-34s %-15s %d" % (key[0], key[1], kinds[key]))
    mods = {}
    for d in diffs:
        mods[d[0] & 255] = mods.get(d[0] & 255, 0) + 1
    print("  differing ordinals by k mod 256 (the six commonest):", sorted(mods.items(), key=lambda x: -x[1])[:6])
    realrec = [d for d in diffs if (d[0] & 255) <= 10]
    print("  of them reading one of the volume records 0 to 10 (k mod 256 in 0..10; the record is real, the model pointer is junk): %d; reading unrelated memory: %d" % (len(realrec), len(diffs) - len(realrec)))
    for d in diffs[:12]:
        print("  differs: k=%d (mod 256 = %d) ptr %08X cell bytes %d,%d  A return %d, B return %d, rig error %d, slot bytes differing %s, tracer %s" % (d[0], d[0] & 255, d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8]))
    write_cases(cases_path, st, out_cases)
    print("cases written to %s: %d (equal to a skip %d, real or alias %d, differing %d); real volumes that fit the port's slot: %s" % (
        cases_path, len(out_cases), sum(1 for c in out_cases if c[1] == 0), sum(1 for c in out_cases if c[1] == 1), sum(1 for c in out_cases if c[1] == 2),
        [j for j in range(11) if volume(st, j)]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
