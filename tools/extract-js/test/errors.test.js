// Review focus 3 of milestone 6: the browser is given a file that is not an ADF, an ADF of another disk, or a flux image.
// Expected: an ExtractError whose message names what was expected, and no assets.
import test from "node:test";
import assert from "node:assert/strict";
import { extract, ExtractError } from "../src/index.js";
import { Adf } from "../src/adf.js";
import { buildAdf } from "./adfbuilder.js";
import { fixtureBytes } from "./helpers.js";

async function refused(bytes, options) {
  let error = null;
  let result = null;
  try {
    result = await extract(bytes, options);
  } catch (e) {
    error = e;
  }
  assert.equal(result, null, "no assets are produced");
  assert.ok(error instanceof ExtractError, `an ExtractError, got ${error && error.stack}`);
  assert.equal(error.name, "ExtractError");
  return error.message;
}

test("not an ADF: a short file, a file of the wrong size, a disk without an AmigaDOS root block", async () => {
  assert.match(await refused(new Uint8Array(0)), /too short \(0 bytes\).*ADF disk image.*901,120 bytes/);
  assert.match(await refused(new TextEncoder().encode("hello, this is a text file")), /too short.*ADF/);
  assert.match(await refused(new Uint8Array(901120 + 512)), /901,120 bytes/);
  assert.match(await refused(new Uint8Array(901120)), /not an AmigaDOS disk.*ADF/);
  const noRoot = fixtureBytes("synth.adf").slice();
  noRoot.fill(0, 880 * 512, 881 * 512);
  assert.match(await refused(noRoot), /root block at block 880/);
  assert.match(await refused("a string"), /bytes of an ADF/);
});

test("a flux image is refused by name: SCP, HFE, MFI", async () => {
  const flux = (magic, size = 3_000_000) => {
    const b = new Uint8Array(size);
    b.set(new TextEncoder().encode(magic));
    return b;
  };
  assert.match(await refused(flux("SCP\u0000\u0080\u0005")), /SCP flux image, not an ADF/);
  assert.match(await refused(flux("HXCPICFE")), /HFE \(HxC\) flux image, not an ADF/);
  assert.match(await refused(flux("HXCHFEV3\u0000")), /HFE \(HxC\) flux image, not an ADF/);
  assert.match(await refused(flux("MAMEFLOPPYIMAGE\u0000", 1_000_000)), /MFI \(MAME\) flux image, not an ADF/);
  assert.match(await refused(flux("SCP", 901120)), /SCP flux image/, "even at the size of an ADF");
});

test("an ADF of another disk: the game's files are missing", async () => {
  const other = buildAdf({ "readme": new TextEncoder().encode("another disk"), "s/startup-sequence": new TextEncoder().encode("echo hi") });
  const m = await refused(other, { allowFlatSpans: true });
  assert.match(m, /not the F\/A-18 Interceptor disk/);
  assert.match(m, /file pix\/splsh not found on the disk/);
  assert.match(m, /pix\/ and text\/ directories and the executable "F-18 Interceptor"/);
});

test("an ADF with the game's pictures and samples but another executable: the hunk signature is missing", async () => {
  const synth = new Adf(fixtureBytes("synth.adf"));
  const files = { "F-18 Interceptor": new TextEncoder().encode("this is not an Amiga executable, but a text file") };
  for (const n of ["splsh", "inst5", "frnt5"]) files[`pix/${n}`] = synth.read(`pix/${n}`);
  for (const n of synth.listdir("text")) files[`text/${n}`] = synth.read(`text/${n}`);
  const m = await refused(buildAdf(files), { allowFlatSpans: true });
  assert.match(m, /not the F\/A-18 Interceptor disk.*"F-18 Interceptor" is not an Amiga executable.*no HUNK_HEADER/);
});

test("an executable that is a hunk file but lacks the game's tables is refused naming the table", async () => {
  const synth = new Adf(fixtureBytes("synth.adf"));
  const files = {};
  for (const n of ["splsh", "inst5", "frnt5"]) files[`pix/${n}`] = synth.read(`pix/${n}`);
  for (const n of synth.listdir("text")) files[`text/${n}`] = synth.read(`text/${n}`);
  const exe = synth.read("F-18 Interceptor");
  files["F-18 Interceptor"] = exe.subarray(0, 0x20);   // a truncated hunk file
  const m = await refused(buildAdf(files), { allowFlatSpans: true });
  assert.match(m, /not the F\/A-18 Interceptor disk|does not hold the F\/A-18 Interceptor data/);
});

test("a damaged picture is reported as such; extraction without a segment table is refused unless flat spans are allowed", async () => {
  const synth = new Adf(fixtureBytes("synth.adf"));
  const files = { "F-18 Interceptor": synth.read("F-18 Interceptor") };
  for (const n of synth.listdir("text")) files[`text/${n}`] = synth.read(`text/${n}`);
  files["pix/splsh"] = new TextEncoder().encode("not a picture at all, just text");
  files["pix/inst5"] = synth.read("pix/inst5");
  files["pix/frnt5"] = synth.read("pix/frnt5");
  assert.match(await refused(buildAdf(files), { allowFlatSpans: true }), /does not hold the F\/A-18 Interceptor data.*not an IFF ILBM/);
  assert.match(await refused(fixtureBytes("synth.adf")), /no segment table: pass options\.segments.*allowFlatSpans/);
});

test("progress stages are reported in order", async () => {
  const stages = [];
  await extract(fixtureBytes("synth.adf"), { allowFlatSpans: true, onProgress: (s) => stages.push(s) });
  assert.deepEqual(stages, ["disk", "pictures", "sounds", "executable", "tables", "manifest"]);
});

test("with a segment table the manifest says the spans are relocated and no flat-spans note is written", async () => {
  const segments = "seg 0 flat 010000 ram C00000 size 3C000 segsize 3C008 ok\n";
  const { files, manifest } = await extract(fixtureBytes("synth.adf"), { segments });
  assert.equal(manifest.spans_relocated, true);
  assert.ok(!files.has("tables/flight_spans_flat.txt"));
  assert.ok(manifest.files["tables/flight_span_trig.bin"]);
});
