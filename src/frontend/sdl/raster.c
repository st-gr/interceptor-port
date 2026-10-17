#include "raster.h"
#include <math.h>
#include <string.h>

#define WIN_X0 0
#define WIN_X1 (RASTER_W - 1)
#define WIN_Y0 1
/* The horizon of a camera with pitch p and roll r: a point (dx, dy) of the window (pixels from the centre) lies on it when
 * (dx / 160) sin r + (dy / 90) cos r = tan p, because the focal constants are 160 horizontally and 90 vertically (note 03,
 * 'Projection'); multiplied by 90 this is dx sin r * (90 / 160) + dy cos r = 90 tan p. */
#define HORIZON_XSCALE (90.0 / 160.0)

void raster_clear(raster_t *r, uint8_t colour)
{
    memset(r->px, colour, sizeof r->px);
}

void raster_set_row_limit(raster_t *r, int row_limit)
{
    if (row_limit < 0) row_limit = 0;
    if (row_limit > RASTER_H - 1) row_limit = RASTER_H - 1;
    r->row_limit = row_limit;
}

/* Per-pixel plane update: new = ((old | set) & ~clr) ^ tog on the low four bits (pens 16 and up count as 0). */
typedef struct { unsigned set, clr, tog; int verbatim; } plane_op_t;   /* verbatim >= 0: store that value (full-mask solid pens, also 16+) */

static plane_op_t solid_op(unsigned mask, unsigned pen)
{
    plane_op_t o;
    mask &= 15; pen &= 15;
    o.set = pen & mask; o.clr = mask & ~pen & 15; o.tog = 0; o.verbatim = -1;
    return o;
}

static void put_px(raster_t *r, int x, int y, const plane_op_t *o)
{
    uint8_t *p;
    if (x < WIN_X0 || x > WIN_X1 || y < WIN_Y0 || y > r->row_limit) return;
    p = &r->px[y * RASTER_W + x];
    if (o->verbatim >= 0) { *p = (uint8_t)o->verbatim; return; }
    *p = (uint8_t)((((*p & 15u) | o->set) & ~o->clr & 15u) ^ o->tog);
}

/* The plane operation of a fill state (note 03, "Primitive output"): solid (pattern < 0): per plane k of the mask the pen bit k sets (1) or clears (0); pattern mode: bit k of
 * the pattern sets (1) or clears (0) plane k, bit k of the toggle toggles it instead. */
static plane_op_t fill_op(unsigned mask, unsigned pen, int pattern, unsigned toggle)
{
    plane_op_t op;
    if (pattern < 0) return solid_op(mask, pen);
    {
        int k;
        op.set = op.clr = op.tog = 0; op.verbatim = -1;
        for (k = 0; k < 4; k++) {
            unsigned b = 1u << k;
            if (!(mask & b)) continue;
            if (toggle & b) op.tog |= b;
            else if ((unsigned)pattern & b) op.set |= b;
            else op.clr |= b;
        }
    }
    return op;
}

static int blit_polygon(raster_t *r, uint8_t *bits, const int16_t *xy, int n, unsigned tiny_pen, unsigned tiny_mask, int keep_mask, int A);
static void blit_line(raster_t *r, uint8_t *bits, const plane_op_t *op, int xs, int ys, int dx, int dy1, int rows_left, int one_dot);

/* Every polygon the draw stage hands over goes through the blitter filler of note 04 (edges XOR-ed into one plane, the inclusive fill from the odd edge pixel to the next);
 * the pixels it covers take the plane operation of the fill state. */
int raster_fill_polygon_planes(raster_t *r, const int16_t *xy, int n, unsigned mask, unsigned pen, int pattern, unsigned toggle)
{
    static uint8_t bits[RASTER_W * RASTER_H];
    plane_op_t op = fill_op(mask, pen, pattern, toggle);
    int y, x;
    if (blit_polygon(r, bits, xy, n, pen, mask, 0, r->row_limit) != 0) return 1;
    for (y = WIN_Y0; y <= r->row_limit; y++)
        for (x = 0; x < RASTER_W; x++)
            if (bits[y * RASTER_W + x]) put_px(r, x, y, &op);
    return 0;
}

int raster_fill_polygon_blit(raster_t *r, const int16_t *xy, int n, unsigned mask, unsigned pen)
{
    return raster_fill_polygon_planes(r, xy, n, mask, pen, -1, 0);
}

