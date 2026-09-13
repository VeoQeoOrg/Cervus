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
void     ip_deliver(netdev_t *dev, uint32_t src, uint32_t dst, uint8_t proto,
                    const uint8_t *payload, size_t plen);
void     ip_frag_input(netdev_t *dev, const uint8_t *pkt, size_t total);
void     ip_frag_tick(void);
int      ip_send(netdev_t *dev, uint32_t dst, uint8_t proto, const void *payload, size_t len, uint8_t ttl);
uint16_t ip_checksum(const void *data, size_t len);
uint32_t ip_source_for(netdev_t *dev, uint32_t dst);
int      loopback_drain_one(void);
void     loopback_drain_all(void);
int      loopback_output(uint16_t ethertype, const uint8_t *pkt, size_t len);
void     loopback_init(void);

#endif
