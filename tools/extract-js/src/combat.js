// Combat tables (tools/extract/extract/combat.py; docs/re/02 section 1, docs/re/07, docs/re/06, docs/re/08): templates.bin,
// ai.bin, burst.bin, target_text.bin, prompts.bin, months.bin, star.bin, briefing_chain.bin, head_step.bin.
// The layouts are documented beside the Python decoders and in tools/extract/README.md.
import { DecodeError, IMAGE_BASE, RECORD_COUNT, RECORD_SIZE, RECORD_TABLE } from "./tables.js";
import { ByteWriter, concat, hex, hexLower0x } from "./util.js";

const dvOf = (b) => new DataView(b.buffer, b.byteOffset, b.byteLength);
const subRange = (image, off, n) => image.slice(off, off + n);

// ---- tables/templates.bin ----
export function decodeTemplates(image) {
  const off = RECORD_TABLE - IMAGE_BASE;
  const end = off + RECORD_COUNT * RECORD_SIZE;
  if (off < 0 || end > image.length) {
    throw new DecodeError(`template table at 0x${hex(RECORD_TABLE)}: ${RECORD_COUNT} records of ${RECORD_SIZE} bytes need the bytes up to 0x${hex(IMAGE_BASE + end)}, image has ${image.length}`);
  }
  return new ByteWriter(4 + end - off).u16(RECORD_COUNT).u16(RECORD_SIZE).bytes(image.subarray(off, end)).finish();
}

// ---- tables/ai.bin ----
const AI_RATE_ADDRS = [0x31316, 0x3132c, 0x31342, 0x31358, 0x3136e];
const AI_RATE_ENTRIES = 11;
const AI_EVASION = 0x30284;
const AI_EVASION_GROUPS = 19;
const AI_OFFSETS = 0x2789e;
const AI_OFFSET_COUNT = 14;
const AI_PRE_WORD = 0x31314;
const AI_RING3 = 0x2d718;
const AI_RING3_DIRECTIONS = 10;
const AI_START = 0x47ade;
const AI_START_ENTRIES = 5;
const AI_CAM_POINTS = 0x47b30;
const AI_CAM_POINT_COUNT = 3;
const AI_FINE = 0x206f2;
const AI_FINE_COUNT = 16;
const AI_RUN_TABLE = 0x2dce8;
const AI_RUN_COUNT = 60;
const AI_RUN_MAX = 33;
const AI_RUN_RECORD_WORDS = 1 + 5 * AI_RUN_MAX;
const AI_HULL_PTRS = 0x3dacc;
const AI_HULL_LISTS = [0x3dafa, 0x3dd28, 0x3dd94];
const AI_HULL_FACES = 6;
const AI_HULL_SLOT_MAX = 16;

function aiWords(image, addr, n, what) {
  const off = addr - IMAGE_BASE;
  if (off < 0 || off + 2 * n > image.length) {
    throw new DecodeError(`ai table ${what} at 0x${hex(addr)}: needs ${2 * n} bytes, image has ${image.length}`);
  }
  const dv = dvOf(image);
  const out = [];
  for (let i = 0; i < n; i++) out.push(dv.getInt16(off + 2 * i, false));
  return out;
}

function aiSub(image, addr, count, wordsPerRecord, what, strideWords = 0) {
  const stride = strideWords || wordsPerRecord;
  const span = aiWords(image, addr, stride * (count - 1) + wordsPerRecord, what);
  const w = [];
  for (let i = 0; i < count; i++) w.push(...span.slice(stride * i, stride * i + wordsPerRecord));
  return new ByteWriter(4 + 2 * w.length).u16(count).u16(2 * wordsPerRecord).many("i16", w).finish();
}

