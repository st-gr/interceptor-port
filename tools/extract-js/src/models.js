// Model decoder: drawing programs to a neutral intermediate form (tools/extract/extract/models.py).
//
// Layouts are those of docs/re/02-assets-and-tables.md sections 2 to 7 and 13. `decodeModel` is a pure function over the
// flat image bytes. The intermediate form and its binary file format ("IMDL" version 5, documented in
// tools/extract/README.md) keep the painter order, the per-side face selection (the BSP elements) and the gate words.
// The decoding rules, messages and the order of every step follow the Python module line by line: the error text of a
// model that fails goes into models/index.txt, so it must be the same text.
import { BlockedError, DecodeError, IMAGE_BASE, Mem, RECORD_COUNT, RECORD_SIZE, RECORD_TABLE, decodeRecord } from "./tables.js";
import { ByteWriter, hex } from "./util.js";

export const IMDL_MAGIC = "IMDL";
export const IMDL_VERSION = 5;

const TYPE_B_HANDLERS = [0x22cfc, 0x22d0a];

// Vertex states.
export const V_UNDEFINED = 0, V_BLOCK = 1, V_STRIP = 2, V_SYNTH = 3, V_UNRESOLVED = 4, V_SECTION = 5;
// Element kinds.
export const E_JUMP = 1, E_AXIS = 2, E_PLANE = 3, E_TRI = 4, E_DRAW = 5;
// Item flags.
export const F_LAST = 1, F_GATED = 2, F_BLOCKED = 4, F_TWO_SIDED = 8, F_BACK_COLOUR = 16, F_PLANE = 32, F_SPECULATIVE = 64;
// Element flags.
const EF_BLOCKED = 2;
const EF_END0 = 8, EF_END1 = 16;
// Primitive kinds.
const P_NOP = 0, P_POINT = 1, P_LINE = 2, P_POLYGON = 3, P_FACES = 4, P_QUAD = 5, P_RUNGS = 6, P_SEGMENTS = 7, P_LINELIST = 8;
const P_HATCH = 9, P_SYNTH = 10, P_SELECTOR = 11, P_POLYLIST = 12, P_TRIANGLE = 13, P_EARLYOUT = 14, P_FIGURE = 15, P_RAW = 16;

const SLOT_LIMIT = 512;
const MAX_STREAM_ELEMENTS = 4096;

function newLevel(o = {}) {
  return {
    threshold: 0, shape: 0, present: 1, leaf: 0, part_id: 0, tm: 0, tf: 0, scale_h: 0, scale_l: 0, block_flags: 0, b8: 0, b9: 0,
    root: -1, vertex_first: 0, vertex_count: 0, elem_first: 0, elem_count: 0, sec_first: 0, sec_count: 0, data_triple: [0, 0, 0],
    ...o,
  };
}

function newElement(kind, o = {}) {
  return { kind, flags: 0, axis: 0, scale: 0, child0: -1, child1: -1, s: [0, 0, 0, 0, 0, 0], list_first: 0, list_count: 0, ...o };
}

function newItem(op, address = 0) {
  return {
    op, flags: 0, g1: 0x7fff, g2: 0, extra_n: 0, extra_slot: 0, pen: 0, w: 0, wback: 0, prim: P_NOP, nops: 0, op_first: 0,
    address, length: 0, skip_first: 0, skip_count: 0, skips: [],
  };
}

function newModel(id, handler, kind) {
  return {
    id, handler, kind, levels: [], vertices: [], elements: [], proglists: [], programs: [], items: [], operands: [], skips: [],
    secpool: [], blocked: 0, nnormal: -1, alt_near: 0xffff, alt_cockpit: 0xffff, hull_flags: 0, phase_first: 0,
  };
}

// ---------------------------------------------------------------------------
// Operation table (section 5)

const OPS = new Map([
  [0, P_NOP], [1, P_POINT], [2, P_LINE], [3, P_POLYGON], [4, P_FACES], [6, P_RUNGS], [7, P_TRIANGLE],
  [8, P_TRIANGLE], [9, P_QUAD], [10, P_QUAD], [11, P_SEGMENTS], [12, P_SEGMENTS], [13, P_LINELIST],
  [14, P_LINELIST], [15, P_QUAD], [16, P_QUAD], [17, P_QUAD], [18, P_QUAD], [19, P_HATCH], [20, P_HATCH],
  [21, P_HATCH], [22, P_HATCH], [23, P_HATCH], [24, P_POLYGON], [25, P_POLYGON], [26, P_POLYGON],
  [27, P_QUAD], [30, P_QUAD], [31, P_EARLYOUT], [32, P_HATCH], [33, P_NOP], [34, P_SYNTH], [35, P_SYNTH],
  [36, P_SYNTH], [38, P_SYNTH], [46, P_SYNTH], [47, P_SYNTH], [39, P_QUAD], [40, P_TRIANGLE], [41, P_SYNTH], [42, P_SYNTH], [43, P_SYNTH],
  [44, P_SYNTH], [45, P_SYNTH], [48, P_LINE], [49, P_POINT], [50, P_SELECTOR], [51, P_SELECTOR],
  [52, P_SYNTH], [53, P_SYNTH], [54, P_SELECTOR], [55, P_POLYLIST], [56, P_SYNTH], [57, P_HATCH],
  [58, P_POINT], [59, P_SYNTH], [62, P_FIGURE], [63, P_SYNTH], [64, P_SYNTH], [65, P_SYNTH],
  [66, P_SELECTOR], [67, P_SELECTOR], [68, P_SELECTOR], [69, P_QUAD], [70, P_SELECTOR], [71, P_FIGURE], [72, P_SELECTOR], [73, P_SELECTOR],
  [74, P_LINE], [75, P_RAW], [76, P_SELECTOR], [77, P_SELECTOR],
]);

const FRAMES = 16;
const SPECULATIVE_OPS = [68, 72];

/** The bytes op 70 skips for the life word t (note 07 11.3). */
function op70Skip(t) {
  if (t < 0) return 0;
  let s = 2 + 0x86 * ((t & 7) >> 1);
  if (t < 0x0c) s += 0x26 + 8 * ((0x0b - t) >> 1);
  if (t > 0x74) s += 0x56 + 8 * ((t - 0x74) >> 1);
  return s;
}

function skipList(op, ops) {
  if (op === 70) {
    const set = new Set();
    for (let t = 0; t < 0x80; t++) set.add(op70Skip(t));
    set.delete(2);
    return [2, ...[...set].sort((a, b) => a - b)];
  }
  if (op === 50) return [0, 0x14, 0x52, 0x7c, 0xa6, 0xd0, 0xfa];
  if (op === 51) return [0, ops[0]];
  if (op === 53) return [ops[2], 0];
  if (op === 54 || op === 76) return [2, 0];
  if (op === 66) return [0, 8];
  if (op === 67) return [0, 18];
  if (op === 73) return [0, 2];
  if (op === 68) return [0, ...Array.from({ length: FRAMES - 1 }, (_, i) => 0x34 * (i + 1))];
  if (op === 72) return [0, ...Array.from({ length: FRAMES - 1 }, (_, i) => 0x12 * (i + 1))];
  return [];
}

// FIXED_SYNTH: no operand, fixed formulas on the absolute slots (note 03 'Fixed-formula synthesis'); 46 = 38, 47 = 45.
const FIXED_SYNTH = new Map([[38, 38], [46, 38], [45, 45], [47, 45], [56, 56], [63, 63], [64, 64], [65, 65]]);
const NO_OPERANDS = [0, 33, 50, 54, 66, 68, 72, 73, 76, 77];

class Reader {
  constructor(mem, addr) {
    this.mem = mem;
    this.addr = addr;
  }

