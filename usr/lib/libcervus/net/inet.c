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

int inet_pton(int af, const char *src, void *dst) {
    (void)af;
    in_addr_t a = inet_addr(src);
    if (a == 0xffffffffu) return 0;
    *(in_addr_t *)dst = a;
    return 1;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
    (void)af;
    struct in_addr in;
    in.s_addr = *(const in_addr_t *)src;
    char *s = inet_ntoa(in);
    unsigned i = 0;
    while (s[i] && i + 1 < size) { dst[i] = s[i]; i++; }
    dst[i] = '\0';
    return dst;
}