void raster_fill_polygon(raster_t *r, const int16_t *xy, int n, uint8_t colour)
{
    raster_fill_polygon_planes(r, xy, n, 15, colour, -1, 0);
}

/* A line of the object drawer (the line routine 0x3455E, a one-pixel blitter line; note 03, "The raster line"): the start is the end with the smaller y (equal y: the first end, (x0, y0)), drawn from
 * (x of the start end, y of the start end + 1), so a horizontal line lies on the row below its y; dy1 = |y1 - y0| - 1 (0 for equal y), dx the signed x difference to the other end, the major axis x
 * when |dx| >= dy1, the error word 4 minor - 2 major with its sign flag stepping the minor axis, every pixel written (major + 1 of them), cut at the last row L (blit_line). */
void raster_line_planes(raster_t *r, int x0, int y0, int x1, int y1, unsigned mask, unsigned pen)
{
    plane_op_t op = solid_op(mask, pen);
    int t, dy1;
    if (y0 > y1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }   /* equal y: the first end stays the start */
    if (y0 + 1 > r->row_limit) return;
    dy1 = y1 - y0 - 1; if (dy1 < 0) dy1 = 0;
    blit_line(r, NULL, &op, x0, y0 + 1, x1 - x0, dy1, r->row_limit - (y0 + 1), 0);
}

void raster_line(raster_t *r, int x0, int y0, int x1, int y1, uint8_t colour)
{
    raster_line_planes(r, x0, y0, x1, y1, 15, colour);
}

void raster_point_sprite(raster_t *r, int x, int y, int sprite, unsigned mask, unsigned pen)
{
    plane_op_t op = solid_op(mask, pen);
    if (y <= 0) return;
    if (sprite == 2 && y >= r->row_limit) sprite = 1;
    if (sprite == 0) { put_px(r, x, y, &op); return; }
    if (sprite == 1) {
        put_px(r, x - 1, y, &op); put_px(r, x, y, &op);
        if (x == 0) put_px(r, RASTER_W - 1, y - 1, &op);
        return;
    }
    {
        int c0 = (x % 16 == 0) ? x : x - 1;
        put_px(r, c0, y, &op); put_px(r, c0 + 1, y, &op);
        put_px(r, c0, y + 1, &op); put_px(r, c0 + 1, y + 1, &op);
    }
}

/* Half widths h of the first R rows of the disc (note 03, "The disc generator", 0x33CAC): two cursors over the pair array, top from pair 0 and
 * bottom from pair R; Bresenham midpoint state x = 0, y = R, d = 3 - 2R. While x < y: write x at the top cursor, then y at the bottom cursor;
 * d < 0: d += 4x + 6, else d += 4(x - y) + 10, y-- and the top cursor advances (the only place it does); the bottom cursor goes back; x++.
 * After the loop x = y writes x at the top cursor. R = 1 is the single pair 1. */
static void disc_halves(int R, int *h)
{
    int pair[130] = { 0 }, top = 0, bot = R, x = 0, y = R, d = 3 - 2 * R, i;
    if (R == 1) { h[0] = 1; return; }
    while (x < y) {
        pair[top] = x;
        pair[bot] = y;
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; top++; }
        bot--;
        x++;
    }
    if (x == y) pair[top] = x;
    for (i = 0; i < R; i++) h[i] = pair[i];
}

void raster_disc(raster_t *r, int x, int y, int R, unsigned mask, unsigned pen)
{
    int h[128], i;
    plane_op_t op = solid_op(mask, pen);
    if (R > 127) R = 127;
    if (R <= 0) { raster_point_sprite(r, x, y, 1, mask, pen); return; }
    disc_halves(R, h);
    for (i = 0; i < 2 * R; i++) {
        int row = y - R + 1 + i, hw = i < R ? h[i] : h[2 * R - 1 - i], xx;
        if (row < WIN_Y0 || row > r->row_limit) continue;
        for (xx = x - hw; xx <= x + hw; xx++) put_px(r, xx, row, &op);
    }
}

/* ---- the window base and the sky polygon (note 04 "Sky fill and ground polygon": the window clear, the polygon filler 0x34CD6) --------------------------- */

void raster_window_base(raster_t *r, uint8_t pen)
{
    int y;
    for (y = WIN_Y0; y <= r->row_limit; y++) memset(&r->px[y * RASTER_W], pen, RASTER_W);
}

static int iabs_(int v) { return v < 0 ? -v : v; }

