import test from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { EXTRACTOR_VERSION, buildManifest, sha256Hex, sha256Pure } from "../src/manifest.js";

const hex = (b) => Buffer.from(b).toString("hex");

test("the pure SHA-256 gives the standard digests and agrees with Node's at every padding boundary", () => {
  assert.equal(hex(sha256Pure(new Uint8Array(0))), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  assert.equal(hex(sha256Pure(new TextEncoder().encode("abc"))), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  for (const n of [1, 54, 55, 56, 57, 63, 64, 65, 119, 120, 127, 128, 1000, 70000]) {
    const data = Uint8Array.from({ length: n }, (_, i) => (i * 31 + n) & 255);
    assert.equal(hex(sha256Pure(data)), createHash("sha256").update(data).digest("hex"), `length ${n}`);
  }
});

test("sha256Hex uses Web Crypto when the page has it and the pure code when it has not", async () => {
  const data = Uint8Array.of(1, 2, 3);
  const want = createHash("sha256").update(data).digest("hex");
  assert.equal(await sha256Hex(data), want);
  const saved = Object.getOwnPropertyDescriptor(globalThis, "crypto");
  Object.defineProperty(globalThis, "crypto", { value: undefined, configurable: true });
  try {
    assert.equal(await sha256Hex(data), want);
  } finally {
    if (saved) Object.defineProperty(globalThis, "crypto", saved);
  }
});

test("the manifest lists the files in sorted order with their hashes, 2-space JSON and a final newline, as Python writes it", async () => {
  const files = new Map([["tables/b.bin", Uint8Array.of(2)], ["VERSION", new TextEncoder().encode("27\n")], ["pix/a.png", Uint8Array.of(1)], ["manifest.json", Uint8Array.of(9)]]);
  const { manifest, bytes } = await buildManifest(files, "ab".repeat(32), { a: 9908 }, false);
  assert.deepEqual(Object.keys(manifest), ["extractor_version", "source_sha256", "sample_rate_hz", "spans_relocated", "files"]);
  assert.equal(manifest.extractor_version, EXTRACTOR_VERSION);
  assert.deepEqual(Object.keys(manifest.files), ["VERSION", "pix/a.png", "tables/b.bin"], "manifest.json is not listed; upper case sorts first");
  assert.equal(manifest.files["pix/a.png"], createHash("sha256").update(Uint8Array.of(1)).digest("hex"));
  const text = new TextDecoder().decode(bytes);
  assert.ok(text.endsWith("}\n"));
  assert.ok(text.startsWith('{\n  "extractor_version": 27,\n  "source_sha256": "'));
  assert.deepEqual(JSON.parse(text), manifest);
  assert.deepEqual(manifest.sample_rate_hz, { a: 9908 });
});
