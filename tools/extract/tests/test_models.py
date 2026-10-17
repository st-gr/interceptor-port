import struct

import pytest

from extract import models as M
from extract.models import (E_AXIS, E_DRAW, E_JUMP, E_PLANE, E_TRI, F_BLOCKED, F_LAST,
                            F_PLANE, F_TWO_SIDED, V_BLOCK, V_SECTION, V_STRIP, V_SYNTH, V_UNRESOLVED,
                            decode_model, read_model, write_model)
from extract.tables import BlockedError, DecodeError
from tests.synth import BASE, Img

REAL_BIN = __import__("pathlib").Path(__file__).resolve().parents[3] / "extracted" / "interceptor.bin"
REC = BASE            # record table at the start; id n at BASE + 20 n
MB = BASE + 0x200     # model base


def put_record(img, mid, base, handler=0x21C44):
    img.l(REC + 20 * mid, handler, base, base, base, 0)


def block(img, addr, verts, b8, flags=1, scale=0x23, flat=False):
    img.w(addr, 0, 0x100, 0x200)
    img.bytes_(addr + 6, bytes([scale, flags, b8, b8]))
    a = addr + 10
    for v in verts:
        a = img.w(a, *([v[0], v[2]] if flat else v))
    return a


def two_level_model(img, mid=5):
    """List: near node (shape A, jump to a leaf-only node); a two-node BSP for the near
    level (three-vertex node, single-program item, list item); one program with a gated
    item that loads one extra vertex."""
    put_record(img, mid, MB)
    img.w(MB, 0x0100, 0x0040, 0x4050)                       # threshold, jump, leaf (no clip)
    img.w(MB + 0x06, 0x0000, 0x0100, 0x0006, 0x000C, 0x0012, 0x0014)   # tri node
    img.w(MB + 0x12, 0xA020)                                # single program at +0x20
    img.w(MB + 0x14, 0xC016)                                # list at +0x16, bit 14: ends stream
    img.w(MB + 0x16, 0x8030)                                # last list entry: program +0x30
    img.w(MB + 0x20, 0xC00C, 0x0000, 0x0006, 0x800C, 0x8003)          # op 3, two sided, colour 3
    img.w(MB + 0x30, 0x0200, 0xFFFF, 0x0001, 0x000C, 0x4008, 0x0000, 0x000C, 0x0005)  # gated line
    img.w(MB + 0x40, 0x8070)                                # leaf only (block +0x70)
    img.w(MB + 0x42, 0xA044)                                # single program +0x44
    img.w(MB + 0x44, 0xC008, 0x0000, 0x0006, 0x0004)       # line, colour 4
    block(img, MB + 0x50, [(1, 2, 3), (4, 5, 6), (7, 8, 9)], 2)
    block(img, MB + 0x70, [(10, 11, 12), (13, 14, 15)], 2)
    return decode_model(img.data, 20 * mid, mid, BASE)


def test_two_level_model_levels_vertices_and_gates():
    img = Img()
    m = two_level_model(img)
    assert [l.threshold for l in m.levels] == [0x100, 0]
    assert [l.shape for l in m.levels] == [0, 2]
    assert m.levels[0].leaf == 0x4050 and m.levels[0].b8 == 2
    assert (m.levels[0].scale_h, m.levels[0].scale_l) == (2, 3)
    v0 = m.vertices[m.levels[0].vertex_first:][:m.levels[0].vertex_count]
    # b8 = 2 plus the extra vertex loaded by the gated item: slot 2 from block vertex 2
    assert [(v.x, v.y, v.z, v.state) for v in v0] == [(1, 2, 3, V_BLOCK), (4, 5, 6, V_BLOCK), (7, 8, 9, V_BLOCK)]
    assert m.levels[1].vertex_count == 2
    gated = [i for i in m.items if i.flags & M.F_GATED]
    assert len(gated) == 1 and gated[0].g1 == 0x200 and gated[0].g2 == 0xFFFF
    assert (gated[0].extra_n, gated[0].extra_slot) == (1, 2)       # byte 12 / 6 = slot 2
    poly = [i for i in m.items if i.op == 3][0]
    assert poly.flags & F_TWO_SIDED and poly.flags & F_LAST and poly.pen == 3
    assert m.operands[poly.op_first:poly.op_first + poly.nops] == [3, 0, 1, 2]


def test_the_data_triple_after_the_block_vertices_is_exported_and_round_trips():
    """Note 07 8.10 rule 5: the draw's vertex loop rotates the three words that follow the b8 vertices into slot b8 (b8 = b9); the level keeps them."""
    img = Img()
    m = two_level_model(img)          # the first block has b8 = 2 and a third vertex-sized triple (7, 8, 9) after its two vertices
    assert m.levels[0].data_triple == (7, 8, 9)
    assert m.levels[1].data_triple == (0, 0, 0)
    assert read_model(write_model(m)).levels[0].data_triple == (7, 8, 9)


def test_bsp_nodes_children_and_draw_items():
    img = Img()
    m = two_level_model(img)
    l0 = m.levels[0]
    root = m.elements[l0.root]
    assert root.kind == E_TRI and root.s[:3] == [0, 1, 2] and root.scale == 2
    c0, c1 = m.elements[root.child0], m.elements[root.child1]
    assert c0.kind == E_DRAW and c0.list_count == 1 and c0.child0 == -1
    assert c1.kind == E_DRAW and c1.child0 == -1          # bit 14 ended the stream
    progs = [m.programs[m.proglists[c.list_first]] for c in (c0, c1)]
    assert [p.item_count for p in progs] == [1, 1]
    l1 = m.levels[1]
    assert m.elements[l1.root].kind == E_DRAW


def test_round_trip_and_reader_errors():
    img = Img()
    m = two_level_model(img)
    b = write_model(m)
    assert read_model(b) == m
    assert b[:4] == b"IMDL"
    with pytest.raises(DecodeError, match="bad magic"):
        read_model(b"XXXX" + b[4:])
    with pytest.raises(DecodeError, match="does not match the counts"):
        read_model(b + b"\0")
    with pytest.raises(DecodeError, match="truncated"):
        read_model(b[:10])


