"""Generate the capture script of the near-ground pass, stage [90] of note 04 (routine 0x2C0EC).

    python3 tools/vamiga-ref/scripts/gen-nearground.py [PARTS]     # writes nearground-0.txt .. nearground-(PARTS-1).txt next to this file (default 1)
    python3 tools/vamiga-ref/scripts/gen-nearground.py probe [PARTS [NAMES]]   # writes nearground-probe-N.txt: the blitter trace around the alternate blit (default all states)
    python3 tools/vamiga-ref/scripts/gen-nearground.py probefill [PARTS [NAMES]]   # nearground-probefill-N.txt: the same with plane 2 filled with a pattern before the blit
    build/vamiga-ref/ref --rom disks/kick12.rom --adf extracted/interceptor.adf --script tools/vamiga-ref/scripts/nearground-0.txt \\
        --log extracted/captures/ng3/nearground-0.log        (create extracted/captures/ng3 first; the log carries the bitmaps)

Every state is a pair of runs of the same paused scene: "on" as the game draws it and "off" with the pass removed (its first instruction replaced by RTS, poke at the flat address 0x2C0EC).
The game is stopped right after the pass by a breakpoint at the instruction after the call (0x11218, the driver's `runto`), so that the planes the frame has been drawn into hold everything
up to and including stage [90] and nothing of the entities, the HUD or the panel.  A state is staged on the frozen machine as in gen-sky-ground.py (the player record's attitude words and
matrices, position and height poked, the pause byte V:4A866 set so that the flight step stops moving them), runs for 2.2 s and is frozen, is run to the breakpoint, then the pass is removed ("off") and the game run to the breakpoint once more (one whole frame); then the camera words the pass reads (V:4AB2A,
V:4AB2E, V:4AB30, V:4A923, the matrix V:4AC90, the window last row V:4AA3C), the three triangle shape tables, the fill block V:4A79E (4, 0, 0, 0xFFFF after the pass), the pointer V:4A76E to the plane table of the
page being drawn and the planes of both pages (RAM 0x4B2D8 and 0x10368, 4 x 8000 bytes each, plane 0 first) are logged.  The check is lists_model/nearground_frames_check.py.  The states come from fixed
seeds: three snapshots (carrier deck, runway, airborne), height 135 to 500 units above the ground (the pass draws triangles below 512), pitch 0 to 7,200 (nose down), random heading, position
shifted by up to 60,000 position units; the "nb" states enlarge the three triangle shapes (poke of the shape tables) so that the filler's general fill is reached often."""

import importlib.util, os, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("gsg", os.path.join(HERE, "gen-sky-ground.py"))
gsg = importlib.util.module_from_spec(spec); spec.loader.exec_module(gsg)

SNAPS = {"carrier": ("extracted/captures/cockpit-carrier.vasnap", 0x11982C00, 0x1059A000),
         "ground": ("extracted/captures/cockpit-ground.vasnap", 0x10545920, 0x10A404F0),
         "airborne": ("extracted/captures/airborne.vasnap", 0x10499990, 0x10B82CF0)}


def scene(name, snap, pitch, heading, h, dx, dz, off, big=False, probe=False):
    path, x, z = SNAPS[snap]
    s = "restore %s\nseglist extracted/interceptor.bin extracted/interceptor.map.json\nfreeze\n" % path
    s += "poke F:4B2A2 2 %d\npoke F:4B2A4 2 %d\npoke F:4B2A6 2 0\n" % (pitch, heading)
    s += gsg.poke_matrices(pitch, heading, 0)
    s += "poke F:4B250 4 %d\npoke F:4B258 4 %d\npoke F:4B254 4 %d\n" % ((x + dx) & 0xFFFFFFFF, (z + dz) & 0xFFFFFFFF, (h - 5) * 256)
    s += "poke F:4A866 1 1\n"
    if big:                                                    # enlarge the three triangle shapes of the unscaled tables so that the filler's general fill (and its blit rectangle) is reached
        for addr, v in ((0x2CDEC, (8, -8, -8, -8, 0, 6)), (0x2CE04, (24, -24, -24, -24, 0, 18)), (0x2CE1C, (60, -60, -60, -60, 0, 45))):
            for i, w in enumerate(v): s += "poke F:%X 2 %d\n" % (addr + 2 * i, w)
    s += "thaw\nwait 2.2\nfreeze\n"                           # the paused scene settles (several complete frames of the poked state)
    if probe:
        return s + probe_tail(name, probe == "fill")
    # Stop at the instruction after the call of the pass (a breakpoint, `runto`), so that the machine is not inside the pass when the
    # entry is replaced by RTS; then run one more frame to the same place and log it there.  (The first captures froze at a random
    # instant, poked a branch to itself at 0x11218 and ran 1 s: when the freeze fell inside the pass, the "off" run still drew it, and
    # its planes held the pass's own result; see note 04, "Stage [90]", the C channel.)
    s += "runto F:11218 20\n"
    if off:
        s += "poke F:2C0EC 2 0x4E75\n"
    s += "runto F:11218 20\n"
    s += "log %s\n" % name
    for lab, addr, n in (("t12", "2CDEC", 12), ("t16", "2CE04", 12), ("t20", "2CE1C", 12), ("cx", "4AB2A", 2), ("cz", "4AB2E", 2), ("hl", "4AB30", 4), ("band", "4A923", 1), ("mat", "4AC90", 18), ("lim", "4AA3C", 2)):
        s += "peek F:%s %d %s_%s\n" % (addr, n, lab, name)
    s += "peek F:4A79E 8 blk_%s\npeek F:4A76E 4 dtab_%s\npeek R:4B2D8 32000 planes0_%s\npeek R:10368 32000 planes1_%s\nthaw\n" % (name, name, name, name)
    return s


