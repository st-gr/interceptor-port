import struct
from pathlib import Path

import pytest

from extract.combat import decode_templates, write_templates
from extract.tables import DecodeError, IMAGE_BASE, RECORD_COUNT, RECORD_SIZE, RECORD_TABLE

REAL = Path(__file__).resolve().parents[3] / "assets"


def invented_records():
    return [struct.pack(">5I", 0x01000000 * (i % 7) + i, 0xA0A00000 + 3 * i, i * i, 0x00FF00FF ^ i, 0xC0DE0000 | (i << 4))
            for i in range(RECORD_COUNT)]


def image(recs, size=0x40000):
    img = bytearray(size)
    o = RECORD_TABLE - IMAGE_BASE
    img[o:o + len(recs) * RECORD_SIZE] = b"".join(recs)
    return bytes(img)


def test_templates_round_trip(tmp_path):
    recs = invented_records()
    write_templates(image(recs), tmp_path)
    b = (tmp_path / "tables" / "templates.bin").read_bytes()
    assert struct.unpack_from("<HH", b) == (134, 20)
    assert len(b) == 4 + 134 * 20
    assert b[4:] == b"".join(recs)          # raw, image byte order
    assert struct.unpack_from(">5I", b, 4 + 20 * 9) == struct.unpack(">5I", recs[9])


def test_templates_refuse_a_table_outside_the_image():
    with pytest.raises(DecodeError, match=r"template table at 0x25ECC"):
        decode_templates(bytes(0x25ECC - IMAGE_BASE + 100))


def test_real_templates_structure_only():
    p = REAL / "tables" / "templates.bin"
    if not p.exists():
        pytest.skip("assets/tables/templates.bin not extracted")
    b = p.read_bytes()
    assert struct.unpack_from("<HH", b) == (134, 20) and len(b) == 4 + 134 * 20


# ---- tables/ai.bin ----
from extract.combat import AI_RUN_TABLE, decode_ai, write_ai, AI_RATE_ADDRS, AI_EVASION, AI_RING3, AI_START, AI_CAM_POINTS, AI_FINE, AI_OFFSETS, AI_PRE_WORD, AI_HULL_PTRS, AI_HULL_LISTS, decode_hull


def put(img, addr, words):
    struct.pack_into(">%dh" % len(words), img, addr - IMAGE_BASE, *words)


HULL_FACES = [[(6 * ((l + f) % 17) | (0x4000 if f % 2 else 0), 6 * f, 6 * l + 12) for f in range(6)] for l in range(3)]


def put_hull(img):
    struct.pack_into(">3I", img, AI_HULL_PTRS - IMAGE_BASE, *AI_HULL_LISTS)
    for l, a in enumerate(AI_HULL_LISTS):
        put(img, a, [w for f in HULL_FACES[l] for w in f] + [-1, 0x21, 0x12])   # the junk after the terminator word is not exported


def ai_image():
    img = bytearray(0x50000)
    for k, a in enumerate(AI_RATE_ADDRS):
        put(img, a, [101 * k + i for i in range(11)])
    put(img, AI_EVASION, [(-1) ** i * (7 + i) for i in range(19 * 8)])
    put(img, AI_OFFSETS, [(-1) ** i * (30 + i) for i in range(42)])
    put(img, AI_PRE_WORD, [20085])
    put(img, AI_RING3, [-3 * i + 1 for i in range(30)])
    put(img, AI_START, [(5 if e < 3 else -0x7FF0 + e) if w == 0 else 1000 * e + w for e in range(5) for w in range(8)])
    for i in range(3):   # the records are 16 bytes apart in the image (index << 4); the two words between are junk
        put(img, AI_CAM_POINTS + 16 * i, [2000 + 6 * i + j for j in range(6)] + [-77, -78])
    put(img, AI_FINE, [-4000 + 17 * i for i in range(32)])
    put_hull(img)
    r = runs_image()
    lo = AI_RUN_TABLE - IMAGE_BASE
    img[lo:lo + 2 * 60 + 10 * 60 * 6] = r[lo:lo + 2 * 60 + 10 * 60 * 6]   # the waypoint runs
    return bytes(img)


