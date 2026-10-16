# hud-emu: runs the original HUD and panel routines and compares them with captures

Tools used for `docs/re/06-cockpit-hud.md` (see its "Reproducing" section). Requires `pip install unicorn Pillow`, `extracted/interceptor.bin` and
`extracted/interceptor.map.json` (tools/hunk), snapshots and frames in `extracted/captures/` (tools/vamiga-ref scripts).

- `emu3.py`: loads the 1 MB of RAM of a `.vasnap` at its real addresses into a 68020 emulator and runs a routine given by flat address; wait-for-blitter stubs
  return, the packed-decimal instructions follow the 68000 rule, blitter register writes are executed by `blit.py`.
- `blit.py`: blitter model (copy with shifts, ascending and descending, fill, line mode with the single-bit option).
- `frame.py`: the HUD and panel part of the frame schedule. `verify_hud.py`, `verify_panel.py`: compare with the openers frames and `logs/hud.txt`.
- `snap.py`, `snapmem.py`: read a `.vasnap` (chip-RAM offset, seglist head) and expose its RAM as flat memory for `emu3.py`. `logload.py`: parses the capture logs (`logs/*.txt`) used by the verify scripts.
- The addendum scripts exit 0 when every check passes and 1 otherwise, and print SKIP and exit 0 when an input is missing. Inputs (all under `extracted/`, git-ignored):

| script | what it checks | inputs |
| --- | --- | --- |
| `verify_addendum.py` | gear mask order and per-variant rows, fuel bar rows, leading digits, view code 1 fuel region, radar scope (blip shift sweep, width, pens, 10-blip cap, target blink, own-ship mark, cone lines), target box, marker slew and shapes, lock circle, range dot, the arc lists | `interceptor.bin`, `captures/cockpit-ground.vasnap`, `captures/cockpit-takeoff.ppm` (gear frame), `captures/views/c-KP6-1.vasnap` and `.ppm` (view code 1) |
| `verify_map.py` | map routine 0x2FC58 over the three tower frames, lines, labels, marks, hiding, heading-to-shape | `interceptor.bin`, `captures/views/tower-1`, `tower-2`, `tower-low` (each `.vasnap` and `.ppm`) |
| `verify_f16.py` | F-16 scale model against the routine over 2,731 states (arguments `first last [step]` for a subset) | `interceptor.bin`, `captures/cockpit-ground.vasnap` |
| `verify_views.py` | the external camera longs (`V:4ACEA`.. negated masked position, `V:4AB1A`.. equal, `V:4AB34`, words, height long), the ring offset of directions 0 and 4 against the attitude matrix and the ring tables (9 snapshots), the aim pair of ext-L-1, the map state words, the head codes' HUD step and window row; snapshot reads only | `captures/views/*.vasnap` (the `views-ground.txt` and `views-airborne.txt` outputs), `interceptor.map.json` |
| `verify_warn.py` | generator, arbiter and message driver frame by frame; code locations of the frame routine; the seeded tick sequence; the counter-against-n relation over every flight snapshot; durations and pause. Scenario names print their frame logs, `--all` prints all | `interceptor.bin`, `captures/cockpit-ground.vasnap`, every flight `.vasnap` under `captures/` and `captures/views/` (phase relation) |

- `panel_exp.py`, `util.py`, `tape_check.py`: experiments (element sweeps, the heading-tape model check).

`snap.py` hard-codes the vasnap chip-RAM offset `0x61CAEE` and the seglist head `0xC07B28`, which tie it to snapshots from this boot path (Kickstart 1.2, slow RAM); other snapshots need those two values changed.

Run from the repository root, for example `python3 tools/hud-emu/verify_hud.py`. Nothing here is part of the build or the tests.
