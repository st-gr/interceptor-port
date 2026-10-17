#!/usr/bin/env python3
"""Convert the emulator's peek log of a free flight (tools/vamiga-ref/scripts/combat7-statedump.txt) into one binary file for the rig
(extracted/captures/state-flight.bin, git-ignored; never committed): the state hunks 71 to 77 at their flat addresses (as the code addresses
them), and the machine's RAM at the real addresses (slow RAM 0xC00000 up, chip RAM below 0x10000 and above the image).

File format (all big-endian): "ST07", u32 count, then per entry: u8 kind (0 flat state hunk, 1 real RAM), u32 address, u32 length, the bytes.
The seglist lines of the log are copied into the header as a text block: u32 length, the lines (hunk, flat and real address of each state hunk).

    python state_dump.py [LOG [OUT]]
"""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
LOG = os.path.join(ROOT, "extracted", "captures", "logs", "combat7-statedump.txt")
OUT = os.path.join(ROOT, "extracted", "captures", "state-flight.bin")


def convert(log=LOG, out=OUT):
    ents, segs = [], []
    for ln in open(log):
        p = ln.split()
        if not p:
            continue
        if p[0] == "seg" and 71 <= int(p[1]) <= 77:
            segs.append(ln.strip())
        if len(p) >= 3 and p[1][:2] in ("F:", "R:"):
            ents.append((0 if p[1][0] == "F" else 1, int(p[1][2:], 16), bytes.fromhex("".join(p[2:]))))
    # a label may be logged once per peek; keep the order of the script
    txt = ("\n".join(segs) + "\n").encode()
    with open(out, "wb") as f:
        f.write(b"ST07" + struct.pack(">I", len(ents)))
        f.write(struct.pack(">I", len(txt)) + txt)
        for kind, a, d in ents:
            f.write(struct.pack(">BII", kind, a, len(d)) + d)
    return len(ents), sum(len(d) for _, _, d in ents)


def load(path=OUT):
    """Return (segs text, [(kind, addr, bytes)])."""
    b = open(path, "rb").read()
    assert b[:4] == b"ST07"
    n, tl = struct.unpack_from(">II", b, 4)
    txt = b[12:12 + tl].decode()
    o = 12 + tl
    out = []
    for _ in range(n):
        kind, a, ln = struct.unpack_from(">BII", b, o)
        o += 9
        out.append((kind, a, b[o:o + ln]))
        o += ln
    return txt, out


if __name__ == "__main__":
    a = sys.argv[1:]
    print("%d peeks, %d bytes" % convert(*a))
