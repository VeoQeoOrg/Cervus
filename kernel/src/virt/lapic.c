#include "../../include/virt/vmx.h"
#include "../../include/virt/kvm.h"
#include "../../include/syscall/syscall_internal.h"
#include "../../include/syscall/errno.h"
#include "../../include/fs/vfs.h"
#include "../../include/sched/sched.h"
#include "../../include/drivers/timer.h"
#include <string.h>
#include <stdlib.h>

extern int eventfd_set_hook(vnode_t *n, void (*fn)(void *), void *ctx);

#define APIC_ID      0x20
#define APIC_LVR     0x30
#define APIC_TPR     0x80
#define APIC_APR     0x90
#define APIC_PPR     0xA0
#define APIC_EOI     0xB0
#define APIC_LDR     0xD0
#define APIC_DFR     0xE0
#define APIC_SVR     0xF0
#define APIC_ISR     0x100
#define APIC_TMR     0x180
#define APIC_IRR     0x200
#define APIC_ESR     0x280
#define APIC_LVTCMCI 0x2F0
#define APIC_ICR     0x300
#define APIC_ICR2    0x310
#define APIC_LVTT    0x320
#define APIC_LVTTHMR 0x330
#define APIC_LVTPC   0x340
#define APIC_LVT0    0x350
#define APIC_LVT1    0x360
#define APIC_LVTERR  0x370
#define APIC_TMICT   0x380
#define APIC_TMCCT   0x390
#define APIC_TDCR    0x3E0

#define APIC_VERSION     0x00050014u
#define LVT_MASKED       (1u << 16)
#define LVT_PERIODIC     (1u << 17)
#define LVT_REMOTE_IRR   (1u << 14)
#define SVR_ENABLED      (1u << 8)
#define ESR_RECV_ILLEGAL (1u << 6)

#define DM_FIXED  0
#define DM_LOWEST 1
#define DM_NMI    4
#define DM_INIT   5
#define DM_SIPI   6
#define DM_EXTINT 7

#define APIC_BASE_ENABLE (1ull << 11)
#define APIC_BASE_EXTD   (1ull << 10)
#define APIC_BASE_ADDR   0x000FFFFFFFFFF000ull

#define MIN_PERIOD_NS 100000ull

static inline uint32_t reg_get(kvm_lapic_t *a, uint32_t off) {
    uint32_t v;
    memcpy(&v, a->regs + off, 4);
    return v;
}

static inline void reg_set(kvm_lapic_t *a, uint32_t off, uint32_t v) {
    memcpy(a->regs + off, &v, 4);
}

static inline uint32_t vec_off(uint32_t base, int v) { return base + (uint32_t)(v >> 5) * 0x10; }

static inline bool vec_test(kvm_lapic_t *a, uint32_t base, int v) {
    return (reg_get(a, vec_off(base, v)) >> (v & 31)) & 1;
}

static inline void vec_set(kvm_lapic_t *a, uint32_t base, int v) {
    uint32_t o = vec_off(base, v);
    reg_set(a, o, reg_get(a, o) | (1u << (v & 31)));
}

static inline void vec_clr(kvm_lapic_t *a, uint32_t base, int v) {
    uint32_t o = vec_off(base, v);
    reg_set(a, o, reg_get(a, o) & ~(1u << (v & 31)));
}

static int vec_highest(kvm_lapic_t *a, uint32_t base) {
    for (int i = 7; i >= 0; i--) {
        uint32_t w = reg_get(a, base + (uint32_t)i * 0x10);
        if (w) return i * 32 + 31 - __builtin_clz(w);
    }
    return -1;
}

static uint32_t ppr_locked(kvm_lapic_t *a) {
    uint32_t tpr = reg_get(a, APIC_TPR) & 0xFF;
    int isr = vec_highest(a, APIC_ISR);
    uint32_t isrv = isr < 0 ? 0 : (uint32_t)isr;
    return (tpr & 0xF0) >= (isrv & 0xF0) ? tpr : (isrv & 0xF0);
}

