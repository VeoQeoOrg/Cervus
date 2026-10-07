#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/net/socket.h"
#include <string.h>
#include <stdlib.h>

#define SCM_RIGHTS  1
#define SOL_SOCKET  1

#define MSG_IOV_MAX 1024
#define MSG_IOV_STACK 16
#define MSG_FDS_MAX 8
#define DGRAM_MAX   65535

struct k_iovec {
    void  *iov_base;
    size_t iov_len;
};

struct k_msghdr {
    void           *msg_name;
    unsigned int    msg_namelen;
    struct k_iovec *msg_iov;
    size_t          msg_iovlen;
    void           *msg_control;
    size_t          msg_controllen;
    int             msg_flags;
};

struct k_cmsghdr {
    size_t cmsg_len;
    int    cmsg_level;
    int    cmsg_type;
};

extern int unix_is_socket(const vnode_t *vn);

static int cmsg_space(int nfds)
{
    size_t raw = sizeof(struct k_cmsghdr) + (size_t)nfds * sizeof(int);
    return (int)((raw + 7u) & ~7u);
}

static struct k_iovec *load_iov(const struct k_msghdr *msg, struct k_iovec *stack, size_t *total, int64_t *err)
{
    *err = 0;
    *total = 0;
    if (msg->msg_iovlen > MSG_IOV_MAX) { *err = -EMSGSIZE; return NULL; }
    struct k_iovec *iov = stack;
    if (msg->msg_iovlen > MSG_IOV_STACK) {
        iov = malloc(msg->msg_iovlen * sizeof(*iov));
        if (!iov) { *err = -ENOMEM; return NULL; }
    }
    if (msg->msg_iovlen &&
        syscall_copy_from_user(iov, msg->msg_iov, msg->msg_iovlen * sizeof(*iov)) < 0) {
        if (iov != stack) free(iov);
        *err = -EFAULT;
        return NULL;
    }
    size_t sum = 0;
    for (size_t i = 0; i < msg->msg_iovlen; i++) {
        if ((int64_t)iov[i].iov_len < 0 || sum + iov[i].iov_len < sum) {
            if (iov != stack) free(iov);
            *err = -EINVAL;
            return NULL;
        }
        sum += iov[i].iov_len;
    }
    *total = sum;
    return iov;
}

static int64_t send_unix_rights(task_t *t, vnode_t *vn, const struct k_msghdr *msg)
{
    uint8_t ctl[256];
    size_t clen = msg->msg_controllen;
    if (clen > sizeof ctl) clen = sizeof ctl;
    if (syscall_copy_from_user(ctl, msg->msg_control, clen) < 0) return -EFAULT;

    struct k_cmsghdr *cm = (struct k_cmsghdr *)ctl;
    if (cm->cmsg_level != SOL_SOCKET || cm->cmsg_type != SCM_RIGHTS) return 0;
    size_t payload = cm->cmsg_len > sizeof(struct k_cmsghdr)
                   ? cm->cmsg_len - sizeof(struct k_cmsghdr) : 0;
    int nfds = (int)(payload / sizeof(int));
    if (nfds > MSG_FDS_MAX) nfds = MSG_FDS_MAX;

    const int *fds = (const int *)(ctl + sizeof(struct k_cmsghdr));
    for (int i = 0; i < nfds; i++) {
        vfs_file_t *pass = fd_get(t->fd_table, fds[i]);
        if (!pass) return -EBADF;
        int64_t r = unix_send_fd(vn, pass);
        if (r < 0) { fd_put(pass); return r; }
    }
    return 0;
}

