#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "syscall_nums.h"
#include "../sched/capabilities.h"

void syscall_init(void);

int64_t syscall_handler_c(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t user_rip);

#endif