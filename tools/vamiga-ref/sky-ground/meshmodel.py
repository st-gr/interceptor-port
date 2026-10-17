"""Model of the sea-mesh pass 0x2F19C (stage [68] of docs/re/04-scene-composition.md): the polygons it hands to the polygon clipper 0x28B6C, in order.

    import meshmodel as mm
    P = mm.Params(x=..., y=..., z=..., M=[9 camera words], a_long=-V:4AB30, ...)
    polys = mm.mesh(P, data)          # list of (count, [(x, y, z) camera-space words]) in the order the original calls the clipper (shift word 0)

`data` is the extracted code image (extracted/interceptor.bin, loaded at 0x10000): the scan lists and the cell-offset table (0x2E600..0x2F19B), the
two grids (0x47D7C coarse, 0x47F40 fine) and the cell records (hunk 68). Nothing from the image is stored in this file."""
import os, struct
from dataclasses import dataclass, field

B = 0x10000
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def load_image(path=None):
    path = path or os.path.join(ROOT, "extracted", "interceptor.bin")
    if not os.path.exists(path):
        print("skipped: needs extracted/interceptor.bin")
        raise SystemExit(0)
    with open(path, "rb") as f:
        return f.read()


def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v
def asr(v, n): return v >> n            # python ints: arithmetic


@dataclass
class Params:
    x: int = 0                 # record +0x14 (cockpit: the viewed record V:4B23C + V:4A996) or V:4ACF6 (external view)
    y: int = 0                 # +0x18 / V:4ACFA
    z: int = 0                 # +0x1C / V:4ACFE
    M: list = field(default_factory=lambda: [256, 0, 0, 0, 256, 0, 0, 0, 256])   # V:4AC90, nine words
    a_long: int = 0            # minus V:4AB30 (camera height in units of 256 position units)
    zoom3: int = 0x80          # V:4AAFA
    f895: int = 0x80           # V:4A895 (bit 7: zoom is the default 0x80; any non-zero value skips the scaling)
    f865: int = 0              # V:4A865 (map view)
    f954: int = 0              # V:4A954 (second sea-zone flag)
    phase: int = 0             # V:4A908
    octant: int = 0            # V:4A90C
    f868: int = 0              # V:4A868 (never written)
    external: int = 0          # V:4A83D


class Data:
    def __init__(self, img=None):
        self.d = img if img is not None else load_image()

    def b(self, a, n=1): return self.d[a - B:a - B + n]
    def u8(self, a): return self.d[a - B]
    def sb(self, a): v = self.d[a - B]; return v - 256 if v > 127 else v
    def u16(self, a): return struct.unpack(">H", self.d[a - B:a - B + 2])[0]
    def w(self, a): return struct.unpack(">h", self.d[a - B:a - B + 2])[0]
    def l(self, a): return struct.unpack(">I", self.d[a - B:a - B + 4])[0]


OFFTAB, SC_COARSE_FAR, SC_COARSE_NEAR, SC_HI1, SC_HI2 = 0x2E600, 0x2E632, 0x2E732, 0x2E772, 0x2E7C2
SC_PH8, SC_PH6, SC_PH4 = 0x2E7DC, 0x2EBDC, 0x2EEDC
SC_LOW = (0x2F0DC, 0x2F11C, 0x2F15C)
DIST = 0x2F4F8
OWN_CELL_LIST = (4, 0xFF)


def scaled_altitude(P):
    d0 = P.a_long
    if not P.f895 and d0 <= 0x7FFF0:
        d0 = asr(d0, 4)
        q = 0x8000 // (2 if s16(P.zoom3) < 2 else (P.zoom3 & 0xFFFF))
        d0 = asr((d0 & 0xFFFF) * (q & 0xFFFF), 4)
    return s32(d0)


def scan_list(D, P, fine, A, quad):
    """Address of the byte list of cell types for this pass (None for the own-cell list, which is returned as OWN_CELL_LIST bytes)."""
    if not fine:
        if A <= 0x5000:
            return ("a", SC_COARSE_NEAR + 4 * quad)
        return ("a", SC_COARSE_FAR + 16 * quad)
    if A > 0x9000:
        return ("a", SC_HI1 if A <= 0x10000 else SC_HI2)
    oct_ = P.octant
    if A <= 0x3A0:
        base = SC_LOW[2] if A <= 0x10 else (SC_LOW[1] if A <= 0x160 else SC_LOW[0])
        a = base + 4 * (P.phase & 0xFF)
        b0 = D.sb(a)
        if b0 < 0 or oct_ in (D.sb(a), D.sb(a + 1), D.sb(a + 2), D.sb(a + 3)):
            return ("own", None)
    ph = P.phase
    a0, a1, a2 = SC_PH4 + 32 * ph, SC_PH6 + 48 * ph, SC_PH8 + 64 * ph
    if A > 0x4800:
        return ("a", a2 + 8 * oct_)
    if A > 0x3000:
        return ("a", a1 + 6 * oct_)
    return ("a", a0 + 4 * oct_)


