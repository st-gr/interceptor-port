#!/usr/bin/env python3
"""Generate the sky/ground capture scripts of note 04 ("Sky fill and ground polygon").

    python3 tools/vamiga-ref/scripts/gen-sky-ground.py            # writes sky-ground-{attitude,height,sea,blackout}.txt next to this file

Each state is staged on the frozen machine: the player record's attitude words (+0x66 pitch, +0x68 heading, +0x6A roll) and
height (+0x18, long) are poked, the pause byte V:4A866 is set so that the flight step stops moving them, the machine runs
for 2.2 s (several draw passes of the same state; a complex scene needs more than one second to reach the display), is frozen again, and the state block (V:4A700, 2336 bytes: camera
matrix, camera words, flags), the colour registers and the frame are logged.  Run from the repository root:

    build/vamiga-ref/ref --rom disks/kick12.rom --adf extracted/interceptor.adf \\
        --script tools/vamiga-ref/scripts/sky-ground-attitude.txt --log extracted/captures/logs/sky-ground-attitude.txt
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
HEAD = ("restore extracted/captures/airborne.vasnap\n"
        "seglist extracted/interceptor.bin extracted/interceptor.map.json\n")


def state(name, pitch, roll, heading, y, view_key=None):
    s = "freeze\n"
    s += "poke F:4B2A2 2 %d\n" % pitch          # record 0 +0x66, 28800 units per circle, nose up is negative
    s += "poke F:4B2A4 2 %d\n" % heading        # +0x68
    s += "poke F:4B2A6 2 %d\n" % roll           # +0x6A
    if y is not None:
        s += "poke F:4B254 4 %d\n" % y          # +0x18 height in position units
    s += "poke F:4A866 1 1\n"                   # pause: the step leaves the attitude alone
    s += "thaw\nwait 2.2\nfreeze\n"
    s += "log %s\n" % name
    s += "peek F:4A700 2336 s71_%s\n" % name
    s += "peek F:4B23C 512 rec0_%s\n" % name
    s += "colors pal_%s\n" % name
    s += "frame extracted/captures/sky/%s.ppm\n" % name
    s += "thaw\n"
    return s


def attitude_matrices(pitch, heading, roll):
    """The record's attitude matrices (A at +0x80 and B at +0x92, Q14, nine words each) for the three angle words (28800 per circle): a Python port of the C port's
    `fx_matrix_a` and `fx_matrix_b` (sine and cosine of angle/8 tenths of a degree as 16384 x sin rounded; B takes the negated angles, 0 staying 0). Checked against the C
    port for 2,000 random angle triples. The flight step derives the angle words from the matrix, so an attitude poked into the words alone survives only in a paused game,
    and even there a step that runs between the thaw and the pause rebuilds the words from the matrix (5 of the 40 first mesh-c states came back with pitch 0): the
    scripts poke the matrices with the words."""
    import math

    def sc(a):
        t = (a >> 3) % 3600
        return round(16384 * math.sin(t * math.pi / 1800)), round(16384 * math.cos(t * math.pi / 1800))

    def w16(v):
        v &= 0xFFFF
        return v - 65536 if v & 0x8000 else v

    def mx(p, q):
        return w16((p * q) >> 14)

    def neg(a):
        return 28800 - a if a else 0

    sA, cA = sc(pitch); sB, cB = sc(heading); sC, cC = sc(roll)
    sBsA, cBsA = mx(sB, sA), mx(cB, sA)
    A = [w16(((sBsA * sC) + cB * cC) >> 14), w16(-mx(cA, sC)), w16((sB * cC - cBsA * sC) >> 14),
         w16((cB * sC - sBsA * cC) >> 14), mx(cA, cC), w16((cBsA * cC + sB * sC) >> 14),
         w16(-mx(sB, cA)), w16(-sA), mx(cB, cA)]
    sA, cA = sc(neg(pitch)); sB, cB = sc(neg(heading)); sC, cC = sc(neg(roll))
    sCsA, cCsA = mx(sC, sA), mx(cC, sA)
    B = [w16((cC * cB - sCsA * sB) >> 14), w16(-w16((sC * cB + cCsA * sB) >> 14)), mx(cA, sB),
         mx(sC, cA), mx(cC, cA), w16(sA),
         w16(-w16((cC * sB + sCsA * cB) >> 14)), w16((sC * sB - cCsA * cB) >> 14), mx(cA, cB)]
    return A, B


def poke_matrices(pitch, heading, roll):
    A, B = attitude_matrices(pitch, heading, roll)
    s = ""
    for base, M in ((0x4B2BC, A), (0x4B2CE, B)):
        for i, v in enumerate(M):
            s += "poke F:%X 2 %d\n" % (base + 2 * i, v)
    return s


def yaw_matrices(heading):
    return attitude_matrices(0, heading, 0)


def chk(name):
    """Peeks just before the frame, so that the log shows what the machine held when the frame was taken: the record's pitch, heading, roll, the pause byte, the octant and the heading words."""
    return ("peek F:4B2A2 6 chk_att_%s\npeek F:4A866 1 chk_pause_%s\npeek F:4A90C 1 chk_oct_%s\npeek F:4AB42 8 chk_words_%s\n" % (name, name, name, name))


