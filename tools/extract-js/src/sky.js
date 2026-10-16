// Tables of the sky fill and the sea mesh pass (tools/extract/extract/sky.py; docs/re/04-scene-composition.md).
// Every table is written as the raw big-endian bytes of the relocated flat image.
import { IMAGE_BASE, DecodeError, Mem } from "./tables.js";
import { hex } from "./util.js";

// name: [first address, last address]
export const SPANS = {
  "sea_zone.bin": [0x23188, 0x233b7],
  "sea_zone2.bin": [0x233b8, 0x23407],
  "mesh_scan.bin": [0x2e600, 0x2f19b],
  "mesh_far.bin": [0x2f4f8, 0x2f51b],
  "mesh_cells.bin": [0x47d7c, 0x495cb],
};
export const SIZES = { "sea_zone.bin": 560, "sea_zone2.bin": 80, "mesh_scan.bin": 2972, "mesh_far.bin": 36, "mesh_cells.bin": 6224 };

/** Map<file name, bytes> */
export function decodeAll(data, base = IMAGE_BASE) {
  const mem = new Mem(data, base, "sky tables");
  const out = new Map();
  for (const [name, [lo, hi]] of Object.entries(SPANS)) {
    const n = hi - lo + 1;
    if (n !== SIZES[name]) throw new DecodeError(`${name}: span 0x${hex(lo)}..0x${hex(hi)} is ${n} bytes, expected ${SIZES[name]}`);
    out.set(name, mem.raw(lo, n));
  }
  return out;
}
