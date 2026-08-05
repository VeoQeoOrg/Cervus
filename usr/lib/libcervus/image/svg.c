#include <image.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct { float x, y; } pt_t;

typedef struct {
    uint32_t *px;
    int w, h;
    float sx, sy, ox, oy;
} canvas_t;

static int ci(char c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }

static uint32_t named_color(const char *s, int len) {
    struct { const char *n; uint32_t c; } tab[] = {
        {"black",0xFF000000},{"white",0xFFFFFFFF},{"red",0xFFFF0000},{"green",0xFF008000},
        {"blue",0xFF0000FF},{"yellow",0xFFFFFF00},{"gray",0xFF808080},{"grey",0xFF808080},
        {"orange",0xFFFFA500},{"purple",0xFF800080},{"cyan",0xFF00FFFF},{"magenta",0xFFFF00FF},
        {"lime",0xFF00FF00},{"navy",0xFF000080},{"silver",0xFFC0C0C0},{"maroon",0xFF800000},
    };
    for (unsigned i = 0; i < sizeof(tab)/sizeof(tab[0]); i++) {
        const char *n = tab[i].n; int j = 0;
        while (j < len && n[j] && ci(s[j]) == n[j]) j++;
        if (j == len && !n[j]) return tab[i].c;
    }
    return 0;
}

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = ci(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static int parse_color(const char *s, int len, uint32_t *out) {
    while (len && (*s == ' ' || *s == '\t')) { s++; len--; }
    if (len >= 4 && !strncmp(s, "none", 4)) return 0;
    if (len && *s == '#') {
        s++; len--;
        int hl = 0; while (hl < len && ((s[hl]>='0'&&s[hl]<='9')||(ci(s[hl])>='a'&&ci(s[hl])<='f'))) hl++;
        if (hl >= 6) { *out = 0xFF000000u | (hexv(s[0])<<20)|(hexv(s[1])<<16)|(hexv(s[2])<<12)|(hexv(s[3])<<8)|(hexv(s[4])<<4)|hexv(s[5]); return 1; }
        if (hl >= 3) { int r=hexv(s[0]),g=hexv(s[1]),b=hexv(s[2]); *out = 0xFF000000u|((r*17)<<16)|((g*17)<<8)|(b*17); return 1; }
        return 0;
    }
    uint32_t c = named_color(s, len);
    if (c) { *out = c; return 1; }
    return 0;
}

static const char *find_attr(const char *tag, const char *end, const char *name) {
    size_t nl = strlen(name);
    for (const char *p = tag; p + nl + 1 < end; p++) {
        if ((p == tag || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n') &&
            !strncmp(p, name, nl) && p[nl] == '=') {
            const char *v = p + nl + 1;
            if (*v == '"' || *v == '\'') return v + 1;
        }
    }
    return NULL;
}

static float attr_f(const char *tag, const char *end, const char *name, float def) {
    const char *v = find_attr(tag, end, name);
    if (!v) return def;
    return strtof(v, NULL);
}

static int attr_color(const char *tag, const char *end, const char *name, uint32_t *out) {
    const char *v = find_attr(tag, end, name);
    if (!v) return -1;
    const char *e = v; while (*e && *e != '"' && *e != '\'') e++;
    return parse_color(v, (int)(e - v), out) ? 1 : 0;
}

static uint32_t style_color(const char *tag, const char *end, const char *key, uint32_t fallback, int *set) {
    *set = 0;
    uint32_t c;
    int r = attr_color(tag, end, key, &c);
    if (r == 1) { *set = 1; return c; }
    if (r == 0) { *set = 0; return fallback; }
    const char *v = find_attr(tag, end, "style");
    if (v) {
        const char *e = v; while (*e && *e != '"' && *e != '\'') e++;
        size_t kl = strlen(key);
        for (const char *p = v; p + kl + 1 < e; p++) {
            if (!strncmp(p, key, kl) && p[kl] == ':') {
                const char *cv = p + kl + 1;
                while (cv < e && (*cv == ' ')) cv++;
                const char *ce = cv; while (ce < e && *ce != ';') ce++;
                if (parse_color(cv, (int)(ce - cv), &c)) { *set = 1; return c; }
                return fallback;
            }
        }
    }
    return fallback;
}

static void put(canvas_t *cv, int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= cv->w || y >= cv->h) return;
    cv->px[(size_t)y * cv->w + x] = c;
}

static void fill_poly(canvas_t *cv, const pt_t *pts, int np, uint32_t color) {
    if (np < 3) return;
    float miny = pts[0].y, maxy = pts[0].y;
    for (int i = 1; i < np; i++) { if (pts[i].y < miny) miny = pts[i].y; if (pts[i].y > maxy) maxy = pts[i].y; }
    int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (y0 < 0) y0 = 0;
    if (y1 > cv->h) y1 = cv->h;
    float xs[256];
    for (int y = y0; y < y1; y++) {
        float yc = y + 0.5f;
        int nx = 0;
        for (int i = 0; i < np; i++) {
            pt_t a = pts[i], b = pts[(i + 1) % np];
            if ((a.y <= yc && b.y > yc) || (b.y <= yc && a.y > yc)) {
                float t = (yc - a.y) / (b.y - a.y);
                if (nx < 256) xs[nx++] = a.x + t * (b.x - a.x);
            }
        }
        for (int i = 0; i < nx - 1; i++)
            for (int j = i + 1; j < nx; j++)
                if (xs[j] < xs[i]) { float tmp = xs[i]; xs[i] = xs[j]; xs[j] = tmp; }
        for (int i = 0; i + 1 < nx; i += 2) {
            int xa = (int)ceilf(xs[i] - 0.5f), xb = (int)floorf(xs[i + 1] - 0.5f);
            for (int x = xa; x <= xb; x++) put(cv, x, y, color);
        }
    }
}

static void stroke_line(canvas_t *cv, float x0, float y0, float x1, float y1, uint32_t color) {
    int dx = (int)fabsf(x1 - x0), dy = (int)fabsf(y1 - y0);
    int steps = dx > dy ? dx : dy;
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        put(cv, (int)lrintf(x0 + (x1 - x0) * t), (int)lrintf(y0 + (y1 - y0) * t), color);
    }
}

