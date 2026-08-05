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
#include "../../include/syscall/syscall_internal.h"
#include "../../include/syscall/errno.h"
#include <string.h>
#include <stdlib.h>

#define SOCK_RXQ       16
#define SOCK_DGRAM_MAX 1536

typedef struct {
    uint32_t ip;
    uint8_t  src6[16];
    uint16_t port;
    uint16_t len;
    uint8_t  data[SOCK_DGRAM_MAX];
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

    dgram_t      q[SOCK_RXQ];
    int          qh, qt, qc;
    spinlock_t   lock;
    task_t      *reader;

    tcp_tcb_t   *tcb;

    struct sock *next;
} sock_t;

static sock_t   *g_socks;
static spinlock_t g_socks_lock;
static uint16_t   g_ephemeral = 49152;
static uint64_t   g_sock_ino = 0x20000;

static const vnode_ops_t sock_vnode_ops;

int sock_is_vnode(const vnode_t *vn) {
    return vn && vn->ops == &sock_vnode_ops;
}

static void sock_enqueue2(sock_t *s, uint32_t ip, const uint8_t *ip6, uint16_t port, const uint8_t *data, size_t len) {
    if (len > SOCK_DGRAM_MAX) len = SOCK_DGRAM_MAX;
    uint64_t f = spinlock_acquire_irqsave(&s->lock);
    if (s->qc < SOCK_RXQ) {
        dgram_t *d = &s->q[s->qt];
        d->ip = ip; d->port = port; d->len = (uint16_t)len;
        if (ip6) memcpy(d->src6, ip6, 16); else memset(d->src6, 0, 16);
        memcpy(d->data, data, len);
        s->qt = (s->qt + 1) % SOCK_RXQ;
        s->qc++;
        task_t *r = s->reader;
        s->reader = NULL;
        spinlock_release_irqrestore(&s->lock, f);
        if (r) task_unblock(r);
    } else {
        spinlock_release_irqrestore(&s->lock, f);
    }
}
static void sock_enqueue(sock_t *s, uint32_t ip, uint16_t port, const uint8_t *data, size_t len) {
    sock_enqueue2(s, ip, 0, port, data, len);
}

int sock_udp_input(uint32_t src_ip, uint16_t src_port, uint16_t dst_port,
                   const uint8_t *data, size_t len) {
    int delivered = 0;
    spinlock_acquire(&g_socks_lock);
    for (sock_t *s = g_socks; s; s = s->next) {
        if (s->type == SOCK_DGRAM && s->bound && s->local_port == dst_port) {
            sock_enqueue(s, src_ip, src_port, data, len);
            delivered = 1;
            break;
        }
    }
    spinlock_release(&g_socks_lock);
    return delivered;
}

int sock_udp6_input(const uint8_t *src6, uint16_t src_port, uint16_t dst_port,
                    const uint8_t *data, size_t len) {
    int delivered = 0;
    spinlock_acquire(&g_socks_lock);
    for (sock_t *s = g_socks; s; s = s->next) {
        if (s->family == AF_INET6 && s->type == SOCK_DGRAM && s->bound && s->local_port == dst_port) {
            sock_enqueue2(s, 0, src6, src_port, data, len);
            delivered = 1;
            break;
        }
    }
    spinlock_release(&g_socks_lock);
    return delivered;
}

void sock_icmp_input(uint32_t src_ip, const uint8_t *data, size_t len) {
    spinlock_acquire(&g_socks_lock);
    for (sock_t *s = g_socks; s; s = s->next)
        if (s->type == SOCK_RAW && s->proto == IPPROTO_ICMP)
            sock_enqueue(s, src_ip, 0, data, len);
    spinlock_release(&g_socks_lock);
}

int64_t sock_op_bind(vnode_t *vn, uint32_t ip, uint16_t port) {
    (void)ip;
    sock_t *s = vn->fs_data;
    s->local_port = port;
    s->bound = 1;
    return 0;
}

