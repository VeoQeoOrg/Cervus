#include "../../include/syscall/syscall.h"
#include "../../include/syscall/syscall_nums.h"
#include "../../include/syscall/errno.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/capabilities.h"
#include "../../include/smp/smp.h"
#include "../../include/smp/percpu.h"
#include "../../include/apic/apic.h"
#include "../../include/gdt/gdt.h"
#include "../../include/memory/vmm.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include "../../include/fs/vfs.h"
#include "../../include/elf/elf.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084
#define EFER_SCE   (1ULL << 0)
#define EFER_NXE   (1ULL << 11)

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t val) {
    asm volatile("wrmsr" :: "c"(msr), "a"((uint32_t)val),
                             "d"((uint32_t)(val >> 32)));
}

static inline task_t* cur_task(void) {
    percpu_t* pc = get_percpu();
    if (!pc) return NULL;
    return (task_t*)pc->current_task;
}

static bool uptr_validate(const void* ptr, size_t len) {
    uintptr_t addr = (uintptr_t)ptr;

    if (addr == 0)                          return false;
    if (addr >= 0x0000800000000000ULL)      return false;
    if (len  >  0x0000800000000000ULL)      return false;
    if (addr + len < addr)                  return false;

    task_t* t = cur_task();
    if (!t || !t->pagemap) return false;

    uintptr_t page = addr & ~0xFFFULL;
    uintptr_t end  = (addr + len + 0xFFFULL) & ~0xFFFULL;
    for (; page < end; page += 0x1000) {
        uint64_t flags = 0;
        if (!vmm_get_page_flags(t->pagemap, page, &flags)) return false;
        if (!(flags & VMM_USER))                           return false;
        if (!(flags & VMM_PRESENT))                        return false;
    }
    return true;
}

static int copy_from_user(void* dst, const void* src, size_t n) {
    if (!uptr_validate(src, n)) return -EFAULT;
    memcpy(dst, src, n);
    return 0;
}

static int copy_to_user(void* dst, const void* src, size_t n) {
    if (!uptr_validate(dst, n)) return -EFAULT;
    memcpy(dst, src, n);
    return 0;
}

static int strncpy_from_user(char* dst, const char* src, size_t max_len) {
    if (!uptr_validate(src, 1)) return -EFAULT;
    size_t i;
    for (i = 0; i < max_len - 1; i++) {
        if ((i == 0) || (((uintptr_t)(src + i) & 0xFFF) == 0)) {
            if (!uptr_validate(src + i, 1)) return -EFAULT;
        }
        dst[i] = src[i];
        if (src[i] == '\0') break;
    }
    dst[i] = '\0';
    return (int)i;
}

static int64_t sys_exit(uint64_t code) {
    task_t* t = cur_task();
    if (t) t->exit_code = (int)(uint8_t)code;

    serial_printf("[SYSCALL] exit(%llu) task='%s' pid=%u\n",
                  code, t ? t->name : "?", t ? t->pid : 0);

    vmm_switch_pagemap(vmm_get_kernel_pagemap());
    task_exit();
}

static int64_t sys_exit_group(uint64_t code) {
    return sys_exit(code);
}

static int64_t sys_getpid(void) {
    task_t* t = cur_task();
    return t ? (int64_t)t->pid : -ESRCH;
}

static int64_t sys_getppid(void) {
    task_t* t = cur_task();
    return t ? (int64_t)t->ppid : -ESRCH;
}

static int64_t sys_fork(void) {
    task_t* parent = cur_task();
    if (!parent) return -ESRCH;

    percpu_t* pc = get_percpu();
    if (pc) {
        parent->user_rsp = pc->syscall_user_rsp;
    }

    task_t* child = task_fork(parent);
    if (!child) return -ENOMEM;

    serial_printf("[SYSCALL] fork: parent pid=%u → child pid=%u\n",
                  parent->pid, child->pid);
    return (int64_t)child->pid;
}

#define EXECVE_MAX_PATH   512
#define EXECVE_MAX_ARGS   128
#define EXECVE_MAX_ARGLEN 4096

#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_ENTRY  9

