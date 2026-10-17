// The whole output of the extractor on the synthetic disk against the Python extractor's (tools/extract/tests/write_synth.py).
import test from "node:test";
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, readFileSync, readdirSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { extract, EXTRACTOR_VERSION } from "../src/index.js";
import { expectedFiles, fixtureBytes, fixtures, pythonAvailable, root } from "./helpers.js";

test("the file set, every byte and the manifest equal the Python extractor's", async () => {
  const { files, manifest } = await extract(fixtureBytes("synth.adf"), { allowFlatSpans: true });
  const expected = expectedFiles();
  assert.deepEqual([...files.keys()].sort(), [...expected.keys()].sort());
  const differing = [];
  for (const [name, bytes] of expected) {
    if (Buffer.compare(Buffer.from(files.get(name)), Buffer.from(bytes)) !== 0) differing.push(name);
  }
  assert.deepEqual(differing, []);
  assert.equal(manifest.extractor_version, EXTRACTOR_VERSION);
  assert.equal(manifest.spans_relocated, false);
  assert.deepEqual(manifest, JSON.parse(new TextDecoder().decode(expected.get("manifest.json"))));
});

test("the extractor version is the Python one's", () => {
  const py = readFileSync(path.join(root, "tools", "extract", "extract", "manifest.py"), "utf8");
  assert.equal(Number(/^EXTRACTOR_VERSION = (\d+)$/m.exec(py)[1]), EXTRACTOR_VERSION);
  assert.equal(JSON.parse(readFileSync(path.join(root, "tools", "extract-js", "package.json"), "utf8")).version, `${EXTRACTOR_VERSION}.0.0`);
});

// The committed expected output must still be what the Python extractor writes: when Python (with amitools) is available the
// extractor is run on the committed disk and compared with the tar. A Python change that is not mirrored shows up here and in the
// corpus; regenerate the fixtures with tools/extract/tests/write_synth.py and write_corpus.py when both ports changed.
test("the committed expected output is what the Python extractor writes now", { skip: !pythonAvailable() && "python with extract and amitools not available" }, () => {
  const dir = mkdtempSync(path.join(tmpdir(), "synth-py-"));
  try {
    const out = path.join(dir, "assets");
    const r = spawnSync("python", ["-m", "extract", path.join(fixtures, "synth.adf"), out, "--allow-flat-spans"], {
      encoding: "utf8", env: { ...process.env, PYTHONPATH: path.join(root, "tools", "extract") },
    });
    assert.equal(r.status, 0, r.stderr);
    const expected = expectedFiles();
    const seen = new Set();
    const walk = (d, rel) => {
      for (const e of readdirSync(d, { withFileTypes: true })) {
        const p = rel ? `${rel}/${e.name}` : e.name;
        if (e.isDirectory()) {
          walk(path.join(d, e.name), p);
        } else {
          seen.add(p);
          assert.ok(expected.has(p), `Python wrote ${p}, which the expected tar lacks`);
          assert.ok(Buffer.compare(readFileSync(path.join(d, e.name)), Buffer.from(expected.get(p))) === 0, `${p} differs from the committed expected output`);
        }
      }
    };
    walk(out, "");
    assert.deepEqual([...seen].sort(), [...expected.keys()].sort());
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