def one_pass(D, P, fine, A, out):
    rec = (P.x, P.y, P.z)
    x, y, z = [v & 0xFFFFFFFF for v in rec]
    xw, zw = (x >> 16) & 0xFFFF, (z >> 16) & 0xFFFF
    shift = 8 if fine else 12
    col, row = (xw >> shift), (zw >> shift)
    if not fine:
        col += 4; row += 4
    quad = 0
    if not fine:
        qx = 3 - ((xw >> 10) & 3)
        qz = 3 - ((zw >> 10) & 3)
        quad = qz * 4 + qx
    kind, a = scan_list(D, P, fine, A, quad)
    lst = list(OWN_CELL_LIST) if kind == "own" else None
    maxc = 0x1F if fine else 7
    grid = 0x47F40 if fine else 0x47D7C
    mask = 0xFFFFFF if fine else 0xFFFFFFF
    # matrix
    M = [s16(v) for v in P.M]
    pos = 0
    while True:
        byte = lst[pos] if lst is not None else D.u8(a + pos)
        pos += 1
        if byte == 0xFF:
            return
        last = byte & 0x80
        e = byte & 0x7F
        shv = near = 0
        altc = 0
        if fine and not P.f865:
            if e == 4:
                altc = P.f954
                shv = 2 if A <= 0x400 else (1 if A <= 0xC80 else 0)
            elif A <= 0xC80:
                shv = 1
                near = 1
        dx, dz = D.sb(OFFTAB + 2 * e), D.sb(OFFTAB + 2 * e + 1)
        c, r = col + dx, row + dz
        ok = 0 <= c <= maxc and 0 <= r <= maxc
        if ok:
            w = D.w(grid + 2 * c + (64 if fine else 16) * r)
            if w > 0:
                rec_a = grid + w
                if D.w(rec_a) >= 0:
                    cell(D, P, fine, A, out, rec_a, x, y, z, mask, dx, dz, shv, near, altc, M)
        if last:
            return


def cell(D, P, fine, A, out, rec_a, x, y, z, mask, dx, dz, shv, near, altc, M):
    d0 = s32(-(x & mask) + ((dx << (24 if fine else 28)) & 0xFFFFFFFF))
    d1 = s32(-(z & mask) + ((dz << (24 if fine else 28)) & 0xFFFFFFFF))
    if altc:
        d7 = 1
    elif not near:
        d7 = 0
    else:
        i = min(asr(A, 8), 0x11)
        d6 = D.u16(DIST + 2 * i)
        if not P.f895:
            q = 0x8000 // (2 if s16(P.zoom3) < 2 else (P.zoom3 & 0xFFFF))
            d6 = asr(d6 * q, 8)
        d6 <<= 16
        # the original keeps the x distance itself in d7 when the x test fails and later tests only its low word (quirk: zero when x is a multiple of 65536)
        dxc, dzc = abs(d0 + 0x800000), abs(d1 + 0x800000)
        d7 = (dxc & 0xFFFF) if dxc > d6 else (1 if dzc > d6 else 0)
        d7 = 1 if d7 else 0
    if fine:
        xo, zo = s16(d0 >> 12), s16(d1 >> 12)
    else:
        xo, zo = s16(d0 >> 16), s16(d1 >> 16)
    head = D.l(rec_a)
    if head & 0x80000000:
        return
    # height terms
    if fine:
        h = s16((y + 0x1000) >> 12)
    else:
        h = s16(y >> 16)
    nh = s16(-h)
    ty = [asr(nh * M[1], 8), asr(nh * M[4], 8), asr(nh * M[7], 8)]
    tx, tyy, tz = [s16(v) for v in ty]
    a = head if d7 else rec_a + 4
    while True:
        n = D.w(a); a += 2
        if n <= 0:
            if n == -1:
                return
            g = s16(((n & 0x7FFF) << 2))
            if g > A:
                return
            n = D.w(a); a += 2
        if n > 0x12:
            return
        verts = []
        cnt = n
        while True:
            vx, vz = D.w(a), D.w(a + 2); a += 4
            px = s16(xo + vx)
            pz = s16(zo + vz)
            cx = s16(s16(asr(px * M[0] + pz * M[2], 8) + tx) << shv)
            cy = s16(s16(asr(px * M[3] + pz * M[5], 8) + tyy) << shv)
            cz = s16(s16(asr(px * M[6] + pz * M[8], 8) + tz) << shv)
            verts.append((cx, cy, cz))
            cnt -= 1
            if not cnt > 0:
                break
        out.append((n, verts))


def mesh(P, D=None):
    D = D or Data()
    out = []
    A = scaled_altitude(P)
    if A > 0x3F8:
        one_pass(D, P, False, A, out)
    one_pass(D, P, True, A, out)
    return out
