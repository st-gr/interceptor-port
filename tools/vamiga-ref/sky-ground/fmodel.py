"""Model of the polygon fill 0x34CD6 (general path), the edge setup 0x3508A and the blitter work of the sky fill: XOR edge lines, then an inclusive
fill from right to left. fillpoly(poly, limit) returns (status, plane); the plane is the sky bit (plane 0) of rows 0..199.
status 'tiny' marks the point and line cases of the filler that are not modelled (note 04: bounding box at most 2 rows high or at most 1 column wide)."""
# Model of the polygon fill 0x34CD6 (general path), line setup 0x3508A and blitter line/fill.
def s16(v):
    v&=0xffff
    return v-0x10000 if v&0x8000 else v
def edge(plane,x0,y0,x1,y1,limit):
    """XOR the line pixels of one edge into plane (dict-of-rows list of 320 bool)"""
    if y1==y0: return
    if y1>y0:
        dy1=y1-y0-1; sr=y0+1; x=x0; dx=x1-x0
    else:
        dy1=y0-y1-1; sr=y1+1; x=x1; dx=x0-x1
    if sr>limit: return
    adx=abs(dx)
    shallow = adx>=dy1
    if dx<0: code=0x17 if shallow else 0x0B
    else: code=0x13 if shallow else 0x03
    if shallow: major,minor=adx,dy1
    else: major,minor=dy1,adx
    a=limit-sr
    if shallow:
        if dy1<=a: length=major
        else:
            q=(2*major*a)//minor
            length=(q>>1)+(q&1)
    else:
        length = a if major>a else major
    err=4*minor-2*major
    sign = err<0
    amod=4*(minor-major); bmod=4*minor
    sud=code&0x10; sul=code&8; aul=code&4
    apt=s16(err)
    y=sr; first=True
    for i in range(length+1):
        if first and 0<=x<320 and 0<=y<200:      # one dot (SING): only the first pixel after a row change is written
            plane[y][x]^=1
        first=False
        if sud:
            if not sign: y = y-1 if sul else y+1; first=True
            x = x-1 if aul else x+1
        else:
            y = y-1 if aul else y+1; first=True
            if not sign: x = x-1 if sul else x+1
        apt = s16(apt + (bmod if sign else amod))
        sign = apt<0
def fillpoly(poly,limit):
    """returns plane (rows list of 320 ints) after sky polygon fill into a cleared plane, or None if the tiny path"""
    pl=[[0]*320 for _ in range(200)]
    xs=[p[0] for p in poly]; ys=[p[1] for p in poly]
    minx,maxx,miny,maxy=min(xs),max(xs),min(ys),max(ys)
    if miny>limit: return 'skip',pl
    d7=maxy-miny
    if d7>2:
        if maxx-minx<=1: return 'tiny',pl
    elif d7==2:
        if maxx-minx<=2: return 'tiny',pl
    else:
        return 'tiny',pl
    n=len(poly)
    for i in range(n-1):
        edge(pl,*poly[i],*poly[i+1],limit)
    edge(pl,*poly[n-1],*poly[0],limit)
    xl=max(minx-1,0)
    wl=xl>>4; wr=maxx>>4
    bottom=min(maxy,limit)
    for y in range(miny,bottom+1):
        carry=0
        for w in range(wr,wl-1,-1):
            for b in range(16):   # bit b: pixel x = w*16+15-b
                x=w*16+15-b
                v=pl[y][x]
                out=v|carry
                if v: carry^=1
                pl[y][x]=out
    return 'ok',pl


def tiny(poly, limit, colour, mask):
    """The point and line cases of the polygon filler 0x34CD6 (bounding box h = maxy - miny, w = maxx - minx; the general fill when h >= 3 and w >= 2, or h = 2 and w >= 3).
    Returns None for the general fill, else a dict {(x, y): 'set' | 'dot'}: 'set' means the pixel becomes pen `colour` (all four planes, the line routine), 'dot' means every plane k of
    `mask` takes bit k of `colour` (pen = (old & ~mask) | (colour & mask)); rows below `limit` are dropped, nothing is drawn when miny > limit. The mask is V:4A79E and the
    colour V:4AA0C as the previous drawing left them."""
    xs = [p[0] for p in poly]; ys = [p[1] for p in poly]
    minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
    h, w = maxy - miny, maxx - minx
    if miny > limit:
        return {}
    if (h >= 3 and w >= 2) or (h == 2 and w >= 3):
        return None
    out = {}
    y1 = miny + 1
    if h >= 3 or (h <= 1 and w >= 2):                                  # line from (minx, miny) to (maxx, maxy), first row skipped, every pixel written
        if h <= 1:
            for x in range(minx, maxx + 1):
                if y1 <= limit: out[(x, y1)] = "set"
            return out
        dy1 = h - 1; dx = w; sr = miny + 1
        shallow = dx >= dy1
        major, minor = (dx, dy1) if shallow else (dy1, dx)
        a = limit - sr
        if shallow:
            length = major if dy1 <= a else ((2 * major * a) // minor + 1) >> 1
        else:
            length = a if major > a else major
        apt = s16(4 * minor - 2 * major); sign = apt < 0
        x, y = minx, sr
        for i in range(length + 1):
            out[(x, y)] = "set"
            if shallow:
                if not sign: y += 1
                x += 1
            else:
                y += 1
                if not sign: x += 1
            apt = s16(apt + (4 * minor if sign else 4 * (minor - major))); sign = apt < 0
        return out
    if h == 2:                                                         # 2 x 2 block at the right end of the box, rows miny + 1 and miny + 2
        x = maxx
        if y1 > limit:
            return out
        if y1 == limit:                                                # last row: the pair routine, dots at x and x - 1 (x - 1 = -1 wraps to the end of the row above)
            out[(x, y1)] = "dot"
            out[(x - 1, y1) if x > 0 else (319, y1 - 1)] = "dot"
            return out
        pair = (x, x + 1) if (x & 15) == 0 else (x - 1, x)           # columns x - 1, x, except x a multiple of 16: x, x + 1
        for y in (y1, y1 + 1):
            out[(pair[0], y)] = "dot"; out[(pair[1], y)] = "dot"
        return out
    if y1 <= limit:                                                    # h <= 1, w <= 1: one dot at minx, two dots when w = 1, on row miny + 1
        out[(minx, y1)] = "dot"
        if w == 1: out[(maxx, y1)] = "dot"
    return out