static uint32_t divide_of(uint32_t tdcr) {
    uint32_t v = (tdcr & 3) | ((tdcr >> 1) & 4);
    return 1u << ((v + 1) & 7);
}

static void timer_start_locked(kvm_lapic_t *a, uint32_t count, uint64_t now) {
    if (!count) {
        a->timer_deadline = 0;
        a->timer_period = 0;
        return;
    }
    uint64_t period = (uint64_t)count * a->timer_divide;
    if ((reg_get(a, APIC_LVTT) & LVT_PERIODIC) && period < MIN_PERIOD_NS) period = MIN_PERIOD_NS;
    a->timer_period = period;
    a->timer_deadline = now + period;
}

static uint32_t timer_current_locked(kvm_lapic_t *a, uint64_t now) {
    if (!a->timer_deadline || now >= a->timer_deadline) return 0;
    uint64_t left = (a->timer_deadline - now) / a->timer_divide;
    uint32_t init = reg_get(a, APIC_TMICT);
    return left > init ? init : (uint32_t)left;
}

static void request_event(kvm_vcpu_t *v) {
    __atomic_or_fetch(&v->requests, VCPU_REQ_EVENT, __ATOMIC_SEQ_CST);
}

void kvm_vcpu_kick(kvm_vcpu_t *v) {
    request_event(v);
    vmx_kick(v);
    uint64_t f = spinlock_acquire_irqsave(&v->halt_lock);
    waitq_wake_all(&v->halt_wq);
    spinlock_release_irqrestore(&v->halt_lock, f);
}

void kvm_lapic_reset(kvm_vcpu_t *v, bool init) {
    kvm_lapic_t *a = &v->lapic;
    uint64_t f = spinlock_acquire_irqsave(&a->lock);
    uint32_t id = init ? reg_get(a, APIC_ID) : v->id << 24;
    memset(a->regs, 0, sizeof a->regs);
    reg_set(a, APIC_ID, id);
    reg_set(a, APIC_LVR, APIC_VERSION);
    reg_set(a, APIC_DFR, 0xFFFFFFFFu);
    reg_set(a, APIC_SVR, 0xFF);
    for (uint32_t o = APIC_LVTT; o <= APIC_LVTERR; o += 0x10) reg_set(a, o, LVT_MASKED);
    reg_set(a, APIC_LVTCMCI, LVT_MASKED);
    a->timer_deadline = 0;
    a->timer_period = 0;
    a->timer_divide = divide_of(0);
    spinlock_release_irqrestore(&a->lock, f);
}

void kvm_lapic_get_state(kvm_vcpu_t *v, struct kvm_lapic_state *s) {
    kvm_lapic_t *a = &v->lapic;
    uint64_t f = spinlock_acquire_irqsave(&a->lock);
    memcpy(s->regs, a->regs, sizeof a->regs);
    uint32_t cur = timer_current_locked(a, sched_now_ns());
    uint32_t ppr = ppr_locked(a);
    spinlock_release_irqrestore(&a->lock, f);
    memcpy(s->regs + APIC_TMCCT, &cur, 4);
    memcpy(s->regs + APIC_PPR, &ppr, 4);
}

void kvm_lapic_set_state(kvm_vcpu_t *v, const struct kvm_lapic_state *s) {
    kvm_lapic_t *a = &v->lapic;
    uint64_t now = sched_now_ns();
    uint64_t f = spinlock_acquire_irqsave(&a->lock);
    memcpy(a->regs, s->regs, sizeof a->regs);
    reg_set(a, APIC_LVR, APIC_VERSION);
    a->timer_divide = divide_of(reg_get(a, APIC_TDCR));
    uint32_t cur = reg_get(a, APIC_TMCCT);
    uint32_t init = reg_get(a, APIC_TMICT);
    reg_set(a, APIC_TMCCT, 0);
    reg_set(a, APIC_PPR, 0);
    timer_start_locked(a, init, now);
    if (cur && cur < init && a->timer_deadline) a->timer_deadline = now + (uint64_t)cur * a->timer_divide;
    spinlock_release_irqrestore(&a->lock, f);
    request_event(v);
}

