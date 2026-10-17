/* src/core/mapmarks.c - the map view's entity marks (see mapmarks.h; docs/re/06-cockpit-hud.md "Map renderer").
 * The projection is the note's: depth Z = cy * 128 / 256, X = (168 x_rel) >> 8 and Y = (252 z_rel) >> 8 with 16-bit wrapping on the high words; the projector 0x3377C rejects a point
 * unless |X| < Z and |Y| < Z (word compares) and maps it to column 319 - (160 + trunc(160 X / Z)) and row 180 - (90 + trunc(90 Y / Z)) (the call row plus one, checked against V:4AA3C). */
#include <string.h>
#include "mapmarks.h"

static int w16(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v & 0xFFFFu); }

void mapmarks_init(mapmarks_t *m) {
    memset(m, 0, sizeof *m);
    memset(m->b71, 0xFF, sizeof m->b71);
}

/* 0x3377C as the map calls it: returns 1 and the screen point, or 0 when the point is rejected. */
static int project(const mapmarks_in_t *in, int32_t px, int32_t pz, int *sx, int *sy) {
    const int Z = w16((w16(in->cam_y >> 16) * 128) >> 8);
    const int X = w16((168 * w16((px >> 16) - (in->cam_x >> 16))) >> 8);
    const int Y = w16((252 * w16((pz >> 16) - (in->cam_z >> 16))) >> 8);
    if (Z <= 0) return 0;
    if (X >= Z || Y >= Z || w16(-X) >= Z || w16(-Y) >= Z) return 0;
    int c = (160 * X) / Z + 160, r = (90 * Y) / Z + 90;   /* C division truncates toward zero, as divs.w does */
    if (c < 0) c = 0;
    if (c > 319) c = 319;
    if (r < 0) r = 0;
    if (r > 179) r = 179;
    *sx = 319 - c;
    *sy = 179 - r + 1;
    if (*sy > in->last_row) return 0;
    return 1;
}

/* The heading sector (0x30142): (heading - 900) in steps of 1800 starting from 1, a negative start giving 0 and a sector beyond 15 giving 0. */
static int sector_of(int16_t heading) {
    int d = (int16_t)(heading - 900), s = 0;
    if (d < 0) return 0;
    do {
        s++;
        if (s > 15) return 0;
        d = (int16_t)(d - 1800);
    } while (d >= 0);
    return s;
}

/* The wrapped difference of two nibbles as 0x300F4 .. 0x30102 compute it: the signed byte difference reduced into -8 .. 7. */
static int nib_diff(int cur, int stored) {
    int d = (cur & 15) - stored;
    if (d >= 0) { if (d >= 8) d -= 16; }
    else if (d <= -8) d += 16;
    return d;
}

void mapmarks_pass(mapmarks_t *m, const mapmarks_in_t *in) {
    m->count = 0;
    for (int i = 0; i < MAPMARKS_RECORDS; i++) {
        const mapmarks_rec_t *r = &in->rec[i];
        if (!(r->w0 & 0x0040)) continue;                              /* 0x2FE34: drawn only with bit 0x40 of word 0 */
        if ((r->w0 & 0x0008) && (r->b03 & 0x80)) continue;            /* hidden: on the ground (0x2FE3C) */
        int sx, sy;
        if (!project(in, r->x, r->z, &sx, &sy)) continue;             /* a failed projection draws nothing */
        if (i != 0 && (r->cls & 0xF0) == 0x20) {                      /* the ship routine 0x3001C: no V:4A86D test, no frame test, no store; not with V:4A90F negative */
            if (in->v4a90f < 0) continue;
            mapmark_t *k = &m->mark[m->count++];
            k->x = (int16_t)sx; k->y = (int16_t)sy; k->rec = (uint8_t)i; k->shape = MAPMARKS_SHIP; k->pen = 3;
            continue;
        }
        int pen;
        if (i == 0) pen = 13;                                         /* 0x2FE6A: the player goes straight to pen 13, before the mode test */
        else if (in->mode == 3 && !(r->b20 & 0x40)) pen = 3;
        else pen = (r->w0 & 0x0008) ? 13 : 1;
        /* 0x30046 after the projection */
        int draw_stored = 0;
        if (!in->v4a86d) {
            if (in->v4a90f < 0) continue;                             /* no entity mark at all */
            if (in->v4a90f != 0 && (in->n & (uint16_t)(int16_t)in->v4a90f) == 0) { /* the direct path: refresh the store, draw nothing */ }
            else if ((int8_t)m->b71[i] >= 0) draw_stored = 1;         /* 0x300D8: a negative stored sector takes the direct path */
        }
        if (draw_stored) {
            const int cx = sx - nib_diff(sx, m->b70[i] >> 4), cy = sy - nib_diff(sy, m->b70[i] & 15);
            mapmark_t *k = &m->mark[m->count++];
            k->x = (int16_t)cx; k->y = (int16_t)cy; k->rec = (uint8_t)i; k->shape = m->b71[i]; k->pen = (uint8_t)pen;
            continue;
        }
        m->b71[i] = (uint8_t)sector_of(r->heading);                   /* 0x30164, 0x30178 */
        m->b70[i] = (uint8_t)(((sx & 15) << 4) | (sy & 15));
        if (!in->v4a86d) continue;                                    /* 0x3017C: with V:4A86D clear the direct path ends here */
        if (!(in->n & 1)) continue;                                   /* 0x30184: bit 0 of V:4A993 */
        mapmark_t *k = &m->mark[m->count++];
        k->x = (int16_t)sx; k->y = (int16_t)sy; k->rec = (uint8_t)i; k->shape = m->b71[i]; k->pen = (uint8_t)pen;
    }
}