static void stroke_poly(canvas_t *cv, const pt_t *pts, int np, int closed, uint32_t color) {
    for (int i = 0; i + 1 < np; i++) stroke_line(cv, pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y, color);
    if (closed && np > 1) stroke_line(cv, pts[np-1].x, pts[np-1].y, pts[0].x, pts[0].y, color);
}

static float tx(canvas_t *cv, float x) { return (x - cv->ox) * cv->sx; }
static float ty(canvas_t *cv, float y) { return (y - cv->oy) * cv->sy; }

static void render_shape(canvas_t *cv, const char *tag, const char *end) {
    int fset, sset;
    uint32_t fill = style_color(tag, end, "fill", 0xFF000000u, &fset);
    uint32_t stroke = style_color(tag, end, "stroke", 0, &sset);
    int has_fill = 1, has_stroke = sset;
    {
        const char *v = find_attr(tag, end, "fill");
        if (v && !strncmp(v, "none", 4)) has_fill = 0;
    }

    if (!strncmp(tag, "rect", 4)) {
        float x = attr_f(tag,end,"x",0), y = attr_f(tag,end,"y",0);
        float w = attr_f(tag,end,"width",0), h = attr_f(tag,end,"height",0);
        pt_t q[4] = {{tx(cv,x),ty(cv,y)},{tx(cv,x+w),ty(cv,y)},{tx(cv,x+w),ty(cv,y+h)},{tx(cv,x),ty(cv,y+h)}};
        if (has_fill) fill_poly(cv, q, 4, fill);
        if (has_stroke) stroke_poly(cv, q, 4, 1, stroke);
    } else if (!strncmp(tag, "circle", 6) || !strncmp(tag, "ellipse", 7)) {
        float cx = attr_f(tag,end,"cx",0), cy = attr_f(tag,end,"cy",0);
        float rx = attr_f(tag,end,"r",0), ry = rx;
        if (!strncmp(tag,"ellipse",7)) { rx = attr_f(tag,end,"rx",0); ry = attr_f(tag,end,"ry",0); }
        pt_t q[72];
        for (int i = 0; i < 72; i++) {
            float a = i * 3.14159265f * 2 / 72;
            q[i].x = tx(cv, cx + rx * cosf(a));
            q[i].y = ty(cv, cy + ry * sinf(a));
        }
        if (has_fill) fill_poly(cv, q, 72, fill);
        if (has_stroke) stroke_poly(cv, q, 72, 1, stroke);
    } else if (!strncmp(tag, "line", 4)) {
        float x1=attr_f(tag,end,"x1",0),y1=attr_f(tag,end,"y1",0),x2=attr_f(tag,end,"x2",0),y2=attr_f(tag,end,"y2",0);
        stroke_line(cv, tx(cv,x1),ty(cv,y1),tx(cv,x2),ty(cv,y2), sset ? stroke : 0xFF000000u);
    } else if (!strncmp(tag, "polygon", 7) || !strncmp(tag, "polyline", 8)) {
        int closed = !strncmp(tag, "polygon", 7);
        const char *v = find_attr(tag, end, "points");
        if (!v) return;
        static pt_t q[4096]; int np = 0;
        const char *pp = v;
        while (*pp && *pp != '"' && *pp != '\'' && np < 4096) {
            while (*pp==' '||*pp==','||*pp=='\n'||*pp=='\t') pp++;
            if (*pp=='"'||*pp=='\''||!*pp) break;
            float xx = strtof(pp, (char**)&pp);
            while (*pp==' '||*pp==',') pp++;
            float yy = strtof(pp, (char**)&pp);
            q[np].x = tx(cv,xx); q[np].y = ty(cv,yy); np++;
        }
        if (has_fill && closed) fill_poly(cv, q, np, fill);
        if (has_stroke || !closed) stroke_poly(cv, q, np, closed, sset ? stroke : (closed?0:0xFF000000u));
    } else if (!strncmp(tag, "path", 4)) {
        const char *v = find_attr(tag, end, "d");
        if (!v) return;
        static pt_t q[8192]; int np = 0;
        float cx = 0, cy = 0, sx0 = 0, sy0 = 0;
        const char *pp = v;
        char cmd = 0;
        while (*pp && *pp != '"' && *pp != '\'') {
            while (*pp==' '||*pp==','||*pp=='\n'||*pp=='\t') pp++;
            if (!*pp || *pp=='"'||*pp=='\'') break;
            if ((*pp>='A'&&*pp<='Z')||(*pp>='a'&&*pp<='z')) { cmd = *pp++; }
            int rel = (cmd >= 'a');
            char c = ci(cmd);
            if (c == 'm' || c == 'l') {
                float x = strtof(pp,(char**)&pp); while(*pp==' '||*pp==',')pp++;
                float y = strtof(pp,(char**)&pp);
                if (rel) { x += cx; y += cy; }
                cx = x; cy = y;
                if (c == 'm') { sx0 = cx; sy0 = cy; }
                if (np < 8192) { q[np].x = tx(cv,cx); q[np].y = ty(cv,cy); np++; }
                if (c == 'm') cmd = rel ? 'l' : 'L';
            } else if (c == 'h') {
                float x = strtof(pp,(char**)&pp); if (rel) x += cx; cx = x;
                if (np < 8192) { q[np].x = tx(cv,cx); q[np].y = ty(cv,cy); np++; }
            } else if (c == 'v') {
                float y = strtof(pp,(char**)&pp); if (rel) y += cy; cy = y;
                if (np < 8192) { q[np].x = tx(cv,cx); q[np].y = ty(cv,cy); np++; }
            } else if (c == 'c' || c == 'q') {
                float x1=strtof(pp,(char**)&pp);while(*pp==' '||*pp==',')pp++;
                float y1=strtof(pp,(char**)&pp);while(*pp==' '||*pp==',')pp++;
                float x2=strtof(pp,(char**)&pp);while(*pp==' '||*pp==',')pp++;
                float y2=strtof(pp,(char**)&pp);while(*pp==' '||*pp==',')pp++;
                float x3, y3;
                if (c == 'c') { x3=strtof(pp,(char**)&pp);while(*pp==' '||*pp==',')pp++; y3=strtof(pp,(char**)&pp); }
                else { x3 = x2; y3 = y2; }
                if (rel) { x1+=cx;y1+=cy;x2+=cx;y2+=cy;x3+=cx;y3+=cy; }
                for (int s = 1; s <= 12 && np < 8192; s++) {
                    float t = s / 12.0f, u = 1 - t;
                    float bx, by;
                    if (c == 'c') {
                        bx = u*u*u*cx + 3*u*u*t*x1 + 3*u*t*t*x2 + t*t*t*x3;
                        by = u*u*u*cy + 3*u*u*t*y1 + 3*u*t*t*y2 + t*t*t*y3;
                    } else {
                        bx = u*u*cx + 2*u*t*x1 + t*t*x2;
                        by = u*u*cy + 2*u*t*y1 + t*t*y2;
                    }
                    q[np].x = tx(cv,bx); q[np].y = ty(cv,by); np++;
                }
                cx = x3; cy = y3;
            } else if (c == 'z') {
                cx = sx0; cy = sy0;
                if (has_fill) fill_poly(cv, q, np, fill);
                if (has_stroke) stroke_poly(cv, q, np, 1, stroke);
                np = 0;
            } else {
                pp++;
            }
        }
        if (np > 1) {
            if (has_fill) fill_poly(cv, q, np, fill);
            if (has_stroke) stroke_poly(cv, q, np, 0, stroke);
        }
    }
}

