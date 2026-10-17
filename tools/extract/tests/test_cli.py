import json
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path

import pytest

from extract.manifest import EXTRACTOR_VERSION
from tests.synth import flight_patches, make_hunk_exe
from tests.test_ilbm import make_ilbm

XDFTOOL = shutil.which("xdftool")
PICTURES = ["splsh", "inst5", "frnt5"]
# the invented sample files of the synthetic disk: textegn and the game's other file names (sizes invented; the object table of version 24 reads their lengths), and three extra names
GAME_SAMPLES = ["textcpt", "textwnd", "texttre", "textger", "text201", "texti0a", "texti0b", "texti1", "texti2", "texti3", "texti4", "texti5"]
DISK_ORDER = ["texti0a", "textger", "textegn", "textcpt", "snda", "sndb", "sndc", "texttre", "texti1", "texti2", "texti3", "texti4", "texti5", "text201", "textwnd", "texti0b"]   # AmigaDOS directory order for these names
SOUND_ORDER = ["sndc", "snda", "sndb"] + GAME_SAMPLES   # written in this (non-alphabetical) order
TEXTEGN_BYTES = 3000   # invented; the first four bytes are known to the tests
pytestmark = pytest.mark.skipif(XDFTOOL is None, reason="xdftool (amitools) not on PATH")


def build_adf(tmp_path, with_pix=True, exe=None):
    ilbm = make_ilbm(16, 1, 1, [[bytes([0xF0, 0x0F])]], [(0, 0, 0), (255, 255, 255)])
    for name in PICTURES:
        (tmp_path / name).write_bytes(ilbm)
    (tmp_path / "textegn").write_bytes(bytes([0, 127, 255, 128]) + bytes(i * 7 & 0xFF for i in range(TEXTEGN_BYTES - 4)))  # signed: 0, 127, -1, -128, then invented
    for k, n in enumerate(SOUND_ORDER):
        size = 4 if n.startswith("snd") else 700 + 450 * (k % 7) + (2500 if n == "textwnd" else 0)   # invented sizes; textwnd is long enough for object 9's offset
        (tmp_path / n).write_bytes(bytes([1, 2, 3, 4]) + bytes((i * 13 + k) & 0xFF for i in range(size - 4)))
    (tmp_path / "textply").write_bytes(bytes(2046) + b"\xff\xff\xff" + bytes(3676 - 2049))   # the demo stream: invented
    (tmp_path / "textctl").write_bytes(bytes(4 * 3676))
    (tmp_path / "exe").write_bytes(exe or make_hunk_exe(0x3C000, flight_patches()))
    adf = tmp_path / "t.adf"
    cmd = [XDFTOOL, str(adf), "create", "+", "format", "Test", "+",
           "write", str(tmp_path / "exe"), "F-18 Interceptor", "+",
           "makedir", "text", "+", "write", str(tmp_path / "textegn"), "text/textegn"]
    for n in SOUND_ORDER + ["textply", "textctl"]:
        cmd += ["+", "write", str(tmp_path / n), f"text/{n}"]
    if with_pix:
        cmd += ["+", "makedir", "pix"]
        for name in PICTURES:
            cmd += ["+", "write", str(tmp_path / name), f"pix/{name}"]
    subprocess.run(cmd, check=True, capture_output=True)
    return adf


def run_extract(adf, out, *extra):
    """The synthetic disks have no segment table: flat spans are allowed unless the test passes its own options."""
    args = extra or ("--allow-flat-spans",)
    return subprocess.run([sys.executable, "-m", "extract", str(adf), str(out), *args],
                          capture_output=True, text=True)


