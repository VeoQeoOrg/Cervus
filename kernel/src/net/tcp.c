#include "../../include/io/serial.h"
#include "../../include/net/tcp.h"
#include "../../include/net/ip.h"
#include "../../include/net/ipv6.h"
#include "../../include/net/socket.h"
#include "../../include/net/net.h"
#include "../../include/net/netdev.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/waitq.h"
#include "../../include/signal/signal.h"
#include "../../include/sched/spinlock.h"
#include "../../include/drivers/timer.h"
#include "../../include/syscall/syscall_internal.h"
#include "../../include/syscall/errno.h"
#include "../../include/fs/poll.h"
#include <string.h>
#include <stdlib.h>

#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PSH 0x08
#define TH_ACK 0x10

enum { TCP_CLOSED, TCP_LISTEN, TCP_SYN_SENT, TCP_SYN_RCVD, TCP_ESTABLISHED,
       TCP_FIN_WAIT1, TCP_FIN_WAIT2, TCP_CLOSE_WAIT, TCP_LAST_ACK, TCP_TIME_WAIT };

#define TCP_RTO_MIN      200
#define TCP_RTO_MAX      120000
#define TCP_RTO_INIT     1000
#define TCP_WSCALE       7
#define TCP_INIT_CWND    10
#define TCP_DEF_MSS      536
#define TCP_SYN_RETRIES  6
#define TCP_RETRIES2     15
#define TCP_FIN_TIMEOUT  60000
#define TCP_TIMEWAIT_MS  2000
#define TCP_WMEM_MAX     (4u << 20)
#define TCP_CWND_MAX     (64u << 20)
#define TCP_OOO_MAX      16
#define TCP_STACK_SEG    1520
#define TCP_SOMAXCONN    4096

typedef struct tcp_ooo {
    uint32_t seq;
    uint32_t len;
    uint8_t *data;
    struct tcp_ooo *next;
} tcp_ooo_t;

struct tcp_tcb {
    int       state;
    int       family;
    uint8_t   laddr6[16], raddr6[16];
    uint32_t  local_ip, remote_ip;
    uint16_t  local_port, remote_port;

    uint32_t  iss, snd_una, snd_nxt;
    uint32_t  rcv_nxt;
    uint32_t  snd_wnd;

    uint8_t   snd_wscale, rcv_wscale;
    uint8_t   wscale_ok;
    uint32_t  mss, smss, peer_mss;
    uint32_t  cwnd, ssthresh;
    uint32_t  dupacks;
    uint32_t  rtt_seq;
    uint64_t  rtt_start;
    int       rtt_timing;
    int32_t   srtt, rttvar;
    uint32_t  rto_ms;

    uint8_t  *sndbuf;
    uint32_t  snd_cap, snd_head, snd_buflen, snd_lim;
    int       snd_locked;
    int       pend_syn, pend_fin, fin_sent;
    int       nodelay;

    uint8_t  *rcvbuf;
    uint32_t  rcv_cap, rcv_head, rcv_count;
    uint32_t  rcv_adv;
    int       fin_seen, reset, app_closed;
    int       err;

    uint64_t  rexmit_ms;
    int       retries;
    uint64_t  close_ms;

    int       ka_on, ka_idle, ka_intvl, ka_cnt, ka_probes;
    uint64_t  last_rx_ms;

    struct tcp_ooo *ooo;

    int       is_listener;
    int       backlog, qlen;
    tcp_opts_t child_opts;
    struct tcp_tcb *listener;
    struct tcp_tcb *accept_q;
    struct tcp_tcb *accept_next;

    spinlock_t lock;
    waitq_t    wq;
    int        refs;
    struct tcp_tcb *next;
};

static tcp_tcb_t  *g_tcbs;
static spinlock_t  g_tcbs_lock;

static uint64_t now_ms(void) { return sched_now_ns() / 1000000ull; }
static netdev_t *tdev(void) { return netdev_default(); }

static uint64_t deadline_of(uint64_t timeout_ms) {
    return timeout_ms ? sched_now_ns() + timeout_ms * 1000000ull : 0;
}

static int expired(uint64_t deadline) {
    return deadline && sched_now_ns() >= deadline;
}

static int interrupted(task_t *me) {
    return me && (me->pending_kill || signal_pending_deliverable(me));
}

static void ring_put(uint8_t *ring, uint32_t cap, uint32_t pos, const uint8_t *src, uint32_t n) {
    uint32_t first = cap - pos;
    if (first > n) first = n;
    memcpy(ring + pos, src, first);
    if (n > first) memcpy(ring, src + first, n - first);
}

static void ring_get(const uint8_t *ring, uint32_t cap, uint32_t pos, uint8_t *dst, uint32_t n) {
    uint32_t first = cap - pos;
    if (first > n) first = n;
    memcpy(dst, ring + pos, first);
    if (n > first) memcpy(dst + first, ring, n - first);
}

static uint32_t rcv_window(const tcp_tcb_t *t) {
    return t->rcv_cap - t->rcv_count;
}

static uint32_t rcv_push(tcp_tcb_t *t, const uint8_t *data, uint32_t dlen) {
    uint32_t space = rcv_window(t);
    uint32_t n = dlen < space ? dlen : space;
    if (n) ring_put(t->rcvbuf, t->rcv_cap, (t->rcv_head + t->rcv_count) % t->rcv_cap, data, n);
    t->rcv_count += n;
    t->rcv_nxt += n;
    return n;
}

static void ooo_free_all(tcp_tcb_t *t) {
    tcp_ooo_t *q = t->ooo;
    while (q) {
        tcp_ooo_t *nx = q->next;
        free(q->data);
        free(q);
        q = nx;
    }
    t->ooo = NULL;
}

static void ooo_insert(tcp_tcb_t *t, uint32_t seq, const uint8_t *data, uint32_t dlen) {
    uint32_t ahead = seq - t->rcv_nxt;
    uint32_t wnd = rcv_window(t);
    if (ahead >= wnd) return;
    if (dlen > wnd - ahead) dlen = wnd - ahead;

    int count = 0;
    for (tcp_ooo_t *q = t->ooo; q; q = q->next) {
        if (q->seq == seq && q->len >= dlen) return;
        count++;
    }
    if (count >= TCP_OOO_MAX) return;

    tcp_ooo_t *n = calloc(1, sizeof(*n));
    if (!n) return;
    n->data = malloc(dlen);
    if (!n->data) { free(n); return; }
    memcpy(n->data, data, dlen);
    n->seq = seq;
    n->len = dlen;

    tcp_ooo_t **pp = &t->ooo;
    while (*pp && (int32_t)((*pp)->seq - seq) < 0) pp = &(*pp)->next;
    n->next = *pp;
    *pp = n;
}

static void ooo_drain(tcp_tcb_t *t) {
    int progress = 1;
    while (progress) {
        progress = 0;
        tcp_ooo_t **pp = &t->ooo;
        while (*pp) {
            tcp_ooo_t *q = *pp;
            int32_t off = (int32_t)(t->rcv_nxt - q->seq);
            if (off >= 0 && (uint32_t)off < q->len) {
                uint32_t want = q->len - (uint32_t)off;
                if (rcv_push(t, q->data + off, want) < want) return;
                *pp = q->next;
                free(q->data);
                free(q);
                progress = 1;
                continue;
            }
            if (off >= (int32_t)q->len) {
                *pp = q->next;
                free(q->data);
                free(q);
                progress = 1;
                continue;
            }
            pp = &q->next;
        }
    }
}

static uint32_t mss_for(netdev_t *dev, int v6) {
    uint32_t mtu = (dev && dev->mtu) ? dev->mtu : 1500;
    if (mtu > 65535) mtu = 65535;
    uint32_t hdr = v6 ? 60 : 40;
    if (mtu < hdr + TCP_DEF_MSS) return TCP_DEF_MSS;
    return mtu - hdr;
}

