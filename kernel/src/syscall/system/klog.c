#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/console/klog.h"

int64_t sys_klog(uint64_t op, uint64_t arg, uint64_t ubuf, uint64_t ulen)
{
    switch (op) {
        case 0: return (int64_t)klog_total();
        case 1: return (int64_t)klog_first();
        case 2: {
            if (!ubuf || ulen < 2) return -EINVAL;
            char kbuf[KLOG_LINE_MAX];
            size_t want = ulen < sizeof(kbuf) ? ulen : sizeof(kbuf);
            int n = klog_get_line(arg, kbuf, want);
            if (n < 0) return -ENOENT;
            if (syscall_copy_to_user((void *)ubuf, kbuf, (size_t)n + 1) < 0)
                return -EFAULT;
            return n;
        }
        default: return -EINVAL;
    }
}
