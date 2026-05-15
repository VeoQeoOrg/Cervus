#include "../../include/syscall/syscall_internal.h"
#include "../../include/smp/percpu.h"
#include <string.h>

task_t *syscall_cur_task(void)
{
    percpu_t *pc = get_percpu();
    return pc ? (task_t *)pc->current_task : NULL;
}

void syscall_save_user_regs(task_t *t)
{
    if (!t) return;
    percpu_t *pc = get_percpu();
    if (!pc) return;
    t->user_rsp       = pc->syscall_user_rsp;
    t->user_saved_rip = pc->user_saved_rip;
    t->user_saved_rbp = pc->user_saved_rbp;
    t->user_saved_rbx = pc->user_saved_rbx;
    t->user_saved_r12 = pc->user_saved_r12;
    t->user_saved_r13 = pc->user_saved_r13;
    t->user_saved_r14 = pc->user_saved_r14;
    t->user_saved_r15 = pc->user_saved_r15;
    t->user_saved_r11 = pc->user_saved_r11;
}

bool syscall_uptr_validate(const void *ptr, size_t len)
{
    uintptr_t addr = (uintptr_t)ptr;
    if (addr < 0x1000ULL) return false;
    if (addr >= 0x0000800000000000ULL) return false;
    if (len > 0x0000800000000000ULL) return false;
    if (len && addr + len - 1 < addr) return false;
    return true;
}

int syscall_copy_from_user(void *dst, const void *src, size_t n)
{
    if (!syscall_uptr_validate(src, n)) return -EFAULT;
    memcpy(dst, src, n);
    return 0;
}

int syscall_copy_to_user(void *dst, const void *src, size_t n)
{
    if (!syscall_uptr_validate(dst, n)) return -EFAULT;
    memcpy(dst, src, n);
    return 0;
}

int syscall_strncpy_from_user(char *dst, const char *src, size_t max)
{
    if (!syscall_uptr_validate(src, 1)) return -EFAULT;
    for (size_t i = 0; i < max - 1; i++) {
        if ((i == 0) || (!((uintptr_t)(src + i) & 0xFFF)))
            if (!syscall_uptr_validate(src + i, 1)) return -EFAULT;
        dst[i] = src[i];
        if (!dst[i]) return (int)i;
    }
    dst[max - 1] = '\0';
    return (int)(max - 1);
}
