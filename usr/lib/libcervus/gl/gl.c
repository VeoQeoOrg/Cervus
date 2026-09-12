#include "glinternal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

glctx_t *gl_ctx;

static void mat_identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat_mul(float *out, const float *a, const float *b)
{
    float t[16];
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            t[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0]
                         + a[1 * 4 + r] * b[c * 4 + 1]
                         + a[2 * 4 + r] * b[c * 4 + 2]
                         + a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
    memcpy(out, t, sizeof t);
}

static float *cur_matrix(void)
{
    return (gl_ctx->mode == GL_PROJECTION) ? gl_ctx->pr : gl_ctx->mv;
}

int glCreateContext(int width, int height)
{
    if (width <= 0 || height <= 0) return -1;
    glDestroyContext();

    glctx_t *c = calloc(1, sizeof(glctx_t));
    if (!c) return -1;

    c->color = malloc((size_t)width * height * sizeof(uint32_t));
    c->depth = malloc((size_t)width * height * sizeof(float));
    c->span_min = malloc((size_t)height * sizeof(int));
    c->span_max = malloc((size_t)height * sizeof(int));
    if (!c->color || !c->depth || !c->span_min || !c->span_max) {
        free(c->color);
        free(c->depth);
        free(c->span_min);
        free(c->span_max);
        free(c);
        return -1;
    }

    c->w = width;
    c->h = height;
    c->vx = 0; c->vy = 0; c->vw = width; c->vh = height;

    mat_identity(c->mv);
    mat_identity(c->pr);
    c->mode = GL_MODELVIEW;

    c->cur_color[0] = c->cur_color[1] = c->cur_color[2] = c->cur_color[3] = 1.0f;
    c->cur_normal[2] = 1.0f;
    c->shade = GL_SMOOTH;
    c->cull_face = GL_BACK;
    c->front_face = GL_CCW;

    c->global_ambient[0] = c->global_ambient[1] = c->global_ambient[2] = 0.2f;
    c->global_ambient[3] = 1.0f;

    for (int i = 0; i < GL_MAX_LIGHTS; i++) {
        c->light[i].position[2] = 1.0f;
        c->light[i].position[3] = 0.0f;
        c->light[i].ambient[3] = 1.0f;
        c->light[i].specular[3] = 1.0f;
        c->light[i].diffuse[3] = 1.0f;
        if (i == 0) {
            c->light[i].diffuse[0] = c->light[i].diffuse[1] = c->light[i].diffuse[2] = 1.0f;
            c->light[i].specular[0] = c->light[i].specular[1] = c->light[i].specular[2] = 1.0f;
        }
    }

    c->mat_ambient[0] = c->mat_ambient[1] = c->mat_ambient[2] = 0.2f;
    c->mat_ambient[3] = 1.0f;
    c->mat_diffuse[0] = c->mat_diffuse[1] = c->mat_diffuse[2] = 0.8f;
    c->mat_diffuse[3] = 1.0f;
    c->mat_specular[3] = 1.0f;
    c->mat_emission[3] = 1.0f;
    c->mat_shininess = 0.0f;

    gl_ctx = c;
    glResetDirty();
    return 0;
}

void glDestroyContext(void)
{
    if (!gl_ctx) return;
    free(gl_ctx->color);
    free(gl_ctx->depth);
    free(gl_ctx->span_min);
    free(gl_ctx->span_max);
    free(gl_ctx);
    gl_ctx = NULL;
}

const uint32_t *glColorBuffer(void) { return gl_ctx ? gl_ctx->color : NULL; }
int glBufferWidth(void)  { return gl_ctx ? gl_ctx->w : 0; }
int glBufferHeight(void) { return gl_ctx ? gl_ctx->h : 0; }

long glTrianglesDrawn(void) { return gl_ctx ? gl_ctx->tri_count : 0; }
void glResetCounters(void)  { if (gl_ctx) gl_ctx->tri_count = 0; }

void glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    if (!gl_ctx) return;
    gl_ctx->vx = x; gl_ctx->vy = y;
    gl_ctx->vw = w; gl_ctx->vh = h;
}

void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    (void)a;
    if (!gl_ctx) return;
    gl_ctx->clear[0] = r; gl_ctx->clear[1] = g; gl_ctx->clear[2] = b;
}

static uint32_t pack_rgb(float r, float g, float b)
{
    int ri = (int)(r * 255.0f + 0.5f);
    int gi = (int)(g * 255.0f + 0.5f);
    int bi = (int)(b * 255.0f + 0.5f);
    if (ri < 0) ri = 0; else if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; else if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; else if (bi > 255) bi = 255;
    return ((uint32_t)ri << 16) | ((uint32_t)gi << 8) | (uint32_t)bi;
}

void glClearRegion(GLbitfield mask, int x0, int y0, int x1, int y1)
{
    glctx_t *c = gl_ctx;
    if (!c) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    if (x0 >= x1 || y0 >= y1) return;

    int run = x1 - x0;

    if (mask & GL_COLOR_BUFFER_BIT) {
        uint32_t v = pack_rgb(c->clear[0], c->clear[1], c->clear[2]);
        for (int y = y0; y < y1; y++) {
            uint32_t *p = c->color + (size_t)y * c->w + x0;
            if (v == 0) {
                memset(p, 0, (size_t)run * sizeof(uint32_t));
            } else {
                for (int i = 0; i < run; i++) p[i] = v;
            }
        }
    }
    if (mask & GL_DEPTH_BUFFER_BIT) {
        for (int y = y0; y < y1; y++) {
            float *p = c->depth + (size_t)y * c->w + x0;
            for (int i = 0; i < run; i++) p[i] = 1.0f;
        }
    }
}

void glClear(GLbitfield mask)
{
    if (!gl_ctx) return;
    glClearRegion(mask, 0, 0, gl_ctx->w, gl_ctx->h);
}

void glResetDirty(void)
{
    glctx_t *c = gl_ctx;
    if (!c) return;
    c->dx0 = c->w; c->dy0 = c->h;
    c->dx1 = 0;    c->dy1 = 0;
    for (int y = 0; y < c->h; y++) {
        c->span_min[y] = c->w;
        c->span_max[y] = 0;
    }
}

const int *glDirtySpanMin(void) { return gl_ctx ? gl_ctx->span_min : NULL; }
const int *glDirtySpanMax(void) { return gl_ctx ? gl_ctx->span_max : NULL; }

int glDirtyRect(int *x0, int *y0, int *x1, int *y1)
{
    glctx_t *c = gl_ctx;
    if (!c) return 0;
    if (c->dx1 <= c->dx0 || c->dy1 <= c->dy0) return 0;
    if (x0) *x0 = c->dx0;
    if (y0) *y0 = c->dy0;
    if (x1) *x1 = c->dx1;
    if (y1) *y1 = c->dy1;
    return 1;
}

void glMatrixMode(GLenum mode) { if (gl_ctx) gl_ctx->mode = (int)mode; }

void glLoadIdentity(void) { if (gl_ctx) mat_identity(cur_matrix()); }

void glLoadMatrixf(const GLfloat *m)
{
    if (gl_ctx && m) memcpy(cur_matrix(), m, 16 * sizeof(float));
}

void glMultMatrixf(const GLfloat *m)
{
    if (!gl_ctx || !m) return;
    float *d = cur_matrix();
    mat_mul(d, d, m);
}

void glPushMatrix(void)
{
    glctx_t *c = gl_ctx;
    if (!c) return;
    if (c->mode == GL_PROJECTION) {
        if (c->pr_sp < GL_MAX_STACK) memcpy(c->pr_stack[c->pr_sp++], c->pr, sizeof c->pr);
    } else {
        if (c->mv_sp < GL_MAX_STACK) memcpy(c->mv_stack[c->mv_sp++], c->mv, sizeof c->mv);
    }
}