def test_extracts_pictures_sounds_and_manifest(tmp_path):
    adf = build_adf(tmp_path)
    out = tmp_path / "assets"
    r = run_extract(adf, out)
    assert r.returncode == 0, r.stderr
    assert (out / "pix" / "splsh.png").exists()
    assert json.loads((out / "pix" / "splsh.palette.json").read_text()) == [[0, 0, 0], [255, 255, 255]]
    assert (out / "sounds" / "textegn.raw").read_bytes()[:4] == bytes([0, 127, 255, 128])
    assert len((out / "sounds" / "textegn.raw").read_bytes()) == TEXTEGN_BYTES
    with wave.open(str(out / "sounds" / "textegn.wav")) as w:
        assert (w.getnchannels(), w.getsampwidth()) == (1, 1)
        assert w.readframes(4) == bytes([128, 255, 127, 0])  # signed -> unsigned
    m = json.loads((out / "manifest.json").read_text())
    assert m["extractor_version"] == EXTRACTOR_VERSION
    assert (out / "VERSION").read_text().strip() == str(EXTRACTOR_VERSION)
    assert (out / "pix" / "index.txt").read_text().split() == PICTURES
    assert (out / "sounds" / "index.txt").read_text().split() == DISK_ORDER
    # directory (hash table) order on the disk: neither write order nor alphabetical
    assert DISK_ORDER != sorted(DISK_ORDER) and DISK_ORDER != SOUND_ORDER
    assert "sample_rate_provisional" not in m
    assert set(m["sample_rate_hz"]) == set(DISK_ORDER) and all(isinstance(v, int) and v > 0 for v in m["sample_rate_hz"].values())
    assert "rate_note" not in m
    assert set(m["files"]) >= {f"pix/{n}{ext}" for n in PICTURES for ext in (".png", ".palette.json")} | {
        f"sounds/{n}{ext}" for n in DISK_ORDER for ext in (".wav", ".raw")} | {"pix/index.txt", "sounds/index.txt", "VERSION"}


def test_wrong_disk_leaves_no_output_and_names_missing_file(tmp_path):
    adf = build_adf(tmp_path, with_pix=False)
    out = tmp_path / "assets"
    r = run_extract(adf, out)
    assert r.returncode == 1
    assert "pix/splsh" in r.stderr
    assert not out.exists()


def test_non_adf_file_is_rejected(tmp_path):
    bogus = tmp_path / "x.adf"
    bogus.write_bytes(b"not a disk")
    r = run_extract(bogus, tmp_path / "assets")
    assert r.returncode == 1
    assert "ADF" in r.stderr or "disk" in r.stderr.lower()


def test_missing_parent_directory_is_created(tmp_path):
    adf = build_adf(tmp_path)
    out = tmp_path / "nested" / "assets"
    r = run_extract(adf, out)
    assert r.returncode == 0, r.stderr
    assert (out / "manifest.json").exists()


def test_unwritable_output_reports_error_without_traceback(tmp_path):
    adf = build_adf(tmp_path)
    blocker = tmp_path / "blocker"
    blocker.write_bytes(b"a regular file")
    r = run_extract(adf, blocker / "assets")
    assert r.returncode == 1
    assert r.stderr.startswith("error:")
    assert "Traceback" not in r.stderr


def test_world_tables_are_written_from_the_executable(tmp_path):
    adf = build_adf(tmp_path)
    out = tmp_path / "assets"
    r = run_extract(adf, out)
    assert r.returncode == 0, r.stderr
    assert (out / "palette" / "fade.pal").stat().st_size == 16 * 16 * 3
    assert (out / "palette" / "flight.pal").stat().st_size == 16 * 3
    assert (out / "fonts" / "hud.bin").stat().st_size == 64 * 5
    assert (out / "tables" / "lod.bin").stat().st_size == 240
    assert (out / "tables" / "range.bin").stat().st_size == 64
    for tag in ("default", "11", "14", "30"):
        assert (out / "tables" / f"camera_ring_{tag}.bin").stat().st_size == 60
    assert (out / "terrain" / "terrain.bin").read_bytes()[:4] == b"ITRN"
    assert (out / "cockpit" / "warnings.bin").stat().st_size == 54 * 28
    assert (out / "cockpit" / "masks.bin").stat().st_size == 340
    assert (out / "cockpit" / "markers.bin").stat().st_size == 2 * (12 + 44)
    assert (out / "cockpit" / "arcs.bin").stat().st_size == 42 + 40
    assert (out / "cockpit" / "mapshapes.bin").stat().st_size == 0x3001C - 0x2FED8
    from extract.cockpit import check_mapshapes   # the hash path: the synthetic disk carries the invented shapes of synth.py
    from tests.synth import SYNTH_MAPSHAPES_FNV1A
    check_mapshapes((out / "cockpit" / "mapshapes.bin").read_bytes(), want_hash=SYNTH_MAPSHAPES_FNV1A)
    for n in PICTURES:   # raw picture indices, one byte per pixel (the test pictures are 16x1)
        assert (out / "pix" / f"{n}.idx").stat().st_size == 16


