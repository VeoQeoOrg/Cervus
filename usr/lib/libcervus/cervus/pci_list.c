#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

long cervus_pci_list(cervus_pci_device_t *out, int max)
{
    return __cervus_sys_ret(syscall2(SYS_PCI_LIST, out, max));
}
