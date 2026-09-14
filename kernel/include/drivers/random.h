#ifndef _KERNEL_DRIVERS_RANDOM_H
#define _KERNEL_DRIVERS_RANDOM_H

#include <stdint.h>
#include <stddef.h>

void random_init(void);
void random_bytes(void *buf, size_t len);
void random_add_entropy(const void *buf, size_t len);

#endif
