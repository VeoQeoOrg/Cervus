#include "../../include/virt/vmx.h"
#include "../../include/virt/kvm.h"
#include "../../include/memory/pmm.h"
#include "../../include/memory/mm.h"
#include "../../include/memory/vmm.h"
#include "../../include/memory/pageref.h"
#include "../../include/syscall/errno.h"
#include <string.h>

#define PTE_ADDR 0x000FFFFFFFFFF000ull
#define EPT_PINNED (1ull << 52)
#define NPT_ACCESSED (1ull << 5)
#define NPT_DIRTY    (1ull << 6)

int ept_init(kvm_vm_t *vm) {
    vm->ept_root = pmm_alloc_zero(1);
    if (!vm->ept_root) return -ENOMEM;
    vm->eptp = pmm_virt_to_phys(vm->ept_root) | (3ull << 3) | 6ull;
    vm->mmu_lock = (spinlock_t)SPINLOCK_INIT;
    return 0;
}

static void free_table(uint64_t *t, int level) {
    for (int i = 0; i < 512; i++) {
        uint64_t e = t[i];
        if (level == 1) {
            if (e & EPT_PINNED) page_unref(e & EPT_ADDR_MASK);
            continue;
        }
        if (!(e & EPT_RWX)) continue;
        free_table((uint64_t *)pmm_phys_to_virt(e & EPT_ADDR_MASK), level - 1);
    }
    pmm_free(t, 1);
}

void ept_destroy(kvm_vm_t *vm) {
    if (!vm->ept_root) return;
    free_table((uint64_t *)vm->ept_root, 4);
    vm->ept_root = NULL;
}

static uint64_t *ept_walk(kvm_vm_t *vm, uint64_t gpa, bool create) {
    uint64_t *t = (uint64_t *)vm->ept_root;
    for (int level = 4; level > 1; level--) {
        int idx = (int)((gpa >> (12 + 9 * (level - 1))) & 511);
        uint64_t e = t[idx];
        if (!(e & EPT_RWX)) {
            if (!create) return NULL;
            void *n = pmm_alloc_zero(1);
            if (!n) return NULL;
            t[idx] = pmm_virt_to_phys(n) | EPT_RWX;
            e = t[idx];
        }
        t = (uint64_t *)pmm_phys_to_virt(e & EPT_ADDR_MASK);
    }
    return &t[(gpa >> 12) & 511];
}

kvm_memslot_t *kvm_gfn_slot(kvm_vm_t *vm, uint64_t gfn) {
    for (int i = 0; i < KVM_MAX_MEMSLOTS; i++) {
        kvm_memslot_t *s = &vm->slots[i];
        if (s->used && s->npages && gfn >= s->gfn && gfn < s->gfn + s->npages) return s;
    }
    return NULL;
}

void kvm_mark_dirty(kvm_vm_t *vm, uint64_t gfn) {
    uint64_t f = spinlock_acquire_irqsave(&vm->mmu_lock);
    kvm_memslot_t *s = kvm_gfn_slot(vm, gfn);
    if (s && s->dirty) {
        uint64_t rel = gfn - s->gfn;
        __atomic_fetch_or(&s->dirty[rel / 64], 1ull << (rel % 64), __ATOMIC_RELAXED);
    }
    spinlock_release_irqrestore(&vm->mmu_lock, f);
}

uint64_t *kvm_slot_swap_dirty(kvm_vm_t *vm, kvm_memslot_t *s, uint64_t *bm) {
    uint64_t f = spinlock_acquire_irqsave(&vm->mmu_lock);
    uint64_t *old = s->dirty;
    s->dirty = bm;
    spinlock_release_irqrestore(&vm->mmu_lock, f);
    return old;
}

static int hva_pin(kvm_vm_t *vm, uint64_t hva, bool write, uint64_t *hpa, bool *pinned) {
    mm_t *mm = vm->mm;
    int r = mm_populate(mm, hva & ~0xFFFull, 1, write);
    if (r < 0) return r;
    kmutex_lock(&mm->lock);
    vmm_pte_t *pte = vmm_pte_lookup(mm->pagemap, hva & ~0xFFFull);
    if (!pte || !(*pte & VMM_PRESENT) || (write && !(*pte & VMM_WRITE))) {
        kmutex_unlock(&mm->lock);
        return -EFAULT;
    }
    *hpa = *pte & PTE_ADDR;
    *pinned = !(*pte & VMM_SHARED);
    if (*pinned) page_ref(*hpa);
    kmutex_unlock(&mm->lock);
    return 0;
}

