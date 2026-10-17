"""Smoke run of the browser build (Playwright), in Chromium, Firefox and WebKit: local only, needs the player's own ADF and `make web`.

    pip install --user playwright && python -m playwright install chromium firefox webkit
    python tools/web/smoke.py extracted/interceptor.adf /tmp/web-smoke [--engines chromium,firefox,webkit] [--quick]

Serves build/web over HTTP and, per engine, runs the same checks (tools/web/README.md lists the shared libraries the engines need in the
sandbox; an engine that cannot launch is skipped with the reason printed):

  pick      refused files first (not an ADF, a flux image, another disk), then the ADF: the messages, Start hidden until Ready
  start     Start, the credits screen draws (screenshots go into the output directory, never into the repository)
  keys      which keys reach the game (SDL's handler runs first, ours after it: defaultPrevented tells what the game consumed), and that
            each of a few keys on a fresh page leaves the credits
  reload    the flight log written at a save point in one visit is read back in the next visit of the same browser context (and is absent in a
            fresh context: the control)
  nostorage a context where indexedDB.open throws: one message line, the run goes on with the in-memory log (Review Focus 3)
  gamepad   a fake pad (navigator.getGamepads replaced) whose button 0 (A) is pressed leaves the credits as RETURN does, through SDL's
            gamepad path and the table of src/frontend/sdl/input.c
  audio     (Chromium only) after Start the AudioContext is running and the ScriptProcessor callback count grows

--quick runs only pick, start, reload, nostorage, gamepad (and audio in Chromium). Prints no game data; exits 1 when a check failed
(a skipped engine is no failure)."""
import argparse
import functools
import hashlib
import http.server
import os
import sys
import threading
from pathlib import Path

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[2]
VIEW = {"width": 1100, "height": 900}

# a fake gamepad: the standard mapping, 17 buttons, button 0 (A) pressed once window.__padA is set
GAMEPAD_INIT = """
(() => {
  const mk = () => {
    const buttons = []; for (let i = 0; i < 17; i++) buttons.push({ pressed: false, touched: false, value: 0 });
    if (window.__padA) buttons[0] = { pressed: true, touched: true, value: 1 };
    return { id: 'Fake Pad (smoke)', index: 0, connected: true, mapping: 'standard', axes: [0, 0, 0, 0], buttons, timestamp: performance.now(), vibrationActuator: null };
  };
  navigator.getGamepads = () => [mk(), null, null, null];
  window.__announcePad = () => { const e = new Event('gamepadconnected'); e.gamepad = mk(); window.dispatchEvent(e); };
})();
"""

# counts the ScriptProcessor callbacks (SDL's audio on the web build); wraps the creation, so the count is the page's own audio path
AUDIO_INIT = """
(() => {
  window.__audioProcess = 0; window.__audioCtxs = [];
  const Ctx = window.AudioContext || window.webkitAudioContext;
  if (!Ctx) return;
  const orig = Ctx.prototype.createScriptProcessor;
  Ctx.prototype.createScriptProcessor = function (...a) {
    window.__audioCtxs.push(this);
    const node = orig.apply(this, a);
    const desc = Object.getOwnPropertyDescriptor(Object.getPrototypeOf(node), 'onaudioprocess');
    let fn = null;
    Object.defineProperty(node, 'onaudioprocess', { configurable: true, get() { return fn; }, set(f) { fn = f; node.addEventListener('audioprocess', (e) => { window.__audioProcess++; f && f.call(node, e); }); } });
    return node;
  };
})();
"""

NO_STORAGE_INIT = "indexedDB.open = function () { throw new DOMException('storage denied (smoke)', 'SecurityError'); };"

results = {}   # engine -> {check: (ok, detail)}


def record(engine, name, ok, detail=""):
    """ok True: passed; False: failed; None: skipped (the detail says why: an engine that cannot do what the check needs is no failure)."""
    results.setdefault(engine, {})[name] = (None if ok is None else bool(ok), detail)
    word = "SKIP" if ok is None else ("PASS" if ok else "FAIL")
    print(f"[{engine}] {name}: {word}{' - ' + detail if detail else ''}", flush=True)


