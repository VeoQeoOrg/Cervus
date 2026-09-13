#ifndef _KERNEL_NET_ICMP_H
#define _KERNEL_NET_ICMP_H

#include <stdint.h>
#include <stddef.h>
#include "netdev.h"

#define ICMP_DEST_UNREACH  3
#define ICMP_TIME_EXCEEDED 11

#define ICMP_DU_PORT       3
#define ICMP_TE_TTL        0
#define ICMP_TE_REASSEMBLY 1

int  icmp_send_error(netdev_t *dev, uint32_t dst, uint8_t type, uint8_t code,
                     const uint8_t *orig, size_t orig_len);
void icmp_rx(netdev_t *dev, uint32_t src, const uint8_t *pkt, size_t len);
int  icmp_send_echo(netdev_t *dev, uint32_t dst, uint16_t id, uint16_t seq);

#endif
