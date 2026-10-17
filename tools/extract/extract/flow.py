"""Game-flow tables: the menu and flash palettes and the scenario tables
(docs/re/02-assets-and-tables.md sections 9 and 11; docs/re/05-flight-model.md 17.14 and 17.17 rule 5).

Everything is little endian in the output. Each sub-table of tables/scenario.bin is prefixed
by ``u16 count, u16 record_size``. Fields the notes leave open are carried as raw words."""
import struct

from .palette import palette_bytes
from .tables import DecodeError, IMAGE_BASE

MENU_PALETTE = 0x29F24
FLASH_PALETTE = 0x29F64
PALETTE_ENTRIES = 32

START_BLOCKS = 0x47ADE        # 16-byte entries of 8 words, first word -1 ends the table (note 05 17.14)
START_RECORD = 16
# Caps of the core's scenario_t (src/core/assets.h): the extractor refuses what the core could not hold.
START_MAX = 8
REGION_ENTRIES_MAX = 16
ROWS_MAX = 16
LISTS_MAX = 16
SUBLISTS_MAX = 48
SUB_ENTRIES_MAX = 160
REGION_TABLE = 0x2DE28        # 4 big-endian 32-bit absolute pointers to the regions
REGION_COUNT = 4
PLACEMENT_TABLE = 0x2DEDA     # 12-byte rows: mode 1..9 -> rows 0..8, 0x7E/0x7F -> row 9
PLACEMENT_ROWS = 10
PLACEMENT_VARIANTS = 3
LIST_HEADS = 4                # a list opens with 4 offset words (from the list start) of sub-lists
ENTRY_WORDS = 5               # a sub-list entry or a region entry: 5 words
ENTRY_SIZE = 2 * ENTRY_WORDS


def _words(image, addr, n, what, signed=False):
    off = addr - IMAGE_BASE
    if off < 0 or off + 2 * n > len(image):
        raise DecodeError(f"{what} at 0x{addr:X}: needs {2 * n} bytes, image has {len(image)}")
    return list(struct.unpack_from(">" + ("h" if signed else "H") * n, image, off))


def palette_words(image: bytes, addr: int, entries: int = PALETTE_ENTRIES):
    """``entries`` colour words 0x0RGB at flat address ``addr``; channels scaled to 8 bits by 17
    (the scaling of palette.decode_fade_table). Returns the 3 * entries bytes."""
    out = []
    for i, w in enumerate(_words(image, addr, entries, "palette")):
        if w > 0x0FFF:
            raise DecodeError(f"palette at 0x{addr:X}: entry {i} is 0x{w:04X}, not a 12-bit colour")
        out.append((((w >> 8) & 15) * 17, ((w >> 4) & 15) * 17, (w & 15) * 17))
    return palette_bytes(out)


def write_palettes(image: bytes, staging) -> None:
    """palette/menu.pal (0x29F24) and palette/flash.pal (0x29F64): 32 entries x u8 r, g, b."""
    from pathlib import Path
    d = Path(staging) / "palette"
    d.mkdir(parents=True, exist_ok=True)
    (d / "menu.pal").write_bytes(palette_words(image, MENU_PALETTE))
    (d / "flash.pal").write_bytes(palette_words(image, FLASH_PALETTE))


def _cap(what: str, n: int, cap: int) -> None:
    if n > cap:
        raise DecodeError(f"scenario {what}: {n} found, the core holds at most {cap}")


def _section(count: int, record_size: int, body: bytes) -> bytes:
    assert len(body) == count * record_size
    return struct.pack("<HH", count, record_size) + body