function decodeRuns(image) {
  const base = AI_RUN_TABLE - IMAGE_BASE;
  if (base < 0 || base + 2 * AI_RUN_COUNT > image.length) {
    throw new DecodeError(`waypoint run table at 0x${hex(AI_RUN_TABLE)} needs ${2 * AI_RUN_COUNT} bytes, image has ${image.length}`);
  }
  const dv = dvOf(image);
  const out = new ByteWriter(4 + 2 * AI_RUN_COUNT * AI_RUN_RECORD_WORDS).u16(AI_RUN_COUNT).u16(2 * AI_RUN_RECORD_WORDS);
  for (let k = 0; k < AI_RUN_COUNT; k++) {
    const off = dv.getUint16(base + 2 * k, false);
    let a = base + off;
    const blocks = [];
    for (;;) {
      if (a + 10 > image.length) {
        throw new DecodeError(`waypoint run ${k} at 0x${hex(IMAGE_BASE + base + off)} leaves the image after ${blocks.length} blocks`);
      }
      const w = [0, 1, 2, 3, 4].map((i) => dv.getInt16(a + 2 * i, false));
      if (w[0] < 0) break;   // bit 15 set: the run ends before this block
      blocks.push(w);
      if (blocks.length > AI_RUN_MAX) {
        throw new DecodeError(`waypoint run ${k} at 0x${hex(IMAGE_BASE + base + off)} has more than ${AI_RUN_MAX} blocks`);
      }
      a += 10;
    }
    const words = [blocks.length, ...blocks.flat()];
    while (words.length < AI_RUN_RECORD_WORDS) words.push(0);
    out.many("i16", words);
  }
  return out.finish();
}

function decodeHull(image) {
  const base = AI_HULL_PTRS - IMAGE_BASE;
  if (base < 0 || base + 4 * AI_HULL_LISTS.length > image.length) {
    throw new DecodeError(`hull pointer table at 0x${hex(AI_HULL_PTRS)} needs ${4 * AI_HULL_LISTS.length} bytes, image has ${image.length}`);
  }
  const dv = dvOf(image);
  const words = [];
  AI_HULL_LISTS.forEach((want, pid) => {
    const ptr = dv.getUint32(base + 4 * pid, false);
    if (ptr !== want) {
      throw new DecodeError(`hull list of part id ${pid}: the table 0x${hex(AI_HULL_PTRS)} holds 0x${hex(ptr)}, note 07 8.2a names 0x${hex(want)}`);
    }
    const off = want - IMAGE_BASE;
    if (off < 0 || off + 6 * (AI_HULL_FACES + 1) > image.length) {
      throw new DecodeError(`hull list of part id ${pid} at 0x${hex(want)} leaves the image`);
    }
    const t = [];
    for (let i = 0; i < 3 * (AI_HULL_FACES + 1); i++) t.push(dv.getInt16(off + 2 * i, false));
    for (let f = 0; f < AI_HULL_FACES; f++) {
      const [a, b, c] = t.slice(3 * f, 3 * f + 3);
      const ok = a >= 0 && [a & 0x3fff, b, c].every((v) => v >= 0 && v % 6 === 0 && Math.floor(v / 6) <= AI_HULL_SLOT_MAX);
      if (!ok) {
        throw new DecodeError(`hull list of part id ${pid} at 0x${hex(want)}: face ${f} (${hexLower0x(a & 0xffff)}, ${hexLower0x(b & 0xffff)}, ${hexLower0x(c & 0xffff)}) is not vertex offsets of slots 0 to ${AI_HULL_SLOT_MAX}`);
      }
      words.push(a, b, c);
    }
    if (t[3 * AI_HULL_FACES] >= 0) {
      throw new DecodeError(`hull list of part id ${pid} at 0x${hex(want)}: no terminator (first word with bit 15 set) after ${AI_HULL_FACES} faces`);
    }
    words.push(t[3 * AI_HULL_FACES], 0, 0);
  });
  return new ByteWriter(4 + 2 * words.length).u16(words.length / 3).u16(6).many("i16", words).finish();
}

export function decodeAi(image) {
  const parts = [];
  for (const a of AI_RATE_ADDRS) parts.push(aiSub(image, a, AI_RATE_ENTRIES, 1, `rate table 0x${hex(a)}`));
  parts.push(aiSub(image, AI_EVASION, AI_EVASION_GROUPS, 8, "evasion table"));
  parts.push(aiSub(image, AI_OFFSETS, AI_OFFSET_COUNT, 3, "offset table"));
  parts.push(aiSub(image, AI_RING3, AI_RING3_DIRECTIONS, 3, "ring offsets of class nibble 3"));
  parts.push(aiSub(image, AI_START, AI_START_ENTRIES, 8, "start blocks"));
  parts.push(aiSub(image, AI_CAM_POINTS, AI_CAM_POINT_COUNT, 6, "airfield camera points", 8));
  parts.push(aiSub(image, AI_FINE, AI_FINE_COUNT, 2, "fine offsets"));
  parts.push(aiSub(image, AI_PRE_WORD, 1, 1, "word before the rate tables"));
  parts.push(decodeRuns(image));
  parts.push(decodeHull(image));
  return concat(parts);
}