def sea_state(name, heading, y, cx, cz, px, pz):
    """Stage the player inside the sea zone (cell coordinates cx, cz; position longs px, pz) at a heading, force a list refresh."""
    s = "freeze\n"
    s += "poke F:4B242 2 %d\npoke F:4B244 2 %d\n" % (cx, cz)          # record +0x06, +0x08: cell words
    s += "poke F:4B250 4 %d\npoke F:4B258 4 %d\n" % (px, pz)          # +0x14 x, +0x1C z
    s += "poke F:4B254 4 %d\n" % y                                      # +0x18 height
    s += "poke F:4B2A2 2 0\npoke F:4B2A4 2 %d\npoke F:4B2A6 2 0\n" % heading
    s += "poke F:4A910 1 0xFF\npoke F:4A866 1 1\n"
    s += "thaw\nwait 2.5\nfreeze\n"
    s += "log %s\n" % name
    s += "peek F:4A700 2336 s71_%s\n" % name
    s += "peek F:4B23C 512 rec0_%s\n" % name
    s += "frame extracted/captures/sky/%s.ppm\n" % name
    s += "thaw\n"
    return s


def write(fn, header, states):
    with open(os.path.join(HERE, fn), "w") as f:
        f.write("# generated by gen-sky-ground.py: %s\n" % header)
        f.write(HEAD)
        for st in states:
            f.write(state(*st))


def wrap(v):
    return v % 28800


def main():
    # Attitude grid at the airborne snapshot's height and place: level, pitched, banked, inverted, dive (28800 units per circle).
    A = []
    for p in (0, -400, -800, -1600, 400, 800, 1600, 3200, 7200, -3200, -7200):
        A.append(("a-p%+d-r0" % p, wrap(p), 0, 4000, None))
    for r in (200, 400, 800, 1600, 2400, 3600, 5000, 7200, 9000, 10800, 14400, -800, -1600, -3600, -7200):
        A.append(("a-p0-r%+d" % r, 0, wrap(r), 4000, None))
    for p, r in ((-800, 1600), (800, 1600), (-1600, 3600), (1600, -3600), (3000, 14400), (-3000, 14400), (10800, 0), (10800, 14400),
                 (18000, 0), (21600, 0), (21600, 14400), (14400, 0), (-5400, 3600)):
        A.append(("a-p%+d-r%+d" % (p, r), wrap(p), wrap(r), 4000, None))
    for h in (0, 1000, 7200, 15000, 22000):
        A.append(("a-h%d" % h, 0, 800, h, None))
    write("sky-ground-attitude.txt", "attitude grid, forward cockpit view", A)
    # Height: level and banked at six heights (the record y, position units; 204.8 units per foot).
    B = []
    for y in (30472, 60000, 150000, 575057, 2000000, 5000000, 8388608):
        B.append(("b-y%d-r0" % y, 0, 0, 4000, y))
        B.append(("b-y%d-r1600" % y, wrap(-400), 1600, 4000, y))
    write("sky-ground-height.txt", "height series", B)


