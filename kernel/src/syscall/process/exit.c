#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/io/serial.h"

int64_t sys_exit(uint64_t code)
{
    task_t *t = syscall_cur_task();
    if (t) t->exit_code = (int)(uint8_t)code;
    serial_printf("[SYSCALL] exit(%llu) task='%s' pid=%u\n",
                  code, t ? t->name : "?", t ? t->pid : 0);
    vmm_switch_pagemap(vmm_get_kernel_pagemap());
    task_exit();
}

int64_t sys_exit_group(uint64_t code) { return sys_exit(code); }