static uintptr_t execve_build_stack(vmm_pagemap_t *map, uintptr_t stack_top, const char *argv_k[], int argc, const char *envp_k[], int envc, const elf_load_result_t *elf) {
    size_t str_bytes = 0;
    for (int i = 0; i < argc; i++)
        str_bytes += strlen(argv_k[i]) + 1;
    for (int i = 0; i < envc; i++)
        str_bytes += strlen(envp_k[i]) + 1;

    size_t frame_bytes = 8 + (argc + 1) * 8 + (envc + 1) * 8 + 6 * 16 + str_bytes + 32;
    size_t frame_pages = (frame_bytes + 0xFFF) >> 12;

    uintptr_t frame_base = (stack_top - frame_pages * 0x1000) & ~(uintptr_t)0xF;

    uint8_t *kbuf = (uint8_t *)malloc(frame_pages * 0x1000);
    if (!kbuf) return 0;
    memset(kbuf, 0, frame_pages * 0x1000);

    uintptr_t kbase = (uintptr_t)kbuf;

#define PUSH64(val) do {                    \
    rsp_user -= 8;                          \
    uintptr_t _off = rsp_user - frame_base; \
    if (_off < frame_pages * 0x1000)        \
        *(uint64_t*)(kbase + _off) = (uint64_t)(val); \
} while(0)

    uintptr_t str_region_user = stack_top - str_bytes - 8;
    str_region_user &= ~(uintptr_t)0xF;
    uintptr_t str_ptr_user = str_region_user;

    uint64_t argv_user[EXECVE_MAX_ARGS + 1];
    for (int i = 0; i < argc; i++) {
        size_t slen = strlen(argv_k[i]) + 1;
        uintptr_t off = str_ptr_user - frame_base;
        if (off < frame_pages * 0x1000)
            memcpy((void*)(kbase + off), argv_k[i], slen);
        argv_user[i] = str_ptr_user;
        str_ptr_user += slen;
    }
    argv_user[argc] = 0;

    uint64_t envp_user[EXECVE_MAX_ARGS + 1];
    for (int i = 0; i < envc; i++) {
        size_t slen = strlen(envp_k[i]) + 1;
        uintptr_t off = str_ptr_user - frame_base;
        if (off < frame_pages * 0x1000)
            memcpy((void*)(kbase + off), envp_k[i], slen);
        envp_user[i] = str_ptr_user;
        str_ptr_user += slen;
    }
    envp_user[envc] = 0;

    int num_pushes = 15 + argc + envc;

    uintptr_t rsp_user = str_region_user & ~(uintptr_t)0xF;

    if ((num_pushes & 1) != 0)
        rsp_user -= 8;

    PUSH64(0);
    PUSH64(AT_NULL);
    PUSH64(0x1000);
    PUSH64(AT_PAGESZ);
    PUSH64(elf->entry);
    PUSH64(AT_ENTRY);
    PUSH64(0);
    PUSH64(AT_PHNUM);
    PUSH64(56);
    PUSH64(AT_PHENT);
    PUSH64(elf->load_base + 64);
    PUSH64(AT_PHDR);

    PUSH64(0);
    for (int i = envc - 1; i >= 0; i--)
        PUSH64(envp_user[i]);

    PUSH64(0);
    for (int i = argc - 1; i >= 0; i--)
        PUSH64(argv_user[i]);

    PUSH64((uint64_t)argc);
#undef PUSH64
    for (size_t pg = 0; pg < frame_pages; pg++) {
        uintptr_t virt = frame_base + pg * 0x1000;
        uint64_t flags = 0;
        if (!vmm_get_page_flags(map, virt, &flags)) {
            void *phys_page = pmm_alloc_zero(1);
            if (!phys_page) { free(kbuf); return 0; }
            if (!vmm_map_page(map, virt, pmm_virt_to_phys(phys_page), VMM_PRESENT | VMM_WRITE | VMM_USER | VMM_NOEXEC)) {
                pmm_free(phys_page, 1);
                free(kbuf);
                return 0;
            }
        }
        uintptr_t phys = 0;
        if (vmm_virt_to_phys(map, virt, &phys) && phys) {
            void *kvirt = pmm_phys_to_virt(phys);
            if (kvirt)
                memcpy(kvirt, kbuf + pg * 0x1000, 0x1000);
        }
    }

    free(kbuf);

    serial_printf("[EXECVE] stack built: frame_base=0x%llx rsp=0x%llx argc=%d envc=%d\n",
                  frame_base, rsp_user, argc, envc);
    return rsp_user;
}

