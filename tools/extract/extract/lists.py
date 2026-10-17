"""Display-list builder tables (docs/re/04a-display-lists.md, Constants; sections 3, 5b and 7).

Everything is read from the relocated flat image. Verbatim tables are written as they are in
the image (big endian); the limit table is written as little-endian i16; the footprint and
reference data are decoded and written as little-endian i16 (see ``decode_land``).
"""
import struct
from typing import Dict, List, Optional, Tuple

from .tables import IMAGE_BASE, RECORD_COUNT, RECORD_SIZE, RECORD_TABLE, DecodeError, Mem, decode_record

# name -> (address, size)
VERBATIM = {
    "lists_quad.bin": (0x4625C, 16),
    "lists_octperm.bin": (0x4626C, 32),
    "lists_group.bin": (0x4628C, 16),
    "lists_subperm.bin": (0x4629C, 64),
    "lists_tileperm.bin": (0x462DC, 96),
    "lists_tiles.bin": (0x20674, 42),
}
# name -> (address, size, number of offset-table words)
BLOBS = {
    "lists_steep.bin": (0x4633A, 610, 4),
    "lists_high.bin": (0x4659C, 3042, 32),
    "lists_low.bin": (0x4717E, 502, 64),
}
LIMIT_ADDR = 0x21022
LIMIT_WORDS = 8
# the two 12-word tables of the drawer's visibility pre-test (note 04a section 8b, rule 6): 0x23FF4 (camera above -0x80) then 0x2400C; i16 LE
VIS_ADDR = 0x23FF4
VIS_WORDS = 24
MAX_ENTRIES = 14
# the near-ground pass, stage [90] (note 04, "Stage [90]: the near-ground pass"): the 32 by 32 class table at 0x2C440 (bytes, verbatim), the three
# point tables 0x2C840 (A, 96 entries), 0x2CA84 (B, 80), 0x2CC68 (C, 64) as one 1452-byte span of (count, class limit) and (x, z, k) word triples
# (written as little-endian i16), and the six triangle shapes 0x2CDEC..0x2CE33 (three (dx, dz) word pairs each, little-endian i16)
NG_CLASS_ADDR, NG_CLASS_SIZE = 0x2C440, 1024
NG_POINTS_ADDR, NG_POINTS_SIZE = 0x2C840, 1452
NG_SHAPES_ADDR, NG_SHAPES_SIZE = 0x2CDEC, 72

CLASSES = {0x21C40: 1, 0x21C34: 2, 0x21D0C: 3, 0x22CFC: 4, 0x22D0A: 5,
           0x21C44: 6, 0x26944: 7, 0x2699E: 8, 0x26A3E: 9}
DESC_SIZE = 8
NONE = 0xFFFF
MAX_NODES = 4
MAX_PLANES = 8
SLOT_SIZE = 256


def _raw(data: bytes, base: int, addr: int, n: int, name: str) -> bytes:
    return Mem(data, base, name).raw(addr, n)


def decode_pattern_lists(blob: bytes, nwords: int, name: str) -> List[List[Tuple[int, List[int]]]]:
    """Parses the lists reachable from the offset table (nwords big-endian words, each a byte
    offset from the start of the blob). Returns, per table word, [(tile, [sub-cells])]."""
    if len(blob) < 2 * nwords:
        raise DecodeError(f"{name}: blob of {len(blob)} bytes is shorter than its offset table")
    out = []
    for i in range(nwords):
        off = struct.unpack_from(">H", blob, 2 * i)[0]
        if off < 2 * nwords or off >= len(blob):
            raise DecodeError(f"{name}: offset word {i} = {off} is outside the blob")
        p, entries = off, []
        while True:
            if p >= len(blob):
                raise DecodeError(f"{name}: list of word {i} runs past the blob")
            t = blob[p]
            if t & 0x80:
                break
            if t > 20:
                raise DecodeError(f"{name}: list of word {i}: tile byte {t} above 20 at offset {p}")
            if len(entries) >= MAX_ENTRIES:
                raise DecodeError(f"{name}: list of word {i} has more than {MAX_ENTRIES} entries")
            if p + 2 > len(blob):
                raise DecodeError(f"{name}: list of word {i} truncated")
            n = blob[p + 1]
            if not 1 <= n <= 16 or p + 2 + n > len(blob):
                raise DecodeError(f"{name}: list of word {i}: bad sub-cell count {n} at offset {p + 1}")
            subs = list(blob[p + 2:p + 2 + n])
            if max(subs) > 15:
                raise DecodeError(f"{name}: list of word {i}: sub-cell above 15 at offset {p + 2}")
            entries.append((t, subs))
            p += 2 + n
        out.append(entries)
    return out


