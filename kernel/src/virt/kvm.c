#include "../../include/virt/vmx.h"
#include "../../include/virt/kvm.h"
#include "../../include/syscall/syscall_internal.h"
#include "../../include/syscall/errno.h"
#include "../../include/fs/vfs.h"
#include "../../include/memory/pmm.h"
#include "../../include/memory/mm.h"
#include "../../include/sched/sched.h"
#include "../../include/time/clocksource.h"
#include "../../include/io/serial.h"
#include "../../include/sse/fpu.h"
#include "../../include/apic/apic.h"
#include <string.h>
#include <stdlib.h>

extern void devfs_register(const char *name, vnode_t *node);

#define MCG_CTL_P (1ull << 8)

#define KVM_FEATURE_CLOCKSOURCE            (1u << 0)
#define KVM_FEATURE_NOP_IO_DELAY           (1u << 1)
#define KVM_FEATURE_CLOCKSOURCE2           (1u << 3)
#define KVM_FEATURE_CLOCKSOURCE_STABLE_BIT (1u << 24)

static const vnode_ops_t kvm_dev_ops;
static const vnode_ops_t kvm_vm_ops;
static const vnode_ops_t kvm_vcpu_ops;

static int copy_in(void *dst, uint64_t src, size_t n) {
    return syscall_copy_from_user(dst, (const void *)src, n) < 0 ? -EFAULT : 0;
}

static int copy_out(uint64_t dst, const void *src, size_t n) {
    return syscall_copy_to_user((void *)dst, src, n) < 0 ? -EFAULT : 0;
}

static int make_fd(const vnode_ops_t *ops, void *data) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;
    vnode_t *vn = calloc(1, sizeof *vn);
    if (!vn) return -ENOMEM;
    vn->type = VFS_NODE_CHARDEV;
    vn->mode = 0600;
    vn->ops = ops;
    vn->fs_data = data;
    vn->refcount = 1;
    vfs_file_t *file = vfs_file_alloc();
    if (!file) { free(vn); return -ENOMEM; }
    file->vnode = vn;
    file->flags = O_RDWR;
    int fd = fd_alloc(t->fd_table, file, 0);
    if (fd < 0) {
        file->vnode = NULL;
        vfs_file_free(file);
        free(vn);
        return -EMFILE;
    }
    fd_set_flags(t->fd_table, fd, FD_CLOEXEC);
    return fd;
}

static void vm_get(kvm_vm_t *vm) {
    __atomic_add_fetch(&vm->refcount, 1, __ATOMIC_ACQ_REL);
}

static void vm_put(kvm_vm_t *vm) {
    if (__atomic_sub_fetch(&vm->refcount, 1, __ATOMIC_ACQ_REL) > 0) return;
    if (vm->mm) mm_notifier_del(vm->mm, vm);
    kvm_irq_destroy(vm);
    ept_destroy(vm);
    for (int i = 0; i < KVM_MAX_MEMSLOTS; i++)
        if (vm->slots[i].dirty) free(vm->slots[i].dirty);
    kvm_ioeventfd_entry_t *e = vm->ioevents;
    while (e) {
        kvm_ioeventfd_entry_t *n = e->next;
        vnode_unref(e->efd);
        free(e);
        e = n;
    }
    if (vm->mm) mm_put(vm->mm);
    free(vm);
}

static int check_extension(uint64_t cap) {
    switch (cap) {
    case KVM_CAP_HLT:
    case KVM_CAP_USER_MEMORY:
    case KVM_CAP_SET_TSS_ADDR:
    case KVM_CAP_EXT_CPUID:
    case KVM_CAP_NOP_IO_DELAY:
    case KVM_CAP_MP_STATE:
    case KVM_CAP_SYNC_MMU:
    case KVM_CAP_DESTROY_MEMORY_REGION_WORKS:
    case KVM_CAP_USER_NMI:
    case KVM_CAP_JOIN_MEMORY_REGIONS_WORKS:
    case KVM_CAP_IRQCHIP:
    case KVM_CAP_IRQFD:
    case KVM_CAP_IRQ_INJECT_STATUS:
    case KVM_CAP_SPLIT_IRQCHIP:
    case KVM_CAP_IOEVENTFD:
    case KVM_CAP_SET_IDENTITY_MAP_ADDR:
    case KVM_CAP_INTERNAL_ERROR_DATA:
    case KVM_CAP_VCPU_EVENTS:
    case KVM_CAP_INTR_SHADOW:
    case KVM_CAP_DEBUGREGS:
    case KVM_CAP_X86_ROBUST_SINGLESTEP:
    case KVM_CAP_ENABLE_CAP:
    case KVM_CAP_XSAVE:
    case KVM_CAP_XCRS:
    case KVM_CAP_GET_TSC_KHZ:
    case KVM_CAP_SIGNAL_MSI:
    case KVM_CAP_READONLY_MEM:
    case KVM_CAP_IOEVENTFD_NO_LENGTH:
    case KVM_CAP_IOEVENTFD_ANY_LENGTH:
    case KVM_CAP_IMMEDIATE_EXIT:
        return 1;
    case KVM_CAP_MCE:
        return KVM_MCE_BANKS;
    case KVM_CAP_ADJUST_CLOCK:
        return KVM_CLOCK_TSC_STABLE;
    case KVM_CAP_TSC_DEADLINE_TIMER:
        return 1;
    case KVM_CAP_NR_VCPUS:
    case KVM_CAP_MAX_VCPUS:
    case KVM_CAP_MAX_VCPU_ID:
        return KVM_MAX_VCPUS;
    case KVM_CAP_NR_MEMSLOTS:
        return KVM_MAX_MEMSLOTS;
    case KVM_CAP_IRQ_ROUTING:
        return KVM_MAX_IRQ_ROUTES;
    default:
        return 0;
    }
}

static void cpuid_host(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}

static uint32_t add_leaf(struct kvm_cpuid_entry2 *out, uint32_t n, uint32_t max, uint32_t fn, uint32_t idx, uint32_t flags) {
    if (n >= max) return n;
    struct kvm_cpuid_entry2 *e = &out[n];
    memset(e, 0, sizeof *e);
    e->function = fn;
    e->index = idx;
    e->flags = flags;
    cpuid_host(fn, idx, &e->eax, &e->ebx, &e->ecx, &e->edx);
    return n + 1;
}