def main_blackout():
    """The fade: V:4A941 (the strain accumulator, -1 when idle) is poked to 15; the main state routine sets the fade target V:4A959 to 2
    while it is above 5 (cockpit, own aircraft) and the vertical-blank server steps the level V:4A958. Sampled every 0.2 s, then every second."""
    out = ["# generated by gen-sky-ground.py: blackout fade (strain accumulator poked to 15; V:4A940 latch, V:4A941 accumulator, V:4A958 level, V:4A959 target, V:4A95B step timer)",
           "restore extracted/captures/airborne.vasnap", "seglist extracted/interceptor.bin extracted/interceptor.map.json",
           "freeze", "poke F:4A941 1 15", "thaw"]
    for i in range(75):
        out.append("wait 0.2" if i < 45 else "wait 1.0")
        out += ["freeze", "log s%02d" % i, "peek F:4A940 2 l940_s%02d" % i, "peek F:4A958 4 l958_s%02d" % i, "peek F:4A992 2 n_s%02d" % i,
                "colors pal_s%02d" % i, "thaw"]
    with open(os.path.join(HERE, "sky-ground-blackout.txt"), "w") as f:
        f.write("\n".join(out) + "\n")


def main_sea():
    # The sea zone: cells x 64..83, z 48..79 (V:4AA00/4AA02 are the cell words divided by 4); the carrier's start cell is (70, 65).
    with open(os.path.join(HERE, "sky-ground-sea.txt"), "w") as f:
        f.write("# generated by gen-sky-ground.py: base ground pen over the sea zone, eight view octants at two heights\n")
        f.write(HEAD)
        for y in (60000, 600000):
            for k in range(8):
                f.write(sea_state("c-y%d-o%d" % (y, k), k * 3600 + 1800, y, 70, 65, 295174144, 274202624))


def mesh_state(name, heading, pitch, y, x, z, zoom_keys=(), settle=5, matrices=False):
    """Stage the player at the world position (x, z: position longs, whole world 0..2^29) and height y (record +0x18), at a heading and pitch, with zoom key presses first;
    a list refresh is forced. Logs the state block, the record and the frame (the sea mesh pass of note 04 is compared with it)."""
    s = "freeze\n"
    for k in zoom_keys:                                                             # the key handler runs only unpaused; `]` halves the factor, `[` doubles it
        s += "poke F:4A866 1 0\nthaw\nkey %s\nwait 0.8\nfreeze\n" % k
    s += "poke F:4B242 2 %d\npoke F:4B244 2 %d\n" % (x >> 22, z >> 22)      # record +0x06, +0x08: cell words
    s += "poke F:4B250 4 %d\npoke F:4B258 4 %d\n" % (x, z)                   # +0x14 x, +0x1C z
    s += "poke F:4B254 4 %d\n" % y
    s += "poke F:4B2A2 2 %d\npoke F:4B2A4 2 %d\npoke F:4B2A6 2 0\n" % (pitch % 28800, heading)
    if matrices:                                                                # a step between the thaw and the pause rebuilds the words from the matrix (state md4 of the first run)
        s += poke_matrices(pitch % 28800, heading, 0)
    s += "poke F:4A910 1 0xFF\npoke F:4A866 1 1\n"
    s += "thaw\nwait %s\nfreeze\n" % settle
    s += "log %s\n" % name
    s += "peek F:4A700 2336 s71_%s\n" % name
    s += "peek F:4B23C 512 rec0_%s\n" % name
    s += "frame extracted/captures/sky/%s.ppm\n" % name
    s += "thaw\n"
    return s


