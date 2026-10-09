#ifndef _NETINET_IN_H
#define _NETINET_IN_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/socket.h>

#define INET_ADDRSTRLEN  16
#define INET6_ADDRSTRLEN 46

#define INADDR_ANY       0u
#define INADDR_LOOPBACK  0x7f000001u
#define INADDR_BROADCAST 0xffffffffu
#define INADDR_NONE      0xffffffffu

#define IPPROTO_IP     0
#define IPPROTO_ICMP   1
#define IPPROTO_IPV6   41
#define IPPROTO_RAW    255
#define IPPROTO_TCP    6
#define IPPROTO_UDP    17
#define IPPROTO_ICMPV6 58

typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;

struct in_addr { in_addr_t s_addr; };

struct sockaddr_in {
    uint16_t       sin_family;
    uint16_t       sin_port;
    struct in_addr sin_addr;
    uint8_t        sin_zero[8];
};

struct in6_addr {
    union {
        uint8_t  __s6_addr[16];
        uint16_t __s6_addr16[8];
        uint32_t __s6_addr32[4];
    } __in6_union;
};
#define s6_addr   __in6_union.__s6_addr
#define s6_addr16 __in6_union.__s6_addr16
#define s6_addr32 __in6_union.__s6_addr32

struct sockaddr_in6 {
    uint16_t        sin6_family;
    uint16_t        sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};

extern const struct in6_addr in6addr_any;
extern const struct in6_addr in6addr_loopback;

#define IN6ADDR_ANY_INIT      { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 } }
#define IN6ADDR_LOOPBACK_INIT { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } }

#define __IN6_W32(a, i) (((uint32_t)((const struct in6_addr *)(a))->s6_addr[4 * (i)] << 24) | ((uint32_t)((const struct in6_addr *)(a))->s6_addr[4 * (i) + 1] << 16) | \
                         ((uint32_t)((const struct in6_addr *)(a))->s6_addr[4 * (i) + 2] << 8) | (uint32_t)((const struct in6_addr *)(a))->s6_addr[4 * (i) + 3])
#define IN6_IS_ADDR_UNSPECIFIED(a) \
    (__IN6_W32(a, 0) == 0 && __IN6_W32(a, 1) == 0 && __IN6_W32(a, 2) == 0 && __IN6_W32(a, 3) == 0)
#define IN6_IS_ADDR_LOOPBACK(a) \
    (__IN6_W32(a, 0) == 0 && __IN6_W32(a, 1) == 0 && __IN6_W32(a, 2) == 0 && __IN6_W32(a, 3) == 1)
#define IN6_IS_ADDR_MULTICAST(a)  (((const struct in6_addr *)(a))->s6_addr[0] == 0xff)
#define IN6_IS_ADDR_LINKLOCAL(a)  (((const struct in6_addr *)(a))->s6_addr[0] == 0xfe && (((const struct in6_addr *)(a))->s6_addr[1] & 0xc0) == 0x80)
#define IN6_IS_ADDR_SITELOCAL(a)  (((const struct in6_addr *)(a))->s6_addr[0] == 0xfe && (((const struct in6_addr *)(a))->s6_addr[1] & 0xc0) == 0xc0)
#define IN6_IS_ADDR_V4MAPPED(a) \
    (__IN6_W32(a, 0) == 0 && __IN6_W32(a, 1) == 0 && __IN6_W32(a, 2) == 0x0000ffffu)
#define IN6_IS_ADDR_V4COMPAT(a) \
    (__IN6_W32(a, 0) == 0 && __IN6_W32(a, 1) == 0 && __IN6_W32(a, 2) == 0 && __IN6_W32(a, 3) > 1)
#define IN6_ARE_ADDR_EQUAL(a, b) \
    (__IN6_W32(a, 0) == __IN6_W32(b, 0) && __IN6_W32(a, 1) == __IN6_W32(b, 1) && \
     __IN6_W32(a, 2) == __IN6_W32(b, 2) && __IN6_W32(a, 3) == __IN6_W32(b, 3))