static void filter_leaf(struct kvm_cpuid_entry2 *e) {
    switch (e->function) {
    case 1: {
        uint32_t c0 = e->ecx;
        e->ecx &= (1u << 0) | (1u << 1) | (1u << 9) | (1u << 13) | (1u << 17) | (1u << 19) |
                  (1u << 20) | (1u << 22) | (1u << 23) | (1u << 25) | (1u << 30);
        e->ecx |= (1u << 21) | (1u << 24);
        if (fpu_xcr0) e->ecx |= c0 & (1u << 26);
        if (fpu_xcr0 & XFEAT_YMM) e->ecx |= c0 & ((1u << 12) | (1u << 28) | (1u << 29));
        e->ecx |= 1u << 31;
        e->edx &= (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6) |
                  (1u << 7) | (1u << 8) | (1u << 9) | (1u << 11) | (1u << 12) | (1u << 13) | (1u << 14) |
                  (1u << 15) | (1u << 16) | (1u << 17) | (1u << 19) | (1u << 23) | (1u << 24) |
                  (1u << 25) | (1u << 26) | (1u << 27) | (1u << 28);
        break;
    }
    case 5:
    case 0xA:
        e->eax = e->ebx = e->ecx = e->edx = 0;
        break;
    case 6:
        e->eax &= 1u << 2;
        e->ebx = e->ecx = e->edx = 0;
        break;
    case 7:
        if (e->index == 0) {
            uint32_t ebx_ok = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 6) | (1u << 7) | (1u << 8) |
                              (1u << 9) | (1u << 13) | (1u << 18) | (1u << 19) | (1u << 20) |
                              (1u << 23) | (1u << 24) | (1u << 29);
            uint64_t cr4 = kvm_arch->supported_cr4();
            if (!(cr4 & (1ull << 16))) ebx_ok &= ~1u;
            if (!(cr4 & (1ull << 20))) ebx_ok &= ~(1u << 7);
            if (!(cr4 & (1ull << 21))) ebx_ok &= ~(1u << 20);
            ebx_ok |= 1u << 10;
            uint32_t ecx_ok = (cr4 & (1ull << 11)) ? (1u << 2) : 0;
            uint32_t edx_ok = 0;
            ecx_ok |= 1u << 8;
            if (fpu_xcr0 & XFEAT_YMM) {
                ebx_ok |= 1u << 5;
                ecx_ok |= (1u << 9) | (1u << 10);
            }
            if ((fpu_xcr0 & XFEAT_AVX512) == XFEAT_AVX512) {
                ebx_ok |= (1u << 16) | (1u << 17) | (1u << 21) | (1u << 26) | (1u << 27) |
                          (1u << 28) | (1u << 30) | (1u << 31);
                ecx_ok |= (1u << 1) | (1u << 6) | (1u << 11) | (1u << 12) | (1u << 14);
                edx_ok |= (1u << 2) | (1u << 3) | (1u << 8) | (1u << 23);
            }
            e->ebx &= ebx_ok;
            e->ecx &= ecx_ok;
            e->edx &= edx_ok;
            e->eax = 0;
        } else {
            e->eax = e->ebx = e->ecx = e->edx = 0;
        }
        break;
    case 0xD:
        if (!fpu_xcr0) {
            e->eax = e->ebx = e->ecx = e->edx = 0;
        } else if (e->index == 0) {
            e->eax = (uint32_t)fpu_xcr0;
            e->edx = (uint32_t)(fpu_xcr0 >> 32);
            e->ebx = e->ecx = fpu_xsave_size_for(fpu_xcr0);
        } else if (e->index == 1) {
            e->eax &= (1u << 0) | (1u << 2);
            e->ebx = e->ecx = e->edx = 0;
        } else if (e->index < 63 && (fpu_xcr0 & (1ull << e->index))) {
            e->ecx &= 2u;
            e->edx = 0;
        } else {
            e->eax = e->ebx = e->ecx = e->edx = 0;
        }
        break;
    case 0x80000001: {
        e->ecx &= (1u << 0) | (1u << 5) | (1u << 8);
        uint32_t edx_ok = (1u << 11) | (1u << 20) | (1u << 26) | (1u << 27) | (1u << 29);
        e->edx &= edx_ok;
        break;
    }
    case 0x80000007:
        e->eax = e->ebx = e->ecx = 0;
        e->edx &= 1u << 8;
        break;
    case 0x80000008:
        e->ebx = 0;
        e->edx = 0;
        break;
    default:
        break;
    }
}

static uint32_t build_supported_cpuid(struct kvm_cpuid_entry2 *out, uint32_t max) {
    uint32_t n = 0, a, b, c, d;
    cpuid_host(0, 0, &a, &b, &c, &d);
    uint32_t maxbasic = a > 0x1F ? 0x1F : a;
    for (uint32_t fn = 0; fn <= maxbasic; fn++) {
        if (fn == 4) {
            for (uint32_t i = 0; i < 8; i++) {
                uint32_t a2, b2, c2, d2;
                cpuid_host(4, i, &a2, &b2, &c2, &d2);
                if (!(a2 & 0x1F) && i) break;
                n = add_leaf(out, n, max, 4, i, KVM_CPUID_FLAG_SIGNIFCANT_INDEX);
            }
        } else if (fn == 7) {
            n = add_leaf(out, n, max, 7, 0, KVM_CPUID_FLAG_SIGNIFCANT_INDEX);
        } else if (fn == 0xB || fn == 0x1F) {
            for (uint32_t i = 0; i < 4; i++) n = add_leaf(out, n, max, fn, i, KVM_CPUID_FLAG_SIGNIFCANT_INDEX);
        } else if (fn == 0xD) {
            if (!fpu_xcr0) continue;
            for (uint32_t i = 0; i < 63; i++)
                if (i < 2 || (fpu_xcr0 & (1ull << i)))
                    n = add_leaf(out, n, max, 0xD, i, KVM_CPUID_FLAG_SIGNIFCANT_INDEX);
        } else if (fn == 0xF || fn == 0x10 || fn == 0x12 || fn == 0x14 || fn == 0x17 || fn == 0x18) {
            continue;
        } else {
            n = add_leaf(out, n, max, fn, 0, 0);
        }
    }
    cpuid_host(0x80000000u, 0, &a, &b, &c, &d);
    uint32_t maxext = a > 0x80000008u ? 0x80000008u : a;
    for (uint32_t fn = 0x80000000u; fn <= maxext; fn++) n = add_leaf(out, n, max, fn, 0, 0);
    for (uint32_t i = 0; i < n; i++) {
        filter_leaf(&out[i]);
        kvm_arch->adjust_cpuid(&out[i]);
    }
    if (n < max) {
        struct kvm_cpuid_entry2 *e = &out[n++];
        memset(e, 0, sizeof *e);
        e->function = 0x40000000u;
        e->eax = 0x40000001u;
        e->ebx = 0x4b4d564bu;
        e->ecx = 0x564b4d56u;
        e->edx = 0x0000004du;
    }
    if (n < max) {
        struct kvm_cpuid_entry2 *e = &out[n++];
        memset(e, 0, sizeof *e);
        e->function = 0x40000001u;
        e->eax = KVM_FEATURE_CLOCKSOURCE | KVM_FEATURE_NOP_IO_DELAY | KVM_FEATURE_CLOCKSOURCE2;
        if (kvm_host_tsc_stable()) e->eax |= KVM_FEATURE_CLOCKSOURCE_STABLE_BIT;
    }
    return n;
}