static int accept_irq(kvm_vcpu_t *v, int dm, int vector, bool level, bool trig) {
    kvm_lapic_t *a = &v->lapic;
    switch (dm) {
    case DM_FIXED:
    case DM_LOWEST: {
        uint64_t f = spinlock_acquire_irqsave(&a->lock);
        if (!(reg_get(a, APIC_SVR) & SVR_ENABLED)) {
            spinlock_release_irqrestore(&a->lock, f);
            return 0;
        }
        if (vector < 16) {
            reg_set(a, APIC_ESR, reg_get(a, APIC_ESR) | ESR_RECV_ILLEGAL);
            spinlock_release_irqrestore(&a->lock, f);
            return 0;
        }
        bool coalesced = vec_test(a, APIC_IRR, vector);
        vec_set(a, APIC_IRR, vector);
        if (trig) vec_set(a, APIC_TMR, vector);
        else vec_clr(a, APIC_TMR, vector);
        spinlock_release_irqrestore(&a->lock, f);
        kvm_vcpu_kick(v);
        return coalesced ? 0 : 1;
    }
    case DM_NMI:
        __atomic_store_n(&v->nmi_pending, true, __ATOMIC_SEQ_CST);
        kvm_vcpu_kick(v);
        return 1;
    case DM_INIT:
        if (trig && !level) return 0;
        __atomic_store_n(&v->mp_state, KVM_MP_STATE_INIT_RECEIVED, __ATOMIC_SEQ_CST);
        kvm_vcpu_kick(v);
        return 1;
    case DM_SIPI:
        if (__atomic_load_n(&v->mp_state, __ATOMIC_SEQ_CST) != KVM_MP_STATE_INIT_RECEIVED) return 0;
        v->sipi_vector = (uint32_t)vector;
        __atomic_store_n(&v->mp_state, KVM_MP_STATE_SIPI_RECEIVED, __ATOMIC_SEQ_CST);
        kvm_vcpu_kick(v);
        return 1;
    default:
        return 0;
    }
}

static bool match_dest(kvm_vcpu_t *v, uint32_t dest, bool logical) {
    kvm_lapic_t *a = &v->lapic;
    if (!(v->apic_base & APIC_BASE_ENABLE)) return false;
    if (!logical) return dest == 0xFF || (reg_get(a, APIC_ID) >> 24) == dest;
    uint32_t ldr = reg_get(a, APIC_LDR) >> 24;
    uint32_t model = reg_get(a, APIC_DFR) >> 28;
    if (model == 0xF) return (ldr & dest) != 0;
    if (model == 0x0) {
        bool cluster = (dest >> 4) == 0xF || (dest >> 4) == (ldr >> 4);
        return cluster && (ldr & dest & 0xF);
    }
    return false;
}

static int deliver(kvm_vm_t *vm, kvm_vcpu_t *src, int shorthand, uint32_t dest, bool logical,
                   int dm, int vector, bool level, bool trig) {
    int ret = -1;
    for (uint32_t i = 0; i < vm->nvcpus && i < KVM_MAX_VCPUS; i++) {
        kvm_vcpu_t *v = vm->vcpus[i];
        if (!v) continue;
        bool hit;
        switch (shorthand) {
        case 1: hit = v == src; break;
        case 2: hit = true; break;
        case 3: hit = v != src; break;
        default: hit = match_dest(v, dest, logical); break;
        }
        if (!hit) continue;
        int r = accept_irq(v, dm, vector, level, trig);
        ret = ret < 0 ? r : ret + r;
        if (dm == DM_LOWEST && shorthand == 0) break;
    }
    return ret;
}