#define IN6_IS_ADDR_MC_NODELOCAL(a) (IN6_IS_ADDR_MULTICAST(a) && (((const struct in6_addr *)(a))->s6_addr[1] & 0xf) == 0x1)
#define IN6_IS_ADDR_MC_LINKLOCAL(a) (IN6_IS_ADDR_MULTICAST(a) && (((const struct in6_addr *)(a))->s6_addr[1] & 0xf) == 0x2)
#define IN6_IS_ADDR_MC_SITELOCAL(a) (IN6_IS_ADDR_MULTICAST(a) && (((const struct in6_addr *)(a))->s6_addr[1] & 0xf) == 0x5)
#define IN6_IS_ADDR_MC_ORGLOCAL(a)  (IN6_IS_ADDR_MULTICAST(a) && (((const struct in6_addr *)(a))->s6_addr[1] & 0xf) == 0x8)
#define IN6_IS_ADDR_MC_GLOBAL(a)    (IN6_IS_ADDR_MULTICAST(a) && (((const struct in6_addr *)(a))->s6_addr[1] & 0xf) == 0xe)

uint32_t htonl(uint32_t x);
uint16_t htons(uint16_t x);
uint32_t ntohl(uint32_t x);
uint16_t ntohs(uint16_t x);
#define htonl(x) __builtin_bswap32(x)
#define htons(x) __builtin_bswap16(x)
#define ntohl(x) __builtin_bswap32(x)
#define ntohs(x) __builtin_bswap16(x)

#define IP_TOS                    1
#define IP_TTL                    2
#define IP_HDRINCL                3
#define IP_RECVERR                11
#define IP_MULTICAST_IF           32
#define IP_MULTICAST_TTL          33
#define IP_MULTICAST_LOOP         34
#define IP_ADD_MEMBERSHIP         35
#define IP_DROP_MEMBERSHIP        36
#define IP_ADD_SOURCE_MEMBERSHIP  39
#define IP_DROP_SOURCE_MEMBERSHIP 40
#define IPV6_UNICAST_HOPS         16
#define IPV6_MULTICAST_IF         17
#define IPV6_MULTICAST_HOPS       18
#define IPV6_MULTICAST_LOOP       19
#define IPV6_ADD_MEMBERSHIP       20
#define IPV6_DROP_MEMBERSHIP      21
#define IPV6_JOIN_GROUP           IPV6_ADD_MEMBERSHIP
#define IPV6_LEAVE_GROUP          IPV6_DROP_MEMBERSHIP
#define IPV6_RECVERR              25
#define IPV6_V6ONLY               26
#define MCAST_JOIN_SOURCE_GROUP   46
#define MCAST_LEAVE_SOURCE_GROUP  47

struct ip_mreq {
    struct in_addr imr_multiaddr;
    struct in_addr imr_interface;
};

struct ip_mreq_source {
    struct in_addr imr_multiaddr;
    struct in_addr imr_interface;
    struct in_addr imr_sourceaddr;
};

struct ipv6_mreq {
    struct in6_addr ipv6mr_multiaddr;
    unsigned int    ipv6mr_interface;
};

