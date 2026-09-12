#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <math.h>
#include <gl.h>
#include <sys/cervus.h>
#include <cervus_util.h>
#include <sys/ioctl.h>

#define TIOCSNONBLOCK 0x5481

typedef struct {
    float nx, ny, nz;
    float x, y, z;
} gvert_t;

typedef struct {
    gvert_t *v;
    int      n, cap;
    float    color[4];
} mesh_t;

static int mesh_room(mesh_t *m, int extra)
{
    if (m->n + extra <= m->cap) return 0;
    int cap = m->cap ? m->cap * 2 : 1024;
    while (cap < m->n + extra) cap *= 2;
    gvert_t *p = realloc(m->v, (size_t)cap * sizeof(gvert_t));
    if (!p) return -1;
    m->v = p;
    m->cap = cap;
    return 0;
}

static void push_v(mesh_t *m, float nx, float ny, float nz,
                   float x, float y, float z)
{
    if (mesh_room(m, 1) != 0) return;
    gvert_t *g = &m->v[m->n++];
    g->nx = nx; g->ny = ny; g->nz = nz;
    g->x = x;   g->y = y;   g->z = z;
}

static void push_quad(mesh_t *m, float nx, float ny, float nz,
                      const float *a, const float *b,
                      const float *c, const float *d)
{
    push_v(m, nx, ny, nz, a[0], a[1], a[2]);
    push_v(m, nx, ny, nz, b[0], b[1], b[2]);
    push_v(m, nx, ny, nz, c[0], c[1], c[2]);

    push_v(m, nx, ny, nz, a[0], a[1], a[2]);
    push_v(m, nx, ny, nz, c[0], c[1], c[2]);
    push_v(m, nx, ny, nz, d[0], d[1], d[2]);
}

static void quad_out(mesh_t *m, const float *a, const float *b,
                     const float *c, const float *d, const float *outward)
{
    float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    float v[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    float n[3] = { u[1] * v[2] - u[2] * v[1],
                   u[2] * v[0] - u[0] * v[2],
                   u[0] * v[1] - u[1] * v[0] };
    float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l < 1e-9f) return;
    n[0] /= l; n[1] /= l; n[2] /= l;

    if (n[0] * outward[0] + n[1] * outward[1] + n[2] * outward[2] < 0.0f) {
        n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2];
        push_quad(m, n[0], n[1], n[2], d, c, b, a);
    } else {
        push_quad(m, n[0], n[1], n[2], a, b, c, d);
    }
}

