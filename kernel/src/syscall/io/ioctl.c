#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include <string.h>

#define IOCTL_KBUF_MAX 128

#define TIOCGWINSZ   0x5413
#define TIOCGCURSOR  0x5480
#define TCGETS       0x5401
#define TCSETS       0x5402
#define TCSETSW      0x5403
#define TCSETSF      0x5404

#define IOCTL_TERMIOS_SIZE 48

static size_t ioctl_out_size(uint64_t request)
{
    switch (request) {
        case TIOCGWINSZ:  return 8;
        case TIOCGCURSOR: return 8;
        case TCGETS:      return IOCTL_TERMIOS_SIZE;
        default:          return 0;
    }
}

static size_t ioctl_in_size(uint64_t request)
{
    switch (request) {
        case TCSETS:
        case TCSETSW:
        case TCSETSF:     return IOCTL_TERMIOS_SIZE;
        default:          return 0;
    }
}

int64_t sys_ioctl(uint64_t fd, uint64_t request, uint64_t arg_ptr)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;

    size_t out_sz = ioctl_out_size(request);
    size_t in_sz  = ioctl_in_size(request);

    if (arg_ptr) {
        size_t validate_sz = out_sz > in_sz ? out_sz : in_sz;
        if (validate_sz == 0) validate_sz = IOCTL_KBUF_MAX;
        if (!syscall_uptr_validate((void *)arg_ptr, validate_sz))
            return -EFAULT;
    }

    char kbuf[IOCTL_KBUF_MAX];
    memset(kbuf, 0, sizeof(kbuf));

    if (arg_ptr && in_sz > 0) {
        if (syscall_copy_from_user(kbuf, (const void *)arg_ptr, in_sz) < 0)
            return -EFAULT;
    }

    int64_t r = vfs_ioctl(f, request, arg_ptr ? (void *)kbuf : (void *)0);
    if (r < 0) return r;

    if (arg_ptr && out_sz > 0) {
        if (syscall_copy_to_user((void *)arg_ptr, kbuf, out_sz) < 0)
            return -EFAULT;
    }
    return r;
}