  w() { const v = this.mem.u16(this.addr); this.addr += 2; return v; }
  sw() { const v = this.mem.s16(this.addr); this.addr += 2; return v; }
  peek() { return this.mem.u16(this.addr); }
  long() { const v = this.mem.u32(this.addr); this.addr += 4; return v; }
}

class Ctx {
  constructor(mem, modelId, kind, blockAddr, strict) {
    this.mem = mem;
    this.id = modelId;
    this.kind = kind;          // 0 3D, 1 flat, 2 type B
    this.block_addr = blockAddr;
    this.strict = strict;
    this.scale_l = 0;
    this.model_base = 0;
  }

  err(msg, addr = null, op = null, blocked = false) {
    const o = op === null ? "" : `: op ${op}`;
    const w = addr === null ? "" : ` at 0x${hex(addr)}`;
    const text = `model ${this.id}${o}${w}: ${msg}`;
    throw blocked ? new BlockedError(text) : new DecodeError(text);
  }
}

/** Vertex byte offset to slot index. */
function slotOf(ctx, word, itemAddr, op) {
  if (word % 6) ctx.err(`vertex operand ${word} is not a multiple of 6`, itemAddr, op);
  const s = Math.floor(word / 6);
  if (s >= SLOT_LIMIT) ctx.err(`bad vertex slot ${s}`, itemAddr, op);
  return s;
}

/** The same with bit 15 stripped and returned: [slot, last]. */
function slotLast(ctx, word, itemAddr, op) {
  const last = Boolean(word & 0x8000);
  return [slotOf(ctx, word & 0x7fff, itemAddr, op), last];
}

function polyTail(ctx, r, item, ops) {
  const W = r.w();
  item.w = W;
  item.pen = W & 0xf;
  if (W & 0x8000) {
    item.flags |= F_TWO_SIDED;
    return;
  }
  if ((W & 0x1000) && !(W & 0x0c00)) {
    const off = r.w();
    item.flags |= F_PLANE;
    const pa = ctx.model_base + off;
    for (let k = 0; k < 6; k++) ops.push(ctx.mem.s16(pa + 2 * k));
  }
  if (W & 0x4000) {
    item.flags |= F_BACK_COLOUR;
    item.wback = r.w();
  }
}

/** Decodes one program item at `addr`. Returns [item, operands, slot refs, synth record or null, next address]. */
function parseItem(ctx, addr) {
  const mem = ctx.mem;
  const r = new Reader(mem, addr);
  const item = newItem(0, addr);
  const first = r.peek();
  let opword;
  if (first & 0x8000) {
    opword = r.w();
  } else {
    item.flags |= F_GATED;
    item.g1 = r.w();
    if (item.g1 !== 0x7fff) item.g2 = r.w();
    const n = r.sw();
    if (n > 0) {
      const start = r.w();
      item.extra_n = n;
      item.extra_slot = start;       // byte offset; converted once the block kind is known
    }
    opword = r.w();
  }
  if (opword & 3) ctx.err(`operation word 0x${hex(opword, 4)} is not a multiple of 4`, addr);
  const op = (opword & 0x3fff) >> 2;
  item.op = op;
  if (opword & 0x4000) item.flags |= F_LAST;
  if (!OPS.has(op)) ctx.err(`unknown operation ${op}`, addr, op);
  item.prim = OPS.get(op);
  let ops = [];
  const slots = [];
  let synth = null;

  const S = (word) => {
    const s = slotOf(ctx, word, addr, op);
    slots.push(s);
    return s;
  };
  const SL = (word) => {
    const [s, last] = slotLast(ctx, word, addr, op);
    slots.push(s);
    return [s, last];
  };
  const slotRange = (firstSlot, n) => {
    for (let i = 0; i < n; i++) slots.push(firstSlot + i);
  };
  const colour = (word) => word & 0xf;
  const words = (n) => {
    const out = [];
    for (let i = 0; i < n; i++) out.push(r.w());
    return out;
  };
  const sList = (n) => {
    const out = [];
    for (let i = 0; i < n; i++) out.push(S(r.w()));
    return out;
  };

  if (op === 7 || op === 8) {
    // Note 03: op 7 = pen, a, b|flip, c, d; op 8 = pen, c, a, b|flip, f, d.
    ops = words(op === 7 ? 5 : 6);
    item.pen = ops[0] & 0xf;
    item.prim = P_TRIANGLE;
    const [c, f, d] = op === 7 ? [ops[3], null, ops[4]] : [ops[1], ops[4], ops[5]];
    const cs = S(c);
    const base = S(d);
    slotRange(base, op === 7 ? 3 : 4);
    if (op === 8) {
      slots.push(cs + 1);
      S(f);
    }
  } else if (FIXED_SYNTH.has(op)) {
    synth = [FIXED_SYNTH.get(op), 0, 0];
  } else if (NO_OPERANDS.includes(op)) {
    // the op word alone
  } else if (op === 1) {
    const v = S(r.w()); item.pen = colour(r.w()); ops = [v];
  } else if (op === 2 || op === 48 || op === 74) {
    const a = S(r.w()); const b = S(r.w()); item.pen = colour(r.w()); ops = [a, b];
  } else if (op === 3) {
    const vs = [];
    for (;;) {
      const [s, last] = SL(r.w());
      vs.push(s);
      if (last) break;
    }
    ops = [vs.length, ...vs];
    polyTail(ctx, r, item, ops);
  } else if (op === 4) {
    const faces = [];
    for (;;) {
      const d = r.w();
      if (d & 0x8000) break;
      const n = r.w();
      const flag = r.w();
      const vs = sList(n);
      faces.push([d, n, flag, ...vs]);
    }
    ops.push(faces.length);
    for (const f of faces) ops.push(...f);
  } else if (op === 6) {
    const n = r.w(); const vs = sList(4); item.pen = colour(r.w()); ops = [n, ...vs];
  } else if (op === 9 || op === 10) {
    item.pen = colour(r.w()); const v = S(r.w()); slotRange(v, op === 9 ? 3 : 4); ops = [v];
  } else if (op === 11 || op === 12) {
    const wd = r.w(); item.w = wd; item.pen = colour(wd);
    const cnt = wd >> 8;
    let v;
    if (op === 12) {
      const vr = S(r.w()); slots.push(vr + 1);
      v = S(r.w()); ops = [cnt, vr, v];
    } else {
      v = S(r.w()); ops = [cnt, v];
    }
    slotRange(v, 2 * cnt);
  } else if (op === 13 || op === 14) {
    item.pen = colour(r.w());
    if (op === 14) {
      const vr = S(r.w()); slots.push(vr + 1); ops.push(vr);
    }
    for (;;) {
      const a = S(r.w()); const [b, last] = SL(r.w());
      ops.push(a, b);
      if (last) break;
    }
  } else if (op === 15) {
    item.pen = colour(r.w()); const [a, b, c, e] = sList(4); slots.push(a + 1);
    ops = [a, b, c, e];
  } else if (op === 16) {
    item.pen = colour(r.w()); const [a, b, c] = sList(3); slots.push(b + 1);
    ops = [a, b, c];
  } else if (op === 17 || op === 18) {
    item.pen = 12;
    for (;;) {
      if (r.peek() & 0x8000) {
        r.w();
        break;
      }
      ops.push(...sList(4));
    }
  } else if (op === 19 || op === 22 || op === 32) {
    item.pen = colour(r.w()); const v = S(r.w()); slotRange(v, 3);
    if (op === 22) {
      const n1 = r.w(); const inner = words(n1); ops = [v, n1, ...inner];
    } else {
      ops = [v, r.w()];
    }
  } else if (op === 20 || op === 23) {
    const v = S(r.w()); slotRange(v, 3); item.pen = op === 20 ? 13 : 0; ops = [v, r.w()];
  } else if (op === 21) {
    const v = S(r.w()); slotRange(v, 3); item.pen = 13;
    const n1 = r.w(); ops = [v, n1, ...words(n1)];
  } else if (op === 57) {
    item.pen = colour(r.w()); const v = S(r.w()); slotRange(v, 3);
    const n1 = r.w(); ops = [v, n1, ...words(n1)];
  } else if (op === 24) {
    const n = r.w(); const vs = sList(n); item.pen = colour(r.w()); ops = [n, ...vs];
  } else if (op === 25) {
    const n = r.w(); item.pen = colour(r.w()); ops = [n]; slotRange(0, n);
  } else if (op === 26) {
    const n = r.w(); const v = S(r.w()); slotRange(v, n); item.pen = 7; ops = [n, v];
  } else if (op === 27 || op === 30) {
    item.pen = colour(r.w()); const v = S(r.w()); slotRange(v, 3); ops = [v];
  } else if (op === 31) {
    ops = [r.w()];
  } else if (op === 34 || op === 36) {
    const v = slotOf(ctx, r.w(), addr, op); const s = r.sw(); ops = [v, s];
    synth = [op, v, s];
  } else if (op === 41) {
    const v = slotOf(ctx, r.w(), addr, op); const s = r.sw(); ops = [v, s];
    synth = [op, v, s];
  } else if (op === 35 || op === 42 || op === 43 || op === 44 || op === 59) {
    const v = slotOf(ctx, r.w(), addr, op); ops = [v];
    synth = [op, v, 0];
  } else if (op === 39 || op === 40) {
    ops = words(op === 39 ? 3 : 2);
    slots.push(...(op === 39 ? [0, 1, 2, 3] : [0, 1, 4]));     // absolute slots (note 5)
  } else if (op === 49) {
    const [a, b, c] = sList(3); const W = r.w(); item.w = W; ops = [a, b, c];
  } else if (op === 51) {
    ops = [r.w()];
  } else if (op === 52) {
    const a = slotOf(ctx, r.w(), addr, op); const b = slotOf(ctx, r.w(), addr, op); const t = slotOf(ctx, r.w(), addr, op);
    ops = [a, b, t];
    synth = [52, a, b, t];
  } else if (op === 53) {
    const a = slotOf(ctx, r.w(), addr, op); const b = slotOf(ctx, r.w(), addr, op); const skip = r.w();
    ops = [a, b, skip]; synth = [53, a, b];
  } else if (op === 55) {
    const base = r.long();
    const offs = [];
    for (;;) {
      const o = r.w();
      if (o & 0x8000) break;
      offs.push(o);
    }
    const polys = [];
    for (const o of offs) {
      const pr = new Reader(mem, base + o);
      const vs = [];
      for (;;) {
        const [s, last] = slotLast(ctx, pr.w(), base + o, op);
        vs.push(s);
        if (last) break;
      }
      const pitem = newItem(3, base + o);
      const pops = [vs.length, ...vs];
      polyTail(ctx, pr, pitem, pops);
      polys.push([pitem.w, pitem.wback, pitem.flags, ...pops]);
      slots.push(...vs);
    }
    ops = [polys.length, ...polys.flat()];
  } else if (op === 58) {
    const v = S(r.w()); item.pen = colour(r.w()); ops = [v];
  } else if (op === 62) {
    ops = words(3);
  } else if (op === 67) {
    const vs = sList(3); const W = r.w(); item.w = W; ops = vs;
  } else if (op === 69) {
    const [a, b, c] = sList(3); ops = [a, b, c];
    polyTail(ctx, r, item, ops);
  } else if (op === 70) {
    ops = [];      // the op word alone: the skip is a function of the object's life word (skipList)
  } else if (op === 71) {
    // a list of discs as op 62: triples (vertex byte offset, pen, size) up to the one whose size word has bit 15 set
    const trip = [];
    for (;;) {
      const v = r.w(), pen = r.w(), size = r.w();
      trip.push(v, pen, size & 0x7fff);
      if (size & 0x8000) break;
    }
    ops = [trip.length / 3, ...trip];
  } else if (op === 75) {
    ops = words(5);
  } else {
    ctx.err(`operation ${op} has no argument layout`, addr, op);
  }
  item.length = r.addr - addr;
  item.skips = skipList(op, ops);
  return [item, ops, slots, synth, r.addr];
}

