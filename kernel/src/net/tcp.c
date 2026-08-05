#include "../../include/net/tcp.h"
#include "../../include/net/ip.h"
#include "../../include/net/ipv6.h"
#include "../../include/net/socket.h"
#include "../../include/net/net.h"
#include "../../include/net/netdev.h"
#include "../../include/sched/sched.h"
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

#define TCP_MSS     1400
#define TCP_SNDBUF  16384
#define TCP_RCVBUF  65536

struct tcp_tcb {
    int       state;
    int       family;
    uint8_t   laddr6[16], raddr6[16];
    uint32_t  local_ip, remote_ip;
    uint16_t  local_port, remote_port;

    uint32_t  iss, snd_una, snd_nxt;
    uint32_t  rcv_nxt;
    uint16_t  snd_wnd;

    uint8_t   sndbuf[TCP_SNDBUF];
    uint32_t  snd_buflen;
    int       pend_syn, pend_fin, fin_sent;

    uint8_t  *rcvbuf;
    uint32_t  rcv_head, rcv_tail, rcv_count;
    int       fin_seen, reset, app_closed;

    uint64_t  rexmit_ms;
    int       retries;
    uint64_t  close_ms;

    int       is_listener;
    struct tcp_tcb *listener;
    struct tcp_tcb *accept_q;
    struct tcp_tcb *accept_next;

    spinlock_t lock;
    task_t    *waiter;
    struct tcp_tcb *next;
};

static tcp_tcb_t  *g_tcbs;
static spinlock_t  g_tcbs_lock;
static uint16_t    g_eport = 40000;

static uint64_t now_ms(void) { return sched_now_ns() / 1000000ull; }
static netdev_t *tdev(void) { return netdev_first(); }

static void tcb_wake(tcp_tcb_t *t) {
    task_t *w = t->waiter;
    t->waiter = NULL;
    if (w) task_unblock(w);
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

static void tcp_output(tcp_tcb_t *t, uint32_t seq, uint8_t flags,
                       const uint8_t *data, uint32_t dlen) {
    netdev_t *dev = tdev();
    if (!dev) return;
    uint8_t seg[20 + TCP_MSS];
    if (dlen > TCP_MSS) dlen = TCP_MSS;

    wr16be(seg + 0, t->local_port);
    wr16be(seg + 2, t->remote_port);
    wr32be(seg + 4, seq);
    wr32be(seg + 8, t->rcv_nxt);
    seg[12] = 5 << 4;
    seg[13] = flags;
    uint32_t win = (t->rcvbuf) ? (TCP_RCVBUF - t->rcv_count) : 8192;
    if (win > 65535) win = 65535;
    wr16be(seg + 14, (uint16_t)win);
    wr16be(seg + 16, 0);
    wr16be(seg + 18, 0);
    if (dlen && data) memcpy(seg + 20, data, dlen);

    if (t->family == AF_INET6) {
        wr16be(seg + 16, tcp6_csum(t->laddr6, t->raddr6, seg, 20 + dlen));
        ipv6_output(dev, t->laddr6, t->raddr6, IPPROTO_TCP, seg, 20 + dlen);
        return;
    }
    wr16be(seg + 16, tcp_csum(t->local_ip, t->remote_ip, seg, 20 + dlen));
    ip_send(dev, t->remote_ip, IPPROTO_TCP, seg, 20 + dlen);
}

static tcp_tcb_t *tcb_find(uint32_t rip, uint16_t rport, uint16_t lport) {
    for (tcp_tcb_t *t = g_tcbs; t; t = t->next)
        if (t->family != AF_INET6 && t->local_port == lport && t->remote_port == rport && t->remote_ip == rip)
            return t;
    return NULL;
}

static tcp_tcb_t *tcb_find6(const uint8_t *rip6, uint16_t rport, uint16_t lport) {
    for (tcp_tcb_t *t = g_tcbs; t; t = t->next)
        if (t->family == AF_INET6 && !t->is_listener && t->local_port == lport && t->remote_port == rport && memcmp(t->raddr6, rip6, 16) == 0)
            return t;
    return NULL;
}

int tcp_connect_start(uint32_t ip, uint16_t port, tcp_tcb_t **out) {
    netdev_t *dev = tdev();
    if (!dev || !dev->ip) return -EINVAL;

    tcp_tcb_t *t = calloc(1, sizeof(*t));
    if (!t) return -ENOMEM;
    t->rcvbuf = malloc(TCP_RCVBUF);
    if (!t->rcvbuf) { free(t); return -ENOMEM; }

    t->local_ip = dev->ip;
    t->remote_ip = ip;
    t->remote_port = port;
    t->local_port = g_eport++;
    if (g_eport == 0) g_eport = 40000;
    t->iss = (uint32_t)(now_ms() * 2654435761u) ^ 0xa5a5;
    t->snd_una = t->iss;
    t->snd_nxt = t->iss + 1;
    t->state = TCP_SYN_SENT;
    t->pend_syn = 1;
    t->rexmit_ms = now_ms() + 1000;

    spinlock_acquire(&g_tcbs_lock);
    t->next = g_tcbs;
    g_tcbs = t;
    spinlock_release(&g_tcbs_lock);

    tcp_output(t, t->iss, TH_SYN, NULL, 0);
    *out = t;
    return 0;
}

int tcp_connect_status(tcp_tcb_t *t) {
    if (!t) return -EINVAL;
    uint64_t f = spinlock_acquire_irqsave(&t->lock);
    int st = t->state, rst = t->reset;
    spinlock_release_irqrestore(&t->lock, f);
    if (rst || st == TCP_CLOSED) return -ECONNREFUSED;
    if (st == TCP_ESTABLISHED)   return 0;
    return -EINPROGRESS;
}

int tcp_connect(uint32_t ip, uint16_t port, tcp_tcb_t **out) {
    tcp_tcb_t *t = NULL;
    int r = tcp_connect_start(ip, port, &t);
    if (r != 0) { *out = NULL; return r; }

    task_t *me = syscall_cur_task();
    uint64_t deadline = now_ms() + 5000;
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&t->lock);
        if (t->state == TCP_ESTABLISHED) { spinlock_release_irqrestore(&t->lock, f); *out = t; return 0; }
        if (t->reset || now_ms() > deadline) { spinlock_release_irqrestore(&t->lock, f); break; }
        if (me && me->pending_kill) { spinlock_release_irqrestore(&t->lock, f); break; }
        if (me) { t->waiter = me; me->runnable = false; me->state = TASK_BLOCKED; }
        spinlock_release_irqrestore(&t->lock, f);
        if (me) sched_reschedule(); else task_yield();
    }
    *out = t;
    return -EINVAL;
}

