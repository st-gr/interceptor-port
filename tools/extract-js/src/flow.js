// Game-flow tables (tools/extract/extract/flow.py; docs/re/02 sections 9 and 11, docs/re/05 17.14 and 17.17, docs/re/08):
// the menu and flash palettes, the scenario tables, the text records, the menu font, the demo replay stream and the
// training tables. Everything is little endian in the output.
import { paletteBytes } from "./palette.js";
import { DecodeError, IMAGE_BASE } from "./tables.js";
import { ByteWriter, concat, hex } from "./util.js";

export const MENU_PALETTE = 0x29f24;
export const FLASH_PALETTE = 0x29f64;
export const PALETTE_ENTRIES = 32;

const START_BLOCKS = 0x47ade;
const START_RECORD = 16;
// Caps of the core's scenario_t (src/core/assets.h): the extractor refuses what the core could not hold.
const START_MAX = 8;
const REGION_ENTRIES_MAX = 16;
const ROWS_MAX = 16;
const LISTS_MAX = 16;
const SUBLISTS_MAX = 48;
const SUB_ENTRIES_MAX = 160;
const REGION_TABLE = 0x2de28;
const REGION_COUNT = 4;
const PLACEMENT_TABLE = 0x2deda;
const PLACEMENT_ROWS = 10;
const LIST_HEADS = 4;
const ENTRY_WORDS = 5;
const ENTRY_SIZE = 2 * ENTRY_WORDS;

/** `n` big-endian words at flat address `addr` (unsigned, or signed with `signed`). */
function words(image, addr, n, what, signed = false) {
  const off = addr - IMAGE_BASE;
  if (off < 0 || off + 2 * n > image.length) {
    throw new DecodeError(`${what} at 0x${hex(addr)}: needs ${2 * n} bytes, image has ${image.length}`);
  }
  const dv = new DataView(image.buffer, image.byteOffset, image.byteLength);
  const out = [];
  for (let i = 0; i < n; i++) out.push(signed ? dv.getInt16(off + 2 * i, false) : dv.getUint16(off + 2 * i, false));
  return out;
}

/** `entries` colour words 0x0RGB at `addr`; channels scaled to 8 bits by 17. Returns the 3 * entries bytes. */
export function paletteWords(image, addr, entries = PALETTE_ENTRIES) {
  const out = [];
  words(image, addr, entries, "palette").forEach((w, i) => {
    if (w > 0x0fff) throw new DecodeError(`palette at 0x${hex(addr)}: entry ${i} is 0x${hex(w, 4)}, not a 12-bit colour`);
    out.push([((w >> 8) & 15) * 17, ((w >> 4) & 15) * 17, (w & 15) * 17]);
  });
  return paletteBytes(out);
}

/** Adds palette/menu.pal (0x29F24) and palette/flash.pal (0x29F64) to `files`. */
export function writePalettes(image, files) {
  files.set("palette/menu.pal", paletteWords(image, MENU_PALETTE));
  files.set("palette/flash.pal", paletteWords(image, FLASH_PALETTE));
}

function cap(what, n, limit) {
  if (n > limit) throw new DecodeError(`scenario ${what}: ${n} found, the core holds at most ${limit}`);
}

function section(count, recordSize, body) {
  if (body.length !== count * recordSize) throw new Error("scenario section size");
  return new ByteWriter(4 + body.length).u16(count).u16(recordSize).bytes(body).finish();
}

function packWords(kind, rows) {
  const w = new ByteWriter(64);
  for (const r of rows) w.many(kind, r);
  return w.finish();
}

