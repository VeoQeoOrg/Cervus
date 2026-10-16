#include "../../include/virt/svm.h"
#include "../../include/virt/vmx.h"
#include "../../include/virt/kvm.h"
#include "../../include/memory/pmm.h"
#include "../../include/smp/percpu.h"
#include "../../include/smp/smp.h"
#include "../../include/sched/sched.h"
#include "../../include/io/serial.h"
#include "../../include/apic/apic.h"
#include "../../include/syscall/errno.h"
#include "../../include/sse/fpu.h"
#include "../../include/drivers/timer.h"
#include <string.h>

extern void svm_enter(vmx_regs_t *regs, uint64_t vmcb_pa, uint64_t host_vmcb_pa);
extern uint32_t smp_get_lapic_id_for_cpu(uint32_t cpu_index);

#define CR0_PE (1ull << 0)
#define CR0_PG (1ull << 31)

#define CR4_VME        (1ull << 0)
#define CR4_PVI        (1ull << 1)
#define CR4_TSD        (1ull << 2)
#define CR4_DE         (1ull << 3)
#define CR4_PSE        (1ull << 4)
#define CR4_PAE        (1ull << 5)
#define CR4_MCE        (1ull << 6)
#define CR4_PGE        (1ull << 7)
#define CR4_PCE        (1ull << 8)
#define CR4_OSFXSR     (1ull << 9)
#define CR4_OSXMMEXCPT (1ull << 10)
#define CR4_UMIP       (1ull << 11)
#define CR4_FSGSBASE   (1ull << 16)
#define CR4_PCIDE      (1ull << 17)
#define CR4_OSXSAVE    (1ull << 18)
#define CR4_SMEP       (1ull << 20)
#define CR4_SMAP       (1ull << 21)

#define EFER_SCE (1ull << 0)
#define EFER_LME (1ull << 8)
#define EFER_LMA (1ull << 10)
#define EFER_NXE (1ull << 11)

#define RFLAGS_IF (1ull << 9)

#define MSR_IA32_SYSENTER_CS  0x174
#define MSR_IA32_SYSENTER_ESP 0x175
#define MSR_IA32_SYSENTER_EIP 0x176
#define MSR_IA32_DEBUGCTL     0x1D9
#define MSR_IA32_PAT          0x277
#define MSR_IA32_TSC          0x010
#define MSR_IA32_TSC_ADJUST   0x03B
#define MSR_IA32_TSC_DEADLINE 0x6E0
#define MSR_IA32_MCG_STATUS   0x17A
#define MSR_IA32_MCG_CTL      0x17B
#define MSR_EFER              0xC0000080
#define MSR_STAR              0xC0000081
#define MSR_LSTAR             0xC0000082
#define MSR_CSTAR             0xC0000083
#define MSR_SFMASK            0xC0000084
#define MSR_FS_BASE           0xC0000100
#define MSR_GS_BASE           0xC0000101
#define MSR_KERNEL_GS_BASE    0xC0000102
#define MSR_TSC_AUX           0xC0000103

enum { SEG_ES, SEG_CS, SEG_SS, SEG_DS, SEG_FS, SEG_GS, SEG_LDTR, SEG_TR };

typedef struct {
    bool     probed;
    bool     ok;
    uint32_t features;
    uint32_t nasid;
    bool     rdtscp;
} svm_caps_t;

static svm_caps_t g_svm;
static void      *g_hsave[MAX_CPUS];
static void      *g_host_vmcb[MAX_CPUS];
static bool       g_svm_on[MAX_CPUS];
static kvm_vcpu_t *g_last_vcpu[MAX_CPUS];
static void      *g_iopm;
static void      *g_msrpm;
static spinlock_t g_asid_lock = SPINLOCK_INIT;
static uint32_t   g_asid_next = 1;

static inline uint64_t rdmsr64(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr64(uint32_t msr, uint64_t v) {
    asm volatile("wrmsr" :: "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)) : "memory");
}

static void cpuid_raw(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}

static inline void xsetbv0(uint64_t v) {
    asm volatile("xsetbv" :: "c"(0), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)) : "memory");
}

static inline vmcb_t *vmcb(kvm_vcpu_t *v) { return (vmcb_t *)v->vmcb; }

static void msrpm_pass(uint32_t msr) {
    uint32_t base;
    if (msr < 0x2000) base = 0;
    else if (msr >= 0xC0000000u && msr < 0xC0002000u) { base = 0x800; msr -= 0xC0000000u; }
    else if (msr >= 0xC0010000u && msr < 0xC0012000u) { base = 0x1000; msr -= 0xC0010000u; }
    else return;
    uint8_t *pm = (uint8_t *)g_msrpm + base + msr / 4;
    *pm &= (uint8_t)~(3u << ((msr % 4) * 2));
}

static bool svm_probe(void) {
    if (g_svm.probed) return g_svm.ok;
    g_svm.probed = true;
    uint32_t a, b, c, d;
    cpuid_raw(0x80000000u, 0, &a, &b, &c, &d);
    if (a < 0x8000000Au) return false;
    cpuid_raw(0x80000001u, 0, &a, &b, &c, &d);
    if (!(c & (1u << 2))) return false;
    g_svm.rdtscp = (d & (1u << 27)) != 0;
    if (rdmsr64(MSR_VM_CR) & VM_CR_SVMDIS) {
        serial_printf("[kvm] SVM is disabled by the firmware\n");
        return false;
    }
    cpuid_raw(0x8000000Au, 0, &a, &b, &c, &d);
    g_svm.features = d;
    g_svm.nasid = b;
    if (!(d & SVM_FEAT_NP) || b < 2) return false;

    uint32_t n = smp_get_cpu_count();
    for (uint32_t i = 0; i < n && i < MAX_CPUS; i++) {
        g_hsave[i] = pmm_alloc_zero(1);
        g_host_vmcb[i] = pmm_alloc_zero(1);
        if (!g_hsave[i] || !g_host_vmcb[i]) return false;
    }
    g_iopm = pmm_alloc_zero(3);
    g_msrpm = pmm_alloc_zero(2);
    if (!g_iopm || !g_msrpm) return false;
    memset(g_iopm, 0xFF, 3 * 4096);
    memset(g_msrpm, 0xFF, 2 * 4096);
    static const uint32_t pass[] = {
        MSR_FS_BASE, MSR_GS_BASE, MSR_KERNEL_GS_BASE, MSR_STAR, MSR_LSTAR, MSR_CSTAR, MSR_SFMASK,
        MSR_IA32_SYSENTER_CS, MSR_IA32_SYSENTER_ESP, MSR_IA32_SYSENTER_EIP,
    };
    for (uint32_t i = 0; i < sizeof pass / sizeof pass[0]; i++) msrpm_pass(pass[i]);

    g_svm.ok = true;
    serial_printf("[kvm] SVM: nested paging, %u ASIDs%s%s%s\n", g_svm.nasid,
                  (d & SVM_FEAT_NRIPS) ? ", next-RIP save" : "",
                  (d & SVM_FEAT_DECODE_ASSISTS) ? ", decode assists" : "",
                  (d & SVM_FEAT_FLUSH_BY_ASID) ? ", flush by ASID" : "");
    return true;
}

bool svm_supported(void) { return svm_probe(); }