void glPopMatrix(void)
{
    glctx_t *c = gl_ctx;
    if (!c) return;
    if (c->mode == GL_PROJECTION) {
        if (c->pr_sp > 0) memcpy(c->pr, c->pr_stack[--c->pr_sp], sizeof c->pr);
    } else {
        if (c->mv_sp > 0) memcpy(c->mv, c->mv_stack[--c->mv_sp], sizeof c->mv);
    }
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    float m[16];
    mat_identity(m);
    m[12] = x; m[13] = y; m[14] = z;
    glMultMatrixf(m);
}

void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    float m[16];
    mat_identity(m);
    m[0] = x; m[5] = y; m[10] = z;
    glMultMatrixf(m);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    float len = sqrtf(x * x + y * y + z * z);
    if (len < 1e-8f) return;
    x /= len; y /= len; z /= len;

    float a = angle * 3.14159265358979f / 180.0f;
    float s = sinf(a), co = cosf(a), t = 1.0f - co;

    float m[16];
    mat_identity(m);
    m[0]  = t * x * x + co;
    m[1]  = t * x * y + s * z;
    m[2]  = t * x * z - s * y;
    m[4]  = t * x * y - s * z;
    m[5]  = t * y * y + co;
    m[6]  = t * y * z + s * x;
    m[8]  = t * x * z + s * y;
    m[9]  = t * y * z - s * x;
    m[10] = t * z * z + co;
    glMultMatrixf(m);
}

void glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    float m[16];
    memset(m, 0, sizeof m);
    m[0]  = (float)(2.0 * n / (r - l));
    m[5]  = (float)(2.0 * n / (t - b));
    m[8]  = (float)((r + l) / (r - l));
    m[9]  = (float)((t + b) / (t - b));
    m[10] = (float)(-(f + n) / (f - n));
    m[11] = -1.0f;
    m[14] = (float)(-2.0 * f * n / (f - n));
    glMultMatrixf(m);
}

void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    float m[16];
    mat_identity(m);
    m[0]  = (float)(2.0 / (r - l));
    m[5]  = (float)(2.0 / (t - b));
    m[10] = (float)(-2.0 / (f - n));
    m[12] = (float)(-(r + l) / (r - l));
    m[13] = (float)(-(t + b) / (t - b));
    m[14] = (float)(-(f + n) / (f - n));
    glMultMatrixf(m);
}

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zn, GLdouble zf)
{
    double h = zn * tan(fovy * 3.14159265358979 / 360.0);
    double w = h * aspect;
    glFrustum(-w, w, -h, h, zn, zf);
}

void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez,
               GLdouble cx, GLdouble cy, GLdouble cz,
               GLdouble ux, GLdouble uy, GLdouble uz)
{
    float f[3] = { (float)(cx - ex), (float)(cy - ey), (float)(cz - ez) };
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl < 1e-8f) return;
    f[0] /= fl; f[1] /= fl; f[2] /= fl;

    float up[3] = { (float)ux, (float)uy, (float)uz };
    float s[3] = { f[1] * up[2] - f[2] * up[1],
                   f[2] * up[0] - f[0] * up[2],
                   f[0] * up[1] - f[1] * up[0] };
    float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (sl < 1e-8f) return;
    s[0] /= sl; s[1] /= sl; s[2] /= sl;

    float u[3] = { s[1] * f[2] - s[2] * f[1],
                   s[2] * f[0] - s[0] * f[2],
                   s[0] * f[1] - s[1] * f[0] };

    float m[16];
    mat_identity(m);
    m[0] = s[0]; m[4] = s[1]; m[8]  = s[2];
    m[1] = u[0]; m[5] = u[1]; m[9]  = u[2];
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2];
    glMultMatrixf(m);
    glTranslatef((float)-ex, (float)-ey, (float)-ez);
}

