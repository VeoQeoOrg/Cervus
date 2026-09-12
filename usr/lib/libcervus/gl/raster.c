#include "glinternal.h"

typedef struct {
    float x, dx;
    float z, dz;
    float r, dr;
    float g, dg;
    float b, db;
} edge_t;

static void edge_setup(edge_t *e, const rvert_t *a, const rvert_t *b, float ystart)
{
    float dy = b->y - a->y;
    float inv = (dy > 1e-6f || dy < -1e-6f) ? 1.0f / dy : 0.0f;

    e->dx = (b->x - a->x) * inv;
    e->dz = (b->z - a->z) * inv;
    e->dr = (b->r - a->r) * inv;
    e->dg = (b->g - a->g) * inv;
    e->db = (b->b - a->b) * inv;

    float step = ystart - a->y;
    e->x = a->x + e->dx * step;
    e->z = a->z + e->dz * step;
    e->r = a->r + e->dr * step;
    e->g = a->g + e->dg * step;
    e->b = a->b + e->db * step;
}

static inline void edge_advance(edge_t *e)
{
    e->x += e->dx;
    e->z += e->dz;
    e->r += e->dr;
    e->g += e->dg;
    e->b += e->db;
}

static inline uint32_t pack(float r, float g, float b)
{
    int ri = (int)(r * 255.0f);
    int gi = (int)(g * 255.0f);
    int bi = (int)(b * 255.0f);
    if (ri < 0) ri = 0; else if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; else if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; else if (bi > 255) bi = 255;
    return ((uint32_t)ri << 16) | ((uint32_t)gi << 8) | (uint32_t)bi;
}

static void span_flat(uint32_t *crow, float *zrow, int x0, int x1,
                      float z, float dz, uint32_t col, int depth_test)
{
    if (!depth_test) {
        for (int x = x0; x <= x1; x++) crow[x] = col;
        return;
    }
    for (int x = x0; x <= x1; x++) {
        if (z < zrow[x]) {
            zrow[x] = z;
            crow[x] = col;
        }
        z += dz;
    }
}

static void span_smooth(uint32_t *crow, float *zrow, int x0, int x1,
                        float z, float dz, float r, float dr,
                        float g, float dg, float b, float db, int depth_test)
{
    if (!depth_test) {
        for (int x = x0; x <= x1; x++) {
            crow[x] = pack(r, g, b);
            r += dr; g += dg; b += db;
        }
        return;
    }
    for (int x = x0; x <= x1; x++) {
        if (z < zrow[x]) {
            zrow[x] = z;
            crow[x] = pack(r, g, b);
        }
        z += dz; r += dr; g += dg; b += db;
    }
}

void gl_raster_triangle(glctx_t *c, const rvert_t *a, const rvert_t *b, const rvert_t *d)
{
    const rvert_t *v0 = a, *v1 = b, *v2 = d, *t;

    if (v0->y > v1->y) { t = v0; v0 = v1; v1 = t; }
    if (v1->y > v2->y) { t = v1; v1 = v2; v2 = t; }
    if (v0->y > v1->y) { t = v0; v0 = v1; v1 = t; }

    float ytop = v0->y, ybot = v2->y;
    if (ybot - ytop < 1e-6f) return;

    int clip_y0 = c->vy < 0 ? 0 : c->vy;
    int clip_y1 = c->vy + c->vh;
    if (clip_y1 > c->h) clip_y1 = c->h;
    int clip_x0 = c->vx < 0 ? 0 : c->vx;
    int clip_x1 = c->vx + c->vw;
    if (clip_x1 > c->w) clip_x1 = c->w;
    if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1) return;

    int y0 = (int)(ytop + 0.5f);
    int y2 = (int)(ybot + 0.5f);
    if (y0 < clip_y0) y0 = clip_y0;
    if (y2 > clip_y1) y2 = clip_y1;
    if (y0 >= y2) return;

    int y1 = (int)(v1->y + 0.5f);
    if (y1 < y0) y1 = y0;
    if (y1 > y2) y1 = y2;

    int flat = (a->r == b->r && a->r == d->r &&
                a->g == b->g && a->g == d->g &&
                a->b == b->b && a->b == d->b);
    uint32_t flat_col = flat ? pack(a->r, a->g, a->b) : 0;

    edge_t e_long, e_short;

    int depth_test = c->depth_test;
    int stride = c->w;

    for (int part = 0; part < 2; part++) {
        int ystart = part ? y1 : y0;
        int yend   = part ? y2 : y1;
        if (ystart >= yend) continue;

        edge_setup(&e_long, v0, v2, (float)ystart + 0.5f);
        if (part == 0) edge_setup(&e_short, v0, v1, (float)ystart + 0.5f);
        else           edge_setup(&e_short, v1, v2, (float)ystart + 0.5f);

        uint32_t *crow = c->color + (size_t)ystart * stride;
        float    *zrow = c->depth + (size_t)ystart * stride;

        for (int y = ystart; y < yend; y++) {
            edge_t *le = &e_long, *re = &e_short;
            if (e_short.x < e_long.x) { le = &e_short; re = &e_long; }

            float xl = le->x, xr = re->x;
            int ix0 = (int)(xl + 0.5f);
            int ix1 = (int)(xr - 0.5f);

            if (ix1 >= ix0) {
                float span = xr - xl;
                float inv = (span > 1e-6f) ? 1.0f / span : 0.0f;
                float dz = (re->z - le->z) * inv;
                float dr = (re->r - le->r) * inv;
                float dg = (re->g - le->g) * inv;
                float db = (re->b - le->b) * inv;

                float pre = ((float)ix0 + 0.5f) - xl;
                float z = le->z + dz * pre;
                float r = le->r + dr * pre;
                float g = le->g + dg * pre;
                float bb = le->b + db * pre;

                if (ix0 < clip_x0) {
                    float adj = (float)(clip_x0 - ix0);
                    z += dz * adj; r += dr * adj; g += dg * adj; bb += db * adj;
                    ix0 = clip_x0;
                }
                if (ix1 >= clip_x1) ix1 = clip_x1 - 1;

                if (ix1 >= ix0) {
                    if (ix0 < c->dx0) c->dx0 = ix0;
                    if (ix1 + 1 > c->dx1) c->dx1 = ix1 + 1;
                    if (y < c->dy0) c->dy0 = y;
                    if (y + 1 > c->dy1) c->dy1 = y + 1;
                    if (ix0 < c->span_min[y]) c->span_min[y] = ix0;
                    if (ix1 + 1 > c->span_max[y]) c->span_max[y] = ix1 + 1;
                    if (flat)
                        span_flat(crow, zrow, ix0, ix1, z, dz, flat_col, depth_test);
                    else
                        span_smooth(crow, zrow, ix0, ix1, z, dz,
                                    r, dr, g, dg, bb, db, depth_test);
                }
            }

            edge_advance(&e_long);
            edge_advance(&e_short);
            crow += stride;
            zrow += stride;
        }
    }
}
