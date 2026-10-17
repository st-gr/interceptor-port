# vamiga-ref: headless reference emulator

Boots a disk image in vAmiga's software core, with no display, and writes the
stable framebuffer as a binary PPM. It is the reference for comparing the port
against the original game. The AROS Kickstart replacement (open source) is the
default ROM; see `docs/re/01-reference-emulator.md` for what it can and cannot run.

All outputs are git-ignored: ROMs and disks under `extracted/` and `disks/`,
binaries under `build/`. Never commit a ROM or anything extracted from a disk.

## Build (about 15 minutes the first time)

    bash tools/vamiga-ref/build.sh

Clones vAmiga to `/sandbox/vamiga`, builds its Core in `/sandbox/vamiga-ref-build`
(outside the repo so `make clean` leaves it alone) and installs
`build/vamiga-ref/ref`.

## Run

    python tools/vamiga-ref/fetch_aros_rom.py extracted      # extracted/aros.rom + aros.ext
    bash tools/vamiga-ref/make_autoboot_adf.sh               # extracted/interceptor-autoboot.adf
    build/vamiga-ref/ref --rom extracted/aros.rom --ext extracted/aros.ext \
        --adf extracted/interceptor-autoboot.adf --seconds 60 --out /tmp/game.ppm

Options: `--rom`, `--ext`, `--adf`, `--rw` (insert the ADF write-enabled: the game's flight log can then be written; without it the disk is write-protected and the game reports its log disk as not write enabled), `--seconds N` (wall clock, real time),
`--out FILE`, `--warp` (run as fast as possible; N is then wall clock, so far
more emulated time passes), `-v` (print CPU/debugger state and the next
instruction at the end, useful to see whether the machine is alive).
Convert for viewing with `magick /tmp/game.ppm /tmp/game.png`.

The machine is an A500 (OCS, 512 KB chip RAM, 512 KB slow RAM), PAL. The frame is
912x313 (hires width; vAmiga's texture is super-hires and every second texel is kept).

To use a real Kickstart 1.2 instead of AROS (needed for the game, see the note), put it
at `disks/kick12.rom` (git-ignored) and run `ref --rom disks/kick12.rom --adf extracted/interceptor.adf ...`
without `--ext`.

## Scripting (`--script FILE`)

`ref --rom disks/kick12.rom --adf extracted/interceptor-autoboot.adf --script FILE` boots the
machine, then runs one command per line (blank lines and `#` comments are skipped). The
script is parsed completely before the machine boots; a bad line prints
`script line N: ...` and exits 2. `--seconds` and `--out` are ignored in script mode.
All waits are wall clock (the machine runs in real time).

| command | effect |
| --- | --- |
| `wait <seconds>` | sleep |
| `key <NAME> [hold_seconds]` | press and release an Amiga raw key (default hold 0.12 s) |
| `type <text>` | letters, digits, spaces, one key at a time; a trailing `\n` (backslash n) adds RETURN |
| `mouse <x> <y>` | absolute pointer position, texture coordinates (x 0..912, y 0..313); the frame's game area starts at (186, 43) |
| `click` | left mouse button down and up |
| `frame <path.ppm>` | write the current frame (912x313) |
| `snapshot <path>` | save a vAmiga snapshot; the path must end in `.vasnap` |
| `code @FILE CHALLENGE` | looks `CHALLENGE` up in FILE (lines `CHALLENGE ANSWER`, `#` comments), types the answer and RETURN; keeps derived countercodes out of committed scripts (use the git-ignored `disks/countercodes.txt`) |
| `peek <addr> <len> [label]` | hex dump of RAM through spy reads (no side effects) as one log line `label ADDR hex...` (32 bytes per group); `<addr>` is hex: `F:4B23C` is a flat-image address (translated through the last `seglist`), `R:C42230` or a bare number is a RAM address |
| `seglist <image.bin> <map.json>` | locates the running game's hunks and logs `seg N flat F ram R size S segsize Z ok` per hunk plus a summary line; arms the `F:` addresses of `peek` (method below). Run it after every `restore` |
| `colors [label]` | logs Denise's COLOR00..COLOR31 (12-bit hex words); the colour registers cannot be read over the bus on OCS, so they come from the chip's state |
| `log <text>` | writes `# text` to the log |
| `step [n]` | after `freeze`: run the machine n video frames (one 50 Hz vertical blank each) and pause again |
| `holduntil KEY ADDR lt\|gt VALUE [max_s]` | hold KEY until the angle word at ADDR (28800 per circle, folded to -14400..14400) is below or above VALUE (polled every 5 ms; the release lags the target by one to four game frames, the logged value is exact), release, log `# holduntil reached N` |
| `stepuntil <addr> <len 1\|2\|4> and\|eq\|ne\|lt\|gt <value> [max_frames]` | after `freeze`: run frame by frame until the big-endian value at `<addr>` (as for `peek`) matches (`and`: the value ANDed with the argument is non-zero; `lt` and `gt` compare signed), tested before the first frame too; logs `# stepuntil reached after N frames`; frame-exact alignment with a game event whatever the wall-clock jitter before it (default limit 2000 frames; a timeout is an error) |
| `poke <addr> <len 1\|2\|4> <value>` | after `freeze`: write slow RAM (`0xC00000..`) or chip RAM, big-endian, same address forms as `peek`; a capture aid for staging a state (the crash allowance and the part id `+0x7B` in note 05 17.17), never for reading the game. The value is read like C `strtol` with base 0 (decimal, `0x` hex, or octal for a leading `0`, so `010` is 8; negative allowed) and **truncated to the low LEN bytes** without a warning (`poke F:4A94F 1 0x1FF` writes `0xFF`, a 2-byte poke of 70000 writes its low 16 bits); an address outside slow and chip RAM fails the run |
| `freeze` / `thaw` | pause / resume the emulated machine, so a group of `peek`, `colors` and `frame` commands sees one instant |
| `copy <src> <dst> <len>` | after `freeze`: copies LEN bytes (1 to 65536) inside the machine's RAM, source read through spy reads, both addresses in the forms of `peek`; a staging aid that moves what the running game holds (a template descriptor into another record), so that a script names addresses only |
| `pokefill <addr> <len> <hex bytes> [ifblit]` | after `freeze`: like `poke`, writes LEN bytes from ADDR with the byte pattern repeated (a staging aid: the C-modulus probe fills a bitplane with a pattern before a blit); `ifblit`: only when the previous `runto` stopped at a blit |
| `runto <addr> [max_s] [ifblit]` | after `freeze`: sets a CPU breakpoint at ADDR (as for `peek`), runs and stops there before the instruction executes (or earlier at a `blitstop` blit); logs `# runto reached ADDR`; a timeout (default 10 s wall clock) is an error; `ifblit`: skipped unless the previous `runto` stopped at a blit |
| `blittrace on\|off` | while on, `runto` also stops at every CPU write to BLTSIZE (a watchpoint at 0xDFF058), logs one `blit` line and goes on: the PC after the write, vAmiga's BLTCON0/1, the A/B/C/D pointers, the masks, all four moduli (`amod bmod cmod dmod`), the CPU's D0/D4/D7 (the game's blit routines write BLTSIZE from one of them), the busy flag and the blitter accuracy level (0 fast, 1 fake, 2 slow: the default, vAmiga's cycle-exact SlowBlitter); the blit itself has not started yet |
| `blitstop <bltcon0 hex>\|off` | `runto` returns right after the BLTSIZE write of a traced blit with this BLTCON0 (logs `# runto stopped after the BLTSIZE write of BLTCON0 ...`), so that `peek` sees the planes before it; to see the result, stop at the next blit's BLTSIZE write (a routine that waits for the blitter first) |
| `blitregs [label]` | logs the same line as `blittrace` now |
| `audtrace on [nodmacon\|frame]\|off` | with `frame`: no watchpoint, `step` and `stepuntil` log one `aud` line per video frame (the state at the end of the frame; fast: a watchpoint stop costs a few hundred milliseconds of wall clock); otherwise, while on, `runto`, `step` and `stepuntil` log every CPU write to `AUD0..3` LC, LEN, PER, VOL, to `DMACON` and to `ADKCON` (a CPU watchpoint on each register; `nodmacon` leaves DMACON out, for 3D scenes in which the game's blitter code writes it hundreds of times a frame): one `aud` line per stop with the frame, the raster line and colour clock, the PC after the write, the registers that changed since the last line (`same` when a register was rewritten with its old value) and then every channel's LC, LEN, PER and VOL as vAmiga holds them; a stop covers the whole instruction, so a long write to LC shows both halves. Needs a frozen machine; on a free-running machine (`thaw`) the stops would pause it |
| `runto <addr> [max_s] soft` | as `runto` but reaching `max_s` first is not an error (`# runto timeout (soft) before ADDR`): a timed run under `audtrace`, for example `runto 0 420 soft` from a cold boot |
| `wav <path.wav> start [vamiga]\|stop` | record the emulator's mixed stereo output (vAmiga's audio port) as a 16-bit stereo WAV at the host sample rate (44,100 Hz). `start` sets hard panning (voices 0 and 3 left, 1 and 2 right: the hardware's pairing) and no audio filter, so that the recording is the voices as the game wrote them; `start vamiga` keeps vAmiga's defaults (a 70 % stereo separation and the A500 filter); a thread drains the port's ring buffer into memory, `step` waits while the buffer is more than 40 % full, the adaptive sample rate is switched off so that the time base is the emulated one (882 samples per 50 Hz frame); `stop` writes the file and logs `# wav PATH samples N rate R` |
| `keydown NAME` / `keyup NAME` | press and hold a key until `keyup` (SHIFT with a digit; a key held across a screen change); `key` is a press and release |
| `unprotect` | clears the write protection of the disk in DF0 (a restored snapshot has it write-protected; the game then reports its log disk as not write enabled; after `unprotect` its DOS can write the `config` file) |
| `savedisk <path.adf>` | exports the disk in DF0 as an ADF, to see what the game wrote (the `config` file of the flight log) |
| `restore <path>` | load a `.vasnap`; waits until the machine has run 3 s first (an earlier load fails vAmiga's hash check) |

A script of more than about 8,000 commands makes the driver crash at start (found with the crash-flow captures of note 05 17.17); keep a script below that, and for a long one pass `--log FILE` (flushed per line) so that the lines survive a crash at exit.

Key names: `1`-`0`, `Q`-`P`, `A`-`L`, `Z`-`M`, `SPACE`, `RETURN`, `ESC`, `F1`-`F10`, `UP DOWN
RIGHT LEFT`, `LSHIFT`, `KP0`-`KP9` (Amiga raw codes; table in `ref.cpp`). The mouse goes to
control port 1 and is untested with this game (it is keyboard driven).

Added key names: `MINUS EQUALS BACKSPACE LBRACKET RBRACKET COMMA PERIOD RSHIFT KPDOT KPENTER`.
Any other raw Amiga key code is `key 0xNN` (0x00 to 0x7F, for example `key 0x66` for the left Amiga key or `key 0x46` for DEL); the key census of note 08 section 11 (`gen-flow-egg.py`) uses it.
Errors, including ones found only when a command runs (a bad snapshot, a failed write), print
`script line N: ...` and exit 2; paths are checked at parse time (`.vasnap` / `.ppm` suffix, existing
directory).

The blitter commands and `runto` were added for the C-modulus probe of note 04 ("Stage [90]": `scripts/gen-nearground.py probe` and `probefill`, checked by `tools/m68k-rig/tests/lists_model/nearground_blit_check.py`). vAmiga's `stepInto` did not stop when called from this driver, so there is no single-step command.

Scripts in `scripts/` (all timing is wall-clock and jittery; the flight scripts start from snapshots):
`boot-to-credits.txt` (150 s once, writes `credits.vasnap`), `demo-flight.txt`, `freeflight-prompt.txt`
(stops at the security prompt and freezes it), `freeflight-takeoff.txt` (types the countercode for the frozen
challenge via `code`; selections, cockpit, external and head views), `freeflight-headings.txt` (takeoff and three
headings), `keys-views.txt` and `keys-flight.txt` (each key once, frames before and after). `wheel_view.py` renders the codewheel for a challenge so you can read the answer by eye
(see the note). Captures are described in `docs/re/01a-reference-captures.md`. The crash-flow captures of note 05 17.17 (`crash-setup-dive.txt` first, then `crash-flow-*.txt`, `crash-keys-*.txt`, `crash-mouse-in-hold.txt`, `crash-allowance-snapshots.txt`, `carrier-*.txt`, `crash-return-key.txt`) are described in `docs/re/01b-flight-captures.md`; run them with `--log FILE`.

The combat closure captures of milestone 7 (note 07; task 3) are `scripts/combat7-*.txt`, written by `scripts/gen-combat7.py PART > combat7-PART.txt` (parts `gunorder`, `gunorder1`: the whole tracer pool at a gun kill in the chase view; `statedump`: the whole machine state of a free flight, converted by `tools/m68k-rig/tests/lists_model/state_dump.py`; `chaffdraw`, `chaffdrawback`, `chaffdrawhead`, `chaffdrawfixed`, `chaffheadorder`, `chaffpix`, `chaffptr`: the chaff drawer stopped at each call with the draw-time camera; `pass93trace`, `pass93site`, `panelband`, `panelmem`: the first non-zero panel counter of a free flight and the external view's panel fills; `subkill`, `losttarget`, `placer15`, `retarget`: run-time pokes into the dying path, the lost-target rule, the entry placer and the player's missile). Logs go to `extracted/captures/logs/combat7-PART.txt`; run one script at a time under `flock /tmp/ref.lock`, with the flock outside and `timeout` inside, so that waiting for the lock does not use up the timeout (`flock /tmp/ref.lock timeout 900 build/vamiga-ref/ref ...`).

## Logging and memory inspection (`--log FILE`)

`--log FILE` appends every `peek`, `colors`, `log` and `seglist` line to FILE (they are also printed). `--parse-only`
parses the script, prints the command count and exits (used by `test_parse.sh`, which also checks that bad lines exit 2).
`logparse.py` reads such a log (`load(path)["label"].u16(offset)` and so on).

Flat-to-RAM mapping: the game is run by AmigaDOS (LoadSeg), so every hunk is a separate allocation, laid out as
`[size long][BPTR to the next segment's link long][data]` in slow RAM (0xC00000 upward on this machine), and the load
addresses depend on the boot path. `seglist` scans chip and slow RAM for the first 16 bytes of the flat image (the operand
longs that the relocation turned into flat addresses are wildcards), finds hunk 0 at `data`, then follows the BPTR chain
(`next*4+4` is the next data address), checks each segment size (hunk size + 8) against `interceptor.map.json` and
stops at the zero link; all 185 hunks resolve. A flat address `F` in hunk `h` is at `ram(h) + (F - flat(h))`.
Hunks are not laid out in order (hunk 62 is in chip RAM, small hunks sit below hunk 0), so never add one constant. `seglist` fails if
any segment size differs from hunk size + 8 by more than 8.
Scripts under `scripts/openers-*.txt` use these commands; see `docs/re/01b-flight-captures.md`.
