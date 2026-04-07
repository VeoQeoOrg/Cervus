#include "../../include/syscall/syscall.h"
#include "../../include/syscall/syscall_nums.h"
#include "../../include/syscall/errno.h"
#include "../../include/acpi/acpi.h"
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
#include "../../include/panic/panic.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
    return pc ? (task_t*)pc->current_task : NULL;
}

static void save_user_regs(task_t* t) {
    if (!t) return;
    percpu_t* pc = get_percpu();
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

static bool uptr_validate(const void* ptr, size_t len) {
    uintptr_t addr = (uintptr_t)ptr;
    if (addr < 0x1000ULL) return false;
    if (addr >= 0x0000800000000000ULL) return false;
    if (len > 0x0000800000000000ULL) return false;
    if (len && addr + len - 1 < addr) return false;
    return true;
}

static int copy_from_user(void* dst, const void* src, size_t n) {
    if (!uptr_validate(src, n)) return -EFAULT;
    memcpy(dst, src, n); return 0;
}
static int copy_to_user(void* dst, const void* src, size_t n) {
    if (!uptr_validate(dst, n)) return -EFAULT;
    memcpy(dst, src, n); return 0;
}
static int strncpy_from_user(char* dst, const char* src, size_t max) {
    if (!uptr_validate(src, 1)) return -EFAULT;
    for (size_t i = 0; i < max - 1; i++) {
        if ((i == 0) || (!((uintptr_t)(src+i) & 0xFFF)))
            if (!uptr_validate(src+i, 1)) return -EFAULT;
        dst[i] = src[i];
        if (!dst[i]) return (int)i;
    }
    dst[max-1] = '\0';
    return (int)(max-1);
}

static int64_t sys_exit(uint64_t code) {
    task_t* t = cur_task();
    if (t) t->exit_code = (int)(uint8_t)code;
    serial_printf("[SYSCALL] exit(%llu) task='%s' pid=%u\n",
                  code, t?t->name:"?", t?t->pid:0);
    vmm_switch_pagemap(vmm_get_kernel_pagemap());
    task_exit();
}
static int64_t sys_exit_group(uint64_t code) { return sys_exit(code); }

static int64_t sys_getpid(void)  { task_t*t=cur_task(); return t?(int64_t)t->pid:-ESRCH; }
static int64_t sys_getppid(void) { task_t*t=cur_task(); return t?(int64_t)t->ppid:-ESRCH; }
static int64_t sys_getuid(void)  { task_t*t=cur_task(); return t?(int64_t)t->uid:-ESRCH; }
static int64_t sys_getgid(void)  { task_t*t=cur_task(); return t?(int64_t)t->gid:-ESRCH; }

static int64_t sys_setuid(uint64_t u) {
    task_t*t=cur_task(); if(!t) return -ESRCH;
    if(t->uid!=UID_ROOT && !cap_has(t->capabilities,CAP_SETUID)) return -EPERM;
    if(u>65535) return -EINVAL;
    t->uid=(uint32_t)u; return 0;
}
static int64_t sys_setgid(uint64_t g) {
    task_t*t=cur_task(); if(!t) return -ESRCH;
    if(t->uid!=UID_ROOT && !cap_has(t->capabilities,CAP_SETUID)) return -EPERM;
    if(g>65535) return -EINVAL;
    t->gid=(uint32_t)g; return 0;
}

static int64_t sys_cap_get(void) {
    task_t*t=cur_task(); return t?(int64_t)t->capabilities:-ESRCH;
}
static int64_t sys_cap_drop(uint64_t mask) {
    task_t*t=cur_task(); if(!t) return -ESRCH;
    t->capabilities=cap_drop(t->capabilities,mask);
    serial_printf("[SYSCALL] cap_drop: pid=%u caps=0x%llx\n",t->pid,t->capabilities);
    return 0;
}

static int64_t sys_task_info(uint64_t pid_arg, uint64_t buf_ptr) {
    if (!buf_ptr) return -EINVAL;
    task_t* target = (pid_arg==0)?cur_task():task_find_by_pid((uint32_t)pid_arg);
    if (!target) return -ESRCH;
    task_t* me = cur_task();
    if (me && me!=target && !cap_has(me->capabilities,CAP_TASK_INFO)) return -EPERM;
    cervus_task_info_t info; memset(&info,0,sizeof(info));
    info.pid=target->pid; info.ppid=target->ppid;
    info.uid=target->uid; info.gid=target->gid;
    info.capabilities=target->capabilities;
    info.state=(uint32_t)target->state; info.priority=(uint32_t)target->priority;
    info.total_runtime_ns=target->total_runtime;
    strncpy(info.name,target->name,sizeof(info.name)-1);
    return copy_to_user((void*)buf_ptr,&info,sizeof(info));
}

static int64_t sys_task_kill(uint64_t pid_arg) {
    task_t*me=cur_task();
    task_t*target=task_find_by_pid((uint32_t)pid_arg);
    if (!target) return -ESRCH;
    bool own=(target->ppid==(me?me->pid:0));
    if (!own && !cap_has(me?me->capabilities:0,CAP_KILL_ANY)) return -EPERM;
    task_kill(target); return 0;
}

static int64_t sys_fork(void) {
    task_t*parent=cur_task(); if(!parent) return -ESRCH;
    save_user_regs(parent);
    task_t*child=task_fork(parent); if(!child) return -ENOMEM;
    serial_printf("[SYSCALL] fork: parent pid=%u → child pid=%u\n",
                  parent->pid,child->pid);
    return (int64_t)child->pid;
}

static int64_t sys_yield(void) { task_yield(); return 0; }

static int64_t sys_wait(uint64_t pid_arg, uint64_t status_ptr, uint64_t flags) {
    task_t*parent=cur_task(); if(!parent) return -ESRCH;
retry:;
    task_t*zombie=NULL;
    {
        uint64_t _cf = spinlock_acquire_irqsave(&children_lock);
        task_t*child=parent->children;
        bool has_children = (child != NULL);
        while (child) {
            bool match=(pid_arg==(uint64_t)-1)||(child->pid==(uint32_t)pid_arg);
            if (match && child->state==TASK_ZOMBIE) { zombie=child; break; }
            child=child->sibling;
        }
        spinlock_release_irqrestore(&children_lock, _cf);

        (void)has_children;
    }

    if (!zombie) {
        if (flags & WNOHANG) return 0;
        save_user_regs(parent);
        parent->wait_for_pid=(pid_arg==(uint64_t)-1)?(uint32_t)-1:(uint32_t)pid_arg;
        parent->runnable=false; parent->state=TASK_BLOCKED;
        if (pid_arg != (uint64_t)-1)
            task_set_foreground((uint32_t)pid_arg);

        serial_printf("[WAIT] pid=%u blocking: user_rsp=0x%llx task_rsp=0x%llx\n",
                      parent->pid,parent->user_rsp,parent->rsp);
        sched_reschedule(); goto retry;
    }

    if (status_ptr) {
        int status=(zombie->exit_code&0xFF)<<8;
        if (copy_to_user((void*)status_ptr,&status,sizeof(int))<0) return -EFAULT;
    }
    uint32_t zpid=zombie->pid;

    {
        uint64_t _cf = spinlock_acquire_irqsave(&children_lock);
        if (parent->children == zombie) {
            parent->children = zombie->sibling;
        } else {
            task_t *prev = parent->children;
            while (prev && prev->sibling != zombie) prev = prev->sibling;
            if (prev) prev->sibling = zombie->sibling;
        }
        zombie->sibling = NULL;
        zombie->parent  = NULL;
        spinlock_release_irqrestore(&children_lock, _cf);
    }

    serial_printf("[SYSCALL] wait: parent pid=%u reaped child pid=%u\n",
                  parent->pid,zpid);

    if (g_foreground_pid == zpid)
        task_set_foreground(0);
    task_destroy(zombie);
    serial_printf("[SYSCALL] wait: task_destroy done, returning %u\n", zpid);
    serial_printf("[WAIT-RET] parent pid=%u user_saved_rip=0x%llx user_rsp=0x%llx\n",
                  parent->pid, parent->user_saved_rip, parent->user_rsp);
    return (int64_t)zpid;
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

static uintptr_t execve_build_stack(vmm_pagemap_t *map, uintptr_t stack_top, const char *argv[], int argc, const elf_load_result_t *elf) {
    size_t str_total = 0;
    for (int i = 0; i < argc; i++) str_total += strlen(argv[i]) + 1;

    size_t n_auxv   = 6;
    size_t ptr_count = 1
                     + (size_t)argc
                     + 1
                     + 1
                     + (n_auxv * 2);
    size_t frame_size = str_total + ptr_count * 8 + 64;
    size_t frame_pages = (frame_size + 0xFFFULL) >> 12;

    uint8_t *kbuf = (uint8_t*)malloc(frame_pages * 0x1000);
    if (!kbuf) return 0;
    memset(kbuf, 0, frame_pages * 0x1000);

    uintptr_t frame_base = (stack_top - frame_pages * 0x1000) & ~0xFULL;

    uint64_t argv_user[EXECVE_MAX_ARGS + 1];
    size_t str_off = 0;
    for (int i = 0; i < argc; i++) {
        size_t slen = strlen(argv[i]) + 1;
        memcpy(kbuf + str_off, argv[i], slen);
        argv_user[i] = frame_base + str_off;
        str_off += slen;
    }

    uint64_t frame[256];
    size_t fi = 0;

    frame[fi++] = (uint64_t)argc;
    for (int i = 0; i < argc; i++) frame[fi++] = argv_user[i];
    frame[fi++] = 0;
    frame[fi++] = 0;

    frame[fi++] = AT_PHDR;   frame[fi++] = elf->load_base + 0x40;
    frame[fi++] = AT_PHENT;  frame[fi++] = 56;
    frame[fi++] = AT_PHNUM;  frame[fi++] = 0;
    frame[fi++] = AT_ENTRY;  frame[fi++] = elf->entry;
    frame[fi++] = AT_PAGESZ; frame[fi++] = 4096;
    frame[fi++] = AT_NULL;   frame[fi++] = 0;

    size_t frame_bytes = fi * 8;
    size_t rsp_offset  = (frame_pages * 0x1000 - frame_bytes) & ~0xFULL;
    memcpy(kbuf + rsp_offset, frame, frame_bytes);
    uintptr_t new_rsp = frame_base + rsp_offset;

    for (size_t pi = 0; pi < frame_pages; pi++) {
        uintptr_t virt = frame_base + pi * 0x1000;
        uintptr_t phys = 0;
        uint64_t  pf   = 0;

        if (!vmm_get_page_flags(map, virt, &pf) || !(pf & VMM_PRESENT)) {
            void *pg = pmm_alloc_zero(1);
            if (!pg) { free(kbuf); return 0; }
            phys = pmm_virt_to_phys(pg);
            if (!vmm_map_page(map, virt, phys,
                              VMM_PRESENT | VMM_WRITE | VMM_USER | VMM_NOEXEC)) {
                pmm_free(pg, 1);
                free(kbuf);
                return 0;
            }
        } else {
            if (!vmm_virt_to_phys(map, virt, &phys))
                { free(kbuf); return 0; }
        }
        memcpy(pmm_phys_to_virt(phys), kbuf + pi * 0x1000, 0x1000);
    }

    serial_printf("[EXECVE] stack built: frame_base=0x%llx rsp=0x%llx argc=%d envc=0\n",
                  frame_base, new_rsp, argc);
    free(kbuf);
    return new_rsp;
}

static int64_t sys_execve(uint64_t path_ptr, uint64_t argv_ptr, uint64_t envp_ptr) {
    (void)envp_ptr;
    task_t *t = cur_task();
    if (!t || !t->is_userspace) return -EPERM;

    char kpath[EXECVE_MAX_PATH];
    if (strncpy_from_user(kpath, (const char*)path_ptr, sizeof(kpath)) < 0) return -EFAULT;
    if (!kpath[0]) return -ENOENT;
    serial_printf("[EXECVE] pid=%u execve(\"%s\")\n", t->pid, kpath);

    const char *kargv_ptrs[EXECVE_MAX_ARGS + 1];
    char (*kargv_store)[EXECVE_MAX_ARGLEN] = malloc(EXECVE_MAX_ARGS * EXECVE_MAX_ARGLEN);
    if (!kargv_store) return -ENOMEM;
    int argc = 0;

    if (argv_ptr) {
        for (;;) {
            if (argc >= EXECVE_MAX_ARGS) { free(kargv_store); return -E2BIG; }
            uint64_t uslot = argv_ptr + (uint64_t)argc * 8;
            uint64_t aptr  = 0;
            if (copy_from_user(&aptr, (const void*)uslot, 8) < 0)
                { free(kargv_store); return -EFAULT; }
            if (!aptr) break;
            if (strncpy_from_user(kargv_store[argc], (const char*)aptr, EXECVE_MAX_ARGLEN) < 0)
                { free(kargv_store); return -EFAULT; }
            kargv_ptrs[argc] = kargv_store[argc]; argc++;
        }
    }
    kargv_ptrs[argc] = NULL;
    if (argc == 0) {
        strncpy(kargv_store[0], kpath, EXECVE_MAX_ARGLEN-1);
        kargv_store[0][EXECVE_MAX_ARGLEN-1] = '\0';
        kargv_ptrs[0] = kargv_store[0]; kargv_ptrs[1] = NULL; argc = 1;
    }

    vfs_file_t *vfile = NULL;
    int vret = vfs_open(kpath, O_RDONLY, 0, &vfile);
    if (vret < 0) { serial_printf("[EXECVE] open failed: %d\n",vret); free(kargv_store); return (int64_t)vret; }
    vfs_stat_t st;
    if (vfs_fstat(vfile,&st)<0 || st.st_size==0) { vfs_close(vfile); free(kargv_store); return -EIO; }
    size_t fsize = (size_t)st.st_size;
    uint8_t *elf_data = malloc(fsize);
    if (!elf_data) { vfs_close(vfile); free(kargv_store); return -ENOMEM; }
    int64_t nr = vfs_read(vfile, elf_data, fsize); vfs_close(vfile);
    if (nr<0 || (size_t)nr!=fsize) { free(elf_data); free(kargv_store); return -EIO; }

    elf_load_result_t elf = elf_load(elf_data, fsize, 0); free(elf_data);
    if (elf.error != ELF_OK) {
        serial_printf("[EXECVE] elf_load: %s\n",elf_strerror(elf.error));
        if (elf.pagemap) vmm_free_pagemap(elf.pagemap);
        free(kargv_store); return -ENOEXEC;
    }

    uintptr_t new_rsp = execve_build_stack(elf.pagemap, elf.stack_top, kargv_ptrs, argc, &elf);
    free(kargv_store);
    if (!new_rsp) { vmm_free_pagemap(elf.pagemap); return -ENOMEM; }

    if (t->fd_table) fd_table_cloexec(t->fd_table);

    vmm_switch_pagemap(vmm_get_kernel_pagemap());

    vmm_pagemap_t* old_pagemap = t->pagemap;
    uint32_t old_flags = t->flags;

    t->pagemap    = elf.pagemap;
    t->cr3        = (uint64_t)pmm_virt_to_phys(elf.pagemap->pml4);
    t->flags     |= TASK_FLAG_OWN_PAGEMAP;
    t->flags     &= ~TASK_FLAG_FORK;
    t->brk_start = t->brk_current = elf.load_end;
    t->brk_max   = 0x0000700000000000ULL;

    t->user_rsp       = new_rsp;
    t->user_saved_rip = elf.entry;
    t->user_saved_rbp = t->user_saved_rbx = 0;
    t->user_saved_r12 = t->user_saved_r13 = t->user_saved_r14 = 0;
    t->user_saved_r15 = t->user_saved_r11 = 0;

    const char *bn = kpath;
    for (const char *p=kpath;*p;p++) if (*p=='/') bn=p+1;
    strncpy(t->name, bn, sizeof(t->name)-1); t->name[sizeof(t->name)-1]='\0';

    percpu_t *pc = get_percpu();
    if (pc) {
        pc->syscall_user_rsp = new_rsp;
        pc->user_saved_rip   = elf.entry;
        pc->user_saved_rbp = pc->user_saved_rbx = 0;
        pc->user_saved_r12 = pc->user_saved_r13 = pc->user_saved_r14 = 0;
        pc->user_saved_r15 = 0;
        pc->user_saved_r11 = 0x200;
    }

    if (old_pagemap && (old_flags & (TASK_FLAG_OWN_PAGEMAP|TASK_FLAG_FORK)))
        vmm_free_pagemap(old_pagemap);

    asm volatile("lock addl $0, (%%rsp)" ::: "memory", "cc");
    vmm_switch_pagemap(t->pagemap);
    serial_printf("[EXECVE] exec ok: entry=0x%llx rsp=0x%llx name='%s'\n",
                  elf.entry, new_rsp, t->name);
    return 0;
}

static int64_t sys_write(uint64_t fd, uint64_t buf_ptr, uint64_t count) {
    if (count == 0) return 0;
    if (count > 4096) count = 4096;

    char kbuf[4097];
    if (copy_from_user(kbuf, (const void*)buf_ptr, count) < 0) return -EFAULT;
    kbuf[count] = '\0';

    task_t *t = cur_task();
    if (t && t->fd_table) {
        vfs_file_t *file = fd_get(t->fd_table, (int)fd);
        if (file)
            return vfs_write(file, kbuf, count);
    }

    if (fd != 1 && fd != 2) return -EBADF;

    {
        static bool at_line_start = true;
        uint64_t i = 0;
        while (i < count) {
            uint64_t j = i;
            while (j < count && kbuf[j] != '\n') j++;
            bool has_newline = (j < count && kbuf[j] == '\n');

            char chunk[4096 + 8];
            size_t clen = 0;
            if (at_line_start && j > i) {
                chunk[clen++] = '['; chunk[clen++] = 'U';
                chunk[clen++] = 'S'; chunk[clen++] = 'E';
                chunk[clen++] = 'R'; chunk[clen++] = ']';
                chunk[clen++] = ' ';
            }
            size_t seg = j - i;
            if (seg > 0) {
                __builtin_memcpy(chunk + clen, kbuf + i, seg);
                clen += seg;
            }
            if (has_newline) {
                chunk[clen++] = '\n';
                at_line_start = true;
                i = j + 1;
            } else {
                if (seg > 0) at_line_start = false;
                i = j;
            }
            if (clen > 0) {
                serial_writebuf(chunk, clen);
                printf("%.*s", (int)clen, chunk);
            }
            if (!has_newline) break;
        }
    }
    return (int64_t)count;
}

static int64_t sys_read(uint64_t fd, uint64_t buf_ptr, uint64_t count) {
    if (count == 0) return 0;
    if (count > 65536) count = 65536;

    task_t *t = cur_task();
    if (!t) return -ESRCH;
    if (!uptr_validate((void*)buf_ptr, count)) return -EFAULT;

    vfs_file_t *file = NULL;
    if (t->fd_table) file = fd_get(t->fd_table, (int)fd);
    if (!file) return -EBADF;

    char kbuf[4096];
    size_t chunk = count > 4096 ? 4096 : count;
    int64_t r = vfs_read(file, kbuf, chunk);
    if (r <= 0) return r;
    memcpy((void*)buf_ptr, kbuf, (size_t)r);
    return r;
}

static int64_t sys_open(uint64_t path_ptr, uint64_t flags, uint64_t mode) {
    task_t *t = cur_task();
    if (!t) return -ESRCH;
    if (!t->fd_table) return -ENOMEM;

    char kpath[VFS_MAX_PATH];
    if (strncpy_from_user(kpath, (const char*)path_ptr, sizeof(kpath)) < 0) return -EFAULT;
    if (!kpath[0]) return -ENOENT;

    vfs_file_t *file = NULL;
    int ret = vfs_open(kpath, (int)flags, (uint32_t)mode, &file);
    if (ret < 0) return (int64_t)ret;

    int newfd = fd_alloc(t->fd_table, file, 0);
    if (newfd < 0) { vfs_close(file); return -EMFILE; }
    return (int64_t)newfd;
}

static int64_t sys_close(uint64_t fd) {
    task_t *t = cur_task();
    if (!t || !t->fd_table) return -EBADF;
    return (int64_t)fd_close(t->fd_table, (int)fd);
}

static int64_t sys_seek(uint64_t fd, uint64_t offset, uint64_t whence) {
    task_t *t = cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    return vfs_seek(f, (int64_t)offset, (int)whence);
}

static int64_t sys_stat(uint64_t path_ptr, uint64_t stat_ptr) {
    if (!stat_ptr) return -EINVAL;
    char kpath[VFS_MAX_PATH];
    if (strncpy_from_user(kpath, (const char*)path_ptr, sizeof(kpath)) < 0) return -EFAULT;
    vfs_stat_t st;
    int r = vfs_stat(kpath, &st);
    if (r < 0) return (int64_t)r;
    return copy_to_user((void*)stat_ptr, &st, sizeof(st));
}

static int64_t sys_fstat(uint64_t fd, uint64_t stat_ptr) {
    if (!stat_ptr) return -EINVAL;
    task_t *t = cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    vfs_stat_t st;
    int r = vfs_fstat(f, &st);
    if (r < 0) return (int64_t)r;
    return copy_to_user((void*)stat_ptr, &st, sizeof(st));
}

#define IOCTL_KBUF_MAX 64

#define TIOCGWINSZ   0x5413
#define TIOCGCURSOR  0x5480

static size_t ioctl_out_size(uint64_t request) {
    switch (request) {
        case TIOCGWINSZ:  return 8;
        case TIOCGCURSOR: return 8;
        default:          return 0;
    }
}

static int64_t sys_ioctl(uint64_t fd, uint64_t request, uint64_t arg_ptr) {
    task_t *t = cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;

    size_t out_sz = ioctl_out_size(request);

    if (arg_ptr) {
        size_t validate_sz = out_sz ? out_sz : IOCTL_KBUF_MAX;
        if (!uptr_validate((void *)arg_ptr, validate_sz))
            return -EFAULT;
    }

    char kbuf[IOCTL_KBUF_MAX];
    memset(kbuf, 0, sizeof(kbuf));

    int64_t r = vfs_ioctl(f, request, arg_ptr ? (void *)kbuf : (void *)0);
    if (r < 0) return r;

    if (arg_ptr && out_sz > 0) {
        if (copy_to_user((void *)arg_ptr, kbuf, out_sz) < 0)
            return -EFAULT;
    }
    return r;
}

static int64_t sys_readdir(uint64_t fd, uint64_t dirent_ptr) {
    if (!dirent_ptr) return -EINVAL;
    task_t *t = cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    vfs_dirent_t kd;
    int r = vfs_readdir(f, &kd);
    if (r < 0) return (int64_t)r;
    return copy_to_user((void*)dirent_ptr, &kd, sizeof(kd));
}

static int64_t sys_dup(uint64_t fd) {
    task_t *t = cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    if (f->vnode && f->vnode->ops && f->vnode->ops->ref)
        f->vnode->ops->ref(f->vnode);
    int nfd = fd_alloc(t->fd_table, f, 0);
    if (nfd < 0) {
        if (f->vnode && f->vnode->ops && f->vnode->ops->unref)
            f->vnode->ops->unref(f->vnode);
        return -EMFILE;
    }
    return (int64_t)nfd;
}

static int64_t sys_dup2(uint64_t oldfd, uint64_t newfd) {
    task_t *t = cur_task();
    if (!t || !t->fd_table) return -EBADF;
    int r = fd_dup2(t->fd_table, (int)oldfd, (int)newfd);
    return r < 0 ? (int64_t)r : (int64_t)newfd;
}

#define PIPE_BUFSZ 4096

typedef struct {
    char     buf[PIPE_BUFSZ];
    uint32_t head, tail;
    int      readers, writers;
    uint32_t reader_waiting_pid;
    spinlock_t lock;
} pipe_shared_t;

typedef struct {
    pipe_shared_t *shared;
    int            end;
} pipe_vdata_t;

static int64_t pipe_read_op(vnode_t *n, void *buf, size_t len, uint64_t off) {
    (void)off;
    pipe_vdata_t  *vd = (pipe_vdata_t*)n->fs_data;
    pipe_shared_t *ps = vd->shared;
    size_t got = 0;
    char *dst = (char*)buf;
    while (got < len) {
        if (ps->head == ps->tail) {
            if (ps->writers == 0) break;
            if (got > 0) break;

            task_t *me = cur_task();
            if (me) {
                save_user_regs(me);
                ps->reader_waiting_pid = me->pid;
                me->runnable = false;
                me->state    = TASK_BLOCKED;
            }
            sched_reschedule();
            if (me) ps->reader_waiting_pid = 0;
            if (me && me->pending_kill) return got > 0 ? (int64_t)got : -EINTR;
            continue;
        }
        dst[got++] = ps->buf[ps->head];
        ps->head = (ps->head + 1) % PIPE_BUFSZ;
    }
    return (int64_t)got;
}

static int64_t pipe_write_op(vnode_t *n, const void *buf, size_t len, uint64_t off) {
    (void)off;
    pipe_vdata_t  *vd = (pipe_vdata_t*)n->fs_data;
    pipe_shared_t *ps = vd->shared;
    if (ps->readers == 0) return -EPIPE;
    const char *src = (const char*)buf;
    for (size_t i = 0; i < len; i++) {
        uint32_t next = (ps->tail + 1) % PIPE_BUFSZ;
        int spins = 0;
        while (next == ps->head) {
            if (ps->readers == 0) return (i > 0) ? (int64_t)i : -EPIPE;
            task_t *me = cur_task();
            if (me) save_user_regs(me);
            task_yield();
            if (me && me->pending_kill) return (i > 0) ? (int64_t)i : -EINTR;
            next = (ps->tail + 1) % PIPE_BUFSZ;
            spins++;
            if (spins > 10000) break;
        }
        if (next == ps->head) {
            return (i > 0) ? (int64_t)i : -EAGAIN;
        }
        ps->buf[ps->tail] = src[i];
        ps->tail = next;
        if (ps->reader_waiting_pid) {
            task_t *reader = task_find_by_pid(ps->reader_waiting_pid);
            if (reader && !reader->runnable) {
                task_unblock(reader);
            }
        }
    }
    return (int64_t)len;
}

static int pipe_stat_op(vnode_t *n, vfs_stat_t *out) {
    memset(out,0,sizeof(*out));
    out->st_ino  = n->ino;
    out->st_type = VFS_NODE_PIPE;
    return 0;
}

static void pipe_ref_op(vnode_t *n)   { (void)n; }

static void pipe_unref_op(vnode_t *n) {
    pipe_vdata_t  *vd = (pipe_vdata_t*)n->fs_data;
    pipe_shared_t *ps = vd->shared;
    if (vd->end == 0) ps->readers--;
    else {
        ps->writers--;
        if (ps->reader_waiting_pid) {
            task_t *reader = task_find_by_pid(ps->reader_waiting_pid);
            if (reader && !reader->runnable) {
                task_unblock(reader);
            }
            ps->reader_waiting_pid = 0;
        }
    }

    int r = ps->readers;
    int w = ps->writers;

    free(vd); free(n);

    if (r <= 0 && w <= 0)
        free(ps);
}

static const vnode_ops_t pipe_read_ops = {
    .read   = pipe_read_op,
    .stat   = pipe_stat_op,
    .ref    = pipe_ref_op,
    .unref  = pipe_unref_op,
};
static const vnode_ops_t pipe_write_ops = {
    .write  = pipe_write_op,
    .stat   = pipe_stat_op,
    .ref    = pipe_ref_op,
    .unref  = pipe_unref_op,
};

static int64_t sys_pipe(uint64_t fds_ptr) {
    if (!uptr_validate((void*)fds_ptr, 2*sizeof(int))) return -EFAULT;

    task_t *t = cur_task();
    if (!t || !t->fd_table) return -ENOMEM;

    pipe_shared_t *ps = (pipe_shared_t*)malloc(sizeof(pipe_shared_t));
    if (!ps) return -ENOMEM;
    memset(ps, 0, sizeof(*ps));
    ps->readers = 1; ps->writers = 1;

    vnode_t    *rv = (vnode_t*)   malloc(sizeof(vnode_t));
    vnode_t    *wv = (vnode_t*)   malloc(sizeof(vnode_t));
    pipe_vdata_t*rd = (pipe_vdata_t*)malloc(sizeof(pipe_vdata_t));
    pipe_vdata_t*wd = (pipe_vdata_t*)malloc(sizeof(pipe_vdata_t));
    if (!rv||!wv||!rd||!wd) {
        free(ps);free(rv);free(wv);free(rd);free(wd); return -ENOMEM;
    }
    memset(rv,0,sizeof(*rv)); memset(wv,0,sizeof(*wv));
    rd->shared=ps; rd->end=0;
    wd->shared=ps; wd->end=1;

    static uint64_t pipe_ino = 0x10000;
    rv->type=VFS_NODE_PIPE; rv->mode=0600; rv->ino=pipe_ino++;
    rv->ops=&pipe_read_ops; rv->fs_data=rd; rv->refcount=1;
    wv->type=VFS_NODE_PIPE; wv->mode=0600; wv->ino=pipe_ino++;
    wv->ops=&pipe_write_ops; wv->fs_data=wd; wv->refcount=1;

    vfs_file_t *rf = vfs_file_alloc();
    vfs_file_t *wf = vfs_file_alloc();
    if (!rf||!wf) {
        free(ps);free(rv);free(wv);free(rd);free(wd);
        if(rf) vfs_file_free(rf);
        if(wf) vfs_file_free(wf);
        return -ENOMEM;
    }
    rf->vnode=rv; rf->flags=O_RDONLY; rf->offset=0; rf->refcount=1;
    wf->vnode=wv; wf->flags=O_WRONLY; wf->offset=0; wf->refcount=1;

    int rfd = fd_alloc(t->fd_table, rf, 0);
    int wfd = fd_alloc(t->fd_table, wf, 0);
    if (rfd<0||wfd<0) {
        fd_close(t->fd_table,rfd); fd_close(t->fd_table,wfd); return -EMFILE;
    }

    int fds[2] = {rfd, wfd};
    memcpy((void*)fds_ptr, fds, sizeof(fds));
    return 0;
}

#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4

static int64_t sys_fcntl(uint64_t fd, uint64_t cmd, uint64_t arg) {
    task_t *t = cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    switch (cmd) {
        case F_GETFD: return (int64_t)fd_get_flags(t->fd_table,(int)fd);
        case F_SETFD: return (int64_t)fd_set_flags(t->fd_table,(int)fd,(int)arg);
        case F_GETFL: return (int64_t)f->flags;
        case F_SETFL: f->flags=(f->flags & O_ACCMODE)|((int)arg & ~O_ACCMODE); return 0;
        default: return -EINVAL;
    }
}

static int64_t sys_brk(uint64_t new_brk) {
    task_t *t = cur_task();
    if (!t || !t->is_userspace) return -EINVAL;
    if (!new_brk) return (int64_t)t->brk_current;
    if (new_brk < t->brk_start || new_brk > t->brk_max) return (int64_t)t->brk_current;

    uintptr_t old_brk  = t->brk_current;
    uintptr_t old_page = (old_brk  + 0xFFFULL) & ~0xFFFULL;
    uintptr_t new_page = (new_brk  + 0xFFFULL) & ~0xFFFULL;

    if (new_brk > old_brk) {
        for (uintptr_t p = old_page; p < new_page; p += 0x1000) {
            void *ph = pmm_alloc_zero(1);
            if (!ph) return (int64_t)t->brk_current;
            if (!vmm_map_page(t->pagemap, p, pmm_virt_to_phys(ph),
                              VMM_PRESENT|VMM_WRITE|VMM_USER|VMM_NOEXEC))
                { pmm_free(ph,1); return (int64_t)t->brk_current; }
        }
    } else {
        for (uintptr_t p = new_page; p < old_page; p += 0x1000)
            vmm_unmap_page(t->pagemap, p);
    }
    t->brk_current = new_brk;
    return (int64_t)new_brk;
}

static int64_t sys_mmap(uint64_t hint, uint64_t length, uint64_t prot, uint64_t flags, uint64_t fd, uint64_t offset) {
    (void)offset;
    task_t *t = cur_task();
    if (!t || !t->is_userspace) return (int64_t)MAP_FAILED;
    if (!(flags & MAP_ANONYMOUS)) return (int64_t)MAP_FAILED;
    if (fd!=(uint64_t)-1 && fd!=0) return (int64_t)MAP_FAILED;
    if (!length) return (int64_t)MAP_FAILED;

    size_t pages = (length + 0xFFFULL) >> 12;
    uintptr_t addr;
    if (flags & MAP_FIXED)       addr = hint & ~0xFFFULL;
    else if (hint)               addr = hint & ~0xFFFULL;
    else { addr = (t->brk_max - (uint64_t)pages*0x1000) & ~0xFFFULL; t->brk_max = addr; }

    uint64_t vf = VMM_PRESENT|VMM_USER;
    if (prot & PROT_WRITE) vf |= VMM_WRITE;
    if (!(prot & PROT_EXEC)) vf |= VMM_NOEXEC;

    for (size_t i = 0; i < pages; i++) {
        void *ph = pmm_alloc_zero(1);
        if (!ph) { for (size_t j=0;j<i;j++) vmm_unmap_page(t->pagemap,addr+j*0x1000); return (int64_t)MAP_FAILED; }
        if (!vmm_map_page(t->pagemap, addr+i*0x1000, pmm_virt_to_phys(ph), vf)) {
            pmm_free(ph,1); for(size_t j=0;j<i;j++) vmm_unmap_page(t->pagemap,addr+j*0x1000); return (int64_t)MAP_FAILED;
        }
    }
    serial_printf("[SYSCALL] mmap: addr=0x%llx pages=%zu prot=0x%llx\n", addr, pages, prot);
    return (int64_t)addr;
}

static int64_t sys_munmap(uint64_t addr, uint64_t length) {
    task_t *t = cur_task();
    if (!t||!t->is_userspace||addr&0xFFF||!length) return -EINVAL;
    size_t pages = (length+0xFFFULL)>>12;
    for (size_t i=0;i<pages;i++) vmm_unmap_page(t->pagemap, addr+i*0x1000);
    return 0;
}

static int64_t sys_uptime(void)     { return (int64_t)hpet_elapsed_ns(); }

static int64_t sys_meminfo(uint64_t buf_ptr) {
    if (!buf_ptr) return -EINVAL;
    cervus_meminfo_t info;
    uint64_t usable = (uint64_t)pmm_get_usable_pages() * PAGE_SIZE;
    uint64_t free   = (uint64_t)pmm_get_free_pages()   * PAGE_SIZE;
    if (free > usable) free = usable;
    info.total_bytes  = usable;
    info.free_bytes   = free;
    info.used_bytes   = usable - free;
    info.usable_bytes = usable;
    info.page_size    = PAGE_SIZE;
    return copy_to_user((void*)buf_ptr, &info, sizeof(info));
}
static int64_t sys_sleep_ns(uint64_t ns) {
    if (ns == 0) return 0;
    if (!hpet_is_available()) {
        task_yield();
        return 0;
    }
    task_t *me = cur_task();
    if (!me) return -ESRCH;

    save_user_regs(me);

    serial_printf("[SLEEP] pid=%u sleeping %llu ns\n", me->pid, ns);

    uint64_t now = hpet_elapsed_ns();
    me->wakeup_time_ns = now + ns;
    me->runnable = false;
    me->state    = TASK_BLOCKED;

    sched_reschedule();

    serial_printf("[SLEEP] pid=%u woke up\n", me ? me->pid : 0);
    return 0;
}
static int64_t sys_clock_get(uint64_t id, uint64_t ts_ptr) {
    (void)id; if (!ts_ptr) return -EINVAL;
    uint64_t ns = hpet_elapsed_ns();
    cervus_timespec_t ts;
    ts.tv_sec  = (int64_t)(ns / 1000000000ULL);
    ts.tv_nsec = (int64_t)(ns % 1000000000ULL);
    return copy_to_user((void*)ts_ptr, &ts, sizeof(ts));
}

static int64_t sys_dbg_print(uint64_t str, uint64_t len) {
    task_t *t = cur_task(); if (!t) return -ESRCH;
    if (t->uid!=UID_ROOT && !cap_has(t->capabilities,CAP_DBG_SERIAL)) return -EPERM;
    if (!len) return 0;
    if (len>512) len=512;
    char kbuf[513];
    if (copy_from_user(kbuf,(const void*)str,len)<0) return -EFAULT;
    kbuf[len]='\0'; serial_printf("[DBG pid=%u] %s",t->pid,kbuf);
    return (int64_t)len;
}

static int64_t sys_ioport_read(uint64_t port, uint64_t width) {
    task_t *t=cur_task(); if(!t||!cap_has(t->capabilities,CAP_IOPORT)) return -EPERM;
    if (port>0xFFFF) return -EINVAL;
    uint64_t v=0;
    switch(width){
        case 1:{uint8_t  x;asm volatile("inb %w1,%b0":"=a"(x):"Nd"((uint16_t)port));v=x;break;}
        case 2:{uint16_t x;asm volatile("inw %w1,%w0":"=a"(x):"Nd"((uint16_t)port));v=x;break;}
        case 4:{uint32_t x;asm volatile("inl %w1,%k0":"=a"(x):"Nd"((uint16_t)port));v=x;break;}
        default:return -EINVAL;
    }
    return (int64_t)v;
}
static int64_t sys_ioport_write(uint64_t port, uint64_t width, uint64_t val) {
    task_t *t=cur_task(); if(!t||!cap_has(t->capabilities,CAP_IOPORT)) return -EPERM;
    if (port>0xFFFF) return -EINVAL;
    switch(width){
        case 1:asm volatile("outb %b0,%w1"::"a"((uint8_t)val),"Nd"((uint16_t)port));break;
        case 2:asm volatile("outw %w0,%w1"::"a"((uint16_t)val),"Nd"((uint16_t)port));break;
        case 4:asm volatile("outl %k0,%w1"::"a"((uint32_t)val),"Nd"((uint16_t)port));break;
        default:return -EINVAL;
    }
    return 0;
}

static int64_t sys_shutdown(void) {
    task_t *t = cur_task();
    if (t && t->uid != 0) return -EPERM;
    serial_writestring("[SYSCALL] shutdown requested\n");
    acpi_shutdown();
    return 0;
}

static int64_t sys_reboot(void) {
    task_t *t = cur_task();
    if (t && t->uid != 0) return -EPERM;
    serial_writestring("[SYSCALL] reboot requested\n");
    acpi_reboot();
    return 0;
}

typedef int64_t (*syscall_fn_t)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);

#define W0(fn) static int64_t _##fn(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return fn();}
#define W1(fn) static int64_t _##fn(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)b;(void)c;(void)d;(void)e;(void)f;return fn(a);}
#define W2(fn) static int64_t _##fn(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)c;(void)d;(void)e;(void)f;return fn(a,b);}
#define W3(fn) static int64_t _##fn(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)d;(void)e;(void)f;return fn(a,b,c);}
#define W6(fn) static int64_t _##fn(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){return fn(a,b,c,d,e,f);}

W1(sys_exit)        W1(sys_exit_group)
W0(sys_getpid)      W0(sys_getppid)
W0(sys_getuid)      W0(sys_getgid)
W1(sys_setuid)      W1(sys_setgid)
W0(sys_fork)        W0(sys_yield)
W0(sys_cap_get)     W1(sys_cap_drop)
W2(sys_task_info)   W1(sys_task_kill)
W3(sys_read)        W3(sys_write)
W3(sys_open)        W1(sys_close)
W3(sys_seek)        W2(sys_stat)
W2(sys_fstat)       W1(sys_dup)
W2(sys_dup2)        W1(sys_pipe)
W3(sys_fcntl)
W3(sys_ioctl)
W2(sys_readdir)
W1(sys_brk)         W6(sys_mmap)
W2(sys_munmap)
W2(sys_clock_get)   W1(sys_sleep_ns)   W0(sys_uptime)   W1(sys_meminfo)
W2(sys_dbg_print)
W2(sys_ioport_read) W3(sys_ioport_write)
W0(sys_shutdown) W0(sys_reboot)

static int64_t _sys_execve(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)d;(void)e;(void)f;return sys_execve(a,b,c);}
static int64_t _sys_wait  (uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)d;(void)e;(void)f;return sys_wait(a,b,c);}