static int64_t do_sendmsg(task_t *t, vfs_file_t *sock, uint64_t msg_ptr, int flags)
{
    vnode_t *vn = sock->vnode;
    if (!sock_is_vnode(vn) && !unix_is_vnode(vn)) return -ENOTSOCK;
    if (sock->flags & O_NONBLOCK) flags |= MSG_DONTWAIT;
    int is_unix = unix_is_socket(vn);

    struct k_msghdr msg;
    if (syscall_copy_from_user(&msg, (void *)msg_ptr, sizeof msg) < 0) return -EFAULT;

    if (msg.msg_control && msg.msg_controllen >= sizeof(struct k_cmsghdr)) {
        if (!is_unix) return -EINVAL;
        int64_t r = send_unix_rights(t, vn, &msg);
        if (r < 0) return r;
    }

    struct k_iovec stack[MSG_IOV_STACK];
    size_t total_len;
    int64_t err;
    struct k_iovec *iov = load_iov(&msg, stack, &total_len, &err);
    if (!iov) return err;

    int64_t r;
    if (!is_unix && sock_type(vn) != SOCK_STREAM) {
        sock_addr_t to;
        const sock_addr_t *top = NULL;
        r = 0;
        if (msg.msg_name && msg.msg_namelen) {
            r = net_parse_addr((uint64_t)msg.msg_name, msg.msg_namelen, &to);
            top = &to;
        }
        if (r == 0 && total_len > DGRAM_MAX) r = -EMSGSIZE;
        uint8_t *kb = NULL;
        if (r == 0) {
            kb = malloc(total_len ? total_len : 1);
            if (!kb) r = -ENOMEM;
        }
        size_t off = 0;
        for (size_t i = 0; r == 0 && i < msg.msg_iovlen; i++) {
            if (!iov[i].iov_len) continue;
            if (syscall_copy_from_user(kb + off, iov[i].iov_base, iov[i].iov_len) < 0) r = -EFAULT;
            off += iov[i].iov_len;
        }
        if (r == 0) r = sock_op_sendto(vn, kb, total_len, flags, top);
        free(kb);
    } else {
        int64_t total = 0;
        r = 0;
        for (size_t i = 0; i < msg.msg_iovlen; i++) {
            if (!iov[i].iov_len) continue;
            int64_t w = net_send_user(vn, (uint64_t)iov[i].iov_base, iov[i].iov_len, flags, NULL);
            if (w < 0) { r = w; break; }
            total += w;
            if ((size_t)w < iov[i].iov_len) break;
        }
        if (total) r = total;
    }
    if (iov != stack) free(iov);
    net_sigpipe(vn, r, flags);
    return r;
}

static int64_t recv_unix_rights(task_t *t, vnode_t *vn, struct k_msghdr *msg, size_t *got_control)
{
    *got_control = 0;
    int fds[MSG_FDS_MAX];
    int nfds = 0;
    size_t room = (msg->msg_controllen - sizeof(struct k_cmsghdr)) / sizeof(int);
    if (room > MSG_FDS_MAX) room = MSG_FDS_MAX;

    while ((size_t)nfds < room) {
        vfs_file_t *passed = unix_recv_fd(vn, 1);
        if (!passed) break;
        int nfd = fd_alloc(t->fd_table, passed, 0);
        if (nfd < 0) { vfs_file_free(passed); break; }
        fds[nfds++] = nfd;
    }
    if (nfds == 0) return 0;

    uint8_t ctl[256];
    memset(ctl, 0, sizeof ctl);
    struct k_cmsghdr *cm = (struct k_cmsghdr *)ctl;
    cm->cmsg_len   = sizeof(struct k_cmsghdr) + (size_t)nfds * sizeof(int);
    cm->cmsg_level = SOL_SOCKET;
    cm->cmsg_type  = SCM_RIGHTS;
    memcpy(ctl + sizeof(struct k_cmsghdr), fds, (size_t)nfds * sizeof(int));
    size_t n = (size_t)cmsg_space(nfds);
    if (n > msg->msg_controllen) n = msg->msg_controllen;
    if (syscall_copy_to_user(msg->msg_control, ctl, n) < 0) return -EFAULT;
    *got_control = n;
    return 0;
}

