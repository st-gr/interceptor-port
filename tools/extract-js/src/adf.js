// Read-only access to an AmigaDOS floppy image (tools/extract/extract/adf.py, which uses amitools).
//
// A DD floppy image is 1760 blocks of 512 bytes; block 880 is the root block. The file system is
// the original (OFS) or the fast one (FFS); the game's disk is OFS. Directory order is the order
// amitools reads: the hash table by index, then the hash chains in the order they were met.
import { ExtractError } from "./errors.js";

export const ADF_SIZE = 901120;          // 80 cylinders x 2 heads x 11 sectors x 512 bytes
export const BLOCK_BYTES = 512;
const BLOCK_LONGS = 128;
const ROOT_BLOCK = 880;
const T_SHORT = 2, T_DATA = 8, T_LIST = 16;
const ST_ROOT = 1, ST_USERDIR = 2, ST_FILE = 0xfffffffd;

const FLUX_MAGICS = [
  ["SCP", "SCP flux image"],
  ["HXCPICFE", "HFE (HxC) flux image"],
  ["HXCHFEV3", "HFE (HxC) flux image"],
  ["MAMEFLOPPYIMAGE", "MFI (MAME) flux image"],
];

const EXPECTED = `an ADF disk image: the sector dump of the Amiga disk, a file of ${ADF_SIZE.toLocaleString("en-US")} bytes (Greaseweazle: gw convert --format amiga.amigados disk.scp disk.adf)`;

function startsWith(bytes, text) {
  if (bytes.length < text.length) return false;
  for (let i = 0; i < text.length; i++) if (bytes[i] !== text.charCodeAt(i)) return false;
  return true;
}

/** Name of the flux format a file starts like, or null. */
export function fluxFormatOf(bytes) {
  for (const [magic, name] of FLUX_MAGICS) if (startsWith(bytes, magic)) return name;
  return null;
}

/** AmigaDOS compares names without regard to case (the international rule maps the Latin-1 letters too). */
function upperName(s) {
  let out = "";
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if ((c >= 0x61 && c <= 0x7a) || (c >= 0xe0 && c <= 0xfe && c !== 0xf7)) out += String.fromCharCode(c - 32);
    else out += s[i];
  }
  return out;
}

/** A problem with the disk image itself; an ExtractError like the others. */
export class AdfError extends ExtractError {}

export class Adf {
  /** @param {Uint8Array} bytes the whole image */
  constructor(bytes) {
    const flux = fluxFormatOf(bytes);
    if (flux) {
      throw new AdfError(`This file is a ${flux}, not an ADF. Expected ${EXPECTED}.`);
    }
    if (bytes.length < ADF_SIZE) {
      throw new AdfError(`The file is too short (${bytes.length} bytes): expected ${EXPECTED}.`);
    }
    if (bytes.length > ADF_SIZE) {
      throw new AdfError(`The file is ${bytes.length} bytes, not the size of a DD disk: expected ${EXPECTED}.`);
    }
    this.bytes = bytes;
    this.dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    if (!(bytes[0] === 0x44 && bytes[1] === 0x4f && bytes[2] === 0x53)) {
      throw new AdfError(`This file is not an AmigaDOS disk (no "DOS" boot block): expected ${EXPECTED}.`);
    }
    this.flags = bytes[3];
    this.isFfs = (this.flags & 1) !== 0;
    const root = this.block(ROOT_BLOCK);
    if (!this.checksumOk(root) || root.long(0) !== T_SHORT || root.long(-1) !== ST_ROOT) {
      throw new AdfError(`This file does not contain an AmigaDOS root block at block ${ROOT_BLOCK}: expected ${EXPECTED}.`);
    }
    this.root = root;
    this._dirs = new Map();
  }

  block(n) {
    if (n < 0 || n >= ADF_SIZE / BLOCK_BYTES) throw new AdfError(`The disk image points at block ${n}, outside the disk.`);
    const o = n * BLOCK_BYTES;
    const dv = new DataView(this.bytes.buffer, this.bytes.byteOffset + o, BLOCK_BYTES);
    return {
      n,
      o,
      dv,
      long(i) { return dv.getUint32((i < 0 ? BLOCK_LONGS + i : i) * 4, false); },
    };
  }

  checksumOk(b) {
    let sum = 0;
    for (let i = 0; i < BLOCK_LONGS; i++) sum = (sum + b.long(i)) >>> 0;
    return sum === 0;
  }

