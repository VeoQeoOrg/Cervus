#ifndef _NET_IF_ARP_H
#define _NET_IF_ARP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/socket.h>

#define MAX_ADDR_LEN 7

#define ARPOP_REQUEST   1
#define ARPOP_REPLY     2
#define ARPOP_RREQUEST  3
#define ARPOP_RREPLY    4
#define ARPOP_InREQUEST 8
#define ARPOP_InREPLY   9
#define ARPOP_NAK       10

struct arphdr {
    uint16_t ar_hrd;
    uint16_t ar_pro;
    uint8_t  ar_hln;
    uint8_t  ar_pln;
    uint16_t ar_op;
};

#define ARPHRD_NETROM     0
#define ARPHRD_ETHER      1
#define ARPHRD_EETHER     2
#define ARPHRD_AX25       3
#define ARPHRD_PRONET     4
#define ARPHRD_CHAOS      5
#define ARPHRD_IEEE802    6
#define ARPHRD_ARCNET     7
#define ARPHRD_DLCI       15
#define ARPHRD_ATM        19
#define ARPHRD_IEEE1394   24
#define ARPHRD_INFINIBAND 32
#define ARPHRD_SLIP       256
#define ARPHRD_CSLIP      257
#define ARPHRD_PPP        512
#define ARPHRD_TUNNEL     768
#define ARPHRD_TUNNEL6    769
#define ARPHRD_FRAD       770
#define ARPHRD_LOOPBACK   772
#define ARPHRD_FDDI       774
#define ARPHRD_IEEE802_TR 800
#define ARPHRD_SIT        776
#define ARPHRD_IPGRE      778
#define ARPHRD_IEEE80211  801
#define ARPHRD_IEEE80211_PRISM 802
#define ARPHRD_IEEE80211_RADIOTAP 803
#define ARPHRD_NONE       0xFFFE
#define ARPHRD_VOID       0xFFFF

struct arpreq {
    struct sockaddr arp_pa;
    struct sockaddr arp_ha;
    int             arp_flags;
    struct sockaddr arp_netmask;
    char            arp_dev[16];
};

#define ATF_COM         0x02
#define ATF_PERM        0x04
#define ATF_PUBL        0x08
#define ATF_USETRAILERS 0x10
#define ATF_NETMASK     0x20
#define ATF_DONTPUB     0x40
#define ATF_MAGIC       0x80

#ifdef __cplusplus
}
#endif
#endif
