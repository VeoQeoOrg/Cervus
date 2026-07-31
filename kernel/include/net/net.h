#ifndef _KERNEL_NET_NET_H
#define _KERNEL_NET_NET_H

#include <stdint.h>
#include <stddef.h>
#include "netdev.h"

#define ETH_P_IP    0x0800
#define ETH_P_ARP   0x0806

#define IP4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                         ((uint32_t)(c) << 8) | (uint32_t)(d))

static inline uint16_t net16(uint16_t x) { return (uint16_t)((x << 8) | (x >> 8)); }
static inline uint32_t net32(uint32_t x) {
    return ((x >> 24) & 0xff) | ((x >> 8) & 0xff00) |
           ((x << 8) & 0xff0000) | (x << 24);
}

static inline uint16_t rd16be(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t rd32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static inline void wr16be(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void wr32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

int eth_send(netdev_t *dev, const uint8_t dst[6], uint16_t ethertype,
             const void *payload, size_t len);

extern const uint8_t eth_broadcast[6];

#endif
