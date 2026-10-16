"""Instrumentation of the drawer's primitive layer in the rig (docs/re/04a-display-lists.md section 8c, Verified by).

Writes 68000 stubs into spare rig RAM (0x800000 and up) and redirects (a) the 78 entries of the operation table at 0x23408 to a
wrapper that calls the real handler and logs its result word and condition codes, and (b) the output routines (polygon filler,
line routine, the three point sprites) to stubs that log their arguments and return at once.  Nothing in the game image on disk
is touched; the rig's RAM copy is patched.  Log record: eight words.
    kind 3  handler call:  3, op, d0, sr, item id (V:4AA6C), x, y, z (V:4ABE2, 4ABE4, 4ABE6)
    kind 4  output routine: 4, routine number (PRIMS below), d0, d1, item id, x, y, z
    kind 7  pre-call record (install(pre=True)): 7, A2 (two words, the address of the arguments), op, item id, x, y, z
    kind 6  stream-end marker (EXITS below): 6, exit number, item id, x, y, z
    kind 5  call of a primitive entry from a handler (CALLS below): 5, call number, d0, sr, item id, x, y, z
The log pointer lives at LOGPTR; read_log() returns the records and resets it.
"""
import struct

STUBS = 0x800000
LOGPTR = 0x8FF000
LOGBASE = 0x900000
TABLE = 0x23408
PRIMS = {1: 0x34A28, 2: 0x3455E, 3: 0x340D4, 4: 0x340EA, 5: 0x3414E}   # filler, line, three point sprites
NOPS = 78


def w(*v):
    return b"".join(struct.pack(">H", x & 0xFFFF) for x in v)


