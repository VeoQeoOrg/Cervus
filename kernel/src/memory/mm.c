#include "../../include/sched/sched.h"
#include "../../include/memory/mm.h"
#include "../../include/memory/pmm.h"
#include "../../include/memory/pageref.h"
#include "../../include/memory/pagecache.h"
#include "../../include/memory/tlb.h"
#include "../../include/fs/vfs.h"
#include "../../include/io/serial.h"
#include "../../include/syscall/errno.h"
#include <string.h>
#include <stdio.h>

#define PTE_ADDR 0x000FFFFFFFFFF000ULL
#define PG       4096ULL

static inline uintptr_t pdown(uintptr_t a) { return a & ~(PG - 1); }
static inline uintptr_t pup(uintptr_t a)   { return (a + PG - 1) & ~(PG - 1); }

typedef struct {
    mm_t     *mm;
    uintptr_t addrs[TLB_SYNC_MAX_ADDRS];
    size_t    n;
    bool      all;
} flushbuf_t;

static void fb_add(flushbuf_t *fb, uintptr_t va) {
    if (fb->all) return;
    if (fb->n < TLB_SYNC_MAX_ADDRS) fb->addrs[fb->n++] = va;
    else fb->all = true;
}

static inline uint64_t cur_cr3(void) {
    uint64_t v;
    asm volatile("mov %%cr3, %0" : "=r"(v));
    return v & ~0xFFFULL;
}

static void fb_done(flushbuf_t *fb) {
    if (!fb->all && !fb->n) return;
    bool alone = __atomic_load_n(&fb->mm->users, __ATOMIC_ACQUIRE) <= 1 && cur_cr3() == fb->mm->cr3;
    if (alone) {
        if (fb->all) asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
        else for (size_t i = 0; i < fb->n; i++) asm volatile("invlpg (%0)" :: "r"(fb->addrs[i]) : "memory");
    } else if (fb->all) {
        tlb_flush_mm(fb->mm->cr3, NULL, 0);
    } else {
        tlb_flush_mm(fb->mm->cr3, fb->addrs, fb->n);
    }
    fb->n = 0;
    fb->all = false;
}

static uint64_t leaf_bits(const vm_area_t *v, bool writable) {
    uint64_t b = VMM_PRESENT | VMM_USER;
    if (writable) b |= VMM_WRITE;
    if (!(v->flags & VM_EXEC)) b |= VMM_NOEXEC;
    return b;
}

typedef void (*pte_cb)(vmm_pte_t *pte, uintptr_t va, void *ctx);

static void walk_ptes(vmm_pagemap_t *pm, uintptr_t start, uintptr_t end, pte_cb cb, void *ctx) {
    uintptr_t va = start;
    while (va < end) {
        vmm_pte_t e4 = pm->pml4[(va >> 39) & 511];
        if (!(e4 & VMM_PRESENT)) { va = (va | ((1ULL << 39) - 1)) + 1; continue; }
        vmm_pte_t *pdpt = (vmm_pte_t *)pmm_phys_to_virt(e4 & PTE_ADDR);
        vmm_pte_t e3 = pdpt[(va >> 30) & 511];
        if (!(e3 & VMM_PRESENT) || (e3 & VMM_PSE)) { va = (va | ((1ULL << 30) - 1)) + 1; continue; }
        vmm_pte_t *pd = (vmm_pte_t *)pmm_phys_to_virt(e3 & PTE_ADDR);
        vmm_pte_t e2 = pd[(va >> 21) & 511];
        if (!(e2 & VMM_PRESENT) || (e2 & VMM_PSE)) { va = (va | ((1ULL << 21) - 1)) + 1; continue; }
        vmm_pte_t *pt = (vmm_pte_t *)pmm_phys_to_virt(e2 & PTE_ADDR);
        uintptr_t stop = (va | ((1ULL << 21) - 1)) + 1;
        if (stop > end || stop == 0) stop = end;
        for (; va < stop; va += PG) {
            vmm_pte_t *pte = &pt[(va >> 12) & 511];
            if (*pte & VMM_PRESENT) cb(pte, va, ctx);
        }
    }
}

static vm_area_t *vma_new(void) { return kzalloc(sizeof(vm_area_t)); }

static void vma_release(vm_area_t *v) {
    if (v->close) v->close(v);
    if (v->vnode) vnode_unref(v->vnode);
    kfree(v);
}

vm_area_t *mm_find_vma(mm_t *mm, uintptr_t addr) {
    for (vm_area_t *v = mm->vmas; v; v = v->next) {
        if (addr < v->start) return NULL;
        if (addr < v->end) return v;
    }
    return NULL;
}