int64_t sock_op_connect(vnode_t *vn, uint32_t ip, uint16_t port, int nonblock) {
    sock_t *s = vn->fs_data;
    if (s->type == SOCK_STREAM) {
        if (s->tcb) {
            int st = tcp_connect_status(s->tcb);
            if (st == 0) { int was = s->connected; s->connected = 1; return was ? -EISCONN : 0; }
            if (st == -EINPROGRESS) return -EALREADY;
            return st;
        }
        if (nonblock) {
            int r = tcp_connect_start(ip, port, &s->tcb);
            if (r != 0) return r;
            s->peer_ip = ip; s->peer_port = port;
            return -EINPROGRESS;
        }
        int r = tcp_connect(ip, port, &s->tcb);
        if (r != 0) return r;
        s->peer_ip = ip; s->peer_port = port; s->connected = 1;
        return 0;
    }
    s->peer_ip = ip;
    s->peer_port = port;
    s->connected = 1;
    return 0;
}

int64_t sock_op_sendto(vnode_t *vn, const void *buf, size_t len, uint32_t ip, uint16_t port) {
    sock_t *s = vn->fs_data;
    netdev_t *dev = netdev_first();
    if (!dev) return -EINVAL;

    if (s->type == SOCK_STREAM) {
        if (!s->tcb) return -EINVAL;
        return tcp_send(s->tcb, buf, len);
    }

    if (!ip) { if (!s->connected) return -EINVAL; ip = s->peer_ip; port = s->peer_port; }

    if (s->type == SOCK_DGRAM && !s->bound) {
        s->local_port = g_ephemeral++;
        if (g_ephemeral == 0) g_ephemeral = 49152;
        s->bound = 1;
    }

    int r = -1;
    for (int attempt = 0; attempt < 12; attempt++) {
        if (s->type == SOCK_DGRAM)
            r = udp_send(dev, ip, s->local_port, port, buf, len);
        else if (s->type == SOCK_RAW && s->proto == IPPROTO_ICMP)
            r = ip_send(dev, ip, IPPROTO_ICMP, buf, len);
        else
            return -EOPNOTSUPP;
        if (r == 0) break;
        task_sleep_ms(5);
    }
    return (r == 0) ? (int64_t)len : -EAGAIN;
}

int64_t sock_op_recvfrom(vnode_t *vn, void *buf, size_t len, int nonblock,
                         uint32_t *src_ip, uint16_t *src_port) {
    sock_t *s = vn->fs_data;
    task_t *me = syscall_cur_task();

    if (s->type == SOCK_STREAM) {
        if (!s->tcb) return -EINVAL;
        if (src_ip)   *src_ip = s->peer_ip;
        if (src_port) *src_port = s->peer_port;
        return tcp_recv(s->tcb, buf, len, nonblock);
    }

    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&s->lock);
        if (s->qc > 0) {
            dgram_t *d = &s->q[s->qh];
            size_t n = d->len;
            if (n > len) n = len;
            memcpy(buf, d->data, n);
            if (src_ip)   *src_ip = d->ip;
            if (src_port) *src_port = d->port;
            s->qh = (s->qh + 1) % SOCK_RXQ;
            s->qc--;
            spinlock_release_irqrestore(&s->lock, f);
            return (int64_t)n;
        }
        if (nonblock) { spinlock_release_irqrestore(&s->lock, f); return -EAGAIN; }
        if (me && me->pending_kill) { spinlock_release_irqrestore(&s->lock, f); return -EINTR; }
        if (me) { s->reader = me; me->runnable = false; me->state = TASK_BLOCKED; }
        spinlock_release_irqrestore(&s->lock, f);
        if (me) sched_reschedule();
        else    task_yield();
    }
}

static int64_t sock_read_op(vnode_t *n, void *buf, size_t len, uint64_t off) {
    (void)off;
    return sock_op_recvfrom(n, buf, len, 0, NULL, NULL);
}

static int64_t sock_write_op(vnode_t *n, const void *buf, size_t len, uint64_t off) {
    (void)off;
    sock_t *s = n->fs_data;
    if (!s->connected) return -EINVAL;
    return sock_op_sendto(n, buf, len, s->peer_ip, s->peer_port);
}

static int sock_stat_op(vnode_t *n, vfs_stat_t *out) {
    memset(out, 0, sizeof(*out));
    out->st_ino  = n->ino;
    out->st_type = VFS_NODE_CHARDEV;
    return 0;
}

static void sock_ref_op(vnode_t *n) { (void)n; }

static void sock_unref_op(vnode_t *n) {
    sock_t *s = n->fs_data;
    if (s->type == SOCK_STREAM && s->tcb) tcp_close(s->tcb);
    spinlock_acquire(&g_socks_lock);
    sock_t **pp = &g_socks;
    while (*pp) {
        if (*pp == s) { *pp = s->next; break; }
        pp = &(*pp)->next;
    }
    spinlock_release(&g_socks_lock);
    free(s);
    free(n);
}