static void tcp_try_send(tcp_tcb_t *t) {
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&t->lock);
        int st = t->state;
        if (st != TCP_ESTABLISHED && st != TCP_CLOSE_WAIT &&
            st != TCP_FIN_WAIT1 && st != TCP_LAST_ACK) {
            spinlock_release_irqrestore(&t->lock, f);
            return;
        }
        uint32_t inflight = t->snd_nxt - t->snd_una;
        uint32_t unsent = (t->snd_buflen > inflight) ? (t->snd_buflen - inflight) : 0;
        uint32_t wnd = t->snd_wnd ? t->snd_wnd : 1;
        uint32_t canwnd = (inflight < wnd) ? (wnd - inflight) : 0;
        uint32_t n = unsent;
        if (n > canwnd) n = canwnd;
        if (n > TCP_MSS) n = TCP_MSS;

        if (n > 0) {
            uint8_t seg[TCP_MSS];
            memcpy(seg, t->sndbuf + inflight, n);
            uint32_t seq = t->snd_nxt;
            t->snd_nxt += n;
            if (t->rexmit_ms == 0) t->rexmit_ms = now_ms() + 1000;
            spinlock_release_irqrestore(&t->lock, f);
            tcp_output(t, seq, TH_ACK | TH_PSH, seg, n);
            continue;
        }

        if (t->pend_fin && !t->fin_sent && t->snd_buflen == 0 && t->snd_una == t->snd_nxt) {
            t->fin_sent = 1;
            uint32_t seq = t->snd_nxt;
            t->snd_nxt += 1;
            if (t->rexmit_ms == 0) t->rexmit_ms = now_ms() + 1000;
            spinlock_release_irqrestore(&t->lock, f);
            tcp_output(t, seq, TH_ACK | TH_FIN, NULL, 0);
            return;
        }

        spinlock_release_irqrestore(&t->lock, f);
        return;
    }
}