static int64_t cpuid_list_out(uint64_t arg, bool emulated) {
    struct kvm_cpuid2 hdr;
    if (copy_in(&hdr, arg, sizeof hdr) < 0) return -EFAULT;
    struct kvm_cpuid_entry2 *buf = calloc(KVM_MAX_CPUID, sizeof *buf);
    if (!buf) return -ENOMEM;
    uint32_t n = emulated ? 0 : build_supported_cpuid(buf, KVM_MAX_CPUID);
    int64_t r = 0;
    if (hdr.nent < n) {
        hdr.nent = n;
        copy_out(arg, &hdr, sizeof hdr);
        r = -E2BIG;
    } else {
        hdr.nent = n;
        if (copy_out(arg, &hdr, sizeof hdr) < 0 ||
            copy_out(arg + sizeof hdr, buf, n * sizeof *buf) < 0)
            r = -EFAULT;
    }
    free(buf);
    return r;
}

static int64_t msr_list_out(uint64_t arg, bool features) {
    struct kvm_msr_list hdr;
    if (copy_in(&hdr, arg, sizeof hdr) < 0) return -EFAULT;
    uint32_t list[64];
    uint32_t n = features ? 0 : kvm_arch->msr_index_list(list, 64);
    if (hdr.nmsrs < n) {
        hdr.nmsrs = n;
        copy_out(arg, &hdr, sizeof hdr);
        return -E2BIG;
    }
    hdr.nmsrs = n;
    if (copy_out(arg, &hdr, sizeof hdr) < 0) return -EFAULT;
    if (n && copy_out(arg + sizeof hdr, list, n * sizeof(uint32_t)) < 0) return -EFAULT;
    return 0;
}

static int64_t create_vm(uint64_t type) {
    if (type != 0) return -EINVAL;
    if (!kvm_arch) return -ENODEV;
    task_t *t = syscall_cur_task();
    if (!t || !t->mm) return -EINVAL;
    kvm_vm_t *vm = calloc(1, sizeof *vm);
    if (!vm) return -ENOMEM;
    kmutex_init(&vm->lock);
    vm->refcount = 1;
    vm->ioevent_lock = (spinlock_t)SPINLOCK_INIT;
    vm->mcg_cap_supported = MCG_CTL_P | KVM_MCE_BANKS;
    kvm_clock_init_vm(vm);
    if (ept_init(vm) < 0) { free(vm); return -ENOMEM; }
    mm_get(t->mm);
    vm->mm = t->mm;
    if (mm_notifier_add(vm->mm, kvm_mmu_notify, vm) < 0) { vm_put(vm); return -EBUSY; }
    int fd = make_fd(&kvm_vm_ops, vm);
    if (fd < 0) vm_put(vm);
    return fd;
}

static int64_t kvm_dev_ioctl(vnode_t *node, uint64_t req, uint64_t arg) {
    (void)node;
    switch (req) {
    case KVM_GET_API_VERSION:
        return arg ? -EINVAL : KVM_API_VERSION;
    case KVM_CREATE_VM:
        return create_vm(arg);
    case KVM_CHECK_EXTENSION:
        return check_extension(arg);
    case KVM_GET_VCPU_MMAP_SIZE:
        return KVM_RUN_PAGES * 4096;
    case KVM_GET_SUPPORTED_CPUID:
        return cpuid_list_out(arg, false);
    case KVM_GET_EMULATED_CPUID:
        return cpuid_list_out(arg, true);
    case KVM_GET_MSR_INDEX_LIST:
        return msr_list_out(arg, false);
    case KVM_GET_MSR_FEATURE_INDEX_LIST:
        return msr_list_out(arg, true);
    case KVM_X86_GET_MCE_CAP_SUPPORTED: {
        uint64_t v = MCG_CTL_P | KVM_MCE_BANKS;
        return copy_out(arg, &v, sizeof v);
    }
    default:
        return -ENOTTY;
    }
}

