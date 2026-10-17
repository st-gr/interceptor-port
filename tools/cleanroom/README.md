# Clean-room scan

`scan.py` proves that the repository holds no byte string of the original. It slides a 12-byte window
(at least 6 distinct byte values, so counting sequences and padding are not windows) over every git-tracked
file of `--repo` and over the extracted assets, the disk image and the relocated program image, and lists
every window of the repository that occurs in one of them.

    python tools/cleanroom/scan.py --repo . --assets assets \
        --disk extracted/interceptor.adf --bin extracted/interceptor.bin

Output: `FILE OFFSET LENGTH SOURCE:OFFSET`, one line per run of overlapping windows. Exit status 0: no
unallowed match; 1: matches listed; 2: usage or allow-list error. The assets, the disk and the image are
git-ignored, so the scan runs locally only; it gates `tools/release/export-public.sh`.

For each match: invent the value (fixtures), extract it at run time (a table the extractor carries), describe it
in prose (notes), or allow it in `allow.txt`. Two kinds of line:

    FILE OFFSETS [src=SOURCE-PREFIX] [maxlen=N] REASON...
    phrase | TEXT [|| TEXT...] | REASON

The first allows the run of `FILE` whose first byte is at `OFFSETS` (one offset, a comma list, `LO-HI` ranges, or `*` for
generated fixtures whose offsets move; `*` is used only with `src=` and `maxlen=`, which limit the allowed source file
and the run's length). The second allows any run of printable text that is part of one of the listed texts (the title, the
names of AmigaOS libraries and calls, disk file names, ordinary English of the Amiga system files). A line without a reason is
an error, so is a line that suppresses nothing (a stale entry), and `allow.txt` itself is never scanned. Allowed so far:
identification and platform names, the extractor's own scaffolding (the pointer tables its validators require, the sound
table's constants, PNG and WAV headers), and ordinary English in the GPL text. A value that a note or a test asserts as game
data is never allowed: it is replaced. The one exception is identification: the addresses of the two pointer tables of the
program image (the hull table, three consecutive 32-bit addresses, and the burst-shape table, nine), which the notes and the
extractor's validators state to say which image is meant; their two lines in `allow.txt` are pinned to the fixture's exact
offsets and run lengths. Run lines name one file; a `phrase` line applies to every file of the repository. The extractor's listings (`manifest.json`, `index.txt`) are not sources. A run without
assets, the disk or the image skips that source with a note and then proves only that the allow list is consistent; the public
export requires them. Run the scan under `ulimit -v 2500000`; it takes about 85 s on this tree.

## What the scan does not see

The scan finds a byte string of 12 or more bytes with 6 or more distinct values. By design it does not see:

- windows shorter than 12 bytes (a copied run of 11 bytes or fewer);
- runs with fewer than 6 distinct byte values in each window (padding, counting runs, "ABAB..." patterns);
- tables of small numbers: three 32-bit values below 0x10000 have at most 4 distinct bytes per window, so a long table of
  small words or longs is invisible; the notes identify such tables by file name and hash instead of printing them;
- numbers re-encoded as text (decimal or hexadecimal figures, or a prose description of a rule): the review of the notes, and
  the numeric-literal check (runs of 8 or more comma-separated integers compared with `assets/tables/*.bin` as bytes, words and
  longs, either order), cover that, by hand.

`tests/test_scan.py` pins these negatives (`test_short_window_ignored`, `test_few_distinct_bytes_ignored` and the
`test_documented_limit_*` tests), so a change of the rules is a deliberate edit of this list. Because of them the statement
the public README makes is limited: no asset, no sample, no text and no table of bytes; the rule constants a note must state
are in the notes and the source.

Tests: `pytest tools/cleanroom -q` (17 tests on scratch trees with invented bytes; they run in the Linux job of CI, the scan itself never does).
