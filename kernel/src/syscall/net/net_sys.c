#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/io/serial.h"
#include "../../../include/net/socket.h"
#include "../../../include/net/net.h"
#include "../../../include/net/netdev.h"
#include "../../../include/net/ip.h"
#include "../../../include/signal/signal.h"
#include <string.h>
#include <stdlib.h>

#define SOCK_STACK_IO 2048
#define DGRAM_MAX     65535

vnode_t *net_sock_from_fd(task_t *t, int fd, vfs_file_t **out_file, int64_t *err) {
    vfs_file_t *f = fd_get(t->fd_table, fd);
    if (!f) { *err = -EBADF; return NULL; }
    if (!sock_is_vnode(f->vnode) && !unix_is_vnode(f->vnode)) {
        fd_put(f);
        *err = -ENOTSOCK;
        return NULL;
    }
    *out_file = f;
    return f->vnode;
}

int64_t net_parse_addr(uint64_t uaddr, uint64_t addrlen, sock_addr_t *a) {
    memset(a, 0, sizeof(*a));
    if (!uaddr) return -EFAULT;
    if (addrlen < 2 || addrlen > 128) return -EINVAL;
    uint8_t sa[28];
    memset(sa, 0, sizeof sa);
    size_t n = addrlen > sizeof sa ? sizeof sa : (size_t)addrlen;
    if (syscall_copy_from_user(sa, (const void *)uaddr, n) < 0) return -EFAULT;
    a->family = sa[0] | (sa[1] << 8);
    if (a->family == AF_UNIX) {
        size_t n = addrlen - 2;
        if (n > UNIX_PATH_MAX) n = UNIX_PATH_MAX;
        if (n && syscall_copy_from_user(a->upath, (const uint8_t *)uaddr + 2, n) < 0) return -EFAULT;
        if (n && a->upath[0]) {
            size_t l = 0;
            while (l < n && a->upath[l]) l++;
            n = l;
        }
        a->ulen = (uint32_t)n;
        return 0;
    }
    if (a->family == AF_INET) {
        if (addrlen < 16) return -EINVAL;
        a->port = rd16be(sa + 2);
        a->ip = rd32be(sa + 4);
    } else if (a->family == AF_INET6) {
        if (addrlen < 24) return -EINVAL;
        a->port = rd16be(sa + 2);
        memcpy(a->ip6, sa + 8, 16);
    } else if (a->family != AF_UNSPEC) {
        return -EAFNOSUPPORT;
    }
    return 0;
}

int64_t net_put_addr(const sock_addr_t *a, uint64_t uaddr, uint32_t cap, uint32_t *outlen) {
    uint8_t sa[2 + UNIX_PATH_MAX + 1];
    memset(sa, 0, sizeof sa);
    uint32_t len;
    if (a->family == AF_UNIX) {
        sa[0] = AF_UNIX;
        uint32_t ul = a->ulen > UNIX_PATH_MAX ? UNIX_PATH_MAX : a->ulen;
        memcpy(sa + 2, a->upath, ul);
        len = 2 + ul;
        if (ul && a->upath[0] && ul < UNIX_PATH_MAX) len++;
    } else if (a->family == AF_INET6) {
        sa[0] = AF_INET6;
        wr16be(sa + 2, a->port);
        memcpy(sa + 8, a->ip6, 16);
        len = 28;
    } else {
        sa[0] = AF_INET;
        wr16be(sa + 2, a->port);
        wr32be(sa + 4, a->ip);
        len = 16;
    }
    uint32_t n = cap < len ? cap : len;
    if (n && syscall_copy_to_user((void *)uaddr, sa, n) < 0) return -EFAULT;
    *outlen = len;
    return 0;
}

static int64_t put_addr_user(const sock_addr_t *a, uint64_t uaddr, uint64_t ulen) {
    if (!uaddr || !ulen) return 0;
    int cap;
    if (syscall_copy_from_user(&cap, (const void *)ulen, sizeof cap) < 0) return -EFAULT;
    if (cap < 0) return -EINVAL;
    uint32_t len = 0;
    int64_t r = net_put_addr(a, uaddr, (uint32_t)cap, &len);
    if (r < 0) return r;
    int l = (int)len;
    if (syscall_copy_to_user((void *)ulen, &l, sizeof l) < 0) return -EFAULT;
    return 0;
}

static int is_stream(vnode_t *vn) {
    if (unix_is_vnode(vn)) return unix_sock_type(vn) == SOCK_STREAM;
    return sock_type(vn) == SOCK_STREAM;
}

