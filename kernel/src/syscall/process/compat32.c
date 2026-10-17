#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/interrupts/interrupts.h"
#include "../../../include/sched/sched.h"
#include "../../../include/gdt/gdt.h"
#include "../../../include/smp/percpu.h"
#include <string.h>

#define NR32_EXIT            1
#define NR32_SET_THREAD_AREA 243
#define NR32_GET_THREAD_AREA 244
#define NR32_EXIT_GROUP      252

#define UD_SEG_32BIT       (1u << 0)
#define UD_CONTENTS_SHIFT  1
#define UD_READ_EXEC_ONLY  (1u << 3)
#define UD_LIMIT_IN_PAGES  (1u << 4)
#define UD_SEG_NOT_PRESENT (1u << 5)
#define UD_USEABLE         (1u << 6)

typedef struct {
    uint32_t entry_number;
    uint32_t base_addr;
    uint32_t limit;
    uint32_t flags;
} user_desc32_t;

extern int64_t sys_exit(uint64_t code);
extern int64_t sys_exit_group(uint64_t code);

static int desc_empty(const user_desc32_t *u) {
    uint32_t f = u->flags;
    return u->base_addr == 0 && u->limit == 0 && ((f >> UD_CONTENTS_SHIFT) & 3) == 0 &&
           (f & UD_READ_EXEC_ONLY) && !(f & UD_SEG_32BIT) && !(f & UD_LIMIT_IN_PAGES) &&
           (f & UD_SEG_NOT_PRESENT) && !(f & UD_USEABLE);
}

static uint64_t make_desc(const user_desc32_t *u) {
    uint32_t f = u->flags;
    uint64_t type = ((uint64_t)(!(f & UD_READ_EXEC_ONLY)) << 1) |
                    ((uint64_t)((f >> UD_CONTENTS_SHIFT) & 3) << 2) | 1;
    uint64_t d = 0;
    d |= (uint64_t)(u->limit & 0xFFFF);
    d |= (uint64_t)(u->base_addr & 0xFFFFFF) << 16;
    d |= type << 40;
    d |= 1ull << 44;
    d |= 3ull << 45;
    d |= (uint64_t)(!(f & UD_SEG_NOT_PRESENT)) << 47;
    d |= (uint64_t)((u->limit >> 16) & 0xF) << 48;
    d |= (uint64_t)((f & UD_USEABLE) ? 1 : 0) << 52;
    d |= (uint64_t)((f & UD_SEG_32BIT) ? 1 : 0) << 54;
    d |= (uint64_t)((f & UD_LIMIT_IN_PAGES) ? 1 : 0) << 55;
    d |= (uint64_t)((u->base_addr >> 24) & 0xFF) << 56;
    return d;
}

static void desc_to_user(uint64_t d, user_desc32_t *u) {
    u->base_addr = (uint32_t)(((d >> 16) & 0xFFFFFF) | (((d >> 56) & 0xFF) << 24));
    u->limit = (uint32_t)((d & 0xFFFF) | (((d >> 48) & 0xF) << 16));
    uint32_t type = (uint32_t)((d >> 40) & 0xF);
    uint32_t f = 0;
    if ((d >> 54) & 1) f |= UD_SEG_32BIT;
    f |= ((type >> 2) & 3) << UD_CONTENTS_SHIFT;
    if (!((type >> 1) & 1)) f |= UD_READ_EXEC_ONLY;
    if ((d >> 55) & 1) f |= UD_LIMIT_IN_PAGES;
    if (!((d >> 47) & 1)) f |= UD_SEG_NOT_PRESENT;
    if ((d >> 52) & 1) f |= UD_USEABLE;
    u->flags = f;
}

int64_t sys_set_thread_area32(uint64_t uptr) {
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    user_desc32_t u;
    if (syscall_copy_from_user(&u, (const void *)uptr, sizeof u) < 0) return -EFAULT;
    int idx = (int)u.entry_number;
    if (idx == -1) {
        for (int i = 0; i < GDT_TLS_ENTRIES; i++)
            if (!t->tls_desc[i]) { idx = GDT_TLS_FIRST + i; break; }
        if (idx == -1) return -ESRCH;
        u.entry_number = (uint32_t)idx;
        if (syscall_copy_to_user((void *)uptr, &u, sizeof u) < 0) return -EFAULT;
    }
    if (idx < GDT_TLS_FIRST || idx >= GDT_TLS_FIRST + GDT_TLS_ENTRIES) return -EINVAL;
    t->tls_desc[idx - GDT_TLS_FIRST] = desc_empty(&u) ? 0 : make_desc(&u);
    gdt_set_tls(smp_cpu_index(), t->tls_desc);
    return 0;
}

int64_t sys_get_thread_area32(uint64_t uptr) {
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    user_desc32_t u;
    if (syscall_copy_from_user(&u, (const void *)uptr, sizeof u) < 0) return -EFAULT;
    int idx = (int)u.entry_number;
    if (idx < GDT_TLS_FIRST || idx >= GDT_TLS_FIRST + GDT_TLS_ENTRIES) return -EINVAL;
    uint64_t d = t->tls_desc[idx - GDT_TLS_FIRST];
    memset(&u, 0, sizeof u);
    u.entry_number = (uint32_t)idx;
    if (d) desc_to_user(d, &u);
    else u.flags = UD_READ_EXEC_ONLY | UD_SEG_NOT_PRESENT;
    return syscall_copy_to_user((void *)uptr, &u, sizeof u) < 0 ? -EFAULT : 0;
}

void compat_int80(struct int_frame_t *f) {
    uint32_t nr = (uint32_t)f->rax;
    uint64_t a1 = (uint32_t)f->rbx;
    int64_t r;
    __asm__ volatile("sti" ::: "memory");
    switch (nr) {
        case NR32_EXIT:            r = sys_exit(a1); break;
        case NR32_EXIT_GROUP:      r = sys_exit_group(a1); break;
        case NR32_SET_THREAD_AREA: r = sys_set_thread_area32(a1); break;
        case NR32_GET_THREAD_AREA: r = sys_get_thread_area32(a1); break;
        default:                   r = -ENOSYS; break;
    }
    __asm__ volatile("cli" ::: "memory");
    f->rax = (uint64_t)(uint32_t)(int32_t)r;
}
