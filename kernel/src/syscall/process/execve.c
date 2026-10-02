#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/elf/elf.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/memory/mm.h"
#include "../../../include/smp/percpu.h"
#include "../../../include/io/serial.h"
#include "../../../include/puzzle/puzzle.h"
#include "../../../include/drivers/random.h"
#include <string.h>
#include <stdlib.h>

#define EXECVE_MAX_PATH    512
#define EXECVE_ARG_MAX     (2u << 20)
#define EXECVE_STRLEN_MAX  (128u << 10)
#define EXECVE_SHEBANG_MAX 256

#define AT_NULL     0
#define AT_PHDR     3
#define AT_PHENT    4
#define AT_PHNUM    5
#define AT_PAGESZ   6
#define AT_BASE     7
#define AT_FLAGS    8
#define AT_ENTRY    9
#define AT_UID      11
#define AT_EUID     12
#define AT_GID      13
#define AT_EGID     14
#define AT_PLATFORM 15
#define AT_HWCAP    16
#define AT_CLKTCK   17
#define AT_SECURE   23
#define AT_RANDOM   25
#define AT_HWCAP2   26
#define AT_EXECFN   31

typedef struct {
    char    *buf;
    size_t   used;
    size_t   cap;
    size_t  *offs;
    int      count;
    int      ocap;
} strvec_t;

static void sv_free(strvec_t *v) {
    free(v->buf);
    free(v->offs);
    memset(v, 0, sizeof *v);
}

static int sv_push(strvec_t *v, const char *s, size_t len, size_t *total) {
    if (len + 1 > EXECVE_STRLEN_MAX) return -E2BIG;
    if (*total + len + 1 + sizeof(char *) > EXECVE_ARG_MAX) return -E2BIG;
    if (v->used + len + 1 > v->cap) {
        size_t nc = v->cap ? v->cap : 4096;
        while (nc < v->used + len + 1) nc *= 2;
        char *nb = realloc(v->buf, nc);
        if (!nb) return -ENOMEM;
        v->buf = nb;
        v->cap = nc;
    }
    if (v->count + 1 > v->ocap) {
        int no = v->ocap ? v->ocap * 2 : 64;
        size_t *n = realloc(v->offs, (size_t)no * sizeof(size_t));
        if (!n) return -ENOMEM;
        v->offs = n;
        v->ocap = no;
    }
    memcpy(v->buf + v->used, s, len);
    v->buf[v->used + len] = 0;
    v->offs[v->count++] = v->used;
    v->used += len + 1;
    *total += len + 1 + sizeof(char *);
    return 0;
}

static int copy_user_strings(strvec_t *v, uint64_t uvec, size_t *total) {
    if (!uvec) return 0;
    char *tmp = malloc(EXECVE_STRLEN_MAX);
    if (!tmp) return -ENOMEM;
    for (int i = 0; ; i++) {
        uint64_t p = 0;
        if (syscall_copy_from_user(&p, (const void *)(uvec + (uint64_t)i * 8), 8) < 0) { free(tmp); return -EFAULT; }
        if (!p) break;
        int n = syscall_strncpy_from_user(tmp, (const char *)p, EXECVE_STRLEN_MAX);
        if (n < 0) { free(tmp); return -EFAULT; }
        if ((size_t)n >= EXECVE_STRLEN_MAX - 1) { free(tmp); return -E2BIG; }
        int r = sv_push(v, tmp, (size_t)n, total);
        if (r < 0) { free(tmp); return r; }
    }
    free(tmp);
    return 0;
}