static void tcp_rto_init(tcp_tcb_t *t, uint32_t mss) {
    t->srtt = -1;
    t->rttvar = 0;
    t->rto_ms = TCP_RTO_INIT;
    t->mss = mss;
    t->smss = mss < TCP_DEF_MSS ? mss : TCP_DEF_MSS;
    t->peer_mss = TCP_DEF_MSS;
    t->cwnd = TCP_INIT_CWND * t->smss;
    t->ssthresh = 0x7fffffffu;
    t->rcv_wscale = TCP_WSCALE;
}

static void snd_autotune(tcp_tcb_t *t) {
    if (t->snd_locked) return;
    uint32_t segs = t->cwnd / t->smss;
    if (segs < TCP_INIT_CWND) segs = TCP_INIT_CWND;
    uint64_t want = 2ull * segs * t->smss;
    if (want > TCP_WMEM_MAX) want = TCP_WMEM_MAX;
    if (want > t->snd_lim) t->snd_lim = (uint32_t)want;
}

static void handshake_done(tcp_tcb_t *t) {
    t->smss = t->mss < t->peer_mss ? t->mss : t->peer_mss;
    t->cwnd = TCP_INIT_CWND * t->smss;
    t->ssthresh = 0x7fffffffu;
    t->rexmit_ms = 0;
    t->retries = 0;
    snd_autotune(t);
}

static void tcp_rtt_update(tcp_tcb_t *t, int32_t sample) {
    if (sample < 1) sample = 1;
    if (t->srtt < 0) {
        t->srtt = sample;
        t->rttvar = sample / 2;
    } else {
        int32_t d = t->srtt - sample;
        if (d < 0) d = -d;
        t->rttvar = (3 * t->rttvar + d) / 4;
        t->srtt = (7 * t->srtt + sample) / 8;
    }
    int32_t rto = t->srtt + 4 * t->rttvar;
    if (rto < TCP_RTO_MIN) rto = TCP_RTO_MIN;
    if (rto > TCP_RTO_MAX) rto = TCP_RTO_MAX;
    t->rto_ms = (uint32_t)rto;
}

static void tcp_loss(tcp_tcb_t *t) {
    uint32_t inflight = t->snd_nxt - t->snd_una;
    uint32_t half = inflight / 2;
    if (half < 2 * t->smss) half = 2 * t->smss;
    t->ssthresh = half;
    t->cwnd = t->smss;
    t->dupacks = 0;
}

static void tcb_wake(tcp_tcb_t *t) {
    waitq_wake_all(&t->wq);
}

static tcp_tcb_t *tcb_hold(tcp_tcb_t *t) {
    if (t) __atomic_add_fetch(&t->refs, 1, __ATOMIC_ACQ_REL);
    return t;
}

static void tcb_drop(tcp_tcb_t *t) {
    if (t) __atomic_sub_fetch(&t->refs, 1, __ATOMIC_ACQ_REL);
}

static int tcb_has_waiters(tcp_tcb_t *t) {
    for (int i = 0; i < WAITQ_SLOTS; i++)
        if (t->wq.slot[i]) return 1;
    return 0;
}

