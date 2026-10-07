#include "../../include/net/socket.h"
#include "../../include/net/netdev.h"
#include "../../include/net/net.h"
#include "../../include/net/udp.h"
#include "../../include/net/ip.h"
#include "../../include/net/ipv6.h"
#include "../../include/net/tcp.h"
#include "../../include/fs/vfs.h"
#include "../../include/fs/poll.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/spinlock.h"
#include "../../include/sched/waitq.h"
#include "../../include/sched/capabilities.h"
#include "../../include/signal/signal.h"
#include "../../include/drivers/timer.h"
#include "../../include/syscall/syscall_internal.h"
#include "../../include/syscall/errno.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define UDP_RMEM_DEF   212992u
#define UDP_WMEM_DEF   212992u
#define SOCK_MEM_MAX   212992u
#define SOCK_MIN_RCV   2304u
#define SOCK_MIN_SND   4608u
#define UDP_MAX_PAYLOAD 65507u

#define FIONREAD_K 0x541B
#define TIOCOUTQ_K 0x5411
#define SIOCSTTL_K 0x5460

typedef struct dgram {
    struct dgram *next;
    uint32_t ip;
    uint8_t  src6[16];
    uint16_t port;
    uint32_t len;
    uint8_t  data[];
} dgram_t;

typedef struct sock {
    int          type;
    int          proto;
    int          family;
    uint8_t      peer_ip6[16];
    uint32_t     peer_ip;
    uint16_t     peer_port;
    uint16_t     local_port;
    int          connected;
    int          bound;

    dgram_t     *dq_head, *dq_tail;
    uint32_t     dq_bytes;
    spinlock_t   lock;
    waitq_t      wq;

    tcp_tcb_t   *tcb;

    uint8_t      ttl;
    uint8_t      tos;

    int          opt_keepalive;
    int          opt_reuseaddr;
    int          opt_reuseport;
    int          opt_broadcast;
    int          opt_dontroute;
    int          opt_nodelay;
    int          opt_v6only;
    uint32_t     sk_sndbuf;
    uint32_t     sk_rcvbuf;
    int          opt_linger_on;
    int          opt_linger_s;
    int          ka_idle, ka_intvl, ka_cnt;
    int64_t      opt_rcvtimeo_ms;
    int64_t      opt_sndtimeo_ms;
    int          last_error;
    int          rd_shutdown;
    int          wr_shutdown;
    int          listening;
    int          backlog;
    int          opt_oobinline;
    uint32_t     local_ip;
    uint8_t      local_ip6[16];

    struct sock *next;
} sock_t;

static sock_t   *g_socks;
static spinlock_t g_socks_lock;
static uint64_t   g_sock_ino = 0x20000;

static const vnode_ops_t sock_vnode_ops;

int sock_is_vnode(const vnode_t *vn) {
    return vn && vn->ops == &sock_vnode_ops;
}

static int interrupted(task_t *me) {
    return me && (me->pending_kill || signal_pending_deliverable(me));
}

static struct {
    uint64_t in_dgrams, no_ports, rcvbuf_errors, out_dgrams;
} g_ustat;

#define USTAT(f) __atomic_add_fetch(&g_ustat.f, 1, __ATOMIC_RELAXED)

static uint32_t udp_rcv_limit(const sock_t *s) {
    return s->sk_rcvbuf ? s->sk_rcvbuf : UDP_RMEM_DEF;
}

static void sock_enqueue2(sock_t *s, uint32_t ip, const uint8_t *ip6, uint16_t port, const uint8_t *data, size_t len) {
    if (len > 65535) return;
    dgram_t *d = malloc(sizeof(*d) + len);
    if (!d) return;
    d->next = NULL;
    d->ip = ip;
    d->port = port;
    d->len = (uint32_t)len;
    if (ip6) memcpy(d->src6, ip6, 16); else memset(d->src6, 0, 16);
    memcpy(d->data, data, len);

    uint64_t f = spinlock_acquire_irqsave(&s->lock);
    if (s->dq_bytes + len > udp_rcv_limit(s) && s->dq_head) {
        spinlock_release_irqrestore(&s->lock, f);
        free(d);
        if (s->type == SOCK_DGRAM) USTAT(rcvbuf_errors);
        return;
    }
    if (s->type == SOCK_DGRAM) USTAT(in_dgrams);
    if (s->dq_tail) s->dq_tail->next = d; else s->dq_head = d;
    s->dq_tail = d;
    s->dq_bytes += (uint32_t)len;
    waitq_wake_all(&s->wq);
    spinlock_release_irqrestore(&s->lock, f);
}

static int dgram_from_peer(const sock_t *s, uint32_t ip, const uint8_t *ip6, uint16_t port) {
    if (!s->connected) return 1;
    if (s->peer_port != port) return 0;
    if (ip6) return memcmp(s->peer_ip6, ip6, 16) == 0;
    return s->peer_ip == ip;
}

int sock_udp_input(uint32_t src_ip, uint16_t src_port, uint16_t dst_port,
                   const uint8_t *data, size_t len) {
    sock_t *hit = NULL;
    uint64_t g = spinlock_acquire_irqsave(&g_socks_lock);
    for (sock_t *s = g_socks; s; s = s->next) {
        if (s->type == SOCK_DGRAM && s->family == AF_INET && s->bound && s->local_port == dst_port &&
            dgram_from_peer(s, src_ip, NULL, src_port)) {
            hit = s;
            sock_enqueue2(s, src_ip, NULL, src_port, data, len);
            break;
        }
    }
    spinlock_release_irqrestore(&g_socks_lock, g);
    if (!hit) USTAT(no_ports);
    return hit != NULL;
}