int kvm_deliver_msi(kvm_vm_t *vm, uint64_t addr, uint32_t data) {
    if ((addr & 0xFFFFFFFFFFF00000ull) != 0xFEE00000ull) return -1;
    uint32_t dest = (uint32_t)(addr >> 12) & 0xFF;
    bool logical = (addr >> 2) & 1;
    int vector = (int)(data & 0xFF);
    int dm = (int)((data >> 8) & 7);
    bool trig = (data >> 15) & 1;
    bool level = trig || ((data >> 14) & 1);
    return deliver(vm, NULL, 0, dest, logical, dm, vector, level, trig);
}

static void send_ipi(kvm_vcpu_t *v, uint32_t low, uint32_t high) {
    deliver(v->vm, v, (int)((low >> 18) & 3), high >> 24, (low >> 11) & 1,
            (int)((low >> 8) & 7), (int)(low & 0xFF), (low >> 14) & 1, (low >> 15) & 1);
}

static int do_eoi(kvm_vcpu_t *v) {
    kvm_lapic_t *a = &v->lapic;
    int ret = -1;
    uint64_t f = spinlock_acquire_irqsave(&a->lock);
    int vec = vec_highest(a, APIC_ISR);
    if (vec >= 0) {
        vec_clr(a, APIC_ISR, vec);
        if (vec_test(a, APIC_TMR, vec)) ret = vec;
    }
    spinlock_release_irqrestore(&a->lock, f);
    if (vec >= 0) request_event(v);
    return ret;
}

static int lapic_reg_write(kvm_vcpu_t *v, uint32_t off, uint32_t val) {
    kvm_lapic_t *a = &v->lapic;
    if (off == APIC_EOI) {
        int vec = do_eoi(v);
        if (vec >= 0 && v->vm->irqchip_split) {
            v->run->exit_reason = KVM_EXIT_IOAPIC_EOI;
            v->run->eoi.vector = (uint8_t)vec;
            return 0;
        }
        return 1;
    }
    if (off == APIC_ICR) {
        uint64_t f = spinlock_acquire_irqsave(&a->lock);
        reg_set(a, APIC_ICR, val & ~(1u << 12));
        uint32_t high = reg_get(a, APIC_ICR2);
        spinlock_release_irqrestore(&a->lock, f);
        send_ipi(v, val, high);
        return 1;
    }

    uint64_t now = sched_now_ns();
    uint64_t f = spinlock_acquire_irqsave(&a->lock);
    bool sw_on = (reg_get(a, APIC_SVR) & SVR_ENABLED) != 0;
    switch (off) {
    case APIC_ID:
        reg_set(a, APIC_ID, val & 0xFF000000u);
        break;
    case APIC_TPR:
        reg_set(a, APIC_TPR, val & 0xFF);
        break;
    case APIC_LDR:
        reg_set(a, APIC_LDR, val & 0xFF000000u);
        break;
    case APIC_DFR:
        reg_set(a, APIC_DFR, val | 0x0FFFFFFFu);
        break;
    case APIC_SVR:
        reg_set(a, APIC_SVR, val & 0x3FF);
        if (!(val & SVR_ENABLED)) {
            for (uint32_t o = APIC_LVTT; o <= APIC_LVTERR; o += 0x10) reg_set(a, o, reg_get(a, o) | LVT_MASKED);
            reg_set(a, APIC_LVTCMCI, reg_get(a, APIC_LVTCMCI) | LVT_MASKED);
        }
        break;
    case APIC_ESR:
        reg_set(a, APIC_ESR, 0);
        break;
    case APIC_ICR2:
        reg_set(a, APIC_ICR2, val & 0xFF000000u);
        break;
    case APIC_LVTT:
        if (!sw_on) val |= LVT_MASKED;
        reg_set(a, APIC_LVTT, val & (0xFFu | LVT_MASKED | LVT_PERIODIC));
        break;
    case APIC_LVTTHMR:
    case APIC_LVTPC:
    case APIC_LVT0:
    case APIC_LVT1:
    case APIC_LVTERR:
    case APIC_LVTCMCI: {
        uint32_t mask = off == APIC_LVTERR ? (0xFFu | LVT_MASKED)
                                            : (0xFFu | (7u << 8) | (1u << 13) | (1u << 15) | LVT_MASKED);
        if (!sw_on) val |= LVT_MASKED;
        reg_set(a, off, (val & mask) | (reg_get(a, off) & LVT_REMOTE_IRR));
        break;
    }
    case APIC_TMICT:
        reg_set(a, APIC_TMICT, val);
        timer_start_locked(a, val, now);
        break;
    case APIC_TDCR:
        reg_set(a, APIC_TDCR, val & 0xB);
        a->timer_divide = divide_of(val);
        break;
    default:
        break;
    }
    spinlock_release_irqrestore(&a->lock, f);
    request_event(v);
    return 1;
}