int64_t tcp_send(tcp_tcb_t *t, const void *buf, size_t len) {
    if (!t) return -EINVAL;
    task_t *me = syscall_cur_task();
    const uint8_t *p = buf;
    size_t done = 0;

    while (done < len) {
        uint64_t f = spinlock_acquire_irqsave(&t->lock);
        if (t->reset || t->state == TCP_CLOSED) { spinlock_release_irqrestore(&t->lock, f); return done ? (int64_t)done : -EINVAL; }
        if (t->state != TCP_ESTABLISHED && t->state != TCP_CLOSE_WAIT) {
            spinlock_release_irqrestore(&t->lock, f);
            return done ? (int64_t)done : -EINVAL;
        }
        uint32_t space = TCP_SNDBUF - t->snd_buflen;
        if (space == 0) {
            if (me && me->pending_kill) { spinlock_release_irqrestore(&t->lock, f); return done ? (int64_t)done : -EINTR; }
            if (me) { t->waiter = me; me->runnable = false; me->state = TASK_BLOCKED; }
            spinlock_release_irqrestore(&t->lock, f);
            if (me) sched_reschedule(); else task_yield();
            continue;
        }
        uint32_t n = (uint32_t)(len - done);
        if (n > space) n = space;
        memcpy(t->sndbuf + t->snd_buflen, p + done, n);
        t->snd_buflen += n;
        done += n;
        spinlock_release_irqrestore(&t->lock, f);
        tcp_try_send(t);
    }
    return (int64_t)done;
}

int64_t tcp_recv(tcp_tcb_t *t, void *buf, size_t len, int nonblock) {
    if (!t) return -EINVAL;
    task_t *me = syscall_cur_task();
    uint8_t *dst = buf;

    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&t->lock);
        if (t->rcv_count > 0) {
            uint32_t n = 0;
            while (n < len && t->rcv_count > 0) {
                dst[n++] = t->rcvbuf[t->rcv_head];
                t->rcv_head = (t->rcv_head + 1) % TCP_RCVBUF;
                t->rcv_count--;
            }
            spinlock_release_irqrestore(&t->lock, f);
            return (int64_t)n;
        }
        if (t->reset) { spinlock_release_irqrestore(&t->lock, f); return -EINVAL; }
        if (t->fin_seen) { spinlock_release_irqrestore(&t->lock, f); return 0; }
        if (nonblock) { spinlock_release_irqrestore(&t->lock, f); return -EAGAIN; }
        if (me && me->pending_kill) { spinlock_release_irqrestore(&t->lock, f); return -EINTR; }
        if (me) { t->waiter = me; me->runnable = false; me->state = TASK_BLOCKED; }
        spinlock_release_irqrestore(&t->lock, f);
        if (me) sched_reschedule(); else task_yield();
    }
}

void tcp_close(tcp_tcb_t *t) {
    if (!t) return;
    uint64_t f = spinlock_acquire_irqsave(&t->lock);
    t->app_closed = 1;
    t->close_ms = now_ms();
    if (t->is_listener) {
        t->state = TCP_CLOSED;
        spinlock_release_irqrestore(&t->lock, f);
        return;
    }
    if (t->state == TCP_ESTABLISHED) {
        t->state = TCP_FIN_WAIT1;
        t->pend_fin = 1;
        spinlock_release_irqrestore(&t->lock, f);
        tcp_try_send(t);
        return;
    }
    if (t->state == TCP_CLOSE_WAIT) {
        t->state = TCP_LAST_ACK;
        t->pend_fin = 1;
        spinlock_release_irqrestore(&t->lock, f);
        tcp_try_send(t);
        return;
    }
    spinlock_release_irqrestore(&t->lock, f);
}

static tcp_tcb_t *find_listener(uint16_t port) {
    for (tcp_tcb_t *t = g_tcbs; t; t = t->next)
        if (t->is_listener && t->local_port == port && t->family != AF_INET6) return t;
    return NULL;
}

static tcp_tcb_t *find_listener6(uint16_t port) {
    for (tcp_tcb_t *t = g_tcbs; t; t = t->next)
        if (t->is_listener && t->local_port == port && t->family == AF_INET6) return t;
    return NULL;
}

