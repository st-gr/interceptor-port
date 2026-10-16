import test from "node:test";
import assert from "node:assert/strict";
import { decodeIlbm, unpackByterun1 } from "../src/ilbm.js";
import { writePng } from "../src/png.js";
import { Adf } from "../src/adf.js";
import { DecodeError } from "../src/tables.js";
import { fixtureBytes, expectedFiles } from "./helpers.js";

const u32 = (v) => [v >>> 24, (v >>> 16) & 255, (v >>> 8) & 255, v & 255];
const tag = (s) => [...s].map((c) => c.charCodeAt(0));
const chunk = (id, body) => [...tag(id), ...u32(body.length), ...body, ...(body.length & 1 ? [0] : [])];

function ilbm({ w, h, planes, compression = 0, body, cmap = [], mask = 0 }) {
  const bmhd = [w >> 8, w & 255, h >> 8, h & 255, 0, 0, 0, 0, planes, mask, compression, 0, 0, 0, 1, 1, 0, 0, 0, 0];
  const inner = [...tag("ILBM"), ...chunk("BMHD", bmhd), ...(cmap.length ? chunk("CMAP", cmap) : []), ...chunk("BODY", body)];
  return Uint8Array.from([...tag("FORM"), ...u32(inner.length), ...inner]);
}

// 16 x 2, two planes. Row 0: plane 0 = F0 0F, plane 1 = FF 00; row 1: plane 0 = 00 00, plane 1 = 80 01.
const ROW0 = [3, 3, 3, 3, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1, 1, 1];
const ROW1 = [2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2];
const BODY = [0xf0, 0x0f, 0xff, 0x00, 0, 0, 0x80, 0x01];

test("two planes: the plane bits are combined into pixel indices, the CMAP is read", () => {
  const img = decodeIlbm(ilbm({ w: 16, h: 2, planes: 2, body: BODY, cmap: [1, 2, 3, 4, 5, 6] }));
  assert.deepEqual([...img.pixels], [...ROW0, ...ROW1]);
  assert.deepEqual(img.palette, [[1, 2, 3], [4, 5, 6]]);
  assert.equal(img.nplanes, 2);
});

test("ByteRun1 bodies and mask planes decode to the same pixels", () => {
  const packed = [7, ...BODY];
  assert.deepEqual([...decodeIlbm(ilbm({ w: 16, h: 2, planes: 2, compression: 1, body: packed })).pixels], [...ROW0, ...ROW1]);
  // with a mask plane after each row's colour planes (masking = 1): the mask is skipped
  const masked = [0xf0, 0x0f, 0xff, 0x00, 0xaa, 0xaa, 0, 0, 0x80, 0x01, 0x55, 0x55];
  assert.deepEqual([...decodeIlbm(ilbm({ w: 16, h: 2, planes: 2, body: masked, mask: 1 })).pixels], [...ROW0, ...ROW1]);
});

test("unpackByterun1: literal runs, repeat runs, the no-op and the output limit", () => {
  assert.deepEqual([...unpackByterun1(Uint8Array.of(1, 9, 8, 0xfe, 7, 0x80, 0, 5), 100)], [9, 8, 7, 7, 7, 5]);
  assert.deepEqual([...unpackByterun1(Uint8Array.of(0xfd, 1), 3)], [1, 1, 1]);
});

test("not an ILBM, a missing BODY and a short body are refused", () => {
  assert.throws(() => decodeIlbm(new Uint8Array(20)), (e) => e instanceof DecodeError && /not an IFF ILBM/.test(e.message));
  const noBody = Uint8Array.from([...tag("FORM"), ...u32(4), ...tag("ILBM")]);
  assert.throws(() => decodeIlbm(noBody), /missing BMHD or BODY/);
  assert.throws(() => decodeIlbm(ilbm({ w: 16, h: 2, planes: 2, body: [1, 2, 3] })), /truncated/);
});

test("the synthetic disk's pictures: idx, palette json and PNG bytes equal the Python extractor's", () => {
  const disk = new Adf(fixtureBytes("synth.adf"));
  const expected = expectedFiles();
  for (const name of ["splsh", "inst5", "frnt5"]) {
    const img = decodeIlbm(disk.read(`pix/${name}`));
    assert.deepEqual(img.pixels, expected.get(`pix/${name}.idx`));
    assert.deepEqual(writePng(img.width, img.height, img.pixels, img.palette), expected.get(`pix/${name}.png`));
  }
});
