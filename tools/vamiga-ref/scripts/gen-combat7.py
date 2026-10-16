#!/usr/bin/env python3
"""Generator of the combat7-*.txt capture scripts (milestone 7, task 3; note 07).

    python gen-combat7.py gunorder > combat7-gunorder.txt

Each part is one script that restores a snapshot, stages a run-time state with `poke` and `copy` (no image bytes are written
into a script), and logs per video frame with `step 1`. Run one script at a time under `flock /tmp/ref.lock`.
"""
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def head(snapshot="airborne"):
    return ["thaw", "restore extracted/captures/%s.vasnap" % snapshot, "seglist extracted/interceptor.bin extracted/interceptor.map.json", "freeze"]


def gunext_staging():
    """The external-view gun kill of followup-combat-batch-g.txt part gunext2: a hostile aircraft in record 8 (staged by `copy` of a
    template descriptor and run-time pokes of the record), the chase view, the target designated, the trigger held."""
    src = os.path.join(HERE, "followup-combat-batch-g.txt")
    lines, on = [], False
    for ln in open(src):
        ln = ln.rstrip("\n")
        if ln.startswith("# --- part gunext2"):
            on = True
            continue
        if on and ln.startswith("peek F:4AB98 2 g0_4AB98"):
            break
        if on and ln and not ln.startswith(("thaw", "restore", "seglist", "freeze")):
            lines.append(ln)
    return lines


def peeks(tag):
    return ["peek F:4AD2A 1280 %s_pool" % tag, "peek F:4ACF6 12 %s_cam" % tag, "peek F:4B23C 128 %s_r0" % tag, "peek F:4C23C 128 %s_r8" % tag,
            "peek F:4A83D 1 %s_view" % tag, "peek F:4A865 1 %s_4A865" % tag, "peek F:4AA78 2 %s_4AA78" % tag, "peek F:4A984 2 %s_4A984" % tag, "peek F:4AB98 2 %s_4AB98" % tag]


def gunorder(hits=0, frames=48):
    out = ["# note 07 12.5 (milestone 7 task 3): the whole tracer pool (slots 0 to 19) at a gun kill in the chase view, one video frame per sample.",
           "# The target's hit nibble (record 8 +0x3C) is poked to %d, so that the kill needs hits in %d passes and earlier tracers are in the pool when it falls;" % (hits, 3 - hits),
           "# the trigger goes down at frame 0 and frames 0 to %d are logged." % (frames - 1)] + head()
    for ln in gunext_staging():
        if ln.startswith("stepuntil"):
            break
        if ln.startswith("poke F:4C278 1 "):
            ln = "poke F:4C278 1 0x%02X" % hits
        out.append(ln)
    for f in range(0, frames):
        out += ["step 1"] + peeks("f%d" % f)
    out.append("keyup SPACE")
    return out


P0 = 0xE7   # V:4A992 at the start of airborne.vasnap


def chaffdraw(view="chase", stops=9):
    """note 07 7.7, 11.4: the chaff drawer 0x3140C is stopped at every call (`runto`), so that the camera, the view matrix, the pool and the pen word are logged at DRAW time (the earlier
    captures logged them after the pass). Each stop also writes the frame, which shows the picture of the previous stop's pass (the flip comes at the end of a pass)."""
    out = ["# note 07 7.7 / 11.4 (milestone 7 task 3): the chaff drawer stopped at each call, the draw-time camera logged; view: %s." % view] + head()
    out += (["key KPENTER 0.12"] if view == "chase" else ["key KP2 0.12"] if view == "back" else ["keydown LSHIFT", "key KPENTER 0.12", "keyup LSHIFT"] if view == "fixed" else ["key KPENTER 0.12", "step 20", "key KP8 0.12"])
    out += ["stepuntil F:4A992 2 eq %d 600" % (P0 + 40), "step 4", "colors pal", "peek F:4A83D 1 view", "frame extracted/captures/combat7/chaff-%s-pre.ppm" % view,
            "peek F:4AB1A 24 pre_cam", "peek F:4AC90 18 pre_mat", "poke F:4A943 1 1"]
    for k in range(stops):
        out += ["runto F:3140C 10", "peek F:4AD2A 640 s%d_pool" % k, "peek F:4A992 2 s%d_pass" % k, "peek F:4AB1A 24 s%d_cam" % k, "peek F:4AC90 18 s%d_mat" % k,
                "peek F:4AA04 4 s%d_cell" % k, "peek F:4A79E 6 s%d_clip" % k, "peek F:4AA0C 2 s%d_pen" % k, "peek F:4AB2A 8 s%d_camoff" % k, "peek F:4AA3C 2 s%d_lowlimit" % k,
                "frame extracted/captures/combat7/chaff-%s-%02d.ppm" % (view, k)]
    out += ["thaw", "step 1"] if False else []
    for k in range(stops, stops + 14):
        out += ["step 1", "frame extracted/captures/combat7/chaff-%s-%02d.ppm" % (view, k)]
    return out