def main_mesh():
    """The sea mesh pass (stage [68]): 2 x 36 states over the whole map (fine cells with sea, coast and land around), heights from 1,000 to 8,000,000 position units,
    eight headings, a few zoomed (the `[` key); scripts sky-ground-mesh-a.txt and -b.txt (run them in parallel)."""
    import random
    rnd = random.Random(20261013)
    heights = [1000, 5000, 20000, 60000, 150000, 400000, 900000, 2000000, 4000000, 8000000]
    for tag in "ab":
        with open(os.path.join(HERE, "sky-ground-mesh-%s.txt" % tag), "w") as f:
            f.write("# generated by gen-sky-ground.py: sea mesh pass at many places and heights\n" + HEAD)
            for i in range(36):
                cx, cz = rnd.randint(6, 28), rnd.randint(2, 28)
                x, z = (cx << 24) + rnd.randint(0, (1 << 24) - 1), (cz << 24) + rnd.randint(0, (1 << 24) - 1)
                y = rnd.choice(heights) + rnd.randint(0, 900)
                zk = ("RBRACKET",) if (i % 9 == 8) else ()                          # `]` halves the zoom factor (0x80 to 0x40)
                f.write(mesh_state("m%s%02d" % (tag, i), rnd.randint(0, 28799), rnd.choice([0, 0, 800, 1600, 2400, 3200]), y, x, z, zk, settle=5))
                if zk:                                                              # `[` doubles it back
                    f.write("freeze\npoke F:4A866 1 0\nthaw\nkey LBRACKET\nwait 0.8\n")


def view_state(name, heading, pitch, y, x, z, keys=(), settle=4.0, undo=(), climb=False):
    """Stage the position with the game running, press keys (external view KPENTER, map view M), then pause the game and poke the attitude and the position again, wait, freeze,
    log the state block, the record and the frame; `undo` keys follow. The first phase runs unpaused (the key handlers and the flag refresh of V:4A953 and V:4A954 need running
    frames), so the aircraft is first given the attitude matrices of a level flight at the wanted heading (`yaw_matrices`, record +0x80 and +0x92, with the angle words):
    the step rebuilds the angle words from the matrix, and the first version of this script poked the words only, so that in the mesh-c and mesh-e captures the game flew the snapshot's own
    attitude (pitch 26360, heading 2256, octant 0) and the final poke of the words, which only a paused game keeps, was undone by the unpaused first phase for the
    external camera, whose heading word is written only by unpaused frames. The pitch is poked after the pause (the cockpit camera follows the words in a paused game)."""
    s = "freeze\npoke F:4A866 1 0\n"
    s += "poke F:4B242 2 %d\npoke F:4B244 2 %d\n" % (x >> 22, z >> 22)
    s += "poke F:4B2A2 2 0\npoke F:4B2A4 2 %d\npoke F:4B2A6 2 0\n" % heading
    p1 = 28800 - 600 if climb else 0                                           # a gentle climb for the low states: level flight at 20,000 units hit the ground in two of the first mesh-e states
    s += (("poke F:4B2A2 2 %d\n" % p1) if climb else "") + poke_matrices(p1, heading, 0)
    s += "poke F:4B250 4 %d\npoke F:4B258 4 %d\npoke F:4B254 4 %d\nthaw\nwait 0.5\n" % (x, z, y)
    for k in keys:
        s += "key %s\nwait 0.8\n" % k
    s += "wait 1.5\nfreeze\n"
    s += "poke F:4B2A2 2 %d\npoke F:4B2A4 2 %d\npoke F:4B2A6 2 0\n" % (pitch % 28800, heading)
    s += poke_matrices(pitch % 28800, heading, 0)
    s += "poke F:4B250 4 %d\npoke F:4B258 4 %d\npoke F:4B254 4 %d\n" % (x, z, y)
    s += "poke F:4A910 1 0xFF\npoke F:4A866 1 1\nthaw\nwait %s\nfreeze\n" % settle
    s += "log %s\n" % name
    s += "peek F:4A700 2336 s71_%s\n" % name
    s += "peek F:4B23C 512 rec0_%s\n" % name
    s += chk(name)
    s += "frame extracted/captures/sky/%s.ppm\n" % name
    s += "poke F:4A866 1 0\nthaw\n"
    for k in undo:
        s += "key %s\nwait 0.8\n" % k
    return s


