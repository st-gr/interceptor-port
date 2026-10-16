"""Tables of the sky fill and the sea mesh pass (docs/re/04-scene-composition.md, "Sky fill and ground polygon" and "Stage [68]: the sea mesh pass").

Every table is written as the raw big-endian bytes of the relocated flat image (the core reads words and longs itself):

  sea_zone.bin    560 bytes  0x23188..0x233B7   the base-colour flag's zone table (V:4A953)
  sea_zone2.bin    80 bytes  0x233B8..0x23407   the second zone table (V:4A954): one zone (row 17, column 16) with one record of nine phase groups
                                                 and the terminating word 0xFFFF; the note's "28 bytes" is the 12-byte header plus the first groups,
                                                 the data runs on to 0x23407 and the operation table of note 03 starts at 0x23408
  mesh_scan.bin  2972 bytes  0x2E600..0x2F19B   the 25 (dx, dz) byte pairs, the scan lists and the own-cell octant tables
  mesh_far.bin     36 bytes  0x2F4F8..0x2F51B   the far-test word table (18 words)
  mesh_cells.bin 6224 bytes  0x47D7C..0x495CB   the coarse grid (8 x 8 words) and records, the fine grid (32 x 32 words) and records; the list
                                                 pointers inside are absolute image addresses (rebase by 0x47D7C)
"""
from typing import Dict

from .tables import IMAGE_BASE, DecodeError, Mem

# name: (first address, last address)
SPANS = {
    "sea_zone.bin": (0x23188, 0x233B7),
    "sea_zone2.bin": (0x233B8, 0x23407),
    "mesh_scan.bin": (0x2E600, 0x2F19B),
    "mesh_far.bin": (0x2F4F8, 0x2F51B),
    "mesh_cells.bin": (0x47D7C, 0x495CB),
}
SIZES = {"sea_zone.bin": 560, "sea_zone2.bin": 80, "mesh_scan.bin": 2972, "mesh_far.bin": 36, "mesh_cells.bin": 6224}


def decode_all(data: bytes, base: int = IMAGE_BASE) -> Dict[str, bytes]:
    mem = Mem(data, base, "sky tables")
    out = {}
    for name, (lo, hi) in SPANS.items():
        n = hi - lo + 1
        if n != SIZES[name]:
            raise DecodeError(f"{name}: span 0x{lo:X}..0x{hi:X} is {n} bytes, expected {SIZES[name]}")
        out[name] = mem.raw(lo, n)
    return out