int sock_udp6_input(const uint8_t *src6, uint16_t src_port, uint16_t dst_port,
                    const uint8_t *data, size_t len) {
    sock_t *hit = NULL;
    uint64_t g = spinlock_acquire_irqsave(&g_socks_lock);
    for (sock_t *s = g_socks; s; s = s->next) {
        if (s->family == AF_INET6 && s->type == SOCK_DGRAM && s->bound && s->local_port == dst_port &&
            dgram_from_peer(s, 0, src6, src_port)) {
            hit = s;
            sock_enqueue2(s, 0, src6, src_port, data, len);
            break;
        }
    }
    spinlock_release_irqrestore(&g_socks_lock, g);
    return hit != NULL;
}

void sock_icmp_input(uint32_t src_ip, const uint8_t *data, size_t len) {
    uint64_t g = spinlock_acquire_irqsave(&g_socks_lock);
    for (sock_t *s = g_socks; s; s = s->next)
        if (s->type == SOCK_RAW && s->proto == IPPROTO_ICMP)
            sock_enqueue2(s, src_ip, NULL, 0, data, len);
    spinlock_release_irqrestore(&g_socks_lock, g);
}

static int port_taken(const sock_t *self, uint32_t ip, uint16_t port, int check_tcp) {
    int taken = 0;
    uint64_t g = spinlock_acquire_irqsave(&g_socks_lock);
    for (sock_t *o = g_socks; o; o = o->next) {
        if (o == self || o->type != self->type || o->family != self->family ||
            !o->bound || o->local_port != port) continue;
        if (ip && o->local_ip && ip != o->local_ip) continue;
        if (self->opt_reuseport && o->opt_reuseport) continue;
        if (self->opt_reuseaddr && o->opt_reuseaddr && !o->listening) continue;
        taken = 1;
        break;
    }
    spinlock_release_irqrestore(&g_socks_lock, g);
    if (!taken && check_tcp && self->type == SOCK_STREAM && tcp_port_in_use(port) && !self->opt_reuseaddr)
        taken = 1;
    return taken;
}

static uint16_t g_port_cursor = 32768;
static spinlock_t g_port_lock = SPINLOCK_INIT;

static uint16_t alloc_port(const sock_t *s) {
    for (int tries = 0; tries < 28232; tries++) {
        uint64_t f = spinlock_acquire_irqsave(&g_port_lock);
        uint16_t p = g_port_cursor++;
        if (g_port_cursor > 60999) g_port_cursor = 32768;
        spinlock_release_irqrestore(&g_port_lock, f);
        if (!s || !port_taken(s, 0, p, 1)) return p;
    }
    return 0;
}

uint16_t sock_alloc_ephemeral_port(void) {
    for (int tries = 0; tries < 28232; tries++) {
        uint16_t p = alloc_port(NULL);
        if (!tcp_port_in_use(p)) return p;
    }
    return 0;
}

static int local_address_ok(uint32_t ip) {
    if (ip == 0 || (ip >> 24) == 127) return 1;
    for (netdev_t *d = netdev_first(); d; d = d->next)
        if (d->ip == ip) return 1;
    return 0;
}

static int64_t autobind(sock_t *s) {
    if (s->bound) return 0;
    uint16_t p = alloc_port(s);
    if (!p) return -EAGAIN;
    s->local_port = p;
    s->bound = 1;
    return 0;
}

static void tcp_opts_of(const sock_t *s, tcp_opts_t *o) {
    o->rcvbuf = s->sk_rcvbuf ? s->sk_rcvbuf / 2 : 0;
    o->sndbuf = s->sk_sndbuf ? s->sk_sndbuf / 2 : 0;
    o->nodelay = s->opt_nodelay;
    o->keepalive = s->opt_keepalive;
    o->ka_idle = s->ka_idle;
    o->ka_intvl = s->ka_intvl;
    o->ka_cnt = s->ka_cnt;
}

static uint64_t timeo(int64_t ms) {
    return ms > 0 ? (uint64_t)ms : 0;
}

int64_t sock_op_bind(vnode_t *vn, const sock_addr_t *a) {
    sock_t *s = vn->fs_data;
    if (s->bound) return -EINVAL;
    if (a->family != s->family) return -EAFNOSUPPORT;
    if (s->family == AF_INET && !local_address_ok(a->ip)) return -EADDRNOTAVAIL;
    uint16_t port = a->port;
    if (port && port < 1024) {
        task_t *me = syscall_cur_task();
        if (me && me->uid != 0 && !cap_has(me->capabilities, CAP_NET_BIND)) return -EACCES;
    }
    if (!port) {
        port = alloc_port(s);
        if (!port) return -EADDRINUSE;
    } else if (port_taken(s, s->family == AF_INET ? a->ip : 0, port, 0)) {
        return -EADDRINUSE;
    }
    if (s->family == AF_INET) s->local_ip = a->ip;
    else memcpy(s->local_ip6, a->ip6, 16);
    s->local_port = port;
    s->bound = 1;
    return 0;
}

static int64_t stream_connected(sock_t *s, int r) {
    if (r == 0) {
        s->connected = 1;
        uint32_t ip = 0;
        uint8_t ip6[16];
        tcp_local(s->tcb, &ip, ip6);
        if (s->family == AF_INET) s->local_ip = ip;
        else memcpy(s->local_ip6, ip6, 16);
    }
    return r;
}

