// Differential test against the Python decoders: test/fixtures/corpus.json holds thousands of calls the Python tests and
// a mutation fuzz made on invented images (tools/extract/tests/write_corpus.py), each with the SHA-256 of the canonical
// result or the exception text. Every call is replayed on the JavaScript decoder and must give the same digest or the same
// message. Images are stored sparsely; "mut" lists changed bytes of the base image and "cut" a truncation.
import test from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import path from "node:path";
import { fixtures } from "./helpers.js";
import { BlockedError, DecodeError, decodeCameraRing, decodeLodTable, decodeRangeTable, decodeRecord } from "../src/tables.js";
import { decodeFadeTable, decodeFont } from "../src/palette.js";
import { decodeIlbm, unpackByterun1 } from "../src/ilbm.js";
import { parse as hunkParse, relocate } from "../src/hunk.js";
import { decodeAll as skyDecodeAll } from "../src/sky.js";
import { decodeArcs, decodeMapshapes, decodeMarkers, decodeMasks, decodeWarnings } from "../src/cockpit.js";
import { decodeAll as flightDecodeAll, parseSegmentTable, referenceLongs } from "../src/flight.js";
import { decodeAll as listsDecodeAll, decodePatternLists } from "../src/lists.js";
import {
  decodeCellTable, decodeDefaultOffsets, decodeItemList, decodePositionBlocks, decodeTerrain, writeTerrain,
} from "../src/terrain.js";
import {
  decodeAi, decodeBriefingChain, decodeBurst, decodeHeadStep, decodeMonths, decodePrompts, decodeStar, decodeTargetText, decodeTemplates,
} from "../src/combat.js";
import { decodeDemo, decodeMenuFont, decodeScenario, decodeText, decodeTraining, paletteWords } from "../src/flow.js";
import { decodeModel, writeModel } from "../src/models.js";

const corpus = JSON.parse(readFileSync(path.join(fixtures, "corpus.json"), "utf8"));

const hex = (b) => Buffer.from(b).toString("hex");
const fromHex = (h) => Uint8Array.from(Buffer.from(h, "hex"));
const dictHex = (m) => Object.fromEntries([...m].map(([k, v]) => [k, hex(v)]));

function canon(x) {
  if (Array.isArray(x)) return "[" + x.map(canon).join(",") + "]";
  if (x !== null && typeof x === "object") {
    return "{" + Object.keys(x).sort().map((k) => JSON.stringify(k) + ":" + canon(x[k])).join(",") + "}";
  }
  return JSON.stringify(x);
}

const imageCache = new Map();
function baseImage(h) {
  if (!imageCache.has(h)) {
    const spec = corpus.images[h];
    const b = new Uint8Array(spec.len);
    for (const [off, runHex] of spec.runs) b.set(fromHex(runHex), off);
    imageCache.set(h, b);
  }
  return imageCache.get(h);
}

/** Decodes an argument of the corpus into a JavaScript value. */
function arg(a) {
  if (a !== null && typeof a === "object" && !Array.isArray(a)) {
    if ("image" in a) {
      let b = baseImage(a.image);
      if (a.mut && a.mut.length) {
        b = b.slice();
        for (const [off, v] of a.mut) b[off] = v;
      }
      if (a.cut !== undefined) b = b.slice(0, a.cut);
      return b;
    }
    if ("dict" in a) return new Map(a.dict.map(([k, v]) => [arg(k), arg(v)]));
  }
  if (Array.isArray(a)) return a.map(arg);
  return a;
}

const ilbmResult = (r) => [r.width, r.height, r.nplanes, r.palette, hex(r.pixels)];