/** All items of the program at `start` including the state variants reachable through the selector skip lists. Returns
 *  [entries sorted by address, addresses of the state-zero path]; an entry is [item, ops, slots, synth]. */
function decodeProgram(ctx, start) {
  const found = new Map();   // address -> entry
  const pending = [[start, false]];
  while (pending.length) {
    let [a, spec] = pending.shift();
    while (!found.has(a)) {
      const [item, ops, slots, synth, end] = parseItem(ctx, a);
      if (spec) item.flags |= F_SPECULATIVE;
      found.set(a, [item, ops, slots, synth]);
      for (const sk of item.skips) {
        if (sk) pending.push([end + sk, spec || SPECULATIVE_OPS.includes(item.op)]);
      }
      a = end;
      if (item.flags & F_LAST) break;
    }
  }
  const order = [...found.keys()].sort((x, y) => x - y);
  for (let i = 0; i + 1 < order.length; i++) {
    const a = order[i], b = order[i + 1];
    if (a + found.get(a)[0].length > b) ctx.err(`skip target 0x${hex(b)} falls inside the item at 0x${hex(a)}`, b, found.get(a)[0].op);
  }
  const straight = [];
  let a = start;
  for (;;) {
    const item = found.get(a)[0];
    straight.push(a);
    if (item.flags & F_LAST) break;
    a = a + item.length + (item.skips.length ? item.skips[0] : 0);
    if (!found.has(a)) ctx.err(`state-zero path leaves the decoded items at 0x${hex(a)}`, item.address, item.op);
  }
  return [order.map((x) => found.get(x)), straight];
}

// ---------------------------------------------------------------------------
// Block, strip section, synthesis

/** Points after `prev` up to and including `b` for a division count n: n-1 interior points with four extra fraction bits and
 *  truncating division, then b. n of 1 or less means no cut. */
function segment(prev, b, n) {
  if (n <= 1) return [b];
  const steps = prev.map((p, i) => Math.trunc(((b[i] - p) * 16) / n));
  const out = [];
  for (let k = 1; k < n; k++) out.push(prev.map((p, i) => p + Math.floor((steps[i] * k) / 16)));
  out.push(b);
  return out;
}

/** The strip section of a 3D block (section 2.4); returns Map<slot, [x, y, z]>. */
function stripSlots(ctx, pos, firstSlot) {
  const mem = ctx.mem;
  const slots = new Map();
  let s0 = firstSlot;
  const vec = (p) => [mem.s16(p), mem.s16(p + 2), mem.s16(p + 4)];
  for (;;) {
    const c = mem.s16(pos);
    pos += 2;
    if (c < 0) break;
    const a = vec(pos);
    pos += 6;
    const fwd = [a];
    let prev = a;
    for (let i = 0; i < c; i++) {
      const b = vec(pos);
      const n = mem.s16(pos + 6);
      pos += 8;
      fwd.push(...segment(prev, b, n));
      prev = b;
    }
    fwd.forEach((p, i) => slots.set(s0 + 2 * i, p));
    const end = s0 + 2 * fwd.length;       // one past the last forward point
    const d = mem.u16(pos);
    pos += 2;
    const count = d & 0x7fff;
    const pts = [];
    for (let i = 0; i < count; i++) {
      const b = vec(pos);
      const n = mem.s16(pos + 6);
      pos += 8;
      pts.push(...segment(prev, b, n));
      prev = b;
    }
    let slot = end - ((d & 0x8000) ? 1 : 3);
    for (const p of pts) {
      if (slot < s0) ctx.err("strip backward pass runs below its chain", pos);
      slots.set(slot, p);
      slot -= 2;
    }
    s0 = end;
    if (slots.size > SLOT_LIMIT) ctx.err("strip section too long", pos);
  }
  return slots;
}

