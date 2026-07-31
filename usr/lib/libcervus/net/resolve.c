#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/netcfg.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define RD16(p) (uint16_t)(((p)[0] << 8) | (p)[1])

static int dns_encode(uint8_t *out, const char *name) {
    int oi = 0;
    const char *p = name;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        int label = (int)(dot - p);
        if (label <= 0 || label > 63) return -1;
        out[oi++] = (uint8_t)label;
        memcpy(out + oi, p, (size_t)label);
        oi += label;
        p = (*dot == '.') ? dot + 1 : dot;
    }
    out[oi++] = 0;
    return oi;
}

static void dns_skip_name(const uint8_t *p, int len, int *i) {
    while (*i < len) {
        uint8_t b = p[*i];
        if (b == 0) { (*i)++; return; }
        if ((b & 0xc0) == 0xc0) { *i += 2; return; }
        *i += b + 1;
    }
}

static in_addr_t dns_parse(const uint8_t *p, int len) {
    if (len < 12) return 0xffffffffu;
    int ancount = RD16(p + 6);
    if (ancount == 0) return 0xffffffffu;
    int i = 12;
    dns_skip_name(p, len, &i);
    i += 4;
    for (int a = 0; a < ancount && i + 10 <= len; a++) {
        dns_skip_name(p, len, &i);
        if (i + 10 > len) break;
        uint16_t type  = RD16(p + i);
        uint16_t rdlen = RD16(p + i + 8);
        i += 10;
        if (type == 1 && rdlen == 4 && i + 4 <= len)
            return (in_addr_t)(p[i] | (p[i+1] << 8) | (p[i+2] << 16) | ((uint32_t)p[i+3] << 24));
        i += rdlen;
    }
    return 0xffffffffu;
}

in_addr_t inet_resolve(const char *name) {
    in_addr_t direct = inet_addr(name);
    if (direct != 0xffffffffu) return direct;

    net_ifcfg_t cfg;
    if (netif_get(0, &cfg) != 0 || !cfg.dns) return 0xffffffffu;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0xffffffffu;

    uint8_t q[300];
    q[0] = 0x12; q[1] = 0x34;
    q[2] = 0x01; q[3] = 0x00;
    q[4] = 0; q[5] = 1;
    q[6] = 0; q[7] = 0; q[8] = 0; q[9] = 0; q[10] = 0; q[11] = 0;
    int nl = dns_encode(q + 12, name);
    if (nl < 0) { close(fd); return 0xffffffffu; }
    int off = 12 + nl;
    q[off++] = 0; q[off++] = 1;
    q[off++] = 0; q[off++] = 1;

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(53);
    dst.sin_addr.s_addr = htonl(cfg.dns);
    sendto(fd, q, (size_t)off, 0, (struct sockaddr *)&dst, sizeof(dst));

    long fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    in_addr_t result = 0xffffffffu;
    uint8_t r[600];
    for (int t = 0; t < 200; t++) {
        ssize_t n = recvfrom(fd, r, sizeof(r), 0, NULL, NULL);
        if (n > 0) { result = dns_parse(r, (int)n); if (result != 0xffffffffu) break; }
        usleep(10000);
    }
    close(fd);
    return result;
}