def stale_script(fn, header, keys, steps=400, level=True, extra=True):
    """Video-frame sampling of the progress marker V:4AB8C, the polygon colour V:4AA0C, the plane-mask block V:4A79E..4A7A5, the frame counter, the pitch words V:4AB42 and
    V:4AB4C and the whole-window flag V:4A956, one `step` at a time in free flight (unpaused). With `level` the aircraft is first given the attitude matrices of a level flight
    (pitch 0, roll 0, heading 0) at 600,000 position units so that the horizon is on the screen and the whole-window row of the sky polygon (note 04) is not taken."""
    out = ["# " + header, "restore extracted/captures/airborne.vasnap", "seglist extracted/interceptor.bin extracted/interceptor.map.json", "freeze"]
    if level:
        out += ["poke F:4B2A2 2 0", "poke F:4B2A4 2 0", "poke F:4B2A6 2 0", "poke F:4B254 4 600000"]
        out += poke_matrices(0, 0, 0).strip().split("\n")
    out.append("thaw")
    out.append("wait 1.5")
    for k in keys:
        out += ["key %s" % k, "wait 2"]
    out.append("freeze")
    for i in range(steps):
        out += ["step", "peek F:4AB8C 2 k%04d" % i, "peek F:4AA0C 2 c%04d" % i, "peek F:4A79E 8 m%04d" % i, "peek F:4A992 2 n%04d" % i]
        if extra:             # (the driver's log table holds about 8,000 entries: a 1,200-step run takes the four peeks only)
            out += ["peek F:4AB42 2 p%04d" % i, "peek F:4AB4C 2 q%04d" % i, "peek F:4A956 1 f%04d" % i]
    with open(os.path.join(HERE, fn), "w") as f:
        f.write("\n".join(out) + "\n")


def main_stale():
    """The stale colour and mask at the sky fill: level cockpit flight, map view, a cockpit view with the HUD off (rear, KP2)."""
    stale_script("sky-ground-stale-colour-level.txt", "stale colour and mask in level cockpit flight (horizon on the screen)", ())
    stale_script("sky-ground-stale-colour-external.txt", "stale colour and mask in the external view (KPENTER, the snapshot's own flight), with the pitch words and V:4A956", ("KPENTER",), steps=700, level=False)
    stale_script("sky-ground-stale-colour-map.txt", "stale colour and mask in the map view (M)", ("M",))
    stale_script("sky-ground-stale-colour-hudoff.txt", "stale colour and mask in the rear cockpit view (KP2, HUD off screen)", ("KP2",), steps=1200, extra=False)


def main_retakes():
    """The three states that were re-shot (scripts sky-ground-mesh-r.txt and -r2.txt): ma12 and mb31 were stale frames at the 5 s settle and are exact at 6 s (ma12r, mb31r);
    mb17 (5.6 ft above the ground) had a frame and a block that disagree at any settle and was re-staged at 6,000 position units (mb17r)."""
    with open(os.path.join(HERE, "sky-ground-mesh-r.txt"), "w") as f:
        f.write("# generated by gen-sky-ground.py: retakes of mesh states ma12 and mb31 with a settle of 6 s\n" + HEAD)
        f.write(mesh_state("ma12r", 19237, 800, 2000655, 147358230, 322737185, settle=6))
        f.write(mesh_state("mb31r", 313, 1600, 8000537, 380812747, 453768555, settle=6))
    with open(os.path.join(HERE, "sky-ground-mesh-r3.txt"), "w") as f:
        f.write("# generated by gen-sky-ground.py: retake of mesh state mc12 (a torn camera matrix in the first shot: the frozen machine had stopped between the matrix build and the row scale)\n" + HEAD)
        f.write(view_state("mc12r", *MC_POS["mc12"], (), settle=4.0))
    with open(os.path.join(HERE, "sky-ground-mesh-r2.txt"), "w") as f:
        f.write("# generated by gen-sky-ground.py: retake of mesh state mb17 at 6,000 position units, settle 6 s\n" + HEAD)
        f.write(mesh_state("mb17r", 1959, 3200, 6000, 357915348, 351536186, settle=6))


