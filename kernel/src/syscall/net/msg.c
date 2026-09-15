#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/fs/vfs.h"
#include <string.h>

#define SCM_RIGHTS  1
#define SOL_SOCKET  1

#define MSG_DONTWAIT 0x40

#define MSG_IOV_MAX 8
#define MSG_FDS_MAX 8

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

extern int64_t unix_send_fd(vnode_t *vn, vfs_file_t *file);
extern vfs_file_t *unix_recv_fd(vnode_t *vn, int nonblock);
extern int     unix_is_socket(const vnode_t *vn);
extern int64_t unix_read_nb(vnode_t *vn, void *buf, size_t len, int nonblock);
extern int64_t unix_write_nb(vnode_t *vn, const void *buf, size_t len, int nonblock);

static int cmsg_space(int nfds)
{
    size_t raw = sizeof(struct k_cmsghdr) + (size_t)nfds * sizeof(int);
    return (int)((raw + 7u) & ~7u);
}

int64_t sys_sendmsg(uint64_t fd, uint64_t msg_ptr, uint64_t flags)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;

    vfs_file_t *sock = fd_get(t->fd_table, (int)fd);
    if (!sock || !sock->vnode) return -EBADF;

    int nonblock = ((flags & MSG_DONTWAIT) || (sock->flags & O_NONBLOCK)) ? 1 : 0;
    int is_unix  = unix_is_socket(sock->vnode);

    struct k_msghdr msg;
    if (syscall_copy_from_user(&msg, (void *)msg_ptr, sizeof msg) < 0) return -EFAULT;
    if (msg.msg_iovlen > MSG_IOV_MAX) return -EINVAL;

    if (msg.msg_control && msg.msg_controllen >= sizeof(struct k_cmsghdr)) {
        if (!unix_is_socket(sock->vnode)) return -EOPNOTSUPP;

        uint8_t ctl[256];
        size_t clen = msg.msg_controllen;
        if (clen > sizeof ctl) clen = sizeof ctl;
        if (syscall_copy_from_user(ctl, msg.msg_control, clen) < 0) return -EFAULT;

        struct k_cmsghdr *cm = (struct k_cmsghdr *)ctl;
        if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS) {
            size_t payload = cm->cmsg_len > sizeof(struct k_cmsghdr)
                           ? cm->cmsg_len - sizeof(struct k_cmsghdr) : 0;
            int nfds = (int)(payload / sizeof(int));
            if (nfds > MSG_FDS_MAX) nfds = MSG_FDS_MAX;

            const int *fds = (const int *)(ctl + sizeof(struct k_cmsghdr));
            for (int i = 0; i < nfds; i++) {
                vfs_file_t *pass = fd_get(t->fd_table, fds[i]);
                if (!pass) return -EBADF;
                int64_t r = unix_send_fd(sock->vnode, pass);
                if (r < 0) return r;
            }
        }
    }

    struct k_iovec iov[MSG_IOV_MAX];
    if (msg.msg_iovlen) {
        if (syscall_copy_from_user(iov, msg.msg_iov,
                                   msg.msg_iovlen * sizeof(struct k_iovec)) < 0)
            return -EFAULT;
    }

    int64_t total = 0;
    for (size_t i = 0; i < msg.msg_iovlen; i++) {
        if (!iov[i].iov_len) continue;
        if (!syscall_uptr_validate(iov[i].iov_base, iov[i].iov_len)) return -EFAULT;
        int64_t w = is_unix ? unix_write_nb(sock->vnode, iov[i].iov_base, iov[i].iov_len, nonblock)
                            : vfs_write(sock, iov[i].iov_base, iov[i].iov_len);
        if (w < 0) return total ? total : w;
        total += w;
        if ((size_t)w < iov[i].iov_len) break;
    }
    return total;
}

int64_t sys_recvmsg(uint64_t fd, uint64_t msg_ptr, uint64_t flags)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;

    vfs_file_t *sock = fd_get(t->fd_table, (int)fd);
    if (!sock || !sock->vnode) return -EBADF;

    int nonblock = ((flags & MSG_DONTWAIT) || (sock->flags & O_NONBLOCK)) ? 1 : 0;
    int is_unix  = unix_is_socket(sock->vnode);

    struct k_msghdr msg;
    if (syscall_copy_from_user(&msg, (void *)msg_ptr, sizeof msg) < 0) return -EFAULT;
    if (msg.msg_iovlen > MSG_IOV_MAX) return -EINVAL;

    size_t got_control = 0;
    if (msg.msg_control && msg.msg_controllen >= (size_t)cmsg_space(1) && is_unix) {
        int fds[MSG_FDS_MAX];
        int nfds = 0;
        size_t room = (msg.msg_controllen - sizeof(struct k_cmsghdr)) / sizeof(int);
        if (room > MSG_FDS_MAX) room = MSG_FDS_MAX;

        while ((size_t)nfds < room) {
            vfs_file_t *passed = unix_recv_fd(sock->vnode, 1);
            if (!passed) break;
            int nfd = fd_alloc(t->fd_table, passed, 0);
            if (nfd < 0) { vfs_file_free(passed); break; }
            fds[nfds++] = nfd;
        }

        if (nfds > 0) {
            uint8_t ctl[256];
            memset(ctl, 0, sizeof ctl);
            struct k_cmsghdr *cm = (struct k_cmsghdr *)ctl;
            cm->cmsg_len   = sizeof(struct k_cmsghdr) + (size_t)nfds * sizeof(int);
            cm->cmsg_level = SOL_SOCKET;
            cm->cmsg_type  = SCM_RIGHTS;
            memcpy(ctl + sizeof(struct k_cmsghdr), fds, (size_t)nfds * sizeof(int));
            got_control = (size_t)cmsg_space(nfds);
            if (got_control > msg.msg_controllen) got_control = msg.msg_controllen;
            if (syscall_copy_to_user(msg.msg_control, ctl, got_control) < 0)
                return -EFAULT;
        }
    }

    struct k_iovec iov[MSG_IOV_MAX];
    if (msg.msg_iovlen) {
        if (syscall_copy_from_user(iov, msg.msg_iov,
                                   msg.msg_iovlen * sizeof(struct k_iovec)) < 0)
            return -EFAULT;
    }

    int64_t total = 0;
    for (size_t i = 0; i < msg.msg_iovlen; i++) {
        if (!iov[i].iov_len) continue;
        if (!syscall_uptr_validate(iov[i].iov_base, iov[i].iov_len)) return -EFAULT;
        int64_t r = is_unix ? unix_read_nb(sock->vnode, iov[i].iov_base, iov[i].iov_len, nonblock)
                            : vfs_read(sock, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) {
            if (total) break;
            return r;
        }
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }

    struct k_msghdr back = msg;
    back.msg_controllen = got_control;
    back.msg_flags = 0;
    if (syscall_copy_to_user((void *)msg_ptr, &back, sizeof back) < 0) return -EFAULT;

    return total;
}