static void vma_link(mm_t *mm, vm_area_t *n) {
    vm_area_t *prev = NULL, *cur = mm->vmas;
    while (cur && cur->start < n->start) { prev = cur; cur = cur->next; }
    n->prev = prev;
    n->next = cur;
    if (prev) prev->next = n; else mm->vmas = n;
    if (cur) cur->prev = n;
}

static void vma_unlink(mm_t *mm, vm_area_t *v) {
    if (v->prev) v->prev->next = v->next; else mm->vmas = v->next;
    if (v->next) v->next->prev = v->prev;
    v->next = v->prev = NULL;
}

static vm_area_t *vma_split(mm_t *mm, vm_area_t *v, uintptr_t at) {
    vm_area_t *n = vma_new();
    if (!n) return NULL;
    *n = *v;
    n->next = n->prev = NULL;
    n->start = at;
    if (v->flags & (VM_FILE | VM_PFNMAP)) n->offset = v->offset + (at - v->start);
    if (n->vnode) vnode_ref(n->vnode);
    if (n->open) n->open(n);
    v->end = at;
    vma_link(mm, n);
    return n;
}

static bool range_free(mm_t *mm, uintptr_t start, uintptr_t end) {
    for (vm_area_t *v = mm->vmas; v; v = v->next) {
        if (v->start >= end) break;
        if (v->end > start) return false;
    }
    return true;
}

static uintptr_t find_gap(mm_t *mm, size_t len) {
    uintptr_t floor = mm->brk ? pup(mm->brk) + 0x10000000ULL : 0x10000000ULL;
    vm_area_t *tail = mm->vmas;
    while (tail && tail->next) tail = tail->next;
    uintptr_t cur_end = mm->mmap_top;
    for (vm_area_t *v = tail; v; v = v->prev) {
        if (v->start >= cur_end) continue;
        if (v->end <= cur_end) {
            uintptr_t lo = v->end > floor ? v->end : floor;
            if (cur_end > lo && cur_end - lo >= len) return cur_end - len;
        }
        cur_end = v->start;
        if (cur_end <= floor) return 0;
    }
    if (cur_end > floor && cur_end - floor >= len) return cur_end - len;
    return 0;
}

typedef struct {
    mm_t       *mm;
    vm_area_t  *vma;
    flushbuf_t *fb;
} zap_ctx_t;

static void writeback_page(vm_area_t *v, uintptr_t va, uintptr_t phys) {
    vnode_t *vn = v->vnode;
    if (!vn || !vn->ops || !vn->ops->write) return;
    uint64_t off = v->offset + (va - v->start);
    uint64_t size = vn->size;
    if (vn->ops->stat) {
        vfs_stat_t st;
        if (vn->ops->stat(vn, &st) == 0) size = st.st_size;
    }
    if (off >= size) return;
    size_t len = (size - off) < PG ? (size_t)(size - off) : (size_t)PG;
    vn->ops->write(vn, pmm_phys_to_virt(phys), len, off);
}

static void zap_cb(vmm_pte_t *pte, uintptr_t va, void *c) {
    zap_ctx_t *z = c;
    vmm_pte_t e = *pte;
    *pte = 0;
    fb_add(z->fb, va);
    if (e & VMM_SHARED) return;
    page_unref(e & PTE_ADDR);
}

static void clean_cb(vmm_pte_t *pte, uintptr_t va, void *c) {
    zap_ctx_t *z = c;
    vmm_pte_t e = *pte;
    if (!(e & VMM_DIRTY) || (e & VMM_SHARED)) return;
    *pte = e & ~VMM_DIRTY;
    fb_add(z->fb, va);
    writeback_page(z->vma, va, e & PTE_ADDR);
}

static void writeback_range(mm_t *mm, vm_area_t *v, uintptr_t start, uintptr_t end) {
    if (!((v->flags & VM_SHARED) && (v->flags & VM_FILE) && (v->flags & VM_WRITE))) return;
    flushbuf_t fb = { .mm = mm };
    zap_ctx_t z = { mm, v, &fb };
    walk_ptes(mm->pagemap, start, end, clean_cb, &z);
    fb_done(&fb);
}

static void zap_range(mm_t *mm, vm_area_t *v, uintptr_t start, uintptr_t end) {
    writeback_range(mm, v, start, end);
    flushbuf_t fb = { .mm = mm };
    zap_ctx_t z = { mm, v, &fb };
    walk_ptes(mm->pagemap, start, end, zap_cb, &z);
    fb_done(&fb);
}