void glEnable(GLenum cap)
{
    glctx_t *c = gl_ctx;
    if (!c) return;
    switch (cap) {
        case GL_DEPTH_TEST: c->depth_test = 1; break;
        case GL_CULL_FACE:  c->cull = 1; break;
        case GL_LIGHTING:   c->lighting = 1; break;
        case GL_NORMALIZE:  c->normalize = 1; break;
        case GL_COLOR_MATERIAL: c->color_material = 1; break;
        default:
            if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + GL_MAX_LIGHTS)
                c->light[cap - GL_LIGHT0].on = 1;
            break;
    }
}

void glDisable(GLenum cap)
{
    glctx_t *c = gl_ctx;
    if (!c) return;
    switch (cap) {
        case GL_DEPTH_TEST: c->depth_test = 0; break;
        case GL_CULL_FACE:  c->cull = 0; break;
        case GL_LIGHTING:   c->lighting = 0; break;
        case GL_NORMALIZE:  c->normalize = 0; break;
        case GL_COLOR_MATERIAL: c->color_material = 0; break;
        default:
            if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + GL_MAX_LIGHTS)
                c->light[cap - GL_LIGHT0].on = 0;
            break;
    }
}

void glShadeModel(GLenum m) { if (gl_ctx) gl_ctx->shade = (int)m; }
void glCullFace(GLenum m)   { if (gl_ctx) gl_ctx->cull_face = (int)m; }
void glFrontFace(GLenum m)  { if (gl_ctx) gl_ctx->front_face = (int)m; }

static void xform_point(const float *m, const float *v, float *out)
{
    out[0] = m[0] * v[0] + m[4] * v[1] + m[8]  * v[2] + m[12];
    out[1] = m[1] * v[0] + m[5] * v[1] + m[9]  * v[2] + m[13];
    out[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14];
}

void glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    glctx_t *c = gl_ctx;
    if (!c || !params) return;
    if (light < GL_LIGHT0 || light >= GL_LIGHT0 + GL_MAX_LIGHTS) return;
    gllight_t *l = &c->light[light - GL_LIGHT0];

    switch (pname) {
        case GL_POSITION: {
            float p[3] = { params[0], params[1], params[2] };
            if (params[3] == 0.0f) {
                l->position[0] = c->mv[0] * p[0] + c->mv[4] * p[1] + c->mv[8]  * p[2];
                l->position[1] = c->mv[1] * p[0] + c->mv[5] * p[1] + c->mv[9]  * p[2];
                l->position[2] = c->mv[2] * p[0] + c->mv[6] * p[1] + c->mv[10] * p[2];
            } else {
                xform_point(c->mv, p, l->position);
            }
            l->position[3] = params[3];
            break;
        }
        case GL_AMBIENT:  memcpy(l->ambient,  params, 4 * sizeof(float)); break;
        case GL_DIFFUSE:  memcpy(l->diffuse,  params, 4 * sizeof(float)); break;
        case GL_SPECULAR: memcpy(l->specular, params, 4 * sizeof(float)); break;
        default: break;
    }
}

void glLightf(GLenum light, GLenum pname, GLfloat param)
{
    float v[4] = { param, param, param, param };
    glLightfv(light, pname, v);
}

void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    glctx_t *c = gl_ctx;
    (void)face;
    if (!c || !params) return;
    switch (pname) {
        case GL_AMBIENT:  memcpy(c->mat_ambient, params, 4 * sizeof(float)); break;
        case GL_DIFFUSE:  memcpy(c->mat_diffuse, params, 4 * sizeof(float)); break;
        case GL_AMBIENT_AND_DIFFUSE:
            memcpy(c->mat_ambient, params, 4 * sizeof(float));
            memcpy(c->mat_diffuse, params, 4 * sizeof(float));
            break;
        case GL_SPECULAR: memcpy(c->mat_specular, params, 4 * sizeof(float)); break;
        case GL_EMISSION: memcpy(c->mat_emission, params, 4 * sizeof(float)); break;
        case GL_SHININESS: c->mat_shininess = params[0]; break;
        default: break;
    }
}

void glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    float v[4] = { param, param, param, param };
    glMaterialfv(face, pname, v);
}

void glNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (!gl_ctx) return;
    gl_ctx->cur_normal[0] = x;
    gl_ctx->cur_normal[1] = y;
    gl_ctx->cur_normal[2] = z;
}

void glNormal3fv(const GLfloat *v) { if (v) glNormal3f(v[0], v[1], v[2]); }

void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    if (!gl_ctx) return;
    gl_ctx->cur_color[0] = r;
    gl_ctx->cur_color[1] = g;
    gl_ctx->cur_color[2] = b;
    gl_ctx->cur_color[3] = a;
}

void glColor3f(GLfloat r, GLfloat g, GLfloat b) { glColor4f(r, g, b, 1.0f); }
void glColor3fv(const GLfloat *v) { if (v) glColor4f(v[0], v[1], v[2], 1.0f); }

static void shade_vertex(glctx_t *c, const float *eye, const float *nrm, float *out)
{
    const float *ma = c->color_material ? c->cur_color : c->mat_ambient;
    const float *md = c->color_material ? c->cur_color : c->mat_diffuse;

    float r = c->mat_emission[0] + ma[0] * c->global_ambient[0];
    float g = c->mat_emission[1] + ma[1] * c->global_ambient[1];
    float b = c->mat_emission[2] + ma[2] * c->global_ambient[2];

    for (int i = 0; i < GL_MAX_LIGHTS; i++) {
        gllight_t *l = &c->light[i];
        if (!l->on) continue;

        float ld[3];
        if (l->position[3] == 0.0f) {
            ld[0] = l->position[0];
            ld[1] = l->position[1];
            ld[2] = l->position[2];
        } else {
            ld[0] = l->position[0] - eye[0];
            ld[1] = l->position[1] - eye[1];
            ld[2] = l->position[2] - eye[2];
        }
        float il = ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2];
        if (il > 1e-12f) {
            float inv = 1.0f / sqrtf(il);
            ld[0] *= inv; ld[1] *= inv; ld[2] *= inv;
        }

        float ndotl = nrm[0] * ld[0] + nrm[1] * ld[1] + nrm[2] * ld[2];

        r += ma[0] * l->ambient[0];
        g += ma[1] * l->ambient[1];
        b += ma[2] * l->ambient[2];

        if (ndotl > 0.0f) {
            r += md[0] * l->diffuse[0] * ndotl;
            g += md[1] * l->diffuse[1] * ndotl;
            b += md[2] * l->diffuse[2] * ndotl;

            if (c->mat_shininess > 0.0f) {
                float el = eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2];
                float vx = 0.0f, vy = 0.0f, vz = 1.0f;
                if (el > 1e-12f) {
                    float inv = 1.0f / sqrtf(el);
                    vx = -eye[0] * inv; vy = -eye[1] * inv; vz = -eye[2] * inv;
                }
                float hx = ld[0] + vx, hy = ld[1] + vy, hz = ld[2] + vz;
                float hl = hx * hx + hy * hy + hz * hz;
                if (hl > 1e-12f) {
                    float inv = 1.0f / sqrtf(hl);
                    float ndoth = nrm[0] * hx * inv + nrm[1] * hy * inv + nrm[2] * hz * inv;
                    if (ndoth > 0.0f) {
                        float sp = (float)pow((double)ndoth, (double)c->mat_shininess);
                        r += c->mat_specular[0] * l->specular[0] * sp;
                        g += c->mat_specular[1] * l->specular[1] * sp;
                        b += c->mat_specular[2] * l->specular[2] * sp;
                    }
                }
            }
        }
    }

    out[0] = r > 1.0f ? 1.0f : r;
    out[1] = g > 1.0f ? 1.0f : g;
    out[2] = b > 1.0f ? 1.0f : b;
}