static bool svm_cpu_enable(uint32_t cpu) {
    if (g_svm_on[cpu]) return true;
    if (!g_hsave[cpu]) return false;
    wrmsr64(MSR_EFER, rdmsr64(MSR_EFER) | EFER_SVME);
    wrmsr64(MSR_VM_HSAVE_PA, pmm_virt_to_phys(g_hsave[cpu]));
    g_svm_on[cpu] = true;
    return true;
}

static uint32_t asid_alloc(void) {
    uint64_t f = spinlock_acquire_irqsave(&g_asid_lock);
    uint32_t a = g_asid_next++;
    if (g_asid_next >= g_svm.nasid) g_asid_next = 1;
    spinlock_release_irqrestore(&g_asid_lock, f);
    return a;
}

static uint64_t svm_supported_cr4(void) {
    uint64_t s = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_PAE | CR4_MCE | CR4_PGE |
                 CR4_PCE | CR4_OSFXSR | CR4_OSXMMEXCPT;
    if (fpu_xcr0) s |= CR4_OSXSAVE;
    uint32_t a, b, c, d;
    cpuid_raw(7, 0, &a, &b, &c, &d);
    if (b & (1u << 0)) s |= CR4_FSGSBASE;
    if (b & (1u << 7)) s |= CR4_SMEP;
    if (b & (1u << 20)) s |= CR4_SMAP;
    if (c & (1u << 2)) s |= CR4_UMIP;
    cpuid_raw(1, 0, &a, &b, &c, &d);
    if (c & (1u << 17)) s |= CR4_PCIDE;
    return s;
}

static uint16_t seg_to_attr(const struct kvm_segment *s) {
    if (s->unusable || !s->present) return 0;
    return (uint16_t)((s->type & 0xF) | ((s->s & 1) << 4) | ((s->dpl & 3) << 5) | ((s->present & 1) << 7) |
                      ((s->avl & 1) << 8) | ((s->l & 1) << 9) | ((s->db & 1) << 10) | ((s->g & 1) << 11));
}

static vmcb_seg_t *seg_ptr(vmcb_t *c, int idx) {
    switch (idx) {
    case SEG_ES: return &c->save.es;
    case SEG_CS: return &c->save.cs;
    case SEG_SS: return &c->save.ss;
    case SEG_DS: return &c->save.ds;
    case SEG_FS: return &c->save.fs;
    case SEG_GS: return &c->save.gs;
    case SEG_LDTR: return &c->save.ldtr;
    default: return &c->save.tr;
    }
}

static void write_seg(vmcb_t *c, int idx, const struct kvm_segment *s) {
    vmcb_seg_t *g = seg_ptr(c, idx);
    g->selector = s->selector;
    g->base = s->base;
    g->limit = s->limit;
    g->attrib = seg_to_attr(s);
    if (idx == SEG_SS) c->save.cpl = s->dpl & 3;
}

static void read_seg(vmcb_t *c, int idx, struct kvm_segment *s) {
    vmcb_seg_t *g = seg_ptr(c, idx);
    memset(s, 0, sizeof *s);
    s->selector = g->selector;
    s->base = g->base;
    s->limit = g->limit;
    uint16_t a = g->attrib;
    s->type = a & 0xF;
    s->s = (a >> 4) & 1;
    s->dpl = (a >> 5) & 3;
    s->present = (a >> 7) & 1;
    s->avl = (a >> 8) & 1;
    s->l = (a >> 9) & 1;
    s->db = (a >> 10) & 1;
    s->g = (a >> 11) & 1;
    s->unusable = s->present ? 0 : 1;
    if (idx == SEG_CS || idx == SEG_TR) s->unusable = 0;
}

static void sync_from_vmcb(kvm_vcpu_t *v) {
    vmcb_t *c = vmcb(v);
    v->cr0 = c->save.cr0;
    v->cr4 = c->save.cr4;
    v->efer = c->save.efer & ~EFER_SVME;
}

static void reset_guest(kvm_vcpu_t *v) {
    vmcb_t *c = vmcb(v);
    struct kvm_segment cs = { .base = 0xFFFF0000ull, .limit = 0xFFFF, .selector = 0xF000,
                              .type = 0xB, .present = 1, .s = 1 };
    struct kvm_segment ds = { .base = 0, .limit = 0xFFFF, .selector = 0, .type = 3, .present = 1, .s = 1 };
    struct kvm_segment ldt = { .base = 0, .limit = 0xFFFF, .type = 2, .present = 1 };
    struct kvm_segment tr = { .base = 0, .limit = 0xFFFF, .type = 0xB, .present = 1 };
    write_seg(c, SEG_CS, &cs);
    write_seg(c, SEG_DS, &ds);
    write_seg(c, SEG_ES, &ds);
    write_seg(c, SEG_SS, &ds);
    write_seg(c, SEG_FS, &ds);
    write_seg(c, SEG_GS, &ds);
    write_seg(c, SEG_LDTR, &ldt);
    write_seg(c, SEG_TR, &tr);
    c->save.gdtr.base = 0;
    c->save.gdtr.limit = 0xFFFF;
    c->save.idtr.base = 0;
    c->save.idtr.limit = 0xFFFF;
    c->save.cpl = 0;
    c->save.efer = EFER_SVME;
    c->save.cr0 = 0x60000010ull;
    c->save.cr3 = 0;
    c->save.cr4 = 0;
    c->save.cr2 = 0;
    c->save.dr6 = 0xFFFF0FF0ull;
    c->save.dr7 = 0x400;
    c->save.rflags = 2;
    c->save.rip = 0xFFF0;
    c->save.rsp = 0;
    c->save.rax = 0;
    c->save.g_pat = 0x0007040600070406ull;
    c->save.dbgctl = 0;
    c->save.star = c->save.lstar = c->save.cstar = c->save.sfmask = c->save.kernel_gs_base = 0;
    c->save.sysenter_cs = c->save.sysenter_esp = c->save.sysenter_eip = 0;
    c->control.event_inj = 0;
    c->control.int_state = 0;
    c->control.int_ctl = SVM_V_INTR_MASKING;
    c->control.intercept_misc1 &= ~(SVM_INTERCEPT_VINTR | SVM_INTERCEPT_IRET);
    memset(&v->regs, 0, sizeof v->regs);
    v->regs.gpr[VCPU_REG_RDX] = 0x600;
    v->nmi_masked = false;
    sync_from_vmcb(v);
}