static uint32_t lapic_reg_read(kvm_vcpu_t *v, uint32_t off) {
    kvm_lapic_t *a = &v->lapic;
    if (off >= KVM_APIC_REG_SIZE) return 0;
    uint64_t f = spinlock_acquire_irqsave(&a->lock);
    uint32_t r;
    switch (off) {
    case APIC_PPR: r = ppr_locked(a); break;
    case APIC_TMCCT: r = timer_current_locked(a, sched_now_ns()); break;
    case APIC_LVR: r = APIC_VERSION; break;
    case APIC_EOI: r = 0; break;
    default: r = reg_get(a, off); break;
    }
    spinlock_release_irqrestore(&a->lock, f);
    return r;
}

static bool lapic_mmio_hit(kvm_vcpu_t *v, uint64_t gpa) {
    if (!v->vm->irqchip_split) return false;
    if (!(v->apic_base & APIC_BASE_ENABLE) || (v->apic_base & APIC_BASE_EXTD)) return false;
    uint64_t base = v->apic_base & APIC_BASE_ADDR;
    return gpa >= base && gpa < base + 4096;
}

bool kvm_mmio_kernel_read(kvm_vcpu_t *v, uint64_t gpa, int size, uint64_t *val) {
    if (!lapic_mmio_hit(v, gpa)) return false;
    uint32_t o = (uint32_t)gpa & 0xFFF;
    uint64_t r = 0;
    if ((o & 0xF) < 4) r = lapic_reg_read(v, o & ~0xFu) >> ((o & 3) * 8);
    *val = size >= 8 ? r : (r & ((1ull << (size * 8)) - 1));
    return true;
}

int kvm_mmio_kernel_write(kvm_vcpu_t *v, uint64_t gpa, int size, uint64_t val) {
    if (!lapic_mmio_hit(v, gpa)) return -1;
    uint32_t o = (uint32_t)gpa & 0xFFF;
    if ((o & 0xF) || size < 4) return 1;
    return lapic_reg_write(v, o, (uint32_t)val);
}

int kvm_lapic_pending(kvm_vcpu_t *v) {
    kvm_lapic_t *a = &v->lapic;
    if (!(v->apic_base & APIC_BASE_ENABLE)) return -1;
    uint64_t f = spinlock_acquire_irqsave(&a->lock);
    int irr = vec_highest(a, APIC_IRR);
    int r = -1;
    if (irr >= 16 && ((uint32_t)irr & 0xF0) > (ppr_locked(a) & 0xF0)) r = irr;
    spinlock_release_irqrestore(&a->lock, f);
    return r;
}

void kvm_lapic_ack(kvm_vcpu_t *v, int vector) {
    kvm_lapic_t *a = &v->lapic;
    uint64_t f = spinlock_acquire_irqsave(&a->lock);
    vec_clr(a, APIC_IRR, vector);
    vec_set(a, APIC_ISR, vector);
    spinlock_release_irqrestore(&a->lock, f);
}

bool kvm_lapic_accept_pic(kvm_vcpu_t *v) {
    if (!(v->apic_base & APIC_BASE_ENABLE)) return true;
    uint32_t lvt0 = reg_get(&v->lapic, APIC_LVT0);
    return !(lvt0 & LVT_MASKED) && ((lvt0 >> 8) & 7) == DM_EXTINT;
}