static uint16_t tcp_csum(uint32_t src, uint32_t dst, const uint8_t *seg, size_t len) {
    uint32_t sum = 0;
    sum += (src >> 16) & 0xffff; sum += src & 0xffff;
    sum += (dst >> 16) & 0xffff; sum += dst & 0xffff;
    sum += 6;
    sum += (uint32_t)len & 0xffff;
    for (size_t i = 0; i + 1 < len; i += 2) sum += (uint32_t)((seg[i] << 8) | seg[i + 1]);
    if (len & 1) sum += (uint32_t)(seg[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint16_t tcp6_csum(const uint8_t *src, const uint8_t *dst, const uint8_t *seg, size_t len) {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i += 2) sum += (uint32_t)((src[i] << 8) | src[i + 1]);
    for (int i = 0; i < 16; i += 2) sum += (uint32_t)((dst[i] << 8) | dst[i + 1]);
    sum += (uint32_t)len & 0xffff; sum += (uint32_t)(len >> 16);
    sum += 6;
    for (size_t i = 0; i + 1 < len; i += 2) sum += (uint32_t)((seg[i] << 8) | seg[i + 1]);
    if (len & 1) sum += (uint32_t)(seg[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static void tcp_try_send(tcp_tcb_t *t);

static void tcp_emit(tcp_tcb_t *t, uint8_t *seg, uint32_t hlen, uint32_t seq, uint8_t flags, uint32_t dlen) {
    netdev_t *dev = tdev();
    if (!dev) return;

    wr16be(seg + 0, t->local_port);
    wr16be(seg + 2, t->remote_port);
    wr32be(seg + 4, seq);
    wr32be(seg + 8, (flags & TH_ACK) ? t->rcv_nxt : 0);
    seg[12] = (uint8_t)((hlen / 4) << 4);
    seg[13] = flags;

    uint32_t win = t->rcvbuf ? rcv_window(t) : 0;
    uint32_t adv;
    if (flags & TH_SYN) {
        if (win > 65535) win = 65535;
        adv = win;
    } else {
        win >>= t->rcv_wscale;
        if (win > 65535) win = 65535;
        adv = win << t->rcv_wscale;
    }
    if (!(flags & TH_RST)) t->rcv_adv = t->rcv_nxt + adv;
    wr16be(seg + 14, (uint16_t)win);
    wr16be(seg + 16, 0);
    wr16be(seg + 18, 0);

    if (t->family == AF_INET6) {
        wr16be(seg + 16, tcp6_csum(t->laddr6, t->raddr6, seg, hlen + dlen));
        ipv6_output(dev, t->laddr6, t->raddr6, IPPROTO_TCP, seg, hlen + dlen);
        return;
    }
    wr16be(seg + 16, tcp_csum(t->local_ip, t->remote_ip, seg, hlen + dlen));
    ip_send(dev, t->remote_ip, IPPROTO_TCP, seg, hlen + dlen, IP_DEFAULT_TTL);
}

static void tcp_output(tcp_tcb_t *t, uint32_t seq, uint8_t flags) {
    uint8_t seg[40];
    uint32_t hlen = 20;
    if (flags & TH_SYN) {
        uint8_t *o = seg + 20;
        o[0] = 2; o[1] = 4;
        wr16be(o + 2, (uint16_t)t->mss);
        hlen = 24;
        if (!(flags & TH_ACK) || t->wscale_ok) {
            o[4] = 1; o[5] = 3; o[6] = 3; o[7] = t->rcv_wscale;
            hlen = 28;
        }
    }
    tcp_emit(t, seg, hlen, seq, flags, 0);
}

static void tcp_parse_opts(tcp_tcb_t *t, const uint8_t *seg, uint32_t doff) {
    uint32_t i = 20;
    while (i < doff) {
        uint8_t kind = seg[i];
        if (kind == 0) break;
        if (kind == 1) { i++; continue; }
        if (i + 1 >= doff) break;
        uint8_t olen = seg[i + 1];
        if (olen < 2 || i + olen > doff) break;
        if (kind == 2 && olen == 4) {
            uint16_t m = rd16be(seg + i + 2);
            if (m >= 88) t->peer_mss = m;
        } else if (kind == 3 && olen == 3) {
            uint8_t ws = seg[i + 2];
            if (ws > 14) ws = 14;
            t->snd_wscale = ws;
            t->wscale_ok = 1;
        }
        i += olen;
    }
}

static void tcp_reject(netdev_t *dev, uint32_t dst_ip, const uint8_t *dst6, const uint8_t *src6,
                       uint16_t sport, uint16_t dport,
                       uint32_t seq, uint32_t ack, uint8_t in_flags,
                       uint32_t seglen) {
    if (in_flags & TH_RST) return;
    if (!dev) dev = tdev();
    if (!dev) return;

    uint8_t seg[20];
    uint32_t out_seq, out_ack;
    uint8_t out_flags;

    if (in_flags & TH_ACK) {
        out_seq = ack;
        out_ack = 0;
        out_flags = TH_RST;
    } else {
        out_seq = 0;
        out_ack = seq + seglen + ((in_flags & TH_SYN) ? 1u : 0u) + ((in_flags & TH_FIN) ? 1u : 0u);
        out_flags = TH_RST | TH_ACK;
    }

    wr16be(seg + 0, dport);
    wr16be(seg + 2, sport);
    wr32be(seg + 4, out_seq);
    wr32be(seg + 8, out_ack);
    seg[12] = 5 << 4;
    seg[13] = out_flags;
    wr16be(seg + 14, 0);
    wr16be(seg + 16, 0);
    wr16be(seg + 18, 0);

    if (dst6) {
        uint8_t src[16];
        if (src6) memcpy(src, src6, 16); else memcpy(src, dev->ip6_ll, 16);
        wr16be(seg + 16, tcp6_csum(src, dst6, seg, 20));
        ipv6_output(dev, src, dst6, IPPROTO_TCP, seg, 20);
        return;
    }
    wr16be(seg + 16, tcp_csum(ip_source_for(dev, dst_ip), dst_ip, seg, 20));
    ip_send(dev, dst_ip, IPPROTO_TCP, seg, 20, IP_DEFAULT_TTL);
}

static tcp_tcb_t *tcb_find(uint32_t rip, uint16_t rport, uint16_t lport) {
    for (tcp_tcb_t *t = g_tcbs; t; t = t->next)
        if (t->family != AF_INET6 && !t->is_listener && t->local_port == lport &&
            t->remote_port == rport && t->remote_ip == rip)
            return t;
    return NULL;
}

static tcp_tcb_t *tcb_find6(const uint8_t *rip6, uint16_t rport, uint16_t lport) {
    for (tcp_tcb_t *t = g_tcbs; t; t = t->next)
        if (t->family == AF_INET6 && !t->is_listener && t->local_port == lport &&
            t->remote_port == rport && memcmp(t->raddr6, rip6, 16) == 0)
            return t;
    return NULL;
}

static tcp_tcb_t *tcb_alloc(const tcp_opts_t *o) {
    tcp_tcb_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->rcv_cap = (o && o->rcvbuf) ? o->rcvbuf : TCP_RMEM_DEF;
    t->rcvbuf = malloc(t->rcv_cap);
    if (!t->rcvbuf) { free(t); return NULL; }
    if (o && o->sndbuf) { t->snd_lim = o->sndbuf; t->snd_locked = 1; }
    else                t->snd_lim = TCP_WMEM_DEF;
    t->nodelay = o ? o->nodelay : 0;
    t->ka_on = o ? o->keepalive : 0;
    t->ka_idle = (o && o->ka_idle) ? o->ka_idle : 7200;
    t->ka_intvl = (o && o->ka_intvl) ? o->ka_intvl : 75;
    t->ka_cnt = (o && o->ka_cnt) ? o->ka_cnt : 9;
    t->last_rx_ms = now_ms();
    return t;
}

static void tcb_free(tcp_tcb_t *t) {
    ooo_free_all(t);
    free(t->rcvbuf);
    free(t->sndbuf);
    free(t);
}

static void tcb_link(tcp_tcb_t *t) {
    uint64_t f = spinlock_acquire_irqsave(&g_tcbs_lock);
    t->next = g_tcbs;
    g_tcbs = t;
    spinlock_release_irqrestore(&g_tcbs_lock, f);
}

static uint32_t new_iss(uint32_t salt) {
    return (uint32_t)(sched_now_ns() / 4000ull) * 2654435761u ^ salt;
}

static void syn_start(tcp_tcb_t *t) {
    t->iss = new_iss(0xa5a5 ^ ((uint32_t)t->local_port << 16) ^ t->remote_port);
    t->snd_una = t->iss;
    t->snd_nxt = t->iss + 1;
    t->state = TCP_SYN_SENT;
    t->pend_syn = 1;
    t->rexmit_ms = now_ms() + t->rto_ms;
}

int tcp_connect_start(uint32_t ip, uint16_t port, uint16_t lport, const tcp_opts_t *o, tcp_tcb_t **out) {
    netdev_t *rdev = netdev_route(ip, NULL);
    netdev_t *any = rdev ? rdev : tdev();
    int local = (ip >> 24) == 127 || (any && ip_source_for(any, ip) == ip);
    netdev_t *dev = local ? netdev_loopback() : rdev;
    if (!dev || (!local && !dev->ip)) return -ENETUNREACH;

    tcp_tcb_t *t = tcb_alloc(o);
    if (!t) return -ENOMEM;

    t->local_ip = local ? ip : ip_source_for(dev, ip);
    t->remote_ip = ip;
    t->remote_port = port;
    t->local_port = lport;
    tcp_rto_init(t, mss_for(dev, 0));
    syn_start(t);
    tcb_link(t);

    tcp_output(t, t->iss, TH_SYN);
    *out = t;
    return 0;
}

static const uint8_t tcp_lo6[16] = { 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,1 };

int tcp_connect6_start(const uint8_t dst6[16], uint16_t port, uint16_t lport, const tcp_opts_t *o, tcp_tcb_t **out) {
    netdev_t *dev = tdev();
    if (!dev) return -ENETUNREACH;
    int local = memcmp(dst6, tcp_lo6, 16) == 0 || memcmp(dst6, dev->ip6_ll, 16) == 0;

    tcp_tcb_t *t = tcb_alloc(o);
    if (!t) return -ENOMEM;

    t->family = AF_INET6;
    memcpy(t->raddr6, dst6, 16);
    if (memcmp(dst6, tcp_lo6, 16) == 0) memcpy(t->laddr6, tcp_lo6, 16);
    else ipv6_get_lladdr(dev, t->laddr6);
    t->remote_port = port;
    t->local_port = lport;
    tcp_rto_init(t, mss_for(local ? netdev_loopback() : dev, 1));
    syn_start(t);
    tcb_link(t);

    tcp_output(t, t->iss, TH_SYN);
    *out = t;
    return 0;
}

int tcp_connect_status(tcp_tcb_t *t) {
    if (!t) return -EINVAL;
    uint64_t f = spinlock_acquire_irqsave(&t->lock);
    int st = t->state, rst = t->reset, err = t->err;
    spinlock_release_irqrestore(&t->lock, f);
    if (st == TCP_SYN_SENT || st == TCP_SYN_RCVD) return -EINPROGRESS;
    if (rst || (st == TCP_CLOSED && err)) return -(err ? err : ECONNREFUSED);
    return 0;
}

int tcp_port_in_use(uint16_t port) {
    int used = 0;
    uint64_t f = spinlock_acquire_irqsave(&g_tcbs_lock);
    for (tcp_tcb_t *t = g_tcbs; t; t = t->next)
        if (t->local_port == port && !(t->app_closed && t->state == TCP_CLOSED)) { used = 1; break; }
    spinlock_release_irqrestore(&g_tcbs_lock, f);
    return used;
}

int tcp_take_error(tcp_tcb_t *t) {
    if (!t) return 0;
    uint64_t f = spinlock_acquire_irqsave(&t->lock);
    int err = t->err;
    t->err = 0;
    spinlock_release_irqrestore(&t->lock, f);
    return err;
}

int tcp_connect_wait(tcp_tcb_t *t, uint64_t timeout_ms) {
    task_t *me = syscall_cur_task();
    uint64_t deadline = deadline_of(timeout_ms);
    for (;;) {
        loopback_drain_all();
        uint64_t f = spinlock_acquire_irqsave(&t->lock);
        if (t->state != TCP_SYN_SENT && t->state != TCP_SYN_RCVD) {
            int err = (t->reset || t->state == TCP_CLOSED) ? (t->err ? t->err : ECONNREFUSED) : 0;
            if (err) t->err = 0;
            spinlock_release_irqrestore(&t->lock, f);
            return -err;
        }
        if (interrupted(me)) { spinlock_release_irqrestore(&t->lock, f); return -EINTR; }
        if (expired(deadline)) { spinlock_release_irqrestore(&t->lock, f); return -EINPROGRESS; }
        waitq_block(&t->wq, &t->lock, f, deadline);
    }
}

int tcp_connect(uint32_t ip, uint16_t port, uint16_t lport, const tcp_opts_t *o,
                uint64_t timeout_ms, tcp_tcb_t **out) {
    tcp_tcb_t *t = NULL;
    int r = tcp_connect_start(ip, port, lport, o, &t);
    if (r != 0) { *out = NULL; return r; }
    *out = t;
    return tcp_connect_wait(t, timeout_ms);
}

static int send_open(int st) {
    return st == TCP_ESTABLISHED || st == TCP_CLOSE_WAIT ||
           st == TCP_FIN_WAIT1 || st == TCP_LAST_ACK;
}

static void tcp_try_send(tcp_tcb_t *t) {
    uint8_t stackseg[TCP_STACK_SEG];
    uint8_t *heap = NULL;
    uint32_t heapcap = 0;
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&t->lock);
        if (!send_open(t->state) || t->reset) {
            spinlock_release_irqrestore(&t->lock, f);
            break;
        }
        uint32_t inflight = t->snd_nxt - t->snd_una;
        uint32_t unsent = (t->snd_buflen > inflight) ? (t->snd_buflen - inflight) : 0;
        uint32_t wnd = t->snd_wnd;
        uint32_t cw = t->cwnd < wnd ? t->cwnd : wnd;
        uint32_t room = (inflight < cw) ? (cw - inflight) : 0;
        if (wnd == 0 && inflight == 0 && unsent) room = 1;
        uint32_t n = unsent;
        if (n > room) n = room;
        if (n > t->smss) n = t->smss;
        if (n && n < t->smss && n == unsent && inflight && !t->nodelay && !t->pend_fin) n = 0;

        if (n > 0) {
            uint8_t *seg = stackseg;
            if (20 + n > sizeof stackseg) {
                if (20 + n > heapcap) {
                    uint32_t want = 20 + t->smss;
                    spinlock_release_irqrestore(&t->lock, f);
                    free(heap);
                    heap = malloc(want);
                    heapcap = heap ? want : 0;
                    if (!heap) break;
                    continue;
                }
                seg = heap;
            }
            ring_get(t->sndbuf, t->snd_cap, (t->snd_head + inflight) % t->snd_cap, seg + 20, n);
            uint32_t seq = t->snd_nxt;
            t->snd_nxt += n;
            if (!t->rtt_timing) {
                t->rtt_timing = 1;
                t->rtt_seq = seq;
                t->rtt_start = now_ms();
            }
            if (t->rexmit_ms == 0) t->rexmit_ms = now_ms() + t->rto_ms;
            uint8_t fl = TH_ACK | (n == unsent ? TH_PSH : 0);
            spinlock_release_irqrestore(&t->lock, f);
            tcp_emit(t, seg, 20, seq, fl, n);
            continue;
        }

        if (t->pend_fin && !t->fin_sent && t->snd_buflen == 0 && t->snd_una == t->snd_nxt) {
            t->fin_sent = 1;
            uint32_t seq = t->snd_nxt;
            t->snd_nxt += 1;
            if (t->rexmit_ms == 0) t->rexmit_ms = now_ms() + t->rto_ms;
            spinlock_release_irqrestore(&t->lock, f);
            tcp_output(t, seq, TH_ACK | TH_FIN);
            break;
        }

        spinlock_release_irqrestore(&t->lock, f);
        break;
    }
    free(heap);
}

static uint32_t snd_space(const tcp_tcb_t *t) {
    return t->snd_buflen < t->snd_lim ? t->snd_lim - t->snd_buflen : 0;
}

int64_t tcp_send(tcp_tcb_t *t, const void *buf, size_t len, int flags, uint64_t timeout_ms) {
    if (!t) return -ENOTCONN;
    task_t *me = syscall_cur_task();
    const uint8_t *p = buf;
    size_t done = 0;
    uint64_t deadline = deadline_of(timeout_ms);

    while (done < len) {
        loopback_drain_all();
        uint64_t f = spinlock_acquire_irqsave(&t->lock);
        if (t->err) {
            int e = t->err;
            t->err = 0;
            spinlock_release_irqrestore(&t->lock, f);
            return done ? (int64_t)done : -e;
        }
        int st = t->state;
        if (t->reset || t->pend_fin || (st != TCP_ESTABLISHED && st != TCP_CLOSE_WAIT &&
                                        st != TCP_SYN_SENT && st != TCP_SYN_RCVD)) {
            spinlock_release_irqrestore(&t->lock, f);
            return done ? (int64_t)done : -EPIPE;
        }

        uint32_t space = (st == TCP_ESTABLISHED || st == TCP_CLOSE_WAIT) ? snd_space(t) : 0;
        if (space > 0) {
            uint32_t n = (len - done) < space ? (uint32_t)(len - done) : space;
            if (t->snd_buflen + n > t->snd_cap) {
                uint32_t want = t->snd_cap ? t->snd_cap : 4096;
                while (want < t->snd_buflen + n) want <<= 1;
                if (want > t->snd_lim && t->snd_lim >= t->snd_buflen + n) want = t->snd_lim;
                spinlock_release_irqrestore(&t->lock, f);
                uint8_t *nb = malloc(want);
                if (!nb) return done ? (int64_t)done : -ENOMEM;
                f = spinlock_acquire_irqsave(&t->lock);
                if (t->snd_cap < want) {
                    if (t->snd_buflen) ring_get(t->sndbuf, t->snd_cap, t->snd_head, nb, t->snd_buflen);
                    uint8_t *old = t->sndbuf;
                    t->sndbuf = nb;
                    t->snd_cap = want;
                    t->snd_head = 0;
                    nb = old;
                }
                spinlock_release_irqrestore(&t->lock, f);
                free(nb);
                continue;
            }
            ring_put(t->sndbuf, t->snd_cap, (t->snd_head + t->snd_buflen) % t->snd_cap, p + done, n);
            t->snd_buflen += n;
            done += n;
            spinlock_release_irqrestore(&t->lock, f);
            tcp_try_send(t);
            continue;
        }

        if (flags & TCP_IO_NONBLOCK) { spinlock_release_irqrestore(&t->lock, f); return done ? (int64_t)done : -EAGAIN; }
        if (interrupted(me))         { spinlock_release_irqrestore(&t->lock, f); return done ? (int64_t)done : -EINTR; }
        if (expired(deadline))       { spinlock_release_irqrestore(&t->lock, f); return done ? (int64_t)done : -EAGAIN; }
        waitq_block(&t->wq, &t->lock, f, deadline);
    }
    loopback_drain_all();
    return (int64_t)done;
}

static int window_update_due(tcp_tcb_t *t) {
    if (t->state != TCP_ESTABLISHED && t->state != TCP_FIN_WAIT1 && t->state != TCP_FIN_WAIT2)
        return 0;
    uint32_t right = t->rcv_nxt + rcv_window(t);
    int32_t grow = (int32_t)(right - t->rcv_adv);
    uint32_t thresh = t->rcv_cap / 2;
    if (2 * t->mss < thresh) thresh = 2 * t->mss;
    return grow >= (int32_t)thresh;
}

int64_t tcp_recv(tcp_tcb_t *t, void *buf, size_t len, int flags, uint64_t timeout_ms) {
    if (!t) return -ENOTCONN;
    if (len == 0) return 0;
    task_t *me = syscall_cur_task();
    uint8_t *dst = buf;
    size_t got = 0;
    size_t target = (flags & TCP_IO_WAITALL) ? len : 1;
    uint64_t deadline = deadline_of(timeout_ms);

    for (;;) {
        loopback_drain_all();
        uint64_t f = spinlock_acquire_irqsave(&t->lock);
        if (flags & TCP_IO_PEEK) {
            if (t->rcv_count >= target || (t->rcv_count && (t->fin_seen || t->reset ||
                                                            (flags & TCP_IO_NONBLOCK)))) {
                uint32_t n = t->rcv_count < len ? t->rcv_count : (uint32_t)len;
                ring_get(t->rcvbuf, t->rcv_cap, t->rcv_head, dst, n);
                spinlock_release_irqrestore(&t->lock, f);
                return (int64_t)n;
            }
        } else if (t->rcv_count > 0) {
            uint32_t n = t->rcv_count < len - got ? t->rcv_count : (uint32_t)(len - got);
            ring_get(t->rcvbuf, t->rcv_cap, t->rcv_head, dst + got, n);
            t->rcv_head = (t->rcv_head + n) % t->rcv_cap;
            t->rcv_count -= n;
            got += n;
            int upd = window_update_due(t);
            spinlock_release_irqrestore(&t->lock, f);
            if (upd) tcp_output(t, t->snd_nxt, TH_ACK);
            if (got >= target) return (int64_t)got;
            continue;
        }

        if (got == 0 && t->err) {
            int e = t->err;
            t->err = 0;
            spinlock_release_irqrestore(&t->lock, f);
            return -e;
        }
        if (t->fin_seen || t->reset || t->state == TCP_CLOSED) {
            spinlock_release_irqrestore(&t->lock, f);
            return (int64_t)got;
        }
        if (flags & TCP_IO_NONBLOCK) { spinlock_release_irqrestore(&t->lock, f); return got ? (int64_t)got : -EAGAIN; }
        if (interrupted(me))         { spinlock_release_irqrestore(&t->lock, f); return got ? (int64_t)got : -EINTR; }
        if (expired(deadline))       { spinlock_release_irqrestore(&t->lock, f); return got ? (int64_t)got : -EAGAIN; }
        waitq_block(&t->wq, &t->lock, f, deadline);
    }
}

int tcp_readable(tcp_tcb_t *t) {
    if (!t) return 0;
    uint64_t f = spinlock_acquire_irqsave(&t->lock);
    int n = (int)t->rcv_count;
    spinlock_release_irqrestore(&t->lock, f);
    return n;
}

void tcp_shutdown(tcp_tcb_t *t) {
    if (!t) return;
    uint64_t f = spinlock_acquire_irqsave(&t->lock);
    if (t->is_listener || t->pend_fin) { spinlock_release_irqrestore(&t->lock, f); return; }
    if (t->state == TCP_ESTABLISHED) {
        t->state = TCP_FIN_WAIT1;
        t->pend_fin = 1;
    } else if (t->state == TCP_CLOSE_WAIT) {
        t->state = TCP_LAST_ACK;
        t->pend_fin = 1;
    } else if (t->state == TCP_SYN_SENT || t->state == TCP_SYN_RCVD) {
        t->state = TCP_CLOSED;
        t->pend_syn = 0;
        t->rexmit_ms = 0;
    }
    tcb_wake(t);
    spinlock_release_irqrestore(&t->lock, f);
    tcp_try_send(t);
}

void tcp_abort(tcp_tcb_t *t) {
    if (!t) return;
    uint64_t f = spinlock_acquire_irqsave(&t->lock);
    int st = t->state;
    int send_rst = !t->reset && st != TCP_CLOSED && st != TCP_LISTEN && st != TCP_TIME_WAIT && st != TCP_SYN_SENT;
    uint32_t seq = t->snd_nxt;
    t->state = TCP_CLOSED;
    t->reset = 1;
    t->pend_syn = 0;
    t->rexmit_ms = 0;
    t->app_closed = 1;
    t->close_ms = now_ms();
    tcb_wake(t);
    spinlock_release_irqrestore(&t->lock, f);
    if (send_rst) tcp_output(t, seq, TH_RST | TH_ACK);
}

static void listener_close(tcp_tcb_t *lst) {
    tcp_tcb_t *orphans = NULL;
    uint64_t g = spinlock_acquire_irqsave(&g_tcbs_lock);
    lst->state = TCP_CLOSED;
    lst->app_closed = 1;
    lst->close_ms = now_ms();
    orphans = lst->accept_q;
    lst->accept_q = NULL;
    lst->qlen = 0;
    for (tcp_tcb_t *c = g_tcbs; c; c = c->next)
        if (c->listener == lst) c->listener = NULL;
    waitq_wake_all(&lst->wq);
    spinlock_release_irqrestore(&g_tcbs_lock, g);

    while (orphans) {
        tcp_tcb_t *c = orphans;
        orphans = c->accept_next;
        c->accept_next = NULL;
        tcp_abort(c);
    }
}

void tcp_close(tcp_tcb_t *t) {
    if (!t) return;
    if (t->is_listener) { listener_close(t); return; }
    uint64_t f = spinlock_acquire_irqsave(&t->lock);
    int unread = t->rcv_count > 0;
    spinlock_release_irqrestore(&t->lock, f);
    if (unread) { tcp_abort(t); return; }

    tcp_shutdown(t);
    f = spinlock_acquire_irqsave(&t->lock);
    t->app_closed = 1;
    t->close_ms = now_ms();
    spinlock_release_irqrestore(&t->lock, f);
}

void tcp_set_opts(tcp_tcb_t *t, const tcp_opts_t *o) {
    if (!t || !o) return;
    uint8_t *nb = NULL;
    uint32_t want = o->rcvbuf;
    if (!t->is_listener && want) nb = malloc(want);

    uint64_t f = spinlock_acquire_irqsave(&t->lock);
    if (t->is_listener) {
        t->child_opts = *o;
    } else {
        t->nodelay = o->nodelay;
        t->ka_on = o->keepalive;
        if (o->ka_idle) t->ka_idle = o->ka_idle;
        if (o->ka_intvl) t->ka_intvl = o->ka_intvl;
        if (o->ka_cnt) t->ka_cnt = o->ka_cnt;
        if (o->sndbuf) { t->snd_lim = o->sndbuf; t->snd_locked = 1; }
        if (nb && want != t->rcv_cap && want >= t->rcv_count) {
            if (t->rcv_count) ring_get(t->rcvbuf, t->rcv_cap, t->rcv_head, nb, t->rcv_count);
            uint8_t *old = t->rcvbuf;
            t->rcvbuf = nb;
            t->rcv_cap = want;
            t->rcv_head = 0;
            nb = old;
        }
    }
    spinlock_release_irqrestore(&t->lock, f);
    free(nb);
    if (!t->is_listener) tcp_try_send(t);
}

void tcp_get_bufs(tcp_tcb_t *t, uint32_t *rcvbuf, uint32_t *sndbuf) {
    uint64_t f = spinlock_acquire_irqsave(&t->lock);
    if (t->is_listener) {
        *rcvbuf = t->child_opts.rcvbuf ? t->child_opts.rcvbuf : TCP_RMEM_DEF;
        *sndbuf = t->child_opts.sndbuf ? t->child_opts.sndbuf : TCP_WMEM_DEF;
    } else {
        *rcvbuf = t->rcv_cap;
        *sndbuf = t->snd_lim;
    }
    spinlock_release_irqrestore(&t->lock, f);
}

uint32_t tcp_mss(tcp_tcb_t *t) {
    return t->is_listener ? TCP_DEF_MSS : t->smss;
}

uint32_t tcp_outq(tcp_tcb_t *t) {
    uint64_t f = spinlock_acquire_irqsave(&t->lock);
    uint32_t n = t->snd_buflen;
    spinlock_release_irqrestore(&t->lock, f);
    return n;
}

void tcp_peer(tcp_tcb_t *t, uint32_t *ip, uint8_t ip6[16], uint16_t *port) {
    if (ip) *ip = t->remote_ip;
    if (ip6) memcpy(ip6, t->raddr6, 16);
    if (port) *port = t->remote_port;
}

void tcp_local(tcp_tcb_t *t, uint32_t *ip, uint8_t ip6[16]) {
    if (ip) *ip = t->local_ip;
    if (ip6) memcpy(ip6, t->laddr6, 16);
}

static tcp_tcb_t *find_listener(uint16_t port, int family) {
    for (tcp_tcb_t *t = g_tcbs; t; t = t->next)
        if (t->is_listener && t->state == TCP_LISTEN && t->local_port == port &&
            (family == AF_INET6) == (t->family == AF_INET6))
            return t;
    return NULL;
}

static int clamp_backlog(int backlog) {
    if (backlog <= 0) return 1;
    return backlog > TCP_SOMAXCONN ? TCP_SOMAXCONN : backlog;
}

static tcp_tcb_t *listen_common(uint16_t port, int family, int backlog, const tcp_opts_t *o) {
    tcp_tcb_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->family = family;
    t->local_port = port;
    t->state = TCP_LISTEN;
    t->is_listener = 1;
    t->backlog = clamp_backlog(backlog);
    if (o) t->child_opts = *o;
    netdev_t *dev = tdev();
    if (family != AF_INET6) t->local_ip = dev ? dev->ip : 0;

    uint64_t f = spinlock_acquire_irqsave(&g_tcbs_lock);
    if (find_listener(port, family)) {
        spinlock_release_irqrestore(&g_tcbs_lock, f);
        free(t);
        return NULL;
    }
    t->next = g_tcbs;
    g_tcbs = t;
    spinlock_release_irqrestore(&g_tcbs_lock, f);
    return t;
}

tcp_tcb_t *tcp_listen(uint16_t port, int backlog, const tcp_opts_t *o) {
    return listen_common(port, AF_INET, backlog, o);
}

tcp_tcb_t *tcp_listen6(uint16_t port, int backlog, const tcp_opts_t *o) {
    return listen_common(port, AF_INET6, backlog, o);
}

void tcp_set_backlog(tcp_tcb_t *lst, int backlog) {
    uint64_t f = spinlock_acquire_irqsave(&g_tcbs_lock);
    lst->backlog = clamp_backlog(backlog);
    spinlock_release_irqrestore(&g_tcbs_lock, f);
}

int tcp_accept(tcp_tcb_t *lst, int flags, uint64_t timeout_ms, tcp_tcb_t **out) {
    task_t *me = syscall_cur_task();
    uint64_t deadline = deadline_of(timeout_ms);
    for (;;) {
        loopback_drain_all();
        uint64_t f = spinlock_acquire_irqsave(&g_tcbs_lock);
        if (lst->state != TCP_LISTEN) { spinlock_release_irqrestore(&g_tcbs_lock, f); return -EINVAL; }
        if (lst->accept_q) {
            tcp_tcb_t *c = lst->accept_q;
            lst->accept_q = c->accept_next;
            c->accept_next = NULL;
            c->listener = NULL;
            lst->qlen--;
            spinlock_release_irqrestore(&g_tcbs_lock, f);
            *out = c;
            return 0;
        }
        if (flags & TCP_IO_NONBLOCK) { spinlock_release_irqrestore(&g_tcbs_lock, f); return -EAGAIN; }
        if (interrupted(me))         { spinlock_release_irqrestore(&g_tcbs_lock, f); return -EINTR; }
        if (expired(deadline))       { spinlock_release_irqrestore(&g_tcbs_lock, f); return -EAGAIN; }
        waitq_block(&lst->wq, &g_tcbs_lock, f, deadline);
    }
}

static int writable(const tcp_tcb_t *t) {
    uint32_t free_ = snd_space(t);
    return t->snd_buflen == 0 || free_ * 2 >= t->snd_buflen;
}

int tcp_poll(tcp_tcb_t *t) {
    int r = 0;
    if (t->is_listener) {
        uint64_t g = spinlock_acquire_irqsave(&g_tcbs_lock);
        if (t->accept_q) r |= POLLIN;
        if (t->state != TCP_LISTEN) r |= POLLHUP;
        spinlock_release_irqrestore(&g_tcbs_lock, g);
        return r;
    }
    uint64_t f = spinlock_acquire_irqsave(&t->lock);
    int st = t->state;
    if (t->rcv_count > 0) r |= POLLIN;
    if (t->fin_seen || t->reset) r |= POLLIN | POLLRDHUP;
    if (t->err) r |= POLLERR;
    if (t->reset || st == TCP_CLOSED) r |= POLLHUP | POLLOUT;
    else if ((st == TCP_ESTABLISHED || st == TCP_CLOSE_WAIT) && writable(t)) r |= POLLOUT;
    else if (t->pend_fin) r |= POLLOUT;
    spinlock_release_irqrestore(&t->lock, f);
    return r;
}

static void tcp_child(tcp_tcb_t *lst, netdev_t *dev, int family,
                      uint32_t src, uint32_t dst, const uint8_t *src6, const uint8_t *dst6,
                      uint16_t sport, const uint8_t *seg, uint32_t doff) {
    uint64_t g = spinlock_acquire_irqsave(&g_tcbs_lock);
    int full = lst->state != TCP_LISTEN || lst->qlen >= lst->backlog;
    tcp_opts_t o = lst->child_opts;
    spinlock_release_irqrestore(&g_tcbs_lock, g);
    if (full) return;

    tcp_tcb_t *c = tcb_alloc(&o);
    if (!c) return;
    c->family = family;
    c->local_port = lst->local_port;
    c->remote_port = sport;
    if (family == AF_INET6) {
        memcpy(c->laddr6, dst6, 16);
        memcpy(c->raddr6, src6, 16);
    } else {
        c->local_ip = dst ? dst : ip_source_for(dev, src);
        c->remote_ip = src;
    }
    tcp_rto_init(c, mss_for(dev, family == AF_INET6));
    tcp_parse_opts(c, seg, doff);
    if (!c->wscale_ok) { c->snd_wscale = 0; c->rcv_wscale = 0; }
    c->snd_wnd = rd16be(seg + 14);
    c->rcv_nxt = rd32be(seg + 4) + 1;
    c->iss = new_iss(0x5a5a ^ ((uint32_t)sport << 16) ^ c->local_port);
    c->snd_una = c->iss;
    c->snd_nxt = c->iss + 1;
    c->state = TCP_SYN_RCVD;
    c->pend_syn = 1;
    c->listener = lst;
    c->rexmit_ms = now_ms() + c->rto_ms;
    tcb_link(c);
    tcp_output(c, c->iss, TH_SYN | TH_ACK);
}

static void enqueue_accept(tcp_tcb_t *t) {
    int orphan = 0;
    uint64_t g = spinlock_acquire_irqsave(&g_tcbs_lock);
    tcp_tcb_t *lst = t->listener;
    if (!lst || lst->state != TCP_LISTEN) {
        orphan = 1;
    } else {
        tcp_tcb_t **pp = &lst->accept_q;
        while (*pp) pp = &(*pp)->accept_next;
        *pp = t;
        t->accept_next = NULL;
        lst->qlen++;
        waitq_wake_all(&lst->wq);
    }
    spinlock_release_irqrestore(&g_tcbs_lock, g);
    if (orphan) tcp_abort(t);
}

static void tcp_input(tcp_tcb_t *t, const uint8_t *seg, size_t len) {
    uint32_t seq   = rd32be(seg + 4);
    uint32_t ack   = rd32be(seg + 8);
    uint32_t doff  = (uint32_t)(seg[12] >> 4) * 4;
    uint8_t  flags = seg[13];
    uint16_t win   = rd16be(seg + 14);
    if (doff < 20 || doff > len) return;
    const uint8_t *data = seg + doff;
    uint32_t dlen = (uint32_t)(len - doff);

    uint64_t f = spinlock_acquire_irqsave(&t->lock);
    t->last_rx_ms = now_ms();
    t->ka_probes = 0;

    if (flags & TH_RST) {
        int st = t->state;
        if (st == TCP_SYN_SENT && (!(flags & TH_ACK) || ack != t->iss + 1)) {
            spinlock_release_irqrestore(&t->lock, f);
            return;
        }
        if (!t->err && !t->app_closed && st != TCP_CLOSED && st != TCP_TIME_WAIT)
            t->err = st == TCP_SYN_SENT ? ECONNREFUSED : st == TCP_CLOSE_WAIT ? EPIPE : ECONNRESET;
        t->reset = 1;
        t->state = TCP_CLOSED;
        t->pend_syn = 0;
        t->rexmit_ms = 0;
        tcb_wake(t);
        spinlock_release_irqrestore(&t->lock, f);
        return;
    }

    if (t->state == TCP_SYN_SENT) {
        if ((flags & TH_SYN) && (flags & TH_ACK) && ack == t->iss + 1) {
            tcp_parse_opts(t, seg, doff);
            if (!t->wscale_ok) { t->snd_wscale = 0; t->rcv_wscale = 0; }
            t->rcv_nxt = seq + 1;
            t->snd_una = ack;
            t->snd_wnd = win;
            t->pend_syn = 0;
            t->state = TCP_ESTABLISHED;
            handshake_done(t);
            tcb_wake(t);
            spinlock_release_irqrestore(&t->lock, f);
            tcp_output(t, t->snd_nxt, TH_ACK);
            return;
        }
        int bad_ack = (flags & TH_ACK) && ack != t->iss + 1;
        spinlock_release_irqrestore(&t->lock, f);
        if (bad_ack) {
            uint8_t rst[20];
            tcp_emit(t, rst, 20, ack, TH_RST, 0);
        }
        return;
    }

    if (t->state == TCP_SYN_RCVD) {
        if ((flags & TH_SYN) && !(flags & TH_ACK)) {
            uint32_t iss = t->iss;
            spinlock_release_irqrestore(&t->lock, f);
            tcp_output(t, iss, TH_SYN | TH_ACK);
            return;
        }
        if (!(flags & TH_ACK) || ack != t->iss + 1) {
            spinlock_release_irqrestore(&t->lock, f);
            return;
        }
        t->snd_una = ack;
        t->snd_wnd = (uint32_t)win << t->snd_wscale;
        t->pend_syn = 0;
        t->state = TCP_ESTABLISHED;
        handshake_done(t);
        spinlock_release_irqrestore(&t->lock, f);
        enqueue_accept(t);
        f = spinlock_acquire_irqsave(&t->lock);
        if (t->state != TCP_ESTABLISHED) { spinlock_release_irqrestore(&t->lock, f); return; }
    }

    if (t->state == TCP_CLOSED || t->state == TCP_LISTEN) {
        spinlock_release_irqrestore(&t->lock, f);
        return;
    }

    if (flags & TH_ACK) {
        uint32_t new_wnd = (uint32_t)win << t->snd_wscale;
        uint32_t old_wnd = t->snd_wnd;
        if ((int32_t)(ack - t->snd_una) > 0 && (int32_t)(ack - t->snd_nxt) <= 0) {
            uint32_t adv = ack - t->snd_una;
            uint32_t data_acked = adv;
            if (t->fin_sent && ack == t->snd_nxt) data_acked = adv - 1;
            if (data_acked > t->snd_buflen) data_acked = t->snd_buflen;
            if (data_acked) {
                t->snd_head = (t->snd_head + data_acked) % t->snd_cap;
                t->snd_buflen -= data_acked;
            }

            if (t->rtt_timing && (int32_t)(ack - t->rtt_seq) > 0) {
                tcp_rtt_update(t, (int32_t)(now_ms() - t->rtt_start));
                t->rtt_timing = 0;
            }

            if (t->cwnd < t->ssthresh) {
                t->cwnd += adv < t->smss ? adv : t->smss;
            } else {
                uint32_t inc = (t->smss * t->smss) / (t->cwnd ? t->cwnd : t->smss);
                t->cwnd += inc ? inc : 1;
            }
            if (t->cwnd > TCP_CWND_MAX) t->cwnd = TCP_CWND_MAX;
            snd_autotune(t);

            t->dupacks = 0;
            t->snd_una = ack;
            t->snd_wnd = new_wnd;
            t->retries = 0;
            t->rexmit_ms = (t->snd_una == t->snd_nxt) ? 0 : (now_ms() + t->rto_ms);
            tcb_wake(t);
        } else if (ack == t->snd_una) {
            int outstanding = t->snd_nxt != t->snd_una;
            if (new_wnd != old_wnd) {
                t->snd_wnd = new_wnd;
                t->dupacks = 0;
                tcb_wake(t);
            } else if (dlen == 0 && outstanding && new_wnd != 0 && !(flags & (TH_SYN | TH_FIN))) {
                if (++t->dupacks == 3) {
                    tcp_loss(t);
                    t->cwnd = t->ssthresh + 3 * t->smss;
                    t->snd_nxt = t->snd_una;
                    t->fin_sent = 0;
                    t->rtt_timing = 0;
                    t->rexmit_ms = now_ms() + t->rto_ms;
                }
            }
            if (new_wnd == 0 && outstanding) t->retries = 0;
        } else if ((int32_t)(ack - t->snd_nxt) > 0) {
            spinlock_release_irqrestore(&t->lock, f);
            tcp_output(t, t->snd_nxt, TH_ACK);
            return;
        }
        if (t->fin_sent && t->snd_una == t->snd_nxt) {
            if (t->state == TCP_FIN_WAIT1) t->state = TCP_FIN_WAIT2;
            else if (t->state == TCP_LAST_ACK) { t->state = TCP_CLOSED; t->rexmit_ms = 0; tcb_wake(t); }
        }
    }

    int data_ok = (t->state == TCP_ESTABLISHED || t->state == TCP_FIN_WAIT1 ||
                   t->state == TCP_FIN_WAIT2);

    if (dlen > 0 && data_ok && t->app_closed && t->state != TCP_ESTABLISHED) {
        uint32_t rseq = t->snd_nxt;
        t->state = TCP_CLOSED;
        t->reset = 1;
        t->rexmit_ms = 0;
        tcb_wake(t);
        spinlock_release_irqrestore(&t->lock, f);
        tcp_output(t, rseq, TH_RST | TH_ACK);
        return;
    }

    if (dlen > 0 && data_ok) {
        int32_t off = (int32_t)(t->rcv_nxt - seq);
        if (off >= (int32_t)dlen) {
            spinlock_release_irqrestore(&t->lock, f);
            tcp_output(t, t->snd_nxt, TH_ACK);
            return;
        }
        if (off >= 0) {
            rcv_push(t, data + off, dlen - (uint32_t)off);
            ooo_drain(t);
            tcb_wake(t);
        } else {
            ooo_insert(t, seq, data, dlen);
            spinlock_release_irqrestore(&t->lock, f);
            tcp_output(t, t->snd_nxt, TH_ACK);
            return;
        }
    }

    if ((flags & TH_FIN) && data_ok && seq + dlen == t->rcv_nxt) {
        t->rcv_nxt++;
        t->fin_seen = 1;
        if (t->state == TCP_ESTABLISHED) {
            t->state = TCP_CLOSE_WAIT;
        } else {
            t->state = TCP_TIME_WAIT;
            t->rexmit_ms = 0;
            t->close_ms = now_ms();
        }
        tcb_wake(t);
        spinlock_release_irqrestore(&t->lock, f);
        tcp_output(t, t->snd_nxt, TH_ACK);
        tcp_try_send(t);
        return;
    }

    spinlock_release_irqrestore(&t->lock, f);
    if (dlen > 0) tcp_output(t, t->snd_nxt, TH_ACK);
    tcp_try_send(t);
}

static int tcp_csum_ok(uint32_t src, uint32_t dst, const uint8_t *seg, size_t len) {
    if (len < 20) return 0;
    if (rd16be(seg + 16) == 0) return 1;

    uint32_t sum = 0;
    sum += (src >> 16) & 0xffff; sum += src & 0xffff;
    sum += (dst >> 16) & 0xffff; sum += dst & 0xffff;
    sum += 6;
    sum += (uint32_t)len & 0xffff;
    for (size_t i = 0; i + 1 < len; i += 2) sum += (uint32_t)((seg[i] << 8) | seg[i + 1]);
    if (len & 1) sum += (uint32_t)(seg[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum == 0;
}

static uint32_t seg_payload(const uint8_t *seg, size_t len) {
    uint32_t hlen = (uint32_t)((seg[12] >> 4) * 4u);
    return (hlen <= len) ? (uint32_t)(len - hlen) : 0u;
}

void tcp_rx(netdev_t *dev, uint32_t src_ip, uint32_t dst_ip, const uint8_t *seg, size_t len) {
    if (len < 20) return;
    if (!tcp_csum_ok(src_ip, dst_ip, seg, len)) {
        if (dev) dev->rx_bad_csum++;
        return;
    }
    if (dev && (seg[13] & TH_SYN) && !(seg[13] & TH_ACK)) dev->rx_tcp_syn++;
    uint16_t sport = rd16be(seg + 0);
    uint16_t dport = rd16be(seg + 2);
    uint8_t  flags = seg[13];
    uint64_t g = spinlock_acquire_irqsave(&g_tcbs_lock);
    tcp_tcb_t *t = tcb_hold(tcb_find(src_ip, sport, dport));
    tcp_tcb_t *lst = t ? NULL : tcb_hold(find_listener(dport, AF_INET));
    spinlock_release_irqrestore(&g_tcbs_lock, g);
    if (t) { tcp_input(t, seg, len); tcb_drop(t); return; }

    uint32_t doff = (uint32_t)(seg[12] >> 4) * 4;
    if (lst && (flags & TH_SYN) && !(flags & (TH_ACK | TH_RST)) && doff >= 20 && doff <= len) {
        tcp_child(lst, dev, AF_INET, src_ip, dst_ip, NULL, NULL, sport, seg, doff);
        tcb_drop(lst);
        return;
    }
    tcb_drop(lst);
    if (lst && !(flags & TH_ACK)) return;
    tcp_reject(dev, src_ip, NULL, NULL, sport, dport, rd32be(seg + 4), rd32be(seg + 8), flags,
               seg_payload(seg, len));
}

void tcp6_rx(netdev_t *dev, const uint8_t *src6, const uint8_t *dst6, const uint8_t *seg, size_t len) {
    if (len < 20) return;
    if (rd16be(seg + 16) != 0 && tcp6_csum(src6, dst6, seg, len) != 0) {
        if (dev) dev->rx_bad_csum++;
        return;
    }
    uint16_t sport = rd16be(seg + 0);
    uint16_t dport = rd16be(seg + 2);
    uint8_t  flags = seg[13];
    uint64_t g = spinlock_acquire_irqsave(&g_tcbs_lock);
    tcp_tcb_t *t = tcb_hold(tcb_find6(src6, sport, dport));
    tcp_tcb_t *lst = t ? NULL : tcb_hold(find_listener(dport, AF_INET6));
    spinlock_release_irqrestore(&g_tcbs_lock, g);
    if (t) { tcp_input(t, seg, len); tcb_drop(t); return; }

    uint32_t doff = (uint32_t)(seg[12] >> 4) * 4;
    if (lst && (flags & TH_SYN) && !(flags & (TH_ACK | TH_RST)) && doff >= 20 && doff <= len) {
        tcp_child(lst, dev, AF_INET6, 0, 0, src6, dst6, sport, seg, doff);
        tcb_drop(lst);
        return;
    }
    tcb_drop(lst);
    if (lst && !(flags & TH_ACK)) return;
    tcp_reject(dev, 0, src6, dst6, sport, dport, rd32be(seg + 4), rd32be(seg + 8), flags,
               seg_payload(seg, len));
}

void tcp_tick(void) {
    uint64_t nowt = now_ms();
    tcp_tcb_t *dead = NULL;
    uint64_t g = spinlock_acquire_irqsave(&g_tcbs_lock);
    tcp_tcb_t **pp = &g_tcbs;
    while (*pp) {
        tcp_tcb_t *t = *pp;
        uint64_t f = spinlock_acquire_irqsave(&t->lock);

        if (t->state == TCP_TIME_WAIT && nowt - t->close_ms > TCP_TIMEWAIT_MS) {
            t->state = TCP_CLOSED;
            tcb_wake(t);
        }
        if (t->app_closed && t->state == TCP_FIN_WAIT2 && nowt - t->close_ms > TCP_FIN_TIMEOUT) {
            t->state = TCP_CLOSED;
            t->rexmit_ms = 0;
        }

        int idle = !tcb_has_waiters(t) && __atomic_load_n(&t->refs, __ATOMIC_ACQUIRE) == 0;
        int reap = t->app_closed && (t->reset || t->state == TCP_CLOSED) && idle;
        if (t->is_listener) reap = t->app_closed && idle;

        if (!reap && t->rexmit_ms && nowt >= t->rexmit_ms &&
            t->state != TCP_CLOSED && t->state != TCP_TIME_WAIT && !t->is_listener) {
            int limit = t->pend_syn ? TCP_SYN_RETRIES : TCP_RETRIES2;
            if (++t->retries > limit) {
                if (!t->err && !t->app_closed) t->err = ETIMEDOUT;
                t->reset = 1;
                t->state = TCP_CLOSED;
                t->rexmit_ms = 0;
                tcb_wake(t);
                spinlock_release_irqrestore(&t->lock, f);
            } else {
                t->rtt_timing = 0;
                uint32_t back = t->rto_ms << (t->retries > 10 ? 10 : t->retries);
                if (back > TCP_RTO_MAX) back = TCP_RTO_MAX;
                t->rexmit_ms = nowt + back;
                if (t->pend_syn) {
                    int syn_ack = (t->state == TCP_SYN_RCVD);
                    uint32_t seq = t->iss;
                    spinlock_release_irqrestore(&t->lock, f);
                    tcp_output(t, seq, syn_ack ? (TH_SYN | TH_ACK) : TH_SYN);
                } else {
                    tcp_loss(t);
                    t->snd_nxt = t->snd_una;
                    t->fin_sent = 0;
                    spinlock_release_irqrestore(&t->lock, f);
                    tcp_try_send(t);
                }
            }
        } else if (!reap && t->ka_on && !t->is_listener && t->rexmit_ms == 0 &&
                   (t->state == TCP_ESTABLISHED || t->state == TCP_CLOSE_WAIT) &&
                   t->snd_una == t->snd_nxt &&
                   nowt - t->last_rx_ms >= (uint64_t)t->ka_idle * 1000ull +
                                           (uint64_t)t->ka_probes * t->ka_intvl * 1000ull) {
            if (t->ka_probes >= t->ka_cnt) {
                if (!t->err && !t->app_closed) t->err = ETIMEDOUT;
                t->reset = 1;
                t->state = TCP_CLOSED;
                tcb_wake(t);
                spinlock_release_irqrestore(&t->lock, f);
            } else {
                t->ka_probes++;
                uint32_t seq = t->snd_una - 1;
                spinlock_release_irqrestore(&t->lock, f);
                tcp_output(t, seq, TH_ACK);
            }
        } else {
            spinlock_release_irqrestore(&t->lock, f);
        }

        if (reap) {
            *pp = t->next;
            t->next = dead;
            dead = t;
            continue;
        }
        pp = &(*pp)->next;
    }
    spinlock_release_irqrestore(&g_tcbs_lock, g);

    while (dead) {
        tcp_tcb_t *n = dead->next;
        tcb_free(dead);
        dead = n;
    }
}