static int64_t sys_execve(uint64_t path_ptr, uint64_t argv_ptr, uint64_t envp_ptr) {
    task_t *t = cur_task();
    if (!t || !t->is_userspace) return -EPERM;

    char kpath[EXECVE_MAX_PATH];
    int plen = strncpy_from_user(kpath, (const char *)path_ptr, sizeof(kpath));
    if (plen < 0) return -EFAULT;
    if (plen == 0) return -ENOENT;

    serial_printf("[EXECVE] pid=%u execve(\"%s\")\n", t->pid, kpath);

    const char *kargv_ptrs[EXECVE_MAX_ARGS + 1];
    char (*kargv_store)[EXECVE_MAX_ARGLEN] =
        malloc(EXECVE_MAX_ARGS * EXECVE_MAX_ARGLEN);
    if (!kargv_store) return -ENOMEM;
    int argc = 0;

    if (argv_ptr) {
        for (;;) {
            if (argc >= EXECVE_MAX_ARGS) { free(kargv_store); return -E2BIG; }

            uint64_t uargv_slot = argv_ptr + (uint64_t)argc * 8;
            uint64_t arg_ptr = 0;
            if (copy_from_user(&arg_ptr, (const void *)uargv_slot,
                               sizeof(uint64_t)) < 0)
                { free(kargv_store); return -EFAULT; }

            if (arg_ptr == 0) break;

            int alen = strncpy_from_user(kargv_store[argc],
                                         (const char *)arg_ptr,
                                         EXECVE_MAX_ARGLEN);
            if (alen < 0) { free(kargv_store); return -EFAULT; }
            kargv_ptrs[argc] = kargv_store[argc];
            argc++;
        }
    }
    kargv_ptrs[argc] = NULL;

    if (argc == 0) {
        strncpy(kargv_store[0], kpath, EXECVE_MAX_ARGLEN - 1);
        kargv_store[0][EXECVE_MAX_ARGLEN - 1] = '\0';
        kargv_ptrs[0] = kargv_store[0];
        kargv_ptrs[1] = NULL;
        argc = 1;
    }

    const char *kenvp_ptrs[EXECVE_MAX_ARGS + 1];
    char (*kenvp_store)[EXECVE_MAX_ARGLEN] =
        malloc(EXECVE_MAX_ARGS * EXECVE_MAX_ARGLEN);
    if (!kenvp_store) { free(kargv_store); return -ENOMEM; }
    int envc = 0;

    if (envp_ptr) {
        for (;;) {
            if (envc >= EXECVE_MAX_ARGS) break;

            uint64_t uenv_slot = envp_ptr + (uint64_t)envc * 8;
            uint64_t env_ptr = 0;
            if (copy_from_user(&env_ptr, (const void *)uenv_slot,
                               sizeof(uint64_t)) < 0) break;

            if (env_ptr == 0) break;

            int elen = strncpy_from_user(kenvp_store[envc], (const char *)env_ptr, EXECVE_MAX_ARGLEN);
            if (elen < 0) break;
            kenvp_ptrs[envc] = kenvp_store[envc];
            envc++;
        }
    }
    kenvp_ptrs[envc] = NULL;

    vfs_file_t *vfile = NULL;
    int vret = vfs_open(kpath, O_RDONLY, 0, &vfile);
    if (vret < 0) {
        serial_printf("[EXECVE] vfs_open failed: %d\n", vret);
        free(kargv_store);
        free(kenvp_store);
        return (int64_t)vret;
    }

    vfs_stat_t st;
    if (vfs_fstat(vfile, &st) < 0 || st.st_size == 0) {
        vfs_close(vfile);
        free(kargv_store);
        free(kenvp_store);
        return -EIO;
    }

    size_t fsize = (size_t)st.st_size;
    uint8_t *elf_data = (uint8_t *)malloc(fsize);
    if (!elf_data) { vfs_close(vfile); free(kargv_store); free(kenvp_store); return -ENOMEM; }

    int64_t nread = vfs_read(vfile, elf_data, fsize);
    vfs_close(vfile);
    if (nread < 0 || (size_t)nread != fsize) {
        free(elf_data);
        free(kargv_store);
        free(kenvp_store);
        return -EIO;
    }

    elf_load_result_t elf = elf_load(elf_data, fsize, 0);
    free(elf_data);

    if (elf.error != ELF_OK) {
        serial_printf("[EXECVE] elf_load failed: %s\n", elf_strerror(elf.error));
        if (elf.pagemap) vmm_free_pagemap(elf.pagemap);
        free(kargv_store);
        free(kenvp_store);
        return -ENOEXEC;
    }

    uintptr_t new_rsp = execve_build_stack(elf.pagemap,
                                            elf.stack_top,
                                            kargv_ptrs, argc,
                                            kenvp_ptrs, envc,
                                            &elf);
    free(kargv_store);
    free(kenvp_store);
    if (!new_rsp) {
        vmm_free_pagemap(elf.pagemap);
        return -ENOMEM;
    }

    if (t->fd_table)
        fd_table_cloexec(t->fd_table);

    vmm_switch_pagemap(vmm_get_kernel_pagemap());

    if (t->pagemap && (t->flags & (TASK_FLAG_OWN_PAGEMAP | TASK_FLAG_FORK))) {
        vmm_free_pagemap(t->pagemap);
    }

    t->pagemap    = elf.pagemap;
    t->cr3        = (uint64_t)pmm_virt_to_phys(elf.pagemap->pml4);
    t->flags     |= TASK_FLAG_OWN_PAGEMAP;
    t->flags     &= ~TASK_FLAG_FORK;

    t->brk_start   = elf.load_end;
    t->brk_current = elf.load_end;
    t->brk_max     = 0x0000700000000000ULL;

    t->user_rsp       = new_rsp;
    t->user_saved_rip = elf.entry;
    t->user_saved_rbp = 0;
    t->user_saved_rbx = 0;
    t->user_saved_r12 = 0;
    t->user_saved_r13 = 0;
    t->user_saved_r14 = 0;
    t->user_saved_r15 = 0;
    t->user_saved_r11 = 0;

    const char *bname = kpath;
    for (const char *p = kpath; *p; p++)
        if (*p == '/') bname = p + 1;
    strncpy(t->name, bname, sizeof(t->name) - 1);
    t->name[sizeof(t->name) - 1] = '\0';

    percpu_t *pc = get_percpu();
    if (pc) {
        pc->syscall_user_rsp = new_rsp;
        pc->user_saved_rip   = elf.entry;
        pc->user_saved_rbp   = 0;
        pc->user_saved_rbx   = 0;
        pc->user_saved_r12   = 0;
        pc->user_saved_r13   = 0;
        pc->user_saved_r14   = 0;
        pc->user_saved_r15   = 0;
        pc->user_saved_r11   = 0x200;
    }

    vmm_switch_pagemap(t->pagemap);

    serial_printf("[EXECVE] exec ok: entry=0x%llx rsp=0x%llx name='%s'\n", elf.entry, new_rsp, t->name);

    return 0;
}