def panelband(passes=6):
    """note 07 12.1 (open item): the four fills of rows 180 to 199 that the panel draw 0x35238 makes in an external view (pen 12, shown black in the frames). The chase view is entered
    by KP-ENTER from the cockpit (V:4A8EE = 3 for three passes); every blitter start (BLTSIZE write) of %d consecutive passes is logged by `blittrace` with the pc that wrote it, one
    `runto` of the frame routine's entry per pass; the page words and the step counter are peeked at each stop. The fills and what overwrites their rows are read from the log.""" % passes
    out = ["# note 07 12.1 (milestone 7 task 3): the blitter starts of %d passes after KP-ENTER, with the pc of each BLTSIZE write." % passes] + head()
    out += ["key KPENTER 0.12", "blittrace on"]
    for k in range(passes):
        out += ["runto F:11124 300", "peek F:4A992 2 s%d_pass" % k, "peek F:4A8EE 2 s%d_panel" % k, "peek F:4A83D 1 s%d_view" % k, "peek F:4AA3E 2 s%d_winext" % k]
    out += ["blittrace off"]
    return out


def record_staging_without_view():
    """the MiG of record 8 as followup-combat-batch-g.txt part gunext2 stages it, without its view keys and without the trigger"""
    out = []
    for ln in gunext_staging():
        if ln.startswith(("key ", "step ", "keydown", "stepuntil", "poke F:4AA78")):
            continue
        out.append(ln)
    return out


def frame_log(tag, extra=()):
    return ["step 1", "peek F:4A992 2 %s_pass" % tag, "peek F:4A961 3 %s_counts" % tag, "peek F:4C23C 128 %s_r8" % tag] + ["peek F:%s %s_%s" % (a, tag, n) for n, a in extra]


def subkill(frames=100):
    """note 07 5.3 / 08 12.9 (F6, open question C4): does the dying path add to V:4A963 for a record of the ship class 0x21 (the submarine)? Three runs from the airborne snapshot,
    record 8 staged as a MiG and put into the dying state with the counter 3: a counted aircraft (class 0x12), the ship (class 0x21) and a not-counted aircraft (W0 bit 3). V:4A963 is
    logged per video frame."""
    out = ["# note 07 5.3 (milestone 7 task 3): the kill counter V:4A963 across the expiry of a dying record: counted aircraft, ship class 0x21, not-counted aircraft."]
    for name, cls, w0 in (("counted", 0x12, 0x1540), ("ship", 0x21, 0x1540), ("notcounted", 0x12, 0x1548)):
        out += ["# --- run %s" % name] + head() + record_staging_without_view()
        out += ["poke F:4C29E 1 0x%02X" % cls, "poke F:4C23C 2 0x%04X" % w0, "poke F:4C288 2 3", "peek F:4A961 3 %s_counts0" % name]
        for f in range(frames):
            out += frame_log("%s_f%d" % (name, f))
    return out


