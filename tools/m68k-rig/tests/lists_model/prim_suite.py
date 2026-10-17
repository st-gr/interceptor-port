"""The primitive-layer checks of docs/re/04a-display-lists.md section 8c and docs/re/03-3d-pipeline.md (draw operations), run as one suite.

    python3 prim_suite.py [SEED] [SCALE]        or    python3 walk_check.py primitives [SEED] [SCALE]

SCALE 1000 is the size cited in the notes (about ten minutes); the default 60 is the quick run of `make test`.  Every script is also runnable
alone (`python3 SCRIPT.py SEED N`), reads the user's git-ignored extracted/interceptor.bin and tools/m68k-rig/librig.so, and skips with exit 0
when one is missing.  Exit 1 when any script reports a difference.
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
# (script, extra leading arguments, count at SCALE 1000, minimum count, what it checks)
SUITE = [
    ("prim_ops.py", [], 1000, 20, "return word and condition codes of every operation handler (direct calls)"),
    ("prim_walk.py", ["primitives"], 8000, 400, "stored +0x14 against the combination of the handler results (rule 9, stream ends, type B abort)"),
    ("prim_walk.py", ["vertices"], 8000, 400, "vertex table after the draw against the model: first vertices, flat second section, strips"),
    ("prim_walk.py", ["divergences"], 1500, 100, "the two first divergences of the code round (op 48 whole-line rule; flat second section)"),
    ("prim_walk.py", ["typeb"], 400, 40, "rule 4B: type B level search at both sides of every threshold"),
    ("ops_tri.py", [], 800, 20, "ops 7 and 8: vertex choice and return"),
    ("ops_pen.py", [], 600, 20, "ops 39 and 40: vertex variant, pen half, second polygon, return"),
    ("ops_points.py", [], 400, 20, "ops 1, 49, 58, 62: points, sprite choice, return"),
    ("ops_synth.py", [], 200, 10, "vertex synthesis ops 41-44, 59: exact word arithmetic"),
    ("sprite_check.py", [], 1500, 30, "point sprites 0x340D4, 0x340EA, 0x3414E: pixels"),
    ("disc_check.py", [], 200, 5, "disc 0x33CAC (op 62): rows"),
    ("gate_check.py", [], 5000, 100, "writers of V:4A83E and V:4A924 (pitch gate)"),
    ("hand_check.py", [], 1000, 50, "x handedness: camera builder 0x32A66 and projection 0x3377C"),
    ("cut34_check.py", [], 300, 10, "0x21C34 cut-off at both sides"),
    ("op4_check.py", [], 1500, 30, "op 4 gate (camera height and record altitude independent), face list break, return"),
    ("sec2_check.py", [], 300, 20, "second vertex section of flat blocks with flag bit 2, constructed blocks"),
    ("line_check.py", [], 6000, 300, "clipped line (op 2) against the open-pyramid rule, boundary classes"),
    ("census_ffff.py", [], 1, 1, "no stream of a terrain or landmark model ends on 0xFFFF; cockpit block control"),
    ("modeldata_check.py", [], 150, 15, "the model data is never written by the builder, walkers or drawers"),
    ("typeb_loop_check.py", [], 150, 15, "type B chains with a self-naming last entry: the scaled distance stays below the limit"),
    ("nearbody_check.py", [], 1, 1, "near body polygons in the deck state (band 3) and none airborne"),
    ("nearbody_rows_check.py", [], 1, 1, "near body triangles for last rows 144-190: uncut classification, line height, fill height"),
    ("ent_check.py", ["all"], 300, 10, "entity entries through the real draw path; OR rule of the handler results"),
    ("zoom_check.py", [], 150, 10, "zoom words 0x20, 0x40, 0x80, 0xFF, 0x100 with V:4A895 zero and not: P1 and rule 5"),
    ("hatch_check.py", [], 1000, 30, "hatch and stripe family (ops 19-23, 32, 57) and ops 6, 12, 14: exact vertex lists handed to the clippers, fill words, which calls count"),
    ("draw_rules_check.py", [], 400, 20, "projection rows per entry, when each handler writes the pen, op 26 and ops 27/30 early-outs, op 6 with N below 2, word negation of -32768"),
    ("line_model.py", [], 40000, 500, "clipped line routine 0x33936: word-exact model of the plane cascade, hit points and projection"),
    ("typeb_toggle_check.py", [], 300, 100, "toggle word across objects: carried into a type B object, cleared by the common drawer, seen by op 26"),
    ("stipple_check.py", [], 600, 30, "fill state (mask, pattern, toggle) written by each operation and inherited by the next"),
    ("bsp_check.py", [], 3000, 100, "block tree side tests: point-and-normal 0x22A94, axis 0x22B3A, three-vertex 0x22A84"),
    ("roll_check.py", [], 400, 20, "camera matrix of the cockpit view: angles are the record words, roll sign"),
    ("synth2_check.py", [], 400, 20, "vertex synthesis ops 34-36, 38, 45-47, 52, 53, 56, 63-65: slots written"),
    ("synth_note_check.py", [], 200, 10, "vertex synthesis ops 41-44, 59 as transcribed in the tables of note 03: every word of the array"),
    ("eye_check.py", [], 300, 20, "camera position with the per-class cockpit eye offset (0x1F45E)"),
]


def run(seed=1, scale=60, quiet=False):
    bad = 0
    for script, lead, base, low, what in SUITE:
        n = max(low, base * scale // 1000)
        cmd = [sys.executable, os.path.join(HERE, script)] + lead + [str(seed), str(n)]
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=HERE)
        lines = [l for l in (p.stdout + p.stderr).strip().split("\n") if l]
        if not quiet:
            print("%-16s %-12s N=%-5d %s" % (script, " ".join(lead), n, what))
            for l in lines[-6:]:
                print("      " + l[:230])
        if p.returncode != 0:
            bad += 1
            print("FAILED: " + " ".join(cmd[1:]))
    print("prim_suite:", "FAILED, %d scripts" % bad if bad else "ok")
    return bad


if __name__ == "__main__":
    sys.exit(1 if run(int(sys.argv[1]) if len(sys.argv) > 1 else 1, int(sys.argv[2]) if len(sys.argv) > 2 else 60) else 0)
