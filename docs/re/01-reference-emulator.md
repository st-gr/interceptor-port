# Reference emulator

## Setup

The reference is vAmiga's Core (a pure software emulator, no GPU or display
needed) driven by a small program, `tools/vamiga-ref/ref.cpp`. The machine is an
A500: OCS, 512 KB chip RAM plus 512 KB slow RAM, PAL. Commands, from the repo root:

    bash tools/vamiga-ref/build.sh                           # builds build/vamiga-ref/ref
    python tools/vamiga-ref/fetch_aros_rom.py extracted      # extracted/aros.rom, aros.ext
    bash tools/vamiga-ref/make_autoboot_adf.sh               # extracted/interceptor-autoboot.adf
    build/vamiga-ref/ref --rom extracted/aros.rom --ext extracted/aros.ext \
        --adf extracted/interceptor-autoboot.adf --seconds 60 --out /tmp/game.ppm

The ROM is the AROS Kickstart replacement, taken from FS-UAE's source as a 1 MB
blob. The blob holds the extension ROM first and the main ROM second, so the
fetcher splits it accordingly (an earlier assumption of the opposite order left
the CPU looping on the extension's first instruction and the screen black).
The autoboot disk is a copy of the game disk in which the game executable is
renamed `game` (AROS's shell splits the original name at its space) and the
startup-sequence runs it directly. Nothing from these outputs is committed.

Harness checks: vAmiga's built-in DiagROM shows its menu in the captured frame, so
the framebuffer path works; AROS without a disk reaches its boot screen.

## What the boot looked like under AROS

- At 60 seconds: AROS had booted from the disk, run the startup-sequence and
  stopped at a CLI window on its blue screen reporting that the game file is not
  executable.
- At 120 seconds: the same frame, unchanged.

Timing note: boots ran in real time (no warp). While the build was compiling in the
background a boot took noticeably longer in wall-clock terms, so measure on an idle
machine.

## Findings

- AROS loads the disk and runs the original 1.2 command-line tools from it (the
  `rename` command ran and printed its usage), so disk access, the filesystem and
  the shell work.
- The game executable is a valid hunk file (185 hunks, only code, data, bss,
  relocation and symbol hunks, no chip-memory demand of note). AROS's loader
  nevertheless rejects it with "file is not executable", whether the file is
  named with a space or not, and also with its symbol hunks stripped, so those
  are not the cause. The exact trigger inside AROS's loader was not pinned down.

## Pinned upstream versions

- vAmiga: commit 899da5ddd872dfb2e9e35e80247cbb2b36086524 (set in `build.sh`).
- FS-UAE (source of the AROS blob): commit f362278ccd4c60991caac3b4d240d4a3f751bea2
  (set in `fetch_aros_rom.py`).

## Open questions

- Launching the game through a Workbench icon was not tried; it is expected to use the
  same LoadSeg and so fail the same way, but that is untested.
- What exactly in AROS's loader rejects the executable was not identified.

## Verdict

Checked: the `game` entry in the autoboot copy has the same protection flags as the
original (`----rwed`, execute allowed), so this is not a protection-bit problem.
AROS boots the disk and runs the startup-sequence, but its LoadSeg refuses the game
executable with "file is not executable" (error 121).

AROS does not boot the game. It starts and reaches its shell but refuses to load
the game executable ("file is not executable"). A Kickstart 1.2 ROM is required:
place it at `disks/kick12.rom` (git-ignored; never commit it).

## Kickstart 1.2: verified 2026-10-13

With the user's Kickstart 1.2 ROM (256 KB) at `disks/kick12.rom` the game runs:

    build/vamiga-ref/ref --rom disks/kick12.rom --adf extracted/interceptor-autoboot.adf \
        --seconds 150 --out /tmp/kick12-150.ppm

- At 60 s of real time the frame still shows the AmigaDOS 1.2 shell window with the
  startup-sequence command running (no error).
- At 150 s the frame shows the game's credits screen (title, 1988 Intellisoft,
  the three credits, publisher, the any-key prompt) on a black screen. The loading
  phase is therefore between 60 and 150 s of real time on this emulator; warp mode
  is not used.
- The 912x313 frame is the super-hires texture decimated by two; colours match the
  expected blue and red title text, so the PPM byte order is right.

A Kickstart 1.3 ROM (`disks/kick13.rom`) is also available and untested; 1.2 is the
reference because the game shipped on a Workbench 1.2 disk.

Next for RE captures: drive the keyboard and mouse through the vAmiga API
(`keyboard.press`, `mouse.setXY`) from a script so the driver can get past
the any-key prompt, the main menu and into flight, and dump frames at chosen times.