def decode_scenario(image: bytes) -> bytes:
    """tables/scenario.bin: eight sub-tables in this order, each ``u16 count, u16 record_size`` then
    count records of record_size bytes, every field a little-endian word.

    1. start blocks (16 bytes: 8 i16 words cell_x, cell_z, n, ox, oz, fx, fz, degrees; the
       terminating -1 block is not stored; a carrier-type block has the first word 0x8000 | record
       and zeros after it).
    2. spawn regions (14 bytes: 7 u16 words: box x0, x1, z0, z1 (the order of the four words is a port
       reading until the note states it), count word (raw; it is not the entry count, meaning open),
       first entry, entry count into table 3). The entry walk ends at a first word with bit 15 set
       (the terminator is not stored).
    3. region entries (10 bytes: 5 u16 words: record byte offset (20 * id), class, slot and flags,
       position-block reference, flags).
    4. placement rows (12 bytes: 3 variants x (u16 list index into table 5, u16 count word)).
    5. lists (8 bytes: 4 u16 sub-list indices into table 6).
    6. sub-lists (4 bytes: u16 first entry, u16 entry count into table 7; the walk ends at a first word
       0xFFFF, not stored) and
    7. sub-list entries (10 bytes: 5 u16 words: template offset into 0x25ECC, class, record index in
       the low 7 bits with 0x8000 a flag, position-block reference or further word, further word; raw).
    8. the mode-0x7D data: empty (count 0, record size 0), see the README.
    """
    def W(addr, n):
        return _words(image, addr, n, "scenario")

    # 1. start blocks
    blocks = []
    for i in range(START_MAX + 1):
        w = tuple(_words(image, START_BLOCKS + START_RECORD * i, 8, "start blocks", signed=True))
        if w[0] == -1:
            break
        blocks.append(w)
    else:
        raise DecodeError(f"start blocks at 0x{START_BLOCKS:X}: no terminator within {START_MAX} blocks (the core holds at most that many)")
    if not blocks:
        raise DecodeError(f"start blocks at 0x{START_BLOCKS:X}: empty table")
    _cap("start blocks", len(blocks), START_MAX)
    s1 = _section(len(blocks), START_RECORD, b"".join(struct.pack("<8h", *b) for b in blocks))

    # 2, 3. regions
    regions, entries = [], []
    for r in range(REGION_COUNT):
        hi, lo = W(REGION_TABLE + 4 * r, 2)
        a = (hi << 16) | lo
        if not IMAGE_BASE <= a < IMAGE_BASE + len(image) - 10:
            raise DecodeError(f"region {r}: pointer 0x{a:X} at 0x{REGION_TABLE + 4 * r:X} is outside the image")
        box = W(a, 4)
        (cnt,) = W(a + 8, 1)
        first, q = len(entries), a + 10
        for _ in range(REGION_ENTRIES_MAX + 1):
            e = W(q, ENTRY_WORDS)
            if e[0] & 0x8000:
                break
            entries.append(e)
            q += ENTRY_SIZE
        else:
            raise DecodeError(f"region {r} at 0x{a:X}: no terminator within {REGION_ENTRIES_MAX} entries")
        regions.append((*box, cnt, first, len(entries) - first))
    _cap("region entries", len(entries), REGION_ENTRIES_MAX)
    s2 = _section(len(regions), 14, b"".join(struct.pack("<7H", *x) for x in regions))
    s3 = _section(len(entries), ENTRY_SIZE, b"".join(struct.pack("<5H", *e) for e in entries))

    # 4-7. placement rows, lists, sub-lists
    rows_raw = [W(PLACEMENT_TABLE + 12 * i, 6) for i in range(PLACEMENT_ROWS)]
    list_offs = sorted({o for row in rows_raw for o in row[0::2]})
    sub_addr, sub_entries, subs = {}, [], []
    lists = []
    for lo in list_offs:
        la = PLACEMENT_TABLE + lo
        heads = W(la, LIST_HEADS)
        idx = []
        for h in heads:
            sa = la + h
            if sa not in sub_addr:
                first, q = len(sub_entries), sa
                for _ in range(SUB_ENTRIES_MAX + 1):
                    e = W(q, ENTRY_WORDS)
                    if e[0] == 0xFFFF:
                        break
                    sub_entries.append(e)
                    q += ENTRY_SIZE
                else:
                    raise DecodeError(f"placement sub-list at 0x{sa:X}: no terminator")
                sub_addr[sa] = len(subs)
                subs.append((first, len(sub_entries) - first))
            idx.append(sub_addr[sa])
        lists.append(idx)
    rows = []
    for row in rows_raw:
        rec = []
        for o, c in zip(row[0::2], row[1::2]):
            rec += [list_offs.index(o), c]
        rows.append(rec)
    for what, n, cap in (("placement rows", len(rows), ROWS_MAX), ("placement lists", len(lists), LISTS_MAX),
                         ("placement sub-lists", len(subs), SUBLISTS_MAX), ("placement sub-list entries", len(sub_entries), SUB_ENTRIES_MAX)):
        _cap(what, n, cap)
    s4 = _section(len(rows), 12, b"".join(struct.pack("<6H", *x) for x in rows))
    s5 = _section(len(lists), 8, b"".join(struct.pack("<4H", *x) for x in lists))
    s6 = _section(len(subs), 4, b"".join(struct.pack("<2H", *x) for x in subs))
    s7 = _section(len(sub_entries), ENTRY_SIZE, b"".join(struct.pack("<5H", *e) for e in sub_entries))
    s8 = _section(0, 0, b"")
    return s1 + s2 + s3 + s4 + s5 + s6 + s7 + s8