def subtables(b):
    o, out = 0, []
    while o < len(b):
        c, s = struct.unpack_from("<HH", b, o)
        out.append((c, s, struct.unpack_from("<%dh" % (c * s // 2), b, o + 4)))
        o += 4 + c * s
    assert o == len(b)
    return out


def test_ai_round_trip(tmp_path):
    write_ai(ai_image(), tmp_path)
    t = subtables((tmp_path / "tables" / "ai.bin").read_bytes())
    assert [(c, s) for c, s, _ in t] == [(11, 2)] * 5 + [(19, 16), (14, 6), (10, 6), (5, 16), (3, 12), (16, 4), (1, 2), (60, 332), (21, 6)]
    assert t[2][2] == tuple(202 + i for i in range(11))
    assert t[5][2][:4] == (7, -8, 9, -10)
    assert t[6][2][:3] == (30, -31, 32) and t[6][2][39:] == (-69, 70, -71)
    assert t[11][2] == (20085,)
    assert t[7][2][:3] == (1, -2, -5)
    assert t[13][2] == tuple(w for l in range(3) for f in HULL_FACES[l] + [(-1, 0, 0)] for w in f)
    assert t[9][2] == tuple(2000 + i for i in range(18)) and t[10][2][31] == -4000 + 17 * 31


def test_hull_pointer_and_faces_are_checked():
    img = bytearray(ai_image())
    struct.pack_into(">I", img, AI_HULL_PTRS - IMAGE_BASE + 4, AI_HULL_LISTS[1] + 2)
    with pytest.raises(DecodeError, match=r"part id 1"):
        decode_hull(bytes(img))
    img = bytearray(ai_image())
    put(img, AI_HULL_LISTS[2] + 6 * 3 + 2, [102])    # face 3, word b = slot 17
    with pytest.raises(DecodeError, match=r"part id 2 .* face 3"):
        decode_hull(bytes(img))
    img = bytearray(ai_image())
    put(img, AI_HULL_LISTS[0] + 6 * 6, [0])           # the terminator lost
    with pytest.raises(DecodeError, match=r"part id 0 .* no terminator"):
        decode_hull(bytes(img))
    with pytest.raises(DecodeError, match=r"hull pointer table"):
        decode_hull(bytes(10))


def test_ai_refuses_naming_the_table():
    with pytest.raises(DecodeError, match=r"rate table 0x31316"):
        decode_ai(bytes(0x31316 - IMAGE_BASE + 4))
    with pytest.raises(DecodeError, match=r"rate table 0x3136E at 0x3136E"):
        decode_ai(ai_image()[:0x3136E - IMAGE_BASE + 10])


def test_real_ai_structure_only():
    p = REAL / "tables" / "ai.bin"
    if not p.exists():
        pytest.skip("assets/tables/ai.bin not extracted")
    t = subtables(p.read_bytes())
    assert [(c, s) for c, s, _ in t] == [(11, 2)] * 5 + [(19, 16), (14, 6), (10, 6), (5, 16), (3, 12), (16, 4), (1, 2), (60, 332), (21, 6)]
    assert len(t[11][2]) == 1
    assert (t[13][0], t[13][1]) == (21, 6)
    for l in range(3):   # structure only: faces name slots 0 to 16 (offsets 6k), the terminator has bit 15 set
        tri = [t[13][2][3 * (7 * l + k):3 * (7 * l + k) + 3] for k in range(7)]
        for a, b, c in tri[:6]:
            assert a >= 0 and all(v % 6 == 0 and 0 <= v <= 96 for v in (a & 0x3FFF, b, c))
        assert tri[6] == (tri[6][0], 0, 0) and tri[6][0] < 0
    # note 07 2.7, structure only: the hard points come in left/right pairs (6 of the class-0x10 owners, 3 pairs of the others), mirrored in x, equal in y and z
    hp = t[6][2]
    for i in range(2, 14, 2):
        left, right = hp[3 * i:3 * i + 3], hp[3 * i + 3:3 * i + 6]
        assert left[0] < 0 < right[0] and left[0] == -right[0] and left[1:] == right[1:]


# ---- the waypoint runs inside tables/ai.bin (note 07 4.3) ----
from extract.combat import decode_runs, AI_RUN_TABLE


def runs_image():
    img = bytearray(0x50000)
    base = AI_RUN_TABLE - IMAGE_BASE
    blocks_at = base + 2 * 60
    # run k = k % 4 + 1 blocks; blocks start after the table; a block whose first word has bit 15 set ends the run (0xFFFF or 0xFF00)
    off = 2 * 60
    for k in range(60):
        struct.pack_into(">H", img, base + 2 * k, off)
        n = k % 4 + 1
        for j in range(n):
            struct.pack_into(">5h", img, base + off + 10 * j, 60 + k, 70 + j, 8000 + k, 9000 + j, 100 * k + j)
        struct.pack_into(">5h", img, base + off + 10 * n, -1 if k % 2 else -256, 0, 0, 0, 0)
        off += 10 * (n + 1)
    return bytes(img)


def test_runs_end_before_the_first_block_with_bit_15_set():
    b = decode_runs(runs_image())
    assert struct.unpack_from("<HH", b) == (60, 332)
    for k in range(60):
        rec = struct.unpack_from("<166h", b, 4 + 332 * k)
        n = k % 4 + 1
        assert rec[0] == n
        assert rec[1:6] == (60 + k, 70, 8000 + k, 9000, 100 * k)
        assert rec[1 + 5 * (n - 1):6 + 5 * (n - 1)] == (60 + k, 70 + n - 1, 8000 + k, 9000 + n - 1, 100 * k + n - 1)
        assert all(w == 0 for w in rec[1 + 5 * n:])


def test_a_run_that_never_ends_or_leaves_the_image_is_refused():
    img = bytearray(runs_image())
    for j in range(34):
        struct.pack_into(">5h", img, AI_RUN_TABLE - IMAGE_BASE + 120 + 10 * j, 1, 1, 1, 1, 1)
    with pytest.raises(DecodeError, match=r"run 0 .* more than 33 blocks"):
        decode_runs(bytes(img))
    with pytest.raises(DecodeError, match=r"waypoint run table"):
        decode_runs(bytes(10))


def test_real_runs_hold_one_to_33_blocks():
    p = REAL / "tables" / "ai.bin"
    if not p.exists():
        pytest.skip("assets/tables/ai.bin not extracted")
    t = subtables(p.read_bytes())
    assert (t[12][0], t[12][1]) == (60, 332)
    lens = [t[12][2][166 * k] for k in range(60)]
    assert all(1 <= n <= 33 for n in lens)   # structure only: a count of 1 to 33 blocks per run
