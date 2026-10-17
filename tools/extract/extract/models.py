"""Model decoder: drawing programs to a neutral intermediate form.

Layouts are those of docs/re/02-assets-and-tables.md sections 2 to 7 and 13.
``decode_model`` is a pure function over the flat image bytes. The intermediate
form (``Model``) and its binary file format ("IMDL" version 5, documented in
tools/extract/README.md) keep the painter order, the per-side face selection
(the BSP elements) and the gate words; flattening to polygons is deliberately
not done.
"""
import struct
from dataclasses import dataclass, field
from typing import List

from .tables import IMAGE_BASE, BlockedError, DecodeError, Mem, decode_record

IMDL_MAGIC = b"IMDL"
IMDL_VERSION = 5

TYPE_B_HANDLERS = (0x22CFC, 0x22D0A)
# Note 02 section 7 / note 03 "Type B models": the zero "placeholder" corners of ids 105, 106,
# 109-132 are literal file data and are exported as ordinary block vertices (state 1).

# Vertex states.
V_UNDEFINED, V_BLOCK, V_STRIP, V_SYNTH, V_UNRESOLVED, V_SECTION = 0, 1, 2, 3, 4, 5

# Element kinds.
E_JUMP, E_AXIS, E_PLANE, E_TRI, E_DRAW = 1, 2, 3, 4, 5

# Item flags.
F_LAST, F_GATED, F_BLOCKED, F_TWO_SIDED, F_BACK_COLOUR, F_PLANE, F_SPECULATIVE = 1, 2, 4, 8, 16, 32, 64
# Element flags (bit 0 of an axis node is its swap bit).
EF_BLOCKED = 2
# The child link stands for a stream end by the word 0xFFFF (not by bit 13 or 14 of a draw item): the drawer returns 0 there (note 04a 8b rule 9).
EF_END0, EF_END1 = 8, 16

# Primitive kinds (the "decoded primitive kind" of the note's section 13).
(P_NOP, P_POINT, P_LINE, P_POLYGON, P_FACES, P_QUAD, P_RUNGS, P_SEGMENTS, P_LINELIST,
 P_HATCH, P_SYNTH, P_SELECTOR, P_POLYLIST, P_TRIANGLE, P_EARLYOUT, P_FIGURE, P_RAW) = range(17)

SLOT_LIMIT = 512
MAX_STREAM_ELEMENTS = 4096


class Blocked(Exception):
    """Internal: a documented BLOCKING item was reached."""


@dataclass
class Level:
    threshold: int = 0
    shape: int = 0          # 0 jump node, 1 inline stream item, 2 leaf only, 3 type B entry
    present: int = 1        # 0 when the leaf is 0xFFFF (draw nothing)
    leaf: int = 0
    part_id: int = 0
    tm: int = 0
    tf: int = 0
    scale_h: int = 0
    scale_l: int = 0
    block_flags: int = 0
    b8: int = 0
    b9: int = 0
    root: int = -1
    vertex_first: int = 0
    vertex_count: int = 0
    elem_first: int = 0
    elem_count: int = 0
    sec_first: int = 0      # slice of Model.secpool (words); sec_count 0: no second section
    sec_count: int = 0
    data_triple: tuple = (0, 0, 0)   # the three words that follow the block's b8 vertices (a non-flat block): the draw's vertex loop rotates them into slot b8 when b8 = b9 (note 07 8.10 rule 5)


@dataclass
class Vertex:
    x: int
    y: int
    z: int
    state: int


@dataclass
class Element:
    kind: int
    flags: int = 0          # axis: bit 0 swap
    axis: int = 0           # axis node: 1 y, 2 x, 3 z
    scale: int = 0
    child0: int = -1
    child1: int = -1
    s: List[int] = field(default_factory=lambda: [0] * 6)
    list_first: int = 0
    list_count: int = 0


@dataclass
class Program:
    item_first: int
    item_count: int
    address: int


@dataclass
class Item:
    op: int
    flags: int = 0
    g1: int = 0x7FFF
    g2: int = 0
    extra_n: int = 0
    extra_slot: int = 0
    pen: int = 0
    w: int = 0
    wback: int = 0
    prim: int = P_NOP
    nops: int = 0
    op_first: int = 0
    address: int = 0
    length: int = 0         # bytes of the item in the source (for skip resolution)
    skip_first: int = 0     # slice of Model.skips: candidate skips of a state selector
    skip_count: int = 0
    skips: List[int] = field(default_factory=list)    # decoder-side only; stored via the pool


@dataclass
class Model:
    id: int
    handler: int
    kind: int               # 0 3D, 1 flat, 2 type B
    levels: List[Level] = field(default_factory=list)
    vertices: List[Vertex] = field(default_factory=list)
    elements: List[Element] = field(default_factory=list)
    proglists: List[int] = field(default_factory=list)
    programs: List[Program] = field(default_factory=list)
    items: List[Item] = field(default_factory=list)
    operands: List[int] = field(default_factory=list)
    skips: List[int] = field(default_factory=list)
    secpool: List[int] = field(default_factory=list)   # second-section streams (see _read_second_section)
    blocked: int = 0        # items flagged F_BLOCKED (lenient mode only)
    nnormal: int = -1       # normal levels; the levels after them are the cockpit-alternate ones (-1: all)
    alt_near: int = 0xFFFF  # level index of the near body (alternate list), 0xFFFF none
    alt_cockpit: int = 0xFFFF
    hull_flags: int = 0     # the block flag byte (+7) of the block 0x2ADDE reads for the gun's box (note 07 8.2): bit 4 = a big hull; stored in the pad byte of every level record
    phase_first: int = 0    # 1 + the level index of the first of the four phase lists of the cone models (note 07 11.5), 0 none


# ---------------------------------------------------------------------------
# Operation table (section 5)

# op -> (name, primitive kind). Operations absent here are unknown.
OPS = {
    0: P_NOP, 1: P_POINT, 2: P_LINE, 3: P_POLYGON, 4: P_FACES, 6: P_RUNGS, 7: P_TRIANGLE,
    8: P_TRIANGLE, 9: P_QUAD, 10: P_QUAD, 11: P_SEGMENTS, 12: P_SEGMENTS, 13: P_LINELIST,
    14: P_LINELIST, 15: P_QUAD, 16: P_QUAD, 17: P_QUAD, 18: P_QUAD, 19: P_HATCH, 20: P_HATCH,
    21: P_HATCH, 22: P_HATCH, 23: P_HATCH, 24: P_POLYGON, 25: P_POLYGON, 26: P_POLYGON,
    27: P_QUAD, 30: P_QUAD, 31: P_EARLYOUT, 32: P_HATCH, 33: P_NOP, 34: P_SYNTH, 35: P_SYNTH,
    36: P_SYNTH, 38: P_SYNTH, 46: P_SYNTH, 47: P_SYNTH, 39: P_QUAD, 40: P_TRIANGLE, 41: P_SYNTH, 42: P_SYNTH, 43: P_SYNTH,
    44: P_SYNTH, 45: P_SYNTH, 48: P_LINE, 49: P_POINT, 50: P_SELECTOR, 51: P_SELECTOR,
    52: P_SYNTH, 53: P_SYNTH, 54: P_SELECTOR, 55: P_POLYLIST, 56: P_SYNTH, 57: P_HATCH,
    58: P_POINT, 59: P_SYNTH, 62: P_FIGURE, 63: P_SYNTH, 64: P_SYNTH, 65: P_SYNTH,
    66: P_SELECTOR, 67: P_SELECTOR, 68: P_SELECTOR, 69: P_QUAD, 70: P_SELECTOR, 71: P_FIGURE, 72: P_SELECTOR, 73: P_SELECTOR,
    74: P_LINE, 75: P_RAW, 76: P_SELECTOR, 77: P_SELECTOR,
}
BLOCKING_OPS = {}    # ops 7 and 8 are specified in note 03 ("Ops 7 and 8"): not blocking

