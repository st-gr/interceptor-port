"""Model of the near-ground pass (stage [90], routine 0x2C0EC): which points of the lattice are candidates, which sprites and triangles it draws.
Reads the three point tables and the distance-class table from the user's git-ignored extracted/interceptor.bin at run time (never committed).
model(state, img) -> list of events: ('s1', x, y) the 1 by 1 sprite 0x340D4, ('s2', x, y) the 2 by 1 sprite 0x340EA, ('tri', (x0,y0),(x1,y1),(x2,y2))
the filler call 0x34A28 with its three vertices, in the order of the original."""
import struct


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def words(img, flat, n):
    o = flat - 0x10000
    return [struct.unpack(">h", img[o + 2 * i:o + 2 * i + 2])[0] for i in range(n)]


def sdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def project(img_m, x, y, h3, hy):
    """Camera-space point: x, y the lattice-relative words, h3 the height vector (three words); returns (X, Y) pixels or None."""
    m = img_m
    d6 = s16(((m[0] * x + m[2] * y) >> 8) + h3[0])
    d2 = s16(((m[3] * x + m[5] * y) >> 8) + h3[1])
    d4 = s16(((m[6] * x + m[8] * y) >> 8) + h3[2])
    if d4 <= 0 or d6 > d4 or s16(-d6) > d4 or d2 > d4 or s16(-d2) > d4:
        return None
    sx = sdiv(d6 * 0xA0, d4) + 0xA0
    sx = min(max(sx, 0), 0x13F)
    sy = sdiv(d2 * 0x5A, d4) + 0x5A
    sy = min(max(sy, 0), 0xB3)
    return 0x13F - sx, 0xB3 - sy


def model(st, img):
    """st: cam_x (V:4AB2A word), cam_z (V:4AB2E word), height_long (V:4AB30), band (V:4A923), matrix (nine words V:4AC90..), limit (V:4AA3C)."""
    out = []
    band = st["band"]
    thr_lim = -0x800 if band == 0 else (-0xD80 if band == 1 else -0x1200)
    h = st["height_long"]
    if h < thr_lim:
        return out, None
    low = s16(h)
    far = 1 if h < -0x200 else 0
    cx, cz = s16(st["cam_x"]), s16(st["cam_z"])
    if low >= -0x80:
        tab, shift = 0x2C840, 3
    elif low >= -0xE0:
        tab, shift = 0x2CA84, 0
    else:
        tab, shift = 0x2CC68, 0
    cx = s16(cx + (((0x400 - cx) & 0xF800)))
    cz = s16(cz + (((0x400 - cz) & 0xF800)))
    n, thr = words(img, tab, 2)
    ents = words(img, tab + 4, 3 * n)
    tri = {-12: (0x2CDEC, 0x2CDF8), -16: (0x2CE04, 0x2CE10), -20: (0x2CE1C, 0x2CE28)}
    cls = img[0x2C440 - 0x10000:0x2C840 - 0x10000]
    m = st["matrix"]
    hh = s16(low << shift)
    h3 = [s16((hh * m[1]) >> 8), s16((hh * m[4]) >> 8), s16((hh * m[7]) >> 8)]
    cand = 0
    for i in range(n):
        x, z, k = ents[3 * i:3 * i + 3]
        x = s16(x + cx); z = s16(z + cz)
        ax = s16(-x if x < 0 else x) >> 8
        az = s16(-z if z < 0 else z) >> 8
        c = cls[((az << 5) + ax) & 0xFFFF] if ((az << 5) + ax) < 1024 else None
        if c is None:
            raise ValueError("class index out of table")
        if c > 127: c -= 256
        if c > thr:
            continue
        cand += 1
        kind = k
        if k < 0:
            if far or c - 1 > 0:
                kind = 1 if k == -12 else 2
        xs, zs = s16(x << shift), s16(z << shift)
        if kind >= 0:
            p = project(m, xs, zs, h3, 0)
            if p is None or p[1] > st["limit"]:
                continue
            out.append(("s2" if kind == 2 else "s1", p[0], p[1]))
        else:
            ta = tri[kind][1 if shift else 0]
            vs = words(img, ta, 6)
            pts = []
            for j in range(3):
                p = project(m, s16(vs[2 * j] + xs), s16(vs[2 * j + 1] + zs), h3, 0)
                if p is None:
                    break
                pts.append(p)
            else:
                out.append(("tri",) + tuple(pts))
    return out, cand
