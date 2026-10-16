"""Text records and the text engine of the original (docs/re/08-game-flow.md, sections 2 and 3).

    python3 text_check.py [--table] [--quick]

Reads the user's git-ignored extracted/interceptor.bin (never committed) and the rig library; skips with exit 0 when one is missing.

Part 1, the decoder of the note: the id table at 0x43DFE (ids 1 to 110, a word offset from 0x43DFE per id), the record layout (row byte, column byte,
then lines: flags, colour, text bytes, 0, delay; a flags byte with bit 7 set ends the record) and the line count are decoded here from the image by
the rule of the note, never with the original's code; an id is a valid record when every line has flags 0 to 3 (bit 7 clear), every text byte is 0x20 to 0x5F and a flags byte with bit 7 set (any such byte, not only 0xFF: the record of id 0x6C ends on the next record's row byte) is reached inside the span 0x43DF4-0x4615A. Counts printed: valid ids, distinct valid records, lines (the note's 106 / 91 / 337 / 241).

Part 2, the engine model: the pass-by-pass behaviour of the text engine at 0x377B2 (typed-out lines, instant lines, spaces, the delay bytes, the end hold, the
glyph blits, the position of a negative id) is a small model written from the rules of section 3; every id alone, and lists of ids, are run through
the model and through the original's routine in the rig, one call per pass; after every pass the four bit planes (40 bytes per scanline, 4 planes), the
engine's state bytes (list index, running flag, mode byte, end hold) and the sound-request counter are compared. The count of passes compared and the
number of differences (0 expected) are printed.

--table prints the record table (index, id range, row, column, colours, lengths: numbers only, no text).
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    from common import Rig, IMGPATH, LIBPATH
    IMG = open(IMGPATH, "rb").read()
except Exception:
    print("text_check: image or rig missing, skipped")
    sys.exit(0)
if not os.path.exists(LIBPATH):
    print("text_check: rig library missing, skipped")
    sys.exit(0)

BASE = 0x10000
IDTAB = 0x43DFE          # word offsets (from 0x43DFE) of the records, id 1 at the first word
NIDS = 110               # ids 1 to 110 (0x6E)
COLTAB = 0x4615A         # 48 entries of two words: byte offset (even), shift in the top nibble
GLYPHTAB = 0x4283C       # word offsets from 0x4283C by character - 0x20; 7 bytes per glyph
STEP_DEFAULT = 0x1B8     # V:4A7F6, bytes between two lines (11 scanlines of 40 bytes)
HOLD = 0x32              # V:4A7FC, passes of the end hold


def u8(a): return IMG[a - BASE]
def u16(a): return struct.unpack(">H", IMG[a - BASE:a - BASE + 2])[0]
def s8(v): return v - 256 if v > 127 else v


# ---- Part 1: records ------------------------------------------------------------------------------------------------
def record_offset(i):
    return IDTAB + u16(IDTAB + 2 * (i - 1))


def parse_record(a):
    """One record: (row, col, [(flags, colour, text bytes, delay)], address after the end byte)."""
    row, col = u8(a), u8(a + 1)
    p = a + 2
    lines = []
    while True:
        fl = u8(p)
        if fl & 0x80:
            return row, col, lines, p + 1
        colour = u8(p + 1)
        p += 2
        s = p
        while u8(p) != 0:
            p += 1
        txt = IMG[s - BASE:p - BASE]
        lines.append((fl, colour, txt, u8(p + 1)))
        p += 2


def valid_record(a):
    """Section 2.3's validity rule on the record at a."""
    try:
        row, col, lines, end = parse_record(a)
    except Exception:
        return False
    if not (0x43DF4 <= a and end <= 0x4615A):
        return False
    return all((ln[0] & 0xFC) == 0 and all(0x20 <= b <= 0x5F for b in ln[2]) for ln in lines)


def records():
    out = {}
    for i in range(1, NIDS + 1):
        a = record_offset(i)
        try:
            out[i] = (a,) + parse_record(a)
        except Exception:
            out[i] = None
    return out