int64_t sock_op_connect(vnode_t *vn, const sock_addr_t *a, int nonblock) {
    sock_t *s = vn->fs_data;
    if (s->type == SOCK_STREAM) {
        if (s->listening) return -EINVAL;
        if (s->tcb) {
            int st = tcp_connect_status(s->tcb);
            if (st == 0) { int was = s->connected; stream_connected(s, 0); return was ? -EISCONN : 0; }
            if (st == -EINPROGRESS) {
                if (nonblock) return -EALREADY;
                return stream_connected(s, tcp_connect_wait(s->tcb, timeo(s->opt_sndtimeo_ms)));
            }
            return st;
        }
        if (a->family != s->family) return -EAFNOSUPPORT;
        int64_t b = autobind(s);
        if (b < 0) return b;
        tcp_opts_t o;
        tcp_opts_of(s, &o);
        int r = s->family == AF_INET6
              ? tcp_connect6_start(a->ip6, a->port, s->local_port, &o, &s->tcb)
              : tcp_connect_start(a->ip, a->port, s->local_port, &o, &s->tcb);
        if (r != 0) { s->tcb = NULL; return r; }
        s->peer_ip = a->ip;
        memcpy(s->peer_ip6, a->ip6, 16);
        s->peer_port = a->port;
        if (nonblock) { loopback_drain_all(); return -EINPROGRESS; }
        return stream_connected(s, tcp_connect_wait(s->tcb, timeo(s->opt_sndtimeo_ms)));
    }
    if (a->family == AF_UNSPEC) {
        s->connected = 0;
        s->peer_ip = 0;
        memset(s->peer_ip6, 0, 16);
        s->peer_port = 0;
        return 0;
    }
    if (a->family != s->family) return -EAFNOSUPPORT;
    if (s->type == SOCK_DGRAM) {
        int64_t b = autobind(s);
        if (b < 0) return b;
    }
    s->peer_ip = a->ip;
    memcpy(s->peer_ip6, a->ip6, 16);
    s->peer_port = a->port;
    s->connected = 1;
    return 0;
}

static const uint8_t g_lo6[16] = { 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,1 };

static int64_t dgram_send(sock_t *s, const void *buf, size_t len, const sock_addr_t *to) {
    netdev_t *dev = netdev_default();
    if (!dev) return -ENETUNREACH;
    if (s->wr_shutdown) return -EPIPE;

    uint32_t ip; uint16_t port;
    const uint8_t *ip6;
    if (to) {
        if (to->family != s->family) return -EAFNOSUPPORT;
        ip = to->ip; ip6 = to->ip6; port = to->port;
    } else {
        if (!s->connected) return -EDESTADDRREQ;
        ip = s->peer_ip; ip6 = s->peer_ip6; port = s->peer_port;
    }
    if (s->type == SOCK_DGRAM && len > UDP_MAX_PAYLOAD) return -EMSGSIZE;
    if (s->family == AF_INET && ip == 0xFFFFFFFFu && !s->opt_broadcast) return -EACCES;

    if (s->type == SOCK_DGRAM && !s->bound) {
        int64_t b = autobind(s);
        if (b < 0) return b;
    }

    uint8_t src6[16];
    if (s->family == AF_INET6) {
        if (memcmp(ip6, g_lo6, 16) == 0) memcpy(src6, g_lo6, 16); else ipv6_get_lladdr(dev, src6);
    }

    int r = -1;
    for (int attempt = 0; attempt < 12; attempt++) {
        if (s->family == AF_INET6) {
            if (s->type != SOCK_DGRAM) return -EOPNOTSUPP;
            r = udp6_send(dev, src6, ip6, s->local_port, port, buf, len);
        } else if (s->type == SOCK_DGRAM) {
            r = udp_send(dev, ip, s->local_port, port, buf, len);
        } else if (s->type == SOCK_RAW && s->proto == IPPROTO_ICMP) {
            r = ip_send(dev, ip, IPPROTO_ICMP, buf, len, s->ttl);
        } else {
            return -EOPNOTSUPP;
        }
        if (r == 0) break;
        task_sleep_ms(5);
    }
    if (r == 0) {
        if (s->type == SOCK_DGRAM) USTAT(out_dgrams);
        loopback_drain_all();
    }
    return (r == 0) ? (int64_t)len : -EHOSTUNREACH;
}

int64_t sock_op_sendto(vnode_t *vn, const void *buf, size_t len, int flags, const sock_addr_t *to) {
    sock_t *s = vn->fs_data;
    if (flags & MSG_OOB) return -EOPNOTSUPP;
    if (s->type != SOCK_STREAM) return dgram_send(s, buf, len, to);

    if (s->listening) return -ENOTCONN;
    if (s->wr_shutdown) return -EPIPE;
    if (!s->tcb) return -EPIPE;
    int tf = (flags & MSG_DONTWAIT) ? TCP_IO_NONBLOCK : 0;
    return tcp_send(s->tcb, buf, len, tf, timeo(s->opt_sndtimeo_ms));
}

