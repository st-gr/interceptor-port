"""Which drawing operations write vertex positions 4 and up of the polygon buffer V:5103C (vertices from 0x51040, 6 bytes each; docs/re/03-3d-pipeline.md "Op 9 reads past its quad").

    python3 polybuf_writers_check.py [SEED [N]]

Each operation handler is called directly (prim_ops.py's random operands and vertex arrays) with a sentinel word in the first 12 vertex positions of the buffer; the highest position that no
longer holds the sentinel afterwards is recorded per operation.  Prints, per operation, the highest written position (0 based) over the calls and the count word at that call."""
import collections, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prim_ops as PO

SENT = 0x7777


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    N = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    u = PO.Unit(); rng = random.Random(seed)
    top = collections.defaultdict(int); cnt = collections.defaultdict(int)
    orig_call = u.call

    def call(op, words, extra=None):
        u.r.poke(0x51040, struct.pack(">h", SENT) * 3 * 12 + bytes(0))
        res = orig_call(op, words, extra)
        buf = struct.unpack(">36h", u.r.peek(0x51040, 72))
        hi = -1
        for k in range(12):
            if any(v != SENT for v in buf[3 * k:3 * k + 3]): hi = k
        cur[0] = max(cur[0], hi); cur[1] = max(cur[1], u.r.u16(0x5103E) if hi >= 0 else 0)
        return res
    u.call = call
    for op in PO.OPS:
        for _ in range(N):
            cur = [-1, 0]
            try: PO.run_op(u, op, rng)
            except Exception: pass
            top[op] = max(top[op], cur[0]); cnt[op] = max(cnt[op], cur[1])
    print("operations that write position 4 or higher (highest 0-based position, largest count word):",
          {op: (top[op], cnt[op]) for op in sorted(top) if top[op] >= 4})
    print("operations that write only positions 0 to 3:", [op for op in sorted(top) if 0 <= top[op] < 4])
    print("operations that write none:", [op for op in sorted(top) if top[op] < 0])




def early_outs(seed=1, n=400):
    """Do ops 3, 69 and 55 write the polygon buffer before their all-behind and face early-outs?  Classifies random calls by (clipper reached, vertices written, count word written),
    with a sentinel in the shift word, the count word and the first 8 vertex positions.  Run: python3 polybuf_writers_check.py early [SEED [N]]"""
    import ctypes
    u = PO.Unit(); rng = random.Random(seed); V = PO.V
    arr = (ctypes.c_uint32 * 1)(0x28B6A)
    LIST = 0x7D0000
    out = collections.defaultdict(collections.Counter)
    for op in (3, 69, 55):
        for _ in range(n):
            u.set_state(rng); vs = u.vertices(rng); pen = PO.R(rng, 0, 15)
            k = PO.R(rng, 5, 6) if op != 69 else 3
            idx = [PO.R(rng, 0, 20) for _ in range(k)]
            W = rng.choice((0x8000 | pen, pen))
            if op == 55:
                ws = [V(x) for x in idx]; ws[-1] |= 0x8000; ws.append(W)
                u.r.poke(LIST, b"".join(struct.pack(">H", w & 0xFFFF) for w in ws + [0, 0, 0]))
                words = [LIST >> 16, LIST & 0xFFFF, 0, 0xFFFF]
            else:
                words = [V(x) for x in idx]
                if op == 3: words[-1] |= 0x8000
                words.append(W)
            u.r.poke(0x5103C, struct.pack(">h", SENT) * 3 * 9)
            u.r.L.rig_watch_set(arr, 1); u.r.L.rig_trace_clear()
            d0, lt, pr, cl, a2 = u.call(op, words)
            called = u.r.L.rig_trace_get(None, 0) > 0
            u.r.L.rig_watch_set(arr, 0)
            buf = struct.unpack(">27h", u.r.peek(0x5103C, 54))
            shift_w = buf[0] != SENT; count_w = buf[1] != SENT
            verts = sum(1 for j in range(7) if any(v != SENT for v in buf[2 + 3 * j:5 + 3 * j]))
            zs = [vs[x][2] for x in idx]
            behind = all(z < 0 for z in zs) if op != 69 else None
            out[op][("clipper" if called else "no clipper", "vertices written %d+" % (3 if verts >= 3 else verts), "count written" if count_w else "count not written", "shift written" if shift_w else "shift not written", "all behind" if behind else "")] += 1
    for op in out:
        print("op", op)
        for k, v in sorted(out[op].items()): print("   ", v, k)


if len(sys.argv) > 1 and sys.argv[1] == "early":
    early_outs(*(int(a) for a in sys.argv[2:4]))
    sys.exit(0)

if __name__ == "__main__":
    main()