static int sock_poll_op(vnode_t *vn, int events) {
    (void)events;
    sock_t *s = vn->fs_data;
    if (s->type == SOCK_STREAM) {
        if (!s->tcb) return POLLHUP;
        return tcp_poll(s->tcb);
    }
    int r = POLLOUT;
    uint64_t f = spinlock_acquire_irqsave(&s->lock);
    if (s->qc > 0) r |= POLLIN;
    spinlock_release_irqrestore(&s->lock, f);
    return r;
}

static const vnode_ops_t sock_vnode_ops = {
    .read  = sock_read_op,
    .write = sock_write_op,
    .stat  = sock_stat_op,
    .ref   = sock_ref_op,
    .unref = sock_unref_op,
    .poll  = sock_poll_op,
};

int sock_family(const vnode_t *vn) { sock_t *s = vn->fs_data; return s ? s->family : AF_INET; }

int64_t sock_op_bind6(vnode_t *vn, const uint8_t ip6[16], uint16_t port) {
    (void)ip6;
    sock_t *s = vn->fs_data;
    s->local_port = port; s->bound = 1;
    return 0;
}

int64_t sock_op_connect6(vnode_t *vn, const uint8_t ip6[16], uint16_t port) {
    sock_t *s = vn->fs_data;
    if (s->type == SOCK_STREAM) {
        int r = tcp_connect6(ip6, port, &s->tcb);
        if (r != 0) return r;
        memcpy(s->peer_ip6, ip6, 16); s->peer_port = port; s->connected = 1;
        return 0;
    }
    memcpy(s->peer_ip6, ip6, 16); s->peer_port = port; s->connected = 1;
    return 0;
}

static const uint8_t g_lo6[16] = { 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,1 };

int64_t sock_op_sendto6(vnode_t *vn, const void *buf, size_t len, const uint8_t ip6[16], uint16_t port) {
    sock_t *s = vn->fs_data;
    netdev_t *dev = netdev_first();
    if (!dev) return -EINVAL;
    if (s->type == SOCK_STREAM) {
        if (!s->tcb) return -EINVAL;
        return tcp_send(s->tcb, buf, len);
    }
    if (s->type != SOCK_DGRAM) return -EOPNOTSUPP;

    const uint8_t *dst = ip6;
    uint8_t zero[16]; memset(zero, 0, 16);
    if (memcmp(dst, zero, 16) == 0) { if (!s->connected) return -EINVAL; dst = s->peer_ip6; port = s->peer_port; }

    if (!s->bound) { s->local_port = g_ephemeral++; if (g_ephemeral == 0) g_ephemeral = 49152; s->bound = 1; }

    uint8_t src[16];
    if (memcmp(dst, g_lo6, 16) == 0) memcpy(src, g_lo6, 16); else ipv6_get_lladdr(dev, src);

    int r = -1;
    for (int attempt = 0; attempt < 12; attempt++) {
        r = udp6_send(dev, src, dst, s->local_port, port, buf, len);
        if (r == 0) break;
        task_sleep_ms(5);
    }
    return (r == 0) ? (int64_t)len : -EAGAIN;
}

int64_t sock_op_recvfrom6(vnode_t *vn, void *buf, size_t len, int nonblock, uint8_t src6[16], uint16_t *src_port) {
    sock_t *s = vn->fs_data;
    task_t *me = syscall_cur_task();
    if (s->type == SOCK_STREAM) {
        if (!s->tcb) return -EINVAL;
        if (src6) memcpy(src6, s->peer_ip6, 16);
        if (src_port) *src_port = s->peer_port;
        return tcp_recv(s->tcb, buf, len, nonblock);
    }
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&s->lock);
        if (s->qc > 0) {
            dgram_t *d = &s->q[s->qh];
            size_t n = d->len; if (n > len) n = len;
            memcpy(buf, d->data, n);
            if (src6) memcpy(src6, d->src6, 16);
            if (src_port) *src_port = d->port;
            s->qh = (s->qh + 1) % SOCK_RXQ; s->qc--;
            spinlock_release_irqrestore(&s->lock, f);
            return (int64_t)n;
        }
        if (nonblock) { spinlock_release_irqrestore(&s->lock, f); return -EAGAIN; }
        if (me && me->pending_kill) { spinlock_release_irqrestore(&s->lock, f); return -EINTR; }
        if (me) { s->reader = me; me->runnable = false; me->state = TASK_BLOCKED; }
        spinlock_release_irqrestore(&s->lock, f);
        if (me) sched_reschedule(); else task_yield();
    }
}

