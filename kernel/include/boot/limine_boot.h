#ifndef _KERNEL_BOOT_LIMINE_BOOT_H
#define _KERNEL_BOOT_LIMINE_BOOT_H

#include <limine.h>

void                       boot_init(void);
struct limine_mp_response *limine_mp(void);

#endif