FILL = "A55AC33C0FF096"


def probe_tail(name, fill=False):
    """The C-modulus probe of note 04 ("Stage [90]", the alternate blit): run to the next entry of the pass, trace every blit of the pass
    (blittrace: the registers at each BLTSIZE write), stop right after the BLTSIZE write of the alternate blit (BLTCON0 0x0FCA), dump both
    pages, run to the BLTSIZE write of the next blit (the scratch clear, BLTCON0 0x0D0C: its routine waits for the blitter to be idle and
    writes no C pointer, so its trace line holds the alternate blit's end value of the C pointer, which gives the C modulus the blitter
    applied) and dump the pages again; then trace the rest of the pass and log the camera words."""
    s = "runto F:2C0EC 20\nblittrace on\nblitstop 0FCA\nlog %s\nrunto F:11218 20\n" % name
    if fill:                                                   # make plane 2 non-uniform (both pages) so that the C source address shows: a 7-byte pattern, so rows and words differ
        s += "pokefill R:%X 8000 %s ifblit\npokefill R:%X 8000 %s ifblit\n" % (0x4B2D8 + 16000, FILL, 0x10368 + 16000, FILL)
    s += "peek F:4A76E 4 dtab_%s-pre\npeek R:4B2D8 32000 planes0_%s-pre\npeek R:10368 32000 planes1_%s-pre\n" % (name, name, name)
    s += "peek F:4A79A 4 sbase_%s-pre\npeek R:18068 8000 scratch_%s-pre\n" % (name, name)     # the scratch plane (the blit's A source, filled); its base V:4A79A is 0x18068 in these runs
    s += "blitstop 0D0C\nrunto F:11218 20 ifblit\n"                  # the next blit, the scratch clear (0x34F92), waits for the blitter to be idle before its registers are written and sets no C pointer
    s += "peek R:4B2D8 32000 planes0_%s-post\npeek R:10368 32000 planes1_%s-post\n" % (name, name)
    s += "blitstop off\nrunto F:11218 20 ifblit\nblittrace off\n"
    for lab, addr, n in (("cx", "4AB2A", 2), ("cz", "4AB2E", 2), ("hl", "4AB30", 4), ("band", "4A923", 1), ("mat", "4AC90", 18), ("lim", "4AA3C", 2)):
        s += "peek F:%s %d %s_%s-probe\n" % (addr, n, lab, name)
    return s + "thaw\n"


def states(count=96, seed=90):
    rng = random.Random(seed)
    out = []
    for i in range(count):
        snap = rng.choice(("carrier", "ground", "ground", "airborne"))
        pitch = rng.choice((0, 1500, 2500, 3500, 5000, 7200))
        out.append(("ng%03d" % i, snap, pitch, rng.randint(0, 28799), rng.choice((135, 150, 170, 200, 260, 350, 450, 500)), rng.randint(-60000, 60000), rng.randint(-60000, 60000)))
    return out


def big_states(count=48, seed=91):
    """Enlarged triangle shapes (see scene): the carrier deck seen from just above it at heights 135 to 500, pitched down, so that the black deck (plane 2 clear) borders the sea (plane 2 set)."""
    rng = random.Random(seed)
    return [("nb%03d" % i, rng.choice(("carrier", "carrier", "ground")), rng.choice((1500, 2500, 3500, 5000)), rng.choice((0, 0, rng.randint(0, 28799))),
             rng.choice((135, 150, 170, 200, 260, 350)), rng.randint(-9000, 9000), rng.randint(-9000, 9000), True) for i in range(count)]


def probe(names, parts=1, fill=False):
    """nearground-probe-N.txt: the C-modulus probe (probe_tail) for the named states (all 144 when none are named), in PARTS files."""
    allst = states() + [s[:7] + (True,) for s in big_states()]
    st = {s[0]: s for s in allst}
    names = names or [s[0] for s in allst]
    for p in range(parts):
        with open(os.path.join(HERE, "nearground-probe%s-%d.txt" % ("fill" if fill else "", p)), "w") as f:
            f.write("# generated by gen-nearground.py probe: part %d of %d\n" % (p, parts))
            for nm in names[p::parts]:
                s = st[nm]
                f.write(scene(nm, *s[1:7], 0, len(s) > 7, probe="fill" if fill else True))


def main(parts=1):
    st = states() + [s[:7] + (True,) for s in big_states()]
    for p in range(parts):
        with open(os.path.join(HERE, "nearground-%d.txt" % p), "w") as f:
            f.write("# generated by gen-nearground.py: part %d of %d\n" % (p, parts))
            for s in st[p::parts]:
                for off in (0, 1):
                    f.write(scene(s[0] + ("-off" if off else "-on"), *s[1:7], off, len(s) > 7))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("probe", "probefill"): probe(sys.argv[3:], int(sys.argv[2]) if len(sys.argv) > 2 else 1, sys.argv[1] == "probefill")
    else: main(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
