#ifndef _SYS_NETCFG_H
#define _SYS_NETCFG_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    char     name[16];
    uint8_t  mac[6];
    uint8_t  _pad[2];
    uint32_t ip, netmask, gateway, dns;
    uint64_t rx_packets, tx_packets, rx_bytes, tx_bytes;
    int32_t  link_up;
    int32_t  mtu;
    uint8_t  ip6_ll[16];
    uint64_t rx_dropped, tx_dropped;
    uint64_t rx_arp, rx_ip, rx_other, rx_not_for_us, rx_tcp_syn, rx_bad_csum, rx_frag_dropped;
    uint64_t rx_frag_reasm, rx_frag_timeout, tx_fragmented;
    int32_t  is_loopback;
    int32_t  _pad2;
} net_ifcfg_t;

int netif_get(int index, net_ifcfg_t *out);
int netif_set(int index, uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns);

#ifdef __cplusplus
}
#endif
#endif
