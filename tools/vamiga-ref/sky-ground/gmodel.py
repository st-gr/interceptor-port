"""Model of the ground-polygon routine 0x32E5A (docs/re/04-scene-composition.md, "Sky fill and ground polygon"): the four far corners of the
ground square in camera space, their clipping against the view pyramid, and the screen polygon of the sky side of the horizon.

corners(M, alt_long)   camera-space corners from the nine camera-matrix words M (V:4AC90) and the altitude long V:4AB1E
find(corn)             (counters, slots, screen points): the pyramid crossings of the corner edges
polygon(...)           the screen polygon (list of (x, y)), 'FULL', or None (no sky polygon)
check_rig.py compares this model with the game's code in the rig on random states; check_frames.py with emulator frames."""

CORNERS = [(10240, -9216), (-11008, -9728), (-10240, 9216), (9728, 11008)]      # (x, z) at 0x32E28


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def trunc_div(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


STATS = {"overflow": 0}        # number of divides whose quotient did not fit 16 bits (counted, then modelled as the original does)


def rdiv(num, d):
    """The divide of the crossing search 0x3324C (and fx_clip, note 03): the 32-bit product num divided by the signed word d (d is not zero here), truncating toward zero, then the
    correction: with h = |d| >> 1 taken as a word and r the remainder (the sign of num) made non-negative as a word, the quotient is kept when h > r (signed word compare),
    else it moves one away from zero by its own sign (a zero quotient moves up). When the quotient does not fit a signed word (DIVS overflow) the dividend register is left
    unchanged, so 'remainder' is the high word of num and 'quotient' the low word of num: the result is the low word of num, moved by one when the high word is not smaller than h."""
    h = abs(d) >> 1
    if d == -32768:
        h = -16384                       # neg.w of -32768 stays -32768, the shift gives -16384
    q = trunc_div(num, d)
    if -32768 <= q <= 32767:
        r = abs(num - q * d)
        qq = q
    else:
        STATS["overflow"] += 1
        r = s16(num >> 16)
        if r < 0:
            r = s16(-r)
        qq = s16(num)
    if h > r:
        return qq
    return qq + 1 if qq >= 0 else qq - 1


def corners(M, alt_long):
    y = s16(alt_long >> 16) >> 2           # high word of V:4AB1E, arithmetic shift right by 2
    out = []
    for x, z in CORNERS:
        c = []
        for r in range(3):
            s = x * M[3 * r] + y * M[3 * r + 1] + z * M[3 * r + 2]
            c.append(s16((s >> 8) + ((s >> 7) & 1)))      # shift right by 8, round half up
        out.append(tuple(c))
    return out


class State:
    lastpt = (0, 0, 0)                    # V:4AB7E: the last intersection candidate; its z is V:4AB82


def isect(plane, O, C):
    """Intersection of the segment from the other corner O to the current corner C with a pyramid plane ('R' x = z, 'L' -x = z, 'T' y = z,
    'B' -y = z). Returns None when the denominator is zero (no candidate stored), else True when the candidate lies inside the pyramid."""
    Xo, Yo, Zo = O
    X, Y, Zc = C
    if plane in "RL":
        if plane == "L":
            Xo, X = -Xo, -X
        fo = s16(Zo - Xo)
        d = s16(s16(X - Zc) + fo)
        if d == 0:
            return None
        y2 = s16(Yo - rdiv(s16(Yo - Y) * fo, d))
        z2 = s16(Zo - rdiv(s16(Zo - Zc) * fo, d))
        x2 = z2 if plane == "R" else -z2
    else:
        if plane == "B":
            Yo, Y = -Yo, -Y
        fo = s16(Zo - Yo)
        d = s16(s16(Y - Zc) + fo)
        if d == 0:
            return None
        x2 = s16(Xo - rdiv(s16(Xo - X) * fo, d))
        z2 = s16(Zo - rdiv(s16(Zo - Zc) * fo, d))
        y2 = z2 if plane == "T" else -z2
    State.lastpt = (x2, y2, z2)
    return z2 >= 0 and x2 <= z2 and -x2 <= z2 and y2 <= z2 and -y2 <= z2


def project(pt):
    x, y, z = pt
    half = lambda q: (q >> 1) + (q & 1)
    sx = half(trunc_div(x * 320, z)) + 160
    sy = half(trunc_div(y * 180, z)) + 90
    return min(max(sx, 0), 319), min(max(sy, 0), 179)


def find(corn):
    P = {0: corn[0], 2: corn[1], 4: corn[2], 6: corn[3]}
    cnt = [0] * 4            # 0 top (y = z), 1 camera-right (x = z, screen left), 2 bottom, 3 camera-left (screen right)
    slot = [0] * 4
    scr = {}
    State.lastpt = (0, 0, 0)
    skip = False
    for d0 in range(8):
        if d0 & 1:
            d3 = 0 if d0 + 1 > 7 else d0 + 1
            d1 = d0 - 1
        else:
            d3 = d0
            d1 = 0 if d0 + 2 > 7 else d0 + 2
        if skip:
            skip = False
            continue
        C, O = P[d3], P[d1]
        X, Y, Z = C
        x1, y1 = X + 1, Y + 1
        Xo, Yo, Zo = O
        lz = lambda: State.lastpt[2]
        I = lambda p: bool(isect(p, O, C))

        def stage_y():
            if y1 < 0:
                if -y1 < Z or Zo <= -Yo: return "N"
                if I("B"): return 2
                if lz() >= 0 or y1 < Z or Zo <= Yo: return "N"
                return 0 if I("T") else "N"
            if y1 < Z or Zo <= Yo: return "N"
            if I("T"): return 0
            if lz() >= 0 or -y1 < Z or Zo <= -Yo: return "N"
            return 2 if I("B") else "N"

        def stage_x2():
            if x1 < 0:
                if -x1 < Z or Zo <= -Xo: return "N"
                if I("L"): return 3
                if lz() >= 0 or x1 < Z or Zo <= Xo: return "N"
                return 1 if I("R") else "N"
            if x1 < Z or Zo <= Xo: return "N"
            if I("R"): return 1
            if lz() >= 0 or -x1 < Z or Zo <= -Xo: return "N"
            return 3 if I("L") else "N"

        if x1 >= Z:
            if Zo <= Xo: res = "N"
            elif I("R"): res = 1
            elif lz() >= 0 or Zo <= -Xo: res = stage_y()
            elif I("L"): res = 3
            else: res = stage_y()
        elif -x1 >= Z:
            if Zo <= -Xo: res = "N"
            elif I("L"): res = 3
            elif lz() >= 0 or Zo <= Xo: res = stage_y()
            elif I("R"): res = 1
            else: res = stage_y()
        elif y1 >= Z:
            if Zo <= Yo: res = "N"
            elif I("T"): res = 0
            elif lz() >= 0 or Zo <= -Yo: res = stage_x2()
            elif I("B"): res = 2
            else: res = stage_x2()
        elif -y1 >= Z:
            if Zo <= -Yo: res = "N"
            elif I("B"): res = 2
            elif lz() >= 0 or Zo <= Yo: res = stage_x2()
            elif I("T"): res = 0
            else: res = stage_x2()
        else:
            res = "N" if Z < 0 else "I"
        if res == "N":
            scr[d0] = (0, 0)
        elif res == "I":
            skip = bool(d0 & 1)
        else:
            cnt[res] += 1
            slot[res] = d0
            scr[d0] = project(State.lastpt)
    return cnt, slot, scr


TL, TR, BR, BL = (0, 0), (319, 0), (319, 179), (0, 179)


def polygon(corn, inv, roll, cockpit=True, pitch=0):
    """inv = bit 1 of V:4A982; roll = V:4AB4A; pitch = V:4AB42 (cockpit) or V:4AB4C (other views); cockpit = V:4A83D is zero."""
    cnt, slot, scr = find(corn)
    sp = lambda i: (319 - scr[slot[i]][0], 179 - scr[slot[i]][1])
    if cnt[1]:
        if cnt[3]:
            return [sp(1), sp(3), BR, BL] if (cockpit and inv) else [sp(1), sp(3), TR, TL]
        if cnt[0]:
            return [sp(1), sp(0), TR, BR, BL] if inv else [sp(1), sp(0), TL]
        if cnt[2]:
            return [sp(1), sp(2), BL] if inv else [sp(1), sp(2), BR, TR, TL]
        return None
    if cnt[0]:
        if cnt[3]:
            return [sp(0), sp(3), BR, BL, TL] if inv else [sp(0), sp(3), TR]
        if cnt[2]:
            return [sp(0), sp(2), BL, TL] if roll >= 0x3840 else [sp(0), sp(2), BR, TR]
        return None
    if not cnt[2]:
        return "FULL" if pitch > 0x3840 else None
    if cnt[3]:
        return [sp(2), sp(3), BR] if inv else [sp(2), sp(3), TR, TL, BL]
    return None
