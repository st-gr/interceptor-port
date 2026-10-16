# tests/frontend/test_world_frames.py - the draw stage's pixels against the reference emulator: the carrier-deck cockpit frame (extracted/captures/cockpit-carrier.vasnap),
# which shows model polygons and lines (the carrier's deck, its markings and edges) over the sea. The port's start state (a flight start on the deck, free flight) is the
# snapshot's: record 0 and the carrier at the cell (70, 65), height 30,472 (docs/re/05-flight-model.md "carrier placement"). Skips without the reference build, the ROM,
# the autoboot disk, the snapshot or the extracted assets.
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "interceptor"
REF = ROOT / "build" / "vamiga-ref" / "ref"
ROM = ROOT / "disks" / "kick12.rom"
ADF = ROOT / "extracted" / "interceptor-autoboot.adf"
SNAP = ROOT / "extracted" / "captures" / "cockpit-carrier.vasnap"
ASSETS = ROOT / "assets"
pytestmark = pytest.mark.skipif(
    not (BIN.exists() and REF.exists() and ROM.exists() and ADF.exists() and SNAP.exists() and (ASSETS / "models" / "index.txt").exists()),
    reason="reference emulator, snapshot or assets absent")


def ppm(path):
    parts = Path(path).read_bytes().split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def test_the_carrier_deck_cockpit_window_matches_the_reference_frame(tmp_path):
    script = tmp_path / "deck.txt"
    ref_frame = tmp_path / "ref.ppm"
    script.write_text(f"restore {SNAP}\nwait 1.0\nfreeze\nframe {ref_frame}\n")
    subprocess.run([str(REF), "--rom", str(ROM), "--adf", str(ADF), "--script", str(script)], check=True, timeout=300, cwd=ROOT, capture_output=True)
    mine = tmp_path / "mine.ppm"
    env = {"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy", "PATH": "/usr/bin:/bin"}
    subprocess.run([str(BIN), "--assets", str(ASSETS), "--flight", "--cockpit", "--frames", "3", "--scale", "1", "--dump", str(mine)], check=True, timeout=120, env=env)
    rw, _, rpx = ppm(ref_frame)
    _, _, mpx = ppm(mine)
    bad = []
    for y in range(1, 145):                                        # the cockpit window rows
        for x in range(320):
            o = 3 * ((42 + y) * rw + 186 + 2 * x)
            q = 3 * (y * 320 + x)
            if rpx[o:o + 3] != mpx[q:q + 3]:
                bad.append((x, y, tuple(rpx[o:o + 3]), tuple(mpx[q:q + 3])))
    # the carrier's polygons, its marking lines and its edges are pixel exact (the blitter filler and line rules of note 04), and so are the three pen 2 dots over the sea (palette 0x888, (106,106,106)):
    # the 2 by 1 sprite at (87, 101) and (88, 101) and the 1 by 1 sprite at (36, 107) are specks of the near-ground pass (stage [90], 0x2C0EC, note 04) that take plane 2 from the sea pen 6; the other five
    # specks of the pass in this snapshot (two 1 by 1 and one 2 by 1 sprites, three line-case triangles) fall on the black deck, pen 0, which has no plane 2
    assert bad == [], bad[:10]
