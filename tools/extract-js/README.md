# extract-js

The asset extractor of the port in JavaScript, for the browser build: the player's own *F/A-18 Interceptor* ADF in, the
`assets/` tree out, in memory. It writes the same files, the same bytes and the same `manifest.json` as the Python extractor
(`tools/extract`, `python -m extract`), at the same extractor version. Nothing of the game is in this package: the decoders are
pure functions over byte arrays (no DOM, no file system, no dependencies), the test disk is invented, and the comparison with
the real disk is run locally only.

Needs Node 22 or a current browser (ES modules, `async`/`await`, `DataView`; Web Crypto for SHA-256 where the page has it,
a small pure implementation where it has not, for instance on a page served over plain `http`).

## Use

    import { extract, ExtractError } from "./src/index.js";

    const { files, manifest } = await extract(new Uint8Array(await file.arrayBuffer()), { segments: segmentTableText });
    // files: Map<string, Uint8Array>   "pix/splsh.png", "tables/ai.bin", ..., "manifest.json"
    // manifest: the parsed manifest.json (extractor_version, source_sha256, sample_rate_hz, files{path: sha256}, ...)

`extract(adfBytes, options)` options:

- `segments`: the text of the loader's segment table (`segments.txt` of this package, the RAM bases only; the Python extractor
  ships the same text as `extract/segments.txt` and a test keeps the two equal). The flight step's junk spans carry the reference machine's values for their relocated longs
  only with it. Without it `extract` refuses, unless
- `allowFlatSpans: true`: the spans keep the flat values, `manifest.spans_relocated` is `false` and the core refuses the assets;
- `onProgress(stage)`: called with `disk`, `pictures`, `sounds`, `executable`, `tables`, `manifest`.

Errors are `ExtractError`s (`error.name === "ExtractError"`) with a message that says what was expected, and no assets:

- not an ADF: a file shorter or longer than 901,120 bytes, no `DOS` boot block, no AmigaDOS root block at block 880;
- a flux image, recognised by its magic and refused by name: SCP, HFE (HxC), MFI (MAME). Convert it with Greaseweazle:
  `gw convert --format amiga.amigados disk.scp disk.adf`;
- an ADF of another disk: the game's files (`pix/splsh`, `text/...`, the executable `F-18 Interceptor`) are missing, or the
  executable is not an Amiga executable (no `HUNK_HEADER`), or a table the extractor reads is not where the game has it;
- a damaged disk (checksum, block list).

`node cli.js <image.adf> <assets dir> [--segments <file> | --allow-flat-spans]` writes the files to a directory, with the same
command line as the Python extractor (`segments.txt` of the package is the default segment table, as the Python extractor's `extract/segments.txt`).

## Layout

`src/` has one module per Python module: `adf` (the AmigaDOS reader that amitools is for the Python side), `hunk`, `ilbm`,
`palette`, `png` (stored deflate blocks, CRC-32, Adler-32), `sky`, `tables`, `cockpit`, `flight`, `lists`, `terrain`,
`models`, `combat`, `flow`, `sounds`, `manifest`, and `index.js` with `extract`. The decoders follow the Python code line by
line, including the order of the checks and the text of every refusal: a model that fails to decode puts its error message in
`models/index.txt`, so the messages are part of the output.

## Tests

    node --test "tools/extract-js/test/*.test.js"

- per module, a test on the synthetic disk (`test/fixtures/synth.adf`, invented bytes made by
  `tools/extract/tests/write_synth.py` from `tools/extract/tests/synth.py`) against the Python extractor's output for it
  (`synth-expected.tar`, 90 files), plus unit tests of the refusals;
- `synth.test.js`: the whole output, the file set, the manifest; and, when Python with amitools is available, that the committed
  expected output is what the Python extractor writes now;
- `corpus.test.js`: `fixtures/corpus.json` holds about 4,500 calls recorded from the Python decoders (the Python tests' calls on
  their invented images, and a mutation fuzz of the table decoders and of the model programs) with the SHA-256 of each result or
  the text of each refusal; each is replayed on the JavaScript decoder (made by `tools/extract/tests/write_corpus.py`);
- `errors.test.js`: the three cases of the browser page (not an ADF, an ADF of another disk, a flux image), on disks made by
  `test/adfbuilder.js`; `adf-files.test.js`: the reader on multi-block files, extension blocks, hash chains, FFS, and (with Python)
  against amitools;
- `tests/frontend/test_extract_js.py` runs both extractors on the synthetic disk and, locally, on `extracted/interceptor.adf`,
  comparing every file byte for byte.

When the Python extractor changes, the JavaScript port changes in the same commit (`tools/extract/README.md`).