static int64_t dgram_recv(sock_t *s, void *buf, size_t len, int flags, sock_addr_t *from, int *oflags) {
    task_t *me = syscall_cur_task();
    uint64_t deadline = s->opt_rcvtimeo_ms > 0 ? sched_now_ns() + (uint64_t)s->opt_rcvtimeo_ms * 1000000ull : 0;
    for (;;) {
        loopback_drain_all();
        uint64_t f = spinlock_acquire_irqsave(&s->lock);
        dgram_t *d = s->dq_head;
        if (d) {
            size_t n = d->len < len ? d->len : len;
            memcpy(buf, d->data, n);
            if (from) {
                from->family = s->family;
                from->ip = d->ip;
                memcpy(from->ip6, d->src6, 16);
                from->port = d->port;
            }
            uint32_t real = d->len;
            if (oflags && real > len) *oflags |= MSG_TRUNC;
            if (!(flags & MSG_PEEK)) {
                s->dq_head = d->next;
                if (!s->dq_head) s->dq_tail = NULL;
                s->dq_bytes -= d->len;
                spinlock_release_irqrestore(&s->lock, f);
                free(d);
            } else {
                spinlock_release_irqrestore(&s->lock, f);
            }
            return (flags & MSG_TRUNC) ? (int64_t)real : (int64_t)n;
        }
        if (s->rd_shutdown) { spinlock_release_irqrestore(&s->lock, f); return 0; }
        if (flags & MSG_DONTWAIT) { spinlock_release_irqrestore(&s->lock, f); return -EAGAIN; }
        if (interrupted(me)) { spinlock_release_irqrestore(&s->lock, f); return -EINTR; }
        if (deadline && sched_now_ns() >= deadline) { spinlock_release_irqrestore(&s->lock, f); return -EAGAIN; }
        waitq_block(&s->wq, &s->lock, f, deadline);
    }
}

int64_t sock_op_recvfrom(vnode_t *vn, void *buf, size_t len, int flags,
                         sock_addr_t *from, int *oflags) {
    sock_t *s = vn->fs_data;
    if (flags & MSG_OOB) return -EINVAL;
    if (s->type != SOCK_STREAM) return dgram_recv(s, buf, len, flags, from, oflags);

    if (s->listening || !s->tcb) return -ENOTCONN;
    if (from) {
        from->family = s->family;
        tcp_peer(s->tcb, &from->ip, from->ip6, &from->port);
    }
    int tf = 0;
    if (flags & MSG_DONTWAIT) tf |= TCP_IO_NONBLOCK;
    if (flags & MSG_PEEK)     tf |= TCP_IO_PEEK;
    if (flags & MSG_WAITALL)  tf |= TCP_IO_WAITALL;
    if (s->rd_shutdown && tcp_readable(s->tcb) == 0) return 0;
    return tcp_recv(s->tcb, buf, len, tf, timeo(s->opt_rcvtimeo_ms));
}

int64_t sock_op_readable(vnode_t *vn) {
    sock_t *s = vn->fs_data;
    if (s->type == SOCK_STREAM) return s->tcb && !s->listening ? tcp_readable(s->tcb) : 0;
    uint64_t f = spinlock_acquire_irqsave(&s->lock);
    int64_t n = s->dq_head ? s->dq_head->len : 0;
    spinlock_release_irqrestore(&s->lock, f);
    return n;
}

static int64_t sock_read_op(vnode_t *n, void *buf, size_t len, uint64_t off) {
    (void)off;
    return sock_op_recvfrom(n, buf, len, vfs_io_nonblock() ? MSG_DONTWAIT : 0, NULL, NULL);
}

static int64_t sock_write_op(vnode_t *n, const void *buf, size_t len, uint64_t off) {
    (void)off;
    sock_t *s = n->fs_data;
    if (s->type != SOCK_STREAM && !s->connected) return -EDESTADDRREQ;
    int64_t r = sock_op_sendto(n, buf, len, vfs_io_nonblock() ? MSG_DONTWAIT : 0, NULL);
    if (r == -EPIPE && s->type == SOCK_STREAM) sched_signal_current(SIGPIPE);
    return r;
}

static int sock_stat_op(vnode_t *n, vfs_stat_t *out) {
    memset(out, 0, sizeof(*out));
    out->st_ino  = n->ino;
    out->st_type = VFS_NODE_SOCKET;
    return 0;
}

static void sock_ref_op(vnode_t *n) { (void)n; }

static void sock_unref_op(vnode_t *n) {
    sock_t *s = n->fs_data;
    if (s->type == SOCK_STREAM && s->tcb) {
        if (s->opt_linger_on && s->opt_linger_s == 0 && !s->listening) tcp_abort(s->tcb);
        else tcp_close(s->tcb);
    }
    uint64_t g = spinlock_acquire_irqsave(&g_socks_lock);
    sock_t **pp = &g_socks;
    while (*pp) {
        if (*pp == s) { *pp = s->next; break; }
        pp = &(*pp)->next;
    }
    spinlock_release_irqrestore(&g_socks_lock, g);
    dgram_t *d = s->dq_head;
    while (d) { dgram_t *nx = d->next; free(d); d = nx; }
    free(s);
    free(n);
}

static int sock_poll_op(vnode_t *vn, int events) {
    (void)events;
    sock_t *s = vn->fs_data;
    if (s->type == SOCK_STREAM) {
        if (!s->tcb) return POLLOUT | POLLHUP;
        int r = tcp_poll(s->tcb);
        if (s->rd_shutdown) r |= POLLIN | POLLRDHUP;
        if (s->wr_shutdown && s->rd_shutdown) r |= POLLHUP;
        return r;
    }
    int r = s->wr_shutdown ? 0 : POLLOUT;
    uint64_t f = spinlock_acquire_irqsave(&s->lock);
    if (s->dq_head || s->rd_shutdown) r |= POLLIN;
    spinlock_release_irqrestore(&s->lock, f);
    return r;
}