def test_axis_plane_and_jump_nodes():
    img = Img()
    put_record(img, 6, MB)
    img.w(MB, 0x8040)                                  # leaf only, block +0x40
    # jump node: w1 unused, w2 0xFC00, target +0x10
    img.w(MB + 2, 0x0000, 0xFC00, 0x0010)
    # axis node at +0x10: vertex 1 (byte 6), y axis (bit 10), swap (bit 12)
    img.w(MB + 0x10, 0x0006, 0x1400, 0x001A, 0x002E)
    # point-and-normal node at +0x1A? children below: use +0x1A for plane node
    img.w(MB + 0x1A, 0x0000, 0x2000, 1, 2, 3, 4, 5, -6 & 0xFFFF, 0x002E, 0x0030)
    img.w(MB + 0x2E, 0xFFFF)
    img.w(MB + 0x30, 0xFFFF)
    block(img, MB + 0x40, [(0, 0, 0), (1, 1, 1)], 2)
    m = decode_model(img.data, 6 * 20, 6, BASE)
    root = m.elements[m.levels[0].root]
    assert root.kind == E_JUMP
    ax = m.elements[root.child0]
    assert ax.kind == E_AXIS and ax.axis == 1 and ax.flags & 1 and ax.s[0] == 1
    plane = m.elements[ax.child0]
    assert plane.kind == E_PLANE and plane.s == [1, 2, 3, 4, 5, -6]
    assert plane.child0 == -1 and plane.child1 == -1
    assert plane.flags & (M.EF_END0 | M.EF_END1) == (M.EF_END0 | M.EF_END1)   # both children are 0xFFFF stream ends (rule 9)
    assert ax.flags == 1 | M.EF_END1   # swap bit; child1 is a 0xFFFF end


def test_node_with_bit_12_but_no_axis_or_plane_is_refused():
    img = Img()
    put_record(img, 6, MB)
    img.w(MB, 0x8040)
    img.w(MB + 2, 0x0000, 0x1000, 0, 0, 0, 0)
    block(img, MB + 0x40, [(0, 0, 0)], 1)
    with pytest.raises(DecodeError, match=r"model 6 at 0x1202: node has bit 12 set"):
        decode_model(img.data, 6 * 20, 6, BASE)


def simple(img, mid, prog_words, verts, b8, flags=1, scale=0x23, extra=(), block_at=0x40, flat=False):
    """A model with one level whose single program is ``prog_words``."""
    put_record(img, mid, MB)
    img.w(MB, 0x8000 | block_at)
    img.w(MB + 2, 0xA010)                  # single program at +0x10
    img.w(MB + 0x10, *prog_words)
    end = block(img, MB + block_at, verts, b8, flags, scale, flat)
    return end


def test_unknown_operation_names_model_op_and_address():
    img = Img()
    simple(img, 9, [0xC014], [(0, 0, 0)], 1)           # 0x14 / 4 = op 5, unused by the game
    with pytest.raises(ValueError, match=r"model 9: op 5 at 0x1210: unknown operation 5"):
        decode_model(img.data, 9 * 20, 9, BASE)


def test_operation_word_not_multiple_of_four():
    img = Img()
    simple(img, 9, [0xC002], [(0, 0, 0)], 1)
    with pytest.raises(ValueError, match=r"model 9 at 0x1210: operation word 0xC002"):
        decode_model(img.data, 9 * 20, 9, BASE)


def test_bad_vertex_slot_is_refused_with_slot_and_address():
    img = Img()
    # line from slot 0 to slot 9; the block has two vertices only
    simple(img, 9, [0xC008, 0x0000, 0x0036, 0x0003], [(0, 0, 0), (1, 1, 1)], 2)
    with pytest.raises(ValueError, match=r"model 9: op 2 at 0x1210: bad vertex slot 9"):
        decode_model(img.data, 9 * 20, 9, BASE)


def test_vertex_operand_not_multiple_of_six():
    img = Img()
    simple(img, 9, [0xC008, 0x0000, 0x0007, 0x0003], [(0, 0, 0), (1, 1, 1)], 2)
    with pytest.raises(ValueError, match="not a multiple of 6"):
        decode_model(img.data, 9 * 20, 9, BASE)


def test_truncated_block_names_model_and_address():
    img = Img(0x245)           # image ends inside the block header
    put_record(img, 9, MB)
    img.w(MB, 0x8040)
    img.w(MB + 2, 0xA010)
    img.w(MB + 0x10, 0xC000)
    with pytest.raises(ValueError, match=r"model 9 at 0x1244: read of 2 bytes outside the image"):
        decode_model(img.data, 9 * 20, 9, BASE)


def test_empty_record_is_refused_not_decoded():
    img = Img()
    with pytest.raises(ValueError, match="model 20: empty record"):
        decode_model(img.data, 0, 20, BASE)


def test_ops_7_and_8_keep_raw_words_and_name_their_slots_without_blocking():
    img = Img()
    # op 7: pen, a, b|flip, c, d (c = slot 4, d = base slot 1): slots 4 and 1..3 must exist
    simple(img, 9, [0xC01C, 1, 6, 0x8000 | 12, 24, 6], [(0, 0, 0)] * 6, 6)
    m = decode_model(img.data, 9 * 20, 9, BASE)
    it = m.items[0]
    assert m.blocked == 0 and not it.flags & F_BLOCKED and it.prim == M.P_TRIANGLE and it.pen == 1
    assert m.operands == [1, 6, 0x800C, 24, 6]
    # op 8: pen, c, a, b|flip, f, d; slots c, c+1, f, base..base+3 (base 3 + 3 = 6 is missing)
    img = Img()
    simple(img, 9, [0xC020, 1, 0, 6, 12, 30, 18], [(0, 0, 0)] * 6, 6)
    with pytest.raises(DecodeError, match="bad vertex slot 6"):
        decode_model(img.data, 9 * 20, 9, BASE)
    img = Img()
    simple(img, 9, [0xC020, 1, 0, 6, 12, 30, 12], [(0, 0, 0)] * 6, 6)
    m = decode_model(img.data, 9 * 20, 9, BASE)
    assert m.blocked == 0 and m.operands == [1, 0, 6, 12, 30, 12]