class Miss extends Error {}

const wrap16 = (x) => ((x + 0x8000) & 0xffff) - 0x8000;

/** A 3-vector of 16-bit words; every operation wraps to 16 bits (note 03, vertex synthesis). */
class W {
  constructor(v) {
    this.v = [wrap16(v[0]), wrap16(v[1]), wrap16(v[2])];
  }

  add(o) { return new W(this.v.map((a, i) => a + o.v[i])); }
  sub(o) { return new W(this.v.map((a, i) => a - o.v[i])); }
  mul(n) { return new W(this.v.map((a) => a * n)); }
  h() { return new W(this.v.map((a) => a >> 1)); }
  q() { return new W(this.v.map((a) => a >> 2)); }
}

/** Shift left by s (arithmetic right by -s when s is negative) per component, wrapped. */
function shl(w, s) {
  return new W(w.v.map((x) => {
    if (s >= 0) return s >= 16 ? 0 : x * (1 << s);
    return x >> Math.min(-s, 31);
  }));
}

function s34(V, s, full = true) {
  const r = {
    4: V(0).sub(V(1)).add(V(2)),
    5: V(0).sub(V(1)).add(V(3)),
    6: V(3).add(V(2)).sub(V(1)),
    7: V(0).sub(V(1).mul(2)).add(V(2)).add(V(3)),
  };
  if (full) {
    const d = shl(V(2).sub(V(3)), s);
    r[8] = V(3).sub(d);
    r[9] = V(3).sub(d).sub(V(1)).add(V(2));
  }
  return r;
}

const s35 = (V, s) => s34(V, s, false);

function s36(V, s) {
  const r = {
    6: V(0).sub(V(1)).add(V(2)), 7: V(0).sub(V(1)).add(V(3)), 8: V(0).sub(V(1)).add(V(4)),
    9: V(0).sub(V(1)).add(V(5)), 10: V(2).add(V(5)).sub(V(1)), 11: V(0).sub(V(1).mul(2)).add(V(2)).add(V(5)),
  };
  const d = shl(V(3).sub(V(4)), s);
  r[12] = V(4).sub(d);
  r[13] = V(4).sub(d).sub(V(2)).add(V(3));
  return r;
}

function s41(V, s) {
  const d = shl(V(2).sub(V(3)), s);
  const r = { 9: V(3).add(V(2)).sub(V(1)), 10: V(3).sub(d) };
  r[11] = V(5).sub(V(6)).add(V(7));
  r[12] = r[11].add(V(8)).sub(V(6));
  const t = V(4).sub(V(6));
  r[13] = V(5).add(t);
  r[14] = V(8).add(t);
  r[15] = V(7).add(t);
  r[16] = r[11].add(t);
  r[17] = r[12].add(t);
  return r;
}

const midpoint = (V) => V(1).add(V(2)).h();

function s38(V) {
  return {
    11: V(0).add(V(1)).sub(V(2)), 12: V(0).add(V(1)).sub(V(4)), 13: V(0).add(V(1)).sub(V(3)),
    14: V(0).sub(V(1)).add(V(6)), 15: V(2).add(V(6)).sub(V(1)), 16: V(0).sub(V(2)).add(V(6)),
    17: V(3).sub(V(4)).add(V(9)), 18: V(0).add(V(1)).sub(V(4).mul(2)).add(V(9)),
    19: V(0).add(V(1)).sub(V(3)).sub(V(4)).add(V(9)), 20: V(5).mul(3).sub(V(7).mul(2)),
  };
}

function s45(V) {
  const M = midpoint(V);
  const r = {
    5: M, 6: M.mul(2).sub(V(3)), 7: M.add(V(0).sub(M).q()),
    8: V(1).add(V(3)).add(V(4)).sub(M.mul(2)), 9: V(2).add(V(3)).add(V(4)).sub(M.mul(2)), 10: V(3).mul(2).add(V(4)).sub(M.mul(2)),
  };
  r[11] = r[7].mul(3).sub(V(0).mul(2));
  return r;
}

function s56(V) {
  return {
    15: V(4).mul(2).sub(V(7)), 16: V(4).mul(2).sub(V(8)), 17: V(4).mul(2).sub(V(9)), 18: V(4).mul(2).sub(V(10)),
    19: V(3).mul(2).sub(V(11)), 20: V(3).mul(2).sub(V(12)), 21: V(3).mul(2).sub(V(13)), 22: V(3).mul(2).sub(V(14)),
  };
}

function s63(V) {
  return {
    16: V(0).sub(V(1)).add(V(2)), 17: V(3).sub(V(4)).add(V(5)), 18: V(7).sub(V(8)).add(V(9)),
    19: V(10).sub(V(11)).add(V(12)), 20: V(13).sub(V(14)).add(V(15)),
  };
}

function s64(V) {
  const M = midpoint(V);
  return {
    23: V(1).add(V(2)).sub(V(0)), 24: M, 25: V(0).sub(M.sub(V(0)).h()),
    26: V(0).sub(M.sub(V(0)).h()).sub(M.sub(V(1)).h()), 27: V(1).sub(M.sub(V(1)).h()),
    28: V(1).sub(M.sub(V(0)).h()).sub(M.sub(V(1)).h()),
  };
}

function s65(V) {
  return { 23: V(1).add(V(2)).sub(V(0)), 24: V(2).add(V(5)).sub(V(0)) };
}

const range = (a, b) => Array.from({ length: b - a }, (_, i) => a + i);

// Dict-style syntheses (op -> [function of (V, s), slots written]): every result is a word vector.
const DICT_SYNTH = new Map([
  [34, [s34, range(4, 10)]], [35, [s35, range(4, 8)]], [36, [s36, range(6, 14)]], [41, [s41, range(9, 18)]],
  [38, [s38, range(11, 21)]], [45, [s45, range(5, 12)]], [56, [s56, range(15, 23)]],
  [63, [s63, range(16, 21)]], [64, [s64, range(23, 29)]], [65, [s65, range(23, 25)]],
]);