def losttarget(frames=60):
    """note 07 4.4 (A3): a counted record whose target (+0x38) is an invalid record and whose +0x5D is a region number (positive) without an entry in its sub-list or region: the
    original loops without end. The record 8 of the staging gets W0 bit 0x0080 (AI valid), the target 1 (a record that is not valid) and +0x5D = 2; the pass counter V:4A992 is logged per
    frame: a stopped counter is the loop. A control run with +0x5D = 0 (the debug word 0x34 is stored instead) follows."""
    out = ["# note 07 4.4 (milestone 7 task 3): the lost-target rule with a region number and no entry."]
    for name, d5d in (("match", 2), ("loop", 3), ("control", 0)):
        out += ["# --- run %s" % name] + head() + record_staging_without_view()
        out += ["poke F:4C23C 2 0x11C0", "poke F:4C274 1 0x01", "poke F:4C299 1 %d" % d5d, "peek F:4B43C 2 %s_target_w0" % name]
        for f in range(frames):
            out += frame_log("%s_f%d" % (name, f), (("dbg", "4A93C 8"),))
    return out


def placer15(frames=200):
    """note 07 2.1 step 5 (A2): the entry placer does not overwrite a record of class 0x15 whose cell x word is non-zero, even when its valid bit is clear. Region 3 (cells 75 to 77) has
    one entry, a class 0x15 for slot 4, filtered to the mode 8; the F1 flight snapshot is run with the mode byte poked to 8, the hostile cap above the count, the player moved into
    the region and record 4 staged: class 0x15, cell x 1, W0 clear (run kept) or class 0x00, cell x 0 (control: it is placed). Record 4 is logged per frame for the tick (V:4A992 low nibble 3)."""
    out = ["# note 07 2.1 step 5 (milestone 7 task 3): the placer and a class 0x15 record with a non-zero cell x."]
    for name, cls, cx in (("kept", 0x15, 1), ("control", 0x00, 0)):
        out += ["# --- run %s" % name] + head("mb/m3-flight")
        out += ["poke F:4A95E 1 8", "poke F:4A962 1 9", "poke F:4A961 1 0",
                "poke F:4BA3C 2 0x0000", "poke F:4BA9E 1 0x%02X" % cls, "poke F:4BA42 2 %d" % cx,
                "poke F:4B242 2 76", "poke F:4B244 2 76",
                "poke F:4B250 4 0x13000000", "poke F:4B258 4 0x13000000"]
        for f in range(frames):
            out += ["step 1", "peek F:4A992 2 %s_f%d_pass" % (name, f), "peek F:4BA3C 128 %s_f%d_r4" % (name, f), "peek F:4A855 1 %s_f%d_regionbits" % (name, f), "peek F:4A961 3 %s_f%d_counts" % (name, f),
                    "peek F:2600C 100 %s_f%d_desc" % (name, f)]
    return out


def retarget(path=None):
    """note 07 8.3 / 8.1 step 6 (A2): the player's missile follows the designation V:4AA78 every pass. The capture followup-combat-kills-missile.txt (generator gen-followup-combat.py
    missile) is replayed with the designation poked to 0xFFFF (dropped) in samples 5 to 8 and back to the target in samples 9 to 12; the missile's record 1 is logged per sample."""
    import re
    src = os.path.join(HERE, "followup-combat-kills-missile.txt")
    out = ["# note 07 8.3 (milestone 7 task 3): the live retarget of the player's missile, the designation dropped and set again in flight."]
    for ln in open(src):
        ln = ln.rstrip("\n")
        m = re.match(r"peek F:4B23C 128 s(\d+)_r0$", ln)
        if m:
            k = int(m.group(1))
            if 5 <= k <= 8:
                out.append("poke F:4AA78 2 0xFFFF")
            elif 9 <= k <= 12:
                out.append("poke F:4AA78 2 0x1000")
        out.append(ln)
        if m and int(m.group(1)) >= 40:
            break
    return out


def pass93trace(stack=None):
    """note 07 12.1 (open item): which code writes the first non-zero V:4A8EE of a free flight (V:4A992 = 93 in the capture of followup-combat-batch-b.txt part firstpass)? The machine is
    run to the pass before (V:4A992 = 92) and stopped at the entry of the redraw helper 0x1EFF4 (the store of 3 into the fourteen redraw bytes); with `ref -v` the registers at the end
    of the script are printed, so that the stack pointer is known; a second script (`stack` = the stack pointer) peeks the return address."""
    out = ["# note 07 12.1 (milestone 7 task 3): the first call of the redraw helper 0x1EFF4 after V:4A992 = 92 in a free flight from the aircraft screen."] + head("aircraft-select")
    out += ["key 1 0.12", "stepuntil F:4A992 2 eq 92 3000", "peek F:4A8EE 2 before_panel", "peek F:1A6BC 4 before_state", "peek F:4A966 1 before_cammode", "runto F:1EFF4 40", "peek F:4A8EE 2 at_panel",
            "peek F:1A6BC 4 at_state", "peek F:4A992 2 at_pass"]
    if stack:
        out += ["peek R:%X 32 stack" % stack]
    return out