static void build_gear(mesh_t *m, float inner, float outer, float width,
                       int teeth, float tooth_depth)
{
    float r0 = inner;
    float r1 = outer - tooth_depth / 2.0f;
    float r2 = outer + tooth_depth / 2.0f;
    float da = 2.0f * 3.14159265358979f / (float)teeth / 4.0f;
    float hw = width * 0.5f;

    float zf[3] = { 0.0f, 0.0f, 1.0f };
    float zb[3] = { 0.0f, 0.0f, -1.0f };

    for (int i = 0; i < teeth; i++) {
        float a0 = (float)i * 2.0f * 3.14159265358979f / (float)teeth;
        float a1 = a0 + da, a2 = a0 + 2.0f * da;
        float a3 = a0 + 3.0f * da, a4 = a0 + 4.0f * da;

        float c0 = cosf(a0), s0 = sinf(a0);
        float c1 = cosf(a1), s1 = sinf(a1);
        float c2 = cosf(a2), s2 = sinf(a2);
        float c3 = cosf(a3), s3 = sinf(a3);
        float c4 = cosf(a4), s4 = sinf(a4);

        float i0f[3] = { r0 * c0, r0 * s0,  hw }, i0b[3] = { r0 * c0, r0 * s0, -hw };
        float i3f[3] = { r0 * c3, r0 * s3,  hw }, i3b[3] = { r0 * c3, r0 * s3, -hw };
        float i4f[3] = { r0 * c4, r0 * s4,  hw }, i4b[3] = { r0 * c4, r0 * s4, -hw };

        float b0f[3] = { r1 * c0, r1 * s0,  hw }, b0b[3] = { r1 * c0, r1 * s0, -hw };
        float b3f[3] = { r1 * c3, r1 * s3,  hw }, b3b[3] = { r1 * c3, r1 * s3, -hw };
        float b4f[3] = { r1 * c4, r1 * s4,  hw }, b4b[3] = { r1 * c4, r1 * s4, -hw };

        float t1f[3] = { r2 * c1, r2 * s1,  hw }, t1b[3] = { r2 * c1, r2 * s1, -hw };
        float t2f[3] = { r2 * c2, r2 * s2,  hw }, t2b[3] = { r2 * c2, r2 * s2, -hw };

        quad_out(m, i0f, b0f, b3f, i3f, zf);
        quad_out(m, i3f, b3f, b4f, i4f, zf);
        quad_out(m, i0b, b0b, b3b, i3b, zb);
        quad_out(m, i3b, b3b, b4b, i4b, zb);

        quad_out(m, b0f, t1f, t2f, b3f, zf);
        quad_out(m, b0b, t1b, t2b, b3b, zb);

        {
            float o[3] = { cosf(a0 + da * 0.5f), sinf(a0 + da * 0.5f), 0.0f };
            quad_out(m, b0f, b0b, t1b, t1f, o);
        }
        {
            float o[3] = { cosf(a1 + da * 0.5f), sinf(a1 + da * 0.5f), 0.0f };
            quad_out(m, t1f, t1b, t2b, t2f, o);
        }
        {
            float o[3] = { cosf(a2 + da * 0.5f), sinf(a2 + da * 0.5f), 0.0f };
            quad_out(m, t2f, t2b, b3b, b3f, o);
        }
        {
            float o[3] = { cosf(a3 + da * 0.5f), sinf(a3 + da * 0.5f), 0.0f };
            quad_out(m, b3f, b3b, b4b, b4f, o);
        }
        {
            float o[3] = { -cosf(a0 + da * 1.5f), -sinf(a0 + da * 1.5f), 0.0f };
            quad_out(m, i0f, i0b, i3b, i3f, o);
        }
        {
            float o[3] = { -cosf(a3 + da * 0.5f), -sinf(a3 + da * 0.5f), 0.0f };
            quad_out(m, i3f, i3b, i4b, i4f, o);
        }
    }
}

static void build_cube(mesh_t *m, float s)
{
    float h = s * 0.5f;
    float v[8][3] = {
        { -h, -h, -h }, {  h, -h, -h }, {  h,  h, -h }, { -h,  h, -h },
        { -h, -h,  h }, {  h, -h,  h }, {  h,  h,  h }, { -h,  h,  h },
    };
    float px[3] = { 1, 0, 0 }, nx[3] = { -1, 0, 0 };
    float py[3] = { 0, 1, 0 }, ny[3] = { 0, -1, 0 };
    float pz[3] = { 0, 0, 1 }, nz[3] = { 0, 0, -1 };
    quad_out(m, v[4], v[5], v[6], v[7], pz);
    quad_out(m, v[0], v[1], v[2], v[3], nz);
    quad_out(m, v[1], v[5], v[6], v[2], px);
    quad_out(m, v[0], v[4], v[7], v[3], nx);
    quad_out(m, v[3], v[2], v[6], v[7], py);
    quad_out(m, v[0], v[1], v[5], v[4], ny);
}

static void draw_mesh(const mesh_t *m)
{
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, m->color);
    glBegin(GL_TRIANGLES);
    const gvert_t *g = m->v;
    for (int i = 0; i < m->n; i++, g++) {
        glNormal3f(g->nx, g->ny, g->nz);
        glVertex3f(g->x, g->y, g->z);
    }
    glEnd();
}

static const unsigned char FONT[][8] = {
    {0,0,0,0,0,0,0,0},
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00},
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00},
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00},
    {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

static const unsigned char FONT_AZ[][8] = {
    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00},
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00},
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00},
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00},
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
    {0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00},
    {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00},
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00},
};