// ---- tables/burst.bin ----
const BURST_PTRS = 0x315a2;
const BURST_SHAPES = 0x315c2;
const BURST_SHAPE_COUNT = 8;
const BURST_SHAPE_BYTES = 18;
const BURST_OFFSETS = [0x31652, 0x31679, 0x316a0];
const BURST_DISCS = 0x316c7;
const BURST_PENS = [0x316e5, 0x316f1, 0x316fd, 0x31709, 0x31715];
const BURST_CYCLE = 0x31722;
const BURST_CYCLE_WORDS = 32;
const BURST_VERSION = 2;
const BURST_BYTES = 409;

export function decodeBurst(image) {
  const at = (addr, n, what) => {
    const off = addr - IMAGE_BASE;
    if (off < 0 || off + n > image.length) {
      throw new DecodeError(`burst table ${what} at 0x${hex(addr)}: needs ${n} bytes, image has ${image.length}`);
    }
    return off;
  };
  const dv = dvOf(image);
  const po = at(BURST_PTRS, 4 * BURST_SHAPE_COUNT, "pointer table");
  for (let k = 0; k < BURST_SHAPE_COUNT; k++) {
    const ptr = dv.getUint32(po + 4 * k, false);
    if (ptr !== BURST_SHAPES + BURST_SHAPE_BYTES * k) {
      throw new DecodeError(`burst pointer table at 0x${hex(BURST_PTRS)}: entry ${k} holds 0x${hex(ptr)}, note 07 11.1 puts shape ${k} at 0x${hex(BURST_SHAPES + BURST_SHAPE_BYTES * k)}`);
    }
  }
  const so = at(BURST_SHAPES, BURST_SHAPE_COUNT * BURST_SHAPE_BYTES, "shapes");
  const out = new ByteWriter(BURST_BYTES).ascii("BRST").u16(BURST_VERSION);
  for (let i = 0; i < 9 * BURST_SHAPE_COUNT; i++) out.i16(dv.getInt16(so + 2 * i, false));
  for (const a of BURST_OFFSETS) out.bytes(subRange(image, at(a, 36, "fragment offsets"), 36));
  out.bytes(subRange(image, at(BURST_DISCS, 27, "disc offsets"), 27));
  BURST_PENS.forEach((a, i) => {
    const pens = subRange(image, at(a, 12, "pen table"), 12);
    if (pens.some((p) => p > 15)) {
      throw new DecodeError(`burst pen table ${i} at 0x${hex(a)} holds a byte that is not a pen number (0 to 15)`);
    }
    out.bytes(pens);
  });
  const co = at(BURST_CYCLE, 2 * BURST_CYCLE_WORDS, "shape cycle");
  const cyc = [];
  for (let i = 0; i < BURST_CYCLE_WORDS; i++) cyc.push(dv.getUint16(co + 2 * i, false));
  if (cyc.some((w) => w % 4 || w >= 4 * BURST_SHAPE_COUNT)) {
    throw new DecodeError(`burst shape cycle at 0x${hex(BURST_CYCLE)} holds a word that is not the byte offset of a shape (a multiple of 4 below ${4 * BURST_SHAPE_COUNT})`);
  }
  out.many("u16", cyc);
  const bytes = out.finish();
  if (bytes.length !== BURST_BYTES) throw new Error("burst table size");
  return bytes;
}

// ---- tables/target_text.bin ----
const TARGET_BLANK = 0x3717c;
const TARGET_BLANK_CHARS = 26;
const TARGET_LABELS = 0x37197;
const TARGET_LABEL_COUNT = 3;
const TARGET_LABEL_CHARS = 4;
const TARGET_NAMES = 0x371a6;
const TARGET_NAME_COUNT = 8;
const TARGET_NAME_CHARS = 6;

