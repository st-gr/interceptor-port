"""Stage [90]'s alternate blit (0x34EEC, BLTCON0 0x0FCA, descending) in the reference emulator, blit by blit: the C-modulus probe of note 04 ("Stage [90]").

    python3 nearground_blit_check.py [LOGDIR]       (default extracted/captures/ngprobe; skips with exit 0 without the logs)

The logs come from tools/vamiga-ref/scripts/nearground-probe-N.txt (gen-nearground.py probe): per state the driver traces every blit of the pass (`blittrace`: the
registers at each BLTSIZE write), stops right after the BLTSIZE write of the first alternate blit, dumps both pages and the scratch plane (the blit's A source),
runs to the BLTSIZE write of the next blit (the scratch clear, whose routine waits for the blitter to be idle and writes no C pointer) and dumps the pages again.
For each such blit this script checks:
  * the C pointer: its end value (in the scratch clear's trace line) against its start value minus H (2 W + BLTCMOD), the descending rule with the traced BLTCMOD;
  * the pixels: plane 2 after the blit against D = (A and B) or (not A and C) computed from the dumps before it, once with C read at BLTCMOD (the traced 40) and
    once with C read at the A/B/D modulus 40 - 2 W ("in place"); every other plane and every byte of plane 2 outside the rectangle must be unchanged.
A state decides between the two when they differ; the emulator must follow the BLTCMOD rule in all of them."""
import glob, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "vamiga-ref"))
PAGES = (0x4B2D8, 0x10368)
SCRATCH = 0x18068


def blits(path):
    """(state name, [trace dicts in order], stopped at 0x0FCA) per state of a probe log."""
    out, cur = [], None
    for ln in open(path):
        if ln.startswith("# ") and re.match(r"# (ng|nb)\d{3}$", ln.strip()):
            cur = [ln.split()[1], [], False]; out.append(cur)
        elif cur and ln.startswith("blit pc "):
            p = ln.split(); d = {p[i]: p[i + 1] for i in range(1, len(p) - 1, 2)}
            cur[1].append(d)
        elif cur and ln.startswith("# runto stopped after the BLTSIZE write of BLTCON0 0FCA"):
            cur[2] = True
    return out


def main():
    d = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, "extracted", "captures", "ngprobe")
    logs = sorted(glob.glob(os.path.join(d, "nearground-probe-*.log")))
    if not logs:
        print("skipped (no probe logs in %s)" % d); return 0
    import logparse
    t = dict(states=0, alt_blits=0, cpt_rule=0, cmod_values=set(), accuracy=set(), exact_cmod=0, exact_inplace=0, deciding=0, deciding_cmod=0, deciding_inplace=0,
             outside_unchanged=0, deciding_pixels=0)
    for lp in logs:
        L = logparse.load(lp)
        for nm, tr, stopped in blits(lp):
            t["states"] += 1
            if not stopped: continue
            i = next(k for k, b in enumerate(tr) if b["con0"] == "0FCA")
            b, nx = tr[i], tr[i + 1]
            assert nx["con0"] == "0D0C", (nm, nx)
            t["alt_blits"] += 1
            t["cmod_values"].add(int(b["cmod"])); t["accuracy"].add(b["accuracy"])
            size = int(b["d0"], 16) & 0xFFFF; H, W = size >> 6, size & 63
            cpt, dpt, bpt, apt = (int(b[k], 16) for k in ("cpt", "dpt", "bpt", "apt"))
            cmod, amod = int(b["cmod"]), int(b["amod"])
            t["cpt_rule"] += int(nx["cpt"], 16) == cpt - H * (2 * W + cmod)
            base = next(p for p in PAGES if p <= dpt < p + 32000)
            pg = PAGES.index(base)
            pre = L["planes%d_%s-pre" % (pg, nm)].data; post = L["planes%d_%s-post" % (pg, nm)].data; scr = L["scratch_%s-pre" % nm].data
            assert L["sbase_%s-pre" % nm].u32(0) == SCRATCH
            w = lambda buf, a: (buf[a] << 8) | buf[a + 1]

            def run(cm):
                new = bytearray(pre); c = cpt - base
                for r in range(H):
                    for k in range(W):
                        A = w(scr, apt - SCRATCH - r * (2 * W + amod) - 2 * k); B = w(pre, bpt - base - r * (2 * W + amod) - 2 * k)
                        C = w(pre, c); c -= 2
                        D = (A & B) | (~A & C & 0xFFFF); o = dpt - base - r * (2 * W + amod) - 2 * k
                        new[o] = D >> 8; new[o + 1] = D & 255
                    c -= cm
                return bytes(new)
            q, n = run(cmod), run(amod)
            t["exact_cmod"] += q == post; t["exact_inplace"] += n == post
            rect = {dpt - base - r * (2 * W + amod) - 2 * k + j for r in range(H) for k in range(W) for j in (0, 1)}
            t["outside_unchanged"] += all(pre[a] == post[a] for a in range(32000) if a not in rect)
            if q != n:
                t["deciding"] += 1; t["deciding_cmod"] += q == post; t["deciding_inplace"] += n == post
                t["deciding_pixels"] += sum(bin(q[a] ^ n[a]).count("1") for a in range(32000))
                print("deciding", nm, "W", W, "H", H, "cmod", cmod, "pixels", sum(bin(q[a] ^ n[a]).count("1") for a in range(32000)), "follows", "BLTCMOD" if q == post else "in place" if n == post else "neither")
    print("near-ground alternate blits: %s" % t)
    ok = t["alt_blits"] and t["cpt_rule"] == t["exact_cmod"] == t["alt_blits"] and t["deciding_inplace"] == 0
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