def decode_verbatim_tables(data: bytes, base: int = IMAGE_BASE) -> Dict[str, bytes]:
    out = {}
    for name, (addr, size) in VERBATIM.items():
        out[name] = _raw(data, base, addr, size, name)
    for name, (addr, size, nwords) in BLOBS.items():
        blob = _raw(data, base, addr, size, name)
        decode_pattern_lists(blob, nwords, name)
        out[name] = blob
    m = Mem(data, base, "lists_limit")
    out["lists_limit.bin"] = b"".join(struct.pack("<h", m.s16(LIMIT_ADDR + 2 * i)) for i in range(LIMIT_WORDS))
    mv = Mem(data, base, "lists_vis")
    out["lists_vis.bin"] = b"".join(struct.pack("<h", mv.s16(VIS_ADDR + 2 * i)) for i in range(VIS_WORDS))
    out["lists_ngclass.bin"] = _raw(data, base, NG_CLASS_ADDR, NG_CLASS_SIZE, "lists_ngclass")
    for name, addr, size in (("lists_ngpts.bin", NG_POINTS_ADDR, NG_POINTS_SIZE), ("lists_ngshape.bin", NG_SHAPES_ADDR, NG_SHAPES_SIZE)):
        mn = Mem(data, base, name)
        out[name] = b"".join(struct.pack("<h", mn.s16(addr + 2 * i)) for i in range(size // 2))
    quad, grp = out["lists_quad.bin"], out["lists_group.bin"]
    if max(quad) > 3 or max(grp) > 3:
        raise DecodeError("lists_quad/lists_group: class value above 3")
    tp = out["lists_tileperm.bin"]
    if any(max(tp[24 * r:24 * r + 21]) > 20 for r in range(4)):
        raise DecodeError("lists_tileperm: tile above 20")
    return out


def _first_leaf(m: Mem, M: int) -> int:
    w0 = m.u16(M)
    if w0 & 0x8000:
        return w0
    if w0 & 0x4000:
        return m.u16(M + 2)
    return m.u16(M + 4)


def _box(m: Mem, a: int):
    return [m.s16(a + 2 * i) for i in range(5)]


def decode_footprint(data: bytes, base: int, M: int):
    """([outer box], [(box, [(ref3, normal3)], n)]) of the model at M, or None when not well formed. ``n`` is the node's plane count;
    a node with n = 0 carries one plane (the first dot product is formed before the count is looked at, note 07 8.2 (4))."""
    m = Mem(data, base, "footprint")
    try:
        leaf = _first_leaf(m, M)
        if leaf == 0xFFFF:
            return None
        blk = M + (leaf & 0xFFF)
        F = M + (m.u16(blk) & 0xFFF)
        outer = _box(m, F)
        nodes, a, seen = [], F + 10, set()
        while True:
            if a in seen or len(nodes) >= MAX_NODES:
                return None
            seen.add(a)
            box = _box(m, a)
            nxt = m.u32(a + 10)
            n = m.u16(a + 14)
            if n > MAX_PLANES:
                return None
            planes = []
            for k in range(max(n, 1)):   # a node with a count of 0 still has the one plane the original forms before it looks at the count (note 07 8.2 (4)); stored after the count byte of 0 (extractor version 26)
                p = a + 16 + 12 * k
                planes.append(([m.s16(p + 2 * i) for i in range(3)], [m.s16(p + 6 + 2 * i) for i in range(3)]))
            nodes.append((box, planes, n))
            if nxt & 0x80000000:
                break
            a = nxt
        if 4 + 10 + sum(14 + 6 * n for _, _, n in nodes) > SLOT_SIZE:
            return None
        return outer, nodes
    except DecodeError:
        return None


def _pack_footprint(fp) -> bytes:
    outer, nodes = fp
    b = struct.pack("<5hB", *outer, len(nodes))
    for box, planes, n in nodes:
        b += struct.pack("<5hB", *box, n)
        for ref, nrm in planes:
            b += struct.pack("<6h", *ref, *nrm)
    return b


def decode_reference(data: bytes, base: int, aux: int):
    m = Mem(data, base, "reference planes")
    c = m.u16(aux)
    ptr = m.u32(aux + 6)
    planes = []
    for k in range(c + 1):
        p = ptr + 12 * k
        planes.append([m.s16(p + 2 * i) for i in range(6)])
    return c, planes


def decode_land(data: bytes, base: int = IMAGE_BASE) -> Tuple[bytes, bytes]:
    """(lists_desc.bin, lists_land.bin). Record r of the object record table at 0x25ECC."""
    m = Mem(data, base, "descriptor table")
    desc, land = b"", b""
    for r in range(RECORD_COUNT):
        rec = decode_record(data, RECORD_TABLE - base + RECORD_SIZE * r, r)
        # decode_record reads at an image offset
        cls, flags, foot, ref = 0, 0, NONE, NONE
        if rec.handler or rec.base or rec.detail_list or rec.alt_list or rec.aux:
            cls = CLASSES.get(rec.handler, 255)
        M = rec.base
        if 0 < M < 0x80000000:
            flags |= 1
            container = cls in (1, 2, 3)   # type B models (4, 5) have another container (note 02 section 7)
            try:
                if not container:
                    raise DecodeError('not a detail-list model')
                leaf = _first_leaf(m, M)
                qual = leaf != 0xFFFF and bool(m.u8(M + 7 + (leaf & 0xFFF)) & 0x10)
            except DecodeError:
                qual = False
            fp = decode_footprint(data, base, M) if container else None
            if fp is not None:
                flags |= 4
                foot = len(land)
                land += _pack_footprint(fp)
            if qual and r < 16:
                qual = False   # templates 0..15 are copied into run-time slots; not static landmarks (note 04a section 4)
            if qual:
                flags |= 2
                if rec.aux:
                    c, planes = decode_reference(data, base, rec.aux)
                    ref = len(land)
                    land += struct.pack("<H", c) + b"".join(struct.pack("<6h", *p) for p in planes)
        desc += struct.pack("<BBHHH", cls, flags, foot, ref, 0)
    if len(desc) != RECORD_COUNT * DESC_SIZE or len(land) >= NONE:
        raise DecodeError("lists_desc/lists_land: bad size")
    return desc, land


def decode_all(data: bytes, base: int = IMAGE_BASE) -> Dict[str, bytes]:
    out = decode_verbatim_tables(data, base)
    out["lists_desc.bin"], out["lists_land.bin"] = decode_land(data, base)
    return out
