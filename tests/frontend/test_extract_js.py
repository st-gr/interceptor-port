"""The two extractors agree byte for byte: tools/extract (Python) and tools/extract-js (JavaScript, for the browser build).

Two tests, both skipped when node is missing:
- the committed synthetic disk (invented bytes, tools/extract-js/test/fixtures/synth.adf) through both: runs in CI;
- the player's own disk, extracted/interceptor.adf, through both, every file compared: local only (the disk is not in the
  repository), skipped in CI. Prints nothing of the data; the comparison fails with the first differing file names."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
EXTRACT_PY = ROOT / "tools" / "extract"
CLI_JS = ROOT / "tools" / "extract-js" / "cli.js"
SYNTH = ROOT / "tools" / "extract-js" / "test" / "fixtures" / "synth.adf"
REAL = ROOT / "extracted" / "interceptor.adf"
NODE = shutil.which("node")

pytestmark = pytest.mark.skipif(NODE is None, reason="node not on PATH")


def files_of(d: Path) -> dict:
    return {p.relative_to(d).as_posix(): p.read_bytes() for p in sorted(d.rglob("*")) if p.is_file()}


def run_both(adf: Path, tmp_path: Path, *flags):
    py, js = tmp_path / "py", tmp_path / "js"
    env = dict(os.environ, PYTHONPATH=str(EXTRACT_PY))
    r = subprocess.run([sys.executable, "-m", "extract", str(adf), str(py), *flags], capture_output=True, text=True, env=env, cwd=EXTRACT_PY)
    assert r.returncode == 0, r.stderr
    r = subprocess.run([NODE, str(CLI_JS), str(adf), str(js), *flags], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return files_of(py), files_of(js)


def assert_identical(a: dict, b: dict) -> int:
    assert sorted(a) == sorted(b), f"file sets differ: only Python {sorted(set(a) - set(b))[:5]}, only JavaScript {sorted(set(b) - set(a))[:5]}"
    differing = [n for n in a if a[n] != b[n]]
    assert not differing, f"{len(differing)} of {len(a)} files differ, first: {differing[:5]}"
    return len(a)


def test_synthetic_disk_is_extracted_identically_by_both_ports(tmp_path):
    n = assert_identical(*run_both(SYNTH, tmp_path, "--allow-flat-spans"))
    assert n > 80


@pytest.mark.skipif(not REAL.exists(), reason="extracted/interceptor.adf absent (the player's own disk image)")
def test_real_disk_is_extracted_identically_by_both_ports(tmp_path):
    n = assert_identical(*run_both(REAL, tmp_path))      # both CLIs take the segment table shipped with their package (tools/extract/extract/segments.txt, tools/extract-js/segments.txt)
    assert n > 200
