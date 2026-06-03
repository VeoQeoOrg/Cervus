#include "../../include/syscall/syscall_internal.h"
#include "../../include/smp/percpu.h"
#include "../../include/memory/vmm.h"
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
    if (addr + len > 0x0000800000000000ULL) return false;

    if (len == 0) return true;
    task_t *t = syscall_cur_task();
    if (!t || !t->pagemap) return true;

    uintptr_t page_start = addr & ~0xFFFULL;
    uintptr_t page_end   = (addr + len - 1) & ~0xFFFULL;
    for (uintptr_t p = page_start; p <= page_end; p += 0x1000) {
        uintptr_t phys;
        if (!vmm_virt_to_phys(t->pagemap, p, &phys)) return false;
    }
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
    if (max == 0) return -EINVAL;
    if (!dst) return -EFAULT;
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

void syscall_path_normalize(char *path)
{
    if (!path || !*path) return;
    char buf[512];
    size_t bi = 0;
    if (path[0] == '/') { buf[bi++] = '/'; }
    size_t i = 0;
    while (path[i]) {
        while (path[i] == '/') i++;
        if (!path[i]) break;
        const char *seg = &path[i];
        size_t slen = 0;
        while (path[i] && path[i] != '/') { i++; slen++; }
        if (slen == 1 && seg[0] == '.') continue;
        if (slen == 2 && seg[0] == '.' && seg[1] == '.') {
            if (bi > 1) {
                bi--;
                while (bi > 1 && buf[bi - 1] != '/') bi--;
                if (bi > 1 && buf[bi - 1] == '/') bi--;
                if (bi == 0) { buf[bi++] = '/'; }
            }
            continue;
        }
        if (bi > 0 && buf[bi - 1] != '/') {
            if (bi >= sizeof(buf) - 1) break;
            buf[bi++] = '/';
        }
        for (size_t k = 0; k < slen && bi < sizeof(buf) - 1; k++)
            buf[bi++] = seg[k];
    }
    if (bi == 0) { buf[bi++] = '/'; }
    if (bi > 1 && buf[bi - 1] == '/') bi--;
    if (bi >= sizeof(buf)) bi = sizeof(buf) - 1;
    buf[bi] = '\0';
    for (size_t k = 0; k <= bi; k++) path[k] = buf[k];
}

int syscall_resolve_path_from_user(char *dst, const char *src, size_t max)
{
    if (!dst || max == 0) return -EINVAL;
    char tmp[512];
    int n = syscall_strncpy_from_user(tmp, src, sizeof(tmp));
    if (n < 0) return n;
    if (tmp[0] == '\0') return -ENOENT;
    if (tmp[0] == '/') {
        size_t L = (size_t)n;
        if (L >= max) return -ENAMETOOLONG;
        memcpy(dst, tmp, L + 1);
    } else {
        task_t *t = syscall_cur_task();
        const char *cwd = (t && t->cwd[0]) ? t->cwd : "/";
        size_t cl = 0; while (cwd[cl]) cl++;
        size_t tl = (size_t)n;
        size_t need = cl + 1 + tl + 1;
        if (need >= max) return -ENAMETOOLONG;
        size_t pos = 0;
        for (size_t k = 0; k < cl; k++) dst[pos++] = cwd[k];
        if (pos == 0 || dst[pos - 1] != '/') dst[pos++] = '/';
        for (size_t k = 0; k < tl; k++) dst[pos++] = tmp[k];
        dst[pos] = '\0';
    }
    syscall_path_normalize(dst);
    return 0;
}