static int unmap_locked(mm_t *mm, uintptr_t start, uintptr_t end) {
    vm_area_t *v = mm->vmas;
    while (v && v->end <= start) v = v->next;
    while (v && v->start < end) {
        if (v->start < start) {
            vm_area_t *n = vma_split(mm, v, start);
            if (!n) return -ENOMEM;
            v = n;
            continue;
        }
        if (v->end > end) {
            if (!vma_split(mm, v, end)) return -ENOMEM;
        }
        vm_area_t *next = v->next;
        zap_range(mm, v, v->start, v->end);
        vma_unlink(mm, v);
        vma_release(v);
        v = next;
    }
    return 0;
}

mm_t *mm_create(void) {
    mm_t *mm = kzalloc(sizeof(mm_t));
    if (!mm) return NULL;
    mm->pagemap = vmm_create_pagemap();
    if (!mm->pagemap) { kfree(mm); return NULL; }
    mm->cr3 = (uint64_t)pmm_virt_to_phys(mm->pagemap->pml4);
    kmutex_init(&mm->lock);
    mm->users = 1;
    mm->mmap_top = MM_MMAP_TOP;
    return mm;
}

void mm_get(mm_t *mm) {
    if (mm) __atomic_fetch_add(&mm->users, 1, __ATOMIC_ACQ_REL);
}

