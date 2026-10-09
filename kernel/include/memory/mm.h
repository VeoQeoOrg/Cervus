#ifndef MM_H
#define MM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "vmm.h"
#include "../sched/kmutex.h"

struct vnode;

#define VM_READ       0x0001u
#define VM_WRITE      0x0002u
#define VM_EXEC       0x0004u
#define VM_SHARED     0x0008u
#define VM_GROWSDOWN  0x0010u
#define VM_PFNMAP     0x0020u
#define VM_FILE       0x0040u
#define VM_STACK      0x0080u
#define VM_HEAP       0x0100u
#define VM_DONTCOPY   0x0200u
#define VM_LOCKED     0x0400u
#define VM_PROT_MASK  (VM_READ | VM_WRITE | VM_EXEC)

#define MM_USER_TOP     0x0000800000000000ULL
#define MM_MMAP_TOP     0x0000700000000000ULL
#define MM_STACK_TOP    0x00007FFFFFFFE000ULL
#define MM_STACK_SIZE   (8ULL << 20)
#define MM_STACK_MIN    (128ULL << 10)
#define MM_STACK_MAX    (1ULL << 30)

typedef struct vm_area vm_area_t;

typedef int  (*vm_pfn_fn)(vm_area_t *vma, uint64_t offset, uintptr_t *phys_out);
typedef void (*vm_hook_fn)(vm_area_t *vma);

struct vm_area {
    vm_area_t     *next;
    vm_area_t     *prev;
    uintptr_t      start;
    uintptr_t      end;
    uint32_t       flags;
    uint64_t       pte_extra;
    struct vnode  *vnode;
    uint64_t       offset;
    vm_pfn_fn      pfn;
    vm_hook_fn     open;
    vm_hook_fn     close;
    void          *priv;
    uint64_t       priv_id;
};

typedef struct mm {
    vmm_pagemap_t *pagemap;
    uint64_t       cr3;
    vm_area_t     *vmas;
    kmutex_t       lock;
    volatile int   users;
    uintptr_t      brk_start;
    uintptr_t      brk;
    uintptr_t      mmap_top;
    struct vnode  *exe;
    uint32_t       def_flags;
} mm_t;

#define MM_REMAP_MAYMOVE   0x1
#define MM_REMAP_FIXED     0x2
#define MM_REMAP_DONTUNMAP 0x4

#define MM_FAULT_WRITE 0x1
#define MM_FAULT_EXEC  0x2
#define MM_FAULT_USER  0x4

mm_t      *mm_create(void);
void       mm_get(mm_t *mm);
bool       mm_tryget(mm_t *mm);
void       mm_put(mm_t *mm);
mm_t      *mm_fork(mm_t *parent);

int        mm_fault(mm_t *mm, uintptr_t addr, int how);
uint64_t   mm_locked_bytes(mm_t *mm);
int        mm_populate(mm_t *mm, uintptr_t addr, size_t len, int write);
int        mm_mlock(mm_t *mm, uintptr_t addr, size_t len, bool lock);
int        mm_mlockall(mm_t *mm, int flags, bool lock);
bool       mm_access_ok(mm_t *mm, uintptr_t addr, size_t len, int write);
int        mm_user_phys(mm_t *mm, uintptr_t addr, int write, uintptr_t *phys_out);

int64_t    mm_map_anon(mm_t *mm, uintptr_t addr, size_t len, uint32_t flags, bool fixed);
int64_t    mm_map_file(mm_t *mm, uintptr_t addr, size_t len, uint32_t flags, bool fixed,
                       struct vnode *vn, uint64_t offset);
int64_t    mm_map_pfn(mm_t *mm, uintptr_t addr, size_t len, uint32_t flags, bool fixed,
                      uint64_t pte_extra, vm_pfn_fn pfn, vm_hook_fn open, vm_hook_fn close,
                      void *priv, uint64_t priv_id, struct vnode *vn, uint64_t offset);
int        mm_unmap(mm_t *mm, uintptr_t addr, size_t len);
int        mm_protect(mm_t *mm, uintptr_t addr, size_t len, uint32_t prot);
int        mm_sync(mm_t *mm, uintptr_t addr, size_t len);
int64_t    mm_remap(mm_t *mm, uintptr_t old, size_t old_len, size_t new_len, int flags, uintptr_t new_addr);
int        mm_mincore(mm_t *mm, uintptr_t addr, size_t len, uint8_t *out);
int        mm_discard(mm_t *mm, uintptr_t addr, size_t len);
uintptr_t  mm_set_brk(mm_t *mm, uintptr_t new_brk);
void       mm_init_heap(mm_t *mm, uintptr_t start);
int        mm_setup_stack(mm_t *mm);
int        mm_copy_to(mm_t *mm, uintptr_t dst, const void *src, size_t len);

vm_area_t *mm_find_vma(mm_t *mm, uintptr_t addr);
bool       mm_range_free(mm_t *mm, uintptr_t start, uintptr_t end);
uint64_t   mm_vm_bytes(mm_t *mm);
int        mm_format_maps(mm_t *mm, char *buf, size_t max);
int        mm_set_exe(mm_t *mm, struct vnode *vn);

#endif