def write_scenario(image: bytes, staging) -> None:
    from pathlib import Path
    d = Path(staging) / "tables"
    d.mkdir(parents=True, exist_ok=True)
    (d / "scenario.bin").write_bytes(decode_scenario(image))


# ---- text records and the menu font (docs/re/08-game-flow.md sections 2 and 3.2) ----
TEXT_ID_TABLE = 0x43DFE        # 110 words: byte offset from this address of the record of id 1..110
TEXT_ID_COUNT = 110
TEXT_RECORDS = 0x43F24         # first record
TEXT_END = 0x4615A             # the column table follows; no record or text byte reaches it
TEXT_NOT_RECORDS = (18, 19, 21, 25)   # ids that name no record (note 08 section 2.1): stored with no lines
TEXT_LINE_MAX_CHARS = 45
TEXT_FLAGS = (0, 1, 3)
TEXT_DELAY_MAX = 120
TEXT_LINES_MAX = 400            # caps of the core's text_t (src/core/assets.h)
TEXT_CHARS_MAX = 8192           # text bytes plus one NUL per line
TEXT_ROW_LIMIT = 200
TEXT_WIDTH = 320

FONT_TABLE = 0x4283C           # 64 words: byte offset from this address of the glyph of character 0x20 + i
FONT_CHARS = 64
FONT_GLYPH = 7
FONT_OFFSET_MIN = 0x80
FONT_OFFSET_MAX = 0x1F3
FONT_BITMAPS = 0x428BC         # glyph bitmaps 0x428BC-0x42A36


def _decode_record(image: bytes, addr: int, rid: int):
    """Decodes the record at flat address ``addr`` (named by id ``rid`` in refusals). Returns the list of lines
    (row, col, flags, colour, delay, text bytes); [] for a record with no lines (its header is never drawn)."""
    def need(a, n=1):
        if a < TEXT_RECORDS or a + n > TEXT_END:
            raise DecodeError(f"text record of id {rid} at 0x{addr:X}: runs outside the record span 0x{TEXT_RECORDS:X}-0x{TEXT_END:X} at 0x{a:X}")
        return a - IMAGE_BASE
    if addr - IMAGE_BASE + 2 > len(image) or TEXT_END - IMAGE_BASE > len(image):
        raise DecodeError(f"text record of id {rid} at 0x{addr:X}: outside the image")
    row, col = image[need(addr, 2)], image[need(addr + 1)]
    a, lines = addr + 2, []
    while True:
        flags = image[need(a)]
        a += 1
        if flags & 0x80:
            break
        if flags not in TEXT_FLAGS:
            raise DecodeError(f"text record of id {rid} at 0x{addr:X}: line {len(lines)} has flags 0x{flags:02X}, expected one of {TEXT_FLAGS}")
        colour = image[need(a)]
        a += 1
        if flags & 1 and colour & 15 < 2:
            raise DecodeError(f"text record of id {rid} at 0x{addr:X}: typed line {len(lines)} has delay {colour & 15} (colour 0x{colour:02X}); the engine never advances below 2")
        text = bytearray()
        while True:
            b = image[need(a)]
            a += 1
            if b == 0:
                break
            if not 0x20 <= b <= 0x5F:
                raise DecodeError(f"text record of id {rid} at 0x{addr:X}: line {len(lines)} has text byte 0x{b:02X} outside 0x20-0x5F")
            text.append(b)
            if len(text) > TEXT_LINE_MAX_CHARS:
                raise DecodeError(f"text record of id {rid} at 0x{addr:X}: line {len(lines)} is longer than {TEXT_LINE_MAX_CHARS} characters")
        delay = image[need(a)]
        a += 1
        if delay > TEXT_DELAY_MAX:
            raise DecodeError(f"text record of id {rid} at 0x{addr:X}: line {len(lines)} has delay {delay}, above {TEXT_DELAY_MAX}")
        lines.append((row, col, flags, colour, delay, bytes(text)))
        if len(lines) > 255:
            raise DecodeError(f"text record of id {rid} at 0x{addr:X}: more than 255 lines")
    if lines:
        if row >= TEXT_ROW_LIMIT:
            raise DecodeError(f"text record of id {rid} at 0x{addr:X}: row {row} is not a scanline below {TEXT_ROW_LIMIT}")
        if col & 1:
            raise DecodeError(f"text record of id {rid} at 0x{addr:X}: column {col} is odd")
        for k, ln in enumerate(lines):
            n = len(ln[5])
            if n and 8 * col + 7 * (n - 1) + 5 > TEXT_WIDTH:
                raise DecodeError(f"text record of id {rid} at 0x{addr:X}: line {k} ({n} characters from column {col}) is wider than {TEXT_WIDTH} pixels")
    return lines