// Sequential syntheses: [slot, function of (V, Wk)]; Wk reads back a slot written earlier by the same op.
const SYNTH_FUNCS = new Map([
  [42, [
    [17, (V) => V(3).add(V(4)).sub(V(8))],
    [18, (V) => V(2).sub(V(1)).add(V(6))],
    [19, (V) => V(6).sub(V(1)).h().add(V(7))],
    [20, (V) => V(6).sub(V(1)).h().add(V(16))],
    [21, (V) => V(6).sub(V(1)).h().add(V(3).add(V(4)).h()).add(V(6).sub(V(1)).mul(2))],
    [22, (V, Wk) => Wk(21).add(V(0)).h()],
    [23, (V) => V(4).add(V(5)).sub(V(8))],
    [24, (V) => V(8).add(V(11)).h()],
    [25, (V) => V(4).add(V(5)).sub(V(3))],
    [26, (V) => V(6).add(V(9)).sub(V(1))],
    [27, (V) => V(6).mul(2).sub(V(1))],
    [28, (V) => V(2).sub(V(1).mul(2)).add(V(6).mul(2))],
    [29, (V) => V(6).sub(V(1)).h().add(V(0))],
    [100, (V) => V(13).add(V(14)).h().add(V(10)).h()],
  ]],
  [43, [
    [10, (V) => V(3).add(V(4)).sub(V(6))],
    [11, (V) => V(3).add(V(4)).h().add(V(5).sub(V(3)).q())],
    [12, (V, Wk) => Wk(11).add(V(0)).h()],
    [13, (V) => V(7).add(V(9).sub(V(7)).q()).add(V(9).sub(V(7)).q().h())],
    [14, (V) => V(4).add(V(5)).sub(V(3))],
    [15, (V) => V(3).add(V(4)).sub(V(7))],
  ]],
  [44, [
    [5, (V) => V(0).add(V(1).sub(V(0)).h())],
    [6, (V) => V(0).add(V(1).sub(V(0)).h().h())],
  ]],
  [59, [
    [21, (V) => V(6).sub(V(7)).add(V(10))],
    [22, (V) => V(3).add(V(4)).h()],
    [23, (V) => V(3).add(V(4)).h().mul(2).sub(V(9))],
    [24, (V) => V(3).add(V(4)).h().mul(2).sub(V(8))],
    [25, (V) => V(4).add(V(5)).sub(V(3))],
    [26, (V) => V(4).sub(V(3)).add(V(5).mul(2)).h().add(V(5)).h()],
    [27, (V) => V(4).sub(V(3)).add(V(5).mul(2)).h().add(V(4)).sub(V(3)).add(V(5)).h()],
    [28, (V) => V(3).add(V(4)).h().mul(4).sub(V(3).mul(2)).add(V(5).mul(2)).sub(V(8)).sub(V(9)).h()],
    [29, (V) => V(1).add(V(2)).h()],
    [30, (V) => V(6).sub(V(7)).add(V(12))],
    [31, (V) => V(6).sub(V(7)).add(V(14))],
    [32, (V) => V(6).sub(V(7)).h().add(V(1)).add(V(6)).sub(V(7))],
    [33, (V) => V(6).sub(V(7)).h().add(V(2)).add(V(6)).sub(V(7))],
    [34, (V) => V(6).sub(V(7)).h().add(V(11)).add(V(6).sub(V(7)).mul(2))],
    [35, (V) => V(6).sub(V(7)).h().add(V(13)).add(V(6).sub(V(7)).mul(2))],
    [36, (V) => V(6).sub(V(7)).h().add(V(1)).add(V(6).sub(V(7)).mul(2))],
    [37, (V) => V(6).sub(V(7)).h().add(V(2)).add(V(6).sub(V(7)).mul(2))],
    [38, (V) => V(3).add(V(4)).h().mul(2).sub(V(3)).add(V(5)).sub(V(9))],
    [39, (V) => V(3).add(V(4)).h().mul(2).sub(V(3)).add(V(5)).sub(V(8))],
    [110, (V) => V(18).add(V(19)).h().add(V(16)).h()],
  ]],
]);

/** Applies one synthesis record to `arr` (Map<slot, [x, y, z, state]>). All arithmetic is 16-bit words. */
function applySynth(arr, rec) {
  const get = (s) => {
    const e = arr.get(s);
    if (e === undefined || (e[3] !== V_BLOCK && e[3] !== V_STRIP && e[3] !== V_SYNTH)) return null;
    return e.slice(0, 3);
  };
  const put = (s, v) => {
    arr.set(s, v !== null ? [v[0], v[1], v[2], V_SYNTH] : [0, 0, 0, V_UNRESOLVED]);
  };

  const op = rec[0];
  if (op === 52) {
    const [, sa, sb, st] = rec;
    const a = get(sa), bb = get(sb);
    for (const [t, off] of [[3, 0], [4, 1], [5, 2]]) {
      const src = get(st + off);
      if (a === null || bb === null || src === null) {
        put(st + t, null);
        continue;
      }
      let d = new W(bb).sub(new W(a));
      if (t !== 3) d = d.h();
      put(st + t, new W(src).add(d).v);
    }
    return;
  }
  if (op === 53) {
    const [, sa, sb] = rec;
    const a = get(sa);
    for (let j = 0; j < 3; j++) {
      const bj = get(sb + j);
      for (let k = 1; k < 6; k++) {
        const src = get(sa + k);
        if (a === null || bj === null || src === null) put(sa + 30 + 5 * j + k - 1, null);
        else put(sa + 30 + 5 * j + k - 1, new W(src).add(new W(bj)).sub(new W(a)).v);
      }
    }
    return;
  }
  const [, b, s] = rec;
  const V = (k) => {
    const v = get(b + k);
    if (v === null) throw new Miss();
    return new W(v);
  };
  if (DICT_SYNTH.has(op)) {
    const [fn, keys] = DICT_SYNTH.get(op);
    try {
      const r = fn(V, s);
      for (const off of Object.keys(r)) put(b + Number(off), r[off].v);
    } catch (e) {
      if (!(e instanceof Miss)) throw e;
      for (const off of keys) put(b + off, null);
    }
    return;
  }
  const done = new Map();
  const Wk = (k) => {
    if (done.has(k)) return done.get(k);
    throw new Miss();
  };
  for (const [off, fn] of SYNTH_FUNCS.get(op)) {
    try {
      const w = fn(V, Wk);
      done.set(off, w);
      put(b + off, w.v);
    } catch (e) {
      if (!(e instanceof Miss)) throw e;
      put(b + off, null);
    }
  }
}

// ---------------------------------------------------------------------------
// Streams (section 3)

class Stream {
  constructor(ctx, base) {
    this.ctx = ctx;
    this.base = base;
    this.elems = [];
    this.memo = new Map();
    this.queue = [];
    this.proglists = [];     // local program indices
    this.progs = [];         // local: [address, entries, straight]
    this.prog_at = new Map();
    this.slotrefs = [];      // [slot, element address, element index]
  }

  program(addr) {
    if (this.prog_at.has(addr)) return this.prog_at.get(addr);
    if (!this.ctx.mem.has(addr, 2)) this.ctx.err(`program address 0x${hex(addr)} outside the image`, addr);
    const [entries, straight] = decodeProgram(this.ctx, addr);
    const idx = this.progs.length;
    this.prog_at.set(addr, idx);
    this.progs.push([addr, entries, straight]);
    return idx;
  }

  /** Element index for the stream position `addr` (-1 for the 0xFFFF end word); new positions are queued, not decoded recursively. */
  ref(addr, forceEnd = false) {
    if (this.memo.has(addr)) return this.memo.get(addr);
    if (this.elems.length >= MAX_STREAM_ELEMENTS) this.ctx.err("stream too large", addr);
    if (this.ctx.mem.u16(addr) === 0xffff) return -1;
    const idx = this.elems.length;
    this.elems.push(newElement(E_DRAW));
    this.memo.set(addr, idx);
    this.queue.push([addr, idx, forceEnd]);
    return idx;
  }

  element(addr, forceEnd = false) {
    const root = this.ref(addr, forceEnd);
    while (this.queue.length) {
      const [a, idx, fe] = this.queue.pop();
      this.decode(a, idx, fe);
    }
    return root;
  }

