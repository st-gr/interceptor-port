#!/usr/bin/env python3
"""wrap_elf.py <image.bin> <out.elf> [--base 0x10000] [--machine arm|68k]

Wrap a flat big-endian image in a minimal ELF32 container with one RWX PT_LOAD
segment at --base, so tools that only accept executable headers (rea) can load
it. Machine 68k is EM_68K (4); arm is EM_ARM (40), a label that only passes
rea's header check and needs REA_GHIDRA_LANGUAGE_ID to analyse as 68000.
"""
import argparse
import struct

MACHINES = {"68k": 4, "arm": 40}
EHSIZE, PHSIZE = 52, 32


def wrap(img: bytes, base: int = 0x10000, machine: str = "arm") -> bytes:
    ident = b"\x7fELF" + bytes([1, 2, 1, 0]) + bytes(8)  # ELF32, big-endian, v1
    hdr = ident + struct.pack(
        ">HHIIIIIHHHHHH", 2, MACHINES[machine], 1, base, EHSIZE, 0, 0,
        EHSIZE, PHSIZE, 1, 40, 0, 0)
    ph = struct.pack(">IIIIIIII", 1, EHSIZE + PHSIZE, base, base,
                     len(img), len(img), 7, 4)
    return hdr + ph + img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("out")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x10000)
    ap.add_argument("--machine", choices=MACHINES, default="arm")
    a = ap.parse_args()
    with open(a.image, "rb") as f:
        img = f.read()
    with open(a.out, "wb") as f:
        f.write(wrap(img, a.base, a.machine))


if __name__ == "__main__":
    main()