static int64_t sock_ioctl_op(vnode_t *vn, uint64_t req, void *arg) {
    sock_t *s = vn->fs_data;
    switch (req) {
    case SIOCSTTL_K: {
        if (!arg) return -EINVAL;
        int ttl = *(int *)arg;
        if (ttl < 1 || ttl > 255) return -EINVAL;
        s->ttl = (uint8_t)ttl;
        return 0;
    }
    case FIONREAD_K:
        if (!arg) return -EFAULT;
        if (s->type == SOCK_STREAM && s->listening) return -EINVAL;
        *(int *)arg = (int)sock_op_readable(vn);
        return 0;
    case TIOCOUTQ_K:
        if (!arg) return -EFAULT;
        *(int *)arg = (s->type == SOCK_STREAM && s->tcb && !s->listening) ? (int)tcp_outq(s->tcb) : 0;
        return 0;
    default:
        return -ENOTTY;
    }
}

static const vnode_ops_t sock_vnode_ops = {
    .read  = sock_read_op,
    .write = sock_write_op,
    .stat  = sock_stat_op,
    .ref   = sock_ref_op,
    .unref = sock_unref_op,
    .poll  = sock_poll_op,
    .ioctl = sock_ioctl_op,
};

int sock_family(const vnode_t *vn) { sock_t *s = vn->fs_data; return s ? s->family : AF_INET; }
int sock_type(const vnode_t *vn)   { sock_t *s = vn->fs_data; return s ? s->type : SOCK_STREAM; }

static vnode_t *sock_wrap(sock_t *s) {
    vnode_t *vn = calloc(1, sizeof(*vn));
    if (!vn) return NULL;
    vn->type = VFS_NODE_SOCKET;
    vn->mode = 0600;
    vn->ino  = __atomic_fetch_add(&g_sock_ino, 1, __ATOMIC_RELAXED);
    vn->ops  = &sock_vnode_ops;
    vn->fs_data = s;
    vn->refcount = 1;

    uint64_t g = spinlock_acquire_irqsave(&g_socks_lock);
    s->next = g_socks;
    g_socks = s;
    spinlock_release_irqrestore(&g_socks_lock, g);
    return vn;
}

vnode_t *sock_new_vnode(int domain, int type, int proto) {
    if (domain != AF_INET && domain != AF_INET6) return NULL;
    if (type != SOCK_DGRAM && type != SOCK_RAW && type != SOCK_STREAM) return NULL;
    if (type == SOCK_DGRAM && proto == 0) proto = IPPROTO_UDP;
    if (type == SOCK_STREAM && proto == 0) proto = IPPROTO_TCP;
    if (type == SOCK_STREAM && proto != IPPROTO_TCP) return NULL;
    if (type == SOCK_DGRAM && proto != IPPROTO_UDP) return NULL;

    sock_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->type = type;
    s->proto = proto;
    s->family = domain;
    s->ttl = IP_DEFAULT_TTL;
    s->ka_idle = 7200;
    s->ka_intvl = 75;
    s->ka_cnt = 9;

    vnode_t *vn = sock_wrap(s);
    if (!vn) { free(s); return NULL; }
    return vn;
}

#define SOL_SOCKET_K    1
#define SOL_IP_K        0
#define SOL_TCP_K       6
#define SOL_IPV6_K      41

#define SO_REUSEADDR_K   2
#define SO_TYPE_K        3
#define SO_ERROR_K       4
#define SO_DONTROUTE_K   5
#define SO_BROADCAST_K   6
#define SO_SNDBUF_K      7
#define SO_RCVBUF_K      8
#define SO_KEEPALIVE_K   9
#define SO_OOBINLINE_K  10
#define SO_LINGER_K     13
#define SO_REUSEPORT_K  15
#define SO_RCVLOWAT_K   18
#define SO_SNDLOWAT_K   19
#define SO_RCVTIMEO_K   20
#define SO_SNDTIMEO_K   21
#define SO_ACCEPTCONN_K 30
#define SO_SNDBUFFORCE_K 32
#define SO_RCVBUFFORCE_K 33
#define SO_PROTOCOL_K   38
#define SO_DOMAIN_K     39

#define IP_TOS_K         1
#define IP_TTL_K         2

#define IPV6_V6ONLY_K   26

#define TCP_NODELAY_K    1
#define TCP_MAXSEG_K     2
#define TCP_KEEPIDLE_K   4
#define TCP_KEEPINTVL_K  5
#define TCP_KEEPCNT_K    6
#define TCP_QUICKACK_K  12

typedef struct { int64_t tv_sec; int64_t tv_usec; } sock_timeval_t;
typedef struct { int l_onoff; int l_linger; } sock_linger_t;

static void apply_tcp_opts(sock_t *s) {
    if (!s->tcb) return;
    tcp_opts_t o;
    tcp_opts_of(s, &o);
    tcp_set_opts(s->tcb, &o);
}

static uint32_t buf_from_user(int v, uint32_t minv, int force) {
    uint32_t val = v < 0 ? 0 : (uint32_t)v;
    if (!force && val > SOCK_MEM_MAX) val = SOCK_MEM_MAX;
    if (val > (64u << 20)) val = 64u << 20;
    val *= 2;
    return val < minv ? minv : val;
}