  decode(addr, idx, forceEnd) {
    const ctx = this.ctx, mem = ctx.mem;
    const e = this.elems[idx];
    const w1 = mem.u16(addr);
    const base = this.base;

    const child = (off, which = 0) => {
      const a = base + off;
      if (!mem.has(a, 2)) ctx.err(`child offset 0x${hex(off)} is outside the image`, addr);
      if (mem.u16(a) === 0xffff) e.flags |= which ? EF_END1 : EF_END0;
      return this.ref(a);
    };
    const slot = (word) => {
      const s = slotOf(ctx, word, addr, null);
      this.slotrefs.push([s, addr, idx]);
      return s;
    };

    if (!(w1 & 0x8000)) {
      const w2 = mem.u16(addr + 2);
      if ((w2 & 0xfc00) === 0xfc00) {
        e.kind = E_JUMP;
        e.child0 = child(mem.u16(addr + 4));
      } else if (w2 & 0x0c00) {
        e.kind = E_AXIS;
        e.axis = (w2 >> 10) & 3;
        e.flags = (w2 >> 12) & 1;
        e.s[0] = slot(w1);
        e.scale = ctx.scale_l;
        e.child0 = child(mem.u16(addr + 4));
        e.child1 = child(mem.u16(addr + 6), 1);
      } else if (w2 & 0x2000) {
        e.kind = E_PLANE;
        e.s = [0, 1, 2, 3, 4, 5].map((k) => mem.s16(addr + 4 + 2 * k));
        e.scale = ctx.scale_l;
        e.child0 = child(mem.u16(addr + 16));
        e.child1 = child(mem.u16(addr + 18), 1);
      } else if (w2 & 0x1000) {
        ctx.err("node has bit 12 set without an axis field or bit 13 (undocumented plane source)", addr);
      } else {
        e.kind = E_TRI;
        e.scale = (w2 >> 7) & 7;
        e.s[0] = slot(w1);
        e.s[1] = slot(mem.u16(addr + 4));
        e.s[2] = slot(mem.u16(addr + 6));
        e.child0 = child(mem.u16(addr + 8));
        e.child1 = child(mem.u16(addr + 10), 1);
      }
      return;
    }
    // draw item
    e.kind = E_DRAW;
    e.flags = (w1 >> 12) & 7;
    let p = addr + 2;
    let ref;
    if (w1 & 0x1000) {
      ref = mem.u32(p);
      p += 4;
    } else {
      ref = base + (w1 & 0xfff);
    }
    const first = this.proglists.length;
    let ends;
    if (w1 & 0x2000) {
      this.proglists.push(this.program(ref));
      ends = true;
    } else {
      let q = ref;
      for (;;) {
        const ent = mem.u16(q);
        q += 2;
        let prog;
        if (ent & 0x1000) {
          prog = mem.u32(q);
          q += 4;
        } else {
          prog = base + (ent & 0xfff);
        }
        this.proglists.push(this.program(prog));
        if (ent & 0x8000) break;
      }
      ends = Boolean(w1 & 0x4000);
    }
    e.list_first = first;
    e.list_count = this.proglists.length - first;
    if (ends) {
      e.child0 = -1;
    } else {
      if (forceEnd) ctx.err("inline stream item does not end the stream", addr);
      if (mem.has(p, 2) && mem.u16(p) === 0xffff) e.flags |= EF_END0;
      e.child0 = this.ref(p);
    }
  }
}

// ---------------------------------------------------------------------------
// Detail list (section 2.1) and levels

/** Returns [[threshold, shape, leaf, stream address, inline], ...] in near-to-far order. */
function walkDetailList(mem, base, ctxErr) {
  const specs = [];
  let pos = 0;
  const seen = new Set();
  for (;;) {
    if (seen.has(pos)) ctxErr("detail list loops", base + pos);
    seen.add(pos);
    const a = base + pos;
    const w = mem.u16(a);
    if (w & 0x8000) {
      specs.push([0, 2, w, a + 2, false]);
      return specs;
    }
    if (!(w & 0x4000)) {
      const jump = mem.u16(a + 2);
      const leaf = mem.u16(a + 4);
      specs.push([w & 0x3fff, 0, leaf, a + 6, false]);
      if (jump & 0x8000) return specs;
      pos = jump;
    } else {
      const leaf = mem.u16(a + 2);
      specs.push([w & 0x3fff, 1, leaf, a + 4, true]);
      pos += 6;
    }
    if (specs.length > 32) ctxErr("detail list too long", a);
  }
}

function toBlockSlot(ctx, off, flat, addr) {
  const stride = flat ? 4 : 6;
  if (off % stride) ctx.err(`extra-vertex offset ${off} is not a multiple of ${stride}`, addr);
  return Math.floor(off / stride);
}

/** Builds the vertex array for a level and validates every slot reference. Returns [array (Map), blocked item count]. */
function finishLevel(ctx, st, blockVertex, nblock, flat, strip, secSlots = 0) {
  const arr = new Map();
  for (let i = 0; i < nblock; i++) {
    const [x, y, z] = blockVertex(i);
    arr.set(i, [x, y, z, V_BLOCK]);
  }
  // extras (static union), then strip slots, then synthesis along the straight path
  for (const [, entries] of st.progs) {
    for (const [item] of entries) {
      if (item.extra_n > 0) {
        const first = toBlockSlot(ctx, item.extra_slot, flat, item.address);
        item.extra_slot = first;
        for (let k = first; k < first + item.extra_n; k++) {
          if (!arr.has(k)) {
            const [x, y, z] = blockVertex(k);
            arr.set(k, [x, y, z, V_BLOCK]);
          }
        }
      }
    }
  }
  if (strip) for (const [slot, p] of strip) arr.set(slot, [p[0], p[1], p[2], V_STRIP]);
  // Slots of the second vertex section: placeholders the core overwrites at run time.
  for (let k = nblock; k < nblock + secSlots; k++) {
    if (arr.has(k)) ctx.err(`second-section slot ${k} collides with another vertex source`);
    arr.set(k, [0, 0, 0, V_SECTION]);
  }
  // Synthesis. Items reached only through state variants are applied first so that their slots exist; the straight path
  // (state zero) is applied last and wins.
  for (const wantStraight of [false, true]) {
    for (const [, entries, straight] of st.progs) {
      const straightSet = new Set(straight);
      for (const [item, , , synth] of entries) {
        if (synth !== null && straightSet.has(item.address) === wantStraight) applySynth(arr, synth);
      }
    }
  }
  // validation
  let blocked = 0;
  const blockedElems = new Set();
  for (const [s, eaddr, eidx] of st.slotrefs) {
    const e = arr.get(s);
    let what;
    if (e === undefined || e[3] === V_UNDEFINED) {
      ctx.err(`bad vertex slot ${s}: no block, strip or synthesised vertex`, eaddr);
    } else if (e[3] === V_UNRESOLVED) {
      what = `vertex slot ${s} is unresolved (note 02 section 5b/7)`;
    } else {
      continue;
    }
    if (ctx.strict) ctx.err("BLOCKING: " + what, eaddr, null, true);
    blockedElems.add(eidx);
  }
  for (const [, entries] of st.progs) {
    for (const [item, , slots] of entries) {
      let bad = null;
      for (const s of slots) {
        const e = arr.get(s);
        if (e === undefined || e[3] === V_UNDEFINED) {
          ctx.err(`bad vertex slot ${s}: no block, strip or synthesised vertex`, item.address, item.op);
        } else if (e[3] === V_UNRESOLVED) {
          bad = s;
        }
      }
      if (bad !== null) {
        if (ctx.strict) ctx.err(`BLOCKING: vertex slot ${bad} is unresolved (note 02 section 5b/7)`, item.address, item.op, true);
        item.flags |= F_BLOCKED;
      }
      if (item.flags & F_BLOCKED) blocked += 1;
    }
  }
  for (const eidx of blockedElems) {
    st.elems[eidx].flags |= EF_BLOCKED;
    blocked += 1;
  }
  return [arr, blocked];
}

/** Appends one level's local structures to the model. */
function mergeLevel(model, level, st, arr, root, blocked, sec = []) {
  let n = 0;
  for (const k of arr.keys()) n = Math.max(n, k + 1);
  level.vertex_first = model.vertices.length;
  level.vertex_count = n;
  level.sec_first = model.secpool.length;
  level.sec_count = sec.length;
  model.secpool.push(...sec);
  for (let i = 0; i < n; i++) {
    const e = arr.get(i);
    model.vertices.push(e ? { x: e[0], y: e[1], z: e[2], state: e[3] } : { x: 0, y: 0, z: 0, state: V_UNDEFINED });
  }
  const eb = model.elements.length;
  const pb = model.programs.length;
  const lb = model.proglists.length;
  level.elem_first = eb;
  level.elem_count = st.elems.length;
  for (const e of st.elems) {
    model.elements.push({
      kind: e.kind, flags: e.flags, axis: e.axis, scale: e.scale,
      child0: e.child0 >= 0 ? e.child0 + eb : -1, child1: e.child1 >= 0 ? e.child1 + eb : -1,
      s: [...e.s], list_first: e.list_first + lb, list_count: e.list_count,
    });
  }
  for (const p of st.proglists) model.proglists.push(p + pb);
  for (const [addr, entries] of st.progs) {
    model.programs.push({ item_first: model.items.length, item_count: entries.length, address: addr });
    for (const [item, ops] of entries) {
      item.skip_first = model.skips.length;
      item.skip_count = item.skips.length;
      model.skips.push(...item.skips);
      item.op_first = model.operands.length;
      item.nops = ops.length;
      model.operands.push(...ops);
      model.items.push(item);
    }
  }
  level.root = root >= 0 ? root + eb : -1;
  model.levels.push(level);
  model.blocked += blocked;
}

