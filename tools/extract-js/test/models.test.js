import test from "node:test";
import assert from "node:assert/strict";
import { decodeModel, writeModel, writeModels, V_BLOCK, E_DRAW } from "../src/models.js";
import { BlockedError, DecodeError, IMAGE_BASE } from "../src/tables.js";
import { syntheticImage, expectedFiles } from "./helpers.js";

const text = (b) => new TextDecoder().decode(b);

test("the synthetic image has 133 empty records and the one invented landmark model (no model program: failed): the index and no model files equal the Python output", async () => {
  const files = new Map();
  const counts = writeModels(await syntheticImage(), files);
  assert.deepEqual(counts, { ok: 0, partial: 0, empty: 133, failed: 1 });
  assert.deepEqual([...files.keys()], ["models/index.txt"]);
  assert.equal(text(files.get("models/index.txt")), text(expectedFiles().get("models/index.txt")));
});

// The image of tools/extract/tests/test_cli.py model_exe(): records at 0x25ECC; id 0 decodes, id 1 has an unknown operation,
// id 2 has a BLOCKING line (slot 20 is left unresolved by op 42 over a block without its sources); the rest are zero.
function modelImage() {
  const img = new Uint8Array(0x3c000);
  const dv = new DataView(img.buffer);
  const at = (a) => a - IMAGE_BASE;
  const words = (a, ...w) => w.forEach((v, i) => dv.setUint16(at(a) + 2 * i, v, false));
  [[0, 0x30000, [0xc008, 0, 6, 3]], [1, 0x30100, [0xc014]], [2, 0x30200, [0x80a8, 0, 0xc00c, 0, 6, 0x8078, 0x8001]]].forEach(([i, base, prog]) => {
    const rec = 0x25ecc + 20 * i;
    [0x21c44, base, base, base, 0].forEach((v, k) => dv.setUint32(at(rec) + 4 * k, v, false));
    words(base, 0x8080, 0xa010);
    words(base + 0x10, ...prog);
    const nv = i === 2 ? 12 : 6;
    words(base + 0x80, 0, 0x100, 0x200);
    img.set([0x23, 1, nv, nv], at(base + 0x86));
    words(base + 0x8a, ...new Array(3 * nv).fill(1));
  });
  return img;
}

test("a model that fails does not abort the run and is listed in the index with its reason", () => {
  const files = new Map();
  const counts = writeModels(modelImage(), files);
  assert.deepEqual(counts, { ok: 1, partial: 1, empty: 131, failed: 1 });
  const rows = text(files.get("models/index.txt")).split("\n").filter((l) => l && !l.startsWith("#")).map((l) => l.split("\t"));
  assert.equal(rows.length, 134);
  assert.deepEqual(rows[0].slice(0, 3), ["0", "model_000.bin", "ok"]);
  assert.equal(rows[1][1], "-");
  assert.ok(rows[1][2].startsWith("failed: model 1: op 5 at 0x30110: unknown operation 5"), rows[1][2]);
  assert.equal(rows[2][1], "model_002.bin");
  assert.ok(rows[2][2].startsWith("partial: model 2: op 3 at 0x30214: BLOCKING"), rows[2][2]);
  assert.ok(rows[3][2].startsWith("empty:"));
  assert.equal(text(files.get("models/model_000.bin").subarray(0, 4)), "IMDL");
  assert.ok(!files.has("models/model_001.bin"));
});

test("strict decoding refuses a BLOCKING item with BlockedError; lenient decoding flags it", () => {
  const img = modelImage();
  assert.throws(() => decodeModel(img, 0x25ecc - IMAGE_BASE + 40, 2), (e) => e instanceof BlockedError);
  const m = decodeModel(img, 0x25ecc - IMAGE_BASE + 40, 2, IMAGE_BASE, false);
  assert.ok(m.blocked > 0);
  assert.throws(() => decodeModel(img, 0x25ecc - IMAGE_BASE + 60, 3), (e) => e instanceof DecodeError && /^model 3: empty record/.test(e.message));
});

test("the first model: one level, one block vertex per slot, IMDL header fields", () => {
  const m = decodeModel(modelImage(), 0x25ecc - IMAGE_BASE, 0);
  assert.equal(m.levels.length, 1);
  assert.equal(m.levels[0].b8, 6);
  assert.equal(m.vertices.length, 6);
  assert.ok(m.vertices.every((v) => v.state === V_BLOCK));
  assert.equal(m.elements[m.levels[0].root].kind, E_DRAW);
  const bytes = writeModel(m);
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  assert.equal(dv.getUint16(4, true), 5, "IMDL version");
  assert.equal(dv.getUint32(12, true), 0x21c44, "handler");
  assert.equal(dv.getUint32(16, true), 1, "level count");
  assert.equal(bytes.length, 60 + 54 + 16 * 6 + 32 + 4 + 12 + 40 + 4 * m.operands.length + 4 * m.skips.length + 4 * m.secpool.length);
});