int ept_fault(kvm_vcpu_t *vcpu, uint64_t gpa, uint64_t qual) {
    kvm_vm_t *vm = vcpu->vm;
    uint64_t gfn = gpa >> 12;
    uint64_t gen = __atomic_load_n(&vm->slots_gen, __ATOMIC_ACQUIRE);
    uint64_t seq = __atomic_load_n(&vm->mmu_seq, __ATOMIC_ACQUIRE);
    kvm_memslot_t *slot = kvm_gfn_slot(vm, gfn);
    if (!slot) return -EFAULT;
    bool write = (qual & EPT_VIOL_WRITE) != 0;
    bool readonly = (slot->flags & KVM_MEM_READONLY) != 0;
    if (write && readonly) return -EFAULT;
    bool logging = slot->dirty != NULL;
    uint64_t hva = slot->hva + ((gfn - slot->gfn) << 12);

    uint64_t hpa;
    bool pinned;
    int r = hva_pin(vm, hva, !readonly, &hpa, &pinned);
    if (r < 0) return -EINVAL;

    uint64_t perm = EPT_READ | EPT_EXEC;
    if (!readonly && (!logging || write)) perm |= EPT_WRITE;
    uint64_t attr = kvm_npt ? (NPT_ACCESSED | NPT_DIRTY) : (EPT_MT_WB | EPT_IGNORE_PAT);
    uint64_t entry = hpa | perm | attr | (pinned ? EPT_PINNED : 0);

    uint64_t f = spinlock_acquire_irqsave(&vm->mmu_lock);
    if (__atomic_load_n(&vm->slots_gen, __ATOMIC_ACQUIRE) != gen ||
        __atomic_load_n(&vm->mmu_seq, __ATOMIC_ACQUIRE) != seq) {
        spinlock_release_irqrestore(&vm->mmu_lock, f);
        if (pinned) page_unref(hpa);
        return 0;
    }
    uint64_t *e = ept_walk(vm, gpa, true);
    if (!e) {
        spinlock_release_irqrestore(&vm->mmu_lock, f);
        if (pinned) page_unref(hpa);
        return -ENOMEM;
    }
    uint64_t old = *e;
    if (!(old & EPT_RWX) && (old & EPT_PINNED)) {
        spinlock_release_irqrestore(&vm->mmu_lock, f);
        if (pinned) page_unref(hpa);
        return 0;
    }
    bool same = (old & EPT_RWX) && (old & EPT_ADDR_MASK) == hpa;
    if (same) {
        *e = (old | perm);
        spinlock_release_irqrestore(&vm->mmu_lock, f);
        if (pinned) page_unref(hpa);
    } else {
        *e = entry;
        spinlock_release_irqrestore(&vm->mmu_lock, f);
        if (old & EPT_RWX) {
            ept_flush_all(vm);
            if (old & EPT_PINNED) page_unref(old & EPT_ADDR_MASK);
        }
    }
    if (write && logging) kvm_mark_dirty(vm, gfn);
    return 0;
}

void ept_flush_all(kvm_vm_t *vm) {
    __atomic_add_fetch(&vm->tlb_gen, 1, __ATOMIC_SEQ_CST);
    for (uint32_t i = 0; i < KVM_MAX_VCPUS; i++) {
        kvm_vcpu_t *v = vm->vcpus[i];
        if (!v) continue;
        __atomic_fetch_or(&v->requests, VCPU_REQ_TLB_FLUSH, __ATOMIC_SEQ_CST);
        kvm_arch->kick(v);
    }
    for (uint32_t i = 0; i < KVM_MAX_VCPUS; i++) {
        kvm_vcpu_t *v = vm->vcpus[i];
        if (!v) continue;
        for (uint64_t spins = 0; __atomic_load_n(&v->mode, __ATOMIC_ACQUIRE) == VCPU_IN_GUEST &&
                                 (__atomic_load_n(&v->requests, __ATOMIC_ACQUIRE) & VCPU_REQ_TLB_FLUSH); spins++) {
            asm volatile("pause");
            if ((spins & 0xFFFFF) == 0xFFFFF) kvm_arch->kick(v);
        }
    }
}

enum { WALK_WRITE_PROTECT, WALK_KILL, WALK_RELEASE };