/* The blitter's edge line into the one-plane bitmap bits (XOR) or, with `bits` NULL, applying the op to the pen of the pixels in px (the mask/pen line of the object drawer and the tiny-polygon line). The line runs
 * from (xs, ys) with the signed x difference dx and dy1 more rows (note 04, "Filling the polygon", step (2)): a Bresenham line with major axis x when |dx| >= dy1
 * and y otherwise, the error term 4 minor - 2 major with a 16-bit sign flag, length + 1 pixels cut to the rows left a = L - ys + 1... `rows_left` is the number of
 * rows below the first row that may still be drawn (L - first row); one_dot: a shallow line writes only its first pixel on each row. */
static void blit_line(raster_t *r, uint8_t *bits, const plane_op_t *op, int xs, int ys, int dx, int dy1, int rows_left, int one_dot)
{
    int adx = iabs_(dx), major = adx >= dy1 ? adx : dy1, minor = adx >= dy1 ? dy1 : adx;
    const int shallow = adx >= dy1;
    int length = major, step, neg, term, x = xs, y = ys, sx = dx < 0 ? -1 : 1;
    if (shallow) {
        if (dy1 > rows_left) {   /* round half up of major * a / dy1, halved from twice the truncated quotient */
            int q = (int)(((int64_t)2 * major * rows_left) / dy1);
            length = (q >> 1) + (q & 1);
        }
    } else if (rows_left < length) length = rows_left;
    if (length < 0) return;
    term = (int16_t)(4 * minor - 2 * major);
    neg = term < 0;
    for (step = 0; step <= length; step++) {
        int new_row = (step == 0);
        if (step > 0) {
            if (shallow) { x += sx; if (!neg) { y++; new_row = 1; } }
            else { y++; new_row = 1; if (!neg) x += sx; }
            term = (int16_t)(term + (neg ? 4 * minor : 4 * (minor - major)));
            neg = term < 0;
        }
        if (one_dot && shallow && !new_row) continue;
        if (x < 0 || x >= RASTER_W || y < 0 || y >= RASTER_H) continue;
        if (op) put_px(r, x, y, op);
        else bits[y * RASTER_W + x] ^= 1;
    }
}

/* The tiny polygons of the filler (0x34CD6): a box of at most two rows or one column draws a line, a block of dots or a dot instead of the fill. */
static void tiny_dot(raster_t *r, int x, int y, unsigned pen, unsigned mask)
{
    uint8_t *p;
    if (y < 0 || y >= RASTER_H) return;
    if (x < 0) { x += RASTER_W; y--; if (y < 0) return; }   /* column -1 wraps to the end of the row above */
    if (x >= RASTER_W) return;
    p = &r->px[y * RASTER_W + x];
    mask &= 15;
    *p = (uint8_t)((((*p) & 15u) & ~mask) | (pen & mask));
}

/* The polygon filler 0x34CD6 of note 04 ("Filling the polygon"): the tiny cases draw straight into the pixels (pen replaces the pixel for a line, the planes of mask
 * take their bit of pen for dots); the general fill leaves the polygon's pixels set in `bits` and returns 0; anything that draws nothing or a tiny case returns 1. */
