#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/net/socket.h"
#include <string.h>
#include <stdlib.h>

#define SCM_RIGHTS      1
#define SCM_CREDENTIALS 2
#define SOL_SOCKET      1
#define CMSG_HDR        16
#define CMSG_ALIGN8(n)  (((n) + 7u) & ~(size_t)7u)
#define UNIX_CHUNK      (256u * 1024u)

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

static void anc_drop(unix_anc_t *anc)
{
    for (int i = 0; i < anc->nfds; i++) if (anc->fds[i]) fd_put(anc->fds[i]);
    anc->nfds = 0;
}

static int64_t parse_control(task_t *t, const struct k_msghdr *msg, unix_anc_t *anc)
{
    if (!msg->msg_control || msg->msg_controllen < sizeof(struct k_cmsghdr)) return 0;
    size_t clen = msg->msg_controllen;
    if (clen > 4096) return -ENOBUFS;
    uint8_t *ctl = malloc(clen);
    if (!ctl) return -ENOMEM;
    if (syscall_copy_from_user(ctl, msg->msg_control, clen) < 0) { free(ctl); return -EFAULT; }

    int64_t r = 0;
    size_t off = 0;
    while (off + sizeof(struct k_cmsghdr) <= clen) {
        struct k_cmsghdr *cm = (struct k_cmsghdr *)(ctl + off);
        if (cm->cmsg_len < sizeof(struct k_cmsghdr) || off + cm->cmsg_len > clen) { r = -EINVAL; break; }
        size_t payload = cm->cmsg_len - CMSG_HDR;
        const uint8_t *data = ctl + off + CMSG_HDR;
        if (cm->cmsg_level != SOL_SOCKET) { r = -EINVAL; break; }
        if (cm->cmsg_type == SCM_RIGHTS) {
            int n = (int)(payload / sizeof(int));
            if (anc->nfds + n > UNIX_MAX_FDS) { r = -EINVAL; break; }
            for (int i = 0; i < n; i++) {
                int fd;
                memcpy(&fd, data + (size_t)i * sizeof(int), sizeof fd);
                vfs_file_t *f = fd_get(t->fd_table, fd);
                if (!f) { r = -EBADF; break; }
                anc->fds[anc->nfds++] = f;
            }
            if (r < 0) break;
        } else if (cm->cmsg_type == SCM_CREDENTIALS) {
            if (payload < 12) { r = -EINVAL; break; }
            uint32_t cr[3];
            memcpy(cr, data, sizeof cr);
            if (t->uid != 0 && (cr[0] != t->pid || cr[1] != t->uid || cr[2] != t->gid)) { r = -EPERM; break; }
            anc->has_creds = 1;
            anc->pid = cr[0]; anc->uid = cr[1]; anc->gid = cr[2];
        } else {
            r = -EINVAL;
            break;
        }
        off += CMSG_ALIGN8(cm->cmsg_len);
    }
    free(ctl);
    if (r < 0) anc_drop(anc);
    return r;
}

