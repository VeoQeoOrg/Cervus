#ifndef _KERNEL_NET_UDP_H
#define _KERNEL_NET_UDP_H

#include <stdint.h>
#include <stddef.h>
#include "netdev.h"

typedef void (*udp_handler_f)(netdev_t *dev, uint32_t src_ip, uint16_t src_port,
                              uint16_t dst_port, const uint8_t *data, size_t len);

int  udp_bind(uint16_t port, udp_handler_f handler);
void udp_unbind(uint16_t port);
int  udp_send(netdev_t *dev, uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
              const void *data, size_t len);
void udp_rx(netdev_t *dev, uint32_t src_ip, uint32_t dst_ip, const uint8_t *pkt, size_t len);

#endif