def pass93site():
    """note 07 12.1 (open item): the run of pass93trace stopped at the redraw helper 0x1EFF4 in the pass V:4A992 = 93 with the camera mode V:4A966 = 6 (the approach camera of the free flight's
    start). The writer 3 of 12.1 is the window-row change at 0x14388 (needs camera mode 6 and the camera height V:4ACFA below 0x24000). This script stops at the call site itself and logs the
    height, the window row V:4AA3C, the mode and the panel counter before and after the stop."""
    out = ["# note 07 12.1 (milestone 7 task 3): the call site 0x14388 of the redraw helper in the approach of a free flight."] + head("aircraft-select")
    out += ["key 1 0.12", "stepuntil F:4A992 2 eq 92 3000", "peek F:4ACFA 4 before_height", "peek F:4AA3C 2 before_row", "peek F:4A966 1 before_cammode", "peek F:4A8EE 2 before_panel",
            "runto F:14388 40", "peek F:4A992 2 at_pass", "peek F:4ACFA 4 at_height", "peek F:4AA3C 2 at_row", "peek F:4A966 1 at_cammode", "peek F:4A8EE 2 at_panel",
            "runto F:1EFF4 20", "peek F:4A992 2 helper_pass", "peek F:4A8EE 2 helper_panel", "step 1", "peek F:4A8EE 2 after_panel", "peek F:4A8F1 8 after_redraw_bytes"]
    return out


def chaffheadorder(stops=6):
    """note 07 7.7 / 12.5: the order of the chaff drawer 0x3140C and the entity drawer 0x21C44 inside a pass, in the head-on external view of chaffdrawhead. After each stop at the chaff
    drawer the next stop is the entity drawer: the same pass number means the gun ran first, the next one means the entity pass came first. Also logs the pass of the stop and the pool."""
    out = ["# note 07 7.7 / 12.5 (milestone 7 task 3): the chaff drawer and the entity drawer, which first within a pass (head-on external view)."] + head()
    out += ["key KPENTER 0.12", "step 20", "key KP8 0.12", "stepuntil F:4A992 2 eq %d 600" % (P0 + 40), "step 4", "poke F:4A943 1 1"]
    for k in range(stops):
        out += ["runto F:3140C 10", "peek F:4A992 2 c%d_pass" % k, "peek F:4AD2A 64 c%d_slot0" % k, "runto F:21C44 10", "peek F:4A992 2 e%d_pass" % k, "peek F:4AA6E 2 e%d_rec" % k]
    return out


def panelmem(passes=6):
    """note 07 12.1 (open item, second half): the blit trace of panelband shows the four fills of the panel draw (pen 12) as the last blits of a pass, in rows 168 to 187 of the page being drawn
    (V:4AA3C = 0xA7 plus 20 rows), page A at 0x10390 + 0x1A18 and page B at 0x4B300 + 0x1A18 for the lowest plane (the origins of the draw table are 0x28 lower) (planes 0x1F40 apart). Without blit tracing, the machine is stopped at the entry of the
    frame routine in each pass after KP-ENTER and the 20 rows of the four planes of both pages are peeked together with the frame: is the fill in the memory that the picture shows?"""
    out = ["# note 07 12.1 (milestone 7 task 3): the band rows of both pages and the frame, per pass after KP-ENTER."] + head()
    out += ["key KPENTER 0.12"]
    for k in range(passes):
        out += ["runto F:11124 20", "peek F:4A992 2 s%d_pass" % k, "peek F:4A8EE 2 s%d_panel" % k, "peek F:4AA3C 2 s%d_row" % k, "frame extracted/captures/combat7/panel-%02d.ppm" % k]
        for page, base in (("a", 0x10390), ("b", 0x4B300)):
            for plane in range(4):
                out.append("peek R:%X 800 s%d_%s%d" % (base + plane * 0x1F40 + 0x1A18, k, page, plane))
    return out


