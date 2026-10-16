"""The browser build (make web): the page, the wasm and the JavaScript extractor, and no asset in the output.
Skipped when emcc is not on PATH (the Emscripten SDK; CI has a job for it)."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

pytestmark = pytest.mark.skipif(shutil.which("emcc") is None, reason="emcc (Emscripten) not on PATH")


def test_make_web_builds_the_page_without_assets():
    r = subprocess.run(["make", "-j1", "web"], cwd=ROOT, capture_output=True, text=True, timeout=1500)
    assert r.returncode == 0, r.stdout[-2000:] + r.stderr[-2000:]
    web = ROOT / "build" / "web"
    for name in ["index.html", "interceptor.js", "interceptor.wasm", "page.js", "extract/index.js", "extract/segments.txt"]:
        assert (web / name).is_file(), name
    assert not list(web.rglob("*.data")), "no preloaded asset package"
    assert not (web / "assets").exists()
    js = (web / "interceptor.js").read_text()
    assert "createInterceptor" in js
    # no asset file of the game is copied: only the page, the module and the extractor's sources
    names = sorted(str(p.relative_to(web)) for p in web.rglob("*") if p.is_file())
    assert all(n.endswith((".js", ".html", ".wasm", ".txt")) for n in names), names
    # the page imports what the build provides
    html = (web / "index.html").read_text()
    for ref in ["./extract/index.js", "./page.js", "interceptor.js", "extract/segments.txt"]:
        assert ref in html
    # milestone 7: the flight log's persistence. The exports the page calls and the IDBFS in the module, the page's wiring of them.
    for fn in ["_web_audio_unlock", "_web_game_end", "_web_log_saved"]:
        assert fn in js, fn
    assert "IDBFS" in js, "the build links -lidbfs.js"
    for ref in ["mountLogStore", "makeSyncer", "onLogSaved", "LOG_PATH", 'id="storage"']:
        assert ref in html, ref
    assert "mountLogStore" in (web / "page.js").read_text()