static int64_t sys_wait(uint64_t pid_arg, uint64_t status_ptr, uint64_t flags) {
    task_t* parent = cur_task();
    if (!parent) return -ESRCH;

retry:;
    task_t* zombie = NULL;
    task_t* child  = parent->children;
    while (child) {
        bool match = (pid_arg == (uint64_t)-1) || (child->pid == (uint32_t)pid_arg);
        if (match && child->state == TASK_ZOMBIE) {
            zombie = child;
            break;
        }
        child = child->sibling;
    }

    if (!zombie) {
        if (flags & WNOHANG) return 0;
        percpu_t* pc2 = get_percpu();
        if (pc2) parent->user_rsp = pc2->syscall_user_rsp;
        parent->wait_for_pid = (pid_arg == (uint64_t)-1) ? (uint32_t)-1 : (uint32_t)pid_arg;
        parent->runnable = false;
        parent->state    = TASK_BLOCKED;
        serial_printf("[WAIT] pid=%u blocking: user_rsp=0x%llx task_rsp=0x%llx\n",
                      parent->pid, parent->user_rsp, parent->rsp);
        sched_reschedule();
        goto retry;
    }

    if (status_ptr) {
        int status = (zombie->exit_code & 0xFF) << 8;
        if (copy_to_user((void*)status_ptr, &status, sizeof(int)) < 0)
            return -EFAULT;
    }

    uint32_t zpid = zombie->pid;

    task_t* prev  = NULL;
    task_t* cur_c = parent->children;
    while (cur_c) {
        if (cur_c == zombie) {
            if (prev) prev->sibling = zombie->sibling;
            else      parent->children = zombie->sibling;
            break;
        }
        prev  = cur_c;
        cur_c = cur_c->sibling;
    }

    task_destroy(zombie);

    serial_printf("[SYSCALL] wait: parent pid=%u reaped child pid=%u\n",
                  parent->pid, zpid);
    return (int64_t)zpid;
}

static int64_t sys_yield(void) {
    task_yield();
    return 0;
}

static int64_t sys_getuid(void) {
    task_t* t = cur_task();
    return t ? (int64_t)t->uid : -ESRCH;
}

static int64_t sys_getgid(void) {
    task_t* t = cur_task();
    return t ? (int64_t)t->gid : -ESRCH;
}

