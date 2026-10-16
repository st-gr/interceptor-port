// The page's logic apart from the DOM (src/frontend/web/page.js): the ADF to MEMFS path, with a fake FS, for the success case and
// the refusals of Review Focus 3 (not an ADF, a flux image, another disk). A refusal creates nothing and leaves the page usable.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import { extract } from "../src/index.js";
import { loadAdf, writeAssets, errorMessage, stageText, unlockAudio } from "../../../src/frontend/web/page.js";
import { buildAdf } from "./adfbuilder.js";
import { fixtureBytes, root } from "./helpers.js";

const segments = readFileSync(path.join(root, "tools", "extract-js", "segments.txt"), "utf8");

function fakeFs() {
  const dirs = new Set(["/"]), files = new Map();
  return {
    dirs, files,
    mkdirTree(d) { let p = ""; for (const part of d.split("/").filter(Boolean)) { p += "/" + part; dirs.add(p); } },
    writeFile(f, bytes) {
      const d = f.slice(0, f.lastIndexOf("/"));
      if (!dirs.has(d)) throw new Error("no directory " + d);
      assert.ok(bytes instanceof Uint8Array);
      files.set(f, bytes);
    },
  };
}

test("success: every extracted file is created under /assets, progress reported", async () => {
  const fs = fakeFs();
  const stages = [];
  const r = await loadAdf({ bytes: fixtureBytes("synth.adf"), extract, segments, fs, onProgress: (s) => stages.push(s) });
  assert.equal(r.ok, true);
  assert.equal(fs.files.size, r.count);
  assert.ok(fs.files.has("/assets/manifest.json"));
  assert.equal(r.manifest.spans_relocated, true);
  assert.equal(stages[0], "disk");
  assert.equal(stages.at(-1), "writing");
  for (const s of stages) assert.match(stageText(s), /\.\.\.$/);
});

async function refusedCase(bytes, pattern) {
  const fs = fakeFs();
  const r = await loadAdf({ bytes, extract, segments, fs });
  assert.equal(r.ok, false);
  assert.match(r.message, pattern);
  assert.equal(fs.files.size, 0, "no asset is created");
  assert.deepEqual([...fs.dirs], ["/"], "no directory either");
  // the page stays usable: the next pick works on the same file system
  const again = await loadAdf({ bytes: fixtureBytes("synth.adf"), extract, segments, fs });
  assert.equal(again.ok, true);
  assert.ok(fs.files.size > 0);
  return r.message;
}

test("not an ADF: the extractor's message verbatim, nothing created", async () => {
  const bytes = new TextEncoder().encode("hello");
  const m = await refusedCase(bytes, /too short \(5 bytes\).*ADF disk image/);
  await assert.rejects(extract(bytes, { segments }), (e) => e.message === m);
});

test("a flux image is refused by name", async () => {
  const scp = new Uint8Array(3_000_000);
  scp.set(new TextEncoder().encode("SCP"));
  await refusedCase(scp, /SCP/);
});

test("another disk (an ADF without the game's files) is refused", async () => {
  const other = buildAdf({ "readme": new TextEncoder().encode("another disk") });
  await refusedCase(other, /not the F\/A-18 Interceptor disk/);
});

test("an unexpected error is wrapped in a plain sentence; a failing FS is reported, not thrown", async () => {
  assert.match(errorMessage(new TypeError("boom")), /Something went wrong.*boom/);
  assert.match(errorMessage("odd"), /odd/);
  const fs = fakeFs();
  fs.writeFile = () => { throw new Error("disk full"); };
  const r = await loadAdf({ bytes: fixtureBytes("synth.adf"), extract, segments, fs });
  assert.equal(r.ok, false);
  assert.match(r.message, /disk full/);
});

test("writeAssets creates nested directories", () => {
  const fs = fakeFs();
  writeAssets(fs, new Map([["a/b/c.bin", new Uint8Array(1)], ["a/d.bin", new Uint8Array(2)], ["e.txt", new Uint8Array(0)]]));
  assert.deepEqual([...fs.files.keys()].sort(), ["/assets/a/b/c.bin", "/assets/a/d.bin", "/assets/e.txt"]);
});

test("the audio unlock is called when the build has it, else nothing happens", () => {
  let n = 0;
  unlockAudio({ _web_audio_unlock: () => n++ });
  unlockAudio({});
  unlockAudio(null);
  assert.equal(n, 1);
});

// ---- the flight log's storage (milestone 7, Task 7): a stand-in FS whose syncfs and mount are scripted ----
import { mountLogStore, makeSyncer, LOG_DIR } from "../../../src/frontend/web/page.js";

function logFs({ idbfs = {}, loadError = null } = {}) {
  const calls = [];
  return {
    calls, filesystems: idbfs ? { IDBFS: idbfs } : {},
    mkdirTree(d) { calls.push(["mkdirTree", d]); },
    mount(type, opts, dir) { calls.push(["mount", dir]); },
    syncfs(populate, cb) { calls.push(["syncfs", populate]); cb(populate ? loadError : null); },
  };
}

test("log storage: mounted at the log directory and loaded before the program starts", async () => {
  const FS = logFs();
  const r = await mountLogStore({ FS });
  assert.deepEqual(r, { persistent: true });
  assert.deepEqual(FS.calls, [["mkdirTree", LOG_DIR], ["mount", LOG_DIR], ["syncfs", true]]);
});

test("log storage: a refused load, a missing IDBFS and a throwing mount are not fatal and leave the game in memory", async () => {
  assert.deepEqual(await mountLogStore({ FS: logFs({ loadError: new Error("denied") }) }), { persistent: false });
  assert.deepEqual(await mountLogStore({ FS: logFs({ idbfs: null }) }), { persistent: false });
  const FS = logFs(); FS.mount = () => { throw new Error("no storage"); };
  assert.deepEqual(await mountLogStore({ FS }), { persistent: false });
});

test("log storage: an open that never answers falls back to the in-memory log after the timeout", async () => {
  const FS = logFs(); FS.syncfs = () => {};   // the callback is never called
  const t0 = Date.now();
  assert.deepEqual(await mountLogStore({ FS }, LOG_DIR, 40), { persistent: false });
  assert.ok(Date.now() - t0 >= 30);
  // a late answer after the timeout changes nothing, and a prompt answer is not delayed by the timer
  let late; const FS2 = logFs(); FS2.syncfs = (p, cb) => { late = cb; };
  const r = await mountLogStore({ FS: FS2 }, LOG_DIR, 10);
  late(null);
  assert.deepEqual(r, { persistent: false });
  assert.deepEqual(await mountLogStore({ FS: logFs() }, LOG_DIR, 60000), { persistent: true });
});

test("log storage: one write at a time, a request during one is run after it, an error is reported", () => {
  const pending = [], seen = [];
  const FS = { syncfs(populate, cb) { assert.equal(populate, false); pending.push(cb); } };
  const request = makeSyncer(FS, (err) => seen.push(err ? err.message : "ok"));
  request(); request(); request();
  assert.equal(pending.length, 1);
  pending.shift()(null);
  assert.equal(pending.length, 1);   // the two requests during the first became one more write
  pending.shift()(new Error("quota"));
  assert.equal(pending.length, 0);
  assert.deepEqual(seen, ["ok", "quota"]);
});