def decode_text(image: bytes):
    """Returns (text.bin, text_ids.bin), little endian, per note 08 section 2.3.

    text.bin: ``u16 line_count``, then per line ``u16 first_id`` (the lowest id naming the record), ``u8 line``
    (index within the record), ``u8 row``, ``u8 col``, ``u8 flags``, ``u8 colour``, ``u8 delay``, ``u16 length``,
    ``length`` text bytes (no terminator). text_ids.bin: ``u16 id_count`` (110), then per id ``u16 id``,
    ``u16 first_line`` (index into text.bin), ``u16 line_count`` (0 for the empty records and the four ids that
    name no record, whose first_line is 0). Ids sharing a record carry the same first_line."""
    offs = _words(image, TEXT_ID_TABLE, TEXT_ID_COUNT, "text id table")
    records = {}                     # record address -> (lowest id, lines)
    for i, off in enumerate(offs):
        rid = i + 1
        if rid in TEXT_NOT_RECORDS:
            continue
        addr = TEXT_ID_TABLE + off
        if addr not in records:
            records[addr] = (rid, _decode_record(image, addr, rid))
    placed, body, nlines = {}, bytearray(), 0
    for addr, (rid, lines) in sorted(records.items(), key=lambda kv: kv[1][0]):
        placed[addr] = (nlines, len(lines))
        for k, (row, col, flags, colour, delay, text) in enumerate(lines):
            body += struct.pack("<HBBBBBBH", rid, k, row, col, flags, colour, delay, len(text)) + text
            nlines += 1
    chars = sum(len(ln[5]) + 1 for _, ls in records.values() for ln in ls)
    if nlines > TEXT_LINES_MAX or chars > TEXT_CHARS_MAX:
        raise DecodeError(f"text: {nlines} lines and {chars} text bytes (with one NUL per line); the core holds at most {TEXT_LINES_MAX} and {TEXT_CHARS_MAX}")
    ids = bytearray(struct.pack("<H", TEXT_ID_COUNT))
    for i, off in enumerate(offs):
        rid = i + 1
        first, n = (0, 0) if rid in TEXT_NOT_RECORDS else placed[TEXT_ID_TABLE + off]
        ids += struct.pack("<HHH", rid, first if n else 0, n)
    return struct.pack("<H", nlines) + bytes(body), bytes(ids)


def write_text(image: bytes, staging) -> None:
    from pathlib import Path
    d = Path(staging) / "tables"
    d.mkdir(parents=True, exist_ok=True)
    text, ids = decode_text(image)
    (d / "text.bin").write_bytes(text)
    (d / "text_ids.bin").write_bytes(ids)


def decode_menu_font(image: bytes) -> bytes:
    """fonts/menu5x7.bin: 64 glyphs x 7 bytes, glyph i the one of character 0x20 + i (the offset table followed)."""
    offs = _words(image, FONT_TABLE, FONT_CHARS, "menu font offset table")
    if offs[0] != FONT_OFFSET_MIN:
        raise DecodeError(f"menu font: first offset is 0x{offs[0]:04X}, expected 0x{FONT_OFFSET_MIN:04X} (character 0x20)")
    out = bytearray()
    for i, o in enumerate(offs):
        c = 0x20 + i
        if not FONT_OFFSET_MIN <= o <= FONT_OFFSET_MAX:
            raise DecodeError(f"menu font: character 0x{c:02X} has offset 0x{o:04X}, outside the glyph bitmaps 0x{FONT_BITMAPS:X}-0x{FONT_TABLE + FONT_OFFSET_MAX + FONT_GLYPH - 1:X}"
                              f" (offsets 0x{FONT_OFFSET_MIN:04X}-0x{FONT_OFFSET_MAX:04X})")
        g = image[FONT_TABLE + o - IMAGE_BASE:FONT_TABLE + o - IMAGE_BASE + FONT_GLYPH]
        if len(g) != FONT_GLYPH:
            raise DecodeError(f"menu font: glyph of character 0x{c:02X} at 0x{FONT_TABLE + o:X} is outside the image")
        if any(b & 7 for b in g):
            raise DecodeError(f"menu font: glyph of character 0x{c:02X} has bits set in the low three bits of a row")
        if o == FONT_OFFSET_MIN and any(g):
            raise DecodeError(f"menu font: the glyph at offset 0x{o:04X} (character 0x{c:02X}) is not blank")
        out += g
    return bytes(out)


