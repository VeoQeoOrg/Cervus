#ifndef _KERNEL_SYSCALL_INTERNAL_H
#define _KERNEL_SYSCALL_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "syscall.h"
#include "syscall_nums.h"
#include "errno.h"
#include "../sched/sched.h"

task_t *syscall_cur_task(void);
void    syscall_save_user_regs(task_t *t);

bool syscall_uptr_validate(const void *ptr, size_t len);
int  syscall_copy_from_user(void *dst, const void *src, size_t n);
int  syscall_copy_to_user(void *dst, const void *src, size_t n);
int  syscall_strncpy_from_user(char *dst, const char *src, size_t max);

#endif