uint8_t kvm_lapic_get_cr8(kvm_vcpu_t *v) {
    return (uint8_t)((reg_get(&v->lapic, APIC_TPR) >> 4) & 0xF);
}

void kvm_lapic_set_cr8(kvm_vcpu_t *v, uint8_t cr8) {
    kvm_lapic_t *a = &v->lapic;
    uint64_t f = spinlock_acquire_irqsave(&a->lock);
    reg_set(a, APIC_TPR, (uint32_t)(cr8 & 0xF) << 4);
    spinlock_release_irqrestore(&a->lock, f);
    request_event(v);
}

uint64_t kvm_lapic_timer_deadline(kvm_vcpu_t *v) {
    return __atomic_load_n(&v->lapic.timer_deadline, __ATOMIC_RELAXED);
}

void kvm_lapic_timer_check(kvm_vcpu_t *v, uint64_t now) {
    kvm_lapic_t *a = &v->lapic;
    uint64_t d = __atomic_load_n(&a->timer_deadline, __ATOMIC_RELAXED);
    if (!d || now < d) return;
    uint64_t f = spinlock_acquire_irqsave(&a->lock);
    if (a->timer_deadline && now >= a->timer_deadline) {
        uint32_t lvt = reg_get(a, APIC_LVTT);
        if ((lvt & LVT_PERIODIC) && a->timer_period) {
            a->timer_deadline += a->timer_period;
            if (a->timer_deadline <= now) a->timer_deadline = now + a->timer_period;
        } else {
            a->timer_deadline = 0;
        }
        int vec = (int)(lvt & 0xFF);
        if (!(lvt & LVT_MASKED) && (reg_get(a, APIC_SVR) & SVR_ENABLED) && vec >= 16) {
            vec_set(a, APIC_IRR, vec);
            vec_clr(a, APIC_TMR, vec);
        }
    }
    spinlock_release_irqrestore(&a->lock, f);
}

int kvm_set_gsi(kvm_vm_t *vm, uint32_t gsi, int level) {
    kvm_route_t hit[4];
    int n = 0;
    uint64_t f = spinlock_acquire_irqsave(&vm->route_lock);
    for (uint32_t i = 0; i < vm->nroutes && n < 4; i++)
        if (vm->routes[i].gsi == gsi && vm->routes[i].type == KVM_IRQ_ROUTING_MSI) hit[n++] = vm->routes[i];
    spinlock_release_irqrestore(&vm->route_lock, f);
    if (!n || !level) return -1;
    int ret = -1;
    for (int i = 0; i < n; i++) {
        uint64_t addr = ((uint64_t)hit[i].address_hi << 32) | hit[i].address_lo;
        int r = kvm_deliver_msi(vm, addr, hit[i].data);
        if (r >= 0) ret = ret < 0 ? r : ret + r;
    }
    return ret;
}

int64_t kvm_set_routing(kvm_vm_t *vm, uint64_t arg) {
    struct kvm_irq_routing hdr;
    if (syscall_copy_from_user(&hdr, (const void *)arg, sizeof hdr) < 0) return -EFAULT;
    if (hdr.nr > KVM_MAX_IRQ_ROUTES || hdr.flags) return -EINVAL;
    struct kvm_irq_routing_entry *ue = NULL;
    kvm_route_t *rt = NULL;
    if (hdr.nr) {
        ue = calloc(hdr.nr, sizeof *ue);
        rt = calloc(hdr.nr, sizeof *rt);
        if (!ue || !rt) { free(ue); free(rt); return -ENOMEM; }
        if (syscall_copy_from_user(ue, (const void *)(arg + sizeof hdr), hdr.nr * sizeof *ue) < 0) {
            free(ue); free(rt);
            return -EFAULT;
        }
    }
    for (uint32_t i = 0; i < hdr.nr; i++) {
        if (ue[i].gsi >= KVM_MAX_IRQ_ROUTES || ue[i].flags ||
            (ue[i].type != KVM_IRQ_ROUTING_MSI && ue[i].type != KVM_IRQ_ROUTING_IRQCHIP)) {
            free(ue); free(rt);
            return -EINVAL;
        }
        rt[i].gsi = ue[i].gsi;
        rt[i].type = ue[i].type;
        if (ue[i].type == KVM_IRQ_ROUTING_MSI) {
            rt[i].address_lo = ue[i].u.msi.address_lo;
            rt[i].address_hi = ue[i].u.msi.address_hi;
            rt[i].data = ue[i].u.msi.data;
        }
    }
    free(ue);
    uint64_t f = spinlock_acquire_irqsave(&vm->route_lock);
    kvm_route_t *old = vm->routes;
    vm->routes = rt;
    vm->nroutes = hdr.nr;
    spinlock_release_irqrestore(&vm->route_lock, f);
    free(old);
    return 0;
}

