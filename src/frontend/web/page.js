// src/frontend/web/page.js - the page's logic apart from the DOM (tested in node: tools/extract-js/test/page.test.js).
//
// loadAdf(): the player's ADF bytes -> the JavaScript extractor -> the files in Emscripten's MEMFS under /assets.
// Nothing is written unless the whole extraction succeeded (Review Focus 3): a refused file leaves the file system as it was
// and the page ready for another pick. The ExtractError texts are shown verbatim.

export const ASSETS_DIR = "/assets";

/** The text shown for a thrown value: the extractor's own words for an ExtractError, a plain sentence for anything else. */
export function errorMessage(e) {
  if (e && e.name === "ExtractError") return e.message;
  return `Something went wrong while reading the disk image: ${e && e.message ? e.message : String(e)}`;
}

/** Create `files` (a Map of relative path -> Uint8Array) under `root` in `fs` (Emscripten's FS: mkdirTree, writeFile). */
export function writeAssets(fs, files, root = ASSETS_DIR) {
  const dirs = new Set([root]);
  for (const name of files.keys()) {
    const i = name.lastIndexOf("/");
    if (i > 0) dirs.add(`${root}/${name.slice(0, i)}`);
  }
  for (const d of [...dirs].sort()) fs.mkdirTree(d);
  for (const [name, bytes] of files) fs.writeFile(`${root}/${name}`, bytes);
}

/**
 * @param {object} o
 * @param {Uint8Array} o.bytes the chosen file
 * @param {(adf: Uint8Array, options: object) => Promise<{files: Map<string, Uint8Array>, manifest: object}>} o.extract
 * @param {string} o.segments the text of the segment table (extract/segments.txt)
 * @param {object} o.fs Emscripten's FS (or a stand-in)
 * @param {(stage: string) => void} [o.onProgress]
 * @returns {Promise<{ok: true, manifest: object, count: number} | {ok: false, message: string}>} never throws
 */
export async function loadAdf({ bytes, extract, segments, fs, onProgress = () => {}, root = ASSETS_DIR }) {
  let result;
  try {
    result = await extract(bytes, { segments, onProgress });
  } catch (e) {
    return { ok: false, message: errorMessage(e) };
  }
  try {
    onProgress("writing");
    writeAssets(fs, result.files, root);
  } catch (e) {
    return { ok: false, message: errorMessage(e) };
  }
  return { ok: true, manifest: result.manifest, count: result.files.size };
}

/** The progress line's words for the extractor's stage names. */
export function stageText(stage) {
  const words = {
    disk: "Reading the disk",
    pictures: "Decoding the pictures",
    sounds: "Decoding the sounds",
    executable: "Reading the program",
    tables: "Reading the tables",
    manifest: "Checking the result",
    writing: "Preparing the game",
  };
  return `${words[stage] || stage}...`;
}

/** Called by the Start click (the user gesture): unlocks the audio context when the build has the hook, else nothing. */
export function unlockAudio(module) {
  if (module && typeof module._web_audio_unlock === "function") module._web_audio_unlock();
}

// ---- the flight log (milestone 7, Task 7) ----------------------------------------------------------------------------------------------------
// The log file lives on an IDBFS (the browser's IndexedDB, one store per origin and profile) mounted at LOG_DIR: loaded with syncfs(true) before the
// program starts, written back with syncfs(false) after every save point of the game (the frontend calls the exported web_log_saved, which calls
// Module.onLogSaved). Storage that is denied or full (private mode, a quota) is not fatal (Review Focus 3): the directory stays an ordinary
// in-memory one, the game runs, and the page says once that the log will not outlast the visit.

export const LOG_DIR = "/log";
export const LOG_PATH = "/log/flight.log";
export const LOG_OPEN_TIMEOUT_MS = 5000;
export const STORAGE_MESSAGE = "The flight log cannot be kept in this browser (its storage is not available); it stays in memory for this visit only.";

/** Mount the IDBFS at `dir` and load it. Never throws, never rejects, and never waits longer than `timeoutMs` (an IndexedDB open that hangs, as in some
 *  private modes, must not stop the game from starting: the log then stays in memory, as for a refused open). @returns {Promise<{persistent: boolean}>}
 *  persistent is false when the storage failed or did not answer in time. */
export function mountLogStore(module, dir = LOG_DIR, timeoutMs = LOG_OPEN_TIMEOUT_MS) {
  const fs = module.FS;
  return new Promise((resolveOnce) => {
    let settled = false, timer = null;
    const resolve = (v) => { if (settled) return; settled = true; if (timer !== null) clearTimeout(timer); resolveOnce(v); };
    timer = setTimeout(() => resolve({ persistent: false }), timeoutMs);
    try {
      fs.mkdirTree(dir);
      const idbfs = fs.filesystems && fs.filesystems.IDBFS;
      if (!idbfs) { resolve({ persistent: false }); return; }
      fs.mount(idbfs, {}, dir);
      fs.syncfs(true, (err) => resolve({ persistent: !err }));
    } catch (e) {
      resolve({ persistent: false });
    }
  });
}

/** A writer of the log directory to the storage: one syncfs(false) at a time, a request during one is run after it. `onDone(err)` follows each. */
export function makeSyncer(fs, onDone = () => {}) {
  let busy = false, again = false;
  function request() {
    if (busy) { again = true; return; }
    busy = true;
    let finished = false;
    const done = (err) => {
      if (finished) return;
      finished = true;
      busy = false;
      onDone(err || null);
      if (again) { again = false; request(); }
    };
    try { fs.syncfs(false, done); } catch (e) { done(e); }
  }
  return request;
}