static void setup_control(kvm_vcpu_t *v) {
    vmcb_control_t *k = &vmcb(v)->control;
    k->intercept_cr = (1u << 8) | (1u << (16 + 4)) | (1u << (16 + 8));
    k->intercept_dr = 0xFFFFFFFFu;
    k->intercept_exceptions = 0;
    k->intercept_misc1 = SVM_INTERCEPT_INTR | SVM_INTERCEPT_NMI | SVM_INTERCEPT_SMI | SVM_INTERCEPT_INIT |
                         SVM_INTERCEPT_RDPMC | SVM_INTERCEPT_CPUID | SVM_INTERCEPT_INVD | SVM_INTERCEPT_HLT |
                         SVM_INTERCEPT_INVLPGA | SVM_INTERCEPT_IOIO | SVM_INTERCEPT_MSR | SVM_INTERCEPT_SHUTDOWN;
    k->intercept_misc2 = SVM_INTERCEPT_VMRUN | SVM_INTERCEPT_VMMCALL | SVM_INTERCEPT_VMLOAD |
                         SVM_INTERCEPT_VMSAVE | SVM_INTERCEPT_STGI | SVM_INTERCEPT_CLGI | SVM_INTERCEPT_SKINIT |
                         SVM_INTERCEPT_MONITOR | SVM_INTERCEPT_MWAIT | SVM_INTERCEPT_MWAIT_CND |
                         SVM_INTERCEPT_XSETBV;
    k->intercept_misc3 = 0;
    k->iopm_base_pa = pmm_virt_to_phys(g_iopm);
    k->msrpm_base_pa = pmm_virt_to_phys(g_msrpm);
    k->asid = v->asid;
    k->tlb_ctl = SVM_TLB_FLUSH_ALL;
    k->int_ctl = SVM_V_INTR_MASKING;
    k->nested_ctl = SVM_NP_ENABLE;
    k->nested_cr3 = pmm_virt_to_phys(v->vm->ept_root);
    v->tsc_offset = v->vm->tsc_offset;
    k->tsc_offset = (uint64_t)v->tsc_offset;
    k->clean = 0;
}

static void svm_state_enter(kvm_vcpu_t *v) { (void)v; }
static void svm_state_leave(kvm_vcpu_t *v) { (void)v; }

static int svm_vcpu_create(kvm_vcpu_t *v) {
    v->loaded_cpu = -1;
    v->last_cpu = -1;
    if (!svm_probe()) return -ENODEV;
    v->vmcb = pmm_alloc_zero(1);
    v->guest_fpu = pmm_alloc_zero(2);
    if (!v->vmcb || !v->guest_fpu) return -ENOMEM;
    v->host_fpu = v->guest_fpu + 4096;
    v->vmcb_phys = pmm_virt_to_phys(v->vmcb);
    v->asid = asid_alloc();
    v->mcg_cap = KVM_MCE_BANKS;
    kvm_vcpu_reset_soft(v);
    setup_control(v);
    reset_guest(v);
    return 0;
}

static void svm_vcpu_destroy(kvm_vcpu_t *v) {
    for (uint32_t i = 0; i < MAX_CPUS; i++)
        if (g_last_vcpu[i] == v) g_last_vcpu[i] = NULL;
    if (v->vmcb) pmm_free(v->vmcb, 1);
    if (v->guest_fpu) pmm_free(v->guest_fpu, 2);
    v->vmcb = NULL;
    v->guest_fpu = NULL;
}

static void guest_state_load(kvm_vcpu_t *v) {
    if (v->guest_loaded) return;
    kvm_guest_fpu_load(v);
    if (g_svm.rdtscp) {
        v->host_tsc_aux = rdmsr64(MSR_TSC_AUX);
        wrmsr64(MSR_TSC_AUX, v->msr_tsc_aux);
    }
    v->guest_loaded = true;
}

static void guest_state_put(kvm_vcpu_t *v) {
    if (!v->guest_loaded) return;
    kvm_guest_fpu_put(v);
    if (g_svm.rdtscp) wrmsr64(MSR_TSC_AUX, v->host_tsc_aux);
    v->guest_loaded = false;
}

static void svm_sched_out(kvm_vcpu_t *v) { guest_state_put(v); }
static void svm_sched_in(kvm_vcpu_t *v) { (void)v; }

static void svm_kick(kvm_vcpu_t *v) {
    int cpu = v->loaded_cpu;
    if (v->mode == VCPU_IN_GUEST && cpu >= 0 && (uint32_t)cpu != smp_cpu_index())
        ipi_reschedule_cpu(smp_get_lapic_id_for_cpu((uint32_t)cpu));
}

static uint64_t svm_get_rip(kvm_vcpu_t *v) { return vmcb(v)->save.rip; }
static void svm_set_rip(kvm_vcpu_t *v, uint64_t rip) { vmcb(v)->save.rip = rip; }
static uint64_t svm_get_rflags(kvm_vcpu_t *v) { return vmcb(v)->save.rflags; }
static void svm_set_rflags(kvm_vcpu_t *v, uint64_t fl) { vmcb(v)->save.rflags = fl; }
static uint64_t svm_get_cr3(kvm_vcpu_t *v) { return vmcb(v)->save.cr3; }
static uint64_t svm_get_dr7(kvm_vcpu_t *v) { return vmcb(v)->save.dr7; }
static void svm_set_dr7(kvm_vcpu_t *v, uint64_t val) { vmcb(v)->save.dr7 = (val & 0xFFFFFFFFull) | 0x400; }
static void svm_apply_tsc_offset(kvm_vcpu_t *v) { vmcb(v)->control.tsc_offset = (uint64_t)v->tsc_offset; }

static uint64_t svm_get_reg(kvm_vcpu_t *v, int reg) {
    if (reg == VCPU_REG_RSP) return vmcb(v)->save.rsp;
    return v->regs.gpr[reg & 15];
}

static void svm_set_reg(kvm_vcpu_t *v, int reg, uint64_t val) {
    if (reg == VCPU_REG_RSP) vmcb(v)->save.rsp = val;
    else v->regs.gpr[reg & 15] = val;
}

static uint64_t svm_seg_base(kvm_vcpu_t *v, int seg) {
    if (seg < 0 || seg > SEG_GS) return 0;
    return seg_ptr(vmcb(v), seg)->base;
}

static uint32_t svm_cs_mode(kvm_vcpu_t *v) {
    uint16_t a = vmcb(v)->save.cs.attrib;
    return (((a >> 9) & 1) ? KVM_CS_LONG : 0) | (((a >> 10) & 1) ? KVM_CS_DB : 0);
}

static int svm_cpl(kvm_vcpu_t *v) {
    if (!(vmcb(v)->save.cr0 & CR0_PE)) return 0;
    return vmcb(v)->save.cpl & 3;
}

static uint64_t svm_get_pdpte(kvm_vcpu_t *v, int i) {
    uint64_t e = 0;
    kvm_read_guest(v->vm, (vmcb(v)->save.cr3 & 0xFFFFFFE0ull) + (uint64_t)(i & 3) * 8, &e, 8);
    return e;
}

static void svm_clear_int_shadow(kvm_vcpu_t *v) { vmcb(v)->control.int_state &= ~SVM_INT_SHADOW; }

static void svm_skip_insn(kvm_vcpu_t *v) { kvm_finish_insn(v, v->next_rip); }

static void svm_init_reset(kvm_vcpu_t *v) {
    vmcb(v)->control.event_inj = 0;
    reset_guest(v);
}

static void svm_sipi(kvm_vcpu_t *v, uint8_t vec) {
    struct kvm_segment cs = { .base = (uint64_t)vec << 12, .limit = 0xFFFF,
                              .selector = (uint16_t)(vec << 8), .type = 0xB, .present = 1, .s = 1 };
    write_seg(vmcb(v), SEG_CS, &cs);
    vmcb(v)->save.rip = 0;
}

static void svm_adjust_cpuid(struct kvm_cpuid_entry2 *e) {
    if (e->function == 0x80000001u && !g_svm.rdtscp) e->edx &= ~(1u << 27);
}