def frames_tick(pg):
    """Whether requestAnimationFrame runs on this page: the game's main loop is driven by it (WPE WebKit headless in the sandbox has no display and does not tick)."""
    pg.evaluate("window.__raf = 0; (function f() { window.__raf++; requestAnimationFrame(f); })()")
    pg.wait_for_timeout(1500)
    return pg.evaluate("window.__raf") > 10


def canvas_hash(pg):
    return hashlib.sha256(pg.locator("#canvas").screenshot()).hexdigest()


def open_ready(ctx, url, adf, timeout=180000, polling=200):
    """A page of the context with the ADF picked and the page at Ready (Start shown)."""
    pg = ctx.new_page()
    pg.goto(url)
    pg.set_input_files("#file", files=[str(adf)])
    pg.wait_for_function("!document.getElementById('start').hidden || document.getElementById('error').textContent.length > 0", timeout=timeout, polling=200)
    return pg


def check_pick(engine, b, url, adf, out):
    pg = b.new_page(viewport=VIEW)
    logs = []
    pg.on("console", lambda m: logs.append(f"{m.type}: {m.text[:200]}"))
    pg.on("pageerror", lambda e: logs.append(f"pageerror: {str(e)[:300]}"))
    pg.goto(url)
    refused = {
        "text": {"name": "notes.txt", "mimeType": "text/plain", "buffer": b"hello"},
        "flux": {"name": "disk.scp", "mimeType": "application/octet-stream", "buffer": b"SCP" + bytes(3_000_000)},
        "other": {"name": "other.adf", "mimeType": "application/octet-stream", "buffer": bytes(901120)},
    }
    ok = True
    for k, f in refused.items():
        pg.set_input_files("#file", files=[f])
        pg.wait_for_function("document.getElementById('error').textContent.length > 0", polling=200)
        ok &= pg.is_hidden("#start") and len(pg.text_content("#error")) > 0
        pg.evaluate("document.getElementById('error').textContent = ''")
    pg.set_input_files("#file", files=[str(adf)])
    pg.wait_for_function("!document.getElementById('start').hidden || document.getElementById('error').textContent.length > 0", timeout=180000, polling=200)
    ok &= pg.is_visible("#start") and pg.text_content("#error") == ""
    record(engine, "pick", ok, f"progress: {pg.text_content('#progress')!r}")
    pg.screenshot(path=str(out / f"{engine}-page-ready.png"))
    return pg, logs


def check_keys_fresh(engine, ctx, url, adf, keys):
    reached = {}
    for k in keys:
        pg = open_ready(ctx, url, adf)
        pg.click("#start")
        pg.wait_for_timeout(2500)
        before = canvas_hash(pg)
        pg.keyboard.press(k)
        pg.wait_for_timeout(1500)
        reached[k] = before != canvas_hash(pg)
        pg.close()
    record(engine, "keys", all(reached.values()), "left the credits: " + " ".join(f"{k}={'yes' if v else 'NO'}" for k, v in reached.items()))


def reach_save_point(pg, timeout_s=90):
    """Play the screens to the first save point of the flight log (the callsign entry's RETURN, note 08 10.5): RETURN until the page counted one."""
    pg.click("#start")
    pg.wait_for_timeout(3000)
    for _ in range(timeout_s):
        if pg.evaluate("window.interceptorPage.state.logSaves") > 0:
            return True
        pg.keyboard.press("Enter")
        pg.wait_for_timeout(1000)
    return pg.evaluate("window.interceptorPage.state.logSaves") > 0


def check_reload(engine, b, url, adf, out):
    ctx = b.new_context(viewport=VIEW)
    try:
        pg = open_ready(ctx, url, adf)
        before = pg.evaluate("window.interceptorPage.readLog()")
        saved = reach_save_point(pg)
        if saved:
            pg.wait_for_function("window.interceptorPage.state.syncs >= 1", timeout=30000, polling=200)
        written = pg.evaluate("window.interceptorPage.readLog()")
        pg.close()
        # the next visit: the same context (the same browser profile storage)
        pg2 = open_ready(ctx, url, adf)
        again = pg2.evaluate("window.interceptorPage.readLog()")
        pg2.close()
        # the control: a fresh context has no log
        ctx2 = b.new_context(viewport=VIEW)
        pg3 = open_ready(ctx2, url, adf)
        fresh = pg3.evaluate("window.interceptorPage.readLog()")
        pg3.close()
        ctx2.close()
        ok = before is None and saved and written is not None and written[:4] == [0x49, 0x4C, 0x4F, 0x47] and again == written and fresh is None
        record(engine, "reload", ok, f"first visit had no log: {before is None}; save point reached: {saved}; log bytes written {len(written or [])}; "
                                     f"read back in the next visit: {again == written}; absent in a fresh context: {fresh is None}")
    finally:
        ctx.close()


