# tests/frontend/test_splash.py - runs the built frontend headless against a synthetic assets dir.
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "interceptor"
sys.path.insert(0, str(ROOT / "tools" / "extract"))
from extract.png import write_png  # noqa: E402

pytestmark = pytest.mark.skipif(not BIN.exists(), reason="build/interceptor not built")

# The core no longer enters GAME_MODE_SPLASH (milestone 4: the first screen is the credits), so these tests force the mode with --text-test: the frontend's splash
# path (video.c) stays and is tested here.
# the version the core accepts (src/core/assets.h), so that the tests follow the extractor version
VERSION = int(re.search(r"#define\s+CORE_ASSETS_VERSION\s+(\d+)", (ROOT / "src" / "core" / "assets.h").read_text()).group(1))


def make_assets(tmp_path):
    (tmp_path / "pix").mkdir()
    pixels = bytes([1] * 320 * 200)                       # solid colour index 1
    write_png(tmp_path / "pix" / "splsh.png", 320, 200, pixels, [(0, 0, 0), (255, 0, 0)])
    (tmp_path / "pix" / "index.txt").write_text("splsh\n")
    (tmp_path / "VERSION").write_text(f"{VERSION}\n")
    (tmp_path / "manifest.json").write_text(json.dumps({"extractor_version": VERSION, "files": {}}))
    return tmp_path


def run(args, env_extra=None):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", **(env_extra or {}))
    return subprocess.run([str(BIN), *args], capture_output=True, text=True, env=env)


def test_missing_assets_dir_exits_with_extractor_hint(tmp_path):
    r = run(["--assets", str(tmp_path / "nope"), "--frames", "1"])
    assert r.returncode == 1
    assert "python -m extract" in r.stderr


def test_splash_frame_is_the_picture(tmp_path):
    assets = make_assets(tmp_path)
    out = tmp_path / "frame.ppm"
    r = run(["--assets", str(assets), "--frames", "2", "--dump", str(out), "--scale", "1", "--text-test", "mode=splash"])
    assert r.returncode == 0, r.stderr
    data = out.read_bytes()
    assert data.startswith(b"P6\n320 200\n255\n")
    px = data[len(b"P6\n320 200\n255\n"):]
    assert px[:3] == bytes([255, 0, 0]) and px[-3:] == bytes([255, 0, 0])


def test_missing_pix_index_fails_naming_the_file(tmp_path):
    assets = make_assets(tmp_path)
    (assets / "pix" / "index.txt").unlink()
    r = run(["--assets", str(assets), "--frames", "1"])
    assert r.returncode == 1
    assert "pix/index.txt" in r.stderr


def test_wrong_version_is_refused(tmp_path):
    assets = make_assets(tmp_path)
    (assets / "VERSION").write_text("1\n")
    r = run(["--assets", str(assets), "--frames", "1"])
    assert r.returncode == 1
    assert f"extractor version 1, need {VERSION}" in r.stderr


def test_index_order_selects_the_first_picture(tmp_path):
    assets = make_assets(tmp_path)
    write_png(assets / "pix" / "other.png", 320, 200, bytes([1] * 320 * 200), [(0, 0, 0), (0, 255, 0)])
    (assets / "pix" / "index.txt").write_text("other\nsplsh\n")
    out = tmp_path / "frame.ppm"
    r = run(["--assets", str(assets), "--frames", "1", "--dump", str(out), "--scale", "1", "--text-test", "mode=splash"])
    assert r.returncode == 0, r.stderr
    px = out.read_bytes()[len(b"P6\n320 200\n255\n"):]
    assert px[:3] == bytes([0, 255, 0])      # "other" (index 0), not red "splsh"