static int64_t sys_setuid(uint64_t new_uid) {
    task_t* t = cur_task();
    if (!t) return -ESRCH;

    if (t->uid != UID_ROOT && !cap_has(t->capabilities, CAP_SETUID))
        return -EPERM;
    if (new_uid > 65535) return -EINVAL;

    t->uid = (uint32_t)new_uid;

    if (new_uid != UID_ROOT)
        t->capabilities = cap_drop(t->capabilities, CAP_IOPORT | CAP_RAWMEM | CAP_SYSADMIN | CAP_REBOOT | CAP_MODULE | CAP_FS_ROOT);

    serial_printf("[SYSCALL] setuid: pid=%u uid=%u\n", t->pid, t->uid);
    return 0;
}

static int64_t sys_setgid(uint64_t new_gid) {
    task_t* t = cur_task();
    if (!t) return -ESRCH;
    if (t->uid != UID_ROOT && !cap_has(t->capabilities, CAP_SETUID))
        return -EPERM;
    if (new_gid > 65535) return -EINVAL;
    t->gid = (uint32_t)new_gid;
    return 0;
}

static int64_t sys_cap_get(void) {
    task_t* t = cur_task();
    return t ? (int64_t)(t->capabilities) : -ESRCH;
}

static int64_t sys_cap_drop(uint64_t mask) {
    task_t* t = cur_task();
    if (!t) return -ESRCH;
    t->capabilities = cap_drop(t->capabilities, mask);
    serial_printf("[SYSCALL] cap_drop: pid=%u caps=0x%llx\n",
                  t->pid, t->capabilities);
    return 0;
}

static int64_t sys_task_info(uint64_t pid_arg, uint64_t buf_ptr) {
    if (!buf_ptr) return -EINVAL;

    task_t* target = (pid_arg == 0) ? cur_task()
                                    : task_find_by_pid((uint32_t)pid_arg);
    if (!target) return -ESRCH;

    task_t* me = cur_task();
    if (me && me != target && !cap_has(me->capabilities, CAP_TASK_INFO))
        return -EPERM;

    cervus_task_info_t info;
    memset(&info, 0, sizeof(info));
    info.pid           = target->pid;
    info.ppid          = target->ppid;
    info.uid           = target->uid;
    info.gid           = target->gid;
    info.capabilities  = target->capabilities;
    info.state         = (uint32_t)target->state;
    info.priority      = (uint32_t)target->priority;
    info.total_runtime_ns = target->total_runtime;
    strncpy(info.name, target->name, sizeof(info.name) - 1);

    return copy_to_user((void*)buf_ptr, &info, sizeof(info));
}

static int64_t sys_write(uint64_t fd, uint64_t buf_ptr, uint64_t count) {
    if (fd != 1 && fd != 2) return -EBADF;
    if (count == 0)          return 0;
    if (count > 4096)        count = 4096;

    char kbuf[4097];
    if (copy_from_user(kbuf, (const void*)buf_ptr, count) < 0)
        return -EFAULT;
    kbuf[count] = '\0';

    static bool at_line_start = true;
    for (uint64_t i = 0; i < count; i++) {
        if (at_line_start) {
            serial_writestring("[USER] ");
            at_line_start = false;
        }
        serial_write(kbuf[i]);
        if (kbuf[i] == '\n')
            at_line_start = true;
    }

    return (int64_t)count;
}

static int64_t sys_read(uint64_t fd, uint64_t buf_ptr, uint64_t count) {
    (void)buf_ptr; (void)count;
    if (fd != 0) return -EBADF;
    return -EAGAIN;
}

static int64_t sys_brk(uint64_t new_brk) {
    task_t* t = cur_task();
    if (!t || !t->is_userspace) return -EINVAL;

    if (new_brk == 0)
        return (int64_t)t->brk_current;

    if (new_brk < t->brk_start)
        return (int64_t)t->brk_current;

    if (new_brk > t->brk_max)
        return (int64_t)t->brk_current;

    uintptr_t old_brk = t->brk_current;
    uintptr_t old_page = (old_brk + 0xFFFULL) & ~0xFFFULL;
    uintptr_t new_page = (new_brk + 0xFFFULL) & ~0xFFFULL;

    if (new_brk > old_brk) {
        for (uintptr_t p = old_page; p < new_page; p += 0x1000) {
            void* phys = pmm_alloc_zero(1);
            if (!phys) return (int64_t)t->brk_current;
            uintptr_t phys_addr = pmm_virt_to_phys(phys);
            if (!vmm_map_page(t->pagemap, p, phys_addr,
                              VMM_PRESENT | VMM_WRITE | VMM_USER | VMM_NOEXEC)) {
                pmm_free(phys, 1);
                return (int64_t)t->brk_current;
            }
        }
    } else if (new_brk < old_brk) {
        for (uintptr_t p = new_page; p < old_page; p += 0x1000)
            vmm_unmap_page(t->pagemap, p);
    }

    t->brk_current = new_brk;
    return (int64_t)new_brk;
}

