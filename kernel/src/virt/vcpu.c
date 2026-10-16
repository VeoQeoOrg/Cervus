#include "../../include/virt/kvm.h"
#include "../../include/sched/sched.h"
#include "../../include/smp/percpu.h"
#include "../../include/signal/signal.h"
#include "../../include/syscall/errno.h"
#include "../../include/drivers/timer.h"
#include "../../include/io/serial.h"
#include "../../include/sse/fpu.h"
#include <string.h>

#define CR0_PE (1ull << 0)
#define CR4_OSXSAVE (1ull << 18)
#define RFLAGS_IF (1ull << 9)
#define RFLAGS_DF (1ull << 10)
#define EFER_LMA (1ull << 10)

#define MSR_IA32_TSC            0x010
#define MSR_IA32_PLATFORM_ID    0x017
#define MSR_IA32_APIC_BASE      0x01B
#define MSR_IA32_FEATURE_CTL    0x03A
#define MSR_IA32_TSC_ADJUST     0x03B
#define MSR_IA32_SPEC_CTRL      0x048
#define MSR_IA32_PRED_CMD       0x049
#define MSR_IA32_UCODE_REV      0x08B
#define MSR_MTRR_CAP            0x0FE
#define MSR_IA32_MCG_CAP        0x179
#define MSR_IA32_MCG_STATUS     0x17A
#define MSR_IA32_MCG_CTL        0x17B
#define MSR_IA32_MISC_ENABLE    0x1A0
#define MSR_MTRR_PHYSBASE0      0x200
#define MSR_MTRR_FIX64K         0x250
#define MSR_MTRR_FIX16K_80000   0x258
#define MSR_MTRR_FIX16K_A0000   0x259
#define MSR_MTRR_FIX4K_C0000    0x268
#define MSR_MTRR_DEF_TYPE       0x2FF
#define MSR_IA32_MC0_CTL        0x400
#define MSR_IA32_TSC_DEADLINE   0x6E0
#define MSR_KVM_WALL_CLOCK      0x11
#define MSR_KVM_SYSTEM_TIME     0x12
#define MSR_KVM_WALL_CLOCK_NEW  0x4b564d00
#define MSR_KVM_SYSTEM_TIME_NEW 0x4b564d01

#define FEATURE_CTL_LOCKED (1ull << 0)

#define APIC_BASE_ENABLE (1ull << 11)
#define APIC_BASE_EXTD   (1ull << 10)
#define APIC_BASE_BSP    (1ull << 8)
#define APIC_BASE_ADDR   0x000FFFFFFFFFF000ull

const kvm_arch_ops_t *kvm_arch;
bool kvm_npt;

bool kvm_arch_probe(void) {
    if (kvm_arch) return true;
    if (vmx_supported()) { kvm_arch = &vmx_ops; return true; }
    if (svm_supported()) { kvm_arch = &svm_ops; kvm_npt = true; return true; }
    return false;
}

