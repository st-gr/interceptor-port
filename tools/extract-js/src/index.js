// The JavaScript port of the asset extractor (tools/extract/extract/cli.py): the player's own ADF in, the assets out, in
// memory. Byte-identical to `python -m extract`: the same files, the same bytes, the same manifest. Pure over byte arrays:
// no DOM, no file system. Both ports change together and share EXTRACTOR_VERSION (tools/extract/README.md).
import { Adf } from "./adf.js";
import { ExtractError } from "./errors.js";
import { relocate } from "./hunk.js";
import { decodeIlbm } from "./ilbm.js";
import { writePng } from "./png.js";
import { EXTRACTOR_VERSION, buildManifest, sha256Hex } from "./manifest.js";
import {
  ARCS_ADDR, MAPSHAPES_ADDR, MARKER_LARGE_ADDR, MARKER_SMALL_ADDR, MASKS_ADDR, WARNING_ADDR,
  decodeArcs, decodeMapshapes, decodeMarkers, decodeMasks, decodeWarnings,
} from "./cockpit.js";
import { writeModels } from "./models.js";
import { FADE_TABLE, FONT_ADDR, decodeFadeTable, decodeFont, fadeBytes, paletteBytes } from "./palette.js";
import { sampleRateHz, writeSample } from "./sounds.js";
import { buildSoundTable } from "./sound_table.js";
import {
  CAMERA_RING, DecodeError, IMAGE_BASE, LOD_TABLE, RANGE_TABLE, cameraRingBytes, decodeCameraRing, decodeLodTable, decodeRangeTable,
} from "./tables.js";
import { decodeAll as decodeFlightTables, parseSegmentTable, referenceLongs } from "./flight.js";
import { decodeAll as decodeListTables } from "./lists.js";
import { writeAi, writeBurst, writeTemplates } from "./combat.js";
import { DEMO_FILES, writeDemo, writeMenuFont, writePalettes, writeScenario, writeText, writeTraining } from "./flow.js";
import { decodeAll as decodeSkyTables } from "./sky.js";
import {
  POSITION_TABLE, decodePositionBlocks, decodeTerrain, positionsText, terrainIndexText, writeTerrain,
} from "./terrain.js";
import { utf8 } from "./util.js";

export { ExtractError, EXTRACTOR_VERSION };

const PICTURES = ["splsh", "inst5", "frnt5"];
const EXECUTABLE = "F-18 Interceptor";
const CAMERA_TAGS = { default: "default", class11: "11", class14: "14", class30: "30" };

const WRONG_DISK = `Expected the F/A-18 Interceptor disk (Electronic Arts, 1988): its pix/ and text/ directories and the executable "${EXECUTABLE}".`;

/** A missing file means this is another disk; any other failure of the reader (a damaged block) stays as it is. */
function notTheGameDisk(e) {
  if (e instanceof ExtractError && /not found on the disk/.test(e.message)) {
    return new ExtractError(`This is not the F/A-18 Interceptor disk: ${e.message}. ${WRONG_DISK}`);
  }
  return e;
}

/** Reads a file of the disk; a missing one means this is another disk. */
function readGameFile(disk, path) {
  try {
    return disk.read(path);
  } catch (e) {
    throw notTheGameDisk(e);
  }
}

function listGameDir(disk, path) {
  try {
    return disk.listdir(path);
  } catch (e) {
    throw notTheGameDisk(e);
  }
}

/** Palette, font, tables and terrain from the relocated image (cli.py write_world). */
function writeWorld(image, files, longs) {
  const off = (addr) => addr - IMAGE_BASE;
  const fade = decodeFadeTable(image, off(FADE_TABLE));
  files.set("palette/fade.pal", fadeBytes(fade));
  files.set("palette/flight.pal", paletteBytes(fade[0]));
  const glyphs = decodeFont(image, off(FONT_ADDR));
  const hud = new Uint8Array(glyphs.length * glyphs[0].length);
  glyphs.forEach((g, i) => hud.set(g, i * g.length));
  files.set("fonts/hud.bin", hud);
  files.set("tables/range.bin", decodeRangeTable(image, off(RANGE_TABLE)));
  files.set("tables/lod.bin", decodeLodTable(image, off(LOD_TABLE)));
  for (const [name, addr] of Object.entries(CAMERA_RING)) {
    files.set(`tables/camera_ring_${CAMERA_TAGS[name]}.bin`, cameraRingBytes(decodeCameraRing(image, off(addr), name)));
  }
  for (const [fname, data] of decodeFlightTables(image, IMAGE_BASE, longs)) files.set(`tables/${fname}`, data);   // note 05 "Extractor needs"
  for (const [fname, data] of decodeListTables(image, IMAGE_BASE)) files.set(`tables/${fname}`, data);           // note 04a
  for (const [fname, data] of decodeSkyTables(image, IMAGE_BASE)) files.set(`tables/${fname}`, data);            // note 04
  writePalettes(image, files);     // menu.pal, flash.pal (note 02 section 11)
  writeScenario(image, files);     // tables/scenario.bin
  writeTraining(image, files);     // tables/training.bin
  writeTemplates(image, files);    // tables/templates.bin
  writeAi(image, files);           // tables/ai.bin
  writeBurst(image, files);        // tables/burst.bin, target_text.bin, months.bin, ...
  writeText(image, files);         // tables/text.bin, text_ids.bin
  writeMenuFont(image, files);     // fonts/menu5x7.bin
  writeModels(image, files);
  const terrain = decodeTerrain(image);
  files.set("terrain/terrain.bin", writeTerrain(terrain));
  files.set("terrain/index.txt", utf8(terrainIndexText(terrain)));
  files.set("cockpit/warnings.bin", decodeWarnings(image, off(WARNING_ADDR)));
  files.set("cockpit/masks.bin", decodeMasks(image, off(MASKS_ADDR)));
  files.set("cockpit/markers.bin", decodeMarkers(image, off(MARKER_SMALL_ADDR), off(MARKER_LARGE_ADDR)));
  files.set("cockpit/arcs.bin", decodeArcs(image, off(ARCS_ADDR)));
  files.set("cockpit/mapshapes.bin", decodeMapshapes(image, off(MAPSHAPES_ADDR)));
  files.set("terrain/positions.txt", utf8(positionsText(decodePositionBlocks(image, POSITION_TABLE, IMAGE_BASE))));
}