void net_sigpipe(vnode_t *vn, int64_t r, int flags) {
    if (r != -EPIPE || (flags & MSG_NOSIGNAL)) return;
    if (unix_is_vnode(vn) ? unix_sock_type(vn) != SOCK_DGRAM : sock_type(vn) == SOCK_STREAM)
        sched_signal_current(SIGPIPE);
}

int64_t net_send_user(vnode_t *vn, uint64_t ubuf, size_t len, int flags, const sock_addr_t *to) {
    if (len && !syscall_uptr_validate((void *)ubuf, len)) return -EFAULT;
    int is_unix = unix_is_vnode(vn);
    int stream = is_stream(vn);
    if (!stream && !is_unix && len > DGRAM_MAX) return -EMSGSIZE;

    uint8_t stackbuf[SOCK_STACK_IO];
    size_t cap = 0;
    void *kb = syscall_bounce_get(len ? len : 1, &cap, stackbuf, sizeof stackbuf);
    if (!stream && cap < len) { syscall_bounce_put(kb, stackbuf); return -ENOBUFS; }

    size_t total = 0;
    int64_t r = 0;
    do {
        size_t chunk = len - total;
        if (chunk > cap) chunk = cap;
        if (chunk && syscall_copy_from_user(kb, (const uint8_t *)ubuf + total, chunk) < 0) { r = -EFAULT; break; }
        r = is_unix ? unix_op_sendmsg(vn, kb, chunk, flags, to, NULL)
                    : sock_op_sendto(vn, kb, chunk, flags, to);
        if (r < 0) break;
        total += (size_t)r;
        if ((size_t)r < chunk) break;
    } while (stream && total < len);
    syscall_bounce_put(kb, stackbuf);
    if (total) return (int64_t)total;
    return r;
}

int64_t net_recv_user(vnode_t *vn, uint64_t ubuf, size_t len, int flags, sock_addr_t *from, int *oflags) {
    if (len && !syscall_uptr_validate_write((void *)ubuf, len)) return -EFAULT;
    int is_unix = unix_is_vnode(vn);
    int stream = is_stream(vn);

    uint8_t stackbuf[SOCK_STACK_IO];
    size_t cap = 0;
    size_t want = len;
    if (!stream && !is_unix && want > DGRAM_MAX) want = DGRAM_MAX;
    void *kb = syscall_bounce_get(want ? want : 1, &cap, stackbuf, sizeof stackbuf);
    if (!stream && cap < want) { syscall_bounce_put(kb, stackbuf); return -ENOBUFS; }

    size_t total = 0;
    int64_t r = 0;
    do {
        size_t chunk = len - total;
        if (chunk > cap) chunk = cap;
        r = is_unix ? unix_op_recvmsg(vn, kb, chunk, flags, from, NULL, oflags)
                    : sock_op_recvfrom(vn, kb, chunk, flags, from, oflags);
        if (r <= 0) break;
        size_t got = (size_t)r < chunk ? (size_t)r : chunk;
        if (got && syscall_copy_to_user((uint8_t *)ubuf + total, kb, got) < 0) { r = -EFAULT; break; }
        if (!stream) { total = (size_t)r; break; }
        total += got;
        if (got < chunk || !(flags & MSG_WAITALL) || (flags & MSG_PEEK)) break;
    } while (total < len);
    syscall_bounce_put(kb, stackbuf);
    if (total) return (int64_t)total;
    return r;
}

static int sock_type_ok(int domain, int type, int proto) {
    if (domain == AF_UNIX) {
        if (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_SEQPACKET)
            return (type > 0 && type <= 10) ? -ESOCKTNOSUPPORT : -EINVAL;
        return proto == 0 ? 0 : -EPROTONOSUPPORT;
    }
    if (domain != AF_INET && domain != AF_INET6) return -EAFNOSUPPORT;
    if (type == SOCK_STREAM) return (proto == 0 || proto == IPPROTO_TCP) ? 0 : -EPROTONOSUPPORT;
    if (type == SOCK_DGRAM)  return (proto == 0 || proto == IPPROTO_UDP) ? 0 : -EPROTONOSUPPORT;
    if (type == SOCK_RAW)    return proto == IPPROTO_ICMP ? 0 : -EPROTONOSUPPORT;
    if (type > 0 && type <= 10) return -ESOCKTNOSUPPORT;
    return -EINVAL;
}