def check_nostorage(engine, b, url, adf, out):
    ctx = b.new_context(viewport=VIEW)
    try:
        ctx.add_init_script(NO_STORAGE_INIT)
        pg = open_ready(ctx, url, adf)
        note = pg.text_content("#storage")
        lines = [l for l in note.split("\n") if l.strip()]
        saved = reach_save_point(pg)
        written = pg.evaluate("window.interceptorPage.readLog()")
        err = pg.text_content("#error")
        pg.screenshot(path=str(out / f"{engine}-nostorage.png"))
        ok = len(lines) == 1 and saved and written is not None and err == "" and not pg.evaluate("window.interceptorPage.state.persistent")
        record(engine, "nostorage", ok, f"message lines: {len(lines)}; the run reached a save point: {saved}; in-memory log {len(written or [])} bytes; error text empty: {err == ''}")
        pg.close()
    finally:
        ctx.close()


def check_gamepad(engine, b, url, adf, out):
    ctx = b.new_context(viewport=VIEW)
    try:
        ctx.add_init_script(GAMEPAD_INIT)
        pg = open_ready(ctx, url, adf)
        pg.click("#start")
        pg.wait_for_timeout(1500)
        pg.evaluate("window.__announcePad()")
        # the credits type and fade in: wait until the picture stands still, then watch it for 3 s (the control) before the press
        last, stable = None, False
        for _ in range(40):
            h = canvas_hash(pg)
            if h == last:
                stable = True
                break
            last = h
            pg.wait_for_timeout(500)
        pg.wait_for_timeout(3000)
        still = stable and canvas_hash(pg) == last
        pg.evaluate("window.__padA = true")
        pg.wait_for_timeout(3000)
        left = canvas_hash(pg) != last
        pg.evaluate("window.__padA = false")
        record(engine, "gamepad", still and left, f"the credits stood still for 3 s before the press: {still}; left after button 0 (A): {left}")
        pg.close()
    finally:
        ctx.close()


def check_audio(engine, p, url, adf, out):
    b = p.chromium.launch(args=["--disable-gpu", "--no-zygote", "--disable-dev-shm-usage", "--autoplay-policy=no-user-gesture-required",
                                "--use-fake-ui-for-media-stream", "--use-fake-device-for-media-stream"])
    try:
        ctx = b.new_context(viewport=VIEW)
        ctx.add_init_script(AUDIO_INIT)
        pg = open_ready(ctx, url, adf)
        pre = pg.evaluate("window.__audioProcess")
        pg.click("#start")
        pg.wait_for_timeout(3000)
        state = pg.evaluate("(window.__audioCtxs || []).map(c => c.state)")
        n1 = pg.evaluate("window.__audioProcess")
        pg.wait_for_timeout(2000)
        n2 = pg.evaluate("window.__audioProcess")
        record(engine, "audio", pre == 0 and state and all(s == "running" for s in state) and n2 > n1 > 0,
               f"AudioContext states {state}; ScriptProcessor callbacks {pre} before Start, {n1} then {n2}")
        pg.close()
        ctx.close()
    finally:
        b.close()


def launch(p, name):
    if name == "chromium":
        return p.chromium.launch(args=["--disable-gpu", "--no-zygote", "--disable-dev-shm-usage"])
    env = dict(os.environ)
    if name == "firefox":   # its own libnss3 and friends must win over the ones in LD_LIBRARY_PATH (older, for Chromium)
        env["LD_LIBRARY_PATH"] = os.path.dirname(p.firefox.executable_path) + ":" + env.get("LD_LIBRARY_PATH", "")
    if name == "webkit":   # the sandbox forbids memfd_create and /dev/shm: tools/web/shmshim.c stands in (tools/web/README.md)
        shim = os.environ.get("SHMSHIM", "/sandbox/pwlibs/lib/libshmshim.so")
        if os.path.exists(shim):
            env["LD_PRELOAD"] = shim
    return getattr(p, name).launch(env=env)


