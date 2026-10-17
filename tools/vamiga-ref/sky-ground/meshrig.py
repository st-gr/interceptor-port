"""Run the original sea-mesh pass 0x2F19C in the rig and log the polygons it hands to the clipper 0x28B6C (the clipper is replaced, in the rig only, by a
stub that copies the polygon buffer V:5103C.. to a log), or draw them with the real clipper and filler.

    import rigw, meshrig
    r = rigw.stage(pitch=.., heading=.., roll=.., alt_long=..)         # camera matrix by the game's own routines
    meshrig.set_inputs(r, P)                                             # meshmodel.Params into the variables the pass reads
    logged = meshrig.run_logged(r)                                       # list of (shift word, count, [(x, y, z)])  as the original produced them
"""
import struct
LOGPTR, LOGBUF = 0x600100, 0x610000
STUB = bytes.fromhex("41F90005103C" "227900600100" "7237" "32D8" "51C9FFFC" "23C900600100" "7001" "4E75")


def set_inputs(r, P, record=0x4B23C):
    """Write a meshmodel.Params into the rig: position (cockpit record or the external camera), camera matrix, flags."""
    r.w8(0x4A83D, P.external)
    r.w16(0x4A996, 0)
    if P.external:
        r.w32(0x4ACF6, P.x); r.w32(0x4ACFA, P.y); r.w32(0x4ACFE, P.z)
    else:
        r.w32(record + 0x14, P.x); r.w32(record + 0x18, P.y); r.w32(record + 0x1C, P.z)
    for i, v in enumerate(P.M):
        r.w16(0x4AC90 + 2 * i, v)
    r.w32(0x4AB30, -P.a_long)
    r.w16(0x4AAFA, P.zoom3)
    r.w8(0x4A895, P.f895); r.w8(0x4A865, P.f865); r.w8(0x4A954, P.f954)
    r.w8(0x4A908, P.phase); r.w8(0x4A90C, P.octant); r.w8(0x4A868, P.f868)


def run_logged(r):
    r.poke(0x28B6C, STUB)
    r.w32(LOGPTR, LOGBUF)
    r.run(0x2F19C)
    end = r.s32(LOGPTR)
    n = (end - LOGBUF) // 112
    raw = r.peek(LOGBUF, 112 * n)
    out = []
    for i in range(n):
        w = struct.unpack(">56h", raw[112 * i:112 * i + 112])
        cnt = w[1]
        k = max(cnt, 1)
        out.append((cnt, [tuple(w[2 + 3 * j:5 + 3 * j]) for j in range(min(k, 18))], w[0]))
    return out
