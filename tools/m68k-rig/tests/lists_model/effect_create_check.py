"""When an effect object is created (docs/re/07-ai-and-weapons.md, section 11.2): a weapon record (class nibble 0) is stepped by the real world update 0x26AF8 below the ground.

    python3 effect_create_check.py

A missile record (class 0, W0 0x10CA, the player's group) is put 5 units below the ground at the centre of each cell of a 12 x 12 grid of the land/sea map, with the
objects table empty, and one world pass is run.  The rule of the note: the record gets the hit bit 0x8000 and the dying bit 0x0400 with the counter 15 and loses the active bit
0x1000; the sea test (record byte +4 bit 1, note 05 section 10) decides: over land an object is created in slot 0 with the model id 0x13 (an even slot) and the life word 0x7F,
over the sea none; the object table is full (16 valid objects): nothing is created and no object is overwritten.  A second missile in the next pass takes slot 1 and the id 0x12.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so, extracted/captures/logs/lists.txt.  Skips with exit 0 when one is missing.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import inputs_missing
from combat import stage, world, REC

OBJ = 0x4D23C


def put_missile(r, i, cx, cz, y_units=-5):
    P = REC(0); M = REC(i)
    r.poke(M, r.peek(P, 0xA4) + bytes(0x200 - 0xA4))
    r.w16(M, 0x10CA); r.w16(M + 2, 0); r.w8(M + 0x62, 0); r.w8(M + 0x5E, 0); r.w16(M + 0x4C, 100); r.w8(M + 5, 0)
    x = (cx << 22) + (1 << 21); z = (cz << 22) + (1 << 21)
    r.w32(M + 0x14, x); r.w32(M + 0x1C, z); r.w32(M + 0x18, (y_units * 256) & 0xFFFFFFFF)
    r.w16(M + 6, cx); r.w16(M + 8, cz); r.w16(M + 0xC, (x & 0x3FFFFF) >> 8); r.w16(M + 0xE, (z & 0x3FFFFF) >> 8); r.w32(M + 0x10, y_units & 0xFFFFFFFF)
    r.w16(M + 0x6C, 0x1000); r.w16(M + 0x6E, 0x1000); r.w16(M + 0x2C, 0xFFFF); r.w8(M + 0x38, 0xFF)
    r.w32(M + 0x3E, 0); r.w32(M + 0x42, 0xFFFFA000); r.w32(M + 0x46, 0)     # sinking, so that the step ends below the ground


def main():
    if inputs_missing(): print("skipped (inputs missing)"); return 0
    bad = 0; land = sea = 0
    for cx in range(60, 72):
        for cz in range(60, 72):
            r = stage(mode=3, place=False)
            r.w8(0x4A95E, 3)
            for k in range(16): r.poke(OBJ + 32 * k, bytes(32))
            put_missile(r, 1, cx, cz)
            world(r)
            M = REC(1); water = bool(r.u8(M + 4) & 2)
            made = [k for k in range(16) if r.u16(OBJ + 32 * k) & 0x40]
            w0 = r.u16(M)
            ok = (w0 & 0x8400) == 0x8400 and not (w0 & 0x1000) and r.s16(M + 0x4C) == 15
            if water:
                sea += 1; ok &= made == []
            else:
                land += 1
                ok &= made == [0] and r.u16(OBJ + 2) == 0x13 and r.u16(OBJ + 4) == 0x7F and r.u8(0x4A910) == 0xFF
            if not ok:
                bad += 1
                if bad < 5: print("DIFF cell %d %d water %s made %s w0 %04X" % (cx, cz, water, made, w0))
    # the second pass: the next free slot (1) takes the id 0x12; and the full table
    r = stage(mode=3, place=False); r.w8(0x4A95E, 3)
    for k in range(16): r.poke(OBJ + 32 * k, bytes(32))
    cell = None
    for cx in range(60, 72):
        for cz in range(60, 72):
            put_missile(r, 1, cx, cz); world(r)
            if r.u16(OBJ) & 0x40: cell = (cx, cz); break
        if cell: break
    ok2 = cell is not None
    if ok2:
        put_missile(r, 2, *cell); world(r)
        ok2 = bool(r.u16(OBJ + 32) & 0x40) and r.u16(OBJ + 34) == 0x12
        for k in range(16):
            r.poke(OBJ + 32 * k, bytes(32)); r.w16(OBJ + 32 * k, 0x40); r.w16(OBJ + 32 * k + 2, 0x55); r.w16(OBJ + 32 * k + 4, 0x66 + k)
        before = r.peek(OBJ, 512)
        put_missile(r, 3, *cell); world(r)
        after = r.peek(OBJ, 512)
        # the world pass decrements every life word once; nothing else may change
        exp = bytearray(before)
        for k in range(16): exp[32 * k + 4:32 * k + 6] = (0x66 + k - 1).to_bytes(2, "big")
        ok2 &= after == bytes(exp)
    bad += 0 if ok2 else 1
    print("effect_create_check: %d cells (%d land, %d sea), second slot and full table %s, %s" % (land + sea, land, sea, "ok" if ok2 else "FAILED", "FAILED %d" % bad if bad else "0 differences"))
    return 1 if bad else 0




def backup_check(n=800, seed=1):
    """the position back-up 0x2AA5E: x += (((dx << 4) / (dy >> 8)) * depth) >> 4 and z likewise, y = 0 (depth = -(y >> 8) as a word, divides and multiplies on words)"""
    import random
    from combat import rig
    rng = random.Random(seed); r = rig(); bad = 0; moved = 0
    def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
    def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v
    for it in range(n):
        stage(mode=3, place=False)
        M = REC(1); r.w16(0x4AA6E, 0x200)
        x = rng.randint(0x1000000, 0x20000000); z = rng.randint(0x1000000, 0x20000000)
        y = rng.choice((-1, -5, -300, -2000, -70000)) * 256 + rng.randint(0, 255) if rng.random() < 0.9 else rng.randint(0, 5000)
        dx = rng.randint(-0x6000, 0x6000); dy = rng.choice((0, -0x100, -0x6000, -0x300, 0x1F, rng.randint(-0x20000, 0x2000))); dz = rng.randint(-0x6000, 0x6000)
        r.w32(M + 0x14, x); r.w32(M + 0x18, y & 0xFFFFFFFF); r.w32(M + 0x1C, z)
        r.run(0x2AA5E, d5=dx & 0xFFFFFFFF, d6=dy & 0xFFFFFFFF, d7=dz & 0xFFFFFFFF, a1=M)
        d1 = s32(dy) >> 8
        if d1 == 0:
            want = (x, y, z)
        else:
            depth = s16(-(s32(y) >> 8))
            def part(dv):
                num = s32(dv << 4)
                den = d1 if -32768 <= d1 <= 32767 else s16(d1)
                if den == 0: return None
                q = abs(num) // abs(den); q = q if (num >= 0) == (den > 0) else -q
                if not -32768 <= q <= 32767: q = s16(num)          # overflow: the dividend stays, its low word is used
                return s32((s16(q) * depth)) >> 4
            px = part(dx); pz = part(dz)
            want = (s32(x + px) if px is not None else x, 0, s32(z + pz) if pz is not None else z)
            moved += 1
        got = (s32(r.u32(M + 0x14)), s32(r.u32(M + 0x18)), s32(r.u32(M + 0x1C)))
        if (got[0] & 0xFFFFFFFF, got[1] & 0xFFFFFFFF, got[2] & 0xFFFFFFFF) != tuple(v & 0xFFFFFFFF for v in want):
            bad += 1
            if bad < 5: print("DIFF backup x %d y %d z %d d %d %d %d: got %s want %s" % (x, y, z, dx, dy, dz, got, want))
    print("backup_check: %d states, %d moved, %s" % (n, moved, "FAILED %d" % bad if bad else "0 differences"))
    return bad


if __name__ == "__main__":
    sys.exit(main() + (1 if backup_check() else 0))