/** tables/scenario.bin: eight sub-tables, each `u16 count, u16 record_size` then the records (see flow.py). */
export function decodeScenario(image) {
  const W = (addr, n) => words(image, addr, n, "scenario");

  // 1. start blocks
  const blocks = [];
  let ended = false;
  for (let i = 0; i < START_MAX + 1; i++) {
    const w = words(image, START_BLOCKS + START_RECORD * i, 8, "start blocks", true);
    if (w[0] === -1) { ended = true; break; }
    blocks.push(w);
  }
  if (!ended) {
    throw new DecodeError(`start blocks at 0x${hex(START_BLOCKS)}: no terminator within ${START_MAX} blocks (the core holds at most that many)`);
  }
  if (!blocks.length) throw new DecodeError(`start blocks at 0x${hex(START_BLOCKS)}: empty table`);
  cap("start blocks", blocks.length, START_MAX);
  const s1 = section(blocks.length, START_RECORD, packWords("i16", blocks));

  // 2, 3. regions
  const regions = [];
  const entries = [];
  for (let r = 0; r < REGION_COUNT; r++) {
    const [hi, lo] = W(REGION_TABLE + 4 * r, 2);
    const a = hi * 65536 + lo;
    if (!(IMAGE_BASE <= a && a < IMAGE_BASE + image.length - 10)) {
      throw new DecodeError(`region ${r}: pointer 0x${hex(a)} at 0x${hex(REGION_TABLE + 4 * r)} is outside the image`);
    }
    const box = W(a, 4);
    const [cnt] = W(a + 8, 1);
    const first = entries.length;
    let q = a + 10;
    let ended2 = false;
    for (let k = 0; k < REGION_ENTRIES_MAX + 1; k++) {
      const e = W(q, ENTRY_WORDS);
      if (e[0] & 0x8000) { ended2 = true; break; }
      entries.push(e);
      q += ENTRY_SIZE;
    }
    if (!ended2) throw new DecodeError(`region ${r} at 0x${hex(a)}: no terminator within ${REGION_ENTRIES_MAX} entries`);
    regions.push([...box, cnt, first, entries.length - first]);
  }
  cap("region entries", entries.length, REGION_ENTRIES_MAX);
  const s2 = section(regions.length, 14, packWords("u16", regions));
  const s3 = section(entries.length, ENTRY_SIZE, packWords("u16", entries));

  // 4-7. placement rows, lists, sub-lists
  const rowsRaw = [];
  for (let i = 0; i < PLACEMENT_ROWS; i++) rowsRaw.push(W(PLACEMENT_TABLE + 12 * i, 6));
  const listOffs = [...new Set(rowsRaw.flatMap((row) => [row[0], row[2], row[4]]))].sort((x, y) => x - y);
  const subAddr = new Map();
  const subEntries = [];
  const subs = [];
  const lists = [];
  for (const lo of listOffs) {
    const la = PLACEMENT_TABLE + lo;
    const heads = W(la, LIST_HEADS);
    const idx = [];
    for (const h of heads) {
      const sa = la + h;
      if (!subAddr.has(sa)) {
        const first = subEntries.length;
        let q = sa;
        let ended3 = false;
        for (let k = 0; k < SUB_ENTRIES_MAX + 1; k++) {
          const e = W(q, ENTRY_WORDS);
          if (e[0] === 0xffff) { ended3 = true; break; }
          subEntries.push(e);
          q += ENTRY_SIZE;
        }
        if (!ended3) throw new DecodeError(`placement sub-list at 0x${hex(sa)}: no terminator`);
        subAddr.set(sa, subs.length);
        subs.push([first, subEntries.length - first]);
      }
      idx.push(subAddr.get(sa));
    }
    lists.push(idx);
  }
  const rows = rowsRaw.map((row) => {
    const rec = [];
    for (let k = 0; k < 3; k++) rec.push(listOffs.indexOf(row[2 * k]), row[2 * k + 1]);
    return rec;
  });
  cap("placement rows", rows.length, ROWS_MAX);
  cap("placement lists", lists.length, LISTS_MAX);
  cap("placement sub-lists", subs.length, SUBLISTS_MAX);
  cap("placement sub-list entries", subEntries.length, SUB_ENTRIES_MAX);
  const s4 = section(rows.length, 12, packWords("u16", rows));
  const s5 = section(lists.length, 8, packWords("u16", lists));
  const s6 = section(subs.length, 4, packWords("u16", subs));
  const s7 = section(subEntries.length, ENTRY_SIZE, packWords("u16", subEntries));
  const s8 = section(0, 0, new Uint8Array(0));
  return concat([s1, s2, s3, s4, s5, s6, s7, s8]);
}

