"""Stage [90] against the reference emulator, on the bitmaps: states captured by tools/vamiga-ref/scripts/gen-nearground.py (nearground-N.txt, logs nearground-N.log).

    python3 nearground_frames_check.py [LOGDIR]        (default extracted/captures/ng3; skips with exit 0 without the logs and extracted/interceptor.bin)

Each state was run twice from the same paused scene and stopped right after the pass (the instruction after the call is a branch to itself): "off" with the pass removed (its entry replaced by RTS),
"on" with it.  The log holds, for each run, the camera words the pass reads, the three triangle shape tables, the fill block and the four planes of the page being drawn.  The "off" planes are the
pens under the pass; nearground_model.py gives the pass's events for the logged camera words and nearground_pixels.py the pens after them; the "on" planes must equal them in every pixel of rows 0 to L (rows 0 to L, the window rows; the rows below L are not drawn by the pass and hold timing-dependent drawing of other stages).  The pass writes plane 2 only
(the blitter trace of nearground_blit_check.py: every D pointer of the pass is plane 2 or the scratch plane), so a pair whose planes 0, 1 and 3 differ in rows 0 to L was staged at different
instants (an object drawn differently) and is discarded with the pairs whose camera words differ.  The comparison is made twice, with the alternate blit's C channel read at BLTCMOD = 40
(`cquirk`, the hardware rule: the port's) and read in place (plane 2 unchanged outside the area, the rule before the C-modulus probe); the states where the two differ decide it (none in the
ng3 captures: plane 2 is uniform where C is read; nearground_blit_check.py decides it on patterned planes).  The check fails unless the BLTCMOD rule equals every paired state.
"""
import glob, os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(ROOT, "tools", "vamiga-ref"))
import nearground_model as M, nearground_pixels as NP


def pens_of(data):
    planes = [data[k * 8000:(k + 1) * 8000] for k in range(4)]
    return [[sum(((planes[k][y * 40 + x // 8] >> (7 - x % 8)) & 1) << k for k in range(4)) for x in range(320)] for y in range(200)]


def main():
    d = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, "extracted", "captures", "ng3")
    binp = os.path.join(ROOT, "extracted", "interceptor.bin")
    logs = sorted(glob.glob(os.path.join(d, "nearground-*.log")))
    if not (logs and os.path.exists(binp)):
        print("skipped (missing %s or extracted/interceptor.bin)" % d); return 0
    import logparse
    img = open(binp, "rb").read()
    t = dict(states=0, unpaired=0, other_planes=0, inactive=0, other_page=0, exact_quirk=0, exact_clean=0, differ_states=0, differ_follow_quirk=0, differ_follow_clean=0, wrong_pixels=0,
             events_sprites=0, events_lines_dots=0, events_general=0, changed_pixels=0, garbage_pixels=0, alt_decides=0, alt_follows_alt=0, alt_follows_plain=0)
    shown = 0
    for lp in logs:
        L = logparse.load(lp)
        ram71 = next(int(ln.split()[5], 16) for ln in open(lp) if ln.startswith("seg 71 "))      # RAM address of hunk 71, which holds V:4A6E8..
        tab = {ram71 + 0x3E: 0, ram71 + 0x4E: 1}                                                    # the plane tables V:4A726 (page 0) and V:4A736 (page 1)
        for nm in sorted({k.split("_", 1)[1][:-3] for k in L if k.startswith("planes0_") and k.endswith("-on")}):
            on, off = nm + "-on", nm + "-off"
            if "planes0_" + off not in L: continue
            pg_on, pg_off = tab.get(L["dtab_" + on].u32(0)), tab.get(L["dtab_" + off].u32(0))
            if pg_on is None or pg_off is None: t["other_page"] += 1; continue
            g = lambda lab: L[lab + "_" + on]
            if any(L[lab + "_" + on].data != L[lab + "_" + off].data for lab in ("cx", "cz", "hl", "band", "mat", "lim")):
                t["unpaired"] += 1; continue                       # the two runs of the pair did not reach the same camera words (the staging raced the flight step)
            st = dict(cam_x=g("cx").s16(0), cam_z=g("cz").s16(0), height_long=g("hl").s32(0), band=g("band").u8(0), matrix=list(struct.unpack(">9h", g("mat").data)), limit=g("lim").s16(0))
            im = bytearray(img)
            for lab, flat in (("t12", 0x2CDEC), ("t16", 0x2CE04), ("t20", 0x2CE1C)): im[flat - 0x10000:flat - 0x10000 + 12] = g(lab).data
            pon_d, poff_d = L["planes%d_%s" % (pg_on, on)].data, L["planes%d_%s" % (pg_off, off)].data
            if any(pon_d[k * 8000:k * 8000 + (st["limit"] + 1) * 40] != poff_d[k * 8000:k * 8000 + (st["limit"] + 1) * 40] for k in (0, 1, 3)):
                t["other_planes"] += 1; continue                   # staged at different instants: a plane the pass never writes differs
            events, cand = M.model(st, im)
            if cand is None: t["inactive"] += 1; continue
            t["states"] += 1
            bg = pens_of(L["planes%d_%s" % (pg_off, off)].data); got = pens_of(L["planes%d_%s" % (pg_on, on)].data)
            rows = st["limit"] + 1                                    # the pass draws in rows 1 to L only; the rows below hold other, timing-dependent drawing of the frame
            bg_full = bg
            q = NP.apply(events, bg, st["limit"])[:rows]; c = NP.apply(events, bg, st["limit"], cquirk=False)[:rows]; bg = bg[:rows]; got = got[:rows]
            for e in events:
                if e[0] != "tri": t["events_sprites"] += 1
                elif NP.fmodel.tiny(list(e[1:]), st["limit"], 3, 4) is None: t["events_general"] += 1
                else: t["events_lines_dots"] += 1
            t["changed_pixels"] += sum(1 for y in range(rows) for x in range(320) if got[y][x] != bg[y][x])
            gq, gc = q == got, c == got
            plain = NP.apply(events, bg_full, st["limit"], alt=False)[:rows]
            if plain != q:                                        # the alternate blit (plane 2 := plane 3) and a plain clear of plane 2 under the area differ in this state
                t["alt_decides"] += 1; t["alt_follows_alt"] += gq; t["alt_follows_plain"] += plain == got
            t["exact_quirk"] += gq; t["exact_clean"] += gc
            nd = sum(1 for y in range(rows) for x in range(320) if q[y][x] != c[y][x])
            t["garbage_pixels"] += nd
            if nd:
                t["differ_states"] += 1; t["differ_follow_quirk"] += gq; t["differ_follow_clean"] += gc
            if not gq:
                bad = [(x, y, bg[y][x], got[y][x], q[y][x], c[y][x]) for y in range(rows) for x in range(320) if got[y][x] != q[y][x]]
                t["wrong_pixels"] += len(bad)
                if shown < 6: shown += 1; print("DIFF", nm, len(bad), bad[:6])
    print("near-ground bitmaps: %s" % t)
    return 1 if t["states"] and t["exact_quirk"] < t["states"] else 0               # the port's rule (alternate blit, C read at BLTCMOD = 40) must equal every paired state


if __name__ == "__main__":
    sys.exit(main())