static void emit_triangle(glctx_t *c, const glvert_t *a, const glvert_t *b, const glvert_t *d);

void glBegin(GLenum mode)
{
    if (!gl_ctx) return;
    gl_ctx->prim = (int)mode;
    gl_ctx->vcount = 0;
    gl_ctx->strip_parity = 0;
}

static void flush_primitive(glctx_t *c)
{
    switch (c->prim) {
        case GL_TRIANGLES:
            if (c->vcount == 3) {
                emit_triangle(c, &c->vbuf[0], &c->vbuf[1], &c->vbuf[2]);
                c->vcount = 0;
            }
            break;
        case GL_QUADS:
            if (c->vcount == 4) {
                emit_triangle(c, &c->vbuf[0], &c->vbuf[1], &c->vbuf[2]);
                emit_triangle(c, &c->vbuf[0], &c->vbuf[2], &c->vbuf[3]);
                c->vcount = 0;
            }
            break;
        case GL_TRIANGLE_STRIP:
            if (c->vcount == 3) {
                if (c->strip_parity)
                    emit_triangle(c, &c->vbuf[1], &c->vbuf[0], &c->vbuf[2]);
                else
                    emit_triangle(c, &c->vbuf[0], &c->vbuf[1], &c->vbuf[2]);
                c->strip_parity ^= 1;
                c->vbuf[0] = c->vbuf[1];
                c->vbuf[1] = c->vbuf[2];
                c->vcount = 2;
            }
            break;
        case GL_QUAD_STRIP:
            if (c->vcount == 4) {
                emit_triangle(c, &c->vbuf[0], &c->vbuf[1], &c->vbuf[2]);
                emit_triangle(c, &c->vbuf[2], &c->vbuf[1], &c->vbuf[3]);
                c->vbuf[0] = c->vbuf[2];
                c->vbuf[1] = c->vbuf[3];
                c->vcount = 2;
            }
            break;
        case GL_TRIANGLE_FAN:
        case GL_POLYGON:
            if (c->vcount == 3) {
                emit_triangle(c, &c->vbuf[0], &c->vbuf[1], &c->vbuf[2]);
                c->vbuf[1] = c->vbuf[2];
                c->vcount = 2;
            }
            break;
        default:
            c->vcount = 0;
            break;
    }
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    glctx_t *c = gl_ctx;
    if (!c || c->vcount >= GL_MAX_VERTS) return;

    glvert_t *v = &c->vbuf[c->vcount];
    float obj[3] = { x, y, z };

    xform_point(c->mv, obj, v->eye);

    const float *m = c->pr;
    const float *e = v->eye;
    v->clip[0] = m[0] * e[0] + m[4] * e[1] + m[8]  * e[2] + m[12];
    v->clip[1] = m[1] * e[0] + m[5] * e[1] + m[9]  * e[2] + m[13];
    v->clip[2] = m[2] * e[0] + m[6] * e[1] + m[10] * e[2] + m[14];
    v->clip[3] = m[3] * e[0] + m[7] * e[1] + m[11] * e[2] + m[15];

    if (c->lighting) {
        const float *mv = c->mv;
        const float *n = c->cur_normal;
        float nx = mv[0] * n[0] + mv[4] * n[1] + mv[8]  * n[2];
        float ny = mv[1] * n[0] + mv[5] * n[1] + mv[9]  * n[2];
        float nz = mv[2] * n[0] + mv[6] * n[1] + mv[10] * n[2];
        float nl = nx * nx + ny * ny + nz * nz;
        if (nl > 1e-12f) {
            float inv = 1.0f / sqrtf(nl);
            nx *= inv; ny *= inv; nz *= inv;
        }
        float nrm[3] = { nx, ny, nz };
        shade_vertex(c, v->eye, nrm, v->color);
    } else {
        v->color[0] = c->cur_color[0];
        v->color[1] = c->cur_color[1];
        v->color[2] = c->cur_color[2];
    }

    c->vcount++;
    flush_primitive(c);
}