static int blit_polygon(raster_t *r, uint8_t *bits, const int16_t *xy, int n, unsigned tiny_pen, unsigned tiny_mask, int keep_mask, int A)
{
    const int L = r->row_limit;   /* V:4AA3C; A is the limit of the entry (V:4AA3C, or 199 for the ball's: note 04 "The two entries of the filler") */
    int minx, maxx, miny, maxy, h, w, i, y, x;
    if (n < 3 || n > RASTER_MAX_VERTS) return 1;
    minx = maxx = xy[0]; miny = maxy = xy[1];
    for (i = 1; i < n; i++) {
        if (xy[2 * i] < minx) minx = xy[2 * i];
        if (xy[2 * i] > maxx) maxx = xy[2 * i];
        if (xy[2 * i + 1] < miny) miny = xy[2 * i + 1];
        if (xy[2 * i + 1] > maxy) maxy = xy[2 * i + 1];
    }
    h = maxy - miny; w = maxx - minx;
    if (miny > A) return 1;
    if (minx < 0 || maxx >= RASTER_W || miny < 0 || maxy >= RASTER_H) return 1;   /* the clipper keeps the vertices inside the 320 x 180 space */
    if (!((h >= 3 && w >= 2) || (h == 2 && w >= 3))) {
        if ((h >= 3 && w <= 1) || (h <= 1 && w >= 2)) {   /* a line in the colour V:4AA0C, all four planes (the plane mask is forced to 0xF while V:4A85A is 0; the near-ground pass sets it: keep_mask, the mask stays) */
            if (miny + 1 > L) return 1;
            plane_op_t lop;
            if (keep_mask) lop = solid_op(tiny_mask, tiny_pen);                                 /* stage [90]: mask 4, colour 3: plane 2 cleared */
            else { lop = solid_op(15, tiny_pen); lop.verbatim = (int)(tiny_pen & 15); }         /* the pen replaces the pixel */
            if (h <= 1) { for (x = minx; x <= maxx; x++) put_px(r, x, miny + 1, &lop); }
            else blit_line(r, NULL, &lop, minx, miny + 1, maxx - minx, h - 1, L - (miny + 1), 0);
        } else if (h == 2) {                                /* a 2 by 2 block of dots */
            if (miny + 1 > A) return 1;
            if (miny + 1 >= L) { tiny_dot(r, maxx, miny + 1, tiny_pen, tiny_mask); tiny_dot(r, maxx - 1, miny + 1, tiny_pen, tiny_mask); }   /* the pair is chosen by V:4AA3C, the exit by A */
            else {
                int c0 = (maxx % 16 == 0) ? maxx : maxx - 1;
                for (y = miny + 1; y <= miny + 2; y++) { tiny_dot(r, c0, y, tiny_pen, tiny_mask); tiny_dot(r, c0 + 1, y, tiny_pen, tiny_mask); }
            }
        } else {                                            /* h <= 1 and w <= 1: one dot, two when w = 1 */
            if (miny + 1 > A) return 1;
            tiny_dot(r, minx, miny + 1, tiny_pen, tiny_mask);
            if (w == 1) tiny_dot(r, maxx, miny + 1, tiny_pen, tiny_mask);
        }
        return 1;
    }
    /* the general fill: the edges, XOR into one plane */
    memset(bits, 0, (size_t)RASTER_W * RASTER_H);
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n, x0 = xy[2 * i], y0 = xy[2 * i + 1], x1 = xy[2 * j], y1 = xy[2 * j + 1];
        if (y0 == y1) continue;
        if (y0 > y1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        if (y0 + 1 > A) continue;                           /* an edge whose first row is below the limit is not drawn */
        blit_line(r, bits, NULL, x0, y0 + 1, x1 - x0, y1 - y0 - 1, A - (y0 + 1), 1);
    }
    /* the inclusive fill, right to left, the carry cleared per row, over the whole words from the word of max(minx - 1, 0) to the word of maxx */
    {
        int xl = (minx - 1 < 0 ? 0 : minx - 1) & ~15, xr = (maxx & ~15) + 15, ylast = maxy < A ? maxy : A;
        for (y = miny; y <= ylast; y++) {
            int carry = 0;
            for (x = (xr < RASTER_W ? xr : RASTER_W - 1); x >= xl; x--) {
                int in = bits[y * RASTER_W + x];
                bits[y * RASTER_W + x] = (uint8_t)(in | carry);
                carry ^= in;
            }
        }
    }
    return 0;
}

/* The alternate blit 0x34EEC (note 04, "The blit's C channel"): BLTCON0 0x0FCA, descending, D = A ? B : C with A the filled area, B plane 3, C = D = plane 2. BLTCMOD still holds 40 (left by the last edge line),
 * the other channels' moduli are 40 - 2 W, and a descending blit subtracts each channel's own modulo, so for the row r of the rectangle counted up from its bottom row and its word i counted from the right, C is read at
 * the plane-2 address of the bottom-right word minus r (40 + 2 W) minus 2 i (W words further left on every row up; below the start of plane 2 the read comes from the end of plane 1, the planes of a page lying 8,000
 * bytes apart, 40 bytes per row). The C words are the planes before the blit. The rectangle is the polygon's rows (miny to min(maxy, L)) and the words from the one of max(minx - 1, 0) to the one of maxx. A pixel under
 * the filled area gets plane 2 := its plane 3, any other pixel of the rectangle plane 2 := the bit of its C word at the same position; the pixels outside the rectangle keep their planes.
 * The C reads that fall before the start of plane 2 come from plane 1 of the same page as it stands before the pass (note 04, probe on 144 states): row 0 of every plane is zero, and the rows below the window hold
 * the panel of the page (plane 1's last rows are the instrument panel's), which `page_below` supplies (the previous frame's panel, since the panel is drawn after the world); with no page_below they read 0.
 * NOTE-GAP (C): in the external views (L = 167 or 179) the rows below L were not captured. The rows written are the polygon's own, row 0 included (question Q4).
 * NOTE-GAP (C): an alternate blit with no edge line drawn before it (BLTCMOD would be the previous blit's value) is not modelled: every triangle of the pass has edge lines. */