def test_op_69_keeps_the_full_w_word_and_the_back_colour():
    img = Img()
    prog = [0x8114, 0, 6, 12, 0x410C, 3,        # op 69, W bit 14 set: a back colour word follows
            0xC114, 0, 6, 12, 0x8005]           # two-sided
    simple(img, 9, prog, [(0, 0, 0), (1, 0, 0), (0, 1, 0)], 3)
    m = decode_model(img.data, 9 * 20, 9, BASE)
    a, b = m.items
    assert a.w == 0x410C and a.pen == 0xC and a.flags & M.F_BACK_COLOUR and a.wback == 3 and a.length == 12
    assert b.flags & F_TWO_SIDED and b.w == 0x8005 and b.pen == 5
    assert m.operands[a.op_first:a.op_first + a.nops] == [0, 1, 2]


@pytest.mark.skipif(not REAL_BIN.exists(), reason="extracted/interceptor.bin absent")
def test_real_image_op_75_and_op_69_shapes_and_bridges():
    from extract.tables import RECORD_SIZE, RECORD_TABLE, IMAGE_BASE
    data = REAL_BIN.read_bytes()

    def model(mid, strict=True):
        return decode_model(data, RECORD_TABLE - IMAGE_BASE + RECORD_SIZE * mid, mid, strict=strict)
    for mid in (112, 113, 121, 123, 126, 127, 128, 129, 130, 131, 132):
        ops75 = [model(mid).operands[i.op_first:i.op_first + i.nops] for i in model(mid).items if i.op == 75]
        assert ops75
        for w in ops75:
            assert len(w) == 5 and w[4] & 0x8000 and not any(x & 0x8000 for x in w[:4])
    for mid in range(48, 53):
        m = model(mid)
        assert m.blocked == 0 and any(i.op == 7 for i in m.items) and any(i.op == 8 for i in m.items)
    for mid in (102, 103):
        for i in model(mid).items:
            if i.op == 69:
                assert i.w & 0x4000 and i.flags & M.F_BACK_COLOUR and i.nops == 3


def test_op_42_leaves_a_slot_unresolved_when_a_source_is_missing_and_blocks_a_polygon_using_it():
    img = Img()
    # op 42 (opword 0x00A8) at base slot 0 over a 12-slot block: slot 20 needs V16, which does
    # not exist; then polygon 0, 1, 20 (20*6 = 120 = 0x78)
    simple(img, 9, [0x80A8, 0x0000, 0xC00C, 0x0000, 0x0006, 0x8078, 0x8001],
           [(i, 0, 0) for i in range(12)], 12)
    with pytest.raises(BlockedError, match=r"model 9: op 3 at 0x1214: BLOCKING: vertex slot 20 is unresolved"):
        decode_model(img.data, 9 * 20, 9, BASE)
    m = decode_model(img.data, 9 * 20, 9, BASE, strict=False)
    v = m.vertices[m.levels[0].vertex_first:]
    assert v[20].state == V_UNRESOLVED and v[100].state == V_UNRESOLVED
    assert v[17].state == V_SYNTH and v[22].state == V_SYNTH
    assert m.blocked == 1


def test_op_42_and_43_follow_the_notes_formulas_including_the_chained_slots():
    h = lambda x: x >> 1
    q = lambda x: x >> 2
    vs = [(3 * i - 5, i * i - 9, 7 - 2 * i) for i in range(30)]
    img = Img()
    simple(img, 9, [0x80AC, 0x0000, 0x80A8, 0x0000, 0xC008, 0, 6, 3], vs, 30)       # op 43 then op 42, base 0
    m = decode_model(img.data, 9 * 20, 9, BASE)
    v = verts_of(m)
    V = lambda k, c: vs[k][c]
    def comp(c):
        r = {}
        r[10] = V(3, c) + V(4, c) - V(6, c)
        r[11] = h(V(3, c) + V(4, c)) + q(V(5, c) - V(3, c))
        r[12] = h(r[11] + V(0, c))
        r[13] = V(7, c) + q(V(9, c) - V(7, c)) + h(q(V(9, c) - V(7, c)))
        r[14] = V(4, c) + V(5, c) - V(3, c)
        return r
    got = [comp(c) for c in range(3)]
    for k in (10, 11, 12, 13, 14):
        assert xyz(v[k]) == tuple(got[c][k] for c in range(3)), k
    for c in range(3):
        d = V(6, c) - V(1, c)
        b21 = h(d) + h(V(3, c) + V(4, c)) + 2 * d
        assert xyz(v[21])[c] == b21
        assert xyz(v[22])[c] == h(b21 + V(0, c))
        assert xyz(v[100])[c] == h(h(got[c][13] + got[c][14]) + got[c][10])


def test_synthesis_op_34_adds_parallelepiped_corners():
    img = Img()
    # op 34 (0x88) base 0 shift 1, then a polygon through the new slots 4, 5, 6
    simple(img, 9, [0x8088, 0x0000, 0x0001, 0xC00C, 0x0018, 0x001E, 0x8024, 0x8002],
           [(0, 0, 0), (1, 0, 0), (0, 2, 0), (0, 0, 4)], 4)
    m = decode_model(img.data, 9 * 20, 9, BASE)
    v = m.vertices[m.levels[0].vertex_first:]
    V0, V1, V2, V3 = [(0, 0, 0), (1, 0, 0), (0, 2, 0), (0, 0, 4)]
    add = lambda *ts: tuple(sum(c * V[k] for c, V in ts) for k in range(3))
    assert (v[4].x, v[4].y, v[4].z) == add((1, V0), (-1, V1), (1, V2)) and v[4].state == V_SYNTH
    assert (v[5].x, v[5].y, v[5].z) == add((1, V0), (-1, V1), (1, V3))
    assert (v[6].x, v[6].y, v[6].z) == add((1, V3), (1, V2), (-1, V1))
    assert (v[7].x, v[7].y, v[7].z) == add((1, V0), (-2, V1), (1, V2), (1, V3))
    # d = (V3 - V2) << 1 = (0, -4, 8); slot 8 = V3 + d; slot 9 = 2 V3 - V1 + d - (V3 - V2)
    assert (v[8].x, v[8].y, v[8].z) == (0, -4, 12)
    assert (v[9].x, v[9].y, v[9].z) == (-1, -2, 12)


def test_op_3_with_plane_word_copies_the_plane_record():
    img = Img()
    # W = 0x1003: bit 12, no axis bits: one offset word follows -> 12-byte record at base + 0x90
    simple(img, 9, [0xC00C, 0x0000, 0x0006, 0x800C, 0x1003, 0x0090], [(0, 0, 0), (1, 0, 0), (0, 1, 0)], 3)
    img.w(MB + 0x90, 10, 20, 30, 40, 50, 0xFFFF)
    m = decode_model(img.data, 9 * 20, 9, BASE)
    it = m.items[0]
    assert it.flags & F_PLANE
    assert m.operands[it.op_first:it.op_first + it.nops] == [3, 0, 1, 2, 10, 20, 30, 40, 50, -1]


