#include "../../include/syscall/syscall_internal.h"
#include "../../include/smp/percpu.h"
#include "../../include/memory/vmm.h"
#include "../../include/fs/vfs.h"
#include "../../include/memory/pmm.h"
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
    t->user_saved_rdi = pc->user_saved_rdi;
    t->user_saved_rsi = pc->user_saved_rsi;
    t->user_saved_rdx = pc->user_saved_rdx;
    t->user_saved_r10 = pc->user_saved_r10;
    t->user_saved_r8  = pc->user_saved_r8;
    t->user_saved_r9  = pc->user_saved_r9;
}

static bool uptr_range_ok(const void *ptr, size_t len, int need_write)
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
    if (!t->is_userspace) return true;

    uintptr_t page_start = addr & ~0xFFFULL;
    uintptr_t page_end   = (addr + len - 1) & ~0xFFFULL;
    for (uintptr_t p = page_start; p <= page_end; p += 0x1000) {
        uint64_t flags;
        if (!vmm_virt_flags(t->pagemap, p, &flags)) return false;
        if (!(flags & VMM_USER)) return false;
        if (need_write && !(flags & VMM_WRITE)) return false;
    }
    return true;
}

bool syscall_uptr_validate(const void *ptr, size_t len)
{
    return uptr_range_ok(ptr, len, 0);
}

bool syscall_uptr_validate_write(const void *ptr, size_t len)
{
    return uptr_range_ok(ptr, len, 1);
}

int syscall_copy_from_user(void *dst, const void *src, size_t n)
{
    if (!uptr_range_ok(src, n, 0)) return -EFAULT;
    memcpy(dst, src, n);
    return 0;
}

int syscall_copy_to_user(void *dst, const void *src, size_t n)
{
    if (!uptr_range_ok(dst, n, 1)) return -EFAULT;
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

static int perm_check(uint32_t mode, uint32_t fuid, uint32_t fgid,
                      uint32_t uid, uint32_t gid, int want)
{
    if (uid == 0) return 1;
    uint32_t m = mode & 0777;
    if (m == 0) return 1;
    int allowed;
    if (uid == fuid)      allowed = (int)((m >> 6) & 7);
    else if (gid == fgid) allowed = (int)((m >> 3) & 7);
    else                  allowed = (int)(m & 7);
    return (allowed & want) == want;
}

int syscall_perm_file(const char *kpath, int want)
{
    task_t *t = syscall_cur_task();
    if (!t || t->uid == 0) return 0;
    vfs_stat_t st;
    if (vfs_stat(kpath, &st) != 0) return -ENOENT;
    return perm_check(st.st_mode, st.st_uid, st.st_gid, t->uid, t->gid, want) ? 0 : -EACCES;
}

int syscall_perm_parent(const char *kpath, int want)
{
    task_t *t = syscall_cur_task();
    if (!t || t->uid == 0) return 0;
    char parent[VFS_MAX_PATH];
    strncpy(parent, kpath, sizeof(parent));
    parent[sizeof(parent) - 1] = 0;
    char *slash = strrchr(parent, '/');
    if (slash == parent) parent[1] = 0;
    else if (slash)      *slash = 0;
    else                 { parent[0] = '/'; parent[1] = 0; }
    vfs_stat_t st;
    if (vfs_stat(parent, &st) != 0) return -EACCES;
    return perm_check(st.st_mode, st.st_uid, st.st_gid, t->uid, t->gid, want) ? 0 : -EACCES;
}

#define SYSCALL_BOUNCE_MAX (256 * 1024)

void *syscall_bounce_get(size_t want, size_t *cap, void *stackbuf, size_t stack_cap)
{
    if (want > SYSCALL_BOUNCE_MAX) want = SYSCALL_BOUNCE_MAX;
    for (size_t sz = want; sz > stack_cap; sz /= 4) {
        void *b = kmalloc(sz);
        if (b) { *cap = sz; return b; }
    }
    *cap = stack_cap;
    return stackbuf;
}

void syscall_bounce_put(void *buf, void *stackbuf)
{
    if (buf && buf != stackbuf) kfree(buf);
}
