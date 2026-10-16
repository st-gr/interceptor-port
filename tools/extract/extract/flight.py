"""Tables the flight step reads (docs/re/05-flight-model.md, "Tables in the image" and
"Extractor needs"). Pure functions over the relocated flat image; ``off`` converts a flat address.

File formats written to assets/tables/ (little-endian int16 words unless stated):

  flight_stick_player.bin   62 words   0x425D8  stick response of the player, by command index
  flight_stick_ai.bin       64 words   0x42654  the same for other entities
  flight_gust.bin           31 words   0x2AB18  gust target by speed index
  flight_sink.bin           31 words   0x2AAD8  sink term by speed index
  flight_span_pitch.bin    286 bytes   big endian, raw image bytes 0x3249A..0x325B8: the 143 words in front of and
                                                 including the bank-turn pitch scale (15 words at 0x3259A). The scale
                                                 index is the signed pitch word >> 8, so a negative pitch word reads
                                                 the words before the table (note 05 17.9). Three relocated address
                                                 longs (0x32540, 0x32560, 0x32568) carry their reference-machine value
  flight_span_trig.bin   60288 bytes   big endian, raw image bytes 0x3AE92..0x49A12: the sine table (0x436E8, 901
                                                 words), the three inverse-sine tables (0x42E92, 0x43098, 0x434E8)
                                                 and everything the matrix builders, the gust rule and the composition
                                                 can read around them with an angle word outside 0..28799 (note 05
                                                 17.9) and the composition's heading and roll lookups, whose byte offset is the
                                                 signed 16-bit value of 2|q| (0x3AE92..0x4AE91, note 05 17.18 a; the reads
                                                 above 0x49A12 are answered 0). The port answers those reads by address from
                                                 these bytes. 337 relocated address longs carry their reference-machine value (below).

The relocated longs of the two spans (note 05 17.9, "the span rule"): the loader rewrote them to RAM addresses on the reference
machine (the Kickstart 1.2 slow-RAM layout), and the original read those values. The value of a long that names flat address F in hunk H
is the RAM base of H plus (F minus the flat start of H), i.e. the file's value plus the RAM base of the target hunk. The RAM bases are the
loader's segment table of the reference machine (lines "seg N flat F ram R ...": addresses and sizes only; the package ships it as
extract/segments.txt, the same text as tools/extract-js/segments.txt, and a test keeps the two equal); the extractor takes it by
default, or from --segments. With --allow-flat-spans and no table (the synthetic disk) the longs keep their flat
values and the extractor says so: a junk read that lands on one then differs from the original (6 of 7200 junk steps).
  flight_len.bin           257 words   0x208E8  vector length helper, Q14
  flight_terrain.bin      2886 words   0x47F40  terrain grid (32 x 32 words) followed by the cell records
                                                 (0x48740-0x495CB, 3724 bytes); grid words are byte offsets from
                                                 the file start
  flight_dir.bin            32 words   0x206F2  16 direction offset pairs
  flight_pads.bin          see below   0x47B72  airfield pads with their quad lists
  flight_carrier.bin       852 bytes   big endian, raw: list 0x3E0F8 (124) + list 0x3E174 (44) +
                                                 structures 0x3E6AE-0x3E95A (684)
  flight_ship2.bin         438 bytes   big endian, raw: list 0x3EDCC (32) + list 0x3EDEC (44) +
                                                 structures 0x3F2B6-0x3F420 (362)

flight_pads.bin: five 8-word entries (the four pads and the terminator, 40 words), then for each of the four
entries a word count n (0 for an entry whose cell x is negative) followed by n words: the quad list that the
entry's descriptor names (the long at 0x2600C + word0 + 0x10), copied up to and including its 0xFFFF word.
"""
import re
import struct

from .hunk import parse as parse_hunks
from .tables import DecodeError, Mem

