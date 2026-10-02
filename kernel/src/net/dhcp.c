#include "../../include/net/dhcp.h"
#include "../../include/net/udp.h"
#include "../../include/net/net.h"
#include "../../include/drivers/timer.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stdbool.h>

#define DHCP_SPORT 68
#define DHCP_DPORT 67
#define DHCP_MAGIC 0x63825363u

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5
#define DHCP_NAK      6

#define OPT_SUBNET    1
#define OPT_ROUTER    3
#define OPT_DNS       6
#define OPT_HOSTNAME  12
#define OPT_REQ_IP    50
#define OPT_LEASE     51
#define OPT_MSG_TYPE  53
#define OPT_SERVER_ID 54
#define OPT_PARAM_REQ 55
#define OPT_CLIENT_ID 61
#define OPT_END       255

#define SEC 1000000000ULL
#define MAX_LEASES 8

enum { ST_INIT, ST_SELECTING, ST_REQUESTING, ST_BOUND, ST_RENEWING };

typedef struct {
    netdev_t *dev;
    int       state;
    uint32_t  xid;
    uint32_t  server;
    uint32_t  offered;
    uint64_t  next_ns;
    uint64_t  t1_ns;
    uint64_t  expire_ns;
    int       tries;
    int       had_address;
} lease_t;

static lease_t g_leases[MAX_LEASES];
static int     g_bound_port;

static lease_t *lease_for(netdev_t *dev, int create)
{
    for (int i = 0; i < MAX_LEASES; i++)
        if (g_leases[i].dev == dev) return &g_leases[i];
    if (!create) return NULL;
    for (int i = 0; i < MAX_LEASES; i++)
        if (!g_leases[i].dev) {
            memset(&g_leases[i], 0, sizeof(g_leases[i]));
            g_leases[i].dev = dev;
            return &g_leases[i];
        }
    return NULL;
}

static void new_xid(lease_t *l)
{
    l->xid = (uint32_t)(sched_now_ns() ^ ((uint64_t)(uintptr_t)l->dev << 7) ^ 0x1a2b3c4dull);
    if (!l->xid) l->xid = 0xdeadbeef;
}

static int build_packet(lease_t *l, uint8_t *buf, uint8_t msg_type, uint32_t ciaddr)
{
    netdev_t *dev = l->dev;
    memset(buf, 0, 300);
    buf[0] = 1;
    buf[1] = 1;
    buf[2] = 6;
    wr32be(buf + 4, l->xid);
    wr16be(buf + 10, ciaddr ? 0 : 0x8000);
    wr32be(buf + 12, ciaddr);
    memcpy(buf + 28, dev->mac, 6);
    wr32be(buf + 236, DHCP_MAGIC);

    uint8_t *o = buf + 240;
    *o++ = OPT_MSG_TYPE; *o++ = 1; *o++ = msg_type;
    *o++ = OPT_CLIENT_ID; *o++ = 7; *o++ = 1; memcpy(o, dev->mac, 6); o += 6;
    if (msg_type == DHCP_REQUEST && !ciaddr) {
        *o++ = OPT_REQ_IP;    *o++ = 4; wr32be(o, l->offered); o += 4;
        *o++ = OPT_SERVER_ID; *o++ = 4; wr32be(o, l->server);  o += 4;
    }
    static const char host[] = "cervus";
    *o++ = OPT_HOSTNAME; *o++ = (uint8_t)(sizeof(host) - 1); memcpy(o, host, sizeof(host) - 1); o += sizeof(host) - 1;
    *o++ = OPT_PARAM_REQ; *o++ = 4; *o++ = OPT_SUBNET; *o++ = OPT_ROUTER; *o++ = OPT_DNS; *o++ = OPT_LEASE;
    *o++ = OPT_END;
    return (int)(o - buf);
}