MC_POS = {}      # name -> (heading, pitch, y, x, z) of the mesh-c states, for the retake of a torn one


def main_mesh_c():
    """Mesh pass cases the random places do not reach: the second sea-zone flag V:4A954 (cell 16, 17), the zoomed lists above altitude 0x9000, the external view and the map view
    (scripts sky-ground-mesh-c.txt, -d.txt, -e.txt)."""
    import random
    rnd = random.Random(7)
    MC_POS.clear()
    with open(os.path.join(HERE, "sky-ground-mesh-c.txt"), "w") as f:
        f.write("# generated by gen-sky-ground.py: V:4A954 cell (16, 17), headings in all eight octants, heights 20,000 to 900,000\n" + HEAD)
        for yi, y in enumerate((20000, 100000, 300000, 600000, 900000)):
            for o in range(8):
                x = (16 << 24) + (3 << 22) + rnd.randint(0, (1 << 22) - 1)       # phase 12: quadrant bits x = 3, z = 0
                z = (17 << 24) + rnd.randint(0, (1 << 22) - 1)
                f.write(view_state("mc%d%d" % (yi, o), o * 3600 + 1800, 1600, y, x, z, (), settle=4.0))
                MC_POS["mc%d%d" % (yi, o)] = (o * 3600 + 1800, 1600, y, x, z)
    with open(os.path.join(HERE, "sky-ground-mesh-d.txt"), "w") as f:
        f.write("# generated by gen-sky-ground.py: zoom 0x20 and 0x40 at height (the scan lists of altitude above 0x9000)\n" + HEAD)
        for i, (y, nk) in enumerate(((1500000, 2), (2500000, 2), (5000000, 2), (8000000, 2), (4800000, 1), (8000000, 1), (3000000, 2), (6500000, 2))):
            cx, cz = rnd.randint(8, 24), rnd.randint(6, 24)
            x, z = (cx << 24) + rnd.randint(0, (1 << 24) - 1), (cz << 24) + rnd.randint(0, (1 << 24) - 1)
            f.write(mesh_state("md%d" % i, rnd.randint(0, 28799), 3200, y, x, z, ("RBRACKET",) * nk, settle=5, matrices=True))
            f.write("freeze\npoke F:4A866 1 0\nthaw\n" + "key LBRACKET\nwait 0.8\n" * nk)       # back to zoom 0x80
    with open(os.path.join(HERE, "sky-ground-mesh-e.txt"), "w") as f:
        f.write("# generated by gen-sky-ground.py: the external view (KPENTER) and the map view (M) at several places\n" + HEAD)
        for i in range(8):
            cx, cz = rnd.randint(8, 24), rnd.randint(6, 24)
            x, z = (cx << 24) + rnd.randint(0, (1 << 24) - 1), (cz << 24) + rnd.randint(0, (1 << 24) - 1)
            y = rnd.choice((20000, 150000, 600000, 2000000))
            f.write(view_state("me%d" % i, rnd.randint(0, 28799), rnd.choice((0, 1600, 3200)), y, x, z, ("KPENTER",), undo=("KPENTER",), climb=y < 100000))
        for i in range(6):
            cx, cz = rnd.randint(8, 24), rnd.randint(6, 24)
            x, z = (cx << 24) + rnd.randint(0, (1 << 24) - 1), (cz << 24) + rnd.randint(0, (1 << 24) - 1)
            y = rnd.choice((20000, 150000, 600000, 2000000))
            f.write(view_state("mf%d" % i, rnd.randint(0, 28799), 1600, y, x, z, ("M",), undo=("M",), climb=y < 100000))


if __name__ == "__main__":
    main()
    main_sea()
    main_blackout()
    main_mesh()
    main_mesh_c()
    main_retakes()
    main_stale()