int64_t sys_socket(uint64_t domain, uint64_t type, uint64_t proto) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -ENOMEM;
    int sflags = (int)(type & (SOCK_NONBLOCK | SOCK_CLOEXEC));
    type &= ~(uint64_t)(SOCK_NONBLOCK | SOCK_CLOEXEC);
    int ok = sock_type_ok((int)domain, (int)type, (int)proto);
    if (ok < 0) return ok;
    vnode_t *vn = (domain == AF_UNIX) ? unix_new_vnode((int)type)
                                      : sock_new_vnode((int)domain, (int)type, (int)proto);
    if (!vn) return -ENOMEM;
    vfs_file_t *f = vfs_file_alloc();
    if (!f) { vn->ops->unref(vn); return -ENOMEM; }
    f->vnode = vn; f->flags = O_RDWR; f->offset = 0; f->refcount = 1;
    if (sflags & SOCK_NONBLOCK) f->flags |= O_NONBLOCK;
    int fd = fd_alloc(t->fd_table, f, 0);
    if (fd < 0) { vfs_file_free(f); return -EMFILE; }
    if (sflags & SOCK_CLOEXEC) fd_set_flags(t->fd_table, fd, FD_CLOEXEC);
    return fd;
}

int64_t sys_bind(uint64_t fd, uint64_t uaddr, uint64_t addrlen) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    int64_t r;
    vfs_file_t *f; vnode_t *vn = net_sock_from_fd(t, (int)fd, &f, &r);
    if (!vn) return r;
    sock_addr_t a;
    r = net_parse_addr(uaddr, addrlen, &a);
    if (r == 0) r = unix_is_vnode(vn) ? unix_op_bind(vn, &a) : sock_op_bind(vn, &a);
    fd_put(f);
    return r;
}

int64_t sys_connect(uint64_t fd, uint64_t uaddr, uint64_t addrlen) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    int64_t r;
    vfs_file_t *f; vnode_t *vn = net_sock_from_fd(t, (int)fd, &f, &r);
    if (!vn) return r;
    int nonblock = (f->flags & O_NONBLOCK) ? 1 : 0;
    sock_addr_t a;
    r = net_parse_addr(uaddr, addrlen, &a);
    if (r == 0) r = unix_is_vnode(vn) ? unix_op_connect(vn, &a, nonblock) : sock_op_connect(vn, &a, nonblock);
    fd_put(f);
    return r;
}

static int64_t do_sendto(uint64_t fd, uint64_t ubuf, uint64_t len, int flags, uint64_t uaddr, uint64_t addrlen) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    int64_t r;
    vfs_file_t *f; vnode_t *vn = net_sock_from_fd(t, (int)fd, &f, &r);
    if (!vn) return r;
    if (f->flags & O_NONBLOCK) flags |= MSG_DONTWAIT;

    sock_addr_t to;
    const sock_addr_t *top = NULL;
    if (uaddr && addrlen && (unix_is_vnode(vn) || sock_type(vn) != SOCK_STREAM)) {
        r = net_parse_addr(uaddr, addrlen, &to);
        if (r < 0) { fd_put(f); return r; }
        top = &to;
    }
    r = net_send_user(vn, ubuf, (size_t)len, flags, top);
    net_sigpipe(vn, r, flags);
    fd_put(f);
    return r;
}

static int64_t do_recvfrom(uint64_t fd, uint64_t ubuf, uint64_t len, int flags, uint64_t uaddr, uint64_t uaddrlen) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    int64_t r;
    vfs_file_t *f; vnode_t *vn = net_sock_from_fd(t, (int)fd, &f, &r);
    if (!vn) return r;
    if (f->flags & O_NONBLOCK) flags |= MSG_DONTWAIT;

    sock_addr_t from;
    memset(&from, 0, sizeof from);
    int oflags = 0;
    r = net_recv_user(vn, ubuf, (size_t)len, flags, &from, &oflags);
    int stream = !unix_is_vnode(vn) && sock_type(vn) == SOCK_STREAM;
    fd_put(f);
    if (r < 0) return r;
    if (uaddr && uaddrlen) {
        if (stream || (from.family == AF_UNIX && from.ulen == 0)) {
            int zero = 0;
            if (syscall_copy_to_user((void *)uaddrlen, &zero, sizeof zero) < 0) return -EFAULT;
        } else {
            int64_t e = put_addr_user(&from, uaddr, uaddrlen);
            if (e < 0) return e;
        }
    }
    return r;
}

int64_t sys_sendto(uint64_t fd, uint64_t ubuf, uint64_t len, uint64_t uaddr, uint64_t addrlen) {
    return do_sendto(fd, ubuf, len, 0, uaddr, addrlen);
}

int64_t sys_recvfrom(uint64_t fd, uint64_t ubuf, uint64_t len, uint64_t uaddr, uint64_t uaddrlen) {
    return do_recvfrom(fd, ubuf, len, 0, uaddr, uaddrlen);
}