bool mm_tryget(mm_t *mm) {
    if (!mm) return false;
    int cur = __atomic_load_n(&mm->users, __ATOMIC_ACQUIRE);
    while (cur > 0) {
        if (__atomic_compare_exchange_n(&mm->users, &cur, cur + 1, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return true;
    }
    return false;
}

void mm_put(mm_t *mm) {
    if (!mm) return;
    if (__atomic_sub_fetch(&mm->users, 1, __ATOMIC_ACQ_REL) != 0) return;
    kmutex_lock(&mm->lock);
    vm_area_t *v = mm->vmas;
    while (v) {
        vm_area_t *next = v->next;
        writeback_range(mm, v, v->start, v->end);
        flushbuf_t fb = { .mm = mm, .all = true };
        zap_ctx_t z = { mm, v, &fb };
        walk_ptes(mm->pagemap, v->start, v->end, zap_cb, &z);
        vma_release(v);
        v = next;
    }
    mm->vmas = NULL;
    kmutex_unlock(&mm->lock);
    if (mm->exe) {
        __atomic_fetch_sub(&mm->exe->deny_write, 1, __ATOMIC_ACQ_REL);
        vnode_unref(mm->exe);
        mm->exe = NULL;
    }
    vmm_free_pagemap(mm->pagemap);
    kfree(mm->pagemap);
    kfree(mm);
}

typedef struct {
    mm_t       *child;
    vm_area_t  *vma;
    flushbuf_t *fb;
    int         err;
} fork_ctx_t;

static void fork_cb(vmm_pte_t *pte, uintptr_t va, void *c) {
    fork_ctx_t *f = c;
    if (f->err) return;
    vmm_pte_t e = *pte;
    vmm_pte_t *cp = vmm_pte_ensure(f->child->pagemap, va);
    if (!cp) { f->err = -ENOMEM; return; }
    if (e & VMM_SHARED) { *cp = e; return; }
    page_ref(e & PTE_ADDR);
    if ((f->vma->flags & VM_SHARED) || !(e & VMM_WRITE)) {
        *cp = e & ~VMM_DIRTY;
        return;
    }
    *pte = e & ~VMM_WRITE;
    fb_add(f->fb, va);
    *cp = e & ~(VMM_WRITE | VMM_DIRTY);
}

mm_t *mm_fork(mm_t *parent) {
    mm_t *child = mm_create();
    if (!child) return NULL;
    kmutex_lock(&parent->lock);
    child->brk_start = parent->brk_start;
    child->brk = parent->brk;
    child->mmap_top = parent->mmap_top;
    if (parent->exe) {
        vnode_ref(parent->exe);
        __atomic_fetch_add(&parent->exe->deny_write, 1, __ATOMIC_ACQ_REL);
        child->exe = parent->exe;
    }
    flushbuf_t fb = { .mm = parent };
    int err = 0;
    for (vm_area_t *v = parent->vmas; v && !err; v = v->next) {
        if (v->flags & VM_DONTCOPY) continue;
        vm_area_t *n = vma_new();
        if (!n) { err = -ENOMEM; break; }
        *n = *v;
        n->next = n->prev = NULL;
        if (n->vnode) vnode_ref(n->vnode);
        if (n->open) n->open(n);
        vma_link(child, n);
        fork_ctx_t f = { child, v, &fb, 0 };
        walk_ptes(parent->pagemap, v->start, v->end, fork_cb, &f);
        err = f.err;
    }
    fb_done(&fb);
    kmutex_unlock(&parent->lock);
    if (err) { mm_put(child); return NULL; }
    return child;
}

static int map_new_page(mm_t *mm, vm_area_t *v, uintptr_t page, int how, vmm_pte_t *pte) {
    bool want_write = (how & MM_FAULT_WRITE) != 0;
    if (v->flags & VM_PFNMAP) {
        uintptr_t phys = 0;
        int r = v->pfn ? v->pfn(v, v->offset + (page - v->start), &phys) : -EFAULT;
        if (r < 0) return r;
        *pte = (phys & PTE_ADDR) | leaf_bits(v, (v->flags & VM_WRITE) != 0) | VMM_SHARED | v->pte_extra;
        return 0;
    }
    if (v->flags & VM_FILE) {
        vnode_t *vn = v->vnode;
        uint64_t index = (v->offset + (page - v->start)) / PG;
        uintptr_t s0 = v->start, e0 = v->end;
        uint32_t f0 = v->flags;
        uint64_t o0 = v->offset;
        vnode_ref(vn);
        kmutex_unlock(&mm->lock);
        uintptr_t cphys = 0;
        int r = pagecache_get(vn, index, &cphys);
        kmutex_lock(&mm->lock);
        if (r < 0) { vnode_unref(vn); return r; }
        v = mm_find_vma(mm, page);
        bool same = v && v->vnode == vn && v->start == s0 && v->end == e0 &&
                    v->flags == f0 && v->offset == o0;
        vnode_unref(vn);
        vmm_pte_t *p2 = vmm_pte_ensure(mm->pagemap, page);
        if (!same || !p2 || (*p2 & VMM_PRESENT)) { page_unref(cphys); return 0; }
        if (v->flags & VM_SHARED) {
            *p2 = cphys | leaf_bits(v, (v->flags & VM_WRITE) != 0);
            return 0;
        }
        if (want_write) {
            uintptr_t np = page_alloc_user();
            if (!np) { page_unref(cphys); return -ENOMEM; }
            memcpy(pmm_phys_to_virt(np), pmm_phys_to_virt(cphys), PG);
            page_unref(cphys);
            *p2 = np | leaf_bits(v, true);
            return 0;
        }
        *p2 = cphys | leaf_bits(v, false);
        return 0;
    }
    uintptr_t np = page_alloc_user();
    if (!np) return -ENOMEM;
    *pte = np | leaf_bits(v, (v->flags & VM_WRITE) != 0);
    return 0;
}

#define FAULT_AROUND_PAGES 16

static void fault_around(mm_t *mm, vm_area_t *v, uintptr_t page) {
    uintptr_t win = page & ~((uintptr_t)FAULT_AROUND_PAGES * PG - 1);
    uintptr_t s = win > v->start ? win : v->start;
    uintptr_t e = win + FAULT_AROUND_PAGES * PG;
    if (e > v->end) e = v->end;
    bool writable = (v->flags & VM_SHARED) && (v->flags & VM_WRITE);
    for (uintptr_t p = s; p < e; p += PG) {
        if (p == page) continue;
        vmm_pte_t *pte = vmm_pte_ensure(mm->pagemap, p);
        if (!pte || (*pte & VMM_PRESENT)) continue;
        uintptr_t phys = 0;
        if (pagecache_peek(v->vnode, (v->offset + (p - v->start)) / PG, &phys) < 0) continue;
        *pte = phys | leaf_bits(v, writable);
    }
}

static int fault_locked(mm_t *mm, uintptr_t addr, int how) {
    uintptr_t page = pdown(addr);
    vm_area_t *v = mm_find_vma(mm, addr);
    if (!v) return -EFAULT;
    if ((how & MM_FAULT_WRITE) && !(v->flags & VM_WRITE)) return -EFAULT;
    if ((how & MM_FAULT_EXEC) && !(v->flags & VM_EXEC)) return -EFAULT;
    if (!(v->flags & VM_PROT_MASK)) return -EFAULT;

    vmm_pte_t *pte = vmm_pte_ensure(mm->pagemap, page);
    if (!pte) return -ENOMEM;
    vmm_pte_t e = *pte;
    if (!(e & VMM_PRESENT)) {
        int r = map_new_page(mm, v, page, how, pte);
        if (r < 0) return r;
        asm volatile("invlpg (%0)" :: "r"(page) : "memory");
        if (!(how & MM_FAULT_WRITE)) {
            vm_area_t *cur = mm_find_vma(mm, page);
            if (cur && (cur->flags & VM_FILE) && cur->vnode) fault_around(mm, cur, page);
        }
        return 0;
    }
    if ((how & MM_FAULT_WRITE) && !(e & VMM_WRITE)) {
        if (e & VMM_SHARED) {
            *pte = e | VMM_WRITE;
        } else if (v->flags & VM_SHARED) {
            *pte = e | VMM_WRITE;
        } else {
            uintptr_t phys = e & PTE_ADDR;
            if (page_refcount(phys) == 1) {
                *pte = e | VMM_WRITE;
            } else {
                uintptr_t np = page_alloc_user();
                if (!np) return -ENOMEM;
                memcpy(pmm_phys_to_virt(np), pmm_phys_to_virt(phys), PG);
                *pte = np | (e & ~PTE_ADDR) | VMM_WRITE;
                flushbuf_t fb = { .mm = mm };
                fb_add(&fb, page);
                fb_done(&fb);
                page_unref(phys);
                return 0;
            }
        }
    }
    asm volatile("invlpg (%0)" :: "r"(page) : "memory");
    return 0;
}

int mm_fault(mm_t *mm, uintptr_t addr, int how) {
    if (!mm || addr >= MM_USER_TOP) return -EFAULT;
    kmutex_lock(&mm->lock);
    int r = fault_locked(mm, addr, how);
    kmutex_unlock(&mm->lock);
    return r;
}

bool mm_access_ok(mm_t *mm, uintptr_t addr, size_t len, int write) {
    if (!mm) return true;
    if (len == 0) return true;
    uintptr_t end = addr + len;
    if (end < addr || end > MM_USER_TOP) return false;
    kmutex_lock(&mm->lock);
    uintptr_t cur = addr;
    bool ok = true;
    while (cur < end) {
        vm_area_t *v = mm_find_vma(mm, cur);
        if (!v || !(v->flags & VM_READ) || (write && !(v->flags & VM_WRITE))) { ok = false; break; }
        cur = v->end;
    }
    kmutex_unlock(&mm->lock);
    return ok;
}

int mm_populate(mm_t *mm, uintptr_t addr, size_t len, int write) {
    if (!mm || len == 0) return 0;
    uintptr_t end = addr + len;
    if (end < addr || end > MM_USER_TOP) return -EFAULT;
    kmutex_lock(&mm->lock);
    int r = 0;
    for (uintptr_t p = pdown(addr); p < end; p += PG) {
        vmm_pte_t *pte = vmm_pte_lookup(mm->pagemap, p);
        if (pte && (*pte & VMM_PRESENT) && (!write || (*pte & VMM_WRITE))) continue;
        r = fault_locked(mm, p, write ? MM_FAULT_WRITE : 0);
        if (r < 0) break;
    }
    kmutex_unlock(&mm->lock);
    return r;
}

int mm_user_phys(mm_t *mm, uintptr_t addr, int write, uintptr_t *phys_out) {
    if (!mm) return -EFAULT;
    int r = mm_populate(mm, addr, 1, write);
    if (r < 0) return r;
    kmutex_lock(&mm->lock);
    vmm_pte_t *pte = vmm_pte_lookup(mm->pagemap, pdown(addr));
    if (!pte || !(*pte & VMM_PRESENT)) { kmutex_unlock(&mm->lock); return -EFAULT; }
    *phys_out = (*pte & PTE_ADDR) | (addr & (PG - 1));
    kmutex_unlock(&mm->lock);
    return 0;
}

int mm_copy_to(mm_t *mm, uintptr_t dst, const void *src, size_t len) {
    const uint8_t *s = src;
    while (len) {
        uintptr_t phys;
        int r = mm_user_phys(mm, dst, 1, &phys);
        if (r < 0) return r;
        size_t chunk = PG - (dst & (PG - 1));
        if (chunk > len) chunk = len;
        memcpy(pmm_phys_to_virt(phys), s, chunk);
        dst += chunk; s += chunk; len -= chunk;
    }
    return 0;
}

static int64_t map_template(mm_t *mm, uintptr_t addr, size_t len, bool fixed, vm_area_t *tpl) {
    if (!mm || len == 0) return -EINVAL;
    len = pup(len);
    if (fixed && (addr & (PG - 1))) return -EINVAL;
    vm_area_t *v = vma_new();
    if (!v) return -ENOMEM;
    *v = *tpl;
    kmutex_lock(&mm->lock);
    uintptr_t at = pdown(addr);
    if (fixed) {
        if (at + len > MM_USER_TOP || at + len < at || at < 0x1000) { kmutex_unlock(&mm->lock); kfree(v); return -EINVAL; }
        int r = unmap_locked(mm, at, at + len);
        if (r < 0) { kmutex_unlock(&mm->lock); kfree(v); return r; }
    } else if (!at || at + len > MM_USER_TOP || at < 0x10000 || !range_free(mm, at, at + len)) {
        at = find_gap(mm, len);
        if (!at) { kmutex_unlock(&mm->lock); kfree(v); return -ENOMEM; }
    }
    v->start = at;
    v->end = at + len;
    if (v->vnode) vnode_ref(v->vnode);
    if (v->open) v->open(v);
    vma_link(mm, v);
    kmutex_unlock(&mm->lock);
    return (int64_t)at;
}

int64_t mm_map_anon(mm_t *mm, uintptr_t addr, size_t len, uint32_t flags, bool fixed) {
    vm_area_t t;
    memset(&t, 0, sizeof t);
    t.flags = flags & ~(VM_FILE | VM_PFNMAP);
    return map_template(mm, addr, len, fixed, &t);
}

int64_t mm_map_file(mm_t *mm, uintptr_t addr, size_t len, uint32_t flags, bool fixed,
                    struct vnode *vn, uint64_t offset) {
    if (!vn || (offset & (PG - 1))) return -EINVAL;
    vm_area_t t;
    memset(&t, 0, sizeof t);
    t.flags = (flags & ~VM_PFNMAP) | VM_FILE;
    t.vnode = vn;
    t.offset = offset;
    return map_template(mm, addr, len, fixed, &t);
}

int64_t mm_map_pfn(mm_t *mm, uintptr_t addr, size_t len, uint32_t flags, bool fixed,
                   uint64_t pte_extra, vm_pfn_fn pfn, vm_hook_fn open, vm_hook_fn close,
                   void *priv, uint64_t priv_id, struct vnode *vn, uint64_t offset) {
    vm_area_t t;
    memset(&t, 0, sizeof t);
    t.flags = (flags & ~VM_FILE) | VM_PFNMAP;
    t.pte_extra = pte_extra;
    t.pfn = pfn;
    t.open = open;
    t.close = close;
    t.priv = priv;
    t.priv_id = priv_id;
    t.vnode = vn;
    t.offset = offset;
    return map_template(mm, addr, len, fixed, &t);
}

int mm_unmap(mm_t *mm, uintptr_t addr, size_t len) {
    if (!mm || (addr & (PG - 1)) || len == 0) return -EINVAL;
    uintptr_t end = addr + pup(len);
    if (end < addr || end > MM_USER_TOP) return -EINVAL;
    kmutex_lock(&mm->lock);
    int r = unmap_locked(mm, addr, end);
    kmutex_unlock(&mm->lock);
    return r;
}

typedef struct { vm_area_t *vma; flushbuf_t *fb; } prot_ctx_t;

static void prot_cb(vmm_pte_t *pte, uintptr_t va, void *c) {
    prot_ctx_t *p = c;
    vmm_pte_t e = *pte;
    vmm_pte_t n = e;
    if (!(p->vma->flags & VM_WRITE)) n &= ~VMM_WRITE;
    else if ((p->vma->flags & VM_SHARED) || (e & VMM_SHARED)) n |= VMM_WRITE;
    if (p->vma->flags & VM_EXEC) n &= ~VMM_NOEXEC; else n |= VMM_NOEXEC;
    if (!(p->vma->flags & VM_PROT_MASK)) n &= ~VMM_USER;
    else n |= VMM_USER;
    if (n != e) { *pte = n; fb_add(p->fb, va); }
}

int mm_protect(mm_t *mm, uintptr_t addr, size_t len, uint32_t prot) {
    if (!mm || (addr & (PG - 1))) return -EINVAL;
    if (len == 0) return 0;
    uintptr_t end = addr + pup(len);
    if (end < addr || end > MM_USER_TOP) return -EINVAL;
    kmutex_lock(&mm->lock);
    uintptr_t cur = addr;
    while (cur < end) {
        vm_area_t *v = mm_find_vma(mm, cur);
        if (!v) { kmutex_unlock(&mm->lock); return -ENOMEM; }
        cur = v->end;
    }
    flushbuf_t fb = { .mm = mm };
    vm_area_t *v = mm->vmas;
    while (v && v->end <= addr) v = v->next;
    while (v && v->start < end) {
        if (v->start < addr) { v = vma_split(mm, v, addr); if (!v) break; continue; }
        if (v->end > end && !vma_split(mm, v, end)) break;
        if ((prot & VM_WRITE) && (v->flags & VM_SHARED) && (v->flags & VM_FILE) && v->vnode &&
            v->vnode->ops && !v->vnode->ops->write) {
            kmutex_unlock(&mm->lock);
            fb_done(&fb);
            return -EACCES;
        }
        v->flags = (v->flags & ~VM_PROT_MASK) | (prot & VM_PROT_MASK);
        prot_ctx_t p = { v, &fb };
        walk_ptes(mm->pagemap, v->start, v->end, prot_cb, &p);
        v = v->next;
    }
    fb_done(&fb);
    kmutex_unlock(&mm->lock);
    return 0;
}

static void populate_locked_vma(mm_t *mm, vm_area_t *v) {
    if (v->flags & VM_PFNMAP) return;
    int write = (v->flags & VM_WRITE) && !(v->flags & VM_SHARED);
    for (uintptr_t p = v->start; p < v->end; p += PG) {
        vmm_pte_t *pte = vmm_pte_lookup(mm->pagemap, p);
        if (pte && (*pte & VMM_PRESENT) && (!write || (*pte & VMM_WRITE))) continue;
        if (fault_locked(mm, p, write ? MM_FAULT_WRITE : 0) < 0) break;
    }
}

int mm_mlock(mm_t *mm, uintptr_t addr, size_t len, bool lock) {
    if (!mm) return -EINVAL;
    uintptr_t start = pdown(addr);
    uintptr_t end = addr + len;
    if (end < addr || end > MM_USER_TOP) return -ENOMEM;
    end = pup(end);
    if (start == end) return 0;
    kmutex_lock(&mm->lock);
    uintptr_t cur = start;
    while (cur < end) {
        vm_area_t *v = mm_find_vma(mm, cur);
        if (!v) { kmutex_unlock(&mm->lock); return -ENOMEM; }
        cur = v->end;
    }
    vm_area_t *v = mm->vmas;
    while (v && v->end <= start) v = v->next;
    while (v && v->start < end) {
        if (v->start < start) { v = vma_split(mm, v, start); if (!v) break; continue; }
        if (v->end > end && !vma_split(mm, v, end)) break;
        if (lock) {
            v->flags |= VM_LOCKED;
            populate_locked_vma(mm, v);
        } else {
            v->flags &= ~VM_LOCKED;
        }
        v = v->next;
    }
    kmutex_unlock(&mm->lock);
    return 0;
}

int mm_mlockall(mm_t *mm, int flags, bool lock) {
    if (!mm) return -EINVAL;
    kmutex_lock(&mm->lock);
    if (!lock) {
        mm->def_flags &= ~VM_LOCKED;
        for (vm_area_t *v = mm->vmas; v; v = v->next) v->flags &= ~VM_LOCKED;
        kmutex_unlock(&mm->lock);
        return 0;
    }
    if (flags & 2) mm->def_flags |= VM_LOCKED;
    else mm->def_flags &= ~VM_LOCKED;
    if (flags & 1) {
        for (vm_area_t *v = mm->vmas; v; v = v->next) {
            v->flags |= VM_LOCKED;
            populate_locked_vma(mm, v);
        }
    }
    kmutex_unlock(&mm->lock);
    return 0;
}

int mm_sync(mm_t *mm, uintptr_t addr, size_t len) {
    if (!mm || (addr & (PG - 1))) return -EINVAL;
    uintptr_t end = addr + pup(len);
    kmutex_lock(&mm->lock);
    for (vm_area_t *v = mm->vmas; v && v->start < end; v = v->next) {
        if (v->end <= addr) continue;
        uintptr_t s = v->start > addr ? v->start : addr;
        uintptr_t e = v->end < end ? v->end : end;
        writeback_range(mm, v, s, e);
    }
    kmutex_unlock(&mm->lock);
    return 0;
}

int mm_discard(mm_t *mm, uintptr_t addr, size_t len) {
    if (!mm || (addr & (PG - 1))) return -EINVAL;
    uintptr_t end = addr + pup(len);
    kmutex_lock(&mm->lock);
    for (vm_area_t *v = mm->vmas; v && v->start < end; v = v->next) {
        if (v->end <= addr) continue;
        if (v->flags & (VM_SHARED | VM_PFNMAP)) continue;
        uintptr_t s = v->start > addr ? v->start : addr;
        uintptr_t e = v->end < end ? v->end : end;
        zap_range(mm, v, s, e);
    }
    kmutex_unlock(&mm->lock);
    return 0;
}

void mm_init_heap(mm_t *mm, uintptr_t start) {
    mm->brk_start = mm->brk = pup(start);
}

uintptr_t mm_set_brk(mm_t *mm, uintptr_t new_brk) {
    if (!mm) return 0;
    kmutex_lock(&mm->lock);
    uintptr_t cur = mm->brk;
    if (!new_brk || new_brk < mm->brk_start) { kmutex_unlock(&mm->lock); return cur; }
    uintptr_t old_end = pup(cur), new_end = pup(new_brk);
    if (new_end > old_end) {
        if (new_end > mm->mmap_top || !range_free(mm, old_end, new_end)) {
            kmutex_unlock(&mm->lock);
            return cur;
        }
        vm_area_t *heap = NULL;
        for (vm_area_t *v = mm->vmas; v; v = v->next)
            if ((v->flags & VM_HEAP) && v->end == old_end) { heap = v; break; }
        if (heap) {
            heap->end = new_end;
        } else {
            vm_area_t *v = vma_new();
            if (!v) { kmutex_unlock(&mm->lock); return cur; }
            v->start = old_end;
            v->end = new_end;
            v->flags = VM_READ | VM_WRITE | VM_HEAP;
            vma_link(mm, v);
        }
    } else if (new_end < old_end) {
        unmap_locked(mm, new_end, old_end);
    }
    mm->brk = new_brk;
    kmutex_unlock(&mm->lock);
    return new_brk;
}

int mm_setup_stack(mm_t *mm) {
    uint64_t size = sched_current_rlimit(RLIMIT_STACK_K);
    if (size == RLIM_INF_K || size > MM_STACK_MAX) size = MM_STACK_MAX;
    if (size < MM_STACK_MIN) size = MM_STACK_MIN;
    size = (size + 0xFFFULL) & ~0xFFFULL;
    int64_t r = mm_map_anon(mm, MM_STACK_TOP - size, size,
                            VM_READ | VM_WRITE | VM_GROWSDOWN | VM_STACK, true);
    return r < 0 ? (int)r : 0;
}

int mm_set_exe(mm_t *mm, struct vnode *vn) {
    if (!mm || !vn) return 0;
    __atomic_fetch_add(&vn->deny_write, 1, __ATOMIC_ACQ_REL);
    if (__atomic_load_n(&vn->writers, __ATOMIC_ACQUIRE) > 0) {
        __atomic_fetch_sub(&vn->deny_write, 1, __ATOMIC_ACQ_REL);
        return -ETXTBSY;
    }
    vnode_ref(vn);
    mm->exe = vn;
    return 0;
}

uint64_t mm_locked_bytes(mm_t *mm) {
    if (!mm) return 0;
    uint64_t total = 0;
    kmutex_lock(&mm->lock);
    for (vm_area_t *v = mm->vmas; v; v = v->next)
        if (v->flags & VM_LOCKED) total += v->end - v->start;
    kmutex_unlock(&mm->lock);
    return total;
}

uint64_t mm_vm_bytes(mm_t *mm) {
    if (!mm) return 0;
    uint64_t total = 0;
    kmutex_lock(&mm->lock);
    for (vm_area_t *v = mm->vmas; v; v = v->next) total += v->end - v->start;
    kmutex_unlock(&mm->lock);
    return total;
}

int mm_format_maps(mm_t *mm, char *buf, size_t max) {
    if (!mm || !buf || !max) return 0;
    size_t n = 0;
    kmutex_lock(&mm->lock);
    for (vm_area_t *v = mm->vmas; v && n + 128 < max; v = v->next) {
        const char *name = "";
        if (v->flags & VM_STACK) name = "[stack]";
        else if (v->flags & VM_HEAP) name = "[heap]";
        uint64_t ino = v->vnode ? v->vnode->ino : 0;
        int w = snprintf(buf + n, max - n, "%012llx-%012llx %c%c%c%c %08llx 00:00 %llu%s%s\n",
                         (unsigned long long)v->start, (unsigned long long)v->end,
                         (v->flags & VM_READ) ? 'r' : '-',
                         (v->flags & VM_WRITE) ? 'w' : '-',
                         (v->flags & VM_EXEC) ? 'x' : '-',
                         (v->flags & VM_SHARED) ? 's' : 'p',
                         (unsigned long long)v->offset, (unsigned long long)ino,
                         name[0] ? "                          " : "", name);
        if (w < 0) break;
        n += (size_t)w;
    }
    kmutex_unlock(&mm->lock);
    return (int)n;
}