tcp_tcb_t *tcp_listen(uint16_t port) {
    tcp_tcb_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    netdev_t *dev = tdev();
    t->local_ip = dev ? dev->ip : 0;
    t->local_port = port;
    t->state = TCP_LISTEN;
    t->is_listener = 1;
    spinlock_acquire(&g_tcbs_lock);
    t->next = g_tcbs;
    g_tcbs = t;
    spinlock_release(&g_tcbs_lock);
    return t;
}

tcp_tcb_t *tcp_accept(tcp_tcb_t *lst, int nonblock, uint32_t *rip, uint16_t *rport) {
    task_t *me = syscall_cur_task();
    for (;;) {
        spinlock_acquire(&g_tcbs_lock);
        if (lst->accept_q) {
            tcp_tcb_t *c = lst->accept_q;
            lst->accept_q = c->accept_next;
            c->accept_next = NULL;
            spinlock_release(&g_tcbs_lock);
            if (rip)   *rip = c->remote_ip;
            if (rport) *rport = c->remote_port;
            return c;
        }
        if (nonblock) { spinlock_release(&g_tcbs_lock); return NULL; }
        if (me && me->pending_kill) { spinlock_release(&g_tcbs_lock); return NULL; }
        if (me) { lst->waiter = me; me->runnable = false; me->state = TASK_BLOCKED; }
        spinlock_release(&g_tcbs_lock);
        if (me) sched_reschedule(); else task_yield();
    }
}

int tcp_poll(tcp_tcb_t *t) {
    int r = 0;
    uint64_t f = spinlock_acquire_irqsave(&t->lock);
    int st = t->state;
    if (t->rcv_count > 0 || t->accept_q) r |= POLLIN;
    if (t->fin_seen || t->reset || st == TCP_CLOSE_WAIT || st == TCP_CLOSED || st == TCP_LAST_ACK)
        r |= POLLIN;
    if (t->reset || st == TCP_CLOSED) r |= POLLHUP;
    if (st == TCP_ESTABLISHED && t->snd_buflen < TCP_SNDBUF) r |= POLLOUT;
    spinlock_release_irqrestore(&t->lock, f);
    return r;
}

static void tcp_accept_syn(tcp_tcb_t *lst, uint32_t src, uint16_t sport, uint32_t seq) {
    netdev_t *dev = tdev();
    if (!dev) return;
    tcp_tcb_t *c = calloc(1, sizeof(*c));
    if (!c) return;
    c->rcvbuf = malloc(TCP_RCVBUF);
    if (!c->rcvbuf) { free(c); return; }
    c->local_ip = dev->ip;
    c->local_port = lst->local_port;
    c->remote_ip = src;
    c->remote_port = sport;
    c->rcv_nxt = seq + 1;
    c->iss = (uint32_t)(now_ms() * 2654435761u) ^ 0x5a5a;
    c->snd_una = c->iss;
    c->snd_nxt = c->iss + 1;
    c->state = TCP_SYN_RCVD;
    c->pend_syn = 1;
    c->listener = lst;
    c->rexmit_ms = now_ms() + 1000;
    spinlock_acquire(&g_tcbs_lock);
    c->next = g_tcbs;
    g_tcbs = c;
    spinlock_release(&g_tcbs_lock);
    tcp_output(c, c->iss, TH_SYN | TH_ACK, NULL, 0);
}

