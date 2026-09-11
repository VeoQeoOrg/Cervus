#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/fs/vfs.h"

#define AF_UNIX     1
#define SOCK_STREAM 1

extern int unix_make_pair(vnode_t **a_out, vnode_t **b_out);

int64_t sys_socketpair(uint64_t domain, uint64_t type, uint64_t fds_ptr)
{
    if (domain != AF_UNIX) return -EPROTO;
    if ((type & 0xFF) != SOCK_STREAM) return -EPROTO;

    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;
    if (!syscall_uptr_validate((void *)fds_ptr, sizeof(int) * 2)) return -EFAULT;

    vnode_t *va = NULL, *vb = NULL;
    if (unix_make_pair(&va, &vb) < 0) return -ENOMEM;

    vfs_file_t *fa = vfs_file_alloc();
    vfs_file_t *fb = vfs_file_alloc();
    if (!fa || !fb) {
        if (fa) vfs_file_free(fa);
        if (fb) vfs_file_free(fb);
        return -ENOMEM;
    }
    fa->vnode = va;
    fb->vnode = vb;
    fa->flags = 2;
    fb->flags = 2;

    int fd_a = fd_alloc(t->fd_table, fa, 0);
    if (fd_a < 0) { vfs_file_free(fa); vfs_file_free(fb); return -EMFILE; }
    int fd_b = fd_alloc(t->fd_table, fb, 0);
    if (fd_b < 0) { fd_close(t->fd_table, fd_a); vfs_file_free(fb); return -EMFILE; }

    int pair[2] = { fd_a, fd_b };
    if (syscall_copy_to_user((void *)fds_ptr, pair, sizeof pair) < 0) {
        fd_close(t->fd_table, fd_a);
        fd_close(t->fd_table, fd_b);
        return -EFAULT;
    }
    return 0;
}
