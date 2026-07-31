#include "../../include/net/ip.h"
#include "../../include/net/icmp.h"
#include "../../include/net/udp.h"
#include "../../include/net/tcp.h"
#include "../../include/net/net.h"
#include "../../include/net/arp.h"
#include <string.h>

static uint16_t g_ip_id = 1;

uint16_t ip_checksum(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t sum = 0;
    while (len > 1) { sum += (uint32_t)((p[0] << 8) | p[1]); p += 2; len -= 2; }
    if (len) sum += (uint32_t)(p[0] << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static int same_subnet(netdev_t *dev, uint32_t ip) {
    return (ip & dev->netmask) == (dev->ip & dev->netmask);
}

int ip_send(netdev_t *dev, uint32_t dst, uint8_t proto, const void *payload, size_t len) {
    if (len > 1480) return -1;

    uint8_t mac[6];
    if (dst == 0xFFFFFFFFu) {
        memcpy(mac, eth_broadcast, 6);
    } else {
        if (!dev->ip) return -1;
        uint32_t nexthop = same_subnet(dev, dst) ? dst : dev->gateway;
        if (arp_lookup(nexthop, mac) != 0) {
            arp_request(dev, nexthop);
            return -1;
        }
    }

    uint8_t pkt[1500];
    pkt[0] = 0x45;
    pkt[1] = 0;
    wr16be(pkt + 2, (uint16_t)(20 + len));
    wr16be(pkt + 4, g_ip_id++);
    wr16be(pkt + 6, 0x4000);
    pkt[8] = 64;
    pkt[9] = proto;
    wr16be(pkt + 10, 0);
    wr32be(pkt + 12, dev->ip);
    wr32be(pkt + 16, dst);
    wr16be(pkt + 10, ip_checksum(pkt, 20));
    memcpy(pkt + 20, payload, len);

    return eth_send(dev, mac, ETH_P_IP, pkt, 20 + len);
}

void ip_rx(netdev_t *dev, const uint8_t *p, size_t len) {
    if (len < 20 || (p[0] >> 4) != 4) return;
    uint32_t ihl = (uint32_t)(p[0] & 0x0f) * 4;
    if (ihl < 20 || len < ihl) return;

    uint16_t total = rd16be(p + 2);
    if (total < ihl || total > len) total = (uint16_t)len;

    uint8_t  proto = p[9];
    uint32_t src   = rd32be(p + 12);
    uint32_t dst   = rd32be(p + 16);

    if (dev->ip && dst != dev->ip && dst != 0xFFFFFFFFu) return;

    const uint8_t *payload = p + ihl;
    size_t plen = total - ihl;

    switch (proto) {
        case IPPROTO_ICMP: icmp_rx(dev, src, payload, plen); break;
        case IPPROTO_UDP:  udp_rx(dev, src, dst, payload, plen); break;
        case IPPROTO_TCP:  tcp_rx(dev, src, dst, payload, plen); break;
        default: break;
    }
}