int64_t sock_op_setopt(vnode_t *vn, int level, int optname,
                       const void *val, uint32_t len) {
    sock_t *s = vn->fs_data;
    if (!s || !val) return -EFAULT;

    if (level == SOL_SOCKET_K && (optname == SO_RCVTIMEO_K || optname == SO_SNDTIMEO_K)) {
        if (len < sizeof(sock_timeval_t)) return -EINVAL;
        const sock_timeval_t *tv = val;
        if (tv->tv_usec < 0 || tv->tv_usec >= 1000000) return -EDOM;
        int64_t ms = tv->tv_sec * 1000 + (tv->tv_usec + 999) / 1000;
        if (ms < 0) ms = 0;
        if (optname == SO_RCVTIMEO_K) s->opt_rcvtimeo_ms = ms;
        else                          s->opt_sndtimeo_ms = ms;
        return 0;
    }
    if (level == SOL_SOCKET_K && optname == SO_LINGER_K) {
        if (len < sizeof(sock_linger_t)) return -EINVAL;
        const sock_linger_t *l = val;
        s->opt_linger_on = l->l_onoff != 0;
        s->opt_linger_s = l->l_linger < 0 ? 0 : l->l_linger;
        return 0;
    }

    if (len < sizeof(int)) return -EINVAL;
    int v = *(const int *)val;

    if (level == SOL_TCP_K) {
        if (s->type != SOCK_STREAM) return -ENOPROTOOPT;
        switch (optname) {
        case TCP_NODELAY_K:   s->opt_nodelay = v != 0; apply_tcp_opts(s); return 0;
        case TCP_MAXSEG_K:    return (v < 88 || v > 65535) && v != 0 ? -EINVAL : 0;
        case TCP_KEEPIDLE_K:  if (v < 1 || v > 32767) return -EINVAL; s->ka_idle = v; apply_tcp_opts(s); return 0;
        case TCP_KEEPINTVL_K: if (v < 1 || v > 32767) return -EINVAL; s->ka_intvl = v; apply_tcp_opts(s); return 0;
        case TCP_KEEPCNT_K:   if (v < 1 || v > 127) return -EINVAL; s->ka_cnt = v; apply_tcp_opts(s); return 0;
        case TCP_QUICKACK_K:  return 0;
        default: return -ENOPROTOOPT;
        }
    }
    if (level == SOL_IP_K) {
        if (s->family != AF_INET) return -ENOPROTOOPT;
        switch (optname) {
        case IP_TTL_K:
            if (v == -1) v = IP_DEFAULT_TTL;
            if (v < 1 || v > 255) return -EINVAL;
            s->ttl = (uint8_t)v;
            return 0;
        case IP_TOS_K: s->tos = (uint8_t)v; return 0;
        default: return -ENOPROTOOPT;
        }
    }
    if (level == SOL_IPV6_K) {
        if (s->family != AF_INET6) return -ENOPROTOOPT;
        if (optname == IPV6_V6ONLY_K) {
            if (s->bound) return -EINVAL;
            s->opt_v6only = v != 0;
            return 0;
        }
        return -ENOPROTOOPT;
    }
    if (level != SOL_SOCKET_K) return -ENOPROTOOPT;

    switch (optname) {
        case SO_REUSEADDR_K: s->opt_reuseaddr = v != 0; return 0;
        case SO_REUSEPORT_K: s->opt_reuseport = v != 0; return 0;
        case SO_BROADCAST_K: s->opt_broadcast = v != 0; return 0;
        case SO_DONTROUTE_K: s->opt_dontroute = v != 0; return 0;
        case SO_KEEPALIVE_K: s->opt_keepalive = v != 0; apply_tcp_opts(s); return 0;
        case SO_OOBINLINE_K: s->opt_oobinline = v != 0; return 0;
        case SO_RCVLOWAT_K:  return v < 0 ? -EINVAL : 0;
        case SO_SNDLOWAT_K:  return -ENOPROTOOPT;
        case SO_SNDBUFFORCE_K:
        case SO_SNDBUF_K: {
            int force = optname == SO_SNDBUFFORCE_K;
            if (force) {
                task_t *me = syscall_cur_task();
                if (me && me->uid != 0) return -EPERM;
            }
            s->sk_sndbuf = buf_from_user(v, SOCK_MIN_SND, force);
            apply_tcp_opts(s);
            return 0;
        }
        case SO_RCVBUFFORCE_K:
        case SO_RCVBUF_K: {
            int force = optname == SO_RCVBUFFORCE_K;
            if (force) {
                task_t *me = syscall_cur_task();
                if (me && me->uid != 0) return -EPERM;
            }
            s->sk_rcvbuf = buf_from_user(v, SOCK_MIN_RCV, force);
            apply_tcp_opts(s);
            return 0;
        }
        default: return -ENOPROTOOPT;
    }
}