void glVertex3fv(const GLfloat *v) { if (v) glVertex3f(v[0], v[1], v[2]); }
void glVertex2f(GLfloat x, GLfloat y) { glVertex3f(x, y, 0.0f); }

void glEnd(void)
{
    if (!gl_ctx) return;
    gl_ctx->vcount = 0;
}

static void lerp_vert(glvert_t *out, const glvert_t *a, const glvert_t *b, float t)
{
    for (int i = 0; i < 4; i++) out->clip[i] = a->clip[i] + (b->clip[i] - a->clip[i]) * t;
    for (int i = 0; i < 3; i++) out->eye[i] = a->eye[i] + (b->eye[i] - a->eye[i]) * t;
    for (int i = 0; i < 3; i++) out->color[i] = a->color[i] + (b->color[i] - a->color[i]) * t;
}

static void to_screen(const glctx_t *c, const glvert_t *v, rvert_t *out)
{
    float inv = 1.0f / v->clip[3];
    float nx = v->clip[0] * inv;
    float ny = v->clip[1] * inv;
    float nz = v->clip[2] * inv;

    out->x = (float)c->vx + (nx * 0.5f + 0.5f) * (float)c->vw;
    out->y = (float)c->vy + (0.5f - ny * 0.5f) * (float)c->vh;
    out->z = nz * 0.5f + 0.5f;
    out->r = v->color[0];
    out->g = v->color[1];
    out->b = v->color[2];
}

static void raster_clipped(glctx_t *c, const glvert_t *a, const glvert_t *b, const glvert_t *d)
{
    rvert_t s0, s1, s2;
    to_screen(c, a, &s0);
    to_screen(c, b, &s1);
    to_screen(c, d, &s2);

    if (c->cull) {
        float area = (s1.x - s0.x) * (s2.y - s0.y) - (s2.x - s0.x) * (s1.y - s0.y);
        int front = (c->front_face == GL_CCW) ? (area < 0.0f) : (area > 0.0f);
        if (c->cull_face == GL_BACK && !front) return;
        if (c->cull_face == GL_FRONT && front) return;
        if (c->cull_face == GL_FRONT_AND_BACK) return;
    }

    if (c->shade == GL_FLAT) {
        s1.r = s2.r = s0.r;
        s1.g = s2.g = s0.g;
        s1.b = s2.b = s0.b;
    }

    c->tri_count++;
    gl_raster_triangle(c, &s0, &s1, &s2);
}

static void emit_triangle(glctx_t *c, const glvert_t *a, const glvert_t *b, const glvert_t *d)
{
    const glvert_t *in[3] = { a, b, d };

    float dist[3];
    int inside = 0;
    for (int i = 0; i < 3; i++) {
        dist[i] = in[i]->clip[3] + in[i]->clip[2];
        if (dist[i] > 0.0f) inside++;
    }

    if (inside == 3) {
        raster_clipped(c, a, b, d);
        return;
    }
    if (inside == 0) return;

    glvert_t poly[4];
    int n = 0;
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        float di = dist[i], dj = dist[j];
        if (di > 0.0f) poly[n++] = *in[i];
        if ((di > 0.0f) != (dj > 0.0f)) {
            float t = di / (di - dj);
            lerp_vert(&poly[n++], in[i], in[j], t);
        }
        if (n >= 4) break;
    }

    for (int i = 2; i < n; i++)
        raster_clipped(c, &poly[0], &poly[i - 1], &poly[i]);
}

void glFlush(void) {}
void glFinish(void) {}