#define INADDR_DUMMY ((in_addr_t) 0xc0000008)
#define INADDR_UNSPEC_GROUP ((in_addr_t) 0xe0000000)
#define INADDR_ALLHOSTS_GROUP ((in_addr_t) 0xe0000001)
#define INADDR_ALLRTRS_GROUP ((in_addr_t) 0xe0000002)
#define INADDR_ALLSNOOPERS_GROUP ((in_addr_t) 0xe000006a)
#define INADDR_MAX_LOCAL_GROUP ((in_addr_t) 0xe00000ff)
#define IPPORT_RESERVED 1024
#define IPPROTO_HOPOPTS 0
#define IPPROTO_IGMP 2
#define IPPROTO_IPIP 4
#define IPPROTO_EGP 8
#define IPPROTO_PUP 12
#define IPPROTO_IDP 22
#define IPPROTO_TP 29
#define IPPROTO_DCCP 33
#define IPPROTO_ROUTING 43
#define IPPROTO_FRAGMENT 44
#define IPPROTO_RSVP 46
#define IPPROTO_GRE 47
#define IPPROTO_ESP 50
#define IPPROTO_AH 51
#define IPPROTO_NONE 59
#define IPPROTO_DSTOPTS 60
#define IPPROTO_MTP 92
#define IPPROTO_BEETPH 94
#define IPPROTO_ENCAP 98
#define IPPROTO_PIM 103
#define IPPROTO_COMP 108
#define IPPROTO_SCTP 132
#define IPPROTO_MH 135
#define IPPROTO_UDPLITE 136
#define IPPROTO_MPLS 137
#define IPPROTO_ETHERNET 143
#define IPPROTO_MPTCP 262
#define IPPROTO_MAX 263
#define IN_CLASSA(a) ((((in_addr_t)(a)) & 0x80000000) == 0)
#define IN_CLASSA_NET 0xff000000
#define IN_CLASSA_NSHIFT 24
#define IN_CLASSA_HOST (0xffffffff & ~IN_CLASSA_NET)
#define IN_CLASSA_MAX 128
#define IN_CLASSB(a) ((((in_addr_t)(a)) & 0xc0000000) == 0x80000000)
#define IN_CLASSB_NET 0xffff0000
#define IN_CLASSB_NSHIFT 16
#define IN_CLASSB_HOST (0xffffffff & ~IN_CLASSB_NET)
#define IN_CLASSB_MAX 65536
#define IN_CLASSC(a) ((((in_addr_t)(a)) & 0xe0000000) == 0xc0000000)
#define IN_CLASSC_NET 0xffffff00
#define IN_CLASSC_NSHIFT 8
#define IN_CLASSC_HOST (0xffffffff & ~IN_CLASSC_NET)
#define IN_CLASSD(a) ((((in_addr_t)(a)) & 0xf0000000) == 0xe0000000)
#define IN_MULTICAST(a) IN_CLASSD(a)
#define IN_EXPERIMENTAL(a) ((((in_addr_t)(a)) & 0xe0000000) == 0xe0000000)
#define IN_BADCLASS(a) ((((in_addr_t)(a)) & 0xf0000000) == 0xf0000000)
#define IN_LOOPBACKNET 127
#define IP_OPTIONS 4
#define IP_ROUTER_ALERT 5
#define IP_RECVOPTS 6
#define IP_RETOPTS 7
#define IP_PKTINFO 8
#define IP_PKTOPTIONS 9
#define IP_PMTUDISC 10
#define IP_MTU_DISCOVER 10
#define IP_RECVTTL 12
#define IP_RECVTOS 13
#define IP_MTU 14
#define IP_FREEBIND 15
#define IP_IPSEC_POLICY 16
#define IP_XFRM_POLICY 17
#define IP_PASSSEC 18
#define IP_TRANSPARENT 19
#define IP_ORIGDSTADDR 20
#define IP_RECVORIGDSTADDR IP_ORIGDSTADDR
#define IP_MINTTL 21
#define IP_NODEFRAG 22
#define IP_CHECKSUM 23
#define IP_BIND_ADDRESS_NO_PORT 24
#define IP_RECVFRAGSIZE 25
#define IP_RECVERR_RFC4884 26
#define IP_UNBLOCK_SOURCE 37
#define IP_BLOCK_SOURCE 38
#define IP_MSFILTER 41
#define IP_MULTICAST_ALL 49
#define IP_UNICAST_IF 50
#define IP_RECVRETOPTS IP_RETOPTS
#define IP_PMTUDISC_DONT 0
#define IP_PMTUDISC_WANT 1
#define IP_PMTUDISC_DO 2
#define IP_PMTUDISC_PROBE 3
#define IP_PMTUDISC_INTERFACE 4
#define IP_PMTUDISC_OMIT 5
#define IP_DEFAULT_MULTICAST_TTL 1
#define IP_DEFAULT_MULTICAST_LOOP 1
#define IP_MAX_MEMBERSHIPS 20
#define MCAST_JOIN_GROUP 42
#define MCAST_BLOCK_SOURCE 43
#define MCAST_UNBLOCK_SOURCE 44
#define MCAST_LEAVE_GROUP 45
#define MCAST_MSFILTER 48
#define MCAST_EXCLUDE 0
#define MCAST_INCLUDE 1
#define IPV6_ADDRFORM 1
#define IPV6_2292PKTINFO 2
#define IPV6_2292HOPOPTS 3
#define IPV6_2292DSTOPTS 4
#define IPV6_2292RTHDR 5
#define IPV6_2292PKTOPTIONS 6
#define IPV6_CHECKSUM 7
#define IPV6_2292HOPLIMIT 8
#define IPV6_NEXTHOP 9
#define IPV6_AUTHHDR 10
#define IPV6_ROUTER_ALERT 22
#define IPV6_MTU_DISCOVER 23
#define IPV6_MTU 24
#define IPV6_JOIN_ANYCAST 27
#define IPV6_LEAVE_ANYCAST 28
#define IPV6_MULTICAST_ALL 29
#define IPV6_ROUTER_ALERT_ISOLATE 30
#define IPV6_IPSEC_POLICY 34
#define IPV6_XFRM_POLICY 35
#define IPV6_HDRINCL 36
#define IPV6_RECVPKTINFO 49
#define IPV6_PKTINFO 50
#define IPV6_RECVHOPLIMIT 51
#define IPV6_HOPLIMIT 52
#define IPV6_RECVHOPOPTS 53
#define IPV6_HOPOPTS 54
#define IPV6_RTHDRDSTOPTS 55
#define IPV6_RECVRTHDR 56
#define IPV6_RTHDR 57
#define IPV6_RECVDSTOPTS 58
#define IPV6_DSTOPTS 59
#define IPV6_RECVPATHMTU 60
#define IPV6_PATHMTU 61
#define IPV6_DONTFRAG 62
#define IPV6_RECVTCLASS 66
#define IPV6_TCLASS 67
#define IPV6_AUTOFLOWLABEL 70
#define IPV6_ADDR_PREFERENCES 72
#define IPV6_MINHOPCOUNT 73
#define IPV6_ORIGDSTADDR 74
#define IPV6_RECVORIGDSTADDR IPV6_ORIGDSTADDR
#define IPV6_TRANSPARENT 75
#define IPV6_UNICAST_IF 76
#define IPV6_RECVFRAGSIZE 77
#define IPV6_FREEBIND 78
#define IPV6_RXHOPOPTS IPV6_HOPOPTS
#define IPV6_RXDSTOPTS IPV6_DSTOPTS
#define IPV6_PMTUDISC_DONT 0
#define IPV6_PMTUDISC_WANT 1
#define IPV6_PMTUDISC_DO 2
#define IPV6_PMTUDISC_PROBE 3
#define IPV6_PMTUDISC_INTERFACE 4
#define IPV6_PMTUDISC_OMIT 5
#define IPV6_PREFER_SRC_TMP 0x0001
#define IPV6_PREFER_SRC_PUBLIC 0x0002
#define IPV6_PREFER_SRC_PUBTMP_DEFAULT 0x0100
#define IPV6_PREFER_SRC_COA 0x0004
#define IPV6_PREFER_SRC_HOME 0x0400
#define IPV6_PREFER_SRC_CGA 0x0008
#define IPV6_PREFER_SRC_NONCGA 0x0800
#define IPV6_RTHDR_LOOSE 0
#define IPV6_RTHDR_STRICT 1
#define IPV6_RTHDR_TYPE_0 0
#define IP_MSFILTER_SIZE(numsrc) \
    (sizeof(struct ip_msfilter) - sizeof(struct in_addr) + (numsrc) * sizeof(struct in_addr))