static const uint8_t *find_option(const uint8_t *opt, size_t len, uint8_t code, uint8_t *out_len)
{
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

static void drop_address(lease_t *l, const char *why)
{
    netdev_t *dev = l->dev;
    if (l->had_address && !dev->static_cfg) {
        serial_printf("[dhcp] %s: released %u.%u.%u.%u (%s)\n", dev->name,
                      (dev->ip >> 24) & 0xff, (dev->ip >> 16) & 0xff, (dev->ip >> 8) & 0xff, dev->ip & 0xff, why);
        dev->ip = 0;
        dev->netmask = 0;
        dev->gateway = 0;
        dev->dns = 0;
    }
    l->had_address = 0;
    l->state = ST_INIT;
    l->tries = 0;
    l->next_ns = 0;
}

static void send_msg(lease_t *l, uint8_t type, uint32_t ciaddr)
{
    uint8_t pkt[300];
    int n = build_packet(l, pkt, type, ciaddr);
    udp_send(l->dev, 0xFFFFFFFFu, DHCP_SPORT, DHCP_DPORT, pkt, (size_t)n);
}

static void dhcp_rx(netdev_t *dev, uint32_t src_ip, uint16_t sport, uint16_t dport,
                    const uint8_t *p, size_t len)
{
    (void)sport; (void)dport;
    lease_t *l = lease_for(dev, 0);
    if (!l || len < 240) return;
    if (p[0] != 2 || rd32be(p + 4) != l->xid) return;
    if (rd32be(p + 236) != DHCP_MAGIC) return;
    if (memcmp(p + 28, dev->mac, 6)) return;

    const uint8_t *opt = p + 240;
    size_t optlen = len - 240;
    uint8_t ol;
    const uint8_t *mt = find_option(opt, optlen, OPT_MSG_TYPE, &ol);
    if (!mt) return;
    uint32_t yiaddr = rd32be(p + 16);
    const uint8_t *sid = find_option(opt, optlen, OPT_SERVER_ID, &ol);
    uint32_t server = sid && ol >= 4 ? rd32be(sid) : src_ip;

    if (mt[0] == DHCP_OFFER && l->state == ST_SELECTING) {
        if (!yiaddr) return;
        l->offered = yiaddr;
        l->server = server;
        l->state = ST_REQUESTING;
        l->tries = 0;
        l->next_ns = sched_now_ns() + 4 * SEC;
        send_msg(l, DHCP_REQUEST, 0);
        return;
    }
    if (mt[0] == DHCP_NAK && (l->state == ST_REQUESTING || l->state == ST_RENEWING)) {
        drop_address(l, "refused by the server");
        return;
    }
    if (mt[0] != DHCP_ACK || (l->state != ST_REQUESTING && l->state != ST_RENEWING)) return;
    if (!yiaddr) return;
    const uint8_t *sn = find_option(opt, optlen, OPT_SUBNET, &ol);
    uint32_t mask = sn && ol >= 4 ? rd32be(sn) : IP4(255, 255, 255, 0);
    const uint8_t *rt = find_option(opt, optlen, OPT_ROUTER, &ol);
    uint32_t gw = rt && ol >= 4 ? rd32be(rt) : 0;
    const uint8_t *dn = find_option(opt, optlen, OPT_DNS, &ol);
    uint32_t dns = dn && ol >= 4 ? rd32be(dn) : 0;
    const uint8_t *lt = find_option(opt, optlen, OPT_LEASE, &ol);
    uint32_t lease = lt && ol >= 4 ? rd32be(lt) : 3600;
    if (lease < 60) lease = 60;
    bool changed = dev->ip != yiaddr || dev->netmask != mask || dev->gateway != gw;
    if (!dev->static_cfg) {
        dev->ip = yiaddr;
        dev->netmask = mask;
        dev->gateway = gw;
        dev->dns = dns;
    }
    uint64_t now = sched_now_ns();
    l->server = server;
    l->state = ST_BOUND;
    l->had_address = 1;
    l->tries = 0;
    l->t1_ns = lease == 0xffffffffu ? ~0ULL : now + (uint64_t)lease * SEC / 2;
    l->expire_ns = lease == 0xffffffffu ? ~0ULL : now + (uint64_t)lease * SEC;
    if (changed)
        serial_printf("[dhcp] %s bound: ip=%u.%u.%u.%u/%u.%u.%u.%u gw=%u.%u.%u.%u dns=%u.%u.%u.%u lease=%us\n",
                      dev->name,
                      (yiaddr >> 24) & 0xff, (yiaddr >> 16) & 0xff, (yiaddr >> 8) & 0xff, yiaddr & 0xff,
                      (mask >> 24) & 0xff, (mask >> 16) & 0xff, (mask >> 8) & 0xff, mask & 0xff,
                      (gw >> 24) & 0xff, (gw >> 16) & 0xff, (gw >> 8) & 0xff, gw & 0xff,
                      (dns >> 24) & 0xff, (dns >> 16) & 0xff, (dns >> 8) & 0xff, dns & 0xff, lease);
}

static uint64_t backoff(int tries)
{
    int s = tries < 4 ? (2 << tries) : 30;
    return (uint64_t)s * SEC;
}

void dhcp_tick(uint64_t now)
{
    if (!g_bound_port) {
        if (udp_bind(DHCP_SPORT, dhcp_rx) == 0) g_bound_port = 1;
    }
    for (netdev_t *dev = netdev_first(); dev; dev = dev->next) {
        if (dev->is_loopback || dev->static_cfg) continue;
        lease_t *l = lease_for(dev, dev->link_up);
        if (!l) continue;
        if (!dev->link_up) {
            if (l->state != ST_INIT || l->had_address) drop_address(l, "link down");
            continue;
        }
        switch (l->state) {
        case ST_INIT:
            if (now < l->next_ns) break;
            new_xid(l);
            l->state = ST_SELECTING;
            l->next_ns = now + backoff(l->tries);
            l->tries++;
            send_msg(l, DHCP_DISCOVER, 0);
            break;
        case ST_SELECTING:
        case ST_REQUESTING:
            if (now >= l->next_ns) {
                l->state = ST_INIT;
                l->next_ns = now;
            }
            break;
        case ST_BOUND:
            if (now >= l->t1_ns) {
                new_xid(l);
                l->state = ST_RENEWING;
                l->next_ns = now + 10 * SEC;
                send_msg(l, DHCP_REQUEST, dev->ip);
            }
            break;
        case ST_RENEWING:
            if (now >= l->expire_ns) {
                drop_address(l, "lease expired");
            } else if (now >= l->next_ns) {
                l->next_ns = now + 10 * SEC;
                send_msg(l, DHCP_REQUEST, dev->ip);
            }
            break;
        }
    }
}

int dhcp_bound(netdev_t *dev)
{
    lease_t *l = lease_for(dev, 0);
    return l && (l->state == ST_BOUND || l->state == ST_RENEWING);
}