static int64_t sys_mmap(uint64_t hint, uint64_t length, uint64_t prot, uint64_t flags, uint64_t fd, uint64_t offset) {
    (void)offset;

    task_t* t = cur_task();
    if (!t || !t->is_userspace) return (int64_t)MAP_FAILED;

    if (!(flags & MAP_ANONYMOUS))         return (int64_t)MAP_FAILED;
    if (fd != (uint64_t)-1 && fd != 0)   return (int64_t)MAP_FAILED;
    if (length == 0)                      return (int64_t)MAP_FAILED;

    size_t pages = (length + 0xFFFULL) >> 12;

    uintptr_t addr;
    if (hint && !(flags & MAP_FIXED)) {
        addr = hint & ~0xFFFULL;
    } else if (flags & MAP_FIXED) {
        if (!hint) return (int64_t)MAP_FAILED;
        addr = hint & ~0xFFFULL;
    } else {
        addr = (t->brk_max - (uint64_t)pages * 0x1000) & ~0xFFFULL;
        t->brk_max = addr;
    }

    uint64_t vmm_flags = VMM_PRESENT | VMM_USER;
    if (prot & PROT_WRITE) vmm_flags |= VMM_WRITE;
    if (!(prot & PROT_EXEC)) vmm_flags |= VMM_NOEXEC;

    for (size_t i = 0; i < pages; i++) {
        void* phys = pmm_alloc_zero(1);
        if (!phys) {
            for (size_t j = 0; j < i; j++)
                vmm_unmap_page(t->pagemap, addr + j * 0x1000);
            return (int64_t)MAP_FAILED;
        }
        if (!vmm_map_page(t->pagemap, addr + i * 0x1000,
                           pmm_virt_to_phys(phys), vmm_flags)) {
            pmm_free(phys, 1);
            for (size_t j = 0; j < i; j++)
                vmm_unmap_page(t->pagemap, addr + j * 0x1000);
            return (int64_t)MAP_FAILED;
        }
    }

    serial_printf("[SYSCALL] mmap: addr=0x%llx pages=%zu prot=0x%llx\n",
                  addr, pages, prot);
    return (int64_t)addr;
}

static int64_t sys_munmap(uint64_t addr, uint64_t length) {
    task_t* t = cur_task();
    if (!t || !t->is_userspace) return -EINVAL;
    if (addr & 0xFFF)           return -EINVAL;
    if (length == 0)            return -EINVAL;

    size_t pages = (length + 0xFFFULL) >> 12;
    for (size_t i = 0; i < pages; i++)
        vmm_unmap_page(t->pagemap, addr + i * 0x1000);

    return 0;
}

static int64_t sys_uptime(void) {
    return 0;
}

static int64_t sys_clock_get(uint64_t clk_id, uint64_t ts_ptr) {
    if (!ts_ptr) return -EINVAL;
    (void)clk_id;

    cervus_timespec_t ts;
    ts.tv_sec  = 0;
    ts.tv_nsec = 0;

    return copy_to_user((void*)ts_ptr, &ts, sizeof(ts));
}

static int64_t sys_sleep_ns(uint64_t ns) {
    (void)ns;
    task_yield();
    return 0;
}

static int64_t sys_dbg_print(uint64_t str_ptr, uint64_t len) {
    task_t* t = cur_task();
    if (!t) return -ESRCH;

    if (t->uid != UID_ROOT && !cap_has(t->capabilities, CAP_DBG_SERIAL))
        return -EPERM;

    if (len == 0)  return 0;
    if (len > 512) len = 512;

    char kbuf[513];
    if (copy_from_user(kbuf, (const void*)str_ptr, len) < 0)
        return -EFAULT;
    kbuf[len] = '\0';

    serial_printf("[DBG pid=%u] %s", t->pid, kbuf);
    return (int64_t)len;
}

