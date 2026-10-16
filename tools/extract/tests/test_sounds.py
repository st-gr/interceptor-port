"""Structure tests of tables/sounds.bin and the sample rates (extractor version 25) on the synthetic disk and on invented file lengths.
The table's values are note 10's and live there; these tests check the shape only."""
import json
import shutil
import struct

import pytest

from extract.sound_table import NONE, OBJECTS, RECORD_BYTES, build_sound_table
from extract.sounds import PAL_CLOCK_HZ, reference_period, sample_rate_hz
from tests.test_cli import DISK_ORDER, build_adf, run_extract

needs_xdftool = pytest.mark.skipif(shutil.which("xdftool") is None, reason="xdftool (amitools) not on PATH")
FIELDS = struct.Struct(">BBBBIIHbBBBH")


def records(b):
    assert len(b) == OBJECTS * RECORD_BYTES
    return [FIELDS.unpack_from(b, RECORD_BYTES * i) for i in range(OBJECTS)]


def check_structure(b, names, sizes):
    recs = records(b)
    assert len(recs) == 37
    for n, (file, kind, voice, volume, offset, length, period, count, reload, nxt, script, pad) in enumerate(recs):
        assert pad == 0
        assert kind in (0, 1, 2, NONE)
        if kind == NONE:
            assert (file, length, offset) == (NONE, 0, 0)
            continue
        assert voice < 4 and volume <= 0x3F and period > 0 and script <= 4
        assert nxt == NONE or (nxt < 37 and nxt != n)
        if kind == 0:
            assert file < len(names)
            assert offset + length <= sizes[names[file]]
        else:
            assert file == NONE and offset == 0 and length > 0 and count == -1
    return recs


@needs_xdftool
def test_the_synthetic_disk_gets_a_table_of_37_records_inside_the_files(tmp_path):
    out = tmp_path / "assets"
    r = run_extract(build_adf(tmp_path), out)
    assert r.returncode == 0, r.stderr
    names = (out / "sounds" / "index.txt").read_text().split()
    assert names == DISK_ORDER
    sizes = {n: (out / "sounds" / f"{n}.raw").stat().st_size for n in names}
    recs = check_structure((out / "tables" / "sounds.bin").read_bytes(), names, sizes)
    assert {r[1] for r in recs} >= {0, 1, 2, NONE}          # samples, a tone, noise and the one object no loader makes
    assert any(r[1] == 0 and r[5] > 0 for r in recs)
    m = json.loads((out / "manifest.json").read_text())
    for n in names:
        assert m["sample_rate_hz"][n] == (PAL_CLOCK_HZ + reference_period(n) // 2) // reference_period(n)
    assert "sample_rate_provisional" not in m


def test_a_missing_file_leaves_its_objects_absent_and_a_short_file_is_clamped():
    names = ["a", "textcpt", "textegn", "textwnd"]
    full = build_sound_table(names, {"textcpt": 100, "textegn": 100, "textwnd": 100})
    check_structure(full, names, {"textcpt": 100, "textegn": 100, "textwnd": 100})
    short = build_sound_table(names, {"textcpt": 3, "textegn": 5})
    recs = check_structure(short, names, {"textcpt": 3, "textegn": 5})
    assert recs[0][1] == 0 and recs[0][5] <= 3
    assert sum(1 for r in recs if r[1] == NONE) > 1
    none = build_sound_table(names, {})
    check_structure(none, names, {})
    assert [r[1] for r in records(none)].count(NONE) < OBJECTS      # the synthesised objects need no file


def test_the_rate_follows_the_period():
    for name in ("textegn", "unknown-file", "texti1"):
        assert abs(sample_rate_hz(name) * reference_period(name) - PAL_CLOCK_HZ) <= reference_period(name) // 2 + 1