def write_menu_font(image: bytes, staging) -> None:
    from pathlib import Path
    d = Path(staging) / "fonts"
    d.mkdir(parents=True, exist_ok=True)
    (d / "menu5x7.bin").write_bytes(decode_menu_font(image))


# ---- the demo replay stream and the training tables (docs/re/08-game-flow.md 7.1 and 8.13) ----
DEMO_FRAMES = 3676
DEMO_FILES = ("textply", "textctl")      # the disk's text/ files that are the stream, not samples
DEMO_MAX_FRAMES = 4096                   # cap of the core's demo_t (src/core/assets.h)
TRAINING_A = 0x2749A                     # 8 rows of 8 bytes: i16 prompt id, i16 name id, i32 unused
TRAINING_B = 0x274E2                     # 8 rows of 8 bytes: i32 camera, i32 script
TRAINING_ROWS = 8
PITCH_RATES = 0x3132C                    # 11 i16 each
ROLL_RATES = 0x31342
RATE_ENTRIES = 11


def decode_demo(textply: bytes, textctl: bytes) -> bytes:
    """tables/demo.bin: ``u32 frame_count`` (3676), the key bytes, then per frame ``u16 word1, u16 word2``
    (little endian; the files' words are big endian). The port plays frames 0 to 2045 only."""
    if len(textply) != DEMO_FRAMES:
        raise DecodeError(f"text/textply: {len(textply)} bytes, expected {DEMO_FRAMES}")
    if len(textctl) != 4 * DEMO_FRAMES:
        raise DecodeError(f"text/textctl: {len(textctl)} bytes, expected {4 * DEMO_FRAMES} ({DEMO_FRAMES} x 4)")
    words = struct.unpack(">%dH" % (2 * DEMO_FRAMES), textctl)
    return struct.pack("<I", DEMO_FRAMES) + bytes(textply) + struct.pack("<%dH" % (2 * DEMO_FRAMES), *words)


def write_demo(textply: bytes, textctl: bytes, staging) -> None:
    from pathlib import Path
    d = Path(staging) / "tables"
    d.mkdir(parents=True, exist_ok=True)
    (d / "demo.bin").write_bytes(decode_demo(textply, textctl))


def decode_training(image: bytes) -> bytes:
    """tables/training.bin, little endian, four sub-tables each prefixed ``u16 count, u16 record_size``:
    A (8 rows of 8 bytes: i16 prompt_id, i16 name_id, i32 unused), B (8 rows of 8: i32 camera, i32 script),
    pitch rates (11 x i16, record size 2), roll rates (11 x i16, record size 2). The script long must have
    bit 31 set (a manoeuvre state; a pointer to a recorded script is not reachable and is refused)."""
    a = []
    for r in range(TRAINING_ROWS):
        off = TRAINING_A + 8 * r - IMAGE_BASE
        if off < 0 or off + 8 > len(image):
            raise DecodeError(f"training table A row {r} at 0x{TRAINING_A + 8 * r:X}: outside the image")
        a.append(struct.unpack_from(">hhi", image, off))
    b = []
    for r in range(TRAINING_ROWS):
        off = TRAINING_B + 8 * r - IMAGE_BASE
        if off < 0 or off + 8 > len(image):
            raise DecodeError(f"training table B row {r} at 0x{TRAINING_B + 8 * r:X}: outside the image")
        cam, script = struct.unpack_from(">ii", image, off)
        if script >= 0:
            raise DecodeError(f"training table B row {r} at 0x{TRAINING_B + 8 * r:X}: script long 0x{script:08X} has bit 31 clear (a script pointer; not supported)")
        b.append((cam, script))
    out = struct.pack("<HH", TRAINING_ROWS, 8) + b"".join(struct.pack("<hhi", *x) for x in a)
    out += struct.pack("<HH", TRAINING_ROWS, 8) + b"".join(struct.pack("<ii", *x) for x in b)
    for addr, what in ((PITCH_RATES, "pitch"), (ROLL_RATES, "roll")):
        out += struct.pack("<HH", RATE_ENTRIES, 2) + struct.pack("<%dh" % RATE_ENTRIES, *_words(image, addr, RATE_ENTRIES, f"training {what} rates", signed=True))
    return out


def write_training(image: bytes, staging) -> None:
    from pathlib import Path
    d = Path(staging) / "tables"
    d.mkdir(parents=True, exist_ok=True)
    (d / "training.bin").write_bytes(decode_training(image))