def test_back_colour_word_follows_w_when_bit_14_is_set():
    img = Img()
    simple(img, 9, [0xC00C, 0x0000, 0x0006, 0x800C, 0x4005, 0x0009], [(0, 0, 0), (1, 0, 0), (0, 1, 0)], 3)
    m = decode_model(img.data, 9 * 20, 9, BASE)
    it = m.items[0]
    assert (it.w, it.wback, it.pen) == (0x4005, 9, 5) and it.flags & M.F_BACK_COLOUR


def test_state_selector_skip_variants_are_decoded_and_checked():
    img = Img()
    # op 54 (0xD8): no argument, skips the next word at state < 6; that word is a
    # last-flagged nop (0xC000); the item after it is the continuation.
    simple(img, 9, [0x80D8, 0xC000, 0xC008, 0x0000, 0x0006, 0x0003], [(0, 0, 0), (1, 0, 0)], 2)
    m = decode_model(img.data, 9 * 20, 9, BASE)
    assert [i.op for i in m.items] == [54, 0, 2]
    assert m.items[0].skips == [2, 0] and m.skips[:2] == [2, 0]
    assert read_model(write_model(m)) == m
    assert m.items[0].address + m.items[0].length + 2 == m.items[2].address


def test_strip_section_generates_even_and_odd_slots():
    img = Img()
    put_record(img, 9, MB)
    img.w(MB, 0x8040)
    img.w(MB + 2, 0xA010)
    # line from slot 1 to slot 4 (4 * 6 = 0x18): both made by the strip
    img.w(MB + 0x10, 0xC008, 0x0006, 0x0018, 0x0002)
    a = block(img, MB + 0x40, [(0, 0, 0)], 1, flags=0x08)
    # chain: C = 1; A = (0, 0, 0); segment to B = (160, 0, 0) cut in n = 2; D = 1;
    # backward point (160, 16, 0) with n = 1; then a negative count
    a = img.w(a, 1, 0, 0, 0)
    a = img.w(a, 160, 0, 0, 2)
    a = img.w(a, 1)
    a = img.w(a, 160, 16, 0, 1)
    img.w(a, 0xFFFF)
    m = decode_model(img.data, 9 * 20, 9, BASE)
    v = m.vertices[m.levels[0].vertex_first:]
    # forward slots 1, 3, 5: A, the half point, B; backward first slot = 1 + 6 - 3 = 4
    assert [(v[s].x, v[s].y, v[s].state) for s in (1, 3, 5, 4)] == [
        (0, 0, V_STRIP), (80, 0, V_STRIP), (160, 0, V_STRIP), (160, 16, V_STRIP)]


def type_b(img, mid, entries, handler=0x22D0A):
    """entries: list of (distance, program words, vertex pairs)."""
    put_record(img, mid, MB, handler)
    addr = MB
    plan = []
    for dist, prog, pairs in entries:
        n = len(prog)
        plan.append((addr, addr + 10 + 2 * n, dist, prog, pairs))
        addr += 10 + 2 * n + 6 + 4 * len(pairs)
    for i, (a, vb, dist, prog, pairs) in enumerate(plan):
        nxt = plan[i + 1][0] if i + 1 < len(plan) else 0xFFFFFFFF
        img.w(a, dist)
        img.l(a + 2, nxt, vb)
        img.w(a + 10, *prog)
        img.w(vb, len(pairs), 0, 0)
        for k, (x, z) in enumerate(pairs):
            img.w(vb + 6 + 4 * k, x, z)


def test_type_b_chain_widens_pairs_and_runs_the_stream():
    img = Img()
    type_b(img, 40, [(0x0A00, [0xC008, 0x0000, 0x0006, 0x0007], [(100, 200), (300, 400)]),
                     (0x0C00, [0xC008, 0x0000, 0x0006, 0x0007], [(1, 2), (3, 4)])])
    m = decode_model(img.data, 40 * 20, 40, BASE)
    assert m.kind == 2 and [l.threshold for l in m.levels] == [0x0A00, 0x0C00]
    v = m.vertices[:2]
    assert [(x.x, x.y, x.z) for x in v] == [(100, 0, 200), (300, 0, 400)]
    assert m.items[0].op == 2 and m.items[0].pen == 7


def test_type_b_zero_corners_are_exported_as_ordinary_vertices():
    img = Img()
    # id 109: near entry declares three vertices but stores two real ones and a zero pair;
    # the zero pair is literal file data (state 1), op 2 may name it, nothing is blocked
    type_b(img, 109, [(0x04D0, [0xC008, 0x0000, 0x000C, 0x0007], [(5, 6), (7, 8), (0, 0)]),
                      (0x0C00, [0xC008, 0x0000, 0x0006, 0x0007], [(5, 6), (7, 8)])])
    m = decode_model(img.data, 109 * 20, 109, BASE)
    assert m.blocked == 0 and m.levels[0].vertex_count == 3
    assert [(v.x, v.y, v.z, v.state) for v in m.vertices[:3]] == [
        (5, 0, 6, V_BLOCK), (7, 0, 8, V_BLOCK), (0, 0, 0, V_BLOCK)]
    assert m.operands[m.items[0].op_first:][:2] and 2 in m.operands[m.items[0].op_first:m.items[0].op_first + m.items[0].nops]
    assert read_model(write_model(m)) == m


def second_section_model(img, mid, words, b8=2, flags=6, prog_slot=0):
    simple(img, mid, [0xC008, 0x0000, 6 * prog_slot, 0x0004], [(1, 0, 2), (3, 0, 4), (5, 0, 6)][:b8], b8,
           flags=flags, flat=True)
    img.w(MB + 0x40 + 10 + 4 * b8, *words)
    return decode_model(img.data, mid * 20, mid, BASE)