static void alt_blit(raster_t *r, const uint8_t *bits, const int16_t *xy, int n)
{
    static uint8_t old[RASTER_W * RASTER_H];
    int minx = xy[0], maxx = xy[0], miny = xy[1], maxy = xy[1], i, y, x;
    const int L = r->row_limit;
    for (i = 1; i < n; i++) {
        if (xy[2 * i] < minx) minx = xy[2 * i];
        if (xy[2 * i] > maxx) maxx = xy[2 * i];
        if (xy[2 * i + 1] < miny) miny = xy[2 * i + 1];
        if (xy[2 * i + 1] > maxy) maxy = xy[2 * i + 1];
    }
    {
        const int wl = (minx - 1 < 0 ? 0 : minx - 1) >> 4, wr = maxx >> 4, W = wr - wl + 1, ylast = maxy < L ? maxy : L;
        memcpy(old, r->px, sizeof old);
        memset(old, 0, RASTER_W);                                  /* row 0 of every plane is zero */
        for (y = L + 1; y < RASTER_H; y++) {                       /* the rows below the window: the page's panel */
            if (r->page_below) memcpy(&old[y * RASTER_W], &r->page_below[y * RASTER_W], RASTER_W);
            else memset(&old[y * RASTER_W], 0, RASTER_W);
        }
        for (y = miny; y <= ylast; y++) {                       /* note 04 states no clamp to the window's first row, and none is made: the rows are the polygon's */
            const int row = ylast - y;                          /* r: counted up from the bottom row */
            for (x = wl * 16; x < (wr + 1) * 16; x++) {
                uint8_t *p = &r->px[y * RASTER_W + x];
                unsigned v = *p & 15u, plane2;
                if (bits[y * RASTER_W + x]) plane2 = (v >> 3) & 1u;
                else {
                    const int wi = wr - (x >> 4);               /* i: counted from the right */
                    long o = (long)(ylast * 40 + 2 * wr) - (long)row * (40 + 2 * W) - 2L * wi;   /* byte offset of the C word from the start of plane 2 */
                    int plane = 2, cy, cx;
                    while (o < 0 && plane >= 0) { o += 8000; plane--; }
                    if (plane < 0) plane2 = 0;
                    else {
                        cy = (int)(o / 40); cx = (int)(o % 40) * 8 + (x & 15);
                        plane2 = cy < RASTER_H ? (old[cy * RASTER_W + cx] >> plane) & 1u : 0u;
                    }
                }
                *p = (uint8_t)((v & ~4u) | (plane2 << 2));
            }
        }
    }
}

/* The near-ground pass's shapes (stage [90], note 04): a triangle (xy: three vertices) goes through the filler's cases with the pass's block (mask 4, pattern 0, colour 3, V:4A85A set). The line, dot and block cases clear
 * plane 2 of their pixels; the general fill (alt != 0) takes the alternate blit 0x34EEC (alt_blit), otherwise it clears plane 2 under the area. */
void raster_ground_triangle(raster_t *r, const int16_t *xy, int alt)
{
    static uint8_t bits[RASTER_W * RASTER_H];
    int y, x;
    if (blit_polygon(r, bits, xy, 3, 3, 4, 1, r->row_limit) != 0) return;
    if (alt) { alt_blit(r, bits, xy, 3); return; }
    for (y = WIN_Y0; y <= r->row_limit; y++)
        for (x = 0; x < RASTER_W; x++)
            if (bits[y * RASTER_W + x]) r->px[y * RASTER_W + x] = (uint8_t)((r->px[y * RASTER_W + x] & 15u) & ~4u);
}

/* The filler's alternate path 0x34EEC for a general fill (see alt_blit). The small shapes are not this path's (they are drawn with the object's mask and pen as any polygon's). */
void raster_fill_alt(raster_t *r, const int16_t *xy, int n, unsigned mask, unsigned pen)
{
    static uint8_t bits[RASTER_W * RASTER_H];
    if (blit_polygon(r, bits, xy, n, pen, mask, 0, r->row_limit) != 0) return;
    alt_blit(r, bits, xy, n);
}

