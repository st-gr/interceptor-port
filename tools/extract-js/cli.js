#!/usr/bin/env node
// node tools/extract-js/cli.js <image.adf> <assets dir> [--segments <file> | --allow-flat-spans]
//
// The same command line as `python -m extract`, for comparing the two ports and for use outside the browser. The only
// file system code of the package lives here, not in src/.
import { mkdirSync, readFileSync, writeFileSync, existsSync, rmSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { extract, ExtractError } from "./src/index.js";

function usage() {
  console.error("usage: node cli.js <image.adf> <assets dir> [--segments <file> | --allow-flat-spans]");
  return 2;
}

async function main(argv) {
  const allowFlatSpans = argv.includes("--allow-flat-spans");
  let args = argv.filter((a) => a !== "--allow-flat-spans");
  let segmentsPath = null;
  if (args.length === 4 && args[2] === "--segments") {
    segmentsPath = args[3];
    args = args.slice(0, 2);
  }
  if (args.length !== 2) return usage();
  const [adfPath, outDir] = args;
  if (!existsSync(adfPath)) {
    console.error(`error: ${adfPath} does not exist`);
    return 1;
  }
  if (segmentsPath === null && !allowFlatSpans) {
    // as the Python CLI: the table shipped with the package (the loader's RAM bases), unless the spans are to stay flat
    segmentsPath = path.join(path.dirname(fileURLToPath(import.meta.url)), "segments.txt");
  }
  const bytes = readFileSync(adfPath);
  try {
    const { files } = await extract(new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength), {
      segments: segmentsPath ? readFileSync(segmentsPath, "utf8") : null,
      allowFlatSpans,
    });
    rmSync(outDir, { recursive: true, force: true });
    for (const [name, data] of files) {
      const dest = path.join(outDir, name);
      mkdirSync(path.dirname(dest), { recursive: true });
      writeFileSync(dest, data);
    }
  } catch (e) {
    if (e instanceof ExtractError) {
      console.error(`error: ${e.message}`);
      return 1;
    }
    throw e;
  }
  console.log(`assets written to ${outDir}`);
  return 0;
}

process.exitCode = await main(process.argv.slice(2));