int64_t sys_sendto_flags(uint64_t fd, uint64_t ubuf, uint64_t len, uint64_t flags,
                         uint64_t uaddr, uint64_t addrlen) {
    return do_sendto(fd, ubuf, len, (int)flags, uaddr, addrlen);
}

int64_t sys_recvfrom_flags(uint64_t fd, uint64_t ubuf, uint64_t len, uint64_t flags,
                           uint64_t uaddr, uint64_t uaddrlen) {
    return do_recvfrom(fd, ubuf, len, (int)flags, uaddr, uaddrlen);
}

int64_t sys_listen(uint64_t fd, uint64_t backlog) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    int64_t r;
    vfs_file_t *f; vnode_t *vn = net_sock_from_fd(t, (int)fd, &f, &r);
    if (!vn) return r;
    r = unix_is_vnode(vn) ? unix_op_listen(vn, (int)backlog) : sock_op_listen(vn, (int)backlog);
    fd_put(f);
    return r;
}

int64_t sys_sock_shutdown(uint64_t fd, uint64_t how) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    int64_t r;
    vfs_file_t *f; vnode_t *vn = net_sock_from_fd(t, (int)fd, &f, &r);
    if (!vn) return r;
    if (how > 2) r = -EINVAL;
    else r = unix_is_vnode(vn) ? unix_op_shutdown(vn, (int)how) : sock_op_shutdown(vn, (int)how);
    fd_put(f);
    return r;
}

int64_t sys_setsockopt(uint64_t fd, uint64_t level, uint64_t optname,
                       uint64_t uval, uint64_t optlen) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    if (optlen > 128) optlen = 128;
    if (!uval && optlen) return -EFAULT;
    int64_t r;
    vfs_file_t *f; vnode_t *vn = net_sock_from_fd(t, (int)fd, &f, &r);
    if (!vn) return r;
    uint8_t kbuf[128];
    memset(kbuf, 0, sizeof kbuf);
    if (optlen && syscall_copy_from_user(kbuf, (const void *)uval, (size_t)optlen) < 0) {
        fd_put(f);
        return -EFAULT;
    }
    r = unix_is_vnode(vn) ? unix_op_setopt(vn, (int)level, (int)optname, kbuf, (uint32_t)optlen)
                          : sock_op_setopt(vn, (int)level, (int)optname, kbuf, (uint32_t)optlen);
    fd_put(f);
    return r;
}

int64_t sys_getsockopt(uint64_t fd, uint64_t level, uint64_t optname,
                       uint64_t uval, uint64_t uoptlen) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    if (!uval || !uoptlen) return -EFAULT;

    int icap = 0;
    if (syscall_copy_from_user(&icap, (const void *)uoptlen, sizeof icap) < 0) return -EFAULT;
    if (icap < 0) return -EINVAL;
    uint32_t cap = (uint32_t)icap > 128 ? 128 : (uint32_t)icap;

    int64_t r;
    vfs_file_t *f; vnode_t *vn = net_sock_from_fd(t, (int)fd, &f, &r);
    if (!vn) return r;

    uint8_t kbuf[128];
    memset(kbuf, 0, sizeof kbuf);
    uint32_t len = cap;
    if (unix_is_vnode(vn)) r = unix_op_getopt(vn, (int)level, (int)optname, kbuf, &len);
    else                   r = sock_op_getopt(vn, (int)level, (int)optname, kbuf, &len);
    fd_put(f);
    if (r < 0) return r;
    if (len > cap) len = cap;
    if (len && syscall_copy_to_user((void *)uval, kbuf, len) < 0) return -EFAULT;
    int l = (int)len;
    if (syscall_copy_to_user((void *)uoptlen, &l, sizeof l) < 0) return -EFAULT;
    return 0;
}

static int64_t do_accept(uint64_t fd, uint64_t uaddr, uint64_t uaddrlen, int flags) {
    if (flags & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) return -EINVAL;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    int64_t r;
    vfs_file_t *lf; vnode_t *lvn = net_sock_from_fd(t, (int)fd, &lf, &r);
    if (!lvn) return r;
    int nonblock = (lf->flags & O_NONBLOCK) ? 1 : 0;

    sock_addr_t peer;
    memset(&peer, 0, sizeof peer);
    vnode_t *cvn = NULL;
    r = unix_is_vnode(lvn) ? unix_op_accept(lvn, nonblock, &cvn, &peer)
                           : sock_op_accept(lvn, nonblock, &cvn, &peer);
    fd_put(lf);
    if (r < 0) return r;

    vfs_file_t *cf = vfs_file_alloc();
    if (!cf) { cvn->ops->unref(cvn); return -ENOMEM; }
    cf->vnode = cvn; cf->flags = O_RDWR; cf->offset = 0; cf->refcount = 1;
    if (flags & SOCK_NONBLOCK) cf->flags |= O_NONBLOCK;
    int cfd = fd_alloc(t->fd_table, cf, 0);
    if (cfd < 0) { vfs_file_free(cf); return -EMFILE; }
    if (flags & SOCK_CLOEXEC) fd_set_flags(t->fd_table, cfd, FD_CLOEXEC);

    if (uaddr && uaddrlen) {
        int64_t e = put_addr_user(&peer, uaddr, uaddrlen);
        if (e < 0) return e;
    }
    return cfd;
}