def run_engine(p, name, url, adf, out, quick):
    try:
        b = launch(p, name)
    except Exception as e:   # the engine cannot run in this sandbox: say why and go on
        lines = [l.strip(" -") for l in str(e).splitlines()]
        useful = [l.split("] ", 1)[-1] for l in lines if "[err]" in l or "did exit" in l or "error while loading" in l]
        reason = "; ".join(dict.fromkeys(useful))[:300] or (lines[0][:200] if lines and lines[0] else type(e).__name__)
        print(f"[{name}] SKIPPED: cannot launch ({reason})", flush=True)
        results.setdefault(name, {})["skipped"] = (True, reason)
        return
    try:
        pg, logs = check_pick(name, b, url, adf, out)
        if not frames_tick(pg):
            why = "requestAnimationFrame does not run in this headless engine here (no display), so the game's main loop cannot run"
            for c in ("start", "keys", "reload", "nostorage", "gamepad"):
                record(name, c, None, why)
            pg.close()
            return
        pg.click("#start")
        pg.wait_for_timeout(4000)
        pg.screenshot(path=str(out / f"{name}-credits.png"))
        pg.locator("#canvas").screenshot(path=str(out / f"{name}-canvas-credits.png"))
        record(name, "start", len(set(pg.locator("#canvas").screenshot())) > 20 and not any(l.startswith("pageerror") for l in logs), "the credits drew")
        if not quick:
            pg.evaluate("window.__keys = []; window.addEventListener('keydown', e => window.__keys.push([e.code, e.defaultPrevented]))")
            keys = ["F1", "F2", "F3", "F4", "F6", "F7", "F8", "F9", "F10", "Numpad1", "Numpad5", "NumpadEnter", "NumpadAdd", "Backspace", "Space", "ShiftLeft", "Comma"]
            for k in keys:
                pg.keyboard.press(k)
                pg.wait_for_timeout(60)
            print(f"[{name}] keys consumed (preventDefault):", pg.evaluate("window.__keys.map(k => k[0] + (k[1] ? '=yes' : '=NO')).join(' ')"), flush=True)
        print(f"[{name}] console:", logs[:8], flush=True)
        pg.close()
        if not quick:
            ctx = b.new_context(viewport=VIEW)
            check_keys_fresh(name, ctx, url, adf, ["F1", "Numpad5", "NumpadEnter", "Backspace", "Comma"])
            ctx.close()
        for fn in (check_reload, check_nostorage, check_gamepad):
            try:
                fn(name, b, url, adf, out)
            except Exception as e:
                record(name, fn.__name__.replace("check_", ""), False, f"{type(e).__name__}: {str(e).splitlines()[0][:200] if str(e) else ''}")
    finally:
        b.close()
    if name == "chromium":
        try:
            check_audio(name, p, url, adf, out)
        except Exception as e:
            record(name, "audio", False, f"{type(e).__name__}: {str(e).splitlines()[0][:200] if str(e) else ''}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("adf")
    ap.add_argument("out")
    ap.add_argument("--engines", default="chromium,firefox,webkit")
    ap.add_argument("--quick", action="store_true")
    a = ap.parse_args()
    adf = Path(a.adf).resolve()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args, **k):
            pass
    handler = functools.partial(Quiet, directory=str(ROOT / "build" / "web"))
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{srv.server_address[1]}/index.html"
    with sync_playwright() as p:
        for name in a.engines.split(","):
            run_engine(p, name.strip(), url, adf, out, a.quick)
    srv.shutdown()
    print("\n== report ==")
    failed = False
    for eng, checks in results.items():
        if "skipped" in checks:
            print(f"{eng}: SKIPPED ({checks['skipped'][1]})")
            continue
        print(f"{eng}: " + ", ".join(f"{n} {'skipped' if ok is None else ('ok' if ok else 'FAILED')}" for n, (ok, _) in checks.items()))
        failed |= any(ok is False for ok, _ in checks.values())
        for n, (ok, why) in checks.items():
            if ok is None:
                print(f"    {n} skipped: {why}")
    sys.exit(1 if failed else 0)


main()
