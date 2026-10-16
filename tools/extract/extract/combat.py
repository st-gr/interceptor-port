"""Combat tables (docs/re/02-assets-and-tables.md section 1; the template/record table).

tables/templates.bin: ``u16 count, u16 record_size`` (little endian), then the records exactly as
they stand in the image: 134 records of 20 bytes, each five 32-bit longs in the image's own (big
endian) byte order. Note 02 names no field boundaries inside the long words, so the records are
carried as five raw longs; the core reader converts each long to a host u32."""
import struct
from pathlib import Path

from .tables import DecodeError, IMAGE_BASE, RECORD_COUNT, RECORD_SIZE, RECORD_TABLE


def decode_templates(image: bytes) -> bytes:
    off = RECORD_TABLE - IMAGE_BASE
    end = off + RECORD_COUNT * RECORD_SIZE
    if off < 0 or end > len(image):
        raise DecodeError(f"template table at 0x{RECORD_TABLE:X}: {RECORD_COUNT} records of {RECORD_SIZE} bytes "
                          f"need the bytes up to 0x{IMAGE_BASE + end:X}, image has {len(image)}")
    return struct.pack("<HH", RECORD_COUNT, RECORD_SIZE) + bytes(image[off:end])


def write_templates(image: bytes, staging) -> None:
    d = Path(staging) / "tables"
    d.mkdir(parents=True, exist_ok=True)
    (d / "templates.bin").write_bytes(decode_templates(image))


# ---- tables/ai.bin (docs/re/07-ai-and-weapons.md sections 3.6, 4.5, 6.1, 6.3 and the Part A constants) ----
# Little endian; every sub-table is prefixed ``u16 count, u16 record_size`` and holds signed 16-bit words
# (the image's words are big endian), in this order:
#   1-5  the rate tables 0x31316, 0x3132C, 0x31342, 0x31358, 0x3136E   11 records of 2 bytes (one word)
#   6    the evasion table 0x30284       19 groups of 16 bytes: 4 x (counter, state), one pair per progress 0 to 3
#   7    the offset table 0x2789E (2.7)  14 records of 6 bytes (x, y, z model units): 0 seat, 1 parachute/pod, 2-7 the six missile hard points of
#        class-0x10 owners (0x278AA), 8-13 the six of the other owners (0x278CE)
#   8    ring offsets, class nibble 3, 0x2D718   10 records of 6 bytes (x, y, z), directions 0 to 9
#   9    start blocks 0x47ADE            5 records of 16 bytes (8 words, note 05 section 10; entries 3 and 4 name carrier records 14, 15)
#   10   airfield camera points 0x47B30  3 records of 6 words 16 bytes apart in the image (index << 4; cell x, cell z, k, h, offset x, offset z), packed to 12 bytes
#   11   fine offsets 0x206F2            16 records of 4 bytes (x, z)
#   12   the word before the rate tables 0x31314   1 record of 2 bytes (20085, the code word 0x4E75 read by the non-aircraft pitch case with a
#        negative index, 3.6); a separate sub-table, so the rate tables keep their 11 entries and their indices
#   13   the waypoint runs of state 8 (4.3): the table 0x2DCE8 of 60 word offsets, each to the first of consecutive 10-byte blocks (five words) that
#        ends before the first block whose first word has bit 15 set; 60 records of 166 words: the number of blocks (0 to 33), then 33 blocks of
#        five words (unused blocks zero). Entry 0 is the table itself (33 blocks, unused by the game)
AI_RATE_ADDRS = (0x31316, 0x3132C, 0x31342, 0x31358, 0x3136E)
AI_RATE_ENTRIES = 11
AI_EVASION = 0x30284
AI_EVASION_GROUPS = 19
AI_OFFSETS = 0x2789E
AI_OFFSET_COUNT = 14
AI_PRE_WORD = 0x31314
AI_RING3 = 0x2D718
AI_RING3_DIRECTIONS = 10
AI_START = 0x47ADE
AI_START_ENTRIES = 5
AI_CAM_POINTS = 0x47B30
AI_CAM_POINT_COUNT = 3
AI_FINE = 0x206F2
AI_FINE_COUNT = 16
AI_RUN_TABLE = 0x2DCE8
AI_RUN_COUNT = 60
AI_RUN_MAX = 33
AI_RUN_RECORD_WORDS = 1 + 5 * AI_RUN_MAX
# 14   the hull lists of the aircraft hit test (note 07 8.2a): the pointer table 0x3DACC (three longs, the part ids 0, 1, 2) is read and each pointer
#      must be the list the note names (0x3DAFA, 0x3DD28, 0x3DD94), else the part id is refused. 21 records of 6 bytes (a triple of words), three lists of
#      7 triples in order (the note's lists all have the same length: no header word): six face triples (a, b, c) and a terminator triple. a, b, c are
#      byte offsets into the record's vertex array (slot k = 6k, slots 0 to 16 = 0 to 96); bit 14 of a selects form B (a & 0x3FFF is the plane point).
#      The terminator carries its first word (bit 15 set) and zeros: its other two words are not read by the test and are not exported. No model vertices.
AI_HULL_PTRS = 0x3DACC
AI_HULL_LISTS = (0x3DAFA, 0x3DD28, 0x3DD94)
AI_HULL_FACES = 6
AI_HULL_SLOT_MAX = 16