const SEC_GROUP_LIMIT = 64;

/** Second vertex section of a flat block with flag bit 2 (note 03, "The second vertex section"). Returns [normalised words,
 *  slot count]; [[], 0] when the level has no section. */
function readSecondSection(ctx, addr, b8) {
  if (b8 < 2) return [[], 0];
  const mem = ctx.mem;
  const out = [];
  let slots = 0;
  for (let g = 0; g < SEC_GROUP_LIMIT; g++) {
    let n = mem.s16(addr);
    addr += 2;
    if (n < 0) {
      out.push(-1);
      return [out, slots];
    }
    n = Math.max(n, 1);
    const pts = [];
    for (let i = 0; i < n; i++) {
      pts.push(mem.s16(addr), mem.s16(addr + 2));
      addr += 4;
    }
    const w = mem.s16(addr);
    addr += 2;
    const single = [];
    if (w >= 0) {
      for (let i = 0; i < 2; i++) {
        single.push(mem.s16(addr), mem.s16(addr + 2));
        addr += 4;
      }
    }
    out.push(n, ...pts, single.length / 2, ...single);
    slots += 2 * n + single.length / 2;
    if (b8 + slots > SLOT_LIMIT) ctx.err("second vertex section runs past the slot limit", addr);
    if (w < 0) {
      out.push(-1);
      return [out, slots];
    }
  }
  return ctx.err("second vertex section has no terminator", addr);
}

function readBlock(ctx, blockAddr) {
  const mem = ctx.mem;
  const lv = newLevel();
  lv.part_id = mem.u16(blockAddr);
  lv.tm = mem.u16(blockAddr + 2);
  lv.tf = mem.u16(blockAddr + 4);
  const sc = mem.u8(blockAddr + 6);
  lv.scale_h = sc >> 4;
  lv.scale_l = sc & 15;
  lv.block_flags = mem.u8(blockAddr + 7);
  lv.b8 = mem.u8(blockAddr + 8);
  lv.b9 = mem.u8(blockAddr + 9);
  if (!(lv.block_flags & 2)) {   // a non-flat block: the data triple after the b8 vertices of 6 bytes
    lv.data_triple = [0, 1, 2].map((k) => mem.s16(blockAddr + 10 + 6 * lv.b8 + 2 * k));
  }
  return lv;
}

/** Decodes one detail level [threshold, shape, leaf, stream address, inline] and appends it; returns its kind (0/1) or null. */
function decodeLevel(mem, base, modelId, strict, model, spec) {
  const [thr, shape, leaf, streamAddr, inline] = spec;
  if (leaf === 0xffff) {
    model.levels.push(newLevel({ threshold: thr, shape, present: 0, leaf, vertex_first: model.vertices.length, elem_first: model.elements.length }));
    return null;
  }
  const blockAddr = base + (leaf & 0xfff);
  const ctx = new Ctx(mem, modelId, 0, blockAddr, strict);
  ctx.model_base = base;
  const lv = readBlock(ctx, blockAddr);
  lv.threshold = thr;
  lv.shape = shape;
  lv.leaf = leaf;
  lv.present = 1;
  const flat = Boolean(lv.block_flags & 2);
  ctx.kind = flat ? 1 : 0;
  ctx.scale_l = lv.scale_l;
  const st = new Stream(ctx, base);
  const root = st.element(streamAddr, inline);
  const data0 = blockAddr + 10;
  const nblock = lv.b8;      // only b8 and the extra ranges exist in the game's vertex array

  const blockVertex = (i) => {
    if (flat) return [mem.s16(data0 + 4 * i), 0, mem.s16(data0 + 4 * i + 2)];
    return [0, 1, 2].map((k) => mem.s16(data0 + 6 * i + 2 * k));
  };

  let strip = null;
  if ((lv.block_flags & 8) && !flat) strip = stripSlots(ctx, data0 + 6 * lv.b8, lv.b8);
  let sec = [], secSlots = 0;
  if ((lv.block_flags & 4) && flat) [sec, secSlots] = readSecondSection(ctx, data0 + 4 * lv.b8, lv.b8);
  const [arr, blocked] = finishLevel(ctx, st, blockVertex, nblock, flat, strip, secSlots);
  mergeLevel(model, lv, st, arr, root, blocked, sec);
  return ctx.kind;
}

function raise(modelId, msg, addr = null) {
  const w = addr === null ? "" : ` at 0x${hex(addr)}`;
  throw new DecodeError(`model ${modelId}${w}: ${msg}`);
}

/** The cockpit-alternate detail list (note 02 section 2.5) as extra levels after the normal ones. */
function decodeAlternate(mem, rec, modelId, strict, model) {
  const base = rec.base, alt = rec.alt_list;
  if (alt === base) return;
  if (alt === base - 2) {                      // ids 0 and 1: one word 0xFFFF, then the normal list
    if (mem.u16(alt) !== 0xffff) raise(modelId, "alternate list at base - 2 does not start with 0xFFFF", alt);
    model.alt_near = model.alt_cockpit = model.levels.length;
    model.levels.push(newLevel({ threshold: 0, shape: 2, present: 0, leaf: 0xffff, vertex_first: model.vertices.length, elem_first: model.elements.length }));
    return;
  }
  if (alt !== base - 8) raise(modelId, `alternate list at 0x${hex(alt)} is neither the base, base - 2 nor base - 8`, alt);
  const [w1, w2, w3, w4] = [0, 1, 2, 3].map((k) => mem.u16(alt + 2 * k));
  if (!(w1 & 0x8000) || !((w2 & 0x8000) && (w2 & 0x2000) && !(w2 & 0x1000)) || !(w3 & 0x8000) || w4 !== 0xffff) {
    raise(modelId, `alternate list words ${hex(w1, 4)} ${hex(w2, 4)} ${hex(w3, 4)} ${hex(w4, 4)} do not have the documented shape`, alt);
  }
  model.alt_near = model.levels.length;
  decodeLevel(mem, base, modelId, strict, model, [0, 2, w1, alt + 2, false]);
  model.alt_cockpit = model.levels.length;
  decodeLevel(mem, base, modelId, strict, model, [0, 2, w3, alt + 6, false]);
  model.levels[model.alt_cockpit].present = 0;
}

// The phase lists of the cone models (note 07 11.5).
const PHASE_LISTS = new Map([
  [2, [0x41658, 0x41666, 0x41678, 0x41674]],
  [13, [0x41658, 0x41666, 0x41678, 0x41674]],
  [11, [0x418fe, 0x4190c, 0x418da, 0x4191a]],
]);
const PHASE_BASE = new Map([[2, 0x41638], [13, 0x41638], [11, 0x418de]]);

function decodePhaseLists(mem, base, modelId, strict, model) {
  model.phase_first = model.levels.length + 1;
  for (const addr of PHASE_LISTS.get(modelId)) {
    const specs = walkDetailList(mem, addr, (m, a = null) => raise(modelId, m, a));
    if (specs.length !== 1 || !(specs[0][2] & 0x8000)) raise(modelId, "phase list is not a single leaf-only node", addr);
    decodeLevel(mem, base, modelId, strict, model, specs[0]);   // the leaf offset is relative to the model's base, as for the normal list
  }
}