static void tcp_input(tcp_tcb_t *t, const uint8_t *seg, size_t len) {
    uint32_t seq   = rd32be(seg + 4);
    uint32_t ack   = rd32be(seg + 8);
    uint8_t  doff  = (seg[12] >> 4) * 4;
    uint8_t  flags = seg[13];
    uint16_t win   = rd16be(seg + 14);
    if (doff < 20 || doff > len) return;
    const uint8_t *data = seg + doff;
    uint32_t dlen = (uint32_t)(len - doff);

    uint64_t f = spinlock_acquire_irqsave(&t->lock);

    if (flags & TH_RST) {
        t->reset = 1;
        t->state = TCP_CLOSED;
        tcb_wake(t);
        spinlock_release_irqrestore(&t->lock, f);
        return;
    }

    t->snd_wnd = win;

    if (t->state == TCP_SYN_SENT) {
        if ((flags & TH_SYN) && (flags & TH_ACK) && ack == t->iss + 1) {
            t->rcv_nxt = seq + 1;
            t->snd_una = ack;
            t->pend_syn = 0;
            t->state = TCP_ESTABLISHED;
            spinlock_release_irqrestore(&t->lock, f);
            tcp_output(t, t->snd_nxt, TH_ACK, NULL, 0);
            f = spinlock_acquire_irqsave(&t->lock);
            tcb_wake(t);
        }
        spinlock_release_irqrestore(&t->lock, f);
        return;
    }

    if (t->state == TCP_SYN_RCVD) {
        if ((flags & TH_ACK) && ack == t->iss + 1) {
            t->snd_una = ack;
            t->pend_syn = 0;
            t->rexmit_ms = 0;
            t->state = TCP_ESTABLISHED;
            tcp_tcb_t *listener = t->listener;
            spinlock_release_irqrestore(&t->lock, f);
            if (listener) {
                spinlock_acquire(&g_tcbs_lock);
                t->accept_next = listener->accept_q;
                listener->accept_q = t;
                task_t *w = listener->waiter;
                listener->waiter = NULL;
                spinlock_release(&g_tcbs_lock);
                if (w) task_unblock(w);
            }
            return;
        }
        spinlock_release_irqrestore(&t->lock, f);
        return;
    }

    if (flags & TH_ACK) {
        if ((int32_t)(ack - t->snd_una) > 0 && (int32_t)(ack - t->snd_nxt) <= 0) {
            uint32_t adv = ack - t->snd_una;
            uint32_t data_acked = adv;
            if (t->fin_sent && ack == t->snd_nxt && adv > 0) data_acked = adv - 1;
            if (data_acked > t->snd_buflen) data_acked = t->snd_buflen;
            if (data_acked) {
                memmove(t->sndbuf, t->sndbuf + data_acked, t->snd_buflen - data_acked);
                t->snd_buflen -= data_acked;
            }
            t->snd_una = ack;
            t->retries = 0;
            t->rexmit_ms = (t->snd_una == t->snd_nxt) ? 0 : (now_ms() + 1000);
            tcb_wake(t);
        }
        if (t->state == TCP_FIN_WAIT1 && t->snd_una == t->snd_nxt) t->state = TCP_FIN_WAIT2;
        else if (t->state == TCP_LAST_ACK && t->snd_una == t->snd_nxt) { t->state = TCP_CLOSED; }
    }

    if (dlen > 0 && seq == t->rcv_nxt &&
        (t->state == TCP_ESTABLISHED || t->state == TCP_FIN_WAIT1 || t->state == TCP_FIN_WAIT2)) {
        uint32_t space = TCP_RCVBUF - t->rcv_count;
        uint32_t n = dlen < space ? dlen : space;
        for (uint32_t i = 0; i < n; i++) {
            t->rcvbuf[t->rcv_tail] = data[i];
            t->rcv_tail = (t->rcv_tail + 1) % TCP_RCVBUF;
        }
        t->rcv_count += n;
        t->rcv_nxt += n;
        tcb_wake(t);
        spinlock_release_irqrestore(&t->lock, f);
        tcp_output(t, t->snd_nxt, TH_ACK, NULL, 0);
        f = spinlock_acquire_irqsave(&t->lock);
    } else if (dlen > 0 && seq != t->rcv_nxt) {
        spinlock_release_irqrestore(&t->lock, f);
        tcp_output(t, t->snd_nxt, TH_ACK, NULL, 0);
        return;
    }

    if ((flags & TH_FIN) && seq + dlen == t->rcv_nxt) {
        t->rcv_nxt++;
        t->fin_seen = 1;
        if (t->state == TCP_ESTABLISHED) t->state = TCP_CLOSE_WAIT;
        else if (t->state == TCP_FIN_WAIT1 || t->state == TCP_FIN_WAIT2) { t->state = TCP_TIME_WAIT; t->close_ms = now_ms(); }
        tcb_wake(t);
        spinlock_release_irqrestore(&t->lock, f);
        tcp_output(t, t->snd_nxt, TH_ACK, NULL, 0);
        return;
    }

    spinlock_release_irqrestore(&t->lock, f);
    tcp_try_send(t);
}