def _ai_words(image: bytes, addr: int, n: int, what: str):
    off = addr - IMAGE_BASE
    if off < 0 or off + 2 * n > len(image):
        raise DecodeError(f"ai table {what} at 0x{addr:X}: needs {2 * n} bytes, image has {len(image)}")
    return struct.unpack_from(">%dh" % n, image, off)


def _ai_sub(image: bytes, addr: int, count: int, words_per_record: int, what: str, stride_words: int = 0) -> bytes:
    """count records of words_per_record words, packed; the records lie stride_words apart in the image (default: back to back)."""
    stride = stride_words or words_per_record
    span = _ai_words(image, addr, stride * (count - 1) + words_per_record, what)
    w = tuple(x for i in range(count) for x in span[stride * i:stride * i + words_per_record])
    return struct.pack("<HH", count, 2 * words_per_record) + struct.pack("<%dh" % len(w), *w)


def decode_runs(image: bytes) -> bytes:
    """The waypoint runs (note 07 4.3): for each of the 60 offsets of the table at 0x2DCE8 the blocks from there up to, not including, the first block
    whose first word has bit 15 set, as a record of 1 + 5 * 33 words (the count, the blocks, zero padding)."""
    base = AI_RUN_TABLE - IMAGE_BASE
    if base < 0 or base + 2 * AI_RUN_COUNT > len(image):
        raise DecodeError(f"waypoint run table at 0x{AI_RUN_TABLE:X} needs {2 * AI_RUN_COUNT} bytes, image has {len(image)}")
    out = b""
    for k in range(AI_RUN_COUNT):
        off = struct.unpack_from(">H", image, base + 2 * k)[0]
        a = base + off
        blocks = []
        while True:
            if a + 10 > len(image):
                raise DecodeError(f"waypoint run {k} at 0x{IMAGE_BASE + base + off:X} leaves the image after {len(blocks)} blocks")
            w = struct.unpack_from(">5h", image, a)
            if w[0] < 0:   # bit 15 set: the run ends before this block
                break
            blocks.append(w)
            if len(blocks) > AI_RUN_MAX:
                raise DecodeError(f"waypoint run {k} at 0x{IMAGE_BASE + base + off:X} has more than {AI_RUN_MAX} blocks")
            a += 10
        words = [len(blocks)] + [x for b in blocks for x in b]
        words += [0] * (AI_RUN_RECORD_WORDS - len(words))
        out += struct.pack("<%dh" % AI_RUN_RECORD_WORDS, *words)
    return struct.pack("<HH", AI_RUN_COUNT, 2 * AI_RUN_RECORD_WORDS) + out