def l(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


def append_header(kind, item, rest):
    """code that appends a record; a0 and sr are saved and restored by the caller of this fragment"""
    c = b""
    c += w(0x2079) + l(LOGPTR)                    # movea.l LOGPTR.l,a0
    c += w(0x30BC, kind)                          # move.w #kind,(a0)
    for disp, val in rest:                        # (displacement, source)
        if isinstance(val, int):
            c += w(0x317C, val, disp)             # move.w #imm,disp(a0)
        elif val[0] == "mem":
            c += w(0x3179) + l(val[1]) + w(disp)  # move.w abs.l,disp(a0)
        elif val[0] == "reg":
            c += w(0x3140 | val[1], disp)         # move.w dN,disp(a0)
        elif val[0] == "a2":
            c += w(0x214A, disp)                  # move.l a2,disp(a0)  (two words)
        elif val[0] == "sr":
            c += w(0x316F, 4, disp)               # move.w 4(a7),disp(a0)  (sr saved under the saved a0)
    c += w(0x41E8, 16)                            # lea 16(a0),a0
    c += w(0x23C8) + l(LOGPTR)                    # move.l a0,LOGPTR.l
    return c


def item_fields(base_disp=8):
    return [(base_disp + 0, ("mem", 0x4AA6C)), (base_disp + 2, ("mem", 0x4ABE2)), (base_disp + 4, ("mem", 0x4ABE4)),
            (base_disp + 6, ("mem", 0x4ABE6))]


def pre_stub(op):
    """pre-call record: 7, A2 (two words), op, item id, x, y, z -- the address of the operation's arguments"""
    c = w(0x40E7, 0x2F08)
    c += append_header(7, None, [(2, ("a2",)), (6, op), (8, ("mem", 0x4AA6C)), (10, ("mem", 0x4ABE2)), (12, ("mem", 0x4ABE4)), (14, ("mem", 0x4ABE6))])
    c += w(0x205F, 0x44DF)
    return c


def op_stub(op, orig, pre=False):
    c = (pre_stub(op) if pre else b"") + w(0x4EB9) + l(orig)      # [pre-call record], jsr orig.l
    c += w(0x40E7, 0x2F08)                        # move sr,-(a7); move.l a0,-(a7)
    c += append_header(3, None, [(2, op), (4, ("reg", 0)), (6, ("sr",))] + item_fields())
    c += w(0x205F, 0x44DF, 0x4E75)                # movea.l (a7)+,a0; move.w (a7)+,ccr; rts
    return c


def prim_stub(pid):
    c = w(0x40E7, 0x2F08)
    c += append_header(4, None, [(2, pid), (4, ("reg", 0)), (6, ("reg", 1))] + item_fields())
    c += w(0x205F, 0x44DF, 0x4E75)
    return c


CALLS = {0x28B6A: 10, 0x28B6C: 11, 0x33936: 12, 0x3385C: 13, 0x33780: 14, 0x33788: 15, 0x33790: 16}   # primitive entries called by the handlers
REGION = (0x22C00, 0x26000)


def call_stub(target, cid):
    c = w(0x4EB9) + l(target)                      # jsr target.l
    c += w(0x40E7, 0x2F08)
    c += append_header(5, None, [(2, cid), (4, ("reg", 0)), (6, ("sr",))] + item_fields())
    c += w(0x205F, 0x44DF, 0x4E75)
    return c


def op_table(rig):
    """the original handler addresses (read before install() patches the table)"""
    return [rig.u32(TABLE + 4 * i) for i in range(NOPS)]


EXITS = {1: "tree walk of the plain drawer reached (0x225FA)", 2: "stream ended by 0xFFFF in the plain drawer (0x22692, returns 0)",
         3: "type B drawer aborted by a negative handler word (0x22E9E, returns 0)", 4: "tree walk of the type B drawer reached (0x22E56)"}


def marker_stub(mid, tail):
    c = w(0x40E7, 0x2F08)
    c += append_header(6, None, [(2, mid)] + item_fields(4))
    c += w(0x205F, 0x44DF) + tail
    return c


def install_exits(rig, addr):
    """log the stream-end exits of the drawers (kind 6 records: 6, exit number, item id, x, y, z)"""
    specs = [
        (1, 0x225FA, w(0x42B9) + l(0x4A7A2) + w(0x4EF9) + l(0x22600)),                     # clr.l V:4A7A2 (replaced), back to 0x22600
        (2, 0x22692, w(0x4E5E, 0x7000, 0x4E75)),                                           # unlk a6; moveq #0,d0; rts (replaced)
        (3, 0x22E9E, w(0x7000, 0x4E5E, 0x4E75)),                                           # moveq #0,d0; unlk a6; rts (replaced)
        (4, 0x22E56, w(0x2479) + l(0x4AAEE) + w(0x4EF9) + l(0x22E5C)),                     # movea.l V:4AAEE.l,a2 (replaced), back to 0x22E5C
    ]
    for mid, at, tail in specs:
        code = marker_stub(mid, tail)
        rig.poke(addr, code); rig.poke(at, w(0x4EF9) + l(addr)); addr += (len(code) + 3) & ~3
    return addr


def install(rig, ops=True, exits=False, pre=False):
    """patch the rig's RAM; call after rig.reset() and before running walkers.  ops=False leaves the operation table alone
    (direct handler calls: the unit checks of prim_ops.py)"""
    addr = STUBS
    orig = op_table(rig)
    for i in range(NOPS if ops else 0):
        code = op_stub(i, orig[i], pre)
        rig.poke(addr, code); rig.w32(TABLE + 4 * i, addr); addr += (len(code) + 3) & ~3
    for pid, a in PRIMS.items():
        code = prim_stub(pid)
        rig.poke(addr, code); rig.poke(a, w(0x4EF9) + l(addr)); addr += (len(code) + 3) & ~3   # jmp stub over the routine's entry
    wrap = {}
    for tgt, cid in CALLS.items():
        code = call_stub(tgt, cid)
        rig.poke(addr, code); wrap[tgt] = addr; addr += (len(code) + 3) & ~3
    img = rig.peek(REGION[0], REGION[1] - REGION[0])
    nsite = 0
    for tgt, wa in wrap.items():
        pat = w(0x4EB9) + l(tgt)
        i = img.find(pat)
        while i >= 0:
            if i % 2 == 0:
                rig.poke(REGION[0] + i + 2, l(wa)); nsite += 1
            i = img.find(pat, i + 2)
    if exits:
        install_exits(rig, addr)
    clear_log(rig)
    return nsite


def clear_log(rig):
    rig.w32(LOGPTR, LOGBASE)


def read_log(rig):
    end = rig.u32(LOGPTR)
    n = (end - LOGBASE) // 16
    raw = rig.peek(LOGBASE, 16 * n)
    out = [struct.unpack(">8H", raw[16 * i:16 * i + 16]) for i in range(n)]
    clear_log(rig)
    return out


def s16(v):
    return v - 0x10000 if v & 0x8000 else v