function decodeNormal(mem, rec, modelId, strict) {
  const base = rec.base;
  const ctxErr = (m, a = null) => raise(modelId, m, a);
  const specs = walkDetailList(mem, base, ctxErr);
  const model = newModel(modelId, rec.handler, 0);
  const kinds = new Set();
  for (const spec of specs) {
    const k = decodeLevel(mem, base, modelId, strict, model, spec);
    if (k !== null) kinds.add(k);
  }
  model.kind = kinds.size === 1 && kinds.has(1) ? 1 : 0;
  model.nnormal = model.levels.length;
  // the block flag byte the gun's hit test reads: the leaf named by the first word of the model's list when that word has bit 15 set, else by the word at +4
  const w0 = mem.u16(base);
  const d7 = w0 & 0x8000 ? w0 : mem.u16(base + 4);
  model.hull_flags = mem.u8(base + (d7 & 0x3fff) + 7);
  decodeAlternate(mem, rec, modelId, strict, model);
  if (PHASE_BASE.get(modelId) === base) decodePhaseLists(mem, base, modelId, strict, model);
  return model;
}

function decodeTypeB(mem, rec, modelId, strict) {
  const model = newModel(modelId, rec.handler, 2);
  let addr = rec.base;
  const seen = new Set();
  const entries = [];
  while (!seen.has(addr)) {
    seen.add(addr);
    const d = mem.u16(addr);
    if (d & 0x8000) break;
    const nxt = mem.u32(addr + 2);
    const vb = mem.u32(addr + 6);
    entries.push([d, addr, vb]);
    if (nxt === 0xffffffff) break;
    addr = nxt;
  }
  for (const [d, eaddr, vb] of entries) {
    const ctx = new Ctx(mem, modelId, 2, vb, strict);
    ctx.model_base = rec.base;
    ctx.scale_l = 0;
    const count = mem.u16(vb);
    if (count > SLOT_LIMIT) ctx.err(`vertex block declares ${count} vertices`, vb);
    const lv = newLevel({ threshold: d, shape: 3, present: 1, leaf: 0 });
    const blockVertex = (i) => [mem.s16(vb + 6 + 4 * i), 0, mem.s16(vb + 8 + 4 * i)];
    const st = new Stream(ctx, 0);
    const prog = st.program(eaddr + 10);
    st.elems.push(newElement(E_DRAW, { list_first: 0, list_count: 1 }));
    st.proglists.push(prog);
    const [arr, blocked] = finishLevel(ctx, st, blockVertex, count, true, null);
    mergeLevel(model, lv, st, arr, 0, blocked);
  }
  model.nnormal = model.levels.length;
  return model;
}

/**
 * Decodes the model of the record at byte `recordOffset` of `data`. `strict` raises DecodeError on documented BLOCKING items;
 * with strict false the affected items are flagged F_BLOCKED and decoding goes on. Unknown operations, bad vertex slots and
 * truncated blocks always raise.
 */
export function decodeModel(data, recordOffset, modelId, imageBase = IMAGE_BASE, strict = true) {
  const mem = new Mem(data, imageBase, `model ${modelId}`);
  const rec = decodeRecord(data, recordOffset, modelId);
  if (rec.base === 0) throw new DecodeError(`model ${modelId}: empty record (run-time slot, zero in the file)`);
  if (TYPE_B_HANDLERS.includes(rec.handler)) return decodeTypeB(mem, rec, modelId, strict);
  return decodeNormal(mem, rec, modelId, strict);
}

// ---------------------------------------------------------------------------
// IMDL file format, version 5 (little endian, counts first)

export function writeModel(m) {
  const w = new ByteWriter(4096);
  w.ascii(IMDL_MAGIC).u16(IMDL_VERSION).u16(m.id).u16(m.kind).u16(m.blocked).u32(m.handler)
    .u32(m.levels.length).u32(m.vertices.length).u32(m.elements.length).u32(m.proglists.length).u32(m.programs.length)
    .u32(m.items.length).u32(m.operands.length).u32(m.skips.length).u32(m.secpool.length)
    .u16(m.nnormal < 0 ? m.levels.length : m.nnormal).u16(m.alt_near).u16(m.alt_cockpit).u16(m.phase_first);
  for (const l of m.levels) {
    w.u16(l.threshold).u16(l.leaf).u8(l.shape).u8(l.present).u16(l.part_id).u16(l.tm).u16(l.tf)
      .u8(l.scale_h).u8(l.scale_l).u8(l.block_flags).u8(l.b8).u8(l.b9).u8(m.hull_flags).u16(0).i32(l.root)
      .u32(l.vertex_first).u32(l.vertex_count).u32(l.elem_first).u32(l.elem_count).u32(l.sec_first).u32(l.sec_count)
      .i16(l.data_triple[0]).i16(l.data_triple[1]).i16(l.data_triple[2]);
  }
  for (const v of m.vertices) w.i32(v.x).i32(v.y).i32(v.z).u32(v.state);
  for (const e of m.elements) {
    w.u8(e.kind).u8(e.flags).u8(e.axis).u8(e.scale).i32(e.child0).i32(e.child1);
    for (const s of e.s) w.i16(s);
    w.u32(e.list_first).u32(e.list_count);
  }
  for (const p of m.proglists) w.u32(p);
  for (const p of m.programs) w.u32(p.item_first).u32(p.item_count).u32(p.address);
  for (const i of m.items) {
    w.u16(i.op).u16(i.flags).u16(i.g1).u16(i.g2).i16(i.extra_n).u16(i.extra_slot).u16(i.pen).u16(i.w).u16(i.wback).u16(i.prim)
      .u16(i.nops).u16(i.length).u32(i.op_first).u32(i.address).u32(i.skip_first).u32(i.skip_count);
  }
  for (const o of m.operands) w.i32(o);
  for (const o of m.skips) w.i32(o);
  for (const o of m.secpool) w.i32(o);
  return w.finish();
}

/**
 * Every model that decodes goes to models/model_NNN.bin; models/index.txt lists all ids with a status: `ok`, `partial: ...`
 * (decoded with the BLOCKING items flagged, the strict decode having failed with that reason), `empty: ...` or `failed: ...`.
 * One failing model never aborts the run. Adds the files to `files` and returns the counts.
 */
export function writeModels(image, files) {
  const lines = ["# id\tfile\tstatus\n"];
  const counts = { ok: 0, partial: 0, empty: 0, failed: 0 };
  for (let i = 0; i < RECORD_COUNT; i++) {
    const off = RECORD_TABLE - IMAGE_BASE + RECORD_SIZE * i;
    let fname = "-", status = "", model = null;
    try {
      model = decodeModel(image, off, i);
      status = "ok";
    } catch (e) {
      if (e instanceof BlockedError) {
        try {
          model = decodeModel(image, off, i, IMAGE_BASE, false);
          status = `partial: ${e.message} (${model.blocked} items flagged blocked)`;
        } catch (e2) {
          if (!(e2 instanceof DecodeError)) throw e2;
          status = `failed: ${e2.message}`;
        }
      } else if (e instanceof DecodeError) {
        status = e.message.includes("empty record") ? `empty: ${e.message}` : `failed: ${e.message}`;
      } else {
        throw e;
      }
    }
    const kind = status.split(":")[0].split(" ")[0];
    counts[kind] += 1;
    if (model !== null) {
      fname = `model_${String(i).padStart(3, "0")}.bin`;
      files.set(`models/${fname}`, writeModel(model));
    }
    lines.push(`${i}\t${fname}\t${status.replaceAll("\n", " ")}\n`);
  }
  files.set("models/index.txt", new TextEncoder().encode(lines.join("")));
  return counts;
}