static inline uint64_t rdtsc_now(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

const struct kvm_cpuid_entry2 *kvm_find_cpuid(kvm_vcpu_t *v, uint32_t fn, uint32_t idx) {
    for (uint32_t i = 0; i < v->ncpuid; i++) {
        const struct kvm_cpuid_entry2 *e = &v->cpuid[i];
        if (e->function != fn) continue;
        if ((e->flags & KVM_CPUID_FLAG_SIGNIFCANT_INDEX) && e->index != idx) continue;
        return e;
    }
    return NULL;
}

bool kvm_xcr0_valid(kvm_vcpu_t *v, uint64_t val) {
    if (!fpu_xcr0) return val == 1;
    uint64_t allowed = fpu_xcr0;
    const struct kvm_cpuid_entry2 *e = kvm_find_cpuid(v, 0xD, 0);
    if (e) allowed &= (e->eax | ((uint64_t)e->edx << 32)) | 1;
    if (!(val & 1) || (val & ~allowed)) return false;
    if ((val & 4) && !(val & 2)) return false;
    if ((val & 0xE0) && ((val & 0xE0) != 0xE0 || !(val & 4))) return false;
    return true;
}

void kvm_queue_exception(kvm_vcpu_t *v, uint8_t vec, bool has_err, uint32_t err) {
    if (v->exc_pending) {
        uint8_t prev = v->exc_vector;
        bool contrib_prev = prev == 0 || (prev >= 10 && prev <= 13);
        bool contrib_new = vec == 0 || (vec >= 10 && vec <= 13);
        if (prev == 8) {
            v->exc_pending = false;
            v->run->exit_reason = KVM_EXIT_SHUTDOWN;
            return;
        }
        if ((contrib_prev && contrib_new) || (prev == 14 && (contrib_new || vec == 14))) {
            vec = 8; has_err = true; err = 0;
        }
    }
    v->exc_pending = true;
    v->exc_vector = vec;
    v->exc_has_error = has_err;
    v->exc_error = err;
}

void emu_inject_gp(kvm_vcpu_t *v) { kvm_queue_exception(v, 13, true, 0); }
void emu_inject_ud(kvm_vcpu_t *v) { kvm_queue_exception(v, 6, false, 0); }

void emu_inject_pf(kvm_vcpu_t *v, uint64_t gva, uint32_t error) {
    v->regs.cr2 = gva;
    kvm_queue_exception(v, 14, true, error);
}

uint64_t emu_seg_base(kvm_vcpu_t *v, int seg) {
    return kvm_arch->seg_base(v, seg);
}

void kvm_finish_insn(kvm_vcpu_t *v, uint64_t next_rip) {
    bool m64 = (v->efer & EFER_LMA) && (kvm_arch->cs_mode(v) & KVM_CS_LONG);
    kvm_arch->set_rip(v, m64 ? next_rip : (next_rip & 0xFFFFFFFFull));
    kvm_arch->clear_int_shadow(v);
}

bool kvm_canonical(uint64_t v) {
    return (uint64_t)((int64_t)(v << 16) >> 16) == v;
}

uint64_t kvm_vcpu_cr8(kvm_vcpu_t *v) {
    return v->vm->irqchip_split ? kvm_lapic_get_cr8(v) : v->cr8;
}

bool kvm_accepts_extint(kvm_vcpu_t *v) {
    return !v->vm->irqchip_split || kvm_lapic_accept_pic(v);
}

int kvm_exit_internal(kvm_vcpu_t *v, uint32_t suberror, uint64_t d0, uint64_t d1) {
    struct kvm_run *run = v->run;
    run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
    run->internal.suberror = suberror;
    run->internal.ndata = 3;
    run->internal.data[0] = d0;
    run->internal.data[1] = d1;
    run->internal.data[2] = kvm_arch->get_rip(v);
    return 0;
}

int kvm_handle_xsetbv(kvm_vcpu_t *v) {
    uint32_t idx = (uint32_t)v->regs.gpr[VCPU_REG_RCX];
    uint64_t val = (uint32_t)v->regs.gpr[VCPU_REG_RAX] | ((uint64_t)(uint32_t)v->regs.gpr[VCPU_REG_RDX] << 32);
    if (idx != 0 || kvm_arch->cpl(v) != 0 || !kvm_xcr0_valid(v, val)) {
        emu_inject_gp(v);
        return 1;
    }
    v->xcr0 = val;
    kvm_arch->skip_insn(v);
    return 1;
}

int kvm_handle_cpuid(kvm_vcpu_t *v) {
    uint32_t fn = (uint32_t)v->regs.gpr[VCPU_REG_RAX];
    uint32_t idx = (uint32_t)v->regs.gpr[VCPU_REG_RCX];
    uint32_t a = 0, b = 0, c = 0, d = 0;
    const struct kvm_cpuid_entry2 *e = kvm_find_cpuid(v, fn, idx);
    if (!e) {
        const struct kvm_cpuid_entry2 *base = kvm_find_cpuid(v, fn & 0xF0000000u, 0);
        uint32_t maxfn = base ? base->eax : 0;
        if (fn >= 0x40000000u && fn < 0x50000000u) {
        } else if (!base || fn > maxfn) {
            const struct kvm_cpuid_entry2 *b0 = kvm_find_cpuid(v, 0, 0);
            if (b0) e = kvm_find_cpuid(v, b0->eax, idx);
        }
    }
    if (e) { a = e->eax; b = e->ebx; c = e->ecx; d = e->edx; }
    if (fn == 0xD && idx == 0 && e) {
        b = fpu_xsave_size_for(v->xcr0 & fpu_xcr0);
    } else if (fn == 1) {
        c &= ~(1u << 27);
        if (v->cr4 & CR4_OSXSAVE) c |= 1u << 27;
        if (!(v->apic_base & APIC_BASE_ENABLE)) d &= ~(1u << 9);
    } else if (fn == 7 && idx == 0) {
        c &= ~(1u << 4);
    }
    v->regs.gpr[VCPU_REG_RAX] = a;
    v->regs.gpr[VCPU_REG_RBX] = b;
    v->regs.gpr[VCPU_REG_RCX] = c;
    v->regs.gpr[VCPU_REG_RDX] = d;
    kvm_arch->skip_insn(v);
    return 1;
}

static bool mtrr_index(uint32_t msr, uint64_t **slot, kvm_vcpu_t *v) {
    if (msr >= MSR_MTRR_PHYSBASE0 && msr < MSR_MTRR_PHYSBASE0 + 16) { *slot = &v->msr_mtrr_var[msr - MSR_MTRR_PHYSBASE0]; return true; }
    if (msr == MSR_MTRR_FIX64K) { *slot = &v->msr_mtrr_fixed[0]; return true; }
    if (msr == MSR_MTRR_FIX16K_80000) { *slot = &v->msr_mtrr_fixed[1]; return true; }
    if (msr == MSR_MTRR_FIX16K_A0000) { *slot = &v->msr_mtrr_fixed[2]; return true; }
    if (msr >= MSR_MTRR_FIX4K_C0000 && msr < MSR_MTRR_FIX4K_C0000 + 8) { *slot = &v->msr_mtrr_fixed[3 + msr - MSR_MTRR_FIX4K_C0000]; return true; }
    if (msr == MSR_MTRR_DEF_TYPE) { *slot = &v->msr_mtrr_def; return true; }
    return false;
}

int kvm_msr_get_common(kvm_vcpu_t *v, uint32_t msr, uint64_t *data, bool host) {
    (void)host;
    uint64_t *slot;
    switch (msr) {
    case MSR_IA32_TSC: *data = rdtsc_now() + (uint64_t)v->tsc_offset; return 0;
    case MSR_IA32_APIC_BASE: *data = v->apic_base; return 0;
    case MSR_IA32_TSC_DEADLINE:
        if (!v->vm->irqchip_split) return -1;
        *data = kvm_lapic_get_tscdeadline(v);
        return 0;
    case MSR_IA32_FEATURE_CTL: *data = v->msr_feature_control; return 0;
    case MSR_IA32_TSC_ADJUST: *data = v->msr_tsc_adjust; return 0;
    case MSR_IA32_PLATFORM_ID: *data = 0; return 0;
    case MSR_IA32_UCODE_REV: *data = 0x100000000ull; return 0;
    case MSR_MTRR_CAP: *data = 0x508; return 0;
    case MSR_IA32_MCG_CAP: *data = v->mcg_cap; return 0;
    case MSR_IA32_MCG_STATUS: *data = v->mcg_status; return 0;
    case MSR_IA32_MCG_CTL: *data = v->mcg_ctl; return 0;
    case MSR_IA32_MISC_ENABLE: *data = v->msr_misc_enable; return 0;
    case MSR_IA32_SPEC_CTRL: *data = v->msr_spec_ctrl; return 0;
    case MSR_KVM_SYSTEM_TIME_NEW:
    case MSR_KVM_SYSTEM_TIME:
        *data = v->msr_kvm_system_time;
        return 0;
    case MSR_KVM_WALL_CLOCK_NEW:
    case MSR_KVM_WALL_CLOCK:
        *data = v->msr_kvm_wall_clock;
        return 0;
    default:
        break;
    }
    if (mtrr_index(msr, &slot, v)) { *data = *slot; return 0; }
    if (msr >= MSR_IA32_MC0_CTL && msr < MSR_IA32_MC0_CTL + 4 * KVM_MCE_BANKS) {
        *data = v->mce_banks[msr - MSR_IA32_MC0_CTL];
        return 0;
    }
    return -1;
}

int kvm_msr_set_common(kvm_vcpu_t *v, uint32_t msr, uint64_t data, bool host) {
    uint64_t *slot;
    switch (msr) {
    case MSR_IA32_TSC:
        v->tsc_offset = kvm_tsc_offset_for_write(v, data, host);
        kvm_arch->apply_tsc_offset(v);
        v->pvclock_dirty = true;
        return 0;
    case MSR_IA32_TSC_DEADLINE:
        if (!v->vm->irqchip_split) return -1;
        kvm_lapic_set_tscdeadline(v, data);
        return 0;
    case MSR_IA32_APIC_BASE:
        if (!host && (data & ~(APIC_BASE_ADDR | APIC_BASE_ENABLE | APIC_BASE_EXTD | APIC_BASE_BSP)))
            return -1;
        if (!v->vm->irqchip_split) {
            if (!host && (data & APIC_BASE_EXTD)) return -1;
            v->apic_base = data;
            return 0;
        }
        return kvm_lapic_set_base(v, data, host);
    case MSR_IA32_FEATURE_CTL:
        if (!host && (v->msr_feature_control & FEATURE_CTL_LOCKED)) return -1;
        v->msr_feature_control = data;
        return 0;
    case MSR_IA32_TSC_ADJUST:
        if (!host) {
            v->tsc_offset += (int64_t)(data - v->msr_tsc_adjust);
            kvm_arch->apply_tsc_offset(v);
            v->pvclock_dirty = true;
        }
        v->msr_tsc_adjust = data;
        return 0;
    case MSR_IA32_UCODE_REV:
    case MSR_IA32_PLATFORM_ID:
    case MSR_IA32_PRED_CMD:
        return 0;
    case MSR_IA32_MCG_CAP:
        if (!host) return -1;
        v->mcg_cap = data;
        return 0;
    case MSR_IA32_MCG_STATUS: v->mcg_status = data; return 0;
    case MSR_IA32_MCG_CTL:
        if (data && data != ~0ull) return -1;
        v->mcg_ctl = data;
        return 0;
    case MSR_IA32_MISC_ENABLE: v->msr_misc_enable = data; return 0;
    case MSR_IA32_SPEC_CTRL: v->msr_spec_ctrl = data; return 0;
    case MSR_KVM_SYSTEM_TIME_NEW:
    case MSR_KVM_SYSTEM_TIME:
        v->msr_kvm_system_time = data;
        v->pvclock_gpa = (data & 1) ? (data & ~1ull) : 0;
        v->pvclock_dirty = true;
        return 0;
    case MSR_KVM_WALL_CLOCK_NEW:
    case MSR_KVM_WALL_CLOCK:
        v->msr_kvm_wall_clock = data;
        if (!host) {
            v->wall_clock_gpa = data;
            v->wall_clock_pending = true;
        }
        return 0;
    default:
        break;
    }
    if (mtrr_index(msr, &slot, v)) { *slot = data; return 0; }
    if (msr >= MSR_IA32_MC0_CTL && msr < MSR_IA32_MC0_CTL + 4 * KVM_MCE_BANKS) {
        uint32_t off = msr - MSR_IA32_MC0_CTL;
        if ((off & 3) == 1 && data && !host) return -1;
        v->mce_banks[off] = data;
        return 0;
    }
    return -1;
}

int kvm_handle_rdmsr(kvm_vcpu_t *v) {
    uint32_t msr = (uint32_t)v->regs.gpr[VCPU_REG_RCX];
    uint64_t val;
    int r = kvm_x2apic_msr(msr) ? kvm_x2apic_read(v, msr, &val) : kvm_arch->get_msr(v, msr, &val, false);
    if (r < 0) {
        emu_inject_gp(v);
        return 1;
    }
    v->regs.gpr[VCPU_REG_RAX] = (uint32_t)val;
    v->regs.gpr[VCPU_REG_RDX] = val >> 32;
    kvm_arch->skip_insn(v);
    return 1;
}

int kvm_handle_wrmsr(kvm_vcpu_t *v) {
    uint32_t msr = (uint32_t)v->regs.gpr[VCPU_REG_RCX];
    uint64_t val = (uint32_t)v->regs.gpr[VCPU_REG_RAX] | (v->regs.gpr[VCPU_REG_RDX] << 32);
    if (kvm_x2apic_msr(msr)) {
        int r = kvm_x2apic_write(v, msr, val);
        if (r < 0) {
            emu_inject_gp(v);
            return 1;
        }
        kvm_arch->skip_insn(v);
        return r;
    }
    if (kvm_arch->set_msr(v, msr, val, false) < 0) {
        emu_inject_gp(v);
        return 1;
    }
    kvm_arch->skip_insn(v);
    return 1;
}

static uint64_t amask_of(int addr_size) {
    return addr_size == 0 ? 0xFFFFull : addr_size == 1 ? 0xFFFFFFFFull : ~0ull;
}

int kvm_handle_pio(kvm_vcpu_t *v, uint16_t port, uint8_t size, bool in, bool string, bool rep,
                   int addr_size, int seg, uint64_t next_rip) {
    struct kvm_run *run = v->run;
    uint8_t *data = (uint8_t *)run + v->pio_data_off;

    if (!string) {
        if (!in) {
            uint64_t val = v->regs.gpr[VCPU_REG_RAX];
            kvm_finish_insn(v, next_rip);
            if (kvm_ioeventfd_signal(v->vm, true, port, size, val)) return 1;
            memcpy(data, &val, size);
            run->exit_reason = KVM_EXIT_IO;
            run->io.direction = KVM_EXIT_IO_OUT;
            run->io.size = size;
            run->io.port = port;
            run->io.count = 1;
            run->io.data_offset = v->pio_data_off;
            return 0;
        }
        v->pend.active = true;
        v->pend.kind = PEND_PIO;
        v->pend.string = false;
        v->pend.pio_in = true;
        v->pend.size = size;
        v->pend.next_rip = next_rip;
        run->exit_reason = KVM_EXIT_IO;
        run->io.direction = KVM_EXIT_IO_IN;
        run->io.size = size;
        run->io.port = port;
        run->io.count = 1;
        run->io.data_offset = v->pio_data_off;
        return 0;
    }

    uint64_t amask = amask_of(addr_size);
    uint64_t count = rep ? (v->regs.gpr[VCPU_REG_RCX] & amask) : 1;
    if (count == 0) { kvm_finish_insn(v, next_rip); return 1; }
    int ptr_reg = in ? VCPU_REG_RDI : VCPU_REG_RSI;
    uint64_t off = v->regs.gpr[ptr_reg] & amask;
    bool df = (kvm_arch->get_rflags(v) & RFLAGS_DF) != 0;
    uint64_t max_elems = 4096 / size;
    uint64_t n = count < max_elems ? count : max_elems;
    if (!df) {
        uint64_t room = (4096 - ((emu_seg_base(v, seg) + off) & 0xFFF)) / size;
        if (room && n > room) n = room;
    } else {
        n = 1;
    }
    if (n == 0) n = 1;

    v->pend.active = true;
    v->pend.kind = PEND_PIO;
    v->pend.string = true;
    v->pend.rep = rep;
    v->pend.pio_in = in;
    v->pend.size = size;
    v->pend.pio_count = (uint32_t)n;
    v->pend.pio_port = port;
    v->pend.pio_seg = seg;
    v->pend.addr_size = (uint8_t)addr_size;
    v->pend.next_rip = next_rip;

    if (!in) {
        for (uint64_t i = 0; i < n; i++) {
            uint64_t a = (off + (df ? -(int64_t)(i * size) : (int64_t)(i * size))) & amask;
            if (emu_read_guest_virt(v, emu_seg_base(v, seg) + a, data + i * size, size) < 0) {
                v->pend.active = false;
                return 1;
            }
        }
    }
    run->exit_reason = KVM_EXIT_IO;
    run->io.direction = in ? KVM_EXIT_IO_IN : KVM_EXIT_IO_OUT;
    run->io.size = size;
    run->io.port = port;
    run->io.count = (uint32_t)n;
    run->io.data_offset = v->pio_data_off;
    return 0;
}

static int pio_complete(kvm_vcpu_t *v) {
    kvm_pending_t *p = &v->pend;
    uint8_t *data = (uint8_t *)v->run + v->pio_data_off;
    p->active = false;
    if (!p->string) {
        if (!p->pio_in) return 1;
        uint64_t val = 0;
        memcpy(&val, data, p->size);
        uint64_t rax = v->regs.gpr[VCPU_REG_RAX];
        if (p->size == 1) rax = (rax & ~0xFFull) | (val & 0xFF);
        else if (p->size == 2) rax = (rax & ~0xFFFFull) | (val & 0xFFFF);
        else rax = (uint32_t)val;
        v->regs.gpr[VCPU_REG_RAX] = rax;
        kvm_finish_insn(v, p->next_rip);
        return 1;
    }
    uint64_t amask = amask_of(p->addr_size);
    int ptr_reg = p->pio_in ? VCPU_REG_RDI : VCPU_REG_RSI;
    uint64_t off = v->regs.gpr[ptr_reg] & amask;
    bool df = (kvm_arch->get_rflags(v) & RFLAGS_DF) != 0;
    uint64_t n = p->pio_count;
    if (p->pio_in) {
        for (uint64_t i = 0; i < n; i++) {
            uint64_t a = (off + (df ? -(int64_t)(i * p->size) : (int64_t)(i * p->size))) & amask;
            if (emu_write_guest_virt(v, emu_seg_base(v, p->pio_seg) + a, data + i * p->size, p->size) < 0)
                return 1;
        }
    }
    uint64_t delta = n * p->size;
    uint64_t noff = df ? off - delta : off + delta;
    v->regs.gpr[ptr_reg] = (v->regs.gpr[ptr_reg] & ~amask) | (noff & amask);
    if (p->rep) {
        uint64_t rcx = (v->regs.gpr[VCPU_REG_RCX] & amask) - n;
        v->regs.gpr[VCPU_REG_RCX] = (v->regs.gpr[VCPU_REG_RCX] & ~amask) | (rcx & amask);
        if (rcx & amask) return 1;
    }
    kvm_finish_insn(v, p->next_rip);
    return 1;
}

int kvm_complete_pending(kvm_vcpu_t *v) {
    if (!v->pend.active) return 1;
    if (v->pend.kind == PEND_PIO) return pio_complete(v);
    if (v->pend.kind == PEND_MMIO) return emu_mmio_complete(v);
    v->pend.active = false;
    return 1;
}

bool kvm_should_stop(kvm_vcpu_t *v) {
    task_t *me = (task_t *)percpu_current_task();
    if (v->run->immediate_exit) return true;
    if (me && (me->pending_kill || signal_pending_deliverable(me))) return true;
    return false;
}

static bool halt_events(kvm_vcpu_t *v, bool if_set) {
    uint32_t mp = __atomic_load_n(&v->mp_state, __ATOMIC_SEQ_CST);
    if (mp == KVM_MP_STATE_INIT_RECEIVED || mp == KVM_MP_STATE_SIPI_RECEIVED) return true;
    if (__atomic_load_n(&v->nmi_pending, __ATOMIC_SEQ_CST)) return true;
    if (!if_set) return false;
    if (v->pending_irq >= 0 && kvm_lapic_accept_pic(v)) return true;
    return kvm_lapic_pending(v) >= 0;
}

int kvm_vcpu_halt(kvm_vcpu_t *v) {
    bool if_set = (kvm_arch->get_rflags(v) & RFLAGS_IF) != 0;
    uint32_t expect = KVM_MP_STATE_RUNNABLE;
    __atomic_compare_exchange_n(&v->mp_state, &expect, KVM_MP_STATE_HALTED, false,
                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    for (;;) {
        kvm_lapic_timer_check(v, sched_now_ns());
        if (halt_events(v, if_set)) break;
        if (kvm_should_stop(v)) {
            v->run->exit_reason = KVM_EXIT_INTR;
            return -EINTR;
        }
        uint64_t f = spinlock_acquire_irqsave(&v->halt_lock);
        if (halt_events(v, if_set) || kvm_should_stop(v)) {
            spinlock_release_irqrestore(&v->halt_lock, f);
            continue;
        }
        waitq_block(&v->halt_wq, &v->halt_lock, f, kvm_lapic_timer_deadline(v));
    }
    expect = KVM_MP_STATE_HALTED;
    __atomic_compare_exchange_n(&v->mp_state, &expect, KVM_MP_STATE_RUNNABLE, false,
                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return 1;
}

static void init_reset(kvm_vcpu_t *v) {
    kvm_arch->init_reset(v);
    v->pending_irq = -1;
    v->exc_pending = false;
    v->nmi_pending = false;
    v->cr8 = 0;
    memset(&v->pend, 0, sizeof v->pend);
    kvm_lapic_reset(v, true);
}

int kvm_vcpu_mp_wait(kvm_vcpu_t *v) {
    if (__atomic_load_n(&v->mp_state, __ATOMIC_SEQ_CST) == KVM_MP_STATE_INIT_RECEIVED)
        init_reset(v);
    for (;;) {
        uint32_t mp = __atomic_load_n(&v->mp_state, __ATOMIC_SEQ_CST);
        if (mp == KVM_MP_STATE_SIPI_RECEIVED) {
            kvm_arch->sipi(v, (uint8_t)(v->sipi_vector & 0xFF));
            uint32_t expect = KVM_MP_STATE_SIPI_RECEIVED;
            __atomic_compare_exchange_n(&v->mp_state, &expect, KVM_MP_STATE_RUNNABLE, false,
                                        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
            return 1;
        }
        if (mp == KVM_MP_STATE_RUNNABLE || mp == KVM_MP_STATE_HALTED) return 1;
        if (kvm_should_stop(v)) {
            v->run->exit_reason = KVM_EXIT_INTR;
            return -EINTR;
        }
        uint64_t f = spinlock_acquire_irqsave(&v->halt_lock);
        mp = __atomic_load_n(&v->mp_state, __ATOMIC_SEQ_CST);
        if (mp == KVM_MP_STATE_SIPI_RECEIVED || mp == KVM_MP_STATE_RUNNABLE || kvm_should_stop(v)) {
            spinlock_release_irqrestore(&v->halt_lock, f);
            continue;
        }
        waitq_block(&v->halt_wq, &v->halt_lock, f, 0);
    }
}

void kvm_vcpu_reset_soft(kvm_vcpu_t *v) {
    v->cr8 = 0;
    v->apic_base = 0xFEE00000ull | APIC_BASE_ENABLE | (v->id == 0 ? APIC_BASE_BSP : 0);
    v->xcr0 = 1;
    memset(v->dr, 0, sizeof v->dr);
    v->dr6 = 0xFFFF0FF0ull;
    v->msr_star = v->msr_lstar = v->msr_cstar = v->msr_sfmask = 0;
    v->msr_kernel_gs_base = v->msr_tsc_aux = 0;
    v->msr_misc_enable = 1;
    v->msr_tsc_adjust = 0;
    v->msr_mtrr_def = 0;
    memset(v->msr_mtrr_fixed, 0, sizeof v->msr_mtrr_fixed);
    memset(v->msr_mtrr_var, 0, sizeof v->msr_mtrr_var);
    v->pending_irq = -1;
    v->nmi_pending = v->nmi_injected = v->nmi_masked = false;
    v->exc_pending = false;
    v->mp_state = KVM_MP_STATE_RUNNABLE;
    memset(&v->pend, 0, sizeof v->pend);
    kvm_lapic_reset(v, false);
    uint16_t fcw = 0x37F;
    uint32_t mxcsr = 0x1F80;
    memset(v->guest_fpu, 0, 4096);
    memcpy(v->guest_fpu, &fcw, 2);
    memcpy(v->guest_fpu + 24, &mxcsr, 4);
}

void kvm_guest_fpu_load(kvm_vcpu_t *v) {
    fpu_save((fpu_state_t *)v->host_fpu);
    fpu_restore((fpu_state_t *)v->guest_fpu);
}

void kvm_guest_fpu_put(kvm_vcpu_t *v) {
    fpu_save((fpu_state_t *)v->guest_fpu);
    fpu_restore((fpu_state_t *)v->host_fpu);
}