def test_flat_second_section_is_normalised_and_slots_are_placeholders():
    img = Img()
    # group 1: two paired points, continuation word 5 (ignored value), two single points;
    # group 2: one paired point, negative continuation word ends the section
    words = [2, 10, 11, 12, 13, 5, 20, 21, 22, 23, 1, 30, 31, -1]
    m = second_section_model(img, 46, words, prog_slot=9)      # slot 9 = last slot of the section
    lv = m.levels[0]
    assert m.blocked == 0 and lv.vertex_count == 2 + 4 + 2 + 2
    pool = m.secpool[lv.sec_first:lv.sec_first + lv.sec_count]
    assert pool == [2, 10, 11, 12, 13, 2, 20, 21, 22, 23, 1, 30, 31, 0, -1]
    v = m.vertices[lv.vertex_first:lv.vertex_first + lv.vertex_count]
    assert [x.state for x in v] == [V_BLOCK] * 2 + [V_SECTION] * 8
    assert all((x.x, x.y, x.z) == (0, 0, 0) for x in v[2:])
    assert read_model(write_model(m)) == m


def test_flat_second_section_terminated_by_a_negative_group_count():
    img = Img()
    m = second_section_model(img, 72, [1, 7, 8, 0, 40, 41, 42, 43, -4], prog_slot=5)
    lv = m.levels[0]
    assert m.secpool[lv.sec_first:lv.sec_first + lv.sec_count] == [1, 7, 8, 2, 40, 41, 42, 43, -1]
    assert lv.vertex_count == 2 + 2 + 2


def test_second_section_zero_count_is_one_pair_and_continuation_value_is_ignored():
    img = Img()
    # n = 0 yields one pair (pool carries n = 1); the word 0x7FFF is non-negative: two singles follow
    m = second_section_model(img, 46, [0, 7, 8, 0x7FFF, 40, 41, 42, 43, 0, 9, 10, -1], prog_slot=7)
    lv = m.levels[0]
    assert m.secpool[lv.sec_first:lv.sec_first + lv.sec_count] == [1, 7, 8, 2, 40, 41, 42, 43, 1, 9, 10, 0, -1]
    assert lv.vertex_count == 2 + 2 + 2 + 2


def test_second_section_slot_beyond_the_section_is_still_a_bad_slot():
    img = Img()
    with pytest.raises(DecodeError, match="bad vertex slot 4"):
        second_section_model(img, 95, [1, 7, 8, -1], prog_slot=4)


def test_second_section_skipped_with_fewer_than_two_plain_vertices():
    img = Img()
    m = second_section_model(img, 73, [1, 7, 8, -1], b8=1)
    assert m.levels[0].sec_count == 0 and m.levels[0].vertex_count == 1 and m.secpool == []


def test_flat_block_without_flag_bit_2_has_no_section():
    img = Img()
    m = second_section_model(img, 44, [1, 7, 8, -1], flags=2)
    assert m.levels[0].sec_count == 0 and m.levels[0].vertex_count == 2


def test_type_b_chain_end_marker_gives_no_levels():
    img = Img()
    put_record(img, 38, MB, 0x22CFC)
    img.w(MB, 0xFFFF)
    m = decode_model(img.data, 38 * 20, 38, BASE)
    assert m.levels == [] and read_model(write_model(m)) == m


def starburst_like_program(pens=(8, 10)):
    """The shape of the effect models' program (note 07 11.3) with invented numbers: op 70, the empty item, then four variants of 0x86 bytes: an op 71 list of six discs, an ascending
    run of six op 62 items and a descending run of six."""
    words = [0x8118, 0xC000]
    discs = [(6 * (12 + k), pens[k & 1], 0x1000 + 0x100 * k) for k in range(6)]
    for _ in range(4):
        words += [0xC11C] + [w for k, (v, p, sz) in enumerate(discs) for w in (v, p, sz | (0x8000 if k == 5 else 0))]
        for run in (discs, discs[::-1]):
            for k, (v, p, sz) in enumerate(run):
                words += [0xC0F8 if k == 5 else 0x80F8, v, p, sz]
    return words


def test_op_70_is_the_op_word_alone_and_its_skip_list_reaches_every_variant():
    """Note 07 11.3: op 70 reads no operand; the program of the effect models is the op word, the empty item, then four variants of 0x86 bytes."""
    img = Img()
    prog = starburst_like_program()
    assert len(prog) * 2 == 2 + 2 + 4 * 0x86
    simple(img, 9, prog, [(i, 0, 0) for i in range(6)], 6, block_at=0x300)
    m = decode_model(img.data, 9 * 20, 9, BASE)
    ops = [i.op for i in m.items]
    assert ops[:2] == [70, 0] and ops.count(71) == 4 and ops.count(62) == 36   # 48 items exist; the 12 descending ones no life word lands on are not decoded
    it70 = m.items[0]
    assert it70.length == 2 and it70.skips[0] == 2
    assert M.op70_skip(-1) == 0 and M.op70_skip(0x40) == 2 and M.op70_skip(0x75) == 0x86 * 2 + 2 + 0x56 + 0 and M.op70_skip(0) == 2 + 0x26 + 8 * 5
    assert max(it70.skips) == 2 + 0x86 * 3 + 0x56 + 8 * 5
    first71 = [i for i in m.items if i.op == 71][0]
    assert m.operands[first71.op_first:first71.op_first + 4] == [6, 72, 8, 0x1000]


def test_every_documented_operation_has_an_argument_layout():
    """OPS and the parser chain must stay in step: parse each operation on zero words
    and never hit the 'no argument layout' branch."""
    for op in M.OPS:
        img = Img()
        simple(img, 9, [0xC000 | (op << 2)] + [0x8000] * 60, [(0, 0, 0)] * 8, 8)
        try:
            decode_model(img.data, 9 * 20, 9, BASE, strict=False)
        except DecodeError as e:
            assert "no argument layout" not in str(e)


def verts_of(m, level=0):
    l = m.levels[level]
    return m.vertices[l.vertex_first:l.vertex_first + l.vertex_count]


def xyz(v):
    return (v.x, v.y, v.z)


def test_op_52_derives_three_slots_from_a_difference():
    img = Img()
    vs = [(0, 0, 0), (10, 0, 0), (30, 10, 5), (1, 1, 1), (2, 2, 2), (3, 3, 3)]
    # op 52 (0xD0): a = slot 1, b = slot 2, t = slot 3; then a line to slots 6 and 8
    simple(img, 9, [0x80D0, 6, 12, 18, 0xC008, 0x24, 0x30, 3], vs, 6)
    v = verts_of(decode_model(img.data, 9 * 20, 9, BASE))
    d = (20, 10, 5)
    assert xyz(v[6]) == (1 + 20, 1 + 10, 1 + 5) and v[6].state == V_SYNTH
    assert xyz(v[7]) == (2 + 10, 2 + 5, 2 + 2)           # half difference, shifted right
    assert xyz(v[8]) == (3 + 10, 3 + 5, 3 + 2)


