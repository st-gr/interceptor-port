// manifest.json (tools/extract/extract/manifest.py). The extractor version is shared with the Python port:
// both change together (tools/extract/README.md).
import { utf8 } from "./util.js";

export const EXTRACTOR_VERSION = 27;

const K = new Uint32Array([
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
]);

/** SHA-256 in plain JavaScript: the fallback where Web Crypto is missing (a browser page served over plain http
 *  has no crypto.subtle). Returns the digest as a Uint8Array of 32 bytes. */
export function sha256Pure(data) {
  const h = new Uint32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
  const n = data.length;
  const total = (((n + 9 + 63) >> 6) << 6);
  const buf = new Uint8Array(total);
  buf.set(data);
  buf[n] = 0x80;
  const dv = new DataView(buf.buffer);
  dv.setUint32(total - 8, Math.floor(n / 0x20000000), false);   // bit length, high word
  dv.setUint32(total - 4, (n << 3) >>> 0, false);
  const w = new Uint32Array(64);
  const rotr = (x, k) => ((x >>> k) | (x << (32 - k))) >>> 0;
  for (let off = 0; off < total; off += 64) {
    for (let i = 0; i < 16; i++) w[i] = dv.getUint32(off + 4 * i, false);
    for (let i = 16; i < 64; i++) {
      const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >>> 3);
      const s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >>> 10);
      w[i] = (w[i - 16] + s0 + w[i - 7] + s1) >>> 0;
    }
    let [a, b, c, d, e, f, g, hh] = h;
    for (let i = 0; i < 64; i++) {
      const S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const ch = (e & f) ^ (~e & g);
      const t1 = (hh + S1 + ch + K[i] + w[i]) >>> 0;
      const S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const maj = (a & b) ^ (a & c) ^ (b & c);
      const t2 = (S0 + maj) >>> 0;
      hh = g; g = f; f = e; e = (d + t1) >>> 0; d = c; c = b; b = a; a = (t1 + t2) >>> 0;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  const out = new Uint8Array(32);
  const odv = new DataView(out.buffer);
  h.forEach((v, i) => odv.setUint32(4 * i, v, false));
  return out;
}

const toHex = (bytes) => Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");

/** SHA-256 of `data` as lower-case hex: Web Crypto when the page has it, else the pure implementation. */
export async function sha256Hex(data) {
  const subtle = globalThis.crypto && globalThis.crypto.subtle;
  if (subtle) return toHex(new Uint8Array(await subtle.digest("SHA-256", data)));
  return toHex(sha256Pure(data));
}

/**
 * The manifest object and its bytes. `files` is the Map of every output file except manifest.json; `sourceSha256` is the
 * hex digest of the ADF. The "files" object lists the paths in sorted order, as the Python writer does.
 */
export async function buildManifest(files, sourceSha256, sampleRates, spansRelocated = true) {
  const entries = {};
  for (const path of [...files.keys()].filter((p) => p !== "manifest.json").sort()) {
    entries[path] = await sha256Hex(files.get(path));
  }
  const manifest = {
    extractor_version: EXTRACTOR_VERSION,
    source_sha256: sourceSha256,
    sample_rate_hz: sampleRates,
    // False: the flight tables' junk spans carry the flat values of their relocated longs (no segment table); the core refuses such assets
    spans_relocated: spansRelocated,
    files: entries,
  };
  return { manifest, bytes: utf8(JSON.stringify(manifest, null, 2) + "\n") };
}