/**
 * Extracts the assets from the bytes of an ADF disk image.
 *
 * @param {Uint8Array} adfBytes the whole file
 * @param {object} [options]
 * @param {string} [options.segments] the text of the loader's segment table (segments.txt of this package, extract/segments.txt of the Python
 *   extractor): the junk spans of the flight tables then carry the reference machine's values. Without it the extraction is
 *   refused unless `allowFlatSpans`, which writes assets the core refuses to load (manifest "spans_relocated": false).
 * @param {boolean} [options.allowFlatSpans]
 * @param {(stage: string) => void} [options.onProgress] called with a short stage name before each stage
 * @returns {Promise<{files: Map<string, Uint8Array>, manifest: object}>} every output file by relative path (manifest.json
 *   included) and the parsed manifest
 * @throws {ExtractError} the file is not an ADF (wrong size, no AmigaDOS root block, a flux image named by its format), is
 *   the ADF of another disk, or is damaged
 */
export async function extract(adfBytes, options = {}) {
  const { segments = null, allowFlatSpans = false, onProgress = () => {} } = options;
  if (!(adfBytes instanceof Uint8Array)) throw new ExtractError("Expected the bytes of an ADF disk image (a Uint8Array).");
  try {
    return await extractInner(adfBytes, segments, allowFlatSpans, onProgress);
  } catch (e) {
    if (e instanceof ExtractError) throw e;
    if (e instanceof DecodeError) {
      const err = new ExtractError(`This disk image does not hold the F/A-18 Interceptor data this extractor reads (${e.message}). ${WRONG_DISK}`);
      err.cause = e;
      throw err;
    }
    throw e;
  }
}

async function extractInner(adfBytes, segments, allowFlatSpans, onProgress) {
  onProgress("disk");
  const disk = new Adf(adfBytes);
  const files = new Map();
  const enc = (s) => utf8(s);

  onProgress("pictures");
  for (const name of PICTURES) {
    const img = decodeIlbm(readGameFile(disk, `pix/${name}`));
    files.set(`pix/${name}.png`, writePng(img.width, img.height, img.pixels, img.palette));
    files.set(`pix/${name}.palette.json`, enc("[" + img.palette.map((c) => `[${c.join(", ")}]`).join(", ") + "]\n"));
    // raw picture indices (one byte per pixel, planes combined): the cockpit draws pens, not RGB
    files.set(`pix/${name}.idx`, img.pixels);
  }

  onProgress("sounds");
  const soundNames = listGameDir(disk, "text").filter((n) => !DEMO_FILES.includes(n));   // textply, textctl are the demo stream
  const sizes = new Map();
  for (const name of soundNames) {
    const raw = readGameFile(disk, `text/${name}`);
    sizes.set(name, raw.length);
    writeSample(raw, name, files);
  }
  writeDemo(readGameFile(disk, "text/textply"), readGameFile(disk, "text/textctl"), files);

  onProgress("executable");
  const exe = readGameFile(disk, EXECUTABLE);
  let image;
  try {
    image = relocate(exe, IMAGE_BASE).image;
  } catch (e) {
    if (e instanceof DecodeError) throw new ExtractError(`This is not the F/A-18 Interceptor disk: its file "${EXECUTABLE}" is not an Amiga executable (${e.message}). ${WRONG_DISK}`);
    throw e;
  }
  let longs = [];
  if (segments !== null) {
    longs = referenceLongs(exe, IMAGE_BASE, parseSegmentTable(segments));
  } else {
    if (!allowFlatSpans) {
      throw new ExtractError("no segment table: pass options.segments (the text of segments.txt) so that the junk spans of the flight tables carry the reference machine's "
        + "values for their relocated longs, or options.allowFlatSpans for assets the core will refuse");
    }
    files.set("tables/flight_spans_flat.txt", enc("the junk spans were extracted without the segment table (manifest spans_relocated: false)\n"));
  }

  onProgress("tables");
  writeWorld(image, files, longs);
  files.set("tables/sounds.bin", buildSoundTable(soundNames, sizes));   // note 10 2.2 (extractor version 25)
  files.set("pix/index.txt", enc(PICTURES.map((n) => `${n}\n`).join("")));
  files.set("sounds/index.txt", enc(soundNames.map((n) => `${n}\n`).join("")));
  files.set("VERSION", enc(`${EXTRACTOR_VERSION}\n`));

  onProgress("manifest");
  const { manifest, bytes } = await buildManifest(
    files, await sha256Hex(adfBytes), Object.fromEntries(soundNames.map((n) => [n, sampleRateHz(n)])), segments !== null);
  files.set("manifest.json", bytes);
  return { files, manifest };
}