static int svm_get_msr(kvm_vcpu_t *v, uint32_t msr, uint64_t *data, bool host) {
    vmcb_t *c = vmcb(v);
    switch (msr) {
    case MSR_IA32_SYSENTER_CS: *data = c->save.sysenter_cs; return 0;
    case MSR_IA32_SYSENTER_ESP: *data = c->save.sysenter_esp; return 0;
    case MSR_IA32_SYSENTER_EIP: *data = c->save.sysenter_eip; return 0;
    case MSR_IA32_DEBUGCTL: *data = c->save.dbgctl; return 0;
    case MSR_IA32_PAT: *data = c->save.g_pat; return 0;
    case MSR_EFER: *data = c->save.efer & ~EFER_SVME; return 0;
    case MSR_STAR: *data = c->save.star; return 0;
    case MSR_LSTAR: *data = c->save.lstar; return 0;
    case MSR_CSTAR: *data = c->save.cstar; return 0;
    case MSR_SFMASK: *data = c->save.sfmask; return 0;
    case MSR_FS_BASE: *data = c->save.fs.base; return 0;
    case MSR_GS_BASE: *data = c->save.gs.base; return 0;
    case MSR_KERNEL_GS_BASE: *data = c->save.kernel_gs_base; return 0;
    case MSR_TSC_AUX:
        if (!g_svm.rdtscp && !host) return -1;
        *data = v->msr_tsc_aux;
        return 0;
    case MSR_VM_CR:
    case MSR_VM_HSAVE_PA:
        return -1;
    default:
        break;
    }
    int r = kvm_msr_get_common(v, msr, data, host);
    if (r < 0 && msr >= 0xC0010000u && msr < 0xC0012000u) {
        *data = 0;
        return 0;
    }
    return r;
}

static int svm_set_msr(kvm_vcpu_t *v, uint32_t msr, uint64_t data, bool host) {
    vmcb_t *c = vmcb(v);
    switch (msr) {
    case MSR_IA32_SYSENTER_CS: c->save.sysenter_cs = data; return 0;
    case MSR_IA32_SYSENTER_ESP:
        if (!kvm_canonical(data)) return -1;
        c->save.sysenter_esp = data;
        return 0;
    case MSR_IA32_SYSENTER_EIP:
        if (!kvm_canonical(data)) return -1;
        c->save.sysenter_eip = data;
        return 0;
    case MSR_IA32_DEBUGCTL:
        if (data & ~3ull) return -1;
        c->save.dbgctl = 0;
        return 0;
    case MSR_IA32_PAT: c->save.g_pat = data; return 0;
    case MSR_EFER: {
        uint64_t allowed = EFER_SCE | EFER_LME | EFER_LMA | EFER_NXE;
        if (data & ~allowed) return -1;
        uint64_t cur = c->save.efer & ~EFER_SVME;
        if (!host && ((data ^ cur) & EFER_LME) && (c->save.cr0 & CR0_PG)) return -1;
        uint64_t n = host ? data : ((data & ~EFER_LMA) | (cur & EFER_LMA));
        c->save.efer = n | EFER_SVME;
        v->efer = n;
        return 0;
    }
    case MSR_STAR: c->save.star = data; return 0;
    case MSR_LSTAR:
        if (!kvm_canonical(data)) return -1;
        c->save.lstar = data;
        return 0;
    case MSR_CSTAR:
        if (!kvm_canonical(data)) return -1;
        c->save.cstar = data;
        return 0;
    case MSR_SFMASK: c->save.sfmask = data; return 0;
    case MSR_FS_BASE:
        if (!kvm_canonical(data)) return -1;
        c->save.fs.base = data;
        return 0;
    case MSR_GS_BASE:
        if (!kvm_canonical(data)) return -1;
        c->save.gs.base = data;
        return 0;
    case MSR_KERNEL_GS_BASE:
        if (!kvm_canonical(data)) return -1;
        c->save.kernel_gs_base = data;
        return 0;
    case MSR_TSC_AUX:
        if (!g_svm.rdtscp && !host) return -1;
        if (data >> 32) return -1;
        v->msr_tsc_aux = data;
        if (v->guest_loaded) wrmsr64(MSR_TSC_AUX, data);
        return 0;
    case MSR_VM_CR:
    case MSR_VM_HSAVE_PA:
        return -1;
    default:
        break;
    }
    int r = kvm_msr_set_common(v, msr, data, host);
    if (r < 0 && msr >= 0xC0010000u && msr < 0xC0012000u) return 0;
    return r;
}

static const uint32_t g_msr_list[] = {
    MSR_IA32_TSC, MSR_IA32_SYSENTER_CS, MSR_IA32_SYSENTER_ESP, MSR_IA32_SYSENTER_EIP,
    MSR_STAR, MSR_LSTAR, MSR_CSTAR, MSR_SFMASK, MSR_KERNEL_GS_BASE, MSR_TSC_AUX,
    MSR_IA32_TSC_ADJUST, MSR_IA32_PAT, MSR_IA32_MCG_STATUS, MSR_IA32_MCG_CTL, MSR_IA32_TSC_DEADLINE,
};

static uint32_t svm_msr_index_list(uint32_t *out, uint32_t max) {
    uint32_t n = sizeof g_msr_list / sizeof g_msr_list[0];
    for (uint32_t i = 0; i < n && i < max; i++) out[i] = g_msr_list[i];
    return n;
}

static bool guest_interruptible(kvm_vcpu_t *v) {
    vmcb_t *c = vmcb(v);
    return (c->save.rflags & RFLAGS_IF) && !(c->control.int_state & SVM_INT_SHADOW);
}

static bool ready_for_injection(kvm_vcpu_t *v) {
    if (vmcb(v)->control.event_inj & SVM_EVTINJ_VALID) return false;
    if (v->exc_pending || v->pending_irq >= 0) return false;
    if (!kvm_accepts_extint(v)) return false;
    return guest_interruptible(v);
}

static void set_misc1(vmcb_t *c, uint32_t bit, bool on) {
    if (on) c->control.intercept_misc1 |= bit;
    else c->control.intercept_misc1 &= ~bit;
}

static void request_irq_window(vmcb_t *c, bool on) {
    if (on) {
        c->control.int_ctl |= SVM_V_IRQ | SVM_V_IGN_TPR | (0xFu << SVM_V_INTR_PRIO_SHIFT);
        c->control.int_vector = 0;
    } else {
        c->control.int_ctl &= ~(SVM_V_IRQ | SVM_V_IGN_TPR | (0xFu << SVM_V_INTR_PRIO_SHIFT));
    }
    set_misc1(c, SVM_INTERCEPT_VINTR, on);
}

