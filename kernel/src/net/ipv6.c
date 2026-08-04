#include "../../include/net/netdev.h"
#include "../../include/net/net.h"
#include "../../include/io/serial.h"
#include <string.h>

#define IPPROTO_ICMPV6 58
#define ND_NS 135
#define ND_NA 136
#define ICMP6_ECHO_REQ 128
#define ICMP6_ECHO_REP 129

static uint16_t icmp6_cksum(const uint8_t *src, const uint8_t *dst, const uint8_t *msg, uint32_t len) {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i += 2) sum += (uint32_t)((src[i] << 8) | src[i + 1]);
    for (int i = 0; i < 16; i += 2) sum += (uint32_t)((dst[i] << 8) | dst[i + 1]);
    sum += (len >> 16) & 0xffff; sum += len & 0xffff;
    sum += IPPROTO_ICMPV6;
    for (uint32_t i = 0; i + 1 < len; i += 2) sum += (uint32_t)((msg[i] << 8) | msg[i + 1]);
    if (len & 1) sum += (uint32_t)(msg[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static void ipv6_send(netdev_t *dev, const uint8_t dmac[6], const uint8_t src[16],
                      const uint8_t dst[16], uint8_t nexthdr, const uint8_t *payload, uint32_t plen) {
    uint8_t pkt[1500];
    if (plen > sizeof(pkt) - 40) return;
    memset(pkt, 0, 40);
    pkt[0] = 0x60;
    pkt[4] = (uint8_t)(plen >> 8); pkt[5] = (uint8_t)plen;
    pkt[6] = nexthdr;
    pkt[7] = 255;
    memcpy(pkt + 8, src, 16);
    memcpy(pkt + 24, dst, 16);
    memcpy(pkt + 40, payload, plen);
    eth_send(dev, dmac, ETH_P_IPV6, pkt, 40 + plen);
}

static void send_na(netdev_t *dev, const uint8_t smac[6], const uint8_t *dst_ip, const uint8_t *target) {
    uint8_t msg[32];
    memset(msg, 0, sizeof msg);
    msg[0] = ND_NA;
    msg[4] = 0x60;
    memcpy(msg + 8, target, 16);
    msg[24] = 2; msg[25] = 1;
    memcpy(msg + 26, dev->mac, 6);
    uint16_t ck = icmp6_cksum(dev->ip6_ll, dst_ip, msg, sizeof msg);
    msg[2] = ck >> 8; msg[3] = ck & 0xff;
    ipv6_send(dev, smac, dev->ip6_ll, dst_ip, IPPROTO_ICMPV6, msg, sizeof msg);
}

void ipv6_rx(netdev_t *dev, const uint8_t *smac, const uint8_t *pkt, size_t len) {
    if (len < 40 || (pkt[0] >> 4) != 6) return;
    uint16_t plen = (uint16_t)((pkt[4] << 8) | pkt[5]);
    uint8_t nexthdr = pkt[6];
    const uint8_t *src = pkt + 8;
    const uint8_t *dst = pkt + 24;
    if (40u + plen > len) plen = (uint16_t)(len - 40);
    const uint8_t *msg = pkt + 40;
    if (nexthdr != IPPROTO_ICMPV6 || plen < 8) return;

    uint8_t type = msg[0];
    if (type == ND_NS) {
        if (plen < 24) return;
        const uint8_t *target = msg + 8;
        if (memcmp(target, dev->ip6_ll, 16) == 0) send_na(dev, smac, src, target);
    } else if (type == ICMP6_ECHO_REQ) {
        if (memcmp(dst, dev->ip6_ll, 16) != 0) return;
        uint8_t reply[1452];
        if (plen > sizeof reply) return;
        memcpy(reply, msg, plen);
        reply[0] = ICMP6_ECHO_REP;
        reply[2] = 0; reply[3] = 0;
        uint16_t ck = icmp6_cksum(dev->ip6_ll, src, reply, plen);
        reply[2] = ck >> 8; reply[3] = ck & 0xff;
        ipv6_send(dev, smac, dev->ip6_ll, src, IPPROTO_ICMPV6, reply, plen);
    }
}
