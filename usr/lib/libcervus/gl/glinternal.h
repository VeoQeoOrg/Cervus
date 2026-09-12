#ifndef _CERVUS_GL_INTERNAL_H
#define _CERVUS_GL_INTERNAL_H

#include <gl.h>

#define GL_MAX_STACK   32
#define GL_MAX_LIGHTS  4
#define GL_MAX_VERTS   16

typedef struct {
    float clip[4];
    float eye[3];
    float color[3];
} glvert_t;

typedef struct {
    float x, y, z;
    float r, g, b;
} rvert_t;

typedef struct {
    int   on;
    float position[4];
    float ambient[4];
    float diffuse[4];
    float specular[4];
} gllight_t;

typedef struct {
    int       w, h;
    uint32_t *color;
    float    *depth;

    int   vx, vy, vw, vh;
    float clear[3];

    float mv[16], pr[16];
    float mv_stack[GL_MAX_STACK][16];
    float pr_stack[GL_MAX_STACK][16];
    int   mv_sp, pr_sp;
    int   mode;

    float cur_normal[3];
    float cur_color[4];

    int shade;
    int depth_test;
    int cull;
    int cull_face;
    int front_face;
    int lighting;
    int normalize;
    int color_material;

    gllight_t light[GL_MAX_LIGHTS];
    float     global_ambient[4];

    float mat_ambient[4];
    float mat_diffuse[4];
    float mat_specular[4];
    float mat_emission[4];
    float mat_shininess;

    int      prim;
    glvert_t vbuf[GL_MAX_VERTS];
    int      vcount;
    int      strip_parity;

    long tri_count;

    int dx0, dy0, dx1, dy1;
    int *span_min;
    int *span_max;
} glctx_t;

extern glctx_t *gl_ctx;

void gl_raster_triangle(glctx_t *c, const rvert_t *a, const rvert_t *b, const rvert_t *d);

#endif