static int64_t set_memory_region(kvm_vm_t *vm, const struct kvm_userspace_memory_region *m) {
    uint32_t id = m->slot & 0xFFFF;
    if ((m->slot >> 16) != 0) return -EINVAL;
    if (id >= KVM_MAX_MEMSLOTS) return -EINVAL;
    if (m->flags & ~(KVM_MEM_LOG_DIRTY_PAGES | KVM_MEM_READONLY)) return -EINVAL;
    if ((m->guest_phys_addr | m->memory_size | m->userspace_addr) & 0xFFF) return -EINVAL;
    if (m->memory_size && (m->userspace_addr >= MM_USER_TOP ||
                           m->userspace_addr + m->memory_size > MM_USER_TOP ||
                           m->userspace_addr + m->memory_size < m->userspace_addr))
        return -EINVAL;

    kmutex_lock(&vm->lock);
    kvm_memslot_t *s = &vm->slots[id];
    uint64_t gfn = m->guest_phys_addr >> 12;
    uint64_t npages = m->memory_size >> 12;

    if (s->used && s->gfn == gfn && s->npages == npages && s->hva == m->userspace_addr && npages &&
        !((s->flags ^ m->flags) & KVM_MEM_READONLY)) {
        bool was = s->dirty != NULL, want = (m->flags & KVM_MEM_LOG_DIRTY_PAGES) != 0;
        if (want && !was) {
            uint64_t *bm = calloc((npages + 63) / 64, sizeof(uint64_t));
            if (!bm) { kmutex_unlock(&vm->lock); return -ENOMEM; }
            kvm_slot_swap_dirty(vm, s, bm);
            ept_write_protect_range(vm, gfn, npages);
        } else if (!want && was) {
            free(kvm_slot_swap_dirty(vm, s, NULL));
        }
        s->flags = m->flags;
        kmutex_unlock(&vm->lock);
        return 0;
    }

    if (npages) {
        for (int i = 0; i < KVM_MAX_MEMSLOTS; i++) {
            kvm_memslot_t *o = &vm->slots[i];
            if (i == (int)id || !o->used) continue;
            if (gfn < o->gfn + o->npages && o->gfn < gfn + npages) {
                kmutex_unlock(&vm->lock);
                return -EEXIST;
            }
        }
    }

    if (s->used) {
        uint64_t ogfn = s->gfn, onp = s->npages;
        s->used = false;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        __atomic_add_fetch(&vm->slots_gen, 1, __ATOMIC_SEQ_CST);
        ept_zap_range(vm, ogfn, onp);
        free(kvm_slot_swap_dirty(vm, s, NULL));
        s->npages = 0;
    }
    if (npages) {
        uint64_t *bm = NULL;
        if (m->flags & KVM_MEM_LOG_DIRTY_PAGES) {
            bm = calloc((npages + 63) / 64, sizeof(uint64_t));
            if (!bm) { kmutex_unlock(&vm->lock); return -ENOMEM; }
        }
        s->id = id;
        s->gfn = gfn;
        s->npages = npages;
        s->hva = m->userspace_addr;
        s->flags = m->flags;
        kvm_slot_swap_dirty(vm, s, bm);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        s->used = true;
        __atomic_add_fetch(&vm->slots_gen, 1, __ATOMIC_SEQ_CST);
    }
    kmutex_unlock(&vm->lock);
    return 0;
}

static int64_t get_dirty_log(kvm_vm_t *vm, const struct kvm_dirty_log *d) {
    uint32_t id = d->slot & 0xFFFF;
    if (id >= KVM_MAX_MEMSLOTS) return -EINVAL;
    kmutex_lock(&vm->lock);
    kvm_memslot_t *s = &vm->slots[id];
    if (!s->used || !s->dirty) { kmutex_unlock(&vm->lock); return -ENOENT; }
    uint64_t words = (s->npages + 63) / 64;
    uint64_t *snap = calloc(words, sizeof(uint64_t));
    if (!snap) { kmutex_unlock(&vm->lock); return -ENOMEM; }
    bool any = false;
    for (uint64_t i = 0; i < words; i++) {
        snap[i] = __atomic_exchange_n(&s->dirty[i], 0, __ATOMIC_ACQ_REL);
        if (snap[i]) any = true;
    }
    if (any) ept_write_protect_range(vm, s->gfn, s->npages);
    kmutex_unlock(&vm->lock);
    int r = copy_out(d->dirty_bitmap, snap, words * sizeof(uint64_t));
    free(snap);
    return r;
}

bool kvm_ioeventfd_signal(kvm_vm_t *vm, bool pio, uint64_t addr, uint32_t len, uint64_t data) {
    if (!vm->ioevents) return false;
    vnode_t *hit = NULL;
    uint64_t f = spinlock_acquire_irqsave(&vm->ioevent_lock);
    for (kvm_ioeventfd_entry_t *e = vm->ioevents; e; e = e->next) {
        if (e->pio != pio || e->addr != addr) continue;
        if (e->len && e->len != len) continue;
        if (e->datamatch) {
            uint64_t mask = len >= 8 ? ~0ull : ((1ull << (len * 8)) - 1);
            if ((data & mask) != (e->data & mask)) continue;
        }
        hit = e->efd;
        vnode_ref(hit);
        break;
    }
    spinlock_release_irqrestore(&vm->ioevent_lock, f);
    if (!hit) return false;
    uint64_t one = 1;
    if (hit->ops && hit->ops->write) hit->ops->write(hit, &one, sizeof one, 0);
    vnode_unref(hit);
    return true;
}

static int64_t ioeventfd(kvm_vm_t *vm, const struct kvm_ioeventfd *io) {
    if (io->flags & ~(KVM_IOEVENTFD_FLAG_DATAMATCH | KVM_IOEVENTFD_FLAG_PIO | KVM_IOEVENTFD_FLAG_DEASSIGN))
        return -EINVAL;
    if (io->len != 0 && io->len != 1 && io->len != 2 && io->len != 4 && io->len != 8) return -EINVAL;
    if (!io->len && (io->flags & KVM_IOEVENTFD_FLAG_DATAMATCH)) return -EINVAL;
    task_t *t = syscall_cur_task();
    vfs_file_t *f = fd_get(t->fd_table, io->fd);
    if (!f) return -EBADF;
    vnode_t *efd = f->vnode;
    vnode_ref(efd);
    fd_put(f);
    bool pio = (io->flags & KVM_IOEVENTFD_FLAG_PIO) != 0;
    bool dm = (io->flags & KVM_IOEVENTFD_FLAG_DATAMATCH) != 0;

    if (io->flags & KVM_IOEVENTFD_FLAG_DEASSIGN) {
        kvm_ioeventfd_entry_t *found = NULL;
        uint64_t fl = spinlock_acquire_irqsave(&vm->ioevent_lock);
        for (kvm_ioeventfd_entry_t **pp = &vm->ioevents; *pp; pp = &(*pp)->next) {
            kvm_ioeventfd_entry_t *e = *pp;
            if (e->efd == efd && e->addr == io->addr && e->len == io->len && e->pio == pio &&
                e->datamatch == dm && (!dm || e->data == io->datamatch)) {
                *pp = e->next;
                found = e;
                vm->nioevents--;
                break;
            }
        }
        spinlock_release_irqrestore(&vm->ioevent_lock, fl);
        vnode_unref(efd);
        if (!found) return -ENOENT;
        vnode_unref(found->efd);
        free(found);
        return 0;
    }

    if (!efd->ops || !efd->ops->write) { vnode_unref(efd); return -EINVAL; }
    kvm_ioeventfd_entry_t *e = calloc(1, sizeof *e);
    if (!e) { vnode_unref(efd); return -ENOMEM; }
    e->addr = io->addr;
    e->len = io->len;
    e->pio = pio;
    e->datamatch = dm;
    e->data = io->datamatch;
    e->efd = efd;
    uint64_t fl = spinlock_acquire_irqsave(&vm->ioevent_lock);
    for (kvm_ioeventfd_entry_t *o = vm->ioevents; o; o = o->next) {
        if (o->addr == e->addr && o->len == e->len && o->pio == pio &&
            (!o->datamatch || !dm || o->data == e->data)) {
            spinlock_release_irqrestore(&vm->ioevent_lock, fl);
            vnode_unref(efd);
            free(e);
            return -EEXIST;
        }
    }
    if (vm->nioevents >= KVM_MAX_IOEVENTFDS) {
        spinlock_release_irqrestore(&vm->ioevent_lock, fl);
        vnode_unref(efd);
        free(e);
        return -ENOSPC;
    }
    e->next = vm->ioevents;
    vm->ioevents = e;
    vm->nioevents++;
    spinlock_release_irqrestore(&vm->ioevent_lock, fl);
    return 0;
}