export function decodeTargetText(image) {
  const strings = (addr, count, chars, what) => {
    const off = addr - IMAGE_BASE;
    const step = chars + 1;
    if (off < 0 || off + count * step > image.length) {
      throw new DecodeError(`target text ${what} at 0x${hex(addr)}: needs ${count * step} bytes, image has ${image.length}`);
    }
    const parts = [];
    for (let i = 0; i < count; i++) {
      const s = subRange(image, off + i * step, step);
      if (s[s.length - 1] !== 0 || s.subarray(0, s.length - 1).includes(0)) {
        throw new DecodeError(`target text ${what} ${i} at 0x${hex(addr + i * step)} is not ${chars} characters and a terminator`);
      }
      parts.push(s.subarray(0, s.length - 1));
    }
    return concat(parts);
  };
  const blank = strings(TARGET_BLANK, 1, TARGET_BLANK_CHARS, "blank line");
  if (blank.length !== TARGET_BLANK_CHARS || blank.some((c) => c !== 0x20)) {
    throw new DecodeError(`target text blank line at 0x${hex(TARGET_BLANK)} is not ${TARGET_BLANK_CHARS} spaces`);
  }
  const labels = strings(TARGET_LABELS, TARGET_LABEL_COUNT, TARGET_LABEL_CHARS, "label");
  const names = strings(TARGET_NAMES, TARGET_NAME_COUNT, TARGET_NAME_CHARS, "name");
  return new ByteWriter(68).u16(TARGET_LABEL_COUNT).u16(TARGET_LABEL_CHARS).u16(TARGET_NAME_COUNT).u16(TARGET_NAME_CHARS).bytes(labels).bytes(names).finish();
}

// ---- tables/prompts.bin ----
const PROMPTS = [0x36923, 0x36929];
const PROMPT_CHARS = 6;

export function decodePrompts(image) {
  const out = new ByteWriter(16).u16(PROMPTS.length).u16(PROMPT_CHARS);
  PROMPTS.forEach((addr, i) => {
    const off = addr - IMAGE_BASE;
    if (off < 0 || off + PROMPT_CHARS + 1 > image.length) {
      throw new DecodeError(`fire prompt ${i} at 0x${hex(addr)}: needs ${PROMPT_CHARS + 1} bytes, image has ${image.length}`);
    }
    const s = subRange(image, off, PROMPT_CHARS);
    if (s.some((c) => c < 0x20 || c > 0x7e)) {
      throw new DecodeError(`fire prompt ${i} at 0x${hex(addr)} holds a byte that is not a printable character`);
    }
    out.bytes(s);
  });
  if (image[PROMPTS[1] - IMAGE_BASE + PROMPT_CHARS] !== 0) {
    throw new DecodeError(`fire prompt 1 at 0x${hex(PROMPTS[1])} is not followed by a terminator`);
  }
  return out.finish();
}

// ---- tables/months.bin ----
const MONTHS_ADDR = 0x292cc;
const MONTH_COUNT = 4;
const MONTH_CHARS = 9;

export function decodeMonths(image) {
  const off = MONTHS_ADDR - IMAGE_BASE;
  const n = MONTH_COUNT * MONTH_CHARS;
  if (off < 0 || off + n > image.length) {
    throw new DecodeError(`month names at 0x${hex(MONTHS_ADDR)}: need ${n} bytes, image has ${image.length}`);
  }
  const out = new Uint8Array(n);
  for (let i = 0; i < MONTH_COUNT; i++) {
    const name = subRange(image, off + i * MONTH_CHARS, MONTH_CHARS);
    if (name.some((c) => c < 0x20 || c > 0x7e)) {
      throw new DecodeError(`month name ${i} at 0x${hex(MONTHS_ADDR + i * MONTH_CHARS)} holds a non-printable byte`);
    }
    out.set(name.reverse(), i * MONTH_CHARS);
  }
  return out;
}

// ---- tables/star.bin ----
const STAR_ADDR = 0x31542;
const STAR_SEGMENTS = 3;
const STAR_LIMIT = 64;

