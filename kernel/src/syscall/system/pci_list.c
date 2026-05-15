#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/pci.h"
#include <string.h>

int64_t sys_pci_list(uint64_t out_ptr, uint64_t max,
                     uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!out_ptr || max == 0) return -EINVAL;
    if (max > 256) max = 256;
    if (!syscall_uptr_validate((void *)out_ptr, max * sizeof(cervus_pci_device_t))) return -EFAULT;

    cervus_pci_device_t *out = (cervus_pci_device_t *)out_ptr;
    int total = pci_device_count();
    int n = total < (int)max ? total : (int)max;

    for (int i = 0; i < n; i++) {
        pci_device_t *d = pci_device_at(i);
        if (!d) break;
        cervus_pci_device_t e;
        memset(&e, 0, sizeof(e));
        e.segment         = d->segment;
        e.bus             = d->bus;
        e.device          = d->device;
        e.function        = d->function;
        e.class_code      = d->class_code;
        e.subclass        = d->subclass;
        e.prog_if         = d->prog_if;
        e.revision        = d->revision;
        e.header_type     = d->header_type & 0x7F;
        e.irq_line        = d->irq_line;
        e.irq_pin         = d->irq_pin;
        e.vendor_id       = d->vendor_id;
        e.device_id       = d->device_id;
        e.has_msi         = d->cap_msi_off  ? 1 : 0;
        e.has_msix        = d->cap_msix_off ? 1 : 0;
        e.msix_table_size = d->msix_table_size;
        for (int b = 0; b < 6; b++) {
            e.bars[b].base         = d->bars[b].base;
            e.bars[b].size         = d->bars[b].size;
            e.bars[b].type         = d->bars[b].type;
            e.bars[b].is_64bit     = d->bars[b].is_64bit;
            e.bars[b].prefetchable = d->bars[b].prefetchable;
        }
        memcpy(&out[i], &e, sizeof(e));
    }
    return n;
}