# ---- Part 2: the engine model ---------------------------------------------------------------------------------------
class Model:
    """The text engine of 0x377B2 for non-input records; planes are four bytearrays of 8000 bytes (40 x 200), index 0 the highest bit plane of the pen
    (the original's plane array is ordered that way)."""

    def __init__(self):
        self.planes = [bytearray(8000) for _ in range(4)]
        self.step = STEP_DEFAULT
        self.L = []          # the id list (V:4A802...), 0 terminated by the caller
        self.idx = 0         # V:4A87E (bytes, steps of 2): here the word index
        self.active = 0      # V:4A87B
        self.mode = 0        # V:4A898
        self.hold = 0        # V:4A7FC
        self.w7fe = 0
        self.w800 = 0
        self.b896 = 0
        self.b897 = 0
        self.fl = 0
        self.pen = 0
        self.D = 0
        self.start = (0, 0, 0)
        self.cur = (0, 0, 0)
        self.sounds = 0
        self.rec = None
        self.a4_saved = 0

    def sound(self):
        self.sounds += 1

    def next_word(self, k):
        return self.L[k] if k < len(self.L) else 0

    def blit(self, k, a4, g, pen):
        off = u16(COLTAB + 4 * k)
        sh = u16(COLTAB + 4 * k + 2) >> 12
        gp = GLYPHTAB + u16(GLYPHTAB + 2 * g)
        for r in range(7):
            byte = u8(gp + r)
            pos = off + a4 + 40 * r
            if pos < 0 or pos + 4 > 8000:
                continue
            m = (byte << 24) >> sh
            for pl in range(4):
                bit = (pen >> (3 - pl)) & 1
                v = struct.unpack(">I", self.planes[pl][pos:pos + 4])[0]
                v = (v | m) if bit else (v & ~m & 0xFFFFFFFF)
                self.planes[pl][pos:pos + 4] = struct.pack(">I", v)

    def end_list(self):
        self.step = STEP_DEFAULT
        if self.hold > 0:
            self.hold -= 1
            if self.hold <= 0:
                self.mode |= 0x80
        self.active = 0

    def begin(self, wid):
        if wid & 0x8000:
            a4 = self.a4_saved + 0x1B8
            n = (wid & 0x3FFF)
            skip = True
        else:
            n = wid
            skip = False
        a = record_offset(n)
        self.rec_addr = a
        self.rec = a
        p = a
        if not skip:
            a4 = u8(p) * 40 + s8(u8(p + 1))
        p += 2
        self.hold = 0
        self.w7fe = 0
        self.w800 = 0
        if s8(self.mode) < 2:
            self.mode = 0
            self.b896 = 1
            self.b897 = 1
        else:
            raise NotImplementedError("input mode")
        self.active = 1
        self.fl = u8(p)
        c = u8(p + 1)
        p += 2
        self.pen = c >> 4
        self.D = 2 if (wid & 0x4000) else (c & 0xF)
        self.start = (0, p, a4)
        self.cur = self.start
        self.a4_saved = a4
        return self.draw_loop("37A20")

    def save(self, k, p, a4):
        self.cur = (k, p, a4)
        self.a4_saved = a4

    def run_pass(self):
        w = self.next_word(self.idx)
        if w == 0:
            return self.end_list()
        if self.active == 0:
            return self.begin(w)
        # running
        if self.hold > 0:
            return self.end_list()
        self.w7fe -= 1
        if self.w7fe > 0:
            return
        if not (self.fl & 1):
            self.cur = self.start
            self.save(*self.start)
            return self.draw_loop("379BA")
        self.w800 -= 1
        if self.w800 <= 0:
            return self.draw_loop("379BA")
        if self.b896 <= 0:
            return
        self.b896 -= 1
        return self.draw_loop("37A18")

    def draw_loop(self, label):
        k, p, a4 = self.cur
        while True:
            if label == "379BA":
                self.w800 = self.D
                self.b896 = 1
                label = "37A18"
            if label == "37A18":
                k, p, a4 = self.cur
                label = "37A20"
            if label == "37A20":
                ch = u8(p)
                if ch & 0x80:
                    return self.end_record()
                if ch == 0:
                    r = self.end_line(k, p, a4)
                    if r is None:
                        return
                    label = r
                    continue
                g = ch - 0x20
                if self.fl & 1:
                    if g == 0:
                        if not (self.fl & 2):
                            pass
                        else:
                            self.save(k + 1, p + 1, a4)
                            label = "379BA"
                            continue
                    else:
                        if self.b896 != 0 and self.sound_ok():
                            self.sound()
                self.blit(k, a4, g, self.pen)
                if not (self.fl & 1):
                    self.save(k + 1, p + 1, a4)
                    label = "379BA"
                    continue
                if self.b896 > 0:
                    self.save(k, p, a4)
                else:
                    self.save(k + 1, p + 1, a4)
                return

    def sound_ok(self):
        return True            # V:4A88F is below 2 in the rig runs (the setting is zero)

    def end_line(self, k, p, a4):
        if not (self.fl & 1):
            self.b897 -= 1
            if self.b897 >= 0:
                self.save(*self.start)
                self.sounds += 1             # the line-end click (0x37C2E), when V:4A88F < 2
                return None
        p += 1
        dl = s8(u8(p))
        p += 1
        self.w7fe = (dl * 4) & 0xFFFF
        if self.w7fe & 0x8000:
            self.w7fe -= 0x10000
        self.fl = u8(p)
        c = None
        if s8(self.fl) >= 0:
            p += 1
            self.b897 = 1
            c = u8(p)
            p += 1
            self.pen = c >> 4
        self.w800 = -1
        a4 += self.step
        if c is not None:
            self.D = c & 0xF
        self.start = (0, p, a4)
        self.save(0, p, a4)
        return None

    def end_record(self):
        if self.next_word(self.idx + 1) != 0:
            self.idx += 1
            self.active = 0
            return
        self.mode = 1
        self.hold = HOLD
        self.idx = 0
        self.L = []


