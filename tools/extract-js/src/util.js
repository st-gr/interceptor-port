// Small helpers shared by the decoders. Pure functions over byte arrays: no DOM, no fs.

/** Python's f"{n:X}" / f"{n:04X}": upper-case hex, optionally zero padded to `width`. */
export function hex(n, width = 0) {
  const s = Math.abs(n).toString(16).toUpperCase().padStart(width, "0");
  return n < 0 ? "-" + s : s;
}

/** Python's f"{n:#x}" (lower-case, 0x prefix). */
export function hexLower0x(n) {
  return (n < 0 ? "-0x" : "0x") + Math.abs(n).toString(16);
}

export function concat(parts) {
  let n = 0;
  for (const p of parts) n += p.length;
  const out = new Uint8Array(n);
  let o = 0;
  for (const p of parts) {
    out.set(p, o);
    o += p.length;
  }
  return out;
}

/** Latin-1 / ASCII text to bytes (the extractor's text is ASCII). */
export function ascii(s) {
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i) & 0xff;
  return out;
}

export function utf8(s) {
  return new TextEncoder().encode(s);
}

export function bytesEqual(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

/** Growable byte writer with the struct formats the Python writers use ("<" little endian). */
export class ByteWriter {
  constructor(size = 256) {
    this.buf = new Uint8Array(size);
    this.dv = new DataView(this.buf.buffer);
    this.len = 0;
  }

  _need(n) {
    if (this.len + n <= this.buf.length) return;
    let size = this.buf.length * 2;
    while (size < this.len + n) size *= 2;
    const nb = new Uint8Array(size);
    nb.set(this.buf.subarray(0, this.len));
    this.buf = nb;
    this.dv = new DataView(nb.buffer);
  }

  u8(v) { this._need(1); this.dv.setUint8(this.len, v & 0xff); this.len += 1; return this; }
  i8(v) { this._need(1); this.dv.setInt8(this.len, ((v + 128) & 0xff) - 128); this.len += 1; return this; }
  u16(v) { this._need(2); this.dv.setUint16(this.len, v & 0xffff, true); this.len += 2; return this; }
  i16(v) { this._need(2); this.dv.setInt16(this.len, ((v + 0x8000) & 0xffff) - 0x8000, true); this.len += 2; return this; }
  u32(v) { this._need(4); this.dv.setUint32(this.len, v >>> 0, true); this.len += 4; return this; }
  i32(v) { this._need(4); this.dv.setInt32(this.len, v | 0, true); this.len += 4; return this; }
  bytes(b) { this._need(b.length); this.buf.set(b, this.len); this.len += b.length; return this; }
  ascii(s) { return this.bytes(ascii(s)); }
  /** several values with one writer method: w.many("i16", [1, 2, 3]) */
  many(kind, list) { for (const v of list) this[kind](v); return this; }
  finish() { return this.buf.slice(0, this.len); }
}