def decode_hull(image: bytes) -> bytes:
    """The hull lists (note 07 8.2a): 3 lists x (6 faces + terminator) triples of words; see the layout comment above."""
    base = AI_HULL_PTRS - IMAGE_BASE
    if base < 0 or base + 4 * len(AI_HULL_LISTS) > len(image):
        raise DecodeError(f"hull pointer table at 0x{AI_HULL_PTRS:X} needs {4 * len(AI_HULL_LISTS)} bytes, image has {len(image)}")
    words = []
    for pid, want in enumerate(AI_HULL_LISTS):
        ptr = struct.unpack_from(">I", image, base + 4 * pid)[0]
        if ptr != want:
            raise DecodeError(f"hull list of part id {pid}: the table 0x{AI_HULL_PTRS:X} holds 0x{ptr:X}, note 07 8.2a names 0x{want:X}")
        off = want - IMAGE_BASE
        if off < 0 or off + 6 * (AI_HULL_FACES + 1) > len(image):
            raise DecodeError(f"hull list of part id {pid} at 0x{want:X} leaves the image")
        t = struct.unpack_from(">%dh" % (3 * (AI_HULL_FACES + 1)), image, off)
        for f in range(AI_HULL_FACES):
            a, b, c = t[3 * f:3 * f + 3]
            ok = a >= 0 and all(v >= 0 and v % 6 == 0 and v // 6 <= AI_HULL_SLOT_MAX for v in (a & 0x3FFF, b, c))
            if not ok:
                raise DecodeError(f"hull list of part id {pid} at 0x{want:X}: face {f} ({a & 0xFFFF:#x}, {b & 0xFFFF:#x}, {c & 0xFFFF:#x}) "
                                  f"is not vertex offsets of slots 0 to {AI_HULL_SLOT_MAX}")
            words += [a, b, c]
        if t[3 * AI_HULL_FACES] >= 0:
            raise DecodeError(f"hull list of part id {pid} at 0x{want:X}: no terminator (first word with bit 15 set) after {AI_HULL_FACES} faces")
        words += [t[3 * AI_HULL_FACES], 0, 0]
    return struct.pack("<HH", len(words) // 3, 6) + struct.pack("<%dh" % len(words), *words)


def decode_ai(image: bytes) -> bytes:
    out = b""
    for a in AI_RATE_ADDRS:
        out += _ai_sub(image, a, AI_RATE_ENTRIES, 1, f"rate table 0x{a:X}")
    out += _ai_sub(image, AI_EVASION, AI_EVASION_GROUPS, 8, "evasion table")
    out += _ai_sub(image, AI_OFFSETS, AI_OFFSET_COUNT, 3, "offset table")
    out += _ai_sub(image, AI_RING3, AI_RING3_DIRECTIONS, 3, "ring offsets of class nibble 3")
    out += _ai_sub(image, AI_START, AI_START_ENTRIES, 8, "start blocks")
    out += _ai_sub(image, AI_CAM_POINTS, AI_CAM_POINT_COUNT, 6, "airfield camera points", 8)
    out += _ai_sub(image, AI_FINE, AI_FINE_COUNT, 2, "fine offsets")
    out += _ai_sub(image, AI_PRE_WORD, 1, 1, "word before the rate tables")
    out += decode_runs(image)
    out += decode_hull(image)
    return out


def write_ai(image: bytes, staging) -> None:
    d = Path(staging) / "tables"
    d.mkdir(parents=True, exist_ok=True)
    (d / "ai.bin").write_bytes(decode_ai(image))


# ---- tables/burst.bin (docs/re/07-ai-and-weapons.md section 11.1 and 11.4; extractor version 16, the pens and the cycle from version 18) ----
# Little endian: the magic "BRST", u16 version (2), then
#   72 i16   the eight burst triangles: shape k (k = 0 to 7, the pointer table 0x315A2 entry k) is three vertices of three words (x, y, z), the 18 bytes at 0x315C2 + 18 k
#   36 i8    the fragment offsets 0x31652:  twelve triples (dx, dy, dz) of signed bytes (the base offsets)
#   36 i8    the fragment offsets 0x31679   (twice the base offsets)
#   36 i8    the fragment offsets 0x316A0   (three times the base offsets)
#   27 i8    the nine disc offsets 0x316C7 (the cruise missile's first layer)
#   60 u8    the five pen tables 0x316E5, 0x316F1, 0x316FD, 0x31709, 0x31715, twelve bytes each, as the image has them: fragment j of the layer takes byte j (the shipped tables hold 9; 2; 13 and 10
#            alternating; 9; 9: kind 1, kind 2, the cruise missile's discs, its two layers of triangles; note 07 11.4)
#   32 u16   the shape cycle 0x31722: fragment j at age a draws the shape named by word (j + a) (a byte offset into the pointer table, 4 k for the shape k)
# 4 + 2 + 144 + 108 + 27 + 60 + 64 = 409 bytes. The pointer table is checked against what the note says the port reproduces (the pointers are 0x315C2 + 18 k) and is not exported; the pens must be
# pen numbers (0 to 15) and the cycle words multiples of 4 below 32.
BURST_PTRS = 0x315A2
BURST_SHAPES = 0x315C2
BURST_SHAPE_COUNT = 8
BURST_SHAPE_BYTES = 18
BURST_OFFSETS = (0x31652, 0x31679, 0x316A0)
BURST_DISCS = 0x316C7
BURST_PENS = (0x316E5, 0x316F1, 0x316FD, 0x31709, 0x31715)
BURST_CYCLE = 0x31722
BURST_CYCLE_WORDS = 32
BURST_MAGIC = b"BRST"
BURST_VERSION = 2
BURST_BYTES = 409


def decode_burst(image: bytes) -> bytes:
    def at(addr, n, what):
        off = addr - IMAGE_BASE
        if off < 0 or off + n > len(image):
            raise DecodeError(f"burst table {what} at 0x{addr:X}: needs {n} bytes, image has {len(image)}")
        return off
    po = at(BURST_PTRS, 4 * BURST_SHAPE_COUNT, "pointer table")
    for k in range(BURST_SHAPE_COUNT):
        ptr = struct.unpack_from(">I", image, po + 4 * k)[0]
        if ptr != BURST_SHAPES + BURST_SHAPE_BYTES * k:
            raise DecodeError(f"burst pointer table at 0x{BURST_PTRS:X}: entry {k} holds 0x{ptr:X}, note 07 11.1 puts shape {k} at 0x{BURST_SHAPES + BURST_SHAPE_BYTES * k:X}")
    so = at(BURST_SHAPES, BURST_SHAPE_COUNT * BURST_SHAPE_BYTES, "shapes")
    out = BURST_MAGIC + struct.pack("<H", BURST_VERSION)
    out += struct.pack("<%dh" % (9 * BURST_SHAPE_COUNT), *struct.unpack_from(">%dh" % (9 * BURST_SHAPE_COUNT), image, so))
    for a in BURST_OFFSETS:
        o = at(a, 36, "fragment offsets")
        out += bytes(image[o:o + 36])
    o = at(BURST_DISCS, 27, "disc offsets")
    out += bytes(image[o:o + 27])
    for i, a in enumerate(BURST_PENS):
        o = at(a, 12, "pen table")
        pens = bytes(image[o:o + 12])
        if any(p > 15 for p in pens):
            raise DecodeError(f"burst pen table {i} at 0x{a:X} holds a byte that is not a pen number (0 to 15)")
        out += pens
    co = at(BURST_CYCLE, 2 * BURST_CYCLE_WORDS, "shape cycle")
    cyc = struct.unpack_from(">%dH" % BURST_CYCLE_WORDS, image, co)
    if any(w % 4 or w >= 4 * BURST_SHAPE_COUNT for w in cyc):
        raise DecodeError(f"burst shape cycle at 0x{BURST_CYCLE:X} holds a word that is not the byte offset of a shape (a multiple of 4 below {4 * BURST_SHAPE_COUNT})")
    out += struct.pack("<%dH" % BURST_CYCLE_WORDS, *cyc)
    assert len(out) == BURST_BYTES
    return out


# ---- tables/target_text.bin (note 06 "Target symbols", note 07 section 12.8; extractor version 16) ----
# The strings the target data of the message line draws (the driver 0x36DB2). In the image: the 26-character blank line and its terminator at 0x3717C, the three page labels
# (0x37197, 0x3719C, 0x371A1: altitude, heading, speed; four characters and a terminator) and the eight names (0x371A6 and seven bytes on: six characters and a terminator).
# Little endian: u16 label_count (3), u16 label_chars (4), u16 name_count (8), u16 name_chars (6), then the label characters (3 x 4) and the name characters (8 x 6), no
# terminators. The names are in the image's order: index 0 and 1 the two hostile fighter types (classes 0x12, 0x13), 2 Air Force One (0x14), 3 the F-16 (0x10), 4 the cruise
# missile (0x15), 5 the text of an undetected target, 6 and 7 the two airliners (0x16, 0x17); the class rule is note 07 12.8 (the frontend applies it).
TARGET_BLANK = 0x3717C
TARGET_BLANK_CHARS = 26
TARGET_LABELS = 0x37197
TARGET_LABEL_COUNT = 3
TARGET_LABEL_CHARS = 4
TARGET_NAMES = 0x371A6
TARGET_NAME_COUNT = 8
TARGET_NAME_CHARS = 6
TARGET_TEXT_BYTES = 8 + TARGET_LABEL_COUNT * TARGET_LABEL_CHARS + TARGET_NAME_COUNT * TARGET_NAME_CHARS


def decode_target_text(image: bytes) -> bytes:
    def strings(addr, count, chars, what):
        off = addr - IMAGE_BASE
        step = chars + 1
        if off < 0 or off + count * step > len(image):
            raise DecodeError(f"target text {what} at 0x{addr:X}: needs {count * step} bytes, image has {len(image)}")
        out = b""
        for i in range(count):
            s = bytes(image[off + i * step:off + (i + 1) * step])
            if s[-1] != 0 or 0 in s[:-1]:
                raise DecodeError(f"target text {what} {i} at 0x{addr + i * step:X} is not {chars} characters and a terminator")
            out += s[:-1]
        return out
    blank = strings(TARGET_BLANK, 1, TARGET_BLANK_CHARS, "blank line")
    if blank != b" " * TARGET_BLANK_CHARS:
        raise DecodeError(f"target text blank line at 0x{TARGET_BLANK:X} is not {TARGET_BLANK_CHARS} spaces")
    labels = strings(TARGET_LABELS, TARGET_LABEL_COUNT, TARGET_LABEL_CHARS, "label")
    names = strings(TARGET_NAMES, TARGET_NAME_COUNT, TARGET_NAME_CHARS, "name")
    return struct.pack("<4H", TARGET_LABEL_COUNT, TARGET_LABEL_CHARS, TARGET_NAME_COUNT, TARGET_NAME_CHARS) + labels + names


# ---- tables/prompts.bin (note 06 "Target symbols" and note 07 12.7; extractor version 19) ----
# The two fire prompts the routine 0x36930 draws (row 129, pen 13, from x 204, four pixels a character): it picks the string at 0x36929 when the selection nibble of the viewed record is 1 (the
# gun) and the string at 0x36923 otherwise (a missile), and hands six characters to the text drawer (a space is not drawn). Both are six characters, back to back, and the second ends in a
# terminator at 0x3692F. Little endian: u16 count (2), u16 chars (6), then the characters of index 0 (the missile form, 0x36923) and index 1 (the gun form, 0x36929), no terminators.
PROMPTS = (0x36923, 0x36929)
PROMPT_CHARS = 6
PROMPTS_BYTES = 4 + len(PROMPTS) * PROMPT_CHARS


def decode_prompts(image: bytes) -> bytes:
    out = b""
    for i, addr in enumerate(PROMPTS):
        off = addr - IMAGE_BASE
        if off < 0 or off + PROMPT_CHARS + 1 > len(image):
            raise DecodeError(f"fire prompt {i} at 0x{addr:X}: needs {PROMPT_CHARS + 1} bytes, image has {len(image)}")
        s = bytes(image[off:off + PROMPT_CHARS])
        if any(c < 0x20 or c > 0x7E for c in s):
            raise DecodeError(f"fire prompt {i} at 0x{addr:X} holds a byte that is not a printable character")
        out += s
    if image[PROMPTS[1] - IMAGE_BASE + PROMPT_CHARS] != 0:
        raise DecodeError(f"fire prompt 1 at 0x{PROMPTS[1]:X} is not followed by a terminator")
    return struct.pack("<2H", len(PROMPTS), PROMPT_CHARS) + out


# ---- tables/months.bin (note 08 section 10.9 and C9; extractor version 16) ----
# The four month names of the date line at 0x292CC: 4 x 9 bytes, each stored reversed in the image (the date routine 0x292F0 copies a name backwards into the line, ending at
# text index 18). The file holds the 36 bytes as they read on the line: each name un-reversed, in calendar order, padded to nine characters with spaces (on the right).
MONTHS_ADDR = 0x292CC
MONTH_COUNT = 4
MONTH_CHARS = 9


def decode_months(image: bytes) -> bytes:
    off = MONTHS_ADDR - IMAGE_BASE
    n = MONTH_COUNT * MONTH_CHARS
    if off < 0 or off + n > len(image):
        raise DecodeError(f"month names at 0x{MONTHS_ADDR:X}: need {n} bytes, image has {len(image)}")
    out = b""
    for i in range(MONTH_COUNT):
        name = bytes(image[off + i * MONTH_CHARS:off + (i + 1) * MONTH_CHARS])
        if any(c < 0x20 or c > 0x7E for c in name):
            raise DecodeError(f"month name {i} at 0x{MONTHS_ADDR + i * MONTH_CHARS:X} holds a non-printable byte")
        out += name[::-1]
    return out


# ---- tables/star.bin (note 07 7.7 and 11; extractor version 22) ----
# The flare's star: three line segments of two points of (x, y, z) words around the slot, offsets in position units divided by 256, drawn through the 3D line routine 0x3385C. The table is
# 18 big-endian words at 0x31542. Little endian here: u16 segment count (3), then the 18 i16.
STAR_ADDR = 0x31542
STAR_SEGMENTS = 3
STAR_BYTES = 2 + STAR_SEGMENTS * 2 * 3 * 2
STAR_LIMIT = 64   # a sanity bound on an offset: the star is a few units across


def decode_star(image: bytes) -> bytes:
    off = STAR_ADDR - IMAGE_BASE
    n = STAR_SEGMENTS * 6
    if off < 0 or off + 2 * n > len(image):
        raise DecodeError(f"flare star at 0x{STAR_ADDR:X}: needs {2 * n} bytes, image has {len(image)}")
    words = struct.unpack_from(">%dh" % n, image, off)
    if any(abs(w) > STAR_LIMIT for w in words):
        raise DecodeError(f"flare star at 0x{STAR_ADDR:X} holds an offset beyond +-{STAR_LIMIT}: not the table of note 07 7.7")
    return struct.pack("<H", STAR_SEGMENTS) + struct.pack("<%dh" % n, *words)


# ---- tables/briefing_chain.bin and tables/head_step.bin (extractor version 23) ----
# The briefing chain (note 08 2.1): for each mission mode 3 to 8 (F1 to F6) five text-record ids (briefing 0, briefing 1, the shared notice, the orders, the success
# outcome), big-endian words at 0x43EE6; the rows are five words each, but a stray word (a data word of the neighbouring code) sits between the second and the third row,
# so the rows start at the byte offsets 0, 10, 22, 32, 42 and 52. One byte per id here: 6 x 5 = 30 bytes, mode-major.
CHAIN_ADDR = 0x43EE6
CHAIN_ROW_OFFSETS = (0, 10, 22, 32, 42, 52)
CHAIN_MODES = 6
CHAIN_IDS = 5
CHAIN_BYTES = CHAIN_MODES * CHAIN_IDS
CHAIN_ID_LIMIT = 110   # the text records are the ids 1 to 110 (note 08 2.1)

# The HUD step bytes of the head view codes 0 to 13 (note 06, the table at 0x1E800): 14 signed bytes, in pixel rows divided by 16 (50 = hidden).
HEAD_STEP_ADDR = 0x1E800
HEAD_STEP_BYTES = 14
HEAD_STEP_LIMIT = 64


def decode_briefing_chain(image: bytes) -> bytes:
    base = CHAIN_ADDR - IMAGE_BASE
    if base < 0 or base + CHAIN_ROW_OFFSETS[-1] + 2 * CHAIN_IDS > len(image):
        raise DecodeError(f"briefing chain at 0x{CHAIN_ADDR:X}: needs the bytes up to 0x{CHAIN_ADDR + CHAIN_ROW_OFFSETS[-1] + 2 * CHAIN_IDS:X}, image has {len(image)}")
    out = bytearray()
    for r, ro in enumerate(CHAIN_ROW_OFFSETS):
        for i in range(CHAIN_IDS):
            (w,) = struct.unpack_from(">H", image, base + ro + 2 * i)
            if not 1 <= w <= CHAIN_ID_LIMIT:
                raise DecodeError(f"briefing chain row {r} entry {i} at 0x{CHAIN_ADDR + ro + 2 * i:X} holds {w}, not a text record id (1 to {CHAIN_ID_LIMIT})")
            out.append(w)
    return bytes(out)


def decode_head_step(image: bytes) -> bytes:
    off = HEAD_STEP_ADDR - IMAGE_BASE
    if off < 0 or off + HEAD_STEP_BYTES > len(image):
        raise DecodeError(f"head step table at 0x{HEAD_STEP_ADDR:X}: needs {HEAD_STEP_BYTES} bytes, image has {len(image)}")
    raw = bytes(image[off:off + HEAD_STEP_BYTES])
    if any(abs(b - 256 if b > 127 else b) > HEAD_STEP_LIMIT for b in raw):
        raise DecodeError(f"head step table at 0x{HEAD_STEP_ADDR:X} holds a step beyond +-{HEAD_STEP_LIMIT}: not the table of note 06")
    return raw


def write_burst(image: bytes, staging) -> None:
    d = Path(staging) / "tables"
    d.mkdir(parents=True, exist_ok=True)
    (d / "burst.bin").write_bytes(decode_burst(image))
    (d / "target_text.bin").write_bytes(decode_target_text(image))
    (d / "months.bin").write_bytes(decode_months(image))
    (d / "prompts.bin").write_bytes(decode_prompts(image))
    (d / "star.bin").write_bytes(decode_star(image))
    (d / "briefing_chain.bin").write_bytes(decode_briefing_chain(image))
    (d / "head_step.bin").write_bytes(decode_head_step(image))