static int64_t create_vcpu(kvm_vm_t *vm, uint64_t id) {
    if (id >= KVM_MAX_VCPUS) return -EINVAL;
    kmutex_lock(&vm->lock);
    if (vm->vcpus[id]) { kmutex_unlock(&vm->lock); return -EEXIST; }
    kvm_vcpu_t *v = calloc(1, sizeof *v);
    if (!v) { kmutex_unlock(&vm->lock); return -ENOMEM; }
    v->vm = vm;
    v->id = (uint32_t)id;
    v->refcount = 1;
    kmutex_init(&v->mutex);
    v->run = pmm_alloc_zero(KVM_RUN_PAGES);
    if (!v->run) { free(v); kmutex_unlock(&vm->lock); return -ENOMEM; }
    v->run_phys = pmm_virt_to_phys(v->run);
    v->pio_data_off = KVM_PIO_PAGE_OFFSET * 4096;
    int r = kvm_arch->vcpu_create(v);
    if (r < 0) {
        kvm_arch->vcpu_destroy(v);
        pmm_free(v->run, KVM_RUN_PAGES);
        free(v);
        kmutex_unlock(&vm->lock);
        return r;
    }
    vm_get(vm);
    int fd = make_fd(&kvm_vcpu_ops, v);
    if (fd < 0) {
        kvm_arch->vcpu_destroy(v);
        pmm_free(v->run, KVM_RUN_PAGES);
        free(v);
        kmutex_unlock(&vm->lock);
        vm_put(vm);
        return fd;
    }
    vm->vcpus[id] = v;
    if (id >= vm->nvcpus) vm->nvcpus = (uint32_t)id + 1;
    kmutex_unlock(&vm->lock);
    return fd;
}

static int64_t kvm_vm_ioctl(vnode_t *node, uint64_t req, uint64_t arg) {
    kvm_vm_t *vm = node->fs_data;
    switch (req) {
    case KVM_CHECK_EXTENSION:
        return check_extension(arg);
    case KVM_CREATE_VCPU:
        return create_vcpu(vm, arg);
    case KVM_SET_USER_MEMORY_REGION: {
        struct kvm_userspace_memory_region m;
        if (copy_in(&m, arg, sizeof m) < 0) return -EFAULT;
        return set_memory_region(vm, &m);
    }
    case KVM_GET_DIRTY_LOG: {
        struct kvm_dirty_log d;
        if (copy_in(&d, arg, sizeof d) < 0) return -EFAULT;
        return get_dirty_log(vm, &d);
    }
    case KVM_SET_TSS_ADDR:
        vm->tss_addr = arg;
        return 0;
    case KVM_SET_IDENTITY_MAP_ADDR: {
        uint64_t a;
        if (copy_in(&a, arg, sizeof a) < 0) return -EFAULT;
        vm->identity_map_addr = a;
        return 0;
    }
    case KVM_SET_NR_MMU_PAGES:
        return 0;
    case KVM_GET_NR_MMU_PAGES:
        return 1 << 20;
    case KVM_SET_BOOT_CPU_ID:
        return arg < KVM_MAX_VCPUS ? 0 : -EINVAL;
    case KVM_IOEVENTFD: {
        struct kvm_ioeventfd io;
        if (copy_in(&io, arg, sizeof io) < 0) return -EFAULT;
        return ioeventfd(vm, &io);
    }
    case KVM_GET_CLOCK: {
        struct kvm_clock_data c;
        memset(&c, 0, sizeof c);
        c.clock = kvm_clock_read(vm);
        c.flags = vm->clock_stable ? KVM_CLOCK_TSC_STABLE : 0;
        return copy_out(arg, &c, sizeof c);
    }
    case KVM_SET_CLOCK: {
        struct kvm_clock_data c;
        if (copy_in(&c, arg, sizeof c) < 0) return -EFAULT;
        kvm_clock_set(vm, c.clock);
        return 0;
    }
    case KVM_SET_GSI_ROUTING:
        return kvm_set_routing(vm, arg);
    case KVM_ENABLE_CAP: {
        struct kvm_enable_cap c;
        if (copy_in(&c, arg, sizeof c) < 0) return -EFAULT;
        if (c.flags || c.cap != KVM_CAP_SPLIT_IRQCHIP) return -EINVAL;
        if (c.args[0] > 2 * KVM_IOAPIC_PINS) return -EINVAL;
        kmutex_lock(&vm->lock);
        int64_t res = 0;
        if (vm->irqchip_split) res = -EEXIST;
        else if (vm->nvcpus) res = -EINVAL;
        else vm->irqchip_split = true;
        kmutex_unlock(&vm->lock);
        return res;
    }
    case KVM_CREATE_IRQCHIP:
        return -EINVAL;
    case KVM_IRQ_LINE:
    case KVM_IRQ_LINE_STATUS: {
        struct kvm_irq_level l;
        if (!vm->irqchip_split) return -ENXIO;
        if (copy_in(&l, arg, sizeof l) < 0) return -EFAULT;
        int st = kvm_set_gsi(vm, l.irq, (int)l.level);
        if (req == KVM_IRQ_LINE_STATUS) {
            l.status = st;
            return copy_out(arg, &l, sizeof l);
        }
        return 0;
    }
    case KVM_SIGNAL_MSI: {
        struct kvm_msi m;
        if (!vm->irqchip_split) return -ENXIO;
        if (copy_in(&m, arg, sizeof m) < 0) return -EFAULT;
        if (m.flags & ~KVM_MSI_VALID_DEVID) return -EINVAL;
        int st = kvm_deliver_msi(vm, ((uint64_t)m.address_hi << 32) | m.address_lo, m.data);
        return st < 0 ? 0 : st;
    }
    case KVM_IRQFD: {
        struct kvm_irqfd f;
        if (!vm->irqchip_split) return -ENXIO;
        if (copy_in(&f, arg, sizeof f) < 0) return -EFAULT;
        return kvm_irqfd(vm, &f);
    }
    default:
        return -ENOTTY;
    }
}

