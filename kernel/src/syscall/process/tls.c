#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/sched.h"

typedef struct {
    uint64_t vaddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} tls_info_t;

int64_t sys_set_fsbase(uint64_t base, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = syscall_cur_task();
    if (!t || !t->is_userspace) return -EPERM;
    if (base >= 0x0000800000000000ULL) return -EINVAL;

    t->fs_base = base;
    asm volatile("wrmsr" :: "c"(0xC0000100u),
                 "a"((uint32_t)base), "d"((uint32_t)(base >> 32)));
    return 0;
}

int64_t sys_tls_info(uint64_t uptr, uint64_t a2, uint64_t a3,
                     uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = syscall_cur_task();
    if (!t) return -EPERM;
    if (!syscall_uptr_validate_write((void *)(uintptr_t)uptr, sizeof(tls_info_t)))
        return -EFAULT;

    tls_info_t info;
    info.vaddr  = t->tls_vaddr;
    info.filesz = t->tls_filesz;
    info.memsz  = t->tls_memsz;
    info.align  = t->tls_align ? t->tls_align : 1;
    *(tls_info_t *)(uintptr_t)uptr = info;
    return 0;
}