export function decodeStar(image) {
  const off = STAR_ADDR - IMAGE_BASE;
  const n = STAR_SEGMENTS * 6;
  if (off < 0 || off + 2 * n > image.length) {
    throw new DecodeError(`flare star at 0x${hex(STAR_ADDR)}: needs ${2 * n} bytes, image has ${image.length}`);
  }
  const dv = dvOf(image);
  const words = [];
  for (let i = 0; i < n; i++) words.push(dv.getInt16(off + 2 * i, false));
  if (words.some((w) => Math.abs(w) > STAR_LIMIT)) {
    throw new DecodeError(`flare star at 0x${hex(STAR_ADDR)} holds an offset beyond +-${STAR_LIMIT}: not the table of note 07 7.7`);
  }
  return new ByteWriter(2 + 2 * n).u16(STAR_SEGMENTS).many("i16", words).finish();
}

// ---- tables/briefing_chain.bin and tables/head_step.bin ----
const CHAIN_ADDR = 0x43ee6;
const CHAIN_ROW_OFFSETS = [0, 10, 22, 32, 42, 52];
const CHAIN_IDS = 5;
const CHAIN_ID_LIMIT = 110;
const HEAD_STEP_ADDR = 0x1e800;
const HEAD_STEP_BYTES = 14;
const HEAD_STEP_LIMIT = 64;

export function decodeBriefingChain(image) {
  const base = CHAIN_ADDR - IMAGE_BASE;
  const last = CHAIN_ROW_OFFSETS[CHAIN_ROW_OFFSETS.length - 1];
  if (base < 0 || base + last + 2 * CHAIN_IDS > image.length) {
    throw new DecodeError(`briefing chain at 0x${hex(CHAIN_ADDR)}: needs the bytes up to 0x${hex(CHAIN_ADDR + last + 2 * CHAIN_IDS)}, image has ${image.length}`);
  }
  const dv = dvOf(image);
  const out = [];
  CHAIN_ROW_OFFSETS.forEach((ro, r) => {
    for (let i = 0; i < CHAIN_IDS; i++) {
      const w = dv.getUint16(base + ro + 2 * i, false);
      if (!(w >= 1 && w <= CHAIN_ID_LIMIT)) {
        throw new DecodeError(`briefing chain row ${r} entry ${i} at 0x${hex(CHAIN_ADDR + ro + 2 * i)} holds ${w}, not a text record id (1 to ${CHAIN_ID_LIMIT})`);
      }
      out.push(w);
    }
  });
  return Uint8Array.from(out);
}

export function decodeHeadStep(image) {
  const off = HEAD_STEP_ADDR - IMAGE_BASE;
  if (off < 0 || off + HEAD_STEP_BYTES > image.length) {
    throw new DecodeError(`head step table at 0x${hex(HEAD_STEP_ADDR)}: needs ${HEAD_STEP_BYTES} bytes, image has ${image.length}`);
  }
  const raw = subRange(image, off, HEAD_STEP_BYTES);
  if (raw.some((b) => Math.abs(b > 127 ? b - 256 : b) > HEAD_STEP_LIMIT)) {
    throw new DecodeError(`head step table at 0x${hex(HEAD_STEP_ADDR)} holds a step beyond +-${HEAD_STEP_LIMIT}: not the table of note 06`);
  }
  return raw;
}

/** Adds tables/templates.bin to `files`. */
export function writeTemplates(image, files) {
  files.set("tables/templates.bin", decodeTemplates(image));
}

/** Adds tables/ai.bin to `files`. */
export function writeAi(image, files) {
  files.set("tables/ai.bin", decodeAi(image));
}

/** Adds the burst, target text, months, prompts, star, briefing chain and head step tables to `files` (the Python order). */
export function writeBurst(image, files) {
  files.set("tables/burst.bin", decodeBurst(image));
  files.set("tables/target_text.bin", decodeTargetText(image));
  files.set("tables/months.bin", decodeMonths(image));
  files.set("tables/prompts.bin", decodePrompts(image));
  files.set("tables/star.bin", decodeStar(image));
  files.set("tables/briefing_chain.bin", decodeBriefingChain(image));
  files.set("tables/head_step.bin", decodeHeadStep(image));
}