static void fpu_to_kvm(const uint8_t *fx, struct kvm_fpu *f) {
    memset(f, 0, sizeof *f);
    memcpy(&f->fcw, fx + 0, 2);
    memcpy(&f->fsw, fx + 2, 2);
    f->ftwx = fx[4];
    memcpy(&f->last_opcode, fx + 6, 2);
    memcpy(&f->last_ip, fx + 8, 8);
    memcpy(&f->last_dp, fx + 16, 8);
    memcpy(&f->mxcsr, fx + 24, 4);
    for (int i = 0; i < 8; i++) memcpy(f->fpr[i], fx + 32 + i * 16, 16);
    for (int i = 0; i < 16; i++) memcpy(f->xmm[i], fx + 160 + i * 16, 16);
}

static void kvm_to_fpu(const struct kvm_fpu *f, uint8_t *fx) {
    memcpy(fx + 0, &f->fcw, 2);
    memcpy(fx + 2, &f->fsw, 2);
    fx[4] = f->ftwx;
    memcpy(fx + 6, &f->last_opcode, 2);
    memcpy(fx + 8, &f->last_ip, 8);
    memcpy(fx + 16, &f->last_dp, 8);
    uint32_t mxcsr = f->mxcsr & fpu_mxcsr_mask;
    memcpy(fx + 24, &mxcsr, 4);
    for (int i = 0; i < 8; i++) memcpy(fx + 32 + i * 16, f->fpr[i], 16);
    for (int i = 0; i < 16; i++) memcpy(fx + 160 + i * 16, f->xmm[i], 16);
}

static int64_t vcpu_msrs(kvm_vcpu_t *v, uint64_t arg, bool set) {
    struct kvm_msrs hdr;
    if (copy_in(&hdr, arg, sizeof hdr) < 0) return -EFAULT;
    if (hdr.nmsrs > 1024) return -E2BIG;
    if (!hdr.nmsrs) return 0;
    struct kvm_msr_entry *ent = calloc(hdr.nmsrs, sizeof *ent);
    if (!ent) return -ENOMEM;
    if (copy_in(ent, arg + sizeof hdr, hdr.nmsrs * sizeof *ent) < 0) { free(ent); return -EFAULT; }
    uint32_t done = 0;
    kvm_arch->state_enter(v);
    for (; done < hdr.nmsrs; done++) {
        int r = set ? kvm_arch->set_msr(v, ent[done].index, ent[done].data, true)
                    : kvm_arch->get_msr(v, ent[done].index, &ent[done].data, true);
        if (r < 0) break;
    }
    kvm_arch->state_leave(v);
    int64_t res = done;
    if (!set && copy_out(arg + sizeof hdr, ent, hdr.nmsrs * sizeof *ent) < 0) res = -EFAULT;
    free(ent);
    return res;
}

static int64_t vcpu_set_cpuid2(kvm_vcpu_t *v, uint64_t arg) {
    struct kvm_cpuid2 hdr;
    if (copy_in(&hdr, arg, sizeof hdr) < 0) return -EFAULT;
    if (hdr.nent > KVM_MAX_CPUID) return -E2BIG;
    if (copy_in(v->cpuid, arg + sizeof hdr, hdr.nent * sizeof(struct kvm_cpuid_entry2)) < 0) return -EFAULT;
    v->ncpuid = hdr.nent;
    return 0;
}

static int64_t vcpu_set_cpuid1(kvm_vcpu_t *v, uint64_t arg) {
    struct kvm_cpuid hdr;
    if (copy_in(&hdr, arg, sizeof hdr) < 0) return -EFAULT;
    if (hdr.nent > KVM_MAX_CPUID) return -E2BIG;
    struct kvm_cpuid_entry *old = calloc(hdr.nent ? hdr.nent : 1, sizeof *old);
    if (!old) return -ENOMEM;
    if (copy_in(old, arg + sizeof hdr, hdr.nent * sizeof *old) < 0) { free(old); return -EFAULT; }
    for (uint32_t i = 0; i < hdr.nent; i++) {
        memset(&v->cpuid[i], 0, sizeof v->cpuid[i]);
        v->cpuid[i].function = old[i].function;
        v->cpuid[i].eax = old[i].eax;
        v->cpuid[i].ebx = old[i].ebx;
        v->cpuid[i].ecx = old[i].ecx;
        v->cpuid[i].edx = old[i].edx;
    }
    v->ncpuid = hdr.nent;
    free(old);
    return 0;
}

static int64_t vcpu_get_cpuid2(kvm_vcpu_t *v, uint64_t arg) {
    struct kvm_cpuid2 hdr;
    if (copy_in(&hdr, arg, sizeof hdr) < 0) return -EFAULT;
    if (hdr.nent < v->ncpuid) {
        hdr.nent = v->ncpuid;
        copy_out(arg, &hdr, sizeof hdr);
        return -E2BIG;
    }
    hdr.nent = v->ncpuid;
    if (copy_out(arg, &hdr, sizeof hdr) < 0) return -EFAULT;
    return copy_out(arg + sizeof hdr, v->cpuid, v->ncpuid * sizeof(struct kvm_cpuid_entry2));
}

