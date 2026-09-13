#include "../../include/net/ip.h"
#include "../../include/net/icmp.h"
#include "../../include/net/udp.h"
#include "../../include/net/tcp.h"
#include "../../include/net/net.h"
#include "../../include/net/arp.h"
#include "../../include/net/netdev.h"
#include <string.h>
#include <stdlib.h>
#include "../../include/io/serial.h"

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

static int ip_is_local(netdev_t *dev, uint32_t ip) {
    if ((ip >> 24) == 127) return 1;
    if (dev && dev->ip && ip == dev->ip) return 1;
    for (netdev_t *d = netdev_first(); d; d = d->next)
        if (d->ip && ip == d->ip) return 1;
    return 0;
}

uint32_t ip_source_for(netdev_t *dev, uint32_t dst) {
    if (ip_is_local(dev, dst)) return dst;
    return dev ? dev->ip : 0;
}

static void ip_fill_header(uint8_t *pkt, uint32_t src, uint32_t dst, uint8_t proto,
                           uint8_t ttl, uint16_t id, uint16_t frag, size_t payload) {
    pkt[0] = 0x45;
    pkt[1] = 0;
    wr16be(pkt + 2, (uint16_t)(20 + payload));
    wr16be(pkt + 4, id);
    wr16be(pkt + 6, frag);
    pkt[8] = ttl;
    pkt[9] = proto;
    wr16be(pkt + 10, 0);
    wr32be(pkt + 12, src);
    wr32be(pkt + 16, dst);
    wr16be(pkt + 10, ip_checksum(pkt, 20));
}

static int ip_emit(netdev_t *dev, const uint8_t mac[6], int local,
                   const uint8_t *pkt, size_t len) {
    if (local) return netdev_transmit(dev, pkt, len);
    return eth_send(dev, mac, ETH_P_IP, pkt, len);
}

int ip_send(netdev_t *dev, uint32_t dst, uint8_t proto, const void *payload, size_t len, uint8_t ttl) {
    if (len > 65515) return -1;

    int local = ip_is_local(dev, dst);
    uint32_t src = local ? dst : (dev ? dev->ip : 0);

    uint8_t mac[6];
    if (local) {
        netdev_t *lo = netdev_loopback();
        if (!lo) return -1;
        dev = lo;
    } else {
        if (!dev) return -1;
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
    }

    size_t mtu = dev->mtu ? dev->mtu : 1500;
    if (mtu < 68) mtu = 68;
    if (mtu > 65535) mtu = 65535;

    uint16_t id = g_ip_id++;

    uint8_t  sbuf[1520];
    uint8_t *pkt = sbuf;
    size_t   want = 20 + (len + 20 <= mtu ? len : ((mtu - 20) & ~(size_t)7));
    if (want > sizeof(sbuf)) {
        pkt = malloc(want);
        if (!pkt) return -1;
    }

    int rc = 0;
    const uint8_t *data = payload;

    if (20 + len <= mtu) {
        ip_fill_header(pkt, src, dst, proto, ttl, id, 0, len);
        memcpy(pkt + 20, data, len);
        rc = ip_emit(dev, mac, local, pkt, 20 + len);
    } else {
        size_t chunk = (mtu - 20) & ~(size_t)7;
        for (size_t off = 0; off < len; off += chunk) {
            size_t n = len - off < chunk ? len - off : chunk;
            uint16_t frag = (uint16_t)(off / 8);
            if (off + n < len) frag |= 0x2000;
            ip_fill_header(pkt, src, dst, proto, ttl, id, frag, n);
            memcpy(pkt + 20, data + off, n);
            rc = ip_emit(dev, mac, local, pkt, 20 + n);
            if (rc != 0) break;
        }
        if (rc == 0) dev->tx_fragmented++;
    }

    if (pkt != sbuf) free(pkt);
    return rc;
}

void ip_deliver(netdev_t *dev, uint32_t src, uint32_t dst, uint8_t proto,
                const uint8_t *payload, size_t plen) {
    switch (proto) {
        case IPPROTO_ICMP: icmp_rx(dev, src, payload, plen); break;
        case IPPROTO_UDP:  udp_rx(dev, src, dst, payload, plen); break;
        case IPPROTO_TCP:  tcp_rx(dev, src, dst, payload, plen); break;
        default: break;
    }
}

void ip_rx(netdev_t *dev, const uint8_t *p, size_t len) {
    if (len < 20 || (p[0] >> 4) != 4) return;
    uint32_t ihl = (uint32_t)(p[0] & 0x0f) * 4;
    if (ihl < 20 || len < ihl) return;

    if (ip_checksum(p, ihl) != 0) {
        dev->rx_bad_csum++;
        return;
    }

    uint16_t total = rd16be(p + 2);
    if (total < ihl || total > len) total = (uint16_t)len;

    uint8_t  proto = p[9];
    uint32_t src   = rd32be(p + 12);
    uint32_t dst   = rd32be(p + 16);

    int is_lb = (dst >> 24) == 127;
    if (!is_lb && dev->ip && dst != dev->ip && dst != 0xFFFFFFFFu) {
        dev->rx_not_for_us++;
        LOG_D("[ip] %s dropping proto=%u for %u.%u.%u.%u, we are %u.%u.%u.%u\n",
              dev->name, proto,
              (dst >> 24) & 0xff, (dst >> 16) & 0xff, (dst >> 8) & 0xff, dst & 0xff,
              (dev->ip >> 24) & 0xff, (dev->ip >> 16) & 0xff,
              (dev->ip >> 8) & 0xff, dev->ip & 0xff);
        return;
    }

    uint16_t frag = rd16be(p + 6);
    if ((frag & 0x2000) || (frag & 0x1FFF)) {
        ip_frag_input(dev, p, total);
        return;
    }

    LOG_D("[ip] %s rx proto=%u %u.%u.%u.%u -> %u.%u.%u.%u len=%u\n",
          dev->name, proto,
          (src >> 24) & 0xff, (src >> 16) & 0xff, (src >> 8) & 0xff, src & 0xff,
          (dst >> 24) & 0xff, (dst >> 16) & 0xff, (dst >> 8) & 0xff, dst & 0xff,
          (unsigned)total);

    ip_deliver(dev, src, dst, proto, p + ihl, total - ihl);
}