int64_t sys_accept(uint64_t fd, uint64_t uaddr, uint64_t uaddrlen) {
    return do_accept(fd, uaddr, uaddrlen, 0);
}

int64_t sys_accept4(uint64_t fd, uint64_t uaddr, uint64_t uaddrlen, uint64_t flags) {
    return do_accept(fd, uaddr, uaddrlen, (int)flags);
}

int64_t sys_sendfd(uint64_t sockfd, uint64_t passfd) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    int64_t r;
    vfs_file_t *sf; vnode_t *vn = net_sock_from_fd(t, (int)sockfd, &sf, &r);
    if (!vn) return r;
    if (!unix_is_vnode(vn)) { fd_put(sf); return -EINVAL; }
    vfs_file_t *pf = fd_get(t->fd_table, (int)passfd);
    if (!pf) { fd_put(sf); return -EBADF; }
    r = unix_send_fd(vn, pf);
    if (r < 0) fd_put(pf);
    fd_put(sf);
    return r;
}

int64_t sys_recvfd(uint64_t sockfd) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    int64_t r;
    vfs_file_t *sf; vnode_t *vn = net_sock_from_fd(t, (int)sockfd, &sf, &r);
    if (!vn) return r;
    if (!unix_is_vnode(vn)) { fd_put(sf); return -EINVAL; }
    int nonblock = (sf->flags & O_NONBLOCK) ? 1 : 0;
    vfs_file_t *pf = unix_recv_fd(vn, nonblock);
    fd_put(sf);
    if (!pf) return nonblock ? -EAGAIN : -EINVAL;
    int nfd = fd_alloc(t->fd_table, pf, 0);
    if (nfd < 0) { fd_put(pf); return -EMFILE; }
    return nfd;
}

int64_t sys_net_ifcfg(uint64_t index, uint64_t ubuf, uint64_t ubuf_len) {
    size_t n = (size_t)ubuf_len;
    if (n == 0 || n > sizeof(net_ifcfg_t)) n = sizeof(net_ifcfg_t);
    if (!syscall_uptr_validate_write((void *)ubuf, n)) return -EFAULT;
    net_ifcfg_t cfg;
    if (net_ifcfg_get((int)index, &cfg) != 0) return -1;
    memcpy((void *)ubuf, &cfg, n);
    return 0;
}

int64_t sys_net_ifset(uint64_t index, uint64_t ip, uint64_t netmask, uint64_t gateway, uint64_t dns) {
    return net_ifcfg_set((int)index, (uint32_t)ip, (uint32_t)netmask, (uint32_t)gateway, (uint32_t)dns);
}

static int64_t sock_name(uint64_t fd, uint64_t uaddr, uint64_t ulen, int peer) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    if (!uaddr || !ulen) return -EFAULT;
    int cap;
    if (syscall_copy_from_user(&cap, (const void *)ulen, sizeof cap) < 0) return -EFAULT;
    if (cap < 0) return -EINVAL;
    int64_t r;
    vfs_file_t *f; vnode_t *vn = net_sock_from_fd(t, (int)fd, &f, &r);
    if (!vn) return r;
    sock_addr_t a;
    r = unix_is_vnode(vn) ? unix_get_name(vn, peer, &a) : sock_get_name(vn, peer, &a);
    fd_put(f);
    if (r < 0) return r;
    uint32_t len = 0;
    r = net_put_addr(&a, uaddr, (uint32_t)cap, &len);
    if (r < 0) return r;
    int l = (int)len;
    if (syscall_copy_to_user((void *)ulen, &l, sizeof l) < 0) return -EFAULT;
    return 0;
}

int64_t sys_getsockname(uint64_t fd, uint64_t uaddr, uint64_t ulen) { return sock_name(fd, uaddr, ulen, 0); }
int64_t sys_getpeername(uint64_t fd, uint64_t uaddr, uint64_t ulen) { return sock_name(fd, uaddr, ulen, 1); }
