# Interceptor port

A clean-room port of *F/A-18 Interceptor* (Electronic Arts, 1988, Amiga) to C99, with an SDL2 desktop
frontend and a browser build (WebAssembly). The port reproduces the original's rules: its flight model, its
screens and timing, its weapons and missions, its sound library. The rules were recovered by reverse
engineering the original program and are written down in `docs/re/`; the code in `src/` was written from those notes.

## What this is not

This repository holds no game asset: no program code of the original, no disk image, no model, picture, text,
sound or sample, and no table of the disk's bytes. The notes in `docs/re/` identify each such table by its file
name and an FNV-1a-32 hash and describe its rule (length, ordering, ranges, what an entry means); the extractor
reads the bytes from your disk. What the notes and the source do contain are the rule constants a
reverse-engineering note has to state (addresses, a few parameters per rule, formulas), and a handful of short
runs of numbers inside such rules. A clean-room scan (`tools/cleanroom`) compares the tree with the disk byte
for byte, with documented limits (see its README: short windows, runs of few distinct bytes and numbers
printed as text are not seen). To play you need your own copy of the original disk. The port
reads it at run time (the extractor in `tools/extract` for the desktop build, a JavaScript port of it in
`tools/extract-js` for the browser). Nothing of the disk is uploaded anywhere: in the browser it stays in the
page. The title and everything in its data belong to Electronic Arts.

## How to play

**In the browser.** Open the hosted page, `https://st-gr.github.io/interceptor-port/`, drop your own disk image of the
game (an `.adf`, 901,120 bytes) on it, wait for "Ready" and click Start. Flux images (SCP, HFE, MFI) must be
decoded to an ADF first, for example with [Greaseweazle](https://github.com/keirf/greaseweazle):
`gw convert --format amiga.amigados disk.scp game.adf`. The flight log is kept in the browser's own storage.

**On the desktop.** You need a C99 compiler, SDL2, make and Python 3.

    pip install -e tools/extract
    python -m extract your-disk.adf assets      # assets/ is git-ignored; never commit it
    make                                        # build/interceptor
    build/interceptor                           # the credits, then the menu
    build/interceptor --start-keys              # the same path with the keys sent for you
    make core test                              # the unit tests, no assets needed
    make golden                                 # scripted runs against tests/golden (the synthetic ones need no assets)
    make web                                    # the browser build in build/web (needs Emscripten)

The player's guide is `docs/manual.md`; `docs/browser-checklist.md` lists the checks only a person in a real
browser can make.

## Status

| Milestone | Content | State |
| --- | --- | --- |
| 1 | Tooling and skeleton: disk decoding, the extractor, the relocated program image, the 68000 comparison rig | done |
| 2 | The world: terrain, objects, the display-list builder and the draw layer | done |
| 3 | Flight: the flight model, take-off, landing, crash rules, cockpit, HUD, sky, ground | done |
| 4 | Game flow: the start screens, menu, demonstration, flight school, carrier qualification, the flight log | done |
| 5 | Combat: cannon, missiles, radar, countermeasures, damage, ejection, the six missions, the hostile aircraft | done |
| 6 | Sound (the original's sound library as a model, the Paula mixer) and the browser build with the in-browser extractor | done |
| 7 | Closure and release: the remaining open rules, the clean-room scan, this export | this release |

## Releases

Maintainers cut a native binary release from GitHub's **Actions → release → Run workflow** page. Enter a semantic
version without the `v` prefix, such as `1.0.0`. The workflow builds Linux x86_64, macOS arm64 and Windows x86_64
archives, writes checksums, creates the matching `v1.0.0` tag and uploads the archives to the GitHub release. The
Windows archive includes SDL2; Linux and macOS users need SDL2 installed as described above.

## Clean room

The code was written from notes, not from the original's bytes. `tools/cleanroom/scan.py` slides a 12-byte window
over every file of this tree and compares it with the extracted assets, the disk image and the program image of
the original. The public export runs it and stops on any match; this tree was exported with no unallowed match.
Its allow list (`tools/cleanroom/allow.txt`) names each tolerated run with a reason. The tests use invented data.
The reverse-engineering notes describe the original's rules in prose and with the port's own constants.
You can repeat the scan with your own disk: `python tools/cleanroom/scan.py --repo . --assets assets --disk
your-disk.adf --bin image.bin` (the program image `image.bin` comes from `tools/hunk/relocate.py`).

The code-wheel tooling (the decoder of the original's copy-protection wheel and its answer table, which the notes
mention as the author's private tooling) and the author's sandbox profiles are not part of this tree. The port
skips the original's security prompt.

## Licence

GPL-3.0, see `LICENSE`. The reference-emulator tooling under `tools/vamiga-ref` builds on
[vAmiga](https://github.com/dirkwhoffmann/vAmiga) (GPL-3.0) and is GPL-3 as upstream; it is tooling for checking
the notes and is not part of the port.

## Trademarks and copyright

*F/A-18 Interceptor* is a title of Electronic Arts, the game's program and data are their copyright, and the
publisher's name and the title are used here only to say what this software is compatible with. This project is
not made, endorsed or approved by Electronic Arts or any rights holder. The port contains no asset of the original
(no program code, picture, model, text, sound, sample or table of bytes); its notes and source state the rules the
original follows, with the constants those rules need. Amiga is a trademark of its respective owner.
