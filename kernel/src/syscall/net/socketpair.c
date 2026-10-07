#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/net/socket.h"

int64_t sys_socketpair(uint64_t domain, uint64_t type, uint64_t fds_ptr)
{
    if (domain == AF_INET || domain == AF_INET6) return -EOPNOTSUPP;
    if (domain != AF_UNIX) return -EAFNOSUPPORT;
    int sflags = (int)(type & (SOCK_NONBLOCK | SOCK_CLOEXEC));
    int stype = (int)(type & ~(uint64_t)(SOCK_NONBLOCK | SOCK_CLOEXEC));

    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;
    if (!syscall_uptr_validate_write((void *)fds_ptr, sizeof(int) * 2)) return -EFAULT;

    vnode_t *va = NULL, *vb = NULL;
    int r = unix_make_pair(stype, &va, &vb);
    if (r < 0) return r;

    vfs_file_t *fa = vfs_file_alloc();
    vfs_file_t *fb = vfs_file_alloc();
    if (!fa || !fb) {
        if (fa) vfs_file_free(fa);
        if (fb) vfs_file_free(fb);
        va->ops->unref(va);
        vb->ops->unref(vb);
        return -ENOMEM;
    }
    fa->vnode = va;
    fb->vnode = vb;
    fa->flags = O_RDWR;
    fb->flags = O_RDWR;
    if (sflags & SOCK_NONBLOCK) {
        fa->flags |= O_NONBLOCK;
        fb->flags |= O_NONBLOCK;
    }

    int fd_a = fd_alloc(t->fd_table, fa, 0);
    if (fd_a < 0) { vfs_file_free(fa); vfs_file_free(fb); return -EMFILE; }
    int fd_b = fd_alloc(t->fd_table, fb, 0);
    if (fd_b < 0) { fd_close(t->fd_table, fd_a); vfs_file_free(fb); return -EMFILE; }

    if (sflags & SOCK_CLOEXEC) {
        fd_set_flags(t->fd_table, fd_a, FD_CLOEXEC);
        fd_set_flags(t->fd_table, fd_b, FD_CLOEXEC);
    }

    int pair[2] = { fd_a, fd_b };
    if (syscall_copy_to_user((void *)fds_ptr, pair, sizeof pair) < 0) {
        fd_close(t->fd_table, fd_a);
        fd_close(t->fd_table, fd_b);
        return -EFAULT;
    }
    return 0;
}