# ---- the rig side ---------------------------------------------------------------------------------------------------
PL = [0x600000 + k * 0x4000 for k in range(4)]
ARR = 0x700000
SNDLOG = 0x7FF000


class Orig:
    def __init__(self):
        self.r = Rig()
        self.reset()

    def reset(self):
        r = self.r
        r.reset()
        # the sound start routine 0x1A042 becomes a counter (its argument words are not needed)
        r.poke(0x1A042, bytes.fromhex("5279" + "007FF000" + "4e75"))
        r.poke(0x2970A, b"\x4e\x75")
        r.poke(0x1858A, b"\x4e\x75")
        r.w16(SNDLOG, 0)
        for k in range(4):
            r.w32(ARR + 4 * k, PL[k])
            r.poke(PL[k], bytes(0x4000))
        r.w32(0x4A76E, ARR)
        r.w32(0x4A7F6, STEP_DEFAULT)

    def call(self):
        rc = self.r.run(0x377B2)
        assert rc == 0, "rig call failed"

    def planes(self):
        return [self.r.peek(p, 8000) for p in PL]

    def state(self):
        r = self.r
        return (r.u8(0x4A87E) >> 1, r.u8(0x4A87B), r.u8(0x4A898), r.u16(0x4A7FC), r.u16(SNDLOG))


def run_both(ids, maxpass=4000):
    """Run a list through the original and the model; return (passes compared, mismatch descriptions)."""
    o = Orig()
    m = Model()
    o.call()                   # one pass with the empty list: sets the line step as the original does at start
    r = o.r
    m.run_pass()
    for i, v in enumerate(ids + [0]):
        r.w16(0x4A802 + 2 * i, v)
    r.w8(0x4A87E, 0)
    m.L = list(ids) + [0]
    m.idx = 0
    diffs = []
    n = 0
    for n in range(1, maxpass):
        o.call()
        m.run_pass()
        if o.planes() != [bytes(p) for p in m.planes]:
            diffs.append((n, "planes"))
        st = o.state()
        mst = (m.idx, m.active, m.mode, m.hold, m.sounds)
        if st != mst:
            diffs.append((n, "state", st, mst))
        if len(diffs) > 3:
            break
        if (st[2] & 0x80) and st[0] == 0:
            break
    return n, diffs


# ---- Part 3: the captures ---------------------------------------------------------------------------------------------
CAPDIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "extracted", "captures")