int64_t sock_op_getopt(vnode_t *vn, int level, int optname,
                       void *val, uint32_t *len) {
    sock_t *s = vn->fs_data;
    if (!s || !val || !len) return -EFAULT;

    if (level == SOL_SOCKET_K && (optname == SO_RCVTIMEO_K || optname == SO_SNDTIMEO_K)) {
        if (*len < sizeof(sock_timeval_t)) return -EINVAL;
        int64_t ms = (optname == SO_RCVTIMEO_K) ? s->opt_rcvtimeo_ms : s->opt_sndtimeo_ms;
        sock_timeval_t tv = { ms / 1000, (ms % 1000) * 1000 };
        *(sock_timeval_t *)val = tv;
        *len = sizeof(sock_timeval_t);
        return 0;
    }
    if (level == SOL_SOCKET_K && optname == SO_LINGER_K) {
        if (*len < sizeof(sock_linger_t)) return -EINVAL;
        sock_linger_t l = { s->opt_linger_on, s->opt_linger_s };
        *(sock_linger_t *)val = l;
        *len = sizeof(sock_linger_t);
        return 0;
    }

    if (*len < sizeof(int)) return -EINVAL;
    int v;
    if (level == SOL_TCP_K) {
        if (s->type != SOCK_STREAM) return -ENOPROTOOPT;
        switch (optname) {
        case TCP_NODELAY_K:   v = s->opt_nodelay; break;
        case TCP_MAXSEG_K:    v = (s->tcb && !s->listening) ? (int)tcp_mss(s->tcb) : 536; break;
        case TCP_KEEPIDLE_K:  v = s->ka_idle; break;
        case TCP_KEEPINTVL_K: v = s->ka_intvl; break;
        case TCP_KEEPCNT_K:   v = s->ka_cnt; break;
        case TCP_QUICKACK_K:  v = 1; break;
        default: return -ENOPROTOOPT;
        }
    } else if (level == SOL_IP_K) {
        if (s->family != AF_INET) return -ENOPROTOOPT;
        switch (optname) {
        case IP_TTL_K: v = s->ttl; break;
        case IP_TOS_K: v = s->tos; break;
        default: return -ENOPROTOOPT;
        }
    } else if (level == SOL_IPV6_K) {
        if (s->family != AF_INET6 || optname != IPV6_V6ONLY_K) return -ENOPROTOOPT;
        v = s->opt_v6only;
    } else if (level == SOL_SOCKET_K) {
        switch (optname) {
        case SO_TYPE_K:      v = s->type; break;
        case SO_ERROR_K:
            v = s->last_error;
            s->last_error = 0;
            if (!v && s->tcb) v = tcp_take_error(s->tcb);
            break;
        case SO_REUSEADDR_K: v = s->opt_reuseaddr; break;
        case SO_REUSEPORT_K: v = s->opt_reuseport; break;
        case SO_BROADCAST_K: v = s->opt_broadcast; break;
        case SO_DONTROUTE_K: v = s->opt_dontroute; break;
        case SO_KEEPALIVE_K: v = s->opt_keepalive; break;
        case SO_SNDBUF_K:
        case SO_RCVBUF_K: {
            uint32_t want = optname == SO_SNDBUF_K ? s->sk_sndbuf : s->sk_rcvbuf;
            if (want) { v = (int)want; break; }
            if (s->type != SOCK_STREAM) { v = optname == SO_SNDBUF_K ? (int)UDP_WMEM_DEF : (int)UDP_RMEM_DEF; break; }
            if (!s->tcb) { v = optname == SO_SNDBUF_K ? (int)TCP_WMEM_DEF : (int)TCP_RMEM_DEF; break; }
            uint32_t r, w;
            tcp_get_bufs(s->tcb, &r, &w);
            v = (int)(optname == SO_SNDBUF_K ? w : r);
            break;
        }
        case SO_OOBINLINE_K: v = s->opt_oobinline; break;
        case SO_RCVLOWAT_K:
        case SO_SNDLOWAT_K:  v = 1; break;
        case SO_ACCEPTCONN_K: v = s->listening; break;
        case SO_DOMAIN_K:    v = s->family; break;
        case SO_PROTOCOL_K:  v = s->proto; break;
        default: return -ENOPROTOOPT;
        }
    } else {
        return -ENOPROTOOPT;
    }
    *(int *)val = v;
    *len = sizeof(int);
    return 0;
}

int64_t sock_op_shutdown(vnode_t *vn, int how) {
    sock_t *s = vn->fs_data;
    if (!s) return -EINVAL;
    if (how < 0 || how > 2) return -EINVAL;
    if (s->type == SOCK_STREAM && !s->connected && !s->listening) {
        if (!s->tcb || tcp_connect_status(s->tcb) != 0) return -ENOTCONN;
    }

    if (how == 0 || how == 2) s->rd_shutdown = 1;
    if ((how == 1 || how == 2) && !s->wr_shutdown) {
        s->wr_shutdown = 1;
        if (s->type == SOCK_STREAM && s->tcb && !s->listening) tcp_shutdown(s->tcb);
    }
    if (s->type == SOCK_STREAM && s->listening && how != 1 && s->tcb) {
        tcp_close(s->tcb);
        s->tcb = NULL;
        s->listening = 0;
    }
    uint64_t f = spinlock_acquire_irqsave(&s->lock);
    waitq_wake_all(&s->wq);
    spinlock_release_irqrestore(&s->lock, f);
    return 0;
}

int64_t sock_op_listen(vnode_t *vn, int backlog) {
    sock_t *s = vn->fs_data;
    if (s->type != SOCK_STREAM) return -EOPNOTSUPP;
    if (s->connected || (s->tcb && !s->listening)) return -EINVAL;
    if (s->listening) { tcp_set_backlog(s->tcb, backlog); s->backlog = backlog; return 0; }
    int64_t b = autobind(s);
    if (b < 0) return b;
    tcp_opts_t o;
    tcp_opts_of(s, &o);
    s->tcb = (s->family == AF_INET6) ? tcp_listen6(s->local_port, backlog, &o)
                                     : tcp_listen(s->local_port, backlog, &o);
    if (!s->tcb) return -EADDRINUSE;
    s->listening = 1;
    s->backlog = backlog;
    return 0;
}

