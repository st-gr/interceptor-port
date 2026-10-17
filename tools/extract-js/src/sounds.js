// The disk's text/ directory holds signed 8-bit mono samples (14 files), not text; the other two files, textply and
// textctl, are the demo replay stream (flow.js decodeDemo) and are not written here (tools/extract/extract/sounds.py).
//
// The WAV rate of a file is 3,546,895 / period (the PAL colour clock), the period being the reference period of note 10
// section 2.4: the period of the file's first play in a flight (a listening copy: the port plays the object table of tables/sounds.bin, not the WAV).
import { ByteWriter } from "./util.js";

export const PAL_CLOCK_HZ = 3546895;
export const DEFAULT_PERIOD = 358;   // note 10 2.1: the period of a new sound object (the music files, and any file the note does not name)

// note 10 2.4: file name -> reference period (the period of the file's first play in a flight)
const REFERENCE_PERIODS = new Map([
  ["text201", 179],   // voice 0; voice 1 plays it at 181
  ["textcpt", 764],
  ["textegn", 776],
  ["textegn2", 776],
  ["textwnd", 392],
  ["texttre", 310],
  ["textger", 700],
]);

export function referencePeriod(name) {
  return REFERENCE_PERIODS.get(name) ?? DEFAULT_PERIOD;
}

export function sampleRateHz(name) {
  const p = referencePeriod(name);
  return Math.floor((PAL_CLOCK_HZ + (p >> 1)) / p);
}

/** The WAV file of `raw` (signed 8-bit samples): unsigned 8-bit PCM, mono, as Python's `wave` module writes it
 *  (a data chunk of odd length is followed by one pad byte, which the RIFF size counts). */
export function wavBytes(raw, rateHz) {
  const n = raw.length;
  const pad = n & 1;
  const w = new ByteWriter(44 + n + pad);
  w.ascii("RIFF").u32(36 + n + pad).ascii("WAVE").ascii("fmt ").u32(16).u16(1).u16(1).u32(rateHz).u32(rateHz).u16(1).u16(8);
  w.ascii("data").u32(n);
  const unsigned = new Uint8Array(n);
  for (let i = 0; i < n; i++) unsigned[i] = raw[i] ^ 0x80;
  w.bytes(unsigned);
  if (pad) w.u8(0);
  return w.finish();
}

/** Adds sounds/<name>.raw (byte-exact) and sounds/<name>.wav to `files`. */
export function writeSample(raw, name, files, rateHz = sampleRateHz(name)) {
  files.set(`sounds/${name}.raw`, raw);
  files.set(`sounds/${name}.wav`, wavBytes(raw, rateHz));
}
