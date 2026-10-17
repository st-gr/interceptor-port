"""Tables of the sky fill and the sea mesh pass (docs/re/04-scene-composition.md): sizes and spot checks on the real image."""
from pathlib import Path

import pytest

from extract.sky import SIZES, SPANS, decode_all
from extract.tables import DecodeError, IMAGE_BASE

REAL = Path(__file__).resolve().parents[3] / "extracted" / "interceptor.bin"
needs_image = pytest.mark.skipif(not REAL.exists(), reason="extracted/interceptor.bin absent")


def test_span_sizes_are_the_documented_ones():
    for name, (lo, hi) in SPANS.items():
        assert hi - lo + 1 == SIZES[name], name


def test_short_image_is_rejected():
    with pytest.raises(DecodeError):
        decode_all(bytes(0x1000), IMAGE_BASE)


@needs_image
def test_real_image_tables_match_the_image_and_have_the_documented_shape():
    image = REAL.read_bytes()
    out = decode_all(image, IMAGE_BASE)
    assert {k: len(v) for k, v in out.items()} == SIZES
    for name, (lo, hi) in SPANS.items():            # verbatim copies of the image bytes
        assert out[name] == image[lo - IMAGE_BASE:hi - IMAGE_BASE + 1], name
    # shape only (no table contents are committed): a zone table opens with an ascending list of row numbers ended by -1, then one offset word per row
    for name in ("sea_zone.bin", "sea_zone2.bin"):
        t = out[name]
        words = [int.from_bytes(t[i:i + 2], "big") for i in range(0, len(t), 2)]
        n = words.index(0xFFFF)
        assert n >= 1 and words[:n] == sorted(set(words[:n]))
        assert all(2 * w < len(t) for w in words[n + 1:2 * n + 1])
    assert out["sea_zone2.bin"][-2:] == b"\xff\xff"   # the chain ends with a negative word
    # the cell offsets are small signed bytes, the far-test table grows, the coarse grid words are positive record offsets inside the blob
    pairs = out["mesh_scan.bin"][:50]
    assert all(b < 3 or b > 253 for b in pairs)
    far = [int.from_bytes(out["mesh_far.bin"][i:i + 2], "big") for i in range(0, 36, 2)]
    assert len(far) == 18 and far == sorted(far) and far[0] > 0
    cells = out["mesh_cells.bin"]
    grid = [int.from_bytes(cells[i:i + 2], "big") for i in range(0, 128, 2)]
    assert all(0 < w < len(cells) for w in grid)