static const uint8_t tcp_lo6[16] = { 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,1 };

int tcp_connect6(const uint8_t dst6[16], uint16_t port, tcp_tcb_t **out) {
    netdev_t *dev = tdev();
    if (!dev) return -EINVAL;

    tcp_tcb_t *t = calloc(1, sizeof(*t));
    if (!t) return -ENOMEM;
    t->rcvbuf = malloc(TCP_RCVBUF);
    if (!t->rcvbuf) { free(t); return -ENOMEM; }

    t->family = AF_INET6;
    memcpy(t->raddr6, dst6, 16);
    if (memcmp(dst6, tcp_lo6, 16) == 0) memcpy(t->laddr6, tcp_lo6, 16);
    else ipv6_get_lladdr(dev, t->laddr6);
    t->remote_port = port;
    t->local_port = g_eport++;
    if (g_eport == 0) g_eport = 40000;
    t->iss = (uint32_t)(now_ms() * 2654435761u) ^ 0xa5a5;
    t->snd_una = t->iss;
    t->snd_nxt = t->iss + 1;
    t->state = TCP_SYN_SENT;
    t->pend_syn = 1;
    t->rexmit_ms = now_ms() + 1000;

    spinlock_acquire(&g_tcbs_lock);
    t->next = g_tcbs;
    g_tcbs = t;
    spinlock_release(&g_tcbs_lock);

    tcp_output(t, t->iss, TH_SYN, NULL, 0);

    task_t *me = syscall_cur_task();
    uint64_t deadline = now_ms() + 5000;
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&t->lock);
        if (t->state == TCP_ESTABLISHED) { spinlock_release_irqrestore(&t->lock, f); *out = t; return 0; }
        if (t->reset || now_ms() > deadline) { spinlock_release_irqrestore(&t->lock, f); break; }
        if (me && me->pending_kill) { spinlock_release_irqrestore(&t->lock, f); break; }
        if (me) { t->waiter = me; me->runnable = false; me->state = TASK_BLOCKED; }
        spinlock_release_irqrestore(&t->lock, f);
        if (me) sched_reschedule(); else task_yield();
    }
    *out = NULL;
    return -EINVAL;
}

tcp_tcb_t *tcp_listen6(uint16_t port) {
    tcp_tcb_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->family = AF_INET6;
    t->local_port = port;
    t->state = TCP_LISTEN;
    t->is_listener = 1;
    spinlock_acquire(&g_tcbs_lock);
    t->next = g_tcbs;
    g_tcbs = t;
    spinlock_release(&g_tcbs_lock);
    return t;
}

tcp_tcb_t *tcp_accept6(tcp_tcb_t *lst, int nonblock, uint8_t rip6[16], uint16_t *rport) {
    task_t *me = syscall_cur_task();
    for (;;) {
        spinlock_acquire(&g_tcbs_lock);
        if (lst->accept_q) {
            tcp_tcb_t *c = lst->accept_q;
            lst->accept_q = c->accept_next;
            c->accept_next = NULL;
            spinlock_release(&g_tcbs_lock);
            if (rip6)  memcpy(rip6, c->raddr6, 16);
            if (rport) *rport = c->remote_port;
            return c;
        }
        if (nonblock) { spinlock_release(&g_tcbs_lock); return NULL; }
        if (me && me->pending_kill) { spinlock_release(&g_tcbs_lock); return NULL; }
        if (me) { lst->waiter = me; me->runnable = false; me->state = TASK_BLOCKED; }
        spinlock_release(&g_tcbs_lock);
        if (me) sched_reschedule(); else task_yield();
    }
}

