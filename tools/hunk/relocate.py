#!/usr/bin/env python3
"""relocate.py <hunk file> <out.bin> <out.map.json> [--base 0x10000]

Lays every hunk of an AmigaDOS LoadSeg executable out sequentially from a
base address, applies the 32-bit relocations, and writes the flat image plus
a JSON map of hunk addresses and the HUNK_SYMBOL link-time symbols
(absolute addresses, sorted). Parses the hunk file itself (amitools drops relocation runs). The output is a
derivative of copyrighted code and belongs in the git-ignored extracted/.
"""
import argparse
import json
import struct
import sys

HUNK_NAME, HUNK_CODE, HUNK_DATA, HUNK_BSS = 0x3E8, 0x3E9, 0x3EA, 0x3EB
HUNK_RELOC32, HUNK_SYMBOL, HUNK_DEBUG, HUNK_END, HUNK_HEADER = 0x3EC, 0x3F0, 0x3F1, 0x3F2, 0x3F3
HUNK_DREL32, HUNK_RELOC32SHORT, HUNK_RELRELOC32 = 0x3F7, 0x3FC, 0x3FD
TYPE_NAMES = {HUNK_CODE: "CODE", HUNK_DATA: "DATA", HUNK_BSS: "BSS"}
MASK = 0x3FFFFFFF  # top two bits of size/type words are memory flags


def parse(data: bytes):
    """Parse a hunk executable directly. Returns (sizes, types, contents,
    relocs, symbols): per hunk the allocated size in bytes, the type name, the
    file data, a list of (offset, target hunk) and a list of (name, offset).
    Every run of every relocation block is kept (a block may list the same
    target hunk more than once)."""
    n = len(data)

    def u32(p):
        if p + 4 > n:
            raise ValueError("truncated hunk file")
        return struct.unpack_from(">I", data, p)[0]

    def u16(p):
        if p + 2 > n:
            raise ValueError("truncated hunk file")
        return struct.unpack_from(">H", data, p)[0]

    def flagged(w):
        if w >> 30 == 3:
            raise ValueError("unsupported 0xC0000000 memory-flag extension (extra flags longword)")
        return w & MASK

    if n < 4 or u32(0) != HUNK_HEADER:
        raise ValueError("not a valid hunk file: no HUNK_HEADER")
    p = 4
    while u32(p) != 0:                      # resident library names
        p += 4 + 4 * u32(p)
    p += 4
    table, first, last = u32(p), u32(p + 4), u32(p + 8)
    p += 12
    count = last - first + 1
    if first != 0 or count <= 0 or count > table:
        raise ValueError("unsupported hunk table")
    sizes = [4 * flagged(u32(p + 4 * i)) for i in range(count)]
    p += 4 * count
    types, contents, relocs, symbols = [], [], [], []
    cur = -1
    while p < n:
        t = flagged(u32(p))
        p += 4
        if t in (HUNK_CODE, HUNK_DATA, HUNK_BSS):
            cur += 1
            if cur >= count:
                raise ValueError("more hunks than the header declares")
            size = 4 * flagged(u32(p))
            p += 4
            types.append(TYPE_NAMES[t])
            relocs.append([])
            symbols.append([])
            if t == HUNK_BSS:
                contents.append(b"")
            else:
                if p + size > n:
                    raise ValueError("truncated hunk file")
                contents.append(data[p:p + size])
                p += size
        elif cur < 0:
            raise ValueError(f"unexpected block 0x{t:x} before first hunk")
        elif t == HUNK_RELOC32:
            while True:
                c = u32(p)
                p += 4
                if c == 0:
                    break
                tgt = u32(p)
                p += 4
                relocs[cur] += [(u32(p + 4 * i), tgt) for i in range(c)]
                p += 4 * c
        elif t in (HUNK_DREL32, HUNK_RELOC32SHORT):
            while True:
                c = u16(p)
                p += 2
                if c == 0:
                    break
                tgt = u16(p)
                p += 2
                relocs[cur] += [(u16(p + 2 * i), tgt) for i in range(c)]
                p += 2 * c
            p = (p + 3) & ~3
        elif t == HUNK_SYMBOL:
            while True:
                c = u32(p) & 0xFFFFFF
                p += 4
                if c == 0:
                    break
                name = data[p:p + 4 * c].rstrip(b"\0").decode("latin-1")
                symbols[cur].append((name, u32(p + 4 * c)))
                p += 4 * c + 4
        elif t in (HUNK_NAME, HUNK_DEBUG):
            p += 4 * u32(p) + 4
        elif t == HUNK_END:
            pass
        else:
            raise ValueError(f"unsupported hunk block 0x{t:x}" + (" (RELRELOC32)" if t == HUNK_RELRELOC32 else ""))
    if p > n:
        raise ValueError("truncated hunk file")
    if cur + 1 != count:
        raise ValueError("fewer hunks than the header declares")
    return sizes, types, contents, relocs, symbols


def relocate(data: bytes, base: int):
    sizes, types, contents, relocs, syms = parse(data)
    addrs, a = [], base
    for sz in sizes:
        addrs.append(a)
        a += sz
    image = bytearray()
    for c, sz in zip(contents, sizes):
        if len(c) > sz:
            raise ValueError("hunk data larger than its allocated size")
        image += c + bytes(sz - len(c))
    entries = applied = 0
    for i, rl in enumerate(relocs):
        for off, tgt in rl:
            entries += 1
            if tgt >= len(addrs) or off + 4 > sizes[i]:
                raise ValueError(f"bad relocation in hunk {i}: offset {off} target {tgt}")
            at = addrs[i] - base + off
            v = struct.unpack_from(">I", image, at)[0]
            struct.pack_into(">I", image, at, (v + addrs[tgt]) & 0xFFFFFFFF)
            applied += 1
    hunks = [{"index": i, "type": types[i], "addr": addrs[i], "size": sizes[i]} for i in range(len(sizes))]
    symbols = sorted(({"name": nm, "addr": addrs[i] + v, "hunk": i}
                      for i, sl in enumerate(syms) for nm, v in sl),
                     key=lambda s: (s["addr"], s["name"]))
    return bytes(image), {"base": base, "hunks": hunks, "symbols": symbols,
                          "reloc_entries": entries, "reloc_count": applied}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("hunk_file")
    ap.add_argument("out_bin")
    ap.add_argument("out_map")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x10000)
    a = ap.parse_args(argv)
    with open(a.hunk_file, "rb") as f:
        data = f.read()
    try:
        image, info = relocate(data, a.base)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    with open(a.out_bin, "wb") as f:
        f.write(image)
    with open(a.out_map, "w") as f:
        json.dump(info, f, indent=2)
    print(f"{len(info['hunks'])} hunks, {len(image)} bytes at 0x{a.base:x}, "
          f"{info['reloc_count']}/{info['reloc_entries']} relocations applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