static void range_walk(kvm_vm_t *vm, uint64_t gfn, uint64_t npages, int op) {
    uint64_t f = spinlock_acquire_irqsave(&vm->mmu_lock);
    for (uint64_t p = 0; p < npages; p++) {
        uint64_t *e = ept_walk(vm, (gfn + p) << 12, false);
        if (!e) {
            uint64_t gpa = (gfn + p) << 12;
            uint64_t step_pages = 512 - ((gpa >> 12) & 511);
            if (step_pages > 1) p += step_pages - 1;
            continue;
        }
        uint64_t old = *e;
        if (op == WALK_RELEASE) {
            if (!(old & EPT_RWX) && (old & EPT_PINNED)) {
                *e = 0;
                page_unref(old & EPT_ADDR_MASK);
            }
            continue;
        }
        if (!(old & EPT_RWX)) continue;
        if (op == WALK_KILL) *e = (old & EPT_PINNED) ? (old & (EPT_ADDR_MASK | EPT_PINNED)) : 0;
        else *e = old & ~EPT_WRITE;
    }
    spinlock_release_irqrestore(&vm->mmu_lock, f);
}

void ept_zap_range(kvm_vm_t *vm, uint64_t gfn, uint64_t npages) {
    range_walk(vm, gfn, npages, WALK_KILL);
    ept_flush_all(vm);
    range_walk(vm, gfn, npages, WALK_RELEASE);
}

void kvm_mmu_notify(void *ctx, uintptr_t start, uintptr_t end) {
    kvm_vm_t *vm = ctx;
    __atomic_add_fetch(&vm->mmu_seq, 1, __ATOMIC_SEQ_CST);
    for (int i = 0; i < KVM_MAX_MEMSLOTS; i++) {
        kvm_memslot_t *s = &vm->slots[i];
        if (!s->used || !s->npages) continue;
        uint64_t hs = s->hva, he = s->hva + (s->npages << 12);
        uint64_t a = start > hs ? start : hs;
        uint64_t b = end < he ? end : he;
        if (a >= b) continue;
        ept_zap_range(vm, s->gfn + ((a - hs) >> 12), (b - a + 0xFFF) >> 12);
    }
}

void ept_write_protect_range(kvm_vm_t *vm, uint64_t gfn, uint64_t npages) {
    range_walk(vm, gfn, npages, WALK_WRITE_PROTECT);
    ept_flush_all(vm);
}

static int gpa_pin(kvm_vm_t *vm, uint64_t gpa, bool write, uint64_t *hpa, bool *pinned) {
    kvm_memslot_t *s = kvm_gfn_slot(vm, gpa >> 12);
    if (!s) return -EFAULT;
    if (write && (s->flags & KVM_MEM_READONLY)) return -EFAULT;
    uint64_t hva = s->hva + (((gpa >> 12) - s->gfn) << 12);
    if (hva_pin(vm, hva, write, hpa, pinned) < 0) return -EFAULT;
    *hpa |= gpa & 0xFFF;
    if (write) kvm_mark_dirty(vm, gpa >> 12);
    return 0;
}

int kvm_read_guest(kvm_vm_t *vm, uint64_t gpa, void *buf, uint64_t len) {
    uint8_t *d = buf;
    while (len) {
        uint64_t chunk = 4096 - (gpa & 0xFFF);
        if (chunk > len) chunk = len;
        uint64_t hpa;
        bool pinned;
        if (gpa_pin(vm, gpa, false, &hpa, &pinned) < 0) return -EFAULT;
        memcpy(d, pmm_phys_to_virt(hpa), chunk);
        if (pinned) page_unref(hpa & ~0xFFFull);
        d += chunk; gpa += chunk; len -= chunk;
    }
    return 0;
}

int kvm_write_guest(kvm_vm_t *vm, uint64_t gpa, const void *buf, uint64_t len) {
    const uint8_t *s = buf;
    while (len) {
        uint64_t chunk = 4096 - (gpa & 0xFFF);
        if (chunk > len) chunk = len;
        uint64_t hpa;
        bool pinned;
        if (gpa_pin(vm, gpa, true, &hpa, &pinned) < 0) return -EFAULT;
        memcpy(pmm_phys_to_virt(hpa), s, chunk);
        if (pinned) page_unref(hpa & ~0xFFFull);
        s += chunk; gpa += chunk; len -= chunk;
    }
    return 0;
}
