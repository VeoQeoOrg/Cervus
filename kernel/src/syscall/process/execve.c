#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/elf/elf.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/smp/percpu.h"
#include "../../../include/io/serial.h"
#include <string.h>
#include <stdlib.h>

#define EXECVE_MAX_PATH   512
#define EXECVE_MAX_ARGS   128
#define EXECVE_MAX_ARGLEN 4096
#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_ENTRY  9

static uintptr_t execve_build_stack(vmm_pagemap_t *map, uintptr_t stack_top,
                                    const char *argv[], int argc,
                                    const elf_load_result_t *elf)
{
    size_t str_total = 0;
    for (int i = 0; i < argc; i++) str_total += strlen(argv[i]) + 1;

    size_t n_auxv      = 6;
    size_t ptr_count   = 1 + (size_t)argc + 1 + 1 + (n_auxv * 2);
    size_t frame_bytes = ptr_count * 8;

    uintptr_t aligned_top  = stack_top & ~(uintptr_t)0xF;
    uintptr_t strings_base = (aligned_top - str_total) & ~(uintptr_t)0xF;

    uintptr_t candidate = strings_base - frame_bytes;
    uintptr_t new_rsp   = ((candidate - 8) & ~(uintptr_t)0xF) + 8;

    if (new_rsp + frame_bytes > strings_base) {
        new_rsp -= 16;
    }

    uintptr_t page_base   = new_rsp & ~(uintptr_t)0xFFF;
    uintptr_t page_end    = (stack_top + 0xFFFULL) & ~(uintptr_t)0xFFF;
    size_t    total_pages = (page_end - page_base) >> 12;
    size_t    kbuf_size   = total_pages * 0x1000;

    if (new_rsp + frame_bytes > strings_base ||
        strings_base + str_total > stack_top) {
        return 0;
    }

    uint8_t *kbuf = (uint8_t *)malloc(kbuf_size);
    if (!kbuf) return 0;
    memset(kbuf, 0, kbuf_size);

    uint64_t argv_user[EXECVE_MAX_ARGS + 1];
    size_t   str_off = 0;
    for (int i = 0; i < argc; i++) {
        size_t slen = strlen(argv[i]) + 1;
        memcpy(kbuf + (strings_base + str_off - page_base), argv[i], slen);
        argv_user[i] = strings_base + str_off;
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

    memcpy(kbuf + (new_rsp - page_base), frame, fi * 8);

    for (size_t pi = 0; pi < total_pages; pi++) {
        uintptr_t virt = page_base + pi * 0x1000;
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
            phys &= ~(uintptr_t)0xFFF;
        }
        memcpy(pmm_phys_to_virt(phys), kbuf + pi * 0x1000, 0x1000);
    }

    serial_printf("[EXECVE] stack built: page_base=0x%llx rsp=0x%llx argc=%d envc=0\n",
                  (unsigned long long)page_base, (unsigned long long)new_rsp, argc);
    free(kbuf);
    return new_rsp;
}