int64_t sock_op_accept(vnode_t *vn, int nonblock, vnode_t **out, sock_addr_t *peer) {
    sock_t *ls = vn->fs_data;
    if (ls->type != SOCK_STREAM) return -EOPNOTSUPP;
    if (!ls->listening || !ls->tcb) return -EINVAL;

    tcp_tcb_t *child = NULL;
    int r = tcp_accept(ls->tcb, nonblock ? TCP_IO_NONBLOCK : 0, timeo(ls->opt_rcvtimeo_ms), &child);
    if (r < 0) return r;

    sock_t *s = calloc(1, sizeof(*s));
    if (!s) { tcp_abort(child); return -ENOMEM; }
    s->type = SOCK_STREAM;
    s->proto = IPPROTO_TCP;
    s->family = ls->family;
    s->tcb = child;
    s->connected = 1;
    s->bound = 1;
    s->ttl = ls->ttl;
    s->opt_nodelay = ls->opt_nodelay;
    s->opt_keepalive = ls->opt_keepalive;
    s->sk_rcvbuf = ls->sk_rcvbuf;
    s->sk_sndbuf = ls->sk_sndbuf;
    s->opt_linger_on = ls->opt_linger_on;
    s->opt_linger_s = ls->opt_linger_s;
    s->ka_idle = ls->ka_idle;
    s->ka_intvl = ls->ka_intvl;
    s->ka_cnt = ls->ka_cnt;
    s->local_port = ls->local_port;
    tcp_local(child, &s->local_ip, s->local_ip6);
    tcp_peer(child, &s->peer_ip, s->peer_ip6, &s->peer_port);

    vnode_t *cvn = sock_wrap(s);
    if (!cvn) { tcp_abort(child); free(s); return -ENOMEM; }
    if (peer) {
        peer->family = s->family;
        peer->ip = s->peer_ip;
        memcpy(peer->ip6, s->peer_ip6, 16);
        peer->port = s->peer_port;
    }
    *out = cvn;
    return 0;
}

int sock_get_name(const vnode_t *vn, int peer, sock_addr_t *out)
{
    sock_t *s = vn->fs_data;
    if (!s) return -EBADF;
    memset(out, 0, sizeof(*out));
    out->family = s->family;
    if (peer) {
        if (!s->connected) {
            if (s->type == SOCK_STREAM && s->tcb && !s->listening && tcp_connect_status(s->tcb) == 0)
                s->connected = 1;
            else
                return -ENOTCONN;
        }
        out->ip = s->peer_ip;
        memcpy(out->ip6, s->peer_ip6, 16);
        out->port = s->peer_port;
        return 0;
    }
    out->ip = s->local_ip;
    memcpy(out->ip6, s->local_ip6, 16);
    if (s->family == AF_INET && !out->ip && s->connected && s->tcb)
        tcp_local(s->tcb, &out->ip, NULL);
    if (s->family == AF_INET && !out->ip && s->connected && s->type != SOCK_STREAM) {
        netdev_t *d = (s->peer_ip >> 24) == 127 ? NULL : netdev_route(s->peer_ip, NULL);
        if (!d && (s->peer_ip >> 24) != 127) d = netdev_default();
        out->ip = d ? d->ip : 0x7F000001u;
    }
    out->port = s->local_port;
    return 0;
}

int sock_proc_udp(char *buf, size_t max, int family) {
    int v6 = family == AF_INET6;
    int n = snprintf(buf, max, v6
        ? "  sl  local_address                         remote_address                        st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ref pointer drops\n"
        : "   sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ref pointer drops\n");
    int idx = 0;
    uint64_t g = spinlock_acquire_irqsave(&g_socks_lock);
    for (sock_t *s = g_socks; s && (size_t)n + 256 < max; s = s->next) {
        if (s->type != SOCK_DGRAM || (s->family == AF_INET6) != v6) continue;
        uint64_t f = spinlock_acquire_irqsave(&s->lock);
        uint32_t rxq = s->dq_bytes;
        spinlock_release_irqrestore(&s->lock, f);
        int st = s->connected ? 0x01 : 0x07;
        if (v6) {
            uint32_t l[4], r[4];
            memcpy(l, s->local_ip6, 16);
            memcpy(r, s->peer_ip6, 16);
            n += snprintf(buf + n, max - (size_t)n,
                          "%5d: %08X%08X%08X%08X:%04X %08X%08X%08X%08X:%04X %02X %08X:%08X %02X:%08lX %08X %5u %8d %lu %d %016lX %u\n",
                          idx, l[0], l[1], l[2], l[3], s->local_port, r[0], r[1], r[2], r[3],
                          s->connected ? s->peer_port : 0, st, 0u, rxq, 0, 0ul, 0u, 0u, 0, 0ul, 2, 0ul, 0u);
        } else {
            n += snprintf(buf + n, max - (size_t)n,
                          "%5d: %08X:%04X %08X:%04X %02X %08X:%08X %02X:%08lX %08X %5u %8d %lu %d %016lX %u\n",
                          idx, __builtin_bswap32(s->local_ip), s->local_port,
                          __builtin_bswap32(s->connected ? s->peer_ip : 0), s->connected ? s->peer_port : 0,
                          st, 0u, rxq, 0, 0ul, 0u, 0u, 0, 0ul, 2, 0ul, 0u);
        }
        idx++;
    }
    spinlock_release_irqrestore(&g_socks_lock, g);
    return n;
}

int sock_proc_udp_snmp(char *buf, size_t max) {
    return snprintf(buf, max,
        "Udp: InDatagrams NoPorts InErrors OutDatagrams RcvbufErrors SndbufErrors InCsumErrors IgnoredMulti MemErrors\n"
        "Udp: %lu %lu %lu %lu %lu 0 0 0 0\n",
        (unsigned long)g_ustat.in_dgrams, (unsigned long)g_ustat.no_ports,
        (unsigned long)g_ustat.rcvbuf_errors, (unsigned long)g_ustat.out_dgrams,
        (unsigned long)g_ustat.rcvbuf_errors);
}
