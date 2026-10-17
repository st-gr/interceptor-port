// The AmigaDOS reader on invented disks: multi-block files, extension blocks, hash chains, FFS, damaged blocks.
import test from "node:test";
import assert from "node:assert/strict";
import { Adf } from "../src/adf.js";
import { buildAdf } from "./adfbuilder.js";
import { spawnSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { createHash } from "node:crypto";
import { pythonAvailable, root } from "./helpers.js";

const pattern = (n, seed = 1) => Uint8Array.from({ length: n }, (_, i) => (i * 7 + seed + (i >> 8)) & 255);

test("files of many sizes read back byte for byte, with extension blocks beyond 72 data blocks (OFS)", () => {
  const files = {
    "empty": new Uint8Array(0),
    "one": Uint8Array.of(9),
    "exact": pattern(488),
    "text/odd": pattern(1001),
    "big": pattern(488 * 150 + 17),     // 151 data blocks: two extension blocks... one extension block of 72, then a second
    "huge": pattern(488 * 300),         // 300 blocks: three extension blocks
  };
  const disk = new Adf(buildAdf(files));
  for (const [name, data] of Object.entries(files)) assert.deepEqual(disk.read(name), data, name);
});

test("FFS data blocks are raw", () => {
  const files = { "a": pattern(512 * 3 + 5), "dir/b": pattern(700, 3) };
  const disk = new Adf(buildAdf(files, { ffs: true }));
  for (const [name, data] of Object.entries(files)) assert.deepEqual(disk.read(name), data, name);
});

test("directory order is the hash table order, with hash chains after the table entries", () => {
  // names that hash to the same slot are chained; the listing visits the table slots first, then the chains
  const names = ["a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"];
  const files = Object.fromEntries(names.map((n, i) => [`d/${n}${n}${n}`, Uint8Array.of(i)]));
  const disk = new Adf(buildAdf(files));
  const listed = disk.listdir("d");
  assert.deepEqual([...listed].sort(), Object.keys(files).map((p) => p.slice(2)).sort());
  assert.deepEqual(disk.listdir("D"), listed, "directory names compare without regard to case");
});

test("a damaged data block or a damaged directory is reported, not read past", () => {
  const bytes = buildAdf({ "f": pattern(2000) });
  const disk = new Adf(bytes);
  const hdrBlock = disk._entries(disk.root)[0].block;
  const firstData = hdrBlock.long(-51);
  const damaged = bytes.slice();
  damaged[firstData * 512 + 100] ^= 0xff;    // the checksum no longer adds up
  assert.throws(() => new Adf(damaged).read("f"), /damaged data block/);
  const badDir = bytes.slice();
  badDir[hdrBlock.o + 40] ^= 1;
  assert.throws(() => new Adf(badDir).read("f"), /directory is damaged/);
});

test("amitools (the Python extractor's reader) lists and reads the invented disks the same way", { skip: !pythonAvailable() && "python with extract and amitools not available" }, () => {
  const names = Array.from({ length: 60 }, (_, i) => `file${i * 37 % 101}`);
  const files = Object.fromEntries(names.map((n, i) => [`d/${n}`, pattern(10 + i * 90, i)]));
  files["big"] = pattern(488 * 200 + 3);
  const dir = mkdtempSync(path.join(tmpdir(), "adf-"));
  try {
    const adfPath = path.join(dir, "t.adf");
    writeFileSync(adfPath, buildAdf(files));
    const script = "import sys, json, hashlib\nfrom extract.adf import Adf\nd = Adf(sys.argv[1])\n"
      + "print(json.dumps([d.listdir('d'), hashlib.sha256(d.read('big')).hexdigest()]))";
    const r = spawnSync("python", ["-c", script, adfPath], { encoding: "utf8", env: { ...process.env, PYTHONPATH: path.join(root, "tools", "extract") } });
    assert.equal(r.status, 0, r.stderr);
    const [order, digest] = JSON.parse(r.stdout);
    const disk = new Adf(buildAdf(files));
    assert.deepEqual(disk.listdir("d"), order);
    assert.equal(createHash("sha256").update(disk.read("big")).digest("hex"), digest);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