int64_t sys_execve(uint64_t path_ptr, uint64_t argv_ptr, uint64_t envp_ptr)
{
    (void)envp_ptr;
    task_t *t = syscall_cur_task();
    if (!t || !t->is_userspace) return -EPERM;

    char kpath[EXECVE_MAX_PATH];
    if (syscall_strncpy_from_user(kpath, (const char *)path_ptr, sizeof(kpath)) < 0) return -EFAULT;
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
            if (syscall_copy_from_user(&aptr, (const void *)uslot, 8) < 0)
                { free(kargv_store); return -EFAULT; }
            if (!aptr) break;
            if (syscall_strncpy_from_user(kargv_store[argc], (const char *)aptr, EXECVE_MAX_ARGLEN) < 0)
                { free(kargv_store); return -EFAULT; }
            kargv_ptrs[argc] = kargv_store[argc]; argc++;
        }
    }
    kargv_ptrs[argc] = NULL;
    if (argc == 0) {
        strncpy(kargv_store[0], kpath, EXECVE_MAX_ARGLEN - 1);
        kargv_store[0][EXECVE_MAX_ARGLEN - 1] = '\0';
        kargv_ptrs[0] = kargv_store[0]; kargv_ptrs[1] = NULL; argc = 1;
    }

    vfs_file_t *vfile = NULL;
    int vret = vfs_open(kpath, O_RDONLY, 0, &vfile);
    if (vret < 0) { serial_printf("[EXECVE] open failed: %d\n", vret); free(kargv_store); return (int64_t)vret; }
    vfs_stat_t st;
    if (vfs_fstat(vfile, &st) < 0 || st.st_size == 0) {
        serial_printf("[EXECVE] fstat/size failed: path='%s' size=%llu\n",
                      kpath, (unsigned long long)st.st_size);
        vfs_close(vfile); free(kargv_store); return -EIO;
    }
    size_t fsize = (size_t)st.st_size;
    uint8_t *elf_data = malloc(fsize);
    if (!elf_data) {
        serial_printf("[EXECVE] malloc(%zu) failed for path='%s'\n", fsize, kpath);
        vfs_close(vfile); free(kargv_store); return -ENOMEM;
    }
    int64_t nr = vfs_read(vfile, elf_data, fsize); vfs_close(vfile);
    if (nr < 0 || (size_t)nr != fsize) {
        serial_printf("[EXECVE] read failed: path='%s' expected=%zu got=%lld\n",
                      kpath, fsize, (long long)nr);
        free(elf_data); free(kargv_store); return -EIO;
    }
    if (fsize < 4 || elf_data[0] != 0x7F || elf_data[1] != 'E' ||
        elf_data[2] != 'L' || elf_data[3] != 'F') {
        serial_printf("[EXECVE] not an ELF: path='%s' magic=%02x%02x%02x%02x\n",
                      kpath,
                      fsize > 0 ? elf_data[0] : 0,
                      fsize > 1 ? elf_data[1] : 0,
                      fsize > 2 ? elf_data[2] : 0,
                      fsize > 3 ? elf_data[3] : 0);
        free(elf_data); free(kargv_store); return -ENOEXEC;
    }

    elf_load_result_t elf = elf_load(elf_data, fsize, 0);
    free(elf_data);
    if (elf.error != ELF_OK) {
        serial_printf("[EXECVE] elf_load: %s\n", elf_strerror(elf.error));
        if (elf.pagemap) vmm_free_pagemap(elf.pagemap);
        free(kargv_store); return -ENOEXEC;
    }

    uintptr_t new_rsp = execve_build_stack(elf.pagemap, elf.stack_top, kargv_ptrs, argc, &elf);
    free(kargv_store);
    if (!new_rsp) { vmm_free_pagemap(elf.pagemap); return -ENOMEM; }

    if (t->fd_table) fd_table_cloexec(t->fd_table);

    vmm_switch_pagemap(vmm_get_kernel_pagemap());

    vmm_pagemap_t *old_pagemap = t->pagemap;
    uint32_t old_flags = t->flags;

    t->pagemap    = elf.pagemap;
    t->cr3        = (uint64_t)pmm_virt_to_phys(elf.pagemap->pml4);
    t->flags     |= TASK_FLAG_OWN_PAGEMAP;
    t->flags     &= ~TASK_FLAG_FORK;
    t->brk_start  = t->brk_current = elf.load_end;
    t->brk_max    = 0x0000700000000000ULL;

    t->user_rsp       = new_rsp;
    t->user_saved_rip = elf.entry;
    t->user_saved_rbp = t->user_saved_rbx = 0;
    t->user_saved_r12 = t->user_saved_r13 = t->user_saved_r14 = 0;
    t->user_saved_r15 = t->user_saved_r11 = 0;

    const char *bn = kpath;
    for (const char *p = kpath; *p; p++) if (*p == '/') bn = p + 1;
    strncpy(t->name, bn, sizeof(t->name) - 1);
    t->name[sizeof(t->name) - 1] = '\0';

    percpu_t *pc = get_percpu();
    if (pc) {
        pc->syscall_user_rsp = new_rsp;
        pc->user_saved_rip   = elf.entry;
        pc->user_saved_rbp = pc->user_saved_rbx = 0;
        pc->user_saved_r12 = pc->user_saved_r13 = pc->user_saved_r14 = 0;
        pc->user_saved_r15 = 0;
        pc->user_saved_r11 = 0x200;
    }

    if (old_pagemap && (old_flags & (TASK_FLAG_OWN_PAGEMAP | TASK_FLAG_FORK)))
        vmm_free_pagemap(old_pagemap);

    asm volatile("lock addl $0, (%%rsp)" ::: "memory", "cc");
    vmm_switch_pagemap(t->pagemap);
    serial_printf("[EXECVE] exec ok: entry=0x%llx rsp=0x%llx name='%s'\n",
                  elf.entry, new_rsp, t->name);
    return 0;
}