def test_missing_executable_is_an_error(tmp_path):
    adf = build_adf(tmp_path)
    # a disk without the game executable is not the game disk
    import subprocess as sp
    bad = tmp_path / "bad.adf"
    sp.run([XDFTOOL, str(bad), "create", "+", "format", "T", "+", "makedir", "pix"],
           check=True, capture_output=True)
    r = run_extract(bad, tmp_path / "assets2")
    assert r.returncode == 1 and "pix/splsh" in r.stderr


def model_exe():
    """Records at 0x25ECC: id 0 decodes, id 1 has an unknown operation, id 2 has a BLOCKING
    line (slot 20 is left unresolved by op 42 over a block without its sources); the rest are zero (empty)."""
    import struct as st
    patches = dict(flight_patches())

    def words(*w):
        return b"".join(st.pack(">H", x & 0xFFFF) for x in w)

    def rec(i, base):
        patches[0x25ECC + 20 * i] = st.pack(">5I", 0x21C44, base, base, base, 0)

    for i, base, prog in ((0, 0x30000, [0xC008, 0, 6, 3]),
                          (1, 0x30100, [0xC014]),
                          (2, 0x30200, [0x80A8, 0, 0xC00C, 0, 6, 0x8078, 0x8001])):
        rec(i, base)
        patches[base] = words(0x8080) + words(0xA010)
        patches[base + 0x10] = words(*prog)
        nv = 12 if i == 2 else 6
        patches[base + 0x80] = words(0, 0x100, 0x200) + bytes([0x23, 1, nv, nv]) + words(*([1] * (3 * nv)))
    return make_hunk_exe(0x3C000, patches)


def test_model_failures_do_not_abort_the_run_and_are_listed_in_the_index(tmp_path):
    adf = build_adf(tmp_path, exe=model_exe())
    out = tmp_path / "assets"
    r = run_extract(adf, out)
    assert r.returncode == 0, r.stderr
    rows = [l.split("\t") for l in (out / "models" / "index.txt").read_text().splitlines()
            if not l.startswith("#")]
    assert len(rows) == 134
    assert rows[0][:3] == ["0", "model_000.bin", "ok"]
    assert rows[1][1] == "-" and rows[1][2].startswith("failed: model 1: op 5 at 0x30110: unknown operation 5")
    assert rows[2][1] == "model_002.bin" and rows[2][2].startswith("partial: model 2: op 3 at 0x30214: BLOCKING")
    assert rows[3][2].startswith("empty:")
    assert (out / "models" / "model_000.bin").read_bytes()[:4] == b"IMDL"
    assert not (out / "models" / "model_001.bin").exists()



def test_extraction_uses_the_shipped_segment_table_unless_the_spans_are_to_stay_flat(tmp_path):
    adf = build_adf(tmp_path)
    out = tmp_path / "assets"
    r = subprocess.run([sys.executable, "-m", "extract", str(adf), str(out)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert json.loads((out / "manifest.json").read_text())["spans_relocated"] is True
    assert not (out / "tables" / "flight_spans_flat.txt").exists()


def test_flat_spans_are_recorded_in_the_manifest_and_marked_for_the_core(tmp_path):
    adf = build_adf(tmp_path)
    out = tmp_path / "assets"
    r = run_extract(adf, out, "--allow-flat-spans")
    assert r.returncode == 0, r.stderr
    assert json.loads((out / "manifest.json").read_text())["spans_relocated"] is False
    assert (out / "tables" / "flight_spans_flat.txt").is_file()


def test_spans_with_the_segment_table_are_recorded_as_relocated(tmp_path):
    adf = build_adf(tmp_path)
    out = tmp_path / "assets"
    seg = tmp_path / "image-static.txt"
    seg.write_text("seg 0 flat 010000 ram C00000 size 3C000 segsize 3C008 ok\n")
    r = run_extract(adf, out, "--segments", str(seg))
    assert r.returncode == 0, r.stderr
    assert json.loads((out / "manifest.json").read_text())["spans_relocated"] is True
    assert not (out / "tables" / "flight_spans_flat.txt").exists()
