"""The extent of the alternate blit's rectangle against the rule, on the traced blits of the C-modulus probe (docs/re/04-scene-composition.md, "Stage [90]", step 11).

    python3 nearground_rect_check.py [LOGDIR]       (default extracted/captures/ngprobe; skips with exit 0 without the logs and extracted/interceptor.bin)

For each probe state the model (nearground_model.py, camera words logged after the trace, the enlarged shape tables for the `nb` states) gives the pass's triangles; the first triangle of the general fill is the one
whose alternate blit the driver stopped at.  The rule: wl = word of max(minx - 1, 0), wr = word of maxx, W = wr - wl + 1, bottom row = min(maxy, L), H = bottom row - miny + 1; the blit starts (descending) at
plane 2 of the drawing page + bottom row * 40 + 2 wr.  The traced BLTSIZE (H, W) and D pointer must equal them."""
import glob, os, re, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(ROOT, "tools", "vamiga-ref"))
import nearground_model as M, nearground_pixels as NP
import nearground_blit_check as BC

BIG = {0x2CDEC: (8, -8, -8, -8, 0, 6), 0x2CE04: (24, -24, -24, -24, 0, 18), 0x2CE1C: (60, -60, -60, -60, 0, 45)}


def main():
    d = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, "extracted", "captures", "ngprobe")
    logs = sorted(glob.glob(os.path.join(d, "nearground-probe-*.log")))
    binp = os.path.join(ROOT, "extracted", "interceptor.bin")
    if not (logs and os.path.exists(binp)):
        print("skipped (no probe logs or image)"); return 0
    import logparse
    img = open(binp, "rb").read()
    t = dict(blits=0, matched=0, mismatched=0, no_model_triangle=0, miny0=0, bottom_cut=0, left_edge=0, widths=set())
    for lp in logs:
        L = logparse.load(lp)
        for nm, tr, stopped in BC.blits(lp):
            if not stopped: continue
            b = next(x for x in tr if x["con0"] == "0FCA")
            g = lambda lab: L["%s_%s-probe" % (lab, nm)]
            st = dict(cam_x=g("cx").s16(0), cam_z=g("cz").s16(0), height_long=g("hl").s32(0), band=g("band").u8(0), matrix=list(struct.unpack(">9h", g("mat").data)), limit=g("lim").s16(0))
            im = bytearray(img)
            if nm.startswith("nb"):
                for a, v in BIG.items(): im[a - 0x10000:a - 0x10000 + 12] = struct.pack(">6h", *v)
            ev, cand = M.model(st, im)
            tris = [e for e in ev if e[0] == "tri" and NP.fmodel.tiny(list(e[1:]), st["limit"], 3, 4) is None and NP.fmodel.fillpoly(list(e[1:]), st["limit"])[0] != "skip"]
            t["blits"] += 1
            if not tris: t["no_model_triangle"] += 1; continue
            poly = tris[0][1:]
            xs = [p[0] for p in poly]; ys = [p[1] for p in poly]
            wl = max(min(xs) - 1, 0) >> 4; wr = max(xs) >> 4; W = wr - wl + 1
            bot = min(max(ys), st["limit"]); H = bot - min(ys) + 1
            size = int(b["d0"], 16) & 0xFFFF; dpt = int(b["dpt"], 16)
            base = next(p for p in BC.PAGES if p <= dpt < p + 32000)
            ok = (size >> 6, size & 63) == (H, W) and dpt == base + 16000 + bot * 40 + 2 * wr
            t["matched" if ok else "mismatched"] += 1
            t["miny0"] += min(ys) <= 0; t["bottom_cut"] += max(ys) > st["limit"]; t["left_edge"] += min(xs) - 1 <= 0; t["widths"].add(W)
            if not ok: print("DIFF", nm, poly, "rule", (H, W, hex(base + 16000 + bot * 40 + 2 * wr)), "trace", (size >> 6, size & 63, hex(dpt)))
    print("alternate blit rectangles: %s" % t)
    return 1 if t["mismatched"] else 0


if __name__ == "__main__":
    sys.exit(main())