#define GROUP_FILTER_SIZE(numsrc) \
    (sizeof(struct group_filter) - sizeof(struct sockaddr_storage) + (numsrc) * sizeof(struct sockaddr_storage))

struct ip_opts {
    struct in_addr ip_dst;
    char ip_opts[40];
};

struct ip_mreqn {
    struct in_addr imr_multiaddr;
    struct in_addr imr_address;
    int imr_ifindex;
};

struct ip_msfilter {
    struct in_addr imsf_multiaddr;
    struct in_addr imsf_interface;
    uint32_t imsf_fmode;
    uint32_t imsf_numsrc;
    struct in_addr imsf_slist[1];
};

struct group_req {
    uint32_t gr_interface;
    struct sockaddr_storage gr_group;
};

struct group_filter {
    uint32_t gf_interface;
    struct sockaddr_storage gf_group;
    uint32_t gf_fmode;
    uint32_t gf_numsrc;
    struct sockaddr_storage gf_slist[1];
};

struct in_pktinfo {
    int ipi_ifindex;
    struct in_addr ipi_spec_dst;
    struct in_addr ipi_addr;
};

struct in6_pktinfo {
    struct in6_addr ipi6_addr;
    unsigned ipi6_ifindex;
};

struct ip6_mtuinfo {
    struct sockaddr_in6 ip6m_addr;
    uint32_t ip6m_mtu;
};

struct group_source_req {
    uint32_t gsr_interface;
    uint32_t __pad;
    struct {
        uint16_t ss_family;
        char     __data[126];
    } gsr_group, gsr_source;
};

#ifdef __cplusplus
}
#endif
#endif