# Selector skips (bytes after the selector's end). The list of a selector item has the
# state-zero choice first; the other entries are the alternatives the extractor follows
# to emit state variants. Op 52 carries no list: its skip (14 bytes per unit of the gear progress, note 03 "Ops 52 and 53") is applied by the core's program runner, which
# runs the n-th of the six op 53 items (all six are decoded through op 53's own list below, so the stream has them); the extractor's baked slots are the state-zero path's.
# Ops 77 (10 per unit), 45, 56, 63-65 have unbounded or undocumented ranges and carry no list (their variants are not decoded).
# Op 70 (the timer skip of the effect models 33 to 35, note 07 11.3) carries the list of every skip its life word t = 0 to 0x7F can make (entry 0: the steady picture's first variant, 2 bytes, which
# the geometry preview follows), so that the items of all four variants and of the ascending and descending runs are decoded; the core's program runner applies the formula to the object's life word.
FRAMES = 16
SPECULATIVE_OPS = (68, 72)    # k = 1..15 frames: followed, items reached this way are flagged


def op70_skip(t):
    """The bytes op 70 skips for the life word t (note 07 11.3): 0 for a negative word (the empty item that follows ends the program), else 2 plus 0x86 per variant k = (t & 7) >> 1,
    plus 0x26 and 8 per step of (0x0B - t) >> 1 below 0x0C, plus 0x56 and 8 per step of (t - 0x74) >> 1 above 0x74."""
    if t < 0:
        return 0
    s = 2 + 0x86 * ((t & 7) >> 1)
    if t < 0x0C:
        s += 0x26 + 8 * ((0x0B - t) >> 1)
    if t > 0x74:
        s += 0x56 + 8 * ((t - 0x74) >> 1)
    return s


def skip_list(op, ops):
    if op == 70:
        return [2] + sorted({op70_skip(t) for t in range(0x80)} - {2})
    if op == 50:
        return [0, 0x14, 0x52, 0x7C, 0xA6, 0xD0, 0xFA]
    if op == 51:
        return [0, ops[0]]
    if op == 53:
        return [ops[2], 0]
    if op in (54, 76):
        return [2, 0]
    if op == 66:
        return [0, 8]
    if op == 67:
        return [0, 18]
    if op == 73:
        return [0, 2]
    if op == 68:
        return [0] + [0x34 * k for k in range(1, FRAMES)]
    if op == 72:
        return [0] + [0x12 * k for k in range(1, FRAMES)]
    return []


SYNTH_OPS = (34, 35, 36, 38, 41, 42, 43, 44, 45, 46, 47, 52, 53, 56, 59, 63, 64, 65)
# FIXED_SYNTH: no operand, fixed formulas on the absolute slots (note 03 'Fixed-formula synthesis'); 46 = 38, 47 = 45.
FIXED_SYNTH = {38: 38, 46: 38, 45: 45, 47: 45, 56: 56, 63: 63, 64: 64, 65: 65}
# State-dependent operations of which the note does not say what they do to the vertex array.
UNDOCUMENTED_BUILDERS = ()


class _Reader:
    def __init__(self, mem: Mem, addr: int, model_id: int):
        self.mem = mem
        self.addr = addr
        self.start = addr
        self.model_id = model_id

    def w(self):
        v = self.mem.u16(self.addr)
        self.addr += 2
        return v

    def sw(self):
        v = self.mem.s16(self.addr)
        self.addr += 2
        return v

    def peek(self):
        return self.mem.u16(self.addr)

    def long(self):
        v = self.mem.u32(self.addr)
        self.addr += 4
        return v


class _Ctx:
    """Per-level decoding state: block geometry, extra ranges, slot references."""

    def __init__(self, mem, model_id, kind, block_addr, strict):
        self.mem = mem
        self.id = model_id
        self.kind = kind          # 0 3D, 1 flat, 2 type B
        self.block_addr = block_addr
        self.strict = strict
        self.extras = []          # (first slot, count)
        self.synth = []           # (op, args, address) in straight-path order
        self.scale_l = 0
        self.model_base = 0
        self.block_hint = None

    def err(self, msg, addr=None, op=None, blocked=False):
        o = "" if op is None else f": op {op}"
        w = "" if addr is None else f" at 0x{addr:X}"
        raise (BlockedError if blocked else DecodeError)(f"model {self.id}{o}{w}: {msg}")


def _slot(ctx, r, word, item_addr, op, last_bit=False):
    """Vertex byte offset to slot index; ``last_bit`` strips and returns bit 15."""
    last = False
    if last_bit:
        last = bool(word & 0x8000)
        word &= 0x7FFF
    if word % 6:
        ctx.err(f"vertex operand {word} is not a multiple of 6", item_addr, op)
    s = word // 6
    if s >= SLOT_LIMIT:
        ctx.err(f"bad vertex slot {s}", item_addr, op)
    return (s, last) if last_bit else s


def _poly_tail(ctx, r, item, ops):
    """W word of a polygon (section 6) and what follows it. Observed in the data
    and missing from the note: with W bit 12 set, bits 10-11 clear and the polygon
    one-sided, an offset word follows that addresses a 12-byte plane record
    (point and normal, six words) at model base + offset; it is copied into the
    operands (after the vertex slots)."""
    W = r.w()
    item.w = W
    item.pen = W & 0xF
    if W & 0x8000:
        item.flags |= F_TWO_SIDED
        return
    if W & 0x1000 and not (W & 0x0C00):
        off = r.w()
        item.flags |= F_PLANE
        pa = ctx.model_base + off
        ops.extend(ctx.mem.s16(pa + 2 * k) for k in range(6))
    if W & 0x4000:
        item.flags |= F_BACK_COLOUR
        item.wback = r.w()