static void tcp_accept_syn6(tcp_tcb_t *lst, const uint8_t *src6, const uint8_t *dst6, uint16_t sport, uint32_t seq) {
    tcp_tcb_t *c = calloc(1, sizeof(*c));
    if (!c) return;
    c->rcvbuf = malloc(TCP_RCVBUF);
    if (!c->rcvbuf) { free(c); return; }
    c->family = AF_INET6;
    memcpy(c->laddr6, dst6, 16);
    memcpy(c->raddr6, src6, 16);
    c->local_port = lst->local_port;
    c->remote_port = sport;
    c->rcv_nxt = seq + 1;
    c->iss = (uint32_t)(now_ms() * 2654435761u) ^ 0x5a5a;
    c->snd_una = c->iss;
    c->snd_nxt = c->iss + 1;
    c->state = TCP_SYN_RCVD;
    c->pend_syn = 1;
    c->listener = lst;
    c->rexmit_ms = now_ms() + 1000;
    spinlock_acquire(&g_tcbs_lock);
    c->next = g_tcbs;
    g_tcbs = c;
    spinlock_release(&g_tcbs_lock);
    tcp_output(c, c->iss, TH_SYN | TH_ACK, NULL, 0);
}

void tcp_rx(netdev_t *dev, uint32_t src_ip, uint32_t dst_ip, const uint8_t *seg, size_t len) {
    (void)dev; (void)dst_ip;
    if (len < 20) return;
    uint16_t sport = rd16be(seg + 0);
    uint16_t dport = rd16be(seg + 2);
    uint32_t seq   = rd32be(seg + 4);
    uint8_t  flags = seg[13];
    spinlock_acquire(&g_tcbs_lock);
    tcp_tcb_t *t = tcb_find(src_ip, sport, dport);
    tcp_tcb_t *lst = t ? NULL : find_listener(dport);
    spinlock_release(&g_tcbs_lock);
    if (!t) {
        if (lst && (flags & TH_SYN) && !(flags & TH_ACK)) tcp_accept_syn(lst, src_ip, sport, seq);
        return;
    }
    tcp_input(t, seg, len);
}

void tcp6_rx(netdev_t *dev, const uint8_t *src6, const uint8_t *dst6, const uint8_t *seg, size_t len) {
    (void)dev;
    if (len < 20) return;
    uint16_t sport = rd16be(seg + 0);
    uint16_t dport = rd16be(seg + 2);
    uint32_t seq   = rd32be(seg + 4);
    uint8_t  flags = seg[13];
    spinlock_acquire(&g_tcbs_lock);
    tcp_tcb_t *t = tcb_find6(src6, sport, dport);
    tcp_tcb_t *lst = t ? NULL : find_listener6(dport);
    spinlock_release(&g_tcbs_lock);
    if (!t) {
        if (lst && (flags & TH_SYN) && !(flags & TH_ACK)) tcp_accept_syn6(lst, src6, dst6, sport, seq);
        return;
    }
    tcp_input(t, seg, len);
}

void tcp_tick(void) {
    uint64_t nowt = now_ms();
    spinlock_acquire(&g_tcbs_lock);
    tcp_tcb_t **pp = &g_tcbs;
    while (*pp) {
        tcp_tcb_t *t = *pp;
        uint64_t f = spinlock_acquire_irqsave(&t->lock);

        int reap = 0;
        if (t->reset || t->state == TCP_CLOSED) {
            if (t->app_closed) reap = 1;
            spinlock_release_irqrestore(&t->lock, f);
        } else if (t->state == TCP_TIME_WAIT) {
            if (nowt - t->close_ms > 2000) reap = 1;
            spinlock_release_irqrestore(&t->lock, f);
        } else if (t->rexmit_ms && nowt >= t->rexmit_ms) {
            if (++t->retries > 8) {
                t->reset = 1; tcb_wake(t);
                spinlock_release_irqrestore(&t->lock, f);
            } else {
                t->rexmit_ms = nowt + 1000;
                int do_syn = t->pend_syn;
                int syn_ack = (t->state == TCP_SYN_RCVD);
                uint32_t seq = t->snd_una;
                if (do_syn) {
                    spinlock_release_irqrestore(&t->lock, f);
                    tcp_output(t, seq, syn_ack ? (TH_SYN | TH_ACK) : TH_SYN, NULL, 0);
                } else {
                    t->snd_nxt = t->snd_una;
                    t->fin_sent = 0;
                    spinlock_release_irqrestore(&t->lock, f);
                    tcp_try_send(t);
                }
            }
        } else {
            spinlock_release_irqrestore(&t->lock, f);
        }

        if (reap && !t->waiter) {
            *pp = t->next;
            free(t->rcvbuf);
            free(t);
            continue;
        }
        pp = &(*pp)->next;
    }
    spinlock_release(&g_tcbs_lock);
}