static const unsigned char *glyph_for(char ch)
{
    if (ch >= '0' && ch <= '9') return FONT[1 + (ch - '0')];
    if (ch >= 'A' && ch <= 'Z') return FONT_AZ[ch - 'A'];
    if (ch >= 'a' && ch <= 'z') return FONT_AZ[ch - 'a'];
    if (ch == '.') return FONT[11];
    if (ch == ':') return FONT[12];
    if (ch == '-') return FONT[13];
    return FONT[0];
}

static void draw_text(uint32_t *buf, int bw, int bh, int px, int py,
                      const char *s, uint32_t fg, int scale)
{
    for (; *s; s++) {
        const unsigned char *g = glyph_for(*s);
        for (int row = 0; row < 8; row++) {
            unsigned char bits = g[row];
            for (int col = 0; col < 8; col++) {
                if (!(bits & (0x80 >> col))) continue;
                for (int sy = 0; sy < scale; sy++) {
                    int y = py + row * scale + sy;
                    if (y < 0 || y >= bh) continue;
                    uint32_t *line = buf + (size_t)y * bw;
                    for (int sx = 0; sx < scale; sx++) {
                        int x = px + col * scale + sx;
                        if (x < 0 || x >= bw) continue;
                        line[x] = fg;
                    }
                }
            }
        }
        px += 8 * scale;
    }
}

static void draw_box(uint32_t *buf, int bw, int bh, int x0, int y0,
                     int w, int h, uint32_t col)
{
    for (int y = y0; y < y0 + h; y++) {
        if (y < 0 || y >= bh) continue;
        uint32_t *line = buf + (size_t)y * bw;
        for (int x = x0; x < x0 + w; x++) {
            if (x < 0 || x >= bw) continue;
            line[x] = col;
        }
    }
}

static void blit_rect(uint32_t *fb, int pitch32, int ox, int oy,
                      const uint32_t *src, int sw, int sh,
                      int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sw) x1 = sw;
    if (y1 > sh) y1 = sh;
    if (x0 >= x1 || y0 >= y1) return;
    size_t bytes = (size_t)(x1 - x0) * sizeof(uint32_t);
    for (int y = y0; y < y1; y++)
        memcpy(fb + (size_t)(oy + y) * pitch32 + ox + x0,
               src + (size_t)y * sw + x0, bytes);
}