static int64_t sys_task_kill(uint64_t pid_arg) {
    task_t* me     = cur_task();
    task_t* target = task_find_by_pid((uint32_t)pid_arg);
    if (!target) return -ESRCH;

    bool own_child = (target->ppid == (me ? me->pid : 0));
    if (!own_child && !cap_has(me ? me->capabilities : 0, CAP_KILL_ANY))
        return -EPERM;

    task_kill(target);
    return 0;
}

static int64_t sys_ioport_read(uint64_t port, uint64_t width) {
    task_t* t = cur_task();
    if (!t || !cap_has(t->capabilities, CAP_IOPORT)) return -EPERM;
    if (port > 0xFFFF) return -EINVAL;

    uint64_t val = 0;
    switch (width) {
        case 1: { uint8_t  v; asm volatile("inb %w1, %b0" : "=a"(v) : "Nd"((uint16_t)port)); val = v; break; }
        case 2: { uint16_t v; asm volatile("inw %w1, %w0" : "=a"(v) : "Nd"((uint16_t)port)); val = v; break; }
        case 4: { uint32_t v; asm volatile("inl %w1, %k0" : "=a"(v) : "Nd"((uint16_t)port)); val = v; break; }
        default: return -EINVAL;
    }
    return (int64_t)val;
}

static int64_t sys_ioport_write(uint64_t port, uint64_t width, uint64_t val) {
    task_t* t = cur_task();
    if (!t || !cap_has(t->capabilities, CAP_IOPORT)) return -EPERM;
    if (port > 0xFFFF) return -EINVAL;

    switch (width) {
        case 1: asm volatile("outb %b0, %w1" :: "a"((uint8_t)val),  "Nd"((uint16_t)port)); break;
        case 2: asm volatile("outw %w0, %w1" :: "a"((uint16_t)val), "Nd"((uint16_t)port)); break;
        case 4: asm volatile("outl %k0, %w1" :: "a"((uint32_t)val), "Nd"((uint16_t)port)); break;
        default: return -EINVAL;
    }
    return 0;
}

typedef int64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static int64_t _sys_exit(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)b;(void)c;(void)d;(void)e;(void)f;return sys_exit(a);}
static int64_t _sys_exit_group(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)b;(void)c;(void)d;(void)e;(void)f;return sys_exit_group(a);}
static int64_t _sys_getpid(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return sys_getpid();}
static int64_t _sys_getppid(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return sys_getppid();}
static int64_t _sys_fork(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return sys_fork();}
static int64_t _sys_execve(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)d;(void)e;(void)f;return sys_execve(a,b,c);}
static int64_t _sys_wait(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)d;(void)e;(void)f;return sys_wait(a,b,c);}
static int64_t _sys_yield(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return sys_yield();}
static int64_t _sys_getuid(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return sys_getuid();}
static int64_t _sys_getgid(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return sys_getgid();}
static int64_t _sys_setuid(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)b;(void)c;(void)d;(void)e;(void)f;return sys_setuid(a);}
static int64_t _sys_setgid(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)b;(void)c;(void)d;(void)e;(void)f;return sys_setgid(a);}
static int64_t _sys_cap_get(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return sys_cap_get();}
static int64_t _sys_cap_drop(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)b;(void)c;(void)d;(void)e;(void)f;return sys_cap_drop(a);}
static int64_t _sys_task_info(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)c;(void)d;(void)e;(void)f;return sys_task_info(a,b);}
static int64_t _sys_read(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)d;(void)e;(void)f;return sys_read(a,b,c);}
static int64_t _sys_write(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)d;(void)e;(void)f;return sys_write(a,b,c);}
static int64_t _sys_brk(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)b;(void)c;(void)d;(void)e;(void)f;return sys_brk(a);}
static int64_t _sys_mmap(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){return sys_mmap(a,b,c,d,e,f);}
static int64_t _sys_munmap(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)c;(void)d;(void)e;(void)f;return sys_munmap(a,b);}
static int64_t _sys_clock_get(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)c;(void)d;(void)e;(void)f;return sys_clock_get(a,b);}
static int64_t _sys_sleep_ns(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)b;(void)c;(void)d;(void)e;(void)f;return sys_sleep_ns(a);}
static int64_t _sys_uptime(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return sys_uptime();}
static int64_t _sys_dbg_print(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)c;(void)d;(void)e;(void)f;return sys_dbg_print(a,b);}
static int64_t _sys_task_kill(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)b;(void)c;(void)d;(void)e;(void)f;return sys_task_kill(a);}
static int64_t _sys_ioport_read(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)c;(void)d;(void)e;(void)f;return sys_ioport_read(a,b);}
static int64_t _sys_ioport_write(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)d;(void)e;(void)f;return sys_ioport_write(a,b,c);}