def op53_model(img, steps):
    """steps: list of (b byte offset, skip bytes); the ops are consecutive 4-word items."""
    vs = [(i, 2 * i, 3 * i) for i in range(10)]
    words = []
    for n, (b, skip) in enumerate(steps):
        words += [0x80D4, 0, b, skip]
    words += [0xC008, 0xB4, 0x108, 3]          # line slot 30 to slot 44
    simple(img, 9, words, vs, 10)
    return decode_model(img.data, 9 * 20, 9, BASE)


def test_op_53_fills_fifteen_slots_and_the_first_step_wins_at_state_zero():
    img = Img()
    m = op53_model(img, [(36, 8), (42, 0)])       # first step skips the second (8 bytes)
    v = verts_of(m)
    # slot 30 = slot 1 + (slot 6 - slot 0) for the first step (b = slot 6)
    assert xyz(v[30]) == (1 + 6, 2 + 12, 3 + 18) and v[30].state == V_SYNTH
    assert xyz(v[44]) == (5 + 8, 10 + 16, 15 + 24)       # k = 5, j = 2: slot 5 + (slot 8 - slot 0)
    assert [i.skips for i in m.items if i.op == 53] == [[8, 0], [0, 0]]
    # the second step (b = slot 7) is a variant: it must not overwrite the state-zero values
    assert xyz(v[30]) != (1 + 7, 2 + 14, 3 + 21)


def test_op_53_variants_alone_still_define_their_slots():
    img = Img()
    m = op53_model(img, [(36, 0), (42, 0)])       # no skip: both run; the later one is straight too
    assert xyz(verts_of(m)[30]) == (1 + 7, 2 + 14, 3 + 21)


def test_op_55_reads_polygon_lists_at_base_plus_offset():
    img = Img()
    lst = BASE + 0x300
    img.w(lst, 0x0000, 0x0006, 0x800C, 0x8004)
    simple(img, 9, [0xC0DC, lst >> 16, lst & 0xFFFF, 0x0000, 0x8000], [(0, 0, 0), (1, 0, 0), (0, 1, 0)], 3)
    m = decode_model(img.data, 9 * 20, 9, BASE)
    it = m.items[0]
    assert m.operands[it.op_first:it.op_first + it.nops] == [1, 0x8004, 0, F_TWO_SIDED, 3, 0, 1, 2]


def test_op_41_and_op_36_use_the_documented_formulas():
    img = Img()
    vs = [(3 * i, i * i, 7 - i) for i in range(9)]
    simple(img, 9, [0x80A4, 0, 1, 0xC008, 0, 6, 3], vs, 9)           # op 41, shift 1
    v = verts_of(decode_model(img.data, 9 * 20, 9, BASE))
    V = vs
    s = lambda f: tuple(f(k) for k in range(3))
    assert xyz(v[9]) == s(lambda k: V[2][k] + V[3][k] - V[1][k])
    d = s(lambda k: (V[2][k] - V[3][k]) << 1)
    assert xyz(v[10]) == s(lambda k: V[3][k] - d[k])
    assert xyz(v[13]) == s(lambda k: V[5][k] + V[4][k] - V[6][k])
    assert xyz(v[17]) == s(lambda k: V[4][k] + V[5][k] - 3 * V[6][k] + V[7][k] + V[8][k])
    img = Img()
    simple(img, 9, [0x8090, 0, 1, 0xC008, 0, 6, 3], vs, 6)           # op 36, shift 1
    v = verts_of(decode_model(img.data, 9 * 20, 9, BASE))
    assert xyz(v[6]) == s(lambda k: V[0][k] - V[1][k] + V[2][k])
    dd = s(lambda k: (V[4][k] - V[3][k]) << 1)
    assert xyz(v[12]) == s(lambda k: V[4][k] + dd[k])
    assert xyz(v[13]) == s(lambda k: 2 * V[4][k] - V[2][k] + dd[k] - (V[4][k] - V[3][k]))


def test_halved_terms_are_shifted_before_they_are_summed():
    img = Img()
    vs = [(0, 0, 0)] * 30
    vs[8] = (1, 0, 0)
    vs[9] = (2, 0, 0)
    # op 59 base 0: slot 28 = h(4h(V3+V4) - 2V3 + 2V5 - V8 - V9) = h(-3) = -2
    simple(img, 9, [0x80EC, 0, 0xC008, 0xA8, 0xA8, 3], vs, 20)
    v = verts_of(decode_model(img.data, 9 * 20, 9, BASE))
    assert xyz(v[28]) == (-2, 0, 0)
    assert v[26].state == V_SYNTH and v[27].state == V_SYNTH


def test_op_67_selector_keeps_the_nine_following_words_as_items():
    img = Img()
    vs = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    prog = [0x810C, 0, 6, 12, 0x8001,                  # op 67: three slots and W
            0x8008, 0, 6, 3,                           # a line
            0x8114, 0, 6, 12, 5,                       # op 69 quad
            0xC008, 0, 6, 4]                           # the item after the 18 skipped bytes
    simple(img, 9, prog, vs, 3)
    m = decode_model(img.data, 9 * 20, 9, BASE)
    assert [i.op for i in m.items] == [67, 2, 69, 2]
    assert m.items[0].skips == [0, 18] and m.items[0].w == 0x8001
    assert m.operands[m.items[0].op_first:][:3] == [0, 1, 2]


def test_ops_68_and_72_variants_are_followed_and_flagged_speculative():
    img = Img()
    simple(img, 9, [0xC110], [(0, 0, 0)], 1, block_at=0x400)
    for k in range(1, 16):
        img.w(MB + 0x12 + 0x34 * k, 0xC000)
    m = decode_model(img.data, 9 * 20, 9, BASE)
    assert m.items[0].op == 68 and not m.items[0].flags & M.F_SPECULATIVE
    assert len(m.items[0].skips) == 16 and m.items[0].skips[:2] == [0, 0x34]
    assert [bool(i.flags & M.F_SPECULATIVE) for i in m.items[1:]] == [True] * 15