static const syscall_fn_t syscall_table[SYSCALL_TABLE_SIZE] = {
    [SYS_EXIT]         = _sys_exit,
    [SYS_EXIT_GROUP]   = _sys_exit_group,
    [SYS_GETPID]       = _sys_getpid,
    [SYS_GETPPID]      = _sys_getppid,
    [SYS_GETUID]       = _sys_getuid,
    [SYS_GETGID]       = _sys_getgid,
    [SYS_SETUID]       = _sys_setuid,
    [SYS_SETGID]       = _sys_setgid,
    [SYS_FORK]         = _sys_fork,
    [SYS_EXECVE]       = _sys_execve,
    [SYS_WAIT]         = _sys_wait,
    [SYS_YIELD]        = _sys_yield,
    [SYS_CAP_GET]      = _sys_cap_get,
    [SYS_CAP_DROP]     = _sys_cap_drop,
    [SYS_TASK_INFO]    = _sys_task_info,
    [SYS_TASK_KILL]    = _sys_task_kill,
    [SYS_READ]         = _sys_read,
    [SYS_WRITE]        = _sys_write,
    [SYS_OPEN]         = _sys_open,
    [SYS_CLOSE]        = _sys_close,
    [SYS_SEEK]         = _sys_seek,
    [SYS_STAT]         = _sys_stat,
    [SYS_FSTAT]        = _sys_fstat,
    [SYS_IOCTL]        = _sys_ioctl,
    [SYS_DUP]          = _sys_dup,
    [SYS_DUP2]         = _sys_dup2,
    [SYS_PIPE]         = _sys_pipe,
    [SYS_FCNTL]        = _sys_fcntl,
    [SYS_READDIR]      = _sys_readdir,
    [SYS_BRK]          = _sys_brk,
    [SYS_MMAP]         = _sys_mmap,
    [SYS_MUNMAP]       = _sys_munmap,
    [SYS_CLOCK_GET]    = _sys_clock_get,
    [SYS_SLEEP_NS]     = _sys_sleep_ns,
    [SYS_UPTIME]       = _sys_uptime,
    [SYS_MEMINFO]      = _sys_meminfo,
    [SYS_DBG_PRINT]    = _sys_dbg_print,
    [SYS_IOPORT_READ]  = _sys_ioport_read,
    [SYS_IOPORT_WRITE] = _sys_ioport_write,
    [SYS_SHUTDOWN]     = _sys_shutdown,
    [SYS_REBOOT]       = _sys_reboot,
};

