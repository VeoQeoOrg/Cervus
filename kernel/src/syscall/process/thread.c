#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/sched.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/fs/vfs.h"
#include <string.h>

extern task_t *task_spawn_thread(task_t *parent, uintptr_t entry,
                                 uintptr_t stack_top, uint64_t arg);

int64_t sys_thread_create(uint64_t entry, uint64_t stack_top, uint64_t arg,
                          uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a4; (void)a5; (void)a6;

    task_t *me = syscall_cur_task();
    if (!me || !me->is_userspace) return -EPERM;

    if (entry < 0x1000 || entry >= 0x0000800000000000ULL) return -EFAULT;
    if (stack_top < 0x2000 || stack_top >= 0x0000800000000000ULL) return -EFAULT;
    if (!syscall_uptr_validate((const void *)(uintptr_t)entry, 1)) return -EFAULT;
    if (!syscall_uptr_validate_write((const void *)(uintptr_t)(stack_top - 16), 16))
        return -EFAULT;

    task_t *th = task_spawn_thread(me, (uintptr_t)entry, (uintptr_t)stack_top, arg);
    if (!th) return -ENOMEM;
    return (int64_t)th->pid;
}

int64_t sys_thread_exit(uint64_t code, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *me = syscall_cur_task();
    if (me) me->exit_code = (int)(code & 0xFF);
    vmm_switch_pagemap(vmm_get_kernel_pagemap());
    task_exit();
    return 0;
}