def test_flat_block_extra_vertices_use_a_stride_of_four():
    img = Img()
    # gated item: g1, g2, N = 2, start = byte 4 of the block data (slot 1), line op 2 last
    prog = [0x0100, 0xFFFF, 2, 4, 0xC008, 0, 12, 3]
    simple(img, 9, prog, [(5, 0, 6), (7, 0, 8), (9, 0, 10)], 1, flags=2, flat=True)
    m = decode_model(img.data, 9 * 20, 9, BASE)
    v = verts_of(m)
    assert [xyz(x) for x in v] == [(5, 0, 6), (7, 0, 8), (9, 0, 10)]
    assert m.items[0].extra_slot == 1 and m.items[0].extra_n == 2


def test_block_gap_slots_are_not_filled_from_the_block():
    img = Img()
    simple(img, 9, [0xC008, 0, 0x12, 3], [(0, 0, 0), (1, 1, 1), (2, 2, 2), (3, 3, 3)], 2)
    with pytest.raises(ValueError, match="bad vertex slot 3"):
        decode_model(img.data, 9 * 20, 9, BASE)


def test_ops_39_and_40_check_their_absolute_slots():
    img = Img()
    simple(img, 9, [0xC09C, 0, 1, 2], [(0, 0, 0), (1, 0, 0)], 2)
    with pytest.raises(ValueError, match="bad vertex slot 2"):
        decode_model(img.data, 9 * 20, 9, BASE)


def test_bsp_node_on_an_unresolved_slot_is_blocked_not_failed_in_lenient_mode():
    img = Img()
    put_record(img, 9, MB)
    img.w(MB, 0x8100)
    # three-vertex node on slots 22, 0, 1 (slot 20 is unresolved after op 42 over a short block), children: the draw item
    img.w(MB + 2, 0x0078, 0x0000, 0x0000, 0x0006, 0x000E, 0x000E)
    img.w(MB + 0x0E, 0xA020)
    img.w(MB + 0x20, 0x80A8, 0x0000, 0xC008, 0x0000, 0x0006, 0x0003)
    block(img, MB + 0x100, [(i, 0, 0) for i in range(12)], 12)
    with pytest.raises(BlockedError, match="BLOCKING: vertex slot 20 is unresolved"):
        decode_model(img.data, 9 * 20, 9, BASE)
    m = decode_model(img.data, 9 * 20, 9, BASE, strict=False)
    assert m.elements[m.levels[0].root].flags & M.EF_BLOCKED and m.blocked == 1


def frame_model(img, op_word, unit, nested=True):
    simple(img, 9, [op_word], [(0, 0, 0), (1, 0, 0), (0, 1, 0)], 3, block_at=0x400)
    for k in range(1, 16):
        at = MB + 0x12 + unit * k
        if k == 1 and nested:      # op 67 inside the first frame: its nine words, then the skip target
            img.w(at, 0x810C, 0, 6, 12, 0x8001, 0x8008, 0, 6, 3, 0x8114, 0, 6, 12, 5, 0xC000)
        else:
            img.w(at, 0xC000)
    return decode_model(img.data, 9 * 20, 9, BASE)


def test_speculative_flag_survives_a_selector_nested_in_a_frame():
    for op_word, unit in ((0xC110, 0x34), (0xC120, 0x12)):      # ops 68 and 72
        m = frame_model(Img(), op_word, unit, nested=unit == 0x34)   # 0x12 frames are too short to nest
        assert m.items[0].op in (68, 72) and not m.items[0].flags & M.F_SPECULATIVE
        if unit == 0x34:
            assert [i.op for i in m.items[1:5]] == [67, 2, 69, 0]
        assert len(m.items) > 15
        assert all(i.flags & M.F_SPECULATIVE for i in m.items[1:])


def test_slot_defined_only_by_a_variant_exists():
    img = Img()
    vs = [(i, 2 * i, 3 * i) for i in range(10)]
    # first step skips the second; the second (a = slot 1) writes slots 31..45, slot 45 only there
    words = [0x80D4, 0, 36, 8, 0x80D4, 6, 36, 0, 0xC008, 0xB4, 0x10E, 3]
    simple(img, 9, words, vs, 10)
    v = verts_of(decode_model(img.data, 9 * 20, 9, BASE))
    assert v[45].state == V_SYNTH and xyz(v[45]) == (6 + 8 - 1, 12 + 16 - 2, 18 + 24 - 3)



@pytest.mark.skipif(not REAL_BIN.exists(), reason="extracted/interceptor.bin absent")
def test_real_image_second_sections_and_type_b_zero_corners():
    from extract.tables import RECORD_SIZE, RECORD_TABLE, IMAGE_BASE
    data = REAL_BIN.read_bytes()
    for mid in (46, 72, 73, 95):
        m = decode_model(data, RECORD_TABLE - IMAGE_BASE + RECORD_SIZE * mid, mid)
        assert m.blocked == 0
        for lv in m.levels:
            if not lv.present:
                continue
            assert lv.sec_count > 0 and m.secpool[lv.sec_first + lv.sec_count - 1] == -1
            v = m.vertices[lv.vertex_first:lv.vertex_first + lv.vertex_count]
            sec = [x for x in v if x.state == V_SECTION]
            assert len(sec) == lv.vertex_count - lv.b8 and v[lv.b8].state == V_SECTION
            # the vertex slice ends exactly at the section's last slot, and the programs name it
            pool, k, slots = m.secpool[lv.sec_first:lv.sec_first + lv.sec_count], 0, 0
            while pool[k] >= 0:
                n = pool[k]
                mm = pool[k + 1 + 2 * n]
                slots += 2 * n + mm
                k += 2 + 2 * n + 2 * mm
            assert slots == len(sec)
    m = decode_model(data, RECORD_TABLE - IMAGE_BASE + RECORD_SIZE * 109, 109)
    assert m.blocked == 0 and all(v.state == V_BLOCK for v in m.vertices)
    assert any((v.x, v.z) == (0, 0) for v in m.vertices[:m.levels[0].vertex_count])


# ---------------------------------------------------------------------------
# The cockpit-alternate detail list as extra levels (note 02 section 2.5), IMDL version 4