int image_decode_svg(const uint8_t *d, size_t n, image_t *out) {
    const char *s = (const char *)d;
    const char *end = s + n;

    float vbx = 0, vby = 0, vbw = 0, vbh = 0;
    float sw = 0, sh = 0;
    for (const char *p = s; p + 4 < end; p++) {
        if (!strncmp(p, "<svg", 4)) {
            const char *te = p; while (te < end && *te != '>') te++;
            sw = attr_f(p, te, "width", 0);
            sh = attr_f(p, te, "height", 0);
            const char *vb = find_attr(p, te, "viewBox");
            if (vb) {
                vbx = strtof(vb, (char**)&vb); while(*vb==' '||*vb==',')vb++;
                vby = strtof(vb, (char**)&vb); while(*vb==' '||*vb==',')vb++;
                vbw = strtof(vb, (char**)&vb); while(*vb==' '||*vb==',')vb++;
                vbh = strtof(vb, (char**)&vb);
            }
            break;
        }
    }

    if (sw <= 0 && vbw > 0) sw = vbw;
    if (sh <= 0 && vbh > 0) sh = vbh;
    if (sw <= 0) sw = 256;
    if (sh <= 0) sh = 256;
    int W = (int)(sw + 0.5f), H = (int)(sh + 0.5f);
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W > 4096) W = 4096;
    if (H > 4096) H = 4096;

    canvas_t cv;
    cv.w = W; cv.h = H;
    cv.px = calloc((size_t)W * H, 4);
    if (!cv.px) return -1;
    if (vbw > 0 && vbh > 0) { cv.sx = W / vbw; cv.sy = H / vbh; cv.ox = vbx; cv.oy = vby; }
    else { cv.sx = 1; cv.sy = 1; cv.ox = 0; cv.oy = 0; }

    for (const char *p = s; p < end; p++) {
        if (*p != '<') continue;
        const char *tag = p + 1;
        if (tag >= end || *tag == '/' || *tag == '?' || *tag == '!') continue;
        const char *te = tag; while (te < end && *te != '>') te++;
        render_shape(&cv, tag, te);
        p = te;
    }

    out->w = W; out->h = H; out->px = cv.px;
    return 0;
}