WORD_TABLES = {
    # name: (address, words)
    "flight_stick_player": (0x425D8, 62),
    "flight_stick_ai": (0x42654, 64),
    "flight_gust": (0x2AB18, 31),
    "flight_sink": (0x2AAD8, 31),
    "flight_len": (0x208E8, 257),
    "flight_terrain": (0x47F40, 32 * 32 + 3724 // 2),
    "flight_dir": (0x206F2, 32),
}
PAD_TABLE = 0x47B72
PAD_ENTRIES = 5
DESCRIPTOR_BASE = 0x2600C
SHIP_BLOBS = {
    # name: [(address, bytes), ...]
    "flight_carrier": [(0x3E0F8, 124), (0x3E174, 44), (0x3E6AE, 0x3E95A - 0x3E6AE)],
    "flight_ship2": [(0x3EDCC, 32), (0x3EDEC, 44), (0x3F2B6, 0x3F420 - 0x3F2B6)],
}
# Image spans the port reads by address (note 05 17.9, 17.18 a: the first byte read by the composition's lookups is 0x3AE92, the builders' first
# byte is 0x4130A; the span ends where the zero fill of hunk 70 begins, 0x49A12). Sizes are checked.
SPANS = {"flight_span_pitch": (0x3249A, 0x325B8), "flight_span_trig": (0x3AE92, 0x49A12)}
SPAN_SIZES = {"flight_span_pitch": 286, "flight_span_trig": 60288}
SHIP_SIZES = {"flight_carrier": 852, "flight_ship2": 438}
PAD_QUAD_LIMIT = 64    # words; the longest list in the image is 33


def _words(mem: Mem, addr: int, n: int, name: str):
    if not mem.has(addr, 2 * n):
        raise DecodeError(f"{name}: {n} words at 0x{addr:X} lie outside the image")
    return [mem.s16(addr + 2 * i) for i in range(n)]


def words_bytes(words) -> bytes:
    return struct.pack("<%dh" % len(words), *words)


def decode_word_table(mem: Mem, name: str) -> bytes:
    addr, n = WORD_TABLES[name]
    out = words_bytes(_words(mem, addr, n, name))
    if len(out) != 2 * n:
        raise DecodeError(f"{name}: expected {2 * n} bytes, got {len(out)}")
    return out


def decode_pads(mem: Mem) -> bytes:
    entries = [_words(mem, PAD_TABLE + 16 * i, 8, "airfield pads") for i in range(PAD_ENTRIES)]
    if entries[PAD_ENTRIES - 1][1] != -1:
        raise DecodeError("airfield pads: the fifth entry is not the terminator (cell x -1)")
    words = [w for e in entries for w in e]
    for i in range(PAD_ENTRIES - 1):
        e = entries[i]
        if e[1] < 0:
            words.append(0)
            continue
        ptr = mem.u32(DESCRIPTOR_BASE + e[0] + 0x10)
        quads = []
        for k in range(PAD_QUAD_LIMIT):
            w = mem.s16(ptr + 2 * k)
            quads.append(w)
            if w == -1:
                break
        else:
            raise DecodeError(f"airfield pads: quad list of entry {i} at 0x{ptr:X} has no terminator")
        if (len(quads) - 1) % 8:
            raise DecodeError(f"airfield pads: quad list of entry {i} is not a whole number of quads")
        words.append(len(quads))
        words.extend(quads)
    return words_bytes(words)


def decode_span(mem: Mem, name: str, reference_longs=()) -> bytes:
    """The span's image bytes with the reference-machine value written over every relocated long that lies wholly inside it."""
    lo, hi = SPANS[name]
    if not mem.has(lo, hi - lo):
        raise DecodeError(f"{name}: 0x{lo:X}..0x{hi:X} lies outside the image")
    out = bytearray(mem.raw(lo, hi - lo))
    if len(out) != SPAN_SIZES[name]:
        raise DecodeError(f"{name}: expected {SPAN_SIZES[name]} bytes, got {len(out)}")
    for addr, value in reference_longs:
        if lo <= addr and addr + 4 <= hi:
            out[addr - lo:addr - lo + 4] = struct.pack(">I", value)
    return bytes(out)


SEG_LINE = re.compile(r"^seg (\d+) flat ([0-9A-Fa-f]+) ram ([0-9A-Fa-f]+) ")


def parse_segment_table(text: str):
    """{hunk index: (flat start, RAM base)} from the loader log (lines "seg N flat F ram R size ...")."""
    segs = {}
    for line in text.splitlines():
        m = SEG_LINE.match(line)
        if m:
            segs[int(m.group(1))] = (int(m.group(2), 16), int(m.group(3), 16))
    return segs


def reference_longs(exe: bytes, base: int, segs):
    """[(flat address, reference-machine value)] for every relocated long of the executable that lies in a junk span: the file's value
    plus the RAM base of the target hunk (note 05 17.9). segs is parse_segment_table's result; the flat starts it names must agree with the
    layout from ``base`` (a mismatch means the log is of another executable)."""
    sizes, _types, contents, relocs, _syms = parse_hunks(exe)
    flat, a = [], base
    for sz in sizes:
        flat.append(a)
        a += sz
    for i, (f, _ram) in segs.items():
        if i < len(flat) and f != flat[i]:
            raise DecodeError(f"segment table: hunk {i} starts at 0x{f:X} there, 0x{flat[i]:X} here (another executable?)")
    if len(segs) < len(sizes):
        raise DecodeError(f"segment table names {len(segs)} hunks, the executable has {len(sizes)}")
    out = []
    for i, rl in enumerate(relocs):
        for off, tgt in rl:
            addr = flat[i] + off
            if not any(lo <= addr and addr + 4 <= hi for lo, hi in SPANS.values()):
                continue
            v = struct.unpack_from(">I", contents[i], off)[0] if off + 4 <= len(contents[i]) else 0
            out.append((addr, (v + segs[tgt][1]) & 0xFFFFFFFF))
    return out


def decode_ship_blob(mem: Mem, name: str) -> bytes:
    out = b"".join(mem.raw(a, n) for a, n in SHIP_BLOBS[name])
    if len(out) != SHIP_SIZES[name]:
        raise DecodeError(f"{name}: expected {SHIP_SIZES[name]} bytes, got {len(out)}")
    # every pointer of the first list names a structure inside the structure block
    l1, _l2, st = SHIP_BLOBS[name]
    lists = [(SHIP_BLOBS[name][0][0], SHIP_BLOBS[name][0][1]), (SHIP_BLOBS[name][1][0], SHIP_BLOBS[name][1][1])]
    for base, n in lists:
        pos = 0
        while pos < n:
            w = mem.u16(base + pos)
            if w == 0xFFFF:
                pos += 2
                continue
            p = mem.u32(base + pos)
            if not (st[0] <= p < st[0] + st[1]):
                raise DecodeError(f"{name}: pointer 0x{p:X} at 0x{base + pos:X} is outside the structures")
            pos += 4
    return out


def decode_all(image: bytes, base: int, longs=()):
    """{file name: bytes} for every flight table. longs: reference_longs(...) of the executable, or empty (the spans then keep flat values)."""
    mem = Mem(image, base, "flight tables")
    out = {f"{n}.bin": decode_word_table(mem, n) for n in WORD_TABLES}
    out["flight_pads.bin"] = decode_pads(mem)
    for n in SPANS:
        out[f"{n}.bin"] = decode_span(mem, n, longs)
    for n in SHIP_BLOBS:
        out[f"{n}.bin"] = decode_ship_blob(mem, n)
    return out