__attribute__((noreturn)) void sysret_bad_rip_panic(uint64_t bad_rip, uint64_t retval) {
    task_t *t = cur_task();
    serial_printf("[SYSRET-PANIC] Non-canonical user RIP=0x%llx before SYSRET!\n"
                  "  syscall retval=0x%llx task=%s pid=%u\n"
                  "  user_saved_rip=0x%llx user_rsp=0x%llx cr3=0x%llx\n",
                  bad_rip, retval,
                  t ? t->name : "?", t ? t->pid : 0,
                  t ? t->user_saved_rip : 0,
                  t ? t->user_rsp : 0,
                  t ? t->cr3 : 0);
    kernel_panic("SYSRET: non-canonical user RIP — would have caused triple fault");
}

__attribute__((noreturn)) void sysret_bad_rsp_panic(uint64_t bad_rsp, uint64_t user_rip) {
    task_t *t = cur_task();
    serial_printf("[SYSRET-PANIC] Non-canonical user RSP=0x%llx before SYSRET!\n"
                  "  user_rip=0x%llx task=%s pid=%u\n"
                  "  user_saved_rip=0x%llx user_rsp=0x%llx cr3=0x%llx\n",
                  bad_rsp, user_rip,
                  t ? t->name : "?", t ? t->pid : 0,
                  t ? t->user_saved_rip : 0,
                  t ? t->user_rsp : 0,
                  t ? t->cr3 : 0);
    kernel_panic("SYSRET: non-canonical user RSP — would have caused triple fault");
}