def ppm(path):
    b = open(path, "rb").read()
    parts = b.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def model_pens(ids, step=STEP_DEFAULT):
    m = Model()
    m.run_pass()
    m.L = list(ids) + [0]
    m.step = step
    for _ in range(5000):
        m.run_pass()
        if m.mode & 0x80:
            break
    img = [[0] * 320 for _ in range(200)]
    for y in range(200):
        for x in range(320):
            v = 0
            for pl in range(4):
                v |= ((m.planes[pl][y * 40 + x // 8] >> (7 - x % 8)) & 1) << (3 - pl)
            img[y][x] = v
    return img


def compare_capture(name, ids, step=STEP_DEFAULT):
    """A text-only capture (credits, log-disk prompt, main menu): the pen of every model pixel must map to one colour in the frame (game window at frame
    x 186, y 42, a game pixel two frame pixels wide) and different pens to different colours. Returns (pixels, mismatches, pens) or None when absent."""
    path = os.path.join(CAPDIR, name)
    if not os.path.exists(path):
        return None
    img = model_pens(ids, step)
    w, h, px = ppm(path)
    cmap, bad = {}, 0
    for y in range(200):
        for x in range(320):
            i = ((42 + y) * w + 186 + 2 * x) * 3
            c = tuple(px[i:i + 3])
            p = img[y][x]
            if p in cmap and cmap[p] != c:
                bad += 1
            cmap.setdefault(p, c)
    if len(set(cmap.values())) != len(cmap):
        bad += 1
    return 64000, bad, len(cmap)


def input_check():
    """Section 3.7: the countercode input mode on the original's routine. Mode 3, six characters allowed, id list [4] (record at row 72, column 34); raw key
    codes are pushed one at a time with four passes each. Returns the number of failed expectations (0 expected) and the number of expectations."""
    o = Orig()
    r = o.r
    o.call()
    r.w8(0x4A898, 3)
    r.w8(0x4A8AD, 6)
    r.w16(0x4A802, 4)
    r.w16(0x4A804, 0)
    r.w8(0x4A87E, 0)
    for _ in range(3):
        o.call()
    fails = 0
    checks = 0

    def lit(cell):
        pl = o.planes()
        n = 0
        for y in range(72, 79):
            for x in range(272 + 7 * cell, 272 + 7 * cell + 5):
                if any((pl[k][y * 40 + x // 8] >> (7 - x % 8)) & 1 for k in range(4)):
                    n += 1
        return n

    def key(code):
        idx, cnt = r.u8(0x4A8B0), r.u8(0x4A8B1)
        r.w8(0x4A899 + (idx + cnt) % 10, code)
        r.w8(0x4A8B1, cnt + 1)
        for _ in range(4):
            o.call()

    def expect(cond):
        nonlocal fails, checks
        checks += 1
        if not cond:
            fails += 1

    expect(lit(0) == 0)                      # the record's own text (blanks) is not drawn and nothing is typed yet
    key(0x01); key(0x02); key(0x03)         # digits 1, 2, 3
    expect(lit(0) > 0 and lit(1) > 0 and lit(2) > 0 and lit(3) == 0)
    expect((r.u8(0x4A8AD), r.u8(0x4A8AE)) == (3, 3))
    key(0x41)                                # backspace: the third cell is erased, the counts are restored
    expect(lit(0) > 0 and lit(1) > 0 and lit(2) == 0)
    expect((r.u8(0x4A8AD), r.u8(0x4A8AE)) == (4, 2))
    key(0x41); key(0x41); key(0x41)          # more backspaces than characters: nothing before the first cell
    expect(lit(0) == 0 and (r.u8(0x4A8AD), r.u8(0x4A8AE)) == (6, 0))
    for c in (0x04, 0x05, 0x06, 0x07, 0x08, 0x09):   # six characters fill the allowance
        key(c)
    expect(r.u8(0x4A8AD) == 0 and r.u8(0x4A8AE) == 6 and all(lit(k) > 0 for k in range(6)))
    # a fresh entry: the seventh key (the record's text is seven characters long) ends it; bit 7 of the mode byte for a pass, then the record ends
    o.reset()
    o.call()
    r.w8(0x4A898, 3)
    r.w8(0x4A8AD, 6)
    r.w16(0x4A802, 4)
    r.w16(0x4A804, 0)
    r.w8(0x4A87E, 0)
    for _ in range(3):
        o.call()
    for c in (0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07):
        key(c)
    expect(r.u8(0x4A898) in (0x83, 1) and r.u8(0x4A8AD) == 0xFF)
    for _ in range(40):
        o.call()
    expect(r.u8(0x4A898) == 1 and r.u16(0x4A802) == 0)
    return fails, checks


def pass_costs(ids):
    """Passes to the end-of-list pass, key-click requests and glyph blits of an id list on the model (the inputs of the pass cost model of section 3.8)."""
    class Counting(Model):
        def __init__(self):
            super().__init__()
            self.nb = 0

        def blit(self, k, a4, g, pen):
            self.nb += 1
            return super().blit(k, a4, g, pen)
    m = Counting()
    m.run_pass()
    m.L = list(ids) + [0]
    n = blits = 0
    while not (m.mode & 1) and n < 20000:
        m.nb = 0
        m.run_pass()
        n += 1
        blits += m.nb
    return n, m.sounds, blits


def predicted_blanks(ids):
    n, clicks, blits = pass_costs(ids)
    return n / 6.0 + 0.25 * clicks + 0.0475 * blits


def main():
    quick = "--quick" in sys.argv
    recs = records()
    bad = [i for i, v in recs.items() if v is None]
    inside = all(v is None or (0x43DF4 <= v[0] and v[4] <= 0x4615A) for v in recs.values())
    distinct = sorted({v[0] for v in recs.values() if v})
    nlines = sum(len(parse_record(a)[2]) for a in distinct)
    ntext = sum(1 for a in distinct for ln in parse_record(a)[2] if ln[2])
    ok = [i for i, v in recs.items() if v and valid_record(v[0])]
    vd = sorted({recs[i][0] for i in ok})
    vlines = sum(len(parse_record(a)[2]) for a in vd)
    vtext = sum(1 for a in vd for ln in parse_record(a)[2] if ln[2])
    print("ids %d, undecodable %d, inside the span: %s; offsets decoded without the validity rule: %d records, %d lines (%d non-empty)"
          % (len(recs), len(bad), inside, len(distinct), nlines, ntext))
    print("section 2.3 validity rule: %d valid ids (not records: %s), %d distinct records, %d lines (%d with text)"
          % (len(ok), sorted(set(recs) - set(ok)), len(vd), vlines, vtext))
    if "--table" in sys.argv:
        for i, v in sorted(recs.items()):
            if v:
                a, row, col, lines, e = v
                print(i, hex(a), row, col, [(hex(f), hex(c), len(t), d) for f, c, t, d in lines])
    sel = [i for i in range(1, NIDS + 1)] if not quick else [1, 7, 98, 99, 100, 101, 102, 109, 91, 40]
    total = 0
    ndiff = 0
    skipped = []
    for i in sel:
        a = recs[i]
        if a is None:
            skipped.append(i)
            continue
        if a[0] < 0x43F24:       # id 21 names the id table itself (not a record)
            skipped.append(i)
            continue
        try:
            n, d = run_both([i])
        except NotImplementedError:
            skipped.append(i)
            continue
        total += n
        ndiff += len(d)
        if d:
            print("id", i, "differs:", d[:2])
    lists = [[100, 101, 102, 103], [0x40, 0x4D, 0x4E, 0x8000 | 0x6E], [98, 0x8000 | 99], [7, 109]]
    for L in lists:
        n, d = run_both(L)
        total += n
        ndiff += len(d)
        if d:
            print("list", L, "differs:", d[:2])
    print("passes compared %d, differences %d, ids not run %s" % (total, ndiff, skipped))
    for name, ids in (("credits", [15]), ("game over F/A-18", [0x63]), ("main menu", [6] + list(range(100, 110)))):
        print("pass cost model, %s: %d passes, %d clicks, %d glyph blits, %.1f blanks" % ((name,) + pass_costs(ids) + (predicted_blanks(ids),)))
    f, c = input_check()
    print("input mode: %d expectations, %d failed" % (c, f))
    ndiff += f
    for name, ids, step in (("menu.ppm", [6] + list(range(100, 110)), 0x1E0), ("credits.ppm", [15], STEP_DEFAULT),
                            ("log-disk-prompt.ppm", [70, 94], STEP_DEFAULT)):
        r = compare_capture(name, ids, step)
        if r is None:
            print("capture %s absent, not compared" % name)
        else:
            print("capture %s: %d pixels, %d mismatches, %d pens" % (name, r[0], r[1], r[2]))
            ndiff += r[1]
    return 1 if ndiff else 0


if __name__ == "__main__":
    sys.exit(main())