static void irqfd_fire(void *ctx) {
    kvm_irqfd_entry_t *e = ctx;
    kvm_set_gsi(e->vm, e->gsi, 1);
}

int64_t kvm_irqfd(kvm_vm_t *vm, const struct kvm_irqfd *a) {
    if (a->flags & ~KVM_IRQFD_FLAG_DEASSIGN) return -EINVAL;
    if (a->gsi >= KVM_MAX_IRQ_ROUTES) return -EINVAL;
    task_t *t = syscall_cur_task();
    vfs_file_t *file = fd_get(t->fd_table, (int)a->fd);
    if (!file) return -EBADF;
    vnode_t *efd = file->vnode;
    vnode_ref(efd);
    fd_put(file);

    if (a->flags & KVM_IRQFD_FLAG_DEASSIGN) {
        kvm_irqfd_entry_t *found = NULL;
        uint64_t f = spinlock_acquire_irqsave(&vm->irqfd_lock);
        for (kvm_irqfd_entry_t **pp = &vm->irqfds; *pp; pp = &(*pp)->next) {
            if ((*pp)->efd == efd && (*pp)->gsi == a->gsi) {
                found = *pp;
                *pp = found->next;
                break;
            }
        }
        spinlock_release_irqrestore(&vm->irqfd_lock, f);
        vnode_unref(efd);
        if (!found) return -ENOENT;
        eventfd_set_hook(found->efd, NULL, NULL);
        vnode_unref(found->efd);
        free(found);
        return 0;
    }

    kvm_irqfd_entry_t *e = calloc(1, sizeof *e);
    if (!e) { vnode_unref(efd); return -ENOMEM; }
    e->vm = vm;
    e->efd = efd;
    e->gsi = a->gsi;
    uint64_t f = spinlock_acquire_irqsave(&vm->irqfd_lock);
    for (kvm_irqfd_entry_t *o = vm->irqfds; o; o = o->next) {
        if (o->efd == efd) {
            spinlock_release_irqrestore(&vm->irqfd_lock, f);
            vnode_unref(efd);
            free(e);
            return -EBUSY;
        }
    }
    e->next = vm->irqfds;
    vm->irqfds = e;
    spinlock_release_irqrestore(&vm->irqfd_lock, f);

    int r = eventfd_set_hook(efd, irqfd_fire, e);
    if (r < 0) {
        f = spinlock_acquire_irqsave(&vm->irqfd_lock);
        for (kvm_irqfd_entry_t **pp = &vm->irqfds; *pp; pp = &(*pp)->next) {
            if (*pp == e) { *pp = e->next; break; }
        }
        spinlock_release_irqrestore(&vm->irqfd_lock, f);
        vnode_unref(efd);
        free(e);
        return r;
    }
    return 0;
}

void kvm_irq_destroy(kvm_vm_t *vm) {
    kvm_irqfd_entry_t *e = vm->irqfds;
    vm->irqfds = NULL;
    while (e) {
        kvm_irqfd_entry_t *n = e->next;
        eventfd_set_hook(e->efd, NULL, NULL);
        vnode_unref(e->efd);
        free(e);
        e = n;
    }
    free(vm->routes);
    vm->routes = NULL;
    vm->nroutes = 0;
}