export function writeScenario(image, files) {
  files.set("tables/scenario.bin", decodeScenario(image));
}

// ---- text records and the menu font (docs/re/08-game-flow.md sections 2 and 3.2) ----
const TEXT_ID_TABLE = 0x43dfe;
const TEXT_ID_COUNT = 110;
const TEXT_RECORDS = 0x43f24;
const TEXT_END = 0x4615a;
const TEXT_NOT_RECORDS = [18, 19, 21, 25];
const TEXT_LINE_MAX_CHARS = 45;
const TEXT_FLAGS = [0, 1, 3];
const TEXT_DELAY_MAX = 120;
const TEXT_LINES_MAX = 400;
const TEXT_CHARS_MAX = 8192;
const TEXT_ROW_LIMIT = 200;
const TEXT_WIDTH = 320;

const FONT_TABLE = 0x4283c;
const FONT_CHARS = 64;
const FONT_GLYPH = 7;
const FONT_OFFSET_MIN = 0x80;
const FONT_OFFSET_MAX = 0x1f3;
const FONT_BITMAPS = 0x428bc;

function decodeRecord(image, addr, rid) {
  const ctxName = `text record of id ${rid} at 0x${hex(addr)}`;
  const need = (a, n = 1) => {
    if (a < TEXT_RECORDS || a + n > TEXT_END) {
      throw new DecodeError(`${ctxName}: runs outside the record span 0x${hex(TEXT_RECORDS)}-0x${hex(TEXT_END)} at 0x${hex(a)}`);
    }
    return a - IMAGE_BASE;
  };
  if (addr - IMAGE_BASE + 2 > image.length || TEXT_END - IMAGE_BASE > image.length) {
    throw new DecodeError(`${ctxName}: outside the image`);
  }
  const row = image[need(addr, 2)], col = image[need(addr + 1)];
  let a = addr + 2;
  const lines = [];
  for (;;) {
    const flags = image[need(a)];
    a += 1;
    if (flags & 0x80) break;
    if (!TEXT_FLAGS.includes(flags)) {
      throw new DecodeError(`${ctxName}: line ${lines.length} has flags 0x${hex(flags, 2)}, expected one of (${TEXT_FLAGS.join(", ")})`);
    }
    const colour = image[need(a)];
    a += 1;
    if ((flags & 1) && (colour & 15) < 2) {
      throw new DecodeError(`${ctxName}: typed line ${lines.length} has delay ${colour & 15} (colour 0x${hex(colour, 2)}); the engine never advances below 2`);
    }
    const text = [];
    for (;;) {
      const b = image[need(a)];
      a += 1;
      if (b === 0) break;
      if (!(b >= 0x20 && b <= 0x5f)) {
        throw new DecodeError(`${ctxName}: line ${lines.length} has text byte 0x${hex(b, 2)} outside 0x20-0x5F`);
      }
      text.push(b);
      if (text.length > TEXT_LINE_MAX_CHARS) {
        throw new DecodeError(`${ctxName}: line ${lines.length} is longer than ${TEXT_LINE_MAX_CHARS} characters`);
      }
    }
    const delay = image[need(a)];
    a += 1;
    if (delay > TEXT_DELAY_MAX) throw new DecodeError(`${ctxName}: line ${lines.length} has delay ${delay}, above ${TEXT_DELAY_MAX}`);
    lines.push([row, col, flags, colour, delay, Uint8Array.from(text)]);
    if (lines.length > 255) throw new DecodeError(`${ctxName}: more than 255 lines`);
  }
  if (lines.length) {
    if (row >= TEXT_ROW_LIMIT) throw new DecodeError(`${ctxName}: row ${row} is not a scanline below ${TEXT_ROW_LIMIT}`);
    if (col & 1) throw new DecodeError(`${ctxName}: column ${col} is odd`);
    lines.forEach((ln, k) => {
      const n = ln[5].length;
      if (n && 8 * col + 7 * (n - 1) + 5 > TEXT_WIDTH) {
        throw new DecodeError(`${ctxName}: line ${k} (${n} characters from column ${col}) is wider than ${TEXT_WIDTH} pixels`);
      }
    });
  }
  return lines;
}

