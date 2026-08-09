#ifndef _KERNEL_BOOT_LIMINE_BOOT_H
#define _KERNEL_BOOT_LIMINE_BOOT_H

#include <limine.h>

void boot_info_init_limine(struct limine_framebuffer_response *fb,
                           struct limine_memmap_response      *mm,
                           struct limine_hhdm_response        *hhdm,
                           struct limine_rsdp_response        *rsdp,
                           struct limine_module_response      *mods);

#endif
