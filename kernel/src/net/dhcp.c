#include "../../include/net/dhcp.h"
#include "../../include/net/udp.h"
#include "../../include/net/net.h"
#include "../../include/drivers/timer.h"
#include "../../include/io/serial.h"
#include <string.h>

#define DHCP_SPORT 68
#define DHCP_DPORT 67
#define DHCP_MAGIC 0x63825363u

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

#define OPT_SUBNET    1
#define OPT_ROUTER    3
#define OPT_DNS       6
#define OPT_REQ_IP    50
#define OPT_MSG_TYPE  53
#define OPT_SERVER_ID 54
#define OPT_PARAM_REQ 55
#define OPT_END       255

enum { ST_INIT, ST_DISCOVER, ST_REQUEST, ST_BOUND };

static netdev_t *g_dev;
static uint32_t  g_xid;
static uint32_t  g_server_id;
static uint32_t  g_offered_ip;
static int       g_state;

static int build_packet(uint8_t *buf, uint8_t msg_type) {
    memset(buf, 0, 300);
    buf[0] = 1;
    buf[1] = 1;
    buf[2] = 6;
    wr32be(buf + 4, g_xid);
    wr16be(buf + 10, 0x8000);
    memcpy(buf + 28, g_dev->mac, 6);
    wr32be(buf + 236, DHCP_MAGIC);

    uint8_t *o = buf + 240;
    *o++ = OPT_MSG_TYPE; *o++ = 1; *o++ = msg_type;

    if (msg_type == DHCP_REQUEST) {
        *o++ = OPT_REQ_IP;    *o++ = 4; wr32be(o, g_offered_ip); o += 4;
        *o++ = OPT_SERVER_ID; *o++ = 4; wr32be(o, g_server_id);  o += 4;
    }

    *o++ = OPT_PARAM_REQ; *o++ = 3; *o++ = OPT_SUBNET; *o++ = OPT_ROUTER; *o++ = OPT_DNS;
    *o++ = OPT_END;

    return (int)(o - buf);
}

static const uint8_t *find_option(const uint8_t *opt, size_t len, uint8_t code, uint8_t *out_len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = opt[i++];
        if (c == OPT_END) break;
        if (c == 0) continue;
        if (i >= len) break;
        uint8_t l = opt[i++];
        if (i + l > len) break;
        if (c == code) { if (out_len) *out_len = l; return opt + i; }
        i += l;
    }
    return NULL;
}

static void dhcp_rx(netdev_t *dev, uint32_t src_ip, uint16_t sport, uint16_t dport,
                    const uint8_t *p, size_t len) {
    (void)src_ip; (void)sport; (void)dport;
    if (len < 240) return;
    if (rd32be(p + 4) != g_xid) return;
    if (rd32be(p + 236) != DHCP_MAGIC) return;

    const uint8_t *opt = p + 240;
    size_t optlen = len - 240;
    uint8_t l;
    const uint8_t *mt = find_option(opt, optlen, OPT_MSG_TYPE, &l);
    if (!mt) return;

    uint32_t yiaddr = rd32be(p + 16);

    if (mt[0] == DHCP_OFFER && g_state == ST_DISCOVER) {
        g_offered_ip = yiaddr;
        const uint8_t *sid = find_option(opt, optlen, OPT_SERVER_ID, &l);
        g_server_id = sid ? rd32be(sid) : src_ip;
        uint8_t pkt[300];
        int n = build_packet(pkt, DHCP_REQUEST);
        g_state = ST_REQUEST;
        udp_send(dev, 0xFFFFFFFFu, DHCP_SPORT, DHCP_DPORT, pkt, (size_t)n);
    } else if (mt[0] == DHCP_ACK && g_state == ST_REQUEST) {
        dev->ip = yiaddr;
        const uint8_t *sn = find_option(opt, optlen, OPT_SUBNET, &l);
        const uint8_t *rt = find_option(opt, optlen, OPT_ROUTER, &l);
        const uint8_t *dn = find_option(opt, optlen, OPT_DNS, &l);
        dev->netmask = sn ? rd32be(sn) : IP4(255, 255, 255, 0);
        dev->gateway = rt ? rd32be(rt) : 0;
        dev->dns     = dn ? rd32be(dn) : 0;
        g_state = ST_BOUND;
        serial_printf("[dhcp] %s bound: ip=%u.%u.%u.%u/%u.%u.%u.%u gw=%u.%u.%u.%u dns=%u.%u.%u.%u\n",
                      dev->name,
                      (dev->ip >> 24) & 0xff, (dev->ip >> 16) & 0xff, (dev->ip >> 8) & 0xff, dev->ip & 0xff,
                      (dev->netmask >> 24) & 0xff, (dev->netmask >> 16) & 0xff, (dev->netmask >> 8) & 0xff, dev->netmask & 0xff,
                      (dev->gateway >> 24) & 0xff, (dev->gateway >> 16) & 0xff, (dev->gateway >> 8) & 0xff, dev->gateway & 0xff,
                      (dev->dns >> 24) & 0xff, (dev->dns >> 16) & 0xff, (dev->dns >> 8) & 0xff, dev->dns & 0xff);
    }
}

void dhcp_start(netdev_t *dev) {
    g_dev = dev;
    g_state = ST_DISCOVER;
    g_xid = (uint32_t)(sched_now_ns() ^ 0x1a2b3c4dull);
    if (!g_xid) g_xid = 0xdeadbeef;
    udp_bind(DHCP_SPORT, dhcp_rx);
    uint8_t pkt[300];
    int n = build_packet(pkt, DHCP_DISCOVER);
    udp_send(dev, 0xFFFFFFFFu, DHCP_SPORT, DHCP_DPORT, pkt, (size_t)n);
}

int dhcp_bound(void) { return g_state == ST_BOUND; }