static void inject_events(kvm_vcpu_t *v) {
    vmcb_t *c = vmcb(v);
    bool irqchip = v->vm->irqchip_split;
    bool busy = (c->control.event_inj & SVM_EVTINJ_VALID) != 0;
    if (!busy && v->exc_pending) {
        uint32_t ev = SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_EXEPT | v->exc_vector;
        if (v->exc_has_error && (c->save.cr0 & CR0_PE)) {
            ev |= SVM_EVTINJ_VALID_ERR;
            c->control.event_inj_err = v->exc_error;
        }
        c->control.event_inj = ev;
        v->exc_pending = false;
        busy = true;
    }
    if (v->nmi_pending) {
        bool after_iret = v->nmi_iret_wait && c->save.rip == v->nmi_iret_rip;
        if (!busy && !v->nmi_masked && !after_iret && !(c->control.int_state & SVM_INT_SHADOW)) {
            c->control.event_inj = SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_NMI | 2;
            v->nmi_pending = false;
            v->nmi_masked = true;
            v->nmi_iret_wait = false;
            busy = true;
            set_misc1(c, SVM_INTERCEPT_IRET, true);
        } else if (v->nmi_masked) {
            set_misc1(c, SVM_INTERCEPT_IRET, true);
        }
    }
    bool open = !busy && guest_interruptible(v);
    if (open && v->pending_irq >= 0 && kvm_accepts_extint(v)) {
        c->control.event_inj = SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_INTR | (uint32_t)v->pending_irq;
        v->pending_irq = -1;
        busy = true;
    } else if (open && irqchip) {
        int vec = kvm_lapic_pending(v);
        if (vec >= 0) {
            kvm_lapic_ack(v, vec);
            c->control.event_inj = SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_INTR | (uint32_t)vec;
            busy = true;
        }
    }
    bool want = (v->pending_irq >= 0 && kvm_accepts_extint(v)) ||
                (v->run->request_interrupt_window && kvm_accepts_extint(v)) ||
                (irqchip && kvm_lapic_pending(v) >= 0) ||
                (v->nmi_pending && v->nmi_iret_wait);
    request_irq_window(c, want && (busy || !guest_interruptible(v)));
}

static void requeue_exit_event(kvm_vcpu_t *v) {
    vmcb_t *c = vmcb(v);
    uint32_t info = c->control.exit_int_info;
    c->control.event_inj = 0;
    if (!(info & SVM_EVTINJ_VALID)) return;
    uint32_t type = info & SVM_EVTINJ_TYPE_MASK;
    if (type == SVM_EVTINJ_TYPE_SOFT) return;
    c->control.event_inj = info;
    c->control.event_inj_err = c->control.exit_int_info_err;
}

static void fill_run_state(kvm_vcpu_t *v) {
    struct kvm_run *run = v->run;
    run->if_flag = (vmcb(v)->save.rflags & RFLAGS_IF) ? 1 : 0;
    run->cr8 = kvm_vcpu_cr8(v);
    run->apic_base = v->apic_base;
    run->ready_for_interrupt_injection = ready_for_injection(v) ? 1 : 0;
    run->flags = 0;
}

static bool decode_mov_crdr(kvm_vcpu_t *v, int *gpr, uint8_t *len) {
    vmcb_t *c = vmcb(v);
    uint8_t b[15];
    bool m64 = (v->efer & EFER_LMA) && (svm_cs_mode(v) & KVM_CS_LONG);
    uint64_t lin = c->save.cs.base + c->save.rip;
    if (!m64) lin &= 0xFFFFFFFFull;
    if (emu_read_guest_virt(v, lin, b, sizeof b) < 0) return false;
    int i = 0;
    uint8_t rex = 0;
    while (i < 14) {
        uint8_t p = b[i];
        if (p == 0x66 || p == 0x67 || p == 0x2E || p == 0x36 || p == 0x3E || p == 0x26 ||
            p == 0x64 || p == 0x65 || p == 0xF0 || p == 0xF2 || p == 0xF3) { i++; continue; }
        if (m64 && (p & 0xF0) == 0x40) { rex = p; i++; continue; }
        break;
    }
    if (i + 2 >= 15 || b[i] != 0x0F) return false;
    uint8_t op = b[i + 1];
    if (op < 0x20 || op > 0x23) return false;
    uint8_t modrm = b[i + 2];
    *gpr = (modrm & 7) | ((rex & 1) << 3);
    *len = (uint8_t)(i + 3);
    return true;
}

static int crdr_operand(kvm_vcpu_t *v, int *gpr) {
    vmcb_t *c = vmcb(v);
    uint8_t len = 0;
    if ((g_svm.features & SVM_FEAT_DECODE_ASSISTS) && (c->control.exit_info_1 >> 63)) {
        *gpr = (int)(c->control.exit_info_1 & 15);
        if (!(g_svm.features & SVM_FEAT_NRIPS) && !decode_mov_crdr(v, gpr, &len)) return -1;
    } else if (!decode_mov_crdr(v, gpr, &len)) {
        return -1;
    }
    if (!(g_svm.features & SVM_FEAT_NRIPS)) v->next_rip = c->save.rip + len;
    return 0;
}

static int handle_cr(kvm_vcpu_t *v, uint32_t code) {
    vmcb_t *c = vmcb(v);
    bool write = code >= SVM_EXIT_WRITE_CR0;
    int cr = (int)(code & 15);
    int gpr;
    if (crdr_operand(v, &gpr) < 0) return kvm_exit_internal(v, KVM_INTERNAL_ERROR_EMULATION, code, c->save.rip);
    if (!write) {
        if (cr == 8) svm_set_reg(v, gpr, kvm_vcpu_cr8(v));
        else { emu_inject_ud(v); return 1; }
        svm_skip_insn(v);
        return 1;
    }
    uint64_t val = svm_get_reg(v, gpr);
    if (cr == 4) {
        if (val & ~svm_supported_cr4()) { emu_inject_gp(v); return 1; }
        if ((v->efer & EFER_LMA) && !(val & CR4_PAE)) { emu_inject_gp(v); return 1; }
        if ((val & CR4_PCIDE) && !(v->cr4 & CR4_PCIDE) && (c->save.cr3 & 0xFFF)) { emu_inject_gp(v); return 1; }
        c->save.cr4 = val;
        v->cr4 = val;
        svm_skip_insn(v);
        return 1;
    }
    if (cr == 8) {
        uint64_t old = v->cr8;
        if (val & ~0xFull) { emu_inject_gp(v); return 1; }
        v->cr8 = val;
        svm_skip_insn(v);
        if (v->vm->irqchip_split) {
            kvm_lapic_set_cr8(v, (uint8_t)val);
            return 1;
        }
        if (val < old) {
            v->run->exit_reason = KVM_EXIT_SET_TPR;
            return 0;
        }
        return 1;
    }
    emu_inject_ud(v);
    return 1;
}

static int handle_dr(kvm_vcpu_t *v, uint32_t code) {
    vmcb_t *c = vmcb(v);
    bool write = code >= SVM_EXIT_WRITE_DR0;
    int dr = (int)(code & 15);
    int gpr;
    if (crdr_operand(v, &gpr) < 0) return kvm_exit_internal(v, KVM_INTERNAL_ERROR_EMULATION, code, c->save.rip);
    if ((dr == 4 || dr == 5) && (v->cr4 & CR4_DE)) { emu_inject_ud(v); return 1; }
    if (dr == 4) dr = 6;
    if (dr == 5) dr = 7;
    if (dr > 7) { emu_inject_ud(v); return 1; }
    if (!write) {
        uint64_t val = dr < 4 ? v->dr[dr] : dr == 6 ? c->save.dr6 : c->save.dr7;
        svm_set_reg(v, gpr, val);
    } else {
        uint64_t val = svm_get_reg(v, gpr);
        if (dr < 4) v->dr[dr] = val;
        else if (dr == 6) c->save.dr6 = (val & 0xFFFFull) | 0xFFFF0FF0ull;
        else c->save.dr7 = (val & 0xFFFFFFFFull) | 0x400;
    }
    svm_skip_insn(v);
    return 1;
}