static uintptr_t build_stack(mm_t *mm, uintptr_t stack_top, const strvec_t *argv,
                             const strvec_t *envp, const elf_load_result_t *elf,
                             const char *execfn, task_t *t) {
    static const char platform[] = "x86_64";
    uintptr_t sp = stack_top & ~(uintptr_t)0xF;

    size_t efl = strlen(execfn) + 1;
    sp -= efl;
    uintptr_t u_execfn = sp;
    if (mm_copy_to(mm, u_execfn, execfn, efl) < 0) return 0;

    sp -= sizeof platform;
    uintptr_t u_platform = sp;
    if (mm_copy_to(mm, u_platform, platform, sizeof platform) < 0) return 0;

    sp &= ~(uintptr_t)0xF;
    sp -= 16;
    uintptr_t u_random = sp;
    uint8_t rnd[16];
    random_bytes(rnd, sizeof rnd);
    if (mm_copy_to(mm, u_random, rnd, sizeof rnd) < 0) return 0;

    sp -= envp->used;
    uintptr_t u_env = sp;
    if (envp->used && mm_copy_to(mm, u_env, envp->buf, envp->used) < 0) return 0;
    sp -= argv->used;
    uintptr_t u_arg = sp;
    if (argv->used && mm_copy_to(mm, u_arg, argv->buf, argv->used) < 0) return 0;

    uint64_t auxv[] = {
        AT_PHDR,     elf->phdr_vaddr ? elf->phdr_vaddr : elf->load_base + 0x40,
        AT_PHENT,    elf->phentsize ? elf->phentsize : 56,
        AT_PHNUM,    elf->phnum,
        AT_PAGESZ,   4096,
        AT_BASE,     elf->has_interp ? elf->interp_base : 0,
        AT_FLAGS,    0,
        AT_ENTRY,    elf->entry,
        AT_UID,      t->uid,
        AT_EUID,     t->uid,
        AT_GID,      t->gid,
        AT_EGID,     t->gid,
        AT_SECURE,   0,
        AT_CLKTCK,   100,
        AT_HWCAP,    0x178bfbff,
        AT_HWCAP2,   0,
        AT_RANDOM,   u_random,
        AT_PLATFORM, u_platform,
        AT_EXECFN,   u_execfn,
        AT_NULL,     0,
    };
    size_t nauxv = sizeof auxv / sizeof auxv[0];
    size_t nptr = 1 + (size_t)argv->count + 1 + (size_t)envp->count + 1 + nauxv;

    sp -= nptr * 8;
    sp &= ~(uintptr_t)0xF;

    uint64_t *frame = malloc(nptr * 8);
    if (!frame) return 0;
    size_t fi = 0;
    frame[fi++] = (uint64_t)argv->count;
    for (int i = 0; i < argv->count; i++) frame[fi++] = u_arg + argv->offs[i];
    frame[fi++] = 0;
    for (int i = 0; i < envp->count; i++) frame[fi++] = u_env + envp->offs[i];
    frame[fi++] = 0;
    for (size_t i = 0; i < nauxv; i++) frame[fi++] = auxv[i];
    int r = mm_copy_to(mm, sp, frame, nptr * 8);
    free(frame);
    return r < 0 ? 0 : sp;
}

static int exec_allowed(const vfs_stat_t *st, uint32_t uid, uint32_t gid)
{
    uint32_t m = st->st_mode & 0777;
    if (uid == 0) return (m & 0111) != 0;
    if (m == 0) return 1;
    int allowed;
    if (uid == st->st_uid)      allowed = (int)((m >> 6) & 7);
    else if (gid == st->st_gid) allowed = (int)((m >> 3) & 7);
    else                        allowed = (int)(m & 7);
    return (allowed & 1) != 0;
}

static int parse_shebang(const uint8_t *hdr, size_t n, char *interp, char *arg) {
    size_t e = 2, li = 0;
    char line[EXECVE_SHEBANG_MAX];
    while (e < n && hdr[e] != '\n' && li < sizeof(line) - 1) line[li++] = (char)hdr[e++];
    line[li] = '\0';
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && i < EXECVE_MAX_PATH - 1) interp[i++] = *p++;
    interp[i] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    i = 0;
    while (*p && *p != '\r' && i < EXECVE_MAX_PATH - 1) arg[i++] = *p++;
    while (i > 0 && (arg[i - 1] == ' ' || arg[i - 1] == '\t')) i--;
    arg[i] = '\0';
    return interp[0] ? 0 : -ENOEXEC;
}

