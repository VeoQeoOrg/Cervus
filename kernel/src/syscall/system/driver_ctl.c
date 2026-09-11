#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/pci.h"
#include <string.h>

#define DRV_OP_LIST      0
#define DRV_OP_START     1
#define DRV_OP_STOP      2
#define DRV_OP_AUTOSTART 3

#define DRV_LIST_MAX 64

int64_t sys_driver_ctl(uint64_t op, uint64_t a, uint64_t b) {
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;

    if (op == DRV_OP_LIST) {
        if (!a) return -EINVAL;
        size_t sz = sizeof(pci_drv_info_t) * DRV_LIST_MAX;
        if (!syscall_uptr_validate((void *)a, sz)) return -EFAULT;

        static pci_drv_info_t tmp[DRV_LIST_MAX];
        int n = pci_driver_list(tmp, DRV_LIST_MAX);
        memcpy((void *)a, tmp, sizeof(pci_drv_info_t) * (size_t)n);
        return n;
    }

    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;

    char name[32];
    if (syscall_strncpy_from_user(name, (const char *)a, sizeof name) < 0) return -EFAULT;

    switch (op) {
        case DRV_OP_START:     return pci_driver_start(name);
        case DRV_OP_STOP:      return pci_driver_stop(name);
        case DRV_OP_AUTOSTART: return pci_driver_set_autostart(name, (int)b);
        default:               return -EINVAL;
    }
}