// call name -> (decoded arguments) => the same normalised value the Python generator builds
const ADAPTERS = {
  "tables.decode_lod_table": (a) => hex(decodeLodTable(a[0], a[1])),
  "tables.decode_range_table": (a) => hex(decodeRangeTable(a[0], a[1])),
  "tables.decode_camera_ring": (a) => decodeCameraRing(a[0], a[1], a[2]),
  "tables.decode_record": (a) => { const r = decodeRecord(a[0], a[1], a[2]); return [r.id, r.handler, r.base, r.detail_list, r.alt_list, r.aux]; },
  "palette.decode_fade_table": (a) => decodeFadeTable(a[0], a[1], a[2], a[3]),
  "palette.decode_font": (a) => decodeFont(a[0], a[1], a[2]).map(hex),
  "ilbm.decode_ilbm": (a) => ilbmResult(decodeIlbm(a[0])),
  "ilbm.unpack_byterun1": (a) => hex(unpackByterun1(a[0], a[1])),
  "hunk.parse": (a) => {
    const r = hunkParse(a[0]);
    return [r.sizes, r.types, r.contents.map(hex), r.relocs, r.symbols];
  },
  "hunk.relocate": (a) => {
    const { image, info } = relocate(a[0], a[1]);
    return [hex(image), info.reloc_entries, info.reloc_count, info.hunks.map((h) => [h.index, h.type, h.addr, h.size]),
      info.symbols.map((s) => [s.name, s.addr, s.hunk])];
  },
  "sky.decode_all": (a) => dictHex(skyDecodeAll(a[0], a[1])),
  "cockpit.decode_warnings": (a) => hex(decodeWarnings(a[0], a[1])),
  "cockpit.decode_masks": (a) => hex(decodeMasks(a[0], a[1])),
  "cockpit.decode_markers": (a) => hex(decodeMarkers(a[0], a[1], a[2])),
  "cockpit.decode_arcs": (a) => hex(decodeArcs(a[0], a[1])),
  "cockpit.decode_mapshapes": (a) => hex(decodeMapshapes(a[0], a[1])),
  "flight.decode_all": (a) => dictHex(flightDecodeAll(a[0], a[1], a[2])),
  "flight.parse_segment_table": (a) => Object.fromEntries([...parseSegmentTable(a[0])].map(([k, v]) => [String(k), v])),
  "flight.reference_longs": (a) => referenceLongs(a[0], a[1], a[2]),
  "lists.decode_all": (a) => dictHex(listsDecodeAll(a[0], a[1])),
  "lists.decode_pattern_lists": (a) => decodePatternLists(a[0], a[1], a[2]),
  "terrain.decode_terrain": (a) => hex(writeTerrain(decodeTerrain(a[0], a[1], a[2], a[3]))),
  "terrain.decode_item_list": (a) => decodeItemList(a[0], a[1], a[2], a[3]).map((i) => [i.layer, i.obj, i.dx, i.dz]),
  "terrain.decode_cell_table": (a) => decodeCellTable(a[0], a[1], a[2], a[3], a[4]),
  "terrain.decode_position_blocks": (a) => decodePositionBlocks(a[0], a[1], a[2]).map((b) => [b.ref, b.cellX, b.cellZ, b.fineX, b.fineZ, b.height]),
  "terrain.decode_default_offsets": (a) => decodeDefaultOffsets(a[0], a[1], a[2]),
  "combat.decode_templates": (a) => hex(decodeTemplates(a[0])),
  "combat.decode_ai": (a) => hex(decodeAi(a[0])),
  "combat.decode_burst": (a) => hex(decodeBurst(a[0])),
  "combat.decode_target_text": (a) => hex(decodeTargetText(a[0])),
  "combat.decode_prompts": (a) => hex(decodePrompts(a[0])),
  "combat.decode_months": (a) => hex(decodeMonths(a[0])),
  "combat.decode_star": (a) => hex(decodeStar(a[0])),
  "combat.decode_briefing_chain": (a) => hex(decodeBriefingChain(a[0])),
  "combat.decode_head_step": (a) => hex(decodeHeadStep(a[0])),
  "flow.decode_scenario": (a) => hex(decodeScenario(a[0])),
  "flow.decode_text": (a) => decodeText(a[0]).map(hex),
  "flow.decode_menu_font": (a) => hex(decodeMenuFont(a[0])),
  "flow.decode_demo": (a) => hex(decodeDemo(a[0], a[1])),
  "flow.decode_training": (a) => hex(decodeTraining(a[0])),
  "flow.palette_words": (a) => hex(paletteWords(a[0], a[1], a[2])),
  "models.decode_model": (a) => hex(writeModel(decodeModel(a[0], a[1], a[2], a[3], a[4]))),
};

function replay(call) {
  const args = call.args.map(arg);
  try {
    const value = ADAPTERS[call.fn](args);
    return { ok: createHash("sha256").update(canon(value)).digest("hex") };
  } catch (e) {
    if (e instanceof BlockedError) return { err: ["blocked", e.message] };
    if (e instanceof DecodeError) return { err: ["decode", e.message] };
    return { crash: e };
  }
}

test("the corpus covers the decoders and both outcomes", () => {
  const count = (fn, kind) => corpus.calls.filter((c) => c.fn === fn && (kind === "ok" ? c.ok !== undefined : c.err && c.err[0] === kind)).length;
  for (const f of corpus.calls.map((c) => c.fn)) assert.ok(f in ADAPTERS, `no adapter for ${f}`);
  assert.ok(corpus.calls.length > 3000);
  assert.ok(count("models.decode_model", "ok") > 100 && count("models.decode_model", "decode") > 100 && count("models.decode_model", "blocked") > 0);
  for (const f of ["combat.decode_ai", "combat.decode_burst", "flow.decode_scenario", "flow.decode_text", "lists.decode_all", "terrain.decode_terrain", "flight.decode_all"]) {
    assert.ok(count(f, "ok") > 10 && count(f, "decode") > 10, `${f}: both outcomes expected`);
  }
});

test("every recorded Python call gives the same digest or the same refusal in JavaScript", () => {
  const failures = [];
  const counts = {};
  corpus.calls.forEach((call, i) => {
    if (call.err && call.err[0] === "other") return;   // a Python exception outside the decoders' own (struct.error and the like)
    const got = replay(call);
    counts[call.fn] = (counts[call.fn] || 0) + 1;
    if (got.crash) {
      failures.push(`#${i} ${call.fn}: JavaScript threw ${got.crash.stack}`);
    } else if (call.ok !== undefined) {
      if (got.ok !== call.ok) failures.push(`#${i} ${call.fn}: ${got.err ? "JavaScript refused: " + got.err[1] : "different result"}`);
    } else if (!got.err || got.err[0] !== call.err[0] || got.err[1] !== call.err[1]) {
      failures.push(`#${i} ${call.fn}: expected ${call.err[0]} "${call.err[1]}", got ${got.err ? got.err[0] + ' "' + got.err[1] + '"' : "a result"}`);
    }
  });
  assert.deepEqual(failures.slice(0, 12), [], `${failures.length} of ${corpus.calls.length} calls differ`);
});