int64_t sys_execve(uint64_t path_ptr, uint64_t argv_ptr, uint64_t envp_ptr)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->is_userspace) return -EPERM;

    char kpath[EXECVE_MAX_PATH];
    int rp = syscall_resolve_path_from_user(kpath, (const char *)path_ptr, sizeof(kpath));
    if (rp < 0) return rp;
    if (!kpath[0]) return -ENOENT;

    strvec_t argv, envp;
    memset(&argv, 0, sizeof argv);
    memset(&envp, 0, sizeof envp);
    size_t total = 0;
    int r = copy_user_strings(&argv, argv_ptr, &total);
    if (r == 0) r = copy_user_strings(&envp, envp_ptr, &total);
    if (r == 0 && argv.count == 0) r = sv_push(&argv, kpath, strlen(kpath), &total);
    if (r < 0) { sv_free(&argv); sv_free(&envp); return r; }

    char execfn[EXECVE_MAX_PATH];
    strncpy(execfn, kpath, sizeof execfn - 1);
    execfn[sizeof execfn - 1] = 0;

    vfs_file_t *vfile = NULL;
    size_t fsize = 0;
    for (int depth = 0; ; depth++) {
        vfs_stat_t xst;
        if (vfs_stat(kpath, &xst) == 0 && !exec_allowed(&xst, t->uid, t->gid)) {
            sv_free(&argv); sv_free(&envp);
            return -EACCES;
        }
        int vret = vfs_open(kpath, O_RDONLY, 0, &vfile);
        if (vret < 0) { sv_free(&argv); sv_free(&envp); return vret; }
        vfs_stat_t st;
        if (vfs_fstat(vfile, &st) < 0 || st.st_size == 0) {
            vfs_close(vfile); sv_free(&argv); sv_free(&envp); return -ENOEXEC;
        }
        fsize = (size_t)st.st_size;
        uint8_t hdr[EXECVE_SHEBANG_MAX];
        size_t want = fsize < sizeof hdr ? fsize : sizeof hdr;
        int64_t rd = vfs_read(vfile, hdr, want);
        if (rd < 4) { vfs_close(vfile); sv_free(&argv); sv_free(&envp); return -ENOEXEC; }

        if (hdr[0] == '#' && hdr[1] == '!') {
            vfs_close(vfile);
            vfile = NULL;
            if (depth >= 4) { sv_free(&argv); sv_free(&envp); return -ELOOP; }
            char interp[EXECVE_MAX_PATH], sharg[EXECVE_MAX_PATH];
            if (parse_shebang(hdr, (size_t)rd, interp, sharg) < 0) {
                sv_free(&argv); sv_free(&envp); return -ENOEXEC;
            }
            strvec_t na;
            memset(&na, 0, sizeof na);
            size_t nt = total;
            r = sv_push(&na, interp, strlen(interp), &nt);
            if (r == 0 && sharg[0]) r = sv_push(&na, sharg, strlen(sharg), &nt);
            if (r == 0) r = sv_push(&na, kpath, strlen(kpath), &nt);
            for (int a = 1; r == 0 && a < argv.count; a++)
                r = sv_push(&na, argv.buf + argv.offs[a], strlen(argv.buf + argv.offs[a]), &nt);
            if (r < 0) { sv_free(&na); sv_free(&argv); sv_free(&envp); return r; }
            sv_free(&argv);
            argv = na;
            total = nt;
            if (interp[0] == '/') {
                strncpy(kpath, interp, sizeof kpath - 1);
                kpath[sizeof kpath - 1] = 0;
            } else {
                rp = syscall_resolve_path_from_user(kpath, interp, sizeof kpath);
                if (rp < 0) { sv_free(&argv); sv_free(&envp); return rp; }
            }
            continue;
        }
        if (hdr[0] != 0x7F || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F') {
            vfs_close(vfile); sv_free(&argv); sv_free(&envp); return -ENOEXEC;
        }
        break;
    }

    elf_load_result_t elf = elf_load_vnode(vfile->vnode, fsize, 0);
    if (elf.error != ELF_OK) {
        serial_printf("[EXECVE] %s: %s\n", kpath, elf_strerror(elf.error));
        if (elf.mm) mm_put(elf.mm);
        vfs_close(vfile); sv_free(&argv); sv_free(&envp);
        return -ENOEXEC;
    }

    if (mm_set_exe(elf.mm, vfile->vnode) < 0) {
        mm_put(elf.mm);
        vfs_close(vfile); sv_free(&argv); sv_free(&envp);
        return -ETXTBSY;
    }

    uintptr_t new_rsp = build_stack(elf.mm, elf.stack_top, &argv, &envp, &elf, execfn, t);
    sv_free(&argv);
    if (!new_rsp) {
        mm_put(elf.mm);
        vfs_close(vfile); sv_free(&envp);
        return -ENOMEM;
    }
    mm_init_heap(elf.mm, elf.load_end);

    if (t->fd_table) fd_table_cloexec(t->fd_table);

    vmm_switch_pagemap(vmm_get_kernel_pagemap());

    mm_t *old_mm = t->mm;
    vmm_pagemap_t *old_pagemap = t->pagemap;
    uint32_t old_flags = t->flags;

    if (t->puzzle) { puzzle_destroy(t->puzzle); t->puzzle = NULL; }

    t->mm         = elf.mm;
    t->pagemap    = elf.mm->pagemap;
    t->cr3        = elf.mm->cr3;
    t->flags     |= TASK_FLAG_OWN_PAGEMAP;
    t->flags     &= ~TASK_FLAG_FORK;
    t->brk_start  = t->brk_current = elf.mm->brk_start;
    t->brk_max    = MM_MMAP_TOP;

    t->fs_base    = 0;
    t->tls_vaddr  = elf.tls_vaddr;
    t->tls_filesz = elf.tls_filesz;
    t->tls_memsz  = elf.tls_memsz;
    t->tls_align  = elf.tls_align;

    t->user_rsp       = new_rsp;
    t->user_saved_rip = elf.has_interp ? elf.interp_entry : elf.entry;
    t->user_saved_rbp = t->user_saved_rbx = 0;
    t->user_saved_r12 = t->user_saved_r13 = t->user_saved_r14 = 0;
    t->user_saved_r15 = t->user_saved_r11 = 0;

    const char *bn = kpath;
    for (const char *p = kpath; *p; p++) if (*p == '/') bn = p + 1;
    strncpy(t->name, bn, sizeof(t->name) - 1);
    t->name[sizeof(t->name) - 1] = '\0';
    strncpy(t->exe, kpath, sizeof(t->exe) - 1);
    t->exe[sizeof(t->exe) - 1] = '\0';

    {
        int puzzle_on = 1;
        for (int i = 0; i < envp.count; i++) {
            if (strcmp(envp.buf + envp.offs[i], "PUZZLE=0") == 0) puzzle_on = 0;
            if (strcmp(envp.buf + envp.offs[i], "CERVUS_PROFILE=1") == 0) profile_start(t);
        }
        if (puzzle_on)
            puzzle_create(t, &elf, elf.mm->brk_start, elf.stack_top, elf.stack_size,
                          vfile->vnode, fsize);
    }
    sv_free(&envp);
    vfs_close(vfile);

    percpu_t *pc = get_percpu();
    if (pc) {
        pc->syscall_user_rsp = new_rsp;
        pc->user_saved_rip   = elf.has_interp ? elf.interp_entry : elf.entry;
        pc->user_saved_rbp = pc->user_saved_rbx = 0;
        pc->user_saved_r12 = pc->user_saved_r13 = pc->user_saved_r14 = 0;
        pc->user_saved_r15 = 0;
        pc->user_saved_r11 = 0x200;
    }

    if (old_mm) mm_put(old_mm);
    else if (old_pagemap && (old_flags & (TASK_FLAG_OWN_PAGEMAP | TASK_FLAG_FORK)))
        vmm_free_pagemap(old_pagemap);

    asm volatile("lock addl $0, (%%rsp)" ::: "memory", "cc");
    vmm_switch_pagemap(t->pagemap);
    return 0;
}