static const syscall_fn_t syscall_table[SYSCALL_TABLE_SIZE] = {
    [SYS_EXIT]       = _sys_exit,
    [SYS_EXIT_GROUP] = _sys_exit_group,
    [SYS_GETPID]     = _sys_getpid,
    [SYS_GETPPID]    = _sys_getppid,
    [SYS_FORK]       = _sys_fork,
    [SYS_EXECVE]     = _sys_execve,
    [SYS_WAIT]       = _sys_wait,
    [SYS_YIELD]      = _sys_yield,
    [SYS_GETUID]     = _sys_getuid,
    [SYS_GETGID]     = _sys_getgid,
    [SYS_SETUID]     = _sys_setuid,
    [SYS_SETGID]     = _sys_setgid,
    [SYS_CAP_GET]    = _sys_cap_get,
    [SYS_CAP_DROP]   = _sys_cap_drop,
    [SYS_TASK_INFO]  = _sys_task_info,
    [SYS_READ]       = _sys_read,
    [SYS_WRITE]      = _sys_write,
    [SYS_BRK]        = _sys_brk,
    [SYS_MMAP]       = _sys_mmap,
    [SYS_MUNMAP]     = _sys_munmap,
    [SYS_CLOCK_GET]  = _sys_clock_get,
    [SYS_SLEEP_NS]   = _sys_sleep_ns,
    [SYS_UPTIME]     = _sys_uptime,
    [SYS_DBG_PRINT]  = _sys_dbg_print,
    [SYS_TASK_KILL]  = _sys_task_kill,
    [SYS_IOPORT_READ]  = _sys_ioport_read,
    [SYS_IOPORT_WRITE] = _sys_ioport_write,
};

int64_t syscall_handler_c(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t user_rip) {
    task_t* t = cur_task();

    if (t) {
        percpu_t* pc = get_percpu();
        if (pc) {
            t->user_saved_rip = pc->user_saved_rip;
            t->user_saved_rbp = pc->user_saved_rbp;
            t->user_saved_rbx = pc->user_saved_rbx;
            t->user_saved_r12 = pc->user_saved_r12;
            t->user_saved_r13 = pc->user_saved_r13;
            t->user_saved_r14 = pc->user_saved_r14;
            t->user_saved_r15 = pc->user_saved_r15;
            t->user_saved_r11 = pc->user_saved_r11;
        } else {
            t->user_saved_rip = user_rip;
        }
    }

#ifdef SYSCALL_TRACE
    if (t && (t->flags & TASK_FLAG_TRACE)) {
        serial_printf("[STRACE] pid=%u nr=%llu a1=0x%llx a2=0x%llx a3=0x%llx\n",
                      t->pid, nr, a1, a2, a3);
    }
#endif

    if (nr >= SYSCALL_TABLE_SIZE || !syscall_table[nr]) {
        serial_printf("[SYSCALL] ENOSYS nr=%llu pid=%u\n",
                      nr, t ? t->pid : 0);
        return -ENOSYS;
    }

    int64_t ret = syscall_table[nr](a1, a2, a3, a4, a5, 0);

#ifdef SYSCALL_TRACE
    if (t && (t->flags & TASK_FLAG_TRACE)) {
        serial_printf("[STRACE] pid=%u nr=%llu → %lld\n", t->pid, nr, ret);
    }
#endif

    return ret;
}

void syscall_init(void) {
    extern void syscall_entry(void);

    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE | EFER_NXE);

    uint64_t star = ((uint64_t)GDT_STAR_SYSRET_BASE << 48)
                  | ((uint64_t)GDT_STAR_SYSCALL_CS  << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_SFMASK, (1U << 9) | (1U << 10) | (1U << 8) | (1U << 18));

    percpu_t* pc = get_percpu();
    if (!pc) {
        serial_printf("[SYSCALL] WARNING: no percpu, skipping kernel_rsp\n");
        return;
    }

    extern tss_t* tss[MAX_CPUS];
    cpu_info_t* cpu_info = smp_get_current_cpu();
    if (!cpu_info) return;

    uint32_t idx = cpu_info->cpu_index;
    if (idx < MAX_CPUS && tss[idx]) {
        pc->syscall_kernel_rsp = tss[idx]->rsp0;
        serial_printf("[SYSCALL] CPU %u (index %u): kernel_rsp=0x%llx\n",
                      pc->cpu_id, idx, pc->syscall_kernel_rsp);
    }
}