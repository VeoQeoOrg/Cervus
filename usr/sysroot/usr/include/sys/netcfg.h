#ifndef _SYS_NETCFG_H
#define _SYS_NETCFG_H

#include <stdint.h>

typedef struct {
    char     name[16];
    uint8_t  mac[6];
    uint8_t  _pad[2];
    uint32_t ip, netmask, gateway, dns;
    uint64_t rx_packets, tx_packets, rx_bytes, tx_bytes;
    int32_t  link_up;
    int32_t  mtu;
} net_ifcfg_t;

int netif_get(int index, net_ifcfg_t *out);

#endif
