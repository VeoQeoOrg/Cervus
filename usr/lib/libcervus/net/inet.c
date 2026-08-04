#include <arpa/inet.h>
#include <stdio.h>
#include <stddef.h>

in_addr_t inet_addr(const char *cp) {
    unsigned parts[4];
    int pi = 0, digits = 0;
    unsigned cur = 0;
    const char *p = cp;
    for (;;) {
        if (*p >= '0' && *p <= '9') {
            cur = cur * 10 + (unsigned)(*p - '0');
            if (cur > 255) return 0xffffffffu;
            digits = 1;
        } else if (*p == '.' || *p == '\0') {
            if (!digits || pi >= 4) return 0xffffffffu;
            parts[pi++] = cur;
            cur = 0; digits = 0;
            if (*p == '\0') break;
        } else {
            return 0xffffffffu;
        }
        p++;
    }
    if (pi != 4) return 0xffffffffu;
    return (in_addr_t)(parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24));
}

char *inet_ntoa(struct in_addr in) {
    static char buf[16];
    uint32_t a = in.s_addr;
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             a & 0xff, (a >> 8) & 0xff, (a >> 16) & 0xff, (a >> 24) & 0xff);
    return buf;
}

const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

static int pton6(const char *s, uint8_t *out) {
    uint8_t tmp[16];
    for (int k = 0; k < 16; k++) tmp[k] = 0;
    int i = 0, dcolon = -1;
    const char *p = s;
    if (p[0] == ':' && p[1] != ':') return 0;
    while (*p) {
        if (*p == ':') {
            if (p[1] == ':') { if (dcolon >= 0) return 0; dcolon = i; p += 2; continue; }
            p++; continue;
        }
        const char *q = p; int isv4 = 0;
        while (*q && *q != ':') { if (*q == '.') isv4 = 1; q++; }
        if (isv4) {
            int b[4], n = 0, val = 0, seen = 0;
            for (const char *r = p; ; r++) {
                if (*r >= '0' && *r <= '9') { val = val * 10 + (*r - '0'); seen = 1; }
                else if (*r == '.' || *r == 0 || *r == ':') { if (!seen || val > 255 || n >= 4) return 0; b[n++] = val; val = 0; seen = 0; if (*r == 0 || *r == ':') break; }
                else return 0;
            }
            if (n != 4 || i > 12) return 0;
            tmp[i++] = b[0]; tmp[i++] = b[1]; tmp[i++] = b[2]; tmp[i++] = b[3];
            p = q; break;
        }
        int val = 0, ndig = 0;
        while (*p && *p != ':') {
            int d; char c = *p;
            if (c >= '0' && c <= '9') d = c - '0';
            else if ((c | 32) >= 'a' && (c | 32) <= 'f') d = (c | 32) - 'a' + 10;
            else return 0;
            val = val * 16 + d; if (++ndig > 4) return 0; p++;
        }
        if (i > 14) return 0;
        tmp[i++] = (uint8_t)(val >> 8); tmp[i++] = (uint8_t)(val & 0xff);
    }
    if (dcolon >= 0) {
        int nafter = i - dcolon;
        for (int k = 0; k < dcolon; k++) out[k] = tmp[k];
        for (int k = dcolon; k < 16 - nafter; k++) out[k] = 0;
        for (int k = 0; k < nafter; k++) out[16 - nafter + k] = tmp[dcolon + k];
    } else {
        if (i != 16) return 0;
        for (int k = 0; k < 16; k++) out[k] = tmp[k];
    }
    return 1;
}

static unsigned put_hex16(char *d, unsigned n, socklen_t cap, uint16_t v) {
    static const char *hd = "0123456789abcdef";
    char rev[4]; int rn = 0;
    if (v == 0) rev[rn++] = '0'; else while (v) { rev[rn++] = hd[v & 0xf]; v >>= 4; }
    while (rn > 0 && n + 1 < cap) d[n++] = rev[--rn];
    return n;
}
static unsigned put_dec(char *d, unsigned n, socklen_t cap, unsigned v) {
    char rev[4]; int rn = 0;
    if (v == 0) rev[rn++] = '0'; else while (v) { rev[rn++] = '0' + v % 10; v /= 10; }
    while (rn > 0 && n + 1 < cap) d[n++] = rev[--rn];
    return n;
}

static const char *ntop6(const uint8_t *a, char *dst, socklen_t cap) {
    int mapped = 1;
    for (int k = 0; k < 10; k++) if (a[k]) mapped = 0;
    if (mapped && a[10] == 0xff && a[11] == 0xff) {
        unsigned n = 0;
        const char *pre = "::ffff:";
        for (int k = 0; pre[k] && n + 1 < cap; k++) dst[n++] = pre[k];
        for (int k = 0; k < 4; k++) { n = put_dec(dst, n, cap, a[12 + k]); if (k < 3 && n + 1 < cap) dst[n++] = '.'; }
        if (n < cap) dst[n] = 0;
        return dst;
    }

    uint16_t w[8];
    for (int i = 0; i < 8; i++) w[i] = (uint16_t)((a[i * 2] << 8) | a[i * 2 + 1]);
    int bi = -1, bl = 0, ci = -1, cl = 0;
    for (int i = 0; i < 8; i++) {
        if (w[i] == 0) { if (ci < 0) { ci = i; cl = 1; } else cl++; if (cl > bl) { bl = cl; bi = ci; } }
        else { ci = -1; cl = 0; }
    }
    if (bl < 2) bi = -1;

    unsigned n = 0;
    if (bi < 0) {
        for (int i = 0; i < 8; i++) { if (i > 0 && n + 1 < cap) dst[n++] = ':'; n = put_hex16(dst, n, cap, w[i]); }
    } else {
        for (int i = 0; i < bi; i++) { if (i > 0 && n + 1 < cap) dst[n++] = ':'; n = put_hex16(dst, n, cap, w[i]); }
        if (n + 2 < cap) { dst[n++] = ':'; dst[n++] = ':'; }
        int first = 1;
        for (int i = bi + bl; i < 8; i++) { if (!first && n + 1 < cap) dst[n++] = ':'; n = put_hex16(dst, n, cap, w[i]); first = 0; }
    }
    if (n < cap) dst[n] = 0; else if (cap > 0) dst[cap - 1] = 0;
    return dst;
}

int inet_pton(int af, const char *src, void *dst) {
    if (af == 10) return pton6(src, (uint8_t *)dst);
    in_addr_t a = inet_addr(src);
    if (a == 0xffffffffu) return 0;
    *(in_addr_t *)dst = a;
    return 1;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
    if (af == 10) return ntop6((const uint8_t *)src, dst, size);
    struct in_addr in;
    in.s_addr = *(const in_addr_t *)src;
    char *s = inet_ntoa(in);
    unsigned i = 0;
    while (s[i] && i + 1 < size) { dst[i] = s[i]; i++; }
    dst[i] = '\0';
    return dst;
}
