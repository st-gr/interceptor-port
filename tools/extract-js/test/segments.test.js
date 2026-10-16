// The browser's segment table (tools/extract-js/segments.txt): the loader's RAM bases of the reference machine, addresses only.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import { extract } from "../src/index.js";
import { fixtureBytes, root } from "./helpers.js";

const table = readFileSync(path.join(root, "tools", "extract-js", "segments.txt"), "utf8");

test("the committed table holds comments and hunk lines only: no bytes of the game", () => {
  for (const line of table.split("\n")) {
    if (line === "" || line.startsWith("#")) continue;
    assert.match(line, /^seglist hunks \d+ of \d+ hunk0 [0-9A-F]+$|^seg \d+ flat [0-9A-F]+ ram [0-9A-F]+ size \d+ segsize \d+ ok$/);
  }
});

test("extract with the table on the synthetic disk relocates the spans", async () => {
  const { manifest, files } = await extract(fixtureBytes("synth.adf"), { segments: table });
  assert.equal(manifest.spans_relocated, true);
  assert.ok(!files.has("tables/flight_spans_flat.txt"));
});
