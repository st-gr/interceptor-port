#!/usr/bin/env python3
"""Check the mesh model (meshmodel.py, the rule of docs/re/04-scene-composition.md "Stage [68]") against emulator frames, without running the original mesh pass:
the sky fill runs in the rig, the model's polygons are handed to the original polygon clipper 0x28B6C (pen 6, plane mask 2, as the pass sets them), and the pens are
compared with extracted/captures/sky/NAME.ppm exactly as check_mesh.py does (sky 5, sea 6, land 4 by colour; every pen 7 pixel of the rig must be pen 7 in the frame).

    python3 tools/vamiga-ref/sky-ground/check_meshmodel.py extracted/captures/logs/sky-ground-mesh-a.txt [label ...]

Needs the logs of gen-sky-ground.py (labels s71_NAME and rec0_NAME), the frames, extracted/interceptor.bin and build/sky-ground/librigblit.so."""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
import logparse, rigw, check_frames as cf, check_mesh as cm, meshmodel as mm


def params_from_state(blk, rec):
    """A meshmodel.Params from the state block V:4A700.. (2336 bytes) and the player record (512 bytes at V:4B23C)."""
    w = lambda a: blk.s16(a - 0x4A700)
    b = lambda a: blk.u8(a - 0x4A700)
    ext = b(0x4A83D)
    if ext:
        x, y, z = [blk.u32(a - 0x4A700) for a in (0x4ACF6, 0x4ACFA, 0x4ACFE)]
    else:
        off = w(0x4A996)
        x, y, z = rec.u32(0x14 + off), rec.u32(0x18 + off), rec.u32(0x1C + off)
    return mm.Params(x=x, y=y, z=z, M=[w(0x4AC90 + 2 * i) for i in range(9)], a_long=-blk.s32(0x4AB30 - 0x4A700), zoom3=w(0x4AAFA) & 0xFFFF,
                     f895=b(0x4A895), f865=b(0x4A865), f954=b(0x4A954), phase=b(0x4A908), octant=b(0x4A90C), f868=b(0x4A868), external=ext)


def draw(r, polys):
    r.w16(0x5103C, 0)
    r.w16(0x4AA0C, 6)
    r.poke(0x4A79E, struct.pack(">4h", 2, 2, 0, 0))
    for n, verts in polys:
        r.w16(0x5103E, n)
        for i, (x, y, z) in enumerate(verts):
            r.w16(0x51040 + 6 * i, x); r.w16(0x51042 + 6 * i, y); r.w16(0x51044 + 6 * i, z)
        r.run(0x28B6C)


def run(log, label, fdir, D):
    L = logparse.load(log)
    st = cf.state(L["s71_" + label])
    if cf.torn(st):
        return None
    r = rigw.stage(state=L["s71_" + label].data)
    r.poke(0x4B23C, L["rec0_" + label].data)
    P = params_from_state(L["s71_" + label], L["rec0_" + label])
    polys = mm.mesh(P, D)
    r.run(0x3486C)
    draw(r, polys)
    pn = rigw.pens(r)
    im = cf.load_ppm(os.path.join(fdir, label + ".ppm"))
    tot = bad = 0
    for y in range(1, st["win"] + 1):
        for x in range(320):
            c = cf.game_px(im, x, y)
            if c in cm.CMAP:
                tot += 1
                bad += pn[y][x] != cm.CMAP[c]
            if pn[y][x] == 7:
                tot += 1
                bad += c != cm.PEN7
    return len(polys), tot, bad, P


if __name__ == "__main__":
    log = sys.argv[1]
    fdir = os.path.join(HERE, "..", "..", "..", "extracted", "captures", "sky")
    L = logparse.load(log)
    labels = sys.argv[2:] or [k[4:] for k in L if k.startswith("s71_")]
    D = mm.Data()
    sumbad = 0
    for lab in labels:
        res = run(log, lab, fdir, D)
        if res is None:
            print(lab, "skipped")
            continue
        n, tot, bad, P = res
        print(lab, "polygons %d, pixels %d, different %d (A=%d zoom=%#x ext=%d phase=%d oct=%d f954=%d)" % (n, tot, bad, P.a_long, P.zoom3, P.external, P.phase, P.octant, P.f954))
        sumbad += bad
    sys.exit(1 if sumbad else 0)