static int handle_ioio(kvm_vcpu_t *v) {
    vmcb_t *c = vmcb(v);
    uint64_t info = c->control.exit_info_1;
    uint8_t size = (info & (1u << 4)) ? 1 : (info & (1u << 5)) ? 2 : 4;
    bool in = (info & SVM_IOIO_IN) != 0;
    bool string = (info & SVM_IOIO_STR) != 0;
    bool rep = (info & SVM_IOIO_REP) != 0;
    uint16_t port = (uint16_t)(info >> 16);
    int addr_size;
    if (info & (1u << 9)) addr_size = 2;
    else if (info & (1u << 8)) addr_size = 1;
    else if (info & (1u << 7)) addr_size = 0;
    else if ((v->efer & EFER_LMA) && (svm_cs_mode(v) & KVM_CS_LONG)) addr_size = 2;
    else addr_size = (svm_cs_mode(v) & KVM_CS_DB) ? 1 : 0;
    int seg = SEG_DS;
    if (in) seg = SEG_ES;
    else if (g_svm.features & SVM_FEAT_DECODE_ASSISTS) seg = (int)((info >> SVM_IOIO_SEG_SHIFT) & 7);
    if (seg > SEG_GS) seg = SEG_DS;
    return kvm_handle_pio(v, port, size, in, string, rep, addr_size, seg, c->control.exit_info_2);
}

static int handle_npf(kvm_vcpu_t *v) {
    vmcb_t *c = vmcb(v);
    uint64_t gpa = c->control.exit_info_2;
    uint64_t err = c->control.exit_info_1;
    int r = ept_fault(v, gpa, err);
    if (r == 0) return 1;
    if (r < 0 && r != -EFAULT) return kvm_exit_internal(v, KVM_INTERNAL_ERROR_EMULATION, gpa, err);
    return emu_mmio(v, gpa);
}

static void dump_guest(kvm_vcpu_t *v, uint32_t code) {
    vmcb_t *c = vmcb(v);
    serial_printf("[kvm] vcpu%u svm exit %x info1=%llx info2=%llx rip=%llx cs=%x/%llx attr=%x cr0=%llx cr4=%llx efer=%llx\n",
                  v->id, code, c->control.exit_info_1, c->control.exit_info_2, c->save.rip,
                  c->save.cs.selector, c->save.cs.base, c->save.cs.attrib, c->save.cr0, c->save.cr4, c->save.efer);
}

static uint64_t fixed_next_rip(kvm_vcpu_t *v, uint32_t code) {
    vmcb_t *c = vmcb(v);
    if (g_svm.features & SVM_FEAT_NRIPS) return c->control.next_rip;
    uint64_t len;
    switch (code) {
    case SVM_EXIT_HLT: len = 1; break;
    case SVM_EXIT_VMMCALL: case SVM_EXIT_XSETBV: case SVM_EXIT_MONITOR:
    case SVM_EXIT_MWAIT: case SVM_EXIT_MWAIT_COND: len = 3; break;
    default: len = 2; break;
    }
    return c->save.rip + len;
}

static int handle_exit(kvm_vcpu_t *v, uint32_t code) {
    vmcb_t *c = vmcb(v);
    struct kvm_run *run = v->run;
    v->next_rip = fixed_next_rip(v, code);
    if (code < SVM_EXIT_READ_DR0) return handle_cr(v, code);
    if (code < SVM_EXIT_EXCP_BASE) return handle_dr(v, code);
    switch (code) {
    case SVM_EXIT_INTR:
    case SVM_EXIT_NMI:
    case SVM_EXIT_SMI:
    case SVM_EXIT_INIT:
        return 1;
    case SVM_EXIT_VINTR:
        request_irq_window(c, false);
        if (run->request_interrupt_window && ready_for_injection(v)) {
            run->exit_reason = KVM_EXIT_IRQ_WINDOW_OPEN;
            return 0;
        }
        return 1;
    case SVM_EXIT_IRET:
        set_misc1(c, SVM_INTERCEPT_IRET, false);
        v->nmi_masked = false;
        v->nmi_iret_wait = true;
        v->nmi_iret_rip = c->save.rip;
        return 1;
    case SVM_EXIT_CPUID:
        return kvm_handle_cpuid(v);
    case SVM_EXIT_HLT:
        svm_skip_insn(v);
        if (v->vm->irqchip_split) return kvm_vcpu_halt(v);
        if (v->pending_irq >= 0 && guest_interruptible(v)) return 1;
        run->exit_reason = KVM_EXIT_HLT;
        return 0;
    case SVM_EXIT_INVD:
    case SVM_EXIT_PAUSE:
    case SVM_EXIT_MONITOR:
    case SVM_EXIT_MWAIT:
    case SVM_EXIT_MWAIT_COND:
        svm_skip_insn(v);
        return 1;
    case SVM_EXIT_VMMCALL:
        v->regs.gpr[VCPU_REG_RAX] = (uint64_t)-1000;
        svm_skip_insn(v);
        return 1;
    case SVM_EXIT_XSETBV:
        return kvm_handle_xsetbv(v);
    case SVM_EXIT_RDPMC:
        emu_inject_gp(v);
        return 1;
    case SVM_EXIT_VMRUN: case SVM_EXIT_VMLOAD: case SVM_EXIT_VMSAVE: case SVM_EXIT_STGI:
    case SVM_EXIT_CLGI: case SVM_EXIT_SKINIT: case SVM_EXIT_INVLPGA:
        emu_inject_ud(v);
        return 1;
    case SVM_EXIT_IOIO:
        return handle_ioio(v);
    case SVM_EXIT_MSR:
        return c->control.exit_info_1 ? kvm_handle_wrmsr(v) : kvm_handle_rdmsr(v);
    case SVM_EXIT_SHUTDOWN:
        run->exit_reason = KVM_EXIT_SHUTDOWN;
        return 0;
    case SVM_EXIT_NPF:
        return handle_npf(v);
    case SVM_EXIT_ERR:
        dump_guest(v, code);
        run->exit_reason = KVM_EXIT_FAIL_ENTRY;
        run->fail_entry.hardware_entry_failure_reason = code;
        run->fail_entry.cpu = smp_cpu_index();
        return 0;
    default:
        dump_guest(v, code);
        return kvm_exit_internal(v, KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON, code, c->control.exit_info_1);
    }
}

