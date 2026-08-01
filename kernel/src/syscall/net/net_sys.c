#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/net/socket.h"
#include "../../../include/net/net.h"
#include "../../../include/net/netdev.h"
#include <string.h>

#define SOCK_IO_MAX 8192

static vnode_t *sock_vnode_from_fd(task_t *t, int fd, vfs_file_t **out_file) {
    vfs_file_t *f = fd_get(t->fd_table, fd);
    if (!f) return NULL;
    if (!sock_is_vnode(f->vnode)) { fd_put(f); return NULL; }
    *out_file = f;
    return f->vnode;
}

static int parse_sockaddr(uint64_t uaddr, uint32_t *ip, uint16_t *port) {
    if (!uaddr) return -1;
    if (!syscall_uptr_validate((void *)uaddr, 16)) return -2;
    uint8_t sa[16];
    memcpy(sa, (void *)uaddr, 16);
    *port = rd16be(sa + 2);
    *ip   = rd32be(sa + 4);
    return 0;
}

int64_t sys_socket(uint64_t domain, uint64_t type, uint64_t proto) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -ENOMEM;
    vnode_t *vn = sock_new_vnode((int)domain, (int)type, (int)proto);
    if (!vn) return -EINVAL;
    vfs_file_t *f = vfs_file_alloc();
    if (!f) { vn->ops->unref(vn); return -ENOMEM; }
    f->vnode = vn; f->flags = O_RDWR; f->offset = 0; f->refcount = 1;
    int fd = fd_alloc(t->fd_table, f, 0);
    if (fd < 0) { vfs_file_free(f); return -EMFILE; }
    return fd;
}

int64_t sys_bind(uint64_t fd, uint64_t uaddr, uint64_t addrlen) {
    (void)addrlen;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    uint32_t ip; uint16_t port;
    if (parse_sockaddr(uaddr, &ip, &port) < 0) return -EINVAL;
    vfs_file_t *f; vnode_t *vn = sock_vnode_from_fd(t, (int)fd, &f);
    if (!vn) return -EBADF;
    int64_t r = sock_op_bind(vn, ip, port);
    fd_put(f);
    return r;
}

int64_t sys_connect(uint64_t fd, uint64_t uaddr, uint64_t addrlen) {
    (void)addrlen;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    uint32_t ip; uint16_t port;
    if (parse_sockaddr(uaddr, &ip, &port) < 0) return -EINVAL;
    vfs_file_t *f; vnode_t *vn = sock_vnode_from_fd(t, (int)fd, &f);
    if (!vn) return -EBADF;
    int64_t r = sock_op_connect(vn, ip, port);
    fd_put(f);
    return r;
}

int64_t sys_sendto(uint64_t fd, uint64_t ubuf, uint64_t len, uint64_t uaddr, uint64_t addrlen) {
    (void)addrlen;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    if (len > SOCK_IO_MAX) len = SOCK_IO_MAX;
    if (len && !syscall_uptr_validate((void *)ubuf, len)) return -EFAULT;

    uint8_t kbuf[SOCK_IO_MAX];
    if (len) memcpy(kbuf, (void *)ubuf, (size_t)len);

    uint32_t ip = 0; uint16_t port = 0;
    if (uaddr && parse_sockaddr(uaddr, &ip, &port) < 0) return -EINVAL;

    vfs_file_t *f; vnode_t *vn = sock_vnode_from_fd(t, (int)fd, &f);
    if (!vn) return -EBADF;
    int64_t r = sock_op_sendto(vn, kbuf, (size_t)len, ip, port);
    fd_put(f);
    return r;
}

int64_t sys_recvfrom(uint64_t fd, uint64_t ubuf, uint64_t len, uint64_t uaddr, uint64_t uaddrlen) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    if (len > SOCK_IO_MAX) len = SOCK_IO_MAX;
    if (len && !syscall_uptr_validate((void *)ubuf, len)) return -EFAULT;

    vfs_file_t *f; vnode_t *vn = sock_vnode_from_fd(t, (int)fd, &f);
    if (!vn) return -EBADF;
    int nonblock = (f->flags & O_NONBLOCK) ? 1 : 0;

    uint8_t kbuf[SOCK_IO_MAX];
    uint32_t sip = 0; uint16_t sport = 0;
    int64_t r = sock_op_recvfrom(vn, kbuf, (size_t)len, nonblock, &sip, &sport);
    fd_put(f);

    if (r > 0) {
        memcpy((void *)ubuf, kbuf, (size_t)r);
        if (uaddr && syscall_uptr_validate((void *)uaddr, 16)) {
            uint8_t sa[16];
            memset(sa, 0, sizeof(sa));
            sa[0] = AF_INET;
            wr16be(sa + 2, sport);
            wr32be(sa + 4, sip);
            memcpy((void *)uaddr, sa, 16);
            if (uaddrlen && syscall_uptr_validate((void *)uaddrlen, sizeof(int))) {
                int sl = 16;
                memcpy((void *)uaddrlen, &sl, sizeof(int));
            }
        }
    }
    return r;
}

int64_t sys_listen(uint64_t fd, uint64_t backlog) {
    (void)backlog;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f; vnode_t *vn = sock_vnode_from_fd(t, (int)fd, &f);
    if (!vn) return -EBADF;
    int64_t r = sock_op_listen(vn);
    fd_put(f);
    return r;
}

int64_t sys_accept(uint64_t fd, uint64_t uaddr, uint64_t uaddrlen) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *lf; vnode_t *lvn = sock_vnode_from_fd(t, (int)fd, &lf);
    if (!lvn) return -EBADF;
    int nonblock = (lf->flags & O_NONBLOCK) ? 1 : 0;

    uint32_t rip = 0; uint16_t rport = 0;
    vnode_t *cvn = sock_op_accept(lvn, nonblock, &rip, &rport);
    fd_put(lf);
    if (!cvn) return nonblock ? -EAGAIN : -EINVAL;

    vfs_file_t *cf = vfs_file_alloc();
    if (!cf) { cvn->ops->unref(cvn); return -ENOMEM; }
    cf->vnode = cvn; cf->flags = O_RDWR; cf->offset = 0; cf->refcount = 1;
    int cfd = fd_alloc(t->fd_table, cf, 0);
    if (cfd < 0) { vfs_file_free(cf); return -EMFILE; }

    if (uaddr && syscall_uptr_validate((void *)uaddr, 16)) {
        uint8_t sa[16];
        memset(sa, 0, sizeof(sa));
        sa[0] = AF_INET;
        wr16be(sa + 2, rport);
        wr32be(sa + 4, rip);
        memcpy((void *)uaddr, sa, 16);
        if (uaddrlen && syscall_uptr_validate((void *)uaddrlen, sizeof(int))) {
            int sl = 16;
            memcpy((void *)uaddrlen, &sl, sizeof(int));
        }
    }
    return cfd;
}

int64_t sys_net_ifcfg(uint64_t index, uint64_t ubuf) {
    if (!syscall_uptr_validate((void *)ubuf, sizeof(net_ifcfg_t))) return -EFAULT;
    net_ifcfg_t cfg;
    if (net_ifcfg_get((int)index, &cfg) != 0) return -1;
    memcpy((void *)ubuf, &cfg, sizeof(cfg));
    return 0;
}

int64_t sys_net_ifset(uint64_t index, uint64_t ip, uint64_t netmask, uint64_t gateway, uint64_t dns) {
    return net_ifcfg_set((int)index, (uint32_t)ip, (uint32_t)netmask, (uint32_t)gateway, (uint32_t)dns);
}
