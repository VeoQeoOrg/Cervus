#include "../../include/net/udp.h"
#include "../../include/net/ip.h"
#include "../../include/net/ipv6.h"
#include "../../include/net/net.h"
#include "../../include/net/socket.h"
#include <string.h>

#define UDP_MAX_BINDS 16

static struct { uint16_t port; udp_handler_f h; } g_binds[UDP_MAX_BINDS];

int udp_bind(uint16_t port, udp_handler_f handler) {
    for (int i = 0; i < UDP_MAX_BINDS; i++)
        if (g_binds[i].port == port) { g_binds[i].h = handler; return 0; }
    for (int i = 0; i < UDP_MAX_BINDS; i++)
        if (!g_binds[i].h) { g_binds[i].port = port; g_binds[i].h = handler; return 0; }
    return -1;
}

void udp_unbind(uint16_t port) {
    for (int i = 0; i < UDP_MAX_BINDS; i++)
        if (g_binds[i].h && g_binds[i].port == port) { g_binds[i].h = NULL; g_binds[i].port = 0; }
}

static uint16_t udp_checksum(uint32_t src, uint32_t dst, const uint8_t *udp, size_t len) {
    uint32_t sum = 0;
    sum += (src >> 16) & 0xffff; sum += src & 0xffff;
    sum += (dst >> 16) & 0xffff; sum += dst & 0xffff;
    sum += 17;
    sum += (uint32_t)len & 0xffff;
    for (size_t i = 0; i + 1 < len; i += 2) sum += (uint32_t)((udp[i] << 8) | udp[i + 1]);
    if (len & 1) sum += (uint32_t)(udp[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    uint16_t c = (uint16_t)~sum;
    return c ? c : 0xFFFF;
}

int udp_send(netdev_t *dev, uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
             const void *data, size_t len) {
    if (len + 8 > 1480) return -1;
    uint8_t pkt[1500];
    wr16be(pkt + 0, src_port);
    wr16be(pkt + 2, dst_port);
    wr16be(pkt + 4, (uint16_t)(8 + len));
    wr16be(pkt + 6, 0);
    memcpy(pkt + 8, data, len);
    wr16be(pkt + 6, udp_checksum(dev->ip, dst_ip, pkt, 8 + len));
    return ip_send(dev, dst_ip, IPPROTO_UDP, pkt, 8 + len, IP_DEFAULT_TTL);
}

static uint16_t udp6_checksum(const uint8_t *src, const uint8_t *dst, const uint8_t *udp, size_t len) {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i += 2) sum += (uint32_t)((src[i] << 8) | src[i + 1]);
    for (int i = 0; i < 16; i += 2) sum += (uint32_t)((dst[i] << 8) | dst[i + 1]);
    sum += (uint32_t)len & 0xffff; sum += (uint32_t)(len >> 16);
    sum += 17;
    for (size_t i = 0; i + 1 < len; i += 2) sum += (uint32_t)((udp[i] << 8) | udp[i + 1]);
    if (len & 1) sum += (uint32_t)(udp[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    uint16_t c = (uint16_t)~sum;
    return c ? c : 0xFFFF;
}

int udp6_send(netdev_t *dev, const uint8_t src[16], const uint8_t dst[16],
              uint16_t sport, uint16_t dport, const void *data, size_t len) {
    if (len + 8 > 1480) return -1;
    uint8_t pkt[1500];
    wr16be(pkt + 0, sport);
    wr16be(pkt + 2, dport);
    wr16be(pkt + 4, (uint16_t)(8 + len));
    wr16be(pkt + 6, 0);
    memcpy(pkt + 8, data, len);
    wr16be(pkt + 6, udp6_checksum(src, dst, pkt, 8 + len));
    return ipv6_output(dev, src, dst, IPPROTO_UDP, pkt, 8 + len);
}

void udp6_rx(netdev_t *dev, const uint8_t *src, const uint8_t *dst, const uint8_t *p, size_t len) {
    (void)dev; (void)dst;
    if (len < 8) return;
    uint16_t src_port = rd16be(p + 0);
    uint16_t dst_port = rd16be(p + 2);
    uint16_t ulen     = rd16be(p + 4);
    if (ulen < 8 || ulen > len) ulen = (uint16_t)len;
    sock_udp6_input(src, src_port, dst_port, p + 8, (size_t)(ulen - 8));
}

void udp_rx(netdev_t *dev, uint32_t src_ip, uint32_t dst_ip, const uint8_t *p, size_t len) {
    (void)dst_ip;
    if (len < 8) return;
    uint16_t src_port = rd16be(p + 0);
    uint16_t dst_port = rd16be(p + 2);
    uint16_t ulen     = rd16be(p + 4);
    if (ulen < 8 || ulen > len) ulen = (uint16_t)len;

    for (int i = 0; i < UDP_MAX_BINDS; i++)
        if (g_binds[i].h && g_binds[i].port == dst_port) {
            g_binds[i].h(dev, src_ip, src_port, dst_port, p + 8, (size_t)(ulen - 8));
            return;
        }

    sock_udp_input(src_ip, src_port, dst_port, p + 8, (size_t)(ulen - 8));
}
