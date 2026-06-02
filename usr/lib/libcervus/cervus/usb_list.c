#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

long cervus_usb_list(cervus_usb_dev_t *out, int max)
{
    return __cervus_sys_ret(syscall2(SYS_USB_LIST, out, max));
}