static int svm_vcpu_run(kvm_vcpu_t *v) {
    task_t *me = (task_t *)percpu_current_task();
    struct kvm_run *run = v->run;
    vmcb_t *c = vmcb(v);
    int r = 1;

    asm volatile("cli" ::: "memory");
    v->owner = me;
    me->kvm_vcpu = v;
    asm volatile("sti" ::: "memory");

    run->exit_reason = KVM_EXIT_UNKNOWN;
    r = kvm_complete_pending(v);
    if (r > 0 && v->vm->irqchip_split &&
        __atomic_load_n(&v->mp_state, __ATOMIC_SEQ_CST) == KVM_MP_STATE_HALTED)
        r = kvm_vcpu_halt(v);

    while (r > 0) {
        if (kvm_should_stop(v)) {
            run->exit_reason = KVM_EXIT_INTR;
            r = -EINTR;
            break;
        }
        if (v->vm->irqchip_split) {
            uint32_t mp = __atomic_load_n(&v->mp_state, __ATOMIC_SEQ_CST);
            if (mp != KVM_MP_STATE_RUNNABLE && mp != KVM_MP_STATE_HALTED) {
                r = kvm_vcpu_mp_wait(v);
                continue;
            }
            kvm_lapic_timer_check(v, sched_now_ns());
        }
        kvm_clock_sync(v);
        asm volatile("cli" ::: "memory");
        percpu_t *pc = get_percpu();
        if (pc->need_resched) {
            pc->need_resched = false;
            sched_reschedule();
            asm volatile("sti" ::: "memory");
            continue;
        }
        uint32_t cpu = smp_cpu_index();
        if (!svm_cpu_enable(cpu)) {
            asm volatile("sti" ::: "memory");
            run->exit_reason = KVM_EXIT_FAIL_ENTRY;
            run->fail_entry.cpu = cpu;
            r = 0;
            break;
        }
        guest_state_load(v);
        if (run->request_interrupt_window && ready_for_injection(v)) {
            asm volatile("sti" ::: "memory");
            run->exit_reason = KVM_EXIT_IRQ_WINDOW_OPEN;
            r = 0;
            break;
        }
        __atomic_and_fetch(&v->requests, ~VCPU_REQ_EVENT, __ATOMIC_ACQ_REL);
        inject_events(v);
        if (run->exit_reason == KVM_EXIT_SHUTDOWN) {
            asm volatile("sti" ::: "memory");
            r = 0;
            break;
        }
        v->loaded_cpu = (int)cpu;
        v->mode = VCPU_IN_GUEST;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        uint32_t req = __atomic_exchange_n(&v->requests, 0, __ATOMIC_ACQ_REL);
        if (v->last_cpu != (int)cpu || g_last_vcpu[cpu] != v || (req & VCPU_REQ_TLB_FLUSH) ||
            v->tlb_gen_seen != v->vm->tlb_gen) {
            v->last_cpu = (int)cpu;
            g_last_vcpu[cpu] = v;
            v->tlb_gen_seen = v->vm->tlb_gen;
            c->control.tlb_ctl = (g_svm.features & SVM_FEAT_FLUSH_BY_ASID) ? SVM_TLB_FLUSH_ASID : SVM_TLB_FLUSH_ALL;
        }
        if (req & VCPU_REQ_EVENT) {
            v->mode = VCPU_OUTSIDE_GUEST;
            asm volatile("sti" ::: "memory");
            continue;
        }
        c->save.rax = v->regs.gpr[VCPU_REG_RAX];
        c->save.cr2 = v->regs.cr2;
        c->control.clean = 0;
        bool xsw = fpu_xcr0 && (v->cr4 & CR4_OSXSAVE) && v->xcr0 != fpu_xcr0;
        if (xsw) xsetbv0(v->xcr0);
        svm_clgi();
        asm volatile("sti" ::: "memory");
        svm_enter(&v->regs, v->vmcb_phys, pmm_virt_to_phys(g_host_vmcb[cpu]));
        asm volatile("cli" ::: "memory");
        svm_stgi();
        c->control.tlb_ctl = SVM_TLB_NONE;
        if (xsw) xsetbv0(fpu_xcr0);
        v->mode = VCPU_OUTSIDE_GUEST;
        v->regs.gpr[VCPU_REG_RAX] = c->save.rax;
        v->regs.cr2 = c->save.cr2;
        sync_from_vmcb(v);
        uint32_t code = c->control.exit_code;
        requeue_exit_event(v);
        asm volatile("sti" ::: "memory");
        r = handle_exit(v, code);
        if (run->exit_reason == KVM_EXIT_SHUTDOWN) r = 0;
    }

    asm volatile("cli" ::: "memory");
    fill_run_state(v);
    guest_state_put(v);
    me->kvm_vcpu = NULL;
    v->loaded_cpu = -1;
    asm volatile("sti" ::: "memory");
    return r < 0 ? r : 0;
}

static void svm_get_regs(kvm_vcpu_t *v, struct kvm_regs *r) {
    vmcb_t *c = vmcb(v);
    r->rax = v->regs.gpr[VCPU_REG_RAX]; r->rbx = v->regs.gpr[VCPU_REG_RBX];
    r->rcx = v->regs.gpr[VCPU_REG_RCX]; r->rdx = v->regs.gpr[VCPU_REG_RDX];
    r->rsi = v->regs.gpr[VCPU_REG_RSI]; r->rdi = v->regs.gpr[VCPU_REG_RDI];
    r->rbp = v->regs.gpr[VCPU_REG_RBP];
    r->r8 = v->regs.gpr[8];   r->r9 = v->regs.gpr[9];
    r->r10 = v->regs.gpr[10]; r->r11 = v->regs.gpr[11];
    r->r12 = v->regs.gpr[12]; r->r13 = v->regs.gpr[13];
    r->r14 = v->regs.gpr[14]; r->r15 = v->regs.gpr[15];
    r->rsp = c->save.rsp;
    r->rip = c->save.rip;
    r->rflags = c->save.rflags;
}

static void svm_set_regs(kvm_vcpu_t *v, const struct kvm_regs *r) {
    vmcb_t *c = vmcb(v);
    v->regs.gpr[VCPU_REG_RAX] = r->rax; v->regs.gpr[VCPU_REG_RBX] = r->rbx;
    v->regs.gpr[VCPU_REG_RCX] = r->rcx; v->regs.gpr[VCPU_REG_RDX] = r->rdx;
    v->regs.gpr[VCPU_REG_RSI] = r->rsi; v->regs.gpr[VCPU_REG_RDI] = r->rdi;
    v->regs.gpr[VCPU_REG_RBP] = r->rbp;
    v->regs.gpr[8] = r->r8;   v->regs.gpr[9] = r->r9;
    v->regs.gpr[10] = r->r10; v->regs.gpr[11] = r->r11;
    v->regs.gpr[12] = r->r12; v->regs.gpr[13] = r->r13;
    v->regs.gpr[14] = r->r14; v->regs.gpr[15] = r->r15;
    c->save.rsp = r->rsp;
    c->save.rip = r->rip;
    c->save.rflags = (r->rflags & 0x3F7FD7ull) | 2;
    v->pend.active = false;
}

static void svm_get_sregs(kvm_vcpu_t *v, struct kvm_sregs *s) {
    vmcb_t *c = vmcb(v);
    memset(s, 0, sizeof *s);
    read_seg(c, SEG_CS, &s->cs);
    read_seg(c, SEG_DS, &s->ds);
    read_seg(c, SEG_ES, &s->es);
    read_seg(c, SEG_FS, &s->fs);
    read_seg(c, SEG_GS, &s->gs);
    read_seg(c, SEG_SS, &s->ss);
    read_seg(c, SEG_TR, &s->tr);
    read_seg(c, SEG_LDTR, &s->ldt);
    s->ss.dpl = c->save.cpl & 3;
    s->gdt.base = c->save.gdtr.base;
    s->gdt.limit = (uint16_t)c->save.gdtr.limit;
    s->idt.base = c->save.idtr.base;
    s->idt.limit = (uint16_t)c->save.idtr.limit;
    s->cr0 = c->save.cr0;
    s->cr2 = v->regs.cr2;
    s->cr3 = c->save.cr3;
    s->cr4 = c->save.cr4;
    s->cr8 = kvm_vcpu_cr8(v);
    s->efer = c->save.efer & ~EFER_SVME;
    s->apic_base = v->apic_base;
    uint32_t ev = c->control.event_inj;
    if (v->pending_irq >= 0) {
        uint32_t n = (uint32_t)v->pending_irq;
        s->interrupt_bitmap[n / 64] |= 1ull << (n % 64);
    } else if ((ev & SVM_EVTINJ_VALID) && (ev & SVM_EVTINJ_TYPE_MASK) == SVM_EVTINJ_TYPE_INTR) {
        uint32_t n = ev & 0xFF;
        s->interrupt_bitmap[n / 64] |= 1ull << (n % 64);
    }
}

