#include "../../include/net/dns.h"
#include "../../include/net/udp.h"
#include "../../include/net/net.h"
#include "../../include/io/serial.h"
#include <string.h>

#define DNS_CLIENT_PORT 49152
#define DNS_SERVER_PORT 53

static uint16_t g_dns_id;
static char     g_query_name[128];

static int encode_name(uint8_t *out, size_t cap, const char *name) {
    int oi = 0;
    const char *p = name;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        int label = (int)(dot - p);
        if (label <= 0 || label > 63) return -1;
        if ((size_t)oi + 1 + (size_t)label + 1 > cap) return -1;
        out[oi++] = (uint8_t)label;
        memcpy(out + oi, p, (size_t)label);
        oi += label;
        p = (*dot == '.') ? dot + 1 : dot;
    }
    if ((size_t)oi + 1 > cap) return -1;
    out[oi++] = 0;
    return oi;
}

static void skip_name(const uint8_t *p, size_t len, size_t *i) {
    while (*i < len) {
        uint8_t b = p[*i];
        if (b == 0) { (*i)++; return; }
        if ((b & 0xc0) == 0xc0) { *i += 2; return; }
        *i += (size_t)b + 1;
    }
}

static void dns_rx(netdev_t *dev, uint32_t src, uint16_t sport, uint16_t dport,
                   const uint8_t *p, size_t len) {
    (void)dev; (void)src; (void)sport; (void)dport;
    if (len < 12) return;
    uint16_t ancount = rd16be(p + 6);
    if (ancount == 0) { serial_printf("[dns] %s: no answer\n", g_query_name); return; }

    size_t i = 12;
    skip_name(p, len, &i);
    i += 4;

    for (int a = 0; a < ancount && i + 10 <= len; a++) {
        skip_name(p, len, &i);
        if (i + 10 > len) break;
        uint16_t type  = rd16be(p + i);
        uint16_t rdlen = rd16be(p + i + 8);
        i += 10;
        if (type == 1 && rdlen == 4 && i + 4 <= len) {
            uint32_t ip = rd32be(p + i);
            serial_printf("[dns] %s = %u.%u.%u.%u\n", g_query_name,
                          (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
            return;
        }
        i += rdlen;
    }
    serial_printf("[dns] %s: no A record\n", g_query_name);
}

void dns_query(netdev_t *dev, const char *name) {
    if (!dev->dns) return;

    uint8_t pkt[300];
    wr16be(pkt + 0, ++g_dns_id);
    wr16be(pkt + 2, 0x0100);
    wr16be(pkt + 4, 1);
    wr16be(pkt + 6, 0);
    wr16be(pkt + 8, 0);
    wr16be(pkt + 10, 0);

    int nl = encode_name(pkt + 12, sizeof(pkt) - 16, name);
    if (nl < 0) return;
    int off = 12 + nl;
    wr16be(pkt + off, 1); off += 2;
    wr16be(pkt + off, 1); off += 2;

    strncpy(g_query_name, name, sizeof(g_query_name) - 1);
    g_query_name[sizeof(g_query_name) - 1] = '\0';

    udp_bind(DNS_CLIENT_PORT, dns_rx);
    udp_send(dev, dev->dns, DNS_CLIENT_PORT, DNS_SERVER_PORT, pkt, (size_t)off);
}
