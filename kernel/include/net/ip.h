#ifndef _KERNEL_NET_IP_H
#define _KERNEL_NET_IP_H

#include <stdint.h>
#include <stddef.h>
#include "netdev.h"

#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17

#define IP_DEFAULT_TTL 64

void     ip_rx(netdev_t *dev, const uint8_t *pkt, size_t len);
int      ip_send(netdev_t *dev, uint32_t dst, uint8_t proto, const void *payload, size_t len, uint8_t ttl);
uint16_t ip_checksum(const void *data, size_t len);
int      loopback_drain_one(void);

#endif