def alt_model(img, mid=3, alt_delta=8):
    put_record(img, mid, MB)
    img.l(REC + 20 * mid + 12, MB - alt_delta)
    img.w(MB, 0x8050)                                  # normal list: one leaf-only level
    img.w(MB + 2, 0xA010)                              # its stream: one program at +0x10
    img.w(MB + 0x10, 0xC008, 0x0000, 0x0006, 0x0003)   # a line, pen 3
    block(img, MB + 0x50, [(0, 0, 0), (1, 0, 0), (0, 1, 0)], 3)
    # the near body: op 59 (base 0) then op 4 with one face through slots 21, 22, 23 (byte offsets 126, 132, 138)
    img.w(MB + 0x20, 0x80EC, 0x0000, 0xC010, 0x0A00, 0x0003, 0x0001, 126, 132, 138, 0xFFFF)
    block(img, MB + 0x90, [(i, 2 * i, 3 * i) for i in range(12)], 12)
    if alt_delta == 8:
        img.w(MB - 8, 0x8090, 0xA020, 0x80C0, 0xFFFF)  # near leaf, one-program item at +0x20, cockpit leaf, end
        block(img, MB + 0xC0, [(0, 0, 0), (100, 0, 0), (0, 100, 0), (100, 100, 0)], 4)
        img.w(MB + 0xC0, 0xFFFF)                       # part id 0xFFFF
    else:
        img.w(MB - 2, 0xFFFF)
    return decode_model(img.data, 20 * mid, mid, BASE)


def test_alternate_list_is_exported_as_two_extra_levels():
    img = Img()
    m = alt_model(img)
    assert (m.nnormal, m.alt_near, m.alt_cockpit) == (1, 1, 2) and len(m.levels) == 3
    n, c = m.levels[1], m.levels[2]
    assert (n.shape, n.threshold, n.present, n.leaf) == (2, 0, 1, 0x8090)
    assert (c.shape, c.threshold, c.present, c.part_id, c.b8) == (2, 0, 0, 0xFFFF, 4)
    assert c.root == -1 and c.vertex_count == 4
    # the near level carries op 59's synthesised slots and op 4's face, whose slots all resolve
    v = m.vertices[n.vertex_first:n.vertex_first + n.vertex_count]
    assert [v[k].state for k in (21, 22, 23)] == [V_SYNTH] * 3
    root = m.elements[n.root]
    prog = m.programs[m.proglists[root.list_first]]
    assert [m.items[prog.item_first + k].op for k in range(prog.item_count)] == [59, 4]
    assert m.blocked == 0
    assert read_model(write_model(m)) == m


def test_alternate_list_of_models_0_and_1_is_one_absent_level():
    img = Img()
    m = alt_model(img, mid=1, alt_delta=2)
    assert (m.nnormal, m.alt_near, m.alt_cockpit) == (1, 1, 1) and len(m.levels) == 2
    assert m.levels[1].present == 0 and m.levels[1].leaf == 0xFFFF
    assert read_model(write_model(m)) == m


def test_models_with_no_distinct_alternate_list_have_no_alternate_levels():
    img = Img()
    m = two_level_model(img)
    assert (m.nnormal, m.alt_near, m.alt_cockpit) == (2, 0xFFFF, 0xFFFF)


def test_imdl_version_5_header_is_60_bytes_with_the_alternate_fields():
    img = Img()
    m = alt_model(img)
    b = write_model(m)
    assert b[:4] == b"IMDL" and struct.unpack_from("<H", b, 4)[0] == 5
    assert struct.unpack_from("<HHHH", b, 52) == (1, 1, 2, 0)
    assert M._HDR.size == 60


@pytest.mark.skipif(not REAL_BIN.exists(), reason="the relocated image is not present")
@pytest.mark.parametrize("mid", [2, 13, 11])
def test_the_cone_models_carry_four_phase_lists_after_their_own(mid):
    """Note 07 11.5: the pod (2, 13) and the parachute (11) have four more detail lists chosen by the phase; they follow the normal levels as levels, phase_first is 1 + the first."""
    from extract.tables import IMAGE_BASE, RECORD_SIZE, RECORD_TABLE
    m = decode_model(REAL_BIN.read_bytes(), RECORD_TABLE - IMAGE_BASE + RECORD_SIZE * mid, mid, IMAGE_BASE)
    assert m.phase_first == m.nnormal + 1 and len(m.levels) == m.nnormal + 4
    assert all(l.present for l in m.levels[m.nnormal:])
    assert read_model(write_model(m)) == m
    other = decode_model(REAL_BIN.read_bytes(), RECORD_TABLE - IMAGE_BASE + RECORD_SIZE * 3, 3, IMAGE_BASE)
    assert other.phase_first == 0                                   # an ordinary model has none


@pytest.mark.skipif(not REAL_BIN.exists(), reason="the relocated image is not present")
@pytest.mark.parametrize("mid,flags", [(0, 0x18), (1, 0x48), (3, 0x01), (8, 0x11), (9, 0x11), (10, 0xFC), (12, 0x11), (13, 0x01)])
def test_the_hull_flag_byte_is_the_block_flag_the_guns_box_test_reads(mid, flags):
    """Note 07 8.2: the gun's box test reads +7 of the block named by the first word of the model's list (bit 15 set) or by the word at +4; bit 4 selects the box 0x800 and the ship routine."""
    from extract.tables import IMAGE_BASE, RECORD_SIZE, RECORD_TABLE
    m = decode_model(REAL_BIN.read_bytes(), RECORD_TABLE - IMAGE_BASE + RECORD_SIZE * mid, mid, IMAGE_BASE)
    assert m.hull_flags == flags
    assert read_model(write_model(m)).hull_flags == flags            # it travels in the pad byte of every level record


def test_the_hull_flag_byte_survives_the_file_format_and_is_zero_for_older_files():
    m = M.Model(5, 0x1234, 0)
    m.levels.append(M.Level(threshold=1, shape=0, present=1, leaf=0x4010, part_id=1, tm=2, tf=4, scale_h=2, scale_l=3, block_flags=1, b8=0, b9=0, root=-1))
    m.hull_flags = 0xFC
    assert read_model(write_model(m)).hull_flags == 0xFC
    b = bytearray(write_model(m))
    b[M._HDR.size + 17] = 0                                           # a file written before extractor 20 has a zero pad
    assert read_model(bytes(b)).hull_flags == 0