static int64_t do_recvmsg(task_t *t, vfs_file_t *sock, uint64_t msg_ptr, int flags)
{
    vnode_t *vn = sock->vnode;
    if (!sock_is_vnode(vn) && !unix_is_vnode(vn)) return -ENOTSOCK;
    if (sock->flags & O_NONBLOCK) flags |= MSG_DONTWAIT;
    int is_unix = unix_is_socket(vn);

    struct k_msghdr msg;
    if (syscall_copy_from_user(&msg, (void *)msg_ptr, sizeof msg) < 0) return -EFAULT;

    size_t got_control = 0;
    if (is_unix && msg.msg_control && msg.msg_controllen >= (size_t)cmsg_space(1)) {
        int64_t r = recv_unix_rights(t, vn, &msg, &got_control);
        if (r < 0) return r;
    }

    struct k_iovec stack[MSG_IOV_STACK];
    size_t total_len;
    int64_t err;
    struct k_iovec *iov = load_iov(&msg, stack, &total_len, &err);
    if (!iov) return err;

    int oflags = 0;
    unsigned int namelen = 0;
    int64_t r;
    if (!is_unix && sock_type(vn) != SOCK_STREAM) {
        sock_addr_t from;
        memset(&from, 0, sizeof from);
        size_t cap = total_len > DGRAM_MAX ? DGRAM_MAX : total_len;
        uint8_t *kb = malloc(cap ? cap : 1);
        if (!kb) {
            r = -ENOMEM;
        } else {
            r = sock_op_recvfrom(vn, kb, cap, flags, &from, &oflags);
            if (r >= 0) {
                size_t left = (size_t)r < cap ? (size_t)r : cap;
                size_t off = 0;
                for (size_t i = 0; left && i < msg.msg_iovlen; i++) {
                    size_t n = iov[i].iov_len < left ? iov[i].iov_len : left;
                    if (n && syscall_copy_to_user(iov[i].iov_base, kb + off, n) < 0) { r = -EFAULT; break; }
                    off += n;
                    left -= n;
                }
                if (r >= 0 && msg.msg_name && msg.msg_namelen) {
                    uint32_t outlen = 0;
                    int64_t e = net_put_addr(&from, (uint64_t)msg.msg_name, msg.msg_namelen, &outlen);
                    if (e < 0) r = e;
                    namelen = outlen;
                }
            }
            free(kb);
        }
    } else {
        int64_t total = 0;
        r = 0;
        int fl = flags;
        for (size_t i = 0; i < msg.msg_iovlen; i++) {
            if (!iov[i].iov_len) continue;
            int64_t g = net_recv_user(vn, (uint64_t)iov[i].iov_base, iov[i].iov_len, fl, NULL, NULL);
            if (g < 0) { if (!total) r = g; break; }
            total += g;
            if ((size_t)g < iov[i].iov_len || g == 0 || (fl & MSG_PEEK)) break;
            if (!(fl & MSG_WAITALL)) fl |= MSG_DONTWAIT;
        }
        if (r == 0) r = total;
    }
    if (iov != stack) free(iov);
    if (r < 0) return r;

    struct k_msghdr back = msg;
    back.msg_controllen = got_control;
    back.msg_namelen = namelen;
    back.msg_flags = oflags;
    if (syscall_copy_to_user((void *)msg_ptr, &back, sizeof back) < 0) return -EFAULT;
    return r;
}

int64_t sys_sendmsg(uint64_t fd, uint64_t msg_ptr, uint64_t flags)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;
    vfs_file_t *sock = fd_get(t->fd_table, (int)fd);
    if (!sock) return -EBADF;
    int64_t r = sock->vnode ? do_sendmsg(t, sock, msg_ptr, (int)flags) : -EBADF;
    fd_put(sock);
    return r;
}

int64_t sys_recvmsg(uint64_t fd, uint64_t msg_ptr, uint64_t flags)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;
    vfs_file_t *sock = fd_get(t->fd_table, (int)fd);
    if (!sock) return -EBADF;
    int64_t r = sock->vnode ? do_recvmsg(t, sock, msg_ptr, (int)flags) : -EBADF;
    fd_put(sock);
    return r;
}
