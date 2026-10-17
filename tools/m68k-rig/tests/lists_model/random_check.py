"""Random-state comparison of the rig's builder (0x1F770) with the Python model (docs/re/04a-display-lists.md, Verified by).

    python3 random_check.py SEED N [CAMRANGE] [landmarks] [active]

Starts from the airborne scene of the emulator log, randomises the player's cell, sub-cell, octant, height, pitch gate and camera
offsets, builds in the rig and compares every queue entry (id, LOD nibble, X and Z words) with model2.build2. `landmarks` places
the player within 2 cells of the table C cells that hold landmarks with collision data (ids 42, 70, 88); `active` leaves the
entity records active and compares the terrain subsequence only. Needs librig.so (make -C tools/m68k-rig librig.so).
"""
import random, struct, sys
from common import Rig, load_scene, run_builder, inputs_missing
if __name__ == "__main__" and inputs_missing():      # the model modules read the image when they are imported
    print('skipped (missing: %s)' % ', '.join(inputs_missing())); sys.exit(0)
from model2 import *
import model2


def st_from(r):
    return dict(aa00=r.u16(0x4AA00), aa02=r.u16(0x4AA02), aa04=r.u16(0x4AA04), aa06=r.u16(0x4AA06), a908=r.u8(0x4A908),
                a909=r.u8(0x4A909), a90c=r.u8(0x4A90C), a83e=r.u8(0x4A83E), hi=r.s32(0x4AB1E) <= -0xA000)


def main():
    seed = int(sys.argv[1]); n = int(sys.argv[2])
    camr = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 3000
    landmarks = "landmarks" in sys.argv; active = "active" in sys.argv
    random.seed(seed)
    lmc = []
    if landmarks:
        for row in range(128):
            for key in range(128):
                its = cell_items('C', row, key)
                if its and any(i[1] in (42, 70, 88) for i in its):
                    lmc.append((key, row))
    r = Rig(); bad = 0; lists = 0; lods = set()
    for it in range(n):
        load_scene(r, 'air')
        if not active:
            for k in range(16):
                r.w8(0x4B23C + 0x200 * k + 1, r.u8(0x4B23C + 0x200 * k + 1) & ~0x40)
        if landmarks:
            lk, lr = random.choice(lmc)
            xc = (lk + random.randint(-2, 2)) % 128; zc = (lr + random.randint(-2, 2)) % 128
        else:
            xc = random.randint(40, 74) if it % 3 else random.randint(0, 127)
            zc = random.randint(38, 76) if it % 3 else random.randint(0, 127)
        lay = random.randint(0, 15)
        r.w16(0x4AA04, xc); r.w16(0x4AA06, zc)
        r.w8(0x4A908, 4 * (3 - (zc & 3)) + (3 - (xc & 3))); r.w8(0x4A909, lay)
        h = random.randint(0x10000, 0x400000) if random.random() < .5 else random.randint(0x400, 0x9000)
        r.w32(0x4AB1E, -h)
        r.w32(0x4AB30, -random.randint(0x80, 0x7FFF) if random.random() < .5 else -(h >> 8))
        r.w16(0x4AB2A, random.randint(-camr, camr)); r.w16(0x4AB2E, random.randint(-camr, camr))
        r.w8(0x4A90C, random.randint(0, 7)); r.w8(0x4A83E, 0 if random.random() < .8 else 1)
        run_builder(r)
        st = st_from(r)
        cam = dict(cam2a=r.s16(0x4AB2A), cam2e=r.s16(0x4AB2E), cam30=r.s32(0x4AB30))
        rig = r.peek(0x53A4A, 5040)
        for base, tier in ((0, 'A'), (0x690, 'B'), (0xD20, 'C')):
            got = []
            for i in range(70):
                e = rig[base + 24 * i:base + 24 * i + 24]
                if e[:2] == b'\xff\xff': break
                if active and (e[1] & 0x50): continue
                got.append((e[0], e[1] & 0xF, struct.unpack('>H', e[6:8])[0], struct.unpack('>H', e[10:12])[0]))
            m = build2(tier, st, cam)
            exp = [(e[0], e[1], e[2], e[3]) for e in m][:69]
            lists += 1
            lods.update(e[1] for e in got)
            ok = got == exp[:len(got)] and (active or len(m) > 69 or len(m) == len(got))
            if not ok:
                bad += 1
                if bad < 4: print('DIFF', it, tier, st, len(got), len(m))
    print(model2.STATS)
    print('states', n, 'tier lists', lists, 'bad', bad, 'LODs seen', sorted(lods))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