static int svm_set_sregs(kvm_vcpu_t *v, const struct kvm_sregs *s) {
    if (s->cr4 & ~svm_supported_cr4()) return -EINVAL;
    vmcb_t *c = vmcb(v);
    write_seg(c, SEG_CS, &s->cs);
    write_seg(c, SEG_DS, &s->ds);
    write_seg(c, SEG_ES, &s->es);
    write_seg(c, SEG_FS, &s->fs);
    write_seg(c, SEG_GS, &s->gs);
    write_seg(c, SEG_SS, &s->ss);
    struct kvm_segment tr = s->tr;
    if (!tr.present || tr.unusable) { tr.present = 1; tr.unusable = 0; tr.type = 0xB; tr.s = 0; }
    write_seg(c, SEG_TR, &tr);
    write_seg(c, SEG_LDTR, &s->ldt);
    if (!(s->cr0 & CR0_PE)) c->save.cpl = 0;
    c->save.gdtr.base = s->gdt.base;
    c->save.gdtr.limit = s->gdt.limit;
    c->save.idtr.base = s->idt.base;
    c->save.idtr.limit = s->idt.limit;
    v->regs.cr2 = s->cr2;
    c->save.cr3 = s->cr3;
    v->cr8 = s->cr8;
    v->apic_base = s->apic_base;
    if (v->vm->irqchip_split) kvm_lapic_set_cr8(v, (uint8_t)(s->cr8 & 0xF));
    uint64_t efer = s->efer;
    if ((s->cr0 & CR0_PG) && (efer & EFER_LME)) efer |= EFER_LMA;
    else efer &= ~EFER_LMA;
    c->save.efer = efer | EFER_SVME;
    c->save.cr4 = s->cr4;
    c->save.cr0 = s->cr0 | (1ull << 4);
    sync_from_vmcb(v);
    for (int i = 0; i < 4; i++) {
        if (!s->interrupt_bitmap[i]) continue;
        v->pending_irq = i * 64 + __builtin_ctzll(s->interrupt_bitmap[i]);
        break;
    }
    return 0;
}

static void svm_get_events(kvm_vcpu_t *v, struct kvm_vcpu_events *e) {
    vmcb_t *c = vmcb(v);
    memset(e, 0, sizeof *e);
    uint32_t ev = c->control.event_inj;
    if (ev & SVM_EVTINJ_VALID) {
        uint32_t type = ev & SVM_EVTINJ_TYPE_MASK;
        if (type == SVM_EVTINJ_TYPE_EXEPT) {
            e->exception.injected = 1;
            e->exception.nr = ev & 0xFF;
            e->exception.has_error_code = (ev & SVM_EVTINJ_VALID_ERR) ? 1 : 0;
            e->exception.error_code = c->control.event_inj_err;
        } else if (type == SVM_EVTINJ_TYPE_INTR || type == SVM_EVTINJ_TYPE_SOFT) {
            e->interrupt.injected = 1;
            e->interrupt.nr = ev & 0xFF;
            e->interrupt.soft = type == SVM_EVTINJ_TYPE_SOFT;
        } else if (type == SVM_EVTINJ_TYPE_NMI) {
            e->nmi.injected = 1;
        }
    }
    if (v->exc_pending) {
        e->exception.injected = 1;
        e->exception.nr = v->exc_vector;
        e->exception.has_error_code = v->exc_has_error;
        e->exception.error_code = v->exc_error;
    }
    if (v->pending_irq >= 0) {
        e->interrupt.injected = 1;
        e->interrupt.nr = (uint8_t)v->pending_irq;
    }
    if (c->control.int_state & SVM_INT_SHADOW) e->interrupt.shadow |= KVM_X86_SHADOW_INT_STI;
    e->nmi.pending = v->nmi_pending;
    e->nmi.masked = v->nmi_masked ? 1 : 0;
    e->sipi_vector = v->sipi_vector;
    e->flags = KVM_VCPUEVENT_VALID_NMI_PENDING | KVM_VCPUEVENT_VALID_SIPI_VECTOR | KVM_VCPUEVENT_VALID_SHADOW;
}

static int svm_set_events(kvm_vcpu_t *v, const struct kvm_vcpu_events *e) {
    vmcb_t *c = vmcb(v);
    c->control.event_inj = 0;
    v->exc_pending = false;
    if (e->exception.injected || e->exception.pending) {
        v->exc_pending = true;
        v->exc_vector = e->exception.nr;
        v->exc_has_error = e->exception.has_error_code;
        v->exc_error = e->exception.error_code;
    }
    v->pending_irq = e->interrupt.injected ? e->interrupt.nr : -1;
    if (e->flags & KVM_VCPUEVENT_VALID_SHADOW) {
        if (e->interrupt.shadow) c->control.int_state |= SVM_INT_SHADOW;
        else c->control.int_state &= ~SVM_INT_SHADOW;
    }
    v->nmi_masked = e->nmi.masked != 0;
    if (e->flags & KVM_VCPUEVENT_VALID_NMI_PENDING) v->nmi_pending = e->nmi.pending;
    if (e->nmi.injected) v->nmi_pending = true;
    if (e->flags & KVM_VCPUEVENT_VALID_SIPI_VECTOR) v->sipi_vector = e->sipi_vector;
    return 0;
}

const kvm_arch_ops_t svm_ops = {
    .name = "SVM",
    .vcpu_create = svm_vcpu_create,
    .vcpu_destroy = svm_vcpu_destroy,
    .vcpu_run = svm_vcpu_run,
    .sched_out = svm_sched_out,
    .sched_in = svm_sched_in,
    .state_enter = svm_state_enter,
    .state_leave = svm_state_leave,
    .kick = svm_kick,
    .get_regs = svm_get_regs,
    .set_regs = svm_set_regs,
    .get_sregs = svm_get_sregs,
    .set_sregs = svm_set_sregs,
    .get_events = svm_get_events,
    .set_events = svm_set_events,
    .get_msr = svm_get_msr,
    .set_msr = svm_set_msr,
    .get_dr7 = svm_get_dr7,
    .set_dr7 = svm_set_dr7,
    .supported_cr4 = svm_supported_cr4,
    .adjust_cpuid = svm_adjust_cpuid,
    .msr_index_list = svm_msr_index_list,
    .apply_tsc_offset = svm_apply_tsc_offset,
    .get_rip = svm_get_rip,
    .set_rip = svm_set_rip,
    .get_rflags = svm_get_rflags,
    .set_rflags = svm_set_rflags,
    .get_reg = svm_get_reg,
    .set_reg = svm_set_reg,
    .seg_base = svm_seg_base,
    .cs_mode = svm_cs_mode,
    .cpl = svm_cpl,
    .get_cr3 = svm_get_cr3,
    .get_pdpte = svm_get_pdpte,
    .clear_int_shadow = svm_clear_int_shadow,
    .skip_insn = svm_skip_insn,
    .init_reset = svm_init_reset,
    .sipi = svm_sipi,
};