static size_t put_control(task_t *t, const struct k_msghdr *msg, unix_anc_t *anc, int flags, int *oflags)
{
    size_t cap = msg->msg_control ? msg->msg_controllen : 0;
    uint8_t ctl[4096];
    if (cap > sizeof ctl) cap = sizeof ctl;
    size_t used = 0;

    if (anc->has_creds) {
        size_t need = CMSG_ALIGN8(CMSG_HDR + 12);
        if (used + CMSG_HDR + 12 <= cap) {
            struct k_cmsghdr *cm = (struct k_cmsghdr *)(ctl + used);
            memset(cm, 0, need);
            cm->cmsg_len = CMSG_HDR + 12;
            cm->cmsg_level = SOL_SOCKET;
            cm->cmsg_type = SCM_CREDENTIALS;
            uint32_t cr[3] = { anc->pid, anc->uid, anc->gid };
            memcpy(ctl + used + CMSG_HDR, cr, sizeof cr);
            used += need <= cap - used ? need : cap - used;
        } else {
            *oflags |= MSG_CTRUNC;
        }
    }

    if (anc->nfds > 0) {
        int fit = 0;
        if (used + CMSG_HDR + sizeof(int) <= cap) fit = (int)((cap - used - CMSG_HDR) / sizeof(int));
        if (fit > anc->nfds) fit = anc->nfds;
        int installed = 0;
        int fds[UNIX_MAX_FDS];
        for (int i = 0; i < fit; i++) {
            int nfd = fd_alloc(t->fd_table, anc->fds[i], 0);
            if (nfd < 0) break;
            anc->fds[i] = NULL;
            if (flags & MSG_CMSG_CLOEXEC) fd_set_flags(t->fd_table, nfd, FD_CLOEXEC);
            fds[installed++] = nfd;
        }
        if (installed < anc->nfds) *oflags |= MSG_CTRUNC;
        for (int i = 0; i < anc->nfds; i++) if (anc->fds[i]) { fd_put(anc->fds[i]); anc->fds[i] = NULL; }
        anc->nfds = 0;
        if (installed) {
            struct k_cmsghdr *cm = (struct k_cmsghdr *)(ctl + used);
            size_t clen = CMSG_HDR + (size_t)installed * sizeof(int);
            memset(cm, 0, CMSG_HDR);
            cm->cmsg_len = clen;
            cm->cmsg_level = SOL_SOCKET;
            cm->cmsg_type = SCM_RIGHTS;
            memcpy(ctl + used + CMSG_HDR, fds, (size_t)installed * sizeof(int));
            size_t step = CMSG_ALIGN8(clen);
            if (step > cap - used) step = cap - used;
            if (step > clen) memset(ctl + used + clen, 0, step - clen);
            used += step;
        }
    }
    if (used && syscall_copy_to_user(msg->msg_control, ctl, used) < 0) return 0;
    return used;
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

static int64_t do_sendmsg(task_t *t, vfs_file_t *sock, uint64_t msg_ptr, int flags)
{
    vnode_t *vn = sock->vnode;
    if (!sock_is_vnode(vn) && !unix_is_vnode(vn)) return -ENOTSOCK;
    if (sock->flags & O_NONBLOCK) flags |= MSG_DONTWAIT;
    int is_unix = unix_is_vnode(vn);

    struct k_msghdr msg;
    if (syscall_copy_from_user(&msg, (void *)msg_ptr, sizeof msg) < 0) return -EFAULT;
    if (!is_unix && msg.msg_control && msg.msg_controllen >= sizeof(struct k_cmsghdr)) return -EINVAL;

    struct k_iovec stack[MSG_IOV_STACK];
    size_t total_len;
    int64_t err;
    struct k_iovec *iov = load_iov(&msg, stack, &total_len, &err);
    if (!iov) return err;

    int64_t r;
    if (is_unix) {
        sock_addr_t to;
        const sock_addr_t *top = NULL;
        unix_anc_t *anc = calloc(1, sizeof(*anc));
        r = anc ? 0 : -ENOMEM;
        if (r == 0 && msg.msg_name && msg.msg_namelen) {
            r = net_parse_addr((uint64_t)msg.msg_name, msg.msg_namelen, &to);
            top = &to;
        }
        if (r == 0) r = parse_control(t, &msg, anc);
        int stream = r == 0 && unix_sock_type(vn) == SOCK_STREAM;
        size_t cap = stream ? (total_len < UNIX_CHUNK ? total_len : UNIX_CHUNK) : total_len;
        uint8_t *kb = NULL;
        if (r == 0) {
            kb = malloc(cap ? cap : 1);
            if (!kb) r = -ENOMEM;
        }
        int64_t total = 0;
        size_t vi = 0, voff = 0;
        while (r == 0) {
            size_t n = 0;
            while (n < cap && vi < msg.msg_iovlen) {
                size_t take = iov[vi].iov_len - voff;
                if (take > cap - n) take = cap - n;
                if (take && syscall_copy_from_user(kb + n, (uint8_t *)iov[vi].iov_base + voff, take) < 0) { r = -EFAULT; break; }
                n += take;
                voff += take;
                if (voff == iov[vi].iov_len) { vi++; voff = 0; }
            }
            if (r < 0) break;
            int64_t w = unix_op_sendmsg(vn, kb, n, flags, top, total ? NULL : anc);
            if (w < 0) { r = w; break; }
            total += w;
            if ((size_t)w < n || vi >= msg.msg_iovlen) break;
        }
        if (anc) { anc_drop(anc); free(anc); }
        free(kb);
        if (total) r = total;
    } else if (sock_type(vn) != SOCK_STREAM) {
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

static int64_t do_recvmsg(task_t *t, vfs_file_t *sock, uint64_t msg_ptr, int flags)
{
    vnode_t *vn = sock->vnode;
    if (!sock_is_vnode(vn) && !unix_is_vnode(vn)) return -ENOTSOCK;
    if (sock->flags & O_NONBLOCK) flags |= MSG_DONTWAIT;
    int is_unix = unix_is_vnode(vn);

    struct k_msghdr msg;
    if (syscall_copy_from_user(&msg, (void *)msg_ptr, sizeof msg) < 0) return -EFAULT;
    size_t got_control = 0;

    struct k_iovec stack[MSG_IOV_STACK];
    size_t total_len;
    int64_t err;
    struct k_iovec *iov = load_iov(&msg, stack, &total_len, &err);
    if (!iov) return err;

    int oflags = 0;
    unsigned int namelen = 0;
    int64_t r;
    if (is_unix) {
        sock_addr_t from;
        memset(&from, 0, sizeof from);
        size_t cap = total_len > UNIX_CHUNK ? UNIX_CHUNK : total_len;
        unix_anc_t *anc = calloc(1, sizeof(*anc));
        uint8_t *kb = malloc(cap ? cap : 1);
        if (!anc || !kb) {
            r = -ENOMEM;
        } else {
            r = unix_op_recvmsg(vn, kb, cap, flags, &from, anc, &oflags);
            if (r >= 0) {
                size_t left = (size_t)r < cap ? (size_t)r : cap;
                size_t off = 0;
                for (size_t i = 0; left && i < msg.msg_iovlen; i++) {
                    size_t n = iov[i].iov_len < left ? iov[i].iov_len : left;
                    if (n && syscall_copy_to_user(iov[i].iov_base, kb + off, n) < 0) { r = -EFAULT; break; }
                    off += n;
                    left -= n;
                }
                if (r >= 0) got_control = put_control(t, &msg, anc, flags, &oflags);
                if (r >= 0 && msg.msg_name && msg.msg_namelen && from.ulen) {
                    uint32_t outlen = 0;
                    int64_t e = net_put_addr(&from, (uint64_t)msg.msg_name, msg.msg_namelen, &outlen);
                    if (e < 0) r = e;
                    namelen = outlen;
                }
            }
            anc_drop(anc);
        }
        free(anc);
        free(kb);
    } else if (sock_type(vn) != SOCK_STREAM) {
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
