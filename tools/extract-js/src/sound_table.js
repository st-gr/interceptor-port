// tables/sounds.bin (extractor version 25): the 37 sound objects of note 10 section 2.2. The same bytes as
// tools/extract/extract/sound_table.py, whose header describes the 20-byte big-endian record.
import { DEFAULT_PERIOD } from "./sounds.js";

export const OBJECTS = 0x25;
export const RECORD_BYTES = 20;
export const NONE = 0xff;
const KIND_SAMPLE = 0, KIND_TONE = 1, KIND_NOISE = 2;
const SCRIPT_NONE = 0, SCRIPT_TONE = 1, SCRIPT_NOISE = 2, SCRIPT_GUN = 3, SCRIPT_LAUNCH = 4;
const DEFAULT_VOLUME = 0x3f;

const TONE_BYTES = 32;
const NOISE_BYTES = 0x800;
const GUN_PERIOD = 150;
const OBJECT1_BYTES = 5580;
const OBJECT3_OFFSET = 0x370;
const OBJECT9_OFFSET = 0x792;
// (even object, file, count, reload, next even object); the odd object is the same on voice 1 and links to next + 1
const MUSIC = [
  [0x0d, "texti0a", 13, 0, 0x0f], [0x0f, "texti0b", 1, 0, 0x11], [0x11, "texti1", 2, 2, 0x13], [0x13, "texti2", 0, 0, 0x15],
  [0x15, "texti1", 2, 2, 0x17], [0x17, "texti2", 0, 0, 0x19], [0x19, "texti3", 2, 2, 0x1b], [0x1b, "texti4", 0, 0, 0x1d],
  [0x1d, "texti3", 2, 2, 0x1f], [0x1f, "texti4", 0, 0, 0x21], [0x21, "texti5", 3, 3, 0x11],
];

/**
 * @param {string[]} soundNames the order of sounds/index.txt
 * @param {Map<string, number>} sizes file name -> length in bytes (the files present on the disk)
 * @returns {Uint8Array} 37 records of 20 bytes
 */
export function buildSoundTable(soundNames, sizes) {
  const index = new Map(soundNames.map((n, i) => [n, i]));
  const blank = () => ({ file: NONE, kind: NONE, voice: 0, volume: 0, offset: 0, length: 0, period: 0, count: 0, reload: 0, next: NONE, script: SCRIPT_NONE });
  const recs = Array.from({ length: OBJECTS }, blank);

  const sample = (obj, name, voice, o = {}) => {
    if (!sizes.has(name) || !index.has(name)) return;
    const size = sizes.get(name);
    const offset = Math.min(o.offset ?? 0, size);
    const length = o.length === undefined ? size - offset : Math.min(o.length, size - offset);
    recs[obj] = {
      file: index.get(name), kind: KIND_SAMPLE, voice, volume: DEFAULT_VOLUME, offset, length, period: o.period ?? DEFAULT_PERIOD,
      count: o.count ?? -1, reload: o.reload ?? 0, next: o.next ?? NONE, script: o.script ?? SCRIPT_NONE,
    };
  };
  const synth = (obj, kind, length, voice, period, script) => {
    recs[obj] = { file: NONE, kind, voice, volume: DEFAULT_VOLUME, offset: 0, length, period, count: -1, reload: 0, next: NONE, script };
  };

  sample(0, "textcpt", 0);
  sample(1, "textcpt", 1, { length: OBJECT1_BYTES });
  sample(2, "textegn", 0);
  sample(3, "textegn", 1, { offset: OBJECT3_OFFSET });
  synth(4, KIND_TONE, TONE_BYTES, 3, DEFAULT_PERIOD, SCRIPT_TONE);
  sample(5, "texttre", 2, { count: 1 });
  synth(6, KIND_NOISE, NOISE_BYTES, 2, DEFAULT_PERIOD, SCRIPT_NOISE);
  sample(8, "textwnd", 0);
  if (recs[3].kind === KIND_SAMPLE) sample(9, "textwnd", 1, { offset: OBJECT9_OFFSET, length: Math.max(0, recs[3].length - OBJECT9_OFFSET) });
  synth(0x0a, KIND_NOISE, NOISE_BYTES, 2, GUN_PERIOD, SCRIPT_GUN);
  sample(0x0b, "textger", 2);
  if (recs[2].kind === KIND_SAMPLE) sample(0x0c, "textegn", 2, { script: SCRIPT_LAUNCH });
  for (const [even, name, count, reload, next] of MUSIC) {
    sample(even, name, 0, { count, reload, next });
    sample(even + 1, name, 1, { count, reload, next: next + 1 });
  }
  sample(0x23, "text201", 0, { period: 179 });
  sample(0x24, "text201", 1, { period: 181 });

  const out = new Uint8Array(OBJECTS * RECORD_BYTES);
  const dv = new DataView(out.buffer);
  recs.forEach((r, i) => {
    const o = i * RECORD_BYTES;
    dv.setUint8(o, r.file); dv.setUint8(o + 1, r.kind); dv.setUint8(o + 2, r.voice); dv.setUint8(o + 3, r.volume);
    dv.setUint32(o + 4, r.offset); dv.setUint32(o + 8, r.length); dv.setUint16(o + 12, r.period);
    dv.setInt8(o + 14, r.count); dv.setUint8(o + 15, r.reload); dv.setUint8(o + 16, r.next); dv.setUint8(o + 17, r.script);
  });
  return out;
}
