#ifndef _NETINET_IF_ETHER_H
#define _NETINET_IF_ETHER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <net/ethernet.h>
#include <net/if_arp.h>

struct ether_arp {
    struct arphdr ea_hdr;
    uint8_t arp_sha[ETH_ALEN];
    uint8_t arp_spa[4];
    uint8_t arp_tha[ETH_ALEN];
    uint8_t arp_tpa[4];
};

#define arp_hrd ea_hdr.ar_hrd
#define arp_pro ea_hdr.ar_pro
#define arp_hln ea_hdr.ar_hln
#define arp_pln ea_hdr.ar_pln
#define arp_op  ea_hdr.ar_op

#ifdef __cplusplus
}
#endif
#endif
