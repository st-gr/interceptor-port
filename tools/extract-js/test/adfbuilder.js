// A small AmigaDOS floppy writer for the tests: invented disks with chosen files (OFS or FFS), so that the error cases and the
// file reader (multi-block files, extension blocks, hash chains) can be tested without the game.
import { ADF_SIZE } from "../src/adf.js";

const BLOCK = 512;
const LONGS = 128;
const ROOT = 880;

function hashName(name) {
  let h = name.length;
  for (let i = 0; i < name.length; i++) h = (h * 13 + name.charCodeAt(i) - (name.charCodeAt(i) >= 97 && name.charCodeAt(i) <= 122 ? 32 : 0)) & 0x7ff;
  return h % 72;
}

class Block {
  constructor(bytes, n) {
    this.n = n;
    this.dv = new DataView(bytes.buffer, n * BLOCK, BLOCK);
  }

  set(i, v) { this.dv.setUint32((i < 0 ? LONGS + i : i) * 4, v >>> 0, false); }
  get(i) { return this.dv.getUint32((i < 0 ? LONGS + i : i) * 4, false); }

  name(s) {
    this.dv.setUint8((LONGS - 20) * 4, s.length);
    for (let i = 0; i < s.length; i++) this.dv.setUint8((LONGS - 20) * 4 + 1 + i, s.charCodeAt(i));
  }

  seal(checkLong = 5) {
    this.set(checkLong, 0);
    let sum = 0;
    for (let i = 0; i < LONGS; i++) sum = (sum + this.get(i)) >>> 0;
    this.set(checkLong, (0 - sum) >>> 0);
  }
}

/**
 * @param {Record<string, Uint8Array>} files path ("dir/name" or "name") -> bytes; one directory level
 * @param {{ffs?: boolean}} [opts]
 */
export function buildAdf(files, { ffs = false } = {}) {
  const bytes = new Uint8Array(ADF_SIZE);
  bytes.set([0x44, 0x4f, 0x53, ffs ? 1 : 0]);
  let next = 881;   // the bitmap block comes first
  const alloc = () => next++;
  const bitmap = new Block(bytes, alloc());   // every block free: the readers only need a valid bitmap block
  for (let i = 1; i < LONGS; i++) bitmap.set(i, 0xffffffff);
  bitmap.seal(0);
  const root = new Block(bytes, ROOT);
  root.set(-49, bitmap.n);
  root.set(0, 2);
  root.set(3, 72);
  root.set(-50, 0xffffffff);
  root.name("TEST");
  root.set(-1, 1);

  const insert = (parent, name, blk) => {
    const h = hashName(name);
    blk.set(-4, parent.get(6 + h));   // chain to the entry that was there
    parent.set(6 + h, blk.n);
    blk.set(-3, parent.n);
  };

  const dirs = new Map();
  const entryBlocks = [];
  const dirBlock = (dname) => {
    if (!dirs.has(dname)) {
      const d = new Block(bytes, alloc());
      d.set(0, 2); d.set(1, d.n); d.name(dname); d.set(-1, 2);
      insert(root, dname, d);
      dirs.set(dname, d);
      entryBlocks.push(d);
    }
    return dirs.get(dname);
  };

  for (const [path, data] of Object.entries(files)) {
    const parts = path.split("/");
    const parent = parts.length === 2 ? dirBlock(parts[0]) : root;
    const name = parts[parts.length - 1];
    const hdr = new Block(bytes, alloc());
    hdr.set(0, 2); hdr.set(1, hdr.n); hdr.name(name); hdr.set(-1, 0xfffffffd);
    hdr.set(-47, data.length);
    const perBlock = ffs ? BLOCK : BLOCK - 24;
    const count = Math.ceil(data.length / perBlock);
    const dataBlocks = [];
    for (let k = 0; k < count; k++) dataBlocks.push(alloc());
    dataBlocks.forEach((bn, k) => {
      const chunk = data.subarray(k * perBlock, (k + 1) * perBlock);
      if (ffs) {
        bytes.set(chunk, bn * BLOCK);
      } else {
        const b = new Block(bytes, bn);
        b.set(0, 8); b.set(1, hdr.n); b.set(2, k + 1); b.set(3, chunk.length); b.set(4, k + 1 < count ? dataBlocks[k + 1] : 0);
        bytes.set(chunk, bn * BLOCK + 24);
        b.seal();
      }
    });
    // pointers: 72 in the header, the rest in extension blocks of 72
    const first = dataBlocks.slice(0, 72);
    hdr.set(2, first.length);
    hdr.set(4, dataBlocks[0] || 0);
    first.forEach((bn, i) => hdr.set(-51 - i, bn));
    let prev = hdr;
    for (let off = 72; off < dataBlocks.length; off += 72) {
      const ext = new Block(bytes, alloc());
      const part = dataBlocks.slice(off, off + 72);
      ext.set(0, 16); ext.set(1, ext.n); ext.set(2, part.length); ext.set(-3, hdr.n); ext.set(-1, 0xfffffffd);
      part.forEach((bn, i) => ext.set(-51 - i, bn));
      entryBlocks.push(ext);   // sealed at the end: its extension field is set by the next one
      prev.set(-2, ext.n);
      prev = ext;
    }
    insert(parent, name, hdr);
    entryBlocks.push(hdr);
  }
  // the chain and parent fields are set by insert(): seal the entries last
  for (const b of entryBlocks) b.seal();
  root.seal();
  return bytes;
}