int64_t syscall_handler_c(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t user_rip) {
    (void)user_rip;
    task_t *t = cur_task();
    if (t) {
        save_user_regs(t);
    }
    if (nr >= SYSCALL_TABLE_SIZE || !syscall_table[nr]) {
        serial_printf("[SYSCALL] unknown nr=%llu\n", nr);
        return -ENOSYS;
    }

    int64_t ret = syscall_table[nr](a1, a2, a3, a4, a5, 0);

    task_t *me = cur_task();
    if (me && me->pending_kill) {
        me->pending_kill = false;
        me->exit_code = 130;
        vmm_switch_pagemap(vmm_get_kernel_pagemap());
        task_exit();
    }
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

    percpu_t *pc = get_percpu();
    if (!pc) {
        serial_printf("[SYSCALL] WARNING: no percpu, skipping kernel_rsp\n");
        return;
    }

    extern tss_t *tss[MAX_CPUS];
    cpu_info_t *cpu_info = smp_get_current_cpu();
    if (!cpu_info) return;

    uint32_t idx = cpu_info->cpu_index;
    if (idx < MAX_CPUS && tss[idx]) {
        pc->syscall_kernel_rsp = tss[idx]->rsp0;
        serial_printf("[SYSCALL] CPU %u (index %u): kernel_rsp=0x%llx\n",
                      pc->cpu_id, idx, pc->syscall_kernel_rsp);
    }
}