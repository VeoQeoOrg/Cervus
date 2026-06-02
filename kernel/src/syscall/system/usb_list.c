#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/usb/xhci.h"
#include "../../../include/drivers/usb/ehci.h"
#include "../../../include/drivers/usb/uhci.h"
#include <string.h>

int64_t sys_usb_list(uint64_t out_ptr, uint64_t max,
                     uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!out_ptr || max == 0) return -EINVAL;
    if (max > 64) max = 64;
    if (!syscall_uptr_validate((void *)out_ptr, max * sizeof(xhci_pub_dev_t)))
        return -EFAULT;

    xhci_pub_dev_t tmp[64];
    int n = xhci_list_devs(tmp, (int)max);
    if (n < 0) n = 0;

    int xhci_bus_count = xhci_controller_count();
    if (xhci_bus_count < 0) xhci_bus_count = 0;
    int ehci_bus_count = ehci_controller_count();
    if (ehci_bus_count < 0) ehci_bus_count = 0;

    if (n < (int)max) {
        int m = ehci_list_devs(tmp + n, (int)max - n, (uint8_t)xhci_bus_count);
        if (m > 0) n += m;
    }
    if (n < (int)max) {
        int m = uhci_list_devs(tmp + n, (int)max - n,
                               (uint8_t)(xhci_bus_count + ehci_bus_count));
        if (m > 0) n += m;
    }

    memcpy((void *)out_ptr, tmp, (size_t)n * sizeof(xhci_pub_dev_t));
    return n;
}
