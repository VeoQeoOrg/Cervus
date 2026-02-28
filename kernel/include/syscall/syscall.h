#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYS_WRITE   1
#define SYS_EXIT    60

void     syscall_init(void);
int64_t  syscall_handler_c(uint64_t nr,
                            uint64_t a1, uint64_t a2, uint64_t a3,
                            uint64_t a4, uint64_t a5, uint64_t a6);
uint32_t lapic_get_id_safe(void);

#endif