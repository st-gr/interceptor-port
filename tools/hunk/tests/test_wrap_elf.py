import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pytest
from wrap_elf import wrap

IMG = bytes(range(256)) * 3


@pytest.mark.parametrize("machine,em", [("68k", 4), ("arm", 40)])
def test_header_and_segment(machine, em):
    e = wrap(IMG, 0x10000, machine)
    assert e[:4] == b"\x7fELF"
    assert e[4] == 1 and e[5] == 2  # 32-bit, big-endian
    etype, emach = struct.unpack_from(">HH", e, 16)
    assert (etype, emach) == (2, em)
    entry, phoff = struct.unpack_from(">II", e, 24)
    assert entry == 0x10000 and phoff == 52
    phentsize, phnum = struct.unpack_from(">HH", e, 42)
    assert (phentsize, phnum) == (32, 1)
    p_type, off, va, pa, fsz, msz, flags, _ = struct.unpack_from(">IIIIIIII", e, 52)
    assert p_type == 1 and va == pa == 0x10000
    assert fsz == msz == len(IMG)
    assert e[off:off + fsz] == IMG
    assert len(e) == off + len(IMG)
    assert struct.unpack_from(">H", e, 40)[0] == 52  # e_ehsize
    assert flags == 7                                 # RWX
    assert struct.unpack_from(">I", e, 52 + 28)[0] == 4  # p_align


def test_default_machine_is_arm():
    e = wrap(IMG, 0x10000)
    assert struct.unpack_from(">H", e, 18)[0] == 40


def test_custom_base():
    e = wrap(IMG, 0x2000)
    assert struct.unpack_from(">I", e, 24)[0] == 0x2000
    assert struct.unpack_from(">I", e, 52 + 8)[0] == 0x2000
