#!/usr/bin/env python3
"""Fetch the AROS Kickstart replacement (open source, APL) embedded in FS-UAE's
source as a gzip blob, and split it into rom + ext halves for vAmiga."""
import gzip
import re
import sys
import urllib.request
from pathlib import Path

FSUAE_SHA = "f362278ccd4c60991caac3b4d240d4a3f751bea2"  # pinned upstream commit
URL = f"https://raw.githubusercontent.com/FrodeSolheim/fs-uae/{FSUAE_SHA}/aros.rom.cpp"
out_dir = Path(sys.argv[1] if len(sys.argv) > 1 else "extracted")
out_dir.mkdir(parents=True, exist_ok=True)
src = urllib.request.urlopen(URL).read().decode()
body = src[src.index("{") + 1:src.rindex("}")]
blob = bytes(int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{2}", body))
rom = gzip.decompress(blob)
assert len(rom) == 1024 * 1024, len(rom)
# The blob holds the extension ROM ("AROS Extension Library") first and the main
# ROM ("AROS ROM Operating System", the one with the boot code) second.
assert b"AROS ROM Operating" in rom[512 * 1024:][:64]
(out_dir / "aros.ext").write_bytes(rom[:512 * 1024])
(out_dir / "aros.rom").write_bytes(rom[512 * 1024:])
print("wrote", out_dir / "aros.rom", out_dir / "aros.ext")