def chaffpix():
    """note 07 7.7: is the chaff's plain pixel (pen 13) written into the bitplanes in the head-on external view of chaffdrawhead? The creation pass (V:4A992 low byte 0x0F of the stop)
    draws it at game pixel (157, 86), the next at (157, 83) (the rig's replay of the draw-time state); in byte terms row * 40 + 19, bit 2. The bytes at x 152 to 167 of those rows are peeked
    in the four planes of both pages (page A planes at 0x10368, page B at 0x4B2D8, 0x1F40 apart, the origin the draw table V:4A76E names) before the drawer runs (stop at 0x3140C), at the next frame routine entry and at
    the one after; the frames follow."""
    out = ["# note 07 7.7 (milestone 7 task 3): the chaff pixel in the bitplanes, head-on external view."] + head()
    out += ["key KPENTER 0.12", "step 20", "key KP8 0.12", "stepuntil F:4A992 2 eq %d 600" % (P0 + 40), "step 4", "poke F:4A943 1 1"]

    def planes(tag):
        o = []
        for page, base in (("a", 0x10368), ("b", 0x4B2D8)):   # the planes of the draw table V:4A76E (page A: 0x10368 + k * 0x1F40), one row above the first clear of the window
            for plane in range(4):
                for row in (85, 86, 87, 82, 83, 84):
                    o.append("peek R:%X 4 %s_%s%d_r%d" % (base + plane * 0x1F40 + row * 40 + 18, tag, page, plane, row))
        return o

    out += ["runto F:3140C 10", "peek F:4A992 2 x0_pass"] + planes("x0")
    out += ["runto F:11124 20", "peek F:4A992 2 x1_pass"] + planes("x1") + ["frame extracted/captures/combat7/pix-1.ppm"]
    out += ["runto F:3140C 10", "peek F:4A992 2 x2_pass"] + planes("x2")
    out += ["runto F:11124 20", "peek F:4A992 2 x3_pass"] + planes("x3") + ["frame extracted/captures/combat7/pix-3.ppm"]
    out += ["runto F:11124 20", "peek F:4A992 2 x4_pass"] + planes("x4") + ["frame extracted/captures/combat7/pix-4.ppm"]
    return out


def chaffptr(table=0xC4227E):
    """note 07 7.7: the plane pointers the point routine writes through. At the chaff drawer's stop of the creation pass (head-on external view) the pointer V:4A76E, the plane-mask
    byte V:4A79F and the pen word are peeked; with `table` (the pointer read from the first run) the four plane pointers it names and the rows around game pixel (157, 86) in the planes
    they point to follow, then the machine runs to the end of the pass and the same rows are read again."""
    out = ["# note 07 7.7 (milestone 7 task 3): the plane pointers of the point routine at the chaff drawer."] + head()
    out += ["key KPENTER 0.12", "step 20", "key KP8 0.12", "stepuntil F:4A992 2 eq %d 600" % (P0 + 40), "step 4", "poke F:4A943 1 1", "runto F:3140C 10", "peek F:4A992 2 y_pass",
            "peek F:4A76E 4 y_ptr", "peek F:4A79E 6 y_clip", "peek F:4AA0C 2 y_pen"]
    if table:
        out += ["peek R:%X 16 y_table" % table]
    return out


PARTS = {"gunorder": gunorder, "gunorder1": lambda: gunorder(2, 19), "chaffdraw": chaffdraw, "chaffdrawback": lambda: chaffdraw("back"), "chaffdrawhead": lambda: chaffdraw("head"), "chaffdrawfixed": lambda: chaffdraw("fixed", 16), "panelband": panelband, "subkill": subkill, "losttarget": losttarget, "placer15": placer15, "retarget": retarget, "pass93trace": pass93trace, "pass93site": pass93site, "chaffheadorder": chaffheadorder, "panelmem": panelmem, "chaffpix": chaffpix, "chaffptr": chaffptr}

if __name__ == "__main__":
    print("\n".join(PARTS[sys.argv[1]]()))
