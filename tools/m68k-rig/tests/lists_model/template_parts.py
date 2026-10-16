"""The part ids of the 16 templates (docs/re/07-ai-and-weapons.md 8.2a, the table of the part ids; docs/re/04a-display-lists.md 8a): a static scan of the template list
at 0x25ECC (five longs per template, the model base in the second) and of each template's detail list (grammar of note 02 2.1): for every level the block named by
the leaf (low 12 bits, from the model base), the first word of the block (the part id word; its low byte is stored at +0x7B), and the block's flag byte (byte +7).
Reports the low nibbles that occur.  The hit-test table 0x3DACC has three entries (nibbles 0 to 2).

    python3 template_parts.py      (reads extracted/interceptor.bin; skips with exit 0 when it is missing)
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import IMGPATH
if not os.path.exists(IMGPATH):
    print("skipped (missing extracted/interceptor.bin)"); sys.exit(0)
img = open(IMGPATH, "rb").read()
def u16(a): return struct.unpack(">H", img[a - 0x10000:a - 0x10000 + 2])[0]
def u32(a): return struct.unpack(">I", img[a - 0x10000:a - 0x10000 + 4])[0]
TPL = 0x25ECC
nib = {}
for t in range(16):
    base = u32(TPL + 20 * t + 4); lst = u32(TPL + 20 * t + 8)
    a = lst; levels = []; seen = set()
    while a not in seen and len(levels) < 16:
        seen.add(a)
        w = u16(a)
        if w == 0xFFFF: break
        if w & 0x8000: leaf = w; nxt = None                          # leaf only: the last-resort level
        elif w & 0x4000: leaf = u16(a + 2); nxt = a + 6               # threshold, leaf, stream item: the far case reads the next node
        else:
            jump = u16(a + 2); leaf = u16(a + 4)                      # threshold, jump, leaf: the far case continues at base + jump
            nxt = None if jump & 0x8000 else base + jump
        if leaf != 0xFFFF:
            blk = base + (leaf & 0xFFF)
            levels.append((leaf, blk, u16(blk), img[blk + 7 - 0x10000]))
        if nxt is None: break
        a = nxt
    print("template %2d base %#x list %#x:" % (t, base, lst), ", ".join("leaf %04X block %#x id %04X flag %02X" % l for l in levels))
    for l in levels: nib.setdefault(l[2] & 15, []).append(t)
print("low nibbles of the part ids and the templates that have them:", {k: sorted(set(v)) for k, v in sorted(nib.items())})