/** Returns [text.bin, text_ids.bin] per note 08 section 2.3. */
export function decodeText(image) {
  const offs = words(image, TEXT_ID_TABLE, TEXT_ID_COUNT, "text id table");
  const records = new Map();   // record address -> [lowest id, lines]
  offs.forEach((off, i) => {
    const rid = i + 1;
    if (TEXT_NOT_RECORDS.includes(rid)) return;
    const addr = TEXT_ID_TABLE + off;
    if (!records.has(addr)) records.set(addr, [rid, decodeRecord(image, addr, rid)]);
  });
  const placed = new Map();
  const body = new ByteWriter(1024);
  let nlines = 0;
  for (const [addr, [rid, lines]] of [...records].sort((x, y) => x[1][0] - y[1][0])) {
    placed.set(addr, [nlines, lines.length]);
    lines.forEach(([row, col, flags, colour, delay, text], k) => {
      body.u16(rid).u8(k).u8(row).u8(col).u8(flags).u8(colour).u8(delay).u16(text.length).bytes(text);
      nlines += 1;
    });
  }
  let chars = 0;
  for (const [, ls] of records.values()) for (const ln of ls) chars += ln[5].length + 1;
  if (nlines > TEXT_LINES_MAX || chars > TEXT_CHARS_MAX) {
    throw new DecodeError(`text: ${nlines} lines and ${chars} text bytes (with one NUL per line); the core holds at most ${TEXT_LINES_MAX} and ${TEXT_CHARS_MAX}`);
  }
  const ids = new ByteWriter(2 + 6 * TEXT_ID_COUNT).u16(TEXT_ID_COUNT);
  offs.forEach((off, i) => {
    const rid = i + 1;
    const [first, n] = TEXT_NOT_RECORDS.includes(rid) ? [0, 0] : placed.get(TEXT_ID_TABLE + off);
    ids.u16(rid).u16(n ? first : 0).u16(n);
  });
  return [new ByteWriter(2 + body.len).u16(nlines).bytes(body.finish()).finish(), ids.finish()];
}

/** Adds tables/text.bin and tables/text_ids.bin to `files`. */
export function writeText(image, files) {
  const [text, ids] = decodeText(image);
  files.set("tables/text.bin", text);
  files.set("tables/text_ids.bin", ids);
}

/** fonts/menu5x7.bin: 64 glyphs x 7 bytes, glyph i the one of character 0x20 + i. */
export function decodeMenuFont(image) {
  const offs = words(image, FONT_TABLE, FONT_CHARS, "menu font offset table");
  if (offs[0] !== FONT_OFFSET_MIN) {
    throw new DecodeError(`menu font: first offset is 0x${hex(offs[0], 4)}, expected 0x${hex(FONT_OFFSET_MIN, 4)} (character 0x20)`);
  }
  const out = new ByteWriter(FONT_CHARS * FONT_GLYPH);
  offs.forEach((o, i) => {
    const c = 0x20 + i;
    if (!(o >= FONT_OFFSET_MIN && o <= FONT_OFFSET_MAX)) {
      throw new DecodeError(`menu font: character 0x${hex(c, 2)} has offset 0x${hex(o, 4)}, outside the glyph bitmaps 0x${hex(FONT_BITMAPS)}-0x${hex(FONT_TABLE + FONT_OFFSET_MAX + FONT_GLYPH - 1)} (offsets 0x${hex(FONT_OFFSET_MIN, 4)}-0x${hex(FONT_OFFSET_MAX, 4)})`);
    }
    const at = FONT_TABLE + o - IMAGE_BASE;
    const g = image.slice(at, at + FONT_GLYPH);
    if (g.length !== FONT_GLYPH) {
      throw new DecodeError(`menu font: glyph of character 0x${hex(c, 2)} at 0x${hex(FONT_TABLE + o)} is outside the image`);
    }
    if (g.some((b) => b & 7)) {
      throw new DecodeError(`menu font: glyph of character 0x${hex(c, 2)} has bits set in the low three bits of a row`);
    }
    if (o === FONT_OFFSET_MIN && g.some((b) => b)) {
      throw new DecodeError(`menu font: the glyph at offset 0x${hex(o, 4)} (character 0x${hex(c, 2)}) is not blank`);
    }
    out.bytes(g);
  });
  return out.finish();
}

