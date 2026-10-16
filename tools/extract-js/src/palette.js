// Palette and HUD font decoders (tools/extract/extract/palette.py; docs/re/02-assets-and-tables.md sections 11 and 12).
import { DecodeError } from "./tables.js";
import { hex } from "./util.js";

export const FADE_TABLE = 0x29fa4;          // 16 steps x 16 colours, 12-bit words 0x0RGB
export const FADE_STEPS = 16;
export const FADE_COLOURS = 16;
export const FONT_ADDR = 0x426d0;           // hunk 61
export const FONT_HUNK_SIZE = 872;
export const FONT_CHARS = 64;               // character codes 0x20 .. 0x5F
export const GLYPH_ROWS = 5;

/** Fade table at byte `offset`: `steps` x `colours` words, each 0x0RGB with 4-bit channels.
 *  Returns steps lists of [r, g, b], channels scaled to 8 bits by 17. */
export function decodeFadeTable(data, offset, steps = FADE_STEPS, colours = FADE_COLOURS) {
  const need = steps * colours * 2;
  if (offset < 0 || offset + need > data.length) {
    throw new DecodeError(`fade table at offset 0x${hex(offset)}: needs ${need} bytes, image has ${data.length}`);
  }
  const dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const out = [];
  for (let s = 0; s < steps; s++) {
    const step = [];
    for (let c = 0; c < colours; c++) {
      const at = offset + 2 * (s * colours + c);
      const w = dv.getUint16(at, false);
      if (w > 0x0fff) {
        throw new DecodeError(`fade table: step ${s} colour ${c} at offset 0x${hex(at)} is 0x${hex(w, 4)}, not a 12-bit colour`);
      }
      step.push([((w >> 8) & 15) * 17, ((w >> 4) & 15) * 17, (w & 15) * 17]);
    }
    out.push(step);
  }
  return out;
}

export function paletteBytes(colours) {
  const out = new Uint8Array(colours.length * 3);
  colours.forEach((rgb, i) => out.set(rgb, 3 * i));
  return out;
}

export function fadeBytes(steps) {
  const out = new Uint8Array(steps.length * steps[0].length * 3);
  let o = 0;
  for (const s of steps) {
    const b = paletteBytes(s);
    out.set(b, o);
    o += b.length;
  }
  return out;
}

/** HUD font. The hunk starts with 64 words, indexed by character code minus 0x20, each the byte offset
 *  from the hunk start of a 5-byte glyph. Returns 64 glyphs of 5 bytes (shared glyphs repeated). */
export function decodeFont(data, offset, hunkSize = FONT_HUNK_SIZE) {
  if (offset < 0 || offset + hunkSize > data.length) {
    throw new DecodeError(`HUD font at offset 0x${hex(offset)}: hunk of ${hunkSize} bytes is truncated`);
  }
  const dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const glyphs = [];
  for (let i = 0; i < FONT_CHARS; i++) {
    const off = dv.getUint16(offset + 2 * i, false);
    if (off + GLYPH_ROWS > hunkSize) {
      throw new DecodeError(`HUD font: glyph ${i} (code 0x${hex(0x20 + i, 2)}) at offset 0x${hex(offset + 2 * i)} points to 0x${hex(off)}, outside the hunk`);
    }
    const g = data.slice(offset + off, offset + off + GLYPH_ROWS);
    if (g.some((b) => b & 0x1f)) {
      throw new DecodeError(`HUD font: glyph ${i} at hunk offset 0x${hex(off)} has bits outside the three pixel columns`);
    }
    glyphs.push(g);
  }
  return glyphs;
}