static int64_t vcpu_ioctl_locked(kvm_vcpu_t *v, uint64_t req, uint64_t arg) {
    switch (req) {
    case KVM_RUN:
        if (arg) return -EINVAL;
        return kvm_arch->vcpu_run(v);
    case KVM_GET_REGS: {
        struct kvm_regs r;
        kvm_arch->get_regs(v, &r);
        return copy_out(arg, &r, sizeof r);
    }
    case KVM_SET_REGS: {
        struct kvm_regs r;
        if (copy_in(&r, arg, sizeof r) < 0) return -EFAULT;
        kvm_arch->set_regs(v, &r);
        return 0;
    }
    case KVM_GET_SREGS: {
        struct kvm_sregs s;
        kvm_arch->get_sregs(v, &s);
        return copy_out(arg, &s, sizeof s);
    }
    case KVM_SET_SREGS: {
        struct kvm_sregs s;
        if (copy_in(&s, arg, sizeof s) < 0) return -EFAULT;
        return kvm_arch->set_sregs(v, &s);
    }
    case KVM_GET_FPU: {
        struct kvm_fpu f;
        fpu_to_kvm(v->guest_fpu, &f);
        return copy_out(arg, &f, sizeof f);
    }
    case KVM_SET_FPU: {
        struct kvm_fpu f;
        if (copy_in(&f, arg, sizeof f) < 0) return -EFAULT;
        kvm_to_fpu(&f, v->guest_fpu);
        if (fpu_xcr0) {
            uint64_t bv;
            memcpy(&bv, v->guest_fpu + 512, 8);
            bv |= 3;
            memcpy(v->guest_fpu + 512, &bv, 8);
        }
        return 0;
    }
    case KVM_GET_XSAVE:
    case KVM_GET_XSAVE2: {
        struct kvm_xsave *x = calloc(1, sizeof *x);
        if (!x) return -ENOMEM;
        if (fpu_xcr0) {
            memcpy(x->region, v->guest_fpu, fpu_area_size);
        } else {
            memcpy(x->region, v->guest_fpu, 512);
            uint64_t bv = 3;
            memcpy((uint8_t *)x->region + 512, &bv, 8);
        }
        int r = copy_out(arg, x, sizeof *x);
        free(x);
        return r;
    }
    case KVM_SET_XSAVE: {
        struct kvm_xsave *x = calloc(1, sizeof *x);
        if (!x) return -ENOMEM;
        if (copy_in(x, arg, sizeof *x) < 0) { free(x); return -EFAULT; }
        uint64_t bv;
        memcpy(&bv, (uint8_t *)x->region + 512, 8);
        uint8_t *fx = (uint8_t *)x->region;
        uint32_t mxcsr;
        memcpy(&mxcsr, fx + 24, 4);
        mxcsr &= fpu_mxcsr_mask;
        memcpy(fx + 24, &mxcsr, 4);
        if (fpu_xcr0) {
            uint64_t comp;
            memcpy(&comp, fx + 520, 8);
            if (comp & (1ull << 63)) { free(x); return -EINVAL; }
            bv &= fpu_xcr0;
            memset(fx + 512, 0, 64);
            memcpy(fx + 512, &bv, 8);
            memcpy(v->guest_fpu, fx, fpu_area_size);
        } else if (bv & 3) memcpy(v->guest_fpu, x->region, 512);
        else {
            uint16_t fcw = 0x37F;
            uint32_t mx = 0x1F80;
            memset(v->guest_fpu, 0, 512);
            memcpy(v->guest_fpu, &fcw, 2);
            memcpy(v->guest_fpu + 24, &mx, 4);
        }
        free(x);
        return 0;
    }
    case KVM_GET_XCRS: {
        struct kvm_xcrs xc;
        memset(&xc, 0, sizeof xc);
        xc.nr_xcrs = 1;
        xc.xcrs[0].xcr = 0;
        xc.xcrs[0].value = v->xcr0;
        return copy_out(arg, &xc, sizeof xc);
    }
    case KVM_SET_XCRS: {
        struct kvm_xcrs xc;
        if (copy_in(&xc, arg, sizeof xc) < 0) return -EFAULT;
        if (xc.nr_xcrs > KVM_MAX_XCRS || xc.flags) return -EINVAL;
        for (uint32_t i = 0; i < xc.nr_xcrs; i++) {
            if (xc.xcrs[i].xcr != 0 || !kvm_xcr0_valid(v, xc.xcrs[i].value)) return -EINVAL;
            v->xcr0 = xc.xcrs[i].value;
        }
        return 0;
    }
    case KVM_GET_MSRS:
        return vcpu_msrs(v, arg, false);
    case KVM_SET_MSRS:
        return vcpu_msrs(v, arg, true);
    case KVM_SET_CPUID2:
        return vcpu_set_cpuid2(v, arg);
    case KVM_SET_CPUID:
        return vcpu_set_cpuid1(v, arg);
    case KVM_GET_CPUID2:
        return vcpu_get_cpuid2(v, arg);
    case KVM_INTERRUPT: {
        struct kvm_interrupt irq;
        if (copy_in(&irq, arg, sizeof irq) < 0) return -EFAULT;
        if (irq.irq >= KVM_NR_INTERRUPTS) return -EINVAL;
        if (v->pending_irq >= 0) return -ENXIO;
        v->pending_irq = (int32_t)irq.irq;
        return 0;
    }
    case KVM_NMI:
        v->nmi_pending = true;
        return 0;
    case KVM_GET_MP_STATE: {
        struct kvm_mp_state mp = { __atomic_load_n(&v->mp_state, __ATOMIC_SEQ_CST) };
        return copy_out(arg, &mp, sizeof mp);
    }
    case KVM_SET_MP_STATE: {
        struct kvm_mp_state mp;
        if (copy_in(&mp, arg, sizeof mp) < 0) return -EFAULT;
        if (mp.mp_state > KVM_MP_STATE_SIPI_RECEIVED) return -EINVAL;
        if (!v->vm->irqchip_split && mp.mp_state != KVM_MP_STATE_RUNNABLE) return -EINVAL;
        __atomic_store_n(&v->mp_state, mp.mp_state, __ATOMIC_SEQ_CST);
        return 0;
    }
    case KVM_GET_LAPIC: {
        if (!v->vm->irqchip_split) return -ENXIO;
        struct kvm_lapic_state *ls = calloc(1, sizeof *ls);
        if (!ls) return -ENOMEM;
        kvm_lapic_get_state(v, ls);
        int r = copy_out(arg, ls, sizeof *ls);
        free(ls);
        return r;
    }
    case KVM_SET_LAPIC: {
        if (!v->vm->irqchip_split) return -ENXIO;
        struct kvm_lapic_state *ls = calloc(1, sizeof *ls);
        if (!ls) return -ENOMEM;
        int r = copy_in(ls, arg, sizeof *ls);
        if (r == 0) kvm_lapic_set_state(v, ls);
        free(ls);
        return r;
    }
    case KVM_GET_VCPU_EVENTS: {
        struct kvm_vcpu_events e;
        kvm_arch->get_events(v, &e);
        return copy_out(arg, &e, sizeof e);
    }
    case KVM_SET_VCPU_EVENTS: {
        struct kvm_vcpu_events e;
        if (copy_in(&e, arg, sizeof e) < 0) return -EFAULT;
        return kvm_arch->set_events(v, &e);
    }
    case KVM_GET_DEBUGREGS: {
        struct kvm_debugregs d;
        memset(&d, 0, sizeof d);
        memcpy(d.db, v->dr, sizeof d.db);
        d.dr6 = v->dr6;
        kvm_arch->state_enter(v);
        d.dr7 = kvm_arch->get_dr7(v);
        kvm_arch->state_leave(v);
        return copy_out(arg, &d, sizeof d);
    }
    case KVM_SET_DEBUGREGS: {
        struct kvm_debugregs d;
        if (copy_in(&d, arg, sizeof d) < 0) return -EFAULT;
        if (d.flags) return -EINVAL;
        memcpy(v->dr, d.db, sizeof v->dr);
        v->dr6 = (d.dr6 & 0xFFFFull) | 0xFFFF0FF0ull;
        kvm_arch->state_enter(v);
        kvm_arch->set_dr7(v, d.dr7);
        kvm_arch->state_leave(v);
        return 0;
    }
    case KVM_X86_SETUP_MCE: {
        uint64_t cap;
        if (copy_in(&cap, arg, sizeof cap) < 0) return -EFAULT;
        uint32_t banks = cap & 0xFF;
        if (!banks || banks > KVM_MCE_BANKS) return -EINVAL;
        if (cap & ~(MCG_CTL_P | 0xFFull | (1ull << 24))) return -EINVAL;
        v->mcg_cap = cap;
        v->mcg_ctl = (cap & MCG_CTL_P) ? ~0ull : 0;
        for (uint32_t i = 0; i < banks; i++) v->mce_banks[i * 4] = ~0ull;
        return 0;
    }
    case KVM_X86_SET_MCE: {
        struct kvm_x86_mce m;
        if (copy_in(&m, arg, sizeof m) < 0) return -EFAULT;
        uint32_t banks = v->mcg_cap & 0xFF;
        if (m.bank >= banks || !(m.status & (1ull << 63))) return -EINVAL;
        v->mce_banks[m.bank * 4 + 1] = m.status;
        v->mce_banks[m.bank * 4 + 2] = m.addr;
        v->mce_banks[m.bank * 4 + 3] = m.misc;
        v->mcg_status = m.mcg_status;
        v->exc_pending = true;
        v->exc_vector = 18;
        v->exc_has_error = false;
        return 0;
    }
    case KVM_GET_TSC_KHZ:
        return (int64_t)g_tsc_khz;
    case KVM_SET_TSC_KHZ:
        return arg == g_tsc_khz ? 0 : -EINVAL;
    case KVM_SET_SIGNAL_MASK:
        return 0;
    case KVM_SET_GUEST_DEBUG: {
        struct kvm_guest_debug g;
        if (copy_in(&g, arg, sizeof g) < 0) return -EFAULT;
        return g.control ? -EINVAL : 0;
    }
    case KVM_TPR_ACCESS_REPORTING:
    case KVM_SET_VAPIC_ADDR:
        return v->vm->irqchip_split ? 0 : -EINVAL;
    case KVM_KVMCLOCK_CTRL:
    case KVM_ENABLE_CAP:
    case KVM_TRANSLATE:
        return -EINVAL;
    case KVM_CHECK_EXTENSION:
        return check_extension(arg);
    default:
        return -ENOTTY;
    }
}