static uint64_t now_ns(void)
{
    cervus_timespec_t ts;
    if (cervus_clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static const char USAGE[] =
    "Usage: gldemo [--gears|--cube] [--size WxH] [--full] [--frames N]\n"
    "A software OpenGL demo: the classic three gears, or a lit cube.\n"
    "\n"
    "  --gears       three meshing gears (default)\n"
    "  --cube        one rotating cube\n"
    "  --size WxH    render at this size instead of 800x600\n"
    "  --full        render at the full screen size\n"
    "  --frames N    stop after N frames and print the average\n"
    "  --flat        flat shading instead of smooth\n"
    "\n"
    "Keys: g gears, c cube, space pause, q quit.\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "gldemo")) return 0;
    argc = cervus_end_of_options(argc, argv);

    int scene = 0;
    int want_w = 800, want_h = 600;
    int full = 0;
    long frame_limit = 0;
    int flat = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--gears")) scene = 0;
        else if (!strcmp(argv[i], "--cube")) scene = 1;
        else if (!strcmp(argv[i], "--full")) full = 1;
        else if (!strcmp(argv[i], "--flat")) flat = 1;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frame_limit = atol(argv[++i]);
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            int w = 0, h = 0;
            if (sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                want_w = w; want_h = h;
            }
        }
    }

    cervus_fb_info_t fbi;
    if (cervus_fb_info(&fbi) != 0) {
        fputs("gldemo: no framebuffer\n", stderr);
        return 1;
    }

    uint32_t *fb = (uint32_t *)cervus_fb_map();
    if (!fb) {
        fputs("gldemo: cannot map the framebuffer\n", stderr);
        return 1;
    }

    int sw = (int)fbi.width, sh = (int)fbi.height;
    int pitch32 = (int)(fbi.pitch / 4);

    int rw = full ? sw : want_w;
    int rh = full ? sh : want_h;
    if (rw > sw) rw = sw;
    if (rh > sh) rh = sh;

    int ox = (sw - rw) / 2;
    int oy = (sh - rh) / 2;

    if (glCreateContext(rw, rh) != 0) {
        fputs("gldemo: out of memory\n", stderr);
        return 1;
    }

    mesh_t gear1, gear2, gear3, cube;
    memset(&gear1, 0, sizeof gear1);
    memset(&gear2, 0, sizeof gear2);
    memset(&gear3, 0, sizeof gear3);
    memset(&cube,  0, sizeof cube);

    build_gear(&gear1, 1.0f, 4.0f, 1.0f, 20, 0.7f);
    build_gear(&gear2, 0.5f, 2.0f, 2.0f, 10, 0.7f);
    build_gear(&gear3, 1.3f, 2.0f, 0.5f, 10, 0.7f);
    build_cube(&cube, 2.4f);

    gear1.color[0] = 0.8f; gear1.color[1] = 0.1f; gear1.color[2] = 0.0f; gear1.color[3] = 1.0f;
    gear2.color[0] = 0.0f; gear2.color[1] = 0.8f; gear2.color[2] = 0.2f; gear2.color[3] = 1.0f;
    gear3.color[0] = 0.2f; gear3.color[1] = 0.2f; gear3.color[2] = 1.0f; gear3.color[3] = 1.0f;
    cube.color[0]  = 0.9f; cube.color[1]  = 0.6f; cube.color[2]  = 0.1f; cube.color[3] = 1.0f;

    struct termios orig, raw;
    int have_tio = (tcgetattr(0, &orig) == 0);
    if (have_tio) {
        raw = orig;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSAFLUSH, &raw);
    }
    int nb = 1;
    ioctl(0, TIOCSNONBLOCK, &nb);

    cervus_fb_acquire();

    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++)
            fb[(size_t)y * pitch32 + x] = 0x101014;

    float lightpos[4] = { 5.0f, 5.0f, 10.0f, 0.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, lightpos);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glShadeModel(flat ? GL_FLAT : GL_SMOOTH);
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);

    glViewport(0, 0, rw, rh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    {
        double h = (double)rh / (double)rw;
        glFrustum(-1.0, 1.0, -h, h, 5.0, 80.0);
    }
    glMatrixMode(GL_MODELVIEW);

    float angle = 0.0f;
    float view_rotx = 20.0f, view_roty = 30.0f;

    uint64_t t_start = now_ns();
    uint64_t t_last = t_start;
    long frames = 0, frames_since = 0;
    float fps = 0.0f;
    long total_frames = 0;
    int paused = 0;
    int running = 1;
    int prev_valid = 0;
    int *pmin = malloc((size_t)rh * sizeof(int));
    int *pmax = malloc((size_t)rh * sizeof(int));
    if (!pmin || !pmax) { fputs("gldemo: out of memory\n", stderr); return 1; }
    for (int y = 0; y < rh; y++) { pmin[y] = 0; pmax[y] = 0; }

    while (running) {
        char key;
        while (read(0, &key, 1) == 1) {
            if (key == 'q' || key == 'Q' || key == 27) running = 0;
            else if (key == 'g' || key == 'G') scene = 0;
            else if (key == 'c' || key == 'C') scene = 1;
            else if (key == ' ') paused = !paused;
            else if (key == 'f' || key == 'F') {
                flat = !flat;
                glShadeModel(flat ? GL_FLAT : GL_SMOOTH);
            }
        }
        if (!running) break;

        if (!prev_valid) {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        } else {
            for (int y = 0; y < rh; y++)
                if (pmax[y] > pmin[y])
                    glClearRegion(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,
                                  pmin[y], y, pmax[y], y + 1);
        }
        glResetDirty();

        glLoadIdentity();
        glTranslatef(0.0f, 0.0f, -40.0f);
        glRotatef(view_rotx, 1.0f, 0.0f, 0.0f);
        glRotatef(view_roty, 0.0f, 1.0f, 0.0f);

        if (scene == 0) {
            glPushMatrix();
            glTranslatef(-3.0f, -2.0f, 0.0f);
            glRotatef(angle, 0.0f, 0.0f, 1.0f);
            draw_mesh(&gear1);
            glPopMatrix();

            glPushMatrix();
            glTranslatef(3.1f, -2.0f, 0.0f);
            glRotatef(-2.0f * angle - 9.0f, 0.0f, 0.0f, 1.0f);
            draw_mesh(&gear2);
            glPopMatrix();

            glPushMatrix();
            glTranslatef(-3.1f, 4.2f, 0.0f);
            glRotatef(-2.0f * angle - 25.0f, 0.0f, 0.0f, 1.0f);
            draw_mesh(&gear3);
            glPopMatrix();
        } else {
            glPushMatrix();
            glScalef(2.2f, 2.2f, 2.2f);
            glRotatef(angle, 0.3f, 1.0f, 0.15f);
            draw_mesh(&cube);
            glPopMatrix();
        }

        uint32_t *cbuf = (uint32_t *)glColorBuffer();

        const int *nmin = glDirtySpanMin();
        const int *nmax = glDirtySpanMax();

        char line[64];
        int whole = (int)fps;
        int tenth = (int)((fps - (float)whole) * 10.0f);
        if (whole < 0) whole = 0;
        if (tenth < 0) tenth = 0;
        snprintf(line, sizeof line, "FPS %d.%d", whole, tenth);
        int tw = 8 * 2 * (int)strlen(line) + 10;
        int th = 8 * 2 + 8;
        draw_box(cbuf, rw, rh, 8, 8, tw, th, 0x000000);
        draw_text(cbuf, rw, rh, 13, 12, line, 0x00FF66, 2);

        snprintf(line, sizeof line, "%s  %dx%d  %ld tris",
                 scene == 0 ? "GEARS" : "CUBE", rw, rh, glTrianglesDrawn());
        int bw2 = 8 * (int)strlen(line) + 10;
        draw_box(cbuf, rw, rh, 8, rh - 26, bw2, 16, 0x000000);
        draw_text(cbuf, rw, rh, 13, rh - 24, line, 0xAAAAAA, 1);
        glResetCounters();

        if (!prev_valid) {
            blit_rect(fb, pitch32, ox, oy, cbuf, rw, rh, 0, 0, rw, rh);
        } else {
            for (int y = 0; y < rh; y++) {
                int lo = pmin[y], hi = pmax[y];
                if (nmax[y] > nmin[y]) {
                    if (hi <= lo) { lo = nmin[y]; hi = nmax[y]; }
                    else {
                        if (nmin[y] < lo) lo = nmin[y];
                        if (nmax[y] > hi) hi = nmax[y];
                    }
                }
                if (hi > lo) blit_rect(fb, pitch32, ox, oy, cbuf, rw, rh, lo, y, hi, y + 1);
            }
        }
        blit_rect(fb, pitch32, ox, oy, cbuf, rw, rh, 8, 8, 8 + tw, 8 + th);
        blit_rect(fb, pitch32, ox, oy, cbuf, rw, rh, 8, rh - 26, 8 + bw2, rh - 10);

        for (int y = 0; y < rh; y++) {
            pmin[y] = nmin[y];
            pmax[y] = nmax[y];
        }
        prev_valid = 1;

        if (!paused) angle += 1.6f;

        frames++;
        frames_since++;
        total_frames++;

        uint64_t now = now_ns();
        if (now - t_last >= 500000000ULL) {
            fps = (float)frames_since * 1000000000.0f / (float)(now - t_last);
            frames_since = 0;
            t_last = now;
        }

        if (frame_limit && total_frames >= frame_limit) break;
    }

    uint64_t t_end = now_ns();

    cervus_fb_release();
    if (have_tio) tcsetattr(0, TCSAFLUSH, &orig);
    nb = 0;
    ioctl(0, TIOCSNONBLOCK, &nb);

    glDestroyContext();
    free(pmin); free(pmax);
    free(gear1.v); free(gear2.v); free(gear3.v); free(cube.v);

    if (t_end > t_start) {
        double secs = (double)(t_end - t_start) / 1e9;
        printf("%ld frames in %.2f s -- %.1f fps at %dx%d\n",
               total_frames, secs, (double)total_frames / secs, rw, rh);
    }
    return 0;
}