def _parse_item(ctx: _Ctx, addr: int):
    """Decode one program item at ``addr``. Returns (item, operands, slot refs,
    synth record or None, next address)."""
    mem = ctx.mem
    r = _Reader(mem, addr, ctx.id)
    item = Item(op=0, address=addr)
    first = r.peek()
    if first & 0x8000:
        opword = r.w()
    else:
        item.flags |= F_GATED
        item.g1 = r.w()
        if item.g1 != 0x7FFF:
            item.g2 = r.w()
        n = r.sw()
        if n > 0:
            start = r.w()
            item.extra_n = n
            item.extra_slot = start       # byte offset; converted once the block kind is known
        opword = r.w()
    if opword & 3:
        ctx.err(f"operation word 0x{opword:04X} is not a multiple of 4", addr)
    op = (opword & 0x3FFF) >> 2
    item.op = op
    if opword & 0x4000:
        item.flags |= F_LAST
    if op not in OPS:
        ctx.err(f"unknown operation {op}", addr, op)
    item.prim = OPS[op]
    ops = []
    slots = []
    synth = None

    def S(word, last_bit=False):
        res = _slot(ctx, r, word, addr, op, last_bit)
        if last_bit:
            slots.append(res[0])
        else:
            slots.append(res)
        return res

    def slot_range(first, n):
        slots.extend(range(first, first + n))

    def colour(word):
        return word & 0xF

    if op in BLOCKING_OPS:
        words = 5 if op == 7 else 6
        ops.extend(r.w() for _ in range(words))
        item.flags |= F_BLOCKED
        item.prim = P_TRIANGLE
        if ctx.strict:
            ctx.err("BLOCKING: " + BLOCKING_OPS[op], addr, op, blocked=True)
    elif op in (7, 8):
        # Note 03: op 7 = pen, a, b|flip, c, d; op 8 = pen, c, a, b|flip, f, d. a, b are block byte
        # offsets, c, f, d slot byte offsets (d the base the slots below are numbered from).
        ops = [r.w() for _ in range(5 if op == 7 else 6)]
        item.pen = ops[0] & 0xF
        item.prim = P_TRIANGLE
        c, f, d = (ops[3], None, ops[4]) if op == 7 else (ops[1], ops[4], ops[5])
        cs = S(c)
        base = S(d)
        # slots named by the formulas: c (and c+1, f for op 8), base+0..2 (op 7) or base+0..3 (op 8)
        slots.extend(range(base, base + (3 if op == 7 else 4)))
        if op == 8:
            slots.append(cs + 1)
            S(f)
    elif op in FIXED_SYNTH:
        synth = (FIXED_SYNTH[op], 0, 0)
    elif op in (0, 33, 50, 54, 66, 68, 72, 73, 76, 77):
        pass
    elif op == 1:
        v = S(r.w()); item.pen = colour(r.w()); ops = [v]
    elif op in (2, 48, 74):
        a = S(r.w()); b = S(r.w()); item.pen = colour(r.w()); ops = [a, b]
    elif op == 3:
        vs = []
        while True:
            s, last = S(r.w(), True)
            vs.append(s)
            if last:
                break
        ops = [len(vs)] + vs
        _poly_tail(ctx, r, item, ops)
    elif op == 4:
        faces = []
        while True:
            d = r.w()
            if d & 0x8000:
                break
            n = r.w()
            flag = r.w()
            vs = [S(r.w()) for _ in range(n)]
            faces.append([d, n, flag] + vs)
        ops.append(len(faces))
        for f in faces:
            ops.extend(f)
    elif op == 6:
        n = r.w(); vs = [S(r.w()) for _ in range(4)]; item.pen = colour(r.w()); ops = [n] + vs
    elif op in (9, 10):
        item.pen = colour(r.w()); v = S(r.w()); slot_range(v, 3 if op == 9 else 4); ops = [v]
    elif op in (11, 12):
        wd = r.w(); item.w = wd; item.pen = colour(wd)
        cnt = wd >> 8
        if op == 12:
            vr = S(r.w()); slots.append(vr + 1)
            v = S(r.w()); ops = [cnt, vr, v]
        else:
            v = S(r.w()); ops = [cnt, v]
        slot_range(v, 2 * cnt)
    elif op in (13, 14):
        item.pen = colour(r.w())
        if op == 14:
            vr = S(r.w()); slots.append(vr + 1); ops.append(vr)
        while True:
            a = S(r.w()); b, last = S(r.w(), True)
            ops += [a, b]
            if last:
                break
    elif op == 15:
        item.pen = colour(r.w()); a, b, c, e = (S(r.w()) for _ in range(4)); slots.append(a + 1)
        ops = [a, b, c, e]
    elif op == 16:
        item.pen = colour(r.w()); a, b, c = (S(r.w()) for _ in range(3)); slots.append(b + 1)
        ops = [a, b, c]
    elif op in (17, 18):
        item.pen = 12
        while True:
            if r.peek() & 0x8000:
                r.w()
                break
            ops.extend(S(r.w()) for _ in range(4))
    elif op in (19, 22, 32):
        item.pen = colour(r.w()); v = S(r.w()); slot_range(v, 3)
        if op == 22:
            n1 = r.w(); inner = [r.w() for _ in range(n1)]; ops = [v, n1] + inner
        else:
            ops = [v, r.w()]
    elif op in (20, 23):
        v = S(r.w()); slot_range(v, 3); item.pen = 13 if op == 20 else 0; ops = [v, r.w()]
    elif op == 21:
        v = S(r.w()); slot_range(v, 3); item.pen = 13
        n1 = r.w(); ops = [v, n1] + [r.w() for _ in range(n1)]
    elif op == 57:
        item.pen = colour(r.w()); v = S(r.w()); slot_range(v, 3)
        n1 = r.w(); ops = [v, n1] + [r.w() for _ in range(n1)]
    elif op == 24:
        n = r.w(); vs = [S(r.w()) for _ in range(n)]; item.pen = colour(r.w()); ops = [n] + vs
    elif op == 25:
        n = r.w(); item.pen = colour(r.w()); ops = [n]; slot_range(0, n)
    elif op == 26:
        n = r.w(); v = S(r.w()); slot_range(v, n); item.pen = 7; ops = [n, v]
    elif op in (27, 30):
        item.pen = colour(r.w()); v = S(r.w()); slot_range(v, 3); ops = [v]
    elif op == 31:
        ops = [r.w()]
    elif op == 34 or op == 36:
        v = _slot(ctx, r, r.w(), addr, op); s = r.sw(); ops = [v, s]
        synth = (op, v, s)
    elif op == 41:
        v = _slot(ctx, r, r.w(), addr, op); s = r.sw(); ops = [v, s]
        synth = (op, v, s)
    elif op in (35, 42, 43, 44, 59):
        v = _slot(ctx, r, r.w(), addr, op); ops = [v]
        synth = (op, v, 0)
    elif op in (39, 40):
        ops = [r.w() for _ in range(3 if op == 39 else 2)]
        slots.extend((0, 1, 2, 3) if op == 39 else (0, 1, 4))     # absolute slots (note 5)
    elif op == 49:
        a, b, c = (S(r.w()) for _ in range(3)); W = r.w(); item.w = W; ops = [a, b, c]
    elif op == 51:
        ops = [r.w()]
    elif op == 52:
        a, b, t = (_slot(ctx, r, r.w(), addr, op) for _ in range(3)); ops = [a, b, t]
        synth = (52, a, b, t)
    elif op == 53:
        a = _slot(ctx, r, r.w(), addr, op); b = _slot(ctx, r, r.w(), addr, op); skip = r.w()
        ops = [a, b, skip]; synth = (53, a, b)
    elif op == 55:
        base = r.long()
        offs = []
        while True:
            o = r.w()
            if o & 0x8000:
                break
            offs.append(o)
        polys = []
        for o in offs:
            pr = _Reader(mem, base + o, ctx.id)
            vs = []
            while True:
                s, last = _slot(ctx, pr, pr.w(), base + o, op, True)
                vs.append(s)
                if last:
                    break
            pitem = Item(op=3, address=base + o)
            pops = [len(vs)] + vs
            _poly_tail(ctx, pr, pitem, pops)
            polys.append([pitem.w, pitem.wback, pitem.flags] + pops)
            slots.extend(vs)
        ops = [len(polys)] + [x for p in polys for x in p]
    elif op == 58:
        v = S(r.w()); item.pen = colour(r.w()); ops = [v]
    elif op == 62:
        ops = [r.w() for _ in range(3)]
    elif op == 67:
        vs = [S(r.w()) for _ in range(3)]; W = r.w(); item.w = W; ops = vs
    elif op == 69:
        a, b, c = (S(r.w()) for _ in range(3)); ops = [a, b, c]
        _poly_tail(ctx, r, item, ops)
    elif op == 70:
        ops = []      # the op word alone: the skip is a function of the object's life word (skip_list)
    elif op == 71:
        # a list of discs as op 62: triples (vertex byte offset, pen, size) up to the one whose size word has bit 15 set (note 03, op 71); ops = [count, then the triples with the bit removed]
        trip = []
        while True:
            v, pen, size = r.w(), r.w(), r.w()
            trip.extend((v, pen, size & 0x7FFF))
            if size & 0x8000:
                break
        ops = [len(trip) // 3] + trip
    elif op == 75:
        ops = [r.w() for _ in range(5)]
    else:   # pragma: no cover - OPS and this chain are kept in step by a test
        ctx.err(f"operation {op} has no argument layout", addr, op)
    item.length = r.addr - addr
    item.skips = skip_list(op, ops)
    return item, ops, slots, synth, r.addr


def _decode_program(ctx: _Ctx, start: int):
    """All items of the program at ``start`` including the state variants reachable
    through the selector skip lists. Returns the items sorted by address and the
    addresses of the state-zero path: from the start, following each selector's
    state-zero skip (``skips[0]``), up to the first last-flagged item reached."""
    found = {}          # address -> (item, ops, slots, synth)
    pending = [(start, False)]
    while pending:
        a, spec = pending.pop(0)
        while a not in found:
            item, ops, slots, synth, end = _parse_item(ctx, a)
            if spec:
                item.flags |= F_SPECULATIVE
            found[a] = (item, ops, slots, synth)
            for sk in item.skips:
                if sk:
                    pending.append((end + sk, spec or item.op in SPECULATIVE_OPS))
            a = end
            if item.flags & F_LAST:
                break
    order = sorted(found)
    for a, b in zip(order, order[1:]):
        if a + found[a][0].length > b:
            ctx.err(f"skip target 0x{b:X} falls inside the item at 0x{a:X}", b, found[a][0].op)
    straight = []
    a = start
    while True:
        item = found[a][0]
        straight.append(a)
        if item.flags & F_LAST:
            break
        a = a + item.length + (item.skips[0] if item.skips else 0)
        if a not in found:
            ctx.err(f"state-zero path leaves the decoded items at 0x{a:X}", item.address, item.op)
    return [found[a] for a in order], straight


# ---------------------------------------------------------------------------
# Block, strip section, synthesis

def _sh(v, s):
    """Shift left by s (right when s is negative), per component."""
    return tuple((x << s) if s >= 0 else (x >> -s) for x in v)


def _segment(prev, b, n):
    """Points after ``prev`` up to and including ``b`` for a division count n:
    n-1 interior points with four extra fraction bits and truncating division,
    then b. n of 1 or less means no cut."""
    if n <= 1:
        return [b]
    out = []
    steps = []
    for p, q in zip(prev, b):
        d = (q - p) << 4
        steps.append(int(d / n))        # truncating division
    for k in range(1, n):
        out.append(tuple(p + ((st * k) >> 4) for p, st in zip(prev, steps)))
    out.append(b)
    return out


def _strip_slots(ctx: _Ctx, pos: int, first_slot: int):
    """Decode the strip section of a 3D block (section 2.4); returns {slot: xyz}."""
    mem = ctx.mem
    slots = {}
    s0 = first_slot
    while True:
        c = mem.s16(pos)
        pos += 2
        if c < 0:
            break
        a = tuple(mem.s16(pos + 2 * k) for k in range(3))
        pos += 6
        fwd = [a]
        prev = a
        for _ in range(c):
            b = tuple(mem.s16(pos + 2 * k) for k in range(3))
            n = mem.s16(pos + 6)
            pos += 8
            fwd.extend(_segment(prev, b, n))
            prev = b
        for i, p in enumerate(fwd):
            slots[s0 + 2 * i] = p
        end = s0 + 2 * len(fwd)       # one past the last forward point
        d = mem.u16(pos)
        pos += 2
        count = d & 0x7FFF
        pts = []
        for _ in range(count):
            b = tuple(mem.s16(pos + 2 * k) for k in range(3))
            n = mem.s16(pos + 6)
            pos += 8
            pts.extend(_segment(prev, b, n))
            prev = b
        slot = end - (1 if d & 0x8000 else 3)
        for p in pts:
            if slot < s0:
                ctx.err("strip backward pass runs below its chain", pos)
            slots[slot] = p
            slot -= 2
        s0 = end
        if len(slots) > SLOT_LIMIT:
            ctx.err("strip section too long", pos)
    return slots


class _Miss(Exception):
    """A source slot is not defined (or unresolved)."""


class _W:
    """A 3-vector of 16-bit words; every operation wraps to 16 bits (note 03, vertex synthesis)."""
    __slots__ = ("v",)

    def __init__(self, v):
        self.v = tuple(((x + 0x8000) & 0xFFFF) - 0x8000 for x in v)

    def __add__(self, o):
        return _W(a + b for a, b in zip(self.v, o.v))

    def __sub__(self, o):
        return _W(a - b for a, b in zip(self.v, o.v))

    def __mul__(self, n):
        return _W(a * n for a in self.v)

    __rmul__ = __mul__

    def h(self):
        return _W(a >> 1 for a in self.v)

    def q(self):
        return _W(a >> 2 for a in self.v)


# Note 03 "Vertex synthesis (ops 41-44, 59)": new slot -> function of (V, B), where V(k) reads
# slot base+k and B(k) the same (slots written earlier in the same op are read back by number).
def _shl(w, s):
    """A word vector shifted left by s (arithmetic right by -s when s is negative), wrapped."""
    return _W(_sh(w.v, s))


def _s34(V, s, full=True):
    r = {4: V(0) - V(1) + V(2), 5: V(0) - V(1) + V(3), 6: V(3) + V(2) - V(1),
         7: V(0) - 2 * V(1) + V(2) + V(3)}
    if full:
        d = _shl(V(2) - V(3), s)
        r[8] = V(3) - d
        r[9] = V(3) - d - V(1) + V(2)
    return r


def _s35(V, s):
    return _s34(V, s, False)


def _s36(V, s):
    r = {6: V(0) - V(1) + V(2), 7: V(0) - V(1) + V(3), 8: V(0) - V(1) + V(4),
         9: V(0) - V(1) + V(5), 10: V(2) + V(5) - V(1), 11: V(0) - 2 * V(1) + V(2) + V(5)}
    d = _shl(V(3) - V(4), s)
    r[12] = V(4) - d
    r[13] = V(4) - d - V(2) + V(3)
    return r


def _s41(V, s):
    d = _shl(V(2) - V(3), s)
    r = {9: V(3) + V(2) - V(1), 10: V(3) - d}
    r[11] = V(5) - V(6) + V(7)
    r[12] = r[11] + V(8) - V(6)
    t = V(4) - V(6)
    r[13] = V(5) + t
    r[14] = V(8) + t
    r[15] = V(7) + t
    r[16] = r[11] + t
    r[17] = r[12] + t
    return r


def _M(V):
    return (V(1) + V(2)).h()


def _s38(V, s):
    return {11: V(0) + V(1) - V(2), 12: V(0) + V(1) - V(4), 13: V(0) + V(1) - V(3),
            14: V(0) - V(1) + V(6), 15: V(2) + V(6) - V(1), 16: V(0) - V(2) + V(6),
            17: V(3) - V(4) + V(9), 18: V(0) + V(1) - 2 * V(4) + V(9),
            19: V(0) + V(1) - V(3) - V(4) + V(9), 20: 3 * V(5) - 2 * V(7)}


def _s45(V, s):
    M = _M(V)
    r = {5: M, 6: 2 * M - V(3), 7: M + (V(0) - M).q(),
         8: V(1) + V(3) + V(4) - 2 * M, 9: V(2) + V(3) + V(4) - 2 * M, 10: 2 * V(3) + V(4) - 2 * M}
    r[11] = 3 * r[7] - 2 * V(0)
    return r


def _s56(V, s):
    return {15: 2 * V(4) - V(7), 16: 2 * V(4) - V(8), 17: 2 * V(4) - V(9), 18: 2 * V(4) - V(10),
            19: 2 * V(3) - V(11), 20: 2 * V(3) - V(12), 21: 2 * V(3) - V(13), 22: 2 * V(3) - V(14)}


def _s63(V, s):
    return {16: V(0) - V(1) + V(2), 17: V(3) - V(4) + V(5), 18: V(7) - V(8) + V(9),
            19: V(10) - V(11) + V(12), 20: V(13) - V(14) + V(15)}


def _s64(V, s):
    M = _M(V)
    return {23: V(1) + V(2) - V(0), 24: M, 25: V(0) - (M - V(0)).h(),
            26: V(0) - (M - V(0)).h() - (M - V(1)).h(), 27: V(1) - (M - V(1)).h(),
            28: V(1) - (M - V(0)).h() - (M - V(1)).h()}


def _s65(V, s):
    return {23: V(1) + V(2) - V(0), 24: V(2) + V(5) - V(0)}


# Dict-style syntheses (op -> (function of (V, s), slots written)): every result is a word vector.
DICT_SYNTH = {
    34: (_s34, range(4, 10)), 35: (_s35, range(4, 8)), 36: (_s36, range(6, 14)), 41: (_s41, range(9, 18)),
    38: (_s38, range(11, 21)), 45: (_s45, range(5, 12)), 56: (_s56, range(15, 23)),
    63: (_s63, range(16, 21)), 64: (_s64, range(23, 29)), 65: (_s65, range(23, 25)),
}


SYNTH_FUNCS = {
    42: [
        (17, lambda V, W: V(3) + V(4) - V(8)),
        (18, lambda V, W: V(2) - V(1) + V(6)),
        (19, lambda V, W: (V(6) - V(1)).h() + V(7)),
        (20, lambda V, W: (V(6) - V(1)).h() + V(16)),
        (21, lambda V, W: (V(6) - V(1)).h() + (V(3) + V(4)).h() + 2 * (V(6) - V(1))),
        (22, lambda V, W: (W(21) + V(0)).h()),
        (23, lambda V, W: V(4) + V(5) - V(8)),
        (24, lambda V, W: (V(8) + V(11)).h()),
        (25, lambda V, W: V(4) + V(5) - V(3)),
        (26, lambda V, W: V(6) + V(9) - V(1)),
        (27, lambda V, W: 2 * V(6) - V(1)),
        (28, lambda V, W: V(2) - 2 * V(1) + 2 * V(6)),
        (29, lambda V, W: (V(6) - V(1)).h() + V(0)),
        (100, lambda V, W: ((V(13) + V(14)).h() + V(10)).h()),
    ],
    43: [
        (10, lambda V, W: V(3) + V(4) - V(6)),
        (11, lambda V, W: (V(3) + V(4)).h() + (V(5) - V(3)).q()),
        (12, lambda V, W: (W(11) + V(0)).h()),
        (13, lambda V, W: V(7) + (V(9) - V(7)).q() + (V(9) - V(7)).q().h()),
        (14, lambda V, W: V(4) + V(5) - V(3)),
        (15, lambda V, W: V(3) + V(4) - V(7)),
    ],
    44: [
        (5, lambda V, W: V(0) + (V(1) - V(0)).h()),
        (6, lambda V, W: V(0) + (V(1) - V(0)).h().h()),
    ],
    59: [
        (21, lambda V, W: V(6) - V(7) + V(10)),
        (22, lambda V, W: (V(3) + V(4)).h()),
        (23, lambda V, W: 2 * (V(3) + V(4)).h() - V(9)),
        (24, lambda V, W: 2 * (V(3) + V(4)).h() - V(8)),
        (25, lambda V, W: V(4) + V(5) - V(3)),
        (26, lambda V, W: ((V(4) - V(3) + 2 * V(5)).h() + V(5)).h()),
        (27, lambda V, W: ((V(4) - V(3) + 2 * V(5)).h() + V(4) - V(3) + V(5)).h()),
        (28, lambda V, W: (4 * (V(3) + V(4)).h() - 2 * V(3) + 2 * V(5) - V(8) - V(9)).h()),
        (29, lambda V, W: (V(1) + V(2)).h()),
        (30, lambda V, W: V(6) - V(7) + V(12)),
        (31, lambda V, W: V(6) - V(7) + V(14)),
        (32, lambda V, W: (V(6) - V(7)).h() + V(1) + V(6) - V(7)),
        (33, lambda V, W: (V(6) - V(7)).h() + V(2) + V(6) - V(7)),
        (34, lambda V, W: (V(6) - V(7)).h() + V(11) + 2 * (V(6) - V(7))),
        (35, lambda V, W: (V(6) - V(7)).h() + V(13) + 2 * (V(6) - V(7))),
        (36, lambda V, W: (V(6) - V(7)).h() + V(1) + 2 * (V(6) - V(7))),
        (37, lambda V, W: (V(6) - V(7)).h() + V(2) + 2 * (V(6) - V(7))),
        (38, lambda V, W: 2 * (V(3) + V(4)).h() - V(3) + V(5) - V(9)),
        (39, lambda V, W: 2 * (V(3) + V(4)).h() - V(3) + V(5) - V(8)),
        (110, lambda V, W: ((V(18) + V(19)).h() + V(16)).h()),
    ],
}


def _apply_synth(arr, rec):
    """Apply one synthesis record to ``arr`` ({slot: [x, y, z, state]}). All arithmetic is 16-bit words."""
    def get(s):
        e = arr.get(s)
        if e is None or e[3] not in (V_BLOCK, V_STRIP, V_SYNTH):
            return None
        return e[:3]

    def put(s, v):
        arr[s] = [v[0], v[1], v[2], V_SYNTH] if v is not None else \
            [0, 0, 0, V_UNRESOLVED]

    op = rec[0]
    if op == 52:
        _, sa, sb, st = rec
        a, bb = get(sa), get(sb)
        for t, off in ((3, 0), (4, 1), (5, 2)):
            src = get(st + off)
            if a is None or bb is None or src is None:
                put(st + t, None)
                continue
            d = _W(bb) - _W(a)
            if t != 3:
                d = d.h()
            put(st + t, (_W(src) + d).v)
        return
    if op == 53:
        _, sa, sb = rec
        a = get(sa)
        for j in range(3):
            bj = get(sb + j)
            for k in range(1, 6):
                src = get(sa + k)
                if a is None or bj is None or src is None:
                    put(sa + 30 + 5 * j + k - 1, None)
                else:
                    put(sa + 30 + 5 * j + k - 1, (_W(src) + _W(bj) - _W(a)).v)
        return
    _, b, s = rec

    def V(k):
        v = get(b + k)
        if v is None:
            raise _Miss
        return _W(v)

    if op in DICT_SYNTH:
        fn, keys = DICT_SYNTH[op]
        try:
            for off, w in fn(V, s).items():
                put(b + off, w.v)
        except _Miss:
            for off in keys:
                put(b + off, None)
        return
    done = {}

    def Wk(k):
        if k in done:
            return done[k]
        raise _Miss
    for off, fn in SYNTH_FUNCS[op]:
        try:
            w = fn(V, Wk)
            done[off] = w
            put(b + off, w.v)
        except _Miss:
            put(b + off, None)


# ---------------------------------------------------------------------------
# Streams (section 3)

class _Stream:
    def __init__(self, ctx: _Ctx, base: int):
        self.ctx = ctx
        self.base = base
        self.elems: List[Element] = []
        self.memo = {}
        self.queue = []
        self.proglists: List[int] = []     # local program indices
        self.progs = []                    # local: (address, entries, straight)
        self.prog_at = {}
        self.slotrefs = []                 # (slot, element address, element index)

    def program(self, addr):
        if addr in self.prog_at:
            return self.prog_at[addr]
        if not self.ctx.mem.has(addr, 2):
            self.ctx.err(f"program address 0x{addr:X} outside the image", addr)
        entries, straight = _decode_program(self.ctx, addr)
        idx = len(self.progs)
        self.prog_at[addr] = idx
        self.progs.append((addr, entries, straight))
        return idx

    def ref(self, addr, force_end=False):
        """Element index for the stream position ``addr`` (-1 for the 0xFFFF end word);
        new positions are queued, not decoded recursively."""
        if addr in self.memo:
            return self.memo[addr]
        if len(self.elems) >= MAX_STREAM_ELEMENTS:
            self.ctx.err("stream too large", addr)
        if self.ctx.mem.u16(addr) == 0xFFFF:
            return -1
        idx = len(self.elems)
        self.elems.append(Element(E_DRAW))
        self.memo[addr] = idx
        self.queue.append((addr, idx, force_end))
        return idx

    def element(self, addr, force_end=False):
        root = self.ref(addr, force_end)
        while self.queue:
            a, idx, fe = self.queue.pop()
            self._decode(a, idx, fe)
        return root

    def _decode(self, addr, idx, force_end):
        ctx, mem = self.ctx, self.ctx.mem
        e = self.elems[idx]
        w1 = mem.u16(addr)
        base = self.base

        def child(off, which=0):
            a = base + off
            if not mem.has(a, 2):
                ctx.err(f"child offset 0x{off:X} is outside the image", addr)
            if mem.u16(a) == 0xFFFF:
                e.flags |= EF_END1 if which else EF_END0
            return self.ref(a)

        def slot(word):
            s = _slot(ctx, None, word, addr, None)
            self.slotrefs.append((s, addr, idx))
            return s

        if not (w1 & 0x8000):
            w2 = mem.u16(addr + 2)
            if (w2 & 0xFC00) == 0xFC00:
                e.kind = E_JUMP
                e.child0 = child(mem.u16(addr + 4))
            elif w2 & 0x0C00:
                e.kind = E_AXIS
                e.axis = (w2 >> 10) & 3
                e.flags = (w2 >> 12) & 1
                e.s[0] = slot(w1)
                e.scale = ctx.scale_l
                e.child0 = child(mem.u16(addr + 4))
                e.child1 = child(mem.u16(addr + 6), 1)
            elif w2 & 0x2000:
                e.kind = E_PLANE
                e.s = [mem.s16(addr + 4 + 2 * k) for k in range(6)]
                e.scale = ctx.scale_l
                e.child0 = child(mem.u16(addr + 16))
                e.child1 = child(mem.u16(addr + 18), 1)
            elif w2 & 0x1000:
                ctx.err("node has bit 12 set without an axis field or bit 13 (undocumented plane source)", addr)
            else:
                e.kind = E_TRI
                e.scale = (w2 >> 7) & 7
                e.s[0] = slot(w1)
                e.s[1] = slot(mem.u16(addr + 4))
                e.s[2] = slot(mem.u16(addr + 6))
                e.child0 = child(mem.u16(addr + 8))
                e.child1 = child(mem.u16(addr + 10), 1)
            return
        # draw item
        e.kind = E_DRAW
        e.flags = (w1 >> 12) & 7
        p = addr + 2
        if w1 & 0x1000:
            ref = mem.u32(p)
            p += 4
        else:
            ref = base + (w1 & 0xFFF)
        first = len(self.proglists)
        if w1 & 0x2000:
            self.proglists.append(self.program(ref))
            ends = True
        else:
            q = ref
            while True:
                ent = mem.u16(q)
                q += 2
                if ent & 0x1000:
                    prog = mem.u32(q)
                    q += 4
                else:
                    prog = base + (ent & 0xFFF)
                self.proglists.append(self.program(prog))
                if ent & 0x8000:
                    break
            ends = bool(w1 & 0x4000)
        e.list_first = first
        e.list_count = len(self.proglists) - first
        if ends:
            e.child0 = -1
        else:
            if force_end:
                ctx.err("inline stream item does not end the stream", addr)
            if mem.has(p, 2) and mem.u16(p) == 0xFFFF:
                e.flags |= EF_END0
            e.child0 = self.ref(p)


# ---------------------------------------------------------------------------
# Detail list (section 2.1) and levels

def _walk_detail_list(mem: Mem, base: int, ctx_err):
    """Return [(threshold, shape, leaf, stream address, inline)] in near-to-far order."""
    specs = []
    pos = 0
    seen = set()
    while True:
        if pos in seen:
            ctx_err("detail list loops", base + pos)
        seen.add(pos)
        a = base + pos
        w = mem.u16(a)
        if w & 0x8000:
            specs.append((0, 2, w, a + 2, False))
            return specs
        if not (w & 0x4000):
            jump = mem.u16(a + 2)
            leaf = mem.u16(a + 4)
            specs.append((w & 0x3FFF, 0, leaf, a + 6, False))
            if jump & 0x8000:
                return specs
            pos = jump
        else:
            leaf = mem.u16(a + 2)
            specs.append((w & 0x3FFF, 1, leaf, a + 4, True))
            pos += 6
        if len(specs) > 32:
            ctx_err("detail list too long", a)


def _to_block_slot(ctx, off, flat, addr):
    stride = 4 if flat else 6
    if off % stride:
        ctx.err(f"extra-vertex offset {off} is not a multiple of {stride}", addr)
    return off // stride


def _finish_level(ctx: _Ctx, st: _Stream, block_vertex, nblock: int, flat: bool,
                  strip_slots, sec_slots=0):
    """Build the vertex array for a level and validate every slot reference.
    Returns (array dict, blocked item count)."""
    arr = {}
    for i in range(nblock):
        x, y, z = block_vertex(i)
        arr[i] = [x, y, z, V_BLOCK]
    # extras (static union), then strip slots, then synthesis along the straight path
    for addr, entries, straight in st.progs:
        for item, ops, slots, synth in entries:
            if item.extra_n > 0:
                first = _to_block_slot(ctx, item.extra_slot, flat, item.address)
                item.extra_slot = first
                for k in range(first, first + item.extra_n):
                    if k not in arr:
                        x, y, z = block_vertex(k)
                        arr[k] = [x, y, z, V_BLOCK]
    for slot, p in (strip_slots or {}).items():
        arr[slot] = [p[0], p[1], p[2], V_STRIP]
    # Slots of the second vertex section: placeholders the core overwrites at run time.
    for k in range(nblock, nblock + sec_slots):
        if k in arr:
            ctx.err(f"second-section slot {k} collides with another vertex source")
        arr[k] = [0, 0, 0, V_SECTION]
    # Synthesis. Items reached only through state variants are applied first so that
    # their slots exist; the straight path (state zero) is applied last and wins.
    for want_straight in (False, True):
        for addr, entries, straight in st.progs:
            straight_set = set(straight)
            for item, ops, slots, synth in entries:
                if synth is not None and (item.address in straight_set) == want_straight:
                    _apply_synth(arr, synth)
    # validation
    blocked = 0
    hint = ctx.block_hint
    for addr, entries, straight in st.progs:
        for item, ops, slots, synth in entries:
            if item.op in UNDOCUMENTED_BUILDERS and not hint:
                hint = (f"op {item.op} at 0x{item.address:X} is state dependent and its vertex "
                        f"output is not documented")
    blocked_elems = set()
    for s, eaddr, eidx in st.slotrefs:
        e = arr.get(s)
        if e is None or e[3] == V_UNDEFINED:
            if hint is None:
                ctx.err(f"bad vertex slot {s}: no block, strip or synthesised vertex", eaddr)
            what = f"vertex slot {s} is undefined ({hint})"
        elif e[3] == V_UNRESOLVED:
            what = f"vertex slot {s} is unresolved (note 02 section 5b/7)"
        else:
            continue
        if ctx.strict:
            ctx.err("BLOCKING: " + what, eaddr, blocked=True)
        blocked_elems.add(eidx)
    for addr, entries, straight in st.progs:
        for item, ops, slots, synth in entries:
            bad = None
            for s in slots:
                e = arr.get(s)
                if e is None or e[3] == V_UNDEFINED:
                    if hint is None:
                        ctx.err(f"bad vertex slot {s}: no block, strip or synthesised vertex",
                                item.address, item.op)
                    if ctx.strict:
                        ctx.err(f"BLOCKING: vertex slot {s} is undefined ({hint})",
                                item.address, item.op, blocked=True)
                    bad = s
                elif e[3] == V_UNRESOLVED:
                    bad = s
            if bad is not None:
                if ctx.strict:
                    ctx.err(f"BLOCKING: vertex slot {bad} is unresolved (note 02 section 5b/7)",
                            item.address, item.op, blocked=True)
                item.flags |= F_BLOCKED
            if item.flags & F_BLOCKED:
                blocked += 1
    for eidx in blocked_elems:
        st.elems[eidx].flags |= EF_BLOCKED
        blocked += 1
    return arr, blocked


def _merge(model: Model, level: Level, st: _Stream, arr, root, blocked, sec=()):
    """Append one level's local structures to the model."""
    n = (max(arr) + 1) if arr else 0
    level.vertex_first = len(model.vertices)
    level.vertex_count = n
    level.sec_first = len(model.secpool)
    level.sec_count = len(sec)
    model.secpool.extend(sec)
    for i in range(n):
        e = arr.get(i)
        model.vertices.append(Vertex(*e) if e else Vertex(0, 0, 0, V_UNDEFINED))
    eb = len(model.elements)
    pb = len(model.programs)
    lb = len(model.proglists)
    level.elem_first = eb
    level.elem_count = len(st.elems)
    for e in st.elems:
        e2 = Element(e.kind, e.flags, e.axis, e.scale,
                     e.child0 + eb if e.child0 >= 0 else -1,
                     e.child1 + eb if e.child1 >= 0 else -1, list(e.s),
                     e.list_first + lb, e.list_count)
        model.elements.append(e2)
    model.proglists.extend(p + pb for p in st.proglists)
    for addr, entries, straight in st.progs:
        model.programs.append(Program(len(model.items), len(entries), addr))
        for item, ops, slots, synth in entries:
            item.skip_first = len(model.skips)
            item.skip_count = len(item.skips)
            model.skips.extend(item.skips)
            item.op_first = len(model.operands)
            item.nops = len(ops)
            model.operands.extend(ops)
            model.items.append(item)
    level.root = root + eb if root >= 0 else -1
    model.levels.append(level)
    model.blocked += blocked


SEC_GROUP_LIMIT = 64


def _read_second_section(ctx: _Ctx, addr: int, b8: int):
    """Second vertex section of a flat block with flag bit 2 (note 03, "The second vertex
    section"). Words (big endian s16) from ``addr`` = block + 10 + 4 b8:
      group:  n (negative ends the section), n points (x, z), w (negative ends the section,
              otherwise two further points (x, z) follow), then the next group.
    The n points become two slots each (the point and the point plus the group's fixed
    offset), the two further points one slot each. Slots are numbered b8, b8+1, ... in
    stream order. Returns (normalised words, slot count): repeated {n, n pairs, m, m pairs}
    (m is 0 or 2) ended by n = -1; ([], 0) when the level has no section.

    Per the note: n is a do-while count (n = 0 yields one pair like n = 1; the pool then
    carries n = 1), the value of w is ignored (only its sign), and the section is skipped
    when b8 is 0 or 1 (the plain loop is a do-while, so only one plain slot exists)."""
    if b8 < 2:
        return [], 0
    mem = ctx.mem
    out, slots = [], 0
    for _g in range(SEC_GROUP_LIMIT):
        n = mem.s16(addr)
        addr += 2
        if n < 0:
            out.append(-1)
            return out, slots
        n = max(n, 1)
        pts = []
        for _i in range(n):
            pts.extend((mem.s16(addr), mem.s16(addr + 2)))
            addr += 4
        w = mem.s16(addr)
        addr += 2
        single = []
        if w >= 0:
            for _i in range(2):
                single.extend((mem.s16(addr), mem.s16(addr + 2)))
                addr += 4
        out.append(n)
        out.extend(pts)
        out.append(len(single) // 2)
        out.extend(single)
        slots += 2 * n + len(single) // 2
        if b8 + slots > SLOT_LIMIT:
            ctx.err("second vertex section runs past the slot limit", addr)
        if w < 0:
            out.append(-1)
            return out, slots
    ctx.err("second vertex section has no terminator", addr)


def _read_block(ctx: _Ctx, block_addr: int):
    mem = ctx.mem
    lv = Level()
    lv.part_id = mem.u16(block_addr)
    lv.tm = mem.u16(block_addr + 2)
    lv.tf = mem.u16(block_addr + 4)
    sc = mem.u8(block_addr + 6)
    lv.scale_h, lv.scale_l = sc >> 4, sc & 15
    lv.block_flags = mem.u8(block_addr + 7)
    lv.b8 = mem.u8(block_addr + 8)
    lv.b9 = mem.u8(block_addr + 9)
    if not lv.block_flags & 2:   # a non-flat block: the data triple after the b8 vertices of 6 bytes
        lv.data_triple = tuple(mem.s16(block_addr + 10 + 6 * lv.b8 + 2 * k) for k in range(3))
    return lv


def _decode_level(mem, base, model_id, strict, model, spec):
    """Decode one detail level (threshold, shape, leaf, stream address, inline) and append it; returns its kind (0/1) or None."""
    thr, shape, leaf, stream_addr, inline = spec
    if leaf == 0xFFFF:
        model.levels.append(Level(threshold=thr, shape=shape, present=0, leaf=leaf,
                                  vertex_first=len(model.vertices), elem_first=len(model.elements)))
        return None
    block_addr = base + (leaf & 0xFFF)
    ctx = _Ctx(mem, model_id, 0, block_addr, strict)
    ctx.model_base = base
    lv = _read_block(ctx, block_addr)
    lv.threshold, lv.shape, lv.leaf, lv.present = thr, shape, leaf, 1
    flat = bool(lv.block_flags & 2)
    ctx.kind = 1 if flat else 0
    ctx.scale_l = lv.scale_l
    st = _Stream(ctx, base)
    root = st.element(stream_addr, force_end=inline)
    data0 = block_addr + 10
    nblock = lv.b8      # only b8 and the extra ranges exist in the game's vertex array

    def block_vertex(i, flat=flat, data0=data0):
        if flat:
            return (mem.s16(data0 + 4 * i), 0, mem.s16(data0 + 4 * i + 2))
        return tuple(mem.s16(data0 + 6 * i + 2 * k) for k in range(3))

    strip = None
    if lv.block_flags & 8 and not flat:
        strip = _strip_slots(ctx, data0 + 6 * lv.b8, lv.b8)
    sec, sec_slots = [], 0
    if lv.block_flags & 4 and flat:
        sec, sec_slots = _read_second_section(ctx, data0 + 4 * lv.b8, lv.b8)
    arr, blocked = _finish_level(ctx, st, block_vertex, nblock, flat, strip, sec_slots)
    _merge(model, lv, st, arr, root, blocked, sec)
    return ctx.kind


def _decode_alternate(mem, rec, model_id, strict, model):
    """The cockpit-alternate detail list (note 02 section 2.5) as extra levels after the normal ones:
    sets model.alt_near and model.alt_cockpit (level indices)."""
    base, alt = rec.base, rec.alt_list
    if alt == base:
        return
    if alt == base - 2:                      # ids 0 and 1: one word 0xFFFF, then the normal list
        if mem.u16(alt) != 0xFFFF:
            _raise(model_id, "alternate list at base - 2 does not start with 0xFFFF", alt)
        model.alt_near = model.alt_cockpit = len(model.levels)
        model.levels.append(Level(threshold=0, shape=2, present=0, leaf=0xFFFF,
                                  vertex_first=len(model.vertices), elem_first=len(model.elements)))
        return
    if alt != base - 8:
        _raise(model_id, f"alternate list at 0x{alt:X} is neither the base, base - 2 nor base - 8", alt)
    w1, w2, w3, w4 = (mem.u16(alt + 2 * k) for k in range(4))
    if not (w1 & 0x8000) or not (w2 & 0x8000 and w2 & 0x2000 and not w2 & 0x1000) or not (w3 & 0x8000) or w4 != 0xFFFF:
        _raise(model_id, f"alternate list words {w1:04X} {w2:04X} {w3:04X} {w4:04X} do not have the documented shape", alt)
    model.alt_near = len(model.levels)
    _decode_level(mem, base, model_id, strict, model, (0, 2, w1, alt + 2, False))
    model.alt_cockpit = len(model.levels)
    _decode_level(mem, base, model_id, strict, model, (0, 2, w3, alt + 6, False))
    model.levels[model.alt_cockpit].present = 0


# The phase lists of the cone models (note 07 11.5): the rescue pod (models 2 and 13, handler 0x2699E) and the parachute (model 11, 0x26A3E) draw the detail list that their phase +0x4C selects.
# Four further lists, each a single leaf-only node followed by its own stream, in the order of the note's rows: phases 3 and 4, phases 1 and 2, phase 0, and phase below 0 (for the parachute
# the phase 0 list is 0x418DA, drawn when the low nibble of +0x7C is not 0, else the list of the negative phases). The model's own list (phases 5 and up) is the normal one. They are appended as
# levels after the cockpit-alternate ones, in this order, and phase_first is 1 + the index of the first.
PHASE_LISTS = {2: (0x41658, 0x41666, 0x41678, 0x41674), 13: (0x41658, 0x41666, 0x41678, 0x41674), 11: (0x418FE, 0x4190C, 0x418DA, 0x4191A)}
PHASE_BASE = {2: 0x41638, 13: 0x41638, 11: 0x418DE}   # the model's own list; a record that does not start there (a test image) has no phase lists


def _decode_phase_lists(mem, base, model_id, strict, model):
    model.phase_first = len(model.levels) + 1
    for addr in PHASE_LISTS[model_id]:
        specs = _walk_detail_list(mem, addr, lambda m, a=None: _raise(model_id, m, a))
        if len(specs) != 1 or not specs[0][2] & 0x8000:
            _raise(model_id, "phase list is not a single leaf-only node", addr)
        _decode_level(mem, base, model_id, strict, model, specs[0])   # the leaf offset is relative to the model's base, as for the normal list


def _decode_normal(mem: Mem, rec, model_id, strict) -> Model:
    base = rec.base
    ctx_err = lambda m, a=None: _raise(model_id, m, a)
    specs = _walk_detail_list(mem, base, ctx_err)
    model = Model(model_id, rec.handler, 0)
    kinds = set()
    for spec in specs:
        k = _decode_level(mem, base, model_id, strict, model, spec)
        if k is not None:
            kinds.add(k)
    model.kind = 1 if kinds == {1} else 0
    model.nnormal = len(model.levels)
    # the block flag byte the gun's hit test reads (0x2AEE6 to 0x2AEFE): the leaf named by the first word of the model's list when that word has bit 15 set, else by the word at +4; the byte is +7 of that block.
    # Read after the levels, so a truncated block is reported by its own decode; a bad read is an error of the model (Mem raises with the model and address), never "not a big hull".
    w0 = mem.u16(base)
    d7 = w0 if w0 & 0x8000 else mem.u16(base + 4)
    model.hull_flags = mem.u8(base + (d7 & 0x3FFF) + 7)
    _decode_alternate(mem, rec, model_id, strict, model)
    if PHASE_BASE.get(model_id) == base:
        _decode_phase_lists(mem, base, model_id, strict, model)
    return model


def _raise(model_id, msg, addr=None):
    w = "" if addr is None else f" at 0x{addr:X}"
    raise DecodeError(f"model {model_id}{w}: {msg}")


def _decode_typeb(mem: Mem, rec, model_id, strict) -> Model:
    model = Model(model_id, rec.handler, 2)
    addr = rec.base
    seen = set()
    entry_no = 0
    entries = []
    while addr not in seen:
        seen.add(addr)
        d = mem.u16(addr)
        if d & 0x8000:
            break
        nxt = mem.u32(addr + 2)
        vb = mem.u32(addr + 6)
        entries.append((d, addr, vb))
        if nxt == 0xFFFFFFFF:
            break
        addr = nxt
    for n, (d, eaddr, vb) in enumerate(entries):
        ctx = _Ctx(mem, model_id, 2, vb, strict)
        ctx.model_base = rec.base
        ctx.scale_l = 0
        count = mem.u16(vb)
        if count > SLOT_LIMIT:
            ctx.err(f"vertex block declares {count} vertices", vb)
        lv = Level(threshold=d, shape=3, present=1, leaf=0)

        def block_vertex(i, vb=vb):
            return (mem.s16(vb + 6 + 4 * i), 0, mem.s16(vb + 8 + 4 * i))

        st = _Stream(ctx, 0)
        prog = st.program(eaddr + 10)
        e = Element(E_DRAW, list_first=0, list_count=1)
        st.elems.append(e)
        st.proglists.append(prog)
        arr, blocked = _finish_level(ctx, st, block_vertex, count, True, None)
        _merge(model, lv, st, arr, 0, blocked)
    model.nnormal = len(model.levels)
    return model


def decode_model(data: bytes, record_offset: int, model_id: int, image_base: int = IMAGE_BASE,
                 strict: bool = True) -> Model:
    """Decode the model of the record at byte ``record_offset`` of ``data``.

    ``strict`` raises DecodeError on documented BLOCKING items; with strict False the
    affected items are flagged F_BLOCKED and decoding goes on. Unknown operations,
    bad vertex slots and truncated blocks always raise."""
    mem = Mem(data, image_base, f"model {model_id}")
    rec = decode_record(data, record_offset, model_id)
    if rec.base == 0:
        raise DecodeError(f"model {model_id}: empty record (run-time slot, zero in the file)")
    if rec.handler in TYPE_B_HANDLERS:
        return _decode_typeb(mem, rec, model_id, strict)
    return _decode_normal(mem, rec, model_id, strict)


# ---------------------------------------------------------------------------
# IMDL file format, version 5 (little endian, counts first)

_HDR = struct.Struct("<4sHHHHI7IIIHHHH")
_LEVEL = struct.Struct("<HHBBHHHBBBBBBHiIIIIIIhhh")
_VERT = struct.Struct("<iiiI")
_ELEM = struct.Struct("<BBBBii6hII")
_PROG = struct.Struct("<III")
_ITEM = struct.Struct("<HHHHhHHHHHHHIIII")
assert (_HDR.size, _LEVEL.size, _VERT.size, _ELEM.size, _PROG.size, _ITEM.size) == (60, 54, 16, 32, 12, 40)


def write_model(m: Model) -> bytes:
    out = [_HDR.pack(IMDL_MAGIC, IMDL_VERSION, m.id, m.kind, m.blocked, m.handler,
                     len(m.levels), len(m.vertices), len(m.elements), len(m.proglists),
                     len(m.programs), len(m.items), len(m.operands), len(m.skips), len(m.secpool),
                     len(m.levels) if m.nnormal < 0 else m.nnormal, m.alt_near, m.alt_cockpit, m.phase_first)]
    for l in m.levels:
        out.append(_LEVEL.pack(l.threshold, l.leaf, l.shape, l.present, l.part_id, l.tm, l.tf,
                               l.scale_h, l.scale_l, l.block_flags, l.b8, l.b9, m.hull_flags, 0, l.root,
                               l.vertex_first, l.vertex_count, l.elem_first, l.elem_count,
                               l.sec_first, l.sec_count, *l.data_triple))
    for v in m.vertices:
        out.append(_VERT.pack(v.x, v.y, v.z, v.state))
    for e in m.elements:
        out.append(_ELEM.pack(e.kind, e.flags, e.axis, e.scale, e.child0, e.child1, *e.s,
                              e.list_first, e.list_count))
    out.append(struct.pack(f"<{len(m.proglists)}I", *m.proglists))
    for p in m.programs:
        out.append(_PROG.pack(p.item_first, p.item_count, p.address))
    for i in m.items:
        out.append(_ITEM.pack(i.op, i.flags, i.g1, i.g2, i.extra_n, i.extra_slot, i.pen, i.w,
                              i.wback, i.prim, i.nops, i.length, i.op_first, i.address,
                              i.skip_first, i.skip_count))
    out.append(struct.pack(f"<{len(m.operands)}i", *m.operands))
    out.append(struct.pack(f"<{len(m.skips)}i", *m.skips))
    out.append(struct.pack(f"<{len(m.secpool)}i", *m.secpool))
    return b"".join(out)


def read_model(b: bytes) -> Model:
    if len(b) < _HDR.size:
        raise DecodeError("IMDL file: truncated header")
    magic, ver, mid, kind, blocked, handler, nl, nv, ne, npl, npg, ni, nops, nsk, nsec, nnormal, alt_near, alt_cockpit, phase_first = _HDR.unpack_from(b, 0)
    if magic != IMDL_MAGIC or ver != IMDL_VERSION:
        raise DecodeError(f"IMDL file: bad magic or version ({magic!r}, {ver})")
    need = (_HDR.size + nl * _LEVEL.size + nv * _VERT.size + ne * _ELEM.size + 4 * npl +
            npg * _PROG.size + ni * _ITEM.size + 4 * nops + 4 * nsk + 4 * nsec)
    if len(b) != need:
        raise DecodeError(f"IMDL file: size {len(b)} does not match the counts (expected {need})")
    m = Model(mid, handler, kind, blocked=blocked, nnormal=nnormal, alt_near=alt_near, alt_cockpit=alt_cockpit, phase_first=phase_first)
    if nl:
        m.hull_flags = _LEVEL.unpack_from(b, _HDR.size)[12]
    p = _HDR.size
    for _i in range(nl):
        f = _LEVEL.unpack_from(b, p)
        p += _LEVEL.size
        m.levels.append(Level(f[0], f[2], f[3], f[1], f[4], f[5], f[6], f[7], f[8], f[9], f[10],
                              f[11], f[14], f[15], f[16], f[17], f[18], f[19], f[20], (f[21], f[22], f[23])))
    for _i in range(nv):
        m.vertices.append(Vertex(*_VERT.unpack_from(b, p)))
        p += _VERT.size
    for _i in range(ne):
        f = _ELEM.unpack_from(b, p)
        p += _ELEM.size
        m.elements.append(Element(f[0], f[1], f[2], f[3], f[4], f[5], list(f[6:12]), f[12], f[13]))
    m.proglists = list(struct.unpack_from(f"<{npl}I", b, p))
    p += 4 * npl
    for _i in range(npg):
        m.programs.append(Program(*_PROG.unpack_from(b, p)))
        p += _PROG.size
    for _i in range(ni):
        f = _ITEM.unpack_from(b, p)
        p += _ITEM.size
        m.items.append(Item(op=f[0], flags=f[1], g1=f[2], g2=f[3], extra_n=f[4], extra_slot=f[5],
                            pen=f[6], w=f[7], wback=f[8], prim=f[9], nops=f[10], length=f[11],
                            op_first=f[12], address=f[13], skip_first=f[14], skip_count=f[15]))
    m.operands = list(struct.unpack_from(f"<{nops}i", b, p))
    p += 4 * nops
    m.skips = list(struct.unpack_from(f"<{nsk}i", b, p))
    p += 4 * nsk
    m.secpool = list(struct.unpack_from(f"<{nsec}i", b, p))
    for it in m.items:
        it.skips = m.skips[it.skip_first:it.skip_first + it.skip_count]
    return m