  _name(b) {
    const o = (BLOCK_LONGS - 20) * 4;
    const len = Math.min(b.dv.getUint8(o), 30);
    let s = "";
    for (let i = 0; i < len; i++) s += String.fromCharCode(b.dv.getUint8(o + 1 + i));
    return s;
  }

  /** Entries of the directory whose block is `dirBlock`: [{name, isFile, block}] in amitools' order. */
  _entries(dirBlock) {
    const cached = this._dirs.get(dirBlock.n);
    if (cached) return cached;
    const queue = [];
    const hashSize = BLOCK_LONGS - 56;
    for (let i = 0; i < hashSize; i++) {
      const bn = dirBlock.long(6 + i);
      if (bn !== 0) queue.push(bn);
    }
    const out = [];
    for (let k = 0; k < queue.length; k++) {
      const b = this.block(queue[k]);
      if (!this.checksumOk(b) || b.long(0) !== T_SHORT) throw new AdfError(`The disk's directory is damaged at block ${b.n}.`);
      const sub = b.long(-1);
      if (sub !== ST_USERDIR && sub !== ST_FILE) throw new AdfError(`The disk's directory has an unsupported entry at block ${b.n}.`);
      out.push({ name: this._name(b), isFile: sub === ST_FILE, block: b });
      const chain = b.long(-4);
      if (chain !== 0) queue.push(chain);
      if (queue.length > ADF_SIZE / BLOCK_BYTES) throw new AdfError("The disk's directory chains loop.");
    }
    this._dirs.set(dirBlock.n, out);
    return out;
  }

  _find(path, wantFile) {
    const parts = path.split("/").filter((p) => p.length);
    let dir = this.root;
    for (let i = 0; i < parts.length; i++) {
      const last = i === parts.length - 1;
      const want = upperName(parts[i]);
      const e = this._entries(dir).find((x) => upperName(x.name) === want);
      if (!e) return null;
      if (last) return e.isFile === wantFile ? e : null;
      if (e.isFile) return null;
      dir = e.block;
    }
    return wantFile ? null : { name: "", isFile: false, block: dir };
  }

  /** The bytes of the file at `path`; throws AdfError("file ... not found on the disk"). */
  read(path) {
    const e = this._find(path, true);
    if (!e) throw new AdfError(`file ${path} not found on the disk`);
    const hdr = e.block;
    const size = hdr.long(-47);
    const perBlock = this.isFfs ? BLOCK_BYTES : BLOCK_BYTES - 24;
    const need = Math.ceil(size / perBlock);
    const list = [];
    let cur = hdr;
    for (let guard = 0; list.length < need; guard++) {
      const count = Math.min(cur.long(2), BLOCK_LONGS - 56);
      for (let i = 0; i < count && list.length < need; i++) list.push(cur.long(-51 - i));
      if (list.length >= need) break;
      const ext = cur.long(-2);
      if (ext === 0 || guard > 64) throw new AdfError(`file ${path}: the disk lacks the data block list`);
      cur = this.block(ext);
      if (!this.checksumOk(cur) || cur.long(0) !== T_LIST) throw new AdfError(`file ${path}: damaged block list at block ${cur.n}`);
    }
    const out = new Uint8Array(size);
    let pos = 0;
    for (let k = 0; k < list.length; k++) {
      const b = this.block(list[k]);
      if (this.isFfs) {
        const n = Math.min(BLOCK_BYTES, size - pos);
        out.set(this.bytes.subarray(b.o, b.o + n), pos);
        pos += n;
      } else {
        if (!this.checksumOk(b) || b.long(0) !== T_DATA || b.long(2) !== k + 1) {
          throw new AdfError(`file ${path}: damaged data block ${b.n}`);
        }
        const n = b.long(3);
        if (n > BLOCK_BYTES - 24 || pos + n > size) throw new AdfError(`file ${path}: damaged data block ${b.n}`);
        out.set(this.bytes.subarray(b.o + 24, b.o + 24 + n), pos);
        pos += n;
      }
    }
    if (pos !== size) throw new AdfError(`file ${path}: file size mismatch: got=${pos} want=${size}`);
    return out;
  }

  /** File names in on-disk directory order (hash table order, not alphabetical); directories are not listed. */
  listdir(path) {
    const e = this._find(path, false);
    if (!e) throw new AdfError(`directory ${path} not found on the disk`);
    return this._entries(e.block).filter((x) => x.isFile).map((x) => x.name);
  }
}