static int64_t kvm_vcpu_ioctl(vnode_t *node, uint64_t req, uint64_t arg) {
    kvm_vcpu_t *v = node->fs_data;
    task_t *t = syscall_cur_task();
    if (!t || t->mm != v->vm->mm) return -EIO;
    kmutex_lock(&v->mutex);
    int64_t r = vcpu_ioctl_locked(v, req, arg);
    kmutex_unlock(&v->mutex);
    return r;
}

static int kvm_vcpu_mmap_page(vnode_t *node, uint64_t offset, uintptr_t *phys_out) {
    kvm_vcpu_t *v = node->fs_data;
    if (offset >= KVM_RUN_PAGES * 4096ull) return -ENXIO;
    *phys_out = v->run_phys + (offset & ~0xFFFull);
    return 0;
}

static void kvm_vcpu_release(vnode_t *node) {
    kvm_vcpu_t *v = node->fs_data;
    if (v) {
        kvm_vm_t *vm = v->vm;
        kmutex_lock(&vm->lock);
        if (vm->vcpus[v->id] == v) vm->vcpus[v->id] = NULL;
        kmutex_unlock(&vm->lock);
        kvm_arch->vcpu_destroy(v);
        pmm_free(v->run, KVM_RUN_PAGES);
        free(v);
        vm_put(vm);
    }
    free(node);
}

static void kvm_vm_release(vnode_t *node) {
    kvm_vm_t *vm = node->fs_data;
    if (vm) vm_put(vm);
    free(node);
}

static int kvm_stat(vnode_t *node, vfs_stat_t *st) {
    memset(st, 0, sizeof *st);
    st->st_type = VFS_NODE_CHARDEV;
    st->st_mode = node->mode | 0020000;
    st->st_nlink = 1;
    st->st_rdev = node->rdev;
    return 0;
}

static const vnode_ops_t kvm_dev_ops = {
    .ioctl_raw = kvm_dev_ioctl,
    .stat = kvm_stat,
};

static const vnode_ops_t kvm_vm_ops = {
    .ioctl_raw = kvm_vm_ioctl,
    .unref = kvm_vm_release,
    .stat = kvm_stat,
};

static const vnode_ops_t kvm_vcpu_ops = {
    .ioctl_raw = kvm_vcpu_ioctl,
    .mmap_page = kvm_vcpu_mmap_page,
    .unref = kvm_vcpu_release,
    .stat = kvm_stat,
};

static vnode_t g_kvm_node;

void kvm_dev_init(void) {
    if (!kvm_arch_probe()) {
        serial_printf("[kvm] no usable VMX or SVM, /dev/kvm not created\n");
        return;
    }
    g_kvm_node.type = VFS_NODE_CHARDEV;
    g_kvm_node.mode = 0666;
    g_kvm_node.ops = &kvm_dev_ops;
    g_kvm_node.refcount = 1;
    g_kvm_node.rdev = vfs_makedev(10, 232);
    devfs_register("kvm", &g_kvm_node);
    serial_printf("[kvm] /dev/kvm ready\n");
}