int raster_blit_cover(const int16_t *xy, int n, int row_limit, uint8_t *bits)
{
    raster_t tmp;
    tmp.row_limit = row_limit;
    return blit_polygon(&tmp, bits, xy, n, 0, 0, 0, row_limit);
}

int raster_ball_fill(raster_t *r, const int16_t *xy, int n, uint8_t *bits)
{
    return blit_polygon(r, bits, xy, n, 3, 15, 0, 199);   /* entry 0x34CD0: A = 199; colour 3 and mask 0xF as the panel routines leave them (note 04, "The attitude ball's tiny cases") */
}

int raster_sky_fill(raster_t *r, const int16_t *xy, int n, int sea, unsigned tiny_pen, unsigned tiny_mask)
{
    static uint8_t bits[RASTER_W * RASTER_H];
    int y, x;
    (void)sea;
    if (blit_polygon(r, bits, xy, n, tiny_pen, tiny_mask, 0, r->row_limit) != 0) return 1;
    for (y = WIN_Y0; y <= r->row_limit; y++)
        for (x = 0; x < RASTER_W; x++)
            if (bits[y * RASTER_W + x]) r->px[y * RASTER_W + x] = 5;   /* plane 0 set, plane 1 cleared over the sea: the sky pen 5 on either base */
    return 0;
}

void raster_sky_scene(raster_t *r, const game_sky_t *s)
{
    int sea = s->valid && s->sea_zone, k;
    raster_window_base(r, sea ? 6 : 4);
    if (!s->valid) return;
    raster_sky_fill(r, s->sky_xy, s->sky_n, sea, s->tiny_pen, s->tiny_mask);
    for (k = 0; k < s->sea_count && k < GAME_MAX_SEA_POLYS; k++)
        raster_fill_polygon_blit(r, s->sea[k].xy, s->sea[k].n, 2, 6);
}

/* Sutherland-Hodgman clip of the window rectangle by the half plane g(x, y) <= 0, g linear: sky for c > 0 is f(x, y + 1) <= 0 so that the
 * polygon's edge is the last sky row; for c < 0 (inverted) the sky starts below the edge row and g is f itself. */
int raster_sky_polygon(int32_t pitch, int32_t roll, int16_t *xy)
{
    const double k = 2.0 * 3.14159265358979323846 / RASTER_ANGLE_CIRCLE;
    double p = (double)(pitch % RASTER_ANGLE_CIRCLE) * k;
    double a = (double)(roll % RASTER_ANGLE_CIRCLE) * k;
    double s = sin(a) * HORIZON_XSCALE, c = cos(a), d;
    double shift = c > 0 ? 1.0 : 0.0;
    double vx[4] = { 0, 319, 319, 0 }, vy[4] = { 0, 0, 179, 179 };
    double ox[8], oy[8];
    int cut[8], n = 0, i;

    if (fabs(cos(p)) < 1e-9) d = sin(p) > 0 ? 1e9 : -1e9;
    else d = 90.0 * tan(p);
    for (i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        double ga = (vx[i] - 159) * s + (vy[i] + shift - 90) * c - d;
        double gb = (vx[j] - 159) * s + (vy[j] + shift - 90) * c - d;
        int ina = ga <= 1e-9, inb = gb <= 1e-9;
        if (ina && n < 8) { ox[n] = vx[i]; oy[n] = vy[i]; cut[n++] = 0; }
        if (ina != inb && n < 8) {
            double t = ga / (ga - gb);
            ox[n] = vx[i] + t * (vx[j] - vx[i]);
            oy[n] = vy[i] + t * (vy[j] - vy[i]);
            cut[n++] = 1;
        }
    }
    for (i = 0; i < n; i++) {
        int x, y;
        if (!cut[i]) { x = (int)ox[i]; y = (int)oy[i]; }
        else {
            x = (int)floor(ox[i] + 0.5);
            y = c > 0 ? (int)ceil(oy[i] - 1e-9) : (int)floor(oy[i] + 1e-9);
        }
        xy[2 * i] = (int16_t)(x < 0 ? 0 : x > 319 ? 319 : x);
        xy[2 * i + 1] = (int16_t)(y < 0 ? 0 : y > 179 ? 179 : y);
    }
    return n < 3 ? 0 : n;
}