vnode_t *sock_new_vnode(int domain, int type, int proto) {
    if (domain != AF_INET && domain != AF_INET6) return NULL;
    if (type != SOCK_DGRAM && type != SOCK_RAW && type != SOCK_STREAM) return NULL;
    if (type == SOCK_DGRAM && proto == 0) proto = IPPROTO_UDP;
    if (type == SOCK_STREAM && proto == 0) proto = IPPROTO_TCP;

    sock_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->type = type;
    s->proto = proto;
    s->family = domain;

    vnode_t *vn = calloc(1, sizeof(*vn));
    if (!vn) { free(s); return NULL; }
    vn->type = VFS_NODE_CHARDEV;
    vn->mode = 0600;
    vn->ino  = g_sock_ino++;
    vn->ops  = &sock_vnode_ops;
    vn->fs_data = s;
    vn->refcount = 1;

    spinlock_acquire(&g_socks_lock);
    s->next = g_socks;
    g_socks = s;
    spinlock_release(&g_socks_lock);
    return vn;
}

int64_t sock_op_listen(vnode_t *vn) {
    sock_t *s = vn->fs_data;
    if (s->type != SOCK_STREAM || !s->bound) return -EINVAL;
    if (s->tcb) return 0;
    s->tcb = (s->family == AF_INET6) ? tcp_listen6(s->local_port) : tcp_listen(s->local_port);
    if (!s->tcb) return -ENOMEM;
    return 0;
}

vnode_t *sock_op_accept(vnode_t *vn, int nonblock, uint32_t *rip, uint16_t *rport) {
    sock_t *ls = vn->fs_data;
    if (ls->type != SOCK_STREAM || !ls->tcb) return NULL;

    tcp_tcb_t *child = tcp_accept(ls->tcb, nonblock, rip, rport);
    if (!child) return NULL;

    sock_t *s = calloc(1, sizeof(*s));
    if (!s) { tcp_close(child); return NULL; }
    s->type = SOCK_STREAM;
    s->proto = IPPROTO_TCP;
    s->tcb = child;
    s->connected = 1;
    s->peer_ip = *rip;
    s->peer_port = *rport;

    vnode_t *cvn = calloc(1, sizeof(*cvn));
    if (!cvn) { tcp_close(child); free(s); return NULL; }
    cvn->type = VFS_NODE_CHARDEV;
    cvn->mode = 0600;
    cvn->ino  = g_sock_ino++;
    cvn->ops  = &sock_vnode_ops;
    cvn->fs_data = s;
    cvn->refcount = 1;

    spinlock_acquire(&g_socks_lock);
    s->next = g_socks;
    g_socks = s;
    spinlock_release(&g_socks_lock);
    return cvn;
}

vnode_t *sock_op_accept6(vnode_t *vn, int nonblock, uint8_t rip6[16], uint16_t *rport) {
    sock_t *ls = vn->fs_data;
    if (ls->type != SOCK_STREAM || !ls->tcb) return NULL;

    tcp_tcb_t *child = tcp_accept6(ls->tcb, nonblock, rip6, rport);
    if (!child) return NULL;

    sock_t *s = calloc(1, sizeof(*s));
    if (!s) { tcp_close(child); return NULL; }
    s->type = SOCK_STREAM;
    s->proto = IPPROTO_TCP;
    s->family = AF_INET6;
    s->tcb = child;
    s->connected = 1;
    memcpy(s->peer_ip6, rip6, 16);
    s->peer_port = *rport;

    vnode_t *cvn = calloc(1, sizeof(*cvn));
    if (!cvn) { tcp_close(child); free(s); return NULL; }
    cvn->type = VFS_NODE_CHARDEV;
    cvn->mode = 0600;
    cvn->ino  = g_sock_ino++;
    cvn->ops  = &sock_vnode_ops;
    cvn->fs_data = s;
    cvn->refcount = 1;

    spinlock_acquire(&g_socks_lock);
    s->next = g_socks;
    g_socks = s;
    spinlock_release(&g_socks_lock);
    return cvn;
}