export function writeMenuFont(image, files) {
  files.set("fonts/menu5x7.bin", decodeMenuFont(image));
}

// ---- the demo replay stream and the training tables (docs/re/08-game-flow.md 7.1 and 8.13) ----
export const DEMO_FRAMES = 3676;
export const DEMO_FILES = ["textply", "textctl"];
const TRAINING_A = 0x2749a;
const TRAINING_B = 0x274e2;
const TRAINING_ROWS = 8;
const PITCH_RATES = 0x3132c;
const ROLL_RATES = 0x31342;
const RATE_ENTRIES = 11;

/** tables/demo.bin: `u32 frame_count` (3676), the key bytes, then per frame `u16 word1, u16 word2` (little endian). */
export function decodeDemo(textply, textctl) {
  if (textply.length !== DEMO_FRAMES) throw new DecodeError(`text/textply: ${textply.length} bytes, expected ${DEMO_FRAMES}`);
  if (textctl.length !== 4 * DEMO_FRAMES) {
    throw new DecodeError(`text/textctl: ${textctl.length} bytes, expected ${4 * DEMO_FRAMES} (${DEMO_FRAMES} x 4)`);
  }
  const dv = new DataView(textctl.buffer, textctl.byteOffset, textctl.byteLength);
  const out = new ByteWriter(4 + DEMO_FRAMES * 5).u32(DEMO_FRAMES).bytes(textply);
  for (let i = 0; i < 2 * DEMO_FRAMES; i++) out.u16(dv.getUint16(2 * i, false));
  return out.finish();
}

export function writeDemo(textply, textctl, files) {
  files.set("tables/demo.bin", decodeDemo(textply, textctl));
}

/** tables/training.bin: four sub-tables each prefixed `u16 count, u16 record_size`. */
export function decodeTraining(image) {
  const dv = new DataView(image.buffer, image.byteOffset, image.byteLength);
  const out = new ByteWriter(256);
  const rowsA = [];
  for (let r = 0; r < TRAINING_ROWS; r++) {
    const off = TRAINING_A + 8 * r - IMAGE_BASE;
    if (off < 0 || off + 8 > image.length) {
      throw new DecodeError(`training table A row ${r} at 0x${hex(TRAINING_A + 8 * r)}: outside the image`);
    }
    rowsA.push([dv.getInt16(off, false), dv.getInt16(off + 2, false), dv.getInt32(off + 4, false)]);
  }
  const rowsB = [];
  for (let r = 0; r < TRAINING_ROWS; r++) {
    const off = TRAINING_B + 8 * r - IMAGE_BASE;
    if (off < 0 || off + 8 > image.length) {
      throw new DecodeError(`training table B row ${r} at 0x${hex(TRAINING_B + 8 * r)}: outside the image`);
    }
    const cam = dv.getInt32(off, false), script = dv.getInt32(off + 4, false);
    if (script >= 0) {
      throw new DecodeError(`training table B row ${r} at 0x${hex(TRAINING_B + 8 * r)}: script long 0x${hex(script, 8)} has bit 31 clear (a script pointer; not supported)`);
    }
    rowsB.push([cam, script]);
  }
  out.u16(TRAINING_ROWS).u16(8);
  for (const [p, n, u] of rowsA) out.i16(p).i16(n).i32(u);
  out.u16(TRAINING_ROWS).u16(8);
  for (const [c, s] of rowsB) out.i32(c).i32(s);
  for (const [addr, what] of [[PITCH_RATES, "pitch"], [ROLL_RATES, "roll"]]) {
    out.u16(RATE_ENTRIES).u16(2).many("i16", words(image, addr, RATE_ENTRIES, `training ${what} rates`, true));
  }
  return out.finish();
}

export function writeTraining(image, files) {
  files.set("tables/training.bin", decodeTraining(image));
}
